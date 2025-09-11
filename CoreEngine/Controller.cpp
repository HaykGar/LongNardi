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
    if(g.GameIsOver())
        return status_codes::NO_LEGAL_MOVES_LEFT; 

    status_codes outcome = status_codes::MISC_FAILURE;
    switch (cmd.action)
    {
    case Actions::NO_OP:    // should not happen
        break;
    case Actions::QUIT:
        quit_requested = true;
        outcome = status_codes::NO_LEGAL_MOVES_LEFT;
        break;
    case Actions::ROLL_DICE:
        if(!dice_rolled)    // treat converse as misc failure
        {
            outcome = g.RollDice();
            if(outcome != status_codes::NO_LEGAL_MOVES_LEFT)
                dice_rolled = true;
        }
        break;
    case Actions::SET_DICE:
        if(!dice_rolled && std::holds_alternative< std::array<int, 2> >(cmd.payload))
        {
            auto dice_to = std::get<std::array<int, 2>>(cmd.payload);
            outcome = g.SimDice(dice_to);
            if(outcome != status_codes::NO_LEGAL_MOVES_LEFT)
                dice_rolled = true;
        }
        break;
    case Actions::SELECT_SLOT:
    case Actions::MOVE_BY_DICE:
        if (!dice_rolled)   // no attempts to move before rolling
            break;
        
        else if(start_selected)
        {
            if(std::holds_alternative<Coord>(cmd.payload))
                outcome = g.TryFinishMove(start, std::get<Coord>(cmd.payload) ); // this makes the move if possible, triggering redraws
            else if (std::holds_alternative<bool>(cmd.payload)){
                outcome = g.TryFinishMove(start, std::get<bool>(cmd.payload));
            }

            if(outcome == status_codes::SUCCESS)  // turn not over
                start_selected = false;
            else
                start_selected = false; // unselect start if illegal move attempted or no legal moves left
        }
        else if(std::holds_alternative<Coord>(cmd.payload))
        {
            outcome = g.TryStart(std::get<Coord>(cmd.payload));
            if (outcome == status_codes::SUCCESS)
            {
                start = std::get<Coord>(cmd.payload);
                start_selected = true;
            }
        }
        break;
    case Actions::AUTOPLAY:
        if(std::holds_alternative<BoardKey>(cmd.payload))
        {
            bool auto_played = g.AutoPlayTurn(std::get<BoardKey>(cmd.payload));
            outcome = auto_played ? status_codes::NO_LEGAL_MOVES_LEFT : status_codes::MISC_FAILURE;
        }
        break;
    default:
        std::cerr << "Unexpected command received\n";
        break;
    }

    if(outcome == status_codes::NO_LEGAL_MOVES_LEFT)
        SwitchTurns();

    return outcome;
}