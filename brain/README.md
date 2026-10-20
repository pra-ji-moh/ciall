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
hippocampus, amygdala, basal ganglia, thalamus, hypothalamus, tectum,
tegmentum, cerebellum, brainstem, spinal cord (sensory and motor), and a
retina. The thalamus grows four nuclei, each with the whole map, one for each
cortical area it relays to. They are wired by 46 rules: the cortical column, the senses arriving
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
it which touch was its own. After twenty days it still feels about 65% of its
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
| **Hearing** (two cochleas; the tectum; the thalamus's hearing nucleus) | When the next heartbeat comes, from a travelling wave across the cerebellum that holds how long ago a sound came (the notes' "rippling"; Keller and Welling) | The heartbeat comes to be expected (about 41% still heard), the irregular voice does not (about 98%); a missed beat leaves a signal of absence as strong as a beat, silence none |
| **Vision in the dark** (the eyes' first connections land broadly: the gene `sense_spread`) | Retinal waves sweep the eyes; inputs that fire with their neuron strengthen, each neuron's total is held fixed so they compete, the weak are pruned | Receptive fields narrow by about a third, about half the first connections are pruned; noise of the same amount barely refines them |
| **The reflex loop** | Hand to mouth, lips and mouth muscles active together: Hebb | The lips-to-mouth reflex grows about fivefold; a reflex never paired stays as grown |

`test_womb.c` checks all of it (17 checks).

## The newborn

```
cc -std=c99 -O2 ob_grow.c ob_view.c ob_womb.c being.c -o being -lm
./being genome.txt ../newborn
```

Grows the newborn from its genome and lives the womb, on its own, in about
five seconds, and writes [`newborn/diary.txt`](../newborn/diary.txt) (its life
week by week, in its own words, every number measured), its brain map, its
pictures, and `brain.bin`: its whole brain, saved, so its life can go on after
birth from exactly where it was born.

## What it is not

Not a model of 86 billion neurons or of any one animal: about 12,500 neurons
and 385,000 connections, a scaffold built on biology's principles, simplified.
The regions and wiring rules are a plausible plan, written down so they can be
argued with and changed. Activity is a rate between 0 and 1, not spikes. Each
sense's learning is its own rule, written where it can be read, not one rule
for everything; some of the womb's events (when the mouth moves with the hand,
what the mother eats) are scripted, not caused by the being. Its life after
birth is the next stage.
