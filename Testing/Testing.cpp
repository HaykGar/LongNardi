#include "Testing.h"

///////////////////////////
/////    TestCase    /////
/////////////////////////
TestCase::TestCase(
        std::string msg,
        Command c,
        std::array<std::array<int, COL>, ROW>& board,
        bool player,
        std::array<int, 2>& dice,
        std::array<int, 2>& diceUsed,
        bool headUsed,
        Game::status_codes exp,
        bool fm
    ) :
        message(std::move(msg)),
        cmd(std::move(c)),
        brd(board),
        p_idx(player),
        dice_(dice),
        diceUsed_(diceUsed),
        headUsed_(headUsed),
        expected(exp),
        first_move(fm)
    {}

TestCase::TestCase(
        std::string msg,
        Command c,
        std::array<std::array<int, COL>, ROW>& board,
        bool player,
        std::array<int, 2>& dice,
        std::array<int, 2>& diceUsed,
        bool headUsed,
        Game::status_codes exp,
        NardiCoord s,
        bool fm
    ) :
        message(std::move(msg)),
        cmd(std::move(c)),
        brd(board),
        p_idx(player),
        dice_(dice),
        diceUsed_(diceUsed),
        headUsed_(headUsed),
        expected(exp),
        start(s),
        first_move(fm)
    {}


//////////////////////////
/////    Builder    /////
////////////////////////

TestBuilder::TestBuilder(int seed) : _game(seed), _ctrl(_game), trw(_game, _ctrl) 
{
    _game.AttachReaderWriter(&trw);
}

TestBuilder& TestBuilder::withBoard(const std::array<std::array<int, COL>, ROW>& b)
{
    _game.board = b;
    ConstructAvailabilitySets();
    return *this;
}

TestBuilder& TestBuilder::withPlayer(bool p_idx)
{
    _game.arbiter.player_idx = p_idx;
    _game.arbiter.player_sign = p_idx ? -1 : 1;
    return *this;
}

TestBuilder& TestBuilder::withDice(const std::array<int,2>& d)
{
    _game.dice[0] = d[0]; 
    _game.dice[1] = d[1];
    _game.doubles_rolled = (d[0] == d[1]);
    ResetControllerState();
    _ctrl.dice_rolled = true;
    return *this;
}

TestBuilder& TestBuilder::withDiceUsed(const std::array<int,2>& d_used)
{
    _game.dice_used[0] = d_used[0];
    _game.dice_used[1] = d_used[1];
    return *this;
}

TestBuilder& TestBuilder::withHeadUsed(bool used)
{
    _game.arbiter.head_used = used;
    return *this;
}

TestBuilder& TestBuilder::withStart(NardiCoord s)
{
    VisualToGameCoord(s);
    _ctrl.start = s;
    _ctrl.start_selected = true;
    return *this;
}

void TestBuilder::ConstructAvailabilitySets()
{
    for(int i = 0; i < 6; ++i)
    {
        _game.arbiter.goes_idx_plusone[0][i].clear();
        _game.arbiter.goes_idx_plusone[1][i].clear();
    }

    const std::array<std::array<int, COL>, ROW>& board = _game.GetBoardRef();
    for(int r = 0; r < ROW; ++r)
    {
        for(int c = 0; c < COL; ++c)
        {
            if(board[r][c] != 0)
            {
                bool player_idx = board[r][c] < 0;
                withPlayer(player_idx);                
                for(int d = 1; d <= 6; ++d)
                {
                    NardiCoord dest = _game.arbiter.CoordAfterDistance(r, c, d, player_idx);
                    if(_game.arbiter.WellDefinedEnd({r, c}, dest) == Game::status_codes::SUCCESS)
                        _game.arbiter.goes_idx_plusone[player_idx][d-1].insert({r, c});
                }
            }
        }
    }
}

void TestBuilder::ResetControllerState()
{
    _ctrl.start_selected = false;
    _ctrl.dice_rolled = false; 
    _ctrl.quit_requested = false;
}

bool TestBuilder::Test(TestCase t_case) 
{
    ResetControllerState();

    Actions test_action = t_case.cmd.action;

    if(!t_case.message.empty())
        std::cout << t_case.message << std::endl;

    withBoard(t_case.brd);
    withPlayer(t_case.p_idx);
    withDice(t_case.dice_);
    withDiceUsed(t_case.diceUsed_);
    if(t_case.start) 
        withStart(t_case.start.value());
    if(t_case.first_move)
        _game.arbiter.turn_number[_game.arbiter.player_idx] = 1;

    if(test_action == Actions::MOVE_BY_DICE || (test_action == Actions::SELECT_SLOT && t_case.start) 
        || test_action == Actions::ROLL_DICE)
    {
        std::cout << "Start Board:\n";
        _game.rw->ReAnimate();
    }

    Game::status_codes result;
    if (test_action == Actions::ROLL_DICE)
        result = _game.arbiter.MakeForcedMovesBothDiceUsable();
    else
        result = _ctrl.ReceiveCommand(t_case.cmd);
    
    bool passed = result == t_case.expected;

    std::cout << "End Board:\n";
    _game.rw->ReAnimate();
    std::cout <<"\n";
    std::cout << (passed ? "[PASSED]" : "[FAILED]") << "\n\n";
            
    return passed;
}


//////////////////////////////////////
/////    Test Case Generator    /////
////////////////////////////////////

std::vector<TestCase> TestLoader::operator() ()
{
    cases = {};
    add_basic_move_cases();
    add_dice_misuse_cases();
    add_legality_cases();
    
    // push back quit request cases
    // push back dice roll after dice already rolled case

    return cases;
}

