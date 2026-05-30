// bindings.cpp
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <numeric>
#include <string>
#include <thread>
#include <variant>
#include <vector>
#include <stdexcept>

namespace py = pybind11;

#include "../CoreEngine/ScenarioBuilder.h"
#include "../CoreEngine/Game.h"
#include "../CoreEngine/Controller.h"
#include "../CoreEngine/Auxilaries.h"
#include "../CoreEngine/ReaderWriter.h"
#include "../CoreEngine/TerminalRW.h"
#include "../CoreEngine/SFMLRW.h"



static constexpr int N_DICE_COMB = 21;

static constexpr int DICE_COMBOS[21][2] = { {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 6},
                                                    {2, 2}, {2, 3}, {2, 4}, {2, 5}, {2, 6},
                                                            {3, 3}, {3, 4}, {3, 5}, {3, 6},
                                                                    {4, 4}, {4, 5}, {4, 6},
                                                                            {5, 5}, {5, 6},
                                                                                    {6, 6} };

static constexpr float COMBO_PROBS[21] = {
                        1.0/36.0,   1.0/18.0,   1.0/18.0,   1.0/18.0,   1.0/18.0,   1.0/18.0, 
                                    1.0/36.0,   1.0/18.0,   1.0/18.0,   1.0/18.0,   1.0/18.0, 
                                                1.0/36.0,   1.0/18.0,   1.0/18.0,   1.0/18.0, 
                                                            1.0/36.0,   1.0/18.0,   1.0/18.0,  
                                                                        1.0/36.0,   1.0/18.0,  
                                                                                    1.0/36.0 };

static constexpr int FEATURE_ROWS = 6;
static constexpr int FEATURE_COLS = Nardi::ROWS * Nardi::COLS + 1;
static constexpr float FEATURE_SCALE = 15.0f;

enum class FeaturePipelineKind
{
    LEGACY,
    CONV
};

FeaturePipelineKind parse_pipeline_kind(const std::string& kind)
{
    if(kind == "legacy")
        return FeaturePipelineKind::LEGACY;
    if(kind == "conv")
        return FeaturePipelineKind::CONV;
    throw std::runtime_error("Unknown feature pipeline kind. Expected 'legacy' or 'conv'.");
}

void fill_feature_matrix(
    const Nardi::Board::Features& f,
    float* out,
    FeaturePipelineKind kind)
{
    for(int row = 0; row < 3; ++row)
        for(int col = 0; col < Nardi::ROWS * Nardi::COLS; ++col)
            out[row * FEATURE_COLS + col] = static_cast<float>(f.player.occ[row][col]) / FEATURE_SCALE;

    for(int row = 0; row < 3; ++row)
        for(int col = 0; col < Nardi::ROWS * Nardi::COLS; ++col)
            out[(row + 3) * FEATURE_COLS + col] = static_cast<float>(f.opp.occ[row][col]) / FEATURE_SCALE;

    std::array<float, FEATURE_ROWS> scalars;
    if(kind == FeaturePipelineKind::LEGACY)
    {
        scalars = {
            static_cast<float>(f.player.pieces_off),
            static_cast<float>(f.opp.pieces_off),
            static_cast<float>(f.player.sq_occ),
            static_cast<float>(f.opp.sq_occ),
            static_cast<float>(f.player.pieces_not_reached),
            static_cast<float>(f.opp.pieces_not_reached)
        };
    }
    else
    {
        scalars = {
            static_cast<float>(f.player.pieces_off),
            static_cast<float>(f.opp.pieces_off),
            static_cast<float>(f.player.pip_count),
            static_cast<float>(f.opp.pip_count),
            static_cast<float>(f.player.pieces_not_reached),
            static_cast<float>(f.opp.pieces_not_reached)
        };
    }

    for(int row = 0; row < FEATURE_ROWS; ++row)
        out[row * FEATURE_COLS + (FEATURE_COLS - 1)] = scalars[row] / FEATURE_SCALE;
}

py::array_t<float> features_to_tensor(
    const Nardi::Board::Features& f,
    const std::string& kind = "conv",
    bool flatten = false)
{
    const auto parsed_kind = parse_pipeline_kind(kind);

    if(flatten)
    {
        py::array_t<float> arr({py::ssize_t(1), py::ssize_t(FEATURE_ROWS * FEATURE_COLS)});
        auto buf = arr.mutable_unchecked<2>();
        fill_feature_matrix(f, &buf(0, 0), parsed_kind);
        return arr;
    }

    py::array_t<float> arr({py::ssize_t(1), py::ssize_t(FEATURE_ROWS), py::ssize_t(FEATURE_COLS)});
    auto buf = arr.mutable_unchecked<3>();
    fill_feature_matrix(f, &buf(0, 0, 0), parsed_kind);
    return arr;
}

