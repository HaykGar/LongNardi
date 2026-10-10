import SwiftUI

// MARK: - Round wood icon button (☰ ⚙ ⚑ ↺)

struct CircleIconButton: View {
    enum Kind {
        case wood, woodLight, resign
        var gradient: LinearGradient {
            switch self {
            case .wood:      return .vertical(Color(hex: 0x6E4326), Color(hex: 0x4A2B16))
            case .woodLight: return .vertical(Color(hex: 0x8A5330), Color(hex: 0x5A3319))
            case .resign:    return Grad.resign
            }
        }
        var border: Color { self == .resign ? Color(hex: 0xFFBEAA, alpha: 0.3)
                                             : Palette.strokeWarm.opacity(0.25) }
        var glyph: Color  { self == .resign ? Palette.resignGlyph : Palette.iconGlyph }
    }
    let systemName: String
    var kind: Kind = .wood
    var size: CGFloat = 40
    var disabled: Bool = false
    var action: () -> Void

    var body: some View {
        Button { if !disabled { action() } } label: {
            Image(systemName: systemName)
                .font(.system(size: size * 0.42, weight: .semibold))
                .foregroundStyle(kind.glyph)
                .frame(width: size, height: size)
                .background(Circle().fill(kind.gradient))
                .overlay(Circle().stroke(kind.border, lineWidth: 1))
                .shadow(color: .black.opacity(0.4), radius: 3, y: 3)
        }
        .buttonStyle(PressDepthButtonStyle(depth: .icon, isEnabled: !disabled))
        .opacity(disabled ? 0.4 : 1)
        .accessibilityRespondsToUserInteraction(!disabled)
    }
}

// MARK: - Parchment popover container (settings / resign / analyzer menu)

struct ParchmentCard<Content: View>: View {
    var width: CGFloat? = 214
    @ViewBuilder var content: () -> Content
    var body: some View {
        content()
            .padding(12)
            .frame(width: width, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: 14).fill(Grad.parchment))
            .overlay(RoundedRectangle(cornerRadius: 14).stroke(Palette.parchmentEdge.opacity(0.3), lineWidth: 1))
            .foregroundStyle(Palette.parchmentInk)
            .shadow(color: .black.opacity(0.5), radius: 17, y: 14)
    }
}

// MARK: - Press depth (scale on touch-down, spring back on release)

enum PressDepth {
    case primary, secondary, icon, chip, die, row, quiet
    var scale: CGFloat {
        switch self {
        case .primary:   return 0.95
        case .secondary: return 0.965
        case .icon:      return 0.88
        case .chip:      return 0.93
        case .die:       return 0.90
        case .row:       return 0.985
        case .quiet:     return 0.94
        }
    }
    var release: Animation { self == .primary ? Motion.bouncy : Motion.snappy }
}

/// Applies press depth to any view given a raw `isPressed`. The press-in always
/// plays in full (>= Motion.Duration.tap) even on a very fast tap, then the
/// release spring runs. When `isEnabled` is false it never scales; a press
/// start triggers the deny shake instead.
struct PressDepthModifier: ViewModifier {
    let isPressed: Bool
    var depth: PressDepth
    var isEnabled: Bool = true
    @State private var shown = false
    @State private var pressedAt = Date.distantPast
    @State private var token = 0
    @State private var shake = 0

    func body(content: Content) -> some View {
        content
            .scaleEffect(shown ? depth.scale : 1)
            .brightness(depth == .primary && shown ? -0.1 : 0)
            .denyShake(trigger: shake)
            .onChange(of: isPressed) { _, pressed in
                token += 1
                guard isEnabled else {
                    if pressed { shake += 1 }
                    if shown { withMotion(depth.release) { shown = false } }
                    return
                }
                if pressed {
                    pressedAt = Date()
                    withMotion(Motion.press()) { shown = true }
                } else {
                    let t = token
                    let wait = max(0, Motion.Duration.tap - Date().timeIntervalSince(pressedAt))
                    DispatchQueue.main.asyncAfter(deadline: .now() + wait) {
                        guard t == token else { return }
                        withMotion(depth.release) { shown = false }
                    }
                }
            }
    }
}

