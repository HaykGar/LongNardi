import SwiftUI

/// Renders the board image with checkers placed via BoardGeometry, handles taps
/// (mapped back to engine coords), and highlights the selected source point.
struct BoardView: View {
    @EnvironmentObject var game: NardiGame

    private static let boardImage = loadImage("BoardImg", "jpg")
    private static let whitePiece = loadImage("WhitePiece", "png")
    private static let blackPiece = loadImage("BlackPiece", "png")

    static func loadImage(_ name: String, _ ext: String) -> UIImage? {
        guard let p = Bundle.main.path(forResource: name, ofType: ext) else { return nil }
        return UIImage(contentsOfFile: p)
    }

    var body: some View {
        GeometryReader { geo in
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
                flightsLayer(geom: geom)
                selectionHighlight(geom: geom)
                if ProcessInfo.processInfo.arguments.contains("--grid") { debugGrid(geom: geom) }
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onEnded { v in
                if let hit = geom.hitTest(v.location, flipped: game.flipped) {
                    game.tap(row: hit.row, col: hit.col)
                }
            })
        }
        .aspectRatio(boardAspect, contentMode: .fit)
    }

    private var boardAspect: CGFloat {
        guard let img = Self.boardImage, img.size.height > 0 else { return 1079.0 / 953.0 }
        return img.size.width / img.size.height
    }

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
                let v = Int(game.board[row * BoardGeometry.cols + col])
                if v != 0 {
                    let cell = geom.cellRect(row: row, col: col, flipped: game.flipped)
                    let down = geom.stacksDownward(row: row, col: col, flipped: game.flipped)
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
        let cell = geom.cellRect(row: row, col: col, flipped: game.flipped)
        let d = min(cell.width * 0.86, cell.height * 0.42)
        let down = geom.stacksDownward(row: row, col: col, flipped: game.flipped)
        let y = down ? cell.minY + d / 2 + 2 : cell.maxY - d / 2 - 2
        return (CGPoint(x: cell.midX, y: y), d, down)
    }

    // Checkers sliding during a move, interpolated by game.animProgress (0->1).
    @ViewBuilder
    private func flightsLayer(geom: BoardGeometry) -> some View {
        let p = game.animProgress
        ForEach(game.flights) { f in
            if let from = f.from {
                let a = anchor(geom, from.0, from.1)
                let img = f.white ? Self.whitePiece : Self.blackPiece
                // Destination point: an arrival cell, or off-board for a bear-off.
                let dest: CGPoint = {
                    if let to = f.to { return anchor(geom, to.0, to.1).pt }
                    return CGPoint(x: a.pt.x, y: a.pt.y + (a.down ? -a.d * 2.2 : a.d * 2.2))
                }()
                let pos = CGPoint(x: a.pt.x + (dest.x - a.pt.x) * p,
                                  y: a.pt.y + (dest.y - a.pt.y) * p)
                Group {
                    if let img {
                        Image(uiImage: img).resizable().frame(width: a.d, height: a.d)
                    } else {
                        Circle().fill(f.white ? Color.white : Color.black)
                            .overlay(Circle().stroke(Color.gray)).frame(width: a.d, height: a.d)
                    }
                }
                .opacity(f.to == nil ? Double(1 - p) : 1)   // fade out a borne-off checker
                .position(pos)
            }
        }
    }

    // Debug: outline every cell (flipped=false base layout) to tune the fractions.
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

    @ViewBuilder
    private func selectionHighlight(geom: BoardGeometry) -> some View {
        if let sel = game.selected {
            let cell = geom.cellRect(row: sel.0, col: sel.1, flipped: game.flipped)
            RoundedRectangle(cornerRadius: 4)
                .stroke(Color.yellow, lineWidth: 3)
                .background(RoundedRectangle(cornerRadius: 4).fill(Color.yellow.opacity(0.18)))
                .frame(width: cell.width, height: cell.height)
                .position(x: cell.midX, y: cell.midY)
        }
    }
}
