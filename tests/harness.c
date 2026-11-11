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
#include <execinfo.h>
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
void test_trunc(void)
{
    trunc_n++;
    if (trunc_n == 1 && getenv("TRUNCBT")) { void *bt[16]; int n = backtrace(bt, 16); backtrace_symbols_fd(bt, n, 2); }
}
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
    static const char *const frag[] = { "x^2", "3x", "2(", "x+", "=0", "/2", "(x+1)", "-(", "x(x", "5)", "1/2", "+(1/3)", "x^", "10", "2x^2",
                                     "sqrt(", "sqrt(4)", "2sqrt(3)", "sqrt(x)", "sqrt(2)+", ")/2", "(-3+", "sqrt(5^2-4*1*6)", "sqrt(sqrt(", "-sqrt(9)", "(1+sqrt(5))/2" };
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
                    if (t[q] == PT_RADICAL && res.nroots == 1) {
                        double want, got = ((double)res.root[0].u + (double)res.root[0].w * sqrt((double)res.root[0].m)) / (double)res.root[0].den;
                        qp = in;
                        want = q_expr();
                        q_sp();
                        CHECK(*qp != 0 || fabs(got - want) <= 1e-6 * (1 + fabs(want)), "%s: radical value %.9g vs %.9g", in, got, want);
                    }
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
/* Venn diagrams: reference tests                                   */
/* ---------------------------------------------------------------- */
/* The reference builds the problem from the regions (so the right answer is
 * known), feeds only the typed numbers to the game, and compares every region
 * and the expression count with what it knows. Its expressions come from a
 * generator that carries its own truth table. */
typedef struct { int t, l, r, v; } gx_t;          /* t: 0 set, 1 not, 2 or, 3 and */
static gx_t gx[64];
static int ngx;
static int gx_new(int t, int l, int r, int v) { gx[ngx].t = t; gx[ngx].l = l; gx[ngx].r = r; gx[ngx].v = v; return ngx++; }
static int gx_rand(int depth, int k)
{
    int c = depth <= 0 ? 0 : (int)(rr() % 6);
    if (c <= 1) return gx_new(0, -1, -1, (int)(rr() % (unsigned)k));
    if (c == 2) return gx_new(1, gx_rand(depth - 1, k), -1, 0);
    if (c <= 4) return gx_new(2, gx_rand(depth - 1, k), gx_rand(depth - 1, k), 0);
    return gx_new(3, gx_rand(depth - 1, k), gx_rand(depth - 1, k), 0);
}
static int gx_truth(int i, int mask)
{
    switch (gx[i].t) {
    case 0: return (mask >> gx[i].v) & 1;
    case 1: return !gx_truth(gx[i].l, mask);
    case 2: return gx_truth(gx[i].l, mask) || gx_truth(gx[i].r, mask);
    default: return gx_truth(gx[i].l, mask) && gx_truth(gx[i].r, mask);
    }
}
static int gx_prec(int i) { return gx[i].t == 2 ? 1 : gx[i].t == 3 ? 2 : 3; }
/* own printer: minimal brackets, sometimes written as n( ... ) */
static void gx_print(char *out, int i, int need)
{
    char a[200], b[200];
    int br = gx_prec(i) < need;
    switch (gx[i].t) {
    case 0: sprintf(out, "%c", 'A' + gx[i].v); return;
    case 1: gx_print(a, gx[i].l, 3); sprintf(out, "%s'", a); return;
    default:
        gx_print(a, gx[i].l, gx[i].t == 2 ? 1 : 2);
        gx_print(b, gx[i].r, gx[i].t == 2 ? 1 : 2);
        if (br) sprintf(out, "%s(%s%c%s)", (rr() % 3 == 0) ? "n" : "", a, gx[i].t == 2 ? 'u' : 'n', b);
        else sprintf(out, "%s%c%s", a, gx[i].t == 2 ? 'u' : 'n', b);
        return;
    }
}

static long venn_checks_2, venn_checks_3, venn_incons, venn_nested, venn_expr_ok;
static int venn_max_text, venn_max_state;

/* the typed numbers of a problem built from the region counts R */
static void venn_fields(int k, const long *R, int with_none)
{
    long U = 0, A = 0, B = 0, C = 0, AB = 0, AC = 0, BC = 0, ABC = 0;
    int m;
    for (m = 0; m < (1 << k); m++) {
        U += R[m];
        if (m & 1) A += R[m];
        if (m & 2) B += R[m];
        if (m & 4) C += R[m];
        if ((m & 3) == 3) AB += R[m];
        if ((m & 5) == 5) AC += R[m];
        if ((m & 6) == 6) BC += R[m];
        if (m == 7) ABC += R[m];
    }
    memset(vn_f, -1, sizeof vn_f);
    vn_f[VF_U] = U; vn_f[VF_A] = A; vn_f[VF_B] = B; vn_f[VF_AB] = AB;
    if (k == 3) { vn_f[VF_C] = C; vn_f[VF_AC] = AC; vn_f[VF_BC] = BC; vn_f[VF_ABC] = ABC; }
    vn_f[VF_NONE] = with_none ? R[0] : -1;
}

/* the number at the end of a state line ("12 - 5 = 7"), or -1 */
static long last_number(const char *s)
{
    const char *e = strrchr(s, '=');
    return e ? atol(e + 1) : -1;
}

/* every piece of a state line made only of numbers and + - (like "12 + 9 - 5") must have the same value as the last one */
static void check_num_chain(const char *state, const char *tag)
{
    char buf[400], *seg, *next;
    double last = 0;
    int have = 0, all;
    clean_nb(state, buf);
    for (seg = buf; seg; seg = next) {
        char *e = strstr(seg, " = ");
        int plain = 1;
        char *c;
        if (e) { *e = 0; next = e + 3; } else next = 0;
        for (c = seg; *c; c++) if (!strchr("0123456789+- ", *c)) plain = 0;
        if (!plain || !seg[0]) continue;
        {
            double v = q_eval(seg, &all);
            if (have) CHECK(fabs(v - last) < 1e-9, "%s: '%s' is not an equal chain (%g vs %g)", tag, state, v, last);
            last = v;
            have = 1;
        }
    }
}

static void venn_check_layout(const char *tag)
{
    int i;
    for (i = 0; i < nsteps; i++) {
        int lt = wrap_draw(steps[i].text, 0, 0, COLS, 100, 0, false);
        int ls = wrap_draw(steps[i].state, 0, 0, VCOLS, 100, 0, false);
        if (lt > venn_max_text) venn_max_text = lt;
        if (ls > venn_max_state) venn_max_state = ls;
        CHECK(lt <= 3, "%s: step %d text needs %d lines", tag, i + 1, lt);
        CHECK(ls <= 10, "%s: step %d state needs %d lines at 13 columns", tag, i + 1, ls);
        CHECK(strstr(steps[i].state, "= -") == 0 && strstr(steps[i].text, "= -") == 0, "%s: step %d shows a negative count", tag, i + 1);
        check_num_chain(steps[i].state, tag);
        CHECK((steps[i].fin != 0) == (i == nsteps - 1), "%s: final flag on step %d", tag, i + 1);
    }
    CHECK(vsum[0] == 0 || wrap_draw(vsum, 0, 0, COLS, 100, 0, false) <= 2, "%s: problem line too long", tag);
}

