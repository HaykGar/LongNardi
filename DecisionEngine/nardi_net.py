import os
import numpy as np
import torch
from torch import nn
import torch.nn.functional as F

import nardi  
    
class LegacyPipeline():
    def __init__(self, flatten=True):
        self.flatten = flatten
        self.kind = "legacy"
    
    def __call__(self, features : list[nardi.Features]):
        if torch.is_tensor(features):
            return features.float()
        if isinstance(features, np.ndarray):
            return torch.from_numpy(features).float()
        if isinstance(features, (list, tuple)):
            return torch.from_numpy(
                nardi.feature_batch_to_tensor(features, self.kind, self.flatten)
            ).float()
        return torch.from_numpy(
            nardi.features_to_tensor(features, self.kind, self.flatten)
        ).float()

class NardiNet(nn.Module):
    def __init__(self, n_h1, n_h2, feature_pipeline=None, input_dim=None, p_dropout=0, out_dim=4):
        super().__init__()
        
        self.pipeline = feature_pipeline if feature_pipeline is not None \
            else LegacyPipeline()
                
        if input_dim == None:
            eng = nardi.Engine()
            dummy = self.feature_to_tensor(eng.board_features())
            self.input_dim = dummy.shape[-1]
            
        else:
            self.input_dim = input_dim   
            
        self.out_dim = out_dim
        
        self.trunk = nn.Sequential(
            nn.Linear(self.input_dim, n_h1),
            nn.SiLU(),
            nn.Dropout(p=p_dropout),
            nn.Linear(n_h1, n_h2),
            nn.SiLU(),
            nn.Dropout(p=p_dropout),
            nn.Linear(n_h2, out_dim)
        )
        
        self.register_buffer("scores", torch.tensor([1.0, 2.0, -1.0, -2.0]))
        
    def feature_to_tensor(self, feat : nardi.Features):
        return self.pipeline(feat)
    
    def forward_tensor(self, x : torch.Tensor):
        logits = self.trunk(x)              

        if self.out_dim == 4:
            probs = torch.softmax(logits, dim=-1)
            return (probs * self.scores).sum(dim=-1)  # weighted sum
        else:
            return logits
                
    def value_from_tensor(self, x: torch.Tensor) -> torch.Tensor:
        """Evaluate from a pipeline-format feature tensor (i.e. the output of
        feature_to_tensor). This is the entry point traced for TorchScript export
        and called by the C++ MCTS target network. Subclasses with a conv stem
        override this to run their stem before the trunk."""
        return self.forward_tensor(x)

    def forward(self, feat: nardi.Features) -> torch.Tensor:
        return self.value_from_tensor(self.feature_to_tensor(feat))

class ConvPipeline(LegacyPipeline):
    def __init__(self):
        super().__init__(flatten=False)
        self.kind = "conv"

class ConvNardiNet(NardiNet):
    def __init__(self, num_channels=8, dropout=0, extra_conv=False, out_dim=4):
        pipeline = ConvPipeline()
        eng = nardi.Engine()
        dummy = pipeline(eng.board_features())
        super().__init__(64, 16, pipeline, input_dim=20*num_channels+dummy.shape[-2], 
                            p_dropout=dropout, out_dim=out_dim)
        
        # (B, 6, 24) board input processed as 1D sequence of 6 channels
        self.conv = nn.Conv1d(dummy.shape[-2], num_channels, kernel_size=5)
        if extra_conv:
            self.conv = nn.Sequential(
                self.conv, 
                nn.ReLU(),
                nn.Conv1d(num_channels, num_channels, kernel_size=5, padding=2)
            )
        self.norm = nn.LayerNorm(num_channels*20)
        self.relu = nn.ReLU()
        
    def value_from_tensor(self, feat : torch.Tensor):
        # feat: conv-pipeline tensor, shape (B, 6, 25)
        board = feat[:, :, :-1]
        scalars = feat[:, :, -1].squeeze(1)

        x = self.conv(board).flatten(1)
        x = self.norm(x)
        x = self.relu(x)

        x = torch.cat([x, scalars], dim=1)
        return self.forward_tensor(x)

    def forward(self, feat : nardi.Features):
        return self.value_from_tensor(self.feature_to_tensor(feat))


