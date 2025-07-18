#pragma once

#include "Auxilaries.h"

#include <iostream>
#include <array>
#include <stack>
#include <queue>
#include <unordered_set>
#include <random>

/*

Case: mtnel during turn

undo move feature

Automate dice rolling from controller

Chi kareli chtoghel tun mtnel araji angam -- check details of this rule
    - bool mtac[2], mtneluc -> mtac[player] = true
    - legality check: yete mtac[opp] == false, qo tun mtneluc: 
        - azat tegh(er) mnum a? 
            - che -> illegal
            - ha -> hnaravor a drancic mek@ mtnel? bolori naxkin 6 tegh@ stugel...
                -> sagh pak: illegal
                -> bac tegher: ardyok hnaravor a mtnel?

Rule precedence: only available move creates block issue not allowed

Thoroughly test basic mechanics
*/


class ReaderWriter;

class Game
{
    public:
        // construction and initialization:
        Game(int seed = 1); 
        void AttachReaderWriter(ReaderWriter* r);

        // enum class for scoping and extra safety
        enum class status_codes 
        {
            SUCCESS,
            NO_LEGAL_MOVES_LEFT,
            OUT_OF_BOUNDS,
            START_EMPTY_OR_ENEMY,
            DEST_ENEMY,
            BACKWARDS_MOVE, 
            BOARD_END_REACHED, 
            NO_PATH,
            ILLEGAL_MOVE,
            START_RESELECT,
            DICE_USED_ALREADY,
            HEAD_PLAYED_ALREADY,
            MISC_FAILURE
        };        

        // Gameplay
        status_codes RollDice();
        status_codes TryStart(const NardiCoord& s) const;
        status_codes TryFinishMove(const NardiCoord& start, const NardiCoord& end);     // No Removals
        status_codes TryMoveByDice(const NardiCoord& start, bool dice);                 // Removals and regular moves
        void SwitchPlayer();
        
        // State Checks
        bool CurrPlayerInEndgame() const;
        bool GameIsOver() const;

        // Getters
        int GetDice(bool idx) const;
        int GetPlayerSign() const;
        bool GetPlayerIdx() const;
        const std::array<std::array<int, COL>, ROW>& GetBoardRef() const;
        const ReaderWriter* GetConstRW();   // const pointer, allowing controller to read commands and ask for 

        // Friend class for testing
        friend class TestBuilder;

    private:
        std::array<std::array<int, COL>, ROW> board;    // first row of board is reversed from view
        std::array<int, 2>  pieces_left;
        std::mt19937 rng;                           // Mersenne Twister engine
        std::uniform_int_distribution<int> dist;    // uniform distribution for dice
        std::array<int, 2> dice; 
        std::array<int, 2> times_dice_used;
        bool doubles_rolled;
        ReaderWriter* rw;
       
        class Arbiter
        {
            public:
                Arbiter(Game* gp);

                // Legality Checks
                status_codes ValidStart(int sr, int sc) const; // check that start is in bounds and occupied by player's piece
                std::pair<status_codes, NardiCoord> CanMoveByDice   (const NardiCoord& start, bool dice_idx, 
                                                                    bool moved_hypothetically = false) const;
                bool CanRemovePiece(const NardiCoord& start, bool d_idx);
                std::pair<status_codes, std::array<int, 2>> LegalMove(int sr, int sc, int er, int ec);

                // Updates and Actions
                void IncrementTurnNumber();
                void FlagHeadIfNeeded(const NardiCoord& start);
                void SwitchPlayer();

                status_codes OnRoll();
                status_codes OnMove(const NardiCoord& start, const NardiCoord& end);
                status_codes OnRemoval(const NardiCoord& start);


                // Getters and state checks
                int GetPlayerSign() const;
                bool GetPlayerIdx() const;
                bool CurrPlayerInEndgame() const;

                // friend class for testing
                friend class TestBuilder;

            private:
                Game* g;
                bool head_used;
                bool player_idx;
                int player_sign;
                std::array<int, 2> turn_number;
                std::array<int, 2> in_enemy_home;
                const NardiCoord head[2] = {NardiCoord(0, 0), NardiCoord(1, 0)};
                std::array< std::array< std::unordered_set<NardiCoord>, 6 >, 2 > goes_idx_plusone;
                std::array<size_t, 2> min_options;
                std::array<int, 2> max_num_occ;
                
                // Legality Helpers
                status_codes WellDefinedEnd(int sr, int sc, int er, int ec) const;      // check that move end from start is friendly or empty
                status_codes WellDefinedEnd(const NardiCoord& start, const NardiCoord& end) const;
                bool BadRowChange(bool er, bool player) const;
                bool BadRowChange(bool er) const;

                std::pair<Game::status_codes, NardiCoord> LegalMove_2step(bool sr, int sc);
                
                bool CanUseDice(bool idx, int n_times = 1) const;
                bool PreventsTurnCompletion(const NardiCoord& start, bool dice_idx) const;
                bool StepsTwice(const NardiCoord& start) const;
                
