/* Host test harness for StepMath CE.
 * It includes the REAL main.c (so it can call the step generator directly),
 * compiled against stand-in graphx / keypadc / fileioc headers with a fake
 * framebuffer. The reference answers below are computed independently of
 * main.c's solving code (only its parser is reused, to read the strings).
 *
 * Usage:  harness types [N seed] [verbose]   step generator vs reference, all 5 types
 *         harness parse [N seed]             parser and polynomial reading vs a generator
 *         harness garbage [N seed]           random strings: never crash, always an answer or a refusal
 *         harness fuzz [frames seed]         the whole UI with random key presses
 *         harness shots DIR                  screenshots
 */
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#define main game_main
#include "../STEPMATH/src/main.c"
#undef main
int game_main(void);
#include "stub/stub_internal.h"

long frames;
static unsigned fclock;
clock_t fake_clock(void) { fclock += 137; return (clock_t)fclock; }

static long trunc_n;
void test_trunc(void) { trunc_n++; }
static int cur_present_screen = -1;
static long present_count[16];
static void on_present_hook(int screen);
void test_present(int screen) { cur_present_screen = screen; if (screen >= 0 && screen < 16) present_count[screen]++; on_present_hook(screen); }

/* ---------------------------------------------------------------- */
/* random numbers                                                   */
/* ---------------------------------------------------------------- */
static unsigned rs_ = 1;
static unsigned rr(void) { rs_ ^= rs_ << 13; rs_ ^= rs_ >> 17; rs_ ^= rs_ << 5; return rs_; }
static long rnd_range(long lo, long hi) { return lo + (long)(rr() % (unsigned)(hi - lo + 1)); }

static long fails, checks;
static int verbose;
static FILE *logf_;
#define FAIL(...) do { fails++; if (fails < 40) { fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)
#define CHECK(cond, ...) do { checks++; if (!(cond)) FAIL(__VA_ARGS__); } while (0)

static void clean_nb(const char *s, char *out)
{
    while (*s) { *out++ = *s == NB ? ' ' : *s; s++; }
    *out = 0;
}

/* ---------------------------------------------------------------- */
/* independent evaluators                                           */
/* ---------------------------------------------------------------- */
/* the game's syntax tree, evaluated exactly with whole numbers (x = xv) as a fraction */
typedef struct { long long n, d; int ok; } rq;
static long long gcdll(long long a, long long b) { if (a < 0) a = -a; if (b < 0) b = -b; while (b) { long long t = a % b; a = b; b = t; } return a; }
static rq rq_make(long long n, long long d)
{
    rq r;
    r.ok = d != 0;
    if (d < 0) { n = -n; d = -d; }
    if (r.ok) { long long g = gcdll(n, d); if (g > 1) { n /= g; d /= g; } }
    r.n = n; r.d = d ? d : 1;
    return r;
}
static rq ev_q(int n, long long xv)
{
    rq a, b, r;
    const node_t *q = &nd[n];
    switch (q->t) {
    case N_NUM: return rq_make(q->v, 1);
    case N_X: return rq_make(xv, 1);
    case N_NEG: a = ev_q(q->l, xv); a.n = -a.n; return a;
    case N_ADD: case N_SUB: case N_MUL: case N_DIV:
        a = ev_q(q->l, xv); b = ev_q(q->r, xv);
        if (!a.ok || !b.ok) { a.ok = 0; return a; }
        if (q->t == N_ADD) return rq_make(a.n * b.d + b.n * a.d, a.d * b.d);
        if (q->t == N_SUB) return rq_make(a.n * b.d - b.n * a.d, a.d * b.d);
        if (q->t == N_MUL) return rq_make(a.n * b.n, a.d * b.d);
        if (b.n == 0) { a.ok = 0; return a; }
        return rq_make(a.n * b.d, a.d * b.n);
    case N_POW: {
        long e = nd[q->r].v, i;
        a = ev_q(q->l, xv);
        r = rq_make(1, 1);
        for (i = 0; i < e; i++) r = rq_make(r.n * a.n, r.d * a.d);
        return r;
    }
    default: break;
    }
    a.ok = 0; a.n = 0; a.d = 1;
    return a;
}
static double ev_d(int n, double x)
{
    const node_t *q = &nd[n];
    switch (q->t) {
    case N_NUM: return (double)q->v;
    case N_X: return x;
    case N_NEG: return -ev_d(q->l, x);
    case N_ADD: return ev_d(q->l, x) + ev_d(q->r, x);
    case N_SUB: return ev_d(q->l, x) - ev_d(q->r, x);
    case N_MUL: return ev_d(q->l, x) * ev_d(q->r, x);
    case N_DIV: return ev_d(q->l, x) / ev_d(q->r, x);
    case N_POW: return pow(ev_d(q->l, x), (double)nd[q->r].v);
    default: return 0;
    }
}

/* a tiny separate evaluator for answer strings with sqrt( ) and ^ */
static const char *qp;
static double q_expr(void);
static void q_sp(void) { while (*qp == ' ') qp++; }
static double q_atom(void)
{
    double v = 0;
    q_sp();
    if (*qp == '(') { qp++; v = q_expr(); q_sp(); if (*qp == ')') qp++; }
    else if (strncmp(qp, "sqrt(", 5) == 0) { qp += 5; v = sqrt(q_expr()); q_sp(); if (*qp == ')') qp++; }
    else if (isdigit((unsigned char)*qp)) { char *e; v = strtod(qp, &e); qp = e; }
    q_sp();
    if (*qp == '^') { qp++; q_sp(); { char *e; double p = strtod(qp, &e); qp = e; v = pow(v, p); } }
    return v;
}
static double q_unary(void)
{
    q_sp();
    if (*qp == '-') { qp++; return -q_unary(); }
    return q_atom();
}
static double q_term(void)
{
    double v = q_unary();
    for (;;) {
        q_sp();
        if (*qp == '*') { qp++; v *= q_unary(); }
        else if (*qp == '/') { qp++; v /= q_unary(); }
        else if (*qp == '(' || strncmp(qp, "sqrt(", 5) == 0) v *= q_atom();
        else break;
    }
    return v;
}
static double q_expr(void)
{
    double v = q_term();
    for (;;) {
        q_sp();
        if (*qp == '+') { qp++; v += q_term(); }
        else if (*qp == '-') { qp++; v -= q_term(); }
        else break;
    }
    return v;
}
static double q_eval(const char *s, int *consumed_all)
{
    double v;
    qp = s;
    v = q_expr();
    q_sp();
    *consumed_all = (*qp == 0);
    return v;
}

/* ---------------------------------------------------------------- */
/* generators that know the answer                                  */
/* ---------------------------------------------------------------- */
#define GK 6
typedef struct { char s[80]; long p[GK]; int n; } gen_t;     /* text and polynomial of a side */

static void g_sign(gen_t *g, long c)
{
    if (g->n == 0) { if (c < 0) strcat(g->s, "-"); }
    else strcat(g->s, c < 0 ? "-" : "+");
}
static void g_mag(char *dst, long c, int k)
{
    char t[40];
    long a = c < 0 ? -c : c;
    if (k == 0) { sprintf(t, "%ld", a); strcat(dst, t); return; }
    if (a != 1) { sprintf(t, "%ld", a); strcat(dst, t); }
    strcat(dst, "x");
    if (k > 1) { sprintf(t, "^%d", k); strcat(dst, t); }
}
static void g_mono(gen_t *g, long c, int k)
{
    g_sign(g, c);
    if (c == 0) { strcat(g->s, k ? "0x" : "0"); if (k > 1) { char t[16]; sprintf(t, "^%d", k); strcat(g->s, t); } }
    else g_mag(g->s, c, k);
    g->p[k] += c;
    g->n++;
}
/* m x^mk ( p x^pk + q ) : style 0 = plain, 1 = implicit */
static void g_bracket(gen_t *g, long m, int mk, long p, int pk, long q)
{
    char t[40];
    g_sign(g, m);
    if (m == 0) strcat(g->s, "0");
    else if (mk == 0) { if (m != 1 && m != -1) { sprintf(t, "%ld", m < 0 ? -m : m); strcat(g->s, t); } }
    else g_mag(g->s, m, mk);
    strcat(g->s, "(");
    if (p < 0) strcat(g->s, "-");
    if (p != 0) {
        long a = p < 0 ? -p : p;
        if (pk == 0 || a != 1) { sprintf(t, "%ld", a); strcat(g->s, t); }
        if (pk > 0) { strcat(g->s, "x"); if (pk > 1) { sprintf(t, "^%d", pk); strcat(g->s, t); } }
    } else {
        strcat(g->s, "0");
    }
    strcat(g->s, q < 0 ? "-" : "+");
    sprintf(t, "%ld", q < 0 ? -q : q);
    strcat(g->s, t);
    strcat(g->s, ")");
    g->p[mk + pk] += m * p;
    g->p[mk] += m * q;
    g->n++;
}

