StepMath CE  -  a step-by-step math tutor for the TI-84 Plus CE
================================================================

You type a problem. StepMath does NOT hand you the answer. It shows ONE step
of the method at a time, in words, together with the equation or expression
as it stands after that step. You press a key to see the next step. The
final answer is itself the last step ("Final answer: x = 5").

Written in C for the CE C/C++ Toolchain (CEdev). Uses only graphx.h,
keypadc.h, fileioc.h, tice.h and the standard C library.


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


WHAT IT CANNOT DO (it says "I can't solve this type of problem yet")
--------------------------------------------------------------------
Never guessed at. These get a plain refusal with the reason:
 - chain / product / quotient rule derivatives, trig, logs, e^x, roots
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
 - input longer than 38 characters


HOW TO TYPE A PROBLEM
---------------------
 Digits        0-9
 x             the X,T,theta,n key
 + - * /       the normal operator keys
 minus         the - key or the (-) key
 ^             the ^ key; the x^2 key types ^2
 ( )           the bracket keys
 =             the Y= key (the calculator has no = key)
 DEL           erase left of the cursor (hold to repeat)
 LEFT / RIGHT  move the cursor
 ENTER / GRAPH solve
 CLEAR         clear the line; CLEAR on an empty line goes to the menu

Implicit multiplication works: 3x, 2(x+1), x(x-2) mean 3*x, 2*(x+1),
x*(x-2). You may also write the * explicitly.
If the line can't be read, the app shows a "^" under the spot where it got
stuck and a short reason (for example "Missing )" or "Only the letter x").

When a line has x but no = sign, it could be a simplify or a derivative
problem, so the app asks which one you want. A line with = is always an
equation; a line with no x is fraction arithmetic (if it has a "/") or
plain number arithmetic.


KEYS WHILE SOLVING
------------------
 ENTER / RIGHT / DOWN / 2nd   next step
 LEFT / UP                    previous step
 CLEAR                        back to the input line (problem kept so you
                              can edit it)
After the final answer: "Try another problem", "Show the full solution
again" (from step 1), "Back to the main menu".

HISTORY: the last 5 problems are saved in the archived appvar STEPHIST
(fileioc). Main menu -> History; ENTER solves one again, DEL removes it.
If the appvar can't be written the app still works, it just forgets.


LIMITS (24-bit int)
-------------------
The eZ80 int is 24 bits, so all maths that can get big uses 32-bit long
with explicit overflow checks. Typed numbers: up to 4 digits. Every
coefficient that appears while solving must stay within +-1,000,000.
Quadratics: a, b and c must be within +-10,000 so b^2-4ac always fits.
Polynomial terms: at most 12 per side; brackets of at most 8 terms.
When a limit is hit the app says so; it never shows a wrong number.


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

Size: about 24 KB compressed program, about 18 KB of BSS (the linker
limit is ~69 KB).


HOW IT WAS TESTED - AND HONEST LIMITS
-------------------------------------
- Built with the real CEdev eZ80 compiler (v11.2, v12.1, v13.0 and the
  nightly) with -Wall -Wextra -Oz: no warnings or errors from the code.
  (The nightly linker prints a harmless note about its own library files.)
  Also passes clang -fsyntax-only -Wall -Wextra -Wshadow -Wconversion
  against the real CEdev headers.
- The step generator was run on the PC (the real main.c with stub graphx /
  keypadc / fileioc headers) on tens of thousands of generated problems of
  every type, including negative coefficients, a=1 versus a>1, fractions
  already in lowest terms, no-solution / infinite-solution equations and
  negative discriminants, plus several hundred thousand random garbage
  strings. Every step's equation/expression was checked against an
  independent evaluator (it must still be true for the roots, or equal the
  original expression), and final answers against an exact reference:
  several million checks, 0 failures. As a check on the checker, 12
  deliberately broken calculations were put into a copy of the code and
  all 12 were caught.
- The whole UI was run for millions of frames with random key presses and
  typed-in problems under AddressSanitizer + UBSan: no errors.
- Screenshots of every screen type and a walkthrough of each problem type
  were rendered on the PC and checked by eye (tests/png).
- Text layout is checked against 40 characters per line and the 320x240
  screen; nothing is cut off.
- NOT done: it has not been run on a real calculator or in an emulator
  (none was available). The PC build uses a simulated screen/keypad, so
  key timing, key-repeat feel and the fileioc appvar on real hardware are
  untested. The eZ80 code itself does compile and link with CEdev.
