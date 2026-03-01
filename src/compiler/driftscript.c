/*
 * driftscript.c — Standalone DriftScript-to-Narsese compiler.
 *
 * Usage:
 *   bin/driftscript < program.ds          # compile stdin, emit to stdout
 *   bin/driftscript --test                # run inline test suite
 *   bin/driftscript --help                # usage info
 *
 * Pure text transformer: DriftScript in, Narsese/shell-commands out.
 * Zero dependency on libdriftnars.
 *
 * Port of examples/python/driftscript.py
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

/* ── Limits ─────────────────────────────────────────────────────────────────── */

#define DS_TOKEN_MAX      1024
#define DS_SYMBOL_MAX     64
#define DS_NODE_MAX       2048
#define DS_CHILDREN_MAX   16
#define DS_OUTPUT_MAX     4096
#define DS_INPUT_MAX      65536
#define DS_LINE_MAX       4096

/* ── Error handling ─────────────────────────────────────────────────────────── */

static char ds_error_buf[1024];
static bool ds_has_error = false;

static void ds_error(const char *msg, int line, int col)
{
    if (line > 0)
        snprintf(ds_error_buf, sizeof(ds_error_buf), "%s (line %d, col %d)", msg, line, col);
    else
        snprintf(ds_error_buf, sizeof(ds_error_buf), "%s", msg);
    ds_has_error = true;
}

static void ds_clear_error(void)
{
    ds_error_buf[0] = '\0';
    ds_has_error = false;
}

/* ── Token types ────────────────────────────────────────────────────────────── */

typedef enum {
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_KEYWORD,
    TOK_SYMBOL,
    TOK_STRING
} TokenType;

typedef struct {
    TokenType type;
    char value[DS_SYMBOL_MAX];
    int line;
    int col;
} Token;

/* ── Tokenizer ──────────────────────────────────────────────────────────────── */

static int tokenize(const char *source, Token *tokens, int max_tokens)
{
    int ntok = 0;
    int lineno = 1;
    int i = 0;
    int len = (int)strlen(source);
    int line_start = 0;

    while (i < len && ntok < max_tokens) {
        char ch = source[i];

        /* Track line boundaries */
        if (ch == '\n') {
            lineno++;
            line_start = i + 1;
            i++;
            continue;
        }

        /* Whitespace */
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            i++;
            continue;
        }

        /* Comment — skip to end of line */
        if (ch == ';') {
            while (i < len && source[i] != '\n')
                i++;
            continue;
        }

        int col = i - line_start + 1;

        if (ch == '(') {
            tokens[ntok].type = TOK_LPAREN;
            tokens[ntok].value[0] = '(';
            tokens[ntok].value[1] = '\0';
            tokens[ntok].line = lineno;
            tokens[ntok].col = col;
            ntok++;
            i++;
        } else if (ch == ')') {
            tokens[ntok].type = TOK_RPAREN;
            tokens[ntok].value[0] = ')';
            tokens[ntok].value[1] = '\0';
            tokens[ntok].line = lineno;
            tokens[ntok].col = col;
            ntok++;
            i++;
        } else if (ch == ':') {
            /* Keyword token */
            int j = i + 1;
            while (j < len && source[j] != ' ' && source[j] != '\t' &&
                   source[j] != '\r' && source[j] != '\n' &&
                   source[j] != '(' && source[j] != ')' && source[j] != ';')
                j++;
            int slen = j - i;
            if (slen >= DS_SYMBOL_MAX) slen = DS_SYMBOL_MAX - 1;
            memcpy(tokens[ntok].value, &source[i], slen);
            tokens[ntok].value[slen] = '\0';
            tokens[ntok].type = TOK_KEYWORD;
            tokens[ntok].line = lineno;
            tokens[ntok].col = col;
            ntok++;
            i = j;
        } else if (ch == '"') {
            /* String literal */
            int j = i + 1;
            int ci = 0;
            char buf[DS_SYMBOL_MAX];
            while (j < len && source[j] != '\n') {
                char c = source[j];
                if (c == '\\') {
                    if (j + 1 >= len || source[j + 1] == '\n') {
                        ds_error("Unterminated string escape", lineno, j - line_start + 1);
                        return -1;
                    }
                    char nc = source[j + 1];
                    if (nc == '"') {
                        if (ci < DS_SYMBOL_MAX - 1) buf[ci++] = '"';
                    } else if (nc == '\\') {
                        if (ci < DS_SYMBOL_MAX - 1) buf[ci++] = '\\';
                    } else {
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Unknown escape: \\%c", nc);
                        ds_error(msg, lineno, j - line_start + 1);
                        return -1;
                    }
                    j += 2;
                } else if (c == '"') {
                    break;
                } else {
                    if (ci < DS_SYMBOL_MAX - 1) buf[ci++] = c;
                    j++;
                }
            }
            if (j >= len || source[j] != '"') {
                ds_error("Unterminated string literal", lineno, col);
                return -1;
            }
            buf[ci] = '\0';
            memcpy(tokens[ntok].value, buf, ci + 1);
            tokens[ntok].type = TOK_STRING;
            tokens[ntok].line = lineno;
            tokens[ntok].col = col;
            ntok++;
            i = j + 1; /* skip closing quote */
        } else {
            /* Symbol: atoms, ops, vars, numbers */
            int j = i;
            while (j < len && source[j] != ' ' && source[j] != '\t' &&
                   source[j] != '\r' && source[j] != '\n' &&
                   source[j] != '(' && source[j] != ')' &&
                   source[j] != ';' && source[j] != '"')
                j++;
            int slen = j - i;
            if (slen >= DS_SYMBOL_MAX) slen = DS_SYMBOL_MAX - 1;
            memcpy(tokens[ntok].value, &source[i], slen);
            tokens[ntok].value[slen] = '\0';
            tokens[ntok].type = TOK_SYMBOL;
            tokens[ntok].line = lineno;
            tokens[ntok].col = col;
            ntok++;
            i = j;
        }
    }
    return ntok;
}

/* ── AST nodes ──────────────────────────────────────────────────────────────── */

typedef enum {
    NODE_ATOM,
    NODE_LIST
} NodeType;

typedef struct {
    NodeType type;
    /* For NODE_ATOM: */
    char value[DS_SYMBOL_MAX];
    bool quoted;
    int line;
    int col;
    /* For NODE_LIST: */
    int children[DS_CHILDREN_MAX];
    int nchildren;
} Node;

/* Node pool */
static Node node_pool[DS_NODE_MAX];
static int node_count;

static void node_pool_reset(void)
{
    node_count = 0;
}

static int node_alloc(void)
{
    if (node_count >= DS_NODE_MAX) {
        ds_error("AST node pool exhausted", 0, 0);
        return -1;
    }
    int idx = node_count++;
    memset(&node_pool[idx], 0, sizeof(Node));
    return idx;
}

static int make_atom(const char *value, bool quoted, int line, int col)
{
    int idx = node_alloc();
    if (idx < 0) return -1;
    node_pool[idx].type = NODE_ATOM;
    strncpy(node_pool[idx].value, value, DS_SYMBOL_MAX - 1);
    node_pool[idx].value[DS_SYMBOL_MAX - 1] = '\0';
    node_pool[idx].quoted = quoted;
    node_pool[idx].line = line;
    node_pool[idx].col = col;
    return idx;
}

static int make_list(int line, int col)
{
    int idx = node_alloc();
    if (idx < 0) return -1;
    node_pool[idx].type = NODE_LIST;
    node_pool[idx].nchildren = 0;
    node_pool[idx].line = line;
    node_pool[idx].col = col;
    return idx;
}

static bool list_add_child(int list_idx, int child_idx)
{
    Node *n = &node_pool[list_idx];
    if (n->nchildren >= DS_CHILDREN_MAX) {
        ds_error("Too many children in list node", n->line, n->col);
        return false;
    }
    n->children[n->nchildren++] = child_idx;
    return true;
}

/* ── Parser ─────────────────────────────────────────────────────────────────── */

static int parse_pos;

static int parse_expr(const Token *tokens, int ntok)
{
    if (parse_pos >= ntok)
        return -1;

    const Token *tok = &tokens[parse_pos];
    if (tok->type == TOK_RPAREN) {
        ds_error("Unexpected ')'", tok->line, tok->col);
        return -1;
    }

    if (tok->type == TOK_LPAREN) {
        /* Parse list */
        int list = make_list(tok->line, tok->col);
        if (list < 0) return -1;
        parse_pos++; /* skip LPAREN */
        while (parse_pos < ntok && tokens[parse_pos].type != TOK_RPAREN) {
            int child = parse_expr(tokens, ntok);
            if (ds_has_error) return -1;
            if (child >= 0) {
                if (!list_add_child(list, child)) return -1;
            }
        }
        if (parse_pos >= ntok) {
            ds_error("Unclosed '('", tok->line, tok->col);
            return -1;
        }
        parse_pos++; /* skip RPAREN */
        return list;
    }

    /* SYMBOL, KEYWORD, or STRING */
    parse_pos++;
    bool quoted = (tok->type == TOK_STRING);
    return make_atom(tok->value, quoted, tok->line, tok->col);
}

