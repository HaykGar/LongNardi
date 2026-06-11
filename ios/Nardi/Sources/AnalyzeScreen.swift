import SwiftUI

/// Persisted analyzer display prefs (independently toggleable, e.g. hide both to
/// play a live game vs vzg0 with no hints, or keep suggestions on to read its move).
enum AnalyzerDisplay {
    static let showEvalKey = "nardi.showEvalBar"
    static let showSuggestionsKey = "nardi.showSuggestions"
}

/// Top-level Analyze container: the position editor, then the analysis board.
struct AnalyzeScreen: View {
    @EnvironmentObject var game: AnalyzeGame
    var onExit: () -> Void
    @AppStorage(AnimationSpeed.storageKey) private var animSpeed: AnimationSpeed = .high
    @AppStorage(AnalyzerDisplay.showEvalKey) private var showEvalBar = true
    @AppStorage(AnalyzerDisplay.showSuggestionsKey) private var showSuggestions = true

    var body: some View {
        VStack(spacing: 8) {
            HStack {
                Menu {
                    Menu {
                        Picker("Animation Speed", selection: $animSpeed) {
                            ForEach(AnimationSpeed.allCases) { Text($0.rawValue).tag($0) }
                        }
                    } label: {
                        Label("Animation Speed: \(animSpeed.rawValue)", systemImage: "speedometer")
                    }
                    Toggle(isOn: $showEvalBar) { Label("Show eval bar", systemImage: "gauge.with.dots.needle.bottom.50percent") }
                    Toggle(isOn: $showSuggestions) { Label("Show engine moves", systemImage: "lightbulb") }
                    Divider()
                    Button { onExit() } label: { Label("Close analysis", systemImage: "xmark") }
                } label: {
                    Label("Menu", systemImage: "line.3.horizontal").font(.callout)
                }
                Spacer()
                Text(game.phase == .editing ? "Set Up Position" : "Analysis")
                    .font(.headline)
                Spacer()
                if game.phase == .analyzing {
                    Button("Edit") { game.backToEditor() }.font(.callout).disabled(game.isThinking)
                } else {
                    Color.clear.frame(width: 44, height: 1)   // balance the title
                }
            }
            .padding(.horizontal)

            if game.phase == .editing { EditorBody() } else { AnalysisBody() }
        }
        .padding(.top, 8)
    }
}

// MARK: - Editor

private struct EditorBody: View {
    @EnvironmentObject var game: AnalyzeGame

    var body: some View {
        VStack(spacing: 10) {
            HStack {
                bankBadge(white: true)
                Spacer()
                bankBadge(white: false)
            }.padding(.horizontal)

            BoardCanvas(board: game.board, flipped: false, selected: nil,
                        flights: [],
                        onTap: { r, c in game.tapCellEditor(row: r, col: c) },
                        onLongPress: { r, c in game.longPressCellEditor(row: r, col: c) })
                .padding(.horizontal, 6)

            Text("Tap to add the active colour · long-press to remove")
                .font(.caption2).foregroundStyle(.secondary)

            Picker("Active", selection: $game.activeColor) {
                Text("White").tag(false); Text("Black").tag(true)
            }.pickerStyle(.segmented).padding(.horizontal)

            HStack(spacing: 16) {
                Toggle("White endgame", isOn: Binding(
                    get: { game.endgameWhite }, set: { game.setEndgame(false, $0) })).font(.caption)
                Toggle("Black endgame", isOn: Binding(
                    get: { game.endgameBlack }, set: { game.setEndgame(true, $0) })).font(.caption)
            }.padding(.horizontal)

            HStack {
                Text("First to move").font(.caption).foregroundStyle(.secondary)
                Picker("Side", selection: $game.sideToMove) {
                    Text("White").tag(false); Text("Black").tag(true)
                }.pickerStyle(.segmented).frame(maxWidth: 180)
            }.padding(.horizontal)

            HStack(spacing: 14) {
                Button("Clear") { game.clearBoard() }.buttonStyle(.bordered)
                Button("Std start") { game.standardStart() }.buttonStyle(.bordered)
            }

            Text(game.validationError ?? (game.modelLoaded ? "Ready to analyze." : "⚠️ model blob missing"))
                .font(.caption).foregroundStyle(game.canAnalyze ? .green : .secondary)
                .multilineTextAlignment(.center).frame(minHeight: 18)

            Button { game.startAnalysis() } label: {
                Label("Analyze", systemImage: "play.fill").font(.title3.bold()).frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent).padding(.horizontal, 40)
            .disabled(!game.canAnalyze)

            Spacer(minLength: 0)
        }
    }

