import SwiftUI

enum AppSection: String, CaseIterable, Identifiable {
    case play = "Play"
    case analyze = "Analyze"
    case history = "History"
    var id: String { rawValue }
}

struct ContentView: View {
    @EnvironmentObject var game: NardiGame
    @EnvironmentObject var analyze: AnalyzeGame
    @EnvironmentObject var store: MatchStore
    @State private var section: AppSection = .play
    @State private var showAnalyze = false
    @State private var review: GameReview? = nil
    @State private var mode: GameMode = .vsComputer
    @State private var opponent: Opponent = .greedy
    @State private var first: FirstMove = .first

    var body: some View {
        Group {
            if let review {
                GameReviewView(review: review,
                               autoOpen: ProcessInfo.processInfo.arguments.contains("--review-analyze"),
                               onExit: { self.review = nil })
                    .environmentObject(analyze)
            } else if showAnalyze {
                AnalyzeScreen(onExit: { showAnalyze = false }).environmentObject(analyze)
            } else if game.phase == .setup {
                setupScreen
            } else {
                gameScreen
            }
        }
        .onAppear {
            game.onGameFinished = { [weak store] in store?.add($0) }
            let args = ProcessInfo.processInfo.arguments
            if args.contains("--seed-history") || args.contains("--seed-history-review") {
                game.devSeedHistory()
            }
            // Dev: seed history then re-run review on the most recent match.
            if args.contains("--seed-history-review"), let m = store.matches.first {
                openReview(m)
            }
            if args.contains("--history") || args.contains("--seed-history") { section = .history }
            if args.contains("--demo") {
                game.newGame(mode: .vsComputer, opponent: .greedy,
                             first: args.contains("--black") ? .second : .first)
            }
            // Dev hooks for verifying the Analyze flow: --analyze opens the editor
            // pre-filled with the standard start; --analyze-run also starts the
            // analysis and ranks the default dice.
            if args.contains("--analyze") || args.contains("--analyze-run") {
                section = .analyze
                analyze.standardStart()
                showAnalyze = true
                if args.contains("--analyze-run") {
                    analyze.startAnalysis()        // auto-applies the default dice
                }
            }
            // Dev: open analysis and programmatically play one sub-move (select the
            // white head, tap die 0) to verify move-playing works without taps.
            if args.contains("--analyze-move") {
                section = .analyze
                analyze.standardStart()
                analyze.startAnalysis()
                showAnalyze = true
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) {
                    analyze.tap(row: 0, col: 0)
                    analyze.tapDie(0)
                }
            }
            if args.contains("--review") || args.contains("--review-analyze") {
                game.devAutoPlayAndReview()
                review = GameReview(log: game.reviewLog, reviewSide: game.reviewSide)
            }
        }
    }

    // MARK: - Setup

    private var setupScreen: some View {
        VStack(spacing: 18) {
            Text("Nardi").font(.system(size: 40, weight: .bold)).padding(.top, 14)

            Picker("Section", selection: $section) {
                ForEach(AppSection.allCases) { Text($0.rawValue).tag($0) }
            }.pickerStyle(.segmented).padding(.horizontal, 30)

            switch section {
            case .play:    playSetup
            case .analyze: analyzeSetup
            case .history: MatchHistoryView(store: store, onReview: openReview).padding(.top, 4)
            }
        }
    }

    private var playSetup: some View {
        VStack(spacing: 22) {
            Spacer()
            Picker("Mode", selection: $mode) {
                ForEach(GameMode.allCases) { Text($0.rawValue).tag($0) }
            }.pickerStyle(.segmented)

            if mode == .vsComputer {
                VStack(spacing: 14) {
                    labeledPicker("Opponent", selection: $opponent, options: Opponent.allCases) { $0.rawValue }
                    labeledPicker("You", selection: $first, options: FirstMove.allCases) { $0.rawValue }
                }
            } else {
                Text("Two players, one device.\nThe board flips each turn.")
                    .multilineTextAlignment(.center).foregroundStyle(.secondary)
            }

            Button {
                game.newGame(mode: mode, opponent: opponent, first: first)
            } label: {
                Text("Start Game").font(.title3.bold()).frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent).padding(.horizontal, 40)
            Spacer()
        }
        .padding(.horizontal)
    }

    private var analyzeSetup: some View {
        VStack(spacing: 22) {
            Spacer()
            Text("Build any position, set the dice, and play moves by hand while the learned evaluator tracks how the eval swings.")
                .multilineTextAlignment(.center).foregroundStyle(.secondary).padding(.horizontal, 30)
            Button {
                analyze.backToEditor()   // always start in the editor (shared model may be mid-analysis)
                showAnalyze = true
            } label: {
                Text("Open Analysis").font(.title3.bold()).frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent).padding(.horizontal, 40)
            Spacer()
        }
        .padding(.horizontal)
    }

    /// Re-run game review on a stored match. Its turn log replays exactly, so the
    /// review (and its "Open in Analyzer" jump) work just as for a live game.
    private func openReview(_ match: SavedMatch) {
        review = GameReview(log: match.reviewTurns, reviewSide: match.reviewSide)
    }

    private func labeledPicker<T: Hashable & Identifiable>(
        _ label: String, selection: Binding<T>, options: [T], title: @escaping (T) -> String
    ) -> some View {
        HStack {
            Text(label).frame(width: 90, alignment: .leading)
            Picker(label, selection: selection) {
                ForEach(options) { Text(title($0)).tag($0) }
            }.pickerStyle(.menu)
            Spacer()
        }
        .padding(.horizontal, 30)
    }

    // MARK: - Game

    private var gameScreen: some View {
        VStack(spacing: 10) {
            HStack {
                offBadge(label: "White off", count: game.whiteOff, white: true)
                Spacer()
                Button("Menu") { game.backToSetup() }.font(.callout)
                Spacer()
                offBadge(label: "Black off", count: game.blackOff, white: false)
            }
            .padding(.horizontal)

            BoardView().environmentObject(game).padding(.horizontal, 6)

            Text(game.status).font(.callout).multilineTextAlignment(.center)
                .frame(maxWidth: .infinity, minHeight: 24)

            HStack(spacing: 18) {
                DieButton(value: game.dice.0, usable: game.dieUsable.0) { game.tapDie(0) }
                DieButton(value: game.dice.1, usable: game.dieUsable.1) { game.tapDie(1) }
                Button { game.undo() } label: {
                    Label("Undo", systemImage: "arrow.uturn.backward")
                }
                .buttonStyle(.bordered)
                .disabled(game.phase != .awaitingHuman)
                Button { game.confirm() } label: {
                    Label("Confirm", systemImage: "checkmark")
                }
                .buttonStyle(.borderedProminent)
                .disabled(!game.canConfirm)
            }
            .padding(.bottom, 6)

            if case let .gameOver(message) = game.phase {
                VStack(spacing: 8) {
                    Text(message).font(.title2.bold())
                    HStack(spacing: 14) {
                        if game.hasReview {
                            Button { review = GameReview(log: game.reviewLog, reviewSide: game.reviewSide) }
                                label: { Label("Review", systemImage: "chart.line.uptrend.xyaxis") }
                                .buttonStyle(.bordered)
                        }
                        Button("New Game") { game.backToSetup() }.buttonStyle(.borderedProminent)
                    }
                }
                .padding(.bottom, 12)
            }
            Spacer(minLength: 0)
        }
        .padding(.top, 8)
    }

    private func offBadge(label: String, count: Int, white: Bool) -> some View {
        VStack(spacing: 2) {
            Text(label).font(.caption2).foregroundStyle(.secondary)
            Text("\(count)").font(.headline.monospacedDigit())
                .foregroundColor(white ? .primary : .primary)
        }
    }
}

private struct DieButton: View {
    let value: Int
    let usable: Bool
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(value > 0 ? "\(value)" : "–")
                .font(.title.monospacedDigit().bold())
                .frame(width: 52, height: 52)
                .background(RoundedRectangle(cornerRadius: 8)
                    .fill(usable ? Color(.systemGray5) : Color(.systemGray3)))
                .foregroundColor(usable ? .primary : .secondary)
                .overlay(RoundedRectangle(cornerRadius: 8)
                    .stroke(usable ? Color.accentColor : Color.clear, lineWidth: 2))
        }
        .disabled(!usable)
    }
}
