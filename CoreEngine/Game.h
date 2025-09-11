#pragma once

#include "Auxilaries.h"
#include "Board.h"

#include <array>
#include <stack>
#include <algorithm>
#include <ranges>
#include <unordered_set>
#include <unordered_map>
#include <random>

/*
Undo Mock via stack

Add Command for displaying all the legal move seqs and how they end...

During endgame, if it's a valid start just check if there's a forced move from here, will streamline play

more than 1 block at a time?????

endgame testing: include case of starting endgame mid-turn, and also in forced checking

Logging services... log everything in files instead of cout

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
        status_codes OnRoll();
        status_codes TryStart(const Coord& s);
        status_codes TryFinishMove(const Coord& start, const Coord& end);   // No Removals
        status_codes TryFinishMove(const Coord& start, bool dice);          // Removals and regular moves

        bool AutoPlayTurn(const BoardKey& key); // currently unused, useful later though
        
        void SwitchPlayer();

        // State Checks
        bool GameIsOver() const;
        bool IsMars() const;

        // Getters
        const Board& GetBoardRef() const;
        BoardKey GetBoardAsKey() const;
        const std::unordered_map<BoardKey, MoveSequence, BoardKeyHash>& GetBoards2Seqs() const;

        int GetDice(bool idx) const;
        const ReaderWriter* GetConstRW();

        // Friend class for testing
        friend class ScenarioBuilder;

    private:        
        // Exception Monitors

        class PreventionMonitor
        {
            public:
                PreventionMonitor(Game& g);

                bool Illegal(const Coord& start, bool dice_idx);
            private:
                Game& _g;
                bool _completable;
     
                bool TurnCompletable();
                void SetCompletable();
                bool CheckNeeded();
        };

        class BadBlockMonitor
        {
            public:
                BadBlockMonitor(Game& g);

                bool Illegal(const Coord& start, bool dice_idx);
                bool Illegal(const Coord& start, const Coord& end);

                void Reset();

            private:
                Game& _g;                
                bool _blockedAll;

                unsigned _blockLength;
                Coord _blockStart;

                bool PreConditions();
                bool CheckMockedState();
                bool IsFixable();
                bool BlockingAll();
        };

        // Pre-computing legal moves

        struct LegalSeqComputer
        {
            public:
                LegalSeqComputer(Game& g);
                void ComputeAllLegalMoves();

                int MaxLen() const;
                const std::unordered_map<BoardKey, MoveSequence, BoardKeyHash>& BrdsToSeqs() const;
                std::unordered_map<BoardKey, MoveSequence, BoardKeyHash>& BrdsToSeqs();
            
            private:
                Game& _g;
                bool _maxDice;
                std::array<bool, 2> _dieIdxs;
                std::unordered_set<BoardKey, BoardKeyHash> _encountered;

                int _maxLen;
                std::unordered_map<BoardKey, MoveSequence, BoardKeyHash> _brdsToSeqs; // possible board configs map to move sequence that form them

                void dfs(std::vector<StartAndDice>& seq);
                bool FirstMoveException();
        };

        // Arbiter, tying these together
        class Arbiter
        {
            public:
                Arbiter(Game& gm);

                status_codes CheckForcedMoves();

                // Legality Checks and helpers
                status_codes BoardAndBlockLegal(const Coord& start, bool dice_idx);
                status_codes BoardAndBlockLegalEnd(const Coord& start, bool dice_idx);

                status_codes CanStartFrom(const Coord& start);
                std::pair<status_codes, Coord> CanMoveByDice(const Coord& start, bool dice_idx);
                std::pair<status_codes, std::array<int, 2>> LegalMove(const Coord& start, const Coord& end);
                std::pair<status_codes, Coord> LegalMove_2step(const Coord& start);

                bool DiceRemovesFrom(const Coord& start, bool d_idx);

                bool CanUseDice(bool idx, int n_times = 1) const;

                // Updates and Actions
                status_codes OnRoll();

            private:
                Game& _g;

                PreventionMonitor   _prevMonitor;
                BadBlockMonitor     _blockMonitor;

                std::pair<status_codes, Coord> CanFinishByDice(const Coord& start, bool dice_idx);
        };

        Board board;
        // std::stack<std::variant< StartAndDice, std::pair<Coord, Coord> > > mock_hist;    implement and use me `

        std::mt19937 rng;                           // Mersenne Twister engine
        std::uniform_int_distribution<int> dist;    // uniform distribution for dice

        std::array<int, 2> dice; 
        std::array<int, 2> times_dice_used;
        std::array<int, 2> turn_number;

        bool doubles_rolled;
        bool first_move_exception;
        bool maxdice_exception;   // can only play one or the other not both

        ReaderWriter* rw;
        Arbiter arbiter;
        LegalSeqComputer legal_turns;


        // Getters
        Coord PlayerHead() const;

        // Dice Actions
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

        status_codes OnMoveOrRemove();

        // Mocking
        bool MockMove(const Coord& start, const Coord& end);
        bool MockMove(const Coord& start, bool dice_idx);
    
        bool UndoMockMove(const Coord& start, const Coord& end);
        bool UndoMockMove(const Coord& start, bool dice_idx);
};


}   // namespace Nardi