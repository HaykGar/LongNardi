#pragma once

#include "Auxilaries.h"
#include "NardiBoard.h"

#include <array>
#include <stack>
#include <queue>
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

        // Gameplay
        status_codes RollDice();
        status_codes TryStart(const NardiCoord& s) const;
        status_codes TryFinishMove(const NardiCoord& start, const NardiCoord& end);     // No Removals
        status_codes TryMoveByDice(const NardiCoord& start, bool dice);                 // Removals and regular moves
        
        // State Checks
        // bool CurrPlayerInEndgame() const;
        bool GameIsOver() const;

        // Getters
        int GetDice(bool idx) const;
        // int GetPlayerSign() const;
        // bool GetPlayerIdx() const;
        const NardiBoard& GetBoardRef() const;
        const ReaderWriter* GetConstRW();   // const pointer, allowing controller to read commands and ask for 

        // Friend class for testing
        friend class TestBuilder;

    private:
        NardiBoard board;    // reminder: first row of board is reversed from view

        // std::array<int, 2>  pieces_left; moved to board

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
                // status_codes ValidStart(const NardiCoord& start) const; // check that start is in bounds and occupied by player's piece
                std::pair<status_codes, NardiCoord> CanMoveByDice   (const NardiCoord& start, bool dice_idx, 
                                                                    bool moved_hypothetically = false) const;
                bool CanRemovePiece(const NardiCoord& start, bool d_idx);
                std::pair<status_codes, std::array<int, 2>> LegalMove(const NardiCoord& start, const NardiCoord& end);

                // Updates and Actions
                void IncrementTurnNumber();
                // void FlagHeadIfNeeded(const NardiCoord& start);
                // void SwitchPlayer();

                status_codes OnRoll();
                status_codes OnMove(const NardiCoord& start, const NardiCoord& end);
                status_codes OnRemoval(const NardiCoord& start);


                // Getters and state checks
                // int GetPlayerSign() const;
                // bool GetPlayerIdx() const;
                bool CurrPlayerInEndgame() const;

                NardiCoord PlayerHead() const;

                // friend class for testing
                friend class TestBuilder;

            private:
                Game* g;
                // bool head_used;
                // bool player_idx;
                // int player_sign;
                std::array<int, 2> turn_number;
                // std::array<int, 2> in_enemy_home;
                // const NardiCoord head[2] = {NardiCoord(0, 0), NardiCoord(1, 0)};

                std::array< std::array< std::unordered_set<NardiCoord>, 6 >, 2 > goes_idx_plusone;

                std::array<size_t, 2> min_options;
                // std::array<int, 2> max_num_occ;
                
                // Legality Helpers
                // status_codes WellDefinedEnd(const NardiCoord& start, const NardiCoord& end) const; // check that move end from start is friendly or empty

                std::pair<status_codes, NardiCoord> LegalMove_2step(const NardiCoord& start);
                
                bool CanUseDice(bool idx, int n_times = 1) const;
                bool PreventsTurnCompletion(const NardiCoord& start, bool dice_idx) const;
                bool StepsTwice(const NardiCoord& start) const;  // rename, rework, move to board `

                // Forced Moves - implement handlers `
                status_codes CheckForcedMoves();

                status_codes CheckForced_2Dice();
                status_codes HandleForced2Dice(bool dice_idx, const std::unordered_set<NardiCoord>& two_step_starts);
                
                status_codes CheckForced_1Dice();
                status_codes HandleForced1Dice(bool dice_idx);
                
                status_codes CheckForced_Doubles();
                void Force_1stMoveException();
                
                status_codes ForceMove(const NardiCoord& start, bool dice_idx, bool check_further = true);
                status_codes ForceRemovePiece(const NardiCoord& start, bool dice_idx);

                // Updates and Actions
                void UpdateAvailabilitySets(const NardiCoord start, const NardiCoord end);  // fixme cleanup code
                void UpdateAvailabilitySets(const NardiCoord start);

                // void Reset();
                // void ResetHead();
                // void SetMaxOcc();
        };
        
        Arbiter arbiter;
        
        // void SwitchPlayer();
        status_codes MakeMove(const NardiCoord& start, const NardiCoord& end, bool check_needed = true);
            // remove check requested parameter, make silentmakemove, silentforcemove functions instead
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

// inline
// void Game::SwitchPlayer()
// {   arbiter.SwitchPlayer();  }

///////////// State Checks /////////////

// inline 
// bool Game::CurrPlayerInEndgame() const
// {   return board.CurrPlayerInEndgame();    }

inline
bool Game::GameIsOver() const 
{   return (board.PiecesLeft().at(0) == 0 || board.PiecesLeft().at(1) == 0);   }

///////////// Getters /////////////

inline 
int Game::GetDice(bool idx) const
{   return dice[idx];   }

// inline
// int Game::GetPlayerSign() const
// {   return arbiter.GetPlayerSign();   }

// inline 
// bool Game::GetPlayerIdx() const
// {   return board.PlayerIdx();   }

inline
const NardiBoard& Game::GetBoardRef() const
{   return board;   }

inline const ReaderWriter* Game::GetConstRW() 
{   return rw;   }

//////////////////////////////
////////   Arbiter   ////////
////////////////////////////

///////////// Legality /////////////


// inline
// bool Game::Arbiter::InBounds(const NardiCoord& coord) const
// {
//     return InBounds(coord.row, coord.col);
// }

// inline
// bool Game::Arbiter::InBounds(int r, int c) const
// {
//     return (r >= 0 && r < ROW && c >= 0 && c < COL);
// }

///////////// Updates and Actions /////////////

inline 
void Game::Arbiter::IncrementTurnNumber()
{   ++turn_number[g->board.PlayerIdx()];  }

// inline 
// void Game::Arbiter::FlagHeadIfNeeded(const NardiCoord& start)
// { 
//     if(!head_used && IsHead(start))
//         head_used = true; 
// }

// inline
// void Game::Arbiter::SwitchPlayer(){
//     player_idx  = !player_idx;
//     player_sign = -player_sign;
//     Reset();
// }

// inline
// void Game::Arbiter::ResetHead()
// { head_used = false; }

// inline
// void Game::Arbiter::Reset()
// {   ResetHead();   }

///////////// Getters and State Checks /////////////

// inline
// int Game::Arbiter::GetPlayerSign() const
// { return player_sign; }

// inline
// bool Game::Arbiter::GetPlayerIdx() const
// { return player_idx; }

inline bool Game::Arbiter::CurrPlayerInEndgame() const
{   return g->board.CurrPlayerInEndgame();   }

inline
NardiCoord Game::Arbiter::PlayerHead() const
{   return {g->board.PlayerIdx(), 0};   }