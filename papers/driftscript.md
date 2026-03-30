# DriftScript: A Domain-Specific Language for Programming Non-Axiomatic Reasoning Agents

**Seamus Brady**
Independent Researcher, Dublin, Ireland
seamus@corvideon.ie | https://seamusbrady.ie

## Abstract

Non-Axiomatic Reasoning Systems (NARS) provide a framework for building
adaptive agents that operate under insufficient knowledge and resources.
However, the standard input language, Narsese, poses a usability barrier: its
dense symbolic notation, overloaded punctuation, and implicit conventions make
programs difficult to read, write, and maintain. We present DriftScript, a
Lisp-like domain-specific language that compiles to Narsese. DriftScript
provides source-level constructs covering the major sentence and term forms
used in Non-Axiomatic Logic (NAL) levels 1 through 8---including inheritance,
temporal implication, variable quantification, sequential conjunction, and
operation invocation---while replacing symbolic syntax with readable
keyword-based S-expressions. The compiler is a zero-dependency, four-stage
pipeline in 1,941 lines of C99. When used with the DriftNARS engine,
DriftScript programs connect to external systems through four structured
callback types and an HTTP operation registry, enabling a sense-reason-act
loop for autonomous agents. We describe the language design and formal grammar,
detail the compiler architecture, and evaluate the compiler through a 106-case
test suite, equivalence testing against hand-written Narsese, a NAL coverage
analysis, structural readability metrics, and compilation benchmarks. The
source code is available at https://github.com/seamus-brady/DriftNARS.

This paper focuses on the design and implementation of the DriftScript
language and its embedding into DriftNARS, rather than on new inference
algorithms for NARS itself.

## 1. Introduction

Non-Axiomatic Reasoning Systems (NARS) [1, 2] implement a logic designed for
intelligent systems that must operate with insufficient knowledge and
resources. Unlike classical logic, which assumes a closed and consistent
knowledge base, Non-Axiomatic Logic (NAL) treats every belief as uncertain,
every inference as defeasible, and every conclusion as subject to revision in
light of new evidence. These properties make NARS attractive for building
autonomous agents that must adapt to unforeseen environments.

The standard input language for NARS is Narsese, a formal notation in which
statements combine terms through copulas (relationship operators) and receive
truth values expressing the system's degree of belief. Narsese is expressive
enough to encode inheritance hierarchies, temporal cause-effect rules,
multi-step plans, and goal-directed behaviour. However, its syntax is
difficult for newcomers and cumbersome for nontrivial programs:

```
<(light_on &/ <(*, {SELF}, switch) --> ^press>) =/> light_off>.
```

This single Narsese sentence encodes "if the light is on and the agent presses
the switch, the light turns off." The overloaded angle brackets, symbolic
copulas (`=/>`), set notation (`{SELF}`), product terms (`*`), operation
prefixes (`^`), and tense markers create a high entry cost for new users.
Difficulty reading and writing Narsese discourages experimentation, slows
debugging, and limits NARS adoption outside the community of specialists
who have internalised its conventions.

Prior NARS implementations---notably OpenNARS [3] and OpenNARS for
Applications (ONA) [4]---have focused on the reasoning engine and its
algorithmic properties, leaving the input language unchanged from its original
formal specification. We are not aware of prior published work on a compiled
DSL covering this range of Narsese constructs.

We present **DriftScript**, a domain-specific language (DSL) that compiles to
Narsese. DriftScript makes the following contributions:

1. **A readable surface syntax for a broad subset of NAL 1--8.** DriftScript
   replaces Narsese's symbolic notation with Lisp-like S-expressions using
   human-readable keywords. The same temporal rule above becomes:

   ```lisp
   (believe (predict (seq "light_on"
                          (call ^press (ext-set "SELF") "switch"))
                     "light_off"))
   ```

2. **A lightweight, zero-dependency compiler.** The DriftScript compiler is a
   pure text transformer implemented as a four-stage pipeline (tokenise, parse,
   compile, emit) in 1,941 lines of C99. It produces Narsese output and
   engine control directives with static validation and source-location
   diagnostics.

3. **Practical embedding in a callback-driven agent runtime.** When used with
   the DriftNARS engine [12], compiled DriftScript programs connect to
   external systems through C, Python, and HTTP interfaces, enabling agents
   that learn from observation, make decisions, and act on their environment.

The remainder of this paper is organised as follows. Section 2 provides
background on NARS, NAL, and Narsese. Section 3 describes the DriftScript
language design, including a formal grammar. Section 4 details the compiler
architecture. Section 5 presents the integration mechanisms that connect
DriftScript to external systems. Section 6 evaluates the compiler through
correctness testing, coverage analysis, readability metrics, and performance
benchmarks. Section 7 demonstrates agent programming through worked examples.
Section 8 discusses related work. Section 9 concludes with limitations and
future directions.

## 2. Background

### 2.1 Non-Axiomatic Reasoning

Non-Axiomatic Logic (NAL) [1] is a term logic designed for systems that reason
under the Assumption of Insufficient Knowledge and Resources (AIKR). In
contrast to classical logics that assume completeness and consistency, NAL
treats every piece of knowledge as tentative and every inference as revisable.

NAL is organised into nine levels of increasing capability [2]:

- **NAL-1--2**: Inheritance and similarity reasoning with truth-value
  functions.
- **NAL-3--4**: Set operations, intersections, and products for structured
  knowledge.
- **NAL-5--6**: Implication, equivalence, variable quantification, and
  higher-order statements.
- **NAL-7--8**: Temporal reasoning, sequential conjunction, procedural
  knowledge, and goal-directed decision making.
- **NAL-9**: Self-monitoring and introspection (not addressed in this paper).

