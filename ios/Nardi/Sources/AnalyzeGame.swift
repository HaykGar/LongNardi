import Foundation
import SwiftUI

/// A position we can return to via arbitrary-history Undo. Restored with
/// set_position (which bypasses the controller's per-turn undo, so undo works
/// across confirmed moves too).
private struct Snapshot { let board: [Int8]; let side: Bool }

/// An engine-recommended move shown in the analyzer: the end-board it reaches and a
/// per-checker descriptor (e.g. "1 → 7, 12 → 17"). No eval is shown — these are
/// ranked by vzg0 lookahead, whose absolute numbers we don't surface.
struct TopMove: Identifiable, Sendable {
    let id = UUID()
    let board: [Int8]
    let label: String
}

enum AnalyzePhase: Equatable { case editing, analyzing }

/// Analysis sandbox: build an arbitrary position in the editor, then set the dice
/// and play moves out by hand, watching the eval swing. The evaluation is pinned
/// to the side to move chosen at setup ("original side"); the board still flips to
/// whoever is on move, but the eval frame never changes.
@MainActor
final class AnalyzeGame: ObservableObject {
    // Immutable constants — nonisolated so the off-main dice search can read them.
    nonisolated static let cells = Int(NARDI_BOARD_CELLS)
    nonisolated static let cols = 12
    nonisolated static let pieces = 15

    // Editor
    @Published var activeColor: Bool = false      // false = white, true = black
    @Published var endgameWhite = false
    @Published var endgameBlack = false
    @Published var sideToMove: Bool = false        // who moves first in analysis
    @Published private(set) var editorBoard: [Int8] = Array(repeating: 0, count: cells)

    // Shared / display
    @Published private(set) var board: [Int8] = Array(repeating: 0, count: cells)
    @Published private(set) var flipped = false
    @Published private(set) var phase: AnalyzePhase = .editing
    @Published private(set) var status = ""

    // Analysis
    @Published private(set) var originalSide: Bool = false
    @Published private(set) var currentSide: Bool = false
    @Published var die1 = 3
    @Published var die2 = 5
    @Published private(set) var diceApplied = false
    // Stored (not computed off the engine) so SwiftUI can read it during render
    // without calling into the C++ engine on every layout pass.
    @Published private(set) var movedThisTurn = false
    // Click-to-move preference (same as the play screen): which die a tapped checker
    // is played with FIRST. false = the larger die; tapping the dice flips it. Resets
    // to the larger each new turn. The engine's dice order is never touched.
    @Published private(set) var tryFirst = false
    // The background suggestion ranking is running. Purely a UI hint (a small
    // spinner): it does NOT gate interaction — play, Back, and Follow stay live
    // because the ranking runs on a separate handle, off the main thread.
    @Published private(set) var isThinking = false
    @Published private(set) var noLegalMoves = false
    // When opened from game review, the dice can be locked to the game's actual
    // roll at each relative turn (useGameDice) or set freely.
    @Published private(set) var useGameDice = false
    @Published private(set) var hasGameDice = false
    private var gameDiceScript: [(Int, Int)] = []   // dice from the jump-in point onward
    private var gameLineBoards: [[Int8]] = []        // the game's positions from the jump-in (mainline)
    private var relTurn = 0                          // turns played since jump-in
    // Logging: when analysis starts from the standard opening, accumulate the played
    // turns so the whole game can be saved to History and reviewed.
    private var loggedTurns: [ReviewTurn] = []
    @Published private(set) var startedFromStandard = false
    @Published private(set) var loggedSaved = false
    /// Archive a logged game to the match history (injected by the app).
    var onLogSaved: ((SavedMatch) -> Void)?
    @Published private(set) var positionEval: Float? = nil
    @Published private(set) var selected: (Int, Int)? = nil
    @Published private(set) var dieUsable: (Bool, Bool) = (false, false)
    @Published private(set) var canConfirm = false
    @Published private(set) var isTerminal = false
    @Published private(set) var canUndo = false
    // Up to 3 engine-recommended moves for the current dice (best first), with
    // their resulting eval in the original-side frame and a move descriptor.
    @Published private(set) var topMoves: [TopMove] = []

    // Move animation (shared BoardCanvas flights; each FlightView self-animates).
    @Published private(set) var flights: [Flight] = []
    @Published private(set) var isAnimating = false

