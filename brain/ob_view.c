/*
 * ob_view.c -- the open brain, made readable: a text map of everything it is,
 * and pictures of it. PNG is written here without any library (uncompressed
 * deflate blocks), so nothing outside this folder is needed to look at it.
 */

#include "ob.h"

#include <stdlib.h>
#include <string.h>

/* ---- the map ----------------------------------------------------------------- */

void ob_write_map(const ob_brain_t *b, FILE *out) {
  unsigned r, k, x, y, layer_n[OB_LAYERS] = {0}, inhib = 0u, deepest_first = 1u;
  static const char LETTER[OB_REGIONS] = {'.', 'F', 'P', 'T', 'O', 'H', 'A', 'B', 't', 'h',
                                          'c', 'g', 'C', 'S', 's', 'm', 'L', 'R', 'o', 'l', 'r', 'v', 'w', 'i'};
  if (b == 0 || out == 0) return;
  fprintf(out, "# open brain map\n# grown from the genome below; every number here can be checked against it\n\n");
  fprintf(out, "## genome\n");
  ob_genome_save(&b->genes, out);
  fprintf(out, "\n## the plate after folding: one letter per progenitor, head (left) to tail (right),\n");
  fprintf(out, "## the left side above the midline, the right side below; the roof at top and bottom edges\n");
  for (y = 0u; y < b->plate.h; y++) {
    if (y == b->plate.h / 2u) fprintf(out, "   %.*s  midline (floor of the tube)\n", (int)b->plate.w,
                                      "----------------------------------------------------------------------"
                                      "--------------------------");
    fprintf(out, "   ");
    for (x = 0u; x < b->plate.w; x++) fputc(LETTER[b->plate.fate[x][y]], out);
    fprintf(out, "\n");
  }
  fprintf(out, "   key:");
  for (r = 1u; r < OB_REGIONS; r++) fprintf(out, " %c %s;", LETTER[r], ob_region_name(r));
  fprintf(out, "\n\n## regions: neurons on the left and right\n");
  for (r = 1u; r < OB_REGIONS; r++) {
    fprintf(out, "   %-24s %6u %6u\n", ob_region_name(r), b->count[r][0], b->count[r][1]);
  }
  for (k = 0u; k < b->n_neurons; k++) {
    const ob_neuron_t *n = &b->neuron[k];
    if (n->region < OB_FRONTAL || n->region > OB_OCCIPITAL) continue;
    if (n->inhibitory) {
      inhib++;
      continue;
    }
    layer_n[n->layer]++;
  }
  /* inside out, checked: within each column the first born are in the deepest layer */
  for (k = 0u; k + 1u < b->n_neurons; k++) {
    const ob_neuron_t *a = &b->neuron[k], *c = &b->neuron[k + 1u];
    if (a->inhibitory || c->inhibitory || a->px != c->px || a->py != c->py) continue;
    if (a->region < OB_FRONTAL || a->region > OB_OCCIPITAL) continue;
    if (c->birth > a->birth && c->layer < a->layer) deepest_first = 0u;
  }
  fprintf(out, "\n## the cortex: excitatory neurons by layer (born inside out: %s), and inhibitory ones\n",
          deepest_first ? "first born deepest, yes" : "NO");
  fprintf(out, "   layer 6 %u, layer 5 %u, layer 4 %u, layer 3 %u, layer 2 %u; inhibitory (migrated in) %u\n",
          layer_n[0], layer_n[1], layer_n[2], layer_n[3], layer_n[4], inhib);
  fprintf(out, "\n## projections: connections grown by each rule, before any pruning\n");
  for (k = 0u; k < 64u; k++) {
    if (b->rule_count[k] == 0u) continue;
    fprintf(out, "   %8u  %s\n", b->rule_count[k], ob_rule_name(k));
  }
  fprintf(out, "\n## in all: %u neurons, %u connections", b->n_neurons, b->n_syn);
  if (b->dropped) fprintf(out, " (%u more did not fit)", b->dropped);
  fprintf(out, "\n");
}

/* ---- PNG, with no library ------------------------------------------------------ */

static unsigned CRC_TABLE[256];

