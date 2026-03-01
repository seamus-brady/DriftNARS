# DriftScript Language Reference

DriftScript is a Lisp-like language that compiles to Narsese. It replaces angle
brackets, arrows, and cryptic copula symbols with S-expressions using human-readable
keywords.

DriftScript is a pure Python preprocessor: DriftScript text goes in, Narsese strings
come out, and those are fed directly to DriftNARS via `add_narsese()` or
`add_driftscript()`.

## Quick Start

```lisp
;; Teach the system some facts
(believe (inherit bird animal))
(believe (inherit robin bird))

;; Ask a question — the system deduces the answer
(ask (inherit robin animal))
;; => <robin --> animal>?

;; Teach a temporal rule and trigger it with a goal
(believe (predict (seq light_on (call ^press)) light_off))
(believe light_on :now)
(goal light_off)
```

## Sentence Forms

Every DriftScript program is a sequence of top-level forms. The three sentence forms
add input to the reasoner:

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

Options can be combined: `(believe x :now :truth 1.0 0.9)` produces `x. :|: {1.0 0.9}`.

## Terms and Copulas

Copulas are binary relations written in prefix form:

| DriftScript | Narsese | Meaning |
|-------------|---------|---------|
| `(inherit A B)` | `<A --> B>` | A is a kind of B |
| `(similar A B)` | `<A <-> B>` | A resembles B |
| `(imply A B)` | `<A ==> B>` | If A then B (eternal) |
| `(predict A B)` | `<A =/> B>` | If A then (temporally) B |
| `(equiv A B)` | `<A <=> B>` | A if and only if B |
| `(instance A B)` | `<A \|-> B>` | Instance relation |

Examples:

```lisp
(inherit robin bird)          ; → <robin --> bird>
(similar cat dog)             ; → <cat <-> dog>
(predict rain wet)            ; → <rain =/> wet>
```

## Connectors

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

`seq` accepts 2 or 3 arguments (matching DriftNARS's `MAX_SEQUENCE_LEN` of 3).

## `call` Shorthand

The `call` form provides a convenient way to express operation invocations:

```lisp
(call ^goto (ext-set SELF) park)   ; → <(*, {SELF}, park) --> ^goto>
(call ^press)                       ; → ^press  (no args = bare op name)
```

With arguments, `call` builds a product term and wraps it in an inheritance statement
with the operation. Without arguments, it emits just the bare operation name.

Common pattern — temporal rule with an operation:

```lisp
(believe (predict (seq at_home (call ^goto (ext-set SELF) park)) at_park))
;; → <(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>.
```

## Variables

DriftScript allows descriptive variable names. They are automatically mapped to
numbered Narsese variables (`$1`-`$9`, `#1`-`#9`, `?1`-`?9`):

| Prefix | Type | Example |
|--------|------|---------|
| `$` | Independent (universal) | `$x` → `$1` |
| `#` | Dependent (existential) | `#thing` → `#1` |
| `?` | Query | `?what` → `?1` |

The same name always maps to the same number within one sentence. Numbering resets
between sentences.

```lisp
(believe (imply (inherit $x bird) (inherit $x animal)))
;; → <($1 --> bird) ==> ($1 --> animal)>.

(believe (imply (inherit $animal $class) (similar $class $animal)))
;; → <($1 --> $2) ==> ($2 <-> $1)>.
```

Already-numbered variables (`$1`, `#2`, etc.) pass through unchanged. Named variables
avoid collisions with explicitly-numbered ones.

## Meta Commands

| DriftScript | Effect |
|-------------|--------|
| `(cycles N)` | Run N inference cycles |
| `(def-op ^name)` | Register an operation |
| `(reset)` | Reset the reasoner |
| `(config key value)` | Set a config parameter |
| `(concurrent)` | Mark next input as same timestep |

Valid config keys: `volume`, `motorbabbling`, `decisionthreshold`,
`anticipationconfidence`, `questionpriming`, `babblingops`, `similaritydistance`.

```lisp
(def-op ^press)
(config volume 0)
(cycles 10)
```

## Comments

`;` starts a comment that extends to end of line (Lisp convention):

```lisp
; This is a comment
(believe (inherit bird animal))  ; inline comment
```

## Full Narsese-DriftScript Equivalence

| Narsese | DriftScript |
|---------|-------------|
| `<bird --> animal>.` | `(believe (inherit bird animal))` |
| `<robin --> animal>?` | `(ask (inherit robin animal))` |
| `<cat <-> dog>.` | `(believe (similar cat dog))` |
| `light_on. :\|:` | `(believe light_on :now)` |
| `light_off! :\|:` | `(goal light_off)` |
| `<(light_on &/ ^press) =/> light_off>.` | `(believe (predict (seq light_on (call ^press)) light_off))` |
| `<($1 --> bird) ==> ($1 --> animal)>.` | `(believe (imply (inherit $x bird) (inherit $x animal)))` |
| `<(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>.` | `(believe (predict (seq at_home (call ^goto (ext-set SELF) park)) at_park))` |
| `(*, A, B, C)` | `(product A B C)` |
| `{SELF}` | `(ext-set SELF)` |
| `[bright, loud]` | `(int-set bright loud)` |
| `(-- A)` | `(not A)` |
| `(A && B)` | `(and A B)` |

## Integration

### Standalone compiler

```python
from driftscript import DriftScript

ds = DriftScript()

# Compile to structured results
results = ds.compile("(believe (inherit bird animal))")
# => [CompileResult(kind='narsese', value='<bird --> animal>.')]

# Get Narsese strings only (raises on directives)
narsese = ds.to_narsese("(believe (inherit A B))")
# => ['<A --> B>.']
```

### With DriftNARS

```python
from driftnars import DriftNARS

with DriftNARS() as nar:
    nar.on_answer(lambda n, f, c, occ, ct: print(f"Answer: {n}"))

    nar.add_driftscript("""
        (believe (inherit bird animal))
        (believe (inherit robin bird))
        (ask (inherit robin animal))
    """)
```

`add_driftscript()` handles all result types automatically — Narsese input, shell
commands, cycle execution, and operation registration.
