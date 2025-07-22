#include "NardiGame.h"
#include "ReaderWriter.h"

///////////////////////////
////////   Game   ////////
/////////////////////////

///////////// Constructor /////////////

Game::Game(int rseed) : board(),  rng(rseed), dist(1, 6), dice({0, 0}), times_dice_used({0, 0}),
                        doubles_rolled(false), rw(nullptr), arbiter(this)
{ } 

Game::Game() :  board(),  rng(std::random_device{}()), dist(1, 6), dice({0, 0}), 
                times_dice_used({0, 0}), doubles_rolled(false), rw(nullptr), arbiter(this)
{}

///////////// Gameplay /////////////

status_codes Game::RollDice() // important to force this only once per turn in controller, no explicit safeguard here
{    
    SetDice(dist(rng), dist(rng));
    return OnRoll();
}
 
void Game::SetDice(int d1, int d2)
{   
    dice[0] = d1; 
    dice[1] = d2;  
    times_dice_used[0] = 0;
    times_dice_used[1] = 0;
    doubles_rolled = (dice[0] == dice[1]); 
}

status_codes Game::OnRoll()
{
    if(rw)
        rw->AnimateDice();

    return arbiter.OnRoll();
}

status_codes Game::TryStart(const NardiCoord& start) const
{
    //std::cout << "called TryStart\n";
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
    //std::cout << "\n\n start: " << start.row << ", " << start.col << "\n";
    //std::cout << "dice: " << dice[dice_idx] << "\n";
    //std::cout << "other dice: " << dice[!dice_idx] << "\n";
    
    if(board.CurrPlayerInEndgame() && arbiter.CanRemovePiece(start, dice_idx))
    {
        UseDice(dice_idx);
        RemovePiece(start);
    }
    
    auto [result, dest]  = arbiter.CanMoveByDice(start, dice_idx);
    
    //std::cout << "res: \n";
    DispErrorCode(result);

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

    if(rw)
        rw->ReAnimate();

    return arbiter.OnMove(start, end);
}

