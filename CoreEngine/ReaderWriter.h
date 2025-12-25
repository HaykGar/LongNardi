#pragma once

#include "Game.h"
#include "Controller.h"

namespace Nardi
{

class ReaderWriter
{
    public:
        ReaderWriter(Game& game, Controller& c) : g(game), ctrl(c) {}
        virtual ~ReaderWriter() = default;

        virtual void Initialize() {}

        // virtual void Render() const = 0;

        virtual status_codes PollInput() = 0;

        virtual void OnGameEvent(const GameEvent& event) = 0;

        virtual void Render() const = 0;         // show the current state of the game

        virtual void InstructionMessage(std::string m) const = 0;      // In some implementations may even do nothing
        virtual void ErrorMessage(std::string m) const = 0;
        virtual void DispErrorCode(status_codes code) const;

    protected:
        Game& g;
        Controller& ctrl;

        virtual Command Input_to_Command() const = 0;
};

}   // namespace Nardi