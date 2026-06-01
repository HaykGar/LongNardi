import SwiftUI

struct ContentView: View {
    @EnvironmentObject var game: NardiGame
    @State private var mode: GameMode = .vsComputer
    @State private var opponent: Opponent = .greedy
    @State private var first: FirstMove = .first

    var body: some View {
        Group {
            if game.phase == .setup {
                setupScreen
            } else {
                gameScreen
            }
        }
        .onAppear {
            let args = ProcessInfo.processInfo.arguments
            if args.contains("--demo") {
                game.newGame(mode: .vsComputer, opponent: .greedy,
                             first: args.contains("--black") ? .second : .first)
            }
        }
    }

    // MARK: - Setup

    private var setupScreen: some View {
        VStack(spacing: 22) {
            Spacer()
            Text("Nardi").font(.system(size: 44, weight: .bold))
            Picker("Mode", selection: $mode) {
                ForEach(GameMode.allCases) { Text($0.rawValue).tag($0) }
            }.pickerStyle(.segmented)

            if mode == .vsComputer {
                VStack(spacing: 14) {
                    labeledPicker("Opponent", selection: $opponent, options: Opponent.allCases) { $0.rawValue }
                    labeledPicker("You", selection: $first, options: FirstMove.allCases) { $0.rawValue }
                }
            } else {
                Text("Two players, one device.\nThe board flips each turn.")
                    .multilineTextAlignment(.center).foregroundStyle(.secondary)
            }

            Button {
                game.newGame(mode: mode, opponent: opponent, first: first)
            } label: {
                Text("Start Game").font(.title3.bold()).frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent).padding(.horizontal, 40)
            Spacer()
        }
        .padding()
    }

    private func labeledPicker<T: Hashable & Identifiable>(
        _ label: String, selection: Binding<T>, options: [T], title: @escaping (T) -> String
    ) -> some View {
        HStack {
            Text(label).frame(width: 90, alignment: .leading)
            Picker(label, selection: selection) {
                ForEach(options) { Text(title($0)).tag($0) }
            }.pickerStyle(.menu)
            Spacer()
        }
        .padding(.horizontal, 30)
    }

    // MARK: - Game

    private var gameScreen: some View {
        VStack(spacing: 10) {
            HStack {
                offBadge(label: "White off", count: game.whiteOff, white: true)
                Spacer()
                Button("Menu") { game.backToSetup() }.font(.callout)
                Spacer()
                offBadge(label: "Black off", count: game.blackOff, white: false)
            }
            .padding(.horizontal)

            BoardView().environmentObject(game).padding(.horizontal, 6)

            Text(game.status).font(.callout).multilineTextAlignment(.center)
                .frame(maxWidth: .infinity, minHeight: 24)

            HStack(spacing: 24) {
                DieButton(value: game.dice.0, usable: game.dieUsable.0) { game.tapDie(0) }
                DieButton(value: game.dice.1, usable: game.dieUsable.1) { game.tapDie(1) }
                Button { game.undo() } label: {
                    Label("Undo", systemImage: "arrow.uturn.backward")
                }
                .buttonStyle(.bordered)
                .disabled(game.phase != .awaitingHuman)
            }
            .padding(.bottom, 6)

            if case let .gameOver(message) = game.phase {
                VStack(spacing: 8) {
                    Text(message).font(.title2.bold())
                    Button("New Game") { game.backToSetup() }.buttonStyle(.borderedProminent)
                }
                .padding(.bottom, 12)
            }
            Spacer(minLength: 0)
        }
        .padding(.top, 8)
    }

    private func offBadge(label: String, count: Int, white: Bool) -> some View {
        VStack(spacing: 2) {
            Text(label).font(.caption2).foregroundStyle(.secondary)
            Text("\(count)").font(.headline.monospacedDigit())
                .foregroundColor(white ? .primary : .primary)
        }
    }
}

private struct DieButton: View {
    let value: Int
    let usable: Bool
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(value > 0 ? "\(value)" : "–")
                .font(.title.monospacedDigit().bold())
                .frame(width: 52, height: 52)
                .background(RoundedRectangle(cornerRadius: 8)
                    .fill(usable ? Color(.systemGray5) : Color(.systemGray3)))
                .foregroundColor(usable ? .primary : .secondary)
                .overlay(RoundedRectangle(cornerRadius: 8)
                    .stroke(usable ? Color.accentColor : Color.clear, lineWidth: 2))
        }
        .disabled(!usable)
    }
}
