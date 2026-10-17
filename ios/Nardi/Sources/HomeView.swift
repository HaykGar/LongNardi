import SwiftUI

/// Home hero (mockup 1a): wordmark + record on the left, three action cards on the
/// right, and a quiet "take the tutorial" ribbon (Tutorial is a future feature — the
/// ribbon just shows a brief "coming soon" toast; nothing is built yet).
struct HomeView: View {
    @ObservedObject var store: MatchStore
    let onPlay: () -> Void
    let onAnalyze: () -> Void
    let onHistory: () -> Void
    let onTutorial: () -> Void
    let onSettings: () -> Void
    /// One-time "we share anonymous game data" notice; Home is the only screen
    /// that ever shows it, so it can never sit over a game.
    @State private var showConsentBanner = SharingConsent().showsBanner(onHome: true)

    var body: some View {
        ZStack {
            RadialBackground(surface: .home)

            // Device-adaptive: side margins + the gap scale with the screen, and the
            // two blocks are capped and CENTRED as a pair — so they pull inward off
            // the edges on a phone (iPhone 14) yet spread cleanly on an iPad.
            GeometryReader { geo in
                let w = geo.size.width
                let sidePad = max(28, w * 0.055)
                let gap = max(22, w * 0.038)
                let avail = max(0, w - sidePad * 2 - gap)
                let leftW = min(330, avail * 0.44)
                let rightW = min(420, avail * 0.56)

                // Hero above, bottom bar (ribbon + consent banner) below, in one
                // VStack so the banner takes its own row and never covers a card.
                VStack(spacing: 0) {
                    HStack(alignment: .center, spacing: gap) {
                        leftBlock.frame(width: leftW, alignment: .leading)
                        rightCards.frame(width: rightW)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)   // centres the pair
                    .padding(.horizontal, sidePad)

                    // Settings + Tutorial (left) · Banner (right, while shown).
                    HStack(alignment: .bottom, spacing: gap) {
                        HStack(spacing: 10) {
                            settingsRibbon
                            tutorialRibbon
                        }
                        .fixedSize()
                        Spacer(minLength: 0)
                        if showConsentBanner {
                            consentBanner.frame(maxWidth: leftW + gap)
                                .transition(.opacity)
                        }
                    }
                    .padding(.horizontal, sidePad).padding(.bottom, 18)
                }
            }
        }
    }

    // Left: wordmark, tagline, stats.
    private var leftBlock: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("Long Nardi")
                .font(ThemeFont.wordmark).foregroundStyle(Palette.cream)
                .minimumScaleFactor(0.6).lineLimit(1)
                .padding(.vertical, 6)
            Text("Play the classic tables game against a trained engine — or set up any position and explore the lines.")
                .font(ThemeFont.bodyText).foregroundStyle(Palette.creamDim)
                .fixedSize(horizontal: false, vertical: true)
            HStack(spacing: 26) {
                StatBlock(value: "\(store.wins)", label: "WINS")
                StatBlock(value: "\(store.winRatePercent)%", label: "WIN RATE")
                StatBlock(value: "\(store.currentStreak)", label: "STREAK")
            }
            .padding(.top, 24)
        }
    }

    // Right: the three action cards.
    private var rightCards: some View {
        VStack(spacing: 14) {
            ActionCard(title: "Play", subtitle: "Against the engine, or pass & play",
                       systemImage: "play.fill", kind: .primary, action: onPlay)
            ActionCard(title: "Analyze", subtitle: "Set up a position & run the engine",
                       systemImage: "scope", kind: .secondary, action: onAnalyze)
            ActionCard(title: "History", subtitle: "Review any past game",
                       systemImage: "list.bullet", kind: .secondary, action: onHistory)
        }
    }

    // One-time sharing notice; ✕ dismisses it for good.
    private var consentBanner: some View {
        HStack(alignment: .center, spacing: 10) {
            Image(systemName: "chart.bar.doc.horizontal")
                .foregroundStyle(Palette.accentSoft)
            Text(SharingConsent.bannerCopy)
                .font(ThemeFont.caption).foregroundStyle(Palette.creamDim)
                .fixedSize(horizontal: false, vertical: true)
            Button {
                SharingConsent().dismissBanner()
                withMotion(Motion.out(Motion.Duration.enter)) { showConsentBanner = false }
            } label: {
                Image(systemName: "xmark").font(.system(size: 12, weight: .heavy))
                    .foregroundStyle(Palette.cream)
                    .frame(width: 44, height: 44).contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Dismiss data sharing notice")
        }
        .padding(.leading, 14).padding(.trailing, 0).padding(.vertical, 0)
        .background(RoundedRectangle(cornerRadius: 14).fill(Grad.homeNavCard)
            .overlay(RoundedRectangle(cornerRadius: 14).stroke(Palette.strokeWarm.opacity(0.18), lineWidth: 1)))
    }

    // Quiet tutorial ribbon → the tutorial chooser.
    private var tutorialRibbon: some View {
        Button(action: onTutorial) {
            HStack(spacing: 8) {
                Image(systemName: "sparkles")
                Text("Take the tutorial ›").font(.system(size: 12, weight: .heavy))
                    .foregroundStyle(Palette.accentSoft)
            }
        }
        .buttonStyle(RibbonButtonStyle())
    }

    // Global Settings (sharing opt-out + animation speed), same ribbon family.
    private var settingsRibbon: some View {
        Button(action: onSettings) {
            HStack(spacing: 8) {
                Image(systemName: "gearshape")
                Text("Settings").font(.system(size: 12, weight: .heavy))
            }
        }
        .buttonStyle(RibbonButtonStyle())
    }
}

