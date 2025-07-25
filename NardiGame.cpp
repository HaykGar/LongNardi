#include "NardiGame.h"
#include "ReaderWriter.h"

///////////////////////////
////////   Game   ////////
/////////////////////////

///////////// Constructor /////////////

Game::Game(int rseed) : board(), mock_board(), rng(rseed), dist(1, 6), dice({0, 0}), times_dice_used({0, 0}),
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

status_codes Game::TryStart(const NardiCoord& start)
{
    //std::cout << "called TryStart\n";
    // During endgame, if it's a valid start just check if there's a forced move from here, will streamline play
    return board.ValidStart(start);
}

status_codes Game::TryFinishMove(const NardiCoord& start, const NardiCoord& end) // assumes start already checked
{   
    ResetMock();

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

    ResetMock(); // could be redundant, but safe
    
    if(arbiter.CanRemovePiece(start, dice_idx))
    {
        UseDice(dice_idx);
        RemovePiece(start);     // change to remove piece start, dice_idx `
    }
    
    auto [result, dest]  = arbiter.CanFinishByDice(start, dice_idx);
    
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
    ResetMock();

    if(rw)
        rw->ReAnimate();

    return arbiter.OnMove();
}

void Game::MockMove(const NardiCoord& start, const NardiCoord& end)
{
    mock_board.Move(start, end);

    arbiter.SolidifyBlockMonitor();
}

void Game::MockMoveByDice(const NardiCoord& start, bool dice_idx)
{
    ++times_mockdice_used.at(mock_board.PlayerIdx());
    NardiCoord dest = mock_board.CoordAfterDistance(start, dice[dice_idx]);
    MockMove(start, dest);
}

status_codes Game::ForceMove(const NardiCoord& start, bool dice_idx)  // only to be called when forced
{
    UseDice(dice_idx);
    if(arbiter.CanRemovePiece(start, dice_idx))
        return RemovePiece(start);
    else
        return MakeMove(start, board.CoordAfterDistance(start, dice[dice_idx]) );
}

status_codes Game::RemovePiece(const NardiCoord& start)
{
    board.Remove(start);
    ResetMock();

    if(rw)
        rw->ReAnimate();
    if(GameIsOver())
        return status_codes::NO_LEGAL_MOVES_LEFT;

    return arbiter.OnRemoval();
}

status_codes Game::ForceRemovePiece(const NardiCoord& start, bool dice_idx)
{
    UseDice(dice_idx);
    return RemovePiece(start);
}

///////////// Updates /////////////

void Game::ResetMock()
{
    mock_board = board;
    times_mockdice_used = times_dice_used;
}

void Game::RealizeMock()
{
    board = mock_board;
    times_dice_used = times_mockdice_used;

    if(rw)
        rw->ReAnimate();
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
    status_codes can_start = _g.mock_board.ValidStart(start);
    if(can_start != status_codes::SUCCESS)
        return {can_start, {} };
    else
        return CanFinishByDice(start, dice_idx);
}

std::pair<status_codes, NardiCoord> Game::Arbiter::CanFinishByDice(const NardiCoord& start, bool dice_idx)
{
    if(!CanUseMockDice(dice_idx))
        return {status_codes::DICE_USED_ALREADY, {} };

    //std::cout << "dice usable\n";

    NardiCoord final_dest = _g.mock_board.CoordAfterDistance(start, _g.dice[dice_idx]);
    status_codes result = _g.mock_board.WellDefinedEnd(start, final_dest);

    //std::cout << "board legal?\n";
    //DispErrorCode(result);
    if (result == status_codes::SUCCESS)
    {
        if (_blockMonitor.Illegal(start, dice_idx))
            return {status_codes::BAD_BLOCK, {} };
        else if(_prevMonitor.Illegal(start, dice_idx))
            return {status_codes::PREVENTS_COMPLETION, {} }; 
    }   
    
    return {result, final_dest};
}

