#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../CoreEngine/Board.h"

namespace nardi_py
{

// A C++-owned, stale copy of the PyTorch value network used to drive MCTS
// rollouts and node priors. It wraps a TorchScript module exported from Python
// via nardi_net.export_target_network (which traces `value_from_tensor`).
//
// The module takes a conv-pipeline feature tensor [N, 6, 25] and returns the
// side-to-move value for each row. The torch headers are confined to the .cpp
// (PIMPL) so this header stays lightweight for the rest of the build.
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
