import SwiftUI

/// Post-game review (mockup 2c): board on the left; eval graph, whole-game dice
/// totals, move caption, a scrubber, and "Open in Analyzer" on the right. Jumping
/// into the analyzer is presented over the review so scrubbing resumes on return.
struct GameReviewView: View {
    @ObservedObject var review: GameReview
    @EnvironmentObject var analyze: AnalyzeGame
    var autoOpen = false
    var onExit: () -> Void

    @State private var index = 0
    @State private var showAnalyzer = false

    var body: some View {
        ZStack(alignment: .top) {
            RadialBackground(surface: .analysis)
            VStack(spacing: 8) {
                header
                if !review.ready {
                    Spacer()
                    ProgressView("Analyzing game…").tint(Palette.accentSoft).foregroundStyle(Palette.creamDim)
                    Spacer()
                } else {
                    content
                }
            }
            .padding(.top, 8)
        }
        .task { await review.compute(); if autoOpen { openAnalyzer() } }
        .fullScreenCover(isPresented: $showAnalyzer) {
            AnalyzeScreen(onExit: { showAnalyzer = false }).environmentObject(analyze)
        }
    }

    private var header: some View {
        HStack {
            Button("‹ Done") { onExit() }.font(ThemeFont.bodyStrong).foregroundStyle(Palette.creamDim)
            Spacer()
            Text("Game Review").font(ThemeFont.screenTitle).foregroundStyle(Palette.cream)
            Spacer()
            Color.clear.frame(width: 60, height: 1)
        }
        .padding(.horizontal, 20)
    }

    /// Board + side panel. The panel's fixed pieces (stats, caption, scrubber, CTA)
    /// are measured against the available height and the eval graph absorbs what's
    /// left, so the column always fits instead of overflowing the screen.
    @ViewBuilder
    private var content: some View {
        GeometryReader { geo in
            let m = PanelMetrics(size: geo.size)
            let pt = review.points[safe: index]
            let pips = review.pipCounts(at: index)
            let b = pt?.board ?? []
            let wOff = b.isEmpty ? 0 : 15 - b.reduce(0) { $0 + max(0, Int($1)) }
            let bOff = b.isEmpty ? 0 : 15 - b.reduce(0) { $0 + max(0, -Int($1)) }
            HStack(alignment: .center, spacing: 18) {
                BoardCanvas(board: b, flipped: review.reviewSide, selected: nil,
                            whitePips: pips.white, blackPips: pips.black,
                            whiteOff: wOff, blackOff: bOff,
                            whiteEndgame: wOff > 0, blackEndgame: bOff > 0,
                            onTap: { _, _ in })
                    .frame(maxWidth: .infinity)

                VStack(alignment: .leading, spacing: m.gap) {
                    evalBox(graphHeight: m.graphHeight)
                    statsBox
                    moveCaption(pt, height: m.captionHeight)
                        .frame(maxWidth: .infinity, alignment: .leading)
                    scrubber
                    Spacer(minLength: 0)
                    analyzerCTA(height: m.ctaHeight)
                }
                .frame(width: m.columnWidth)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .padding(.horizontal, 16)
        }
    }

    private func analyzerCTA(height: CGFloat) -> some View {
        Button { openAnalyzer() } label: { Label("Open in Analyzer", systemImage: "scope") }
            .buttonStyle(PrimaryCTAButtonStyle(cornerRadius: 12, minHeight: height))
    }

    private func evalBox(graphHeight: CGFloat) -> some View {
        let pt = review.points[safe: index]
        return VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("Eval (\(review.reviewSideName))").font(ThemeFont.caption).foregroundStyle(Palette.muted)
                Spacer()
                Text(pt?.evalReview.map { String(format: "%+.3f", $0) } ?? "—")
                    .font(ThemeFont.evalValue)
                    .foregroundStyle((pt?.evalReview ?? 0) >= 0 ? Palette.evalPos : Palette.evalNeg)
            }
            EvalGraph(values: review.points.map { $0.evalReview }, currentIndex: index)
                .frame(height: graphHeight)
        }
        .padding(10)
        .background(RoundedRectangle(cornerRadius: 12).fill(Color.black.opacity(0.28)))
    }

    /// Whole-game stats: the dice each side rolled, and how the engine would have
    /// fared in the reviewed player's seat on those same rolls. One box (rather than
    /// two) so the second stat costs a row, not a whole card's worth of chrome.
    private var statsBox: some View {
        VStack(alignment: .leading, spacing: 5) {
            HStack(spacing: 8) {
                Image(systemName: "dice.fill").font(ThemeFont.micro).foregroundStyle(Palette.muted)
                Text("Dice rolled — White \(review.whiteDiceTotal) · Black \(review.blackDiceTotal)")
                    .font(ThemeFont.micro).foregroundStyle(Palette.toastInk)
                    .lineLimit(1).minimumScaleFactor(0.8)
                Spacer(minLength: 0)
            }
            // The result reads as part of the sentence ("… in your seat — Loss"), so
            // it sits directly after the label and the slack goes at the end of the row.
            HStack(spacing: 5) {
                Image(systemName: "cpu").font(ThemeFont.micro).foregroundStyle(Palette.muted)
                    .padding(.trailing, 3)
                Text("Computer in your seat —")
                    .font(ThemeFont.micro).foregroundStyle(Palette.toastInk)
                    .lineLimit(1).minimumScaleFactor(0.8)
                // Always resolved by the time the review renders; "—" only if the
                // rollout produced no outcome at all (no model).
                if let cf = review.counterfactual {
                    Text(cf.headline).font(ThemeFont.micro).lineLimit(1).minimumScaleFactor(0.8)
                        .foregroundStyle(cf.finished ? (cf.reviewedWon ? Palette.winText : Palette.evalNeg)
                                                     : Palette.muted)
                } else {
                    Text("—").font(ThemeFont.micro).foregroundStyle(Palette.muted)
                }
                Spacer(minLength: 0)
            }
        }
        .padding(.vertical, 8).padding(.horizontal, 12)
        .background(RoundedRectangle(cornerRadius: 9).fill(Palette.fillWarm.opacity(0.06)))
    }

    @ViewBuilder
    private func moveCaption(_ pt: ReviewPoint?, height: CGFloat) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            if let pt {
                if let mover = pt.mover, let dice = pt.dice {
                    Text("Move \(index): \(mover ? "Black" : "White") \(dice.0)-\(dice.1) · \(pt.label)")
                        .font(ThemeFont.caption).foregroundStyle(Palette.cream)
                        .lineLimit(2).minimumScaleFactor(0.85)
                } else {
                    Text("Start position").font(ThemeFont.caption).foregroundStyle(Palette.cream)
                }
                if let d = pt.blunderDelta, d > 0.005, let best = pt.bestEval, let played = pt.playedEval {
                    Text("best \(String(format: "%+.2f", best)) vs played \(String(format: "%+.2f", played))")
                        .font(ThemeFont.micro).foregroundStyle(Palette.accentSoft).lineLimit(1)
                }
            }
        }
        .frame(height: height, alignment: .topLeading)
        .clipped()
    }

    private var scrubber: some View {
        HStack(spacing: 12) {
            Button { index = max(0, index - 1) } label: { Image(systemName: "chevron.left") }
                .buttonStyle(GhostPillButtonStyle(disabled: index == 0)).disabled(index == 0)
            Slider(value: Binding(get: { Double(index) }, set: { index = Int($0.rounded()) }),
                   in: 0...Double(max(1, review.points.count - 1)), step: 1)
                .tint(Palette.accentSoft)
            Button { index = min(review.points.count - 1, index + 1) } label: { Image(systemName: "chevron.right") }
                .buttonStyle(GhostPillButtonStyle(disabled: index >= review.points.count - 1))
                .disabled(index >= review.points.count - 1)
        }
    }

    private func openAnalyzer() {
        let i = max(0, index - 1)
        guard let pt = review.points[safe: i] else { return }
        analyze.openForReview(board: pt.board, side: pt.sideToMove, anchor: review.reviewSide,
                              gameDice: review.gameDiceFrom(i), gameLine: review.gameBoardsFrom(i))
        showAnalyzer = true
    }
}

