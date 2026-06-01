#include "nardi_c_api.h"

#include <exception>
#include <new>
#include <string>

#include "nardi_engine.h"
#include "../CoreEngine/Auxilaries.h"

// Opaque handle: owns a live engine plus the last error string surfaced to C.
struct NardiHandle
{
    nardi_py::NardiEngine engine;
    std::string last_error;
};

namespace
{

void fill_board(const Nardi::BoardConfig& board, signed char* out)
{
    for(int r = 0; r < Nardi::ROWS; ++r)
        for(int c = 0; c < Nardi::COLS; ++c)
            out[r * Nardi::COLS + c] = static_cast<signed char>(board[static_cast<size_t>(r)][static_cast<size_t>(c)]);
}

} // namespace

// Run `body` guarded; on C++ exception, record the message and return `errret`.
#define NARDI_GUARD(h, errret, body)                                  \
    do {                                                              \
        if((h) == nullptr)                                            \
            return errret;                                            \
        try { body }                                                  \
        catch(const std::exception& e) { (h)->last_error = e.what(); return errret; } \
        catch(...) { (h)->last_error = "unknown error"; return errret; } \
    } while(0)

extern "C" {

NardiHandle* nardi_create(void)
{
    try { return new NardiHandle(); }
    catch(...) { return nullptr; }
}

void nardi_destroy(NardiHandle* h)
{
    delete h;
}

NardiStatus nardi_reset(NardiHandle* h)
{
    NARDI_GUARD(h, NARDI_ERR, {
        h->engine.reset();
        h->last_error.clear();
        return NARDI_OK;
    });
}

NardiStatus nardi_load_model(NardiHandle* h, const char* blob_path)
{
    NARDI_GUARD(h, NARDI_ERR, {
        if(blob_path == nullptr)
        {
            h->last_error = "nardi_load_model: null path";
            return NARDI_ERR;
        }
        h->engine.load_target_network(blob_path);
        return NARDI_OK;
    });
}

NardiStatus nardi_configure_players(NardiHandle* h, NardiStrategy white, NardiStrategy black)
{
    NARDI_GUARD(h, NARDI_ERR, {
        h->engine.configure_players(static_cast<nardi_py::Strategy>(white),
                                    static_cast<nardi_py::Strategy>(black));
        return NARDI_OK;
    });
}

NardiStatus nardi_set_mcts_params(NardiHandle* h, int n_sims, float temperature,
                                  int exploratory, float c_uct, float dirichlet_eps,
                                  float dirichlet_alpha, int rollouts_per_leaf)
{
    NARDI_GUARD(h, NARDI_ERR, {
        h->engine.set_mcts_params(n_sims, temperature, exploratory != 0, c_uct,
                                  dirichlet_eps, dirichlet_alpha, rollouts_per_leaf);
        return NARDI_OK;
    });
}

NardiStep nardi_advance(NardiHandle* h)
{
    NARDI_GUARD(h, NARDI_STEP_ERROR, {
        return static_cast<NardiStep>(h->engine.advance());
    });
}

int nardi_current_player(NardiHandle* h)
{
    NARDI_GUARD(h, -1, { return h->engine.current_player() ? 1 : 0; });
}

int nardi_sign(NardiHandle* h)
{
    NARDI_GUARD(h, 0, { return h->engine.sign(); });
}

int nardi_is_terminal(NardiHandle* h)
{
    NARDI_GUARD(h, -1, { return h->engine.is_terminal() ? 1 : 0; });
}

int nardi_should_continue(NardiHandle* h)
{
    NARDI_GUARD(h, -1, { return h->engine.should_continue_game() ? 1 : 0; });
}

int nardi_winner_result(NardiHandle* h)
{
    NARDI_GUARD(h, -1, { return h->engine.winner_result(); });
}

NardiStatus nardi_dice(NardiHandle* h, int out_dice[2])
{
    NARDI_GUARD(h, NARDI_ERR, {
        if(out_dice == nullptr) { h->last_error = "nardi_dice: null out"; return NARDI_ERR; }
        const auto d = h->engine.dice_values();
        out_dice[0] = d[0];
        out_dice[1] = d[1];
        return NARDI_OK;
    });
}

NardiStatus nardi_board(NardiHandle* h, signed char* out)
{
    NARDI_GUARD(h, NARDI_ERR, {
        if(out == nullptr) { h->last_error = "nardi_board: null out"; return NARDI_ERR; }
        fill_board(h->engine.board_features().raw_data, out);
        return NARDI_OK;
    });
}

int nardi_legal_move_count(NardiHandle* h)
{
    NARDI_GUARD(h, -1, { return h->engine.legal_move_count(); });
}

NardiStatus nardi_option_board(NardiHandle* h, int idx, signed char* out)
{
    NARDI_GUARD(h, NARDI_ERR, {
        if(out == nullptr) { h->last_error = "nardi_option_board: null out"; return NARDI_ERR; }
        const auto options = h->engine.current_options();
        if(idx < 0 || static_cast<size_t>(idx) >= options.size())
        {
            h->last_error = "nardi_option_board: index out of range";
            return NARDI_ERR;
        }
        fill_board(options[static_cast<size_t>(idx)].raw_data, out);
        return NARDI_OK;
    });
}

NardiStatus nardi_apply_human_move(NardiHandle* h, int idx)
{
    NARDI_GUARD(h, NARDI_ERR, {
        h->engine.apply_human_move(idx);
        return NARDI_OK;
    });
}

const char* nardi_last_error(NardiHandle* h)
{
    if(h == nullptr)
        return "null handle";
    return h->last_error.c_str();
}

} // extern "C"
