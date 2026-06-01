// Swift driver for the plain-C API, proving the iOS integration seam: because
// nardi_c_api.h is plain C, Swift consumes it directly through a Clang module
// map -- no Objective-C++ shim required. This is exactly how the iOS app will
// talk to the engine (the SwiftUI layer will call these same functions).
//
// Usage: nardi_swift_test <model_blob_path>

import NardiC
import Foundation

guard CommandLine.arguments.count > 1 else {
    FileHandle.standardError.write("usage: nardi_swift_test <model_blob>\n".data(using: .utf8)!)
    exit(2)
}
let blobPath = CommandLine.arguments[1]

guard let game = nardi_create() else { fatalError("nardi_create failed") }
defer { nardi_destroy(game) }

if nardi_load_model(game, blobPath) != NARDI_OK {
    fatalError("load_model: \(String(cString: nardi_last_error(game)))")
}

// Play one game; the human side picks a random legal option. Returns winner (1/2) or -1.
func play(_ white: NardiStrategy, _ black: NardiStrategy) -> Int32 {
    nardi_configure_players(game, white, black)
    nardi_reset(game)
    for _ in 0..<5000 {
        switch nardi_advance(game) {
        case NARDI_STEP_GAME_OVER:
            return nardi_winner_result(game)
        case NARDI_STEP_AWAITING_HUMAN:
            let n = nardi_legal_move_count(game)
            if n > 0 { _ = nardi_apply_human_move(game, Int32.random(in: 0..<n)) }
        case NARDI_STEP_ERROR:
            return -1
        default:
            break // BotMoved / TurnPassed
        }
    }
    return -1
}

var failures = 0
func check(_ cond: Bool, _ msg: String) {
    if !cond { FileHandle.standardError.write("FAIL: \(msg)\n".data(using: .utf8)!); failures += 1 }
}

// initial board has all 30 checkers
nardi_reset(game)
var board = [Int8](repeating: 0, count: Int(NARDI_BOARD_CELLS))
check(nardi_board(game, &board) == NARDI_OK, "read board")
check(board.reduce(0) { $0 + abs(Int($1)) } == 30, "initial board has 30 checkers")

check(play(NARDI_HUMAN, NARDI_GREEDY) > 0, "human vs greedy finishes")
check(play(NARDI_HUMAN, NARDI_LOOKAHEAD) > 0, "human vs lookahead finishes")
nardi_set_mcts_params(game, 20, 1.0, 0, 0.1, 0.25, 0.3, 0)
check(play(NARDI_MCTS, NARDI_HEURISTIC) > 0, "mcts vs heuristic finishes")

var modelWins = 0
let games = 10
for _ in 0..<games {
    let w = play(NARDI_GREEDY, NARDI_HEURISTIC)         // model is white
    if w > 0 && nardi_current_player(game) == 1 { modelWins += 1 }   // white won
}
print("Swift: greedy(model) vs heuristic: \(modelWins)/\(games) wins for model")
check(modelWins > games / 2, "model beats heuristic majority")

print(failures == 0 ? "SWIFT C API OK (Swift -> C engine)" : "\(failures) CHECK(S) FAILED")
exit(failures == 0 ? 0 : 1)
