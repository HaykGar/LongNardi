# Long Nardi

A from-scratch C++ implementation of the board game **Long Nardi**, together with a
research codebase for training neural-network position evaluators by self-play
reinforcement learning (TD(λ), 1-ply lookahead, and MCTS).

The repository has two halves that are useful to two different audiences:

- **You want to play the game or ship the app** → you only need the [Core Engine](#core-engine)
  and the platform front-ends (`ios/`, `images/`).
- **You want to train your own agents, design new architectures, or reproduce the
  results** → everything you need lives in [`DecisionEngine/`](#decisionengine), and this
  README walks you through it end to end.

If you are in the second camp, read this in order:

1. [The game and its rules](#the-game)
2. [Project structure](#project-structure) — what to touch and what to ignore
3. [Building the Python extension](#building-the-python-extension)
4. [Feature extraction](#feature-extraction-the-c-side) — `Board::Features` on the C++ side
5. [Feature pipelines](#feature-pipelines) — turning features into model tensors, and writing your own
6. [Model architectures](#model-architectures-nardi-net) — the shared `feature → tensor → value` contract
7. [Environments](#environments-gameenv-and-lookaheadgameenv) — `GameEnv` and `LookaheadGameEnv`
8. [The training script](#the-training-script) — `train.py`, the `-f` start file, and checkpointing
9. [Benchmarking and evaluation](#benchmarking-and-evaluation)
10. [The learning algorithm](#the-learning-algorithm-tdλ) and [results](#results)

---

## The game

Long Nardi is a tables/backgammon variant played on a 24-point board with 15 checkers
per side, all starting stacked on each player's "head." Unlike backgammon there is no
hitting; the strategic core is blocking, priming, and racing your checkers home and off
the board. If you don't already know the rules, this Wikipedia article explains them well:
<https://en.wikipedia.org/wiki/Long_Nardy>.

Internally the board is a `2×12` integer grid (`ROWS=2`, `COLS=12`); positive counts are
the player's checkers, negative are the opponent's.

---

## Project structure

```
Nardi/
├── CoreEngine/        C++ game rules, controller, and rendering          (ignore for ML work)
│   └── Testing/       deprecated C++ unit tests                          (needs refactoring)
├── DecisionEngine/    C++↔Python bindings, models, training, benchmarks  (the ML codebase)
├── ios/               iOS app front-end                                  (visual only)
├── images/            piece/board art for the desktop SFML view          (visual only)
├── dist/              stale packaging snapshot                           (candidate for deletion)
└── ci_scripts/        Xcode Cloud build hooks                            (CI only)
```

### Core Engine

`CoreEngine/` is the pure-C++ implementation of the game: board representation, move
generation and legality (`Board`), turn flow (`Controller`, `Game`), and pluggable
rendering through the `ReaderWriter` interface (`TerminalRW`, `SFMLRW`). Anyone forking
this project to **train their own agents should not need to touch this directory** — the
Python bindings expose everything you need, and the game logic is stable. The only potentially 
relevant portion for ML training is the Board::Features struct in Board.h/Board.cpp for 
handcrafting custom features (more on this later).

Two caveats if you do want to get into the weeds of the C++ core:

- **`CoreEngine/Testing/` is deprecated.** The `Controller` API changed in several
  places (notably, turns no longer auto-advance when they run out of legal moves — UIs
  now `confirm_turn()` explicitly), and the tests were not kept in sync. They require
  some light refactoring before they will build and pass.
- The engine is consumed by the Python extension via `setup.py`, which compiles the
  `CoreEngine/*.cpp` files directly (there is also a Bazel `BUILD` for the standalone
  desktop binary).

If you want custom graphics, inherit from `ReaderWriter` and attach your view — the rest
of the engine is rendering-agnostic.

### iOS, images, dist

`ios/` (the SwiftUI app + generated Xcode project) and `images/` (board and checker art
for the desktop SFML view) are **purely for the visual/product side of the app** and are
irrelevant to training experiments.

`dist/` is a leftover packaging snapshot (`dist/NardiGame/…` contains only a
`pyproject.toml`, a `requirements-game.txt`, and three PNGs). Nothing in the build
references it, and it is committed to git but not produced by any current script.

> **On deleting `dist/`:** I'd remove it. It's a stale, partial artifact — not a
> reproducible build output, not referenced by `setup.py`, `pyproject.toml`, or CI, and
> not in `.gitignore`. Deleting it removes confusion at zero cost; if you ever script a
> real desktop bundle later, regenerate it and add the output path to `.gitignore` so it
> stays out of version control. The only reason *not* to delete it is if some external
> process you have outside this repo expects those exact paths — which seems unlikely.

### DecisionEngine

This is where the interesting work happens: the pybind11 bindings to the C++ engine, the
feature tensorizers, the PyTorch model architectures, the self-play training loop, and
all the benchmarking/Elo/MCTS scripts. The rest of this README is about this directory.

---

## Building the Python extension

### Requirements

- Python 3.12+ (a `.venv` is recommended; prebuilt `nardi.cpython-3xx-*.so` files are
  checked in for several versions but you should rebuild for your own platform)
- A C++23-compatible compiler
- [SFML **3.x**](https://www.sfml-project.org/) installed system-wide (desktop view)
- [pybind11](https://github.com/pybind/pybind11) and PyTorch (LibTorch ships with the
  `torch` pip wheel; `setup.py` links against it)

### Install

From `DecisionEngine/`, with your virtual environment active:

```bash
pip install -e .
```

This compiles the `nardi` extension (the `CoreEngine/*.cpp` files plus the
`DecisionEngine` binding sources listed in `setup.py`) and installs it editable. You only
need to do this once (re-run after changing any `.cpp`/`.h`).

`setup.py` defines two compile flags worth knowing about:

- `NARDI_ENABLE_TORCH` — the C++ "target network" used for in-engine MCTS/greedy bots
  loads a **TorchScript** blob. (The iOS build omits this and uses a hand-rolled,
  Torch-free inference net instead — see `nardi_infer.{h,cpp}`.) The module exposes
  `nardi.USES_TORCH` so Python can pick the right export format automatically.
- `NARDI_ENABLE_SFML` — compiles the desktop SFML view path.

---

## Feature extraction (the C++ side)

Everything the models see starts as a `Nardi::Board::Features` object, extracted in C++
and exposed to Python as `nardi.Features`. **Features are always from the perspective of
the player whose turn it is**, so there are no negative values and the same network can
evaluate both sides — you flip perspective, not sign, to switch players.

`Features` (see [`CoreEngine/Board.h`](CoreEngine/Board.h#L64)) holds two
`PlayerBoardInfo` structs, `player` (side to move) and `opp`, plus the `raw_data` board:

```cpp
struct PlayerBoardInfo {
    // occ[k] is a length-24 (ROWS*COLS) indicator vector over board points:
    std::array<std::array<uint8_t, ROWS*COLS>, 3> occ;
    //   occ[0][i] = 1 if this player occupies point i        (any checkers)
    //   occ[1][i] = 1 if point i has 2+ of this player's checkers
    //   occ[2][i] = (n_pieces - 2) if point i has 3+ checkers (the "stack height" beyond a made point)
    int     pip_count;            // number of pip-moves to bear all checkers off
    uint8_t pieces_off;           // checkers already borne off
    uint8_t pieces_not_reached;   // checkers that have not yet reached the opponent's home quadrant
    uint8_t sq_occ;               // number of distinct points this player occupies
};
```

The three-plane `occ` encoding (occupied / made-point / extra-height) is the same trick
TD-Gammon used: it lets a convolution or linear layer reason about blots, made points, and
prime structure separately rather than from a single raw count.

These fields are exposed to Python read-only on `nardi.Features` and
`nardi.PlayerBoardInfo`:

```python
f = engine.board_features()      # a nardi.Features for the side to move
f.player.pip_count, f.player.pieces_off, f.player.sq_occ
f.player.occ                     # numpy view, shape (3, 24)
f.raw_data                       # numpy view of the 2x12 board
f.swap_perspective()             # flip player/opp in place
```

If you want to invent **new hand-crafted features** (which, per TD-Gammon, can matter a
lot), add a field to `PlayerBoardInfo`, populate it in `Board::ExtractFeatures`
(`CoreEngine/Board.cpp`), expose it via pybind11 in
[`bindings.cpp`](DecisionEngine/bindings.cpp#L376), and then surface it in a feature
pipeline (next section). The occ planes and the scalar fields are the two natural places
to add signal.

---

## Feature pipelines

A `Features` object is structured data; a model needs a flat float tensor. The conversion
lives in C++ ([`binding_utils.cpp`](DecisionEngine/binding_utils.cpp)) and is exposed as
two module functions:

```python
nardi.features_to_tensor(features,        kind="conv", flatten=False)  # one Features  -> [1, 6, 25] or [1, 150]
nardi.feature_batch_to_tensor(list_of_features, kind="conv", flatten=False)  # N Features -> [N, 6, 25] or [N, 150]
```

### The tensor layout

Every pipeline produces the **same `6 × 25` matrix** (or its `150`-vector flattening),
which is the shared contract all model architectures rely on:

- **Rows 0–2:** the side-to-move player's three `occ` planes (occupied / made / height),
  each length 24.
- **Rows 3–5:** the opponent's three `occ` planes.
- **Column 24 (the last column):** six scalar features, one per row, appended as an extra
  "point."

All values are divided by `FEATURE_SCALE = 15.0` (the number of checkers), so everything
is roughly in `[0, 1]`.

The two `kind` values differ only in **which scalars** go in that last column:

| `kind`     | scalar column (rows 0–5)                                                                 |
| ---------- | ---------------------------------------------------------------------------------------- |
| `"legacy"` | player/opp `pieces_off`, player/opp `sq_occ`, player/opp `pieces_not_reached`            |
| `"conv"`   | player/opp `pieces_off`, player/opp `pip_count`, player/opp `pieces_not_reached`         |

`flatten=True` returns `[N, 150]` (for MLPs); `flatten=False` returns `[N, 6, 25]` (for
conv/residual stems, which slice off the scalar column and process the `6×24` board as a
1-D sequence of 6 channels).

### The Python wrapper

[`nardi_net.py`](DecisionEngine/nardi_net.py) wraps these in tiny callables so a model can
carry its own preprocessing:

```python
class LegacyPipeline:                 # kind="legacy", flatten=True  -> [N, 150]  (for MLP)
class ConvPipeline(LegacyPipeline):   # kind="conv",   flatten=False -> [N, 6, 25] (for Conv/ResNet)
```

A pipeline is just an object with a `.kind` (`"legacy"`/`"conv"`), a `.flatten` (bool),
and a `__call__` that accepts any of: a single `nardi.Features`, a `list[nardi.Features]`,
a numpy array already produced by C++, or a Torch tensor already shaped for the model
(passing tensors through untouched is what lets the batched-lookahead path feed the model
directly). The `Simulator` reads `model.pipeline.kind`/`.flatten` to ask C++ for the right
batch layout, so **every model must expose a `.pipeline`.**

### Writing your own pipeline

To add a genuinely new feature layout you generally do two things:

1. **C++:** add a `kind` to `parse_pipeline_kind` and a branch in `fill_feature_matrix`
   in [`binding_utils.cpp`](DecisionEngine/binding_utils.cpp#L24) (and bump
   `FEATURE_ROWS`/`FEATURE_COLS` in `binding_utils.h` if you change the shape). Rebuild.
2. **Python:** subclass `LegacyPipeline`, set `self.kind`/`self.flatten`, and hand it to
   your model.

If your new features are *derived from existing ones* and don't need new C++, you can skip
step 1 and just compute them inside a Python pipeline's `__call__` — but the C++ path is
what the batched lookahead and the in-engine bots use, so keep them in sync if you want
those to see your features. See
[`cpp_feature_lookahead.md`](DecisionEngine/cpp_feature_lookahead.md) for the full C++
feature/lookahead design notes.

---

## Model architectures (Nardi Net)

All models live in [`nardi_net.py`](DecisionEngine/nardi_net.py) and share one contract:

```
nardi.Features  --pipeline-->  tensor  --value_from_tensor-->  scalar value (side-to-move)
```

`NardiNet` is the base class and defines the shared trunk and the **valuation head**:

```python
model(features)                 # forward(): pipeline -> value_from_tensor -> value
model.value_from_tensor(x)      # the traced/exported entry point; subclasses override this
model.feature_to_tensor(feat)   # = model.pipeline(feat)
```

### The valuation head

The trunk ends in a `Linear(n_h2, out_dim)`. With the default `out_dim=4`, the four logits
are softmaxed and dotted with the score vector `[1, 2, -1, -2]` (single win, gammon/mars,
single loss, double loss), so the network's scalar output is an **expected game result in
points** from the side-to-move's perspective. This is the quantity TD(λ) regresses toward.
(`out_dim != 4` returns raw logits, used by the ensemble's gating head.)

This shared head is why a single network can value any position and why lookahead/MCTS can
treat the output as a calibrated value.

### Provided architectures

| Class           | Pipeline        | Stem                                                        |
| --------------- | --------------- | ----------------------------------------------------------- |
| `NardiNet`      | `LegacyPipeline`| none — flat `150` → MLP (`Linear 64 → SiLU → Linear 16 → SiLU → Linear 4`) |
| `ConvNardiNet`  | `ConvPipeline`  | `Conv1d(6→C, k=5)` over the `6×24` board (optional 2nd conv with `extra_conv=True`), LayerNorm+ReLU, concat scalars, then the MLP |
| `ResNardiNet`   | `ConvPipeline`  | one `ResidualBlock` (two `k=5` convs + 1×1 skip projection), LayerNorm+ReLU, concat scalars, then the MLP |
| `NardiSemble`   | `ConvPipeline`  | a gating `ConvNardiNet` whose softmax weights a frozen list of sub-models |

Each conv/residual model overrides `value_from_tensor` to: slice the `[B, 6, 25]` tensor
into `board = x[:, :, :-1]` and `scalars = x[:, :, -1]`, run the stem over the board,
concatenate the scalars back in, and feed the shared `forward_tensor` head. **That split is
the only thing a new stem has to respect.**

### Designing a new architecture

1. Subclass `NardiNet` (or `ConvNardiNet`).
2. In `__init__`, build your stem and call `super().__init__(n_h1, n_h2, pipeline,
   input_dim=<flattened stem output + #scalars>, out_dim=4)`.
3. Override `value_from_tensor(self, x)` to run stem → concat scalars → `self.forward_tensor`.
4. Make sure `self.pipeline` is set (the base class does this from the `feature_pipeline`
   arg) so the `Simulator` can batch features for you.

That's the whole extension surface. Because everything funnels through
`value_from_tensor`, your new model is automatically trainable, benchmarkable, and
exportable.

### Exporting for the C++ bots

Two exporters bridge a trained model into the C++ engine:

- `export_target_network(model, path)` — traces `value_from_tensor` to **TorchScript**
  (`[N, 6, 25]` in, value out). This is what `Engine.load_target_network` consumes in the
  default LibTorch build, and what the in-engine greedy/lookahead/MCTS bots and `mcts_train`
  use.
- `export_weights(model, path)` — serializes parameters to a flat, Torch-free `.nardiw`
  blob for the hand-rolled inference net (`nardi_infer.cpp`) used by the iOS build and the
  parity test.
- `export_for_engine(model, path)` — picks the right one based on `nardi.USES_TORCH`.

---

## Environments: `GameEnv` and `LookaheadGameEnv`

[`env.py`](DecisionEngine/env.py) defines gym-style self-play environments for TD(λ)
training. The key design idea: **an env owns one in-flight game and its own eligibility
trace, but never touches the shared model weights.** The trainer owns the weights and
decides when to apply updates, so you can run many envs in parallel, average their update
directions, and apply once. Weight ownership is global; trajectory state is local.

Each env wraps its own `Simulator` (and thus its own C++ `Engine`).

### `GameEnv` — raw TD(λ)

```python
env = GameEnv(model, params, *, build_pos=None, lam=0.7, track=False)

env.reset()            # start a fresh game, clear the eligibility trace,
                       #   cache the initial value+gradient; returns the initial value
env.step(move_fn)      # play one move via move_fn(sim); returns (update, done, info)
```

- `model` — the shared model. `params` — the trainable parameters (the env builds one
  eligibility trace tensor per param).
- `build_pos` — optional callable to set a non-standard start position (e.g.
  `config.withRandomEndgame` for endgame fine-tuning); `None` = standard opening.
- `lam` — the TD(λ) decay λ.
- `track` — if `True` (typically only env 0), records the per-move value trajectory for
  plotting.

**What `step` does**, in order:
1. Decay the eligibility trace and add the cached gradient of the *previous* value:
   `e ← λ·e + ∇Y_old`.
2. Apply one move with `move_fn(sim)` (under `torch.no_grad()`).
3. If the game isn't over, evaluate the new afterstate and cache `(Y_new, ∇Y_new)`. If it
   is over, `Y_new` is the **true game result** (in points, kept in the previous eval's
   frame).
4. Compute `delta = Y_new − Y_old`, clamp it to `[-1, 1]` (so a lucky-dice swing can't blow
   up the update), and return `update = clamp(delta) · e` — this game's TD(λ) step
   direction, *unscaled* by the learning rate (the trainer scales and averages).

`info` is `{}` normally and `{"result", "sign"}` on the terminal move.

A `move_fn` is any `move_fn(sim)` that applies one move — e.g.
`sim.apply_greedy_move(model)` or `sim.apply_noisy_move(eps, temperature, model)`. The
trainer builds these for you (see [the training script](#the-training-script)).

### `LookaheadGameEnv` — bootstrapping from 1-ply lookahead

```python
env = LookaheadGameEnv(model, params, *, build_pos=None, lam=0.7,
                       track=False, target_model=None)
env.step()             # move_fn is ignored; the env does its own 1-ply search
```

This subclass changes the **bootstrap target** without changing what is trained. It still
learns the raw value function `V(s) = model(s)` (its gradient feeds the trace exactly as
before), but the TD target is the **1-ply lookahead value** `L(a)` of the chosen afterstate
— an expectimax over the opponent's 21 dice outcomes, computed by the C++ batch:

```
delta_t = L_frozen(a_t) − V(a_{t-1})       (semi-gradient TD; target = lookahead)
trace  += ∇V(a_{t-1})
```

Crucially, `L` is computed with a **frozen `target_model`** (a copy of the live model,
snapped only between training stages), exactly like a DQN target network — this stops the
target from chasing its own tail as weights move mid-stage. The live model is still used
for the gradient. `step` ignores its `move_fn` argument because move selection *is* the
lookahead search. If a roll has no legal move, the turn passes and the env falls back to
the naive model value for that step.

---

## The training script

[`train.py`](DecisionEngine/train.py) is the entry point for self-play TD(λ) training. It
defines `TDTrainer` (raw TD targets, `GameEnv`) and `LookaheadTDTrainer` (lookahead
targets, `LookaheadGameEnv`, with a frozen target network resynced every stage).

### Command line

```bash
python train.py --arch MLP   -f weights/my_mlp.pt   --directory figures/my_run
python train.py --arch ResNet -f weights/my_res.pt  --lookahead --envs 8 --stages 200 --games 4000
```

| Flag                       | Meaning                                                                                  |
| -------------------------- | ---------------------------------------------------------------------------------------- |
| `--arch {MLP,Conv,ResNet}` | **required.** Which architecture to instantiate (`Conv` uses `extra_conv=True`).         |
| `-f`, `--file`             | weights checkpoint file (the **start file**; see below).                                 |
| `--directory`              | output dir for figures (surprise/win-rate/eval-trajectory plots). Created if missing.    |
| `--lookahead`              | use `LookaheadTDTrainer` (1-ply lookahead bootstrap targets). Slower per move.           |
| `--envs N`                 | number of parallel self-play envs (synchronized batch of games). `1` = original single-game. |
| `--stages N`               | number of training stages (default 100).                                                 |
| `--games N`                | games per stage (default 5000).                                                          |
| `--noise-frac F`           | fraction of stages (from the start) that use noisy exploratory play; the rest are greedy.|
| `--lambda F`               | TD(λ) decay (default 0.7).                                                                |
| `--lr F`                   | initial learning rate α (default 0.01).                                                  |
| `--endgame`                | train only from random endgame positions (`config.withRandomEndgame`).                   |
| `--baseline-res`           | benchmark against `weights/res2.pt` (greedy) instead of the default heuristic bot.       |

### The `-f` start file

`-f` is both the **starting weights** and the **save target's name basis**:

- If the file doesn't exist, it's created empty and training starts from a randomly
  initialized model.
- If it exists and is non-empty, its `state_dict` is loaded and training **resumes from
  those weights**.
- If you omit `-f` entirely, the trainer warns that nothing will be saved and waits for you
  to confirm.

So to continue a run, point `-f` at the checkpoint you want to start from; to start fresh,
point it at a new path.

### Checkpointing

At the **end of every stage**, the trainer saves a separate checkpoint rather than
overwriting the start file. Checkpoints go in a per-run directory under
`checkpoints/`, named after the start file's prefix (its basename without `.pt`).
Given `-f weights/myrun.pt`, stage `s` is saved as:

```
weights/checkpoints/myrun/chkpt{s}.pt
```

So a 100-stage run leaves `chkpt0.pt … chkpt99.pt` in `weights/checkpoints/myrun/`, and
your original `-f` file is never clobbered. This is what makes the [Polyak / SWA averaging
step](#supporting-scripts) downstream possible — you average a tail of these per-stage
checkpoints into a final model.

### What a stage does

Each stage (`TDTrainer.train` → `run_batch`):

1. Anneals exploration `eps` and `temperature` toward their floors (`eps_min`, `t_min`)
   on a geometric schedule with half-lives `h_e`, `h_t`.
2. Decides noisy vs. greedy play for the stage based on `--noise-frac`.
3. Runs `--games` games as a synchronized batch over `--envs` envs: every env steps one
   move, the per-game TD updates are **averaged**, and a single update scaled by α is
   applied. Finished games reset immediately so the batch stays full.
4. Benchmarks the live model against the baseline (heuristic, or `res2.pt` greedy with
   `--baseline-res`) and logs the win rate. If the win rate plateaus (improvement `< 3%`
   for `>5` stages), α is decayed toward `alpha_min`.
5. Saves the stage checkpoint and (for `LookaheadTDTrainer`) snaps the frozen target
   network to the live weights.

Plots (max surprise per stage, win rate per stage, sample eval trajectories) are written to
`--directory` at the end.

---

## Benchmarking and evaluation

All of these live in `DecisionEngine/` and use the `Simulator`
([`sim_play.py`](DecisionEngine/sim_play.py)), which is the single object that knows how to
play a game with a given **strategy**. Strategies (`Strategy` enum / `strat_to_func`):

- `greedy` — pick the highest-valued legal afterstate (model required).
- `lookahead` — 1-ply expectimax over opponent dice (model required); Tesauro-style, and
  the strongest of the cheap strategies.
- `mcts` — model-guided MCTS in C++ (requires a conv-pipeline model; exports a target net
  first).
- `heuristic` — greedy on square-occupancy, no model (the default weak baseline).
- `random` — uniform random legal move.
- `human` — interactive play.

Key scripts:

- **`benchmark.py`** — multiprocessed head-to-head between two MLP configs (or a model vs.
  the heuristic), reporting win rates. The `if __name__ == "__main__"` block reproduces the
  64-16 vs 128-32 vs heuristic comparison.
- **`get_elos.py`** — parallel round-robin across the `PLAYERS` registry (`mlp`, `conv`,
  `deepconv`, `res`, `res2`), producing relative Elo ratings. Edit `PLAYERS`/`CTOR_KEYS` to
  add your own models.
- **`mcts_benchmark.py`** — `python mcts_benchmark.py --weights … --arch ResNet --strat-a
  mcts --strat-b lookahead --sims 100` to compare MCTS vs. lookahead head-to-head.
- **`endgame_benchmark.py`** — MCTS vs. 1-ply lookahead specifically on endgame positions.
- **`endgame.py`** — play a single graphical game from a random endgame against a model.
- **`sim_play.py`** (`__main__`) — quick scripted matches (e.g. `vzgo` vs `res2`).

To **play against a model** with graphics, use `Simulator.play_with_graphics(model,
strat)` (SFML view). `models.py` loads the pretrained `mlp`, `res2`, and `vzgo` weights for
convenience.

### Supporting scripts

- **`polyak_average.py`** — `python polyak_average.py <checkpoint_dir> <out.pt> [--last N]
  [--ema BETA] [--glob PATTERN]` averages a tail of the per-stage `chkpt*.pt` files in a
  run's checkpoint directory (e.g. `weights/checkpoints/myres/`) into one model
  (Polyak-Ruppert / SWA). This is how the released averaged checkpoints (e.g.
  `polAvg20_lookahead.pt`) were produced.
- **`mcts_train.py`** — AlphaZero-style fitted value iteration: generate MCTS self-play
  targets, then supervised-fit the value net to them (`python mcts_train.py --help`).
- **`models.py`** — central registry of pretrained models loaded from `weights/`.

### Reproducing the results

1. `pip install -e .` in `DecisionEngine/`.
2. Train a model, e.g. `python train.py --arch ResNet -f weights/myres.pt --lookahead
   --envs 8 --stages 200 --games 4000 --directory figures/myres`.
3. (Optional) average the tail checkpoints: `python polyak_average.py
   weights/checkpoints/myres weights/myres_avg.pt --last 20`.
4. Benchmark: add your model to `get_elos.py`'s `PLAYERS` and run it, or run
   `benchmark.py` / `mcts_benchmark.py` for head-to-heads. The pretrained `weights/*.pt`
   files are the references to beat.

---

## The learning algorithm: TD(λ)

Models are trained with a Temporal-Difference method close to Tesauro's
[TD-Gammon](https://dl.acm.org/doi/pdf/10.1145/203330.203343), with a few modifications.
During training the model plays itself, tracking the evaluation at every step. Let `Y_t` be
the evaluation from white's perspective at step `t`, `delta = Y_{t+1} − Y_t`, and `grad_t`
the gradient of `Y_t` w.r.t. the weights `w`. After every move:

```
w  ←  w + α · delta · Σ_{k=1..t} λ^(t-k) · grad_k
```

`delta` measures how much the previous evaluation "missed," and `λ` controls how much
credit/blame earlier evaluations receive — `grad_t` has coefficient `λ⁰ = 1`, `grad_{t-3}`
has `λ³`, and so on, assigning exponentially decaying blame to past evaluations. At the end
of a game `Y_{t+1}` is the **actual outcome in points** (`1, 2, -1, -2`), which provides the
ground-truth signal. See
[Temporal Difference Learning and TD-Gammon](https://dl.acm.org/doi/pdf/10.1145/203330.203343)
for the full treatment.

For further details on my work, please see this article (link forthcoming).