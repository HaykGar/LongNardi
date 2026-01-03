#include "Game.h"
#include "ReaderWriter.h"

using namespace Nardi;

///////////////////////////
////////   Game   ////////
/////////////////////////

///////////// Constructor and intialization /////////////

Game::Game(int rseed) : board(), rng(rseed), dist(1, 6), dice({0, 0}), times_dice_used({0, 0}), 
                        turn_number({0, 0}), emitted_game_over(false), doubles_rolled(false), 
                        rw(nullptr), arbiter(*this), legal_turns(*this)
{} 

Game::Game() :  board(), rng(std::random_device{}()), dist(1, 6), dice({0, 0}), 
                emitted_game_over(false), doubles_rolled(false), times_dice_used({0, 0}),
                turn_number({0, 0}), rw(nullptr), arbiter(*this), legal_turns(*this)
{}

Game::Game(const Game& other) : rng(std::random_device{}()), dist(1, 6), rw(nullptr), 
                                arbiter(*this), legal_turns(*this, other.legal_turns)
{
    mvs_this_turn = other.mvs_this_turn;
    history = other.history;
    turn_number = other.turn_number;
    board = other.board;
    dice = other.dice;
    emitted_game_over = other.emitted_game_over;
    doubles_rolled = (dice[0] == dice[1]); 
    first_move_exception = other.first_move_exception;
    maxdice_exception = other.maxdice_exception;
    times_dice_used = other.times_dice_used;
}

void Game::AttachReaderWriter(ReaderWriter* r)
{   rw = r;   }

void Game::AttachReaderWriter(std::shared_ptr<ReaderWriter> r)
{
    rw = r.get();
}

///////////// Getters /////////////

const Board& Game::GetBoardRef() const
{
    return board;
}

BoardConfig Game::GetBoardData() const
{
    return board.View();
}

int Game::GetDice(bool idx) const
{   return dice[idx];   }

const ReaderWriter* Game::GetConstRW() 
{   return rw;   }


Coord Game::PlayerHead() const
{   return {board.PlayerIdx(), 0};   }


const std::unordered_map<BoardConfig, MoveSequence, BoardConfigHash>& Game::GetBoards2Seqs() const
{
    return legal_turns.BrdsToSeqs();
}

///////////// Gameplay /////////////

status_codes Game::RollDice() // important to force this only once per turn in controller, no explicit safeguard here
{    
    SetDice(dist(rng), dist(rng));
    return OnRoll();
}

status_codes Game::SimDice(DieType d_to)
{
    SetDice(d_to[0], d_to[1]);
    return OnRoll();
}

status_codes Game::OnRoll()
{
    EmitEvent(GameEvent{EventCode::DICE_ROLL, dice});
    IncrementTurnNumber();  // starts at 0

    first_move_exception = false;
    maxdice_exception = false;
    return arbiter.CheckForcedMoves();
}
 
void Game::SetDice(int d1, int d2)
{   
    dice[0] = d1; 
    dice[1] = d2;  
    
    doubles_rolled = (dice[0] == dice[1]); 
}

void Game::UseDice(bool idx, int n)
{   
    times_dice_used[idx] += n;  
}

