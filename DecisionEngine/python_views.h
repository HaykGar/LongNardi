#pragma once

#include <pybind11/numpy.h>

#include "../CoreEngine/Board.h"

namespace nardi_py
{

namespace py = pybind11;

py::array_t<uint8_t> occ_view(const Nardi::Board::Features::PlayerBoardInfo& pi);
py::array_t<int8_t> raw_data_view(const Nardi::Board::Features& f);

} // namespace nardi_py
