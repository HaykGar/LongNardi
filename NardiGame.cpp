#include "NardiGame.h"

     int player_sign;

        std::mt19937 rng;                           // Mersenne Twister engine
        std::uniform_int_distribution<int> dist;    // uniform distribution for dice
        int dice[2] = {0, 0};
        int dice_used[2] = {0, 0};
        bool doubles_rolled;

Game::Game(int rseed) : player_sign(1), rng(rseed), dist(1, 6), doubles_rolled(false), arbiter(this) 
{
    board = {  { {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15}, {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }  };
} 
// player sign will actually have it be random 50/50, need to handle dice and other objects

void Game::RollDice()
{
    dice_used[0] = 0;
    dice_used[1] = 0;
    dice[0] = dist(rng);
    dice[1] = dist(rng);

    doubles_rolled = (dice[0] == dice[1]);
}

void Game::UseDice(bool idx, unsigned reps)    // pass bool to protect against out of bounds
{
    dice_used[idx] += dice[idx] * reps;
}

bool Game::MoveOver() const
{
    return (    (dice_used[0] + dice_used[1]) == ( (doubles_rolled + 1) * (dice[0] + dice[1]) )   );
    // since dice_used are only incremented by their corresponding dice, check if both values are used or in case of doubles if they're both used twice
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

    move_history.emplace(start_row, start_col, end_row, end_col, dice_used[0], dice_used[1]);
}

void Game::UndoMove()
{
    if(move_history.empty())
    {
        rw->ErrorMessage("No moves to undo");
    }
    else{
        Move& lastMove = move_history.top();

        board.at(lastMove.startRow).at(lastMove.startCol) += player_sign;
        board.at(lastMove.endRow).at(lastMove.endCol) -= player_sign;

        if(doubles_rolled)  // possibly dice used before this piece was moved, so we should not erase all of the progress on dice_used
        {
            unsigned d = GetDistance(lastMove.startRow, lastMove.startCol, lastMove.endRow, lastMove.endCol);
            dice_used[0] -= d;  // ok if negative, we only need the sum with dice_used[1] to match sum of (dice[0] + dice[1])*multiplier
        }
        else    // no doubles so only one dice used so far
        {
            dice_used[0] -= lastMove.dice1;
            dice_used[1] -= lastMove.dice2;
        }
        move_history.pop();
    }
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

        do 
        {
            do
            {
                rw->InstructionMessage("Start slot:");
                start = rw->ReportSelectedSlot();
            } while (!arbiter.ValidStart(start[0], start[1]));
    
            do
            {
                rw->InstructionMessage("End slot:");
                end = rw->ReportSelectedSlot();
            } while (!arbiter.LegalMove(start[0], start[1], end[0], end[1]));
    
            MakeMove(start[0], start[1], end[0], end[1]);
            rw->ReAnimate();
        }while(!MoveOver());

        player_sign = - player_sign;
        std::cin.ignore(10000, '\n');
    }

}

unsigned Game::GetDistance(int start_row, int start_col, int end_row, int end_col) const
{
    if(start_row == end_row)
        return abs(end_col - start_col);
    else if (start_row == 0)
        return start_col + end_col + 1;
    else // start_row == 1
        return (11 - start_col) + (12 - end_col);
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
        g->rw->ErrorMessage("Out of bounds or invalid input, please enter valid coordinates\n");
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

bool Game::Arbiter::WellDefinedMove(int start_row, int start_col, int end_row, int end_col) const
{
    if(end_row < 0 || end_row > 1 || end_col < 0 || end_col > 11)
    {
        g->rw->ErrorMessage("Out of bounds or invalid input, please enter valid coordinates\n");
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
        return true;
}

bool Game::Arbiter::LegalMove(int start_row, int start_col, int end_row, int end_col) const
{
    if(!WellDefinedMove(start_row, start_col, end_row, end_col)){
        return false;
    }
    else
    {
        unsigned d = g->GetDistance(start_row, start_col, end_row, end_col);

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
        else if(g->doubles_rolled)
        {
            for(int i = 2; i <= 4; ++i) // i=1 case eliminated above
            {
                if(d == i * g->dice[0])
                {
                    g->UseDice(0, i);
                    return true;
                }
            }
            return false;
        }
        else if (d == ( g->dice[0] + g->dice[1]))
        {
            return LegalMove_2step(start_row, start_col);
        }
        else
        {
            g->rw->ErrorMessage("Invalid end, cannot reach this slot with current dice roll\n");
            return false; // impossible to move there with current dice roll
        }
    }
}

bool Game::Arbiter::LegalMove_2step(int sr, int sc) const  
// if this is called, then the end square is not blocked by the enemy
{
    auto dest1 = g->CalculateFinalCoords(sr, sc, g->dice[0]);

    if(LegalMove(sr, sc, dest1[0], dest1[1])) // can move by first dice[0]
    {      
        g->UseDice(1);  // use second dice to get to the end
        return true;
    }

    auto dest2 = g->CalculateFinalCoords(sr, sc, g->dice[1]);

    if(LegalMove(sr, sc, dest2[0], dest2[1])) // can move by dice[1] first
    {
        g->UseDice(0);
        return true;
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

//////////////////////////
/////    Move    ////////
////////////////////////

Game::Move::Move(int sr, int sc, int er, int ec, int d1, int d2) : 
startRow(sr), startCol(sc), endRow(er), endCol(ec), dice1(d1), dice2(d2) {}


///////////////////////////////
/////  Reader/Writer  ////////
/////////////////////////////

ReaderWriter::ReaderWriter(const Game& game) : g(game) {}