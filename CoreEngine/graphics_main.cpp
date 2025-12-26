#include "Game.h"
#include "Controller.h"
#include "SFMLRW.h"
#include "ScenarioBuilder.h"


int main()
{
    bool running = true;
    while (running)
    {
        Nardi::Game g;
        Nardi::Controller c(g);
        std::shared_ptr<Nardi::SFMLRW> rw = std::make_shared<Nardi::SFMLRW>(g, c);
        g.AttachReaderWriter(rw.get());

        while(!c.QuitRequested())
        {
            rw->PollInput();
            rw->Render();

            if(g.GameIsOver())
            {
                rw->OnGameEvent(GameEvent{ EventCode::GAME_OVER, std::monostate{} });
                break;
            }
        }

        while (!c.QuitRequested() && !c.RestartRequested())
        {
            rw->PollInput();
            rw->Render();
        }

        if (!c.RestartRequested())
            running = false;
    }
}