static unsigned crc(const unsigned char *p, size_t n, unsigned c) {
  size_t i;
  if (CRC_TABLE[1] == 0u) {
    unsigned k, v;
    for (k = 0u; k < 256u; k++) {
      v = k;
      for (i = 0u; i < 8u; i++) v = (v & 1u) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
      CRC_TABLE[k] = v;
    }
  }
  c = ~c;
  for (i = 0u; i < n; i++) c = CRC_TABLE[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
  return ~c;
}

static void put32(unsigned char *p, unsigned v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, unsigned n) {
  unsigned char head[8], tail[4];
  unsigned c;
  put32(head, n);
  memcpy(head + 4, type, 4);
  fwrite(head, 1, 8, f);
  if (n) fwrite(data, 1, n, f);
  c = crc(head + 4, 4, 0u);
  c = crc(data, n, c);
  put32(tail, c);
  fwrite(tail, 1, 4, f);
}

/* rgb is w*h*3 bytes. Written as a PNG of uncompressed deflate blocks. */
static int write_png(const char *path, const unsigned char *rgb, unsigned w, unsigned h) {
  FILE *f = fopen(path, "wb");
  unsigned char ihdr[13];
  size_t raw_n = (size_t)h * (1u + 3u * w), blocks, zn, i, at = 0u;
  unsigned char *raw, *z;
  unsigned a = 1u, b2 = 0u, y;
  static const unsigned char SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (f == 0) return 1;
  raw = (unsigned char *)malloc(raw_n);
  blocks = (raw_n + 65534u) / 65535u;
  zn = 2u + raw_n + blocks * 5u + 4u;
  z = (unsigned char *)malloc(zn);
  if (raw == 0 || z == 0) {
    free(raw);
    free(z);
    fclose(f);
    return 1;
  }
  for (y = 0u; y < h; y++) {
    raw[y * (1u + 3u * w)] = 0u;
    memcpy(raw + y * (1u + 3u * w) + 1u, rgb + (size_t)y * 3u * w, 3u * w);
  }
  z[at++] = 0x78;
  z[at++] = 0x01;
  for (i = 0u; i < raw_n; i += 65535u) {
    size_t len = raw_n - i < 65535u ? raw_n - i : 65535u;
    z[at++] = (unsigned char)(i + len >= raw_n ? 1 : 0);
    z[at++] = (unsigned char)(len & 0xFFu);
    z[at++] = (unsigned char)(len >> 8);
    z[at++] = (unsigned char)(~len & 0xFFu);
    z[at++] = (unsigned char)((~len >> 8) & 0xFFu);
    memcpy(z + at, raw + i, len);
    at += len;
  }
  for (i = 0u; i < raw_n; i++) {
    a = (a + raw[i]) % 65521u;
    b2 = (b2 + a) % 65521u;
  }
  put32(z + at, (b2 << 16) | a);
  at += 4u;
  fwrite(SIG, 1, 8, f);
  put32(ihdr, w);
  put32(ihdr + 4, h);
  ihdr[8] = 8u;
  ihdr[9] = 2u;
  ihdr[10] = ihdr[11] = ihdr[12] = 0u;
  chunk(f, "IHDR", ihdr, 13u);
  chunk(f, "IDAT", z, (unsigned)at);
  chunk(f, "IEND", 0, 0u);
  free(raw);
  free(z);
  return fclose(f) != 0;
}

/* ---- colours ----------------------------------------------------------------------- */

static const unsigned char REGION_RGB[OB_REGIONS][3] = {
    {20, 20, 24},    {230, 120, 60},  {70, 140, 230}, {240, 200, 70}, {120, 200, 110}, {200, 90, 200},
    {220, 60, 80},   {150, 110, 80},  {90, 210, 210}, {170, 150, 220}, {250, 160, 190}, {120, 120, 150},
    {110, 170, 60},  {140, 140, 140}, {240, 240, 240}, {180, 180, 120}, {255, 255, 255}, {255, 255, 255},
    {255, 230, 120}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {250, 160, 190}};

static void fill_rect(unsigned char *img, unsigned w, unsigned x0, unsigned y0, unsigned rw, unsigned rh,
                      const unsigned char *c) {
  unsigned x, y;
  for (y = y0; y < y0 + rh; y++) {
    for (x = x0; x < x0 + rw; x++) memcpy(img + 3u * ((size_t)y * w + x), c, 3);
  }
}

int ob_draw_plate(const ob_brain_t *b, const char *path) {
  const unsigned S = 8u;
  unsigned w = b->plate.w * S, h = b->plate.h * S, x, y;
  unsigned char *img = (unsigned char *)calloc((size_t)w * h * 3u, 1u);
  int rc;
  if (img == 0) return 1;
  for (x = 0u; x < b->plate.w; x++) {
    for (y = 0u; y < b->plate.h; y++) fill_rect(img, w, x * S, y * S, S, S, REGION_RGB[b->plate.fate[x][y]]);
  }
  /* the midline */
  {
    static const unsigned char MID[3] = {0, 0, 0};
    fill_rect(img, w, 0u, (b->plate.h / 2u) * S - 1u, w, 2u, MID);
  }
  rc = write_png(path, img, w, h);
  free(img);
  return rc;
}

int ob_draw_morphogens(const ob_brain_t *b, const char *path) {
  const unsigned S = 8u;
  unsigned w = b->plate.w * S, h = b->plate.h * S, x, y;
  unsigned char *img = (unsigned char *)calloc((size_t)w * h * 3u, 1u);
  int rc;
  if (img == 0) return 1;
  for (x = 0u; x < b->plate.w; x++) {
    for (y = 0u; y < b->plate.h; y++) {
      unsigned char c[3];
      c[0] = (unsigned char)(255.0 * b->plate.bmp[x][y]);   /* red: BMP from the roof */
      c[1] = (unsigned char)(255.0 * b->plate.wnt[x]);      /* green: Wnt towards the tail */
      c[2] = (unsigned char)(255.0 * b->plate.shh[x][y]);   /* blue: SHH from the floor */
      fill_rect(img, w, x * S, y * S, S, S, c);
    }
  }
  rc = write_png(path, img, w, h);
  free(img);
  return rc;
}

/* The two hemispheres seen from above, front at the top: each column of
   cortex (one progenitor's neurons) drawn as a block in its area's colour,
   with its five layers as stripes, deepest darkest; a white dot for each
   inhibitory neuron that migrated into it. Between the hemispheres, the
   corpus callosum, as thick as the share of connections that cross. */
int ob_draw_cortex(const ob_brain_t *b, const char *path) {
  const unsigned S = 10u, GAP = 60u, M = 12u;
  unsigned tel = 0u, half = b->plate.h / 2u, x, y, k, crossing = 0u, cortical_in = 0u;
  unsigned W, H, HW;
  unsigned char *img;
  static unsigned char inter[OB_PLATE_MAX_W][OB_PLATE_MAX_H];
  int rc;
  for (x = 0u; x < b->plate.w; x++) {
    for (y = 0u; y < b->plate.h; y++) {
      unsigned f = b->plate.fate[x][y];
      if (f >= OB_FRONTAL && f <= OB_AMYGDALA && x + 1u > tel) tel = x + 1u;
    }
  }
  HW = half * S;
  W = 2u * M + 2u * HW + GAP;
  H = 2u * M + tel * S;
  img = (unsigned char *)calloc((size_t)W * H * 3u, 1u);
  if (img == 0) return 1;
  memset(inter, 0, sizeof inter);
  for (k = 0u; k < b->n_neurons; k++) {
    const ob_neuron_t *n = &b->neuron[k];
    if (!n->inhibitory) continue;
    /* where it ended up: the column it migrated to, found by its sheet position */
    {
      unsigned bx, by, best = 0xFFFFFFFFu, cx = 0u, cy = 0u;
      for (bx = 0u; bx < tel; bx++) {
        for (by = 0u; by < b->plate.h; by++) {
          unsigned side = by < half ? OB_LEFT : OB_RIGHT;
          if (b->plate.fate[bx][by] != n->region || side != n->side) continue;
          /* nearest column of its area to where it sits (good enough for a picture) */
          {
            unsigned d = (unsigned)((bx * 7u + by * 13u + k) % 97u);   /* a fixed scatter, not a measurement */
            if (d < best) {
              best = d;
              cx = bx;
              cy = by;
            }
          }
        }
      }
      if (best != 0xFFFFFFFFu && inter[cx][cy] < 255u) inter[cx][cy]++;
    }
  }
  for (x = 0u; x < tel; x++) {
    for (y = 0u; y < b->plate.h; y++) {
      unsigned f = b->plate.fate[x][y], side = y < half ? OB_LEFT : OB_RIGHT, l, j;
      unsigned across = side == OB_LEFT ? y : b->plate.h - 1u - y;   /* 0 at the roof edge */
      /* the roof edge (where the hippocampus forms) faces the other hemisphere, as it does */
      unsigned px = side == OB_LEFT ? M + (half - 1u - across) * S : M + HW + GAP + across * S;
      unsigned py = M + x * S;
      if (f < OB_FRONTAL || f > OB_AMYGDALA) continue;
      for (l = 0u; l < OB_LAYERS; l++) {
        unsigned char c[3];
        for (j = 0u; j < 3u; j++) c[j] = (unsigned char)(REGION_RGB[f][j] * (0.35 + 0.65 * l / (OB_LAYERS - 1.0)));
        fill_rect(img, W, px, py + l * 2u, S - 1u, 2u, c);
      }
      if (inter[x][y]) {
        static const unsigned char WHITE[3] = {255, 255, 255};
        fill_rect(img, W, px + S / 2u - 1u, py + S / 2u - 1u, 2u, 2u, WHITE);
      }
    }
  }
  for (k = 0u; k < b->n_syn; k++) {
    unsigned to;
    (void)to;
    if (strcmp(ob_rule_name(b->syn[k].rule), "corpus callosum") == 0) crossing++;
  }
  for (k = 0u; k < b->n_neurons; k++) {
    if (b->neuron[k].region >= OB_FRONTAL && b->neuron[k].region <= OB_OCCIPITAL) cortical_in += b->neuron[k].syn_n;
  }
  {
    static const unsigned char BRIDGE[3] = {235, 235, 235};
    unsigned span = tel * S, thick = cortical_in ? (unsigned)(span * 3.0 * crossing / (double)cortical_in) : 2u;
    if (thick < 2u) thick = 2u;
    if (thick > span) thick = span;
    fill_rect(img, W, M + HW + 8u, M + span / 2u - thick / 2u, GAP - 16u, thick, BRIDGE);
  }
  rc = write_png(path, img, W, H);
  free(img);
  return rc;
}

/* Retinotopy: colour each left-retina neuron by where it sits (red across,
   green down), then colour each neuron of the right occipital cortex's layer 4
   by the average colour of the retina it ultimately hears from, through the
   thalamus. Order on the retina, before anything is seen, shows as order on
   the cortex: a smooth rainbow, not noise. */
int ob_draw_retinotopy(const ob_brain_t *b, const char *path) {
  const unsigned W = 520u, H = 260u, S = 220u;
  unsigned char *img = (unsigned char *)calloc((size_t)W * H * 3u, 1u);
  static double sum_r[OB_MAX_NEURONS], sum_g[OB_MAX_NEURONS];
  static unsigned hits[OB_MAX_NEURONS];
  unsigned k, s;
  int rc;
  if (img == 0) return 1;
  memset(hits, 0, sizeof hits);
  memset(sum_r, 0, sizeof sum_r);
  memset(sum_g, 0, sizeof sum_g);
  /* the thalamus: what each of its neurons hears from the retina */
  for (k = 0u; k < b->n_neurons; k++) {
    const ob_neuron_t *n = &b->neuron[k];
    if (n->region != OB_THALAMUS) continue;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      const ob_neuron_t *src = &b->neuron[b->syn[s].from];
      if (src->region != OB_RETINA_L && src->region != OB_RETINA_R) continue;
      sum_r[k] += src->sx;
      sum_g[k] += src->sy;
      hits[k]++;
    }
  }
  /* the occipital cortex, layer 4: what it hears from the thalamus's retina-fed neurons */
  for (k = 0u; k < b->n_neurons; k++) {
    const ob_neuron_t *n = &b->neuron[k];
    double rr = 0.0, gg = 0.0;
    unsigned h = 0u;
    if (n->region != OB_OCCIPITAL || n->layer != 2u || n->inhibitory) continue;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      unsigned from = b->syn[s].from;
      if (b->neuron[from].region != OB_THALAMUS || hits[from] == 0u) continue;
      rr += sum_r[from] / hits[from];
      gg += sum_g[from] / hits[from];
      h++;
    }
    if (h > 0u) {
      sum_r[k] = rr / h;
      sum_g[k] = gg / h;
      hits[k] = h;
    }
  }
  for (k = 0u; k < b->n_neurons; k++) {
    const ob_neuron_t *n = &b->neuron[k];
    unsigned char c[3];
    unsigned x, y;
    if (n->region == OB_RETINA_L) {
      c[0] = (unsigned char)(255.0 * n->sx);
      c[1] = (unsigned char)(255.0 * n->sy);
      c[2] = 90u;
      x = 20u + (unsigned)(n->sx * (S - 10u));
      y = 20u + (unsigned)(n->sy * (S - 10u));
      fill_rect(img, W, x, y, 8u, 8u, c);
    } else if (n->region == OB_OCCIPITAL && n->layer == 2u && !n->inhibitory && n->side == OB_RIGHT &&
               hits[k] > 0u) {
      c[0] = (unsigned char)(255.0 * sum_r[k]);
      c[1] = (unsigned char)(255.0 * sum_g[k]);
      c[2] = 90u;
      x = 280u + (unsigned)(n->sx * (S - 10u));
      y = 20u + (unsigned)(n->sy * (S - 10u));
      fill_rect(img, W, x, y, 6u, 6u, c);
    }
  }
  rc = write_png(path, img, W, H);
  free(img);
  return rc;
}

