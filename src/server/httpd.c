/*
 * httpd.c — Minimal HTTP server wrapping the DriftNARS engine.
 *
 * Accepts DriftScript (or raw Narsese) via POST and returns the engine
 * output as plain text.
 *
 * Endpoints:
 *   POST /driftscript   — compile & execute DriftScript source
 *   POST /narsese       — execute raw Narsese / shell commands (one per line)
 *   POST /reset         — reset the reasoner
 *   GET  /health        — liveness check
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
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "NAR.h"
#include "Shell.h"
#include "driftscript.h"

/* ── Limits ────────────────────────────────────────────────────────────────── */

#define REQ_BUF_SIZE   65536
#define RESP_BUF_SIZE  (1 << 20)   /* 1 MiB max captured output */
#define MAX_BODY_SIZE  65536

/* ── Output capture ────────────────────────────────────────────────────────── */
/*
 * The engine writes to stdout via printf/puts/fputs.  We redirect stdout to
 * an in-memory buffer during request handling so we can return the output in
 * the HTTP response body.
 */

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

    /* Drain the pipe into the memstream */
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
        "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
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

    /* Request line */
    const char *line_end = strstr(raw, "\r\n");
    if (!line_end) return -1;

    sscanf(raw, "%15s %255s", req->method, req->path);

    /* Find Content-Length */
    const char *cl = strcasestr(raw, "Content-Length:");
    if (cl) {
        cl += strlen("Content-Length:");
        while (*cl == ' ') cl++;
        req->content_length = (size_t)atol(cl);
    }

    /* Find body (after \r\n\r\n) */
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
    /* Work on a copy so we can mutate with strtok */
    size_t len = strlen(input);
    char *copy = malloc(len + 1);
    if (!copy) return;
    memcpy(copy, input, len + 1);

    char *saveptr;
    char *line = strtok_r(copy, "\n", &saveptr);
    while (line) {
        /* Trim leading whitespace */
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
        send_json(client_fd, 200, "OK", "{\"status\":\"reset\"}");
        return;
    }

    /* POST /driftscript */
    if (!strcmp(req->method, "POST") && !strcmp(req->path, "/driftscript")) {
        if (!req->body || req->content_length == 0) {
            send_text(client_fd, 400, "Bad Request", "Empty body\n");
            return;
        }

        /* Null-terminate the body */
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
    fprintf(stderr, "  POST /driftscript  — execute DriftScript\n");
    fprintf(stderr, "  POST /narsese      — execute Narsese (one statement per line)\n");
    fprintf(stderr, "  POST /reset        — reset the reasoner\n");
    fprintf(stderr, "  GET  /health       — liveness check\n");

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

        /* Read headers first, then body based on Content-Length */
        while (total < (ssize_t)sizeof(reqbuf) - 1) {
            n = read(client_fd, reqbuf + total, (size_t)(sizeof(reqbuf) - 1 - (size_t)total));
            if (n <= 0) break;
            total += n;
            reqbuf[total] = '\0';

            /* Check if we have full headers */
            char *hdr_end = strstr(reqbuf, "\r\n\r\n");
            if (hdr_end) {
                size_t hdr_size = (size_t)(hdr_end - reqbuf) + 4;
                /* Parse content length */
                const char *cl = strcasestr(reqbuf, "Content-Length:");
                size_t content_length = 0;
                if (cl) {
                    cl += strlen("Content-Length:");
                    while (*cl == ' ') cl++;
                    content_length = (size_t)atol(cl);
                }
                /* Check if we have the full body */
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
