import Foundation
import SwiftUI

/// A position we can return to via arbitrary-history Undo. Restored with
/// set_position (which bypasses the controller's per-turn undo, so undo works
/// across confirmed moves too).
private struct Snapshot { let board: [Int8]; let side: Bool }

/// An engine-recommended move shown in the analyzer: the end-board it reaches and a
/// per-checker descriptor (e.g. "1 → 7, 12 → 17"). No eval is shown — these are
/// ranked by vzg0 lookahead, whose absolute numbers we don't surface.
struct TopMove: Identifiable {
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
    static let cells = Int(NARDI_BOARD_CELLS)
    static let cols = 12
    static let pieces = 15

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
    @Published private(set) var noLegalMoves = false
    // When opened from game review, the dice can be locked to the game's actual
    // roll at each relative turn (useGameDice) or set freely.
    @Published private(set) var useGameDice = false
    @Published private(set) var hasGameDice = false
    private var gameDiceScript: [(Int, Int)] = []   // dice from the jump-in point onward
    private var gameLineBoards: [[Int8]] = []        // the game's positions from the jump-in (mainline)
    private var relTurn = 0                          // turns played since jump-in
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
    private let handle: OpaquePointer
    // Move selection (top-3 ranking, the line the engine recommends) uses the strong
    // vzg0 net on `handle`. The DISPLAYED eval uses the well-calibrated res2 net on a
    // separate handle: vzg0 isn't antisymmetric so its absolute evals are biased and
    // swingy, while res2 reads ~0 at the symmetric start. The eval handle also serves
    // as scratch for decoding a suggestion's sub-moves without touching live state.
    private let evalHandle: OpaquePointer
    private var history: [Snapshot] = []
    private var turnStart: Snapshot? = nil          // position at the start of the current turn

