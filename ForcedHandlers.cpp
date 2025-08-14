#include "NardiGame.h"

////////////////////////////////////
////////   LegalTurnSeqs   ////////
//////////////////////////////////

Game::LegalTurnSeqs::LegalTurnSeqs(Game& g) : _g(g) {}

const std::vector< std::vector<StartAndDice> >& Game::LegalTurnSeqs::ViewMoveSeqs() const
{
    return _vals;
}

void Game::LegalTurnSeqs::ComputeAllLegalMoves()
{
    _brdsToSeqs.clear();

    std::vector<StartAndDice> seq; 
    dfs(seq);

    // ... very end
    _vals.clear();
    for(const auto& [_, v] : _brdsToSeqs )
        _vals.push_back(v);
}

void Game::LegalTurnSeqs::dfs(std::vector<StartAndDice>& seq)
{
    std::array< std::vector<NardiCoord>, 2 > movables = {_g.arbiter.GetMovables(0), _g.arbiter.GetMovables(1) }; 
        // bypass legality check by only giving pre-approved legal moves

    if(movables.at(0).size() + movables.at(1).size() == 0)  // no moves left
    {
        std::string key = Board2Str(_g.board._mockBoard.View());
        if(!_brdsToSeqs.contains(key))
            _brdsToSeqs.emplace(key, seq);
        return;
    }

    for(int dieIdx = 0; dieIdx < 2; ++ dieIdx)
    {
        for(const auto& coord : movables.at(dieIdx) )   
        {
            seq.emplace_back(coord, dieIdx);
            _g.MockAndUpdateByDice(coord, dieIdx);
            dfs(seq);
            _g.UndoMockAndUpdateByDice(coord, dieIdx);
            seq.pop_back();
        }
    }
}

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
    // std::cout << "doubles is\n";

    // std::cout << "mock dice used: " << _g.times_mockdice_used.at(0) << " " 
                    // << _g.times_mockdice_used.at(1) << "\n";

    if( first_move_checker.PreConditions() )  // first move double 4 or 6
        return first_move_checker.MakeForced();

    steps_left = 4 - (_g.times_mockdice_used.at(0) + _g.times_mockdice_used.at(1));
    if(steps_left <= 0)
        return status_codes::NO_LEGAL_MOVES_LEFT;

    // std::cout << "steps left: " << steps_left << "\n";

    auto movables = _arb.GetMovables(0);    // copy
    // std::cout << "num movables: " << movables.size() << "\n";

    for(int i = 0; i < movables.size(); ++i)
    {
        int n_movables = _g.board._mockBoard.MovablePieces(movables.at(i));

        for(int j = 0; j < n_movables; ++j)
        {
            if(steps_left <= 0)         // previous iteration mocked through to the end, this one can mock further
            {
                _g.ResetMock();
                return status_codes::SUCCESS;
            }
            
            // std::cout << "i = " << i << "\n";
            MockFrom(movables.at(i));   // updates steps_left
        }
    }

    if(steps_left < 0)
    {
        _g.ResetMock();
        return status_codes::SUCCESS;
    }
    
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
        _g.MockAndUpdateByDice(start, 0);

        // updatge start and dest
        start = dest;
        std::tie(can_go, dest) = _arb.CanMoveByDice(start, 0);
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

    if (canUse[0] + canUse[1] == 1 )  // can use 1 but NOT both dice
    {
        _activeDiceIdx = (canUse[1] == 1);
        return true;
    }
    return false;
}

status_codes Game::SingleDiceHandler::Check()
{
    return ForceFromDice(_activeDiceIdx);
}

