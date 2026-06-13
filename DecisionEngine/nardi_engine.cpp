#include "nardi_engine.h"

#include <algorithm>
#include <future>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_set>

namespace nardi_py
{

namespace
{

std::vector<Nardi::BoardConfig> legal_boards_for_current_dice(const Nardi::ScenarioBuilder& b)
{
    const auto& b2s = b.GetGame().GetBoards2Seqs();
    std::vector<Nardi::BoardConfig> boards;
    boards.reserve(b2s.size());
    for(const auto& kv : b2s)
        boards.push_back(kv.first);
    return boards;
}

} // namespace

NardiEngine::NardiEngine()
: _builder(), _config(_builder)
{
    _builder.withFirstTurn();
}

ScenarioConfig& NardiEngine::GetConfig()
{
    return _config;
}

void NardiEngine::AttachNewTRW()
{
    _builder.AttachNewRW(Nardi::TerminalRWFactory());
}

void NardiEngine::AttachNewSFMLRW()
{
#ifdef NARDI_ENABLE_SFML
    _builder.AttachNewRW(Nardi::SFMLRWFactory());
    _builder.GetView()->PollInput();
#else
    throw std::runtime_error("SFML view not available: built without NARDI_ENABLE_SFML.");
#endif
}

void NardiEngine::DetachRW()
{
    _builder.DetachRW();
}

std::array<int, 2> NardiEngine::dice_values() const
{
    return {_builder.GetGame().GetDice(0), _builder.GetGame().GetDice(1)};
}

int NardiEngine::dice_as_idx() const
{
    return flat_dice_idx(_builder.GetGame().GetDice(0), _builder.GetGame().GetDice(1));
}

Nardi::Board::Features NardiEngine::board_features() const
{
    return _builder.GetGame().GetBoardRef().ExtractFeatures();
}

void NardiEngine::Render()
{
    _builder.Render();
}

bool NardiEngine::roll_has_children()
{
    auto status = _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::ROLL_DICE));
    _last_children = enumerate(status);
    return !_last_children.empty();
}

std::vector<Nardi::Board::Features> NardiEngine::roll_and_enumerate()
{
    const auto status = _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::ROLL_DICE));
    _last_children = enumerate(status);
    return _last_children;
}

std::vector<Nardi::Board::Features> NardiEngine::set_and_enumerate(int d1, int d2)
{
    std::array<int, 2> new_dice = {d1, d2};
    const auto status = _builder.ReceiveCommand(Nardi::Command(new_dice));

    _last_children = enumerate(status);
    return _last_children;
}

void NardiEngine::apply_board(const Nardi::BoardConfig& brd)
{
    // Record the individual sub-moves (for the UI to animate each separately).
    _builder.GetGame().StartRecording();
    auto status = _builder.ReceiveCommand(Nardi::Command(brd));
    _builder.GetGame().StopRecording();
    if(status != Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
    {
        Nardi::DispErrorCode(status);
        throw std::runtime_error("Autoplay failed to complete");
    }
    // A whole-board apply is a complete bot/auto turn, so confirm it here (advance
    // to the next player) -- preserving the old auto-advance behavior for bots and
    // the Python training/benchmark code. Human incremental moves go through
    // MOVE_BY_DICE and require an explicit confirm_turn() so they can be undone
    // first. (No-op when the move ended the game, which finalizes immediately.)
    _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::CONFIRM_TURN_OVER));
    _last_children.clear();
    _last_lookahead_batch.reset();
}

bool NardiEngine::current_player() const
{
    return _builder.GetGame().GetBoardRef().PlayerIdx();
}

int NardiEngine::sign() const
{
    return current_player() ? -1 : 1;
}

std::array<int, 2> NardiEngine::pip_counts() const
{
    // ExtractFeatures() reports pips from the side-to-move's frame (player =
    // current player, opp = the other). Re-key by colour so callers get a
    // stable {white, black} regardless of whose turn it is.
    const Nardi::Board::Features f = board_features();
    const bool side = current_player();   // 0 white, 1 black (side to move)
    std::array<int, 2> out{};             // index 0 = white, 1 = black
    out[side]  = f.player.pip_count;
    out[!side] = f.opp.pip_count;
    return out;
}

int NardiEngine::turn_num() const
{
    return _builder.GetGame().GetTotalTurnNumber();
}

int NardiEngine::player_turn_num(bool player) const
{
    return _builder.GetGame().GetTurnNumber(player);
}

NardiEngine::Node::Node()
{
    for(int i = 0; i < N_DICE_COMB; ++i)
        children_by_dice[i].prob = COMBO_PROBS[i];
}

bool NardiEngine::Node::isLeaf()
{
    for(const auto& ch_ch : children_by_dice)
        if(!ch_ch.data.empty())
            return false;
    return true;
}

std::shared_ptr<NardiEngine::Node> NardiEngine::Node::MakeChild(
    const Nardi::Board::Features& f,
    int d_idx)
{
    auto child = std::make_shared<Node>();
    child->features = f;
    children_by_dice[d_idx].data.push_back(child);
    return child;
}

