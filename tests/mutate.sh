#!/bin/sh
# Mutation check: each sed edit breaks one calculation in a COPY of main.c; the harness must report failures.
cd "$(dirname "$0")"
SRC=../STEPMATH/src/main.c
cp $SRC /tmp/main.orig.c
trap 'cp /tmp/main.orig.c $SRC' EXIT
n=0; caught=0
run() {
  desc="$1"; expr="$2"
  cp /tmp/main.orig.c $SRC
  sed -i "$expr" $SRC
  if cmp -s $SRC /tmp/main.orig.c; then echo "NOT APPLIED: $desc"; return; fi
  n=$((n+1))
  if ! ./build.sh >/dev/null 2>&1; then echo "BUILD FAIL: $desc"; return; fi
  out=$(./harness types 400 1 2>&1 | tail -3)
  if echo "$out" | grep -q " 0 failures"; then echo "MISSED:  $desc"; else caught=$((caught+1)); echo "caught:  $desc"; fi
}
run "linear: wrong sign moving constant" 's/pr\[0\] -= v;/pr[0] += v;/'
run "quad: discriminant b2+fac4"        's/D = b2 - fac4;/D = b2 + fac4;/'
run "deriv: c*k -> c*(k+1)"             's/sb_mono(&tb, c \* k, k - 1, true);/sb_mono(\&tb, c * (k+1), k - 1, true);/'
run "combine: sum sign"                 's/sum += s->it\[i\].m.c;/sum -= s->it[i].m.c;/'
run "frac: n1 uses m2"                  's/n1 = fr_a.n \* m1;/n1 = fr_a.n * m2;/'
run "frac: n2 uses m1"                  's/n2 = fr_b.n \* m2;/n2 = fr_b.n * m1;/'
run "expand: product sign"              's/long v = it.m.c;\n        if (!mul_c(&v, it.br\[j\].c))/&/;0,/out\[j\].m.c = v;/s//out[j].m.c = -v;/'
run "deriv state: c = m->c * m->k -> +"  's/long c = m->k == 0 ? 0 : m->c \* m->k;/long c = m->k == 0 ? 0 : m->c + m->k;/'
run "deriv final: dp"                     's/dp\[m->k - 1\] += m->c \* m->k;/dp[m->k - 1] += m->c * (m->k + 1);/'
run "quad: factor root x2 sign"           's/x2 = (-b - sD) \/ (2 \* a)/x2 = (-b + sD) \/ (2 * a)/'
run "quad: formula root sign"             's/long u = -b, w = sg \* f, den = 2 \* a, g;/long u = b, w = sg * f, den = 2 * a, g;/'
run "frac: lcm"                           's/L = fr_a.d \/ gcdl(fr_a.d, fr_b.d) \* fr_b.d;/L = fr_a.d * fr_b.d;/'
echo "$caught / $n mutations caught"
