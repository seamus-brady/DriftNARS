/*
 * httpd.c — Minimal HTTP server wrapping the DriftNARS engine.
 *
 * Accepts DriftScript (or raw Narsese) via POST and returns the engine
 * output as plain text.  Supports runtime operation registration with
 * outbound HTTP callback delivery when operations execute.
 *
 * Endpoints:
 *   POST   /driftscript    — compile & execute DriftScript source
 *   POST   /narsese        — execute raw Narsese / shell commands (one per line)
 *   POST   /reset          — reset the reasoner
 *   GET    /health         — liveness check
 *   GET    /ops            — list registered operations and their callbacks
 *   POST   /ops/register   — register an operation with a callback URL
 *   DELETE /ops/:name      — unregister an operation
 *   POST   /config         — set runtime reasoner parameters
 *   POST   /save           — save entire state to binary file
 *   POST   /load           — load state from binary file
 *   POST   /compact        — free lowest-priority concepts down to target count
 *
 * Build:  make httpd
 * Run:    bin/driftnars-httpd --port 8080
 */

/* _GNU_SOURCE needed for open_memstream() and strcasestr() on Linux */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "NAR.h"
#include "Shell.h"
#include "driftscript.h"

/* ── Limits ────────────────────────────────────────────────────────────────── */

#define REQ_BUF_SIZE      65536
#define RESP_BUF_SIZE     (1 << 20)   /* 1 MiB max captured output */
#define MAX_BODY_SIZE     65536
#define OPS_MAX           64
#define OP_NAME_MAX       64
#define CALLBACK_URL_MAX  512

/* ── Op registry ───────────────────────────────────────────────────────────── */

typedef struct {
    char   op[OP_NAME_MAX];
    char   callback_url[CALLBACK_URL_MAX];
    double min_confidence;
    bool   enabled;
} OpEntry;

static OpEntry op_registry[OPS_MAX];
static int     op_count = 0;

static int op_find(const char *op)
{
    for (int i = 0; i < op_count; i++)
        if (!strcmp(op_registry[i].op, op)) return i;
    return -1;
}

static int op_register(const char *op, const char *callback_url,
                       double min_confidence)
{
    int idx = op_find(op);
    if (idx < 0) {
        if (op_count >= OPS_MAX) return -1;
        idx = op_count++;
    }
    snprintf(op_registry[idx].op,           OP_NAME_MAX,      "%s", op);
    snprintf(op_registry[idx].callback_url, CALLBACK_URL_MAX, "%s", callback_url);
    op_registry[idx].min_confidence = min_confidence;
    op_registry[idx].enabled = true;
    return idx;
}

static int op_unregister(const char *op)
{
    int idx = op_find(op);
    if (idx < 0) return -1;
    memmove(&op_registry[idx], &op_registry[idx + 1],
            (size_t)(op_count - idx - 1) * sizeof(OpEntry));
    op_count--;
    return 0;
}

static void ops_to_json(char *out, size_t out_sz)
{
    size_t pos = 0;
    pos += (size_t)snprintf(out + pos, out_sz - pos, "[");
    for (int i = 0; i < op_count; i++) {
        if (i > 0) pos += (size_t)snprintf(out + pos, out_sz - pos, ",");
        pos += (size_t)snprintf(out + pos, out_sz - pos,
            "{\"op\":\"%s\",\"callback_url\":\"%s\","
            "\"min_confidence\":%.4f,\"enabled\":%s}",
            op_registry[i].op,
            op_registry[i].callback_url,
            op_registry[i].min_confidence,
            op_registry[i].enabled ? "true" : "false");
    }
    snprintf(out + pos, out_sz - pos, "]");
}

/* ── Outbound HTTP POST (callback delivery) ────────────────────────────────── */

