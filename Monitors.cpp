#include "Game.h"

using namespace Nardi;

///////////// Turn Completion /////////////

Game::PreventionMonitor::PreventionMonitor(Game& g) : _g(g), turn_last_updated({0, 0})
{}

bool Game::PreventionMonitor::CheckNeeded()
{
    return (!_g.doubles_rolled && _g.arbiter.CanUseDice(0) && _g.arbiter.CanUseDice(1) && TurnCompletable() );
}

bool Game::PreventionMonitor::Illegal(const Coord& start, bool dice_idx)
{
    if(CheckNeeded() && _g.arbiter.LegalMove_2step(start).first != status_codes::SUCCESS)
    {
        _g.MockAndUpdateBlock(start, dice_idx); // moves or removes as needed

        for(Coord coord(_g.board.PlayerIdx(), 0); coord.InBounds(); coord = _g.board.CoordAfterDistance(coord, 1))
        {
            if(_g.arbiter.BoardAndBlockLegal(coord, !dice_idx) == status_codes::SUCCESS)
            {
                _g.UndoMockAndUpdateBlock(start, dice_idx);
                return false;
            }
        }
        
        _g.UndoMockAndUpdateBlock(start, dice_idx);
        return true;
    }
    
    return false;
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
        int steps_left = (_g.arbiter.CanUseDice(0) + _g.arbiter.CanUseDice(1)) * (1 + _g.doubles_rolled);
        _completable = (steps_left <= _g.legal_turns.MaxLen());

        turn_last_updated.at(_g.board.PlayerIdx()) = _g.turn_number.at(_g.board.PlayerIdx());
    }
}

///////////// Bad Block /////////////

Game::BadBlockMonitor::BadBlockMonitor(Game& g) : _g(g), _state(block_state::CLEAR) {}

void Game::BadBlockMonitor::Reset()
{
    _blockedAll = false;
    _blockLength = 0;
    _state = block_state::CLEAR;
}

void Game::BadBlockMonitor::Solidify()
{
    Reset();
    _blockedAll = BlockingAll();

    if(_blockedAll)
        WillBeFixable();    // set block state
    else
        _state = block_state::CLEAR;
}

bool Game::BadBlockMonitor::BlockingAll()
{
    if(!PreConditions())
        return false;
    
    bool other_player = !_g.board.PlayerIdx();
    int player_sign = _g.board.PlayerSign();
    Coord coord(_g.board.PlayerIdx(), COLS - 1);
    unsigned streak = 0;
    
    for(; coord.InBounds(); coord = _g.board.CoordAfterDistance(coord, -1, other_player))
    {
        if(_g.board.at(coord) * player_sign > 0)
        {
            ++streak;
            if(streak == 6 && (BlockageAround(coord) && !PieceAhead()) )  // will go over this location and set vars accordingly
            {
                while (coord.InBounds() && _g.board.at(coord) * player_sign > 0)
                {
                    _blockStart = coord;
                    coord = _g.board.CoordAfterDistance(coord, -1, other_player);
                }     

                return true; 
            }
        }
        else
            streak = 0;
    }

    return false;
}

bool Game::BadBlockMonitor::PreConditions()
{
    return _g.board.ReachedEnemyHome().at(! _g.board.PlayerIdx() ) == 0; // not possible once enemy entered home 
}

bool Game::BadBlockMonitor::Illegal(const Coord& start, bool dice_idx)
{
    if( !_g.SilentMock(start, dice_idx) ) // updates mock dice used
    {
        std::cerr << "unexpected input to block illegal check, no valid silent mock\n";
        return true;
    }
    bool ret = CheckMockedState();
    _g.UndoSilentMock(start, dice_idx);
    return ret;
}

bool Game::BadBlockMonitor::Illegal(const Coord& start, const Coord& end)
{    
    if(!_g.SilentMock(start, end))// updates mock dice used
    {
        std::cout << "unexpected input to block illegal check, no valid silent mock\n";
        return true;
    }
    bool ret = CheckMockedState();
    _g.UndoSilentMock(start, end);
    return ret;
}

bool Game::BadBlockMonitor::CheckMockedState()
{
    if(_blockedAll)
        return !Unblocked() && !WillBeFixable();
    else    // maybe just always take this branch??? `
        return ( BlockingAll() && !PieceAhead() && !WillBeFixable() );
}

bool Game::BadBlockMonitor::Unblocked()
{
    Coord start = _blockStart;
    for(int i = 0; i < 6 && start.InBounds(); ++i, start = _g.board.CoordAfterDistance(start, 1, !_g.board.PlayerIdx()))
    {
        if(_g.board.at(start) * _g.board.PlayerSign() <= 0)  // empty or enemy, should only ever be friendly or empty
            return true;
    }

    return false;
}

