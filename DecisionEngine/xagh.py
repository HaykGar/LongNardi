import nardi
from sim_play import Simulator
import models

# model = None

# print("Enter \'c\' to play aganst computer or press Enter to play against human.")
# computer = input().lower() == 'c'

# if computer:
#     print("Enter \'e\' to play against easy difficulty or press Enter to play against hard difficulty.")
#     easy = input().lower() == 'e'
#     model = models.mlp_model if easy else models.res_v2
   
model = models.res_v2 
strategy = "lookahead" if model else "human"

sim = Simulator(sleep_time=1)
sim.play_with_graphics(model, strategy)