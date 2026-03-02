# DriftNARS

A fast, embeddable reasoning engine in C99. DriftNARS implements
[Non-Axiomatic Logic](https://cis.temple.edu/~pwang/NARS-Intro.html) (NAL) levels 1-8
— temporal reasoning, uncertainty handling, learning from experience, and goal-driven
decision making — in a single-file-includable C library with no external dependencies.

Unlike neural networks, NARS reasons symbolically with explicit logic: it deduces new
knowledge from what it's told, learns cause-and-effect from observation, and takes
actions to achieve goals — all with built-in uncertainty tracking. Every conclusion
carries a truth value that says how much evidence supports it.

Forked from [OpenNARS for Applications (ONA)](https://github.com/opennars/OpenNARS-for-Applications)
at commit [`dc4efd0`](https://github.com/opennars/OpenNARS-for-Applications/commit/dc4efd0abd520cdb79bf53bfa3c285ebb24f2e8a),
then refactored into a clean embeddable library core.

## Table of Contents

- [Build](#build)
- [Quick Start](#quick-start)
- [DriftScript](#driftscript)
  - [DriftScript REPL](#driftscript-repl)
  - [Tutorial](#tutorial)
  - [DriftScript vs Narsese](#driftscript-vs-narsese)
- [Narsese Shell](#narsese-shell)
- [C Library](#c-library)
- [Python](#python)
- [Documentation](#documentation)
- [License](#license)

## Build

```bash
make                # builds bin/driftnars + .a + .dylib/.so
make OPENMP=1       # with OpenMP threading
make test           # run all unit + system tests
make clean          # remove build artifacts
```

Two-stage build: Stage 1 compiles a bootstrap binary that generates the inference
rule table (`src/RuleTable.c`), then Stage 2 compiles the final binary into `bin/`.

## Quick Start

```bash
make
bin/driftnars driftscript
```

```
driftscript> (believe (inherit "robin" "bird"))
driftscript> (believe (inherit "bird" "animal"))
driftscript> (ask (inherit "robin" "animal"))
Answer: <robin --> animal>. ... Truth: frequency=1.000000, confidence=0.810000
```

You give the system facts and questions — it reasons out the answers. We never said
robin is an animal; it deduced that from the two inheritance links. The confidence of
0.81 (vs the input's 0.9) reflects that this is an indirect conclusion — the system
tracks evidence strength automatically.

## DriftScript

DriftScript is DriftNARS's human-friendly input language. It compiles to Narsese
(the underlying logic language) but replaces angle brackets and cryptic copula symbols
with readable S-expressions:

```lisp
; Teach the system some facts
(believe (inherit "bird" "animal"))
(believe (inherit "robin" "bird"))

; Ask a question — the system deduces the answer
(ask (inherit "robin" "animal"))

; Goal-driven action: teach a rule, provide a state, set a goal — the system acts
(def-op ^press)
(believe (predict (seq "light_on" (call ^press)) "light_off"))
(believe "light_on" :now)
(goal "light_off")
; => ^press executed
```

Concept names are quoted strings. Keywords (`believe`, `inherit`, `seq`), variables
(`$x`, `?what`), and operations (`^press`) stay unquoted.

### DriftScript REPL

```bash
bin/driftnars driftscript
```

The DriftScript REPL compiles input on the fly and feeds it directly to the reasoner.
Multi-line input is supported — the prompt changes to `...>` while parentheses are
unbalanced. Line editing, history, and Ctrl-C/Ctrl-D work as expected. Type `quit`
to exit.

Piped input and scripts work too — the prompt is suppressed automatically:

```bash
bin/driftnars driftscript < examples/driftscript/01_hello.ds
```

### Tutorial

The [`examples/driftscript/`](examples/driftscript/) directory contains 10 progressive
tutorial files, each self-contained and runnable. Start from zero and work through
deduction, temporal reasoning, learning from experience, and multi-step planning:

| File | Topic |
|------|-------|
| [`01_hello.ds`](examples/driftscript/01_hello.ds) | First beliefs, questions, deduction |
| [`02_truth.ds`](examples/driftscript/02_truth.ds) | Truth values: frequency, confidence, expectation |
| [`03_copulas.ds`](examples/driftscript/03_copulas.ds) | All 6 relationship types |
| [`04_connectors.ds`](examples/driftscript/04_connectors.ds) | Compound terms: sets, products, sequences |
| [`05_time.ds`](examples/driftscript/05_time.ds) | Temporal reasoning and predictions |
| [`06_operations.ds`](examples/driftscript/06_operations.ds) | Goals, actions, and decision making |
| [`07_variables.ds`](examples/driftscript/07_variables.ds) | Universal, existential, and query variables |
| [`08_learning.ds`](examples/driftscript/08_learning.ds) | Learning rules from observation |
| [`09_multistep.ds`](examples/driftscript/09_multistep.ds) | Multi-step planning and goal decomposition |
| [`10_config.ds`](examples/driftscript/10_config.ds) | Tuning: volume, thresholds, cycles, reset |

```bash
bin/driftnars driftscript < examples/driftscript/01_hello.ds
```

### DriftScript vs Narsese

DriftScript compiles to Narsese. You can use either — DriftScript is more readable,
Narsese is more compact:

| Narsese | DriftScript |
|---------|-------------|
| `<bird --> animal>.` | `(believe (inherit "bird" "animal"))` |
| `<robin --> animal>?` | `(ask (inherit "robin" "animal"))` |
| `light_off! :\|:` | `(goal "light_off")` |
| `<($1 --> bird) ==> ($1 --> animal)>.` | `(believe (imply (inherit $x "bird") (inherit $x "animal")))` |
| `<(light_on &/ ^press) =/> light_off>.` | `(believe (predict (seq "light_on" (call ^press)) "light_off"))` |

## Narsese Shell

For direct Narsese input — the raw logic language without the DriftScript layer:

```bash
bin/driftnars shell
```

```
driftnars> <bird --> animal>.
driftnars> <robin --> bird>.
driftnars> <robin --> animal>?
Answer: <robin --> animal>. creationTime=2 Truth: frequency=1.000000, confidence=0.810000
```

The Narsese shell has full line editing (arrow keys, Home/End, backspace), a 32-entry
command history (up/down arrows), Ctrl-C to clear the line, and Ctrl-D to exit. Piped
input and scripts work automatically with prompt suppression:

```bash
echo '<bird --> animal>.' | bin/driftnars shell
```

Type `help` at the prompt for a summary of all Narsese input formats, tense markers,
truth value syntax, and `*` commands (e.g., `*volume=0`, `*motorbabbling=0.1`).

Both shells share the same reasoning engine and the same line editing — choose
DriftScript for readability or Narsese for directness.

## C Library

DriftNARS compiles to a static library (`libdriftnars.a`) and a shared library
(`libdriftnars.dylib`/`.so`) for embedding in any C/C++ application:

```c
#include "NAR.h"

NAR_t *nar = NAR_New();
NAR_INIT(nar);

NAR_AddInputNarsese(nar, "<bird --> animal>.");
NAR_AddInputNarsese(nar, "<robin --> bird>.");
NAR_AddInputNarsese(nar, "<robin --> animal>?");

NAR_Cycles(nar, 100);
NAR_Free(nar);
```

Link with `-lm -lpthread`.

All mutable state lives in the `NAR_t` struct — no hidden globals — so you can run
multiple independent reasoner instances in the same process.

### Output callbacks

Instead of parsing stdout, register structured callbacks for answers, decisions,
operation execution, and inference events:

```c
void my_answer(void *ud, const char *narsese, double freq, double conf,
               long occTime, long createTime) {
    printf("Got answer: %s f=%.2f c=%.2f\n", narsese, freq, conf);
}

NAR_SetAnswerHandler(nar, my_answer, NULL);
```

Four callback types: `NAR_SetEventHandler`, `NAR_SetAnswerHandler`,
`NAR_SetDecisionHandler`, `NAR_SetExecutionHandler`. All are optional and
use flat C primitives for easy FFI integration.

## Python

A ctypes wrapper provides the full DriftNARS API from Python, with both Narsese
and DriftScript input:

```python
from driftnars import DriftNARS

with DriftNARS() as nar:
    nar.on_answer(lambda n, f, c, occ, ct: print(f"Answer: {n} f={f:.2f} c={c:.2f}"))
    nar.on_execution(lambda op, args: print(f"{op} executed"))

    # DriftScript — the readable way
    nar.add_driftscript("""
        (def-op ^press)
        (believe (predict (seq "light_on" (call ^press)) "light_off"))
        (believe "light_on" :now)
        (goal "light_off")
    """)

    # Or raw Narsese
    nar.add_narsese("<bird --> animal>.")
```

See [`examples/python/`](examples/python/) for complete examples with all four
callback types.

## Documentation

| Document | Description |
|----------|-------------|
| [`docs/driftscript_reference.md`](docs/driftscript_reference.md) | DriftScript language reference |
| [`docs/narsese_primer.md`](docs/narsese_primer.md) | Narsese language reference |
| [`examples/driftscript/`](examples/driftscript/) | DriftScript tutorial (10 progressive lessons) |
| [`examples/python/`](examples/python/) | Python integration examples |
| [`engineering_log.md`](engineering_log.md) | Change history from the ONA fork |

## License

MIT. See [LICENSE](LICENSE).
