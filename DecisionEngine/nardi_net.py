import os
import numpy as np
import torch
from torch import nn

import nardi  

from abc import ABC, abstractmethod
class IFeaturePipeline(ABC):    
    def handle_list_case(self, features : list[nardi.Features]):
        xs = [self.__call__(f) for f in features]
        return torch.stack(xs, dim=0)
    
    @abstractmethod
    def __call__(self, features : list[nardi.Features]):
        pass
    
class LegacyPipeline(IFeaturePipeline):
    def __init__(self):
        super().__init__()
    
    def __call__(self, features : list[nardi.Features]):
        if isinstance(features, (list, tuple)):
            return self.handle_list_case(features)
        
        board = torch.cat([
            torch.from_numpy(features.player.occ).float(),
            torch.from_numpy(features.opp.occ).float()
        ], dim=0)
        
        other = torch.tensor([
            features.player.pieces_off, 
            features.opp.pieces_off,
            features.player.sq_occ, 
            features.opp.sq_occ,
            features.player.pieces_not_reached, 
            features.opp.pieces_not_reached
        ], dtype=torch.float32)
        
        return torch.cat([board, other.unsqueeze(1)], dim=1).div_(15.0)

class NardiNet(nn.Module):
    def __init__(self, n_h1, n_h2, feature_pipeline=None):
        super().__init__()
        
        self.feature_to_tensor = feature_pipeline if feature_pipeline is not None else LegacyPipeline()
        
        self.trunk = nn.Sequential(
            nn.Linear(6*25, n_h1),
            nn.SiLU(),
            nn.Linear(n_h1, n_h2),
            nn.SiLU(),
            nn.Linear(n_h2, 4)  # 4 logits for outcome probabilities
        )
        # Scoring vector: +1, +2, -1, -2
        self.register_buffer("scores", torch.tensor([1.0, 2.0, -1.0, -2.0]))   
        
    # does not achieve same performance as legacy model, something is wrong...        
        
    def forward(self, feat: nardi.Features) -> torch.Tensor:
        x = self.feature_to_tensor(feat).flatten(1)
        logits = self.trunk(x)              
        probs = torch.softmax(logits, dim=-1)
        return (probs * self.scores).sum(dim=-1)  # weighted sum

# def SpatialNardiNet(nn.Module):
#     def __init__(self):
#        super().__init__()
#        pass
#        # ... 
#        # ...

#     def feat_to_tensor(self, features : nardi.Features | list[nardi.Features]):
#         if isinstance(features, (list, tuple)):
#             return handle_list_case(features, self.feat_to_tensor)
        
#         board = torch.cat([
#             torch.from_numpy(features.player.occ).float(),
#             torch.from_numpy(features.opp.occ).float()
#         ], dim=0).div_(15.0)
        
#         global_info = torch.tensor([
#             features.player.pieces_off, 
#             features.opp.pieces_off,
#             features.player.pip_count, 
#             features.opp.pip_count,
#             features.player.pieces_not_reached, 
#             features.opp.pieces_not_reached
#         ], dtype=torch.float32)
        
#         return board, global_info
    
#     def forward(self, features : nardi.Features | list[nardi.Features]):
#         board, global_info = self.feat_to_tensor(features)
        
        
    