/* one consistent problem: regions R, an optional expression text with its truth table */
static void venn_case(int k, const long *R, int with_none, const char *expr, int tt_root, const char *tag)
{
    int m, nfill = k == 2 ? 4 : 8, i, want_steps;
    uint8_t seen = 0;
    long t0 = trunc_n;
    vn_k = (uint8_t)k;
    venn_fields(k, R, with_none);
    strncpy(vn_expr, expr ? expr : "", MAXIN);
    vn_expr[MAXIN] = 0;
    venn_make_summary();
    if (!solve_venn()) { FAIL("%s: refused a good problem: %s | %s", tag, vn_head ? vn_head : "?", vn_msg); return; }
    if (k == 2) venn_checks_2++; else venn_checks_3++;
    for (m = 0; m < (1 << k); m++) CHECK(vr[m] == R[m], "%s: region %d is %ld, wanted %ld", tag, m, vr[m], R[m]);
    CHECK(trunc_n == t0, "%s: truncated", tag);
    /* the fill steps: one region each, innermost first, the outside last */
    {
        static const int order2[4] = { 3, 1, 2, 0 };
        static const int order3[8] = { 7, 3, 5, 6, 1, 2, 4, 0 };
        const int *ord = k == 2 ? order2 : order3;
        for (i = 0; i < nfill && i < nsteps; i++) {
            uint8_t added = (uint8_t)(steps[i].vfill & ~seen);
            CHECK(added == (1 << ord[i]), "%s: step %d fills mask %02x, wanted region %d", tag, i + 1, added, ord[i]);
            CHECK(steps[i].vhl == (1 << ord[i]) || (ord[i] == 0 && steps[i].vhl == 1), "%s: step %d highlight %02x", tag, i + 1, steps[i].vhl);
            CHECK(last_number(steps[i].state) == R[ord[i]], "%s: step %d state '%s' vs %ld", tag, i + 1, steps[i].state, R[ord[i]]);
            seen = steps[i].vfill;
        }
    }
    if (expr && expr[0]) {
        long want = 0;
        int sroot;
        venn_expr_ok++;
        for (m = 0; m < (1 << k); m++) if ((tt_root >> m) & 1) want += R[m];
        CHECK(res.num == want, "%s: n(%s) = %ld, wanted %ld", tag, expr, res.num, want);
        CHECK(steps[nsteps - 1].fin, "%s: last step is not final", tag);
        CHECK(steps[nsteps - 1].vfill == (k == 2 ? 0x0F : 0xFF), "%s: diagram not full at the end", tag);
        /* the lit regions are exactly the truth table */
        { uint8_t tt = 0; for (m = 0; m < (1 << k); m++) if ((tt_root >> m) & 1) tt = (uint8_t)(tt | (1 << m));
          CHECK(steps[nsteps - 1].vhl == tt, "%s: lit regions %02x, wanted %02x", tag, steps[nsteps - 1].vhl, tt); }
        CHECK(last_number(steps[nsteps - 1].state) == want, "%s: final line '%s' vs %ld", tag, steps[nsteps - 1].state, want);
        /* the canonical text in the final step (spaces and all) reads back as the same set */
        {
            char canon[200], plain[200], *e;
            const char *t = strstr(steps[nsteps - 1].text, "n(");
            if (t) {
                int depth = 0, np = 0;
                clean_nb(t + 2, canon);
                for (e = canon; *e; e++) { if (*e == '(') depth++; else if (*e == ')') { if (depth == 0) break; depth--; } }
                *e = 0;
                for (e = canon; *e; e++) if (*e != ' ') plain[np++] = *e;
                plain[np] = 0;
                sroot = se_parse(plain);
                CHECK(sroot >= 0 && se_eval(sroot) == (uint8_t)tt_root, "%s: canonical text '%s' does not read back", tag, canon);
            }
        }
        for (i = 0; i < nsteps; i++) { (void)i; }
        want_steps = nfill + 1;
        CHECK(nsteps >= want_steps, "%s: %d steps", tag, nsteps);
    } else {
        CHECK(nsteps == nfill + 1 && steps[nsteps - 1].fin, "%s: %d steps without an expression", tag, nsteps);
    }
    venn_check_layout(tag);
    if (logf_) {
        fprintf(logf_, "=== VENN %d sets  %s  find %s\n", k, vsum, expr && expr[0] ? expr : "(nothing)");
        for (i = 0; i < nsteps; i++) {
            char t[300], s2[300];
            clean_nb(steps[i].text, t);
            clean_nb(steps[i].state, s2);
            fprintf(logf_, "  %s %d [fill %02x lit %02x]: %s\n         -> %s\n", steps[i].fin ? "FINAL" : "step ", i + 1, steps[i].vfill, steps[i].vhl, t, s2);
        }
    }
}

/* the numbers are given (typed), the game must refuse and say so */
static void venn_bad(int k, const long *f, const char *tag, const char *needle)
{
    int i;
    vn_k = (uint8_t)k;
    for (i = 0; i < VF_N; i++) vn_f[i] = f[i];
    vn_expr[0] = 0;
    venn_make_summary();
    CHECK(!solve_venn(), "%s: accepted inconsistent numbers", tag);
    CHECK(vn_head != 0 && vn_msg[0] != 0, "%s: no message", tag);
    if (needle) CHECK(strstr(vn_msg, needle) != 0, "%s: message '%s' lacks '%s'", tag, vn_msg, needle);
    CHECK(strstr(vn_msg, "= -") == 0, "%s: the message shows a negative count: %s", tag, vn_msg);
    venn_incons++;
    if (logf_) fprintf(logf_, "=== VENN %d sets INCONSISTENT %s\n   %s\n   %s\n", k, vsum, vn_head, vn_msg);
}

/* does a typed set of numbers describe real regions? (independent of the game) */
static int ref_consistent(int k, const long *f)
{
    long U = f[VF_U], A = f[VF_A], B = f[VF_B], C = f[VF_C], AB = f[VF_AB], AC = f[VF_AC], BC = f[VF_BC], ABC = f[VF_ABC], none = f[VF_NONE];
    long r[8], sum = 0;
    int m;
    if (k == 2) {
        r[3] = AB; r[1] = A - AB; r[2] = B - AB;
        sum = r[1] + r[2] + r[3];
    } else {
        r[7] = ABC; r[3] = AB - ABC; r[5] = AC - ABC; r[6] = BC - ABC;
        r[1] = A - r[3] - r[5] - r[7]; r[2] = B - r[3] - r[6] - r[7]; r[4] = C - r[5] - r[6] - r[7];
        for (m = 1; m < 8; m++) sum += r[m];
    }
    for (m = 1; m < (1 << k); m++) if (r[m] < 0) return 0;
    if (U - sum < 0) return 0;
    if (none >= 0 && none != U - sum) return 0;
    return 1;
}

