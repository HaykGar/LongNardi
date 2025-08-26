#include "NardiMVC.h"
#include "TerminalRW.h"
#include <iostream>

using namespace Nardi;

int main()
{

    NardiMVC manager(TerminalRWFactory);

    manager.Animate();

    while(!manager.QuitRequested() && !manager.GameIsOver())
    {
        manager.Animate();
        std::cout << "Awaiting command\n";
        manager.AwaitUserCommand();
        std::cout << "Command received\n\n\n";    // can be more specific later
    }

    if(manager.GameIsOver())
        std::cout << "Game Over\n";

    return 0;
}