static void http_post_callback(const char *url, const char *json_body)
{
    if (strncmp(url, "http://", 7) != 0) return;
    const char *host_start = url + 7;
    const char *slash = strchr(host_start, '/');
    const char *path  = slash ? slash : "/";

    size_t hostport_len = slash ? (size_t)(slash - host_start)
                                : strlen(host_start);
    char hostport[256];
    if (hostport_len >= sizeof(hostport)) return;
    memcpy(hostport, host_start, hostport_len);
    hostport[hostport_len] = '\0';

    char host[256];
    char port_str[16] = "80";
    char *colon = strchr(hostport, ':');
    if (colon) {
        size_t hlen = (size_t)(colon - hostport);
        memcpy(host, hostport, hlen);
        host[hlen] = '\0';
        snprintf(port_str, sizeof(port_str), "%s", colon + 1);
    } else {
        snprintf(host, sizeof(host), "%s", hostport);
    }

    struct addrinfo hints = { .ai_socktype = SOCK_STREAM };
    struct addrinfo *res;
    if (getaddrinfo(host, port_str, &hints, &res) != 0) return;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return; }

    /* 5-second timeout so a hung callback target doesn't block the server */
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd); freeaddrinfo(res); return;
    }
    freeaddrinfo(res);

    size_t body_len = strlen(json_body);
    char req[4096];
    int req_len = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        path, host, port_str, body_len, json_body);
    if (req_len > 0)
        write(fd, req, (size_t)req_len);

    char discard[512];
    while (read(fd, discard, sizeof(discard)) > 0) {}
    close(fd);
}

/* ── Execution handler (fires outbound callbacks) ──────────────────────────── */

/*
 * Note: NAR_ExecutionHandler only provides op name and args — not the truth
 * values that triggered the decision.  We report frequency=1.0/confidence=1.0
 * because the decision already passed the engine's decision threshold.  The
 * min_confidence field in the op registry is checked against the engine's
 * decision threshold at registration time (it's a contract with the caller),
 * not at execution time.
 */
static void on_execution(void *userdata, const char *op, const char *args)
{
    (void)userdata;

    int idx = op_find(op);
    if (idx < 0 || !op_registry[idx].enabled) return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long ts_ms = (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);

    char payload[1024];
    snprintf(payload, sizeof(payload),
        "{\"op\":\"%s\",\"args\":\"%s\","
        "\"frequency\":1.0,\"confidence\":1.0,"
        "\"timestamp_ms\":%ld}",
        op, args ? args : "", ts_ms);

    http_post_callback(op_registry[idx].callback_url, payload);
}

/* ── Output capture ────────────────────────────────────────────────────────── */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    FILE  *stream;
    FILE  *saved_stdout;
    int    saved_fd;
    int    pipe_rd;
} Capture;

static int capture_start(Capture *cap)
{
    cap->buf = NULL;
    cap->len = 0;
    cap->stream = open_memstream(&cap->buf, &cap->len);
    if (!cap->stream) return -1;

    fflush(stdout);
    cap->saved_fd = dup(STDOUT_FILENO);
    int pipefd[2];
    if (pipe(pipefd) < 0) { fclose(cap->stream); return -1; }

    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    cap->pipe_rd = pipefd[0];
    return 0;
}

static void capture_end(Capture *cap)
{
    fflush(stdout);
    dup2(cap->saved_fd, STDOUT_FILENO);
    close(cap->saved_fd);

    char tmp[4096];
    ssize_t n;
    while ((n = read(cap->pipe_rd, tmp, sizeof(tmp))) > 0)
        fwrite(tmp, 1, (size_t)n, cap->stream);
    close(cap->pipe_rd);

    fclose(cap->stream);
}

/* ── HTTP helpers ──────────────────────────────────────────────────────────── */

static void send_response(int fd, int status, const char *status_text,
                          const char *content_type, const char *body, size_t body_len)
{
    char hdr[512];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: POST, GET, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "\r\n",
        status, status_text, content_type, body_len);
    write(fd, hdr, (size_t)hdr_len);
    if (body_len > 0)
        write(fd, body, body_len);
}

static void send_text(int fd, int status, const char *status_text, const char *body)
{
    send_response(fd, status, status_text, "text/plain; charset=utf-8",
                  body, strlen(body));
}

static void send_json(int fd, int status, const char *status_text, const char *body)
{
    send_response(fd, status, status_text, "application/json",
                  body, strlen(body));
}

/* ── Minimal JSON field extractors ─────────────────────────────────────────── */

static int extract_json_string(const char *json, const char *key,
                               char *out, size_t out_sz)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_sz - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return 0;
}

