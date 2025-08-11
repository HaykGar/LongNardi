#include "NardiGame.h"
#include "ReaderWriter.h"


///////////////////////////
////////   Game   ////////
/////////////////////////

///////////// Constructor and intialization /////////////

Game::Game(int rseed) : board(*this), rng(rseed), dist(1, 6), dice({0, 0}), times_dice_used({0, 0}),
                        doubles_rolled(false), turn_number({0, 0}), rw(nullptr), arbiter(*this)
{} 

Game::Game() :  board(*this), rng(std::random_device{}()), dist(1, 6), dice({0, 0}), times_dice_used({0, 0}), 
                doubles_rolled(false), turn_number({0, 0}), rw(nullptr), arbiter(*this)
{}

void Game::AttachReaderWriter(ReaderWriter* r)
{   rw = r;   }

///////////// Getters /////////////

const NardiBoard& Game::GetBoardRef() const
{
    return board._realBoard;
}


int Game::GetDice(bool idx) const
{   return dice[idx];   }

const ReaderWriter* Game::GetConstRW() 
{   return rw;   }

const std::unordered_set<NardiCoord>& Game::PlayerGoesByMockDice(bool dice_idx) const
{   return board._mockBoard.PlayerGoesByDist(dice[dice_idx]);   }

const std::unordered_set<NardiCoord>& Game::PlayerGoesByDice(bool dice_idx) const
{   
    return board._realBoard.PlayerGoesByDist(dice[dice_idx]);   
}

NardiCoord Game::PlayerHead() const
{   return {board.PlayerIdx(), 0};   }

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

void Game::UseDice(bool idx, int n)
{   
    times_dice_used[idx] += n;  
    times_mockdice_used = times_dice_used;
}

status_codes Game::OnRoll()
{
    if(rw)
        rw->AnimateDice();

    IncrementTurnNumber();  // starts at 0

    board.ResetMock();
    
    return arbiter.OnRoll();
}

status_codes Game::TryStart(const NardiCoord& start)
{
    board.ResetMock();
    //std::cout << "called TryStart\n";
    // During endgame, if it's a valid start just check if there's a forced move from here, will streamline play
    auto s = board._realBoard.ValidStart(start);
    // std::cout << "start valid?\n";
    // DispErrorCode(s);
    return s;
}

status_codes Game::TryFinishMove(const NardiCoord& start, const NardiCoord& end) // assumes start already checked
{
    std::cout << "trying to complete move from " << start.AsStr() << " to " << end.AsStr() << "\n";
    board.ResetMock();

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
    std::cout << "trying to move by dice " << dice[dice_idx] << " from " << start.AsStr() << "\n";
    board.ResetMock(); // could be redundant, but safe
    
    auto [result, dest]  = arbiter.CanFinishByDice(start, dice_idx);
    
    std::cout << "res: \n";
    DispErrorCode(result);

    if(result != status_codes::SUCCESS )
        return result;
    
    else
        return MakeMove(start, dice_idx);
}

status_codes Game::MakeMove(const NardiCoord& start, const NardiCoord& end)
{
    board.Move(start, end);

    if(rw)
        rw->ReAnimate();

    return arbiter.OnMove();
}

void Game::MockAndUpdate(const NardiCoord& start, const NardiCoord& end)
{
    int d = board._realBoard.GetDistance(start, end);
    if(d == dice[0])
        ++times_mockdice_used[0];
    else if(d == dice[1])
        ++times_mockdice_used[1];
    else if(d == dice[0] + dice[1])
    {
        ++times_mockdice_used[0];
        ++times_mockdice_used[1];
    }
    else if(doubles_rolled && d % dice[0] == 0 && arbiter.CanUseMockDice(0, d / dice[0]) )
        times_mockdice_used[0] += d / dice[0];
    else
        std::cout << "!!!!\nunexpected input to MockAndUpdate\n";
    
    
    board.Mock_Move(start, end);
    arbiter.OnMockChange();  // not sure if this is needed
}

void Game::UndoMockAndUpdate(const NardiCoord& start, const NardiCoord& end)
{
    int d = board._realBoard.GetDistance(start, end);
    if(d == dice[0])
        --times_mockdice_used[0];
    else if(d == dice[1])
        --times_mockdice_used[1];
    else if(d == dice[0] + dice[1])
    {
        --times_mockdice_used[0];
        --times_mockdice_used[1];
    }
    else if(doubles_rolled && d % dice[0] == 0 && arbiter.CanUseMockDice(0, d / dice[0]) )
        times_mockdice_used[0] -= d / dice[0];
    else
        std::cout << "!!!!\nunexpected input to MockAndUpdate\n";

    board.Mock_UndoMove(start, end);
    arbiter.OnMockChange();
}

