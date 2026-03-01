"""
DriftNARS Python bindings via ctypes.

Usage:
    from driftnars import DriftNARS

    with DriftNARS() as nar:
        nar.on_answer(lambda narsese, f, c, occ, ct: print(f"Answer: {narsese}"))
        nar.add_narsese("<bird --> animal>.")
        nar.add_narsese("<robin --> bird>.")
        nar.add_narsese("<robin --> animal>?")
"""
import ctypes
import ctypes.util
import os
import platform
import sys

# Callback C function types (must match typedefs in NAR.h)
_EventCB = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,   # userdata
    ctypes.c_int,       # reason
    ctypes.c_char_p,    # narsese
    ctypes.c_char,      # type
    ctypes.c_double,    # freq
    ctypes.c_double,    # conf
    ctypes.c_double,    # priority
    ctypes.c_long,      # occTime
    ctypes.c_double,    # dt
)

_AnswerCB = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,   # userdata
    ctypes.c_char_p,    # narsese
    ctypes.c_double,    # freq
    ctypes.c_double,    # conf
    ctypes.c_long,      # occTime
    ctypes.c_long,      # createTime
)

_DecisionCB = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,   # userdata
    ctypes.c_double,    # expectation
    ctypes.c_char_p,    # imp
    ctypes.c_double,    # imp_freq
    ctypes.c_double,    # imp_conf
    ctypes.c_double,    # imp_dt
    ctypes.c_char_p,    # prec
    ctypes.c_double,    # prec_freq
    ctypes.c_double,    # prec_conf
    ctypes.c_long,      # prec_occTime
)

_ExecutionCB = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,   # userdata
    ctypes.c_char_p,    # op
    ctypes.c_char_p,    # args
)


def _find_lib():
    """Locate libdriftnars shared library."""
    ext = "dylib" if platform.system() == "Darwin" else "so"
    # Look relative to this file first (../../bin/), then cwd
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(here, "..", "..", "bin", f"libdriftnars.{ext}"),
        os.path.join(os.getcwd(), "bin", f"libdriftnars.{ext}"),
        os.path.join(os.getcwd(), f"libdriftnars.{ext}"),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return os.path.abspath(path)
    # Last resort: let ctypes search the system
    found = ctypes.util.find_library("driftnars")
    if found:
        return found
    raise FileNotFoundError(
        f"Cannot find libdriftnars.{ext}. Build with 'make' first.\n"
        f"Searched: {candidates}"
    )