bool Game::Arbiter::CanRemovePiece(const NardiCoord& start, bool dice_idx)
{
    if(!CanUseMockDice(dice_idx) || !_g.mock_board.CurrPlayerInEndgame())
        return false;
    
    int pos_from_end = COL - start.col;
    return (pos_from_end == _g.dice[dice_idx] ||  // dice val exactly
            (pos_from_end == _g.mock_board.MaxNumOcc().at(_g.mock_board.PlayerIdx()) && _g.dice[dice_idx] > pos_from_end) );  
                // largest available is less than dice
}

bool Game::Arbiter::CanUseMockDice(bool idx, int n) const
{
    int new_val = _g.times_mockdice_used[idx] + n;
    return ( new_val <= 1 + _g.doubles_rolled*(3 - _g.times_mockdice_used[!idx]));
}

std::pair<status_codes, std::array<int, 2>> Game::Arbiter::LegalMove(const NardiCoord& start, const NardiCoord& end)    
// array represents how many times each dice is used, 0 or 1 usually, in case of doubles can be up to 4
{    
    unsigned d = _g.mock_board.GetDistance(start, end);

    if(d == _g.dice[0])
        return {CanFinishByDice(start, 0).first, {1, 0} };
    else if (d == _g.dice[1]) { //std::cout << "moving by dice val: " << _g.dice[1] << "\n";
        return {CanFinishByDice(start, 1).first, {0, 1} }; }
    else if (d == _g.dice[0] + _g.dice[1])
        return {LegalMove_2step(start).first, {1, 1}};
    else if ( _g.doubles_rolled && (d % _g.dice[0] == 0)  )
    {
        if(!CanUseMockDice(0, d / _g.dice[0]) )
            return { status_codes::DICE_USED_ALREADY, {} };
        
        auto [step2_status, step2_dest] = LegalMove_2step(start); 
        if(step2_status != status_codes::SUCCESS)
            return { status_codes::NO_PATH, {} };
        else if( d == _g.dice[0] * 3)
            return { CanFinishByDice(step2_dest, 0).first, {3, 0} };
        else if(d == _g.dice[0] * 4)
            return { LegalMove_2step(step2_dest).first, {4, 0} };
    }
    
    return {status_codes::NO_PATH, {}};
}

std::pair<status_codes, NardiCoord> Game::Arbiter::LegalMove_2step(const NardiCoord& start)  
{
    if(!CanUseMockDice(0) || !CanUseMockDice(1))
        return {status_codes::DICE_USED_ALREADY, {} };

    bool first_dice = 0;

    NardiCoord mid = _g.mock_board.CoordAfterDistance(start, _g.dice[first_dice]);
    status_codes status = _g.mock_board.WellDefinedEnd(start, mid);
    if(status != status_codes::SUCCESS || _blockMonitor.Illegal(start, first_dice))
    {
        if(_g.doubles_rolled)
            return {status_codes::NO_PATH, {} }; 
        
        first_dice = !first_dice;   // try both dice to get to a midpoint, no need if doubles
        mid = _g.mock_board.CoordAfterDistance(start, _g.dice[first_dice]);
        status = _g.mock_board.WellDefinedEnd(start, mid);

        if(status != status_codes::SUCCESS || _blockMonitor.Illegal(start, first_dice))
            return {status_codes::NO_PATH, {} }; 
    }
    // valid midpoint reached

    ++_g.times_mockdice_used[_g.mock_board.PlayerIdx()];
    _g.MockMove(start, mid);

    NardiCoord dest = _g.mock_board.CoordAfterDistance(mid, _g.dice.at(!first_dice));

    if(_blockMonitor.Illegal(mid, !first_dice))
        status = status_codes::BAD_BLOCK;
    else
        status = _g.mock_board.WellDefinedEnd(mid, dest);

    _g.ResetMock();
    return { status, dest };
}

bool Game::Arbiter::IllegalBlocking(const NardiCoord& start, bool idx)
{
    return _blockMonitor.Illegal(start, idx);
}

void Game::Arbiter::UpdateMovables()
{
    for(const auto& coord : _g.PlayerGoesByDice(0))
    {
        std::cout << "coord considered: " << coord.row << ", " << coord.col << "\n";
        if(CanMoveByDice(coord, 0).first == status_codes::SUCCESS )   // checks prevention as well
        {
            _movables.at(0).push_back(coord);
            std::cout << "works with dice 0\n";
        }
        else
        {
            DispErrorCode(CanMoveByDice(coord, 0).first);
            std::cout << "doesn't work\n";
        }
            
    }
    for(const auto& coord : _g.PlayerGoesByDice(1))
    {
        if(CanMoveByDice(coord, 1).first == status_codes::SUCCESS )
            _movables.at(1).push_back(coord);
    }
}

