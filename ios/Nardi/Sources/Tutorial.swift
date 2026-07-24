import SwiftUI

// MARK: - Tutorial chooser (mockup 3a)

/// "How to play": pick the quick UI tour (built) or the full rules lesson (tabled).
/// Reached from Home's "New to Nardi?" ribbon.
struct TutorialChooserView: View {
    let onQuickTour: () -> Void
    let onBack: () -> Void

    var body: some View {
        ZStack {
            RadialBackground(surface: .home)
            HStack(alignment: .center, spacing: 30) {
                VStack(alignment: .leading, spacing: 14) {
                    Text("How to play").font(ThemeFont.serif(40, .bold)).foregroundStyle(Palette.cream)
                    Text("Two ways in. Already know Long Nardi? Take the quick tour of the controls. New to the game? The full lesson walks the rules with you.")
                        .font(ThemeFont.bodyText).foregroundStyle(Palette.creamDim)
                        .frame(maxWidth: 300, alignment: .leading)
                    Button(action: onBack) { Label("Back to Home", systemImage: "chevron.left") }
                        .font(ThemeFont.caption).foregroundStyle(Palette.muted).buttonStyle(.plain)
                        .padding(.top, 4)
                }
                .frame(width: 320, alignment: .leading)

                Spacer(minLength: 20)

                VStack(spacing: 14) {
                    // Quick UI tour — built.
                    Button(action: onQuickTour) {
                        ChooserCardBody(icon: "bolt.fill", iconColor: .white, titleColor: .white,
                                        title: "I know the rules",
                                        subtitle: "Quick UI tour · ~30 seconds. Tapping, dice order, undo, replay & settings.",
                                        subtitleColor: Color.white.opacity(0.85))
                        .background(RoundedRectangle(cornerRadius: 18).fill(Grad.cta)
                            .shadow(color: .black.opacity(0.45), radius: 8, y: 8))
                    }
                    .buttonStyle(.plain)

                    // Full rules lesson — coming soon (disabled).
                    ChooserCardBody(icon: "sparkles", iconColor: Palette.accentSoft, titleColor: Palette.cream,
                                    title: "New to Nardi",
                                    subtitle: "Full tutorial · rules + UI, taught through guided scenarios.",
                                    subtitleColor: Palette.creamDim)
                        .opacity(0.82)
                        .overlay(alignment: .topTrailing) {
                            Text("COMING SOON").font(.system(size: 9, weight: .heavy)).tracking(1)
                                .foregroundStyle(Palette.accentSoft)
                                .padding(.horizontal, 8).padding(.vertical, 4)
                                .background(Capsule().fill(Palette.accentSoft.opacity(0.15)))
                                .padding(14)
                        }
                        .background(RoundedRectangle(cornerRadius: 18).fill(Grad.homeNavCard)
                            .overlay(RoundedRectangle(cornerRadius: 18).stroke(Palette.strokeWarm.opacity(0.18), lineWidth: 1)))
                }
                .frame(width: 400)
            }
            .padding(.leading, 36).padding(.trailing, 40)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
}

private struct ChooserCardBody: View {
    let icon: String
    let iconColor: Color
    let titleColor: Color
    let title: String
    let subtitle: String
    let subtitleColor: Color
    var body: some View {
        HStack(spacing: 16) {
            Image(systemName: icon).font(.system(size: 22, weight: .semibold))
                .foregroundStyle(iconColor).frame(width: 30)
            VStack(alignment: .leading, spacing: 3) {
                Text(title).font(ThemeFont.cardTitle).foregroundStyle(titleColor)
                Text(subtitle).font(ThemeFont.caption).foregroundStyle(subtitleColor)
            }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 20).padding(.vertical, 18)
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

// MARK: - Fast tour (mockup 3b)

/// Which element the current coach-mark glows.
enum TourTarget: Hashable { case greenDots, dice, confirm, undo, replay, settings, resign }

private struct TourStep {
    let target: TourTarget
    let title: String
    let body: String
}

/// Anchor-preference plumbing so the glow can sit exactly over the tagged element
/// wherever it lays out.
struct TourAnchorKey: PreferenceKey {
    static var defaultValue: [TourTarget: Anchor<CGRect>] = [:]
    static func reduce(value: inout [TourTarget: Anchor<CGRect>], nextValue: () -> [TourTarget: Anchor<CGRect>]) {
        value.merge(nextValue()) { _, n in n }
    }
}
extension View {
    func tourAnchor(_ t: TourTarget) -> some View {
        anchorPreference(key: TourAnchorKey.self, value: .bounds) { [t: $0] }
    }
}

/// A guided walkthrough over a REAL, fully-interactive game (a tutorial-local
/// NardiGame). The opponent opens with an animated move, so the player lands on
/// their first turn able to tap checkers, reorder dice, undo, confirm, and replay
/// exactly as in a normal game. Each step glows one control and explains it;
/// Next / Back / Restart never gate play. Skip (→ chooser), Start playing (→ setup).
struct TutorialView: View {
    let onSkip: () -> Void
    let onStartPlaying: () -> Void
    var startStep: Int = 0        // dev hook only; users always start at 0

    @StateObject private var game = NardiGame()
    @AppStorage(BoardTheme.storageKey) private var boardThemeRaw = BoardTheme.green.rawValue
    @AppStorage(AnimationSpeed.storageKey) private var speed: AnimationSpeed = .high
    @State private var step = 0
    @State private var started = false
    @State private var menuSettings = false
    @State private var menuResign = false

    private var theme: BoardTheme { BoardTheme(rawValue: boardThemeRaw) ?? .green }

    private let steps: [TourStep] = [
        .init(target: .greenDots, title: "Tap to move",
              body: "Your opponent just moved. A green dot marks every checker you can move — tap one to play it with your dice."),
        .init(target: .dice, title: "Your dice",
              body: "These are your dice. Tap either one to switch which value a tapped checker tries first."),
        .init(target: .confirm, title: "Confirm your turn",
              body: "Once your turn is complete, the green ✓ appears — tap it to lock in your move."),
        .init(target: .undo, title: "Undo",
              body: "Changed your mind? Undo takes back a hop before you confirm."),
        .init(target: .replay, title: "Replay",
              body: "Replay re-plays your opponent's last move — tap it to watch again."),
        .init(target: .settings, title: "Settings",
              body: "Animation speed and quitting to the menu live here."),
        .init(target: .resign, title: "Resign",
              body: "Offer to resign at any time — a single (oin) or a double (mars)."),
    ]

    private var current: TourStep { steps[min(step, steps.count - 1)] }

    var body: some View {
        ZStack(alignment: .top) {
            RadialBackground(surface: theme.surface)

            VStack(spacing: 4) {
                header
                HStack(alignment: .center, spacing: 14) {
                    boardArea.frame(maxWidth: .infinity)
                    coachCard.frame(width: 240)
                }
                .padding(.horizontal, 14).padding(.bottom, 8)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }

            topRightChrome
        }
        .environmentObject(game)
        .overlayPreferenceValue(TourAnchorKey.self) { anchors in
            GeometryReader { geo in
                if let rect = glowRect(current.target, anchors, geo) {
                    TourGlow(cornerRadius: current.target == .greenDots ? 14 : 22)
                        .frame(width: rect.width + 8, height: rect.height + 8)
                        .position(x: rect.midX, y: rect.midY)
                        .allowsHitTesting(false)
                        .animation(.easeInOut(duration: 0.3), value: step)
                }
            }
        }
        .onAppear {
            if !started {
                started = true
                game.setupTutorial()   // fixed position + dice, instant, no bot
            }
            if startStep > 0 { step = min(startStep, steps.count - 1) }
        }
    }

    private var header: some View {
        HStack {
            Button("‹ Skip tour", action: onSkip)
                .font(ThemeFont.bodyStrong).foregroundStyle(Palette.creamDim).frame(width: 96, alignment: .leading)
            Spacer()
            Text("Quick Tour").font(ThemeFont.serif(17, .bold)).foregroundStyle(Palette.cream)
            Spacer()
            Color.clear.frame(width: 96, height: 1)
        }
        .padding(.horizontal, 16).padding(.top, 8)
    }

    // Interactive board (dice + on-board ✓ come from BoardView) + undo/status/replay.
    private var boardArea: some View {
        VStack(spacing: 6) {
            BoardView(alwaysShowConfirm: true)          // ✓ stays visible, greys until ready
                .environmentObject(game)
                .aspectRatio(1.95, contentMode: .fit)   // tight bounds → bigger board + snug glow
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .tourAnchor(.greenDots)

            HStack(spacing: 12) {
                CircleIconButton(systemName: "arrow.counterclockwise", kind: .wood, size: 38,
                                 disabled: !game.canUndo) { game.undo() }
                    .tourAnchor(.undo)
                Spacer(minLength: 8)
                Text(game.status).font(ThemeFont.caption).foregroundStyle(Palette.creamDim)
                    .lineLimit(1)
                Spacer(minLength: 8)
                Button { game.replayLastMove() } label: {
                    Label(game.lastMoveDice.map { "Replay \($0.0)-\($0.1)" } ?? "Replay",
                          systemImage: "play.circle")
                }
                .buttonStyle(GhostPillButtonStyle(disabled: !game.canReplay))
                .disabled(!game.canReplay)
                .tourAnchor(.replay)
            }
            .padding(.horizontal, 4)
        }
    }

    private var topRightChrome: some View {
        VStack(alignment: .trailing, spacing: 8) {
            HStack(spacing: 9) {
                CircleIconButton(systemName: "flag.fill", kind: .resign, disabled: !game.canOffer) {
                    menuResign.toggle(); menuSettings = false
                }
                .tourAnchor(.resign)
                CircleIconButton(systemName: "gearshape.fill",
                                 kind: theme.id == "cognac" ? .woodLight : .wood) {
                    menuSettings.toggle(); menuResign = false
                }
                .tourAnchor(.settings)
            }
            if menuSettings { settingsPopover }
            if menuResign { resignPopover }
            Spacer()
        }
        .padding(.top, 12).padding(.trailing, 16)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
    }

    private var settingsPopover: some View {
        ParchmentCard {
            VStack(alignment: .leading, spacing: 10) {
                SectionLabel(text: "Animation speed", color: Palette.parchmentInk.opacity(0.6))
                SegmentedChips(options: AnimationSpeed.allCases, selection: $speed,
                               context: .parchment) { $0.rawValue }
                Divider().background(Palette.parchmentEdge.opacity(0.25))
                Button { menuSettings = false; onSkip() } label: {
                    Label("Quit tutorial", systemImage: "house")
                        .font(ThemeFont.bodyStrong).foregroundStyle(Palette.destructiveInk)
                }
                .buttonStyle(.plain)
            }
        }
        .transition(.opacity)
    }

    // Illustrative only — the options don't fire (resigning would end the tutorial
    // game). The popup just shows where resign lives; tapping an option dismisses it.
    private var resignPopover: some View {
        ParchmentCard(width: 200) {
            VStack(alignment: .leading, spacing: 8) {
                SectionLabel(text: "Offer to resign", color: Palette.parchmentInk.opacity(0.6))
                Button { menuResign = false } label: {
                    Label("Single (oin)", systemImage: "flag").font(ThemeFont.bodyStrong)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.vertical, 8).padding(.horizontal, 10)
                        .background(RoundedRectangle(cornerRadius: 9).fill(Palette.parchmentEdge.opacity(0.1)))
                }
                .buttonStyle(.plain).foregroundStyle(Palette.parchmentInk)
                Button { menuResign = false } label: {
                    Label("Mars (double)", systemImage: "flag.checkered").font(ThemeFont.bodyStrong)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.vertical, 8).padding(.horizontal, 10)
                        .background(RoundedRectangle(cornerRadius: 9).fill(Palette.destructiveInk.opacity(0.14)))
                }
                .buttonStyle(.plain).foregroundStyle(Palette.destructiveInk)
            }
        }
        .transition(.opacity)
    }

    private var coachCard: some View {
        VStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 8) {
                Text("FAST TOUR · \(step + 1) / \(steps.count)")
                    .font(.system(size: 10, weight: .heavy)).tracking(0.5).foregroundStyle(Palette.accentBottom)
                Text(current.title).font(ThemeFont.serif(18, .bold)).foregroundStyle(Palette.parchmentInk)
                Text(current.body).font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(Color(hex: 0x5A4426)).fixedSize(horizontal: false, vertical: true)

                HStack(spacing: 6) {
                    ForEach(0..<steps.count, id: \.self) { i in
                        Circle().fill(i == step ? Palette.accentBottom : Palette.parchmentEdge.opacity(0.3))
                            .frame(width: 7, height: 7)
                    }
                }
                .padding(.top, 2)

                HStack(spacing: 8) {
                    if step > 0 {
                        Button("Back") { withAnimation { step -= 1 } }
                            .font(ThemeFont.bodyStrong).foregroundStyle(Color(hex: 0x6E4326))
                            .padding(.vertical, 9).padding(.horizontal, 14)
                            .background(RoundedRectangle(cornerRadius: 11).fill(Palette.parchmentEdge.opacity(0.12)))
                            .buttonStyle(.plain)
                    }
                    if step < steps.count - 1 {
                        Button { withAnimation { step += 1 } } label: {
                            Text("Next ›").frame(maxWidth: .infinity)
                        }
                        .font(ThemeFont.buttonLabel).foregroundStyle(.white)
                        .padding(.vertical, 10)
                        .background(RoundedRectangle(cornerRadius: 11).fill(Grad.cta))
                        .buttonStyle(.plain)
                    } else {
                        Button(action: onStartPlaying) {
                            Label("Start playing", systemImage: "play.fill").frame(maxWidth: .infinity)
                        }
                        .font(ThemeFont.buttonLabel).foregroundStyle(.white)
                        .padding(.vertical, 10)
                        .background(RoundedRectangle(cornerRadius: 11).fill(Grad.confirm))
                        .buttonStyle(.plain)
                    }
                }
                .padding(.top, 2)
            }
            .padding(14)
            .background(RoundedRectangle(cornerRadius: 16).fill(Grad.parchment))
            .shadow(color: .black.opacity(0.5), radius: 12, y: 8)

            Button { withAnimation { step = 0 } } label: {
                Label("Restart tour", systemImage: "arrow.counterclockwise").font(ThemeFont.micro)
            }
            .foregroundStyle(Palette.muted).buttonStyle(.plain)
        }
    }

