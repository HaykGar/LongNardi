#include "NardiGame.h"
#include "ReaderWriter.h"

///////////////////////////
////////   Game   ////////
/////////////////////////

///////////// Constructor /////////////

Game::Game(int rseed) : board(),  rng(rseed), dist(1, 6), dice({0, 0}), times_dice_used({0, 0}),
                        doubles_rolled(false), rw(nullptr), arbiter(this)
{ } 

///////////// Gameplay /////////////

status_codes Game::RollDice() // important to force this only once per turn in controller, no explicit safeguard here
{    
    board.SwitchPlayer();
    times_dice_used[0] = 0;
    times_dice_used[1] = 0;
    dice[0] = dist(rng);
    dice[1] = dist(rng);
    doubles_rolled = (dice[0] == dice[1]);

    rw->AnimateDice();

    return arbiter.OnRoll();
}

status_codes Game::TryStart(const NardiCoord& start) const
{
    // During endgame, if it's a valid start just check if there's a forced move from here, will streamline play
    return board.ValidStart(start);
}

status_codes Game::TryFinishMove(const NardiCoord& start, const NardiCoord& end) // assumes start already checked
{   
    auto [can_move, times_used]  = arbiter.LegalMove(start, end);
    if (can_move != status_codes::SUCCESS)
        return can_move;
    else
    {
        UseDice(0, times_used[0]); UseDice(1, times_used[1]);
        return MakeMove(start, end);    // checks for further forced moves internally
    }
}

status_codes Game::TryMoveByDice(const NardiCoord& start, bool dice_idx)
{
    if(board.CurrPlayerInEndgame() && arbiter.CanRemovePiece(start, dice_idx))
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

status_codes Game::MakeMove(const NardiCoord& start, const NardiCoord& end)
{
    board.Move(start, end);

    rw->ReAnimate();

    return arbiter.OnMove(start, end);
}

status_codes Game::RemovePiece(const NardiCoord& start)
{
    board.Remove(start);
    rw->ReAnimate();
    if(GameIsOver())
        return status_codes::NO_LEGAL_MOVES_LEFT;
    
    return arbiter.OnRemoval(start);
}

////////////////////////////
////////   Arbiter ////////
//////////////////////////

///////////// Constructor /////////////

Game::Arbiter::Arbiter(Game* gp) : g(gp), turn_number({0, 0}), forcing_doubles(false)   // player sign 50/50 ?
{
    for(int p = 0; p < 2; ++p)  // 0 is player 1, 1 is player -1
        for(int i = 0; i < 6; ++i)
            goes_idx_plusone[p][i].insert(NardiCoord(p, 0));   // "head" can move any of the numbers initially
}

///////////// Legality /////////////

std::pair<status_codes, NardiCoord> Game::Arbiter::CanMoveByDice(const NardiCoord& start, bool dice_idx) const
{
    if(!CanUseDice(dice_idx))
        return {status_codes::DICE_USED_ALREADY, {} };

    NardiCoord final_dest = g->board.CoordAfterDistance(start, g->dice[dice_idx]);
    status_codes result = g->board.WellDefinedEnd(start, final_dest);

    if (result == status_codes::SUCCESS && PreventsTurnCompletion(start, dice_idx))
        return {status_codes::PREVENTS_COMPLETION, {} };
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
            && goes_idx_plusone[g->board.PlayerIdx()][g->dice[!dice_idx] - 1].empty() && !StepsTwice(start));
        /* 
            no doubles, both dice useable, each dice had options at the start of turn, the 
            other dice can only play via a 2step, no 2steps from here
        */
}

bool Game::Arbiter::StepsTwice(const NardiCoord& start) const
{
    NardiCoord dest = g->board.CoordAfterDistance(start, g->dice[0] + g->dice[1]);
    return (g->board.WellDefinedEnd(start, dest) == status_codes::SUCCESS);
}