### 2.2 Narsese

Narsese is the formal input language of NARS. Every Narsese input is a
**sentence**: a term combined with a punctuation mark and an optional truth
value.

**Terms** are either atomic (e.g., `bird`, `light_on`) or compound, built from
other terms using copulas and connectors:

| Copula | Notation | Meaning |
|--------|----------|---------|
| Inheritance | `-->` | "is a kind of" |
| Similarity | `<->` | "resembles" |
| Implication | `==>` | "if ... then" (eternal) |
| Temporal implication | `=/>` | "if ... then" (temporal) |
| Equivalence | `<=>` | "if and only if" |

**Connectors** build compound terms:

| Connector | Notation | Meaning |
|-----------|----------|---------|
| Sequential conjunction | `&/` | Events in temporal order |
| Conjunction | `&&` | Both hold |
| Disjunction | `\|\|` | At least one holds |
| Negation | `--` | Negation |
| Product | `*` | Ordered tuple |
| Extensional set | `{...}` | Specific individuals |
| Intensional set | `[...]` | Properties |

**Punctuation** determines the sentence type: `.` for beliefs ("I know this"),
`?` for questions ("what do you know?"), and `!` for goals ("I want this").

**Truth values** are pairs `{f, c}` where frequency *f* indicates how often
the statement holds and confidence *c* indicates the amount of evidence. Both
lie in [0, 1]. The default truth value is {1.0, 0.9}. An **expectation**
value, computed as *e = c(f - 0.5) + 0.5*, is used in DriftNARS (following
ONA) for decision making: the engine executes an operation only when the
expectation of the relevant implication exceeds a configurable threshold.

**Tense markers** stamp beliefs and goals with temporal information: `:|:` for
present, `:\:` for past, and `:/:` for future (questions only).

### 2.3 OpenNARS for Applications

OpenNARS for Applications (ONA) [4] is a C-based NARS implementation that
emphasises real-time performance and procedural reasoning (NAL-7/8). ONA
introduced efficient event processing, sensorimotor integration, and
operation-based decision making to the NARS ecosystem. DriftNARS [12] is a
fork of ONA, restructured as an embeddable library with instance-based state
management and a public C API. DriftScript was developed as part of DriftNARS
but produces standard Narsese output and could in principle target any
NARS implementation that accepts compatible Narsese input.

## 3. The DriftScript Language

### 3.1 Design Goals

DriftScript was designed with four goals:

1. **Readability**: Replace symbolic notation with English keywords.
2. **Broad coverage**: Provide constructs for the major Narsese sentence and
   term forms used in NAL 1--8.
3. **Static validation**: Catch structural errors at compile time (arity
   violations, quoting errors, invalid truth values) rather than at the
   reasoning engine.
4. **Composability**: Produce standard Narsese output so DriftScript can be
   used with any compatible NARS implementation or mixed with raw Narsese
   input.

The choice of S-expression syntax was deliberate: prefix notation allows
trivial parsing with recursive descent, uniform arity checking on every form,
and no ambiguity with Narsese's own punctuation characters. It also provides
a natural path to future macro support.

### 3.2 Formal Grammar

The following grammar defines DriftScript in EBNF notation:

```ebnf
program     = { form } ;
form        = sentence | meta ;
sentence    = "(" sent_kw term { option } ")" ;
sent_kw     = "believe" | "ask" | "goal" ;
option      = tense | truth | dt ;
tense       = ":now" | ":past" | ":future" ;
truth       = ":truth" float float ;
dt          = ":dt" integer ;

term        = atom | copula_form | conn_form | call_form ;
atom        = string | variable | operation ;
string      = '"' { char } '"' ;
variable    = ( "$" | "#" | "?" ) ident ;
operation   = "^" ident ;
ident       = letter { letter | digit | "_" } | digit { digit } ;

copula_form = "(" copula term term ")" ;
copula      = "inherit" | "similar" | "imply" | "predict"
            | "equiv" | "instance" ;

conn_form   = "(" connector term { term } ")" ;
connector   = "seq" | "and" | "or" | "not" | "product"
            | "ext-set" | "int-set"
            | "ext-inter" | "int-inter"
            | "ext-diff" | "int-diff"
            | "ext-image1" | "ext-image2"
            | "int-image1" | "int-image2" ;

call_form   = "(" "call" operation { term } ")" ;

meta        = "(" "cycles" integer ")"
            | "(" "def-op" operation ")"
            | "(" "reset" ")"
            | "(" "config" config_key value ")"
            | "(" "concurrent" ")" ;
config_key  = "volume" | "motorbabbling" | "decisionthreshold"
            | "anticipationconfidence" | "questionpriming"
            | "babblingops" | "similaritydistance" ;

comment     = ";" { any char except newline } newline ;
```

Arity constraints not expressible in the grammar are enforced by the
compiler: all copulas require exactly 2 arguments, `not` requires exactly 1,
`seq` requires 2 or 3, and all binary connectors (`and`, `or`, `ext-inter`,
etc.) require exactly 2.

### 3.3 Sentence Forms

Every DriftScript program is a sequence of top-level **forms**. The three
sentence forms correspond directly to Narsese sentence types:

```lisp
(believe <term>)                  ; belief  -> "."
(believe <term> :now)             ; present-tense belief -> ". :|:"
(believe <term> :truth 0.8 0.9)  ; explicit truth value -> ". {0.8 0.9}"
(ask <term>)                      ; question -> "?"
(goal <term>)                     ; goal (always present-tense) -> "! :|:"
```

The `:now`, `:past`, `:future`, `:truth`, and `:dt` options modify the
sentence. Options can be combined: `(believe "x" :now :truth 1.0 0.9)`
produces `x. :|: {1.0 0.9}`. Goals are always emitted with present tense
(`:|:`); questions may use any tense. Duplicate or conflicting tense options
produce the last-seen value.

