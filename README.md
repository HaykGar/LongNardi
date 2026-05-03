# Long Nardi Game

## Rules

If you don't already know how to play, this wikepedia article does a good job of explaining the rules:
https://en.wikipedia.org/wiki/Long_Nardy

## Usage

### Requirements

- Python 3.12 (recommended to use the provided virtual environment)
- A C++17-compatible compiler
- SFML **3.x** installed system-wide
- pybind11

This project uses `setup.py` / `pyproject.toml` to build the C++ core via pybind11.

### Installation

From the `DecisionEngine/` directory, create and activate a virtual environment (recommended), then run:

```bash
pip install -e .
```

only once. After this, you can run 
```bash
python xagh.py
```

and play against a computer or (in-person) human opponent.


## Components

This project consists of a Core Engine and Decision Engine. The Core Engine simply implements the game in C++ with modular graphics. Anyone who forks this repository can inherit from the ReaderWriter class to create any custom graphics they want. The Decision Engine defines model architectures for a position evaluator, as well as training and other utility scripts.  


## Model Training

All models were trained using a Temporal Difference method, similar to the one used in Tesauro's [TD Gammon](https://dl.acm.org/doi/pdf/10.1145/203330.203343) but with some important modifications. During training, the model plays games against itself, keeping track of the evaluation at every step. Let Y_t denote the evaluation from white's perspective at step t, delta = Y_{t+1} - Y_t, and grad_t be the gradient for Y_t w.r.t. the model weights, denoted w. Then, after every move, we perform the update w = w + alpha * delta * sum_{k=1}^t (lambda^{t-k} grad_k) where alpha is a step size and lambda is a fixed hyper-parameter strictly between 0 and 1. At the end of the game, instead of an evaluation Y_{t+1} will be the actual outcome of the game in points, so 1, 2, -1, or -2. This will provide the ground truth which allows the model to learn.

To put into words, delta is how much the current eval changed, so it measures how much the previous evaluation "missed," and lambda controls how much credit/blame previous evaluations receive. For example, notice that grad_t has coefficient lambda^0 = 1, while grad_{t-3} has coefficient lambda^3, and thus contributes less to the change in weights. In short, the idea of the method is to assign exponentially decaying "blame" to past evaluations for how much the current evaluation is off from the previous. For more detail please see [Temporal Difference Learning and TD-Gammon](https://dl.acm.org/doi/pdf/10.1145/203330.203343).

Aside from these standard practices, I also made several modifications to the training process. First, rather than always playing greedy moves, I introduce Dirichlet noise for the first 24 moves of the game, with a prior distribution obtained by softmax(eval / temperature) over each potential option. The initial temperature is 1, which I anneal to around 0.1 throughout the training process. The Dirichlet noise parameter epsilon is also annealed from 0.25 to about 0.1. The purpose of this is to encourage even more exploration at the start of the game and avoid some of the common pitfalls of self-play reinforcement learning methods. I also clamp the value of delta to be between -1 and 1, since sometimes there are lucky dice rolls which drastically change the outcome of a game, and I wanted to mitigate their impact. I use 0.7 for the lambda parameter, and my step size alpha starts at 0.01 then cools 0.001 throughout the training process, especially when improvement against some baseline plateaus. 

## Model Play and Evaluation

As recommended by Tesauro, for move selection in games I employ a 1-ply look-ahead, essentially performing an expectimax with 1 level of chance nodes after the possible child positions. As expected, this has consistently performed better than simple greedy play.

For evaluating a model's performance, I benchmark it against different computer opponents using some hand-coded heuristics, as well as against human play. Since there are not many readily available experts, and my own resources are limited, most of the evaluation is performed against these weaker computer opponents. I also compared different model architectures to each other and obtained relative ELO values for them based on their performances.

## Final Results

For a simple 2-layer MLP, I trained several architectures in parallel, namely with 64-16 hidden units in each layer, 128-32, and 256-64. Perhaps not surprisingly, The 256-64 model was significantly slower and actually performed the worst out of the three. In head to head matches using lookahead move selection for each model, the best performing model had 64 units in the first hidden layer and 16 in the second, exactly as in TD-Gammon. However, it had only a slight edge over the 128-32 model, with a win rate of only 51.04% after 2000 games. Both models achieved over 98% win rates against the heuristic opponent. For the sake of computational efficiency, I decided to only keep the 64-16 MLP.

I also experimented with Convolutional Neural Networks, performing 1 or 2 1D convolutions with a kernel size of 5 on extracted board features, then passing them through a 2-layer (64 and 16) MLP to get the final evaluation. The models with 1 and 2 convolutions performed significantly better than the MLP. 

Most recently, I experimented with ResNet inspired models. This performed the best out of all of them in terms of ELO rating, but is not significantly better in head to head matches against either convolutional model. However, the performance gap between the ResNet and standard Convolutional models appears to grow wider when we incorporate a lookahead move selection strategy, where all models plan ahead to have more accurate evaluation estimates for each move.

I also experimented with ensemble methods, but (as I expected) these were not particularly fruitful. I briefly considered adding an attention layer, but I think this will be very computationally intensive and will have too much difficulty converging with TD training. In general, too much complexity has made training very difficult, and lead to very poorly performing models. For example, adding dropout or a second residual block resulted in worse performance than the hueristic strategy.

Future plans include exploring MCTS based training approaches as in AlphaGoZero, adding sharper handcrafted features (these made a big difference for TD Gammon), and making the whole thing more presentable/pretty. I also have ideas on how to make lookahead search much more efficient, as the current version was only implemented for proof-of-concept and sometimes takes several seconds despite parallelization.