extension View {
    func pressDepth(_ depth: PressDepth, isPressed: Bool, isEnabled: Bool = true) -> some View {
        modifier(PressDepthModifier(isPressed: isPressed, depth: depth, isEnabled: isEnabled))
    }
}

/// Plain button with press depth only (no chrome). For disabled controls pass
/// `isEnabled: false` and guard the action instead of using `.disabled()`, so
/// the tap still lands and shakes.
struct PressDepthButtonStyle: ButtonStyle {
    var depth: PressDepth = .secondary
    var isEnabled: Bool = true
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .contentShape(Rectangle())
            .pressDepth(depth, isPressed: configuration.isPressed, isEnabled: isEnabled)
    }
}

// MARK: - Deny shake / value bump / text swap

/// Horizontal −3 / 3 / −2 / 1 / 0 px shake over 280ms, played each time
/// `trigger` changes. No-op under Reduce Motion.
struct DenyShakeModifier<T: Equatable>: ViewModifier {
    let trigger: T
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    func body(content: Content) -> some View {
        if reduceMotion {
            content
        } else {
            content.keyframeAnimator(initialValue: CGFloat(0), trigger: trigger) { view, x in
                view.offset(x: x)
            } keyframes: { _ in
                let step = Motion.Duration.shake / 5
                LinearKeyframe(-3, duration: step)
                LinearKeyframe(3, duration: step)
                LinearKeyframe(-2, duration: step)
                LinearKeyframe(1, duration: step)
                LinearKeyframe(0, duration: step)
            }
        }
    }
}

/// Scale bump 1 → `peak` → 1 whenever `trigger` changes (counters, pip badge, die emphasis).
struct ValueBumpModifier<T: Equatable>: ViewModifier {
    let trigger: T
    var peak: CGFloat = 1.1
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    func body(content: Content) -> some View {
        if reduceMotion {
            content
        } else {
            content.keyframeAnimator(initialValue: CGFloat(1), trigger: trigger) { view, s in
                view.scaleEffect(s)
            } keyframes: { _ in
                // Handoff: 1.1x bump over 320ms total.
                CubicKeyframe(peak, duration: 0.11)
                CubicKeyframe(1, duration: 0.21)
            }
        }
    }
}

/// Crossfade + small rise when `value` changes (status text, labels).
struct TextSwapModifier<T: Hashable>: ViewModifier {
    let value: T
    func body(content: Content) -> some View {
        ZStack {
            content
                .id(value)
                .transition(.asymmetric(insertion: .opacity.combined(with: .offset(y: 4)),
                                        removal: .opacity))
        }
        .animation(Motion.anim(Motion.out(0.22)), value: value)
    }
}

extension View {
    /// Deny shake on each change of `trigger` (e.g. an Int bumped on a refused tap).
    func denyShake<T: Equatable>(trigger: T) -> some View {
        modifier(DenyShakeModifier(trigger: trigger))
    }
    /// 1 → peak → 1 scale bump on each change of `trigger`.
    func valueBump<T: Equatable>(_ trigger: T, peak: CGFloat = 1.1) -> some View {
        modifier(ValueBumpModifier(trigger: trigger, peak: peak))
    }
    /// Crossfade-rise swap of a view whose content depends on `value`.
    func textSwap<T: Hashable>(_ value: T) -> some View {
        modifier(TextSwapModifier(value: value))
    }
    /// Rolling-digit swap for numeric Text (numericText content transition).
    func numericSwap<V: Equatable>(_ value: V) -> some View {
        contentTransition(.numericText())
            .animation(Motion.anim(Motion.out()), value: value)
    }
}

// MARK: - Button styles

/// Orange gradient CTA (Play / Analyze / Start Game / Open in Analyzer).
struct PrimaryCTAButtonStyle: ButtonStyle {
    var cornerRadius: CGFloat = 14
    var minHeight: CGFloat = 48
    var isEnabled: Bool = true
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(ThemeFont.buttonLabel)
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity, minHeight: minHeight)
            .background(RoundedRectangle(cornerRadius: cornerRadius).fill(Grad.cta))
            .shadow(color: .black.opacity(0.4), radius: 8, y: 6)
            .pressDepth(.primary, isPressed: configuration.isPressed, isEnabled: isEnabled)
    }
}

