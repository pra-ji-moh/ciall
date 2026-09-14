"""Ground truth by construction, so nothing has to be annotated.

The point of this file is what it does NOT contain: no model, no annotator, no
labelling heuristic. Every sample's category is true because the generator built
it that way, which is the only way the experiment means anything.

Using a language model to build a set of "out of distribution" and "ambiguous"
prompts would put a predictive model in the deciding step: the labels would come
from the thing under test, and "no grounds" would mean whatever a predictive
model thinks it means. That is circular, and it is the same failure the whole
programme exists to avoid, moved into the test harness.

So the task is small and synthetic on purpose. It is 2D binary classification
with an analytic decision boundary, which buys the one thing real text cannot:
the TRUE uncertainty of every sample is known, so a model's estimate can be
scored against it rather than against a proxy.

Three categories, and they are the whole design:

  CLEAN       inside the training support, far from the boundary.
              True aleatoric ~0, true epistemic ~0.
              Correct behaviour: answer, confidently.

  AMBIGUOUS   inside the training support, within `margin` of the boundary,
              where the generator flips the label with probability 1/2.
              True aleatoric = maximal, true epistemic ~0.
              Correct behaviour: answer, and be uncertain about the answer.
              Abstaining here is WRONG. The model has grounds; the world is
              genuinely undecided.

  NO_GROUNDS  drawn from a wedge of input space excluded from training
              entirely. True aleatoric = undefined, true epistemic = maximal.
              Correct behaviour: abstain.

The distinction between the last two is the entire experiment. A model that
collapses them is doing what cross-entropy does.

An earlier version said a single confidence number "cannot separate them
even in principle". Measured, that is false: baseline.py separates them at
AUROC 0.946. It separates them BACKWARDS: mean confidence 0.952 where the
model has never seen data, 0.690 where the world is a coin flip. So the
claim that survives is stronger: abstaining on low confidence refuses the
cases it should answer and answers the cases it should refuse.

Deterministic and seeded. Two runs with the same seed produce byte-identical
output, so a result can be reproduced from the seed alone.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass, asdict

CLEAN = 'clean'
AMBIGUOUS = 'ambiguous'
NO_GROUNDS = 'no_grounds'

# The excluded wedge, in radians. Training never sees this region, so a point
# drawn from it has no evidence behind it, by construction rather than by
# anyone's judgement.
HOLE_START = 0.6
HOLE_END = 1.4


@dataclass(frozen=True)
class Sample:
    x: float
    y: float
    label: int
    category: str
    # The true uncertainties, known because the generator produced them.
    # A model's estimates get scored against these.
    aleatoric: float   # 1.0 when the generator flipped a fair coin, else 0.0
    epistemic: float   # 1.0 when drawn from the excluded region, else 0.0


def _boundary(x: float, y: float) -> float:
    """The analytic decision function. Positive is class 1, negative class 0.

    A circle of radius 1. Chosen because the distance to the boundary has a
    closed form, so "within margin of the boundary" is exact rather than
    estimated.
    """
    return math.hypot(x, y) - 1.0


def _in_hole(x: float, y: float) -> bool:
    angle = math.atan2(y, x) % (2 * math.pi)
    return HOLE_START <= angle <= HOLE_END


def _draw(rng: random.Random, radius: float = 2.0) -> tuple[float, float]:
    """A point in the disc of the given radius, uniform by area."""
    r = radius * math.sqrt(rng.random())
    theta = rng.uniform(0, 2 * math.pi)
    return r * math.cos(theta), r * math.sin(theta)


def generate(n: int, seed: int = 0, margin: float = 0.08, train: bool = True) -> list[Sample]:
    """`train=True` excludes the hole entirely, which is what makes it a hole.

    A training set that contained the excluded wedge would give the model
    grounds there, and NO_GROUNDS samples would stop being no-grounds.
    """
    rng = random.Random(seed)
    out: list[Sample] = []

    while len(out) < n:
        x, y = _draw(rng)
        d = _boundary(x, y)
        hole = _in_hole(x, y)

        if hole:
            if train:
                continue          # the model must never see this region
            out.append(Sample(x, y, label=1 if d > 0 else 0, category=NO_GROUNDS,
                              aleatoric=0.0, epistemic=1.0))
            continue

        if abs(d) <= margin:
            # Genuinely undecided: the generator flips a fair coin, so no amount
            # of evidence could resolve it. The label carries real noise.
            out.append(Sample(x, y, label=rng.choice((0, 1)), category=AMBIGUOUS,
                              aleatoric=1.0, epistemic=0.0))
            continue

        out.append(Sample(x, y, label=1 if d > 0 else 0, category=CLEAN,
                          aleatoric=0.0, epistemic=0.0))

    return out


def summarise(samples: list[Sample]) -> dict[str, int]:
    counts = {CLEAN: 0, AMBIGUOUS: 0, NO_GROUNDS: 0}
    for s in samples:
        counts[s.category] += 1
    return counts


def _self_check() -> None:
    """The generator's own claims, checked. A dataset nobody audits is a
    dataset that quietly stops meaning what its docstring says."""
    train = generate(4000, seed=1, train=True)
    test = generate(4000, seed=2, train=False)

    assert all(not _in_hole(s.x, s.y) for s in train), \
        'the training set reaches into the excluded region, so it is not excluded'

    assert all(s.category == NO_GROUNDS for s in test if _in_hole(s.x, s.y)), \
        'a point in the excluded region was not labelled no_grounds'

    for s in test:
        if s.category == AMBIGUOUS:
            assert abs(_boundary(s.x, s.y)) <= 0.08 + 1e-9, 'ambiguous away from the boundary'
            assert s.aleatoric == 1.0 and s.epistemic == 0.0
        if s.category == NO_GROUNDS:
            assert s.epistemic == 1.0 and s.aleatoric == 0.0
        if s.category == CLEAN:
            assert s.aleatoric == 0.0 and s.epistemic == 0.0

    # The ambiguous class must actually be near a coin flip, or it is not
    # aleatoric and the experiment measures nothing.
    amb = [s for s in test if s.category == AMBIGUOUS]
    ones = sum(s.label for s in amb) / max(len(amb), 1)
    assert 0.4 < ones < 0.6, f'ambiguous labels are not a coin flip: {ones:.3f}'

    # Determinism, because a seed that does not reproduce proves nothing.
    assert [asdict(s) for s in generate(500, seed=7, train=False)] \
        == [asdict(s) for s in generate(500, seed=7, train=False)], 'not reproducible'

    print('self-check passed')
    print('  train', summarise(train))
    print('  test ', summarise(test))
    print(f'  ambiguous label balance: {ones:.3f}  (a fair coin, by construction)')


if __name__ == '__main__':
    _self_check()