int ob_write_png_rgb(const char *path, const unsigned char *rgb, unsigned w, unsigned h) {
  return write_png(path, rgb, w, h);
}

/* ---- the brain to a file and back ----------------------------------------------- */

static const char OB_MAGIC[8] = {'O', 'B', 'R', 'A', 'I', 'N', '0', '1'};

int ob_save(const ob_brain_t *b, const char *path) {
  FILE *f;
  int bad;
  if (b == 0 || path == 0 || (f = fopen(path, "wb")) == 0) return 1;
  bad = fwrite(OB_MAGIC, 1, 8, f) != 8u || fwrite(&b->genes, sizeof b->genes, 1, f) != 1u ||
        fwrite(&b->plate, sizeof b->plate, 1, f) != 1u || fwrite(&b->n_neurons, sizeof b->n_neurons, 1, f) != 1u ||
        fwrite(&b->n_syn, sizeof b->n_syn, 1, f) != 1u ||
        fwrite(b->neuron, sizeof(ob_neuron_t), b->n_neurons, f) != b->n_neurons ||
        fwrite(b->syn, sizeof(ob_synapse_t), b->n_syn, f) != b->n_syn ||
        fwrite(b->count, sizeof b->count, 1, f) != 1u || fwrite(b->rule_count, sizeof b->rule_count, 1, f) != 1u;
  return (fclose(f) != 0) || bad;
}

int ob_load(ob_brain_t *b, const char *path) {
  FILE *f;
  char magic[8];
  int bad;
  if (b == 0 || path == 0 || (f = fopen(path, "rb")) == 0) return 1;
  bad = fread(magic, 1, 8, f) != 8u || memcmp(magic, OB_MAGIC, 8) != 0 ||
        fread(&b->genes, sizeof b->genes, 1, f) != 1u || fread(&b->plate, sizeof b->plate, 1, f) != 1u ||
        fread(&b->n_neurons, sizeof b->n_neurons, 1, f) != 1u || fread(&b->n_syn, sizeof b->n_syn, 1, f) != 1u;
  if (!bad && (b->n_neurons > OB_MAX_NEURONS || b->n_syn > OB_MAX_SYN)) bad = 1;
  if (!bad) {
    bad = fread(b->neuron, sizeof(ob_neuron_t), b->n_neurons, f) != b->n_neurons ||
          fread(b->syn, sizeof(ob_synapse_t), b->n_syn, f) != b->n_syn ||
          fread(b->count, sizeof b->count, 1, f) != 1u || fread(b->rule_count, sizeof b->rule_count, 1, f) != 1u;
  }
  b->dropped = 0u;
  fclose(f);
  return bad;
}
