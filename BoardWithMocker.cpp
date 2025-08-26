#include "Game.h"

using namespace Nardi;

Game::BoardWithMocker::BoardWithMocker(Game& g) : _realBoard(), _mockBoard(), _game(g) 
{
    ResetMock();
}

//////////////////////////////// Getters ////////////////////////////////

bool Game::BoardWithMocker::PlayerIdx() const
{
    return _realBoard.PlayerIdx();
}

int Game::BoardWithMocker::PlayerSign() const
{
    return _realBoard.PlayerSign();
}

bool Game::BoardWithMocker::IsPlayerHead(const Coord& c) const
{
    return _realBoard.IsPlayerHead(c);
}


bool Game::BoardWithMocker::MisMatch() const
{
    return _realBoard != _mockBoard;
}

//////////////////////////////// Updates and Actions ////////////////////////////////

void Game::BoardWithMocker::ResetMock()
{
    if(_mockBoard != _realBoard)
        _mockBoard = _realBoard;

    _game.times_mockdice_used = _game.times_dice_used;
}

void Game::BoardWithMocker::RealizeMock()
{
    if(_mockBoard != _realBoard)
        _realBoard = _mockBoard;

    _game.times_dice_used = _game.times_mockdice_used;
}

void Game::BoardWithMocker::Move(const Coord& start, const Coord& end)
{
    _realBoard.Move(start, end);
    ResetMock();
}

void Game::BoardWithMocker::Remove(const Coord& to_remove)
{
    _realBoard.Remove(to_remove);
    ResetMock();
}

void Game::BoardWithMocker::Mock_Move(const Coord& start, const Coord& end)
{
    _mockBoard.Move(start, end);
}

void Game::BoardWithMocker::Mock_UndoMove(const Coord& start, const Coord& end)
{
    _mockBoard.UndoMove(start, end);
}

void Game::BoardWithMocker::Mock_Remove(const Coord& to_remove)
{
    _mockBoard.Remove(to_remove);
}

void Game::BoardWithMocker::Mock_UndoRemove(const Coord& to_remove)
{
    _mockBoard.UndoRemove(to_remove);
}

void Game::BoardWithMocker::SwitchPlayer()
{
    _realBoard.SwitchPlayer();
    ResetMock();
}