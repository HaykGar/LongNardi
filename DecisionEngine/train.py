import nardi
from nardi_net import NardiNet
import utils

import torch
from torch import optim, nn
import torch.nn.functional as F
import numpy as np
import random

device = torch.device("cpu") # "mps" if torch.backends.mps.is_available() else "cpu")
print(f"Using device: {device}")

model = NardiNet()
model.load_state_dict(torch.load('nardi_model.pt'))
model.to(device)

eng = nardi.Engine()

LAMBDA = 0.7        # Temporal Difference parameter
ALPHA = 0.001       # Learning Rate

K = 24              # Turns per game with Noise
eps = 0.25          # mixing parameter for noise
eps0 = eps          # initial epsilon
eps_min = 0.1       # minimum allowed epsilon
h_e = 15            # epsilon half-life          

temperature = 1     # for selecting moves weighted by eval
t0 = temperature    # initial temperature
t_min = 0.1         # minimum temperature
h_t = 20            # stages per temperature half-life

sign = 1            # represents player's turn and change in perspective
max_surprise = 0    # greatest swing in position evaluation after one move
turn_num = 0        # turn number in current game

points = [0, 0]
surprises = []
eval_traces = []
win_rates = []
rand_wins = []

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
    
def print_by_key(brdkey):
    a = np.asarray(brdkey, dtype=np.int16)
    
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

def apply_greedy_move(eval_only=True):
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
    
def add_dirichlet_noise(priors):
    n = len(priors)
    if(n <= 1):
        return priors
    
    alpha = np.clip(6.0 / n, 0.2, 0.8)
    noise = np.random.dirichlet([alpha] * len(priors))
    noisy_priors = (1 - eps) * priors + eps * noise
    noisy_priors = noisy_priors / np.sum(noisy_priors)
    return noisy_priors

def apply_noisy_move():
    with torch.inference_mode():
        model.eval()
        if turn_num > K:
            apply_greedy_move() # advances turn internally
        else:
            options = eng.roll_and_enumerate()
            if options.shape[0] == 1:
                eng.apply_board(options[0])
            elif options.shape[0] != 0:
                evals = model(to_tensor(options)).detach().cpu()
                # evals = evals.cpu().detach().numpy()
                evals = evals - evals.max()
                priors = F.softmax(evals / temperature, dim=0).cpu().numpy()
                
                # logits = evals ** (1.0 / temperature)
                # priors = F.softmax(torch.tensor(logits), dim=0).detach().numpy()
                
                noisy_priors = add_dirichlet_noise(priors)
                chosen_idx = np.random.choice(len(noisy_priors), p=noisy_priors)
                chosen = options[chosen_idx]
                eng.apply_board(chosen)
            
            advance_turn()        
        model.train()

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
        avg_score *= -1 # shift to player's perspective, since gc evals are from opponent perspective
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
    num_games = 100 if greedy else 10
        
    for rand_first in range(2):
        reset()
        m_sign = 1 if not rand_first else -1

        for game in range(num_games):
            reset()
            history = []
            rand_move = rand_first
            
            while not eng.is_terminal():
                history.append(utils.to_visual_board(eng.board_key(), sign))    # add current pos and player to move to history
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
                history.append(utils.to_visual_board(eng.board_key(), sign))
                rand_wins.append(history)
                 
    model.train()
    print(f"random simulation {i} results: ")
    print(f"model: {mod_score}, random: {rand_score}")
    
    wr = mod_score / (mod_score + rand_score)
    print(f"win rate: {wr*100}%")
    win_rates.append(wr) 

print("pre-training") 
model_vs_random(0)

################################
########   train loop   ########            
################################

for stage in range(1):#00):
    evals = []
    # anneal noise and temperature
    eps = eps_min + (eps0 - eps_min) * 2.0**(-stage / h_e)  
    temperature = t_min + (t0 - t_min) * 2.0**(-stage / h_t)
    
    for game in range(2):#0000):
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

################################
######## end train loop ########            
################################

print("post training random games: ")
model_vs_random(-1, greedy=False)
print("points each: ", points)

np.save('rand_wins.npy', np.array(rand_wins, dtype=object), allow_pickle=True)

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

# Plot win_rate over training stages
plt.figure(figsize=(10, 4))
plt.plot(win_rates)
plt.title('Win Rate Per Stage')
plt.xlabel('Stage')
plt.ylabel('Win Rate')
plt.grid(True)
plt.show()


# ToDo:
# add features for longest block and pieces behind it... 
    # requires changing boardkey in cpp and model shape and everywhere where shape checks happen
    # maybe block robustness? ie how many pieces in the block have more than one piece on them or something similar
# solver enhancement ?
# eventually may use eval for AGZ style training, but likely not