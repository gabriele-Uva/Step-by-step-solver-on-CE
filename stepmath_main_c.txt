/*
 * StepMath CE  (v1)
 * A step-by-step math tutor for the TI-84 Plus CE. The student types a
 * problem; the app recognises its type and shows ONE step of the method at
 * a time, in words, with the running equation or expression.
 * Built with the CE C/C++ Toolchain (CEdev): graphx, keypadc, fileioc.
 *
 * WHAT IS SUPPORTED (everything else is refused with a plain message)
 *   1. Linear equations in x, both sides may have brackets, like terms and
 *      x on both sides:  3x+5=20   2(x-4)=x+7   2x+1=2x+3 (no solution)
 *   2. Quadratic equations (anything that becomes ax^2+bx+c=0):
 *      factoring when the roots are integers, otherwise the quadratic
 *      formula with the discriminant as its own step. Negative
 *      discriminant: "no real solutions" (complex roots are not computed).
 *   3. Simplifying expressions: expand brackets one at a time (a number or
 *      a term times ONE bracket, or a minus sign before a bracket), then
 *      combine like terms one power of x at a time.
 *   4. Derivatives d/dx of sums of terms c*x^n (sum, constant and power
 *      rule). NOT supported: chain, product and quotient rule, brackets.
 *   5. Fraction arithmetic: two fractions (or a fraction and a whole
 *      number) with + - * /. Write a division of fractions as (1/2)/(3/4).
 *
 * INPUT GRAMMAR (one line, at most 38 characters)
 *   expr   := term { (+|-) term }
 *   term   := unary { (*|/) unary  |  implicit multiplication }
 *   unary  := - unary | + unary | power
 *   power  := atom [ ^ whole-number ]
 *   atom   := whole number (at most 4 digits) | x | ( expr )
 *   input  := expr [ = expr ]
 *   Implicit multiplication: a number, x or ) followed by x or ( , as in
 *   3x, 2(x+1), x(x-2).  (x+1)(x+2) parses but is refused later: only ONE
 *   bracket per term is supported.  Only the letter x is a variable.
 *
 * LIMITS
 *   - integers only (no decimals); numbers typed have at most 4 digits;
 *     every coefficient that appears while solving must stay within
 *     +-1,000,000 (quadratic a, b, c within +-10,000 so that b^2-4ac fits
 *     in 32 bits); powers of x up to 12.
 *   - int is 24 bits on the eZ80: all arithmetic that can get big is done
 *     in long (32 bits) and checked before it can overflow.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tice.h>
#include <graphx.h>
#include <keypadc.h>
#include <fileioc.h>

#ifdef SM_HOST_TEST
#include "test_hooks.h"
#define TEST_HOOK(x) x
#else
#define TEST_HOOK(x)
#endif

#define SCR_W 320
#define SCR_H 240
#define MAXIN 38            /* longest problem that can be typed */
#define COLS 38             /* text columns used for wrapped text */
#define NB '\x01'           /* a space that is never used for a line break */
#define MAXK 12             /* highest power of x */
#define CLIM 1000000L       /* limit for any coefficient */
#define QLIM 10000L         /* limit for a, b, c of a quadratic */
#define LONGMAX 2147483647L

/* ------------------------------------------------------------------ */
/* Palette                                                             */
/* ------------------------------------------------------------------ */
enum {
    C_TRANSP, C_BG, C_PANEL, C_HEAD, C_WHITE, C_YELLOW, C_GREEN, C_RED, C_CYAN,
    C_GRAY, C_BLACK, C_DKGRAY, C_ORANGE, C_COUNT
};
#define RGB(r, g, b) gfx_RGBTo1555(r, g, b)
static uint16_t pal[16];

static void build_palette(void)
{
    pal[C_TRANSP] = RGB(255, 0, 255);
    pal[C_BG] = RGB(16, 22, 40);
    pal[C_PANEL] = RGB(30, 42, 72);
    pal[C_HEAD] = RGB(40, 90, 170);
    pal[C_WHITE] = RGB(255, 255, 255);
    pal[C_YELLOW] = RGB(255, 220, 70);
    pal[C_GREEN] = RGB(90, 230, 130);
    pal[C_RED] = RGB(255, 100, 90);
    pal[C_CYAN] = RGB(120, 220, 240);
    pal[C_GRAY] = RGB(150, 160, 185);
    pal[C_BLACK] = RGB(0, 0, 0);
    pal[C_DKGRAY] = RGB(70, 80, 105);
    pal[C_ORANGE] = RGB(255, 160, 60);
}

/* ------------------------------------------------------------------ */
/* Bounded string builder (nothing can overflow its buffer)            */
/* ------------------------------------------------------------------ */
typedef struct { char *p; uint16_t cap, len; } sb_t;

static void sb_init(sb_t *b, char *p, uint16_t cap)
{
    b->p = p;
    b->cap = cap;
    b->len = 0;
    p[0] = 0;
}

static void sb_c(sb_t *b, char c)
{
    if (b->len + 1 < b->cap) {
        b->p[b->len++] = c;
        b->p[b->len] = 0;
    } else {
        TEST_HOOK(test_trunc());
    }
}

static void sb_s(sb_t *b, const char *s)
{
    while (*s) sb_c(b, *s++);
}

static void sb_n(sb_t *b, long v)
{
    char t[12];
    uint8_t n = 0;
    unsigned long u;
    if (v < 0) { sb_c(b, '-'); u = 0UL - (unsigned long)v; }
    else u = (unsigned long)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) sb_c(b, t[--n]);
}

/* a number, in brackets when it is negative: 3 or (-3) */
static void sb_pn(sb_t *b, long v)
{
    if (v < 0) { sb_c(b, '('); sb_n(b, v); sb_c(b, ')'); }
    else sb_n(b, v);
}

/* " + " or " - " (the space after the sign never breaks a line) */
static void sb_op(sb_t *b, bool minus)
{
    sb_c(b, ' ');
    sb_c(b, minus ? '-' : '+');
    sb_c(b, NB);
}

static long gcdl(long a, long b)
{
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { long t = a % b; a = b; b = t; }
    return a;
}

