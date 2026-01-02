#include "Game.h"
#include "Controller.h"
#include "TerminalRW.h"
#include "SFMLRW.h"
#include "ScenarioBuilder.h"

#include <iostream>
#include <iterator>

using namespace Nardi;

void HumanVsRand()
{
    Game model;
    Controller ctrl(model);
    TerminalRW view(model, ctrl);
    model.AttachReaderWriter(&view);

    status_codes status;
    bool isComputerTurn = false;

    std::random_device rd;
    std::mt19937 gen(rd());

    view.Render();

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
                    std::unordered_map<BoardConfig, MoveSequence, BoardConfigHash>::iterator it = b2s.begin();
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
                status = view.PollInput();
                std::cout << "received command with result ";
                DispErrorCode(status);
            }

            view.Render();
        }

        isComputerTurn = !isComputerTurn;
    }
}

void GraphicHumanVsHuman()
{
    ScenarioBuilder _builder;
    _builder.AttachNewRW(SFMLRWFactory());
    _builder.withFirstTurn();

    auto human_turn = [&]()
    {
        if(!_builder.GetView())
            throw std::runtime_error("Tried human moves without initializing view");
    
        _builder.GetView()->InstructionMessage("Awaiting command\n");
        
        while(true)
        {
            Nardi::status_codes status = _builder.GetView()->PollInput();
            if (status != Nardi::status_codes::WAITING)
                _builder.GetView()->DispErrorCode(status);

            if (status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
                break;
        }
    };

    while(!_builder.GetCtrl().QuitRequested() && !_builder.GetGame().GameIsOver())
    {
        human_turn();
    }
}

int main()
{
    GraphicHumanVsHuman();
    // HumanVsRand();

    // builder.AttachNewRW(SFMLRWFactory());

    // BoardConfig block_dubar = {{    { 7, 0,-2, 1, 2, 1, 1, 1, 0, 0, 1, 0},
    //                                 {-6,-1, 0,-1,-2,-1, 1, 0, 0,-2, 0, 0} }};

    // builder.withScenario(white, block_dubar, 2, 2);
    // auto status = builder.GetCtrl().ReceiveCommand(Command(0, 4));
    // status = builder.GetCtrl().ReceiveCommand(Command(0, 8));

    // DispErrorCode(status);

    // builder.withRandomEndgame();

    // builder.AttachNewRW(TerminalRWFactory());

    // builder.Render();

    // builder.withFirstTurn();
    // auto rc = builder.withScenario(white, TestGlobals::start_brd, 1, 4);
    // rc = builder.ReceiveCommand(Command(0,0));
    // rc = builder.ReceiveCommand(Command(first));
    // rc = builder.ReceiveCommand(Command(0,1));
    // rc = builder.ReceiveCommand(Command(second));

    // rc = builder.withDice(3, 6);
    // auto b2s = builder.GetGame().GetBoards2Seqs();

    // std::cout<<"\n";

    // auto brd = TestGlobals::block_wrap1;

    // // Test with wrap_dice1 = {1, 6}
    // auto rc = builder.withScenario(white, brd, TestGlobals::wrap_dice1[0], TestGlobals::wrap_dice1[1], 0, 0);

    // // Select (1,8) and move by dice first (1) to get to (1,9), forming a blocked position
    // rc = builder.ReceiveCommand(Command(1,8));
    // rc = builder.ReceiveCommand(Command(first));

    // // Try to move second dice (6) from (0,0) - should NOT succeed due to blockade
    // rc = builder.ReceiveCommand(Command(0,0));
    // rc = builder.ReceiveCommand(Command(second));

    // // Test with wrap_dice2 = {1, 3}
    // rc = builder.withScenario(white, brd, TestGlobals::wrap_dice2[0], TestGlobals::wrap_dice2[1], 0, 0);

    // builder.StatusReport();

    return 0;
}