# StepMath CE  (v2)

A step-by-step math tutor for the **TI-84 Plus CE**, written in C for the
[CE C/C++ Toolchain (CEdev)](https://ce-programming.github.io/toolchain/). You type a problem; instead of the
answer it reveals **one step of the method at a time**, in words, with the running equation, and waits for a key.
The final answer is itself the last step.

Supported: linear equations, quadratics (factoring when the roots are whole numbers, otherwise the quadratic formula
with the discriminant as its own step), simplifying expressions (expand one bracket, combine like terms), derivatives
(power, sum and constant rules only), fraction arithmetic (two fractions: + - * /), **square-root expressions** such as
the quadratic formula typed in (`(-5+sqrt(5^2-4*1*6))/(2*1)`) and **Venn diagrams with 2 or 3 sets** (a form for the
numbers, the regions filled in one per step on a drawn diagram, then a set expression like `n(A u B)' n C` evaluated).
Anything else gets "I can't solve this type of problem yet" and is never guessed at.

**New in v2:** a real **2nd key** (2nd lights up, then **x²** types `sqrt(`), **CLEAR** is now backspace (MODE clears the
line), and **DEL quits the program at once from any screen** (no question, nothing saved).

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
| `docs/screenshots/` | screenshots from the PC test build (`v2_overview.png` is the quick look) |
| `tests/` | PC test harness: stub headers, step generator checked against independent references, fuzzers, mutation check, ASan/UBSan |

Build: open the CEdev prompt in `STEPMATH/` and run `make`.
PC tests (Linux, gcc): `tests/run_all.sh` (Venn reference tests, sqrt tests, 2nd-key and DEL tests, fuzzers, 27 mutation checks).

Not run on real hardware or an emulator; see `STEPMATH/README.txt` for the honest limits.
