#pragma once

#include <iostream>
#include <array>
#include <stack>
#include <random>

/*
Row received as int, make sure that error cases aren't cast to bool and be wary of this

Handle Win, hanel@ vor prcni it'll alert. Possibly increment a score or something, maybe controller or display handles that

unselect start coords if they are chosen again as destination - in controller

controller class

replaced coord from array<int, 2> to NardiCoord

Chi kareli chtoghel tun mtnel araji angam -- check details of this rule
    - bool mtac[2], mtneluc -> mtac[player] = true
    - legality check: yete mtac[opp] == false, qo tun mtneluc: 
        - azat tegh(er) mnum a? 
            - che -> illegal
            - ha -> hnaravor a drancic mek@ mtnel? bolori naxkin 6 tegh@ stugel...
                -> sagh pak: illegal
                -> bac tegher: ardyok hnaravor a mtnel?

Possible move checks, eg forced to do a specific combination so both dice can be used, as opposed to one legal moves and no moves left
handle no or not enough legal moves case

Check for only one piece from the "head" per turn (with the exceptions 66, 44 on move 1)

undo move feature

Hanel@ verjum

Thoroughly test basic mechanics

If no legal move both dice, need to try larger one first

Keep track of legal moves
*/

const int ROW = 2;
const int COL = 12;

// using NardiCoord = std::pair<int, int>;

struct NardiCoord
{
    NardiCoord(int r, int c) : row(r), col(c) {}
    NardiCoord() : row(), col() {}
    int row;
    int col;
};


class ReaderWriter;

class Game{

    public:
        Game(int seed = 1); 

        enum status_codes
        {
            SUCCESS,
            OUT_OF_BOUNDS,
            START_EMPTY_OR_ENEMY,
            DEST_ENEMY,
            BACKWARDS_MOVE, 
            BOARD_END_REACHED, 
            NO_PATH_TO_DEST,
            START_RESELECT, 


            // ...
        }; //   `
        
        void PlayGame(); // TODO move logic to controller class, will need to modify legalmove to include validstart `

        void AttachReaderWriter(ReaderWriter* r);

        void RollDice(); // set both dice to random integer 1 to 6

        status_codes TryStart(int sr, int sc) const;
        status_codes TryMove(int sr, int sc, int er, int ec); // `

        void ClearBoard(); // quit game

        void UndoMove(); // FIXME: add the functionality in Controller
            // as it is now, undoes entire turn so far. Once a turn is over it is over strictly.
        
        const std::array<std::array<int, COL>, ROW>& GetBoardRef() const;

        int GetPlayerSign() const;

        unsigned GetDistance(bool sr, int sc, bool er, int ec) const;

        void SetDice(int d1, int d2); // TESTING ONLY, DELETE ME LATER `

    private:
        std::array<std::array<int, COL>, ROW> board;
        // first row of board is reversed in view, but stored this way for convenience/efficiency
        int player_sign;

        std::mt19937 rng;                           // Mersenne Twister engine
        std::uniform_int_distribution<int> dist;    // uniform distribution for dice
        int dice[2] = {0, 0};
        int dice_used[2] = {0, 0};
        bool doubles_rolled;

        bool UseDice(bool idx);

        NardiCoord CalculateFinalCoords(bool sr, int sc, bool dice_idx) const;

        class Arbiter
        {
            public:
                Arbiter(Game* gp);
                status_codes ValidStart(int sr, int sc) const; // check that start is in bounds and occupied by player's piece
                status_codes LegalMove(int sr, int sc, int er, int ec) const;

            private:
                Game* g;

                status_codes LegalMove_2step(bool sr, int sc) const;
                bool BadRowChange(bool er) const;
                status_codes WellDefinedMove(int sr, int sc, int er, int ec) const; // check that move start and end are not against the rules
        };

        struct Move{
            Move(bool sr, int sc, bool er, int ec, int d1, int d2);

            bool sr;
            int sc;
            bool er;
            int ec;
            int dice1;
            int dice2;
        };

        Arbiter arbiter;
        ReaderWriter* rw;
        std::stack<Move> move_history;
        
        void MakeMove(bool sr, int sc, bool er, int ec);
        bool MoveOver() const;
};

class ReaderWriter
{
    public:
        ReaderWriter(const Game& game);
        
        void ReadInput();

        virtual void ReAnimate() const = 0;         // show the current state of the game
        virtual void AnimateDice(int d1, int d2) const = 0;
        virtual bool ReadQuitOrProceed() const = 0;                // read in quit or continue from user before every dice roll
        virtual NardiCoord ReportSelectedSlot() const = 0;    // Return coordinates of slot user selects, either dest or source
        virtual void InstructionMessage(std::string m) const = 0;      // In some implementations may even do nothing
        virtual void ErrorMessage(std::string m) const = 0;
    
        protected:
        const Game& g;
        const std::array<std::array<int, COL>, ROW> board;

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
void Game::AttachReaderWriter(ReaderWriter* r)
{
    rw = r;
}