    let modelLoaded: Bool
    // THREE engines, each touched from exactly one thread so they never race:
    //  • `handle` (vzg0) — the LIVE interactive state, main thread only. Dice are
    //    rolled here cheaply (nardi_set_dice, no net evals) so play/back/follow are
    //    always instant; suggested moves are played by matching their end board.
    //  • `evalHandle` (res2) — the DISPLAYED eval, main thread only. res2 is well
    //    calibrated (~0 at the symmetric start) where vzg0 is biased/swingy.
    //  • `searchHandle` (vzg0) — the BACKGROUND suggestion ranking + sub-move
    //    labels. Used only on `searchQueue`, never on the main thread, so the slow
    //    one-ply lookahead can run while the user keeps interacting with `handle`.
    private let handle: OpaquePointer
    private let evalHandle: OpaquePointer
    private let searchHandle: OpaquePointer
    private var history: [Snapshot] = []
    private var turnStart: Snapshot? = nil          // position at the start of the current turn
    // The background suggestion ranking is debounced (so dragging a die 1→6 doesn't
    // rank between every step) and runs on a serial queue against `searchHandle`.
    static let suggestDebounceNanos: Int = 500_000_000   // 0.5s quiet period
    private let searchQueue = DispatchQueue(label: "nardi.analyze.suggestions")
    private var suggestWork: DispatchWorkItem? = nil     // pending/in-flight ranking
    private var suggestGen = 0      // bumped whenever suggestions become stale; late results are dropped

    init() {
        guard let h = nardi_create(), let e = nardi_create(), let s = nardi_create() else {
            fatalError("nardi_create failed")
        }
        handle = h; evalHandle = e; searchHandle = s
        let sel  = Bundle.main.path(forResource: "vzg0", ofType: "nardiw").map { nardi_load_model(h, $0) == NARDI_OK } ?? false
        let disp = Bundle.main.path(forResource: "res2", ofType: "nardiw").map { nardi_load_model(e, $0) == NARDI_OK } ?? false
        // Search handle ranks with the same net as selection (vzg0).
        _ = Bundle.main.path(forResource: "vzg0", ofType: "nardiw").map { nardi_load_model(s, $0) == NARDI_OK } ?? false
        modelLoaded = sel && disp
    }
    deinit { nardi_destroy(handle); nardi_destroy(evalHandle); nardi_destroy(searchHandle) }

    // MARK: - Geometry / counts helpers

    static func sign(_ black: Bool) -> Int8 { black ? -1 : 1 }

    /// A colour's bear-off (home) quadrant: white row 1 cols 6-11, black row 0 cols 6-11.
    static func inHome(row: Int, col: Int, black: Bool) -> Bool {
        col >= 6 && row == (black ? 0 : 1)
    }

    func placed(_ black: Bool) -> Int {
        editorBoard.reduce(0) { $0 + (black ? max(0, -Int($1)) : max(0, Int($1))) }
    }
    func bank(_ black: Bool) -> Int { Self.pieces - placed(black) }
    func borneOff(_ black: Bool) -> Int { isEndgame(black) ? Self.pieces - placed(black) : 0 }
    func isEndgame(_ black: Bool) -> Bool { black ? endgameBlack : endgameWhite }

    // MARK: - Editor

    func clearBoard() {
        editorBoard = Array(repeating: 0, count: Self.cells)
        board = editorBoard
        status = ""
    }

    /// Standard opening: 15 on each head. Turns endgame flags off.
    func standardStart() {
        var b = [Int8](repeating: 0, count: Self.cells)
        b[0] = 15            // white head (0,0)
        b[Self.cols] = -15   // black head (1,0)
        endgameWhite = false; endgameBlack = false
        editorBoard = b; board = b
        status = ""
    }

    /// Dev/testing: drop in a board + side + dice and start analysis (used by the
    /// --analyze-mid launch hook to reach a multi-move position).
    func devSetup(board b: [Int8], side: Bool, d1: Int, d2: Int) {
        endgameWhite = false; endgameBlack = false
        editorBoard = b; board = b
        sideToMove = side
        die1 = d1; die2 = d2
        startAnalysis()
    }

    func tapCellEditor(row: Int, col: Int) {
        let black = activeColor
        guard bank(black) > 0 else { status = "\(black ? "Black" : "White") bank is empty."; return }
        let idx = row * Self.cols + col
        let cur = editorBoard[idx]
        if cur != 0 && (cur > 0) == black {            // opposite colour occupies this point
            status = "A point holds one colour only."
            return
        }
        if isEndgame(black) && !Self.inHome(row: row, col: col, black: black) {
            status = "Endgame: \(black ? "black" : "white") may only go in its home board."
            return
        }
        editorBoard[idx] = cur + Self.sign(black)
        board = editorBoard
        status = ""
    }

    func longPressCellEditor(row: Int, col: Int) {
        let idx = row * Self.cols + col
        let cur = editorBoard[idx]
        guard cur != 0 else { return }
        editorBoard[idx] = cur - (cur > 0 ? 1 : -1)   // remove one, returns to its bank
        board = editorBoard
        status = ""
    }

