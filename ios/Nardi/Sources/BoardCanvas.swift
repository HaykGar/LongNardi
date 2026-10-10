import SwiftUI

/// Frames of the play controls, published as SwiftUI anchors so an overlay — e.g.
/// the tutorial's coach-mark glow — can highlight the REAL control wherever it lays
/// out, with NO hardcoded rects. Generic: these views know nothing about the
/// tutorial; they just advertise where their controls ended up. Gated behind a
/// `publishAnchors` flag so the normal play screen pays nothing.
enum PlayControl: Hashable { case board, dice, confirm, undo, replay, settings, resign }

struct PlayAnchorKey: PreferenceKey {
    static let defaultValue: [PlayControl: Anchor<CGRect>] = [:]
    static func reduce(value: inout [PlayControl: Anchor<CGRect>],
                       nextValue: () -> [PlayControl: Anchor<CGRect>]) {
        value.merge(nextValue()) { _, n in n }
    }
}
extension View {
    /// Publish this view's bounds under `control` when `active` (no-op otherwise).
    /// Attach it to the control BEFORE any `.position`/`.frame(alignment:)` that
    /// places it — `.bounds` resolves the control's own final rect. Do NOT attach it
    /// across a `.offset` (offset is render-only and the anchor keeps the pre-offset
    /// layout frame).
    ///
    /// Uses `transformAnchorPreference` (MERGE), not `anchorPreference` (REPLACE): the
    /// board control wraps the dice/confirm/undo controls, so a replacing write would
    /// wipe the descendants' entries. Merging adds this control to whatever the
    /// subtree already published, so nesting is safe.
    func playAnchor(_ control: PlayControl, active: Bool) -> some View {
        transformAnchorPreference(key: PlayAnchorKey.self, value: .bounds) { dict, anchor in
            if active { dict[control] = anchor }
        }
    }
}

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
    /// Bumped per new roll ⇒ the dice play their toss (0 / unchanged ⇒ static).
    var rollSeq: Int = 0
    /// Bumped per dice-order swap ⇒ the dice play their emphasis bump.
    var diceEmphasis: Int = 0
    /// Overrides the board-relative die size (Review uses a fixed 31pt die).
    var diceSize: CGFloat? = nil
    /// On-board confirm pill (play screen only), shown centred when true.
    var canConfirm: Bool = false
    /// When true the ✓ pill is ALWAYS present, greyed + disabled unless canConfirm
    /// (used by the tutorial so the control stays visible while being explained).
    var alwaysShowConfirm: Bool = false
    /// EXPERIMENTAL (play screen only, easily revertible): shorten each slot's tap
    /// target so it doesn't reach the board centre, then float Confirm in the centre
    /// of the RIGHT half and Undo in the centre of the LEFT half.
    var centerHalfControls: Bool = false
    var canUndo: Bool = false
    /// Publish real control frames via PlayAnchorKey (tutorial coach marks). Off in
    /// normal play so it costs nothing.
    var publishAnchors: Bool = false
    var onTap: (Int, Int) -> Void
    var onLongPress: ((Int, Int) -> Void)? = nil
    var onToggleDice: (() -> Void)? = nil
    var onConfirm: (() -> Void)? = nil
    var onUndo: (() -> Void)? = nil

    @State private var leftLeaf: CGRect = .zero    // captured in the "board" space
    @State private var rightLeaf: CGRect = .zero
    @State private var prevFlights: [Flight] = []  // to spot landings (for the ripple)
    @State private var landings: [Landing] = []

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
                .playAnchor(.board, active: publishAnchors)   // tight fitted-board rect
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
                         selected: selected, startMask: startMask, shortTap: centerHalfControls,
                         onTap: onTap, onLongPress: onLongPress)
                    .background(leafReader(false))
                CenterBarView().frame(width: gutterW)
                LeafView(top: .topRight, bot: .botRight, board: board, flipped: flipped,
                         selected: selected, startMask: startMask, shortTap: centerHalfControls,
                         onTap: onTap, onLongPress: onLongPress)
                    .background(leafReader(true))
                TrayView(count: perspOff, white: perspWhite).frame(width: trayW)
            }
            .padding(framePad)

            // Overlays, all in the "board" coordinate space.
            let trays = (left: CGRect(x: framePad, y: framePad, width: trayW, height: size.height - 2 * framePad),
                         right: CGRect(x: W - framePad - trayW, y: framePad, width: trayW, height: size.height - 2 * framePad))
            FlightLayer(flights: flights, flipped: flipped, board: board,
                        metrics: BoardMetrics(leftLeaf: leftLeaf, rightLeaf: rightLeaf),
                        trays: trays, trayCounts: (oppOff, perspOff), theme: theme)
            ForEach(landings) { LandingRipple(landing: $0) }

            if !dice.isEmpty {
                let dSize = diceSize ?? dieSize
                HStack(spacing: dSize * 0.42) {
                    // Key by physical die so a swap slides them; fall back to position
                    // when ids repeat (e.g. four dice for doubles).
                    let unique = Set(dice.map(\.id)).count == dice.count
                    let keyed = dice.enumerated().map { (key: unique ? $0.element.id : 100 + $0.offset, i: $0.offset, d: $0.element) }
                    ForEach(keyed, id: \.key) { e in
                        let i = e.i, d = e.d
                        RollingDie(value: d.value, rollSeq: rollSeq, index: unique ? d.id : i,
                                   delay: RollingDie.stagger(index: i, diceCount: dice.count),
                                   style: theme.dice, primary: d.isFirst, disabled: !d.hasStart,
                                   size: dSize, emphasis: diceEmphasis, onTap: onToggleDice)
                    }
                }
                .animation(Motion.anim(Motion.snappy), value: dice.map(\.id))
                .playAnchor(.dice, active: publishAnchors)
            }
            if canConfirm || alwaysShowConfirm {
                let confirm = Button { if canConfirm { onConfirm?() } } label: {
                    Image(systemName: "checkmark").font(.system(size: 18, weight: .heavy))
                }
                .buttonStyle(ConfirmPillButtonStyle(isEnabled: canConfirm))
                .grayscale(canConfirm ? 0 : 1)
                .opacity(canConfirm ? 1 : 0.45)
                .shadow(color: Palette.confirmTop.opacity(canConfirm ? 0.6 : 0), radius: 10)
                .playAnchor(.confirm, active: publishAnchors)
                .transition(.scale(scale: 0.6).combined(with: .opacity))
                if centerHalfControls {
                    confirm.position(x: size.width * 0.75, y: size.height * 0.5)
                } else {
                    confirm.offset(y: size.height * 0.19)
                }
            }
            // EXPERIMENTAL: on-board Undo, centred in the LEFT half.
            if centerHalfControls, let onUndo {
                CircleIconButton(systemName: "arrow.counterclockwise", kind: .wood, size: 40,
                                 disabled: !canUndo) { onUndo() }
                    .playAnchor(.undo, active: publishAnchors)
                    .position(x: size.width * 0.25, y: size.height * 0.5)
            }
            // Pip badges drawn on TOP of the board (over the centre bar) so 3-digit
            // counts never clip behind the leaves on smaller boards.
            if showPips && (whitePips > 0 || blackPips > 0) {
                PipBadge(count: oppPip, fontSize: size.height * 0.032).position(x: size.width / 2, y: size.height * 0.135)
                PipBadge(count: perspPip, fontSize: size.height * 0.032).position(x: size.width / 2, y: size.height * 0.865)
            }
        }
        .animation(Motion.anim(Motion.bouncy), value: canConfirm)
        .coordinateSpace(name: "board")
        .onChange(of: flights.map(\.id)) { _, ids in
            spawnLandings(landed: prevFlights.filter { !ids.contains($0.id) })
            prevFlights = flights
        }
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

    /// A ripple where each finished flight touched down (skipped under Reduce Motion).
    private func spawnLandings(landed: [Flight]) {
        guard !Motion.reduceMotion else { return }
        let m = BoardMetrics(leftLeaf: leftLeaf, rightLeaf: rightLeaf)
        for f in landed {
            guard let (r, c) = f.to else { continue }
            let n = r * BoardMap.cols + c < board.count ? abs(Int(board[r * BoardMap.cols + c])) : 1
            guard let a = m.slotAnchor(row: r, col: c, flipped: flipped, index: n - 1) else { continue }
            let l = Landing(pt: a.pt, d: a.d)
            landings.append(l)
            Task { @MainActor in
                try? await Task.sleep(nanoseconds: UInt64(Motion.Duration.ripple * 1_000_000_000) + 50_000_000)
                landings.removeAll { $0.id == l.id }
            }
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
    var shortTap: Bool = false
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
                          startMask: startMask, shortTap: shortTap,
                          onTap: onTap, onLongPress: onLongPress)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

/// EXPERIMENTAL: a tap target that covers only the outer part of a slot (from the
/// board edge inward), leaving the centre band free for the on-board buttons.
private struct SlotTapShape: Shape {
    var downward: Bool
    var frac: CGFloat = 0.62
    func path(in r: CGRect) -> Path {
        let h = r.height * frac
        let y = downward ? r.minY : r.maxY - h
        return Path(CGRect(x: r.minX, y: y, width: r.width, height: h))
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
    var shortTap: Bool = false
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
                CheckerColumn(count: abs(v), white: v > 0, downward: downward, cellW: w, cellH: h)
                // Drawn AFTER the checkers so it stays visible — startable points always
                // hold checkers, so a dot under the stack would only show mid-animation.
                ZStack {
                    if dot {
                        StartDot(diameter: w * 0.28, atTop: downward, count: abs(v), w: w, h: h)
                            .transition(.scale(scale: 0.3).combined(with: .opacity))
                    }
                }
                .animation(Motion.anim(dot ? Motion.bouncy : Motion.exit(Motion.Duration.quick)), value: dot)
                if isSel {
                    RoundedRectangle(cornerRadius: 4)
                        .stroke(Palette.selectionStroke, lineWidth: 2)
                        .background(RoundedRectangle(cornerRadius: 4).fill(Palette.selectionStroke.opacity(0.14)))
                        .padding(1)
                }
            }
            .frame(width: w, height: h)
            .contentShape(shortTap ? AnyShape(SlotTapShape(downward: downward)) : AnyShape(Rectangle()))
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
    let count: Int
    let w: CGFloat
    let h: CGFloat
    var body: some View {
        // Sits just past the stack's outermost checker, so it never covers the count
        // drawn on the base checker.
        let d = BoardMap.checkerDiameter(cellW: w, cellH: h)
        let shown = CGFloat(max(1, min(count, BoardMap.maxShown)))
        let reach = BoardMap.stackInset + d + (shown - 1) * BoardMap.stackStep(d: d, cellH: h) + diameter / 2 + 1
        let edge = min(reach, h - diameter / 2)
        Circle()
            .fill(RadialGradient(gradient: Palette.startDotStops, center: .init(x: 0.35, y: 0.3),
                                 startRadius: 0, endRadius: diameter * 0.6))
            .overlay(Circle().stroke(Color.white.opacity(0.85), lineWidth: 1))
            .frame(width: diameter, height: diameter)
            .shadow(color: .black.opacity(0.5), radius: 1, y: 0.5)
            .position(x: w / 2, y: atTop ? edge : h - edge)
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
        let step = BoardMap.stackStep(d: d, cellH: cellH)
        ZStack {
            ForEach(0..<max(0, shown), id: \.self) { k in
                let baseY = downward ? d / 2 + BoardMap.stackInset : cellH - d / 2 - BoardMap.stackInset
                let y = downward ? baseY + CGFloat(k) * step : baseY - CGFloat(k) * step
                ZStack {
                    CheckerView(white: white, d: d)
                    // The count sits on the top checker (nearest the board centre).
                    if k == shown - 1 && count > BoardMap.maxShown {
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
                        .transition(.asymmetric(insertion: .modifier(active: ChipSquash(on: true), identity: ChipSquash(on: false)),
                                                removal: .opacity))
                }
            }
            .animation(Motion.anim(Motion.bouncy), value: count)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
            .padding(.vertical, g.size.height * 0.03)
        }
        .background(RoundedRectangle(cornerRadius: 5).fill(Grad.tray))
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color.black.opacity(0.5), lineWidth: 1))
    }
}

