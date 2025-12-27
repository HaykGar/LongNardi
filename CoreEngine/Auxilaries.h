#pragma once 

#include <iostream>
#include <array>
#include <variant>
#include <string>
#include <cstdint> // Required for int8_t


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

using BoardConfig = std::array< std::array<int8_t, COLS>, ROWS>;
using DieType = std::array<int, 2>;

// constexpr int BK_ROWS = 6;
// constexpr int BK_COLS = ROWS*COLS + 1;
// using BoardConfig = std::array< std::array<uint8_t, BK_COLS>, BK_ROWS >;

// Helper function to combine hashes
template <class T>
inline void hash_combine(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

// Hash function for BoardConfig
struct BoardConfigHash 
{
    std::size_t operator()(const BoardConfig& key) const
    {
        std::size_t seed = 0;
        for(const auto& inner_container : key)
        {
            for(const auto& num : inner_container)
                hash_combine(seed, num);
        }
        return seed;
    }
};


////////////////////////////////////////////////////////////////////////////////
// Meaningful bools for colors and dice
////////////////////////////////////////////////////////////////////////////////

inline constexpr bool white = 0;
inline constexpr bool black = 1;

inline constexpr bool first = 0;
inline constexpr bool second = 1;

////////////////////////////////////////////////////////////////////////////////
// enum classes for scoping and extra safety
////////////////////////////////////////////////////////////////////////////////

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
    WAITING,
    MISC_FAILURE
};        

enum class Actions
{
    QUIT,
    UNDO,
    ROLL_DICE,
    SET_DICE,
    SELECT_SLOT, 
    MOVE_BY_DICE,
    AUTOPLAY,
    RESTART,
    RANDOM_AUTOPLAY,
    NO_OP
};   // later: add resign offer, mars offer

////////////////////////////////////////////////////////////////////////////////
// useful structs and aliases
////////////////////////////////////////////////////////////////////////////////

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

struct MoveData
{
    Coord from;
    Coord to;
    bool die_idx;
};

using RemoveData = StartAndDice;
using EventData = std::variant<std::monostate, MoveData, RemoveData, DieType>;

enum class EventCode {
    DICE_ROLL,
    MOVE,
    REMOVE,
    TURN_SWITCH,
    GAME_OVER,
    QUIT
};

struct GameEvent {
    EventCode code;
    EventData data;
};


struct Command // considering making this std::variant or something... 
{
    Command(Actions a);
    Command(const Coord& coord);
    Command(int r, int c);
    Command(bool dice_idx);
    Command(BoardConfig final_config);
    Command(DieType set_to);

    Actions action;
    std::variant<std::monostate, Coord, bool, BoardConfig, DieType > payload;
};

struct PlayerBoardInfo
{
    /*
    first row of each sub-occ array is 1 for every coord that player occupies, 0 everywhere else
    second "                     " 1 "                                  " with 2 or more pieces
    third "                      " n_pieces - 2 "                       " with 3 or more pieces
    */
    std::array<std::array<uint8_t, ROWS*COLS>, 3> occ;

    uint8_t pieces_off; // pieces removed by player
    uint8_t pieces_not_reached;
};

/*
features are from perspective of player whose move it is. Hence, no negative values.
*/
struct BoardFeatures
{
    BoardFeatures(BoardConfig board, bool p_idx);    
    PlayerBoardInfo player;
    PlayerBoardInfo opp;
};

/*
BoardConfig Representation:

        24 ct                   1ct
------ 1-pieces w ------| pieces off w 
........................|       .
------ 2-pieces w ------| pieces off b
........................|       .
------ 3-pieces w ------| total sq occupied w
........................|       .
------ 1-pieces b ------| total sq occupied b
........................|       .
------ 2-pieces b ------| pieces not reached w
........................|       .
------ 3-pieces b ------| pieces not reached b
........................|       .

hoping that intuitively, pieces not reached conveys "endgame-ness" with higher value indicating 
    less endgame-ness

Idea: do something smarter, craft total possible moves blocked instead of sq occ?
*/

////////////////////////////////////////////////////////////////////////////////
// utility functions 
////////////////////////////////////////////////////////////////////////////////

void VisualToGameCoord(Coord& coord); // not needed currently, but for graphics later
int BoolToSign(bool p_idx);
void DispErrorCode(status_codes code);

void DisplayBoard(const BoardConfig& b);
void DisplayKey(const BoardConfig& bk);

// const BoardConfig AsKey()
// {
//     BoardConfig key = {};      // does this guarantee initializing to 0 ?
//     std::array<int, 2> sq_occ = {0, 0};

//     // player channels
//     Coord start(player_idx, 0);
//     for(int i = 0; i < ROWS*COLS; ++i)
//     {
//         Coord coord = CoordAfterDistance(start, i);
//         int occupancy = at(coord) * player_sign;
//         int n_pieces = abs(occupancy);
//         int key_row = (occupancy >= 0) ? 0 : 3;
        
