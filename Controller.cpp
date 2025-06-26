#include "Controller.h"

Controller::Controller(Game& game) : start(), start_selected(false), dice_rolled(false), quit_requested(false), g(game) {}

void Controller::SwitchTurns()
{
    g.SwitchPlayerSign();
    
    dice_rolled = false;
    start_selected = false;
}

void Controller::ReceiveCommand(Command cmd)
{
    switch (cmd.action)
    {
    case Actions::NO_OP:
        break;
    case Actions::QUIT:
        quit_requested = true;
        break;
    case Actions::ROLL_DICE:
        if(!dice_rolled)
        {
            g.RollDice();
            dice_rolled = true;
        }
        break;
    case Actions::SELECT_SLOT:
        if (!dice_rolled)   // no attempts to move before rolling
            break;
        
        else if(start_selected)
        {
            Game::status_codes move_status = g.TryFinishMove(start, cmd.to_select.value()); // this makes the move if possible, triggering redraws
            if(move_status == Game::status_codes::SUCCESS)
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
            Game::status_codes start_status = g.TryStart(cmd.to_select.value());

            if (start_status == Game::status_codes::SUCCESS)
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
}

