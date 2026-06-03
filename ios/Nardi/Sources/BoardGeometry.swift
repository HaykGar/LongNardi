import CoreGraphics

/// Maps engine board coordinates (row 0/1, col 0..11) to screen rectangles on the
/// board image, mirroring CoreEngine/SFMLRW.cpp: row 0 (white) is drawn along the
/// top with columns reversed (visualCol = 11 - col); row 1 (black) along the
/// bottom (visualCol = col); 12 columns split into two 6-wide halves. `flipped`
/// swaps the rows and reverses the top row (pass-and-play / black perspective) so
/// the active player's head is at the TOP-RIGHT.
///
/// BoardImg.jpg is drawn in slight perspective: the bottom points are spread a
/// little wider than the top, and the top/bottom insets differ. So the point
/// centers are anchored PER visual row + half (col0/col5 center fractions,
/// measured from the art), with vertical insets per top/bottom edge. The flip is
/// a logical row/col remap (not a pixel rotation). See BOARD_ART_SPEC.txt.
struct BoardGeometry {
    static let cols = 12
    static let rows = 2

    let board: CGRect          // displayed (aspect-fit) board image frame

    // (col0 center, col5 center) as fractions of board width, per row/half.
    private let topLeft  = (c0: CGFloat(0.0839), c5: CGFloat(0.4189))
    private let topRight = (c0: CGFloat(0.5816), c5: CGFloat(0.9161))
    private let botLeft  = (c0: CGFloat(0.0769), c5: CGFloat(0.4194))
    private let botRight = (c0: CGFloat(0.5843), c5: CGFloat(0.9231))
    // Vertical insets of the point bases (fractions of board height).
    private let padYTopFrac: CGFloat = 0.092
    private let padYBotFrac: CGFloat = 0.076

    private var innerTop: CGFloat { board.minY + board.height * padYTopFrac }
    private var innerHeight: CGFloat { board.height * (1 - padYTopFrac - padYBotFrac) }
    private var cellH: CGFloat { innerHeight / CGFloat(Self.rows) }

    private func anchor(vRow: Int, rightHalf: Bool) -> (c0: CGFloat, c5: CGFloat) {
        switch (vRow, rightHalf) {
        case (0, false): return topLeft
        case (0, true):  return topRight
        case (1, false): return botLeft
        default:         return botRight
        }
    }

    private func visual(row: Int, col: Int, flipped: Bool) -> (vRow: Int, vCol: Int) {
        var vRow = row
        var vCol = (row == 0) ? (Self.cols - 1 - col) : col
        if flipped { vRow = 1 - vRow; vCol = Self.cols - 1 - vCol }
        return (vRow, vCol)
    }

    func cellRect(row: Int, col: Int, flipped: Bool) -> CGRect {
        let v = visual(row: row, col: col, flipped: flipped)
        let rightHalf = v.vCol >= Self.cols / 2
        let localCol = rightHalf ? v.vCol - Self.cols / 2 : v.vCol
        let a = anchor(vRow: v.vRow, rightHalf: rightHalf)
        let cwFrac = (a.c5 - a.c0) / 5
        let centerFrac = a.c0 + CGFloat(localCol) * cwFrac
        let w = cwFrac * board.width
        return CGRect(x: board.minX + centerFrac * board.width - w / 2,
                      y: innerTop + CGFloat(v.vRow) * cellH,
                      width: w, height: cellH)
    }

    func stacksDownward(row: Int, col: Int, flipped: Bool) -> Bool {
        visual(row: row, col: col, flipped: flipped).vRow == 0
    }

    func hitTest(_ point: CGPoint, flipped: Bool) -> (row: Int, col: Int)? {
        guard point.y >= innerTop, point.y < innerTop + innerHeight,
              point.x >= board.minX, point.x < board.maxX else { return nil }
        var vRow = (point.y < innerTop + cellH) ? 0 : 1
        let xfrac = (point.x - board.minX) / board.width
        let rightHalf = xfrac >= 0.5
        let a = anchor(vRow: vRow, rightHalf: rightHalf)
        let cwFrac = (a.c5 - a.c0) / 5
        let localCol = Int(((xfrac - a.c0) / cwFrac).rounded())
        guard localCol >= 0, localCol < Self.cols / 2 else { return nil }
        var vCol = localCol + (rightHalf ? Self.cols / 2 : 0)
        if flipped { vRow = 1 - vRow; vCol = Self.cols - 1 - vCol }
        let row = vRow
        let col = (vRow == 0) ? (Self.cols - 1 - vCol) : vCol
        return (row, col)
    }
}
