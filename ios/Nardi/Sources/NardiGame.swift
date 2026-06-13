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

/// Resignation level a player offers: a single win (oin) or a double (mars).
enum ResignLevel: String, Identifiable {
    case oin  = "single (oin)"
    case mars = "mars (double)"
    var id: String { rawValue }
    var isMars: Bool { self == .mars }
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
    // Squares from which each die can start a move (bit row*cols+col), straight from
    // the engine's precomputed start sets. Drives the green start dots; kept in sync
    // with dieUsable since both are refreshed together from the live engine state.
    @Published private(set) var startMasks: (Int, Int) = (0, 0)
    // Click-to-move preference: which die a tapped checker is played with FIRST.
    // false = dice.0 (the larger — OnRoll puts the larger first); true = dice.1.
    // Tapping the dice flips it; it resets to the larger at the start of each turn.
    // The engine's own dice order is never touched — this is purely a UI choice.
    @Published private(set) var tryFirst = false
    @Published private(set) var selected: (Int, Int)? = nil
    @Published private(set) var flipped = false
    @Published private(set) var phase: Phase = .setup
    @Published private(set) var status = ""
    @Published private(set) var whiteOff = 0
    @Published private(set) var blackOff = 0
    @Published private(set) var canConfirm = false   // turn complete, awaiting confirm
    // A resignation the side-to-move has offered, awaiting the opponent's response
    // (pass & play only — vs-computer decides immediately). nil = no offer pending.
    @Published private(set) var pendingResign: ResignLevel? = nil

    // Move animation: the checker(s) currently in flight (each FlightView self-
    // animates, so there's no per-frame progress to publish).
    @Published private(set) var flights: [Flight] = []
    @Published private(set) var isAnimating = false
    // Replay: the last actual move animated (by EITHER side). Replay is offered only
    // while it's the other side's turn, so it always re-plays the opponent's move.
    private var lastMove: ReplayMove? = nil
    // Accumulates the in-progress human turn's hops so the whole turn can be replayed.
    private var turnSubs: [(from: (Int, Int)?, to: (Int, Int)?)] = []
    private var turnStartBoard: [Int8] = []
    private var turnMoverSign: Int8 = 1
    static let cols = 12

    /// A move's animation captured for replay: the board before it, the ordered
    /// sub-move hops, the mover's sign, the board after, and the dice that rolled it.
    private struct ReplayMove {
        let before: [Int8]
        let subs: [(from: (Int, Int)?, to: (Int, Int)?)]
        let moverSign: Int8
        let after: [Int8]
        let dice: (Int, Int)
    }

    /// The dice of the replayable move, for the Replay button label.
    var lastMoveDice: (Int, Int)? { lastMove?.dice }

    /// Whether the in-progress human turn has a sub-move to take back (drives the
    /// center Undo button). Derived from the accumulated hops, so it tracks taps/undos.
    var canUndo: Bool { phase == .awaitingHuman && !turnSubs.isEmpty && !isAnimating }

    /// Offer Replay only when the recorded move was made by the OPPONENT of whoever
    /// is now to move — so it's always "their last move", never your own, and it's
    /// unavailable right after a pass (a pass records no move, so lastMove is stale
    /// and belongs to the current player).
    var canReplay: Bool {
        guard let lm = lastMove, !isAnimating, phase != .botThinking else { return false }
        let blackToMove = (nardi_current_player(handle) == 1)
        return (lm.moverSign < 0) != blackToMove
    }

    /// Record `move` as the replayable move and capture the dice it was rolled with.
    private func recordReplay(before: [Int8], subs: [(from: (Int, Int)?, to: (Int, Int)?)],
                              moverSign: Int8, after: [Int8]) {
        guard !subs.isEmpty else { return }
        lastMove = ReplayMove(before: before, subs: subs, moverSign: moverSign, after: after, dice: dice)
    }

    private let handle: OpaquePointer
    // Scratch engines used only to judge a resignation offer (vs-computer): the
    // strong vzg0 net and the calibrated res2 net. Independent of the playing
    // opponent's net, so the accept rule is the same at every difficulty.
    private let vzgEvalHandle: OpaquePointer
    private let res2EvalHandle: OpaquePointer
    private let vzgEvalLoaded: Bool
    private let res2EvalLoaded: Bool
    private var modelLoaded: Bool = false
    private var mode: GameMode = .vsComputer
    private var opponent: Opponent = .medium   // remembered for the saved record
    private var humanIsWhite = true   // vs-computer orientation (fixed)

