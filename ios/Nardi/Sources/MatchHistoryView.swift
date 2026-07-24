import SwiftUI

/// Finished games (mockup 1e): a scrollable list with "Show more" revealing the
/// next 10, tap a row to open its review. No delete anywhere (per the v2 plan).
/// Colours come from the shared `Palette`.
struct HistoryView: View {
    @ObservedObject var store: MatchStore
    var onReview: (SavedMatch) -> Void
    var onBack: () -> Void

    @State private var shown = 10

    private var visible: [SavedMatch] { Array(store.matches.prefix(shown)) }
    private var canShowMore: Bool { shown < store.matches.count }
    private var showMoreCount: Int { max(0, min(10, store.matches.count - shown)) }

    var body: some View {
        ZStack {
            RadialBackground(surface: .history)
            VStack(spacing: 12) {
                header
                if store.matches.isEmpty {
                    emptyState
                } else {
                    ScrollView {
                        LazyVStack(spacing: 8) {
                            ForEach(visible) { match in
                                Button { onReview(match) } label: { MatchRow(match: match) }
                                    .buttonStyle(.plain)
                            }
                            if canShowMore {
                                Button { withAnimation { shown = min(shown + 10, store.matches.count) } } label: {
                                    Text("Show \(showMoreCount) more")
                                        .font(ThemeFont.bodyStrong).foregroundStyle(Palette.accentSoft)
                                        .frame(maxWidth: .infinity).padding(.vertical, 11)
                                        .background(RoundedRectangle(cornerRadius: 12).fill(Palette.fillWarm.opacity(0.06)))
                                }
                                .buttonStyle(.plain).padding(.top, 2)
                            }
                        }
                        .frame(maxWidth: 620)
                        .frame(maxWidth: .infinity)
                        .padding(.horizontal, 24).padding(.bottom, 16)
                    }
                }
            }
        }
    }

    private var header: some View {
        HStack {
            Button(action: onBack) { Label("Home", systemImage: "chevron.left") }
                .buttonStyle(GhostPillButtonStyle())
            Spacer()
            Text("History").font(ThemeFont.screenTitle).foregroundStyle(Palette.cream)
            Spacer()
            Text("\(store.matches.count) \(store.matches.count == 1 ? "game" : "games")")
                .font(ThemeFont.caption).foregroundStyle(Palette.muted)
                .frame(minWidth: 70, alignment: .trailing)
        }
        .padding(.horizontal, 20).padding(.top, 14)
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Spacer()
            Image(systemName: "clock.arrow.circlepath")
                .font(.system(size: 44)).foregroundStyle(Palette.muted)
            Text("No games yet").font(.headline).foregroundStyle(Palette.cream)
            Text("Finish a game and it'll appear here.\nTap any game to review it or open positions in the analyzer.")
                .font(.subheadline).foregroundStyle(Palette.creamDim)
                .multilineTextAlignment(.center).padding(.horizontal, 40)
            Spacer()
        }
    }
}

/// One row: result chip + opponent / outcome / metadata + a review affordance.
private struct MatchRow: View {
    let match: SavedMatch

    var body: some View {
        HStack(spacing: 14) {
            ResultChip(won: match.reviewedSideWon, mars: match.mars)
            VStack(alignment: .leading, spacing: 3) {
                Text(match.opponentLabel).font(.system(size: 15, weight: .bold)).foregroundStyle(Palette.cream)
                Text(outcomeText).font(.subheadline)
                    .foregroundColor(match.reviewedSideWon ? Palette.winText : Palette.lossText)
                Text("\(match.moveCount) moves · \(Self.dateFmt.string(from: match.date))")
                    .font(ThemeFont.micro).foregroundStyle(Palette.muted)
            }
            Spacer()
            Image(systemName: "chart.line.uptrend.xyaxis")
                .foregroundStyle(Palette.sectionLabel).font(.callout)
        }
        .padding(.vertical, 10).padding(.horizontal, 14)
        .background(RoundedRectangle(cornerRadius: 14).fill(Palette.rowFill))
        .overlay(RoundedRectangle(cornerRadius: 14).stroke(Palette.rowStroke, lineWidth: 1))
    }

    private var outcomeText: String {
        let verb = match.reviewedSideWon ? "Won" : "Lost"
        let mars = match.mars ? " (mars)" : ""
        return "\(verb) as \(match.reviewSideName)\(mars)"
    }

    static let dateFmt: DateFormatter = {
        let f = DateFormatter(); f.dateStyle = .medium; f.timeStyle = .short
        return f
    }()
}

/// Colour-coded W/L badge with a gradient; a star marks a mars (gammon).
private struct ResultChip: View {
    let won: Bool
    let mars: Bool
    var body: some View {
        ZStack(alignment: .topTrailing) {
            RoundedRectangle(cornerRadius: 12)
                .fill(LinearGradient(colors: won ? Palette.winFill : Palette.lossFill,
                                     startPoint: .top, endPoint: .bottom))
                .frame(width: 46, height: 46)
                .overlay(Text(won ? "W" : "L").font(.title3.bold()).foregroundColor(.white))
            if mars {
                Image(systemName: "star.fill").font(.system(size: 12)).foregroundColor(Palette.marsStar)
                    .offset(x: 5, y: -5)
            }
        }
    }
}