bool Game::BadBlockMonitor::Unblocks(const Coord& start, const Coord& end)
{
    if( _g.board.PlayerSign() * _g.board.at(start) != 1 )
        return false;   // not even vacating square

    auto dist = _g.board.GetDistance(_blockStart, start, !_g.board.PlayerIdx()); 

    if(dist >= 0 && dist < 6)   // start is a blocking piece, at most the 6th one
    {
        int blocked_after = _blockLength - dist - 1;

        if(end == _g.board.CoordAfterDistance(_blockStart, _blockLength) )
            ++blocked_after;    // start piece blocks at the end, doesn't actually get subtracted

        return (blocked_after < 6);
    }
    else
        return false;   // start is not one of the blockers, or it's after the 6th so we still have blockage
}

bool Game::BadBlockMonitor::BlockageAround(const Coord& end)
{
    if(_g.board.at(end) * _g.board.PlayerSign() <= 0)
        return false;   //not occupying end

    _blockLength = 1;   // end coord itself
    
    unsigned n_ahead = 0;
    unsigned n_behind = 0;
    int player_sign = _g.board.PlayerSign();
    bool other_player = !_g.board.PlayerIdx();

    Coord coord = _g.board.CoordAfterDistance(end, 1, other_player);

    // check ahead of end
    for(; !coord.OutOfBounds() && _g.board.at(coord) * player_sign > 0; coord = _g.board.CoordAfterDistance(coord, 1, other_player))
        ++n_ahead;
    
    //check behind end
    coord = _g.board.CoordAfterDistance(end, -1, other_player);
    for(; !coord.OutOfBounds() && _g.board.at(coord) * player_sign > 0; coord = _g.board.CoordAfterDistance(coord, -1, other_player))
        ++n_behind;
    
    _blockStart = _g.board.CoordAfterDistance(end, -n_behind, other_player);
    _blockLength += n_ahead + n_behind;
    
    return (_blockLength >= 6);
}

bool Game::BadBlockMonitor::PieceAhead() 
{
    Coord after_block = _g.board.CoordAfterDistance(_blockStart, _blockLength, !_g.board.PlayerIdx());

    for(; !after_block.OutOfBounds(); after_block = _g.board.CoordAfterDistance(after_block, 1, !_g.board.PlayerIdx()))
    {
        if(_g.board.at(after_block) * _g.board.PlayerSign() < 0){
            return true;
        }
    }

    return false;
}

bool Game::BadBlockMonitor::WillBeFixable() // reminder the block is already mocked in, also unblocking can never trigger move prevention
{    
    if(!StillBlocking())
        return true;

    std::pair<int, int> times_usable = {0, 0};
    if(_g.doubles_rolled)
        times_usable.first = 4 - (_g.times_dice_used[0] + _g.times_dice_used[1]);
    else
    {
        times_usable.first  = (_g.times_dice_used[0] == 0) ? 1 : 0;
        times_usable.second = (_g.times_dice_used[1] == 0) ? 1 : 0;
    }

    Coord coord;
    bool success = false;


    auto TryDice = [&](bool d_idx, const Coord& c) -> bool {
        Coord dest = _g.board.CoordAfterDistance(c, _g.dice[d_idx]);
        if(_g.board.WellDefinedEnd(c, dest) == status_codes::SUCCESS)
        {
            _g.SilentMock(c, dest);
            success =  WillBeFixable();
            _g.UndoSilentMock(c, dest);
            if(success)
                return true;
        }

        return false;
    };

    auto PrepReturn = [&](bool fixable) -> bool {
        _state = fixable ? block_state::FIXABLE_BLOCK : block_state::BAD_BLOCK;
        return fixable;
    };

    for( int d = 0; d < 6; ++d )
    {
        coord = _g.board.CoordAfterDistance(_blockStart, d, !_g.board.PlayerIdx());
        if(_g.board.ValidStart(coord) != status_codes::SUCCESS)
            continue;
        if(times_usable.first > 0 && TryDice(0, coord))
            return PrepReturn(true);
        
        if(times_usable.second > 0 && TryDice(1, coord))
            return PrepReturn(true);
    }

    return PrepReturn(false);
}

bool Game::BadBlockMonitor::StillBlocking()
{
    Coord start = _blockStart;
    while( start.InBounds() && _g.board.PlayerSign() * _g.board.at(start) <= 0)
    {
        start = _g.board.CoordAfterDistance(start, 1, !_g.board.PlayerIdx() );
    }   // sometimes this spot is unblocked by willbefixable recursions, so we find the next blocked one
    if(start.OutOfBounds())
        return false;

    Coord coord = _g.board.CoordAfterDistance(start, 1, !_g.board.PlayerIdx() );
    for(int d = 1; d < 6 && coord.InBounds(); ++d)
    {
        if(_g.board.PlayerSign() * _g.board.at(coord) <= 0)
            return false;
        coord = _g.board.CoordAfterDistance(coord, 1, !_g.board.PlayerIdx() );
    }

    return true;
}

Game::BadBlockMonitor::block_state Game::BadBlockMonitor::State() const
{
    return _state;
}