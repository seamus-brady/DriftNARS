#!/usr/bin/env python3
"""
DriftNARS + DriftScript example — goals, operations, and callbacks.

Same four examples as example.py, rewritten using DriftScript syntax.

Run from the repository root after building:
    make
    cd examples/python && python3 example_driftscript.py
"""
from driftnars import DriftNARS


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def parse_product_args(args):
    """Parse a Narsese product term like '({SELF} * park)' into a list of parts."""
    if not args or " * " not in args:
        return [args] if args else []
    inner = args.strip()
    if inner.startswith("(") and inner.endswith(")"):
        inner = inner[1:-1].strip()
    return [p.strip() for p in inner.split(" * ")]


# ---------------------------------------------------------------------------
# Your application logic — these functions get called by DriftNARS
# ---------------------------------------------------------------------------

light_state = {"on": True}

def handle_execution(op_name, args):
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
    if narsese == "None":
        print(f"  <<< No answer found")
    else:
        print(f"  <<< Answer: {narsese}  (freq={freq:.2f}, conf={conf:.2f})")

def handle_decision(exp, imp, imp_f, imp_c, imp_dt, prec, prec_f, prec_c, prec_occ):
    print(f"  --- Decision: expectation={exp:.3f}")
    print(f"      Rule: {imp}  (f={imp_f:.2f}, c={imp_c:.2f})")
    print(f"      Because: {prec}  (f={prec_f:.2f}, c={prec_c:.2f})")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

with DriftNARS() as nar:
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
    nar.add_driftscript("""
        (believe (inherit bird animal))
        (believe (inherit robin bird))
    """)

    print("Asking: is robin an animal?")
    nar.add_driftscript("(ask (inherit robin animal))")
    print()

    # ------------------------------------------------------------------
    # Example 2: Goals trigger operations via temporal implications
    # ------------------------------------------------------------------
    print("=" * 60)
    print("EXAMPLE 2: Goal triggers an operation")
    print("=" * 60)
    print()

    nar.add_driftscript("(def-op ^press)")

    print("Teaching: if light is on and you press, light goes off.")
    nar.add_driftscript("(believe (predict (seq light_on (call ^press)) light_off))")

    print("Input:    light is on (now)")
    nar.add_driftscript("(believe light_on :now)")

    print("Goal:     want light off")
    print()
    nar.add_driftscript("(goal light_off)")

    nar.add_driftscript("(cycles 5)")
    print()

    # ------------------------------------------------------------------
    # Example 3: Operations with arguments
    # ------------------------------------------------------------------
    print("=" * 60)
    print("EXAMPLE 3: Operation with arguments")
    print("=" * 60)
    print()

    nar.add_driftscript("(def-op ^goto)")

    print("Teaching: if at_home and goto(SELF,park), arrive at park.")
    nar.add_driftscript("""
        (believe (predict (seq at_home
            (call ^goto (ext-set SELF) park)) at_park))
    """)

    print("Input:    at_home (now)")
    nar.add_driftscript("(believe at_home :now)")

    print("Goal:     want to be at park")
    print()
    nar.add_driftscript("(goal at_park)")

    nar.add_driftscript("(cycles 5)")
    print()

    # ------------------------------------------------------------------
    # Example 4: Learning from experience
    # ------------------------------------------------------------------
    print("=" * 60)
    print("EXAMPLE 4: Learning from experience")
    print("=" * 60)
    print()

    nar2 = DriftNARS()
    nar2.on_answer(handle_answer)
    nar2.on_decision(handle_decision)
    nar2.on_execution(handle_execution)

    nar2.add_driftscript("(def-op ^grab)")

    # Round 1: observe a sequence of events
    print("Observing: see_food, then ^grab happens, then have_food.")
    nar2.add_driftscript("""
        (believe see_food :now)
        (believe ^grab :now)           ; operation feedback — it happened
        (believe have_food :now)
        (cycles 5)
    """)

    # Round 2: same sequence — reinforces the learned rule
    print("Observing again (reinforcement)...")
    nar2.add_driftscript("""
        (believe see_food :now)
        (believe ^grab :now)
        (believe have_food :now)
        (cycles 5)
    """)

    # Round 3: provide precondition + goal — system should act
    print()
    print("Now: see_food is true, and we want have_food.")
    nar2.add_driftscript("""
        (believe see_food :now)
        (goal have_food)
        (cycles 10)
    """)

    nar2.close()
    print()
    print("Done.")
