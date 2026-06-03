#pragma once

#include "../CoreEngine/ScenarioBuilder.h"

namespace nardi_py
{

/*
ScenarioConfig exposes the scenario-building subset of ScenarioBuilder without
exposing reader/writer ownership or other controller internals. Pybind-free; the
numpy (py::array board) overload of withScenario is provided in bindings.cpp.
*/
class ScenarioConfig
{
public:
    explicit ScenarioConfig(Nardi::ScenarioBuilder& sb);

    Nardi::status_codes withScenario(
        bool p_idx,
        const Nardi::BoardConfig& b,
        int d1,
        int d2,
        int d1u = 0,
        int d2u = 0);

    Nardi::status_codes withDice(int d1, int d2, int d1_used, int d2_used);
    void withRandomEndgame(bool p_idx);
    void withFirstTurn();

private:
    Nardi::ScenarioBuilder& _builder;
};

} // namespace nardi_py
