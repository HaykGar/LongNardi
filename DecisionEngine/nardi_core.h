#pragma once

// Pybind-free core constants and helpers shared by the engine and the binding
// layer. Keeping these out of binding_utils.h (which pulls in <pybind11/numpy.h>)
// lets the engine core + C API compile without Python -- a prerequisite for the
// iOS build.

#include <optional>
#include <random>
#include <vector>

#include "../CoreEngine/Board.h"

namespace nardi_py
{

inline constexpr int N_DICE_COMB = 21;

inline constexpr int DICE_COMBOS[N_DICE_COMB][2] = {
    {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 6},
            {2, 2}, {2, 3}, {2, 4}, {2, 5}, {2, 6},
                    {3, 3}, {3, 4}, {3, 5}, {3, 6},
                            {4, 4}, {4, 5}, {4, 6},
                                    {5, 5}, {5, 6},
                                            {6, 6}
};

inline constexpr float COMBO_PROBS[N_DICE_COMB] = {
    1.0f / 36.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f,
                   1.0f / 36.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f,
                                  1.0f / 36.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f,
                                                 1.0f / 36.0f, 1.0f / 18.0f, 1.0f / 18.0f,
                                                                1.0f / 36.0f, 1.0f / 18.0f,
                                                                               1.0f / 36.0f
};

// Terminal check from the side-to-move's perspective: returns the win margin
// (1 normal, 2 mars) if the side-to-move has borne off all pieces, else nullopt.
std::optional<float> terminal_value_for_side_to_move(const Nardi::Board::Features& f);

// Softmax(values / temperature) mixed with Dirichlet noise, sampled with `rng`.
int sample_noisy_index(
    const std::vector<float>& values,
    float eps,
    float temperature,
    std::mt19937& rng);

} // namespace nardi_py
