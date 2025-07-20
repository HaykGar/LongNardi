#include "Auxilaries.h"

// View treats board as it sees it, but board reverses row 1. Command makes sure this is consistent upon delivery to controller

Command::Command(Actions a, NardiCoord coord) : action(a)
{
    VisualToGameCoord(coord);
    payload.emplace<NardiCoord>( coord );
}

Command::Command(Actions a, int r, int c) : action(a)
{
    Command(a, {r, c});
}

Command::Command(Actions a, bool dice_idx) : action(a), payload(dice_idx) {}


void VisualToGameCoord(NardiCoord& coord)
{
    if (coord.row == 0)
        coord.col = COL - coord.col - 1;
}

int BoolToSign(bool p_idx)
{   return p_idx ? -1 : 1;   }