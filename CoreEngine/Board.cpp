#include "Board.h"

using namespace Nardi;

///////////// Constructor /////////////

Board::Board() :      data {{   { PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                {-PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }},
                                pieces_per_player{ {PIECES_PER_PLAYER, PIECES_PER_PLAYER} },
                                player_idx(0), player_sign(BoolToSign(player_idx)), head_used(false),
                                reached_enemy_home{0, 0}, pieces_left{PIECES_PER_PLAYER, PIECES_PER_PLAYER}
{}

Board::Board(const BoardConfig& d) : player_idx(0), player_sign(BoolToSign(player_idx)), head_used(false)
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

    if(!head_used && IsPlayerHead(start))
        head_used = true; 
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
}

void Board::Remove(const Coord& to_remove)
{
    data.at(to_remove.row).at(to_remove.col) -= player_sign;
    --pieces_left.at(player_idx);
}

void Board::UndoRemove(const Coord& to_remove)
{
    data.at(to_remove.row).at(to_remove.col) += player_sign;
    ++pieces_left.at(player_idx);
}

int Board::MaxNumOcc() const
{
    int max_num_occ = 6;
    while(max_num_occ > 0 && player_sign * at(!player_idx, COLS - max_num_occ) <= 0 )   // slot empty or enemy
        --max_num_occ;
        
    return max_num_occ;
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
    if(at(start) * player_sign <= 0)   // no pieces or enemy pieces
        return 0;
    else
        return IsPlayerHead(start) ? 1 : abs(at(start));
}

// operators

bool Board::operator== (const Board& other) const
{
    return  (
                this->data == other.data &&
                this->player_idx == other.player_idx &&
                this->head_used == other.head_used
            );
}

// testing

void Board::SetData(const BoardConfig& b)
{
    data = b;
    CalcPiecesLeftandReached();
    pieces_per_player = pieces_left;
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
}


///////////// Getters /////////////


const int8_t& Board::at(size_t r, size_t c) const
{   return data.at(r).at(c);   }

const int8_t& Board::at (const Coord& s) const
{   return at(s.row, s.col);   }

const BoardConfig& Board::View() const
{
    return data;
}

bool Board::PlayerIdx() const
{   return player_idx;   }

int8_t Board::PlayerSign() const
{   return player_sign;   }

bool Board::HeadUsed() const
{   return head_used;   }
 
const std::array<int, 2>& Board::ReachedEnemyHome() const
{   return reached_enemy_home;   }
 
const std::array<int, 2>& Board::PiecesLeft() const
{   return pieces_left;   }


void Board::Print() const
{
    DisplayBoard(data);
}

const BoardKey Board::AsKey() const
{
    BoardKey key = {};      // does this guarantee initializing to 0 ?
    std::array<int, 2> sq_occ = {0, 0};

    // player channels
    Coord start(player_idx, 0);
    for(int i = 0; i < ROWS*COLS; ++i)
    {
        Coord coord = CoordAfterDistance(start, i);
        int occupancy = at(coord) * player_sign;
        int n_pieces = abs(occupancy);
        int key_row = (occupancy >= 0) ? 0 : 3;
        
        if(n_pieces <= 1)   // 1 or 0
            key[key_row][i] = n_pieces;
        else // n_pieces > 1
        {
            key[key_row][i] = 1;
            key[key_row+1][i] = 1;
            key[key_row+2][i] = n_pieces - 2;
        }

        if(n_pieces > 0)
            ++sq_occ[(occupancy < 0)];    // idx 1 for opponent, 0 for friendly
    }


    // pieces off
    key[0][ROWS*COLS] = pieces_per_player[player_idx] - pieces_left[player_idx];
    key[1][ROWS*COLS] = pieces_per_player[!player_idx] - pieces_left[!player_idx];

    // total squares occupied
    key[2][ROWS*COLS] = sq_occ[0];
    key[3][ROWS*COLS] = sq_occ[1];

    // pieces not reached home
    key[4][ROWS*COLS] = pieces_per_player[player_idx] - reached_enemy_home[player_idx];
    key[5][ROWS*COLS] = pieces_per_player[!player_idx] - reached_enemy_home[!player_idx];

    return key;
}

///////////// Updates and Actions /////////////


void Board::SwitchPlayer()
{
    player_idx = !player_idx;
    player_sign = BoolToSign(player_idx);
    head_used = false;
}

///////////// Legality Helpers /////////////

bool Board::IsPlayerHead(const Coord& coord) const
{   return (coord.row == player_idx && coord.col == 0);   }


bool Board::HeadReuseIssue(const Coord& coord) const
{   return (IsPlayerHead(coord) && head_used);   }

bool Board::CurrPlayerInEndgame() const
{   return reached_enemy_home[player_idx] >= pieces_per_player[player_idx];   }