#include "NardiGame.h"

////////////////////////////////////////////
////////   RuleExceptionMonitors   ////////
//////////////////////////////////////////

/*

NO CALLS TO MockAndUpdate else seg fault - the update step depends on the monitors.

*/

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

    Reset();

    return status_codes::NO_LEGAL_MOVES_LEFT;
}

///////////// Turn Completion /////////////

Game::PreventionMonitor::PreventionMonitor(Game& g, Arbiter& a) : RuleExceptionMonitor(g, a), turn_last_updated({0, 0})
{}

void Game::PreventionMonitor::Reset()
{
    _preconditions = PreConditions();
}

bool Game::PreventionMonitor::PreConditions()
{
    _preconditions = (!_g.doubles_rolled && _arb.CanUseMockDice(0) && _arb.CanUseMockDice(1) && TurnCompletable() );
                                    // if last_turn_checked == turn number return stashed, else...

            // could play both before
            // case: both could play only once from the same coord, one piece - checked in forcing
    return _preconditions;
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
                    _g.board.Mock_MovablePieces(start) == 1
                )   // moving start prevents moving any other piece by other dice
            )   
        )
    );
}

bool Game::PreventionMonitor::TurnCompletable()
{
    SetCompletable();
    return _completable;
}

void Game::PreventionMonitor::SetCompletable()
{
    if( turn_last_updated.at(_g.board.PlayerIdx()) != _g.turn_number.at(_g.board.PlayerIdx()) )
    {
        auto two_steps = _arb.GetTwoSteppers(1);    // blocking checked
        if(two_steps.size() > 0)
            _completable = true;

        else
        {
            std::array<size_t, 2> options = { _g.PlayerGoesByDice(0).size(), _g.PlayerGoesByDice(1).size() };
            bool share_1piece_slot = false;
            for(const auto& coord : _g.PlayerGoesByDice(0))
            {
                if(_arb.IllegalBlocking(coord, 0))
                    --options.at(0);
            }
            for(const auto& coord : _g.PlayerGoesByDice(1))
            {
                if(_arb.IllegalBlocking(coord, 1))
                    --options.at(1);

                else if( _g.PlayerGoesByDice(0).contains(coord) && _g.board. MovablePieces(coord) == 1 )
                    share_1piece_slot = true;
            }

            if(options.at(0) == 0 || options.at(1) == 0 || (options.at(0) == 1 && options.at(1) == 1 && share_1piece_slot))
                _completable = false;
            
            else
                _completable = true;
        }
        turn_last_updated.at(_g.board.PlayerIdx()) = _g.turn_number.at(_g.board.PlayerIdx());
    }
}

