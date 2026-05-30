import os
import torch
from nardi_net import NardiNet, ConvNardiNet, ResNardiNet

weights_dir = os.path.join(os.path.dirname(__file__), "weights")

mlp_model = NardiNet(64, 16)
state_dict = torch.load(os.path.join(weights_dir, "mw64_16.pt"), map_location=torch.device('cpu'), weights_only=True)
mlp_model.load_state_dict(state_dict)
mlp_model.eval()

conv_model = ConvNardiNet()
conv_model.load_state_dict(torch.load(os.path.join(weights_dir, "conv_v1.pt"), map_location=torch.device('cpu'), weights_only=True))
conv_model.eval()

conv_2x_model = ConvNardiNet(extra_conv=True)
conv_2x_model.load_state_dict(torch.load(os.path.join(weights_dir, "deepconv_v1.pt"), map_location=torch.device('cpu'), weights_only=True))
conv_2x_model.eval()

conv_2x_v2 = ConvNardiNet(extra_conv=True)
conv_2x_v2.load_state_dict(torch.load(os.path.join(weights_dir, "deepconv_v2.pt"), map_location=torch.device('cpu'), weights_only=True))
conv_2x_v2.eval()

res_model = ResNardiNet()
res_model.load_state_dict(torch.load(os.path.join(weights_dir, "res.pt"), map_location=torch.device('cpu'), weights_only=True))
res_model.eval()

res_v2 = ResNardiNet()
res_v2.load_state_dict(torch.load(os.path.join(weights_dir, "res2.pt"), map_location=torch.device('cpu'), weights_only=True))
res_v2.eval()

res_v2_prime = ResNardiNet()
res_v2_prime.load_state_dict(torch.load(os.path.join(weights_dir, "res2prime.pt"), map_location=torch.device('cpu'), weights_only=True))
res_v2_prime.eval()

res_greedy = ResNardiNet()
res_greedy.load_state_dict(torch.load(os.path.join(weights_dir, "res_greedy.pt"), map_location=torch.device('cpu'), weights_only=True))
res_greedy.eval()
