# No chain

The foundation is one axiom: **to reason is to eliminate possibility.**

Everything in `certifiable-c/smarsh_reason.{h,c}` is that axiom and its
consequences. This file states the one result that makes it a different
thing from a scoring pipeline, rather than a scoring pipeline with better
vocabulary.

## The three objects

A **state** is the set of worlds still possible. It begins as everything
and only ever shrinks. `sr_eliminate` is the only operation that shrinks
it, so the complete history of a computation is a list of eliminations.

A **query** is a total function from worlds to answers. It is the
question. It carries no score, no verdict, and no state, and it means the
same thing before and after any elimination.

An **answer set** is what a query still admits: `D(q) = { q(w) : w live }`.
Everything reported is counted off `|D|`:

| condition | verdict |
|---|---|
| `\|D\| == 1` | derived — the survivors agree |
| `\|D\| > 1` | undetermined — they do not |
| `\|D\| == 0` | contradiction — nothing survives |
| no domain | groundless — there was never a question |

`S = 1 − |D|/dom` is how much of the answer space is gone.
`H = log2 |D|` is what a single-answer emission would *claim* and not hold.

## The theorem

Let `a` be an unknown boolean. Then `D(a) = D(not a) = {false, true}`,
and `S = 0` for both. Identical answer sets, identical supports.

Now compose:

```
a and a       ->  {false, true}   undetermined
a and not a   ->  {false}         DERIVED false
a or  not a   ->  {true}          DERIVED true
```

Three composites. One operand signature. Three different verdicts, two of
them derived from structure alone with no evidence whatsoever.

**Therefore `S_out = f(S_left, S_right)` is unsatisfiable.** Not
inadvisable — nonexistent. `f` would have to return three values for one
argument. The same exhibition kills any `g` over answer sets, since the
answer sets are identical too.

## What the theorem does and does not reach

Stated at its correct scope: **no compositional confidence semantics
exists.** There is no way to assign a number to each claim such that the
number for a composite is a function of the numbers for its parts.

That reaches three things:

- any system that attaches a confidence to a claim and then **combines
  claims**, symbolic or otherwise
- any **interface** whose contract is a per-answer confidence, since the
  consumer will combine what it is given
- any **chain of thought that commits an intermediate conclusion and
  reuses it**, because commitment is what turns a step into an operand

It does **not** reach a transformer's internals, and saying otherwise
would be the kind of overreach that gets a document put down on page one.
A transformer does not compute a score for `a`, a score for `not a`, and
combine them. It composes high-dimensional representations and the softmax
sits at the end. Nothing above says anything about that, and nothing above
needs to.

The result is smaller than "confidence pipelines cannot be repaired" and
it is worth more, because it is exactly true. What it rules out is a
*semantics*, not an architecture.

Checked, not asserted: `certifiable-c/verify_reason.py` section 4.

## What the kernel does instead

It never forms a score chain. It composes the **questions** — pointwise
over worlds, `sr_map1` / `sr_map2` — and evaluates the composite **once**
against the surviving worlds.

Correlation is therefore not handled. It never arises. `a and not a` is
false at every world because it is false at every world; there is no
dependency tracking anywhere in the file, because the operands were never
separated in the first place.

## Observation and speculation are the same operation

```
sr_observe(state, q, a)      eliminate every world where q(w) != a
sr_speculate(state, q, ...)  eliminate every world where q(w) != a,
                             for an a that nothing licensed
```

Identical mechanics. The only difference is whether a constraint
authorised the cut. So "unlicensed elimination" stops being a metaphor for
`H` and becomes literally the operation performed: a speculation cuts
`log2|D|` bits worth of worlds that no constraint cut, and that debt is
recorded in the ancestry — a *set* of guesses, so one guess reached by two
paths is one guess — rather than lost into the state.

This is the only place in the kernel where anything happens that was not
forced. It is gated on `S >= tau`, it requires a caller-assigned
`guess_id`, its pick is **uniform** over what remains — it does not try to
be right more often than chance, which is exactly what keeps it from being
prediction — and it taints everything downstream. Every other function is
derivation or it is counting.

A refusal reports **undetermined**, not groundless. There is a question
and there is a domain; the language surface renders a refusal as the
`groundless` value without the kernel having to pretend those are the same
thing.

## Absorption is derived, never declared

`false and groundless = false` used to be a hand-written rule. It is now a
consequence: when an operand is not a question at all, the kernel scans the
operator table across everything the operands could be, and if the table is
**constant** across that product the answer is forced regardless.

