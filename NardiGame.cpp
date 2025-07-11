#include "NardiGame.h"
#include "ReaderWriter.h"

Game::Game(int rseed) : player_idx(0), rng(rseed), dist(1, 6), doubles_rolled(false), arbiter(this), rw(nullptr) 
{
    player_sign = player_idx ? -1 : 1;
    board = {  { {15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }  };
} 
// player sign will actually have it be random 50/50, need to handle dice and other objects

Game::status_codes Game::RollDice() // important to force this only once per turn in controller, no explicit safeguard here `
{
    dice_used[0] = 0;
    dice_used[1] = 0;
    dice[0] = dist(rng);
    dice[1] = dist(rng);

    doubles_rolled = (dice[0] == dice[1]);

    rw->AnimateDice();

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
    dice_used[idx] += dice[idx] * n;
}

bool Game::TurnOver() const
{
    return ( !arbiter.CanUseDice(0) && !arbiter.CanUseDice(1) );    // set a flag to avoid redundancies
}

Game::status_codes Game::MakeMove(const NardiCoord& start, const NardiCoord& end, bool check_requested)
{
    board[start.row][start.col] -= player_sign;
    board[end.row][end.col] += player_sign;

    arbiter.UpdateAvailabilitySets(start, end);

    arbiter.FlagHeadIfNeeded(start);

    move_history.emplace(start, end, dice_used[0], dice_used[1]);

    rw->ReAnimate();

    if(check_requested)
        return arbiter.MakeForcedMoves();
    else 
        return status_codes::SUCCESS;
}

// void Game::UndoMove()   // strict: no undoing after enemy dice roll. Watch for case after turn over, before enemy rolls or make even stricter. Needs safeguard or removal
// {
//     // going to be more complicated with legality dicts, FIX `
//     if(move_history.empty()) // no-op ? `
//     {
//         rw->ErrorMessage("No moves to undo");
//     }
//     else{
//         Move& lastMove = move_history.top();

//         MakeMove(lastMove.end, lastMove.start);

//         if(doubles_rolled)  // possibly dice used before this piece was moved, so we should not erase all of the progress on dice_used
//         {
//             unsigned d = arbiter.GetDistance(lastMove.start.row, lastMove.start.col, lastMove.end.row, lastMove.end.col);
//             dice_used[0] -= d;  // ok if negative, we only need the sum with dice_used[1] to match sum of (dice[0] + dice[1])*multiplier
//         }
//         else    // no doubles so only one dice used so far
//         {
//             dice_used[0] -= lastMove.m_diceUsed1;
//             dice_used[1] -= lastMove.m_diceUsed2;
//         }
//         move_history.pop();
//     }
// }

unsigned Game::Arbiter::GetDistance(bool sr, int sc, bool er, int ec) const
{
    if(sr == er)
        return ec - sc; // sc >= ec is invalid, so this will be positive if called on well-defined move
    else
        return COL - sc + ec;
}

NardiCoord Game::Arbiter::CoordAfterDistance(int row, int col, int d) const
{
    int ec = col + d;
    if(ec > 0 && ec < COL)
        return {row, ec};
    else    // bad row change in WellDefinedMove
    {
        if(ec < 0)
            ec = COL - ec;
        else // ec > COL
            ec -= COL;

        return {!row, ec};
    }
}

NardiCoord Game::Arbiter::CoordAfterDistance(const NardiCoord& start, int d) const
{
    return CoordAfterDistance(start.row, start.col, d);
}

void Game::SwitchPlayer()
{
    player_idx  = !player_idx;
    player_sign = -player_sign;
    arbiter.ResetHead();
}

/////////////////////////
/////   Arbiter ////////
///////////////////////

Game::Arbiter::Arbiter(Game* gp) : g(gp), head_used(false)
{
    head[0] = NardiCoord(0, 0);
    head[1] = NardiCoord(1, 0);
    for(int p = 0; p < 2; ++p)  // 0 is player 1, 1 is player -1
        for(int i = 0; i < 6; ++i)
            goes_idx_plusone[p][i].insert(NardiCoord(p, 0));   // "head" can move any of the numbers initially
}

Game::status_codes Game::Arbiter::ValidStart(int sr, int sc) const
{
    if(sr < 0 || sr > ROW - 1 || sc < 0 || sc > COL - 1)
        return status_codes::OUT_OF_BOUNDS;
    else if (g->player_sign * g->board[sr][sc] <= 0)
        return status_codes::START_EMPTY_OR_ENEMY;
    else if (HeadReuseIssue(sr, sc)) 
        return status_codes::HEAD_PLAYED_ALREADY;
    
    return status_codes::SUCCESS;
}

Game::status_codes Game::Arbiter::WellDefinedMove(int sr, int sc, int er, int ec) const // assumes start already checked to be valid
{   // avoid using goes_idx_plusone here, else circular reasoning
    if(er < 0 || er > ROW - 1 || ec < 0 || ec > COL - 1)
        return status_codes::OUT_OF_BOUNDS;
    
    else if (g->player_sign * g->board[er][ec] < 0)   // destination occupied by opponent
        return status_codes::DEST_ENEMY;
    
    else if(sr == er )
    {
        if (sc == ec)
            return status_codes::START_RESELECT; // unselect start in this case
        else if (sc > ec)
            return status_codes::BACKWARDS_MOVE;

        return status_codes::SUCCESS; // prevent unnecessary RowChangeCheck
    }

    else if (BadRowChange(er)) // sr != er
        return status_codes::BOARD_END_REACHED;
    
    return status_codes::SUCCESS;
}

Game::status_codes Game::Arbiter::WellDefinedMove(const NardiCoord& start, const NardiCoord& end) const // start already checked to be valid
{
    return WellDefinedMove(start.row, start.col, end.row, end.col);
}

std::pair<Game::status_codes, std::array<int, 2>> Game::Arbiter::LegalMove(int sr, int sc, int er, int ec)    
// array represents how many times each dice is used, 0 or 1 usually, in case of doubles can be up to 4
{
    status_codes well_def = WellDefinedMove(sr, sc, er, ec);
    if(well_def != status_codes::SUCCESS)
        return {well_def, {}};
    
    unsigned d = GetDistance(sr, sc, er, ec); // bool casting here is ok since move well defined

    if(d == g->dice[0]) {
        if(CanUseDice(0))
            return {status_codes::SUCCESS, {1, 0} };    // alert game that the legal move uses first dice once
       
        return {status_codes::NO_PATH_TO_DEST, {}}; // dice already used up
    }
    else if (d == g->dice[1]) {
        if(CanUseDice(1))
            return {status_codes::SUCCESS, {0, 1} };
        
        return {status_codes::NO_PATH_TO_DEST, {} }; // dice already used up
    }
    else    // can't reach with just one dice
    {
        bool both_dice_works = (d == ( g->dice[0] + g->dice[1]));   // dice one and two together make the distance

        if(both_dice_works || (g->doubles_rolled && d % g->dice[0] == 0 && d / g->dice[0] <= 4) )  // both dice enough, or doubles with d a multiple of dice[0] <= 4
        {
            status_codes step2x = LegalMove_2step(sr, sc);  // if doubles, this step updates midpoint

            if (step2x != status_codes::SUCCESS)
                return {step2x, {} };
            else if(both_dice_works)
                return {status_codes::SUCCESS, {1, 1} };
            else  // doubles with either 3-4 reps, case 1 & 2 covered earlier, step2x succesful
            { 
                int steps_needed = 0;
                status_codes final_step = status_codes::DICE_USED_ALREADY;

                if(d == 3 * g->dice[0] && CanUseDice(0, 3)) {
                    final_step = LegalMove(midpoint.row, midpoint.col, er, ec).first;
                    steps_needed = 3;
                }
                else if (d == 4 * g->dice[0] && CanUseDice(0, 4)) {
                    final_step = LegalMove_2step(midpoint.row, midpoint.col);
                    steps_needed = 4;
                }

                if(final_step == status_codes::SUCCESS)
                    return { final_step, {steps_needed, 0} };
                else
                    return { final_step, {} }; // cannot use dice or obstacle on the way
            }
        }
        else
            return {status_codes::NO_PATH_TO_DEST, {}};
    }
}

Game::status_codes Game::Arbiter::LegalMove_2step(bool sr, int sc)  
{
    NardiCoord dest1 = CalculateFinalCoords(sr, sc, 0);

    if(g->doubles_rolled) // extra check needed in this case, otherwise we are guaranteed that the endpoint is well defined already
    {   // When moving 4, the second call of this function doesn't actually use a new midpoint because both_dice_works == true
        midpoint = CalculateFinalCoords(dest1.row, dest1.col, 1);

        status_codes well_def = WellDefinedMove(sr, sc, midpoint.row, midpoint.col);
        if(well_def != status_codes::SUCCESS)
            return well_def;
    }
    // can move by first dice[0], use second dice to get to the end
    if( (LegalMove(sr, sc, dest1.row, dest1.col).first == status_codes::SUCCESS) &&  CanUseDice(1)) // dice0 used in LegalMove call
        return status_codes::SUCCESS;
    
    NardiCoord dest2 = CalculateFinalCoords(sr, sc, 1);

    if( (LegalMove(sr, sc, dest2.row, dest2.col).first == status_codes::SUCCESS) && CanUseDice(0) ) // can move by dice[1] first, dice1 used in LegalMove call
        return status_codes::SUCCESS;

    return status_codes::NO_PATH_TO_DEST;
}

bool Game::Arbiter::BadRowChange(bool er) const // guaranteed sr != er
{
    int r = g->player_sign + er; // white to row 1 (r==2) or black to row 0 (r==-1) only acceptable choices, else r==1 or 0
    return (r == 0 || r == 1);
}

void Game::Arbiter::UpdateAvailabilitySets(const NardiCoord start, const NardiCoord dest) // not reference since we destroy stuff
{   // only to be called within MakeMove()
    if (g->board[start.row][start.col] == 0) // start square vacated
    {
        for(int i = 0; i < 6; ++i)  // update player's options - can no longer move from this square
            goes_idx_plusone[g->player_idx][i].erase(start);   // no op if it wasn't in the set
      
        // update other player's options - can now move to this square
        int d = 1;
        NardiCoord coord = CoordAfterDistance(start, -d);
        status_codes can_go = WellDefinedMove(coord, start);

        while( can_go != status_codes::OUT_OF_BOUNDS  && d <= 6 )
        {
            if(can_go == status_codes::SUCCESS && g->board[coord.row][coord.col] * g->player_sign < 0) // other player occupies coord
                goes_idx_plusone[!g->player_idx][d-1].insert(coord);   // coord reaches a new empty spot at distance d

            ++d;
            coord = CoordAfterDistance(start, -d);
            can_go = WellDefinedMove(coord, start);
        }
    }
    
    if (abs(g->board[dest.row][dest.col]) == 1)  // dest newly filled, note both ifs can be true
    {
        // update player's options - can now move from this square
        int d = 1;
        NardiCoord coord = CoordAfterDistance(dest, d);
        status_codes can_go = WellDefinedMove(dest, coord);

        while( can_go != status_codes::OUT_OF_BOUNDS  && d <= 6 )
        {
            if(can_go == status_codes::SUCCESS && g->board[coord.row][coord.col] * g->player_sign >= 0) // square empty or occupied by player already
                goes_idx_plusone[g->player_idx][d-1].insert(dest);
            
            ++d;
            coord = CoordAfterDistance(dest, d);
            can_go = WellDefinedMove(dest, coord);
        }

        // update other player's options - can no longer move to this square
        d = 1;
        coord = CoordAfterDistance(start, -d);
        while(d <= 6 && WellDefinedMove(coord, start) == status_codes::SUCCESS)
        {
            if(g->board[coord.row][coord.col] * g->player_sign < 0) // other player occupies coord
                goes_idx_plusone[!g->player_idx][d-1].erase(coord);   // pieces at coord can no longer travel here

            ++d;
            coord = CoordAfterDistance(start, -d);
            can_go = WellDefinedMove(coord, start);
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
    size_t total_options[2] = {goes_idx_plusone[g->player_idx][g->dice[0] - 1].size(), goes_idx_plusone[g->player_idx][g->dice[1] - 1].size() };
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
            for(const auto& coord : goes_idx_plusone[g->player_idx][g->dice[i] - 1])
            {
                if(!two_step_starts.contains(coord) && (!goes_idx_plusone[g->player_idx][g->dice[0] - 1].contains(coord) 
                    || !goes_idx_plusone[g->player_idx][g->dice[1] - 1].contains(coord) ) )  // avoiding redundancy, uninserted and missing from one set
                {
                    auto [outcome, dest] = CanMoveByDice(coord, i);    // this first step must work by construction
                    std::tie(outcome, dest) = CanMoveByDice(dest, !i);
                    if(outcome == status_codes::SUCCESS)        // can complete double step from a new location
                    {
                        bool new_option_idx = !goes_idx_plusone[g->player_idx][g->dice[1] - 1].contains(coord);
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
    int steps_left = 4 - ( (g->dice_used[0] + g->dice_used[1]) / g->dice[0] ); // steps left to complete turn
    int steps_taken = 0;    // counter, when exceeds steps_left 

    if(goes_idx_plusone[g->player_idx][g->dice[0] - 1].empty())    // no pieces that go
        return status_codes::NO_LEGAL_MOVES_LEFT;

    std::queue<NardiCoord> move_starts;
    for(const auto& coord : goes_idx_plusone[g->player_idx][g->dice[0] - 1])
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
    if(goes_idx_plusone[g->player_idx][g->dice[dice_idx] - 1].size() == 1)  // no new 2step moves
        return ForceMove(*goes_idx_plusone[g->player_idx][g->dice[dice_idx] - 1].begin(), dice_idx); // makes other forced move as needed
    else    // only a 2step move for max dice
        return ForceMove(*two_step_starts.begin(), !dice_idx); // should always be NO_LEGAL_MOVES_LEFT
        /*
            can't make single move with dice[idx], so moves other dice then when checking forced moves it should have to move 
            again as idx will only have one option
        */
}

Game::status_codes Game::Arbiter::HandleSingleDiceCase(bool dice_idx) // no doubles here, one dice used already
{       
    if(goes_idx_plusone[g->player_idx][g->dice[dice_idx] - 1].empty())
        return status_codes::NO_LEGAL_MOVES_LEFT;
    else if (goes_idx_plusone[g->player_idx][g->dice[dice_idx] - 1].size() == 1)
    {
        NardiCoord start = *goes_idx_plusone[g->player_idx][g->dice[dice_idx] - 1].begin();
        if(!HeadReuseIssue(start))
            g->TryMoveByDice(start, dice_idx);
        return status_codes::NO_LEGAL_MOVES_LEFT;   // success or fail, no legal moves remain
    }
    else if (goes_idx_plusone[g->player_idx][g->dice[dice_idx] - 1].size() == 2 &&     // two pieces that go
            goes_idx_plusone[g->player_idx][g->dice[dice_idx] - 1].contains(head[g->player_idx]) &&    // one of which is head
            HeadReuseIssue(head[g->player_idx]))                                        // can't reuse head, so only one piece which actually goes
    {
        std::unordered_set<NardiCoord>::iterator it =  goes_idx_plusone[g->player_idx][g->dice[dice_idx] - 1].begin();
        if (IsHead(*it)) // only 2 items, either not head or next one is
            ++it;
        ForceMove(*it, dice_idx);
        return status_codes::NO_LEGAL_MOVES_LEFT;
    }
    else
        return status_codes::SUCCESS;   // not forced to move, at least two choices
}

std::pair<Game::status_codes, NardiCoord> Game::Arbiter::CanMoveByDice(const NardiCoord& start, bool dice_idx) const
{
    if(!CanUseDice(dice_idx))
        return {status_codes::DICE_USED_ALREADY, {} };
    
    NardiCoord final_dest = CalculateFinalCoords(start.row, start.col, dice_idx);
    status_codes result = WellDefinedMove(start.row, start.col, final_dest.row, final_dest.col);

    return {result, final_dest};
}

const NardiCoord& Game::Arbiter::GetMidpoint() const
{
    return midpoint;
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

bool Game::Arbiter::CanUseDice(bool idx, int n) const
{
    int new_val = g->dice_used[idx] + (n * g->dice[idx]);
    /*
    if doubles, need new_val + dice[!idx] <= 4*dice[idx] <==> new_val <= 4*dice[idx] - dice[!idx]
    else, need new_val <=  1*g->dice[idx]
    */
    return ( new_val <= (1 + 3*g->doubles_rolled)*g->dice[idx] - g->doubles_rolled * g->dice[!idx] );
}

Game::status_codes Game::Arbiter::ForceMove(const NardiCoord& start, bool dice_idx, bool check_further)  // only to be called when forced
{
    NardiCoord dest = CalculateFinalCoords(start.row, start.col, dice_idx);
    g->UseDice(dice_idx);
    return g->MakeMove(start, dest, check_further);
}

//////////////////////////
/////    Move    ////////
////////////////////////

Game::Move::Move(const NardiCoord& s, const NardiCoord& e, int d1, int d2) : 
start(s), end(e), m_diceUsed1(d1), m_diceUsed2(d2) {}
