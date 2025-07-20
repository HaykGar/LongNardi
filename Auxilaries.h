#pragma once 

#include<iostream>
#include <variant>

constexpr int ROW = 2;
constexpr int COL = 12;
constexpr int PIECES_PER_PLAYER = 15;

// enum classes for scoping and extra safety

enum class status_codes 
{
    SUCCESS,
    NO_LEGAL_MOVES_LEFT,
    OUT_OF_BOUNDS,
    START_EMPTY_OR_ENEMY,
    DEST_ENEMY,
    BACKWARDS_MOVE, 
    BOARD_END_REACHED, 
    NO_PATH,
    PREVENTS_COMPLETION,
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
    NardiCoord() : row(-1), col(-1) {}  // -1 to emphasize uninitialized `

    bool operator==(const NardiCoord& rhs) const;
    bool OutOfBounds() const;

    int row;
    int col;
};

inline
bool NardiCoord::operator==(const NardiCoord& rhs) const
{
    return (this->row == rhs.row && this->col == rhs.col);
}

inline 
bool NardiCoord::OutOfBounds() const
{   return (row < 0 || row >= ROW || col < 0 || col >= COL);    }

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

void VisualToGameCoord(NardiCoord& coord);

int BoolToSign(bool p_idx);