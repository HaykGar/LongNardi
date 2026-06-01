#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../CoreEngine/Board.h"

namespace nardi_py
{

// A C++-owned, stale copy of the value network used to drive MCTS rollouts and
// node priors. It wraps a hand-rolled, dependency-free InferenceNet (see
// nardi_infer.{h,cpp}) loaded from a weight blob exported by
// nardi_net.export_weights / export_target_network.
//
// evaluate() takes a position's Features and returns the side-to-move value,
// matching model(features) in Python. The concrete net type is hidden behind
// the PIMPL so this header stays lightweight and torch-free.
class TargetModel
{
public:
    TargetModel();
    ~TargetModel();

    TargetModel(const TargetModel&) = delete;
    TargetModel& operator=(const TargetModel&) = delete;

    // Load a TorchScript .pt file (replaces any previously loaded network).
    void load(const std::string& path);
    bool is_loaded() const;

    // Evaluate one position's features -> side-to-move value.
    float evaluate(const Nardi::Board::Features& f);

    // Batched evaluation; the workhorse for rollouts and node-prior expansion.
    std::vector<float> evaluate_batch(const std::vector<Nardi::Board::Features>& features);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace nardi_py
