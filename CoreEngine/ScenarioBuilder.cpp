#include "ScenarioBuilder.h"

using namespace Nardi;


//////////////// Initialization ////////////////

ScenarioBuilder::ScenarioBuilder() : _game(), _ctrl(_game)
{
    _game.turn_number = {5, 5}; // avoiding first turn case unless explicitly requested
}

ScenarioBuilder::ScenarioBuilder(const ScenarioBuilder& other) : _game(other._game), _ctrl(_game), _view()
{
    _ctrl.start_selected = other._ctrl.start_selected;
    _ctrl.dice_rolled = other._ctrl.dice_rolled;
    _ctrl.quit_requested = other._ctrl.quit_requested;
    _ctrl.sim_mode = other._ctrl.sim_mode;

    /*
    even if other has a view attached, I don't want the view pointers to look at the same view object, so this
    also has default behavior of no view
    */
}

void ScenarioBuilder::StartPreRoll(bool p_idx, const BoardConfig& b)
{
    withPlayer(p_idx);
    withBoard(b);
    ResetControllerState();
}

status_codes ScenarioBuilder::withScenario(bool p_idx, const BoardConfig& b, int d1, int d2, int d1u, int d2u)
{
    StartPreRoll(p_idx, b);
    return withDice(d1, d2, d1u, d2u);
}

status_codes ScenarioBuilder::withDice(int d1, int d2, int d1_used, int d2_used)
{
    int max_uses = (_game.doubles_rolled + 1) * 2;

    if(d1_used < 0 || d2_used < 0 || d1_used + d2_used > max_uses)
        std::cerr << "bad dice used values in withDice\n";
    else
        _game.times_dice_used = { d1_used, d2_used };

    return _ctrl.ReceiveCommand(std::array<int, 2>{d1, d2});
}

void ScenarioBuilder::withPlayer(bool p_idx)
{
    if(_game.board.PlayerIdx() != p_idx)
        _game.board.SwitchPlayer();
}

void ScenarioBuilder::withBoard(const BoardConfig& b)
{
    _game.board.head_used = false;
    _game.board.SetData(b);

    _game.mvs_this_turn.clear();

    std::stack<Game::TurnData> empty;
    std::swap(_game.history, empty);
}

void ScenarioBuilder::withRandomEndgame(bool p_idx)
{
    BoardConfig rand_board{};

    for (int row = 0; row < 2; ++row)
    {
        int sign = row ? 1 : -1;
        for(int i = 0; i < 15; ++i)
        {
            int pos = _game.dist(_game.rng);
            rand_board[row][COLS - pos] += sign;
        }
    }

    _game.turn_number = {2, 2}; // avoid first turn exceptions

    // feed in randomly generated endgame board scenario
    StartPreRoll(p_idx, rand_board);        
}

void ScenarioBuilder::ResetControllerState()
{
    _ctrl.start_selected = false;
    _ctrl.dice_rolled = false; 
    _ctrl.quit_requested = false;
    _ctrl.sim_mode = false;
}

void ScenarioBuilder::withFirstTurn()
{
    _game.turn_number = {0, 0};
}

//////////////// Actions ////////////////

status_codes ScenarioBuilder::ReceiveCommand(const Command& cmd)
{
    return _ctrl.ReceiveCommand(cmd);
}

status_codes ScenarioBuilder::SimulateMove(const BoardConfig& b)
{
    if(!_ctrl.sim_mode)
        return status_codes::MISC_FAILURE;

    status_codes ret = ReceiveCommand(Command(b));
    _ctrl.AdvanceSimTurn();
}

void ScenarioBuilder::Reset()
{
    withBoard(TestGlobals::start_brd);
    withPlayer(white);
    _game.times_dice_used = {0, 0};
    withFirstTurn();

    ResetControllerState();
}

void ScenarioBuilder::AttachNewRW(const IRWFactory& f)
{
    auto up = f.make(_game, _ctrl);
    _view = std::shared_ptr<ReaderWriter>(std::move(up));
    _game.AttachReaderWriter(_view.get());
}

void ScenarioBuilder::DetachRW() { _game.rw = nullptr; _view = nullptr; }

void ScenarioBuilder::Render() { if(_view) _view->Render(); }

void ScenarioBuilder::PrintBoard() const
{
    DisplayBoard(_game.board.data);
}

void ScenarioBuilder::StatusReport() const
{
    std::cout << "player: " << _game.board.PlayerIdx() << "\n";

    std::cout << "dice: " << _game.dice[0] << " " << _game.dice[1] << "\n";

    std::cout << "dice used: " << _game.times_dice_used[0] << ", " << _game.times_dice_used[1] << "\n";

    std::cout << "board: \n";
    PrintBoard();
}

std::string ScenarioBuilder::StatusString() const
{
    std::stringstream ss;
    ss << "player: " << _game.board.PlayerIdx() << "\n";

    ss << "dice: " << _game.dice[0] << " " << _game.dice[1] << "\n";

    ss << "dice used: " << _game.times_dice_used[0] << ", " << _game.times_dice_used[1] << "\n";

    ss << "board: \n";

    for(int i = 0; i < ROWS; ++i)
    {
        for (int j = 0; j < COLS; ++j)
        {
            ss << static_cast<int>(_game.board.data[i][j]) << "\t";
        }
        ss << "\n\n";
    }
    ss << "\n\n\n\n";

    return ss.str();
}

const int ScenarioBuilder::GetBoardAt(const Coord& coord) const
{
    return _game.board.at(coord);
}
 
const int ScenarioBuilder::GetBoardAt(int r, int c) const
{
    return _game.board.at(r, c);
}

const BoardConfig& ScenarioBuilder::GetBoard() const
{
    return _game.board.data;
}
 
Game& ScenarioBuilder::GetGame()
{ return _game; }

const Game& ScenarioBuilder::GetGame() const
{ return _game; }
 
Controller& ScenarioBuilder::GetCtrl()
{ return _ctrl; }
 
const Controller& ScenarioBuilder::GetCtrl() const
{ return _ctrl; }

ReaderWriter* ScenarioBuilder::GetView() { return _game.rw; }
const ReaderWriter* ScenarioBuilder::GetView() const { return _game.rw; }