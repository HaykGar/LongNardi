#include "NardiGame.h"

///////////// Turn Completion /////////////

Game::PreventionMonitor::PreventionMonitor(Game& g) : _g(g), turn_last_updated({0, 0})
{}

bool Game::PreventionMonitor::CheckNeeded()
{
    return (!_g.doubles_rolled && _g.arbiter.CanUseMockDice(0) && _g.arbiter.CanUseMockDice(1) && TurnCompletable() );
            // could play both before
            // case: both could play only once from the same coord, one piece - checked in forcing
}

bool Game::PreventionMonitor::Illegal(const NardiCoord& start, bool dice_idx)
{
    if(CheckNeeded() && !MakesSecondStep(start))
    {
        std::unordered_set<NardiCoord> other_dice_options = _g.PlayerGoesByMockDice(!dice_idx);

        std::unordered_set<NardiCoord>::iterator it = other_dice_options.begin();
        while(it != other_dice_options.end())
        {
            if(_g.arbiter.IllegalBlocking(*it, !dice_idx))    // illegalblocking modifies mock board... careful of dangling
            {
                std::unordered_set<NardiCoord>::iterator old = it;
                ++it;
                other_dice_options.erase(*old);
            }
            else
                ++it;
        }

        return  ( other_dice_options.empty() || ( other_dice_options.size() == 1 && other_dice_options.contains(start) &&  _g.board._mockBoard.MovablePieces(start) == 1 ) );
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
        auto two_steps = _g.arbiter.GetTwoSteppers(1);    // blocking checked
        if(two_steps.size() > 0)
            _completable = true;

        else
        {
            std::array<std::unordered_set<NardiCoord>, 2> options = { _g.PlayerGoesByDice(0), _g.PlayerGoesByDice(1) };

            for(const auto& coord : _g.PlayerGoesByDice(0))
            {
                if(_g.arbiter.IllegalBlocking(coord, 0))
                    options.at(0).erase(coord);
            }
            for(const auto& coord : _g.PlayerGoesByDice(1))
            {
                if(_g.arbiter.IllegalBlocking(coord, 1))
                    options.at(1).erase(coord);
            }

            if( options.at(0).empty() || options.at(1).empty() )
                _completable = false;
            else if (options.at(0).size() == 1 && options.at(1).size() == 1)
            {
                NardiCoord start = *options.at(0).begin();
                _completable = !(start == *options.at(1).begin() && _g.board._realBoard.MovablePieces(start) == 1);
            }            
            else
                _completable = true;
        }
        turn_last_updated.at(_g.board.PlayerIdx()) = _g.turn_number.at(_g.board.PlayerIdx());
    }
}

bool Game::PreventionMonitor::MakesSecondStep(const NardiCoord& start) const
{
    NardiCoord end = _g.board._realBoard.CoordAfterDistance(start, _g.dice[0] + _g.dice[1]);
    return (_g.board._mockBoard.WellDefinedEnd(start, end) == status_codes::SUCCESS && !_g.arbiter.IllegalBlocking(start, end) );
}

///////////// Bad Block /////////////

Game::BadBlockMonitor::BadBlockMonitor(Game& g) : _g(g), _isSolidified(false), _state(block_state::CLEAR) {}

void Game::BadBlockMonitor::Reset()
{
    _blockedAll = false;
    _isSolidified = false;
    _blockLength = 0;
    _state = block_state::CLEAR;
}

