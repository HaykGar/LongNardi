#include "NardiGame.h"
#include "ReaderWriter.h"

Game::Game(int rseed) : rng(rseed), dist(1, 6), dice({0, 0}), dice_used({0, 0}), 
                        doubles_rolled(false), arbiter(this), rw(nullptr) 
{
    board = { { { 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} } };
} 

Game::status_codes Game::RollDice() // important to force this only once per turn in controller, no explicit safeguard here `
{    
    dice_used[0] = 0;
    dice_used[1] = 0;
    dice[0] = dist(rng);
    dice[1] = dist(rng);
    doubles_rolled = (dice[0] == dice[1]);

    rw->AnimateDice();

    arbiter.IncrementTurnNumber();

    return arbiter.MakeForcedMovesBothDiceUsable();
}

Game::status_codes Game::TryStart(const NardiCoord& s) const
{
    return arbiter.ValidStart(s.row, s.col);
}

Game::status_codes Game::TryFinishMove(const NardiCoord& start, const NardiCoord& end) // assumes start already checked
{   // 
    auto [can_move, times_dice_used]  = arbiter.LegalMove(start.row, start.col, end.row, end.col);
    if (can_move != status_codes::SUCCESS)
        return can_move;
    else
    {
        UseDice(0, times_dice_used[0]); UseDice(1, times_dice_used[1]);
        return MakeMove(start, end);    // checks for further forced moves internally
    }
}

std::pair<Game::status_codes, NardiCoord> Game::TryMoveByDice(const NardiCoord& start, bool dice_idx)
{
    auto [result, dest]  = arbiter.CanMoveByDice(start, dice_idx);
    if(result != status_codes::SUCCESS )
        return {result, {} };
    else{
        UseDice(dice_idx);
        return { MakeMove(start, dest), dest };
    }
}

void Game::UseDice(bool idx, int n)
{
    dice_used[idx] += n;
}

bool Game::TurnOver() const
{
    return ( !arbiter.CanUseDice(0) && !arbiter.CanUseDice(1) );    // set a flag to avoid redundancies
}

Game::status_codes Game::MakeMove(const NardiCoord& start, const NardiCoord& end, bool check_requested)
{
    board[start.row][start.col] -= arbiter.GetPlayerSign();
    board[end.row][end.col] += arbiter.GetPlayerSign();

    arbiter.UpdateAvailabilitySets(start, end);

    arbiter.FlagHeadIfNeeded(start);

    // move_history.emplace(start, end, dice_used[0], dice_used[1]);

    rw->ReAnimate();

    if(check_requested)
        return arbiter.MakeForcedMoves();
    else 
        return status_codes::SUCCESS;
}

unsigned Game::Arbiter::GetDistance(bool sr, int sc, bool er, int ec) const
{
    if(sr == er)
        return ec - sc; // sc >= ec is invalid, so this will be positive if called on well-defined move
    else
        return COL - sc + ec;
}

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
            ec = COL - ec;
        else // ec > COL
            ec -= COL;

        return {!row, ec};
    }
}

NardiCoord Game::Arbiter::CoordAfterDistance(const NardiCoord& start, int d, bool player) const
{
    return CoordAfterDistance(start.row, start.col, d, player);
}


/////////////////////////
/////   Arbiter ////////
///////////////////////

Game::Arbiter::Arbiter(Game* gp) : g(gp), head_used(false), player_idx(0), turn_number({0, 0})   // player sign 50/50 ?
{
    player_sign = player_idx ? -1 : 1;
    for(int p = 0; p < 2; ++p)  // 0 is player 1, 1 is player -1
        for(int i = 0; i < 6; ++i)
            goes_idx_plusone[p][i].insert(NardiCoord(p, 0));   // "head" can move any of the numbers initially
}

Game::status_codes Game::Arbiter::ValidStart(int sr, int sc) const
{
    status_codes well_def = WellDefinedStart(sr, sc);
    if(well_def != status_codes::SUCCESS)
        return well_def;
    else if(CanMoveByDice( {sr, sc}, 0).first != status_codes::SUCCESS && 
            CanMoveByDice( {sr, sc}, 1).first != status_codes::SUCCESS )    // can't move by either dice
        return status_codes::NO_PATH;
    else
        return status_codes::SUCCESS;
}

Game::status_codes Game::Arbiter::WellDefinedStart(int sr, int sc) const
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

Game::status_codes Game::Arbiter::WellDefinedStart(const NardiCoord& start) const
{
    return WellDefinedStart(start.row, start.col);
}

// FIXME: treating no blocking tun as an occupied square... so well defined move will handle this `
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
        else if(d == g->dice[0] * 3)
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