/* a random side with up to `maxdeg` powers; brackets with probability */
static void gen_side(gen_t *g, int maxdeg, int brackets, long rng)
{
    int pieces = (int)rnd_range(1, 3), i;
    memset(g, 0, sizeof *g);
    for (i = 0; i < pieces; i++) {
        long c = rnd_range(-rng, rng);
        int r = (int)(rr() % 10);
        if (brackets && r < 3 && maxdeg >= 1) {
            int mk = (rr() % 4 == 0 && maxdeg >= 2) ? 1 : 0;
            g_bracket(g, rnd_range(-rng, rng), mk, rnd_range(-rng, rng), (int)(rr() % 2), rnd_range(-rng, rng));
            if (mk == 1 && g->p[2] != 0 && maxdeg < 2) { /* fine */ }
        } else {
            int k = (int)(rr() % (unsigned)(maxdeg + 1));
            if (rr() % 3 == 0) k = k > 0 ? 1 : 0;
            g_mono(g, c, k);
        }
    }
}

static int poly_degv(const long *p, int n) { int k; for (k = n - 1; k >= 0; k--) if (p[k]) return k; return -1; }

/* reference printing of a polynomial */
static void ref_print_poly(const long *p, int n, char *out)
{
    int k, first = 1;
    out[0] = 0;
    for (k = n - 1; k >= 0; k--) {
        char t[32];
        long c = p[k], a = c < 0 ? -c : c;
        if (!c) continue;
        if (first) { if (c < 0) strcat(out, "-"); }
        else strcat(out, c < 0 ? " - " : " + ");
        if (k == 0) sprintf(t, "%ld", a);
        else if (k == 1) { if (a == 1) strcpy(t, "x"); else sprintf(t, "%ldx", a); }
        else { if (a == 1) sprintf(t, "x^%d", k); else sprintf(t, "%ldx^%d", a, k); }
        strcat(out, t);
        first = 0;
    }
    if (first) strcpy(out, "0");
}

/* ---------------------------------------------------------------- */
/* checks shared by every solved problem                            */
/* ---------------------------------------------------------------- */
static int max_text_lines, max_state_lines, too_long_lines;

static void check_layout(const char *in)
{
    int i;
    CHECK(nsteps >= 1, "%s: no steps", in);
    CHECK(!st_over, "%s: too many steps", in);
    for (i = 0; i < nsteps; i++) {
        int lt = wrap_draw(steps[i].text, 0, 0, COLS, 100, 0, false);
        int ls = wrap_draw(steps[i].state, 0, 0, COLS, 100, 0, false);
        if (lt > max_text_lines) max_text_lines = lt;
        if (ls > max_state_lines) max_state_lines = ls;
        if (lt > 7 || ls > 5) { too_long_lines++; FAIL("%s: step %d needs %d text lines / %d state lines", in, i + 1, lt, ls); }
        CHECK(steps[i].text[0] != 0, "%s: step %d has no text", in, i + 1);
        CHECK(steps[i].state[0] != 0, "%s: step %d has no state", in, i + 1);
        CHECK((steps[i].fin != 0) == (i == nsteps - 1), "%s: final flag wrong on step %d", in, i + 1);
    }
}

static void print_steps(const char *in, const char *label)
{
    int i;
    if (!logf_) return;
    fprintf(logf_, "=== %s   [%s]\n", in, label);
    for (i = 0; i < nsteps; i++) {
        char t[300], s[300];
        clean_nb(steps[i].text, t);
        clean_nb(steps[i].state, s);
        fprintf(logf_, "  %s %d: %s\n         -> %s\n", steps[i].fin ? "FINAL" : "step ", i + 1, t, s);
    }
}

/* ---------------------------------------------------------------- */
/* equations: linear and quadratic                                  */
/* ---------------------------------------------------------------- */
static long counts[8];

/* sample the original equation as a polynomial: P(x) = left - right at x = 0..3 */
static int sample_equation(const char *in, long long *c0, long long *c1, long long *c2, long long *p3)
{
    int root = parse(in), l, r, x;
    long long P[4];
    if (root < 0 || nd[root].t != N_EQ) return 0;
    l = nd[root].l;
    r = nd[root].r;
    for (x = 0; x < 4; x++) {
        rq a = ev_q(l, x), b = ev_q(r, x);
        if (!a.ok || !b.ok || a.d != 1 || b.d != 1) return 0;
        P[x] = a.n - b.n;
    }
    *c0 = P[0];
    *c2 = (P[2] - 2 * P[1] + P[0]) / 2;
    *c1 = P[1] - P[0] - *c2;
    *p3 = P[3];
    return 1;
}

/* the parser refuses typed numbers of 5+ digits, but states may legitimately show them (e.g. 319260 = 0) */
static int has_bignum(const char *s)
{
    int run = 0;
    for (; *s; s++) { if (*s >= '0' && *s <= '9') { if (++run >= 5) return 1; } else run = 0; }
    return 0;
}

/* all states marked EQ_EQN must hold at the given roots */
static void check_eqn_states(const char *in, const double *roots, int nroots)
{
    int i, k;
    for (i = 0; i < nsteps; i++) {
        char buf[300];
        int root;
        if (steps[i].eqv != EQ_EQN) continue;
        clean_nb(steps[i].state, buf);
        if (has_bignum(buf)) continue;
        root = parse(buf);
        CHECK(root >= 0 && nd[root].t == N_EQ, "%s: step %d state '%s' is not an equation", in, i + 1, buf);
        if (root < 0 || nd[root].t != N_EQ) continue;
        for (k = 0; k < nroots; k++) {
            double l = ev_d(nd[root].l, roots[k]), r = ev_d(nd[root].r, roots[k]);
            double tol = 1e-6 * (1 + fabs(l) + fabs(r));
            CHECK(fabs(l - r) <= tol, "%s: step %d '%s' is false at x = %.9g (%.9g vs %.9g)", in, i + 1, buf, roots[k], l, r);
        }
    }
}

/* for "no solution": no sample x satisfies any equation state; for "every x": all do */
static void check_eqn_states_sets(const char *in, int all)
{
    int i, k;
    for (i = 0; i < nsteps; i++) {
        char buf[300];
        int root;
        if (steps[i].eqv != EQ_EQN) continue;
        clean_nb(steps[i].state, buf);
        if (has_bignum(buf)) continue;
        root = parse(buf);
        if (root < 0 || nd[root].t != N_EQ) { FAIL("%s: state '%s' not an equation", in, buf); continue; }
        for (k = -4; k <= 4; k++) {
            rq l = ev_q(nd[root].l, k), r = ev_q(nd[root].r, k);
            int same = l.ok && r.ok && l.n == r.n && l.d == r.d;
            CHECK(same == (all != 0), "%s: step %d '%s' at x=%d should be %s", in, i + 1, buf, k, all ? "true" : "false");
        }
    }
}

