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
            if game.phase == .analyzing {
                Button("Edit") { game.backToEditor() }
                    .font(ThemeFont.bodyStrong).foregroundStyle(Palette.creamDim)
                    .frame(width: 44)
            } else {
                Button("Close") { onExit() }
                    .font(ThemeFont.bodyStrong).foregroundStyle(Palette.creamDim)
                    .frame(width: 44)
            }
        }
        .padding(.horizontal, 16)
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
                            whiteOff: game.whiteOff, blackOff: game.blackOff,
                            whiteEndgame: game.whiteEndgame, blackEndgame: game.blackEndgame,
                            showPips: game.validationError == nil,
                            onTap: { r, c in game.tapCellEditor(row: r, col: c) },
                            onLongPress: { r, c in game.longPressCellEditor(row: r, col: c) })
                Text("Tap a point to add · switch to Erase to remove")
                    .font(ThemeFont.micro).foregroundStyle(Palette.muted)
            }
            .frame(maxWidth: .infinity)

            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    SectionLabel(text: "Place / Remove")
                    SegmentedChips(options: [EditTool.white, .black, .erase], selection: $game.editTool,
                                   context: .onDark) { t in t == .white ? "White" : t == .black ? "Black" : "Erase" }
                    Text("On board — White \(game.placed(false)) · Black \(game.placed(true))")
                        .font(ThemeFont.micro).foregroundStyle(Palette.muted)

                    SectionLabel(text: "Dice")
                    HStack(spacing: 16) {
                        editorDie(0, value: game.die1)
                        editorDie(1, value: game.die2)
                    }

                    HStack(spacing: 14) {
                        Toggle(isOn: Binding(get: { game.endgameWhite }, set: { game.setEndgame(false, $0) })) {
                            Text("White endgame").font(ThemeFont.micro)
                        }.toggleStyle(.diceFromGame)
                        Toggle(isOn: Binding(get: { game.endgameBlack }, set: { game.setEndgame(true, $0) })) {
                            Text("Black endgame").font(ThemeFont.micro)
                        }.toggleStyle(.diceFromGame)
                    }

                    HStack {
                        SectionLabel(text: "First to move")
                        Spacer()
                        SegmentedChips(options: [false, true], selection: $game.sideToMove, context: .onDark) {
                            $0 ? "Black" : "White"
                        }.frame(width: 150)
                    }

                    HStack(spacing: 10) {
                        Button("Clear") { game.clearBoard() }.buttonStyle(GhostPillButtonStyle())
                        Button("Std start") { game.standardStart() }.buttonStyle(GhostPillButtonStyle())
                    }

                    Text(game.validationError ?? (game.modelLoaded ? "Ready to analyze." : "⚠️ model blob missing"))
                        .font(ThemeFont.caption)
                        .foregroundStyle(game.canAnalyze ? Palette.winText : Palette.muted)
                        .frame(maxWidth: .infinity, alignment: .leading)

                    Button { game.startAnalysis() } label: { Label("Analyze", systemImage: "play.fill") }
                        .buttonStyle(PrimaryCTAButtonStyle())
                        .disabled(!game.canAnalyze)
                        .opacity(game.canAnalyze ? 1 : 0.5)
                }
                .padding(.trailing, 4)
            }
            .frame(width: 250)
        }
        .padding(.horizontal, 16)
    }

    private func editorDie(_ which: Int, value: Int) -> some View {
        HStack(spacing: 6) {
            StepButton(symbol: "minus") { game.stepEditorDie(which, by: -1) }
            DieFace(value: value, size: 40)
            StepButton(symbol: "plus") { game.stepEditorDie(which, by: 1) }
        }
    }
}

// MARK: - Analyzer (2b)

private struct AnalysisBody: View {
    @EnvironmentObject var game: AnalyzeGame
    @AppStorage(AnalyzerDisplay.showEvalKey) private var showEvalBar = true
    @AppStorage(AnalyzerDisplay.showSuggestionsKey) private var showSuggestions = true

    var body: some View {
        VStack(spacing: 8) {
            if showEvalBar {
                EvalBar(value: game.positionEval, anchor: game.originalSideName).padding(.horizontal, 16)
            }
            HStack(alignment: .center, spacing: 16) {
                BoardCanvas(board: game.board, flipped: game.flipped, selected: game.selected,
                            flights: game.flights, startMask: game.startMaskCombined,
                            whitePips: game.pipCounts.white, blackPips: game.pipCounts.black,
                            whiteOff: game.whiteOff, blackOff: game.blackOff,
                            whiteEndgame: game.whiteEndgame, blackEndgame: game.blackEndgame,
                            onTap: { r, c in game.tap(row: r, col: c) })
                    .frame(maxWidth: .infinity)

                rightPanel.frame(width: 236)
            }
            .padding(.horizontal, 16)
        }
    }

