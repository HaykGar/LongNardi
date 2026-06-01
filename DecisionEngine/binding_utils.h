#pragma once

#include <pybind11/numpy.h>

#include <string>
#include <vector>

#include "nardi_core.h"
#include "../CoreEngine/Board.h"

namespace nardi_py
{

namespace py = pybind11;

// Pybind-free shared constants/helpers (N_DICE_COMB, DICE_COMBOS, COMBO_PROBS,
// terminal_value_for_side_to_move, sample_noisy_index) now live in nardi_core.h.
// This header keeps only the numpy/pybind conversion helpers.

inline constexpr int FEATURE_ROWS = 6;
inline constexpr int FEATURE_COLS = Nardi::ROWS * Nardi::COLS + 1;
inline constexpr float FEATURE_SCALE = 15.0f;

enum class FeaturePipelineKind
{
    LEGACY,
    CONV
};

FeaturePipelineKind parse_pipeline_kind(const std::string& kind);

py::array_t<float> features_to_tensor(
    const Nardi::Board::Features& f,
    const std::string& kind = "conv",
    bool flatten = false);

py::array_t<float> feature_batch_to_tensor(
    const std::vector<Nardi::Board::Features>& features,
    const std::string& kind = "conv",
    bool flatten = false);

py::array_t<int8_t> board_to_array(const Nardi::BoardConfig& board);

std::vector<float> parse_1d_values(
    py::array_t<float, py::array::c_style | py::array::forcecast> values,
    size_t expected_size);

} // namespace nardi_py
