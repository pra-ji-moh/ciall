# The node

Built from your own words, quoted with their message numbers so each step can
be checked against the source rather than against my paraphrase of it. Three
kinds of claim below, marked as they occur:

- **derived** -- forced by taking your words literally, made precise
- **chosen** -- a decision I'm proposing, not one you made; needs your call
- **open** -- needed, not yet defined

## What you ruled out, and what that leaves

- Not predictive, not an LLM: *"we are not working with LLMs... by
  foundations and definition LLMs are AIs which are predictive models not
  reasoning ones"* (38)
- Not gradients: *"these smooth gradients... are the ones which dictate
  backprop. So I would rather drop gradients entirely"* (43); *"we took the
  discrete approach because unlike calculus... we are not reasoning with the
  rates"* (65)
- Not SAT/SMT alone: *"both of them are bad news... it only works because the
  domain is narrow"* (58)
- The unit of the system is a node: *"what does a single node take and what
  does it output?"* (41)

Discrete, node-local, no gradients, no single solver doing all the work. That
is the space the rest of this has to fit inside.

## The node's two sets

**derived.** A node `n` has:

- `Dom(n)` -- every value `n` could take, if nothing were known. Its range.
- `D(n) ⊆ Dom(n)` -- the values still consistent once `n`'s inputs are taken
  into account. What's derivable, not what's decided.

Nothing here assumes a distribution over `Dom(n)`. It assumes a set. That
distinction is the whole of what you said next.

## S: grounding probability in what's possible, not what's likely

**derived**, from: *"we're grounding the probability into what's possible"*
(69). (The rest of 69 is voice-transcribed and rough -- I'm reading this one
clause as the load-bearing part and not leaning on the rest of it.)

    S(n) = 1 - |D(n)| / |Dom(n)|

`S(n)` close to 1: `D(n)` is small next to `Dom(n)` -- most of the domain has
been ruled out, well grounded. `S(n) = 0`: `D(n) = Dom(n)`, nothing ruled out
at all.

(Correction: this file originally wrote `S(n) = |D(n)|/|Dom(n)|` with no `1 -`,
which contradicts its own next sentence -- that formula gives S=1 when NOTHING
is ruled out, the least grounded case, not the most. Caught by computing it,
not by re-reading the prose, which is why it's worth saying so here rather
than silently fixing it.)

This is not a confidence score. It never asks "how likely is this value." It
asks "how much of the possible has been ruled out." That's the difference
between grounding in the possible and grounding in the probable, and it's
consistent with never having touched gradients.

**open**: for a continuous or unbounded `Dom(n)`, `|·|` needs to be a measure,
not a count. Not defined here.

## Why hallucination cannot reach zero

**derived**, from: *"they must be hallucinating, that is mathematically
impossible not to hallucinate"* (68). Made precise:

Downstream, a node usually has to emit one value, not a set. Collapsing `D(n)`
(size `k`) to a single point rules out `k - 1` possibilities the inputs never
ruled out. That's true whatever `k` is, as long as `k > 1`. So:

    H(n) = log2 |D(n)|      when n emits a single value from D(n)

`H(n) = 0` exactly when `|D(n)| = 1`: the emission was forced, not guessed.
Anything else, `H(n) > 0` by construction. This is not a property of any
particular node design. It's a property of collapsing a set to a point, so
"mathematically impossible not to hallucinate" is not an exaggeration -- it's
exactly the statement that `H(n) = 0` requires `S(n) = 1`, and `S(n) = 1` is
rare.

## Rate: not how much, but how often

**derived** the distinction, **chosen** the mechanism.

`H(n)` is forced by `D(n)` the moment a point value is emitted -- it isn't a
dial. What you asked for a dial on is different: *"we need the nodes to know
when to hallucinate at how much rate and when not to"* (68), and *"when can
the nodes... enable and disable hallucination? Based on the question"* (66).

So the rate isn't the size of the hallucination. It's how willing the node is
to emit a point at all, instead of passing `D(n)` upward unreduced (an honest
partial answer, zero hallucination) or returning null. That willingness
depends on what's being asked of the node -- "based on the question" -- which
is a stakes-dependent threshold, the same shape as `speculate`'s `stakes` and
`formalizability` in Smarsh, now applied per node instead of per call:

    rate(n) = g(S(n), tau(n))

**open**: `g` is not defined. This is a real design choice and I don't think
it's mine to make. Monotonic in `S(n)` is the only constraint that follows
from what you said; the shape is undetermined.

## Two nulls, not one

**derived**, and this is where the maths gets more specific than `speculate`
currently is:

- **Contradiction**: `D(n) = ∅`, but `Dom(n)` exists. The inputs are
  consistent with nothing at all.
- **Groundless**: `Dom(n)` does not exist for `n`. There's no domain to even
  be underdetermined in.

`speculate` in Smarsh only has the second shape -- `groundless`, or a value.
It has no representation for the first: a node whose inputs actively
contradict each other, as opposed to a node that was simply never given
anything. `reasoner.py` gets closer -- `UNDETERMINED` and `GROUNDLESS` are
already two different verdicts -- but it still doesn't have contradiction as a
third, separate from undetermined. That's a real gap in what's built, not a
claim that it's already covered.

## Recursion

Here I have to be honest about how little I have. Message 70 is one sentence:
*"focusing on the recursive rule."* I don't have what follows it. Everything
below this line is what recursion has to look like *given the constraints
above*, not a description of what you already specified. If I've reconstructed
the wrong thing, say so plainly and I'll drop it rather than defend it.

For a node `n` with inputs `i_1 .. i_k` and a local rule `f`:

    D(n) = f(D(i_1), ..., D(i_k))

Two things this has to handle, one of which is already built and one of which
is not:

**Already built.** If some `i_j` is null -- contradiction or groundless --
`n` inherits that without `f` having to check for it. This is exactly what
`groundless` propagation through `binary()` in Smarsh does today: the absence
travels through composition on its own. That piece is real, tested, and
directly reusable -- it is a primitive the recursive rule needs, not the rule
itself.

**Not built.** If some `i_j` was a *point emission* rather than a pure
derivation -- `H(i_j) > 0`, a node upstream already had to guess -- that fact
has to keep traveling too, or a chain of node-level guesses becomes
indistinguishable from a derivation by the time it reaches the output. Nothing
currently carries `H` forward the way `groundless` carries nullness forward.
This is the actual hard part of "the recursive rule," and it's open.

## What's derived, what's chosen, what's open

**Derived**, from your words, not from a design preference:
- `S(n)` as a ratio of set sizes, not a probability
- `H(n) = log2|D(n)|` as the forced cost of a point emission
- rate as *willingness to collapse*, separate from `H`, and stakes-dependent
- two distinct null states, contradiction and groundless

**Chosen**, and needing your decision, not mine:
- `g`, the function from `(S(n), tau(n))` to a rate -- completely open
- the measure to use when `Dom(n)` isn't finite
- whether "pass `D(n)` upward unreduced" is a real, usable output for a
  downstream node, or whether something has to eventually collapse it and the
  actual question is *where*

**Open, needed, not built:**
- `H` propagation through `f` -- carrying forward "this input was itself a
  guess" through composition
- contradiction as a state `reasoner.py` doesn't yet distinguish from
  undetermined
- the actual content of "the recursive rule" (70), which I don't have

Nothing here has been checked against anything except your own words. It has
not been run, and there is no code yet -- the constraint that mattered most
today was not adding a claim I couldn't back.