    /// Called once when a real (non-dev) game finishes, so the app can archive it
    /// to the match history. Injected by the app; nil disables archiving.
    var onGameFinished: ((SavedMatch) -> Void)?
    private var matchSaved = false   // guards against double-archiving one game
    /// id of the SavedMatch for the just-finished game, so Review can open + cache it.
    @Published private(set) var lastMatchID: UUID? = nil

    // Post-game review: one record per completed turn, plus a stash for the
    // in-progress human turn (recorded on confirm / game-ending move).
    @Published private(set) var reviewLog: [ReviewTurn] = []
    private var humanTurnPre: ([Int8], Bool)? = nil
    private var humanTurnDice: (Int, Int) = (0, 0)
    /// Whose play to review: the human (vs-computer) or White (pass & play).
    var reviewSide: Bool { mode == .vsComputer ? !humanIsWhite : false }
    var hasReview: Bool { !reviewLog.isEmpty }

    init() {
        guard let h = nardi_create(), let v = nardi_create(), let r = nardi_create() else {
            fatalError("nardi_create failed")
        }
        handle = h; vzgEvalHandle = v; res2EvalHandle = r
        // Load the resignation-judge nets first (direct C calls, no self), so all
        // stored properties are initialized before loadModel() (a method) is called.
        vzgEvalLoaded  = Bundle.main.path(forResource: "vzg0", ofType: "nardiw").map { nardi_load_model(v, $0) == NARDI_OK } ?? false
        res2EvalLoaded = Bundle.main.path(forResource: "res2", ofType: "nardiw").map { nardi_load_model(r, $0) == NARDI_OK } ?? false
        // Default to the strong network so the dev self-play hooks (which use a
        // model bot) have one loaded; newGame swaps in the chosen opponent's net.
        modelLoaded = loadModel("vzg0")
    }
    deinit { nardi_destroy(handle); nardi_destroy(vzgEvalHandle); nardi_destroy(res2EvalHandle) }

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

