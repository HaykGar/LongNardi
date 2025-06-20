#include "NardiGame.h"

Game::Game(int rseed) : player_sign(1), rng(rseed), dist(1, 6), doubles_rolled(false), arbiter(this) 
{
    board = {  { {15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }  };
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

void Game::UseDice(bool idx, unsigned reps)    // pass bool to protect against out of bounds, reps 1 by default, only not for doubles
{
    dice_used[idx] += dice[idx] * reps;
}

bool Game::MoveOver() const
{
    return (    (dice_used[0] + dice_used[1]) == ( (doubles_rolled + 1) * (dice[0] + dice[1]) )   );
    // since dice_used are only incremented by their corresponding dice, check if both values are used or in case of doubles if they're both used twice
}

std::array<int, 2> Game::CalculateFinalCoords(bool sr, int sc, bool dice_idx) const
{
    int ec = sc + dice[dice_idx];
    if (ec < COL)
        return {sr, ec};
    else
        return { (!sr), ec - COL};
    // validity not inherently guaranteed by this computation
} 

void Game::MakeMove(bool sr, int sc, bool er, int ec)
{
    board[sr][sc] -= player_sign;
    board[er][ec] += player_sign;

    move_history.emplace(sr, sc, er, ec, dice_used[0], dice_used[1]);
}

void Game::UndoMove()
{
    if(move_history.empty())
    {
        rw->ErrorMessage("No moves to undo");
    }
    else{
        Move& lastMove = move_history.top();

        board.at(lastMove.sr).at(lastMove.sc) += player_sign;
        board.at(lastMove.er).at(lastMove.ec) -= player_sign;

        if(doubles_rolled)  // possibly dice used before this piece was moved, so we should not erase all of the progress on dice_used
        {
            unsigned d = GetDistance(lastMove.sr, lastMove.sc, lastMove.er, lastMove.ec);
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

                if(start[0] == 0)
                    start[1] = COL - start[1] - 1; // reverse top row, no consequence if invalid coords given
            } while (!arbiter.ValidStart(start[0], start[1]));
    
            do
            {
                rw->InstructionMessage("End slot:");
                end = rw->ReportSelectedSlot();

                if(end[0] == 0)
                    end[1] = COL - end[1] - 1;
            } while (!arbiter.LegalMove(start[0], start[1], end[0], end[1]));
    
            MakeMove(start[0], start[1], end[0], end[1]);
            rw->ReAnimate();
        }while(!MoveOver());

        player_sign = - player_sign;
        std::cin.ignore(10000, '\n');
    }

}

unsigned Game::GetDistance(bool sr, int sc, bool er, int ec) const
{

    if(sr == er)
        return ec - sc; // sc >= ec is invalid, so this will be positive if called on well-defined move
    else
        return COL - sc + ec;
}

void Game::ClearBoard()
{
    board = {  { {15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }  };
}

/////////////////////////
/////   Arbiter ////////
///////////////////////

Game::Arbiter::Arbiter(Game* gp) : g(gp) {}

bool Game::Arbiter::ValidStart(int sr, int sc) const
{
    if(sr < 0 || sr > ROW - 1 || sc < 0 || sc > COL - 1)
    {
        g->rw->ErrorMessage("Out of bounds or invalid input, please enter valid coordinates\n");
        return false;
    }    
    else if (g->player_sign * g->board[sr][sc] <= 0)   // start slot empty or occupied by opponent
    {
        g->rw->ErrorMessage("Cannot move opponent's pieces or empty slots \n");
        return false;
    }
    else
        return true;
}

bool Game::Arbiter::WellDefinedMove(int sr, int sc, int er, int ec) const // start already checked to be valid
{
    if(er < 0 || er > ROW - 1 || ec < 0 || ec > COL - 1)
    {
        g->rw->ErrorMessage("Out of bounds or invalid input, please enter valid coordinates\n");
        return false;
    }
    else if (g->player_sign * g->board[er][ec] < 0)   // destination occupied by opponent
    {
        g->rw->ErrorMessage("Cannot move onto enemy pieces\n");
        return false;
    }
    else if(sr == er )
    {
        if (sc == ec)
            return false; // FIXME unselect start in this case
        else if (sc > ec)
        {
            g->rw->ErrorMessage("No backwards moves, please enter valid coordinates\n ");
            return false;
        }

        return true;
    }
    else if (BadRowChange(er)) // sr != er
    {
        g->rw->ErrorMessage("End of board reached, please enter valid coordinates \n");
        return false;
    }
    
    return true;
}

bool Game::Arbiter::LegalMove(int sr, int sc, int er, int ec) const
{
    if(!WellDefinedMove(sr, sc, er, ec)){
        return false;
    }
    else
    {
        unsigned d = g->GetDistance(sr, sc, er, ec); // bool casting here is ok since move well defined

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
            return LegalMove_2step(sr, sc);
        }
        else
        {
            g->rw->ErrorMessage("Invalid end, cannot reach this slot with current dice roll\n");
            return false; // impossible to move there with current dice roll
        }
    }
}

bool Game::Arbiter::LegalMove_2step(bool sr, int sc) const  
// if this is called, then the end square is not blocked by the enemy
{
    auto dest1 = g->CalculateFinalCoords(sr, sc, 0);

    if(LegalMove(sr, sc, dest1[0], dest1[1])) // can move by first dice[0]
    {      
        g->UseDice(1);  // use second dice to get to the end, dice1 used in LegalMove call
        return true;
    }

    auto dest2 = g->CalculateFinalCoords(sr, sc, 1);

    if(LegalMove(sr, sc, dest2[0], dest2[1])) // can move by dice[1] first, dice0 used in LegalMove call
    {
        g->UseDice(0);
        return true;
    }
    
    g->rw->ErrorMessage("Path obstructed by enemy pieces, unable to move to this end coordinate \n");
    return false;
}

bool Game::Arbiter::BadRowChange(bool er) const // guaranteed sr != er
{
    int r = g->player_sign + er; // white to row 1 (r==2) or black to row 0 (r==-1) only acceptable choices, else r==1 or 0
    return (r == 0 || r == 1);
}

//////////////////////////
/////    Move    ////////
////////////////////////

Game::Move::Move(bool start_r, int start_c, bool end_r, int end_c, int d1, int d2) : 
sr(start_r), sc(start_c), er(end_r), ec(end_c), dice1(d1), dice2(d2) {}


///////////////////////////////
/////  Reader/Writer  ////////
/////////////////////////////

ReaderWriter::ReaderWriter(const Game& game) : g(game) {}