static void test_equation(const char *in)
{
    long long c0, c1, c2, c3;
    long long A, B, C;
    int deg;
    cls_t cl;
    uint8_t want;
    long trunc0 = trunc_n;
    if (!sample_equation(in, &c0, &c1, &c2, &c3)) { FAIL("%s: cannot sample", in); return; }
    A = c2; B = c1; C = c0;
    /* degrees: P(3) tells whether anything beyond x^2 hides in it */
    CHECK(c3 == A * 9 + B * 3 + C, "%s: generator made a cubic", in);
    deg = A ? 2 : B ? 1 : 0;
    want = deg == 2 ? PT_QUAD : PT_LINEAR;
    CHECK(classify(in, &cl) && cl.type == want && !cl.ambiguous, "%s: classified as %d (%s), wanted %d", in, cl.type, cl.why ? cl.why : "", want);
    if (!(classify(in, &cl) && cl.type == want)) return;
    CHECK(generate(in, want), "%s: generate failed: %s", in, g_fail ? g_fail : "?");
    if (nsteps == 0) return;
    check_layout(in);
    CHECK(trunc_n == trunc0, "%s: a string was truncated", in);
    if (want == PT_LINEAR) {
        counts[0]++;
        if (B == 0) {
            int same = C == 0;
            CHECK(res.kind == (same ? RK_ALL : RK_NOSOL), "%s: wanted %s, got kind %d", in, same ? "all" : "none", res.kind);
            check_eqn_states_sets(in, same);
        } else {
            long long n = -C, d = B;
            double root;
            if (d < 0) { n = -n; d = -d; }
            { long long g = gcdll(n, d); if (g > 1) { n /= g; d /= g; } }
            CHECK(res.kind == RK_ONE && res.num == n && res.den == d, "%s: wanted %lld/%lld got %ld/%ld (kind %d)", in, n, d, res.num, res.den, res.kind);
            root = (double)n / (double)d;
            check_eqn_states(in, &root, 1);
            /* the final answer text and the decimal */
            {
                char want_s[40], got[300];
                if (d == 1) sprintf(want_s, "Final answer: x = %lld", n); else sprintf(want_s, "Final answer: x = %lld/%lld", n, d);
                clean_nb(steps[nsteps - 1].text, got);
                CHECK(strcmp(got, want_s) == 0, "%s: final text '%s' wanted '%s'", in, got, want_s);
                if (d != 1) {
                    char *p = strstr(steps[nsteps - 1].state, "(about ");
                    CHECK(p != 0 && fabs(atof(p + 7) - root) <= 0.0051, "%s: bad decimal in '%s'", in, steps[nsteps - 1].state);
                }
            }
        }
    } else {
        double D = (double)(B * B - 4 * A * C);
        double roots[2];
        int nr = 0, k;
        counts[1]++;
        if (D < 0) {
            CHECK(res.kind == RK_NOSOL && res.nroots == 0, "%s: wanted no real solutions", in);
            CHECK(strstr(steps[nsteps - 1].text, "no real solutions") != 0, "%s: final text", in);
        } else {
            double sq = sqrt(D);
            int integer;
            roots[nr++] = (-B + sq) / (2.0 * A);
            if (D > 0) roots[nr++] = (-B - sq) / (2.0 * A);
            integer = fabs(roots[0] - floor(roots[0] + 0.5)) < 1e-9 && fabs(roots[nr - 1] - floor(roots[nr - 1] + 0.5)) < 1e-9;
            CHECK(res.kind == RK_ROOTS && res.nroots == nr, "%s: wanted %d roots, got kind %d n %d", in, nr, res.kind, res.nroots);
            for (k = 0; k < nr; k++) {
                int j, found = 0;
                for (j = 0; j < res.nroots; j++) {
                    double v = ((double)res.root[j].u + (double)res.root[j].w * sqrt((double)res.root[j].m)) / (double)res.root[j].den;
                    if (fabs(v - roots[k]) < 1e-9 * (1 + fabs(roots[k]))) found = 1;
                }
                CHECK(found, "%s: root %.9g missing in the answer", in, roots[k]);
            }
            check_eqn_states(in, roots, nr);
            /* factoring is used exactly when the roots are whole numbers */
            {
                int fact = 0, form = 0, i;
                for (i = 0; i < nsteps; i++) {
                    if (strstr(steps[i].text, "Find two numbers")) fact = 1;
                    if (strstr(steps[i].text, "quadratic formula")) form = 1;
                }
                CHECK(integer ? (fact && !form) : (!fact && form), "%s: wrong method (integer roots %d, factoring %d, formula %d)", in, integer, fact, form);
            }
            /* every "x = ..." / "x1 = ..." chain in the steps evaluates to the right root */
            {
                int i;
                for (i = 0; i < nsteps; i++) {
                    const char *st = steps[i].state;
                    int which = -1;
                    char buf[300], *seg, *save;
                    if (strncmp(st, "x1 = ", 5) == 0) which = 0;
                    else if (strncmp(st, "x2 = ", 5) == 0) which = 1;
                    if (which < 0 || nr < 2) continue;
                    clean_nb(st + 5, buf);
                    { char *tl = strstr(buf, " ~ "); if (tl) *tl = 0; }
                    for (seg = buf; seg; seg = save) {
                        char *e = strstr(seg, " = ");
                        int all;
                        double v;
                        if (e) { *e = 0; save = e + 3; } else save = 0;
                        v = q_eval(seg, &all);
                        CHECK(all && fabs(v - roots[which]) < 1e-9 * (1 + fabs(roots[which])), "%s: step %d chain part '%s' = %.9g, wanted %.9g", in, i + 1, seg, v, roots[which]);
                    }
                    /* the ~ decimal */
                    {
                        const char *ap = strstr(st, " ~ ");
                        if (ap) CHECK(fabs(atof(ap + 3) - roots[which]) <= 0.0101, "%s: decimal '%s' vs %.9g", in, ap, roots[which]);
                    }
                }
            }
        }
        /* the discriminant step: every part of the chain is D */
        {
            int i;
            for (i = 0; i < nsteps; i++) {
                if (strncmp(steps[i].state, "D = ", 4) == 0) {
                    char buf[300], *seg, *save;
                    clean_nb(steps[i].state + 4, buf);
                    for (seg = buf; seg; seg = save) {
                        char *e = strstr(seg, " = ");
                        int all;
                        double v;
                        if (e) { *e = 0; save = e + 3; } else save = 0;
                        v = q_eval(seg, &all);
                        CHECK(all && fabs(v - D) < 1e-9, "%s: discriminant part '%s' = %.9g, wanted %.9g", in, seg, v, D);
                    }
                }
            }
        }
    }
    if (verbose || counts[want == PT_LINEAR ? 0 : 1] <= 14) print_steps(in, want == PT_LINEAR ? "linear" : "quadratic");
}

/* ---------------------------------------------------------------- */
/* expressions: simplify and derivative                             */
/* ---------------------------------------------------------------- */
static int sample_poly(const char *in, long *p, int n);
static void test_simplify(const char *in, const long *refp, int n)
{
    long trunc0 = trunc_n;
    char want[120], got[300];
    int i;
    cls_t cl;
    long long orig[9];
    int root;
    if (!classify(in, &cl) || cl.type != PT_SIMPLIFY) { FAIL("%s: classified wrongly (%d)", in, cl.type); return; }
    root = parse(in);
    for (i = -4; i <= 4; i++) { rq q = ev_q(root, i); orig[i + 4] = q.n; }
    CHECK(generate(in, PT_SIMPLIFY), "%s: generate failed: %s", in, g_fail ? g_fail : "?");
    if (nsteps == 0) return;
    counts[2]++;
    check_layout(in);
    CHECK(trunc_n == trunc0, "%s: truncated", in);
    ref_print_poly(refp, n, want);
    clean_nb(steps[nsteps - 1].state, got);
    CHECK(strcmp(want, got) == 0, "%s: final '%s' wanted '%s'", in, got, want);
    for (i = 0; i < n && i < MAXK + 1; i++) CHECK(res.poly[i] == refp[i], "%s: res.poly[%d]=%ld wanted %ld", in, i, res.poly[i], refp[i]);
    for (i = 0; i < nsteps; i++) {
        char buf[300];
        int r2, x;
        if (steps[i].eqv != EQ_EXPR) continue;
        clean_nb(steps[i].state, buf);
        r2 = parse(buf);
        CHECK(r2 >= 0, "%s: state '%s' does not parse", in, buf);
        if (r2 < 0) continue;
        for (x = -4; x <= 4; x++) {
            rq q = ev_q(r2, x);
            CHECK(q.ok && q.n == orig[x + 4] && q.d == 1, "%s: step %d '%s' differs at x=%d", in, i + 1, buf, x);
        }
    }
    if (verbose || counts[2] <= 12) print_steps(in, "simplify");
}

