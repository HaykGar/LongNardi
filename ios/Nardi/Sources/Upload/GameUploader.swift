import Foundation
import Network

// MARK: - Input

/// One turn of a finished game in the cloud format (ADR 0002): the two dice plus
/// the ordered engine-native moves `[start, die_idx]`. A pass has no moves.
/// Encodes as `{"d":[a,b],"m":[[start,die_idx],...]}`.
struct RecordedTurn: Codable, Equatable, Sendable {
    var dice: [Int]
    var moves: [[Int]]

    enum CodingKeys: String, CodingKey {
        case dice = "d"
        case moves = "m"
    }
}

enum BotLevel: Sendable { case easy, medium, hard }

/// Who sits in seat 2 (GLOSSARY: opponent kind).
enum OpponentKind: Sendable {
    case bot(BotLevel)
    case localHuman
}

/// A finished game as seen from seat 1 (the device's player; White in pass & play).
struct FinishedGame: Sendable {
    var id: UUID
    var opponent: OpponentKind
    var userFirst: Bool
    var seat1Won: Bool
    var mars: Bool
    var resigned: Bool
    var finishedAt: Date
    var turns: [RecordedTurn]
}

// MARK: - Transport

enum InsertOutcome: Sendable {
    case inserted
    /// The row's id already exists (Postgres 23505 / HTTP 409): a previous
    /// attempt landed, so this counts as success.
    case duplicate
}

/// Where rows go. The real one is `SupabaseTransport`; tests use a fake.
/// Thrown by a transport when the server refuses a row for good (a 4xx other
/// than auth/timeout/rate-limit): retrying the same row can never succeed.
struct UploadRejected: Error, Equatable {
    let status: Int
}

protocol GameUploadTransport: Sendable {
    /// The anonymous install ID (auth uid) that owns seat 1.
    func installID() async throws -> String
    /// Insert one `games` row (JSON object). Throws on any failure to retry later.
    func insert(row: Data) async throws -> InsertOutcome
}

// MARK: - Row

/// The `games` row minus `seat1_user`, which is only known once signed in.
struct GameRow: Codable, Sendable {
    var id: UUID
    var opponentKind: String
    var botModel: String?
    var userFirst: Bool
    var outcome: Int
    var resigned: Bool
    var appVersion: String
    var build: String
    var finishedAt: String
    var turns: [RecordedTurn]

    init(_ g: FinishedGame, appVersion: String, build: String) {
        id = g.id
        switch g.opponent {
        case .localHuman: opponentKind = "local_human"; botModel = nil
        case .bot(.easy): opponentKind = "bot_easy"; botModel = "heuristic"
        case .bot(.medium): opponentKind = "bot_medium"; botModel = "mlp"
        case .bot(.hard): opponentKind = "bot_hard"; botModel = "vzg0"
        }
        userFirst = g.userFirst
        outcome = (g.seat1Won ? 1 : -1) * (g.mars ? 2 : 1)
        resigned = g.resigned
        self.appVersion = appVersion
        self.build = build
        let f = ISO8601DateFormatter()
        f.formatOptions = [.withInternetDateTime]
        finishedAt = f.string(from: g.finishedAt)
        turns = g.turns
    }

    /// The JSON object sent to `games`, with seat 1 filled in and seat 2 null.
    func payload(seat1User: String) throws -> Data {
        try JSONEncoder().encode(Payload(row: self, seat1User: seat1User))
    }

    private struct Payload: Encodable {
        let row: GameRow
        let seat1User: String
        enum K: String, CodingKey {
            case id, seat1_user, seat2_user, opponent_kind, bot_model, user_first,
                 outcome, resigned, app_version, build, finished_at, turns
        }
        func encode(to encoder: Encoder) throws {
            var c = encoder.container(keyedBy: K.self)
            try c.encode(row.id.uuidString.lowercased(), forKey: .id)
            try c.encode(seat1User, forKey: .seat1_user)
            try c.encodeNil(forKey: .seat2_user)
            try c.encode(row.opponentKind, forKey: .opponent_kind)
            if let m = row.botModel { try c.encode(m, forKey: .bot_model) } else { try c.encodeNil(forKey: .bot_model) }
            try c.encode(row.userFirst, forKey: .user_first)
            try c.encode(row.outcome, forKey: .outcome)
            try c.encode(row.resigned, forKey: .resigned)
            try c.encode(row.appVersion, forKey: .app_version)
            try c.encode(row.build, forKey: .build)
            try c.encode(row.finishedAt, forKey: .finished_at)
            try c.encode(row.turns, forKey: .turns)
        }
    }
}

// MARK: - Uploader

