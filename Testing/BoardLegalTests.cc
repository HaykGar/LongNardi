#include <gtest/gtest.h>
#include "Testing.h"

// ───────────────────────── 1. invalid start selections ───────────────────────
TEST_F(TestBuilder, StartSelect_InvalidStarts)
{
    auto brd = starts_check;

    DisplayBoard(brd);

    auto rc = StartOfTurn(white, brd, /*d1*/2, /*d2*/3);
    PrintBoard();

    DispErrorCode(rc);

    ASSERT_EQ(rc, status_codes::SUCCESS)
                << "StartOfTurn should succeed, no forced moves";

    // 1-A  empty slot
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,2} ));
    EXPECT_EQ(rc, status_codes::START_EMPTY_OR_ENEMY)
        << "Selecting empty slot (0,2) did not return START_EMPTY_OR_ENEMY";

    // 1-B  enemy pile
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {1,0}));
    EXPECT_EQ(rc, status_codes::START_EMPTY_OR_ENEMY)
        << "Enemy pile (black head) should be rejected";

    // 1-C  column out of range
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, COL}));
    EXPECT_EQ(rc, status_codes::OUT_OF_BOUNDS)
        << "Column == COL should yield OUT_OF_BOUNDS";

    // 1-D  row out of range
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {2, 0}));
    EXPECT_EQ(rc, status_codes::OUT_OF_BOUNDS)
        << "Row 2 should yield OUT_OF_BOUNDS (only 0 & 1 valid)";
}

// ───────────────────────── 2. multiple valid starts ──────────────────────────
TEST_F(TestBuilder, StartSelect_MultipleLegalStarts)
{
    auto brd = ZeroWhite1BlackBoard();
    brd[0][0] = 5;   // white head
    brd[0][3] = 2;   // another legal pile
    brd[0][5] = 1;   // yet another
    brd[1][0] = -15;  // game not over

    DisplayBoard(brd);

    ASSERT_EQ(StartOfTurn(white, brd, 2, 4),
              status_codes::SUCCESS)
              << "No forced moves expected on (2,4) roll";

    // 2-A  legal start at column 3
    auto rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,3}));
    ASSERT_EQ(rc, status_codes::SUCCESS)
        << "Valid pile (0,3) should be selectable";
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,3}));

    ASSERT_NE(rc, status_codes::SUCCESS) << "should not be able to re-select";  // no path anyway as no dice combo = 0

    // 2-B  legal re-select head (allowed after previous cleared)
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0}));
    ASSERT_EQ(rc, status_codes::SUCCESS)
        << "Head (0,0) should be selectable after clearing previous start";

    // 2-C  selecting head again without moving
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0}));
    EXPECT_NE(rc, status_codes::SUCCESS)
        << "Re-selecting same start without move should return NO_PATH";
}

// ───────────────────────── 3. Head Reuse ──────────────────────────



/* Head reused on ordinary roll (two dice) */
TEST_F(TestBuilder, HeadReuse_NormalRoll)
{
    auto brd = HeadScenarioBoard();

    // dice 1 and 2 → both moves possible without ending turn
    ASSERT_EQ(StartOfTurn(white, brd, /*d1=*/1, /*d2=*/2), status_codes::SUCCESS)
        << "StartOfTurn should succeed (no forced moves)";

    // --- Move #1: from head using die index 0 (value 1) ---------------------
    auto rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0}));
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Unable to select head for first move";

    rc = ReceiveCommand(Command(Actions::MOVE_BY_DICE, first));   // use die 0 (=1)
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Head move failed with die 1";

    // --- Attempt to select head again (should be blocked) -------------------
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0}));
    EXPECT_EQ(rc, status_codes::HEAD_PLAYED_ALREADY)
        << "Head reused in same turn but did NOT return HEAD_PLAYED_ALREADY";
}

