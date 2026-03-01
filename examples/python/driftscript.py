"""
DriftScript — a Lisp-like language that compiles to Narsese.

    from driftscript import DriftScript

    ds = DriftScript()
    results = ds.compile("(believe (inherit robin bird))")
    # => [CompileResult(kind='narsese', value='<robin --> bird>.')]

Four-stage pipeline: tokenize -> parse -> compile -> emit.
"""
from dataclasses import dataclass


class DriftScriptError(Exception):
    """Raised on parse or compile errors, with source location info."""
    def __init__(self, message, line=None, col=None, source_line=None):
        self.line = line
        self.col = col
        self.source_line = source_line
        loc = ""
        if line is not None:
            loc = f" (line {line}, col {col})"
        super().__init__(f"{message}{loc}")


@dataclass
class CompileResult:
    kind: str   # "narsese" | "shell_command" | "cycles" | "def_op"
    value: str


# ---------------------------------------------------------------------------
# Tokenizer
# ---------------------------------------------------------------------------

@dataclass
class Token:
    type: str   # "LPAREN" | "RPAREN" | "KEYWORD" | "SYMBOL"
    value: str
    line: int
    col: int


def tokenize(source):
    """Tokenize DriftScript source into a list of Tokens."""
    tokens = []
    lines = source.split("\n")
    for lineno, line_text in enumerate(lines, 1):
        i = 0
        while i < len(line_text):
            ch = line_text[i]
            if ch == ";":
                break  # comment to end of line
            if ch in " \t\r":
                i += 1
                continue
            col = i + 1
            if ch == "(":
                tokens.append(Token("LPAREN", "(", lineno, col))
                i += 1
            elif ch == ")":
                tokens.append(Token("RPAREN", ")", lineno, col))
                i += 1
            elif ch == ":":
                # keyword token
                j = i + 1
                while j < len(line_text) and line_text[j] not in " \t\r\n();":
                    j += 1
                tokens.append(Token("KEYWORD", line_text[i:j], lineno, col))
                i = j
            else:
                # symbol: atoms, ops, vars, numbers
                j = i
                while j < len(line_text) and line_text[j] not in " \t\r\n();":
                    j += 1
                tokens.append(Token("SYMBOL", line_text[i:j], lineno, col))
                i = j
    return tokens


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

@dataclass
class Atom:
    value: str
    line: int
    col: int

    def __repr__(self):
        return f"Atom({self.value!r})"


def parse(tokens):
    """Parse tokens into a list of top-level AST nodes (Atoms or lists)."""
    pos = [0]  # mutable index

    def _parse_expr():
        if pos[0] >= len(tokens):
            return None
        tok = tokens[pos[0]]
        if tok.type == "RPAREN":
            raise DriftScriptError("Unexpected ')'", tok.line, tok.col)
        if tok.type == "LPAREN":
            return _parse_list()
        # SYMBOL or KEYWORD
        pos[0] += 1
        return Atom(tok.value, tok.line, tok.col)

    def _parse_list():
        open_tok = tokens[pos[0]]
        pos[0] += 1  # skip LPAREN
        items = []
        while pos[0] < len(tokens) and tokens[pos[0]].type != "RPAREN":
            node = _parse_expr()
            if node is not None:
                items.append(node)
        if pos[0] >= len(tokens):
            raise DriftScriptError("Unclosed '('", open_tok.line, open_tok.col)
        pos[0] += 1  # skip RPAREN
        return items

    result = []
    while pos[0] < len(tokens):
        node = _parse_expr()
        if node is not None:
            result.append(node)
    return result


# ---------------------------------------------------------------------------
# Compiler
# ---------------------------------------------------------------------------

# Copula map: keyword -> Narsese copula
_COPULAS = {
    "inherit":  "-->",
    "similar":  "<->",
    "imply":    "==>",
    "predict":  "=/>",
    "equiv":    "<=>",
    "instance": "|->",
}

