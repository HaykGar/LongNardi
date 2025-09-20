import torch
import numpy as np
import torch.nn.functional as F

from functools import partial

from nardi_net import NardiNet
import nardi
import utils

class SimPlay:
    def __init__(self):
        self.device = torch.device("cpu") # "mps" if torch.backends.mps.is_available() else "cpu")
        # self.model = NardiNet(layer1_size, layer2_size)
        # self.load_file = load_file
        # if load_file != '':
        #     self.model.load_state_dict(torch.load(load_file))
        # self.model.to(self.device)
        self.eng = nardi.Engine()
        
        self.sign = 1            # represents player's turn and change in perspective
        self.max_surprise = 0    # greatest swing in position evaluation after one move in a game
        self.turn_num = 0        # turn number in current game
        
        self.win_rates = []
        self.opp_wins = []
        
    def to_tensor(self, board_np):
        t = torch.from_numpy(board_np).to(device=self.device, dtype=torch.float32)
        t.div_(15.0)
        return t
    
    def eval_and_grad(self, model):
        eval = self.sign * model(self.to_tensor(self.eng.board_key()).unsqueeze(0))
        grad = torch.autograd.grad(eval, model.parameters(), retain_graph=False, create_graph=False)
        return eval, grad
    
    def reset(self):
        self.sign = 1
        self.max_surprise = 0
        self.turn_num = 0
        self.eng.reset()
           
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
        after_roll = self.eng.status_str()

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

    def human_move(self):                       # very rudimentary, just for testing
        options = self.eng.roll_and_enumerate()
        if options.shape[0] > 1:
            print("you rolled: ", self.eng.dice())
            print("current board:")
            utils.print_from_key(self.eng.board_key(), self.sign)  
            
            moves = input("input separated by space from.row from.col distance from2.row etc: ").split()
            seqs = []
            for i in range(0, len(moves), 3):
                seqs.append([int(x) for x in moves[i:i+3]])
                                
            new_brd = utils.key_after_moves(self.eng.board_key(), self.sign, seqs)
            
            move_made = False
            
            for child in options:
                if (child == new_brd).all():
                    self.eng.apply_board(new_brd)
                    move_made = True
                    
            if not move_made:
                print("failed to find matching board among children")
                print("child options were:\n\n")
                for child in options:
                    utils.print_from_key(child, self.sign)
                print("playing first child")
                self.eng.apply_board(options[0])
            
        elif options.shape[0] == 1:
            print("forcing move")
            self.eng.apply_board(options[0])
        self.advance_turn()

    def benchmark(self, model1 : NardiNet, model_strat = "greedy", other_model=None, opponent_strat = "random", num_games = 0):
        mod_score = 0
        opp_score = 0  
        
        if model_strat == "greedy":
            model_move = partial(self.apply_greedy_move, model=model1)
        elif model_strat == "lookahead":
            model_move = partial(self.apply_lookahead_move, model=model1)  
        else:
            print("invalid model strategy, please select among greedy and lookahead")
            return
        
        opp_move = None

        if opponent_strat == "random":
            opp_move = self.play_random_move
        elif opponent_strat == "human":
            opp_move = self.human_move
        elif other_model is not None:
            if opponent_strat == "greedy":
                opp_move = partial(self.apply_greedy_move, model=other_model)
            elif opponent_strat == "lookahead":
                opp_move = partial(self.apply_lookahead_move, model=other_model)
        
        if opp_move is None:
            print("invalid opponent strategy, please select among random and human for computer or valid model options with model arg")
            return None
                
        if num_games <= 0:
            num_games = 100

        for model_first in range(2):
            model_is_first = bool(model_first)
            self.reset()
            m_sign = 1 if bool(model_is_first) else -1

            for game in range(num_games):
                self.reset()
                history = []

                while not self.eng.is_terminal():
                    history.append(utils.to_visual_board(self.eng.board_key(), self.sign))    # add current pos and player to move to history
                    is_mod_move = (self.sign == m_sign)
                    if is_mod_move:
                        model_move()
                    else:
                        opp_move()

                # game over
                if(is_mod_move):
                    mod_score += self.eng.winner_result()
                else:
                    opp_score += self.eng.winner_result()
                    history.append(utils.to_visual_board(self.eng.board_key(), self.sign))
                    self.opp_wins.append(history)       

        wr = mod_score / (mod_score + opp_score)
        print(f"win rate: {wr*100}%")
        self.win_rates.append(wr)       
        return mod_score, opp_score