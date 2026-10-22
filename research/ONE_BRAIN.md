# One brain

There were three brains, built at different times, that did not touch:

| | What it is | Where |
|---|---|---|
| The grown brain | Anatomy: 13,000 neurons and 396,000 connections grown from the genome, region by region; the womb's learning written into its synapses | `brain/ob_grow.c`, `ob_womb.c` |
| The life brain | A compact sketch of a few regions that lives: a restless brainstem, a cerebellum that predicts, dopamine from learning progress, basal ganglia that learn, a hippocampus that keeps every moment | `brain/ob_life.c` |
| The mind | Baby Ciall's cognition in practice worlds: an object map, prediction, invention, imagination, intuition, a controller choosing among them, sleep, self-correction | `certifiable-c/smarsh_mind.c` |

So the genome's ideas (a thicker corpus callosum, Einstein's parietal lobe)
changed wiring that no living thought travelled, and the mind's ideas ran in a
brain-less program. This is the plan to make them one: the grown brain is the
one that lives, and the other two become what its regions do.

## What each region does

| Region | In the one brain | From |
|---|---|---|
| Retina, cochlea, skin, inner ear (new: the vestibule) | Where the world arrives: the camera's view, the sound's bands, touch, being moved | the grown brain |
| Spinal sensory, at the head | The eye muscles' stretch: where its eyes point | new |
| Thalamus, four nuclei | Each sense's relay to its cortex; where predictions cancel what was expected, and what is left is surprise | the grown brain, the womb |
| Brainstem | Restless neurons; zones of them are its acts: eyes left, right, up, down, voice, a jerk of the body; a zone acts when its neurons' own activity, with what the basal ganglia add, crosses threshold | the life brain, as neurons |
| Cerebellum | Efference copies and the senses arrive and linger a few moments; its synapses onto the thalamus learn, by the delta rule, to predict each sense | the womb's touch learning, for every sense |
| Tegmentum | Dopamine: learning progress, from the thalamus's surprise, in each situation | the life brain |
| Basal ganglia | Hear the cortex (where it looks, what it sees, what it did), and learn on their synapses to the brainstem which urges lead to progress | the mobile's learning, for every act |
| Hippocampus | Keeps every moment, filed under its own neurons' activity; expects what came next when now repeats a moment; learns where that can be trusted | the life brain's memory, keyed by real neurons |
| Parietal cortex | The map of objects and space | the mind (phase 2) |
| Frontal cortex | The controller: explore, follow intuition, imagine, ask; and self-correction | the mind (phase 2) |
| Two hemispheres, corpus callosum | One exact and eliminating (Hartley), one associative (Shannon); the callosum carries between them | phase 2 and after |

## Phases

1. **The grown brain lives** (`brain/ob_one.c`). Its own neurons carry the
   senses, act, predict and learn; the same checks as the life brain's
   (`test_life.c`) must pass on it: it acts of itself, it is not caught by the
   television's snow, it tells its own voice and jerks from its mother's, it
   half expects her answers. Then its memory moves into its hippocampus, and
   `test_memory.c`'s checks must pass on it too.
2. **The mind becomes its cortex.** The mind's parts run on its regions, and
   it plays the practice worlds through its own eyes and acts.
3. **The one brain on the phone.** The phone grows it from its genome and
   lives it.

**Where it stands.** Phase 1 is done, with one part deferred: the grown brain
lives, keeps every moment, and with adaptation and memory passes the sketch's
checks about as well as the sketch (`brain/README.md` has the numbers and the
ten changes it took). The newborn's own brain, after the womb, lives on this
way (`being.c`). Its memory is the sketch's hippocampus, shared, each sense
filing its own: filing moments under the hippocampus's own neurons waits for a
cortex whose patterns repeat when the world does, which is phase 2's first job
too (the mind's object map needs the same).

**Phase 2, step 1, done:** its cortex learns what it sees (`test_cortex.c`); the
entorhinal cortex is grown as the hippocampus's gateway; its hippocampus has
patterns of its own, and it recalls episodes through them (a little help, so
far; the per-sense memory still does most). 

**Phase 2, step 2, done:** the frontal cortex reasons by elimination (Hartley),
and it grows into it. The brain ages (`ob_one_age`), and the myelination wave
goes through it, the frontal cortex last. Its DLPFC holds hypotheses as groups
of neurons that keep themselves firing, as many as their myelin lets them
against shared inhibition: none before 8 months, 39 at three years. Its ACC
darkens what an outcome contradicts. In one unbroken life, from genes through
the womb to three years, it meets a blicket box (a rule of 144) and needs 143
tries at 9 months, 73 at a year, 18 at two, 12 at three; the same life without
frontal reasoning barely makes it chime (`test_frontal.c`; `brain/README.md`).

**Next:** the being proposes its own hypotheses, from what its cortex has
learned, instead of being handed a list.

The life brain (`ob_life.c`) stays as the sketch it was: smaller, faster, and
the thing to compare the one brain against. Where the one brain does worse
than the sketch, that is a result, written down.

Still to discuss before they are built: sharpening the big ideas into tests
that could fail, and which life stage switches each idea on.
