# python-reference

The first-generation prototype, in the Python it was written in. All of it
has been ported to C in the folder above. It is kept as the oracle the C
was checked against. `../../build_c.sh` needs no Python.

| Python | Replaced by |
|---|---|
| reasoner.py, node_reasoner.py, rank_domain.py, compose.py | native_reasoner.c |
| reasoner.py main, node_reasoner.py main | demo_reasoner.c, demo_node_reasoner.c |
| the four test_*.py files (79 checks) | test_native.c (81: the 79, plus 2 added) |
| dataset.py | dataset.c, test_dataset.c |
| baseline.py | baseline.c |
| Python's `random` module | pyrand.c, which draws the same numbers, bit for bit |
| numpy's `default_rng` | nprand.c, the same, checked by test_nprand.c |

How exact the port is:

- **Reasoner:** `diff_native.c` against `diff_native.py`, 168,000 results
  over 600 seeded worlds, 0 mismatches. In 1,102 of them the two picked
  different shortest derivations of equal length, because Python walks
  neighbours in hash-set order and C in ascending order. Every C witness
  is checked to be a valid chain of facts.
- **Dataset:** every sample gets the same category and label. Up to 2 ulps
  differ in about 3% of coordinates, because the C maths library rounds
  `cos` and `sin` differently from the one Python uses on Windows. Neither
  is authoritative: measured against 60-digit arithmetic, on the values
  where they differ, the C library is correctly rounded 58% of the time
  for cos and 54% for sin, Python's 42% and 46%.
- **Baseline:** identical to Python, every line. `nprand.c` reproduces
  numpy's `default_rng` (SeedSequence, PCG64, the ziggurat normal) bit for
  bit: `test_nprand.c` checks it against numpy's own output, including a
  CRC over a million normals. The ziggurat tables are numpy's, copied by
  `make_ziggurat_tables.py`, because recomputing them does not reproduce
  numpy's bits.

Two defects were fixed here while porting. `node_reasoner.py`'s demo
crashed, because `speculate` had started requiring a `guess_id` and the
demo never passed one. And `test_compose.py` printed "all checks passed"
before its last five checks, so a failure in them could not fail the
file.
