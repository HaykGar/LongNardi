#pragma once

#include "NardiGame.h"
#include <termios.h>
#include <unistd.h>

class TerminalRW : public ReaderWriter
{
    public:
        TerminalRW(const Game& g);
        virtual void ReAnimate() const;         // show the current state of the game
        virtual void AnimateDice(int d1, int d2) const;
        virtual bool ReadQuitOrProceed() const;                // read in quit or continue from user before every dice roll
        virtual NardiCoord ReportSelectedSlot() const;    // Return coordinates of slot user selects, either dest or source
        virtual void InstructionMessage(std::string m) const;
        virtual void ErrorMessage(std::string m) const;
        // game inherited

    private:
        char getch() const;
};