std::shared_ptr<LookaheadBatch> NardiEngine::MakeLookaheadBatch()
{
    auto batch = std::make_shared<LookaheadBatch>();
    auto legal_children = enumerate(Nardi::status_codes::SUCCESS);
    if(legal_children.empty())
    {
        _last_lookahead_batch = batch;
        return batch;
    }

    _builder.ToSimMode();

    try
    {
        for(const auto& child_feature : legal_children)
        {
            LookaheadBatch::ChildChoice child;
            child.board = child_feature.raw_data;

            const auto terminal_value = terminal_value_for_side_to_move(child_feature);
            if(terminal_value.has_value())
            {
                child.terminal_value = terminal_value.value();
                batch->children.clear();
                batch->eval_features.clear();
                batch->children.push_back(std::move(child));
                // Preserve the old simulator shortcut: immediate wins use the
                // true terminal result and ignore model-valued alternatives.
                break;
            }

            auto status = _builder.SimulateMove(child_feature.raw_data);
            if(status != Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
            {
                Nardi::DispErrorCode(status);
                throw std::runtime_error("Lookahead failed to simulate a child move.");
            }

            Nardi::ScenarioBuilder after_child(_builder);

            struct DiceResult
            {
                int dice_idx;
                std::vector<Nardi::Board::Features> features;
            };

            std::vector<std::future<DiceResult>> futures;
            futures.reserve(N_DICE_COMB);

            // Fan out the 21 chance outcomes. Every task owns its ScenarioBuilder
            // copy, so dice-setting and enumeration mutate only thread-local state.
            for(int d_idx = 0; d_idx < N_DICE_COMB; ++d_idx)
            {
                futures.push_back(std::async(
                    std::launch::async,
                    [after_child, d_idx]() mutable -> DiceResult
                    {
                        const auto& dice = DICE_COMBOS[d_idx];
                        return DiceResult{
                            d_idx,
                            NardiEngine::set_and_enumerate(dice[0], dice[1], after_child)
                        };
                    }));
            }

            // Join before mutating the shared LookaheadBatch. This keeps all batch
            // writes single-threaded and avoids synchronization inside the batch.
            std::array<DiceResult, N_DICE_COMB> dice_results;
            try
            {
                for(auto& future : futures)
                {
                    DiceResult result = future.get();
                    dice_results[static_cast<size_t>(result.dice_idx)] = std::move(result);
                }
            }
            catch(...)
            {
                _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::UNDO_TURN));
                throw;
            }

            const auto& board = after_child.GetGame().GetBoardRef();
            const bool next_player = !board.PlayerIdx();

            // Flatten grandchildren into one eval_features vector. Dice groups store
            // either a terminal value or indices into that vector.
            for(const auto& result : dice_results)
            {
                auto& group = child.dice_groups[static_cast<size_t>(result.dice_idx)];
                if(result.features.empty())
                {
                    // No opponent move: the same board becomes the next state, but
                    // from the root player's side-to-move perspective.
                    auto& eval_indices = std::get<std::vector<int>>(group.data);
                    eval_indices.push_back(static_cast<int>(batch->eval_features.size()));
                    batch->eval_features.push_back(
                        board.ExtractFeatures(after_child.GetGame().GetBoardData(), next_player)
                    );
                    continue;
                }

                for(const auto& f : result.features)
                {
                    const auto opp_terminal_value = terminal_value_for_side_to_move(f);
                    if(opp_terminal_value.has_value())
                    {
                        // The opponent won after the root child, so from the root
                        // player's perspective this child outcome is negative.
                        const float child_terminal_value = -opp_terminal_value.value();
                        if(std::holds_alternative<float>(group.data)
                            && std::get<float>(group.data) != child_terminal_value)
                            throw std::runtime_error(
                                "One opponent dice group produced inconsistent terminal values.");

                        group.data = child_terminal_value;
                        continue;
                    }

                    if(std::holds_alternative<float>(group.data))
                        continue;

                    // Re-feature the opponent's non-terminal reply from the root
                    // player's perspective before adding it to the model batch.
                    auto& eval_indices = std::get<std::vector<int>>(group.data);
                    eval_indices.push_back(static_cast<int>(batch->eval_features.size()));
                    batch->eval_features.push_back(board.ExtractFeatures(f.raw_data, next_player));
                }
            }

            _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::UNDO_TURN));
            batch->children.push_back(std::move(child));
        }
    }
    catch(...)
    {
        _builder.EndSimMode();
        throw;
    }

    _builder.EndSimMode();
    _last_lookahead_batch = batch;
    return batch;
}

void NardiEngine::apply_best_lookahead(const std::vector<float>& values)
{
    auto batch = require_lookahead_batch();
    // Copy out before apply_board() invalidates batch/_last_children.
    const Nardi::BoardConfig board =
        batch->children.at(static_cast<size_t>(batch->best_index_values(values))).board;
    apply_board(board);
}

void NardiEngine::apply_noisy_board(const std::vector<float>& values, float eps, float temperature)
{
    const auto& children = require_children();
    if(const auto terminal_idx = terminal_child_index(children); terminal_idx.has_value())
    {
        apply_board(children.at(terminal_idx.value()).raw_data);
        return;
    }

    const int idx = sample_noisy_index(values, eps, temperature, _rng);
    apply_board(children.at(static_cast<size_t>(idx)).raw_data);
}

