# ONA Engineering Log

## Project: Production Library Refactor

**Goal:** Promote OpenNARS-for-Applications from a research binary into an embeddable C library
(`libONA.a` / `libONA.so`) suitable for production use by external consumers (NASA JPL, Cisco, etc.).

**Constraint:** All `make test` / `./NAR test` tests must pass after every phase.

---

## Phase 1 — Type System Cleanup

**Problem:** Two `#define` type aliases bypassed the C type system, causing silent truncation bugs
and making code harder to analyse with static tools.

### 1a. `Atom` typedef (`src/Config.h`)

```c
// Before
#define Atom unsigned char

// After
#include <stdint.h>
typedef uint8_t Atom;
```

`uint8_t` is unambiguous (exactly 8 bits, unsigned), matches the old `unsigned char` semantics
on all platforms, and participates in type checking.

### 1b. `Hash_t` typedef (`src/Globals.h`, `src/Term.h`, `src/Term.c`, `src/HashTable.h`, `src/HashTable.c`, `src/Globals.c`, `src/Narsese.c`)

```c
// Before
#define HASH_TYPE long

// After
typedef int64_t Hash_t;
```

`long` is 32 bits on Windows 64-bit, 64 bits on Linux/macOS. `int64_t` is always 64 bits.
All `HASH_TYPE` references replaced with `Hash_t` across the codebase.

### 1c. Empty-param functions → `(void)`

47 functions declared as `void f()` (K&R style — any argument list accepted) changed to
`void f(void)` (C99 prototype — no arguments accepted). Files affected:
`Cycle.c/h`, `Event.c/h`, `NAL.c/h`, `Shell.c/h`, `Memory.c/h`, `Narsese.c/h`,
`InvertedAtomIndex.c/h`, all unit test and system test headers.

Eliminates `-Wstrict-prototypes` warnings across the board. `RuleTable.c` (generated file)
is exempt.

**Verification:** `make clean && make` — zero strict-prototypes warnings.

---

## Phase 2 — Assert / Diagnostic Cleanup

**Problem:** The custom `assert` macro printed to stdout and called `exit(1)`, making it
unsuitable for library use (kills the host process, pollutes stdout).

### 2a. stderr output (`src/Globals.c`)

```c
// Before: fputs(message, stdout); exit(1);
// After:  fputs(message, stderr); fputc('\n', stderr); exit(1);
```

### 2b. NDEBUG support (`src/Globals.h`)

```c
#ifdef NDEBUG
  #define assert(b, msg) ((void)(b))
#else
  #define assert(b, msg) Globals_assert((b), (msg))
#endif
```

Release builds compiled with `-DNDEBUG` no longer pay the assert overhead. Internal guards
still fire in debug builds.

### 2c. Error codes (`src/Globals.h`)

Added public-API error code constants here (rather than `NAR.h`) to avoid circular includes —
`Narsese.c` needs the codes but cannot include `NAR.h` because `NAR.h` includes `Narsese.h`.

```c
#define NAR_OK          0
#define NAR_ERR_PARSE  -1   /* Narsese parse / input format error */
#define NAR_ERR_MEM    -2   /* memory full — input dropped (non-fatal) */
#define NAR_ERR_INIT   -3   /* called before NAR_INIT */
```

**Verification:** `make test` passes. No test triggers an assert.

---

## Phase 3 — NAR Context Struct (core library change)

**Problem:** All mutable state (concepts, atom table, current time, RNG seed, thresholds, etc.)
lived in file-scope global variables spread across ~15 source files. This made it impossible
to instantiate two independent NAR reasoners in the same process.

**Solution:** Define `NAR_t` — a single heap-allocated struct containing all instance state.
Every function that accesses that state gains a `NAR_t *nar` first parameter.

### 3a. `NAR_t` definition (`src/NAR.h`)

The struct is grouped by origin module and contains (in order):
- Memory storage: concept arrays, priority queues, hash tables, event buffers
- Narsese: atom name/value arrays, hash table, parser scratch buffers
- InvertedAtomIndex: chain element storage and stack
- Runtime-tunable flags: `PRINT_DERIVATIONS`, `PRINT_INPUT`, etc.
- Per-instance tunables: thresholds (`DECISION_THRESHOLD`, `CONDITION_THRESHOLD`, etc.)
- Truth parameters: `TRUTH_EVIDENTIAL_HORIZON`, `TRUTH_PROJECTION_DECAY`
- Core state: `currentTime`, `op_k`, `initialized`, `rand_seed`

