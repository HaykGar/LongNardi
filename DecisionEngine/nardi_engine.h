#pragma once

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <random>
#include <string>
#include <vector>

#include "lookahead_batch.h"
#include "mcts_node.h"
#include "scenario_config.h"
#include "target_model.h"
#include "../CoreEngine/Auxilaries.h"
#include "../CoreEngine/Controller.h"
#include "../CoreEngine/Game.h"
#include "../CoreEngine/ReaderWriter.h"
#include "../CoreEngine/ScenarioBuilder.h"
#include "../CoreEngine/TerminalRW.h"
// The SFML desktop view is optional: builds that render elsewhere (e.g. iOS via
// SwiftUI) compile without SFML by leaving NARDI_ENABLE_SFML undefined.
#ifdef NARDI_ENABLE_SFML
#include "../CoreEngine/SFMLRW.h"
#endif

namespace nardi_py
{

// Per-player move strategy used by the in-C++ match orchestrator (advance()).
enum class Strategy
{
    Human,
    Greedy,
    Lookahead,
    Mcts,
    Heuristic,
    Random
};

// Result of one advance() step, driving an external UI / caller loop.
enum class StepResult
{
    GameOver,       // game is finished; query winner_result()
    AwaitingHuman,  // dice rolled, legal options cached; caller must apply_human_move()
    BotMoved,       // a bot played its turn this step
    TurnPassed      // current player had no legal moves; turn switched, no move made
};

// A thin wrapper that owns a live Game and provides Python-friendly methods.
class NardiEngine
{
public:
    NardiEngine();

    ScenarioConfig& GetConfig();

    void AttachNewTRW();
    void AttachNewSFMLRW();
    void DetachRW();

    std::array<int, 2> dice_values() const; // dice as {d0, d1}
    int dice_as_idx() const;
    Nardi::Board::Features board_features() const;
    void Render();

    bool roll_has_children();
    std::vector<Nardi::Board::Features> roll_and_enumerate();
    std::vector<Nardi::Board::Features> set_and_enumerate(int d1, int d2);

    void apply_board(const Nardi::BoardConfig& brd);
    bool current_player() const;
    int sign() const;
    // Pip count (1-moves to bear all pieces off) per colour: {white, black}.
    std::array<int, 2> pip_counts() const;
    int turn_num() const;
    int player_turn_num(bool player) const;

    struct Node;

    struct ChanceAndChildren
    {
        float prob;
        std::vector<std::shared_ptr<Node>> data;
    };

    struct Node
    {
        Node();
        std::optional<int> result;
        Nardi::Board::Features features;
        std::array<ChanceAndChildren, N_DICE_COMB> children_by_dice;

        bool isLeaf();
        std::shared_ptr<Node> MakeChild(const Nardi::Board::Features& f, int d_idx);
    };

    std::shared_ptr<LookaheadBatch> MakeLookaheadBatch();

    // values are per-eval-feature / per-child side-to-move evaluations from the
    // Python model; the numpy parsing happens in the binding layer.
    void apply_best_lookahead(const std::vector<float>& values);
    void apply_noisy_board(const std::vector<float>& values, float eps, float temperature);
    void apply_greedy_board(const std::vector<float>& values);
    void apply_random_board();
    void apply_heuristic_board();

    // --- In-C++ bot decisions (model evaluated by the hand-rolled InferenceNet,
    // no Python eval needed). The *_choice methods return the index of the move
    // the bot would play without applying it (useful for UI/highlighting and for
    // parity testing); the apply_* methods choose and play in one step. The
    // *_with variants take an explicit net so an orchestrator can drive two
    // different bots; the *_target variants use the loaded target network.
    int greedy_choice(const TargetModel& net) const;
    void apply_greedy_with(const TargetModel& net);
    int lookahead_choice(const TargetModel& net);
    void apply_lookahead_with(const TargetModel& net);

    int greedy_choice_target() const;
    void apply_greedy_target();
    int lookahead_choice_target();
    void apply_lookahead_target();

