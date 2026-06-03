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
                    animProgress: game.animProgress,
                    onTap: { row, col in game.tap(row: row, col: col) })
    }
}
