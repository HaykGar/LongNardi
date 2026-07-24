import SwiftUI

/// The reusable, fully-vector landscape board (mockup screens 1c/1d/2a/2b/2c).
/// Pure presentation: it takes plain values + closures so the play screen
/// (NardiGame), the analyzer (AnalyzeGame) and game review all render through it.
///
/// Layout: [ bear-off tray · left leaf (topLeft/botLeft) · centre bar · right leaf
/// (topRight/botRight) · bear-off tray ]. Each of the four leaves holds a row of
/// six tappable `PointView`s, so a tap target *is* its slot — no coordinate
/// hit-testing. Coordinate math (engine ↔ quadrant, incl. the flip) lives in
/// `BoardMap`; flight/replay anchors in `BoardMetrics`.
struct BoardCanvas: View {
    let board: [Int8]
    let flipped: Bool
    var selected: (Int, Int)? = nil
    var flights: [Flight] = []
    var startMask: Int = 0
    var whitePips: Int = 0
    var blackPips: Int = 0
    var whiteOff: Int = 0
    var blackOff: Int = 0
    var whiteEndgame: Bool = false
    var blackEndgame: Bool = false
    var showPips: Bool = true
    var theme: BoardTheme = .green
    /// On-board dice overlay (play screen only). Empty ⇒ no dice drawn.
    var dice: [DieSpec] = []
    /// On-board confirm pill (play screen only), shown centred when true.
    var canConfirm: Bool = false
    var onTap: (Int, Int) -> Void
    var onLongPress: ((Int, Int) -> Void)? = nil
    var onToggleDice: (() -> Void)? = nil
    var onConfirm: (() -> Void)? = nil

    @State private var leftLeaf: CGRect = .zero    // captured in the "board" space
    @State private var rightLeaf: CGRect = .zero

    // Perspective helpers: the perspective player is White iff not flipped. Their
    // home is botRight (tray on the right); the opponent's home is topLeft (left).
    private var perspWhite: Bool { !flipped }
    private var perspOff: Int  { flipped ? blackOff : whiteOff }
    private var oppOff: Int    { flipped ? whiteOff : blackOff }
    private var perspEndgame: Bool { flipped ? blackEndgame : whiteEndgame }
    private var oppEndgame: Bool   { flipped ? whiteEndgame : blackEndgame }
    private var oppPip: Int   { flipped ? whitePips : blackPips }
    private var perspPip: Int { flipped ? blackPips : whitePips }

    var body: some View {
        GeometryReader { geo in
            let rect = fittedRect(in: geo.size)
            boardContent(size: rect.size)
                .frame(width: rect.width, height: rect.height)
                .position(x: rect.midX, y: rect.midY)
        }
    }

    /// Fit the board's fixed aspect inside the available space, centred.
    private func fittedRect(in size: CGSize) -> CGRect {
        let aspect: CGFloat = 1.95
        var w = size.width, h = w / aspect
        if h > size.height { h = size.height; w = h * aspect }
        return CGRect(x: (size.width - w) / 2, y: (size.height - h) / 2, width: w, height: h)
    }

