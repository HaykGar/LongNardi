import Foundation
import SwiftUI

/// One scrubbable position in a reviewed game. Point 0 is the start; point i (>=1)
/// is the position AFTER turn i. Evals are pinned to the reviewed player's frame.
struct ReviewPoint: Identifiable {
    let id = UUID()
    let board: [Int8]
    let sideToMove: Bool        // who is on move at this position
    let mover: Bool?            // who moved to reach it (nil for the start)
    let dice: (Int, Int)?
    let moved: Bool
    let evalReview: Float?      // static eval here, reviewed-player frame
    let blunderDelta: Float?    // reviewed-player turns w/ >=2 options: best - played (>=0)
    let bestEval: Float?        // reviewed-player frame
    let playedEval: Float?
    let label: String           // move descriptor, e.g. "13 -> 8"
}

/// What the game would have become with vzg0 in the reviewed player's seat, playing
/// the same dice. The opponent replays their real moves for as long as the line
/// hasn't diverged; once it has, that position never occurred in the game, so vzg0
/// plays them too — this is "engine in your seat", not "you vs the same opponent".
struct CounterfactualOutcome: Codable {
    let reviewedWon: Bool
    let mars: Bool
    let finished: Bool        // reached a terminal position (false = hit the turn cap)
}

/// A persisted, ready-to-display game review (cached on a SavedMatch). Stores only
/// the computed bits per position (~tens of bytes each); bumping `version`
/// invalidates stale caches, e.g. after retraining the display/selection nets.
struct CachedReview: Codable {
    let version: Int
    let points: [CachedReviewPoint]
    /// Added after the first shipped review format: absent in older caches, which
    /// are topped up in place rather than recomputed wholesale.
    var counterfactual: CounterfactualOutcome? = nil
}

struct CachedReviewPoint: Codable {
    var board: [Int8]
    var sideToMove: Bool
    var mover: Bool?
    var die0: Int?
    var die1: Int?
    var moved: Bool
    var evalReview: Float?
    var blunderDelta: Float?
    var bestEval: Float?
    var playedEval: Float?
    var label: String

    init(_ p: ReviewPoint) {
        board = p.board; sideToMove = p.sideToMove; mover = p.mover
        die0 = p.dice?.0; die1 = p.dice?.1; moved = p.moved
        evalReview = p.evalReview; blunderDelta = p.blunderDelta
        bestEval = p.bestEval; playedEval = p.playedEval; label = p.label
    }
    var point: ReviewPoint {
        ReviewPoint(board: board, sideToMove: sideToMove, mover: mover,
                    dice: die0.flatMap { d0 in die1.map { (d0, $0) } },
                    moved: moved, evalReview: evalReview, blunderDelta: blunderDelta,
                    bestEval: bestEval, playedEval: playedEval, label: label)
    }
}

/// Computes a post-game review from NardiGame's recorded turns: the eval
/// trajectory from the reviewed player's perspective, and that player's biggest
/// blunder (largest best-vs-played lookahead gap). Owns its own engine.
@MainActor
final class GameReview: ObservableObject {
    static let cols = 12
    nonisolated static let cells = Int(NARDI_BOARD_CELLS)   // also read by the off-actor rollout
    /// Bump to invalidate every cached review (e.g. after retraining res2/vzg0).
    static let reviewVersion = 1

    @Published private(set) var points: [ReviewPoint] = []
    @Published private(set) var ready = false
    /// The rollout's result, in place before `ready` flips. nil means it produced no
    /// outcome at all (no model / empty log), not that it's still running.
    @Published private(set) var counterfactual: CounterfactualOutcome? = nil

    let reviewSide: Bool
    let modelLoaded: Bool
    private let handle: OpaquePointer       // vzg0: blunder / best-move selection
    private let evalHandle: OpaquePointer   // res2: the displayed eval-trajectory graph
    private let log: [ReviewTurn]
    private let cached: CachedReview?                  // reuse instead of recomputing
    private let onComputed: ((CachedReview) -> Void)?  // report a freshly-computed review to cache
    /// The rollout the live game already ran for this game (usually finished before
    /// the game ended). Absent — or for a different game — means running it here.
    private let pending: CounterfactualRunner?

