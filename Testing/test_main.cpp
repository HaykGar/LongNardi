#include "Testing.h"

int main()
{
    TestBuilder tester;

    std::array< std::array<int, COL>, ROW> brd = {  { {15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }  };
    std::array<int, 2> dice = {6, 5};
    std::array<int, 2> dice_used = {0, 0};

    std::queue<TestCase> cases;
    cases.push(TestCase (
        "Select Test",
        Command(SELECT_SLOT, {0, 11}),
        brd,
        false, // white
        dice,
        dice_used,
        false,  // head not used
        Game::status_codes::SUCCESS
    ));

    cases.push(TestCase (
        "Move Test",
        Command(SELECT_SLOT, {0, 0}),
        brd,
        false, // white
        dice,
        dice_used,
        false,  // head not used
        Game::status_codes::NO_LEGAL_MOVES_LEFT,
        {0, 0} // start coord
    ));

    while(!cases.empty())
    {
        tester.Test(cases.front());
        cases.pop();
    }

}