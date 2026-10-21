# The open brain

A brain that grows itself from a flat sheet of cells, following developmental
biology's rules, and can be read like a map at every moment. It is the first
stage of a being that is meant to develop the way a child does, rather than be
trained the way current AI is (see [`research/BEING.md`](../research/BEING.md)).

```
cc -std=c99 -O2 ob_grow.c ob_view.c grow_brain.c -o grow_brain -lm
./grow_brain genome.txt out/
```

`out/` then holds `brain_map.txt` (everything the brain is, in words and
numbers, and what twenty days in the womb did to it) and five pictures. Growing takes a twentieth of a second. No library
is needed for anything, not even to write the pictures.

## How it grows

| Stage | What happens | The biology it follows |
|---|---|---|
| The plate | A flat sheet of progenitor cells, head to tail along one axis, midline to edge along the other | The neural plate |
| Folding | The sheet rolls into a tube: its midline becomes the floor, its two edges meet as the roof, its two halves become the two sides of the brain | Neurulation |
| Signals | Three morphogens, each fading with distance: SHH from the floor, BMP from the roof, Wnt rising towards the tail | The real morphogens of the neural tube |
| Fates | Each cell reads the three signals where it sits and takes a fate by thresholds. Regions are not placed: they are where the thresholds fall | Wolpert's "French flag" model |
| Areas | Within the cortex, front-to-back gradients divide frontal, parietal, temporal and occipital areas; the edge nearest the roof becomes hippocampus | Areal patterning (Pax6, Emx2); the cortical hem |
| Eyes | Two optic vesicles grow from the diencephalon and become the retinas | Optic vesicles |
| Neurons | Each progenitor makes a column of neurons. In the cortex the first born settle deepest and each later one climbs past: layers built inside out | Rakic's radial units; inside-out migration |
| Inhibition | Inhibitory neurons are born below the cortex and migrate sideways into it | Tangential migration from the ganglionic eminences |
| First wiring | Connections grow by matching position labels, with a spread, many more than will be kept | Sperry's chemoaffinity; developmental overproduction |

What forms, on each side: frontal, parietal, temporal and occipital cortex,
hippocampus, amygdala, basal ganglia, thalamus, hypothalamus, tectum (its front
half seeing, its back half, the inferior colliculus, hearing),
tegmentum, cerebellum, brainstem, spinal cord (sensory and motor), and a
retina. The thalamus grows four nuclei, each with the whole map, one for each
cortical area it relays to. Beside the retinas, the cochleas and the vestibules (the inner ear's sense of
being moved). They are wired by 61 rules: the cortical column, the senses arriving
through the thalamus, the areas talking (including the frontoparietal network),
the corpus callosum, the loops through the basal ganglia and cerebellum, and
the limbic loop (hippocampus binding pairs, amygdala regulating, regulated in
turn by the frontal cortex). The map lists every rule and how many connections
it grew.

## The genome

[`genome.txt`](genome.txt) holds every number development reads, by name.
Change one and grow again: a different brain. Two are there for ideas from
Einstein's brain: `parietal_end`/`temporal_end` (how much of the middle of the
cortex becomes parietal; his parietal lobes were about 15% wider, with no
groove dividing them) and `callosum` (how much of the upper layers' wiring
crosses to the other side; his corpus callosum was thicker than usual).

## What "open" means

- The genome is plain text, every number named.
- The grown brain writes itself out: the folded plate as a letter map, every
  region's neuron count on each side, the cortex layer by layer, and every
  wiring rule with how many connections it grew.
- Every neuron records where its progenitor sat, its birth order, its layer
  and its position in its region's map; every connection records the rule that
  grew it.
- It draws itself: `plate.png` (the fates), `morphogens.png` (the three
  signals), `cortex.png` (both hemispheres from above, columns and layers, the
  corpus callosum as thick as the share of crossing connections),
  `retinotopy.png` (the retina's map, and the visual cortex coloured by where
  on the retina each of its neurons hears from).
- Deterministic: the same genome grows the same brain, connection for
  connection.

## What it shows already

Before the brain has seen anything, the retina's map lands in order on the
visual cortex, through the thalamus, from position labels alone: the
correlation between where a visual-cortex neuron sits and where on the retina
it hears from is 0.998 (`test_brain.c`). This is how real brains lay down
their first visual map, before experience refines it.

