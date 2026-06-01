import Foundation
import SwiftUI

/// Opponent options surfaced in the UI, mapped to the C engine's strategies.
enum Opponent: String, CaseIterable, Identifiable {
    case greedy = "Greedy"
    case lookahead = "1-ply Lookahead"
    case mcts = "MCTS"
    case heuristic = "Heuristic"
    var id: String { rawValue }

    var strategy: NardiStrategy {
        switch self {
        case .greedy: return NARDI_GREEDY
        case .lookahead: return NARDI_LOOKAHEAD
        case .mcts: return NARDI_MCTS
        case .heuristic: return NARDI_HEURISTIC
        }
    }
}

enum Phase: Equatable {
    case idle
    case botThinking
    case awaitingHuman
    case gameOver(winner: Int, margin: Int)
}

/// ObservableObject wrapper over the plain-C engine API. All engine calls stay on
/// the main actor (the C engine is single-threaded / not thread-safe).
@MainActor
final class NardiGame: ObservableObject {
    static let boardCells = Int(NARDI_BOARD_CELLS) // 24 (2 x 12)

    @Published private(set) var board: [Int8] = Array(repeating: 0, count: boardCells)
    @Published private(set) var dice: (Int, Int) = (0, 0)
    @Published private(set) var phase: Phase = .idle
    @Published private(set) var options: [[Int8]] = []      // legal end-boards for the human
    @Published private(set) var humanIsWhite = true
    @Published private(set) var status = "Tap New Game to start."

    private let handle: OpaquePointer
    private let modelLoaded: Bool

    init() {
        guard let h = nardi_create() else { fatalError("nardi_create failed") }
        handle = h
        if let path = Bundle.main.path(forResource: "model", ofType: "nardiw") {
            modelLoaded = (nardi_load_model(h, path) == NARDI_OK)
        } else {
            modelLoaded = false
        }
    }

    deinit { nardi_destroy(handle) }

    func newGame(opponent: Opponent, humanIsWhite: Bool = true) {
        self.humanIsWhite = humanIsWhite
        let human = NARDI_HUMAN
        let bot = opponent.strategy
        nardi_configure_players(handle, humanIsWhite ? human : bot, humanIsWhite ? bot : human)
        if opponent == .mcts {
            nardi_set_mcts_params(handle, 120, 1.0, 0, 0.1, 0.25, 0.3, 0)
        }
        nardi_reset(handle)
        refreshBoard()
        status = modelLoaded ? "Game on." : "⚠️ model blob missing — bots use no network."
        pump()
    }

    /// Apply the human's chosen legal option (index into `options`).
    func chooseOption(_ idx: Int) {
        guard case .awaitingHuman = phase else { return }
        guard nardi_apply_human_move(handle, Int32(idx)) == NARDI_OK else {
            status = "Move rejected: " + String(cString: nardi_last_error(handle))
            return
        }
        options = []
        refreshBoard()
        pump()
    }

    /// Drive the state machine: play bot moves (with a short delay for legibility)
    /// until it's the human's turn or the game ends.
    private func pump() {
        phase = .botThinking
        Task { @MainActor in
            while true {
                let step = nardi_advance(handle)
                refreshBoard()
                switch step {
                case NARDI_STEP_GAME_OVER:
                    let winnerIsWhite = (nardi_current_player(handle) == 1)
                    let humanWon = (winnerIsWhite == humanIsWhite)
                    let margin = Int(nardi_winner_result(handle))
                    phase = .gameOver(winner: winnerIsWhite ? 1 : 2, margin: margin)
                    status = humanWon ? (margin == 2 ? "You win — mars! 🎉" : "You win! 🎉")
                                      : (margin == 2 ? "Bot wins (mars)." : "Bot wins.")
                    return
                case NARDI_STEP_AWAITING_HUMAN:
                    loadOptions()
                    phase = .awaitingHuman
                    status = "Your turn — dice \(dice.0)-\(dice.1). Pick a move."
                    return
                case NARDI_STEP_ERROR:
                    status = "Engine error: " + String(cString: nardi_last_error(handle))
                    phase = .idle
                    return
                default: // BotMoved / TurnPassed
                    status = "Bot played \(dice.0)-\(dice.1)…"
                    try? await Task.sleep(nanoseconds: 450_000_000)
                }
            }
        }
    }

    private func refreshBoard() {
        var buf = [Int8](repeating: 0, count: Self.boardCells)
        if nardi_board(handle, &buf) == NARDI_OK { board = buf }
        var d = [Int32](repeating: 0, count: 2)
        if nardi_dice(handle, &d) == NARDI_OK { dice = (Int(d[0]), Int(d[1])) }
    }

    private func loadOptions() {
        let n = Int(nardi_legal_move_count(handle))
        var opts: [[Int8]] = []
        for i in 0..<max(0, n) {
            var buf = [Int8](repeating: 0, count: Self.boardCells)
            if nardi_option_board(handle, Int32(i), &buf) == NARDI_OK { opts.append(buf) }
        }
        options = opts
    }
}
