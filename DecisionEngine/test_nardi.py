from sim_play import SimPlay
from nardi_net import NardiNet
import torch

# # Load the file back into a NumPy object array
# loaded_arrays_obj = np.load('rand_wins.npy', allow_pickle=True)

# # Convert the NumPy object array back to a Python list
# loaded_arrays_list = loaded_arrays_obj.tolist()

# # You can now access and use the loaded arrays
# for move, position in enumerate(loaded_arrays_list[2]):
#     print(f"move {move}, position: ")
#     utils.print_board(position)

simulator = SimPlay()

def model_vs_model(model1, m1_name, model2, m2_name, ng=100):
    print(m1_name, " vs ", m2_name)
    score1, score2 = simulator.benchmark(model1=model1, model_strat="lookahead", other_model=model2, opponent_strat="lookahead", num_games=ng, )
    print(m1_name, " scored ", score1, " while ", m2_name, " scored ", score2)
    print(f"Respective win rates are {score1 / (score1 + score2)} and {score2 / (score1 + score2)}")
    print("\n\n")
    return score1, score2

m1 = NardiNet(128, 64)
m1.load_state_dict(torch.load('nardi_model.pt'))
m1.eval()
m1_score = 0

m2 = NardiNet(64, 16)
m2.load_state_dict(torch.load('nardi_model2.pt'))
m2.eval()
m2_score = 0

m3 = NardiNet(256, 128)
m3.load_state_dict(torch.load('nardi_model_256128.pt'))
m3.eval()
m3_score = 0

d1, d2 = model_vs_model(m1, "128-64", m2, "64-16", ng=1)
m1_score += d1
m2_score += d2

d1, d3 = model_vs_model(m1, "128-64", m3, "256-128", ng=1)
m1_score += d1
m3_score += d3

d2, d3 = model_vs_model(m2, "64-16", m3, "256-128", ng=1)
m2_score += d2
m3_score += d3

print(f"Final scores: ")
print("128-64: ", m1_score, ", 64-16: ", m2_score, ", 256-128: ", m3_score)