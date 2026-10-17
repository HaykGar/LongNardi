import XCTest

/// Exercises the game uploader only through its public interface (`submit`,
/// `retryPending`, `waitUntilIdle`) with a fake transport standing in for Supabase.
final class GameUploaderTests: XCTestCase {
    private var dir: URL!

    override func setUpWithError() throws {
        dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("GameUploaderTests-\(UUID().uuidString)")
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(at: dir)
    }

    private func makeUploader(_ transport: FakeTransport?,
                              sharing: @escaping @Sendable () -> Bool = { true }) -> GameUploader {
        GameUploader(directory: dir, transport: transport,
                     appVersion: "2.3", build: "7", sharingEnabled: sharing)
    }

    private func sampleGame(id: UUID = UUID(uuidString: "11111111-2222-3333-4444-555555555555")!,
                            opponent: OpponentKind = .bot(.hard),
                            userFirst: Bool = false, seat1Won: Bool = true,
                            mars: Bool = true, resigned: Bool = false) -> FinishedGame {
        FinishedGame(id: id, opponent: opponent, userFirst: userFirst,
                     seat1Won: seat1Won, mars: mars, resigned: resigned,
                     finishedAt: Date(timeIntervalSince1970: 1_790_000_000),
                     turns: [RecordedTurn(dice: [5, 3], moves: [[0, 0], [5, 1]]),
                             RecordedTurn(dice: [6, 6], moves: [])])
    }

    private func json(_ data: Data) throws -> [String: Any] {
        try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
    }

    // MARK: - Row

    func testBuildsRowForFinishedBotGame() async throws {
        let t = FakeTransport()
        let up = makeUploader(t)
        await up.submit(sampleGame())
        await up.waitUntilIdle()

        let rows = t.sentRows
        XCTAssertEqual(rows.count, 1)
        let row = try json(try XCTUnwrap(rows.first))
        XCTAssertEqual(row["id"] as? String, "11111111-2222-3333-4444-555555555555")
        XCTAssertEqual(row["seat1_user"] as? String, "anon-uid")
        XCTAssertTrue(row["seat2_user"] is NSNull)
        XCTAssertEqual(row["opponent_kind"] as? String, "bot_hard")
        XCTAssertEqual(row["bot_model"] as? String, "vzg0")
        XCTAssertEqual(row["user_first"] as? Bool, false)
        XCTAssertEqual(row["outcome"] as? Int, 2)
        XCTAssertEqual(row["resigned"] as? Bool, false)
        XCTAssertEqual(row["app_version"] as? String, "2.3")
        XCTAssertEqual(row["build"] as? String, "7")
        XCTAssertEqual(row["finished_at"] as? String, "2026-09-21T14:13:20Z")
        let turns = try XCTUnwrap(row["turns"] as? [[String: Any]])
        XCTAssertEqual(turns.count, 2)
        XCTAssertEqual(turns[0]["d"] as? [Int], [5, 3])
        XCTAssertEqual(turns[0]["m"] as? [[Int]], [[0, 0], [5, 1]])
        XCTAssertEqual(turns[1]["d"] as? [Int], [6, 6])
        XCTAssertEqual(turns[1]["m"] as? [[Int]], [])   // a pass
    }

    func testOutcomeIsFromSeatOneViewAndHumansHaveNoBotModel() async throws {
        let t = FakeTransport()
        let up = makeUploader(t)
        await up.submit(sampleGame(id: UUID(), opponent: .localHuman, userFirst: true,
                                   seat1Won: false, mars: false, resigned: true))
        await up.submit(sampleGame(id: UUID(), opponent: .bot(.easy), seat1Won: false, mars: true))
        await up.submit(sampleGame(id: UUID(), opponent: .bot(.medium), seat1Won: true, mars: false))
        await up.waitUntilIdle()

        let rows = try t.sentRows.map(json)
        XCTAssertEqual(rows.count, 3)
        let human = try XCTUnwrap(rows.first { $0["opponent_kind"] as? String == "local_human" })
        XCTAssertEqual(human["outcome"] as? Int, -1)
        XCTAssertTrue(human["bot_model"] is NSNull)
        XCTAssertEqual(human["user_first"] as? Bool, true)
        XCTAssertEqual(human["resigned"] as? Bool, true)
        let easy = try XCTUnwrap(rows.first { $0["opponent_kind"] as? String == "bot_easy" })
        XCTAssertEqual(easy["outcome"] as? Int, -2)
        XCTAssertEqual(easy["bot_model"] as? String, "heuristic")
        let medium = try XCTUnwrap(rows.first { $0["opponent_kind"] as? String == "bot_medium" })
        XCTAssertEqual(medium["outcome"] as? Int, 1)
        XCTAssertEqual(medium["bot_model"] as? String, "mlp")
    }