### 3.4 Terms and Copulas

Copulas are written as binary prefix operators:

| DriftScript | Narsese | Meaning |
|-------------|---------|---------|
| `(inherit A B)` | `<A --> B>` | A is a kind of B |
| `(similar A B)` | `<A <-> B>` | A resembles B |
| `(imply A B)` | `<A ==> B>` | If A then B (eternal) |
| `(predict A B)` | `<A =/> B>` | If A then B (temporal) |
| `(equiv A B)` | `<A <=> B>` | A if and only if B |
| `(instance A B)` | `<A \|-> B>` | A is an instance of B |

All copulas require exactly two arguments. The `instance` copula is
syntactic sugar for the Narsese instance relation (`|->`).

### 3.5 Connectors

Connectors build compound terms:

| DriftScript | Narsese | Arity | Notes |
|-------------|---------|-------|-------|
| `(seq A B)` | `(A &/ B)` | 2--3 | Sequential conjunction |
| `(and A B)` | `(A && B)` | 2 | Conjunction |
| `(or A B)` | `(A \|\| B)` | 2 | Disjunction |
| `(not A)` | `(-- A)` | 1 | Negation |
| `(product A B C)` | `(*, A, B, C)` | 1+ | Product |
| `(ext-set A B)` | `{A, B}` | 1+ | Extensional set |
| `(int-set A B)` | `[A, B]` | 1+ | Intensional set |
| `(ext-inter A B)` | `(&, A, B)` | 2 | Ext. intersection |
| `(int-inter A B)` | `(\|, A, B)` | 2 | Int. intersection |
| `(ext-diff A B)` | `(-, A, B)` | 2 | Ext. difference |
| `(int-diff A B)` | `(~, A, B)` | 2 | Int. difference |
| `(ext-image1 R X)` | `(/1, R, X)` | 2 | Ext. image (place 1) |
| `(ext-image2 R X)` | `(/2, R, X)` | 2 | Ext. image (place 2) |
| `(int-image1 R X)` | `(\1, R, X)` | 2 | Int. image (place 1) |
| `(int-image2 R X)` | `(\2, R, X)` | 2 | Int. image (place 2) |

The `seq` connector is restricted to 2 or 3 arguments, matching the engine's
`MAX_SEQUENCE_LEN` parameter. All arity constraints are enforced at compile
time.

### 3.6 The `call` Shorthand

Operations---the system's means of acting on the world---are invoked through
the `call` form, which is syntactic sugar for the standard Narsese encoding:

```lisp
(call ^press)                           ; bare: ^press
(call ^goto (ext-set "SELF") "park")    ; with args: <(*, {SELF}, park) --> ^goto>
```

Without arguments, `call` emits the bare operation name. With arguments, it
constructs a product term wrapped in an inheritance statement.

### 3.7 Variables

DriftScript supports three variable types, roughly corresponding to the
variable quantifiers in NAL-6:

| Prefix | Type | Narsese | NAL role |
|--------|------|---------|----------|
| `$` | Independent | `$1`, `$2` | Universal-like ("for all") |
| `#` | Dependent | `#1`, `#2` | Existential-like ("there exists") |
| `?` | Query | `?1`, `?2` | "What fills this slot?" |

The precise semantics of NARS variables differ from classical quantifiers;
see Wang [2, Ch. 7] for the formal treatment. DriftScript's role is limited
to providing named variables that compile to Narsese's numbered notation.

Variables may use descriptive names (`$animal`, `?what`) which are
automatically mapped to numbered Narsese variables. The mapping resets between
top-level forms, giving each sentence its own variable scope:

```lisp
(believe (imply (inherit $x "bird") (inherit $x "animal")))
;; => <<$1 --> bird> ==> <$1 --> animal>>.

(believe (imply (inherit $y "fish") (inherit $y "swimmer")))
;; => <<$1 --> fish> ==> <$1 --> swimmer>>.
```

The compiler pre-scans each form for explicitly numbered variables (e.g.,
`$1`) and assigns named variables to the next available slot, preventing
collisions:

```lisp
(believe (imply (inherit $1 "bird") (inherit $x "animal")))
;; $1 is reserved; $x maps to $2
;; => <<$1 --> bird> ==> <$2 --> animal>>.
```

### 3.8 Quoting Rules

DriftScript enforces a strict distinction between language keywords and user
data. All concept names (atoms that represent domain objects) must be
double-quoted. Keywords, operations, and variables must *not* be quoted:

```lisp
;; Correct:
(believe (inherit "robin" "bird"))

;; Compile-time errors:
(believe (inherit robin bird))       ; atoms must be quoted
(believe ("inherit" "A" "B"))        ; keywords must not be quoted
(believe (inherit "$x" "bird"))      ; variables must not be quoted
```

Strings support two escape sequences: `\"` for a literal double quote and
`\\` for a literal backslash. No other escapes are supported; strings cannot
span multiple lines.

### 3.9 Meta Commands

DriftScript includes meta commands for controlling the reasoning engine.
These compile to engine control directives rather than Narsese:

```lisp
(cycles 10)                  ; run 10 inference cycles
(def-op ^press)              ; register an operation
(reset)                      ; clear all memory
(config volume 0)            ; set configuration parameter
(config decisionthreshold 0.6)
(concurrent)                 ; mark next input as simultaneous
```

The `config` command accepts a fixed set of keys (listed in the grammar in
Section 3.2) and validates values as numbers.

## 4. Compiler Architecture

### 4.1 Overview