static void test_deriv(const char *in, const long *refp, int n)
{
    long trunc0 = trunc_n;
    char want[120], got[300], dwant[120];
    long dp[GK + 2];
    int i, termsk = 0, terms0 = 0;
    long poly_in[GK + 2];
    cls_t cl;
    memset(dp, 0, sizeof dp);
    for (i = 1; i < n; i++) dp[i - 1] = refp[i] * i;
    CHECK(classify(in, &cl) && cl.type == PT_SIMPLIFY && cl.ambiguous, "%s: should be ambiguous", in);
    CHECK(generate(in, PT_DERIV), "%s: derivative generate failed: %s", in, g_fail ? g_fail : "?");
    if (nsteps == 0) return;
    counts[3]++;
    check_layout(in);
    CHECK(trunc_n == trunc0, "%s: truncated", in);
    ref_print_poly(dp, n, dwant);
    clean_nb(steps[nsteps - 1].state, got);
    CHECK(strcmp(dwant, got) == 0, "%s: derivative '%s' wanted '%s'", in, got, dwant);
    for (i = 0; i < n; i++) CHECK(res.poly[i] == dp[i], "%s: res.poly[%d]", in, i);
    CHECK(steps[nsteps - 1].eqv == EQ_DERIV, "%s: final eqv", in);
    /* the rule names match the terms of the (combined) function */
    memcpy(poly_in, refp, sizeof(long) * (size_t)n);
    for (i = 0; i < n; i++) { if (poly_in[i] && i >= 1) termsk++; if (poly_in[i] && i == 0) terms0++; }
    {
        int pr = 0, cr = 0, p1 = 0;
        for (i = 0; i < nsteps; i++) {
            if (strstr(steps[i].text, "Power rule")) pr++;
            if (strstr(steps[i].text, "Constant rule")) cr++;
            if (strstr(steps[i].text, "Power rule with n = 1")) p1++;
        }
        CHECK(pr == termsk, "%s: %d power-rule steps for %d x-terms", in, pr, termsk);
        CHECK(cr == (terms0 ? 1 : (poly_degv(refp, n) < 0 ? 1 : 0)), "%s: %d constant-rule steps", in, cr);
        CHECK(p1 == (refp[1] != 0), "%s: n = 1 steps", in);
    }
    /* sentences like "so d/dx[3x^3] = 9x^2." state the right derivative */
    for (i = 0; i < nsteps; i++) {
        char buf[400];
        const char *p;
        clean_nb(steps[i].text, buf);
        for (p = buf; (p = strstr(p, "d/dx[")) != 0; p += 5) {
            const char *e = strchr(p, ']'), *r, *rend;
            char t[80], rr[80];
            long tp[GK + 2], rp[GK + 2];
            int k, ok = 1;
            if (!e || strncmp(e, "] = ", 4) != 0 || e - p - 5 >= 79) continue;
            memcpy(t, p + 5, (size_t)(e - p - 5)); t[e - p - 5] = 0;
            if (strchr(t, 'n')) continue;
            r = e + 4;
            rend = r;
            while (*rend && !(*rend == '.' && (rend[1] == ' ' || rend[1] == 0))) rend++;
            if (rend - r >= 79) continue;
            memcpy(rr, r, (size_t)(rend - r)); rr[rend - r] = 0;
            if (!sample_poly(t, tp, GK) || !sample_poly(rr, rp, GK)) { FAIL("%s: cannot read '%s' = '%s'", in, t, rr); continue; }
            for (k = 1; k < GK; k++) if (rp[k - 1] != tp[k] * k) ok = 0;
            if (rp[GK - 1] != 0) ok = 0;
            CHECK(ok, "%s: text says d/dx[%s] = %s", in, t, rr);
        }
    }
    /* every intermediate state: replace each d/dx[T] by the true derivative of T; the whole must equal f'(x) */
    for (i = 0; i < nsteps; i++) {
        char buf[300], out[400];
        const char *p;
        int x, r2, oi = 0;
        if (steps[i].eqv == EQ_EXPR || steps[i].eqv == EQ_DERIV) continue;
        clean_nb(steps[i].state, buf);
        for (p = buf; *p && oi < 380; ) {
            if (strncmp(p, "d/dx[", 5) == 0) {
                char t[80], dd[80];
                long tp[GK + 2], td[GK + 2];
                const char *e = strchr(p, ']');
                int tn = 0, k;
                if (!e || e - p - 5 >= 79) { FAIL("%s: bad d/dx in '%s'", in, buf); break; }
                memcpy(t, p + 5, (size_t)(e - p - 5)); t[e - p - 5] = 0;
                if (!sample_poly(t, tp, GK)) { FAIL("%s: cannot read '%s'", in, t); break; }
                memset(td, 0, sizeof td);
                for (k = 1; k < GK; k++) td[k - 1] = tp[k] * k;
                for (k = 0; k < GK; k++) if (td[k]) tn = k + 1;
                if (tn == 0) tn = 1;
                ref_print_poly(td, tn, dd);
                oi += sprintf(out + oi, "(%s)", dd);
                p = e + 1;
            } else out[oi++] = *p++;
        }
        out[oi] = 0;
        {
            char *seg, *save;
            for (seg = out; seg; seg = save) {
                char *e = strstr(seg, " = ");
                if (e) { *e = 0; save = e + 3; } else save = 0;
                r2 = parse(seg);
                CHECK(r2 >= 0, "%s: step %d state '%s' -> '%s' does not parse", in, i + 1, buf, seg);
                if (r2 < 0) continue;
                for (x = -4; x <= 4; x++) {
                    rq q = ev_q(r2, x);
                    long long want_v = 0, pw = 1;
                    int k;
                    for (k = 0; k < n; k++) { want_v += dp[k] * pw; pw *= x; }
                    CHECK(q.ok && q.d == 1 && q.n == want_v, "%s: step %d '%s' is not f'(x) at x=%d", in, i + 1, buf, x);
                }
            }
        }
    }
    /* any combine steps are true expressions */
    {
        int root = parse(in), x;
        long long orig[9];
        for (x = -4; x <= 4; x++) { rq q = ev_q(root, x); orig[x + 4] = q.n; }
        for (i = 0; i < nsteps; i++) {
            char buf[300];
            int r2;
            if (steps[i].eqv != EQ_EXPR) continue;
            clean_nb(steps[i].state, buf);
            r2 = parse(buf);
            if (r2 < 0) { FAIL("%s: bad state '%s'", in, buf); continue; }
            for (x = -4; x <= 4; x++) { rq q = ev_q(r2, x); CHECK(q.ok && q.n == orig[x + 4], "%s: combine step %d wrong", in, i + 1); }
        }
    }
    (void)want; (void)terms0;
    if (verbose || counts[3] <= 12) print_steps(in, "derivative");
}