    private func bankBadge(white: Bool) -> some View {
        let black = !white
        return VStack(spacing: 2) {
            Text(white ? "White" : "Black").font(.caption2).foregroundStyle(.secondary)
            Text("bank \(game.bank(black))").font(.subheadline.monospacedDigit())
            if game.isEndgame(black) {
                Text("off \(game.borneOff(black))").font(.caption2).foregroundStyle(.secondary)
            }
        }
    }
}

// MARK: - Analysis

private struct AnalysisBody: View {
    @EnvironmentObject var game: AnalyzeGame
    @AppStorage(AnalyzerDisplay.showEvalKey) private var showEvalBar = true
    @AppStorage(AnalyzerDisplay.showSuggestionsKey) private var showSuggestions = true

    var body: some View {
        VStack(spacing: 8) {
            if showEvalBar {
                EvalBar(value: game.positionEval, anchor: game.originalSide ? "Black" : "White")
                    .padding(.horizontal)
            }

            BoardCanvas(board: game.board, flipped: game.flipped, selected: game.selected,
                        flights: game.flights,
                        onTap: { r, c in game.tap(row: r, col: c) })
                .padding(.horizontal, 6)

            diceControls

            if showSuggestions { engineMoves }

            // Logged a full game from the standard start — save it to History to review.
            if game.canSaveLog {
                Button { game.saveLoggedGame() } label: {
                    Label("Save game to History", systemImage: "square.and.arrow.down")
                }
                .buttonStyle(.borderedProminent).font(.callout)
            }

            // While a background dice search runs, show a thinking indicator in place
            // of the status (same frame, so nothing shifts).
            HStack(spacing: 6) {
                if game.isThinking { ProgressView().controlSize(.mini) }
                Text(game.isThinking ? "Analyzing…" : game.status)
                    .font(.caption).multilineTextAlignment(.center)
            }
            .frame(maxWidth: .infinity, minHeight: 16)

            // Stepping through the reviewed game: are we still on it, or in a line
            // that didn't happen?
            if game.hasGameDice {
                Text(game.onGameLine ? "On the game line — step forward with Follow ▸"
                                     : "Exploring a line that didn't happen — Undo to rejoin the game")
                    .font(.caption2)
                    .foregroundStyle(game.onGameLine ? Color.secondary : Color.orange)
                    .multilineTextAlignment(.center)
            }

            HStack(spacing: 16) {
                if game.noLegalMoves {
                    Button("Pass") { game.pass() }.buttonStyle(.bordered).disabled(game.isThinking)
                }
                Button { game.undo() } label: { Label("Back", systemImage: "chevron.backward") }
                    .buttonStyle(.bordered).disabled(!game.canUndo || game.isAnimating || game.isThinking)
                Button { game.confirm() } label: { Label("Confirm", systemImage: "checkmark") }
                    .buttonStyle(.borderedProminent).disabled(!game.canConfirm || game.isThinking)
                if game.hasGameDice {
                    Button { game.followGameMove() } label: { Label("Follow", systemImage: "chevron.forward.2") }
                        .buttonStyle(.bordered).disabled(!game.canFollowGame)
                }
            }
            .padding(.bottom, 6)
            Spacer(minLength: 0)
        }
    }

