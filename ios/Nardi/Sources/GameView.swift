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

    private var theme: BoardTheme { BoardTheme(rawValue: boardThemeRaw) ?? .green }

    var body: some View {
        ZStack {
            RadialBackground(surface: theme.surface)

            BoardView().environmentObject(game)
                .padding(.top, 10).padding(.bottom, 50).padding(.horizontal, 12)

            statusLine
            topRightControls
            bottomBar

            if game.pendingResign != nil { resignResponse }
            if case .gameOver = game.phase { gameOverPanel }
        }
        .onChange(of: autoFlip) { _, _ in game.refreshFlip() }
    }

    // MARK: Status (thin caption, top-centre)

    private var statusLine: some View {
        VStack {
            Text(game.status)
                .font(ThemeFont.caption).foregroundStyle(Palette.creamDim)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 90).padding(.top, 6)
                .frame(maxWidth: .infinity)
            Spacer()
        }
        .allowsHitTesting(false)
    }

    // MARK: Top-right: resign + settings (always reachable)

    private var topRightControls: some View {
        VStack(alignment: .trailing, spacing: 8) {
            HStack(spacing: 9) {
                CircleIconButton(systemName: "flag.fill", kind: .resign, disabled: !game.canOffer) {
                    menuResign.toggle(); menuSettings = false
                }
                CircleIconButton(systemName: "gearshape.fill",
                                 kind: theme.id == "cognac" ? .woodLight : .wood) {
                    menuSettings.toggle(); menuResign = false
                }
            }
            if menuSettings { settingsPopover }
            if menuResign { resignPopover }
            Spacer()
        }
        .padding(.top, 12).padding(.trailing, 16)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
    }

    private var settingsPopover: some View {
        ParchmentCard {
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
        }
        .transition(.opacity)
    }

    private var resignPopover: some View {
        ParchmentCard(width: 200) {
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
        }
        .transition(.opacity)
    }

    // MARK: Bottom bar: undo + replay (left)

    private var bottomBar: some View {
        VStack {
            Spacer()
            HStack(spacing: 10) {
                if game.canUndo {
                    CircleIconButton(systemName: "arrow.counterclockwise", kind: .wood, size: 38) {
                        game.undo()
                    }
                    .transition(.scale.combined(with: .opacity))
                }
                Button { game.replayLastMove() } label: {
                    Label(game.lastMoveDice.map { "Replay \($0.0)-\($0.1)" } ?? "Replay",
                          systemImage: "play.circle")
                }
                .buttonStyle(GhostPillButtonStyle(disabled: !game.canReplay))
                .disabled(!game.canReplay)
                Spacer()
            }
            .padding(.leading, 16).padding(.bottom, 10)
            .animation(.easeInOut(duration: 0.15), value: game.canUndo)
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
            HStack(spacing: 14) {
                if game.hasReview {
                    Button { onReview() } label: {
                        Label("Review", systemImage: "chart.line.uptrend.xyaxis")
                    }
                    .buttonStyle(GhostPillButtonStyle())
                }
                Button { onNewGame() } label: { Text("New Game").padding(.horizontal, 6) }
                    .buttonStyle(PrimaryCTAButtonStyle(cornerRadius: 12, minHeight: 44))
                    .frame(width: 180)
            }
        }
        .padding(28)
        .background(RoundedRectangle(cornerRadius: 22).fill(Color.black.opacity(0.62)))
        .overlay(RoundedRectangle(cornerRadius: 22).stroke(Palette.strokeWarm.opacity(0.18), lineWidth: 1))
        .shadow(radius: 24)
    }
}
