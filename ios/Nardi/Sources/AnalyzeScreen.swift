import SwiftUI

/// Top-level Analyze container: the position editor, then the analysis board.
struct AnalyzeScreen: View {
    @EnvironmentObject var game: AnalyzeGame
    var onExit: () -> Void

    var body: some View {
        VStack(spacing: 8) {
            HStack {
                Button("Menu") { onExit() }.font(.callout)
                Spacer()
                Text(game.phase == .editing ? "Set Up Position" : "Analysis")
                    .font(.headline)
                Spacer()
                if game.phase == .analyzing {
                    Button("Edit") { game.backToEditor() }.font(.callout)
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

    var body: some View {
        VStack(spacing: 8) {
            EvalBar(value: game.positionEval, anchor: game.originalSide ? "Black" : "White")
                .padding(.horizontal)

            BoardCanvas(board: game.board, flipped: game.flipped, selected: game.selected,
                        flights: game.flights,
                        onTap: { r, c in game.tap(row: r, col: c) })
                .padding(.horizontal, 6)

            diceControls

            engineMoves

            Text(game.status).font(.caption).multilineTextAlignment(.center)
                .frame(maxWidth: .infinity, minHeight: 16)

            HStack(spacing: 16) {
                if game.noLegalMoves {
                    Button("Pass") { game.pass() }.buttonStyle(.bordered)
                }
                Button { game.undo() } label: { Label("Undo", systemImage: "arrow.uturn.backward") }
                    .buttonStyle(.bordered).disabled(!game.canUndo || game.isAnimating)
                Button { game.confirm() } label: { Label("Confirm", systemImage: "checkmark") }
                    .buttonStyle(.borderedProminent).disabled(!game.canConfirm)
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
                    .buttonStyle(.plain).disabled(game.isAnimating)
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
                    AnalyzeDie(value: game.die1, usable: game.dieUsable.0) { game.tapDie(0) }
                    AnalyzeDie(value: game.die2, usable: game.dieUsable.1) { game.tapDie(1) }
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
