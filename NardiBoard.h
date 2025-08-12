#pragma once

#include "Auxilaries.h"
#include <unordered_set>

class NardiBoard 
{
public:
    // Constructor
    NardiBoard();
    NardiBoard(const std::array<std::array<int, COL>, ROW>& d);

    // Getters
    const int& at(const NardiCoord& s) const;
    const int& at(size_t r, size_t c) const;

    bool PlayerIdx() const;
    int PlayerSign() const;
    bool HeadUsed() const;

    const std::array<int, 2>& MaxNumOcc() const;
    const std::array<int, 2>& ReachedEnemyHome() const;
    const std::array<int, 2>& PiecesLeft() const;
    const std::array< std::array< std::unordered_set<NardiCoord>, 6 >, 2 >& GoesIdxPlusOne() const; // unnecessary?
    const std::unordered_set<NardiCoord>& PlayerGoesByDist(size_t dist) const;

    // Updates and Actions    
    void Move(const NardiCoord& start, const NardiCoord& end);
    void UndoMove(const NardiCoord& start, const NardiCoord& end);

    void Remove(const NardiCoord& to_remove);
    void UndoRemove(const NardiCoord& to_remove);

    void SwitchPlayer();

    void Print() const;
    
    // Legality Checks
    status_codes ValidStart(const NardiCoord& s) const;
    status_codes WellDefinedEnd(const NardiCoord& start, const NardiCoord& end) const;  // check that move end from start is friendly or empty
    
    bool IsPlayerHead(const NardiCoord& c) const;
    bool HeadReuseIssue(const NardiCoord& c) const;

    bool CurrPlayerInEndgame() const;

    // Calculations
    NardiCoord CoordAfterDistance(const NardiCoord& start, int d, bool player) const;
    NardiCoord CoordAfterDistance(const NardiCoord& start, int d) const;

    int GetDistance(const NardiCoord& start, const NardiCoord& end, bool player) const;
    int GetDistance(const NardiCoord& start, const NardiCoord& end) const;

    unsigned MovablePieces(const NardiCoord& start) const;

    // operators
    bool operator==(const NardiBoard& other) const;

    friend class TestBuilder;
private:
    std::array<std::array<int, COL>, ROW> data;     // worth making private?

    bool player_idx;
    int player_sign;
    bool head_used;
    std::array<int, 2> reached_enemy_home;
    std::array<int, 2>  pieces_left;
    std::array<int, 2> max_num_occ; // left uninitialized because we don't use it until the endgame, maybe can set to -1, -1
    std::array< std::array< std::unordered_set<NardiCoord>, 6 >, 2 > goes_idx_plusone;

    // Updates and Actions
    void OnMove(const NardiCoord& start, const NardiCoord& end);
    void OnUndoMove(const NardiCoord& start, const NardiCoord& end);

    void OnRemove(const NardiCoord& to_remove);
    void OnUndoRemove(const NardiCoord& to_remove);

    void UpdateAvailabilitySets(const NardiCoord& start, const NardiCoord& end);
    void UpdateAvailabilityStart(const NardiCoord start);
    void UpdateAvailabilityDest(const NardiCoord& dest);
    void SetMaxOcc();

    void FlagHeadIfNeeded(const NardiCoord& start);

    // Legality
    bool Bad_RowChangeTo(bool er, bool player) const;

    // Testing
    void SetData(const std::array<std::array<int, COL>, ROW>& b);
    void CalcPiecesLeftandReached();
    void ConstructAvailabilitySets();

};