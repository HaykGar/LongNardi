#pragma once

#include "NardiGame.h"

// Can add a print status function that switches on status and prints appropriate function, could be good for debugging 
        // not necessary
class Controller
{
    public:
        Controller(Game& game);
        virtual ~Controller();
        
        virtual status_codes ReceiveCommand(const Command& cmd);
        bool QuitRequested() const;

        friend class TestBuilder;
    private:
        Game& g;
        NardiCoord start;
        bool start_selected;
        bool dice_rolled;
        bool quit_requested;

        void SwitchTurns();
};


inline
bool Controller::QuitRequested() const
{
    return quit_requested;
}