status_codes Game::RemovePiece(const NardiCoord& start)
{
    board.Remove(start);
    if(rw)
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
{ }

///////////// Legality /////////////

std::pair<status_codes, NardiCoord> Game::Arbiter::CanMoveByDice(const NardiCoord& start, bool dice_idx) const
{
    if(!CanUseDice(dice_idx))
        return {status_codes::DICE_USED_ALREADY, {} };

    //std::cout << "dice usable\n";

    NardiCoord final_dest = g->board.CoordAfterDistance(start, g->dice[dice_idx]);
    status_codes result = g->board.WellDefinedEnd(start, final_dest);

    //std::cout << "board legal?\n";
    // DispErrorCode(result);
    
    if (result == status_codes::SUCCESS && PreventsTurnCompletion(start, dice_idx))
    {    
        //std::cout << "bad prevention\n";
        return {status_codes::PREVENTS_COMPLETION, {} };
    }
    else
        return {result, final_dest};
}

bool Game::Arbiter::CanUseDice(bool idx, int n) const
{
    int new_val = g->times_dice_used[idx] + n;
    return ( new_val <= 1 + g->doubles_rolled*(3 - g->times_dice_used[!idx]));
}

bool Game::Arbiter::PreventsTurnCompletion(const NardiCoord& start, bool dice_idx) const // only called by MoveByDice
{
    // //std::cout << "doub roll: " << std::boolalpha << g->doubles_rolled << "\n";
    // //std::cout << "dices usable: " << std::boolalpha << (CanUseDice(0) && CanUseDice(1)) << "\n";
    // //std::cout << "options: " << min_options[0] << " " << min_options[1] << "\n";
    // //std::cout << "steps twice: " << std::boolalpha << (MakesSecondStep(start)) << "\n";
    // //std::cout << "contains start: " << std::boolalpha << (PlayerGoesByDice(!dice_idx).contains(start)) << "\n";
    // //std::cout << "only 1 went: " << std::boolalpha << (PlayerGoesByDice(!dice_idx).size() == 1) << "\n"; 
        
    // //std::cout << "\n\n";
    // for(const auto& coord : PlayerGoesByDice(!dice_idx))
    // {
    //     //std::cout << coord.row << ", " << coord.col << " goes by " << g->dice[!dice_idx] << "\n";
    // }
    // //std::cout << "\n\n";
    
    // //std::cout << "no piece went by other: " << std::boolalpha << (PlayerGoesByDice(!dice_idx).empty()) << "\n"; 
    // //std::cout << "only 1 movable at start: " << std::boolalpha << (g->board.MovablePieces(start) == 1) << "\n"; 

    // //std::cout << "return value " << (
    //     !g->doubles_rolled && 
    //     CanUseDice(0) && CanUseDice(1) && 
    //     min_options[0] >= 1 && min_options[1] >= 1  &&  // could play both before
    //     !MakesSecondStep(start) &&                      // cannot continue with this piece
    //     ( 
    //         PlayerGoesByDice(!dice_idx).empty() ||      // no pieces move first by other dice
    //         (
    //             PlayerGoesByDice(!dice_idx).size() == 1 && 
    //             PlayerGoesByDice(!dice_idx).contains(start) && 
    //             g->board.MovablePieces(start) == 1
    //         )   // moving start prevents moving any other piece by other dice
    //     )   
    // ) << "\n"; // expect true here
    // //std::cout << "\n\n\n\n";


    
    return 
    (
        !g->doubles_rolled && 
        CanUseDice(0) && CanUseDice(1) && 
        min_options[0] >= 1 && min_options[1] >= 1  &&  // could play both before
        !MakesSecondStep(start) &&                      // cannot continue with this piece
        ( 
            PlayerGoesByDice(!dice_idx).empty() ||      // no pieces move first by other dice
            (
                PlayerGoesByDice(!dice_idx).size() == 1 && 
                PlayerGoesByDice(!dice_idx).contains(start) && 
                g->board.MovablePieces(start) == 1
            )   // moving start prevents moving any other piece by other dice
        )   
    );
}

bool Game::Arbiter::MakesSecondStep(const NardiCoord& start) const
{
    return (g->board.WellDefinedEnd (
                start, 
                g->board.CoordAfterDistance(start, g->dice[0] + g->dice[1]) 
            ) == status_codes::SUCCESS  );
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
    unsigned d = g->board.GetDistance(start, end);

    if(d == g->dice[0])
        return {CanMoveByDice(start, 0).first, {1, 0} };
    else if (d == g->dice[1]) { //std::cout << "moving by dice val: " << g->dice[1] << "\n";
        return {CanMoveByDice(start, 1).first, {0, 1} }; }
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
    if(!CanUseDice(0) || !CanUseDice(1))
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

status_codes Game::Arbiter::OnRoll()
{
    IncrementTurnNumber();
    return CheckForcedMoves();
}

status_codes Game::Arbiter::OnMove(const NardiCoord& start, const NardiCoord& end)
{
    return CheckForcedMoves();
}


status_codes Game::Arbiter::OnRemoval(const NardiCoord& start)
{
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


    min_options = { PlayerGoesByDice(0).size(), 
                    PlayerGoesByDice(1).size() };
    bool max_dice = (g->dice[1] > g->dice[0]);

    if(g->board.CurrPlayerInEndgame() && g->dice[max_dice] >= g->board.MaxNumOcc().at( g->board.PlayerIdx() ) )
        return ForceRemovePiece({!g->board.PlayerIdx(), COL - g->board.MaxNumOcc().at( g->board.PlayerIdx() )}, max_dice);
    
    // no doubles, no head reuse issues since we can't have made a move yet
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
            NardiCoord start = *PlayerGoesByDice(0).begin();
            if(start == *PlayerGoesByDice(1).begin() && 
                g->board.MovablePieces(start) == 1 )
                return HandleForced1Dice(max_dice); // Will make other forced move if possible
        }
    }
    // eliminated 0-0, 0-1, and 1-1 with 1 piece cases, options[more_options] > 1 or they're both 1
    bool more_options = min_options[1] > min_options[0];
    // iterate through coords for dice with more options, try to find a 2step move
    std::stack<NardiCoord> two_step_starts; // start coords for 2-step moves
    for(const auto& coord : PlayerGoesByDice(more_options))    
    {
        auto [can_go, _] = LegalMove_2step(coord);
        if(can_go == status_codes::SUCCESS)
        {
            if(min_options[more_options] == 1)    // case both only move from 1 square w/ multiple pieces
                ++min_options[more_options];
            
            ++min_options[!more_options];
            if(min_options[0] > 1 && min_options[1] > 1)
                return status_codes::SUCCESS;
            two_step_starts.push(coord);
        }
    }
    if(min_options[max_dice] == 1) // if can only play 1 dice, forced to play larger one
        return HandleForced2Dice(max_dice, two_step_starts);
    else if(min_options[!max_dice] == 1)
        return HandleForced2Dice(!max_dice, two_step_starts);
    else    // one of the moves is impossible but nothing is forced, there are multiple legal moves for the other
        return status_codes::SUCCESS;   
}

