#include "Testing.h"

using namespace Nardi;

//////////////////////////////
/////    TestBuilder    /////
////////////////////////////

//////////////// Initialization ////////////////

TestBuilder::TestBuilder() : _bldr(), _game(_bldr.GetGame()), _ctrl(_bldr.GetCtrl())
{}

status_codes TestBuilder::StartOfTurn(bool p_idx, const BoardConfig& b, int d1, int d2)
{
    return _bldr.withScenario(p_idx, b, d1, d2, 0, 0);
}

status_codes TestBuilder::withDice(int d1, int d2)
{
    return _bldr.withDice(d1, d2);
}


void TestBuilder::withFirstTurn()
{
    _bldr.withFirstTurn();
}

//////////////// Actions ////////////////

status_codes TestBuilder::ReceiveCommand(const Command& cmd)
{
    return _bldr.ReceiveCommand(cmd);
}

void TestBuilder::PrintBoard() const
{
    _bldr.PrintBoard();
}

void TestBuilder::StatusReport() const
{
   _bldr.StatusReport();
}