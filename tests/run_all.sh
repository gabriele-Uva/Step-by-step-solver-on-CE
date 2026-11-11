#!/bin/sh
# Runs every host test against ../STEPMATH/src/main.c (needs gcc with ASan/UBSan, python3 + PIL only for png conversion).
set -e
cd "$(dirname "$0")"
./build.sh
./harness types 4000 1            # steps of linear / quadratic / simplify / derivative / fractions vs references
./harness types 4000 2
./harness garbage 150000 5        # random strings (also sqrt( ) and random Venn problems
./harness radical 3000 1          # typed sqrt( expressions and the quadratic formula vs an independent evaluator
./harness venn 6000 1             # Venn: 2-set and 3-set problems built from known regions, expressions, inconsistent numbers
./harness ui                      # 2nd key, sqrt( through the real keys, Venn forms through the real keys
./harness del                     # DEL from 16 screens, and the keypad-ghosting test
FUZZ_TYPE=8 ./harness fuzz 200000 21   # the whole UI, random keys, ghosting on, DEL pressed at random
./harness fuzz 1000000 1
mkdir -p shots && ./harness shots shots
./mutate.sh                       # deliberately broken copies of the code must be caught