The DriftScript compiler is a pure text transformer: it accepts DriftScript
source and emits Narsese sentences and engine control directives. It has no
dependency on the DriftNARS engine and can be used as a standalone command-line
tool, linked as a library (with `DS_LIBRARY` defined), or invoked as a
subprocess.

The compiler is behaviour-preserving with respect to emitted Narsese and engine execution:
it emits standard Narsese forms accepted by DriftNARS/ONA-compatible engines
and does not modify inference behaviour. This is validated empirically through
equivalence testing (Section 6) rather than proven formally.

The compiler is implemented in 1,941 lines of C99 with zero external
dependencies. It follows a four-stage pipeline where each stage produces a
separate representation consumed by the next:

```
Source -> [Tokeniser] -> Tokens -> [Parser] -> AST -> [Compiler] -> [Emitter] -> Output
```

### 4.2 Tokeniser

The tokeniser converts raw source into a flat array of up to 1,024 tokens.
Five token types are recognised:

- `TOK_LPAREN` / `TOK_RPAREN`: Parentheses.
- `TOK_KEYWORD`: Colon-prefixed options (`:now`, `:truth`, `:dt`).
- `TOK_STRING`: Double-quoted literals with escape support.
- `TOK_SYMBOL`: Everything else (keywords, operations, variables, numbers).

Line and column information is tracked for every token, enabling precise error
reporting throughout the pipeline. Comments (`;` to end of line) are stripped
during tokenisation, following Lisp convention.

### 4.3 Parser

The parser is a recursive-descent parser that builds an abstract syntax tree
(AST) from the token stream. Two node types exist:

- `NODE_ATOM`: A leaf node carrying a value string and a `quoted` flag
  indicating whether it originated from a string literal.
- `NODE_LIST`: An interior node with up to 16 children.

Nodes are allocated from a fixed pool of 2,048 entries, avoiding dynamic
memory allocation. These static bounds (1,024 tokens, 2,048 nodes, 256
results) are generous for the intended use case of incremental agent scripting
but would not accommodate very large batch inputs; large-scale batch
compilation would require dynamic allocation.

### 4.4 Compiler

The compiler traverses the AST and produces structured results. Each top-level
form yields a `DS_CompileResult` tagged with a **result kind**:

```c
typedef enum {
    DS_RES_NARSESE,       // Narsese sentence
    DS_RES_SHELL_COMMAND,  // Engine directive (e.g., *reset)
    DS_RES_CYCLES,         // Cycle count
    DS_RES_DEF_OP          // Operation registration
} DS_ResultKind;
```

The result kind allows the host environment (REPL, HTTP server, or Python
bindings) to route each result to the appropriate handler without parsing the
output string.

Compilation proceeds by dispatching on the head symbol of each form:

1. **Sentence forms** (`believe`, `ask`, `goal`) are compiled by recursively
   compiling the term argument, then assembling the Narsese sentence with
   appropriate punctuation, tense suffix, truth value, and temporal offset.

2. **Terms** dispatch to specialised handlers based on the head symbol:
   copulas, connectors, or the `call` shorthand. Each handler validates arity
   and emits the corresponding Narsese notation.

3. **Meta commands** produce engine control directives or cycle counts.

**Variable renaming** occurs within each top-level form. Before compiling a
sentence, the compiler pre-scans the AST for explicitly numbered variables
(e.g., `$1`) and marks those numbers as reserved. Named variables are then
assigned to the lowest available number, ensuring deterministic output without
collisions. Up to 9 variables of each type are supported per sentence.

**Validation** is performed throughout:

- Copulas must have exactly 2 arguments.
- Sequential conjunction (`seq`) must have 2 or 3 arguments.
- Negation (`not`) must have exactly 1 argument.
- Truth value frequency and confidence must be floats in [0.0, 1.0].
- Temporal offset (`:dt`) must be an integer.
- Configuration keys are checked against a whitelist.
- Quoting rules are enforced on every atom.
- Goals cannot use `:past` or `:future` tense.
- Questions cannot have `:truth` values.

All errors include source location (line, column) for diagnostic precision.

### 4.5 Emitter

In standalone mode, the emitter writes each result to standard output. In
library mode, results are returned to the caller through the public function:

```c
int DS_CompileSource(const char *source,
                     DS_CompileResult *results,
                     int max_results);
```

This returns the number of results on success or -1 on error, with the error
message available via `DS_GetError()`.

## 5. Integration Architecture

DriftScript is a standalone compiler. The integration mechanisms described in
this section are part of the DriftNARS engine [12], not part of DriftScript
itself. They are relevant because they enable DriftScript programs to
participate in agent loops that connect reasoning to action.

### 5.1 Callback Types

DriftNARS provides four structured callback types, each delivering flat C
primitives for FFI compatibility:

**Event Handler.** Fires for every input, derived, or revised event:

```c
typedef void (*NAR_EventHandler)(void *userdata, int reason,
    const char *narsese, char type, double freq, double conf,
    double priority, long occTime, double dt);
```

The `reason` parameter distinguishes input events (1), derived inferences (2),
and belief revisions (3).

**Answer Handler.** Fires when a question receives an answer:

```c
typedef void (*NAR_AnswerHandler)(void *userdata, const char *narsese,
    double freq, double conf, long occTime, long createTime);
```

**Decision Handler.** Fires when the engine makes a decision above the
configured threshold, exposing the implication, precondition, and computed
expectation:

```c
typedef void (*NAR_DecisionHandler)(void *userdata, double expectation,
    const char *imp, double imp_freq, double imp_conf, double imp_dt,
    const char *prec, double prec_freq, double prec_conf,
    long prec_occTime);
```

**Execution Handler.** Fires when an operation is about to execute:

```c
typedef void (*NAR_ExecutionHandler)(void *userdata,
    const char *op, const char *args);
```

### 5.2 HTTP Operation Registry

The DriftNARS HTTP server wraps the engine in a REST API and adds an
**operation registry** that maps operation names to external callback URLs.
When the execution handler fires, the server POSTs a JSON payload to the
registered URL:

```json
{
  "op": "^press",
  "args": "({SELF} * switch)",
  "frequency": 1.0,
  "confidence": 1.0,
  "timestamp_ms": 1711584000000
}
```

Operations are registered at runtime via the REST API:

```
POST /ops/register
{"op": "^press", "callback_url": "http://localhost:3000/execute"}
```

This architecture allows external systems---web services, robots, IoT
devices---to participate in the agent's action loop without coupling to
the engine internals.

### 5.3 Python Bindings

The Python wrapper (`driftnars.py`, 246 lines) provides a high-level
interface using ctypes FFI:

```python
from driftnars import DriftNARS

with DriftNARS() as nar:
    nar.on_answer(lambda n, f, c, occ, ct:
        print(f"Answer: {n} truth=({f:.2f}, {c:.2f})"))

    nar.on_execution(lambda op, args:
        print(f"Execute: {op} {args}"))

    nar.add_driftscript("""
        (def-op ^press)
        (believe (predict (seq "light_on" (call ^press)) "light_off"))
        (believe "light_on" :now)
        (goal "light_off")
        (cycles 10)
    """)
```

The `add_driftscript()` method compiles DriftScript source and dispatches
each result by kind: Narsese sentences are fed to the engine, cycle counts
trigger inference, engine directives configure parameters, and operation
definitions register with the engine.

### 5.4 The Sense-Reason-Act Loop

The DriftScript compiler and DriftNARS integration mechanisms compose into an
agent loop:

1. **Sense**: Environmental observations enter as present-tense beliefs
   (`(believe "sensor_on" :now)`).
2. **Reason**: The engine runs inference cycles, deriving temporal
   implications, revising beliefs, and evaluating goals against learned rules.
3. **Decide**: When the expectation of an implication exceeds the decision
   threshold, the engine selects an operation.
4. **Act**: The execution callback delivers the operation to the external
   system (via C function pointer, Python callback, or HTTP POST).
5. **Feedback**: The external system reports the outcome as a new belief,
   closing the loop.

## 6. Evaluation

### 6.1 Compiler Correctness

The DriftScript compiler includes an inline test suite of **106 test cases**
covering 13 categories:

| Category | Tests | Coverage |
|----------|-------|----------|
| Tokeniser | 14 | Parens, keywords, strings, escapes, comments, line/col tracking |
| Parser | 6 | Atoms, nested lists, multiple forms, error detection |
| Copulas | 8 | All 6 copulas + arity error cases |
| Connectors | 17 | All 14 connectors + arity errors |
| Call shorthand | 4 | Bare ops, with args, error cases |
| Sentences | 12 | All forms, tenses, truth values, dt |
| Variables | 6 | Named, numbered, multi-var, collision avoidance |
| Meta commands | 7 | cycles, reset, def-op, config, concurrent, errors |
| Nested compounds | 5 | Deep nesting, mixed copulas/connectors/variables |
| Multi-statement | 2 | Multiple forms, variable scope isolation |
| Error detection | 4 | Bare atoms, unknown forms/terms |
| Quoting enforcement | 7 | All quoting violation categories |
| Truth/tense validation | 10 | Range checks, type checks, illegal combinations |

All 106 tests pass. Tests include unit tests for each compiler stage and
integration tests comparing emitted Narsese against expected canonical
outputs. The suite is invoked with `bin/driftscript --test`.

### 6.2 Equivalence Testing

To verify that DriftScript produces Narsese that yields identical engine
behaviour, we compiled DriftScript programs and equivalent hand-written
Narsese through the DriftNARS engine and compared outputs. Two representative
cases:

**Deduction chain:**

| | DriftScript input | Narsese input |
|---|---|---|
| Source | `(believe (inherit "robin" "bird"))` | `<robin --> bird>.` |
| | `(believe (inherit "bird" "animal"))` | `<bird --> animal>.` |
| | `(cycles 5)` | `5` |
| | `(ask (inherit "robin" "animal"))` | `<robin --> animal>?` |
| Answer | `frequency=1.000000, confidence=0.810000` | `frequency=1.000000, confidence=0.810000` |

**Temporal rule with goal-directed execution:**

| | DriftScript input | Narsese input |
|---|---|---|
| Rule | `(believe (predict (seq "light_on" (call ^press)) "light_off"))` | `<(light_on &/ ^press) =/> light_off>.` |
| State | `(believe "light_on" :now)` | `light_on. :|:` |
| Goal | `(goal "light_off")` | `light_off! :|:` |
| Decision | `expectation=0.791600` | `expectation=0.791600` |
| Execution | `^press executed` | `^press executed` |

In both cases, the engine output---including truth values, stamps, decision
expectations, and operation executions---is **byte-identical** between the
DriftScript and Narsese paths. We tested 12 representative programs spanning
deduction, temporal inference, variable use, and operation invocation; all
produced byte-identical outputs. The DriftScript compiler is transparent to
the reasoning engine.

### 6.3 NAL Coverage Analysis

The following table maps NAL levels to DriftScript constructs and notes
coverage boundaries:

