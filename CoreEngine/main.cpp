#include "NardiMVC.h"

#include "Game.h"
#include "Controller.h"
#include "TerminalRW.h"
#include "ScenarioBuilder.h"

#include <iostream>
#include <iterator>

using namespace Nardi;

int main()
{
    Game model;
    Controller ctrl(model);
    TerminalRW view(model, ctrl);
    model.AttachReaderWriter(&view);

    status_codes status;
    bool isComputerTurn = false;

    std::random_device rd;
    std::mt19937 gen(rd());

    view.ReAnimate();

    while(!ctrl.QuitRequested() && !model.GameIsOver())
    {
        status = ctrl.ReceiveCommand(Command(Actions::ROLL_DICE));

        // turn action func pointer = isComputerTurn ? comp : human...

        while(status != status_codes::NO_LEGAL_MOVES_LEFT)
        {
            if(isComputerTurn)
            {
                auto b2s = model.GetBoards2Seqs();
                if(b2s.size() > 0)
                {
                    std::uniform_int_distribution<size_t> dist(0, b2s.size() - 1);
                    size_t random_offset = dist(gen);
                    std::unordered_map<BoardKey, MoveSequence, BoardKeyHash>::iterator it = b2s.begin();
                    std::advance(it, random_offset);

                    status = ctrl.ReceiveCommand(Command(it->first));
                    DispErrorCode(status);
                }
                else
                {
                    status = status_codes::NO_LEGAL_MOVES_LEFT;
                    ctrl.SwitchTurns();
                    std::cout << "no legal moves for computer\n";
                }
            }
            else    // human player
            {
                std::cout << "Awaiting command\n";
                status = view.AwaitUserCommand();
                std::cout << "received command with result ";
                DispErrorCode(status);
            }

            view.ReAnimate();
        }


        isComputerTurn = !isComputerTurn;
    }

    // ScenarioBuilder builder;

    // builder.withFirstTurn();
    // status_codes outcome = builder.withScenario(white, TestGlobals::start_brd, 4, 4, 0, 0);

    // builder.StatusReport();

    // builder.ReceiveCommand(Command(0, 0));
    // builder.ReceiveCommand(Command(0, 8));
    // builder.ReceiveCommand(Command(0, 0));
    // builder.ReceiveCommand(Command(first));

    // builder.StatusReport();

    // outcome = builder.ReceiveCommand(Command(0, 4));
    // outcome = builder.ReceiveCommand(Command(first));

    // DispErrorCode(outcome);

    // builder.StatusReport();

    // auto brd = builder.GetBoard();

    // DisplayBoard(brd);

    return 0;
}