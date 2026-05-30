#pragma once

#include <pybind11/numpy.h>

#include <array>
#include <optional>
#include <variant>
#include <vector>

#include "binding_utils.h"
#include "../CoreEngine/Board.h"

namespace nardi_py
{

namespace py = pybind11;

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

    py::array_t<float> tensor(const std::string& kind = "conv", bool flatten = false) const;

    py::array_t<float> child_values(
        py::array_t<float, py::array::c_style | py::array::forcecast> values) const;

    int best_index(py::array_t<float, py::array::c_style | py::array::forcecast> values) const;

    py::array_t<int8_t> best_board(
        py::array_t<float, py::array::c_style | py::array::forcecast> values) const;

private:
    std::vector<float> parse_values(
        py::array_t<float, py::array::c_style | py::array::forcecast> values) const;

    static float child_value(const ChildChoice& child, const std::vector<float>& values);
};

} // namespace nardi_py
