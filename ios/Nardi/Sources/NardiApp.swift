import SwiftUI

@main
struct NardiApp: App {
    @StateObject private var game = NardiGame()
    @StateObject private var analyze = AnalyzeGame()

    var body: some Scene {
        WindowGroup {
            ContentView().environmentObject(game).environmentObject(analyze)
        }
    }
}
