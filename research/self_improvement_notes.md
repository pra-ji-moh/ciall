# A child that corrects and builds itself: reading notes and a design

Continues reading_notes.md. The question: what would it take for Ciall to
correct itself and build itself up, toward what GPT-6 Astra does on ARC-AGI-3,
without leaving the Hartley core (hypotheses held as a set, ruled down by
contradiction, H = log2 of what is left, uniform choice)?

## Where it stands

ARC-AGI-3 technical report (arXiv 2603.24621, March 2026): humans solve 100% of
the environments (486 participants, median 8.1 minutes). Frontier models then:
Gemini 3.1 Pro 0.37%, GPT-5.4 (High) 0.26%, Opus 4.6 (Max) 0.25%. Ciall, fresh,
no memory: 0.373%. GPT-6 Astra (September 2026, standard harness): 62.7%.

The report names what separates humans from AI: "Human reasoning capability is
not bound by domain knowledge"; AI systems "blindly try many options" rather than
forming a model of the environment fast. It asks for four abilities: exploration,
modelling, goal inference, planning. Ciall has parts of all four; what it lacks is
a hypothesis language rich enough to model most games, and the ability to grow
that language itself.

## What the self-improving systems that exist actually do

**Gödel machine** (Schmidhuber, 2003). Rewrites any part of its own code once it
has *proved* the rewrite useful. Optimal in principle; in practice no one has
found such proofs for anything interesting. Every working system since replaces
the proof with a test.

**Darwin Gödel Machine** (Zhang, Hu, Lu, Lange, Clune; arXiv 2505.22954, 2025).
A coding agent rewrites its own Python (its tools, its workflow, its prompts),
proposing each change from its own evaluation logs, with a frozen language model
writing the code. Each variant is tested in stages on real tasks and kept in an
*archive* of every variant ever made; parents are chosen favouring good scores
and few children. SWE-bench 20% -> 50%, Polyglot 14% -> 31%, over about two weeks
and heavy API cost. Ablations: without self-improvement, gains "taper off
quickly"; without the archive (only the latest version kept), one bad change
poisons everything after it. Both are needed.

**AlphaEvolve** (DeepMind, arXiv 2506.13131, 2025). A language model proposes
changes to a program; automated evaluators score every variant; an evolutionary
database keeps diverse niches (MAP-Elites). Found a 0.7% global compute recovery
in Google's data centres and a 23% FlashAttention speed-up.

**STOP** (Zelikman et al., arXiv 2310.02304). An improver, run on itself, gets
better at improving. Also records how often generated code tried to escape its
sandbox: self-improvement needs a sandbox.

**Boundless Socratic Learning** (Schaul, arXiv 2411.16905). A closed system can
improve itself only with three things: feedback that stays aligned with what the
observer wants ("what can self-correct is behaviour given feedback, but not
feedback itself"), coverage (it must keep exploring, or it drifts, collapses or
narrows), and scale.

**DreamCoder** (Ellis et al., PLDI 2021). Solves tasks by searching programs in
a library; then, asleep, finds pieces common to its solutions and adds them to
the library as new primitives when they shorten the total description (MDL). The
library deepens over time: the system invents its own vocabulary. No language
model is needed for this part.

**Popper** (Cropper and Morel, "Learning programs by learning from failures",
2021). Generate a program, test it, and when it fails turn the failure into a
*constraint* that prunes every program that would fail the same way (too general:
prune its generalisations; too specific: prune its specialisations). This is
elimination over programs, and the most direct Hartley reading of program
learning in the literature.

**Verifier-guided self-improvement** (AlphaProof, Nature 2025; Goedel-Prover-V2;
"Propose, Solve, Verify"). A strict verifier (Lean) judges; the system learns
from what passes and what fails, with no human labels.

**ARC-AGI-3 systems** (see reading_notes.md). OPINE-World (20/25 games) and
Executable World Models (58% RHAE) both keep the world model as code and admit
it only if it reproduces every recorded transition. Their language model
*proposes*; the check *judges*.

## The pattern

Every system that has built itself up by a large margin has the same shape:

    a proposer of changes  ->  a strict judge on real tasks  ->  an archive of what passed

and every one of them uses a pretrained language model as the proposer. The
judge is always an exact test against reality, which is Hartley in all but name.
The only proposers that extend a system's own language *without* a language model
are DreamCoder's library learning (compress what worked into new primitives) and
Popper's learning from failures (turn each failure into a constraint).

## A design for Ciall

1. **Hypotheses are programs.** For ARC: small programs over objects in the grid
   (what an action does to a thing of a kind; what ends a level). For stories:
   what a word does to the picture of the world. Not a fixed menu of atoms.

2. **Learning is Popper's generate, test, constrain.** Every observed transition
   is a test; a failed program becomes a constraint that prunes every program
   that would fail the same way. What is left is the version space; H = log2 of
   it (counted, or bounded where it cannot be counted). The child acts uniformly
   among what is left, and explores where two surviving programs disagree.

3. **It grows its own language, DreamCoder's way.** After it solves things, it
   looks for pieces common to its solutions and adds them as new primitives, but
   only if they shorten what it knows *and* its scores do not fall on games held
   back. This is how it invents new parts of itself natively.

4. **It corrects its own code, the Darwin Gödel Machine's way.** Every version
   of the child (settings, library, code) goes into an archive with its scores;
   a change is judged in stages on real ARC games, dev set first, held-out set
   last; bad changes stay in the archive as dead ends rather than being built on.
   Parents are chosen uniformly among the archive's undominated members.

5. **Feedback is the benchmark, kept aligned.** RHAE on development games, with
   held-back games no change is tuned on (Schaul: the judge must not be
   something the child can bend). **Coverage** is curiosity plus the archive's
   diversity. **Scale** is the hourly cloud run, growing.

## The decision this leaves

Every system above that reached large gains used a pretrained language model to
propose code changes. Two ways forward:

- **Native only.** Proposals come from Popper-style constraints and DreamCoder-
  style compression, entirely inside Ciall. True to "no prediction anywhere";
  no one has shown this reaching frontier scores; slower.
- **Native judge, model proposer.** A language model (e.g. Claude) proposes code
  changes to Ciall; the Hartley judge admits a change only if it passes every
  check and raises the score on held-out games. This is the Darwin Gödel Machine
  route, the one with published results, and it keeps every belief the child
  holds strictly Hartley: the model only suggests, it never decides.

## Sources

- ARC-AGI-3 technical report: https://arxiv.org/html/2603.24621v1
- Astra on ARC-AGI-3: https://arcprize.org/blog/astra
- Gödel machines: https://arxiv.org/abs/cs/0309048
- Darwin Gödel Machine: https://arxiv.org/html/2505.22954v3 ; https://sakana.ai/dgm/
- AlphaEvolve: https://arxiv.org/abs/2506.13131
- STOP: https://arxiv.org/abs/2310.02304
- Boundless Socratic Learning: https://arxiv.org/html/2411.16905
- DreamCoder: https://dl.acm.org/doi/10.1145/3453483.3454080
- Popper, learning from failures: https://arxiv.org/abs/2005.02259
- AlphaProof: https://www.nature.com/articles/s41586-025-09833-y
- ARC Prize 2025 technical report: https://arxiv.org/abs/2601.10904
