import SwiftUI

// MARK: - Hex helper

extension Color {
    /// 0xRRGGBB in sRGB. Every colour in the design is an sRGB hex.
    init(hex: UInt32, alpha: Double = 1) {
        self.init(.sRGB,
                  red:   Double((hex >> 16) & 0xFF) / 255,
                  green: Double((hex >> 8)  & 0xFF) / 255,
                  blue:  Double( hex        & 0xFF) / 255,
                  opacity: alpha)
    }
}

// MARK: - Palette
//
// Single source of truth for the whole v2 look. A human restyler edits here.
// Values are lifted verbatim from the Claude Design mockup (warm-wood theme).
enum Palette {
    // Text tones
    static let cream        = Color(hex: 0xF3E6CD)   // wordmark / titles / checker counts
    static let creamDim     = Color(hex: 0xC9B898)   // subtitles, "Close"/"Edit"/"Done" links
    static let muted        = Color(hex: 0x9C8768)   // small meta labels
    static let sectionLabel = Color(hex: 0xB0623A)   // uppercase tracked labels + list chevrons
    static let buttonInk    = Color(hex: 0xE7D6B4)   // text on dark ghost buttons / dice ±
    static let toastInk     = Color(hex: 0xDCC9A6)   // "Dice rolled" chip, tutorial ribbon
    static let iconGlyph    = Color(hex: 0xF0D9B0)   // ☰ ⚙ ↺ glyph
    static let resignGlyph  = Color(hex: 0xFFDCCB)   // ⚑ glyph
    static let overlayBadge = Color(hex: 0xEFE4CD)   // "Off n/15" badge text

    // Accents
    static let accentTop    = Color(hex: 0xC9762F)   // CTA gradient top
    static let accentBottom = Color(hex: 0xA3541F)   // CTA gradient bottom
    static let accentSoft   = Color(hex: 0xE9A25F)   // secondary accent: roll / show-more / graph / slider
    static let dieRing      = Color(hex: 0xE0B64A)   // active die border (try-order highlight)

    // Semantic status
    static let confirmTop    = Color(hex: 0x3EA45A)
    static let confirmBottom = Color(hex: 0x2C7D42)
    static let resignTop     = Color(hex: 0x8A2F22)
    static let resignBottom  = Color(hex: 0x5E1D15)
    static let destructiveInk = Color(hex: 0x9A2A1A)
    static let winText       = Color(hex: 0x7FD39A)
    static let lossText      = Color(hex: 0xE59B8C)
    static let evalPos       = Color(hex: 0x7FD39A)
    static let evalNeg       = Color(hex: 0xE59B8C)
    static let marsStar      = Color(hex: 0xFFD76A)

    // Parchment (popovers)
    static let parchmentTop    = Color(hex: 0xF3E8D2)
    static let parchmentBottom = Color(hex: 0xE4D3AF)
    static let parchmentInk    = Color(hex: 0x3A2410)
    static let parchmentEdge   = Color(hex: 0x5A3719)

    // Board woodwork (shared by both board art directions)
    static let woodFrameTop    = Color(hex: 0x7D4527)
    static let woodFrameBottom = Color(hex: 0x582D16)
    static let leafTop         = Color(hex: 0x8A5330)
    static let leafBottom      = Color(hex: 0x6B3D20)
    static let centerBarTop    = Color(hex: 0x4A2814)
    static let centerBarBottom = Color(hex: 0x341D0D)
    static let trayTop         = Color(hex: 0x5C1616)
    static let trayBottom      = Color(hex: 0x380C0C)
    static let pipBadgeTop     = Color(hex: 0xDCC69D)
    static let pipBadgeBottom  = Color(hex: 0xB89A63)
    static let pipBadgeInk     = Color(hex: 0x3A2410)
    static let gutterPinTop    = Color(hex: 0xE6C46A)
    static let gutterPinBottom = Color(hex: 0xA97F28)

    // Triangle points (alternate cream/dark per column parity)
    static let pointCream = Color(hex: 0xE8D1A2)
    static let pointDark  = Color(hex: 0x66391D)

    // Bear-off tray chips
    static let trayChipBlackTop    = Color(hex: 0x3A2A1C)
    static let trayChipBlackBottom = Color(hex: 0x140D07)
    static let trayChipBlackEdge   = Color(hex: 0xC99A45)
    static let trayChipWhiteTop    = Color(hex: 0xFCF5E2)
    static let trayChipWhiteBottom = Color(hex: 0xDCC38B)
    static let trayChipWhiteEdge   = Color(hex: 0x7C5A2E)

