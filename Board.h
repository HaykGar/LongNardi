#pragma once

#include "Auxilaries.h"
#include <unordered_set>

namespace Nardi
{

class Board 
{
public:
    // Constructor
    Board();
    Board(const std::array<std::array<int, COLS>, ROWS>& d);

    // Getters
    const int& at(const Coord& s) const;
    const int& at(size_t r, size_t c) const;
    const boardConfig& View() const;

    bool PlayerIdx() const;
    int PlayerSign() const;
    bool HeadUsed() const;

    int MaxNumOcc() const;
    const std::array<int, 2>& ReachedEnemyHome() const;
    const std::array<int, 2>& PiecesLeft() const;

    // Updates and Actions    
    void Move(const Coord& start, const Coord& end);
    void UndoMove(const Coord& start, const Coord& end);

    void Remove(const Coord& to_remove);
    void UndoRemove(const Coord& to_remove);

    void SwitchPlayer();

    void Print() const;
    
    // Legality Checks
    status_codes ValidStart(const Coord& s) const;
    status_codes WellDefinedEnd(const Coord& start, const Coord& end) const;  // check that move end from start is friendly or empty
    status_codes WellDefinedEnd(const Coord& start, const Coord& end, bool player) const;
    
    bool IsPlayerHead(const Coord& c) const;
    bool HeadReuseIssue(const Coord& c) const;

    bool CurrPlayerInEndgame() const;

    // Calculations
    Coord CoordAfterDistance(const Coord& start, int d, bool player) const;
    Coord CoordAfterDistance(const Coord& start, int d) const;

    int GetDistance(const Coord& start, const Coord& end, bool player) const;
    int GetDistance(const Coord& start, const Coord& end) const;

    unsigned MovablePieces(const Coord& start) const;

    // operators
    bool operator==(const Board& other) const;

    friend class ScenarioBuilder;
private:
    boardConfig data;

    bool player_idx;
    int player_sign;
    bool head_used;
    std::array<int, 2> pieces_per_player;
    std::array<int, 2> reached_enemy_home;
    std::array<int, 2>  pieces_left;

    // Updates and Actions
    void OnMove(const Coord& start, const Coord& end);
    void OnUndoMove(const Coord& start, const Coord& end);

    // Legality
    bool Bad_RowChangeTo(bool er, bool player) const;

    // Manaul Initialization
    void SetData(const std::array<std::array<int, COLS>, ROWS>& b);
    void CalcPiecesLeftandReached();
};

} // namespace Nardi