"""Unit tests for the DriftScript compiler (pure Python, no C library needed)."""
import pytest
from driftscript import DriftScript, DriftScriptError, tokenize, parse, Atom


@pytest.fixture
def ds():
    return DriftScript()


# ---------------------------------------------------------------------------
# Tokenizer
# ---------------------------------------------------------------------------

class TestTokenizer:
    def test_parens(self):
        tokens = tokenize("(believe x)")
        types = [t.type for t in tokens]
        assert types == ["LPAREN", "SYMBOL", "SYMBOL", "RPAREN"]

    def test_keyword(self):
        tokens = tokenize(":now :truth :dt")
        assert all(t.type == "KEYWORD" for t in tokens)
        assert [t.value for t in tokens] == [":now", ":truth", ":dt"]

    def test_comment_stripped(self):
        tokens = tokenize("(believe x) ; this is a comment")
        assert len(tokens) == 4  # only the expression tokens

    def test_comment_full_line(self):
        tokens = tokenize("; just a comment\n(believe x)")
        types = [t.type for t in tokens]
        assert types == ["LPAREN", "SYMBOL", "SYMBOL", "RPAREN"]

    def test_line_col_tracking(self):
        tokens = tokenize("(believe\n  x)")
        lparen = tokens[0]
        assert lparen.line == 1 and lparen.col == 1
        x_tok = [t for t in tokens if t.value == "x"][0]
        assert x_tok.line == 2

    def test_op_symbol(self):
        tokens = tokenize("^press")
        assert tokens[0].type == "SYMBOL"
        assert tokens[0].value == "^press"

    def test_var_symbol(self):
        tokens = tokenize("$x #y ?z $1")
        assert all(t.type == "SYMBOL" for t in tokens)
        assert [t.value for t in tokens] == ["$x", "#y", "?z", "$1"]

    def test_empty_source(self):
        assert tokenize("") == []
        assert tokenize("  \n  \n  ") == []


class TestTokenizerStrings:
    def test_basic_string(self):
        tokens = tokenize('"hello"')
        assert len(tokens) == 1
        assert tokens[0].type == "STRING"
        assert tokens[0].value == "hello"

    def test_escaped_quote(self):
        tokens = tokenize(r'"say \"hi\""')
        assert tokens[0].type == "STRING"
        assert tokens[0].value == 'say "hi"'

    def test_escaped_backslash(self):
        tokens = tokenize(r'"a\\b"')
        assert tokens[0].type == "STRING"
        assert tokens[0].value == "a\\b"

    def test_unterminated_string(self):
        with pytest.raises(DriftScriptError, match="Unterminated string"):
            tokenize('"hello')

    def test_unknown_escape(self):
        with pytest.raises(DriftScriptError, match="Unknown escape"):
            tokenize(r'"a\nb"')

    def test_string_in_expression(self):
        tokens = tokenize('(believe "bird")')
        types = [t.type for t in tokens]
        assert types == ["LPAREN", "SYMBOL", "STRING", "RPAREN"]
        assert tokens[2].value == "bird"

    def test_string_line_col(self):
        tokens = tokenize('  "hello"')
        assert tokens[0].line == 1
        assert tokens[0].col == 3

    def test_string_adjacent_to_paren(self):
        tokens = tokenize('("bird")')
        types = [t.type for t in tokens]
        assert types == ["LPAREN", "STRING", "RPAREN"]

    def test_symbol_stops_at_quote(self):
        tokens = tokenize('abc"def"')
        assert len(tokens) == 2
        assert tokens[0].type == "SYMBOL"
        assert tokens[0].value == "abc"
        assert tokens[1].type == "STRING"
        assert tokens[1].value == "def"


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

class TestParser:
    def test_simple_atom(self):
        tokens = tokenize("x")
        ast = parse(tokens)
        assert len(ast) == 1
        assert isinstance(ast[0], Atom)
        assert ast[0].value == "x"

    def test_nested_list(self):
        tokens = tokenize('(inherit (ext-set "A") "B")')
        ast = parse(tokens)
        assert len(ast) == 1
        assert isinstance(ast[0], list)
        assert len(ast[0]) == 3  # inherit, (ext-set "A"), "B"

    def test_multiple_toplevel(self):
        tokens = tokenize("(believe x) (ask y)")
        ast = parse(tokens)
        assert len(ast) == 2

    def test_unclosed_paren(self):
        tokens = tokenize("(believe x")
        with pytest.raises(DriftScriptError, match="Unclosed"):
            parse(tokens)

    def test_unexpected_rparen(self):
        tokens = tokenize(")")
        with pytest.raises(DriftScriptError, match="Unexpected"):
            parse(tokens)

    def test_string_parsed_as_quoted_atom(self):
        tokens = tokenize('"bird"')
        ast = parse(tokens)
        assert len(ast) == 1
        assert isinstance(ast[0], Atom)
        assert ast[0].value == "bird"
        assert ast[0].quoted is True

    def test_symbol_parsed_as_unquoted_atom(self):
        tokens = tokenize("bird")
        ast = parse(tokens)
        assert ast[0].quoted is False


