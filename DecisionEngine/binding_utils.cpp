#include "binding_utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>

namespace nardi_py
{

FeaturePipelineKind parse_pipeline_kind(const std::string& kind)
{
    if(kind == "legacy")
        return FeaturePipelineKind::LEGACY;
    if(kind == "conv")
        return FeaturePipelineKind::CONV;
    throw std::runtime_error("Unknown feature pipeline kind. Expected 'legacy' or 'conv'.");
}

namespace
{

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

} // namespace

py::array_t<float> features_to_tensor(
    const Nardi::Board::Features& f,
    const std::string& kind,
    bool flatten)
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
    const std::string& kind,
    bool flatten)
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

py::array_t<int8_t> board_to_array(const Nardi::BoardConfig& board)
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

// sample_noisy_index and terminal_value_for_side_to_move moved to nardi_core.cpp
// (pybind-free).

} // namespace nardi_py