    // MARK: - Pending queue

    func testPendingGameSurvivesFailedSendAndRestartThenRetries() async throws {
        let failing = FakeTransport()
        failing.script = [.failure(FakeNetworkError())]
        let first = makeUploader(failing)
        await first.submit(sampleGame())
        await first.waitUntilIdle()
        XCTAssertTrue(failing.sentRows.isEmpty)

        // "Restart": a fresh uploader on the same directory, as on next launch.
        let healthy = FakeTransport()
        let second = makeUploader(healthy)
        await second.retryPending()
        await second.waitUntilIdle()
        XCTAssertEqual(healthy.sentRows.count, 1)
        XCTAssertEqual(try json(try XCTUnwrap(healthy.sentRows.first))["id"] as? String,
                       "11111111-2222-3333-4444-555555555555")

        // Sent rows leave the queue: a further retry sends nothing.
        await second.retryPending()
        await second.waitUntilIdle()
        XCTAssertEqual(healthy.sentRows.count, 1)
    }

    func testGameStaysQueuedWhileUnconfigured() async throws {
        let unconfigured = makeUploader(nil)
        await unconfigured.submit(sampleGame())
        await unconfigured.waitUntilIdle()

        let t = FakeTransport()
        let configured = makeUploader(t)
        await configured.retryPending()
        await configured.waitUntilIdle()
        XCTAssertEqual(t.sentRows.count, 1)
    }

    func testDuplicateResponseCountsAsSuccessAndDequeues() async throws {
        let t = FakeTransport()
        t.script = [.success(.duplicate)]
        let up = makeUploader(t)
        await up.submit(sampleGame())
        await up.waitUntilIdle()

        let later = FakeTransport()
        let relaunched = makeUploader(later)
        await relaunched.retryPending()
        await relaunched.waitUntilIdle()
        XCTAssertTrue(later.sentRows.isEmpty)
    }

    func testPermanentlyRejectedRowDoesNotBlockLaterGames() async throws {
        let a = UUID(uuidString: "00000000-0000-0000-0000-00000000000A")!
        let b = UUID(uuidString: "00000000-0000-0000-0000-00000000000B")!
        let offline = makeUploader(nil)
        await offline.submit(sampleGame(id: a))
        await offline.submit(sampleGame(id: b))
        await offline.waitUntilIdle()

        let t = FakeTransport()
        t.script = [.failure(UploadRejected(status: 400))]   // a's row is refused for good
        let up = makeUploader(t)
        await up.retryPending()
        await up.waitUntilIdle()
        XCTAssertEqual(try t.sentRows.map(json).map { $0["id"] as? String },
                       [b.uuidString.lowercased()])

        // The rejected row is set aside, not retried forever.
        await up.retryPending()
        await up.waitUntilIdle()
        XCTAssertEqual(t.sentRows.count, 1)
        XCTAssertTrue(FileManager.default.fileExists(atPath: dir
            .appendingPathComponent("Rejected/\(a.uuidString).json").path))
    }

    func testUndecodableQueuedFileIsSetAsideNotDeleted() async throws {
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        try Data("{\"future\":1}".utf8).write(to: dir.appendingPathComponent("odd.json"))
        let t = FakeTransport()
        let up = makeUploader(t)
        await up.retryPending()
        await up.waitUntilIdle()
        XCTAssertTrue(t.sentRows.isEmpty)
        XCTAssertTrue(FileManager.default.fileExists(atPath: dir
            .appendingPathComponent("Rejected/odd.json").path))
    }

    // MARK: - Opt-out

    func testOptedOutGamesAreNeverQueuedEvenAfterOptingBackIn() async throws {
        let sharing = Flag(false)
        let t = FakeTransport()
        let up = makeUploader(t, sharing: { sharing.value })
        await up.submit(sampleGame(id: UUID()))
        up.submitInBackground(sampleGame(id: UUID()))
        await up.waitUntilIdle()
        XCTAssertTrue(t.sentRows.isEmpty)

        sharing.value = true
        await up.retryPending()
        let relaunched = makeUploader(t, sharing: { sharing.value })
        await relaunched.retryPending()
        await relaunched.waitUntilIdle()
        XCTAssertTrue(t.sentRows.isEmpty)
        XCTAssertTrue(t.sentOnMainThread.isEmpty)
    }

