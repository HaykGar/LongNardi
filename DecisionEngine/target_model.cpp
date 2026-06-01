#include "target_model.h"

#include <stdexcept>

#include "nardi_infer.h"

namespace nardi_py
{

struct TargetModel::Impl
{
    std::unique_ptr<InferenceNet> net;
};

TargetModel::TargetModel() : _impl(std::make_unique<Impl>()) {}
TargetModel::~TargetModel() = default;

void TargetModel::load(const std::string& path)
{
    // Hand-rolled, dependency-free inference (see nardi_infer.{h,cpp}). Replaces
    // the previous TorchScript / LibTorch path so the C++ engine no longer links
    // against PyTorch. The blob is produced by nardi_net.export_weights.
    _impl->net = load_inference_net(path);
}

bool TargetModel::is_loaded() const { return _impl->net != nullptr; }

float TargetModel::evaluate(const Nardi::Board::Features& f) const
{
    if(!_impl->net)
        throw std::runtime_error("TargetModel: no network loaded (call load() first).");
    return _impl->net->evaluate(f);
}

std::vector<float> TargetModel::evaluate_batch(const std::vector<Nardi::Board::Features>& features) const
{
    if(!_impl->net)
        throw std::runtime_error("TargetModel: no network loaded (call load() first).");
    return _impl->net->evaluate_batch(features);
}

} // namespace nardi_py