status_codes Game::Arbiter::HandleForced2Dice(bool dice_idx, const std::stack<NardiCoord>& two_step_starts) // return no legal moves if none left after making one?
{
    if(PlayerGoesByDice(dice_idx).size() == 1)  // no new 2step moves
        return ForceMove(*PlayerGoesByDice(dice_idx).begin(), dice_idx); // makes other forced move as needed
    else    // only a 2step move for max dice
    {
        NardiCoord start = two_step_starts.top();
        return ForceMove(start, !dice_idx); // should always be NO_LEGAL_MOVES_LEFT
    }
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
    if(PlayerGoesByDice(dice_idx).empty())
        return status_codes::NO_LEGAL_MOVES_LEFT;
    else if (PlayerGoesByDice(dice_idx).size() == 1)
    {
        NardiCoord start = *PlayerGoesByDice(dice_idx).begin();
        if(!g->board.HeadReuseIssue(start))
            return ForceMove(start, dice_idx);
        else
            return status_codes::NO_LEGAL_MOVES_LEFT;   // success or fail, no legal moves remain
    }
    else if (PlayerGoesByDice(dice_idx).size() == 2 &&     // two pieces that go
            PlayerGoesByDice(dice_idx).contains( PlayerHead() ) &&    // one of which is head
            g->board.HeadUsed()  )                                        // can't reuse head, so only one piece which actually goes
    {
        std::unordered_set<NardiCoord>::iterator it =  PlayerGoesByDice(dice_idx).begin();
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
        if( PlayerGoesByDice(0).empty() ||
            ( PlayerGoesByDice(0).size() == 1 &&
            g->board.HeadReuseIssue(*PlayerGoesByDice(0).begin()) ) )
        {
            forcing_doubles = false;
            return status_codes::NO_LEGAL_MOVES_LEFT;
        }
        else 
            return ForceMove(*PlayerGoesByDice(0).begin(), 0);   // will recurse until empty
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

    if(PlayerGoesByDice(0).empty())    // no pieces that go
        return status_codes::NO_LEGAL_MOVES_LEFT;

    for(const auto& coord : PlayerGoesByDice(0))
    {
        if (g->board.HeadReuseIssue(coord))
            continue;   // don't use this coord

        int n_pieces = g->board.MovablePieces(coord);
        
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
    if( PlayerGoesByDice(0).empty() ||
            ( PlayerGoesByDice(0).size() == 1 &&
            g->board.HeadReuseIssue(*PlayerGoesByDice(0).begin()) ) )
    {
        forcing_doubles = false;
        return status_codes::NO_LEGAL_MOVES_LEFT;
    }
    else
    { 
        forcing_doubles = true;
        return ForceMove(*PlayerGoesByDice(0).begin(), 0);   // will recurse until empty
    }

}

void Game::Arbiter::Force_1stMoveException()
{
    int dist = g->dice[0] * (1 + (g->dice[0] == 4) );    // 8 if double 4, else 6
    NardiCoord head(g->board.PlayerIdx(), 0);

    NardiCoord dest(head.row, dist);

    g->board.Move(head, dest);
    g->board.Move(head, dest);

    if(g->rw)
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