void Game::Arbiter::UpdateAvailabilitySets(const NardiCoord start, const NardiCoord dest) // not reference since we destroy stuff
{   // only to be called within MakeMove()
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

Game::status_codes Game::Arbiter::MakeForcedMoves()
{
    bool canUse0 = CanUseDice(0);
    bool canUse1 = CanUseDice(1);
    if(canUse0 && canUse1)
        return MakeForcedMovesBothDiceUsable(); // includes doubles case
    else if (canUse0 || canUse1)
        return MakeForcedMoves_SingleDice();
    else
        return status_codes::NO_LEGAL_MOVES_LEFT;   // no dice to move by
}

Game::status_codes Game::Arbiter::MakeForcedMovesBothDiceUsable()
{
    if(g->doubles_rolled)
        return ForcedMoves_DoublesCase();
    
    // no doubles from here, no head reuse issues since we can't have made a move yet
    size_t total_options[2] = {goes_idx_plusone[player_idx][g->dice[0] - 1].size(), goes_idx_plusone[player_idx][g->dice[1] - 1].size() };
        // at this point this is just the number of ways to move in one step for both dice rolls
    if(total_options[0] > 1 && total_options[1] > 1)
        return status_codes::SUCCESS;
    else if (total_options[0] == total_options[1] && total_options[0] == 0)
        return status_codes::NO_LEGAL_MOVES_LEFT;
    else    // at least 1 non-zero, so we can move by at least one of the dice
    {
        std::unordered_set<NardiCoord> two_step_starts; // start coords for 2-step moves
        for(int i = 0; i < 2; ++i ) // populate two_step_starts with all valid start points that use both dice
        {
            for(const auto& coord : goes_idx_plusone[player_idx][g->dice[i] - 1])
            {
                if(!two_step_starts.contains(coord) && (!goes_idx_plusone[player_idx][g->dice[0] - 1].contains(coord) 
                    || !goes_idx_plusone[player_idx][g->dice[1] - 1].contains(coord) ) )  // avoiding redundancy, uninserted and missing from one set
                {
                    auto [outcome, dest] = CanMoveByDice(coord, i);    // this first step must work by construction
                    std::tie(outcome, dest) = CanMoveByDice(dest, !i, true);    // made a hypothetical move already
                    if(outcome == status_codes::SUCCESS)        // can complete double step from a new location
                    {
                        bool new_option_idx = !goes_idx_plusone[player_idx][g->dice[1] - 1].contains(coord);
                        ++total_options[new_option_idx]; 
                            // incremement options for the dice that can now move from coord with a 2step
                        if(total_options[0] > 1 && total_options[1] > 1)
                            return status_codes::SUCCESS;   // nothing forced

                        two_step_starts.insert(coord);  // total_options[i] is = two_step_starts.size() + g_idx[player_idx][dice[i] - 1].size()
                    }
                }
            }
        }   // at least 1 move is forced or impossible if not returned by now
        bool max_dice = (g->dice[1] > g->dice[0]);
        if(total_options[max_dice] == 1) // if can only play 1 dice, forced to play larger one
            return HandleForced2DiceCase(max_dice, two_step_starts);
        else if(total_options[!max_dice] == 1)
            return HandleForced2DiceCase(!max_dice, two_step_starts);
        else    // one of the moves is impossible but nothing is forced, there are multiple legal moves for the other
            return status_codes::SUCCESS;   
    }    
}

Game::status_codes Game::Arbiter::MakeForcedMoves_SingleDice()
{
    if(g->doubles_rolled)
        return ForcedMoves_DoublesCase();
    
    bool canUse0 = CanUseDice(0);
    bool canUse1 = CanUseDice(1);

    if(canUse0 || canUse1 )   // can use either dice, if true here we can only use 1 of them
        return HandleSingleDiceCase(canUse1);
    else
        return status_codes::NO_LEGAL_MOVES_LEFT;   // cannot move by either dice
}

Game::status_codes Game::Arbiter::ForcedMoves_DoublesCase()
{
    if(turn_number[player_idx] == 1 && (g->dice[0] == 4 || g->dice[0] == 6 ) )
    {
        ForceMove(head[player_idx], 0, false);  // twice from head
        ForceMove(head[player_idx], 0, false);
        if(g->dice[0] == 4 )
        {
            ForceMove({player_idx, g->dice[0]}, 0, false);      // move both resulting pieces
            ForceMove({player_idx, g->dice[0]}, 0, false);
        }
        return status_codes::NO_LEGAL_MOVES_LEFT;
    }

    int steps_left = 4 - ( (g->dice_used[0]*g->dice[0] + g->dice_used[1]*g->dice[1]) / g->dice[0] ); // steps left to complete turn
    int steps_taken = 0;    // counter, when exceeds steps_left 

    if(goes_idx_plusone[player_idx][g->dice[0] - 1].empty())    // no pieces that go
        return status_codes::NO_LEGAL_MOVES_LEFT;

    std::queue<NardiCoord> move_starts;
    for(const auto& coord : goes_idx_plusone[player_idx][g->dice[0] - 1])
    {
        if (HeadReuseIssue(coord))
            continue;   // don't use this coord

        int n_pieces = IsHead(coord) ? 1 : abs(g->board[coord.row][coord.col]); // FIXME HERE add not 4 or 6, not first move `
        
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
        int n = IsHead(start) ? 1 : abs(g->board[start.row][start.col]);    // FIRST MOVE...
        for(int i = 0; i < n; ++i)
            ForceMove(start, 0, false);
        
        move_starts.pop();
    }

    return status_codes::NO_LEGAL_MOVES_LEFT;   // impossible to have partial forcing in doubles case
}

Game::status_codes Game::Arbiter::HandleForced2DiceCase(bool dice_idx, const std::unordered_set<NardiCoord>& two_step_starts) // return no legal moves if none left after making one?
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

Game::status_codes Game::Arbiter::HandleSingleDiceCase(bool dice_idx) // no doubles here, one dice used already
{       
    if(goes_idx_plusone[player_idx][g->dice[dice_idx] - 1].empty())
        return status_codes::NO_LEGAL_MOVES_LEFT;
    else if (goes_idx_plusone[player_idx][g->dice[dice_idx] - 1].size() == 1)
    {
        NardiCoord start = *goes_idx_plusone[player_idx][g->dice[dice_idx] - 1].begin();
        if(!HeadReuseIssue(start))
            g->TryMoveByDice(start, dice_idx);
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

std::pair<Game::status_codes, NardiCoord> Game::Arbiter::CanMoveByDice(const NardiCoord& start, bool dice_idx, bool moved_hypothetically) const
{
    if(!CanUseDice(dice_idx))
        return {status_codes::DICE_USED_ALREADY, {} };
    
    NardiCoord final_dest = CalculateFinalCoords(start.row, start.col, dice_idx);
    status_codes result = WellDefinedEnd(start.row, start.col, final_dest.row, final_dest.col);

    if (result == status_codes::SUCCESS && !moved_hypothetically && PreventsTurnCompletion(start, dice_idx))
        return {status_codes::ILLEGAL_MOVE, {} };
    else
        return {result, final_dest};
}

bool Game::Arbiter::PreventsTurnCompletion(const NardiCoord& start, bool dice_idx) const // only called by MoveByDice
{
    if(!g->doubles_rolled && CanUseDice(0) && CanUseDice(1) && !StepsTwice(start))
    {       // first move checked and can use dice redundant but leaving for safety for now `
        /*
            If can move another piece (goes_idx non empty, isn't just start or can reuse start), no problem
            so problem is when goes_idx empty or only start without reuse available
        */
        bool NoReuseStart = IsHead(start) || player_sign * g->board[start.row][start.col] == 1;
        return ( goes_idx_plusone[player_idx][g->dice[!dice_idx] - 1].empty() || 
                (   goes_idx_plusone[player_idx][g->dice[!dice_idx] - 1].size() == 1 &&
                    goes_idx_plusone[player_idx][g->dice[!dice_idx] - 1].contains(start) && NoReuseStart    ) );
    }
    else
        return false;
}

bool Game::Arbiter::StepsTwice(const NardiCoord& start) const
{
    NardiCoord dest = CalculateFinalCoords(start, 0);
    dest =  CalculateFinalCoords(dest, 1);
    return (WellDefinedEnd(start, dest) == status_codes::SUCCESS);
}

NardiCoord Game::Arbiter::CalculateFinalCoords(bool sr, int sc, bool dice_idx) const
{
    int ec = sc + g->dice[dice_idx];
    if (ec < COL)
        return {sr, ec};
    else
        return { (!sr), ec - COL};
    // validity not inherently guaranteed by this computation
} 

NardiCoord Game::Arbiter::CalculateFinalCoords(const NardiCoord& start, bool dice_idx) const
{
    return CalculateFinalCoords(start.row, start.col, dice_idx);
}

bool Game::Arbiter::CanUseDice(bool idx, int n) const
{
    /*
        if doubles, new_val + dice_used[!idx] <= 4      <==>    new_val <= 4 - dice_used[!idx]
        else, new_val <= 1
    */
    int new_val = g->dice_used[idx] + n;
    return ( new_val <= 1 + g->doubles_rolled*(3 - g->dice_used[!idx]));
}

Game::status_codes Game::Arbiter::ForceMove(const NardiCoord& start, bool dice_idx, bool check_further)  // only to be called when forced
{
    NardiCoord dest = CalculateFinalCoords(start.row, start.col, dice_idx);
    g->UseDice(dice_idx);
    return g->MakeMove(start, dest, check_further);
}