# ---------------------------------------------------------------------------
# Copulas
# ---------------------------------------------------------------------------

class TestCopulas:
    def test_inherit(self, ds):
        r = ds.compile_one('(believe (inherit "bird" "animal"))')
        assert r.value == "<bird --> animal>."

    def test_similar(self, ds):
        r = ds.compile_one('(believe (similar "cat" "dog"))')
        assert r.value == "<cat <-> dog>."

    def test_imply(self, ds):
        r = ds.compile_one('(believe (imply "A" "B"))')
        assert r.value == "<A ==> B>."

    def test_predict(self, ds):
        r = ds.compile_one('(believe (predict "A" "B"))')
        assert r.value == "<A =/> B>."

    def test_equiv(self, ds):
        r = ds.compile_one('(believe (equiv "A" "B"))')
        assert r.value == "<A <=> B>."

    def test_instance(self, ds):
        r = ds.compile_one('(believe (instance "A" "B"))')
        assert r.value == "<A |-> B>."

    def test_copula_wrong_arity(self, ds):
        with pytest.raises(DriftScriptError, match="2 arguments"):
            ds.compile_one('(believe (inherit "A"))')

    def test_copula_too_many(self, ds):
        with pytest.raises(DriftScriptError, match="2 arguments"):
            ds.compile_one('(believe (inherit "A" "B" "C"))')


# ---------------------------------------------------------------------------
# Connectors
# ---------------------------------------------------------------------------

class TestConnectors:
    def test_seq_2(self, ds):
        r = ds.compile_one('(believe (predict (seq "a" "b") "c"))')
        assert r.value == "<(a &/ b) =/> c>."

    def test_seq_3(self, ds):
        r = ds.compile_one('(believe (predict (seq "a" "b" "c") "d"))')
        assert r.value == "<(a &/ b &/ c) =/> d>."

    def test_seq_too_few(self, ds):
        with pytest.raises(DriftScriptError, match="2 or 3"):
            ds.compile_one('(believe (seq "a"))')

    def test_seq_too_many(self, ds):
        with pytest.raises(DriftScriptError, match="2 or 3"):
            ds.compile_one('(believe (seq "a" "b" "c" "d"))')

    def test_and(self, ds):
        r = ds.compile_one('(believe (and "A" "B"))')
        assert "(A && B)" in r.value

    def test_or(self, ds):
        r = ds.compile_one('(believe (or "A" "B"))')
        assert "(A || B)" in r.value

    def test_not(self, ds):
        r = ds.compile_one('(believe (not "A"))')
        assert "(-- A)" in r.value

    def test_product(self, ds):
        r = ds.compile_one('(believe (inherit (product "A" "B" "C") "rel"))')
        assert "(*, A, B, C)" in r.value

    def test_ext_set_single(self, ds):
        r = ds.compile_one('(believe (inherit (ext-set "SELF") "person"))')
        assert "{SELF}" in r.value

    def test_ext_set_multi(self, ds):
        r = ds.compile_one('(believe (inherit (ext-set "A" "B") "group"))')
        assert "{A, B}" in r.value

    def test_int_set(self, ds):
        r = ds.compile_one('(believe (inherit "x" (int-set "bright" "loud")))')
        assert "[bright, loud]" in r.value

    def test_ext_inter(self, ds):
        r = ds.compile_one('(believe (inherit (ext-inter "A" "B") "C"))')
        assert "(&, A, B)" in r.value

    def test_int_inter(self, ds):
        r = ds.compile_one('(believe (inherit (int-inter "A" "B") "C"))')
        assert "(|, A, B)" in r.value

    def test_ext_diff(self, ds):
        r = ds.compile_one('(believe (inherit (ext-diff "A" "B") "C"))')
        assert "(-, A, B)" in r.value

    def test_int_diff(self, ds):
        r = ds.compile_one('(believe (inherit (int-diff "A" "B") "C"))')
        assert "(~, A, B)" in r.value

    def test_ext_image1(self, ds):
        r = ds.compile_one('(believe (inherit (ext-image1 "R" "X") "Y"))')
        assert "(/1, R, X)" in r.value

    def test_ext_image2(self, ds):
        r = ds.compile_one('(believe (inherit (ext-image2 "R" "X") "Y"))')
        assert "(/2, R, X)" in r.value

    def test_int_image1(self, ds):
        r = ds.compile_one('(believe (inherit (int-image1 "R" "X") "Y"))')
        assert "(\\1, R, X)" in r.value

    def test_int_image2(self, ds):
        r = ds.compile_one('(believe (inherit (int-image2 "R" "X") "Y"))')
        assert "(\\2, R, X)" in r.value

    def test_not_wrong_arity(self, ds):
        with pytest.raises(DriftScriptError, match="1 argument"):
            ds.compile_one('(believe (not "A" "B"))')


