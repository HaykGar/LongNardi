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

    StatusReport();

    const auto& g = GetGame();
    const auto& b2s = g.GetBoards2Seqs();

    EXPECT_EQ(b2s.size(), 1);

    ASSERT_EQ(ReceiveCommand(Command(0, 0)), status_codes::SUCCESS);
    EXPECT_EQ(ReceiveCommand(Command(0, 8)), status_codes::SUCCESS);
    ASSERT_EQ(ReceiveCommand(Command(0, 0)), status_codes::SUCCESS);
    EXPECT_EQ(ReceiveCommand(Command(first)), status_codes::SUCCESS);

    StatusReport();

    ASSERT_EQ(ReceiveCommand(Command(0, 4)), status_codes::SUCCESS);
    EXPECT_EQ(ReceiveCommand(Command(first)), status_codes::NO_LEGAL_MOVES_LEFT);

    StatusReport();

    EXPECT_EQ(static_cast<int>(GetBoardAt(0, 8)), 2);
    PrintBoard();

    ASSERT_NE(status_codes::SUCCESS, ReceiveCommand(Command(0, 0))) << "able to select before rolling";

    outcome = withDice(6, 6);

    const auto& b2s_ = g.GetBoards2Seqs();
    EXPECT_EQ(b2s_.size(), 1);
}

/*──────────────────────────────────────────────────────────────────────────────
Have to play by larger dice
──────────────────────────────────────────────────────────────────────────────*/

TEST_F(TestBuilder, MaxDice)
{
    BoardConfig diceMaxExc = { {{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,-1},
                                {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}  };

    auto rc = StartOfTurn(white, diceMaxExc, 5, 6);
    StatusReport();

    DispErrorCode(rc);
    ASSERT_EQ(rc, status_codes::SUCCESS);

    auto& g = GetGame();
    auto b2s = g.GetBoards2Seqs();
    EXPECT_EQ(b2s.size(), 1);
    EXPECT_EQ(b2s.begin()->second.size(), 1);

    EXPECT_EQ(b2s.begin()->second.at(0)._diceIdx, second);

    rc = ReceiveCommand(Command(0, 0));
    ASSERT_EQ(rc, status_codes::SUCCESS);

    rc = ReceiveCommand(Command(first));
    EXPECT_EQ(rc, status_codes::MISC_FAILURE);

    rc = ReceiveCommand(Command(0, 0));
    ASSERT_EQ(rc, status_codes::SUCCESS);
    rc = ReceiveCommand(Command(second));
    EXPECT_EQ(rc, status_codes::NO_LEGAL_MOVES_LEFT);
}