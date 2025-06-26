#pragma once

#include "NardiGame.h"
#include "Controller.h"
#include "ReaderWriter.h"

#include <memory>
#include <functional>

/*

Manages the full Model-View-Controller flow and set-up between game, controller, and ReaderWriter

*/

using ReaderWriterFactory = std::function<std::unique_ptr<ReaderWriter>(Game&, Controller&) >;

class NardiMVC
{
    public:
        NardiMVC(ReaderWriterFactory factory, int seed = 1);

        void AwaitUserCommand();

        bool QuitRequested() const;
        bool GameIsOver() const;

        void Animate() const;

    private:
        Game model;
        Controller controller;
        std::unique_ptr<ReaderWriter> view;

};

inline
bool NardiMVC::QuitRequested() const
{
    return controller.QuitRequested();
}

inline 
bool NardiMVC::GameIsOver() const
{
    return model.GameIsOver();
}