/* ---------------------------------------------------------------- */
/* fractions                                                        */
/* ---------------------------------------------------------------- */
static void test_frac(const char *in, long long an, long long ad, char op, long long bn, long long bd)
{
    long trunc0 = trunc_n;
    long long n, d, g;
    cls_t cl;
    double want;
    int i;
    char got[300];
    CHECK(classify(in, &cl) && cl.type == PT_FRAC, "%s: classified as %d (%s)", in, cl.type, cl.why ? cl.why : "");
    if (cl.type != PT_FRAC) return;
    CHECK(generate(in, PT_FRAC), "%s: generate failed: %s", in, g_fail ? g_fail : "?");
    if (nsteps == 0) return;
    counts[4]++;
    check_layout(in);
    CHECK(trunc_n == trunc0, "%s: truncated", in);
    if (op == '+') { n = an * bd + bn * ad; d = ad * bd; }
    else if (op == '-') { n = an * bd - bn * ad; d = ad * bd; }
    else if (op == '*') { n = an * bn; d = ad * bd; }
    else if (op == '/') { n = an * bd; d = ad * bn; }
    else { n = an; d = ad; }
    if (d < 0) { n = -n; d = -d; }
    g = gcdll(n, d);
    if (g > 1) { n /= g; d /= g; }
    if (n == 0) d = 1;
    CHECK(res.kind == RK_FRAC && res.num == n && res.den == d, "%s: wanted %lld/%lld got %ld/%ld", in, n, d, res.num, res.den);
    want = (double)n / (double)d;
    clean_nb(steps[nsteps - 1].text, got);
    {
        char ws[80];
        if (d == 1) sprintf(ws, "Final answer: %lld", n); else sprintf(ws, "Final answer: %lld/%lld", n, d);
        CHECK(strcmp(got, ws) == 0, "%s: final '%s' wanted '%s'", in, got, ws);
    }
    if (op == '+' || op == '-') {
        long long L = ad / gcdll(ad, bd) * bd;
        char ws[60];
        int found = 0;
        for (i = 0; i < nsteps; i++) {
            if (strstr(steps[i].text, "Find a common denominator")) {
                sprintf(ws, "divide into is %lld.", L);
                found = 1;
                CHECK(ad != bd && strstr(steps[i].text, ws), "%s: common denominator should be %lld ('%s')", in, L, steps[i].text);
            }
            if (strstr(steps[i].text, "already have the same denominator")) {
                found = 1;
                CHECK(ad == bd, "%s: same-denominator step wrongly used", in);
            }
        }
        CHECK(found, "%s: no common denominator step", in);
    }
    /* every state is a chain of expressions that all equal the answer */
    for (i = 0; i < nsteps; i++) {
        char buf[300], *seg, *save;
        if (steps[i].eqv != EQ_EXPR) continue;
        clean_nb(steps[i].state, buf);
        for (seg = buf; seg; seg = save) {
            char *e = strstr(seg, " = ");
            int all;
            double v;
            if (e) { *e = 0; save = e + 3; } else save = 0;
            v = q_eval(seg, &all);
            CHECK(all && fabs(v - want) < 1e-9, "%s: step %d part '%s' = %.9g, wanted %.9g", in, i + 1, seg, v, want);
        }
    }
    /* steps that are not "=" chains are the plain expression being rewritten: also the same value */
    if (d != 1) {
        char *p = strstr(steps[nsteps - 1].state, "(about ");
        CHECK(p && fabs(atof(p + 7) - want) <= 0.0051, "%s: decimal in '%s'", in, steps[nsteps - 1].state);
    }
    if (verbose || counts[4] <= 14) print_steps(in, "fractions");
}

/* ---------------------------------------------------------------- */
/* reading polynomials by sampling the syntax tree                  */
/* ---------------------------------------------------------------- */
/* coefficients p[0..n-1] of an expression of degree < n, from its values at x = 0..n-1 */
static int sample_poly(const char *in, long *p, int n)
{
    int root = parse(in), i, j, k;
    double m[GK][GK + 1];
    if (root < 0) return 0;
    for (i = 0; i < n; i++) {
        rq q = ev_q(root, i);
        if (!q.ok || q.d != 1) return 0;
        for (j = 0; j < n; j++) m[i][j] = pow((double)i, (double)j);
        m[i][n] = (double)q.n;
    }
    for (i = 0; i < n; i++) {
        double piv = m[i][i];
        for (j = i; j <= n; j++) m[i][j] /= piv;
        for (k = 0; k < n; k++) {
            double f;
            if (k == i) continue;
            f = m[k][i];
            for (j = i; j <= n; j++) m[k][j] -= f * m[i][j];
        }
    }
    for (i = 0; i < n; i++) p[i] = (long)floor(m[i][n] + 0.5);
    return 1;
}

/* ---------------------------------------------------------------- */
/* the test lists                                                   */
/* ---------------------------------------------------------------- */
static void test_expr_string(const char *in, int deriv)
{
    long p[GK];
    int n = GK;
    if (!sample_poly(in, p, n)) { FAIL("%s: cannot sample", in); return; }
    if (deriv && strchr(in, 'x') == 0) return;      /* a constant is not offered the derivative */
    if (deriv) test_deriv(in, p, n);
    else test_simplify(in, p, n);
}

static void fmt_operand(char *out, long long n, long long d, int variant)
{
    /* variant 1: negative denominators like 3/(-4) */
    if (d == 1) sprintf(out, "%lld", n);
    else if (variant && n > 0 && d > 1) sprintf(out, "%lld/(-%lld)", n, d), (void)0;
    else sprintf(out, "%lld/%lld", n, d);
}

static void test_frac_case(long long an, long long ad, char op, long long bn, long long bd)
{
    char A[40], B[40], in[100];
    long long bn2 = bn, bd2 = bd;
    long long an2 = an, ad2 = ad;
    if (op == 0) {
        fmt_operand(A, an, ad, 0);
        sprintf(in, "%s", A);
    } else {
        char Bp[48];
        fmt_operand(A, an, ad, 0);
        fmt_operand(B, bn, bd, 0);
        if (an < 0) { char t[48]; if (op == '/') sprintf(t, "(%s)", A); else strcpy(t, A); strcpy(A, t); }
        if (op == '/' || bn < 0 || (op == '*' && ad != 1 && bd != 1 && 0)) sprintf(Bp, "(%s)", B);
        else strcpy(Bp, B);
        if (op == '/') { char t[48]; if (an >= 0 && ad != 1) { sprintf(t, "(%s)", A); strcpy(A, t); } else if (an >= 0) { /* plain integer */ } }
        sprintf(in, "%s%c%s", A, op, Bp);
    }
    if (strlen(in) > MAXIN) return;
    if (!strchr(in, '/')) return;           /* whole numbers only: that is a "simplify" problem, tested elsewhere */
    /* a/(-d) forms are only produced by variant 1 */
    test_frac(in, an2, ad2, op, bn2, bd2);
}