    /// Board orientation: vs-computer is fixed to the human's perspective; pass-and-play
    /// flips to the active player each turn, unless the auto-flip preference is off.
    private func desiredFlip() -> Bool {
        let whiteToMove = (nardi_current_player(handle) == 0)
        return isPassAndPlay ? (BoardFlip.auto ? !whiteToMove : false) : !humanIsWhite
    }
    /// Re-apply the orientation when the auto-flip preference is toggled mid-game.
    func refreshFlip() { flipped = desiredFlip() }

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
        lastMove = nil
        turnSubs = []
        pendingResign = nil
        lastMatchID = nil
        reviewLog = []
        humanTurnPre = nil
        board = engineBoard()   // opening position shows instantly (no animation)
        // Orient the board for the human from the first frame (same rule as
        // beginHumanTurn): the perspective player's head sits top-right, so flip
        // iff that player is Black. Otherwise a Black human sees the board
        // unflipped until their first turn, then it suddenly rotates.
        flipped = desiredFlip()
        refreshMeta()
        advanceLoop()
    }

    // MARK: - Human interaction

    /// Click-to-move: tapping a checker plays it with the preferred die (the one
    /// tried first), falling back to the other die. Which die is "first" is the
    /// `tryFirst` preference (flipped by tapping the dice). The per-die start sets
    /// tell us which die can actually start from the tapped square.
    func tap(row: Int, col: Int) {
        guard phase == .awaitingHuman, !isAnimating, pendingResign == nil else { return }
        let bit = 1 << (row * Self.cols + col)
        let firstIdx = tryFirst ? 1 : 0
        let secondIdx = tryFirst ? 0 : 1
        let idx = (startMask(firstIdx) & bit) != 0 ? firstIdx
                : (startMask(secondIdx) & bit) != 0 ? secondIdx
                : -1
        guard idx >= 0 else { return }   // no move starts from here with either die
        _ = nardi_human_select(handle, Int32(row), Int32(col))
        playSelected(byDie: idx)
    }

    /// Flip which die a tapped checker is played with first (tapping the dice
    /// "reverses" them). Purely a UI preference — the engine's dice are untouched.
    func toggleTryFirst() {
        guard phase == .awaitingHuman, !isAnimating, pendingResign == nil else { return }
        tryFirst.toggle()
    }

    /// The die index (0/1) the UI tries first this turn (0 = the larger die).
    var firstDieIndex: Int { tryFirst ? 1 : 0 }
    func startMask(_ idx: Int) -> Int { idx == 0 ? startMasks.0 : startMasks.1 }

    /// The two dice in UI order — the first entry is the one a tapped checker is
    /// played with first. `hasStart` is whether that die can currently start any
    /// move (for greying); `isFirst` marks the preferred die for highlighting.
    ///
    /// Doubles are a single value, so both dice share one start state (the engine's
    /// per-index budgets can diverge mid-turn): grey both, or neither — never one.
    var orderedDice: [(value: Int, hasStart: Bool, isFirst: Bool)] {
        let f = firstDieIndex, s = (firstDieIndex == 0 ? 1 : 0)
        let doubles = (dice.0 == dice.1)
        let anyStart = (startMasks.0 | startMasks.1) != 0
        func entry(_ i: Int, _ isFirst: Bool) -> (value: Int, hasStart: Bool, isFirst: Bool) {
            (i == 0 ? dice.0 : dice.1, doubles ? anyStart : startMask(i) != 0, isFirst)
        }
        return [entry(f, true), entry(s, false)]
    }

    private func playSelected(byDie idx: Int) {
        if nardi_human_move_die(handle, Int32(idx)) != NARDI_OK {
            status = "Illegal move."
            return
        }
        selected = nil   // click-to-move: no lingering selection (green dots show next starts)
        Task { @MainActor in
            await animateMoves()
            turnSubs += recentMoves()   // accumulate this hop into the turn for replay
            // A move that ends the game finalizes immediately (no confirm needed).
            if nardi_is_terminal(handle) == 1 {
                recordReplay(before: turnStartBoard, subs: turnSubs, moverSign: turnMoverSign, after: engineBoard())
                recordHumanTurn()
                saveMatchIfNeeded()
                phase = .gameOver(message: outcomeMessage())
                return
            }
            selected = nil
            updateConfirmState()
        }
    }

    func confirm() {
        guard phase == .awaitingHuman, canConfirm, !isAnimating, pendingResign == nil else { return }
        recordReplay(before: turnStartBoard, subs: turnSubs, moverSign: turnMoverSign, after: engineBoard())
        recordHumanTurn()
        nardi_confirm_turn(handle)
        canConfirm = false
        selected = nil
        advanceLoop()   // advance to the next player / bot
    }

    // MARK: - Resignation (offer oin / mars)

    /// The side to move may offer a resignation on their own turn.
    var canOffer: Bool { phase == .awaitingHuman && !isAnimating && pendingResign == nil }

    /// Offer to resign at `level`. Pass & play: the opponent gets accept/decline
    /// buttons. vs-computer: VZG-0 decides right away (it only ever responds).
    func offerResign(_ level: ResignLevel) {
        guard canOffer else { return }
        if isPassAndPlay {
            pendingResign = level
            let who = (nardi_current_player(handle) == 0) ? "White" : "Black"
            status = "\(who) offers to resign — \(level.rawValue). Opponent: accept or decline?"
        } else if botAcceptsResign(level) {
            acceptResign(level)
        } else {
            status = "Computer declined your \(level.rawValue) offer — play on."
        }
    }

    /// Pass & play: the opponent accepts or declines the pending offer.
    func respondResign(accept: Bool) {
        guard let level = pendingResign else { return }
        pendingResign = nil
        if accept {
            acceptResign(level)
        } else {
            let who = (nardi_current_player(handle) == 0) ? "White" : "Black"
            status = "Offer declined — \(who) to move."
        }
    }

    /// Conclude the game from an accepted resignation: the offerer (the side to move)
    /// loses; the opponent wins at `level` (oin = normal, mars = double).
    private func acceptResign(_ level: ResignLevel) {
        let offererIsWhite = (nardi_current_player(handle) == 0)
        let winnerWhite = !offererIsWhite
        saveResignedMatch(winnerWhite: winnerWhite, mars: level.isMars)
        phase = .gameOver(message: resignMessage(winnerWhite: winnerWhite, mars: level.isMars))
    }

    /// VZG-0's response (vs-computer). Mars is free points, so always accept. For oin,
    /// accept only when VZG-0 is NOT comfortably ahead: max(vzg0, res2) < 0.5 from its
    /// own perspective (else it declines and plays on for a possible mars).
    private func botAcceptsResign(_ level: ResignLevel) -> Bool {
        if level.isMars { return true }
        return botEval() < 0.5
    }

    /// Max of the vzg0 and res2 static evals of the current board, in VZG-0's frame.
    /// Mid-turn the side to move is the human, so evaluate side-to-move and NEGATE;
    /// once the turn is complete (awaiting confirm) it's effectively VZG-0 to move, so
    /// evaluate from its side directly. Never scores from the human's point of view.
    private func botEval() -> Float {
        let b = engineBoard()
        let humanSide: Int32 = humanIsWhite ? 0 : 1
        let botSide: Int32 = humanIsWhite ? 1 : 0
        let turnComplete = (nardi_turn_is_complete(handle) == 1)

        func eval(_ h: OpaquePointer, _ loaded: Bool, side: Int32) -> Float? {
            guard loaded else { return nil }
            var v: Float = 0
            let ok = b.withUnsafeBufferPointer { buf -> Bool in
                nardi_set_position(h, buf.baseAddress, side) == NARDI_OK
                    && nardi_evaluate_position(h, &v) == NARDI_OK
            }
            return ok ? v : nil
        }
        func botValue(_ h: OpaquePointer, _ loaded: Bool) -> Float? {
            turnComplete ? eval(h, loaded, side: botSide)
                         : eval(h, loaded, side: humanSide).map { -$0 }
        }

        let vals = [botValue(vzgEvalHandle, vzgEvalLoaded), botValue(res2EvalHandle, res2EvalLoaded)].compactMap { $0 }
        // No eval available (blobs missing): treat as not-ahead so the offer is accepted.
        return vals.max() ?? -.greatestFiniteMagnitude
    }

    private func resignMessage(winnerWhite: Bool, mars: Bool) -> String {
        let tag = mars ? " (mars!)" : ""
        if mode == .vsComputer {
            return (winnerWhite == humanIsWhite) ? "You win!\(tag)" : "Computer wins\(tag) — you resigned."
        }
        return (winnerWhite ? "White" : "Black") + " wins\(tag) by resignation."
    }

    /// Archive a resigned game (engine is NOT terminal, so winner/mars are explicit).
    private func saveResignedMatch(winnerWhite: Bool, mars: Bool) {
        guard !matchSaved, let save = onGameFinished else { return }
        matchSaved = true
        let id = UUID()
        lastMatchID = id
        save(SavedMatch(id: id, date: Date(),
                        modeRaw: mode.rawValue,
                        opponentRaw: mode == .vsComputer ? opponent.rawValue : nil,
                        reviewSide: reviewSide,
                        winnerWhite: winnerWhite, mars: mars,
                        turns: reviewLog.map { SavedTurn($0) }))
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
        guard phase == .awaitingHuman, !isAnimating, pendingResign == nil else { return }
        nardi_human_undo(handle)
        selected = nil
        Task { @MainActor in
            await animateMoves()   // slide the checker back
            if !turnSubs.isEmpty { turnSubs.removeLast() }   // drop the undone hop
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
                    // Record this move for replay (mover = the side that just moved).
                    recordReplay(before: board, subs: recentMoves(),
                                 moverSign: preSide ? -1 : 1, after: engineBoard())
                    // This step is either the computer's move or a FORCED auto-play of
                    // the human's move; word it for whoever actually moved (preSide:
                    // true = black). Pass & play has no computer, so it's always auto.
                    let moverIsHuman = isPassAndPlay || (!preSide == humanIsWhite)
                    status = moverIsHuman ? "Auto-played \(dice.0)-\(dice.1)…"
                                          : "Opponent played \(dice.0)-\(dice.1)…"
                    await animateMoves()
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
        flipped = desiredFlip()
        // Start accumulating this turn's hops (for replaying the whole turn later).
        turnSubs = []
        turnStartBoard = engineBoard()
        turnMoverSign = whiteToMove ? 1 : -1
        selected = nil
        tryFirst = false          // default to the larger die each new turn
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
        startMasks = (Int(nardi_starts_mask(handle, 0)), Int(nardi_starts_mask(handle, 1)))
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

    /// Animate the last command's sub-moves (pure display). Replay recording is done
    /// by the callers (advanceLoop for the bot, confirm/tapDie for a human turn), not
    /// here, so a turn is recorded once as a whole rather than hop by hop.
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

        await runFlights(ReplayMove(before: before, subs: moves, moverSign: moverSign, after: after, dice: dice))
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

    /// Re-watch the opponent's last move. It rewinds to just before that move,
    /// replays the slide, then settles on the current true board (in case the user
    /// has already started their own turn since).
    func replayLastMove() {
        guard canReplay, let lm = lastMove else { return }
        let replay = ReplayMove(before: lm.before, subs: lm.subs, moverSign: lm.moverSign,
                                after: engineBoard(), dice: lm.dice)
        Task { @MainActor in await runFlights(replay) }
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
        let id = UUID()
        lastMatchID = id    // so the Review button can open (and cache) this match
        save(SavedMatch(id: id, date: Date(),
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
