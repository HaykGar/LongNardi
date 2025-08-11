#include <gtest/gtest.h>
#include "Testing.h"

/*

Need unit tests for following functions:
    - LegalMove_2step:
        -   does NOT modify mock board
    - CanMoveByDice && CanFinishByDice

    - TurnCompletable
        - works as needed
        - does NOT modify mock
    Prevention illegal()

    bad blocks

    twosteppers()

    updatemovables()

    all board functions

*/


// ───────────────────────── 4. Misc start and force - mini-game ──────────────────────────

TEST_F(TestBuilder, MiniGame)
{
  DisplayBoard(start_brd);
  status_codes outcome = StartOfTurn(white, start_brd, 6, 5);
  PrintBoard();
  ASSERT_EQ(outcome, status_codes::NO_LEGAL_MOVES_LEFT) << "did not force all - white first move\n";
  ASSERT_EQ(GetBoardAt(0, 11), 1);
  ASSERT_EQ(GetBoardAt(0, 5), 0);
  (GetBoardAt(0, 6), 0);

  outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0}));  // player changed to black, dice not rolled
  EXPECT_NE(outcome, status_codes::SUCCESS) << "selected white despite black turn pre roll";

  outcome = withDice(5, 6);  // will complete all legal moves, pass turn to white
  ASSERT_EQ(outcome, status_codes::NO_LEGAL_MOVES_LEFT) << "did not force all - black first move\n";

  outcome = withDice(2, 2);
  ASSERT_EQ(outcome, status_codes::SUCCESS) << "unexpected forced on second move";

  outcome = ReceiveCommand(Command(Actions::MOVE_BY_DICE, first));
  ASSERT_NE(outcome, status_codes::SUCCESS) << "no start selected incorrectly handled"; 

  outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0})); // white select head
  ASSERT_EQ(outcome, status_codes::SUCCESS) << "failure to select white head";

  outcome = ReceiveCommand(Command(Actions::MOVE_BY_DICE, first));  // move by first dice from head
  ASSERT_EQ(outcome, status_codes::SUCCESS) << "failure to move from head on second move";

  outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0})); // white select head
  EXPECT_EQ(outcome, status_codes::HEAD_PLAYED_ALREADY) << "Reusing head";           // head already used, should not work
}

/*──────────────────────────────────────────────────────────────────────────────
   TESTS – MOVE-EXECUTION & LEGALITY MATRIX
   –  uses only the public TestBuilder API
   –  every assertion ends with a descriptive  <<  message
   –  coordinates are model-space (0,0 = white head)
──────────────────────────────────────────────────────────────────────────────*/



// TEST_F(TestBuilder, Blockade_FrontRowSix)
// {
//     auto b = ZeroWhite1BlackBoard();
//     // place a checker at head
//     b[0][0] = 1;
//     // occupy points 1–5
//     for (int c = 1; c <= 5; ++c) b[0][c] = 1;

//     ASSERT_EQ(StartOfTurn(white, b, /*d1*/2, /*d2*/4), status_codes::SUCCESS)
//         << "Nothing should be forced on roll (2,4)";

//     // select head
//     ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0})),
//               status_codes::SUCCESS)
//         << "Must be able to pick head";

//     // attempt two-step to 0,6 (creates a 6-point blockade at 1–6)
//     auto status = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,6}));
//     EXPECT_NE(status, status_codes::SUCCESS)
//         << "Manually creating 6-point blockade (1–6) must be rejected";
// }

/*
  Case 2: Blockade that wraps to second row.
    – Dice = (3,3) doubles → sum = 6 (two-step allowed).
    – Board has white at 0,9;0,10;0,11;1,0;1,1 (five in a row across the wrap).
    – Start from 0,8 (another checker), two-step to 1,2 makes points 9,10,11,0,1,2 all white
      with no enemy beyond 1,2.
*/

// TEST_F(TestBuilder, Blockade_WrapRowSix)
// {
//     auto b = ZeroWhite1BlackBoard();
//     // place checkers to form five-in-a-row across the wrap for black
//     b[1][0]  = -4;
//     b[1][1]  = -1;
//     b[1][2]  = -1;
//     b[1][3]  = -1;
//     b[0][11] = -1;
    
//     // piece that will move to block
//     b[0][5]  = -1;

//     // extra pieces for no force
//     b[1][7]  = -2;  

//     // pieces getting blocked
//     b[0][7]  = 4;

//     DisplayBoard(b);

//     auto rc = StartOfTurn(black, b, /*d1*/3, /*d2*/2);

//     PrintBoard();
//     DispErrorCode(rc);
//     ASSERT_EQ(rc, status_codes::SUCCESS)
//         << "Doubles (3,3) should not force a move when many options exist";

//     ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {0,5})),
//               status_codes::SUCCESS)
//         << "Must be able to pick start (0,5)";

//     // two-step to (1,2) wraps around; that creates 9–11,0–2 all white
//     auto status = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 10}));
//     EXPECT_NE(status, status_codes::SUCCESS)
//         << "Wrapping blockade of 6 across rows must be rejected";
// }