/* Parse all top-level expressions, returning them as children of a root list node. */
static int parse_all(const Token *tokens, int ntok)
{
    parse_pos = 0;
    int root = make_list(1, 1);
    if (root < 0) return -1;

    while (parse_pos < ntok) {
        int node = parse_expr(tokens, ntok);
        if (ds_has_error) return -1;
        if (node >= 0) {
            if (!list_add_child(root, node)) return -1;
        }
    }
    return root;
}

/* ── Compiler ───────────────────────────────────────────────────────────────── */

/* Compile result kinds */
typedef enum {
    RES_NARSESE,
    RES_SHELL_COMMAND,
    RES_CYCLES,
    RES_DEF_OP
} ResultKind;

typedef struct {
    ResultKind kind;
    char value[DS_OUTPUT_MAX];
} CompileResult;

/* Copula lookup */
typedef struct { const char *name; const char *cop; } CopulaEntry;
static const CopulaEntry copulas[] = {
    {"inherit",  "-->"},
    {"similar",  "<->"},
    {"imply",    "==>"},
    {"predict",  "=/>"},
    {"equiv",    "<=>"},
    {"instance", "|->"},
    {NULL, NULL}
};

static const char *find_copula(const char *name)
{
    for (int i = 0; copulas[i].name; i++)
        if (strcmp(copulas[i].name, name) == 0)
            return copulas[i].cop;
    return NULL;
}

/* Connector lookup */
typedef enum {
    STYLE_INFIX,
    STYLE_UNARY,
    STYLE_PRODUCT,
    STYLE_EXT_SET,
    STYLE_INT_SET,
    STYLE_PREFIX
} ConnStyle;

typedef struct { const char *name; const char *op; ConnStyle style; } ConnEntry;
static const ConnEntry connectors[] = {
    {"seq",        "&/",  STYLE_INFIX},
    {"and",        "&&",  STYLE_INFIX},
    {"or",         "||",  STYLE_INFIX},
    {"not",        "--",  STYLE_UNARY},
    {"product",    "*",   STYLE_PRODUCT},
    {"ext-set",    NULL,  STYLE_EXT_SET},
    {"int-set",    NULL,  STYLE_INT_SET},
    {"ext-inter",  "&",   STYLE_PREFIX},
    {"int-inter",  "|",   STYLE_PREFIX},
    {"ext-diff",   "-",   STYLE_PREFIX},
    {"int-diff",   "~",   STYLE_PREFIX},
    {"ext-image1", "/1",  STYLE_PREFIX},
    {"ext-image2", "/2",  STYLE_PREFIX},
    {"int-image1", "\\1", STYLE_PREFIX},
    {"int-image2", "\\2", STYLE_PREFIX},
    {NULL, NULL, 0}
};

static const ConnEntry *find_connector(const char *name)
{
    for (int i = 0; connectors[i].name; i++)
        if (strcmp(connectors[i].name, name) == 0)
            return &connectors[i];
    return NULL;
}

/* Valid config keys */
static const char *valid_config_keys[] = {
    "volume", "motorbabbling", "decisionthreshold",
    "anticipationconfidence", "questionpriming", "babblingops",
    "similaritydistance", NULL
};

static bool is_valid_config_key(const char *key)
{
    for (int i = 0; valid_config_keys[i]; i++)
        if (strcmp(valid_config_keys[i], key) == 0)
            return true;
    return false;
}

/* Tense map */
static const char *tense_suffix(const char *kw)
{
    if (strcmp(kw, ":now") == 0) return " :|:";
    if (strcmp(kw, ":past") == 0) return " :\\:";
    if (strcmp(kw, ":future") == 0) return " :/:";
    return NULL;
}

/* ── Compiler state (per top-level form) ────────────────────────────────────── */

typedef struct {
    /* Variable renaming maps: name[i] -> numbered[i] */
    char ind_names[9][DS_SYMBOL_MAX];   /* $-variable names */
    char ind_mapped[9][DS_SYMBOL_MAX];  /* $1, $2, ... */
    int ind_count;
    bool ind_used[10]; /* 1..9 */

    char dep_names[9][DS_SYMBOL_MAX];   /* #-variable names */
    char dep_mapped[9][DS_SYMBOL_MAX];
    int dep_count;
    bool dep_used[10];

    char qry_names[9][DS_SYMBOL_MAX];   /* ?-variable names */
    char qry_mapped[9][DS_SYMBOL_MAX];
    int qry_count;
    bool qry_used[10];
} CompilerState;

static void compiler_init(CompilerState *cs)
{
    memset(cs, 0, sizeof(*cs));
}

/* Check if string is all digits */
static bool is_all_digits(const char *s)
{
    if (!*s) return false;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s)) return false;
    return true;
}

/* Check if string is a valid integer (possibly negative) */
static bool is_integer(const char *s)
{
    if (!*s) return false;
    if (*s == '-') s++;
    return is_all_digits(s);
}

/* Check if string is a valid float */
static bool is_float(const char *s)
{
    if (!*s) return false;
    char *end;
    (void)strtod(s, &end);
    return *end == '\0';
}

/* Pre-scan for reserved variables ($1, #2, ?3, etc.) */
static void scan_reserved_vars(CompilerState *cs, int node_idx)
{
    if (node_idx < 0) return;
    Node *n = &node_pool[node_idx];
    if (n->type == NODE_ATOM) {
        const char *v = n->value;
        if (strlen(v) >= 2 && (v[0] == '$' || v[0] == '#' || v[0] == '?') && is_all_digits(v + 1)) {
            int num = atoi(v + 1);
            if (num >= 1 && num <= 9) {
                if (v[0] == '$') cs->ind_used[num] = true;
                else if (v[0] == '#') cs->dep_used[num] = true;
                else cs->qry_used[num] = true;
            }
        }
    } else {
        for (int i = 0; i < n->nchildren; i++)
            scan_reserved_vars(cs, n->children[i]);
    }
}

/* Rename variable, e.g. $x -> $1 */
static const char *rename_var(CompilerState *cs, const char *name, int line, int col)
{
    char prefix = name[0];
    const char *rest = name + 1;

    /* Already numbered: pass through */
    if (is_all_digits(rest))
        return name;

    /* Pick the right map */
    char (*names)[DS_SYMBOL_MAX];
    char (*mapped)[DS_SYMBOL_MAX];
    int *count;
    bool *used;

    if (prefix == '$') {
        names = cs->ind_names; mapped = cs->ind_mapped;
        count = &cs->ind_count; used = cs->ind_used;
    } else if (prefix == '#') {
        names = cs->dep_names; mapped = cs->dep_mapped;
        count = &cs->dep_count; used = cs->dep_used;
    } else {
        names = cs->qry_names; mapped = cs->qry_mapped;
        count = &cs->qry_count; used = cs->qry_used;
    }

    /* Already mapped? */
    for (int i = 0; i < *count; i++)
        if (strcmp(names[i], name) == 0)
            return mapped[i];

    /* Find next available number */
    int n = 1;
    while (n <= 9 && used[n]) n++;
    if (n > 9) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Too many %c-variables (max 9)", prefix);
        ds_error(msg, line, col);
        return NULL;
    }
    used[n] = true;

    if (*count >= 9) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Too many %c-variables (max 9)", prefix);
        ds_error(msg, line, col);
        return NULL;
    }
    strncpy(names[*count], name, DS_SYMBOL_MAX - 1);
    snprintf(mapped[*count], DS_SYMBOL_MAX, "%c%d", prefix, n);
    const char *result = mapped[*count];
    (*count)++;
    return result;
}

/* ── Node helpers ───────────────────────────────────────────────────────────── */

static const char *head_value(int node_idx)
{
    if (node_idx < 0) return NULL;
    Node *n = &node_pool[node_idx];
    if (n->type != NODE_LIST || n->nchildren == 0) return NULL;
    Node *head = &node_pool[n->children[0]];
    if (head->type != NODE_ATOM) return NULL;
    return head->value;
}

