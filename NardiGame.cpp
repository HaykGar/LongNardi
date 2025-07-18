#include "NardiGame.h"
#include "ReaderWriter.h"

///////////////////////////
////////   Game   ////////
/////////////////////////

///////////// Constructor /////////////

Game::Game(int rseed) : pieces_left({PIECES_PER_PLAYER, PIECES_PER_PLAYER}),  rng(rseed), dist(1, 6), dice({0, 0}), times_dice_used({0, 0}),
                        doubles_rolled(false), rw(nullptr), arbiter(this)
{
    board = { { { PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                {-PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} } };
} 

///////////// Gameplay /////////////

Game::status_codes Game::RollDice() // important to force this only once per turn in controller, no explicit safeguard here
{    
    times_dice_used[0] = 0;
    times_dice_used[1] = 0;
    dice[0] = dist(rng);
    dice[1] = dist(rng);
    doubles_rolled = (dice[0] == dice[1]);

    rw->AnimateDice();

    return arbiter.OnRoll();
}

Game::status_codes Game::TryStart(const NardiCoord& s) const
{
    // During endgame, if it's a valid start just check if there's a forced move from here, will streamline play
    return arbiter.ValidStart(s.row, s.col);
}

Game::status_codes Game::TryFinishMove(const NardiCoord& start, const NardiCoord& end) // assumes start already checked
{   
    auto [can_move, times_used]  = arbiter.LegalMove(start.row, start.col, end.row, end.col);
    if (can_move != status_codes::SUCCESS)
        return can_move;
    else
    {
        UseDice(0, times_used[0]); UseDice(1, times_used[1]);
        return MakeMove(start, end);    // checks for further forced moves internally
    }
}

Game::status_codes Game::TryMoveByDice(const NardiCoord& start, bool dice_idx)
{
    if(CurrPlayerInEndgame() && arbiter.CanRemovePiece(start, dice_idx))
    {
        UseDice(dice_idx);
        RemovePiece(start);
    }
    
    auto [result, dest]  = arbiter.CanMoveByDice(start, dice_idx);
    if(result != status_codes::SUCCESS )
        return result;
    
    else{
        UseDice(dice_idx);
        return MakeMove(start, dest);
    }
}

Game::status_codes Game::MakeMove(const NardiCoord& start, const NardiCoord& end, bool check_requested)
{
    board[start.row][start.col] -= arbiter.GetPlayerSign();
    board[end.row][end.col] += arbiter.GetPlayerSign();

    rw->ReAnimate();

    if(check_requested)
        return arbiter.OnMove(start, end);
    else
        return status_codes::SUCCESS;
}

Game::status_codes Game::RemovePiece(const NardiCoord& start)
{
    board[start.row][start.col] -= GetPlayerSign();
    --pieces_left[GetPlayerIdx()];
    rw->ReAnimate();
    if(GameIsOver())
        return status_codes::NO_LEGAL_MOVES_LEFT;
    
    return arbiter.OnRemoval(start);
}

////////////////////////////
////////   Arbiter ////////
//////////////////////////

///////////// Constructor /////////////

Game::Arbiter::Arbiter(Game* gp) : g(gp), head_used(false), player_idx(0), turn_number({0, 0}), in_enemy_home({0, 0})   // player sign 50/50 ?
{
    player_sign = player_idx ? -1 : 1;
    for(int p = 0; p < 2; ++p)  // 0 is player 1, 1 is player -1
        for(int i = 0; i < 6; ++i)
            goes_idx_plusone[p][i].insert(NardiCoord(p, 0));   // "head" can move any of the numbers initially
}

///////////// Legality /////////////

Game::status_codes Game::Arbiter::ValidStart(int sr, int sc) const
{
    if(sr < 0 || sr > ROW - 1 || sc < 0 || sc > COL - 1)
        return status_codes::OUT_OF_BOUNDS;
    else if (player_sign * g->board[sr][sc] <= 0)
        return status_codes::START_EMPTY_OR_ENEMY;
    else if (HeadReuseIssue(sr, sc)) 
        return status_codes::HEAD_PLAYED_ALREADY;
    else 
        return status_codes::SUCCESS;
}

Game::status_codes Game::Arbiter::WellDefinedEnd(int sr, int sc, int er, int ec) const
{   // avoid using goes_idx_plusone here, else circular reasoning
    if(er < 0 || er > ROW - 1 || ec < 0 || ec > COL - 1)
        return status_codes::OUT_OF_BOUNDS;
    
    else if (player_sign * g->board[er][ec] < 0)   // destination occupied by opponent
        return status_codes::DEST_ENEMY;
    
    else if(sr == er )
    {
        if (sc == ec)
            return status_codes::START_RESELECT;
        else if (sc > ec)
            return status_codes::BACKWARDS_MOVE;

        return status_codes::SUCCESS; // prevent unnecessary RowChangeCheck
    }

    else if (BadRowChange(er)) // sr != er
        return status_codes::BOARD_END_REACHED;
    
    return status_codes::SUCCESS;
}

Game::status_codes Game::Arbiter::WellDefinedEnd(const NardiCoord& start, const NardiCoord& end) const // start already checked to be valid
{
    return WellDefinedEnd(start.row, start.col, end.row, end.col);
}

bool Game::Arbiter::BadRowChange(bool er, bool player) const // guaranteed sr != er
{
    int sign = player ? -1 : 1;
    int r = sign + er; // white to row 1 (r==2) or black to row 0 (r==-1) only acceptable choices, else r==1 or 0
    return (r == 0 || r == 1);
}

bool Game::Arbiter::BadRowChange(bool er) const // guaranteed sr != er
{
    return BadRowChange(er, player_idx);
}

std::pair<Game::status_codes, NardiCoord> Game::Arbiter::CanMoveByDice(const NardiCoord& start, bool dice_idx, bool moved_hypothetically) const
{
    if(!CanUseDice(dice_idx))
        return {status_codes::DICE_USED_ALREADY, {} };
    
    NardiCoord final_dest = CoordAfterDistance(start.row, start.col, g->dice[dice_idx], player_idx);
    status_codes result = WellDefinedEnd(start.row, start.col, final_dest.row, final_dest.col);

    if (result == status_codes::SUCCESS && !moved_hypothetically && PreventsTurnCompletion(start, dice_idx))
        return {status_codes::ILLEGAL_MOVE, {} };
    else
        return {result, final_dest};
}

bool Game::Arbiter::CanUseDice(bool idx, int n) const
{
    /*
        if doubles, new_val + times_dice_used[!idx] <= 4      <==>    new_val <= 4 - times_dice_used[!idx]
        else, new_val <= 1
    */
    int new_val = g->times_dice_used[idx] + n;
    return ( new_val <= 1 + g->doubles_rolled*(3 - g->times_dice_used[!idx]));
}

bool Game::Arbiter::PreventsTurnCompletion(const NardiCoord& start, bool dice_idx) const // only called by MoveByDice
{
    return (!g->doubles_rolled && CanUseDice(0) && CanUseDice(1) && min_options[0] >= 1 && min_options[1] >= 1 
            && goes_idx_plusone[player_idx][g->dice[!dice_idx] - 1].empty() && !StepsTwice(start));
        /* 
            no doubles, both dice useable, each dice had options at the start of turn, the 
            other dice can only play via a 2step, no 2steps from here
        */
}

bool Game::Arbiter::StepsTwice(const NardiCoord& start) const
{
    NardiCoord dest = CoordAfterDistance(start, g->dice[0] + g->dice[1], player_idx);
    return (WellDefinedEnd(start, dest) == status_codes::SUCCESS);
}

bool Game::Arbiter::CanRemovePiece(const NardiCoord& start, bool dice_idx)
{
    if(!CanUseDice(dice_idx))
        return false;
    
    int pos_from_end = COL - start.col;
    return (pos_from_end == g->dice[dice_idx] ||  // dice val exactly
            (pos_from_end == max_num_occ[player_idx] && g->dice[dice_idx] > pos_from_end) );  // largest available, less than dice
}

std::pair<Game::status_codes, std::array<int, 2>> Game::Arbiter::LegalMove(int sr, int sc, int er, int ec)    
// array represents how many times each dice is used, 0 or 1 usually, in case of doubles can be up to 4
{    
    unsigned d = GetDistance(sr, sc, er, ec); // bool casting here is ok since move well defined

    if(d == g->dice[0])
        return {CanMoveByDice({sr, sc}, 0).first, {1, 0} };
    else if (d == g->dice[1]) 
        return {CanMoveByDice({sr, sc}, 1).first, {0, 1} };
    else if (d == g->dice[0] + g->dice[1])
        return {LegalMove_2step(sr, sc).first, {1, 1}};
    else if ( g->doubles_rolled && (d % g->dice[0] == 0)  )
    {
        if(!CanUseDice(0, d / g->dice[0]) )
            return { status_codes::DICE_USED_ALREADY, {} };
        
        auto [step2_status, step2_dest] = LegalMove_2step(sr, sc); 
        if(step2_status != status_codes::SUCCESS)
            return { status_codes::NO_PATH, {} };
        else if( d == g->dice[0] * 3)
            return { CanMoveByDice(step2_dest, 0).first, {3, 0} };
        else if(d == g->dice[0] * 4)
            return { LegalMove_2step(step2_dest.row, step2_dest.col).first, {4, 0} };
    }
    
    return {status_codes::NO_PATH, {}};
}

std::pair<Game::status_codes, NardiCoord> Game::Arbiter::LegalMove_2step(bool sr, int sc)  
{
    if(!CanUseDice(0) || !CanUseDice(1))        // redundant but safer
        return {status_codes::DICE_USED_ALREADY, {} };

    bool first_dice = 0;
    auto [status, mid] = CanMoveByDice( {sr, sc}, first_dice);
    if(status != status_codes::SUCCESS && !g->doubles_rolled){
        std::tie(status, mid) = CanMoveByDice( {sr, sc}, 1);    
        first_dice = !first_dice;   // try both dice to get to a midpoint, no need if doubles
    }

    if(status == status_codes::SUCCESS)
        return CanMoveByDice(mid, !first_dice, true);
    else
        return {status_codes::NO_PATH, {} };    // unable to reach midpoint
}


///////////// Coord and Distance Calculations /////////////

NardiCoord Game::Arbiter::CoordAfterDistance(int row, int col, int d, bool player) const
{
    int ec = col + d;
    if(ec >= 0 && ec < COL) // no row changing
        return {row, ec};
    else
    {
        bool er = !row;
        if( (d > 0 && BadRowChange(er, player)) || (d < 0 && BadRowChange(row, player)) )   
            return {row, ec};    // moved forward past end or backwards before beginning of board, return original out of bounds value
        
        else if(ec < 0)
            ec = COL + ec;
        else // ec > COL
            ec -= COL;

        return {!row, ec};
    }
}

NardiCoord Game::Arbiter::CoordAfterDistance(const NardiCoord& start, int d, bool player) const
{
    return CoordAfterDistance(start.row, start.col, d, player);
}

unsigned Game::Arbiter::GetDistance(bool sr, int sc, bool er, int ec) const
{
    if(sr == er)
        return ec - sc; // sc >= ec is invalid, so this will be positive if called on well-defined move
    else
        return COL - sc + ec;
}

///////////// Updates and Actions /////////////

void Game::Arbiter::UpdateAvailabilitySets(const NardiCoord start, const NardiCoord dest) // not reference since we destroy stuff
{   // only to be called within MakeMove()

    UpdateAvailabilitySets(start);
    
    if (abs(g->board[dest.row][dest.col]) == 1)  // dest newly filled, note both ifs can be true
    {
        // update player's options - can now possibly move from dest square
        int d = 1;
        NardiCoord coord = CoordAfterDistance(dest, d, player_idx);
        bool in_bounds = InBounds(coord);
        while( in_bounds && d <= 6 )
        {
            if(g->board[coord.row][coord.col] * player_sign >= 0) // square empty or occupied by player already
                goes_idx_plusone[player_idx][d-1].insert(dest);
            
            ++d;
            coord = CoordAfterDistance(dest, d, player_idx);
            in_bounds = InBounds(coord);
        }
        // update other player's options - can no longer move to dest square
        d = 1;
        coord = CoordAfterDistance(dest, -d, !player_idx);
        in_bounds = InBounds(coord);
        while( in_bounds && d <= 6 )
        {
            if(g->board[coord.row][coord.col] * player_sign < 0) // other player occupies coord
                goes_idx_plusone[!player_idx][d-1].erase(coord);   // pieces at coord can no longer travel here

            ++d;
            coord = CoordAfterDistance(dest, -d, !player_idx);
            in_bounds = InBounds(coord);
        }
    }
}

void Game::Arbiter::UpdateAvailabilitySets(const NardiCoord start)
{
    if (g->board[start.row][start.col] == 0) // start square vacated
    {
        for(int i = 0; i < 6; ++i)  // update player's options - can no longer move from this square
            goes_idx_plusone[player_idx][i].erase(start);   // no op if it wasn't in the set
        
        // update other player's options - can now move to this square
        int d = 1;
        NardiCoord coord = CoordAfterDistance(start, -d, !player_idx);
        bool in_bounds = InBounds(coord);

        while( in_bounds  && d <= 6 )
        {
            if(g->board[coord.row][coord.col] * player_sign < 0) // other player occupies coord
                goes_idx_plusone[!player_idx][d-1].insert(coord);   // coord reaches a new empty spot at start with distance d

            ++d;
            coord = CoordAfterDistance(start, -d, !player_idx);
            in_bounds = InBounds(coord);
        }
    }

    if(CurrPlayerInEndgame())
    {
        for(int i = max_num_occ[player_idx]; i <= 6; ++i)
            goes_idx_plusone[player_idx][i-1].insert({!player_idx, COL - max_num_occ[player_idx]}); 

        for(int i = 1; i < max_num_occ[player_idx]; ++i)
        {
            if(g->board[!player_idx][COL - i] * player_sign > 0)
                goes_idx_plusone[player_idx][i-1].insert({!player_idx, COL - i});
        }
    }
}

void Game::Arbiter::SetMaxOcc()
{
    max_num_occ[player_idx] = 6;     // largest number by which we can remove a dice, class member???
    while(player_sign * g->board[!player_idx][COL - max_num_occ[player_idx]] <= 0 && max_num_occ[player_idx] > 0)   // slot empty or enemy
        --max_num_occ[player_idx];   
}

Game::status_codes Game::Arbiter::OnRoll()
{
    IncrementTurnNumber();
    return CheckForcedMoves();
}

Game::status_codes Game::Arbiter::OnMove(const NardiCoord& start, const NardiCoord& end)
{
    UpdateAvailabilitySets(start, end);
    FlagHeadIfNeeded(start);
    if(end.row != player_idx && end.col >= 6 && (start.col < 6 || start.row != end.row) )  // moved to home from outside
        ++in_enemy_home[player_idx];
    if(CurrPlayerInEndgame())
        SetMaxOcc();
    
    return CheckForcedMoves();
}


Game::status_codes Game::Arbiter::OnRemoval(const NardiCoord& start)
{
    SetMaxOcc();
    UpdateAvailabilitySets(start);

    return CheckForcedMoves();
}

///////////// Forced Moves /////////////

Game::status_codes Game::Arbiter::CheckForcedMoves()
{
    if(g->doubles_rolled)
        return CheckForced_Doubles();

    else if(CanUseDice(0) && CanUseDice(1))
        return CheckForced_2Dice();

    else if(CanUseDice(0) || CanUseDice(1))
        return CheckForced_1Dice();
    else
        return status_codes::NO_LEGAL_MOVES_LEFT;
}

Game::status_codes Game::Arbiter::CheckForced_2Dice()
{   
    min_options = {goes_idx_plusone[player_idx][g->dice[0] - 1].size(), goes_idx_plusone[player_idx][g->dice[1] - 1].size() };
    bool max_dice = (g->dice[1] > g->dice[0]);

    if(CurrPlayerInEndgame() && g->dice[max_dice] >= max_num_occ[player_idx])
        return ForceRemovePiece({!player_idx, COL - max_num_occ[player_idx]}, max_dice);
    
    // no doubles from here, no head reuse issues since we can't have made a move yet
        // at this point this is just the number of ways to move in one step for both dice rolls
    if(min_options[0] > 1 && min_options[1] > 1)
        return status_codes::SUCCESS;
    else if(min_options[0] + min_options[1] == 1)   // one has no options, other has only 1
        return HandleForced1Dice(min_options[1] == 1);
    else if (min_options[0] == min_options[1])
    {
        if (min_options[0] == 0)
            return status_codes::NO_LEGAL_MOVES_LEFT;
        else if(min_options[0] == 1)
        {
            NardiCoord start = *goes_idx_plusone[player_idx][g->dice[0] - 1].begin();
            if(start == *goes_idx_plusone[player_idx][g->dice[1] - 1].begin() && 
                (g->board[start.row][start.col] * player_sign == 1 || IsHead(start)) )
                return HandleForced1Dice(max_dice); // Will make other forced move if possible
        }
    }
    // eliminated 0-0, 0-1, and 1-1 with 1 piece cases, options[more_options] > 1 or they're both 1
    bool more_options = min_options[1] > min_options[0];
    // iterate through coords for dice with more options, try to find a 2step move
    std::unordered_set<NardiCoord> two_step_starts; // start coords for 2-step moves
    for(const auto& coord : goes_idx_plusone[player_idx][g->dice[more_options] - 1])    
    {
        auto [can_go, _] = LegalMove_2step(coord.row, coord.col);
        if(can_go == status_codes::SUCCESS)
        {
            if(min_options[more_options] == 1)    // case both only move from 1 square w/ multiple pieces
                ++min_options[more_options];
            
            ++min_options[!more_options];
            if(min_options[0] > 1 && min_options[1] > 1)
                return status_codes::SUCCESS;
            two_step_starts.insert(coord);
        }
    }
    if(min_options[max_dice] == 1) // if can only play 1 dice, forced to play larger one
        return HandleForced2Dice(max_dice, two_step_starts);
    else if(min_options[!max_dice] == 1)
        return HandleForced2Dice(!max_dice, two_step_starts);
    else    // one of the moves is impossible but nothing is forced, there are multiple legal moves for the other
        return status_codes::SUCCESS;   
}

Game::status_codes Game::Arbiter::HandleForced2Dice(bool dice_idx, const std::unordered_set<NardiCoord>& two_step_starts) // return no legal moves if none left after making one?
{
    if(goes_idx_plusone[player_idx][g->dice[dice_idx] - 1].size() == 1)  // no new 2step moves
        return ForceMove(*goes_idx_plusone[player_idx][g->dice[dice_idx] - 1].begin(), dice_idx); // makes other forced move as needed
    else    // only a 2step move for max dice
        return ForceMove(*two_step_starts.begin(), !dice_idx); // should always be NO_LEGAL_MOVES_LEFT
        /*
            can't make single move with dice[idx], so moves other dice then when checking forced moves it should have to move 
            again as idx will only have one option
        */
}

Game::status_codes Game::Arbiter::CheckForced_1Dice()
{
    bool active_dice = CanUseDice(1);
    if (CurrPlayerInEndgame() && (g->dice[active_dice] >= max_num_occ[player_idx]) )
        return ForceRemovePiece({!player_idx, COL - max_num_occ[player_idx]}, active_dice);
    
    return HandleForced1Dice(active_dice);
}

Game::status_codes Game::Arbiter::HandleForced1Dice(bool dice_idx) // no doubles here, one dice used already
{       
    if(goes_idx_plusone[player_idx][g->dice[dice_idx] - 1].empty())
        return status_codes::NO_LEGAL_MOVES_LEFT;
    else if (goes_idx_plusone[player_idx][g->dice[dice_idx] - 1].size() == 1)
    {
        NardiCoord start = *goes_idx_plusone[player_idx][g->dice[dice_idx] - 1].begin();
        if(!HeadReuseIssue(start))
            return ForceMove(start, dice_idx);
        else
            return status_codes::NO_LEGAL_MOVES_LEFT;   // success or fail, no legal moves remain
    }
    else if (goes_idx_plusone[player_idx][g->dice[dice_idx] - 1].size() == 2 &&     // two pieces that go
            goes_idx_plusone[player_idx][g->dice[dice_idx] - 1].contains(head[player_idx]) &&    // one of which is head
            HeadReuseIssue(head[player_idx]))                                        // can't reuse head, so only one piece which actually goes
    {
        std::unordered_set<NardiCoord>::iterator it =  goes_idx_plusone[player_idx][g->dice[dice_idx] - 1].begin();
        if (IsHead(*it)) // only 2 items, either not head or next one is
            ++it;
        ForceMove(*it, dice_idx);
        return status_codes::NO_LEGAL_MOVES_LEFT;
    }
    else
        return status_codes::SUCCESS;   // not forced to move, at least two choices
}

Game::status_codes Game::Arbiter::CheckForced_Doubles()
{
    if(turn_number[player_idx] == 1 && (g->dice[0] == 4 || g->dice[0] == 6 ) )  // first move double 4 or 6
    {
        Force_1stMoveException();
        return status_codes::NO_LEGAL_MOVES_LEFT;
    }

    int steps_left = 4 - ( (g->times_dice_used[0]*g->dice[0] + g->times_dice_used[1]*g->dice[1]) / g->dice[0] ); // steps left to complete turn
    int steps_taken = 0;    // counter, when exceeds steps_left 

    if(goes_idx_plusone[player_idx][g->dice[0] - 1].empty())    // no pieces that go
        return status_codes::NO_LEGAL_MOVES_LEFT;

    std::queue<NardiCoord> move_starts;
    for(const auto& coord : goes_idx_plusone[player_idx][g->dice[0] - 1])
    {
        if (HeadReuseIssue(coord))
            continue;   // don't use this coord

        int n_pieces = IsHead(coord) ? 1 : abs(g->board[coord.row][coord.col]);
        
        NardiCoord start = coord;
        auto [canGo, dest] = CanMoveByDice(start, 0);

        while(canGo == status_codes::SUCCESS)   // runs at most 4 times without returning
        {
            steps_taken += n_pieces;
            move_starts.push(start);

            if(steps_taken > steps_left)            
                return status_codes::SUCCESS;

            start = dest;
            std::tie(canGo, dest) = CanMoveByDice(start, 0);
        }
    }  
    // if we exit loop, then the moves are forced
    while(!move_starts.empty())
    {
        NardiCoord start = move_starts.front();
        int n = IsHead(start) ? 1 : abs(g->board[start.row][start.col]);
        for(int i = 0; i < n; ++i)
            ForceMove(start, 0, false);
        
        move_starts.pop();
    }

    return status_codes::NO_LEGAL_MOVES_LEFT;   // impossible to have partial forcing in doubles case
}

void Game::Arbiter::Force_1stMoveException()
{
    g->board[head[player_idx].row][head[player_idx].col] -= 2 * player_sign;
    int dist = g->dice[0] * (1 + (g->dice[0] == 4) );    // 8 if double 4, else 6

    NardiCoord dest(head[player_idx].row, dist);
    g->board[dest.row][dest.col] += 2 * player_sign; 

    NardiCoord coord_to;
    NardiCoord coord_from;

    for(int i = 0; i < 6; ++i)
    {
        coord_to = CoordAfterDistance(dest, i+1, player_idx);
        if(g->board[coord_to.row][coord_to.col] == 0 )
            goes_idx_plusone[player_idx][i].insert(dest);

        coord_from = CoordAfterDistance(dest, -(i+1), player_idx);
        if(g->board[coord_from.row][coord_from.col] * player_sign < 0)
            goes_idx_plusone[!player_idx][i].erase(coord_from);
    }

    g->rw->ReAnimate(); 
}

Game::status_codes Game::Arbiter::ForceMove(const NardiCoord& start, bool dice_idx, bool check_further)  // only to be called when forced
{
    NardiCoord dest =  CoordAfterDistance(start.row, start.col, g->dice[dice_idx], player_idx);
    g->UseDice(dice_idx);
    return g->MakeMove(start, dest, check_further);
}

Game::status_codes Game::Arbiter::ForceRemovePiece(const NardiCoord& start, bool dice_idx)
{
    g->UseDice(dice_idx);
    return g->RemovePiece(start);
}

