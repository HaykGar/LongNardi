#pragma once

#include <array>
#include <optional>
#include <variant>
#include <vector>

#include "nardi_core.h"
#include "../CoreEngine/Board.h"

namespace nardi_py
{

// Pybind-free one-ply lookahead frontier. The numpy/pybind surface
// (tensor / child_values / best_index / best_board taking py::array) is provided
// as free functions in bindings.cpp; the engine and C API use only the
// std::vector-based methods here.
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

    int num_children() const;
    int num_eval_features() const;

    // Aggregate per-eval-feature side-to-move `values` into one value per child.
    std::vector<float> child_values_vec(const std::vector<float>& values) const;

    // Index of the best child given eval-feature `values`, honoring terminal-win
    // shortcuts.
    int best_index_values(const std::vector<float>& values) const;

private:
    static float child_value(const ChildChoice& child, const std::vector<float>& values);
};

} // namespace nardi_py