    private var rightPanel: some View {
        VStack(spacing: 9) {
            diceControls
            if game.hasGameDice {
                Toggle(isOn: Binding(get: { game.useGameDice }, set: { game.setUseGameDice($0) })) {
                    Text("Dice from game").font(ThemeFont.micro).foregroundStyle(Palette.accentSoft)
                }
                .toggleStyle(.diceFromGame)
                .disabled(game.movedThisTurn || game.isAnimating)
                .padding(.horizontal, 11).padding(.vertical, 8)
                .background(RoundedRectangle(cornerRadius: 10).fill(Palette.accentSoft.opacity(0.1)))
            }
            if showSuggestions { engineMoves }
            if game.canSaveLog {
                Button { game.saveLoggedGame() } label: { Label("Save game to History", systemImage: "square.and.arrow.down") }
                    .buttonStyle(PrimaryCTAButtonStyle(cornerRadius: 10, minHeight: 40))
            }
            HStack(spacing: 6) {
                if game.isThinking { ProgressView().controlSize(.mini) }
                Text(game.status).font(ThemeFont.micro).foregroundStyle(Palette.muted)
                    .multilineTextAlignment(.center)
            }
            .frame(maxWidth: .infinity, minHeight: 16)
            if game.hasGameDice {
                Text(game.onGameLine ? "On the game line — Follow ▸"
                                     : "Off the game line — Back to rejoin")
                    .font(.system(size: 10, weight: .semibold))
                    .foregroundStyle(game.onGameLine ? Palette.muted : Palette.accentSoft)
                    .multilineTextAlignment(.center)
            }
            Spacer(minLength: 0)
            bottomControls
        }
    }

    @ViewBuilder private var diceControls: some View {
        let dice = game.orderedDice
        HStack(spacing: 10) {
            dieWithSteppers(slot: 0, spec: dice[0])
            dieWithSteppers(slot: 1, spec: dice[1])
        }
        Button { game.randomizeDice() } label: {
            Label("Roll random", systemImage: "arrow.triangle.2.circlepath")
                .font(ThemeFont.caption).foregroundStyle(Palette.accentSoft)
                .frame(maxWidth: .infinity).padding(.vertical, 8)
                .background(RoundedRectangle(cornerRadius: 9).fill(Palette.fillWarm.opacity(0.08)))
                .overlay(RoundedRectangle(cornerRadius: 9).stroke(Palette.strokeWarm.opacity(0.2), lineWidth: 1))
        }
        .buttonStyle(.plain)
        .disabled(game.movedThisTurn || game.isAnimating || game.isTerminal)
    }

    private func dieWithSteppers(slot: Int, spec: (value: Int, hasStart: Bool, isFirst: Bool)) -> some View {
        let locked = game.useGameDice || game.movedThisTurn || game.isAnimating || game.isTerminal
        return HStack(spacing: 4) {
            StepButton(symbol: "minus") { game.stepDie(slot: slot, by: -1) }.disabled(locked).opacity(locked ? 0.4 : 1)
            DieFace(value: spec.value, style: .classic, primary: spec.isFirst, disabled: !spec.hasStart,
                    size: 38) { game.toggleTryFirst() }
            StepButton(symbol: "plus") { game.stepDie(slot: slot, by: 1) }.disabled(locked).opacity(locked ? 0.4 : 1)
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
                    .buttonStyle(.plain).disabled(game.isAnimating)
                }
            }
        }
    }

    private var bottomControls: some View {
        HStack(spacing: 10) {
            if game.noLegalMoves { Button("Pass") { game.pass() }.buttonStyle(GhostPillButtonStyle()) }
            Button { game.undo() } label: { Label("Back", systemImage: "chevron.backward") }
                .buttonStyle(GhostPillButtonStyle(disabled: !game.canUndo || game.isAnimating))
                .disabled(!game.canUndo || game.isAnimating)
            Button { game.confirm() } label: { Label("Confirm", systemImage: "checkmark") }
                .buttonStyle(ConfirmPillButtonStyle())
                .disabled(!game.canConfirm).opacity(game.canConfirm ? 1 : 0.5)
            if game.hasGameDice {
                Button { game.followGameMove() } label: { Image(systemName: "chevron.forward.2") }
                    .buttonStyle(GhostPillButtonStyle(disabled: !game.canFollowGame))
                    .disabled(!game.canFollowGame)
            }
        }
    }
}

// MARK: - Shared bits

/// A small wood +/- stepper button.
struct StepButton: View {
    let symbol: String
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Image(systemName: symbol).font(.system(size: 13, weight: .heavy))
                .foregroundStyle(Palette.buttonInk)
                .frame(width: 26, height: 26)
                .background(RoundedRectangle(cornerRadius: 7).fill(Palette.fillWarm.opacity(0.09)))
                .overlay(RoundedRectangle(cornerRadius: 7).stroke(Palette.strokeWarm.opacity(0.2), lineWidth: 1))
        }
        .buttonStyle(.plain)
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
            Text("Eval · \(anchor)").font(ThemeFont.caption).foregroundStyle(Palette.muted)
                .fixedSize()
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.white.opacity(0.12)).frame(height: 8)
                    Capsule().fill((v ?? 0) >= 0 ? Grad.evalBar : LinearGradient.horizontal(Palette.lossFill[1], Palette.lossFill[0]))
                        .frame(width: max(0, geo.size.width * frac), height: 8)
                }
                .frame(maxHeight: .infinity, alignment: .center)
            }.frame(height: 8)
            Text(v.map { String(format: "%+.3f", $0) } ?? "—")
                .font(ThemeFont.evalValue).foregroundStyle((v ?? 0) >= 0 ? Palette.evalPos : Palette.evalNeg)
                .fixedSize()
        }
    }
}