# ---------------------------------------------------------------------------
# Call shorthand
# ---------------------------------------------------------------------------

class TestCall:
    def test_call_with_args(self, ds):
        r = ds.compile_one('(believe (inherit (call ^goto (ext-set "SELF") "park") "action"))')
        assert "<(*, {SELF}, park) --> ^goto>" in r.value

    def test_call_no_args(self, ds):
        r = ds.compile_one('(believe (predict (seq "light_on" (call ^press)) "light_off"))')
        assert "^press" in r.value

    def test_call_bare_op(self, ds):
        r = ds.compile_one('(believe (predict (seq "a" (call ^press)) "b"))')
        assert "(a &/ ^press)" in r.value

    def test_call_missing_op(self, ds):
        with pytest.raises(DriftScriptError, match="operation"):
            ds.compile_one("(believe (call))")


# ---------------------------------------------------------------------------
# Sentence forms
# ---------------------------------------------------------------------------

class TestSentences:
    def test_believe_eternal(self, ds):
        r = ds.compile_one('(believe (inherit "bird" "animal"))')
        assert r.kind == "narsese"
        assert r.value == "<bird --> animal>."

    def test_believe_now(self, ds):
        r = ds.compile_one('(believe "light_on" :now)')
        assert r.value == "light_on. :|:"

    def test_believe_truth(self, ds):
        r = ds.compile_one('(believe (inherit "bird" "animal") :truth 1.0 0.9)')
        assert r.value == "<bird --> animal>. {1.0 0.9}"

    def test_believe_now_truth(self, ds):
        r = ds.compile_one('(believe "light_on" :now :truth 1.0 0.9)')
        assert r.value == "light_on. :|: {1.0 0.9}"

    def test_believe_dt(self, ds):
        r = ds.compile_one('(believe (predict "a" "b") :now :dt 5)')
        assert r.value == "dt=5 <a =/> b>. :|:"

    def test_ask_eternal(self, ds):
        r = ds.compile_one('(ask (inherit "robin" "animal"))')
        assert r.value == "<robin --> animal>?"

    def test_ask_now(self, ds):
        r = ds.compile_one('(ask (inherit "robin" "animal") :now)')
        assert r.value == "<robin --> animal>? :|:"

    def test_ask_past(self, ds):
        r = ds.compile_one('(ask (inherit "robin" "animal") :past)')
        assert r.value == "<robin --> animal>? :\\:"

    def test_ask_future(self, ds):
        r = ds.compile_one('(ask (inherit "robin" "animal") :future)')
        assert r.value == "<robin --> animal>? :/:"

    def test_goal(self, ds):
        r = ds.compile_one('(goal "light_off")')
        assert r.value == "light_off! :|:"

    def test_goal_truth(self, ds):
        r = ds.compile_one('(goal "light_off" :truth 1.0 0.9)')
        assert r.value == "light_off! :|: {1.0 0.9}"

    def test_bare_atom_believe(self, ds):
        r = ds.compile_one('(believe "light_on" :now)')
        assert r.value == "light_on. :|:"


# ---------------------------------------------------------------------------
# Variables
# ---------------------------------------------------------------------------

