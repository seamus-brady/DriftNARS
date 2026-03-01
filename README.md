# DriftNARS

A minimalist Non-Axiomatic Reasoning System (NARS) in C99.

DriftNARS implements Non-Axiomatic Logic (NAL) levels 1-8, providing temporal and
procedural reasoning, uncertainty handling, and adaptive decision making.

Forked from [OpenNARS for Applications (ONA)](https://github.com/opennars/OpenNARS-for-Applications)
at commit [`dc4efd0`](https://github.com/opennars/OpenNARS-for-Applications/commit/dc4efd0abd520cdb79bf53bfa3c285ebb24f2e8a),
then refactored into a clean embeddable library core.

## Build

```bash
make                # builds bin/driftnars + .a + .dylib/.so
make OPENMP=1       # with OpenMP threading
make test           # run all unit + system tests
make clean          # remove build artifacts
```

Two-stage build: Stage 1 compiles a bootstrap binary that generates the inference
rule table (`src/RuleTable.c`), then Stage 2 compiles the final binary into `bin/`.

## Usage

### Interactive shell

```bash
bin/driftnars shell
```

The shell provides a `driftnars>` prompt with line editing (arrow keys, Home/End,
backspace), command history (up/down arrows, 32-entry ring buffer), Ctrl-C to clear
the current line, and Ctrl-D on an empty line to exit. When stdin is a pipe or file
the prompt and line editing are disabled automatically, so scripted usage works as
expected:

```bash
echo '<bird --> animal>.' | bin/driftnars shell
```

Type `help` at the prompt for a summary of all Narsese input formats and `*` commands.

### C library

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

Link against `libdriftnars.a` (static) or `libdriftnars.dylib`/`.so` (shared)
with `-lm -lpthread`.

#### Output callbacks

Instead of parsing stdout, library consumers can register structured callbacks:

```c
void my_answer(void *ud, const char *narsese, double freq, double conf,
               long occTime, long createTime) {
    printf("Got answer: %s f=%.2f c=%.2f\n", narsese, freq, conf);
}

NAR_SetAnswerHandler(nar, my_answer, NULL);
```

Four callback types are available: `NAR_SetEventHandler`, `NAR_SetAnswerHandler`,
`NAR_SetDecisionHandler`, `NAR_SetExecutionHandler`. All are optional — callbacks
that are not set remain silent.

### Python

```python
from driftnars import DriftNARS

def on_execution(op_name, args):
    """Called by DriftNARS when it decides to execute an operation.

    op_name: e.g. "^goto"
    args:    Narsese product string, e.g. "({SELF} * park)", or "" if none
    """
    if op_name == "^goto":
        target = args.strip("()").split(" * ")[-1]
        print(f"Going to {target}!")

with DriftNARS() as nar:
    nar.on_answer(lambda n, f, c, occ, ct: print(f"Answer: {n} f={f:.2f} c={c:.2f}"))
    nar.on_execution(on_execution)

    # Register an operation — DriftNARS will call on_execution when it fires
    nar.add_operation("^goto")

    # Teach: "if at home and you goto(SELF,park), you arrive at park"
    nar.add_narsese("<(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>.")

    # Precondition holds, give a goal — triggers ^goto with args
    nar.add_narsese("at_home. :|:")
    nar.add_narsese("at_park! :|:")
    # => on_execution called with op_name="^goto", args="({SELF} * park)"
```

See `examples/python/` for a complete example with all four callback types
and `docs/narsese_primer.md` for a comprehensive Narsese language reference.

#### DriftScript

DriftScript is a Lisp-like language that compiles to Narsese, replacing angle brackets
and cryptic copula symbols with readable S-expressions. All concept names are quoted
as string literals for unambiguous parsing:

```python
# Instead of raw Narsese:
nar.add_narsese("<(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>.")

# Write DriftScript:
nar.add_driftscript('(believe (predict (seq "at_home" (call ^goto (ext-set "SELF") "park")) "at_park"))')
```

Side-by-side comparison:

| Narsese | DriftScript |
|---------|-------------|
| `<bird --> animal>.` | `(believe (inherit "bird" "animal"))` |
| `<robin --> animal>?` | `(ask (inherit "robin" "animal"))` |
| `light_off! :\|:` | `(goal "light_off")` |
| `<($1 --> bird) ==> ($1 --> animal)>.` | `(believe (imply (inherit $x "bird") (inherit $x "animal")))` |

Keywords (`believe`, `inherit`, `seq`), variables (`$x`), and operations (`^press`)
stay unquoted — only concept names need quotes.

See `docs/driftscript_reference.md` for the full language reference and
`examples/python/example_driftscript.py` for a complete example.

## License

MIT. See [LICENSE](LICENSE).
