#pragma once

#include "ScenarioBuilder.h"

#include <gtest/gtest.h>

using namespace TestGlobals;

namespace Nardi
{

class TestBuilder : public testing::Test 
{
    protected:
        TestBuilder(); 

        // flags and setup
        void withFirstTurn();

        // setting internals explicitly
        status_codes StartOfTurn(bool p_idx, const std::array<std::array<int, COLS>, ROWS>& b, int d1, int d2);
        status_codes withDice(int d1, int d2);

        status_codes ReceiveCommand(const Command& c);

        void PrintBoard() const;

        const int& GetBoardAt(const Coord& coord) const;
        const int& GetBoardAt(int r, int c) const;

        const boardConfig GetBoard() const;

        void StatusReport() const;

        const Game& GetGame() const;
        
    private:
        ScenarioBuilder _bldr;
        Game& _game;
        Controller& _ctrl;
};

inline 
const int& TestBuilder::GetBoardAt(const Coord& coord) const
{
    return _bldr.GetBoardAt(coord);
}

inline 
const int& TestBuilder::GetBoardAt(int r, int c) const
{
    auto& brd = _game.GetBoardRef();
    return brd.at(r, c);
}

inline
const boardConfig TestBuilder::GetBoard() const
{
    return _bldr.GetBoard();
}

inline 
const Game& TestBuilder::GetGame() const
{ return _game; }

}   // namespace Nardi