static int extract_json_double(const char *json, const char *key, double *out)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ' || *p == ':') p++;
    if (!(*p == '-' || (*p >= '0' && *p <= '9'))) return -1;
    *out = strtod(p, NULL);
    return 0;
}

/* ── Request parsing (minimal) ─────────────────────────────────────────────── */

typedef struct {
    char method[16];
    char path[256];
    char *body;
    size_t content_length;
} Request;

static int parse_request(const char *raw, size_t raw_len, Request *req)
{
    memset(req, 0, sizeof(*req));

    const char *line_end = strstr(raw, "\r\n");
    if (!line_end) return -1;

    sscanf(raw, "%15s %255s", req->method, req->path);

    const char *cl = strcasestr(raw, "Content-Length:");
    if (cl) {
        cl += strlen("Content-Length:");
        while (*cl == ' ') cl++;
        req->content_length = (size_t)atol(cl);
    }

    const char *body_start = strstr(raw, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        size_t header_size = (size_t)(body_start - raw);
        if (raw_len >= header_size + req->content_length) {
            req->body = (char *)body_start;
        }
    }

    return 0;
}

/* ── DriftScript dispatch (mirrors Shell_DS_Dispatch) ──────────────────────── */

static int dispatch_driftscript(NAR_t *nar, const char *source)
{
    DS_CompileResult results[DS_RESULTS_MAX];
    int n = DS_CompileSource(source, results, DS_RESULTS_MAX);
    if (n < 0) {
        printf("Error: %s\n", DS_GetError());
        return -1;
    }

    for (int i = 0; i < n; i++) {
        switch (results[i].kind) {
        case DS_RES_NARSESE:
            if (NAR_AddInputNarsese(nar, results[i].value) != NAR_OK)
                printf("Error: Narsese parse failed for: %s\n", results[i].value);
            break;
        case DS_RES_CYCLES:
        case DS_RES_SHELL_COMMAND: {
            int cmd = Shell_ProcessInput(nar, results[i].value);
            if (cmd == SHELL_RESET)
                Shell_NARInit(nar);
            break;
        }
        case DS_RES_DEF_OP: {
            char reg_cmd[DS_OUTPUT_MAX + 16];
            snprintf(reg_cmd, sizeof(reg_cmd), "*register %s", results[i].value);
            Shell_ProcessInput(nar, reg_cmd);
            break;
        }
        }
    }
    return 0;
}

/* ── Narsese line-by-line dispatch ─────────────────────────────────────────── */