bool Game::Arbiter::CanRemovePiece(const NardiCoord& start, bool dice_idx)
{
    if(!CanUseDice(dice_idx))
        return false;
    
    int pos_from_end = COL - start.col;
    return (pos_from_end == g->dice[dice_idx] ||  // dice val exactly
            (pos_from_end == g->board.MaxNumOcc().at(g->board.PlayerIdx()) && g->dice[dice_idx] > pos_from_end) );  // largest available, less than dice
}

std::pair<status_codes, std::array<int, 2>> Game::Arbiter::LegalMove(const NardiCoord& start, const NardiCoord& end)    
// array represents how many times each dice is used, 0 or 1 usually, in case of doubles can be up to 4
{    
    unsigned d = g->board.GetDistance(start, end); // bool casting here is ok since move well defined

    if(d == g->dice[0])
        return {CanMoveByDice(start, 0).first, {1, 0} };
    else if (d == g->dice[1]) 
        return {CanMoveByDice(start, 1).first, {0, 1} };
    else if (d == g->dice[0] + g->dice[1])
        return {LegalMove_2step(start).first, {1, 1}};
    else if ( g->doubles_rolled && (d % g->dice[0] == 0)  )
    {
        if(!CanUseDice(0, d / g->dice[0]) )
            return { status_codes::DICE_USED_ALREADY, {} };
        
        auto [step2_status, step2_dest] = LegalMove_2step(start); 
        if(step2_status != status_codes::SUCCESS)
            return { status_codes::NO_PATH, {} };
        else if( d == g->dice[0] * 3)
            return { CanMoveByDice(step2_dest, 0).first, {3, 0} };
        else if(d == g->dice[0] * 4)
            return { LegalMove_2step(step2_dest).first, {4, 0} };
    }
    
    return {status_codes::NO_PATH, {}};
}

std::pair<status_codes, NardiCoord> Game::Arbiter::LegalMove_2step(const NardiCoord& start)  
{
    if(!CanUseDice(0) || !CanUseDice(1))        // redundant but safer
        return {status_codes::DICE_USED_ALREADY, {} };

    bool first_dice = 0;
    auto [status, mid] = CanMoveByDice( start, first_dice);
    if(status != status_codes::SUCCESS && !g->doubles_rolled){
        std::tie(status, mid) = CanMoveByDice( start, 1);    
        first_dice = !first_dice;   // try both dice to get to a midpoint, no need if doubles
    }

    if(status == status_codes::SUCCESS)
    {
        auto [outcome, dest] = CanMoveByDice(mid, !first_dice);
        if(outcome == status_codes::SUCCESS || outcome == status_codes::PREVENTS_COMPLETION)
            return { status_codes::SUCCESS, dest };
        else
            return { outcome, dest };
    }
    else
        return {status_codes::NO_PATH, {} };    // unable to reach midpoint
}

///////////// Updates and Actions /////////////

void Game::Arbiter::UpdateAvailabilitySets(const NardiCoord start, const NardiCoord dest) // not reference since we destroy stuff
{   // only to be called within MakeMove()

    UpdateAvailabilitySets(start);
    
    if (abs(g->board.at(dest)) == 1)  // dest newly filled, note both ifs can be true
    {
        // update player's options - can now possibly move from dest square
        int d = 1;
        NardiCoord coord = g->board.CoordAfterDistance(dest, d);
        while( !coord.OutOfBounds() && d <= 6 )
        {
            if(g->board.at(coord) * g->board.PlayerSign() >= 0) // square empty or occupied by player already
                goes_idx_plusone[g->board.PlayerIdx()][d-1].insert(dest);
            
            ++d;
            coord = g->board.CoordAfterDistance(dest, d);
        }
        // update other player's options - can no longer move to dest square
        d = 1;
        coord = g->board.CoordAfterDistance(dest, -d, !g->board.PlayerIdx());
        while( !coord.OutOfBounds() && d <= 6 )
        {
            if(g->board.at(coord) * g->board.PlayerSign() < 0) // other player occupies coord
                goes_idx_plusone[!g->board.PlayerIdx()][d-1].erase(coord);   // pieces at coord can no longer travel here

            ++d;
            coord = g->board.CoordAfterDistance(dest, -d, !g->board.PlayerIdx());
        }
    }
}