/// Green confirm pill (✓ Confirm).
struct ConfirmPillButtonStyle: ButtonStyle {
    var isEnabled: Bool = true
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 15, weight: .heavy))
            .foregroundStyle(.white)
            .padding(.horizontal, 22)
            .frame(height: 40)
            .background(Capsule().fill(Grad.confirm))
            .overlay(Capsule().stroke(Color.white.opacity(0.3), lineWidth: 1))
            .shadow(color: .black.opacity(0.55), radius: 8, y: 6)
            .pressDepth(.primary, isPressed: configuration.isPressed, isEnabled: isEnabled)
    }
}

/// Dark ghost pill (Home "‹ Home" / History back / bottom-bar Replay).
struct GhostPillButtonStyle: ButtonStyle {
    var disabled: Bool = false
    var compact: Bool = false   // smaller font + padding (tutorial's tight left band)
    var depth: PressDepth = .secondary
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(compact ? .system(size: 11, weight: .bold) : ThemeFont.bodyStrong)
            .foregroundStyle(Palette.buttonInk)
            .padding(.horizontal, compact ? 6 : 16).padding(.vertical, compact ? 4 : 9)
            .background(Capsule().fill(Palette.fillWarm.opacity(0.07)))
            .overlay(Capsule().stroke(Palette.strokeWarm.opacity(0.18), lineWidth: 1))
            .opacity(disabled ? 0.4 : 1)
            .pressDepth(depth, isPressed: configuration.isPressed, isEnabled: !disabled)
    }
}

/// Quiet tutorial ribbon on Home.
struct RibbonButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .padding(.horizontal, 15).padding(.vertical, 8)
            .background(Capsule().fill(Palette.strokeWarm.opacity(0.08)))
            .overlay(Capsule().stroke(Palette.strokeWarm.opacity(0.16), lineWidth: 1))
            .foregroundStyle(Palette.toastInk)
            .pressDepth(.quiet, isPressed: configuration.isPressed)
    }
}

// MARK: - Segmented selector chips (White/Black/Erase, High/Low, Easy/Medium/Hard, first-move)

enum ChipContext { case parchment, onDark }

/// Fill/ink a chip takes when selected, so a White/Black picker can read as the
/// checker it stands for instead of the generic wood highlight.
struct ChipTint {
    let fill: Color
    let ink: Color

    static let whiteChecker = ChipTint(fill: Color(hex: 0xEAD7AB), ink: Palette.whiteCheckerCountInk)
    static let blackChecker = ChipTint(fill: Color(hex: 0x241812), ink: Palette.blackCheckerCountInk)
}

struct SegmentedChips<Option: Hashable>: View {
    let options: [Option]
    @Binding var selection: Option
    var context: ChipContext = .onDark
    /// When non-nil for an option, that option's selected state uses the tint
    /// plus a bright outline (the outline is what reads as "selected").
    var tint: ((Option) -> ChipTint?)? = nil
    let label: (Option) -> String