static void run_fixed(void)
{
    static const char *const lin[] = {
        "3x+5=20", "2(x-4)=x+7", "2x+1=2x+3", "5=5", "x=7", "-x=3", "4x=6", "6x=4", "x/2=1" /* refused */, "7x-3=3x+9",
        "3(x+2)=2(x-1)+9", "5-2x=x+17", "x+x=4+x", "2x-6=2(x-3)", "-(x-3)=2x+9", "10=3x+1", "0x+3=3", "9x=0", "x-5=-5", "3x+4-(x+1)=x+7",
        "4(x+1)-3(x-2)=20", "x^2+x=x^2+5", "x^3+x=x^3+4", "2x^2-5=2x^2+3x+1", "1=2", "x=x", "-3x-3=-3x-3", "7x=7x+1", "100x=250", "-7x=-21",
        "x+3=-x", "2(3x+1)-4=x", "12x-5=-5+12x+0", "5x-5x=0", "3=x", "2x+2x+2x=18"
    };
    static const char *const quad[] = {
        "x^2+5x+6=0", "x^2-5x+6=0", "2x^2+3x-1=0", "x^2+2x+1=0", "x^2+4=0", "2x^2+3x-2=0", "x^2-3=0", "x^2+4x=0", "x^2=9", "x^2-9=0",
        "3x^2-12=0", "x^2=2x+8", "x^2+x=x+4", "2x^2-8x+8=0", "-x^2+4x-3=0", "x^2+x+1=0", "6x^2-5x+1=0", "4x^2+4x+1=0", "x^2-2x-2=0", "x(x+4)=21",
        "(x+3)(x-2)=0" /* refused */, "5x^2=0", "x^2-4x=-4", "3x^2+2x=1", "-2x^2-4x-2=0", "x^2=x", "2(x^2+3x)=-4", "x^2+6x+9=x+9", "9999x^2+9999x+9999=0",
        "10x^2+3x-1=0", "x^2-x-1=0", "-3x^2+5=0", "3x^2+2x-5=x^2+x+1", "x^2+3x+3=2x^2+x+3", "x^3=x^3+x^2-4"
    };
    static const char *const simp[] = {
        "3(x+2)+4x", "2(x+1)-3(x-4)", "5-(x+2)", "x+x+x", "3x+5", "-(x-3)+2x^2-x^2", "7", "2*3+4", "3(4+2)-5", "x(x+3)-2x",
        "-3(2x-1)", "(x+1)", "x-x", "2x^2+3x^2-x", "5-3-2", "x^2+2x+x^2+1+x", "-(x+1)-(x-1)", "4x-2(x+3)+6", "x+(x+1)", "3x+2x^2+5-x^2+2-x",
        "2(x^2+3)-x(x+1)", "0x+3", "x^3+x^3", "-x", "10x-10x+1", "2x(x+1)", "x-(x+2)+(x-3)", "3(x+1)+2(x+2)+4(x+3)"
    };
    static const char *const der[] = {
        "3x^2+5x+7", "x^3", "7", "5x-x^2", "x+x+x^2", "-4x^5+x", "x", "2x^4-x^2+x-9", "100x^3-50x^2+x",
        "x-x", "6", "-x^2", "9x^2+9x^2", "x^2+1", "2x+3x", "-7x+7", "x^5+x^4+x^3+x^2+x+1", "5-x", "0"
    };
    static const char *const bad[] = {
        "x/2+1=3", "1/x=2", "x^4=16", "(x+1)(x+2)", "(x+1)^2", "2^3", "3x+", "3x=", "=5", "x==2", "", "()", "5)(", "x^", "x^-2", "3.5x=7", "12345x=1", "2(x+(3x+1))=4", "2(x+1)(x-2)=0", "1/2+1/3+1/4",
        "1/2/3/4", "1/0+2", "(1/2)/(0/3)", "3(x+1)(", "x^13", "x^2^2", "x^2-10000x+9999=0", "3 4", "2x3", "sqrt(4)", "y+1=3", "x=y", "a", "1/2=x", "2(1/2+x)", "7/(x+1)", "3(x^2+1)=5(x^3+1)"
    };
    static const char *const bad_expect_fail[] = { "3(5000x^2+x)=0", "99999=x" };
    size_t i;
    cls_t cl;
    for (i = 0; i < sizeof lin / sizeof lin[0]; i++) {
        if (!strcmp(lin[i], "x/2=1")) { CHECK(!classify(lin[i], &cl) && cl.why, "x/2=1 should be refused"); continue; }
        if (!strncmp(lin[i], "x^3+x=", 6)) { /* cancels to linear */ }
        test_equation(lin[i]);
    }
    for (i = 0; i < sizeof quad / sizeof quad[0]; i++) {
        if (!strcmp(quad[i], "(x+3)(x-2)=0")) { CHECK(!classify(quad[i], &cl) && cl.why, "two brackets should be refused"); continue; }
        if (!strcmp(quad[i], "x^3=x^3+x^2-4")) { test_equation(quad[i]); continue; }
        test_equation(quad[i]);
    }
    for (i = 0; i < sizeof simp / sizeof simp[0]; i++) test_expr_string(simp[i], 0);
    for (i = 0; i < sizeof der / sizeof der[0]; i++) test_expr_string(der[i], 1);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        long t0 = trunc_n;
        int ok = classify(bad[i], &cl);
        if (!strcmp(bad[i], "(x+1)") || 0) continue;
        if (ok) {
            /* some strings are fine after all (the list also has a few valid ones): they must then solve cleanly */
            uint8_t t = cl.type;
            if (cl.ambiguous) t = PT_SIMPLIFY;
            if (!generate(bad[i], t)) { CHECK(g_fail && g_fail[0], "%s: refusal without a reason", bad[i]); }
            else check_layout(bad[i]);
        } else {
            CHECK(cl.why && cl.why[0], "%s: refusal without a reason", bad[i]);
        }
        CHECK(trunc_n == t0, "%s: truncation", bad[i]);
    }
    for (i = 0; i < sizeof bad_expect_fail / sizeof bad_expect_fail[0]; i++) {
        int ok = classify(bad_expect_fail[i], &cl);
        if (ok) CHECK(!generate(bad_expect_fail[i], cl.type) && g_fail, "%s: should be refused", bad_expect_fail[i]);
        else CHECK(cl.why != 0, "%s: reason", bad_expect_fail[i]);
    }
    /* a high power is beyond what the sampler reads: check it directly */
    CHECK(classify("x^12", &cl) && generate("x^12", PT_DERIV) && res.poly[11] == 12 && strcmp(steps[nsteps - 1].state, "12x^11") == 0, "x^12 derivative");
    /* fractions: hand-picked */
    {
        static const long long F[][5] = {
            { 1, 4, '+', 1, 6 }, { 1, 2, '+', 3, 4 }, { 3, 4, '-', 1, 6 }, { 2, 3, '*', 5, 7 }, { 1, 2, '/', 3, 4 },
            { 6, 8, 0, 0, 0 }, { 5, 10, '-', 1, 2 }, { 3, 5, 0, 0, 0 }, { -1, 2, '+', 3, 4 }, { 4, 1, '+', 1, 3 },
            { 7, 3, '*', 3, 7 }, { 2, 5, '/', 2, 5 }, { -3, 4, '/', -9, 8 }, { 1, 6, '+', 1, 6 }, { 5, 6, '-', 5, 6 },
            { 0, 3, '+', 1, 2 }, { 9999, 9998, '+', 9997, 9996 }, { 1, 1, '/', 2, 3 }, { 4, 6, 0, 0, 0 }, { 0, 5, 0, 0, 0 },
            { 12, 18, '*', 3, 4 }, { 1, 2, '-', 3, 4 }, { 7, 1, '*', 1, 7 }, { -5, 3, '-', 2, 9 }, { 2, 3, '/', 4, 1 }
        };
        for (i = 0; i < sizeof F / sizeof F[0]; i++) test_frac_case(F[i][0], F[i][1], (char)F[i][2], F[i][3], F[i][4]);
        /* more spellings of the same things */
        test_frac("1/2--3/4", 1, 2, '-', -3, 4);
        test_frac("3/(-4)", -3, 4, 0, 0, 0);
        test_frac("-(1/2)+1/3", -1, 2, '+', 1, 3);
        test_frac("2/3*5/7", 2, 3, '*', 5, 7);
        test_frac("1/2*3", 1, 2, '*', 3, 1);
        test_frac("3*1/2", 3, 1, '*', 1, 2);
        test_frac("1/2/3", 1, 2, '/', 3, 1);
        CHECK(classify("(1/2)/(0)", &cl) && generate("(1/2)/(0)", PT_FRAC) == false && g_fail, "division by zero must be refused");
    }
}

/* ---------------------------------------------------------------- */
/* random tests                                                     */
/* ---------------------------------------------------------------- */
static void gen_eq_string(char *out, int maxdeg, int brackets, long rng)
{
    gen_t L, R;
    do {
        gen_side(&L, maxdeg, brackets, rng);
        gen_side(&R, maxdeg, brackets, rng);
    } while (strlen(L.s) + strlen(R.s) + 1 > MAXIN);
    sprintf(out, "%s=%s", L.s, R.s);
}