py::array_t<float> feature_batch_to_tensor(
    const std::vector<Nardi::Board::Features>& features,
    const std::string& kind = "conv",
    bool flatten = false)
{
    const auto parsed_kind = parse_pipeline_kind(kind);
    const py::ssize_t n = static_cast<py::ssize_t>(features.size());

    if(flatten)
    {
        py::array_t<float> arr({n, py::ssize_t(FEATURE_ROWS * FEATURE_COLS)});
        auto buf = arr.mutable_unchecked<2>();
        for(py::ssize_t i = 0; i < n; ++i)
            fill_feature_matrix(features[static_cast<size_t>(i)], &buf(i, 0), parsed_kind);
        return arr;
    }

    py::array_t<float> arr({n, py::ssize_t(FEATURE_ROWS), py::ssize_t(FEATURE_COLS)});
    auto buf = arr.mutable_unchecked<3>();
    for(py::ssize_t i = 0; i < n; ++i)
        fill_feature_matrix(features[static_cast<size_t>(i)], &buf(i, 0, 0), parsed_kind);

    return arr;
}

static py::array_t<int8_t> board_to_array(const Nardi::BoardConfig& board)
{
    py::array_t<int8_t> arr({py::ssize_t(Nardi::ROWS), py::ssize_t(Nardi::COLS)});
    auto buf = arr.mutable_unchecked<2>();

    for(py::ssize_t r = 0; r < Nardi::ROWS; ++r)
        for(py::ssize_t c = 0; c < Nardi::COLS; ++c)
            buf(r, c) = board[static_cast<size_t>(r)][static_cast<size_t>(c)];

    return arr;
}

std::vector<float> parse_1d_values(
    py::array_t<float, py::array::c_style | py::array::forcecast> values,
    size_t expected_size)
{
    if(values.ndim() != 1)
        throw std::runtime_error("Expected a 1D values array.");
    if(static_cast<size_t>(values.shape(0)) != expected_size)
        throw std::runtime_error("Values length does not match board count.");

    auto buf = values.unchecked<1>();
    std::vector<float> parsed(expected_size);
    for(py::ssize_t i = 0; i < values.shape(0); ++i)
        parsed[static_cast<size_t>(i)] = buf(i);
    return parsed;
}

int sample_noisy_index(
    const std::vector<float>& values,
    float eps,
    float temperature,
    std::mt19937& rng)
{
    if(values.empty())
        throw std::runtime_error("Cannot sample from empty values.");
    if(values.size() == 1)
        return 0;
    if(eps < 0.0f || eps > 1.0f)
        throw std::runtime_error("Noisy move eps must be in [0, 1].");
    if(temperature <= 0.0f)
        throw std::runtime_error("Noisy move temperature must be positive.");

    const float max_value = *std::max_element(values.begin(), values.end());
    std::vector<double> priors;
    priors.reserve(values.size());

    double total = 0.0;
    for(float value : values)
    {
        const double p = std::exp(static_cast<double>((value - max_value) / temperature));
        priors.push_back(p);
        total += p;
    }
    for(double& p : priors)
        p /= total;

    const double alpha = std::clamp(6.0 / static_cast<double>(values.size()), 0.2, 0.8);
    std::gamma_distribution<double> gamma(alpha, 1.0);
    std::vector<double> noise(priors.size());

    double noise_total = 0.0;
    for(double& w : noise)
    {
        w = gamma(rng);
        noise_total += w;
    }

    if(noise_total == 0.0)
    {
        const double uniform = 1.0 / static_cast<double>(noise.size());
        std::fill(noise.begin(), noise.end(), uniform);
    }
    else
    {
        for(double& w : noise)
            w /= noise_total;
    }

    std::vector<double> final_probs(priors.size());
    for(size_t i = 0; i < priors.size(); ++i)
        final_probs[i] = (1.0 - eps) * priors[i] + eps * noise[i];

    std::discrete_distribution<int> dist(final_probs.begin(), final_probs.end());
    return dist(rng);
}

std::optional<float> terminal_value_for_side_to_move(const Nardi::Board::Features& f)
{
    if(f.player.pieces_off < Nardi::PIECES_PER_PLAYER)
        return std::nullopt;

    return f.opp.pieces_off == 0 ? 2.0f : 1.0f;
}

/*

ScenarioConfig exposes a safer view of ScenarioBuilder which only crafts scenarios without messing 
with internals like RW pointers and other potentially error-causing things

*/

class ScenarioConfig
{
    public:
        ScenarioConfig(Nardi::ScenarioBuilder& sb) : _builder(sb) {}

        Nardi::status_codes withScenario(bool p_idx, const Nardi::BoardConfig& b, int d1, int d2, int d1u=0, int d2u=0)
        {
            return _builder.withScenario(p_idx, b, d1, d2, d1u, d2u);
        }

        Nardi::status_codes withScenario(
            bool p_idx, 
            py::array_t<int8_t, py::array::c_style | py::array::forcecast> board,
            int d1, int d2, int d1u = 0, int d2u = 0)
        {
            if (board.ndim() != 2 || board.shape(0) != Nardi::ROWS || board.shape(1) != Nardi::COLS)
                throw std::runtime_error("BoardConfig must be shape (2, 12)");

            Nardi::BoardConfig cfg;
            auto buf = board.unchecked<2>();

            for (size_t r = 0; r < Nardi::ROWS; ++r)
                for (size_t c = 0; c < Nardi::COLS; ++c)
                    cfg[r][c] = buf(r, c);

            return withScenario(p_idx, cfg, d1, d2, d1u, d2u);
        }

