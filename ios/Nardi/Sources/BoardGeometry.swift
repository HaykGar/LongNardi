import SwiftUI

/// The four 6-slot quadrants of the landscape board, matching the mockup's DOM:
/// left leaf = topLeft over botLeft; right leaf = topRight over botRight.
enum Quadrant: CaseIterable { case topLeft, topRight, botLeft, botRight }

/// Pure coordinate math between the mockup quadrants and the engine board
/// (index = row*12 + col; value > 0 = white, < 0 = black). This reuses v1's
/// PROVEN `visual()` map verbatim — only the pixel layout changed (a uniform
/// grid instead of the old perspective art), so the mapping stays correct.
///
/// Verified (see Board.cpp / SFMLRW.cpp): row 0 (white) draws along the visual
/// TOP with columns reversed; row 1 (black) along the BOTTOM. The 180° flip
/// (Black perspective / pass-and-play) puts the perspective player's head at the
/// TOP-RIGHT — each side's head lands at topRight[5], its home in botRight.
enum BoardMap {
    static let cols = 12
    static let rows = 2

    /// engine (row,col) -> FINAL visual (vRow,vCol). Copied from v1 BoardGeometry.
    static func visual(row: Int, col: Int, flipped: Bool) -> (vRow: Int, vCol: Int) {
        var vRow = row
        var vCol = (row == 0) ? (cols - 1 - col) : col
        if flipped { vRow = 1 - vRow; vCol = cols - 1 - vCol }
        return (vRow, vCol)
    }

    /// Inverse of `visual()`: FINAL visual (vRow,vCol) -> engine (row,col).
    static func engine(vRow: Int, vCol: Int, flipped: Bool) -> (row: Int, col: Int) {
        var r = vRow, c = vCol
        if flipped { r = 1 - r; c = cols - 1 - c }       // undo flip
        let row = r
        let col = (r == 0) ? (cols - 1 - c) : c           // undo top-row reversal
        return (row, col)
    }

    /// Engine (row,col) for slot i (0..5, left→right) of a quadrant.
    static func coord(_ q: Quadrant, _ i: Int, flipped: Bool) -> (row: Int, col: Int) {
        let vRow: Int, vCol: Int
        switch q {
        case .topLeft:  vRow = 0; vCol = i
        case .botLeft:  vRow = 1; vCol = i
        case .topRight: vRow = 0; vCol = 6 + i
        case .botRight: vRow = 1; vCol = 6 + i
        }
        return engine(vRow: vRow, vCol: vCol, flipped: flipped)
    }

    /// True when the point sits on the visual TOP row (checkers stack downward).
    static func stacksDownward(row: Int, col: Int, flipped: Bool) -> Bool {
        visual(row: row, col: col, flipped: flipped).vRow == 0
    }

    /// One checker's on-screen diameter for a slot of the given size. Shared by the
    /// static stacks (PointView) and the flight layer so they line up exactly.
    static func checkerDiameter(cellW: CGFloat, cellH: CGFloat) -> CGFloat {
        min(cellW * 0.82, cellH * 0.40)
    }
    static let stackOverlap: CGFloat = 0.60   // fraction of a diameter each stacked checker advances
    static let maxShown = 5
    static let stackInset: CGFloat = 2        // gap between the board edge and the base checker

    /// Distance between consecutive checkers in a stack: 60% of a diameter, clamped
    /// so a full (maxShown) stack never leaves its half of the board.
    static func stackStep(d: CGFloat, cellH: CGFloat) -> CGFloat {
        let fit = (cellH - d - 2 * stackInset) / CGFloat(maxShown - 1)
        return max(0, min(d * stackOverlap, fit))
    }
}

/// Uniform pixel metrics for the flight/replay animation, built from the two leaf
/// frames captured (in the board coordinate space) by `LeafFrameKey`.
struct BoardMetrics {
    let leftLeaf: CGRect
    let rightLeaf: CGRect
    var ready: Bool { leftLeaf.width > 0 && rightLeaf.width > 0 }

    func cellRect(row: Int, col: Int, flipped: Bool) -> CGRect {
        let v = BoardMap.visual(row: row, col: col, flipped: flipped)
        let rightHalf = v.vCol >= 6
        let leaf = rightHalf ? rightLeaf : leftLeaf
        let localCol = rightHalf ? v.vCol - 6 : v.vCol
        let cw = leaf.width / 6, ch = leaf.height / 2
        return CGRect(x: leaf.minX + CGFloat(localCol) * cw,
                      y: leaf.minY + CGFloat(v.vRow) * ch, width: cw, height: ch)
    }

    /// Stack-base anchor + checker diameter + which half the point is in.
    func anchor(row: Int, col: Int, flipped: Bool) -> (pt: CGPoint, d: CGFloat, rightHalf: Bool)? {
        guard ready else { return nil }
        let v = BoardMap.visual(row: row, col: col, flipped: flipped)
        let cell = cellRect(row: row, col: col, flipped: flipped)
        let d = BoardMap.checkerDiameter(cellW: cell.width, cellH: cell.height)
        let down = v.vRow == 0
        let y = down ? cell.minY + d / 2 + BoardMap.stackInset : cell.maxY - d / 2 - BoardMap.stackInset
        return (CGPoint(x: cell.midX, y: y), d, v.vCol >= 6)
    }

    /// Centre of stack slot `index` (0 = base) on a point — where the index-th
    /// checker actually sits, capped at the last drawn slot.
    func slotAnchor(row: Int, col: Int, flipped: Bool, index: Int) -> (pt: CGPoint, d: CGFloat, rightHalf: Bool)? {
        guard let a = anchor(row: row, col: col, flipped: flipped) else { return nil }
        let cell = cellRect(row: row, col: col, flipped: flipped)
        let step = BoardMap.stackStep(d: a.d, cellH: cell.height)
        let k = CGFloat(min(max(0, index), BoardMap.maxShown - 1))
        let down = BoardMap.visual(row: row, col: col, flipped: flipped).vRow == 0
        return (CGPoint(x: a.pt.x, y: a.pt.y + (down ? k : -k) * step), a.d, a.rightHalf)
    }
}

/// Captures the two leaf frames (keyed by isRightHalf) so the flight layer can
/// position sliding checkers in the same "board" coordinate space.
struct LeafFrameKey: PreferenceKey {
    static var defaultValue: [Bool: CGRect] = [:]
    static func reduce(value: inout [Bool: CGRect], nextValue: () -> [Bool: CGRect]) {
        value.merge(nextValue()) { _, n in n }
    }
}
