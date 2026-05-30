#include "nardi_engine.h"

#include <algorithm>
#include <future>
#include <iostream>
#include <stdexcept>

namespace nardi_py
{

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
    _builder.AttachNewRW(Nardi::SFMLRWFactory());
    _builder.GetView()->PollInput();
}

void NardiEngine::DetachRW()
{
    _builder.DetachRW();
}

py::array_t<uint8_t> NardiEngine::dice() const
{
    std::array<int, 2> dice = {
        _builder.GetGame().GetDice(0),
        _builder.GetGame().GetDice(1)
    };
    py::array_t<uint8_t> arr(py::ssize_t(2));

    auto buf = arr.mutable_unchecked<1>();
    buf(0) = dice[0];
    buf(1) = dice[1];

    return arr;
}

int NardiEngine::dice_as_idx() const
{
    return flat_dice_idx(_builder.GetGame().GetDice(0), _builder.GetGame().GetDice(1));
}

Nardi::Board::Features NardiEngine::board_features() const
{
    return _builder.GetGame().GetBoardRef().ExtractFeatures();
}

void NardiEngine::PrintArray3D(py::array_t<uint8_t>& arr)
{
    auto buf = arr.unchecked<3>();
    auto shape = arr.shape();
    std::cout << "Array shape: [" << shape[0] << "," << shape[1] << "," << shape[2] << "]\n";
    for(ssize_t i = 0; i < shape[0]; ++i)
    {
        std::cout << "Index " << i << ":\n";
        for(ssize_t r = 0; r < shape[1]; ++r)
        {
            for(ssize_t c = 0; c < shape[2]; ++c)
                std::cout << static_cast<int>(buf(i, r, c)) << " ";
            std::cout << "\n";
        }
        std::cout << "----------------------\n";
    }
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
    auto status = _builder.ReceiveCommand(Nardi::Command(brd));
    if(status != Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
    {
        Nardi::DispErrorCode(status);
        throw std::runtime_error("Autoplay failed to complete");
    }
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

void NardiEngine::apply_best_lookahead(
    py::array_t<float, py::array::c_style | py::array::forcecast> values)
{
    auto batch = require_lookahead_batch();
    apply_board(batch->children.at(static_cast<size_t>(batch->best_index(values))).board);
}

void NardiEngine::apply_noisy_board(
    py::array_t<float, py::array::c_style | py::array::forcecast> values,
    float eps,
    float temperature)
{
    const auto& children = require_children();
    if(const auto terminal_idx = terminal_child_index(children); terminal_idx.has_value())
    {
        apply_board(children.at(terminal_idx.value()).raw_data);
        return;
    }

    std::vector<float> parsed_values = parse_1d_values(values, children.size());
    const int idx = sample_noisy_index(parsed_values, eps, temperature, _rng);
    apply_board(children.at(static_cast<size_t>(idx)).raw_data);
}

void NardiEngine::apply_greedy_board(
    py::array_t<float, py::array::c_style | py::array::forcecast> values)
{
    const auto& children = require_children();
    if(const auto terminal_idx = terminal_child_index(children); terminal_idx.has_value())
    {
        apply_board(children.at(terminal_idx.value()).raw_data);
        return;
    }

    std::vector<float> parsed_values = parse_1d_values(values, children.size());

    auto best = std::max_element(parsed_values.begin(), parsed_values.end());
    const size_t idx = static_cast<size_t>(std::distance(parsed_values.begin(), best));
    apply_board(children.at(idx).raw_data);
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
    auto best = std::max_element(
        children.begin(),
        children.end(),
        [](const auto& lhs, const auto& rhs)
        {
            return lhs.player.sq_occ < rhs.player.sq_occ;
        });
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
