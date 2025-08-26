#pragma once

#include <array>
#include <iostream>

#include "Auxilaries.h"

using namespace Nardi;

namespace TestGlobals
{

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
// Meaningful bools for colors and dice
////////////////////////////////////////////////////////////////////////////////

inline constexpr bool white = 0;
inline constexpr bool black = 1;

inline constexpr bool first = 0;
inline constexpr bool second = 1;


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

inline constexpr dice prev3dice = {6, 5};

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

inline constexpr dice block3_dice = {6, 5};

inline constexpr
                   //      0  1  2  3  4. 5. 6. 7. 8. 9. 10 11   
boardConfig block_wrap1 = {{  { 10, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
                              {-12, 0, 0, 0, 0,-1,-1,-1, 1, 0, 1, 1} }};

inline constexpr dice wrap_dice1 = {1, 6};
inline constexpr dice wrap_dice2 = {1, 3};
inline constexpr dice wrap_dice3 = {1, 4}; 

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
inline constexpr dice ds = {3, 3};

}