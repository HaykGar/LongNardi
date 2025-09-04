#pragma once 

#include <iostream>
#include <array>
#include <variant>
#include <string>

namespace Nardi
{

////////////////////////////////////////////////////////////////////////////////
// Game Constants
////////////////////////////////////////////////////////////////////////////////

constexpr int ROWS = 2;
constexpr int COLS = 12;
constexpr int PIECES_PER_PLAYER = 15;

////////////////////////////////////////////////////////////////////////////////
// Useful Aliases
////////////////////////////////////////////////////////////////////////////////

using boardConfig = std::array< std::array<int, COLS>, ROWS>;

////////////////////////////////////////////////////////////////////////////////
// Meaningful bools for colors and dice
////////////////////////////////////////////////////////////////////////////////

inline constexpr bool white = 0;
inline constexpr bool black = 1;

inline constexpr bool first = 0;
inline constexpr bool second = 1;

// enum classes for scoping and extra safety

enum class status_codes 
{
    SUCCESS,
    NO_LEGAL_MOVES_LEFT,
    OUT_OF_BOUNDS,
    START_EMPTY_OR_ENEMY,
    DEST_ENEMY,
    BACKWARDS_MOVE, 
    NO_PATH,
    PREVENTS_COMPLETION,
    BAD_BLOCK,
    DICE_USED_ALREADY,
    HEAD_PLAYED_ALREADY,
    MISC_FAILURE
};        

enum class Actions
{
    QUIT,
    UNDO,
    ROLL_DICE,
    SELECT_SLOT, 
    MOVE_BY_DICE,
    AUTOPLAY,
    NO_OP
};   // later: add resign offer, mars offer

struct Coord
{
    Coord(int r, int c) : row(r), col(c) {}
    Coord() : row(-1), col(-1) {} // initialize out of bounds to force explicit assignment before use

    bool operator==(const Coord& rhs) const;
    bool OutOfBounds() const;
    bool InBounds() const;

    void Print() const;
    std::string AsStr() const;

    int row;
    int col;
};

struct StartAndDice
{ 
    StartAndDice(const Coord& f, bool d) : _from(f), _diceIdx(d) {}
    Coord _from; 
    bool _diceIdx; 
};

using MoveSequence = std::vector<StartAndDice>;

}   // namespace Nardi

namespace std
{
    template<>
    struct hash<Nardi::Coord>
    {
        constexpr std::size_t operator()(const Nardi::Coord& c) const noexcept
        {
            if(c.row < 0 || c.col < 0){
                std::cerr << "Bad Coord hashed\n";
                return Nardi::ROWS*Nardi::COLS + 1;  // one after any valid coord
            }
            return Nardi::COLS * c.row + c.col;
        }
    };
} // namespace std

namespace Nardi
{

struct Command // considering making this std::variant or something... 
{
    Command(Actions a) : action(a) {}
    Command(Coord coord);
    Command(int r, int c);
    Command(bool dice_idx);
    Command(std::string final_config);

    Actions action;
    std::variant<std::monostate, Coord, bool, std::string> payload;
};

void VisualToGameCoord(Coord& coord); // not needed currently, but for graphics later
int BoolToSign(bool p_idx);
void DispErrorCode(status_codes code);

void DisplayBoard(const std::array<std::array<int, COLS>, ROWS>& b);

std::string Board2Str(const boardConfig& b);

}   // namespace Nardi

///////////////////////////////////////////////
//////////////// Test Globals ////////////////
/////////////////////////////////////////////

namespace TestGlobals
{

using namespace Nardi;

////////////////////////////////////////////////////////////////////////////////
// Useful functions
////////////////////////////////////////////////////////////////////////////////

inline 
boardConfig ZeroWhite1BlackBoard() {
    boardConfig b{};
    for (auto& r : b) r.fill(0);
    b[1][0] = -1;
    return b;
}

inline
boardConfig SafeBoard() {
    boardConfig b{};
    for (auto& r : b) r.fill(0);
    b[1][0] = -1;   // no game over
    b[1][1] = 1;    // to prevent forcing moves
    b[1][2] = 1;
    return b;
}

inline
boardConfig HeadScenarioBoard()
{
    /* 
        minimal board:
        white head 5 pieces @ col 0
        extra white piles @ col 3,5 so we can move without ending turn
        black head @ (1,0) but otherwise empty
    */
    boardConfig b{};
    for (auto& r : b) r.fill(0);
    b[0][0] = 5;    // white head
    b[0][3] = 2;
    b[0][5] = 1;
    b[1][0] = -5;   // black head
    return b;
}

////////////////////////////////////////////////////////////////////////////////
// boards and dice 
////////////////////////////////////////////////////////////////////////////////

inline constexpr 
                     //          0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
boardConfig start_brd = {{    { 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                              {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};

inline constexpr 
                         //           0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
boardConfig board_legal = {{       { 10, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1}, 
                                   {-12, 0, 0, 0, 1,-1,-1,-1, 0, 0, 1, 1} }};

inline constexpr 
                              //     0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
boardConfig starts_check = {{      { 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                   {-5, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};

inline constexpr 
                       //         0     1  2  3  4. 5. 6. 7. 8. 9. 10 11   
boardConfig preventions1 = {{ { 15 - 2, 0, 0,-1, 0, 0, 1,-1, 0, 0, 0, 1}, 
                              {-(15-2), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};

inline constexpr 
                       //         0     1  2  3  4. 5. 6. 7. 8. 9. 10 11   
boardConfig preventions2 = {{ { 15 - 3,-1, 0, 1, 0, 0,-1, 1,-1, 0, 0, 1}, 
                              {-(15-3), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};  


                              inline constexpr 
                       //        0  1  2  3  4  5  6  7  8  9 10 11   
boardConfig preventions3 = {{ {  0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 3}, 
                              {-12, 1,-1,-1, 0,-1, 2, 0, 2, 2, 2, 2} }};  

inline constexpr std::array<int, 2> prev3dice = {6, 5};

inline constexpr 
                         //     0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
boardConfig block_check1 = {{ {14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                              {-8, 0, 0,-1, 1,-1,-1,-1,-1, 0,-1,-1} }};

inline constexpr 
                         //     0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
boardConfig block_check2 = {{ {11, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0}, 
                              {-9, 0, 0, 0, 0,-1,-1,-1, 0,-1,-1,-1} }};

inline constexpr 
                          //    0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
boardConfig block_check3 = {{ { 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2}, 
                              {-9, 0, 0, 0, 0,-1,-1,-1, 0,-1,-1,-1} }};

inline constexpr std::array<int, 2> block3_dice = {6, 5};

inline constexpr
                   //      0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
boardConfig block_wrap1 = {{  { 10, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                              {-12, 0, 0, 0, 0,-1,-1,-1, 1, 0, 1, 1} }};

inline constexpr std::array<int, 2> wrap_dice1 = {1, 6};
inline constexpr std::array<int, 2> wrap_dice2 = {1, 3};
inline constexpr std::array<int, 2> wrap_dice3 = {1, 4}; 

inline constexpr 
                         //      0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
boardConfig block_wrap2 = {{  { 10, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                              {-12, 0, 0, 0, 1,-1,-1,-1, 0, 0, 1, 1} }};

inline constexpr 
                         //     0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
boardConfig block_doub1 = {{  {12, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0}, 
                              {-1, 0,-2, 0, 0,-2,-2,-2, 0,-2,-2,-2} }};

inline constexpr
boardConfig doubles_stacked = {{   { 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                   {-15, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};
inline constexpr std::array<int, 2> ds = {3, 3};

}