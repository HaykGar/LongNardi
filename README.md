# Long Nardi Game

## Game Itself

### Intro and Goal of the Game

Long Nardi is a variant of backgammon popular in Armenia, Iran, and probably elsewhere in the region. The mechanics of moving are similar to backgammon, except there is no capturing pieces. Similar to backgammon there is an endgame phase where pieces "bear off" - are removed from the board. The game is played on a 2x12 board with two six-sided dice. Each player starts with 15 pieces placed at their opposite right slot, referred to as their "head." Their goal is to get all of their pieces to the furthest 6 squares - this region is called their "home" - and then remove them before the other player. The first player who removes all of their pieces wins the game, and if a player finishes before their opponent removes a single piece, they are awarded 2 points instead of 1 (this is called a "mars"). 

### Movement Rules

Naturally, players can only move their own pieces. Furthermore, players are generally allowed to move from their head slot only once per turn. Also, they may never move onto slots that are occupied by 1 or more enemy pieces. These rules - selecting a friendly pieces without head reuse issue and moving to an empty or friendly occupied square - are what consitutes "board legal" moves.

Each player starts their turn by rolling the dice. Pieces can be moved by these distances. Players must try to use all of the available dice, and in cases where they can't, they must first try to move with the dice with greater value. In the case of doubles, everything is the same except the player tries to make 4 moves instead of only 2. The turn ends when no further moves are possible, either by using up both dice or no legal moves remaining.

Once all pieces are in the "home" region, the player enters the endgame phase. In this phase, squares are numbered right to left from 1 to 6, starting at the very last square. If a piece is on a square matching one of the dice rolled, it can be removed. Otherwise, if there are no pieces to the left of it, and the dice value is bigger than it's square number, the piece may also be removed. Players may also make normal moves as they would before. For example, given a roll of (1, 1) with 3 pieces on the last square (1, 11) and 2 pieces to its left (1, 10), the player may move (1, 10) -> (1, 11) twice then remove two pieces from (1, 11) or remove 3 pieces from (1, 11) and move 1 piece (1, 10) -> (1, 11).

### Rule Exceptions

#### Blocking

Players are not allowed to leave blocked six or more consecutive slots unless their opponent has at least one piece ahead of this blockage. Players may create such a blockage during a turn, as long as they unblock it by the end of the turn. 

#### Preventing Completion and Max Dice

As discussed, players must "try" to complete the turn. This means that they must make a sequence of moves that maximizes the number of dice used. More concretely, in non-doubles case, if there are 1 or more move combinations that use both dice, then the player cannot make a move using one dice which results in no more moves by the other dice. In the doubles case, the player must make 4 moves, or however many are possible if less than 4. Also, in cases where there are possible moves for each dice, but no combination that uses both dice, the player must make a move with the larger dice.

### First Move

Only on the first move, if the player rolls double 4 or 6, they can play two pieces from the head. For example, if on the very first turn I roll a double 6, I can and must move 2 pieces from head by distance 6.

### Conclusion

Moves are legal when they follow game mechanics, are "board legal," and satisfy all other requirements in the exceptions. Here is what a starting board would look like for the player with white pieces, representing black pieces in a slot with negative quantities, white ones with positive quantities:

0   0   0   0   0   0   0   0   0   0   0   15

-15 0   0   0   0   0   0   0   0   0   0   0


## The Core Engine

### Architecture and Overview

The game's core engine follows a typical model-view-controller architecture. In the future, it will be quite simple to use inheritance to adapt the game for different platforms and visual interfaces. 

When a user enters a command into the view, it is interpreted then sent to the controller, which uses a switch statement to call appropriate methods from game, which updates its internal state, triggers redraws and view updates as needed, and sends the return status to the controller. This process repeats until the user quits the game or one side wins.

