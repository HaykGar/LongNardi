#include "NardiGame.h"


Game::Game() : player_sign(1), arbiter(this) {} 
// player sign will actually have it be random 50/50, need to handle dice and other objects

int Game::CalculateFinalCoords(int sr, int sc, int d) const
{

    int dir = 2*sr - 1; // to keep clockwise movement
    int j = d*dir; 
    if (j < 12 && j >= 0)
        return sr*12 + j;
    else if (j >= 12){
        j = 12 - (j - 11);
        return (sc - 1)*12 + j;
    }
    else{
        j = -j;
        return (sc + 1)*12 + j;
    }
}

bool Game::MakeMove(int start_row, int start_col, int end_row, int end_col)
{
    if( arbiter.LegalMove(start_row, start_col, end_row, end_col) )
    {
        board[start_row][start_col] -= player_sign;
        board[end_row][end_col] += player_sign;
        return true;
    }
    else{
        return false;
    }
    
}

void Game::DisplayBoard() const // animate this later with some graphic library
{
    if (player_sign > 0){
        for(int i = 1; i <= 12; ++i)
        {
            std::cout << board[1][12-i] << "\t";
        }

        std::cout << "\n\n";

        for(int i = 1; i <= 12; ++i)
        {
            std::cout << board[0][12 - i] << "\t";
        }

    }
    else{
        for(int i = 0; i < 12; ++i)
        {
            std::cout << board[0][i] << "\t";
        }
        std::cout << "\n\n";
        for(int i = 0; i < 12; ++i)
        {
            std::cout << board[1][i] << "\t";
        }
    }
}


/////////////////////////
/////   Arbiter ////////
///////////////////////

Game::Arbiter::Arbiter(Game* gp) : g(gp) {}

bool Game::Arbiter::LegalMove(int start_row, int start_col, int end_row, int end_col) const
{
    if(start_row < 0 || start_row > 1 || start_col < 0 || start_col > 11 || end_row < 0 || end_row > 1 || end_col < 0 || end_col > 11)
    {
        std::cout << "Please enter valid coordinates\n";
        return false;
    }
    else if(g->player_sign * g->board[start_row][start_col] <= 0)   // start slot empty or occupied by opponent
    {
        std::cout << "Cannot move opponent's pieces or empty slots \n";
        return false;
    }
    else if (g->player_sign * g->board[start_row][start_col] < 0)   // destination occupied by opponent
    {
        std::cout << "Cannot move onto enemy pieces\n";
        return false;
    }
    else if (BadRowChange(start_row, end_row)){
        return false;
    }
    else
    {
        int col_d = abs(end_col - start_col);
        int d;
        if(start_row != end_row)
            d = 12 - col_d;
        else
            d = col_d;
        
        if(d == g->dice[0] || d == g->dice[1])
            return true;
        else if (d == ( g->dice[0] + g->dice[1]))
        {
            return LegalMove_2d(start_row, start_col, g->dice[0],  g->dice[1]);
        }
        else
            return false; // impossible to move there with current dice roll
    }

    // Once the player reaches the far quadrant, pieces must not be allowed to move back around to the home quadrant. Need to pay attention to row change
} 

bool Game::Arbiter::LegalMove_2d(int sr, int sc, int d1, int d2) const
{
    int dest1 = g->CalculateFinalCoords(sr, sc, d1);
    int er1 = dest1 / 12;
    int ec1 = dest1 % 12;

    if(LegalMove(sr, sc, er1, ec1)) // can move by first d
    {
        int dest2 = g->CalculateFinalCoords(er1, ec1, d2);
        int er2 = dest2 / 12;
        int ec2 = dest2 % 12;

        if(LegalMove(er1, ec1, er2, ec2))   // can move from resulting spot by second d
            return true;
    }

    int dest2 = g->CalculateFinalCoords(sr, sc, d2);
    int er2 = dest2 / 12;
    int ec2 = dest2 % 12;
    if(LegalMove(sr, sc, er2, ec2)) // can move second amount first
    {
        int dest3 = g->CalculateFinalCoords(er2, ec2, d1);
        int er3 = dest3 / 12;
        int ec3 = dest3 % 12;

        return LegalMove(er2, ec2, er3, ec3); // can reach final destination
    }
    
}

bool Game::Arbiter::BadRowChange(int sr, int er) const
{
    if(sr == er)
        return false;
    else
        return !(er = sr + g->player_sign); // only ok when white goes from row 0 to 1 or black from 1 to 0
}