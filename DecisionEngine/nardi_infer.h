#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../CoreEngine/Board.h"

namespace nardi_py
{

// Hand-rolled, dependency-free CPU inference for the small Nardi value nets.
//
// This mirrors the PyTorch model definitions in nardi_net.py (NardiNet /
// ConvNardiNet / ResNardiNet) so the exact same value network can run inside
// the Python research module AND on platforms where shipping PyTorch is
// impractical (e.g. iOS). Weights are loaded from a flat ".nardiw" blob written
// by nardi_net.export_weights; there is no LibTorch / torch::jit dependency.
//
// evaluate() / evaluate_batch() return the side-to-move value, identical to
// calling model(features) in Python (i.e. model.value_from_tensor of the conv
// or legacy feature tensor).
class InferenceNet
{
public:
    virtual ~InferenceNet() = default;

    // Side-to-move value for one position.
    virtual float evaluate(const Nardi::Board::Features& f) const = 0;

    // Batched evaluation (the workhorse for MCTS rollouts / prior expansion).
    virtual std::vector<float> evaluate_batch(
        const std::vector<Nardi::Board::Features>& features) const = 0;
};

// Load a weight blob and construct the matching network. Throws std::runtime_error
// on a malformed file or unsupported architecture tag.
std::unique_ptr<InferenceNet> load_inference_net(const std::string& path);

} // namespace nardi_py
