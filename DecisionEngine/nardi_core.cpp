#include "nardi_core.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace nardi_py
{

int sample_noisy_index(
    const std::vector<float>& values,
    float eps,
    float temperature,
    std::mt19937& rng)
{
    if(values.empty())
        throw std::runtime_error("Cannot sample from empty values.");
    if(values.size() == 1)
        return 0;
    if(eps < 0.0f || eps > 1.0f)
        throw std::runtime_error("Noisy move eps must be in [0, 1].");
    if(temperature <= 0.0f)
        throw std::runtime_error("Noisy move temperature must be positive.");

    const float max_value = *std::max_element(values.begin(), values.end());
    std::vector<double> priors;
    priors.reserve(values.size());

    double total = 0.0;
    for(float value : values)
    {
        const double p = std::exp(static_cast<double>((value - max_value) / temperature));
        priors.push_back(p);
        total += p;
    }
    for(double& p : priors)
        p /= total;

    const double alpha = std::clamp(6.0 / static_cast<double>(values.size()), 0.2, 0.8);
    std::gamma_distribution<double> gamma(alpha, 1.0);
    std::vector<double> noise(priors.size());

    double noise_total = 0.0;
    for(double& w : noise)
    {
        w = gamma(rng);
        noise_total += w;
    }

    if(noise_total == 0.0)
    {
        const double uniform = 1.0 / static_cast<double>(noise.size());
        std::fill(noise.begin(), noise.end(), uniform);
    }
    else
    {
        for(double& w : noise)
            w /= noise_total;
    }

    std::vector<double> final_probs(priors.size());
    for(size_t i = 0; i < priors.size(); ++i)
        final_probs[i] = (1.0 - eps) * priors[i] + eps * noise[i];

    std::discrete_distribution<int> dist(final_probs.begin(), final_probs.end());
    return dist(rng);
}

std::optional<float> terminal_value_for_side_to_move(const Nardi::Board::Features& f)
{
    int player_pieces_on_board = 0;
    for(const auto& row : f.player.occ)
        for(uint8_t count : row)
            player_pieces_on_board += count;

    if(player_pieces_on_board > 0)
        return std::nullopt;

    return f.opp.pieces_off == 0 ? 2.0f : 1.0f;
}

} // namespace nardi_py
