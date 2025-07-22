#include "Auxilaries.h"

// View treats board as it sees it, but board reverses row 1. Command makes sure this is consistent upon delivery to controller

Command::Command(Actions a, NardiCoord coord) : action(a)
{
    payload.emplace<NardiCoord>( coord );
}

Command::Command(Actions a, int r, int c) : action(a)
{
    payload.emplace<NardiCoord>(r, c);
}

Command::Command(Actions a, bool dice_idx) : action(a), payload(dice_idx) {}


void VisualToGameCoord(NardiCoord& coord)
{
    if (coord.row == 0)
        coord.col = COL - coord.col - 1;
}

int BoolToSign(bool p_idx)
{   return p_idx ? -1 : 1;   }


void DispErrorCode(status_codes c)
{
    switch (c)
    {
    case status_codes::SUCCESS:
        std::cout << "Success";
        break;
    case status_codes::NO_LEGAL_MOVES_LEFT:
        std::cout << "NO_LEGAL_MOVES_LEFT";
        break;
    case status_codes::OUT_OF_BOUNDS:
        std::cout << "OUT_OF_BOUNDS";
        break;
    case status_codes::START_EMPTY_OR_ENEMY:
        std::cout << "START_EMPTY_OR_ENEMY";
        break;
    case status_codes::DEST_ENEMY:
        std::cout << "DEST_ENEMY";
        break;
    case status_codes::BACKWARDS_MOVE:
        std::cout << "BACKWARDS_MOVE";
        break;
    case status_codes::BOARD_END_REACHED:
        std::cout << "BOARD_END_REACHED";
        break;
    case status_codes::NO_PATH:
        std::cout << "NO_PATH";
        break;
    case status_codes::DICE_USED_ALREADY:
        std::cout << "DICE_USED_ALREADY";
        break;
    case status_codes::HEAD_PLAYED_ALREADY:
        std::cout << "HEAD_PLAYED_ALREADY";
        break;
    case status_codes::MISC_FAILURE:
        std::cout << "MISC_FAILURE";
        break;
    default:
        break;
    }

    std::cout << "\n\n";
}