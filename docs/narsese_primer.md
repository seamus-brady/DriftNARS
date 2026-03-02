# Narsese Primer for DriftNARS

A practical guide to Narsese for developers embedding DriftNARS as a library.

---

## 1. What Narsese Is

Narsese is the input language of NARS (Non-Axiomatic Reasoning System). It lets you
tell the system what you believe, what you want, and what you want to know. The system
reasons over these inputs using probabilistic truth values, temporal sequencing, and
adaptive decision making.

Everything in Narsese is one of three things:

| Punctuation | Name | Meaning | Example |
|-------------|------|---------|---------|
| `.` | Belief | "I know this" | `<bird --> animal>.` |
| `!` | Goal | "I want this" | `<food --> got>! :\|:` |
| `?` | Question | "What do you know about this?" | `<robin --> animal>?` |

---

## 2. Terms

A **term** is the subject of a statement. Terms can be atomic (single words) or compound
(built from other terms with connectors).

### 2a. Atomic terms

Any alphanumeric string:

```
cat
robin
room7
sensor_left
```

Atoms are case-sensitive. `Cat` and `cat` are different terms. Atom names can contain
letters, digits, and underscores, and must be shorter than 32 characters
(`ATOMIC_TERM_LEN_MAX`). The system can hold up to 255 distinct atoms (`ATOMS_MAX`).

### 2b. The SELF atom

`SELF` is a reserved atom that represents the reasoning agent itself. It appears in
operation arguments (see Section 8) and is always available after initialization.

---

## 3. Copulas (Relationships)

A **copula** connects two terms into a statement. Statements are written in angle
brackets with the copula between the two terms:

```
<subject COPULA predicate>
```

### Inheritance `-->`

"Subject is a kind of predicate." The most common relationship.

```narsese
<robin --> bird>.          // robin is a bird
<bird --> animal>.         // bird is a kind of animal
```

From these two beliefs, the system deduces `<robin --> animal>` automatically.

### Similarity `<->`

"Subject and predicate are interchangeable." Symmetric — unlike inheritance, the
direction does not matter.

```narsese
<cat <-> feline>.          // cat and feline are equivalent
```

### Implication `==>`

"If subject is true, then predicate is true." Declarative (non-temporal) implication.

```narsese
<rain ==> wet>.            // if rain, then wet
```

### Temporal implication `=/>`

"Subject leads to predicate over time." This is the core of procedural reasoning —
it encodes cause-and-effect relationships that involve time and possibly operations.

```narsese
<(light_on &/ ^press_button) =/> light_off>.
```

This says: "if the light is on and you press the button, the light turns off."

### Equivalence `<=>`

"Subject and predicate imply each other." Bidirectional implication.

```narsese
<raining <=> clouds>.
```

### Instance property `|->`

"Subject has the continuous property predicate." Used for numeric/sensor values.

```narsese
<temperature |-> hot>.
```

---

## 4. Compound Terms

Compound terms combine multiple terms using connectors.

### Product `(*, a, b, ...)`

An ordered tuple. Products are used for multi-argument structures, especially
operation arguments.

```narsese
<(*, john, mary) --> friends>.    // john-and-mary are friends
<(*, {SELF}, door) --> ^open>.    // open(SELF, door)
```

### Extensional set `{a, b, c}`

A set of instances. "Exactly these things."

```narsese
<{tom, jerry} --> cat>.           // tom and jerry are cats
<{SELF} --> agent>.               // SELF is an agent
```

### Intensional set `[a, b, c]`

A set of properties. "Having these attributes."

```narsese
<bird --> [flying, feathered]>.   // birds are flying and feathered
```

### Sequential conjunction `&/`

"A then B" — temporal sequence. Events that happen in order.

```narsese
<(a &/ b) =/> c>.                 // a, then b, leads to c
<(see_food &/ ^grab) =/> have_food>.
```

Maximum sequence length is 3 terms (`MAX_SEQUENCE_LEN`).

### Conjunction `&&`

"A and B" — logical conjunction (no temporal ordering).

```narsese
<(a && b) ==> c>.                 // a and b together imply c
```

### Disjunction `||`

"A or B" — logical disjunction.

