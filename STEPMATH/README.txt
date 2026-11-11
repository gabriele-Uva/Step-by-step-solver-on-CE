StepMath CE  -  a step-by-step math tutor for the TI-84 Plus CE
================================================================

You type a problem. StepMath does NOT hand you the answer. It shows ONE step
of the method at a time, in words, together with the equation or expression
as it stands after that step. You press a key to see the next step. The
final answer is itself the last step ("Final answer: x = 5").

Written in C for the CE C/C++ Toolchain (CEdev). Uses only graphx.h,
keypadc.h, fileioc.h, tice.h and the standard C library.

VERSION 2 ADDS (everything from version 1 is still here)
 * VENN DIAGRAMS with 2 or 3 sets: the regions are filled in one per step,
   drawn as a real Venn diagram, and a set expression such as n(A u B)' n C
   is worked out at the end. 3 sets are FULLY supported, not partial.
 * A 2nd KEY like the real calculator: 2nd, then x^2, types sqrt( .
   (Version 1 had no way to type a square root at all.) sqrt( now works in
   typed quadratic-formula expressions and inside equations.
 * DEL = EMERGENCY QUIT, from any screen. See "DEL" below. Erasing a
   character moved to CLEAR.


!!! DEL QUITS THE PROGRAM AT ONCE !!!
-------------------------------------
Press DEL on ANY screen (title, typing, in the middle of the steps, a Venn
diagram, the history, the help, a message...) and the program ends
immediately: no question, no "are you sure", nothing is saved (the problem
you were working on is lost). One press is enough; you do not have to hold
it. It is the panic button: it leaves the screen clean for the calculator's
OS. DEL does NOT erase text any more: use CLEAR to erase (see the key
legend).
(A calculator keypad can fake a key when three other keys are held at the
same time in one particular pattern. A DEL that those keys could have faked
is ignored, so it can never quit by accident. A real DEL on its own, or with
one other key held, always works. If it ever seems to do nothing because
you were holding several keys, let go of them and press DEL again.)


KEY LEGEND (also shown on the screens)
--------------------------------------
 TYPING A PROBLEM (New problem screen)
   0-9, X (the X,T,theta,n key), + - * / ^ ( )     as printed
   (-)            also types a minus sign
   Y=             types the equals sign =
   x^2            types ^2
   2nd, then x^2  types  sqrt(   ("2nd" lights up in the top right corner
                  of the screen; it goes off again after the very next
                  key, like the little arrow on the real calculator;
                  pressing 2nd twice cancels it; any other key after 2nd
                  just cancels it and does its normal job)
   CLEAR          erases the character before the cursor (a whole sqrt(
                  at once). Hold it to erase more. On an EMPTY line CLEAR
                  goes back to the menu.
   MODE           clears the whole line
   LEFT / RIGHT   move the cursor
   ENTER or GRAPH solve
   DEL            QUIT THE PROGRAM (see above)

 THE VENN FORM (Venn diagram: 2 sets / 3 sets in the main menu)
   UP / DOWN      choose the field        0-9   type a number (up to 999)
   ENTER          next field (on the last field: solve)
   GRAPH          solve from any field
   CLEAR          erase one character     MODE  back to the menu
   On the expression line (the last field) the letters are typed with the
   keys that have those letters printed on them:
        MATH = A      APPS = B      PRGM = C
        LOG  = n  (intersection, "and")        VARS = u  (union, "or")
        the comma key = '   (complement, "not")     ( and ) as usual
   LEFT / RIGHT move the cursor in the expression.

 WHILE SOLVING
   ENTER / RIGHT / DOWN / 2nd   next step       LEFT / UP   previous step
   CLEAR   back to typing (the problem is kept so you can fix it)
   after the last step: "Try another problem", "Show the full solution
   again", "Back to the main menu"

 HISTORY   ENTER solves one again, MODE removes the entry (it used to be
           DEL), CLEAR goes back.


WHAT IT CAN DO (and exactly which forms)
----------------------------------------
1. LINEAR EQUATIONS in x
     3x+5=20     2(x-4)=x+7     5-x=2x-1     -(x+3)=4
   Brackets (one bracket per term) and x on both sides are fine. Steps:
   expand brackets, combine like terms (each side), move terms, divide,
   then a "Check" step puts the answer back into the original equation.
   Special results, shown as a proper last step:
     2x+1=2x+3   ->  "no solution"
     2x+1=2x+1   ->  "every real number" (infinitely many solutions)

2. QUADRATIC EQUATIONS (anything that turns into ax^2+bx+c=0)
     x^2-5x+6=0     2x^2-2x-4=0     x^2+x=6     x^2=4     2x^2+5x+3=0
   - If the roots are whole numbers: factoring ("two numbers that multiply
     to c and add to b"). If a is not 1 and divides b and c, the first
     step divides every term by a (2x^2-2x-4=0 becomes x^2-x-2=0).
     An equation not yet in the form ...=0 first gets a step that moves
     everything to the left.
   - Otherwise (roots are fractions or irrational, or a does not divide
     b and c): the quadratic formula, e.g. 2x^2+5x+3=0.
     The formula path shows the discriminant b^2-4ac as its own step, then
     the square root (perfect square / simplified radical / decimal
     estimate), then the two roots reduced as far as possible, with a
     2-decimal estimate for irrational ones.
   - Negative discriminant: the answer is "no real solutions". Complex
     (imaginary) roots are NOT computed - this is a deliberate choice.
   - A double root (x^2-2x+1=0) is reported as one root "(a double root)".
   - NEW: you can also type sqrt( yourself, see 6.

3. SIMPLIFYING EXPRESSIONS
     3(x+2)+4x      2(x+1)-3(x-4)      x(x+4)-x^2      -(2x-1)+5
   One operation per step: expand ONE bracket, then combine like terms one
   power of x at a time. If there is nothing to do, it says so.

4. DERIVATIVES d/dx   (sum rule, constant rule, power rule ONLY)
     x^3+2x      3x^2+5x+7      4x^5-x
   The chain rule, product rule and quotient rule are NOT supported and the
   app says so ("Derivatives of brackets need the chain or product rule,
   which is not supported"). Expand the bracket yourself first, or use the
   Simplify option, then take the derivative of the result.

5. FRACTION ARITHMETIC: add, subtract, multiply, divide TWO fractions
   (a whole number counts as a fraction)
     1/2+3/4      5/6-1/4      2/3*5/7      (1/2)/(3/4)      3/4+2
   Steps: common denominator (the smallest one), rewrite, combine,
   simplify (gcd); the final answer also shows a decimal estimate.
   Division MUST be typed with brackets: (1/2)/(3/4). Plain 1/2/3/4 is
   refused because it would be ambiguous.

6. SQUARE ROOTS: typing sqrt( with 2nd + x^2  (NEW)
   The quadratic formula with the numbers put in, typed as one line, is
   worked out one operation at a time: the numbers under the root first,
   then the root (perfect square, simplified like sqrt(48) = 4sqrt(3), or
   left as a root with a decimal estimate), then the sums, the division,
   and the answer in lowest terms:
     (-5+sqrt(5^2-4*1*6))/(2*1)         -> -2
     (-3+sqrt(3^2-4*2*(-1)))/(2*2)      -> (-3 + sqrt(17))/4  (about 0.28)
     sqrt(48)+2        (4+2sqrt(3))/2        1/sqrt(2)        sqrt(3)^2
   Type a negative number inside a bracket: (-1). Type the + and the -
   version as two separate lines (there is no +/- key).
   In an equation with x, sqrt( is allowed only on numbers that are
   perfect squares: x^2+sqrt(25)x+6=0 is read as x^2+5x+6=0.
   Not supported (and the app says so): sqrt of x, sqrt of a negative
   number, sqrt of a fraction, a root inside a root, two different roots
   in one expression (sqrt(2)+sqrt(3)), complex numbers.
   Numbers inside such an expression must stay within about 10^9.

7. VENN DIAGRAMS: 2 sets and 3 sets (NEW)
   Choose "Venn diagram: 2 sets" or "3 sets" in the main menu and fill in
   the numbers, the way a worksheet gives them. n(A n B) means EVERYTHING in
   both A and B (the middle of a 3-set diagram included):
        2 sets: n(U) total, n(A), n(B), n(A n B), [outside: optional]
        3 sets: n(U), n(A), n(B), n(C), n(A n B), n(A n C), n(B n C),
                n(A n B n C), [outside: optional]
   Numbers are whole numbers from 0 to 999. "outside" (the elements in none
   of the sets) is optional; if you give it, it is checked.
   Word problems are not read: type the numbers into the form.
   Example: 20 students, 12 like A, 9 like B, 5 like both: U=20, A=12, B=9,
   A n B=5.

   STEPS: one region is filled in per step, each with a one-line
   explanation, and the count appears INSIDE the drawn diagram as soon as it
   is known (unknown regions show ?):
        3 sets: the middle (all three) first, then each pair-only region by
                subtracting the middle, then A only / B only / C only by
                subtracting the others, then the outside from n(U)
        2 sets: the overlap, then A only, then B only, then the outside
   The newest region is lit up. The right-hand panel shows the arithmetic,
   e.g.  20 - 5 - 4 - 3 = 8.

   THE EXPRESSION (optional, last field of the form), in set notation:
        A  B  C          the sets (C only in a 3-set problem)
        u                union (the "u" stands for the union sign)
        n                intersection (the "n" stands for the intersection
                         sign); n is also the count: n(...) is a bracket
                         that means "how many in this"
        '                complement ("not", everything in U outside it)
        ( )              brackets, nested as deep as you like (at most 8)
   Precedence: ' first, then n, then u. Examples that work:
        n(A)                 n(A u B)            n(A n B)
        A'                   n(B n C)'           n(A u B)' n C
        n((A n B) u C)       (A u B)' u C'       A n B' n C'
   n(X)' means (X)': the number in the complement of X. n(A u B)' n C means
   (A u B)' n C: the elements of C that are in neither A nor B.
   The expression is worked out after the diagram is full: each inner part
   of the expression gets its own step (its regions are lit up on the
   diagram and added up), and the last step is the answer, as one line like
        n(A u B) = n(A) + n(B) - n(A n B) = 12 + 9 - 5 = 16
   (for a union of two or three sets the inclusion-exclusion formula,
   for a complement  n(X)' = n(U) - n(X), otherwise the lit regions added).
   The notation is shown on the form itself and on the help pages.

   WHEN THE NUMBERS DO NOT FIT, the app says so instead of drawing a wrong
   diagram, and goes back to the form with your numbers still there:
        - a region that would come out below 0 (an overlap bigger than a
          set, n(A n B) smaller than n(A n B n C), a set too small for its
          overlaps)
        - the circles hold more than n(U)
        - the optional "outside" number does not match n(U) minus the circles
        - a missing number, or an expression that cannot be read (the
          position of the mistake is pointed out, and "C" in a 2-set
          problem is refused)
   Venn problems are not saved in the history (only the 5 other kinds are).


WHAT IT CANNOT DO (it says "I can't solve this type of problem yet")
--------------------------------------------------------------------
Never guessed at. These get a plain refusal with the reason:
 - chain / product / quotient rule derivatives, trig, logs, e^x, roots
   other than sqrt( on whole numbers as described in 6
 - equations of degree 3 or higher, systems of equations, inequalities
 - more than one variable (only the letter x), decimals (use fractions)
 - complex (imaginary) roots
 - x in a denominator or x inside a fraction (1/x, x/2) - "Division with x
   in it is not supported"
 - two brackets multiplying each other, e.g. (x+1)(x+2)
 - brackets inside brackets
 - three or more fractions in one problem
 - powers of x above 12; exponents that are not plain whole numbers
 - numbers longer than 4 digits; results that would overflow (see limits)
 - input longer than 38 characters (the Venn expression too)
 - Venn: 4 or more sets, word problems typed as text, numbers above 999


HOW TO TYPE A PROBLEM
---------------------
See the key legend at the top. Implicit multiplication works: 3x, 2(x+1),
x(x-2), 2sqrt(3) mean 3*x, 2*(x+1), x*(x-2), 2*sqrt(3). You may also write
the * explicitly.
If the line can't be read, the app shows a "^" under the spot where it got
stuck and a short reason (for example "Missing )" or "Only the letter x").

When a line has x but no = sign, it could be a simplify or a derivative
problem, so the app asks which one you want. A line with = is always an
equation; a line with no x is fraction arithmetic (if it has a "/"), a
square-root expression (if it has sqrt( ), or plain number arithmetic.

HISTORY: the last 5 problems (not Venn) are saved in the archived appvar
STEPHIST (fileioc). Main menu -> History. If the appvar can't be written
the app still works, it just forgets. DEL (quit) saves nothing.


LIMITS (24-bit int)
-------------------
The eZ80 int is 24 bits, so all maths that can get big uses 32-bit long
with explicit overflow checks. Typed numbers: up to 4 digits (Venn: 999).
Every coefficient that appears while solving must stay within +-1,000,000.
Quadratics: a, b and c must be within +-10,000 so b^2-4ac always fits.
Polynomial terms: at most 12 per side; brackets of at most 8 terms.
Square-root expressions: every value and the radicand within 10^9, at
most about 60 operations. When a limit is hit the app says so; it never
shows a wrong number.


BUILDING
--------
Folder layout:
    STEPMATH/makefile
    STEPMATH/src/main.c

 1. Install the CE C/C++ Toolchain (CEdev) - https://ce-programming.github.io/toolchain/
    and make sure it is on your PATH (cedev-config must work).
 2. Open a terminal in the STEPMATH folder and run:  make
 3. Send bin/STEPMATH.8xp to the calculator with TI Connect CE
    (or any CE link program). Run it from the Asm( menu or the Programs
    list (prgmSTEPMATH on OS 5.3+).
 NOTE FOR WINDOWS: check that the file really is called  main.c  and not
 main.c.txt (Explorer hides known extensions). Same for  makefile  - it has
 no extension at all.
 The txt copies (stepmath_main_c.txt, stepmath_makefile.txt,
 stepmath_README.txt) are for transferring the files in a form that cannot
 be mangled; rename them back to main.c, makefile and README.txt.

Size: about 42 KB compressed program, about 19 KB of BSS (the linker
limit is ~69 KB).
The program uses setjmp/longjmp from the standard C library for the DEL
quit (it jumps back to main(), which calls gfx_End() and returns).


HOW IT WAS TESTED - AND HONEST LIMITS
-------------------------------------
- Built with the real CEdev eZ80 compiler (v11.2, v12.1 and v13.0) with
  -Wall -Wextra -Oz: no warnings, no errors. (The nightly build of the
  toolchain was not available in this session, so it was not tried.)
  clang -fsyntax-only -Wall -Wextra -Wshadow -Wconversion against the real
  CEdev headers: clean. Static data (BSS): 19.2 KB.
- Version 1 tests, still passing on the new code: the step generator for
  linear / quadratic / simplify / derivative / fraction problems against
  independent references (about 450,000 checks per run, 0 failures).
- VENN, in the test program (the real main.c against stub headers): about
  3,000 problems per run (half 2 sets, half 3 sets) are built from known
  region counts, only the typed numbers are given to the game, and every
  region, every step order, every count in every step and every answer is
  compared with what was built. Includes 11 hand-checked 2-set and 9
  hand-checked 3-set problems (with the answers worked out by hand in the
  test), sets that are empty or equal, expressions generated at random with
  their own truth tables (about 940 per 3,000 problems are nested 2-3 levels
  deep, e.g. n(A u B)' n C, n((A n B) u C), (A u B)' u C', n(B n C)'), and
  the final one-line formula is re-added by the test. Inconsistent numbers:
  8 hand-made cases (overlap bigger than a set, middle bigger than a pair,
  a set too small for its overlaps, circles bigger than n(U), the optional
  outside number not matching, a blank) and about 1,200 random changes of one
  number, where the game had to agree with an independent consistency check
  every time; no message or step ever shows a negative count. Layout: every
  step text fits 3 lines and every calculation 10 lines of 13 columns.
- SQUARE ROOTS: 33 hand-picked expressions (perfect squares, simplified
  roots, 1/sqrt(2), conjugates, sqrt(2)*sqrt(2), ...), 10 that must be
  refused (negative root, two different roots, divide by 0, overflow) and
  1,500 random typed quadratic formulas (a, b, c random, + and -): the
  final value and EVERY step's expression are compared with an independent
  evaluator; 0 failures. Typed end to end with the real keys (2nd then x^2)
  through the UI: x^2+5x+6=0 gives -2, 2x^2+3x-1=0 gives (-3+sqrt(17))/4,
  and x^2+sqrt(25)x+6=0 is solved as a quadratic.
- 2nd KEY: through the real key presses: 2nd lights, x^2 types sqrt( and
  the indicator goes off, x^2 alone types ^2, 2nd twice cancels, any other
  key cancels, MODE clears the line.
- DEL: tested from 16 different screens (title, help, empty and typed input
  line, input with 2nd lit, the "cannot solve" message, the choose menu, in
  the middle of a linear and a square-root solution, the end menu, history,
  both Venn forms, in the middle of a 2-set and a 3-set diagram, the Venn
  "numbers don't fit" message and its end menu): each time the program left at
  once (no key needed afterwards), gfx_End() was called, main() returned,
  and nothing was written. Also: all 213,052 combinations of 1 to 4 keys
  through the keypad ghosting model: 920 of them read as DEL without DEL
  being pressed and none was accepted; a real DEL alone or with one other
  key always worked.
- Random keys: over a million frames of the whole UI (about 400,000 key
  scans in each of 5 long runs) with random arrows, 2nd, the Venn letters,
  typed problems (including sqrt( ones), typed Venn forms and DEL at random,
  under AddressSanitizer and UBSan with keypad ghosting on: no errors. This
  found and fixed one real bug (a cursor left outside a shorter line after a
  Venn error message). 140,000 random strings and 100,000 random Venn
  problems (blank, huge, inconsistent numbers, random expressions): always
  an answer or a message with a reason.
- As a check on the tests, 27 deliberately broken copies of the code (12
  from version 1, 7 in the Venn code, 4 in the square-root code, 2 in the 2nd
  key, 2 in DEL) were run: all 27 were caught.
- Screenshots of every screen (including the input line with 2nd lit, a 2-set
  diagram being filled in, a 3-set diagram complete with a nested expression,
  the "numbers don't fit" message, square-root walkthroughs) were rendered
  on the PC and checked by eye (docs/screenshots).
- Text layout is checked against 40 characters per line and the 320x240
  screen; nothing is cut off.
- NOT done: it has not been run on a real calculator or in an emulator
  (none was available). The PC build uses a simulated screen/keypad, so
  key timing, key-repeat feel, how the 2nd indicator looks on the real
  screen, the fileioc appvar and the setjmp quit on real hardware are
  untested. The eZ80 code itself does compile and link with CEdev. The
  keypad ghosting rule is the standard textbook one; real hardware may
  differ.
