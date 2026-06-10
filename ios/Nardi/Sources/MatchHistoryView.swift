import SwiftUI

/// Scrollable list of finished games. Tapping a match re-runs game review on it
/// (from there the user can open any position in the analyzer). Swipe to delete.
///
/// Styling is intentionally vector/SwiftUI-only (SF Symbols, gradients, system
/// colours) and factored into small pieces — `ResultChip`, `MatchRow`, the colour
/// constants — so a designer can restyle it without unpicking layout from logic.
struct MatchHistoryView: View {
    @ObservedObject var store: MatchStore
    var onReview: (SavedMatch) -> Void

    @State private var confirmClear = false

    var body: some View {
        VStack(spacing: 0) {
            if store.matches.isEmpty {
                emptyState
            } else {
                header
                List {
                    ForEach(store.matches) { match in
                        Button { onReview(match) } label: { MatchRow(match: match) }
                            .buttonStyle(.plain)
                            .listRowInsets(.init(top: 6, leading: 16, bottom: 6, trailing: 16))
                            .listRowSeparator(.hidden)
                            // Delete one game: swipe, or long-press for the menu.
                            .swipeActions(edge: .trailing) {
                                Button(role: .destructive) { store.delete(match) } label: {
                                    Label("Delete", systemImage: "trash")
                                }
                            }
                            .contextMenu {
                                Button { onReview(match) } label: { Label("Review", systemImage: "chart.line.uptrend.xyaxis") }
                                Button(role: .destructive) { store.delete(match) } label: {
                                    Label("Delete game", systemImage: "trash")
                                }
                            }
                    }
                }
                .listStyle(.plain)
            }
        }
    }

    private var header: some View {
        HStack {
            Text("\(store.matches.count) \(store.matches.count == 1 ? "match" : "matches")")
                .font(.caption).foregroundStyle(.secondary)
            Spacer()
            Button(role: .destructive) { confirmClear = true } label: {
                Label("Clear", systemImage: "trash").font(.caption)
            }
            .confirmationDialog("Delete all saved matches?", isPresented: $confirmClear, titleVisibility: .visible) {
                Button("Delete All", role: .destructive) { store.clear() }
                Button("Cancel", role: .cancel) {}
            }
        }
        .padding(.horizontal, 20).padding(.bottom, 4)
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Spacer()
            Image(systemName: "clock.arrow.circlepath")
                .font(.system(size: 44)).foregroundStyle(.secondary)
            Text("No matches yet").font(.headline)
            Text("Finish a game and it'll appear here.\nTap any match to review it or open positions in the analyzer.")
                .font(.subheadline).foregroundStyle(.secondary)
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
                Text(match.opponentLabel).font(.headline)
                Text(outcomeText).font(.subheadline)
                    .foregroundColor(match.reviewedSideWon ? Palette.winText : Palette.lossText)
                Text("\(match.moveCount) moves · \(Self.dateFmt.string(from: match.date))")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            Image(systemName: "chart.line.uptrend.xyaxis")
                .foregroundStyle(.secondary).font(.callout)
        }
        .padding(.vertical, 10).padding(.horizontal, 14)
        .background(RoundedRectangle(cornerRadius: 14).fill(Color(.secondarySystemBackground)))
    }

    private var outcomeText: String {
        let verb = match.reviewedSideWon ? "Won" : "Lost"
        let mars = match.mars ? " (mars)" : ""
        return "\(verb) as \(match.reviewSideName)\(mars)"
    }

    static let dateFmt: DateFormatter = {
        let f = DateFormatter()
        f.dateStyle = .medium; f.timeStyle = .short
        return f
    }()
}

/// Colour-coded W/L badge with a faint vertical gradient; a small star marks a
/// mars (gammon). Self-contained so the palette is the only thing to tweak.
private struct ResultChip: View {
    let won: Bool
    let mars: Bool

    var body: some View {
        ZStack(alignment: .topTrailing) {
            RoundedRectangle(cornerRadius: 12)
                .fill(LinearGradient(colors: won ? Palette.winFill : Palette.lossFill,
                                     startPoint: .top, endPoint: .bottom))
                .frame(width: 46, height: 46)
                .overlay(
                    Text(won ? "W" : "L")
                        .font(.title3.bold()).foregroundColor(.white)
                )
            if mars {
                Image(systemName: "star.fill")
                    .font(.system(size: 12)).foregroundColor(.yellow)
                    .offset(x: 5, y: -5)
            }
        }
    }
}

/// Single source of truth for history colours — restyle here.
private enum Palette {
    static let winFill  = [Color.green, Color.green.opacity(0.7)]
    static let lossFill = [Color.red.opacity(0.85), Color.red.opacity(0.6)]
    static let winText  = Color.green
    static let lossText = Color.red
}
