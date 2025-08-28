#include "Game.h"
#include "ReaderWriter.h"

using namespace Nardi;

///////////////////////////
////////   Game   ////////
/////////////////////////

///////////// Constructor and intialization /////////////

Game::Game(int rseed) : board(*this), rng(rseed), dist(1, 6), dice({0, 0}), times_dice_used({0, 0}), turn_number({0, 0}), 
                        doubles_rolled(false), rw(nullptr), arbiter(*this), legal_turns(*this, arbiter)
{} 

Game::Game() :  board(*this), rng(std::random_device{}()), dist(1, 6), dice({0, 0}), doubles_rolled(false), 
                times_dice_used({0, 0}), turn_number({0, 0}), rw(nullptr), arbiter(*this), legal_turns(*this, arbiter)
{}

void Game::AttachReaderWriter(ReaderWriter* r)
{   rw = r;   }

///////////// Getters /////////////

const Board& Game::GetBoardRef() const
{
    return board._realBoard;
}

int Game::GetDice(bool idx) const
{   return dice[idx];   }

const ReaderWriter* Game::GetConstRW() 
{   return rw;   }

const std::unordered_set<Coord>& Game::PlayerGoesByMockDice(bool dice_idx) const
{   return board._mockBoard.PlayerGoesByDist(dice[dice_idx]);   }

const std::unordered_set<Coord>& Game::PlayerGoesByDice(bool dice_idx) const
{   
    return board._realBoard.PlayerGoesByDist(dice[dice_idx]);   
}

Coord Game::PlayerHead() const
{   return {board.PlayerIdx(), 0};   }

const auto Game::ViewAllLegalMoveSeqs() const
{
    return legal_turns.BrdsToSeqs() | std::views::values;
}

const std::unordered_map<std::string, MoveSequence>& Game::GetBoards2Seqs() const
{
    return legal_turns.BrdsToSeqs();
}

///////////// Gameplay /////////////

status_codes Game::RollDice() // important to force this only once per turn in controller, no explicit safeguard here
{    
    SetDice(dist(rng), dist(rng));
    return OnRoll();
}