```narsese
<(rain || sprinkler) ==> wet>.
```

### Negation `--`

"Not A" — negation.

```narsese
<(-- rain) ==> dry>.              // not raining implies dry
```

### Extensional/intensional intersection, difference, images

These are less commonly used in practice but are part of NAL:

| Connector | Syntax | Meaning |
|-----------|--------|---------|
| Ext. intersection | `(&, a, b)` | a AND b (extensional) |
| Int. intersection | `(\|, a, b)` | a AND b (intensional) |
| Ext. difference | `(-, a, b)` | a BUT NOT b |
| Int. difference | `(~, a, b)` | a BUT NOT b (intensional) |
| Ext. image | `(/1, R, _)` or `(/2, R, _)` | Relational image |
| Int. image | `(\1, R, _)` or `(\2, R, _)` | Relational image |

---

## 5. Truth Values

Every belief carries a **truth value** — a pair of numbers that describe how certain
the system is about the statement.

### The two components

| Component | Range | Meaning |
|-----------|-------|---------|
| **Frequency** (f) | 0.0–1.0 | How often the statement is true. 1.0 = always true, 0.0 = always false, 0.5 = as often true as false. |
| **Confidence** (c) | 0.0–0.99 | How much evidence backs the frequency estimate. 0.0 = no evidence (guess), 0.9 = strong evidence. Confidence can never reach 1.0. |

### Specifying truth values

If you omit the truth value, defaults apply: frequency=1.0, confidence=0.9.

```narsese
<bird --> animal>.                  // defaults: f=1.0 c=0.9
<bird --> animal>. {0.8 0.7}       // explicit: f=0.8 c=0.7
<bird --> animal>. %0.8;0.7%       // legacy format (also accepted)
```

The `{f c}` format is preferred. There **must** be a space before the opening `{`.

### Expectation

Internally, the system often reduces a truth value to a single number called
**expectation**:

```
expectation = confidence * (frequency - 0.5) + 0.5
```

This combines frequency and confidence into a single 0–1 metric. It is the basis for
all decision making: an operation executes only if its goal's expectation exceeds
`DECISION_THRESHOLD` (default 0.501).

| f | c | Expectation | Interpretation |
|---|---|-------------|----------------|
| 1.0 | 0.9 | 0.95 | Strong positive belief |
| 0.0 | 0.9 | 0.05 | Strong negative belief |
| 1.0 | 0.0 | 0.50 | No evidence (neutral) |
| 0.5 | 0.9 | 0.50 | Known to be ambiguous |
| 1.0 | 0.5 | 0.75 | Moderate positive belief |

### How truth values change

The system derives new beliefs by applying **truth functions** to existing ones.
The key truth functions and when they apply:

| Rule | When | Frequency formula | Confidence formula |
|------|------|-------------------|--------------------|
| **Deduction** | A-->B, B-->C => A-->C | f1 * f2 | c1 * c2 * f1 |
| **Abduction** | A-->B, C-->B => A-->C | f2 | w2c(f1 * c1 * c2) |
| **Induction** | A-->B, A-->C => B-->C | f2 | w2c(f1 * c1 * c2) |
| **Revision** | Two independent beliefs about the same thing | weighted avg | increases |
| **Intersection** | A and B both true | f1 * f2 | c1 * c2 |
| **Projection** | Belief from time T applied at time T' | f (unchanged) | c * decay^|T-T'| |
| **Eternalization** | Temporal belief becomes timeless rule | f (unchanged) | w2c(c) |

Where `w2c(w) = w / (w + horizon)` converts evidence weight to confidence (horizon
defaults to 1.0).

**Key insight**: Every derivation **loses** some confidence. The system can never become
more certain than its inputs. Confidence only increases through **revision** — receiving
independent evidence for the same claim.

---

## 6. Tense (Temporal Markers)

Beliefs and goals exist in time. The tense marker goes after the punctuation:

| Marker | Name | Meaning |
|--------|------|---------|
| *(none)* | Eternal | Timeless truth. "Birds are animals, always." |
| `:\|:` | Present | "This is true right now." Tagged with current system time. |
| `:\:` | Past | "This was true." (Questions and beliefs only, beliefs not actually supported for input) |
| `:/:` | Future | "This will be true." (Questions only) |

