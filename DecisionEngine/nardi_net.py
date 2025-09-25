import os
import numpy as np
import torch
from torch import nn

class NardiNet(nn.Module):
    def __init__(self, n_h1, n_h2):
        super().__init__()
        self.flatten = nn.Flatten()
        self.trunk = nn.Sequential(
            nn.Linear(6 * 25, n_h1),
            nn.SiLU(),
            nn.Linear(n_h1, n_h2),
            nn.SiLU(),
            nn.Linear(n_h2, 4)  # 4 logits for outcome probabilities
        )
        # Scoring vector: +1, +2, -1, -2
        self.register_buffer("scores", torch.tensor([1.0, 2.0, -1.0, -2.0]))
        
    def forward(self, x: torch.Tensor) -> torch.Tensor: 
        # x: (B, 6, 25)
        x = x.float()
        x = self.flatten(x)
        logits = self.trunk(x)               # (B, 4)
        probs = torch.softmax(logits, dim=-1)
        expected = (probs * self.scores).sum(dim=-1)  # weighted sum
        return expected                      # shape: (B,)