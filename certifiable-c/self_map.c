/*
 * self_map.c -- the child reading its own source, and writing down what it is made of.
 *
 *   self_map file.c file.h ...        > child/self_map.txt
 *
 * For every file it is given it writes: how long it is, every function defined in
 * it with the line it starts on, and every point of itself it is allowed to change
 * (a #define carrying a "self:" note, see smarsh_self.h). Then, for each function,
 * which of the other functions it names: that is the shape of the thing it is.
 *
 * It parses only what it needs to: a function definition is a line that starts in
 * the first column, is not a directive and not a type declaration, and ends in "{"
 * or is followed by a line that is "{". This is the child's own view of itself, so
 * it is deliberately simple and says plainly what it could not read.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 20000u
#define MAX_LINE 512u
#define MAX_FUNCS 4000u
#define NAME_LEN 64u

static char LINES[MAX_LINES][MAX_LINE];
static unsigned N_LINES;
static char FUNC[MAX_FUNCS][NAME_LEN];
static unsigned FUNC_LINE[MAX_FUNCS];
static unsigned FUNC_FILE[MAX_FUNCS];
static unsigned FUNC_START[MAX_FUNCS];   /* where its body begins, among all lines read */
static unsigned N_FUNCS;
static char FILES[64][256];
static unsigned N_FILES;

static int ident_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* the name just before the first '(' of a function definition, or 0 */
static int function_name(const char *line, const char *next, char *out) {
  const char *open, *p;
  unsigned n = 0u;
  size_t len = strlen(line);
  if (len == 0u || line[0] == '#' || line[0] == ' ' || line[0] == '\t' || line[0] == '/' ||
      line[0] == '}' || line[0] == '*') {
    return 0;
  }
  if (strncmp(line, "typedef", 7u) == 0 || strstr(line, ";") != 0) return 0;
  open = strchr(line, '(');
  if (open == 0) return 0;
  if (line[len - 1u] != '{' && !(next != 0 && next[0] == '{')) return 0;
  p = open;
  while (p > line && !ident_char(p[-1])) p--;
  while (p > line && ident_char(p[-1])) p--;
  while (p < open && ident_char(*p) && n < NAME_LEN - 1u) out[n++] = *p++;
  out[n] = '\0';
  return n > 0u;
}

static void read_file(const char *path) {
  FILE *f = fopen(path, "r");
  unsigned first = N_LINES, i;
  if (f == 0) {
    printf("file %s could not be read\n", path);
    return;
  }
  while (N_LINES < MAX_LINES && fgets(LINES[N_LINES], (int)MAX_LINE, f) != 0) {
    size_t n = strlen(LINES[N_LINES]);
    while (n > 0u && (LINES[N_LINES][n - 1u] == '\n' || LINES[N_LINES][n - 1u] == '\r')) {
      LINES[N_LINES][--n] = '\0';
    }
    N_LINES++;
  }
  fclose(f);
  if (N_FILES < 64u) {
    strncpy(FILES[N_FILES], path, 255u);
    FILES[N_FILES][255] = '\0';
    N_FILES++;
  }
  printf("file %s lines %u\n", path, N_LINES - first);
  for (i = first; i < N_LINES; i++) {
    char name[NAME_LEN];
    const char *self;
    if (function_name(LINES[i], i + 1u < N_LINES ? LINES[i + 1u] : 0, name) && N_FUNCS < MAX_FUNCS) {
      strcpy(FUNC[N_FUNCS], name);
      FUNC_LINE[N_FUNCS] = i - first + 1u;
      FUNC_FILE[N_FUNCS] = N_FILES - 1u;
      FUNC_START[N_FUNCS] = i;
      N_FUNCS++;
    }
    self = strstr(LINES[i], "/* self:");
    if (self != 0 && strncmp(LINES[i], "#define", 7u) == 0) {
      char nm[NAME_LEN], val[NAME_LEN];
      if (sscanf(LINES[i] + 7, "%63s %63s", nm, val) == 2) {
        printf("open %s %s now %s may be %s", path, nm, val, self + 8);
        printf("\n");
      }
    }
  }
}

int main(int argc, char **argv) {
  int a;
  unsigned i, j, k;
  if (argc < 2) {
    printf("give it the files it is made of\n");
    return 2;
  }
  printf("what I am made of, as I read it\n\n");
  for (a = 1; a < argc; a++) read_file(argv[a]);
  printf("\nfunctions %u in %u files\n\n", N_FUNCS, N_FILES);
  for (i = 0u; i < N_FUNCS; i++) {
    /* its body: from where it begins to where the next one in the same file does */
    unsigned begin = FUNC_START[i], end = (i + 1u < N_FUNCS) ? FUNC_START[i + 1u] : N_LINES;
    printf("function %s in %s at line %u calls", FUNC[i], FILES[FUNC_FILE[i]], FUNC_LINE[i]);
    for (j = 0u; j < N_FUNCS; j++) {
      int found = 0;
      if (j == i) continue;
      for (k = begin; k < end && !found; k++) {
        const char *hit = LINES[k];
        size_t len = strlen(FUNC[j]);
        for (hit = strstr(hit, FUNC[j]); hit != 0; hit = strstr(hit + 1, FUNC[j])) {
          char before = hit == LINES[k] ? ' ' : hit[-1];
          const char *after = hit + len;
          while (*after == ' ') after++;
          if (!ident_char(before) && *after == '(') {
            found = 1;
            break;
          }
        }
      }
      if (found) printf(" %s", FUNC[j]);
    }
    printf("\n");
  }
  return 0;
}
