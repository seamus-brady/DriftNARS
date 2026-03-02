#!/usr/bin/env python3
"""
DriftNARS Python example — goals, operations, and callbacks.

Demonstrates:
  1. Basic inheritance reasoning and question answering
  2. Teaching a temporal rule, then using a goal to trigger an operation
  3. Operations with arguments — the system passes structured args to your code
  4. Learning from experience (observation -> implicit rule -> goal-driven execution)

Run from the repository root after building:
    make
    cd examples/python && python3 example.py
"""
from driftnars import DriftNARS


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def parse_product_args(args):
    """Parse a Narsese product term like '({SELF} * park)' into a list of parts.

    Returns an empty list for empty/atomic args.
    """
    if not args or " * " not in args:
        return [args] if args else []
    # Strip outer parens: "({SELF} * park * foo)" -> "{SELF} * park * foo"
    inner = args.strip()
    if inner.startswith("(") and inner.endswith(")"):
        inner = inner[1:-1].strip()
    return [p.strip() for p in inner.split(" * ")]


# ---------------------------------------------------------------------------
# Your application logic — these functions get called by DriftNARS
# ---------------------------------------------------------------------------

light_state = {"on": True}

def handle_execution(op_name, args):
    """Called whenever DriftNARS executes an operation.

    op_name is the operation (e.g. "^goto"), args is a Narsese term string
    (e.g. "({SELF} * park)") or "" if the operation takes no arguments.
    """
    if op_name == "^press":
        light_state["on"] = not light_state["on"]
        state = "ON" if light_state["on"] else "OFF"
        print(f"  >>> ^press executed! Light is now {state}")
    elif op_name == "^goto":
        parts = parse_product_args(args)
        target = parts[-1] if parts else "unknown"
        print(f"  >>> ^goto executed! Moving to: {target}")
    elif op_name == "^grab":
        print(f"  >>> ^grab executed! Grabbed the object.")
    else:
        print(f"  >>> {op_name} executed (args: {args!r})")

def handle_answer(narsese, freq, conf, occ_time, create_time):
    """Called whenever DriftNARS answers a question."""
    if narsese == "None":
        print(f"  <<< No answer found")
    else:
        print(f"  <<< Answer: {narsese}  (freq={freq:.2f}, conf={conf:.2f})")

def handle_decision(exp, imp, imp_f, imp_c, imp_dt, prec, prec_f, prec_c, prec_occ):
    """Called when DriftNARS decides to act (before execution)."""
    print(f"  --- Decision: expectation={exp:.3f}")
    print(f"      Rule: {imp}  (f={imp_f:.2f}, c={imp_c:.2f})")
    print(f"      Because: {prec}  (f={prec_f:.2f}, c={prec_c:.2f})")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

with DriftNARS() as nar:
    # Wire up callbacks — DriftNARS will call our functions above
    nar.on_answer(handle_answer)
    nar.on_decision(handle_decision)
    nar.on_execution(handle_execution)

    # ------------------------------------------------------------------
    # Example 1: Inheritance reasoning — the system deduces new knowledge
    # ------------------------------------------------------------------
    print("=" * 60)
    print("EXAMPLE 1: Inheritance reasoning")
    print("=" * 60)
    print()
    print("Teaching: robin is a bird, bird is an animal.")
    nar.add_narsese("<bird --> animal>.")
    nar.add_narsese("<robin --> bird>.")

    print("Asking: is robin an animal?")
    nar.add_narsese("<robin --> animal>?")
    print()

    # ------------------------------------------------------------------
    # Example 2: Goals trigger operations via temporal implications
    # ------------------------------------------------------------------
    print("=" * 60)
    print("EXAMPLE 2: Goal triggers an operation")
    print("=" * 60)
    print()

    # Register ^press — DriftNARS will call handle_execution when it fires
    nar.add_operation("^press")

    # Teach the rule: "if the light is on and you press, the light goes off"
    print("Teaching: <(light_on &/ ^press) =/> light_off>.")
    nar.add_narsese("<(light_on &/ ^press) =/> light_off>.")

    # Tell the system the light is on right now
    print("Input:    light_on. :|:")
    nar.add_narsese("light_on. :|:")

    # Give the system a goal: "I want the light off"
    print("Goal:     light_off! :|:")
    print()
    nar.add_narsese("light_off! :|:")

    # The system should decide to execute ^press, which calls handle_execution
    nar.cycles(5)
    print()

    # ------------------------------------------------------------------
    # Example 3: Operations with arguments
    # ------------------------------------------------------------------
    print("=" * 60)
    print("EXAMPLE 3: Operation with arguments")
    print("=" * 60)
    print()

    # Register ^goto — when it fires, handle_execution receives the args
    nar.add_operation("^goto")

    # Teach: "if at_home and you goto(SELF,park), you arrive at park"
    # The (*, {SELF}, park) product is the argument structure
    print("Teaching: <(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>.")
    nar.add_narsese("<(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>.")

    # Precondition: we are at home
    print("Input:    at_home. :|:")
    nar.add_narsese("at_home. :|:")

    # Goal: we want to be at the park
    print("Goal:     at_park! :|:")
    print()
    nar.add_narsese("at_park! :|:")

    # DriftNARS should execute ^goto with args "({SELF} * park)"
    nar.cycles(5)
    print()

    # ------------------------------------------------------------------
    # Example 4: Learning from experience
    # ------------------------------------------------------------------
    print("=" * 60)
    print("EXAMPLE 4: Learning from experience")
    print("=" * 60)
    print()

    # Start fresh
    nar2 = DriftNARS()
    nar2.on_answer(handle_answer)
    nar2.on_decision(handle_decision)
    nar2.on_execution(handle_execution)

    nar2.add_operation("^grab")

    # Round 1: observe a sequence of events (no explicit rule given)
    print("Observing: see_food, then ^grab happens, then have_food.")
    nar2.add_narsese("see_food. :|:")
    nar2.add_narsese("^grab. :|:")       # operation feedback (it happened)
    nar2.add_narsese("have_food. :|:")   # outcome observed
    nar2.cycles(5)

    # Round 2: same sequence — reinforces the learned rule
    print("Observing again (reinforcement)...")
    nar2.add_narsese("see_food. :|:")
    nar2.add_narsese("^grab. :|:")
    nar2.add_narsese("have_food. :|:")
    nar2.cycles(5)

    # Round 3: now provide precondition + goal — system should act
    print()
    print("Now: see_food is true, and we want have_food.")
    nar2.add_narsese("see_food. :|:")
    nar2.add_narsese("have_food! :|:")
    nar2.cycles(10)

    nar2.close()
    print()
    print("Done.")
