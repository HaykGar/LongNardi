#include "scenario_config.h"

#include <stdexcept>

namespace nardi_py
{

ScenarioConfig::ScenarioConfig(Nardi::ScenarioBuilder& sb)
: _builder(sb)
{}

Nardi::status_codes ScenarioConfig::withScenario(
    bool p_idx,
    const Nardi::BoardConfig& b,
    int d1,
    int d2,
    int d1u,
    int d2u)
{
    return _builder.withScenario(p_idx, b, d1, d2, d1u, d2u);
}

Nardi::status_codes ScenarioConfig::withScenario(
    bool p_idx,
    py::array_t<int8_t, py::array::c_style | py::array::forcecast> board,
    int d1,
    int d2,
    int d1u,
    int d2u)
{
    if(board.ndim() != 2 || board.shape(0) != Nardi::ROWS || board.shape(1) != Nardi::COLS)
        throw std::runtime_error("BoardConfig must be shape (2, 12)");

    Nardi::BoardConfig cfg;
    auto buf = board.unchecked<2>();

    for(size_t r = 0; r < Nardi::ROWS; ++r)
        for(size_t c = 0; c < Nardi::COLS; ++c)
            cfg[r][c] = buf(r, c);

    return withScenario(p_idx, cfg, d1, d2, d1u, d2u);
}

Nardi::status_codes ScenarioConfig::withDice(int d1, int d2, int d1_used, int d2_used)
{
    return _builder.withDice(d1, d2, d1_used, d2_used);
}

void ScenarioConfig::withRandomEndgame(bool p_idx)
{
    _builder.withRandomEndgame(p_idx);
}

void ScenarioConfig::withFirstTurn()
{
    _builder.withFirstTurn();
}

} // namespace nardi_py
