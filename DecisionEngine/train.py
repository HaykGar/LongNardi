from sim_play import Simulator
import nardi

import torch

import numpy as np
from tqdm import tqdm
import os
import matplotlib.pyplot as plt


class GameEnv:
    """A gym-style self-play environment for TD(lambda) training.

    Each env owns everything specific to one game in flight:
      - its own engine (via Simulator),
      - its own TD(lambda) eligibility trace (accum_grad),
      - and the computation of its own value and gradient (eval_and_grad).

    The trainer owns the *shared* model weights and decides how/when to apply
    updates; an env never touches the weights. `step` plays one move and hands
    back this game's update direction, so the trainer can average across envs
    before applying. This keeps weight ownership global and trajectory state local.

    API:
      env.reset()                -> initial value (also clears trace)
      env.step(move_fn)          -> (update, done, info)

    where move_fn(sim) applies one move with whatever strategy the caller wants
    (e.g. noisy/greedy), `update` is a per-parameter list (this game's
    clamp(delta) * eligibility_trace), and `info` carries the result on terminal.
    """

    __slots__ = ("model", "params", "build_pos", "LAMBDA", "device",
                 "sim", "accum_grad", "Y_new", "g",
                 "track", "eval_trace", "max_surprise")

    def __init__(self, model, params, *, build_pos=None, lam=0.7, track=False):
        self.model = model
        self.params = params
        self.build_pos = build_pos
        self.LAMBDA = lam

        self.sim = Simulator()
        self.device = self.sim.device

        # Eligibility trace + cached current value/gradient (this env's own copy,
        # so a sibling env's eval can never clobber it).
        self.accum_grad = [torch.zeros_like(p) for p in params]
        self.Y_new = None
        self.g = None

        self.track = track          # env 0 records a trajectory for plotting
        self.eval_trace = []
        self.max_surprise = 0.0

    def eval_and_grad(self, pos=None):
        """Evaluate the current (or given) position and its gradient w.r.t. params."""
        value = self.sim.eval_position(self.model, pos)
        grads = torch.autograd.grad(
            value, self.params,
            retain_graph=False, create_graph=False, allow_unused=False,
        )
        # grads are freshly allocated by autograd, so this env keeps its own copy.
        return value.squeeze(), list(grads)

    def reset(self):
        """Start a fresh game, clear the eligibility trace, cache the initial eval."""
            
        self.sim.reset(build_pos=self.build_pos)

        for e in self.accum_grad:
            e.zero_()

        self.Y_new, self.g = self.eval_and_grad()
        self.max_surprise = 0.0
        self.eval_trace = [float(self.Y_new.item())] if self.track else []
        return self.Y_new

    def step(self, move_fn):
        """Advance one move via move_fn(sim); return (update, done, info).

        update : list[Tensor] per trainable parameter, this game's TD(lambda)
                 update direction  clamp(delta) * eligibility_trace.
        done   : whether the game ended on this move.
        info   : {} normally; {'result', 'sign'} on termination (for scoring).
        """
        Y_old = self.Y_new

        # Eligibility trace: e <- lambda * e + grad(Y_old)
        for e, gi in zip(self.accum_grad, self.g):
            e.mul_(self.LAMBDA).add_(gi)

        with torch.no_grad():
            move_fn(self.sim)

        done = self.sim.eng.is_terminal()
        info = {}
        if not done:
            self.Y_new, self.g = self.eval_and_grad()
        else:
            result = self.sim.eng.winner_result()
            sign = self.sim.sign
            # sign is read from C++; after a completed terminal move the controller
            # has advanced to the losing side, so unflip to keep Y_new in the
            # previous eval frame.
            self.Y_new = torch.tensor(-sign * result, device=self.device, dtype=torch.float32)
            info = {"result": result, "sign": sign}

        delta = self.Y_new - Y_old
        self.max_surprise = max(self.max_surprise, float(delta.abs().item()))
        if self.track:
            self.eval_trace.append(float(self.Y_new.item()))

        # clamp(delta) * trace, mirroring the original per-move TD update; the
        # trainer averages these across envs before scaling by alpha.
        clamped = torch.clamp(delta, min=-1, max=1)
        update = [clamped * e for e in self.accum_grad]
        return update, done, info


