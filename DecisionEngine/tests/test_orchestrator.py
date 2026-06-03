"""Exercise the in-C++ match orchestrator (NardiEngine.advance() + the StepResult
state machine), which replaces the Python turn loop in sim_play.Simulator.

Checks:
  * full games complete for human-vs-bot, bot-vs-bot, and MCTS configurations,
    driven entirely by advance();
  * AwaitingHuman is only returned on the human's turn (and the human side is to
    move at that point);
  * the orchestrated model bot is actually playing well (beats the heuristic).

Run directly:  python tests/test_orchestrator.py
"""

import os
import random
import sys
import tempfile

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import nardi  # noqa: E402
from nardi_net import ResNardiNet, export_for_engine  # noqa: E402

WEIGHTS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "weights")
MAX_STEPS = 5000


def _engine_with_model():
    model = ResNardiNet()
    model.load_state_dict(torch.load(os.path.join(WEIGHTS_DIR, "res2.pt"),
                                     map_location="cpu", weights_only=True))
    model.eval()
    blob = tempfile.mktemp(suffix=".nardiw")
    export_for_engine(model, blob)
    eng = nardi.Engine()
    eng.reset()
    eng.load_target_network(blob)
    return eng, blob


def _play_one(eng, white, black, simulate_human=True):
    """Drive a single game to completion via advance(). Returns winner_result
    (1 or 2) or None if it didn't finish. Asserts AwaitingHuman invariants."""
    eng.configure_players(white, black)
    eng.reset()
    for _ in range(MAX_STEPS):
        res = eng.advance()
        if res == nardi.StepResult.GameOver:
            return eng.winner_result() if eng.is_terminal() else None
        if res == nardi.StepResult.AwaitingHuman:
            # Only the human side may be asked for input, and it must be to move.
            human_is_white = (white == nardi.Strategy.Human)
            assert eng.sign() == (1 if human_is_white else -1), \
                "AwaitingHuman returned when the human is not to move"
            n = eng.legal_move_count()
            assert n > 0
            eng.apply_human_move(random.randrange(n))
        # BotMoved / TurnPassed: nothing to do, just continue.
    return None


def test_human_vs_bot_completes():
    eng, blob = _engine_with_model()
    try:
        for black in (nardi.Strategy.Greedy, nardi.Strategy.Lookahead, nardi.Strategy.Heuristic):
            w = _play_one(eng, nardi.Strategy.Human, black)
            assert w in (1, 2), f"human vs {black} did not finish: {w}"
    finally:
        os.remove(blob)


def test_bot_vs_bot_completes_no_human_prompt():
    eng, blob = _engine_with_model()
    try:
        eng.configure_players(nardi.Strategy.Greedy, nardi.Strategy.Lookahead)
        eng.reset()
        saw_human = False
        winner = None
        for _ in range(MAX_STEPS):
            res = eng.advance()
            if res == nardi.StepResult.AwaitingHuman:
                saw_human = True
                break
            if res == nardi.StepResult.GameOver:
                winner = eng.winner_result()
                break
        assert not saw_human, "bot-vs-bot must never await a human move"
        assert winner in (1, 2)
    finally:
        os.remove(blob)


def test_mcts_orchestration_completes():
    eng, blob = _engine_with_model()
    try:
        eng.set_mcts_params(n_sims=20)
        w = _play_one(eng, nardi.Strategy.Mcts, nardi.Strategy.Heuristic)
        assert w in (1, 2)
    finally:
        os.remove(blob)


def _win_rate(eng, model_strat, opp_strat, n_games):
    """Model plays white in half, black in half, vs the opponent."""
    model_wins = opp_wins = 0
    for g in range(n_games):
        if g % 2 == 0:
            white, black, model_is_white = model_strat, opp_strat, True
        else:
            white, black, model_is_white = opp_strat, model_strat, False
        w = _play_one(eng, white, black)
        if w is None:
            continue
        winner_is_white = (w_white_won(eng))
        model_won = (winner_is_white == model_is_white)
        model_wins += model_won
        opp_wins += (not model_won)
    return model_wins, opp_wins


def w_white_won(eng):
    # After the game ends the controller has switched to the loser, so the winner
    # is !current_player; white is player idx 0.
    return eng.current_player() == 1  # current is black(1) => white won


def test_model_beats_heuristic():
    eng, blob = _engine_with_model()
    try:
        mw, ow = _win_rate(eng, nardi.Strategy.Greedy, nardi.Strategy.Heuristic, n_games=20)
        assert mw + ow > 0
        assert mw > ow, f"model (greedy) should beat heuristic, got {mw}-{ow}"
    finally:
        os.remove(blob)


if __name__ == "__main__":
    random.seed(0)
    eng, blob = _engine_with_model()
    try:
        for black in (nardi.Strategy.Greedy, nardi.Strategy.Lookahead):
            w = _play_one(eng, nardi.Strategy.Human, black)
            print(f"human vs {black}: winner_result={w}")
        eng.set_mcts_params(n_sims=20)
        print("mcts vs heuristic:", _play_one(eng, nardi.Strategy.Mcts, nardi.Strategy.Heuristic))
        mw, ow = _win_rate(eng, nardi.Strategy.Greedy, nardi.Strategy.Heuristic, n_games=20)
        print(f"greedy(model) vs heuristic over 20 games: {mw}-{ow}")
        print("ORCHESTRATOR OK")
    finally:
        os.remove(blob)
