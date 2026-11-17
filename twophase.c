/*
 * twophase.c -- Herbert Kociemba's Two-Phase Algorithm
 *
 * References:
 *   https://kociemba.org/math/cubielevel.htm   (cubie-level model)
 *   https://kociemba.org/math/twophase.'htm     (two-phase algorithm)
 *
 * Structure
 *   1. Cubie level: a cube is (cp, co, ep, eo) for 8 corners / 12 edges.
 *      Moves are cubie cubes; applying a move = cube multiplication.
 *   2. Coordinate level:
 *        phase 1: twist (0..2186), flip (0..2047), UDslice (0..494)
 *                 -> (0,0,0) exactly when the cube is in G1 = <U,D,R2,L2,F2,B2>
 *        phase 2: corner perm (0..40319), U/D edge perm (0..40319),
 *                 slice-edge perm (0..23)
 *   3. Move tables for every coordinate (coord x move -> coord).
 *   4. Pruning tables (BFS distance) on coordinate pairs:
 *        phase 1: twist x slice, flip x slice
 *        phase 2: cornerperm x sliceperm, edgeperm x sliceperm
 *   5. IDA* in phase 1; every phase-1 solution is handed to an IDA* phase 2.
 *      Phase 1 keeps going with longer maneuvers to find shorter totals.
 *
 * This is a mock-up: no symmetry reduction, simple (but admissible) pruning
 * tables, tables are rebuilt at every start (~1-2 s). It does find solutions
 * of typically 20-23 moves within a second or two.
 *
 * Build:  cc -O2 -o twophase twophase.c
 * Usage:  ./twophase "R U R' U' F2 D L"     solve the given scramble
 *         ./twophase random [seed]          solve a random cube
 *         options (after the cube):  -t <target length>  -s <seconds>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* 1. Cubie level                                                      */
/* ------------------------------------------------------------------ */
enum { URF, UFL, ULB, UBR, DFR, DLF, DBL, DRB };         /* corners */
enum { UR, UF, UL, UB, DR, DF, DL, DB, FR, FL, BL, BR }; /* edges   */

typedef struct {
  unsigned char cp[8], co[8];   /* corner permutation / orientation (mod 3) */
  unsigned char ep[12], eo[12]; /* edge permutation / orientation (mod 2)   */
} Cube;

/* The six basic quarter turns, written in "replaced-by" notation:
 * after the move, position i holds the cubie that was at cp[i]. */
