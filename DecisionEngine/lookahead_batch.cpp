#include "lookahead_batch.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace nardi_py
{

int LookaheadBatch::num_children() const
{
    return static_cast<int>(children.size());
}

int LookaheadBatch::num_eval_features() const
{
    return static_cast<int>(eval_features.size());
}

py::array_t<float> LookaheadBatch::tensor(const std::string& kind, bool flatten) const
{
    return feature_batch_to_tensor(eval_features, kind, flatten);
}

py::array_t<float> LookaheadBatch::child_values(
    py::array_t<float, py::array::c_style | py::array::forcecast> values) const
{
    std::vector<float> parsed_values = parse_values(values);
    py::array_t<float> arr(py::ssize_t(children.size()));
    auto out = arr.mutable_unchecked<1>();

    for(size_t i = 0; i < children.size(); ++i)
        out(static_cast<py::ssize_t>(i)) = child_value(children[i], parsed_values);

    return arr;
}

int LookaheadBatch::best_index(
    py::array_t<float, py::array::c_style | py::array::forcecast> values) const
{
    return best_index_values(parse_values(values));
}

int LookaheadBatch::best_index_values(const std::vector<float>& values) const
{
    if(children.empty())
        throw std::runtime_error("Cannot select from an empty lookahead batch.");

    if(values.size() != eval_features.size())
        throw std::runtime_error("Lookahead values length does not match eval feature count.");

    // A true winning child is always optimal. Do not compare it against model
    // estimates, because overestimated non-terminal states must not hide a win.
    for(size_t i = 0; i < children.size(); ++i)
        if(children[i].terminal_value.has_value())
            return static_cast<int>(i);

    int best = 0;
    float best_value = child_value(children[0], values);

    for(size_t i = 1; i < children.size(); ++i)
    {
        const float candidate = child_value(children[i], values);
        if(candidate > best_value)
        {
            best = static_cast<int>(i);
            best_value = candidate;
        }
    }

    return best;
}

py::array_t<int8_t> LookaheadBatch::best_board(
    py::array_t<float, py::array::c_style | py::array::forcecast> values) const
{
    return board_to_array(children.at(static_cast<size_t>(best_index(values))).board);
}

std::vector<float> LookaheadBatch::parse_values(
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

float LookaheadBatch::child_value(const ChildChoice& child, const std::vector<float>& values)
{
    if(child.terminal_value.has_value())
        return child.terminal_value.value();

    float avg_child_eval = 0.0f;
    for(int d_idx = 0; d_idx < N_DICE_COMB; ++d_idx)
    {
        const DiceGroup& group = child.dice_groups[d_idx];
        float dice_value;

        if(std::holds_alternative<float>(group.data))
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
                dice_value = std::min(dice_value, values.at(static_cast<size_t>(eval_idx)));
        }

        avg_child_eval += dice_value * COMBO_PROBS[d_idx];
    }

    return avg_child_eval;
}

} // namespace nardi_py