    /// When endgame is toggled ON, any of that colour outside its home board is
    /// illegal; clear those so the position stays valid.
    func setEndgame(_ black: Bool, _ on: Bool) {
        if black { endgameBlack = on } else { endgameWhite = on }
        guard on else { return }
        var b = editorBoard
        for row in 0..<2 {
            for col in 0..<Self.cols where !Self.inHome(row: row, col: col, black: black) {
                let idx = row * Self.cols + col
                if b[idx] != 0 && (b[idx] > 0) == !black { b[idx] = 0 }   // this colour, outside home
            }
        }
        editorBoard = b; board = b
    }

    /// nil when the position is valid to analyze; otherwise the reason.
    var validationError: String? {
        for black in [false, true] {
            let p = placed(black), name = black ? "Black" : "White"
            if p == 0 { return "\(name) needs at least one checker." }
            if isEndgame(black) {
                if p > Self.pieces { return "\(name): too many checkers." }
            } else if p != Self.pieces {
                return "\(name): place all \(Self.pieces) (\(p) placed) or enable endgame."
            }
        }
        return nil
    }
    var canAnalyze: Bool { validationError == nil && modelLoaded }

    func startAnalysis() {
        guard canAnalyze else {
            status = validationError ?? (modelLoaded ? "" : "Model blob missing.")
            return
        }
        originalSide = sideToMove
        currentSide = sideToMove
        flipped = currentSide                 // on-move player's head sits top-right
        editorBoard.withUnsafeBufferPointer { _ = nardi_set_position(handle, $0.baseAddress, sideToMove ? 1 : 0) }
        board = engineBoard()
        history = []
        turnStart = nil
        gameDiceScript = []; gameLineBoards = []; hasGameDice = false; useGameDice = false; relTurn = 0   // free
        // A standard-opening start can be logged + reviewed as a full game.
        var std = [Int8](repeating: 0, count: Self.cells); std[0] = 15; std[Self.cols] = -15
        startedFromStandard = (editorBoard == std && !endgameWhite && !endgameBlack)
        loggedTurns = []
        loggedSaved = false
        resetTurnState()
        isTerminal = false
        canUndo = false
        phase = .analyzing
        refreshGameLineFlag()
        positionEval = staticEval()
        autoApplyDice()   // apply the default dice so play + eval show immediately
    }

    func backToEditor() {
        phase = .editing
        board = editorBoard
        resetTurnState()            // also abandons any in-flight search (bumps searchGen)
        onGameLine = false          // editor isn't on a game line; avoids reading the engine here
        status = ""
    }

    /// Open the analyzer directly on a position (from game review), skipping the
    /// editor. `side` is the position's side to move (so moves play correctly);
    /// `anchor` is the frame the eval is pinned to — the reviewed player — so the
    /// eval stays from their perspective even when it's the opponent's turn here.
    /// When `gameDice` is supplied (the reviewed game's rolls from this point on),
    /// the dice default to following the game and can be unlocked to explore freely.
    func openForReview(board b: [Int8], side: Bool, anchor: Bool,
                       gameDice: [(Int, Int)] = [], gameLine: [[Int8]] = []) {
        editorBoard = b
        board = b
        originalSide = anchor          // eval frame = reviewed player
        currentSide = side             // who actually moves here
        flipped = side
        b.withUnsafeBufferPointer { _ = nardi_set_position(handle, $0.baseAddress, side ? 1 : 0) }
        history = []
        turnStart = nil
        gameDiceScript = gameDice
        gameLineBoards = gameLine
        startedFromStandard = false   // opened mid-game; not a loggable full trajectory
        loggedTurns = []
        loggedSaved = false
        hasGameDice = !gameDice.isEmpty
        useGameDice = hasGameDice          // default to the game's dice when available
        relTurn = 0
        resetTurnState()
        isTerminal = false
        canUndo = false
        phase = .analyzing
        refreshGameLineFlag()
        positionEval = staticEval()
        autoApplyDice()
        status = hasGameDice ? "Following the game's dice — toggle off to explore freely."
                             : "Explore — change the dice or play a move."
    }

    // MARK: - Analysis: dice + evaluation

    private func engineBoard() -> [Int8] {
        var buf = [Int8](repeating: 0, count: Self.cells)
        _ = nardi_board(handle, &buf)
        return buf
    }

    /// Sign that pins a side-to-move value to the original-side frame.
    private var frameSign: Float { (currentSide == originalSide) ? 1 : -1 }

    /// At a terminal position, the EXACT game result in the original-side frame
    /// (+/-1 normal, +/-2 mars) -- not the model's estimate. The winner is the
    /// side with no checkers left on the board, and the margin is mars iff the
    /// loser bore off none. nil when the position is not terminal.
    private func terminalResultEval() -> Float? {
        guard nardi_is_terminal(handle) == 1 else { return nil }
        let b = engineBoard()
        let whiteOn = b.reduce(0) { $0 + max(0, Int($1)) }
        let winnerIsBlack = (whiteOn == 0) ? false : true   // winner = side borne off (0 left)
        let margin = Float(nardi_winner_result(handle))      // 1 normal, 2 mars
        return margin * (winnerIsBlack == originalSide ? 1 : -1)
    }

