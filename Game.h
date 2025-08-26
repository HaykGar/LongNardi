#pragma once

#include "Auxilaries.h"
#include "Board.h"

#include <array>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <random>
#include <memory>

/*
more than 1 block at a time?????

Endgame legality, can we actually remove the piece without leaving an illegal block?

endgame testing: include case of starting endgame mid-turn, and also in forced checking

Logging services... log everything in files instead of cout

TryFinishMove, legal move checkers, etc, need to be integrated with hanel in endgame...

Automate dice rolling from controller ?

On select start, check forced moves from there, return this if valid start?

undo move feature

*/

namespace Nardi
{

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
        status_codes TryStart(const Coord& s);
        status_codes TryFinishMove(const Coord& start, const Coord& end);     // No Removals
        status_codes TryMoveByDice(const Coord& start, bool dice);                 // Removals and regular moves
        
        void SwitchPlayer();

        // State Checks
        bool GameIsOver() const;

        // Getters
        const Board& GetBoardRef() const;
        const std::vector< std::vector<StartAndDice> >& ViewAllLegalMoveSeqs() const;

        int GetDice(bool idx) const;
        const ReaderWriter* GetConstRW();

        // Friend class for testing
        friend class TestBuilder;

    private:
        class Arbiter;
        
        // Exception Monitors

        class PreventionMonitor
        {
            public:
                PreventionMonitor(Game& g);

                bool Illegal(const Coord& start, bool dice_idx);
                bool CheckNeeded();
            private:
                Game& _g;
                bool _completable;
                std::array<int, 2> turn_last_updated;
     
                bool MakesSecondStep(const Coord& start) const;   
                bool TurnCompletable();
                void SetCompletable();
        };

        class BadBlockMonitor
        {
            public:
                BadBlockMonitor(Game& g);
                enum class block_state
                {
                    CLEAR,
                    FIXABLE_BLOCK,
                    BAD_BLOCK
                };

                bool Illegal(const Coord& start, bool dice_idx);
                bool Illegal(const Coord& start, const Coord& end);
                
                bool PreConditions();

                void Reset();
                void Solidify();

                block_state State() const;

            private:
                Game& _g;                
                bool _blockedAll;
                bool _isSolidified;
                block_state _state;

                unsigned _blockLength;
                Coord _blockStart;

                bool CheckMockedState();

                bool BlockageAround(const Coord& end);
                bool PieceAhead();
                bool WillBeFixable();
                bool BlockingAll();
                bool StillBlocking();

                bool Unblocks(const Coord& start, const Coord& end);
                bool Unblocked();
        };

        // Arbiter, tying these together
        class Arbiter
        {
            public:
                Arbiter(Game& gm);

                // Legality Checks and helpers
                std::pair<status_codes, Coord> CanMoveByDice(const Coord& start, bool dice_idx);
                std::pair<status_codes, Coord> CanFinishByDice   (const Coord& start, bool dice_idx);
                std::pair<status_codes, std::array<int, 2>> LegalMove(const Coord& start, const Coord& end);
                std::pair<status_codes, Coord> LegalMove_2step(const Coord& start);

                bool DiceRemovesFrom(const Coord& start, bool d_idx);

                bool CanUseMockDice(bool idx, int n_times = 1) const;

                bool IllegalBlocking(const Coord& start, bool d_idx);
                bool IllegalBlocking(const Coord& start, const Coord& end);
                
                // Getters
                const std::vector<Coord>& GetMovables(bool idx);
                std::unordered_set<Coord> GetTwoSteppers(size_t max_qty, const std::array<std::vector<Coord>, 2>& to_search);
                std::unordered_set<Coord> GetTwoSteppers(size_t max_qty);

                BadBlockMonitor::block_state BlockState() const;

                // Updates and Actions
                status_codes OnRoll();
                status_codes OnMove();
                status_codes OnRemoval();

                void OnMockChange();

                // friend class for testing
                friend class TestBuilder;

            private:
                Game& _g;
                std::array< std::vector<Coord>, 2 >  _movables;

                PreventionMonitor   _prevMonitor;
                BadBlockMonitor     _blockMonitor;

                // Forced Moves
                status_codes CheckForcedMoves();

                void UpdateMovables();
        };

        struct BoardWithMocker
        {
            public:
                BoardWithMocker(Game& g);

                // boards 
                Board _realBoard;
                Board _mockBoard;

                // wrap with real Board for convenience
                bool PlayerIdx() const;
                int PlayerSign() const;

                bool IsPlayerHead(const Coord& c) const;

                // check
                bool MisMatch() const;

                // Updates and Actions
                void ResetMock();
                void RealizeMock();
            
                void Move(const Coord& start, const Coord& end);
                void Remove(const Coord& to_remove);

                void Mock_Move(const Coord& start, const Coord& end);
                void Mock_UndoMove(const Coord& start, const Coord& end);

                void Mock_Remove(const Coord& to_remove);
                void Mock_UndoRemove(const Coord& to_remove);

                void SwitchPlayer();

            private:
                Game& _game;
        };

        struct LegalSeqComputer
        {
            public:
                LegalSeqComputer(Game& g);
                void ComputeAllLegalMoves();
                const std::vector< std::vector<StartAndDice> >& ViewMoveSeqs() const;
            
            private:
                Game& _g;
                std::unordered_map<std::string, std::vector<StartAndDice> > _brdsToSeqs;
                std::vector< std::vector<StartAndDice> > _vals;

                std::unordered_set<std::string> _encountered;

                void dfs(std::vector<StartAndDice>& seq);
                bool ForceFirstMove();
        };

        BoardWithMocker board;                      // contains real and mock boards

        std::mt19937 rng;                           // Mersenne Twister engine
        std::uniform_int_distribution<int> dist;    // uniform distribution for dice

        std::array<int, 2> dice; 
        std::array<int, 2> times_dice_used;
        std::array<int, 2> times_mockdice_used;

        bool doubles_rolled;
        std::array<int, 2> turn_number;

        ReaderWriter* rw;
        Arbiter arbiter;
        LegalSeqComputer legal_turns;

        // Getters
        const std::unordered_set<Coord>& PlayerGoesByMockDice(bool dice_idx) const;
        const std::unordered_set<Coord>& PlayerGoesByDice(bool dice_idx) const;

        Coord PlayerHead() const;

        // Dice Actions
        status_codes OnRoll();
        void SetDice(int d1, int d2);
        void UseDice(bool idx, int n = 1);

        // Updates
        void IncrementTurnNumber();
        void ReAnimate();
        void AnimateDice();
        
        // Moving
        status_codes MakeMove(const Coord& start, const Coord& end);
        status_codes MakeMove(const Coord& start, bool dice_idx);

        status_codes RemovePiece(const Coord& start);

        // Mocking
        bool SilentMock(const Coord& start, const Coord& end);
        bool SilentMock(const Coord& start, bool dice_idx);
    
        // void MockAndUpdate(const Coord& start, const Coord& end);
        bool UndoSilentMock(const Coord& start, const Coord& end);
        bool UndoSilentMock(const Coord& start, bool dice_idx);

        // void UndoMockAndUpdate(const Coord& start, const Coord& end);
        void MockAndUpdateByDice(const Coord& start, bool dice_idx);
        void UndoMockAndUpdateByDice(const Coord& start, bool dice_idx);
        void RealizeMock();
        void ResetMock();
};


}   // namespace Nardi