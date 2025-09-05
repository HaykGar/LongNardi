#include "Game.h"
#include "ReaderWriter.h"

using namespace Nardi;

///////////////////////////
////////   Game   ////////
/////////////////////////

///////////// Constructor and intialization /////////////

Game::Game(int rseed) : board(), rng(rseed), dist(1, 6), dice({0, 0}), times_dice_used({0, 0}), turn_number({0, 0}), 
                        doubles_rolled(false), rw(nullptr), arbiter(*this), legal_turns(*this)
{} 

Game::Game() :  board(), rng(std::random_device{}()), dist(1, 6), dice({0, 0}), doubles_rolled(false), 
                times_dice_used({0, 0}), turn_number({0, 0}), rw(nullptr), arbiter(*this), legal_turns(*this)
{}

void Game::AttachReaderWriter(ReaderWriter* r)
{   rw = r;   }

///////////// Getters /////////////

const Board& Game::GetBoardRef() const
{
    return board;
}

BoardKey Game::GetBoardAsKey() const
{
    return board.AsKey();
}

int Game::GetDice(bool idx) const
{   return dice[idx];   }

const ReaderWriter* Game::GetConstRW() 
{   return rw;   }


Coord Game::PlayerHead() const
{   return {board.PlayerIdx(), 0};   }


const std::unordered_map<BoardKey, MoveSequence, BoardKeyHash>& Game::GetBoards2Seqs() const
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
}

bool Game::AutoPlayTurn(const BoardKey& key)
{

    auto& b2s = legal_turns.BrdsToSeqs();

    if(!b2s.contains(key))
        return false;
    else{
        for(const auto& sd : b2s[key])
        {
            auto status = arbiter.CanMoveByDice(sd._from, sd._diceIdx).first;
            if( status != status_codes::SUCCESS){
                DispErrorCode(status);
                std::cout << "fme is " << first_move_exception << "\n";

                throw std::runtime_error("AutoPlay attempted illegal move");
            }

            UseDice(sd._diceIdx);
            if(arbiter.DiceRemovesFrom(sd._from, sd._diceIdx))
                board.Remove(sd._from);
            else
                board.Move(sd._from, board.CoordAfterDistance(sd._from, dice[sd._diceIdx]) );
        }
        return true;
    }
}

status_codes Game::TryStart(const Coord& start)
{
    return arbiter.CanStartFrom(start);
}

status_codes Game::TryFinishMove(const Coord& start, const Coord& end) // assumes start already checked
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

status_codes Game::TryFinishMove(const Coord& start, bool dice_idx)
{    
    auto [result, dest]  = arbiter.CanMoveByDice(start, dice_idx);
    if(result != status_codes::SUCCESS )
        return result;
    else
        return MakeMove(start, dice_idx);
}

status_codes Game::MakeMove(const Coord& start, const Coord& end)
{
    board.Move(start, end);
    return OnMoveOrRemove();
}

status_codes Game::MakeMove(const Coord& start, bool dice_idx)
{
    UseDice(dice_idx);
    if(arbiter.DiceRemovesFrom(start, dice_idx))
        return RemovePiece(start);
    else
        return MakeMove(start, board.CoordAfterDistance(start, dice[dice_idx]) );
}

status_codes Game::RemovePiece(const Coord& start)
{
    board.Remove(start);
    return OnMoveOrRemove();
}

status_codes Game::OnMoveOrRemove()
{
    ReAnimate();
    return arbiter.CheckForcedMoves();
}

bool Game::SilentMock(const Coord& start, const Coord& end)
{
    if(end.OutOfBounds() || start.OutOfBounds())
    {
        std::cout << "attempted to mock out of bounds, if attempting removal use the coord, dice overload \n";
        return false;
    }
    int d = board.GetDistance(start, end);
    if(d == dice[0])
        ++times_dice_used[0];
    else if(d == dice[1])
        ++times_dice_used[1];
    else if(d == dice[0] + dice[1])
    {
        ++times_dice_used[0];
        ++times_dice_used[1];
    }
    else if(doubles_rolled && d % dice[0] == 0 && arbiter.CanUseDice(0, d / dice[0]) )
        times_dice_used[0] += d / dice[0];
    else
    {
        std::cout << "!!!!\nunexpected input to SilentMock\n";
        return false;
    }

    board.Move(start, end);

    // mock_hist.push(std::pair<Coord, Coord>(start, end)); `

    return true;
}


