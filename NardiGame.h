#pragma once

#include "Auxilaries.h"
#include "NardiBoard.h"

#include <array>
#include <stack>
#include <queue>
#include <random>

/*
Pass ownership of goes_idx to board... 
    needs board to handle the 1st move exception
    const reference Getters for the sets...

Automate dice rolling from controller ?

Chi kareli chtoghel tun mtnel araji angam -- details:
    Can't block >= 6 consecutive squares without at least 1 enemy piece ahead of the block


Rule precedence: only available move creates block issue not allowed

undo move feature

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
                std::array<int, 2> turn_number;
                bool forcing_doubles;

                std::array< std::array< std::unordered_set<NardiCoord>, 6 >, 2 > goes_idx_plusone;

                std::array<size_t, 2> min_options;
                
                
                // Legality Helpers
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
                
                status_codes ForceMove(const NardiCoord& start, bool dice_idx);
                status_codes ForceRemovePiece(const NardiCoord& start, bool dice_idx);

                // Updates and Actions
                void UpdateAvailabilitySets(const NardiCoord start, const NardiCoord end);  // fixme cleanup code
                void UpdateAvailabilitySets(const NardiCoord start);

                // void Reset();
                // void ResetHead();
                // void SetMaxOcc();
        };
        
        Arbiter arbiter;
        
        status_codes MakeMove(const NardiCoord& start, const NardiCoord& end);
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
bool Game::GameIsOver() const 
{   return (board.PiecesLeft().at(0) == 0 || board.PiecesLeft().at(1) == 0);   }

///////////// Getters /////////////

inline 
int Game::GetDice(bool idx) const
{   return dice[idx];   }

inline
const NardiBoard& Game::GetBoardRef() const
{   return board;   }

inline const ReaderWriter* Game::GetConstRW() 
{   return rw;   }

//////////////////////////////
////////   Arbiter   ////////
////////////////////////////

///////////// Updates and Actions /////////////

inline 
void Game::Arbiter::IncrementTurnNumber()
{   ++turn_number[g->board.PlayerIdx()];  }

inline bool Game::Arbiter::CurrPlayerInEndgame() const
{   return g->board.CurrPlayerInEndgame();   }

inline
NardiCoord Game::Arbiter::PlayerHead() const
{   return {g->board.PlayerIdx(), 0};   }