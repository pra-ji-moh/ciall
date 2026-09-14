/*
 * stack_depth.c -- the worst-case stack of every public function, measured
 * from the compiler's own assembly, and checked against a stated bound.
 *
 *   stack_depth BOUND [-d CAP] file1.s file2.s ...
 *
 * Reads x86-64 assembly in AT&T syntax (what clang -S emits), and for each
 * function takes its frame as the compiler laid it out: return address,
 * one slot per register pushed in the prologue, plus the `subq $N, %rsp`
 * (or the size handed to __chkstk for large frames on Windows). It then
 * follows every call and tail jump to other functions in the same files
 * and takes the deepest chain.
 *
 * Recursion. Without -d, any cycle fails: a recursive chain has no bound
 * this can state. With -d CAP the caller asserts a depth counter: every
 * recursive entry checks depth < CAP, and every cycle through the
 * recursive functions increments it. Then no function can appear twice at
 * one depth, so a recursive group costs at most (CAP + 1) times the sum of
 * its frames (CAP levels that pass the check, plus the one that fails it).
 * That assertion is about the source, not the assembly, so it is checked
 * separately: build_c.sh greps for recursive calls that do not add one.
 *
 * Calls to functions outside the files (memset, memcpy, log2) count as
 * zero: they are library leaves, and their own stack is the C library's.
 * Stated rather than hidden.
 *
 * Exit 0 if every chain is within BOUND bytes, 1 otherwise.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FN 512
#define MAX_CALLS 32
#define NAME_LEN 64

typedef struct {
  char name[NAME_LEN];
  long frame;        /* bytes this function itself uses */
  int n_calls;
  char calls[MAX_CALLS][NAME_LEN];
  int state;         /* 0 unvisited, 1 on the stack, 2 done */
  long deep;         /* frame plus the deepest callee chain */
  int next;          /* the callee on that chain, -1 if none */
} fn_t;

static fn_t FN[MAX_FN];
static int n_fn = 0;

static int find(const char *name) {
  int i;
  for (i = 0; i < n_fn; i++) if (strcmp(FN[i].name, name) == 0) return i;
  return -1;
}

static int is_ident_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static void read_ident(const char *p, char *out) {
  int k = 0;
  while (k < NAME_LEN - 1 && (is_ident_start(*p) || (*p >= '0' && *p <= '9'))) out[k++] = *p++;
  out[k] = '\0';
}

static int parse(const char *path) {
  FILE *f = fopen(path, "r");
  char line[512];
  fn_t *cur = NULL;
  long pushes = 0, sub = 0, chk = 0;
  if (f == NULL) { printf("cannot open %s\n", path); return 0; }
  while (fgets(line, sizeof line, f) != NULL) {
    const char *t = line;
    if (is_ident_start(line[0])) {
      /* a label at column 0 that is not a local (.L) one starts a function */
      char name[NAME_LEN];
      read_ident(line, name);
      if (line[strlen(name)] == ':') {
        if (cur != NULL) cur->frame = 8 + pushes * 8 + sub;
        if (n_fn >= MAX_FN) { fclose(f); return 0; }
        cur = &FN[n_fn++];
        memset(cur, 0, sizeof *cur);
        strcpy(cur->name, name);
        cur->next = -1;
        pushes = sub = chk = 0;
        continue;
      }
    }
    if (cur == NULL) continue;
    while (*t == ' ' || *t == '\t') t++;
    if (strncmp(t, "pushq", 5) == 0 && strstr(t, "%r") != NULL) pushes++;
    if (strncmp(t, "subq", 4) == 0 && strstr(t, "%rsp") != NULL && sub == 0) {
      const char *d = strchr(t, '$');
      if (d != NULL) sub = strtol(d + 1, NULL, 10);
    }
    if (strncmp(t, "movl", 4) == 0 && strstr(t, "%eax") != NULL) {
      const char *d = strchr(t, '$');
      if (d != NULL) chk = strtol(d + 1, NULL, 10);
    }
    if (strstr(t, "__chkstk") != NULL && chk > 0) sub = chk;
    if ((strncmp(t, "callq", 5) == 0 || strncmp(t, "jmp", 3) == 0)) {
      const char *p = t + (t[0] == 'c' ? 5 : 3);
      while (*p == ' ' || *p == '\t') p++;
      if (is_ident_start(*p) && cur->n_calls < MAX_CALLS) {
        read_ident(p, cur->calls[cur->n_calls]);
        cur->n_calls++;
      }
    }
  }
  if (cur != NULL) cur->frame = 8 + pushes * 8 + sub;
  fclose(f);
  return 1;
}

/* ---- strongly connected components (Tarjan), so recursion is a group ---- */

