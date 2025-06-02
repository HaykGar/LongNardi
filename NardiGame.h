#include <iostream>
#include <array>
#include <cstdlib>

/*
Will need to capture game states and later allow undo move feature

Check for only one piece from the "head" per turn (with the exceptions on move 1)

*/


class Game{

    public:
        Game(int seed = 1);
        
        void PlayGame();

        void DisplayBoard() const;  // will move to private later

        void UseDice(int idx); // should be private but I want arbiter to access it

    private:
        int board[2][12] = { {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15}, {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
        int player_sign;
        int dice[2];
        void RollDice(); // set both dice to random integer 1 to 6, important to force this to happen each turn.
        
        int seed;

        std::array<int, 2> CalculateFinalCoords(int sr, int sc, int d) const;

        class Arbiter
        {
            public:
                Arbiter(Game* gp);
                bool ValidStart(int sr, int sc) const; // check that start is in bounds and occupied by player's piece
                bool LegalMove(int start_row, int start_col, int end_row, int end_col) const;
                bool LegalMove_2d(int start_row, int start_col, int d1, int d2) const;
                bool BadRowChange(int sr, int er) const;

            private:
                Game* g;
        };

        Arbiter arbiter;
        
        void MakeMove(int start_row, int start_col, int end_row, int end_col);
        bool MoveOver() const;
};