/// Side-panel sizing for the review screen. Every element except the eval graph has
/// a known height, so the graph takes the remainder — the column then fits any
/// landscape size (small phones tighten the spacing) instead of running off-screen.
private struct PanelMetrics {
    let gap: CGFloat
    let captionHeight: CGFloat
    let ctaHeight: CGFloat
    let graphHeight: CGFloat
    let columnWidth: CGFloat

    init(size: CGSize) {
        let tight = size.height < 380
        gap = tight ? 6 : 10
        captionHeight = tight ? 26 : 34
        ctaHeight = tight ? 40 : 46
        columnWidth = min(288, max(226, size.width * 0.36))
        // evalBox chrome (padding + label + inner spacing) 41 · statsBox 51 ·
        // scrubber 38, plus the four inter-element gaps.
        let fixed: CGFloat = 41 + 51 + 38 + captionHeight + ctaHeight + gap * 4
        graphHeight = max(34, min(78, size.height - fixed))
    }
}

/// Line graph of per-ply evals in [-2,+2] with a dashed zero line and a scrubber cursor.
struct EvalGraph: View {
    let values: [Float?]
    let currentIndex: Int
    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            let n = max(1, values.count - 1)
            let x: (Int) -> CGFloat = { CGFloat($0) / CGFloat(n) * w }
            let y: (Float) -> CGFloat = { h * (1 - CGFloat((max(-2, min(2, $0)) + 2) / 4)) }
            ZStack {
                RoundedRectangle(cornerRadius: 6).fill(Color.black.opacity(0.25))
                Path { p in p.move(to: CGPoint(x: 0, y: h / 2)); p.addLine(to: CGPoint(x: w, y: h / 2)) }
                    .stroke(Color.white.opacity(0.18), style: .init(lineWidth: 1, dash: [4, 4]))
                Path { p in
                    var started = false
                    for (i, v) in values.enumerated() {
                        guard let v else { started = false; continue }
                        let pt = CGPoint(x: x(i), y: y(v))
                        if started { p.addLine(to: pt) } else { p.move(to: pt); started = true }
                    }
                }.stroke(Palette.accentSoft, lineWidth: 2.5)
                Path { p in p.move(to: CGPoint(x: x(currentIndex), y: 0)); p.addLine(to: CGPoint(x: x(currentIndex), y: h)) }
                    .stroke(Color.white.opacity(0.55), lineWidth: 1.5)
            }
        }
    }
}

extension Array {
    subscript(safe i: Int) -> Element? { indices.contains(i) ? self[i] : nil }
}

// MARK: - Counterfactual presentation

extension CounterfactualOutcome {
    /// The engine's result in the reviewed player's seat.
    var headline: String {
        guard finished else { return "Unresolved" }
        return (reviewedWon ? "Win" : "Loss") + (mars ? " (mars)" : "")
    }
}