    /// Displayed value of the current board in the original frame: the exact result
    /// if terminal, else the well-calibrated res2 net's side-to-move estimate
    /// (mirrored onto the eval handle so the live state is untouched), pinned with
    /// the same sign convention as before.
    private func staticEval() -> Float? {
        if let result = terminalResultEval() { return result }
        guard modelLoaded else { return nil }
        let b = engineBoard()
        b.withUnsafeBufferPointer { _ = nardi_set_position(evalHandle, $0.baseAddress, currentSide ? 1 : 0) }
        var v: Float = 0
        return nardi_evaluate_position(evalHandle, &v) == NARDI_OK ? v * frameSign : nil
    }

    private func resetTurnState() {
        cancelSuggestions()           // any pending/in-flight ranking is now stale
        movedThisTurn = false
        tryFirst = false              // default to the larger die each new turn
        diceApplied = false
        noLegalMoves = false
        selected = nil
        canConfirm = false
        dieUsable = (false, false)
        topMoves = []
    }

    /// Mark any pending or in-flight suggestion ranking as stale: cancel the queued
    /// work and bump the generation so a result that already started is discarded
    /// when it tries to apply. Called whenever the position changes.
    private func cancelSuggestions() {
        suggestWork?.cancel()
        suggestWork = nil
        suggestGen += 1
        isThinking = false
    }

    /// Play an engine-recommended move (whole turn) and advance to the next side,
    /// exactly as if the user had played it by hand and confirmed. (Suggestions only
    /// exist once the background ranking has landed, so the live handle's options for
    /// these dice are already enumerated.)
    func applyTopMove(_ i: Int) {
        guard phase == .analyzing, diceApplied, !movedThisTurn, !isAnimating,
              !canConfirm, i < topMoves.count else { return }
        _ = applyEndBoard(topMoves[i].board)
    }

    /// Play, as a whole confirmed turn, the legal move that reaches `target` from the
    /// current position — matched against the live handle's enumerated options (which
    /// nardi_set_dice populated). Records the undo snapshot, animates, advances. No
    /// net evaluation, so it's instant. Returns false if no option matches.
    @discardableResult
    private func applyEndBoard(_ target: [Int8], following: Bool = false) -> Bool {
        let n = Int(nardi_legal_move_count(handle))
        var idx = -1
        var b = [Int8](repeating: 0, count: Self.cells)
        for j in 0..<max(0, n) {
            if nardi_option_board(handle, Int32(j), &b) == NARDI_OK, b == target { idx = j; break }
        }
        guard idx >= 0 else { return false }
        if let ts = turnStart { history.append(ts) }
        nardi_apply_human_move(handle, Int32(idx))      // applies + confirms (switches side), records sub-moves
        logCompletedTurn(moved: true)                   // record for a savable/reviewable log
        canUndo = true
        Task { @MainActor in
            await animateMoves()                      // moverSign uses the pre-switch side
            currentSide = (nardi_current_player(handle) == 1)
            flipped = currentSide
            board = engineBoard()
            turnStart = nil
            resetTurnState()
            relTurn += 1
            refreshGameLineFlag()
            isTerminal = (nardi_is_terminal(handle) == 1)
            positionEval = staticEval()
            status = isTerminal ? "Position is terminal. Undo to continue."
                   : following ? "Following the game — \(currentSide ? "Black" : "White") to move."
                               : "\(currentSide ? "Black" : "White") to move."
            autoApplyDice()
        }
        return true
    }

    // MARK: - Stepping through the reviewed game (mainline)

    /// True when the current confirmed position is exactly the game's position at
    /// this step — i.e. we haven't branched into a line that didn't happen. Stored
    /// and refreshed only at turn boundaries (via refreshGameLineFlag), never
    /// recomputed mid-animation, so the on/off-line indicator doesn't flicker while
    /// a move is still sliding.
    @Published private(set) var onGameLine = false

    private func refreshGameLineFlag() {
        onGameLine = hasGameDice && relTurn < gameLineBoards.count
            && engineBoard() == gameLineBoards[relTurn]
    }

    /// Whether "Follow game" can step forward: on the game line, a next game move
    /// exists, and we're at a clean turn start (greyed out otherwise).
    var canFollowGame: Bool {
        phase == .analyzing && !isAnimating && !movedThisTurn && !isTerminal
            && relTurn + 1 < gameLineBoards.count && onGameLine
    }

