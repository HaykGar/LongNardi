#pragma once

#include "Game.h"
#include "Controller.h"
#include "Auxilaries.h"
#include "ReaderWriter.h"
#include <sstream>

namespace Nardi
{

class ScenarioBuilder
{
    public:
        ScenarioBuilder(); 
        ScenarioBuilder(const ScenarioBuilder& other);

        // flags and setup
        void withFirstTurn();

        // setting internals explicitly
        status_codes withScenario(bool p_idx, const BoardConfig& b, int d1, int d2, int d1u=0, int d2u=0);
        status_codes withDice(int d1, int d2, int d1_used = 0, int d2_used = 0);

        // more general scenarios
        void withRandomEndgame(bool p_idx = false);

        // Actions
        status_codes ReceiveCommand(const Command& c);
        void Reset();

        void AttachNewRW(const IRWFactory& f);

        void DetachRW();

        void PrintBoard() const;

        const int GetBoardAt(const Coord& coord) const;
        const int GetBoardAt(int r, int c) const;

        const BoardConfig& GetBoard() const;

        void StatusReport() const;
        std::string StatusString() const;

        Game& GetGame();
        const Game& GetGame() const;

        Controller& GetCtrl();
        const Controller& GetCtrl() const;

        ReaderWriter* GetView();
        const ReaderWriter* GetView() const;

        void Render();

    private:
        Game _game;
        Controller _ctrl;
        std::shared_ptr<ReaderWriter> _view;

        void StartPreRoll(bool p_idx, const BoardConfig& b );

        void withPlayer(bool p_idx);
        void withBoard(const BoardConfig& b);
        void ResetControllerState();
};

}   // namespace Nardi