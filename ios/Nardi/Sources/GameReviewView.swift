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

    @ViewBuilder
    private var content: some View {
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

            VStack(alignment: .leading, spacing: 10) {
                evalBox
                diceTotals
                moveCaption(pt).frame(maxWidth: .infinity, alignment: .leading)
                scrubber
                Spacer(minLength: 0)
                Button { openAnalyzer() } label: { Label("Open in Analyzer", systemImage: "scope") }
                    .buttonStyle(PrimaryCTAButtonStyle(cornerRadius: 12, minHeight: 46))
            }
            .frame(width: 288)
        }
        .padding(.horizontal, 16)
    }

    private var evalBox: some View {
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
                .frame(height: 78)
        }
        .padding(10)
        .background(RoundedRectangle(cornerRadius: 12).fill(Color.black.opacity(0.28)))
    }

    private var diceTotals: some View {
        HStack(spacing: 8) {
            Image(systemName: "dice.fill").foregroundStyle(Palette.muted)
            Text("Dice rolled — White \(review.whiteDiceTotal) · Black \(review.blackDiceTotal)")
                .font(ThemeFont.micro).foregroundStyle(Palette.toastInk)
            Spacer()
        }
        .padding(.vertical, 8).padding(.horizontal, 12)
        .background(RoundedRectangle(cornerRadius: 9).fill(Palette.fillWarm.opacity(0.06)))
    }

    @ViewBuilder
    private func moveCaption(_ pt: ReviewPoint?) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            if let pt {
                if let mover = pt.mover, let dice = pt.dice {
                    Text("Move \(index): \(mover ? "Black" : "White") \(dice.0)-\(dice.1) · \(pt.label)")
                        .font(ThemeFont.caption).foregroundStyle(Palette.cream)
                } else {
                    Text("Start position").font(ThemeFont.caption).foregroundStyle(Palette.cream)
                }
                if let d = pt.blunderDelta, d > 0.005, let best = pt.bestEval, let played = pt.playedEval {
                    Text("best \(String(format: "%+.2f", best)) vs played \(String(format: "%+.2f", played))")
                        .font(ThemeFont.micro).foregroundStyle(Palette.accentSoft)
                }
            }
        }
        .frame(minHeight: 30)
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