static void run_venn_tests(long N)
{
    long i;
    char tag[80];
    /* hand-checked problems (the numbers in the comments were worked out by hand) */
    {
        /* 20 students, 12 like A, 9 like B, 5 both: A only 7, both 5, B only 4, neither 4 */
        long R[8] = { 4, 7, 4, 5 };
        venn_case(2, R, 0, 0, 0, "hand2a");
        venn_case(2, R, 1, "AuB", 0x0E, "hand2b");           /* n(A u B) = 7+5+4 = 16 */
        venn_case(2, R, 0, "n(AuB)'", 0x01, "hand2c");       /* 4 */
        venn_case(2, R, 0, "A'", 0x05, "hand2d");            /* 4 + 4 = 8 */
        venn_case(2, R, 0, "AnB'", 0x02, "hand2e");          /* 7 */
        venn_case(2, R, 0, "(AnB)'", 0x07, "hand2f");        /* 20 - 5 = 15 */
        CHECK(res.num == 15, "hand2f: n((A n B)') = %ld", res.num);
        venn_case(2, R, 0, "A'uB'", 0x07, "hand2g");         /* De Morgan: (A n B)' */
        { long R2[8] = { 0, 3, 0, 0 };  venn_case(2, R2, 1, "A", 0x0A, "hand2h"); }    /* a single set */
        { long R2[8] = { 0, 0, 0, 8 };  venn_case(2, R2, 0, "AnB", 0x08, "hand2i"); }  /* A = B */
        { long R2[8] = { 9, 0, 0, 0 };  venn_case(2, R2, 1, "AuB", 0x0E, "hand2j"); }  /* sets that are empty */
        { long R2[8] = { 1, 20, 14, 0 }; venn_case(2, R2, 0, "n(AnB)", 0x08, "hand2k"); }   /* no overlap */
    }
    {
        /* U 50, A 20, B 18, C 15, AB 8, AC 7, BC 6, ABC 3: middle 3, AB only 5, AC only 4, BC only 3,
           A only 20-5-4-3 = 8, B only 18-5-3-3 = 7, C only 15-4-3-3 = 5, outside 50-35 = 15 */
        long R[8] = { 15, 8, 7, 5, 5, 4, 3, 3 };
        venn_case(3, R, 0, 0, 0, "hand3a");
        CHECK(vr[0] == 15 && vr[7] == 3 && vr[1] == 8 && vr[2] == 7 && vr[4] == 5, "hand3a: regions");
        venn_case(3, R, 1, "AuBuC", 0xFE, "hand3b");                          /* 35 */
        CHECK(res.num == 35, "hand3b: %ld", res.num);
        venn_case(3, R, 0, "n(BnC)'", 0xFF & ~((1 << 6) | (1 << 7)), "hand3c"); /* 50 - 6 = 44 */
        CHECK(res.num == 44, "hand3c: %ld", res.num);
        venn_case(3, R, 0, "n(AuB)'nC", 0x10, "hand3d");                     /* only C: 5 */
        CHECK(res.num == 5, "hand3d: %ld", res.num);
        /* n((A n B) u C): regions 3,7 and 4,5,6,7 = 5+3+5+4+3 = 20 */
        venn_case(3, R, 0, "n((AnB)uC)", (1 << 3) | (1 << 7) | (1 << 4) | (1 << 5) | (1 << 6), "hand3e");
        CHECK(res.num == 20, "hand3e: %ld", res.num);
        venn_case(3, R, 0, "(AuB)'uC'", 0x1F, "hand3f");                /* masks 0,1,2,3,4 = 15+8+7+5+5 = 40 */
        CHECK(res.num == 40, "hand3f: %ld", res.num);
        venn_case(3, R, 0, "AnBnC", 0x80, "hand3g");                          /* 3 */
        venn_case(3, R, 0, "AnB'nC'", 0x02, "hand3h");                        /* A only 8 */
        venn_case(3, R, 0, "n(AuB)", 0xEE, "hand3i");                          /* masks 1,2,3,5,6,7 = 8+7+5+4+3+3 = 30 */
        CHECK(res.num == 30, "hand3i: %ld", res.num);
    }
    /* inconsistent numbers */
    {
        long f[VF_N];
        int j;
        for (j = 0; j < VF_N; j++) f[j] = -1;
        f[VF_U] = 20; f[VF_A] = 12; f[VF_B] = 9; f[VF_AB] = 10;                    /* overlap bigger than B */
        venn_bad(2, f, "bad2a", "cannot be bigger");
        f[VF_U] = 15; f[VF_A] = 12; f[VF_B] = 9; f[VF_AB] = 5;                     /* the circles hold 16 but U is 15 */
        venn_bad(2, f, "bad2b", "n(U) must be at least 16");
        f[VF_U] = 20; f[VF_NONE] = 5;                                              /* outside is really 4 */
        f[VF_U] = 20; f[VF_A] = 12; f[VF_B] = 9; f[VF_AB] = 5; f[VF_NONE] = 5;
        venn_bad(2, f, "bad2c", "do not add up");
        for (j = 0; j < VF_N; j++) f[j] = -1;
        f[VF_U] = 50; f[VF_A] = 20; f[VF_B] = 18; f[VF_C] = 15; f[VF_AB] = 2; f[VF_AC] = 7; f[VF_BC] = 6; f[VF_ABC] = 3;   /* AB < ABC */
        venn_bad(3, f, "bad3a", "hold the middle");
        f[VF_AB] = 8; f[VF_A] = 10;                                                  /* A too small for its overlaps */
        venn_bad(3, f, "bad3b", "too small");
        f[VF_A] = 20; f[VF_U] = 30;                                                  /* U smaller than the circles */
        venn_bad(3, f, "bad3c", "n(U) must be at least");
        f[VF_U] = 50; f[VF_NONE] = 14;                                               /* outside should be 15 */
        venn_bad(3, f, "bad3d", "do not add up");
        f[VF_NONE] = -1; f[VF_B] = -1;                                                /* a blank */
        venn_bad(3, f, "bad3e", "Please fill in n(B)");
    }
    /* many random problems, with random expressions that carry their own truth tables */
    for (i = 0; i < N; i++) {
        int k = (i & 1) ? 3 : 2, m, with_none = (int)(rr() & 1), nested = 0;
        long R[8];
        char expr[120];
        int g, tt = 0;
        memset(R, 0, sizeof R);
        for (m = 0; m < (1 << k); m++) R[m] = (rr() % 4 == 0) ? 0 : (long)(rr() % 60);
        if (rr() % 8) {
            ngx = 0;
            g = gx_rand(1 + (int)(rr() % 3), k);
            gx_print(expr, g, 0);
            for (m = 0; m < (1 << k); m++) if (gx_truth(g, m)) tt |= 1 << m;
            nested = (gx[g].t != 0 && ((gx[gx[g].l].t > 0) || (gx[g].r >= 0 && gx[gx[g].r].t > 0)));
            if (strlen(expr) > MAXIN) { expr[0] = 0; tt = 0; nested = 0; }
        } else {
            expr[0] = 0;
        }
        if (nested) venn_nested++;
        sprintf(tag, "random%ld", i);
        if (expr[0] && (rr() & 1)) {                    /* sometimes wrapped as n( ... ) */
            char w[160];
            if (strlen(expr) + 3 <= MAXIN) { sprintf(w, "n(%s)", expr); strcpy(expr, w); }
        }
        venn_case(k, R, with_none, expr, tt, tag);
        /* now perturb one typed number: the game must agree with the independent consistency check */
        {
            long f[VF_N];
            int j, pick;
            venn_fields(k, R, with_none);
            for (j = 0; j < VF_N; j++) f[j] = vn_f[j];
            pick = (int)(rr() % VF_N);
            if (f[pick] >= 0) {
                f[pick] += (long)(rr() % 7) - 3;
                if (f[pick] < 0) f[pick] = 0;
                vn_k = (uint8_t)k;
                for (j = 0; j < VF_N; j++) vn_f[j] = f[j];
                vn_expr[0] = 0;
                venn_make_summary();
                if (ref_consistent(k, f)) {
                    CHECK(solve_venn(), "%s: perturbed but consistent numbers refused: %s", tag, vn_msg);
                } else {
                    CHECK(!solve_venn(), "%s: inconsistent numbers accepted: %s", tag, vsum);
                    if (vn_msg[0]) CHECK(vn_head != 0, "%s: message without heading", tag);
                    venn_incons++;
                }
            }
        }
    }
    /* expression mistakes: refused with a position */
    {
        static const char *const badx[] = { "A u", "(AuB", "AuB)", "AxB", "D", "n(", "AnuB", "u A", "A''' n", "n()", "((((((((((A))))))))))", "AuBuC", "C" };
        size_t b;
        long R[8] = { 4, 7, 4, 5 };
        for (b = 0; b < sizeof badx / sizeof badx[0]; b++) {
            vn_k = 2;
            venn_fields(2, R, 0);
            strncpy(vn_expr, badx[b], MAXIN);
            vn_expr[MAXIN] = 0;
            venn_make_summary();
            if (solve_venn()) {
                /* a few of these are valid; the rest must say why */
                FAIL("expression '%s' was accepted", badx[b]);
            } else {
                CHECK(vn_head != 0 && vn_msg[0] && vn_at >= 0 && vn_at <= (int)strlen(badx[b]), "'%s': message %s at %d", badx[b], vn_msg, vn_at);
                if (logf_) fprintf(logf_, "=== VENN expression '%s' refused: %s (at %d)\n", badx[b], vn_msg, vn_at);
            }
        }
    }
    printf("venn: %ld 2-set and %ld 3-set problems solved and checked, %ld inconsistent number sets refused, %ld expressions (%ld nested)\n",
           venn_checks_2, venn_checks_3, venn_incons, venn_expr_ok, venn_nested);
    printf("  longest Venn step text: %d lines (limit 3), longest calculation: %d lines of 13 columns (limit 10)\n", venn_max_text, venn_max_state);
}

/* random Venn numbers (blank, huge, inconsistent) and random set-expression strings: never a crash,
   a negative count or a wrong answer */
static void run_venn_garbage(long N)
{
    static const char alpha[] = "ABCABCunun\x27\x27\x27()()xDn ";
    long i, ok = 0, refused = 0;
    for (i = 0; i < N; i++) {
        int k = (rr() & 1) ? 3 : 2, j, len = (int)rnd_range(0, 30);
        long t0 = trunc_n;
        vn_k = (uint8_t)k;
        for (j = 0; j < VF_N; j++) {
            unsigned c = rr() % 10;
            vn_f[j] = c == 0 ? -1 : c == 1 ? (long)(rr() % 1500) : (long)(rr() % 120);
        }
        for (j = 0; j < len; j++) vn_expr[j] = alpha[rr() % (sizeof alpha - 1)];
        vn_expr[len] = 0;
        if (rr() % 3 == 0) vn_expr[0] = 0;
        venn_make_summary();
        if (solve_venn()) {
            int m;
            long tot = 0;
            ok++;
            CHECK(nsteps >= 1 && steps[nsteps - 1].fin, "venn garbage: no final step");
            venn_check_layout("garbage");
            for (m = 0; m < (1 << k); m++) { CHECK(vr[m] >= 0, "negative region"); tot += vr[m]; }
            CHECK(tot == vn_f[VF_U], "regions add up to %ld, not n(U) = %ld", tot, vn_f[VF_U]);
        } else {
            refused++;
            CHECK(vn_head != 0, "venn garbage: refusal without a heading");
            CHECK(strstr(vn_msg, "= -") == 0, "venn garbage: negative count in the message: %s", vn_msg);
            CHECK(vn_at < 0 || vn_at <= (int)strlen(vn_expr), "venn garbage: error position %d", vn_at);
        }
        CHECK(trunc_n == t0, "venn garbage: truncated string");
    }
    printf("venn garbage: %ld random problems: %ld solved, %ld refused with a message\n", N, ok, refused);
}

