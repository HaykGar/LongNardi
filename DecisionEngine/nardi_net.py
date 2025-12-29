import os
import numpy as np
import torch
from torch import nn
import torch.nn.functional as F

import nardi  
    
class LegacyPipeline():
    def __init__(self, flatten=True):
        self.flatten=flatten
            
    def handle_list_case(self, features : list[nardi.Features]):
        xs = [self.process_single(f) for f in features]
        return torch.cat(xs, dim=0) # assumes batch dimension
        
    def process_board(self, features : nardi.Features):
        return torch.cat([
            torch.from_numpy(features.player.occ).float(),
            torch.from_numpy(features.opp.occ).float()
        ], dim=0)
        
    def process_scalars(self, features : nardi.Features):
        return torch.tensor([
            features.player.pieces_off, 
            features.opp.pieces_off,
            features.player.sq_occ, 
            features.opp.sq_occ,
            features.player.pieces_not_reached, 
            features.opp.pieces_not_reached
        ], dtype=torch.float32).unsqueeze(1)
        
    def process_single(self, features : nardi.Features):
        board = self.process_board(features)
        scalars = self.process_scalars(features)
        
        ret = torch.cat([board, scalars], dim=1).unsqueeze(0).div_(15.0)
        if self.flatten:
            return ret.flatten(-2)
        else:
            return ret
    
    def __call__(self, features : list[nardi.Features]):
        if isinstance(features, (list, tuple)):
            return self.handle_list_case(features)
        return self.process_single(features)



class NardiNet(nn.Module):
    def __init__(self, n_h1, n_h2, feature_pipeline=None, input_dim=None, p_dropout=0):
        super().__init__()
        
        self.pipeline = feature_pipeline if feature_pipeline is not None \
            else LegacyPipeline()
                
        if input_dim == None:
            eng = nardi.Engine()
            dummy = self.feature_to_tensor(eng.board_features())
            self.input_dim = dummy.shape[-1]
            
        else:
            self.input_dim = input_dim   
        
        self.trunk = nn.Sequential(
            nn.Linear(self.input_dim, n_h1),
            nn.SiLU(),
            nn.Dropout(p=p_dropout),
            nn.Linear(n_h1, n_h2),
            nn.SiLU(),
            nn.Dropout(p=p_dropout),
            nn.Linear(n_h2, 4)  # 4 logits for outcome probabilities
        )
        
        self.p_drop = p_dropout 
        
        self.register_buffer("scores", torch.tensor([1.0, 2.0, -1.0, -2.0]))
        
        
    def feature_to_tensor(self, feat : nardi.Features):
        return self.pipeline(feat).to(next(self.parameters()).device)
    
    def forward_tensor(self, x : torch.Tensor):
        logits = self.trunk(x)              
        probs = torch.softmax(logits, dim=-1)
        return (probs * self.scores).sum(dim=-1)  # weighted sum
                
    def forward(self, feat: nardi.Features) -> torch.Tensor:
        x = self.feature_to_tensor(feat)
        return self.forward_tensor(x)
    

class ConvPipeline(LegacyPipeline):
    def __init__(self):
        super().__init__(flatten=False)
        
    def process_scalars(self, features):
        return torch.tensor([
            features.player.pieces_off, 
            features.opp.pieces_off,
            features.player.pip_count, 
            features.opp.pip_count,
            features.player.pieces_not_reached, 
            features.opp.pieces_not_reached
        ], dtype=torch.float32).unsqueeze(1)

class ConvNardiNet(NardiNet):
    def __init__(self, num_channels=8, dropout=0, extra_conv=False):
        pipeline = ConvPipeline()
        eng = nardi.Engine()
        dummy = pipeline(eng.board_features())
        super().__init__(64, 16, ConvPipeline, input_dim=20*num_channels+dummy.shape[-2], p_dropout=dropout)
        
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
        
    def forward(self, feat : nardi.Features):
        feat = self.feature_to_tensor(feat) # shape (B, 6, 25)    
        board = feat[:, :, :-1]
        scalars = feat[:, :, -1].squeeze(1)
        
        x = self.conv(board).flatten(1)
        x = self.norm(x)
        x = self.relu(x)
        
        x = torch.cat([x, scalars], dim=1)
        return self.forward_tensor(x)
    

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
        
    def forward(self, feat : nardi.Features):
        feat = self.feature_to_tensor(feat) # shape (B, 6, 25)    
        board = feat[:, :, :-1]
        scalars = feat[:, :, -1].squeeze(1)
        
        x = self.res_block(board).flatten(1)
        x = self.norm(x)
        x = self.relu(x)
        
        x = torch.cat([x, scalars], dim=1)
        return self.forward_tensor(x)
        