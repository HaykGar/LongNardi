import nardi
from sim_play import Simulator
import models

model = models.res_model

strategy = "lookahead"

sim = Simulator(sleep_time=0)
sim.play_with_graphics(model, strategy, from_endgame=True)