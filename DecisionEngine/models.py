import os
import torch
from nardi_net import NardiNet, ConvNardiNet, ResNardiNet

weights_dir = os.path.join(os.path.dirname(__file__), "weights")

mlp_model = NardiNet(64, 16)
state_dict = torch.load(os.path.join(weights_dir, "mlp.pt"), map_location=torch.device('cpu'), weights_only=True)
mlp_model.load_state_dict(state_dict)
mlp_model.eval()

res_v2 = ResNardiNet()
res_v2.load_state_dict(torch.load(os.path.join(weights_dir, "res2.pt"), map_location=torch.device('cpu'), weights_only=True))
res_v2.eval()

vzgo = ResNardiNet()
vzgo.load_state_dict(torch.load(os.path.join(weights_dir, "polAvg10_lookahead.pt"), map_location=torch.device('cpu'), weights_only=True))
vzgo.eval()