    // Checkers (vector). Radial stops incl. the mid-stop from the mockup.
    static let whiteCheckerStops = Gradient(stops: [
        .init(color: Color(hex: 0xFDF6E4), location: 0.0),
        .init(color: Color(hex: 0xEAD7AB), location: 0.66),
        .init(color: Color(hex: 0xD6BD88), location: 1.0)])
    static let blackCheckerStops = Gradient(stops: [
        .init(color: Color(hex: 0x4C3726), location: 0.0),
        .init(color: Color(hex: 0x241812), location: 0.66),
        .init(color: Color(hex: 0x120B06), location: 1.0)])
    static let whiteCheckerRing  = Color(hex: 0x5B3D1C)          // high-contrast default
    static let whiteCheckerInner = Color(hex: 0x78562A, alpha: 0.4)
    static let whiteCheckerCountInk = Color(hex: 0x5B3F1E)
    static let blackCheckerRing  = Color(hex: 0xC99A45)
    static let blackCheckerInner = Color(hex: 0xC99A45, alpha: 0.5)
    static let blackCheckerCountInk = Color(hex: 0xF2E6C8)
    static let checkerGloss      = Color.white.opacity(0.22)

    // Board affordances
    static let startDotStops = Gradient(colors: [Color(hex: 0x9CF5A6), Color(hex: 0x3AA84E)])
    static let selectionStroke = Color(hex: 0xE0B64A)

    // Reusable translucent bases (compose with .opacity(...))
    static let fillWarm   = Color(hex: 0xFFF0D7)   // rgba(255,240,215,x): ghost-button fills
    static let strokeWarm = Color(hex: 0xFFDCAA)   // rgba(255,220,170,x): ghost-button borders

    // History rows (migrated from MatchHistoryView's old private Palette)
    static let winFill  = [confirmTop, confirmBottom]
    static let lossFill = [Color(hex: 0xC2452F), Color(hex: 0x9A2F1E)]
    static let rowSelectFill   = Color(hex: 0xB0623A, alpha: 0.18)
    static let rowSelectStroke = Color(hex: 0xB0623A, alpha: 0.5)
    static let rowFill         = Color(hex: 0xFFF5E1, alpha: 0.05)
    static let rowStroke       = Color(hex: 0xFFDCAA, alpha: 0.10)
    static let bannerFill      = Color(hex: 0xB0623A, alpha: 0.16)
    static let bannerStroke    = Color(hex: 0xB0623A, alpha: 0.4)
}

// MARK: - Gradient direction helpers (map CSS angles -> SwiftUI)

extension LinearGradient {
    static func vertical(_ top: Color, _ bottom: Color) -> LinearGradient {
        LinearGradient(colors: [top, bottom], startPoint: .top, endPoint: .bottom)
    }
    static func diagonal(_ a: Color, _ b: Color) -> LinearGradient {          // ~145deg / 150deg
        LinearGradient(colors: [a, b], startPoint: .topLeading, endPoint: .bottomTrailing)
    }
    static func horizontal(_ a: Color, _ b: Color) -> LinearGradient {
        LinearGradient(colors: [a, b], startPoint: .leading, endPoint: .trailing)
    }
}

// MARK: - Named gradients

enum Grad {
    static let woodFrame = LinearGradient.diagonal(Palette.woodFrameTop, Palette.woodFrameBottom)
    static let leaf      = LinearGradient.vertical(Palette.leafTop, Palette.leafBottom)
    static let centerBar = LinearGradient.vertical(Palette.centerBarTop, Palette.centerBarBottom)
    static let tray      = LinearGradient.vertical(Palette.trayTop, Palette.trayBottom)
    static let pipBadge  = LinearGradient.vertical(Palette.pipBadgeTop, Palette.pipBadgeBottom)
    static let gutterPin = LinearGradient.vertical(Palette.gutterPinTop, Palette.gutterPinBottom)
    static let cta       = LinearGradient.vertical(Palette.accentTop, Palette.accentBottom)
    static let confirm   = LinearGradient.vertical(Palette.confirmTop, Palette.confirmBottom)
    static let resign    = LinearGradient.vertical(Palette.resignTop, Palette.resignBottom)
    static let parchment = LinearGradient.diagonal(Palette.parchmentTop, Palette.parchmentBottom)
    static let evalBar   = LinearGradient.horizontal(Palette.confirmTop, Color(hex: 0x5CC47A))
    static let winChip   = LinearGradient.vertical(Palette.winFill[0], Palette.winFill[1])
    static let lossChip  = LinearGradient.vertical(Palette.lossFill[0], Palette.lossFill[1])
    /// Home secondary nav card: wood -> transparent.
    static let homeNavCard = LinearGradient(colors: [Color(hex: 0x6E4326), Color(hex: 0x482915, alpha: 0)],
                                            startPoint: .topLeading, endPoint: .bottomTrailing)
}

