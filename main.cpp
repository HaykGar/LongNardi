#include "NardiMVC.h"
#include "TerminalRW.h"

#include <iostream>

int main()
{

    NardiMVC manager(TerminalRWFactory);

    manager.Animate();

    while(!manager.QuitRequested() && !manager.GameIsOver())
    {
        std::cout << "Enter r to roll dice, u to undo, or q to quit. Otherwise, enter coordinates separated by white space\n";
        manager.AwaitUserCommand();
        std::cout << "Command received, awaiting further commands\n";    // can be more specific later
    }

    return 0;
}