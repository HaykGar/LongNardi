#pragma once

#include <pybind11/numpy.h>

#include "../CoreEngine/ScenarioBuilder.h"

namespace nardi_py
{

namespace py = pybind11;

/*
ScenarioConfig exposes the scenario-building subset of ScenarioBuilder without
exposing reader/writer ownership or other controller internals to Python.
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

    Nardi::status_codes withScenario(
        bool p_idx,
        py::array_t<int8_t, py::array::c_style | py::array::forcecast> board,
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
