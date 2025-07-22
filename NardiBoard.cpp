#include "NardiBoard.h"

///////////// Constructor /////////////

NardiBoard::NardiBoard() :      data {{ { PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                        {-PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }},
                                player_idx(0), player_sign(BoolToSign(player_idx)), reached_enemy_home{0, 0}, 
                                pieces_left{PIECES_PER_PLAYER, PIECES_PER_PLAYER}
{
    for(int p = 0; p < 2; ++p)
        for(int i = 0; i < 6; ++i)
            goes_idx_plusone[p][i].emplace(p, 0);   // "head" can move any of the numbers initially
}

///////////// Updates and Actions /////////////

void NardiBoard::Move(const NardiCoord& start, const NardiCoord& end)
{
    data[start.row][start.col]  -= player_sign;
    data[end.row][end.col]      += player_sign;
    OnMove(start, end);
}

void NardiBoard::OnMove(const NardiCoord& start, const NardiCoord& end)
{
    if(end.row != player_idx && end.col >= 6 && (start.col < 6 || start.row != end.row) )  // moved to home from outside
        ++reached_enemy_home[player_idx];

    FlagHeadIfNeeded(start);

    UpdateAvailabilitySets(start, end);
}

void NardiBoard::Remove(const NardiCoord& to_remove)
{
    data[to_remove.row][to_remove.col] -= player_sign;
    --pieces_left.at(player_idx);
    OnRemove(to_remove);
}

void NardiBoard::OnRemove(const NardiCoord& to_remove)
{
    UpdateAvailabilitySets(to_remove);
}

void NardiBoard::UpdateAvailabilitySets(const NardiCoord& start, const NardiCoord& dest)
{   // only to be called within MakeMove()
    UpdateAvailabilitySets(start);
    
    if ( at(dest) * player_sign == 1 )  // dest newly filled, note both ifs can be true
    {
        // update player's options - can now possibly move from dest square
        int d = 1;
        NardiCoord coord = CoordAfterDistance(dest, d);
        while( !coord.OutOfBounds() && d <= 6 )
        {
            if(at(coord) * player_sign >= 0) // square empty or occupied by player already
                goes_idx_plusone[player_idx][d-1].insert(dest);
            
            ++d;
            coord = CoordAfterDistance(dest, d);
        }
        // update other player's options - can no longer move to dest square
        d = 1;
        coord = CoordAfterDistance(dest, -d, !player_idx);
        while( !coord.OutOfBounds() && d <= 6 )
        {
            if(at(coord) * player_sign < 0) // other player occupies coord
                goes_idx_plusone[!player_idx][d-1].erase(coord);   // pieces at coord can no longer travel here

            ++d;
            coord = CoordAfterDistance(dest, -d, !player_idx);
        }
    }
}

void NardiBoard::UpdateAvailabilitySets(const NardiCoord start)  // not reference since we destroy stuff
{
    if (at(start) == 0) // start square vacated
    {
        for(int i = 0; i < 6; ++i)  // update player's options - can no longer move from this square
            goes_idx_plusone[player_idx][i].erase(start);   // no op if it wasn't in the set
        
        // update other player's options - can now move to this square
        int d = 1;
        NardiCoord coord = CoordAfterDistance(start, -d, !player_idx);

        while( !coord.OutOfBounds()  && d <= 6 )
        {
            if(at(coord) * player_sign < 0) // other player occupies coord
                goes_idx_plusone[!player_idx][d-1].insert(coord);   // coord reaches a new empty spot at start with distance d

            ++d;
            coord = CoordAfterDistance(start, -d, !player_idx);
        }
    }

    if(CurrPlayerInEndgame())
    {
        SetMaxOcc();
        for(int i = max_num_occ.at(player_idx); i <= 6; ++i)
            goes_idx_plusone[player_idx][i-1].insert({!player_idx, COL - max_num_occ.at(player_idx)}); 

        for(int i = 1; i < max_num_occ.at(player_idx); ++i)
            if(at(!player_idx, COL - i) * player_sign  > 0)
                goes_idx_plusone[player_idx][i-1].insert({!player_idx, COL - i});
    }
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
    //std::cout << "\n\n\n\n";
    
    //std::cout << "out of bounds? " << std::boolalpha << start.OutOfBounds() << "\n";
    // if(!start.OutOfBounds())
    // {
    //     //std::cout << "checking start: " << start.row << ", " << start.col << " with val " << at(start) << "\n";
    //     //std::cout << "empty or enemy? " << std::boolalpha << (player_sign * at(start) <= 0) << "\n";
    //     //std::cout << "HeadReuse?" << std::boolalpha << HeadReuseIssue(start) << "\n";
    // }

    //std::cout << "\n\n\n\n";

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

///////////// Calculations /////////////

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

unsigned NardiBoard::MovablePieces(const NardiCoord& start)
{
    if(at(start) == 0)
        return 0;
    else
        return IsPlayerHead(start) ? 1 : abs(at(start));
}