# Connector map: keyword -> (Narsese operator, wrapper style)
# Styles: "infix" = A op B, "prefix" = (op, A, B), "set" = {A, B} or [A, B],
#         "unary" = (-- A)
_CONNECTORS = {
    "seq":        ("&/",  "infix"),
    "and":        ("&&",  "infix"),
    "or":         ("||",  "infix"),
    "not":        ("--",  "unary"),
    "product":    ("*",   "product"),
    "ext-set":    (None,  "ext-set"),
    "int-set":    (None,  "int-set"),
    "ext-inter":  ("&",   "prefix"),
    "int-inter":  ("|",   "prefix"),
    "ext-diff":   ("-",   "prefix"),
    "int-diff":   ("~",   "prefix"),
    "ext-image1": ("/1",  "prefix"),
    "ext-image2": ("/2",  "prefix"),
    "int-image1": ("\\1", "prefix"),
    "int-image2": ("\\2", "prefix"),
}

_VALID_CONFIG_KEYS = {
    "volume", "motorbabbling", "decisionthreshold",
    "anticipationconfidence", "questionpriming", "babblingops",
    "similaritydistance",
}

_TENSE_MAP = {
    ":now":    " :|:",
    ":past":   " :\\:",
    ":future": " :/:",
}


def _head_value(node):
    """Return the head symbol string of a list node, or None."""
    if isinstance(node, list) and len(node) > 0 and isinstance(node[0], Atom):
        return node[0].value
    return None


def _node_loc(node):
    """Extract line/col from a node for error messages."""
    if isinstance(node, Atom):
        return node.line, node.col
    if isinstance(node, list) and len(node) > 0:
        return _node_loc(node[0])
    return None, None


def _error(msg, node=None, line=None, col=None):
    if node is not None and line is None:
        line, col = _node_loc(node)
    return DriftScriptError(msg, line, col)