static void node_loc(int node_idx, int *line, int *col)
{
    if (node_idx < 0) { *line = 0; *col = 0; return; }
    Node *n = &node_pool[node_idx];
    if (n->type == NODE_ATOM) {
        *line = n->line; *col = n->col;
    } else if (n->type == NODE_LIST && n->nchildren > 0) {
        node_loc(n->children[0], line, col);
    } else {
        *line = n->line; *col = n->col;
    }
}

static void node_error(const char *msg, int node_idx)
{
    int line, col;
    node_loc(node_idx, &line, &col);
    ds_error(msg, line, col);
}

/* ── Term compilation ───────────────────────────────────────────────────────── */

/* Forward declaration */
static bool compile_term(CompilerState *cs, int node_idx, char *out, int outsize);

static bool compile_atom(CompilerState *cs, int node_idx, char *out, int outsize)
{
    Node *n = &node_pool[node_idx];
    const char *v = n->value;

    /* Variable: $x, #y, ?z */
    if (strlen(v) >= 2 && (v[0] == '$' || v[0] == '#' || v[0] == '?')) {
        if (n->quoted) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Variables must not be quoted: %s not \"%s\"", v, v);
            ds_error(msg, n->line, n->col);
            return false;
        }
        const char *renamed = rename_var(cs, v, n->line, n->col);
        if (!renamed) return false;
        strncpy(out, renamed, outsize - 1);
        out[outsize - 1] = '\0';
        return true;
    }

    /* Operation: ^name */
    if (v[0] == '^') {
        if (n->quoted) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Operations must not be quoted: %s not \"%s\"", v, v);
            ds_error(msg, n->line, n->col);
            return false;
        }
        strncpy(out, v, outsize - 1);
        out[outsize - 1] = '\0';
        return true;
    }

    /* Regular atom — must be quoted */
    if (!n->quoted) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Atom '%s' must be a string literal: \"%s\"", v, v);
        ds_error(msg, n->line, n->col);
        return false;
    }

    strncpy(out, v, outsize - 1);
    out[outsize - 1] = '\0';
    return true;
}

static bool compile_copula(CompilerState *cs, const char *head, int node_idx,
                            int *args, int nargs, char *out, int outsize)
{
    if (nargs != 2) {
        char msg[128];
        snprintf(msg, sizeof(msg), "'%s' requires exactly 2 arguments, got %d", head, nargs);
        node_error(msg, node_idx);
        return false;
    }

    const char *cop = find_copula(head);
    char left[DS_OUTPUT_MAX], right[DS_OUTPUT_MAX];
    if (!compile_term(cs, args[0], left, sizeof(left))) return false;
    if (!compile_term(cs, args[1], right, sizeof(right))) return false;

    snprintf(out, outsize, "<%s %s %s>", left, cop, right);
    return true;
}

static bool compile_connector(CompilerState *cs, const char *head, int node_idx,
                                int *args, int nargs, char *out, int outsize)
{
    const ConnEntry *conn = find_connector(head);

    switch (conn->style) {
    case STYLE_UNARY: {
        if (nargs != 1) {
            char msg[128];
            snprintf(msg, sizeof(msg), "'%s' requires exactly 1 argument", head);
            node_error(msg, node_idx);
            return false;
        }
        char inner[DS_OUTPUT_MAX];
        if (!compile_term(cs, args[0], inner, sizeof(inner))) return false;
        snprintf(out, outsize, "(%s %s)", conn->op, inner);
        return true;
    }

    case STYLE_EXT_SET: {
        if (nargs < 1) {
            char msg[128];
            snprintf(msg, sizeof(msg), "'%s' requires at least 1 argument", head);
            node_error(msg, node_idx);
            return false;
        }
        int pos = 0;
        pos += snprintf(out + pos, outsize - pos, "{");
        for (int i = 0; i < nargs; i++) {
            char part[DS_OUTPUT_MAX];
            if (!compile_term(cs, args[i], part, sizeof(part))) return false;
            if (i > 0) pos += snprintf(out + pos, outsize - pos, ", ");
            pos += snprintf(out + pos, outsize - pos, "%s", part);
        }
        pos += snprintf(out + pos, outsize - pos, "}");
        (void)pos;
        return true;
    }

    case STYLE_INT_SET: {
        if (nargs < 1) {
            char msg[128];
            snprintf(msg, sizeof(msg), "'%s' requires at least 1 argument", head);
            node_error(msg, node_idx);
            return false;
        }
        int pos = 0;
        pos += snprintf(out + pos, outsize - pos, "[");
        for (int i = 0; i < nargs; i++) {
            char part[DS_OUTPUT_MAX];
            if (!compile_term(cs, args[i], part, sizeof(part))) return false;
            if (i > 0) pos += snprintf(out + pos, outsize - pos, ", ");
            pos += snprintf(out + pos, outsize - pos, "%s", part);
        }
        pos += snprintf(out + pos, outsize - pos, "]");
        (void)pos;
        return true;
    }

    case STYLE_PRODUCT: {
        if (nargs < 1) {
            char msg[128];
            snprintf(msg, sizeof(msg), "'%s' requires at least 1 argument", head);
            node_error(msg, node_idx);
            return false;
        }
        int pos = 0;
        pos += snprintf(out + pos, outsize - pos, "(*, ");
        for (int i = 0; i < nargs; i++) {
            char part[DS_OUTPUT_MAX];
            if (!compile_term(cs, args[i], part, sizeof(part))) return false;
            if (i > 0) pos += snprintf(out + pos, outsize - pos, ", ");
            pos += snprintf(out + pos, outsize - pos, "%s", part);
        }
        pos += snprintf(out + pos, outsize - pos, ")");
        (void)pos;
        return true;
    }

    case STYLE_INFIX: {
        if (strcmp(head, "seq") == 0) {
            if (nargs < 2 || nargs > 3) {
                char msg[128];
                snprintf(msg, sizeof(msg), "'seq' requires 2 or 3 arguments, got %d", nargs);
                node_error(msg, node_idx);
                return false;
            }
        } else {
            if (nargs != 2) {
                char msg[128];
                snprintf(msg, sizeof(msg), "'%s' requires exactly 2 arguments", head);
                node_error(msg, node_idx);
                return false;
            }
        }
        int pos = 0;
        pos += snprintf(out + pos, outsize - pos, "(");
        for (int i = 0; i < nargs; i++) {
            char part[DS_OUTPUT_MAX];
            if (!compile_term(cs, args[i], part, sizeof(part))) return false;
            if (i > 0) pos += snprintf(out + pos, outsize - pos, " %s ", conn->op);
            pos += snprintf(out + pos, outsize - pos, "%s", part);
        }
        pos += snprintf(out + pos, outsize - pos, ")");
        (void)pos;
        return true;
    }

    case STYLE_PREFIX: {
        if (nargs != 2) {
            char msg[128];
            snprintf(msg, sizeof(msg), "'%s' requires exactly 2 arguments", head);
            node_error(msg, node_idx);
            return false;
        }
        char left[DS_OUTPUT_MAX], right[DS_OUTPUT_MAX];
        if (!compile_term(cs, args[0], left, sizeof(left))) return false;
        if (!compile_term(cs, args[1], right, sizeof(right))) return false;
        snprintf(out, outsize, "(%s, %s, %s)", conn->op, left, right);
        return true;
    }
    }
    return false;
}

static bool compile_call(CompilerState *cs, int node_idx, int *args, int nargs,
                           char *out, int outsize)
{
    if (nargs < 1) {
        node_error("'call' requires at least an operation name", node_idx);
        return false;
    }

    Node *op_node = &node_pool[args[0]];
    if (op_node->type != NODE_ATOM || op_node->value[0] != '^') {
        node_error("'call' first argument must be an operation (^name)", args[0]);
        return false;
    }
    if (op_node->quoted) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Operations must not be quoted: %s", op_node->value);
        ds_error(msg, op_node->line, op_node->col);
        return false;
    }

    if (nargs == 1) {
        /* Bare operation */
        strncpy(out, op_node->value, outsize - 1);
        out[outsize - 1] = '\0';
        return true;
    }

    /* Build product and inheritance */
    int pos = 0;
    pos += snprintf(out + pos, outsize - pos, "<(*, ");
    for (int i = 1; i < nargs; i++) {
        char part[DS_OUTPUT_MAX];
        if (!compile_term(cs, args[i], part, sizeof(part))) return false;
        if (i > 1) pos += snprintf(out + pos, outsize - pos, ", ");
        pos += snprintf(out + pos, outsize - pos, "%s", part);
    }
    pos += snprintf(out + pos, outsize - pos, ") --> %s>", op_node->value);
    (void)pos;
    return true;
}