```narsese
<bird --> animal>.             // eternal — always true
<food --> visible>. :|:        // present — true right now
<food --> visible>? :|:        // question — is food visible now?
<reward --> got>? :/:          // question — will I get a reward?
```

### Restrictions

- **Beliefs** (`.`): eternal or present only. Past (`:\:`) and future (`:/:`) beliefs
  return `NAR_ERR_PARSE`.
- **Goals** (`!`): present only. Eternal goals return `NAR_ERR_PARSE`. You must always
  write `! :\|:` (or `! :|:`).
- **Questions** (`?`): any tense is valid.

### The `dt=` prefix

For temporal implications, you can specify the expected time offset between cause
and effect:

```narsese
dt=5 <light_on =/> light_off>. :|:
```

This says the implication has a 5-cycle delay. The system learns `dt` values
automatically from observation; you rarely need to specify this manually.

### How time works internally

The system maintains a `currentTime` counter (starts at 1). Each call to
`NAR_Cycles(nar, 1)` or `NAR_AddInputNarsese` (which runs one cycle internally)
increments it. Present-tense events are stamped with `currentTime`. Eternal events
get the special value `OCCURRENCE_ETERNAL` (-1).

Beliefs decay over time via **truth projection**: confidence is multiplied by
`TRUTH_PROJECTION_DECAY^distance` (default 0.8 per cycle). A belief from 10 cycles
ago retains only `0.8^10 = 0.107` of its original confidence. This makes the system
naturally favor recent information.

---

## 7. Questions and Answers

Questions make the system search its memory for matching beliefs.

### Simple questions

```narsese
<robin --> animal>?            // does robin inherit from animal?
```

The system responds with the best-matching belief:

```
Answer: <robin --> animal>. creationTime=2 Stamp=[2,1] Truth: frequency=1.000000, confidence=0.810000
```

### Questions with query variables

Use `?1` through `?9` as wildcards:

```narsese
<robin --> ?1>?                // what categories does robin belong to?
<? 1 --> animal>?              // what things are animals?
```

### Temporal questions

```narsese
<food --> visible>? :|:        // is food visible now? (checks recent beliefs)
<reward --> got>? :/:          // will I get a reward? (checks predictions)
```

### How answers are ranked

The system searches all concepts for beliefs that unify with the question term.
Results are ranked by truth **expectation** (f * c + 0.5 * (1 - c)). The
highest-expectation match is returned.

For temporal questions, belief **projection** applies: a belief from 10 cycles ago
is projected to the current time with reduced confidence before comparison.

### Programmatic answer handling

With the callback API, you don't need to parse stdout:

```python
def on_answer(narsese, freq, conf, occ_time, create_time):
    print(f"Answer: {narsese} f={freq:.2f} c={conf:.2f}")

nar.on_answer(on_answer)
nar.add_narsese("<robin --> animal>?")
```

The callback fires with the structured answer data.

---

## 8. Operations

Operations are how DriftNARS acts on the world. An operation is a named action
(starting with `^`) that the system can decide to execute. When it does, your code
gets called.

### Registering operations

From Python:

```python
nar.add_operation("^left")
nar.add_operation("^grab")
nar.add_operation("^say")
```

From C:

```c
Feedback my_left(Term args) {
    printf("Turning left!\n");
    return (Feedback) {0};
}
NAR_AddOperation(nar, "^left", my_left);
```

Operation names **must** start with `^`. You can register up to `OPERATIONS_MAX` (10)
operations.

### How operations get triggered

The system executes operations through **goal-directed reasoning**. The pipeline is:

1. You teach the system a **temporal implication**: "if condition A holds and you do
   ^op, then result B follows."
2. You give the system a **goal**: "I want B."
3. The system checks whether condition A currently holds.
4. If A holds and the implication is strong enough, the system decides to execute ^op.

```python
# 1. Teach: "if you see food and grab, you get food"
nar.add_narsese("<(see_food &/ ^grab) =/> have_food>.")

# 2. Provide the precondition
nar.add_narsese("see_food. :|:")

# 3. Give the goal
nar.add_narsese("have_food! :|:")

# Result: DriftNARS executes ^grab
```