    // --- Two-ply lookahead (depth-2 expectiminimax). Where one-ply evaluates
    // each root move by the opponent's dice-averaged best static reply, two-ply
    // evaluates that reply by looking one further ply (the mover's own dice-
    // averaged best move, static leaf). `top_k <= 0` expands every root move to
    // depth two; `top_k > 0` expands only the K best moves by one-ply value (the
    // rest keep their one-ply value) -- the cost/strength knob, since full
    // two-ply is ~21x a one-ply per expanded move. lookahead2_child_values
    // returns the per-root-child values aligned with the last lookahead batch's
    // children (root-mover frame); the *_choice methods return the argmax index;
    // apply_* play it (with the forced/no-move short-circuits, like one-ply).
    std::vector<float> lookahead2_child_values(const TargetModel& net, int top_k);
    int lookahead2_choice(const TargetModel& net, int top_k);
    void apply_lookahead2_with(const TargetModel& net, int top_k);
    int lookahead2_choice_target(int top_k);
    void apply_lookahead2_target(int top_k);
    std::vector<float> lookahead2_child_values_target(int top_k);
    // Model-evaluation count of the last two-ply computation (for cost analysis).
    long last_lookahead2_evals() const;

    // --- In-C++ match orchestrator (the turn loop, moved out of Python). The
    // caller repeatedly calls advance(); each step rolls for the current player
    // and either plays a bot move, reports that a human move is awaited, reports
    // a forced pass, or reports the game is over. For a model-bot side, load the
    // value network once via load_target_network().
    void configure_players(Strategy white, Strategy black);
    void set_mcts_params(int n_sims, float temperature = 1.0f, bool exploratory = false,
                         float c_uct = 0.1f, float dirichlet_eps = 0.25f,
                         float dirichlet_alpha = 0.3f, int rollouts_per_leaf = 0);
    StepResult advance();
    std::vector<Nardi::Board::Features> current_options() const;
    int legal_move_count() const;
    void apply_human_move(int idx);

    // --- Incremental human move interface (mirrors SFMLRW): after advance()
    // returns AwaitingHuman (dice rolled), the UI selects a source point then
    // "clicks" a die to move; on a successful sub-move the destination auto-
    // selects so dice can be chained. The turn ends automatically when no legal
    // moves remain (the controller switches players); detect via turn_in_progress.
    bool human_select(int row, int col);   // returns true if a source was selected
    bool human_move_die(int die_idx);      // true if a sub-move was applied
    bool human_undo();                     // undo the last sub-move this turn
    bool can_use_die(int die_idx);         // die still playable this turn
    bool start_is_selected() const;
    std::array<int, 2> selected_start() const; // {row,col} or {-1,-1}
    // Bitmask of squares that can start a move with die `die_idx` (bit row*COLS+col
    // set), from the precomputed per-die start sets refreshed by CheckForcedMoves.
    int starts_mask(int die_idx) const;
    // Recompute legal moves + per-die start sets for the current position (used by
    // analysis, which sets dice without going through the match loop's roll).
    void refresh_forced();
    bool turn_in_progress() const;         // dice rolled and game not over
    bool turn_is_complete() const;         // no legal moves left, awaiting confirm
    void confirm_turn();                   // advance to next player (if turn complete)

    // Sub-moves applied by the last move command (apply_board / human_move_die /
    // human_undo), in order, as {fromRow, fromCol, toRow, toCol}; -1 = off-board.
    // Lets the UI animate each sub-move separately (a forced/bot turn can be many).
    std::vector<std::array<int, 4>> recent_moves() const;

    void human_turn(bool dice_rolled = false);
    void restart_or_quit();
    bool is_terminal() const;
    int winner_result() const;
    std::vector<Nardi::Board::Features> enumerate(Nardi::status_codes status);
    void reset();
    void status_report();
    std::string status_str();
    bool should_continue_game() const;
    bool quit_requested() const;