static void run_random(long N)
{
    long i;
    char in[100];
    for (i = 0; i < N; i++) {
        long rng = (i % 5 == 0) ? 3 : (i % 5 == 1) ? 9 : (i % 5 == 2) ? 30 : (i % 5 == 3) ? 2 : 1;
        int kind = (int)(rr() % 6);
        if (kind <= 1) {                                   /* linear-ish */
            gen_eq_string(in, 1, kind == 1, rng);
            test_equation(in);
        } else if (kind == 2) {                            /* quadratic-ish, any shape */
            gen_eq_string(in, 2, 1, rng);
            test_equation(in);
        } else if (kind == 3) {                            /* quadratics with integer roots */
            long a = rnd_range(-6, 6), r1 = rnd_range(-9, 9), r2 = rnd_range(-9, 9), b, c;
            int form = (int)(rr() % 3);
            if (a == 0) a = 1;
            b = -a * (r1 + r2);
            c = a * r1 * r2;
            if (form == 0) {
                gen_t g; memset(&g, 0, sizeof g);
                g_mono(&g, a, 2); g_mono(&g, b, 1); g_mono(&g, c, 0);
                sprintf(in, "%s=0", g.s);
            } else if (form == 1) {
                gen_t g; memset(&g, 0, sizeof g);
                g_mono(&g, a, 2); g_mono(&g, b, 1);
                sprintf(in, "%s=%ld", g.s, -c);
            } else {
                gen_t g; memset(&g, 0, sizeof g);
                g_mono(&g, c, 0); g_mono(&g, b, 1); g_mono(&g, a, 2);
                sprintf(in, "0=%s", g.s);
            }
            test_equation(in);
        } else if (kind == 4) {                            /* quadratics with any coefficients */
            long a = rnd_range(-40, 40), b = rnd_range(-200, 200), c = rnd_range(-200, 200);
            gen_t g;
            if (a == 0) a = 7;
            memset(&g, 0, sizeof g);
            g_mono(&g, a, 2); g_mono(&g, b, 1); g_mono(&g, c, 0);
            sprintf(in, "%s=0", g.s);
            test_equation(in);
        } else {                                           /* a quadratic with a big a, b, c */
            long a = rnd_range(-9999, 9999), b = rnd_range(-9999, 9999), c = rnd_range(-9999, 9999);
            gen_t g;
            if (a == 0) a = 9999;
            memset(&g, 0, sizeof g);
            g_mono(&g, a, 2); g_mono(&g, b, 1); g_mono(&g, c, 0);
            sprintf(in, "%s=0", g.s);
            test_equation(in);
        }
    }
    /* expressions */
    for (i = 0; i < N; i++) {
        gen_t g;
        long rng = (i % 3 == 0) ? 3 : (i % 3 == 1) ? 9 : 40;
        long p[GK];
        int n = GK, k, derivable = 1;
        do { gen_side(&g, 2, (int)(rr() % 2), rng); } while (strlen(g.s) > MAXIN);
        if (!sample_poly(g.s, p, n)) { FAIL("%s: cannot sample", g.s); continue; }
        for (k = 0; k < n; k++) CHECK(p[k] == g.p[k], "%s: generator and tree disagree at x^%d (%ld vs %ld)", g.s, k, g.p[k], p[k]);
        (void)derivable;
        test_simplify(g.s, p, n);
        if (strchr(g.s, 'x') && !strchr(g.s, '(')) test_deriv(g.s, p, n);
    }
    /* fractions */
    for (i = 0; i < N; i++) {
        static const char ops[] = { '+', '-', '*', '/', 0 };
        long long an = rnd_range(-12, 12), ad = rnd_range(1, 12), bn = rnd_range(-12, 12), bd = rnd_range(1, 12);
        char op = ops[rr() % 5];
        if (rr() % 7 == 0) { an = rnd_range(-9999, 9999); ad = rnd_range(1, 9999); bn = rnd_range(-9999, 9999); bd = rnd_range(1, 9999); }
        if (rr() % 5 == 0) ad = 1;
        if (rr() % 5 == 0) bd = 1;
        if (op == '/' && bn == 0) bn = 1;
        test_frac_case(an, ad, op, bn, bd);
    }
}

/* ---------------------------------------------------------------- */
/* garbage in, answer or refusal out                                */
/* ---------------------------------------------------------------- */
static void run_garbage(long N)
{
    static const char alpha[] = "0123456789xx+-*/^()=0123xx(()) ";
    static const char *const frag[] = { "x^2", "3x", "2(", "x+", "=0", "/2", "(x+1)", "-(", "x(x", "5)", "1/2", "+(1/3)", "x^", "10", "2x^2" };
    long i, solved = 0, refused = 0;
    for (i = 0; i < N; i++) {
        char in[60];
        int len = (int)rnd_range(0, 42), j;
        cls_t cl;
        long t0 = trunc_n;
        in[0] = 0;
        if (rr() % 2) {
            for (j = 0; j < len; j++) in[j] = alpha[rr() % (sizeof alpha - 1)];
            in[len > MAXIN ? MAXIN : len] = 0;
        } else {
            int k = (int)rnd_range(1, 8);
            while (k-- > 0 && strlen(in) < 30) strcat(in, frag[rr() % (sizeof frag / sizeof frag[0])]);
            in[MAXIN] = 0;
        }
        if (classify(in, &cl)) {
            uint8_t t[2];
            int nt = 1, q;
            t[0] = cl.type;
            if (cl.ambiguous) { t[1] = PT_DERIV; nt = 2; }
            for (q = 0; q < nt; q++) {
                if (generate(in, t[q])) {
                    solved++;
                    check_layout(in);
                    CHECK(res.type == t[q], "%s: result type", in);
                    /* generic truth checks */
                    if (t[q] == PT_LINEAR || t[q] == PT_QUAD) {
                        long long c0, c1, c2, c3;
                        if (sample_equation(in, &c0, &c1, &c2, &c3) && c3 == c2 * 9 + c1 * 3 + c0) {
                            double roots[2];
                            int nr = 0;
                            if (t[q] == PT_LINEAR && res.kind == RK_ONE) { roots[nr++] = (double)res.num / (double)res.den; }
                            else if (t[q] == PT_QUAD && res.kind == RK_ROOTS) {
                                int r;
                                for (r = 0; r < res.nroots; r++) roots[nr++] = ((double)res.root[r].u + (double)res.root[r].w * sqrt((double)res.root[r].m)) / (double)res.root[r].den;
                            }
                            if (nr) check_eqn_states(in, roots, nr);
                            else if (t[q] == PT_LINEAR && (res.kind == RK_ALL || res.kind == RK_NOSOL)) check_eqn_states_sets(in, res.kind == RK_ALL);
                        }
                    }
                } else {
                    refused++;
                    CHECK(g_fail && g_fail[0], "%s: refusal without reason", in);
                }
            }
        } else {
            refused++;
            CHECK(cl.why && cl.why[0], "%s: refusal without reason", in);
            CHECK(cl.at < 0 || cl.at <= (int)strlen(in), "%s: error position %d", in, cl.at);
        }
        CHECK(trunc_n == t0, "%s: truncated string", in);
    }
    printf("garbage: %ld random strings: %ld solved, %ld refused with a reason\n", N, solved, refused);
}

/* ---------------------------------------------------------------- */
/* fake keypad                                                      */
/* ---------------------------------------------------------------- */
typedef struct { uint8_t g, m; int shot; } tap_t;
static tap_t q[8192];
static int qh, qt, tap_release;
static void tap(uint8_t g, uint8_t m) { q[qt & 8191].g = g; q[qt & 8191].m = m; q[qt & 8191].shot = -1; qt++; }
static const char *shot_names[1024];
static int nshots;
static const char *shot_dir;
static void shot(const char *name)
{
    q[qt & 8191].g = 0; q[qt & 8191].m = 0; q[qt & 8191].shot = nshots;
    shot_names[nshots++] = name;
    qt++;
}
static void idle(int n) { while (n-- > 0) tap(0, 0); }
static void typestr(const char *s)
{
    for (; *s; s++) {
        int i;
        if (*s == '^') { tap(6, kb_Power); continue; }
        for (i = 0; i < NKM; i++) if (KM[i].s[0] == *s && KM[i].s[1] == 0 && !(KM[i].g == 5 && KM[i].m == kb_Chs)) { tap(KM[i].g, KM[i].m); break; }
    }
}

static int fuzz_mode, fuzz_left;
static long fuzz_frames;