class ResidualBlock(nn.Module):
    def __init__(self, 
                 in_channels : int, 
                 out_channels : int,
                 downsample=True):
        super().__init__()
        if downsample:
            stride=2
        else:
            stride=1
            
        self.conv1 = nn.Conv1d(in_channels, out_channels, kernel_size=5, padding=2, stride=stride)             
        self.conv2 = nn.Conv1d(out_channels, out_channels, kernel_size=5, padding=2)
             
        self.proj = nn.Conv1d(in_channels, out_channels, kernel_size=1, stride=stride)
            
        self.relu = nn.ReLU()
            
    def forward(self, x : torch.Tensor):
        # conv block
        out = self.conv1(x)
        out = self.relu(out)
        out = self.conv2(out)
        # downsample input if needed
        x = self.proj(x)
        # skip connection and relu
        out = self.relu(out + x)
        return out
        
class ResNardiNet(NardiNet):
    def __init__(self, dropout=0, conv_out=8):
        pipeline = ConvPipeline()
        eng = nardi.Engine()
        dummy = pipeline(eng.board_features())
        super().__init__(64, 16, pipeline, input_dim=24*conv_out+dummy.shape[-2], p_dropout=dropout)
        
        self.res_block = ResidualBlock(in_channels=dummy.shape[-2], out_channels=conv_out, downsample=False)
        self.norm = nn.LayerNorm(24*conv_out)
        self.relu = nn.ReLU()
        
    def value_from_tensor(self, feat : torch.Tensor):
        # feat: conv-pipeline tensor, shape (B, 6, 25)
        board = feat[:, :, :-1]
        scalars = feat[:, :, -1].squeeze(1)

        x = self.res_block(board).flatten(1)
        x = self.norm(x)
        x = self.relu(x)

        x = torch.cat([x, scalars], dim=1)
        return self.forward_tensor(x)

    def forward(self, feat : nardi.Features):
        return self.value_from_tensor(self.feature_to_tensor(feat))

import struct

# Flat weight-blob format consumed by the hand-rolled C++ inference net
# (nardi_infer.{h,cpp}). Layout (all little-endian):
#   magic "NRDW" | version u32 | kind u32 | n_tensors u32
#   then per tensor: name_len u32 | name bytes | ndim u32 | dims u32... | float32 data
# `kind` selects the C++ architecture: 0 = NardiNet (MLP), 1 = ConvNardiNet,
# 2 = ResNardiNet.
_WEIGHT_MAGIC = b"NRDW"
_WEIGHT_VERSION = 1


def _model_kind(model):
    # ResNardiNet / ConvNardiNet both subclass NardiNet, so check most-derived first.
    if isinstance(model, ResNardiNet):
        return 2
    if isinstance(model, ConvNardiNet):
        return 1
    if isinstance(model, NardiNet):
        return 0
    raise TypeError(f"export_weights: unsupported model type {type(model).__name__}")


def export_weights(model, path):
    """Serialize a model's parameters + buffers to a flat .nardiw blob for the
    hand-rolled, torch-free C++ inference net. State-dict keys (e.g. trunk.0.weight,
    res_block.conv1.weight, scores) are written verbatim so the C++ side can look
    them up by name. Returns `path`."""
    was_training = model.training
    model.eval()
    kind = _model_kind(model)
    state = model.state_dict()
    with open(path, "wb") as fh:
        fh.write(_WEIGHT_MAGIC)
        fh.write(struct.pack("<III", _WEIGHT_VERSION, kind, len(state)))
        for name, tensor in state.items():
            arr = tensor.detach().to("cpu").contiguous().float().numpy()
            name_b = name.encode("utf-8")
            fh.write(struct.pack("<I", len(name_b)))
            fh.write(name_b)
            fh.write(struct.pack("<I", arr.ndim))
            for dim in arr.shape:
                fh.write(struct.pack("<I", int(dim)))
            fh.write(arr.astype("<f4", copy=False).tobytes())
    if was_training:
        model.train()
    return path


def export_target_network(model, path, device="cpu"):
    """Export `model` as a weight blob for the C++ MCTS target network. Kept for
    backwards compatibility with callers (e.g. Simulator.prepare_mcts); now writes
    a torch-free .nardiw blob via export_weights instead of TorchScript."""
    return export_weights(model, path)

class NardiSemble(ConvNardiNet):
    def __init__(self, models : list[NardiNet], freeze_models = True):
        super().__init__(out_dim=len(models))
        self.models = nn.ModuleList(models)
        
        if freeze_models:
            for m in self.models:
                for p in m.parameters():
                    p.requires_grad = False
                m.eval()

    def forward(self, feat : nardi.Features | list[nardi.Features]):
        weights = torch.softmax(super().forward(feat), dim=-1) # (B, n_models)
        
        model_outputs = torch.stack([m(feat) for m in self.models], dim=-1) # (B, n_models)
            
        return (model_outputs * weights).sum(dim=-1) # (B, )