    /// Step forward along the reviewed game: replay the move the game actually made
    /// this turn (its dice + recorded resulting position), animated like any move.
    func followGameMove() {
        guard canFollowGame else { return }
        let (d1, d2) = gameDiceScript[relTurn]
        let target = gameLineBoards[relTurn + 1]
        let cur = engineBoard()
        if target == cur {   // the game's turn here was a forced pass
            if let ts = turnStart { history.append(ts) }
            currentSide.toggle()
            flipped = currentSide
            cur.withUnsafeBufferPointer { _ = nardi_set_position(handle, $0.baseAddress, currentSide ? 1 : 0) }
            turnStart = nil
            resetTurnState()
            relTurn += 1
            refreshGameLineFlag()
            canUndo = true
            positionEval = staticEval()
            status = "Following the game (pass) — \(currentSide ? "Black" : "White") to move."
            autoApplyDice()
            return
        }
        // Re-roll the game's dice on the live handle (cheap) so its options reflect
        // that roll, then replay the move the game made by matching its end board.
        _ = nardi_set_dice(handle, Int32(d1), Int32(d2))
        guard applyEndBoard(target, following: true) else {
            status = "Couldn't replay the game's move."
            return
        }
    }

    // MARK: - Logging a played game (from the standard start) for review

    /// Append the just-completed turn to the trajectory log (only when this session
    /// started from the standard opening, so it forms a reviewable full game).
    private func logCompletedTurn(moved: Bool) {
        guard startedFromStandard, let ts = turnStart else { return }
        loggedTurns.append(ReviewTurn(preBoard: ts.board, preSide: ts.side,
                                      dice: (die1, die2), postBoard: engineBoard(), moved: moved))
    }

    /// Whether the logged game can be saved: a completed (terminal) game from the
    /// standard start that hasn't already been saved this session.
    var canSaveLog: Bool {
        startedFromStandard && isTerminal && !loggedTurns.isEmpty && !loggedSaved
    }

    /// Save the logged game to the match history so it can be reviewed like a played
    /// game. The result is read off the terminal position.
    func saveLoggedGame() {
        guard canSaveLog, let save = onLogSaved else { return }
        let b = engineBoard()
        let whiteOn = b.reduce(0) { $0 + max(0, Int($1)) }
        let winnerWhite = (whiteOn == 0)                 // winner has borne off all (0 left)
        let mars = (nardi_winner_result(handle) == 2)
        loggedSaved = true
        save(SavedMatch(id: UUID(), date: Date(),
                        modeRaw: "Logged game", opponentRaw: nil,
                        reviewSide: false,               // review White's play
                        winnerWhite: winnerWhite, mars: mars,
                        turns: loggedTurns.map { SavedTurn($0) }))
        status = "Saved to History — review it from the History tab."
    }

    /// Point number 1..24 along the mover's path for an engine (row, col).
    nonisolated private static func pointNumber(_ row: Int, _ col: Int, black: Bool) -> Int {
        let onHeadRow = (row == (black ? 1 : 0))
        return (onHeadRow ? col : cols + col) + 1
    }

    /// Describe an engine suggestion as the actual per-checker hops, e.g.
    /// "1 → 7, 12 → 17" (a checker chaining both dice shows both hops). The engine
    /// only records sub-moves when a move is applied, so replay the suggestion on
    /// the eval handle -- found by matching its end board, so the model/ranking is
    /// irrelevant -- and read the recorded {from→to} of each checker. Runs on the
    /// search queue, so it's nonisolated and takes the eval handle explicitly.
    nonisolated private static func subMoveLabel(scratchHandle sh: OpaquePointer, preBoard: [Int8],
                                                 side: Bool, end: [Int8], _ d1: Int, _ d2: Int) -> String {
        preBoard.withUnsafeBufferPointer { _ = nardi_set_position(sh, $0.baseAddress, side ? 1 : 0) }
        let m = Int(nardi_analyze_dice(sh, Int32(d1), Int32(d2)))
        var idx = -1
        for j in 0..<max(0, m) {
            var b = [Int8](repeating: 0, count: cells); var v: Float = 0
            if nardi_analyzed_move(sh, Int32(j), &b, &v) == NARDI_OK, b == end { idx = j; break }
        }
        guard idx >= 0, nardi_apply_analyzed_move(sh, Int32(idx)) == NARDI_OK else { return "—" }
        var parts: [String] = []
        for k in 0..<max(0, Int(nardi_move_count(sh))) {
            var a = [Int32](repeating: -1, count: 4)
            if nardi_get_move(sh, Int32(k), &a) == NARDI_OK {
                let from = pointNumber(Int(a[0]), Int(a[1]), black: side)
                let to = a[2] < 0 ? "off" : "\(pointNumber(Int(a[2]), Int(a[3]), black: side))"
                parts.append("\(from) → \(to)")
            }
        }
        return parts.isEmpty ? "—" : parts.joined(separator: ", ")
    }