void Game::MockAndUpdateByDice(const NardiCoord& start, bool dice_idx)
{
    ++times_mockdice_used[dice_idx];
    if(board._realBoard.CurrPlayerInEndgame() && arbiter.DiceRemovesPiece(start, dice_idx))
        board.Mock_Remove(start);
    else
        board.Mock_Move(start, board._realBoard.CoordAfterDistance(start, dice[dice_idx]));
    
    arbiter.OnMockChange();
}

void Game::ResetMock()
{
    // std::cout << "game - reset mock\n";
    // std::cout << "current dice are " << dice[0] << " " << dice[1] <<"\n";
    board.ResetMock();
    arbiter.OnMockChange();
}

void Game::RealizeMock()
{
    board.RealizeMock();
    arbiter.OnMockChange();
}

status_codes Game::MakeMove(const NardiCoord& start, bool dice_idx)
{
    std::cout << "moving from " << start.AsStr() << " by " << dice[dice_idx] << "\n";
    UseDice(dice_idx);
    if(arbiter.DiceRemovesPiece(start, dice_idx))
        return RemovePiece(start);
    else
        return MakeMove(start, board._realBoard.CoordAfterDistance(start, dice[dice_idx]) );
}

status_codes Game::RemovePiece(const NardiCoord& start)
{
    board.Remove(start);

    if(rw)
        rw->ReAnimate();
    if(GameIsOver())
        return status_codes::NO_LEGAL_MOVES_LEFT;

    return arbiter.OnRemoval();
}

bool Game::GameIsOver() const 
{   return (board._realBoard.PiecesLeft().at(0) == 0 || board._realBoard.PiecesLeft().at(1) == 0);   }

void Game::SwitchPlayer()
{   
    board.SwitchPlayer();
}

void Game::IncrementTurnNumber()
{   ++turn_number[board.PlayerIdx()];   }


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
    // std::cout << "called without problems - canmovebydice\n";

    status_codes can_start = _g.board._mockBoard.ValidStart(start);
    if(can_start != status_codes::SUCCESS)
        return {can_start, {} };
    else
        return CanFinishByDice(start, dice_idx);
}

std::pair<status_codes, NardiCoord> Game::Arbiter::CanFinishByDice(const NardiCoord& start, bool dice_idx)
{
    if(!CanUseMockDice(dice_idx))
        return {status_codes::DICE_USED_ALREADY, {} };

    for( const auto& coord : _movables.at(dice_idx))
    {
        if(coord == start)
            return { status_codes::SUCCESS, _g.board._realBoard.CoordAfterDistance(start, _g.dice[dice_idx]) };
    }

    // std::cout << "not found in movables, tried " << start.AsStr() << " with dice " << _g.dice[dice_idx] << "\n";
    //std::cout << "dice usable\n";

    NardiCoord final_dest = _g.board._realBoard.CoordAfterDistance(start, _g.dice[dice_idx]);
    status_codes result = _g.board._mockBoard.WellDefinedEnd(start, final_dest);

    //std::cout << "board legal?\n";
    //// DispErorrCode(result);
    if (result == status_codes::SUCCESS)
    {
        if (_blockMonitor.Illegal(start, dice_idx))
        {
            std::cout << "illegal block\n";
            return {status_codes::BAD_BLOCK, {} };
        }
            
        else if(_prevMonitor.Illegal(start, dice_idx))
            return {status_codes::PREVENTS_COMPLETION, {} }; 
    }
    else if( DiceRemovesPiece(start, dice_idx) )
        return {status_codes::SUCCESS, {}};
    
    return {result, final_dest};
}