static const Cube basic[6] = {
    /* U */ {{UBR, URF, UFL, ULB, DFR, DLF, DBL, DRB},
             {0, 0, 0, 0, 0, 0, 0, 0},
             {UB, UR, UF, UL, DR, DF, DL, DB, FR, FL, BL, BR},
             {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    /* R */
    {{DFR, UFL, ULB, URF, DRB, DLF, DBL, UBR},
     {2, 0, 0, 1, 1, 0, 0, 2},
     {FR, UF, UL, UB, BR, DF, DL, DB, DR, FL, BL, UR},
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    /* F */
    {{UFL, DLF, ULB, UBR, URF, DFR, DBL, DRB},
     {1, 2, 0, 0, 2, 1, 0, 0},
     {UR, FL, UL, UB, DR, FR, DL, DB, UF, DF, BL, BR},
     {0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0}},
    /* D */
    {{URF, UFL, ULB, UBR, DLF, DBL, DRB, DFR},
     {0, 0, 0, 0, 0, 0, 0, 0},
     {UR, UF, UL, UB, DF, DL, DB, DR, FR, FL, BL, BR},
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    /* L */
    {{URF, ULB, DBL, UBR, DFR, UFL, DLF, DRB},
     {0, 1, 2, 0, 0, 2, 1, 0},
     {UR, UF, BL, UB, DR, DF, FL, DB, FR, UL, DL, BR},
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    /* B */
    {{URF, UFL, UBR, DRB, DFR, DLF, ULB, DBL},
     {0, 0, 1, 2, 0, 0, 2, 1},
     {UR, UF, UL, BR, DR, DF, DL, BL, FR, FL, UB, DB},
     {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1}},
};

static Cube moveCube[18]; /* U1 U2 U3 R1 R2 R3 F1 ... B3 */
static const char *moveName[18] = {"U", "U2", "U'", "R", "R2", "R'",
                                   "F", "F2", "F'", "D", "D2", "D'",
                                   "L", "L2", "L'", "B", "B2", "B'"};

static void cube_solved(Cube *c) {
  for (int i = 0; i < 8; i++) {
    c->cp[i] = i;
    c->co[i] = 0;
  }
  for (int i = 0; i < 12; i++) {
    c->ep[i] = i;
    c->eo[i] = 0;
  }
}

/* r = a * b  (first a, then b) -- the cubie-level group operation */
static void cube_mul(const Cube *a, const Cube *b, Cube *r) {
  Cube t;
  for (int i = 0; i < 8; i++) {
    t.cp[i] = a->cp[b->cp[i]];
    t.co[i] = (a->co[b->cp[i]] + b->co[i]) % 3;
  }
  for (int i = 0; i < 12; i++) {
    t.ep[i] = a->ep[b->ep[i]];
    t.eo[i] = (a->eo[b->ep[i]] + b->eo[i]) & 1;
  }
  *r = t;
}

static void cube_move(Cube *c, int m) { cube_mul(c, &moveCube[m], c); }

static void init_moves(void) {
  for (int ax = 0; ax < 6; ax++) {
    Cube c;
    cube_solved(&c);
    for (int p = 0; p < 3; p++) {
      cube_mul(&c, &basic[ax], &c);
      moveCube[ax * 3 + p] = c;
    }
  }
}

/* ------------------------------------------------------------------ */
/* 2. Coordinates                                                      */
/* ------------------------------------------------------------------ */
#define N_TWIST 2187
#define N_FLIP 2048
#define N_SLICE 495
#define N_PERM8 40320
#define N_SPERM 24
#define N_MOVE 18

static int Cnk(int n, int k) {
  if (k < 0 || k > n)
    return 0;
  int r = 1;
  for (int i = 0; i < k; i++)
    r = r * (n - i) / (i + 1);
  return r;
}

/* Lehmer code of a permutation of 0..n-1 (identity -> 0) */
static int perm_get(const unsigned char *a, int n) {
  int idx = 0;
  for (int i = 0; i < n; i++) {
    int c = 0;
    for (int j = i + 1; j < n; j++)
      if (a[j] < a[i])
        c++;
    idx = idx * (n - i) + c;
  }
  return idx;
}
static void perm_set(unsigned char *a, int n, int idx) {
  int d[12], avail[12];
  for (int i = n - 1; i >= 0; i--) {
    d[i] = idx % (n - i);
    idx /= (n - i);
  }
  for (int i = 0; i < n; i++)
    avail[i] = i;
  for (int i = 0, m = n; i < n; i++, m--) {
    a[i] = avail[d[i]];
    for (int j = d[i]; j < m - 1; j++)
      avail[j] = avail[j + 1];
  }
}

static int get_twist(const Cube *c) {
  int t = 0;
  for (int i = 0; i < 7; i++)
    t = t * 3 + c->co[i];
  return t;
}
static void set_twist(Cube *c, int t) {
  int s = 0;
  for (int i = 6; i >= 0; i--) {
    c->co[i] = t % 3;
    s += c->co[i];
    t /= 3;
  }
  c->co[7] = (3 - s % 3) % 3;
}
static int get_flip(const Cube *c) {
  int f = 0;
  for (int i = 0; i < 11; i++)
    f = f * 2 + c->eo[i];
  return f;
}
static void set_flip(Cube *c, int f) {
  int s = 0;
  for (int i = 10; i >= 0; i--) {
    c->eo[i] = f & 1;
    s += f & 1;
    f >>= 1;
  }
  c->eo[11] = s & 1;
}
/* UDslice: which 4 of the 12 positions hold FR,FL,BL,BR (order ignored) */
static int get_slice(const Cube *c) {
  int a = 0, x = 0;
  for (int j = 11; j >= 0; j--)
    if (c->ep[j] >= FR) {
      a += Cnk(11 - j, x + 1);
      x++;
    }
  return a;
}
static void set_slice(Cube *c, int a) {
  int x = 4, s = FR, o = UR;
  for (int j = 0; j < 12; j++) {
    if (x > 0 && a - Cnk(11 - j, x) >= 0) {
      c->ep[j] = s++;
      a -= Cnk(11 - j, x);
      x--;
    } else
      c->ep[j] = o++;
  }
}
static int get_cperm(const Cube *c) { return perm_get(c->cp, 8); }
static void set_cperm(Cube *c, int i) { perm_set(c->cp, 8, i); }
static int get_eperm(const Cube *c) {
  return perm_get(c->ep, 8);
} /* valid in G1 */
static void set_eperm(Cube *c, int i) {
  perm_set(c->ep, 8, i);
  for (int k = 8; k < 12; k++)
    c->ep[k] = k;
}
static int get_sperm(const Cube *c) { /* valid in G1 */
  unsigned char a[4];
  for (int i = 0; i < 4; i++)
    a[i] = c->ep[8 + i] - 8;
  return perm_get(a, 4);
}
static void set_sperm(Cube *c, int i) {
  unsigned char a[4];
  perm_set(a, 4, i);
  for (int k = 0; k < 8; k++)
    c->ep[k] = k;
  for (int k = 0; k < 4; k++)
    c->ep[8 + k] = 8 + a[k];
}

/* ------------------------------------------------------------------ */
/* 3. Move tables                                                      */
/* ------------------------------------------------------------------ */
static const int p2moves[10] = {0, 1,  2,  4,  7,
                                9, 10, 11, 13, 16}; /* U* R2 F2 D* L2 B2 */
static int isP2move[18];

static unsigned short twistMove[N_TWIST][N_MOVE];
static unsigned short flipMove[N_FLIP][N_MOVE];
static unsigned short sliceMove[N_SLICE][N_MOVE];
static unsigned short cpermMove[N_PERM8][N_MOVE];
static unsigned short epermMove[N_PERM8][N_MOVE]; /* only phase-2 moves valid */
static unsigned char spermMove[N_SPERM][N_MOVE];  /* only phase-2 moves valid */

static void init_move_tables(void) {
  Cube c;
  for (int i = 0; i < 10; i++)
    isP2move[p2moves[i]] = 1;
  for (int i = 0; i < N_TWIST; i++)
    for (int m = 0; m < N_MOVE; m++) {
      cube_solved(&c);
      set_twist(&c, i);
      cube_move(&c, m);
      twistMove[i][m] = get_twist(&c);
    }
  for (int i = 0; i < N_FLIP; i++)
    for (int m = 0; m < N_MOVE; m++) {
      cube_solved(&c);
      set_flip(&c, i);
      cube_move(&c, m);
      flipMove[i][m] = get_flip(&c);
    }
  for (int i = 0; i < N_SLICE; i++)
    for (int m = 0; m < N_MOVE; m++) {
      cube_solved(&c);
      set_slice(&c, i);
      cube_move(&c, m);
      sliceMove[i][m] = get_slice(&c);
    }
  for (int i = 0; i < N_PERM8; i++)
    for (int m = 0; m < N_MOVE; m++) {
      cube_solved(&c);
      set_cperm(&c, i);
      cube_move(&c, m);
      cpermMove[i][m] = get_cperm(&c);
    }
  for (int i = 0; i < N_PERM8; i++)
    for (int k = 0; k < 10; k++) {
      int m = p2moves[k];
      cube_solved(&c);
      set_eperm(&c, i);
      cube_move(&c, m);
      epermMove[i][m] = get_eperm(&c);
    }
  for (int i = 0; i < N_SPERM; i++)
    for (int k = 0; k < 10; k++) {
      int m = p2moves[k];
      cube_solved(&c);
      set_sperm(&c, i);
      cube_move(&c, m);
      spermMove[i][m] = get_sperm(&c);
    }
}

/* ------------------------------------------------------------------ */
/* 4. Pruning tables: BFS distance from the goal in a pair of coords   */
/* ------------------------------------------------------------------ */
static unsigned char *pr_twist_slice, *pr_flip_slice;  /* phase 1 */
static unsigned char *pr_cperm_sperm, *pr_eperm_sperm; /* phase 2 */

typedef unsigned short (*Tab)[N_MOVE];

static unsigned char *build_prune(int nA, Tab mA, int nB, const void *mBv,
                                  int bIsByte, const int *moves, int nMoves) {
  int n = nA * nB, filled = 1, depth = 0;
  unsigned char *t = malloc(n);
  memset(t, 0xFF, n);
  t[0] = 0;
  while (filled < n) {
    int found = 0;
    for (int i = 0; i < n; i++) {
      if (t[i] != depth)
        continue;
      int a = i / nB, b = i % nB;
      for (int k = 0; k < nMoves; k++) {
        int m = moves[k];
        int na = mA[a][m];
        int nb = bIsByte ? ((const unsigned char (*)[N_MOVE])mBv)[b][m]
                         : ((const unsigned short (*)[N_MOVE])mBv)[b][m];
        int j = na * nB + nb;
        if (t[j] == 0xFF) {
          t[j] = depth + 1;
          found++;
        }
      }
    }
    if (!found)
      break;
    filled += found;
    depth++;
  }
  return t;
}

static void init_prune_tables(void) {
  int all[18];
  for (int i = 0; i < 18; i++)
    all[i] = i;
  pr_twist_slice =
      build_prune(N_TWIST, twistMove, N_SLICE, sliceMove, 0, all, 18);
  pr_flip_slice = build_prune(N_FLIP, flipMove, N_SLICE, sliceMove, 0, all, 18);
  pr_cperm_sperm =
      build_prune(N_PERM8, cpermMove, N_SPERM, spermMove, 1, p2moves, 10);
  pr_eperm_sperm =
      build_prune(N_PERM8, epermMove, N_SPERM, spermMove, 1, p2moves, 10);
}

/* ------------------------------------------------------------------ */
/* 5. Search                                                           */
/* ------------------------------------------------------------------ */
static Cube start;
static int sol[40]; /* current maneuver (phase1 + phase2) */
static int best[40], bestLen = 99, bestSplit;
static int targetLen = 20;
static clock_t deadline;
static long nodes1, nodes2;
static int done;

static int max2(int a, int b) { return a > b ? a : b; }

/* forbid same axis twice and the order "opposite face then face" (D U ~ U D) */
static int allowed(int last, int m) {
  if (last < 0)
    return 1;
  int la = last / 3, a = m / 3;
  return la != a && !(la == a + 3);
}

static int phase2(int cp, int ep, int sp, int depth, int n, int last) {
  nodes2++;
  if (cp == 0 && ep == 0 && sp == 0)
    return 1;
  if (depth == 0)
    return 0;
  if (max2(pr_cperm_sperm[cp * 24 + sp], pr_eperm_sperm[ep * 24 + sp]) > depth)
    return 0;
  for (int k = 0; k < 10; k++) {
    int m = p2moves[k];
    if (!allowed(last, m))
      continue;
    sol[n] = m;
    if (phase2(cpermMove[cp][m], epermMove[ep][m], spermMove[sp][m], depth - 1,
               n + 1, m))
      return 1;
  }
  return 0;
}

static void start_phase2(int n1) {
  /* compute phase-2 coordinates from the cubie cube after phase 1 */
  Cube c = start;
  for (int i = 0; i < n1; i++)
    cube_move(&c, sol[i]);
  int cp = get_cperm(&c), ep = get_eperm(&c), sp = get_sperm(&c);
  int h = max2(pr_cperm_sperm[cp * 24 + sp], pr_eperm_sperm[ep * 24 + sp]);
  int maxd = bestLen - 1 - n1; /* only accept strictly shorter totals */
  if (maxd > 18)
    maxd = 18;
  for (int d = h; d <= maxd; d++) {
    if (phase2(cp, ep, sp, d, n1, n1 ? sol[n1 - 1] : -1)) {
      bestLen = n1 + d;
      bestSplit = n1;
      memcpy(best, sol, sizeof(int) * bestLen);
      printf("  found %2d moves (phase 1: %2d, phase 2: %2d)  %.2fs\n", bestLen,
             n1, d, (double)clock() / CLOCKS_PER_SEC);
      fflush(stdout);
      if (bestLen <= targetLen)
        done = 1;
      return;
    }
  }
}

static void phase1(int tw, int fl, int sl, int depth, int n, int last) {
  if (done)
    return;
  if ((++nodes1 & 0xFFFF) == 0 && clock() > deadline) {
    done = 1;
    return;
  }
  if (depth == 0) {
    /* reached G1. If the last move is itself a G1 move, the shorter
     * maneuver already reached G1 -- skip this duplicate. */
    if (tw == 0 && fl == 0 && sl == 0 && (n == 0 || !isP2move[last]))
      start_phase2(n);
    return;
  }
  for (int m = 0; m < N_MOVE; m++) {
    if (!allowed(last, m))
      continue;
    int t = twistMove[tw][m], f = flipMove[fl][m], s = sliceMove[sl][m];
    int h =
        max2(pr_twist_slice[t * N_SLICE + s], pr_flip_slice[f * N_SLICE + s]);
    if (h > depth - 1)
      continue;
    sol[n] = m;
    phase1(t, f, s, depth - 1, n + 1, m);
    if (done)
      return;
  }
}

static void solve(double seconds) {
  deadline = clock() + (clock_t)(seconds * CLOCKS_PER_SEC);
  int tw = get_twist(&start), fl = get_flip(&start), sl = get_slice(&start);
  int h =
      max2(pr_twist_slice[tw * N_SLICE + sl], pr_flip_slice[fl * N_SLICE + sl]);
  for (int d = h; d < bestLen && !done; d++)
    phase1(tw, fl, sl, d, 0, -1);
}

/* ------------------------------------------------------------------ */
/* Helpers: scramble parsing, random cube, verification                */
/* ------------------------------------------------------------------ */
static int parse_scramble(const char *s, Cube *c) {
  const char *faces = "URFDLB";
  cube_solved(c);
  while (*s) {
    while (*s == ' ')
      s++;
    if (!*s)
      break;
    const char *p = strchr(faces, *s);
    if (!p) {
      fprintf(stderr, "bad move near '%s'\n", s);
      return 0;
    }
    int ax = p - faces, pw = 0;
    s++;
    if (*s == '2') {
      pw = 1;
      s++;
    } else if (*s == '\'') {
      pw = 2;
      s++;
    }
    cube_move(c, ax * 3 + pw);
  }
  return 1;
}

static int parity(const unsigned char *a, int n) {
  int p = 0;
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (a[j] < a[i])
        p ^= 1;
  return p;
}

static void random_cube(Cube *c) {
  cube_solved(c);
  do {
    for (int i = 7; i > 0; i--) {
      int j = rand() % (i + 1);
      unsigned char t = c->cp[i];
      c->cp[i] = c->cp[j];
      c->cp[j] = t;
    }
    for (int i = 11; i > 0; i--) {
      int j = rand() % (i + 1);
      unsigned char t = c->ep[i];
      c->ep[i] = c->ep[j];
      c->ep[j] = t;
    }
  } while (parity(c->cp, 8) != parity(c->ep, 12));
  set_twist(c, rand() % N_TWIST);
  set_flip(c, rand() % N_FLIP);
}

static int is_solved(const Cube *c) {
  Cube s;
  cube_solved(&s);
  return memcmp(c, &s, sizeof s) == 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(
        stderr,
        "usage: %s \"<scramble>\" | random [seed]  [-t target] [-s seconds]\n",
        argv[0]);
    return 1;
  }
  double seconds = 10.0;
  int argi = 2;
  init_moves();

  if (strcmp(argv[1], "random") == 0) {
    unsigned seed = (unsigned)time(NULL);
    if (argc > 2 && argv[2][0] != '-') {
      seed = atoi(argv[2]);
      argi = 3;
    }
    srand(seed);
    random_cube(&start);
    printf("random cube, seed %u\n", seed);
  } else if (!parse_scramble(argv[1], &start))
    return 1;

  for (; argi < argc; argi++) {
    if (!strcmp(argv[argi], "-t") && argi + 1 < argc)
      targetLen = atoi(argv[++argi]);
    else if (!strcmp(argv[argi], "-s") && argi + 1 < argc)
      seconds = atof(argv[++argi]);
  }

  clock_t t0 = clock();
  init_move_tables();
  init_prune_tables();
  printf("tables built in %.2fs\n", (double)(clock() - t0) / CLOCKS_PER_SEC);
  printf("start coords: twist=%d flip=%d slice=%d\n", get_twist(&start),
         get_flip(&start), get_slice(&start));

  if (is_solved(&start)) {
    printf("already solved\n");
    return 0;
  }
  solve(seconds);
  if (bestLen == 99) {
    printf("no solution found in time\n");
    return 1;
  }

  printf("\nsolution (%d moves): ", bestLen);
  for (int i = 0; i < bestLen; i++)
    printf("%s%s ", i == bestSplit ? ". " : "", moveName[best[i]]);
  printf("\n");

  Cube c = start;
  for (int i = 0; i < bestLen; i++)
    cube_move(&c, best[i]);
  printf("verify: %s\n", is_solved(&c) ? "SOLVED" : "FAILED");
  printf("nodes: phase1 %ld, phase2 %ld\n", nodes1, nodes2);
  return 0;
}
