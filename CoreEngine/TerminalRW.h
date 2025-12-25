#pragma once

#include "ReaderWriter.h"

#include <iostream>
#include <string>
#include <vector>
#include <sstream> // For std::istringstream
#include <iterator> // For std::istream_iterator
#include <algorithm> // For std::copy

#include <memory>   // for std::unique_ptr factory

using namespace Nardi;

class TerminalRW : public ReaderWriter
{
    public:
        TerminalRW(Game& g, Controller& c);
        
        virtual status_codes PollInput() override;

        virtual void OnGameEvent(const GameEvent& e) override;
        
        virtual void Render() const override;         // show the current state of the game

        virtual void InstructionMessage(std::string m) const override;
        virtual void ErrorMessage(std::string m) const override;
        // game, controller inherited

    protected:
        std::string input;
        void AnimateDice() const;
        virtual Command Input_to_Command() const override;
        std::vector<std::string> splitStringByWhitespace(const std::string& str) const;
        bool isNumeric(std::string s) const;
};

std::unique_ptr<ReaderWriter> TerminalRWFactory(Game& g, Controller& c);    // factory declaration