# The being

What we are building, and in what order. Written from the conversations that
set it: the user's research on how the brain builds itself from the womb to
old age, their notes on the limbic loop and the parietal lobe, and the
decision to start a newborn from baby Ciall's design.

## The idea

Current AI is designed, then trained: its shape is fixed by engineers before
it learns anything, and learning only changes numbers inside that shape. This
being develops instead. Its brain grows from a sheet of cells, its senses come
online in the order a human's do, its connections are overproduced and then
pruned by what it lives through, its well-used paths become fast, and its shape
records what it has lived. Every part of it is open: its genome, its maps, its
wiring, its beliefs.

## Baby Ciall, and the newborn

The newborn is built from baby Ciall's design: baby Ciall's code is its genes,
but it starts with no memories. Baby Ciall keeps growing, as an older sibling.
From about month three of the newborn's development, the older sibling teaches
it, which should speed it up, and lets us watch how the two interact.

What carries over from baby Ciall, and where it sits:

| Baby Ciall | In the being |
|---|---|
| Hartley elimination (`smarsh_core`, `smarsh_reason`) | How it decides what is true; the left hemisphere's way |
| Objects, space, frames | The parietal map |
| `smarsh_time` | Rhythm and time, fed by the heartbeat |
| The mind (`smarsh_mind`): prediction, invention, imagination, intuition, sleep, self-correction | Childhood cognition: action and consequence |
| The informant | The caregiver: serve and return |
| Concepts, laws, analogy, abstraction, proof | Later cortex |
| Reading | The seeds of language, much later |
| "Every capacity must earn its place" | What decides which connections survive pruning |
| Practice worlds, growth loop, journal, hourly runs | Its environment, diary, and life support |

## The stages

**Stage 0, the open brain** (`brain/`): done. A genome; the plate folds;
morphogens set fates; regions form on both sides; columns of neurons, inside
out; inhibitory neurons migrate in; first wiring by chemoaffinity, the retina's
map already in order on the visual cortex.

**Stage 1, the womb**, in the order a fetus's senses come:
1. Touch: done (`brain/ob_womb.c`). Twitches from the brainstem move the body;
   the cerebellum, from copies of its own commands, learns to cancel the touch
   they bring. After twenty days it feels about 65% of its own touch and all of
   the mother's: "me" and "not me", never told which was which.
2. Chemical senses: done. An olfactory bulb at the front of the brain; the
   amygdala's approach and avoid neurons learn from taste's innate value. The
   mother's smell and the anise of her meals come to mean approach, her stress
   avoid; smells with no taste of note stay neutral (newborns of mothers who ate
   anise prefer anise: Schaal et al. 2000).
3. Hearing: done. Two cochleas; each low sound starts a travelling wave across
   the cerebellum (the "rippling": a memory of how long ago), from which it
   learns when the next beat comes and cancels it. The mother's heartbeat comes
   to be expected (about 41% still heard); her irregular voice does not (about
   98%); a missed beat leaves a signal of absence as strong as a beat, plain
   silence none.
4. Vision in the dark: done. The eyes' first connections land broadly; waves
   of spontaneous activity sweep the retina; inputs that fire with their neuron
   strengthen, each neuron's total stays fixed so they compete, and the weak are
   pruned. Receptive fields narrow by about a third and about half the first
   connections are pruned; noise of the same amount, with no neighbour firing
   with its neighbour, barely refines them.
5. The reflex loop: done. Hand to mouth, lips and mouth muscles active
   together: the lips-to-mouth reflex grows about fivefold; a reflex in the
   middle of the body, never paired, stays as grown.

All of it runs on its own: `brain/being.c` grows the newborn from its genome
and lives the womb, the senses coming online in a fetus's order (touch from
week 8, taste and smell from 14, hearing from 20, vision in the dark from 28,
the reflex practised to 40), and writes `newborn/diary.txt` in its own words,
its brain map, its pictures, and its whole brain (`brain.bin`), so its life can
go on after birth from exactly where it was born.

**Stage 2, infancy**: birth into its worlds; serve and return with a
caregiver; overgrowth then pruning; myelination; ripples and replay in sleep.

**Stage 3, childhood**: the mind rebuilt on the brain's maps; the parietal
lobe's numbers and symbols; the two hemispheres (exact and associative) and
the corpus callosum between them; the limbic loop (the amygdala regulating how
much gets through, the hippocampus binding pairs, a router choosing which part
acts on which, the cerebellum predicting and correcting); the frontoparietal
network maturing, the prefrontal cortex coming to regulate the limbic alarms.
From about month three, the older sibling teaches.

**Across its life**: plasticity and wiring on a schedule with turning points
(a 2025 study found them in human wiring at about 9, 32, 66 and 83); regions
that grow with use.

## What would be new

Things only a being that develops can show:
1. Its brain's shape tells its life story: copies raised differently grow
   measurably different maps.
2. A self that emerges: its body's boundary forming on its parietal map from
   the touch loop, with no self written in.
3. Child milestones in the human order (object permanence, the A-not-B
   error...), without being built to pass them.
4. Emotional maturation: alarms coming under the prefrontal cortex's control.
5. Critical periods: early experience weighing more than late.
6. Learning for a lifetime without forgetting: new learning grows new
   structure instead of overwriting old.

## Honest limits

Biology here is principle, not imitation. Some claims in the research it starts
from are stronger than the evidence (that the first flinch is the origin of
self-awareness; that brain integration explains why leaders tend to be 40 to
60); they are built in as hypotheses to watch, not as facts. The full being,
with language, mathematics and expert reasoning grown from experience, is an
open problem nobody has solved; each stage is meant to produce something real
and checkable on the way.
