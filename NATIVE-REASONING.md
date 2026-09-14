# Native reasoning

The forward program. [POSITION.md](POSITION.md) says what exists; this says what
is being built toward and, more usefully, what would falsify it.

## What the phrase means here

"AI-native reasoning" is used loosely everywhere, so it is pinned down narrowly
here or it means nothing:

> A system that produces a claim **together with the grounds for it**, where the
> absence of grounds is a first-class outcome rather than a low-confidence
> guess, and where neither the claim nor the grounds are written by the thing
> being evaluated.

That is deliberately not "a model that thinks." It is a property a system can
have or fail to have, and it is testable. A system has it if there exists an
input for which the correct behaviour is to return nothing, and the system
returns nothing, and the record says why.

Under that definition Smarsh already has the property at the execution layer.
Nothing yet has it at the claim layer for a formulation it was not handed.

## The one unsolved problem

Everything reduces to this:

> **Can a formulation be generated and still be checkable?**

Today a human who knows the domain writes the arithmetic and the invariants, and
the machinery checks them. That works, and it does not scale past a handful of
domains. Generating the formulation moves the trust boundary onto the generator:
if the derivation is wrong, the checker faithfully proves the wrong claim and
reports success, which is a false result wearing the authority of a real one.
That is the exact failure this whole position exists to prevent, relocated one
step upstream.

So the problem is not "generate a formulation." It is:

> **Generate a formulation whose wrongness is detectable without already knowing
> the right one.**

Some structure exists for attacking that, and it is worth writing down before
any code:

- **A generated formulation is a claim, not a fact.** It should arrive
  `ungrounded`, and any verdict computed from it should inherit that label.
  Taint propagation already carries labels through every operation, so a
  "proved" derived from an unconfirmed formulation cannot launder itself into a
  finding. This costs nothing to add later and should not be built early.
- **The formulation belongs in the record, not just the verdict.** A reviewer's
  real question is not "did the solver run correctly," it is "is this the right
  arithmetic for my claim." A record showing only `proved` hides the one step
  that needed checking.
- **Cross-checking is cheaper than proving correctness.** Two independently
  generated formulations of the same claim that disagree is a signal available
  without an oracle. Dimensional analysis, unit consistency, and known limiting
  cases are all cheap falsifiers of a formulation that do not require knowing
  the answer.

## Constraints that must hold

These are not preferences. Breaking any one of them makes the output stop being
evidence, which is the only thing being sold.

**1. Determinism.** The same inputs must produce the same decision and the same
record. This is why `speculate` takes rejection history as an argument rather
than reading accumulated state: a decision that depends on ambient history does
not replay, and a run that cannot be reproduced from its manifest proves
nothing. Any generated formulation must be seeded and the seed recorded.

**2. No model in the deciding step.** Ciall's own README states this. If a model
clusters queries into regions, and the region determines the rejection history,
and that determines behaviour, then a model is in the deciding step
transitively. The fix is the same as the FFI boundary: precompute the artefact,
pin it by digest, and the decision stays deterministic given that artefact. The
defensible claim becomes "no model in the deciding step, and the artefact it
depends on is named by hash."

**3. Abstention must cost something.** The known failure mode of any
learned abstention mechanism is reward hacking toward permanent uncertainty:
declining is free, so the system learns to decline. `speculate` is currently
immune because the bar is stated by whoever knows the domain and nothing trains
against it. **The moment this moves into a loss function that immunity is
gone.** Any objective derived here must make abstention cost something, and that
constraint should be written down before the first derivation, not discovered
after.

**4. Every step must say how it is computed, not what it achieves.** This is the
test the Gemini document failed: `Ax = 0` says "project onto the kernel of A"
and never says how A is chosen so that hallucinated content lands in the null
space and true content does not, which is the entire problem asserted as solved.
Fisher information passes this test. Anything that invokes advanced mathematics
to paper over an unresolved step fails it.

## Where the model layer actually sits

The diagnosis is sound and is not original, which is good: it means there is
prior art to build against rather than a blank page.

Standard cross-entropy conflates **epistemic** uncertainty (no grounds to know)
with **aleatoric** uncertainty (grounds exist, the answer is genuinely
ambiguous). A low-probability token looks identical either way. The loss
function assumes there is always a right-shaped answer to output and has no way
to represent a legitimate null.

**And it is not only the objective.** Softmax attention collapses ambiguous
attention scores into one normalised distribution and discards the spread, at
every layer, in the forward pass. The signal is gone before any loss function
sees it. This matters for where to intervene: an objective-only fix is asking
the loss to recover information the architecture already threw away.

Two facts from the literature, both of which change the plan:

- **Epistemic calibration is a separate capability from accuracy, and training
  does not develop it.** Frontier models sit at 0.12-0.40 expected calibration
  error, and extended reasoning makes calibration *worse*, not better. Models
  with thinking enabled show weaker uncertainty attribution despite stronger
  reasoning.
- **The cost objection is obsolete.** Credal attention, which replaces the
  single attention distribution with a convex set and computes vacuity
  directly, reports 4.4% inference and 11.6% training overhead. Not N models,
  not an intractable posterior. The reason foundational work here was said to be
  economically illegible no longer holds.

Existing routes, with their costs:

| Route | Cost | Trained in, or bolted on |
|---|---|---|
| Deep ensembles | N models | trained in |
| Bayesian neural networks | brutal to scale | trained in |
| Conformal prediction | cheap | post-hoc |
| Fisher information / Laplace | one model, second-order quantity | post-hoc |
| **Credal / Dirichlet attention** | **4.4% inference, 11.6% training** | **trained in** |

The last row supersedes what this file said first, which was that Fisher was the
cheapest live route. It is cheap, but it annotates a trained model from outside.
Credal attention is comparably cheap *and* inside the forward pass, which is
where the signal is currently being destroyed.

The gradient framing that came out of the conversation maps onto the Fisher
row, and it is still worth understanding: the curvature at the optimum is a
posterior over weights, and where the gradient distribution is wide across
similar inputs the model has little evidence. Prior art before deriving
anything: Laplace approximations for deep networks, SWAG, and gradient-norm
out-of-distribution detection.

But it is an annotation of a model that has already thrown the signal away.
Under a "must exist in my lifetime" constraint that still argues for it as the
cheap first probe, and against it as the destination.

Smarsh cannot help with this part. Its prover is linear arithmetic and
backpropagation is products of partial derivatives all the way down, so every
term of interest goes opaque. There is also no autodiff. Use SymPy for the
algebra and finite-difference gradient checking for the numerics; use Smarsh for
the reproducibility artefact around the experiment, which is a real contribution
and a different one.

## The specific opening

Two gaps, stated by the authors of the work that would otherwise close them.

**1. The uncertainty is measured but not used.** From the Credal Transformer's
own limitations: the signal is *"mainly used as a post-hoc metric rather than
actively guiding information flow within the network."* Nobody has made vacuity
*steer* generation. It annotates.

**2. Nothing checks whether the reasoning was grounded.** From the
evidence-sufficiency work: existing methods *"rely on uncertainty estimation or
evidence sufficiency checks, but neither tests whether the reasoning process for
generation is actually grounded in the evidence."*

And one thing that appears to be unclaimed, which came out of building the
execution layer rather than reading papers:

**Every system found returns its confidence to the caller.** Support scores,
vacuity, calibrated probabilities: all handed back to the thing being gated.
`speculate` does not. The program receives a behaviour and the numbers go to a
record only a third party reads.

That asymmetry is not decoration. It is the answer to the failure mode that
kills learned abstention: a system that can read its own margin can be trained
to sit just over the bar, and a system that can branch on the margin encodes the
bar into itself. Withholding the signal from the generating pathway makes
gaming it structurally impossible rather than merely penalised.

So the sharpest available claim is the intersection of the three:

> **A vacuity signal that gates generation, computed inside the forward pass,
> and not visible to the pathway it gates.**

Narrow, falsifiable, cheap enough to test small, and each of its three parts is
either open by the admission of the people closest to it or absent from the
literature entirely.

## First experiments, ordered

Each is falsifiable and small enough to be wrong quickly, which is the point.

1. **Port `dataFusion` to Smarsh.** Its refusal to fuse measurements in decisive
   tension is the thesis in one function. Output: a signed record reading
   `REFUSED to fuse: sources in decisive tension`. This tests whether the record
   is something a reviewer would actually want, on one domain rather than nine.
2. **Formulation in the record.** Extend the manifest to carry the arithmetic a
   claim was formulated into, hashed. Tests whether "is this the right
   formulation" becomes answerable by someone who was not there.
3. **Cheap falsifiers of a formulation.** Dimensional consistency, limiting
   cases, and cross-checking two independent formulations. Tests whether
   wrongness is detectable without an oracle, which is the whole wall.
4. **Only then, the model layer.** One narrow question: on a model small enough
   to train in an afternoon, does a vacuity signal that *gates* generation beat
   the same signal used only to annotate it, measured as confident-wrong-answer
   rate on inputs designed to be out of distribution versus genuinely ambiguous?
   Run it twice, once with the signal visible to the generating pathway and once
   withheld, because the difference between those two runs is the part nobody
   else has tested and the part most likely to be wrong.

   Tooling: PyTorch or JAX, not Smarsh. Its prover is linear arithmetic and
   backpropagation is products all the way down, so every term of interest goes
   opaque, and there is no autodiff. Use SymPy for the algebra and
   finite-difference checking for the gradients. Smarsh's contribution to this
   step is the reproducibility artefact around the experiment, which is real and
   is a different contribution.

Step 4 is worth nothing until steps 1 to 3 have shown the thesis survives
contact with one real claim. The order is not caution, it is that the later
steps are only interesting if the earlier ones worked.

## How this ends up wrong

Written down so it is recognisable from the inside.

- **Generality before extraction.** Nine domains, none extracted from a real
  user, is the CORBA shape. The tenth domain is worth less than the first user.
- **Scope inflation.** Attaching the idea to increasingly distant fields is a
  tell that the idea is being made to carry more than it can. It is interesting
  on its own terms.
- **A checker that proves the wrong thing.** The most dangerous outcome is not
  failure, it is a confident verdict on a bad formulation, which is exactly the
  failure mode the position exists to prevent.
- **Claiming both halves.** The checking half is built and the formulating half
  is not. Every claim should be narrow enough to survive being checked line by
  line, because the entire argument is that claims should be checkable.