/// Uploads finished games to the cloud history, in the background, at most once
/// each. Hand it a finished game; it builds the row, persists it to a pending
/// queue on disk (one atomically-written file per game), and sends it through
/// the injected transport off the main thread. Pending games are retried on
/// launch and whenever the network comes back. A duplicate-id response counts
/// as delivered. Never shows UI and never throws to callers.
///
/// Opt-out is decided at queue time: a game finished while `sharingEnabled()`
/// is false is never written, so it can't upload after opting back in.
actor GameUploader {
    private let directory: URL
    private let transport: GameUploadTransport?
    private let appVersion: String
    private let build: String
    private let sharingEnabled: @Sendable () -> Bool
    private var lastDrain: Task<Void, Never>?
    private let background = BackgroundWork()
    private var monitor: NetworkMonitor?

    init(directory: URL, transport: GameUploadTransport?, appVersion: String, build: String,
         sharingEnabled: @escaping @Sendable () -> Bool = { true }) {
        self.directory = directory
        self.transport = transport
        self.appVersion = appVersion
        self.build = build
        self.sharingEnabled = sharingEnabled
    }

    /// Default on-device queue: Application Support/GameUploads.
    static var defaultDirectory: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        return base.appendingPathComponent("GameUploads", isDirectory: true)
    }

    /// Fire-and-forget entry point for the game layer: checks opt-out now (queue
    /// time), then does all disk and network work on a background task.
    nonisolated func submitInBackground(_ game: FinishedGame) {
        guard sharingEnabled() else { return }
        background.add(Task.detached(priority: .utility) { [self] in
            await self.enqueue(game)
        })
    }

    /// Queue `game` (unless sharing is off) and try to send everything pending.
    func submit(_ game: FinishedGame) async {
        guard sharingEnabled() else { return }
        await enqueue(game)
    }

    /// Try to send every pending game (on launch, on network return).
    func retryPending() async {
        await drain()
    }

    /// Start retrying: once now, and whenever a network path becomes available.
    func start() {
        background.add(Task.detached(priority: .utility) { [self] in await self.retryPending() })
        guard monitor == nil else { return }
        monitor = NetworkMonitor { [weak self] in
            guard let self else { return }
            Task.detached(priority: .utility) { await self.retryPending() }
        }
    }

    /// Wait for in-flight background submissions and sends (tests).
    func waitUntilIdle() async {
        await background.waitAll()
        await lastDrain?.value
    }

    // MARK: Internals

    private func enqueue(_ game: FinishedGame) async {
        let row = GameRow(game, appVersion: appVersion, build: build)
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let file = directory.appendingPathComponent("\(game.id.uuidString).json")
            try JSONEncoder().encode(row).write(to: file, options: .atomic)
        } catch {
            return   // can't persist: drop silently; play is never affected
        }
        await drain()
    }

    /// Serialised: each drain waits for the previous one, so rows are never sent
    /// twice concurrently.
    private func drain() async {
        let prev = lastDrain
        let task = Task { [self] in
            await prev?.value
            await self.drainOnce()
        }
        lastDrain = task
        await task.value
    }

    private func drainOnce() async {
        guard let transport else { return }   // unconfigured: keep everything queued
        let files = (try? FileManager.default.contentsOfDirectory(
            at: directory, includingPropertiesForKeys: nil))?
            .filter { $0.pathExtension == "json" }
            .sorted { $0.lastPathComponent < $1.lastPathComponent } ?? []
        guard !files.isEmpty else { return }
        guard let uid = try? await transport.installID() else { return }
        for file in files {
            guard let data = try? Data(contentsOf: file),
                  let row = try? JSONDecoder().decode(GameRow.self, from: data) else {
                setAside(file)   // unreadable (e.g. older/newer format): keep, don't send
                continue
            }
            do {
                _ = try await transport.insert(row: row.payload(seat1User: uid))
                try? FileManager.default.removeItem(at: file)   // inserted or duplicate
            } catch is UploadRejected {
                setAside(file)   // permanent refusal: don't block the games behind it
            } catch {
                return   // offline / server error: leave queued for the next retry
            }
        }
    }
}

extension GameUploader {
    /// Moves a queued file into `Rejected/` so it is never retried but not lost.
    nonisolated func setAside(_ file: URL) {
        let fm = FileManager.default
        let rejected = file.deletingLastPathComponent().appendingPathComponent("Rejected", isDirectory: true)
        try? fm.createDirectory(at: rejected, withIntermediateDirectories: true)
        let dest = rejected.appendingPathComponent(file.lastPathComponent)
        try? fm.removeItem(at: dest)
        if (try? fm.moveItem(at: file, to: dest)) == nil { try? fm.removeItem(at: file) }
    }
}

/// Tracks detached background tasks so tests can wait for them.
private final class BackgroundWork: @unchecked Sendable {
    private let lock = NSLock()
    private var tasks: [Task<Void, Never>] = []
    func add(_ t: Task<Void, Never>) { lock.withLock { tasks.append(t) } }
    func waitAll() async {
        while let t = lock.withLock({ tasks.isEmpty ? nil : tasks.removeFirst() }) {
            await t.value
        }
    }
}

/// Calls `onAvailable` each time the device gains a usable network path.
private final class NetworkMonitor: @unchecked Sendable {
    private let monitor = NWPathMonitor()
    private var wasSatisfied = true   // the launch retry covers the initial state
    init(onAvailable: @escaping @Sendable () -> Void) {
        monitor.pathUpdateHandler = { [weak self] path in
            guard let self else { return }
            let ok = path.status == .satisfied
            if ok && !self.wasSatisfied { onAvailable() }
            self.wasSatisfied = ok
        }
        monitor.start(queue: DispatchQueue(label: "nardi.uploader.network", qos: .utility))
    }
    deinit { monitor.cancel() }
}
