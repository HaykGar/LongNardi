#include <gtest/gtest.h>
#include "Testing.h"

/*──────────────────────────────────────────────────────────────────────────────
  8. Move that appears legal but leaves no options → PREVENTS_COMPLETION
──────────────────────────────────────────────────────────────────────────────*/
TEST_F(TestBuilder, Legality_MovePreventsCompletion)
{
    auto brd = ZeroWhite1BlackBoard();
    brd[0][0] = 5;  // A
    brd[0][11] = -1; // no 6-5 for A
    brd[0][3] = 1;  // B
    brd[0][8] = -1; // B can't go 5, but 6-5 works
    brd[0][5] = 1;  // C
    brd[0][10] = -1;    // C no 5 or 6

    std::cout << "starting board... \n\n";
    DisplayBoard(brd);

    auto rc = StartOfTurn(white, brd, 5, 6);
    PrintBoard();
    DispErrorCode(rc);

    ASSERT_EQ(rc, status_codes::SUCCESS);   // not forced to the end, but partially forced
    ASSERT_EQ(GetBoardAt(0, 9), 1) << "did not force only 6 that doesn't prevent completion";
    ASSERT_EQ(GetBoardAt(0, 3), 0);


    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0,0}}), status_codes::SUCCESS);
    EXPECT_NE(ReceiveCommand({Actions::MOVE_BY_DICE, 1}),    status_codes::SUCCESS)
        << "dice 6 should not be re-usable ";

    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0,0}}), status_codes::SUCCESS);
    rc = ReceiveCommand({Actions::MOVE_BY_DICE, first});
    EXPECT_EQ(rc, status_codes::NO_LEGAL_MOVES_LEFT) << "couldn't move from head";
    ASSERT_GE(GetBoardAt(0, 5), 1);

    brd = ZeroWhite1BlackBoard();
    brd[0][0] = 3;  // 2, 1, NOT both
    brd[0][3] = -1;

    brd[0][6] = 1;  // 2 -> 1 ONLY
    brd[0][7] = -1;

    brd[0][11] = 1; // 2 -> 1 ONLY
    brd[1][0] = -2;

    std::cout << "pre roll: \n";
    DisplayBoard(brd);

    ASSERT_EQ(StartOfTurn(white, brd, 1, 2), status_codes::SUCCESS);

    StatusReport();

    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0,0}}), status_codes::SUCCESS);
    EXPECT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0,2}}), status_codes::PREVENTS_COMPLETION);
}

TEST_F(TestBuilder, PreventsCompletion)
{
  auto brd = preventions1;
  status_codes outcome = StartOfTurn(white, brd, 1, 2);
  ASSERT_EQ(outcome, status_codes::SUCCESS) << "forced all on roll - preventions1";

  outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0}));
  ASSERT_EQ(outcome, status_codes::SUCCESS) << "unable to select head - preventions1";

  outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 2}));
  EXPECT_EQ(outcome, status_codes::PREVENTS_COMPLETION);

  brd[0][6] = 0;
  outcome = StartOfTurn(white, brd, 1, 2);
  EXPECT_EQ(outcome, status_codes::SUCCESS);
  EXPECT_EQ(GetBoardAt(1, 1), 1) << "did not partially force move, head->2 will prevent";

  outcome = StartOfTurn(white, preventions2, 1, 2);
  ASSERT_EQ(outcome, status_codes::SUCCESS) << "forced on roll - preventions2";

  outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 3}));
  ASSERT_EQ(outcome, status_codes::SUCCESS) << "unable to select start - preventions2";

  outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 5}));
  EXPECT_EQ(outcome, status_codes::PREVENTS_COMPLETION) << "should be no available 1 moves";
}