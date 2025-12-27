from sim_play import Simulator
from nardi_net import NardiNet

import torch
import numpy as np
from tqdm import tqdm
import os
import matplotlib.pyplot as plt

#TODO make the hyperparams init args with default values

class TDTrainer:
    def __init__(self, model, output_dir="", weights_file=None):
    ###################################
    ######### hyperparameters #########     
    ###################################
        self.LAMBDA = 0.7           # Temporal Difference parameter
        self.alpha = 0.01           # Learning Rate
        self.alpha_0 = self.alpha   # initial learning rate
        self.alpha_min = 0.001      # minimum learning rate

        self.K = 24                 # Turns per game with Noise
        self.eps = 0.25             # mixing parameter for noise
        self.eps0 = self.eps        # initial epsilon
        self.eps_min = 0.1          # minimum allowed epsilon
        self.h_e = 15               # epsilon half-life          

        self.temperature = 1        # for selecting moves weighted by eval
        self.t0 = self.temperature  # initial temperature
        self.t_min = 0.1            # minimum temperature
        self.h_t = 20               # stages per temperature half-life    
        
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
        
    #######################################
    ######### model and simulator #########     
    #######################################
        self.simulator = Simulator()
        
        self.model = model
        if weights_file is not None and os.path.getsize(weights_file) != 0:
            self.model.load_state_dict(torch.load(weights_file))

        self.model.to(self.simulator.device)
        
    ####################################
    ######### helper functions #########     
    ####################################
    
    def record_results(self):   
        if self.output_dir is not None:
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
            
    def playout(self, track_eval : bool):
        
        self.simulator.reset()
        max_surprise = 0    # greatest swing in position evaluation after one move in a game
        evals = []

        Y_new, g = self.simulator.eval_and_grad(self.model)
        if track_eval:
            evals.append(float(Y_new.item()))
        
        accum_grad = [torch.zeros_like(p, device=self.simulator.device) for p in self.model.parameters()] # in shape of weights gradient

        while not self.simulator.eng.is_terminal():
            Y_old = Y_new
            
            torch._foreach_mul_(accum_grad, self.LAMBDA)        # e ← λ e  
            torch._foreach_add_(accum_grad, g)          # e ← e + ∇V_t-1  
            
            self.simulator.apply_noisy_move(self.K, self.eps, self.temperature, self.model)

            if not self.simulator.eng.is_terminal():
                Y_new, g = self.simulator.eval_and_grad(self.model)
            else:
                Y_new = torch.tensor(-self.simulator.sign * self.simulator.eng.winner_result(), device=self.simulator.device, dtype=torch.float32)   
                    # need to unflip sign here
                self.points[(self.simulator.sign == -1)] += self.simulator.eng.winner_result()

            if track_eval:
                evals.append(float(Y_new.item()))

            delta = Y_new - Y_old
            
            if abs(delta) > max_surprise:
                max_surprise = abs(delta)

            delta = torch.clamp(delta, min=-1, max=1)
            
            with torch.no_grad():
                torch._foreach_add_(list(self.model.parameters()), accum_grad, alpha=self.alpha * float(delta)) 
                # w = w + alpha*delta*accum_grad
                
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
        max_surprise = 0

        for stage in tqdm(range(n_stages), desc="Outer Loop"):
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
    # import argparse

    # #####################################    
    # ######### command line args #########     
    # #####################################

    # def positive_int(value):
    #     """Custom type for positive integers (>0)."""
    #     ivalue = int(value)
    #     if ivalue <= 0:
    #         raise argparse.ArgumentTypeError(f"{value} must be > 0")
    #     return ivalue

    # def valid_dir(dirpath):
    #     """Ensure provided path is a valid directory."""
    #     if dirpath is not None:
    #         if not os.path.exists(dirpath):
    #             os.makedirs(dirpath)
    #     return dirpath

    # def valid_file(filepath):
    #     if not os.path.exists(filepath):
    #         print(f"File not found: '{filepath}'. Creating a new empty file.")
    #         try:
    #             with open(filepath, 'w') as f:
    #                 pass
    #             print(f"Successfully created '{filepath}'.")
    #         except IOError as e:
    #             print(f"Error creating file '{filepath}': {e}")
    #             return None
        
    #     return filepath

    # parser = argparse.ArgumentParser(
    #     description="Process two positive integers, a directory path, and an optional filename."
    # )

    # parser.add_argument("layer1_size", type=positive_int, help="number of nodes in the first hidden layer")
    # parser.add_argument("layer2_size", type=positive_int, help="number of nodes in the second hidden layer")
    # parser.add_argument("--directory", type=valid_dir, help="Optional path to an existing directory", default=None)
    # parser.add_argument("-f", "--file", type=valid_file, help="Optional filename for loading network weights", default=None)

    # args = parser.parse_args()
    
    # model = NardiNet(args.layer1_size, args.layer2_size)
    
    # trainer = TDTrainer(model, args.directory, args.file)
    # trainer.train()
    
    model = NardiNet(64, 16)
    trainer = TDTrainer(model, weights_file="v2_64-16.pt")
    trainer.train(n_stages=100, games_per_stage=5000)

# ToDo:
    # add features for longest block and pieces behind it... maybe also pieces moving per dice?
    # maybe block robustness? ie how many pieces in the block have more than one piece on them or something similar
    # Pieces in home or pieces not yet in home... several training games far into simulations failed to detect that it was about to do Mars,
    # but this would be obvious from such features

# substantially larger 256-128 model seems to perform similarly to 64 - 16 but barely half as fast... smaller is better

# instead of solver - resignation threshold, so look at raw probability nodes in the model and based on that decide game outcomes...
    # set this threshold pretty high, at least 90%, and see how AGZ training handled this I think they had a holdout set without this...

# experiment with lower lambda
# schedule learning rate ?
# train 2 models - 64 and 16 hidden units for one, 128 and 32 for the other

# pruning in lookahead search?

# coming to think that MCTS is a necessity
# also considering ViT approach