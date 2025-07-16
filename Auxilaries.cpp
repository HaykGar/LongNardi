#include "Auxilaries.h"

// View treats board as it sees it, but board reverses row 1. Command makes sure this is consistent upon delivery to controller

Command::Command(Actions a, int r, int c) : action(a), payload(NardiCoord(r, c))
{
    if (r == 0)
    {
        std::get<NardiCoord>(payload).col = COL - c - 1;
    }
}


Command::Command(Actions a, NardiCoord coord) : action(a), payload(coord)
{
    if (coord.row == 0)
    {
        std::get<NardiCoord>(payload).col = COL - coord.col - 1;
    }
}

Command::Command(Actions a, bool dice_idx) : action(a), payload(dice_idx) {}