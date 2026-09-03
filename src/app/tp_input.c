/* tp_input.c — event source manager, command grammar, stdin source. */
#define _POSIX_C_SOURCE 200809L
#include "tp_input.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *tp_event_kind_str(tp_event_kind k)
{
    switch (k) {
    case TP_EVENT_NONE:       return "none";
    case TP_EVENT_QUIT:       return "quit";
    case TP_EVENT_PLAY:       return "play";
    case TP_EVENT_EXPRESSION: return "expr";
    case TP_EVENT_LOOK:       return "look";
    case TP_EVENT_SPEED:      return "speed";
    case TP_EVENT_PAUSE:      return "pause";
    case TP_EVENT_STATUS:     return "status";
    case TP_EVENT_STATS:      return "stats";
    case TP_EVENT_SHOT:       return "shot";
    case TP_EVENT_RELOAD:     return "reload";
    }
    return "?";
}

const char *tp_command_help(void)
{
    return
        "commands:\n"
        "  play <clip> [fade]     cross-fade to a clip (fade seconds, default 0.25)\n"
        "  set expr <name>        set the expression layer\n"
        "  look <yaw> <pitch> [w] look direction, each -1..1\n"
        "  speed <rate>           playback rate, 0..8\n"
        "  pause | resume         freeze or resume animation\n"
        "  status                 current state\n"
        "  stats                  frame-time percentiles\n"
        "  shot [path]            write a PNG of the next frame\n"
        "  reload                 reload shaders and assets\n"
        "  quit                   shut down cleanly\n"
        "  help                   this text\n";
}

/* Split off the next whitespace-delimited token, advancing *p. */
static char *next_token(char **p)
{
    char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) { *p = s; return NULL; }

    char *start = s;
    while (*s && *s != ' ' && *s != '\t') s++;
    if (*s) { *s = '\0'; s++; }
    *p = s;
    return start;
}

bool tp_command_parse(const char *line, tp_event *out, char *err, size_t errlen)
{
    memset(out, 0, sizeof(*out));
    out->reply_fd = -1;

    char buf[256];
    snprintf(buf, sizeof(buf), "%s", line);

    /* Trim trailing CR/LF/space so a client using \r\n works too. */
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                 buf[n - 1] == ' '  || buf[n - 1] == '\t'))
        buf[--n] = '\0';

    char *p = buf;
    char *cmd = next_token(&p);
    if (!cmd || !*cmd || *cmd == '#') { out->kind = TP_EVENT_NONE; return true; }

    if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit") || !strcmp(cmd, "stop")) {
        out->kind = TP_EVENT_QUIT;
        return true;
    }
    if (!strcmp(cmd, "play")) {
        char *clip = next_token(&p);
        if (!clip) { snprintf(err, errlen, "play needs a clip name"); return false; }
        snprintf(out->str, sizeof(out->str), "%s", clip);
        char *fade = next_token(&p);
        out->a = fade ? (f32)atof(fade) : 0.25f;
        out->kind = TP_EVENT_PLAY;
        return true;
    }
    if (!strcmp(cmd, "set")) {
        char *what = next_token(&p);
        if (!what) { snprintf(err, errlen, "set needs a property"); return false; }
        if (!strcmp(what, "expr") || !strcmp(what, "expression")) {
            char *v = next_token(&p);
            if (!v) { snprintf(err, errlen, "set expr needs a value"); return false; }
            snprintf(out->str, sizeof(out->str), "%s", v);
            out->kind = TP_EVENT_EXPRESSION;
            return true;
        }
        if (!strcmp(what, "speed")) {
            char *v = next_token(&p);
            if (!v) { snprintf(err, errlen, "set speed needs a value"); return false; }
            out->a = (f32)atof(v);
            out->kind = TP_EVENT_SPEED;
            return true;
        }
        snprintf(err, errlen, "unknown property '%s'", what);
        return false;
    }
    if (!strcmp(cmd, "look")) {
        char *x = next_token(&p);
        char *y = next_token(&p);
        if (!x || !y) { snprintf(err, errlen, "look needs yaw and pitch"); return false; }
        char *w = next_token(&p);
        out->a = (f32)atof(x);
        out->b = (f32)atof(y);
        out->c = w ? (f32)atof(w) : 1.0f;
        out->kind = TP_EVENT_LOOK;
        return true;
    }
    if (!strcmp(cmd, "speed")) {
        char *v = next_token(&p);
        if (!v) { snprintf(err, errlen, "speed needs a rate"); return false; }
        out->a = (f32)atof(v);
        out->kind = TP_EVENT_SPEED;
        return true;
    }
    if (!strcmp(cmd, "pause"))  { out->kind = TP_EVENT_PAUSE; out->a = 1.0f; return true; }
    if (!strcmp(cmd, "resume")) { out->kind = TP_EVENT_PAUSE; out->a = 0.0f; return true; }
    if (!strcmp(cmd, "status")) { out->kind = TP_EVENT_STATUS; return true; }
    if (!strcmp(cmd, "stats"))  { out->kind = TP_EVENT_STATS;  return true; }
    if (!strcmp(cmd, "reload")) { out->kind = TP_EVENT_RELOAD; return true; }
    if (!strcmp(cmd, "shot") || !strcmp(cmd, "screenshot")) {
        char *path = next_token(&p);
        if (path) snprintf(out->str, sizeof(out->str), "%s", path);
        out->kind = TP_EVENT_SHOT;
        return true;
    }
    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
        out->kind = TP_EVENT_STATUS;   /* handled as a status reply + help */
        snprintf(out->str, sizeof(out->str), "help");
        return true;
    }

    snprintf(err, errlen, "unknown command '%s'", cmd);
    return false;
}

