from sim_play import SimPlay
from nardi_net import NardiNet
import torch
import utils

import numpy as np

# # Load the file back into a NumPy object array
# loaded_arrays_obj = np.load('rand_wins.npy', allow_pickle=True)

# # Convert the NumPy object array back to a Python list
# loaded_arrays_list = loaded_arrays_obj.tolist()

# # You can now access and use the loaded arrays
# for move, position in enumerate(loaded_arrays_list[2]):
#     print(f"move {move}, position: ")
#     utils.print_board(position)

simulator = SimPlay()

def model_vs_model(model1, m1_name, model2, m2_name, ng=1000):
    print(m1_name, " vs ", m2_name)
    score1, score2 = simulator.benchmark(model1=model1, model_strat="lookahead", other_model=model2, opponent_strat="lookahead", num_games=ng)
    print(m1_name, " scored ", score1, " while ", m2_name, " scored ", score2)
    print(f"Respective win rates are {score1 / (score1 + score2)} and {score2 / (score1 + score2)}")
    print("\n\n")
    return score1, score2

def model_vs_heuristic(model, name, ng=1000):
    print(name, " vs heuristic")
    model_score, heur_score = simulator.benchmark(model1=model, model_strat="lookahead", opponent_strat="heuristic", num_games=ng)
    print(name, " scored ", model_score, " while the heuristic scored ", heur_score)
    print(f"Model win rates is {model_score / (model_score + heur_score)}")
    print("\n\n")
    return model_score, heur_score


m1 = NardiNet(64, 16)
m1_name = "64-16"
m1.load_state_dict(torch.load('mw64_16.pt'))
m1.eval()

m2 = NardiNet(128, 32)
m2_name = "128-32"
m2.load_state_dict(torch.load('mw128_32.pt'))
m2.eval()

heuristic_score = 0
m1_score = 0
m2_score = 0

d, h = model_vs_heuristic(m1, m1_name)
m1_score += d
heuristic_score += h

d, h = model_vs_heuristic(m2, m2_name)
m2_score += d
heuristic_score += h

d1, d2 = model_vs_model(m1, m1_name, m2, m2_name)
m1_score += d1
m2_score += d2

print(f"Final scores: ")
print(m1_name, ": ", m1_score, ", ", m2_name, ": ", m2_score, ", heuristic: ", heuristic_score)

# eng = simulator.eng
# sign = 1

# while not eng.is_terminal():
#     key = eng.board_key()
#     print("key: ", key)
#     print("board:")
#     utils.print_from_key(key, sign)
#     options = eng.roll_and_enumerate()
#     print("successfully called roll_and_enumerate")
#     print("options are\n", options)
#     if options.shape[0] != 0:
#         rand_idx = np.random.choice(len(options))
#         eng.apply_board(options[rand_idx])
#         print("made move")
#     sign *= -1

# print("final board: ")
# utils.print_from_key(eng.board_key(), sign)