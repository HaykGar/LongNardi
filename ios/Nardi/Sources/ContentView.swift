import SwiftUI

/// Top-level destinations. The Home hero is the root; the review screen is a
/// separate overlay (@State review) that wins over `route`, reproducing v1's
/// `if let review { … }` precedence so review open/close behaves identically.
enum Route: Equatable {
    case home, playSetup, game, analyze, history, tutorialChooser, tutorial

    /// Navigation depth: moving deeper pushes in, moving shallower pulls back.
    var depth: Int {
        switch self {
        case .home: return 0
        case .playSetup, .analyze, .history, .tutorialChooser: return 1
        case .game, .tutorial: return 2
        }
    }
}

struct ContentView: View {
    @EnvironmentObject var game: NardiGame
    @EnvironmentObject var analyze: AnalyzeGame
    @EnvironmentObject var store: MatchStore

    @State private var route: Route = .home {
        willSet { forward = newValue.depth > route.depth }
    }
    @State private var review: GameReview? = nil {
        willSet { forward = newValue != nil }
    }
    /// Reviews still computing, by match — reopening one rejoins it instead of
    /// starting over (closing Review doesn't stop the work).
    @State private var running: [UUID: GameReview] = [:]
    @State private var forward = true                    // direction of the last navigation
    @State private var reviewReturn: Route = .history   // where "Done" lands after review
    @State private var tourStart = 0                    // dev hook: tour step to open at

    /// Screens slide in 16pt from the right going deeper (from the left going back).
    /// The outgoing screen is removed un-animated: screens are full-bleed, so any
    /// crossfade shows two screens (e.g. game board + review) through each other.
    private var screenTransition: AnyTransition {
        .asymmetric(insertion: .opacity.combined(with: .offset(x: forward ? 16 : -16))
                        .combined(with: .scale(scale: 0.992)),
                    removal: AnyTransition.opacity.animation(nil))
    }

    var body: some View {
        ZStack {
            Color(hex: 0x150B04).ignoresSafeArea()   // dark backdrop under the fade-in
            screen
                .id(review != nil ? "review" : "\(route)")
                .transition(screenTransition)
        }
        .animation(Motion.anim(Motion.out(Motion.Duration.enter)), value: route)
        .animation(Motion.anim(Motion.out(Motion.Duration.enter)), value: review != nil)
        .onAppear(perform: wireCallbacksAndDevHooks)
    }

    @ViewBuilder
    private var screen: some View {
        Group {
            if let review {
                GameReviewView(review: review,
                               autoOpen: ProcessInfo.processInfo.arguments.contains("--review-analyze"),
                               onExit: { self.review = nil; route = reviewReturn })
                    .environmentObject(analyze)
            } else {
                switch route {
                case .home:
                    HomeView(store: store,
                             onPlay:     { route = .playSetup },
                             onAnalyze:  { analyze.backToEditor(); route = .analyze },
                             onHistory:  { route = .history },
                             onTutorial: { route = .tutorialChooser })
                case .tutorialChooser:
                    TutorialChooserView(onQuickTour: { route = .tutorial },
                                        onBack:      { route = .home })
                case .tutorial:
                    TutorialView(onSkip:         { route = .tutorialChooser },
                                 onStartPlaying: { route = .playSetup },
                                 startStep:      tourStart)
                case .playSetup:
                    PlaySetupView(onStart: startGame, onBack: { route = .home })
                case .game:
                    GameView(onQuit:    { game.backToSetup(); route = .home },
                             onNewGame: { game.backToSetup(); route = .playSetup },
                             onReview:  openReviewFromCurrentGame)
                        .environmentObject(game)
                case .analyze:
                    AnalyzeScreen(onExit: { route = .home }).environmentObject(analyze)
                case .history:
                    HistoryView(store: store,
                                onReview: { m in reviewReturn = .history; openReview(m) },
                                onBack:   { route = .home })
                }
            }
        }
    }

    // MARK: - Actions

    private func startGame(_ cfg: PlayConfig) {
        game.newGame(mode: cfg.mode, opponent: cfg.opponent, first: cfg.first)
        route = .game
    }

    private func openReviewFromCurrentGame() {
        reviewReturn = .game
        // The rollout this game has been running all along — usually already done.
        let pending = game.counterfactual
        if let id = game.lastMatchID, let m = store.matches.first(where: { $0.id == id }) {
            openReview(m, pending: pending)
        } else {
            review = GameReview(log: game.reviewLog, reviewSide: game.reviewSide, pending: pending)
        }
    }

    /// Open review for a stored match, reusing its cached review or computing + caching once.
    private func openReview(_ match: SavedMatch, pending: CounterfactualRunner? = nil) {
        if let r = running[match.id], !r.ready { review = r; return }
        let r = GameReview(log: match.reviewTurns, reviewSide: match.reviewSide,
                           cached: match.review, pending: pending,
                           onComputed: { [weak store] data in store?.setReview(match.id, data) })
        running[match.id] = r
        review = r
        Task { await r.compute(); running[match.id] = nil }   // runs on even if Review closes
    }

    // MARK: - Dev / verification launch hooks (preserved from v1; destinations → route)