| NAL Level | Construct | DriftScript form | Status |
|-----------|-----------|-----------------|--------|
| 1 | Inheritance | `(inherit A B)` | Covered |
| 2 | Similarity | `(similar A B)` | Covered |
| 2 | Instance | `(instance A B)` | Covered |
| 3 | Ext. intersection | `(ext-inter A B)` | Covered |
| 3 | Int. intersection | `(int-inter A B)` | Covered |
| 3 | Ext. difference | `(ext-diff A B)` | Covered |
| 3 | Int. difference | `(int-diff A B)` | Covered |
| 4 | Product | `(product A B ...)` | Covered |
| 4 | Ext. image | `(ext-image1 R X)`, `(ext-image2 R X)` | Covered |
| 4 | Int. image | `(int-image1 R X)`, `(int-image2 R X)` | Covered |
| 5 | Conjunction | `(and A B)` | Covered |
| 5 | Disjunction | `(or A B)` | Covered |
| 5 | Negation | `(not A)` | Covered |
| 5 | Implication | `(imply A B)` | Covered |
| 5 | Equivalence | `(equiv A B)` | Covered |
| 6 | Independent var | `$name` | Covered |
| 6 | Dependent var | `#name` | Covered |
| 6 | Query var | `?name` | Covered |
| 7 | Tense (present) | `:now` | Covered |
| 7 | Tense (past) | `:past` | Covered (questions only) |
| 7 | Tense (future) | `:future` | Covered (questions only) |
| 7 | Temporal implication | `(predict A B)` | Covered |
| 7 | Temporal offset | `:dt N` | Covered |
| 8 | Sequential conjunction | `(seq A B [C])` | Covered (max 3 elements) |
| 8 | Operations | `(call ^op ...)` | Covered |
| 8 | Goals | `(goal ...)` | Covered |

The covered constructs correspond to those commonly used in practical NAL
programs. Higher-order statements where implications or equivalences appear
as terms in other copulas can be represented by nesting (e.g., `(imply
(inherit $x "bird") (inherit $x "animal"))` compiles to the higher-order
`<<$1 --> bird> ==> <$1 --> animal>>`), but DriftScript does not provide
explicit syntax for all possible higher-order combinations. Less common or
highly nested forms can be expressed via raw Narsese, which can be freely
mixed with DriftScript input.

### 6.4 Structural Readability Metrics

We compiled 15 representative programs (ranging from simple beliefs to
complex temporal rules with operations and variables) in both DriftScript and
equivalent Narsese and measured structural properties:

| Metric | DriftScript | Narsese | Ratio |
|--------|-------------|---------|-------|
| Total characters | 693 | 412 | 1.68x |
| Symbol (non-alphanumeric) characters | 107 | 167 | 0.64x |
| Distinct symbol characters used | 8 | 20 | 0.40x |
| Alphabetic characters | 452 | 186 | 2.43x |
| Alphabetic / total ratio | 0.64 | 0.44 | -- |

DriftScript programs are longer in total characters (1.68x) due to keyword
names, but use **36% fewer symbolic characters** and **60% fewer distinct
symbol types**. The alphabetic-to-total ratio rises from 0.44 (Narsese) to
0.64 (DriftScript), indicating that a larger proportion of the program
consists of readable English text rather than punctuation.

The 8 distinct symbols in DriftScript (`$`, `(`, `)`, `-`, `:`, `?`, `^`,
`_`) are a strict subset of the 20 used in Narsese (which adds `!`, `&`, `*`,
`,`, `.`, `/`, `<`, `=`, `>`, `{`, `|`, `}`). This reduction in symbol
vocabulary is the primary mechanism by which DriftScript reduces cognitive
load.

These are structural metrics that quantify syntactic differences, not
cognitive load. We do not claim they directly measure readability or
comprehension, but they support the qualitative argument that DriftScript
replaces symbolic density with alphabetic keywords.

### 6.5 Compilation Performance

The DriftScript compiler processes 300 forms (a mix of beliefs, temporal
rules, and questions) in **3 milliseconds** on an Apple M-series processor
(single-threaded).
Single-form compilation (including process startup for the standalone binary)
averages 1.3 ms, dominated by process launch overhead.

In library mode (via `DS_CompileSource()`), compilation is effectively
instantaneous relative to the inference cycles that follow. The compiler
allocates no dynamic memory; all state lives in fixed-size arrays on the
stack or in static storage.

## 7. Worked Examples

### 7.1 Deductive Reasoning

The simplest use of DriftScript teaches the system facts and asks questions:

```lisp
;; Teach an inheritance chain
(believe (inherit "robin" "bird"))
(believe (inherit "bird" "animal"))

;; Run inference
(cycles 5)

;; Ask a question
(ask (inherit "robin" "animal"))
(cycles 5)
```

Engine output:

```
Answer: <robin --> animal>. creationTime=2 Stamp=[2,1]
  Truth: frequency=1.000000, confidence=0.810000
```

The system deduces `<robin --> animal>` by chaining the two inheritance
beliefs through the NAL-1 deduction rule. Confidence decreases from the
default 0.9 to 0.81 because the conclusion rests on an inference chain.

### 7.2 Temporal Learning and Goal-Directed Action

This example demonstrates implicit rule acquisition---the system learns a
temporal pattern from observation alone:

```lisp
(def-op ^grab)
(config volume 0)

;; Round 1: Observe a sequence
(believe "see_food" :now)
(cycles 5)
(believe (call ^grab) :now)
(cycles 5)
(believe "have_food" :now)
(cycles 5)

;; Round 2: Repeat (strengthens learned rule)
(believe "see_food" :now)
(cycles 5)
(believe (call ^grab) :now)
(cycles 5)
(believe "have_food" :now)
(cycles 5)

;; Apply: present precondition and goal
(believe "see_food" :now)
(goal "have_food")
(cycles 10)
```