    private func glowRect(_ t: TourTarget, _ anchors: [TourTarget: Anchor<CGRect>], _ geo: GeometryProxy) -> CGRect? {
        func bounds(_ k: TourTarget) -> CGRect? { anchors[k].map { geo[$0] } }
        switch t {
        case .greenDots:
            return bounds(.greenDots)
        case .dice:
            guard let b = bounds(.greenDots) else { return nil }
            return CGRect(x: b.midX - 58, y: b.midY - 30, width: 116, height: 60)
        case .confirm:
            guard let b = bounds(.greenDots) else { return nil }
            return CGRect(x: b.midX - 34, y: b.midY + b.height * 0.19 - 21, width: 68, height: 42)
        default:
            return bounds(t)
        }
    }
}

/// A pulsing accent glow ring drawn over the highlighted element.
private struct TourGlow: View {
    var cornerRadius: CGFloat = 16
    @State private var pulse = false
    var body: some View {
        RoundedRectangle(cornerRadius: cornerRadius)
            .stroke(Palette.accentSoft, lineWidth: 3)
            .shadow(color: Palette.accentSoft.opacity(0.8), radius: pulse ? 16 : 8)
            .opacity(pulse ? 1 : 0.75)
            .onAppear { withAnimation(.easeInOut(duration: 0.9).repeatForever(autoreverses: true)) { pulse = true } }
    }
}