void NardiEngine::apply_greedy_board(const std::vector<float>& values)
{
    const auto& children = require_children();
    if(const auto terminal_idx = terminal_child_index(children); terminal_idx.has_value())
    {
        apply_board(children.at(terminal_idx.value()).raw_data);
        return;
    }

    if(values.size() != children.size())
        throw std::runtime_error("apply_greedy_board: values length does not match children.");
    auto best = std::max_element(values.begin(), values.end());
    const size_t idx = static_cast<size_t>(std::distance(values.begin(), best));
    apply_board(children.at(idx).raw_data);
}

int NardiEngine::greedy_choice(const TargetModel& net) const
{
    const auto& children = require_children();
    if(const auto terminal_idx = terminal_child_index(children); terminal_idx.has_value())
        return static_cast<int>(terminal_idx.value());

    const std::vector<float> values = net.evaluate_batch(children);
    return static_cast<int>(
        std::distance(values.begin(), std::max_element(values.begin(), values.end())));
}

void NardiEngine::apply_greedy_with(const TargetModel& net)
{
    const int idx = greedy_choice(net);
    // Copy the board out before apply_board() clears _last_children.
    const Nardi::BoardConfig board = require_children().at(static_cast<size_t>(idx)).raw_data;
    apply_board(board);
}

int NardiEngine::lookahead_choice(const TargetModel& net)
{
    auto batch = MakeLookaheadBatch(); // also caches _last_lookahead_batch
    if(batch->children.empty())
        return -1; // no legal move; the turn passes

    const std::vector<float> values = net.evaluate_batch(batch->eval_features);
    return batch->best_index_values(values);
}

void NardiEngine::apply_lookahead_with(const TargetModel& net)
{
    // Forced move: with a single legal end-board there is nothing to choose, so
    // skip the (expensive) one-ply search entirely and just play it.
    const auto children = enumerate(Nardi::status_codes::SUCCESS);
    if(children.empty())
        return; // no legal move; the turn passes
    if(children.size() == 1)
    {
        const Nardi::BoardConfig board = children.front().raw_data;
        apply_board(board);
        return;
    }

    const int idx = lookahead_choice(net);
    if(idx < 0)
        return;
    const Nardi::BoardConfig board =
        _last_lookahead_batch->children.at(static_cast<size_t>(idx)).board;
    apply_board(board);
}

int NardiEngine::greedy_choice_target() const
{
    if(!_target_model.is_loaded())
        throw std::runtime_error("greedy_choice_target requires load_target_network(path) first.");
    return greedy_choice(_target_model);
}

void NardiEngine::apply_greedy_target()
{
    if(!_target_model.is_loaded())
        throw std::runtime_error("apply_greedy_target requires load_target_network(path) first.");
    apply_greedy_with(_target_model);
}

int NardiEngine::lookahead_choice_target()
{
    if(!_target_model.is_loaded())
        throw std::runtime_error("lookahead_choice_target requires load_target_network(path) first.");
    return lookahead_choice(_target_model);
}

void NardiEngine::apply_lookahead_target()
{
    if(!_target_model.is_loaded())
        throw std::runtime_error("apply_lookahead_target requires load_target_network(path) first.");
    apply_lookahead_with(_target_model);
}

// ---- Two-ply lookahead -------------------------------------------------- //

float NardiEngine::oneply_value_to_mover(const Nardi::BoardConfig& board, bool mover,
                                         const TargetModel& net, Nardi::ScenarioBuilder& scratch)
{
    // Value to `mover` of a pre-roll position (mover to move): average over the
    // mover's 21 dice of [best move, static leaf]. After the mover moves it is
    // the opponent's turn, so a non-terminal reply is evaluated from the
    // opponent's perspective and negated into the mover's frame; a reply that
    // bears off the mover's last checker is a terminal win for the mover.
    const bool opp = !mover;
    const Nardi::Board& boardref = scratch.GetGame().GetBoardRef();
    float total = 0.0f;

    for(int d = 0; d < N_DICE_COMB; ++d)
    {
        scratch.ResetPreRoll(mover, board);
        const auto responses = set_and_enumerate(DICE_COMBOS[d][0], DICE_COMBOS[d][1], scratch);

        float best;
        if(responses.empty())
        {
            // Mover cannot move for this roll: it passes to the opponent. Use the
            // static value of the position in the mover's frame.
            best = -net.evaluate(boardref.ExtractFeatures(board, opp));
            ++_last_lookahead2_evals;
        }
        else
        {
            std::vector<Nardi::Board::Features> leaves;
            leaves.reserve(responses.size());
            best = -std::numeric_limits<float>::infinity();
            for(const auto& f : responses)
            {
                // `f` is featured from the mover's perspective, so a terminal
                // value here is a win for the mover.
                if(const auto term = terminal_value_for_side_to_move(f); term.has_value())
                    best = std::max(best, term.value());
                else
                    leaves.push_back(boardref.ExtractFeatures(f.raw_data, opp));
            }
            if(!leaves.empty())
            {
                const auto evals = net.evaluate_batch(leaves);   // value to opponent
                _last_lookahead2_evals += static_cast<long>(evals.size());
                for(float e : evals)
                    best = std::max(best, -e);                    // value to mover
            }
        }
        total += COMBO_PROBS[d] * best;
    }
    return total;
}

