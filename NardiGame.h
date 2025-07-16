#pragma once

#include "Auxilaries.h"

#include <iostream>
#include <array>
#include <stack>
#include <queue>
#include <unordered_set>
#include <random>

/*
Check for only one piece from the "head" per turn 
    (with the exceptions 66, 44 on move 1)

Legality issue (see test_cases)

undo move feature

UI and also for driver: let them move pieces by dice, so input start coord and number

Automate dice rolling from controller

Hanel@ verjum

Handle Win, hanel@ vor prcni it'll alert. Possibly increment a score or something, maybe controller or display handles that
    counters for pieces in house...

Clean up Coord vs row and col

Chi kareli chtoghel tun mtnel araji angam -- check details of this rule
    - bool mtac[2], mtneluc -> mtac[player] = true
    - legality check: yete mtac[opp] == false, qo tun mtneluc: 
        - azat tegh(er) mnum a? 
            - che -> illegal
            - ha -> hnaravor a drancic mek@ mtnel? bolori naxkin 6 tegh@ stugel...
                -> sagh pak: illegal
                -> bac tegher: ardyok hnaravor a mtnel?

Thoroughly test basic mechanics
*/


class ReaderWriter;

class Game
{
    public:
        Game(int seed = 1); 

        enum class status_codes // enum class for scoping and extra safety
        {
            SUCCESS,
            NO_LEGAL_MOVES_LEFT,
            OUT_OF_BOUNDS,
            START_EMPTY_OR_ENEMY,
            DEST_ENEMY,
            BACKWARDS_MOVE, 
            BOARD_END_REACHED, 
            NO_PATH_TO_DEST,
            START_RESELECT,
            DICE_USED_ALREADY,
            HEAD_PLAYED_ALREADY,    // set a flag to false every time dice rolled, whenever board[player_idx][0] is played, maybe need one for selected, one for played...
            MISC_FAILURE
        }; // 
        
        void AttachReaderWriter(ReaderWriter* r);

        status_codes RollDice(); // set both dice to random integer 1 to 6

        status_codes TryStart(const NardiCoord& s) const;
        status_codes TryFinishMove(const NardiCoord& start, const NardiCoord& end); // assuming valid start already
        std::pair<Game::status_codes, NardiCoord> TryMoveByDice(const NardiCoord& start, bool dice);


        // void UndoMove(); // FIXME: add the functionality in Controller
            // as it is now, undoes entire turn so far. Once a turn is over it is over strictly. Also, will need changes for legality dicts `

        void SwitchPlayer();
        
        const std::array<std::array<int, COL>, ROW>& GetBoardRef() const;

        int GetPlayerSign() const;
        bool GetPlayerIdx() const;
        
        bool GameIsOver() const;

        int GetDice(bool idx) const;

        const ReaderWriter* GetConstRW();   // const pointer, allowing controller to read commands and ask for 

        bool TurnOver() const;

        // For Testing Only
        friend class TestBuilder;

    private:
        std::array<std::array<int, COL>, ROW> board;
        // first row of board is reversed in view, but stored this way for convenience/efficiency
        bool player_idx;
        int player_sign;

        std::mt19937 rng;                           // Mersenne Twister engine
        std::uniform_int_distribution<int> dist;    // uniform distribution for dice
        int dice[2] = {0, 0};
        int dice_used[2] = {0, 0};
        bool doubles_rolled;

        void UseDice(bool idx, int n = 1);

        class Arbiter
        {
            public:
                Arbiter(Game* gp);
                status_codes ValidStart(int sr, int sc) const; // check that start is in bounds and occupied by player's piece
                std::pair<status_codes, std::array<int, 2>> LegalMove(int sr, int sc, int er, int ec);
                
                status_codes MakeForcedMovesBothDiceUsable();
                status_codes MakeForcedMoves_SingleDice();

                std::pair<status_codes, NardiCoord> CanMoveByDice(const NardiCoord& start, bool dice_idx) const;

                const NardiCoord& GetMidpoint() const;

                unsigned GetDistance(bool sr, int sc, bool er, int ec) const;

                NardiCoord CalculateFinalCoords(bool sr, int sc, bool dice_idx) const;
                NardiCoord CoordAfterDistance(int row, int col, int d) const;
                NardiCoord CoordAfterDistance(const NardiCoord& start, int d) const;

                void UpdateAvailabilitySets(const NardiCoord start, const NardiCoord end);

                void ResetHead();
                void FlagHeadIfNeeded(const NardiCoord& start);
                bool IsHead(const NardiCoord& c) const;
                bool IsHead(int r, int c) const;
                bool HeadReuseIssue(const NardiCoord& c) const;
                bool HeadReuseIssue(int r, int c) const;

                bool CanUseDice(bool idx, int n_times = 1) const;
                status_codes MakeForcedMoves();

                friend class TestBuilder;

            private:
                Game* g;
                bool head_used;
                NardiCoord midpoint;

                const NardiCoord head[2] = {NardiCoord(0, 0), NardiCoord(1, 0)};
                std::array< std::array< std::unordered_set<NardiCoord>, 6 >, 2 > goes_idx_plusone;

                Game::status_codes ForceMove(const NardiCoord& start, bool dice_idx, bool check_further_forced = true);
                
                status_codes LegalMove_2step(bool sr, int sc);
                bool BadRowChange(bool er) const;
                status_codes WellDefinedMove(int sr, int sc, int er, int ec) const; // check that move start and end are not against the rules
                status_codes WellDefinedMove(const NardiCoord& start, const NardiCoord& end) const; // check that move start and end are not against the rules

                status_codes ForcedMoves_DoublesCase();
                status_codes HandleForced2DiceCase(bool dice_idx, const std::unordered_set<NardiCoord>& two_step_starts);
                status_codes HandleSingleDiceCase(bool dice_idx);
        };
        // struct Move{
        //     Move(const NardiCoord& s, const NardiCoord& e, int d1, int d2);

        //     NardiCoord start;
        //     NardiCoord end;
        //     int m_diceUsed1;
        //     int m_diceUsed2;
        // };

        Arbiter arbiter;
        ReaderWriter* rw;
        // std::stack<Move> move_history;
        
        status_codes MakeMove(const NardiCoord& start, const NardiCoord& end, bool check = true);
};

inline
const std::array<std::array<int, COL>, ROW>& Game::GetBoardRef() const
{
    return board;
}

inline
int Game::GetPlayerSign() const
{
    return player_sign;
}

inline 
bool Game::GetPlayerIdx() const
{ return player_idx; }

inline
void Game::AttachReaderWriter(ReaderWriter* r)
{
    rw = r;
}

inline 
int Game::GetDice(bool idx) const
{
    return dice[idx];
}

inline
bool Game::GameIsOver() const {return false; } // FIXME LATER, won't even be inline `

inline const ReaderWriter* Game::GetConstRW() {return rw;}


inline 
void Game::Arbiter::FlagHeadIfNeeded(const NardiCoord& start)
{ 
    if(!head_used && IsHead(start))
        head_used = true; 
}

inline
void Game::Arbiter::ResetHead()
{ head_used = false; }

inline
bool Game::Arbiter::IsHead(const NardiCoord& coord) const
{ return (coord.row == g->player_idx && coord.col == 0); }

inline 
bool Game::Arbiter::IsHead(int r, int c) const
{ return (r == g->player_idx && c == 0); }

inline
bool Game::Arbiter::HeadReuseIssue(const NardiCoord& coord) const
{ return (IsHead(coord) && head_used); }

inline
bool Game::Arbiter::HeadReuseIssue(int r, int c) const
{ return (IsHead(r, c) && head_used); }
