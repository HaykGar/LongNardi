#pragma once 

#include<optional>

const int ROW = 2;
const int COL = 12;

struct NardiCoord
{
    NardiCoord(int r, int c) : row(r), col(c) {}
    NardiCoord() : row(-1), col(-1) {}  // -1 to emphasize uninitialized `
    int row;
    int col;
};

enum Actions
{
    QUIT,
    UNDO,
    ROLL_DICE,
    SELECT_SLOT, 
    NO_OP
};   // later: add resign offer, mars offer

struct Command
{
    Command(Actions a) : action(a) {}
    Command(Actions a, int r, int c);
    Command(Actions a, NardiCoord coord);

    Actions action;
    std::optional<NardiCoord> to_select;
};
