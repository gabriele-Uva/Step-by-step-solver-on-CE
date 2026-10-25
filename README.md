# StepMath CE

A step-by-step math tutor for the **TI-84 Plus CE**, written in C for the
[CE C/C++ Toolchain (CEdev)](https://ce-programming.github.io/toolchain/). You type a problem; instead of the
answer it reveals **one step of the method at a time**, in words, with the running equation, and waits for a key.
The final answer is itself the last step.

Supported: linear equations, quadratics (factoring when the roots are whole numbers, otherwise the quadratic formula
with the discriminant as its own step), simplifying expressions (expand one bracket, combine like terms), derivatives
(power, sum and constant rules only) and fraction arithmetic (two fractions: + - * /). Anything else gets
"I can't solve this type of problem yet" and is never guessed at.

Type with the digits, the **X** key, `+ - * / ^ ( )`, **Y=** for `=` and the **x²** key for `^2`; `3x` and `2(x+1)`
work without `*`. Write fraction division as `(1/2)/(3/4)`. **Enter**/**Graph** solves, **Enter** or **→** shows the
next step, **←** goes back, **Clear** edits the problem (or quits from the menu). The last 5 problems are kept in the
archived appvar `STEPHIST`.

| Path | What |
|---|---|
| `STEPMATH/src/main.c` | the whole program (tokenizer, recursive-descent parser, step generators, UI) |
| `STEPMATH/makefile` | CEdev makefile |
| `STEPMATH/README.txt` | controls, exact supported forms, what is NOT supported, limits, build steps, test results |
| `stepmath_main_c.txt`, `stepmath_makefile.txt`, `stepmath_README.txt` | plain-text copies |
| `prebuilt/STEPMATH.8xp` | ready-built program (CEdev v13.0) |
| `docs/screenshots/` | screenshots from the PC test build (`overview.png` is the quick look) |
| `tests/` | PC test harness: stub headers, step generator checked against independent references, fuzzers, mutation check, ASan/UBSan |

Build: open the CEdev prompt in `STEPMATH/` and run `make`.
PC tests (Linux, gcc): `tests/run_all.sh`.

Not run on real hardware or an emulator; see `STEPMATH/README.txt` for the honest limits.
