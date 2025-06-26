#include "Auxilaries.h"

// View treats board as it sees it, but board reverses row 1. Command makes sure this is consistent upon delivery to controller

Command::Command(Actions a, int r, int c) : action(a), to_select(NardiCoord(r, c))
{
    if (r == 0)
    {
        to_select.value().col = COL - c - 1;
    }
}


Command::Command(Actions a, NardiCoord coord) : action(a), to_select(coord)
{
    if (coord.row == 0)
    {
        to_select.value().col = COL - coord.col - 1;
    }
}