    /// Apply the chosen dice. The cheap part runs synchronously on the LIVE handle
    /// (roll + enumerate + a single display eval — no net-eval lookahead), so the
    /// position is instantly playable: you can move, go Back, or Follow right away.
    /// The slow vzg0 ranking that fills the top-3 suggestions is debounced and runs
    /// on the background search handle, so it never holds up interaction. Turn start
    /// only.
    func setDice() {
        guard phase == .analyzing, !isAnimating, !isTerminal, !movedThisTurn else { return }
        guard let (d1, d2) = effectiveDice() else {
            cancelSuggestions()
            diceApplied = false; noLegalMoves = false; topMoves = []
            positionEval = staticEval()
            status = "Past the game's last move — turn off ‘Dice from game’ to keep exploring."
            return
        }
        die1 = d1; die2 = d2          // reflect the applied dice in the UI / steppers
        if turnStart == nil { turnStart = Snapshot(board: engineBoard(), side: currentSide) }

        // Cheap, on the live handle: roll the dice + enumerate legal options so play
        // is enabled immediately (no model evaluation involved).
        let n = Int(nardi_set_dice(handle, Int32(d1), Int32(d2)))
        diceApplied = true
        noLegalMoves = (n == 0)
        selected = nil
        canConfirm = false
        dieUsable = (nardi_can_use_die(handle, 0) == 1, nardi_can_use_die(handle, 1) == 1)
        positionEval = staticEval()   // res2 display eval — single forward pass, cheap

        if n > 0, let pre = turnStart?.board {
            status = "Make your move — engine suggestions loading…"
            scheduleSuggestions(preBoard: pre, side: currentSide, d1: d1, d2: d2)
        } else {
            cancelSuggestions(); topMoves = []
            status = "No legal move for \(die1)-\(die2). Tap Pass."
        }
    }

    /// Debounced background ranking: after a 0.5s quiet period, rank the legal moves
    /// on `searchHandle` (off the main thread) and hand the labelled top-3 back. Each
    /// call supersedes the previous; a position change drops a result mid-flight.
    private func scheduleSuggestions(preBoard pre: [Int8], side: Bool, d1: Int, d2: Int) {
        suggestWork?.cancel()
        suggestGen += 1
        let gen = suggestGen
        isThinking = true
        nonisolated(unsafe) let sh = searchHandle
        let work = DispatchWorkItem {
            let tops = AnalyzeGame.rankSuggestions(searchHandle: sh, preBoard: pre, side: side, d1: d1, d2: d2)
            Task { @MainActor [weak self] in self?.applySuggestions(tops, gen: gen) }
        }
        suggestWork = work
        searchQueue.asyncAfter(deadline: .now() + .nanoseconds(Self.suggestDebounceNanos), execute: work)
    }

    /// Off-main (search queue): rank the legal moves on the search handle (vzg0,
    /// best first) and label the best three as per-checker hops. Pure C work over
    /// captured values, isolated to `searchHandle`, so it never races the live state.
    nonisolated private static func rankSuggestions(
        searchHandle sh: OpaquePointer, preBoard pre: [Int8], side: Bool, d1: Int, d2: Int
    ) -> [TopMove] {
        pre.withUnsafeBufferPointer { _ = nardi_set_position(sh, $0.baseAddress, side ? 1 : 0) }
        let n = Int(nardi_analyze_dice(sh, Int32(d1), Int32(d2)))
        // Capture the top-3 ranked end boards BEFORE labelling: subMoveLabel re-analyzes
        // and applies on the same handle, which clears the ranked list, so reading more
        // boards afterwards would fail (that capped suggestions at one).
        var boards: [[Int8]] = []
        for i in 0..<min(max(0, n), 3) {
            var b = [Int8](repeating: 0, count: cells); var v: Float = 0
            if nardi_analyzed_move(sh, Int32(i), &b, &v) == NARDI_OK { boards.append(b) }
        }
        return boards.map { b in
            TopMove(board: b, label: subMoveLabel(scratchHandle: sh, preBoard: pre, side: side, end: b, d1, d2))
        }
    }

    /// Main actor: install ranked suggestions unless they've been superseded by a
    /// newer position (gen mismatch) or we've since left analysis.
    private func applySuggestions(_ tops: [TopMove], gen: Int) {
        guard gen == suggestGen, phase == .analyzing else { return }
        isThinking = false
        topMoves = tops
        if !movedThisTurn && diceApplied && !noLegalMoves && !tops.isEmpty {
            status = "Top engine moves for \(currentSide ? "Black" : "White") shown — make your move."
        }
    }

    /// Recompute the stored `movedThisTurn` from the engine (a checker has moved iff
    /// the board differs from the turn's starting board). Call only on the main
    /// thread when no background search is in flight. We track this as stored state
    /// — rather than comparing the board on every read — so SwiftUI never calls into
    /// the engine during render while the search queue is using the handle.
    private func refreshMovedThisTurn() {
        movedThisTurn = turnStart.map { engineBoard() != $0.board } ?? false
    }

