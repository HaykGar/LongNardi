import SwiftUI

/// Persisted analyzer display prefs (independently toggleable).
enum AnalyzerDisplay {
    static let showEvalKey = "nardi.showEvalBar"
    static let showSuggestionsKey = "nardi.showSuggestions"
}

/// Analyze container (mockup 2a editor + 2b analyzer). Landscape: board on the
/// left, controls on the right. Drives `AnalyzeGame` (engine-backed).
struct AnalyzeScreen: View {
    @EnvironmentObject var game: AnalyzeGame
    var onExit: () -> Void
    @AppStorage(AnimationSpeed.storageKey) private var speed: AnimationSpeed = .high
    @AppStorage(AnalyzerDisplay.showEvalKey) private var showEvalBar = true
    @AppStorage(AnalyzerDisplay.showSuggestionsKey) private var showSuggestions = true
    @AppStorage(BoardFlip.storageKey) private var autoFlip = true
    @State private var menuOpen = false

    var body: some View {
        ZStack(alignment: .topLeading) {
            RadialBackground(surface: .analysis)
            VStack(spacing: 8) {
                topBar
                if game.phase == .editing { EditorBody() } else { AnalysisBody() }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .padding(.top, 8)
            if menuOpen { menuPopover.padding(.leading, 16).padding(.top, 54) }
        }
        .onChange(of: autoFlip) { _, _ in game.refreshFlip() }
    }

    private var topBar: some View {
        HStack {
            CircleIconButton(systemName: "line.3.horizontal", kind: .wood, size: 36) { menuOpen.toggle() }
            Spacer()
            Text(game.phase == .editing ? "Set Up Position" : game.modeLabel)
                .font(ThemeFont.screenTitle).foregroundStyle(Palette.cream)
            Spacer()
            rightBarButton.frame(width: 72, alignment: .trailing)
        }
        .padding(.horizontal, 16)
    }

    @ViewBuilder private var rightBarButton: some View {
        if game.isReviewSession {
            // A review is an analysis OF THE PLAYED GAME: no position editing, so the
            // line can never be swapped for an abstract setup that has left the game.
            Button("Done") { onExit() }
                .font(ThemeFont.bodyStrong).foregroundStyle(Palette.creamDim)
        } else if game.phase == .analyzing {
            Button("Edit") { game.backToEditor(fromAnalysis: true) }
                .font(ThemeFont.bodyStrong).foregroundStyle(Palette.creamDim)
        } else if game.cameFromAnalysis {
            Button { game.resumeAnalysis() } label: { Label("Back", systemImage: "chevron.left") }
                .font(ThemeFont.bodyStrong).foregroundStyle(Palette.creamDim)
        } else {
            Button { onExit() } label: {
                Image(systemName: "xmark")
                    .font(.system(size: 15, weight: .bold))
                    .foregroundStyle(Palette.creamDim)
                    .frame(width: 44, height: 44)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Close")
        }
    }

    private var menuPopover: some View {
        ParchmentCard {
            VStack(alignment: .leading, spacing: 10) {
                SectionLabel(text: "Animation speed", color: Palette.parchmentInk.opacity(0.6))
                SegmentedChips(options: AnimationSpeed.allCases, selection: $speed, context: .parchment) { $0.rawValue }
                Toggle(isOn: $showEvalBar) { Text("Show eval bar").font(ThemeFont.bodyStrong) }.toggleStyle(.flip)
                Toggle(isOn: $showSuggestions) { Text("Show engine moves").font(ThemeFont.bodyStrong) }.toggleStyle(.flip)
                Toggle(isOn: $autoFlip) { Text("Flip board each turn").font(ThemeFont.bodyStrong) }.toggleStyle(.flip)
                Divider().background(Palette.parchmentEdge.opacity(0.25))
                Button { menuOpen = false; onExit() } label: {
                    Label(game.isReviewSession ? "Exit game review" : "Close analysis", systemImage: "xmark")
                        .font(ThemeFont.bodyStrong).foregroundStyle(Palette.destructiveInk)
                }
                .buttonStyle(.plain)
            }
        }
        .transition(.opacity)
    }
}

// MARK: - Editor (2a)

private struct EditorBody: View {
    @EnvironmentObject var game: AnalyzeGame

    var body: some View {
        HStack(alignment: .center, spacing: 18) {
            VStack(spacing: 6) {
                BoardCanvas(board: game.board, flipped: false, selected: nil,
                            whitePips: game.editorPipCounts.white, blackPips: game.editorPipCounts.black,
                            whiteOff: game.borneOff(false), blackOff: game.borneOff(true),
                            whiteEndgame: game.isEndgame(false), blackEndgame: game.isEndgame(true),
                            showPips: game.validationError == nil,
                            onTap: { r, c in game.tapCellEditor(row: r, col: c) },
                            onLongPress: { r, c in game.longPressCellEditor(row: r, col: c) })
                Text("Tap a point to add · switch to Erase to remove")
                    .font(ThemeFont.micro).foregroundStyle(Palette.muted)
            }
            .frame(maxWidth: .infinity)

            VStack(spacing: 10) {
                ScrollView {
                    VStack(alignment: .leading, spacing: 12) {
                        SectionLabel(text: "Place / Remove")
                        SegmentedChips(options: [EditTool.white, .black, .erase], selection: $game.editTool,
                                       context: .onDark,
                                       tint: { _ in .parchment }) {
                            t in t == .white ? "White" : t == .black ? "Black" : "Erase"
                        }
                        Text("On board — White \(game.placed(false)) · Black \(game.placed(true))")
                            .font(ThemeFont.micro).foregroundStyle(Palette.muted)
                            .numericSwap(game.placed(false) * 100 + game.placed(true))

                        HStack {
                            SectionLabel(text: "First to move")
                            Spacer()
                            SegmentedChips(options: [false, true], selection: $game.sideToMove, context: .onDark,
                                           tint: { _ in .parchment }) {
                                $0 ? "Black" : "White"
                            }.frame(width: 150)
                        }

                        HStack(spacing: 10) {
                            Button("Clear") { game.clearBoard() }.buttonStyle(GhostPillButtonStyle())
                            Button("Standard Start") { game.standardStart() }.buttonStyle(GhostPillButtonStyle())
                        }

                        Text(game.validationError ?? (game.modelLoaded ? "Ready to analyze." : "⚠️ model blob missing"))
                            .font(ThemeFont.caption)
                            .foregroundStyle(game.canAnalyze ? Palette.winText : Palette.muted)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .textSwap(game.validationError ?? "")
                    }
                    .padding(.trailing, 4)
                }
                // Pinned so it never scrolls out of reach.
                // Invalid: 0.4 + the style's deny shake (no .disabled, so the press reaches it).
                Button {
                    if game.canAnalyze { game.startAnalysis() }
                } label: { Label("Analyze", systemImage: "play.fill") }
                    .buttonStyle(PrimaryCTAButtonStyle(isEnabled: game.canAnalyze))
                    .opacity(game.canAnalyze ? 1 : 0.4)
            }
            .frame(width: 250)
        }
        .frame(maxHeight: .infinity)
        .padding(.horizontal, 16).padding(.bottom, 10)
    }
}

// MARK: - Analyzer (2b)

private struct AnalysisBody: View {
    @EnvironmentObject var game: AnalyzeGame
    @AppStorage(AnalyzerDisplay.showEvalKey) private var showEvalBar = true
    @AppStorage(AnalyzerDisplay.showSuggestionsKey) private var showSuggestions = true

    var body: some View {
        VStack(spacing: 6) {
            if showEvalBar {
                EvalBar(value: game.positionEval, anchor: game.originalSideName).padding(.horizontal, 16)
            }
            HStack(alignment: .center, spacing: 16) {
                BoardCanvas(board: game.board, flipped: game.flipped, selected: game.selected,
                            flights: game.flights, startMask: game.startMaskCombined,
                            whitePips: game.pipCounts.white, blackPips: game.pipCounts.black,
                            whiteOff: game.whiteOff, blackOff: game.blackOff,
                            whiteEndgame: game.whiteEndgame, blackEndgame: game.blackEndgame,
                            canConfirm: game.canConfirm,           // on-board ✓ pill when the turn is done
                            onTap: { r, c in game.tap(row: r, col: c) },
                            onConfirm: { game.confirm() })
                    .frame(maxWidth: .infinity)

                rightPanel.frame(width: 236)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .padding(.horizontal, 16).padding(.bottom, 10)
        }
    }

    private var rightPanel: some View {
        VStack(spacing: 9) {
            diceControls                                 // fixed at the top
            if game.hasGameDice {
                Toggle(isOn: Binding(get: { game.useGameDice }, set: { game.setUseGameDice($0) })) {
                    Text("Dice from game").font(ThemeFont.micro).foregroundStyle(Palette.accentSoft)
                }
                .toggleStyle(.diceFromGame)
                .disabled(game.movedThisTurn || game.isAnimating)
                .padding(.horizontal, 11).padding(.vertical, 8)
                .background(RoundedRectangle(cornerRadius: 10).fill(Palette.accentSoft.opacity(0.1)))
            }
            // Scrolls, so the bottom bar below never moves with the suggestions list.
            ScrollView {
                VStack(spacing: 8) {
                    if showSuggestions { engineMoves }
                    if game.canSaveLog {
                        Button { game.saveLoggedGame() } label: {
                            Label("Save game to History", systemImage: "square.and.arrow.down")
                        }
                        .buttonStyle(PrimaryCTAButtonStyle(cornerRadius: 10, minHeight: 40))
                    }
                    HStack(spacing: 6) {
                        if game.isThinking { ProgressView().controlSize(.mini) }
                        Text(game.status).font(ThemeFont.micro).foregroundStyle(Palette.muted)
                            .multilineTextAlignment(.center)
                            .textSwap(game.status)
                    }
                    .frame(maxWidth: .infinity, minHeight: 16)
                    if game.hasGameDice && !game.onGameLine {
                        Text("Off the game line — Back to rejoin")
                            .font(.system(size: 10, weight: .semibold))
                            .foregroundStyle(Palette.accentSoft)
                            .multilineTextAlignment(.center)
                    }
                }
            }
            .frame(maxHeight: .infinity)
            bottomControls                               // fixed at the bottom
        }
    }

    @ViewBuilder private var diceControls: some View {
        let dice = game.orderedDice
        let rollLocked = game.movedThisTurn || game.isAnimating || game.isTerminal
        HStack(spacing: 10) {
            dieWithSteppers(slot: 0, spec: dice[0])
            dieWithSteppers(slot: 1, spec: dice[1])
        }
        Button { if !rollLocked { game.randomizeDice() } } label: {
            Label("Roll random", systemImage: "arrow.triangle.2.circlepath")
                .font(ThemeFont.caption).foregroundStyle(Palette.accentSoft)
                .frame(maxWidth: .infinity).padding(.vertical, 8)
                .background(RoundedRectangle(cornerRadius: 9).fill(Palette.fillWarm.opacity(0.08)))
                .overlay(RoundedRectangle(cornerRadius: 9).stroke(Palette.strokeWarm.opacity(0.2), lineWidth: 1))
        }
        .buttonStyle(PressDepthButtonStyle(depth: .secondary, isEnabled: !rollLocked))
        .opacity(rollLocked ? 0.4 : 1)
    }

    private func dieWithSteppers(slot: Int, spec: (value: Int, hasStart: Bool, isFirst: Bool)) -> some View {
        let locked = game.useGameDice || game.movedThisTurn || game.isAnimating || game.isTerminal
        let step = { (d: Int) in if !locked { game.stepDie(slot: slot, by: d) } }
        return HStack(spacing: 4) {
            StepButton(symbol: "minus", isEnabled: !locked) { step(-1) }.opacity(locked ? 0.4 : 1)
            RollingDie(value: spec.value, rollSeq: game.rollSeq, index: slot,
                       delay: RollingDie.stagger(index: slot, diceCount: 2),
                       style: .classic, primary: spec.isFirst, disabled: !spec.hasStart,
                       size: 38, emphasis: game.tryFirst ? 1 : 0) { game.toggleTryFirst() }
            StepButton(symbol: "plus", isEnabled: !locked) { step(1) }.opacity(locked ? 0.4 : 1)
        }
    }

    @ViewBuilder private var engineMoves: some View {
        if game.diceApplied && !game.movedThisTurn && !game.topMoves.isEmpty {
            VStack(alignment: .leading, spacing: 4) {
                SectionLabel(text: "Engine moves")
                ForEach(Array(game.topMoves.enumerated()), id: \.element.id) { i, m in
                    Button { game.applyTopMove(i) } label: {
                        HStack {
                            Text("\(i + 1)").font(ThemeFont.micro).foregroundStyle(Palette.muted)
                            Text(m.label).font(.system(size: 12.5, weight: .bold).monospacedDigit())
                                .foregroundStyle(Palette.cream)
                            Spacer()
                            Image(systemName: "play.circle").foregroundStyle(Palette.muted)
                        }
                        .padding(.vertical, 8).padding(.horizontal, 11)
                        .background(RoundedRectangle(cornerRadius: 10).fill(Palette.fillWarm.opacity(0.06)))
                        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Palette.strokeWarm.opacity(0.14), lineWidth: 1))
                    }
                    .buttonStyle(PressDepthButtonStyle(depth: .row, isEnabled: !game.isAnimating))
                    .disabled(game.isAnimating)
                }
            }
        }
    }

    // Back (left, fixed) · Pass (centre, when no legal move).
    // Confirm is NOT here — it appears on the board centre when the turn is complete.
    private var bottomControls: some View {
        HStack(spacing: 10) {
            let backOff = !game.canUndo || game.isAnimating
            Button { if !backOff { game.undo() } } label: {
                Label("Back", systemImage: "chevron.backward")
            }
                .buttonStyle(GhostPillButtonStyle(disabled: backOff))
            Spacer(minLength: 0)
            if game.noLegalMoves {
                Button("Pass") { game.pass() }.buttonStyle(GhostPillButtonStyle())
            }
            Spacer(minLength: 0)
            if game.hasGameDice {
                let nextOff = !game.canFollowGame || game.isAnimating
                Button { if !nextOff { game.followGameMove() } } label: {
                    HStack(spacing: 6) { Text("Next"); Image(systemName: "chevron.forward") }
                }
                    .buttonStyle(GhostPillButtonStyle(disabled: nextOff))
            } else {
                Color.clear.frame(width: 40, height: 1)   // keep Back anchored left
            }
        }
    }
}

// MARK: - Shared bits

/// A small wood +/- stepper button.
struct StepButton: View {
    let symbol: String
    var isEnabled: Bool = true
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Image(systemName: symbol).font(.system(size: 13, weight: .heavy))
                .foregroundStyle(Palette.buttonInk)
                .frame(width: 26, height: 26)
                .background(RoundedRectangle(cornerRadius: 7).fill(Palette.fillWarm.opacity(0.09)))
                .overlay(RoundedRectangle(cornerRadius: 7).stroke(Palette.strokeWarm.opacity(0.2), lineWidth: 1))
        }
        .buttonStyle(PressDepthButtonStyle(depth: .icon, isEnabled: isEnabled))
    }
}

/// The eval bar (original-perspective), pinned to `anchor` (never hardcoded White).
struct EvalBar: View {
    let value: Float?
    let anchor: String
    var body: some View {
        let v = value
        let frac = v.map { Double(max(-2, min(2, $0)) + 2) / 4 } ?? 0.5
        return HStack(spacing: 10) {
            Text("Eval · \(anchor)").font(ThemeFont.caption).foregroundStyle(Palette.muted).fixedSize()
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.white.opacity(0.12)).frame(height: 8)
                    Capsule().fill((v ?? 0) >= 0 ? Grad.evalBar : LinearGradient.horizontal(Palette.lossFill[1], Palette.lossFill[0]))
                        .frame(width: max(0, geo.size.width * frac), height: 8)
                        .animation(Motion.anim(Motion.out(Motion.Duration.enter)), value: frac)
                }
                .frame(maxHeight: .infinity, alignment: .center)
            }.frame(height: 8)
            Text(v.map { String(format: "%+.3f", $0) } ?? "—")
                .font(ThemeFont.evalValue).foregroundStyle((v ?? 0) >= 0 ? Palette.evalPos : Palette.evalNeg).fixedSize()
                .numericSwap(v)
                .valueBump(v)
        }
    }
}