### Operation arguments

Operations can take arguments using the product-inheritance pattern:

```narsese
<(*, {SELF}, target) --> ^goto>.      // goto(SELF, target)
<(*, {SELF}, door) --> ^open>.        // open(SELF, door)
```

The `{SELF}` in the first product position marks it as an executable operation
belonging to this agent. When the operation fires, the execution callback receives
`op_name` (e.g. `"^goto"`) and `args` as a Narsese product string (e.g.
`"({SELF} * park)"`). For operations without arguments, `args` is an empty string.

A full example with argument handling:

```python
nar.add_operation("^goto")

# Teach: "if at_home and you goto(SELF, park), you arrive at park"
nar.add_narsese("<(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>.")
nar.add_narsese("at_home. :|:")
nar.add_narsese("at_park! :|:")

# => execution callback fires with op_name="^goto", args="({SELF} * park)"
```

### Parsing args in the callback

The `args` string is a Narsese product term. To extract the parts:

```python
def parse_product_args(args):
    """Parse '({SELF} * park)' into ['SELF', 'park']."""
    if not args or " * " not in args:
        return [args] if args else []
    inner = args.strip("()")
    return [p.strip().strip("{}") for p in inner.split(" * ")]

def on_execution(op_name, args):
    if op_name == "^goto":
        parts = parse_product_args(args)
        target = parts[-1]   # "park"
        robot.go_to(target)
    elif op_name == "^grab":
        robot.grab()

nar.on_execution(on_execution)
```

### The execution callback

For library consumers, the recommended pattern is to use `add_operation` (which
registers a no-op C callback) and handle execution via the execution callback:

```python
def on_execution(op_name, args):
    if op_name == "^grab":
        robot.grab()
    elif op_name == "^goto":
        parts = args.strip("()").split(" * ")
        target = parts[-1]
        robot.go_to(target)

nar.on_execution(on_execution)
nar.add_operation("^grab")
nar.add_operation("^goto")
```

This is simpler than writing C callbacks and works naturally from any language.

### Motor babbling

When the system has no learned implications (or low confidence ones), it explores
by randomly executing operations. This is called **motor babbling** and happens with
probability `MOTOR_BABBLING_CHANCE` (default 0.2). As the system learns which
operations achieve which goals, babbling is suppressed in favor of learned behavior.

You can tune this:

```python
# Access the NAR_t struct field directly via ctypes if needed,
# or use shell commands in interactive mode:
# *motorbabbling=0.1
```

---

## 9. Variables

Variables let you express general rules and ask open-ended questions.

### Independent variables `$1`–`$9`

Universally quantified. "For all X, ..."

```narsese
<$1 --> bird> ==> <$1 --> animal>.    // for all X: if X is a bird, X is an animal
```

### Dependent variables `#1`–`#9`

Existentially quantified. "There exists an X such that ..."

```narsese
<(#1 * #2) --> likes>.                // someone likes something
```

### Query variables `?1`–`?9`

Used in questions. "What X satisfies ...?"

```narsese
<robin --> ?1>?                       // what is robin?
<(?1 * ?2) --> friends>?              // who are friends?
```

Variables are automatically introduced during inference. When the system observes
`<robin --> bird>` and `<robin --> animal>`, it can derive the rule
`<$1 --> bird> ==> <$1 --> animal>` by introducing variable `$1`.

---

## 10. Temporal and Procedural Learning

This is the heart of what makes DriftNARS useful for adaptive systems. The system
learns cause-and-effect relationships from observation and uses them to achieve goals.

### Learning from observation

When events happen in sequence, the system automatically forms temporal implications:

```python
# Cycle 1: the light is on
nar.add_narsese("light_on. :|:")

# Cycle 2: you press the button
nar.add_narsese("^press. :|:")

# Cycle 3: the light turns off
nar.add_narsese("light_off. :|:")
```

From this, the system derives:

```
<(light_on &/ ^press) =/> light_off>.
```

"If the light is on and you press the button, the light turns off."

The confidence of this implication starts low (single observation). Repeated
observations of the same pattern increase confidence through **revision**.

