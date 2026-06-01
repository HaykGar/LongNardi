import Foundation
import SwiftUI

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

enum GameMode: String, CaseIterable, Identifiable {
    case vsComputer = "vs Computer"
    case passAndPlay = "Pass & Play"
    var id: String { rawValue }
}

enum FirstMove: String, CaseIterable, Identifiable {
    case first = "I play first (White)"
    case second = "I play second (Black)"
    case random = "Random"
    var id: String { rawValue }
}

enum Phase: Equatable {
    case setup
    case botThinking
    case awaitingHuman
    case gameOver(message: String)
}

@MainActor
final class NardiGame: ObservableObject {
    static let cells = Int(NARDI_BOARD_CELLS)

    @Published private(set) var board: [Int8] = Array(repeating: 0, count: cells)
    @Published private(set) var dice: (Int, Int) = (0, 0)
    @Published private(set) var dieUsable: (Bool, Bool) = (false, false)
    @Published private(set) var selected: (Int, Int)? = nil
    @Published private(set) var flipped = false
    @Published private(set) var phase: Phase = .setup
    @Published private(set) var status = ""
    @Published private(set) var whiteOff = 0
    @Published private(set) var blackOff = 0
    @Published private(set) var canConfirm = false   // turn complete, awaiting confirm

    private let handle: OpaquePointer
    private let modelLoaded: Bool
    private var mode: GameMode = .vsComputer
    private var humanIsWhite = true   // vs-computer orientation (fixed)

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

    var isPassAndPlay: Bool { mode == .passAndPlay }

    func newGame(mode: GameMode, opponent: Opponent, first: FirstMove) {
        self.mode = mode
        switch mode {
        case .passAndPlay:
            nardi_configure_players(handle, NARDI_HUMAN, NARDI_HUMAN)
        case .vsComputer:
            humanIsWhite = (first == .first) || (first == .random && Bool.random())
            let bot = opponent.strategy
            nardi_configure_players(handle,
                                    humanIsWhite ? NARDI_HUMAN : bot,
                                    humanIsWhite ? bot : NARDI_HUMAN)
            if opponent == .mcts { nardi_set_mcts_params(handle, 120, 1.0, 0, 0.1, 0.25, 0.3, 0) }
        }
        nardi_reset(handle)
        selected = nil
        refresh()
        advanceLoop()
    }

    // MARK: - Human interaction

    func tap(row: Int, col: Int) {
        guard phase == .awaitingHuman else { return }
        _ = nardi_human_select(handle, Int32(row), Int32(col))
        updateSelection()
    }

    func tapDie(_ idx: Int) {
        guard phase == .awaitingHuman else { return }
        guard nardi_start_selected(handle) == 1 else {
            status = "Select a checker first, then tap a die."
            return
        }
        if nardi_human_move_die(handle, Int32(idx)) != NARDI_OK {
            status = "Illegal move for that die."
            return
        }
        refresh()
        // A move that ends the game finalizes immediately (no confirm needed).
        if nardi_is_terminal(handle) == 1 {
            phase = .gameOver(message: outcomeMessage())
            return
        }
        // The turn does NOT auto-advance. Keep the destination selected so dice can
        // be chained; once no legal moves remain, enable Confirm (the player may
        // still Undo before confirming).
        updateSelection()
        updateConfirmState()
    }

    func confirm() {
        guard phase == .awaitingHuman, canConfirm else { return }
        nardi_confirm_turn(handle)
        canConfirm = false
        selected = nil
        advanceLoop()   // advance to the next player / bot
    }

    func undo() {
        guard phase == .awaitingHuman else { return }
        nardi_human_undo(handle)
        refresh()
        updateSelection()
        updateConfirmState()
    }

    private func updateConfirmState() {
        canConfirm = (nardi_turn_is_complete(handle) == 1)
        if canConfirm { status = "Turn complete — tap Confirm (or Undo to revise)." }
    }

    // MARK: - Engine loop

    private func advanceLoop() {
        phase = .botThinking
        Task { @MainActor in
            while true {
                let step = nardi_advance(handle)
                refresh()
                switch step {
                case NARDI_STEP_GAME_OVER:
                    phase = .gameOver(message: outcomeMessage())
                    return
                case NARDI_STEP_AWAITING_HUMAN:
                    beginHumanTurn()
                    return
                case NARDI_STEP_ERROR:
                    status = "Engine error: " + String(cString: nardi_last_error(handle))
                    return
                default:                       // BotMoved / TurnPassed
                    status = "Opponent played \(dice.0)-\(dice.1)…"
                    try? await Task.sleep(nanoseconds: 500_000_000)
                }
            }
        }
    }

    private func beginHumanTurn() {
        let whiteToMove = (nardi_current_player(handle) == 0)
        // The perspective player's head must sit at the TOP-RIGHT. White's head is
        // already top-right in the base layout (flipped=false); Black's needs the
        // 180-degree flip (swap rows + reverse the top row). So flip iff the
        // perspective player is Black. Pass-and-play uses the active player's
        // perspective (board inverts each turn); vs-computer is fixed to the human.
        let perspectiveIsBlack = isPassAndPlay ? !whiteToMove : !humanIsWhite
        flipped = perspectiveIsBlack
        selected = nil
        canConfirm = false
        phase = .awaitingHuman
        let who = isPassAndPlay ? (whiteToMove ? "White" : "Black") : "You"
        status = "\(who) to move — dice \(dice.0)-\(dice.1)."
        if !modelLoaded && mode == .vsComputer {
            status = "⚠️ model blob missing — rebuild with make_model.sh"
        }
    }

    // MARK: - State refresh

    private func refresh() {
        var buf = [Int8](repeating: 0, count: Self.cells)
        if nardi_board(handle, &buf) == NARDI_OK { board = buf }
        var d = [Int32](repeating: 0, count: 2)
        if nardi_dice(handle, &d) == NARDI_OK { dice = (Int(d[0]), Int(d[1])) }
        dieUsable = (nardi_can_use_die(handle, 0) == 1, nardi_can_use_die(handle, 1) == 1)
        let whiteOn = board.reduce(0) { $0 + max(0, Int($1)) }
        let blackOn = board.reduce(0) { $0 + max(0, -Int($1)) }
        whiteOff = 15 - whiteOn
        blackOff = 15 - blackOn
    }

    private func updateSelection() {
        if nardi_start_selected(handle) == 1 {
            var rc = [Int32](repeating: -1, count: 2)
            if nardi_selected_start(handle, &rc) == NARDI_OK { selected = (Int(rc[0]), Int(rc[1])) }
        } else {
            selected = nil
        }
    }

    private func outcomeMessage() -> String {
        // After game end the loser is to move, so winner = !current_player; white = 0.
        let whiteWon = (nardi_current_player(handle) == 1)
        let mars = (nardi_winner_result(handle) == 2)
        let tag = mars ? " (mars!)" : ""
        if mode == .vsComputer {
            return (whiteWon == humanIsWhite) ? "You win!\(tag)" : "Computer wins\(tag)."
        }
        return (whiteWon ? "White wins\(tag)" : "Black wins\(tag)")
    }

    func backToSetup() { phase = .setup }
}
