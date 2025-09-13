import nardi
from nardi_net import NardiNet
from utils import SimulationMode

import torch
from torch import optim, nn
import torch.nn.functional as F
import numpy as np
import random

import inspect, sys
print("nardi file:", nardi.__file__)
print("engine dir has with_sim_mode?", 'with_sim_mode' in dir(nardi.Engine))
print("python:", sys.executable)

device = torch.device("cpu") #"mps" if torch.backends.mps.is_available() else "cpu")
print(f"Using device: {device}")

model = NardiNet()
# model.load_state_dict(torch.load('nardi_model.pt'))
model.to(device)

eng = nardi.Engine()

LAMBDA = 0.7        # Temporal Difference parameter
ALPHA = 0.01        # Learning Rate
K = 16              # Turns per game with Noise
sign = 1            # represents player's turn and change in perspective
max_surprise = 0    # greatest swing in position evaluation after one move
turn_num = 0        # turn number in current game

epsilons = [0.25, 0.25*0.5, 0.25*(0.5**2), 0.25*(0.5**3), 0]   # mixing parameters for noise
eps = epsilons[0]

temperature = 1 # fixme, anneal this

points = [0, 0]
surprises = []
eval_traces = []
win_rates = []

def to_tensor(board_np, dev=device):
    t = torch.from_numpy(board_np).to(device=dev, dtype=torch.float32)
    t.div_(15.0)
    return t

def eval_and_grad():
    eval = sign * model(to_tensor(eng.board_key()).unsqueeze(0))
    grad = torch.autograd.grad(eval, model.parameters(), retain_graph=False, create_graph=False)
    return eval, grad

def reset():
    global sign
    sign = 1
    global max_surprise
    max_surprise = 0
    global turn_num
    turn_num = 0
    
    eng.reset()
    
def print_board(brdkey):
    a = np.asarray(brdkey, dtype=np.int16)   # <- cast to signed
    
    brd = [ 0 for i in range(24) ]
    for i in range(24):
        brd[i] = a[0][i]
    
    for j in range(24):
        if a[1][j] != 0:
            assert a[0][j] == 0
            brd[j] = - a[1][j]

    ret_brd = []
    ret_brd.append(brd[:12])
    ret_brd.append(brd[12:])
    print(ret_brd)
    return ret_brd
    
def advance_turn():
    global sign
    sign *= -1
    global turn_num
    turn_num += 1

def apply_greedy_move(eval_only=False):
    options = eng.roll_and_enumerate() # no negation since we are looking from current perspective
    if options.shape[0] != 0:
        if eval_only:
            with torch.inference_mode():
                evals = model(to_tensor(options))
        else:
            evals = model(to_tensor(options))

        best = options[int(evals.argmax().item())]
        eng.apply_board(best)

    advance_turn()
    
def add_dirichlet_noise(priors, alpha=0.03):
    noise = np.random.dirichlet([alpha] * len(priors))
    noisy_priors = (1 - eps) * priors + eps * noise
    noisy_priors = noisy_priors / np.sum(noisy_priors)
    return noisy_priors
    
def apply_noisy_move():
    if turn_num > K:
        apply_greedy_move()
    else:
        options = eng.roll_and_enumerate()
        if options.shape[0] != 0:
            evals = model(to_tensor(options))
            evals = evals.cpu().detach().numpy()
            
            logits = evals ** (1.0 / temperature)
            priors = F.softmax(torch.tensor(logits), dim=0).detach().numpy()
            
            noisy_priors = add_dirichlet_noise(priors)
            chosen_idx = np.random.choice(len(noisy_priors), p=noisy_priors)
            chosen = options[chosen_idx]
            eng.apply_board(chosen)
            
        advance_turn()

def apply_lookahead_move(eval_only=True):
    starting_brd = eng.board_key()
    old_status = eng.status_str()

    children = eng.roll_and_enumerate()
    after_roll = eng.status_str()
    
    if len(children) == 0:
        advance_turn()
        return
    
    for child in children:
        if child[0][24] == 15:          # winning move from current position
            eng.apply_board(child)
            advance_turn()
            return
        
    # no immmediately winning moves at this point
    best_score = -999 # minimum possible score is -2

    grandchildren, counts, wins = eng.children_to_grandchildren(children)
    
    last_idx = 0
        
    with torch.inference_mode():
        model.eval()
        gc_evals = model(to_tensor(grandchildren)).flatten().numpy() if len(grandchildren) else np.array([], dtype=np.float32)
        c_evals  = model(to_tensor(children)).flatten().numpy()

        
    for ch_idx in range(len(children)):
        avg_score = 0
        for flat_dice in range(21):
            dice = eng.flat_to_dice(flat_dice)
            prob = (1 / 36) if dice[0] == dice[1] else (1 / 18)
            cnt = counts[ch_idx][flat_dice]
            seg = gc_evals[last_idx:last_idx + cnt]
            
            if wins[ch_idx][flat_dice] > 0:
                assert counts[ch_idx][flat_dice] == 0
                avg_score += wins[ch_idx][flat_dice] * prob
            else:
                avg_score += prob * (seg.max() if cnt > 0 else -float(c_evals[ch_idx]))
            
            last_idx += counts[ch_idx][flat_dice]

        # end for
        if avg_score > best_score:
            best_score = avg_score
            best_idx = ch_idx
        
    eng.apply_board(children[best_idx])
    advance_turn()
    
