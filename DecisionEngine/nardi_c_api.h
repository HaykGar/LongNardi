#ifndef NARDI_C_API_H
#define NARDI_C_API_H

/*
 * Plain-C state-machine API over the Nardi C++ engine. This is the single
 * integration seam intended for non-Python consumers (notably an iOS app via a
 * thin Objective-C++ bridge). It mirrors the in-C++ match orchestrator
 * (NardiEngine::advance): the caller drives the game one step at a time, the
 * engine handles dice, bots, and turn switching internally, and the caller
 * supplies only human moves.
 *
 * All functions are exception-safe: C++ exceptions are caught at the boundary,
 * reported via nardi_last_error(), and surfaced as a NARDI_ERR return code (or a
 * NARDI_STEP_ERROR step). No C++ types cross this boundary.
 *
 * Threading: a NardiHandle is not thread-safe; use one per thread or serialize.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Board geometry: 2 rows x 12 columns, row-major. */
#define NARDI_BOARD_CELLS 24

/* Per-player move strategy (must match nardi_py::Strategy ordering). */
typedef enum
{
    NARDI_HUMAN = 0,
    NARDI_GREEDY = 1,
    NARDI_LOOKAHEAD = 2,
    NARDI_MCTS = 3,
    NARDI_HEURISTIC = 4,
    NARDI_RANDOM = 5
} NardiStrategy;

/* Result of nardi_advance (must match nardi_py::StepResult ordering, with an
 * extra error sentinel that only the C boundary can produce). */
typedef enum
{
    NARDI_STEP_GAME_OVER = 0,
    NARDI_STEP_AWAITING_HUMAN = 1,
    NARDI_STEP_BOT_MOVED = 2,
    NARDI_STEP_TURN_PASSED = 3,
    NARDI_STEP_ERROR = -1
} NardiStep;

/* Generic return code for non-step functions. */
typedef enum
{
    NARDI_OK = 0,
    NARDI_ERR = -1
} NardiStatus;

typedef struct NardiHandle NardiHandle;

/* Lifecycle. nardi_create returns NULL only on allocation failure. */
NardiHandle* nardi_create(void);
void nardi_destroy(NardiHandle* h);
NardiStatus nardi_reset(NardiHandle* h);

/* Configuration. Load the value network used by model bots (greedy / lookahead /
 * mcts) from a weight blob produced by nardi_net.export_weights. */
NardiStatus nardi_load_model(NardiHandle* h, const char* blob_path);
NardiStatus nardi_configure_players(NardiHandle* h, NardiStrategy white, NardiStrategy black);
NardiStatus nardi_set_mcts_params(NardiHandle* h, int n_sims, float temperature,
                                  int exploratory, float c_uct, float dirichlet_eps,
                                  float dirichlet_alpha, int rollouts_per_leaf);

/* Drive the match one step. See NardiStep. */
NardiStep nardi_advance(NardiHandle* h);

/* Queries. */
int nardi_current_player(NardiHandle* h); /* 0 = white, 1 = black; -1 on error */
int nardi_sign(NardiHandle* h);           /* +1 white to move, -1 black; 0 on error */
int nardi_is_terminal(NardiHandle* h);    /* 1 / 0; -1 on error */
int nardi_should_continue(NardiHandle* h);/* 1 / 0; -1 on error */
int nardi_winner_result(NardiHandle* h);  /* 1 normal, 2 mars; -1 on error/not over */

/* Write the two dice into out_dice[2]. */
NardiStatus nardi_dice(NardiHandle* h, int out_dice[2]);

/* Write the current board (NARDI_BOARD_CELLS signed bytes, row-major) into out. */
NardiStatus nardi_board(NardiHandle* h, signed char* out /* [NARDI_BOARD_CELLS] */);

/* Human move interface (valid after nardi_advance returns AWAITING_HUMAN). */
int nardi_legal_move_count(NardiHandle* h); /* >=0; -1 on error */
/* Write the board that legal option `idx` would produce into out. */
NardiStatus nardi_option_board(NardiHandle* h, int idx, signed char* out /* [NARDI_BOARD_CELLS] */);
/* Play legal option `idx`. */
NardiStatus nardi_apply_human_move(NardiHandle* h, int idx);

/* Most recent error message for this handle (never NULL; "" if none). */
const char* nardi_last_error(NardiHandle* h);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NARDI_C_API_H */