/* ---------------------------------------------------------------- */
/* square roots typed in: the quadratic formula                     */
/* ---------------------------------------------------------------- */
static long rad_n;

static double rad_value(const root_t *r)
{
    return ((double)r->u + (double)r->w * sqrt((double)r->m)) / (double)r->den;
}

static void rad_case(const char *in, int expect_ok, const char *tag)
{
    cls_t cl;
    int i, ok;
    double want;
    long t0 = trunc_n;
    int all = 0;
    qp = in;
    want = q_expr();
    q_sp();
    ok = classify(in, &cl);
    CHECK(ok && cl.type == PT_RADICAL, "%s: '%s' classified as %d (%s)", tag, in, cl.type, cl.why ? cl.why : "");
    if (!ok) return;
    if (!generate(in, PT_RADICAL)) {
        CHECK(!expect_ok, "%s: '%s' refused: %s", tag, in, g_fail ? g_fail : "?");
        CHECK(g_fail != 0, "%s: refusal without a reason", tag);
        if (logf_) fprintf(logf_, "=== %s  REFUSED: %s\n", in, g_fail ? g_fail : "?");
        return;
    }
    CHECK(expect_ok, "%s: '%s' accepted but a refusal was expected", tag, in);
    rad_n++;
    check_layout(in);
    CHECK(trunc_n == t0, "%s: truncated", in);
    {
        double got = rad_value(&res.root[0]);
        CHECK(fabs(got - want) <= 1e-7 * (1 + fabs(want)), "%s: '%s' = %.9g, wanted %.9g", tag, in, got, want);
        CHECK(res.root[0].den > 0 && gcdll(gcdll(res.root[0].u, res.root[0].w), res.root[0].den) == 1, "%s: value not in lowest terms", in);
    }
    /* every step's expression still equals the original */
    for (i = 0; i < nsteps; i++) {
        char buf[300];
        const char *p;
        double v;
        if (steps[i].eqv != EQ_EXPR && !steps[i].fin) continue;
        clean_nb(steps[i].state, buf);
        p = buf;
        if (p[0] == '=' && p[1] == ' ') p += 2;
        {
            char *par = strstr((char *)p, "   (about");
            if (par) *par = 0;
        }
        v = q_eval(p, &all);
        CHECK(all && fabs(v - want) <= 1e-7 * (1 + fabs(want)), "%s: step %d '%s' = %.9g, wanted %.9g", in, i + 1, buf, v, want);
    }
    if (logf_) print_steps(in, "square roots");
}

static void run_radical_tests(long N)
{
    static const char *const good[] = {
        "sqrt(25)", "sqrt(16)+3", "sqrt(48)", "2sqrt(3)*3", "sqrt(2)*sqrt(2)", "sqrt(2)+sqrt(2)", "3sqrt(5)-sqrt(5)", "(4+2sqrt(3))/2", "(-3+sqrt(17))/4",
        "(-5+sqrt(5^2-4*1*6))/(2*1)", "(-5-sqrt(5^2-4*1*6))/(2*1)", "(-1+sqrt(1-4*1*(-1)))/(2*1)", "(3+sqrt(9-4*2*1))/(2*2)", "1/sqrt(2)", "(1+sqrt(5))/2",
        "sqrt(1)", "sqrt(0)", "2*(3+sqrt(4))", "sqrt(3^2+4^2)", "-sqrt(9)", "10/sqrt(5)", "(2+sqrt(8))/2", "sqrt(2)*3sqrt(2)", "(-(-4)+sqrt(16-4*1*3))/2",
        "sqrt(12)/sqrt(3)", "5-sqrt(16)", "sqrt(50)-sqrt(2)", "(sqrt(3)+1)(sqrt(3)-1)", "sqrt(7)^2", "(1+sqrt(2))^2", "sqrt(9999)", "sqrt(100)+sqrt(81)", "(6+sqrt(36-4*3*(-2)))/(2*3)"
    };
    static const char *const bad[] = {
        "sqrt(-4)", "sqrt(2)+sqrt(3)", "sqrt(1/2)", "sqrt(sqrt(2))", "(1+sqrt(3))/(1-sqrt(1))", "1/(sqrt(4)-2)", "sqrt(2)*sqrt(3)", "5/(3-3)+sqrt(4)",
        "9999*9999*9999*sqrt(4)", "sqrt(9999)^12"
    };
    size_t g;
    long i;
    for (g = 0; g < sizeof good / sizeof good[0]; g++) rad_case(good[g], 1, "good");
    for (g = 0; g < sizeof bad / sizeof bad[0]; g++) rad_case(bad[g], 0, "bad");
    /* the quadratic formula with random a, b, c typed the way a student would */
    for (i = 0; i < N; i++) {
        long a = rnd_range(1, 9), b = rnd_range(-30, 30), c = rnd_range(-30, 30), D;
        char in[200], A[16], B[16], C[16];
        int sign = (int)(rr() & 1);
        if (rr() & 1) a = -a;
        D = b * b - 4 * a * c;
        sprintf(A, a < 0 ? "(%ld)" : "%ld", a);
        sprintf(B, b < 0 ? "(%ld)" : "%ld", b);
        sprintf(C, c < 0 ? "(%ld)" : "%ld", c);
        sprintf(in, "(%s%ssqrt(%s^2-4*%s*%s))/(2*%s)", b == 0 ? "0" : (b < 0 ? "-(" : "-"), sign ? "+" : "-", B, A, C, A);
        if (b < 0) sprintf(in, "(-(%ld)%ssqrt(%s^2-4*%s*%s))/(2*%s)", b, sign ? "+" : "-", B, A, C, A);
        else if (b > 0) sprintf(in, "(-%ld%ssqrt(%s^2-4*%s*%s))/(2*%s)", b, sign ? "+" : "-", B, A, C, A);
        else sprintf(in, "(0%ssqrt(%s^2-4*%s*%s))/(2*%s)", sign ? "+" : "-", B, A, C, A);
        rad_case(in, D >= 0, "formula");
    }
    printf("radical: %ld expressions with sqrt( solved and checked against an independent evaluator\n", rad_n);
}

/* sqrt( inside an equation (only perfect squares) */
static void run_sqrt_equations(void)
{
    cls_t cl;
    CHECK(classify("x^2+sqrt(25)x+6=0", &cl) && cl.type == PT_QUAD, "sqrt in a quadratic");
    CHECK(generate("x^2+sqrt(25)x+6=0", PT_QUAD) && res.nroots == 2 && res.root[0].u == -3 && res.root[1].u == -2, "x^2+5x+6=0 roots");
    CHECK(classify("2x+sqrt(16)=sqrt(100)", &cl) && cl.type == PT_LINEAR, "sqrt in a linear equation");
    CHECK(generate("2x+sqrt(16)=sqrt(100)", PT_LINEAR) && res.kind == RK_ONE && res.num == 3 && res.den == 1, "2x+4=10");
    CHECK(!classify("x^2=sqrt(17)", &cl) && cl.why && strstr(cl.why, "perfect"), "non perfect square with x: %s", cl.why ? cl.why : "?");
    CHECK(!classify("sqrt(x)+1", &cl) && cl.why, "sqrt of x refused");
    CHECK(!classify("x+sqrt(-4)=0", &cl) && cl.why, "negative root with x refused");
    CHECK(classify("3sqrt(4)x+2", &cl) && cl.type == PT_SIMPLIFY, "implicit multiplication with sqrt");
    {
        char c[100];
        int ok = generate("3sqrt(4)x+2", PT_SIMPLIFY);
        clean_nb(steps[nsteps - 1].state, c);
        CHECK(ok && strcmp(c, "6x + 2") == 0, "3sqrt(4)x+2 -> 6x + 2 (%s)", c);
    }
    CHECK(!classify("sqrt(4", &cl) && cl.at >= 0, "missing bracket after sqrt(");
    CHECK(!classify("sqr(4)", &cl) && cl.at >= 0, "misspelt sqrt");
    printf("sqrt in equations: checked\n");
}

