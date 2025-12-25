#include "TerminalRW.h"

TerminalRW::TerminalRW(Game& game, Controller& c) : ReaderWriter(game, c) {}

void TerminalRW::Render() const
{
    AnimateDice();
    const auto& board = g.GetBoardRef();
    bool row = board.PlayerIdx();
    
    std::cout << "________________________________________________________________________________________________\n";
    std::cout << "________________________________________________________________________________________________\n";

    auto draw_row = [&](int layer, bool top)
    {
        int c; 
        int dir;
        int sign;
        if(top) {
            c = COLS - 1;
            dir = -1;
        }
        else{
            c = 0;
            dir = 1;
        }
        for(; c >= 0 && c < COLS; c += dir)
        {
            int raw = static_cast<int>(board.at(row, c));
            int val = abs(raw) - layer;

            if (raw > 0)
                sign = 1;
            else
                sign = -1;

            if(layer == 0)
            {
                if (val + layer > 0)
                    std::cout << sign << "\t";
                else
                    std::cout << 0 << "\t";
            }
            else if(val <= 0)
                std::cout << "\t";
            else if (val > 0)
            {   
                if (layer == 1)
                    std::cout << sign << "\t";
                else if (layer == 2)
                    std::cout << sign * val << "\t";
            }
        }
        std::cout<<"\n";
    };
    
    for(int i = 0; i < 3; ++i)
        draw_row(i, true);
    std::cout<<"\n\n\n";
    row = !row;

    for(int i = 2; i >= 0; --i)
        draw_row(i, false);

    std::cout << "________________________________________________________________________________________________\n";
    std::cout << "________________________________________________________________________________________________\n";

    std::cout << "\n\n\n\n\n";
}

void TerminalRW::AnimateDice() const  // assumes proper values of dice fed in
{
    DieType dice = {g.GetDice(0), g.GetDice(1)};
    if(dice[0] + dice[1] > 0) {
        std::cout << "Player " << static_cast<int>(g.GetBoardRef().PlayerSign()) << ", you rolled: " 
                << dice[0] << " " << dice[1] << "\n\n";
    }
}

status_codes TerminalRW::PollInput()
{
    std::getline(std::cin, input);
    Command cmd = Input_to_Command();
    status_codes code = status_codes::MISC_FAILURE;

    if(cmd.action != Actions::NO_OP) // avoid wasteful passing flow to control
        return ctrl.ReceiveCommand(cmd);
    else
        return code;
}

void TerminalRW::OnGameEvent(const GameEvent& e)
{
    if (e.code != EventCode::QUIT)
        Render();
    else
        InstructionMessage("Game ended by user.");
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
            Render();
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