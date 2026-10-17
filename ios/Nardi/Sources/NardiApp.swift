import SwiftUI

@main
struct NardiApp: App {
    @StateObject private var game = NardiGame()
    @StateObject private var analyze = AnalyzeGame()
    @StateObject private var store = MatchStore()

    init() {
        #if DEBUG
        // dev: bring back the one-time sharing notice.
        if ProcessInfo.processInfo.arguments.contains("--reset-consent-notice") {
            UserDefaults.standard.removeObject(forKey: SharingConsent.noticeAcknowledgedKey)
        }
        #endif
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