std::vector<float> NardiEngine::lookahead2_child_values(const TargetModel& net, int top_k)
{
    _last_lookahead2_evals = 0;
    auto batch = MakeLookaheadBatch();   // root one-ply frontier (caches _last_lookahead_batch)
    if(batch->children.empty())
        return {};

    const std::vector<float> values1 = net.evaluate_batch(batch->eval_features);
    _last_lookahead2_evals += static_cast<long>(values1.size());
    const std::vector<float> child1 = batch->child_values_vec(values1);   // one-ply per child

    // Which root children to expand to depth two: the top_k by one-ply value
    // (all of them if top_k <= 0). Terminal children carry no eval features and
    // are left untouched -- their child_value is already the true terminal value.
    const int n = static_cast<int>(child1.size());
    std::unordered_set<int> expand;
    if(top_k <= 0 || top_k >= n)
    {
        for(int i = 0; i < n; ++i) expand.insert(i);
    }
    else
    {
        std::vector<int> order(static_cast<size_t>(n));
        std::iota(order.begin(), order.end(), 0);
        std::partial_sort(order.begin(), order.begin() + top_k, order.end(),
                          [&](int a, int b) { return child1[static_cast<size_t>(a)] > child1[static_cast<size_t>(b)]; });
        expand.insert(order.begin(), order.begin() + top_k);
    }

    // Replace the static leaf of each non-terminal grandchild (opponent reply) of
    // an expanded child with that grandchild's one-ply value, then re-aggregate.
    const bool mover = current_player();
    std::vector<float> values2 = values1;
    Nardi::ScenarioBuilder scratch(_builder);
    scratch.SetTurnNumbers(5, 5);   // grandchildren are past the opening; no first-move rule
    for(int ci = 0; ci < static_cast<int>(batch->children.size()); ++ci)
    {
        if(expand.find(ci) == expand.end())
            continue;
        for(const auto& group : batch->children[static_cast<size_t>(ci)].dice_groups)
        {
            if(!std::holds_alternative<std::vector<int>>(group.data))
                continue;   // terminal opponent dice group (a float) -- keep as is
            for(int idx : std::get<std::vector<int>>(group.data))
            {
                const Nardi::BoardConfig g = batch->eval_features[static_cast<size_t>(idx)].raw_data;
                values2[static_cast<size_t>(idx)] = oneply_value_to_mover(g, mover, net, scratch);
            }
        }
    }

    return batch->child_values_vec(values2);
}

int NardiEngine::lookahead2_choice(const TargetModel& net, int top_k)
{
    const auto child_vals = lookahead2_child_values(net, top_k);
    if(child_vals.empty())
        return -1;
    return static_cast<int>(
        std::distance(child_vals.begin(), std::max_element(child_vals.begin(), child_vals.end())));
}

void NardiEngine::apply_lookahead2_with(const TargetModel& net, int top_k)
{
    // Move selection: short-circuit forced / no-move positions like one-ply.
    const auto children = enumerate(Nardi::status_codes::SUCCESS);
    if(children.empty())
        return;   // no legal move; the turn passes
    if(children.size() == 1)
    {
        apply_board(children.front().raw_data);
        return;
    }
    const int idx = lookahead2_choice(net, top_k);
    if(idx < 0)
        return;
    const Nardi::BoardConfig board =
        _last_lookahead_batch->children.at(static_cast<size_t>(idx)).board;
    apply_board(board);
}

int NardiEngine::lookahead2_choice_target(int top_k)
{
    if(!_target_model.is_loaded())
        throw std::runtime_error("lookahead2_choice_target requires load_target_network(path) first.");
    return lookahead2_choice(_target_model, top_k);
}

void NardiEngine::apply_lookahead2_target(int top_k)
{
    if(!_target_model.is_loaded())
        throw std::runtime_error("apply_lookahead2_target requires load_target_network(path) first.");
    apply_lookahead2_with(_target_model, top_k);
}

std::vector<float> NardiEngine::lookahead2_child_values_target(int top_k)
{
    if(!_target_model.is_loaded())
        throw std::runtime_error("lookahead2_child_values_target requires load_target_network(path) first.");
    return lookahead2_child_values(_target_model, top_k);
}

long NardiEngine::last_lookahead2_evals() const
{
    return _last_lookahead2_evals;
}

void NardiEngine::configure_players(Strategy white, Strategy black)
{
    _player_strats[0] = white;
    _player_strats[1] = black;
}

void NardiEngine::set_mcts_params(int n_sims, float temperature, bool exploratory,
                                  float c_uct, float dirichlet_eps, float dirichlet_alpha,
                                  int rollouts_per_leaf)
{
    _mcts_params.n_sims = n_sims;
    _mcts_params.temperature = temperature;
    _mcts_params.exploratory = exploratory;
    _mcts_params.c_uct = c_uct;
    _mcts_params.dirichlet_eps = dirichlet_eps;
    _mcts_params.dirichlet_alpha = dirichlet_alpha;
    _mcts_params.rollouts_per_leaf = rollouts_per_leaf;
}