Lifecycle functions added:
```c
NAR_t *NAR_New(void);   // malloc + zero-init
void   NAR_Free(NAR_t *nar);
```

### 3b. Module-by-module changes

Every module's globals were deleted and replaced with `nar->field` references. The
`NAR_t *nar` parameter was threaded through every affected function. Key discoveries
that were **not** in the original plan:

- **`Table_AddAndRevise`** needed `NAR_t *nar` — it calls `Inference_ImplicationRevision`
  which calls `Narsese_copulaEquals`. The plan incorrectly stated Table functions needed
  no changes. Fixed in `src/Table.h`, `src/Table.c`, and all callers.

- **`countStatementAtoms` / `countHigherOrderStatementAtoms`** in `Variable.c` are static
  helper functions that call `Narsese_copulaEquals` and `Narsese_IsSimpleAtom`. Both needed
  `NAR_t *nar` added.

- **`Cycle_PopEvents`** calls `Memory_printAddedEvent(nar, ...)` but had no `nar` parameter
  in the original implementation. Fixed by adding `NAR_t *nar` and updating its two callers
  in `Cycle_Perform`.

- **`Cycle_GoalSequenceDecomposition` / `Cycle_DeclarativeGoalReasoning`** referenced
  bare `currentTime` without a `currentTime` parameter — these functions access time via
  `nar->currentTime` now.

### 3c. Truth.c global sync (known limitation)

`Truth.c` functions `Truth_Projection` and `Truth_Eternalize` use two C globals
(`TRUTH_EVIDENTIAL_HORIZON`, `TRUTH_PROJECTION_DECAY`) for performance. These are synced
from the instance on every `NAR_INIT` call:

```c
TRUTH_EVIDENTIAL_HORIZON = nar->TRUTH_EVIDENTIAL_HORIZON;
TRUTH_PROJECTION_DECAY   = nar->TRUTH_PROJECTION_DECAY;
```

**Implication:** Sequential single-instance use is fully equivalent to the original.
Interleaving calls between two instances with *different* horizon/decay settings would
cause the second-initialised instance's values to apply to both. A full fix would pass
`nar` into every `Truth_Projection` call — deferred.

### 3d. `main.c` update

```c
int main(int argc, char *argv[]) {
    NAR_t *nar = NAR_New();
    mysrand(nar, 666);
    ...
    NAR_Free(nar);
    return 0;
}
```

### 3e. Test runners

`Run_Unit_Tests(NAR_t *nar)` and `Run_System_Tests(NAR_t *nar)` updated.
All individual test functions receive `NAR_t *nar` and call `NAR_INIT` internally.

### 3f. NetworkNAR (`src/NetworkNAR/UDPNAR.c`)

Holds its own `NAR_t *nar` allocated at startup, passed into `Cycle_Perform` and
`Shell_ProcessInput`.

**Verification:** `make clean && make && ./NAR test` — all 27 tests pass.

---

## Phase 4 — Error Codes on Public API

**Problem:** Public API functions called `exit(1)` on bad user input, making the library
unusable as an embedded component. Callers had no way to detect or recover from parse errors.

### 4a. `Narsese_Sentence` return type (`src/Narsese.h`, `src/Narsese.c`)

Changed from `void` to `int`. Five internal asserts that fired on malformed Narsese input
replaced with:
```c
fputs("<description>\n", stderr);
return NAR_ERR_PARSE;
```
The five cases: input too short, input exceeds `NARSESE_LEN_MAX`, missing truth value
delimiters, missing punctuation separator, invalid punctuation character.

`return NAR_OK;` added at end of successful parse.

### 4b. `NAR_INIT`, `NAR_AddInputNarsese`, `NAR_AddInputNarsese2` (`src/NAR.h`, `src/NAR.c`)

All changed from `void` to `int`. `NAR_AddInputNarsese2` propagates the `Narsese_Sentence`
return code. Two remaining input-validation asserts replaced with `return NAR_ERR_PARSE`:
- Eternal goals (`!` with no tense) are not supported
- Future/past belief events (`.` with tense >= 2) are not supported

**Verification:** `./NAR test` passes. Smoke test confirmed error paths return `NAR_ERR_PARSE`
without crashing.

---

## Phase 5 — Audit and Invariant Hardening

After Phase 4, a comprehensive audit classified every assert replacement and checked
related invariants. Key findings and resolutions:

### 5a. Shell.c ignored return values (lines 406, 431)

`NAR_AddInputNarsese2` (in `*query` handler) and `NAR_AddInputNarsese` (default input path)
return values were discarded. Added checks:

```c
if(NAR_AddInputNarsese(nar, line) != NAR_OK)
{
    fputs("//Error: input parse failed\n", stderr);
}
```

### 5b. Temporal ordering guards (`src/Inference.c`, lines 43, 58)

`Inference_BeliefIntersection` and `Inference_BeliefInduction` asserted
`b->occurrenceTime >= a->occurrenceTime`. Under NDEBUG this was a no-op, meaning a
temporally-reversed pair would produce a negative `occurrenceTimeOffset` and corrupt the
resulting implication term.

Replaced with early-return guards using the existing `*success` mechanism:
```c
if(b->occurrenceTime < a->occurrenceTime) { *success = false; return (Event/Implication){0}; }
```

### 5c. Sequence length bound (`src/Cycle.c`, line 167)

`Cycle_GoalSequenceDecomposition` asserted `i <= MAX_SEQUENCE_LEN` while iterating into
a stack-allocated array of size `MAX_SEQUENCE_LEN+1`. Under NDEBUG, an overlong sequence
would write beyond the array end.

Replaced with an early return:
```c
if(i > MAX_SEQUENCE_LEN) { fputs("Warning: ...\n", stderr); return false; }
```

### 5d. Truth bounds clamping (`src/Table.c`, `src/Decision.c`)

Six asserts guarding truth value ranges `[0.0, 1.0]` before and after implication revision,
and one assert on negative confirmation confidence, replaced with defensive clamps:

```c
value = MIN(1.0, MAX(0.0, value));
```

For mathematically correct inputs the clamp is a no-op. For out-of-range values (indicating
a bug in a Truth function) the clamp prevents corrupt data from entering the knowledge base.

### 5e. Findings deferred (not changed)

- **Stamp truncation**: At `STAMP_SIZE=10`, stamps are silently truncated when the evidential
  base exceeds 10 elements. Long derivation chains lose history. No change made — this is
  a known capacity limit, not a bug.
- **~35 remaining internal asserts**: Asserts guarding programmer-error conditions
  (null pointers, array bounds in internal data structures) remain as asserts. Under NDEBUG
  they become no-ops — accepted as the standard C library tradeoff.
- **Full Truth.c context threading**: Deferred (see Phase 3c note above).

**Verification:** `./NAR test` — all 27 tests pass.

---

## Files Modified Summary

| File | Phases | Nature of change |
|------|--------|-----------------|
| `src/Config.h` | 1a | `typedef uint8_t Atom` |
| `src/Globals.h` | 1b, 2b, 2c | `typedef int64_t Hash_t`; NDEBUG assert macro; error codes |
| `src/Globals.c` | 1b, 2a | `Globals_Hash` return type; stderr; `myrand`/`mysrand` gain `NAR_t*` |
| `src/Term.h/.c` | 1b | `Hash_t` rename |
| `src/HashTable.h/.c` | 1b | `Hash_t` rename |
| `src/NAR.h/.c` | 1c, 3, 4 | `NAR_t` definition; `NAR_New/Free`; error-returning public API |
| `src/Memory.h/.c` | 1c, 3 | Remove globals; add `NAR_t*` |
| `src/Narsese.h/.c` | 1c, 3, 4 | Remove globals; add `NAR_t*`; `Narsese_Sentence` returns `int` |
| `src/Event.h/.c` | 1c, 3 | Remove globals; add `NAR_t*` |
| `src/Decision.h/.c` | 1c, 3, 5d | Remove globals; add `NAR_t*`; truth clamp |
| `src/Truth.h/.c` | 3 | Remove globals; sync pattern retained for horizon/decay |
| `src/Variable.h/.c` | 1c, 3 | `similarity_distance` into `NAR_t`; static helpers gain `NAR_t*` |
| `src/Table.h/.c` | 3, 5d | `Table_AddAndRevise` gains `NAR_t*`; truth clamps replace asserts |
| `src/Inference.h/.c` | 1c, 3, 5b | Add `NAR_t*`; temporal ordering guards |
| `src/InvertedAtomIndex.h/.c` | 1c, 3 | Remove globals; add `NAR_t*` |
| `src/OccurrenceTimeIndex.h/.c` | 1c, 3 | Add `NAR_t*` |
| `src/NAL.h/.c` | 1c, 3 | Add `NAR_t*` |
| `src/Cycle.h/.c` | 1c, 3, 5c | Remove static IDs; add `NAR_t*`; sequence length guard |
| `src/Stats.h/.c` | 3 | Remove globals; add `NAR_t*` |
| `src/Shell.h/.c` | 1c, 3, 5a | Add `NAR_t*`; check return values |
| `src/main.c` | 3 | Use `NAR_New()` / `NAR_Free()` |
| `src/unit_tests/*.h` | 1c, 3 | `Run_Unit_Tests(NAR_t*)`; all test functions gain `NAR_t*` |
| `src/system_tests/*.h` | 1c, 3 | `Run_System_Tests(NAR_t*)`; all test functions gain `NAR_t*` |
| `src/NetworkNAR/UDPNAR.c/.h` | 3 | Holds own `NAR_t*` |