    var body: some View {
        HStack(spacing: context == .parchment ? 6 : 9) {
            ForEach(options, id: \.self) { opt in
                let on = (opt == selection)
                let t = tint?(opt)
                Button { withMotion(Motion.snappy) { selection = opt } } label: {
                Text(label(opt))
                    .font(.system(size: context == .parchment ? 12 : 12.5, weight: .bold))
                    .lineLimit(1).minimumScaleFactor(0.7)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, context == .parchment ? 5 : 8)
                    .background(chipBackground(on: on, tint: t))
                    .foregroundStyle(chipForeground(on: on, tint: t))
                    .overlay(chipBorder(on: on, tint: t))
                    .contentShape(Rectangle())
                }
                .buttonStyle(PressDepthButtonStyle(depth: .chip))
                .accessibilityAddTraits(on ? .isSelected : [])
            }
        }
    }

    private var radius: CGFloat { context == .parchment ? 7 : 10 }

    @ViewBuilder private func chipBackground(on: Bool, tint: ChipTint?) -> some View {
        if let tint, on {
            RoundedRectangle(cornerRadius: radius)
                .fill(tint.fill)
                .shadow(color: .black.opacity(0.4), radius: 5, y: 3)
        } else {
            switch context {
            case .parchment:
                RoundedRectangle(cornerRadius: 7)
                    .fill(on ? AnyShapeStyle(Color(hex: 0x6E4326))
                             : AnyShapeStyle(Palette.parchmentEdge.opacity(0.12)))
            case .onDark:
                RoundedRectangle(cornerRadius: 10)
                    .fill(on ? AnyShapeStyle(Grad.woodFrame)
                             : AnyShapeStyle(Palette.fillWarm.opacity(0.08)))
                    .shadow(color: on ? .black.opacity(0.4) : .clear, radius: 5, y: 3)
            }
        }
    }
    private func chipForeground(on: Bool, tint: ChipTint?) -> Color {
        if let tint, on { return tint.ink }
        switch context {
        case .parchment: return on ? Color(hex: 0xF5ECD6) : Color(hex: 0x6E4326)
        case .onDark:    return on ? Color(hex: 0xF5ECD6) : Palette.toastInk
        }
    }
    @ViewBuilder private func chipBorder(on: Bool, tint: ChipTint?) -> some View {
        if on, tint != nil {
            RoundedRectangle(cornerRadius: radius).stroke(Palette.accentSoft, lineWidth: 2)
        } else if context == .onDark, !on {
            RoundedRectangle(cornerRadius: 10).stroke(Palette.strokeWarm.opacity(0.2), lineWidth: 1)
        }
    }
}

// MARK: - Pill toggle switch (flip board / dice-from-game)

struct PillToggleStyle: ToggleStyle {
    var onColor: Color
    var offColor: Color
    var knob: Color = Color(hex: 0xF5ECD6)
    func makeBody(configuration: Configuration) -> some View {
        HStack {
            configuration.label
            Spacer(minLength: 8)
            Capsule()
                .fill(configuration.isOn ? onColor : offColor)
                .animation(Motion.anim(Motion.out(Motion.Duration.base)), value: configuration.isOn)
                .frame(width: 40, height: 23)
                .overlay(alignment: configuration.isOn ? .trailing : .leading) {
                    Circle().fill(knob).frame(width: 19, height: 19).padding(2)
                        .shadow(color: .black.opacity(0.4), radius: 1, y: 1)
                }
                .animation(Motion.anim(.spring(duration: 0.32, bounce: 0.2)), value: configuration.isOn)
        }
        .contentShape(Rectangle())
        .onTapGesture { configuration.isOn.toggle() }
        .accessibilityAddTraits(configuration.isOn ? [.isButton, .isSelected] : .isButton)
    }
}
extension ToggleStyle where Self == PillToggleStyle {
    static var flip: PillToggleStyle { .init(onColor: Color(hex: 0x6E4326),
                                             offColor: Palette.parchmentEdge.opacity(0.25)) }
    static var diceFromGame: PillToggleStyle { .init(onColor: Palette.accentTop,
                                                     offColor: Color.white.opacity(0.18), knob: .white) }
}

// MARK: - Uppercase section label ("PLACE / REMOVE", "DICE", "ENGINE MOVES")

struct SectionLabel: View {
    let text: String
    var color: Color = Palette.sectionLabel
    var body: some View {
        Text(text).font(.system(size: 10, weight: .heavy))
            .tracking(1.2).textCase(.uppercase).foregroundStyle(color)
    }
}

// MARK: - Dice (3×3 pip face) — shared by the board overlay and the analyzer panel

struct DieSpec: Equatable {
    var value: Int
    var id: Int = 0              // physical die index (stable across order swaps)
    var hasStart: Bool = true    // false => greyed (no legal start this turn)
    var isFirst: Bool = false    // the die tried first (gold accent ring)
}

