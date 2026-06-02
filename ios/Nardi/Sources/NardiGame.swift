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
            await animateTransition()
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
            await animateTransition()   // slide the checker back
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
                switch step {
                case NARDI_STEP_GAME_OVER:
                    await animateTransition()
                    phase = .gameOver(message: outcomeMessage())
                    return
                case NARDI_STEP_AWAITING_HUMAN:
                    await animateTransition()   // animate any forced move that preceded
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
                    await animateTransition()   // slide the checker(s)
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

    /// Animate the board from its current displayed state to the engine's state by
    /// sliding the moved checker(s). nardi has no hitting, so only the mover's
    /// checkers change between two states.
    private func animateTransition() async {
        refreshMeta()
        let old = board
        let new = engineBoard()
        guard old != new else { return }
        let fs = computeFlights(old: old, new: new)
        guard !fs.isEmpty else { board = new; return }

        // Show sources already decremented; the flying checkers carry them across.
        var mid = old
        for f in fs {
            if let (r, c) = f.from {
                let i = r * Self.cols + c
                if mid[i] > 0 { mid[i] -= 1 } else if mid[i] < 0 { mid[i] += 1 }
            }
        }
        board = mid
        flights = fs
        isAnimating = true
        animProgress = 0
        withAnimation(.easeOut(duration: animDuration)) { animProgress = 1 }
        try? await Task.sleep(nanoseconds: UInt64(animDuration * 1_000_000_000))
        flights = []
        animProgress = 0
        isAnimating = false
        board = new
    }

    private func computeFlights(old: [Int8], new: [Int8]) -> [Flight] {
        var deps: [(Int, Int)] = []   // cells losing checkers (one entry per checker)
        var arrs: [(Int, Int)] = []
        var white = true
        for i in 0..<Self.cells {
            let o = Int(old[i]); let n = Int(new[i])
            if o == n { continue }
            white = (o != 0) ? (o > 0) : (n > 0)
            let r = i / Self.cols, c = i % Self.cols
            let delta = abs(n) - abs(o)
            if delta < 0 { for _ in 0..<(-delta) { deps.append((r, c)) } }
            else { for _ in 0..<delta { arrs.append((r, c)) } }
        }
        // Pair sorted by column (then row) so slides read as forward motion.
        let key: ((Int, Int)) -> (Int, Int) = { ($0.1, $0.0) }
        deps.sort { key($0) < key($1) }
        arrs.sort { key($0) < key($1) }
        var fs: [Flight] = []
        let paired = min(deps.count, arrs.count)
        for k in 0..<paired { fs.append(Flight(from: deps[k], to: arrs[k], white: white)) }
        for k in paired..<deps.count { fs.append(Flight(from: deps[k], to: nil, white: white)) }  // borne off
        return fs
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
