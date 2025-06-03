#include "NardiGame.h"


Game::Game(int rseed) : player_sign(1),  seed(rseed),  arbiter(this) 
{
    std::srand(seed);
    board = {  { {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15}, {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }  };
} 
// player sign will actually have it be random 50/50, need to handle dice and other objects

void Game::RollDice() // FIXME handle doubles
{
    dice[0] = (rand() % 6) + 1;
    dice[1] = (rand() % 6) + 1;
}

void Game::UseDice(int idx)
{
    dice[idx] = 0;
}

bool Game::MoveOver() const
{
    return (dice[0] + dice[1] == 0); // may get more complicated for doubles
}

std::array<int, 2> Game::CalculateFinalCoords(int sr, int sc, int d) const
{

    int dir = 2*sr - 1; // to keep clockwise movement
    int j = sc + d*dir; 
    if (j < 12 && j >= 0)
        return {sr, j};
    else if (j >= 12){
        j = 12 - (j - 11);
        return {(sr - 1), j};
    }
    else
    {
        j = -j - 1;
        return {(sr + 1), j};
    }
}

void Game::MakeMove(int start_row, int start_col, int end_row, int end_col)
{
    board[start_row][start_col] -= player_sign;
    board[end_row][end_col] += player_sign;
}

void Game::PlayGame() // undo feature, especially for start coord
{
    if(!rw)
    {
        std::cerr << "No reader/writer attached, terminating game.\n";
        ClearBoard();
        return;
    }

    std::array<int, 2> start;
    std::array<int, 2> end;

    rw->ReAnimate();

    while(true)
    {
        rw->InstructionMessage("Press q to quit, anything else to roll the dice\n");
        bool manual_quit = rw->ReadQuitOrProceed();
        if(manual_quit)
        {
            rw->ErrorMessage("Quitting game\n");
            ClearBoard();
            return;
        }
        
        RollDice();
        rw->AnimateDice(dice[0], dice[1]);

        while(!MoveOver())
        {
            do
            {
                rw->InstructionMessage("Start slot:");
                start = rw->ReportSelectedSlot();   // FIXME - crashes when a non-number is entered
            } while (!arbiter.ValidStart(start[0], start[1]));
    
            do
            {
                rw->InstructionMessage("End slot:");
                end = rw->ReportSelectedSlot();
            } while (!arbiter.LegalMove(start[0], start[1], end[0], end[1]));
    
            MakeMove(start[0], start[1], end[0], end[1]);
            rw->ReAnimate();
        }

        player_sign = - player_sign;
        std::cin.ignore(10000, '\n');
    }

}

void Game::ClearBoard()
{
    board = {  { {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15}, {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }  };
}

/////////////////////////
/////   Arbiter ////////
///////////////////////

Game::Arbiter::Arbiter(Game* gp) : g(gp) {}

bool Game::Arbiter::ValidStart(int start_row, int start_col) const
{
    if(start_row < 0 || start_row > 1 || start_col < 0 || start_col > 11)
    {
        g->rw->ErrorMessage("Out of bounds, please enter valid coordinates\n");
        return false;
    }    
    else if (g->player_sign * g->board[start_row][start_col] <= 0)   // start slot empty or occupied by opponent
    {
        g->rw->ErrorMessage("Cannot move opponent's pieces or empty slots \n");
        return false;
    }
    else
        return true;
}

bool Game::Arbiter::LegalMove(int start_row, int start_col, int end_row, int end_col) const
{
    if(end_row < 0 || end_row > 1 || end_col < 0 || end_col > 11)
    {
        g->rw->ErrorMessage("Out of bounds, please enter valid coordinates\n");
        return false;
    }
    else if (g->player_sign * g->board[end_row][end_col] < 0)   // destination occupied by opponent
    {
        g->rw->ErrorMessage("Cannot move onto enemy pieces\n");
        return false;
    }
    else if(start_row == end_row && start_row == 0 && start_col <= end_col)
    {
        g->rw->ErrorMessage("No backwards moves, please enter valid coordinates\n ");
        return false;
    }
    else if(start_row == end_row && start_row == 1 && start_col >= end_col)
    {
        g->rw->ErrorMessage("No backwards moves, please enter valid coordinates\n ");
        return false;
    }
    else if (BadRowChange(start_row, end_row)){
        g->rw->ErrorMessage("End of board reached, please enter valid coordinates \n");
        return false;
    }
    else
    {
        int col_d = abs(end_col - start_col);
        int d;
        if(start_row == end_row)
            d = col_d;
        else if (start_row == 0)
            d = start_col + end_col + 1;
        else // start_row == 1
            d = (11 - start_col) + (12 - end_col);
        
        if(d == g->dice[0])
        {
            g->UseDice(0);
            return true;
        }
        else if (d == g->dice[1])
        {
            g->UseDice(1);
            return true;
        }
        else if (d == ( g->dice[0] + g->dice[1]))   // later account for doubles
        {
            return LegalMove_2d(start_row, start_col, g->dice[0],  g->dice[1]);
        }
        else
        {
            g->rw->ErrorMessage("Invalid end, cannot reach this slot with current dice roll\n");
            return false; // impossible to move there with current dice roll
        }
    }
}

bool Game::Arbiter::LegalMove_2d(int sr, int sc, int d1, int d2) const
{
    auto dest1 = g->CalculateFinalCoords(sr, sc, d1);

    if(LegalMove(sr, sc, dest1[0], dest1[1])) // can move by first d
    {
        auto dest2 = g->CalculateFinalCoords(dest1[0], dest1[1], d2);

        if(LegalMove(dest1[0], dest1[1], dest2[0], dest2[1]))   // can move from resulting spot by second d
        {
            g->UseDice(0);
            g->UseDice(1);
            return true;
        }
    }

    auto dest2 = g->CalculateFinalCoords(sr, sc, d2);

    if(LegalMove(sr, sc, dest2[0], dest2[1])) // can move second amount first
    {
        auto dest3 = g->CalculateFinalCoords(dest2[0], dest2[1], d1);

        return LegalMove(dest2[0], dest2[1], dest3[0], dest3[1]); // last available way to reach final destination
    }
    
    g->rw->ErrorMessage("Path obstructed by enemy pieces, unable to move to this end coordinate \n");
    return false;
}

bool Game::Arbiter::BadRowChange(int sr, int er) const
{
    if(sr == er)
        return false;
    else
        return !(er == sr + g->player_sign); // only ok when white goes from row 0 to 1 or black from 1 to 0
}



///////////////////////////////
/////  Reader/Writer  ////////
/////////////////////////////

ReaderWriter::ReaderWriter(const Game& game) : g(game) {}