const std::vector<NardiCoord>& Game::Arbiter::GetMovables(bool idx)
{
    return _movables.at(idx);
}

std::unordered_set<NardiCoord> Game::Arbiter::GetTwoSteppers(size_t max_qty, const std::array<std::vector<NardiCoord>, 2>& to_search)
{
    std::unordered_set<NardiCoord> two_steppers;

    for(const auto& start : to_search.at(0))
    {
        auto [can_go, dest] = LegalMove_2step(start);
        if(can_go == status_codes::SUCCESS)
        {
            two_steppers.insert(start);
            if(two_steppers.size() == max_qty)
                return two_steppers;
        }
    }

    for(const auto& start : to_search.at(1))
    {
        if(two_steppers.contains(start))
            continue;
        
        auto [can_go, dest] = LegalMove_2step(start);
        if(can_go == status_codes::SUCCESS)
        {
            two_steppers.insert(start);
            if(two_steppers.size() == max_qty)
                return two_steppers;
        }
    }
    return two_steppers;
}

std::unordered_set<NardiCoord> Game::Arbiter::GetTwoSteppers(size_t max_qty)
{
    std::array<std::vector<NardiCoord>, 2> to_search = { 
                    std::vector<NardiCoord>(_g.PlayerGoesByMockDice(0).begin(), _g.PlayerGoesByMockDice(0).end()),
                    std::vector<NardiCoord>(_g.PlayerGoesByMockDice(1).begin(), _g.PlayerGoesByMockDice(1).end())  };
    return GetTwoSteppers(max_qty, to_search);
}

///////////// Updates and Actions /////////////

status_codes Game::Arbiter::OnRoll()
{
    _blockMonitor.Reset();
    _prevMonitor.Reset();

    UpdateMovables();

    return CheckForcedMoves();
}

status_codes Game::Arbiter::OnMove()
{
    return CheckForcedMoves();
}


status_codes Game::Arbiter::OnRemoval()
{
    _prevMonitor.Reset();

    return CheckForcedMoves();
}

void Game::Arbiter::SolidifyBlockMonitor()
{
    _blockMonitor.Solidify();
}

///////////// Forced Moves /////////////

status_codes Game::Arbiter::CheckForcedMoves()
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

Game::RuleExceptionMonitor::RuleExceptionMonitor(Game& g, Arbiter& a) : _g(g), _arb(a), _preconditions(false) {}

bool Game::RuleExceptionMonitor::IsFlagged() const
{
    return _preconditions;
}

void Game::RuleExceptionMonitor::Reset()
{
    _preconditions = false;
}

///////////// FirstMove /////////////

Game::FirstMoveException::FirstMoveException(Game& g, Arbiter& a) : RuleExceptionMonitor(g, a) {}

void Game::PreventionMonitor::Reset()
{
    _completable = TurnCompletable();   // redundant but clear
    _preconditions = PreConditions();
}

bool Game::FirstMoveException::PreConditions()
{
    _preconditions = (_g.turn_number[_g.board.PlayerIdx()] == 1 && (_g.dice[0] == 4 || _g.dice[0] == 6 ) );
    return _preconditions;
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

Game::PreventionMonitor::PreventionMonitor(Game& g, Arbiter& a) : RuleExceptionMonitor(g, a), turn_last_updated({0, 0})
{}

bool Game::PreventionMonitor::PreConditions()
{
    _preconditions = (!_g.doubles_rolled && _arb.CanUseMockDice(0) && _arb.CanUseMockDice(1) && TurnCompletable() );
                                    // if last_turn_checked == turn number return stashed, else...

            // could play both before
            // case: both could play only once from the same coord, one piece - checked in forcing
    return _preconditions;
}

bool Game::PreventionMonitor::MakesSecondStep(const NardiCoord& start) const
{   
    return (_g.mock_board.WellDefinedEnd(start, _g.mock_board.CoordAfterDistance(start, _g.dice[0] + _g.dice[1]) ) 
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
                _g.PlayerGoesByMockDice(!dice_idx).empty() ||      // no pieces move first by other dice
                (
                    _g.PlayerGoesByMockDice(!dice_idx).size() == 1 && 
                    _g.PlayerGoesByMockDice(!dice_idx).contains(start) && 
                    _g.mock_board.MovablePieces(start) == 1
                )   // moving start prevents moving any other piece by other dice
            )   
        )
    );
}