**Unchanged:** `src/Stamp.c/h`, `src/PriorityQueue.c/h`, `src/Stack.c/h`, `src/Usage.c/h`,
`src/RuleTable.c` (generated). `Truth_*`, `Stamp_*`, `Term_*`, `PriorityQueue_*`,
`HashTable_*`, `Stack_*`, `Usage_*` operate purely on passed values and needed no `nar` param.

---

## Mathematical Invariants

The NAL inference rules (deduction, induction, abduction, revision, intersection,
eternalization, projection) are **unchanged**. All truth-function formulas in `Truth.c`
and all rule applications in `NAL.c` are identical to the pre-refactor code.

The refactor affects only:
- **Storage layout** (struct vs globals — same data, same operations)
- **Error handling** (graceful rejection vs crash on bad input)
- **Edge-case robustness** (temporal ordering, sequence length, truth bound clamping)

For valid inputs the system is mathematically equivalent to the original.

---

## Phase 6 — Interactive Shell with Line Editing

**Problem:** The shell was a bare `fgets()` loop with no prompt, no line editing, no command
history, no help text, and 10 hardcoded no-op operations (`^left`, `^right`, `^up`, `^down`,
`^say`, `^pick`, `^drop`, `^go`, `^activate`, `^deactivate`) that consumed all `OPERATIONS_MAX`
slots.

### 6a. New module: `Linedit` (`src/Linedit.h`, `src/Linedit.c`)

Self-contained raw termios line editor, ~190 lines, zero external dependencies beyond
`<termios.h>`, `<unistd.h>`, `<string.h>`, `<stdio.h>`, `<stdbool.h>`.

**API:**
```c
char *Linedit_Read(const char *prompt);  // returns line from static buffer, NULL on EOF
void  Linedit_Cleanup(void);             // restores terminal state
```

**Design decisions:**

- **isatty gate** — if stdin is not a TTY (pipe, file, redirect), falls back to plain `fgets()`
  with no prompt. All existing scripted/piped usage is preserved unchanged.
- **Raw mode is per-line** — enters raw mode before reading, restores cooked mode before
  returning. Engine output (derivations, decisions, answers) happens between prompts in normal
  cooked mode, so there are no interleaving or display issues.
- **32-entry ring buffer history** — up/down arrow navigation, consecutive duplicate
  suppression. Current input is saved/restored when browsing history.
- **Editing:** left/right arrow cursor movement, backspace (127 and 8), Home/End (Ctrl-A/E
  and `\e[H`/`\e[F` escape sequences), Ctrl-C clears line, Ctrl-D on empty line returns EOF.
- **Line refresh** uses `\r\033[K` (carriage return + clear to end of line) — works on any
  ANSI terminal without needing to track previous line length.

### 6b. Shell cleanup (`src/Shell.c`)

**`Shell_NARInit`** — removed all 10 hardcoded no-op operation registrations. The function
now does only `NAR_INIT(nar)` + `nar->PRINT_DERIVATIONS = true`. This frees all
`OPERATIONS_MAX` slots for actual user/application operations. The `Shell_op_nop` function
is retained — it is used by the `*setopname` command to give a default action to operations
registered via the shell protocol.

**`Shell_Start`** — replaced `fgets()` loop with `Linedit_Read("driftnars> ")`. Added
`Linedit_Cleanup()` on exit to ensure terminal state is restored.

**`Shell_ProcessInput`** — added `help` command (also accepts `:help` and `*help`). Prints
a categorised summary of Narsese input formats and all `*` commands. Inserted early in the
if/else chain so it is checked before the Narsese parser.

### 6c. No changes needed elsewhere

- `src/Shell.h` — public API (`Shell_Start`, `Shell_NARInit`, `Shell_ProcessInput`) unchanged.
- `Makefile` — `$(wildcard src/*.c)` automatically picks up `Linedit.c`.
- System tests — `Bandrobot_Test.h` and others call `Shell_ProcessInput` directly (never
  `Shell_NARInit`), so removing the no-op operations has zero impact on test behaviour.

