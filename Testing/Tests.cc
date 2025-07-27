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

// globals

using board = std::array< std::array<int, COL>, ROW>;

                   //         0              1  2  3  4. 5. 6. 7. 8. 9. 10 11   
board start_brd = {{    { PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                        {-PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};

                       //     0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
board starts_check = {{     { 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0}, 
                            {-5, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};

                       //         0              1  2  3  4. 5. 6. 7. 8. 9. 10 11   
board preventions1 = {{ { PIECES_PER_PLAYER - 2, 0, 0,-1, 0, 0, 1,-1, 0, 0, 0, 1}, 
                        {-(PIECES_PER_PLAYER-2), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};

                       //         0              1  2  3  4. 5. 6. 7. 8. 9. 10 11   
board preventions2 = {{ { PIECES_PER_PLAYER - 3,-1, 0, 1, 0, 0,-1, 1,-1, 0, 0, 1}, 
                        {-(PIECES_PER_PLAYER-3), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};  

                   //     0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
board block_check1 = {{ {14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                        {-8, 0, 0,-1, 1,-1,-1,-1,-1, 0,-1,-1} }};

                   //     0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
board block_check2 = {{ {11, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0}, 
                        {-9, 0, 0, 0, 0,-1,-1,-1, 0,-1,-1,-1} }};

// helper: build an all-zero board with 1 black piece at its head
static std::array<std::array<int, COL>, ROW> ZeroWhite1BlackBoard() {
    std::array<std::array<int, COL>, ROW> b{};
    for (auto& r : b) r.fill(0);
    b[1][0] = -1;
    return b;
}

static std::array<std::array<int, COL>, ROW> SafeBoard() {
    std::array<std::array<int, COL>, ROW> b{};
    for (auto& r : b) r.fill(0);
    b[1][0] = -1;   // no game over
    b[1][1] = 1;    // to prevent forcing moves
    b[1][2] = 1;
    return b;
}

bool white = 0;
bool black = 1;

bool first = 0;
bool second = 1;

/*──────────────────────────────────────────────────────────────────────────────
  TESTS – Start-coordinate validation (model coordinates: (row,col) with 0,0
          = white head).  Each test uses StartOfTurn(), which *immediately*
          applies onRoll() and any forced moves, then returns that status.
──────────────────────────────────────────────────────────────────────────────*/


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

static std::array<std::array<int, COL>, ROW> HeadScenarioBoard()
{
    /* minimal board:
         white head 5 pieces @ col 0
         extra white piles @ col 3,5 so we can move without ending turn
         black head @ (1,0) but otherwise empty
    */
    std::array<std::array<int, COL>, ROW> b{};
    for (auto& r : b) r.fill(0);
    b[0][0] = 5;    // white head
    b[0][3] = 2;
    b[0][5] = 1;
    b[1][0] = -5;   // black head
    return b;
}

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

    /* -- Move #2 : other pile (0,3 → 0,7) (die idx 1) ---------------------- */
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,3}));
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Cannot select pile (0,3)";

    rc = ReceiveCommand(Command(Actions::MOVE_BY_DICE, 1));   // use second 4
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Move #2 failed";

    /* -- Move #3 : pile (0,5 → 0,9)  (die idx 0 again) --------------------- */
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,5}));
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Cannot select pile (0,5)";

    rc = ReceiveCommand(Command(Actions::MOVE_BY_DICE, first));   // third 4
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Move #3 failed";

    /* -- Move #4 : try head again → should be blocked ---------------------- */
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0}));
    EXPECT_EQ(rc, status_codes::HEAD_PLAYED_ALREADY)
        << "Head reused on 4-th move (doubles) but not flagged";
}

// ───────────────────────── 4. Misc start and force - mini-game ──────────────────────────

TEST_F(TestBuilder, StartSelectCheck)
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
  3. Move onto enemy pile (illegal)
──────────────────────────────────────────────────────────────────────────────*/
TEST_F(TestBuilder, Move_OntoEnemyPiece)
{
    auto brd = SafeBoard();
    brd[0][0] = 1;    // white
    brd[0][4] = -1;   // enemy pile

    ASSERT_EQ(StartOfTurn(white, brd, 4, 5), status_codes::SUCCESS);

    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {0,0}}), status_codes::SUCCESS);
    EXPECT_EQ(ReceiveCommand({Actions::MOVE_BY_DICE, first}),     status_codes::DEST_ENEMY)
        << "landing on enemy pile must be DEST_ENEMY";
}

/*──────────────────────────────────────────────────────────────────────────────
  4. Move that passes end of board (white 0,10 by 3 steps → 1,? out-of-range)
──────────────────────────────────────────────────────────────────────────────*/
TEST_F(TestBuilder, Move_PastEndOfBoard)
{
    auto brd = SafeBoard();
    brd[1][10] = 1;   // piece near end
    brd[0][5]  = 1;   // second piece ensures no forced moves

    ASSERT_EQ(StartOfTurn(white, brd, 3, 5), status_codes::SUCCESS);

    ASSERT_EQ(ReceiveCommand({Actions::SELECT_SLOT, {1,10}}), status_codes::SUCCESS);
    auto rc = ReceiveCommand({Actions::MOVE_BY_DICE, first});

    DispErrorCode(rc);
    EXPECT_NE(rc, status_codes::SUCCESS) << "move that crosses board end should be OUT_OF_BOUNDS or BOARD_END_REACHED";
}

/*──────────────────────────────────────────────────────────────────────────────
  6. Doubles forced-move: only one 4-move exists → onRoll resolves, no moves left
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
}

/*──────────────────────────────────────────────────────────────────────────────
  7. Doubles first move weirdness
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
  status_codes outcome = StartOfTurn(white, preventions1, 1, 2);
  ASSERT_EQ(outcome, status_codes::SUCCESS) << "forced all on roll - preventions1";

  outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0}));
  ASSERT_EQ(outcome, status_codes::SUCCESS) << "unable to select head - preventions1";

  outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 2}));
  EXPECT_EQ(outcome, status_codes::PREVENTS_COMPLETION);

  preventions1[0][6] = 0;
  outcome = StartOfTurn(white, preventions1, 1, 2);
  EXPECT_EQ(outcome, status_codes::SUCCESS);
  EXPECT_EQ(GetBoardAt(1, 1), 1) << "did not partially force move, head->2 will prevent";

  outcome = StartOfTurn(white, preventions2, 1, 2);
  ASSERT_EQ(outcome, status_codes::SUCCESS) << "forced on roll - preventions2";

  outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 3}));
  ASSERT_EQ(outcome, status_codes::SUCCESS) << "unable to select start - preventions2";

  outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 5}));
  EXPECT_EQ(outcome, status_codes::PREVENTS_COMPLETION) << "should be no available 1 moves";
}


/*──────────────────────────────────────────────────────────────────────────────
  9. Happy-path: two legal moves consume both dice cleanly
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

// ──────────────────────────────────────────────────────────────────────────────
// BLOCKADE RULE TESTS – manual 2-step moves that would create a 6-point blockade
// (no enemy piece beyond point 6 in a row), EXPECT_NE(status, SUCCESS)
// ──────────────────────────────────────────────────────────────────────────────

/*
  Case 1: Simple front-row blockade.
    – Dice = (2,4) → sum = 6.
    – Board has white checkers at points 1,2,3,4,5 (five in a row).
    – A head checker at 0 (so start = {0,0}) can move 6 to {0,6}, which would make points 1–6 all white
      with no enemy at 7.
*/
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