/* Head reused on doubles (four moves) */
TEST_F(TestBuilder, HeadReuse_DoublesFourthMove)
{
    auto brd = HeadScenarioBoard();
    std::cout << "starting board... \n\n";
    DisplayBoard(brd);

    
    auto rc = StartOfTurn(white, brd, 4, 4);
    PrintBoard();
    DispErrorCode(rc);
    // doubles 4-4 → 4 moves this turn
    ASSERT_EQ(rc, status_codes::SUCCESS)
        << "StartOfTurn (4,4) should succeed";

    /* -- Move #1 : head piece from 0 to 4 (die idx 0) ---------------------- */
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0}));
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Cannot select head for move #1";

    rc = ReceiveCommand(Command(Actions::MOVE_BY_DICE, first));   // use first 4
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Move #1 from head failed";

    PrintBoard();

    /* -- Move #2 : other pile (0,3 → 0,7) (die idx 1) ---------------------- */
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,3}));
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Cannot select pile (0,3)";

    rc = ReceiveCommand(Command(Actions::MOVE_BY_DICE, 1));   // use second 4
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Move #2 failed";

    PrintBoard();

    /* -- Move #3 : pile (0,5 → 0,9)  (die idx 0 again) --------------------- */
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,5}));
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Cannot select pile (0,5)";

    rc = ReceiveCommand(Command(Actions::MOVE_BY_DICE, first));   // third 4
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Move #3 failed";

    PrintBoard();

    /* -- Move #4 : try head again → should be blocked ---------------------- */
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0}));
    EXPECT_EQ(rc, status_codes::HEAD_PLAYED_ALREADY)
        << "Head reused on 4-th move (doubles) but not flagged";
}

// ───────────────────────── 4. Well Def End ──────────────────────────

TEST_F(TestBuilder, MoveOntoEnemyPiece)
{
    auto brd = SafeBoard();
    brd[0][0] = 1;    // white
    brd[0][4] = -1;   // enemy pile

    ASSERT_EQ(StartOfTurn(white, brd, 4, 5), status_codes::SUCCESS);

    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0,0}}), status_codes::SUCCESS);
    EXPECT_EQ(ReceiveCommand({Actions::MOVE_BY_DICE, first}),     status_codes::DEST_ENEMY)
        << "landing on enemy pile must be DEST_ENEMY";

    NardiBoard brd2(board_legal);

    EXPECT_EQ(brd2.WellDefinedEnd({0, 0}, {0, 11}), status_codes::DEST_ENEMY);
    
    brd2.SwitchPlayer();

    EXPECT_EQ(brd2.WellDefinedEnd({1, 0}, {1, 11}), status_codes::DEST_ENEMY);
}

TEST_F(TestBuilder, OutOfBounds)
{
    auto brd = SafeBoard();
    brd[1][10] = 1;   // piece near end
    brd[0][5]  = 1;   // second piece ensures no forced moves

    ASSERT_EQ(StartOfTurn(white, brd, 3, 5), status_codes::SUCCESS);

    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {1,10}}), status_codes::SUCCESS);
    auto rc = ReceiveCommand({Actions::MOVE_BY_DICE, first});

    DispErrorCode(rc);
    EXPECT_NE(rc, status_codes::SUCCESS) << "move that crosses board end should be OUT_OF_BOUNDS";
    
    NardiBoard brd2(board_legal);

    EXPECT_EQ(brd2.WellDefinedEnd({0, 0}, {0, COL}), status_codes::OUT_OF_BOUNDS);   // past end to right
    EXPECT_EQ(brd2.WellDefinedEnd({0, 0}, {1, -1}), status_codes::OUT_OF_BOUNDS);    // past end to left

    EXPECT_EQ(brd2.WellDefinedEnd({0, 0}, {1, 3}), status_codes::SUCCESS);           // valid row change
    EXPECT_EQ(brd2.WellDefinedEnd({1, 10}, {0, 0}), status_codes::OUT_OF_BOUNDS);    // bad row change for white
    brd2.SwitchPlayer();                                                             // switch to black
    EXPECT_EQ(brd2.WellDefinedEnd({1, 0}, {0, 3}), status_codes::SUCCESS);           // valid row change
    EXPECT_EQ(brd2.WellDefinedEnd({0, 11}, {1, 0}), status_codes::OUT_OF_BOUNDS);    // bad row change black
}


TEST(WellDefEnd, BackwardsMove)
{
    NardiBoard brd(board_legal);
    EXPECT_EQ(brd.WellDefinedEnd({0, 2}, {0, 1}), status_codes::BACKWARDS_MOVE);

    brd.SwitchPlayer();

    EXPECT_EQ(brd.WellDefinedEnd({0, 11}, {0, 4}), status_codes::BACKWARDS_MOVE);
}

