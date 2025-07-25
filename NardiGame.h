#pragma once

#include "Auxilaries.h"
#include "NardiBoard.h"

#include <array>
#include <stack>
#include <random>

/*
touch up back block for doubles...

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
        status_codes TryStart(const NardiCoord& s);
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
        NardiBoard mock_board;

        std::mt19937 rng;                           // Mersenne Twister engine
        std::uniform_int_distribution<int> dist;    // uniform distribution for dice

        std::array<int, 2> dice; 
        std::array<int, 2> times_dice_used;

        std::array<int, 2> times_mockdice_used;

        bool doubles_rolled;
        std::array<int, 2> turn_number;
        ReaderWriter* rw;

        class Arbiter;

        // Exception Monitors
        struct RuleExceptionMonitor 
        {
            public:
                RuleExceptionMonitor(Game& g, Arbiter& a);
                
                bool IsFlagged() const;
                virtual void Reset();
                virtual bool PreConditions() = 0;  // conditions for this test to be needed - RENAME FIXME ` or simply make IsFlagged, then overload in first move
                virtual bool Illegal(const NardiCoord& start, bool dice_idx) = 0; // Violation of rules
            protected:
                Game& _g;
                Arbiter& _arb;
                bool _preconditions; // rename or re-design `

        };

        struct FirstMoveException : public RuleExceptionMonitor
        {
            public:
                FirstMoveException(Game& g, Arbiter& a);
                virtual bool Illegal(const NardiCoord& start, bool dice_idx) override;    // parameters not needed here
                virtual status_codes MakeForced();
                virtual bool PreConditions() override;
        };

        struct PreventionMonitor : public RuleExceptionMonitor
        {
            public:
                PreventionMonitor(Game& g, Arbiter& a);

                virtual void Reset() override;
                virtual bool Illegal(const NardiCoord& start, bool dice_idx) override;
                virtual bool PreConditions() override;
            private:
                bool _completable;
                std::array<int, 2> turn_last_updated;

                bool MakesSecondStep(const NardiCoord& start) const;   
                bool TurnCompletable();
             
        };

        struct BadBlockMonitor : public RuleExceptionMonitor    // doubles change things somewhat - 
                                                                // fixable() if we can move multiple pieces !!! fixme `
                                                                // need to attach this logic to CanFinishByDice, legal2step needs to be careful
        {
            public:
                BadBlockMonitor(Game& g, Arbiter& a);
                enum class block_state
                {
                    CLEAR,
                    FIXABLE_BLOCK,
                    BAD_BLOCK
                };

                virtual bool Illegal(const NardiCoord& start, bool dice_idx) override;
                virtual bool PreConditions() override;

                virtual void Reset() override;
                void Solidify();

                block_state State() const;
                const std::unordered_set<NardiCoord>& Unblockers() const;

            private:
                // _blocked true when Blockage with no pieces ahead, fixable or not
                bool _blockedAll;
                bool _isSolidified;
                block_state _state;

                unsigned _blockLength;
                NardiCoord _blockStart;
                bool _diceAttempting;
                std::unordered_set<NardiCoord> _unblockers;

                bool BlockageAt(const NardiCoord& end);
                bool PieceAhead();
                bool WillBeFixable();

                bool BlockingAll();
        };

        // Forced move handlers
        struct ForcedHandler
        {
            public:
                ForcedHandler(Game& g, Arbiter& a);

                virtual bool Is() = 0;
                virtual status_codes Check() = 0;

            protected:
                Arbiter& _arb;
                Game& _g;
        };

        struct DoublesHandler : public ForcedHandler
        {
            public:
                DoublesHandler(Game& g, Arbiter& a);

                virtual bool Is() override;
                virtual status_codes Check() override;
            
            private:
                int steps_left;
                FirstMoveException first_move_checker;

                void MockFrom(NardiCoord start);
        };

        struct SingleDiceHandler : public ForcedHandler
        {
            public:
                SingleDiceHandler(Game& g, Arbiter& a);

                virtual bool Is() override;
                virtual status_codes Check() override;
            protected:
                virtual status_codes ForceFromDice(bool idx);
        };

        struct TwoDiceHandler : public ForcedHandler
        {
            public:
                TwoDiceHandler(Game& g, Arbiter& a);

                virtual bool Is() override;
                virtual status_codes Check() override;
        };

        // Arbiter, tying these together
        class Arbiter
        {
            public:
                Arbiter(Game& gm);

                // Legality Checks and helpers
                std::pair<status_codes, NardiCoord> CanMoveByDice(const NardiCoord& start, bool dice_idx);
                std::pair<status_codes, NardiCoord> CanFinishByDice   (const NardiCoord& start, bool dice_idx);
                std::pair<status_codes, std::array<int, 2>> LegalMove(const NardiCoord& start, const NardiCoord& end);
                std::pair<status_codes, NardiCoord> LegalMove_2step(const NardiCoord& start);

                bool CanRemovePiece(const NardiCoord& start, bool d_idx);

                bool CanUseMockDice(bool idx, int n_times = 1) const;
                bool CanUseDice(bool idx, int n_times = 1) const;

                bool IllegalBlocking(const NardiCoord& start, bool d_idx);
                
                // Getters
                const std::vector<NardiCoord>& GetMovables(bool idx);
                std::unordered_set<NardiCoord> GetTwoSteppers(size_t max_qty, const std::array<std::vector<NardiCoord>, 2>& to_search);
                std::unordered_set<NardiCoord> GetTwoSteppers(size_t max_qty);

                // Updates and Actions
                status_codes OnRoll();
                status_codes OnMove();
                status_codes OnRemoval();

                void SolidifyBlockMonitor();

                // friend class for testing
                friend class TestBuilder;

            private:
                Game& _g;
                std::array< std::vector<NardiCoord>, 2 >  _movables;

                DoublesHandler      _doubles;
                TwoDiceHandler      _twoDice;
                SingleDiceHandler   _singleDice;

                PreventionMonitor   _prevMonitor;
                FirstMoveException  _firstMove;
                BadBlockMonitor     _blockMonitor;

                // Forced Moves
                status_codes CheckForcedMoves();

                void UpdateMovables();
        };
        
        Arbiter arbiter;

        // Getters
        const std::unordered_set<NardiCoord>& PlayerGoesByMockDice(bool dice_idx) const;
        const std::unordered_set<NardiCoord>& PlayerGoesByDice(bool dice_idx) const;


        NardiCoord PlayerHead() const;

        // Dice Actions
        status_codes OnRoll();
        void SetDice(int d1, int d2);
        void UseDice(bool idx, int n = 1);

        // Updates
        void IncrementTurnNumber();
        void ResetMock();
        void RealizeMock(); // implement me `

        
        // Moving
        status_codes MakeMove(const NardiCoord& start, const NardiCoord& end);

        void MockMove(const NardiCoord& start, const NardiCoord& end);
        void MockMoveByDice(const NardiCoord& start, bool dice_idx);


        status_codes RemovePiece(const NardiCoord& start);

        status_codes ForceMove(const NardiCoord& start, bool dice_idx);
        status_codes ForceRemovePiece(const NardiCoord& start, bool dice_idx);
        
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
{   
    times_dice_used[idx] += n;  
    times_mockdice_used = times_dice_used;
}

inline
bool Game::GameIsOver() const 
{   return (board.PiecesLeft().at(0) == 0 || board.PiecesLeft().at(1) == 0);   }

inline 
void Game::SwitchPlayer()
{   
    board.SwitchPlayer();
    mock_board.SwitchPlayer();
}

///////////// Getters /////////////

inline 
int Game::GetDice(bool idx) const
{   return dice[idx];   }

inline
const NardiBoard& Game::GetBoardRef() const
{   return board;   }

inline const ReaderWriter* Game::GetConstRW() 
{   return rw;   }

inline
const std::unordered_set<NardiCoord>& Game::PlayerGoesByMockDice(bool dice_idx) const
{   return mock_board.PlayerGoesByDist(dice[dice_idx]);   }

inline 
const std::unordered_set<NardiCoord>& Game::PlayerGoesByDice(bool dice_idx) const
{   return board.PlayerGoesByDist(dice[dice_idx]);   }

inline
NardiCoord Game::PlayerHead() const
{   return {board.PlayerIdx(), 0};   }


///////////// Updates /////////////

inline 
void Game::IncrementTurnNumber()
{   ++turn_number[board.PlayerIdx()];   }


