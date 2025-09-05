#include "TerminalRW.h"

TerminalRW::TerminalRW(const Game& game, Controller& c) : ReaderWriter(game, c) {}

void TerminalRW::ReAnimate() const
{
    const auto& board = g.GetBoardRef();
    bool player_row = board.PlayerIdx();
    
    for(int c = COLS - 1; c >= 0; --c)
        std::cout << board.at(player_row, c) << "\t";

    std::cout << "\n\n";

    for(int c = 0; c < COLS; ++c)
        std::cout << board.at(!player_row, c) << "\t";

    std::cout << "\n\n\n\n\n";
}

void TerminalRW::AnimateDice() const  // assumes proper values of dice fed in
{
    std::cout << "Player " << g.GetBoardRef().PlayerSign() << ", you rolled: " << g.GetDice(0) << " " << g.GetDice(1) << std::endl;
}


status_codes TerminalRW::AwaitUserCommand()
{
    std::getline(std::cin, input);
    Command cmd = Input_to_Command();
    status_codes code = status_codes::MISC_FAILURE;

    if(cmd.action != Actions::NO_OP) // avoid wasteful passing flow to control
        return ctrl.ReceiveCommand(cmd);
    else
        return code;
}

Command TerminalRW::Input_to_Command() const
{
    std::vector<std::string> input_pieces = splitStringByWhitespace(input);
    
    if (input_pieces.size() == 1 && input_pieces[0].length() == 1)
    {
        char ch = input_pieces[0][0];
        if(ch == 'q')
            return Command(Actions::QUIT);
        
        else if(ch == 'r')
            return Command(Actions::ROLL_DICE);

        else if(ch == 'p')
        {
            ReAnimate();
            return Command(Actions::NO_OP);
        }

        else if(isdigit(ch))
            return Command(static_cast<bool>(ch - '0'));

        else
            return Actions::NO_OP;
    }
    else if(input_pieces.size() == 2 && isNumeric(input_pieces[0]) && isNumeric(input_pieces[1]) )
        return Command( stoi(input_pieces[0]), stoi(input_pieces[1]) );
    
    else
        return Command(Actions::NO_OP);
}


std::vector<std::string> TerminalRW::splitStringByWhitespace(const std::string& str) const
{
    std::istringstream iss(str);
    std::vector<std::string> tokens;

    // Use std::istream_iterator to read words separated by whitespace
    std::copy(std::istream_iterator<std::string>(iss),
              std::istream_iterator<std::string>(),
              std::back_inserter(tokens));

    return tokens;
}

bool TerminalRW::isNumeric(std::string s) const
{
    for(int i = 0; i < s.length(); ++i)
    {
        if(!isdigit(s[i]))
            return false;
    }
    return true;
}

void TerminalRW::InstructionMessage(std::string m) const
{
    std::cout << m << std::endl;
}

void TerminalRW::ErrorMessage(std::string m) const
{
    std::cerr << m << std::endl;
}

///////////////////////////
/////   Factory   ////////
/////////////////////////

std::unique_ptr<ReaderWriter> TerminalRWFactory(Game& g, Controller& c)
{
    return std::make_unique<TerminalRW>(g, c);
}