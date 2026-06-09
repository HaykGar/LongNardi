import Foundation
import SwiftUI

enum Opponent: String, CaseIterable, Identifiable {
    case easy = "Easy"      // hand-crafted heuristic, no network
    case medium = "Medium"  // greedy over the small MLP value net
    case hard = "Hard"      // 1-ply lookahead over the Polyak-averaged ResNardiNet
    var id: String { rawValue }
    var strategy: NardiStrategy {
        switch self {
        case .easy: return NARDI_HEURISTIC
        case .medium: return NARDI_GREEDY
        case .hard: return NARDI_LOOKAHEAD
        }
    }
    /// Bundled `.nardiw` value network this opponent plays with (nil = none, the
    /// heuristic needs no network). See ios/scripts/make_model.sh.
    var modelResource: String? {
        switch self {
        case .easy: return nil
        case .medium: return "mlp"
        case .hard: return "vzg0"
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

/// One completed turn, recorded during play to drive post-game review. `moved`
/// is false for a forced pass (no legal move). `preSide` is who moved this turn;
/// the position after it (postBoard) has `!preSide` to move.
struct ReviewTurn {
    let preBoard: [Int8]
    let preSide: Bool
    let dice: (Int, Int)
    let postBoard: [Int8]
    let moved: Bool
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

    // Move animation: the checker(s) currently in flight (each FlightView self-
    // animates, so there's no per-frame progress to publish).
    @Published private(set) var flights: [Flight] = []
    @Published private(set) var isAnimating = false
    // Replay: the most recently animated move, re-playable via replayLastMove().
    @Published private(set) var canReplay = false
    private var lastMove: ReplayMove? = nil
    static let cols = 12

    /// A move's animation captured for replay: the board before it, the ordered
    /// sub-move hops, the mover's sign, and the board after.
    private struct ReplayMove {
        let before: [Int8]
        let subs: [(from: (Int, Int)?, to: (Int, Int)?)]
        let moverSign: Int8
        let after: [Int8]
    }

    private let handle: OpaquePointer
    private var modelLoaded: Bool = false
    private var mode: GameMode = .vsComputer
    private var opponent: Opponent = .medium   // remembered for the saved record
    private var humanIsWhite = true   // vs-computer orientation (fixed)

    /// Called once when a real (non-dev) game finishes, so the app can archive it
    /// to the match history. Injected by the app; nil disables archiving.
    var onGameFinished: ((SavedMatch) -> Void)?
    private var matchSaved = false   // guards against double-archiving one game

    // Post-game review: one record per completed turn, plus a stash for the
    // in-progress human turn (recorded on confirm / game-ending move).
    @Published private(set) var reviewLog: [ReviewTurn] = []
    private var humanTurnPre: ([Int8], Bool)? = nil
    private var humanTurnDice: (Int, Int) = (0, 0)
    /// Whose play to review: the human (vs-computer) or White (pass & play).
    var reviewSide: Bool { mode == .vsComputer ? !humanIsWhite : false }
    var hasReview: Bool { !reviewLog.isEmpty }

    init() {
        guard let h = nardi_create() else { fatalError("nardi_create failed") }
        handle = h
        // Default to the strong network so the dev self-play hooks (which use a
        // model bot) have one loaded; newGame swaps in the chosen opponent's net.
        modelLoaded = loadModel("vzg0")
    }
    deinit { nardi_destroy(handle) }

    /// Load a bundled `.nardiw` value network into the engine. Returns whether it
    /// loaded; a missing blob means the project needs `make_model.sh` + xcodegen.
    @discardableResult
    private func loadModel(_ resource: String) -> Bool {
        guard let path = Bundle.main.path(forResource: resource, ofType: "nardiw") else {
            return false
        }
        return nardi_load_model(handle, path) == NARDI_OK
    }

    var isPassAndPlay: Bool { mode == .passAndPlay }

    func newGame(mode: GameMode, opponent: Opponent, first: FirstMove) {
        self.mode = mode
        self.opponent = opponent
        matchSaved = false
        switch mode {
        case .passAndPlay:
            nardi_configure_players(handle, NARDI_HUMAN, NARDI_HUMAN)
        case .vsComputer:
            humanIsWhite = (first == .first) || (first == .random && Bool.random())
            let bot = opponent.strategy
            nardi_configure_players(handle,
                                    humanIsWhite ? NARDI_HUMAN : bot,
                                    humanIsWhite ? bot : NARDI_HUMAN)
            // Load this opponent's value network (the heuristic needs none).
            if let res = opponent.modelResource {
                modelLoaded = loadModel(res)
            } else {
                modelLoaded = true
            }
        }
        nardi_reset(handle)
        selected = nil
        flights = []
        isAnimating = false
        canReplay = false
        lastMove = nil
        reviewLog = []
        humanTurnPre = nil
        board = engineBoard()   // opening position shows instantly (no animation)
        // Orient the board for the human from the first frame (same rule as
        // beginHumanTurn): the perspective player's head sits top-right, so flip
        // iff that player is Black. Otherwise a Black human sees the board
        // unflipped until their first turn, then it suddenly rotates.
        let whiteToMove = (nardi_current_player(handle) == 0)
        flipped = isPassAndPlay ? !whiteToMove : !humanIsWhite
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
            updateSelection()   // engine deselects on an illegal move — mirror that in the UI
            return
        }
        selected = nil   // hide highlight while the checker slides
        Task { @MainActor in
            await animateMoves()
            // A move that ends the game finalizes immediately (no confirm needed).
            if nardi_is_terminal(handle) == 1 {
                recordHumanTurn()
                saveMatchIfNeeded()
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
        recordHumanTurn()
        nardi_confirm_turn(handle)
        canConfirm = false
        selected = nil
        advanceLoop()   // advance to the next player / bot
    }

    /// Append the just-completed human turn to the review log (post = current
    /// engine board; confirm/finalize doesn't change the board, only the side).
    private func recordHumanTurn() {
        guard let pre = humanTurnPre else { return }
        reviewLog.append(ReviewTurn(preBoard: pre.0, preSide: pre.1,
                                    dice: humanTurnDice, postBoard: engineBoard(), moved: true))
        humanTurnPre = nil
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
                let preBoard = engineBoard()
                let preSide = (nardi_current_player(handle) == 1)
                let step = nardi_advance(handle)
                refreshMeta()
                // Only a BotMoved step applied a move (and recorded sub-moves); the
                // other steps just rolled / passed / ended, so nothing to animate.
                switch step {
                case NARDI_STEP_GAME_OVER:
                    saveMatchIfNeeded()
                    phase = .gameOver(message: outcomeMessage())
                    return
                case NARDI_STEP_AWAITING_HUMAN:
                    humanTurnPre = (preBoard, preSide)   // completed at confirm / game end
                    humanTurnDice = dice
                    beginHumanTurn()
                    return
                case NARDI_STEP_ERROR:
                    status = "Engine error: " + String(cString: nardi_last_error(handle))
                    return
                case NARDI_STEP_TURN_PASSED:
                    reviewLog.append(ReviewTurn(preBoard: preBoard, preSide: preSide,
                                                dice: dice, postBoard: preBoard, moved: false))
                    status = "No legal moves — passing."
                    try? await Task.sleep(nanoseconds: 400_000_000)
                default:                       // BotMoved (bot or forced auto-play)
                    reviewLog.append(ReviewTurn(preBoard: preBoard, preSide: preSide,
                                                dice: dice, postBoard: engineBoard(), moved: true))
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

    /// Animate the last command's sub-moves and stash them so the move can be
    /// replayed (e.g. to re-watch what the computer did).
    private func animateMoves() async {
        refreshMeta()
        let moves = recentMoves()
        let before = board
        let after = engineBoard()
        guard !moves.isEmpty else { board = after; return }

        // One mover per turn (nardi has no hitting). Take its sign from the first
        // source cell on the displayed board.
        var moverSign: Int8 = 0
        for m in moves {
            if let (r, c) = m.from { let v = before[r * Self.cols + c]; if v != 0 { moverSign = v > 0 ? 1 : -1; break } }
        }
        if moverSign == 0 { moverSign = (nardi_sign(handle) >= 0) ? 1 : -1 }   // e.g. undone bear-off

        lastMove = ReplayMove(before: before, subs: moves, moverSign: moverSign, after: after)
        canReplay = true
        await runFlights(lastMove!)
    }

    /// Slide each sub-move in sequence (pure display; touches no engine state), one
    /// finishing before the next starts. The displayed board advances hop by hop so
    /// it matches the engine's true sequence (intermediate landings / bear-offs).
    private func runFlights(_ move: ReplayMove) async {
        isAnimating = true
        var disp = move.before
        board = disp
        let dur = UInt64(BoardCanvas.flightDuration * 1_000_000_000)
        for m in move.subs {
            if let (r, c) = m.from { disp[r * Self.cols + c] -= move.moverSign }   // lift off source
            board = disp
            flights = [Flight(from: m.from, to: m.to, white: move.moverSign > 0)]
            try? await Task.sleep(nanoseconds: dur)
            if let (r, c) = m.to { disp[r * Self.cols + c] += move.moverSign }      // land on dest
            flights = []
            board = disp
            try? await Task.sleep(nanoseconds: 70_000_000)                          // brief gap between hops
        }
        isAnimating = false
        board = move.after
    }

    /// Re-play the most recent move's animation. The displayed board sits at the
    /// post-move position between animations, so replaying ends right back there.
    func replayLastMove() {
        guard let lm = lastMove, !isAnimating, phase != .botThinking else { return }
        Task { @MainActor in await runFlights(lm) }
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

    /// Archive the just-finished game to the match history exactly once. The
    /// engine is in its terminal state here, so winner/mars read straight off it.
    private func saveMatchIfNeeded() {
        guard !matchSaved, !reviewLog.isEmpty, let save = onGameFinished else { return }
        matchSaved = true
        let whiteWon = (nardi_current_player(handle) == 1)   // loser is to move at game end
        let mars = (nardi_winner_result(handle) == 2)
        save(SavedMatch(id: UUID(), date: Date(),
                        modeRaw: mode.rawValue,
                        opponentRaw: mode == .vsComputer ? opponent.rawValue : nil,
                        reviewSide: reviewSide,
                        winnerWhite: whiteWon, mars: mars,
                        turns: reviewLog.map { SavedTurn($0) }))
    }

    func backToSetup() { phase = .setup }

    /// Synchronously self-play random(white) vs greedy(black) to completion,
    /// returning the per-turn log. Used by the dev review/history hooks.
    private func selfPlayLog() -> [ReviewTurn] {
        nardi_configure_players(handle, NARDI_RANDOM, NARDI_GREEDY)
        nardi_reset(handle)
        var log: [ReviewTurn] = []
        var guardCount = 0
        while nardi_should_continue(handle) == 1 && guardCount < 600 {
            guardCount += 1
            let preBoard = engineBoard()
            let preSide = (nardi_current_player(handle) == 1)
            let step = nardi_advance(handle)
            var d = [Int32](repeating: 0, count: 2); _ = nardi_dice(handle, &d)
            let dv = (Int(d[0]), Int(d[1]))
            if step == NARDI_STEP_TURN_PASSED {
                log.append(ReviewTurn(preBoard: preBoard, preSide: preSide, dice: dv, postBoard: preBoard, moved: false))
            } else if step == NARDI_STEP_BOT_MOVED {
                log.append(ReviewTurn(preBoard: preBoard, preSide: preSide, dice: dv, postBoard: engineBoard(), moved: true))
            } else {
                break
            }
        }
        return log
    }

    /// Dev/verification only: self-play to completion, recording the review log,
    /// then land on game-over so the Review screen can be opened (white = reviewed
    /// side, so it has real blunders).
    func devAutoPlayAndReview() {
        mode = .passAndPlay   // reviewSide == White
        humanTurnPre = nil
        reviewLog = selfPlayLog()
        refreshMeta()
        board = engineBoard()
        phase = .gameOver(message: outcomeMessage())
    }

    /// Dev/verification only: self-play a few games with varied mode/opponent
    /// labels and archive them, so the History tab has data to render.
    func devSeedHistory() {
        let setups: [(GameMode, Opponent?)] = [
            (.vsComputer, .hard), (.vsComputer, .medium), (.passAndPlay, nil),
        ]
        for (i, (m, opp)) in setups.enumerated() {
            let log = selfPlayLog()
            guard !log.isEmpty else { continue }
            let whiteWon = (nardi_current_player(handle) == 1)
            let mars = (nardi_winner_result(handle) == 2)
            onGameFinished?(SavedMatch(
                id: UUID(), date: Date().addingTimeInterval(Double(-i) * 3600),
                modeRaw: m.rawValue, opponentRaw: opp?.rawValue, reviewSide: false,
                winnerWhite: whiteWon, mars: mars, turns: log.map { SavedTurn($0) }))
        }
    }
}
