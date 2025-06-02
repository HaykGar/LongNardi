#include<iostream>

/*
Will need to capture game states and later allow undo move feature

Add game play function and write test cases


*/


class Game{

    public:
        Game();
        
        bool MakeMove(int start_row, int start_col, int end_row, int end_col);
        void DisplayBoard() const;

    private:
        int board[2][12] = {{-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15}};
        int player_sign;
        int dice[2];
        void RollDice(); // set both dice to random integer 1 to 6, important to force this to happen each turn

        int CalculateFinalCoords(int sr, int sc, int d) const;

        class Arbiter
        {
            public:
                Arbiter(Game* gp);
                bool LegalMove(int start_row, int start_col, int end_row, int end_col) const;
                bool LegalMove_2d(int start_row, int start_col, int d1, int d2) const;
                bool BadRowChange(int sr, int er) const;

            private:
                Game* g;
        };

        Arbiter arbiter;
};

