#pragma once 

#include <iostream>
#include <variant>
#include <string>

constexpr int ROW = 2;
constexpr int COL = 12;
constexpr int PIECES_PER_PLAYER = 15;

using boardConfig = std::array< std::array<int, COL>, ROW>;
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
    NO_OP
};   // later: add resign offer, mars offer

struct NardiCoord
{
    NardiCoord(int r, int c) : row(r), col(c) {}
    NardiCoord() : row(-1), col(-1) {} // initialize out of bounds to force explicit assignment before use

    bool operator==(const NardiCoord& rhs) const;
    bool OutOfBounds() const;
    bool InBounds() const;

    void Print() const;
    std::string AsStr() const;

    int row;
    int col;
};

struct StartAndDice{ 
    StartAndDice(NardiCoord f, bool d) : _from(f), _diceIdx(d) {}
    NardiCoord _from; 
    bool _diceIdx; 
};


namespace std 
{
    template<>
    struct hash<NardiCoord>
    {
        constexpr std::size_t operator()(const NardiCoord& c) const noexcept
        {
            if(c.row < 0 || c.col < 0){
                std::cerr << "Bad NardiCoord hashed\n";
                return ROW*COL + 1;  // one after any valid coord
            }
            return COL * c.row + c.col;
        }
    };
} // namespace std

struct Command // considering making this std::variant or something... 
{
    Command(Actions a) : action(a) {}
    Command(Actions a, NardiCoord coord);
    Command(Actions a, int r, int c);
    Command(Actions a, bool dice_idx);

    Actions action;
    std::variant<std::monostate, NardiCoord, bool> payload;
};

void VisualToGameCoord(NardiCoord& coord); // not needed currently, but for graphics later
int BoolToSign(bool p_idx);
void DispErrorCode(status_codes code);

void DisplayBoard(const std::array<std::array<int, COL>, ROW>& b);

std::string Board2Str(const boardConfig& b);