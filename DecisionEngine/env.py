import torch
from sim_play import Simulator

class GameEnv:
    """A gym-style self-play environment for TD(lambda) training.

    Each env owns everything specific to one game in flight:
      - its own engine (via Simulator),
      - its own TD(lambda) eligibility trace (accum_grad),
      - and the computation of its own value and gradient (eval_and_grad).

    The trainer owns the *shared* model weights and decides how/when to apply
    updates; an env never touches the weights. `step` plays one move and hands
    back this game's update direction, so the trainer can average across envs
    before applying. This keeps weight ownership global and trajectory state local.

    API:
      env.reset()                -> initial value (also clears trace)
      env.step(move_fn)          -> (update, done, info)

    where move_fn(sim) applies one move with whatever strategy the caller wants
    (e.g. noisy/greedy), `update` is a per-parameter list (this game's
    clamp(delta) * eligibility_trace), and `info` carries the result on terminal.
    """

    __slots__ = ("model", "params", "build_pos", "LAMBDA", "device",
                 "sim", "accum_grad", "Y_new", "g",
                 "track", "eval_trace", "max_surprise")

    def __init__(self, model, params, *, build_pos=None, lam=0.7, track=False):
        self.model = model
        self.params = params
        self.build_pos = build_pos
        self.LAMBDA = lam

        self.sim = Simulator()
        self.device = self.sim.device

        # Eligibility trace + cached current value/gradient (this env's own copy,
        # so a sibling env's eval can never clobber it).
        self.accum_grad = [torch.zeros_like(p) for p in params]
        self.Y_new = None
        self.g = None

        self.track = track          # env 0 records a trajectory for plotting
        self.eval_trace = []
        self.max_surprise = 0.0

    def eval_and_grad(self, pos=None):
        """Evaluate the current (or given) position and its gradient w.r.t. params."""
        value = self.sim.eval_position(self.model, pos)
        grads = torch.autograd.grad(
            value, self.params,
            retain_graph=False, create_graph=False, allow_unused=False,
        )
        # grads are freshly allocated by autograd, so this env keeps its own copy.
        return value.squeeze(), list(grads)

    def reset(self):
        """Start a fresh game, clear the eligibility trace, cache the initial eval."""
            
        self.sim.reset(build_pos=self.build_pos)

        for e in self.accum_grad:
            e.zero_()

        self.Y_new, self.g = self.eval_and_grad()
        self.max_surprise = 0.0
        self.eval_trace = [float(self.Y_new.item())] if self.track else []
        return self.Y_new

    def step(self, move_fn):
        """Advance one move via move_fn(sim); return (update, done, info).

        update : list[Tensor] per trainable parameter, this game's TD(lambda)
                 update direction  clamp(delta) * eligibility_trace.
        done   : whether the game ended on this move.
        info   : {} normally; {'result', 'sign'} on termination (for scoring).
        """
        Y_old = self.Y_new

        # Eligibility trace: e <- lambda * e + grad(Y_old)
        for e, gi in zip(self.accum_grad, self.g):
            e.mul_(self.LAMBDA).add_(gi)

        with torch.no_grad():
            move_fn(self.sim)

        done = self.sim.eng.is_terminal()
        info = {}
        if not done:
            self.Y_new, self.g = self.eval_and_grad()
        else:
            result = self.sim.eng.winner_result()
            sign = self.sim.sign
            # sign is read from C++; after a completed terminal move the controller
            # has advanced to the losing side, so unflip to keep Y_new in the
            # previous eval frame.
            self.Y_new = torch.tensor(-sign * result, device=self.device, dtype=torch.float32)
            info = {"result": result, "sign": sign}

        delta = self.Y_new - Y_old
        self.max_surprise = max(self.max_surprise, float(delta.abs().item()))
        if self.track:
            self.eval_trace.append(float(self.Y_new.item()))

        # clamp(delta) * trace, mirroring the original per-move TD update; the
        # trainer averages these across envs before scaling by alpha.
        clamped = torch.clamp(delta, min=-1, max=1)
        update = [clamped * e for e in self.accum_grad]
        return update, done, info


