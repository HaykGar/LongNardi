import SwiftUI

/// User-selectable animation speed, persisted in UserDefaults so the board's
/// FlightView and the game loops read the same value. High = quick slides; Low =
/// slowed down for clarity.
enum AnimationSpeed: String, CaseIterable, Identifiable {
    case high = "High"
    case low  = "Low"
    var id: String { rawValue }
    var flightDuration: Double { self == .low ? 0.95 : 0.4 }

    static let storageKey = "nardi.animationSpeed"
    static var current: AnimationSpeed {
        AnimationSpeed(rawValue: UserDefaults.standard.string(forKey: storageKey) ?? "") ?? .high
    }
}

/// Renders the board image with checkers placed via BoardGeometry and maps taps
/// (and optional long-presses, used by the analysis editor) back to engine coords.
/// Pure presentation: it takes plain values and closures so both the play screen
/// (NardiGame) and the analyze screen (AnalyzeGame) can reuse the same geometry.
struct BoardCanvas: View {
    let board: [Int8]
    let flipped: Bool
    let selected: (Int, Int)?
    let flights: [Flight]
    /// Bitmask (bit row*cols+col, engine coords) of squares from which a move can
    /// start this turn; each gets a small green dot. 0 = none (e.g. the analyzer).
    var startMask: Int = 0
    var onTap: (Int, Int) -> Void
    /// When set, a long-press on a cell calls this (engine coords). Only attached
    /// when provided, so it never interferes with the play screen's plain taps.
    var onLongPress: ((Int, Int) -> Void)? = nil

    @State private var lastTouch: CGPoint = .zero
    @State private var didLongPress = false

    private static let boardImage = loadImage("BoardImg", "jpg")
    private static let whitePiece = loadImage("WhitePiece", "png")
    private static let blackPiece = loadImage("BlackPiece", "png")

    /// One checker slide, set by the user's AnimationSpeed (read live so a change
    /// takes effect on the next move). Game objects sleep for this per hop, so it
    /// stays in sync with the FlightView's self-animation.
    static var flightDuration: Double { AnimationSpeed.current.flightDuration }

    static func loadImage(_ name: String, _ ext: String) -> UIImage? {
        guard let p = Bundle.main.path(forResource: name, ofType: ext) else { return nil }
        return UIImage(contentsOfFile: p)
    }

    /// The board image's intrinsic aspect (width / height), falling back to the
    /// known asset ratio if the image can't be loaded.
    static let imageAspect: CGFloat = {
        guard let img = boardImage, img.size.height > 0 else { return 1079.0 / 953.0 }
        return img.size.width / img.size.height
    }()

    /// A fixed on-screen board size derived only from the (portrait-locked) screen
    /// — never from sibling views. Pinning the board to this keeps it rooted in
    /// place: it won't shrink or grow as controls (the engine-move list, status,
    /// buttons) appear and disappear between moves. The height is capped to a
    /// fraction of the screen so short devices still leave room for the controls.
    static func fixedSize(horizontalPadding pad: CGFloat = 6, maxHeightFraction: CGFloat = 0.46) -> CGSize {
        let screen = UIScreen.main.bounds.size
        let widthLimited = max(0, screen.width - pad * 2)
        let h = min(widthLimited / imageAspect, screen.height * maxHeightFraction)
        return CGSize(width: h * imageAspect, height: h)
    }

    var body: some View {
        let fixed = Self.fixedSize()
        return GeometryReader { geo in
            let rect = boardRect(in: geo.size)
            let geom = BoardGeometry(board: rect)
            ZStack(alignment: .topLeading) {
                if let img = Self.boardImage {
                    Image(uiImage: img).resizable()
                        .frame(width: rect.width, height: rect.height)
                        .position(x: rect.midX, y: rect.midY)
                } else {
                    RoundedRectangle(cornerRadius: 8).fill(Color(red: 0.36, green: 0.22, blue: 0.10))
                        .frame(width: rect.width, height: rect.height)
                        .position(x: rect.midX, y: rect.midY)
                }
                checkers(geom: geom)
                startDots(geom: geom)
                flightsLayer(geom: geom)
                selectionHighlight(geom: geom)
                if ProcessInfo.processInfo.arguments.contains("--grid") { debugGrid(geom: geom) }
            }
            .contentShape(Rectangle())
            .gesture(tapGesture(geom: geom))
            .modifier(LongPressIfNeeded(enabled: onLongPress != nil,
                                        didLongPress: $didLongPress,
                                        action: { fireLongPress(geom: geom) }))
        }
        // Fixed, screen-derived size so the board stays rooted — it never resizes
        // when surrounding controls appear/disappear between moves.
        .frame(width: fixed.width, height: fixed.height)
    }

