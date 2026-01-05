from nardi_net import NardiNet, ConvNardiNet, ResNardiNet, NardiSemble
import torch

mlp_model = NardiNet(64, 16)
state_dict = torch.load("mw64_16.pt", map_location=torch.device('cpu'), weights_only=True)
mlp_model.load_state_dict(state_dict)
mlp_model.eval()

conv_model = ConvNardiNet()
conv_model.load_state_dict(torch.load("conv_v1.pt", map_location=torch.device('cpu'), weights_only=True))
conv_model.eval()

conv_2x_model = ConvNardiNet(extra_conv=True)
conv_2x_model.load_state_dict(torch.load("conv_v3.pt", map_location=torch.device('cpu'), weights_only=True))
conv_2x_model.eval()

res_model = ResNardiNet()
res_model.load_state_dict(torch.load("res.pt", map_location=torch.device('cpu'), weights_only=True))
res_model.eval()