StepResult NardiEngine::advance()
{
    if(!should_continue_game())
        return StepResult::GameOver;

    // index 0 == white (player idx false), 1 == black (player idx true)
    const Strategy strat = _player_strats[static_cast<size_t>(current_player())];

    // Roll once for the current player. Skip if the turn is already complete and
    // merely awaiting confirmation (e.g. a prior no-move roll), so we don't re-roll.
    if(_builder.GetCtrl().AwaitingRoll() && !turn_is_complete())
        roll_and_enumerate();

    if(_last_children.empty())
    {
        // No legal moves: nothing to undo, so confirm the (forced) pass for
        // everyone -- bot or human -- and advance to the next player.
        confirm_turn();
        return StepResult::TurnPassed;
    }

    if(_last_children.size() == 1)
    {
        // Forced move: a single legal end-board. Auto-play it for everyone
        // (human or bot) -- no decision to make, so skip any search/selection.
        // The UI still animates it. apply_board auto-confirms / advances.
        const Nardi::BoardConfig board = _last_children.front().raw_data;
        apply_board(board);
        return StepResult::BotMoved;
    }

    if(strat == Strategy::Human)
        return StepResult::AwaitingHuman; // UI drives incremental moves + confirm_turn()

    switch(strat)
    {
    case Strategy::Greedy:
        apply_greedy_target();
        break;
    case Strategy::Lookahead:
        apply_lookahead_target();
        break;
    case Strategy::Mcts:
        mcts_apply_move(_mcts_params.n_sims, _mcts_params.temperature, _mcts_params.exploratory,
                        _mcts_params.c_uct, _mcts_params.dirichlet_eps,
                        _mcts_params.dirichlet_alpha, _mcts_params.rollouts_per_leaf);
        break;
    case Strategy::Heuristic:
        apply_heuristic_board();
        break;
    case Strategy::Random:
        apply_random_board();
        break;
    case Strategy::Human:
        break; // unreachable (handled above)
    }
    return StepResult::BotMoved;
}

std::vector<Nardi::Board::Features> NardiEngine::current_options() const
{
    return _last_children;
}

int NardiEngine::legal_move_count() const
{
    return static_cast<int>(_last_children.size());
}

void NardiEngine::apply_human_move(int idx)
{
    const auto& children = require_children();
    if(idx < 0 || static_cast<size_t>(idx) >= children.size())
        throw std::runtime_error("apply_human_move: move index out of range.");
    // Copy out before apply_board() clears _last_children.
    const Nardi::BoardConfig board = children.at(static_cast<size_t>(idx)).raw_data;
    apply_board(board);
}

bool NardiEngine::human_select(int row, int col)
{
    _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::RELEASE_SELECTED));
    const auto status = _builder.ReceiveCommand(Nardi::Command(row, col));
    return status == Nardi::status_codes::SUCCESS;
}

bool NardiEngine::human_move_die(int die_idx)
{
    _builder.GetGame().StartRecording();
    const auto status = _builder.ReceiveCommand(Nardi::Command(static_cast<bool>(die_idx)));
    _builder.GetGame().StopRecording();
    // SUCCESS: moved, turn continues (dest auto-selected). NO_LEGAL_MOVES_LEFT:
    // moved and that completed the turn (controller switched players).
    return status == Nardi::status_codes::SUCCESS
        || status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT;
}

bool NardiEngine::human_undo()
{
    _builder.GetGame().StartRecording();
    const auto status = _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::UNDO));
    _builder.GetGame().StopRecording();
    return status == Nardi::status_codes::SUCCESS;
}

std::vector<std::array<int, 4>> NardiEngine::recent_moves() const
{
    // Sub-moves of the last recorded command, in application order, as
    // {fromRow, fromCol, toRow, toCol}; -1 marks off-board (bear-off / replace).
    std::vector<std::array<int, 4>> out;
    for(const auto& m : _builder.GetGame().MoveLog())
        out.push_back({m.from.row, m.from.col, m.to.row, m.to.col});
    return out;
}

bool NardiEngine::can_use_die(int die_idx)
{
    if(_builder.GetCtrl().AwaitingRoll())
        return false;
    return _builder.GetGame().CanUseDice(static_cast<bool>(die_idx));
}

bool NardiEngine::start_is_selected() const
{
    return _builder.GetCtrl().StartIsSelected();
}

std::array<int, 2> NardiEngine::selected_start() const
{
    if(!_builder.GetCtrl().StartIsSelected())
        return {-1, -1};
    const auto& s = _builder.GetCtrl().GetStart();
    return {s.row, s.col};
}

int NardiEngine::starts_mask(int die_idx) const
{
    if(die_idx != 0 && die_idx != 1)
        return 0;
    int mask = 0;
    for(const auto& c : _builder.GetGame().GetStarts()[static_cast<size_t>(die_idx)])
        mask |= (1 << (c.row * Nardi::COLS + c.col));   // bit per square, row-major
    return mask;
}

void NardiEngine::refresh_forced()
{
    _builder.GetGame().RefreshForced();
}

bool NardiEngine::turn_in_progress() const
{
    return !_builder.GetCtrl().AwaitingRoll() && !_builder.GetGame().GameIsOver();
}

bool NardiEngine::turn_is_complete() const
{
    return _builder.GetCtrl().TurnIsComplete();
}

void NardiEngine::confirm_turn()
{
    // Advance to the next player. No-op unless the turn is complete (the
    // controller enforces "all dice used / no legal moves left").
    _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::CONFIRM_TURN_OVER));
}

void NardiEngine::apply_random_board()
{
    const auto& children = require_children();
    std::uniform_int_distribution<size_t> dist(0, children.size() - 1);
    apply_board(children.at(dist(_rng)).raw_data);
}