/* ---------------------------------------------------------------- */
/* DEL: the panic key                                               */
/* ---------------------------------------------------------------- */
static uint8_t ph_kg[64], ph_km[64];
static long ph_subsets, ph_phantoms, ph_bad;
static void ph_check(const int *idx, int cnt)
{
    uint8_t kb[8];
    int c;
    memset(kb, 0, 8);
    for (c = 0; c < cnt; c++) kb[ph_kg[idx[c]]] |= ph_km[idx[c]];
    ph_subsets++;
    stub_ghost(kb);
    memset(keys, 0, sizeof keys);
    for (c = 1; c < 8; c++) keys[c] = kb[c];
    if (kb[1] & kb_Del) {
        ph_phantoms++;
        if (del_hit()) { ph_bad++; if (ph_bad < 5) printf("PHANTOM DEL accepted: %02x %02x %02x %02x %02x %02x %02x\n", kb[1], kb[2], kb[3], kb[4], kb[5], kb[6], kb[7]); }
    }
}

static void run_phantom_test(void)
{
    /* every real key of the keypad (see the table in keypadc.h), except DEL itself */
    static const uint8_t cnt[8] = { 0, 8, 7, 8, 8, 7, 7, 4 };
    static const uint8_t start[8] = { 0, 0, 1, 0, 0, 0, 0, 0 };
    int n = 0, g, b, i, j, k, l, idx[4];
    long alone_ok = 0;
    for (g = 1; g < 8; g++) for (b = start[g]; b < start[g] + cnt[g]; b++) {
        if (g == 1 && b == 7) continue;
        ph_kg[n] = (uint8_t)g;
        ph_km[n] = (uint8_t)(1 << b);
        n++;
    }
    /* all sets of 1 to 4 keys other than DEL: a ghosted DEL must never be accepted */
    for (i = 0; i < n; i++) {
        idx[0] = i; ph_check(idx, 1);
        for (j = i + 1; j < n; j++) {
            idx[1] = j; ph_check(idx, 2);
            for (k = j + 1; k < n; k++) {
                idx[2] = k; ph_check(idx, 3);
                for (l = k + 1; l < n; l++) { idx[3] = l; ph_check(idx, 4); }
            }
        }
    }
    CHECK(ph_bad == 0, "%ld phantom DEL presses were accepted", ph_bad);
    /* a real DEL alone, or with one other key, always works (even through ghosting) */
    for (i = -1; i < n; i++) {
        uint8_t kb[8];
        memset(kb, 0, 8);
        kb[1] |= kb_Del;
        if (i >= 0) kb[ph_kg[i]] |= ph_km[i];
        stub_ghost(kb);
        memset(keys, 0, sizeof keys);
        for (g = 1; g < 8; g++) keys[g] = kb[g];
        CHECK(del_hit(), "a real DEL with %s was not accepted", i < 0 ? "no other key" : "one other key");
        alone_ok++;
    }
    printf("DEL: %ld key combinations (1 to 4 keys) through the ghosting model: %ld read as DEL without DEL being pressed, %ld of them accepted; a real DEL alone or with 1 other key worked in %ld of %d cases\n",
           ph_subsets, ph_phantoms, ph_bad, alone_ok, n + 1);
}

/* ---------------------------------------------------------------- */
/* fake keypad                                                      */
/* ---------------------------------------------------------------- */
typedef struct { uint8_t g, m; int shot; void (*fn)(void); } tap_t;
static tap_t q[8192];
static int qh, qt, tap_release;
static void tap(uint8_t g, uint8_t m) { q[qt & 8191].g = g; q[qt & 8191].m = m; q[qt & 8191].shot = -1; q[qt & 8191].fn = 0; qt++; }
/* run f when the game reaches this point of the script (between two keys) */
static void act(void (*f)(void)) { q[qt & 8191].g = 0; q[qt & 8191].m = 0; q[qt & 8191].shot = -1; q[qt & 8191].fn = f; qt++; }
static long watch_limit;
static int fuzz_mode, fuzz_left, del_intended;
static long fuzz_frames;
static const char *shot_names[1024];
static int nshots;
static const char *shot_dir;
static void shot(const char *name)
{
    q[qt & 8191].g = 0; q[qt & 8191].m = 0; q[qt & 8191].fn = 0; q[qt & 8191].shot = nshots;
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

/* ---------------------------------------------------------------- */
/* UI scripts: typing sqrt(, the 2nd indicator, Venn forms, DEL      */
/* ---------------------------------------------------------------- */
/* types a string the way a student would: sqrt( is 2nd then the x^2 key, set letters use their keys */
static void typeall(const char *s)
{
    while (*s) {
        int i, found = 0;
        if (!strncmp(s, "sqrt(", 5)) { tap(1, kb_2nd); tap(2, kb_Square); s += 5; continue; }
        if (*s == '^') { tap(6, kb_Power); s++; continue; }
        for (i = 0; i < NKM && !found; i++)
            if (KM[i].s[0] == *s && KM[i].s[1] == 0 && !(KM[i].g == 5 && KM[i].m == kb_Chs)) { tap(KM[i].g, KM[i].m); found = 1; }
        for (i = 0; i < NVK && !found; i++)
            if (VK[i].s[0] == *s) { tap(VK[i].g, VK[i].m); found = 1; }
        s++;
    }
}

static void to_title_top(void)
{
    int i;
    for (i = 0; i < 7; i++) tap(7, kb_Up);
}

static void pick_title(int item)
{
    int i;
    to_title_top();
    for (i = 0; i < item; i++) tap(7, kb_Down);
    tap(6, kb_Enter);
    idle(2);
}

/* the numbers of a Venn form, one per field (an empty one stays blank), then the expression */
static void venn_type(const char *nums, const char *expr)
{
    const char *p = nums;
    for (;;) {
        const char *e = strchr(p, ',');
        size_t n = e ? (size_t)(e - p) : strlen(p), i;
        for (i = 0; i < n; i++) typeall((char[]){ p[i], 0 });
        tap(6, kb_Enter);
        if (!e) break;
        p = e + 1;
    }
    if (expr && expr[0]) typeall(expr);
}

/* how many steps the game will make for these typed numbers (used to know how often to press ENTER) */
static int venn_count(int k, const char *nums, const char *expr)
{
    static const uint8_t r2[5] = { VF_U, VF_A, VF_B, VF_AB, VF_NONE };
    static const uint8_t r3[9] = { VF_U, VF_A, VF_B, VF_C, VF_AB, VF_AC, VF_BC, VF_ABC, VF_NONE };
    const uint8_t *rows = k == 2 ? r2 : r3;
    int i = 0;
    const char *p = nums;
    memset(vn_f, -1, sizeof vn_f);
    for (;;) {
        const char *e = strchr(p, ',');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n > 0) vn_f[rows[i]] = atol(p);
        i++;
        if (!e) break;
        p = e + 1;
    }
    vn_k = (uint8_t)k;
    strncpy(vn_expr, expr, MAXIN);
    vn_expr[MAXIN] = 0;
    venn_make_summary();
    return solve_venn() ? nsteps : 0;
}

static void act_second_on(void) { CHECK(second, "the 2nd indicator is not lit after pressing 2nd"); }
static void act_second_off(void) { CHECK(!second, "the 2nd indicator is still lit after the next key"); }
static void act_prob_is_sqrt(void) { CHECK(strcmp(prob, "sqrt(") == 0, "2nd then x^2 typed '%s' instead of sqrt(", prob); }
static void act_prob_is_sq(void) { CHECK(strcmp(prob, "sqrt(^2") == 0, "x^2 without 2nd typed '%s' (after sqrt( )", prob); }
static void act_prob_empty(void) { CHECK(prob[0] == 0, "prob is '%s'", prob); }

static void run_second_key_test(void)
{
    /* 2nd lights up, x^2 then types sqrt(, the indicator goes off; x^2 alone types ^2;
       2nd twice cancels; any other key switches it off without typing anything special */
    to_title_top();
    tap(6, kb_Enter);
    idle(3);
    act(act_prob_empty);
    tap(1, kb_2nd); idle(2);
    act(act_second_on);
    tap(2, kb_Square); idle(2);
    act(act_second_off);
    act(act_prob_is_sqrt);
    tap(2, kb_Square); idle(2);
    act(act_prob_is_sq);
    tap(1, kb_2nd); idle(2); act(act_second_on);
    tap(1, kb_2nd); idle(2); act(act_second_off);              /* pressed twice: off again */
    tap(1, kb_2nd); idle(2); act(act_second_on);
    tap(7, kb_Left); idle(2); act(act_second_off);             /* some other key: off, nothing typed */
    tap(1, kb_Mode); idle(2); act(act_prob_empty);              /* MODE clears the line */
    tap(6, kb_Clear); idle(2);                                  /* CLEAR on an empty line: back to the menu */
    tap(6, kb_Clear);                                           /* CLEAR on the title: quit */
    game_main();
}

