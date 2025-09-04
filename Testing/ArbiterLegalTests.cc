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
    ASSERT_EQ(ReceiveCommand(Command(0,0)), status_codes::SUCCESS)
        << "could not select head";
    ASSERT_EQ(ReceiveCommand(Command(0, 7)), status_codes::DEST_ENEMY)
        << "second step should be DEST_ENEMY";

    ASSERT_EQ(ReceiveCommand(Command(0, 1)), status_codes::SUCCESS)
        << "could not select 0, 1";
    ASSERT_EQ(ReceiveCommand(Command(0, 8)), status_codes::DEST_ENEMY)
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
    ASSERT_EQ(ReceiveCommand(Command(0,1)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(first)), status_codes::SUCCESS);

    // try to re-use die-0 on another piece
    ASSERT_EQ(ReceiveCommand(Command(0,1)), status_codes::SUCCESS);
    EXPECT_NE(ReceiveCommand(Command(first)), status_codes::SUCCESS)
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
    ASSERT_EQ(ReceiveCommand(Command(0,0)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(first)),     status_codes::SUCCESS);

    // move second pile by 3
    ASSERT_EQ(ReceiveCommand(Command(0,2)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(second)), status_codes::NO_LEGAL_MOVES_LEFT);

    // further move should yield NO_LEGAL_MOVES_LEFT
    EXPECT_NE(ReceiveCommand(Command(first)), status_codes::SUCCESS)
        << "after both dice used, turn over";
}

TEST_F(TestBuilder, DoublesLegalMove)
{
    auto brd = start_brd;
    brd[0][0] = 14; brd[0][1] = -1; brd[0][3] = -1;
    brd[1][0] = -13; brd[1][2] = 1;

    StartOfTurn(white, brd, 5, 5);
    auto rc = ReceiveCommand(Command(0, 0));
    ASSERT_EQ(rc, status_codes::SUCCESS);

    rc = ReceiveCommand((1, 3));
    std::cout << "weirdness...\n\n\n\n";
    DispErrorCode(rc);
    ASSERT_EQ(rc, status_codes::SUCCESS);
}






TEST_F(TestBuilder, EndgameWeirdness)
{
    boardConfig seg = {{ {0, 0,	0, 0, 0,-1, 0,-1, 0, 0, 0, 0},
    	                 {0, 0,	0, 0, 0, 0,	0, 0, 0, 0,	0, PIECES_PER_PLAYER} }};

    auto rc = StartOfTurn(white, seg, 4, 3);
    DispErrorCode(rc);
    EXPECT_EQ(rc, status_codes::SUCCESS);

    std::cout << "on to the second one :) \n\n\n";

    boardConfig at_zsh = {{ {0,	0,	0,	0,	0,	0,	0,	0,	-1,	0,	0,	0},	
                            {0,	0,	0,	0,	0,	1,	0,	1,	0,	0,	0,	0}}};

    rc = StartOfTurn(white, at_zsh, 6, 1);
    ASSERT_EQ(rc, status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(1, 5)), status_codes::SUCCESS) << "couldn't select start as in mini-game";
    rc = ReceiveCommand(Command(second));
    DispErrorCode(rc);
    EXPECT_EQ(rc, status_codes::SUCCESS);
}