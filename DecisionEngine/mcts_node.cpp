#include "mcts_node.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nardi_py
{

namespace
{

// Features of a board from `player`'s perspective (side-to-move convention).
Nardi::Board::Features features_for(const Nardi::BoardConfig& board, bool player)
{
    return Nardi::Board().ExtractFeatures(board, player);
}

// Legal end-of-turn afterstate boards for the dice already set on the builder.
std::vector<Nardi::BoardConfig> enumerate_current_dice(Nardi::ScenarioBuilder& b)
{
    if(b.GetCtrl().AwaitingRoll())
        throw std::runtime_error("MCTS: root simulations require dice to be rolled first.");

    const auto& b2s = b.GetGame().GetBoards2Seqs();
    std::vector<Nardi::BoardConfig> out;
    out.reserve(b2s.size());
    for(const auto& kv : b2s)
        out.push_back(kv.first);
    return out;
}

// Set the given dice on a sim-mode builder and return the legal afterstates
// (empty if the side to move has no legal move).
std::vector<Nardi::BoardConfig> set_dice_and_enumerate(Nardi::ScenarioBuilder& b, int d1, int d2)
{
    const auto status = b.ReceiveCommand(Nardi::Command(Nardi::DieType{d1, d2}));
    if(status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
        return {};
    if(status != Nardi::status_codes::SUCCESS)
    {
        Nardi::DispErrorCode(status);
        throw std::runtime_error("MCTS: unexpected status setting dice.");
    }

    const auto& b2s = b.GetGame().GetBoards2Seqs();
    std::vector<Nardi::BoardConfig> out;
    out.reserve(b2s.size());
    for(const auto& kv : b2s)
        out.push_back(kv.first);
    return out;
}

// Apply the move reaching `board` and advance to the opponent (sim mode).
void apply_move(Nardi::ScenarioBuilder& b, const Nardi::BoardConfig& board)
{
    const auto status = b.SimulateMove(board);
    if(status != Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
    {
        Nardi::DispErrorCode(status);
        throw std::runtime_error("MCTS: failed to apply a move.");
    }
}

inline int roll_die(std::mt19937& rng)
{
    std::uniform_int_distribution<int> d(1, 6);
    return d(rng);
}

} // namespace

MCTSTree::MCTSTree(const Nardi::BoardConfig& board, bool player)
: root(std::make_shared<MCTSNode>(board, player))
{
}

int MCTSTree::combo_index(int d1, int d2)
{
    if(d1 > d2)
        std::swap(d1, d2); // canonical d1 <= d2
    int idx = 0;
    for(int k = 1; k < d1; ++k)
        idx += (7 - k);
    idx += (d2 - d1);
    return idx; // 0..20
}

Nardi::Board::Features MCTSTree::root_features() const
{
    return features_for(root->board, root->player);
}

void MCTSTree::run_simulations(int n, Nardi::ScenarioBuilder& root_builder,
                               TargetModel& model, std::mt19937& rng)
{
    root_dice_idx = combo_index(root_builder.GetGame().GetDice(0),
                                root_builder.GetGame().GetDice(1));

    for(int i = 0; i < n; ++i)
    {
        Nardi::ScenarioBuilder sim_builder(root_builder); // private copy per simulation
        simulate(root, sim_builder, model, rng, /*is_root=*/true);
    }
}

float MCTSTree::simulate(const std::shared_ptr<MCTSNode>& node, Nardi::ScenarioBuilder& builder,
                         TargetModel& model, std::mt19937& rng, bool is_root)
{
    if(node->terminal)
        return node->terminal_value;

    // Pick the dice for this visit: the real dice at the root (a known decision),
    // a sampled dice (by probability) at deeper chance nodes.
    int d_idx;
    std::vector<Nardi::BoardConfig> boards;
    if(is_root)
    {
        d_idx = root_dice_idx;
        boards = enumerate_current_dice(builder);
    }
    else
    {
        const int d1 = roll_die(rng);
        const int d2 = roll_die(rng);
        boards = set_dice_and_enumerate(builder, d1, d2);
        d_idx = combo_index(d1, d2);
    }

    DiceBucket& bucket = node->by_dice[d_idx];
    const bool cplayer = !node->player;

    std::shared_ptr<MCTSNode> child;

    if(boards.empty())
    {
        // Forced pass for this dice: opponent to move on the same board.
        builder.GetCtrl().AdvanceSimTurn();
        auto& slot = bucket.moves[node->board];
        if(!slot)
        {
            slot = std::make_shared<MCTSNode>(node->board, cplayer);
            slot->prior = model.evaluate(features_for(node->board, cplayer));
            slot->prior_set = true;
        }
        child = slot;
    }
    else
    {
        ensure_moves(bucket, boards, cplayer, model);
        const Nardi::BoardConfig& chosen = uct_select(bucket, boards);
        apply_move(builder, chosen);

        if(builder.GetGame().GameIsOver())
        {
            // node->player just bore off all pieces and won.
            const float margin = builder.GetGame().IsMars() ? 2.0f : 1.0f;
            child = bucket.moves[chosen];
            child->terminal = true;
            child->terminal_value = -margin; // child (loser) frame
            child->Q = -margin;
            if(child->N == 0)
                child->N = 1;
            const float v = margin; // node frame
            node->Q = (node->Q * node->N + v) / (node->N + 1);
            ++node->N;
            ++bucket.N;
            return v;
        }

        child = bucket.moves[chosen];
    }

    // Descend into / evaluate the child, then back up in this node's frame.
    float v;
    if(child->terminal)
    {
        v = -child->terminal_value;
    }
    else if(child->N == 0)
    {
        // Leaf: value-net estimate (default) or averaged rollouts (child's frame).
        float r;
        if(rollouts_per_leaf <= 0)
        {
            r = child->prior;
        }
        else
        {
            float sum = 0.0f;
            for(int i = 0; i < rollouts_per_leaf; ++i)
            {
                if(i + 1 < rollouts_per_leaf)
                {
                    Nardi::ScenarioBuilder rb(builder);
                    sum += rollout(rb, model, rng);
                }
                else
                {
                    sum += rollout(builder, model, rng);
                }
            }
            r = sum / static_cast<float>(rollouts_per_leaf);
        }
        child->Q = r;
        child->N = 1;
        v = -r;
    }
    else
    {
        v = -simulate(child, builder, model, rng, /*is_root=*/false);
    }

    node->Q = (node->Q * node->N + v) / (node->N + 1);
    ++node->N;
    ++bucket.N;
    return v;
}

float MCTSTree::rollout(Nardi::ScenarioBuilder& builder, TargetModel& model, std::mt19937& rng)
{
    const bool start_player = builder.GetGame().GetBoardRef().PlayerIdx();

    for(int step = 0; step < 1000; ++step) // safety cap; Nardi always terminates
    {
        const bool mover = builder.GetGame().GetBoardRef().PlayerIdx();
        const int d1 = roll_die(rng);
        const int d2 = roll_die(rng);
        const auto boards = set_dice_and_enumerate(builder, d1, d2);

        if(boards.empty())
        {
            builder.GetCtrl().AdvanceSimTurn(); // pass
            continue;
        }

        // Greedy: evaluate each afterstate from the opponent's (side-to-move)
        // perspective and pick the one that minimizes it (best for the mover).
        std::vector<Nardi::Board::Features> feats;
        feats.reserve(boards.size());
        for(const auto& b : boards)
            feats.push_back(features_for(b, !mover));
        const std::vector<float> vals = model.evaluate_batch(feats);

        size_t best = 0;
        for(size_t i = 1; i < vals.size(); ++i)
            if(vals[i] < vals[best])
                best = i;

        apply_move(builder, boards[best]);

        if(builder.GetGame().GameIsOver())
        {
            const float margin = builder.GetGame().IsMars() ? 2.0f : 1.0f;
            return (mover == start_player) ? margin : -margin;
        }
    }

    return 0.0f; // unreachable in practice
}

void MCTSTree::ensure_moves(DiceBucket& bucket, const std::vector<Nardi::BoardConfig>& candidates,
                            bool child_player, TargetModel& model)
{
    std::vector<Nardi::BoardConfig> fresh;
    for(const auto& b : candidates)
        if(bucket.moves.find(b) == bucket.moves.end())
            fresh.push_back(b);

    if(fresh.empty())
        return;

    std::vector<Nardi::Board::Features> feats;
    feats.reserve(fresh.size());
    for(const auto& b : fresh)
        feats.push_back(features_for(b, child_player));
    const std::vector<float> priors = model.evaluate_batch(feats);

    for(size_t i = 0; i < fresh.size(); ++i)
    {
        auto child = std::make_shared<MCTSNode>(fresh[i], child_player);
        child->prior = priors[i];
        child->prior_set = true;
        bucket.moves[fresh[i]] = child;
    }
}

const Nardi::BoardConfig& MCTSTree::uct_select(const DiceBucket& bucket,
                                               const std::vector<Nardi::BoardConfig>& candidates) const
{
    const float logN = std::log(static_cast<float>(bucket.N) + 1.0f);

    size_t best = 0;
    float best_score = -std::numeric_limits<float>::infinity();
    for(size_t i = 0; i < candidates.size(); ++i)
    {
        const auto& child = bucket.moves.at(candidates[i]);
        // Unvisited child: model prior as the value estimate (first-play urgency).
        // Negate to the parent's frame (child is the opponent).
        const float q_est = (child->N == 0) ? child->prior : child->Q;
        const float score = -q_est + c_uct * std::sqrt(logN / static_cast<float>(child->N + 1));
        if(score > best_score)
        {
            best_score = score;
            best = i;
        }
    }
    return candidates[best];
}

const DiceBucket& MCTSTree::root_bucket() const
{
    auto it = root->by_dice.find(root_dice_idx);
    if(it == root->by_dice.end())
        throw std::runtime_error("MCTS: no simulations recorded for the rolled root dice.");
    return it->second;
}

Nardi::BoardConfig MCTSTree::select_move(const std::vector<Nardi::BoardConfig>& legal_boards,
                                         float temperature, std::mt19937& rng) const
{
    // Implicit policy over the real-dice root moves:
    //   pi(a) = (1 - eps) * softmax_tau(N) + eps * Dirichlet(alpha)
    const DiceBucket& bucket = root_bucket();
    const size_t K = legal_boards.size();
    if(K == 0)
        throw std::runtime_error("MCTS select_move: no legal moves.");

    const double inv_temp = 1.0 / std::max(1e-3f, temperature);

    std::vector<double> boltzmann(K, 0.0);
    double bsum = 0.0;
    for(size_t i = 0; i < K; ++i)
    {
        auto it = bucket.moves.find(legal_boards[i]);
        const int n = (it != bucket.moves.end()) ? it->second->N : 0;
        if(n > 0)
        {
            boltzmann[i] = std::pow(static_cast<double>(n), inv_temp);
            bsum += boltzmann[i];
        }
    }
    if(bsum > 0.0)
        for(double& w : boltzmann)
            w /= bsum;

    std::vector<double> policy(K);
    if(dirichlet_eps > 0.0f)
    {
        std::gamma_distribution<double> gamma(dirichlet_alpha, 1.0);
        std::vector<double> noise(K);
        double nsum = 0.0;
        for(size_t i = 0; i < K; ++i)
        {
            noise[i] = gamma(rng);
            nsum += noise[i];
        }
        if(nsum <= 0.0)
        {
            std::fill(noise.begin(), noise.end(), 1.0);
            nsum = static_cast<double>(K);
        }
        const double eps = dirichlet_eps;
        for(size_t i = 0; i < K; ++i)
            policy[i] = (1.0 - eps) * boltzmann[i] + eps * (noise[i] / nsum);
    }
    else
    {
        policy = boltzmann;
    }

    double psum = 0.0;
    for(double w : policy)
        psum += w;
    if(psum <= 0.0)
        std::fill(policy.begin(), policy.end(), 1.0);

    std::discrete_distribution<size_t> dist(policy.begin(), policy.end());
    return legal_boards[dist(rng)];
}

Nardi::BoardConfig MCTSTree::select_best(const std::vector<Nardi::BoardConfig>& legal_boards) const
{
    const DiceBucket& bucket = root_bucket();
    const Nardi::BoardConfig* best = nullptr;
    int best_n = -1;
    for(const auto& board : legal_boards)
    {
        auto it = bucket.moves.find(board);
        const int n = (it != bucket.moves.end()) ? it->second->N : 0;
        if(n > best_n)
        {
            best_n = n;
            best = &board;
        }
    }
    if(best == nullptr)
        throw std::runtime_error("MCTS select_best: no legal moves.");
    return *best;
}

} // namespace nardi_py
