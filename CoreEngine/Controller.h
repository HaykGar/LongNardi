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
        virtual status_codes ReceiveCommand(const Actions& act);

        bool QuitRequested() const;
        bool RestartRequested() const;
        bool AwaitingRoll() const;
        bool StartIsSelected() const;
        const Coord& GetStart() const;

        void SwitchTurns();
        void OnTurnSwitch();

        bool IsInSimMode() const;
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
        bool restart_requested;
        bool sim_mode;
};


inline
bool Controller::QuitRequested() const
{
    return quit_requested;
}

inline
bool Controller::RestartRequested() const
{
    return restart_requested;
}

inline
bool Controller::AwaitingRoll() const
{ return !dice_rolled; }

inline bool Controller::StartIsSelected() const
{ return start_selected; }

inline 
const Coord& Controller::GetStart() const
{ return start; }

}   // namespace Nardi