bool Game::PreventionMonitor::MakesSecondStep(const NardiCoord& start) const
{
    return (_g.board.Mock_WellDefinedEnd(start, _g.board.CoordAfterDistance(start, _g.dice[0] + _g.dice[1]) ) == status_codes::SUCCESS);
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

Game::BadBlockMonitor::BadBlockMonitor(Game& g, Arbiter& a) :   RuleExceptionMonitor(g, a), _isSolidified(false),  
                                                                _state(block_state::CLEAR) {}

void Game::BadBlockMonitor::Reset()
{
    _preconditions = false;
    _blockedAll = false;
    _isSolidified = false;
    _blockLength = 0;
    _state = block_state::CLEAR;
}

void Game::BadBlockMonitor::Solidify()
{
    // std::cout << "solidifying \n\n\n";

    PreConditions();
    _blockedAll =  BlockingAll();
    _isSolidified = true;

    // std::cout << "blocking all? " << std::boolalpha << _blockedAll << "\n";
    // std::cout << "block length? " << _blockLength << "\n";
}

bool Game::BadBlockMonitor::BlockingAll()
{
    if(!PreConditions())
        return false;
    
    bool other_player = !_g.board.PlayerIdx();
    int player_sign = _g.board.PlayerSign();
    NardiCoord coord(_g.board.PlayerIdx(), COL - 1);
    unsigned streak = 0;
    
    for(; coord.InBounds(); coord = _g.board.CoordAfterDistance(coord, -1, other_player))
    {
        if(_g.board.Mock_at(coord) * player_sign > 0)
        {
            ++streak;
            if(streak == 6 && (BlockageAround(coord) && !PieceAhead()) )  // CreatesBlockageAt will go over this location and set vars accordingly
                return true;   
        }
        else
            streak = 0;
    }

    return false;
}

bool Game::BadBlockMonitor::PreConditions()
{
    _preconditions = _g.board.Mock_ReachedEnemyHome().at(! _g.board.PlayerIdx() ) == 0; // not possible once enemy entered home 
    return _preconditions;
}

bool Game::BadBlockMonitor::Illegal(const NardiCoord& start, bool dice_idx)
{
    NardiCoord end = _g.board.CoordAfterDistance(start, _g.dice[dice_idx]);
    return IllegalEndpoints(start, end);
}

bool Game::BadBlockMonitor::Illegal(const NardiCoord& start, const NardiCoord& end)
{    
    return IllegalEndpoints(start, end);
}

bool Game::BadBlockMonitor::IllegalEndpoints(const NardiCoord& start, const NardiCoord& end)
{
    if(_isSolidified && _blockedAll)
    {
        return !Unblocks(start, end);
    }
    else
    {
        _g.board.Mock_Move(start, end);
        bool result = ( PreConditions() && BlockageAround(end) && !PieceAhead() && !WillBeFixable() );

        _g.board.Mock_UndoMove(start, end);
        return result;
    }
}

bool Game::BadBlockMonitor::Unblocks(const NardiCoord& start, const NardiCoord& end)
{
    // std::cout << "checking if " << start.AsStr() << " unblocks\n";
    auto dist = _g.board.GetDistance(_blockStart, start); 
    if(dist >= 0 && dist < 6)   // start is a blocking piece, at most the 6th one
    {
        unsigned blocked_after = _blockLength - dist - 1;

        if(end == _g.board.CoordAfterDistance(_blockStart, _blockLength) )
            ++blocked_after;    // start piece blocks at the end, doesn't actually get subtracted

        return (blocked_after < 6);
    }
    else
        return false;   // start is not one of the blockers, or it's after the 6th so we still have blockage
}

bool Game::BadBlockMonitor::BlockageAround(const NardiCoord& end)
{
    _blockLength = 1;   // end coord itself
    
    unsigned n_ahead = 0;
    unsigned n_behind = 0;
    int player_sign = _g.board.PlayerSign();
    bool other_player = !_g.board.PlayerIdx();

    NardiCoord coord = _g.board.CoordAfterDistance(end, 1, other_player);

    // check ahead of end
    for(; !coord.OutOfBounds() && _g.board.Mock_at(coord) * player_sign > 0; coord = _g.board.CoordAfterDistance(coord, 1, other_player))
        ++n_ahead;
    
    //check behind end
    coord = _g.board.CoordAfterDistance(end, -1, other_player);
    for(; !coord.OutOfBounds() && _g.board.Mock_at(coord) * player_sign > 0; coord = _g.board.CoordAfterDistance(coord, -1, other_player))
        ++n_behind;

    // std::cout << "n ahead: " << n_ahead << ", n behind: " << n_behind << "\n";
    
    _blockStart = _g.board.CoordAfterDistance(end, -n_behind, other_player);
    _blockLength += n_ahead + n_behind;
    
    return (_blockLength >= 6);
}

bool Game::BadBlockMonitor::PieceAhead() 
{
    NardiCoord after_block = _g.board.CoordAfterDistance(_blockStart, _blockLength, !_g.board.PlayerIdx());

    for(; !after_block.OutOfBounds(); after_block = _g.board.CoordAfterDistance(after_block, 1, !_g.board.PlayerIdx()))
    {
        if(_g.board.Mock_at(after_block) * _g.board.PlayerSign() < 0){
            return true;
        }
    }

    return false;
}

bool Game::BadBlockMonitor::WillBeFixable()
{    
    // std::cout << "checking if block is fixable\n";
    if(_blockLength < 6)
        return true;    // should never be called in this condition

    if( _g.times_mockdice_used[0] + _g.times_mockdice_used[1] >= (_g.doubles_rolled + 1) * 2 )
    {   // no more dice to use
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