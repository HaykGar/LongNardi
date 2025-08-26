#include "Controller.h"

using namespace Nardi;

Controller::Controller(Game& game) : g(game), start(), start_selected(false), dice_rolled(false), quit_requested(false)  {}

Controller::~Controller()
{}

void Controller::SwitchTurns()
{
    g.SwitchPlayer();
   
    dice_rolled = false;
    start_selected = false;
}

status_codes Controller::ReceiveCommand(const Command& cmd)
{
    //std::cout << "received command: " << static_cast<int>(cmd.action) << "\n";
    //std::cout << "start selected? " << std::boolalpha << start_selected << "\n";
    //std::cout << "dice rolled? " << std::boolalpha << dice_rolled << "\n";
    //std::cout << "Game Over? "   << std::boolalpha << g.GameIsOver() << "\n";

    if(g.GameIsOver())  // redundant, cheap protection so no changes made after end.
        return status_codes::NO_LEGAL_MOVES_LEFT; 

    status_codes outcome = status_codes::MISC_FAILURE;
    switch (cmd.action)
    {
    case Actions::NO_OP:    // should not happen
        break;
    case Actions::QUIT:
        quit_requested = true;
        outcome = status_codes::SUCCESS;  // user requested quit is not a problem
        break;
    case Actions::ROLL_DICE:
        if(!dice_rolled)    // treat converse as misc failure
        {
            outcome = g.RollDice();
            if(outcome == status_codes::NO_LEGAL_MOVES_LEFT)
                SwitchTurns();
            else
                dice_rolled = true;
        }
        break;
    case Actions::SELECT_SLOT:
    case Actions::MOVE_BY_DICE:
        //std::cout << "trying to select or move\n";
        if (!dice_rolled)   // no attempts to move before rolling
        {
            break;
        }
        
        else if(start_selected)
        {
            //std::cout << "\n\nstart_selected\n\n";
            if(std::holds_alternative<Coord>(cmd.payload))
                outcome = g.TryFinishMove(start, std::get<Coord>(cmd.payload) ); // this makes the move if possible, triggering redraws
            else if (std::holds_alternative<bool>(cmd.payload)){
                //std::cout << "\n\nattempting movebydice\n\n";
                outcome = g.TryMoveByDice(start, std::get<bool>(cmd.payload));
            }

            if(outcome == status_codes::SUCCESS)  // turn not over
                start_selected = false;
            else if(outcome == status_codes::NO_LEGAL_MOVES_LEFT)
                SwitchTurns();
            else
                start_selected = false; // unselect start if illegal move attempted or start re-selected
        }
        else
        {
            //std::cout << "trying start select\n";
            if(!std::holds_alternative<Coord>(cmd.payload))
                break;  // attempted to move by dice without selecting start, or monostate payload
            
            outcome = g.TryStart(std::get<Coord>(cmd.payload));
            if (outcome == status_codes::SUCCESS)
            {
                start = std::get<Coord>(cmd.payload);
                start_selected = true;
            }
        }
        break;
    default:
        std::cerr << "Unexpected command received\n";
        break;
    }

    return outcome;
}