// MARK: - Radial screen surfaces (the "table" / app backgrounds)

struct RadialSurface: Equatable {
    var inner: Color
    var outer: Color
    var center: UnitPoint
    var radiusFactor: CGFloat   // fraction of distance to farthest corner (CSS "%")

    static let home      = RadialSurface(inner: Color(hex: 0x3C2617), outer: Color(hex: 0x1A0F08), center: .init(x: 0.22, y: 0.24), radiusFactor: 0.90)
    static let history   = RadialSurface(inner: Color(hex: 0x33200F), outer: Color(hex: 0x160C05), center: .init(x: 0.50, y: -0.10), radiusFactor: 0.95)
    static let analysis  = RadialSurface(inner: Color(hex: 0x2C1B0E), outer: Color(hex: 0x150B04), center: .init(x: 0.50, y: -0.10), radiusFactor: 0.95)
    static let feltGreen  = RadialSurface(inner: Color(hex: 0x356451), outer: Color(hex: 0x173026), center: .init(x: 0.50, y: 0.40), radiusFactor: 0.90)
    static let feltCognac = RadialSurface(inner: Color(hex: 0x6E3F24), outer: Color(hex: 0x37200F), center: .init(x: 0.50, y: 0.38), radiusFactor: 0.92)
}

/// Full-bleed radial background. Derives endRadius from geometry so the CSS "%"
/// reproduces at any size. Keep it BEHIND content in a ZStack (it ignores safe area).
struct RadialBackground: View {
    let surface: RadialSurface
    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            let cx = surface.center.x * w, cy = surface.center.y * h
            let far = [CGPoint(x: 0, y: 0), CGPoint(x: w, y: 0),
                       CGPoint(x: 0, y: h), CGPoint(x: w, y: h)]
                .map { hypot($0.x - cx, $0.y - cy) }.max() ?? max(w, h)
            RadialGradient(gradient: Gradient(colors: [surface.inner, surface.outer]),
                           center: surface.center, startRadius: 0,
                           endRadius: max(1, far * surface.radiusFactor))
        }
        .ignoresSafeArea()
    }
}

// MARK: - Dice art

struct DiceStyle: Equatable {
    var faceTop: Color, faceBottom: Color, pip: Color, border: Color
    var faceGradient: LinearGradient { .diagonal(faceTop, faceBottom) }

    static let classic = DiceStyle(faceTop: Color(hex: 0xFFFFFF), faceBottom: Color(hex: 0xE9E6DD),
                                   pip: Color(hex: 0x2A2118), border: Color.black.opacity(0.08))
    static let bone    = DiceStyle(faceTop: Color(hex: 0xF1E6C9), faceBottom: Color(hex: 0xD9C49B),
                                   pip: Color(hex: 0x4A3320), border: Color(hex: 0x4A3320, alpha: 0.35))
}

// MARK: - Board theme = the ONE value you swap to go 1c <-> 1d

struct BoardTheme: Equatable {
    var id: String
    var surface: RadialSurface
    var dice: DiceStyle
    var iconButtonTop: Color
    var iconButtonBottom: Color

    static let green  = BoardTheme(id: "green",  surface: .feltGreen,  dice: .classic,
                                   iconButtonTop: Color(hex: 0x6E4326), iconButtonBottom: Color(hex: 0x4A2B16))
    static let cognac = BoardTheme(id: "cognac", surface: .feltCognac, dice: .bone,
                                   iconButtonTop: Color(hex: 0x8A5330), iconButtonBottom: Color(hex: 0x5A3319))
}

extension BoardTheme: RawRepresentable {
    init?(rawValue: String) { self = rawValue == "cognac" ? .cognac : .green }
    var rawValue: String { id }

    static let storageKey = "nardi.boardTheme"
}

// MARK: - Typography
//
// Swap the two base functions to bundle Cinzel / Mulish TTFs later — single point of change.
enum ThemeFont {
    static func serif(_ size: CGFloat, _ weight: Font.Weight = .semibold) -> Font {
        .system(size: size, weight: weight, design: .serif)     // ~Cinzel (New York)
    }
    static func sans(_ size: CGFloat, _ weight: Font.Weight = .regular) -> Font {
        .system(size: size, weight: weight)                     // ~Mulish (SF)
    }