void NardiEngine::apply_heuristic_board()
{
    const auto& children = require_children();

    // Once the mover has every checker home (the bear-off phase), maximizing board
    // coverage is the wrong goal -- it spreads checkers out and stalls the race.
    // Switch objectives: bear off as many checkers as possible, breaking ties by the
    // lower remaining pip count (closer to finishing). Outside the endgame keep the
    // old behavior of maximizing coverage (sq_occ). Children are featured from the
    // mover's perspective, so `player.*` are this player's post-move values.
    const bool endgame = _builder.GetGame().GetBoardRef().CurrPlayerInEndgame();

    auto worse_than = [endgame](const auto& lhs, const auto& rhs)
    {
        // Comparator for max_element: true when lhs is the less-preferred move.
        if(endgame)
        {
            if(lhs.player.pieces_off != rhs.player.pieces_off)
                return lhs.player.pieces_off < rhs.player.pieces_off; // more off is better
            return lhs.player.pip_count > rhs.player.pip_count;       // fewer pips is better
        }
        return lhs.player.sq_occ < rhs.player.sq_occ;
    };

    auto best = std::max_element(children.begin(), children.end(), worse_than);
    apply_board(best->raw_data);
}

void NardiEngine::human_turn(bool dice_rolled)
{
    if(!_builder.GetView())
        throw std::runtime_error("Tried human moves without initializing view");

    _builder.GetView()->InstructionMessage("Awaiting command\n");

    Nardi::status_codes status;

    if(!dice_rolled)
        status = _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::ROLL_DICE));
    else
    {
        status = _builder.GetCtrl().AwaitingRoll() ? Nardi::status_codes::NO_LEGAL_MOVES_LEFT
                                                   : Nardi::status_codes::SUCCESS;
    }

    if(status != Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
        GetHumanInput();
}

void NardiEngine::restart_or_quit()
{
    if(!_builder.GetView())
        return;

    if(!_builder.GetCtrl().QuitRequested() && !_builder.GetCtrl().RestartRequested())
        GetHumanInput();

    if(_builder.GetCtrl().RestartRequested())
        reset();
}

bool NardiEngine::is_terminal() const
{
    return _builder.GetGame().GameIsOver();
}

int NardiEngine::winner_result() const
{
    if(!_builder.GetGame().GameIsOver())
        throw std::runtime_error("winner_result(): game not over");
    return _builder.GetGame().IsMars() ? 2 : 1;
}

