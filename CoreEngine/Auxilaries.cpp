#include "Auxilaries.h"

namespace Nardi
{

// View treats board as it sees it, but board reverses row 1. Command makes sure this is consistent upon delivery to controller

///////////// Coord /////////////

bool Coord::operator==(const Coord& rhs) const
{   return (this->row == rhs.row && this->col == rhs.col);   }
 
bool Coord::OutOfBounds() const
{   return (row < 0 || row >= ROWS || col < 0 || col >= COLS);    }

bool Coord::InBounds() const
{   return !OutOfBounds();   }

void Coord::Print() const
{
    std::cout << "(" << row << ", " << col << ")\n";
}

std::string Coord::AsStr() const
{   return "(" + std::to_string(row) + ", " + std::to_string(col) + ")";   }


///////////// Command /////////////

Command::Command(Coord coord) : action(Actions::SELECT_SLOT)
{
    payload.emplace<Coord>( coord );
}

Command::Command(int r, int c) : action(Actions::SELECT_SLOT)
{
    payload.emplace<Coord>(r, c);
}

Command::Command(bool dice_idx) : action(Actions::MOVE_BY_DICE), payload(dice_idx) {}

Command::Command(BoardKey end_key) : action(Actions::AUTOPLAY), payload(end_key) {}

///////////// Misc Utilities /////////////


void VisualToGameCoord(Coord& coord)
{
    if (coord.row == 0)
        coord.col = COLS - coord.col - 1;
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
    case status_codes::BAD_BLOCK:
        std::cout << "BAD_BLOCK";
        break;        
    case status_codes::PREVENTS_COMPLETION:
        std::cout << "PREVENTS_COMPLETION";
        break;  
    default:
        std::cout << "did not recognize code" << static_cast<int>(c);
        break;
    }

    std::cout << "\n\n";
}

void DisplayBoard(const BoardConfig& brd)
{
    for(int i = 0; i < ROWS; ++i)
    {
        for (int j = 0; j < COLS; ++j)
        {
            std::cout << static_cast<int>(brd[i][j]) << "\t";
        }
        std::cout<< "\n\n";
    }
    std::cout<< "\n\n\n\n";
}

void DisplayKey(const BoardKey& bk)
{
    for(const auto& ctr : bk)
    {
        for (const auto& n : ctr)
            std::cout << static_cast<int>(n) << " ";
        std::cout << "\n";
    }
}
}   // namespace Nardi