#pragma once

#include "../NardiGame.h"
#include "../Controller.h"
#include "TestGlobals.h"

#include <gtest/gtest.h>

using namespace TestGlobals;

class TestBuilder : public testing::Test 
{
    protected:
        TestBuilder(); 

        // flags and setup
        void withFirstTurn();

        // setting internals explicitly
        status_codes StartOfTurn(bool p_idx, const std::array<std::array<int, COL>, ROW>& b, int d1, int d2);
        status_codes withDice(int d1, int d2);

        status_codes ReceiveCommand(const Command& c);

        void PrintBoard() const;

        const int& GetBoardAt(const NardiCoord& coord) const;
        const int& GetBoardAt(int r, int c) const;

        const std::array<std::array<int, COL>, ROW>& GetBoard() const;

        void StatusReport() const;
        
    private:
        std::unique_ptr<Game> _game;
        std::unique_ptr<Controller> _ctrl;

        void StartPreRoll(bool p_idx, const std::array<std::array<int, COL>, ROW>& b );

        void withPlayer(bool p_idx);
        void withBoard(const std::array<std::array<int, COL>, ROW>& b);
        void ResetControllerState();
};

inline 
const int& TestBuilder::GetBoardAt(const NardiCoord& coord) const
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
const std::array<std::array<int, COL>, ROW>& TestBuilder::GetBoard() const
{
    return _game->board._realBoard.data;
}