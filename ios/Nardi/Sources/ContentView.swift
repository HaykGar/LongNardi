import SwiftUI

// Board is 2 rows x 12 cols, row-major. Cell sign = owner, magnitude = checkers.
private let COLS = 12

struct ContentView: View {
    @EnvironmentObject var game: NardiGame
    @State private var opponent: Opponent = .greedy

    var body: some View {
        VStack(spacing: 16) {
            Text("Nardi").font(.largeTitle.bold())

            HStack {
                Picker("Opponent", selection: $opponent) {
                    ForEach(Opponent.allCases) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.menu)
                Button("New Game") { game.newGame(opponent: opponent) }
                    .buttonStyle(.borderedProminent)
            }

            DiceView(dice: game.dice)

            BoardView(board: game.board, cell: 26)

            Text(game.status).font(.callout).multilineTextAlignment(.center)
                .frame(maxWidth: .infinity)

            switch game.phase {
            case .awaitingHuman:
                MovePicker()
            case .botThinking:
                ProgressView().padding(.top, 4)
            case .gameOver:
                Button("Play Again") { game.newGame(opponent: opponent) }
                    .buttonStyle(.bordered)
            case .idle:
                EmptyView()
            }
            Spacer()
        }
        .padding()
        .onAppear {
            // Debug/demo hook: `simctl launch ... --demo` auto-starts a game so the
            // live board can be captured without UI tapping. No effect normally.
            if ProcessInfo.processInfo.arguments.contains("--demo") {
                game.newGame(opponent: opponent)
            }
        }
    }
}

private struct DiceView: View {
    let dice: (Int, Int)
    var body: some View {
        HStack(spacing: 10) {
            ForEach([dice.0, dice.1], id: \.self) { d in
                Text(d > 0 ? "\(d)" : "–")
                    .font(.title2.monospacedDigit().bold())
                    .frame(width: 36, height: 36)
                    .background(RoundedRectangle(cornerRadius: 6).fill(Color(.systemGray5)))
            }
        }
    }
}

private struct BoardView: View {
    let board: [Int8]
    let cell: CGFloat

    var body: some View {
        VStack(spacing: 4) {
            row(0..<COLS)
            row(COLS..<(2 * COLS))
        }
        .padding(8)
        .background(RoundedRectangle(cornerRadius: 10).fill(Color(.systemGray6)))
    }

    private func row(_ range: Range<Int>) -> some View {
        HStack(spacing: 3) {
            ForEach(range, id: \.self) { i in
                PointCell(value: i < board.count ? Int(board[i]) : 0, size: cell)
            }
        }
    }
}

private struct PointCell: View {
    let value: Int
    let size: CGFloat
    var body: some View {
        let isWhite = value > 0
        RoundedRectangle(cornerRadius: 4)
            .fill(value == 0 ? Color(.systemGray4)
                             : (isWhite ? Color.white : Color.black))
            .overlay(
                Text(value == 0 ? "" : "\(abs(value))")
                    .font(.caption2.bold())
                    .foregroundColor(isWhite ? .black : .white)
            )
            .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color(.systemGray3)))
            .frame(width: size, height: size * 1.4)
    }
}

/// Lists the legal end-of-turn options as compact mini-boards; tapping plays one.
private struct MovePicker: View {
    @EnvironmentObject var game: NardiGame
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("\(game.options.count) legal move(s):").font(.subheadline)
            ScrollView {
                VStack(spacing: 6) {
                    ForEach(Array(game.options.enumerated()), id: \.offset) { idx, b in
                        Button { game.chooseOption(idx) } label: {
                            MiniBoard(board: b)
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .frame(maxHeight: 220)
        }
    }
}

private struct MiniBoard: View {
    let board: [Int8]
    var body: some View {
        VStack(spacing: 1) {
            strip(0..<COLS)
            strip(COLS..<(2 * COLS))
        }
        .padding(5)
        .background(RoundedRectangle(cornerRadius: 6).fill(Color(.systemGray6)))
    }
    private func strip(_ range: Range<Int>) -> some View {
        HStack(spacing: 1) {
            ForEach(range, id: \.self) { i in
                let v = i < board.count ? Int(board[i]) : 0
                Rectangle()
                    .fill(v == 0 ? Color(.systemGray4) : (v > 0 ? Color.white : Color.black))
                    .overlay(Text(v == 0 ? "" : "\(abs(v))").font(.system(size: 7)).foregroundColor(v > 0 ? .black : .white))
                    .frame(width: 14, height: 16)
            }
        }
    }
}