    static var wordmark    : Font { serif(64, .bold) }
    static var screenTitle : Font { serif(22, .bold) }
    static var cardTitle   : Font { serif(21, .bold) }
    static var bodyText    : Font { sans(14) }
    static var bodyStrong  : Font { sans(13, .bold) }
    static var caption     : Font { sans(12, .semibold) }
    static var micro       : Font { sans(11, .semibold) }
    static var buttonLabel : Font { sans(15, .heavy) }
    static var pipBadge    : Font { sans(11, .heavy).monospacedDigit() }
    static var checkerCount: Font { sans(11, .heavy).monospacedDigit() }
    static var evalValue   : Font { sans(14, .heavy).monospacedDigit() }
    static var statNumber  : Font { sans(24, .heavy).monospacedDigit() }
}

// MARK: - Motion tokens (handoff nardi-motion.js)

/// Single source for every spring, easing and duration. Under Reduce Motion
/// `Motion.anim` collapses everything to ~1ms.
enum Motion {
    // Springs
    static let snappy = Animation.spring(response: 0.27, dampingFraction: 0.59)
    static let bouncy = Animation.spring(response: 0.31, dampingFraction: 0.46)
    static let gentle = Animation.spring(response: 0.38, dampingFraction: 0.78)

    /// Timing-curve durations (seconds).
    enum Duration {
        static let tap: Double     = 0.08
        static let quick: Double   = 0.14
        static let base: Double    = 0.20
        static let enter: Double   = 0.30
        static let move: Double    = 0.30
        static let moveLow: Double = 0.46
        static let roll: Double    = 0.54
        static let ripple: Double  = 0.34
        static let shake: Double   = 0.28
    }

    // Easings
    static func out(_ d: Double = Duration.base) -> Animation { .timingCurve(0.2, 0.8, 0.2, 1, duration: d) }
    static func exit(_ d: Double = Duration.base) -> Animation { .timingCurve(0.5, 0, 0.9, 0.4, duration: d) }
    static func inOut(_ d: Double = Duration.base) -> Animation { .timingCurve(0.45, 0, 0.2, 1, duration: d) }
    static func press(_ d: Double = Duration.tap) -> Animation { .timingCurve(0.3, 0, 0.4, 1, duration: d) }
    static func travel(_ d: Double = AnimationSpeed.flightDuration) -> Animation {
        .timingCurve(0.32, 0.08, 0.18, 1, duration: d)
    }

    /// System Reduce Motion (for non-View code; views may also read
    /// `@Environment(\.accessibilityReduceMotion)`).
    static var reduceMotion: Bool { UIAccessibility.isReduceMotionEnabled }

    /// Route an animation through Reduce Motion: returns ~1ms linear when on.
    static func anim(_ a: Animation) -> Animation {
        reduceMotion ? .linear(duration: 0.001) : a
    }
}

/// `withAnimation` that honours Reduce Motion.
@discardableResult
func withMotion<R>(_ a: Animation, _ body: () throws -> R) rethrows -> R {
    try withAnimation(Motion.anim(a), body)
}

// MARK: - Persisted user prefs (moved here from the old BoardCanvas.swift)

/// User-selectable animation speed. High = quick slides; Low = slowed for clarity.
enum AnimationSpeed: String, CaseIterable, Identifiable {
    case high = "High"
    case low  = "Low"
    var id: String { rawValue }
    /// One checker slide (handoff: 300ms High, 460ms Low); ~instant under Reduce Motion.
    var flightDuration: Double {
        if Motion.reduceMotion { return 0.001 }
        return self == .low ? Motion.Duration.moveLow : Motion.Duration.move
    }

    static let storageKey = "nardi.animationSpeed"
    static var current: AnimationSpeed {
        AnimationSpeed(rawValue: UserDefaults.standard.string(forKey: storageKey) ?? "") ?? .high
    }
    /// One checker slide, read live so a change takes effect on the next move.
    static var flightDuration: Double { current.flightDuration }
}

/// Whether the board flips to the active player's perspective each turn
/// (pass-and-play and the analyzer). Persisted; default on.
enum BoardFlip {
    static let storageKey = "nardi.autoFlip"
    static var auto: Bool { UserDefaults.standard.object(forKey: storageKey) as? Bool ?? true }
}

/// Whether pip counts are shown on the board's centre bar. Persisted; default on.
enum PipDisplay {
    static let storageKey = "nardi.showPipCounts"
    static var show: Bool { UserDefaults.standard.object(forKey: storageKey) as? Bool ?? true }
}