void tp_input_reply(int fd, const char *fmt, ...)
{
    if (fd < 0) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;

    ssize_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, (size_t)(n - off));
        /* The client may have hung up mid-reply; that is normal, not an error
         * worth failing the frame over. */
        if (w <= 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += w;
    }
}

/* ---- source manager -------------------------------------------------- */

void tp_input_init(tp_input *in) { memset(in, 0, sizeof(*in)); }

void tp_input_shutdown(tp_input *in)
{
    for (int i = 0; i < in->count; ++i) {
        if (in->sources[i] && in->sources[i]->close)
            in->sources[i]->close(in->sources[i]);
    }
    memset(in, 0, sizeof(*in));
}

tp_result tp_input_add(tp_input *in, tp_input_source *src)
{
    if (in->count >= TP_MAX_INPUT_SOURCES)
        return TP_FAIL(TP_ERR_NOMEM, "too many input sources");
    in->sources[in->count++] = src;
    TP_DEBUG("input source '%s' registered", src->name);
    return TP_OK;
}

int tp_input_poll(tp_input *in, tp_event *out, int max)
{
    int total = 0;
    for (int i = 0; i < in->count && total < max; ++i) {
        tp_input_source *s = in->sources[i];
        if (!s || !s->poll) continue;
        total += s->poll(s, out + total, max - total);
    }
    return total;
}

/* ---- stdin source ---------------------------------------------------- */
/* Useful the moment you have no socket client written yet: pipe commands in,
 * or type them if stdin is a terminal. */

typedef struct {
    char   buf[512];
    size_t len;
    bool   eof;
} tp_stdin_source;

static int stdin_poll(tp_input_source *self, tp_event *out, int max)
{
    tp_stdin_source *s = (tp_stdin_source *)self->ud;
    if (s->eof || max <= 0) return 0;

    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    int count = 0;

    while (count < max && poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
        char chunk[256];
        ssize_t n = read(STDIN_FILENO, chunk, sizeof(chunk));
        if (n == 0) { s->eof = true; break; }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            s->eof = true;
            break;
        }

        for (ssize_t i = 0; i < n && count < max; ++i) {
            if (chunk[i] == '\n') {
                s->buf[s->len] = '\0';
                char err[128] = {0};
                tp_event ev;
                if (tp_command_parse(s->buf, &ev, err, sizeof(err))) {
                    if (ev.kind != TP_EVENT_NONE) out[count++] = ev;
                } else {
                    TP_WARN("stdin: %s", err);
                }
                s->len = 0;
            } else if (s->len < sizeof(s->buf) - 1) {
                s->buf[s->len++] = chunk[i];
            } else {
                /* Overlong line: drop it rather than splitting it into two
                 * commands, which would be worse than losing it. */
                TP_WARN("stdin: line too long, discarded");
                s->len = 0;
            }
        }
    }
    return count;
}

static void stdin_close(tp_input_source *self)
{
    tp_free(self->ud);
    tp_free(self);
}

tp_result tp_input_add_stdin(tp_input *in)
{
    tp_stdin_source *s = (tp_stdin_source *)tp_alloc(sizeof(*s));
    tp_input_source *src = (tp_input_source *)tp_alloc(sizeof(*src));
    if (!s || !src) { tp_free(s); tp_free(src); return TP_ERR_NOMEM; }

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    src->name  = "stdin";
    src->poll  = stdin_poll;
    src->close = stdin_close;
    src->ud    = s;

    tp_result r = tp_input_add(in, src);
    if (r != TP_OK) { tp_free(s); tp_free(src); }
    return r;
}
