#pragma once

#include "Game.h"
#include "Controller.h"

namespace Nardi
{

class ReaderWriter
{
    public:
        ReaderWriter(const Game& game, Controller& c) : g(game), ctrl(c) {}
        virtual ~ReaderWriter() {}

        virtual void AwaitUserCommand() = 0;

        virtual void ReAnimate() const = 0;         // show the current state of the game
        virtual void AnimateDice() const = 0; // get rid of this maybe, or make it reanimate wrapper in most cases?

        virtual void InstructionMessage(std::string m) const = 0;      // In some implementations may even do nothing
        virtual void ErrorMessage(std::string m) const = 0;
        virtual void DispErrorCode(status_codes code) const;

    protected:
        const Game& g;
        Controller& ctrl;

        virtual Command Input_to_Command() const = 0;
};

}   // namespace Nardi