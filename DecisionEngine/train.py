import nardi
from sim_play import SimPlay
from nardi_net import NardiNet

import torch
import numpy as np
from tqdm import tqdm
import os
import argparse
import matplotlib.pyplot as plt

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

parser = argparse.ArgumentParser(
    description="Process two positive integers, a directory path, and an optional filename."
)

parser.add_argument("layer1_size", type=positive_int, help="number of nodes in the first hidden layer")
parser.add_argument("layer2_size", type=positive_int, help="number of nodes in the second hidden layer")
parser.add_argument("--directory", type=valid_dir, help="Optional path to an existing directory", default=None)
parser.add_argument("-f", "--file", type=valid_file, help="Optional filename for loading network weights", default=None)

args = parser.parse_args()

#################################
######### training vars #########     
#################################

LAMBDA = 0.7        # Temporal Difference parameter
alpha = 0.01        # Learning Rate
alpha_0 = alpha     # initial learning rate
alpha_min = 0.001   # minimum learning rate

K = 24              # Turns per game with Noise
eps = 0.25          # mixing parameter for noise
eps0 = eps          # initial epsilon
eps_min = 0.1       # minimum allowed epsilon
h_e = 15            # epsilon half-life          

temperature = 1     # for selecting moves weighted by eval
t0 = temperature    # initial temperature
t_min = 0.1         # minimum temperature
h_t = 20            # stages per temperature half-life

points = [0, 0]
eval_traces = []
surprises = []

weights_file = args.file
output_dir = args.directory

################################
########   train loop   ########            
################################

simulator = SimPlay()
last_wr = 0
no_improve = 0

model = NardiNet(args.layer1_size, args.layer2_size)
if weights_file is not None and os.path.getsize(weights_file) != 0:
    model.load_state_dict(torch.load(weights_file))

model.to(simulator.device)

print("pre-training random simulation")
mod_score, rand_score = simulator.benchmark(model1=model, model_strat="greedy", num_games=20)
print(f"model score: {mod_score}, random score: {rand_score}")

for stage in tqdm(range(100), desc="Outer Loop"):
    evals = []
    # anneal noise and temperature
    eps = eps_min + (eps0 - eps_min) * 2.0**(-stage / h_e)  
    temperature = t_min + (t0 - t_min) * 2.0**(-stage / h_t)
    
    for game in tqdm(range(5000), desc=f"Inner Loop {stage+1}", leave=False):
        simulator.reset()
        
        Y_new, g = simulator.eval_and_grad(model)
        if game == 0:
            evals.append(float(Y_new.item()))
        
        accum_grad = [torch.zeros_like(p, device=simulator.device) for p in model.parameters()] # in shape of weights gradient

        while not simulator.eng.is_terminal():
            Y_old = Y_new
            
            torch._foreach_mul_(accum_grad, LAMBDA)        # e ← λ e  
            torch._foreach_add_(accum_grad, g)          # e ← e + ∇V_t-1  
            
            simulator.apply_noisy_move(K, eps, temperature, model)

            if not simulator.eng.is_terminal():
                Y_new, g = simulator.eval_and_grad(model)
            else:
                Y_new = torch.tensor(-simulator.sign * simulator.eng.winner_result(), device=simulator.device, dtype=torch.float32)   
                    # need to unflip sign here
                points[(simulator.sign == -1)] += simulator.eng.winner_result()

            if game == 0:
                evals.append(float(Y_new.item()))

            delta = Y_new - Y_old
            
            if abs(delta) > simulator.max_surprise:
                max_surprise = abs(delta)

            delta = torch.clamp(delta, min=-1, max=1)
            
            with torch.no_grad():
                torch._foreach_add_(list(model.parameters()), accum_grad, alpha=alpha * float(delta)) # w = w + alpha*delta*accum_grad
    
    ################################ per stage report + actions ################################     
    print()
    surprises.append(float(max_surprise.item()))      # greatest swing in eval during the last game            
    print("max surprise of ", max_surprise)
    
    print(f"results of simulation {stage+1}")
    mod_score, rand_score = simulator.benchmark(model1=model, num_games=1000)    
    print(f"model score: {mod_score}, random score: {rand_score}")
    print()
    
    wr = 100 * mod_score / (mod_score + rand_score)
    if wr - last_wr < 0.1:
        no_improve += 1
        if no_improve > 5:
            alpha = alpha_min + 0.7 * (alpha - alpha_min)
            no_improve = 0
    last_wr = wr
    
    eval_traces.append(evals)
    if weights_file is not None:
        torch.save(model.state_dict(), weights_file)    # save weights to file after each stage
    ################################ end stage report + actions ################################     


################################
######## end train loop ########            
################################

print(f"post-training simulation with lookahead")
mod_score, rand_score = simulator.benchmark(model1=model, model_strat="lookahead", num_games=10)
print(f"model score: {mod_score}, random score: {rand_score}")

print("total points each: ", points)

# np.save('rand_wins.npy', np.array(simulator.opp_wins, dtype=object), allow_pickle=True)

if output_dir is not None:
    
    win_rates = simulator.win_rates

    # Plot max_surprise over training stages
    plt.figure(figsize=(10, 4))
    plt.plot(surprises)
    plt.title('Max Surprise Per Stage')
    plt.xlabel('Stage')
    plt.ylabel('Max Surprise')
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, 'max_surprise_per_stage.png'))
    plt.close()  # Close the figure to free up memory

    # Plot eval trajectories for select games
    for i, trace in enumerate(eval_traces):
        plt.figure(figsize=(8, 3))
        plt.plot(trace, marker='o')
        plt.title(f'Eval Trajectory - Game {i+1}')
        plt.xlabel('Move #')
        plt.ylabel('Eval')
        plt.grid(True)
        plt.savefig(os.path.join(output_dir, f'eval_trajectory_game_{i+1}.png'))
        plt.close()

    # Plot win_rate over training stages
    plt.figure(figsize=(10, 4))
    plt.plot(win_rates)
    plt.title('Win Rate Per Stage')
    plt.xlabel('Stage')
    plt.ylabel('Win Rate')
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, 'win_rate_per_stage.png'))
    plt.close()

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