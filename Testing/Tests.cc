#include <gtest/gtest.h>
#include "Testing.h"

// globals

std::array< std::array<int, COL>, ROW> start_brd = {{ { PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                                      {-PIECES_PER_PLAYER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};

bool white = 0;
bool black = 1;

// TEST_F(TestBuilder, StartSelectCheck)
// {
//   std::array<int, 2> dice = {6, 5};
//   status_codes outcome = StartOfTurn(white, start_brd, dice[0], dice[1]);
//   ASSERT_EQ(outcome, status_codes::NO_LEGAL_MOVES_LEFT) << "did not force all - white first move\n";

//   outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0}));  // player changed to black, dice not rolled
//   EXPECT_NE(outcome, status_codes::SUCCESS) << "selected white despite black turn pre roll";
//   EXPECT_EQ(outcome, status_codes::MISC_FAILURE) << "failed with unexpected return val";

//   outcome = ReceiveCommand(Command(Actions::ROLL_DICE));  // will complete all legal moves, pass turn to white
//   ASSERT_EQ(outcome, status_codes::NO_LEGAL_MOVES_LEFT) << "did not force all - black first move\n";

//   outcome = withDice(1, 2);
//   ASSERT_EQ(outcome, status_codes::SUCCESS) << "unexpected forced on second move";

//   outcome = ReceiveCommand(Command(Actions::MOVE_BY_DICE, 0));
//   EXPECT_EQ(outcome, status_codes::MISC_FAILURE) << "no start selected incorrectly handled"; 

//   // PrintBoard();
//   outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0})); // white select head
//   ASSERT_EQ(outcome, status_codes::SUCCESS) << "failure to select white head";

//   outcome = ReceiveCommand(Command(Actions::MOVE_BY_DICE, 0));  // move by first dice from head
//   ASSERT_EQ(outcome, status_codes::SUCCESS) << "failure to move from head on second move";

//   outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0})); // white select head
//   EXPECT_EQ(outcome, status_codes::HEAD_PLAYED_ALREADY) << "Reusing head";           // head already used, should not work
// }

// TEST_F(TestBuilder, FirstMoveDouble4Or6)
// {
//   withFirstTurn();
//   status_codes outcome = StartOfTurn(white, start_brd, 4, 4);
//   ASSERT_EQ(outcome, status_codes::NO_LEGAL_MOVES_LEFT) << "4 4 first move failed";
//   // PrintBoard();

//   ASSERT_NE(status_codes::SUCCESS, ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0})));
//   outcome = withDice(6, 6);
//   ASSERT_EQ(outcome, status_codes::NO_LEGAL_MOVES_LEFT) << "6 6 first move failed";
//   // PrintBoard();
// }
//                                                          //       0                1  2  3  4. 5. 6. 7. 8. 9. 10 11   
// std::array< std::array<int, COL>, ROW> preventions1 = {{  { PIECES_PER_PLAYER - 2, 0, 0,-1, 0, 0, 1,-1, 0, 0, 0, 1}, 
//                                                           {-(PIECES_PER_PLAYER-2), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};

//                                                          //       0                1  2  3  4. 5. 6. 7. 8. 9. 10 11   
// std::array< std::array<int, COL>, ROW> preventions2 = {{  { PIECES_PER_PLAYER - 3,-1, 0, 1, 0, 0,-1, 1,-1, 0, 0, 1}, 
//                                                           {-(PIECES_PER_PLAYER-3), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};                                                          


// TEST_F(TestBuilder, PreventsCompletion)
// {
//   status_codes outcome = StartOfTurn(white, preventions1, 1, 2);
//   ASSERT_EQ(outcome, status_codes::SUCCESS) << "forced on roll - preventions";

//   outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0}));
//   ASSERT_EQ(outcome, status_codes::SUCCESS) << "unable to select head - preventions1";

//   outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 2}));
//   EXPECT_EQ(outcome, status_codes::PREVENTS_COMPLETION);

//   outcome = StartOfTurn(white, preventions2, 1, 2);
//   ASSERT_EQ(outcome, status_codes::SUCCESS) << "forced on roll - preventions2";

//   outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 3}));
//   ASSERT_EQ(outcome, status_codes::SUCCESS) << "unable to select start - preventions2";

//   outcome = ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 5}));
//   EXPECT_EQ(outcome, status_codes::PREVENTS_COMPLETION) << "should be no available 1 moves";
// }

/*──────────────────────────────────────────────────────────────────────────────
  TESTS – Start-coordinate validation (model coordinates: (row,col) with 0,0
          = white head).  Each test uses StartOfTurn(), which *immediately*
          applies onRoll() and any forced moves, then returns that status.
──────────────────────────────────────────────────────────────────────────────*/

// helper: build an all-zero board
static std::array<std::array<int, COL>, ROW> ZeroWhite1BlackBoard() {
    std::array<std::array<int, COL>, ROW> b{};
    for (auto& r : b) r.fill(0);
    b[1][0] = -1;
    return b;
}

// ───────────────────────── 1. invalid start selections ───────────────────────
TEST_F(TestBuilder, StartSelect_InvalidStarts)
{
  auto brd = ZeroWhite1BlackBoard();
  brd[0][3] = 4;      // white pile we can legally select later
  brd[1][0] = -5;     // black head (enemy pile)

  auto rc = StartOfTurn(white, brd, /*d1*/2, /*d2*/3);
  ASSERT_EQ(rc, status_codes::SUCCESS)
            << "StartOfTurn should succeed (no forced moves possible)";

  // PrintBoard();

  // 1-A  empty slot
  rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,2} ));
  DispErrorCode(rc);
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


// ───────────────────────── 2. Head Reuse ──────────────────────────

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

    rc = ReceiveCommand(Command(Actions::MOVE_BY_DICE, 0));   // use die 0 (=1)
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Head move failed with die 1";

    // --- Attempt to select head again (should be blocked) -------------------
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0}));
    EXPECT_EQ(rc, status_codes::HEAD_PLAYED_ALREADY)
        << "Head reused in same turn but did NOT return HEAD_PLAYED_ALREADY";
}

