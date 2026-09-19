# What the child is built on: reading notes

Each note ends with what it means for the child. Everything here keeps the core
Hartley: a set of hypotheses, ruled down by contradiction, H = log2 of what is
left, and uniform choice among what is left. Nothing is weighed or made likelier.

## 1. Hartley, "Transmission of Information" (Bell System Technical Journal, 1928)

Information is the logarithm of the number of possibilities a message could
have been: H = n log s for n symbols from s. Hartley says outright that this
measures only the receiver's ability to tell which one was meant, "regardless
of any associated meaning or other psychological or semantic aspect". It needs
no probabilities: every possibility counts the same.

**For the child.** What it knows is exactly the set of possibilities it has not
ruled out, and how much it still has to learn is log2 of that set. A learner
that holds one hypothesis has thrown this measure away: it cannot say how much
it does not know. Every part of the child must hold a *set*.

## 2. Mitchell, "Generalization as Search" (Artificial Intelligence 18, 1982); version spaces

Concept learning is search in a space of hypotheses. The candidate-elimination
algorithm keeps the version space -- every hypothesis consistent with every
example -- bounded by its most specific and most general members, and each
example cuts it down. Learning is done when one hypothesis is left; an empty
version space means the hypothesis language cannot express the concept.

**For the child.** This is the formal theory of what the child already does in
its ending theories and story rules. Two lessons it has not yet taken fully:
the version space can be kept *implicitly*, by its boundaries or by per-part
domains, instead of by listing every member; and an empty version space is not
noise to be tolerated but proof that the language must widen.

## 3. Constraint propagation: arc consistency, unit propagation, backtracking (Dechter, ch. 3)

When hypotheses depend on each other (what "dropped" means depends on what
"took" means), a value can be eliminated as soon as no value of its neighbours
supports it (arc consistency). Unit propagation does this in linear time for
clauses. What propagation cannot settle, backtracking settles exactly: try a
value, propagate, and undo on contradiction.

**For the child.** This is how to hold the full set of world pictures without
listing them: a domain per word (what it could still do), each story a
constraint over the words in it, and elimination by propagation. A story with
one undecided word eliminates that word's impossible meanings directly; the
rest wait until their neighbours are settled. When nothing more can be
eliminated, the child is exactly as unsure as log2 of the product of domains
says, and it asks the world (reads or acts) where that is largest.

## 4. Gopnik, "A Theory of Causal Learning in Children" (Psychological Review, 2004); the child as scientist

Children build causal maps of their world and change them in the light of
evidence the way scientists change theories. They learn most from *intervening*
-- doing something and seeing what changes -- and can reason from indirect
evidence. Gopnik describes them as Bayesian.

**For the child.** Hartley elimination is the limiting case of that picture:
a uniform prior and evidence that either fits or does not (likelihood 1 or 0).
Under those two conditions the posterior is uniform over the survivors and its
entropy is exactly the Hartley measure. The child keeps that limiting case on
purpose, so that nothing it believes rests on a weight it made up. What it
should take from Gopnik is the intervention: ask the world where the set of
possibilities is largest, and choose the act that could rule the most out.

## 5. Einstein: principle theories and constructive theories (The Times, 1919); the thought experiments

Einstein separated constructive theories, built bottom-up from assumed parts
(the kinetic theory of gases), from principle theories, which start from a
few general principles found in experience (the speed of light is the same for
every observer) that every process must satisfy. The principles are criteria
that *separate* what can happen from what cannot. He turned to principles when
building up from known facts would not converge. His thought experiments test a
theory by following it, in imagination, to a case where it must say something
definite.

**For the child.** A principle is elimination at scale: one invariant rules out
every hypothesis that would break it, across all situations at once. The child
should look for invariants in what it has seen -- a thing is in one place at a
time; the number of cells of a colour never changes; this action is always
undone by that one -- and, once no observation has contradicted one, use it to
cut every hypothesis family at once. A thought experiment is running its world
model forward to where two surviving hypotheses disagree, and going to look
there.

## 6. ARC-AGI-3, the strongest published systems (2026)

**OPINE-World** (arXiv 2607.01531) clears 20 of 25 public games (score 78.4;
human-to-agent action ratio 1.7). Its world model is object-centric executable
code: a transition rule per object type. A proposed model "is admitted only
when it reproduces every recorded transition exactly"; any mismatch is a
counterexample that sends it back. It explores where its types and rules
explain least. After a level is cleared and the model verified, a bounded
planner searches the model for a route, executed step by step, and any
divergence is a counterexample.

**Executable World Models** (arXiv 2605.05138): a coding agent keeps the world
model as Python, verified against every recorded observation, planned in, and
repeatedly refactored to stay compact and general. 58.1% mean RHAE with GPT-5.5.
Its main failure: "premature commitment to an incorrect or overly specific
world model", refining one model instead of considering alternatives.

**For the child.** Both judges are pure elimination: a model survives only if
every observation agrees. What they have that the child lacks is a rich
language of hypotheses (object types, per-type transition rules) and planning
inside a verified model. Their failure is what holding one model does; holding
the set of consistent models, as Hartley requires, is the cure. So the order is:

1. Stories: the world picture holds a *set* (domain per word, propagation),
   never one picture; an empty set widens the language.
2. ARC: the same, for the board -- per object type, the set of transition rules
   still consistent with every transition seen; explore where log2 of that set
   is largest; plan inside the surviving models once they agree on a route.
3. Principles: invariants found across everything seen, used to cut every
   family at once.

## Sources

- Hartley 1928: http://keszei.chem.elte.hu/entropia/Hartley1928text.pdf
- Mitchell, version space learning: https://en.wikipedia.org/wiki/Version_space_learning ; https://www.cs.cornell.edu/courses/cs472/2004fa/Materials/2004/8-version-space-4up.pdf
- Dechter, constraint propagation: https://ics.uci.edu/~dechter/courses/ics-276/spring-19/reading/chapter3-constraints.pdf
- Gopnik et al. 2004: https://alisongopnik.com/Papers_Alison/Psychological%20Review%20%20Final.pdf
- Einstein, principle and constructive theories: https://arxiv.org/pdf/2512.13463 ; https://en.wikipedia.org/wiki/Einstein's_thought_experiments
- OPINE-World: https://arxiv.org/html/2607.01531v2
- Executable World Models for ARC-AGI-3: https://arxiv.org/html/2605.05138v2