static void write_ppm(const char *name)
{
    char path[256];
    FILE *f;
    int x, y;
    snprintf(path, sizeof path, "%s/%s.ppm", shot_dir, name);
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 240\n255\n");
    for (y = 0; y < 240; y++) for (x = 0; x < 320; x++) {
        uint16_t c = stub_pal[vram[scr_idx][y][x]];
        unsigned char rgb[3] = { (unsigned char)(((c >> 10) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 31) * 255 / 31),
                                 (unsigned char)((c & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void on_present_hook(int screen) { (void)screen; }

void stub_on_swap(void)
{
    frames++;
    memset(&cost, 0, sizeof cost);
    if (frames > 40000000L) { fprintf(stderr, "ERROR: frame cap reached\n"); exit(3); }
}

void stub_feed_keys(uint8_t *kb)
{
    memset(kb, 0, 8);
    if (tap_release) { tap_release = 0; return; }
    if (qh != qt) {
        tap_t *t = &q[qh & 8191];
        if (t->shot >= 0 && shot_dir) write_ppm(shot_names[t->shot]);
        kb[t->g] |= t->m;
        qh++;
        tap_release = 1;
        return;
    }
    if (fuzz_mode) {
        /* random key presses; now and then a whole problem is typed in */
        static const uint8_t g[] = { 3, 3, 4, 5, 3, 4, 5, 3, 4, 5, 3, 6, 6, 6, 6, 6, 5, 6, 6, 1, 1, 1, 2, 7, 7, 7, 7, 1, 4, 5, 6 };
        static const uint8_t m[] = { kb_0, kb_1, kb_2, kb_3, kb_4, kb_5, kb_6, kb_7, kb_8, kb_9, kb_GraphVar, kb_Add, kb_Sub, kb_Mul, kb_Div,
                                     kb_Power, kb_Chs, kb_Enter, kb_Clear, kb_Yequ, kb_Del, kb_Graph, kb_Square, kb_Left, kb_Right, kb_Up,
                                     kb_Down, kb_2nd, kb_LParen, kb_RParen, kb_Enter };
        static const char *const probs[] = { "3x+5=20", "x^2-5x+6=0", "2x^2+3x-1=0", "3(x+2)+4x", "x^3+2x", "1/2+3/4", "(1/2)/(3/4)",
                                            "2x+1=2x+3", "x^2+4=0", "5-(x+2)", "9999x^2+9999x+9999=0", "1/0", "x^4=16", "((((" };
        if (--fuzz_left <= 0) { fuzz_left = 1; }
        if (fuzz_frames-- <= 0) { memset(kb, 0, 8); if ((fuzz_frames & 3) == 0) kb[6] |= kb_Clear; return; }   /* time is up: CLEAR our way out */
        if (rr() % (getenv("FUZZ_TYPE") ? (unsigned)atoi(getenv("FUZZ_TYPE")) : 60u) == 0) {
            typestr(probs[rr() % (sizeof probs / sizeof probs[0])]);
            tap(6, kb_Enter);
            { int n = (int)(rr() % 30); while (n-- > 0) tap(6, kb_Enter); }
            return;
        }
        {
            int n = (int)(rr() % 3), k;
            for (k = 0; k < n; k++) {
                int i = (int)(rr() % (sizeof g));
                if (g[i] == 6 && m[i] == kb_Clear && rr() % 4) continue;
                kb[g[i]] |= m[i];
            }
        }
    }
}

/* ---------------------------------------------------------------- */
/* screenshots                                                      */
/* ---------------------------------------------------------------- */
static char *shot_name(const char *fmt, const char *a, int n)
{
    char *c = malloc(96);
    snprintf(c, 96, fmt, a, n);
    return c;
}

static void shots_script(void)
{
    /* each problem is typed in and every step is shown; the number of steps is asked from the generator first */
    static const struct { const char *prob; const char *name; int choose; } P[] = {
        { "3x+5=20", "linear", 0 },
        { "2(x-4)=x+7", "linear_brackets", 0 },
        { "2x+1=2x+3", "linear_nosolution", 0 },
        { "x^2+5x+6=0", "quad_factoring", 0 },
        { "2x^2+3x-1=0", "quad_formula", 0 },
        { "x^2+4=0", "quad_negative", 0 },
        { "3(x+2)+4x", "simplify", 0 },
        { "x^3+2x", "derivative", 1 },
        { "3x^2+5x+7", "derivative2", 1 },
        { "1/4+1/6", "fractions_add", 0 },
        { "(1/2)/(3/4)", "fractions_div", 0 },
        { "x/2=1", "cannot", 0 },
        { "3x+", "cannot_syntax", 0 }
    };
    size_t i;
    int s, needclear = 0;
    idle(4);
    shot("00_title");
    tap(7, kb_Down); tap(7, kb_Down); tap(6, kb_Enter); idle(4); shot("01_help_types");
    tap(7, kb_Right); idle(2); shot("02_help_typing");
    tap(7, kb_Right); idle(2); shot("03_help_limits");
    tap(6, kb_Clear); idle(4);
    tap(7, kb_Up); tap(7, kb_Up); tap(6, kb_Enter); idle(4);
    shot("04_input_empty");
    for (i = 0; i < sizeof P / sizeof P[0]; i++) {
        cls_t cl;
        int type, solved = 0, n = 0;
        if (needclear) { tap(6, kb_Clear); idle(2); }
        typestr(P[i].prob);
        idle(2);
        shot(shot_name("%s_a_input%.0d", P[i].name, 0));
        if (classify(P[i].prob, &cl)) {
            type = P[i].choose ? PT_DERIV : cl.type;
            if (generate(P[i].prob, type)) { solved = 1; n = nsteps; }
        }
        tap(6, kb_Enter);
        idle(3);
        if (!solved) {
            shot(shot_name("%s_b_message%.0d", P[i].name, 0));
            tap(6, kb_Enter); idle(3);
            needclear = 1;
            continue;
        }
        if (cl.ambiguous) {
            shot(shot_name("%s_b_choose%.0d", P[i].name, 0));
            if (P[i].choose) tap(7, kb_Down);
            tap(6, kb_Enter); idle(3);
        }
        for (s = 0; s < n; s++) {
            shot(shot_name("%s_s%02d", P[i].name, s + 1));
            tap(6, kb_Enter);
            idle(2);
        }
        shot(shot_name("%s_z_end_menu%.0d", P[i].name, 0));
        tap(6, kb_Enter); idle(3);              /* "Try another problem": back to an empty input line */
        needclear = 0;
    }
    /* history */
    if (needclear) { tap(6, kb_Clear); idle(2); }
    tap(6, kb_Clear); idle(3);
    tap(7, kb_Down); tap(6, kb_Enter); idle(4);
    shot("history");
    tap(6, kb_Enter); idle(3);
    shot("history_replay_s1");
    tap(6, kb_Clear); idle(2); tap(6, kb_Clear); idle(2);
}

/* ---------------------------------------------------------------- */
/* main                                                             */
/* ---------------------------------------------------------------- */
int main(int argc, char **argv)
{
    const char *sc = argc > 1 ? argv[1] : "types";
    long n = argc > 2 ? atol(argv[2]) : 200;
    unsigned seed = argc > 3 ? (unsigned)atoi(argv[3]) : 1;
    if (getenv("VERBOSE")) verbose = 1;
    rs_ = seed ? seed : 1;
    logf_ = stdout;
    if (!strcmp(sc, "types")) {
        char name[200];
        const char *lf = getenv("LOG");
        if (lf) { logf_ = fopen(lf, "w"); if (!logf_) logf_ = stdout; }
        (void)name;
        run_fixed();
        run_random(n);
        printf("types: %ld checks, %ld failures\n", checks, fails);
        printf("  tested: %ld linear, %ld quadratic, %ld simplify, %ld derivative, %ld fraction problems\n", counts[0], counts[1], counts[2], counts[3], counts[4]);
        printf("  longest step text: %d lines (limit 7), longest state: %d lines (limit 5), truncated strings: %ld\n", max_text_lines, max_state_lines, trunc_n);
        return fails != 0 || trunc_n != 0;
    }
    if (!strcmp(sc, "garbage")) {
        logf_ = 0;
        run_garbage(n);
        printf("garbage: %ld checks, %ld failures, longest text %d lines, state %d lines, truncated %ld\n", checks, fails, max_text_lines, max_state_lines, trunc_n);
        return fails != 0 || trunc_n != 0;
    }
    if (!strcmp(sc, "fuzz")) {
        fuzz_mode = 1;
        fuzz_frames = n;
        logf_ = 0;
        stub_ghost_on = 0;
        while (fuzz_frames > 0) game_main();
        printf("fuzz: %ld frames shown, presents by screen:", frames);
        { int i; for (i = 0; i < 8; i++) printf(" %ld", present_count[i]); }
        printf("\ntruncated strings: %ld  text overflow: %ld  NoClip violations: %ld\n", trunc_n, text_overflow, violations);
        return trunc_n != 0 || text_overflow != 0 || violations != 0;
    }
    if (!strcmp(sc, "shots")) {
        shot_dir = argc > 2 ? argv[2] : ".";
        logf_ = 0;
        shots_script();
        {
            /* leave through the title menu */
            tap(6, kb_Clear); idle(2); tap(6, kb_Clear); idle(2); tap(6, kb_Clear); idle(2);
        }
        game_main();
        printf("shots: %d saved; text overflow %ld, NoClip %ld, truncated %ld\n", nshots, text_overflow, violations, trunc_n);
        return text_overflow != 0 || violations != 0 || trunc_n != 0;
    }
    fprintf(stderr, "unknown scenario\n");
    return 2;
}