    init(log: [ReviewTurn], reviewSide: Bool,
         cached: CachedReview? = nil, pending: CounterfactualRunner? = nil,
         onComputed: ((CachedReview) -> Void)? = nil) {
        self.log = log
        self.reviewSide = reviewSide
        self.cached = cached
        self.onComputed = onComputed
        self.pending = pending?.covers(turnCount: log.count, reviewSide: reviewSide) == true ? pending : nil
        guard let h = nardi_create(), let e = nardi_create() else { fatalError("nardi_create failed") }
        handle = h; evalHandle = e
        // Strong vzg0 picks the best move (blunder detection); well-calibrated res2
        // produces the eval graph (vzg0 isn't antisymmetric, so its graph swings).
        let sel  = Bundle.main.path(forResource: "vzg0", ofType: "nardiw").map { nardi_load_model(h, $0) == NARDI_OK } ?? false
        let disp = Bundle.main.path(forResource: "res2", ofType: "nardiw").map { nardi_load_model(e, $0) == NARDI_OK } ?? false
        modelLoaded = sel && disp
    }
    deinit { nardi_destroy(handle); nardi_destroy(evalHandle) }

    var reviewSideName: String { reviewSide ? "Black" : "White" }

    /// Total pips rolled by each colour over the whole game (both dice every roll,
    /// passes included; a double counts as four dice — a 5-5 is 20 pips of possible
    /// advancement, not 10). Summed from the recorded turns — each point past the
    /// start carries that turn's dice and mover — so it also works for a cached review.
    var whiteDiceTotal: Int { diceTotal(black: false) }
    var blackDiceTotal: Int { diceTotal(black: true) }
    private func diceTotal(black: Bool) -> Int {
        points.reduce(0) { acc, p in
            guard let d = p.dice, p.mover == black else { return acc }
            // A double is four moves of that value, so it advances 4× the die.
            return acc + (d.0 == d.1 ? d.0 * 4 : d.0 + d.1)
        }
    }

    /// The game's actual dice from review point `pointIndex` onward (turn k's dice
    /// is the move out of point k), so the analyzer can lock to the game's rolls.
    func gameDiceFrom(_ pointIndex: Int) -> [(Int, Int)] {
        guard pointIndex < log.count else { return [] }
        return log[pointIndex...].map { $0.dice }
    }

    /// The game's positions from review point `pointIndex` onward — the mainline the
    /// analyzer steps through (index k = k turns after the jump-in). Aligned with
    /// gameDiceFrom, so dice[k] is the roll from boards[k] to boards[k+1].
    func gameBoardsFrom(_ pointIndex: Int) -> [[Int8]] {
        guard pointIndex < points.count else { return [] }
        return points[pointIndex...].map { $0.board }
    }

    /// Pip counts for the scrubbed position at `index`, from the engine (idle eval
    /// handle — used only during compute(), free afterward; @MainActor).
    func pipCounts(at index: Int) -> (white: Int, black: Int) {
        guard index >= 0, index < points.count else { return (0, 0) }
        let p = points[index]
        p.board.withUnsafeBufferPointer {
            _ = nardi_set_position(evalHandle, $0.baseAddress, p.sideToMove ? 1 : 0)
        }
        var pips = [Int32](repeating: 0, count: 2)
        guard nardi_pip_counts(evalHandle, &pips) == NARDI_OK else { return (0, 0) }
        return (Int(pips[0]), Int(pips[1]))
    }

