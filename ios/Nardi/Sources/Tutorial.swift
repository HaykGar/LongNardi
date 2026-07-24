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

/// A guided, UI-only walkthrough over a static demo board. No engine involved —
/// the board is a fixed illustrative position; each step glows one control and
/// explains it. Next / Back / Restart, Skip (→ chooser), Start playing (→ setup).
struct TutorialView: View {
    let onSkip: () -> Void
    let onStartPlaying: () -> Void
    var startStep: Int = 0        // dev hook only; users always start at 0
    @AppStorage(BoardTheme.storageKey) private var boardThemeRaw = BoardTheme.green.rawValue
    @State private var step = 0

    private var theme: BoardTheme { BoardTheme(rawValue: boardThemeRaw) ?? .green }

    private let steps: [TourStep] = [
        .init(target: .greenDots, title: "Tap to move",
              body: "A green dot marks every checker you can move. Tap one, and it plays with your dice."),
        .init(target: .dice, title: "Your dice",
              body: "These are your dice. Tap either one to switch which value a tapped checker tries first."),
        .init(target: .confirm, title: "Confirm your turn",
              body: "Once your turn is complete, the green ✓ appears in the middle — tap it to lock in your move."),
        .init(target: .undo, title: "Undo",
              body: "Changed your mind? Undo takes back a hop before you confirm."),
        .init(target: .replay, title: "Replay",
              body: "Replay re-plays your opponent's last move, so you never miss what they did."),
        .init(target: .settings, title: "Settings",
              body: "Animation speed, flipping the board, and quitting to the menu all live here."),
        .init(target: .resign, title: "Resign",
              body: "Offer to resign at any time — a single (oin) or a double (mars)."),
    ]

    // Static demo position (purely illustrative; not engine-validated).
    private let demoBoard: [Int8] = {
        var b = [Int8](repeating: 0, count: 24)
        b[0] = 2; b[3] = 2; b[5] = 3; b[7] = 2; b[10] = 3; b[11] = 3     // white 15
        b[12] = -3; b[15] = -2; b[18] = -3; b[20] = -2; b[22] = -2; b[23] = -3  // black 15
        return b
    }()
    private let demoMask = (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7)     // green dots
    private let demoDice = [DieSpec(value: 5, hasStart: true, isFirst: true),
                            DieSpec(value: 3, hasStart: true)]

    private var current: TourStep { steps[min(step, steps.count - 1)] }

    var body: some View {
        ZStack(alignment: .top) {
            RadialBackground(surface: theme.surface)

            VStack(spacing: 6) {
                header
                HStack(alignment: .center, spacing: 16) {
                    boardArea.frame(maxWidth: .infinity)
                    coachCard.frame(width: 264)
                }
                .padding(.horizontal, 16).padding(.bottom, 10)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }

            // Top-right chrome (decorative, but glow-targetable).
            HStack(spacing: 9) {
                CircleIconButton(systemName: "flag.fill", kind: .resign) {}.tourAnchor(.resign)
                CircleIconButton(systemName: "gearshape.fill",
                                 kind: theme.id == "cognac" ? .woodLight : .wood) {}.tourAnchor(.settings)
            }
            .padding(.top, 12).padding(.trailing, 16)
            .frame(maxWidth: .infinity, alignment: .topTrailing)
        }
        // The glow, positioned over the current step's element.
        .overlayPreferenceValue(TourAnchorKey.self) { anchors in
            GeometryReader { geo in
                if let rect = glowRect(current.target, anchors, geo) {
                    TourGlow()
                        .frame(width: rect.width + 16, height: rect.height + 16)
                        .position(x: rect.midX, y: rect.midY)
                        .allowsHitTesting(false)
                        .animation(.easeInOut(duration: 0.3), value: step)
                }
            }
        }
        .onAppear { if startStep > 0 { step = min(startStep, steps.count - 1) } }
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

    private var boardArea: some View {
        VStack(spacing: 6) {
            BoardCanvas(board: demoBoard, flipped: false, selected: nil,
                        startMask: demoMask, whitePips: 167, blackPips: 167,
                        showPips: true, theme: theme, dice: demoDice, canConfirm: true,
                        onTap: { _, _ in })
                .tourAnchor(.greenDots)   // whole board (tap-to-move / green dots)

            HStack(spacing: 12) {
                CircleIconButton(systemName: "arrow.counterclockwise", kind: .wood, size: 38) {}
                    .tourAnchor(.undo)
                Spacer(minLength: 8)
                Text("Your roll — 5 and 3").font(ThemeFont.caption).foregroundStyle(Palette.creamDim)
                Spacer(minLength: 8)
                Text(verbatim: "▶ Replay 4-2")
                    .font(ThemeFont.bodyStrong).foregroundStyle(Palette.buttonInk)
                    .padding(.horizontal, 16).padding(.vertical, 9)
                    .background(Capsule().fill(Palette.fillWarm.opacity(0.07)))
                    .overlay(Capsule().stroke(Palette.strokeWarm.opacity(0.18), lineWidth: 1))
                    .tourAnchor(.replay)
            }
            .padding(.horizontal, 8)
        }
    }

    private var coachCard: some View {
        VStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 8) {
                Text("FAST TOUR · \(step + 1) / \(steps.count)")
                    .font(.system(size: 10, weight: .heavy)).tracking(0.5).foregroundStyle(Palette.accentBottom)
                Text(current.title).font(ThemeFont.serif(18, .bold)).foregroundStyle(Palette.parchmentInk)
                Text(current.body).font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(Color(hex: 0x5A4426)).fixedSize(horizontal: false, vertical: true)

                // progress dots
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
            return CGRect(x: b.midX - 58, y: b.midY - 32, width: 116, height: 64)
        case .confirm:
            guard let b = bounds(.greenDots) else { return nil }
            return CGRect(x: b.midX - 52, y: b.midY + b.height * 0.19 - 24, width: 104, height: 48)
        default:
            return bounds(t)
        }
    }
}

/// A pulsing accent glow ring drawn over the highlighted element.
private struct TourGlow: View {
    @State private var pulse = false
    var body: some View {
        RoundedRectangle(cornerRadius: 16)
            .stroke(Palette.accentSoft, lineWidth: 3)
            .background(RoundedRectangle(cornerRadius: 16).fill(Palette.accentSoft.opacity(0.12)))
            .shadow(color: Palette.accentSoft.opacity(0.8), radius: pulse ? 16 : 8)
            .opacity(pulse ? 1 : 0.75)
            .onAppear { withAnimation(.easeInOut(duration: 0.9).repeatForever(autoreverses: true)) { pulse = true } }
    }
}
