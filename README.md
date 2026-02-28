# DriftNARS

A minimalist Non-Axiomatic Reasoning System (NARS) in C99.

DriftNARS implements Non-Axiomatic Logic (NAL) levels 1-8, providing temporal and
procedural reasoning, uncertainty handling, and adaptive decision making.

Forked from [OpenNARS for Applications (ONA)](https://github.com/opennars/OpenNARS-for-Applications)
at commit [`dc4efd0`](https://github.com/opennars/OpenNARS-for-Applications/commit/dc4efd0abd520cdb79bf53bfa3c285ebb24f2e8a),
then refactored into a clean embeddable library core.

## Build

```bash
make                # builds bin/driftnars + bin/libdriftnars.a
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

Link against `libdriftnars.a` with `-lm -lpthread`.

## License

MIT. See [LICENSE](LICENSE).