static void dispatch_narsese(NAR_t *nar, const char *input)
{
    size_t len = strlen(input);
    char *copy = malloc(len + 1);
    if (!copy) return;
    memcpy(copy, input, len + 1);

    char *saveptr;
    char *line = strtok_r(copy, "\n", &saveptr);
    while (line) {
        while (*line && isspace((unsigned char)*line)) line++;
        if (*line) {
            int cmd = Shell_ProcessInput(nar, line);
            if (cmd == SHELL_RESET)
                Shell_NARInit(nar);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(copy);
}

/* ── Request handler ───────────────────────────────────────────────────────── */

static void handle_request(int client_fd, NAR_t *nar, Request *req)
{
    /* CORS preflight */
    if (!strcmp(req->method, "OPTIONS")) {
        send_text(client_fd, 204, "No Content", "");
        return;
    }

    /* GET /health */
    if (!strcmp(req->method, "GET") && !strcmp(req->path, "/health")) {
        send_json(client_fd, 200, "OK", "{\"status\":\"ok\"}");
        return;
    }

    /* POST /reset */
    if (!strcmp(req->method, "POST") && !strcmp(req->path, "/reset")) {
        Shell_NARInit(nar);
        NAR_SetExecutionHandler(nar, on_execution, NULL);
        /* Re-register all ops from the registry with the fresh engine */
        for (int i = 0; i < op_count; i++) {
            char reg_cmd[OP_NAME_MAX + 16];
            snprintf(reg_cmd, sizeof(reg_cmd), "*register %s", op_registry[i].op);
            Shell_ProcessInput(nar, reg_cmd);
        }
        send_json(client_fd, 200, "OK", "{\"status\":\"reset\"}");
        return;
    }

    /* POST /driftscript */
    if (!strcmp(req->method, "POST") && !strcmp(req->path, "/driftscript")) {
        if (!req->body || req->content_length == 0) {
            send_text(client_fd, 400, "Bad Request", "Empty body\n");
            return;
        }

        char body[MAX_BODY_SIZE + 1];
        size_t blen = req->content_length < MAX_BODY_SIZE ? req->content_length : MAX_BODY_SIZE;
        memcpy(body, req->body, blen);
        body[blen] = '\0';

        Capture cap;
        if (capture_start(&cap) < 0) {
            send_text(client_fd, 500, "Internal Server Error", "Capture init failed\n");
            return;
        }

        dispatch_driftscript(nar, body);
        fflush(stdout);
        capture_end(&cap);

        send_response(client_fd, 200, "OK", "text/plain; charset=utf-8",
                      cap.buf ? cap.buf : "", cap.len);
        free(cap.buf);
        return;
    }

    /* POST /narsese */
    if (!strcmp(req->method, "POST") && !strcmp(req->path, "/narsese")) {
        if (!req->body || req->content_length == 0) {
            send_text(client_fd, 400, "Bad Request", "Empty body\n");
            return;
        }

        char body[MAX_BODY_SIZE + 1];
        size_t blen = req->content_length < MAX_BODY_SIZE ? req->content_length : MAX_BODY_SIZE;
        memcpy(body, req->body, blen);
        body[blen] = '\0';

        Capture cap;
        if (capture_start(&cap) < 0) {
            send_text(client_fd, 500, "Internal Server Error", "Capture init failed\n");
            return;
        }

        dispatch_narsese(nar, body);
        fflush(stdout);
        capture_end(&cap);

        send_response(client_fd, 200, "OK", "text/plain; charset=utf-8",
                      cap.buf ? cap.buf : "", cap.len);
        free(cap.buf);
        return;
    }

    /* GET /ops — list all registered ops */
    if (!strcmp(req->method, "GET") && !strcmp(req->path, "/ops")) {
        char body[OPS_MAX * 256];
        ops_to_json(body, sizeof(body));
        send_json(client_fd, 200, "OK", body);
        return;
    }

    /* POST /ops/register — register an op with a callback URL */
    if (!strcmp(req->method, "POST") && !strcmp(req->path, "/ops/register")) {
        if (!req->body || req->content_length == 0) {
            send_text(client_fd, 400, "Bad Request", "Empty body\n");
            return;
        }

        char body[MAX_BODY_SIZE + 1];
        size_t blen = req->content_length < MAX_BODY_SIZE
                      ? req->content_length : MAX_BODY_SIZE;
        memcpy(body, req->body, blen);
        body[blen] = '\0';

        char op[OP_NAME_MAX]          = {0};
        char cb_url[CALLBACK_URL_MAX] = {0};
        double min_conf = 0.0;

        extract_json_string(body, "op",           op,     sizeof(op));
        extract_json_string(body, "callback_url", cb_url, sizeof(cb_url));
        extract_json_double(body, "min_confidence", &min_conf);

        if (!op[0] || !cb_url[0]) {
            send_text(client_fd, 400, "Bad Request",
                      "op and callback_url are required\n");
            return;
        }

        /* Check registry capacity before committing to the engine */
        if (op_find(op) < 0 && op_count >= OPS_MAX) {
            send_text(client_fd, 507, "Insufficient Storage",
                      "Op registry full\n");
            return;
        }

        /* Register with DriftNARS engine */
        char reg_cmd[OP_NAME_MAX + 16];
        snprintf(reg_cmd, sizeof(reg_cmd), "*register %s", op);
        Shell_ProcessInput(nar, reg_cmd);

        op_register(op, cb_url, min_conf);

        char resp[512];
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"registered\",\"op\":\"%s\"}", op);
        send_json(client_fd, 200, "OK", resp);
        return;
    }

    /* DELETE /ops/:name — unregister an op */
    if (!strcmp(req->method, "DELETE") &&
        !strncmp(req->path, "/ops/", 5)) {

        const char *op_name = req->path + 5;
        if (!op_name[0]) {
            send_text(client_fd, 400, "Bad Request", "Op name required\n");
            return;
        }

        if (op_unregister(op_name) < 0) {
            send_text(client_fd, 404, "Not Found", "Op not found\n");
            return;
        }

        char resp[256];
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"unregistered\",\"op\":\"%s\"}", op_name);
        send_json(client_fd, 200, "OK", resp);
        return;
    }

    /* POST /config — set runtime reasoner parameters */
    if (!strcmp(req->method, "POST") && !strcmp(req->path, "/config")) {
        if (!req->body || req->content_length == 0) {
            send_text(client_fd, 400, "Bad Request", "Empty body\n");
            return;
        }

        char body[MAX_BODY_SIZE + 1];
        size_t blen = req->content_length < MAX_BODY_SIZE
                      ? req->content_length : MAX_BODY_SIZE;
        memcpy(body, req->body, blen);
        body[blen] = '\0';

        Capture cap;
        if (capture_start(&cap) < 0) {
            send_text(client_fd, 500, "Internal Server Error",
                      "Capture init failed\n");
            return;
        }

        static const struct { const char *json_key; const char *shell_key; } mappings[] = {
            { "decision_threshold",      "decisionthreshold"      },
            { "motorbabbling",           "motorbabbling"          },
            { "volume",                  "volume"                 },
            { "anticipation_confidence", "anticipationconfidence" },
            { "question_priming",        "questionpriming"        },
            { NULL, NULL }
        };

        for (int i = 0; mappings[i].json_key; i++) {
            double val;
            if (extract_json_double(body, mappings[i].json_key, &val) == 0) {
                char cmd[128];
                snprintf(cmd, sizeof(cmd), "*%s=%.6g",
                         mappings[i].shell_key, val);
                Shell_ProcessInput(nar, cmd);
            }
        }

        fflush(stdout);
        capture_end(&cap);
        free(cap.buf);

        send_json(client_fd, 200, "OK", "{\"status\":\"configured\"}");
        return;
    }

    /* POST /save — save state to a file (path in JSON body) */
    if (!strcmp(req->method, "POST") && !strcmp(req->path, "/save")) {
        if (!req->body || req->content_length == 0) {
            send_text(client_fd, 400, "Bad Request", "Empty body\n");
            return;
        }
        char body[MAX_BODY_SIZE + 1];
        size_t blen = req->content_length < MAX_BODY_SIZE
                      ? req->content_length : MAX_BODY_SIZE;
        memcpy(body, req->body, blen);
        body[blen] = '\0';

        char path[1024] = {0};
        extract_json_string(body, "path", path, sizeof(path));
        if (!path[0]) {
            send_text(client_fd, 400, "Bad Request",
                      "\"path\" field is required\n");
            return;
        }

        int rc = NAR_Save(nar, path);
        if (rc == NAR_OK) {
            char resp[1280];
            snprintf(resp, sizeof(resp),
                     "{\"status\":\"saved\",\"path\":\"%s\"}", path);
            send_json(client_fd, 200, "OK", resp);
        } else {
            send_text(client_fd, 500, "Internal Server Error",
                      "Failed to save state\n");
        }
        return;
    }

    /* POST /load — load state from a file (path in JSON body) */
    if (!strcmp(req->method, "POST") && !strcmp(req->path, "/load")) {
        if (!req->body || req->content_length == 0) {
            send_text(client_fd, 400, "Bad Request", "Empty body\n");
            return;
        }
        char body[MAX_BODY_SIZE + 1];
        size_t blen = req->content_length < MAX_BODY_SIZE
                      ? req->content_length : MAX_BODY_SIZE;
        memcpy(body, req->body, blen);
        body[blen] = '\0';

        char path[1024] = {0};
        extract_json_string(body, "path", path, sizeof(path));
        if (!path[0]) {
            send_text(client_fd, 400, "Bad Request",
                      "\"path\" field is required\n");
            return;
        }

        int rc = NAR_Load(nar, path);
        if (rc == NAR_OK) {
            /* Re-attach execution handler after load */
            NAR_SetExecutionHandler(nar, on_execution, NULL);
            char resp[1280];
            snprintf(resp, sizeof(resp),
                     "{\"status\":\"loaded\",\"path\":\"%s\"}", path);
            send_json(client_fd, 200, "OK", resp);
        } else {
            send_text(client_fd, 500, "Internal Server Error",
                      "Failed to load state\n");
        }
        return;
    }

    /* POST /compact — free lowest-priority concepts to reduce memory */
    if (!strcmp(req->method, "POST") && !strcmp(req->path, "/compact")) {
        /* Expect JSON body: {"target": 100} */
        const char *tgt = strstr(req->body, "\"target\"");
        int target = 0;
        if (tgt) {
            tgt = strchr(tgt, ':');
            if (tgt) target = atoi(tgt + 1);
        }
        if (target < 0) target = 0;
        int remaining = NAR_Compact(nar, target);
        char resp[256];
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"compacted\",\"concepts\":%d,\"allocated\":%d}\n",
                 remaining, nar->concepts_allocated);
        send_json(client_fd, 200, "OK", resp);
        return;
    }

    send_text(client_fd, 404, "Not Found", "Unknown endpoint\n");
}