//         if(n_pieces <= 1)   // 1 or 0
//             key[key_row][i] = n_pieces;
//         else // n_pieces > 1
//         {
//             key[key_row][i] = 1;
//             key[key_row+1][i] = 1;
//             key[key_row+2][i] = n_pieces - 2;
//         }

//         if(n_pieces > 0)
//             ++sq_occ[(occupancy < 0)];    // idx 1 for opponent, 0 for friendly
//     }


//     // pieces off
//     key[0][ROWS*COLS] = pieces_per_player[player_idx] - pieces_left[player_idx];
//     key[1][ROWS*COLS] = pieces_per_player[!player_idx] - pieces_left[!player_idx];

//     // total squares occupied
//     key[2][ROWS*COLS] = sq_occ[0];
//     key[3][ROWS*COLS] = sq_occ[1];

//     // pieces not reached home
//     key[4][ROWS*COLS] = pieces_per_player[player_idx] - reached_enemy_home[player_idx];
//     key[5][ROWS*COLS] = pieces_per_player[!player_idx] - reached_enemy_home[!player_idx];

//     return key;
// }



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
BoardConfig ZeroWhite1BlackBoard() {
    BoardConfig b{};
    for (auto& r : b) r.fill(0);
    b[1][0] = -1;
    return b;
}

inline
BoardConfig SafeBoard() {
    BoardConfig b{};
    for (auto& r : b) r.fill(0);
    b[1][0] = -1;   // no game over
    b[1][1] = 1;    // to prevent forcing moves
    b[1][2] = 1;
    return b;
}

inline
BoardConfig HeadScenarioBoard()
{
    /* 
        minimal board:
        white head 5 pieces @ col 0
        extra white piles @ col 3,5 so we can move without ending turn
        black head @ (1,0) but otherwise empty
    */
    BoardConfig b{};
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
BoardConfig start_brd = {{    { 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                              {-15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};

inline constexpr 
                         //           0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
BoardConfig board_legal = {{       { 10, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1}, 
                                   {-12, 0, 0, 0, 1,-1,-1,-1, 0, 0, 1, 1} }};

inline constexpr 
                              //     0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
BoardConfig starts_check = {{      { 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                   {-5, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};

inline constexpr 
                       //         0     1  2  3  4. 5. 6. 7. 8. 9. 10 11   
BoardConfig preventions1 = {{ { 15 - 2, 0, 0,-1, 0, 0, 1,-1, 0, 0, 0, 1}, 
                              {-(15-2), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};

inline constexpr 
                       //         0     1  2  3  4. 5. 6. 7. 8. 9. 10 11   
BoardConfig preventions2 = {{ { 15 - 3,-1, 0, 1, 0, 0,-1, 1,-1, 0, 0, 1}, 
                              {-(15-3), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};  


                              inline constexpr 
                       //        0  1  2  3  4  5  6  7  8  9 10 11   
BoardConfig preventions3 = {{ {  0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 3}, 
                              {-12, 1,-1,-1, 0,-1, 2, 0, 2, 2, 2, 2} }};  

inline constexpr DieType prev3dice = {6, 5};

inline constexpr 
                         //     0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
BoardConfig block_check1 = {{ {14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                              {-8, 0, 0,-1, 1,-1,-1,-1,-1, 0,-1,-1} }};

inline constexpr 
                         //     0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
BoardConfig block_check2 = {{ {11, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0}, 
                              {-9, 0, 0, 0, 0,-1,-1,-1, 0,-1,-1,-1} }};

inline constexpr 
                          //    0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
BoardConfig block_check3 = {{ { 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2}, 
                              {-9, 0, 0, 0, 0,-1,-1,-1, 0,-1,-1,-1} }};

inline constexpr DieType block3_dice = {6, 5};

inline constexpr
                   //      0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
BoardConfig block_wrap1 = {{  { 10, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                              {-12, 0, 0, 0, 0,-1,-1,-1, 1, 0, 1, 1} }};

inline constexpr DieType wrap_dice1 = {1, 6};
inline constexpr DieType wrap_dice2 = {1, 3};
inline constexpr DieType wrap_dice3 = {1, 4}; 

inline constexpr 
                         //      0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
BoardConfig block_wrap2 = {{  { 10, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                              {-12, 0, 0, 0, 1,-1,-1,-1, 0, 0, 1, 1} }};

inline constexpr 
                         //     0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
BoardConfig block_doub1 = {{  {12, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0}, 
                              {-1, 0,-2, 0, 0,-2,-2,-2, 0,-2,-2,-2} }};

inline constexpr
BoardConfig doubles_stacked = {{   { 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                                   {-15, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0} }};
inline constexpr DieType ds = {3, 3};

}