    // Up to 3 engine-recommended moves for the current roll (vzg0, best first),
    // shown as per-checker hops. Tap one to play it.
    @ViewBuilder
    private var engineMoves: some View {
        if game.diceApplied && !game.movedThisTurn && !game.topMoves.isEmpty {
            VStack(spacing: 3) {
                Text("Engine moves").font(.caption2).foregroundStyle(.secondary)
                ForEach(Array(game.topMoves.enumerated()), id: \.element.id) { i, m in
                    Button { game.applyTopMove(i) } label: {
                        HStack {
                            Text("\(i + 1).").font(.caption2.monospacedDigit()).foregroundStyle(.secondary)
                            Text(m.label).font(.callout.monospacedDigit())
                            Spacer()
                            Image(systemName: "play.circle").foregroundStyle(.secondary)
                        }
                        .padding(.vertical, 5).padding(.horizontal, 12)
                        .background(RoundedRectangle(cornerRadius: 6).fill(Color(.systemGray6)))
                    }
                    .buttonStyle(.plain).disabled(game.isAnimating || game.isThinking)
                }
            }
            .padding(.horizontal, 24)
        }
    }

    @ViewBuilder
    private var diceControls: some View {
        VStack(spacing: 6) {
            // From game review: follow the game's actual dice, or unlock to explore
            // alternate rolls.
            if game.hasGameDice {
                Toggle("Dice from game", isOn: Binding(
                    get: { game.useGameDice }, set: { game.setUseGameDice($0) }))
                    .font(.caption).fixedSize()
                    .disabled(game.movedThisTurn || game.isAnimating)
            }

            // Steppers: change the dice any time before a checker moves (disabled
            // while locked to game dice — toggle off to explore).
            HStack(spacing: 12) {
                Stepper("D1: \(game.die1)", value: $game.die1, in: 1...6).fixedSize()
                Stepper("D2: \(game.die2)", value: $game.die2, in: 1...6).fixedSize()
            }
            .disabled(game.useGameDice || game.movedThisTurn || game.isAnimating || game.isTerminal)
            .onChange(of: game.die1) { _, _ in game.diceChanged() }
            .onChange(of: game.die2) { _, _ in game.diceChanged() }

            // Tap a die to play the selected checker by that value.
            if game.diceApplied && !game.noLegalMoves {
                HStack(spacing: 14) {
                    AnalyzeDie(value: game.die1, usable: game.dieUsable.0 && !game.isThinking) { game.tapDie(0) }
                    AnalyzeDie(value: game.die2, usable: game.dieUsable.1 && !game.isThinking) { game.tapDie(1) }
                }
            }
        }
    }
}

/// The original-perspective eval bar, shared by analysis and (later) game review.
struct EvalBar: View {
    let value: Float?
    let anchor: String
    var body: some View {
        let v = value
        let frac = v.map { Double(max(-2, min(2, $0)) + 2) / 4 } ?? 0.5
        return VStack(spacing: 2) {
            HStack {
                Text("Eval (\(anchor))").font(.caption).foregroundStyle(.secondary)
                Spacer()
                Text(v.map { String(format: "%+.3f", $0) } ?? "—")
                    .font(.subheadline.monospacedDigit().bold())
                    .foregroundColor((v ?? 0) >= 0 ? .green : .red)
            }
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color(.systemGray5)).frame(height: 6)
                    Capsule().fill((v ?? 0) >= 0 ? Color.green : Color.red)
                        .frame(width: geo.size.width * frac, height: 6)
                }
            }.frame(height: 6)
        }
    }
}

private struct AnalyzeDie: View {
    let value: Int
    let usable: Bool
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text("\(value)").font(.title2.monospacedDigit().bold())
                .frame(width: 46, height: 46)
                .background(RoundedRectangle(cornerRadius: 8).fill(usable ? Color(.systemGray5) : Color(.systemGray3)))
                .foregroundColor(usable ? .primary : .secondary)
                .overlay(RoundedRectangle(cornerRadius: 8).stroke(usable ? Color.accentColor : .clear, lineWidth: 2))
        }.disabled(!usable)
    }
}
