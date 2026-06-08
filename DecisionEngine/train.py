from sim_play import Simulator
import torch

import numpy as np
from tqdm import tqdm
import os
import matplotlib.pyplot as plt

from env import *

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
                # Save each stage as its own checkpoint under a per-run directory
                # named after the weights file's prefix, e.g.
                #   weights/myrun.pt -> weights/checkpoints/myrun/chkpt{stage}.pt,
                # instead of overwriting the original weights file.
                parent = os.path.dirname(self.weights_file)
                prefix = os.path.splitext(os.path.basename(self.weights_file))[0]
                chkpt_dir = os.path.join(parent, "checkpoints", prefix)
                os.makedirs(chkpt_dir, exist_ok=True)
                chkpt_file = os.path.join(chkpt_dir, f"chkpt{stage}.pt")
                torch.save(self.model.state_dict(), chkpt_file)    # save weights to file after each stage

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

    parser.add_argument(
        "--baseline-res",
        action='store_true',
        help="Benchmark against weights/res2.pt (a ResNardiNet) playing greedily, "
             "instead of the default heuristic baseline."
    )

    args = parser.parse_args()
    
    if args.architecture == "Conv":
        model = nardi_net.ConvNardiNet(extra_conv=True)
    elif args.architecture == "MLP":
        model = nardi_net.NardiNet(64, 16)
    elif args.architecture == "ResNet":
        model = nardi_net.ResNardiNet()

    # Default baseline: no model, heuristic play. With --baseline-res, benchmark
    # against weights/res2.pt (a ResNardiNet) playing greedily.
    baseline_args = (None, "heuristic")
    if args.baseline_res:
        baseline_model = nardi_net.ResNardiNet()
        baseline_model.load_state_dict(torch.load("weights/res2.pt"))
        baseline_model.eval()
        baseline_args = (baseline_model, "greedy")

    trainer_cls = LookaheadTDTrainer if args.lookahead else TDTrainer
    trainer = trainer_cls(model, weights_file=args.file, output_dir=args.directory,
                          endgame_finetune=args.endgame, num_envs=args.envs,
                          baseline_args=baseline_args)
    trainer.train(n_stages=args.stages, games_per_stage=args.games)