        Nardi::status_codes withDice(int d1, int d2, int d1_used, int d2_used)
        {
            return _builder.withDice(d1, d2, d1_used, d2_used);
        }        

        void withRandomEndgame(bool p_idx)
        {
            _builder.withRandomEndgame(p_idx);
        }    

        void withFirstTurn() { _builder.withFirstTurn(); }

    private:
        Nardi::ScenarioBuilder& _builder;
};

class LookaheadBatch
{
public:
    struct DiceGroup
    {
        std::variant<std::vector<int>, float> data;
    };

    struct ChildChoice
    {
        Nardi::BoardConfig board;
        std::optional<float> terminal_value;
        std::array<DiceGroup, N_DICE_COMB> dice_groups;
    };

    std::vector<ChildChoice> children;
    std::vector<Nardi::Board::Features> eval_features;

    int num_children() const
    {
        return static_cast<int>(children.size());
    }

    int num_eval_features() const
    {
        return static_cast<int>(eval_features.size());
    }

    py::array_t<float> tensor(const std::string& kind = "conv", bool flatten = false) const
    {
        return feature_batch_to_tensor(eval_features, kind, flatten);
    }

    py::array_t<float> child_values(
        py::array_t<float, py::array::c_style | py::array::forcecast> values) const
    {
        /*
            given the full batched evaluation from python, we use the child_values function to find the lookahead value of each child.
            This function returns an array of valuations all child boards.
        */
        std::vector<float> parsed_values = parse_values(values);
        py::array_t<float> arr({py::ssize_t(children.size())});
        auto out = arr.mutable_unchecked<1>();

        for(size_t i = 0; i < children.size(); ++i)
            out(static_cast<py::ssize_t>(i)) = child_value(children[i], parsed_values);

        return arr;
    }

    int best_index(py::array_t<float, py::array::c_style | py::array::forcecast> values) const
    /*
        Uses the child_values function to get the updated lookahead evaluation for each child, then simply
        identifies the index with highest root-perspective valuation to later apply that move
    */
    {
        if(children.empty())
            throw std::runtime_error("Cannot select from an empty lookahead batch.");

        std::vector<float> parsed_values = parse_values(values);
        int best = 0;
        float best_value = child_value(children[0], parsed_values);

        for(size_t i = 1; i < children.size(); ++i)
        {
            const float candidate = child_value(children[i], parsed_values);
            if(candidate > best_value)
            {
                best = static_cast<int>(i);
                best_value = candidate;
            }
        }

        return best;
    }

    py::array_t<int8_t> best_board(
        py::array_t<float, py::array::c_style | py::array::forcecast> values) const
    // returns the best board according to expectimax search as a numpy array to python
    {
        return board_to_array(children.at(static_cast<size_t>(best_index(values))).board);
    }

private:
    std::vector<float> parse_values(     // turns values into a vector of floats
        py::array_t<float, py::array::c_style | py::array::forcecast> values) const
    {
        if(values.ndim() != 1)
            throw std::runtime_error("Lookahead values must be a 1D array.");
        if(values.shape(0) != static_cast<py::ssize_t>(eval_features.size()))
            throw std::runtime_error("Lookahead values length does not match eval feature count.");

        auto buf = values.unchecked<1>();
        std::vector<float> parsed(static_cast<size_t>(values.shape(0)));
        for(py::ssize_t i = 0; i < values.shape(0); ++i)
            parsed[static_cast<size_t>(i)] = buf(i);
        return parsed;
    }

    static float child_value(const ChildChoice& child, const std::vector<float>& values)
    /*
    Given a child and the full flat vector of evaluations, we want to find the value of this child position

        Case 1, this child is already terminal --> just return its terminal evaluation

        Case 2, else --> iterate over every dice group. If a dice results in no legal moves, then there is exactly one position to evaluate: the orignal child 
                         with players reversed. In any case the root-perspective eval of this child is the minimum of all these grandchildren evals. This is the
                         min part of the expectimax search.
                         Average all of these dice group evaluations weighted by the probability of that dice combo. This is the final evaluation of the child. 
    */
    {
        if(child.terminal_value.has_value())
            return child.terminal_value.value();

        float avg_child_eval = 0.0f;
        for(int d_idx = 0; d_idx < N_DICE_COMB; ++d_idx)
        {
            const DiceGroup& group = child.dice_groups[d_idx];
            float dice_value;

            if(std::holds_alternative<float>(group.data))   // terminal grandchild
            {
                // Terminal opponent wins are already stored from the root player's perspective.
                dice_value = std::get<float>(group.data);
            }
            else
            {
                // Non-terminal grandchildren are also stored from the root player's perspective,
                // so the opponent chooses the minimum-valued continuation.
                const auto& eval_indices = std::get<std::vector<int>>(group.data);
                if(eval_indices.empty())
                    throw std::runtime_error("Lookahead dice group has no eval indices.");

                dice_value = std::numeric_limits<float>::infinity();
                for(int eval_idx : eval_indices)
                    dice_value = std::min(dice_value, values.at(static_cast<size_t>(eval_idx)));    // min part of minimax
            }

            avg_child_eval += dice_value * COMBO_PROBS[d_idx];
        }

        return avg_child_eval;
    }
};

