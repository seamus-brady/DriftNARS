# CLAUDE.md — DriftNARS

Quick-reference for AI assistants working on this codebase.

---

## What this is

DriftNARS is a C99 Non-Axiomatic Reasoning System (NARS). It implements Non-Axiomatic
Logic (NAL) levels 1–8, providing temporal/procedural reasoning, uncertainty handling,
and adaptive decision making.

Forked from [OpenNARS for Applications (ONA)](https://github.com/opennars/OpenNARS-for-Applications)
at commit [`dc4efd0`](https://github.com/opennars/OpenNARS-for-Applications/commit/dc4efd0abd520cdb79bf53bfa3c285ebb24f2e8a),
then stripped to a minimal embeddable library core. See `engineering_log.md` for
the full change history.

---

## Build

```bash
make                        # builds bin/driftnars + .a + .dylib/.so
make OPENMP=1               # with OpenMP threading
make test                   # run all unit + system tests
make clean                  # remove build artifacts
bin/driftnars shell         # interactive Narsese REPL
```

Two-stage build: Stage 1 compiles a bootstrap binary, runs it to generate `src/RuleTable.c`,
then Stage 2 compiles the final binary with `STAGE=2` defined. `src/RuleTable.c` is a
generated file — do not edit it manually.

---

## Public API (the library interface)

```c
// Lifecycle
NAR_t *NAR_New(void);                    // heap-allocate + zero-init
void   NAR_Free(NAR_t *nar);
int    NAR_INIT(NAR_t *nar);             // returns NAR_OK

// Input
int    NAR_AddInputNarsese(NAR_t *nar, char *sentence);
int    NAR_AddInputNarsese2(NAR_t *nar, char *sentence, bool query, double threshold);
Event  NAR_AddInputBelief(NAR_t *nar, Term term);
Event  NAR_AddInputGoal(NAR_t *nar, Term term);

// Execution
void   NAR_Cycles(NAR_t *nar, int cycles);
void   NAR_AddOperation(NAR_t *nar, char *op_name, Action callback);
void   NAR_AddOperationName(NAR_t *nar, const char *op_name);  // no-op action (use with callbacks)

// Output callbacks (all optional — pass NULL handler to disable)
void   NAR_SetEventHandler(NAR_t *nar, NAR_EventHandler handler, void *userdata);
void   NAR_SetAnswerHandler(NAR_t *nar, NAR_AnswerHandler handler, void *userdata);
void   NAR_SetDecisionHandler(NAR_t *nar, NAR_DecisionHandler handler, void *userdata);
void   NAR_SetExecutionHandler(NAR_t *nar, NAR_ExecutionHandler handler, void *userdata);

// Term to string
int    Narsese_SprintTerm(NAR_t *nar, Term *term, char *buf, int bufsize);

// Reason codes: NAR_EVENT_INPUT=1, NAR_EVENT_DERIVED=2, NAR_EVENT_REVISED=3
// Error codes (defined in src/Globals.h)
// NAR_OK = 0, NAR_ERR_PARSE = -1, NAR_ERR_MEM = -2, NAR_ERR_INIT = -3
```

Minimal usage:
```c
NAR_t *nar = NAR_New();
NAR_INIT(nar);
NAR_AddInputNarsese(nar, "<bird --> animal>.");
NAR_AddInputNarsese(nar, "<robin --> bird>.");
NAR_AddInputNarsese(nar, "<robin --> animal>?");
NAR_Free(nar);
```

---

## Key architectural facts

### NAR_t is the entire instance

All mutable state lives in `NAR_t` (defined in `src/NAR.h`). There are no meaningful
file-scope globals except two in `Truth.c` (see below). Every function that touches
instance state takes `NAR_t *nar` as its first parameter.

### Truth.c global sync (known limitation)

`Truth_Projection` and `Truth_Eternalize` read two C globals:
`TRUTH_EVIDENTIAL_HORIZON` and `TRUTH_PROJECTION_DECAY`. These are synced from the
instance on every `NAR_INIT` call. For single-instance or strictly sequential
multi-instance use this is transparent. Interleaving calls between two instances
with *different* horizon/decay values would cause interference. Fixing this requires
threading `NAR_t *nar` into every Truth function — deferred.

### Operation callbacks

```c
typedef Feedback (*Action)(Term args);
```

All operation callbacks take a single `Term args` parameter. They do **not** receive
`NAR_t *nar`. If a callback needs to feed input back to the reasoner, it must hold its
own `NAR_t *` reference externally.

### Output callbacks

Four optional callback types let library consumers receive structured events instead of
parsing stdout. All use flat C primitives (no struct params) for FFI compatibility:

- **`NAR_EventHandler`** — fires for every input/derived/revised event
- **`NAR_AnswerHandler`** — fires when a question is answered
- **`NAR_DecisionHandler`** — fires when a decision is made above threshold
- **`NAR_ExecutionHandler`** — fires when an operation is about to execute

Callbacks fire **before** the existing printf output, regardless of `PRINT_INPUT` /
`PRINT_DERIVATIONS` flags. Set a handler to `NULL` to disable it.

Python bindings: `examples/python/driftnars.py` wraps the shared library via ctypes.

### Narsese syntax quick reference

```
<bird --> animal>.          # inheritance belief (eternal)
<robin --> bird>. :|:       # belief at current time
<cat <-> dog>?              # similarity question
<robin --> animal>?         # question (answered from memory)
<(A &/ ^op) =/> B>.         # temporal implication (A then op leads to B)
^goto({SELF} * {target})!   # goal: execute operation
```

Copulas: `:` inheritance, `=` similarity, `$` temporal implication, `?` implication,
`^` equivalence, `+` sequence, `&/` sequential conjunction, `;` conjunction.

### Tense

- No tense = eternal
- `:|:` = present (current time)
- `:\:` = past
- `:/:` = future (questions only; future belief events are not supported)

---

## Source file map

| File | Role |
|------|------|
| `src/NAR.h/.c` | Public API, `NAR_t` definition, lifecycle |
| `src/Cycle.h/.c` | Main reasoning loop (`Cycle_Perform`) |
| `src/Decision.h/.c` | Decision making, motor babbling, anticipation |
| `src/Memory.h/.c` | Concept storage, priority queues, hash tables |
| `src/NAL.h/.c` | All inference rules (generated into RuleTable.c at build time) |
| `src/Narsese.h/.c` | Parser and term encoder |
| `src/Inference.h/.c` | Temporal/procedural inference primitives |
| `src/Truth.h/.c` | Truth-function arithmetic |
| `src/Variable.h/.c` | Variable unification and substitution |
| `src/InvertedAtomIndex.h/.c` | Fast concept lookup by atom |
| `src/OccurrenceTimeIndex.h/.c` | Time-ordered event index |
| `src/Event.h/.c` | Event construction |
| `src/Stamp.h/.c` | Evidential base (circular reasoning prevention) |
| `src/Table.h/.c` | Fixed-size sorted implication tables |
| `src/Shell.h/.c` | Interactive REPL and command processing |
| `src/Stats.h/.c` | Runtime statistics |
| `src/Globals.h/.c` | Error codes, assert macro, hash, RNG |
| `src/Config.h` | All compile-time parameters (226+ constants) |
| `src/main.c` | Entry point: test runner, shell, rule-table generation |
| `src/unit_tests/` | 10 unit tests |
| `src/system_tests/` | 13 system tests |
| `examples/python/driftnars.py` | Python ctypes wrapper class |
| `examples/python/example.py` | Python usage example |

**Pure value functions (no `nar` param):** `Truth_*`, `Stamp_*`, `Term_*`,
`PriorityQueue_*`, `HashTable_*`, `Stack_*`, `Usage_*`.

---

## Key configuration parameters (`src/Config.h`)

| Parameter | Default | Meaning |
|-----------|---------|---------|
| `CONCEPTS_MAX` | 4096 | Max concepts in memory |
| `ATOMS_MAX` | 255 | Max distinct atoms |
| `OPERATIONS_MAX` | 10 | Max registered operations |
| `STAMP_SIZE` | 10 | Max evidential base size (silent truncation beyond this) |
| `MAX_SEQUENCE_LEN` | 3 | Max length of `&/` sequences |
| `TABLE_SIZE` | 20 | Max implications per concept slot |
| `DECISION_THRESHOLD_INITIAL` | 0.501 | Min expectation to execute an operation |
| `MOTOR_BABBLING_CHANCE_INITIAL` | 0.2 | Random exploration rate |
| `TRUTH_EVIDENTIAL_HORIZON_INITIAL` | 1.0 | Prior evidence weight |
| `TRUTH_PROJECTION_DECAY_INITIAL` | 0.8 | Time-decay factor for temporal projection |

Runtime-tunable fields live directly on `NAR_t` (e.g. `nar->DECISION_THRESHOLD`,
`nar->MOTOR_BABBLING_CHANCE`).

---

## Invariants to preserve

1. **Truth values always in [0.0, 1.0]** — `frequency` and `confidence` both.
   `confidence` is additionally bounded away from 1.0 by `Truth_w2c` arithmetic.
2. **Temporal ordering** — in `Inference_BeliefIntersection` and `Inference_BeliefInduction`,
   `b->occurrenceTime >= a->occurrenceTime` must hold. Violations now return `*success = false`.
3. **Stamp non-overlap for revision** — `Stamp_checkOverlap` must return false before
   `Truth_Revision` is applied. Enforced in `Inference_ImplicationRevision` via choice fallback.
4. **Sequence length** — `&/` sequences must not exceed `MAX_SEQUENCE_LEN`. Violations in
   goal decomposition now return early.
5. **NAR_INIT before use** — asserted on all public functions.

---

## Common pitfalls

- **Don't edit `src/RuleTable.c`** — it is regenerated on every build.
- **Operation names must start with `^`** — `NAR_AddOperation` asserts this.
- **Eternal goals are not supported** — `NAR_AddInputNarsese` returns `NAR_ERR_PARSE` for `!`
  without a tense marker.
- **Future/past beliefs not supported** — `.` with `:\:` or `:/:`  returns `NAR_ERR_PARSE`.
- **Adding a new module** that accesses instance state requires threading `NAR_t *nar` through
  its call chain and adding the fields to `NAR_t` in `src/NAR.h`.
- **Truth.c global sync**: if adding a second NAR instance with different tuning, call
  `NAR_INIT` on the active instance before using its truth functions.