void Game::Arbiter::UpdateAvailabilitySets(const NardiCoord start)
{
    if (g->board.at(start) == 0) // start square vacated
    {
        for(int i = 0; i < 6; ++i)  // update player's options - can no longer move from this square
            goes_idx_plusone[g->board.PlayerIdx()][i].erase(start);   // no op if it wasn't in the set
        
        // update other player's options - can now move to this square
        int d = 1;
        NardiCoord coord = g->board.CoordAfterDistance(start, -d, !g->board.PlayerIdx());

        while( !coord.OutOfBounds()  && d <= 6 )
        {
            if(g->board.at(coord) * g->board.PlayerSign() < 0) // other player occupies coord
                goes_idx_plusone[!g->board.PlayerIdx()][d-1].insert(coord);   // coord reaches a new empty spot at start with distance d

            ++d;
            coord = g->board.CoordAfterDistance(start, -d, !g->board.PlayerIdx());
        }
    }

    if(g->board.CurrPlayerInEndgame())
    {
        for(int i = g->board.MaxNumOcc().at(g->board.PlayerIdx()); i <= 6; ++i)
            goes_idx_plusone[g->board.PlayerIdx()][i-1].insert({!g->board.PlayerIdx(), COL - g->board.MaxNumOcc().at(g->board.PlayerIdx())}); 

        for(int i = 1; i < g->board.MaxNumOcc().at(g->board.PlayerIdx()); ++i)
        {
            if(g->board.at(!g->board.PlayerIdx(), COL - i) * g->board.PlayerSign()  > 0)
                goes_idx_plusone[g->board.PlayerIdx()][i-1].insert({!g->board.PlayerIdx(), COL - i});
        }
    }
}

status_codes Game::Arbiter::OnRoll()
{
    IncrementTurnNumber();
    return CheckForcedMoves();
}

status_codes Game::Arbiter::OnMove(const NardiCoord& start, const NardiCoord& end)
{
    UpdateAvailabilitySets(start, end);    
    return CheckForcedMoves();
}


status_codes Game::Arbiter::OnRemoval(const NardiCoord& start)
{
    // SetMaxOcc();
    UpdateAvailabilitySets(start);

    return CheckForcedMoves();
}

///////////// Forced Moves /////////////

status_codes Game::Arbiter::CheckForcedMoves()
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