No explicit rules were provided. The engine observed the temporal pattern
`see_food -> ^grab -> have_food` twice. On the second observation, the
engine forms the predictive implication `<(see_food &/ ^grab) =/> have_food>`
with sufficient confidence that, when the precondition is true and the goal
is active, the rule's expectation exceeds the decision threshold and
`^grab` is executed.

### 7.3 Multi-Step Planning with Callbacks

This example shows a two-step plan with execution callbacks in Python:

```python
from driftnars import DriftNARS

with DriftNARS() as nar:
    state = {"location": "home", "has_key": False}

    def handle_execution(op, args):
        if op == "^pickup":
            state["has_key"] = True
            nar.add_narsese("have_key. :|:")
        elif op == "^unlock" and state["has_key"]:
            state["location"] = "inside"
            nar.add_narsese("inside. :|:")

    nar.on_execution(handle_execution)

    nar.add_driftscript("""
        (def-op ^pickup)
        (def-op ^unlock)

        (believe (predict (seq "see_key" (call ^pickup)) "have_key"))
        (believe (predict (seq "have_key" (call ^unlock)) "inside"))

        (believe "see_key" :now)
        (goal "inside")
        (cycles 20)
    """)
```

The engine finds that `^unlock` requires `have_key` as a precondition. Since
`have_key` is not currently true, the engine looks for a way to achieve it,
finds that `^pickup` with precondition `see_key` (currently true) produces
`have_key`, and executes `^pickup`. The callback feeds `have_key` back as a
present-tense belief. On the next cycle, the engine re-evaluates and executes
`^unlock`. Whether multi-step decomposition succeeds depends on the engine's
cycle budget, concept priority, and decision threshold; it is not guaranteed
for arbitrary chain lengths.

### 7.4 HTTP-Based Agent

The HTTP server enables language-agnostic integration:

```bash
# Start the server
bin/driftnars-httpd --port 8080 &

# Register an operation with a callback URL
curl -X POST http://localhost:8080/ops/register \
  -d '{"op":"^water","callback_url":"http://localhost:3000/water"}'

# Teach the system via DriftScript
curl -X POST http://localhost:8080/driftscript \
  -d '(believe (predict (seq "soil_dry" (call ^water)) "soil_moist"))
      (config decisionthreshold 0.5)'

# Feed sensor data and set a goal
curl -X POST http://localhost:8080/narsese -d 'soil_dry. :|:'
curl -X POST http://localhost:8080/narsese -d 'soil_moist! :|:'
curl -X POST http://localhost:8080/narsese -d '10'
```

When the engine decides to execute `^water`, the HTTP server POSTs the
execution payload to the registered URL. The entire interaction uses standard
HTTP and JSON.

## 8. Related Work

### Domain-Specific Languages for Logic and Reasoning

Domain-specific languages for logic programming have a long history. Prolog
[5] established the paradigm of logic programming with Horn clauses. Datalog
[6] restricts Prolog to guarantee termination and enable efficient evaluation.
Answer Set Programming languages like the Potassco suite [7] provide
declarative problem specification for combinatorial search. These systems
operate under the Closed World Assumption and do not handle uncertainty or
temporal reasoning natively.

Probabilistic programming languages such as ProbLog [8] and Church [9]
extend logic programming with probabilistic inference but do not address
the NARS-specific concerns of resource-bounded reasoning, temporal sequencing,
and operation-based decision making.

DriftScript differs from these in targeting a specific formal language
(Narsese) as output rather than providing its own inference engine. It is
closer in spirit to syntax frontends or transpilers for existing formal
systems than to standalone logic languages.

### NARS Implementations

OpenNARS [3] is the reference Java implementation of NARS. It provides a GUI
and Narsese input but no high-level DSL. OpenNARS for Applications (ONA) [4]
reimplemented the core in C for real-time performance and introduced
sensorimotor capabilities. Neither provides an alternative surface syntax.
The NARS community has produced various tools for interacting with Narsese,
including web-based editors and visualisers, but we are not aware of a
published compiled DSL that covers this range of Narsese constructs.

### Agent Programming Languages

Agent-oriented programming languages such as AgentSpeak(L) [10] and Jason
[11] provide high-level constructs for belief-desire-intention (BDI) agents.
These share DriftScript's goal of making agent programming accessible but
operate on classical logic without NARS's uncertainty handling, truth-value
revision, or temporal learning from observation. DriftScript occupies a
similar niche for the NARS paradigm: a readable authoring surface for
agent programs, with the reasoning engine handling inference.

### Why S-Expressions?

The choice of Lisp-like syntax was driven by four practical considerations:
(1) trivial parsing with recursive descent, (2) uniform prefix notation that
allows arity checking on every form, (3) no ambiguity with Narsese's own
punctuation characters (`<`, `>`, `.`, `!`, `?`, `{`, `}`), and (4) a
natural path to future macro support. An infix or YAML/JSON-based syntax
would have required either a more complex parser or careful disambiguation
from Narsese notation.

## 9. Discussion and Future Work

### Limitations

DriftScript is a **syntactic transformation**: it does not perform type
checking, semantic analysis, or optimisation. It cannot detect, for example,
that a temporal implication uses eternal tense (a likely error) or that an
operation referenced in a rule has not been registered.

The compiler has **no formal semantics** of its own beyond compilation to
Narsese. Correctness is validated empirically (Section 6) rather than proven.

The **callback architecture** is part of DriftNARS, not DriftScript. Callback
behaviour is defined in the host language (C, Python, or via HTTP), creating a
boundary between declarative knowledge specification and imperative action
logic.

The evaluation in this paper is **structural and demonstrative** rather than
comparative. We do not have user study data measuring whether DriftScript
programs are faster to write or debug than equivalent Narsese.

### Future Directions

**Semantic validation.** The compiler could warn on common mistakes: eternal
temporal implications, unregistered operations, duplicate beliefs, and
unreachable goals.

