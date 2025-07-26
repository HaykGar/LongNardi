#include "ReaderWriter.h"

void ReaderWriter::DispErrorCode(status_codes c) const
{
    switch (c)
    {
    case status_codes::SUCCESS:
        ErrorMessage("Success");
        break;
    case status_codes::NO_LEGAL_MOVES_LEFT:
        ErrorMessage("NO_LEGAL_MOVES_LEFT");
        break;
        break;
    case status_codes::OUT_OF_BOUNDS:
        ErrorMessage("OUT_OF_BOUNDS");
        break;
    case status_codes::START_EMPTY_OR_ENEMY:
        ErrorMessage("START_EMPTY_OR_ENEMY");
        break;
    case status_codes::DEST_ENEMY:
        ErrorMessage("DEST_ENEMY");
        break;
    case status_codes::BACKWARDS_MOVE:
        ErrorMessage("BACKWARDS_MOVE");
        break;
    case status_codes::NO_PATH:
        ErrorMessage("NO_PATH");
        break;
    case status_codes::DICE_USED_ALREADY:
        ErrorMessage("DICE_USED_ALREADY");
        break;
    case status_codes::HEAD_PLAYED_ALREADY:
        ErrorMessage("HEAD_PLAYED_ALREADY");
        break;
    case status_codes::MISC_FAILURE:
        ErrorMessage("MISC_FAILURE");
        break;
    case status_codes::BAD_BLOCK:
        ErrorMessage("BAD_BLOCK");
        break;        
    default:
        ErrorMessage("did not recognize code");
        break;
    }
}