bool Game::PreventionMonitor::TurnCompletable()
{
    if( turn_last_updated.at(_g.board.PlayerIdx()) != _g.turn_number.at(_g.board.PlayerIdx()) )
    {
        auto two_steps = _arb.GetTwoSteppers(1);    // blocking checked
        if(two_steps.size() >= 1)
            _completable = true;

        else
        {
            std::array<size_t, 2> options = { _g.PlayerGoesByMockDice(0).size(), _g.PlayerGoesByMockDice(1).size() };
            bool share_1piece_slot = false;
            for(const auto& coord : _g.PlayerGoesByMockDice(0))
            {
                if(_arb.IllegalBlocking(coord, 0))
                    --options.at(0);
            }
            for(const auto& coord : _g.PlayerGoesByMockDice(1))
            {
                if(_arb.IllegalBlocking(coord, 1))
                    --options.at(1);

                else if( _g.PlayerGoesByMockDice(0).contains(coord) && _g.mock_board.MovablePieces(coord) == 1 )
                    share_1piece_slot = true;
            }

            if(options.at(0) == 0 || options.at(1) == 0 || (options.at(0) == 1 && options.at(1) == 1 && share_1piece_slot))
                _completable = false;
            
            else
                _completable = true;
        }
        turn_last_updated.at(_g.board.PlayerIdx()) = _g.turn_number.at(_g.board.PlayerIdx());
    }
    
    return _completable;
}

    // //std::cout << "doub roll: " << std::boolalpha << _g.doubles_rolled << "\n";
    // //std::cout << "dices usable: " << std::boolalpha << (CanUseMockDice(0) && CanUseMockDice(1)) << "\n";
    // //std::cout << "options: " << options[0] << " " << options[1] << "\n";
    // //std::cout << "steps twice: " << std::boolalpha << (MakesSecondStep(start)) << "\n";
    // //std::cout << "contains start: " << std::boolalpha << (_g.PlayerGoesByMockDice(!dice_idx).contains(start)) << "\n";
    // //std::cout << "only 1 went: " << std::boolalpha << (_g.PlayerGoesByMockDice(!dice_idx).size() == 1) << "\n"; 
        
    // //std::cout << "\n\n";
    // for(const auto& coord : _g.PlayerGoesByMockDice(!dice_idx))
    // {
    //     //std::cout << coord.row << ", " << coord.col << " goes by " << _g.dice[!dice_idx] << "\n";
    // }
    // //std::cout << "\n\n";
    
    // //std::cout << "no piece went by other: " << std::boolalpha << (_g.PlayerGoesByMockDice(!dice_idx).empty()) << "\n"; 
    // //std::cout << "only 1 movable at start: " << std::boolalpha << (_g.board.MovablePieces(start) == 1) << "\n"; 

    // //std::cout << "return value " << (
    //     !_g.doubles_rolled && 
    //     CanUseMockDice(0) && CanUseMockDice(1) && 
    //     options[0] >= 1 && options[1] >= 1  &&  // could play both before
    //     !MakesSecondStep(start) &&                      // cannot continue with this piece
    //     ( 
    //         _g.PlayerGoesByMockDice(!dice_idx).empty() ||      // no pieces move first by other dice
    //         (
    //             _g.PlayerGoesByMockDice(!dice_idx).size() == 1 && 
    //             _g.PlayerGoesByMockDice(!dice_idx).contains(start) && 
    //             _g.board.MovablePieces(start) == 1
    //         )   // moving start prevents moving any other piece by other dice
    //     )   
    // ) << "\n"; // expect true here
    // //std::cout << "\n\n\n\n";


///////////// Bad Block /////////////

