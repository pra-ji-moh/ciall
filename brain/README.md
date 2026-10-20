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
numbers) and four pictures. Growing takes a twentieth of a second. No library
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
retina. They are wired by 44 rules: the cortical column, the senses arriving
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

## What it is not

Not a model of 86 billion neurons or of any one animal: about 12,500 neurons
and 385,000 connections, a scaffold built on biology's principles, simplified.
The regions and wiring rules are a plausible plan, written down so they can be
argued with and changed. Nothing in it is active yet: no neuron fires. That
comes next, with the senses, in the order a child's come.
