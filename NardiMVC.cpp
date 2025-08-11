#include "NardiMVC.h"

NardiMVC::NardiMVC(ReaderWriterFactory factory, int seed) : model(seed), controller(model), view(factory(model, controller))
{
    model.AttachReaderWriter(view.get());
}

NardiMVC::NardiMVC(ReaderWriterFactory factory) : model(), controller(model), view(factory(model, controller))
{
    model.AttachReaderWriter(view.get());
}

void NardiMVC::AwaitUserCommand()
{
    view->AwaitUserCommand();
}

void NardiMVC::Animate() const
{
    view->ReAnimate();
}