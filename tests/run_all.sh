#!/bin/sh
# Runs every host test against ../STEPMATH/src/main.c (needs gcc with ASan/UBSan).
set -e
cd "$(dirname "$0")"
./build.sh
./harness types 4000 1
./harness types 4000 2
./harness garbage 150000 5
FUZZ_TYPE=8 ./harness fuzz 200000 21
./harness fuzz 1000000 1
mkdir -p shots && ./harness shots shots
./mutate.sh
