#include "Game.h"

using namespace Nardi;

///////////// Turn Completion /////////////

Game::PreventionMonitor::PreventionMonitor(Game& g) : _g(g)
{}

bool Game::PreventionMonitor::CheckNeeded()
{
    return (!_g.doubles_rolled && _g.arbiter.CanUseDice(0) && _g.arbiter.CanUseDice(1) && TurnCompletable() );
}

bool Game::PreventionMonitor::TurnCompletable()
{
    int steps_left = (_g.arbiter.CanUseDice(0) + _g.arbiter.CanUseDice(1)) * (1 + _g.doubles_rolled);
    return (steps_left <= _g.legal_turns.MaxLen());
}

bool Game::PreventionMonitor::Illegal(const Coord& start, bool dice_idx)
{
    if(CheckNeeded())
    {
        _g.MockMove(start, dice_idx); // moves or removes as needed

        for(Coord coord(_g.board.PlayerIdx(), 0); coord.InBounds(); coord = _g.board.CoordAfterDistance(coord, 1))
        {
            if(_g.arbiter.BoardAndBlockLegal(coord, !dice_idx) == status_codes::SUCCESS)
            {
                _g.UndoMove(start, dice_idx);
                return false;
            }
        }
        
        _g.UndoMove(start, dice_idx);
        return true;
    }
    
    return false;
}

///////////// Bad Block /////////////

Game::BadBlockMonitor::BadBlockMonitor(Game& g) : _g(g) {}

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
    int moves_left;

    if(_g.doubles_rolled)
        moves_left = total_moves_per_die - _g.times_dice_used[0] - _g.times_dice_used[1];
    else
        moves_left = total_moves_per_die - _g.times_dice_used[available_dice];

    if(moves_left < 1)
        return false;

    auto CanFixFrom = [&](const Coord& start) -> bool 
    {
        Coord dest = _g.board.CoordAfterDistance(start, _g.dice[available_dice], _g.board.PlayerIdx());   // in player's direction
        int n_pieces = abs(_g.board.at(start));
        if(n_pieces <= moves_left)
        {
            int moves_made = 0;
            for(int i = 0; i < n_pieces; ++i)
            {
                if(_g.board.ValidStart(start) == status_codes::SUCCESS && _g.board.WellDefinedEnd(start, dest) == status_codes::SUCCESS)
                {
                    _g.MockMove(start, available_dice);
                    ++moves_made;
                }
                else
                    break;
            }

            bool fixed = !BlockingAll(); 

            for(int i = 0; i < moves_made; ++i)
                _g.UndoMove(start, available_dice);

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
    _g.UndoMove(start, dice_idx);
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
    _g.UndoMove(start, end);
    return ret;
}

bool Game::BadBlockMonitor::CheckMockedState()
{
    return ( BlockingAll() && !IsFixable() );
}