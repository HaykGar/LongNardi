#pragma once
#include "../NardiGame.h"
#include "../Controller.h"

#include <gtest/gtest.h>

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
        
    private:
        Game _game;
        Controller _ctrl;

        void StartPreRoll(bool p_idx, const std::array<std::array<int, COL>, ROW>& b );

        void withPlayer(bool p_idx);
        void withBoard(const std::array<std::array<int, COL>, ROW>& b);
        void SetDerived();
        void CalcPiecesLeftandReached();
        void ConstructAvailabilitySets();
        void ResetControllerState();
};

inline 
const int& TestBuilder::GetBoardAt(const NardiCoord& coord) const
{
    return _game.board.at(coord);
}

inline 
const int& TestBuilder::GetBoardAt(int r, int c) const
{
    return _game.board.at(r, c);
}