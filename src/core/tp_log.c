/* tp_log.c — logging, result strings, allocation, file IO. */
#define _POSIX_C_SOURCE 200809L
#include "tp_common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static tp_log_level g_level = TP_LOG_INFO;
static bool g_color = false;
static bool g_color_probed = false;

void tp_log_set_level(tp_log_level lvl) { g_level = lvl; }
tp_log_level tp_log_get_level(void) { return g_level; }
void tp_log_set_color(bool on) { g_color = on; g_color_probed = true; }

const char *tp_result_str(tp_result r)
{
    switch (r) {
    case TP_OK:              return "ok";
    case TP_ERR_ARGS:        return "bad arguments";
    case TP_ERR_PLATFORM:    return "platform error";
    case TP_ERR_GL:          return "GL error";
    case TP_ERR_IO:          return "I/O error";
    case TP_ERR_CAPS:        return "capability/profile violation";
    case TP_ERR_NOMEM:       return "out of memory";
    case TP_ERR_UNSUPPORTED: return "unsupported";
    }
    return "unknown error";
}

static const char *level_tag(tp_log_level l)
{
    switch (l) {
    case TP_LOG_TRACE: return "TRACE";
    case TP_LOG_DEBUG: return "DEBUG";
    case TP_LOG_INFO:  return "INFO ";
    case TP_LOG_WARN:  return "WARN ";
    case TP_LOG_ERROR: return "ERROR";
    default:           return "?????";
    }
}

static const char *level_color(tp_log_level l)
{
    switch (l) {
    case TP_LOG_TRACE: return "\033[90m";
    case TP_LOG_DEBUG: return "\033[36m";
    case TP_LOG_INFO:  return "\033[0m";
    case TP_LOG_WARN:  return "\033[33m";
    case TP_LOG_ERROR: return "\033[31m";
    default:           return "\033[0m";
    }
}

/* Monotonic seconds since first log call — cheaper to read than wall clock
 * when scanning a frame-time trace. */
static double log_elapsed(void)
{
    static struct timespec t0;
    static bool have_t0 = false;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!have_t0) { t0 = now; have_t0 = true; }
    return (double)(now.tv_sec - t0.tv_sec) +
           (double)(now.tv_nsec - t0.tv_nsec) * 1e-9;
}

void tp_logf(tp_log_level lvl, const char *file, int line, const char *fmt, ...)
{
    if (lvl < g_level) return;

    if (!g_color_probed) {
        g_color = isatty(STDERR_FILENO) ? true : false;
        g_color_probed = true;
    }

    const char *base = strrchr(file, '/');
    base = base ? base + 1 : file;

    FILE *out = stderr;
    if (g_color) fputs(level_color(lvl), out);
    fprintf(out, "[%8.3f] %s ", log_elapsed(), level_tag(lvl));

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);

    /* Source location is noise at INFO and above; keep it for the levels you
     * only turn on when something is actually wrong. */
    if (lvl <= TP_LOG_DEBUG || lvl >= TP_LOG_ERROR)
        fprintf(out, "  (%s:%d)", base, line);

    if (g_color) fputs("\033[0m", out);
    fputc('\n', out);
    fflush(out);
}

/* ---- allocation ------------------------------------------------------ */
void *tp_alloc(size_t n)
{
    if (n == 0) n = 1;
    void *p = calloc(1, n);
    if (!p) TP_ERROR("out of memory allocating %zu bytes", n);
    return p;
}

void *tp_realloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (!q && n) TP_ERROR("out of memory reallocating to %zu bytes", n);
    return q;
}

void tp_free(void *p) { free(p); }

/* ---- file IO --------------------------------------------------------- */
char *tp_read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { TP_ERROR("cannot open '%s'", path); return NULL; }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);

    char *buf = (char *)tp_alloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) *out_len = got;
    return buf;
}
