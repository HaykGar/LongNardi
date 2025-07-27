#include "NardiGame.h"

Game::BoardWithMocker::BoardWithMocker(Game& g) : _realBoard(), _mockBoard(), _game(g) 
{
    ResetMock();
}

//////////////////////////////// Getters ////////////////////////////////

const NardiBoard& Game::BoardWithMocker::ViewReal() const
{
    return _realBoard;
}

const int& Game::BoardWithMocker::at(const NardiCoord& s) const
{
    return _realBoard.at(s);
}

const int& Game::BoardWithMocker::at(size_t r, size_t c) const
{
    return _realBoard.at(r, c);
}

const int& Game::BoardWithMocker::Mock_at(const NardiCoord& s) const
{
    return _mockBoard.at(s);
}
const int& Game::BoardWithMocker::Mock_at(size_t r, size_t c) const
{
    return _mockBoard.at(r, c);
}

bool Game::BoardWithMocker::PlayerIdx() const
{
    return _realBoard.PlayerIdx();
}

int Game::BoardWithMocker::PlayerSign() const
{
    return _realBoard.PlayerSign();
}

bool Game::BoardWithMocker::HeadUsed() const
{
    return _realBoard.HeadUsed();
}

bool Game::BoardWithMocker::IsPlayerHead(const NardiCoord& c) const
{
    return _realBoard.IsPlayerHead(c);
}

bool Game::BoardWithMocker::Mock_HeadUsed() const
{
    return _mockBoard.HeadUsed();
}

const std::array<int, 2>& Game::BoardWithMocker::MaxNumOcc() const
{
    return _realBoard.MaxNumOcc();
}

const std::array<int, 2>& Game::BoardWithMocker::Mock_MaxNumOcc() const
{
    return _mockBoard.MaxNumOcc();
}

const std::array<int, 2>& Game::BoardWithMocker::ReachedEnemyHome() const
{
    return _realBoard.ReachedEnemyHome();
}

const std::array<int, 2>& Game::BoardWithMocker::Mock_ReachedEnemyHome() const
{
    return _mockBoard.ReachedEnemyHome();
}

const std::array<int, 2>& Game::BoardWithMocker::PiecesLeft() const
{
    return _realBoard.PiecesLeft();
}

const std::array<int, 2>& Game::BoardWithMocker::Mock_PiecesLeft() const
{
    return _mockBoard.PiecesLeft();
}

const std::array< std::array< std::unordered_set<NardiCoord>, 6 >, 2 >& Game::BoardWithMocker::GoesIdxPlusOne() const
{
    return _realBoard.GoesIdxPlusOne();
}

const std::array< std::array< std::unordered_set<NardiCoord>, 6 >, 2 >& Game::BoardWithMocker::Mock_GoesIdxPlusOne() const
{
    return _mockBoard.GoesIdxPlusOne();
}

const std::unordered_set<NardiCoord>& Game::BoardWithMocker::PlayerGoesByDist(size_t dist) const
{
    return _realBoard.PlayerGoesByDist(dist);
}

const std::unordered_set<NardiCoord>& Game::BoardWithMocker::Mock_PlayerGoesByDist(size_t dist) const
{
    return _mockBoard.PlayerGoesByDist(dist);
}

unsigned Game::BoardWithMocker::MovablePieces(const NardiCoord& start) const
{
    return _realBoard.MovablePieces(start);
}

unsigned Game::BoardWithMocker::Mock_MovablePieces(const NardiCoord& start) const
{
    return _mockBoard.MovablePieces(start);
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

void Game::BoardWithMocker::Move(const NardiCoord& start, const NardiCoord& end)
{
    _realBoard.Move(start, end);
    ResetMock();
}
void Game::BoardWithMocker::Remove(const NardiCoord& to_remove)
{
    _realBoard.Remove(to_remove);
    ResetMock();
}

void Game::BoardWithMocker::Mock_Move(const NardiCoord& start, const NardiCoord& end)
{
    _mockBoard.Move(start, end);
}

void Game::BoardWithMocker::Mock_UndoMove(const NardiCoord& start, const NardiCoord& end)
{
    _mockBoard.UndoMove(start, end);
}

void Game::BoardWithMocker::Mock_Remove(const NardiCoord& to_remove)
{
    _mockBoard.Remove(to_remove);
}

void Game::BoardWithMocker::Mock_UndoRemove(const NardiCoord& to_remove)
{
    _mockBoard.UndoRemove(to_remove);
}

void Game::BoardWithMocker::SwitchPlayer()
{
    _realBoard.SwitchPlayer();
    ResetMock();
}

//////////////////////////////// Legality Checks ////////////////////////////////

status_codes Game::BoardWithMocker::ValidStart(const NardiCoord& s) const
{
    return _realBoard.ValidStart(s);
}

status_codes Game::BoardWithMocker::Mock_ValidStart(const NardiCoord& s) const
{
    return _mockBoard.ValidStart(s);
}

status_codes Game::BoardWithMocker::WellDefinedEnd(const NardiCoord& start, const NardiCoord& end) const
{
    return _realBoard.WellDefinedEnd(start, end);
}

status_codes Game::BoardWithMocker::Mock_WellDefinedEnd(const NardiCoord& start, const NardiCoord& end) const
{
    return _mockBoard.WellDefinedEnd(start, end);
}

bool Game::BoardWithMocker::HeadReuseIssue(const NardiCoord& c) const
{
    return _realBoard.HeadReuseIssue(c);
}

bool Game::BoardWithMocker::Mock_HeadReuseIssue(const NardiCoord& c) const
{
    return _mockBoard.HeadReuseIssue(c);
}

bool Game::BoardWithMocker::CurrPlayerInEndgame() const
{
    return _realBoard.CurrPlayerInEndgame();
}

bool Game::BoardWithMocker::Mock_CurrPlayerInEndgame() const
{
    return _mockBoard.CurrPlayerInEndgame();
}

//////////////////////////////// Calculations ////////////////////////////////
NardiCoord Game::BoardWithMocker::CoordAfterDistance(const NardiCoord& start, int d, bool player) const
{
    return _realBoard.CoordAfterDistance(start, d, player);
}
NardiCoord Game::BoardWithMocker::CoordAfterDistance(const NardiCoord& start, int d) const
{
    return _realBoard.CoordAfterDistance(start, d);
}
int Game::BoardWithMocker::GetDistance(const NardiCoord& start, const NardiCoord& end) const
{
    return _realBoard.GetDistance(start, end);
}

//////////////////////////////// Testing ////////////////////////////////
void Game::BoardWithMocker::SetPlayer(bool player)
{
    if(_realBoard.PlayerIdx() != player )
    {
        _realBoard.SwitchPlayer();
        ResetMock();
    }
}