    /// Async so the "Analyzing…" spinner stays live: the engine calls run on the
    /// main actor (it isn't thread-safe), but we yield between turns.
    ///
    /// Nothing is shown until BOTH halves are done — the per-move analysis here and
    /// the counterfactual rollout — so the screen never presents a half-filled review
    /// (and the analyzer can't be opened into a running rollout). They overlap rather
    /// than queue: the rollout owns its own engine handle on a background queue, so
    /// the wait is the slower of the two, not their sum.
    func compute() async {
        guard !ready, !log.isEmpty else { return }

        // A complete cached review is instant — no engine work at all.
        if let c = cached, c.version == Self.reviewVersion, let cf = c.counterfactual {
            points = c.points.map { $0.point }
            counterfactual = cf
            ready = true
            return
        }

        // Usually already done: the live game ran this alongside play. Otherwise it
        // starts here and overlaps the per-move analysis below.
        let rollout = pending ?? CounterfactualRunner(replaying: log, reviewSide: reviewSide)

        // A cache written before the counterfactual existed keeps its per-move
        // analysis; only the rollout is topped up.
        if let c = cached, c.version == Self.reviewVersion {
            let cf = await rollout.result()
            points = c.points.map { $0.point }
            counterfactual = cf
            ready = true
            onComputed?(CachedReview(version: Self.reviewVersion,
                                     points: c.points, counterfactual: cf))
            return
        }

        var pts: [ReviewPoint] = []

        // Point 0: the starting position (before the first recorded turn).
        let first = log[0]
        pts.append(ReviewPoint(board: first.preBoard, sideToMove: first.preSide, mover: nil,
                               dice: nil, moved: true,
                               evalReview: staticEval(first.preBoard, first.preSide),
                               blunderDelta: nil, bestEval: nil, playedEval: nil, label: "Start"))

        for (k, t) in log.enumerated() {
            let after = !t.preSide   // side to move after the turn
            var blunder: Float? = nil, best: Float? = nil, played: Float? = nil
            if t.moved && t.preSide == reviewSide {
                (blunder, best, played) = blunderFor(t)
            }
            pts.append(ReviewPoint(
                board: t.postBoard, sideToMove: after, mover: t.preSide,
                dice: t.dice, moved: t.moved,
                evalReview: staticEval(t.postBoard, after),
                blunderDelta: blunder, bestEval: best, playedEval: played,
                label: t.moved ? describe(before: t.preBoard, after: t.postBoard, moverBlack: t.preSide)
                               : "pass"))
            if k % 3 == 0 { await Task.yield() }   // let the UI breathe
        }

        let cf = await rollout.result()

        points = pts
        counterfactual = cf
        ready = true
        // Hand the computed review back to be cached, so re-opening is instant.
        onComputed?(CachedReview(version: Self.reviewVersion,
                                 points: pts.map { CachedReviewPoint($0) }, counterfactual: cf))
    }

    // MARK: - Engine helpers

    /// Graph eval, from the well-calibrated res2 net (on the eval handle), pinned to
    /// the reviewed-player frame with the side-to-move + sign convention.
    private func staticEval(_ board: [Int8], _ side: Bool) -> Float? {
        guard modelLoaded else { return nil }
        board.withUnsafeBufferPointer { _ = nardi_set_position(evalHandle, $0.baseAddress, side ? 1 : 0) }
        // Terminal positions report the exact game result (+1 normal, +2 mars) in
        // the winner's frame, not a model estimate. The winner is whoever has no
        // checkers left on the board (white's are >0, black's <0).
        if nardi_is_terminal(evalHandle) == 1 {
            let whiteOn = board.reduce(0) { $0 + max(0, Int($1)) }
            let winnerIsBlack = (whiteOn != 0)
            let margin = Float(nardi_winner_result(evalHandle))
            return margin * (winnerIsBlack == reviewSide ? 1 : -1)
        }
        var v: Float = 0
        guard nardi_evaluate_position(evalHandle, &v) == NARDI_OK else { return nil }
        return v * (side == reviewSide ? 1 : -1)   // pin to reviewed-player frame
    }

    /// (delta, bestEval, playedEval) for a reviewed-player turn, all in the
    /// reviewed-player frame. nil delta when the move was forced (<=1 option).
    private func blunderFor(_ t: ReviewTurn) -> (Float?, Float?, Float?) {
        guard modelLoaded else { return (nil, nil, nil) }
        t.preBoard.withUnsafeBufferPointer { _ = nardi_set_position(handle, $0.baseAddress, t.preSide ? 1 : 0) }
        let n = Int(nardi_analyze_dice(handle, Int32(t.dice.0), Int32(t.dice.1)))
        guard n > 1 else { return (nil, nil, nil) }   // forced / no real choice
        var best: Float = -.infinity
        var played: Float? = nil
        for i in 0..<n {
            var b = [Int8](repeating: 0, count: Self.cells)
            var v: Float = 0
            guard nardi_analyzed_move(handle, Int32(i), &b, &v) == NARDI_OK else { continue }
            best = max(best, v)               // mover == reviewSide, so already its frame
            if b == t.postBoard { played = v }
        }
        guard let p = played, best.isFinite else { return (nil, nil, nil) }
        return (max(0, best - p), best, p)
    }

    /// Deterministic dice for the stretch past the game's recorded rolls (splitmix64),
    /// so a recomputed counterfactual always tells the same story.
    // MARK: - Move descriptor (point numbers 1..24 along the mover's path)

    private func pointNumber(row: Int, col: Int, black: Bool) -> Int {
        let onHeadRow = (row == (black ? 1 : 0))
        return (onHeadRow ? col : Self.cols + col) + 1
    }

