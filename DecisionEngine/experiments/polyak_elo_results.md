# Polyak-averaged Lookahead models — greedy round-robin ELO

_Generated 2026-06-07 16:42_

## Setup

- Play: **greedy** (1-ply, no lookahead), CPU.
- Games per matchup: **5000** (2500 with each side moving first).
- Round robin over 5 models, 10 matchups.
- Scores are **points** (gammon/mars count 2), as Simulator.benchmark reports them.

## Models

- `avg10_ema9` ← `pol_avg_lookahead10_ema9.pt`
- `avg20_ema9` ← `pol_avg_lookahead20_ema9.pt`
- `avg10_ema99` ← `pol_avg_lookahead10_ema99.pt`
- `avg20_ema99` ← `pol_avg_lookahead20_ema99.pt`
- `chkpt45` ← `Checkpoints/Lookahead/chkpt45lookahead_res2.pt`

  Averaging recipe (training order = chkpt0..chkpt49):
  - `avg10_*` = forward EMA over chkpt40..49; `avg20_*` = over chkpt30..49.
  - `ema9` = beta 0.9, `ema99` = beta 0.99. Forward EMA weights the *oldest*
    checkpoint in the window most (e.g. beta=0.99 over 10 ⇒ chkpt40 ≈ 0.91,
    chkpt49 ≈ 0.01).

## Ranking (Bradley-Terry MLE, order-independent, mean=1500)

| rank | model | BT-Elo | EloTracker | win share | total pts |
|---|---|---:|---:|---:|---:|
| 1 | avg10_ema9 | 1503 | 1202 | 50.4% | 12802 |
| 2 | avg20_ema9 | 1502 | 1201 | 50.4% | 12773 |
| 3 | chkpt45 | 1500 | 1200 | 50.1% | 12743 |
| 4 | avg20_ema99 | 1498 | 1199 | 49.6% | 12518 |
| 5 | avg10_ema99 | 1497 | 1198 | 49.5% | 12544 |

## Head-to-head matrix (row's point win share vs column)

| | avg10_ema9 | avg20_ema9 | chkpt45 | avg20_ema99 | avg10_ema99 |
|---|---|---|---|---|---|
| **avg10_ema9** | — | 49.4% | 50.8% | 50.7% | 50.9% |
| **avg20_ema9** | 50.6% | — | 49.2% | 51.1% | 50.6% |
| **chkpt45** | 49.2% | 50.8% | — | 49.5% | 50.8% |
| **avg20_ema99** | 49.3% | 48.9% | 50.5% | — | 49.5% |
| **avg10_ema99** | 49.1% | 49.4% | 49.2% | 50.5% | — |

## Raw matchup point totals

| A | B | pts A | pts B | A win share |
|---|---|---:|---:|---:|
| avg10_ema9 | avg10_ema99 | 3202 | 3093 | 50.9% |
| avg10_ema9 | avg20_ema9 | 3143 | 3216 | 49.4% |
| avg10_ema9 | avg20_ema99 | 3216 | 3132 | 50.7% |
| avg10_ema9 | chkpt45 | 3241 | 3133 | 50.8% |
| avg10_ema99 | avg20_ema99 | 3170 | 3110 | 50.5% |
| avg10_ema99 | chkpt45 | 3138 | 3243 | 49.2% |
| avg20_ema9 | avg10_ema99 | 3224 | 3143 | 50.6% |
| avg20_ema9 | avg20_ema99 | 3211 | 3073 | 51.1% |
| avg20_ema9 | chkpt45 | 3122 | 3224 | 49.2% |
| avg20_ema99 | chkpt45 | 3203 | 3143 | 50.5% |

## Conclusion

**All 5 models are a statistical dead heat in greedy play.** The full BT-Elo
spread is 6 points (1497–1503); every head-to-head sits in 48.9%–51.1%. With
5000 games/matchup the standard error on a win share is ~0.7% (a bit larger
since scoring is point-weighted, gammon=2), so nothing here clears ~1.5 SE — no
result is significant.

What the (insignificant) ordering still suggests, and why it's consistent:
- `ema9` variants ≈ `chkpt45` (top three within 3 Elo). Beta 0.9 over the last
  10/20 keeps mass near the recent, already-good region, so it neither helps nor
  hurts vs the best single checkpoint.
- `ema99` variants are marginally *worse* (bottom two). Forward EMA with beta
  0.99 over a short window is dominated by the *oldest* checkpoint in the window
  (chkpt40 / chkpt30), which is slightly less trained — exactly the "early
  checkpoints drag the average" failure mode.

**Takeaway:** Polyak/EMA averaging of these checkpoints gives no measurable
gain over `chkpt45` for greedy play. This matches the prior intuition that
chkpt45 is about as good as it gets here. If averaging is worth pursuing, the
better-motivated knobs would be (a) uniform mean of the tail rather than EMA,
and (b) recency-weighted EMA (weight newest most) rather than the standard
forward EMA used here — but given a 6-Elo total spread, expected upside is small.