static bool compile_term(CompilerState *cs, int node_idx, char *out, int outsize)
{
    if (node_idx < 0) {
        ds_error("Empty term", 0, 0);
        return false;
    }

    Node *n = &node_pool[node_idx];
    if (n->type == NODE_ATOM) {
        return compile_atom(cs, node_idx, out, outsize);
    }

    /* It's a list */
    if (n->nchildren == 0) {
        node_error("Empty term", node_idx);
        return false;
    }

    const char *head = head_value(node_idx);
    if (!head) {
        node_error("Term list must start with a symbol", node_idx);
        return false;
    }

    Node *head_node = &node_pool[n->children[0]];
    if (head_node->quoted) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Term keyword must not be quoted: %s", head);
        ds_error(msg, head_node->line, head_node->col);
        return false;
    }

    int *args = &n->children[1];
    int nargs = n->nchildren - 1;

    if (find_copula(head))
        return compile_copula(cs, head, node_idx, args, nargs, out, outsize);
    if (find_connector(head))
        return compile_connector(cs, head, node_idx, args, nargs, out, outsize);
    if (strcmp(head, "call") == 0)
        return compile_call(cs, node_idx, args, nargs, out, outsize);

    char msg[128];
    snprintf(msg, sizeof(msg), "Unknown term form: '%s'", head);
    ds_error(msg, head_node->line, head_node->col);
    return false;
}

/* ── Top-level compilation ──────────────────────────────────────────────────── */

static bool compile_sentence(CompilerState *cs, int node_idx, CompileResult *result)
{
    Node *n = &node_pool[node_idx];
    const char *head = node_pool[n->children[0]].value;
    int nargs = n->nchildren - 1;

    if (nargs < 1) {
        char msg[128];
        snprintf(msg, sizeof(msg), "'%s' requires at least a term", head);
        node_error(msg, node_idx);
        return false;
    }

    int term_idx = n->children[1];

    /* Parse options */
    const char *tense = NULL;
    const char *truth_f = NULL;
    const char *truth_c = NULL;
    const char *dt_val = NULL;

    int i = 2;
    while (i < n->nchildren) {
        Node *opt = &node_pool[n->children[i]];
        if (opt->type == NODE_ATOM && (strcmp(opt->value, ":now") == 0 ||
            strcmp(opt->value, ":past") == 0 || strcmp(opt->value, ":future") == 0)) {
            if (strcmp(head, "goal") == 0 && (strcmp(opt->value, ":past") == 0 ||
                strcmp(opt->value, ":future") == 0)) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Goals cannot use %s tense", opt->value);
                ds_error(msg, opt->line, opt->col);
                return false;
            }
            tense = opt->value;
            i++;
        } else if (opt->type == NODE_ATOM && strcmp(opt->value, ":truth") == 0) {
            if (strcmp(head, "ask") == 0) {
                ds_error("Questions cannot have :truth values", opt->line, opt->col);
                return false;
            }
            if (i + 2 >= n->nchildren) {
                ds_error(":truth requires F and C values", opt->line, opt->col);
                return false;
            }
            Node *f_node = &node_pool[n->children[i + 1]];
            Node *c_node = &node_pool[n->children[i + 2]];
            if (f_node->type != NODE_ATOM || c_node->type != NODE_ATOM) {
                ds_error(":truth F C must be numbers", opt->line, opt->col);
                return false;
            }
            if (!is_float(f_node->value)) {
                char msg[128];
                snprintf(msg, sizeof(msg), ":truth frequency must be a number, got '%s'", f_node->value);
                ds_error(msg, f_node->line, f_node->col);
                return false;
            }
            if (!is_float(c_node->value)) {
                char msg[128];
                snprintf(msg, sizeof(msg), ":truth confidence must be a number, got '%s'", c_node->value);
                ds_error(msg, c_node->line, c_node->col);
                return false;
            }
            double f_val = strtod(f_node->value, NULL);
            double c_val = strtod(c_node->value, NULL);
            if (f_val < 0.0 || f_val > 1.0) {
                char msg[128];
                snprintf(msg, sizeof(msg), ":truth frequency must be in [0.0, 1.0], got %s", f_node->value);
                ds_error(msg, f_node->line, f_node->col);
                return false;
            }
            if (c_val < 0.0 || c_val > 1.0) {
                char msg[128];
                snprintf(msg, sizeof(msg), ":truth confidence must be in [0.0, 1.0], got %s", c_node->value);
                ds_error(msg, c_node->line, c_node->col);
                return false;
            }
            truth_f = f_node->value;
            truth_c = c_node->value;
            i += 3;
        } else if (opt->type == NODE_ATOM && strcmp(opt->value, ":dt") == 0) {
            if (i + 1 >= n->nchildren) {
                ds_error(":dt requires a value", opt->line, opt->col);
                return false;
            }
            Node *dt_node = &node_pool[n->children[i + 1]];
            if (dt_node->type != NODE_ATOM) {
                ds_error(":dt value must be a number", opt->line, opt->col);
                return false;
            }
            if (!is_integer(dt_node->value)) {
                char msg[128];
                snprintf(msg, sizeof(msg), ":dt value must be an integer, got '%s'", dt_node->value);
                ds_error(msg, dt_node->line, dt_node->col);
                return false;
            }
            dt_val = dt_node->value;
            i += 2;
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "Unknown option: '%s'", opt->value);
            ds_error(msg, opt->line, opt->col);
            return false;
        }
    }

    /* Compile the term */
    char term_str[DS_OUTPUT_MAX];
    if (!compile_term(cs, term_idx, term_str, sizeof(term_str)))
        return false;

    /* Punctuation */
    char punct;
    if (strcmp(head, "believe") == 0) punct = '.';
    else if (strcmp(head, "ask") == 0) punct = '?';
    else punct = '!';

    /* Tense suffix */
    const char *tense_str = "";
    if (strcmp(head, "goal") == 0) {
        tense_str = " :|:";
    } else if (tense) {
        tense_str = tense_suffix(tense);
    }

    /* Truth value suffix */
    char truth_str[64] = "";
    if (truth_f) {
        snprintf(truth_str, sizeof(truth_str), " {%s %s}", truth_f, truth_c);
    }

    /* dt prefix */
    char dt_str[32] = "";
    if (dt_val) {
        snprintf(dt_str, sizeof(dt_str), "dt=%s ", dt_val);
    }

    result->kind = RES_NARSESE;
    snprintf(result->value, DS_OUTPUT_MAX, "%s%s%c%s%s", dt_str, term_str, punct, tense_str, truth_str);
    return true;
}

static bool compile_cycles(int node_idx, CompileResult *result)
{
    Node *n = &node_pool[node_idx];
    if (n->nchildren != 2) {
        node_error("'cycles' requires exactly 1 argument (count)", node_idx);
        return false;
    }
    Node *count_node = &node_pool[n->children[1]];
    if (count_node->type != NODE_ATOM) {
        node_error("'cycles' argument must be a number", node_idx);
        return false;
    }
    if (!is_all_digits(count_node->value)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "'cycles' argument must be a number, got '%s'", count_node->value);
        ds_error(msg, count_node->line, count_node->col);
        return false;
    }
    result->kind = RES_CYCLES;
    strncpy(result->value, count_node->value, DS_OUTPUT_MAX - 1);
    result->value[DS_OUTPUT_MAX - 1] = '\0';
    return true;
}

static bool compile_def_op(int node_idx, CompileResult *result)
{
    Node *n = &node_pool[node_idx];
    if (n->nchildren != 2) {
        node_error("'def-op' requires exactly 1 argument (^name)", node_idx);
        return false;
    }
    Node *name_node = &node_pool[n->children[1]];
    if (name_node->type != NODE_ATOM || name_node->value[0] != '^') {
        node_error("'def-op' argument must be an operation name (^name)", n->children[1]);
        return false;
    }
    if (name_node->quoted) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Operations must not be quoted: %s", name_node->value);
        ds_error(msg, name_node->line, name_node->col);
        return false;
    }
    result->kind = RES_DEF_OP;
    strncpy(result->value, name_node->value, DS_OUTPUT_MAX - 1);
    result->value[DS_OUTPUT_MAX - 1] = '\0';
    return true;
}