/* floor(sqrt(n)) for 0 <= n <= 2^31-1 */
static long isqrtl(long n)
{
    long lo = 0, hi = 46340;
    if (n <= 0) return 0;
    while (lo < hi) {
        long mid = (lo + hi + 1) / 2;
        if (mid * mid <= n) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

/* floor(sqrt(n) * 10^digits) for 0 <= n <= 5*10^8 and digits <= 3,
   digit by digit so that nothing overflows 32 bits */
static long sqrt_scaled(long n, int digits)
{
    long p = isqrtl(n), r = n - p * p, d, best;
    int i;
    for (i = 0; i < digits; i++) {
        r *= 100;
        best = 0;
        for (d = 9; d > 0; d--) {
            if ((20 * p + d) * d <= r) { best = d; break; }
        }
        r -= (20 * p + best) * best;
        p = p * 10 + best;
    }
    return p;
}

/* a / d rounded to the nearest integer, halves away from zero (d > 0) */
static long round_div(long a, long d)
{
    if (a >= 0) return (2 * a + d) / (2 * d);
    return -((-2 * a + d) / (2 * d));
}

/* n/d with the sign moved to the top; d must not be 0 */
static void sb_frac(sb_t *b, long n, long d)
{
    if (d < 0) { n = -n; d = -d; }
    sb_n(b, n);
    if (d != 1) { sb_c(b, '/'); sb_n(b, d); }
}

/* a number with two decimals: v100 = value * 100 */
static void sb_dec2(sb_t *b, long v100)
{
    long a = v100 < 0 ? -v100 : v100;
    if (v100 < 0) sb_c(b, '-');
    sb_n(b, a / 100);
    sb_c(b, '.');
    sb_c(b, (char)('0' + (a / 10) % 10));
    sb_c(b, (char)('0' + a % 10));
}

/* ------------------------------------------------------------------ */
/* Parser: text -> syntax tree                                         */
/* ------------------------------------------------------------------ */
enum { N_NUM, N_X, N_NEG, N_ADD, N_SUB, N_MUL, N_DIV, N_POW, N_EQ };
typedef struct { uint8_t t; int16_t l, r; long v; } node_t;
#define MAXN 120
static node_t nd[MAXN];
static int nn;
static const char *pp, *pin;     /* parse position and start */
static const char *perr;         /* parse error text, or 0 */
static int perr_at;              /* its position (0-based) */
static uint8_t pdepth;

static void fail_at(const char *msg)
{
    if (!perr) { perr = msg; perr_at = (int)(pp - pin); }
}

static int mk(uint8_t t, int l, int r, long v)
{
    if (nn >= MAXN) { fail_at("That is too long for me to read."); return -1; }
    nd[nn].t = t;
    nd[nn].l = (int16_t)l;
    nd[nn].r = (int16_t)r;
    nd[nn].v = v;
    return nn++;
}

static void skipsp(void)
{
    while (*pp == ' ' || *pp == NB) pp++;
}

static int p_expr(void);

static int p_atom(void)
{
    skipsp();
    if (*pp >= '0' && *pp <= '9') {
        long v = 0;
        int digits = 0;
        while (*pp >= '0' && *pp <= '9') { v = v * 10 + (*pp - '0'); pp++; digits++; if (digits > 4) break; }
        if (digits > 4) { fail_at("Numbers can have at most 4 digits."); return -1; }
        if (*pp == '.') { fail_at("Decimals are not supported. Use whole numbers."); return -1; }
        return mk(N_NUM, -1, -1, v);
    }
    if (*pp == 'x' || *pp == 'X') {
        pp++;
        return mk(N_X, -1, -1, 0);
    }
    if (*pp == '(') {
        int e;
        pp++;
        if (++pdepth > 16) { fail_at("Too many brackets inside brackets."); return -1; }
        e = p_expr();
        pdepth--;
        if (e < 0) return -1;
        skipsp();
        if (*pp != ')') { fail_at("A closing bracket ) is missing."); return -1; }
        pp++;
        return e;
    }
    if (*pp == 0) fail_at("The problem ends too early.");
    else if (*pp == ')') fail_at("There is a ) without a ( before it.");
    else fail_at("I expected a number, x or ( here.");
    return -1;
}

static int p_pow(void)
{
    int a = p_atom(), e;
    long v = 0;
    int digits = 0;
    if (a < 0) return -1;
    skipsp();
    if (*pp != '^') return a;
    pp++;
    skipsp();
    while (*pp >= '0' && *pp <= '9') { v = v * 10 + (*pp - '0'); pp++; if (++digits > 3) break; }
    if (digits == 0 || digits > 2) { fail_at("After ^ I need a whole number from 0 to 12."); return -1; }
    if (v > MAXK) { fail_at("Powers above 12 are not supported."); return -1; }
    e = mk(N_NUM, -1, -1, v);
    if (e < 0) return -1;
    return mk(N_POW, a, e, 0);
}

static int p_unary(void)
{
    skipsp();
    if (*pp == '-' || *pp == '+') {
        bool neg = *pp == '-';
        int a;
        pp++;
        if (++pdepth > 16) { fail_at("Too many signs in a row."); return -1; }
        a = p_unary();
        pdepth--;
        if (a < 0) return -1;
        return neg ? mk(N_NEG, a, -1, 0) : a;
    }
    return p_pow();
}

static int p_term(void)
{
    int a = p_unary();
    if (a < 0) return -1;
    for (;;) {
        char c;
        int b;
        skipsp();
        c = *pp;
        if (c == '*' || c == '/') {
            pp++;
            b = p_unary();
            if (b < 0) return -1;
            a = mk(c == '*' ? N_MUL : N_DIV, a, b, 0);
        } else if (c == 'x' || c == 'X' || c == '(') {
            b = p_pow();                          /* implicit multiplication */
            if (b < 0) return -1;
            a = mk(N_MUL, a, b, 0);
        } else {
            break;
        }
        if (a < 0) return -1;
    }
    return a;
}

static int p_expr(void)
{
    int a = p_term();
    if (a < 0) return -1;
    for (;;) {
        char c;
        int b;
        skipsp();
        c = *pp;
        if (c != '+' && c != '-') break;
        pp++;
        b = p_term();
        if (b < 0) return -1;
        a = mk(c == '+' ? N_ADD : N_SUB, a, b, 0);
        if (a < 0) return -1;
    }
    return a;
}

/* returns the root node, or -1 with perr set */
static int parse(const char *s)
{
    int a, b;
    nn = 0;
    perr = 0;
    perr_at = 0;
    pdepth = 0;
    pin = pp = s;
    skipsp();
    if (*pp == 0) { fail_at("Type a problem first."); return -1; }
    a = p_expr();
    if (a < 0) return -1;
    skipsp();
    if (*pp == '=') {
        pp++;
        b = p_expr();
        if (b < 0) return -1;
        skipsp();
        if (*pp == '=') { fail_at("Only one = sign is allowed."); return -1; }
        a = mk(N_EQ, a, b, 0);
        if (a < 0) return -1;
    }
    skipsp();
    if (*pp != 0) {
        if (*pp >= '0' && *pp <= '9') fail_at("Put numbers before brackets or x, like 3x or 2(x+1).");
        else fail_at("I do not understand this part.");
        return -1;
    }
    return a;
}

static bool has_x(int n)
{
    if (n < 0) return false;
    if (nd[n].t == N_X) return true;
    if (nd[n].t == N_NUM) return false;
    return has_x(nd[n].l) || has_x(nd[n].r);
}

static bool has_div(int n)
{
    if (n < 0) return false;
    if (nd[n].t == N_DIV) return true;
    if (nd[n].t == N_NUM || nd[n].t == N_X) return false;
    return has_div(nd[n].l) || has_div(nd[n].r);
}

/* ------------------------------------------------------------------ */
/* Algebra: a side of an equation as a list of terms                   */
/* ------------------------------------------------------------------ */
/* A side is a list of "items". An item is c*x^k, optionally times ONE
   bracket holding up to 8 terms:  3(x + 2),  -(2x - 1),  x(x + 4),  5x. */
#define MAXIT 12
#define MAXBR 8
typedef struct { long c; uint8_t k; } mono_t;
typedef struct { mono_t m; uint8_t nb; mono_t br[MAXBR]; } item_t;
typedef struct { item_t it[MAXIT]; uint8_t n; } side_t;
typedef long poly_t[MAXK + 1];

static const char *cv_err;       /* why a conversion failed */
static bool no_br;               /* converting the inside of a bracket */
static uint8_t grp_depth;

static bool is_group(int n)
{
    return nd[n].t == N_ADD || nd[n].t == N_SUB;
}

static bool mul_c(long *c, long v)
{
    long a = *c < 0 ? -*c : *c, b = v < 0 ? -v : v;
    if (b != 0 && a > CLIM / b) { cv_err = "Those numbers get too big for me."; return false; }
    *c *= v;
    return true;
}

static bool add_k(item_t *it, long e)
{
    if (it->m.k + e > MAXK) { cv_err = "Powers of x above 12 are not supported."; return false; }
    it->m.k = (uint8_t)(it->m.k + e);
    return true;
}

static bool side_add(side_t *s, int n, int sign);

/* multiply the item by the product of the factors under node n */
static bool fac(item_t *it, int n)
{
    const node_t *q = &nd[n];
    switch (q->t) {
    case N_NUM:
        return mul_c(&it->m.c, q->v);
    case N_X:
        return add_k(it, 1);
    case N_POW:
        if (nd[q->l].t == N_X) return add_k(it, nd[q->r].v);
        cv_err = "Powers are only supported on x, like x^2.";
        return false;
    case N_NEG:
        it->m.c = -it->m.c;
        return fac(it, q->l);
    case N_MUL:
        return fac(it, q->l) && fac(it, q->r);
    case N_ADD:
    case N_SUB: {
        side_t tmp;
        bool ok;
        uint8_t j;
        if (grp_depth >= 2) { cv_err = "Brackets inside brackets are not supported."; return false; }
        tmp.n = 0;
        grp_depth++;
        no_br = true;
        ok = side_add(&tmp, n, 1);
        grp_depth--;
        if (grp_depth == 0) no_br = false;
        if (!ok) return false;
        if (tmp.n == 1) {
            if (!mul_c(&it->m.c, tmp.it[0].m.c)) return false;
            return add_k(it, tmp.it[0].m.k);
        }
        if (it->nb > 0) { cv_err = "Only one bracket per term is supported, so (x+1)(x+2) is not."; return false; }
        if (tmp.n > MAXBR) { cv_err = "That bracket has too many terms."; return false; }
        for (j = 0; j < tmp.n; j++) it->br[j] = tmp.it[j].m;
        it->nb = tmp.n;
        return true;
    }
    default:
        cv_err = has_x(n) ? "Division with x in it is not supported (no x/2 or 1/x)."
                          : "Division is only supported for number fractions like 1/2+3/4.";
        return false;
    }
}

static bool add_term(side_t *s, int n, int sign)
{
    item_t it;
    if (s->n >= MAXIT) { cv_err = "That has too many terms for me."; return false; }
    memset(&it, 0, sizeof it);
    it.m.c = sign;
    if (!fac(&it, n)) return false;
    if (no_br && it.nb > 0) { cv_err = "Brackets inside brackets are not supported."; return false; }
    s->it[s->n++] = it;
    return true;
}

static bool side_add(side_t *s, int n, int sign)
{
    const node_t *q = &nd[n];
    switch (q->t) {
    case N_ADD:
        return side_add(s, q->l, sign) && side_add(s, q->r, sign);
    case N_SUB:
        if (!side_add(s, q->l, sign)) return false;
        return is_group(q->r) ? add_term(s, q->r, -sign) : side_add(s, q->r, -sign);
    case N_NEG:
        if (is_group(q->l)) return add_term(s, n, sign);
        return side_add(s, q->l, -sign);
    default:
        return add_term(s, n, sign);
    }
}

static bool to_side(side_t *s, int n)
{
    s->n = 0;
    cv_err = 0;
    no_br = false;
    grp_depth = 0;
    return side_add(s, n, 1);
}

/* ---- polynomials ---- */
static bool side_poly(const side_t *s, long *p)
{
    uint8_t i, j;
    memset(p, 0, sizeof(long) * (MAXK + 1));
    for (i = 0; i < s->n; i++) {
        const item_t *it = &s->it[i];
        if (it->nb == 0) {
            p[it->m.k] += it->m.c;
            if (p[it->m.k] > CLIM || p[it->m.k] < -CLIM) { cv_err = "Those numbers get too big for me."; return false; }
        } else {
            for (j = 0; j < it->nb; j++) {
                long v = it->m.c;
                int kk = it->m.k + it->br[j].k;
                if (kk > MAXK) { cv_err = "Powers of x above 12 are not supported."; return false; }
                if (!mul_c(&v, it->br[j].c)) return false;
                p[kk] += v;
                if (p[kk] > CLIM || p[kk] < -CLIM) { cv_err = "Those numbers get too big for me."; return false; }
            }
        }
    }
    return true;
}

static int poly_deg(const long *p)
{
    int k;
    for (k = MAXK; k >= 0; k--) if (p[k]) return k;
    return -1;
}

static void poly_side(side_t *s, const long *p)
{
    int k;
    s->n = 0;
    for (k = MAXK; k >= 0; k--) {
        if (!p[k]) continue;
        s->it[s->n].m.c = p[k];
        s->it[s->n].m.k = (uint8_t)k;
        s->it[s->n].nb = 0;
        s->n++;
    }
}

/* value of a polynomial at an integer v; false if it does not fit */
static bool poly_eval(const long *p, long v, long *out)
{
    long acc = 0, pw = 1;
    int k;
    for (k = 0; k <= MAXK; k++) {
        long term, a, b;
        if (p[k]) {
            a = p[k] < 0 ? -p[k] : p[k];
            b = pw < 0 ? -pw : pw;
            if (b != 0 && a > LONGMAX / 4 / b) return false;
            term = p[k] * pw;
            acc += term;
            if (acc > LONGMAX / 4 || acc < -(LONGMAX / 4)) return false;
        }
        if (k < MAXK) {
            long av = v < 0 ? -v : v, ap = pw < 0 ? -pw : pw;
            if (av != 0 && ap > LONGMAX / 4 / av) {
                /* the rest of the powers are too big: only fine if no higher term */
                int j;
                for (j = k + 1; j <= MAXK; j++) if (p[j]) return false;
                *out = acc;
                return true;
            }
            pw *= v;
        }
    }
    *out = acc;
    return true;
}

/* ---- printing ---- */
static void sb_coef_x(sb_t *b, long a, int k)    /* a >= 0 */
{
    if (k == 0) { sb_n(b, a); return; }
    if (a != 1) sb_n(b, a);
    sb_c(b, 'x');
    if (k > 1) { sb_c(b, '^'); sb_n(b, k); }
}

static void sb_mono(sb_t *b, long c, int k, bool first)
{
    bool neg = c < 0;
    long a = neg ? -c : c;
    if (first) { if (neg) sb_c(b, '-'); }
    else sb_op(b, neg);
    if (c == 0) { sb_c(b, '0'); return; }
    sb_coef_x(b, a, k);
}

static void sb_item(sb_t *b, const item_t *it, bool first)
{
    uint8_t j;
    bool neg, one;
    long a;
    if (it->nb == 0) { sb_mono(b, it->m.c, it->m.k, first); return; }
    neg = it->m.c < 0;
    a = neg ? -it->m.c : it->m.c;
    if (first) { if (neg) sb_c(b, '-'); }
    else sb_op(b, neg);
    one = a == 1 && it->m.k == 0;
    if (!one) sb_coef_x(b, a, it->m.k);
    sb_c(b, '(');
    for (j = 0; j < it->nb; j++) sb_mono(b, it->br[j].c, it->br[j].k, j == 0);
    sb_c(b, ')');
}

static void sb_side(sb_t *b, const side_t *s)
{
    uint8_t i;
    if (s->n == 0) { sb_c(b, '0'); return; }
    for (i = 0; i < s->n; i++) sb_item(b, &s->it[i], i == 0);
}

/* ---- expanding a bracket and combining like terms ---- */
static int first_br(const side_t *s)
{
    uint8_t i;
    for (i = 0; i < s->n; i++) if (s->it[i].nb) return i;
    return -1;
}

static bool expand_item(side_t *s, int idx)
{
    item_t it = s->it[idx];
    item_t out[MAXBR];
    uint8_t j, i;
    if (s->n - 1 + it.nb > MAXIT) { cv_err = "That has too many terms for me."; return false; }
    for (j = 0; j < it.nb; j++) {
        long v = it.m.c;
        if (!mul_c(&v, it.br[j].c)) return false;
        if (it.m.k + it.br[j].k > MAXK) { cv_err = "Powers of x above 12 are not supported."; return false; }
        memset(&out[j], 0, sizeof out[j]);
        out[j].m.c = v;
        out[j].m.k = (uint8_t)(it.m.k + it.br[j].k);
    }
    for (i = s->n; i > (uint8_t)(idx + 1); i--) s->it[i - 1 + it.nb - 1] = s->it[i - 1];
    for (j = 0; j < it.nb; j++) s->it[idx + j] = out[j];
    s->n = (uint8_t)(s->n - 1 + it.nb);
    return true;
}

static bool side_in_order(const side_t *s)
{
    uint8_t i;
    for (i = 1; i < s->n; i++) if (s->it[i].m.k >= s->it[i - 1].m.k) return false;
    return true;
}

static void side_sort(side_t *s)
{
    uint8_t i, j;
    for (i = 1; i < s->n; i++) {
        item_t t = s->it[i];
        j = i;
        while (j > 0 && s->it[j - 1].m.k < t.m.k) { s->it[j] = s->it[j - 1]; j--; }
        s->it[j] = t;
    }
}

/* ------------------------------------------------------------------ */
/* Step storage                                                        */
/* ------------------------------------------------------------------ */
enum { PT_NONE, PT_LINEAR, PT_QUAD, PT_SIMPLIFY, PT_DERIV, PT_FRAC };
static const char *const PT_NAME[] = {
    "", "Linear equation", "Quadratic equation", "Simplify expression", "Derivative d/dx", "Fraction arithmetic"
};

/* what a step's state line is, so that the tests can check it */
enum { EQ_NONE, EQ_EQN, EQ_EXPR, EQ_DERIV };
#define MAXSTEPS 32
#define STEXT 220
#define SSTATE 240
typedef struct { char text[STEXT]; char state[SSTATE]; uint8_t eqv; uint8_t fin; } step_t;
static step_t steps[MAXSTEPS];
static int nsteps;
static step_t dummy_step;
static bool st_over;
static sb_t tb, sbs;        /* builders for the text and the state of the step being written */

/* exact answers, for the tests */
typedef struct { long u, w, m, den; } root_t;       /* (u + w*sqrt(m)) / den */
enum { RK_NONE, RK_ONE, RK_NOSOL, RK_ALL, RK_ROOTS, RK_POLY, RK_FRAC };
typedef struct {
    uint8_t type, kind;
    long num, den;
    uint8_t nroots;
    root_t root[2];
    poly_t poly;
} result_t;
static result_t res;

static void st_begin(step_t *s, uint8_t eqv, uint8_t fin)
{
    s->eqv = eqv;
    s->fin = fin;
    sb_init(&tb, s->text, STEXT);
    sb_init(&sbs, s->state, SSTATE);
}

static void st_new(uint8_t eqv)
{
    if (nsteps >= MAXSTEPS - 1) { st_over = true; st_begin(&dummy_step, eqv, 0); return; }
    st_begin(&steps[nsteps++], eqv, 0);
}

/* the last step always fits */
static void st_final(uint8_t eqv)
{
    st_begin(&steps[nsteps < MAXSTEPS ? nsteps : MAXSTEPS - 1], eqv, 1);
    if (nsteps < MAXSTEPS) nsteps++;
}

static void steps_reset(void)
{
    nsteps = 0;
    st_over = false;
    memset(&res, 0, sizeof res);
}

/* ------------------------------------------------------------------ */
/* WORKED EXAMPLES (the reference for the wording below)               */
/* ------------------------------------------------------------------ */
/* Linear   3x + 5 = 20
 *   Step 1  Subtract 5 from both sides to get the x term by itself.   3x = 15
 *   Step 2  Divide both sides by 3 to get x by itself.                x = 5
 *   Step 3  Check: put x = 5 in the original ... 20 = 20
 *   Final answer: x = 5
 * Linear   2(x - 4) = x + 7
 *   Step 1  Expand 2(x - 4): multiply every term inside by 2.         2x - 8 = x + 7
 *   Step 2  Subtract x from both sides ... x terms on the left.       x - 8 = 7
 *   Step 3  Add 8 to both sides.                                      x = 15
 * Linear   2x + 1 = 2x + 3
 *   Step 1  Subtract 2x ...                                           1 = 3
 *   Step 2  The x terms are gone and 1 = 3 is never true: no solution.
 * Quadratic x^2 + 5x + 6 = 0  (integer roots: factoring)
 *   Step 1  Two numbers that multiply to 6 and add to 5: 2 and 3.
 *   Step 2  Factors:  (x + 2)(x + 3) = 0
 *   Step 3  A product is 0 only if a factor is 0:  x + 2 = 0 or x + 3 = 0
 *   Step 4  x = -2 or x = -3
 * Quadratic 2x^2 + 3x - 1 = 0  (formula)
 *   Step 1  a = 2, b = 3, c = -1 in x = (-b +/- sqrt(b^2 - 4ac)) / (2a)
 *   Step 2  D = (3)^2 - 4(2)(-1) = 9 - (-8) = 17
 *   Step 3  sqrt(17) cannot be simplified (about 4.12)
 *   Step 4  x1 = (-3 + sqrt(17))/4 ~ 0.28
 *   Step 5  x2 = (-3 - sqrt(17))/4 ~ -1.78
 * Simplify 3(x + 2) + 4x
 *   Step 1  Expand 3(x + 2): multiply every term inside by 3.         3x + 6 + 4x
 *   Step 2  Combine the x terms: 3x + 4x = 7x                         7x + 6
 * Derivative 3x^2 + 5x + 7
 *   Step 1  Sum rule: take d/dx of each term.   d/dx[3x^2] + d/dx[5x] + d/dx[7]
 *   Step 2  Power rule n = 2: d/dx[3x^2] = 6x.   6x + d/dx[5x] + d/dx[7]
 *   Step 3  Power rule n = 1: d/dx[5x] = 5.      6x + 5 + d/dx[7]
 *   Step 4  Constant rule: d/dx[7] = 0.          6x + 5 + 0
 *   Step 5  Add up (drop the 0).                 6x + 5
 * Fractions 1/4 + 1/6
 *   Step 1  Common denominator: the smallest number 4 and 6 both divide is 12.
 *   Step 2  Rewrite: multiply 1/4 top and bottom by 3, 1/6 by 2.      3/12 + 2/12
 *   Step 3  Add the numerators, keep the denominator.                 (3 + 2)/12 = 5/12
 *   (Step 4 simplifies when the numerator and denominator share a factor.)
 */

/* ------------------------------------------------------------------ */
/* Steps shared by equations and expressions                           */
/* ------------------------------------------------------------------ */
static side_t gL, gR;            /* the sides as they are right now */
static bool g_eq;                /* equation (two sides) or expression (one side) */
static const char *g_fail;       /* why solving was impossible */

#define EQV (g_eq ? EQ_EQN : EQ_EXPR)

static void put_state(void)
{
    sb_side(&sbs, &gL);
    if (g_eq) { sb_s(&sbs, " = "); sb_side(&sbs, &gR); }
}

static void sb_xk(sb_t *b, int k)
{
    if (k == 0) { sb_s(b, "plain numbers"); return; }
    sb_c(b, 'x');
    if (k > 1) { sb_c(b, '^'); sb_n(b, k); }
    sb_s(b, " terms");
}

/* expand one bracket per step, left side first */
static bool expand_sides(void)
{
    for (;;) {
        side_t *s;
        const char *where = "";
        int i;
        const item_t *it;
        if ((i = first_br(&gL)) >= 0) { s = &gL; if (g_eq) where = " on the left side"; }
        else if (g_eq && (i = first_br(&gR)) >= 0) { s = &gR; where = " on the right side"; }
        else break;
        it = &s->it[i];
        st_new(EQV);
        if (it->m.k == 0 && it->m.c == 1) {
            sb_s(&tb, "Remove the brackets ");
            sb_item(&tb, it, true);
            sb_s(&tb, where);
            sb_s(&tb, ": nothing multiplies them, so the terms inside stay as they are.");
        } else if (it->m.k == 0 && it->m.c == -1) {
            sb_s(&tb, "Distribute the minus sign in ");
            sb_item(&tb, it, true);
            sb_s(&tb, where);
            sb_s(&tb, ": it changes the sign of every term inside the brackets.");
        } else {
            sb_s(&tb, "Expand ");
            sb_item(&tb, it, true);
            sb_s(&tb, where);
            sb_s(&tb, ": multiply every term inside the brackets by ");
            sb_mono(&tb, it->m.c, it->m.k, true);
            sb_c(&tb, '.');
        }
        if (!expand_item(s, i)) { g_fail = cv_err; return false; }
        put_state();
    }
    return true;
}

/* combine like terms of one side, one power of x per step, then put the
   terms in order */
static void combine_side(side_t *s, const char *where)
{
    int k;
    uint8_t i;
    for (k = MAXK; k >= 0; k--) {
        int first = -1, cnt = 0;
        long sum = 0;
        side_t out;
        for (i = 0; i < s->n; i++) {
            if (s->it[i].m.k != k) continue;
            if (first < 0) first = i;
            cnt++;
            sum += s->it[i].m.c;
        }
        if (cnt < 2 && !(cnt == 1 && sum == 0)) continue;
        if (cnt == 1 && s->n == 1 && k == 0) continue;      /* the side is just the number 0 */
        st_new(EQV);
        if (cnt == 1) {
            sb_s(&tb, "The term 0");
            if (k) { sb_c(&tb, 'x'); if (k > 1) { sb_c(&tb, '^'); sb_n(&tb, k); } }
            sb_s(&tb, where);
            sb_s(&tb, " is just 0, so it disappears.");
        } else {
            bool f = true;
            sb_s(&tb, "Combine the ");
            sb_xk(&tb, k);
            sb_s(&tb, where);
            sb_s(&tb, ": ");
            for (i = 0; i < s->n; i++) {
                if (s->it[i].m.k != k) continue;
                sb_mono(&tb, s->it[i].m.c, k, f);
                f = false;
            }
            sb_s(&tb, " = ");
            sb_mono(&tb, sum, k, true);
            if (sum == 0) sb_s(&tb, " (they cancel out)");
        }
        out.n = 0;
        for (i = 0; i < s->n; i++) {
            if (s->it[i].m.k != k) { out.it[out.n++] = s->it[i]; continue; }
            if ((int)i == first && sum != 0) {
                out.it[out.n] = s->it[i];
                out.it[out.n].m.c = sum;
                out.n++;
            }
        }
        *s = out;
        put_state();
    }
    if (!side_in_order(s)) {
        st_new(EQV);
        sb_s(&tb, "Write the terms");
        sb_s(&tb, where);
        sb_s(&tb, " in order, highest power of x first.");
        side_sort(s);
        put_state();
    }
}

/* ------------------------------------------------------------------ */
/* Linear equations                                                    */
/* ------------------------------------------------------------------ */
static void set_sides(const long *pl, const long *pr)
{
    poly_side(&gL, pl);
    poly_side(&gR, pr);
}

/* subtract the right side's term v*x^k from both sides (cancelling it) */
static void move_term_step(long *pl, long *pr, int k, const char *why)
{
    long v = pr[k];
    st_new(EQ_EQN);
    sb_s(&tb, v > 0 ? "Subtract " : "Add ");
    sb_mono(&tb, v < 0 ? -v : v, k, true);
    sb_s(&tb, v > 0 ? " from" : " to");
    sb_s(&tb, " both sides");
    sb_s(&tb, why);
    pl[k] -= v;
    pr[k] = 0;
    set_sides(pl, pr);
    put_state();
}

static void x_equals(sb_t *b, long n, long d)
{
    sb_s(b, "x = ");
    sb_frac(b, n, d);
}

static bool solve_linear(void)
{
    poly_t pl, pr, e1, e2;
    long a, c, n, d, g, lv, rv;
    int k;
    side_t o1 = gL, o2 = gR;

    steps_reset();
    res.type = PT_LINEAR;
    if (!expand_sides()) return false;
    combine_side(&gL, g_eq ? " on the left side" : "");
    combine_side(&gR, " on the right side");
    if (!side_poly(&gL, pl) || !side_poly(&gR, pr)) { g_fail = cv_err; return false; }

    for (k = MAXK; k >= 2; k--) {
        if (pr[k]) move_term_step(pl, pr, k, ". The terms with the same power of x cancel.");
    }
    if (pr[1]) move_term_step(pl, pr, 1, " to collect all the x terms on the left side.");

    a = pl[1];
    if (a == 0) {
        bool same = pl[0] == pr[0];
        st_new(EQ_EQN);
        sb_s(&tb, "The x terms are gone (or there were none), and what is left is ");
        sb_n(&tb, pl[0]);
        sb_s(&tb, " = ");
        sb_n(&tb, pr[0]);
        sb_s(&tb, same ? ". That is always true, so every number x is a solution."
                       : ". That is never true, so no number x is a solution.");
        set_sides(pl, pr);
        put_state();
        st_final(EQ_NONE);
        sb_s(&tb, same ? "Final answer: every real number" : "Final answer: no solution");
        sb_s(&sbs, same ? "x can be any number" : "no value of x works");
        res.kind = same ? RK_ALL : RK_NOSOL;
        return true;
    }

    if (pl[0]) {
        long v = pl[0];
        st_new(EQ_EQN);
        sb_s(&tb, v > 0 ? "Subtract " : "Add ");
        sb_n(&tb, v < 0 ? -v : v);
        sb_s(&tb, v > 0 ? " from" : " to");
        sb_s(&tb, " both sides to get the x term by itself.");
        pr[0] -= v;
        pl[0] = 0;
        set_sides(pl, pr);
        put_state();
    }
    c = pr[0];
    if (a == 1) {
        n = c;
        d = 1;
    } else if (a == -1) {
        n = -c;
        d = 1;
        st_new(EQ_EQN);
        sb_s(&tb, "Divide both sides by -1 (every sign flips) to get x by itself.");
        x_equals(&sbs, n, d);
    } else {
        n = c;
        d = a;
        if (d < 0) { n = -n; d = -d; }
        st_new(EQ_EQN);
        sb_s(&tb, "Divide both sides by ");
        sb_n(&tb, a);
        sb_s(&tb, " to get x by itself.");
        if (n % d == 0) {
            n /= d;
            d = 1;
            x_equals(&sbs, n, d);
        } else {
            x_equals(&sbs, n, d);
            g = gcdl(n, d);
            if (g > 1) {
                st_new(EQ_EQN);
                sb_s(&tb, "Simplify the fraction: divide the top and the bottom by ");
                sb_n(&tb, g);
                sb_c(&tb, '.');
                n /= g;
                d /= g;
                x_equals(&sbs, n, d);
            }
        }
    }
    /* check an integer answer in the original equation */
    if (d == 1 && side_poly(&o1, e1) && side_poly(&o2, e2) && poly_eval(e1, n, &lv) && poly_eval(e2, n, &rv) && lv == rv) {
        st_new(EQ_NONE);
        sb_s(&tb, "Check: put x = ");
        sb_n(&tb, n);
        sb_s(&tb, " into the original equation. The left side becomes ");
        sb_n(&tb, lv);
        sb_s(&tb, " and the right side becomes ");
        sb_n(&tb, rv);
        sb_s(&tb, ". They are equal, so the answer is right.");
        sb_n(&sbs, lv);
        sb_s(&sbs, " = ");
        sb_n(&sbs, rv);
    }
    st_final(EQ_NONE);
    sb_s(&tb, "Final answer: ");
    x_equals(&tb, n, d);
    x_equals(&sbs, n, d);
    if (d != 1) {
        sb_s(&sbs, "   (about ");
        sb_dec2(&sbs, round_div(n * 100, d));
        sb_c(&sbs, ')');
    }
    res.kind = RK_ONE;
    res.num = n;
    res.den = d;
    return true;
}

/* ------------------------------------------------------------------ */
/* Quadratic equations                                                 */
/* ------------------------------------------------------------------ */
/* "(x + 3)", "(x - 2)" or "x" for the factor x - r (r = -u) */
static void sb_factor(sb_t *b, long u)
{
    if (u == 0) { sb_c(b, 'x'); return; }
    sb_s(b, "(x");
    sb_op(b, u < 0);
    sb_n(b, u < 0 ? -u : u);
    sb_c(b, ')');
}

/* "x + 3 = 0" */
static void sb_factor_eq(sb_t *b, long u)
{
    sb_c(b, 'x');
    if (u != 0) {
        sb_op(b, u < 0);
        sb_n(b, u < 0 ? -u : u);
    }
    sb_s(b, " = 0");
}

/* the root (u + w sqrt(m))/den for the sign sg of the square root term;
   D = f*f*m (m == 1: D is the perfect square f*f) */
static void calc_root(root_t *r, long a, long b, long f, long m, int sg)
{
    long u = -b, w = sg * f, den = 2 * a, g;
    if (m == 1) { u += w; w = 0; }
    if (den < 0) { u = -u; w = -w; den = -den; }
    g = gcdl(gcdl(u, w), den);
    if (g > 1) { u /= g; w /= g; den /= g; }
    r->u = u;
    r->w = w;
    r->m = m;
    r->den = den;
}

static void sb_surd(sb_t *b, const root_t *r)
{
    char tmp[48];
    sb_t t;
    if (r->w == 0) { sb_frac(b, r->u, r->den); return; }
    sb_init(&t, tmp, sizeof tmp);
    if (r->u != 0) {
        sb_n(&t, r->u);
        sb_op(&t, r->w < 0);
    } else if (r->w < 0) {
        sb_c(&t, '-');
    }
    {
        long aw = r->w < 0 ? -r->w : r->w;
        if (aw != 1) sb_n(&t, aw);
        sb_s(&t, "sqrt(");
        sb_n(&t, r->m);
        sb_c(&t, ')');
    }
    if (r->den == 1) {
        sb_s(b, tmp);
    } else if (r->u != 0) {
        sb_c(b, '(');
        sb_s(b, tmp);
        sb_s(b, ")/");
        sb_n(b, r->den);
    } else {
        sb_s(b, tmp);
        sb_c(b, '/');
        sb_n(b, r->den);
    }
}

/* the value of a root times 100, rounded */
static long root_v100(const root_t *r)
{
    long numer = r->u * 1000;
    if (r->w != 0) numer += r->w * sqrt_scaled(r->m, 3);
    return round_div(round_div(numer, r->den), 10);
}

static void sb_approx(sb_t *b, const root_t *r)
{
    sb_s(b, "~ ");
    sb_dec2(b, root_v100(r));
}

static bool solve_quad(void)
{
    poly_t pl, pr;
    long a, b, c, D, sD, b2, fac4, f, m, p, q, r1, r2, u, v, t;
    int k;
    bool rnz = false;

    steps_reset();
    res.type = PT_QUAD;
    if (!expand_sides()) return false;
    combine_side(&gL, " on the left side");
    combine_side(&gR, " on the right side");
    if (!side_poly(&gL, pl) || !side_poly(&gR, pr)) { g_fail = cv_err; return false; }
    for (k = MAXK; k >= 3; k--) {
        if (pr[k]) move_term_step(pl, pr, k, ". The terms with the same power of x cancel.");
    }
    for (k = 0; k <= 2; k++) if (pr[k]) rnz = true;
    if (rnz) {
        st_new(EQ_EQN);
        sb_s(&tb, "Subtract the whole right side from both sides so that the right side is 0. This is the standard form ax^2 + bx + c = 0.");
        for (k = 0; k <= 2; k++) { pl[k] -= pr[k]; pr[k] = 0; }
        set_sides(pl, pr);
        put_state();
    }
    a = pl[2];
    b = pl[1];
    c = pl[0];
    if (a == 0 || a > QLIM || a < -QLIM || b > QLIM || b < -QLIM || c > QLIM || c < -QLIM) {
        g_fail = "For quadratics, a, b and c must be between -10000 and 10000.";
        return false;
    }
    b2 = b * b;
    fac4 = 4 * a * c;
    D = b2 - fac4;

    /* integer roots: factor */
    sD = D >= 0 ? isqrtl(D) : -1;
    if (D >= 0 && sD * sD == D && (-b + sD) % (2 * a) == 0 && (-b - sD) % (2 * a) == 0) {
        long x1 = (-b + sD) / (2 * a), x2 = (-b - sD) / (2 * a);
        if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
        r1 = x1;
        r2 = x2;
        p = b / a;
        q = c / a;
        u = -r1;
        v = -r2;
        if (a != 1) {
            st_new(EQ_EQN);
            sb_s(&tb, "Divide every term by ");
            sb_n(&tb, a);
            sb_s(&tb, " so that the x^2 term has coefficient 1.");
            pl[2] = 1; pl[1] = p; pl[0] = q;
            set_sides(pl, pr);
            put_state();
        }
        st_new(EQ_NONE);
        sb_s(&tb, "Find two numbers that multiply to ");
        sb_n(&tb, q);
        sb_s(&tb, " and add up to ");
        sb_n(&tb, p);
        sb_s(&tb, ". The numbers are ");
        sb_n(&tb, u);
        sb_s(&tb, " and ");
        sb_n(&tb, v);
        sb_c(&tb, '.');
        sb_n(&sbs, u);
        sb_s(&sbs, " * ");
        sb_pn(&sbs, v);
        sb_s(&sbs, " = ");
        sb_n(&sbs, u * v);
        sb_s(&sbs, ",   ");
        sb_n(&sbs, u);
        sb_s(&sbs, " + ");
        sb_pn(&sbs, v);
        sb_s(&sbs, " = ");
        sb_n(&sbs, u + v);

        st_new(EQ_EQN);
        sb_s(&tb, "Write the factors: each of the two numbers goes next to x.");
        if (v == 0) { t = u; u = v; v = t; }       /* a plain x first */
        sb_factor(&sbs, u);
        sb_factor(&sbs, v);
        sb_s(&sbs, " = 0");
        if (v == 0) { t = u; u = v; v = t; }

        st_new(EQ_NONE);
        if (u == v) {
            sb_s(&tb, "If a product is 0, one of its factors has to be 0. Both factors are the same here.");
            sb_factor_eq(&sbs, u);
        } else {
            sb_s(&tb, "If a product is 0, one of its factors has to be 0. So either factor can be set equal to 0.");
            sb_factor_eq(&sbs, u);
            sb_s(&sbs, "   or   ");
            sb_factor_eq(&sbs, v);
        }
        st_new(EQ_NONE);
        sb_s(&tb, "Solve each small equation by moving the number to the other side.");
        sb_s(&sbs, "x = ");
        sb_n(&sbs, r1);
        if (r1 != r2) { sb_s(&sbs, "   or   x = "); sb_n(&sbs, r2); }
        st_final(EQ_NONE);
        sb_s(&tb, "Final answer: x = ");
        sb_n(&tb, r1);
        if (r1 != r2) { sb_s(&tb, " or x = "); sb_n(&tb, r2); }
        else sb_s(&tb, " (a double root)");
        sb_s(&sbs, "x = ");
        sb_n(&sbs, r1);
        if (r1 != r2) { sb_s(&sbs, "   or   x = "); sb_n(&sbs, r2); }
        res.kind = RK_ROOTS;
        res.nroots = r1 == r2 ? 1 : 2;
        res.root[0].u = r1; res.root[0].w = 0; res.root[0].m = 1; res.root[0].den = 1;
        res.root[1].u = r2; res.root[1].w = 0; res.root[1].m = 1; res.root[1].den = 1;
        return true;
    }

    /* the quadratic formula */
    st_new(EQ_EQN);
    sb_s(&tb, "Use the quadratic formula x = (-b +/- sqrt(b^2 - 4ac)) / (2a). Here a = ");
    sb_n(&tb, a);
    sb_s(&tb, ", b = ");
    sb_n(&tb, b);
    sb_s(&tb, " and c = ");
    sb_n(&tb, c);
    sb_c(&tb, '.');
    set_sides(pl, pr);
    put_state();

    st_new(EQ_NONE);
    sb_s(&tb, "First the discriminant D = b^2 - 4ac. It tells us how many real solutions there are.");
    sb_s(&sbs, "D = ");
    sb_pn(&sbs, b);
    sb_s(&sbs, "^2 - 4(");
    sb_n(&sbs, a);
    sb_s(&sbs, ")(");
    sb_n(&sbs, c);
    sb_c(&sbs, ')');
    sb_s(&sbs, " = ");
    sb_n(&sbs, b2);
    sb_s(&sbs, " - ");
    sb_pn(&sbs, fac4);
    sb_s(&sbs, " = ");
    sb_n(&sbs, D);

    if (D < 0) {
        st_new(EQ_NONE);
        sb_s(&tb, "D = ");
        sb_n(&tb, D);
        sb_s(&tb, " is negative, and a negative number has no real square root. So there are no real solutions.");
        sb_s(&sbs, "D < 0");
        st_final(EQ_NONE);
        sb_s(&tb, "Final answer: no real solutions");
        sb_s(&sbs, "(complex roots are not computed)");
        res.kind = RK_NOSOL;
        res.nroots = 0;
        return true;
    }
    if (D == 0) {
        root_t r;
        long n = -b, d = 2 * a, g;
        st_new(EQ_NONE);
        sb_s(&tb, "D = 0, so there is exactly one solution (a double root): x = -b/(2a).");
        sb_s(&sbs, "x = -");
        sb_pn(&sbs, b);
        sb_s(&sbs, "/(2*");
        sb_pn(&sbs, a);
        sb_s(&sbs, ") = ");
        sb_frac(&sbs, n, d);
        if (d < 0) { n = -n; d = -d; }
        g = gcdl(n, d);
        if (g > 1) { n /= g; d /= g; sb_s(&sbs, " = "); sb_frac(&sbs, n, d); }
        calc_root(&r, a, b, 0, 1, 1);
        st_final(EQ_NONE);
        sb_s(&tb, "Final answer: x = ");
        sb_surd(&tb, &r);
        sb_s(&tb, " (a double root)");
        sb_s(&sbs, "x = ");
        sb_surd(&sbs, &r);
        res.kind = RK_ROOTS;
        res.nroots = 1;
        res.root[0] = r;
        res.root[1] = r;
        return true;
    }

    /* D > 0 */
    {
        root_t rt[2];
        int s;
        f = 1;
        m = D;
        if (sD * sD == D) {
            f = sD;
            m = 1;
        } else {
            long j;
            for (j = sD; j >= 2; j--) {
                if (D % (j * j) == 0) { f = j; m = D / (j * j); break; }
            }
        }
        st_new(EQ_NONE);
        sb_s(&tb, "D is positive, so there are two real solutions. Take the square root of D.");
        sb_s(&sbs, "sqrt(");
        sb_n(&sbs, D);
        sb_s(&sbs, ") = ");
        if (m == 1) {
            sb_n(&sbs, f);
        } else if (f > 1) {
            sb_n(&sbs, f);
            sb_s(&sbs, "sqrt(");
            sb_n(&sbs, m);
            sb_c(&sbs, ')');
        } else {
            sb_s(&sbs, "(cannot be simplified)");
        }
        if (m != 1) {
            long s3 = sqrt_scaled(D, 3);
            sb_s(&sbs, ", about ");
            sb_dec2(&sbs, round_div(s3, 10));
        }
        for (s = 0; s < 2; s++) {
            int sg = s == 0 ? 1 : -1;
            long den = 2 * a;
            char t1[56], t2[56];
            sb_t a1, a2;
            calc_root(&rt[s], a, b, f, m, sg);
            sb_init(&a1, t1, sizeof t1);
            sb_init(&a2, t2, sizeof t2);
            st_new(EQ_NONE);
            sb_s(&tb, s == 0 ? "First solution: use + in the formula." : "Second solution: use - in the formula.");
            sb_s(&sbs, s == 0 ? "x1 = " : "x2 = ");
            /* the formula with the numbers put in, before any reducing */
            sb_c(&a1, '(');
            sb_n(&a1, -b);
            sb_op(&a1, sg < 0);
            if (m == 1) {
                sb_n(&a1, f);
            } else {
                if (f > 1) sb_n(&a1, f);
                sb_s(&a1, "sqrt(");
                sb_n(&a1, m);
                sb_c(&a1, ')');
            }
            sb_s(&a1, ")/");
            sb_pn(&a1, den);
            sb_s(&sbs, t1);
            if (m == 1) {
                long nu = -b + sg * f;
                sb_s(&sbs, " = ");
                sb_frac(&sbs, nu, den);
            }
            sb_surd(&a2, &rt[s]);
            {
                /* show the reduced form when it is different from what is already written */
                char t3[56];
                sb_t a3;
                sb_init(&a3, t3, sizeof t3);
                if (m == 1) sb_frac(&a3, -b + sg * f, den);
                else sb_s(&a3, t1);
                if (strcmp(t3, t2) != 0) {
                    sb_s(&sbs, " = ");
                    sb_s(&sbs, t2);
                }
            }
            if (m != 1) {
                sb_c(&sbs, ' ');
                sb_approx(&sbs, &rt[s]);
            }
        }
        st_final(EQ_NONE);
        sb_s(&tb, "Final answer: x = ");
        sb_surd(&tb, &rt[0]);
        sb_s(&tb, " or x = ");
        sb_surd(&tb, &rt[1]);
        sb_s(&sbs, "x = ");
        sb_surd(&sbs, &rt[0]);
        sb_s(&sbs, "   or   x = ");
        sb_surd(&sbs, &rt[1]);
        if (m != 1) {
            sb_s(&sbs, "   (about ");
            sb_dec2(&sbs, root_v100(&rt[0]));
            sb_s(&sbs, " and ");
            sb_dec2(&sbs, root_v100(&rt[1]));
            sb_c(&sbs, ')');
        }
        res.kind = RK_ROOTS;
        res.nroots = 2;
        res.root[0] = rt[0];
        res.root[1] = rt[1];
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Simplifying expressions                                             */
/* ------------------------------------------------------------------ */
static bool solve_simplify(void)
{
    poly_t p;
    steps_reset();
    res.type = PT_SIMPLIFY;
    if (!expand_sides()) return false;
    combine_side(&gL, "");
    if (nsteps == 0) {
        st_new(EQ_EXPR);
        sb_s(&tb, "There are no brackets to expand and no like terms to combine. (Products of plain numbers like 3*4 are worked out directly.)");
        put_state();
    }
    if (!side_poly(&gL, p)) { g_fail = cv_err; return false; }
    st_final(EQ_EXPR);
    sb_s(&tb, "Final answer: ");
    sb_side(&tb, &gL);
    sb_side(&sbs, &gL);
    res.kind = RK_POLY;
    memcpy(res.poly, p, sizeof p);
    return true;
}

/* ------------------------------------------------------------------ */
/* Derivatives                                                         */
/* ------------------------------------------------------------------ */
/* the state while terms 0..upto have been differentiated */
static void deriv_state(const side_t *s, int upto)
{
    uint8_t j;
    for (j = 0; j < s->n; j++) {
        const mono_t *m = &s->it[j].m;
        if ((int)j <= upto) {
            long c = m->k == 0 ? 0 : m->c * m->k;
            sb_mono(&sbs, c, m->k == 0 ? 0 : m->k - 1, j == 0);
        } else {
            if (j > 0) sb_op(&sbs, false);
            sb_s(&sbs, "d/dx[");
            sb_mono(&sbs, m->c, m->k, true);
            sb_c(&sbs, ']');
        }
    }
}

static bool solve_deriv(void)
{
    poly_t dp;
    uint8_t j;
    bool zero = false;
    side_t fin;
    steps_reset();
    res.type = PT_DERIV;
    combine_side(&gL, "");
    if (gL.n == 0) {
        st_new(EQ_NONE);
        sb_s(&tb, "Constant rule: the derivative of 0 (or of any plain number) is 0.");
        sb_s(&sbs, "d/dx[0] = 0");
    } else {
        if (gL.n >= 2) {
            st_new(EQ_NONE);
            sb_s(&tb, "Sum rule: the derivative of a sum is the sum of the derivatives, so differentiate term by term.");
            deriv_state(&gL, -1);
        }
        for (j = 0; j < gL.n; j++) {
            const mono_t *m = &gL.it[j].m;
            long c = m->c;
            int k = m->k;
            st_new(EQ_NONE);
            if (k == 0) {
                sb_s(&tb, "Constant rule: the derivative of a plain number is 0, so d/dx[");
                sb_n(&tb, c);
                sb_s(&tb, "] = 0.");
                zero = true;
            } else if (k == 1) {
                sb_s(&tb, "Power rule with n = 1: d/dx[x] = 1.");
                if (c != 1) {
                    sb_s(&tb, " The constant ");
                    sb_n(&tb, c);
                    sb_s(&tb, " stays, so d/dx[");
                    sb_mono(&tb, c, k, true);
                    sb_s(&tb, "] = ");
                    sb_n(&tb, c);
                    sb_c(&tb, '.');
                }
            } else {
                sb_s(&tb, "Power rule: d/dx[x^n] = n x^(n-1). Here n = ");
                sb_n(&tb, k);
                sb_s(&tb, ", so d/dx[x^");
                sb_n(&tb, k);
                sb_s(&tb, "] = ");
                sb_mono(&tb, k, k - 1, true);
                sb_c(&tb, '.');
                if (c != 1) {
                    sb_s(&tb, " The constant ");
                    sb_n(&tb, c);
                    sb_s(&tb, " stays, so d/dx[");
                    sb_mono(&tb, c, k, true);
                    sb_s(&tb, "] = ");
                    sb_mono(&tb, c * k, k - 1, true);
                    sb_c(&tb, '.');
                }
            }
            deriv_state(&gL, j);
        }
    }
    /* the answer, highest power first */
    memset(dp, 0, sizeof dp);
    for (j = 0; j < gL.n; j++) {
        const mono_t *m = &gL.it[j].m;
        if (m->k >= 1) dp[m->k - 1] += m->c * m->k;
    }
    poly_side(&fin, dp);
    if (zero && gL.n >= 2) {
        st_new(EQ_NONE);
        sb_s(&tb, "Add up the results. A term that is 0 just disappears.");
        sb_side(&sbs, &fin);
    }
    st_final(EQ_DERIV);
    sb_s(&tb, "Final answer: the derivative is ");
    sb_side(&tb, &fin);
    sb_side(&sbs, &fin);
    res.kind = RK_POLY;
    memcpy(res.poly, dp, sizeof dp);
    return true;
}

/* ------------------------------------------------------------------ */
/* Fraction arithmetic                                                 */
/* ------------------------------------------------------------------ */
typedef struct { long n, d; } fr_t;      /* d > 0 */
static int fr_op;                        /* '+', '-', '*', '/' or 0 (one fraction) */
static fr_t fr_a, fr_b;
static const char *fr_err;

static bool int_of(int n, long *v)
{
    if (nd[n].t == N_NUM) { *v = nd[n].v; return true; }
    if (nd[n].t == N_NEG && nd[nd[n].l].t == N_NUM) { *v = -nd[nd[n].l].v; return true; }
    return false;
}

/* an operand: a whole number or a fraction of whole numbers, maybe negative */
static bool frac_of(int n, fr_t *f)
{
    long a, b;
    if (nd[n].t == N_NEG) {
        if (!frac_of(nd[n].l, f)) return false;
        f->n = -f->n;
        return true;
    }
    if (nd[n].t == N_NUM) { f->n = nd[n].v; f->d = 1; return true; }
    if (nd[n].t == N_DIV && int_of(nd[n].l, &a) && int_of(nd[n].r, &b)) {
        if (b == 0) { fr_err = "A denominator is 0, and dividing by 0 is undefined."; return false; }
        if (b < 0) { a = -a; b = -b; }
        f->n = a;
        f->d = b;
        return true;
    }
    return false;
}

static bool frac_shape(int root)
{
    int t = nd[root].t;
    fr_err = 0;
    fr_op = 0;
    if (frac_of(root, &fr_a)) return true;
    if (fr_err) return false;
    if (t == N_DIV) {
        /* 1/2*3/4 reads as ((1/2)*3)/4, which is the same as (1/2)*(3/4) */
        int l = nd[root].l;
        long cv, rv;
        fr_t x;
        if (nd[l].t == N_MUL && int_of(nd[root].r, &rv) && int_of(nd[l].r, &cv) && frac_of(nd[l].l, &x)) {
            if (rv == 0) { fr_err = "A denominator is 0, and dividing by 0 is undefined."; return false; }
            if (rv < 0) { cv = -cv; rv = -rv; }
            fr_a = x;
            fr_b.n = cv;
            fr_b.d = rv;
            fr_op = '*';
            return true;
        }
    }
    if (t != N_ADD && t != N_SUB && t != N_MUL && t != N_DIV) return false;
    if (!frac_of(nd[root].l, &fr_a)) return false;
    if (!frac_of(nd[root].r, &fr_b)) return false;
    fr_op = t == N_ADD ? '+' : t == N_SUB ? '-' : t == N_MUL ? '*' : '/';
    return true;
}

static void sb_fr(sb_t *b, long n, long d)
{
    sb_frac(b, n, d);
}

/* a fraction in brackets when it is negative */
static void sb_pfr(sb_t *b, long n, long d)
{
    if (n < 0) { sb_c(b, '('); sb_fr(b, n, d); sb_c(b, ')'); }
    else sb_fr(b, n, d);
}

static void sb_sym(sb_t *b, char c)
{
    sb_c(b, ' ');
    sb_c(b, c);
    sb_c(b, NB);
}

static void fr_final(long n, long d)
{
    st_final(EQ_NONE);
    sb_s(&tb, "Final answer: ");
    sb_fr(&tb, n, d);
    sb_fr(&sbs, n, d);
    if (d != 1) {
        sb_s(&sbs, "   (about ");
        sb_dec2(&sbs, round_div(n * 100, d));
        sb_c(&sbs, ')');
    }
    res.kind = RK_FRAC;
    res.num = n;
    res.den = d;
}

/* simplify n/d with a step if it can be reduced; returns the reduced value */
static void fr_simplify(long *n, long *d)
{
    long g;
    if (*n == 0) {
        if (*d != 1) {
            st_new(EQ_EXPR);
            sb_s(&tb, "The top is 0, so the whole fraction is 0.");
            sb_s(&sbs, "0");
            *d = 1;
        }
        return;
    }
    g = gcdl(*n, *d);
    if (g > 1) {
        long n2 = *n / g, d2 = *d / g;
        st_new(EQ_EXPR);
        sb_s(&tb, "Simplify: divide the top and the bottom by ");
        sb_n(&tb, g);
        sb_s(&tb, ", the biggest number that goes into both.");
        sb_fr(&sbs, *n, *d);
        sb_s(&sbs, " = ");
        sb_fr(&sbs, n2, d2);
        *n = n2;
        *d = d2;
    }
}

static bool solve_frac(void)
{
    long n, d;
    steps_reset();
    res.type = PT_FRAC;
    if (fr_op == 0) {
        n = fr_a.n;
        d = fr_a.d;
        if (gcdl(n, d) <= 1 && n != 0) {
            st_new(EQ_EXPR);
            sb_s(&tb, "This fraction is already in lowest terms: no number except 1 goes into both the top and the bottom.");
            sb_fr(&sbs, n, d);
        } else {
            fr_simplify(&n, &d);
        }
        fr_final(n, d);
        return true;
    }
    if (fr_op == '+' || fr_op == '-') {
        long L, m1, m2, n1, n2, N;
        bool minus = fr_op == '-';
        if (fr_a.d == fr_b.d) {
            L = fr_a.d;
            m1 = m2 = 1;
            st_new(EQ_EXPR);
            sb_s(&tb, "Both fractions already have the same denominator, ");
            sb_n(&tb, L);
            sb_s(&tb, ", so they can be combined directly.");
            sb_fr(&sbs, fr_a.n, fr_a.d);
            sb_sym(&sbs, minus ? '-' : '+');
            sb_pfr(&sbs, fr_b.n, fr_b.d);
        } else {
            L = fr_a.d / gcdl(fr_a.d, fr_b.d) * fr_b.d;
            m1 = L / fr_a.d;
            m2 = L / fr_b.d;
            st_new(EQ_EXPR);
            sb_s(&tb, "Find a common denominator: the smallest number that both ");
            sb_n(&tb, fr_a.d);
            sb_s(&tb, " and ");
            sb_n(&tb, fr_b.d);
            sb_s(&tb, " divide into is ");
            sb_n(&tb, L);
            sb_c(&tb, '.');
            sb_fr(&sbs, fr_a.n, fr_a.d);
            sb_sym(&sbs, minus ? '-' : '+');
            sb_pfr(&sbs, fr_b.n, fr_b.d);
            n1 = fr_a.n * m1;
            n2 = fr_b.n * m2;
            st_new(EQ_EXPR);
            sb_s(&tb, "Rewrite each fraction with denominator ");
            sb_n(&tb, L);
            sb_s(&tb, ": ");
            if (m1 > 1) {
                sb_s(&tb, "multiply the top and the bottom of ");
                sb_fr(&tb, fr_a.n, fr_a.d);
                sb_s(&tb, " by ");
                sb_n(&tb, m1);
            } else {
                sb_fr(&tb, fr_a.n, fr_a.d);
                sb_s(&tb, " already has it");
            }
            sb_s(&tb, "; ");
            if (m2 > 1) {
                sb_s(&tb, "multiply the top and the bottom of ");
                sb_fr(&tb, fr_b.n, fr_b.d);
                sb_s(&tb, " by ");
                sb_n(&tb, m2);
            } else {
                sb_fr(&tb, fr_b.n, fr_b.d);
                sb_s(&tb, " already has it");
            }
            sb_c(&tb, '.');
            sb_n(&sbs, n1);
            sb_c(&sbs, '/');
            sb_n(&sbs, L);
            sb_sym(&sbs, minus ? '-' : '+');
            if (n2 < 0) sb_c(&sbs, '(');
            sb_n(&sbs, n2);
            sb_c(&sbs, '/');
            sb_n(&sbs, L);
            if (n2 < 0) sb_c(&sbs, ')');
        }
        n1 = fr_a.n * m1;
        n2 = fr_b.n * m2;
        N = minus ? n1 - n2 : n1 + n2;
        st_new(EQ_EXPR);
        sb_s(&tb, minus ? "Subtract the tops and keep the common denominator." : "Add the tops and keep the common denominator.");
        sb_c(&sbs, '(');
        sb_n(&sbs, n1);
        sb_sym(&sbs, minus ? '-' : '+');
        sb_pn(&sbs, n2);
        sb_s(&sbs, ")/");
        sb_n(&sbs, L);
        sb_s(&sbs, " = ");
        sb_fr(&sbs, N, L);
        n = N;
        d = L;
        fr_simplify(&n, &d);
        fr_final(n, d);
        return true;
    }
    if (fr_op == '/') {
        long rn, rd;
        if (fr_b.n == 0) { g_fail = "Dividing by 0 is undefined."; return false; }
        rn = fr_b.d;
        rd = fr_b.n;
        if (rd < 0) { rn = -rn; rd = -rd; }
        st_new(EQ_EXPR);
        sb_s(&tb, "Dividing by a fraction is the same as multiplying by its reciprocal, so flip ");
        sb_fr(&tb, fr_b.n, fr_b.d);
        sb_s(&tb, " upside down and multiply.");
        sb_fr(&sbs, fr_a.n, fr_a.d);
        sb_sym(&sbs, '*');
        sb_pfr(&sbs, rn, rd);
        fr_b.n = rn;
        fr_b.d = rd;
    }
    {
        long N = fr_a.n * fr_b.n, D = fr_a.d * fr_b.d;
        st_new(EQ_EXPR);
        sb_s(&tb, "Multiply the tops together and the bottoms together.");
        sb_c(&sbs, '(');
        sb_n(&sbs, fr_a.n);
        sb_sym(&sbs, '*');
        sb_pn(&sbs, fr_b.n);
        sb_s(&sbs, ")/(");
        sb_n(&sbs, fr_a.d);
        sb_sym(&sbs, '*');
        sb_n(&sbs, fr_b.d);
        sb_s(&sbs, ") = ");
        sb_fr(&sbs, N, D);
        n = N;
        d = D;
        fr_simplify(&n, &d);
        fr_final(n, d);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Classifying and solving                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t type;          /* PT_*, or PT_NONE when it cannot be solved */
    bool ambiguous;        /* simplify or derivative: the student chooses */
    const char *why;       /* the reason, when it cannot be solved */
    int at;                /* parse error position, or -1 */
} cls_t;

static bool classify(const char *in, cls_t *c)
{
    int root;
    side_t L, R;
    poly_t pl, pr, P;
    int deg, k;
    uint8_t i;
    memset(c, 0, sizeof *c);
    c->at = -1;
    root = parse(in);
    if (root < 0) {
        c->why = perr;
        c->at = perr_at;
        return false;
    }
    if (nd[root].t == N_EQ) {
        if (!to_side(&L, nd[root].l) || !to_side(&R, nd[root].r)) { c->why = cv_err; return false; }
        if (!side_poly(&L, pl) || !side_poly(&R, pr)) { c->why = cv_err; return false; }
        for (k = 0; k <= MAXK; k++) P[k] = pl[k] - pr[k];
        deg = poly_deg(P);
        if (deg > 2) { c->why = "Equations with x^3 or higher powers left in them are not supported yet."; return false; }
        c->type = deg == 2 ? PT_QUAD : PT_LINEAR;
        return true;
    }
    if (!has_x(root)) {
        if (has_div(root)) {
            if (frac_shape(root)) { c->type = PT_FRAC; return true; }
            c->why = fr_err ? fr_err : "I can only do two fractions at a time. Write a division like (1/2)/(3/4).";
            return false;
        }
        if (!to_side(&L, root)) { c->why = cv_err; return false; }
        c->type = PT_SIMPLIFY;
        return true;
    }
    if (!to_side(&L, root)) { c->why = cv_err; return false; }
    if (!side_poly(&L, pl)) { c->why = cv_err; return false; }
    c->type = PT_SIMPLIFY;
    c->ambiguous = true;
    for (i = 0; i < L.n; i++) if (L.it[i].nb) c->ambiguous = false;
    return true;
}

/* writes the steps for the problem; false with g_fail set if it cannot */
static bool generate(const char *in, uint8_t type)
{
    int root = parse(in);
    g_fail = 0;
    steps_reset();
    if (root < 0) { g_fail = perr; return false; }
    if (type == PT_FRAC) {
        if (!frac_shape(root)) { g_fail = fr_err ? fr_err : "I can only do two fractions at a time."; return false; }
        return solve_frac();
    }
    g_eq = nd[root].t == N_EQ;
    if (g_eq) {
        if (!to_side(&gL, nd[root].l) || !to_side(&gR, nd[root].r)) { g_fail = cv_err; return false; }
    } else {
        if (!to_side(&gL, root)) { g_fail = cv_err; return false; }
        gR.n = 0;
    }
    switch (type) {
    case PT_LINEAR: return solve_linear();
    case PT_QUAD: return solve_quad();
    case PT_SIMPLIFY: return solve_simplify();
    case PT_DERIV:
        if (first_br(&gL) >= 0) { g_fail = "Derivatives of brackets need the chain or product rule, which is not supported."; return false; }
        return solve_deriv();
    default: g_fail = "I can't solve this type of problem yet."; return false;
    }
}

/* ------------------------------------------------------------------ */
/* Timing and keys                                                     */
/* ------------------------------------------------------------------ */
static long next_tick;
static uint8_t tick_frac;

static void tick_reset(void)
{
    next_tick = (long)clock();
    tick_frac = 0;
}

static bool tick_due(void)
{
    return (long)((long)clock() - next_tick) >= 0;
}

/* a fixed 60 Hz tick: 32768 / 60 = 546 remainder 8 */
static void tick_wait(void)
{
    while (!tick_due()) {
    }
    next_tick += 546;
    tick_frac += 8;
    if (tick_frac >= 60) {
        tick_frac -= 60;
        next_tick++;
    }
    if (tick_due()) tick_reset();
}

static uint8_t keys[8], prev_keys[8];

static void read_keys(void)
{
    uint8_t i;
    memcpy(prev_keys, keys, sizeof keys);
    kb_Scan();
    for (i = 1; i < 8; i++) keys[i] = kb_Data[i];
    keys[0] = 0;
}

#define KDOWN(g, m) ((keys[g] & (m)) != 0)
#define KPRESS(g, m) ((keys[g] & (m)) && !(prev_keys[g] & (m)))

/* a held key repeats: at once, then again every 4 ticks after 14 ticks */
static uint8_t rep_t[5];

static bool rep_key(uint8_t slot, bool down, bool pressed)
{
    if (!down) { rep_t[slot] = 0; return false; }
    if (pressed) { rep_t[slot] = 0; return true; }
    if (rep_t[slot] < 255) rep_t[slot]++;
    return rep_t[slot] >= 14 && ((rep_t[slot] - 14) % 4) == 0;
}

static bool nav_left(void) { return rep_key(0, KDOWN(7, kb_Left), KPRESS(7, kb_Left)); }
static bool nav_right(void) { return rep_key(1, KDOWN(7, kb_Right), KPRESS(7, kb_Right)); }
static bool nav_up(void) { return rep_key(2, KDOWN(7, kb_Up), KPRESS(7, kb_Up)); }
static bool nav_down(void) { return rep_key(3, KDOWN(7, kb_Down), KPRESS(7, kb_Down)); }
static bool key_del(void) { return rep_key(4, KDOWN(1, kb_Del), KPRESS(1, kb_Del)); }
static bool key_enter(void) { return KPRESS(6, kb_Enter); }
static bool key_submit(void) { return KPRESS(6, kb_Enter) || KPRESS(1, kb_Graph); }
static bool key_ok(void) { return KPRESS(6, kb_Enter) || KPRESS(1, kb_2nd); }
static bool key_clear(void) { return KPRESS(6, kb_Clear); }

static void keys_flush(void)
{
    read_keys();
    read_keys();
    memset(rep_t, 0, sizeof rep_t);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */
static bool dirty;

static void fill(int x, int y, int w, int h, uint8_t c)
{
    gfx_SetColor(c);
    gfx_FillRectangle(x, y, w, h);
}

static void txt(const char *s, int x, int y, uint8_t c)
{
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(c);
    gfx_PrintStringXY(s, x, y);
}

static void txt_big(const char *s, int x, int y, uint8_t c, uint8_t sc)
{
    gfx_SetTextScale(sc, sc);
    gfx_SetTextFGColor(c);
    gfx_PrintStringXY(s, x, y);
    gfx_SetTextScale(1, 1);
}

static int txt_w(const char *s)
{
    return (int)strlen(s) * 8;
}

static void txt_c(const char *s, int y, uint8_t c)
{
    txt(s, (SCR_W - txt_w(s)) / 2, y, c);
}

/* Wrap s to `cols` columns and draw up to maxl lines from (x, y), 10 pixels
   apart. Breaks at spaces only (NB never breaks). Returns the number of
   lines the whole text needs. */
static int wrap_draw(const char *s, int x, int y, int cols, int maxl, uint8_t color, bool draw)
{
    int lines = 0;
    char buf[COLS + 2];
    while (*s) {
        int i = 0, last = -1, len, j;
        while (s[i] && i < cols) {
            if (s[i] == ' ') last = i;
            i++;
        }
        if (s[i] == 0 || s[i] == ' ') len = i;
        else if (last > 0) len = last;
        else len = i;
        for (j = 0; j < len && j < COLS; j++) buf[j] = s[j] == NB ? ' ' : s[j];
        buf[j] = 0;
        if (draw && lines < maxl) txt(buf, x, y + lines * 10, color);
        lines++;
        s += len;
        while (*s == ' ') s++;
    }
    return lines;
}

static uint8_t cur_screen;
static void present(void)
{
    TEST_HOOK(test_present(cur_screen));
    gfx_SwapDraw();
}

static void screen_begin(uint8_t id)
{
    cur_screen = id;
    dirty = true;
    keys_flush();
    tick_reset();
}

static void draw_header(const char *right)
{
    fill(0, 0, SCR_W, 16, C_HEAD);
    txt("StepMath CE", 6, 4, C_WHITE);
    if (right) txt(right, SCR_W - 6 - txt_w(right), 4, C_YELLOW);
}

static void draw_bg(void)
{
    gfx_FillScreen(C_BG);
}

/* ------------------------------------------------------------------ */
/* State of the session                                                */
/* ------------------------------------------------------------------ */
static char prob[MAXIN + 1];       /* the typed problem */
static uint8_t plen, pcur;
static uint8_t ptype;              /* the chosen type */

/* the last problems that were solved */
#define HIST_N 5
typedef struct { uint16_t magic; uint8_t n; uint8_t t[HIST_N]; char s[HIST_N][MAXIN + 1]; } hist_t;
#define HIST_MAGIC 0x534D
#define HIST_NAME "STEPHIST"
static hist_t hist;

static void hist_load(void)
{
    uint8_t h = ti_Open(HIST_NAME, "r");
    uint8_t i;
    memset(&hist, 0, sizeof hist);
    if (h) {
        if (ti_Read(&hist, sizeof hist, 1, h) != 1 || hist.magic != HIST_MAGIC) memset(&hist, 0, sizeof hist);
        ti_Close(h);
    }
    hist.magic = HIST_MAGIC;
    if (hist.n > HIST_N) hist.n = 0;
    for (i = 0; i < hist.n; i++) {
        hist.s[i][MAXIN] = 0;
        if (hist.t[i] < PT_LINEAR || hist.t[i] > PT_FRAC || hist.s[i][0] == 0) { hist.n = 0; break; }
    }
}

static void hist_save(bool archive)
{
    uint8_t h = ti_Open(HIST_NAME, "w");
    if (!h) return;
    hist.magic = HIST_MAGIC;
    ti_Write(&hist, sizeof hist, 1, h);
    if (archive) ti_SetArchiveStatus(true, h);
    ti_Close(h);
}

static void hist_remove(uint8_t i)
{
    for (; i + 1 < hist.n; i++) {
        hist.t[i] = hist.t[i + 1];
        memcpy(hist.s[i], hist.s[i + 1], sizeof hist.s[i]);
    }
    if (hist.n) hist.n--;
}

static void hist_add(const char *s, uint8_t type)
{
    uint8_t i;
    for (i = 0; i < hist.n; i++) {
        if (strcmp(hist.s[i], s) == 0) { hist_remove(i); break; }
    }
    if (hist.n >= HIST_N) hist.n = HIST_N - 1;
    for (i = hist.n; i > 0; i--) {
        hist.t[i] = hist.t[i - 1];
        memcpy(hist.s[i], hist.s[i - 1], sizeof hist.s[i]);
    }
    hist.t[0] = type;
    strncpy(hist.s[0], s, MAXIN);
    hist.s[0][MAXIN] = 0;
    hist.n++;
    hist_save(false);
}

/* ------------------------------------------------------------------ */
/* Title and help                                                      */
/* ------------------------------------------------------------------ */
enum { SC_TITLE, SC_INPUT, SC_STEPS, SC_END, SC_HIST, SC_HELP, SC_ERROR, SC_CHOOSE };

static void draw_menu(const char *const *it, uint8_t n, uint8_t sel, int y0, int dy)
{
    uint8_t i;
    for (i = 0; i < n; i++) {
        int y = y0 + i * dy;
        if (i == sel) {
            fill(20, y - 4, 280, dy - 2, C_HEAD);
            txt(">", 28, y, C_YELLOW);
        }
        txt(it[i], 44, y, i == sel ? C_WHITE : C_GRAY);
    }
}

static uint8_t title_sel;

static uint8_t title_screen(void)
{
    static const char *const it[4] = { "New problem", "History (last problems)", "How to type, what it can do", "Quit" };
    screen_begin(SC_TITLE);
    for (;;) {
        tick_wait();
        read_keys();
        if (nav_up() && title_sel > 0) { title_sel--; dirty = true; }
        if (nav_down() && title_sel < 3) { title_sel++; dirty = true; }
        if (key_ok()) return title_sel;
        if (key_clear()) return 3;
        if (dirty) {
            draw_bg();
            fill(0, 0, SCR_W, 56, C_HEAD);
            txt_big("StepMath CE", 28, 12, C_WHITE, 3);
            txt_c("learn the METHOD, one step at a time", 44, C_YELLOW);
            draw_menu(it, 4, title_sel, 80, 24);
            txt_c("Equations, derivatives, fractions...", 190, C_GRAY);
            txt_c("UP/DOWN: choose    ENTER: go", 214, C_GRAY);
            present();
            dirty = false;
        }
    }
}

#define HELP_PAGES 3
static const char *const HELP[HELP_PAGES][10] = {
    { "What I can do", "",
      "1 Linear equation    3x+5=20",
      "2 Quadratic          x^2-5x+6=0",
      "3 Simplify           3(x+2)+4x",
      "4 Derivative d/dx    x^3+2x",
      "5 Fractions          1/2+3/4",
      "                     (1/2)/(3/4)",
      "I show ONE step at a time. Anything",
      "else gets a plain 'cannot solve'." },
    { "How to type", "",
      "0-9, the X key (x), + - * / ^ ( )",
      "(-) also types a minus sign",
      "Y= key types the equals sign =",
      "x^2 key types ^2",
      "3x means 3*x, 2(x+1), x(x-2) too",
      "DEL erases, arrows move the cursor",
      "ENTER or GRAPH solves it",
      "CLEAR clears; again on empty: menu" },
    { "Limits", "",
      "Whole numbers up to 4 digits, no",
      "decimals. Only the letter x.",
      "Quadratics: a, b, c within +-10000.",
      "No complex numbers: D < 0 gives",
      "'no real solutions'.",
      "Derivatives: sums of terms like 3x^2",
      "(no chain, product, quotient rule).",
      "Fractions: two at a time." }
};

static void draw_help(uint8_t page)
{
    uint8_t i;
    char b[24];
    sb_t s;
    draw_bg();
    draw_header("Help");
    txt_big(HELP[page][0], 8, 24, C_YELLOW, 2);
    for (i = 2; i < 10; i++) txt(HELP[page][i], 8, 54 + (i - 2) * 14, C_WHITE);
    sb_init(&s, b, sizeof b);
    sb_s(&s, "Page ");
    sb_n(&s, page + 1);
    sb_c(&s, '/');
    sb_n(&s, HELP_PAGES);
    txt(b, 8, 182, C_GRAY);
    txt("LEFT/RIGHT: page      CLEAR: back", 8, 222, C_GRAY);
}

static void help_screen(void)
{
    uint8_t page = 0;
    screen_begin(SC_HELP);
    for (;;) {
        tick_wait();
        read_keys();
        if (key_clear()) return;
        if (nav_left() && page > 0) { page--; dirty = true; }
        if ((nav_right() || key_ok()) && page + 1 < HELP_PAGES) { page++; dirty = true; }
        if (dirty) {
            draw_help(page);
            present();
            dirty = false;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The input line                                                      */
/* ------------------------------------------------------------------ */
typedef struct { uint8_t g, m; const char *s; } keymap_t;
static const keymap_t KM[] = {
    { 3, kb_0, "0" }, { 3, kb_1, "1" }, { 4, kb_2, "2" }, { 5, kb_3, "3" }, { 3, kb_4, "4" },
    { 4, kb_5, "5" }, { 5, kb_6, "6" }, { 3, kb_7, "7" }, { 4, kb_8, "8" }, { 5, kb_9, "9" },
    { 3, kb_GraphVar, "x" }, { 6, kb_Add, "+" }, { 6, kb_Sub, "-" }, { 5, kb_Chs, "-" },
    { 6, kb_Mul, "*" }, { 6, kb_Div, "/" }, { 6, kb_Power, "^" }, { 4, kb_LParen, "(" },
    { 5, kb_RParen, ")" }, { 1, kb_Yequ, "=" }, { 2, kb_Square, "^2" }
};
#define NKM ((int)(sizeof KM / sizeof KM[0]))

static void input_insert(const char *s)
{
    uint8_t n = (uint8_t)strlen(s);
    if (plen + n > MAXIN) return;
    memmove(prob + pcur + n, prob + pcur, (size_t)(plen - pcur) + 1);
    memcpy(prob + pcur, s, n);
    pcur = (uint8_t)(pcur + n);
    plen = (uint8_t)(plen + n);
}

static void input_delete(void)
{
    if (pcur == 0) return;
    memmove(prob + pcur - 1, prob + pcur, (size_t)(plen - pcur) + 1);
    pcur--;
    plen--;
}

static void draw_input(bool blink)
{
    char buf[8];
    sb_t s;
    draw_bg();
    draw_header("New problem");
    txt("Type your problem:", 8, 24, C_YELLOW);
    fill(4, 38, 312, 22, C_PANEL);
    fill(4, 38, 312, 1, C_GRAY);
    fill(4, 59, 312, 1, C_GRAY);
    txt(prob, 8, 44, C_WHITE);
    if (blink) fill(8 + 8 * pcur, 55, 8, 2, C_YELLOW);
    sb_init(&s, buf, sizeof buf);
    sb_n(&s, plen);
    sb_c(&s, '/');
    sb_n(&s, MAXIN);
    txt(buf, SCR_W - 8 - txt_w(buf), 26, C_GRAY);
    txt("Keys: 0-9  X  + - * / ^ ( )", 8, 72, C_CYAN);
    txt("Y= types =     x^2 key types ^2", 8, 84, C_CYAN);
    txt("DEL erases   arrows move the cursor", 8, 96, C_CYAN);
    txt("Examples:", 8, 116, C_YELLOW);
    txt("3x+5=20        x^2-5x+6=0", 8, 128, C_WHITE);
    txt("3(x+2)+4x      x^3+2x (d/dx)", 8, 140, C_WHITE);
    txt("1/2+3/4        (1/2)/(3/4)", 8, 152, C_WHITE);
    txt("ENTER or GRAPH: solve", 8, 184, C_GREEN);
    txt("CLEAR: clear the line (empty: menu)", 8, 198, C_GRAY);
}

/* returns true to solve, false to go back to the menu */
static bool input_screen(void)
{
    uint8_t blink = 0;
    screen_begin(SC_INPUT);
    for (;;) {
        int i;
        tick_wait();
        read_keys();
        if (++blink >= 30) { blink = 0; dirty = true; }
        for (i = 0; i < NKM; i++) {
            if (KPRESS(KM[i].g, KM[i].m)) { input_insert(KM[i].s); dirty = true; blink = 0; }
        }
        if (key_del()) { input_delete(); dirty = true; blink = 0; }
        if (nav_left() && pcur > 0) { pcur--; dirty = true; blink = 0; }
        if (nav_right() && pcur < plen) { pcur++; dirty = true; blink = 0; }
        if (key_clear()) {
            if (plen == 0) return false;
            prob[0] = 0;
            plen = pcur = 0;
            dirty = true;
        }
        if (key_submit() && plen > 0) return true;
        if (dirty) {
            draw_input(blink < 15);
            present();
            dirty = false;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Messages and choices                                                */
/* ------------------------------------------------------------------ */
static void message_screen(const char *head, const char *why, int at)
{
    screen_begin(SC_ERROR);
    for (;;) {
        tick_wait();
        read_keys();
        if (key_ok() || key_clear()) return;
        if (dirty) {
            draw_bg();
            draw_header("Oops");
            fill(4, 24, 312, 24, C_DKGRAY);
            txt(head, 10, 32, C_RED);
            txt("Your problem:", 8, 58, C_GRAY);
            txt(prob, 8, 70, C_WHITE);
            if (at >= 0 && at <= (int)plen) txt("^", 8 + 8 * at, 80, C_RED);
            if (why) wrap_draw(why, 8, 100, COLS, 6, C_YELLOW, true);
            txt("ENTER: back to the problem", 8, 214, C_GRAY);
            present();
            dirty = false;
        }
    }
}

/* simplify or derivative? returns PT_NONE if cancelled */
static uint8_t choose_screen(void)
{
    static const char *const it[2] = { "Simplify the expression", "Take the derivative d/dx" };
    uint8_t sel = 0;
    screen_begin(SC_CHOOSE);
    for (;;) {
        tick_wait();
        read_keys();
        if (nav_up() && sel > 0) { sel--; dirty = true; }
        if (nav_down() && sel < 1) { sel++; dirty = true; }
        if (key_ok()) return sel == 0 ? PT_SIMPLIFY : PT_DERIV;
        if (key_clear()) return PT_NONE;
        if (dirty) {
            draw_bg();
            draw_header("Which one?");
            txt("Your problem:", 8, 28, C_GRAY);
            txt(prob, 8, 40, C_WHITE);
            wrap_draw("This has x but no = sign, so it could be two kinds of problem. What would you like?", 8, 64, COLS, 4, C_YELLOW, true);
            draw_menu(it, 2, sel, 114, 26);
            txt("UP/DOWN, ENTER: choose   CLEAR: back", 8, 214, C_GRAY);
            present();
            dirty = false;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Showing the steps                                                   */
/* ------------------------------------------------------------------ */
static void draw_steps(int cur)
{
    char b[24];
    sb_t s;
    const step_t *st = &steps[cur];
    draw_bg();
    draw_header(PT_NAME[ptype]);
    txt("Problem:", 8, 20, C_GRAY);
    txt(prob, 8, 30, C_WHITE);
    fill(4, 43, 312, 1, C_DKGRAY);
    sb_init(&s, b, sizeof b);
    if (st->fin) sb_s(&s, "Final answer");
    else { sb_s(&s, "Step "); sb_n(&s, cur + 1); }
    txt(b, 8, 49, st->fin ? C_GREEN : C_YELLOW);
    if (cur > 0) txt("<- back", SCR_W - 8 - 7 * 8, 49, C_GRAY);
    wrap_draw(st->text, 8, 64, COLS, 7, C_WHITE, true);
    fill(4, 144, 312, 64, C_PANEL);
    fill(4, 144, 312, 1, st->fin ? C_GREEN : C_GRAY);
    wrap_draw(st->state, 8, 151, COLS, 5, st->fin ? C_GREEN : C_CYAN, true);
    txt(st->fin ? "ENTER: what next?" : "ENTER or -> : next step", 8, 214, C_GREEN);
    txt("<-: back   CLEAR: edit problem", 8, 226, C_GRAY);
}

/* what to do after the last step: 0 new problem, 1 again, 2 menu, 3 back to the steps */
static uint8_t end_screen(void)
{
    static const char *const it[3] = { "Try another problem", "Show the full solution again", "Back to the main menu" };
    uint8_t sel = 0;
    screen_begin(SC_END);
    for (;;) {
        tick_wait();
        read_keys();
        if (nav_up() && sel > 0) { sel--; dirty = true; }
        if (nav_down() && sel < 2) { sel++; dirty = true; }
        if (key_ok()) return sel;
        if (key_clear() || nav_left()) return 3;
        if (dirty) {
            draw_bg();
            draw_header("Done!");
            txt("Problem:", 8, 28, C_GRAY);
            txt(prob, 8, 40, C_WHITE);
            wrap_draw(steps[nsteps - 1].state, 8, 62, COLS, 3, C_GREEN, true);
            draw_menu(it, 3, sel, 108, 26);
            txt("UP/DOWN, ENTER: choose   LEFT: back", 8, 214, C_GRAY);
            present();
            dirty = false;
        }
    }
}

enum { RUN_EDIT, RUN_NEW, RUN_MENU };

static uint8_t steps_view(void)
{
    int cur = 0;
    screen_begin(SC_STEPS);
    for (;;) {
        tick_wait();
        read_keys();
        if (key_clear()) return RUN_EDIT;
        if (nav_left() || nav_up()) { if (cur > 0) { cur--; dirty = true; } }
        if (nav_right() || nav_down() || key_enter() || KPRESS(1, kb_2nd)) {
            if (cur + 1 < nsteps) {
                cur++;
                dirty = true;
            } else {
                uint8_t r = end_screen();
                if (r == 0) return RUN_NEW;
                if (r == 2) return RUN_MENU;
                if (r == 1) cur = 0;
                screen_begin(SC_STEPS);
            }
        }
        if (dirty) {
            draw_steps(cur);
            present();
            dirty = false;
        }
    }
}

/* solve prob (type 0: work it out) and show the steps */
static uint8_t run_problem(uint8_t type)
{
    cls_t c;
    if (type == PT_NONE) {
        if (!classify(prob, &c)) {
            if (c.at >= 0) message_screen("I can't read that problem.", c.why, c.at);
            else message_screen("I can't solve this type of problem yet", c.why, -1);
            return RUN_EDIT;
        }
        type = c.type;
        if (c.ambiguous) {
            type = choose_screen();
            if (type == PT_NONE) return RUN_EDIT;
        }
    }
    ptype = type;
    if (!generate(prob, type)) {
        message_screen("I can't solve this type of problem yet", g_fail, -1);
        return RUN_EDIT;
    }
    hist_add(prob, type);
    return steps_view();
}

/* ------------------------------------------------------------------ */
/* History                                                             */
/* ------------------------------------------------------------------ */
static void draw_hist(uint8_t sel)
{
    uint8_t i;
    draw_bg();
    draw_header("History");
    if (hist.n == 0) {
        txt_c("No problems solved yet.", 80, C_GRAY);
    } else {
        for (i = 0; i < hist.n; i++) {
            int y = 30 + i * 30;
            if (i == sel) fill(4, y - 4, 312, 28, C_HEAD);
            txt(hist.s[i], 12, y, i == sel ? C_WHITE : C_GRAY);
            txt(PT_NAME[hist.t[i]], 12, y + 12, C_CYAN);
        }
    }
    txt("ENTER: solve it again   DEL: remove", 8, 214, C_GRAY);
    txt("CLEAR: back", 8, 226, C_GRAY);
}

/* returns RUN_* when a problem was opened, or RUN_EDIT-less 255 to go back */
static uint8_t history_screen(void)
{
    uint8_t sel = 0;
    screen_begin(SC_HIST);
    for (;;) {
        tick_wait();
        read_keys();
        if (key_clear()) return 255;
        if (nav_up() && sel > 0) { sel--; dirty = true; }
        if (nav_down() && sel + 1 < hist.n) { sel++; dirty = true; }
        if (key_del() && hist.n > 0) {
            hist_remove(sel);
            hist_save(false);
            if (sel >= hist.n && sel > 0) sel--;
            dirty = true;
        }
        if (key_ok() && hist.n > 0) {
            uint8_t r;
            strncpy(prob, hist.s[sel], MAXIN);
            prob[MAXIN] = 0;
            plen = pcur = (uint8_t)strlen(prob);
            r = run_problem(hist.t[sel]);
            if (r != RUN_EDIT) return r;
            screen_begin(SC_HIST);
        }
        if (dirty) {
            draw_hist(sel);
            present();
            dirty = false;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    uint8_t m;
    gfx_Begin();
    build_palette();
    gfx_SetPalette(pal, sizeof pal, 0);
    gfx_SetDrawBuffer();
    gfx_SetTransparentColor(C_TRANSP);
    gfx_SetTextTransparentColor(C_TRANSP);
    gfx_SetTextBGColor(C_TRANSP);
    gfx_SetMonospaceFont(8);
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    hist_load();
    prob[0] = 0;
    plen = pcur = 0;
    tick_reset();
    for (;;) {
        m = title_screen();
        if (m == 1) {
            /* "Try another problem" from the history goes straight to a new input line */
            if (history_screen() == RUN_NEW) { prob[0] = 0; plen = pcur = 0; m = 0; }
        }
        if (m == 0) {
            for (;;) {
                uint8_t r;
                if (!input_screen()) break;
                r = run_problem(PT_NONE);
                if (r == RUN_NEW) { prob[0] = 0; plen = pcur = 0; }
                else if (r == RUN_MENU) break;
            }
        } else if (m == 2) {
            help_screen();
        } else if (m == 3) {
            break;
        }
    }
    hist_save(true);
    gfx_End();
    return 0;
}
