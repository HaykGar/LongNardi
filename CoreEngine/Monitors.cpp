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
        _g.MockMove(start, dice_idx); // moves or removes as needed

        for(Coord coord(_g.board.PlayerIdx(), 0); coord.InBounds(); coord = _g.board.CoordAfterDistance(coord, 1))
        {
            if(_g.arbiter.BoardAndBlockLegal(coord, !dice_idx) == status_codes::SUCCESS)
            {
                _g.UndoMockMove(start, dice_idx);
                return false;
            }
        }
        
        _g.UndoMockMove(start, dice_idx);
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

Game::BadBlockMonitor::BadBlockMonitor(Game& g) : _g(g) {}

void Game::BadBlockMonitor::Reset()
{
    _blockedAll = false;
    _blockLength = 0;
}

bool Game::BadBlockMonitor::PreConditions()
{
    return _g.board.ReachedEnemyHome().at(! _g.board.PlayerIdx() ) == 0; // not possible once enemy entered home 
}

bool Game::BadBlockMonitor::BlockingAll()
{
    if(!PreConditions())
        return false;
    
    bool other_player = !_g.board.PlayerIdx();
    int player_sign = _g.board.PlayerSign();
    Coord coord(_g.board.PlayerIdx(), COLS - 1);
    int streak = 0;
    
    for(; coord.InBounds(); coord = _g.board.CoordAfterDistance(coord, -1, other_player))
    {
        if(_g.board.at(coord) * player_sign > 0)    // friendly occupied
        {
            ++streak;
            if(streak == 6)  // will go over this location and set vars accordingly
            {
                _blockStart = coord;
                _blockLength = streak;
                coord = _g.board.CoordAfterDistance(coord, -1, other_player);

                while (coord.InBounds() && _g.board.at(coord) * player_sign > 0)
                {
                    _blockStart = coord;
                    ++_blockLength;
                    coord = _g.board.CoordAfterDistance(coord, -1, other_player);
                }

                return true; 
            }
        }
        else if(_g.board.at(coord) * player_sign < 0) // enemy occupied
            return false;   // even if there's a block, enemy has a piece after it
        else
            streak = 0;
    }

    return false;
}

bool Game::BadBlockMonitor::IsFixable() // reminder the block is already mocked in, also unblocking can never trigger move prevention
{
    bool available_dice = _g.arbiter.CanUseDice(1);
    if(!_g.arbiter.CanUseDice(available_dice))  // if can't use 1 so 0 "available" - can't use 0 then all dice used up
        return false;

    int total_moves_per_die = _g.doubles_rolled ? 4 : 1;
    int moves_left = total_moves_per_die - _g.times_dice_used[available_dice];

    auto CanFixFrom = [&](const Coord& start) -> bool 
    {
        Coord dest = _g.board.CoordAfterDistance(start, _g.dice[available_dice], _g.board.PlayerIdx());   // in player's direction
        if(abs(_g.board.at(start)) <= moves_left)
        {
            int moves_made = 0;
            for(int i = 0; i < abs(_g.board.at(start)); ++i)
            {
                if(_g.board.ValidStart(start) == status_codes::SUCCESS && _g.board.WellDefinedEnd(start, dest) == status_codes::SUCCESS)
                {
                    _g.MockMove(start, available_dice);
                    ++moves_made;
                }
            }

            bool fixed = (moves_made == moves_left) && !BlockingAll(); 
                // can actually move all of these pieces in the stack and it unblocks

            for(int i = 0; i < moves_made; ++i)
                _g.UndoMockMove(start, available_dice);

            return fixed;
        }

        return false;
    };
    
    for(int d = _blockLength - 6; d < 6; ++d)
    {
        Coord start = _g.board.CoordAfterDistance(_blockStart, d, !_g.board.PlayerIdx());
        if(CanFixFrom(start)) return true;
    }

    return false;
}

bool Game::BadBlockMonitor::Illegal(const Coord& start, bool dice_idx)
{
    if( !_g.MockMove(start, dice_idx) ) // updates mock dice used
    {
        std::cerr << "unexpected input to block illegal check, no valid silent mock\n";
        return true;
    }
    bool ret = CheckMockedState();
    _g.UndoMockMove(start, dice_idx);
    return ret;
}

bool Game::BadBlockMonitor::Illegal(const Coord& start, const Coord& end)
{    
    if(!_g.MockMove(start, end))// updates mock dice used
    {
        std::cout << "unexpected input to block illegal check, no valid silent mock\n";
        return true;
    }
    bool ret = CheckMockedState();
    _g.UndoMockMove(start, end);
    return ret;
}

bool Game::BadBlockMonitor::CheckMockedState()
{
    return ( BlockingAll() && !IsFixable() );
}

// bool Game::BadBlockMonitor::Unblocked()
// {
//     Coord start = _blockStart;
//     for(int i = 0; i < 6 && start.InBounds(); ++i, start = _g.board.CoordAfterDistance(start, 1, !_g.board.PlayerIdx()))
//     {
//         if(_g.board.at(start) * _g.board.PlayerSign() <= 0)  // empty or enemy, should only ever be friendly or empty
//             return true;
//     }

//     return false;
// }

// bool Game::BadBlockMonitor::Unblocks(const Coord& start, const Coord& end)
// {
//     if( _g.board.PlayerSign() * _g.board.at(start) != 1 )
//         return false;   // not even vacating square

//     auto dist = _g.board.GetDistance(_blockStart, start, !_g.board.PlayerIdx()); 

//     if(dist >= 0 && dist < 6)   // start is a blocking piece, at most the 6th one
//     {
//         int blocked_after = _blockLength - dist - 1;

//         if(end == _g.board.CoordAfterDistance(_blockStart, _blockLength) )
//             ++blocked_after;    // start piece blocks at the end, doesn't actually get subtracted

//         return (blocked_after < 6);
//     }
//     else
//         return false;   // start is not one of the blockers, or it's after the 6th so we still have blockage
// }