std::vector<Nardi::Board::Features> NardiEngine::enumerate(Nardi::status_codes status)
{
    if(status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
        return {};
    if(status != Nardi::status_codes::SUCCESS)
    {
        Nardi::DispErrorCode(status);
        throw std::runtime_error("RollDice: unexpected controller status.");
    }
    if(_builder.GetCtrl().AwaitingRoll())
        throw std::runtime_error("Incorrect usage, have not yet rolled dice.");

    const auto& b2s = _builder.GetGame().GetBoards2Seqs();

    std::vector<Nardi::Board::Features> features_vec;
    features_vec.reserve(b2s.size());
    for(const auto& kv : b2s)
        features_vec.push_back(_builder.GetGame().GetBoardRef().ExtractFeatures(kv.first));

    return features_vec;
}

void NardiEngine::reset()
{
    _builder.Reset();
    _last_children.clear();
    _last_lookahead_batch.reset();
}

void NardiEngine::status_report()
{
    _builder.StatusReport();
}

std::string NardiEngine::status_str()
{
    return _builder.StatusString();
}

bool NardiEngine::should_continue_game() const
{
    return !(_builder.GetCtrl().QuitRequested() || _builder.GetGame().GameIsOver());
}

bool NardiEngine::quit_requested() const
{
    return _builder.GetCtrl().QuitRequested();
}

void NardiEngine::load_target_network(const std::string& path)
{
    _target_model.load(path);
}

float NardiEngine::debug_target_eval()
{
    // Evaluate the current board (side-to-move perspective) with the C++ target
    // model. For comparison against the Python model to validate the bridge.
    return _target_model.evaluate(board_features());
}

void NardiEngine::set_position(const Nardi::BoardConfig& brd, bool side)
{
    // Drop an arbitrary position in (board + side to move), pre-roll. Validity of
    // the static position (piece counts, endgame home restriction) is the caller's
    // responsibility; the engine accepts whatever board it is given.
    _builder.ResetPreRoll(side, brd);

    // Turn numbers drive the first-move head rule: the 4-4 / 6-6 "two checkers off
    // the head" exception fires only when the side to move has turn_number == 0. A
    // player sitting on the standard opening (all 15 on their head) is genuinely on
    // their first move, so give them turn_number 0; any other arrangement is past
    // the opening, so use a count > 1 (no first-move exception). Done per player so
    // an as-yet-unmoved opponent still gets its own first move handled correctly
    // once play switches to it.
    const auto at_start = [&brd](bool p) {
        return brd[p][0] == (p == Nardi::white ? Nardi::PIECES_PER_PLAYER
                                               : -Nardi::PIECES_PER_PLAYER);
    };
    _builder.SetTurnNumbers(at_start(Nardi::white) ? 0 : 2,
                            at_start(Nardi::black) ? 0 : 2);

    _last_children.clear();
    _last_lookahead_batch.reset();
    _analyzed.clear();
}

float NardiEngine::evaluate_position() const
{
    if(!_target_model.is_loaded())
        throw std::runtime_error("evaluate_position requires load_target_network(path) first.");
    return _target_model.evaluate(board_features());
}

std::vector<std::pair<Nardi::BoardConfig, float>> NardiEngine::analyze_dice(int d1, int d2)
{
    if(!_target_model.is_loaded())
        throw std::runtime_error("analyze_dice requires load_target_network(path) first.");

    const auto children = set_and_enumerate(d1, d2);   // set the dice on the position
    _analyzed.clear();
    // No legal move for these dice: it's a forced pass. The controller did NOT
    // mark the dice rolled, so building a lookahead batch here would throw
    // ("have not yet rolled dice"); return empty so the caller treats it as a pass.
    if(children.empty())
        return _analyzed;

    auto batch = MakeLookaheadBatch(); // one-ply frontier (caches _last_lookahead_batch)
    if(batch->children.empty())
        return _analyzed;

    const std::vector<float> values = _target_model.evaluate_batch(batch->eval_features);
    const std::vector<float> child_vals = batch->child_values_vec(values);

    _analyzed.reserve(batch->children.size());
    for(size_t i = 0; i < batch->children.size(); ++i)
        _analyzed.emplace_back(batch->children[i].board, child_vals[static_cast<size_t>(i)]);

    // Best move for the side to move first (child_values are in the mover frame).
    std::sort(_analyzed.begin(), _analyzed.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return _analyzed;
}

void NardiEngine::apply_analyzed_move(int idx)
{
    if(idx < 0 || static_cast<size_t>(idx) >= _analyzed.size())
        throw std::runtime_error("apply_analyzed_move: index out of range");
    const Nardi::BoardConfig board = _analyzed[static_cast<size_t>(idx)].first;
    apply_board(board);   // applies + confirms (switches side), records sub-moves
    _analyzed.clear();
}

int NardiEngine::analyzed_count() const
{
    return static_cast<int>(_analyzed.size());
}

Nardi::BoardConfig NardiEngine::analyzed_board(int idx) const
{
    if(idx < 0 || static_cast<size_t>(idx) >= _analyzed.size())
        throw std::runtime_error("analyzed_board: index out of range");
    return _analyzed[static_cast<size_t>(idx)].first;
}

float NardiEngine::analyzed_value(int idx) const
{
    if(idx < 0 || static_cast<size_t>(idx) >= _analyzed.size())
        throw std::runtime_error("analyzed_value: index out of range");
    return _analyzed[static_cast<size_t>(idx)].second;
}

std::vector<std::pair<Nardi::Board::Features, float>> NardiEngine::run_mcts_game(
    int n_sims,
    float temperature,
    int max_turns,
    float c_uct,
    float dirichlet_eps,
    float dirichlet_alpha,
    int rollouts_per_leaf)
{
    if(!_target_model.is_loaded())
        throw std::runtime_error("run_mcts_game requires load_target_network(path) first.");
    if(n_sims <= 0)
        throw std::runtime_error("run_mcts_game n_sims must be positive.");
    if(temperature <= 0.0f)
        throw std::runtime_error("run_mcts_game temperature must be positive.");
    if(max_turns <= 0)
        throw std::runtime_error("run_mcts_game max_turns must be positive.");

    reset();
    _builder.ToSimMode();

    // Apply the search/exploration tunables to every tree built this game.
    const auto configure = [&](MCTSTree& t)
    {
        t.c_uct = c_uct;
        t.dirichlet_eps = dirichlet_eps;
        t.dirichlet_alpha = dirichlet_alpha;
        t.rollouts_per_leaf = rollouts_per_leaf;
    };

    // Record (features, side-to-move) for every position we actually move from.
    // The value target is assigned only once the game ends: the actual outcome
    // (Monte-Carlo / AGZ value target), expressed in each position's side-to-move
    // frame. This is unbiased w.r.t. the search; the MCTS only drives play.
    struct Pending
    {
        Nardi::Board::Features features;
        bool player;
    };
    std::vector<Pending> pending;
    pending.reserve(128);

    try
    {
        for(int turn = 0; turn < max_turns && !_builder.GetGame().GameIsOver(); ++turn)
        {
            const bool player = current_player();
            const auto& board = _builder.GetGame().GetBoardData();

            const auto status = _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::ROLL_DICE));
            if(status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
            {
                _builder.GetCtrl().AdvanceSimTurn(); // forced pass, no decision recorded
                continue;
            }
            if(status != Nardi::status_codes::SUCCESS)
            {
                Nardi::DispErrorCode(status);
                throw std::runtime_error("MCTS self-play failed to roll dice.");
            }

            const auto legal_boards = legal_boards_for_current_dice(_builder);
            if(legal_boards.empty())
                throw std::runtime_error("MCTS self-play roll succeeded with no legal boards.");

            // Fresh tree each move, rooted at the real rolled dice.
            MCTSTree tree(board, player);
            configure(tree);
            tree.run_simulations(n_sims, _builder, _target_model, _rng);

            pending.push_back({tree.root_features(), player});

            const auto chosen = tree.select_move(legal_boards, temperature, _rng);
            const auto move_status = _builder.SimulateMove(chosen);
            if(move_status != Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
            {
                Nardi::DispErrorCode(move_status);
                throw std::runtime_error("MCTS self-play failed to apply selected move.");
            }
        }
    }
    catch(...)
    {
        _builder.EndSimMode();
        throw;
    }

    // Label every recorded position with the final game outcome. After the
    // winning move the controller has switched to the loser, so winner =
    // !current_player and the margin is winner_result() (1 normal, 2 mars).
    std::vector<std::pair<Nardi::Board::Features, float>> samples;
    if(_builder.GetGame().GameIsOver())
    {
        const float margin = static_cast<float>(winner_result());
        const bool winner = !current_player();
        samples.reserve(pending.size());
        for(const auto& p : pending)
        {
            const float target = (p.player == winner) ? margin : -margin;
            samples.emplace_back(p.features, target);
        }
    }
    // else: game hit max_turns without finishing -> no outcome, drop its samples.

    _builder.EndSimMode();
    _last_children.clear();
    _last_lookahead_batch.reset();
    return samples;
}

void NardiEngine::mcts_apply_move(
    int n_sims,
    float temperature,
    bool exploratory,
    float c_uct,
    float dirichlet_eps,
    float dirichlet_alpha,
    int rollouts_per_leaf)
{
    if(!_target_model.is_loaded())
        throw std::runtime_error("mcts_apply_move requires load_target_network(path) first.");
    if(n_sims <= 0)
        throw std::runtime_error("mcts_apply_move n_sims must be positive.");
    if(_builder.GetCtrl().AwaitingRoll())
        throw std::runtime_error("mcts_apply_move requires dice to be rolled first.");

    const auto legal_boards = legal_boards_for_current_dice(_builder);
    if(legal_boards.empty())
        throw std::runtime_error("mcts_apply_move called with no legal moves.");

    if(legal_boards.size() == 1)
    {
        // Forced move: skip the MCTS search and just play the only legal board.
        apply_board(legal_boards.front());
        return;
    }

    MCTSTree tree(_builder.GetGame().GetBoardData(), current_player());
    tree.c_uct = c_uct;
    tree.dirichlet_eps = dirichlet_eps;
    tree.dirichlet_alpha = dirichlet_alpha;
    tree.rollouts_per_leaf = rollouts_per_leaf;

    // Run the search headlessly on copies of the real builder, then apply the
    // chosen move through the normal (non-sim) path so graphics/turn-switching
    // behave exactly as for the other move strategies.
    _builder.ToSimMode();
    try
    {
        tree.run_simulations(n_sims, _builder, _target_model, _rng);
    }
    catch(...)
    {
        _builder.EndSimMode();
        throw;
    }
    _builder.EndSimMode();

    const Nardi::BoardConfig chosen = exploratory
        ? tree.select_move(legal_boards, temperature, _rng)
        : tree.select_best(legal_boards);

    apply_board(chosen);
}

const std::vector<Nardi::Board::Features>& NardiEngine::require_children() const
{
    if(_last_children.empty())
        throw std::runtime_error("No legal children cached. Roll/set dice and enumerate first.");
    return _last_children;
}

std::shared_ptr<LookaheadBatch> NardiEngine::require_lookahead_batch() const
{
    if(!_last_lookahead_batch)
        throw std::runtime_error("No lookahead batch available. Call make_lookahead_batch() first.");
    if(_last_lookahead_batch->children.empty())
        throw std::runtime_error("Cannot apply lookahead move from an empty batch.");
    return _last_lookahead_batch;
}

std::optional<size_t> NardiEngine::terminal_child_index(
    const std::vector<Nardi::Board::Features>& children)
{
    for(size_t i = 0; i < children.size(); ++i)
        if(terminal_value_for_side_to_move(children[i]).has_value())
            return i;

    return std::nullopt;
}

int NardiEngine::flat_dice_idx(int d1, int d2)
{
    if(d1 > d2)
        std::swap(d1, d2);

    std::array<int, 2> dice = {d1, d2};

    int row_from_top = (7 - dice[0]);
    int row_offset = (row_from_top * (row_from_top + 1) / 2);
    int d_idx = N_DICE_COMB - row_offset + (dice[1] - dice[0]);

    return d_idx;
}

std::vector<Nardi::Board::Features> NardiEngine::set_and_enumerate(
    int d1,
    int d2,
    Nardi::ScenarioBuilder& b)
{
    std::array<int, 2> new_dice = {d1, d2};
    auto status = b.ReceiveCommand(Nardi::Command(new_dice));

    if(status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
        return {};
    if(status != Nardi::status_codes::SUCCESS)
    {
        Nardi::DispErrorCode(status);
        throw std::runtime_error("SetDice: unexpected controller status in lookahead search.");
    }

    const auto& b2s = b.GetGame().GetBoards2Seqs();

    std::vector<Nardi::Board::Features> features_vec;
    features_vec.reserve(b2s.size());
    for(const auto& kv : b2s)
        features_vec.push_back(b.GetGame().GetBoardRef().ExtractFeatures(kv.first));

    return features_vec;
}

void NardiEngine::GetHumanInput()
{
    if(!_builder.GetView())
        return;

    while(true)
    {
        Nardi::status_codes status = _builder.GetView()->PollInput();

        if(status != Nardi::status_codes::WAITING)
            _builder.GetView()->DispErrorCode(status);

        if(status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
            break;
    }
}

} // namespace nardi_py
