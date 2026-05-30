#include "python_views.h"

namespace nardi_py
{

py::array_t<uint8_t> occ_view(const Nardi::Board::Features::PlayerBoardInfo& pi)
{
    return py::array_t<uint8_t>(
        {3, Nardi::ROWS * Nardi::COLS},
        {sizeof(uint8_t) * Nardi::ROWS * Nardi::COLS, sizeof(uint8_t)},
        pi.occ.data()->data(),
        py::cast(&pi)
    );
}

py::array_t<int8_t> raw_data_view(const Nardi::Board::Features& f)
{
    return py::array_t<int8_t>(
        {Nardi::ROWS, Nardi::COLS},
        {sizeof(int8_t) * Nardi::COLS, sizeof(int8_t)},
        f.raw_data.data()->data(),
        py::cast(&f)
    );
}

} // namespace nardi_py
