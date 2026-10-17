import SwiftUI

@main
struct NardiApp: App {
    @StateObject private var game = NardiGame()
    @StateObject private var analyze = AnalyzeGame()
    @StateObject private var store = MatchStore()

    init() {
        // dev: bring back the one-time sharing banner (before Home reads it).
        if ProcessInfo.processInfo.arguments.contains("--reset-consent-banner") {
            UserDefaults.standard.removeObject(forKey: SharingConsent.bannerDismissedKey)
        }
    }

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
