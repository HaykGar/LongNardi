import SwiftUI

/// The landscape play screen (mockup 1c): felt surface, the vector board with its
/// on-board dice + Confirm pill, always-live Resign & Settings (top-right), an
/// undo / replay bottom bar, the pass-&-play resign response, and the game-over
/// panel. All game actions call straight through to `NardiGame` (engine-backed).
struct GameView: View {
    @EnvironmentObject var game: NardiGame
    let onQuit: () -> Void
    let onNewGame: () -> Void
    let onReview: () -> Void

    @AppStorage(BoardTheme.storageKey) private var boardThemeRaw = BoardTheme.green.rawValue
    @AppStorage(AnimationSpeed.storageKey) private var speed: AnimationSpeed = .high
    @AppStorage(BoardFlip.storageKey) private var autoFlip = true
    @AppStorage(PipDisplay.storageKey) private var showPips = true

    @State private var menuSettings = false
    @State private var menuResign = false

    /// EXPERIMENTAL: when true, Undo/Confirm float in the centre of each board half
    /// (tap targets shortened) instead of Undo living in the bottom bar. Flip to
    /// false to revert to the classic bottom-bar Undo + centred Confirm.
    private let centerHalfControls = true

    // Felt bands kept clear of the board so the chrome never clips it: a LEFT band
    // just wide enough for the centred Replay control (past the Dynamic Island,
    // which the safe area already excludes) and a RIGHT band for the settings/resign
    // column. Kept minimal so the board stretches as large as the aspect allows.
    private let leftBand: CGFloat = 82
    private let rightBand: CGFloat = 60

    private var theme: BoardTheme { BoardTheme(rawValue: boardThemeRaw) ?? .green }

    var body: some View {
        ZStack {
            RadialBackground(surface: theme.surface)

            // Stretched as large as the aspect allows within the reserved bands.
            BoardView(centerHalfControls: centerHalfControls).environmentObject(game)
                .padding(.leading, leftBand).padding(.trailing, rightBand)
                .padding(.vertical, 6)

            topRightControls
            replayControl
            statusBar

            if game.pendingResign != nil { resignResponse }
            if case .gameOver = game.phase { gameOverPanel }
        }
        .onChange(of: autoFlip) { _, _ in game.refreshFlip() }
    }

    // MARK: Top-right: resign + settings (always reachable)

    private var topRightControls: some View {
        // Settings in the top-right corner with Resign directly under it. Each opens
        // a native popover anchored to ITS OWN button (arrow points at it, taps
        // outside dismiss) — the conventional iOS pattern.
        VStack(spacing: 8) {
            CircleIconButton(systemName: "gearshape.fill",
                             kind: theme.id == "cognac" ? .woodLight : .wood) {
                menuSettings.toggle(); menuResign = false
            }
            .popover(isPresented: $menuSettings, arrowEdge: .top) {
                settingsPopover.presentationCompactAdaptation(.popover)
            }
            CircleIconButton(systemName: "flag.fill", kind: .resign, disabled: !game.canOffer) {
                menuResign.toggle(); menuSettings = false
            }
            .popover(isPresented: $menuResign, arrowEdge: .top) {
                resignPopover.presentationCompactAdaptation(.popover)
            }
        }
        .padding(.top, 12).padding(.trailing, 4)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
    }

    private var settingsPopover: some View {
        VStack(alignment: .leading, spacing: 10) {
            SectionLabel(text: "Animation speed", color: Palette.parchmentInk.opacity(0.6))
            SegmentedChips(options: AnimationSpeed.allCases, selection: $speed,
                           context: .parchment) { $0.rawValue }
            if game.isPassAndPlay {
                Toggle(isOn: $autoFlip) { Text("Flip board each turn").font(ThemeFont.bodyStrong) }
                    .toggleStyle(.flip)
            }
            Toggle(isOn: $showPips) { Text("Show pip counts").font(ThemeFont.bodyStrong) }
                .toggleStyle(.flip)
            Divider().background(Palette.parchmentEdge.opacity(0.25))
            Button { menuSettings = false; onQuit() } label: {
                Label("Quit to main menu", systemImage: "house")
                    .font(ThemeFont.bodyStrong).foregroundStyle(Palette.destructiveInk)
            }
            .buttonStyle(.plain)
        }
        .padding(14).frame(width: 236).foregroundStyle(Palette.parchmentInk)
        .background(Grad.parchment)
    }