### Using learned knowledge

Once the implication exists, goals trigger action:

```python
nar.add_narsese("light_on. :|:")       # precondition holds
nar.add_narsese("light_off! :|:")      # goal: turn off the light
# => system executes ^press
```

### Anticipation

When the system executes an operation based on an implication, it **anticipates**
the outcome. If the predicted outcome doesn't occur, the implication's confidence
decreases (negative confirmation). This allows the system to:

- Stop repeating actions that don't work
- Adapt when the environment changes
- Prefer more reliable implications

### Sequence decomposition

Goals involving multi-step sequences are decomposed. If the system knows:

```
<(a &/ b &/ c) =/> result>.
```

And you give it `result! :\|:`, it will:

1. Check if `a` holds (precondition).
2. Execute `b` (if `b` is an operation).
3. Check if the result of `b` holds.
4. Execute `c`.

Each step must satisfy `CONDITION_THRESHOLD` (default 0.501) for the decomposition
to proceed.

---

## 11. The Evidential Base (Stamps)

Every belief carries a **stamp** — a list of evidence IDs tracking which input events
contributed to it. This prevents circular reasoning.

```
Input: <A>. → stamp = [1]
Input: <B>. → stamp = [2]
Derived: <C>. from A and B → stamp = [1, 2]
```

When the system tries to combine two beliefs, it checks for **stamp overlap**. If
both beliefs share an evidence ID, they came from the same source and cannot be
used for revision (which requires independent evidence). Instead, the system uses
**choice** — keeping whichever belief has higher confidence.

The stamp size is limited to `STAMP_SIZE` (10). Beyond this, older evidence IDs are
silently dropped. This is a known capacity limit, not a bug.

---

## 12. The Reasoning Cycle

Each call to `NAR_Cycles(nar, 1)` performs one reasoning cycle:

1. **Select belief events** — Pick the highest-priority belief from the event buffer.
2. **Learn temporal implications** — If the belief is recent, search for sequential
   patterns with other recent events and operations.
3. **Anticipate** — For any active implications, predict outcomes.
4. **Select goal events** — Pick the highest-priority goal.
5. **Goal processing** — Decompose sequences, apply declarative reasoning, make
   decisions.
6. **Execute** — If a decision exceeds the threshold, execute the operation.
7. **Inference** — Apply NAL inference rules between the selected belief and all
   related concepts in memory.
8. **Forgetting** — Decay all priorities. Evict lowest-priority events and concepts
   when buffers are full.

`NAR_AddInputNarsese` automatically runs one cycle after adding the input.

### Tunable parameters

All of these live on the `NAR_t` struct and can be modified at runtime:

| Parameter | Default | What it does |
|-----------|---------|--------------|
| `DECISION_THRESHOLD` | 0.501 | Min expectation to execute an operation |
| `MOTOR_BABBLING_CHANCE` | 0.2 | Probability of random operation execution |
| `TRUTH_PROJECTION_DECAY` | 0.8 | Per-cycle confidence decay for temporal beliefs |
| `TRUTH_EVIDENTIAL_HORIZON` | 1.0 | Prior evidence weight in w2c conversion |
| `QUESTION_PRIMING` | 0.1 | Priority boost for concepts matched by questions |
| `ANTICIPATION_CONFIDENCE` | 0.01 | Confidence assigned to failed predictions |

---

## 13. Concepts and Memory

The system organizes knowledge into **concepts**. Each concept corresponds to a unique
term and stores:

| Field | What it holds |
|-------|---------------|
| `belief` | Best eternal belief about this term |
| `belief_spike` | Most recent temporal belief |
| `predicted_belief` | Anticipated future state (from implications) |
| `goal_spike` | Most recent goal about this term |
| `precondition_beliefs[op]` | Temporal implications involving operation `op` |
| `implication_links` | Declarative implications |
| `priority` | Attention weight (decays each cycle) |

Memory holds up to `CONCEPTS_MAX` (4096) concepts. When full, the lowest-priority
concept is evicted to make room.

---

## 14. Complete Syntax Reference

### Statement format

```
[dt=N] <term COPULA term>[punctuation] [tense] [{freq conf}]
```

