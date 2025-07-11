#pragma once

#include "NardiGame.h"

// Can add a print status function that switches on status and prints appropriate function, could be good for debugging 
        // not necessary
class Controller
{
    public:
        Controller(Game& game);
        virtual Game::status_codes ReceiveCommand(Command& cmd);
        bool QuitRequested() const;

    protected:
        NardiCoord start;
        bool start_selected;
        bool dice_rolled;

        bool quit_requested;

        void SwitchTurns();

        Game& g;

};


inline
bool Controller::QuitRequested() const
{
    return quit_requested;
}