class TestVariables:
    def test_named_to_numbered(self, ds):
        r = ds.compile_one('(believe (imply (inherit $x "bird") (inherit $x "animal")))')
        assert r.value == "<<$1 --> bird> ==> <$1 --> animal>>."

    def test_multiple_vars(self, ds):
        r = ds.compile_one('(believe (imply (inherit $a $b) (similar $b $a)))')
        assert "$1" in r.value
        assert "$2" in r.value
        # Same var maps to same number
        assert r.value.count("$1") == 2
        assert r.value.count("$2") == 2

    def test_passthrough_numbered(self, ds):
        r = ds.compile_one('(believe (imply (inherit $1 "bird") (inherit $1 "animal")))')
        assert r.value == "<<$1 --> bird> ==> <$1 --> animal>>."

    def test_dependent_var(self, ds):
        r = ds.compile_one('(believe (imply (inherit $x "bird") (inherit $x #y)))')
        assert "#1" in r.value

    def test_query_var(self, ds):
        r = ds.compile_one('(ask (inherit ?x "animal"))')
        assert "?1" in r.value

    def test_collision_avoidance(self, ds):
        # $1 is explicitly used, so $x should get $2
        r = ds.compile_one('(believe (imply (inherit $1 "bird") (inherit $x "animal")))')
        assert "$1" in r.value
        assert "$2" in r.value


# ---------------------------------------------------------------------------
# Directives
# ---------------------------------------------------------------------------

class TestDirectives:
    def test_cycles(self, ds):
        r = ds.compile_one("(cycles 10)")
        assert r.kind == "cycles"
        assert r.value == "10"

    def test_reset(self, ds):
        r = ds.compile_one("(reset)")
        assert r.kind == "shell_command"
        assert r.value == "*reset"

    def test_def_op(self, ds):
        r = ds.compile_one("(def-op ^press)")
        assert r.kind == "def_op"
        assert r.value == "^press"

    def test_config(self, ds):
        r = ds.compile_one("(config volume 100)")
        assert r.kind == "shell_command"
        assert r.value == "*volume=100"

    def test_config_invalid_key(self, ds):
        with pytest.raises(DriftScriptError, match="Unknown config"):
            ds.compile_one("(config badkey 1)")

    def test_concurrent(self, ds):
        r = ds.compile_one("(concurrent)")
        assert r.kind == "shell_command"
        assert r.value == "*concurrent"

    def test_cycles_non_number(self, ds):
        with pytest.raises(DriftScriptError, match="number"):
            ds.compile_one("(cycles abc)")


# ---------------------------------------------------------------------------
# Nested compounds
# ---------------------------------------------------------------------------

class TestNested:
    def test_predict_seq_call(self, ds):
        source = '(believe (predict (seq "at_home" (call ^goto (ext-set "SELF") "park")) "at_park"))'
        r = ds.compile_one(source)
        assert r.value == "<(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>."

    def test_predict_seq_bare_op(self, ds):
        source = '(believe (predict (seq "light_on" (call ^press)) "light_off"))'
        r = ds.compile_one(source)
        assert r.value == "<(light_on &/ ^press) =/> light_off>."

    def test_deep_nesting(self, ds):
        source = '(believe (imply (and (inherit $x "bird") (inherit $x "flyer")) (inherit $x "animal")))'
        r = ds.compile_one(source)
        assert "==>" in r.value
        assert "&&" in r.value
        assert "$1" in r.value


# ---------------------------------------------------------------------------
# Multi-statement and to_narsese
# ---------------------------------------------------------------------------

class TestMulti:
    def test_multi_statement(self, ds):
        source = """
        (believe (inherit "bird" "animal"))
        (believe (inherit "robin" "bird"))
        (ask (inherit "robin" "animal"))
        """
        results = ds.compile(source)
        assert len(results) == 3
        assert results[0].value == "<bird --> animal>."
        assert results[1].value == "<robin --> bird>."
        assert results[2].value == "<robin --> animal>?"

    def test_to_narsese(self, ds):
        source = '(believe (inherit "A" "B"))\n(ask (inherit "C" "D"))'
        narsese = ds.to_narsese(source)
        assert narsese == ["<A --> B>.", "<C --> D>?"]

    def test_to_narsese_rejects_directives(self, ds):
        with pytest.raises(DriftScriptError, match="to_narsese"):
            ds.to_narsese("(cycles 10)")

    def test_variable_scope_per_sentence(self, ds):
        # $x in first sentence should be $1, $x in second sentence also $1
        # (they're independent)
        source = """
        (believe (imply (inherit $x "bird") (inherit $x "animal")))
        (believe (imply (inherit $y "fish") (inherit $y "swimmer")))
        """
        results = ds.compile(source)
        assert "$1" in results[0].value
        assert "$1" in results[1].value


# ---------------------------------------------------------------------------
# Error cases
# ---------------------------------------------------------------------------

