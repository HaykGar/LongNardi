import torch
import numpy as np
import torch.nn.functional as F

from functools import partial

from nardi_net import NardiNet
import nardi
import utils
from tqdm import tqdm

class Simulator:
    def __init__(self):
        self.device = torch.device("cpu") # "mps" if torch.backends.mps.is_available() else "cpu")
        self.eng = nardi.Engine()
        self.config = self.eng.config()
        
        self.sign = 1            # represents player's turn and change in perspective
        self.turn_num = 0        # turn number in current game
        
        self.win_rates = []
        
    def to_tensor(self, board_np):
        t = torch.from_numpy(board_np).to(device=self.device, dtype=torch.float32)
        t.div_(15.0)
        return t
    
    def eval_and_grad(self, model):
        eval = self.sign * model(self.to_tensor(self.eng.board_key()).unsqueeze(0))
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
        if options.shape[0] != 0:
            if eval_only:
                with torch.inference_mode():
                    evals = model(self.to_tensor(options))
            else:
                evals = model(self.to_tensor(options))

            best = options[int(evals.argmax().item())]
            self.eng.apply_board(best)

        self.advance_turn()
        
    def apply_noisy_move(self, K, eps, temperature, model):
        with torch.inference_mode():
            model.eval()
            if self.turn_num > K:
                self.apply_greedy_move(model) # advances turn internally
            else:
                options = self.eng.roll_and_enumerate()
                if options.shape[0] == 1:
                    self.eng.apply_board(options[0])
                elif options.shape[0] != 0:
                    evals = model(self.to_tensor(options)).detach().cpu()
                    evals = evals - evals.max()
                    priors = F.softmax(evals / temperature, dim=0).cpu().numpy()
                    
                    noisy_priors = utils.add_dirichlet_noise(priors, eps)
                    chosen_idx = np.random.choice(len(noisy_priors), p=noisy_priors)
                    chosen = options[chosen_idx]
                    self.eng.apply_board(chosen)
                
                self.advance_turn()        
            model.train()
            
    def apply_lookahead_move(self, model):
        starting_brd = self.eng.board_key()
        old_status = self.eng.status_str()

        children = self.eng.roll_and_enumerate()
        
        # FIXME REMOVE THIS
        dice = self.eng.dice()

        if len(children) == 0:
            self.advance_turn()
            return

        for child in children:
            if child[0][24] == 15:          # winning move from current position
                self.eng.apply_board(child)
                self.advance_turn()
                return
            
        # no immmediately winning moves at this point
        best_score = -999 # minimum possible score is -2
        grandchildren, counts, wins = self.eng.children_to_grandchildren(children)
        last_idx = 0
            
        with torch.inference_mode():
            gc_evals = model(self.to_tensor(grandchildren)).flatten().numpy() if len(grandchildren) else np.array([], dtype=np.float32)
            c_evals  = model(self.to_tensor(children)).flatten().numpy()
                
        for ch_idx in range(len(children)):
            avg_score = 0
            for flat_dice in range(21):
                dice = self.eng.flat_to_dice(flat_dice)
                prob = (1 / 36) if dice[0] == dice[1] else (1 / 18)
                cnt = counts[ch_idx][flat_dice]
                seg = gc_evals[last_idx:last_idx + cnt]
                
                if wins[ch_idx][flat_dice] > 0:
                    assert counts[ch_idx][flat_dice] == 0
                    avg_score += wins[ch_idx][flat_dice] * prob
                elif cnt > 0:
                    avg_score += prob * seg.max()
                else:   # no legal moves
                    avg_score += prob * -float(c_evals[ch_idx])

                last_idx += counts[ch_idx][flat_dice]
            # end for
            avg_score *= -1 # shift to player's perspective, since gc evals are from opponent perspective
            if avg_score > best_score:
                best_score = avg_score
                best_idx = ch_idx
        # end for  
        self.eng.apply_board(children[best_idx])
        self.advance_turn()
        
    def play_random_move(self):
        options = self.eng.roll_and_enumerate()
        if options.shape[0] != 0:               # at least 1 possible move   
            rand_idx = np.random.choice(len(options))
            self.eng.apply_board(options[rand_idx])
        self.advance_turn()
    
    def max_coverage_move(self):
        options = self.eng.roll_and_enumerate()
        if options.shape[0] != 0:               # at least 1 possible move   
            sq_occ = options[:, 2, -1]
            self.eng.apply_board(options[int(sq_occ.argmax().item())])
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
                return self.max_coverage_move
            elif strat == "human":
                return self.human_turn
        else:   # model is not None:
            if strat == "greedy":
                return partial(self.apply_greedy_move, model=model)
            elif strat == "lookahead":
                return partial(self.apply_lookahead_move, model=model)
            
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

    def benchmark(self, p1, p1_strat="greedy", p2=None, p2_strat="random", num_games = 100, position_setup=None, 
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
    # sim.benchmark(model, "lookahead", None, "human", 1, sim.config.withRandomEndgame)
    sim.play_with_graphics(opp_model=model, opp_strat="lookahead")
    
    # use cmd line args later for more generality