TEST(WellDefEnd, Success)
{
    NardiBoard brd(board_legal);

    // dest empty
    EXPECT_EQ(brd.WellDefinedEnd({0, 0}, {1, 3}), status_codes::SUCCESS);
    // dest friently
    EXPECT_EQ(brd.WellDefinedEnd({0, 0}, {1, 4}), status_codes::SUCCESS);

    brd.SwitchPlayer();

    EXPECT_EQ(brd.WellDefinedEnd({1, 0}, {0, 3}), status_codes::SUCCESS);
    EXPECT_EQ(brd.WellDefinedEnd({1, 5}, {1, 7}), status_codes::SUCCESS);
}


////////////////////////////////////////////////////////////////////////////////////////////////
// Calculators
////////////////////////////////////////////////////////////////////////////////////////////////

TEST(Calculators, CoordAfterDist)
{
    NardiBoard brd(start_brd);
    int d = 0;

    NardiCoord head_w(0, 0);
    NardiCoord head_b(1, 0);
    
    for(d; d < COL; ++d)
        EXPECT_EQ(brd.CoordAfterDistance(head_w, d), NardiCoord(0, d) );

    EXPECT_EQ(brd.CoordAfterDistance(head_w, d),  head_b);

    for(d = 0; d < COL; ++d)
        EXPECT_EQ(brd.CoordAfterDistance(head_b, d), NardiCoord(1, d));

    EXPECT_EQ(brd.CoordAfterDistance(head_b, d), NardiCoord(1, 12));
    // check negatives... `

    NardiCoord end_w(1, 11);
    NardiCoord end_b(0, 11);

    for(d = 0; d < COL; ++d)
        EXPECT_EQ(brd.CoordAfterDistance(end_w, -d), NardiCoord(1, 11-d) );

    EXPECT_EQ(brd.CoordAfterDistance(end_w, -d), NardiCoord(0, 11));


    for(d = 0; d < COL; ++d)
        EXPECT_EQ(brd.CoordAfterDistance(end_b, -d), NardiCoord(0, 11-d) );

    EXPECT_EQ(brd.CoordAfterDistance(end_b, -d), NardiCoord(0, -1) );

    brd.SwitchPlayer(); // same with black now

    for(d = 0; d < COL; ++d)
        EXPECT_EQ(brd.CoordAfterDistance(head_b, d), NardiCoord(1, d));

    EXPECT_EQ(brd.CoordAfterDistance(head_b, d), head_w );

    for(d; d < COL; ++d)
        EXPECT_EQ(brd.CoordAfterDistance(head_w, d), NardiCoord(0, d) );

    EXPECT_EQ(brd.CoordAfterDistance(head_w, d), NardiCoord(0, 12));
    
    // check negatives... `

    for(d = 0; d < COL; ++d)
        EXPECT_EQ(brd.CoordAfterDistance(end_b, -d), NardiCoord(0, 11-d) );

    EXPECT_EQ(brd.CoordAfterDistance(end_b, -d), NardiCoord(1, 11));

    for(d = 0; d < COL; ++d)
        EXPECT_EQ(brd.CoordAfterDistance(end_w, -d), NardiCoord(1, 11-d) );

    EXPECT_EQ(brd.CoordAfterDistance(end_w, -d), NardiCoord(1, -1) );
}

TEST(Calculators, GetDistance)
{
    NardiBoard brd = SafeBoard();
    EXPECT_EQ( brd.GetDistance({0, 0}, {0, 9}), 9 );        // same row forward
    EXPECT_EQ( brd.GetDistance({0, 11}, {0, 9}), -2 );      // same row backward

    // white row changing
    EXPECT_EQ( brd.GetDistance({0, 3}, {1, 3}), 12 );       // row change forward
    EXPECT_EQ( brd.GetDistance({1, 1}, {0, 7}), -6 );       // row change backward
    // black row change
    brd.SwitchPlayer();
    EXPECT_EQ( brd.GetDistance({0, 0}, {1, 3}), -9 );       // backward
    EXPECT_EQ( brd.GetDistance({1, 1}, {0, 6}),  17);       // forward
}



