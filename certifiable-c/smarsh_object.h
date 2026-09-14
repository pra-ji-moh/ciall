/*
 * smarsh_object.h -- worlds made of things, not of columns.
 *
 * ============================================================
 * WHAT THIS ADDS
 * ============================================================
 * smarsh_frame.h finds a description when a situation is a row of numbers.
 * Most of what there is to reason about is not a row of numbers. A set, a
 * word, a graph, a triangle: each is a collection of THINGS, with something
 * attached to each thing, and JOINS between them.
 *
 * So a situation here is a SCENE:
 *
 *   items    any number of them, each carrying a few numbers of its own
 *   joins    which items are joined to which
 *   outcome  what happened, when there is something to predict
 *
 * That one shape carries all of them. A set is items with nothing joined.
 * A word is items carrying a symbol, joined in a chain. A graph is items
 * and joins. A triangle is three items carrying an angle, or three corners
 * carrying coordinates and joined by sides.
 *
 * ============================================================
 * SAYING SOMETHING ABOUT ALL OF THEM AT ONCE
 * ============================================================
 * With things rather than columns, a description has to be able to speak of
 * all of them at once, so the language has quantifiers:
 *
 *   every x: ...            true of every item
 *   some x: ...             true of at least one
 *   every x and y: ...      true of every two distinct items
 *   every joined x and y:   true across every join
 *   how many x with ...     a number: the items a condition holds of
 *
 * and over a whole scene: how many items, how many joins, the total of an
 * attribute, the largest of it. "For every triangle, the angles come to a
 * straight angle" is then sayable, and is found by the same elimination
 * that finds everything else: it is the description that survives every
 * scene when the others do not.
 *
 * ============================================================
 * WHAT "THE SAME DESCRIPTION" MEANS HERE
 * ============================================================
 * Nothing is sampled. A finite universe is written out in full: every scene
 * of at most three things whose numbers come from a grid across the declared
 * range, with every possible set of joins among them, together with every
 * scene one step from one it actually saw (one number moved by one, one join
 * added or taken away, one thing dropped, one thing repeated). Against that
 * universe: a statement true in every scene of it rules nothing out, so it
 * says nothing; a statement that never fails where another holds is forced
 * by that one; and how much a statement rules out is an exact count. Each
 * judgement therefore has a scene behind it as a witness. The claim is
 * bounded and stated as such: proved over that universe, not beyond it.
 *
 * ============================================================
 * THE SCOPE, STATED
 * ============================================================
 * Two items can be spoken of at once, not three, so "for every three
 * points" is not sayable yet. Descriptions are bounded in size and in
 * number, and when that bound is reached it says so rather than claiming it
 * looked everywhere. Up to OB_MAX_ITEMS items in a scene, OB_MAX_ATTR
 * numbers on each, OB_MAX_SCENES scenes.
 */

#ifndef SMARSH_OBJECT_H
#define SMARSH_OBJECT_H

#include "smarsh_core.h"
#include "smarsh_interval.h"
#include "smarsh_space.h"

#define OB_MAX_ITEMS 12u
#define OB_MAX_ATTR 3u
#define OB_MAX_SCENES 16u
#define OB_MAX_FACTS 12u
#define OB_TEXT 160u
#define OB_TERM_SIZE 7u
#define OB_STMT_SIZE 9u
#define OB_BUDGET 600000

typedef struct {
  unsigned n_items;
  double attr[OB_MAX_ITEMS][OB_MAX_ATTR];
  unsigned char join[OB_MAX_ITEMS][OB_MAX_ITEMS];
  double outcome;
  int has_outcome;
} ob_scene_t;

typedef struct {
  unsigned n_attr;
  char attr_name[OB_MAX_ATTR][SX_NAME];
  double lo[OB_MAX_ATTR], hi[OB_MAX_ATTR];
  int whole[OB_MAX_ATTR];
  char outcome_name[SX_NAME];
  unsigned n_scenes;
  ob_scene_t scene[OB_MAX_SCENES];
} ob_world_t;

void ob_world_init(ob_world_t *w);
sm_status_t ob_attr(ob_world_t *w, const char *name, double lo, double hi, int whole,
                    unsigned *idx);
sm_status_t ob_outcome_name(ob_world_t *w, const char *name);
/* Start a new scene; items and joins go into the newest one. */
sm_status_t ob_scene(ob_world_t *w);
sm_status_t ob_item(ob_world_t *w, const double *attrs, unsigned *idx);
sm_status_t ob_join(ob_world_t *w, unsigned a, unsigned b);
sm_status_t ob_says(ob_world_t *w, double outcome);

typedef struct {
  int found;
  char law[OB_TEXT];
  char rival[OB_TEXT];
  int has_rival;
  unsigned candidates, survivors;
  double support;
  unsigned reached;                   /* longest description it got to write */
  int capped;
} ob_law_t;

/* The description of the outcome that survives every scene, shortest first. */
sm_status_t ob_find_law(const ob_world_t *w, ob_law_t *out);

typedef struct {
  unsigned n;
  char text[OB_MAX_FACTS][OB_TEXT];
  unsigned ruled_out[OB_MAX_FACTS];   /* imagined scenes each one rules out */
  unsigned universe;                  /* scenes it settled that over: every one there is */
  unsigned universe_items;            /* the largest scene in that universe */
  unsigned candidates;
  unsigned reached;                   /* longest thing it got to say */
  int capped;
} ob_facts_t;

/* What is true in every scene, with nothing to predict and no question. */
sm_status_t ob_facts(const ob_world_t *w, ob_facts_t *out);

void ob_report_law(const ob_world_t *w, const ob_law_t *l);
void ob_report_facts(const ob_world_t *w, const ob_facts_t *f);

#endif /* SMARSH_OBJECT_H */