static bool compile_config(int node_idx, CompileResult *result)
{
    Node *n = &node_pool[node_idx];
    if (n->nchildren != 3) {
        node_error("'config' requires exactly 2 arguments (key value)", node_idx);
        return false;
    }
    Node *key_node = &node_pool[n->children[1]];
    Node *val_node = &node_pool[n->children[2]];
    if (key_node->type != NODE_ATOM) {
        node_error("'config' key must be a symbol", node_idx);
        return false;
    }
    if (val_node->type != NODE_ATOM) {
        node_error("'config' value must be a symbol", node_idx);
        return false;
    }
    if (!is_valid_config_key(key_node->value)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Unknown config key: '%s'. Valid: anticipationconfidence, babblingops, decisionthreshold, motorbabbling, questionpriming, similaritydistance, volume", key_node->value);
        ds_error(msg, key_node->line, key_node->col);
        return false;
    }
    result->kind = RES_SHELL_COMMAND;
    snprintf(result->value, DS_OUTPUT_MAX, "*%s=%s", key_node->value, val_node->value);
    return true;
}

static bool compile_toplevel(int node_idx, CompileResult *result)
{
    Node *n = &node_pool[node_idx];

    if (n->type == NODE_ATOM) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Bare atom at top level: '%s'", n->value);
        ds_error(msg, n->line, n->col);
        return false;
    }

    const char *head = head_value(node_idx);
    if (!head) {
        node_error("Empty expression", node_idx);
        return false;
    }

    Node *head_node = &node_pool[n->children[0]];
    if (head_node->quoted) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Top-level keyword must not be quoted: %s", head);
        ds_error(msg, head_node->line, head_node->col);
        return false;
    }

    /* Init compiler state for this form */
    CompilerState cs;
    compiler_init(&cs);

    /* Pre-scan for reserved variable numbers */
    scan_reserved_vars(&cs, node_idx);

    if (strcmp(head, "believe") == 0 || strcmp(head, "ask") == 0 || strcmp(head, "goal") == 0) {
        return compile_sentence(&cs, node_idx, result);
    } else if (strcmp(head, "cycles") == 0) {
        return compile_cycles(node_idx, result);
    } else if (strcmp(head, "reset") == 0) {
        result->kind = RES_SHELL_COMMAND;
        strcpy(result->value, "*reset");
        return true;
    } else if (strcmp(head, "def-op") == 0) {
        return compile_def_op(node_idx, result);
    } else if (strcmp(head, "config") == 0) {
        return compile_config(node_idx, result);
    } else if (strcmp(head, "concurrent") == 0) {
        result->kind = RES_SHELL_COMMAND;
        strcpy(result->value, "*concurrent");
        return true;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "Unknown top-level form: '%s'", head);
    ds_error(msg, head_node->line, head_node->col);
    return false;
}

/* ── Compile full source ────────────────────────────────────────────────────── */

#define DS_RESULTS_MAX 256

static int compile_source(const char *source, CompileResult *results, int max_results)
{
    ds_clear_error();
    node_pool_reset();

    Token tokens[DS_TOKEN_MAX];
    int ntok = tokenize(source, tokens, DS_TOKEN_MAX);
    if (ntok < 0) return -1;

    int root = parse_all(tokens, ntok);
    if (ds_has_error) return -1;

    Node *root_node = &node_pool[root];
    int nresults = 0;

    for (int i = 0; i < root_node->nchildren && nresults < max_results; i++) {
        if (!compile_toplevel(root_node->children[i], &results[nresults]))
            return -1;
        nresults++;
    }
    return nresults;
}

/* ── Emit to stdout ─────────────────────────────────────────────────────────── */

static void emit_result(const CompileResult *r)
{
    switch (r->kind) {
    case RES_NARSESE:
    case RES_SHELL_COMMAND:
    case RES_CYCLES:
        puts(r->value);
        break;
    case RES_DEF_OP:
        printf("*register %s\n", r->value);
        break;
    }
}

/* ── Main loop: paren-balanced stdin reading ────────────────────────────────── */

static int run_compiler(void)
{
    char input[DS_INPUT_MAX];
    int input_len = 0;
    int paren_depth = 0;
    bool in_string = false;
    bool escape_next = false;
    char line_buf[DS_LINE_MAX];

    while (fgets(line_buf, sizeof(line_buf), stdin)) {
        int line_len = (int)strlen(line_buf);

        /* Accumulate into input buffer */
        if (input_len + line_len >= DS_INPUT_MAX - 1) {
            fprintf(stderr, "driftscript: input buffer overflow\n");
            return 1;
        }
        memcpy(&input[input_len], line_buf, line_len);
        input_len += line_len;
        input[input_len] = '\0';

        /* Track paren depth to know when we have complete forms */
        for (int i = 0; i < line_len; i++) {
            char ch = line_buf[i];
            if (escape_next) {
                escape_next = false;
                continue;
            }
            if (in_string) {
                if (ch == '\\') escape_next = true;
                else if (ch == '"') in_string = false;
                continue;
            }
            if (ch == ';') break; /* comment — rest of line is ignored */
            if (ch == '"') { in_string = true; continue; }
            if (ch == '(') paren_depth++;
            else if (ch == ')') paren_depth--;
        }

        /* When paren depth returns to 0, compile accumulated input */
        if (paren_depth <= 0 && input_len > 0) {
            /* Check if there's any non-whitespace content */
            bool has_content = false;
            for (int i = 0; i < input_len; i++) {
                if (!isspace((unsigned char)input[i])) { has_content = true; break; }
            }

            if (has_content) {
                CompileResult results[DS_RESULTS_MAX];
                int n = compile_source(input, results, DS_RESULTS_MAX);
                if (n < 0) {
                    fprintf(stderr, "driftscript: %s\n", ds_error_buf);
                    return 1;
                }
                for (int i = 0; i < n; i++)
                    emit_result(&results[i]);
                fflush(stdout);
            }

            input_len = 0;
            input[0] = '\0';
            paren_depth = 0;
            in_string = false;
            escape_next = false;
        }
    }

    /* Handle any remaining input (shouldn't normally happen with balanced parens) */
    if (input_len > 0) {
        bool has_content = false;
        for (int i = 0; i < input_len; i++) {
            if (!isspace((unsigned char)input[i])) { has_content = true; break; }
        }
        if (has_content) {
            CompileResult results[DS_RESULTS_MAX];
            int n = compile_source(input, results, DS_RESULTS_MAX);
            if (n < 0) {
                fprintf(stderr, "driftscript: %s\n", ds_error_buf);
                return 1;
            }
            for (int i = 0; i < n; i++)
                emit_result(&results[i]);
            fflush(stdout);
        }
    }

    return 0;
}

/* ── Test suite ─────────────────────────────────────────────────────────────── */

static int test_pass = 0;
static int test_fail = 0;

static void check_compile(const char *input, const char *expected, const char *test_name)
{
    CompileResult results[DS_RESULTS_MAX];
    int n = compile_source(input, results, DS_RESULTS_MAX);
    if (n < 0) {
        fprintf(stderr, "  FAIL [%s]: compile error: %s\n", test_name, ds_error_buf);
        test_fail++;
        return;
    }
    if (n != 1) {
        fprintf(stderr, "  FAIL [%s]: expected 1 result, got %d\n", test_name, n);
        test_fail++;
        return;
    }
    if (strcmp(results[0].value, expected) != 0) {
        fprintf(stderr, "  FAIL [%s]:\n    expected: %s\n    got:      %s\n", test_name, expected, results[0].value);
        test_fail++;
        return;
    }
    test_pass++;
}

static void check_compile_kind(const char *input, ResultKind expected_kind,
                                 const char *expected_value, const char *test_name)
{
    CompileResult results[DS_RESULTS_MAX];
    int n = compile_source(input, results, DS_RESULTS_MAX);
    if (n < 0) {
        fprintf(stderr, "  FAIL [%s]: compile error: %s\n", test_name, ds_error_buf);
        test_fail++;
        return;
    }
    if (n != 1) {
        fprintf(stderr, "  FAIL [%s]: expected 1 result, got %d\n", test_name, n);
        test_fail++;
        return;
    }
    if (results[0].kind != expected_kind) {
        fprintf(stderr, "  FAIL [%s]: wrong result kind (expected %d, got %d)\n",
                test_name, expected_kind, results[0].kind);
        test_fail++;
        return;
    }
    if (strcmp(results[0].value, expected_value) != 0) {
        fprintf(stderr, "  FAIL [%s]:\n    expected: %s\n    got:      %s\n",
                test_name, expected_value, results[0].value);
        test_fail++;
        return;
    }
    test_pass++;
}