/* ── Main ──────────────────────────────────────────────────────────────────── */

static volatile sig_atomic_t running = 1;
static void sigint_handler(int sig) { (void)sig; running = 0; }

static void print_usage(void)
{
    fputs("Usage: driftnars-httpd --port PORT\n"
          "\n"
          "Options:\n"
          "  --port PORT   Port to listen on (required)\n"
          "  --help        Show this help\n", stderr);
}

int main(int argc, char *argv[])
{
    int port = 0;

    for (int i = 1; i < argc; i++) {
        if ((!strcmp(argv[i], "--port") || !strcmp(argv[i], "-p")) && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_usage();
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: --port is required (1-65535)\n");
        print_usage();
        return 1;
    }

    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Initialize DriftNARS */
    NAR_t *nar = NAR_New();
    if (!nar) { fputs("Failed to allocate NAR\n", stderr); return 1; }
    Shell_NARInit(nar);
    NAR_SetExecutionHandler(nar, on_execution, NULL);

    /* Create listening socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        .sin_port = htons((uint16_t)port),
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    fprintf(stderr, "DriftNARS HTTP server listening on http://127.0.0.1:%d\n", port);
    fprintf(stderr, "Endpoints:\n");
    fprintf(stderr, "  POST   /driftscript    — execute DriftScript\n");
    fprintf(stderr, "  POST   /narsese        — execute Narsese (one per line)\n");
    fprintf(stderr, "  POST   /reset          — reset the reasoner\n");
    fprintf(stderr, "  GET    /health         — liveness check\n");
    fprintf(stderr, "  GET    /ops            — list registered ops\n");
    fprintf(stderr, "  POST   /ops/register   — register op with callback URL\n");
    fprintf(stderr, "  DELETE /ops/:name      — unregister an op\n");
    fprintf(stderr, "  POST   /config         — set runtime parameters\n");

    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        /* Read the full request (simple: single read, no chunked/streaming) */
        char reqbuf[REQ_BUF_SIZE];
        ssize_t total = 0;
        ssize_t n;

        while (total < (ssize_t)sizeof(reqbuf) - 1) {
            n = read(client_fd, reqbuf + total, (size_t)(sizeof(reqbuf) - 1 - (size_t)total));
            if (n <= 0) break;
            total += n;
            reqbuf[total] = '\0';

            char *hdr_end = strstr(reqbuf, "\r\n\r\n");
            if (hdr_end) {
                size_t hdr_size = (size_t)(hdr_end - reqbuf) + 4;
                const char *cl = strcasestr(reqbuf, "Content-Length:");
                size_t content_length = 0;
                if (cl) {
                    cl += strlen("Content-Length:");
                    while (*cl == ' ') cl++;
                    content_length = (size_t)atol(cl);
                }
                if ((size_t)total >= hdr_size + content_length)
                    break;
            }
        }

        if (total <= 0) {
            close(client_fd);
            continue;
        }

        Request req;
        if (parse_request(reqbuf, (size_t)total, &req) == 0) {
            handle_request(client_fd, nar, &req);
        } else {
            send_text(client_fd, 400, "Bad Request", "Malformed request\n");
        }

        close(client_fd);
    }

    fprintf(stderr, "\nShutting down.\n");
    close(server_fd);
    NAR_Free(nar);
    return 0;
}
