#include "Game.h"
#include "Controller.h"
#include "SFMLRW.h"
#include "ScenarioBuilder.h"


int main()
{
    Nardi::Game g;
    Nardi::Controller c(g);
    std::shared_ptr<Nardi::SFMLRW> rw = std::make_shared<Nardi::SFMLRW>(g, c);

    g.AttachReaderWriter(rw.get());

    while(!c.QuitRequested() && !g.GameIsOver())
    {
        rw->PollInput();
        rw->Render();
    }

}