So it holds for `or` and true, for `min` and `max` over ranks, and for any
operator with an absorbing element over any domain — none of which are
named anywhere in the code. When the table is not constant the verdict is
groundless, never undetermined, because the product is an
over-approximation and claiming an answer set from it would claim more
than is held.

## Witnesses

Every answer carries the surviving world set, and a separate checker
re-derives the verdict from that set and the query alone. The checker
shares no verdict-producing code with the engine, deliberately: a checker
that calls into the engine only confirms the engine agrees with itself.

A speculation's witness still says **undetermined**. It does not back the
guessed value, and that gap is the entire distinction between a derivation
and a guess.

## Settling: commitment is earned, not scheduled

Nothing may be passed forward because it is time to pass something
forward. `sr_settle_commit` yields a value only when the target is
**derived**. Undetermined does not commit, contradiction does not commit,
groundless does not commit, and no number of rounds changes that.

The stopping criterion is exact and needs no threshold. A piece of context
has stopped moving the answer exactly when applying it eliminates no
answer that was not already eliminated. Zero is zero; there is no epsilon
and nothing to tune.

Four properties that a numeric convergence test on a belief vector does
not have:

1. **No epsilon.** Nothing to pick and nothing to get wrong.
2. **Termination is proved, not assumed.** The state only shrinks and a
   productive round removes at least one world, so there are at most
   `n_worlds` productive rounds. No contraction argument is needed,
   because monotone plus finite is enough.
3. **It is decidable.** "Did the answer set change" is a bitset
   comparison, not a judgement about how small a difference is small.
4. **It cannot double-count.** `sr_eliminate` is idempotent, so the same
   evidence arriving by two paths does nothing the second time. That is
   the loopy-belief-propagation failure made structurally impossible
   rather than corrected for after the fact.

The trace also separates two things a single norm cannot tell apart:

| | meaning |
|---|---|
| **productive** | the round eliminated worlds. Something was learned. |
| **informative** | the round moved the *answer*. Something was learned that mattered to this question. |

A round can be productive without being informative: context that rules
out worlds the target could not tell apart anyway. That is exactly the
shape of a relevant-looking but useless retrieval, and it is visible here
as a number rather than invisible inside a norm.

One consequence worth stating on its own: **convergence alone earns
nothing.** A belief can stabilise on not knowing. When it does, the answer
is undetermined and nothing is emitted.

## Reopening a claim

The state is a fixed-size value with no pointers, so a checkpoint is a
copy and reopening a claim is cheap.

Licensed eliminations never need reopening; they are only wrong if the
constraint was wrong. Speculations are exactly what does, so `sr_retract`
restores the checkpoint **and** drops that guess from the ancestry, so the
debt disappears along with what it bought. Dropping one without the other
would leave either a phantom guess or an unpaid one.

## There is no calibration gap

A uniform pick over `|D|` is right `1/|D|` of the time **by
construction**, so the expected accuracy of a guess is derived rather than
estimated. There is nothing to measure and nothing to correct.

Confirmed anyway, over 4,000 seeds each, because a derived number nobody
checked is still a claim:

```
|D| = 2   measured 0.5000   1/|D| = 0.5
|D| = 4   measured 0.2510   1/|D| = 0.25
|D| = 8   measured 0.1265   1/|D| = 0.125
```

## What this costs

Worlds are enumerated, capped at 256 — eight independent booleans. That is
small and it is a hard ceiling.

What it buys is exactness: sound *and* complete over the finite universe,
no heuristic, no approximation, no search order, nothing to tune, and a
result a fifteen-line checker can confirm. Scaling past 256 worlds is a
real, unsolved, separate problem. It is not solved here.

## Status

`test_smarsh_reason.c` has 194 checks, all passing, compiled as strict C99
with zero warnings. It checks the C against ground truth computed a
different way (independent brute force, no shared helpers): random
compositions, unary maps, absorptions, asks whose verdicts must each
re-derive from their own witness, and settle runs in which the answer set
must only ever shrink. `diff_kernel.c` separately ran 3,000 random trials
identical to the Python reference.

Section 13 additionally reads the C source and classifies every single
read of `S` into one of four permitted roles. Any new read that is not one
of them fails the suite, so the no-chain property is enforced against the
text rather than remembered.

This section used to say the C had never been compiled. It has been now,
and compiling it found a bug the Python reference shared: a NaN threshold
passed the speculation gate. Both are fixed. `../build_c.sh` rebuilds and
reruns everything. The Python that verified the reasoning before a compiler
existed is kept in `python-reference/`.