    private func tapGesture(geom: BoardGeometry) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { lastTouch = $0.location }
            .onEnded { v in
                lastTouch = v.location
                // A completed long-press already handled this touch; don't also tap.
                if didLongPress { didLongPress = false; return }
                if let hit = geom.hitTest(v.location, flipped: flipped) {
                    onTap(hit.row, hit.col)
                }
            }
    }

    private func fireLongPress(geom: BoardGeometry) {
        if let hit = geom.hitTest(lastTouch, flipped: flipped) {
            onLongPress?(hit.row, hit.col)
        }
    }

    private var boardAspect: CGFloat { Self.imageAspect }

    private func boardRect(in size: CGSize) -> CGRect {
        let a = boardAspect
        var w = size.width, h = w / a
        if h > size.height { h = size.height; w = h * a }
        return CGRect(x: (size.width - w) / 2, y: (size.height - h) / 2, width: w, height: h)
    }

    @ViewBuilder
    private func checkers(geom: BoardGeometry) -> some View {
        ForEach(0..<BoardGeometry.rows, id: \.self) { row in
            ForEach(0..<BoardGeometry.cols, id: \.self) { col in
                let v = Int(board[row * BoardGeometry.cols + col])
                if v != 0 {
                    let cell = geom.cellRect(row: row, col: col, flipped: flipped)
                    let down = geom.stacksDownward(row: row, col: col, flipped: flipped)
                    checkerStack(count: abs(v), white: v > 0, cell: cell, down: down)
                }
            }
        }
    }

    @ViewBuilder
    private func checkerStack(count: Int, white: Bool, cell: CGRect, down: Bool) -> some View {
        let d = min(cell.width * 0.86, cell.height * 0.42)
        let shown = min(count, 5)
        let img = white ? Self.whitePiece : Self.blackPiece
        ForEach(0..<shown, id: \.self) { i in
            let baseY = down ? cell.minY + d / 2 + 2 : cell.maxY - d / 2 - 2
            let y = down ? baseY + CGFloat(i) * d * 0.55 : baseY - CGFloat(i) * d * 0.55
            ZStack {
                if let img {
                    Image(uiImage: img).resizable().frame(width: d, height: d)
                } else {
                    Circle().fill(white ? Color.white : Color.black)
                        .overlay(Circle().stroke(Color.gray))
                        .frame(width: d, height: d)
                }
                if i == 0 && count > 5 {
                    Text("\(count)").font(.system(size: d * 0.42, weight: .bold))
                        .foregroundColor(white ? .black : .white)
                }
            }
            .position(x: cell.midX, y: y)
        }
    }

    // The point where checker index 0 sits in a cell (stack base), plus its size.
    private func anchor(_ geom: BoardGeometry, _ row: Int, _ col: Int) -> (pt: CGPoint, d: CGFloat, down: Bool) {
        let cell = geom.cellRect(row: row, col: col, flipped: flipped)
        let d = min(cell.width * 0.86, cell.height * 0.42)
        let down = geom.stacksDownward(row: row, col: col, flipped: flipped)
        let y = down ? cell.minY + d / 2 + 2 : cell.maxY - d / 2 - 2
        return (CGPoint(x: cell.midX, y: y), d, down)
    }

    // Checkers sliding during a move. Each flight is a FlightView that animates
    // itself from start to end on appear (framework-interpolated), so the slide is
    // smooth without republishing a per-frame progress on the game object.
    @ViewBuilder
    private func flightsLayer(geom: BoardGeometry) -> some View {
        ForEach(flights) { f in
            let aFrom = f.from.map { anchor(geom, $0.0, $0.1) }
            let aTo = f.to.map { anchor(geom, $0.0, $0.1) }
            if let known = aFrom ?? aTo {
                let d = known.d
                let off = CGPoint(x: known.pt.x, y: known.pt.y + (known.down ? -d * 2.2 : d * 2.2))
                FlightView(start: aFrom?.pt ?? off, end: aTo?.pt ?? off, d: d,
                           image: f.white ? Self.whitePiece : Self.blackPiece, white: f.white,
                           fadeOut: f.to == nil, fadeIn: f.from == nil)
            }
        }
    }

    @ViewBuilder
    private func debugGrid(geom: BoardGeometry) -> some View {
        ForEach(0..<BoardGeometry.rows, id: \.self) { row in
            ForEach(0..<BoardGeometry.cols, id: \.self) { col in
                let r = geom.cellRect(row: row, col: col, flipped: false)
                Rectangle().stroke(Color.red, lineWidth: 1)
                    .frame(width: r.width, height: r.height)
                    .position(x: r.midX, y: r.midY)
            }
        }
    }

    // A green dot at the OUTER (rim-facing) end of every point a move can start from
    // this turn — just outside the slot rectangle, away from the board center. Sized
    // from the point WIDTH (the cell is half the board tall, so height isn't a useful
    // scale). Bit row*cols+col in `startMask` (engine coords).
    @ViewBuilder
    private func startDots(geom: BoardGeometry) -> some View {
        ForEach(0..<(BoardGeometry.rows * BoardGeometry.cols), id: \.self) { i in
            if (startMask >> i) & 1 == 1 {
                let row = i / BoardGeometry.cols
                let col = i % BoardGeometry.cols
                let cell = geom.cellRect(row: row, col: col, flipped: flipped)
                let down = geom.stacksDownward(row: row, col: col, flipped: flipped)
                let dia = max(6, cell.width * 0.25)
                // Top-row points open downward, so their outer edge is the top (minY);
                // bottom-row points the opposite. Sit the dot just past that edge.
                let y = down ? cell.minY - dia / 2 - 1 : cell.maxY + dia / 2 + 1
                Circle().fill(Color.green)
                    .overlay(Circle().stroke(Color.white, lineWidth: 1))
                    .frame(width: dia, height: dia)
                    .shadow(color: .black.opacity(0.5), radius: 1, y: 0.5)
                    .position(x: cell.midX, y: y)
            }
        }
    }

    @ViewBuilder
    private func selectionHighlight(geom: BoardGeometry) -> some View {
        if let sel = selected {
            let cell = geom.cellRect(row: sel.0, col: sel.1, flipped: flipped)
            RoundedRectangle(cornerRadius: 4)
                .stroke(Color.yellow, lineWidth: 3)
                .background(RoundedRectangle(cornerRadius: 4).fill(Color.yellow.opacity(0.18)))
                .frame(width: cell.width, height: cell.height)
                .position(x: cell.midX, y: cell.midY)
        }
    }
}