    private func wireCallbacksAndDevHooks() {
        // MUST be first: devSeedHistory() calls onGameFinished synchronously, which
        // --seed-history-review then reads back from the store.
        game.onGameFinished = { [weak store] in store?.add($0) }
        let uploader = GameUploader.shared
        game.onGameForUpload = { uploader.submitInBackground($0) }
        analyze.onLogSaved = { [weak store] in store?.add($0) }
        let args = ProcessInfo.processInfo.arguments

        if args.contains("--seed-history") || args.contains("--seed-history-review") {
            game.devSeedHistory()
        }
        if args.contains("--seed-history-review"), let m = store.matches.first {
            reviewReturn = .history
            openReview(m)
        }
        if args.contains("--history") || args.contains("--seed-history") { route = .history }
        if args.contains("--tutorial") { route = .tutorialChooser }
        if args.contains("--tour") { route = .tutorial }
        if args.contains("--tour-undo") { tourStart = 3; route = .tutorial }      // dev: chrome glow
        if args.contains("--tour-confirm") { tourStart = 2; route = .tutorial }   // dev: confirm glow
        if let i = args.firstIndex(of: "--tour-step"), i + 1 < args.count,        // dev: any glow step
           let n = Int(args[i + 1]) { tourStart = n; route = .tutorial }

        if args.contains("--demo") || args.contains("--demo-move") {
            game.newGame(mode: .vsComputer, opponent: .medium,
                         first: args.contains("--black") ? .second : .first)
            route = .game
        }
        // Start a game and offer a resignation on the human's turn (verify accept/decline).
        if args.contains("--resign-oin") || args.contains("--resign-mars") {
            let level: ResignLevel = args.contains("--resign-mars") ? .mars : .oin
            game.newGame(mode: args.contains("--pnp") ? .passAndPlay : .vsComputer,
                         opponent: .hard, first: .first)
            route = .game
            Task { @MainActor in
                for _ in 0..<16 {
                    if game.canOffer { game.offerResign(level); break }
                    try? await Task.sleep(nanoseconds: 400_000_000)
                }
            }
        }
        // Play the human's turn out so the center Undo + Confirm controls can be verified.
        if args.contains("--demo-move") {
            Task { @MainActor in
                for _ in 0..<30 {
                    if game.phase == .awaitingHuman && !game.canConfirm && !game.isAnimating {
                        let mask = game.startMasks.0 | game.startMasks.1
                        if mask != 0 {
                            let i = mask.trailingZeroBitCount
                            game.tap(row: i / NardiGame.cols, col: i % NardiGame.cols)
                        }
                    }
                    if game.canConfirm { break }
                    try? await Task.sleep(nanoseconds: 400_000_000)
                }
                if args.contains("--demo-undo") {
                    try? await Task.sleep(nanoseconds: 2_000_000_000)
                    game.undo()
                }
            }
        }
        // A spread-out mid-game position + a 6-3 roll (several legal moves) → verify top-3.
        if args.contains("--analyze-mid") {
            var b = [Int8](repeating: 0, count: 24)
            b[0] = 11; b[3] = 1; b[5] = 1; b[7] = 1; b[9] = 1
            b[12] = -15
            analyze.devSetup(board: b, side: false, d1: 6, d2: 3)
            route = .analyze
            if args.contains("--analyze-swap") {
                DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { analyze.toggleTryFirst() }
            }
        }
        // Die 6 has no legal start (blocked) but die 3 does — verify the no-start die greys.
        if args.contains("--analyze-block") {
            var b = [Int8](repeating: 0, count: 24)
            b[0] = 13; b[1] = 2
            b[6] = -2; b[7] = -2
            b[12] = -11
            analyze.devSetup(board: b, side: false, d1: 6, d2: 3)
            route = .analyze
        }
        // Open the analyzer in review mode on a 1-move line, then Follow to its end
        // (a non-terminal position) to verify you can keep playing past the end.
        if args.contains("--review-end") {
            var std = [Int8](repeating: 0, count: 24); std[0] = 15; std[12] = -15
            var b2 = [Int8](repeating: 0, count: 24); b2[0] = 14; b2[11] = 1; b2[12] = -15
            analyze.openForReview(board: std, side: false, anchor: false,
                                  gameDice: [(6, 5)], gameLine: [std, b2])
            route = .analyze
            Task { @MainActor in
                try? await Task.sleep(nanoseconds: 800_000_000)
                analyze.followGameMove()
            }
        }
        if args.contains("--analyze") || args.contains("--analyze-run") {
            analyze.standardStart()
            route = .analyze
            if args.contains("--analyze-run") { analyze.startAnalysis() }
            if args.contains("--black") {            // dev: editor with the Black chips selected
                analyze.editTool = .black
                analyze.sideToMove = true
            }
        }
        // Open analysis and play one sub-move programmatically (verify move-playing).
        if args.contains("--analyze-move") || args.contains("--analyze-undo") {
            analyze.standardStart()
            analyze.startAnalysis()
            route = .analyze
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) {
                analyze.tap(row: 0, col: 0)
                if args.contains("--analyze-undo") {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { analyze.undo() }
                }
            }
        }
        if args.contains("--review") || args.contains("--review-analyze") {
            game.devAutoPlayAndReview()
            reviewReturn = .home
            review = GameReview(log: game.reviewLog, reviewSide: game.reviewSide)
        }
        // dev: play a real game (runner fed live), then open its review — it should
        // appear with no rollout wait.
        if args.contains("--live-review") {
            Task { @MainActor in
                await game.devLivePlayAndReview()
                route = .game
                openReviewFromCurrentGame()
            }
        }
    }
}