    init() {
        guard let h = nardi_create(), let e = nardi_create() else { fatalError("nardi_create failed") }
        handle = h; evalHandle = e
        let sel  = Bundle.main.path(forResource: "vzg0", ofType: "nardiw").map { nardi_load_model(h, $0) == NARDI_OK } ?? false
        let disp = Bundle.main.path(forResource: "res2", ofType: "nardiw").map { nardi_load_model(e, $0) == NARDI_OK } ?? false
        modelLoaded = sel && disp
    }
    deinit { nardi_destroy(handle); nardi_destroy(evalHandle) }

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
        resetTurnState()
        isTerminal = false
        canUndo = false
        phase = .analyzing
        positionEval = staticEval()
        autoApplyDice()   // apply the default dice so play + eval show immediately
    }

    func backToEditor() {
        phase = .editing
        board = editorBoard
        resetTurnState()
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
        hasGameDice = !gameDice.isEmpty
        useGameDice = hasGameDice          // default to the game's dice when available
        relTurn = 0
        resetTurnState()
        isTerminal = false
        canUndo = false
        phase = .analyzing
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
        diceApplied = false
        noLegalMoves = false
        selected = nil
        canConfirm = false
        dieUsable = (false, false)
        topMoves = []
    }

    /// Play an engine-recommended move (whole turn) and advance to the next side,
    /// exactly as if the user had played it by hand and confirmed.
    func applyTopMove(_ i: Int) {
        guard phase == .analyzing, diceApplied, !movedThisTurn, !isAnimating,
              !canConfirm, i < topMoves.count else { return }
        applyAnalyzed(i)
    }

    /// Apply ranked analyzed move `idx` (already enumerated on the handle) as a whole
    /// turn: record the pre-move snapshot for undo, play + confirm, animate, advance.
    private func applyAnalyzed(_ idx: Int, following: Bool = false) {
        if let ts = turnStart { history.append(ts) }
        nardi_apply_analyzed_move(handle, Int32(idx))   // applies + confirms (switches side)
        canUndo = true
        Task { @MainActor in
            await animateMoves()                      // moverSign uses the pre-switch side
            currentSide = (nardi_current_player(handle) == 1)
            flipped = currentSide
            board = engineBoard()
            turnStart = nil
            resetTurnState()
            relTurn += 1
            isTerminal = (nardi_is_terminal(handle) == 1)
            positionEval = staticEval()
            status = isTerminal ? "Position is terminal. Undo to continue."
                   : following ? "Following the game — \(currentSide ? "Black" : "White") to move."
                               : "\(currentSide ? "Black" : "White") to move."
            autoApplyDice()
        }
    }

    // MARK: - Stepping through the reviewed game (mainline)

    /// True when the current confirmed position is exactly the game's position at
    /// this step — i.e. we haven't branched into a line that didn't happen.
    var onGameLine: Bool {
        hasGameDice && relTurn < gameLineBoards.count && engineBoard() == gameLineBoards[relTurn]
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
            canUndo = true
            positionEval = staticEval()
            status = "Following the game (pass) — \(currentSide ? "Black" : "White") to move."
            autoApplyDice()
            return
        }
        let n = Int(nardi_analyze_dice(handle, Int32(d1), Int32(d2)))   // game's roll
        var idx = -1
        for j in 0..<max(0, n) {
            var b = [Int8](repeating: 0, count: Self.cells); var v: Float = 0
            if nardi_analyzed_move(handle, Int32(j), &b, &v) == NARDI_OK, b == target { idx = j; break }
        }
        guard idx >= 0 else { status = "Couldn't replay the game's move."; return }
        applyAnalyzed(idx, following: true)
    }

    /// Point number 1..24 along the mover's path for an engine (row, col).
    private func pointNumber(_ row: Int, _ col: Int, black: Bool) -> Int {
        let onHeadRow = (row == (black ? 1 : 0))
        return (onHeadRow ? col : Self.cols + col) + 1
    }

    /// Describe an engine suggestion as the actual per-checker hops, e.g.
    /// "1 → 7, 12 → 17" (a checker chaining both dice shows both hops). The engine
    /// only records sub-moves when a move is applied, so replay the suggestion on
    /// the eval handle -- found by matching its end board, so the model/ranking is
    /// irrelevant -- and read the recorded {from→to} of each checker.
    private func subMoveLabel(preBoard: [Int8], side: Bool, end: [Int8], _ d1: Int, _ d2: Int) -> String {
        preBoard.withUnsafeBufferPointer { _ = nardi_set_position(evalHandle, $0.baseAddress, side ? 1 : 0) }
        let m = Int(nardi_analyze_dice(evalHandle, Int32(d1), Int32(d2)))
        var idx = -1
        for j in 0..<max(0, m) {
            var b = [Int8](repeating: 0, count: Self.cells); var v: Float = 0
            if nardi_analyzed_move(evalHandle, Int32(j), &b, &v) == NARDI_OK, b == end { idx = j; break }
        }
        guard idx >= 0, nardi_apply_analyzed_move(evalHandle, Int32(idx)) == NARDI_OK else { return "—" }
        var parts: [String] = []
        for k in 0..<max(0, Int(nardi_move_count(evalHandle))) {
            var a = [Int32](repeating: -1, count: 4)
            if nardi_get_move(evalHandle, Int32(k), &a) == NARDI_OK {
                let from = pointNumber(Int(a[0]), Int(a[1]), black: side)
                let to = a[2] < 0 ? "off" : "\(pointNumber(Int(a[2]), Int(a[3]), black: side))"
                parts.append("\(from) → \(to)")
            }
        }
        return parts.isEmpty ? "—" : parts.joined(separator: ", ")
    }

    /// Apply the chosen dice to the current position. vzg0 ranks the legal moves
    /// (best first) to populate the top-3 suggestions; the eval bar shows the res2
    /// value of the current position. Only allowed at the start of a turn.
    func setDice() {
        guard phase == .analyzing, !isAnimating, !isTerminal, !movedThisTurn else { return }
        guard let (d1, d2) = effectiveDice() else {
            diceApplied = false; noLegalMoves = false
            positionEval = staticEval()
            status = "Past the game's last move — turn off ‘Dice from game’ to keep exploring."
            return
        }
        die1 = d1; die2 = d2          // reflect the applied dice in the UI / steppers
        turnStart = Snapshot(board: engineBoard(), side: currentSide)
        let n = Int(nardi_analyze_dice(handle, Int32(d1), Int32(d2)))
        guard n >= 0 else { status = "Analyze failed: " + String(cString: nardi_last_error(handle)); return }

        let preBoard = engineBoard()
        var tops: [TopMove] = []
        for i in 0..<min(n, 3) {   // vzg0's best-first ranking; labels only (no eval shown)
            var b = [Int8](repeating: 0, count: Self.cells)
            var v: Float = 0
            if nardi_analyzed_move(handle, Int32(i), &b, &v) == NARDI_OK {
                tops.append(TopMove(board: b,
                                    label: subMoveLabel(preBoard: preBoard, side: currentSide, end: b, d1, d2)))
            }
        }
        topMoves = tops
        diceApplied = true
        noLegalMoves = (n == 0)
        selected = nil
        canConfirm = false
        dieUsable = (nardi_can_use_die(handle, 0) == 1, nardi_can_use_die(handle, 1) == 1)
        positionEval = staticEval()   // res2 display eval of the current position
        status = n > 0 ? "Top engine moves for \(currentSide ? "Black" : "White") shown — make your move."
                       : "No legal move for \(die1)-\(die2). Tap Pass."
    }

    /// True once a checker has actually moved this turn. NOTE: we compare the
    /// board to the turn's starting board rather than using turn_in_progress,
    /// because analyze_dice marks the engine's dice as "rolled" (turn_in_progress
    /// becomes true) even though no checker has moved yet.
    var movedThisTurn: Bool {
        guard let ts = turnStart else { return false }
        return engineBoard() != ts.board
    }

    /// Re-apply the dice after the user changes a stepper (no-op while locked to
    /// game dice, or once a checker has moved -- Undo first to change again).
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

    /// Apply the current dice when entering a fresh turn (start / confirm / pass /
    /// step-back), so the playable dice + eval appear without an extra tap.
    private func autoApplyDice() {
        if modelLoaded && !isTerminal { setDice() }
    }

    /// Eval of the current (possibly mid/after-move) board — the res2 display eval.
    private func currentEval() -> Float? { staticEval() }

    // MARK: - Analysis: hands-on moves

    func tap(row: Int, col: Int) {
        guard phase == .analyzing, diceApplied, !noLegalMoves, !isAnimating, !canConfirm else { return }
        _ = nardi_human_select(handle, Int32(row), Int32(col))
        updateSelection()
    }

    func tapDie(_ idx: Int) {
        guard phase == .analyzing, diceApplied, !isAnimating, !canConfirm else { return }
        guard nardi_start_selected(handle) == 1 else { status = "Select a checker first, then a die."; return }
        if nardi_human_move_die(handle, Int32(idx)) != NARDI_OK {
            status = "Illegal move for that die."
            updateSelection()
            return
        }
        selected = nil
        canUndo = true
        Task { @MainActor in
            await animateMoves()
            dieUsable = (nardi_can_use_die(handle, 0) == 1, nardi_can_use_die(handle, 1) == 1)
            updateSelection()
            if nardi_turn_is_complete(handle) == 1 {
                canConfirm = true
                status = "Move complete — Confirm to switch sides (or Undo)."
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
        nardi_confirm_turn(handle)
        currentSide = (nardi_current_player(handle) == 1)
        flipped = currentSide
        board = engineBoard()
        turnStart = nil
        resetTurnState()
        isTerminal = (nardi_is_terminal(handle) == 1)
        canUndo = !history.isEmpty
        relTurn += 1                       // advance the game-dice pointer
        positionEval = staticEval()
        status = isTerminal ? "Position is terminal. Undo to continue."
                            : "\(currentSide ? "Black" : "White") to move."
        autoApplyDice()   // apply dice for the new side (game dice or free)
    }

    /// No legal move for the chosen dice: switch sides without moving (undoable).
    func pass() {
        guard phase == .analyzing, diceApplied, noLegalMoves, !isAnimating else { return }
        if let ts = turnStart { history.append(ts) }
        currentSide.toggle()
        flipped = currentSide
        board.withUnsafeBufferPointer { _ = nardi_set_position(handle, $0.baseAddress, currentSide ? 1 : 0) }
        turnStart = nil
        resetTurnState()
        relTurn += 1
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