status_codes Game::OnRoll()
{
    AnimateDice();
    IncrementTurnNumber();  // starts at 0
    board.ResetMock();

    first_move_exception = false;
    maxdice_exception = false;
    return arbiter.OnRoll();
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

status_codes Game::TryStart(const Coord& start)
{
    //std::cout << "called TryStart\n";
    if(first_move_exception && start == Coord(board.PlayerIdx(), 0) && board._realBoard.at(start) > PIECES_PER_PLAYER - 2)
        return status_codes::SUCCESS;

    auto s = board._realBoard.ValidStart(start);
    // std::cout << "start valid?\n";
    // DispErrorCode(s);
    return s;
}

status_codes Game::TryFinishMove(const Coord& start, const Coord& end) // assumes start already checked
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

status_codes Game::TryFinishMove(const Coord& start, bool dice_idx)
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

status_codes Game::MakeMove(const Coord& start, const Coord& end)
{
    board.Move(start, end);
    ReAnimate();
    ResetMock();

    return arbiter.CheckForcedMoves();
}

status_codes Game::MakeMove(const Coord& start, bool dice_idx)
{
    UseDice(dice_idx);
    if(arbiter.DiceRemovesFrom(start, dice_idx))
        return RemovePiece(start);
    else
        return MakeMove(start, board._realBoard.CoordAfterDistance(start, dice[dice_idx]) );
}

status_codes Game::RemovePiece(const Coord& start)
{
    board.Remove(start);
    ReAnimate();
    if(GameIsOver())
        return status_codes::NO_LEGAL_MOVES_LEFT;

    ResetMock();
    return arbiter.CheckForcedMoves();
}

bool Game::SilentMock(const Coord& start, const Coord& end)
{
    if(end.OutOfBounds() || start.OutOfBounds())
    {
        std::cout << "attempted to mock out of bounds, if attempting removal use the coord, dice overload \n";
        return false;
    }
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
    {
        std::cout << "!!!!\nunexpected input to SilentMock\n";
        return false;
    }

    board.Mock_Move(start, end);
    return true;
}


bool Game::UndoSilentMock(const Coord& start, const Coord& end)
{
    if(end.OutOfBounds() || start.OutOfBounds())
    {
        std::cout << "attempted to mock out of bounds, if attempting removal use the coord, dice overload \n";
        return false;
    }
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
    {
        std::cout << "!!!!\nunexpected input to UndoSilentMock\n";
        return false;
    }
    board.Mock_UndoMove(start, end);
    return true;
}

bool Game::SilentMock(const Coord& start, bool dice_idx)
{
    Coord dest = board._realBoard.CoordAfterDistance(start, dice[dice_idx]);

    if(board._mockBoard.CurrPlayerInEndgame() && arbiter.DiceRemovesFrom(start, dice_idx))
        board.Mock_Remove(start);
    else if(board._mockBoard.WellDefinedEnd(start, dest) == status_codes::SUCCESS)
        board.Mock_Move(start, dest);
    else
        return false;

    ++times_mockdice_used[dice_idx];
    return true;
}

bool Game::UndoSilentMock(const Coord& start, bool dice_idx)
{
    Coord dest = board._realBoard.CoordAfterDistance(start, dice[dice_idx]);

    if(arbiter.DiceRemovesFrom(start, dice_idx))
        board.Mock_UndoRemove(start);
    else if(board._mockBoard.WellDefinedEnd(start, dest) == status_codes::SUCCESS)
        board.Mock_UndoMove(start, board._realBoard.CoordAfterDistance(start, dice[dice_idx]));
    else
        return false;

    --times_mockdice_used[dice_idx];
    return true;
}

void Game::MockAndUpdateBlock(const Coord& start, bool dice_idx)
{
    SilentMock(start, dice_idx);
    arbiter.SolidifyBlock();
}

void Game::UndoMockAndUpdateBlock(const Coord& start, bool dice_idx)
{
    UndoSilentMock(start, dice_idx);
    arbiter.SolidifyBlock();
}

void Game::ResetMock()
{
    board.ResetMock();
    arbiter.SolidifyBlock();
}

void Game::RealizeMock()
{
    board.RealizeMock();
    arbiter.SolidifyBlock();
}


bool Game::GameIsOver() const 
{   return (board._realBoard.PiecesLeft().at(0) == 0 || board._realBoard.PiecesLeft().at(1) == 0);   }

void Game::SwitchPlayer()
{   
    board.SwitchPlayer();
}

void Game::IncrementTurnNumber()
{   ++turn_number[board.PlayerIdx()];   }

void Game::ReAnimate()
{
    if(rw)
        rw->ReAnimate();
}

void Game::AnimateDice()
{
    if(rw)
        rw->AnimateDice();
}


////////////////////////////
////////   Arbiter ////////
//////////////////////////

///////////// Constructor /////////////

Game::Arbiter::Arbiter(Game& g) :  _g(g), _prevMonitor(_g), _blockMonitor(_g)
{}

///////////// Legality /////////////

std::pair<status_codes, Coord> Game::Arbiter::CanMoveByDice(const Coord& start, bool dice_idx)
{
    status_codes can_start = _g.board._mockBoard.ValidStart(start);
    if(can_start != status_codes::SUCCESS)
        return {can_start, {} };
    else
        return CanFinishByDice(start, dice_idx);
}

status_codes Game::Arbiter::BoardAndBlockLegal(const Coord& start, bool dice_idx)
{
    if(_g.maxdice_exception)
    {
        auto& b2s = _g.legal_turns.BrdsToSeqs();
        for(const auto& [k, v] : b2s)
        {
            if(v.at(0)._from == start && v.at(0)._diceIdx == dice_idx)
                return status_codes::SUCCESS;
        }

        return status_codes::MISC_FAILURE;
    }
    else if(_g.first_move_exception)
    {
        if(start == Coord(_g.board.PlayerIdx(), 0) && abs(_g.board._mockBoard.at(start)) > PIECES_PER_PLAYER - 2)
            return status_codes::SUCCESS;
        else if (   _g.dice[0] == 4 && start == Coord(_g.board.PlayerIdx(), 4) && 
                    abs(_g.board._mockBoard.at(start)) * _g.board.PlayerSign() > 0  )
            return status_codes::SUCCESS;
        else
            return status_codes::MISC_FAILURE;
    }

    status_codes can_start = _g.board._mockBoard.ValidStart(start);
    if(can_start != status_codes::SUCCESS)
        return can_start;
    else if(!CanUseMockDice(dice_idx))
        return status_codes::DICE_USED_ALREADY;

    Coord final_dest = _g.board._realBoard.CoordAfterDistance(start, _g.dice[dice_idx]);
    status_codes result = _g.board._mockBoard.WellDefinedEnd(start, final_dest);

    if(result != status_codes::SUCCESS)
    {
        if( DiceRemovesFrom(start, dice_idx))
        {
            if(_blockMonitor.Illegal(start, dice_idx))
                return status_codes::BAD_BLOCK;
            else
                return status_codes::SUCCESS;
        }
        else
            return result;
    }
    else if (_blockMonitor.Illegal(start, dice_idx))
        return status_codes::BAD_BLOCK;
    else
        return result;  // success
}

std::pair<status_codes, Coord> Game::Arbiter::CanFinishByDice(const Coord& start, bool dice_idx)
{    
    status_codes result = BoardAndBlockLegal(start, dice_idx);
    //std::cout << "board and block legal?\n";
    //// DispErorrCode(result);

    if(result != status_codes::SUCCESS)
        return {result, {}};
    else if (_prevMonitor.Illegal(start, dice_idx))
        return {status_codes::PREVENTS_COMPLETION, {} }; 
    else
        return {result, _g.board._realBoard.CoordAfterDistance(start, _g.dice[dice_idx])};     
}

bool Game::Arbiter::DiceRemovesFrom(const Coord& start, bool dice_idx)
{
    if(!_g.board._mockBoard.CurrPlayerInEndgame())
        return false;
    
    int pos_from_end = COLS - start.col;
    return (pos_from_end == _g.dice[dice_idx] ||  // dice val exactly
            (pos_from_end >= _g.board._mockBoard.MaxNumOcc().at(_g.board.PlayerIdx()) && _g.dice[dice_idx] > pos_from_end) );  
                // largest available is less than dice
}

bool Game::Arbiter::CanUseMockDice(bool idx, int n) const
{
    int new_val = _g.times_mockdice_used[idx] + n;
    return ( new_val <= 1 + _g.doubles_rolled*(3 - _g.times_mockdice_used[!idx]));
}

std::pair<status_codes, std::array<int, 2>> Game::Arbiter::LegalMove(const Coord& start, const Coord& end)    
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

std::pair<status_codes, Coord> Game::Arbiter::LegalMove_2step(const Coord& start)  
{
    if(!CanUseMockDice(0) || !CanUseMockDice(1))
        return {status_codes::DICE_USED_ALREADY, {} };

    if(_g.maxdice_exception)
        return {status_codes::MISC_FAILURE, {} };
    else if (_g.first_move_exception)
    {
        if(_g.dice[0] == 4 && start == Coord(_g.board.PlayerIdx(), 0) && abs(_g.board._mockBoard.at(start)) > PIECES_PER_PLAYER - 2)
            return {status_codes::SUCCESS, _g.board._realBoard.CoordAfterDistance(start, 8)};
        else
            return {status_codes::MISC_FAILURE, {} };
    } 

    Coord end = _g.board._realBoard.CoordAfterDistance(start, _g.dice[0] + _g.dice[1]);
    status_codes second_step = _g.board._mockBoard.WellDefinedEnd(start, end);

    if(second_step != status_codes::SUCCESS)
        return {second_step, end };
    else if(_blockMonitor.Illegal(start, end)){
        return {status_codes::BAD_BLOCK, end};
    }
        
    bool first_dice = 0;

    Coord mid = _g.board._realBoard.CoordAfterDistance(start, _g.dice[first_dice]);
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

bool Game::Arbiter::IllegalBlocking(const Coord& start, bool idx)
{
    return _blockMonitor.Illegal(start, idx);
}

bool Game::Arbiter::IllegalBlocking(const Coord& start, const Coord& end)
{
    return _blockMonitor.Illegal(start, end);
}

///////////// Updates and Actions /////////////

status_codes Game::Arbiter::OnRoll()
{
    _blockMonitor.Reset();

    return CheckForcedMoves();
}

void Game::Arbiter::SolidifyBlock()
{
    _blockMonitor.Solidify();
}

///////////// Forced Moves /////////////

status_codes Game::Arbiter::CheckForcedMoves()
{
    _g.legal_turns.ComputeAllLegalMoves();

    if(_g.legal_turns.BrdsToSeqs().empty())
        return status_codes::NO_LEGAL_MOVES_LEFT;
    else
        return status_codes::SUCCESS;
}

///////////////////////////////////////
////////   LegalSeqComputer   ////////
/////////////////////////////////////

Game::LegalSeqComputer::LegalSeqComputer(Game& g, Arbiter& a) : _g(g), _arb(a) {}

int Game::LegalSeqComputer::MaxLen() const {
    return _maxLen;
}

const std::unordered_map<std::string, MoveSequence>& Game::LegalSeqComputer::BrdsToSeqs() const {
    return _brdsToSeqs;
}

void Game::LegalSeqComputer::ComputeAllLegalMoves()
{
    _brdsToSeqs.clear();
    _encountered.clear();

    _maxLen = 0;
    _maxDice = (_g.dice[1] > _g.dice[0]);
    _dieIdxs = { _maxDice, !_maxDice};

    _g.board.ResetMock();

    if(FirstMoveException())
        return;
    
    std::vector<StartAndDice> seq; 
    dfs(seq);

    if(_maxLen == 1 && !_g.doubles_rolled && _arb.CanUseMockDice(0) && _arb.CanUseMockDice(1))   // non-doubles, only possible to use 1 not both of the dice
    {
        bool max_dice_possible = std::any_of(_brdsToSeqs.begin(), _brdsToSeqs.end(), 
            [&](const auto& pair) {
                return pair.second.at(0)._diceIdx == _maxDice;
            });
        if(max_dice_possible)
        {
            std::erase_if(_brdsToSeqs, [&](const auto& item){
                return item.second.at(0)._diceIdx != _maxDice;
            });
            _g.maxdice_exception = true;
        }
            
    }
}

void Game::LegalSeqComputer::dfs(std::vector<StartAndDice>& seq)
{
    std::array<std::unordered_set<Coord>, 2> options = { _g.PlayerGoesByMockDice(0), _g.PlayerGoesByMockDice(1) }; 
    
    bool legal_move_found = false;

    for(int i = 0; i < 2; ++i)
    {
        std::unordered_set<Coord>::iterator it = options.at(_dieIdxs[i]).begin();
        while(it != options.at(_dieIdxs[i]).end())
        {
            Coord coord = *it;
            if(_arb.BoardAndBlockLegal(*it, _dieIdxs[i]) != status_codes::SUCCESS)
                it = options.at(_dieIdxs[i]).erase(it);
            else
            {
                legal_move_found = true;

                seq.emplace_back(coord, _dieIdxs[i]);
                _g.MockAndUpdateBlock(coord, _dieIdxs[i]);

                std::string brdStr = Board2Str(_g.board._mockBoard.View());
                if(! _encountered.contains(brdStr) ) 
                {
                    dfs(seq);
                    _encountered.insert(brdStr);
                }

                _g.UndoMockAndUpdateBlock(coord, _dieIdxs[i]);
                seq.pop_back();

                ++it;
            }
        }
    }

    if(!legal_move_found && seq.size() >= _maxLen && !seq.empty())  // no legal moves left, valid length sequence found
    {
        if(seq.size() > _maxLen)
        {
            _maxLen = seq.size();
            _brdsToSeqs.clear();    // all previous insertions were not complete turns
        }
        std::string key = Board2Str(_g.board._mockBoard.View());
        if(!_brdsToSeqs.contains(key))
            _brdsToSeqs.emplace(key, seq);
    }
}

bool Game::LegalSeqComputer::FirstMoveException()   // fixme re-compute mid turn
{
    if (_g.turn_number[_g.board.PlayerIdx()] == 1 && _g.doubles_rolled && (_g.dice[0] == 4 || _g.dice[0] == 6 ) ) // first move exception
    {
        std::cout << "in firstmoveexception\n";
        DisplayBoard(_g.board._realBoard.View());

        Coord head(_g.board.PlayerIdx(), 0);
        Coord dest1(_g.board.PlayerIdx(), _g.dice[0]);
        MoveSequence seq;

        while( abs(_g.board._mockBoard.at(head)) > PIECES_PER_PLAYER - 2)
        {
            _g.board.Mock_Move(head, dest1);
            seq.emplace_back(head, 0);
        }

        if(_g.dice[0] == 4)
        {
            Coord dest2(_g.board.PlayerIdx(), 8);

            while (abs(_g.board._mockBoard.at(dest2)) < 2 )
            {
                _g.board.Mock_Move(dest1, dest2);
                seq.emplace_back(dest1, 0);   
            }
        }

        if(!seq.empty())
        {
            std::string key = Board2Str(_g.board._mockBoard.View());
            _brdsToSeqs.emplace(key, seq);
        }

        _g.first_move_exception = true;
        return true;
    }

    return false;
}