/// Adds a location-aware long-press only when enabled (the play screen passes
/// enabled=false so its plain taps are never swallowed by a slow press).
private struct LongPressIfNeeded: ViewModifier {
    let enabled: Bool
    @Binding var didLongPress: Bool
    let action: () -> Void

    func body(content: Content) -> some View {
        if enabled {
            content.simultaneousGesture(
                LongPressGesture(minimumDuration: 0.4).onEnded { _ in
                    didLongPress = true
                    action()
                }
            )
        } else {
            content
        }
    }
}

/// A single sliding checker. It renders at `start`, then on appear animates itself
/// to `end` over `BoardCanvas.flightDuration`. Because the motion is a SwiftUI
/// animation local to this view (not a per-frame value republished on the game
/// object), only this view redraws each frame — the board doesn't — so the slide
/// stays smooth. Bear-off (`fadeOut`) fades while sliding off; an entering checker
/// (`fadeIn`, e.g. an undone bear-off) fades in.
private struct FlightView: View {
    let start: CGPoint
    let end: CGPoint
    let d: CGFloat
    let image: UIImage?
    let white: Bool
    let fadeOut: Bool
    let fadeIn: Bool

    @State private var progressed = false

    var body: some View {
        piece
            .frame(width: d, height: d)
            .opacity(fadeOut ? (progressed ? 0 : 1) : (fadeIn ? (progressed ? 1 : 0) : 1))
            .position(progressed ? end : start)
            .onAppear {
                withAnimation(.easeOut(duration: BoardCanvas.flightDuration)) { progressed = true }
            }
    }

    @ViewBuilder private var piece: some View {
        if let image {
            Image(uiImage: image).resizable()
        } else {
            Circle().fill(white ? Color.white : Color.black).overlay(Circle().stroke(Color.gray))
        }
    }
}
