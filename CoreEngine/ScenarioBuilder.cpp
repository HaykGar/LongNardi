#include "ScenarioBuilder.h"

using namespace Nardi;


//////////////// Initialization ////////////////

ScenarioBuilder::ScenarioBuilder() : _game(), _ctrl(_game)
{
    _game.turn_number = {2, 2}; // avoiding first turn case unless explicitly requested
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

    // _game.SetDice(d1, d2);

    // status_codes outcome = _game.OnRoll();

    // if(outcome == status_codes::NO_LEGAL_MOVES_LEFT)
    //     _ctrl.SwitchTurns();
    // else
    //     _ctrl.dice_rolled = true;

    // return outcome;
}

status_codes ScenarioBuilder::withDice(int d1, int d2)
{
    return withDice(d1, d2, 0, 0);
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
    _game.board = _game.board;
}

void ScenarioBuilder::ResetControllerState()
{
    _ctrl.start_selected = false;
    _ctrl.dice_rolled = false; 
    _ctrl.quit_requested = false;
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

void ScenarioBuilder::Reset()
{
    withBoard(TestGlobals::start_brd);
    withPlayer(white);
    _game.times_dice_used = {0, 0};
    withFirstTurn();

    ResetControllerState();
}

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