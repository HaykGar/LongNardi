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
    Board(const BoardConfig& d);

    // Getters
    const int8_t& at(const Coord& s) const;
    const int8_t& at(size_t r, size_t c) const;
    const BoardConfig& View() const;
    
    bool PlayerIdx() const;
    int8_t PlayerSign() const;
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

    // Nested struct for feature extraction
    //features are from perspective of player whose move it is. Hence, no negative values.
    struct BoardFeatures
    {
        struct PlayerBoardInfo
        {
            /*
            first row of each sub-occ array is 1 for every coord that player occupies, 0 everywhere else
            second "                     " 1 "                                  " with 2 or more pieces
            third "                      " n_pieces - 2 "                       " with 3 or more pieces
            */
            std::array<std::array<uint8_t, ROWS*COLS>, 3> occ{};    // 0-init

            int pip_count = 0;      // number of 1-moves to remove all pieces

            uint8_t pieces_off; // pieces removed by player
            uint8_t pieces_not_reached;
            uint8_t sq_occ;
        };
        
        PlayerBoardInfo player;
        PlayerBoardInfo opp;

        std::array<std::array<uint8_t, ROWS*COLS>, 2> raw_plyr_channels{};
    };

    const BoardFeatures ExtractFeatures() const;

    friend class ScenarioBuilder;
private:
    BoardConfig data;

    bool player_idx;
    int8_t player_sign;
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
    void SetData(const BoardConfig& b);
    void CalcPiecesLeftandReached();
};

} // namespace Nardi