status_codes Game::Arbiter::CheckForced_2Dice()
{   
    min_options = {goes_idx_plusone[g->board.PlayerIdx()][g->dice[0] - 1].size(), goes_idx_plusone[g->board.PlayerIdx()][g->dice[1] - 1].size() };
    bool max_dice = (g->dice[1] > g->dice[0]);

    if(g->board.CurrPlayerInEndgame() && g->dice[max_dice] >= g->board.MaxNumOcc().at( g->board.PlayerIdx() ) )
        return ForceRemovePiece({!g->board.PlayerIdx(), COL - g->board.MaxNumOcc().at( g->board.PlayerIdx() )}, max_dice);
    
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
            NardiCoord start = *goes_idx_plusone[g->board.PlayerIdx()][g->dice[0] - 1].begin();
            if(start == *goes_idx_plusone[g->board.PlayerIdx()][g->dice[1] - 1].begin() && 
                (g->board.at(start) * g->board.PlayerSign() == 1 || g->board.IsPlayerHead(start)) )
                return HandleForced1Dice(max_dice); // Will make other forced move if possible
        }
    }
    // eliminated 0-0, 0-1, and 1-1 with 1 piece cases, options[more_options] > 1 or they're both 1
    bool more_options = min_options[1] > min_options[0];
    // iterate through coords for dice with more options, try to find a 2step move
    std::unordered_set<NardiCoord> two_step_starts; // start coords for 2-step moves
    for(const auto& coord : goes_idx_plusone[g->board.PlayerIdx()][g->dice[more_options] - 1])    
    {
        auto [can_go, _] = LegalMove_2step(coord);
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

status_codes Game::Arbiter::HandleForced2Dice(bool dice_idx, const std::unordered_set<NardiCoord>& two_step_starts) // return no legal moves if none left after making one?
{
    if(goes_idx_plusone[g->board.PlayerIdx()][g->dice[dice_idx] - 1].size() == 1)  // no new 2step moves
        return ForceMove(*goes_idx_plusone[g->board.PlayerIdx()][g->dice[dice_idx] - 1].begin(), dice_idx); // makes other forced move as needed
    else    // only a 2step move for max dice
        return ForceMove(*two_step_starts.begin(), !dice_idx); // should always be NO_LEGAL_MOVES_LEFT
        /*
            can't make single move with dice[idx], so moves other dice then when checking forced moves it should have to move 
            again as idx will only have one option
        */
}

status_codes Game::Arbiter::CheckForced_1Dice()
{
    bool active_dice = CanUseDice(1);
    if (g->board.CurrPlayerInEndgame() && (g->dice[active_dice] >= g->board.MaxNumOcc().at( g->board.PlayerIdx() ) ) )
        return ForceRemovePiece({!g->board.PlayerIdx(), COL - g->board.MaxNumOcc().at( g->board.PlayerIdx() )}, active_dice);
    
    return HandleForced1Dice(active_dice);
}

status_codes Game::Arbiter::HandleForced1Dice(bool dice_idx) // no doubles here, one dice used already
{       
    if(goes_idx_plusone[g->board.PlayerIdx()][g->dice[dice_idx] - 1].empty())
        return status_codes::NO_LEGAL_MOVES_LEFT;
    else if (goes_idx_plusone[g->board.PlayerIdx()][g->dice[dice_idx] - 1].size() == 1)
    {
        NardiCoord start = *goes_idx_plusone[g->board.PlayerIdx()][g->dice[dice_idx] - 1].begin();
        if(!g->board.HeadReuseIssue(start))
            return ForceMove(start, dice_idx);
        else
            return status_codes::NO_LEGAL_MOVES_LEFT;   // success or fail, no legal moves remain
    }
    else if (goes_idx_plusone[g->board.PlayerIdx()][g->dice[dice_idx] - 1].size() == 2 &&     // two pieces that go
            goes_idx_plusone[g->board.PlayerIdx()][g->dice[dice_idx] - 1].contains( PlayerHead() ) &&    // one of which is head
            g->board.HeadUsed()  )                                        // can't reuse head, so only one piece which actually goes
    {
        std::unordered_set<NardiCoord>::iterator it =  goes_idx_plusone[g->board.PlayerIdx()][g->dice[dice_idx] - 1].begin();
        if (g->board.IsPlayerHead(*it)) // only 2 items, either not head or next one is
            ++it;
        ForceMove(*it, dice_idx);
        return status_codes::NO_LEGAL_MOVES_LEFT;
    }
    else
        return status_codes::SUCCESS;   // not forced to move, at least two choices
}

status_codes Game::Arbiter::CheckForced_Doubles()
{

    if(forcing_doubles)
    {
        if( goes_idx_plusone[g->board.PlayerIdx()][g->dice[0] - 1].empty() ||
            ( goes_idx_plusone[g->board.PlayerIdx()][g->dice[0] - 1].size() == 1 &&
            g->board.HeadReuseIssue(*goes_idx_plusone[g->board.PlayerIdx()][g->dice[0] - 1].begin()) ) )
        {
            forcing_doubles = false;
            return status_codes::NO_LEGAL_MOVES_LEFT;
        }
        else 
            return ForceMove(*goes_idx_plusone[g->board.PlayerIdx()][g->dice[0] - 1].begin(), 0);   // will recurse until empty
    }
    else if (g->times_dice_used[0] + g->times_dice_used[1] > 0) // if no forced on roll, no forced after
    {
        if(g->times_dice_used[0] + g->times_dice_used[1] == 4)
            return status_codes::NO_LEGAL_MOVES_LEFT;
        else
            return status_codes::SUCCESS;
    }    

    if( turn_number[g->board.PlayerIdx()] == 1 && (g->dice[0] == 4 || g->dice[0] == 6 ) )  // first move double 4 or 6
    {
        Force_1stMoveException();
        return status_codes::NO_LEGAL_MOVES_LEFT;
    }

    int steps_left = 4; // 4 steps left to complete turn by earlier if statement
    int steps_taken = 0;

    if(goes_idx_plusone[g->board.PlayerIdx()][g->dice[0] - 1].empty())    // no pieces that go
        return status_codes::NO_LEGAL_MOVES_LEFT;

    for(const auto& coord : goes_idx_plusone[g->board.PlayerIdx()][g->dice[0] - 1])
    {
        if (g->board.HeadReuseIssue(coord))
            continue;   // don't use this coord

        int n_pieces = g->board.IsPlayerHead(coord) ? 1 : abs(g->board.at(coord));
        
        NardiCoord start = coord;
        auto [canGo, dest] = CanMoveByDice(start, 0);

        while(canGo == status_codes::SUCCESS)   // runs at most 4 times without returning
        {
            steps_taken += n_pieces;

            if(steps_taken > steps_left)            
                return status_codes::SUCCESS;

            start = dest;
            std::tie(canGo, dest) = CanMoveByDice(start, 0);
        }
    }  
    // if we exit loop, then the moves are forced
    if( goes_idx_plusone[g->board.PlayerIdx()][g->dice[0] - 1].empty() ||
            ( goes_idx_plusone[g->board.PlayerIdx()][g->dice[0] - 1].size() == 1 &&
            g->board.HeadReuseIssue(*goes_idx_plusone[g->board.PlayerIdx()][g->dice[0] - 1].begin()) ) )
    {
        forcing_doubles = false;
        return status_codes::NO_LEGAL_MOVES_LEFT;
    }
    else
    { 
        forcing_doubles = true;
        return ForceMove(*goes_idx_plusone[g->board.PlayerIdx()][g->dice[0] - 1].begin(), 0);   // will recurse until empty
    }

}

void Game::Arbiter::Force_1stMoveException()
{
    int dist = g->dice[0] * (1 + (g->dice[0] == 4) );    // 8 if double 4, else 6
    NardiCoord head(g->board.PlayerIdx(), 0);

    NardiCoord dest(head.row, dist);

    g->board.Move(head, dest);
    g->board.Move(head, dest);

    NardiCoord to;
    NardiCoord from;

    for(int i = 0; i < 6; ++i)
    {
        to = g->board.CoordAfterDistance(dest, i+1);
        if(g->board.at(to) == 0 )
            goes_idx_plusone[g->board.PlayerIdx()][i].insert(dest);

        from = g->board.CoordAfterDistance(dest, -(i+1));
        if(g->board.at(from) * g->board.PlayerSign() < 0)
            goes_idx_plusone[!g->board.PlayerIdx()][i].erase(from);
    }

    g->rw->ReAnimate(); 
}

status_codes Game::Arbiter::ForceMove(const NardiCoord& start, bool dice_idx)  // only to be called when forced
{
    NardiCoord dest =  g->board.CoordAfterDistance(start, g->dice[dice_idx]);
    g->UseDice(dice_idx);
    return g->MakeMove(start, dest);
}

status_codes Game::Arbiter::ForceRemovePiece(const NardiCoord& start, bool dice_idx)
{
    g->UseDice(dice_idx);
    return g->RemovePiece(start);
}