/// Which of the 9 grid cells (row-major) are lit for a die face value 1...6.
func dicePips(_ value: Int) -> [Bool] {
    switch value {
    case 1: return [false,false,false, false,true, false, false,false,false]
    case 2: return [true, false,false, false,false,false, false,false,true ]
    case 3: return [true, false,false, false,true, false, false,false,true ]
    case 4: return [true, false,true,  false,false,false, true, false,true ]
    case 5: return [true, false,true,  false,true, false, true, false,true ]
    case 6: return [true, false,true,  true, false,true,  true, false,true ]
    default: return Array(repeating: false, count: 9)
    }
}

struct DieFace: View {
    let value: Int
    var style: DiceStyle = .classic
    var primary: Bool = false
    var disabled: Bool = false
    var size: CGFloat = 38
    var onTap: (() -> Void)? = nil
    /// Override the default `size * 0.2` corner radius (Review dice use 7 at 31pt).
    var cornerRadius: CGFloat? = nil
    /// Bump this (any change) to play the 1 → 1.1 → 1 emphasis bump (active-die swap).
    var emphasis: Int = 0
    /// Suppress the active ring (e.g. while rolling); it pops in when this turns back on.
    var showRing: Bool = true

    private var radius: CGFloat { cornerRadius ?? size * 0.2 }
    private var ringOn: Bool { primary && showRing }

    var body: some View {
        let pips = dicePips(value)
        let face = VStack(spacing: 0) {
            ForEach(0..<3, id: \.self) { r in
                HStack(spacing: 0) {
                    ForEach(0..<3, id: \.self) { c in
                        ZStack {
                            if pips[r * 3 + c] {
                                Circle().fill(style.pip).frame(width: size * 0.15, height: size * 0.15)
                            }
                        }
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                    }
                }
            }
        }
        .padding(size * 0.16)
        .frame(width: size, height: size)
        .background(RoundedRectangle(cornerRadius: radius).fill(style.faceGradient))
        .overlay(RoundedRectangle(cornerRadius: radius).stroke(style.border, lineWidth: 1))
        .overlay(ActiveRing(on: ringOn, radius: radius))
        .shadow(color: .black.opacity(0.4), radius: 2, y: 1)
        .opacity(disabled ? 0.4 : 1)
        .saturation(disabled ? 0 : 1)
        .valueBump(emphasis, peak: 1.1)

        if let onTap {
            Button(action: onTap) { face }.buttonStyle(PressDepthButtonStyle(depth: .die))
        } else {
            face
        }
    }
}

/// Amber active-die ring: pops in from 1.3 with a snappy spring, fades out.
private struct ActiveRing: View {
    let on: Bool
    let radius: CGFloat
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    var body: some View {
        RoundedRectangle(cornerRadius: radius)
            .stroke(Palette.dieRing, lineWidth: 2.5)
            .opacity(on ? 1 : 0)
            .animation(Motion.anim(Motion.out(Motion.Duration.quick)), value: on)
            .keyframeAnimator(initialValue: CGFloat(1), trigger: on) { view, s in
                view.scaleEffect(s)
            } keyframes: { _ in
                if on && !reduceMotion {
                    MoveKeyframe(1.3)
                    SpringKeyframe(1, duration: 0.35, spring: .init(response: 0.27, dampingRatio: 0.59))
                } else {
                    MoveKeyframe(1)
                }
            }
            .allowsHitTesting(false)
    }
}

/// A DieFace that plays the handoff's one-continuous-toss roll each time
/// `rollSeq` changes: anticipation squash, parabolic hop with ease-out spin,
/// land at 56% with a squash, one small bounce. The face flickers through
/// random values (45ms interval, +14ms per change) and shows `value` exactly
/// on landing. The active ring is hidden while rolling and pops in after.
/// Stagger: pass `delay = index * (fourDice ? 0.06 : 0.09)` (see `RollingDie.stagger`).
struct RollingDie: View {
    let value: Int
    var rollSeq: Int
    var index: Int = 0
    var delay: Double = 0
    var style: DiceStyle = .classic
    var primary: Bool = false
    var disabled: Bool = false
    var size: CGFloat = 38
    var cornerRadius: CGFloat? = nil
    var emphasis: Int = 0
    var onTap: (() -> Void)? = nil

