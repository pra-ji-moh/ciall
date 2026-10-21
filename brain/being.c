/*
 * being.c -- the newborn's life, from its genome to its birth, on its own.
 *
 *   being [genome.txt] [dir]
 *
 * Grows its brain from the genome, then lives the womb, the senses coming
 * online in the order a fetus's do, is born, and lives its first months
 * (serve and return: the mobile), and writes into dir:
 *
 *   diary.txt      its life so far, week by week, in its own words
 *   brain_map.txt  everything its brain is, and what each stage did to it
 *   brain.bin      its whole brain (genes, neurons, connections), so its life
 *                  can go on after birth from exactly where it was born
 *   *.png          its brain's pictures, and its body as it knows it
 *
 * Nobody needs to be present. The same genome lives the same life.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ob.h"
#include "ob_crib.h"
#include "ob_one.h"

static ob_brain_t BRAIN;
static ob_womb_stats_t TOUCH;
static ob_smell_stats_t SMELL;
static ob_heart_stats_t HEART;
static ob_vision_stats_t VISION;
static ob_reflex_stats_t REFLEX;
static ob_mobile_stats_t MOBILE, YOKED;
static ob_one_t ONE;
static ob_crib_life_t CRIB;

static unsigned live_one(void *self, const float *in) { return ob_one_live((ob_one_t *)self, in); }

/* dir/name, if it fits; 0 if it does not */
static int join(char *out, size_t cap, const char *dir, const char *name) {
  size_t a = strlen(dir), b = strlen(name);
  if (a + b + 2u > cap) return 0;
  memcpy(out, dir, a);
  out[a] = '/';
  memcpy(out + a + 1u, name, b + 1u);
  return 1;
}

static FILE *open_in(const char *dir, const char *name, const char *mode) {
  char path[1024];
  return join(path, sizeof path, dir, name) ? fopen(path, mode) : 0;
}

static void picture(const char *dir, const char *name, int (*draw)(const ob_brain_t *, const char *)) {
  char path[1024];
  if (join(path, sizeof path, dir, name)) draw(&BRAIN, path);
}

