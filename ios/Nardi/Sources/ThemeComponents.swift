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
        Button(action: action) {
            Image(systemName: systemName)
                .font(.system(size: size * 0.42, weight: .semibold))
                .foregroundStyle(kind.glyph)
                .frame(width: size, height: size)
                .background(Circle().fill(kind.gradient))
                .overlay(Circle().stroke(kind.border, lineWidth: 1))
                .shadow(color: .black.opacity(0.4), radius: 3, y: 3)
        }
        .buttonStyle(.plain)
        .opacity(disabled ? 0.4 : 1)
        .disabled(disabled)
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

// MARK: - Button styles

/// Orange gradient CTA (Play / Analyze / Start Game / Open in Analyzer).
struct PrimaryCTAButtonStyle: ButtonStyle {
    var cornerRadius: CGFloat = 14
    var minHeight: CGFloat = 48
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(ThemeFont.buttonLabel)
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity, minHeight: minHeight)
            .background(RoundedRectangle(cornerRadius: cornerRadius).fill(Grad.cta))
            .shadow(color: .black.opacity(0.4), radius: 8, y: 6)
            .scaleEffect(configuration.isPressed ? 0.98 : 1)
            .opacity(configuration.isPressed ? 0.85 : 1)
    }
}

/// Green confirm pill (✓ Confirm).
struct ConfirmPillButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 15, weight: .heavy))
            .foregroundStyle(.white)
            .padding(.horizontal, 22)
            .frame(height: 40)
            .background(Capsule().fill(Grad.confirm))
            .overlay(Capsule().stroke(Color.white.opacity(0.3), lineWidth: 1))
            .shadow(color: .black.opacity(0.55), radius: 8, y: 6)
            .opacity(configuration.isPressed ? 0.85 : 1)
    }
}

/// Dark ghost pill (Home "‹ Home" / History back / bottom-bar Replay).
struct GhostPillButtonStyle: ButtonStyle {
    var disabled: Bool = false
    var compact: Bool = false   // smaller font + padding (tutorial's tight left band)
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(compact ? .system(size: 11, weight: .bold) : ThemeFont.bodyStrong)
            .foregroundStyle(Palette.buttonInk)
            .padding(.horizontal, compact ? 6 : 16).padding(.vertical, compact ? 4 : 9)
            .background(Capsule().fill(Palette.fillWarm.opacity(0.07)))
            .overlay(Capsule().stroke(Palette.strokeWarm.opacity(0.18), lineWidth: 1))
            .opacity((disabled ? 0.4 : 1) * (configuration.isPressed ? 0.7 : 1))
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
            .opacity(configuration.isPressed ? 0.7 : 1)
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
                Text(label(opt))
                    .font(.system(size: context == .parchment ? 12 : 12.5, weight: .bold))
                    .lineLimit(1).minimumScaleFactor(0.7)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, context == .parchment ? 5 : 8)
                    .background(chipBackground(on: on, tint: t))
                    .foregroundStyle(chipForeground(on: on, tint: t))
                    .overlay(chipBorder(on: on, tint: t))
                    .contentShape(Rectangle())
                    .onTapGesture { selection = opt }
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
                .frame(width: 40, height: 23)
                .overlay(alignment: configuration.isOn ? .trailing : .leading) {
                    Circle().fill(knob).frame(width: 19, height: 19).padding(2)
                        .shadow(color: .black.opacity(0.4), radius: 1, y: 1)
                }
                .animation(.easeInOut(duration: 0.15), value: configuration.isOn)
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
        .background(RoundedRectangle(cornerRadius: size * 0.2).fill(style.faceGradient))
        .overlay(RoundedRectangle(cornerRadius: size * 0.2)
            .stroke(primary ? Palette.dieRing : style.border, lineWidth: primary ? 2.5 : 1))
        .shadow(color: .black.opacity(0.4), radius: 2, y: 1)
        .opacity(disabled ? 0.4 : 1)
        .saturation(disabled ? 0 : 1)

        if let onTap {
            Button(action: onTap) { face }.buttonStyle(.plain)
        } else {
            face
        }
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
