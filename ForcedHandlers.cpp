#include "NardiGame.h"

/////////////////////////////////////
////////   ForcedHandlers   ////////
///////////////////////////////////

///////////// Base Class /////////////

Game::ForcedHandler::ForcedHandler(Game& g, Arbiter& a) : _arb(a), _g(g) {}

///////////// Doubles /////////////

Game::DoublesHandler::DoublesHandler(Game& g, Arbiter& a) : ForcedHandler(g, a), first_move_checker(g, a) {}

bool Game::DoublesHandler::Is()
{
    if(_g.board.MisMatch()){
        std::cerr << "!!!!\nmock and board improperly updated, attempted force on mismatched boards - doubles\n!!!!\n";
        return false;
    }
    return (_g.dice[0] == _g.dice[1]);
}

status_codes Game::DoublesHandler::Check()
{
    std::cout << "doubles is\n";

    if(!Is())
        return status_codes::MISC_FAILURE;

    if( first_move_checker.PreConditions() )  // first move double 4 or 6
        return first_move_checker.MakeForced();

    steps_left = 4 - (_g.times_mockdice_used.at(0) + _g.times_mockdice_used.at(1));

    std::cout << "steps left: " << steps_left << "\n";

    auto movables = _arb.GetMovables(0);
    std::cout << "num movables: " << movables.size() << "\n";

    for(int i = 0; i < movables.size(); ++i)
    {
        if(steps_left <= 0)         // previous iteration mocked through to the end, this one can mock further
        {
            _g.ResetMock();
            return status_codes::SUCCESS;
        }
         
        // std::cout << "i = " << i << "\n";
        MockFrom(movables.at(i));   // updates steps_left
    }
    // steps left >= 0, need to make all forcing moves
    _g.RealizeMock();

    return status_codes::NO_LEGAL_MOVES_LEFT;
}

void Game::DoublesHandler::MockFrom(NardiCoord start)
{
    // std::cout << "mocking from: " << start.row << ", " << start.col << "\n";

    auto [can_go, dest] = _arb.CanMoveByDice(start, 0);
    // std::cout << "can go: " ;
    // DispErorrCode(can_go);

    while(can_go == status_codes::SUCCESS)
    {
        --steps_left;
        // std::cout << "can move from " << start.row << ", " << start.col << " to " << dest.row << ", " << dest.col << "\n";

        if(steps_left < 0)
            return;   // nothing forced
        
        // mock move
        ++_g.times_mockdice_used[_g.board.PlayerIdx()];
        _g.MockMove(start, dest);

        // updatge start and dest
        start = dest;
        std::tie(can_go, dest) = _arb.CanFinishByDice(start, 0);
    }
}

///////////// Single Dice /////////////

Game::SingleDiceHandler::SingleDiceHandler(Game& g, Arbiter& a) : ForcedHandler(g, a) {}

bool Game::SingleDiceHandler::Is()
{
    if(_g.board.MisMatch()){
        std::cerr << "!!!!\nmock and board improperly updated, attempted force on mismatched boards - singledice\n!!!!\n";
        return false;
    }
    bool canUse[2] = { _arb.CanUseMockDice(0), _arb.CanUseMockDice(1) };

    // std::cout << "usability of dice: " << canUse[0] << " " <<  canUse[1] << "\n";

    return (canUse[0] + canUse[1] == 1 );  // can use 1 but NOT both dice
}

status_codes Game::SingleDiceHandler::Check()
{
    // std::cout << "1 dice is\n";

    bool active_dice = _arb.CanUseMockDice(1);
    return ForceFromDice(active_dice);
}

status_codes Game::SingleDiceHandler::ForceFromDice(bool active_dice)
{
    auto movables = _arb.GetMovables(active_dice);
    // std::cout << "movables size " << movables.size() << "\n";
    if(movables.size() == 0)
        return status_codes::NO_LEGAL_MOVES_LEFT;
    else if(movables.size() == 1)
        return _g.ForceMove(movables.at(0), active_dice);
    else
        return status_codes::SUCCESS;   // not forced to move, at least two choices
}

///////////// Two Dice /////////////