Game::BadBlockMonitor::BadBlockMonitor(Game& g, Arbiter& a) : RuleExceptionMonitor(g, a), _isSolidified(false),  _state(block_state::CLEAR)  
{}

void Game::BadBlockMonitor::Reset()
{
    _preconditions = false;
    _blockedAll = false;
    _isSolidified = false;
    _blockLength = 0;
    _unblockers.clear();
}

void Game::BadBlockMonitor::Solidify()
{
    _blockedAll =  BlockingAll();
    _isSolidified = true;
}

bool Game::BadBlockMonitor::BlockingAll()
{
    if(!PreConditions())
        return false;
    
    bool other_player = !_g.mock_board.PlayerIdx();
    int player_sign = _g.mock_board.PlayerSign();
    NardiCoord coord(_g.mock_board.PlayerIdx(), COL - 1);
    unsigned streak = 0;

    for(; coord.OutOfBounds(); coord = _g.mock_board.CoordAfterDistance(coord, -1, other_player))
    {
        if(_g.mock_board.at(coord) * player_sign > 0)
        {
            ++streak;
            if(streak == 6)
                return (BlockageAt(coord) && !PieceAhead());    // BlockageAt will go over this location and set vars accordingly
        }
        else
            streak = 0;
    }

    return false;
}

bool Game::BadBlockMonitor::PreConditions()
{
    _preconditions = _g.mock_board.ReachedEnemyHome().at(! _g.mock_board.PlayerIdx() ) == 0; // not possible once enemy entered home 
    return _preconditions;
}

bool Game::BadBlockMonitor::Illegal(const NardiCoord& start, bool dice_idx)
{
    NardiCoord end = _g.mock_board.CoordAfterDistance(start, _g.dice[dice_idx]);
    if(_isSolidified && _blockedAll)
        return !(_unblockers.contains(start) && abs( _g.mock_board.at(start) )== 1 && 
                _g.mock_board.WellDefinedEnd(start, end) == status_codes::SUCCESS);
    
    return (PreConditions() &&  _g.mock_board.at(end) == 0 && BlockageAt(end) && !PieceAhead() && !WillBeFixable() );
}

bool Game::BadBlockMonitor::BlockageAt(const NardiCoord& end)
{
    _blockLength = 1;   // end coord itself
    
    unsigned n_ahead = 0;
    unsigned n_behind = 0;
    int player_sign = _g.mock_board.PlayerSign();
    bool other_player = !_g.mock_board.PlayerIdx();

    NardiCoord coord = _g.mock_board.CoordAfterDistance(end, 1, other_player);

    // check ahead of end
    for(; !coord.OutOfBounds() && _g.mock_board.at(coord) * player_sign > 0; coord = _g.mock_board.CoordAfterDistance(coord, 1, other_player))
        ++n_ahead;
    
    //check behind end
    coord = _g.mock_board.CoordAfterDistance(end, -1, other_player);
    for(; !coord.OutOfBounds() && _g.mock_board.at(coord) * player_sign > 0; coord = _g.mock_board.CoordAfterDistance(coord, -1, other_player))
        ++n_behind;
    
    _blockStart = _g.mock_board.CoordAfterDistance(end, -n_behind, other_player);
    _blockLength += n_ahead + n_behind;

    return (_blockLength > 6);
}

bool Game::BadBlockMonitor::PieceAhead() 
{
    NardiCoord after_block = _g.mock_board.CoordAfterDistance(_blockStart, _blockLength, !_g.mock_board.PlayerIdx());

    for(; !after_block.OutOfBounds(); after_block = _g.mock_board.CoordAfterDistance(after_block, 1, !_g.mock_board.PlayerIdx()))
    {
        if(_g.mock_board.at(after_block) * _g.mock_board.PlayerSign() < 0){
            _blockedAll = false;
            return true;
        }
    }
    _blockedAll = true;
    return false;
}