static int CALLEE[MAX_FN][MAX_CALLS], N_CALLEE[MAX_FN];
static int IDX[MAX_FN], LOW[MAX_FN], ON[MAX_FN], STK[MAX_FN], COMP[MAX_FN];
static int sp = 0, counter = 0, n_comp = 0;
static long COMP_SUM[MAX_FN];
static int COMP_SIZE[MAX_FN], COMP_SELF[MAX_FN];

static void strong(int v) {
  int k;
  IDX[v] = LOW[v] = ++counter;
  STK[sp++] = v;
  ON[v] = 1;
  for (k = 0; k < N_CALLEE[v]; k++) {
    int w = CALLEE[v][k];
    if (IDX[w] == 0) {
      strong(w);
      if (LOW[w] < LOW[v]) LOW[v] = LOW[w];
    } else if (ON[w] && IDX[w] < LOW[v]) {
      LOW[v] = IDX[w];
    }
  }
  if (LOW[v] == IDX[v]) {
    int w;
    do {
      w = STK[--sp];
      ON[w] = 0;
      COMP[w] = n_comp;
      COMP_SUM[n_comp] += FN[w].frame;
      COMP_SIZE[n_comp]++;
    } while (w != v);
    n_comp++;
  }
}

static long CAP = -1;
static long COMP_DEEP[MAX_FN];
static int COMP_DONE[MAX_FN], COMP_NEXT[MAX_FN];

/* worst stack from entering component c: its own cost, plus the deepest
   component reachable by one call out of it */
static int comp_deep(int c) {
  int v, k;
  long own;
  if (COMP_DONE[c]) return 1;
  if (COMP_SIZE[c] > 1 || COMP_SELF[c]) {
    if (CAP < 0) {
      printf("RECURSION among:");
      for (v = 0; v < n_fn; v++) if (COMP[v] == c) printf(" %s", FN[v].name);
      printf("\n  no stack bound without a depth cap (-d)\n");
      return 0;
    }
    own = (CAP + 1) * COMP_SUM[c];
  } else {
    own = COMP_SUM[c];
  }
  COMP_DEEP[c] = own;
  COMP_NEXT[c] = -1;
  for (v = 0; v < n_fn; v++) {
    if (COMP[v] != c) continue;
    for (k = 0; k < N_CALLEE[v]; k++) {
      int d = COMP[CALLEE[v][k]];
      if (d == c) continue;
      if (!comp_deep(d)) return 0;
      if (own + COMP_DEEP[d] > COMP_DEEP[c]) {
        COMP_DEEP[c] = own + COMP_DEEP[d];
        COMP_NEXT[c] = d;
      }
    }
  }
  COMP_DONE[c] = 1;
  return 1;
}

static void print_comp(int c) {
  int v, first = 1;
  for (v = 0; v < n_fn; v++) {
    if (COMP[v] != c) continue;
    printf("%s%s(%ld)", first ? "" : "+", FN[v].name, FN[v].frame);
    first = 0;
  }
  if (COMP_SIZE[c] > 1 || COMP_SELF[c]) printf(" x%ld levels", CAP + 1);
}

int main(int argc, char **argv) {
  long bound;
  int i, a = 2, k, worst = -1, c;
  if (argc < 3) { printf("usage: stack_depth BOUND [-d CAP] file.s...\n"); return 2; }
  bound = strtol(argv[1], NULL, 10);
  if (argc > 4 && strcmp(argv[2], "-d") == 0) { CAP = strtol(argv[3], NULL, 10); a = 4; }
  for (; a < argc; a++) if (!parse(argv[a])) return 1;
  if (n_fn == 0) { printf("no functions found\n"); return 1; }
  for (i = 0; i < n_fn; i++) {
    for (k = 0; k < FN[i].n_calls; k++) {
      int j = find(FN[i].calls[k]);
      if (j >= 0) CALLEE[i][N_CALLEE[i]++] = j;   /* others are library leaves */
    }
  }
  for (i = 0; i < n_fn; i++) if (IDX[i] == 0) strong(i);
  for (i = 0; i < n_fn; i++) {
    for (k = 0; k < N_CALLEE[i]; k++) if (CALLEE[i][k] == i) COMP_SELF[COMP[i]] = 1;
  }
  for (c = 0; c < n_comp; c++) {
    if (!comp_deep(c)) return 1;
    if (worst < 0 || COMP_DEEP[c] > COMP_DEEP[worst]) worst = c;
  }
  printf("worst-case stack %ld bytes (bound %ld): ", COMP_DEEP[worst], bound);
  for (c = worst; c >= 0; c = COMP_NEXT[c]) {
    if (c != worst) printf(" -> ");
    print_comp(c);
  }
  printf("\n");
  return COMP_DEEP[worst] <= bound ? 0 : 1;
}
