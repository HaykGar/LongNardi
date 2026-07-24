import SwiftUI

@main
struct NardiApp: App {
    @StateObject private var game = NardiGame()
    @StateObject private var analyze = AnalyzeGame()
    @StateObject private var store = MatchStore()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(game)
                .environmentObject(analyze)
                .environmentObject(store)
                .preferredColorScheme(.dark)   // v2 is a fixed warm-dark theme
                .tint(Palette.accentSoft)
        }
    }
}