/* 2.  Head reused on doubles (four moves) */
TEST_F(TestBuilder, HeadReuse_DoublesFourthMove)
{
    auto brd = HeadScenarioBoard();

    // doubles 4-4 → 4 moves this turn
    ASSERT_EQ(StartOfTurn(white, brd, 4, 4), status_codes::SUCCESS)
        << "StartOfTurn (4,4) should succeed";

    /* -- Move #1 : head piece from 0 to 4 (die idx 0) ---------------------- */
    auto rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0}));
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Cannot select head for move #1";

    rc = ReceiveCommand(Command(Actions::MOVE_BY_DICE, 0));   // use first 4
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Move #1 from head failed";

    /* -- Move #2 : other pile (0,3 → 0,7) (die idx 1) ---------------------- */
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,3}));
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Cannot select pile (0,3)";

    rc = ReceiveCommand(Command(Actions::MOVE_BY_DICE, 1));   // use second 4
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Move #2 failed";

    /* -- Move #3 : pile (0,5 → 0,9)  (die idx 0 again) --------------------- */
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,5}));
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Cannot select pile (0,5)";

    rc = ReceiveCommand(Command(Actions::MOVE_BY_DICE, 0));   // third 4
    ASSERT_EQ(rc, status_codes::SUCCESS) << "Move #3 failed";

    /* -- Move #4 : try head again → should be blocked ---------------------- */
    rc = ReceiveCommand(Command(Actions::SELECT_SLOT, {0,0}));
    EXPECT_EQ(rc, status_codes::HEAD_PLAYED_ALREADY)
        << "Head reused on 4-th move (doubles) but not flagged";
}