bool Game::AutoPlayTurn(const BoardConfig& key)
{
    auto& b2s = legal_turns.BrdsToSeqs();
    auto original_brd = board.View();

    if(!b2s.contains(key))
        return false;
    else{
        for(const auto& sd : b2s[key])
        {
            auto status = arbiter.CanMoveByDice(sd._from, sd._diceIdx).first;
            if( status != status_codes::SUCCESS){
                DispErrorCode(status);
                std::cerr << "fme is " << first_move_exception << ", current board:\n";
                std::cerr << "dice are " << dice[0] << ", " << dice[1] << "\n";
                DisplayBoard(board.View());
                std::cerr << "original board:\n";
                DisplayBoard(original_brd);
                std::cerr << "attempted move: " << sd._from.AsStr() << " by " << dice[sd._diceIdx] << "\n";

                std::cerr << "full sequence of moves attempting:\n";
                for (const auto& mv : b2s[key])
                    std::cerr << mv._from.AsStr() << " by " << dice[mv._diceIdx] << "\n";

                throw std::runtime_error("AutoPlay attempted illegal move");
            }

            UseDice(sd._diceIdx);
            if(arbiter.DiceRemovesFrom(sd._from, sd._diceIdx)) {
                board.Remove(sd._from);
                EmitEvent(GameEvent{EventCode::REMOVE, RemoveData{sd._from, sd._diceIdx}});
            }
            else {
                Coord dest = board.CoordAfterDistance(sd._from, dice[sd._diceIdx]);
                board.Move(sd._from, dest);
                EmitEvent(GameEvent{EventCode::MOVE, MoveData{sd._from, dest, sd._diceIdx}});
            }
            mvs_this_turn.emplace_back(sd._from, sd._diceIdx);
        }
        arbiter.CheckForcedMoves();
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
        if(times_used[0] + times_used[1] == 1)
        {
            bool used_idx = (times_used[1] == 1);
            return MakeMove(start, used_idx);
        }
        else if(doubles_rolled)
        {
            int steps = times_used[0] + times_used[1];
            Coord from(start);
            status_codes status = status_codes::SUCCESS;
            while(steps > 0 && status == status_codes::SUCCESS)
            {
                status = MakeMove(from, 0);
                --steps;
                from = board.CoordAfterDistance(from, dice[0]);
            }
            return status;
        }
        else
        {
            if(times_used[0] + times_used[1] != 2)
                throw std::runtime_error("unexpected dice usages in TryFinishMove");
            
            bool first_dice = 0;
            Coord mid = board.CoordAfterDistance(start, dice[first_dice]);
            if(board.WellDefinedEnd(start, mid) != status_codes::SUCCESS)
            {
                first_dice = !first_dice;
                mid = board.CoordAfterDistance(start, dice[first_dice]);
            }
            MakeMove(start, first_dice);
            return MakeMove(mid, !first_dice);
        }
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

status_codes Game::MakeMove(const Coord& start, bool dice_idx)
{
    if(MockMove(start, dice_idx))
    {
        mvs_this_turn.emplace_back(start, dice_idx);

        Coord end = board.CoordAfterDistance(start, dice[dice_idx]);

        if(end.InBounds())
            EmitEvent(GameEvent{EventCode::MOVE, MoveData{start, end, dice_idx}});
        else
            EmitEvent(GameEvent{EventCode::REMOVE, RemoveData{start, dice_idx}});

        return arbiter.CheckForcedMoves();
    }
    else
        return status_codes::MISC_FAILURE;
}

bool Game::MockMove(const Coord& start, const Coord& end)
{
    if(end.OutOfBounds() || start.OutOfBounds())
    {
        std::cerr << "attempted to mock out of bounds, if attempting removal use the coord, dice overload \n";
        return false;
    }
    int d = board.GetDistance(start, end);
    if(d == dice[0])
        UseDice(0);
    else if(d == dice[1])
        UseDice(1);
    else if(d == dice[0] + dice[1])
    {
        UseDice(0);
        UseDice(1);
    }
    else if(doubles_rolled && d % dice[0] == 0 && arbiter.CanUseDice(0, d / dice[0]) )
        UseDice(0, d / dice[0]);
    else
    {
        std::cerr << "!!!!\nunexpected input to MockMove\n";
        return false;
    }

    board.Move(start, end);

    return true;
}


bool Game::UndoMove(const Coord& start, const Coord& end)
{
    if(end.OutOfBounds() || start.OutOfBounds())
    {
        std::cerr << "attempted to mock out of bounds, if attempting removal use the coord, dice overload \n";
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
        std::cerr << "!!!!\nunexpected input to UndoMove\n";
        return false;
    }
    board.UndoMove(start, end);
    return true;
}

bool Game::MockMove(const Coord& start, bool dice_idx)
{
    Coord dest = board.CoordAfterDistance(start, dice[dice_idx]);

    if(board.CurrPlayerInEndgame() && arbiter.DiceRemovesFrom(start, dice_idx))
        board.Remove(start);
    else if(board.WellDefinedEnd(start, dest) == status_codes::SUCCESS)
        board.Move(start, dest);
    else
        return false;

    UseDice(dice_idx);

    return true;
}

bool Game::UndoMove(const Coord& start, bool dice_idx)
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

bool Game::UndoMove(const StartAndDice& sd)
{
    return UndoMove(sd._from, sd._diceIdx);
}

status_codes Game::UndoCurrentTurn()
{
    if(TurnInProgress() || history.empty())
    {
        std::cerr << "unexpected tip or empty hist in UndoCurrentTurn\n";
        return status_codes::MISC_FAILURE;
    }

    --turn_number[board.PlayerIdx()];
    auto [mvs, old_d] = history.top();
    SetDice(old_d[0], old_d[1]);

    board.SwitchPlayer();
    while(!mvs.empty())
    {
        int last_idx = mvs.size() - 1;
        UndoMove(mvs[last_idx]);
        mvs.pop_back();
    }
    
    history.pop();
    times_dice_used[0] = 0;
    times_dice_used[1] = 0;

    --turn_number[board.PlayerIdx()];   // other player... incremented in onroll
    return OnRoll();
}


bool Game::TurnInProgress() const
{
    return (times_dice_used[0] != 0 || times_dice_used[1] != 0);
}

bool Game::GameIsOver() const 
{   
    bool over = (board.PiecesLeft().at(0) == 0 || board.PiecesLeft().at(1) == 0);  
    if(over && !emitted_game_over){
        EmitEvent(GameEvent{ EventCode::GAME_OVER, std::monostate{} });
        emitted_game_over = true;
    }
    return over;
}

bool Game::IsMars() const
{
    if(GameIsOver())
        return (board.PiecesLeft().at(0) == PIECES_PER_PLAYER || board.PiecesLeft().at(1) == PIECES_PER_PLAYER);
    
    return false;
}

bool Game::CanUseDice(bool idx) {return arbiter.CanUseDice(idx);}

void Game::SwitchPlayer()
{   
    board.SwitchPlayer();
    times_dice_used[0] = 0;
    times_dice_used[1] = 0;

    history.emplace(mvs_this_turn, dice);
    mvs_this_turn.clear();

    EmitEvent(GameEvent{ EventCode::TURN_SWITCH, std::monostate{} });
}

void Game::IncrementTurnNumber()
{   ++turn_number[board.PlayerIdx()];   }

void Game::EmitEvent(const GameEvent& e) const
{
    if(rw)
        rw->OnGameEvent(e);
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
    else if(_g.maxdice_exception && _g.dice[dice_idx] < _g.dice[!dice_idx])
        return status_codes::MISC_FAILURE;
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
    int total_dice = (1+_g.doubles_rolled) * 2;
    if(_g.times_dice_used[0] + _g.times_dice_used[1] > total_dice - 2)
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

Game::LegalSeqComputer::LegalSeqComputer(Game& g, const LegalSeqComputer& other) : _g(g)
{
    _maxDice = other._maxDice;
    _dieIdxs = other._dieIdxs;
    _encountered = other._encountered;

    _maxLen = other._maxLen;
    _brdsToSeqs = other._brdsToSeqs;
}

int Game::LegalSeqComputer::MaxLen() const {
    return _maxLen;
}

const std::unordered_map<BoardConfig, MoveSequence, BoardConfigHash>& Game::LegalSeqComputer::BrdsToSeqs() const {
    return _brdsToSeqs;
}

std::unordered_map<BoardConfig, MoveSequence, BoardConfigHash>& Game::LegalSeqComputer::BrdsToSeqs() {
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
            auto orig_size = _brdsToSeqs.size();

            std::erase_if(_brdsToSeqs, [&](const auto& item){
                return item.second.at(0)._diceIdx != _maxDice;
            });
            _g.maxdice_exception = _brdsToSeqs.size() != orig_size;
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
                _g.MockMove(coord, _dieIdxs[i]);

                BoardConfig brdkey = _g.board.View();
                if(! _encountered.contains(brdkey) ) 
                {
                    dfs(seq);
                    _encountered.insert(brdkey);
                }

                _g.UndoMove(coord, _dieIdxs[i]);
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
        BoardConfig key = _g.board.View();
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
            _g.MockMove(head, 0);
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
                _g.MockMove(mid, 0);
                seq.emplace_back(mid, 0);   
                ++at_dest;
                ++n_2moves;
            }
        }

        if(!seq.empty())
        {
            BoardConfig key = _g.board.View();
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
                _g.UndoMove(mid, 0);
                --n_2moves;
            }
            
            while(n_moves > 0)
            {
                _g.UndoMove(head, 0);
                --n_moves;
            }
        }

        _g.first_move_exception = true;
        return true;
    }

    return false;
}