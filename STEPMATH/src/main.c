/*
 * StepMath CE  (v2)
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
 *   6. Square roots typed with sqrt( : an expression such as the quadratic
 *      formula with the numbers put in, (-5+sqrt(5^2-4*1*6))/(2*1), worked
 *      out one operation at a time; inside equations sqrt( works on
 *      perfect squares. sqrt( is typed with 2nd then the x^2 key.
 *   7. Venn diagrams with 2 or 3 sets: a form for the numbers, the regions
 *      filled in one per step (and drawn), then a set expression such as
 *      n(A u B)' n C evaluated (u union, n intersection, ' complement).
 *
 * KEYS THAT ARE NOT PLAIN
 *   2nd lights up (shown on screen) and goes off after the next key; 2nd then
 *   x^2 types sqrt( . CLEAR erases a character, MODE clears the line.
 *   DEL quits the program at once from any screen (longjmp to main, which
 *   calls gfx_End()); nothing is saved.
 *
 * INPUT GRAMMAR (one line, at most 38 characters)
 *   expr   := term { (+|-) term }
 *   term   := unary { (*|/) unary  |  implicit multiplication }
 *   unary  := - unary | + unary | power
 *   power  := atom [ ^ whole-number ]
 *   atom   := whole number (at most 4 digits) | x | ( expr ) | sqrt( expr )
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
#include <setjmp.h>
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
    C_GRAY, C_BLACK, C_DKGRAY, C_ORANGE,
    C_VR1, C_VR2, C_VR3, C_VR4, C_VR5, C_VR6, C_VR7,      /* Venn regions by membership mask 1..7 */
    C_VUNK, C_VHL, C_VHL0, C_BOX, C_COUNT
};
#define C_VR(m) ((uint8_t)(C_VR1 + (m) - 1))
#define RGB(r, g, b) gfx_RGBTo1555(r, g, b)
static uint16_t pal[C_COUNT];

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
    pal[C_VR1] = RGB(60, 110, 200);      /* A only */
    pal[C_VR2] = RGB(60, 170, 100);      /* B only */
    pal[C_VR3] = RGB(60, 160, 170);      /* A and B */
    pal[C_VR4] = RGB(210, 130, 50);      /* C only */
    pal[C_VR5] = RGB(135, 120, 125);     /* A and C */
    pal[C_VR6] = RGB(135, 150, 75);      /* B and C */
    pal[C_VR7] = RGB(150, 150, 175);     /* A, B and C */
    pal[C_VUNK] = RGB(44, 54, 84);       /* not filled in yet */
    pal[C_VHL] = RGB(255, 215, 70);      /* highlighted region */
    pal[C_VHL0] = RGB(110, 95, 30);      /* highlighted outside region */
    pal[C_BOX] = RGB(22, 30, 52);        /* inside the universal box, outside the circles */
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
enum { N_NUM, N_X, N_NEG, N_ADD, N_SUB, N_MUL, N_DIV, N_POW, N_EQ, N_SQRT, N_VAL };
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
    if (pp[0] == 's' && pp[1] == 'q' && pp[2] == 'r' && pp[3] == 't' && pp[4] == '(') {
        int e;
        pp += 5;
        if (++pdepth > 16) { fail_at("Too many brackets inside brackets."); return -1; }
        e = p_expr();
        pdepth--;
        if (e < 0) return -1;
        skipsp();
        if (*pp != ')') { fail_at("The closing bracket ) of sqrt( is missing."); return -1; }
        pp++;
        return mk(N_SQRT, e, -1, 0);
    }
    if (*pp == 0) fail_at("The problem ends too early.");
    else if (*pp == ')') fail_at("There is a ) without a ( before it.");
    else fail_at("I expected a number, x, ( or sqrt( here.");
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
        } else if (c == 'x' || c == 'X' || c == '(' || c == 's') {
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

static bool has_sqrt(int n)
{
    if (n < 0) return false;
    if (nd[n].t == N_SQRT) return true;
    if (nd[n].t == N_NUM || nd[n].t == N_X) return false;
    return has_sqrt(nd[n].l) || has_sqrt(nd[n].r);
}

/* The whole-number value of an expression made of numbers, + - * ^ and
   square roots of perfect squares (used for sqrt( inside an equation). */
#define CILIM 100000000L
static const char *ci_err;
static bool ci_eval(int n, long *v)
{
    long a, b;
    const node_t *q;
    if (n < 0) return false;
    q = &nd[n];
    switch (q->t) {
    case N_NUM:
        *v = q->v;
        return true;
    case N_NEG:
        if (!ci_eval(q->l, &a)) return false;
        *v = -a;
        return true;
    case N_ADD:
    case N_SUB:
    case N_MUL:
        if (!ci_eval(q->l, &a) || !ci_eval(q->r, &b)) return false;
        if (q->t == N_ADD) {
            *v = a + b;
        } else if (q->t == N_SUB) {
            *v = a - b;
        } else {
            long aa = a < 0 ? -a : a, bb = b < 0 ? -b : b;
            if (bb != 0 && aa > CILIM / bb) { ci_err = "Those numbers get too big for me."; return false; }
            *v = a * b;
        }
        if (*v > CILIM || *v < -CILIM) { ci_err = "Those numbers get too big for me."; return false; }
        return true;
    case N_POW: {
        long k = nd[q->r].v, r = 1, i;
        if (!ci_eval(q->l, &a)) return false;
        for (i = 0; i < k; i++) {
            long ar = r < 0 ? -r : r, aa = a < 0 ? -a : a;
            if (aa != 0 && ar > CILIM / aa) { ci_err = "Those numbers get too big for me."; return false; }
            r *= a;
        }
        *v = r;
        return true;
    }
    case N_SQRT: {
        long s;
        if (!ci_eval(q->l, &a)) return false;
        if (a < 0) { ci_err = "There is a square root of a negative number, which has no real value."; return false; }
        s = isqrtl(a);
        if (s * s != a) {
            ci_err = "With x in the problem, sqrt( only works on perfect squares like sqrt(25).";
            return false;
        }
        *v = s;
        return true;
    }
    default:
        ci_err = "Inside sqrt( here I can only use whole-number sums, products and powers.";
        return false;
    }
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
    case N_SQRT: {
        long v;
        if (has_x(q->l)) { cv_err = "A square root of x is not supported."; return false; }
        ci_err = 0;
        if (!ci_eval(n, &v)) { cv_err = ci_err ? ci_err : "I can't use that square root."; return false; }
        return mul_c(&it->m.c, v);
    }
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
enum { PT_NONE, PT_LINEAR, PT_QUAD, PT_SIMPLIFY, PT_DERIV, PT_FRAC, PT_RADICAL, PT_VENN };
static const char *const PT_NAME[] = {
    "", "Linear equation", "Quadratic equation", "Simplify expression", "Derivative d/dx", "Fraction arithmetic",
    "Square roots", "Venn diagram"
};

/* what a step's state line is, so that the tests can check it */
enum { EQ_NONE, EQ_EQN, EQ_EXPR, EQ_DERIV };
#define MAXSTEPS 32
#define STEXT 220
#define SSTATE 240
typedef struct { char text[STEXT]; char state[SSTATE]; uint8_t eqv; uint8_t fin; uint8_t vfill, vhl; } step_t;
static step_t steps[MAXSTEPS];
static int nsteps;
static step_t dummy_step;
static step_t *st_ptr;          /* the step being written */
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
    s->vfill = 0;
    s->vhl = 0;
    st_ptr = s;
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
/* Expressions with square roots (the quadratic formula typed in)      */
/* ------------------------------------------------------------------ */
/* Typing sqrt( is what makes this possible: the student can type the
 * quadratic formula with the numbers put in, for example
 *     (-5+sqrt(5^2-4*1*6))/(2*1)
 * and sees it worked out one operation at a time: the numbers under the
 * root, the root itself (perfect square / simplified / left as it is), the
 * sums and the division, then the answer with a decimal estimate.
 * A value is (u + w*sqrt(m)) / den in lowest terms (den > 0; w = 0 for a
 * plain number or fraction). Only ONE kind of root can be mixed in one
 * expression, and a root can only be taken of a whole number. Every
 * finished operation turns its node into an N_VAL node. */
#define VLIM 1000000000L
#define MAXRV 64
static root_t rvals[MAXRV];
static int nrv, rd_root;
static const char *rd_err;

static bool vmul(long a, long b, long *r)
{
    long aa = a < 0 ? -a : a, bb = b < 0 ? -b : b;
    if (aa != 0 && bb > VLIM / aa) return false;
    *r = a * b;
    return true;
}

static bool vok(long v)
{
    return v <= VLIM && v >= -VLIM;
}

static void rv_norm(root_t *r)
{
    long g;
    if (r->den < 0) { r->u = -r->u; r->w = -r->w; r->den = -r->den; }
    if (r->w == 0) r->m = 1;
    g = gcdl(gcdl(r->u, r->w), r->den);
    if (g > 1) { r->u /= g; r->w /= g; r->den /= g; }
}

static void rv_int(root_t *r, long n)
{
    r->u = n;
    r->w = 0;
    r->m = 1;
    r->den = 1;
}

#define RV_BIG (rd_err = "The numbers get too big for me.", false)

/* a + sg * b */
static bool rv_add(root_t *r, const root_t *a, const root_t *b, int sg)
{
    long l, fa, fb, t1, t2, t3, t4, m;
    if (a->w != 0 && b->w != 0 && a->m != b->m) {
        rd_err = "I can only combine square roots of the same number.";
        return false;
    }
    m = a->w != 0 ? a->m : b->m;
    l = a->den / gcdl(a->den, b->den);
    if (!vmul(l, b->den, &l)) return RV_BIG;
    fa = l / a->den;
    fb = l / b->den;
    if (!vmul(a->u, fa, &t1) || !vmul(b->u, fb, &t2) || !vmul(a->w, fa, &t3) || !vmul(b->w, fb, &t4)) return RV_BIG;
    r->u = t1 + sg * t2;
    r->w = t3 + sg * t4;
    r->m = m;
    r->den = l;
    if (!vok(r->u) || !vok(r->w)) return RV_BIG;
    rv_norm(r);
    return true;
}

static bool rv_mul(root_t *r, const root_t *a, const root_t *b)
{
    long t1, t2, t3, t4, t5, m;
    if (a->w != 0 && b->w != 0 && a->m != b->m) {
        rd_err = "I can only combine square roots of the same number.";
        return false;
    }
    m = a->w != 0 ? a->m : (b->w != 0 ? b->m : 1);
    if (!vmul(a->u, b->u, &t1) || !vmul(a->w, b->w, &t2) || !vmul(t2, m, &t2)) return RV_BIG;
    if (!vmul(a->u, b->w, &t3) || !vmul(a->w, b->u, &t4) || !vmul(a->den, b->den, &t5)) return RV_BIG;
    r->u = t1 + t2;
    r->w = t3 + t4;
    r->m = m;
    r->den = t5;
    if (!vok(r->u) || !vok(r->w)) return RV_BIG;
    rv_norm(r);
    return true;
}

static bool rv_div(root_t *r, const root_t *a, const root_t *b)
{
    root_t inv;
    if (b->u == 0 && b->w == 0) {
        rd_err = "Dividing by 0 is undefined.";
        return false;
    }
    if (b->w == 0) {
        inv.u = b->den;
        inv.w = 0;
        inv.m = 1;
        inv.den = b->u;
    } else {
        /* 1/b = den (u - w sqrt(m)) / (u^2 - w^2 m) */
        long uu, ww;
        if (!vmul(b->u, b->u, &uu) || !vmul(b->w, b->w, &ww) || !vmul(ww, b->m, &ww)) return RV_BIG;
        if (!vmul(b->den, b->u, &inv.u) || !vmul(b->den, b->w, &inv.w)) return RV_BIG;
        inv.w = -inv.w;
        inv.m = b->m;
        inv.den = uu - ww;
    }
    rv_norm(&inv);
    return rv_mul(r, a, &inv);
}

/* n = f*f*m with m as small as possible (n > 0) */
static void rad_split(long n, long *f, long *m)
{
    long k;
    *f = 1;
    *m = n;
    for (k = 2; k * k <= *m; k++) {
        while (*m % (k * k) == 0) {
            *m /= k * k;
            *f *= k;
        }
    }
}

/* the value times 100, rounded; false if it is too big to estimate */
static bool rv_v100(const root_t *r, long *out)
{
    long numer, s, t;
    if (r->w == 0) {
        if (!vmul(r->u, 100, &numer)) return false;
        *out = round_div(numer, r->den);
        return true;
    }
    if (r->m > 500000000L) return false;
    s = sqrt_scaled(r->m, 3);
    if (!vmul(r->w, s, &t) || !vmul(r->u, 1000, &numer)) return false;
    numer += t;
    *out = round_div(round_div(numer, r->den), 10);
    return true;
}

/* a value as text: 5, -3/4, sqrt(17), (-3 + sqrt(17))/4 */
static void sb_rv(sb_t *b, const root_t *r)
{
    if (r->w == 0) sb_frac(b, r->u, r->den);
    else sb_surd(b, r);
}

static void sb_rv_approx(sb_t *b, const root_t *r)
{
    long v;
    if ((r->w != 0 || r->den != 1) && rv_v100(r, &v)) {
        sb_s(b, "   (about ");
        sb_dec2(b, v);
        sb_c(b, ')');
    }
}

/* the tree as text; ctx: 0 plain, 1 right of + or -, 2 left of * or /, 3 base of ^ or under a -, 4 right of * or / */
static void sb_ast(sb_t *b, int n, int ctx)
{
    const node_t *q = &nd[n];
    bool br;
    switch (q->t) {
    case N_NUM:
        sb_n(b, q->v);
        break;
    case N_VAL: {
        const root_t *r = &rvals[q->v];
        bool neg = r->w == 0 ? r->u < 0 : (r->u < 0 || (r->u == 0 && r->w < 0));
        bool frac = r->den != 1;
        bool sum = r->w != 0 && r->u != 0 && r->den == 1;
        bool coef = r->w != 0 && (r->w != 1 || r->u != 0);
        br = ctx == 1 ? neg : (ctx == 2 || ctx == 4) ? (neg || sum || frac) : ctx == 3 ? (neg || sum || frac || coef) : false;
        if (br) sb_c(b, '(');
        sb_rv(b, r);
        if (br) sb_c(b, ')');
        break;
    }
    case N_NEG:
        br = ctx != 0;
        if (br) sb_c(b, '(');
        sb_c(b, '-');
        sb_ast(b, q->l, 3);
        if (br) sb_c(b, ')');
        break;
    case N_SQRT:
        sb_s(b, "sqrt(");
        sb_ast(b, q->l, 0);
        sb_c(b, ')');
        break;
    case N_POW:
        sb_ast(b, q->l, 3);
        sb_c(b, '^');
        sb_ast(b, q->r, 0);
        break;
    case N_ADD:
    case N_SUB:
        br = ctx != 0;
        if (br) sb_c(b, '(');
        sb_ast(b, q->l, 0);
        sb_op(b, q->t == N_SUB);
        sb_ast(b, q->r, 1);
        if (br) sb_c(b, ')');
        break;
    default:                      /* N_MUL, N_DIV */
        br = ctx >= 3;
        if (br) sb_c(b, '(');
        sb_ast(b, q->l, 2);
        sb_sym(b, q->t == N_MUL ? '*' : '/');
        sb_ast(b, q->r, 4);
        if (br) sb_c(b, ')');
        break;
    }
}

static void rd_leaf(int n, root_t *v)
{
    if (nd[n].t == N_VAL) *v = rvals[nd[n].v];
    else rv_int(v, nd[n].v);
}

/* the first operation (left to right, innermost first) whose operands are
   plain values, or -1 when the whole expression is one value */
static int rd_find(int n)
{
    const node_t *q = &nd[n];
    int r;
    if (q->t == N_NUM || q->t == N_VAL) return -1;
    r = rd_find(q->l);
    if (r >= 0) return r;
    if (q->t != N_NEG && q->t != N_SQRT) {
        r = rd_find(q->r);
        if (r >= 0) return r;
    }
    return n;
}

static bool rd_set(int n, const root_t *v)
{
    if (nrv >= MAXRV) { rd_err = "That expression is too long for me."; return false; }
    rvals[nrv] = *v;
    nd[n].t = N_VAL;
    nd[n].v = nrv++;
    nd[n].l = nd[n].r = -1;
    return true;
}

/* the same text apart from spaces */
static bool same_text(const char *a, const char *b)
{
    for (;;) {
        while (*a == ' ' || *a == NB) a++;
        while (*b == ' ' || *b == NB) b++;
        if (*a != *b) return false;
        if (*a == 0) return true;
        a++;
        b++;
    }
}

/* one operation: write its step (or fold it without a step when nothing visible changes) */
static bool rd_step(int n)
{
    node_t *q = &nd[n];
    root_t a, b, r;
    char before[96], after[96];
    sb_t sb1, sb2;
    const char *verb = "";
    int t = q->t;
    long f = 1, m = 1;
    bool root_step = false;
    sb_init(&sb1, before, sizeof before);
    sb_ast(&sb1, n, 0);
    rd_leaf(q->l, &a);
    if (t == N_SQRT) {
        long s;
        if (a.w != 0 || a.den != 1) { rd_err = "I can only take the square root of a whole number."; return false; }
        if (a.u < 0) { rd_err = "The number under a square root is negative, so there is no real square root."; return false; }
        s = isqrtl(a.u);
        root_step = true;
        if (s * s == a.u) {
            rv_int(&r, s);
        } else {
            rad_split(a.u, &f, &m);
            r.u = 0;
            r.w = f;
            r.m = m;
            r.den = 1;
        }
        verb = "Square root: ";
    } else {
        bool ok = false;
        rd_leaf(q->r, &b);
        switch (t) {
        case N_ADD: ok = rv_add(&r, &a, &b, 1); verb = "Add: "; break;
        case N_SUB: ok = rv_add(&r, &a, &b, -1); verb = "Subtract: "; break;
        case N_MUL: ok = rv_mul(&r, &a, &b); verb = "Multiply: "; break;
        case N_DIV: ok = rv_div(&r, &a, &b); verb = "Divide: "; break;
        default: {                            /* N_POW */
            long k = nd[q->r].v, i;
            rv_int(&r, 1);
            ok = true;
            for (i = 0; i < k && ok; i++) {
                root_t t2 = r;
                ok = rv_mul(&r, &t2, &a);
            }
            verb = "Power: ";
            break;
        }
        }
        if (!ok) return false;
    }
    sb_init(&sb2, after, sizeof after);
    sb_rv(&sb2, &r);
    if (!root_step) {
        /* the finished operation as it would be written: compare ignoring spaces */
        char shown[96];
        sb_t sb3;
        root_t keep = r;
        sb_init(&sb3, shown, sizeof shown);
        sb_rv(&sb3, &keep);
        if (same_text(before, shown)) {
            return rd_set(n, &r);                       /* nothing visible happens: no step */
        }
        /* a plain number and a root cannot be combined: it just stays as it is */
        if ((t == N_ADD || t == N_SUB) && (a.w == 0) != (b.w == 0) && (a.w == 0 ? a.u != 0 : b.u != 0) && keep.w != 0) {
            return rd_set(n, &r);
        }
    }
    if (!rd_set(n, &r)) return false;
    st_new(EQ_EXPR);
    sb_s(&tb, verb);
    if (root_step) {
        if (r.w == 0) {
            sb_s(&tb, before);
            sb_s(&tb, " = ");
            sb_s(&tb, after);
            sb_s(&tb, ", because ");
            sb_n(&tb, r.u);
            sb_s(&tb, " * ");
            sb_n(&tb, r.u);
            sb_s(&tb, " = ");
            sb_n(&tb, a.u);
            sb_c(&tb, '.');
        } else if (f > 1) {
            sb_s(&tb, before);
            sb_s(&tb, " = ");
            sb_s(&tb, after);
            sb_s(&tb, ". Pull out the square factor: ");
            sb_n(&tb, a.u);
            sb_s(&tb, " = ");
            sb_n(&tb, f * f);
            sb_s(&tb, " * ");
            sb_n(&tb, m);
            sb_c(&tb, '.');
        } else {
            sb_s(&tb, before);
            sb_s(&tb, " is not a perfect square and has no square factor, so it stays as it is");
            if (rv_v100(&r, &f)) {
                sb_s(&tb, " (about ");
                sb_dec2(&tb, f);
                sb_c(&tb, ')');
            }
            sb_c(&tb, '.');
        }
    } else {
        sb_s(&tb, before);
        sb_s(&tb, " = ");
        sb_s(&tb, after);
        sb_c(&tb, '.');
    }
    sb_ast(&sbs, rd_root, 0);
    return true;
}

static bool solve_radical(int root)
{
    int guard = 0;
    root_t v;
    steps_reset();
    res.type = PT_RADICAL;
    nrv = 0;
    rd_err = 0;
    rd_root = root;
    if (has_x(root)) { g_fail = "A square root of x is not supported. Put numbers under sqrt(."; return false; }
    for (;;) {
        int n = rd_find(root);
        if (n < 0) break;
        if (nd[n].t == N_NEG) {                     /* a minus sign in front of a value: no step */
            rd_leaf(nd[n].l, &v);
            v.u = -v.u;
            v.w = -v.w;
            rv_norm(&v);
            if (!rd_set(n, &v)) { g_fail = rd_err; return false; }
            continue;
        }
        if (++guard > 80) { g_fail = "That expression is too long for me."; return false; }
        if (!rd_step(n)) { g_fail = rd_err; return false; }
    }
    if (st_over) { g_fail = "That expression needs too many steps for me."; return false; }
    rd_leaf(root, &v);
    st_final(EQ_NONE);
    sb_s(&tb, "Final answer: ");
    sb_rv(&tb, &v);
    sb_s(&sbs, "= ");
    sb_rv(&sbs, &v);
    sb_rv_approx(&sbs, &v);
    res.kind = RK_ROOTS;
    res.nroots = 1;
    res.root[0] = v;
    res.root[1] = v;
    return true;
}

/* ------------------------------------------------------------------ */
/* Venn diagrams (2 and 3 sets)                                        */
/* ------------------------------------------------------------------ */
/* The numbers come from a form, the way they are on a worksheet:
 *   2 sets   n(U)  n(A)  n(B)  n(A n B)             [outside, optional]
 *   3 sets   n(U)  n(A)  n(B)  n(C)  n(A n B)  n(A n C)  n(B n C)
 *            n(A n B n C)                            [outside, optional]
 * where n(A n B) is EVERYTHING in both A and B (the middle included).
 * A region is named by its membership mask: bit 0 = in A, bit 1 = in B,
 * bit 2 = in C, so 0 is outside all the sets and 7 the middle.
 * The regions are filled in one per step, innermost first:
 *   middle  ->  the pairwise-only regions (subtract the middle)
 *           ->  the only-A / only-B / only-C regions (subtract the rest)
 *           ->  outside (n(U) minus everything inside the circles)
 * A region that would be negative, or an outside count that does not match,
 * stops with a plain message: the numbers do not fit together.
 * Then the typed expression is worked out, in set notation with
 *   A B C  the sets      u  union      n  intersection      '  complement
 *   n( ... ) = brackets that also say "count this", so n(A u B)' n C means
 *   (A u B)' n C.  ' binds first, then n, then u.                       */
#define VMAX 999
enum { VF_U, VF_A, VF_B, VF_C, VF_AB, VF_AC, VF_BC, VF_ABC, VF_NONE, VF_N };
static uint8_t vn_k = 2;
static long vn_f[VF_N];            /* the numbers; -1 = left blank */
static char vn_expr[MAXIN + 1];    /* the expression to evaluate, may be empty */
static long vr[8];                 /* region counts by membership mask */
static char vn_msg[200];           /* why the numbers do not fit, or the expression error */
static const char *vn_head;        /* the heading that goes with it */
static int vn_at;                  /* position of an expression error, or -1 */
static uint8_t vfilled;            /* regions shown so far (bit = mask) */

/* a step that fills region `reg` (or none: reg < 0) and highlights `hl` */
static void vstep(int reg, uint8_t hl)
{
    st_new(EQ_NONE);
    if (reg >= 0) vfilled = (uint8_t)(vfilled | (1 << reg));
    st_ptr->vfill = vfilled;
    st_ptr->vhl = hl;
}

static bool vn_bad(const char *head)
{
    vn_head = head;
    vn_at = -1;
    return false;
}

static const char *const VF_LABEL[VF_N] = {
    "n(U)", "n(A)", "n(B)", "n(C)", "n(A n B)", "n(A n C)", "n(B n C)", "n(A n B n C)", "outside"
};

/* fills vr[] step by step; false with vn_head / vn_msg set when the numbers do not fit */
static bool venn_fill(void)
{
    long U = vn_f[VF_U], A = vn_f[VF_A], B = vn_f[VF_B], C = vn_f[VF_C];
    long AB = vn_f[VF_AB], AC = vn_f[VF_AC], BC = vn_f[VF_BC], ABC = vn_f[VF_ABC], none = vn_f[VF_NONE];
    long inside;
    sb_t vb;
    int i;
    static const uint8_t need2[] = { VF_U, VF_A, VF_B, VF_AB };
    static const uint8_t need3[] = { VF_U, VF_A, VF_B, VF_C, VF_AB, VF_AC, VF_BC, VF_ABC };
    const uint8_t *need = vn_k == 2 ? need2 : need3;
    int nneed = vn_k == 2 ? 4 : 8;
    memset(vr, 0, sizeof vr);
    vfilled = 0;
    sb_init(&vb, vn_msg, sizeof vn_msg);
    for (i = 0; i < nneed; i++) {
        if (vn_f[need[i]] < 0 || vn_f[need[i]] > VMAX) {
            sb_s(&vb, "Please fill in ");
            sb_s(&vb, VF_LABEL[need[i]]);
            sb_s(&vb, " with a whole number from 0 to 999.");
            return vn_bad("A number is missing.");
        }
    }
    if (none > VMAX) {
        sb_s(&vb, "The outside count must be a whole number from 0 to 999 (or empty).");
        return vn_bad("A number is missing.");
    }

    if (vn_k == 2) {
        vr[3] = AB;
        vstep(3, 1 << 3);
        sb_s(&tb, "Start with the overlap. n(A n B) = ");
        sb_n(&tb, AB);
        sb_s(&tb, " is the part that is in both A and B.");
        sb_s(&sbs, "A and B = ");
        sb_n(&sbs, AB);

        vr[1] = A - AB;
        if (vr[1] < 0) {
            sb_s(&vb, "Only A would be ");
            sb_n(&vb, A);
            sb_s(&vb, " - ");
            sb_n(&vb, AB);
            sb_s(&vb, ", which is below zero. The overlap n(A n B) cannot be bigger than the whole set n(A).");
            return vn_bad("The numbers don't fit together.");
        }
        vstep(1, 1 << 1);
        sb_s(&tb, "Only A: take the overlap away from all of A. n(A) - n(A n B).");
        sb_n(&sbs, A);
        sb_s(&sbs, " - ");
        sb_n(&sbs, AB);
        sb_s(&sbs, " = ");
        sb_n(&sbs, vr[1]);

        vr[2] = B - AB;
        if (vr[2] < 0) {
            sb_s(&vb, "Only B would be ");
            sb_n(&vb, B);
            sb_s(&vb, " - ");
            sb_n(&vb, AB);
            sb_s(&vb, ", which is below zero. The overlap n(A n B) cannot be bigger than the whole set n(B).");
            return vn_bad("The numbers don't fit together.");
        }
        vstep(2, 1 << 2);
        sb_s(&tb, "Only B: take the overlap away from all of B. n(B) - n(A n B).");
        sb_n(&sbs, B);
        sb_s(&sbs, " - ");
        sb_n(&sbs, AB);
        sb_s(&sbs, " = ");
        sb_n(&sbs, vr[2]);

        inside = vr[1] + vr[2] + vr[3];
    } else {
        long ab, ac, bc;
        vr[7] = ABC;
        vstep(7, 1 << 7);
        sb_s(&tb, "Start in the middle: the part in all three sets is n(A n B n C) = ");
        sb_n(&tb, ABC);
        sb_c(&tb, '.');
        sb_s(&sbs, "A, B and C = ");
        sb_n(&sbs, ABC);

        ab = AB - ABC;
        ac = AC - ABC;
        bc = BC - ABC;
        {
            static const char *const nm[3] = { "A and B", "A and C", "B and C" };
            long pair[3], mid[3];
            uint8_t mask[3] = { 3, 5, 6 };
            int p;
            pair[0] = AB; pair[1] = AC; pair[2] = BC;
            mid[0] = ab; mid[1] = ac; mid[2] = bc;
            for (p = 0; p < 3; p++) {
                if (mid[p] < 0) {
                    sb_s(&vb, nm[p]);
                    sb_s(&vb, " only would be ");
                    sb_n(&vb, pair[p]);
                    sb_s(&vb, " - ");
                    sb_n(&vb, ABC);
                    sb_s(&vb, ", which is below zero. The overlap of two sets must hold the middle (all three) too.");
                    return vn_bad("The numbers don't fit together.");
                }
                vr[mask[p]] = mid[p];
                vstep(mask[p], (uint8_t)(1 << mask[p]));
                sb_s(&tb, nm[p]);
                sb_s(&tb, " only: take the middle away from the overlap. n(");
                sb_s(&tb, p == 0 ? "A n B" : p == 1 ? "A n C" : "B n C");
                sb_s(&tb, ") - n(A n B n C).");
                sb_n(&sbs, pair[p]);
                sb_s(&sbs, " - ");
                sb_n(&sbs, ABC);
                sb_s(&sbs, " = ");
                sb_n(&sbs, mid[p]);
            }
        }
        {
            static const char *const nm[3] = { "A", "B", "C" };
            long tot[3], low[3][2], sp;
            uint8_t mask[3] = { 1, 2, 4 };
            int p;
            tot[0] = A; tot[1] = B; tot[2] = C;
            low[0][0] = ab; low[0][1] = ac;
            low[1][0] = ab; low[1][1] = bc;
            low[2][0] = ac; low[2][1] = bc;
            for (p = 0; p < 3; p++) {
                sp = tot[p] - low[p][0] - low[p][1] - ABC;
                if (sp < 0) {
                    sb_s(&vb, "Only ");
                    sb_s(&vb, nm[p]);
                    sb_s(&vb, " would be ");
                    sb_n(&vb, tot[p]);
                    sb_s(&vb, " - ");
                    sb_n(&vb, low[p][0]);
                    sb_s(&vb, " - ");
                    sb_n(&vb, low[p][1]);
                    sb_s(&vb, " - ");
                    sb_n(&vb, ABC);
                    sb_s(&vb, ", which is below zero. n(");
                    sb_s(&vb, nm[p]);
                    sb_s(&vb, ") is too small to hold its overlaps.");
                    return vn_bad("The numbers don't fit together.");
                }
                vr[mask[p]] = sp;
                vstep(mask[p], (uint8_t)(1 << mask[p]));
                sb_s(&tb, nm[p]);
                sb_s(&tb, " only: take away the two pair-only parts and the middle from all of ");
                sb_s(&tb, nm[p]);
                sb_c(&tb, '.');
                sb_n(&sbs, tot[p]);
                sb_s(&sbs, " - ");
                sb_n(&sbs, low[p][0]);
                sb_s(&sbs, " - ");
                sb_n(&sbs, low[p][1]);
                sb_s(&sbs, " - ");
                sb_n(&sbs, ABC);
                sb_s(&sbs, " = ");
                sb_n(&sbs, sp);
            }
        }
        inside = vr[1] + vr[2] + vr[3] + vr[4] + vr[5] + vr[6] + vr[7];
    }

    vr[0] = U - inside;
    if (vr[0] < 0) {
        sb_s(&vb, "The circles already hold ");
        sb_n(&vb, inside);
        sb_s(&vb, " elements, but n(U) is only ");
        sb_n(&vb, U);
        sb_s(&vb, ". n(U) must be at least ");
        sb_n(&vb, inside);
        sb_c(&vb, '.');
        return vn_bad("The numbers don't fit together.");
    }
    if (none >= 0 && none != vr[0]) {
        sb_s(&vb, "The circles hold ");
        sb_n(&vb, inside);
        sb_s(&vb, " elements, so ");
        sb_n(&vb, U);
        sb_s(&vb, " - ");
        sb_n(&vb, inside);
        sb_s(&vb, " = ");
        sb_n(&vb, vr[0]);
        sb_s(&vb, " are outside, but the problem says ");
        sb_n(&vb, none);
        sb_s(&vb, ". The numbers do not add up to n(U) = ");
        sb_n(&vb, U);
        sb_c(&vb, '.');
        return vn_bad("The numbers don't fit together.");
    }
    vstep(0, 1);
    sb_s(&tb, "Outside all the circles: n(U) minus everything inside them (");
    {
        int m, first = 1;
        for (m = 1; m < (1 << vn_k); m++) {
            if (!first) sb_op(&tb, false);
            sb_n(&tb, vr[m]);
            first = 0;
        }
    }
    sb_s(&tb, " = ");
    sb_n(&tb, inside);
    sb_s(&tb, ").");
    sb_n(&sbs, U);
    sb_s(&sbs, " - ");
    sb_n(&sbs, inside);
    sb_s(&sbs, " = ");
    sb_n(&sbs, vr[0]);
    return true;
}

/* the calculation panel of the Venn screen is this many columns wide and 10 lines high */
#define VCOLS 13
static int wrap_draw(const char *s, int x, int y, int cols, int maxl, uint8_t color, bool draw);

/* the calculation line "prefix = sym = num"; the prefix, then the symbolic part, are dropped until it fits the panel */
static void vcalc(const char *prefix, const char *sym, const char *num)
{
    char buf[SSTATE];
    sb_t b;
    int tries;
    for (tries = 0; tries < 3; tries++) {
        sb_init(&b, buf, sizeof buf);
        if (tries < 1 && prefix[0]) { sb_s(&b, prefix); sb_s(&b, " = "); }
        if (tries < 2 && sym[0]) { sb_s(&b, sym); sb_s(&b, " = "); }
        sb_s(&b, num);
        if (wrap_draw(buf, 0, 0, VCOLS, 100, 0, false) <= 10) break;
    }
    sb_s(&sbs, buf);
}

/* add a sentence to the text of the step only if the text then still fits in 3 lines */
static void vtail(const char *tail)
{
    char buf[STEXT];
    sb_t b;
    sb_init(&b, buf, sizeof buf);
    sb_s(&b, tb.p);
    sb_s(&b, tail);
    if (wrap_draw(buf, 0, 0, COLS, 100, 0, false) <= 3) sb_s(&tb, tail);
}

/* ---- set expressions ---- */
enum { SN_SET, SN_NOT, SN_OR, SN_AND };
typedef struct { uint8_t t, v; int8_t l, r; } sn_t;
#define MAXSN 40
static sn_t sn[MAXSN];
static int nsn, sn_root;
static const char *se_p, *se_in, *se_err;
static int se_at;
static uint8_t se_depth;

static void se_fail(const char *m)
{
    if (!se_err) { se_err = m; se_at = (int)(se_p - se_in); }
}

static int se_mk(uint8_t t, int l, int r, uint8_t v)
{
    if (nsn >= MAXSN) { se_fail("That expression is too long for me."); return -1; }
    sn[nsn].t = t;
    sn[nsn].l = (int8_t)l;
    sn[nsn].r = (int8_t)r;
    sn[nsn].v = v;
    return nsn++;
}

static int se_expr(void);

static int se_atom(void)
{
    char c = *se_p;
    int e;
    if (c == 'A' || c == 'B' || c == 'C') {
        if (c - 'A' >= vn_k) { se_fail("This is a 2-set problem, so there is no set C."); return -1; }
        se_p++;
        return se_mk(SN_SET, -1, -1, (uint8_t)(c - 'A'));
    }
    if (c == '(' || (c == 'n' && se_p[1] == '(')) {
        /* n( at the start of an operand is just a bracket that says "count this" */
        se_p += c == 'n' ? 2 : 1;
        if (++se_depth > 8) { se_fail("Too many brackets inside brackets."); return -1; }
        e = se_expr();
        se_depth--;
        if (e < 0) return -1;
        if (*se_p != ')') { se_fail("A closing bracket ) is missing."); return -1; }
        se_p++;
        return e;
    }
    if (c == 0) se_fail("The expression ends too early.");
    else if (c == ')') se_fail("There is a ) without a ( before it.");
    else se_fail("I expected A, B, C, ( or n( here.");
    return -1;
}

static int se_fact(void)
{
    int a = se_atom();
    while (a >= 0 && *se_p == '\'') {
        se_p++;
        a = se_mk(SN_NOT, a, -1, 0);
    }
    return a;
}

static int se_term(void)
{
    int a = se_fact(), b;
    while (a >= 0 && *se_p == 'n') {
        se_p++;
        b = se_fact();
        if (b < 0) return -1;
        a = se_mk(SN_AND, a, b, 0);
    }
    return a;
}

static int se_expr(void)
{
    int a = se_term(), b;
    while (a >= 0 && *se_p == 'u') {
        se_p++;
        b = se_term();
        if (b < 0) return -1;
        a = se_mk(SN_OR, a, b, 0);
    }
    return a;
}

/* the root node, or -1 with se_err / se_at set */
static int se_parse(const char *s)
{
    int a;
    nsn = 0;
    se_err = 0;
    se_at = 0;
    se_depth = 0;
    se_in = se_p = s;
    a = se_expr();
    if (a < 0) return -1;
    if (*se_p != 0) {
        se_fail(*se_p == ')' ? "There is a ) without a ( before it." : "I do not understand this part.");
        return -1;
    }
    return a;
}

/* ctx: 0 top, 1 under u, 2 under n, 3 under ' */
static void sb_sn(sb_t *b, int i, int ctx)
{
    const sn_t *q = &sn[i];
    bool br;
    switch (q->t) {
    case SN_SET:
        sb_c(b, (char)('A' + q->v));
        break;
    case SN_NOT:
        sb_sn(b, q->l, 3);
        sb_c(b, '\'');
        break;
    case SN_AND:
        br = ctx == 3;
        if (br) sb_c(b, '(');
        sb_sn(b, q->l, 2);
        sb_sym(b, 'n');
        sb_sn(b, q->r, 2);
        if (br) sb_c(b, ')');
        break;
    default:
        br = ctx >= 2;
        if (br) sb_c(b, '(');
        sb_sn(b, q->l, 1);
        sb_sym(b, 'u');
        sb_sn(b, q->r, 1);
        if (br) sb_c(b, ')');
        break;
    }
}

/* the set of regions (bit = membership mask) that node i covers */
static uint8_t se_eval(int i)
{
    const sn_t *q = &sn[i];
    uint8_t full = (uint8_t)(vn_k == 2 ? 0x0F : 0xFF), r = 0;
    int m;
    switch (q->t) {
    case SN_SET:
        for (m = 0; m < (1 << vn_k); m++) if ((m >> q->v) & 1) r = (uint8_t)(r | (1 << m));
        return r;
    case SN_NOT:
        return (uint8_t)(~se_eval(q->l) & full);
    case SN_OR:
        return (uint8_t)(se_eval(q->l) | se_eval(q->r));
    default:
        return (uint8_t)(se_eval(q->l) & se_eval(q->r));
    }
}

static long se_count(uint8_t set)
{
    long t = 0;
    int m;
    for (m = 0; m < (1 << vn_k); m++) if (set & (1 << m)) t += vr[m];
    return t;
}

/* "7 + 5 + 4 = 16" for the regions of a set (the outside region last) */
static void sb_regsum(sb_t *b, uint8_t set)
{
    int m, n = 0;
    for (m = 1; m <= (1 << vn_k); m++) {
        int r = m == (1 << vn_k) ? 0 : m;
        if (!(set & (1 << r))) continue;
        if (n++) sb_op(b, false);
        sb_n(b, vr[r]);
    }
    if (n == 0) sb_c(b, '0');
    else if (n > 1) {
        sb_s(b, " = ");
        sb_n(b, se_count(set));
    }
}

/* total of one set, and of the overlap of two sets, as typed */
static long vn_set_total(int v) { return vn_f[v == 0 ? VF_A : v == 1 ? VF_B : VF_C]; }
static long vn_pair_total(int x, int y)
{
    if (x > y) { int t = x; x = y; y = t; }
    return vn_f[x == 0 && y == 1 ? VF_AB : x == 0 ? VF_AC : VF_BC];
}

/* which sets appear in a tree of unions only (a bit for each set), or 0 if there is anything else */
static uint8_t se_union_sets(int i)
{
    const sn_t *q = &sn[i];
    uint8_t a, b;
    if (q->t == SN_SET) return (uint8_t)(1 << q->v);
    if (q->t != SN_OR) return 0;
    a = se_union_sets(q->l);
    b = se_union_sets(q->r);
    if (!a || !b || (a & b)) return 0;
    return (uint8_t)(a | b);
}

/* the compound nodes under the root, innermost first */
static int se_ord[10], se_nord;
static bool se_collect(int i, int root)
{
    const sn_t *q = &sn[i];
    if (q->t == SN_SET) return true;
    if (!se_collect(q->l, root)) return false;
    if (q->t != SN_NOT && !se_collect(q->r, root)) return false;
    if (i != root) {
        if (se_nord >= 8) return false;
        se_ord[se_nord++] = i;
    }
    return true;
}

static bool venn_expr(void)
{
    int i;
    uint8_t set, usets;
    long total;
    sb_t vb;
    char pre[100], sym[170], num[110];
    sb_t bp, bs, bn;
    sb_init(&vb, vn_msg, sizeof vn_msg);
    sn_root = se_parse(vn_expr);
    if (sn_root < 0) {
        sb_s(&vb, se_err);
        vn_head = "I can't read that expression.";
        vn_at = se_at;
        return false;
    }
    se_nord = 0;
    if (!se_collect(sn_root, sn_root)) {
        sb_s(&vb, "That expression has too many parts for me. Try a shorter one.");
        return vn_bad("I can't read that expression.");
    }
    for (i = 0; i < se_nord; i++) {
        const sn_t *q = &sn[se_ord[i]];
        set = se_eval(se_ord[i]);
        vfilled = (uint8_t)(vn_k == 2 ? 0x0F : 0xFF);
        st_new(EQ_NONE);
        st_ptr->vfill = vfilled;
        st_ptr->vhl = set;
        sb_s(&tb, "Work out ");
        sb_sn(&tb, se_ord[i], 0);
        sb_c(&tb, ':');
        {
            char long_form[STEXT];
            sb_t lf;
            sb_init(&lf, long_form, sizeof long_form);
            sb_s(&lf, tb.p);
            if (q->t == SN_NOT) {
                sb_s(&lf, " everything in U that is NOT in ");
                sb_sn(&lf, q->l, 0);
                sb_c(&lf, '.');
            } else if (q->t == SN_OR) {
                sb_s(&lf, " the elements in ");
                sb_sn(&lf, q->l, 2);
                sb_s(&lf, " or in ");
                sb_sn(&lf, q->r, 2);
                sb_s(&lf, " (or both).");
            } else {
                sb_s(&lf, " the elements in both ");
                sb_sn(&lf, q->l, 2);
                sb_s(&lf, " and ");
                sb_sn(&lf, q->r, 2);
                sb_c(&lf, '.');
            }
            if (wrap_draw(long_form, 0, 0, COLS, 100, 0, false) <= 2) sb_s(&tb, long_form + tb.len);
            else sb_c(&tb, '.');
        }
        vtail(" Its regions are lit up.");
        sb_init(&bp, pre, sizeof pre);
        sb_s(&bp, "n(");
        sb_sn(&bp, se_ord[i], 0);
        sb_c(&bp, ')');
        sb_init(&bs, sym, sizeof sym);
        sb_init(&bn, num, sizeof num);
        sb_regsum(&bn, set);
        vcalc(pre, sym, num);
    }
    set = se_eval(sn_root);
    total = se_count(set);
    vfilled = (uint8_t)(vn_k == 2 ? 0x0F : 0xFF);
    st_final(EQ_NONE);
    st_ptr->vfill = vfilled;
    st_ptr->vhl = set;
    sb_s(&tb, "Final answer: n(");
    sb_sn(&tb, sn_root, 0);
    sb_s(&tb, ") = ");
    sb_n(&tb, total);
    sb_c(&tb, '.');
    vtail(" The regions that are lit up add up to this.");
    sb_init(&bp, pre, sizeof pre);
    sb_s(&bp, "n(");
    sb_sn(&bp, sn_root, 0);
    sb_c(&bp, ')');
    sb_init(&bs, sym, sizeof sym);
    sb_init(&bn, num, sizeof num);
    usets = se_union_sets(sn_root);
    {
        const sn_t *q = &sn[sn_root];
        if (q->t == SN_NOT) {
            sb_s(&bs, "n(U) - n(");
            sb_sn(&bs, q->l, 0);
            sb_c(&bs, ')');
            sb_n(&bn, vn_f[VF_U]);
            sb_s(&bn, " - ");
            sb_n(&bn, se_count(se_eval(q->l)));
            sb_s(&bn, " = ");
            sb_n(&bn, total);
        } else if (q->t == SN_OR && usets == 7) {
            sb_s(&bs, "n(A) + n(B) + n(C) - n(A n B) - n(A n C) - n(B n C) + n(A n B n C)");
            sb_n(&bn, vn_f[VF_A]);
            sb_s(&bn, " + ");
            sb_n(&bn, vn_f[VF_B]);
            sb_s(&bn, " + ");
            sb_n(&bn, vn_f[VF_C]);
            sb_s(&bn, " - ");
            sb_n(&bn, vn_f[VF_AB]);
            sb_s(&bn, " - ");
            sb_n(&bn, vn_f[VF_AC]);
            sb_s(&bn, " - ");
            sb_n(&bn, vn_f[VF_BC]);
            sb_s(&bn, " + ");
            sb_n(&bn, vn_f[VF_ABC]);
            sb_s(&bn, " = ");
            sb_n(&bn, total);
        } else if (q->t == SN_OR && (usets == 3 || usets == 5 || usets == 6)) {
            int x = usets == 6 ? 1 : 0, y = usets == 3 ? 1 : 2;
            sb_s(&bs, "n(");
            sb_c(&bs, (char)('A' + x));
            sb_s(&bs, ") + n(");
            sb_c(&bs, (char)('A' + y));
            sb_s(&bs, ") - n(");
            sb_c(&bs, (char)('A' + x));
            sb_s(&bs, " n ");
            sb_c(&bs, (char)('A' + y));
            sb_c(&bs, ')');
            sb_n(&bn, vn_set_total(x));
            sb_s(&bn, " + ");
            sb_n(&bn, vn_set_total(y));
            sb_s(&bn, " - ");
            sb_n(&bn, vn_pair_total(x, y));
            sb_s(&bn, " = ");
            sb_n(&bn, total);
        } else {
            sb_regsum(&bn, set);
        }
    }
    vcalc(pre, sym, num);
    res.kind = RK_NONE;
    res.num = total;
    return true;
}

/* all the steps of a Venn problem; false with vn_head / vn_msg set */
static bool solve_venn(void)
{
    steps_reset();
    res.type = PT_VENN;
    vn_head = 0;
    vn_at = -1;
    if (!venn_fill()) return false;
    if (vn_expr[0]) {
        if (!venn_expr()) return false;
        if (st_over) { vn_msg[0] = 0; vn_head = "That expression needs too many steps for me."; return false; }
        return true;
    }
    vfilled = (uint8_t)(vn_k == 2 ? 0x0F : 0xFF);
    st_final(EQ_NONE);
    st_ptr->vfill = vfilled;
    st_ptr->vhl = 0;
    sb_s(&tb, "Final answer: the diagram is complete. Add all the regions to check that they make n(U).");
    sb_regsum(&sbs, vfilled);
    sb_s(&sbs, " = n(U)");
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
        if (has_sqrt(root)) { c->type = PT_RADICAL; return true; }
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
    if (type == PT_RADICAL) return solve_radical(root);
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
static jmp_buf quit_jb;             /* DEL jumps back to main(), which calls gfx_End() */

/* DEL is the panic key. On the keypad matrix, three other keys held together
   (one in DEL's column, one in its row-mate's row and one sharing a column with
   a key of DEL's row) read exactly like DEL, so a DEL that those keys could
   have faked is ignored; any DEL on its own is real. */
static bool del_hit(void)
{
    uint8_t c, g;
    if (!(keys[1] & kb_Del)) return false;
    for (g = 2; g < 8; g++) {
        if (!(keys[g] & 0x80)) continue;
        for (c = 0; c < 7; c++) if ((keys[1] & (1 << c)) && (keys[g] & (1 << c))) return false;
    }
    return true;
}

static void read_keys(void)
{
    uint8_t i;
    memcpy(prev_keys, keys, sizeof keys);
    kb_Scan();
    for (i = 1; i < 8; i++) keys[i] = kb_Data[i];
    keys[0] = 0;
    if (del_hit()) {
        TEST_HOOK(test_del_quit());
        longjmp(quit_jb, 1);                     /* quit at once, from any screen */
    }
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
/* CLEAR is the backspace key (DEL is the panic key) */
static bool key_back(void) { return rep_key(4, KDOWN(6, kb_Clear), KPRESS(6, kb_Clear)); }

/* any key went down this tick, other than 2nd itself */
static bool any_press(void)
{
    uint8_t g;
    for (g = 1; g < 8; g++) {
        uint8_t m = (uint8_t)(keys[g] & (uint8_t)~prev_keys[g]);
        if (g == 1) m &= (uint8_t)~kb_2nd;
        if (m) return true;
    }
    return false;
}
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
        if (hist.t[i] < PT_LINEAR || hist.t[i] > PT_RADICAL || hist.s[i][0] == 0) { hist.n = 0; break; }
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
enum { SC_TITLE, SC_INPUT, SC_STEPS, SC_END, SC_HIST, SC_HELP, SC_ERROR, SC_CHOOSE, SC_VENN };

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

/* returns 0 new problem, 1 Venn 2 sets, 2 Venn 3 sets, 3 history, 4 help, 5 quit */
static uint8_t title_screen(void)
{
    static const char *const it[6] = {
        "New problem", "Venn diagram: 2 sets", "Venn diagram: 3 sets", "History (last problems)",
        "How to type, what it can do", "Quit"
    };
    screen_begin(SC_TITLE);
    for (;;) {
        tick_wait();
        read_keys();
        if (nav_up() && title_sel > 0) { title_sel--; dirty = true; }
        if (nav_down() && title_sel < 5) { title_sel++; dirty = true; }
        if (key_ok()) return title_sel;
        if (key_clear()) return 5;
        if (dirty) {
            draw_bg();
            fill(0, 0, SCR_W, 56, C_HEAD);
            txt_big("StepMath CE", 28, 12, C_WHITE, 3);
            txt_c("learn the METHOD, one step at a time", 44, C_YELLOW);
            draw_menu(it, 6, title_sel, 66, 20);
            txt_c("Equations, derivatives, fractions, Venn", 192, C_GRAY);
            txt_c("UP/DOWN: choose    ENTER: go", 206, C_GRAY);
            txt_c("DEL = quit the program, from any screen", 220, C_RED);
            present();
            dirty = false;
        }
    }
}

#define HELP_PAGES 5
static const char *const HELP[HELP_PAGES][10] = {
    { "What I can do", "",
      "1 Linear equation    3x+5=20",
      "2 Quadratic          x^2-5x+6=0",
      "3 Simplify           3(x+2)+4x",
      "4 Derivative d/dx    x^3+2x",
      "5 Fractions          1/2+3/4",
      "6 Square roots       sqrt(25)+3",
      "7 Venn diagrams      2 or 3 sets",
      "I show ONE step at a time." },
    { "How to type", "",
      "0-9, the X key (x), + - * / ^ ( )",
      "(-) also types a minus sign",
      "Y= key types the equals sign =",
      "x^2 key types ^2",
      "2nd, then x^2 types sqrt(   (2nd",
      "lights up; it goes off after the",
      "next key). CLEAR erases a character,",
      "MODE clears the line. ENTER solves." },
    { "Venn diagrams", "",
      "Pick Venn 2 sets / 3 sets in the menu",
      "and fill in the numbers. n(A n B) is",
      "EVERYTHING in A and B. Optional: an",
      "expression with A B C, n(and), u(or),",
      "'(not): n(A u B)'  n(A u B)' n C",
      "MATH=A APPS=B PRGM=C LOG=n VARS=u",
      "comma key = ' (complement) ( ) as usual",
      "Numbers 0 to 999." },
    { "Limits", "",
      "Whole numbers up to 4 digits, no",
      "decimals. Only the letter x.",
      "Quadratics: a, b, c within +-10000.",
      "No complex numbers: D < 0 gives",
      "'no real solutions'.",
      "sqrt( takes whole numbers; one kind of",
      "root per expression.",
      "Derivatives: 3x^2 sums. Fractions: 2." },
    { "DEL = QUIT", "",
      "DEL quits the program AT ONCE, from",
      "any screen. No question is asked and",
      "nothing is saved: a problem you are",
      "in the middle of is lost.",
      "Use it as a panic button.",
      "(Erasing is on the CLEAR key.)",
      "",
      "" }
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

/* insert s at the cursor of buf (len chars, at most cap) */
static void str_insert(char *buf, uint8_t *len, uint8_t *cur, uint8_t cap, const char *s)
{
    uint8_t n = (uint8_t)strlen(s);
    if (*len + n > cap) return;
    memmove(buf + *cur + n, buf + *cur, (size_t)(*len - *cur) + 1);
    memcpy(buf + *cur, s, n);
    *cur = (uint8_t)(*cur + n);
    *len = (uint8_t)(*len + n);
}

/* erase the character before the cursor (a whole sqrt( at once) */
static void str_delete(char *buf, uint8_t *len, uint8_t *cur)
{
    uint8_t n = 1;
    if (*cur == 0) return;
    if (*cur >= 5 && memcmp(buf + *cur - 5, "sqrt(", 5) == 0) n = 5;
    memmove(buf + *cur - n, buf + *cur, (size_t)(*len - *cur) + 1);
    *cur = (uint8_t)(*cur - n);
    *len = (uint8_t)(*len - n);
}

static void input_insert(const char *s) { str_insert(prob, &plen, &pcur, MAXIN, s); }
static void input_delete(void) { str_delete(prob, &plen, &pcur); }

static bool second;                /* the 2nd key is lit: the next key types its second function */

/* the 2nd indicator, like the little arrow on the real calculator */
static void draw_2nd(bool on)
{
    int x = 232, y = 21;
    if (on) {
        fill(x, y, 80, 14, C_YELLOW);
        txt("2nd", x + 6, y + 3, C_BLACK);
        gfx_SetColor(C_BLACK);
        gfx_FillTriangle(x + 40, y + 11, x + 50, y + 11, x + 45, y + 3);
        txt("lit", x + 56, y + 3, C_BLACK);
    } else {
        gfx_SetColor(C_DKGRAY);
        gfx_Rectangle(x, y, 80, 14);
        txt("2nd", x + 6, y + 3, C_DKGRAY);
    }
}

static void draw_input(bool blink)
{
    char buf[8];
    sb_t s;
    draw_bg();
    draw_header("New problem");
    txt("Type your problem:", 8, 24, C_YELLOW);
    draw_2nd(second);
    fill(4, 38, 312, 22, C_PANEL);
    fill(4, 38, 312, 1, C_GRAY);
    fill(4, 59, 312, 1, C_GRAY);
    txt(prob, 8, 44, C_WHITE);
    if (blink) fill(8 + 8 * pcur, 55, 8, 2, C_YELLOW);
    sb_init(&s, buf, sizeof buf);
    sb_n(&s, plen);
    sb_c(&s, '/');
    sb_n(&s, MAXIN);
    txt(buf, SCR_W - 8 - txt_w(buf), 63, C_GRAY);
    txt("Keys: 0-9  X  + - * / ^ ( )", 8, 72, C_CYAN);
    txt("Y= types =     x^2 key types ^2", 8, 83, C_CYAN);
    txt("2nd then x^2 types sqrt(", 8, 94, second ? C_YELLOW : C_CYAN);
    txt("CLEAR erases   MODE clears the line", 8, 105, C_CYAN);
    txt("arrows move the cursor", 8, 116, C_CYAN);
    txt("Examples:", 8, 130, C_YELLOW);
    txt("3x+5=20        x^2-5x+6=0", 8, 141, C_WHITE);
    txt("3(x+2)+4x      x^3+2x (d/dx)", 8, 152, C_WHITE);
    txt("1/2+3/4        (1/2)/(3/4)", 8, 163, C_WHITE);
    txt("(-5+sqrt(5^2-4*1*6))/(2*1)", 8, 174, C_WHITE);
    txt("ENTER or GRAPH: solve", 8, 190, C_GREEN);
    txt("CLEAR on an empty line: menu", 8, 202, C_GRAY);
    txt("DEL: QUIT the program (any screen)", 8, 216, C_RED);
}

/* returns true to solve, false to go back to the menu */
static bool input_screen(void)
{
    uint8_t blink = 0;
    screen_begin(SC_INPUT);
    second = false;
    plen = (uint8_t)strlen(prob);               /* whatever was put in prob, the cursor must lie inside it */
    if (pcur > plen) pcur = plen;
    for (;;) {
        int i;
        tick_wait();
        read_keys();
        if (++blink >= 30) { blink = 0; dirty = true; }
        if (KPRESS(1, kb_2nd)) {
            second = !second;                     /* 2nd lights up, or goes off again */
            dirty = true;
            blink = 0;
        }
        for (i = 0; i < NKM; i++) {
            if (KPRESS(KM[i].g, KM[i].m)) {
                if (second && KM[i].g == 2 && KM[i].m == kb_Square) input_insert("sqrt(");
                else input_insert(KM[i].s);
                dirty = true;
                blink = 0;
            }
        }
        if (KPRESS(6, kb_Clear) && plen == 0) return false;
        if (KPRESS(1, kb_Mode)) { prob[0] = 0; plen = pcur = 0; dirty = true; blink = 0; }
        if (key_back()) { input_delete(); dirty = true; blink = 0; }
        if (nav_left() && pcur > 0) { pcur--; dirty = true; blink = 0; }
        if (nav_right() && pcur < plen) { pcur++; dirty = true; blink = 0; }
        if (key_submit() && plen > 0) return true;
        if (any_press() && second) { second = false; dirty = true; }     /* one-shot, like the real calculator */
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
static void draw_steps_venn(int cur);

static void draw_steps(int cur)
{
    char b[24];
    if (ptype == PT_VENN) { draw_steps_venn(cur); return; }
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
/* Venn diagram screens                                                */
/* ------------------------------------------------------------------ */
#define VB_X 6
#define VB_Y 102
#define VB_W 196
#define VB_H 104
/* circles (centre x, centre y, radius) inside the box, and where the counts go */
static const int16_t VC2[2][3] = { { 78, 54, 36 }, { 118, 54, 36 } };
static const int16_t VC3[3][3] = { { 81, 42, 31 }, { 115, 42, 31 }, { 98, 70, 31 } };
static const int16_t VL2[4][2] = { { 0, 0 }, { 62, 54 }, { 134, 54 }, { 98, 54 } };
static const int16_t VL3[8][2] = { { 0, 0 }, { 69, 33 }, { 127, 33 }, { 98, 29 }, { 98, 85 }, { 79, 62 }, { 117, 62 }, { 98, 51 } };
static const int16_t VN2[2][2] = { { 28, 50 }, { 160, 50 } };
static const int16_t VN3[3][2] = { { 36, 38 }, { 152, 38 }, { 134, 66 } };

static void venn_text_c(const char *s, int cx, int cy, uint8_t c)
{
    txt(s, VB_X + cx - txt_w(s) / 2, VB_Y + cy - 4, c);
}

/* the diagram: regions in `fill` show their count, regions in `hl` are lit up */
static void venn_draw(uint8_t k, uint8_t fill_set, uint8_t hl)
{
    const int16_t (*cc)[3] = k == 2 ? VC2 : VC3;
    const int16_t (*ll)[2] = k == 2 ? VL2 : VL3;
    static const uint8_t setcol[3] = { C_CYAN, C_GREEN, C_ORANGE };
    int y, i, j, nm = 1 << k;
    char b[12];
    sb_t s;
    fill(VB_X, VB_Y, VB_W, VB_H, (hl & 1) ? C_VHL0 : C_BOX);
    for (y = 0; y < VB_H; y++) {
        int xs[8], lo[3], hi[3], n = 0;
        xs[n++] = 0;
        xs[n++] = VB_W;
        for (i = 0; i < k; i++) {
            int dy = y - cc[i][1], r = cc[i][2], half;
            lo[i] = hi[i] = -1;
            if (dy < -r || dy > r) continue;
            half = (int)isqrtl((long)r * r - (long)dy * dy);
            lo[i] = cc[i][0] - half;
            hi[i] = cc[i][0] + half + 1;
            xs[n++] = lo[i];
            xs[n++] = hi[i];
        }
        for (i = 1; i < n; i++) {                       /* sort the break points */
            int v = xs[i];
            for (j = i; j > 0 && xs[j - 1] > v; j--) xs[j] = xs[j - 1];
            xs[j] = v;
        }
        for (i = 0; i + 1 < n; i++) {
            int xa = xs[i], xb = xs[i + 1], m = 0;
            if (xb <= xa) continue;
            for (j = 0; j < k; j++) if (lo[j] >= 0 && xa >= lo[j] && xa < hi[j]) m |= 1 << j;
            if (m == 0) continue;
            gfx_SetColor((hl & (1 << m)) ? C_VHL : (fill_set & (1 << m)) ? C_VR((uint8_t)m) : C_VUNK);
            gfx_HorizLine(VB_X + xa, VB_Y + y, xb - xa);
        }
    }
    for (i = 0; i < k; i++) {
        gfx_SetColor(setcol[i]);
        gfx_Circle(VB_X + cc[i][0], VB_Y + cc[i][1], (uint24_t)cc[i][2]);
        gfx_Circle(VB_X + cc[i][0], VB_Y + cc[i][1], (uint24_t)(cc[i][2] - 1));
        b[0] = (char)('A' + i);
        b[1] = 0;
        venn_text_c(b, k == 2 ? VN2[i][0] : VN3[i][0], k == 2 ? VN2[i][1] : VN3[i][1], setcol[i]);
    }
    gfx_SetColor(C_GRAY);
    gfx_Rectangle(VB_X, VB_Y, VB_W, VB_H);
    for (i = 1; i < nm; i++) {
        sb_init(&s, b, sizeof b);
        if (fill_set & (1 << i)) sb_n(&s, vr[i]);
        else sb_c(&s, '?');
        venn_text_c(b, ll[i][0], ll[i][1], (hl & (1 << i)) ? C_BLACK : (fill_set & (1 << i)) ? C_WHITE : C_GRAY);
    }
    sb_init(&s, b, sizeof b);
    sb_s(&s, "U=");
    sb_n(&s, vn_f[VF_U]);
    txt(b, VB_X + 4, VB_Y + 3, C_GRAY);
    sb_init(&s, b, sizeof b);
    sb_s(&s, "out ");
    if (fill_set & 1) sb_n(&s, vr[0]);
    else sb_c(&s, '?');
    txt(b, VB_X + 4, VB_Y + VB_H - 12, (fill_set & 1) ? C_WHITE : C_GRAY);
}

static char vsum[160];                 /* the Venn problem as one line of text */

static void venn_make_summary(void)
{
    sb_t s;
    static const char *const nm[VF_N] = { "U", "A", "B", "C", "AnB", "AnC", "BnC", "AnBnC", "out" };
    static const uint8_t k2[] = { VF_U, VF_A, VF_B, VF_AB, VF_NONE };
    static const uint8_t k3[] = { VF_U, VF_A, VF_B, VF_C, VF_AB, VF_AC, VF_BC, VF_ABC, VF_NONE };
    const uint8_t *f = vn_k == 2 ? k2 : k3;
    int i, n = vn_k == 2 ? 5 : 9;
    sb_init(&s, vsum, sizeof vsum);
    for (i = 0; i < n; i++) {
        if (vn_f[f[i]] < 0) continue;
        if (s.len) sb_c(&s, ' ');
        sb_s(&s, nm[f[i]]);
        sb_c(&s, '=');
        sb_n(&s, vn_f[f[i]]);
    }
    if (vn_expr[0]) {
        sb_s(&s, "  find ");
        sb_s(&s, vn_expr);
    }
    while (wrap_draw(vsum, 0, 0, COLS, 100, 0, false) > 2 && s.len > 4) {       /* two lines of text at most */
        s.len = (uint16_t)(s.len - 1);
        vsum[s.len] = 0;
        if (s.len > 3) { vsum[s.len - 1] = '.'; vsum[s.len - 2] = '.'; vsum[s.len - 3] = '.'; }
    }
}

static void draw_steps_venn(int cur)
{
    char b[24];
    sb_t s;
    const step_t *st = &steps[cur];
    draw_bg();
    draw_header(vn_k == 2 ? "Venn: 2 sets" : "Venn: 3 sets");
    txt("Problem:", 8, 20, C_GRAY);
    wrap_draw(vsum, 8, 30, COLS, 2, C_WHITE, true);
    fill(4, 52, 312, 1, C_DKGRAY);
    sb_init(&s, b, sizeof b);
    if (st->fin) sb_s(&s, "Final answer");
    else { sb_s(&s, "Step "); sb_n(&s, cur + 1); }
    txt(b, 8, 55, st->fin ? C_GREEN : C_YELLOW);
    if (cur > 0) txt("<- back", SCR_W - 8 - 7 * 8, 55, C_GRAY);
    wrap_draw(st->text, 8, 66, COLS, 3, C_WHITE, true);
    venn_draw(vn_k, st->vfill, st->vhl);
    fill(VB_X + VB_W + 4, VB_Y, SCR_W - (VB_X + VB_W + 4) - 4, VB_H, C_PANEL);
    fill(VB_X + VB_W + 4, VB_Y, SCR_W - (VB_X + VB_W + 4) - 4, 1, st->fin ? C_GREEN : C_GRAY);
    wrap_draw(st->state, VB_X + VB_W + 8, VB_Y + 5, VCOLS, 10, st->fin ? C_GREEN : C_CYAN, true);
    txt(st->fin ? "ENTER: what next?" : "ENTER or -> : next step", 8, 214, C_GREEN);
    txt("<-: back   CLEAR: edit numbers", 8, 226, C_GRAY);
}

/* ---- the form ---- */
/* the keys that type the set expression (A B C are the letters printed on MATH APPS PRGM) */
static const keymap_t VK[] = {
    { 2, kb_Math, "A" }, { 3, kb_Apps, "B" }, { 4, kb_Prgm, "C" }, { 2, kb_Log, "n" }, { 5, kb_Vars, "u" },
    { 3, kb_Comma, "'" }, { 4, kb_LParen, "(" }, { 5, kb_RParen, ")" }
};
#define NVK ((int)(sizeof VK / sizeof VK[0]))
static char vf_txt[VF_N][4];
static uint8_t vf_sel, ecur;
static const uint8_t VROWS2[5] = { VF_U, VF_A, VF_B, VF_AB, VF_NONE };
static const uint8_t VROWS3[9] = { VF_U, VF_A, VF_B, VF_C, VF_AB, VF_AC, VF_BC, VF_ABC, VF_NONE };
static const char *const VF_HINT[VF_N] = {
    "all elements", "in A", "in B", "in C", "in A and B", "in A and C", "in B and C", "in all three", "in none (opt.)"
};

static void vf_clear(void)
{
    memset(vf_txt, 0, sizeof vf_txt);
    vn_expr[0] = 0;
    vf_sel = 0;
    ecur = 0;
}

static void vf_collect(void)
{
    int i;
    for (i = 0; i < VF_N; i++) {
        long v = 0;
        const char *p = vf_txt[i];
        if (!*p) { vn_f[i] = -1; continue; }
        while (*p) v = v * 10 + (*p++ - '0');
        vn_f[i] = v;
    }
}

static void draw_vform(bool blink)
{
    const uint8_t *rows = vn_k == 2 ? VROWS2 : VROWS3;
    int nr = vn_k == 2 ? 5 : 9, dy = vn_k == 2 ? 16 : 13, y0 = vn_k == 2 ? 24 : 22, i, y;
    draw_bg();
    draw_header(vn_k == 2 ? "Venn: 2 sets" : "Venn: 3 sets");
    for (i = 0; i < nr; i++) {
        int f = rows[i];
        y = y0 + i * dy;
        if (i == vf_sel) fill(4, y - 2, 312, dy - 1, C_HEAD);
        txt(VF_LABEL[f], 10, y, i == vf_sel ? C_WHITE : C_GRAY);
        txt(VF_HINT[f], 112, y, C_DKGRAY);
        fill(250, y - 2, 40, 11, C_PANEL);
        txt(vf_txt[f], 254, y, C_YELLOW);
        if (i == vf_sel && blink) fill(254 + 8 * (int)strlen(vf_txt[f]), y + 7, 8, 2, C_YELLOW);
    }
    y = y0 + nr * dy + 4;
    if (vf_sel == nr) fill(4, y - 2, 312, 12, C_HEAD);
    txt("Evaluate (optional), e.g. n(A u B)':", 8, y, vf_sel == nr ? C_WHITE : C_GRAY);
    fill(4, y + 11, 312, 14, C_PANEL);
    txt(vn_expr, 8, y + 14, C_WHITE);
    if (vf_sel == nr && blink) fill(8 + 8 * ecur, y + 22, 8, 2, C_YELLOW);
    y += 30;
    txt("MATH=A APPS=B PRGM=C  LOG=n VARS=u", 8, y, C_CYAN);
    txt("n: intersection (and)  u: union (or)", 8, y + 10, C_CYAN);
    txt("comma key types ' (complement)", 8, y + 20, C_CYAN);
    txt("UP/DOWN field  GRAPH solve  MODE menu", 8, 206, C_GREEN);
    txt("CLEAR erases   DEL: quit the program", 8, 218, C_GRAY);
}

/* returns true to solve, false to go back to the title */
static bool venn_form(void)
{
    const uint8_t *rows = vn_k == 2 ? VROWS2 : VROWS3;
    int nr = vn_k == 2 ? 5 : 9, i;
    uint8_t blink = 0;
    screen_begin(SC_VENN);
    for (;;) {
        tick_wait();
        read_keys();
        if (++blink >= 30) { blink = 0; dirty = true; }
        if (nav_up() && vf_sel > 0) { vf_sel--; dirty = true; blink = 0; }
        if (nav_down() && vf_sel < nr) { vf_sel++; dirty = true; blink = 0; }
        if (vf_sel < nr) {
            char *t = vf_txt[rows[vf_sel]];
            size_t n = strlen(t);
            for (i = 0; i < 10; i++) {
                if (KPRESS(KM[i].g, KM[i].m) && n < 3) {
                    t[n++] = KM[i].s[0];
                    t[n] = 0;
                    dirty = true;
                    blink = 0;
                }
            }
            if (key_back() && n > 0) { t[n - 1] = 0; dirty = true; blink = 0; }
        } else {
            uint8_t len = (uint8_t)strlen(vn_expr);
            for (i = 0; i < NVK; i++) {
                if (KPRESS(VK[i].g, VK[i].m)) {
                    str_insert(vn_expr, &len, &ecur, MAXIN, VK[i].s);
                    dirty = true;
                    blink = 0;
                }
            }
            if (key_back()) { str_delete(vn_expr, &len, &ecur); dirty = true; blink = 0; }
            if (nav_left() && ecur > 0) { ecur--; dirty = true; blink = 0; }
            if (nav_right() && ecur < len) { ecur++; dirty = true; blink = 0; }
        }
        if (KPRESS(1, kb_Mode)) return false;
        if (KPRESS(1, kb_Graph)) return true;
        if (KPRESS(6, kb_Enter)) {
            if (vf_sel < nr) { vf_sel++; dirty = true; blink = 0; }
            else return true;
        }
        if (dirty) {
            draw_vform(blink < 15);
            present();
            dirty = false;
        }
    }
}

/* solves the form; false (after a message) when the numbers or the expression do not work */
static bool venn_attempt(void)
{
    vf_collect();
    venn_make_summary();
    if (!solve_venn()) {
        if (vn_at >= 0) {                      /* a mistake in the expression: show it with a pointer */
            strncpy(prob, vn_expr, MAXIN);
            prob[MAXIN] = 0;
        } else {
            strncpy(prob, vsum, MAXIN);
            prob[MAXIN] = 0;
        }
        plen = pcur = (uint8_t)strlen(prob);
        message_screen(vn_head ? vn_head : "I can't solve that.", vn_msg, vn_at);
        return false;
    }
    strncpy(prob, vsum, MAXIN);
    prob[MAXIN] = 0;
    plen = pcur = (uint8_t)strlen(prob);
    return true;
}

static void run_venn(uint8_t k)
{
    vn_k = k;
    vf_clear();
    for (;;) {
        uint8_t r;
        if (!venn_form()) return;
        if (!venn_attempt()) continue;
        ptype = PT_VENN;
        r = steps_view();
        if (r == RUN_NEW) vf_clear();
        else if (r == RUN_MENU) return;
    }
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
    txt("ENTER: solve it again   MODE: remove", 8, 214, C_GRAY);
    txt("CLEAR: back   DEL: quit the program", 8, 226, C_GRAY);
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
        if (KPRESS(1, kb_Mode) && hist.n > 0) {
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
    if (setjmp(quit_jb) != 0) {
        /* DEL was pressed on some screen: leave at once, saving nothing */
        gfx_End();
        return 0;
    }
    for (;;) {
        m = title_screen();
        if (m == 3) {
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
        } else if (m == 1 || m == 2) {
            run_venn((uint8_t)(m + 1));
        } else if (m == 4) {
            help_screen();
        } else if (m == 5) {
            break;
        }
    }
    hist_save(true);
    gfx_End();
    return 0;
}