class LookaheadGameEnv(GameEnv):
    """A self-play env whose TD targets come from 1-ply lookahead (expectimax)
    instead of the raw model value.

    We still train the raw value function V(s) = model(s): its gradient feeds the
    eligibility trace exactly as in GameEnv. What changes is the *bootstrap
    target*. For an afterstate a (the board after a player's move), the better
    estimate of its value is the lookahead value L(a) = the value of the best
    move under 1-ply search, which the C++ batch already computes as the chosen
    child's `child_value` (an expectation over the opponent's dice). So:

        delta_t = L_frozen(a_t) - V(a_{t-1})   (semi-gradient TD; target = lookahead)
        trace  += grad V(a_{t-1})

    The lookahead target is computed with a *frozen* target_model (a copy of the
    live model, updated only between stages). This prevents the target from
    chasing its own tail as weights update mid-stage — the same instability that
    DQN solved with a frozen target network. The live model is still used for
    eval_and_grad (gradient computation).
    """

    __slots__ = GameEnv.__slots__ + ("target_model",)

    def __init__(self, model, params, *, build_pos=None, lam=0.7, track=False,
                 target_model=None):
        super().__init__(model, params, build_pos=build_pos, lam=lam, track=track)
        # Frozen copy used for target computation. Set by LookaheadTDTrainer and
        # updated between stages; falls back to the live model if not provided
        # (preserves the original behaviour when used standalone).
        self.target_model = target_model if target_model is not None else model

    def _lookahead_move(self):
        """Roll, pick the best 1-ply move, and return (target, done, info).

        target : white-perspective value of the resulting afterstate — the
                 lookahead value of the chosen move under the frozen target_model
                 (or the true result on a win).
        """
        sim = self.sim
        # Frozen model for the target; live model for gradients elsewhere.
        target_model = self.target_model

        # No legal move for this roll: the turn passes with the board unchanged.
        # No lookahead is possible, so fall back to the naive model value.
        if not sim.eng.roll_has_children():
            with torch.inference_mode():
                target = float(sim.sign * target_model(sim.eng.board_features()).item())
            return target, False, {}

        batch = sim.eng.make_lookahead_batch()
        if batch.num_children == 0:
            raise Exception("roll has children but batch has none")

        target, values = sim.lookahead_eval_and_values(target_model, batch)
        sim.eng.apply_best_lookahead(values)

        if sim.eng.is_terminal():
            # A winning move: the true game result replaces the estimate. sign has
            # flipped to the losing side, so unflip to stay in the same frame.
            result = sim.eng.winner_result()
            return float(-sim.sign * result), True, {"result": result, "sign": sim.sign}

        return target, False, {}

    def step(self, move_fn=None):
        # Y_old is the *prediction* V(a_{t-1}) cached from the previous step.
        Y_old = self.Y_new

        # Eligibility trace: e <- lambda * e + grad(V(a_{t-1}))
        for e, gi in zip(self.accum_grad, self.g):
            e.mul_(self.LAMBDA).add_(gi)

        target, done, info = self._lookahead_move()

        if not done:
            # Prediction at the new afterstate (its gradient feeds next step's trace).
            self.Y_new, self.g = self.eval_and_grad()
        else:
            self.Y_new = torch.tensor(target, device=self.device, dtype=torch.float32)

        delta = target - Y_old
        self.max_surprise = max(self.max_surprise, float(delta.abs().item()))
        if self.track:
            self.eval_trace.append(float(target))

        clamped = torch.clamp(delta, min=-1, max=1)
        update = [clamped * e for e in self.accum_grad]
        return update, done, info


