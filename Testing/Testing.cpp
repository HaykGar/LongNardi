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
        Game::status_codes exp
    ) :
        message(std::move(msg)),
        cmd(std::move(c)),
        brd(board),
        p_idx(player),
        dice_(dice),
        diceUsed_(diceUsed),
        headUsed_(headUsed),
        expected(exp)
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
        NardiCoord s
    ) :
        message(std::move(msg)),
        cmd(std::move(c)),
        brd(board),
        p_idx(player),
        dice_(dice),
        diceUsed_(diceUsed),
        headUsed_(headUsed),
        expected(exp),
        start(s)
    {}


//////////////////////////
/////    Builder    /////
////////////////////////

TestBuilder::TestBuilder(int seed) : _game(seed), _ctrl(_game), srw(_game, _ctrl) 
{
    _game.AttachReaderWriter(&srw);
}

TestBuilder& TestBuilder::withBoard(const std::array<std::array<int, COL>, ROW>& b)
{
    _game.board = b;
    ConstructAvailabilitySets();
    return *this;
}

TestBuilder& TestBuilder::withPlayer(bool p_idx)
{
    _game.player_idx = p_idx;
    _game.player_sign = p_idx ? -1 : 1;
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
    _ctrl.start = s;
    _ctrl.start_selected = true;
    return *this;
}

void TestBuilder::ConstructAvailabilitySets()
{
    Game::Arbiter& arb = _game.arbiter;
    const std::array<std::array<int, COL>, ROW>& board = _game.GetBoardRef();
    for(int r = 0; r < ROW; ++r)
    {
        for(int c = 0; c < COL; ++c)
        {
            if(board[r][c] != 0)
            {
                bool player_idx = board[r][c] < 0;
                withPlayer(player_idx);
                NardiCoord start(r, c);
                for(int d = 1; d <= 6; ++d)
                {
                    NardiCoord dest = arb.CoordAfterDistance(start, d);
                    if(arb.WellDefinedMove(start, dest) == Game::status_codes::SUCCESS)
                        arb.goes_idx_plusone[player_idx][d-1].insert(start);
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
    withBoard(t_case.brd);
    withDice(t_case.dice_);
    withDiceUsed(t_case.diceUsed_);
    withPlayer(t_case.p_idx);

    if(t_case.start)
        withStart(t_case.start.value());
            
    if(!t_case.message.empty())
        std::cout << t_case.message << std::endl;
    
    Game::status_codes result = _ctrl.ReceiveCommand(t_case.cmd);
    bool passed = result == t_case.expected;
    std::cout << (passed ? "[PASSED]" : "[FAILED]") << "\n\n";
            
    return passed;
}

///////////////////////////
/////    SilentRW    /////
/////////////////////////

SilentRW::SilentRW(const Game& game, Controller& c) : ReaderWriter(game, c) {}

void SilentRW::AwaitUserCommand() {}
void SilentRW::ReAnimate() const {}        
void SilentRW::AnimateDice() const {}
void SilentRW::InstructionMessage(std::string m) const {}
void SilentRW::ErrorMessage(std::string m) const {}

Command SilentRW::Input_to_Command() const { return Command(Actions::NO_OP); } 

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
    std::array< std::array<int, COL>, ROW> brd = {  { {15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }  };
    std::array<int, 2> dice = {6, 5};
    std::array<int, 2> dice_used = {0, 0};

    std::queue<TestCase> cases;

    cases.push(TestCase (
        "Select Test",
        Command(Actions::SELECT_SLOT, {0, 11}),
        brd,
        false, // white
        dice,
        dice_used,
        false,  // head not used
        Game::status_codes::SUCCESS
    ));

    cases.push(TestCase (
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

    std::array< std::array<int, COL>, ROW> brd2 = {  { {13, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                                    {-10, 0, 0, 0, 0, 0, 0, -1, 0, -1, 0, 1} }  };

    std::array< std::array<int, COL>, ROW> brd3 = {  {  {15, 0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0}, 
                                                        {-13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }  };

    std::array<int, 2> dice2 = {3, 4};

    cases.push(TestCase(
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

    cases.push(TestCase(
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

    cases.push(TestCase(
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

    cases.push(TestCase(
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

    cases.push(TestCase(
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

    cases.push(TestCase(
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

    cases.push(TestCase(
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

}

void TestLoader::add_legality_cases()
{
    cases.push(TestCase(
        "Prevents Turn Finish",
        Command(Actions::MOVE_BY_DICE, 0),  // move by 5
        brd2,
        true, // black
        dice, 
        dice_used,
        false,  // head not used
        Game::status_codes::ILLEGAL_MOVE,
        {1, 0} // start coord
    ));
}
