#include <gtest/gtest.h>
#include "Testing.h" 

/*──────────────────────────────────────────────────────────────────────────────
Doubles first move weirdness
──────────────────────────────────────────────────────────────────────────────*/

TEST_F(TestBuilder, FirstMoveDouble4Or6)
{
    withFirstTurn();
    status_codes outcome = StartOfTurn(white, start_brd, 4, 4);
    EXPECT_EQ(outcome, status_codes::NO_LEGAL_MOVES_LEFT) << "4 4 first move failed";
    EXPECT_EQ(GetBoardAt(0, 8), 2);
    PrintBoard();

    ASSERT_NE(status_codes::SUCCESS, ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0}))) << "able to select before rolling";
    outcome = withDice(6, 6);
    ASSERT_EQ(outcome, status_codes::NO_LEGAL_MOVES_LEFT) << "6 6 first move failed";
    ASSERT_EQ(GetBoardAt(1, 6), -2);

    PrintBoard();
}