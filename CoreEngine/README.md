# Long Nardi Game

## Game Itself

### Intro

Long Nardi is a variant of backgammon popular in Armenia, Iran, and probably elsewhere in the region. The mechanics of moving are similar to backgammon, except there is no capturing pieces. Similar to backgammon there is an endgame phase where pieces "bear off" - are removed from the board. The game is played on a 2x12 board with two six-sided dice. Each player starts with 15 pieces all placed at their opposite right slot, referred to as their "head." The first player who removes all his/her pieces wins the game. I'll explain the rules and mechanics in the subsequent sections.

### Movement Rules

Naturally, players can only move their own pieces. Furthermore, players are generally allowed to move from their head slot only once per turn (see exceptions section). Also, they may never move onto slots that are occupied by 1 or more enemy pieces. These rules, selecting a friendly pieces without head reuse issue and moving to an empty or friendly occupied square, are what consitutes "board legal" moves.

Each player starts their turn by rolling the dice. Pieces can be moved by these distances. Players must try to use all of the available dice, and in cases where they can't, they must first try to move with the dice with greater value. In the case of doubles, everything is the same, except the player tries to make 4 moves instead of only 2. The turn ends when no further moves are possible, either by using up both dice or no legal moves remaining.

### Rule Exceptions

#### Blocking

Players are not allowed to leave blocked six or more consecutive slots unless their opponent has at least one piece ahead of this blockage. Players may create such a blockage during a turn, as long as they unblock it by the end of the turn. 

#### Preventing Completion and Max Dice

As discussed, players must "try" to complete the turn. This means that they must make a sequence of moves that maximizes the amount of dice they use. More concretely, in non-doubles case, if there are 1 or more move combinations that use both dice, then the player cannot make a move using one dice which results in no more moves by the other dice. In the doubles case, there is no such issue, but the player must make 4 moves, or as many are possible if less than 4. Also, in cases where there are possible moves for each dice, but no combination that uses both dice, the player must make a move with the larger dice.

### First Move

Only on the first move, if the player rolls double 4 or 6, they can and must play two pieces from the head. That is, if on the very first turn a player rolls double 6, he must move 2 pieces from head distance 6, if double 4 then two pieces distance 8 (since distance 12 is blocked by his opponent's head).

### Conclusion

Moves are legal when they follow game mechanics, are "board legal," and satisfy all other requirements in the exceptions. Here is what a starting board would look like for the player with white pieces, representing black pieces in a slot with negative quantities, white ones with positive quantities:

0   0   0   0   0   0   0   0   0   0   0   15

-15 0   0   0   0   0   0   0   0   0   0   0


## The Program

### Architecture and Overview

The game follows a typical model-view-controller architecture. In the future, it will be quite simple to use inheritance to adapt the game for different platforms and visual interfaces. 

When a user enters a command into the view, it is interpreted then sent to the controller, which uses a switch statement to call appropriate methods from game, which updates its internal state, triggers redraws and view updates as needed and sends the return status to the controller. This process repeats.

When the dice are rolled, the Game class sets these values from a uniform distribution on {1, ..., 6}, then a prevention monitor object checks to see if we are in a non-double case and that the turn is fully completable. If this is the case, then it will be necessary to check moves and see if they violate the prevention exception. Then, all feasible sequences of moves for the full turn are computed and saved. In this step, the first move and max dice exception are also accounted for. Then, when players attempt a move, these are checked by an arbiter object, which also invokes a block monitor object to prevent illegal blockings. Finally, if the moves are legal, we recalculate the remaining possible moves. If this is empty, the turn is over.