When the dice are rolled, the Game class sets the values from a uniform distribution on {1, ..., 6} and recursively pre-computes all legal end board positions using these dice. These are stored in a map from board representation to the associated move sequences. The legality checks entail making use of a nested private class, Arbiter, as well as BadBlockMonitor to ensure that all moves in the sequence are in fact legal. This pre-computation accounts for max dice and first move exceptions, setting appropriate flags as needed. Later, when users attempt to make manual moves, the Arbiter checks once again to make sure these moves are legal, this time also making use of the PreventionMonitor object to ensure that these moves allow for turn completion. There is also an option to auto-play the moves to one of the possible end board positions, which is used during training for the decision engine.

## The Decision Engine

### What it Does

The decision engine is implemented as a Neural Network with 2 hidden layers and 4 output neurons, which are combined to form a 1-dimensional output. As input, the network takes in a representation of the board and some additional information (pieces removed each player, pieces not reached each player's home, and squares occupied by each player). The 4 output nodes outputs represent probability weights of the current player winning, marsing, losing, and getting mars-ed, respectively. These are then softmaxed, and we take the expected outcome using the resulting distribution to get the final model output: an evaluation ranging from [-2, 2]. Thus, the model takes a static board representation, and outputs a score based on its approximation for the expected outcome of the game.

### Training

The model was trained using a Temporal Difference method, similar to the one used in Tesauro's [TD Gammon](https://dl.acm.org/doi/pdf/10.1145/203330.203343) but with some important modifications. During training, the model plays games against itself, keeping track of the evaluation at every step. Let Y_t denote the evaluation from white's perspective at step t, delta = Y_{t+1} - Y_t, and grad_t be the gradient for Y_t w.r.t. the model weights, denoted w. Then, after every move, we perform the update w = w + alpha * delta * sum_{k=1}^t (lambda^{t-k} grad_k) where alpha is a step size and lambda is a fixed hyper-parameter strictly between 0 and 1. At the end of the game, instead of an evaluation Y_{t+1} will be the actual outcome of the game in points, so 1, 2, -1, or -2. This will provide the ground truth which allows the model to learn.

To put into words, delta is how much the current eval changed, so it measures how much the previous evaluation "missed," and lambda controls how much credit/blame previous evaluations receive. For example, notice that grad_t has coefficient lambda^0 = 1, while grad_{t-3} has coefficient lambda^3, and thus contributes less to the change in weights. In short, the idea of the method is to assign exponentially decaying "blame" to past evaluations for how much the current evaluation is off from the previous. For more detail please see [Temporal Difference Learning and TD-Gammon](https://dl.acm.org/doi/pdf/10.1145/203330.203343).

Aside from these standard practices, I also made several modifications to the training process. First, rather than always playing greedy moves, I introduce Dirichlet noise for the first 24 moves of the game, with a prior distribution obtained by softmax(eval / temperature) over each potential option. The initial temperature is 1, which I anneal to around 0.1 throughout the training process. The Dirichlet noise parameter epsilon is also annealed from 0.25 to about 0.1. The purpose of this is to encourage even more exploration at the start of the game and avoid some of the common pitfalls of self-play reinforcement learning methods. I also clamp the value of delta to be between -1 and 1, since sometimes there are lucky dice rolls which drastically change the outcome of a game, and I wanted to mitigate their impact. I use 0.7 for the lambda parameter, and my step size alpha starts at 0.01 then cools 0.001 throughout the training process. 

### Model Play and Evaluation

As recommended by Tesauro, for move selection in games I employ a 1-ply look-ahead, essentially performing an expectimax with 1 level of chance nodes after the possible child positions. As expected, this has consistently performed better than simple greedy play.

For evaluating the model's performance, I benchmark it against different computer opponents using some hand-coded heuristics, as well as against human play. Since there are not many readily available experts, and my own resources are limited, most of the evaluation is performed against these weaker computer opponents.

### Final Results

I trained several architectures in parallel, namely with 64-16 hidden units in each layer, 128-32, and 256-64. Perhaps not surprisingly, The 256-64 model was significantly slower and actually performed the worst out of the three. In head to head matches using lookahead move selection for each model, the best performing model had 64 units in the first hidden layer and 16 in the second, exactly as in TD-Gammon. However, it had only a slight edge over the 128-32 model, with a win rate of only 51.04% after 2000 games. Both models achieved over 98% win rates against the heuristic opponent.
