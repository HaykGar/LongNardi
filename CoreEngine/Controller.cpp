#include "Controller.h"

using namespace Nardi;

Controller::Controller(Game& game) : g(game), start(), start_selected(false), dice_rolled(false), quit_requested(false), restart_requested(false), sim_mode(false) {}

Controller::~Controller()
{}

bool Controller::IsInSimMode() const
{ return sim_mode; }

void Controller::ToSimMode()
{ sim_mode = true; }

void Controller::EndSimMode()
{ sim_mode = false; }

bool Controller::AdvanceSimTurn()   // fixme - need more protections for when we call this eg turn is actually over ` `
{ 
    if(!sim_mode)
        return false;

    SwitchTurns();
    return true;
}

void Controller::SwitchTurns()
{
    g.SwitchPlayer();
    OnTurnSwitch();
}

void Controller::OnTurnSwitch()
{
    dice_rolled = false;
    start_selected = false;
}

status_codes Controller::ReceiveCommand(const Command& cmd)
{
    if(g.GameIsOver() && cmd.action != Actions::UNDO 
        && cmd.action != Actions::RESTART && cmd.action != Actions::QUIT)   // can undo terminal positions
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
    case Actions::RESTART:
        restart_requested = true;
        outcome = status_codes::NO_LEGAL_MOVES_LEFT;
        break;

    case Actions::UNDO:
        if(sim_mode)
        {
            outcome = g.UndoCurrentTurn();          // fixme undoing into no legal moves case. Specifically for chaining undo `
            if(outcome == status_codes::SUCCESS)
            {
                dice_rolled = true;
                start_selected = false;
            }
        }
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
        if(!g.TurnInProgress() && std::holds_alternative< std::array<int, 2> >(cmd.payload))
        {
            outcome = g.SimDice(std::get<std::array<int, 2>>(cmd.payload));
            if(outcome != status_codes::NO_LEGAL_MOVES_LEFT || sim_mode)
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
    case Actions::RANDOM_AUTOPLAY: {
        auto b2s = g.GetBoards2Seqs();
        if(b2s.size() > 0)
        {
            std::unordered_map<BoardConfig, MoveSequence, BoardConfigHash>::iterator it = b2s.begin();
            auto key = it->first;
            bool auto_played = g.AutoPlayTurn(key);
            outcome = auto_played ? status_codes::NO_LEGAL_MOVES_LEFT : status_codes::MISC_FAILURE;
            if(!auto_played)
            {
                std::cerr << "failed to auto-play with key\n";
                DisplayKey(std::get<BoardConfig>(cmd.payload));
            }
        }
    }
        break;
    case Actions::AUTOPLAY:
        if(std::holds_alternative<BoardConfig>(cmd.payload))
        {
            bool auto_played = g.AutoPlayTurn(std::get<BoardConfig>(cmd.payload));
            outcome = auto_played ? status_codes::NO_LEGAL_MOVES_LEFT : status_codes::MISC_FAILURE;
            if(!auto_played)
            {
                std::cerr << "failed to auto-play with key\n";
                DisplayKey(std::get<BoardConfig>(cmd.payload));
            }
        }
        break;
    default:
        std::cerr << "Unexpected command received\n";
        break;
    }

    if(outcome == status_codes::NO_LEGAL_MOVES_LEFT && !sim_mode)
        SwitchTurns();

    return outcome;
}

status_codes Controller::ReceiveCommand(Actions act)
{
    return ReceiveCommand(Command(act));
}