Game::TwoDiceHandler::TwoDiceHandler(Game& g, Arbiter& a) : ForcedHandler(g, a) {}

bool Game::TwoDiceHandler::Is()
{
    if(_g.board.MisMatch()){
        std::cerr << "!!!!\nmock and board improperly updated, attempted force on mismatched boards - twodice\n!!!!\n";
        return false;
    }
    return (_arb.CanUseMockDice(0) &&  _arb.CanUseMockDice(1));
}

status_codes Game::TwoDiceHandler::Check()
{       // no doubles, no head reuse issues since we can't have made a move yet
    // std::cout << "2 dice: " << _g.dice[0] << ", " << _g.dice[1] << " \n";

    bool max_dice = (_g.dice[1] > _g.dice[0]);

    std::array< std::vector<NardiCoord>, 2 > dice_movables = { _arb.GetMovables(0), _arb.GetMovables(1) };

    // std::cout << "options: " << dice_movables.at(0).size() << " " << dice_movables.at(1).size() << "\n";

    if (dice_movables.at(0).size() > 1 && dice_movables.at(1).size() > 1)       // nothing forced
        return status_codes::SUCCESS;
    
    else if(dice_movables.at(0).size() + dice_movables.at(1).size() == 0)         // neither have any options,  0-0
        return status_codes::NO_LEGAL_MOVES_LEFT;

    else if( dice_movables.at(0).size() + dice_movables.at(1).size() == 1 )       // one has no options, other has only 1,  0-1
    {
        bool moving_by = dice_movables.at(1).size() == 1;
        return _g.ForceMove(dice_movables.at(moving_by).at(0), moving_by);
    }
    
    else if( dice_movables.at(0).size() == 1 && dice_movables.at(1).size() == 1)
    {
        NardiCoord max_start = dice_movables.at(max_dice).at(0);
        NardiCoord min_start = dice_movables.at(!max_dice).at(0);

        // std::cout << "moving from " << max_start.AsStr() << " and " << min_start.AsStr() << "\n";
        // std::cout << "movable pieces each: " << _g.board.Mock_MovablePieces(max_start) << " and " <<_g.board.Mock_MovablePieces(min_start) << "\n";

        if( min_start == max_start && _g.board.Mock_MovablePieces(max_start) == 1 )
            return _g.ForceMove(max_start, max_dice);
    }

    // std::cout << "need to get two steppers\n";

    std::unordered_set<NardiCoord> two_steppers = _arb.GetTwoSteppers(2, dice_movables);
    
    // std::cout << two_steppers.size() << " many found\n";

    if(two_steppers.size() == 0)
    {
        if(dice_movables.at(max_dice).size() == 1)  // max start has  1 choice
            return _g.ForceMove(dice_movables.at(max_dice).at(0), max_dice);
        else if(dice_movables.at(!max_dice).size() == 1)    // other has only 1 choice
            return _g.ForceMove(dice_movables.at(!max_dice).at(0), !max_dice);
        else
            return status_codes::SUCCESS;   // impossible to complete turn, but nothing to force for possible dice
    }
    else if(two_steppers.size() == 1)
    {
        if( dice_movables.at(0).size() == 0 || dice_movables.at(1).size() == 0) // one not movable without 2step
        {
            bool must_move = dice_movables.at(1).size() > 0;    // can't be both 0
            return _g.ForceMove(*two_steppers.begin(), must_move);
        }
        else
        {
            // neither is 0, not both >1, so at least 1 of the sizes is exactly 1
            bool one_choice = (dice_movables.at(1).size() == 1);

            if( dice_movables.at(one_choice).at(0) == *two_steppers.begin() )   // no new choices for this dice
                return _g.ForceMove(dice_movables.at(one_choice).at(0), one_choice);   
    
            else if ( dice_movables.at(!one_choice).size() == 1 && dice_movables.at(!one_choice).at(0) == *two_steppers.begin() )
                return _g.ForceMove(dice_movables.at(!one_choice).at(0), !one_choice);   
            else
                return status_codes::SUCCESS;
        }        
    }
    else    // two_steppers.size() > 1
        return status_codes::SUCCESS;
}
