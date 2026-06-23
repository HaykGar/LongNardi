from nardi_net import ResNardiNet
from sim_play import Simulator
import argparse, torch, os

def existing_file(filepath):
    if not os.path.exists(filepath):
        raise argparse.ArgumentTypeError(f"File not found: '{filepath}'")
    return filepath

parser = argparse.ArgumentParser()


parser.add_argument(
    "-f", 
    "--file", 
    type=existing_file, 
    required=True,
    help="Weights file for ResNardiNet model. Player 1 in the benchmarking"
)

parser.add_argument(
    "--baseline",
    type=existing_file,
    default="./weights/vzg0.pt"
)

parser.add_argument(
    "--lookahead",
    action="store_true",
    help="use 1-ply lookahead (expectimax) move selection strategy with fewer games total."
)

args = parser.parse_args()

player1 = ResNardiNet()
player1.load_state_dict(torch.load(args.file, map_location=torch.device('cpu'), weights_only=True))
player1.eval()

player2 = ResNardiNet()
player2.load_state_dict(torch.load(args.baseline, map_location=torch.device('cpu'), weights_only=True))
player2.eval()

sim = Simulator()

strat, ng = ("lookahead", 500) if args.lookahead else ('greedy', 5000)

sim.benchmark(player1, strat, player2, strat, num_games=ng)