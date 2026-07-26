import SwiftUI

/// What the setup step produces and `newGame` consumes.
struct PlayConfig: Equatable {
    var mode: GameMode = .vsComputer
    var opponent: Opponent = .medium
    var first: FirstMove = .first
}

extension FirstMove {
    /// Short chip label (the rawValues like "I play first (White)" are too long here).
    var shortLabel: String {
        switch self {
        case .first:  return "First"
        case .second: return "Second"
        case .random: return "Random"
        }
    }
}

/// The new Play setup step (mockup has none — Play links straight to the board, so
/// this is designed in the same warm-wood language as Home). Pick opponent +
/// first-move (or Pass & Play), then Start.
struct PlaySetupView: View {
    let onStart: (PlayConfig) -> Void
    let onBack: () -> Void
    @State private var cfg = PlayConfig()
    // Default the opponent to the difficulty last played; picking a new one writes
    // it straight back so it sticks across game-over, quit, and app restart.
    @AppStorage(Opponent.storageKey) private var savedOpponent = Opponent.medium
    // Same for the first-move choice, so first/second/random sticks like difficulty.
    @AppStorage(FirstMove.storageKey) private var savedFirst = FirstMove.first

    var body: some View {
        ZStack(alignment: .topLeading) {
            RadialBackground(surface: .home)

            Button(action: onBack) {
                Label("Home", systemImage: "chevron.left")
            }
            .buttonStyle(GhostPillButtonStyle())
            .padding(.leading, 20).padding(.top, 16)

            HStack(alignment: .center, spacing: 34) {
                // Left: title + CTA
                VStack(alignment: .leading, spacing: 16) {
                    Text("New Game").font(ThemeFont.serif(48, .bold)).foregroundStyle(Palette.cream)
                    Text("Choose who you're up against, then who moves first.")
                        .font(ThemeFont.bodyText).foregroundStyle(Palette.creamDim)
                        .frame(maxWidth: 280, alignment: .leading)
                    Spacer().frame(height: 4)
                    Button { onStart(cfg) } label: {
                        Label("Start Game", systemImage: "play.fill")
                    }
                    .buttonStyle(PrimaryCTAButtonStyle())
                    .frame(maxWidth: 300)
                }
                .frame(maxWidth: .infinity, alignment: .leading)

                // Right: choices
                VStack(spacing: 14) {
                    ChoiceCard(title: "Mode") {
                        SegmentedChips(options: GameMode.allCases, selection: $cfg.mode,
                                       context: .onDark) { $0.rawValue }
                    }
                    if cfg.mode == .vsComputer {
                        ChoiceCard(title: "Opponent") {
                            SegmentedChips(options: Opponent.allCases,
                                           selection: Binding(get: { cfg.opponent },
                                                              set: { cfg.opponent = $0; savedOpponent = $0 }),
                                           context: .onDark) { $0.rawValue }
                        }
                        ChoiceCard(title: "You play") {
                            SegmentedChips(options: FirstMove.allCases,
                                           selection: Binding(get: { cfg.first },
                                                              set: { cfg.first = $0; savedFirst = $0 }),
                                           context: .onDark) { $0.shortLabel }
                        }
                    } else {
                        ChoiceCard(title: "Pass & Play") {
                            Text("Two players, one device. The board flips to the active player each turn (toggle in-game).")
                                .font(ThemeFont.caption).foregroundStyle(Palette.creamDim)
                        }
                    }
                }
                .frame(width: 420)
            }
            .padding(.horizontal, 48)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .onAppear { cfg.opponent = savedOpponent; cfg.first = savedFirst }
    }
}

/// A tan-bordered cognac card with a small uppercase caption.
private struct ChoiceCard<Content: View>: View {
    let title: String
    @ViewBuilder var content: () -> Content
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            SectionLabel(text: title)
            content()
        }
        .padding(14)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 16).fill(Grad.homeNavCard))
        .overlay(RoundedRectangle(cornerRadius: 16).stroke(Palette.strokeWarm.opacity(0.18), lineWidth: 1))
    }
}
