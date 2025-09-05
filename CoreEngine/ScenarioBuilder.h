#pragma once

#include "Game.h"
#include "Controller.h"
#include "Auxilaries.h"

namespace Nardi
{

class ScenarioBuilder
{
    public:
        ScenarioBuilder(); 

        // flags and setup
        void withFirstTurn();

        // setting internals explicitly
        status_codes withScenario(bool p_idx, const BoardConfig& b, int d1, int d2, int d1u, int d2u);
        status_codes withDice(int d1, int d2, int d1_used, int d2_used);
        status_codes withDice(int d1, int d2);

        // Actions
        status_codes ReceiveCommand(const Command& c);
        void Reset();

        void PrintBoard() const;

        const int GetBoardAt(const Coord& coord) const;
        const int GetBoardAt(int r, int c) const;

        const BoardConfig& GetBoard() const;

        void StatusReport() const;

        Game& GetGame();
        const Game& GetGame() const;

        Controller& GetCtrl();
        const Controller& GetCtrl() const;

    private:
        Game _game;
        Controller _ctrl;

        void StartPreRoll(bool p_idx, const BoardConfig& b );

        void withPlayer(bool p_idx);
        void withBoard(const BoardConfig& b);
        void ResetControllerState();
};

}   // namespace Nardi