                bool IsHead(const NardiCoord& c) const;
                bool IsHead(int r, int c) const;
                bool HeadReuseIssue(const NardiCoord& c) const;
                bool HeadReuseIssue(int r, int c) const;

                bool InBounds(const NardiCoord& coord) const;
                bool InBounds(int r, int c) const;
                
                // Coord and Distance Calculations
                NardiCoord CoordAfterDistance(int row, int col, int d, bool player)const;
                NardiCoord CoordAfterDistance(const NardiCoord& start, int d, bool player) const;
                unsigned GetDistance(bool sr, int sc, bool er, int ec) const;

                // Forced Moves
                status_codes CheckForcedMoves();

                status_codes CheckForced_2Dice();
                status_codes HandleForced2Dice(bool dice_idx, const std::unordered_set<NardiCoord>& two_step_starts);
                
                status_codes CheckForced_1Dice();
                status_codes HandleForced1Dice(bool dice_idx);
                
                status_codes CheckForced_Doubles();
                void Force_1stMoveException();
                
                Game::status_codes ForceMove(const NardiCoord& start, bool dice_idx, bool check_further = true);
                Game::status_codes ForceRemovePiece(const NardiCoord& start, bool dice_idx);


                // Updates and Actions
                void UpdateAvailabilitySets(const NardiCoord start, const NardiCoord end);  // fixme cleanup code
                void UpdateAvailabilitySets(const NardiCoord start);

                void Reset();
                void ResetHead();
                void SetMaxOcc();
        };
        
        Arbiter arbiter;
        
        status_codes MakeMove(const NardiCoord& start, const NardiCoord& end, bool check_needed = true);
        status_codes RemovePiece(const NardiCoord& start);
        void UseDice(bool idx, int n = 1);
};


///////////////////////////
////////   Game   ////////
/////////////////////////

///////////// Initialization /////////////

inline
void Game::AttachReaderWriter(ReaderWriter* r)
{   rw = r;   }

///////////// Gameplay /////////////

inline 
void Game::UseDice(bool idx, int n)
{   times_dice_used[idx] += n;  }

inline
void Game::SwitchPlayer()
{   arbiter.SwitchPlayer();  }

///////////// State Checks /////////////

inline 
bool Game::CurrPlayerInEndgame() const
{   return arbiter.CurrPlayerInEndgame();    }

inline
bool Game::GameIsOver() const 
{   return (pieces_left[0] == 0 || pieces_left[1] == 0);   }

///////////// Getters /////////////

inline 
int Game::GetDice(bool idx) const
{   return dice[idx];   }

inline
int Game::GetPlayerSign() const
{   return arbiter.GetPlayerSign();   }

inline 
bool Game::GetPlayerIdx() const
{   return arbiter.GetPlayerIdx();   }

inline
const std::array<std::array<int, COL>, ROW>& Game::GetBoardRef() const
{   return board;   }

inline const ReaderWriter* Game::GetConstRW() 
{   return rw;   }

//////////////////////////////
////////   Arbiter   ////////
////////////////////////////

///////////// Legality /////////////

inline
bool Game::Arbiter::IsHead(const NardiCoord& coord) const
{ return (coord.row == player_idx && coord.col == 0); }

inline 
bool Game::Arbiter::IsHead(int r, int c) const
{ return (r == player_idx && c == 0); }

inline
bool Game::Arbiter::HeadReuseIssue(const NardiCoord& coord) const
{ return (IsHead(coord) && head_used); }

inline
bool Game::Arbiter::HeadReuseIssue(int r, int c) const
{ return HeadReuseIssue({r, c}); }

inline
bool Game::Arbiter::InBounds(const NardiCoord& coord) const
{
    return InBounds(coord.row, coord.col);
}

inline
bool Game::Arbiter::InBounds(int r, int c) const
{
    return (r >= 0 && r < ROW && c >= 0 && c < COL);
}

///////////// Updates and Actions /////////////

inline 
void Game::Arbiter::IncrementTurnNumber()
{   ++turn_number[player_idx];  }

inline 
void Game::Arbiter::FlagHeadIfNeeded(const NardiCoord& start)
{ 
    if(!head_used && IsHead(start))
        head_used = true; 
}

inline
void Game::Arbiter::SwitchPlayer(){
    player_idx  = !player_idx;
    player_sign = -player_sign;
    Reset();
}

inline
void Game::Arbiter::ResetHead()
{ head_used = false; }

inline
void Game::Arbiter::Reset()
{   ResetHead();   }

///////////// Getters and State Checks /////////////

inline
int Game::Arbiter::GetPlayerSign() const
{ return player_sign; }

inline
bool Game::Arbiter::GetPlayerIdx() const
{ return player_idx; }

inline bool Game::Arbiter::CurrPlayerInEndgame() const
{ return (in_enemy_home[player_idx] == PIECES_PER_PLAYER); }