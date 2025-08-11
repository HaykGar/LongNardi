#include <gtest/gtest.h>
#include "Testing.h" 


/*──────────────────────────────────────────────────────────────────────────────
  1. Two-step turn: first die works, second die blocked (enemy pile)
──────────────────────────────────────────────────────────────────────────────*/
TEST_F(TestBuilder, Move_TwoStep_FirstOkSecondIllegal)
{
    auto brd = SafeBoard();
    brd[0][0] = 1;     // white piece to move
    brd[0][1] = 1;
    brd[0][7] = -1;    // enemy pile blocks second step
    brd[0][8] = -1;

    // dice 3 (idx0) and 4 (idx1) → 0,0→0,3 legal, 0,3→0,7 illegal
    ASSERT_EQ(StartOfTurn(white, brd, 3, 4), status_codes::SUCCESS);

    // step 1
    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0,0}}), status_codes::SUCCESS)
        << "could not select head";
    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0, 7} }), status_codes::DEST_ENEMY)
        << "second step should be DEST_ENEMY";

    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0,1}}), status_codes::SUCCESS)
        << "could not select 0, 1";
    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0, 8} }), status_codes::DEST_ENEMY)
        << "second step should be DEST_ENEM";
}







/*──────────────────────────────────────────────────────────────────────────────
  2. Use same die twice → DICE_USED_ALREADY
──────────────────────────────────────────────────────────────────────────────*/
TEST_F(TestBuilder, Move_ReusingSameDieTwice)
{
    auto brd = SafeBoard();
    brd[0][1] = 2;      // two white pieces

    ASSERT_EQ(StartOfTurn(white, brd, 1, 3), status_codes::SUCCESS);

    // move #1 with die-0
    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0,1}}), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand({Actions::MOVE_BY_DICE, first}),     status_codes::SUCCESS);

    // try to re-use die-0 on another piece
    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0,1}}), status_codes::SUCCESS);
    EXPECT_NE(ReceiveCommand({Actions::MOVE_BY_DICE, first}),     status_codes::SUCCESS)
        << "re-using exhausted die should yield DICE_USED_ALREADY";
}


/*──────────────────────────────────────────────────────────────────────────────
Happy-path: two legal moves consume both dice cleanly
──────────────────────────────────────────────────────────────────────────────*/
TEST_F(TestBuilder, Legality_AllGood)
{
    auto brd = SafeBoard();
    brd[0][0] = 1;
    brd[0][2] = 1;

    ASSERT_EQ(StartOfTurn(white, brd, 2, 3), status_codes::SUCCESS);

    // move head by 2
    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0,0}}), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand({Actions::MOVE_BY_DICE, first}),     status_codes::SUCCESS);

    // move second pile by 3
    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0,2}}), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand({Actions::MOVE_BY_DICE, 1}),     status_codes::NO_LEGAL_MOVES_LEFT);

    // further move should yield NO_LEGAL_MOVES_LEFT
    EXPECT_NE(ReceiveCommand({Actions::MOVE_BY_DICE, first}),      status_codes::SUCCESS)
        << "after both dice used, turn over";
}