bool Game::BadBlockMonitor::WillBeFixable()
{
    if(_blockLength < 6)
        return true;    // should never be called in this condition

    if( !_arb.CanUseMockDice(!_diceAttempting) )
        return false;

    unsigned d = _g.dice[_diceAttempting];
    int i = _blockLength - 6;
    NardiCoord to_move = _g.mock_board.CoordAfterDistance(_blockStart, i, !_g.mock_board.PlayerIdx());

    for(; i < 6; ++i, to_move = _g.mock_board.CoordAfterDistance(to_move, 1, !_g.mock_board.PlayerIdx()))
    {
        if(_g.mock_board.WellDefinedEnd( to_move, _g.mock_board.CoordAfterDistance(to_move, d) ) == status_codes::SUCCESS
            && abs(_g.mock_board.at(to_move)) <= 1 && !_g.mock_board.HeadReuseIssue(to_move)  )
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

/////////////////////////////////////
////////   ForcedHandlers   ////////
///////////////////////////////////

///////////// Base Class /////////////

Game::ForcedHandler::ForcedHandler(Game& g, Arbiter& a) : _arb(a), _g(g) {}

///////////// Doubles /////////////

Game::DoublesHandler::DoublesHandler(Game& g, Arbiter& a) : ForcedHandler(g, a), first_move_checker(g, a) {}

bool Game::DoublesHandler::Is()
{
    if(_g.mock_board != _g.board){
        std::cerr << "!!!!\nmock and board improperly updated, attempted force on mismatched boards - doubles\n!!!!\n";
        return false;
    }
    return (_g.dice[0] == _g.dice[1]);
}

status_codes Game::DoublesHandler::Check()
{
    std::cout << "doubles checking\n";
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
         
        std::cout << "i = " << i << "\n";
        MockFrom(movables.at(i));   // updates steps_left
    }
    // steps left >= 0, need to make all forcing moves
    _g.RealizeMock();

    return status_codes::NO_LEGAL_MOVES_LEFT;
}

void Game::DoublesHandler::MockFrom(NardiCoord start)
{
    std::cout << "mocking from: " << start.row << ", " << start.col << "\n";

    auto [can_go, dest] = _arb.CanMoveByDice(start, 0);
    std::cout << "can go: " ;
    DispErrorCode(can_go);

    while(can_go == status_codes::SUCCESS)
    {
        --steps_left;
        std::cout << "can move from " << start.row << ", " << start.col << " to " << dest.row << ", " << dest.col << "\n";

        if(steps_left < 0)
            return;   // nothing forced
        
        // mock move
        ++_g.times_mockdice_used[_g.mock_board.PlayerIdx()];
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
    if(_g.mock_board != _g.board){
        std::cerr << "!!!!\nmock and board improperly updated, attempted force on mismatched boards - singledice\n!!!!\n";
        return false;
    }
    bool canUse[2] = { _arb.CanUseMockDice(0), _arb.CanUseMockDice(1) };
    return (canUse[0] + canUse[1] == 1 );  // can use 1 but NOT both dice
}

status_codes Game::SingleDiceHandler::Check()
{
    bool active_dice = _arb.CanUseMockDice(1);
    return ForceFromDice(active_dice);
}

status_codes Game::SingleDiceHandler::ForceFromDice(bool active_dice)
{
    auto movables = _arb.GetMovables(active_dice);
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
    if(_g.mock_board != _g.board){
        std::cerr << "!!!!\nmock and board improperly updated, attempted force on mismatched boards - twodice\n!!!!\n";
        return false;
    }
    return (_arb.CanUseMockDice(0) &&  _arb.CanUseMockDice(1));
}

status_codes Game::TwoDiceHandler::Check()
{       // no doubles, no head reuse issues since we can't have made a move yet
    std::cout << "2 dice\n";

    bool max_dice = (_g.dice[1] > _g.dice[0]);

    std::array< std::vector<NardiCoord>, 2 > dice_movables = { _arb.GetMovables(0), _arb.GetMovables(1) };

    std::cout << "options: " << dice_movables[0].size() << " " << dice_movables[1].size() << "\n";

    if (dice_movables.at(0).size() >= 1 && dice_movables.at(1).size() >= 1)       // nothing forced
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
        if( min_start == max_start && _g.board.MovablePieces(max_start) == 1 )
            return _g.ForceMove(max_start, max_dice);
    }

    std::unordered_set<NardiCoord> two_steppers = _arb.GetTwoSteppers(2, dice_movables);
    
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
