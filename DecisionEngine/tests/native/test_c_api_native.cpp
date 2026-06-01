// Standalone native driver for the plain-C API. Compiled WITHOUT pybind11 or
// Python (see native_test.sh) to prove the engine core + C API link and run
// with zero Python dependency -- the configuration the iOS static lib will use.
//
// Usage: test_c_api_native <model_blob_path>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../../nardi_c_api.h"

namespace
{

int g_failures = 0;

void check(bool cond, const char* msg)
{
    if(!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_failures;
    }
}

// Play one game; human side picks legal option 0. Returns winner_result or -1.
int play(NardiHandle* h, NardiStrategy white, NardiStrategy black)
{
    check(nardi_configure_players(h, white, black) == NARDI_OK, "configure_players");
    check(nardi_reset(h) == NARDI_OK, "reset");

    for(int step = 0; step < 5000; ++step)
    {
        NardiStep s = nardi_advance(h);
        if(s == NARDI_STEP_ERROR)
        {
            std::fprintf(stderr, "advance error: %s\n", nardi_last_error(h));
            return -1;
        }
        if(s == NARDI_STEP_GAME_OVER)
            return nardi_winner_result(h);
        if(s == NARDI_STEP_AWAITING_HUMAN)
        {
            int n = nardi_legal_move_count(h);
            check(n > 0, "human options available");
            check(nardi_apply_human_move(h, 0) == NARDI_OK, "apply_human_move");
        }
    }
    return -1;
}

} // namespace

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        std::fprintf(stderr, "usage: %s <model_blob_path>\n", argv[0]);
        return 2;
    }

    NardiHandle* h = nardi_create();
    check(h != nullptr, "create handle");
    if(!h)
        return 1;

    check(nardi_load_model(h, argv[1]) == NARDI_OK, "load_model");

    // initial board has all 30 checkers
    check(nardi_reset(h) == NARDI_OK, "reset");
    signed char board[NARDI_BOARD_CELLS];
    check(nardi_board(h, board) == NARDI_OK, "read board");
    int total = 0;
    for(int i = 0; i < NARDI_BOARD_CELLS; ++i)
        total += board[i] < 0 ? -board[i] : board[i];
    check(total == 30, "initial board has 30 checkers");

    // a few configurations finish
    check(play(h, NARDI_HUMAN, NARDI_GREEDY) > 0, "human vs greedy finishes");
    check(play(h, NARDI_HUMAN, NARDI_LOOKAHEAD) > 0, "human vs lookahead finishes");
    check(play(h, NARDI_GREEDY, NARDI_HEURISTIC) > 0, "greedy vs heuristic finishes");
    nardi_set_mcts_params(h, 20, 1.0f, 0, 0.1f, 0.25f, 0.3f, 0);
    check(play(h, NARDI_MCTS, NARDI_HEURISTIC) > 0, "mcts vs heuristic finishes");

    // strength sanity: greedy(model, white) should dominate heuristic
    int model_wins = 0;
    const int games = 12;
    for(int g = 0; g < games; ++g)
    {
        int w = play(h, NARDI_GREEDY, NARDI_HEURISTIC);
        if(w > 0 && nardi_current_player(h) == 1) // white won (loser to move)
            ++model_wins;
    }
    std::printf("greedy(model) vs heuristic: %d/%d wins for model\n", model_wins, games);
    check(model_wins > games / 2, "model beats heuristic majority");

    // error path: out-of-range human move reports error without crashing
    nardi_configure_players(h, NARDI_HUMAN, NARDI_GREEDY);
    nardi_reset(h);
    nardi_advance(h);
    check(nardi_apply_human_move(h, 9999) != NARDI_OK, "bad human idx returns error");
    check(std::strlen(nardi_last_error(h)) > 0, "error message set");

    nardi_destroy(h);

    if(g_failures == 0)
        std::printf("NATIVE C API OK (zero-Python build)\n");
    else
        std::printf("%d CHECK(S) FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