static const char *e2e_prob;
static void act_expect_prob(void) { CHECK(strcmp(prob, e2e_prob) == 0, "typed '%s', the line shows '%s'", e2e_prob, prob); }

/* a problem typed with the real keys, solved, stepped through to the end */
static void ui_solve(const char *typed, int steps_to_press)
{
    int i;
    e2e_prob = typed;
    pick_title(0);
    typeall(typed);
    idle(2);
    act(act_expect_prob);
    tap(6, kb_Enter);
    idle(3);
    for (i = 0; i < steps_to_press; i++) { tap(6, kb_Enter); idle(1); }
    idle(2);
}

static void run_sqrt_ui_test(void)
{
    /* the quadratic formula for x^2+5x+6=0, typed: root x = (-5 + 1)/2 = -2 */
    const char *f = "(-5+sqrt(5^2-4*1*6))/(2*1)";
    cls_t cl;
    int n;
    CHECK(classify(f, &cl) && cl.type == PT_RADICAL, "formula classified");
    CHECK(generate(f, PT_RADICAL), "formula generate");
    n = nsteps;
    ui_solve(f, n - 1);                         /* press Enter on every step but the last */
    tap(6, kb_Enter); idle(3);                  /* past the end: the "Done!" menu */
    tap(7, kb_Down); tap(7, kb_Down); tap(6, kb_Enter); idle(3);   /* back to the main menu */
    tap(6, kb_Clear);                           /* quit */
    game_main();
    CHECK(ptype == PT_RADICAL, "the problem was solved as type %d", ptype);
    CHECK(res.nroots == 1 && res.root[0].u == -2 && res.root[0].w == 0 && res.root[0].den == 1, "formula root %ld/%ld", res.root[0].u, res.root[0].den);
    CHECK(strcmp(steps[nsteps - 1].state, "= -2") == 0, "final line '%s'", steps[nsteps - 1].state);

    /* an irrational root: x = (-3 + sqrt(17))/4 for 2x^2+3x-1=0, typed with the (-) key and brackets */
    f = "(-3+sqrt(3^2-4*2*(-1)))/(2*2)";
    CHECK(classify(f, &cl) && cl.type == PT_RADICAL, "formula 2 classified");
    CHECK(generate(f, PT_RADICAL), "formula 2 generate");
    n = nsteps;
    {
        /* with the (-) key for the minus sign in front of the 1 */
        int i;
        e2e_prob = f;
        pick_title(0);
        for (i = 0; f[i]; ) {
            if (f[i] == '(' && f[i + 1] == '-' && f[i + 2] == '1') { tap(4, kb_LParen); tap(5, kb_Chs); tap(3, kb_1); i += 3; continue; }
            if (!strncmp(f + i, "sqrt(", 5)) { tap(1, kb_2nd); tap(2, kb_Square); i += 5; continue; }
            { char one[2] = { f[i], 0 }; typeall(one); }
            i++;
        }
        idle(2);
        act(act_expect_prob);
        tap(6, kb_Enter); idle(3);
        { int k; for (k = 0; k < n - 1; k++) { tap(6, kb_Enter); idle(1); } }
        tap(6, kb_Enter); idle(3);
        tap(7, kb_Down); tap(7, kb_Down); tap(6, kb_Enter); idle(3);
        tap(6, kb_Clear);
        game_main();
    }
    CHECK(res.root[0].u == -3 && res.root[0].w == 1 && res.root[0].m == 17 && res.root[0].den == 4, "irrational root (%ld + %ld sqrt(%ld))/%ld", res.root[0].u, res.root[0].w, res.root[0].m, res.root[0].den);
    {
        char c[300];
        clean_nb(steps[nsteps - 1].state, c);
        CHECK(strstr(c, "(-3 + sqrt(17))/4") != 0 && strstr(c, "about 0.28") != 0, "final line '%s'", c);
    }

    /* sqrt( in a quadratic equation typed with the keys: x^2+sqrt(25)x+6=0 */
    ui_solve("x^2+sqrt(25)x+6=0", 0);
    {
        int k;
        CHECK(classify("x^2+sqrt(25)x+6=0", &cl) && generate("x^2+sqrt(25)x+6=0", PT_QUAD), "quadratic with sqrt generate");
        for (k = 0; k < nsteps; k++) { tap(6, kb_Enter); idle(1); }
    }
    tap(7, kb_Down); tap(7, kb_Down); tap(6, kb_Enter); idle(3);
    tap(6, kb_Clear);
    game_main();
    CHECK(ptype == PT_QUAD && res.nroots == 2 && res.root[0].u == -3 && res.root[1].u == -2, "x^2+sqrt(25)x+6=0 solved through the UI");
    printf("sqrt( typed with 2nd + x^2 and solved through the UI: quadratic formula expressions and an equation: checked\n");
}

/* the Venn form, driven with keys, to the final step */
static void run_venn_ui_test(void)
{
    int n;
    /* 2 sets: n(U)=20 n(A)=12 n(B)=9 n(A n B)=5 and the expression n(A u B) */
    n = venn_count(2, "20,12,9,5,", "n(AuB)");
    CHECK(n == 5, "2-set steps %d", n);
    pick_title(1);
    venn_type("20,12,9,5,", "n(AuB)");
    tap(1, kb_Graph);                         /* GRAPH solves from anywhere in the form */
    idle(4);
    { int k; for (k = 0; k < n; k++) { tap(6, kb_Enter); idle(1); } }       /* every step, then the end menu */
    tap(7, kb_Down); tap(7, kb_Down); tap(6, kb_Enter); idle(3);
    tap(6, kb_Clear);
    game_main();
    CHECK(ptype == PT_VENN && vn_k == 2, "Venn 2 sets was not solved through the UI (type %d)", ptype);
    CHECK(vr[0] == 4 && vr[1] == 7 && vr[2] == 4 && vr[3] == 5, "2-set regions %ld %ld %ld %ld", vr[0], vr[1], vr[2], vr[3]);
    {
        char c[300];
        clean_nb(steps[nsteps - 1].state, c);
        CHECK(strcmp(c, "n(A u B) = n(A) + n(B) - n(A n B) = 12 + 9 - 5 = 16") == 0, "2-set final line: %s", c);
    }

    /* 3 sets with a nested expression, then an inconsistent one that must give a message */
    n = venn_count(3, "50,20,18,15,8,7,6,3,", "n(AuB)'nC");
    CHECK(n >= 9, "3-set steps %d", n);
    pick_title(2);
    venn_type("50,20,18,15,8,7,6,3,", "n(AuB)'nC");
    tap(6, kb_Enter);                          /* ENTER on the expression row solves too */
    idle(4);
    { int k; for (k = 0; k < n; k++) { tap(6, kb_Enter); idle(1); } }
    tap(7, kb_Down); tap(7, kb_Down); tap(6, kb_Enter); idle(3);
    tap(6, kb_Clear);
    game_main();
    CHECK(ptype == PT_VENN && vn_k == 3 && vr[0] == 15 && vr[7] == 3 && vr[4] == 5, "3-set regions through the UI");
    CHECK(res.num == 5, "n(AuB)'nC = %ld, wanted 5", res.num);

    pick_title(1);
    venn_type("20,12,9,10,", "");               /* the overlap 10 is bigger than n(B) = 9 */
    tap(1, kb_Graph); idle(4);                  /* the message screen */
    tap(6, kb_Enter); idle(3);                  /* back to the form */
    tap(1, kb_Mode); idle(2);                   /* MODE: back to the menu */
    tap(6, kb_Clear);
    game_main();
    CHECK(vn_head != 0 && strstr(vn_msg, "cannot be bigger") != 0, "inconsistent numbers gave '%s'", vn_msg);
    printf("Venn forms typed with the real keys and solved through the UI: 2 sets, 3 sets with a nested expression, an inconsistent problem: checked\n");
}

static long del_runs, del_fails;
static long del_writes_at_quit;
static int del_screen = -1, del_ptype = -1;
static long del_quits_total;
static int del_intended_hook(void) { int ok = !fuzz_mode || del_intended; del_intended = 0; return ok; }
void test_del_quit(void)
{
    del_writes_at_quit = stub_writes;
    del_screen = cur_present_screen;
    del_ptype = ptype;
    del_quits_total++;
    if (!del_intended_hook()) { FAIL("the program quit on a DEL that was not pressed (a ghost key?)"); }
}

