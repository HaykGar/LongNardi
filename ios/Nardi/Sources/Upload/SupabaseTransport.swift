import Foundation
import Security

extension GameUploader {
    /// UserDefaults key for "Share anonymous game data" (default on). #5 binds
    /// the Settings toggle to this key; it is read at queue time.
    static let sharingDefaultsKey = "nardi.shareGameData"

    /// The app's uploader: Supabase transport from Info.plist (games stay queued
    /// silently when unconfigured). Retries pending games on first use (launch)
    /// and whenever the network returns.
    static let shared: GameUploader = {
        let info = Bundle.main.infoDictionary ?? [:]
        let up = GameUploader(
            directory: defaultDirectory,
            transport: SupabaseTransport.Config.fromBundle().map { SupabaseTransport(config: $0) },
            appVersion: info["CFBundleShortVersionString"] as? String ?? "",
            build: info["CFBundleVersion"] as? String ?? "",
            sharingEnabled: {
                UserDefaults.standard.object(forKey: sharingDefaultsKey) as? Bool ?? true
            })
        Task.detached(priority: .utility) { await up.start() }
        return up
    }()
}

/// Real uploader transport: Supabase anonymous auth (the install ID) plus a
/// PostgREST insert into `games`, over plain URLSession (no SDK dependency).
/// The session's refresh token lives in the Keychain so the install ID survives
/// relaunches (a reinstall may produce a new one — fine per GLOSSARY).
final class SupabaseTransport: GameUploadTransport, @unchecked Sendable {
    struct Config: Sendable {
        let url: URL
        let anonKey: String

        /// Reads `SupabaseURL` / `SupabaseAnonKey` from Info.plist (populated from
        /// the gitignored Config/Supabase.xcconfig). nil when not configured.
        static func fromBundle(_ bundle: Bundle = .main) -> Config? {
            guard let u = bundle.object(forInfoDictionaryKey: "SupabaseURL") as? String,
                  let k = bundle.object(forInfoDictionaryKey: "SupabaseAnonKey") as? String,
                  !k.isEmpty, !k.hasPrefix("$("), u.hasPrefix("https://"),
                  let url = URL(string: u), url.host != nil else { return nil }
            return Config(url: url, anonKey: k)
        }
    }

    enum TransportError: Error { case http(Int), malformed }

    private struct Session: Codable {
        var accessToken: String
        var refreshToken: String
        var userID: String
        var expiresAt: Date
    }

    private let config: Config
    private let urlSession: URLSession
    private let lock = NSLock()
    private var cached: Session?

    init(config: Config, urlSession: URLSession = .shared) {
        self.config = config
        self.urlSession = urlSession
        self.cached = Keychain.load().flatMap { try? JSONDecoder().decode(Session.self, from: $0) }
    }

    // MARK: GameUploadTransport

    func installID() async throws -> String {
        try await validSession().userID
    }

    func insert(row: Data) async throws -> InsertOutcome {
        let s = try await validSession()
        var req = request("rest/v1/games", bearer: s.accessToken)
        req.setValue("return=minimal", forHTTPHeaderField: "Prefer")
        req.httpBody = row
        let (body, status) = try await send(req)
        switch status {
        case 200..<300: return .inserted
        case 409: return .duplicate
        default:
            if String(data: body, encoding: .utf8)?.contains("23505") == true { return .duplicate }
            if status == 401 { setSession(nil, keepRefresh: true) }   // force a refresh next time
            // Other 4xx (bad row, RLS, too large) will never succeed: tell the
            // uploader to set the row aside. 408/429 and 5xx are transient.
            if (400..<500).contains(status), ![401, 408, 429].contains(status) {
                throw UploadRejected(status: status)
            }
            throw TransportError.http(status)
        }
    }

    // MARK: Auth

    private func validSession() async throws -> Session {
        let current = lock.withLock { cached }
        if let s = current, s.expiresAt > Date().addingTimeInterval(60) { return s }
        if let s = current {
            do {
                return try await refresh(s.refreshToken)
            } catch TransportError.http(let status) where status == 400 || status == 401 {
                // Refresh token definitively invalid: only now take a new identity.
            }
            // Any other failure (offline, 5xx, malformed) propagates: the game
            // stays queued and the existing install ID is kept.
        }
        return try await signUpAnonymously()
    }

    private func signUpAnonymously() async throws -> Session {
        var req = request("auth/v1/signup", bearer: config.anonKey)
        req.httpBody = Data("{}".utf8)
        return try await storeSession(from: req)
    }

    private func refresh(_ token: String) async throws -> Session {
        var req = request("auth/v1/token?grant_type=refresh_token", bearer: config.anonKey)
        req.httpBody = try JSONEncoder().encode(["refresh_token": token])
        return try await storeSession(from: req)
    }

    private func storeSession(from req: URLRequest) async throws -> Session {
        let (body, status) = try await send(req)
        guard (200..<300).contains(status) else { throw TransportError.http(status) }
        struct Auth: Decodable {
            struct User: Decodable { let id: String }
            let access_token: String
            let refresh_token: String
            let expires_in: Double
            let user: User
        }
        guard let a = try? JSONDecoder().decode(Auth.self, from: body) else { throw TransportError.malformed }
        let s = Session(accessToken: a.access_token, refreshToken: a.refresh_token,
                        userID: a.user.id, expiresAt: Date().addingTimeInterval(a.expires_in))
        setSession(s, keepRefresh: false)
        return s
    }

    /// keepRefresh: expire the access token but keep the refresh token / uid.
    private func setSession(_ s: Session?, keepRefresh: Bool) {
        let toStore: Session? = lock.withLock {
            if keepRefresh, var c = cached { c.expiresAt = .distantPast; cached = c } else { cached = s }
            return cached
        }
        if let toStore, let data = try? JSONEncoder().encode(toStore) { Keychain.save(data) }
    }

    // MARK: HTTP

    private func request(_ path: String, bearer: String) -> URLRequest {
        var req = URLRequest(url: URL(string: path, relativeTo: config.url)!)
        req.httpMethod = "POST"
        req.timeoutInterval = 30
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.setValue(config.anonKey, forHTTPHeaderField: "apikey")
        req.setValue("Bearer \(bearer)", forHTTPHeaderField: "Authorization")
        return req
    }

    private func send(_ req: URLRequest) async throws -> (Data, Int) {
        let (data, resp) = try await urlSession.data(for: req)
        return (data, (resp as? HTTPURLResponse)?.statusCode ?? 0)
    }
}

/// Minimal generic-password Keychain slot for the anonymous auth session.
private enum Keychain {
    private static let base: [String: Any] = [
        kSecClass as String: kSecClassGenericPassword,
        kSecAttrService as String: "com.haykg.nardi.supabase",
        kSecAttrAccount as String: "anonymous-session",
    ]

    static func load() -> Data? {
        var q = base
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: CFTypeRef?
        return SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess ? out as? Data : nil
    }

    static func save(_ data: Data) {
        let attrs: [String: Any] = [
            kSecValueData as String: data,
            kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
        ]
        if SecItemUpdate(base as CFDictionary, attrs as CFDictionary) == errSecItemNotFound {
            SecItemAdd(base.merging(attrs) { $1 } as CFDictionary, nil)
        }
    }
}