class TDTrainer:
    env_cls = GameEnv   # which environment class run_batch drives (see LookaheadTDTrainer)

    def __init__(
        self,
        model,
        *,
        output_dir=None,
        weights_file=None,

        # ---- learning params ----
        LAMBDA=0.5,
        alpha=0.01,
        alpha_min_factor=0.1,

        # ---- noise / exploration ----
        # K=24,
        eps=0.25,
        eps_min=0.1,
        h_e=15,
        noise_fraction=0.0,

        # ---- temperature ----
        temperature=1.0,
        t_min=0.1,
        h_t=20,
        
        # ---- benchmarks ----
        baseline_args=(None, "heuristic"),
        # ---- config ----
        endgame_finetune = False,
        num_envs = 1,
    ):
        if not weights_file:
            print("WARNING - training results will not save anywhere. Press enter to proceed")
            input()

        self.model = model
        self.output_dir = output_dir
        self.weights_file = weights_file

        ###################################
        ######### hyperparameters #########
        ###################################
        self.LAMBDA = LAMBDA

        self.alpha = alpha
        self.alpha_0 = alpha
        self.alpha_min = alpha * alpha_min_factor

        # self.K = K

        self.eps = eps
        self.eps0 = eps
        self.eps_min = eps_min
        self.h_e = h_e

        self.temperature = temperature
        self.t0 = temperature
        self.t_min = t_min
        self.h_t = h_t
        
        self.noise_fraction = noise_fraction

    #######################################
    ######### model and simulator #########
    #######################################
        # A dedicated simulator used only for benchmarking / reporting, kept
        # separate from the training environments so a benchmark never disturbs
        # an in-flight training game.
        self.simulator = Simulator()

        self.model = model
        if weights_file:
            if not os.path.exists(weights_file):
                open(weights_file, "wb").close()
            elif os.path.getsize(weights_file) > 0:
                self.model.load_state_dict(torch.load(weights_file))

        self.model.to(self.simulator.device)
        self.device = self.simulator.device

        self.baseline = baseline_args

        self.build_pos = self.simulator.config.withRandomEndgame if endgame_finetune else None

    #################################
    ######### training vars #########
    #################################
        self.points = [0, 0]
        self.eval_traces = []
        self.surprises = []
        self.last_wr = 0
        self.no_improve = 0

        self.output_dir = output_dir
        self.weights_file = weights_file

        self.params = [p for p in self.model.parameters() if p.requires_grad]

    #################################
    ######### environments ##########
    #################################
        # Each environment owns its own engine (via Simulator) and its own
        # TD(lambda) eligibility trace. Games run as a synchronized batch: every
        # env steps one move, the per-game updates are averaged, and a single
        # averaged update is applied (see run_batch). num_envs == 1 reproduces
        # the original one-game-at-a-time behavior exactly.
        self.num_envs = max(1, num_envs)
        self.envs = [
            self.env_cls(self.model, self.params,
                         build_pos=self.build_pos, lam=self.LAMBDA, track=(i == 0))
            for i in range(self.num_envs)
        ]
        
    ####################################
    ######### helper functions #########     
    ####################################
    
    def record_results(self):
        if self.output_dir:
            os.makedirs(self.output_dir, exist_ok=True)

            win_rates = self.simulator.win_rates
            # Plot max_surprise over training stages
            plt.figure(figsize=(10, 4))
            plt.plot(self.surprises)
            plt.title('Max Surprise Per Stage')
            plt.xlabel('Stage')
            plt.ylabel('Max Surprise')
            plt.grid(True)
            plt.savefig(os.path.join(self.output_dir, 'max_surprise_per_stage.png'))
            plt.close()  # Close the figure to free up memory

            # Plot eval trajectories for select games
            for i, trace in enumerate(self.eval_traces):
                plt.figure(figsize=(8, 3))
                plt.plot(trace, marker='o')
                plt.title(f'Eval Trajectory - Game {i+1}')
                plt.xlabel('Move #')
                plt.ylabel('Eval')
                plt.grid(True)
                plt.savefig(os.path.join(self.output_dir, f'eval_trajectory_game_{i+1}.png'))
                plt.close()

            # Plot win_rate over training stages
            plt.figure(figsize=(10, 4))
            plt.plot(win_rates)
            plt.title('Win Rate Per Stage')
            plt.xlabel('Stage')
            plt.ylabel('Win Rate')
            plt.grid(True)
            plt.savefig(os.path.join(self.output_dir, 'win_rate_per_stage.png'))
            plt.close()

    def make_move_fn(self, noisy):
        """Build the per-move strategy passed to env.step for the current stage.

        noisy=True  -> noisy exploration for the whole game (softmax + Dirichlet
                       noise, using the current annealed eps/temperature).
        noisy=False -> greedy play.

        run_batch calls this once per stage; train() uses noisy for the first
        half of the stages, then switches to greedy.
        """
        model = self.model

        if noisy:
            eps, temperature = self.eps, self.temperature

            def move_fn(sim):
                sim.apply_noisy_move(eps, temperature, model)
        else:
            def move_fn(sim):
                sim.apply_greedy_move(model)

        return move_fn

    def run_batch(self, games_per_stage, desc="Inner Loop", noisy=False):
        """Run games_per_stage games, stepping all envs as a synchronized batch.

        Each iteration steps every active env one move, averages the per-game
        TD updates, and applies a single averaged weight update. Finished games
        are immediately reset so the batch stays full until the target count is
        reached. Returns (max_surprise, first_game_eval_trace) for reporting,
        mirroring the old per-stage "first game" tracking.

        A whole stage runs inside this one call, so it carries its own progress
        bar (over completed games) — otherwise the outer per-stage bar would sit
        at 0% for the entire stage and look frozen.
        """
        move_fn = self.make_move_fn(noisy)

        for env in self.envs:
            env.reset()

        games_completed = 0
        first_trace = None
        first_max_surprise = 0.0
        captured_first = False

        pbar = tqdm(total=games_per_stage, desc=desc, leave=False)
        while games_completed < games_per_stage:
            batch_update = [torch.zeros_like(p) for p in self.params]
            n_contrib = 0

            for env in self.envs:
                update_contrib, done, info = env.step(move_fn)
                for acc, c in zip(batch_update, update_contrib):
                    acc.add_(c)
                n_contrib += 1

                if done:
                    games_completed += 1
                    pbar.update(1)
                    self.points[(info["sign"] == -1)] += info["result"]
                    # env 0's first completed game is the one we plot/report.
                    if env.track and not captured_first:
                        first_trace = list(env.eval_trace)
                        first_max_surprise = env.max_surprise
                        captured_first = True
                    if games_completed < games_per_stage:
                        env.reset()

            # Apply the batch-averaged update once per synchronized step.
            if n_contrib > 0:
                scale = self.alpha / n_contrib
                with torch.no_grad():
                    for p, acc in zip(self.params, batch_update):
                        p.add_(acc, alpha=scale)
        pbar.close()

        # If env 0 never finished a game this stage (only happens if env 0 was
        # still mid-game when the target was hit), fall back to its partial trace.
        if not captured_first:
            first_trace = list(self.envs[0].eval_trace)
            first_max_surprise = self.envs[0].max_surprise

        return first_max_surprise, first_trace

    def report_progress(self, ng):
        mod_score, base_score = self.simulator.benchmark(self.model, 
                                                         "greedy", 
                                                         self.baseline[0], 
                                                         self.baseline[1], 
                                                         num_games=ng)
        print(f"model score: {mod_score}, baseline score: {base_score}")
        
        return 100 * mod_score / (mod_score + base_score)
