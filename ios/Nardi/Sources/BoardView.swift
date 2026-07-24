import SwiftUI

/// The play-screen board: feeds NardiGame's published state into the shared
/// vector `BoardCanvas` and routes taps / dice-toggle / confirm back to the game.
struct BoardView: View {
    @EnvironmentObject var game: NardiGame
    @AppStorage(BoardTheme.storageKey) private var boardThemeRaw = BoardTheme.green.rawValue
    @AppStorage(PipDisplay.storageKey) private var showPips = true

    private var theme: BoardTheme { BoardTheme(rawValue: boardThemeRaw) ?? .green }

    var body: some View {
        BoardCanvas(
            board: game.board,
            flipped: game.flipped,
            selected: game.selected,
            flights: game.flights,
            startMask: game.startMaskCombined,
            whitePips: game.pipCounts.white,
            blackPips: game.pipCounts.black,
            whiteOff: game.whiteOff,
            blackOff: game.blackOff,
            whiteEndgame: game.whiteEndgame,
            blackEndgame: game.blackEndgame,
            showPips: showPips,
            theme: theme,
            dice: game.dice.0 > 0
                ? game.orderedDice.map { DieSpec(value: $0.value, hasStart: $0.hasStart, isFirst: $0.isFirst) }
                : [],
            canConfirm: game.canConfirm,
            onTap: { row, col in game.tap(row: row, col: col) },
            onToggleDice: { game.toggleTryFirst() },
            onConfirm: { game.confirm() }
        )
    }
}
