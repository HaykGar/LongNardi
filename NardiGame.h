#pragma once

#include <iostream>
#include <array>
#include <stack>
#include <random>

/*
Will need to capture game states and later allow undo move feature

Check for only one piece from the "head" per turn (with the exceptions 66, 44 on move 1)

Chi kareli chtoghel tun mtnel araji angam

Hanel@ verjum

juxt

Legal move checks, forced to do a specific combination so both dice can be used, as opposed to one legal moves and no moves left

handle no legal moves case

Thoroughly test basic mechanics
*/

const int ROW = 2;
const int COL = 12;

class ReaderWriter;

class Game{

    public:
        Game(int seed = 1);
        
        void PlayGame();

        void UseDice(bool idx, unsigned reps = 1); // should be private but I want arbiter to access it

        void AttachReaderWriter(ReaderWriter* r);

        int GetBoardRowCol(int row, int col) const;

        int GetPlayerSign() const;

        void ClearBoard();

        unsigned GetDistance(int sr, int sc, int er, int ec) const;

    private:
        std::array<std::array<int, COL>, ROW> board;
        int player_sign;

        std::mt19937 rng;                           // Mersenne Twister engine
        std::uniform_int_distribution<int> dist;    // uniform distribution for dice
        int dice[2] = {0, 0};
        int dice_used[2] = {0, 0};
        bool doubles_rolled;

        void RollDice(); // set both dice to random integer 1 to 6, important to force this to happen each turn.

        std::array<int, 2> CalculateFinalCoords(int sr, int sc, int d) const;

        class Arbiter
        {
            public:
                Arbiter(Game* gp);
                bool ValidStart(int sr, int sc) const; // check that start is in bounds and occupied by player's piece
                bool LegalMove(int start_row, int start_col, int end_row, int end_col) const;

            private:
                Game* g;

                bool LegalMove_2step(int start_row, int start_col) const;
                bool BadRowChange(int sr, int er) const;
                bool WellDefinedMove(int start_row, int start_col, int end_row, int end_col) const; // check that move start and end are not against the rule sin

        };

        struct Move{
            Move(int sr, int sc, int er, int ec, int d1, int d2);

            int startRow;
            int startCol;
            int endRow;
            int endCol;
            int dice1;
            int dice2;
        };

        Arbiter arbiter;
        ReaderWriter* rw;
        std::stack<Move> move_history;
        
        void MakeMove(int start_row, int start_col, int end_row, int end_col);
        void UndoMove(); // FIXME: add the functionality in PlayGame
            // as it is now, undoes entire turn so far. Once a turn is over it is over strictly.
        bool MoveOver() const;
};

class ReaderWriter
{
    public:
        ReaderWriter(const Game& game);
        virtual void ReAnimate() const = 0;         // show the current state of the game
        virtual void AnimateDice(int d1, int d2) const = 0;
        virtual bool ReadQuitOrProceed() const = 0;                // read in quit or continue from user before every dice roll
        virtual std::array<int, 2> ReportSelectedSlot() const = 0;    // Return coordinates of slot user selects, either dest or source
        virtual void InstructionMessage(std::string m) const = 0;      // In some implementations may even do nothing
        virtual void ErrorMessage(std::string m) const = 0;
    
        protected:
        const Game& g;

};

inline
int Game::GetBoardRowCol(int row, int col) const
{
    return board.at(row).at(col);    // .at() handles out of bounds issues
}

inline
int Game::GetPlayerSign() const
{
    return player_sign;
}

inline
void Game::AttachReaderWriter(ReaderWriter* r)
{
    rw = r;
}