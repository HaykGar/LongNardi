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

                ZStack(alignment: .bottomLeading) {
                    Color.clear
                    HStack(alignment: .center, spacing: gap) {
                        leftBlock.frame(width: leftW, alignment: .leading)
                        rightCards.frame(width: rightW)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)   // centres the pair
                    .padding(.horizontal, sidePad)

                    tutorialRibbon
                        .padding(.leading, sidePad).padding(.bottom, 18)
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

    // Quiet tutorial ribbon → the tutorial chooser.
    private var tutorialRibbon: some View {
        Button(action: onTutorial) {
            HStack(spacing: 8) {
                Image(systemName: "sparkles")
                Text("New to Nardi?").font(ThemeFont.caption)
                Text("Take the tutorial ›").font(.system(size: 12, weight: .heavy))
                    .foregroundStyle(Palette.accentSoft)
            }
        }
        .buttonStyle(RibbonButtonStyle())
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
