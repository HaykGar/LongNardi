#pragma once 

#include<iostream>
#include <variant>

const int ROW = 2;
const int COL = 12;

struct NardiCoord
{
    NardiCoord(int r, int c) : row(r), col(c) {}
    NardiCoord() : row(-1), col(-1) {}  // -1 to emphasize uninitialized `

    bool operator==(const NardiCoord& rhs) const;

    int row;
    int col;
};

inline
bool NardiCoord::operator==(const NardiCoord& rhs) const
{
    return (this->row == rhs.row && this->col == rhs.col);
}

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

enum class Actions
{
    QUIT,
    UNDO,
    ROLL_DICE,
    SELECT_SLOT, 
    MOVE_BY_DICE,
    NO_OP
};   // later: add resign offer, mars offer

void VisualToGameCoord(NardiCoord& coord);

struct Command // considering making this std::variant or something... 
{
    Command(Actions a) : action(a) {}
    Command(Actions a, NardiCoord coord);
    Command(Actions a, int r, int c);
    Command(Actions a, bool dice_idx);

    Actions action;
    std::variant<std::monostate, NardiCoord, bool> payload;
};