################################
########   train loop   ########            
################################

    def train(self, n_stages=100, games_per_stage=5000):
        print(f"pre-training simulation (training with {self.num_envs} parallel env(s))")
        self.report_progress(ng=20)

        for stage in tqdm(range(n_stages), desc="Outer Loop", leave=False):
            # anneal noise and temperature
            self.eps = self.eps_min + (self.eps0 - self.eps_min) * 2.0**(-stage / self.h_e)
            self.temperature = self.t_min + (self.t0 - self.t_min) * 2.0**(-stage / self.h_t)

            # Noisy exploration for the first half of stages, then pure greedy.
            noisy = (stage / n_stages) < self.noise_fraction

            # Run games_per_stage games as a synchronized batch across all envs.
            max_surprise, evals = self.run_batch(games_per_stage, desc=f"Inner Loop {stage+1}", noisy=noisy)
            ################################ per stage report + actions ################################
            print()
            self.surprises.append(float(max_surprise))      # greatest swing in eval during the first game
            print("max surprise of ", max_surprise)

            print(f"results of simulation {stage+1}")
            self.wr = self.report_progress(ng=100)
            print()
            
            if self.wr - self.last_wr < 0.03:
                self.no_improve += 1
                if self.no_improve > 5:
                    self.alpha = self.alpha_min + 0.7 * (self.alpha - self.alpha_min)
                    self.no_improve = 0
            self.last_wr = self.wr
            
            self.eval_traces.append(evals)
            if self.weights_file is not None:
                torch.save(self.model.state_dict(), self.weights_file)    # save weights to file after each stage

            self.after_stage(stage)
            ################################ end stage report + actions ################################
                    

        print(f"post-training simulation")
        self.report_progress(1000)

        print("total points each: ", self.points)
        self.record_results()

    def after_stage(self, *_):
        """Called at the end of every stage. Subclasses use this to sync frozen
        target networks or perform other per-stage housekeeping."""

####################################
########   end train loop   ########
####################################