class DriftNARS:
    """High-level wrapper around the DriftNARS C library."""

    # Reason codes (match NAR.h)
    EVENT_INPUT = 1
    EVENT_DERIVED = 2
    EVENT_REVISED = 3

    def __init__(self, lib_path=None):
        path = lib_path or _find_lib()
        self._lib = ctypes.CDLL(path)
        self._setup_api()
        self._nar = self._lib.NAR_New()
        if not self._nar:
            raise MemoryError("NAR_New() returned NULL")
        self._lib.NAR_INIT(self._nar)
        # prevent GC of stored CFUNCTYPE instances
        self._cb_refs = []

    def _setup_api(self):
        lib = self._lib
        lib.NAR_New.argtypes = []
        lib.NAR_New.restype = ctypes.c_void_p
        lib.NAR_Free.argtypes = [ctypes.c_void_p]
        lib.NAR_Free.restype = None
        lib.NAR_INIT.argtypes = [ctypes.c_void_p]
        lib.NAR_INIT.restype = ctypes.c_int
        lib.NAR_AddInputNarsese.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.NAR_AddInputNarsese.restype = ctypes.c_int
        lib.NAR_Cycles.argtypes = [ctypes.c_void_p, ctypes.c_int]
        lib.NAR_Cycles.restype = None
        lib.NAR_AddOperationName.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.NAR_AddOperationName.restype = None
        lib.NAR_SetEventHandler.argtypes = [ctypes.c_void_p, _EventCB, ctypes.c_void_p]
        lib.NAR_SetEventHandler.restype = None
        lib.NAR_SetAnswerHandler.argtypes = [ctypes.c_void_p, _AnswerCB, ctypes.c_void_p]
        lib.NAR_SetAnswerHandler.restype = None
        lib.NAR_SetDecisionHandler.argtypes = [ctypes.c_void_p, _DecisionCB, ctypes.c_void_p]
        lib.NAR_SetDecisionHandler.restype = None
        lib.NAR_SetExecutionHandler.argtypes = [ctypes.c_void_p, _ExecutionCB, ctypes.c_void_p]
        lib.NAR_SetExecutionHandler.restype = None
        lib.Shell_ProcessInput.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.Shell_ProcessInput.restype = ctypes.c_int

    def close(self):
        if self._nar:
            self._lib.NAR_Free(self._nar)
            self._nar = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def __del__(self):
        self.close()

    def add_narsese(self, sentence):
        """Add a Narsese sentence. Returns 0 on success."""
        return self._lib.NAR_AddInputNarsese(self._nar, sentence.encode("utf-8"))

    def cycles(self, n):
        """Run n inference cycles."""
        self._lib.NAR_Cycles(self._nar, n)

    def add_operation(self, name):
        """Register an operation name (must start with ^)."""
        self._lib.NAR_AddOperationName(self._nar, name.encode("utf-8"))

    def on_event(self, callback):
        """Set event callback: callback(reason, narsese, type, freq, conf, priority, occTime, dt)"""
        def _wrapper(_ud, reason, narsese, typ, freq, conf, priority, occ, dt):
            # c_char arrives as bytes in Python 3
            type_chr = typ.decode("ascii") if isinstance(typ, bytes) else chr(typ)
            callback(reason, narsese.decode("utf-8", errors="replace"),
                     type_chr, freq, conf, priority, occ, dt)
        ref = _EventCB(_wrapper)
        self._cb_refs.append(ref)
        self._lib.NAR_SetEventHandler(self._nar, ref, None)

    def on_answer(self, callback):
        """Set answer callback: callback(narsese, freq, conf, occTime, createTime)"""
        def _wrapper(_ud, narsese, freq, conf, occ, ct):
            callback(narsese.decode("utf-8", errors="replace"), freq, conf, occ, ct)
        ref = _AnswerCB(_wrapper)
        self._cb_refs.append(ref)
        self._lib.NAR_SetAnswerHandler(self._nar, ref, None)

    def on_decision(self, callback):
        """Set decision callback: callback(exp, imp, imp_f, imp_c, imp_dt, prec, prec_f, prec_c, prec_occ)"""
        def _wrapper(_ud, exp, imp, imp_f, imp_c, imp_dt, prec, prec_f, prec_c, prec_occ):
            callback(exp, imp.decode("utf-8", errors="replace"), imp_f, imp_c, imp_dt,
                     prec.decode("utf-8", errors="replace"), prec_f, prec_c, prec_occ)
        ref = _DecisionCB(_wrapper)
        self._cb_refs.append(ref)
        self._lib.NAR_SetDecisionHandler(self._nar, ref, None)

    def _process_shell_input(self, line):
        """Send a shell command (e.g. '*reset', '*volume=100') to the NAR."""
        return self._lib.Shell_ProcessInput(self._nar, line.encode("utf-8"))

    def add_driftscript(self, source):
        """Compile DriftScript source and feed it to the reasoner.

        Handles narsese input, shell commands, cycle directives, and operation
        registration automatically.
        """
        from driftscript import DriftScript
        ds = DriftScript()
        for result in ds.compile(source):
            if result.kind == "narsese":
                self.add_narsese(result.value)
            elif result.kind == "shell_command":
                self._process_shell_input(result.value)
            elif result.kind == "cycles":
                self.cycles(int(result.value))
            elif result.kind == "def_op":
                self.add_operation(result.value)
            else:
                raise ValueError(f"Unknown DriftScript result kind: {result.kind!r}")

    def on_execution(self, callback):
        """Set execution callback: callback(op_name, args)"""
        def _wrapper(_ud, op, args):
            callback(op.decode("utf-8", errors="replace"),
                     args.decode("utf-8", errors="replace"))
        ref = _ExecutionCB(_wrapper)
        self._cb_refs.append(ref)
        self._lib.NAR_SetExecutionHandler(self._nar, ref, None)
