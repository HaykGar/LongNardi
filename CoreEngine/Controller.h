#pragma once

#include "Game.h"

namespace Nardi
{

// Can add a print status function that switches on status and prints appropriate function, could be good for debugging 
        // not necessary
class Controller
{
    public:
        Controller(Game& game);
        virtual ~Controller();
        
        virtual status_codes ReceiveCommand(const Command& cmd);
        virtual status_codes ReceiveCommand(Actions act);
        bool QuitRequested() const;

        void SwitchTurns();
        void OnTurnSwitch();

        bool InSimMode() const;
        void ToSimMode();
        void EndSimMode();

        bool AdvanceSimTurn();

        friend class ScenarioBuilder;
    private:
        Game& g;
        Coord start;
        bool start_selected;
        bool dice_rolled;
        bool quit_requested;
        bool sim_mode;
};


inline
bool Controller::QuitRequested() const
{
    return quit_requested;
}

}   // namespace Nardi