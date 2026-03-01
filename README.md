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

with DriftNARS() as nar:
    nar.on_answer(lambda n, f, c, occ, ct: print(f"Answer: {n} f={f:.2f} c={c:.2f}"))
    nar.add_narsese("<bird --> animal>.")
    nar.add_narsese("<robin --> bird>.")
    nar.add_narsese("<robin --> animal>?")
```

See `examples/python/` for a complete example with all four callback types.

## License

MIT. See [LICENSE](LICENSE).