    @ViewBuilder
    private func boardContent(size: CGSize) -> some View {
        let W = size.width
        let framePad = W * 0.013, trayW = W * 0.040, leafGap = W * 0.009, gutterW = W * 0.034
        let dieSize = size.height * 0.135

        ZStack {
            RoundedRectangle(cornerRadius: W * 0.024).fill(Grad.woodFrame)
                .overlay(RoundedRectangle(cornerRadius: W * 0.024)
                    .stroke(Color.black.opacity(0.28), lineWidth: 2))
                .shadow(color: .black.opacity(0.5), radius: 12, y: 8)

            HStack(spacing: leafGap) {
                TrayView(count: oppOff, white: !perspWhite).frame(width: trayW)
                LeafView(top: .topLeft, bot: .botLeft, board: board, flipped: flipped,
                         selected: selected, startMask: startMask, onTap: onTap, onLongPress: onLongPress)
                    .background(leafReader(false))
                CenterBarView().frame(width: gutterW)
                LeafView(top: .topRight, bot: .botRight, board: board, flipped: flipped,
                         selected: selected, startMask: startMask, onTap: onTap, onLongPress: onLongPress)
                    .background(leafReader(true))
                TrayView(count: perspOff, white: perspWhite).frame(width: trayW)
            }
            .padding(framePad)

            // Overlays, all in the "board" coordinate space.
            FlightLayer(flights: flights, flipped: flipped,
                        metrics: BoardMetrics(leftLeaf: leftLeaf, rightLeaf: rightLeaf), theme: theme)

            if !dice.isEmpty {
                HStack(spacing: dieSize * 0.42) {
                    ForEach(Array(dice.enumerated()), id: \.offset) { _, d in
                        DieFace(value: d.value, style: theme.dice, primary: d.isFirst,
                                disabled: !d.hasStart, size: dieSize, onTap: onToggleDice)
                    }
                }
            }
            if canConfirm {
                Button { onConfirm?() } label: { Label("Confirm", systemImage: "checkmark") }
                    .buttonStyle(ConfirmPillButtonStyle())
                    .offset(y: size.height * 0.19)
            }
            // Pip badges drawn on TOP of the board (over the centre bar) so 3-digit
            // counts never clip behind the leaves on smaller boards.
            if showPips && (whitePips > 0 || blackPips > 0) {
                PipBadge(count: oppPip).position(x: size.width / 2, y: size.height * 0.135)
                PipBadge(count: perspPip).position(x: size.width / 2, y: size.height * 0.865)
            }
        }
        .coordinateSpace(name: "board")
        .overlay(alignment: .topLeading) {
            if oppEndgame { OffBadge(count: oppOff).padding(W * 0.022) }
        }
        .overlay(alignment: .bottomTrailing) {
            if perspEndgame { OffBadge(count: perspOff).padding(W * 0.022) }
        }
        .onPreferenceChange(LeafFrameKey.self) { d in
            if let l = d[false] { leftLeaf = l }
            if let r = d[true]  { rightLeaf = r }
        }
    }

    private func leafReader(_ isRight: Bool) -> some View {
        GeometryReader { g in
            Color.clear.preference(key: LeafFrameKey.self,
                                   value: [isRight: g.frame(in: .named("board"))])
        }
    }
}

// MARK: - One leaf (a half of the board): top row over bottom row, 6 points each

private struct LeafView: View {
    let top: Quadrant
    let bot: Quadrant
    let board: [Int8]
    let flipped: Bool
    let selected: (Int, Int)?
    let startMask: Int
    let onTap: (Int, Int) -> Void
    let onLongPress: ((Int, Int) -> Void)?