    // MARK: - Threading

    @MainActor
    func testBackgroundSubmitSendsOffTheMainThread() async throws {
        let t = FakeTransport()
        let up = makeUploader(t)
        up.submitInBackground(sampleGame())   // returns immediately on the main thread
        await up.waitUntilIdle()
        XCTAssertEqual(t.sentRows.count, 1)
        XCTAssertEqual(t.sentOnMainThread, [false])
    }
}

final class Flag: @unchecked Sendable {
    private let lock = NSLock()
    private var _value: Bool
    init(_ v: Bool) { _value = v }
    var value: Bool {
        get { lock.withLock { _value } }
        set { lock.withLock { _value = newValue } }
    }
}

/// Stand-in for Supabase. Thread-safe; scripted per call.
final class FakeTransport: GameUploadTransport, @unchecked Sendable {
    private let lock = NSLock()
    private var _rows: [Data] = []
    private var _onMain: [Bool] = []
    /// Results to return for successive inserts; when exhausted, `.inserted`.
    var script: [Result<InsertOutcome, Error>] = []

    var sentRows: [Data] { lock.withLock { _rows } }
    var sentOnMainThread: [Bool] { lock.withLock { _onMain } }

    func installID() async throws -> String { "anon-uid" }

    func insert(row: Data) async throws -> InsertOutcome {
        let next: Result<InsertOutcome, Error> = lock.withLock {
            _onMain.append(Thread.isMainThread)
            let r = script.isEmpty ? .success(.inserted) : script.removeFirst()
            if case .success = r { _rows.append(row) }
            return r
        }
        return try next.get()
    }
}

struct FakeNetworkError: Error {}

/// The opt-out flag and the one-time consent banner, through UserDefaults (the
/// same store the Settings toggle writes via @AppStorage).
final class SharingConsentTests: XCTestCase {
    private var defaults: UserDefaults!
    private let suite = "SharingConsentTests"

    override func setUp() {
        UserDefaults().removePersistentDomain(forName: suite)
        defaults = UserDefaults(suiteName: suite)
    }

    override func tearDown() { UserDefaults().removePersistentDomain(forName: suite) }

    func testSharingDefaultsOnAndFollowsTheSettingsKey() {
        let consent = SharingConsent(defaults: defaults)
        XCTAssertTrue(consent.sharingEnabled)
        defaults.set(false, forKey: "nardi.shareGameData")
        XCTAssertFalse(consent.sharingEnabled)
    }

    func testBannerShowsOnHomeUntilDismissedThenNeverAgain() {
        XCTAssertTrue(SharingConsent(defaults: defaults).showsBanner(onHome: true))
        SharingConsent(defaults: defaults).dismissBanner()
        XCTAssertFalse(SharingConsent(defaults: defaults).showsBanner(onHome: true))
    }

    func testBannerNeverShowsOffHome() {
        XCTAssertFalse(SharingConsent(defaults: defaults).showsBanner(onHome: false))
    }

    func testGamesFinishedWhileOptedOutViaSettingsNeverUploadEvenAfterOptingBackIn() async throws {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("SharingConsentTests-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: dir) }
        nonisolated(unsafe) let d = defaults!
        let t = FakeTransport()
        func make() -> GameUploader {
            GameUploader(directory: dir, transport: t, appVersion: "1", build: "1",
                         sharingEnabled: { SharingConsent(defaults: d).sharingEnabled })
        }
        d.set(false, forKey: GameUploader.sharingDefaultsKey)   // toggle off in Settings
        let up = make()
        await up.submit(FinishedGame(id: UUID(), opponent: .bot(.hard), userFirst: true,
                                     seat1Won: true, mars: false, resigned: false,
                                     finishedAt: Date(), turns: []))
        await up.waitUntilIdle()

        d.set(true, forKey: GameUploader.sharingDefaultsKey)    // toggle back on
        let relaunched = make()
        await relaunched.retryPending()
        await relaunched.waitUntilIdle()
        XCTAssertTrue(t.sentRows.isEmpty)
    }
}
