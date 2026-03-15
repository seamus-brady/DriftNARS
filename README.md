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
- [HTTP Server](#http-server)
- [State Persistence](#state-persistence)
- [Memory and Resource Management](#memory-and-resource-management)
- [Error Handling](#error-handling)
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
rule table (`src/engine/RuleTable.c`), then Stage 2 compiles the final binary into `bin/`.

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

## HTTP Server

DriftNARS includes a lightweight HTTP server for integrating the reasoning engine
with web applications, scripts, or any HTTP client:

```bash
make httpd
bin/driftnars-httpd --port 8080
```

### Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/driftscript` | Compile & execute DriftScript, returns engine output |
| `POST` | `/narsese` | Execute raw Narsese / shell commands (one per line) |
| `POST` | `/reset` | Reset the reasoner |
| `GET` | `/health` | Liveness check — returns `{"status":"ok"}` |
| `GET` | `/ops` | List registered operations and their callback URLs |
| `POST` | `/ops/register` | Register an operation with a callback URL |
| `DELETE` | `/ops/:name` | Unregister an operation |
| `POST` | `/config` | Set runtime reasoner parameters |
| `POST` | `/save` | Save entire state to binary file |
| `POST` | `/load` | Load state from binary file |
| `POST` | `/compact` | Free lowest-priority concepts to reduce memory |

### Reasoning

```bash
curl -X POST http://127.0.0.1:8080/driftscript -d '
(believe (inherit "robin" "bird"))
(believe (inherit "bird" "animal"))
(cycles 5)
(ask (inherit "robin" "animal"))
'
```

### Operation callbacks

Register operations at runtime. When the reasoner decides to execute an operation,
the server sends an HTTP POST to the registered callback URL with a JSON payload:

```bash
# Register ^press — when executed, POST to your service
curl -X POST http://127.0.0.1:8080/ops/register \
    -H 'Content-Type: application/json' \
    -d '{
      "op": "^press",
      "callback_url": "http://localhost:4000/nars/executions",
      "min_confidence": 0.6
    }'

# Teach a rule and trigger the operation
curl -X POST http://127.0.0.1:8080/driftscript -d '
(believe (predict (seq "light_on" (call ^press)) "light_off"))
(believe "light_on" :now)
(goal "light_off")
'
# => ^press fires, your service receives:
#    {"op":"^press","args":"","frequency":1.0,"confidence":1.0,"timestamp_ms":...}
```

```bash
# List registered operations
curl http://127.0.0.1:8080/ops

# Unregister
curl -X DELETE http://127.0.0.1:8080/ops/^press
```

### Runtime configuration

```bash
curl -X POST http://127.0.0.1:8080/config \
    -H 'Content-Type: application/json' \
    -d '{
      "decision_threshold": 0.65,
      "motorbabbling": 0.0,
      "volume": 0
    }'
```

Available config keys: `decision_threshold`, `motorbabbling`, `volume`,
`anticipation_confidence`, `question_priming`.

### State persistence

Save and restore the entire reasoner state via the HTTP API — see the
[State Persistence](#state-persistence) section below for full details including
CLI usage, the C API, and what gets serialized.

### Examples and tests

See [`examples/httpd/`](examples/httpd/) for ready-to-run scripts:

```bash
./examples/httpd/example.sh      # demo all endpoints
./examples/httpd/test_ops.sh     # test operation callback API + save/load (20 tests)
```

## State Persistence

DriftNARS can save its entire state — all learned concepts, beliefs, temporal
implications, event queues, configuration, and timing — to a binary `.dnar` file,
and reload it later. This lets you checkpoint a trained system, shut down, and
resume exactly where you left off.

### What gets saved

Everything the reasoner has learned and every tunable parameter:

- All concepts and their beliefs, goal spikes, predicted beliefs
- Temporal implication tables (precondition beliefs, implication links)
- Cycling belief and goal event queues with priorities
- Atom table (all term names the system has seen)
- Occurrence time index
- Operation registrations (names and babbling arguments, but not C function pointers)
- Runtime parameters (decision threshold, motor babbling, truth decay, etc.)
- Internal counters (current time, stamp base, RNG seed, concept IDs)

What is **not** saved: C function pointers (operation callbacks, output handlers).
These belong to the running process and are preserved across a load — you don't
need to re-register them.

### CLI usage

From either the Narsese shell or DriftScript REPL:

```
driftnars> <bird --> animal>.
driftnars> <robin --> bird>.
driftnars> 5
driftnars> *save /tmp/brain.dnar
State saved to /tmp/brain.dnar

driftnars> *reset
driftnars> *load /tmp/brain.dnar
State loaded from /tmp/brain.dnar

driftnars> <robin --> animal>?
Answer: <robin --> animal>. creationTime=2 Truth: frequency=1.000000, confidence=0.810000

driftnars> *compact 50
Compacted to 50 concepts (50 allocated)
```

This also works with piped input for scripted workflows:

```bash
echo '<bird --> animal>.
<robin --> bird>.
5
*save /tmp/brain.dnar' | bin/driftnars shell
```

### HTTP API

```bash
# Save current state
curl -X POST http://127.0.0.1:8080/save \
    -H 'Content-Type: application/json' \
    -d '{"path":"/tmp/brain.dnar"}'
# => {"status":"saved","path":"/tmp/brain.dnar"}

# Load state (replaces current state entirely)
curl -X POST http://127.0.0.1:8080/load \
    -H 'Content-Type: application/json' \
    -d '{"path":"/tmp/brain.dnar"}'
# => {"status":"loaded","path":"/tmp/brain.dnar"}

# Free lowest-priority concepts to reduce memory
curl -X POST http://127.0.0.1:8080/compact \
    -H 'Content-Type: application/json' \
    -d '{"target":100}'
# => {"status":"compacted","concepts":100,"allocated":100}
```

After loading, the system continues reasoning from the restored state. Any
registered HTTP operation callbacks survive the load automatically.

### C API

```c
// Save
int rc = NAR_Save(nar, "/tmp/brain.dnar");  // returns NAR_OK or NAR_ERR_IO

// Load (replaces all state in nar)
rc = NAR_Load(nar, "/tmp/brain.dnar");      // returns NAR_OK or NAR_ERR_IO

// Free lowest-priority concepts to reduce memory (returns remaining count)
int remaining = NAR_Compact(nar, 100);
```

Output callbacks (`NAR_SetAnswerHandler`, etc.) and operation action function
pointers are preserved across `NAR_Load` — they are not part of the file.

### Compatibility

The binary format includes a header with all compile-time configuration constants
(`CONCEPTS_MAX`, `ATOMS_MAX`, `STAMP_SIZE`, etc.). A `.dnar` file can only be loaded
by a binary compiled with the same configuration. Attempting to load a file from an
incompatible build returns `NAR_ERR_IO`.

## Memory and Resource Management

### Static allocation model

DriftNARS uses lazy, on-demand allocation for concepts. When you call `NAR_New()`,
it allocates a `NAR_t` struct (~6 MB) containing event queues, hash tables, and index
structures — but no concepts. Each `Concept` (~294 KB, due to its implication tables
with `TABLE_SIZE=120` entries) is allocated individually from the heap only when the
reasoner first needs it.

A fresh instance with 3 inputs uses ~8 MB total. At full capacity (`CONCEPTS_MAX=4096`
concepts), memory usage reaches ~1.2 GB — the same capacity as ONA, but you only pay
for what you use.

This design is deliberate:

- **Pay-as-you-go** — memory grows with actual usage, not maximum capacity
- **Same reasoning characteristics** — identical `TABLE_SIZE` and `CONCEPTS_MAX` as the
  original, preserving probability distributions and inference quality
- **Bounded** — the system never exceeds its configured maximum, and when concept
  capacity is reached, lowest-priority concepts are evicted and their storage recycled

### Resource limits

Key limits are compile-time constants in `src/engine/Config.h`:

| Parameter | Default | What it bounds |
|-----------|---------|----------------|
| `CONCEPTS_MAX` | 4096 | Maximum concepts in memory |
| `ATOMS_MAX` | 255 | Maximum distinct atom names |
| `OPERATIONS_MAX` | 10 | Maximum registered operations |
| `CYCLING_BELIEF_EVENTS_MAX` | 20 | Belief event cycling queue size |
| `CYCLING_GOAL_EVENTS_MAX` | 10 | Goal event cycling queue size |
| `STAMP_SIZE` | 10 | Evidential base entries per stamp |
| `TABLE_SIZE` | 120 | Implications per concept slot |

When a pool is full, the system handles it gracefully — it evicts the lowest-priority
item (for priority queues) or silently drops the new entry (for index structures).
This is NARS's "Attention and Resource Control" (AIKR) principle: finite resources
force the system to prioritize, which is a feature, not a limitation.

### Customizing limits

To change limits, edit `src/engine/Config.h` and rebuild. Increasing `CONCEPTS_MAX`
increases memory proportionally (~300 KB per concept with default `TABLE_SIZE`).
Reducing `TABLE_SIZE` or `OPERATIONS_MAX` significantly reduces per-concept size.
Note that `.dnar` save files are tied to the compile-time configuration — you cannot
load a file saved with different limits.

## Error Handling

All internal data structure operations fail gracefully rather than aborting:

- **Stack overflow/underflow** — `Stack_Push` returns `false` when full,
  `Stack_Pop` returns `NULL` when empty. Callers check and degrade safely
  (e.g., the inverted atom index silently skips indexing if the pool is exhausted).
- **Hash table full** — `HashTable_Set` silently drops the entry when the
  internal free list is empty. `HashTable_Delete` is a no-op if the item isn't found.
- **Narsese input too long** — returns `NULL` / empty term instead of aborting.
  The parser remains usable for subsequent normal-length input.
- **Atom table full** — returns atom index 0 (invalid) when `ATOMS_MAX` is exceeded.
- **Memory allocation failure** — `NAR_New()` returns `NULL` if `calloc` fails.
- **File I/O errors** — `NAR_Save`/`NAR_Load` return `NAR_ERR_IO` on failure.

Error codes returned by the public API:

| Code | Constant | Meaning |
|------|----------|---------|
| 0 | `NAR_OK` | Success |
| -1 | `NAR_ERR_PARSE` | Narsese parse or input format error |
| -2 | `NAR_ERR_MEM` | Memory full — input dropped (non-fatal) |
| -3 | `NAR_ERR_INIT` | Called before `NAR_INIT` |
| -4 | `NAR_ERR_IO` | File I/O error (save/load) |

The system is designed to keep running when it hits limits. Concepts get evicted
by priority, events cycle through bounded queues, and evidence accumulates within
fixed-size stamps. These aren't error conditions — they're the normal operating
mode of a resource-bounded reasoning system.

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
