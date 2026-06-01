#include "target_model.h"

#include <ATen/Parallel.h>
#include <torch/script.h>

#include <stdexcept>

#include "../CoreEngine/Auxilaries.h"

namespace nardi_py
{

namespace
{

constexpr int FEAT_ROWS = 6;
constexpr int FEAT_COLS = Nardi::ROWS * Nardi::COLS + 1; // 25
constexpr float FEAT_SCALE = 15.0f;

// Fill a [6, 25] row-major block for one Features object, matching the conv
// pipeline layout produced by binding_utils::feature_batch_to_tensor (kind=conv):
//   rows 0-2 : player occupancy planes
//   rows 3-5 : opponent occupancy planes
//   last col : per-row scalars (pieces_off, pip_count, pieces_not_reached)
// Everything divided by 15.
void fill_features(const Nardi::Board::Features& f, float* out)
{
    for(int row = 0; row < 3; ++row)
        for(int col = 0; col < Nardi::ROWS * Nardi::COLS; ++col)
            out[row * FEAT_COLS + col] = static_cast<float>(f.player.occ[row][col]) / FEAT_SCALE;

    for(int row = 0; row < 3; ++row)
        for(int col = 0; col < Nardi::ROWS * Nardi::COLS; ++col)
            out[(row + 3) * FEAT_COLS + col] = static_cast<float>(f.opp.occ[row][col]) / FEAT_SCALE;

    const float scalars[FEAT_ROWS] = {
        static_cast<float>(f.player.pieces_off),
        static_cast<float>(f.opp.pieces_off),
        static_cast<float>(f.player.pip_count),
        static_cast<float>(f.opp.pip_count),
        static_cast<float>(f.player.pieces_not_reached),
        static_cast<float>(f.opp.pieces_not_reached)
    };
    for(int row = 0; row < FEAT_ROWS; ++row)
        out[row * FEAT_COLS + (FEAT_COLS - 1)] = scalars[row] / FEAT_SCALE;
}

} // namespace

struct TargetModel::Impl
{
    torch::jit::script::Module module;
    bool loaded = false;
};

TargetModel::TargetModel() : _impl(std::make_unique<Impl>()) {}
TargetModel::~TargetModel() = default;

void TargetModel::load(const std::string& path)
{
    // One worker process per environment (multiprocessing), so keep libtorch
    // single-threaded to avoid oversubscription across workers.
    at::set_num_threads(1);
    _impl->module = torch::jit::load(path);
    _impl->module.eval();
    _impl->loaded = true;
}

bool TargetModel::is_loaded() const { return _impl->loaded; }

float TargetModel::evaluate(const Nardi::Board::Features& f)
{
    return evaluate_batch({f}).front();
}

std::vector<float> TargetModel::evaluate_batch(const std::vector<Nardi::Board::Features>& features)
{
    if(!_impl->loaded)
        throw std::runtime_error("TargetModel: no network loaded (call load() first).");

    const int n = static_cast<int>(features.size());
    std::vector<float> result(static_cast<size_t>(n));
    if(n == 0)
        return result;

    torch::InferenceMode guard;

    auto input = torch::empty({n, FEAT_ROWS, FEAT_COLS}, torch::kFloat32);
    float* data = input.data_ptr<float>();
    for(int i = 0; i < n; ++i)
        fill_features(features[static_cast<size_t>(i)], data + i * FEAT_ROWS * FEAT_COLS);

    auto output = _impl->module.forward({input}).toTensor().contiguous().to(torch::kFloat32);
    const float* acc = output.data_ptr<float>();
    for(int i = 0; i < n; ++i)
        result[static_cast<size_t>(i)] = acc[i];

    return result;
}

} // namespace nardi_py