class TestErrors:
    def test_bare_atom_toplevel(self, ds):
        with pytest.raises(DriftScriptError, match="Bare atom"):
            ds.compile("hello")

    def test_unknown_form(self, ds):
        with pytest.raises(DriftScriptError, match="Unknown top-level"):
            ds.compile_one("(frobnicate x)")

    def test_unknown_term_form(self, ds):
        with pytest.raises(DriftScriptError, match="Unknown term"):
            ds.compile_one('(believe (frobnicate "A" "B"))')

    def test_empty_believe(self, ds):
        with pytest.raises(DriftScriptError, match="requires"):
            ds.compile_one("(believe)")

    def test_compile_one_multiple(self, ds):
        with pytest.raises(DriftScriptError, match="Expected 1"):
            ds.compile_one('(believe "x" :now) (ask "y")')


# ---------------------------------------------------------------------------
# String quoting enforcement
# ---------------------------------------------------------------------------

class TestQuotingEnforcement:
    def test_bare_atom_rejected(self, ds):
        with pytest.raises(DriftScriptError, match='must be a string literal'):
            ds.compile_one('(believe (inherit bird "animal"))')

    def test_quoted_keyword_rejected(self, ds):
        with pytest.raises(DriftScriptError, match="must not be quoted"):
            ds.compile_one('(believe ("inherit" "A" "B"))')

    def test_quoted_toplevel_rejected(self, ds):
        with pytest.raises(DriftScriptError, match="must not be quoted"):
            ds.compile_one('("believe" (inherit "A" "B"))')

    def test_quoted_variable_rejected(self, ds):
        with pytest.raises(DriftScriptError, match="must not be quoted"):
            ds.compile_one('(believe (inherit "$x" "bird"))')

    def test_quoted_operation_in_call_rejected(self, ds):
        with pytest.raises(DriftScriptError, match="must not be quoted"):
            ds.compile_one('(believe (predict (seq "a" (call "^press")) "b"))')

    def test_quoted_operation_in_def_op_rejected(self, ds):
        with pytest.raises(DriftScriptError, match="must not be quoted"):
            ds.compile_one('(def-op "^press")')

    def test_quoted_connector_rejected(self, ds):
        with pytest.raises(DriftScriptError, match="must not be quoted"):
            ds.compile_one('(believe ("seq" "a" "b"))')


# ---------------------------------------------------------------------------
# Validation (truth, dt, tense)
# ---------------------------------------------------------------------------

class TestValidation:
    def test_truth_non_numeric_freq(self, ds):
        with pytest.raises(DriftScriptError, match="frequency must be a number"):
            ds.compile_one('(believe (inherit "A" "B") :truth abc 0.9)')

    def test_truth_non_numeric_conf(self, ds):
        with pytest.raises(DriftScriptError, match="confidence must be a number"):
            ds.compile_one('(believe (inherit "A" "B") :truth 1.0 xyz)')

    def test_truth_freq_out_of_range(self, ds):
        with pytest.raises(DriftScriptError, match="frequency must be in"):
            ds.compile_one('(believe (inherit "A" "B") :truth 1.5 0.9)')

    def test_truth_conf_out_of_range(self, ds):
        with pytest.raises(DriftScriptError, match="confidence must be in"):
            ds.compile_one('(believe (inherit "A" "B") :truth 0.9 -0.1)')

    def test_truth_negative_freq(self, ds):
        with pytest.raises(DriftScriptError, match="frequency must be in"):
            ds.compile_one('(believe (inherit "A" "B") :truth -0.5 0.9)')

    def test_dt_non_numeric(self, ds):
        with pytest.raises(DriftScriptError, match="integer"):
            ds.compile_one('(believe (predict "A" "B") :now :dt abc)')

    def test_dt_float_rejected(self, ds):
        with pytest.raises(DriftScriptError, match="integer"):
            ds.compile_one('(believe (predict "A" "B") :now :dt 5.5)')

    def test_goal_past_tense(self, ds):
        with pytest.raises(DriftScriptError, match="Goals cannot use :past"):
            ds.compile_one('(goal "x" :past)')

    def test_goal_future_tense(self, ds):
        with pytest.raises(DriftScriptError, match="Goals cannot use :future"):
            ds.compile_one('(goal "x" :future)')

    def test_ask_truth_rejected(self, ds):
        with pytest.raises(DriftScriptError, match="Questions cannot have :truth"):
            ds.compile_one('(ask (inherit "A" "B") :truth 1.0 0.9)')
