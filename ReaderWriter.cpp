#include "ReaderWriter.h"

void ReaderWriter::DispErrorCode(Game::status_codes c) const
{
    switch (c)
    {
    case Game::status_codes::SUCCESS:
        ErrorMessage("Success");
        break;
    case Game::status_codes::NO_LEGAL_MOVES_LEFT:
        ErrorMessage("NO_LEGAL_MOVES_LEFT");
        break;
        break;
    case Game::status_codes::OUT_OF_BOUNDS:
        ErrorMessage("OUT_OF_BOUNDS");
        break;
    case Game::status_codes::START_EMPTY_OR_ENEMY:
        ErrorMessage("START_EMPTY_OR_ENEMY");
        break;
    case Game::status_codes::DEST_ENEMY:
        ErrorMessage("DEST_ENEMY");
        break;
    case Game::status_codes::BACKWARDS_MOVE:
        ErrorMessage("BACKWARDS_MOVE");
        break;
    case Game::status_codes::BOARD_END_REACHED:
        ErrorMessage("BOARD_END_REACHED");
        break;
    case Game::status_codes::NO_PATH:
        ErrorMessage("NO_PATH");
        break;
    case Game::status_codes::START_RESELECT:
        ErrorMessage("START_RESELECT");
        break;
    case Game::status_codes::DICE_USED_ALREADY:
        ErrorMessage("DICE_USED_ALREADY");
        break;
    case Game::status_codes::HEAD_PLAYED_ALREADY:
        ErrorMessage("HEAD_PLAYED_ALREADY");
        break;
    case Game::status_codes::MISC_FAILURE:
        ErrorMessage("MISC_FAILURE");
        break;
    default:
        break;
    }
}