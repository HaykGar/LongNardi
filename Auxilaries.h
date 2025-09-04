#pragma once 

#include <iostream>
#include <variant>
#include <string>

namespace Nardi
{

constexpr int ROWS = 2;
constexpr int COLS = 12;
constexpr int PIECES_PER_PLAYER = 15;

using boardConfig = std::array< std::array<int, COLS>, ROWS>;
using dice = std::array<int, 2>;

// enum classes for scoping and extra safety

enum class status_codes 
{
    SUCCESS,
    NO_LEGAL_MOVES_LEFT,
    OUT_OF_BOUNDS,
    START_EMPTY_OR_ENEMY,
    DEST_ENEMY,
    BACKWARDS_MOVE, 
    NO_PATH,
    PREVENTS_COMPLETION,
    BAD_BLOCK,
    DICE_USED_ALREADY,
    HEAD_PLAYED_ALREADY,
    MISC_FAILURE
};        

enum class Actions
{
    QUIT,
    UNDO,
    ROLL_DICE,
    SELECT_SLOT, 
    MOVE_BY_DICE,
    AUTOPLAY,
    NO_OP
};   // later: add resign offer, mars offer

struct Coord
{
    Coord(int r, int c) : row(r), col(c) {}
    Coord() : row(-1), col(-1) {} // initialize out of bounds to force explicit assignment before use

    bool operator==(const Coord& rhs) const;
    bool OutOfBounds() const;
    bool InBounds() const;

    void Print() const;
    std::string AsStr() const;

    int row;
    int col;
};

struct StartAndDice
{ 
    StartAndDice(const Coord& f, bool d) : _from(f), _diceIdx(d) {}
    Coord _from; 
    bool _diceIdx; 
};

using MoveSequence = std::vector<StartAndDice>;

}   // namespace Nardi

namespace std
{
    template<>
    struct hash<Nardi::Coord>
    {
        constexpr std::size_t operator()(const Nardi::Coord& c) const noexcept
        {
            if(c.row < 0 || c.col < 0){
                std::cerr << "Bad Coord hashed\n";
                return Nardi::ROWS*Nardi::COLS + 1;  // one after any valid coord
            }
            return Nardi::COLS * c.row + c.col;
        }
    };
} // namespace std

namespace Nardi
{

struct Command // considering making this std::variant or something... 
{
    Command(Actions a) : action(a) {}
    Command(Coord coord);
    Command(int r, int c);
    Command(bool dice_idx);
    Command(std::string final_config);

    Actions action;
    std::variant<std::monostate, Coord, bool, std::string> payload;
};

void VisualToGameCoord(Coord& coord); // not needed currently, but for graphics later
int BoolToSign(bool p_idx);
void DispErrorCode(status_codes code);

void DisplayBoard(const std::array<std::array<int, COLS>, ROWS>& b);

std::string Board2Str(const boardConfig& b);

}   // namespace Nardi