All parts except the term and punctuation are optional.

### Copulas

| Input | Canonical | Name |
|-------|-----------|------|
| `-->` | `:` | Inheritance |
| `<->` | `=` | Similarity |
| `=/>` | `$` | Temporal implication |
| `==>` | `?` | Implication |
| `<=>` | `^` | Equivalence |
| `\|->` | `,` | Instance property |

### Connectors

| Input | Canonical | Name |
|-------|-----------|------|
| `*` | `*` | Product |
| `&/` | `+` | Sequence |
| `&&` | `;` | Conjunction |
| `\|\|` | `_` | Disjunction |
| `--` | `!` | Negation |
| `{` `}` | `"` | Extensional set |
| `[` `]` | `'` | Intensional set |
| `/1` `/2` | `/` `%` | External images |
| `\1` `\2` | `\` `#` | Internal images |

### Punctuation

| Symbol | Type | Tense requirement |
|--------|------|-------------------|
| `.` | Belief | Eternal or `:\|:` |
| `!` | Goal | `:\|:` required |
| `?` | Question | Any tense |

### Truth values

```
{frequency confidence}         // preferred
%frequency;confidence%         // legacy
```

Both values in [0.0, 1.0]. Default: f=1.0, c=0.9.

### Tense markers

```
(none)     eternal
:|:        present
:\:        past (questions only)
:/:        future (questions only)
```

---

## 15. Common Patterns

### Teach a fact

```python
nar.add_narsese("<bird --> animal>.")           # eternal belief
nar.add_narsese("<robin --> bird>.")
nar.add_narsese("<robin --> animal>?")           # system deduces the answer
```

### Teach a temporal rule and trigger it

```python
nar.add_operation("^press")
nar.add_narsese("<(light_on &/ ^press) =/> light_off>.")   # teach the rule
nar.add_narsese("light_on. :|:")                            # precondition holds
nar.add_narsese("light_off! :|:")                           # goal
# => ^press executes (args="")
```

### Trigger an operation with arguments

```python
def on_execution(op_name, args):
    if op_name == "^goto":
        parts = args.strip("()").split(" * ")
        target = parts[-1]
        print(f"Going to {target}")

nar.on_execution(on_execution)
nar.add_operation("^goto")
nar.add_narsese("<(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>.")
nar.add_narsese("at_home. :|:")
nar.add_narsese("at_park! :|:")
# => ^goto executes (args="({SELF} * park)")
```

### Learn from experience (no explicit rule)

```python
nar.add_operation("^grab")

# Round 1: observe sequence
nar.add_narsese("see_food. :|:")
nar.add_narsese("^grab. :|:")                  # feedback: grab happened
nar.add_narsese("have_food. :|:")              # outcome observed

# The system learns: <(see_food &/ ^grab) =/> have_food>

# Round 2: use learned knowledge
nar.add_narsese("see_food. :|:")
nar.add_narsese("have_food! :|:")              # goal triggers ^grab
```

### Express uncertainty

```python
nar.add_narsese("<weather --> sunny>. {0.7 0.5}")   # 70% sunny, moderate confidence
nar.add_narsese("<weather --> rainy>. {0.3 0.5}")   # 30% rainy, moderate confidence
```

### Negative beliefs

```python
nar.add_narsese("<cat --> dog>. {0.0 0.9}")     # cat is NOT a dog (high confidence)
nar.add_narsese("<(-- danger) ==> safe>.")      # not danger implies safe
```

### Multi-step procedures

```python
nar.add_operation("^step1")
nar.add_operation("^step2")

nar.add_narsese("<(ready &/ ^step1 &/ ^step2) =/> done>.")
nar.add_narsese("ready. :|:")
nar.add_narsese("done! :|:")
# => system decomposes into ^step1, then ^step2
```

---

## 16. Limits and Constraints