status_codes Game::SingleDiceHandler::ForceFromDice(bool active_dice)
{
    auto& seqs = _g.legal_turn.ViewMoveSeqs();
    if(seqs.size() == 0)
        return status_codes::NO_LEGAL_MOVES_LEFT;
    else if(seqs.size() == 1)
    {
        auto [start, d_idx] = seqs.at(0).at(0);
        return _g.MakeMove(start, d_idx);
    }
    else
        return status_codes::SUCCESS;   // not forced to move, at least two choices

        
    // auto& movables = _g.legal_turn._moves.at(_activeDiceIdx);

    // if(movables.size() == 0)
    //     return status_codes::NO_LEGAL_MOVES_LEFT;
    // else if(movables.size() == 1)
    //     return _g.MakeMove(movables.at(0), active_dice);
    // else
    //     return status_codes::SUCCESS;   // not forced to move, at least two choices
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
    _maxDice = (_g.dice[1] > _g.dice[0]);

    // auto& seqs = _g.legal_turn.ViewMoveSeqs();

    // if(seqs.size() == 0)
    //     return status_codes::NO_LEGAL_MOVES_LEFT;
    // else if(seqs.size() == 1)
    // {
    //     for(auto [start, d_idx] : seqs.at(0))
    //         _g.MockAndUpdateByDice(start, d_idx);

    //     _g.RealizeMock();
    //     return status_codes::NO_LEGAL_MOVES_LEFT;
    // }
    // else if(seqs.at(0).size() < 2) // can't complete a full turn
    // {
    //     std::vector<NardiCoord> max_starts;
    //     std::vector<NardiCoord> min_starts;
    //     for(auto [start, d_idx] : seqs.at(0))
    //     {
    //         if(d_idx == _maxDice)
    //             max_starts.push_back(start);
    //         else
    //             min_starts.push_back(start);
    //     }

    //     if(max_starts.size() == 1)
    //         return _g.MakeMove(max_starts.at(0), _maxDice);
    //     else if(min_starts.size() == 1)
    //         return _g.MakeMove(min_starts.at(0), !_maxDice);
    //     else
    //         return status_codes::SUCCESS;   // no full turn but nothing forced either, at least 2 options for the playable dice
    // }
    // else
    //     return status_codes::SUCCESS;

    std::array< std::vector<NardiCoord>, 2 > dice_movables = { _arb.GetMovables(0), _arb.GetMovables(1) };

    // std::cout << "options: " << dice_movables.at(0).size() << " " << dice_movables.at(1).size() << "\n";

    if (dice_movables.at(0).size() > 1 && dice_movables.at(1).size() > 1)       // nothing forced
        return status_codes::SUCCESS;
    
    else if(dice_movables.at(0).size() + dice_movables.at(1).size() == 0)         // neither have any options,  0-0
        return status_codes::NO_LEGAL_MOVES_LEFT;

    else if( dice_movables.at(0).size() + dice_movables.at(1).size() == 1 )       // one has no options, other has only 1,  0-1
    {
        bool moving_by = dice_movables.at(1).size() == 1;
        return _g.MakeMove(dice_movables.at(moving_by).at(0), moving_by);
    }
    
    else if( dice_movables.at(0).size() == 1 && dice_movables.at(1).size() == 1 )
    {
        NardiCoord max_start = dice_movables.at(_maxDice).at(0);
        NardiCoord min_start = dice_movables.at(!_maxDice).at(0);

        // std::cout << "moving from " << max_start.AsStr() << " and " << min_start.AsStr() << "\n";
        // std::cout << "movable pieces each: " << _g.board._mockBoard.MovablePieces(max_start) << " and " <<_g.board._mockBoard.MovablePieces(min_start) << "\n";

        if( min_start == max_start && _g.board._mockBoard.MovablePieces(max_start) == 1 )
            return _g.MakeMove(max_start, _maxDice);
    }
















    

    // std::cout << "need to get two steppers\n";

    std::unordered_set<NardiCoord> two_steppers = _arb.GetTwoSteppers(2, dice_movables);
    
    // std::cout << two_steppers.size() << " many found\n";

    if(two_steppers.size() == 0)
    {
        if(dice_movables.at(_maxDice).size() == 1)  // max start has  1 choice
            return _g.MakeMove(dice_movables.at(_maxDice).at(0), _maxDice);
        else if(dice_movables.at(!_maxDice).size() == 1)    // other has only 1 choice
            return _g.MakeMove(dice_movables.at(!_maxDice).at(0), !_maxDice);
        else
            return status_codes::SUCCESS;   // impossible to complete turn, but nothing to force for possible dice
    }
    else if(two_steppers.size() == 1)
    {
        if( dice_movables.at(0).size() == 0 || dice_movables.at(1).size() == 0) // one not movable without 2step
        {
            bool must_move = dice_movables.at(1).size() > 0;    // can't be both 0
            return _g.MakeMove(*two_steppers.begin(), must_move);
        }
        else
        {
            // neither is 0, not both >1, so at least 1 of the sizes is exactly 1
            bool one_choice = (dice_movables.at(1).size() == 1);

            if( dice_movables.at(one_choice).at(0) == *two_steppers.begin() )   // no new choices for this dice
                return _g.MakeMove(dice_movables.at(one_choice).at(0), one_choice);   
    
            else if ( dice_movables.at(!one_choice).size() == 1 && dice_movables.at(!one_choice).at(0) == *two_steppers.begin() )
                return _g.MakeMove(dice_movables.at(!one_choice).at(0), !one_choice);   
            else
                return status_codes::SUCCESS;
        }        
    }
    else    // two_steppers.size() > 1
        return status_codes::SUCCESS;
}