// A thin wrapper that owns a live Game and provides Python-friendly methods.
class NardiEngine {
public:
    NardiEngine()
    : _builder(), _config(_builder)
    {
        _builder.withFirstTurn();
    }

    ScenarioConfig& GetConfig() 
    { return _config; }

    void AttachNewTRW()
    {
        _builder.AttachNewRW(Nardi::TerminalRWFactory());
    }

    void AttachNewSFMLRW()
    {
        _builder.AttachNewRW(Nardi::SFMLRWFactory());
        _builder.GetView()->PollInput();
    }

    void DetachRW()
    {
        _builder.DetachRW();
    }

    py::array_t<uint8_t> dice() const
    {
        std::array<int, 2> dice = { _builder.GetGame().GetDice(0), _builder.GetGame().GetDice(1) };
        py::array_t<uint8_t> arr({py::ssize_t(2)});

        auto buf = arr.mutable_unchecked<1>();
        buf(0) = dice[0];
        buf(1) = dice[1];

        return arr;
    }

    int dice_as_idx() const
    {
        return flat_dice_idx( _builder.GetGame().GetDice(0), _builder.GetGame().GetDice(1));
    }

    // Return 6x25 uint8 (player-perspective)

    Nardi::Board::Features board_features() const {
        return _builder.GetGame().GetBoardRef().ExtractFeatures();
    }

    // Helper to print a 3D numpy array (uint8) as [N,2,25]
    void PrintArray3D(py::array_t<uint8_t>& arr) {
        auto buf = arr.unchecked<3>();
        auto shape = arr.shape();
        std::cout << "Array shape: [" << shape[0] << "," << shape[1] << "," << shape[2] << "]\n";
        for (ssize_t i = 0; i < shape[0]; ++i) {
            std::cout << "Index " << i << ":\n";
            for (ssize_t r = 0; r < shape[1]; ++r) {
                for (ssize_t c = 0; c < shape[2]; ++c) {
                    std::cout << (int)buf(i, r, c) << " ";
                }
                std::cout << "\n";
            }
            std::cout << "----------------------\n";
        }
    }

    void Render() {

        _builder.Render();
    }

