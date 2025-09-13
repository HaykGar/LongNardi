import os
import numpy as np
import torch
from torch import nn

class NardiNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.flatten = nn.Flatten()
        self.trunk = nn.Sequential(
            nn.Linear(2 * 25, 128),
            nn.SiLU(),
            nn.Linear(128, 64),
            nn.SiLU(),
            nn.Linear(64, 4)  # 4 logits for outcome probabilities
        )
        # Scoring vector: +1, +2, -1, -2
        self.register_buffer("scores", torch.tensor([1.0, 2.0, -1.0, -2.0]))
        
    def forward(self, x: torch.Tensor) -> torch.Tensor: 
        # x: (B, 2, 25)
        x = x.float()
        x = self.flatten(x)
        logits = self.trunk(x)               # (B, 4)
        probs = torch.softmax(logits, dim=-1)
        expected = (probs * self.scores).sum(dim=-1)  # weighted sum
        return expected                      # shape: (B,)