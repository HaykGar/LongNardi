#pragma once
#include "../NardiGame.h"
#include "../Controller.h"
#include "../ReaderWriter.h"

#include<functional>
#include<cassert>

struct TestCase
{
    TestCase(
        std::string msg,
        Command c,
        std::array<std::array<int, COL>, ROW>& board,
        bool player,
        std::array<int, 2>& dice,
        std::array<int, 2>& diceUsed,
        bool headUsed,
        Game::status_codes exp
    );
    TestCase(
        std::string msg,
        Command c,
        std::array<std::array<int, COL>, ROW>& board,
        bool player,
        std::array<int, 2>& dice,
        std::array<int, 2>& diceUsed,
        bool headUsed,
        Game::status_codes exp,
        NardiCoord s
    );

    std::string message;
    Command cmd;
    std::array<std::array<int, COL>, ROW> brd;
    bool p_idx;
    std::array<int,2> dice_;
    std::array<int,2> diceUsed_;
    bool headUsed_ = false;
    Game::status_codes expected;

    std::optional<NardiCoord> start;
};

class SilentRW : public ReaderWriter   // does nothing
{
    public: 
        SilentRW(const Game& game, Controller& c);
        virtual void AwaitUserCommand();
        virtual void ReAnimate() const;   
        virtual void AnimateDice() const;
        virtual void InstructionMessage(std::string m) const;
        virtual void ErrorMessage(std::string m) const;
    private:
        virtual Command Input_to_Command() const;
};

class TestBuilder
{
    public:
        TestBuilder(int seed = 1); 

        // setting internals explicitly
        TestBuilder& withBoard(const std::array<std::array<int, COL>, ROW>& b);
        TestBuilder& withPlayer(bool p_idx);
        TestBuilder& withDice(const std::array<int,2>& d);
        TestBuilder& withDiceUsed(const std::array<int,2>& d_used);
        TestBuilder& withHeadUsed(bool used);
        TestBuilder& withStart(NardiCoord start);   // assumes validity

        // check test case
        bool Test(TestCase t_case);

    private:
        Game _game;
        Controller _ctrl;
        SilentRW srw;

        void ConstructAvailabilitySets();
        void ResetControllerState();
};