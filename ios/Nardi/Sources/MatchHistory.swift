import Foundation

/// A completed turn, serialised for storage. Mirrors `ReviewTurn` but with a
/// Codable shape (tuples aren't Codable), so a finished game can be reloaded and
/// re-reviewed or re-analysed later.
struct SavedTurn: Codable {
    let preBoard: [Int8]
    let preSide: Bool
    let die0: Int
    let die1: Int
    let postBoard: [Int8]
    let moved: Bool

    init(_ t: ReviewTurn) {
        preBoard = t.preBoard; preSide = t.preSide
        die0 = t.dice.0; die1 = t.dice.1
        postBoard = t.postBoard; moved = t.moved
    }

    var reviewTurn: ReviewTurn {
        ReviewTurn(preBoard: preBoard, preSide: preSide, dice: (die0, die1),
                   postBoard: postBoard, moved: moved)
    }
}

/// One archived game: enough to show the result and to re-run game review / open
/// any position in the analyzer (via its turn log).
struct SavedMatch: Codable, Identifiable {
    let id: UUID
    let date: Date
    let modeRaw: String          // GameMode.rawValue
    let opponentRaw: String?     // Opponent.rawValue (nil for pass & play)
    let reviewSide: Bool         // side whose play is reviewed (true = Black)
    let winnerWhite: Bool        // who won, regardless of review side
    let mars: Bool
    let turns: [SavedTurn]
    var review: CachedReview? = nil   // computed lazily on first open, then cached

    var reviewTurns: [ReviewTurn] { turns.map { $0.reviewTurn } }
    var reviewSideName: String { reviewSide ? "Black" : "White" }
    var moveCount: Int { turns.count }

    /// vs-computer review side is the human; pass & play reviews White. So a win
    /// for the reviewed side is the outcome the player cares about.
    var reviewedSideWon: Bool { winnerWhite == !reviewSide }

    /// Human-facing one-liner for the opponent / mode.
    var opponentLabel: String {
        if let o = opponentRaw { return "vs \(o)" }
        return modeRaw == GameMode.passAndPlay.rawValue ? "Pass & Play" : modeRaw
    }
}

/// Stores finished games as JSON in the app's Documents directory. Small enough
/// to keep entirely in memory; rewrites the whole file on every change. Kept
/// deliberately simple so a redesign can swap the backing store without touching
/// callers (they only use `matches`, `add`, `delete`, `clear`).
@MainActor
final class MatchStore: ObservableObject {
    @Published private(set) var matches: [SavedMatch] = []

    private let url: URL

    init() {
        let dir = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        url = dir.appendingPathComponent("matches.json")
        load()
    }

    func add(_ m: SavedMatch) {
        matches.insert(m, at: 0)          // newest first
        persist()
    }

    /// Cache a computed review onto its match and persist it (tiny — a few KB).
    func setReview(_ id: UUID, _ review: CachedReview) {
        guard let i = matches.firstIndex(where: { $0.id == id }) else { return }
        matches[i].review = review
        persist()
    }

    func delete(_ m: SavedMatch) {
        matches.removeAll { $0.id == m.id }
        persist()
    }

    func delete(at offsets: IndexSet) {
        matches.remove(atOffsets: offsets)
        persist()
    }

    func clear() {
        matches.removeAll()
        persist()
    }

    private func load() {
        guard let data = try? Data(contentsOf: url),
              let decoded = try? JSONDecoder().decode([SavedMatch].self, from: data)
        else { return }
        matches = decoded.sorted { $0.date > $1.date }
    }

    private func persist() {
        guard let data = try? JSONEncoder().encode(matches) else { return }
        try? data.write(to: url, options: .atomic)
    }
}