class _Compiler:
    """Stateful compiler for one top-level form. Handles variable renaming."""

    def __init__(self):
        self._var_map = {}       # "$name" -> "$1" etc., per sentence
        self._dep_map = {}       # "#name" -> "#1" etc.
        self._query_map = {}     # "?name" -> "?1" etc.
        self._used_independent = set()
        self._used_dependent = set()
        self._used_query = set()

    def _scan_reserved_vars(self, node):
        """Pre-scan AST for explicitly-numbered vars like $1, #2, ?3."""
        if isinstance(node, Atom):
            v = node.value
            if len(v) >= 2 and v[0] in "$#?" and v[1:].isdigit():
                num = int(v[1:])
                if v[0] == "$":
                    self._used_independent.add(num)
                elif v[0] == "#":
                    self._used_dependent.add(num)
                else:
                    self._used_query.add(num)
        elif isinstance(node, list):
            for child in node:
                self._scan_reserved_vars(child)

    def _rename_var(self, name, node):
        """Map a named variable to a numbered one. e.g. $x -> $1."""
        prefix = name[0]     # $ or # or ?
        rest = name[1:]

        # Already numbered: pass through
        if rest.isdigit():
            return name

        if prefix == "$":
            mapping, used = self._var_map, self._used_independent
        elif prefix == "#":
            mapping, used = self._dep_map, self._used_dependent
        else:
            mapping, used = self._query_map, self._used_query

        if name in mapping:
            return mapping[name]

        # Find next available number
        n = 1
        while n in used:
            n += 1
        if n > 9:
            raise _error(f"Too many {prefix}-variables (max 9)", node)
        used.add(n)
        mapped = f"{prefix}{n}"
        mapping[name] = mapped
        return mapped

    def compile_toplevel(self, node):
        """Compile a top-level AST node into a CompileResult."""
        if isinstance(node, Atom):
            raise _error(f"Bare atom at top level: {node.value!r}", node)

        head = _head_value(node)
        if head is None:
            raise _error("Empty expression", node)

        # Pre-scan for reserved variable numbers
        self._scan_reserved_vars(node)

        if head in ("believe", "ask", "goal"):
            return self._compile_sentence(node)
        elif head == "cycles":
            return self._compile_cycles(node)
        elif head == "reset":
            return CompileResult("shell_command", "*reset")
        elif head == "def-op":
            return self._compile_def_op(node)
        elif head == "config":
            return self._compile_config(node)
        elif head == "concurrent":
            return CompileResult("shell_command", "*concurrent")
        else:
            raise _error(f"Unknown top-level form: {head!r}", node[0])

    def _compile_sentence(self, node):
        head = node[0].value
        args = node[1:]

        if len(args) < 1:
            raise _error(f"'{head}' requires at least a term", node[0])

        # Separate term from keyword options
        term_node = args[0]
        options = args[1:]

        # Parse options
        tense = None
        truth_f = None
        truth_c = None
        dt_val = None

        i = 0
        while i < len(options):
            opt = options[i]
            if isinstance(opt, Atom) and opt.value in (":now", ":past", ":future"):
                tense = opt.value
                i += 1
            elif isinstance(opt, Atom) and opt.value == ":truth":
                if i + 2 >= len(options):
                    raise _error(":truth requires F and C values", opt)
                truth_f = options[i + 1]
                truth_c = options[i + 2]
                if not isinstance(truth_f, Atom) or not isinstance(truth_c, Atom):
                    raise _error(":truth F C must be numbers", opt)
                i += 3
            elif isinstance(opt, Atom) and opt.value == ":dt":
                if i + 1 >= len(options):
                    raise _error(":dt requires a value", opt)
                dt_val = options[i + 1]
                if not isinstance(dt_val, Atom):
                    raise _error(":dt value must be a number", opt)
                i += 2
            else:
                raise _error(f"Unknown option: {opt!r}", opt if isinstance(opt, Atom) else node)
                i += 1

        # Compile the term
        term_str = self._compile_term(term_node)

        # Build the Narsese string
        if head == "believe":
            punct = "."
        elif head == "ask":
            punct = "?"
        else:  # goal
            punct = "!"

        # Determine tense suffix
        if head == "goal":
            tense_str = " :|:"  # goals always present
        elif tense is not None:
            tense_str = _TENSE_MAP[tense]
        else:
            tense_str = ""

        # Build truth value suffix
        truth_str = ""
        if truth_f is not None:
            truth_str = f" {{{truth_f.value} {truth_c.value}}}"

        # Build dt prefix
        dt_str = ""
        if dt_val is not None:
            dt_str = f"dt={dt_val.value} "

        narsese = f"{dt_str}{term_str}{punct}{tense_str}{truth_str}"
        return CompileResult("narsese", narsese)

    def _compile_term(self, node):
        """Compile a term node (Atom or list) into a Narsese term string."""
        if isinstance(node, Atom):
            return self._compile_atom(node)

        # It's a list
        if len(node) == 0:
            raise _error("Empty term", node)

        head = _head_value(node)
        if head is None:
            raise _error("Term list must start with a symbol", node)

        args = node[1:]

        # Copula?
        if head in _COPULAS:
            return self._compile_copula(head, args, node)

        # Connector?
        if head in _CONNECTORS:
            return self._compile_connector(head, args, node)

        # call shorthand?
        if head == "call":
            return self._compile_call(args, node)

        raise _error(f"Unknown term form: {head!r}", node[0])

    def _compile_atom(self, atom):
        """Compile a bare atom, handling variable renaming."""
        v = atom.value
        if len(v) >= 2 and v[0] in "$#?":
            return self._rename_var(v, atom)
        return v

    def _compile_copula(self, head, args, node):
        if len(args) != 2:
            raise _error(f"'{head}' requires exactly 2 arguments, got {len(args)}", node[0])
        left = self._compile_term(args[0])
        right = self._compile_term(args[1])
        cop = _COPULAS[head]
        return f"<{left} {cop} {right}>"

    def _compile_connector(self, head, args, node):
        op, style = _CONNECTORS[head]

        if style == "unary":
            if len(args) != 1:
                raise _error(f"'{head}' requires exactly 1 argument", node[0])
            inner = self._compile_term(args[0])
            return f"({op} {inner})"

        if style == "ext-set":
            if len(args) < 1:
                raise _error(f"'{head}' requires at least 1 argument", node[0])
            parts = [self._compile_term(a) for a in args]
            return "{" + ", ".join(parts) + "}"

        if style == "int-set":
            if len(args) < 1:
                raise _error(f"'{head}' requires at least 1 argument", node[0])
            parts = [self._compile_term(a) for a in args]
            return "[" + ", ".join(parts) + "]"

        if style == "product":
            if len(args) < 1:
                raise _error(f"'{head}' requires at least 1 argument", node[0])
            parts = [self._compile_term(a) for a in args]
            return "(*, " + ", ".join(parts) + ")"

        if style == "infix":
            if head == "seq":
                if len(args) < 2 or len(args) > 3:
                    raise _error(f"'seq' requires 2 or 3 arguments, got {len(args)}", node[0])
            else:
                if len(args) != 2:
                    raise _error(f"'{head}' requires exactly 2 arguments", node[0])
            parts = [self._compile_term(a) for a in args]
            return "(" + f" {op} ".join(parts) + ")"

        if style == "prefix":
            if len(args) != 2:
                raise _error(f"'{head}' requires exactly 2 arguments", node[0])
            left = self._compile_term(args[0])
            right = self._compile_term(args[1])
            return f"({op}, {left}, {right})"

        raise _error(f"Internal error: unknown connector style {style!r}", node[0])

    def _compile_call(self, args, node):
        """Compile (call ^op arg1 arg2 ...) shorthand."""
        if len(args) < 1:
            raise _error("'call' requires at least an operation name", node[0])

        op_node = args[0]
        if not isinstance(op_node, Atom) or not op_node.value.startswith("^"):
            raise _error("'call' first argument must be an operation (^name)", op_node if isinstance(op_node, Atom) else node[0])

        op_name = op_node.value
        call_args = args[1:]

        if len(call_args) == 0:
            return op_name  # bare operation

        # Build product and inheritance
        parts = [self._compile_term(a) for a in call_args]
        product = "(*, " + ", ".join(parts) + ")"
        return f"<{product} --> {op_name}>"

    def _compile_cycles(self, node):
        if len(node) != 2:
            raise _error("'cycles' requires exactly 1 argument (count)", node[0])
        count_node = node[1]
        if not isinstance(count_node, Atom):
            raise _error("'cycles' argument must be a number", node[0])
        try:
            int(count_node.value)
        except ValueError:
            raise _error(f"'cycles' argument must be a number, got {count_node.value!r}", count_node)
        return CompileResult("cycles", count_node.value)

    def _compile_def_op(self, node):
        if len(node) != 2:
            raise _error("'def-op' requires exactly 1 argument (^name)", node[0])
        name_node = node[1]
        if not isinstance(name_node, Atom) or not name_node.value.startswith("^"):
            raise _error("'def-op' argument must be an operation name (^name)", name_node if isinstance(name_node, Atom) else node[0])
        return CompileResult("def_op", name_node.value)

    def _compile_config(self, node):
        if len(node) != 3:
            raise _error("'config' requires exactly 2 arguments (key value)", node[0])
        key_node = node[1]
        val_node = node[2]
        if not isinstance(key_node, Atom):
            raise _error("'config' key must be a symbol", node[0])
        if not isinstance(val_node, Atom):
            raise _error("'config' value must be a symbol", node[0])
        key = key_node.value
        if key not in _VALID_CONFIG_KEYS:
            raise _error(f"Unknown config key: {key!r}. Valid: {', '.join(sorted(_VALID_CONFIG_KEYS))}", key_node)
        return CompileResult("shell_command", f"*{key}={val_node.value}")


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

class DriftScript:
    """DriftScript compiler: S-expressions -> Narsese."""

    def compile(self, source):
        """Compile multi-statement DriftScript source into a list of CompileResults."""
        tokens = tokenize(source)
        ast = parse(tokens)
        results = []
        for node in ast:
            compiler = _Compiler()
            results.append(compiler.compile_toplevel(node))
        return results

    def compile_one(self, source):
        """Compile a single DriftScript statement. Raises if source has != 1 statement."""
        results = self.compile(source)
        if len(results) != 1:
            raise DriftScriptError(f"Expected 1 statement, got {len(results)}")
        return results[0]

    def to_narsese(self, source):
        """Compile and return only the Narsese strings. Raises on non-narsese directives."""
        results = self.compile(source)
        out = []
        for r in results:
            if r.kind != "narsese":
                raise DriftScriptError(f"to_narsese() encountered a {r.kind} directive: {r.value}")
            out.append(r.value)
        return out
