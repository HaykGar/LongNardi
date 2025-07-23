#include "NardiGame.h"
#include "ReaderWriter.h"

///////////////////////////
////////   Game   ////////
/////////////////////////

///////////// Constructor /////////////

Game::Game(int rseed) : board(),  rng(rseed), dist(1, 6), dice({0, 0}), times_dice_used({0, 0}),
                        doubles_rolled(false), turn_number({0, 0}), rw(nullptr), arbiter(*this)
{} 

Game::Game() :  board(),  rng(std::random_device{}()), dist(1, 6), dice({0, 0}), times_dice_used({0, 0}), 
                doubles_rolled(false), turn_number({0, 0}), rw(nullptr), arbiter(*this)
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

    IncrementTurnNumber();  // starts at 0
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
    // DispErrorCode(result);

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

status_codes Game::ForceMove(const NardiCoord& start, bool dice_idx)  // only to be called when forced
{
    UseDice(dice_idx);
    return MakeMove(start, board.CoordAfterDistance(start, dice[dice_idx]) );
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

status_codes Game::ForceRemovePiece(const NardiCoord& start, bool dice_idx)
{
    UseDice(dice_idx);
    return RemovePiece(start);
}

////////////////////////////
////////   Arbiter ////////
//////////////////////////

///////////// Constructor /////////////

Game::Arbiter::Arbiter(Game& gm) :  _g(gm), _doubles(_g, *this), _twoDice(_g, *this), _singleDice(_g, *this),
                                    _prevMonitor(_g, *this), _firstMove(_g, *this), _blockMonitor(_g, *this)
{}

///////////// Legality /////////////

std::pair<status_codes, NardiCoord> Game::Arbiter::CanMoveByDice(const NardiCoord& start, bool dice_idx)
{
    if(!CanUseDice(dice_idx))
        return {status_codes::DICE_USED_ALREADY, {} };

    //std::cout << "dice usable\n";

    NardiCoord final_dest = _g.board.CoordAfterDistance(start, _g.dice[dice_idx]);
    status_codes result = _g.board.WellDefinedEnd(start, final_dest);

    //std::cout << "board legal?\n";
    //DispErrorCode(result);
    if (result == status_codes::SUCCESS)
    {
        if (_blockMonitor.Illegal(start, dice_idx))
            return {status_codes::BAD_BLOCK, {} };
        else if(_prevMonitor.Illegal(start, dice_idx))
            return {status_codes::PREVENTS_COMPLETION, {} }; 
    }   
    else
        return {result, final_dest};
}

bool Game::Arbiter::CanUseDice(bool idx, int n) const
{
    int new_val = _g.times_dice_used[idx] + n;
    return ( new_val <= 1 + _g.doubles_rolled*(3 - _g.times_dice_used[!idx]));
}

bool Game::Arbiter::CanRemovePiece(const NardiCoord& start, bool dice_idx)
{
    if(!CanUseDice(dice_idx))
        return false;
    
    int pos_from_end = COL - start.col;
    return (pos_from_end == _g.dice[dice_idx] ||  // dice val exactly
            (pos_from_end == _g.board.MaxNumOcc().at(_g.board.PlayerIdx()) && _g.dice[dice_idx] > pos_from_end) );  // largest available, less than dice
}

std::pair<status_codes, std::array<int, 2>> Game::Arbiter::LegalMove(const NardiCoord& start, const NardiCoord& end)    
// array represents how many times each dice is used, 0 or 1 usually, in case of doubles can be up to 4
{    
    unsigned d = _g.board.GetDistance(start, end);

    if(d == _g.dice[0])
        return {CanMoveByDice(start, 0).first, {1, 0} };
    else if (d == _g.dice[1]) { //std::cout << "moving by dice val: " << _g.dice[1] << "\n";
        return {CanMoveByDice(start, 1).first, {0, 1} }; }
    else if (d == _g.dice[0] + _g.dice[1])
        return {LegalMove_2step(start).first, {1, 1}};
    else if ( _g.doubles_rolled && (d % _g.dice[0] == 0)  )
    {
        if(!CanUseDice(0, d / _g.dice[0]) )
            return { status_codes::DICE_USED_ALREADY, {} };
        
        auto [step2_status, step2_dest] = LegalMove_2step(start); 
        if(step2_status != status_codes::SUCCESS)
            return { status_codes::NO_PATH, {} };
        else if( d == _g.dice[0] * 3)
            return { CanMoveByDice(step2_dest, 0).first, {3, 0} };
        else if(d == _g.dice[0] * 4)
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
    if(status != status_codes::SUCCESS && !_g.doubles_rolled){
        std::tie(status, mid) = CanMoveByDice( start, 1);    
        first_dice = !first_dice;   // try both dice to get to a midpoint, no need if doubles
    }

    if(status == status_codes::SUCCESS)
    {
        auto [outcome, dest] = CanMoveByDice(mid, !first_dice);
        if(outcome == status_codes::SUCCESS && !_blockMonitor.Unblockers().empty())
            
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
    _blockMonitor.Reset();
    return CheckForcedMoves();
}

status_codes Game::Arbiter::OnMove(const NardiCoord& start, const NardiCoord& end)
{
    _prevMonitor.Reset();

    return CheckForcedMoves();
}


status_codes Game::Arbiter::OnRemoval(const NardiCoord& start)
{
    return CheckForcedMoves();
}

///////////// Forced Moves /////////////

status_codes Game::Arbiter::CheckForcedMoves()  // fixme `
{
    if(_doubles.Is())
        return _doubles.Check();
    else if(_twoDice.Is())
        return _twoDice.Check();
    else if(_singleDice.Is())
        return _singleDice.Check();
    else
        return status_codes::NO_LEGAL_MOVES_LEFT;
}

////////////////////////////////////////////
////////   RuleExceptionMonitors   ////////
//////////////////////////////////////////

///////////// Base Class /////////////

Game::RuleExceptionMonitor::RuleExceptionMonitor(Game& g, Arbiter& a) : _g(g), _arb(a), _flag(false) {}

bool Game::RuleExceptionMonitor::IsFlagged() const
{
    return _flag;
}

void Game::RuleExceptionMonitor::Reset()
{
    _flag = false;
}

///////////// FirstMove /////////////

Game::FirstMoveException::FirstMoveException(Game& g, Arbiter& a) : RuleExceptionMonitor(g, a) {}

bool Game::FirstMoveException::PreConditions()
{
    _flag = (_g.turn_number[_g.board.PlayerIdx()] == 1 && (_g.dice[0] == 4 || _g.dice[0] == 6 ) );
    return _flag;
}

bool Game::FirstMoveException::Illegal(const NardiCoord& s, bool dice_idx)
{
    return  (  PreConditions() );
}

status_codes Game::FirstMoveException::MakeForced()
{
    int dist = _g.dice[0] * (1 + (_g.dice[0] == 4) );    // 8 if double 4, else 6
    NardiCoord head(_g.board.PlayerIdx(), 0);

    NardiCoord dest(head.row, dist);

    _g.board.Move(head, dest);
    _g.board.Move(head, dest);

    if(_g.rw)
        _g.rw->ReAnimate(); 

    Reset();

    return status_codes::NO_LEGAL_MOVES_LEFT;
}

///////////// Turn Completion /////////////

Game::PreventionMonitor::PreventionMonitor(Game& g, Arbiter& a) : RuleExceptionMonitor(g, a) 
{
    _flag = PreConditions();  // redundant but more clear
}

bool Game::PreventionMonitor::PreConditions()
{
    _flag = (!_g.doubles_rolled && _arb.CanUseDice(0) && _arb.CanUseDice(1) && _g.min_options[0] >= 1 && _g.min_options[1] >= 1 );
            // could play both before
    return _flag;
}

bool Game::PreventionMonitor::MakesSecondStep(const NardiCoord& start) const
{   
    return (_g.board.WellDefinedEnd(start, _g.board.CoordAfterDistance(start, _g.dice[0] + _g.dice[1]) ) 
                == status_codes::SUCCESS  );
}

bool Game::PreventionMonitor::Illegal(const NardiCoord& start, bool dice_idx)
{
    return
    (
        PreConditions() &&
        (
            !MakesSecondStep(start) &&                      // cannot continue with this piece
            ( 
                _g.PlayerGoesByDice(!dice_idx).empty() ||      // no pieces move first by other dice
                (
                    _g.PlayerGoesByDice(!dice_idx).size() == 1 && 
                    _g.PlayerGoesByDice(!dice_idx).contains(start) && 
                    _g.board.MovablePieces(start) == 1
                )   // moving start prevents moving any other piece by other dice
            )   
        )
    );
}

    // //std::cout << "doub roll: " << std::boolalpha << _g.doubles_rolled << "\n";
    // //std::cout << "dices usable: " << std::boolalpha << (CanUseDice(0) && CanUseDice(1)) << "\n";
    // //std::cout << "options: " << _g.min_options[0] << " " << _g.min_options[1] << "\n";
    // //std::cout << "steps twice: " << std::boolalpha << (MakesSecondStep(start)) << "\n";
    // //std::cout << "contains start: " << std::boolalpha << (_g.PlayerGoesByDice(!dice_idx).contains(start)) << "\n";
    // //std::cout << "only 1 went: " << std::boolalpha << (_g.PlayerGoesByDice(!dice_idx).size() == 1) << "\n"; 
        
    // //std::cout << "\n\n";
    // for(const auto& coord : _g.PlayerGoesByDice(!dice_idx))
    // {
    //     //std::cout << coord.row << ", " << coord.col << " goes by " << _g.dice[!dice_idx] << "\n";
    // }
    // //std::cout << "\n\n";
    
    // //std::cout << "no piece went by other: " << std::boolalpha << (_g.PlayerGoesByDice(!dice_idx).empty()) << "\n"; 
    // //std::cout << "only 1 movable at start: " << std::boolalpha << (_g.board.MovablePieces(start) == 1) << "\n"; 

    // //std::cout << "return value " << (
    //     !_g.doubles_rolled && 
    //     CanUseDice(0) && CanUseDice(1) && 
    //     _g.min_options[0] >= 1 && _g.min_options[1] >= 1  &&  // could play both before
    //     !MakesSecondStep(start) &&                      // cannot continue with this piece
    //     ( 
    //         _g.PlayerGoesByDice(!dice_idx).empty() ||      // no pieces move first by other dice
    //         (
    //             _g.PlayerGoesByDice(!dice_idx).size() == 1 && 
    //             _g.PlayerGoesByDice(!dice_idx).contains(start) && 
    //             _g.board.MovablePieces(start) == 1
    //         )   // moving start prevents moving any other piece by other dice
    //     )   
    // ) << "\n"; // expect true here
    // //std::cout << "\n\n\n\n";


///////////// Bad Block /////////////

Game::BadBlockMonitor::BadBlockMonitor(Game& g, Arbiter& a) : RuleExceptionMonitor(g, a), _state(block_state::CLEAR) {}

bool Game::BadBlockMonitor::PreConditions()
{
    return _g.board.ReachedEnemyHome().at(! _g.board.PlayerIdx() ) == 0; // not active once enemy entered home 
}

bool Game::BadBlockMonitor::Illegal(const NardiCoord& start, bool dice_idx)
{
    if( PreConditions() )
    {
        if( !_flag )
        {
            NardiCoord end = _g.board.CoordAfterDistance(start, _g.dice[dice_idx]);
            _diceAttempting = dice_idx;

            return ( _g.board.at(end) == 0 && BlockageAt(end) && !PieceAhead() && !Fixable() );
        }
        else
            return !_unblockers.contains(start);
    }
    else
        return false;
}

bool Game::BadBlockMonitor::BlockageAt(const NardiCoord& end)
{
    _blockLength = 1;   // end coord itself
    
    unsigned n_ahead = 0;
    unsigned n_behind = 0;
    int player_sign = _g.board.PlayerSign();
    bool other_player = !_g.board.PlayerIdx();

    NardiCoord coord = _g.board.CoordAfterDistance(end, 1, other_player);

    // check ahead of end
    for(coord; !coord.OutOfBounds() && _g.board.at(coord) * player_sign > 0; coord = _g.board.CoordAfterDistance(coord, 1, other_player))
        ++n_ahead;
    
    //check behind end
    coord = _g.board.CoordAfterDistance(end, -1, other_player);;
    for(coord; !coord.OutOfBounds() && _g.board.at(coord) * player_sign > 0; coord = _g.board.CoordAfterDistance(coord, -1, other_player))
        ++n_behind;
    
    _blockStart = _g.board.CoordAfterDistance(end, -n_behind, other_player);
    _blockLength += n_ahead + n_behind;

    return (_blockLength > 6);
}

bool Game::BadBlockMonitor::PieceAhead() 
{
    NardiCoord after_block = _g.board.CoordAfterDistance(_blockStart, _blockLength, !_g.board.PlayerIdx());

    for(;!after_block.OutOfBounds(); after_block = _g.board.CoordAfterDistance(after_block, 1, !_g.board.PlayerIdx()))
    {
        if(_g.board.at(after_block) * _g.board.PlayerSign() < 0){
            _flag = true;
            return true;
        }
    }
    return false;
}

bool Game::BadBlockMonitor::Fixable()
{
    if(_blockLength < 6)
        return true;    // should never be called in this condition

    if( !_arb.CanUseDice(0) || !_arb.CanUseDice(1) )
        return false;

    unsigned d = _g.dice[_diceAttempting];
    int i = _blockLength - 6;
    NardiCoord to_move = _g.board.CoordAfterDistance(_blockStart, i, !_g.board.PlayerIdx());

    for(i; i < 6; ++i, to_move = _g.board.CoordAfterDistance(to_move, 1, !_g.board.PlayerIdx()))
    {
        if(_g.board.WellDefinedEnd( to_move, _g.board.CoordAfterDistance(to_move, d) ) == status_codes::SUCCESS
            && abs(_g.board.at(to_move)) <= 1 && !_g.board.HeadReuseIssue(to_move)  )
        {
            _unblockers.insert(to_move);
        }
    }

    if(_unblockers.empty())
    {
        _state = block_state::BAD_BLOCK;
        return false;
    }
    else
    {
        _state = block_state::FIXABLE_BLOCK;
        return true;
    }
}

Game::BadBlockMonitor::block_state Game::BadBlockMonitor::State() const
{
    return _state;
}

const std::unordered_set<NardiCoord>& Game::BadBlockMonitor::Unblockers() const
{
    return _unblockers;
}

void Game::BadBlockMonitor::Reset()
{
    _state = block_state::CLEAR;
    _blockLength = 0;
    _unblockers.clear();
}

/////////////////////////////////////
////////   ForcedHandlers   ////////
///////////////////////////////////

///////////// Base Class /////////////

Game::ForcedHandler::ForcedHandler(Game& g, Arbiter& a) : _arb(a), _g(g) {}

///////////// Doubles /////////////

Game::DoublesHandler::DoublesHandler(Game& g, Arbiter& a) : ForcedHandler(g, a), forcing_doubles(false), first_move_checker(g, a) {}

bool Game::DoublesHandler::Is()
{
    return (_g.dice[0] == _g.dice[1]);
}

status_codes Game::DoublesHandler::Check()
{
    if(forcing_doubles)
    {
        if( _g.PlayerGoesByDice(0).empty() || ( _g.PlayerGoesByDice(0).size() == 1 && _g.board.HeadReuseIssue(*_g.PlayerGoesByDice(0).begin()) ) )
        {   // base case
            forcing_doubles = false;
            return status_codes::NO_LEGAL_MOVES_LEFT;
        }
        else
            return _g.ForceMove(*_g.PlayerGoesByDice(0).begin(), 0);   // recurse
    }  
    else if( first_move_checker.PreConditions() )  // first move double 4 or 6
        return first_move_checker.MakeForced();
    else if (_g.times_dice_used[0] + _g.times_dice_used[1] > 0) // if no forced on roll, no forced after
    {
        if(_g.times_dice_used[0] + _g.times_dice_used[1] == 4)
            return status_codes::NO_LEGAL_MOVES_LEFT;
        else
            return status_codes::SUCCESS;
    }  
    // right after roll, no more exceptions

    int steps_left = 4; // 4 steps left to complete turn by earlier if statement
    int steps_taken = 0;

    if(_g.PlayerGoesByDice(0).empty())    // no pieces that go
        return status_codes::NO_LEGAL_MOVES_LEFT;

    for(const auto& coord : _g.PlayerGoesByDice(0))
    {
        if (_g.board.HeadReuseIssue(coord))
            continue;   // don't use this coord

        int n_pieces = _g.board.MovablePieces(coord);
        
        NardiCoord start = coord;
        auto [canGo, dest] = _arb.CanMoveByDice(start, 0);

        while(canGo == status_codes::SUCCESS)   // runs at most 4 times without returning
        {
            steps_taken += n_pieces;

            if(steps_taken > steps_left)            
                return status_codes::SUCCESS;

            start = dest;
            std::tie(canGo, dest) = _arb.CanMoveByDice(start, 0);
        }
    }  
    // if we exit loop, then the moves are forced
    if( _g.PlayerGoesByDice(0).empty() ||
            ( _g.PlayerGoesByDice(0).size() == 1 &&
            _g.board.HeadReuseIssue(*_g.PlayerGoesByDice(0).begin()) ) )
    {
        forcing_doubles = false;
        return status_codes::NO_LEGAL_MOVES_LEFT;
    }
    else
    { 
        forcing_doubles = true;
        return _g.ForceMove(*_g.PlayerGoesByDice(0).begin(), 0);   // will recurse until empty
    }
}

///////////// Single Dice /////////////

Game::SingleDiceHandler::SingleDiceHandler(Game& g, Arbiter& a) : ForcedHandler(g, a) {}

bool Game::SingleDiceHandler::Is()
{
    bool canUse[2] = { _arb.CanUseDice(0), _arb.CanUseDice(1) };
    return ( (canUse[0] || canUse[1]) && (!canUse[0] || !canUse[1]) );  // can use 1 but NOT both dice
}

status_codes Game::SingleDiceHandler::Check()
{
    bool active_dice = _arb.CanUseDice(1);
    if ( _g.board.CurrPlayerInEndgame() && _g.dice[active_dice] >= _g.board.MaxNumOcc().at(_g.board.PlayerIdx()) )
        return _g.ForceRemovePiece({!_g.board.PlayerIdx(), COL - _g.board.MaxNumOcc().at( _g.board.PlayerIdx() )}, active_dice);
    else
        return ForceFromDice(active_dice);
}

status_codes Game::SingleDiceHandler::ForceFromDice(bool dice_idx) // no doubles here, one dice used already
{       
    if(_g.PlayerGoesByDice(dice_idx).empty())
        return status_codes::NO_LEGAL_MOVES_LEFT;
    else if (_g.PlayerGoesByDice(dice_idx).size() == 1)
    {
        NardiCoord start = *_g.PlayerGoesByDice(dice_idx).begin();
        if(!_g.board.HeadReuseIssue(start) && _arb.CanMoveByDice(start, dice_idx).first == status_codes::SUCCESS)
            return _g.ForceMove(start, dice_idx);
        else
            return status_codes::NO_LEGAL_MOVES_LEFT;   // success or fail, no legal moves remain
    }
    else if (_g.PlayerGoesByDice(dice_idx).size() == 2 &&     // two pieces that go
            _g.PlayerGoesByDice(dice_idx).contains( _g.PlayerHead() ) &&    // one of which is head
            _g.board.HeadUsed()  )                                          // can't reuse head, so only one piece which actually goes
    {
        std::unordered_set<NardiCoord>::iterator it =  _g.PlayerGoesByDice(dice_idx).begin();
        if (_g.board.IsPlayerHead(*it)) // only 2 items, either not head or next one is
            ++it;
        _g.ForceMove(*it, dice_idx);
        return status_codes::NO_LEGAL_MOVES_LEFT;
    }
    else
        return status_codes::SUCCESS;   // not forced to move, at least two choices
}

///////////// Two Dice /////////////

Game::TwoDiceHandler::TwoDiceHandler(Game& g, Arbiter& a) : SingleDiceHandler(g, a) {}

bool Game::TwoDiceHandler::Is()
{
    return (_arb.CanUseDice(0) &&  _arb.CanUseDice(1));
}

status_codes Game::TwoDiceHandler::Check()
{
    _g.min_options = { _g.PlayerGoesByDice(0).size(), _g.PlayerGoesByDice(1).size() };
    bool max_dice = (_g.dice[1] > _g.dice[0]);
    NardiCoord start = *_g.PlayerGoesByDice(max_dice).begin();

    if(_g.board.CurrPlayerInEndgame() && _g.dice[max_dice] >= _g.board.MaxNumOcc().at( _g.board.PlayerIdx() ) )
        return _g.ForceRemovePiece({!_g.board.PlayerIdx(), COL - _g.board.MaxNumOcc().at( _g.board.PlayerIdx() )}, max_dice);
    
    // no doubles, no head reuse issues since we can't have made a move yet
    if(_g.min_options[0] > 1 && _g.min_options[1] > 1)
        return status_codes::SUCCESS;
    else if(_g.min_options[0] + _g.min_options[1] == 1)   // one has no options, other has only 1
        return ForceFromDice(_g.min_options[1] == 1);
    else if (_g.min_options[0] == _g.min_options[1])
    {
        if (_g.min_options[0] == 0)
            return status_codes::NO_LEGAL_MOVES_LEFT;
        else if(_g.min_options[0] == 1)
        {
            if(start == *_g.PlayerGoesByDice(!max_dice).begin() && _g.board.MovablePieces(start) == 1 )
                return ForceFromDice(max_dice); // Will make other forced move if possible
        }
    }
    else if (_g.min_options[max_dice] == 1 && _arb.CanMoveByDice(start, max_dice).first != status_codes::SUCCESS)  // prevention issue 
        return ForceFromDice(max_dice);
    else if (_arb.CanMoveByDice(*_g.PlayerGoesByDice(!max_dice).begin(), !max_dice).first != status_codes::SUCCESS )
        return ForceFromDice(!max_dice);

    // eliminated 0-0, 0-1, 1-1 with same piece, and 1-? with move prevention on the 1, options[more_options] > 1 or they're both 1
    
    bool more_options = _g.min_options[1] > _g.min_options[0];
    
    // iterate through coords for dice with more options, try to find a 2step move
    std::stack<NardiCoord> two_step_starts; // start coords for 2-step moves
    for(const auto& coord : _g.PlayerGoesByDice(more_options))    
    {
        auto [can_go, _] = _arb.LegalMove_2step(coord);
        if(can_go == status_codes::SUCCESS)
        {
            if(_g.min_options[more_options] == 1)    // case both only move from 1 square w/ multiple pieces
                ++_g.min_options[more_options];
            
            ++_g.min_options[!more_options];
            if(_g.min_options[0] > 1 && _g.min_options[1] > 1)
                return status_codes::SUCCESS;
            two_step_starts.push(coord);
        }
    }
    if(_g.min_options[max_dice] == 1) // if can only play 1 dice, forced to play larger one
        return HandleForced2Dice(max_dice, two_step_starts);
    else if(_g.min_options[!max_dice] == 1)
        return HandleForced2Dice(!max_dice, two_step_starts);
    else    // one of the moves is impossible but nothing is forced, there are multiple legal moves for the other
        return status_codes::SUCCESS;   
}

status_codes Game::TwoDiceHandler::HandleForced2Dice(bool dice_idx, const std::stack<NardiCoord>& two_step_starts) // return no legal moves if none left after making one?
{       
    if(_g.PlayerGoesByDice(dice_idx).size() == 1)  // no new 2step moves
        return _g.ForceMove(*_g.PlayerGoesByDice(dice_idx).begin(), dice_idx); // makes other forced move as needed
    else    // only a 2step move for max dice
    {
        NardiCoord start = two_step_starts.top();
        return _g.ForceMove(start, !dice_idx); // should always be NO_LEGAL_MOVES_LEFT
    }
}