    bool roll_has_children() {
        auto status = _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::ROLL_DICE));
        _last_children = enumerate(status);
        return !_last_children.empty();
    }

    // Roll dice & return only board keys as [N,6,25] uint8 (end-of-turn leaves).
    std::vector<Nardi::Board::Features> roll_and_enumerate() {
        const auto status = _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::ROLL_DICE));

        _last_children = enumerate(status);
        return _last_children;
    }

    // Set dice & return only board keys as [N,6,25] uint8 (end-of-turn leaves).
    std::vector<Nardi::Board::Features> set_and_enumerate(int d1, int d2) 
    {
        std::array<int, 2> new_dice = {d1, d2};
        const auto status = _builder.ReceiveCommand(Nardi::Command(new_dice));

        _last_children = enumerate(status);
        return _last_children;
    }

    void apply_board(const Nardi::BoardConfig& brd) 
    {
        auto status = _builder.ReceiveCommand(Nardi::Command(brd));   // will autoplay this board via controller
        if(status != Nardi::status_codes::NO_LEGAL_MOVES_LEFT){
            Nardi::DispErrorCode(status);
            throw std::runtime_error("Autoplay failed to complete");
        }
        _last_children.clear();
        _last_lookahead_batch.reset();
    }

    bool current_player() const
    {
        return _builder.GetGame().GetBoardRef().PlayerIdx();
    }

    int sign() const
    {
        return current_player() ? -1 : 1;
    }

    int turn_num() const
    {
        return _builder.GetGame().GetTotalTurnNumber();
    }

    int player_turn_num(bool player) const
    {
        return _builder.GetGame().GetTurnNumber(player);
    }

    struct Node;

    struct ChanceAndChildren
    {
        float prob;
        std::vector<std::shared_ptr<Node>> data;
    };

    struct Node
    {
        Node() {
            for(int i = 0; i < N_DICE_COMB; ++i)
                children_by_dice[i].prob = COMBO_PROBS[i];
        }
        std::optional<int> result;  // present only if terminal
        Nardi::Board::Features features;
        std::array<ChanceAndChildren, N_DICE_COMB> children_by_dice;

        bool isLeaf() {
            for(const auto& ch_ch : children_by_dice)
                if(!ch_ch.data.empty())
                    return false;
            return true;
        }

        std::shared_ptr<Node> MakeChild(const Nardi::Board::Features& f, int d_idx)
        {
            auto child = std::make_shared<Node>();
            child->features = f;
            children_by_dice[d_idx].data.push_back(child);
            return child;
        }
    };

    // std::shared_ptr<Node> OnePlyLookahead()
    // {
    //     std::shared_ptr<Node> root = std::make_shared<Node>();
    //     root->features = board_features();
        
    //     auto children_features = enumerate(Nardi::status_codes::SUCCESS);   
    //         // only considers current dice and game state

    //     int d_idx = dice_as_idx();

    //     _builder.ToSimMode();

    //     for(int i = 0; i < children_features.size(); ++i)
    //     {
    //         root->MakeChild(children_features[i], d_idx);
    //         auto child = root->children_by_dice[d_idx].data.at(i);
    //         _builder.SimulateMove(child->features.raw_data);    // auto-play this position

    //         if(!_builder.GetGame().GameIsOver())
    //         {
    //             #pragma omp parallel for schedule(dynamic)
    //             for(int c = 0; c < N_DICE_COMB; ++c)
    //             {
    //                 std::vector<std::shared_ptr<Node>> buffer;                
    //                 Nardi::ScenarioBuilder builder(_builder);
    //                 auto& dice = DICE_COMBOS[c];
    //                 auto feats = set_and_enumerate(dice[0], dice[1], builder);
    //                 for (auto& f : feats)
    //                 {
    //                     auto gc = std::make_shared<Node>();
    //                     gc->features = f;
    //                     buffer.push_back(gc);
    //                 }
    //                 child->children_by_dice[c].data = std::move(buffer);                    
    //             }
    //         }
    //         _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::UNDO_TURN));  // undo
    //     }

    //     _builder.EndSimMode();

    //     return root;
    // }

    std::shared_ptr<LookaheadBatch> MakeLookaheadBatch()
    {
        auto batch = std::make_shared<LookaheadBatch>();
        auto legal_children = enumerate(Nardi::status_codes::SUCCESS);
        if(legal_children.empty()) {
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
                    // Preserve the old simulator shortcut: if a child wins immediately,
                    // take the true terminal result and ignore model-valued alternatives.
                    break;
                }

                auto status = _builder.SimulateMove(child_feature.raw_data);
                if(status != Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
                {
                    Nardi::DispErrorCode(status);
                    throw std::runtime_error("Lookahead failed to simulate a child move.");
                }

                Nardi::ScenarioBuilder after_child(_builder);

                // Each DiceResult is independent once the child move has been applied:
                // copy the post-child builder, set one dice combination, and enumerate
                // the opponent's legal end-of-turn boards.
                struct DiceResult
                {
                    int dice_idx;
                    std::vector<Nardi::Board::Features> features;
                };

                std::vector<std::future<DiceResult>> futures;
                futures.reserve(N_DICE_COMB);

                // Fan out the 21 chance outcomes. Every task owns its ScenarioBuilder copy,
                // so the game-state mutation inside set_and_enumerate is thread-local.
                for(int d_idx = 0; d_idx < N_DICE_COMB; ++d_idx)
                {
                    futures.push_back(std::async(
                        std::launch::async,
                        // Capture after_child by value on purpose. Each worker mutates
                        // its own copied builder once, so no cleanup is needed between dice.
                        [this, after_child, d_idx]() mutable -> DiceResult
                        {
                            const auto& dice = DICE_COMBOS[d_idx];
                            return DiceResult{
                                d_idx,
                                set_and_enumerate(dice[0], dice[1], after_child)
                            };
                        }));
                }

                // Join the dice tasks before touching the shared LookaheadBatch.
                // This keeps batch mutation single-threaded and avoids synchronization.
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

    void apply_best_lookahead(py::array_t<float, py::array::c_style | py::array::forcecast> values)
    {
        auto batch = require_lookahead_batch();
        apply_board(batch->children.at(static_cast<size_t>(batch->best_index(values))).board);
    }

    void apply_noisy_board(
        py::array_t<float, py::array::c_style | py::array::forcecast> values,
        float eps,
        float temperature)
    {
        const auto& children = require_children();
        std::vector<float> parsed_values = parse_1d_values(values, children.size());
        const int idx = sample_noisy_index(parsed_values, eps, temperature, _rng);
        apply_board(children.at(static_cast<size_t>(idx)).raw_data);
    }

    void apply_greedy_board(py::array_t<float, py::array::c_style | py::array::forcecast> values)
    {
        const auto& children = require_children();
        std::vector<float> parsed_values = parse_1d_values(values, children.size());

        auto best = std::max_element(parsed_values.begin(), parsed_values.end());
        const size_t idx = static_cast<size_t>(std::distance(parsed_values.begin(), best));
        apply_board(children.at(idx).raw_data);
    }

    void apply_random_board()
    {
        const auto& children = require_children();
        std::uniform_int_distribution<size_t> dist(0, children.size() - 1);
        apply_board(children.at(dist(_rng)).raw_data);
    }

    void apply_heuristic_board()
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

    void human_turn(bool dice_rolled = false) 
    {
        if(!_builder.GetView())
            throw std::runtime_error("Tried human moves without initializing view");
        
        _builder.GetView()->InstructionMessage("Awaiting command\n");

        Nardi::status_codes status;

        if(!dice_rolled)
            status = _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::ROLL_DICE));
        else {
            status = _builder.GetCtrl().AwaitingRoll() ? Nardi::status_codes::NO_LEGAL_MOVES_LEFT :
                                                         Nardi::status_codes::SUCCESS;
        }

        if(status != Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
            GetHumanInput();
    }

    void restart_or_quit()
    {
        if(!_builder.GetView())
            return;

        if(!_builder.GetCtrl().QuitRequested() && !_builder.GetCtrl().RestartRequested())
            GetHumanInput();
            
        if (_builder.GetCtrl().RestartRequested())
            reset();
    }

    bool is_terminal() const {
        return _builder.GetGame().GameIsOver();
    }

    int winner_result() const {
        if (!_builder.GetGame().GameIsOver())
            throw std::runtime_error("winner_result(): game not over"); // consider sentinel value or something
        return _builder.GetGame().IsMars() ? 2 : 1;
    }

    std::vector<Nardi::Board::Features> enumerate(Nardi::status_codes status)
    {
        if (status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT) {
            return {};
        } else if (status != Nardi::status_codes::SUCCESS) {
            Nardi::DispErrorCode(status);
            throw std::runtime_error("RollDice: unexpected controller status (should never happen).");
        } else if (_builder.GetCtrl().AwaitingRoll()) {
            throw std::runtime_error("Incorrect usage, have not yet rolled dice.");
        }

        // Success: get map<BoardConfig, MoveSequence, ...>
        const auto& b2s = _builder.GetGame().GetBoards2Seqs();

        std::vector<Nardi::Board::Features> features_vec;
        features_vec.reserve(b2s.size());
        for (const auto& kv : b2s)
            features_vec.push_back(_builder.GetGame().GetBoardRef().ExtractFeatures(kv.first));

        return features_vec;
    }

    void reset() {
        _builder.Reset();
        _last_children.clear();
        _last_lookahead_batch.reset();
        // Nardi::BoardConfig brd = {{ {6,-1,-1,-1,-1,-1,-1,-1,-2, 0, 0, 0},
        //                             {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,-6, 9}}};
        // _builder.ResetPreRoll(false, brd);
    }

    void status_report()
    {
        _builder.StatusReport();
    }

    std::string status_str()
    {
        return _builder.StatusString();
    }

    bool should_continue_game() const
    {
        return !(_builder.GetCtrl().QuitRequested() || _builder.GetGame().GameIsOver());
    }

    bool quit_requested() const
    {
        return _builder.GetCtrl().QuitRequested();
    }

private:
    Nardi::ScenarioBuilder _builder;
    ScenarioConfig         _config;
    std::vector<Nardi::Board::Features> _last_children;
    std::shared_ptr<LookaheadBatch> _last_lookahead_batch;
    std::mt19937 _rng{std::random_device{}()};

    const std::vector<Nardi::Board::Features>& require_children() const
    {
        if(_last_children.empty())
            throw std::runtime_error("No legal children cached. Roll/set dice and enumerate first.");
        return _last_children;
    }

    std::shared_ptr<LookaheadBatch> require_lookahead_batch() const
    {
        if(!_last_lookahead_batch)
            throw std::runtime_error("No lookahead batch available. Call make_lookahead_batch() first.");
        if(_last_lookahead_batch->children.empty())
            throw std::runtime_error("Cannot apply lookahead move from an empty batch.");
        return _last_lookahead_batch;
    }

    int flat_dice_idx(int d1, int d2) const
    {
        if(d1 > d2)
            std::swap(d1, d2);
        
        std::array<int, 2> dice = {d1, d2};
        
        int row_from_top = (7-dice[0]);
        int row_offset = (row_from_top*(row_from_top+1)/2);
        int d_idx = N_DICE_COMB - row_offset + (dice[1] - dice[0]);

        return d_idx;
    }

    std::vector<Nardi::Board::Features> set_and_enumerate(int d1, int d2, Nardi::ScenarioBuilder& b) 
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
        for (const auto& kv : b2s)
            features_vec.push_back(b.GetGame().GetBoardRef().ExtractFeatures(kv.first));

        return features_vec;
    }

    void GetHumanInput()
    {
        if(!_builder.GetView())
            return;

        while(true)
        {
            Nardi::status_codes status = _builder.GetView()->PollInput();
            
            if (status != Nardi::status_codes::WAITING){
                _builder.GetView()->DispErrorCode(status);
            }

            if (status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
                break;
        }
    }
};