class LookaheadGameEnv(GameEnv):
    """A self-play env whose TD targets come from 1-ply lookahead (expectimax)
    instead of the raw model value.

    We still train the raw value function V(s) = model(s): its gradient feeds the
    eligibility trace exactly as in GameEnv. What changes is the *bootstrap
    target*. For an afterstate a (the board after a player's move), the better
    estimate of its value is the lookahead value L(a) = the value of the best
    move under 1-ply search, which the C++ batch already computes as the chosen
    child's `child_value` (an expectation over the opponent's dice). So:

        delta_t = L_frozen(a_t) - V(a_{t-1})   (semi-gradient TD; target = lookahead)
        trace  += grad V(a_{t-1})

    The lookahead target is computed with a *frozen* target_model (a copy of the
    live model, updated only between stages). This prevents the target from
    chasing its own tail as weights update mid-stage — the same instability that
    DQN solved with a frozen target network. The live model is still used for
    eval_and_grad (gradient computation).
    """

    __slots__ = GameEnv.__slots__ + ("target_model",)

    def __init__(self, model, params, *, build_pos=None, lam=0.7, track=False,
                 target_model=None):
        super().__init__(model, params, build_pos=build_pos, lam=lam, track=track)
        # Frozen copy used for target computation. Set by LookaheadTDTrainer and
        # updated between stages; falls back to the live model if not provided
        # (preserves the original behaviour when used standalone).
        self.target_model = target_model if target_model is not None else model

    def _lookahead_move(self):
        """Roll, pick the best 1-ply move, and return (target, done, info).

        target : white-perspective value of the resulting afterstate — the
                 lookahead value of the chosen move under the frozen target_model
                 (or the true result on a win).
        """
        sim = self.sim
        # Frozen model for the target; live model for gradients elsewhere.
        target_model = self.target_model

        # No legal move for this roll: the turn passes with the board unchanged.
        # No lookahead is possible, so fall back to the naive model value.
        if not sim.eng.roll_has_children():
            with torch.inference_mode():
                target = float(sim.sign * target_model(sim.eng.board_features()).item())
            # The Controller no longer auto-advances on a no-move roll; confirm the pass.
            sim.eng.confirm_turn()
            return target, False, {}

        batch = sim.eng.make_lookahead_batch()
        if batch.num_children == 0:
            raise Exception("roll has children but batch has none")

        target, values = sim.lookahead_eval_and_values(target_model, batch)
        sim.eng.apply_best_lookahead(values)

        if sim.eng.is_terminal():
            # A winning move: the true game result replaces the estimate. sign has
            # flipped to the losing side, so unflip to stay in the same frame.
            result = sim.eng.winner_result()
            return float(-sim.sign * result), True, {"result": result, "sign": sim.sign}

        return target, False, {}

    def step(self, move_fn=None):
        # Y_old is the *prediction* V(a_{t-1}) cached from the previous step.
        Y_old = self.Y_new

        # Eligibility trace: e <- lambda * e + grad(V(a_{t-1}))
        for e, gi in zip(self.accum_grad, self.g):
            e.mul_(self.LAMBDA).add_(gi)

        target, done, info = self._lookahead_move()

        if not done:
            # Prediction at the new afterstate (its gradient feeds next step's trace).
            self.Y_new, self.g = self.eval_and_grad()
        else:
            self.Y_new = torch.tensor(target, device=self.device, dtype=torch.float32)

        delta = target - Y_old
        self.max_surprise = max(self.max_surprise, float(delta.abs().item()))
        if self.track:
            self.eval_trace.append(float(target))

        clamped = torch.clamp(delta, min=-1, max=1)
        update = [clamped * e for e in self.accum_grad]
        return update, done, info

