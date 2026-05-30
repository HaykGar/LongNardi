import torch
import numpy as np

from functools import partial

import time

import nardi
from tqdm import tqdm

class Simulator:
    def __init__(self, sleep_time=0):
        self.device = torch.device("cpu") # "mps" if torch.backends.mps.is_available() else "cpu")
        self.eng = nardi.Engine()
        self.config = self.eng.config()
        
        # self.sign = 1            # represents player's turn and change in perspective
        # self.turn_num = 0        # turn number in current game
        
        self.win_rates = []
        
        self.sleep_time = sleep_time

    @property
    def sign(self):
        return self.eng.sign()

    @property
    def turn_num(self):
        return self.eng.turn_num()
        
    def set_dice_from_input(self):
        dice = input("please enter desired dice: ").split()  # no extra checks for now
        return self.eng.set_and_enumerate(int(dice[0]), int(dice[1]))        
                
    def eval_position(self, model, pos=None):
        if pos is None:
            eval = self.sign * model(self.eng.board_features())
        else:
            eval = self.sign * model(pos)
        return eval

    def model_feature_args(self, model):
        pipeline = getattr(model, "pipeline", None)
        if pipeline is None:
            raise TypeError("Model must expose a .pipeline for C++ feature batching.")

        if not hasattr(pipeline, "kind") or not hasattr(pipeline, "flatten"):
            raise TypeError("Model pipeline must define .kind and .flatten for C++ feature batching.")

        if pipeline.kind not in {"legacy", "conv"}:
            raise ValueError(f"Unsupported C++ feature pipeline kind: {pipeline.kind!r}")

        if not isinstance(pipeline.flatten, bool):
            raise TypeError("Model pipeline .flatten must be a bool.")

        return (
            pipeline.kind,
            pipeline.flatten,
        )

    def ensure_model_on_device(self, model):
        for p in model.parameters():
            if p.device != self.device:
                raise RuntimeError(
                    f"Model is on {p.device}, but simulator device is {self.device}. "
                    "Call model.to(simulator.device) before lookahead evaluation."
                )
        return

    def evaluate_lookahead_batch(self, batch, model):
        if batch.num_eval_features == 0:
            return np.empty((0,), dtype=np.float32)

        kind, flatten = self.model_feature_args(model)
        self.ensure_model_on_device(model)
        # C++ owns the search tree and feature layout; Python only runs the model
        # over the flat feature batch and returns values to C++ for aggregation.
        x = torch.from_numpy(batch.tensor(kind, flatten)).to(self.device)
        with torch.inference_mode():
            values = model(x)
        return values.detach().cpu().numpy().astype(np.float32, copy=False)
    
    def lookahead_eval(self, model):
        batch = self.eng.make_lookahead_batch()
        if batch.num_children == 0:
            raise Exception("tried to do lookahead with no legal moves")

        values = self.evaluate_lookahead_batch(batch, model)
        return float(batch.child_values(values).max())
    
    def best_child_eval(self, model):
        children = self.eng.get_children()
        evals = model(children)
        return evals.max()    
    
    def reset(self, *, build_pos = None, scenario=None):
        if scenario:
            self.config.withScenario(scenario.player_idx, 
                                     scenario.board_config, 
                                     scenario.dice[0], scenario.dice[1])
        elif build_pos is None:
            self.eng.reset()
        else:
            build_pos()
           
    def advance_turn(self):
        time.sleep(self.sleep_time)

    def apply_greedy_move(self, model, options=None, eval_only=True):
        if options is None:
            options = self.eng.roll_and_enumerate() # no negation since we are looking from current perspective
        if len(options) != 0:
            if eval_only:
                with torch.inference_mode():
                    evals = model(options)
            else:
                evals = model(options)

            values = evals.detach().cpu().numpy().astype(np.float32, copy=False)
            self.eng.apply_greedy_board(values)
            self.advance_turn()
        else:
            self.advance_turn()
        
    def apply_noisy_move(self, K, eps, temperature, model, options=None):            
        if self.turn_num > K:
            self.apply_greedy_move(model, options) # advances turn internally
        else:
            if options is None:
                options = self.eng.roll_and_enumerate()
            if len(options) == 0:
                self.advance_turn()
                return

            with torch.inference_mode():
                values = model(options).detach().cpu().numpy().astype(np.float32, copy=False)
            # C++ duplicates the old softmax + Dirichlet-noise move sampling,
            # then applies the selected child through the normal controller.
            self.eng.apply_noisy_board(values, eps, temperature)
            self.advance_turn()

    def apply_lookahead_move(self, evaluator, options = None):
        if options is None and not self.eng.roll_has_children():
            self.advance_turn()
            return

        batch = self.eng.make_lookahead_batch()
        if batch.num_children == 0:
            self.advance_turn()
            return

        values = self.evaluate_lookahead_batch(batch, evaluator)
        # C++ aggregates the values, chooses the best child, and applies it.
        self.eng.apply_best_lookahead(values)
        self.advance_turn()

    def play_random_move(self, options=None):
        if options is None:
            options = self.eng.roll_and_enumerate()
            
        if len(options) != 0:               # at least 1 possible move   
            self.eng.apply_random_board()
            
        self.advance_turn()
    
    def heuristic_move(self, options=None):
        if options is None:
            options = self.eng.roll_and_enumerate()
        if len(options) != 0:               # at least 1 possible move 
            self.eng.apply_heuristic_board()
            return self.advance_turn()
        else:
            self.advance_turn()
        
    # TODO add more sophisticated heuristics and tie breakers for the above
    
    def human_turn(self, options=None):
        self.eng.human_turn(options is None)    # will roll fresh or use manually set dice
            
        time.sleep(self.sleep_time * 0.5)
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
                return partial(self.apply_greedy_move, model=model, eval_only=True)
            elif strat == "lookahead":
                return partial(self.apply_lookahead_move, evaluator=model)
            
        print("invalid model and strat provided, no valid function call found")
        return None

    def simulate_game(self, p1_move, p2_move, swap_order=False, position_setup=None, manual_dice=False):    
        if p1_move is None or p2_move is None:
            print("failure initializing model and opp moves, aborting simulation")
            return None
        
        moves = [p2_move, p1_move]
        score = [0, 0]
        
        dice_roll = self.set_dice_from_input if manual_dice else self.eng.roll_and_enumerate
                        
        run = True
        while run:
            self.reset(build_pos=position_setup)
            self.eng.Render()
            while self.eng.should_continue_game():
                is_p1_move = (self.sign == 1 and not swap_order) or (self.sign == -1 and swap_order)
                options = dice_roll()
                if not options:
                    self.advance_turn()
                else:
                    moves[is_p1_move](options=options)
                                
            self.eng.restart_or_quit()
            run = self.eng.should_continue_game()
            
            if self.eng.is_terminal():
                score[not is_p1_move] += self.eng.winner_result()
            
        return score

    def benchmark(self, p1, p1_strat="greedy", p2=None, p2_strat="heuristic", num_games = 100, position_setup=None, 
                  graphics=None):
        if num_games <= 0:
            num_games = 100
        
        scores = [0, 0]
        
        p1_move = self.strat_to_func(p1, p1_strat)
        p2_move = self.strat_to_func(p2, p2_strat)
        
        if p1_strat == "human" or p2_strat == "human":
            if graphics=="terminal":
                self.eng.AttachNewTRW()
            elif graphics=="sfml":
                self.eng.AttachNewSFMLRW()

        for model_first in tqdm(range(2), desc="Outer Loop", leave=False):
            p1_first = bool(model_first)

            for game in tqdm(range(num_games), desc="Inner Loop", leave=False):
                result = self.simulate_game(p1_move, p2_move, p1_first, position_setup)
                
                scores[0] += result[0]
                scores[1] += result[1]
                                    
                if p2_strat == "human":
                    print(f"game {model_first*num_games + game} is over\n\n")   
        
        wr = scores[0] / (scores[0] + scores[1])
        print(f"win rate: {wr*100}%")
        self.win_rates.append(wr)  
        
        if p1_strat == "human" or p2_strat == "human":
            self.eng.DetachRW() 
                
        return scores
   
    def play_with_graphics(self, 
                           opp_model=None, 
                           opp_strat="human", 
                           from_endgame=False, 
                           manual_dice=False, 
                           switch_order = False
    ):
        p1_move = self.human_turn if not switch_order else self.strat_to_func(opp_model, opp_strat)
        p2_move = self.strat_to_func(opp_model, opp_strat) if not switch_order else self.human_turn
        
        setup = self.config.withRandomEndgame if from_endgame else None
        
        self.eng.AttachNewSFMLRW()
        scores = self.simulate_game(p1_move, p2_move, position_setup=setup, manual_dice=manual_dice)
        if self.eng.is_terminal():
            if scores[0] > scores[1]:
                winner = 1
            elif scores[0] < scores[1]:
                winner = 2
            else:
                winner = None
                
            if winner is not None:
                print(f"congratulations player {winner}!")
                print("You Win!")
            else:
                print("Match ended in a draw")
                
            print(f"score: {scores[0]} - {scores[1]} ")
        else:
            print("exited due to quit")
      
if __name__ =="__main__":
    import models
    
    model = models.res_v2
    sim = Simulator(sleep_time=1)
    
    switch = len(input("Press Enter for normal play or input something to switch order ")) > 0
    
    sim.play_with_graphics(model, "lookahead", manual_dice=True, switch_order=switch)