bool Game::UndoSilentMock(const Coord& start, const Coord& end)
{
    if(end.OutOfBounds() || start.OutOfBounds())
    {
        std::cout << "attempted to mock out of bounds, if attempting removal use the coord, dice overload \n";
        return false;
    }
    int d = board.GetDistance(start, end);
    if(d == dice[0])
        --times_dice_used[0];
    else if(d == dice[1])
        --times_dice_used[1];
    else if(d == dice[0] + dice[1])
    {
        --times_dice_used[0];
        --times_dice_used[1];
    }
    else if(doubles_rolled && d % dice[0] == 0 && arbiter.CanUseDice(0, d / dice[0]) )
        times_dice_used[0] -= d / dice[0];
    else
    {
        std::cout << "!!!!\nunexpected input to UndoSilentMock\n";
        return false;
    }
    board.UndoMove(start, end);
    return true;
}

bool Game::SilentMock(const Coord& start, bool dice_idx)
{
    Coord dest = board.CoordAfterDistance(start, dice[dice_idx]);

    if(board.CurrPlayerInEndgame() && arbiter.DiceRemovesFrom(start, dice_idx))
        board.Remove(start);
    else if(board.WellDefinedEnd(start, dest) == status_codes::SUCCESS)
        board.Move(start, dest);
    else
        return false;

    ++times_dice_used[dice_idx];

    // mock_hist.push(StartAndDice(start, dice_idx)); `

    return true;
}

bool Game::UndoSilentMock(const Coord& start, bool dice_idx)
{
    Coord dest = board.CoordAfterDistance(start, dice[dice_idx]);

    if(arbiter.DiceRemovesFrom(start, dice_idx))
        board.UndoRemove(start);
    else if(board.WellDefinedEnd(start, dest) == status_codes::SUCCESS)
        board.UndoMove(start, board.CoordAfterDistance(start, dice[dice_idx]));
    else
        return false;

    --times_dice_used[dice_idx];
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

bool Game::GameIsOver() const 
{   return (board.PiecesLeft().at(0) == 0 || board.PiecesLeft().at(1) == 0);   }

bool Game::IsMars() const
{
    if(GameIsOver())
        return (board.PiecesLeft().at(0) == PIECES_PER_PLAYER || board.PiecesLeft().at(1) == PIECES_PER_PLAYER);
    
    return false;
}

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

status_codes Game::Arbiter::CanStartFrom(const Coord& start)
{
    if(_g.first_move_exception && start == _g.PlayerHead() && abs(_g.board.at(start)) > PIECES_PER_PLAYER - 2)
        return status_codes::SUCCESS;

    return _g.board.ValidStart(start);
}

std::pair<status_codes, Coord> Game::Arbiter::CanMoveByDice(const Coord& start, bool dice_idx)
{
    status_codes can_start = CanStartFrom(start);

    if(can_start == status_codes::SUCCESS)
        return CanFinishByDice(start, dice_idx);
    else
        return {can_start, {} };        
}

status_codes Game::Arbiter::BoardAndBlockLegal(const Coord& start, bool dice_idx)
{
    status_codes can_start = _g.board.ValidStart(start);

    if(can_start != status_codes::SUCCESS)
        return can_start;
    else
        return BoardAndBlockLegalEnd(start, dice_idx);
}

status_codes Game::Arbiter::BoardAndBlockLegalEnd(const Coord& start, bool dice_idx)
{
    if(!CanUseDice(dice_idx))
        return status_codes::DICE_USED_ALREADY;
    else if(_g.maxdice_exception)    // only flagged after pre-compute carried out
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
        if(start == _g.PlayerHead() && abs(_g.board.at(start)) > PIECES_PER_PLAYER - 2)
            return status_codes::SUCCESS;
        else if (   _g.dice[0] == 4 && start == Coord(_g.board.PlayerIdx(), 4) && 
                    abs(_g.board.at(start)) > 0  )
            return status_codes::SUCCESS;
        else
            return status_codes::MISC_FAILURE;
    }

    Coord final_dest = _g.board.CoordAfterDistance(start, _g.dice[dice_idx]);
    status_codes result = _g.board.WellDefinedEnd(start, final_dest);
    
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
    status_codes result = BoardAndBlockLegalEnd(start, dice_idx);

    if(result != status_codes::SUCCESS)
        return {result, {}};
    else if (_prevMonitor.Illegal(start, dice_idx))
        return {status_codes::PREVENTS_COMPLETION, {} }; 
    else
        return {result, _g.board.CoordAfterDistance(start, _g.dice[dice_idx])};     
}

