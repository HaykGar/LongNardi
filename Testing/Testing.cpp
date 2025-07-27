#include "Testing.h"

//////////////////////////////
/////    TestBuilder    /////
////////////////////////////

//////////////// Initialization ////////////////

TestBuilder::TestBuilder()
{
    _game = std::make_unique<Game>(1);
    _ctrl = std::make_unique<Controller>(*_game);
    _game->turn_number = {2, 2}; // avoiding first turn case unless explicitly requested
}

void TestBuilder::StartPreRoll(bool p_idx, const std::array<std::array<int, COL>, ROW>& b)
{
    withPlayer(p_idx);
    withBoard(b);
    ResetControllerState();
}

status_codes TestBuilder::StartOfTurn(bool p_idx, const std::array<std::array<int, COL>, ROW>& b, int d1, int d2)
{
    StartPreRoll(p_idx, b);
    return withDice(d1, d2);
}

status_codes TestBuilder::withDice(int d1, int d2)
{
    _game->SetDice(d1, d2);
    _game->board.ResetMock();

    status_codes outcome = _game->OnRoll();

    PrintBoard();

    if(outcome == status_codes::NO_LEGAL_MOVES_LEFT)
        _ctrl->SwitchTurns();
    else
        _ctrl->dice_rolled = true;

    return outcome;
}

void TestBuilder::withPlayer(bool p_idx)
{
    _game->board.SetPlayer(p_idx);
}

void TestBuilder::withBoard(const std::array<std::array<int, COL>, ROW>& b)
{
    _game->board._realBoard.head_used = false;
    _game->board._realBoard.SetData(b);
    _game->board.ResetMock();
}

void TestBuilder::ResetControllerState()
{
    _ctrl->start_selected = false;
    _ctrl->dice_rolled = false; 
    _ctrl->quit_requested = false;
}

void TestBuilder::withFirstTurn()
{
    _game->turn_number = {0, 0};
}

//////////////// Actions ////////////////

status_codes TestBuilder::ReceiveCommand(const Command& cmd)
{
    // std::cout << "\n\n\n\n";
    // std::cout << "received command: " << static_cast<int>(cmd.action) << "\n";

    auto rc = _ctrl->ReceiveCommand(cmd);
    DispErrorCode(rc);
    return rc;
}

void TestBuilder::PrintBoard() const
{
    for(int r = 0; r < ROW; ++r)
    {
        for (int c = 0; c < COL; ++c)
        {
            std::cout << _game->board.at(r, c) << "\t";
        }
        std::cout << "\n\n";
    }
    std::cout << "\n\n\n";
}

void TestBuilder::StatusReport() const
{
    std::cout << "player: " << _game->board.PlayerIdx() << "\n";

    std::cout << "dice: " << _game->dice[0] << " " << _game->dice[1] << "\n";

    std::cout << "dice used: " << _game->times_dice_used[0] << ", " << _game->times_dice_used[1] << "\n";

    std::cout << "board: \n";
    PrintBoard();
}