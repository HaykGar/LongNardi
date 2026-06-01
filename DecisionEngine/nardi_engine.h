#pragma once

#include <pybind11/numpy.h>

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
#include "../CoreEngine/SFMLRW.h"
#include "../CoreEngine/ScenarioBuilder.h"
#include "../CoreEngine/TerminalRW.h"

namespace nardi_py
{

namespace py = pybind11;

// A thin wrapper that owns a live Game and provides Python-friendly methods.
class NardiEngine
{
public:
    NardiEngine();

    ScenarioConfig& GetConfig();

    void AttachNewTRW();
    void AttachNewSFMLRW();
    void DetachRW();

    py::array_t<uint8_t> dice() const;
    int dice_as_idx() const;
    Nardi::Board::Features board_features() const;
    void PrintArray3D(py::array_t<uint8_t>& arr);
    void Render();

    bool roll_has_children();
    std::vector<Nardi::Board::Features> roll_and_enumerate();
    std::vector<Nardi::Board::Features> set_and_enumerate(int d1, int d2);

    void apply_board(const Nardi::BoardConfig& brd);
    bool current_player() const;
    int sign() const;
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

    void apply_best_lookahead(py::array_t<float, py::array::c_style | py::array::forcecast> values);
    void apply_noisy_board(
        py::array_t<float, py::array::c_style | py::array::forcecast> values,
        float eps,
        float temperature);
    void apply_greedy_board(py::array_t<float, py::array::c_style | py::array::forcecast> values);
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
    TargetModel _target_model;
    std::mt19937 _rng{std::random_device{}()};

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