static void check_error(const char *input, const char *expected_substr, const char *test_name)
{
    CompileResult results[DS_RESULTS_MAX];
    int n = compile_source(input, results, DS_RESULTS_MAX);
    if (n >= 0) {
        fprintf(stderr, "  FAIL [%s]: expected error but compiled successfully\n", test_name);
        test_fail++;
        return;
    }
    if (!strstr(ds_error_buf, expected_substr)) {
        fprintf(stderr, "  FAIL [%s]: error message mismatch\n    expected substr: %s\n    got: %s\n",
                test_name, expected_substr, ds_error_buf);
        test_fail++;
        return;
    }
    test_pass++;
}

static void check_multi(const char *input, int expected_count, const char **expected_values,
                           const char *test_name)
{
    CompileResult results[DS_RESULTS_MAX];
    int n = compile_source(input, results, DS_RESULTS_MAX);
    if (n < 0) {
        fprintf(stderr, "  FAIL [%s]: compile error: %s\n", test_name, ds_error_buf);
        test_fail++;
        return;
    }
    if (n != expected_count) {
        fprintf(stderr, "  FAIL [%s]: expected %d results, got %d\n", test_name, expected_count, n);
        test_fail++;
        return;
    }
    for (int i = 0; i < n; i++) {
        if (strcmp(results[i].value, expected_values[i]) != 0) {
            fprintf(stderr, "  FAIL [%s]: result %d mismatch\n    expected: %s\n    got:      %s\n",
                    test_name, i, expected_values[i], results[i].value);
            test_fail++;
            return;
        }
    }
    test_pass++;
}

static void check_contains(const char *input, const char *expected_substr, const char *test_name)
{
    CompileResult results[DS_RESULTS_MAX];
    int n = compile_source(input, results, DS_RESULTS_MAX);
    if (n < 0) {
        fprintf(stderr, "  FAIL [%s]: compile error: %s\n", test_name, ds_error_buf);
        test_fail++;
        return;
    }
    if (n != 1) {
        fprintf(stderr, "  FAIL [%s]: expected 1 result, got %d\n", test_name, n);
        test_fail++;
        return;
    }
    if (!strstr(results[0].value, expected_substr)) {
        fprintf(stderr, "  FAIL [%s]: output does not contain '%s'\n    got: %s\n",
                test_name, expected_substr, results[0].value);
        test_fail++;
        return;
    }
    test_pass++;
}

/* Tokenizer tests */
static void test_tokenizer(void)
{
    printf("  Tokenizer tests...\n");

    /* test_parens */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        node_pool_reset();
        int n = tokenize("(believe x)", tokens, DS_TOKEN_MAX);
        if (n == 4 && tokens[0].type == TOK_LPAREN && tokens[1].type == TOK_SYMBOL &&
            tokens[2].type == TOK_SYMBOL && tokens[3].type == TOK_RPAREN)
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_parens]\n"); test_fail++; }
    }

    /* test_keyword */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize(":now :truth :dt", tokens, DS_TOKEN_MAX);
        if (n == 3 && tokens[0].type == TOK_KEYWORD && tokens[1].type == TOK_KEYWORD &&
            tokens[2].type == TOK_KEYWORD &&
            strcmp(tokens[0].value, ":now") == 0 &&
            strcmp(tokens[1].value, ":truth") == 0 &&
            strcmp(tokens[2].value, ":dt") == 0)
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_keyword]\n"); test_fail++; }
    }

    /* test_comment_stripped */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize("(believe x) ; this is a comment", tokens, DS_TOKEN_MAX);
        if (n == 4) test_pass++;
        else { fprintf(stderr, "  FAIL [tok_comment]: got %d tokens\n", n); test_fail++; }
    }

    /* test_comment_full_line */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize("; just a comment\n(believe x)", tokens, DS_TOKEN_MAX);
        if (n == 4 && tokens[0].type == TOK_LPAREN)
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_comment_full]\n"); test_fail++; }
    }

    /* test_line_col */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize("(believe\n  x)", tokens, DS_TOKEN_MAX);
        if (n == 4 && tokens[0].line == 1 && tokens[0].col == 1 &&
            tokens[2].line == 2)
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_line_col]\n"); test_fail++; }
    }

    /* test_op_symbol */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize("^press", tokens, DS_TOKEN_MAX);
        if (n == 1 && tokens[0].type == TOK_SYMBOL && strcmp(tokens[0].value, "^press") == 0)
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_op]\n"); test_fail++; }
    }

    /* test_var_symbol */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize("$x #y ?z $1", tokens, DS_TOKEN_MAX);
        if (n == 4 && strcmp(tokens[0].value, "$x") == 0 &&
            strcmp(tokens[1].value, "#y") == 0 &&
            strcmp(tokens[2].value, "?z") == 0 &&
            strcmp(tokens[3].value, "$1") == 0)
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_var]\n"); test_fail++; }
    }

    /* test_empty */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n1 = tokenize("", tokens, DS_TOKEN_MAX);
        int n2 = tokenize("  \n  \n  ", tokens, DS_TOKEN_MAX);
        if (n1 == 0 && n2 == 0) test_pass++;
        else { fprintf(stderr, "  FAIL [tok_empty]\n"); test_fail++; }
    }

    /* test_basic_string */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize("\"hello\"", tokens, DS_TOKEN_MAX);
        if (n == 1 && tokens[0].type == TOK_STRING && strcmp(tokens[0].value, "hello") == 0)
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_basic_string]\n"); test_fail++; }
    }

    /* test_escaped_quote */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize("\"say \\\"hi\\\"\"", tokens, DS_TOKEN_MAX);
        if (n == 1 && tokens[0].type == TOK_STRING && strcmp(tokens[0].value, "say \"hi\"") == 0)
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_escaped_quote]: got '%s'\n", tokens[0].value); test_fail++; }
    }

    /* test_escaped_backslash */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize("\"a\\\\b\"", tokens, DS_TOKEN_MAX);
        if (n == 1 && tokens[0].type == TOK_STRING && strcmp(tokens[0].value, "a\\b") == 0)
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_escaped_backslash]\n"); test_fail++; }
    }

    /* test_unterminated_string */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize("\"hello", tokens, DS_TOKEN_MAX);
        if (n < 0 && strstr(ds_error_buf, "Unterminated string"))
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_unterminated]\n"); test_fail++; }
    }

    /* test_unknown_escape */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize("\"a\\nb\"", tokens, DS_TOKEN_MAX);
        if (n < 0 && strstr(ds_error_buf, "Unknown escape"))
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_unknown_escape]\n"); test_fail++; }
    }

    /* test_string_in_expression */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize("(believe \"bird\")", tokens, DS_TOKEN_MAX);
        if (n == 4 && tokens[2].type == TOK_STRING && strcmp(tokens[2].value, "bird") == 0)
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_string_in_expr]\n"); test_fail++; }
    }

    /* test_symbol_stops_at_quote */
    {
        Token tokens[DS_TOKEN_MAX];
        ds_clear_error();
        int n = tokenize("abc\"def\"", tokens, DS_TOKEN_MAX);
        if (n == 2 && tokens[0].type == TOK_SYMBOL && strcmp(tokens[0].value, "abc") == 0 &&
            tokens[1].type == TOK_STRING && strcmp(tokens[1].value, "def") == 0)
            test_pass++;
        else { fprintf(stderr, "  FAIL [tok_symbol_stops_at_quote]\n"); test_fail++; }
    }
}

