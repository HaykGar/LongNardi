#include "NardiGame.h"
#include "TerminalRW.h"

int main()
{
    Game g;
    TerminalRW trw(g);
    g.AttachReaderWriter(&trw);
    g.PlayGame();
    return 0;
}