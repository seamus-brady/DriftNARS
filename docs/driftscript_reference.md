# DriftScript Language Reference

DriftScript is a Lisp-like language that compiles to Narsese. It replaces angle
brackets, arrows, and cryptic copula symbols with S-expressions using human-readable
keywords.

The easiest way to use DriftScript is the built-in REPL:

```bash
bin/driftnars driftscript            # interactive REPL
bin/driftnars driftscript < file.ds  # run a script
```

DriftScript also works from Python (`nar.add_driftscript(...)`) and as a standalone
compiler (`bin/driftscript`). See the [Integration](#integration) section for details.

## Quick Start

```lisp
;; Teach the system some facts
(believe (inherit "bird" "animal"))
(believe (inherit "robin" "bird"))

;; Ask a question — the system deduces the answer
(ask (inherit "robin" "animal"))
;; => <robin --> animal>?

;; Teach a temporal rule and trigger it with a goal
(believe (predict (seq "light_on" (call ^press)) "light_off"))
(believe "light_on" :now)
(goal "light_off")
```

## String Literals

All concept names and values must be quoted with double quotes. This prevents
bareword confusion and makes the boundary between language keywords and user data
unambiguous.

**Must be quoted** (double quotes):
- Concept names: `"bird"`, `"animal"`, `"light_on"`, `"SELF"`
- Any atom that represents data/content rather than structure

**Must NOT be quoted** (bare symbols):
- Keywords: `believe`, `ask`, `goal`, `inherit`, `similar`, `seq`, etc.
- Options: `:now`, `:past`, `:future`, `:truth`, `:dt`
- Variables: `$x`, `#thing`, `?what`, `$1`
- Operations: `^press`, `^goto`, `^grab`
- Numbers in `:truth` and `:dt`: `1.0`, `0.9`, `5`
- Config keys and values: `volume`, `100`

**Escape sequences** (inside double quotes):
- `\"` — literal double quote
- `\\` — literal backslash

No other escapes are supported. Strings cannot span multiple lines.

```lisp
;; Correct
(believe (inherit "robin" "bird"))
(believe (predict (seq "light_on" (call ^press)) "light_off"))
(ask (inherit ?x "animal"))

;; Wrong — bare atoms rejected
(believe (inherit robin bird))     ; ERROR: Atom 'robin' must be a string literal
```

## Sentence Forms

Every DriftScript program is a sequence of top-level forms. The three sentence forms
add input to the reasoner:

### `believe` — assert a belief

```lisp
;; Eternal belief (no tense)
(believe (inherit "bird" "animal"))
;; => <bird --> animal>.

;; Present-tense belief
(believe "light_on" :now)
;; => light_on. :|:

;; With explicit truth value (frequency, confidence)
(believe (inherit "bird" "animal") :truth 1.0 0.9)
;; => <bird --> animal>. {1.0 0.9}

;; Present-tense with truth value
(believe "light_on" :now :truth 1.0 0.9)
;; => light_on. :|: {1.0 0.9}

;; With temporal offset (dt)
(believe (predict "a" "b") :now :dt 5)
;; => dt=5 <a =/> b>. :|:
```

Truth values must be numbers in `[0.0, 1.0]`. The `:dt` value must be an integer.

### `ask` — pose a question

```lisp
;; Eternal question
(ask (inherit "robin" "animal"))
;; => <robin --> animal>?

;; Present-tense question
(ask (inherit "robin" "animal") :now)
;; => <robin --> animal>? :|:

;; Past-tense question
(ask (inherit "robin" "animal") :past)
;; => <robin --> animal>? :\:

;; Future-tense question
(ask (inherit "robin" "animal") :future)
;; => <robin --> animal>? :/:
```

Questions cannot have `:truth` values.

### `goal` — declare a goal

```lisp
;; Goal (always present-tense automatically)
(goal "light_off")
;; => light_off! :|:

;; Goal with truth value
(goal "light_off" :truth 1.0 0.9)
;; => light_off! :|: {1.0 0.9}
```

Goals cannot use `:past` or `:future` tense.

### Summary table

| Form | Narsese | Notes |
|------|---------|-------|
| `(believe <term>)` | `<term>.` | Eternal belief |
| `(believe <term> :now)` | `<term>. :\|:` | Present-tense belief |
| `(believe <term> :truth F C)` | `<term>. {F C}` | With explicit truth value |
| `(believe <term> :now :dt N)` | `dt=N <term>. :\|:` | With temporal offset |
| `(ask <term>)` | `<term>?` | Eternal question |
| `(ask <term> :now)` | `<term>? :\|:` | Present-tense question |
| `(ask <term> :past)` | `<term>? :\:` | Past-tense question |
| `(ask <term> :future)` | `<term>? :/:` | Future-tense question |
| `(goal <term>)` | `<term>! :\|:` | Goal (always present-tense) |
| `(goal <term> :truth F C)` | `<term>! :\|: {F C}` | Goal with truth value |

Options can be combined: `(believe "x" :now :truth 1.0 0.9)` produces `x. :|: {1.0 0.9}`.

## Terms and Copulas

Copulas are binary relations written in prefix form:

### `inherit` — inheritance (`-->`)

A is a kind of B, or A has the properties of B.

```lisp
(believe (inherit "robin" "bird"))
;; => <robin --> bird>.
```

### `similar` — similarity (`<->`)

A and B share properties in both directions.

```lisp
(believe (similar "cat" "dog"))
;; => <cat <-> dog>.
```

### `imply` — implication (`==>`)

If A then B (eternal, non-temporal).

```lisp
(believe (imply "rain" "wet_ground"))
;; => <rain ==> wet_ground>.
```

### `predict` — predictive implication (`=/>`)

If A then B, with a temporal ordering (A happens before B).

```lisp
(believe (predict "rain" "wet_ground"))
;; => <rain =/> wet_ground>.
```

### `equiv` — equivalence (`<=>`)

A if and only if B.

```lisp
(believe (equiv "bachelor" "unmarried_man"))
;; => <bachelor <=> unmarried_man>.
```

### `instance` — instance relation (`|->`)

A is an instance of B (A belongs to class B as a specific member).

```lisp
(believe (instance "Tweety" "bird"))
;; => <Tweety |-> bird>.
```

### Copula summary table

| DriftScript | Narsese | Meaning |
|-------------|---------|---------|
| `(inherit A B)` | `<A --> B>` | A is a kind of B |
| `(similar A B)` | `<A <-> B>` | A resembles B |
| `(imply A B)` | `<A ==> B>` | If A then B (eternal) |
| `(predict A B)` | `<A =/> B>` | If A then (temporally) B |
| `(equiv A B)` | `<A <=> B>` | A if and only if B |
| `(instance A B)` | `<A \|-> B>` | Instance relation |

All copulas require exactly 2 arguments.

## Connectors

### `seq` — sequential conjunction (`&/`)

Events in temporal order. Accepts 2 or 3 arguments (matching DriftNARS's
`MAX_SEQUENCE_LEN` of 3).

```lisp
(believe (predict (seq "light_on" (call ^press)) "light_off"))
;; => <(light_on &/ ^press) =/> light_off>.

(believe (predict (seq "a" "b" "c") "d"))
;; => <(a &/ b &/ c) =/> d>.
```

### `and` — conjunction (`&&`)

Both A and B hold.

```lisp
(believe (imply (and (inherit $x "bird") (inherit $x "flyer")) (inherit $x "animal")))
;; => <<(<$1 --> bird> && <$1 --> flyer>) ==> <$1 --> animal>>>.
```

### `or` — disjunction (`||`)

Either A or B holds.

```lisp
(believe (imply (or "rain" "sprinkler") "wet_grass"))
;; => <(rain || sprinkler) ==> wet_grass>.
```

### `not` — negation (`--`)

The negation of A. Unary (exactly 1 argument).

```lisp
(believe (imply (not "rain") "dry_ground"))
;; => <(-- rain) ==> dry_ground>.
```

### `product` — product (`*`)

N-ary product term (1 or more arguments).

```lisp
(believe (inherit (product "A" "B" "C") "relation"))
;; => <(*, A, B, C) --> relation>.
```

### `ext-set` — extensional set (`{...}`)

One or more elements as an extensional set.

```lisp
(believe (inherit (ext-set "SELF") "person"))
;; => <{SELF} --> person>.

(believe (inherit (ext-set "red" "green" "blue") "color"))
;; => <{red, green, blue} --> color>.
```

### `int-set` — intensional set (`[...]`)

One or more elements as an intensional set.

```lisp
(believe (inherit "x" (int-set "bright" "loud")))
;; => <x --> [bright, loud]>.
```

### `ext-inter` — extensional intersection (`&`)

```lisp
(believe (inherit (ext-inter "A" "B") "C"))
;; => <(&, A, B) --> C>.
```

### `int-inter` — intensional intersection (`|`)

```lisp
(believe (inherit (int-inter "A" "B") "C"))
;; => <(|, A, B) --> C>.
```

### `ext-diff` — extensional difference (`-`)

```lisp
(believe (inherit (ext-diff "A" "B") "C"))
;; => <(-, A, B) --> C>.
```

### `int-diff` — intensional difference (`~`)

```lisp
(believe (inherit (int-diff "A" "B") "C"))
;; => <(~, A, B) --> C>.
```

### `ext-image1`, `ext-image2` — extensional image (`/1`, `/2`)

```lisp
(believe (inherit (ext-image1 "R" "X") "Y"))
;; => <(/1, R, X) --> Y>.

(believe (inherit (ext-image2 "R" "X") "Y"))
;; => <(/2, R, X) --> Y>.
```

### `int-image1`, `int-image2` — intensional image (`\1`, `\2`)

```lisp
(believe (inherit (int-image1 "R" "X") "Y"))
;; => <(\1, R, X) --> Y>.

(believe (inherit (int-image2 "R" "X") "Y"))
;; => <(\2, R, X) --> Y>.
```

### Connector summary table

| DriftScript | Narsese | Notes |
|-------------|---------|-------|
| `(seq A B)` | `(A &/ B)` | Sequential conjunction (2-3 args) |
| `(and A B)` | `(A && B)` | Conjunction |
| `(or A B)` | `(A \|\| B)` | Disjunction |
| `(not A)` | `(-- A)` | Negation |
| `(product A B C)` | `(*, A, B, C)` | Product (n-ary) |
| `(ext-set A B)` | `{A, B}` | Extensional set |
| `(int-set A B)` | `[A, B]` | Intensional set |
| `(ext-inter A B)` | `(&, A, B)` | Extensional intersection |
| `(int-inter A B)` | `(\|, A, B)` | Intensional intersection |
| `(ext-diff A B)` | `(-, A, B)` | Extensional difference |
| `(int-diff A B)` | `(~, A, B)` | Intensional difference |
| `(ext-image1 R X)` | `(/1, R, X)` | Extensional image (place 1) |
| `(ext-image2 R X)` | `(/2, R, X)` | Extensional image (place 2) |
| `(int-image1 R X)` | `(\1, R, X)` | Intensional image (place 1) |
| `(int-image2 R X)` | `(\2, R, X)` | Intensional image (place 2) |

## `call` Shorthand

The `call` form provides a convenient way to express operation invocations:

### With arguments

Builds a product term and wraps it in an inheritance statement with the operation:

```lisp
(call ^goto (ext-set "SELF") "park")
;; => <(*, {SELF}, park) --> ^goto>
```

### Without arguments

Emits just the bare operation name:

```lisp
(call ^press)
;; => ^press
```

### Common pattern — temporal rule with an operation

```lisp
(believe (predict (seq "at_home" (call ^goto (ext-set "SELF") "park")) "at_park"))
;; => <(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>.
```

## Variables

DriftScript allows descriptive variable names. They are automatically mapped to
numbered Narsese variables (`$1`-`$9`, `#1`-`#9`, `?1`-`?9`):

### `$` — independent (universal) variables

Used in implications and general rules. The same `$name` always maps to the same
number within one sentence.

```lisp
(believe (imply (inherit $x "bird") (inherit $x "animal")))
;; => <<$1 --> bird> ==> <$1 --> animal>>.

(believe (imply (inherit $animal $class) (similar $class $animal)))
;; => <<$1 --> $2> ==> <$2 <-> $1>>.
```

### `#` — dependent (existential) variables

Something exists but is not independently quantified.

```lisp
(believe (imply (inherit $x "bird") (inherit $x #y)))
;; => <<$1 --> bird> ==> <$1 --> #1>>.
```

### `?` — query variables

Used in questions to ask "what fills this slot?":

```lisp
(ask (inherit ?x "animal"))
;; => <?1 --> animal>?
```

### Numbered variables

Already-numbered variables (`$1`, `#2`, etc.) pass through unchanged. Named variables
avoid collisions with explicitly-numbered ones:

```lisp
(believe (imply (inherit $1 "bird") (inherit $x "animal")))
;; $1 is reserved, so $x gets $2
;; => <<$1 --> bird> ==> <$2 --> animal>>.
```

Numbering resets between sentences — each top-level form has its own variable scope.

## Meta Commands

### `cycles` — run inference cycles

```lisp
(cycles 10)
```

Runs 10 inference cycles. The argument must be a positive integer.

### `def-op` — register an operation

```lisp
(def-op ^press)
(def-op ^goto)
```

Registers an operation name with the reasoner. The name must start with `^`.

### `reset` — reset the reasoner

```lisp
(reset)
```

Clears all memory and resets the reasoner state.

### `config` — set a configuration parameter

```lisp
(config volume 0)
(config decisionthreshold 0.6)
(config motorbabbling 0.1)
```

Valid config keys: `volume`, `motorbabbling`, `decisionthreshold`,
`anticipationconfidence`, `questionpriming`, `babblingops`, `similaritydistance`.

### `concurrent` — mark next input as same timestep

```lisp
(concurrent)
```

Marks the next input as occurring at the same time step as the previous one.

## Comments

`;` starts a comment that extends to end of line (Lisp convention):

```lisp
; This is a comment
(believe (inherit "bird" "animal"))  ; inline comment
```

## Full Narsese-DriftScript Equivalence

| Narsese | DriftScript |
|---------|-------------|
| `<bird --> animal>.` | `(believe (inherit "bird" "animal"))` |
| `<robin --> animal>?` | `(ask (inherit "robin" "animal"))` |
| `<cat <-> dog>.` | `(believe (similar "cat" "dog"))` |
| `light_on. :\|:` | `(believe "light_on" :now)` |
| `light_off! :\|:` | `(goal "light_off")` |
| `<Tweety \|-> bird>.` | `(believe (instance "Tweety" "bird"))` |
| `<(light_on &/ ^press) =/> light_off>.` | `(believe (predict (seq "light_on" (call ^press)) "light_off"))` |
| `<($1 --> bird) ==> ($1 --> animal)>.` | `(believe (imply (inherit $x "bird") (inherit $x "animal")))` |
| `<(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>.` | `(believe (predict (seq "at_home" (call ^goto (ext-set "SELF") "park")) "at_park"))` |
| `(*, A, B, C)` | `(product "A" "B" "C")` |
| `{SELF}` | `(ext-set "SELF")` |
| `[bright, loud]` | `(int-set "bright" "loud")` |
| `(-- A)` | `(not "A")` |
| `(A && B)` | `(and "A" "B")` |

## Integration

### Tutorial

For a progressive, hands-on introduction see `examples/driftscript/` — 10 runnable
`.ds` files that build from first beliefs through temporal reasoning and multi-step
planning. Start with:

```bash
bin/driftnars driftscript < examples/driftscript/01_hello.ds
```

### Interactive REPL

The `driftscript` subcommand launches an interactive REPL that compiles DriftScript
on the fly and feeds results directly to the reasoner:

```bash
bin/driftnars driftscript
```

```
driftscript> (believe (inherit "bird" "animal"))
Input: <bird --> animal>. ...
driftscript> (believe (inherit "robin" "bird"))
Input: <robin --> bird>. ...
driftscript> (ask (inherit "robin" "animal"))
Answer: <robin --> animal>. ...
```

Multi-line input is supported — the prompt changes to `...>` while parentheses are
unbalanced:

```
driftscript> (believe (predict
...>   (seq "light_on" (call ^press))
...>   "light_off"))
Input: ...
```

Shell commands (`*volume=0`, `*stats`, etc.) and cycle shorthands (`5`, empty line)
work at the top level. Comments starting with `;` are ignored. Type `quit` to exit.

Piped input also works:

```bash
echo '(believe (inherit "bird" "animal"))' | bin/driftnars driftscript
```

### Standalone compiler (CLI)

```bash
# Compile DriftScript to Narsese on stdout
echo '(believe (inherit "bird" "animal"))' | bin/driftscript
# => <bird --> animal>.

# Pipe directly into the DriftNARS shell
echo '(believe (inherit "bird" "animal"))' | bin/driftscript | bin/driftnars shell

# Run inline tests
bin/driftscript --test
```

### With DriftNARS (Python)

```python
from driftnars import DriftNARS

with DriftNARS() as nar:
    nar.on_answer(lambda n, f, c, occ, ct: print(f"Answer: {n}"))

    nar.add_driftscript("""
        (believe (inherit "bird" "animal"))
        (believe (inherit "robin" "bird"))
        (ask (inherit "robin" "animal"))
    """)
```

`add_driftscript()` handles all result types automatically — Narsese input, shell
commands, cycle execution, and operation registration.
