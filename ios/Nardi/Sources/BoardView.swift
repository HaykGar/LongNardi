import SwiftUI

/// The play-screen board: feeds NardiGame's published state into the shared
/// BoardCanvas and routes taps back to the game.
struct BoardView: View {
    @EnvironmentObject var game: NardiGame

    var body: some View {
        BoardCanvas(board: game.board,
                    flipped: game.flipped,
                    selected: game.selected,
                    flights: game.flights,
                    startMask: game.startMasks.0 | game.startMasks.1,
                    whitePips: game.pipCounts.white,
                    blackPips: game.pipCounts.black,
                    onTap: { row, col in game.tap(row: row, col: col) })
    }
}
