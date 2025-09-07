#include <gtest/gtest.h>
#include "Testing.h"

TEST_F(TestBuilder, BlockadeHomeEntry)
{    
    auto& brd = block_check1;

    DisplayBoard(brd);

    auto rc = StartOfTurn(black, brd, 3, 6);
    DispErrorCode(rc);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    PrintBoard();
    ASSERT_EQ(ReceiveCommand(Command(1,3)), status_codes::SUCCESS ) << "can't get start";

    rc = ReceiveCommand(Command(1,9));

    ASSERT_EQ( rc , status_codes::SUCCESS ) << "temporary blockage should be ok";

    StatusReport();

    rc =  ReceiveCommand(Command(1,0));

    StatusReport();

    ASSERT_EQ( rc, status_codes::SUCCESS ); 

    ASSERT_NE( ReceiveCommand(Command(first)) , status_codes::SUCCESS ) << "need to lift blockage";

    ASSERT_EQ( ReceiveCommand(Command(1,6)) , status_codes::SUCCESS ) << "can't get start";
    ASSERT_EQ( ReceiveCommand(Command(first)) , status_codes::NO_LEGAL_MOVES_LEFT ) << "should lift blockage and end turn";

    DisplayBoard(brd);
    rc = StartOfTurn(black, brd, 3, 6);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    ASSERT_EQ( ReceiveCommand(Command(1,3)) , status_codes::SUCCESS ) << "can't get start";
    ASSERT_EQ( ReceiveCommand(Command(second)) , status_codes::SUCCESS ) << "temporary blockage should be ok";

    ASSERT_EQ( ReceiveCommand(Command(1, 6)) , status_codes::SUCCESS ) << "can't get start";
    ASSERT_EQ( ReceiveCommand(Command(first)) , status_codes::NO_LEGAL_MOVES_LEFT ) << "should lift blockage and end turn";

    DisplayBoard(brd);

    std::cout << "problematic part\n\n";

    rc = StartOfTurn(black, brd, 3, 6);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    StatusReport();

    // Select (1,3), should be success
    ASSERT_EQ(ReceiveCommand(Command(1,3)), status_codes::SUCCESS);
    // Move by dice second (6)
    ASSERT_EQ(ReceiveCommand(Command(second)), status_codes::SUCCESS);   // create blockage

    StatusReport();

    // Select slot (1,11), try to move by dice first (3), should NOT be success
    ASSERT_EQ(ReceiveCommand(Command(1,11)), status_codes::SUCCESS);
    EXPECT_NE(ReceiveCommand(Command(first)), status_codes::SUCCESS);

    ASSERT_EQ(ReceiveCommand(Command(1, 10)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(first)), status_codes::NO_LEGAL_MOVES_LEFT);
    
    // Select any piece in 1,6 to 1,10 except 1,9, assert success and move by dice second, assert no legal moves left
    for (int col = 6; col <= 8; ++col) {
        rc = StartOfTurn(black, brd, 3, 6);
        ASSERT_EQ(rc, status_codes::SUCCESS);

        // Select (1,3), should be success
        ASSERT_EQ(ReceiveCommand(Command(1,3)), status_codes::SUCCESS);
        // Move by dice second (6)
        ASSERT_EQ(ReceiveCommand(Command(second)), status_codes::SUCCESS);   // create blockage


        ASSERT_EQ(ReceiveCommand(Command(1, col)), status_codes::SUCCESS);
        ASSERT_EQ(ReceiveCommand(Command(first)), status_codes::NO_LEGAL_MOVES_LEFT); // unblock
    }
}

TEST_F(TestBuilder, BlockadeUnblockOnlyBySpecificMove)
{
    auto brd = block_check2;

    auto rc = StartOfTurn(black, brd, 3, 6);
    ASSERT_EQ(rc, status_codes::SUCCESS);
    StatusReport();

    ASSERT_EQ(ReceiveCommand(Command(1,5)), status_codes::SUCCESS);

    rc = ReceiveCommand(Command(first));
    StatusReport();

    std::cout << "rc was\n";
    DispErrorCode(rc);

    ASSERT_EQ(ReceiveCommand(Command(1, 11)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(second)), status_codes::NO_LEGAL_MOVES_LEFT);
}