/// Global Settings, reached from Home. Binds to the SAME UserDefaults keys as the
/// in-game Settings popover, so both stay in sync.
struct SettingsView: View {
    let onBack: () -> Void
    @AppStorage(AnimationSpeed.storageKey) private var speed: AnimationSpeed = .high
    @AppStorage(GameUploader.sharingDefaultsKey) private var shareGameData = true

    var body: some View {
        ZStack {
            RadialBackground(surface: .home)
            GeometryReader { geo in
                let w = geo.size.width
                let sidePad = max(28, w * 0.05)
                let gap = max(20, w * 0.03)
                let avail = max(0, w - sidePad * 2 - gap)
                let leftW = min(340, avail * 0.44)
                let rightW = min(420, avail * 0.56)

                HStack(alignment: .center, spacing: gap) {
                    VStack(alignment: .leading, spacing: 14) {
                        Text("Settings").font(ThemeFont.serif(40, .bold)).foregroundStyle(Palette.cream)
                            .minimumScaleFactor(0.7).lineLimit(1)
                        Text("These apply everywhere. You can also change them from Settings during a game.")
                            .font(ThemeFont.bodyText).foregroundStyle(Palette.creamDim)
                            .fixedSize(horizontal: false, vertical: true)
                        Button(action: onBack) { Label("Back to Home", systemImage: "chevron.left") }
                            .font(ThemeFont.caption).foregroundStyle(Palette.muted).buttonStyle(.plain)
                            .padding(.top, 4)
                    }
                    .frame(width: leftW, alignment: .leading)

                    VStack(alignment: .leading, spacing: 18) {
                        VStack(alignment: .leading, spacing: 10) {
                            SectionLabel(text: "Animation speed", color: Palette.creamDim)
                            SegmentedChips(options: AnimationSpeed.allCases, selection: $speed,
                                           context: .onDark) { $0.rawValue }
                        }
                        Divider().background(Palette.strokeWarm.opacity(0.2))
                        Toggle(isOn: $shareGameData) {
                            VStack(alignment: .leading, spacing: 2) {
                                Text("Share anonymous game data").font(ThemeFont.bodyStrong)
                                    .foregroundStyle(Palette.cream)
                                Text("Finished games help improve Nardi.")
                                    .font(ThemeFont.caption).foregroundStyle(Palette.creamDim)
                                    .fixedSize(horizontal: false, vertical: true)
                            }
                        }
                        .toggleStyle(.flip)
                    }
                    .padding(22)
                    .frame(width: rightW, alignment: .leading)
                    .background(RoundedRectangle(cornerRadius: 18).fill(Grad.homeNavCard)
                        .overlay(RoundedRectangle(cornerRadius: 18).stroke(Palette.strokeWarm.opacity(0.18), lineWidth: 1)))
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .padding(.horizontal, sidePad)
            }
        }
    }
}

/// A Home nav card. `.primary` = the cognac gradient Play card; `.secondary` = the
/// tan-bordered translucent Analyze / History cards.
private struct ActionCard: View {
    enum Kind { case primary, secondary }
    let title: String
    let subtitle: String
    let systemImage: String
    let kind: Kind
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 16) {
                Image(systemName: systemImage)
                    .font(.system(size: 22, weight: .semibold))
                    .foregroundStyle(kind == .primary ? .white : Palette.accentSoft)
                    .frame(width: 28)
                VStack(alignment: .leading, spacing: 2) {
                    Text(title).font(ThemeFont.cardTitle)
                        .foregroundStyle(kind == .primary ? .white : Palette.cream)
                    Text(subtitle).font(ThemeFont.caption)
                        .foregroundStyle(kind == .primary ? Color.white.opacity(0.82) : Palette.creamDim)
                }
                Spacer(minLength: 0)
            }
            .padding(.horizontal, 22).padding(.vertical, kind == .primary ? 20 : 18)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(background)
        }
        .buttonStyle(.plain)
    }

    @ViewBuilder private var background: some View {
        switch kind {
        case .primary:
            RoundedRectangle(cornerRadius: 18).fill(Grad.cta)
                .shadow(color: .black.opacity(0.45), radius: 8, y: 8)
        case .secondary:
            RoundedRectangle(cornerRadius: 18).fill(Grad.homeNavCard)
                .overlay(RoundedRectangle(cornerRadius: 18).stroke(Palette.strokeWarm.opacity(0.18), lineWidth: 1))
        }
    }
}