    static func stagger(index: Int, diceCount: Int) -> Double {
        Double(index) * (diceCount >= 4 ? 0.06 : 0.09)
    }

    @State private var rollStart: Date? = nil
    @State private var token = 0
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        TimelineView(.animation(paused: rollStart == nil)) { ctx in
            let t = rollStart.map { ctx.date.timeIntervalSince($0) } ?? Motion.Duration.roll
            let u = min(1, max(0, t / Motion.Duration.roll))
            let p = RollingDie.pose(u: u, k: index)
            DieFace(value: u < 0.56 ? flicker(t: max(0, t)) : value,
                    style: style, primary: primary, disabled: disabled, size: size,
                    onTap: onTap, cornerRadius: cornerRadius, emphasis: emphasis,
                    showRing: rollStart == nil)
                .rotationEffect(.degrees(p.r))
                .scaleEffect(p.sc)
                .offset(x: p.x, y: p.y)
        }
        .onChange(of: rollSeq) { _, _ in start() }
    }

    private func start() {
        guard !reduceMotion else { rollStart = nil; return }
        token += 1
        let t = token
        rollStart = Date().addingTimeInterval(delay)
        DispatchQueue.main.asyncAfter(deadline: .now() + delay + Motion.Duration.roll) {
            if t == token { rollStart = nil }
        }
    }

    /// Random in-air face; changes at cumulative 45 + 14·i ms intervals.
    private func flicker(t: Double) -> Int {
        let ms = t * 1000
        var n = 0, acc = 45.0
        while acc <= ms { n += 1; acc += 45 + 14 * Double(n) }
        var h = UInt64(truncatingIfNeeded: rollSeq &* 73856093 ^ index &* 19349663 ^ n &* 83492791)
        h = (h ^ (h >> 13)) &* 0x9E3779B97F4A7C15
        return Int((h >> 32) % 6) + 1
    }

    /// Analytic toss curve (port of nardi-motion.js roll()).
    static func pose(u: Double, k: Int) -> (x: CGFloat, y: CGFloat, sc: CGFloat, r: Double) {
        if u >= 1 { return (0, 0, 1, 0) }
        let s: Double = k % 2 == 1 ? -1 : 1
        let dx = s * -5
        var x = 0.0, y = 0.0, sc = 1.0, r = 0.0
        if u < 0.08 {
            let a0 = sin(.pi * u / 0.08)
            sc = 1 - 0.09 * a0; y = 1.5 * a0; r = -6 * s * a0
        } else {
            let rv = min(1, (u - 0.08) / 0.72)
            r = s * 360 * (1 - pow(1 - rv, 3))
            if u < 0.58 {
                let a = (u - 0.08) / 0.5
                y = -18 * 4 * a * (1 - a); sc = 1 + 0.06 * sin(.pi * a); x = dx * sin(.pi * a)
            } else if u < 0.8 {
                let b = (u - 0.58) / 0.22
                y = -4 * 4 * b * (1 - b)
            }
            sc -= 0.07 * exp(-pow((u - 0.58) / 0.035, 2)) + 0.03 * exp(-pow((u - 0.8) / 0.03, 2))
        }
        return (CGFloat(x), CGFloat(y), CGFloat(sc), r)
    }
}

// MARK: - Small shared bits

/// Home stat column (big number + tiny tracked caption).
struct StatBlock: View {
    let value: String
    let label: String
    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(value).font(ThemeFont.statNumber).foregroundStyle(Palette.cream)
            Text(label).font(.system(size: 10, weight: .heavy)).tracking(1.2)
                .foregroundStyle(Palette.muted)
        }
    }
}

/// A brief self-dismissing toast (used by the "coming soon" tutorial ribbon).
struct ComingSoonToast: View {
    let text: String
    var body: some View {
        Text(text)
            .font(ThemeFont.caption).foregroundStyle(Palette.parchmentInk)
            .padding(.horizontal, 14).padding(.vertical, 9)
            .background(Capsule().fill(Grad.parchment))
            .shadow(color: .black.opacity(0.4), radius: 8, y: 4)
            .transition(.opacity.combined(with: .move(edge: .bottom)))
    }
}