TEST_F(TestBuilder, BlockadeRowWrap)
{
    std::cout << "\n\n\nblock wrap\n\n\n";
    auto brd = block_wrap1;

    // Test with wrap_dice1 = {1, 6}
    auto rc = StartOfTurn(white, brd, wrap_dice1[0], wrap_dice1[1]);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    // Select (1,8) and move by dice first (1) to get to (1,9), forming a blocked position
    ASSERT_EQ(ReceiveCommand(Command(1,8)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(first)), status_codes::SUCCESS);

    // Try to move second dice (6) from (0,0) - should NOT succeed due to blockade
    ASSERT_EQ(ReceiveCommand(Command(0,0)), status_codes::SUCCESS);
    EXPECT_NE(ReceiveCommand(Command(second)), status_codes::SUCCESS)
        << "Move from (0,0) with second dice should be blocked by row wrap blockade";

    // Test with wrap_dice2 = {1, 3}
    rc = StartOfTurn(white, brd, wrap_dice2[0], wrap_dice2[1]);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    // Select (1,8) and move by dice first (1) to get to (1,9), forming a blocked position
    ASSERT_EQ(ReceiveCommand(Command(1,8)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(first)), status_codes::SUCCESS);

    // Try to move second dice (3) from (0,0) - should NOT succeed due to blockade
    ASSERT_EQ(ReceiveCommand(Command(0,0)), status_codes::SUCCESS);
    EXPECT_NE(ReceiveCommand(Command(second)), status_codes::NO_LEGAL_MOVES_LEFT)
        << "Move from (0,0) with second dice should be blocked by row wrap blockade";

    // Test with wrap_dice3 = {1, 4}
    rc = StartOfTurn(white, brd, wrap_dice3[0], wrap_dice3[1]);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    StatusReport();

    // Select (1,8) and move by dice first (1) to get to (1,9), forming a blocked position
    ASSERT_EQ(ReceiveCommand(Command(1,8)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(first)), status_codes::SUCCESS);

    StatusReport();

    // Try to move second dice (4) from (0,0) - should NOT succeed due to blockade
    ASSERT_EQ(ReceiveCommand(Command(0,0)), status_codes::SUCCESS);
    EXPECT_NE(ReceiveCommand(Command(second)), status_codes::SUCCESS)
        << "Move from (0,0) with second dice should be blocked by row wrap blockade";

    StatusReport();

    // Finally, select (0,2) and move by second dice to get a successful unblock
    ASSERT_EQ(ReceiveCommand(Command(0,2)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(second)), status_codes::NO_LEGAL_MOVES_LEFT)
        << "Move from (0,2) with second dice should successfully unblock and end turn";

    brd = block_wrap1;
    brd[0][11] = -1;
    ++brd[1][0];

    // Test with wrap_dice1 = {1, 6}
    rc = StartOfTurn(white, brd, wrap_dice1[0], wrap_dice1[1]);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    // Select (1,8) and move by dice first (1) to get to (1,9), forming a blocked position with piece ahead
    ASSERT_EQ(ReceiveCommand(Command(1,8)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(first)), status_codes::SUCCESS);

    ASSERT_EQ(ReceiveCommand(Command(0,0)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(second)), status_codes::NO_LEGAL_MOVES_LEFT);   // should be no bad blockade now
}

TEST_F(TestBuilder, UnliftableWithDoubles)
{
    auto brd = block_doub1;

    DisplayBoard(brd);
    
    auto rc = StartOfTurn(black, brd, 3, 3);
    DispErrorCode(rc);

    ASSERT_EQ(rc, status_codes::SUCCESS);
    ASSERT_EQ(brd, GetBoard());

    ASSERT_EQ(ReceiveCommand(Command(1,2)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(1,8)), status_codes::SUCCESS); // move to create block

    StatusReport();

    ASSERT_EQ(ReceiveCommand(Command(1,5)), status_codes::SUCCESS);
    ASSERT_NE(ReceiveCommand(Command(first)),status_codes::SUCCESS);
}

TEST_F(TestBuilder, BlockEndOfTurn)
{
    BoardConfig blockUnblock = {{   {3, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0}, 
                                    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,-1,-1}}};

    StartOfTurn(white, blockUnblock, 5, 1);

    auto rc = ReceiveCommand(Command(0,3));
    ASSERT_EQ(rc, status_codes::SUCCESS);
    rc = ReceiveCommand(Command(second));
    ASSERT_EQ(rc, status_codes::SUCCESS);

    rc = ReceiveCommand(Command(0,0));
    ASSERT_EQ(rc, status_codes::SUCCESS);
    rc = ReceiveCommand(Command(first));
    ASSERT_EQ(rc, status_codes::BAD_BLOCK);

    StatusReport();
}

TEST_F(TestBuilder, BlockUnblock)
{
    BoardConfig blockUnblock = {{   {3, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0}, 
                                    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,-1,-1}}};

    StartOfTurn(white, blockUnblock, 5, 1);

    auto rc = ReceiveCommand(Command(0,0));
    ASSERT_EQ(rc, status_codes::SUCCESS);
    rc = ReceiveCommand(Command(first));
    ASSERT_EQ(rc, status_codes::SUCCESS);

    StatusReport();

    rc = ReceiveCommand(Command(0,5));
    ASSERT_EQ(rc, status_codes::SUCCESS);
    rc = ReceiveCommand(Command(second));
    ASSERT_EQ(rc, status_codes::NO_LEGAL_MOVES_LEFT);
}

TEST_F(TestBuilder, Mars)
{
    BoardConfig mars = {{   {3, 2, 2, 2, 2, 0,-1, 0, 0, 0, 0, 0}, 
                            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,-1,-1}}};

    StartOfTurn(white, mars, 5, 1);

    auto rc = ReceiveCommand(Command(0,0));
    ASSERT_EQ(rc, status_codes::SUCCESS);
    rc = ReceiveCommand(Command(first));
    ASSERT_EQ(rc, status_codes::SUCCESS);

    StatusReport();

    rc = ReceiveCommand(Command(0,3));
    ASSERT_EQ(rc, status_codes::SUCCESS);
    rc = ReceiveCommand(Command(second));
    ASSERT_EQ(rc, status_codes::NO_LEGAL_MOVES_LEFT);
}

TEST_F(TestBuilder, AutoplayMistake)
{
    BoardConfig brd = {{{ 1, 3, 1,-2, 4, 0,-1, 0, 0, 0, 0, 0},
                        {-2,-1, 6,-1,-2,-2, 0,-1,-1,-2, 0, 0}}};

    StartOfTurn(black, brd, 6, 1);

    auto rc = ReceiveCommand(Command(1,0));
    ASSERT_EQ(rc, status_codes::SUCCESS);
    rc = ReceiveCommand(Command(first));
    ASSERT_EQ(rc, status_codes::SUCCESS);   // move 0,0 -> 0,6 to form a block

    rc = ReceiveCommand(Command(1, 3));
    ASSERT_EQ(rc, status_codes::SUCCESS);
    rc = ReceiveCommand(Command(second));   // try to move 1,3 -> 1,4
    ASSERT_EQ(rc, status_codes::BAD_BLOCK);
}