static void del_try(const char *name, void (*setup)(void), int want_screen, int want_ptype)
{
    long f0;
    setup();
    tap(1, kb_Del);
    f0 = frames;
    watch_limit = frames + 5000;
    stub_gfx_ended = 0;
    del_writes_at_quit = -1;
    game_main();
    watch_limit = 0;
    del_runs++;
    CHECK(stub_gfx_ended, "%s: DEL did not reach gfx_End()", name);
    CHECK(qh == qt, "%s: DEL was not the last key consumed", name);
    CHECK(del_writes_at_quit >= 0 && stub_writes == del_writes_at_quit, "%s: something was saved after DEL (%ld writes)", name, stub_writes - del_writes_at_quit);
    CHECK(frames - f0 < 4000, "%s: took %ld frames", name, frames - f0);
    CHECK(del_screen == want_screen, "%s: DEL was pressed on screen %d, wanted %d", name, del_screen, want_screen);
    if (want_ptype >= 0) CHECK(del_ptype == want_ptype, "%s: problem type %d, wanted %d", name, del_ptype, want_ptype);
    if (stub_gfx_ended == 0) del_fails++;
    printf("  DEL on %-34s -> left at once, gfx_End() called\n", name);
}

static void p_title(void) { to_title_top(); idle(2); }
static void p_help(void) { pick_title(4); idle(2); tap(7, kb_Right); idle(2); }
static void p_input(void) { pick_title(0); idle(3); }
static void p_input_typed(void) { pick_title(0); typeall("3x+sqrt("); tap(1, kb_2nd); idle(3); }
static void p_message(void) { pick_title(0); typeall("x/2=1"); tap(6, kb_Enter); idle(3); }
static void p_choose(void) { pick_title(0); typeall("x^3+2x"); tap(6, kb_Enter); idle(3); }
static void p_steps_mid(void) { pick_title(0); typeall("3x+5=20"); tap(6, kb_Enter); idle(2); tap(6, kb_Enter); idle(2); }
static void p_end(void)
{
    int i;
    pick_title(0);
    typeall("3x+5=20");
    tap(6, kb_Enter);
    idle(2);
    CHECK(generate("3x+5=20", PT_LINEAR), "p_end generate");
    for (i = 0; i < nsteps; i++) { tap(6, kb_Enter); idle(1); }
    idle(3);
}
static void p_radical_steps(void) { pick_title(0); typeall("(-3+sqrt(17))/4"); tap(6, kb_Enter); idle(2); tap(6, kb_Enter); idle(2); }
static void p_history(void) { pick_title(3); idle(3); }
static void p_venn2_form(void) { pick_title(1); venn_type("20,12,9", 0); idle(2); }
static void p_venn3_form(void) { pick_title(2); idle(2); tap(7, kb_Down); tap(7, kb_Down); typeall("12"); idle(2); }
static void p_venn_steps(void) { pick_title(1); venn_type("20,12,9,5,", "n(AuB)'"); tap(1, kb_Graph); idle(3); tap(6, kb_Enter); idle(2); tap(6, kb_Enter); idle(2); }
static void p_venn3_steps(void) { pick_title(2); venn_type("50,20,18,15,8,7,6,3,", "n((AnB)uC)"); tap(1, kb_Graph); idle(3); tap(6, kb_Enter); idle(2); }
static void p_venn_msg(void) { pick_title(1); venn_type("20,12,9,10,", ""); tap(1, kb_Graph); idle(3); }
static void p_venn_end(void)
{
    int i;
    pick_title(1);
    venn_type("20,12,9,5,", "AuB");
    tap(1, kb_Graph);
    idle(3);
    for (i = venn_count(2, "20,12,9,5,", "AuB"); i > 0; i--) { tap(6, kb_Enter); idle(1); }
    idle(3);
}

static void run_del_tests(void)
{
    del_try("the title screen", p_title, SC_TITLE, -1);
    del_try("the help screen (page 2)", p_help, SC_HELP, -1);
    del_try("an empty input line", p_input, SC_INPUT, -1);
    del_try("the input line with 2nd lit", p_input_typed, SC_INPUT, -1);
    del_try("the 'cannot solve' message", p_message, SC_ERROR, -1);
    del_try("the simplify-or-derivative menu", p_choose, SC_CHOOSE, -1);
    del_try("a linear problem, step 2", p_steps_mid, SC_STEPS, PT_LINEAR);
    del_try("the end menu (Done!)", p_end, SC_END, PT_LINEAR);
    del_try("a square-root problem, step 2", p_radical_steps, SC_STEPS, PT_RADICAL);
    del_try("the history screen", p_history, SC_HIST, -1);
    del_try("the Venn 2-set form", p_venn2_form, SC_VENN, -1);
    del_try("the Venn 3-set form (field 3)", p_venn3_form, SC_VENN, -1);
    del_try("Venn 2 sets, step 3 of the steps", p_venn_steps, SC_STEPS, PT_VENN);
    del_try("Venn 3 sets, step 2 of the steps", p_venn3_steps, SC_STEPS, PT_VENN);
    del_try("the Venn 'numbers don't fit' message", p_venn_msg, SC_ERROR, -1);
    del_try("the Venn end menu (Done!)", p_venn_end, SC_END, PT_VENN);
    printf("DEL: %ld screens tested, DEL left at once every time and never saved anything\n", del_runs);
}


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
    if (watch_limit && frames > watch_limit) { fprintf(stderr, "FAIL: the program did not quit (hang)\n"); exit(4); }
    if (frames > 40000000L) { fprintf(stderr, "ERROR: frame cap reached\n"); exit(3); }
}