    private func describe(before: [Int8], after: [Int8], moverBlack: Bool) -> String {
        let s = Int(moverBlack ? -1 : 1)
        var srcs: [Int] = [], dsts: [Int] = []
        for row in 0..<2 {
            for col in 0..<Self.cols {
                let idx = row * Self.cols + col
                let d = (Int(after[idx]) - Int(before[idx])) * s
                let pt = pointNumber(row: row, col: col, black: moverBlack)
                if d < 0 { srcs += Array(repeating: pt, count: -d) }
                if d > 0 { dsts += Array(repeating: pt, count: d) }
            }
        }
        let off = srcs.count - dsts.count
        var dest = dsts.sorted().map(String.init)
        if off > 0 { dest += Array(repeating: "off", count: off) }
        let from = srcs.sorted().map(String.init).joined(separator: ",")
        return from.isEmpty ? "—" : "\(from) → \(dest.joined(separator: ","))"
    }
}

// MARK: - Counterfactual: vzg0 in the reviewed player's seat

/// Replays the game with vzg0 picking the reviewed player's moves, on the game's own
/// dice, and reports how that game would have ended. The opponent replays their real
/// move while the position still matches the game's (so an unchanged line reproduces
/// the game exactly); after the first deviation those moves never happened, so vzg0
/// plays that seat too.
///
/// Turns are CONSUMED AS THEY ARRIVE rather than read out of a finished log, so the
/// live game can drive the rollout while it's still being played — by the time the
/// game ends the answer is usually already in hand and the review opens without a
/// wait. A finished game drives the same object by feeding its whole log at once.
///
/// Nothing here has to predict the game's future: the rollout needs turn k's roll
/// only once the game has played turn k, and it parks until then. Past the game's
/// last turn (its line runs longer than the real game) the dice come from a
/// fixed-seed generator, so a given game always reproduces the same line.
final class CounterfactualRunner: @unchecked Sendable {
    private let reviewSide: Bool
    private let handle: OpaquePointer?
    // .utility: this runs while the player plays, so it must never outbid the bot's
    // own move selection for CPU.
    private let queue = DispatchQueue(label: "nardi.counterfactual", qos: .utility)

    // `cond` guards everything below AND parks the worker between fed turns.
    private let cond = NSCondition()
    private var turns: [ReviewTurn] = []
    private var closed = false          // the game is over: no more turns are coming
    private var cancelled = false       // the game was abandoned: stop and discard
    private var done = false
    private var outcome: CounterfactualOutcome? = nil
    private var waiters: [CheckedContinuation<CounterfactualOutcome?, Never>] = []

    /// Live game: turns are fed as they're played.
    init(reviewSide: Bool) {
        self.reviewSide = reviewSide
        var h = nardi_create()
        // vzg0 plays both seats in the rollout, so it ranks with the selection net.
        if let hh = h, Bundle.main.path(forResource: "vzg0", ofType: "nardiw")
            .map({ nardi_load_model(hh, $0) != NARDI_OK }) ?? true {
            nardi_destroy(hh)
            h = nil
        }
        handle = h
        queue.async { [self] in run() }
    }

    /// Finished game: feed the whole log and let it run.
    convenience init(replaying log: [ReviewTurn], reviewSide: Bool) {
        self.init(reviewSide: reviewSide)
        log.forEach(feed)
        close()
    }

    deinit { if let handle { nardi_destroy(handle) } }

    // MARK: Driving it

    /// Hand the rollout the turn the game just played.
    func feed(_ turn: ReviewTurn) {
        cond.lock(); turns.append(turn); cond.broadcast(); cond.unlock()
    }

    /// The game is over — the log is final. The rollout plays out its own tail (its
    /// line usually outlives the real game) and finishes.
    func close() {
        cond.lock(); closed = true; cond.broadcast(); cond.unlock()
    }

    /// The game was abandoned mid-play; drop the work. A game that reached `close()`
    /// is left alone: its remaining work is finite and its answer may still be wanted.
    func cancel() {
        cond.lock()
        if !closed { cancelled = true; cond.broadcast() }
        cond.unlock()
    }

    /// True if this rollout is the one for `log` — a guard against showing a stale
    /// game's counterfactual (turn counts are fed one-for-one with the review log).
    func covers(turnCount: Int, reviewSide side: Bool) -> Bool {
        cond.lock(); defer { cond.unlock() }
        return side == reviewSide && closed && !cancelled && turns.count == turnCount
    }

    /// The result, waiting for the rollout if it's still going.
    func result() async -> CounterfactualOutcome? {
        await withCheckedContinuation { cont in
            cond.lock()
            if done { cond.unlock(); cont.resume(returning: outcome) }
            else { waiters.append(cont); cond.unlock() }
        }
    }