bool Game::Arbiter::DiceRemovesFrom(const Coord& start, bool dice_idx)
{
    if(!_g.board.CurrPlayerInEndgame())
        return false;
    
    int pos_from_end = COLS - start.col;
    return (pos_from_end == _g.dice[dice_idx] ||  // dice val exactly
            (pos_from_end >= _g.board.MaxNumOcc() && _g.dice[dice_idx] > pos_from_end) );  
                // largest available is less than dice
}

bool Game::Arbiter::CanUseDice(bool idx, int n) const
{
    int new_val = _g.times_dice_used[idx] + n;
    return ( new_val <= 1 + _g.doubles_rolled*(3 - _g.times_dice_used[!idx]));
}

std::pair<status_codes, std::array<int, 2>> Game::Arbiter::LegalMove(const Coord& start, const Coord& end)    
// array represents how many times each dice is used, 0 or 1 usually, in case of doubles can be up to 4
{    
    int d = _g.board.GetDistance(start, end);

    if(d == _g.dice[0])
        return {CanFinishByDice(start, 0).first, {1, 0} };
    else if (d == _g.dice[1]) 
        return {CanFinishByDice(start, 1).first, {0, 1} }; 
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
            return { CanFinishByDice(step2_dest, 0).first, {3, 0} };
        else if(d == _g.dice[0] * 4)
            return { LegalMove_2step(step2_dest).first, {4, 0} };
    }
    
    return {status_codes::NO_PATH, {}};
}

