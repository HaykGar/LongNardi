#pragma once

#include "Auxilaries.h"
#include "NardiBoard.h"

#include <array>
#include <stack>
#include <random>

/*
Chi kareli chtoghel tun mtnel araji angam -- details:
    Can't block >= 6 consecutive squares without at least 1 enemy piece ahead of the block

Rule precedence: only available move creates block issue not allowed

Pass ownership of goes_idx to board... 
    needs board to handle the 1st move exception
    const reference Getters for the sets...

Automate dice rolling from controller ?

On select start, check forced moves from there, return this if valid start?

undo move feature

*/


class ReaderWriter;

class Game
{
    public:
        // construction and initialization:
        Game(int seed);
        Game();
        void AttachReaderWriter(ReaderWriter* r);

        // Gameplay
        status_codes RollDice();
        status_codes TryStart(const NardiCoord& s) const;
        status_codes TryFinishMove(const NardiCoord& start, const NardiCoord& end);     // No Removals
        status_codes TryMoveByDice(const NardiCoord& start, bool dice);                 // Removals and regular moves
        
        void SwitchPlayer();

        // State Checks
        bool GameIsOver() const;

        // Getters
        int GetDice(bool idx) const;
        const NardiBoard& GetBoardRef() const;
        const ReaderWriter* GetConstRW();

        // Friend class for testing
        friend class TestBuilder;

    private:
        NardiBoard board;    // reminder: first row of board is reversed from view

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
                std::pair<status_codes, NardiCoord> CanMoveByDice   (const NardiCoord& start, bool dice_idx) const;
                bool CanRemovePiece(const NardiCoord& start, bool d_idx);
                std::pair<status_codes, std::array<int, 2>> LegalMove(const NardiCoord& start, const NardiCoord& end);

                // Updates and Actions
                void IncrementTurnNumber();

                status_codes OnRoll();
                status_codes OnMove(const NardiCoord& start, const NardiCoord& end);
                status_codes OnRemoval(const NardiCoord& start);

                // friend class for testing
                friend class TestBuilder;

            private:
                Game* g;
                std::array<int, 2> turn_number;
                bool forcing_doubles;
                std::array<size_t, 2> min_options;

                // Getter
                const std::unordered_set<NardiCoord>& PlayerGoesByDice(bool dice_idx) const;
                NardiCoord PlayerHead() const;

                // Legality Helpers
                std::pair<status_codes, NardiCoord> LegalMove_2step(const NardiCoord& start);
                
                bool CanUseDice(bool idx, int n_times = 1) const;
                bool PreventsTurnCompletion(const NardiCoord& start, bool dice_idx) const;
                bool MakesSecondStep(const NardiCoord& start) const;

                // Forced Moves - implement handlers `
                status_codes CheckForcedMoves();

                status_codes CheckForced_2Dice();
                status_codes HandleForced2Dice(bool dice_idx, const std::stack<NardiCoord>& two_step_starts);
                
                status_codes CheckForced_1Dice();
                status_codes HandleForced1Dice(bool dice_idx);
                
                status_codes CheckForced_Doubles();
                void Force_1stMoveException();
                
                status_codes ForceMove(const NardiCoord& start, bool dice_idx);
                status_codes ForceRemovePiece(const NardiCoord& start, bool dice_idx);
        };
        
        Arbiter arbiter;

        status_codes OnRoll();
        void SetDice(int d1, int d2);
        void UseDice(bool idx, int n = 1);
        
        status_codes MakeMove(const NardiCoord& start, const NardiCoord& end);
        status_codes RemovePiece(const NardiCoord& start);
        
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

inline 
void Game::SwitchPlayer()
{   board.SwitchPlayer();   }

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

///////////// Getters /////////////

inline
const std::unordered_set<NardiCoord>& Game::Arbiter::PlayerGoesByDice(bool dice_idx) const
{   return g->board.PlayerGoesByDist(g->dice[dice_idx]);   }

inline
NardiCoord Game::Arbiter::PlayerHead() const
{   return {g->board.PlayerIdx(), 0};   }

///////////// Updates and Actions /////////////

inline 
void Game::Arbiter::IncrementTurnNumber()
{   ++turn_number[g->board.PlayerIdx()];  }