**Verification:** `make clean && make` — zero warnings. `make test` — all tests pass.
Interactive testing confirmed: prompt display, arrow key editing, history navigation,
Ctrl-C/Ctrl-D behaviour, `help` output, and piped input fallback.

---

## Phase 7 — Library Output Callbacks, Shared Library, Python Bindings

**Problem:** All DriftNARS output (derivations, answers, decisions, executions) was printed
directly to stdout via `printf`/`fputs`. Library consumers had no programmatic way to receive
these events — they could only parse stdout text.

### 7a. `Narsese_SprintTerm` (`src/Narsese.h`, `src/Narsese.c`)

Added a buffer-writing counterpart to `Narsese_PrintTerm`. Three static helpers
(`sprint_append`, `Narsese_SprintAtom`, `Narsese_SprintTermRecursive`) mirror the existing
print functions mechanically — every `fputs(str, stdout)` becomes `sprint_append(ctx, str)`.

```c
#define NARSESE_SPRINT_BUFSIZE NARSESE_LEN_MAX
int Narsese_SprintTerm(NAR_t *nar, Term *term, char *buf, int bufsize);
```

Returns the number of characters written, or -1 on truncation.

### 7b. Callback types and NAR_t fields (`src/NAR.h`)

Four callback typedefs with flat C primitives (ctypes-friendly — no struct params):

```c
typedef void (*NAR_EventHandler)(void *userdata, int reason, const char *narsese,
    char type, double freq, double conf, double priority, long occTime, double dt);
typedef void (*NAR_AnswerHandler)(void *userdata, const char *narsese,
    double freq, double conf, long occTime, long createTime);
typedef void (*NAR_DecisionHandler)(void *userdata, double expectation,
    const char *imp, double imp_freq, double imp_conf, double imp_dt,
    const char *prec, double prec_freq, double prec_conf, long prec_occTime);
typedef void (*NAR_ExecutionHandler)(void *userdata, const char *op, const char *args);
```

Reason codes: `NAR_EVENT_INPUT` (1), `NAR_EVENT_DERIVED` (2), `NAR_EVENT_REVISED` (3).

Eight new fields on `NAR_t` (handler + userdata pairs), zero-initialized by `calloc` in
`NAR_New` — callbacks are inactive by default.

### 7c. Registration functions (`src/NAR.c`)

Four trivial setters (`NAR_SetEventHandler`, `NAR_SetAnswerHandler`,
`NAR_SetDecisionHandler`, `NAR_SetExecutionHandler`) plus `NAR_AddOperationName` — a
convenience function that registers an operation with a no-op action, for library consumers
who handle execution via the execution callback.

### 7d. Callback invocations

Callbacks fire at four sites, **before** the existing printf output, regardless of
`PRINT_INPUT`/`PRINT_DERIVATIONS` flags:

| Site | File | Callback |
|------|------|----------|
| `Memory_printAddedKnowledge` | `src/Memory.c` | `event_handler` |
| `NAR_PrintAnswer` | `src/NAR.c` | `answer_handler` |
| `Decision_Suggest` | `src/Decision.c` | `decision_handler` |
| `Decision_Execute` | `src/Decision.c` | `execution_handler` |

Each site allocates 1–2 stack buffers of `NARSESE_SPRINT_BUFSIZE` (~2KB each).

### 7e. Shared library build (`Makefile`)

- Added `-fPIC` to base `CFLAGS`
- OS detection: `uname -s` → Darwin = `.dylib` with `-dynamiclib`, else `.so` with `-shared`
- New target: `$(BINDIR)/libdriftnars.$(SHLIB_EXT)` linking existing `OBJS_LIB`
- `all:` target now builds binary + static lib + shared lib

### 7f. Python ctypes wrapper (`examples/python/driftnars.py`)

`DriftNARS` class (~170 lines):
- Loads shared lib, declares argtypes/restype for all public functions
- Context manager support (`with DriftNARS() as nar:`)
- `add_narsese(sentence)`, `cycles(n)`, `add_operation(name)`
- `on_event(cb)`, `on_answer(cb)`, `on_decision(cb)`, `on_execution(cb)`
- Stores `CFUNCTYPE` references to prevent GC

### 7g. Python example (`examples/python/example.py`)

Demonstrates inheritance reasoning + operation handling with all four callback types.

**Verification:** `make clean && make` — zero warnings, builds binary + `.a` + `.dylib`.
`make test` — all 27 tests pass. `python3 examples/python/example.py` — all four callback
types fire correctly.