std::pair<status_codes, Coord> Game::Arbiter::LegalMove_2step(const Coord& start)  
{
    if(!CanUseDice(0) || !CanUseDice(1))
        return {status_codes::DICE_USED_ALREADY, {} };

    if(_g.maxdice_exception)
        return {status_codes::MISC_FAILURE, {} };
    else if (_g.first_move_exception)
    {
        if(_g.dice[0] == 4 && start == _g.PlayerHead() && abs(_g.board.at(start)) > PIECES_PER_PLAYER - 2)
            return {status_codes::SUCCESS, _g.board.CoordAfterDistance(start, 8)};
        else
            return {status_codes::MISC_FAILURE, {} };
    } 

    Coord end = _g.board.CoordAfterDistance(start, _g.dice[0] + _g.dice[1]);
    status_codes second_step = _g.board.WellDefinedEnd(start, end);

    if(second_step != status_codes::SUCCESS)
        return {second_step, end };
    else if(_blockMonitor.Illegal(start, end)){
        return {status_codes::BAD_BLOCK, end};
    }
        
    bool first_dice = 0;

    Coord mid = _g.board.CoordAfterDistance(start, _g.dice[first_dice]);
    status_codes status = _g.board.WellDefinedEnd(start, mid);

    if(status != status_codes::SUCCESS)
    {
        if(_g.doubles_rolled)
            return {status_codes::NO_PATH, end }; 
        
        first_dice = !first_dice;   // try both dice to get to a midpoint, no need if doubles
        mid = _g.board.CoordAfterDistance(start, _g.dice[first_dice]);
        status = _g.board.WellDefinedEnd(start, mid);

        if(status != status_codes::SUCCESS)
            return {status_codes::NO_PATH, end }; 
    }
    // valid midpoint reached
    return { status, end };
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

Game::LegalSeqComputer::LegalSeqComputer(Game& g) : _g(g) {}

int Game::LegalSeqComputer::MaxLen() const {
    return _maxLen;
}

const std::unordered_map<BoardKey, MoveSequence, BoardKeyHash>& Game::LegalSeqComputer::BrdsToSeqs() const {
    return _brdsToSeqs;
}

std::unordered_map<BoardKey, MoveSequence, BoardKeyHash>& Game::LegalSeqComputer::BrdsToSeqs() {
    return _brdsToSeqs;
}

void Game::LegalSeqComputer::ComputeAllLegalMoves()
{
    _brdsToSeqs.clear();
    _encountered.clear();

    _maxLen = 0;
    _maxDice = (_g.dice[1] > _g.dice[0]);
    _dieIdxs = { _maxDice, !_maxDice};

    if(FirstMoveException())
        return;
    
    std::vector<StartAndDice> seq; 
    dfs(seq);

    if(_maxLen == 1 && !_g.doubles_rolled && _g.arbiter.CanUseDice(0) && _g.arbiter.CanUseDice(1))   // non-doubles, only possible to use 1 not both of the dice
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
    size_t oldSize = seq.size();
    
    for(int i = 0; i < 2; ++i)
    {
        if(!_g.arbiter.CanUseDice(_dieIdxs[i]))
            continue;

        for(Coord coord = _g.PlayerHead(); coord.InBounds(); coord = _g.board.CoordAfterDistance(coord, 1))
        {
            if(_g.arbiter.BoardAndBlockLegal(coord, _dieIdxs[i]) == status_codes::SUCCESS)
            {
                seq.emplace_back(coord, _dieIdxs[i]);
                _g.MockAndUpdateBlock(coord, _dieIdxs[i]);

                BoardKey brdkey = _g.board.AsKey();
                if(! _encountered.contains(brdkey) ) 
                {
                    dfs(seq);
                    _encountered.insert(brdkey);
                }

                _g.UndoMockAndUpdateBlock(coord, _dieIdxs[i]);
                seq.pop_back();
            }
        }
    }

    if(oldSize == seq.size() && seq.size() >= _maxLen && !seq.empty())  // no further legal moves left, valid length sequence found
    {
        if(seq.size() > _maxLen)
        {
            _maxLen = seq.size();
            _brdsToSeqs.clear();    // all previous insertions were not complete turns
        }
        BoardKey key = _g.board.AsKey();
        if(!_brdsToSeqs.contains(key)){
            _brdsToSeqs.emplace(key, seq);
        }
    }
}

bool Game::LegalSeqComputer::FirstMoveException()   // fixme re-compute mid turn
{
    if (_g.turn_number[_g.board.PlayerIdx()] == 1 && _g.doubles_rolled && (_g.dice[0] == 4 || _g.dice[0] == 6 ) ) // first move exception
    {
        /*
        
        4 4 case: enemy can occupy with the 8 with 5, 5 first roll
        6, 6 case: 6 square is distance 18 from enemy home, 18 > 12 and 18/4 = 4.5 so not reachable
        
        */
        MoveSequence seq;

        Coord head = _g.PlayerHead();
        Coord mid(_g.board.PlayerIdx(), 4);

        Coord dest(_g.board.PlayerIdx(), _g.dice[0]);

        int at_head = abs(_g.board.at(head));
        int n_moves = 0;
        int n_2moves = 0;

        while( at_head > PIECES_PER_PLAYER - 2)
        {
            _g.SilentMock(head, 0);
            seq.emplace_back(head, 0);
            --at_head;
            ++n_moves;
        }

        if(_g.dice[0] == 4 && _g.board.WellDefinedEnd(mid, {_g.board.PlayerIdx(), 8}) == status_codes::SUCCESS)
        {
            dest = {_g.board.PlayerIdx(), 8};
            int at_dest = abs(_g.board.at(dest));
            while (at_dest < 2)
            {
                _g.SilentMock(mid, 0);
                seq.emplace_back(mid, 0);   
                ++at_dest;
                ++n_2moves;
            }
        }

        if(!seq.empty())
        {
            BoardKey key = _g.board.AsKey();
            _brdsToSeqs.emplace(key, seq);

            if(_g.dice[0] == 6 && abs(_g.board.at(_g.board.PlayerIdx(), 6)) != 2 )
                throw std::runtime_error("First Move error on 6 6");
            
            else if(_g.dice[0] == 4 && abs(_g.board.at(_g.board.PlayerIdx(), 8)) != 2 && _g.board.PlayerSign() * _g.board.at(_g.board.PlayerIdx(), 8) != -1)
            {
                DisplayBoard(_g.board.View());
                throw std::runtime_error("First Move error on 4 4");
            }
                

            while(n_2moves > 0)
            {
                _g.UndoSilentMock(mid, 0);
                --n_2moves;
            }
            
            while(n_moves > 0)
            {
                _g.UndoSilentMock(head, 0);
                --n_moves;
            }
        }

        _g.first_move_exception = true;
        return true;
    }

    return false;
}