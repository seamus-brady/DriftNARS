#!/usr/bin/env python3
"""
DriftNARS Python example — demonstrates all four callback types.

Run from the repository root after building:
    make
    python3 examples/python/example.py
"""
from driftnars import DriftNARS

REASON_NAMES = {1: "INPUT", 2: "DERIVED", 3: "REVISED"}

def on_event(reason, narsese, typ, freq, conf, priority, occ_time, dt):
    label = REASON_NAMES.get(reason, str(reason))
    print(f"  [event]     {label}: {narsese}{typ} "
          f"f={freq:.2f} c={conf:.2f} pri={priority:.2f} occ={occ_time}")

def on_answer(narsese, freq, conf, occ_time, create_time):
    print(f"  [answer]    {narsese} f={freq:.2f} c={conf:.2f} "
          f"occ={occ_time} created={create_time}")

def on_decision(exp, imp, imp_f, imp_c, imp_dt, prec, prec_f, prec_c, prec_occ):
    print(f"  [decision]  exp={exp:.3f} imp={imp} "
          f"f={imp_f:.2f} c={imp_c:.2f} dt={imp_dt:.1f}")
    print(f"              prec={prec} f={prec_f:.2f} c={prec_c:.2f} occ={prec_occ}")

def on_execution(op, args):
    print(f"  [execution] {op} args={args}")


with DriftNARS() as nar:
    nar.on_event(on_event)
    nar.on_answer(on_answer)
    nar.on_decision(on_decision)
    nar.on_execution(on_execution)

    print("=== Inheritance reasoning ===")
    nar.add_narsese("<bird --> animal>.")
    nar.add_narsese("<robin --> bird>.")
    nar.add_narsese("<robin --> animal>?")

    print("\n=== Operation handling ===")
    nar.add_operation("^left")
    nar.add_narsese("<(a &/ ^left) =/> b>.")
    nar.add_narsese("a. :|:")
    nar.add_narsese("b! :|:")
    nar.cycles(10)

    print("\nDone.")