/* Parser tests */
static void test_parser(void)
{
    printf("  Parser tests...\n");

    /* test_simple_atom */
    {
        ds_clear_error();
        node_pool_reset();
        Token tokens[DS_TOKEN_MAX];
        int ntok = tokenize("x", tokens, DS_TOKEN_MAX);
        int root = parse_all(tokens, ntok);
        Node *r = &node_pool[root];
        if (!ds_has_error && r->nchildren == 1 && node_pool[r->children[0]].type == NODE_ATOM &&
            strcmp(node_pool[r->children[0]].value, "x") == 0)
            test_pass++;
        else { fprintf(stderr, "  FAIL [parse_simple_atom]\n"); test_fail++; }
    }

    /* test_nested_list */
    {
        ds_clear_error();
        node_pool_reset();
        Token tokens[DS_TOKEN_MAX];
        int ntok = tokenize("(inherit (ext-set \"A\") \"B\")", tokens, DS_TOKEN_MAX);
        int root = parse_all(tokens, ntok);
        Node *r = &node_pool[root];
        if (!ds_has_error && r->nchildren == 1) {
            Node *list = &node_pool[r->children[0]];
            if (list->type == NODE_LIST && list->nchildren == 3)
                test_pass++;
            else { fprintf(stderr, "  FAIL [parse_nested_list]: %d children\n", list->nchildren); test_fail++; }
        } else { fprintf(stderr, "  FAIL [parse_nested_list]\n"); test_fail++; }
    }

    /* test_multiple_toplevel */
    {
        ds_clear_error();
        node_pool_reset();
        Token tokens[DS_TOKEN_MAX];
        int ntok = tokenize("(believe x) (ask y)", tokens, DS_TOKEN_MAX);
        int root = parse_all(tokens, ntok);
        Node *r = &node_pool[root];
        if (!ds_has_error && r->nchildren == 2)
            test_pass++;
        else { fprintf(stderr, "  FAIL [parse_multi_toplevel]\n"); test_fail++; }
    }

    /* test_unclosed_paren */
    {
        ds_clear_error();
        node_pool_reset();
        Token tokens[DS_TOKEN_MAX];
        int ntok = tokenize("(believe x", tokens, DS_TOKEN_MAX);
        (void)parse_all(tokens, ntok);
        if (ds_has_error && strstr(ds_error_buf, "Unclosed"))
            test_pass++;
        else { fprintf(stderr, "  FAIL [parse_unclosed]\n"); test_fail++; }
    }

    /* test_unexpected_rparen */
    {
        ds_clear_error();
        node_pool_reset();
        Token tokens[DS_TOKEN_MAX];
        int ntok = tokenize(")", tokens, DS_TOKEN_MAX);
        (void)parse_all(tokens, ntok);
        if (ds_has_error && strstr(ds_error_buf, "Unexpected"))
            test_pass++;
        else { fprintf(stderr, "  FAIL [parse_unexpected_rparen]\n"); test_fail++; }
    }

    /* test_string_parsed_as_quoted_atom */
    {
        ds_clear_error();
        node_pool_reset();
        Token tokens[DS_TOKEN_MAX];
        int ntok = tokenize("\"bird\"", tokens, DS_TOKEN_MAX);
        int root = parse_all(tokens, ntok);
        Node *r = &node_pool[root];
        if (!ds_has_error && r->nchildren == 1) {
            Node *atom = &node_pool[r->children[0]];
            if (atom->type == NODE_ATOM && atom->quoted && strcmp(atom->value, "bird") == 0)
                test_pass++;
            else { fprintf(stderr, "  FAIL [parse_string_quoted]\n"); test_fail++; }
        } else { fprintf(stderr, "  FAIL [parse_string_quoted]\n"); test_fail++; }
    }
}

/* Copula tests */
static void test_copulas(void)
{
    printf("  Copula tests...\n");
    check_compile("(believe (inherit \"bird\" \"animal\"))", "<bird --> animal>.", "cop_inherit");
    check_compile("(believe (similar \"cat\" \"dog\"))", "<cat <-> dog>.", "cop_similar");
    check_compile("(believe (imply \"A\" \"B\"))", "<A ==> B>.", "cop_imply");
    check_compile("(believe (predict \"A\" \"B\"))", "<A =/> B>.", "cop_predict");
    check_compile("(believe (equiv \"A\" \"B\"))", "<A <=> B>.", "cop_equiv");
    check_compile("(believe (instance \"A\" \"B\"))", "<A |-> B>.", "cop_instance");
    check_error("(believe (inherit \"A\"))", "2 arguments", "cop_wrong_arity");
    check_error("(believe (inherit \"A\" \"B\" \"C\"))", "2 arguments", "cop_too_many");
}

/* Connector tests */
static void test_connectors(void)
{
    printf("  Connector tests...\n");
    check_compile("(believe (predict (seq \"a\" \"b\") \"c\"))", "<(a &/ b) =/> c>.", "conn_seq_2");
    check_compile("(believe (predict (seq \"a\" \"b\" \"c\") \"d\"))", "<(a &/ b &/ c) =/> d>.", "conn_seq_3");
    check_error("(believe (seq \"a\"))", "2 or 3", "conn_seq_too_few");
    check_error("(believe (seq \"a\" \"b\" \"c\" \"d\"))", "2 or 3", "conn_seq_too_many");
    check_contains("(believe (and \"A\" \"B\"))", "(A && B)", "conn_and");
    check_contains("(believe (or \"A\" \"B\"))", "(A || B)", "conn_or");
    check_contains("(believe (not \"A\"))", "(-- A)", "conn_not");
    check_contains("(believe (inherit (product \"A\" \"B\" \"C\") \"rel\"))", "(*, A, B, C)", "conn_product");
    check_contains("(believe (inherit (ext-set \"SELF\") \"person\"))", "{SELF}", "conn_ext_set_single");
    check_contains("(believe (inherit (ext-set \"A\" \"B\") \"group\"))", "{A, B}", "conn_ext_set_multi");
    check_contains("(believe (inherit \"x\" (int-set \"bright\" \"loud\")))", "[bright, loud]", "conn_int_set");
    check_contains("(believe (inherit (ext-inter \"A\" \"B\") \"C\"))", "(&, A, B)", "conn_ext_inter");
    check_contains("(believe (inherit (int-inter \"A\" \"B\") \"C\"))", "(|, A, B)", "conn_int_inter");
    check_contains("(believe (inherit (ext-diff \"A\" \"B\") \"C\"))", "(-, A, B)", "conn_ext_diff");
    check_contains("(believe (inherit (int-diff \"A\" \"B\") \"C\"))", "(~, A, B)", "conn_int_diff");
    check_contains("(believe (inherit (ext-image1 \"R\" \"X\") \"Y\"))", "(/1, R, X)", "conn_ext_image1");
    check_contains("(believe (inherit (ext-image2 \"R\" \"X\") \"Y\"))", "(/2, R, X)", "conn_ext_image2");
    check_contains("(believe (inherit (int-image1 \"R\" \"X\") \"Y\"))", "(\\1, R, X)", "conn_int_image1");
    check_contains("(believe (inherit (int-image2 \"R\" \"X\") \"Y\"))", "(\\2, R, X)", "conn_int_image2");
    check_error("(believe (not \"A\" \"B\"))", "1 argument", "conn_not_wrong_arity");
}

/* Call shorthand tests */
static void test_call(void)
{
    printf("  Call tests...\n");
    check_contains("(believe (inherit (call ^goto (ext-set \"SELF\") \"park\") \"action\"))",
                   "<(*, {SELF}, park) --> ^goto>", "call_with_args");
    check_contains("(believe (predict (seq \"light_on\" (call ^press)) \"light_off\"))",
                   "^press", "call_no_args");
    check_contains("(believe (predict (seq \"a\" (call ^press)) \"b\"))",
                   "(a &/ ^press)", "call_bare_op");
    check_error("(believe (call))", "operation", "call_missing_op");
}

/* Sentence tests */
static void test_sentences(void)
{
    printf("  Sentence tests...\n");
    check_compile("(believe (inherit \"bird\" \"animal\"))", "<bird --> animal>.", "sent_believe_eternal");
    check_compile("(believe \"light_on\" :now)", "light_on. :|:", "sent_believe_now");
    check_compile("(believe (inherit \"bird\" \"animal\") :truth 1.0 0.9)",
                  "<bird --> animal>. {1.0 0.9}", "sent_believe_truth");
    check_compile("(believe \"light_on\" :now :truth 1.0 0.9)",
                  "light_on. :|: {1.0 0.9}", "sent_believe_now_truth");
    check_compile("(believe (predict \"a\" \"b\") :now :dt 5)",
                  "dt=5 <a =/> b>. :|:", "sent_believe_dt");
    check_compile("(ask (inherit \"robin\" \"animal\"))", "<robin --> animal>?", "sent_ask_eternal");
    check_compile("(ask (inherit \"robin\" \"animal\") :now)", "<robin --> animal>? :|:", "sent_ask_now");
    check_compile("(ask (inherit \"robin\" \"animal\") :past)", "<robin --> animal>? :\\:", "sent_ask_past");
    check_compile("(ask (inherit \"robin\" \"animal\") :future)", "<robin --> animal>? :/:", "sent_ask_future");
    check_compile("(goal \"light_off\")", "light_off! :|:", "sent_goal");
    check_compile("(goal \"light_off\" :truth 1.0 0.9)", "light_off! :|: {1.0 0.9}", "sent_goal_truth");
    check_compile("(believe \"light_on\" :now)", "light_on. :|:", "sent_bare_atom_believe");
}