    /// Re-apply the dice after the user changes a stepper (no-op while locked to
    /// game dice, or once a checker has moved -- Undo first to change again). The
    /// cheap roll/enumerate in setDice() runs immediately so the dice feel instant;
    /// only the suggestion ranking is debounced (inside scheduleSuggestions).
    func diceChanged() {
        guard phase == .analyzing, !isAnimating, !movedThisTurn, !isTerminal, !useGameDice else { return }
        setDice()
    }

    /// Toggle locking the dice to the reviewed game's roll at this relative turn.
    func setUseGameDice(_ on: Bool) {
        guard phase == .analyzing, !isAnimating, !movedThisTurn, hasGameDice else { return }
        useGameDice = on
        setDice()   // re-apply with game or free dice
    }

    /// The dice to apply: the game's dice at the current relative turn when locked,
    /// else the user-set dice. nil = locked to game but past the game's last turn.
    private func effectiveDice() -> (Int, Int)? {
        if useGameDice {
            return relTurn < gameDiceScript.count ? gameDiceScript[relTurn] : nil
        }
        return (die1, die2)
    }

    /// Entering a fresh turn (start / confirm / pass / step-back / follow): roll the
    /// new turn's dice. setDice() applies them immediately on the live handle (so the
    /// position is instantly playable) and debounces only the suggestion ranking.
    private func autoApplyDice() {
        guard modelLoaded && !isTerminal else { return }
        setDice()
    }

    /// Eval of the current (possibly mid/after-move) board — the res2 display eval.
    private func currentEval() -> Float? { staticEval() }

    // MARK: - Analysis: hands-on moves

    // MARK: - Click-to-move (same model as the play screen)

    func startMaskFor(_ idx: Int) -> Int { Int(nardi_starts_mask(handle, Int32(idx))) }
    /// Combined startable squares (either die) — for the green start dots.
    var startMaskCombined: Int { startMaskFor(0) | startMaskFor(1) }
    /// The engine die index (0/1) tried first when a checker is tapped: slot 0 (D1)
    /// by default, slot 1 (D2) when reversed. Slot-based (not value-based) so the
    /// D1/D2 steppers stay coupled to the visual order — tapping the dice swaps both
    /// the steppers and the dice shown below together, with no value jumping.
    var firstDieIndex: Int { tryFirst ? 1 : 0 }
    /// Engine die index shown in display slot `slot` (0 = first-try, 1 = second-try).
    func dieIndex(forSlot slot: Int) -> Int { slot == 0 ? firstDieIndex : (firstDieIndex == 0 ? 1 : 0) }

    /// The two dice in UI order — first is tried first. Doubles share one start state
    /// (grey both or neither), matching the play screen.
    var orderedDice: [(value: Int, hasStart: Bool, isFirst: Bool)] {
        let f = firstDieIndex, s = (f == 0 ? 1 : 0)
        let doubles = (die1 == die2)
        let anyStart = startMaskCombined != 0
        func entry(_ i: Int, _ isFirst: Bool) -> (value: Int, hasStart: Bool, isFirst: Bool) {
            (i == 0 ? die1 : die2, doubles ? anyStart : startMaskFor(i) != 0, isFirst)
        }
        return [entry(f, true), entry(s, false)]
    }

    /// Click-to-move: tapping a checker plays it with the preferred die, falling back
    /// to the other. Mirrors the play screen exactly.
    func tap(row: Int, col: Int) {
        guard phase == .analyzing, diceApplied, !noLegalMoves, !isAnimating, !canConfirm else { return }
        let bit = 1 << (row * Self.cols + col)
        let firstIdx = firstDieIndex
        let secondIdx = firstIdx == 0 ? 1 : 0
        let idx = (startMaskFor(firstIdx) & bit) != 0 ? firstIdx
                : (startMaskFor(secondIdx) & bit) != 0 ? secondIdx
                : -1
        guard idx >= 0 else { return }
        _ = nardi_human_select(handle, Int32(row), Int32(col))
        playSelected(byDie: idx)
    }

    /// Flip which die a tapped checker is played with first (tap the dice to reverse).
    func toggleTryFirst() {
        guard phase == .analyzing, diceApplied, !isAnimating, !canConfirm else { return }
        tryFirst.toggle()
    }

    private func playSelected(byDie idx: Int) {
        if nardi_human_move_die(handle, Int32(idx)) != NARDI_OK {
            status = "Illegal move."
            return
        }
        movedThisTurn = true        // a checker has now moved this turn
        selected = nil
        canUndo = true
        Task { @MainActor in
            await animateMoves()
            dieUsable = (nardi_can_use_die(handle, 0) == 1, nardi_can_use_die(handle, 1) == 1)
            selected = nil          // click-to-move: no lingering selection (dots show next starts)
            if nardi_turn_is_complete(handle) == 1 {
                canConfirm = true
                status = "Move complete — Confirm to switch sides (or Back)."
            }
            positionEval = currentEval()
        }
    }

