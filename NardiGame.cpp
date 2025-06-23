#include "NardiGame.h"

Game::Game(int rseed) : player_sign(1), rng(rseed), dist(1, 6), doubles_rolled(false), arbiter(this) 
{
    board = {  { {15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }  };
} 
// player sign will actually have it be random 50/50, need to handle dice and other objects

void Game::RollDice() // important to force this only once per turn in controller, no explicit safeguard here `
{
    dice_used[0] = 0;
    dice_used[1] = 0;
    dice[0] = dist(rng);
    dice[1] = dist(rng);

    doubles_rolled = (dice[0] == dice[1]);
}

void Game::SetDice(int d1, int d2)
{
    dice_used[0] = 0;
    dice_used[1] = 0;
    dice[0] = d1;
    dice[1] = d2;

    doubles_rolled = (dice[0] == dice[1]);
}

Game::status_codes Game::TryStart(int sr, int sc) const
{
    return arbiter.ValidStart(sr, sc);
}

Game::status_codes Game::TryMove(int sr, int sc, int er, int ec)
{
    status_codes start = TryStart(sr, sc);  // redundant ?
    if(start != SUCCESS)
        return start;
    
    status_codes can_move = arbiter.LegalMove(sr, sc, er, ec);
    if (can_move != SUCCESS)
        return can_move;
    else
    {
        MakeMove(sr, sc, er, ec);
        rw->ReAnimate();
        return SUCCESS;
    }
}

bool Game::UseDice(bool idx)
{
    // dice has been used, in case of doubles check if it's been used all 4 times
    if(dice_used[idx] > 0 && (!doubles_rolled || (dice_used[idx] + dice_used[!idx] >= 4 * dice[0])) )
        return false;

    dice_used[idx] += dice[idx];
    return true;
}

bool Game::MoveOver() const
{
    return (    (dice_used[0] + dice_used[1]) == ( (doubles_rolled + 1) * (dice[0] + dice[1]) )   );
    // since dice_used are only incremented by their corresponding dice, check if both values are used or in case of doubles if they're both used twice
}

NardiCoord Game::CalculateFinalCoords(bool sr, int sc, bool dice_idx) const
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

void Game::UndoMove()   // strict: no undoing after enemy dice roll. Watch for case after turn over, before enemy rolls or make even stricter
{
    if(move_history.empty()) // no-op ? `
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

    NardiCoord start;
    NardiCoord end;

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

                if(start.row == 0)
                    start.col = COL - start.col - 1; // reverse top row, no consequence if invalid coords given
            } while (arbiter.ValidStart(start.row, start.col) != SUCCESS);
    
            do
            {
                rw->InstructionMessage("End slot:");
                end = rw->ReportSelectedSlot();

                if(end.row == 0)
                    end.col = COL - end.col - 1;
            } while (arbiter.LegalMove(start.row, start.col, end.row, end.col) != SUCCESS);
    
            MakeMove(start.row, start.col, end.row, end.col);
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

Game::status_codes Game::Arbiter::ValidStart(int sr, int sc) const
{
    if(sr < 0 || sr > ROW - 1 || sc < 0 || sc > COL - 1)
        return OUT_OF_BOUNDS;

    else if (g->player_sign * g->board[sr][sc] <= 0)
        return START_EMPTY_OR_ENEMY;

    else
        return SUCCESS;
}

Game::status_codes Game::Arbiter::WellDefinedMove(int sr, int sc, int er, int ec) const // start already checked to be valid
{
    if(er < 0 || er > ROW - 1 || ec < 0 || ec > COL - 1)
        return OUT_OF_BOUNDS;
    
    else if (g->player_sign * g->board[er][ec] < 0)   // destination occupied by opponent
        return DEST_ENEMY;
    
    else if(sr == er )
    {
        if (sc == ec)
            return START_RESELECT; // FIXME unselect start in this case - controller can check `
        else if (sc > ec)
            return BACKWARDS_MOVE;

        return SUCCESS; // prevent unnecessary RowChangeCheck
    }

    else if (BadRowChange(er)) // sr != er
        return BOARD_END_REACHED;
    
    return SUCCESS;
}

Game::status_codes Game::Arbiter::LegalMove(int sr, int sc, int er, int ec) const
{
    status_codes well_def = WellDefinedMove(sr, sc, er, ec);
    if(well_def != SUCCESS)
        return well_def;
    
    unsigned d = g->GetDistance(sr, sc, er, ec); // bool casting here is ok since move well defined

    if(d == g->dice[0])
    {
        if(g->UseDice(0))
            return SUCCESS;
       
        return NO_PATH_TO_DEST; // dice already used up

    }
    else if (d == g->dice[1])
    {
        if(g->UseDice(1))
            return SUCCESS;
        
        return NO_PATH_TO_DEST; // dice already used up

    }
    else    // can't reach with just one dice
    {
        bool both_dice_works = (d == ( g->dice[0] + g->dice[1]));   // dice one and two together make the distance

        if(both_dice_works || (g->doubles_rolled && d % g->dice[0] == 0 && d / g->dice[0] <= 4) )  // both dice enough, or doubles with d a multiple of dice[0] <= 4
        {
            std::pair<int, int> dice_used_copy(g->dice_used[0], g->dice_used[1]);
            status_codes step2x = LegalMove_2step(sr, sc);

            if(both_dice_works)
                return step2x;

            else    // doubles with either 3 or 4 reps, case 2 just covered, case 1 covered earlier d == g->dice[0]
            {
                if(step2x != SUCCESS)
                    return step2x;
                
                NardiCoord midpoint = g->CalculateFinalCoords(sr, sc, 0);
                midpoint = g->CalculateFinalCoords(midpoint.row, midpoint.col, 1); // take both steps, calculate coordinates of resulting "midpoint"
                
                status_codes final_step;
                if(d == 3 * g->dice[0])
                    final_step = LegalMove(midpoint.row, midpoint.col, er, ec);
                
                else if (d == 4 * g->dice[0])   // same as else since all other cases previously covered
                    final_step = LegalMove_2step(midpoint.row, midpoint.col); 

                if(final_step != SUCCESS)
                {
                    g->dice_used[0] = dice_used_copy.first;
                    g->dice_used[1] = dice_used_copy.second; // undo effects of legalmove_2step if we can't do the full move
                }

                return final_step;
            }
        }
        else
            return NO_PATH_TO_DEST;
    }
}

Game::status_codes Game::Arbiter::LegalMove_2step(bool sr, int sc) const  
{
    std::pair<int, int> dice_used_copy(g->dice_used[0], g->dice_used[1]);

    NardiCoord dest1 = g->CalculateFinalCoords(sr, sc, 0);

    if(g->doubles_rolled) // extra check needed in this case, otherwise we are guaranteed that the endpoint is well defined already
    {    
        NardiCoord final_dest = g->CalculateFinalCoords(dest1.row, dest1.col, 1);

        status_codes well_def = WellDefinedMove(sr, sc, final_dest.row, final_dest.col);
        if(well_def != SUCCESS)
            return well_def;
    }
    // can move by first dice[0], use second dice to get to the end
    if( (LegalMove(sr, sc, dest1.row, dest1.col) == SUCCESS) &&  g->UseDice(1)) // dice0 used in LegalMove call
        return SUCCESS;
    
    NardiCoord dest2 = g->CalculateFinalCoords(sr, sc, 1);

    if( (LegalMove(sr, sc, dest2.row, dest2.col) == SUCCESS) && g->UseDice(0) ) // can move by dice[1] first, dice1 used in LegalMove call
        return SUCCESS;
    
    g->dice_used[0] = dice_used_copy.first;
    g->dice_used[1] = dice_used_copy.second;

    return NO_PATH_TO_DEST;
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

ReaderWriter::ReaderWriter(const Game& game) : g(game), board(game.GetBoardRef()) {}