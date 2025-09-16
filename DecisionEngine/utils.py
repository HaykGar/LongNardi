import nardi 
import numpy as np

class SimulationMode:
    def __init__(self, engn):
        self.engine = engn
        
    def __enter__(self):
        self.engine.with_sim_mode()
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.engine.end_sim_mode()

def to_visual_board(bkey, sgn):
    a = np.asarray(bkey, dtype=np.int8)
    if a.shape != (2, 25):
        print(f"invalid board key, expected shape (2, 25), got {a.shape}")
        return None
    elif sgn not in (-1, 1):
        print(f"invalid sign, expected 1 or -1, got {sgn}")
        return None
    
    a = a[:, :24]
    if not np.all((a[0] == 0) | (a[1] == 0)):
        print(bkey)
        raise ValueError("invalid board: both players have checkers on the same point")
    else: 
        board = sgn * (a[0] - a[1]).reshape(2, 12)
        return board
        
def print_board(board):
    if board.shape != (2, 12):
        print(f"invalid board, expected shape (2, 25), got {board.shape}")
    else:
        for i in range(len(board[0]) - 1, -1, -1):
            print(board[0][i], end="\t")
        print("\n\n")
        for num in board[1]:
            print(num, end="\t")
        print("\n\n\n\n")