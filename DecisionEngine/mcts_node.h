#pragma once

#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

#include "target_model.h"
#include "../CoreEngine/Auxilaries.h"
#include "../CoreEngine/Board.h"
#include "../CoreEngine/ScenarioBuilder.h"

namespace nardi_py
{

struct MCTSNode;

// The decision sub-node for one specific dice outcome at a chance node. Holds the
// move children (afterstates) reachable with that dice and their own statistics,
// so each dice's best response is searched independently (proper expectimax).
struct DiceBucket
{
    int N = 0; // simulations that sampled this dice at the parent node
    std::unordered_map<Nardi::BoardConfig, std::shared_ptr<MCTSNode>, Nardi::BoardConfigHash> moves;
};

// A node is a PRE-ROLL position (a chance node): a board with `player` about to
// roll. Its value is an expectation over the dice. Each simulation that reaches
// it samples a dice (by probability) into one of the per-dice buckets, then picks
// a move there. Children (afterstates) are themselves pre-roll nodes for the
// opponent, so the tree alternates chance -> dice -> move -> chance ...
//
// Frame convention (negamax): Q / prior / terminal_value are in THIS node's
// side-to-move perspective; a child is the opponent, so its value is negated.
struct MCTSNode
{
    Nardi::BoardConfig board;
    bool player;                   // side to move (about to roll)

    bool  terminal = false;
    float terminal_value = 0.0f;   // side-to-move (loser) frame: -win_margin

    float Q = 0.0f;                // mean value over sampled dice (this node's frame)
    int   N = 0;                   // total simulations through this node
    float prior = 0.0f;            // model value of this position (this node's frame)
    bool  prior_set = false;

    std::unordered_map<int, DiceBucket> by_dice; // dice combo index (0..20) -> bucket

    MCTSNode(const Nardi::BoardConfig& b, bool p) : board(b), player(p) {}
};

// One MCTS search rooted at the current real-dice position (one tree per move).
class MCTSTree
{
public:
    std::shared_ptr<MCTSNode> root;

    // Tunables.
    float c_uct           = 0.1f;  // small: sibling value spreads are tiny
    float dirichlet_alpha = 0.3f;
    float dirichlet_eps   = 0.25f;
    int   rollouts_per_leaf = 0;   // 0 => value-net leaf (recommended)

    int root_dice_idx = -1;        // combo index of the real rolled dice at the root

    MCTSTree(const Nardi::BoardConfig& board, bool player);

    // Run `n` simulations. `root_builder` must be at the root (board, player) with
    // the real dice already rolled, in sim mode; each sim descends a private copy.
    // The root uses the real dice; deeper chance nodes sample dice by probability.
    void run_simulations(int n, Nardi::ScenarioBuilder& root_builder,
                         TargetModel& model, std::mt19937& rng);

    // TRAIN policy over the real-dice root moves:
    //   pi(a) = (1 - eps) * softmax_tau(N) + eps * Dirichlet(alpha)
    Nardi::BoardConfig select_move(const std::vector<Nardi::BoardConfig>& legal_boards,
                                   float temperature, std::mt19937& rng) const;

    // EVAL policy: the most-visited real-dice root move (deterministic).
    Nardi::BoardConfig select_best(const std::vector<Nardi::BoardConfig>& legal_boards) const;

    Nardi::Board::Features root_features() const;

    static int combo_index(int d1, int d2); // canonical 0..20 index for a dice pair

private:
    float simulate(const std::shared_ptr<MCTSNode>& node, Nardi::ScenarioBuilder& builder,
                   TargetModel& model, std::mt19937& rng, bool is_root);
    float rollout(Nardi::ScenarioBuilder& builder, TargetModel& model, std::mt19937& rng);

    // Create move children (afterstate nodes with model priors) for any candidate
    // boards not yet in the bucket; batches the model evaluation.
    void ensure_moves(DiceBucket& bucket, const std::vector<Nardi::BoardConfig>& candidates,
                      bool child_player, TargetModel& model);

    // UCT (negamax) argmax over a bucket's candidate moves.
    const Nardi::BoardConfig& uct_select(const DiceBucket& bucket,
                                         const std::vector<Nardi::BoardConfig>& candidates) const;

    // The real-dice bucket at the root (where the played move is chosen).
    const DiceBucket& root_bucket() const;
};

} // namespace nardi_py