    // MARK: The rollout (worker thread)

    /// Turn `k` of the real game, parking until the game has played it. nil once the
    /// game is over without ever reaching turn k — the rollout has outlived it.
    private func turn(_ k: Int) -> ReviewTurn? {
        cond.lock(); defer { cond.unlock() }
        while k >= turns.count && !closed && !cancelled { cond.wait() }
        return k < turns.count ? turns[k] : nil
    }

    private var isCancelled: Bool {
        cond.lock(); defer { cond.unlock() }
        return cancelled
    }

    private func finish(_ o: CounterfactualOutcome?) {
        cond.lock()
        outcome = o; done = true
        let pending = waiters; waiters = []
        cond.unlock()
        pending.forEach { $0.resume(returning: o) }
    }

    private func run() {
        guard let handle, let first = turn(0) else { return finish(nil) }
        let cap = 600                       // safety net; real games are far shorter
        var board = first.preBoard
        var side = first.preSide
        var rng = SeededRoller(seed: 0x6E61_7264_69)   // "nardi"
        var onGameLine = true               // still replaying positions the game reached
        var t = 0

        while t < cap {
            board.withUnsafeBufferPointer { _ = nardi_set_position(handle, $0.baseAddress, side ? 1 : 0) }
            if nardi_is_terminal(handle) == 1 { break }

            // Turns alternate strictly in both games (a pass is still a turn), so the
            // game's turn `t` is the same side as this one and its roll applies. This
            // parks until the live game has played it.
            let played = turn(t)
            if isCancelled { return finish(nil) }
            // The line holds only while every position so far matched the game's.
            onGameLine = onGameLine && played.map { board == $0.preBoard } == true
            let dice = played?.dice ?? (rng.die(), rng.die())

            let n = Int(nardi_analyze_dice(handle, Int32(dice.0), Int32(dice.1)))
            if n <= 0 {                     // no legal move: pass, side switches
                side.toggle()
                t += 1
                continue
            }

            // Read the ranked options once: analyzing again on this handle would
            // clear the list. Index 0 is vzg0's pick.
            var options: [[Int8]] = []
            for i in 0..<n {
                var b = [Int8](repeating: 0, count: GameReview.cells)
                var v: Float = 0
                if nardi_analyzed_move(handle, Int32(i), &b, &v) == NARDI_OK { options.append(b) }
            }
            guard !options.isEmpty else { break }

            // The opponent keeps their real move while the line is intact; the
            // reviewed player always gets vzg0's best.
            var choice = 0
            if side != reviewSide, onGameLine, let real = played,
               let j = options.firstIndex(of: real.postBoard) {
                choice = j
            }
            guard nardi_apply_analyzed_move(handle, Int32(choice)) == NARDI_OK else { break }

            var next = [Int8](repeating: 0, count: GameReview.cells)
            _ = nardi_board(handle, &next)
            board = next
            side.toggle()
            t += 1
        }

        guard let result = terminalOutcome(handle: handle, board: board, side: side) else {
            return finish(CounterfactualOutcome(reviewedWon: false, mars: false, finished: false))
        }
        finish(CounterfactualOutcome(reviewedWon: result.reviewedWon, mars: result.mars, finished: true))
    }

    /// Winner + mars for a terminal board, in reviewed-player terms. nil if the
    /// position isn't terminal (the rollout hit the turn cap).
    private func terminalOutcome(handle: OpaquePointer, board: [Int8],
                                 side: Bool) -> (reviewedWon: Bool, mars: Bool)? {
        board.withUnsafeBufferPointer { _ = nardi_set_position(handle, $0.baseAddress, side ? 1 : 0) }
        guard nardi_is_terminal(handle) == 1 else { return nil }
        let whiteOn = board.reduce(0) { $0 + max(0, Int($1)) }
        let winnerIsBlack = (whiteOn != 0)          // the winner has nothing left on board
        return (winnerIsBlack == reviewSide, nardi_winner_result(handle) == 2)
    }
}

/// Deterministic dice for the part of the counterfactual line that runs past the
/// real game's last roll (splitmix64), so a review always reproduces itself.
private struct SeededRoller {
    private var state: UInt64
    init(seed: UInt64) { state = seed }
    mutating func die() -> Int {
        state &+= 0x9E37_79B9_7F4A_7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
        z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
        z ^= (z >> 31)
        return Int(z % 6) + 1
    }
}
