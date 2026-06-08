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

/// Computes a post-game review from NardiGame's recorded turns: the eval
/// trajectory from the reviewed player's perspective, and that player's biggest
/// blunder (largest best-vs-played lookahead gap). Owns its own engine.
@MainActor
final class GameReview: ObservableObject {
    static let cols = 12
    static let cells = Int(NARDI_BOARD_CELLS)

    @Published private(set) var points: [ReviewPoint] = []
    @Published private(set) var biggestBlunder: Int? = nil   // index into points
    @Published private(set) var ready = false

    let reviewSide: Bool
    let modelLoaded: Bool
    private let handle: OpaquePointer
    private let log: [ReviewTurn]

    init(log: [ReviewTurn], reviewSide: Bool) {
        self.log = log
        self.reviewSide = reviewSide
        guard let h = nardi_create() else { fatalError("nardi_create failed") }
        handle = h
        // Review uses the strongest network (the Polyak-averaged ResNardiNet).
        if let path = Bundle.main.path(forResource: "polavg10", ofType: "nardiw") {
            modelLoaded = (nardi_load_model(h, path) == NARDI_OK)
        } else {
            modelLoaded = false
        }
    }
    deinit { nardi_destroy(handle) }

    var reviewSideName: String { reviewSide ? "Black" : "White" }

    /// The game's actual dice from review point `pointIndex` onward (turn k's dice
    /// is the move out of point k), so the analyzer can lock to the game's rolls.
    func gameDiceFrom(_ pointIndex: Int) -> [(Int, Int)] {
        guard pointIndex < log.count else { return [] }
        return log[pointIndex...].map { $0.dice }
    }

    /// Async so the "Analyzing…" spinner stays live: the engine calls run on the
    /// main actor (it isn't thread-safe), but we yield between turns.
    func compute() async {
        guard !ready, !log.isEmpty else { return }
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

        points = pts
        biggestBlunder = pts.enumerated()
            .filter { $0.element.blunderDelta != nil }
            .max { ($0.element.blunderDelta ?? 0) < ($1.element.blunderDelta ?? 0) }
            .map { $0.offset }
        ready = true
    }

    // MARK: - Engine helpers

    private func staticEval(_ board: [Int8], _ side: Bool) -> Float? {
        guard modelLoaded else { return nil }
        board.withUnsafeBufferPointer { _ = nardi_set_position(handle, $0.baseAddress, side ? 1 : 0) }
        // Terminal positions report the exact game result (+1 normal, +2 mars) in
        // the winner's frame, not a model estimate. The winner is whoever has no
        // checkers left on the board (white's are >0, black's <0).
        if nardi_is_terminal(handle) == 1 {
            let whiteOn = board.reduce(0) { $0 + max(0, Int($1)) }
            let winnerIsBlack = (whiteOn != 0)
            let margin = Float(nardi_winner_result(handle))
            return margin * (winnerIsBlack == reviewSide ? 1 : -1)
        }
        var v: Float = 0
        guard nardi_evaluate_position(handle, &v) == NARDI_OK else { return nil }
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