void Game::BadBlockMonitor::Solidify()
{
    // std::cout << "solidifying \n\n\n";

    Reset();
    _blockedAll = BlockingAll();
    _isSolidified = true;

    if(_blockedAll)
        WillBeFixable();    // set block state
    else
        _state = block_state::CLEAR;

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
    
    for(; coord.InBounds(); coord = _g.board._realBoard.CoordAfterDistance(coord, -1, other_player))
    {
        if(_g.board._mockBoard.at(coord) * player_sign > 0)
        {
            ++streak;
            if(streak == 6 && (BlockageAround(coord) && !PieceAhead()) )  // CreatesBlockageAt will go over this location and set vars accordingly
            {
                do
                {
                    _blockStart = coord;
                    coord = _g.board._realBoard.CoordAfterDistance(coord, -1, other_player);
                } while (coord.InBounds() && _g.board._mockBoard.at(coord) * player_sign > 0);     

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
    return _g.board._mockBoard.ReachedEnemyHome().at(! _g.board.PlayerIdx() ) == 0; // not possible once enemy entered home 
}

bool Game::BadBlockMonitor::Illegal(const NardiCoord& start, bool dice_idx)
{
    NardiCoord end = _g.board._realBoard.CoordAfterDistance(start, _g.dice[dice_idx]);
    return IllegalEndpoints(start, end);
}

bool Game::BadBlockMonitor::Illegal(const NardiCoord& start, const NardiCoord& end)
{    
    return IllegalEndpoints(start, end);
}

bool Game::BadBlockMonitor::IllegalEndpoints(const NardiCoord& start, const NardiCoord& end)
{
    if(!_g.SilentMock(start, end))// updates mock dice used
    {
        std::cout << "unexpected input to block illegale check, no valid silent mock\n";
        return true;
    }

    bool ret;
    if(_isSolidified && _blockedAll)
        ret = !Unblocked() && !WillBeFixable();
        // ret = !Unblocks(start, end);
    else
        ret = ( PreConditions() && BlockageAround(end) && !PieceAhead() && !WillBeFixable() );

    _g.UndoSilentMock(start, end);
    return ret;
}

bool Game::BadBlockMonitor::Unblocked()
{
    NardiCoord start = _blockStart;
    for(int i = 0; i < 6 && start.InBounds(); ++i, start = _g.board._mockBoard.CoordAfterDistance(start, 1, !_g.board.PlayerIdx()))
    {
        if(_g.board._mockBoard.at(start) * _g.board.PlayerSign() <= 0)  // empty or enemy, should only ever be friendly or empty
            return true;
    }

    return false;
}

bool Game::BadBlockMonitor::Unblocks(const NardiCoord& start, const NardiCoord& end)
{
    if( _g.board.PlayerSign() * _g.board._realBoard.at(start) != 1 )
        return false;   // not even vacating square

    std::cout << "checking if " << start.AsStr() << " unblocks\n";
    auto dist = _g.board._realBoard.GetDistance(_blockStart, start, !_g.board.PlayerIdx()); 

    // std::cout << "block starts at ";
    // _blockStart.Print();

    std::cout << "dist: " << dist << "\n";
    if(dist >= 0 && dist < 6)   // start is a blocking piece, at most the 6th one
    {
        unsigned blocked_after = _blockLength - dist - 1;
        std::cout << "blocked after: " << blocked_after << "\n";

        if(end == _g.board._realBoard.CoordAfterDistance(_blockStart, _blockLength) )
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

    NardiCoord coord = _g.board._realBoard.CoordAfterDistance(end, 1, other_player);

    // check ahead of end
    for(; !coord.OutOfBounds() && _g.board._mockBoard.at(coord) * player_sign > 0; coord = _g.board._realBoard.CoordAfterDistance(coord, 1, other_player))
        ++n_ahead;
    
    //check behind end
    coord = _g.board._realBoard.CoordAfterDistance(end, -1, other_player);
    for(; !coord.OutOfBounds() && _g.board._mockBoard.at(coord) * player_sign > 0; coord = _g.board._realBoard.CoordAfterDistance(coord, -1, other_player))
        ++n_behind;

    // std::cout << "n ahead: " << n_ahead << ", n behind: " << n_behind << "\n";
    
    _blockStart = _g.board._realBoard.CoordAfterDistance(end, -n_behind, other_player);
    // std::cout << "block starts at ";
    // _blockStart.Print();
    _blockLength += n_ahead + n_behind;
    
    return (_blockLength >= 6);
}

bool Game::BadBlockMonitor::PieceAhead() 
{
    NardiCoord after_block = _g.board._realBoard.CoordAfterDistance(_blockStart, _blockLength, !_g.board.PlayerIdx());

    for(; !after_block.OutOfBounds(); after_block = _g.board._realBoard.CoordAfterDistance(after_block, 1, !_g.board.PlayerIdx()))
    {
        if(_g.board._mockBoard.at(after_block) * _g.board.PlayerSign() < 0){
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
        times_usable.first = 4 - (_g.times_mockdice_used[0] + _g.times_mockdice_used[1]);
    else
    {
        times_usable.first  = (_g.times_mockdice_used[0] == 0) ? 1 : 0;
        times_usable.second = (_g.times_mockdice_used[1] == 0) ? 1 : 0;
    }

    NardiCoord coord;
    bool success = false;


    auto TryDice = [&](bool d_idx, const NardiCoord& c) -> bool {

        // std::cout << "trying to move from " << c.AsStr() << "by " << _g.dice[d_idx] << " in willbefixable\n";

        NardiCoord dest = _g.board._mockBoard.CoordAfterDistance(c, _g.dice[d_idx], _g.board.PlayerIdx());
        auto rc = _g.board._mockBoard.WellDefinedEnd(c, dest);
        // DispErrorCode(rc);
        if(rc == status_codes::SUCCESS)
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
        coord = _g.board._mockBoard.CoordAfterDistance(_blockStart, d, !_g.board.PlayerIdx());
        if(_g.board._mockBoard.ValidStart(coord) != status_codes::SUCCESS)
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
    NardiCoord start = _blockStart;
    while( start.InBounds() && _g.board.PlayerSign() * _g.board._mockBoard.at(start) <= 0)
    {
        // std::cout << "shifting start, blockstart is still: "; _blockStart.Print();

        start = _g.board._mockBoard.CoordAfterDistance(start, 1, !_g.board.PlayerIdx() );
    }   // sometimes this spot is unblocked by willbefixable recursions, so we find the next blocked one
    if(start.OutOfBounds())
        return false;

    NardiCoord coord = _g.board._mockBoard.CoordAfterDistance(start, 1, !_g.board.PlayerIdx() );
    for(int d = 1; d < 6 && coord.InBounds(); ++d)
    {
        if(_g.board.PlayerSign() * _g.board._mockBoard.at(coord) <= 0)
            return false;
        coord = _g.board._mockBoard.CoordAfterDistance(coord, 1, !_g.board.PlayerIdx() );
    }

    return true;
}

Game::BadBlockMonitor::block_state Game::BadBlockMonitor::State() const
{
    return _state;
}