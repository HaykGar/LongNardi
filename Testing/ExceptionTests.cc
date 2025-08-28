#include <gtest/gtest.h>
#include "Testing.h" 

/*──────────────────────────────────────────────────────────────────────────────
Doubles first move weirdness
──────────────────────────────────────────────────────────────────────────────*/

TEST_F(TestBuilder, FirstMoveDouble4Or6)
{
    withFirstTurn();
    status_codes outcome = StartOfTurn(white, start_brd, 4, 4);
    ASSERT_EQ(outcome, status_codes::SUCCESS) << "4 4 first move failed";

    const auto& g = GetGame();
    const auto& b2s = g.GetBoards2Seqs();

    EXPECT_EQ(b2s.size(), 1);

    std::string key = b2s.begin()->first;
    boardConfig expected = {{   { 13, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0},
                                {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}  }};

    EXPECT_EQ(key, Board2Str(expected));

    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0})), status_codes::SUCCESS);
    EXPECT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 8})), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0})), status_codes::SUCCESS);
    EXPECT_EQ(ReceiveCommand(Command(Actions::MOVE_BY_DICE, first)), status_codes::SUCCESS);

    StatusReport();

    ASSERT_EQ(ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 4})), status_codes::SUCCESS);
    EXPECT_EQ(ReceiveCommand(Command(Actions::MOVE_BY_DICE, first)), status_codes::NO_LEGAL_MOVES_LEFT);

    EXPECT_EQ(GetBoardAt(0, 8), 2);
    PrintBoard();

    ASSERT_NE(status_codes::SUCCESS, ReceiveCommand(Command(Actions::SELECT_SLOT, {0, 0}))) << "able to select before rolling";

    outcome = withDice(6, 6);

    const auto& b2s_ = g.GetBoards2Seqs();
    EXPECT_EQ(b2s_.size(), 1);
    key = b2s_.begin()->first;
    expected = {{   { 13, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0},
                    {-13, 0, 0, 0, 0, 0,-2, 0, 0, 0, 0, 0}  }};
    EXPECT_EQ(key, Board2Str(expected));
}

/*──────────────────────────────────────────────────────────────────────────────
Have to play by larger dice
──────────────────────────────────────────────────────────────────────────────*/