void TestLoader::add_basic_move_cases()
{
    std::array< std::array<int, COL>, ROW> brd = {{ {PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                                    {-PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};
    std::array<int, 2> dice = {6, 5};
    std::array<int, 2> dice_used = {0, 0};

    cases.push_back(TestCase (
        "Select Test",
        Command(Actions::SELECT_SLOT, {0, 11}),
        brd,
        false, // white
        dice,
        dice_used,
        false,  // head not used
        Game::status_codes::SUCCESS
    ));

    cases.push_back(TestCase (
        "Move Test",
        Command(Actions::SELECT_SLOT, {0, 0}),  
        brd,
        false, // white
        dice,
        dice_used,
        false,  // head not used
        Game::status_codes::NO_LEGAL_MOVES_LEFT,
        {0, 11} // start coord as seen
    ));

    std::array< std::array<int, COL>, ROW> brd3 = {  {  {PIECES_PER_PLAYER, 0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0}, 
                                                        {-13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }  };

    std::array<int, 2> dice2 = {3, 4};

    cases.push_back(TestCase(
        "2 step move - both midpoints obstructed",
        Command(Actions::SELECT_SLOT, {0, 4}),
        brd3,
        false, // white
        dice2, 
        dice_used,
        false,  // head not used
        Game::status_codes::NO_PATH,
        {0, 11} // start coord
    ));

    std::array<int, 2> dice3 = {4, 2};

    cases.push_back(TestCase(
        "2 step move no obstruction",
        Command(Actions::SELECT_SLOT, {0, 5}),
        brd3,
        false, // white
        dice3, 
        dice_used,
        false,  // head not used
        Game::status_codes::NO_LEGAL_MOVES_LEFT,
        {0, 11} // start coord
    ));

    std::array<int, 2> dice4 = {5, 5};

    cases.push_back(TestCase(
        "Doubles - move 3x no obstructions",
        Command(Actions::SELECT_SLOT, {1, 3}),
        brd,    // starting board
        false, // white
        dice4, 
        dice_used,
        false,  // head not used
        Game::status_codes::NO_LEGAL_MOVES_LEFT,
        {0, 11} // start coord, head
    ));

    cases.push_back(TestCase(
        "Doubles - move 4x no obstructions",
        Command(Actions::SELECT_SLOT, {1, 8}),
        brd,    // starting board
        false, // white
        dice4, 
        dice_used,
        false,  // head not used
        Game::status_codes::NO_LEGAL_MOVES_LEFT,
        {0, 11} // start coord, head
    ));

    std::array< std::array<int, COL>, ROW> brd4 = {  {  {14, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0}, 
                                                        {-14, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0} }  };

    cases.push_back(TestCase(
        "Doubles - move 3x with obstruction",
        Command(Actions::SELECT_SLOT, {1, 3}),
        brd4,
        false, // white
        dice4, 
        dice_used,
        false,  // head not used
        Game::status_codes::NO_PATH,
        {0, 11} // start coord
    ));

    cases.push_back(TestCase(
        "Doubles - move 4x with obstruction",
        Command(Actions::SELECT_SLOT, {1, 8}),
        brd4,
        false, // white
        dice4, 
        dice_used,
        false,  // head not used
        Game::status_codes::NO_PATH,
        {0, 11} // start coord
    ));

    std::array< std::array<int, COL>, ROW> brd5 = {  {  {14, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0}, 
                                                        {-13, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0} }  };

    cases.push_back(TestCase(
        "Doubles - forced move, not enough for full turn",
        Command(Actions::MOVE_BY_DICE, 0),
        brd5,
        false, // white
        dice4, 
        dice_used,
        false,  // head not used
        Game::status_codes::NO_LEGAL_MOVES_LEFT,
        {0, 5} // start coord
    ));
}

void TestLoader::add_dice_misuse_cases()
{
    std::array< std::array<int, COL>, ROW> brd = {  { {13, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0}, 
                                                    {-14, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0} }  };
    
    std::array<int, 2> dice = {6, 5};
    std::array<int, 2> dice_used = {1, 0};

    cases.push_back(TestCase(
        "Reuse dice no doubles",
        Command(Actions::MOVE_BY_DICE, 0),
        brd,
        0, // white
        dice, 
        dice_used,
        false,  // head not used
        Game::status_codes::DICE_USED_ALREADY,
        {0, 0} // start coord
    ));

    std::array< std::array<int, COL>, ROW> start_brd = {  { {PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                                    {-PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }  };

    std::array<int, 2> dice2 = {6, 6};
    std::array<int, 2> dice_used2 = {0, 0};

    cases.push_back(TestCase(
        "First move double 6s",
        Command(Actions::ROLL_DICE),
        start_brd,
        0, // white
        dice2, 
        dice_used2,
        false,  // head not used
        Game::status_codes::NO_LEGAL_MOVES_LEFT,
        true    // first move
    ));

    dice2 = { {4, 4} };
        cases.push_back(TestCase(
        "First move double 4s",
        Command(Actions::ROLL_DICE),
        start_brd,
        0, // white
        dice2, 
        dice_used2,
        false,  // head not used
        Game::status_codes::NO_LEGAL_MOVES_LEFT,
        true    // first move
    ));
}

void TestLoader::add_legality_cases()
{
    std::array< std::array<int, COL>, ROW> brd = {  { {13, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                                    {-10, 0, 0, 0, 0, 0, 0, -1, 0, -1, 0, 1} }  };
    std::array<int, 2> dice = {6, 5};
    std::array<int, 2> dice_used = {0, 0};

    cases.push_back(TestCase(
        "Prevents Turn Finish",
        Command(Actions::MOVE_BY_DICE, 0),  // move by 5
        brd,
        true, // black
        dice, 
        dice_used,
        false,  // head not used
        Game::status_codes::ILLEGAL_MOVE,
        {1, 0} // start coord
    ));


}