**Inline callback definitions.** A future version could allow callback logic
to be specified within DriftScript itself, perhaps through a `(when-execute
^op ...)` form that generates the appropriate host-language binding.

**Interactive development tools.** A language server providing completion,
hover documentation, and real-time compilation feedback would further lower
the barrier to entry.

**User study.** A formal evaluation comparing DriftScript and Narsese for
common agent programming tasks---measuring time to completion, error rates,
and subjective readability---would quantify the improvements we currently
support only with structural metrics.

## 10. Conclusion

DriftScript provides a readable, Lisp-like surface syntax for a broad subset
of Narsese covering NAL levels 1 through 8. Its four-stage compiler,
implemented in 1,941 lines of zero-dependency C99, produces standard Narsese
output and integrates with the DriftNARS engine's callback architecture to
support agent development through C, Python, and HTTP interfaces.

Evaluation shows that the compiler passes 106 test cases, produces output
byte-identical to hand-written Narsese in equivalence tests, reduces symbolic
character density by 36% relative to raw Narsese, and compiles 300 forms in
3 milliseconds.

DriftScript does not change NARS's reasoning---it provides a more readable
authoring surface for the programs that drive it.

## References

[1] P. Wang, *Rigid Flexibility: The Logic of Intelligence*. Springer, 2006.

[2] P. Wang, *Non-Axiomatic Logic: A Model of Intelligent Reasoning*. World
Scientific, 2013.

[3] P. Wang, "Non-Axiomatic Reasoning System: Exploring the Essence of
Intelligence," Ph.D. dissertation, Indiana University, 1995.

[4] P. Hammer and T. Lofthouse, "OpenNARS for Applications: Architecture
and Control," in *Artificial General Intelligence*, Springer, 2020,
pp. 193--204.

[5] W. F. Clocksin and C. S. Mellish, *Programming in Prolog*, 5th ed.
Springer, 2003.

[6] S. Ceri, G. Gottlob, and L. Tanca, "What you always wanted to know about
Datalog (and never dared to ask)," *IEEE Trans. Knowl. Data Eng.*, vol. 1,
no. 1, pp. 146--166, 1989.

[7] M. Gebser, R. Kaminski, B. Kaufmann, and T. Schaub, "Answer Set Solving
in Practice," *Synthesis Lectures on Artificial Intelligence and Machine
Learning*, Morgan & Claypool, 2012.

[8] L. De Raedt, A. Kimmig, and H. Toivonen, "ProbLog: A probabilistic
Prolog and its application in link discovery," in *IJCAI*, 2007, pp.
2462--2467.

[9] N. D. Goodman, V. K. Mansinghka, D. M. Roy, K. Bonawitz, and J. B.
Tenenbaum, "Church: A language for generative models," in *UAI*, 2008, pp.
220--229.

[10] A. S. Rao, "AgentSpeak(L): BDI agents speak out in a logical computable
language," in *MAAMAW*, Springer, 1996, pp. 42--55.

[11] R. H. Bordini, J. F. Hubner, and M. Wooldridge, *Programming Multi-Agent
Systems in AgentSpeak using Jason*. Wiley, 2007.

[12] S. Brady, "DriftNARS: An embeddable Non-Axiomatic Reasoning System,"
2026. Available: https://github.com/seamus-brady/DriftNARS

## Appendix A: Translation Examples

| # | DriftScript | Narsese |
|---|-------------|---------|
| 1 | `(believe (inherit "robin" "bird"))` | `<robin --> bird>.` |
| 2 | `(believe (similar "cat" "dog"))` | `<cat <-> dog>.` |
| 3 | `(believe (imply "rain" "wet"))` | `<rain ==> wet>.` |
| 4 | `(believe (predict "A" "B"))` | `<A =/> B>.` |
| 5 | `(believe (equiv "A" "B"))` | `<A <=> B>.` |
| 6 | `(believe (instance "Tweety" "bird"))` | `<Tweety \|-> bird>.` |
| 7 | `(believe "light_on" :now)` | `light_on. :\|:` |
| 8 | `(goal "light_off")` | `light_off! :\|:` |
| 9 | `(ask (inherit "robin" "animal"))` | `<robin --> animal>?` |
| 10 | `(ask (inherit ?x "animal"))` | `<?1 --> animal>?` |
| 11 | `(believe (inherit "x" :truth 0.8 0.9))` | `x. {0.8 0.9}` |
| 12 | `(believe (predict (seq "a" "b") "c"))` | `<(a &/ b) =/> c>.` |
| 13 | `(believe (predict (seq "a" "b" "c") "d"))` | `<(a &/ b &/ c) =/> d>.` |
| 14 | `(believe (inherit (ext-set "SELF") "agent"))` | `<{SELF} --> agent>.` |
| 15 | `(believe (inherit "x" (int-set "bright")))` | `<x --> [bright]>.` |
| 16 | `(believe (inherit (product "A" "B") "rel"))` | `<(*, A, B) --> rel>.` |
| 17 | `(believe (imply (inherit $x "bird") (inherit $x "animal")))` | `<<$1 --> bird> ==> <$1 --> animal>>.` |
| 18 | `(believe (predict (seq "light_on" (call ^press)) "light_off"))` | `<(light_on &/ ^press) =/> light_off>.` |
| 19 | `(believe (predict (seq "at_home" (call ^goto (ext-set "SELF") "park")) "at_park"))` | `<(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>.` |
| 20 | `(believe (imply (and (inherit $x "bird") (inherit $x "flyer")) (inherit $x "animal")))` | `<<(<$1 --> bird> && <$1 --> flyer>) ==> <$1 --> animal>>>.` |