| Limit | Value | What happens on violation |
|-------|-------|--------------------------|
| `ATOMS_MAX` | 255 | Assert failure |
| `OPERATIONS_MAX` | 10 | Assert failure |
| `CONCEPTS_MAX` | 4096 | Lowest-priority concept evicted |
| `NARSESE_LEN_MAX` | 2148 chars | `NAR_ERR_PARSE` returned |
| `ATOMIC_TERM_LEN_MAX` | 32 chars | Silently truncated |
| `COMPOUND_TERM_SIZE_MAX` | 64 nodes | Assert failure |
| `MAX_SEQUENCE_LEN` | 3 | Warning, early return |
| `STAMP_SIZE` | 10 | Silently truncated |
| `TABLE_SIZE` | 20 | Lowest-truth implication evicted |
| `CYCLING_BELIEF_EVENTS_MAX` | 40 | Lowest-priority event evicted |
| `CYCLING_GOAL_EVENTS_MAX` | 400 | Lowest-priority event evicted |

### Input restrictions

- Operation names must start with `^`.
- Goals must have a tense marker (usually `:\|:`).
- Beliefs cannot have past (`:\:`) or future (`:/:`) tense.
- There must be a space before `{` in truth values.
- Input must be at least 2 characters and shorter than `NARSESE_LEN_MAX`.

---

## 17. Callback Reference

The four callback types correspond to four kinds of system output:

### Event callback

Fires for every input, derived, or revised event.

```python
def on_event(reason, narsese, type_char, freq, conf, priority, occ_time, dt):
    # reason: 1=INPUT, 2=DERIVED, 3=REVISED
    # type_char: '.' (belief) or '!' (goal)  [the raw char from EVENT_TYPE]
    # occ_time: -1 for eternal, else cycle number
    pass
```

**Note on the `type_char` parameter**: this is the C `char type` field, which is
`EVENT_TYPE_BELIEF` (2, renders as `chr(2)` = `'\x02'`) or `EVENT_TYPE_GOAL` (1,
renders as `chr(1)` = `'\x01'`). The Python wrapper converts this to a string
for you: `'\x02'` for beliefs, `'\x01'` for goals.

### Answer callback

Fires when a question is answered.

```python
def on_answer(narsese, freq, conf, occ_time, create_time):
    # narsese: "None" if no answer found
    # occ_time: -1 for eternal answers
    pass
```

### Decision callback

Fires when the system decides to execute an operation (before execution).

```python
def on_decision(expectation, imp_narsese, imp_freq, imp_conf, imp_dt,
                prec_narsese, prec_freq, prec_conf, prec_occ_time):
    # expectation: the desire value that triggered the decision
    # imp_*: the temporal implication used
    # prec_*: the precondition belief that matched
    pass
```

### Execution callback

Fires when an operation is about to execute.

```python
def on_execution(op_name, args):
    # op_name: e.g. "^left", "^grab", "^goto"
    # args: Narsese product string, e.g. "({SELF} * park)", or "" if no arguments
    #
    # To extract argument parts from a product:
    #   parts = args.strip("()").split(" * ")  # ["SELF}", "park"]
    pass
```

---

## 18. Glossary

| Term | Meaning |
|------|---------|
| **Atom** | A single named entity: `bird`, `^goto`, `SELF` |
| **Belief** | A statement the system holds to be true, with a truth value |
| **Concept** | A memory entry for a unique term, storing beliefs and implications |
| **Confidence** | How much evidence supports a belief's frequency estimate |
| **Copula** | A relationship connector: `-->`, `<->`, `=/>`, etc. |
| **Cycle** | One step of the reasoning loop |
| **Eternal** | A timeless belief (no occurrence time) |
| **Expectation** | Single-number summary of truth: `c * (f - 0.5) + 0.5` |
| **Frequency** | How often a statement is true (0.0–1.0) |
| **Goal** | A desired state the system should try to achieve |
| **Implication** | A stored cause-effect relationship between terms |
| **Motor babbling** | Random operation execution for exploration |
| **NAL** | Non-Axiomatic Logic — the formal logic underlying NARS |
| **Narsese** | The input language for NARS systems |
| **Operation** | A named action (`^op`) the system can execute |
| **Projection** | Adjusting a belief's confidence for temporal distance |
| **Revision** | Combining two independent beliefs about the same thing |
| **Stamp** | Evidential base tracking which inputs contributed to a belief |
| **Term** | The subject of a statement — atomic or compound |
| **Truth value** | A (frequency, confidence) pair describing belief strength |
