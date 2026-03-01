# DriftScript Tutorial

A progressive, hands-on introduction to DriftScript and NARS reasoning.
Each file is self-contained, heavily commented, and runnable as-is.

## Prerequisites

Build DriftNARS from the repository root:

```bash
make
```

## Running the tutorials

Pipe any `.ds` file into the DriftScript REPL:

```bash
bin/driftnars driftscript < examples/driftscript/01_hello.ds
```

Or run interactively and type lines yourself:

```bash
bin/driftnars driftscript
```

## Files

| File | Topic | What you'll learn |
|------|-------|-------------------|
| `01_hello.ds` | First steps | Beliefs, questions, deduction |
| `02_truth.ds` | Truth values | Frequency, confidence, expectation |
| `03_copulas.ds` | Relationships | All 6 copulas: inherit, similar, imply, predict, equiv, instance |
| `04_connectors.ds` | Compound terms | Sets, products, and/or/not, sequences |
| `05_time.ds` | Temporal reasoning | Present tense, predictions, time decay |
| `06_operations.ds` | Goals and actions | def-op, goal-driven execution, call with arguments |
| `07_variables.ds` | Variables | $independent, #dependent, ?query variables |
| `08_learning.ds` | Learning from experience | Implicit rule acquisition from observation |
| `09_multistep.ds` | Multi-step planning | Chaining operations, goal decomposition |
| `10_config.ds` | Tuning the reasoner | Volume, thresholds, cycles, reset, concurrent |

## What next

- `docs/driftscript_reference.md` — complete language reference
- `docs/narsese_primer.md` — underlying Narsese logic
- `examples/python/example_driftscript.py` — Python integration with callbacks