/* Variable tests */
static void test_variables(void)
{
    printf("  Variable tests...\n");
    check_compile("(believe (imply (inherit $x \"bird\") (inherit $x \"animal\")))",
                  "<<$1 --> bird> ==> <$1 --> animal>>.", "var_named_to_numbered");
    check_compile("(believe (imply (inherit $1 \"bird\") (inherit $1 \"animal\")))",
                  "<<$1 --> bird> ==> <$1 --> animal>>.", "var_passthrough_numbered");

    /* test_multiple_vars */
    {
        CompileResult results[DS_RESULTS_MAX];
        int n = compile_source("(believe (imply (inherit $a $b) (similar $b $a)))", results, DS_RESULTS_MAX);
        if (n == 1 && strstr(results[0].value, "$1") && strstr(results[0].value, "$2")) {
            /* Count occurrences */
            int count1 = 0, count2 = 0;
            const char *p = results[0].value;
            while ((p = strstr(p, "$1")) != NULL) { count1++; p++; }
            p = results[0].value;
            while ((p = strstr(p, "$2")) != NULL) { count2++; p++; }
            if (count1 == 2 && count2 == 2) test_pass++;
            else { fprintf(stderr, "  FAIL [var_multiple_vars]: $1=%d, $2=%d\n", count1, count2); test_fail++; }
        } else { fprintf(stderr, "  FAIL [var_multiple_vars]\n"); test_fail++; }
    }

    check_contains("(believe (imply (inherit $x \"bird\") (inherit $x #y)))", "#1", "var_dependent");
    check_contains("(ask (inherit ?x \"animal\"))", "?1", "var_query");

    /* test_collision_avoidance */
    {
        CompileResult results[DS_RESULTS_MAX];
        int n = compile_source("(believe (imply (inherit $1 \"bird\") (inherit $x \"animal\")))",
                               results, DS_RESULTS_MAX);
        if (n == 1 && strstr(results[0].value, "$1") && strstr(results[0].value, "$2"))
            test_pass++;
        else { fprintf(stderr, "  FAIL [var_collision]: %s\n", n >= 1 ? results[0].value : "error"); test_fail++; }
    }
}

/* Directive tests */
static void test_directives(void)
{
    printf("  Directive tests...\n");
    check_compile_kind("(cycles 10)", RES_CYCLES, "10", "dir_cycles");
    check_compile_kind("(reset)", RES_SHELL_COMMAND, "*reset", "dir_reset");
    check_compile_kind("(def-op ^press)", RES_DEF_OP, "^press", "dir_def_op");
    check_compile_kind("(config volume 100)", RES_SHELL_COMMAND, "*volume=100", "dir_config");
    check_error("(config badkey 1)", "Unknown config", "dir_config_invalid");
    check_compile_kind("(concurrent)", RES_SHELL_COMMAND, "*concurrent", "dir_concurrent");
    check_error("(cycles abc)", "number", "dir_cycles_non_number");
}

/* Nested compound tests */
static void test_nested(void)
{
    printf("  Nested compound tests...\n");
    check_compile("(believe (predict (seq \"at_home\" (call ^goto (ext-set \"SELF\") \"park\")) \"at_park\"))",
                  "<(at_home &/ <(*, {SELF}, park) --> ^goto>) =/> at_park>.", "nested_predict_seq_call");
    check_compile("(believe (predict (seq \"light_on\" (call ^press)) \"light_off\"))",
                  "<(light_on &/ ^press) =/> light_off>.", "nested_predict_seq_bare_op");
    check_contains("(believe (imply (and (inherit $x \"bird\") (inherit $x \"flyer\")) (inherit $x \"animal\")))",
                   "==>", "nested_deep_imply");
    check_contains("(believe (imply (and (inherit $x \"bird\") (inherit $x \"flyer\")) (inherit $x \"animal\")))",
                   "&&", "nested_deep_and");
    check_contains("(believe (imply (and (inherit $x \"bird\") (inherit $x \"flyer\")) (inherit $x \"animal\")))",
                   "$1", "nested_deep_var");
}

/* Multi-statement tests */
static void test_multi(void)
{
    printf("  Multi-statement tests...\n");
    {
        const char *expected[] = {"<bird --> animal>.", "<robin --> bird>.", "<robin --> animal>?"};
        check_multi("(believe (inherit \"bird\" \"animal\"))\n"
                    "(believe (inherit \"robin\" \"bird\"))\n"
                    "(ask (inherit \"robin\" \"animal\"))",
                    3, expected, "multi_statement");
    }

    /* test_variable_scope_per_sentence */
    {
        CompileResult results[DS_RESULTS_MAX];
        int n = compile_source(
            "(believe (imply (inherit $x \"bird\") (inherit $x \"animal\")))\n"
            "(believe (imply (inherit $y \"fish\") (inherit $y \"swimmer\")))",
            results, DS_RESULTS_MAX);
        if (n == 2 && strstr(results[0].value, "$1") && strstr(results[1].value, "$1"))
            test_pass++;
        else { fprintf(stderr, "  FAIL [multi_var_scope]\n"); test_fail++; }
    }
}

/* Error tests */
static void test_errors(void)
{
    printf("  Error tests...\n");
    check_error("hello", "Bare atom", "err_bare_atom");
    check_error("(frobnicate x)", "Unknown top-level", "err_unknown_form");
    check_error("(believe (frobnicate \"A\" \"B\"))", "Unknown term", "err_unknown_term");
    check_error("(believe)", "requires", "err_empty_believe");
}

/* Quoting enforcement tests */
static void test_quoting(void)
{
    printf("  Quoting enforcement tests...\n");
    check_error("(believe (inherit bird \"animal\"))", "must be a string literal", "quot_bare_atom");
    check_error("(believe (\"inherit\" \"A\" \"B\"))", "must not be quoted", "quot_keyword");
    check_error("(\"believe\" (inherit \"A\" \"B\"))", "must not be quoted", "quot_toplevel");
    check_error("(believe (inherit \"$x\" \"bird\"))", "must not be quoted", "quot_variable");
    check_error("(believe (predict (seq \"a\" (call \"^press\")) \"b\"))", "must not be quoted", "quot_op_call");
    check_error("(def-op \"^press\")", "must not be quoted", "quot_op_def_op");
    check_error("(believe (\"seq\" \"a\" \"b\"))", "must not be quoted", "quot_connector");
}

/* Validation tests */
static void test_validation(void)
{
    printf("  Validation tests...\n");
    check_error("(believe (inherit \"A\" \"B\") :truth abc 0.9)", "frequency must be a number", "val_truth_freq_nan");
    check_error("(believe (inherit \"A\" \"B\") :truth 1.0 xyz)", "confidence must be a number", "val_truth_conf_nan");
    check_error("(believe (inherit \"A\" \"B\") :truth 1.5 0.9)", "frequency must be in", "val_truth_freq_range");
    check_error("(believe (inherit \"A\" \"B\") :truth 0.9 -0.1)", "confidence must be in", "val_truth_conf_range");
    check_error("(believe (inherit \"A\" \"B\") :truth -0.5 0.9)", "frequency must be in", "val_truth_neg_freq");
    check_error("(believe (predict \"A\" \"B\") :now :dt abc)", "integer", "val_dt_nan");
    check_error("(believe (predict \"A\" \"B\") :now :dt 5.5)", "integer", "val_dt_float");
    check_error("(goal \"x\" :past)", "Goals cannot use :past", "val_goal_past");
    check_error("(goal \"x\" :future)", "Goals cannot use :future", "val_goal_future");
    check_error("(ask (inherit \"A\" \"B\") :truth 1.0 0.9)", "Questions cannot have :truth", "val_ask_truth");
}

static int run_tests(void)
{
    printf("DriftScript C compiler — test suite\n");
    test_pass = 0;
    test_fail = 0;

    test_tokenizer();
    test_parser();
    test_copulas();
    test_connectors();
    test_call();
    test_sentences();
    test_variables();
    test_directives();
    test_nested();
    test_multi();
    test_errors();
    test_quoting();
    test_validation();

    printf("\n  %d passed, %d failed\n", test_pass, test_fail);
    return test_fail > 0 ? 1 : 0;
}

/* ── Usage ──────────────────────────────────────────────────────────────────── */

static void print_usage(void)
{
    puts("Usage: driftscript [OPTIONS]");
    puts("");
    puts("DriftScript-to-Narsese compiler. Reads DriftScript from stdin,");
    puts("writes Narsese and shell commands to stdout.");
    puts("");
    puts("Options:");
    puts("  --help    Show this help");
    puts("  --test    Run inline test suite");
    puts("");
    puts("Example:");
    puts("  echo '(believe (inherit \"bird\" \"animal\"))' | bin/driftscript");
    puts("  bin/driftscript < program.ds | bin/driftnars shell");
}

/* ── Entry point ────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[1], "--test") == 0) {
            return run_tests();
        }
        fprintf(stderr, "driftscript: unknown option '%s'\n", argv[1]);
        return 1;
    }

    return run_compiler();
}
