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

/// A checker sliding during a move animation. `to == nil` means it was borne off
/// (slides off the board); engine coords (row, col).
struct Flight: Identifiable {
    let id = UUID()
    let from: (Int, Int)?
    let to: (Int, Int)?
    let white: Bool
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

    // Move animation: checkers in flight + a 0->1 progress the board view lerps.
    @Published private(set) var flights: [Flight] = []
    @Published private(set) var animProgress: CGFloat = 0
    @Published private(set) var isAnimating = false
    static let cols = 12
    private let animDuration: Double = 0.45

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
        flights = []
        isAnimating = false
        board = engineBoard()   // opening position shows instantly (no animation)
        refreshMeta()
        advanceLoop()
    }

    // MARK: - Human interaction

    func tap(row: Int, col: Int) {
        guard phase == .awaitingHuman, !isAnimating else { return }
        _ = nardi_human_select(handle, Int32(row), Int32(col))
        updateSelection()
    }

    func tapDie(_ idx: Int) {
        guard phase == .awaitingHuman, !isAnimating else { return }
        guard nardi_start_selected(handle) == 1 else {
            status = "Select a checker first, then tap a die."
            return
        }
        if nardi_human_move_die(handle, Int32(idx)) != NARDI_OK {
            status = "Illegal move for that die."
            return
        }
        selected = nil   // hide highlight while the checker slides
        Task { @MainActor in
            await animateMoves()
            // A move that ends the game finalizes immediately (no confirm needed).
            if nardi_is_terminal(handle) == 1 {
                phase = .gameOver(message: outcomeMessage())
                return
            }
            // The turn does NOT auto-advance: keep the destination selected so dice
            // can be chained; once no legal moves remain, enable Confirm (the player
            // may still Undo before confirming).
            updateSelection()
            updateConfirmState()
        }
    }

    func confirm() {
        guard phase == .awaitingHuman, canConfirm, !isAnimating else { return }
        nardi_confirm_turn(handle)
        canConfirm = false
        selected = nil
        advanceLoop()   // advance to the next player / bot
    }

    func undo() {
        guard phase == .awaitingHuman, !isAnimating else { return }
        nardi_human_undo(handle)
        selected = nil
        Task { @MainActor in
            await animateMoves()   // slide the checker back
            updateSelection()
            updateConfirmState()
        }
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
                refreshMeta()
                // Only a BotMoved step applied a move (and recorded sub-moves); the
                // other steps just rolled / passed / ended, so nothing to animate.
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
                case NARDI_STEP_TURN_PASSED:
                    status = "No legal moves — passing."
                    try? await Task.sleep(nanoseconds: 400_000_000)
                default:                       // BotMoved (bot or forced auto-play)
                    status = "Opponent played \(dice.0)-\(dice.1)…"
                    await animateMoves()       // slide each sub-move in sequence
                    try? await Task.sleep(nanoseconds: 200_000_000)
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

    private func engineBoard() -> [Int8] {
        var buf = [Int8](repeating: 0, count: Self.cells)
        _ = nardi_board(handle, &buf)
        return buf
    }

    /// Refresh dice / off-counts / borne-off from the TRUE engine state (the
    /// displayed `board` may lag during a slide animation).
    private func refreshMeta() {
        var d = [Int32](repeating: 0, count: 2)
        if nardi_dice(handle, &d) == NARDI_OK { dice = (Int(d[0]), Int(d[1])) }
        dieUsable = (nardi_can_use_die(handle, 0) == 1, nardi_can_use_die(handle, 1) == 1)
        let eb = engineBoard()
        whiteOff = 15 - eb.reduce(0) { $0 + max(0, Int($1)) }
        blackOff = 15 - eb.reduce(0) { $0 + max(0, -Int($1)) }
    }

    /// Read the sub-moves applied by the last move command, in order.
    private func recentMoves() -> [(from: (Int, Int)?, to: (Int, Int)?)] {
        let n = Int(nardi_move_count(handle))
        var out: [(from: (Int, Int)?, to: (Int, Int)?)] = []
        for i in 0..<max(0, n) {
            var a = [Int32](repeating: -1, count: 4)
            if nardi_get_move(handle, Int32(i), &a) == NARDI_OK {
                let from = a[0] >= 0 ? (Int(a[0]), Int(a[1])) : nil
                let to = a[2] >= 0 ? (Int(a[2]), Int(a[3])) : nil
                out.append((from, to))
            }
        }
        return out
    }

    /// Animate each sub-move of the last command separately, one finishing before
    /// the next starts (a checker played with both dice slides in two hops). The
    /// displayed board is advanced sub-move by sub-move so the animation matches
    /// the engine's true sequence (including intermediate landings / bear-offs).
    private func animateMoves() async {
        refreshMeta()
        let moves = recentMoves()
        guard !moves.isEmpty else { board = engineBoard(); return }

        // One mover per turn (nardi has no hitting). Take its sign from the first
        // source cell on the displayed board.
        var moverSign: Int8 = 0
        for m in moves {
            if let (r, c) = m.from { let v = board[r * Self.cols + c]; if v != 0 { moverSign = v > 0 ? 1 : -1; break } }
        }
        if moverSign == 0 { moverSign = (nardi_sign(handle) >= 0) ? 1 : -1 }   // e.g. undone bear-off

        isAnimating = true
        var disp = board
        for m in moves {
            if let (r, c) = m.from { disp[r * Self.cols + c] -= moverSign }   // lift off source
            board = disp
            flights = [Flight(from: m.from, to: m.to, white: moverSign > 0)]
            animProgress = 0
            withAnimation(.easeOut(duration: animDuration)) { animProgress = 1 }
            try? await Task.sleep(nanoseconds: UInt64(animDuration * 1_000_000_000))
            if let (r, c) = m.to { disp[r * Self.cols + c] += moverSign }      // land on dest
            flights = []
            animProgress = 0
            board = disp
            try? await Task.sleep(nanoseconds: 70_000_000)                     // brief gap between hops
        }
        isAnimating = false
        board = engineBoard()   // safety sync
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
