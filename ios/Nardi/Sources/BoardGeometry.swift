import CoreGraphics

/// Maps engine board coordinates (row 0/1, col 0..11) to screen rectangles on the
/// board image, mirroring CoreEngine/SFMLRW.cpp: row 0 (white) is drawn along the
/// top with columns reversed (visualCol = 11 - col); row 1 (black) along the
/// bottom (visualCol = col); the 12 columns split into two 6-wide halves with a
/// center bar. `flipped` rotates placement 180 degrees for pass-and-play so the
/// active player's home is at the bottom (see BOARD_ART_SPEC.txt).
struct BoardGeometry {
    static let cols = 12
    static let rows = 2

    let board: CGRect          // displayed (aspect-fit) board image frame

    // Inset fractions of the displayed board image (tune to match new art).
    let padXFrac: CGFloat = 0.140
    let padYFrac: CGFloat = 0.050
    let gapFrac: CGFloat  = 0.070

    private var halfWidth: CGFloat { board.width * (1 - 2 * padXFrac - gapFrac) / 2 }
    private var innerHeight: CGFloat { board.height * (1 - 2 * padYFrac) }
    private var innerTop: CGFloat { board.minY + board.height * padYFrac }
    private var leftX: CGFloat { board.minX + board.width * padXFrac }
    private var rightX: CGFloat { leftX + halfWidth + board.width * gapFrac }
    private var cellW: CGFloat { halfWidth / CGFloat(Self.cols / 2) }
    private var cellH: CGFloat { innerHeight / CGFloat(Self.rows) }

    private func baseCellRect(row: Int, col: Int) -> CGRect {
        let visualCol = (row == 0) ? (Self.cols - 1 - col) : col
        let rightHalf = visualCol >= Self.cols / 2
        let localCol = rightHalf ? visualCol - Self.cols / 2 : visualCol
        let originX = rightHalf ? rightX : leftX
        return CGRect(x: originX + CGFloat(localCol) * cellW,
                      y: innerTop + CGFloat(row) * cellH,
                      width: cellW, height: cellH)
    }

    func cellRect(row: Int, col: Int, flipped: Bool) -> CGRect {
        let r = baseCellRect(row: row, col: col)
        guard flipped else { return r }
        // 180-degree rotation about the board image center.
        return CGRect(x: board.minX + board.maxX - r.maxX,
                      y: board.minY + board.maxY - r.maxY,
                      width: r.width, height: r.height)
    }

    /// Inverse of cellRect: which engine (row, col) does a screen point fall in?
    func hitTest(_ point: CGPoint, flipped: Bool) -> (row: Int, col: Int)? {
        var p = point
        if flipped {
            p = CGPoint(x: board.minX + board.maxX - point.x,
                        y: board.minY + board.maxY - point.y)
        }
        let inLeft = p.x >= leftX && p.x < leftX + halfWidth
        let inRight = p.x >= rightX && p.x < rightX + halfWidth
        guard (inLeft || inRight), p.y >= innerTop, p.y < innerTop + innerHeight else { return nil }
        let originX = inLeft ? leftX : rightX
        let localCol = Int((p.x - originX) / cellW)
        let row = Int((p.y - innerTop) / cellH)
        guard row >= 0, row < Self.rows, localCol >= 0, localCol < Self.cols / 2 else { return nil }
        let visualCol = localCol + (inRight ? Self.cols / 2 : 0)
        let logicalCol = (row == 0) ? (Self.cols - 1 - visualCol) : visualCol
        return (row, logicalCol)
    }

    /// True if the (possibly flipped) cell sits in the top half of the board, so
    /// its checker stack should grow downward (else upward).
    func stacksDownward(row: Int, col: Int, flipped: Bool) -> Bool {
        cellRect(row: row, col: col, flipped: flipped).midY < board.midY
    }
}
