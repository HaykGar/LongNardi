#include "NardiBoard.h"

///////////// Constructor /////////////

NardiBoard::NardiBoard() :      player_idx(0), player_sign(BoolToSign(player_idx)),
                                reached_enemy_home{0, 0}, pieces_left{PIECES_PER_PLAYER, PIECES_PER_PLAYER},
                                data {{ { PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                        {-PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }}  {}

///////////// Updates and Actions /////////////

void NardiBoard::Move(const NardiCoord& start, const NardiCoord& end)
{
    data[start.row][start.col]  -= player_sign;
    data[end.row][end.col]      += player_sign;

    if(end.row != player_idx && end.col >= 6 && (start.col < 6 || start.row != end.row) )  // moved to home from outside
        ++reached_enemy_home[player_idx];

    FlagHeadIfNeeded(start);
    if(CurrPlayerInEndgame())
        SetMaxOcc();
}

void NardiBoard::Remove(const NardiCoord& to_remove)
{
    data[to_remove.row][to_remove.col] -= player_sign;
    --pieces_left.at(player_idx);

    SetMaxOcc();
}

void NardiBoard::SetMaxOcc()
{
    max_num_occ[player_idx] = 6;
    while(max_num_occ[player_idx] > 0 && player_sign * at(!player_idx, COL - max_num_occ[player_idx]) <= 0 )   // slot empty or enemy
        --max_num_occ[player_idx];   
}


///////////// Legality /////////////

status_codes NardiBoard::ValidStart(const NardiCoord& start) const
{
    if(start.OutOfBounds())
        return status_codes::OUT_OF_BOUNDS;
    else if ( player_sign * at(start) <= 0)
        return status_codes::START_EMPTY_OR_ENEMY;
    else if ( HeadReuseIssue(start) ) 
        return status_codes::HEAD_PLAYED_ALREADY;
    else
        return status_codes::SUCCESS;
}

status_codes NardiBoard::WellDefinedEnd(const NardiCoord& start, const NardiCoord& end) const
{
    if( end.OutOfBounds() )
        return status_codes::OUT_OF_BOUNDS;
    else if ( player_sign * at(end) < 0)   // destination occupied by opponent
        return status_codes::DEST_ENEMY;
    else if(start.row == end.row )
    {
        if (start.col >= end.col)
            return status_codes::BACKWARDS_MOVE;    // treat start reselect as "backwards"

        return status_codes::SUCCESS; // prevent unnecessary RowChangeCheck
    }
    else if (Bad_RowChangeTo(end.row, player_idx)) // sr != er
        return status_codes::BOARD_END_REACHED;
    else
        return status_codes::SUCCESS;
}

bool NardiBoard::Bad_RowChangeTo(bool er, bool player) const
{
    int sign = player ? -1 : 1;
    int r = sign + er; // white to row 1 (r==2) or black to row 0 (r==-1) only acceptable choices, else r==1 or 0
    return (r == 0 || r == 1);
}

///////////// Coord and Distance Calculations /////////////

NardiCoord NardiBoard::CoordAfterDistance(const NardiCoord& start, int d, bool player) const
{
    int ec = start.col + d;
    if(ec >= 0 && ec < COL) // no row changing
        return {start.row, ec};
    else
    {
        bool er = !start.row;
        if( (d > 0 && Bad_RowChangeTo(er, player)) || (d < 0 && Bad_RowChangeTo(start.row, player)) )   
            return {start.row, ec};    // moved forward past end or backwards before beginning of board, return original out of bounds value
        
        else if(ec < 0)
            ec = COL + ec;
        else // ec > COL
            ec -= COL;

        return {!start.row, ec};
    }
}

NardiCoord NardiBoard::CoordAfterDistance(const NardiCoord& start, int d) const
{
    return CoordAfterDistance(start, d, player_idx);
}

unsigned NardiBoard::GetDistance(const NardiCoord& start, const NardiCoord& end) const    // well-defined move
{
    if(start.row == end.row)
        return end.col - start.col; // sc >= ec is invalid, so this will be positive if called on well-defined move
    else
        return COL - start.col + end.col;
}