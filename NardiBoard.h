#pragma once

#include "Auxilaries.h"
#include <unordered_set>

struct NardiBoard 
{
public:
    // Constructor
    NardiBoard();

    // Getters
    const int& at(const NardiCoord& s) const;
    const int& at(size_t r, size_t c) const;

    bool PlayerIdx() const;
    int PlayerSign() const;
    bool HeadUsed() const;
    const std::array<int, 2>& MaxNumOcc() const;
    const std::array<int, 2>& ReachedEnemyHome() const;
    const std::array<int, 2>& PiecesLeft() const;

    // Updates and Actions    
    void Move(const NardiCoord& start, const NardiCoord& end);
    void Remove(const NardiCoord& to_remove);
    void SetMaxOcc();
    void SwitchPlayer();
    void FlagHeadIfNeeded(const NardiCoord& start);

    // Legality Checks
    status_codes ValidStart(const NardiCoord& s) const;
    status_codes WellDefinedEnd(const NardiCoord& start, const NardiCoord& end) const;  // check that move end from start is friendly or empty
    bool Bad_RowChangeTo(bool er, bool player) const;

    bool IsPlayerHead(const NardiCoord& c) const;
    bool HeadReuseIssue(const NardiCoord& c) const;

    bool CurrPlayerInEndgame() const;

    // Coord and Distance Calculations
    NardiCoord CoordAfterDistance(const NardiCoord& start, int d, bool player) const;
    NardiCoord CoordAfterDistance(const NardiCoord& start, int d) const;
    unsigned GetDistance(const NardiCoord& start, const NardiCoord& end) const;

private:
    bool player_idx;
    int player_sign;
    bool head_used;
    std::array<int, 2> reached_enemy_home;
    std::array<int, 2>  pieces_left;
    std::array<int, 2> max_num_occ;

    std::array<std::array<int, COL>, ROW> data;     // worth making private?
};

///////////// Getters /////////////

inline
const int& NardiBoard::at(size_t r, size_t c) const
{   return data.at(r).at(c);   }

inline
const int& NardiBoard::at (const NardiCoord& s) const
{   return at(s.row, s.col);   }

inline 
bool NardiBoard::PlayerIdx() const
{   return player_idx;   }

inline
int NardiBoard::PlayerSign() const
{   return player_sign;   }

inline
bool NardiBoard::HeadUsed() const
{   return head_used;   }

inline
const std::array<int, 2>& NardiBoard::MaxNumOcc() const
{   return max_num_occ;   }


inline 
const std::array<int, 2>& NardiBoard::ReachedEnemyHome() const
{   return reached_enemy_home;   }

inline 
const std::array<int, 2>& NardiBoard::PiecesLeft() const
{   return pieces_left;   }

///////////// Updates and Actions /////////////

inline
void NardiBoard::SwitchPlayer()
{
    player_idx = !player_idx;
    player_sign = BoolToSign(player_idx);
    head_used = false;
}

inline 
void NardiBoard::FlagHeadIfNeeded(const NardiCoord& start)
{ 
    if(!head_used && IsPlayerHead(start))
        head_used = true; 
}

///////////// Legality Helpers /////////////


inline
bool NardiBoard::IsPlayerHead(const NardiCoord& coord) const
{   return (coord.row == player_idx && coord.col == 0);   }

inline
bool NardiBoard::HeadReuseIssue(const NardiCoord& coord) const
{   return (IsPlayerHead(coord) && head_used);   }

inline
bool NardiBoard::CurrPlayerInEndgame() const
{   return reached_enemy_home[player_idx] >= PIECES_PER_PLAYER;   }