py::array_t<uint8_t> occ_view(const Nardi::Board::Features::PlayerBoardInfo& pi)
{
    return py::array_t<uint8_t>(
        {3, Nardi::ROWS*Nardi::COLS},               // shape

        // {row stride size in bytes, col stride size in bytes}
        {sizeof(uint8_t) * Nardi::ROWS*Nardi::COLS, sizeof(uint8_t)}, 

        pi.occ.data()->data(),                      // ptr to pi.occ[0][0], here same as &pi.occ[0][0]
        py::cast(&pi)                               // memory ownership with cpp object
    );
}

py::array_t<int8_t> raw_data_view(const Nardi::Board::Features& f)
{
    return py::array_t<int8_t>(
        {Nardi::ROWS, Nardi::COLS},
        {sizeof(uint8_t) * Nardi::COLS, sizeof(uint8_t)},
        f.raw_data.data()->data(),
        py::cast(&f)
    );
}


// ---- pybind11 module ----
PYBIND11_MODULE(nardi, m) 
{
    m.doc() = "Py bindings for Nardi C++ engine (Python owns the loop) and scenario config object for easier manipulation";

    m.def("features_to_tensor",     &features_to_tensor,
          py::arg("features"),
          py::arg("kind") = "conv",
          py::arg("flatten") = false,
          R"(Convert one Features object to a model-ready float tensor buffer.)");
    m.def("feature_batch_to_tensor", &feature_batch_to_tensor,
          py::arg("features"),
          py::arg("kind") = "conv",
          py::arg("flatten") = false,
          R"(Convert a list of Features objects to a model-ready float tensor buffer.)");

    py::class_<NardiEngine>(m, "Engine")
        .def(py::init<>())
        .def("config",              &NardiEngine::GetConfig, 
             py::return_value_policy::reference)
        .def("AttachNewTRW",        &NardiEngine::AttachNewTRW,
             R"(Attaches TerminalRW for terminal view of game state.)")
        .def("AttachNewSFMLRW",     &NardiEngine::AttachNewSFMLRW,
             R"(Attaches SFMLRW for graphical view of game state.)")
        .def("DetachRW",            &NardiEngine::DetachRW,
             R"(Detaches ReaderWriter removing any view of game.)")
        .def("Render",              &NardiEngine::Render,
                R"(If view attached, then display)")
        .def("human_turn",          &NardiEngine::human_turn, py::arg("dice_rolled") = false,
             R"(Prompt human user for move and return false if their turn is over, true otherwise)")
        .def("board_features",      &NardiEngine::board_features,
             R"(Return 6x25 uint8 board (player-perspective).)")
        .def("apply_board",         &NardiEngine::apply_board, py::arg("board"),
             R"(Apply the sequence that reaches the provided board state [2,12] uint8.)")
        .def("current_player",      &NardiEngine::current_player,
             R"(Return the C++ current player index.)")
        .def("sign",                &NardiEngine::sign,
             R"(Return +1 for white-to-move and -1 for black-to-move.)")
        .def("turn_num",            &NardiEngine::turn_num,
             R"(Return total completed turns across both players.)")
        .def("player_turn_num",     &NardiEngine::player_turn_num,
             py::arg("player"),
             R"(Return completed turns for one player.)")
        .def("dice",                &NardiEngine::dice,
             R"(Return 1x2 uint8 dice values.)")    
        .def("dice_as_idx",         &NardiEngine::dice_as_idx,
             R"(Get dice pair as a flattened idx corresponding to DICE_COMBOS)")
        .def("roll_has_children",   &NardiEngine::roll_has_children,
             R"(Roll dice. Return false if no legal moves left)")
        .def("roll_and_enumerate",  &NardiEngine::roll_and_enumerate,
             R"(Roll dice and return uint8 array of shape [N,6,25] with end-of-turn boards.)")
        .def("set_and_enumerate",  
             py::overload_cast<int, int>(&NardiEngine::set_and_enumerate),
             py::arg("d1"), py::arg("d2"),
             R"(Set dice and enumerate all legal end-of-turn positions.)")
        .def("get_children",        &NardiEngine::enumerate,
             R"(List all children given the current dice roll. Error if dice not yet rolled.)")
        // .def("one_ply_lookahead",   &NardiEngine::OnePlyLookahead,
        //      R"(Look 1 ply ahead and return the root of the search tree.)")
        .def("make_lookahead_batch",
             [](NardiEngine& eng)
             {
                 py::gil_scoped_release release;
                 return eng.MakeLookaheadBatch();
             },
             R"(Build a batched one-ply lookahead frontier for model evaluation.)")
        .def("apply_best_lookahead",&NardiEngine::apply_best_lookahead,
             py::arg("values"),
             R"(Apply the best child from the last lookahead batch.)")
        .def("apply_noisy_board",    &NardiEngine::apply_noisy_board,
             py::arg("values"),
             py::arg("eps"),
             py::arg("temperature"),
             R"(Sample and apply a cached child board using softmax(values / temperature) plus Dirichlet noise.)")
        .def("apply_greedy_board",   &NardiEngine::apply_greedy_board,
             py::arg("values"),
             R"(Apply the highest-valued cached child board.)")
        .def("apply_random_board",   &NardiEngine::apply_random_board,
             R"(Apply a random cached child board.)")
        .def("apply_heuristic_board",&NardiEngine::apply_heuristic_board,
             R"(Apply the cached child board with highest current-player square occupancy.)")
        .def("status_report",       &NardiEngine::status_report)
        .def("status_str",          &NardiEngine::status_str)
        .def("is_terminal",         &NardiEngine::is_terminal)
        .def("winner_result",       &NardiEngine::winner_result)
        .def("reset",               &NardiEngine::reset)
        .def("should_continue_game",&NardiEngine::should_continue_game)
        .def("restart_or_quit",   &NardiEngine::restart_or_quit)
        .def("quit_requested",      &NardiEngine::quit_requested);

    py::class_<ScenarioConfig>(m, "ScenarioConfig")
        .def("withScenario",
            (Nardi::status_codes (ScenarioConfig::*)(
                bool,
                py::array_t<int8_t, py::array::c_style | py::array::forcecast>,
                int, int, int, int
            ))                      &ScenarioConfig::withScenario,
            py::arg("p_idx"),
            py::arg("board"),
            py::arg("d1"),
            py::arg("d2"),
            py::arg("d1u") = 0,
            py::arg("d2u") = 0)
        .def("withDice",            &ScenarioConfig::withDice,
            py::arg("d1"),
            py::arg("d2"),
            py::arg("d1_used") = 0,
            py::arg("d2_used") = 0)
        .def("withRandomEndgame",   &ScenarioConfig::withRandomEndgame,
            py::arg("p_idx") = false);

    py::enum_<Nardi::status_codes>(m, "status_codes")
        .value("SUCCESS",               Nardi::status_codes::SUCCESS)
        .value("NO_LEGAL_MOVES_LEFT",   Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
        .value("OUT_OF_BOUNDS",         Nardi::status_codes::OUT_OF_BOUNDS)
        .value("START_EMPTY_OR_ENEMY",  Nardi::status_codes::START_EMPTY_OR_ENEMY)
        .value("DEST_ENEMY",            Nardi::status_codes::DEST_ENEMY)
        .value("BACKWARDS_MOVE",        Nardi::status_codes::BACKWARDS_MOVE)
        .value("NO_PATH",               Nardi::status_codes::NO_PATH)
        .value("PREVENTS_COMPLETION",   Nardi::status_codes::PREVENTS_COMPLETION)
        .value("BAD_BLOCK",             Nardi::status_codes::BAD_BLOCK)
        .value("DICE_USED_ALREADY",     Nardi::status_codes::DICE_USED_ALREADY)
        .value("HEAD_PLAYED_ALREADY",   Nardi::status_codes::HEAD_PLAYED_ALREADY)
        .value("MISC_FAILURE",          Nardi::status_codes::MISC_FAILURE)
        .export_values();

    py::class_<Nardi::Board::Features>(m, "Features")
        .def_readonly("player",             &Nardi::Board::Features::player)
        .def_readonly("opp",                &Nardi::Board::Features::opp)
        .def_property_readonly("raw_data",  &raw_data_view);

    py::class_<Nardi::Board::Features::PlayerBoardInfo>(m, "PlayerBoardInfo")
        .def_readonly("pip_count",          &Nardi::Board::Features::PlayerBoardInfo::pip_count)
        .def_readonly("pieces_off",         &Nardi::Board::Features::PlayerBoardInfo::pieces_off)
        .def_readonly("pieces_not_reached", &Nardi::Board::Features::PlayerBoardInfo::pieces_not_reached)
        .def_readonly("sq_occ",             &Nardi::Board::Features::PlayerBoardInfo::sq_occ)
        .def_property_readonly("occ",       &occ_view);

    py::class_<NardiEngine::ChanceAndChildren>(m, "ChanceAndChildren")
        .def_readonly("prob",               &NardiEngine::ChanceAndChildren::prob)
        .def_readonly("data",               &NardiEngine::ChanceAndChildren::data);

    py::class_<NardiEngine::Node, std::shared_ptr<NardiEngine::Node>>(m, "Node")
        .def("is_leaf",                     &NardiEngine::Node::isLeaf)
        .def_readonly("result",             &NardiEngine::Node::result)
        .def_readonly("features",           &NardiEngine::Node::features)
        .def_readonly("children_by_dice",   &NardiEngine::Node::children_by_dice);

    py::class_<LookaheadBatch, std::shared_ptr<LookaheadBatch>>(m, "LookaheadBatch")
        .def_property_readonly("num_children",       &LookaheadBatch::num_children)
        .def_property_readonly("num_eval_features",  &LookaheadBatch::num_eval_features)
        .def("tensor",                               &LookaheadBatch::tensor,
            py::arg("kind") = "conv",
            py::arg("flatten") = false,
            R"(Return model-ready eval features as [N,6,25] or [N,150].)")
        .def("child_values",                         &LookaheadBatch::child_values,
            py::arg("values"),
            R"(Aggregate leaf values into one value per legal child move.)")
        .def("best_index",                           &LookaheadBatch::best_index,
            py::arg("values"),
            R"(Return the argmax child move index after value aggregation.)")
        .def("best_board",                           &LookaheadBatch::best_board,
            py::arg("values"),
            R"(Return the raw board for the argmax child move.)");
}
