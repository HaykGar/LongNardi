#include "Controller.h"

Controller::Controller(Game& game) : start(), start_selected(false), dice_rolled(false), quit_requested(false), g(game) {}

void Controller::SwitchTurns()
{
    g.SwitchPlayer();
    
    dice_rolled = false;
    start_selected = false;
}

Game::status_codes Controller::ReceiveCommand(Command& cmd)
{
    Game::status_codes outcome = Game::status_codes::MISC_FAILURE;
    switch (cmd.action)
    {
    case Actions::NO_OP:    // should not happen
        break;
    case Actions::QUIT:
        quit_requested = true;
        outcome = Game::status_codes::SUCCESS;  // user requested quit is not a problem
        break;
    case Actions::ROLL_DICE:
        if(!dice_rolled)    // treat converse as misc failure
        {
            outcome = g.RollDice();
            if(outcome == Game::status_codes::NO_LEGAL_MOVES || (outcome == Game::status_codes::FORCED_MOVE_MADE && g.TurnOver()) )
                SwitchTurns();
            else
                dice_rolled = true;
        }
        break;
    case Actions::SELECT_SLOT:
        if (!dice_rolled)   // no attempts to move before rolling
            break;
        
        else if(start_selected)
        {
            outcome = g.TryFinishMove(start, cmd.to_select.value()); // this makes the move if possible, triggering redraws
            if(outcome == Game::status_codes::SUCCESS)
            {
                start_selected = false;
                if(g.TurnOver())
                    SwitchTurns();
            }
            else
                start_selected = false; // unselect start if illegal move attempted or start re-selected

            // ... respond to info, eg else print status (move_status)
        }
        else
        {
            outcome = g.TryStart(cmd.to_select.value());

            if (outcome == Game::status_codes::SUCCESS)
            {
                start = cmd.to_select.value();
                start_selected = true;
            }
            // else ... respond to info or maybe do nothing
        }
        break;
    default:
        std::cerr << "Unexpected command received\n";
        break;
    }

    return outcome;
}