def play_random_move():
    options = eng.roll_and_enumerate()
    if options.shape[0] != 0:               # at least 1 possible move        
        eng.apply_board(random.choice(options))
    advance_turn()

    
def model_vs_random(i : int, greedy=True):
    mod_score = 0
    rand_score = 0  
    model.eval()
    
    model_move = apply_greedy_move if greedy else apply_lookahead_move
    num_games = 1000 if greedy else 10
    
    # played = 0
    
    for rand_first in range(2):
        reset()
        m_sign = 1 if not rand_first else -1

        for game in range(num_games):
            reset()
            rand_move = rand_first
            
            while not eng.is_terminal():
                m_move = (sign == m_sign)
                if rand_move:
                    play_random_move()
                else:
                    model_move()
                    
                rand_move = not rand_move
            # game over

            if(m_move):
                mod_score += eng.winner_result()
            else:
                rand_score += eng.winner_result()  
            
        
    model.train()
    print(f"random simulation {i} results: ")
    print(f"model: {mod_score}, random: {rand_score}")
    
    wr = mod_score / (mod_score + rand_score)
    print(f"win rate: {wr*100}%")
    win_rates.append(wr)

def validate_state(Y_new, g):
    """Add validation checks"""
    assert sign in [-1, 1], f"Invalid sign: {sign}"
    assert not torch.isnan(Y_new), "Y_new is NaN"
    assert not torch.isinf(Y_new), "Y_new is inf"
    
    # Check gradients
    for i, g_tensor in enumerate(g):
        if torch.isnan(g_tensor).any():
            print(f"NaN in gradient {i}")
        if torch.isinf(g_tensor).any():
            print(f"Inf in gradient {i}")  

print("pre-training") 
model_vs_random(0)

for stage in range(10):
    evals = []
    if stage < 5:
        eps = epsilons[0]
    elif stage < 10:
        eps = epsilons[1]
    elif stage < 15:
        eps = epsilons[2]
    elif stage < 20:
        eps = epsilons[3]
    else:
        eps = epsilons[4]
    
    for game in range(20):
        reset()
        
        Y_new, g = eval_and_grad()
        if game == 0:
            evals.append(float(Y_new.item()))
        
        accum_grad = [torch.zeros_like(p, device=device) for p in model.parameters()] # in shape of weights gradient

        while not eng.is_terminal():
            Y_old = Y_new
            
            torch._foreach_mul_(accum_grad, LAMBDA)        # e ← λ e  
            torch._foreach_add_(accum_grad, g)          # e ← e + ∇V_t-1  
            
            opponent_move = (sign == -1)
            
            apply_noisy_move()
            
            if not eng.is_terminal():
                Y_new, g = eval_and_grad()
            else:
                Y_new = torch.tensor(-sign * eng.winner_result(), device=device, dtype=torch.float32)   # need to unflip sign here
                points[opponent_move] += eng.winner_result()

            if game == 0:
                evals.append(float(Y_new.item()))

            delta = Y_new - Y_old
            
            if abs(delta) > max_surprise:
                max_surprise = abs(delta)
            
            with torch.no_grad():
                torch._foreach_add_(list(model.parameters()), accum_grad, alpha=ALPHA * float(delta)) 
                
    surprises.append(float(max_surprise.item()))      # greatest swing in eval during the last game            
    print("max surprise of ", max_surprise)
    model_vs_random(stage + 1)
    eval_traces.append(evals)
    
torch.save(model.state_dict(), "nardi_model.pt")    # save weights to file

print("post training random games: ")
model_vs_random(-1, greedy=False)
print("points each: ", points)

import matplotlib.pyplot as plt

# Plot max_surprise over training stages
plt.figure(figsize=(10, 4))
plt.plot(surprises)
plt.title('Max Surprise Per Stage')
plt.xlabel('Stage')
plt.ylabel('Max Surprise')
plt.grid(True)
plt.show()

# Plot eval trajectories for select games
for i, trace in enumerate(eval_traces):
    plt.figure(figsize=(8, 3))
    plt.plot(trace, marker='o')
    plt.title(f'Eval Trajectory - Game {i+1}')
    plt.xlabel('Move #')
    plt.ylabel('Eval')
    plt.grid(True)
    plt.show()


plt.figure(figsize=(12, 4))
plt.plot(surprises, marker='.', linewidth=0.5)
plt.title("Max Surprise per Game")
plt.xlabel("Game Index")
plt.ylabel("Max Surprise (ΔY)")
plt.grid(True)
plt.tight_layout()
plt.show()


# ToDo:
# need more exploration - dirichlet noise, softmaxing over distribution of move evalse with decaying 
#                         temperature, other ideas?
# solver enhancement ?