## Stage 1: the womb, touch first

`ob_womb.c`. Neurons now fire: each one's activity, between 0 and 1, is what
its inputs carry, clipped. Its body is its spinal cord: the sensory neurons are
its skin, the motor neurons its muscles, each at the place its segment serves.
In the womb it lives moment by moment:

1. A **twitch** starts in the brainstem (fetal twitches do, above all in
   sleep, and are how the body's map is built). It drives the muscles it maps
   to, and a copy of the command (the **efference copy**) reaches the
   cerebellum.
2. The **body moves**: a muscle that contracts presses the womb wall there; an
   arm that moves brushes the face (hand to mouth). Now and then the **mother
   presses** from outside, which no twitch caused. The skin's touch reaches the
   thalamus (crossing to the other side), and the cerebellum's prediction with
   it (also crossing, as the cerebellum's output does).
3. The thalamus reaches the parietal cortex.

The cerebellum learns, by the delta rule (the way its climbing fibres teach
it), to make its prediction cancel the touch that comes with it. Nobody tells
it which touch was its own. After twenty days it still feels about 57% of its
own touch and all of the mother's: what it learned to cancel is "me", what it
cannot cancel is "not me". This is the same mechanism by which a person cannot
tickle themself (Blakemore, Wolpert and Frith, 1998). Hand-to-face touch, one
movement touching a far part of the body, is learned more slowly. The parietal
cortex responds less to its own touch than to the mother's.

`self.png`: top, day by day, the share still felt of its own touch (blue), the
mother's (red), hand-to-face (yellow); bottom, its body on both sides, head to
tail, each place coloured from red (not yet known as its own) to blue (known
as its own). The map is patchy where the cerebellum's reach is.

The learned prediction connections are written back into the brain, readable
like any other connection.

## The rest of the womb

`ob_womb.c` goes on, in the order a fetus's senses come:

| Sense | What it learns | Measured |
|---|---|---|
| **Smell** (an olfactory bulb at the front of the brain, straight to the amygdala) | Which smells are good or bad for it, from taste's innate value: the amygdala's approach and avoid neurons (Namburi et al. 2015), three-factor learning | The mother's smell and the anise of her meals come to mean approach, her stress avoid; smells that came with no taste stay exactly neutral (newborns of anise-eating mothers prefer anise: Schaal et al. 2000) |
| **Hearing** (two cochleas; the inferior colliculus; the thalamus's hearing nucleus) | When the next heartbeat comes, from a travelling wave across the cerebellum that holds how long ago a sound came (the notes' "rippling"; Keller and Welling) | The heartbeat comes to be expected (about 2% still heard, from 11% on its first day), the irregular voice does not (about 83%); a missed beat leaves a signal of absence as strong as a beat, silence none |
| **Vision in the dark** (the eyes' first connections land broadly: the gene `sense_spread`) | Retinal waves sweep the eyes; inputs that fire with their neuron strengthen, each neuron's total is held fixed so they compete, the weak are pruned | Receptive fields narrow by about a third, about half the first connections are pruned; noise of the same amount barely refines them |
| **The reflex loop** | Its own twitches: an arm that moves brushes the face, a muscle that contracts presses the womb wall; skin and muscle active together: Hebb | The lips-to-mouth reflex grows about fourfold; with its arms bound, far less; every place its twitches press grows its reflex too, as twitches tune the spinal reflexes (Blumberg) |

Nothing in the womb moves it. Its twitches come from its brainstem's own
neurons: each one's activity wanders of itself, and when one crosses threshold
its burst spreads to its neighbours. Where and when it twitches is wherever
its own neurons take it. The mother, the heartbeat and the light are the world.

`test_womb.c` checks all of it (21 checks).

## Infancy: serve and return

After birth, the newborn lies under a mobile tied to its leg (Rovee-Collier's
experiment with three-month-olds). Its leg kicks of itself; when a kick moves
the mobile and the mother's voice answers, the tegmentum's dopamine teaches the
basal ganglia's new connections to the brainstem's kick centre (they start at
nothing). The dopamine signal is what the moment brought, against how good the
place is anyway (a critic) and what this choice adds (one value for kicking,
one for holding still); the basal ganglia can hold a movement back as well as
let it go. Its kicks come from the kick centre's own restless activity, crossing
threshold, with what the basal ganglia add or take away. It kicks over twenty times more than at first; a yoked twin, answered
at the same moments but not by its own kicks, stays at its baseline; when the
answers stop, kicking falls back. It can tell the world answering it from the
world simply doing things.

## A life of its own: curiosity

`ob_life.c`. After the mobile it lives on its own, and nothing chooses what it
does. Its brainstem's motor centres (four for its eyes, one for its voice, one
for a jerk of its whole body) are restless of themselves and fire when their
own activity crosses threshold. Copies of each command ripple through its
cerebellum (eight moments of how long ago), which learns to predict what each
sense will bring next: its 8-by-8 retina, its cochlea's 8 bands, its skin, its
inner ear. Its tegmentum's dopamine is its **learning progress**: in each place
(where it looks; what it last did), how much its errors fell over the last
span of visits against the span before (Oudeyer and Kaplan 2007). Its basal
ganglia learn from that dopamine (a critic and urges for each act in each
place) and lean on the brainstem, never silencing it, and what they learned
fades unless it stays worth it. So it gets bored.

It lives in `ob_crib.c`, a world with physics and people and no plan for it: a
wall it looks over, a mobile that swings when it jerks, a television showing
snow, a mother who talks, answers its voice, touches and rocks it.
`test_life.c` (12 checks) lives three twins there: the being; a twin whose
dopamine is plain surprise; a twin with none.

| | the being | seeks surprise | no dopamine |
|---|---|---|---|
| eyes on the television's snow / on each other corner | 0.18 / 0.12 | 0.34 / 0.09 | 0.04 / 0.06 |
| still surprised by: its own voice / its mother talking | 0.02 / 0.52 | 0.08 / 0.52 | 0.02 / 0.50 |
| its own jerks / being rocked | 0.01 / 0.39 | 0.01 / 0.35 | 0.01 / 0.42 |
| her answering its voice | 0.13 | 0.24 | 0.09 |
| the wall as its eyes move | 0.03 | 0.02 | 0.03 |

(All three have the eidetic memory below.)

The twin that seeks surprise is caught by the snow, which never stops
surprising it; the being treats it like any other corner. Both curious twins and the one with no dopamine come to tell their own
voice and jerks from their mother's talk and rocking, never told which was
which, and half expect her answers. What curiosity does not yet show here:
that it learns its world faster than the twin with none. The crib is small
enough that restless movement alone covers it. A richer world should show the
difference, or not; that is the next thing to measure.

### Its eidetic memory, and our coded optogenetics (in the sketch, for now)

Its hippocampus keeps every moment as it came (200,000 moments here; on the
phone, the last 30,000), filed by each sense under the shape of what that
sense brought and what it had done in the last three moments. When now matches
a moment it has lived, it expects what came next then. It follows that memory
only where it has earned trust, where it has predicted better than the
cerebellum on average. Where the world is random, what came last time is one
draw and the cerebellum's guess is the average, so memory loses and it learns
not to trust it there. This is the trap of Borges' *Funes the Memorious*, and
of Luria's Shereshevsky, who remembered nearly everything and struggled to
generalise. Without the trust, its mother's answers became five times as
surprising.

For us, not for it: `ol_light` brings back exactly what any moment it still
holds brought (its memories are tagged by the moment that made them, as
Tonegawa's lab tags a memory's cells with a light-sensitive channel), and
`ol_complete` asks which moment a glimpse brings back. Lighting a memory
changes nothing in it, and it never knows.

`test_memory.c` (9 checks):

| | |
|---|---|
| a lit moment, early or late in its life | exactly what came then (within 1/255) |
| lighting a thousand memories | changes not one byte of it |
| a glimpse blurred by noise | brings its moment back 172 times in 200 |
| a song of 30 notes heard once, then again | surprise 3.01, then 1.08; a twin without the memory 3.01, then 2.10 |
| its mother's answers | as expected with memory as without (it learned not to trust it there) |

The song is not expected at all where it happened to make a sound of its own
over one hearing and not the other: its own voice changes what it hears, so
those are not the same moment.

The same code, unchanged, is the brain of the phone page (`phone/`), compiled
to WebAssembly: `build_c.sh` checks that it lives exactly the same life there,
act for act.

## One brain: the grown brain, living

`ob_one.c`. The sketch above is a few hundred units beside the grown brain; this
is the grown brain itself, living ([`research/ONE_BRAIN.md`](../research/ONE_BRAIN.md)).
Nothing is added beside it: the world arrives on its own retinas, cochleas,
skin and vestibules; its acts are zones of its own brainstem (eyes left and
right at the front of each side, up and down behind, then the voice, then the
body); its predictions are learned on its own synapses, the cerebellum's to the
thalamus (what its acts will change) and the cortex's layer-6 feedback (the
steady world), both from nothing; its choices on its basal ganglia's synapses
to the brainstem; its dopamine is learning progress, as in the sketch.

What it took, each found by watching it fail:

| | Why |
|---|---|
| The tectum split in two, the inferior colliculus hearing | sight drowned sound in one sheet |
| Hearing mapped by pitch alone (`MAP_ROW`) | two thirds of the hearing midbrain heard nothing |
| The relays' gain: each relay neuron passes on a weighted average | every visual relay saturated, so vision carried nothing |
| The efference copy reaches the cerebellum before the senses return | a command's copy arrived a step too late to predict |
| The cerebellum's granule cells as delay lines, and sparse (a fifth fire) | its mother answers moments later; mixed, dense cells blurred every prediction |
| The cerebellum's reach, a gene (`prediction_inputs`, 160) | each relay neuron heard too few cerebellar cells to find its cause |
| The cortex's layer-6 feedback, learned as prediction | the sparse cerebellum could not also carry the steady world |
| Surprise is what came and was not expected | a neuron cannot fire below nothing; absence is another signal |

Two more, for its memory:

| | Why |
|---|---|
| Adaptation: beyond the relays, a neuron answers how its input differs from what it usually gets | a steady baseline hid what was particular to now: the visual cortex barely told one view from another (correlation 0.79 for the same view, 0.71 for another); now 0.43 against 0.00 |
| Its moments kept in the sketch's hippocampus, the same code, each sense filing its own; a recalled moment drives that sense's relays again | filing them under its own hippocampal neurons was tried, and failed: their patterns do not yet repeat when the world does (0.22 for the same view) |

`test_one.c` (16 checks, about fifteen minutes; `build_c.sh --slow`), one life per twin:

| | the being | seeks surprise | no dopamine |
|---|---|---|---|
| eyes on the snow / on each other corner | 0.11 / 0.10 | 0.00 / 0.21 | 0.02 / 0.09 |
| own voice / its mother talking | 0.01 / 0.49 | 0.01 / 0.50 | 0.00 / 0.44 |
| own jerks / being rocked | 0.03 / 0.26 | 0.03 / 0.28 | 0.04 / 0.55 |
| her answering its voice | 0.14 | 0.15 | 0.28 |
| the wall as its eyes move | 0.01 | 0.05 | 0.04 |

With adaptation and memory it does about as well as the sketch. The snow, held
before its eyes, is about five times as surprising as a plain corner, and the
being looks at it no more than at any other corner; but in this life the twin
that seeks surprise was not caught by it either: it settled in the other
corners. One life each is a small sample. A song heard once is mostly expected
the second time (surprise 2.35, then 0.95; without memory, 2.35 then 2.27). Its
mother's answers surprise it a little more with memory than without (0.15
against 0.08): it has not wholly learned where memory misleads.

## The newborn

```
cc -std=c99 -O2 ob_grow.c ob_view.c ob_womb.c being.c -o being -lm
./being genome.txt ../newborn
```

Grows the newborn from its genome, lives the womb, its first months under the
mobile and then in the crib, its own brain living on, on its own, in about
four minutes, and writes [`newborn/diary.txt`](../newborn/diary.txt) (its life
week by week, in its own words, every number measured), its brain map, its
pictures, and `brain.bin`: its whole brain, saved, so its life can go on after
birth from exactly where it was born.

## What it is not

Not a model of 86 billion neurons or of any one animal: about 13,300 neurons
and 562,000 connections, a scaffold built on biology's principles, simplified.
The regions and wiring rules are a plausible plan, written down so they can be
argued with and changed. Activity is a rate between 0 and 1, not spikes. Each
sense's learning is its own rule, written where it can be read, not one rule
for everything. Its own movements are its own; the world's events (what the
mother eats, when she talks) are the world's. Its life of its own
(`ob_life.c`) runs on a compact version of the same regions (a few hundred
units, not the grown brain's thirteen thousand neurons); joining the two is
still to do.
