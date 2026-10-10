import SwiftUI

/// The play-screen board: feeds NardiGame's published state into the shared
/// vector `BoardCanvas` and routes taps / dice-toggle / confirm back to the game.
struct BoardView: View {
    @EnvironmentObject var game: NardiGame
    var alwaysShowConfirm = false          // tutorial: keep the ✓ present but greyed
    var centerHalfControls = false         // EXPERIMENTAL: on-board Undo/Confirm per half
    var publishAnchors = false             // tutorial: publish control frames for coach marks
    @AppStorage(BoardTheme.storageKey) private var boardThemeRaw = BoardTheme.green.rawValue
    @AppStorage(PipDisplay.storageKey) private var showPips = true

    private var theme: BoardTheme { BoardTheme(rawValue: boardThemeRaw) ?? .green }

    var body: some View {
        BoardCanvas(
            board: game.board,
            flipped: game.flipped,
            selected: game.selected,
            flights: game.flights,
            startMask: game.isReplaying ? 0 : game.startMaskCombined,
            whitePips: game.pipCounts.white,
            blackPips: game.pipCounts.black,
            whiteOff: game.whiteOff,
            blackOff: game.blackOff,
            whiteEndgame: game.whiteEndgame,
            blackEndgame: game.blackEndgame,
            showPips: showPips,
            theme: theme,
            dice: game.dice.0 > 0
                ? game.orderedDice.enumerated().map { i, d in
                    // Physical die index: the first entry is the preferred die.
                    let id = (i == 0) == (game.firstDieIndex == 0) ? 0 : 1
                    return DieSpec(value: d.value, id: id, hasStart: d.hasStart, isFirst: d.isFirst)
                }
                : [],
            rollSeq: game.rollSeq,
            diceEmphasis: game.swapCount,
            canConfirm: game.canConfirm,
            alwaysShowConfirm: alwaysShowConfirm,
            centerHalfControls: centerHalfControls,
            canUndo: game.canUndo,
            publishAnchors: publishAnchors,
            onTap: { row, col in game.tap(row: row, col: col) },
            onToggleDice: { game.toggleTryFirst() },
            onConfirm: { game.confirm() },
            onUndo: centerHalfControls ? { game.undo() } : nil
        )
    }
}
