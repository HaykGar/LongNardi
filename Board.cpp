#include "Board.h"

using namespace Nardi;

///////////// Constructor /////////////

Board::Board() :      data {{ { PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                        {-PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }},
                                pieces_per_player{ {PIECES_PER_PLAYER, PIECES_PER_PLAYER} },
                                // data {{ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                //         {-PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2} }},
                                player_idx(0), player_sign(BoolToSign(player_idx)), head_used(false),
                                reached_enemy_home{0, 0}, pieces_left{PIECES_PER_PLAYER, PIECES_PER_PLAYER}
{
    for(int p = 0; p < 2; ++p)
        for(int i = 0; i < 6; ++i)
            goes_idx_plusone[p][i].emplace(p, 0);   // "head" can move any of the numbers initially 
    // CalcPiecesLeftandReached();
    // ConstructAvailabilitySets();
}

Board::Board(const std::array<std::array<int, COLS>, ROWS>& d) : player_idx(0), player_sign(BoolToSign(player_idx)), head_used(false)
{
    SetData(d);
}

///////////// Updates and Actions /////////////

void Board::Move(const Coord& start, const Coord& end)
{
    data.at(start.row).at(start.col)  -= player_sign;
    data.at(end.row).at(end.col)      += player_sign;
    OnMove(start, end);
}

void Board::OnMove(const Coord& start, const Coord& end)
{
    if(end.row != player_idx && end.col >= 6 && (start.col < 6 || start.row != end.row) )  // moved to home from outside
        ++reached_enemy_home[player_idx];

    FlagHeadIfNeeded(start);

    UpdateAvailabilitySets(start, end);
}

void Board::UndoMove(const Coord& start, const Coord& end)
{
    data.at(start.row).at(start.col)  += player_sign;
    data.at(end.row).at(end.col)      -= player_sign;
    OnUndoMove(start, end);
}

void Board::OnUndoMove(const Coord& start, const Coord& end)
{
    if(end.row != player_idx && end.col >= 6 && (start.col < 6 || start.row != end.row) )  // moved to home from outside
        --reached_enemy_home[player_idx];

    if(IsPlayerHead(start))
        head_used = false;

    UpdateAvailabilitySets(end, start);
}

void Board::Remove(const Coord& to_remove)
{
    data.at(to_remove.row).at(to_remove.col) -= player_sign;
    --pieces_left.at(player_idx);
    OnRemove(to_remove);
}

void Board::UndoRemove(const Coord& to_remove)
{
    data.at(to_remove.row).at(to_remove.col) += player_sign;
    ++pieces_left.at(player_idx);
    OnUndoRemove(to_remove);
}

void Board::OnUndoRemove(const Coord& to_remove)
{
    UpdateAvailabilityDest(to_remove);
}

void Board::OnRemove(const Coord& to_remove)
{
    UpdateAvailabilityStart(to_remove);
}

void Board::UpdateAvailabilitySets(const Coord& start, const Coord& dest)
{   // only to be called within MakeMove()
    UpdateAvailabilityStart(start);
    UpdateAvailabilityDest(dest);
}

void Board::UpdateAvailabilityStart(const Coord start)  // not reference since we destroy stuff
{
    if (at(start) == 0) // start square vacated
    {
        for(int i = 0; i < 6; ++i)  // update player's options - can no longer move from this square
            goes_idx_plusone[player_idx][i].erase(start);   // no op if it wasn't in the set
        
        // update other player's options - can now move to this square
        int d = 1;
        Coord coord = CoordAfterDistance(start, -d, !player_idx);

        while( !coord.OutOfBounds() && d <= 6 )
        {
            if(at(coord) * player_sign < 0 && WellDefinedEnd(coord, start, !player_idx) == status_codes::SUCCESS)
                goes_idx_plusone[!player_idx][d-1].insert(coord);   // enemy piece at coord brd legal to move to start

            ++d;
            coord = CoordAfterDistance(start, -d, !player_idx);
        }
    }

    if(CurrPlayerInEndgame())
    {
        SetMaxOcc();
        if(max_num_occ.at(player_idx) > 0)
        {
            for(int i = max_num_occ.at(player_idx); i <= 6; ++i)
                goes_idx_plusone[player_idx][i-1].insert({!player_idx, COLS - max_num_occ.at(player_idx)}); 

            for(int i = 1; i < max_num_occ.at(player_idx); ++i)
                if(at(!player_idx, COLS - i) * player_sign  > 0)    // player occupies square
                    goes_idx_plusone[player_idx][i-1].insert({!player_idx, COLS - i});
        }
    }
}

void Board::UpdateAvailabilityDest(const Coord& dest)
{
    if ( at(dest) * player_sign == 1 )  // dest newly filled, note both ifs can be true
    {
        // update player's options - can now possibly move from dest square
        int d = 1;
        Coord coord = CoordAfterDistance(dest, d);
        while( !coord.OutOfBounds() && d <= 6 )
        {
            if(WellDefinedEnd(dest, coord) == status_codes::SUCCESS)    // brd legal to move from newly filled slot to coord
                goes_idx_plusone[player_idx][d-1].insert(dest);
            
            ++d;
            coord = CoordAfterDistance(dest, d);
        }
        // update other player's options - can no longer move to dest square
        d = 1;
        coord = CoordAfterDistance(dest, -d, !player_idx);
        while( !coord.OutOfBounds() && d <= 6 )
        {
            goes_idx_plusone[!player_idx][d-1].erase(coord);   // pieces at coord can no longer travel here
                // no-op if coord wasn't already contained in it
            ++d;
            coord = CoordAfterDistance(dest, -d, !player_idx);
        }
    }
}

void Board::SetMaxOcc()
{
    max_num_occ[player_idx] = 6;
    while(max_num_occ[player_idx] > 0 && player_sign * at(!player_idx, COLS - max_num_occ[player_idx]) <= 0 )   // slot empty or enemy
        --max_num_occ[player_idx];   
}

///////////// Legality /////////////

status_codes Board::ValidStart(const Coord& start) const
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

status_codes Board::WellDefinedEnd(const Coord& start, const Coord& end) const
{
    return WellDefinedEnd(start, end, player_idx);
}

status_codes Board::WellDefinedEnd(const Coord& start, const Coord& end, bool player) const
{
    if( end.OutOfBounds() )
        return status_codes::OUT_OF_BOUNDS;
    else if ( BoolToSign(player) * at(end) < 0)   // destination occupied by player's opponent
        return status_codes::DEST_ENEMY;
    else if(start.row == end.row )
    {
        if (start.col >= end.col)
            return status_codes::BACKWARDS_MOVE;    // treat start reselect as "backwards"

        return status_codes::SUCCESS; // prevent unnecessary RowChangeCheck
    }
    else if (Bad_RowChangeTo(end.row, player)) // sr != er
        return status_codes::OUT_OF_BOUNDS;
    else
        return status_codes::SUCCESS;
}

bool Board::Bad_RowChangeTo(bool er, bool player) const
{
    int sign = player ? -1 : 1;
    int r = sign + er; // white to row 1 (r==2) or black to row 0 (r==-1) only acceptable choices, else r==1 or 0
    return (r == 0 || r == 1);
}

///////////// Calculations /////////////

Coord Board::CoordAfterDistance(const Coord& start, int d, bool player) const
{
    int ec = start.col + d;
    if(ec >= 0 && ec < COLS) // no row changing
        return {start.row, ec};
    else
    {
        bool er = !start.row;
        if( (d > 0 && Bad_RowChangeTo(er, player)) || (d < 0 && Bad_RowChangeTo(start.row, player)) )   
            return {start.row, ec};    // moved forward past end or backwards before beginning of board, return original out of bounds value
        
        else if(ec < 0)
            ec = COLS + ec;
        else // ec > COLS
            ec -= COLS;

        return {!start.row, ec};
    }
}

Coord Board::CoordAfterDistance(const Coord& start, int d) const
{
    return CoordAfterDistance(start, d, player_idx);
}

int Board::GetDistance(const Coord& start, const Coord& end) const
{
    return GetDistance(start, end, player_idx);
}

int Board::GetDistance(const Coord& start, const Coord& end, bool player) const
{
    if(start.row == end.row)
        return end.col - start.col;
    else if(end.row != player)  // row change forward
        return COLS - start.col + end.col;   
    else                            // row change backward
        return -(COLS - end.col + start.col);
}

unsigned Board::MovablePieces(const Coord& start) const
{
    if(at(start) == 0)
        return 0;
    else
        return IsPlayerHead(start) ? 1 : abs(at(start));
}

// operators

bool Board::operator== (const Board& other) const
{

    if (!(this->data == other.data && this->player_idx == other.player_idx && this->head_used == other.head_used))
        return false;

    for(int plyr = 0; plyr < 2; ++ plyr)
        for(int i = 0; i < 6; ++i)
            if(this->goes_idx_plusone[plyr][i] != other.goes_idx_plusone[plyr][i])
                return false;
    
    return true;
}

// testing

void Board::SetData(const std::array<std::array<int, COLS>, ROWS>& b)
{
    data = b;
    CalcPiecesLeftandReached();
    pieces_per_player = pieces_left;
    ConstructAvailabilitySets();
}

void Board::CalcPiecesLeftandReached()
{
    Coord starts [2] = { {0, 0}, {1, 0} };
    pieces_left = {0, 0};
    reached_enemy_home = {0, 0};
    
    for (; starts[0].col < 6; 
                starts[0] = CoordAfterDistance(starts[0], 1),
                starts[1] = CoordAfterDistance(starts[1], 1)    )
    {
        pieces_left[at(starts[0]) < 0] += abs(at(starts[0]));   // no-op if 0
        pieces_left[at(starts[1]) < 0] += abs(at(starts[1]));
    }
    for (;  starts[0].row == 0 && starts[0].col < COLS;
                    starts[0] = CoordAfterDistance(starts[0], 1),
                    starts[1] = CoordAfterDistance(starts[1], 1)    )    
    {
        if(at(starts[0]) < 0)   // black piece at end of white's row - home
            reached_enemy_home[1] += abs(at(starts[0]));
        if(at(starts[1]) > 0)   // white piece at end of black's row - home
            reached_enemy_home[0] += abs(at(starts[1]));

        pieces_left[at(starts[0]) < 0] += abs(at(starts[0]));   // no-op if 0
        pieces_left[at(starts[1]) < 0] += abs(at(starts[1]));
    }

    std::cout << "pieces left white: " << pieces_left[0] << "\n";
    std::cout << "pieces left black: " << pieces_left[1] << "\n";

    std::cout << "pieces reached white: " << reached_enemy_home[0] << "\n";
    std::cout << "pieces reached black: " << reached_enemy_home[1] << "\n";
}

void Board::ConstructAvailabilitySets()
{
    for(int i = 0; i < 6; ++i)
    {
        goes_idx_plusone[0][i].clear();
        goes_idx_plusone[1][i].clear();
    }
    for(int plyr = 0; plyr < 2; ++plyr)
    {
        Coord start(player_idx, 0);
        while (!start.OutOfBounds())
        {
            if(ValidStart(start) == status_codes::SUCCESS)
            {
                for(int d = 1; d <= 6; ++d)
                {
                    Coord dest = CoordAfterDistance(start, d, player_idx);
                    if(WellDefinedEnd(start, dest) == status_codes::SUCCESS)
                        goes_idx_plusone[player_idx][d-1].insert(start);
                }
            }
            start = CoordAfterDistance(start, 1, player_idx);
        }

        if(CurrPlayerInEndgame())
        {
            std::cout << "endgame case detected for player " << player_sign << "\n";

            SetMaxOcc();
            if(max_num_occ.at(player_idx) > 0)
            {
                for(int i = max_num_occ.at(player_idx); i <= 6; ++i)
                    goes_idx_plusone[player_idx][i-1].insert({!player_idx, COLS - max_num_occ.at(player_idx)}); 

                for(int i = 1; i < max_num_occ.at(player_idx); ++i)
                    if(at(!player_idx, COLS - i) * player_sign  > 0)
                        goes_idx_plusone[player_idx][i-1].insert({!player_idx, COLS - i});
            }
        }

        SwitchPlayer();
    }
}


///////////// Getters /////////////


const int& Board::at(size_t r, size_t c) const
{   return data.at(r).at(c);   }

const int& Board::at (const Coord& s) const
{   return at(s.row, s.col);   }

const boardConfig& Board::View() const
{
    return data;
}

bool Board::PlayerIdx() const
{   return player_idx;   }

int Board::PlayerSign() const
{   return player_sign;   }

bool Board::HeadUsed() const
{   return head_used;   }

const std::array<int, 2>& Board::MaxNumOcc() const
{   return max_num_occ;   }
 
const std::array<int, 2>& Board::ReachedEnemyHome() const
{   return reached_enemy_home;   }

 
const std::array<int, 2>& Board::PiecesLeft() const
{   return pieces_left;   }


const std::array< std::array< std::unordered_set<Coord>, 6 >, 2 >& Board::GoesIdxPlusOne() const
{   
    return goes_idx_plusone;   
}

const std::unordered_set<Coord>& Board::PlayerGoesByDist(size_t dist) const
{   
    return goes_idx_plusone.at(player_idx).at(dist - 1);   
}

void Board::Print() const
{
    DisplayBoard(data);
}

///////////// Updates and Actions /////////////


void Board::SwitchPlayer()
{
    player_idx = !player_idx;
    player_sign = BoolToSign(player_idx);
    head_used = false;
}

 
void Board::FlagHeadIfNeeded(const Coord& start)
{ 
    if(!head_used && IsPlayerHead(start))
        head_used = true; 
}

///////////// Legality Helpers /////////////

bool Board::IsPlayerHead(const Coord& coord) const
{   return (coord.row == player_idx && coord.col == 0);   }


bool Board::HeadReuseIssue(const Coord& coord) const
{   return (IsPlayerHead(coord) && head_used);   }

bool Board::CurrPlayerInEndgame() const
{   return reached_enemy_home[player_idx] >= pieces_per_player[player_idx];   }