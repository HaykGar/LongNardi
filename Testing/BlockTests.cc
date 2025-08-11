#include <gtest/gtest.h>
#include "Testing.h"


TEST_F(TestBuilder, BlockadeHomeEntry)
{
    auto brd = block_check1;

    DisplayBoard(brd);

    auto rc = StartOfTurn(black, brd, 3, 6);
    DispErrorCode(rc);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    PrintBoard();
    ASSERT_EQ( ReceiveCommand(Command(Actions::SELECT_SLOT, {1,3})) , status_codes::SUCCESS ) << "can't get start";

    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {1,9}));

    ASSERT_EQ( rc , status_codes::SUCCESS ) << "temporary blockage should be ok";

    StatusReport();

    rc =  ReceiveCommand(Command(Actions::SELECT_SLOT, {1,0}));

    StatusReport();

    ASSERT_EQ( rc, status_codes::SUCCESS ); 

    ASSERT_NE( ReceiveCommand(Command(Actions::MOVE_BY_DICE,  0  )) , status_codes::SUCCESS ) << "need to lift blockage";

    ASSERT_EQ( ReceiveCommand(Command(Actions::SELECT_SLOT, {1,6})) , status_codes::SUCCESS ) << "can't get start";
    ASSERT_EQ( ReceiveCommand(Command(Actions::MOVE_BY_DICE,  0  )) , status_codes::NO_LEGAL_MOVES_LEFT ) << "should lift blockage and end turn";

    DisplayBoard(brd);
    rc = StartOfTurn(black, brd, 3, 6);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    ASSERT_EQ( ReceiveCommand(Command(Actions::SELECT_SLOT, {1,3})) , status_codes::SUCCESS ) << "can't get start";
    ASSERT_EQ( ReceiveCommand(Command(Actions::MOVE_BY_DICE, 1)) , status_codes::SUCCESS ) << "temporary blockage should be ok";

    ASSERT_EQ( ReceiveCommand(Command(Actions::SELECT_SLOT, {1, 6})) , status_codes::SUCCESS ) << "can't get start";
    ASSERT_EQ( ReceiveCommand(Command(Actions::MOVE_BY_DICE,  first  )) , status_codes::NO_LEGAL_MOVES_LEFT ) << "should lift blockage and end turn";

    DisplayBoard(brd);

    rc = StartOfTurn(black, brd, 3, 6);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    // Select (1,3), should be success
    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {1,3})), status_codes::SUCCESS);
    // Move by dice second (6)
    ASSERT_EQ(ReceiveCommand(Command(Actions::MOVE_BY_DICE, second)), status_codes::SUCCESS);   // create blockage

    // Select slot (1,11), try to move by dice first (3), should NOT be success
    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {1,11})), status_codes::SUCCESS);
    EXPECT_NE(ReceiveCommand(Command(Actions::MOVE_BY_DICE, first)), status_codes::SUCCESS);

    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {1, 10})), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(Actions::MOVE_BY_DICE, first)), status_codes::NO_LEGAL_MOVES_LEFT);
    
    // Select any piece in 1,6 to 1,10 except 1,9, assert success and move by dice second, assert no legal moves left
    for (int col = 6; col <= 8; ++col) {
        rc = StartOfTurn(black, brd, 3, 6);
        ASSERT_EQ(rc, status_codes::SUCCESS);

        // Select (1,3), should be success
        ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {1,3})), status_codes::SUCCESS);
        // Move by dice second (6)
        ASSERT_EQ(ReceiveCommand(Command(Actions::MOVE_BY_DICE, second)), status_codes::SUCCESS);   // create blockage


        ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {1, col})), status_codes::SUCCESS);
        ASSERT_EQ(ReceiveCommand(Command(Actions::MOVE_BY_DICE, first)), status_codes::NO_LEGAL_MOVES_LEFT); // unblock
    }
}

TEST_F(TestBuilder, BlockadeUnblockOnlyBySpecificMove)
{
    auto brd = block_check2;

    auto rc = StartOfTurn(black, brd, 3, 6);
    ASSERT_EQ(rc, status_codes::SUCCESS);
    StatusReport();

    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {1,5})), status_codes::SUCCESS);

    rc = ReceiveCommand(Command(Actions::MOVE_BY_DICE, first));
    StatusReport();

    std::cout << "rc was\n";
    DispErrorCode(rc);

    ASSERT_EQ(rc, status_codes::NO_LEGAL_MOVES_LEFT);
    
    ASSERT_EQ(GetBoardAt(0,5), -1);
}

TEST_F(TestBuilder, BlockadeRowWrap)
{
    std::cout << "\n\n\nblock wrap\n\n\n";
    auto brd = block_wrap1;

    // Test with wrap_dice1 = {1, 6}
    auto rc = StartOfTurn(white, brd, wrap_dice1[0], wrap_dice1[1]);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    // Select (1,8) and move by dice first (1) to get to (1,9), forming a blocked position
    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {1,8})), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(Actions::MOVE_BY_DICE, first)), status_codes::SUCCESS);

    // Try to move second dice (6) from (0,0) - should NOT succeed due to blockade
    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0})), status_codes::SUCCESS);
    EXPECT_NE(ReceiveCommand(Command(Actions::MOVE_BY_DICE, second)), status_codes::SUCCESS)
        << "Move from (0,0) with second dice should be blocked by row wrap blockade";

    // Test with wrap_dice2 = {1, 3}
    rc = StartOfTurn(white, brd, wrap_dice2[0], wrap_dice2[1]);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    // Select (1,8) and move by dice first (1) to get to (1,9), forming a blocked position
    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {1,8})), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(Actions::MOVE_BY_DICE, first)), status_codes::SUCCESS);

    // Try to move second dice (3) from (0,0) - should NOT succeed due to blockade
    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0})), status_codes::SUCCESS);
    EXPECT_NE(ReceiveCommand(Command(Actions::MOVE_BY_DICE, second)), status_codes::NO_LEGAL_MOVES_LEFT)
        << "Move from (0,0) with second dice should be blocked by row wrap blockade";

    // Test with wrap_dice3 = {1, 4}
    rc = StartOfTurn(white, brd, wrap_dice3[0], wrap_dice3[1]);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    StatusReport();

    // Select (1,8) and move by dice first (1) to get to (1,9), forming a blocked position
    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {1,8})), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(Actions::MOVE_BY_DICE, first)), status_codes::SUCCESS);

    StatusReport();

    // Try to move second dice (4) from (0,0) - should NOT succeed due to blockade
    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0})), status_codes::SUCCESS);
    EXPECT_NE(ReceiveCommand(Command(Actions::MOVE_BY_DICE, second)), status_codes::SUCCESS)
        << "Move from (0,0) with second dice should be blocked by row wrap blockade";

    StatusReport();

    // Finally, select (0,2) and move by second dice to get a successful unblock
    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {0,2})), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(Actions::MOVE_BY_DICE, second)), status_codes::NO_LEGAL_MOVES_LEFT)
        << "Move from (0,2) with second dice should successfully unblock and end turn";
}