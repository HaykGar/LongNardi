import torch
import numpy as np
import torch.nn.functional as F

from functools import partial

from nardi_net import NardiNet
import nardi
from tqdm import tqdm

class Simulator:
    def __init__(self):
        self.device = torch.device("cpu") # "mps" if torch.backends.mps.is_available() else "cpu")
        self.eng = nardi.Engine()
        self.config = self.eng.config()
        
        self.sign = 1            # represents player's turn and change in perspective
        self.turn_num = 0        # turn number in current game
        
        self.win_rates = []
        
    def add_dirichlet_noise(self, priors, eps):
        n = len(priors)
        if(n <= 1):
            return priors
    
        alpha = np.clip(6.0 / n, 0.2, 0.8)
        noise = np.random.dirichlet([alpha] * len(priors))
        noisy_priors = (1 - eps) * priors + eps * noise
        noisy_priors = noisy_priors / np.sum(noisy_priors)
        return noisy_priors
            
    # FIXME why am I doing this? torch.optim... ?
    def eval_and_grad(self, model):
        eval = self.sign * model(self.eng.board_features())
        grad = torch.autograd.grad(eval, model.parameters(), retain_graph=False, create_graph=False)
        return eval, grad
    
    def reset(self, build_pos = None):
        self.sign = 1
        self.turn_num = 0
        if build_pos is None:
            self.eng.reset()
        else:
            build_pos()
           
    def advance_turn(self):
        self.sign *= -1
        self.turn_num += 1

    def apply_greedy_move(self, model, eval_only=True):
        options = self.eng.roll_and_enumerate() # no negation since we are looking from current perspective
        if len(options) != 0:
            if eval_only:
                with torch.inference_mode():
                    evals = model(options)
            else:
                evals = model(options)

            best = options[int(evals.argmax().item())]
            self.eng.apply_board(best.raw_data)

        self.advance_turn()
        
    def apply_noisy_move(self, K, eps, temperature, model, train=True):
        if not train:
            model.eval()
            
        if self.turn_num > K:
            self.apply_greedy_move(model) # advances turn internally
        else:
            options = self.eng.roll_and_enumerate()
            if len(options) == 1:
                self.eng.apply_board(options[0].raw_data)
            elif len(options) != 0:
                evals = model(options).detach().cpu()
                evals = evals - evals.max()
                priors = F.softmax(evals / temperature, dim=0).cpu().numpy()
                
                noisy_priors = self.add_dirichlet_noise(priors, eps)
                chosen_idx = np.random.choice(len(noisy_priors), p=noisy_priors)
                chosen = options[chosen_idx]
                self.eng.apply_board(chosen.raw_data)
            
            self.advance_turn()        
        
        if not train:
            model.train()

# FIXME (Lookahead move) COMPLETELY BROKEN DUE TO REPRESENTATION CHANGE
   
    def play_random_move(self):
        options = self.eng.roll_and_enumerate()
        if len(options) != 0:               # at least 1 possible move   
            rand_idx = np.random.choice(len(options))
            self.eng.apply_board(options[rand_idx].raw_data)
        self.advance_turn()
    
    def heuristic_move(self):
        options = self.eng.roll_and_enumerate()
        if len(options) != 0:               # at least 1 possible move 
            coverages = np.array([f.player.sq_occ for f in options])
            self.eng.apply_board(options[coverages.argmax()].raw_data)
        self.advance_turn()
        
    # TODO add more sophisticated heuristics and tie breakers for the above
    
    def human_turn(self):
        if self.eng.roll():
            self.eng.human_turn()
        self.advance_turn()
        
    def strat_to_func(self, model, strat):
        if model is None:
            if strat == "random":
                return self.play_random_move
            elif strat == 'heuristic':
                return self.heuristic_move
            elif strat == "human":
                return self.human_turn
        else:   # model is not None:
            if strat == "greedy":
                return partial(self.apply_greedy_move, model=model)
            elif strat == "lookahead":
                raise ValueError("lookahead move selection currently unsupported")
            
        print("invalid model and strat provided, no valid function call found")
        return None

    def simulate_game(self, p1_move, p2_move, swap_order=False, position_setup=None):    
        if p1_move is None or p2_move is None:
            print("failure initializing model and opp moves, aborting simulation")
            return None

        self.reset(position_setup)
        self.eng.Render()

        while self.eng.should_continue_game():
            is_p1_move = (self.sign == 1 and not swap_order) or (self.sign == -1 and swap_order)
            if is_p1_move:
                p1_move()
            else:
                p2_move()
            
        # game over, return true if first player won, false if second player won
        return is_p1_move

    def benchmark(self, p1, p1_strat="greedy", p2=None, p2_strat="heuristic", num_games = 100, position_setup=None, 
                  graphics=None):
        if num_games <= 0:
            num_games = 100

        p1_score = 0
        p2_score = 0
        
        p1_move = self.strat_to_func(p1, p1_strat)
        p2_move = self.strat_to_func(p2, p2_strat)
        
        if p1_strat == "human" or p2_strat == "human":
            if graphics=="terminal":
                self.eng.AttachNewTRW()
            elif graphics=="sfml":
                self.eng.AttachNewSFMLRW()

        for model_first in tqdm(range(2), desc="Outer Loop"):
            p1_first = bool(model_first)

            for game in tqdm(range(num_games), desc="Inner Loop", leave=False):
                p1_win = self.simulate_game(p1_move, p2_move, p1_first, position_setup)
                
                p1_score += p1_win * self.eng.winner_result()
                p2_score += (1 - p1_win) * self.eng.winner_result()
                                    
                if p2_strat == "human":
                    print(f"game {model_first*num_games + game} is over\n\n")   
        
        wr = p1_score / (p1_score + p2_score)
        print(f"win rate: {wr*100}%")
        self.win_rates.append(wr)  
        
        if p1_strat == "human" or p2_strat == "human":
            self.eng.DetachRW() 
                
        return p1_score, p2_score
   
    def play_with_graphics(self, opp_strat="human", opp_model=None):
        p1_move = self.human_turn
        p2_move = self.strat_to_func(opp_model, opp_strat)
        
        self.eng.AttachNewSFMLRW()
        p1_last = self.simulate_game(p1_move, p2_move)
        if self.eng.is_terminal():
            print(f"congratulations player {2-p1_last}!")
            print("You Win!")
        else:
            print("exited due to quit")
        
    
if __name__ == "__main__":
    model = NardiNet(64, 16)
    state_dict = torch.load("mw64_16.pt", map_location=torch.device('cpu'), weights_only=True)
    model.load_state_dict(state_dict)
    model.eval()
    sim = Simulator()
    
    sim.benchmark(model, num_games=1000)
    sim.benchmark(model, "greedy", None, "heuristic", 1000) #,sim.config.withRandomEndgame)
    # sim.play_with_graphics(opp_model=model, opp_strat="greedy")
    
    # use cmd line args later for more generality