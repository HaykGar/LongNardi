#include "TerminalRW.h"

TerminalRW::TerminalRW(const Game& game) : ReaderWriter(game) {}

void TerminalRW::ReAnimate() const
{
    bool player_row = (1 - g.GetPlayerSign()) / 2; // 0 for 1, 1 for -1
    
    for(int c = COL - 1; c >= 0; --c)
        std::cout << board.at(player_row).at(c) << "\t";

    std::cout << "\n\n";

    for(int c = 0; c < COL; ++c)
        std::cout << board.at(!player_row).at(c) << "\t";

    std::cout << "\n\n";
}

void TerminalRW::AnimateDice(int d1, int d2) const  // assumes proper values of dice fed in
{
    std::cout << "Player " << g.GetPlayerSign() << ", you rolled: " << d1 << " " << d2 << std::endl;
}

char TerminalRW::getch() const
{
    struct termios oldt, newt;
    int ch;

    // Get current terminal settings
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;

    // Disable canonical mode and echo
    newt.c_lflag &= ~(ICANON | ECHO);

    // Apply new settings
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    // Read a single character
    ch = getchar();

    // Restore old settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    return ch;
}

bool TerminalRW::ReadQuitOrProceed() const
{
    char user_input = getch();
    return (user_input == 'q');
}

NardiCoord TerminalRW::ReportSelectedSlot() const
{
    int r, c;
    
    std::cout << "enter row\n";
    std::cin >> r;
    if (std::cin.fail()) 
    {
        std::cin.clear();
        std::cin.ignore(10000, '\n'); 
        ErrorMessage("Invalid input. Please enter a number.\n");
        return {-1, -1};
    }
    std::cout << "enter col\n";
    std::cin >> c;
    if (std::cin.fail()) 
    {
        std::cin.clear();
        std::cin.ignore(10000, '\n'); 
        ErrorMessage("Invalid input. Please enter a number.\n");
        return {-1, -1};
    }

    return {r, c};
}

void TerminalRW::InstructionMessage(std::string m) const
{
    std::cout << m << std::endl;
}

void TerminalRW::ErrorMessage(std::string m) const
{
    std::cerr << m << std::endl;
}