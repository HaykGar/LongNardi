#pragma once

#include "Auxilaries.h"
#include "NardiBoard.h"

#include <array>
#include <stack>
#include <unordered_set>
#include <random>
#include <memory>

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
        const NardiBoard& GetBoardRef() const;

        int GetDice(bool idx) const;
        const ReaderWriter* GetConstRW();

        // Friend class for testing
        friend class TestBuilder;

    private:
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
                
                bool Illegal(const NardiCoord& start, const NardiCoord& end);
                
                virtual bool PreConditions() override;

                virtual void Reset() override;
                void Solidify();

                block_state State() const;

            private:
                // _blocked true when Blockage with no pieces ahead, fixable or not
                bool _blockedAll;
                bool _isSolidified;
                block_state _state;

                unsigned _blockLength;
                NardiCoord _blockStart;
                bool _diceAttempting;

                bool IllegalEndpoints(const NardiCoord& start, const NardiCoord& end);

                bool CreatesBlockageAt(const NardiCoord& end);
                bool BlockageAround(const NardiCoord& end);
                bool PieceAhead();
                bool WillBeFixable();
                bool BlockingAll();

                bool Unblocks(const NardiCoord& start, const NardiCoord& end);
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

                bool IllegalBlocking(const NardiCoord& start, bool d_idx);
                
                // Getters
                const std::vector<NardiCoord>& GetMovables(bool idx);
                std::unordered_set<NardiCoord> GetTwoSteppers(size_t max_qty, const std::array<std::vector<NardiCoord>, 2>& to_search);
                std::unordered_set<NardiCoord> GetTwoSteppers(size_t max_qty);

                // Updates and Actions
                status_codes OnRoll();
                status_codes OnMove();
                status_codes OnRemoval();

                void OnMockChange();
                void OnChange();

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

        class BoardWithMocker
        {
            public:
                BoardWithMocker(Game& g);

                // Getters
                const NardiBoard& ViewReal() const;

                const int& at(const NardiCoord& s) const;
                const int& at(size_t r, size_t c) const;

                const int& Mock_at(const NardiCoord& s) const;
                const int& Mock_at(size_t r, size_t c) const;

                bool PlayerIdx() const;
                int PlayerSign() const;

                bool IsPlayerHead(const NardiCoord& c) const;

                bool HeadUsed() const;
                bool Mock_HeadUsed() const;

                const std::array<int, 2>& MaxNumOcc() const;
                const std::array<int, 2>& ReachedEnemyHome() const;
                const std::array<int, 2>& PiecesLeft() const;
                const std::array< std::array< std::unordered_set<NardiCoord>, 6 >, 2 >& GoesIdxPlusOne() const; 
                const std::unordered_set<NardiCoord>& PlayerGoesByDist(size_t dist) const;

                const std::array<int, 2>& Mock_MaxNumOcc() const;
                const std::array<int, 2>& Mock_ReachedEnemyHome() const;
                const std::array<int, 2>& Mock_PiecesLeft() const;
                const std::array< std::array< std::unordered_set<NardiCoord>, 6 >, 2 >& Mock_GoesIdxPlusOne() const;
                const std::unordered_set<NardiCoord>& Mock_PlayerGoesByDist(size_t dist) const;

                unsigned MovablePieces(const NardiCoord& start) const;
                unsigned Mock_MovablePieces(const NardiCoord& start) const;

                bool MisMatch() const;

                // Updates and Actions
                void ResetMock();
                void RealizeMock();
            
                void Move(const NardiCoord& start, const NardiCoord& end);
                void Remove(const NardiCoord& to_remove);

                void Mock_Move(const NardiCoord& start, const NardiCoord& end);
                void Mock_Remove(const NardiCoord& to_remove);

                void SwitchPlayer();

                // Legality Checks
                status_codes ValidStart(const NardiCoord& s) const;
                status_codes Mock_ValidStart(const NardiCoord& s) const;

                status_codes WellDefinedEnd(const NardiCoord& start, const NardiCoord& end) const;  // check that move end from start is friendly or empty
                status_codes Mock_WellDefinedEnd(const NardiCoord& start, const NardiCoord& end) const;  // check that move end from start is friendly or empty
            
                bool HeadReuseIssue(const NardiCoord& c) const;
                bool Mock_HeadReuseIssue(const NardiCoord& c) const;

                bool CurrPlayerInEndgame() const;
                bool Mock_CurrPlayerInEndgame() const;

                // Calculations
                NardiCoord CoordAfterDistance(const NardiCoord& start, int d, bool player) const;
                NardiCoord CoordAfterDistance(const NardiCoord& start, int d) const;
                int GetDistance(const NardiCoord& start, const NardiCoord& end) const;

                friend class TestBuilder;
            private:
                NardiBoard _realBoard;
                NardiBoard _mockBoard;
                Game& _game;

                // testing
                void SetPlayer(bool player);
        };

        BoardWithMocker board;     // contains real and mock boards

        std::mt19937 rng;                           // Mersenne Twister engine
        std::uniform_int_distribution<int> dist;    // uniform distribution for dice

        std::array<int, 2> dice; 
        std::array<int, 2> times_dice_used;

        std::array<int, 2> times_mockdice_used;

        bool doubles_rolled;
        std::array<int, 2> turn_number;
        ReaderWriter* rw;
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
        
        // Moving
        status_codes MakeMove(const NardiCoord& start, const NardiCoord& end);

        void MockMove(const NardiCoord& start, const NardiCoord& end);
        void MockMoveByDice(const NardiCoord& start, bool dice_idx);
        void RealizeMock();
        void ResetMock();


        status_codes RemovePiece(const NardiCoord& start);

        status_codes ForceMove(const NardiCoord& start, bool dice_idx);
        status_codes ForceRemovePiece(const NardiCoord& start, bool dice_idx);
        
};