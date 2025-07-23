#include "Testing.h"

//////////////////////////////
/////    TestBuilder    /////
////////////////////////////

//////////////// Initialization ////////////////

TestBuilder::TestBuilder() : _game(1), _ctrl(_game)
{
    _game.turn_number = {2, 2}; // avoiding first turn case unless explicitly requested
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
    _game.SetDice(d1, d2);
    status_codes outcome = _game.OnRoll();

    // PrintBoard();
    //std::cout << "on dice roll: ";
    DispErrorCode(outcome);

    if(outcome == status_codes::NO_LEGAL_MOVES_LEFT)
        _ctrl.SwitchTurns();
    else
        _ctrl.dice_rolled = true;

    return outcome;
}

void TestBuilder::withPlayer(bool p_idx)
{
    _game.board.player_idx = p_idx;
    _game.board.player_sign = p_idx ? -1 : 1;
}


void TestBuilder::withBoard(const std::array<std::array<int, COL>, ROW>& b)
{
    _game.board.data = b;
    SetDerived();
}

void TestBuilder::SetDerived()
{
    _game.board.head_used = false;
    CalcPiecesLeftandReached();   // need this before availabilities for detecting endgame state
    ConstructAvailabilitySets();
}

void TestBuilder::CalcPiecesLeftandReached()
{
    NardiCoord starts [2] = { {0, 0}, {1, 0} };
    _game.board.pieces_left = {0, 0};
    _game.board.reached_enemy_home = {0, 0};
    
    for (; starts[0].col < 6; 
                starts[0] = _game.board.CoordAfterDistance(starts[0], 1),
                starts[1] = _game.board.CoordAfterDistance(starts[1], 1)    )
    {
        // std::cout << "r1 start: " << starts[0].row << ", " << starts[0].col << std::endl;

        _game.board.pieces_left[_game.board.at(starts[0]) < 0] += abs(_game.board.at(starts[0]));   // no-op if 0
        _game.board.pieces_left[_game.board.at(starts[1]) < 0] += abs(_game.board.at(starts[1]));
    }
    for (;  starts[0].row == 0 && starts[0].col < COL;
                    starts[0] = _game.board.CoordAfterDistance(starts[0], 1),
                    starts[1] = _game.board.CoordAfterDistance(starts[1], 1)    )    
    {
                    
        // std::cout << "r1 start: " << starts[0].row << ", " << starts[0].col << std::endl;

        if(_game.board.at(starts[0]) < 0)   // black piece at end of white's row - home
            _game.board.reached_enemy_home[1] += abs(_game.board.at(starts[0]));
        if(_game.board.at(starts[1]) > 0)   // white piece at end of black's row - home
            _game.board.reached_enemy_home[0] += abs(_game.board.at(starts[0]));

        _game.board.pieces_left[_game.board.at(starts[0]) < 0] += abs(_game.board.at(starts[0]));   // no-op if 0
        _game.board.pieces_left[_game.board.at(starts[1]) < 0] += abs(_game.board.at(starts[1]));
    }
    // std::cout << "\n\n\n";
}

void TestBuilder::ConstructAvailabilitySets()
{
    for(int i = 0; i < 6; ++i)
    {
        _game.board.goes_idx_plusone[0][i].clear();
        _game.board.goes_idx_plusone[1][i].clear();
    }

    const auto& board = _game.GetBoardRef();
    NardiCoord start(0, 0);
    while (!start.OutOfBounds())
    {
        if(board.at(start) != 0)
        {
            bool player_idx = board.at(start) < 0;
            for(int d = 1; d <= 6; ++d)
            {
                NardiCoord dest = _game.board.CoordAfterDistance(start, d, player_idx);
                if(_game.board.WellDefinedEnd(start, dest) == status_codes::SUCCESS)    // relies on p_idx
                    _game.board.goes_idx_plusone[player_idx][d-1].insert(start);
            }
        }
        start = board.CoordAfterDistance(start, 1, 0);
    }

    if(_game.board.CurrPlayerInEndgame())
    {
        _game.board.SetMaxOcc();
        for(int i = _game.board.max_num_occ.at(_game.board.player_idx); i <= 6; ++i)
            _game.board.goes_idx_plusone[_game.board.player_idx][i-1].insert({
                    !_game.board.player_idx, COL - _game.board.max_num_occ.at(_game.board.player_idx)}); 

        for(int i = 1; i < _game.board.max_num_occ.at(_game.board.player_idx); ++i)
            if(board.at(!_game.board.player_idx, COL - i) * _game.board.player_sign  > 0)
                _game.board.goes_idx_plusone[_game.board.player_idx][i-1].insert({!_game.board.player_idx, COL - i});
    }
}

void TestBuilder::ResetControllerState()
{
    _ctrl.start_selected = false;
    _ctrl.dice_rolled = false; 
    _ctrl.quit_requested = false;
}

void TestBuilder::withFirstTurn()
{
    _game.turn_number = {0, 0};
}

//////////////// Actions ////////////////

status_codes TestBuilder::ReceiveCommand(const Command& cmd)
{
    // std::cout << "\n\n\n\n";
    // std::cout << "received command: " << static_cast<int>(cmd.action) << "\n";

    return _ctrl.ReceiveCommand(cmd);
}

void TestBuilder::PrintBoard() const
{
    for(int r = 0; r < ROW; ++r)
    {
        for (int c = 0; c < COL; ++c)
        {
            std::cout << _game.board.data.at(r).at(c) << "\t";
        }
        std::cout << "\n\n";
    }
    std::cout << "\n\n\n";
}