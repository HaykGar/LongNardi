import SwiftUI

/// Post-game review: eval graph from the reviewed player's perspective, their
/// biggest blunder, a scrubber through every position, and a jump into the
/// analyzer (presented over the review, so scrolling resumes on return).
struct GameReviewView: View {
    @ObservedObject var review: GameReview
    @EnvironmentObject var analyze: AnalyzeGame
    var autoOpen = false        // dev: open the analyzer once analysis is ready
    var onExit: () -> Void

    @State private var index = 0
    @State private var showAnalyzer = false

    var body: some View {
        VStack(spacing: 8) {
            HStack {
                Button("Done") { onExit() }.font(.callout)
                Spacer()
                Text("Game Review").font(.headline)
                Spacer()
                Color.clear.frame(width: 44, height: 1)
            }.padding(.horizontal)

            if !review.ready {
                Spacer()
                ProgressView("Analyzing game…")
                Spacer()
            } else {
                content
            }
        }
        .padding(.top, 8)
        .task { await review.compute(); if autoOpen { openAnalyzer() } }
        .fullScreenCover(isPresented: $showAnalyzer) {
            AnalyzeScreen(onExit: { showAnalyzer = false }).environmentObject(analyze)
        }
    }

    private func openAnalyzer() {
        // Analyze the move that PRODUCED the position shown at `index`: open the
        // position just BEFORE it (points[index-1]) with that turn's actual roll,
        // so the user can re-decide the move rather than land on its result.
        let i = max(0, index - 1)
        guard let pt = review.points[safe: i] else { return }
        analyze.openForReview(board: pt.board, side: pt.sideToMove, anchor: review.reviewSide,
                              gameDice: review.gameDiceFrom(i), gameLine: review.gameBoardsFrom(i))
        showAnalyzer = true
    }

    @ViewBuilder
    private var content: some View {
        let pt = review.points[safe: index]

        EvalGraph(values: review.points.map { $0.evalReview },
                  blunderIndex: review.biggestBlunder, currentIndex: index)
            .frame(height: 96).padding(.horizontal)

        if let b = review.biggestBlunder, let bp = review.points[safe: b], let d = bp.blunderDelta {
            Button { index = b } label: {
                HStack(spacing: 6) {
                    Image(systemName: "exclamationmark.triangle.fill").foregroundColor(.orange)
                    Text("Biggest blunder: move \(b) · −\(String(format: "%.2f", d))")
                        .font(.caption.bold())
                    Spacer()
                    Text("tap").font(.caption2).foregroundStyle(.secondary)
                }
                .padding(.vertical, 6).padding(.horizontal, 12)
                .background(RoundedRectangle(cornerRadius: 8).fill(Color.orange.opacity(0.12)))
            }
            .buttonStyle(.plain).padding(.horizontal)
        }

        // Whole-game dice totals (sum of both dice on every roll, per colour).
        HStack(spacing: 6) {
            Image(systemName: "dice.fill").foregroundStyle(.secondary)
            Text("Dice rolled — White \(review.whiteDiceTotal) · Black \(review.blackDiceTotal)")
                .font(.caption.bold())
            Spacer()
        }
        .padding(.vertical, 6).padding(.horizontal, 12)
        .background(RoundedRectangle(cornerRadius: 8).fill(Color(.systemGray6)))
        .padding(.horizontal)

        BoardCanvas(board: pt?.board ?? [], flipped: review.reviewSide, selected: nil,
                    flights: [], onTap: { _, _ in })
            .padding(.horizontal, 6)

        moveCaption(pt)

        // Scrubber
        HStack(spacing: 14) {
            Button { index = max(0, index - 1) } label: { Image(systemName: "chevron.left") }
                .disabled(index == 0)
            Slider(value: Binding(get: { Double(index) },
                                  set: { index = Int($0.rounded()) }),
                   in: 0...Double(max(1, review.points.count - 1)), step: 1)
            Button { index = min(review.points.count - 1, index + 1) } label: { Image(systemName: "chevron.right") }
                .disabled(index >= review.points.count - 1)
        }.padding(.horizontal)

        Button { openAnalyzer() } label: {
            Label("Open in Analyzer", systemImage: "scope").frame(maxWidth: .infinity)
        }
        .buttonStyle(.borderedProminent).padding(.horizontal, 40).padding(.top, 2)

        Spacer(minLength: 0)
    }

    @ViewBuilder
    private func moveCaption(_ pt: ReviewPoint?) -> some View {
        VStack(spacing: 2) {
            if let pt {
                if let mover = pt.mover, let dice = pt.dice {
                    Text("Move \(index): \(mover ? "Black" : "White") \(dice.0)-\(dice.1) · \(pt.label)")
                        .font(.callout)
                } else {
                    Text("Start position").font(.callout)
                }
                HStack(spacing: 10) {
                    Text("Eval (\(review.reviewSideName)): " + (pt.evalReview.map { String(format: "%+.3f", $0) } ?? "—"))
                        .font(.caption.monospacedDigit())
                        .foregroundColor((pt.evalReview ?? 0) >= 0 ? .green : .red)
                    if let d = pt.blunderDelta, d > 0.005, let best = pt.bestEval, let played = pt.playedEval {
                        Text("best \(String(format: "%+.2f", best)) vs played \(String(format: "%+.2f", played))")
                            .font(.caption2).foregroundStyle(.secondary)
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, minHeight: 30)
    }
}

/// Minimal line graph of per-ply evals in [-2, +2] with a zero line, the biggest
/// blunder marked, and the current scrubber position highlighted.
struct EvalGraph: View {
    let values: [Float?]
    let blunderIndex: Int?
    let currentIndex: Int

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            let n = max(1, values.count - 1)
            let x: (Int) -> CGFloat = { CGFloat($0) / CGFloat(n) * w }
            let y: (Float) -> CGFloat = { h * (1 - CGFloat((max(-2, min(2, $0)) + 2) / 4)) }

            ZStack {
                RoundedRectangle(cornerRadius: 6).fill(Color(.systemGray6))
                // zero line
                Path { p in p.move(to: CGPoint(x: 0, y: h / 2)); p.addLine(to: CGPoint(x: w, y: h / 2)) }
                    .stroke(Color(.systemGray3), style: .init(lineWidth: 1, dash: [3, 3]))
                // eval line
                Path { p in
                    var started = false
                    for (i, v) in values.enumerated() {
                        guard let v else { started = false; continue }
                        let pt = CGPoint(x: x(i), y: y(v))
                        if started { p.addLine(to: pt) } else { p.move(to: pt); started = true }
                    }
                }.stroke(Color.accentColor, lineWidth: 2)
                // biggest blunder marker
                if let b = blunderIndex, let v = values[safe: b] ?? nil {
                    Circle().fill(Color.orange).frame(width: 8, height: 8).position(x: x(b), y: y(v))
                }
                // current position
                Path { p in p.move(to: CGPoint(x: x(currentIndex), y: 0)); p.addLine(to: CGPoint(x: x(currentIndex), y: h)) }
                    .stroke(Color.primary.opacity(0.5), lineWidth: 1)
            }
        }
    }
}

extension Array {
    subscript(safe i: Int) -> Element? { indices.contains(i) ? self[i] : nil }
}