int main(int argc, char **argv) {
  ob_genome_t g;
  const char *dir = argc > 2 ? argv[2] : "newborn";
  FILE *diary, *map;
  char path[1024];
  unsigned last;

  ob_genome_default(&g);
  if (argc > 1 && !ob_genome_load(&g, argv[1])) {
    fprintf(stderr, "being: cannot read %s\n", argv[1]);
    return 2;
  }
  diary = open_in(dir, "diary.txt", "w");
  if (diary == 0) {
    fprintf(stderr, "being: cannot write into %s (does it exist?)\n", dir);
    return 2;
  }

  fprintf(diary, "the diary of the newborn: its life from its genes to its birth\n");
  fprintf(diary, "(written by being.c; every number here is measured, none is told to it)\n\n");

  /* weeks 3 to 8: the brain builds itself */
  ob_grow(&BRAIN, &g);
  fprintf(diary, "weeks 3-4   a flat sheet of %u by %u cells folds into a tube. Three signals, from the floor, the\n"
                 "            roof and the tail, tell each cell what to become.\n", g.plate_w, g.plate_h);
  fprintf(diary, "weeks 5-8   %u neurons are born, in columns, the cortex inside out; %u connections grow, more than\n"
                 "            will be kept. My eyes are mapped onto my visual cortex before I have seen anything.\n",
          BRAIN.n_neurons, BRAIN.n_syn);

  /* week 8 on: touch */
  ob_womb_touch(&BRAIN, 20u, 300u, &TOUCH);
  last = TOUCH.days - 1u;
  fprintf(diary, "weeks 8-14  I twitch. My body presses the wall, my hand brushes my face. Sometimes something presses\n"
                 "            me that I did not cause. I learn to expect the touch my own moves bring: now I feel %.0f%%\n"
                 "            of my own touch and %.0f%% of what comes from outside. That is the first me, and not me.\n",
          100.0 * TOUCH.self_left[last], 100.0 * TOUCH.other_left[last]);

  /* week 14 on: taste and smell */
  ob_womb_smell(&BRAIN, 20u, 50u, &SMELL);
  fprintf(diary, "weeks 14-20 I taste and smell what my mother eats. Her own smell (%+.2f) and the anise in her meals\n"
                 "            (%+.2f) come to mean good; the smell of her stress (%+.2f) means bad. Smells that came with\n"
                 "            nothing I could taste mean nothing to me yet.\n",
          SMELL.after[0], SMELL.after[1], SMELL.after[5]);

  /* week 20 on: hearing */
  ob_womb_heartbeat(&BRAIN, 20u, 400u, &HEART);
  last = HEART.days - 1u;
  fprintf(diary, "weeks 20-28 I hear. Her heart, lub dub, again and again: I come to know when the next beat comes, and\n"
                 "            hear it at %.0f%%. Her voice I cannot predict; I hear it at %.0f%%. When a beat does not come,\n"
                 "            I notice the silence where it should be.\n",
          100.0 * HEART.beat_left[last], 100.0 * HEART.voice_left[last]);

  /* week 28 on: vision in the dark */
  ob_womb_vision(&BRAIN, 20u, 200u, 1, &VISION);
  fprintf(diary, "weeks 28-36 No light yet, but waves sweep my eyes from inside. Where my eyes connect to my brain, what\n"
                 "            fires together stays and the rest is let go: %u connections pruned, %u kept; each place in my\n"
                 "            seeing narrows from %.1f to %.1f cells across.\n",
          VISION.pruned, VISION.kept, VISION.field_before, VISION.field_after);

  /* week 36 on: the reflex, practised */
  ob_womb_reflex(&BRAIN, 20u, 100u, 1, &REFLEX);
  fprintf(diary, "weeks 36-40 I twitch of myself; my arms brush my face, my body presses the wall. Where skin and muscle\n"
                 "            go together the pull between them grows: lips to mouth from %.1f to %.1f, ready for feeding;\n"
                 "            the middle of my body from %.1f to %.1f.\n",
          REFLEX.mouth_before, REFLEX.mouth_after, REFLEX.body_before, REFLEX.body_after);

  fprintf(diary, "week 40     Born.\n");

  /* infancy: serve and return */
  {
    static ob_brain_t twin;
    static unsigned char sched[8u * 200u / 8u + 8u];
    ob_mobile_stats_t yoked;
    unsigned k;
    double mid = 0.0, mid_y = 0.0;
    memset(sched, 0, sizeof sched);
    ob_infant_mobile(&BRAIN, 8u, 200u, 1, sched, &MOBILE);
    /* a twin, from the same genes, answered at the same moments but not by its own kicks */
    ob_grow(&twin, &g);
    ob_infant_mobile(&twin, 8u, 200u, 0, sched, &yoked);
    for (k = 2u; k <= 6u; k++) {
      mid += MOBILE.kicks[k] / 5.0;
      mid_y += yoked.kicks[k] / 5.0;
    }
    YOKED = yoked;
    fprintf(diary, "month 3     A mobile hangs over me; a ribbon ties it to my leg. At first I kick %u times in a\n"
                   "            while. When my kicks move it and her voice answers, I kick %.0f times. My twin, who\n"
                   "            sees and hears the same but not because of anything it does, kicks %.0f. When it stops\n"
                   "            answering, I kick %u. The world answers me.\n",
            MOBILE.kicks[0], mid, mid_y, MOBILE.kicks[7]);
  }

  /* months 3 on: a life of its own, led by its curiosity: its own brain, the
     one the womb and the mobile shaped, goes on living (ob_one.c) */
  {
    static const char *const DID[OL_MOTORS] = {"look left", "look right", "look up", "look down", "make a sound",
                                               "jerk"};
    static ob_one_t frozen;
    ob_crib_being_t being;
    double x[OC_EXAMS];
    unsigned m, tenth, most[OC_TENTHS];
    ob_one_born(&ONE, &BRAIN, g.seed, OL_PROGRESS);
    being.self = &ONE;
    being.live = live_one;
    being.err = ONE.err;
    being.energy = ONE.energy;
    ob_crib_live_any(&being, g.seed, 40000ul, &CRIB);
    memcpy(&frozen, &ONE, sizeof frozen);
    frozen.learning = 0u;
    being.self = &frozen;
    being.err = frozen.err;
    being.energy = frozen.energy;
    ob_crib_examine_any(&being, g.seed + 1u, 20000ul, x);
    ob_one_settle(&ONE);
    memcpy(&BRAIN, &ONE.brain, sizeof BRAIN);   /* what it lived, kept in its brain */
    for (tenth = 0u; tenth < OC_TENTHS; tenth++) {
      most[tenth] = 0u;
      for (m = 1u; m < OL_MOTORS; m++)
        if (CRIB.acts[tenth][m] > CRIB.acts[tenth][most[tenth]]) most[tenth] = m;
    }
    fprintf(diary, "\nmonths 3-6  Nobody tells me what to do. My brainstem is restless; when it fires I look, or make a\n"
                   "            sound, or jerk. I like what I am getting better at predicting, and I get bored of\n"
                   "            what I know. What I did most, in each tenth of these months:\n           ");
    for (tenth = 0u; tenth < OC_TENTHS; tenth++)
      fprintf(diary, "%s %s", tenth == 0u ? "" : tenth == 5u ? ",\n           " : ",", DID[most[tenth]]);
    fprintf(diary, ".\n            In the end I look at the snow on the television %.0f%% of the time, and at each other\n"
                   "            corner %.0f%%: %s\n",
            100.0 * CRIB.tv, 100.0 * CRIB.corner,
            CRIB.tv <= 1.3 * CRIB.corner ? "it never gets any easier, so it holds nothing for me."
                                         : "it never gets any easier, and still it draws me.");
    fprintf(diary, "            Still surprising: my own voice %.0f%%, my mother talking %.0f%%, her answering me %.0f%%;\n"
                   "            my own jerks %.0f%%, being rocked %.0f%%; her touch %.0f%%.\n",
            100.0 * x[OC_VOICE], 100.0 * x[OC_TALK], 100.0 * x[OC_ANSWER], 100.0 * x[OC_JERK],
            100.0 * x[OC_ROCKED], 100.0 * x[OC_TOUCHED]);
    fprintf(diary, "            %s %s\n",
            x[OC_VOICE] < 0.8 * x[OC_TALK] ? "I know my own voice from hers." : "I cannot yet tell my voice from hers.",
            x[OC_JERK] < 0.8 * x[OC_ROCKED] ? "I know my own jerks from her rocking."
                                             : "I cannot yet tell my jerks from her rocking.");
    fprintf(diary, "            All of it in my own brain: my cerebellum's synapses to my thalamus learned what to\n"
                   "            expect, my basal ganglia's synapses to my brainstem what to want.\n");
  }
  fclose(diary);

  map = open_in(dir, "brain_map.txt", "w");
  if (map != 0) {
    ob_write_map(&BRAIN, map);
    ob_write_womb(&TOUCH, map);
    ob_write_smell(&SMELL, map);
    ob_write_heart(&HEART, map);
    ob_write_vision(&VISION, map);
    ob_write_reflex(&REFLEX, map);
    ob_write_mobile(&MOBILE, &YOKED, map);
    fclose(map);
  }
  picture(dir, "plate.png", ob_draw_plate);
  picture(dir, "morphogens.png", ob_draw_morphogens);
  picture(dir, "cortex.png", ob_draw_cortex);
  picture(dir, "retinotopy.png", ob_draw_retinotopy);
  if (join(path, sizeof path, dir, "self.png")) ob_draw_self(&BRAIN, &TOUCH, path);
  if (!join(path, sizeof path, dir, "brain.bin") || ob_save(&BRAIN, path) != 0)
    fprintf(stderr, "being: could not save its brain\n");
  printf("the newborn is born: %u neurons, %u connections; its diary is in %s/diary.txt\n", BRAIN.n_neurons,
         BRAIN.n_syn, dir);
  return 0;
}
