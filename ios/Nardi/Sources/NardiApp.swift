import SwiftUI

@main
struct NardiApp: App {
    @StateObject private var game = NardiGame()

    var body: some Scene {
        WindowGroup {
            ContentView().environmentObject(game)
        }
    }
}