    var body: some View {
        VStack(spacing: 0) {
            row(top, downward: true)
            row(bot, downward: false)
        }
        .padding(.horizontal, 2)
        .background(RoundedRectangle(cornerRadius: 5).fill(Grad.leaf))
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color.black.opacity(0.35), lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 5))
    }

    private func row(_ q: Quadrant, downward: Bool) -> some View {
        HStack(spacing: 1) {
            ForEach(0..<6, id: \.self) { i in
                PointView(quadrant: q, index: i, downward: downward,
                          board: board, flipped: flipped, selected: selected,
                          startMask: startMask, onTap: onTap, onLongPress: onLongPress)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

// MARK: - One point (slot): triangle + checker stack + start dot + selection

private struct PointView: View {
    let quadrant: Quadrant
    let index: Int
    let downward: Bool                 // top row ⇒ stacks grow downward from the top edge
    let board: [Int8]
    let flipped: Bool
    let selected: (Int, Int)?
    let startMask: Int
    let onTap: (Int, Int) -> Void
    let onLongPress: ((Int, Int) -> Void)?

    var body: some View {
        let (row, col) = BoardMap.coord(quadrant, index, flipped: flipped)
        let bit = row * BoardMap.cols + col
        let v = bit < board.count ? Int(board[bit]) : 0
        let vCol = BoardMap.visual(row: row, col: col, flipped: flipped).vCol
        let dot = (startMask >> bit) & 1 == 1
        let isSel = selected.map { $0.0 == row && $0.1 == col } ?? false

        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            ZStack {
                Triangle(pointingDown: downward)
                    .fill(LinearGradient.vertical(vCol.isMultiple(of: 2) ? Palette.pointCream : Palette.pointDark,
                                                  (vCol.isMultiple(of: 2) ? Palette.pointCream : Palette.pointDark).opacity(0.72)))
                    .opacity(0.92)
                if dot { StartDot(diameter: w * 0.26, atTop: downward, w: w, h: h) }
                CheckerColumn(count: abs(v), white: v > 0, downward: downward, cellW: w, cellH: h)
                if isSel {
                    RoundedRectangle(cornerRadius: 4)
                        .stroke(Palette.selectionStroke, lineWidth: 2)
                        .background(RoundedRectangle(cornerRadius: 4).fill(Palette.selectionStroke.opacity(0.14)))
                        .padding(1)
                }
            }
            .frame(width: w, height: h)
            .contentShape(Rectangle())
            .modifier(TapAndHold(onTap: { onTap(row, col) },
                                 onHold: onLongPress.map { lp in { lp(row, col) } }))
        }
    }
}

/// Attaches a tap, plus an optional long-press (editor only, so play taps aren't
/// swallowed by a slow press).
private struct TapAndHold: ViewModifier {
    let onTap: () -> Void
    let onHold: (() -> Void)?
    func body(content: Content) -> some View {
        if let onHold {
            content.onTapGesture { onTap() }
                .onLongPressGesture(minimumDuration: 0.4) { onHold() }
        } else {
            content.onTapGesture { onTap() }
        }
    }
}

/// Triangular point. Base on the outer edge, apex toward the board centre.
struct Triangle: Shape {
    var pointingDown: Bool
    func path(in r: CGRect) -> Path {
        var p = Path()
        let inset = r.width * 0.08
        if pointingDown {                       // base at top, apex near bottom-centre
            p.move(to: CGPoint(x: r.minX + inset, y: r.minY))
            p.addLine(to: CGPoint(x: r.maxX - inset, y: r.minY))
            p.addLine(to: CGPoint(x: r.midX, y: r.minY + r.height * 0.9))
        } else {                                // base at bottom, apex near top-centre
            p.move(to: CGPoint(x: r.minX + inset, y: r.maxY))
            p.addLine(to: CGPoint(x: r.maxX - inset, y: r.maxY))
            p.addLine(to: CGPoint(x: r.midX, y: r.maxY - r.height * 0.9))
        }
        p.closeSubpath()
        return p
    }
}

private struct StartDot: View {
    let diameter: CGFloat
    let atTop: Bool
    let w: CGFloat
    let h: CGFloat
    var body: some View {
        Circle()
            .fill(RadialGradient(gradient: Palette.startDotStops, center: .init(x: 0.35, y: 0.3),
                                 startRadius: 0, endRadius: diameter * 0.6))
            .overlay(Circle().stroke(Color.white.opacity(0.85), lineWidth: 1))
            .frame(width: diameter, height: diameter)
            .shadow(color: .black.opacity(0.5), radius: 1, y: 0.5)
            .position(x: w / 2, y: atTop ? diameter / 2 + 1 : h - diameter / 2 - 1)
    }
}

// MARK: - Checkers

/// A single vector checker with a high-contrast ring (readable on felt and wood).
struct CheckerView: View {
    let white: Bool
    let d: CGFloat
    var body: some View {
        Circle()
            .fill(RadialGradient(gradient: white ? Palette.whiteCheckerStops : Palette.blackCheckerStops,
                                 center: .init(x: 0.36, y: 0.3), startRadius: 0, endRadius: d * 0.72))
            .overlay(Circle().stroke(white ? Palette.whiteCheckerRing : Palette.blackCheckerRing,
                                     lineWidth: max(1, d * 0.07)))
            .overlay(Circle().inset(by: d * 0.17)
                .stroke(white ? Palette.whiteCheckerInner : Palette.blackCheckerInner, lineWidth: max(0.5, d * 0.03)))
            .overlay(Circle().fill(Palette.checkerGloss).blur(radius: d * 0.12).padding(d * 0.24)
                .offset(y: -d * 0.1).blendMode(.softLight))
            .frame(width: d, height: d)
            .shadow(color: .black.opacity(0.4), radius: d * 0.06, y: d * 0.04)
    }
}

/// The stack of checkers on one point (up to 5 shown; count badge on the base when more).
private struct CheckerColumn: View {
    let count: Int
    let white: Bool
    let downward: Bool
    let cellW: CGFloat
    let cellH: CGFloat
    var body: some View {
        let d = BoardMap.checkerDiameter(cellW: cellW, cellH: cellH)
        let shown = min(count, BoardMap.maxShown)
        ZStack {
            ForEach(0..<max(0, shown), id: \.self) { k in
                let baseY = downward ? d / 2 + 2 : cellH - d / 2 - 2
                let y = downward ? baseY + CGFloat(k) * d * BoardMap.stackOverlap
                                 : baseY - CGFloat(k) * d * BoardMap.stackOverlap
                ZStack {
                    CheckerView(white: white, d: d)
                    if k == 0 && count > BoardMap.maxShown {
                        Text("\(count)").font(.system(size: d * 0.42, weight: .bold))
                            .foregroundColor(white ? Palette.whiteCheckerCountInk : Palette.blackCheckerCountInk)
                    }
                }
                .position(x: cellW / 2, y: y)
            }
        }
        .frame(width: cellW, height: cellH)
        .allowsHitTesting(false)
    }
}

// MARK: - Trays, centre bar, badges

private struct TrayView: View {
    let count: Int
    let white: Bool
    var body: some View {
        GeometryReader { g in
            let chipH = max(3, g.size.height * 0.028)
            VStack(spacing: g.size.height * 0.006) {
                ForEach(0..<max(0, min(count, 15)), id: \.self) { _ in
                    RoundedRectangle(cornerRadius: 2)
                        .fill(LinearGradient.vertical(white ? Palette.trayChipWhiteTop : Palette.trayChipBlackTop,
                                                      white ? Palette.trayChipWhiteBottom : Palette.trayChipBlackBottom))
                        .overlay(RoundedRectangle(cornerRadius: 2)
                            .stroke((white ? Palette.trayChipWhiteEdge : Palette.trayChipBlackEdge).opacity(0.8), lineWidth: 0.5))
                        .frame(width: g.size.width * 0.62, height: chipH)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
            .padding(.vertical, g.size.height * 0.03)
        }
        .background(RoundedRectangle(cornerRadius: 5).fill(Grad.tray))
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color.black.opacity(0.5), lineWidth: 1))
    }
}

private struct CenterBarView: View {
    var body: some View {
        GeometryReader { g in
            VStack(spacing: g.size.height * 0.05) {
                Spacer()
                pin(g); pin(g)
                Spacer()
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .background(RoundedRectangle(cornerRadius: 4).fill(Grad.centerBar))
        .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color.black.opacity(0.55), lineWidth: 1))
    }
    private func pin(_ g: GeometryProxy) -> some View {
        RoundedRectangle(cornerRadius: 2).fill(Grad.gutterPin)
            .frame(width: g.size.width * 0.55, height: g.size.height * 0.075)
    }
}

/// A pip-count badge (drawn as a top overlay so it never clips behind the leaves).
private struct PipBadge: View {
    let count: Int
    var body: some View {
        Text("\(count)").font(ThemeFont.pipBadge).foregroundStyle(Palette.pipBadgeInk)
            .padding(.horizontal, 5).padding(.vertical, 2)
            .background(Capsule().fill(Grad.pipBadge))
            .overlay(Capsule().stroke(Color.black.opacity(0.25), lineWidth: 0.5))
            .fixedSize()
            .shadow(color: .black.opacity(0.4), radius: 1, y: 0.5)
    }
}

private struct OffBadge: View {
    let count: Int
    var body: some View {
        Text("Off \(count)/15").font(.system(size: 11, weight: .heavy)).monospacedDigit()
            .foregroundStyle(Palette.overlayBadge)
            .padding(.horizontal, 8).padding(.vertical, 3)
            .background(Capsule().fill(Color.black.opacity(0.55)))
    }
}

// MARK: - Flight / replay layer

private struct FlightLayer: View {
    let flights: [Flight]
    let flipped: Bool
    let metrics: BoardMetrics
    let theme: BoardTheme
    var body: some View {
        if metrics.ready {
            ForEach(flights) { f in
                let aFrom = f.from.flatMap { metrics.anchor(row: $0.0, col: $0.1, flipped: flipped) }
                let aTo   = f.to.flatMap   { metrics.anchor(row: $0.0, col: $0.1, flipped: flipped) }
                if let known = aFrom ?? aTo {
                    let dir: CGFloat = known.rightHalf ? 1 : -1     // bear-off slides toward that half's tray
                    let off = CGPoint(x: known.pt.x + dir * known.d * 2.2, y: known.pt.y)
                    FlightView(start: aFrom?.pt ?? off, end: aTo?.pt ?? off, d: known.d,
                               white: f.white, fadeOut: f.to == nil, fadeIn: f.from == nil)
                }
            }
        }
    }
}

/// A single sliding checker that self-animates start→end on appear (framework-
/// interpolated), so only this view redraws per frame — the board stays smooth.
private struct FlightView: View {
    let start: CGPoint
    let end: CGPoint
    let d: CGFloat
    let white: Bool
    let fadeOut: Bool
    let fadeIn: Bool
    @State private var progressed = false
    var body: some View {
        CheckerView(white: white, d: d)
            .opacity(fadeOut ? (progressed ? 0 : 1) : (fadeIn ? (progressed ? 1 : 0) : 1))
            .position(progressed ? end : start)
            .onAppear { withAnimation(.easeOut(duration: AnimationSpeed.flightDuration)) { progressed = true } }
    }
}