/// A newly borne-off chip lands squashed (wide + flat) and springs to shape.
private struct ChipSquash: ViewModifier {
    let on: Bool
    func body(content: Content) -> some View {
        content.scaleEffect(x: on ? 1.25 : 1, y: on ? 0.3 : 1, anchor: .bottom).opacity(on ? 0 : 1)
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
    var fontSize: CGFloat = 11   // scaled to the board so it shrinks with a smaller board
    var body: some View {
        Text("\(count)").font(.system(size: fontSize, weight: .heavy).monospacedDigit())
            .numericSwap(count)
            .foregroundStyle(Palette.pipBadgeInk)
            .padding(.horizontal, fontSize * 0.45).padding(.vertical, fontSize * 0.18)
            .background(Capsule().fill(Grad.pipBadge))
            .overlay(Capsule().stroke(Color.black.opacity(0.25), lineWidth: 0.5))
            .fixedSize()
            .shadow(color: .black.opacity(0.4), radius: 1, y: 0.5)
            .valueBump(count)
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
    let board: [Int8]
    let metrics: BoardMetrics
    let trays: (left: CGRect, right: CGRect)
    let trayCounts: (left: Int, right: Int)
    let theme: BoardTheme
    var body: some View {
        if metrics.ready {
            ForEach(flights) { f in
                // The source was already decremented and the destination not yet
                // incremented, so each count is exactly the moving checker's slot.
                let aFrom = f.from.flatMap { metrics.slotAnchor(row: $0.0, col: $0.1, flipped: flipped, index: count($0)) }
                let aTo   = f.to.flatMap   { metrics.slotAnchor(row: $0.0, col: $0.1, flipped: flipped, index: count($0)) }
                if let known = aFrom ?? aTo {
                    // Bear-off: that half's tray (the right half's home tray is on the right).
                    let off = trayPoint(right: known.rightHalf)
                    FlightView(start: aFrom?.pt ?? off, end: aTo?.pt ?? off, d: known.d,
                               white: f.white, fadeOut: f.to == nil, fadeIn: f.from == nil)
                }
            }
        }
    }

    private func count(_ rc: (Int, Int)) -> Int {
        let i = rc.0 * BoardMap.cols + rc.1
        return i < board.count ? abs(Int(board[i])) : 0
    }

    /// The next free chip slot in a tray (chips stack up from the bottom).
    private func trayPoint(right: Bool) -> CGPoint {
        let t = right ? trays.right : trays.left
        let n = CGFloat(min(right ? trayCounts.right : trayCounts.left, 14))
        let chipH = max(3, t.height * 0.028), gap = t.height * 0.006
        return CGPoint(x: t.midX, y: t.maxY - t.height * 0.03 - (n + 0.5) * (chipH + gap))
    }
}

/// A single travelling checker that self-animates start→end on appear (framework-
/// interpolated), so only this view redraws per frame — the board stays smooth.
/// It lifts on an arc (scale + deeper shadow at the apex); a bear-off fades into
/// the tray, an undone bear-off fades out of it.
private struct FlightView: View {
    let start: CGPoint
    let end: CGPoint
    let d: CGFloat
    let white: Bool
    let fadeOut: Bool
    let fadeIn: Bool
    @State private var t: CGFloat = 0
    var body: some View {
        let dist = hypot(end.x - start.x, end.y - start.y)
        CheckerView(white: white, d: d)
            .modifier(FlightPath(t: t, start: start, end: end,
                                 lift: Motion.reduceMotion ? 0 : min(dist * 0.22, d * 1.1),
                                 fadeOut: fadeOut, fadeIn: fadeIn))
            .onAppear { withAnimation(Motion.travel()) { t = 1 } }
    }
}

private struct FlightPath: ViewModifier, Animatable {
    var t: CGFloat
    let start: CGPoint
    let end: CGPoint
    let lift: CGFloat
    let fadeOut: Bool
    let fadeIn: Bool
    var animatableData: CGFloat {
        get { t }
        set { t = newValue }
    }
    func body(content: Content) -> some View {
        let arc = lift > 0 ? sin(.pi * t) : 0
        let opacity = fadeOut ? 1 - max(0, (t - 0.6) / 0.4) : (fadeIn ? min(1, t / 0.4) : 1)
        content
            .scaleEffect(1 + 0.12 * arc)
            .shadow(color: .black.opacity(0.3 * arc), radius: 2 + 6 * arc, y: 2 + 5 * arc)
            .opacity(opacity)
            .position(x: start.x + (end.x - start.x) * t,
                      y: start.y + (end.y - start.y) * t - lift * arc)
    }
}

/// Where a travelling checker touched down (drives a one-shot ripple).
private struct Landing: Identifiable {
    let id = UUID()
    let pt: CGPoint
    let d: CGFloat
}

private struct LandingRipple: View {
    let landing: Landing
    @State private var go = false
    var body: some View {
        Circle()
            .stroke(Color.white.opacity(0.7), lineWidth: 1.5)
            .frame(width: landing.d, height: landing.d)
            .scaleEffect(go ? 1.7 : 0.95)
            .opacity(go ? 0 : 0.8)
            .position(landing.pt)
            .allowsHitTesting(false)
            .onAppear { withAnimation(Motion.out(Motion.Duration.ripple)) { go = true } }
    }
}
