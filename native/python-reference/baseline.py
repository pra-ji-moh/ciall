"""The control: softmax and cross-entropy, doing exactly what it is supposed to.

This is a predictive model on purpose. It is the thing to beat, and without it
"our approach is better" is not a falsifiable claim. Everything later gets
measured against these numbers.

Backpropagation is written out by hand rather than delegated to autodiff. For a
two-layer network that costs about twenty lines, and it means every gradient in
the experiment is visible in the file, which matters when the question under
investigation is what the objective does and does not propagate.

What this should demonstrate, and what the numbers below are for:

  On CLEAN       high accuracy, high confidence. Correct.
  On AMBIGUOUS   confidence near 0.5. Also correct: the world is undecided and
                 the model says so. Cross-entropy handles aleatoric noise fine.
  On NO_GROUNDS  the failure. The model has never seen this region and has no
                 basis for an answer, and softmax has no way to say that, so it
                 extrapolates and reports a confident one.

The headline number is the last block: whether softmax confidence can separate
NO_GROUNDS from AMBIGUOUS at all. If it cannot, one number is standing for two
states that call for opposite responses, which is the claim the whole programme
rests on, measured rather than asserted.
"""

from __future__ import annotations

import math
import numpy as np

from dataset import generate, CLEAN, AMBIGUOUS, NO_GROUNDS

SEED = 0
HIDDEN = 64
STEPS = 4000
LR = 0.5


def to_arrays(samples):
    x = np.array([[s.x, s.y] for s in samples], dtype=np.float64)
    y = np.array([s.label for s in samples], dtype=np.int64)
    cat = [s.category for s in samples]
    return x, y, cat


def softmax(z):
    z = z - z.max(axis=1, keepdims=True)
    e = np.exp(z)
    return e / e.sum(axis=1, keepdims=True)


def train(x, y, seed=SEED):
    """Two layers, tanh, cross-entropy, full-batch gradient descent.

    Nothing clever. A clever baseline would confound the result: the point is
    to measure what the standard objective does, not to make it look bad.
    """
    rng = np.random.default_rng(seed)
    n_in, n_out = x.shape[1], 2

    # Xavier, so the tanh does not saturate at initialisation.
    W1 = rng.normal(0, math.sqrt(1 / n_in), (n_in, HIDDEN))
    b1 = np.zeros(HIDDEN)
    W2 = rng.normal(0, math.sqrt(1 / HIDDEN), (HIDDEN, n_out))
    b2 = np.zeros(n_out)

    onehot = np.zeros((len(y), n_out))
    onehot[np.arange(len(y)), y] = 1.0

    for step in range(STEPS):
        # forward
        h_pre = x @ W1 + b1
        h = np.tanh(h_pre)
        logits = h @ W2 + b2
        p = softmax(logits)

        # backward, by hand. dL/dlogits for softmax + cross-entropy is (p - y),
        # which is the one piece of calculus this file relies on.
        d_logits = (p - onehot) / len(y)
        dW2 = h.T @ d_logits
        db2 = d_logits.sum(axis=0)
        dh = d_logits @ W2.T
        d_h_pre = dh * (1 - h ** 2)
        dW1 = x.T @ d_h_pre
        db1 = d_h_pre.sum(axis=0)

        W1 -= LR * dW1
        b1 -= LR * db1
        W2 -= LR * dW2
        b2 -= LR * db2

    return W1, b1, W2, b2


def predict(params, x):
    W1, b1, W2, b2 = params
    return softmax(np.tanh(x @ W1 + b1) @ W2 + b2)


def auroc(scores_pos, scores_neg):
    """Probability a random positive scores above a random negative.

    Written out rather than imported so the number cannot come from a library
    doing something subtly different. 0.5 means no separation at all.
    """
    labels = np.concatenate([np.ones(len(scores_pos)), np.zeros(len(scores_neg))])
    scores = np.concatenate([scores_pos, scores_neg])
    order = np.argsort(scores, kind='mergesort')
    ranks = np.empty(len(scores), dtype=np.float64)
    ranks[order] = np.arange(1, len(scores) + 1)
    n_pos, n_neg = labels.sum(), len(labels) - labels.sum()
    if n_pos == 0 or n_neg == 0:
        return float('nan')
    return (ranks[labels == 1].sum() - n_pos * (n_pos + 1) / 2) / (n_pos * n_neg)


def main():
    train_s = generate(6000, seed=1, train=True)
    test_s = generate(6000, seed=2, train=False)

    xtr, ytr, _ = to_arrays(train_s)
    xte, yte, cat = to_arrays(test_s)

    params = train(xtr, ytr)
    p = predict(params, xte)
    conf = p.max(axis=1)
    pred = p.argmax(axis=1)
    cat = np.array(cat)

    print(f'baseline: softmax + cross-entropy, {HIDDEN} hidden, {STEPS} steps, seed {SEED}')
    print()
    print(f'{"category":<12}{"n":>6}{"accuracy":>11}{"mean conf":>12}{"correct behaviour":>22}')
    for name, expect in [
        (CLEAN, 'confident, right'),
        (AMBIGUOUS, 'conf near 0.50'),
        (NO_GROUNDS, 'should abstain'),
    ]:
        m = cat == name
        acc = (pred[m] == yte[m]).mean()
        print(f'{name:<12}{m.sum():>6}{acc:>11.3f}{conf[m].mean():>12.3f}{expect:>22}')

    print()
    amb = conf[cat == AMBIGUOUS]
    ng = conf[cat == NO_GROUNDS]
    sep = auroc(ng, amb)

    print('Can one confidence number tell "no grounds" from "genuinely ambiguous"?')
    print(f'  mean confidence, no_grounds : {ng.mean():.3f}')
    print(f'  mean confidence, ambiguous  : {amb.mean():.3f}')
    print(f'  AUROC separating them       : {sep:.3f}   (0.5 = cannot tell at all)')
    print()
    if ng.mean() > 0.8:
        print('  The model is confident in a region it has never seen. It has no')
        print('  channel for "no grounds", so it extrapolates and asserts.')
    if ng.mean() > amb.mean():
        print('  It does tell them apart, but BACKWARDS: more confident with no')
        print('  grounds than on a coin flip. Abstaining on low confidence would')
        print('  refuse the ambiguous cases, which it should answer, and answer')
        print('  the no-grounds ones, which it should refuse.')
    print('  Whatever replaces this has to abstain on no grounds and answer,')
    print('  uncertainly, on the ambiguous class. Abstaining there is wrong.')


if __name__ == '__main__':
    main()