    func undo() {
        guard phase == .analyzing, !isAnimating else { return }
        if movedThisTurn {                                    // peel back a sub-move
            _ = nardi_human_undo(handle)
            canConfirm = false
            Task { @MainActor in
                await animateMoves()
                dieUsable = (nardi_can_use_die(handle, 0) == 1, nardi_can_use_die(handle, 1) == 1)
                updateSelection()
                positionEval = currentEval()
                refreshMovedThisTurn()                        // may have peeled back to the turn start
                canUndo = movedThisTurn || !history.isEmpty
            }
        } else if let snap = history.popLast() {              // step back across a confirmed turn
            currentSide = snap.side
            flipped = currentSide
            snap.board.withUnsafeBufferPointer { _ = nardi_set_position(handle, $0.baseAddress, snap.side ? 1 : 0) }
            board = snap.board
            turnStart = nil
            resetTurnState()
            relTurn = max(0, relTurn - 1)
            if !loggedTurns.isEmpty { loggedTurns.removeLast() }   // keep the log in sync
            refreshGameLineFlag()
            isTerminal = false
            positionEval = staticEval()
            canUndo = !history.isEmpty
            status = "Stepped back — \(currentSide ? "Black" : "White") to move."
            autoApplyDice()
        }
    }

    func confirm() {
        guard phase == .analyzing, canConfirm, !isAnimating else { return }
        if let ts = turnStart { history.append(ts) }
        logCompletedTurn(moved: true)   // record for a savable/reviewable log
        nardi_confirm_turn(handle)
        currentSide = (nardi_current_player(handle) == 1)
        flipped = currentSide
        board = engineBoard()
        turnStart = nil
        resetTurnState()
        isTerminal = (nardi_is_terminal(handle) == 1)
        canUndo = !history.isEmpty
        relTurn += 1                       // advance the game-dice pointer
        refreshGameLineFlag()
        positionEval = staticEval()
        status = isTerminal ? "Position is terminal. Undo to continue."
                            : "\(currentSide ? "Black" : "White") to move."
        autoApplyDice()   // apply dice for the new side (game dice or free)
    }

    /// No legal move for the chosen dice: switch sides without moving (undoable).
    func pass() {
        guard phase == .analyzing, diceApplied, noLegalMoves, !isAnimating else { return }
        if let ts = turnStart { history.append(ts) }
        logCompletedTurn(moved: false)   // record the pass in the log
        currentSide.toggle()
        flipped = currentSide
        board.withUnsafeBufferPointer { _ = nardi_set_position(handle, $0.baseAddress, currentSide ? 1 : 0) }
        turnStart = nil
        resetTurnState()
        relTurn += 1
        refreshGameLineFlag()
        positionEval = staticEval()
        canUndo = !history.isEmpty
        status = "Passed — \(currentSide ? "Black" : "White") to move."
        autoApplyDice()
    }

    private func updateSelection() {
        if nardi_start_selected(handle) == 1 {
            var rc = [Int32](repeating: -1, count: 2)
            if nardi_selected_start(handle, &rc) == NARDI_OK { selected = (Int(rc[0]), Int(rc[1])) }
        } else {
            selected = nil
        }
    }

    // MARK: - Move animation (sub-move log, like the play screen)

    private func recentMoves() -> [(from: (Int, Int)?, to: (Int, Int)?)] {
        let n = Int(nardi_move_count(handle))
        var out: [(from: (Int, Int)?, to: (Int, Int)?)] = []
        for i in 0..<max(0, n) {
            var a = [Int32](repeating: -1, count: 4)
            if nardi_get_move(handle, Int32(i), &a) == NARDI_OK {
                out.append((a[0] >= 0 ? (Int(a[0]), Int(a[1])) : nil,
                            a[2] >= 0 ? (Int(a[2]), Int(a[3])) : nil))
            }
        }
        return out
    }

    private func animateMoves() async {
        let subs = recentMoves()
        guard !subs.isEmpty else { board = engineBoard(); return }
        let moverSign = Self.sign(currentSide)   // the side making the move (turn not switched yet)
        isAnimating = true
        var disp = board
        let dur = UInt64(BoardCanvas.flightDuration * 1_000_000_000)
        for m in subs {
            if let (r, c) = m.from { disp[r * Self.cols + c] -= moverSign }
            board = disp
            flights = [Flight(from: m.from, to: m.to, white: moverSign > 0)]   // FlightView self-animates
            try? await Task.sleep(nanoseconds: dur)
            if let (r, c) = m.to { disp[r * Self.cols + c] += moverSign }
            flights = []
            board = disp
            try? await Task.sleep(nanoseconds: 60_000_000)
        }
        isAnimating = false
        board = engineBoard()
    }
}
