#!/bin/sh
# Builds the host test harness (ASan + UBSan) from the real game source.
set -e
cd "$(dirname "$0")"
CFLAGS="-g -O1 -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
gcc $CFLAGS -Istub -c stub/stub.c -o stub.o 2>&1 | head -5 || true
gcc $CFLAGS -DSM_HOST_TEST -Dclock=fake_clock -Istub -c harness.c -o harness.o
gcc $CFLAGS -rdynamic stub.o harness.o -o harness -lm
echo built tests/harness
