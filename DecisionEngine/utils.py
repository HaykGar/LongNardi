import torch
import numpy as np

class SimulationMode:
    def __init__(self, engn):
        self.engine = engn
        
    def __enter__(self):
        self.engine.with_sim_mode()
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.engine.end_sim_mode()

def add_dirichlet_noise(priors, eps):
    n = len(priors)
    if(n <= 1):
        return priors
    
    alpha = np.clip(6.0 / n, 0.2, 0.8)
    noise = np.random.dirichlet([alpha] * len(priors))
    noisy_priors = (1 - eps) * priors + eps * noise
    noisy_priors = noisy_priors / np.sum(noisy_priors)
    return noisy_priors

def key_after_moves(bkey, sign, moves):
    if len(moves) == 0:
        return bkey

    plyr_row = (sign == -1) # 1 for black 0 for white
    for mv in moves:        
        # mv[0] is row from, mv[1] is col from, mv[2] is distance
        pos_from = 12*mv[0] + mv[1]
        pos_to = min(pos_from + mv[2], 24) # in case of removal, this should always be 24, the "removed" count
        dest_was_empty = False
        
        # decrement start pos
        if bkey[2][pos_from] > 0:
            bkey[2][pos_from] -= 1
        elif bkey[1][pos_from] < 0:
            bkey[1][pos_from] -= 1
        elif bkey[0][pos_from] > 0:
            bkey[0][pos_from] -= 1
        else:
            print(f"invalid move attempted, no pieces to move from this start coord")
            return bkey
        
        # increment destination pos
        if pos_to == 24:    # removing piece
            bkey[0][24] += 1
        elif bkey[1][pos_to] == 1:  # >= 2 pieces
            bkey[2][pos_to] += 1
        elif bkey[0][pos_to] == 1:  # >= 1 piece, not >= 2 so exactly 1
            bkey[1][pos_to] += 1
        else:
            bkey[0][pos_to] += 1
            dest_was_empty = True
        
        # set squares occupied
        if dest_was_empty and bkey[0][pos_from] > 0:
            bkey[2][-1] += 1 # another square occupied
        elif not dest_was_empty and bkey[0][pos_from] == 0:
            bkey[2][-1] -= 1 # vacated start square

        # set pieces not reached home
        if pos_from < 18 and pos_to >= 18:  # from outside home to home
            bkey[4][-1] -= 1    # another piece reached home

    return bkey

def to_visual_board(bkey, sgn):
    a = np.asarray(bkey, dtype=np.int8)
    if a.shape != (6, 25):
        print(f"invalid board key, expected shape (6, 25), got {a.shape}")
        return None
    elif sgn not in (-1, 1):
        print(f"invalid sign, expected 1 or -1, got {sgn}")
        return None
    
    a = a[:, :24]
    if not np.all((a[0] == 0) | (a[-1] == 0)):
        print(bkey)
        raise ValueError("invalid board: both players have checkers on the same point")
    else: 
        board = sgn * ( (a[0] + a[1] + a[2]) - (a[3] + a[4] + a[5]) ).reshape(2, 12)
        return board
        
def print_board(board):
    if board.shape != (2, 12):
        print(f"invalid board, expected shape (2, 12), got {board.shape}")
    else:
        ret = np.zeros((7, 12), dtype=int)
        
        for i in range(len(board[0])):
            if board[0][i] == 0:
                continue
            sign = abs(board[0][i]) / board[0][i]
            if(abs(board[0][i]) > 3):
                ret[0][-(i+1)] = sign
                ret[1][-(i+1)] = sign
                ret[2][-(i+1)] = sign * (abs(board[0][i]) - 2)
            else:
                for row in range(abs(board[0][i])):
                    ret[row][-(i+1)] = sign
                    
        for i in range(len(board[1])):
            if board[1][i] == 0:
                continue
            sign = abs(board[1][i]) / board[1][i]
            if(abs(board[1][i]) > 3):
                ret[-1][i] = sign
                ret[-2][i] = sign
                ret[-3][i] = sign * (abs(board[1][i]) - 2)
            else:
                for row in range(abs(board[1][i])):
                    ret[-(row + 1)][i] = sign
                    
        print("start")

        for num in range(11, -1, -1):
            print(num, end="\t")       
        print()     
        print("________________________________________________________________________________________________")
        print("________________________________________________________________________________________________")

                    
        for num in ret[0]:
            print(num, end="\t")
        print()
                
        for row in range(1, len(ret) - 1):
            for num in ret[row]:
                if num == 0:
                    print("", end="\t")
                else:
                    print(num, end="\t")
            print()
         
        for num in ret[-1]:
            print(num, end="\t")
        print()
        print("________________________________________________________________________________________________")
        print("________________________________________________________________________________________________")
        for num in range(12):
            print(num, end="\t") 
        print()
            
        print("end")
        print("\n\n\n")
        
def print_from_key(bkey, sign):
    board = to_visual_board(bkey, sign)
    print_board(board)