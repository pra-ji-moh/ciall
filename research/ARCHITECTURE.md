# The child as a mind of parts

How the child plays a world it can only see and act in, part by part, and
where each part comes from. The code is `certifiable-c/smarsh_mind.c`; the
worlds are `certifiable-c/arc_worldgen.c`; growth is `certifiable-c/smarsh_grow.c`.

## The one rule that holds it together

**Evidence decides; experience guides.** Every belief is a set of what has not
been ruled out (Hartley: uncertainty is log2 of what is left). A belief survives
only by never being contradicted. Experience (Shannon: what has happened how
often) only decides *what to try first*. It orders; it never decides.

## The parts

| Part | Brain analogy | What it does | Where it comes from |
|---|---|---|---|
| Parietal map | parietal lobe: "where things are" | The frame as things: patches of one colour, the ground under them (remembered where hidden), and movers, found by moving together. Reads two relations off the ground: the odd one out (the only colour that comes to one unit's worth among small things) and a legend (a row of distinct colours, each also found elsewhere) | Spelke's core knowledge: objects, and things that move as one |
| Beliefs | | Per mover: moves by a fixed step per action (stopped by walls), or only when pushed. Which colours block. Which conditions end a level. All held as sets, ruled down | Hartley 1928; Mitchell's version spaces |
| Prediction | predictive processing | Before every action: where every mover will be, and whether the level ends. After it: every mismatch corrects the belief that made it. What no belief explains is a puzzle | Clark; Friston; Piaget's accommodation |
| Inventor | | Starts with one block ("a mover covers a colour"). When nothing it believes explains an ending, or nothing it believes can be tested, it builds the next blocks (the odd one out; the legend in order; the legend reversed) and keeps only those that agree with everything seen in the world, replayed | Campbell and Simonton (variation and selection); "every capacity must earn its place" (`demo_baby.c`) |
| Imagination | episodic simulation | Plans are made inside its beliefs: a search over where the movers could be, for a state in which a surviving belief says the level ends. It acts on the plan and replans when a prediction fails | Schacter and Addis |
| Novelty | the fly's mushroom body | A sparse random expansion of features of the first frame, the strongest 8 of 128 units kept (FlyHash). Compared with every world before: how new is this? | Dasgupta et al. 2017 |
| Intuition | recognition-primed decisions | What ended the worlds that looked like this one, by how often: the order in which surviving ideas are tried. Trusted only once it has been right at least twice, three times as often as wrong | Klein; Kahneman and Klein 2009 |
| Controller | frontoparietal network | Each move: explore (an action not yet known, or somewhere not yet been), trust intuition, imagine, or ask (stuck: give up the weakest conclusions first) | Cole et al. 2013, "flexible hubs" |
| Library and packing | | Ideas that explained 3 worlds become blocks it starts every world with | DreamCoder; chunking |
| Sleep | hippocampal replay | Between stretches: worlds it could not explain are replayed through every block | Wagner et al. 2004; replay |
| Teacher | productive failure | Five worlds of a kind in a row not done well: the teacher names the *kind* of idea that ends them. The child still has to find which, and it still has to survive everything seen | Kapur |
| Switches | optogenetics | Any part can be switched off. At the end of every stretch it plays the same worlds without each part in turn, and writes down what each is worth | |

## What it does, measured

Finished well (every level, the later levels within twice the shortest), 10
worlds of each kind, the mind keeping one library across them. The old
player is `smarsh_play.c`, which reasons over cells.

| Kind | difficulty 0, old → mind | 3 | 6 | 9 |
|---|---|---|---|---|
| mazes | 10 → 10 | 10 → 10 | 10 → 10 | 10 → 10 |
| keys and doors | 10 → 10 | 10 → 10 | 10 → 9 | 10 → 10 |
| pushing | 9 → 9 | 9 → 9 | 10 → 10 | 10 → 10 |
| odd one out | 5 → 9 | 4 → 9 | 0 → 10 | 0 → 10 |
| mirror twin | 0 → 10 | 0 → 10 | 0 → 10 | 0 → 10 |
| in order | 0 → 10 | 0 → 10 | 0 → 10 | 0 → 10 |

`test_mind.c` checks each part: that it invents the odd one out and the legend
in order itself, finds the mirrored twin, predicts mostly right, that switching
the inventor off loses the abstract worlds, that sleep finds the idea that fits
a world it could not explain, that intuition names what ends worlds with a
legend, that packing happens, and that it is deterministic.

## What it is not, yet

- **It plays the practice worlds, not ARC-AGI-3.** The ARC games still go to
  `play_arc.c` (the situation explorer). They have pointing actions, more than
  five actions, and endings this language cannot yet say. Bringing the mind to
  them is the next step.
- **The inventor's blocks are few, and written in.** It composes "cover" with
  "the one that differs" and "the legend, in order or reversed". It does not
  yet build new blocks out of old ones; that would be the step from invention
  to open-ended invention.
- **Intuition sees only the first frame.** Worlds that look alike (a maze and an
  odd-one-out world are both mazes with tiles) get the same intuition. A person
  would guess the same way, but would also revise the guess after a few moves.
- **The relations are read off the ground in fixed ways.** "Odd" is by amount of
  colour; "legend" is a row. Other ways of differing (by shape, by size) are not
  read.
