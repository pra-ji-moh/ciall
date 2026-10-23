# Hidden-rule benchmark: how many tests does it take to find the rule?

The blicket box of `brain/test_frontal.c`, made larger. A box chimes when a test
(a set of acts, done at one of four places) matches its hidden rule. A rule is a
place and a function of the acts: one act; two acts (both, either, exactly one);
three acts (all, any, at least two). A level fixes the number of acts, which
fixes how many rules there are. The solver tries tests, is told chime or silence,
and is done when it can name the rule. Score: tests spent (median; fewer is
better).

| | L1 | L2 | L3 | L4 | L5 |
|---|---|---|---|---|---|
| rules | 144 | 368 | 2,020 | 5,516 | 11,700 |
| **ideal** (knows the family, best test every time) | 7.5 | 10 | 12.5 | 12.5 | 15 |
| **frontier model** (Claude Opus, told the family, shell access) | 7 (8, 6) | | 11.5 (12, 11) | | 14 (14, 14) |
| random tests, knows the family | 70 | 54 | 105 | 98 | 128 |
| **our frontal cortex**, 4 slots (1 year old) | 75 | 190 | 1,040 | 2,904 (7/8 solved) | 3,000 (0/8) |
| **our frontal cortex**, 39 slots (3 years old) | 11.5 | 24.5 | 116 | 323 | 620 |
| **our frontal cortex**, 66 slots (grown) | 8.5 | 15.5 | 74 | 189 | 369 |
| our frontal cortex, 39 slots **plus memory** | 7.5 | 10 | 11 | 13 | 13 |
| passive neural net (knows nothing of the family, learns from random tests) | 128 | 256 | 512 | 1,024 | 768 |

Eight rules per level for the non-language-model arms (`python3 run_bench.py 8`),
two for the language model (`llm_oracle.py`; all six answers correct, none read
the oracle's files). The Python model of our frontal cortex agrees with the C
brain (`brain/test_frontal.c`) at L1: 143, 73, 18, 11 tests at 2, 4, 19, 39 slots
there, against 143, 73, 18, 12 in the brain.

## What it says

1. **We do not beat a frontier model here. We lose.** Given the family, the
   language model does what the ideal solver does: it keeps the list of rules still
   possible (in a small script of its own), asks the test that splits them most
   evenly, strikes out what the answer contradicts. That is the Hartley approach.
   Its test counts matched the ideal solver's at every level.
2. **Our frontal cortex, as built, does not scale.** Its working memory holds a few
   dozen rules and rules out only what it holds. At 11,700 rules it needs 620 tests
   at three years old, worse than testing at random (128). It was fine at 144 rules
   only because 144 almost fits in mind. The test at L1 hid this.
3. **Memory fixes it.** If each outcome is remembered and every rule is checked
   against it, the same 39-slot mind needs 7.5 to 13 tests, as good as ideal. But
   that is thinking, not testing: it makes about 26,000 rule checks at L5 (6,000 at
   L3), growing with the number of rules. And it is just the algorithm, not a
   finding about brains.
4. **A neural network trained on random tests is the slowest of all** (128 to 1,024
   examples), because it knows nothing of the family. This is what training
   without a hypothesis space looks like. It is also the fairest picture of how
   today's models learn when they are not given the rules.
5. **Where it is not a loss yet is where this benchmark did not look.** Every arm
   here except the network was told the family of rules. The hard part of human
   reasoning, and the part nobody has built well, is proposing the family:
   noticing that the rule might involve pairs, thresholds, order, a new kind of
   thing. A benchmark where the family is not given, where a rule must be
   invented from a few examples, is the one worth building next. It is also where a
   frontier model could well do worse than it did here.

## Caveats

- Two runs per level for the language model; one model; it had a shell and wrote
  its own bookkeeping script, which is external memory. A model with no tools may do
  worse.
- It was told the family in words. The network and our brain were not given more
  than the same.
- "Ideal" samples 300 candidate tests per step, so it is near, not exactly, optimal.
- Our frontal cortex here is a Python model of the algorithm, checked against the C
  brain at one level, not the neurons at every level.
