#include <gtest/gtest.h>
#include "Testing.h" 



/*──────────────────────────────────────────────────────────────────────────────
Doubles first move weirdness
──────────────────────────────────────────────────────────────────────────────*/

TEST_F(TestBuilder, FirstMoveDouble4Or6)
{
    withFirstTurn();
    status_codes outcome = StartOfTurn(white, start_brd, 4, 4);
    ASSERT_EQ(outcome, status_codes::NO_LEGAL_MOVES_LEFT) << "4 4 first move failed";
    ASSERT_EQ(GetBoardAt(0, 8), 2);
    PrintBoard();

    ASSERT_NE(status_codes::SUCCESS, ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0})));
    outcome = withDice(6, 6);
    ASSERT_EQ(outcome, status_codes::NO_LEGAL_MOVES_LEFT) << "6 6 first move failed";
    ASSERT_EQ(GetBoardAt(1, 6), -2);

    PrintBoard();
}

/*──────────────────────────────────────────────────────────────────────────────
Doubles forced-move: only one 4-move exists → onRoll resolves, no moves left
──────────────────────────────────────────────────────────────────────────────*/
TEST_F(TestBuilder, LegalityForcedMovesDoubles)
{
    auto brd = ZeroWhite1BlackBoard();
    brd[0][0] = 1;      // only this checker can move 4
    brd[1][2] = 1;
    brd[0][8] = -1;     // enemy piece blocks second 4; forces unique move
    brd[1][6] = -1;

    DisplayBoard(brd);

    EXPECT_EQ(StartOfTurn(white, brd, 4, 4), status_codes::NO_LEGAL_MOVES_LEFT)
        << "onRoll should auto-move once and leave NO_LEGAL_MOVES_LEFT";

    // fixme needs more situations !! `
}

TEST_F(TestBuilder, Extra)
{
    auto stat = StartOfTurn(white, doubles_stacked, ds[0], ds[1]);
    DispErrorCode(stat);
    ASSERT_EQ(stat, status_codes::SUCCESS);
}

////////// Chat

using enum status_codes;