bool Game::Arbiter::DiceRemovesPiece(const NardiCoord& start, bool dice_idx)
{
    if(!_g.board._mockBoard.CurrPlayerInEndgame())
        return false;
    
    int pos_from_end = COL - start.col;
    return (pos_from_end == _g.dice[dice_idx] ||  // dice val exactly
            (pos_from_end == _g.board._mockBoard.MaxNumOcc().at(_g.board.PlayerIdx()) && _g.dice[dice_idx] > pos_from_end) );  
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
    int d = _g.board._realBoard.GetDistance(start, end);

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

    NardiCoord end = _g.board._realBoard.CoordAfterDistance(start, _g.dice[0] + _g.dice[1]);
    status_codes second_step = _g.board._mockBoard.WellDefinedEnd(start, end);

    if(second_step != status_codes::SUCCESS)
        return {second_step, end };
    else if(_blockMonitor.Illegal(start, end)){
        return {status_codes::BAD_BLOCK, end};
    }
        
    bool first_dice = 0;

    NardiCoord mid = _g.board._realBoard.CoordAfterDistance(start, _g.dice[first_dice]);
    status_codes status = _g.board._mockBoard.WellDefinedEnd(start, mid);

    if(status != status_codes::SUCCESS)
    {
        if(_g.doubles_rolled)
            return {status_codes::NO_PATH, end }; 
        
        first_dice = !first_dice;   // try both dice to get to a midpoint, no need if doubles
        mid = _g.board._realBoard.CoordAfterDistance(start, _g.dice[first_dice]);
        status = _g.board._mockBoard.WellDefinedEnd(start, mid);

        if(status != status_codes::SUCCESS)
            return {status_codes::NO_PATH, end }; 
    }
    // valid midpoint reached
    return { status, end };
}

bool Game::Arbiter::IllegalBlocking(const NardiCoord& start, bool idx)
{
    return _blockMonitor.Illegal(start, idx);
}

bool Game::Arbiter::IllegalBlocking(const NardiCoord& start, const NardiCoord& end)
{
    return _blockMonitor.Illegal(start, end);
}

void Game::Arbiter::UpdateMovables()
{
    _movables = {};

    if(CanUseMockDice(0))
    {
        std::vector<NardiCoord> candidates(_g.PlayerGoesByMockDice(0).begin(), _g.PlayerGoesByMockDice(0).end());
        // std::cout << "updating movables for dice: " << _g.dice[0] << "\n";

        for(const auto& coord : candidates)
        {
            // std::cout << "coord considered: " << coord.row << ", " << coord.col << "\n";

            if(CanMoveByDice(coord, 0).first == status_codes::SUCCESS )   // checks prevention as well
            {
                _movables.at(0).push_back(coord);
                // std::cout << "works with dice 0\n";
            }
            // else
            // {
            //     continue;
            //     // DispErorrCode(CanMoveByDice(coord, 0).first);
            //     // std::cout << "doesn't work\n";
            // }
                
        }
    }
    if(CanUseMockDice(1))
    {
        std::vector<NardiCoord> candidates(_g.PlayerGoesByMockDice(1).begin(), _g.PlayerGoesByMockDice(1).end());
        // std::cout << "updating movables for dice: " << _g.dice[1] << "\n";
        for( const auto& coord : candidates )
        {
            // std::cout << "coord considered: " << coord.row << ", " << coord.col << "\n";
            if(CanMoveByDice(coord, 1).first == status_codes::SUCCESS )
                _movables.at(1).push_back(coord);
        }
    }
}

const std::vector<NardiCoord>& Game::Arbiter::GetMovables(bool idx)
{
    return _movables.at(idx);
}

std::unordered_set<NardiCoord> Game::Arbiter::GetTwoSteppers(size_t max_qty, const std::array<std::vector<NardiCoord>, 2>& to_search)
{
    std::unordered_set<NardiCoord> two_steppers;

    // std::cout << "in 2steppers, searching among: \n";
    for (const auto& c : to_search.at(0))
        c.Print();

    for (const auto& c : to_search.at(1))
        c.Print();


    for(const auto& start : to_search.at(0))
    {
        auto [can_go, dest] = LegalMove_2step(start);
        // std:: cout << "tried to move to " << dest.AsStr() << "\n";
        DispErrorCode(can_go);
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
        // std:: cout << "tried to move to " << dest.AsStr() << "\n";

        DispErrorCode(can_go);

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
    // std::cout << "called onchange from OnMove\n";

    _g.ResetMock();
    return CheckForcedMoves();
}


status_codes Game::Arbiter::OnRemoval()
{
    // std::cout << "called onchange from OnRemoval\n";
    _g.ResetMock();
    return CheckForcedMoves();
}

void Game::Arbiter::OnMockChange()
{
    _blockMonitor.Solidify();
    UpdateMovables();
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