class LookaheadTDTrainer(TDTrainer):
    """TDTrainer whose envs bootstrap from 1-ply lookahead targets.

    Adds a *frozen target network* that is used exclusively for computing the
    lookahead bootstrap value L(s). The live model is still trained normally; the
    frozen copy is snapped to the live model at the end of every stage (fitted-VI
    rhythm). This prevents the target from chasing its own tail mid-stage.

    The noisy/greedy move_fn is still respected: move selection and target
    computation are now decoupled — the env always uses the frozen model for the
    Bellman target L(s) regardless of which move was chosen.
    """

    env_cls = LookaheadGameEnv

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        import copy
        # Frozen copy, updated only between stages. Starts as an exact clone so
        # the first stage's targets are consistent with the starting weights.
        self.target_model = copy.deepcopy(self.model)
        self.target_model.eval()
        # Distribute the shared frozen object to every env. All envs hold the
        # same reference, so after_stage's load_state_dict propagates to all.
        for env in self.envs:
            env.target_model = self.target_model

    def after_stage(self, *_):
        """Snap the frozen target network to the current live model weights."""
        self.target_model.load_state_dict(self.model.state_dict())
        self.target_model.eval()


if __name__ == "__main__":
    import argparse
    import nardi_net

    #####################################    
    ######### command line args #########     
    #####################################

    def positive_int(value):
        """Custom type for positive integers (>0)."""
        ivalue = int(value)
        if ivalue <= 0:
            raise argparse.ArgumentTypeError(f"{value} must be > 0")
        return ivalue

    def valid_dir(dirpath):
        """Ensure provided path is a valid directory."""
        if dirpath is not None:
            if not os.path.exists(dirpath):
                os.makedirs(dirpath)
        return dirpath

    def valid_file(filepath):
        if not os.path.exists(filepath):
            print(f"File not found: '{filepath}'. Creating a new empty file.")
            try:
                with open(filepath, 'w') as f:
                    pass
                print(f"Successfully created '{filepath}'.")
            except IOError as e:
                print(f"Error creating file '{filepath}': {e}")
                return None
        
        return filepath
    
    def valid_float_0to1(x):
        x = float(x)
        if not 0.0 <= x <= 1.0:
            raise argparse.ArgumentTypeError("Expected [0, 1], got float out of range")
        return x

    parser = argparse.ArgumentParser(
        description="Process model and traiing configuration with optional save file and directory for figures."
    )

    parser.add_argument(
        "--directory", 
        type=valid_dir, 
        help="Optional path to an existing directory to store figures", 
        default=None)
    
    parser.add_argument(
        "-f", 
        "--file", 
        type=valid_file, 
        help="Optional filename for loading network weights", 
        default=None)
    
    parser.add_argument(
        "--architecture",
        "--arch",
        type=str,
        choices=["MLP", "Conv", "ResNet"],
        required=True,
        help="Architecture to train"
    )
    
    parser.add_argument(
        "--noise-frac",
        type=valid_float_0to1,
        default=0,
        help="fraction of training stages with noisy move selection"
    )
    
    parser.add_argument(
        "--stages",
        type=positive_int,
        default=100,
        help="number of stages in training"
    )
    
    parser.add_argument(
        "--games",
        type=positive_int,
        default=5000,
        help="number of games per stage"
    )

    parser.add_argument(
        "--envs",
        type=positive_int,
        default=1,
        help="number of parallel self-play environments (batch size of games). "
             "Per synchronized step every env plays one move and the TD updates "
             "are averaged before being applied. Default 1 reproduces single-game training."
    )

    parser.add_argument(
        "--lookahead",
        action="store_true",
        help="use 1-ply lookahead (expectimax) values as TD targets instead of "
             "raw model evaluations (LookaheadTDTrainer). Slower per move."
    )
    
    parser.add_argument(
        "--lambda", 
        type=valid_float_0to1, 
        help="Optional value for LAMBDA in TDTrainer, must be in (0,1).", 
        default=0.7
    )
    
    parser.add_argument(
        "--lr", 
        type=valid_float_0to1, 
        help="Optional value for learning rate in TDTrainer, must be in (0,1).", 
        default=0.01
    )
    
    parser.add_argument(
        "--endgame",
        action='store_true',
        help='Optional flag to enable training only in endgame scenarios'
    )
    
    args = parser.parse_args()
    
    if args.architecture == "Conv":
        model = nardi_net.ConvNardiNet(extra_conv=True)
    elif args.architecture == "MLP":
        model = nardi_net.NardiNet(64, 16)
    elif args.architecture == "ResNet":
        model = nardi_net.ResNardiNet()

    trainer_cls = LookaheadTDTrainer if args.lookahead else TDTrainer
    trainer = trainer_cls(model, weights_file=args.file, output_dir=args.directory,
                          endgame_finetune=args.endgame, num_envs=args.envs)
    trainer.train(n_stages=args.stages, games_per_stage=args.games)
