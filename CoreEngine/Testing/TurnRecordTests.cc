// Confirmed-turn record: each confirmed turn is its dice plus the ordered
// start-and-die sub-moves actually played. Driven through the public
// ScenarioBuilder / Controller command API, as the app does.
//
// Build + run: CoreEngine/Testing/run_turn_record_tests.sh

#include <gtest/gtest.h>

#include "../ScenarioBuilder.h"

using namespace Nardi;

namespace
{

struct Mv { int row; int col; bool die; };

void ExpectTurn(const Game::RecordedTurn& t, DieType dice, std::vector<Mv> moves)
{
    EXPECT_EQ(t.dice, dice);
    ASSERT_EQ(t.moves.size(), moves.size());
    for(size_t i = 0; i < moves.size(); ++i)
    {
        EXPECT_EQ(t.moves[i]._from, Coord(moves[i].row, moves[i].col)) << "move " << i;
        EXPECT_EQ(t.moves[i]._diceIdx, moves[i].die) << "move " << i;
    }
}

status_codes Play(ScenarioBuilder& b, int row, int col, bool die)
{
    b.ReceiveCommand(Command(Actions::RELEASE_SELECTED));
    b.ReceiveCommand(Command(row, col));
    return b.ReceiveCommand(Command(die));
}

void Confirm(ScenarioBuilder& b)
{
    b.ReceiveCommand(Command(Actions::CONFIRM_TURN_OVER));
}

}   // namespace

TEST(TurnRecord, EmptyBeforeAnyTurn)
{
    ScenarioBuilder b;
    b.Reset();
    EXPECT_TRUE(b.GetGame().TurnRecord().empty());
}

TEST(TurnRecord, NormalMoveRecordedOnConfirm)
{
    ScenarioBuilder b;
    b.Reset();
    b.withDice(5, 3);
    Play(b, 0, 0, 0);
    b.ReceiveCommand(Command(true));   // destination (0,5) auto-selected
    EXPECT_TRUE(b.GetGame().TurnRecord().empty());   // not confirmed yet

    Confirm(b);
    const auto rec = b.GetGame().TurnRecord();
    ASSERT_EQ(rec.size(), 1u);
    ExpectTurn(rec[0], {5, 3}, {{0, 0, 0}, {0, 5, 1}});
}

TEST(TurnRecord, DiceStoredLargerFirst)
{
    ScenarioBuilder b;
    b.Reset();
    b.withDice(3, 5);
    Play(b, 0, 0, 0);
    b.ReceiveCommand(Command(true));
    Confirm(b);
    ExpectTurn(b.GetGame().TurnRecord().at(0), {5, 3}, {{0, 0, 0}, {0, 5, 1}});
}

TEST(TurnRecord, DoublesRecordFourSubMoves)
{
    ScenarioBuilder b;
    BoardConfig brd = {{ { 13, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                         {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};
    b.withScenario(white, brd, 2, 2);
    Play(b, 0, 1, 0);
    for(int i = 0; i < 3; ++i)
        b.ReceiveCommand(Command(false));   // destination auto-selected each time
    Confirm(b);

    const auto rec = b.GetGame().TurnRecord();
    ASSERT_EQ(rec.size(), 1u);
    ExpectTurn(rec[0], {2, 2}, {{0, 1, 0}, {0, 3, 0}, {0, 5, 0}, {0, 7, 0}});
}

TEST(TurnRecord, PassIsEmptyMoveList)
{
    ScenarioBuilder b;
    // White's 6-6 lands on black at (0,6) / (0,7) from both occupied points.
    BoardConfig brd = {{ { 14, 1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0},
                         {-13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};
    b.withScenario(white, brd, 6, 6);
    Confirm(b);

    const auto rec = b.GetGame().TurnRecord();
    ASSERT_EQ(rec.size(), 1u);
    ExpectTurn(rec[0], {6, 6}, {});
}

TEST(TurnRecord, BearOffRecordsStartAndDie)
{
    ScenarioBuilder b;
    BoardConfig brd = {{ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                         {-15, 0, 0, 0, 0, 0, 14, 0, 0, 0, 0, 1} }};
    b.withScenario(white, brd, 2, 1);
    Play(b, 1, 11, 1);   // bear off with the 1
    Play(b, 1, 6, 0);
    Confirm(b);

    const auto rec = b.GetGame().TurnRecord();
    ASSERT_EQ(rec.size(), 1u);
    ExpectTurn(rec[0], {2, 1}, {{1, 11, 1}, {1, 6, 0}});
}

TEST(TurnRecord, GameEndingTurnIsRecorded)
{
    ScenarioBuilder b;
    BoardConfig brd = {{ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                         {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1} }};
    b.withScenario(white, brd, 2, 1);
    Play(b, 1, 11, 1);
    ASSERT_TRUE(b.GetGame().GameIsOver());

    const auto rec = b.GetGame().TurnRecord();
    ASSERT_EQ(rec.size(), 1u);
    ExpectTurn(rec[0], {2, 1}, {{1, 11, 1}});
}

TEST(TurnRecord, UndoWithinTurnLeavesNoTrace)
{
    ScenarioBuilder b;
    b.Reset();
    b.withDice(5, 3);
    Play(b, 0, 0, 0);
    b.ReceiveCommand(Command(Actions::UNDO));
    Play(b, 0, 0, 1);                  // (0,0) -> (0,3)
    b.ReceiveCommand(Command(false));  // (0,3) -> (0,8)
    Confirm(b);

    const auto rec = b.GetGame().TurnRecord();
    ASSERT_EQ(rec.size(), 1u);
    ExpectTurn(rec[0], {5, 3}, {{0, 0, 1}, {0, 3, 0}});
}

TEST(TurnRecord, TurnsAccumulateAcrossPlayers)
{
    ScenarioBuilder b;
    b.Reset();
    b.withDice(5, 3);
    Play(b, 0, 0, 0);
    b.ReceiveCommand(Command(true));
    Confirm(b);
    b.withDice(4, 2);
    Play(b, 1, 0, 0);
    b.ReceiveCommand(Command(true));
    Confirm(b);

    const auto rec = b.GetGame().TurnRecord();
    ASSERT_EQ(rec.size(), 2u);
    ExpectTurn(rec[1], {4, 2}, {{1, 0, 0}, {1, 4, 1}});
}

TEST(TurnRecord, ResetClearsRecord)
{
    ScenarioBuilder b;
    b.Reset();
    b.withDice(5, 3);
    Play(b, 0, 0, 0);
    b.ReceiveCommand(Command(true));
    Confirm(b);
    b.Reset();
    EXPECT_TRUE(b.GetGame().TurnRecord().empty());
}