    private var resignPopover: some View {
        VStack(alignment: .leading, spacing: 8) {
            SectionLabel(text: "Offer to resign", color: Palette.parchmentInk.opacity(0.6))
            Button { menuResign = false; game.offerResign(.oin) } label: {
                Label("Single (oin)", systemImage: "flag").font(ThemeFont.bodyStrong)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.vertical, 8).padding(.horizontal, 10)
                    .background(RoundedRectangle(cornerRadius: 9).fill(Palette.parchmentEdge.opacity(0.1)))
            }
            .buttonStyle(.plain).foregroundStyle(Palette.parchmentInk)
            Button { menuResign = false; game.offerResign(.mars) } label: {
                Label("Mars (double)", systemImage: "flag.checkered").font(ThemeFont.bodyStrong)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.vertical, 8).padding(.horizontal, 10)
                    .background(RoundedRectangle(cornerRadius: 9).fill(Palette.destructiveInk.opacity(0.14)))
            }
            .buttonStyle(.plain).foregroundStyle(Palette.destructiveInk)
        }
        .padding(14).frame(width: 220).foregroundStyle(Palette.parchmentInk)
        .background(Grad.parchment)
    }

    // MARK: Replay (left felt band, centre height) + status (bottom centre)

    private var replayControl: some View {
        Button { game.replayLastMove() } label: {
            HStack(spacing: 5) {
                Image(systemName: "play.fill")
                if let d = game.lastMoveDice { Text("\(d.0)-\(d.1)") }
            }
        }
        .buttonStyle(GhostPillButtonStyle(disabled: !game.canReplay))
        .disabled(!game.canReplay)
        .padding(.leading, 2)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
    }

    private var statusBar: some View {
        VStack {
            Spacer()
            Text(game.status)
                .font(ThemeFont.caption).foregroundStyle(Palette.creamDim)
                .multilineTextAlignment(.center).lineLimit(2)
                .padding(.bottom, 8).padding(.horizontal, leftBand + rightBand)
        }
    }

    // MARK: Pass & play resign response (centre)

    private var resignResponse: some View {
        VStack(spacing: 10) {
            Text(game.status).font(ThemeFont.bodyStrong).foregroundStyle(Palette.cream)
                .multilineTextAlignment(.center).padding(.horizontal, 30)
            HStack(spacing: 44) {
                Button { game.respondResign(accept: false) } label: {
                    Image(systemName: "xmark.circle.fill").font(.system(size: 52)).foregroundStyle(.red)
                }
                Button { game.respondResign(accept: true) } label: {
                    Image(systemName: "checkmark.circle.fill").font(.system(size: 52)).foregroundStyle(.green)
                }
            }
        }
        .padding(24)
        .background(RoundedRectangle(cornerRadius: 20).fill(Color.black.opacity(0.55)))
        .shadow(radius: 20)
    }

    // MARK: Game over

    private var gameOverPanel: some View {
        let message: String = { if case let .gameOver(m) = game.phase { return m } else { return "" } }()
        return VStack(spacing: 14) {
            Text(message).font(ThemeFont.screenTitle).foregroundStyle(Palette.cream)
                .multilineTextAlignment(.center)
            HStack(spacing: 12) {
                if game.hasReview {
                    Button { onReview() } label: {
                        Label("Review", systemImage: "chart.line.uptrend.xyaxis")
                    }
                    .buttonStyle(GhostPillButtonStyle())
                }
                Button { onQuit() } label: { Label("Home", systemImage: "house") }
                    .buttonStyle(GhostPillButtonStyle())
                Button { onNewGame() } label: { Text("New Game").padding(.horizontal, 6) }
                    .buttonStyle(PrimaryCTAButtonStyle(cornerRadius: 12, minHeight: 44))
                    .frame(width: 160)
            }
        }
        .padding(28)
        .background(RoundedRectangle(cornerRadius: 22).fill(Color.black.opacity(0.62)))
        .overlay(RoundedRectangle(cornerRadius: 22).stroke(Palette.strokeWarm.opacity(0.18), lineWidth: 1))
        .shadow(radius: 24)
    }
}