    void load_target_network(const std::string& path);
    float debug_target_eval();

    // --- Analysis mode (board editor + learned-evaluator analysis). Set an
    // arbitrary position (board + side to move), evaluate it with the loaded
    // value network, then set explicit dice and rank the legal moves by a
    // one-ply lookahead. apply_analyzed_move() plays a ranked move (recording
    // sub-moves for animation, like a bot turn) so a line can be played out.
    void set_position(const Nardi::BoardConfig& brd, bool side);
    float evaluate_position() const;   // side-to-move value; requires a model
    // Ranked legal moves for explicit dice {d1,d2}: {end-board, lookahead value
    // in the side-to-move (mover) frame}, best first. Empty if no legal move.
    std::vector<std::pair<Nardi::BoardConfig, float>> analyze_dice(int d1, int d2);
    void apply_analyzed_move(int idx); // play ranked move idx (switches side)
    // Accessors over the cached ranked moves from the last analyze_dice (best
    // first), for the plain-C boundary.
    int analyzed_count() const;
    Nardi::BoardConfig analyzed_board(int idx) const;
    float analyzed_value(int idx) const;
    std::vector<std::pair<Nardi::Board::Features, float>> run_mcts_game(
        int n_sims,
        float temperature = 1.0f,
        int max_turns = 1000,
        float c_uct = 0.1f,
        float dirichlet_eps = 0.25f,
        float dirichlet_alpha = 0.3f,
        int rollouts_per_leaf = 0);

    // Move strategy: with dice already rolled, run MCTS from the current position
    // and apply the chosen move. exploratory=false (eval) plays the most-visited
    // move; exploratory=true (train) samples the Boltzmann+Dirichlet policy.
    void mcts_apply_move(
        int n_sims,
        float temperature = 1.0f,
        bool exploratory = false,
        float c_uct = 0.1f,
        float dirichlet_eps = 0.25f,
        float dirichlet_alpha = 0.3f,
        int rollouts_per_leaf = 0);

private:
    Nardi::ScenarioBuilder _builder;
    ScenarioConfig _config;
    std::vector<Nardi::Board::Features> _last_children;
    std::shared_ptr<LookaheadBatch> _last_lookahead_batch;
    std::vector<std::pair<Nardi::BoardConfig, float>> _analyzed; // ranked analysis moves
    TargetModel _target_model;
    std::mt19937 _rng{std::random_device{}()};
    long _last_lookahead2_evals = 0;   // model evals in the last two-ply computation

    // One-ply value to `mover` of a pre-roll position (mover to move): the dice-
    // averaged best move with a static leaf. Used as the two-ply leaf. `scratch`
    // is a reusable builder for enumerating the mover's responses.
    float oneply_value_to_mover(const Nardi::BoardConfig& board, bool mover,
                                const TargetModel& net, Nardi::ScenarioBuilder& scratch);

    // Orchestrator state: per-player strategy (index 0 = white, 1 = black) and
    // MCTS search tunables used when a side plays the Mcts strategy.
    std::array<Strategy, 2> _player_strats{Strategy::Human, Strategy::Greedy};
    struct
    {
        int n_sims = 200;
        float temperature = 1.0f;
        bool exploratory = false;
        float c_uct = 0.1f;
        float dirichlet_eps = 0.25f;
        float dirichlet_alpha = 0.3f;
        int rollouts_per_leaf = 0;
    } _mcts_params;

    const std::vector<Nardi::Board::Features>& require_children() const;
    std::shared_ptr<LookaheadBatch> require_lookahead_batch() const;
    static std::optional<size_t> terminal_child_index(const std::vector<Nardi::Board::Features>& children);
    static int flat_dice_idx(int d1, int d2);
    static std::vector<Nardi::Board::Features> set_and_enumerate(
        int d1,
        int d2,
        Nardi::ScenarioBuilder& b);
    void GetHumanInput();
};

} // namespace nardi_py
