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
            // Device-adaptive: side margins, gap and the two column widths all scale
            // with the screen, so the pair never smushes on a small phone nor sprawls
            // on an iPad (mirrors HomeView).
            GeometryReader { geo in
                let w = geo.size.width
                let sidePad = max(28, w * 0.05)
                let gap = max(20, w * 0.03)
                let avail = max(0, w - sidePad * 2 - gap)
                let leftW = min(340, avail * 0.44)
                let rightW = min(420, avail * 0.56)

                HStack(alignment: .center, spacing: gap) {
                    VStack(alignment: .leading, spacing: 14) {
                        Text("How to play").font(ThemeFont.serif(40, .bold)).foregroundStyle(Palette.cream)
                            .minimumScaleFactor(0.7).lineLimit(1)
                        Text("Two ways in. Already know Long Nardi? Take the quick tour of the controls. New to the game? The full lesson walks the rules with you.")
                            .font(ThemeFont.bodyText).foregroundStyle(Palette.creamDim)
                            .fixedSize(horizontal: false, vertical: true)
                        Button(action: onBack) { Label("Back to Home", systemImage: "chevron.left") }
                            .font(ThemeFont.caption).foregroundStyle(Palette.muted).buttonStyle(.plain)
                            .padding(.top, 4)
                    }
                    .frame(width: leftW, alignment: .leading)

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
                    .frame(width: rightW)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .padding(.horizontal, sidePad)
            }
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

private struct TourStep {
    let target: PlayControl        // which REAL control this step glows
    let title: String
    let body: String
}

/// A guided walkthrough over a REAL, fully-interactive game (a tutorial-local
/// NardiGame). It renders the SAME shared `PlaySurface` as the real play screen —
/// given a smaller frame so the board self-fits smaller and the coach card sits
/// beside it — so the tour can never drift from the game layout. Every coach-mark
/// glow tracks the TRUE control via the anchors `PlaySurface` publishes
/// (`publishAnchors`), so there are no hardcoded rects to re-sync: wherever a
/// control lands and whatever its outline is, that is exactly what gets ringed.
/// The player lands on their first turn able to tap checkers, reorder dice, undo,
/// confirm, and replay exactly as in a normal game. Next / Back / Restart never
/// gate play. Skip (→ chooser), Start playing (→ setup).
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
        .init(target: .board, title: "Tap to move",
              body: "Your opponent just moved. A green dot marks every checker you can move — tap one to play it with your dice. If possible, the checker will move by the dice on the left, otherwise it will move by the dice on the right."),
        .init(target: .dice, title: "Your dice",
              body: "What if I want to move by the other die first? These are your dice. Tap either one to switch which value a tapped checker tries first."),
        .init(target: .confirm, title: "Confirm your turn",
              body: "Once your turn is complete, the green ✓ appears — tap it to lock in your move."),
        .init(target: .undo, title: "Undo",
              body: "Changed your mind? Undo takes back a hop before you confirm."),
        .init(target: .replay, title: "Replay",
              body: "Replay re-plays your opponent's last move — tap it to watch again."),
        .init(target: .settings, title: "Settings",
              body: "Animation speed and quitting to the menu live here."),
        .init(target: .resign, title: "Resign",
              body: "Offer to resign at any time — a single (oin) or a double (mars). If a computer opponent evaluates its position as strong enough, it may decline your offer of oin, and only accept a mars."),
    ]

    private var current: TourStep { steps[min(step, steps.count - 1)] }

    var body: some View {
        ZStack(alignment: .top) {
            RadialBackground(surface: theme.surface)

            VStack(spacing: 4) {
                header
                GeometryReader { geo in
                    let coachW = min(240, max(206, geo.size.width * 0.26))
                    HStack(alignment: .center, spacing: 12) {
                        // The REAL play layout (single source of truth). A smaller
                        // frame makes the board self-fit smaller (BoardCanvas adapts —
                        // no scaleEffect, so published anchors stay in true screen
                        // coordinates). publishAnchors → glow tracks real controls.
                        // Tighter bands + a compact replay reclaim width so the board
                        // is as large as fits without touching the chrome or borders.
                        PlaySurface(menuSettings: $menuSettings, menuResign: $menuResign,
                                    alwaysShowConfirm: true, publishAnchors: true, drawBackground: false,
                                    leftBand: 60, rightBand: 50, compactReplay: true,
                                    settingsMenu: { settingsPopover }, resignMenu: { resignPopover })
                            .environmentObject(game)
                            .frame(maxWidth: .infinity, maxHeight: .infinity)
                        coachCard
                            .frame(width: coachW)
                            .frame(maxHeight: geo.size.height)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
                .padding(.horizontal, 10).padding(.bottom, 8)
            }
        }
        .environmentObject(game)
        .overlayPreferenceValue(PlayAnchorKey.self) { anchors in
            GeometryReader { geo in
                if let rect = glowRect(current.target, anchors, geo) {
                    TourGlow(cornerRadius: current.target == .board ? 14 : 22)
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
            Button(action: onSkip) {
                Text("‹ Skip tour")
                    .font(ThemeFont.bodyStrong).foregroundStyle(Palette.creamDim)
                    .frame(width: 96, height: 40, alignment: .leading)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            Spacer()
            Text("Quick Tour").font(ThemeFont.serif(17, .bold)).foregroundStyle(Palette.cream)
            Spacer()
            Color.clear.frame(width: 96, height: 1)
        }
        .padding(.horizontal, 16).padding(.top, 8)
    }

    private var settingsPopover: some View {
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
        .padding(14).frame(width: 236).foregroundStyle(Palette.parchmentInk)
        .background(Grad.parchment)
    }

    // Illustrative only — the options don't fire (resigning would end the tutorial
    // game). The popup just shows where resign lives; tapping an option dismisses it.
    private var resignPopover: some View {
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
        .padding(14).frame(width: 220).foregroundStyle(Palette.parchmentInk)
        .background(Grad.parchment)
    }

    private var coachCard: some View {
        VStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 8) {
                Text("FAST TOUR · \(step + 1) / \(steps.count)")
                    .font(.system(size: 10, weight: .heavy)).tracking(0.5).foregroundStyle(Palette.accentBottom)
                Text(current.title).font(ThemeFont.serif(18, .bold)).foregroundStyle(Palette.parchmentInk)
                    .lineLimit(1).minimumScaleFactor(0.8)
                // Adaptive: wraps freely, then scales down to fit the space the card
                // is given rather than growing it unbounded — so a long step never
                // makes the card too big or pushes it off-screen.
                Text(current.body).font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(Color(hex: 0x5A4426))
                    .minimumScaleFactor(0.6)
                    .frame(maxWidth: .infinity, alignment: .leading)

                HStack(spacing: 6) {
                    ForEach(0..<steps.count, id: \.self) { i in
                        Circle().fill(i == step ? Palette.accentBottom : Palette.parchmentEdge.opacity(0.3))
                            .frame(width: 7, height: 7)
                    }
                }
                .padding(.top, 2)

                HStack(spacing: 8) {
                    if step > 0 {
                        // Clamp, don't decrement: the index must reflect the slide it
                        // IS, so rapid taps can never drive it out of range.
                        // Padding/background live INSIDE the label so the whole
                        // pill is the hit area, not just the glyphs.
                        Button { withAnimation { step = max(step - 1, 0) } } label: {
                            Text("Back")
                                .font(ThemeFont.bodyStrong).foregroundStyle(Color(hex: 0x6E4326))
                                .padding(.vertical, 9).padding(.horizontal, 14)
                                .background(RoundedRectangle(cornerRadius: 11).fill(Palette.parchmentEdge.opacity(0.12)))
                                .contentShape(RoundedRectangle(cornerRadius: 11))
                        }
                        .buttonStyle(.plain)
                    }
                    if step < steps.count - 1 {
                        // Clamp to the last slide so fast taps can't overshoot to "8/7".
                        Button { withAnimation { step = min(step + 1, steps.count - 1) } } label: {
                            Text("Next ›")
                                .font(ThemeFont.buttonLabel).foregroundStyle(.white)
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 10)
                                .background(RoundedRectangle(cornerRadius: 11).fill(Grad.cta))
                                .contentShape(RoundedRectangle(cornerRadius: 11))
                        }
                        .buttonStyle(.plain)
                    } else {
                        Button(action: onStartPlaying) {
                            Label("Start playing", systemImage: "play.fill")
                                .font(ThemeFont.buttonLabel).foregroundStyle(.white)
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 10)
                                .background(RoundedRectangle(cornerRadius: 11).fill(Grad.confirm))
                                .contentShape(RoundedRectangle(cornerRadius: 11))
                        }
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

    /// Every glow reads the REAL control's published anchor — no magic numbers, so
    /// the ring is always tight to whatever the control actually is, wherever the
    /// shared layout puts it.
    private func glowRect(_ t: PlayControl, _ anchors: [PlayControl: Anchor<CGRect>], _ geo: GeometryProxy) -> CGRect? {
        anchors[t].map { geo[$0] }
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