void stub_feed_keys(uint8_t *kb)
{
    memset(kb, 0, 8);
    if (tap_release) { tap_release = 0; return; }
    while (qh != qt) {
        tap_t *t = &q[qh & 8191];
        if (t->fn) { void (*f)(void) = t->fn; qh++; f(); continue; }
        if (t->shot >= 0 && shot_dir) write_ppm(shot_names[t->shot]);
        kb[t->g] |= t->m;
        qh++;
        tap_release = 1;
        return;
    }
    if (fuzz_mode) {
        /* random key presses (every key the game uses, 2nd and the Venn letters included); now and then a whole
           problem is typed in, and DEL is pressed on purpose (never by accident: ghosting is on) */
        static const struct { uint8_t g, m; } K[] = {
            { 3, kb_0 }, { 3, kb_1 }, { 4, kb_2 }, { 5, kb_3 }, { 3, kb_4 }, { 4, kb_5 }, { 5, kb_6 }, { 3, kb_7 }, { 4, kb_8 }, { 5, kb_9 },
            { 3, kb_GraphVar }, { 6, kb_Add }, { 6, kb_Sub }, { 6, kb_Mul }, { 6, kb_Div }, { 6, kb_Power }, { 5, kb_Chs }, { 6, kb_Enter },
            { 6, kb_Clear }, { 1, kb_Yequ }, { 1, kb_Graph }, { 2, kb_Square }, { 7, kb_Left }, { 7, kb_Right }, { 7, kb_Up }, { 7, kb_Down },
            { 1, kb_2nd }, { 1, kb_2nd }, { 4, kb_LParen }, { 5, kb_RParen }, { 6, kb_Enter }, { 2, kb_Math }, { 3, kb_Apps }, { 4, kb_Prgm },
            { 2, kb_Log }, { 5, kb_Vars }, { 3, kb_Comma }, { 1, kb_Mode }, { 2, kb_Alpha }, { 4, kb_Stat }, { 2, kb_Ln }, { 1, kb_Zoom },
            { 2, kb_Square }, { 1, kb_2nd }
        };
        static const char *const probs[] = { "3x+5=20", "x^2-5x+6=0", "2x^2+3x-1=0", "3(x+2)+4x", "x^3+2x", "1/2+3/4", "(1/2)/(3/4)",
                                            "2x+1=2x+3", "x^2+4=0", "5-(x+2)", "9999x^2+9999x+9999=0", "1/0", "x^4=16", "((((",
                                            "(-3+sqrt(17))/4", "sqrt(48)+2", "x^2+sqrt(25)x+6=0", "(-5+sqrt(5^2-4*1*6))/(2*1)", "sqrt(sqrt(", "sqrt(-4)" };
        static const char *const vennx[] = { "AuB", "n(AuB)'", "A'nB", "n((AnB)uC)", "(AuB)'nC", "AnBnC", "A'uB'uC'", "n(BnC)'", "AuuB", "D", "((A" };
        if (--fuzz_left <= 0) { fuzz_left = 1; }
        if (fuzz_frames-- <= 0) {                     /* time is up: DEL ends it, from wherever the game is */
            memset(kb, 0, 8);
            kb[1] |= kb_Del;
            del_intended = 1;
            return;
        }
        if (rr() % 400 == 0) { kb[1] |= kb_Del; del_intended = 1; return; }
        if (cur_present_screen == SC_VENN && rr() % 12 == 0) {
            char nums[80];
            int k, n = (int)(rr() % 10);
            nums[0] = 0;
            for (k = 0; k < n; k++) { char one[8]; sprintf(one, k ? ",%d" : "%d", (int)(rr() % 70)); strcat(nums, one); }
            venn_type(nums, vennx[rr() % (sizeof vennx / sizeof vennx[0])]);
            tap(1, kb_Graph);
            { int e = (int)(rr() % 12); while (e-- > 0) tap(6, kb_Enter); }
            return;
        }
        if (rr() % (getenv("FUZZ_TYPE") ? (unsigned)atoi(getenv("FUZZ_TYPE")) : 60u) == 0) {
            typeall(probs[rr() % (sizeof probs / sizeof probs[0])]);
            tap(6, kb_Enter);
            { int n = (int)(rr() % 30); while (n-- > 0) tap(6, kb_Enter); }
            return;
        }
        {
            int n = (int)(rr() % 3), k;
            for (k = 0; k < n; k++) {
                int i = (int)(rr() % (sizeof K / sizeof K[0]));
                if (K[i].g == 6 && K[i].m == kb_Clear && rr() % 4) continue;
                kb[K[i].g] |= K[i].m;
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
        { "(-3+sqrt(17))/4", "sqrt_formula", 0 },
        { "(-5+sqrt(5^2-4*1*6))/(2*1)", "sqrt_formula2", 0 },
        { "x^2+sqrt(25)x+6=0", "sqrt_in_equation", 0 },
        { "x/2=1", "cannot", 0 },
        { "3x+", "cannot_syntax", 0 }
    };
    size_t i;
    int s, needclear = 0;
    idle(4);
    shot("00_title");
    pick_title(4); idle(2); shot("01_help_types");
    tap(7, kb_Right); idle(2); shot("02_help_typing");
    tap(7, kb_Right); idle(2); shot("03_help_venn");
    tap(7, kb_Right); idle(2); shot("03b_help_limits");
    tap(7, kb_Right); idle(2); shot("03c_help_del");
    tap(6, kb_Clear); idle(4);
    pick_title(0); idle(2);
    shot("04_input_empty");
    /* the 2nd key: lit, then sqrt( typed by the next key, then off again */
    tap(1, kb_2nd); idle(2); shot("05_input_2nd_lit");
    tap(2, kb_Square); idle(2); shot("05b_input_sqrt_typed_2nd_off");
    tap(1, kb_Mode); idle(2);
    for (i = 0; i < sizeof P / sizeof P[0]; i++) {
        cls_t cl;
        int type, solved = 0, n = 0;
        if (needclear) { tap(1, kb_Mode); idle(2); }
        typeall(P[i].prob);
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
    if (needclear) { tap(1, kb_Mode); idle(2); }
    tap(6, kb_Clear); idle(3);
    pick_title(3); idle(2);
    shot("history");
    tap(6, kb_Enter); idle(3);
    shot("history_replay_s1");
    tap(6, kb_Clear); idle(2); tap(6, kb_Clear); idle(2);
    /* Venn diagrams */
    {
        static const struct { int k; const char *nums; const char *expr; const char *name; } V[] = {
            { 2, "20,12,9,5,", "n(AuB)", "venn2" },
            { 2, "20,12,9,5,", "(AuB)'", "venn2_complement" },
            { 3, "50,20,18,15,8,7,6,3,", "n(AuB)'nC", "venn3_nested" },
            { 3, "50,20,18,15,8,7,6,3,", "n((AnB)uC)", "venn3_nested2" },
            { 3, "50,20,18,15,8,7,6,3,", "AuBuC", "venn3_union" },
            { 3, "50,20,18,15,8,7,6,3,", "", "venn3_plain" }
        };
        size_t v;
        for (v = 0; v < sizeof V / sizeof V[0]; v++) {
            int n = venn_count(V[v].k, V[v].nums, V[v].expr), st;
            pick_title(V[v].k - 1);
            idle(2);
            venn_type(V[v].nums, V[v].expr);
            idle(2);
            shot(shot_name("%s_a_form%.0d", V[v].name, 0));
            tap(1, kb_Graph);
            idle(3);
            for (st = 0; st < n; st++) {
                shot(shot_name("%s_s%02d", V[v].name, st + 1));
                tap(6, kb_Enter);
                idle(2);
            }
            shot(shot_name("%s_z_end_menu%.0d", V[v].name, 0));
            tap(7, kb_Down); tap(7, kb_Down); tap(6, kb_Enter); idle(3);      /* back to the main menu */
        }
        /* numbers that do not fit together, and an expression with a mistake */
        pick_title(1); idle(2);
        venn_type("20,12,9,10,", "");
        idle(2);
        shot("venn_bad_a_form");
        tap(1, kb_Graph); idle(3);
        shot("venn_bad_b_message");
        tap(6, kb_Enter); idle(3);
        tap(1, kb_Mode); idle(2);
        pick_title(2); idle(2);
        venn_type("50,20,18,15,8,7,6,3,", "(AuB");
        tap(1, kb_Graph); idle(3);
        shot("venn_badexpr_message");
        tap(6, kb_Enter); idle(3);
        tap(1, kb_Mode); idle(2);
        pick_title(1); idle(2);
        venn_type("20,12,9,5", "");
        idle(2);
        shot("venn_form_2sets");
        tap(1, kb_Mode); idle(2);
    }
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
        run_venn_garbage(n);
        printf("garbage: %ld checks, %ld failures, longest text %d lines, state %d lines, truncated %ld\n", checks, fails, max_text_lines, max_state_lines, trunc_n);
        return fails != 0 || trunc_n != 0;
    }
    if (!strcmp(sc, "fuzz")) {
        fuzz_mode = 1;
        fuzz_frames = n;
        logf_ = 0;
        stub_ghost_on = 1;                          /* keypad ghosting simulated on every scan */
        while (fuzz_frames > 0) game_main();
        printf("fuzz: %ld frames shown, presents by screen (title input steps end history help error choose venn):", frames);
        { int i; for (i = 0; i < 9; i++) printf(" %ld", present_count[i]); }
        printf("\ntruncated strings: %ld  text overflow: %ld  NoClip violations: %ld  failures: %ld\n", trunc_n, text_overflow, violations, fails);
        printf("DEL quits: %ld (every one pressed on purpose; the last one ended the run), gfx_End() reached: %d\n", del_quits_total, stub_gfx_ended);
        return fails != 0 || trunc_n != 0 || text_overflow != 0 || violations != 0 || !stub_gfx_ended;
    }
    if (!strcmp(sc, "venn")) {
        if (getenv("LOG")) logf_ = fopen(getenv("LOG"), "w"); else logf_ = 0;
        run_venn_tests(n);
        printf("venn: %ld checks, %ld failures, truncated strings %ld\n", checks, fails, trunc_n);
        return fails != 0 || trunc_n != 0;
    }
    if (!strcmp(sc, "radical")) {
        if (getenv("LOG")) logf_ = fopen(getenv("LOG"), "w"); else logf_ = 0;
        run_radical_tests(n);
        run_sqrt_equations();
        printf("radical: %ld checks, %ld failures, truncated strings %ld\n", checks, fails, trunc_n);
        return fails != 0 || trunc_n != 0;
    }
    if (!strcmp(sc, "ui")) {
        logf_ = 0;
        stub_ghost_on = 0;
        run_second_key_test();
        run_sqrt_ui_test();
        run_venn_ui_test();
        printf("ui: %ld checks, %ld failures, text overflow %ld, NoClip violations %ld\n", checks, fails, text_overflow, violations);
        return fails != 0 || text_overflow != 0 || violations != 0;
    }
    if (!strcmp(sc, "del")) {
        logf_ = 0;
        stub_ghost_on = 0;
        run_phantom_test();
        run_del_tests();
        printf("del: %ld checks, %ld failures\n", checks, fails);
        return fails != 0;
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
