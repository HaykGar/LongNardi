#pragma once

#include "../Game.h"
#include "../Controller.h"
#include "TestGlobals.h"

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

        const std::array<std::array<int, COLS>, ROWS>& GetBoard() const;

        void StatusReport() const;
        
    private:
        std::unique_ptr<Game> _game;
        std::unique_ptr<Controller> _ctrl;

        void StartPreRoll(bool p_idx, const std::array<std::array<int, COLS>, ROWS>& b );

        void withPlayer(bool p_idx);
        void withBoard(const std::array<std::array<int, COLS>, ROWS>& b);
        void ResetControllerState();
};

inline 
const int& TestBuilder::GetBoardAt(const Coord& coord) const
{
    auto& brd = _game->GetBoardRef();
    return brd.at(coord);
}

inline 
const int& TestBuilder::GetBoardAt(int r, int c) const
{
    auto& brd = _game->GetBoardRef();
    return brd.at(r, c);
}

inline
const std::array<std::array<int, COLS>, ROWS>& TestBuilder::GetBoard() const
{
    return _game->board._realBoard.data;
}

}   // namespace Nardi