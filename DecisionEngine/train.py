from sim_play import Simulator
import nardi_net

import torch
import numpy as np
from tqdm import tqdm
import os
import matplotlib.pyplot as plt

#TODO make the hyperparams init args with default values

class TDTrainer:
    def __init__(
        self,
        model,
        output_dir=None,
        weights_file=None,

        # ---- learning params ----
        LAMBDA=0.7,
        alpha=0.01,
        alpha_min=0.001,

        # ---- noise / exploration ----
        K=24,
        eps=0.25,
        eps_min=0.1,
        h_e=15,

        # ---- temperature ----
        temperature=1.0,
        t_min=0.1,
        h_t=20,
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
        self.alpha_min = alpha_min

        self.K = K

        self.eps = eps
        self.eps0 = eps
        self.eps_min = eps_min
        self.h_e = h_e

        self.temperature = temperature
        self.t0 = temperature
        self.t_min = t_min
        self.h_t = h_t

    #######################################
    ######### model and simulator #########     
    #######################################
        self.simulator = Simulator()
        
        self.model = model
        if weights_file:
            if not os.path.exists(weights_file):
                open(weights_file, "wb").close()
            elif os.path.getsize(weights_file) > 0:
                self.model.load_state_dict(torch.load(weights_file))

        self.model.to(self.simulator.device)
        
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
        
        self.grad_buf = [torch.zeros_like(p) for p in self.model.parameters()]
        self.params = list(self.model.parameters())
        
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

    def eval_and_grad(self):
        eval = self.simulator.eval_current(self.model)
        
        grads = torch.autograd.grad(
            eval,
            self.model.parameters(),
            retain_graph=False,
            create_graph=False,
            allow_unused=False
        )

        # copy into persistent buffer (NO allocation)
        for buf, g in zip(self.grad_buf, grads):
            buf.copy_(g)
            
        return eval.squeeze(), self.grad_buf
        
    def playout(self, track_eval : bool):
        
        self.simulator.reset()
        max_surprise = torch.zeros((), device=self.simulator.device)
            # greatest swing in position evaluation after one move in a game
        evals = []

        Y_new, g = self.eval_and_grad()
        
        if track_eval:
            evals.append(float(Y_new.item()))
        
        accum_grad = [torch.zeros_like(p, device=self.simulator.device) for p in self.model.parameters()] # in shape of weights gradient

        while not self.simulator.eng.is_terminal():
            Y_old = Y_new
            
            for e, gi in zip(accum_grad, g):
                e.mul_(self.LAMBDA).add_(gi)
            
            with torch.no_grad():
                self.simulator.apply_noisy_move(self.K, self.eps, self.temperature, self.model)

            if not self.simulator.eng.is_terminal():
                Y_new, g = self.eval_and_grad()
            else:
                Y_new = torch.tensor(-self.simulator.sign * self.simulator.eng.winner_result(), device=self.simulator.device, dtype=torch.float32)   
                    # need to unflip sign here
                self.points[(self.simulator.sign == -1)] += self.simulator.eng.winner_result()

            if track_eval:
                evals.append(float(Y_new.item()))

            delta = Y_new - Y_old
            
            delta_abs = delta.abs()
            max_surprise = torch.maximum(max_surprise, delta_abs)

            delta = torch.clamp(delta, min=-1, max=1)
            
            with torch.no_grad():
                scale = self.alpha * delta         
                for p, e in zip(self.params, accum_grad):
                    p.add_(e * scale)
                
        if track_eval:
            return max_surprise, evals
################################
########   train loop   ########            
################################

    def train(self, n_stages=100, games_per_stage=5000):
        print("pre-training simulation")
        mod_score, rand_score = self.simulator.benchmark(self.model, "greedy", num_games=20)
        print(f"model score: {mod_score}, baseline score: {rand_score}")
        
        evals = []

        for stage in tqdm(range(n_stages), desc="Outer Loop", leave=False):
            # anneal noise and temperature
            self.eps = self.eps_min + (self.eps0 - self.eps_min) * 2.0**(-stage / self.h_e)  
            self.temperature = self.t_min + (self.t0 - self.t_min) * 2.0**(-stage / self.h_t)
            
            for game in tqdm(range(games_per_stage), desc=f"Inner Loop {stage+1}", leave=False):
                if game == 0:
                    max_surprise, evals = self.playout(track_eval=True)
                else:
                    self.playout(track_eval=False)
            ################################ per stage report + actions ################################     
            print()
            self.surprises.append(float(max_surprise.item()))      # greatest swing in eval during the first game            
            print("max surprise of ", max_surprise)
            
            print(f"results of simulation {stage+1}")
            mod_score, rand_score = self.simulator.benchmark(self.model, "greedy",num_games=1000)    
            print(f"model score: {mod_score}, baseline score: {rand_score}")
            print()
            
            self.wr = 100 * mod_score / (mod_score + rand_score)
            if self.wr - self.last_wr < 0.03:
                self.no_improve += 1
                if self.no_improve > 5:
                    self.alpha = self.alpha_min + 0.7 * (self.alpha - self.alpha_min)
                    self.no_improve = 0
            self.last_wr = self.wr
            
            self.eval_traces.append(evals)
            if self.weights_file is not None:
                torch.save(self.model.state_dict(), self.weights_file)    # save weights to file after each stage
            ################################ end stage report + actions ################################     

        print(f"post-training simulation")
        mod_score, rand_score = self.simulator.benchmark(self.model, "greedy", num_games=10)
        print(f"model score: {mod_score}, baseline score: {rand_score}")

        print("total points each: ", self.points)
        self.record_results()

####################################
########   end train loop   ########            
####################################

if __name__ == "__main__":
    import argparse

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
    
    def valid_dropout(x):
        x = float(x)
        if not 0.0 <= x < 1.0:
            raise argparse.ArgumentTypeError("dropout must be in [0.0, 1.0)")
        return x

    parser = argparse.ArgumentParser(
        description="Process a str architecture name, float dropout rate, directory path and an optional filename."
    )

    parser.add_argument(
        "--directory", 
        type=valid_dir, 
        help="Optional path to an existing directory", 
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
        "--dropout",
        type=float,
        default=0.0,
        help="Dropout probability (default: 0.0)"
    )
    
    parser.add_argument(
        "--games",
        type=positive_int,
        default=5000,
        help="number of games per stage"
    )
    
    parser.add_argument(
        "--stages",
        type=positive_int,
        default=100,
        help="number of stages in training"
    )
    
    args = parser.parse_args()
    
    if args.architecture == "Conv":
        model = nardi_net.ConvNardiNet(dropout=args.dropout)
    elif args.architecture == "MLP":
        model = nardi_net.NardiNet(p_dropout=args.dropout)
    elif args.architecture == "ResNet":
        model = nardi_net.ResNardiNet()

    trainer = TDTrainer(model, weights_file=args.file, output_dir=args.directory)
    trainer.train(n_stages=args.stages, games_per_stage=args.games)

# ToDo:
    # add features for longest block and pieces behind it... maybe also pieces moving per dice?
    # maybe block robustness? ie how many pieces in the block have more than one piece on them or something similar
    # Pieces in home or pieces not yet in home... several training games far into simulations failed to detect that it was about to do Mars,
    # but this would be obvious from such features

# instead of solver - resignation threshold, so look at raw probability nodes in the model and based on that decide game outcomes...
# set this threshold pretty high, at least 90%, and see how AGZ training handled this I think they had a holdout set without this...

# experiment with lower lambda
# schedule learning rate ?
# train 2 models - 64 and 16 hidden units for one, 128 and 32 for the other

# pruning in lookahead search?

# coming to think that MCTS is a necessity
# also considering ViT approach