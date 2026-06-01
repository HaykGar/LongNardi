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
