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

Nardi::BoardConfig read_board(const signed char* in)
{
    Nardi::BoardConfig board{};
    for(int r = 0; r < Nardi::ROWS; ++r)
        for(int c = 0; c < Nardi::COLS; ++c)
            board[static_cast<size_t>(r)][static_cast<size_t>(c)] =
                static_cast<int8_t>(in[r * Nardi::COLS + c]);
    return board;
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

NardiStatus nardi_pip_counts(NardiHandle* h, int out_pips[2])
{
    NARDI_GUARD(h, NARDI_ERR, {
        if(out_pips == nullptr) { h->last_error = "nardi_pip_counts: null out"; return NARDI_ERR; }
        const auto p = h->engine.pip_counts();
        out_pips[0] = p[0];
        out_pips[1] = p[1];
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

NardiStatus nardi_human_select(NardiHandle* h, int row, int col)
{
    NARDI_GUARD(h, NARDI_ERR, {
        if(!h->engine.human_select(row, col))
        {
            h->last_error = "cannot select that point";
            return NARDI_ERR;
        }
        return NARDI_OK;
    });
}

NardiStatus nardi_human_move_die(NardiHandle* h, int die_idx)
{
    NARDI_GUARD(h, NARDI_ERR, {
        if(!h->engine.human_move_die(die_idx))
        {
            h->last_error = "illegal move for that die";
            return NARDI_ERR;
        }
        return NARDI_OK;
    });
}

NardiStatus nardi_human_undo(NardiHandle* h)
{
    NARDI_GUARD(h, NARDI_ERR, { h->engine.human_undo(); return NARDI_OK; });
}

int nardi_can_use_die(NardiHandle* h, int die_idx)
{
    NARDI_GUARD(h, -1, { return h->engine.can_use_die(die_idx) ? 1 : 0; });
}

int nardi_starts_mask(NardiHandle* h, int die_idx)
{
    NARDI_GUARD(h, -1, { return h->engine.starts_mask(die_idx); });
}

int nardi_start_selected(NardiHandle* h)
{
    NARDI_GUARD(h, -1, { return h->engine.start_is_selected() ? 1 : 0; });
}

NardiStatus nardi_selected_start(NardiHandle* h, int out_rc[2])
{
    NARDI_GUARD(h, NARDI_ERR, {
        if(out_rc == nullptr) { h->last_error = "nardi_selected_start: null out"; return NARDI_ERR; }
        const auto rc = h->engine.selected_start();
        out_rc[0] = rc[0];
        out_rc[1] = rc[1];
        return NARDI_OK;
    });
}

int nardi_turn_in_progress(NardiHandle* h)
{
    NARDI_GUARD(h, -1, { return h->engine.turn_in_progress() ? 1 : 0; });
}

int nardi_turn_is_complete(NardiHandle* h)
{
    NARDI_GUARD(h, -1, { return h->engine.turn_is_complete() ? 1 : 0; });
}

NardiStatus nardi_confirm_turn(NardiHandle* h)
{
    NARDI_GUARD(h, NARDI_ERR, { h->engine.confirm_turn(); return NARDI_OK; });
}

int nardi_move_count(NardiHandle* h)
{
    NARDI_GUARD(h, -1, { return static_cast<int>(h->engine.recent_moves().size()); });
}

NardiStatus nardi_get_move(NardiHandle* h, int idx, int out_move[4])
{
    NARDI_GUARD(h, NARDI_ERR, {
        if(out_move == nullptr) { h->last_error = "nardi_get_move: null out"; return NARDI_ERR; }
        const auto moves = h->engine.recent_moves();
        if(idx < 0 || static_cast<size_t>(idx) >= moves.size())
        {
            h->last_error = "nardi_get_move: index out of range";
            return NARDI_ERR;
        }
        for(int k = 0; k < 4; ++k)
            out_move[k] = moves[static_cast<size_t>(idx)][static_cast<size_t>(k)];
        return NARDI_OK;
    });
}

NardiStatus nardi_set_position(NardiHandle* h, const signed char* board, int side)
{
    NARDI_GUARD(h, NARDI_ERR, {
        if(board == nullptr) { h->last_error = "nardi_set_position: null board"; return NARDI_ERR; }
        h->engine.set_position(read_board(board), side != 0);
        return NARDI_OK;
    });
}

NardiStatus nardi_evaluate_position(NardiHandle* h, float* out_value)
{
    NARDI_GUARD(h, NARDI_ERR, {
        if(out_value == nullptr) { h->last_error = "nardi_evaluate_position: null out"; return NARDI_ERR; }
        *out_value = h->engine.evaluate_position();
        return NARDI_OK;
    });
}

int nardi_set_dice(NardiHandle* h, int d1, int d2)
{
    // Cheap counterpart to nardi_analyze_dice: roll {d1,d2} on the current position
    // and enumerate the legal whole-turn options (the same set_and_enumerate that
    // analyze_dice runs), but WITHOUT the one-ply lookahead ranking or any net
    // evaluation. Leaves the handle ready for human play (select/move_die, or
    // option_board + apply_human_move) and needs no model loaded. Also refreshes the
    // per-die start sets so analysis can show the same start highlights as play.
    NARDI_GUARD(h, -1, {
        const int n = static_cast<int>(h->engine.set_and_enumerate(d1, d2).size());
        h->engine.refresh_forced();
        return n;
    });
}

int nardi_analyze_dice(NardiHandle* h, int d1, int d2)
{
    NARDI_GUARD(h, -1, { return static_cast<int>(h->engine.analyze_dice(d1, d2).size()); });
}

int nardi_analyzed_count(NardiHandle* h)
{
    NARDI_GUARD(h, -1, { return h->engine.analyzed_count(); });
}

NardiStatus nardi_analyzed_move(NardiHandle* h, int idx, signed char* out_board, float* out_value)
{
    NARDI_GUARD(h, NARDI_ERR, {
        if(out_board == nullptr || out_value == nullptr)
        { h->last_error = "nardi_analyzed_move: null out"; return NARDI_ERR; }
        fill_board(h->engine.analyzed_board(idx), out_board);
        *out_value = h->engine.analyzed_value(idx);
        return NARDI_OK;
    });
}

NardiStatus nardi_apply_analyzed_move(NardiHandle* h, int idx)
{
    NARDI_GUARD(h, NARDI_ERR, { h->engine.apply_analyzed_move(idx); return NARDI_OK; });
}

const char* nardi_last_error(NardiHandle* h)
{
    if(h == nullptr)
        return "null handle";
    return h->last_error.c_str();
}

} // extern "C"
