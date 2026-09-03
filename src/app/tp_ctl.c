/* tp_ctl.c — unix socket control source.
 *
 * PLAN.md §8 calls this "the single most valuable architectural decision in
 * the plan", and the reason is the shape of the dependency rather than the
 * protocol: every future feature — voice, an LLM, sensors, a web UI — becomes
 * a separate process that writes lines to a socket, and the renderer stays a
 * renderer. So the protocol here is deliberately dull: newline-delimited
 * ASCII, no framing, no versioning handshake, greppable in a log, drivable
 * from `socat` or a shell one-liner.
 *
 * Everything is non-blocking. The renderer polls once per frame and never
 * waits on a client, because a control client that hangs must not be able to
 * drop a frame.
 */
#define _POSIX_C_SOURCE 200809L
#include "tp_input.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define TP_CTL_MAX_CLIENTS 8
#define TP_CTL_BUF 512

typedef struct {
    int    fd;
    char   buf[TP_CTL_BUF];
    size_t len;
} tp_ctl_client;

typedef struct {
    int  listen_fd;
    char path[108];  /* sun_path is 108 bytes on Linux */
    tp_ctl_client clients[TP_CTL_MAX_CLIENTS];
    int  client_count;
    bool greeted;
} tp_ctl;

static void ctl_drop_client(tp_ctl *c, int idx)
{
    if (idx < 0 || idx >= c->client_count) return;
    close(c->clients[idx].fd);
    TP_DEBUG("ctl: client %d disconnected", idx);
    /* Order does not matter, so fill the hole from the end. */
    c->clients[idx] = c->clients[c->client_count - 1];
    c->client_count--;
}

static void ctl_accept(tp_ctl *c)
{
    for (;;) {
        int fd = accept(c->listen_fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) continue;
            /* EAGAIN is the normal "no pending connections" case. */
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                TP_WARN("ctl: accept failed: %s", strerror(errno));
            return;
        }

        if (c->client_count >= TP_CTL_MAX_CLIENTS) {
            const char *msg = "ERR too many clients\n";
            ssize_t ignored = write(fd, msg, strlen(msg));
            (void)ignored;
            close(fd);
            TP_WARN("ctl: refused a connection, %d clients already",
                    c->client_count);
            continue;
        }

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        fcntl(fd, F_SETFD, FD_CLOEXEC);

        tp_ctl_client *cl = &c->clients[c->client_count++];
        cl->fd = fd;
        cl->len = 0;

        const char *hello = "tactipalm ready. 'help' for commands.\n";
        ssize_t ignored = write(fd, hello, strlen(hello));
        (void)ignored;
        TP_DEBUG("ctl: client connected (%d total)", c->client_count);
    }
}

static int ctl_read_client(tp_ctl *c, int idx, tp_event *out, int max)
{
    tp_ctl_client *cl = &c->clients[idx];
    int count = 0;

    for (;;) {
        char chunk[256];
        ssize_t n = read(cl->fd, chunk, sizeof(chunk));

        if (n == 0) { ctl_drop_client(c, idx); return count; }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return count;
            TP_DEBUG("ctl: read error: %s", strerror(errno));
            ctl_drop_client(c, idx);
            return count;
        }

        for (ssize_t i = 0; i < n; ++i) {
            if (chunk[i] != '\n') {
                if (cl->len < sizeof(cl->buf) - 1) {
                    cl->buf[cl->len++] = chunk[i];
                } else {
                    tp_input_reply(cl->fd, "ERR line too long\n");
                    cl->len = 0;
                }
                continue;
            }

            cl->buf[cl->len] = '\0';
            cl->len = 0;

            tp_event ev;
            char err[128] = {0};
            if (!tp_command_parse(cl->buf, &ev, err, sizeof(err))) {
                tp_input_reply(cl->fd, "ERR %s\n", err);
                continue;
            }
            if (ev.kind == TP_EVENT_NONE) continue;

            if (count >= max) {
                /* The event buffer is full for this frame. Leaving the rest in
                 * the socket is correct — it is a stream, and we will be back
                 * next frame. */
                tp_input_reply(cl->fd, "BUSY retry\n");
                return count;
            }

            ev.reply_fd = cl->fd;
            out[count++] = ev;
        }
    }
}

static int ctl_poll(tp_input_source *self, tp_event *out, int max)
{
    tp_ctl *c = (tp_ctl *)self->ud;
    if (max <= 0) return 0;

    ctl_accept(c);

    int count = 0;
    /* Iterate backwards: ctl_drop_client swaps the last client into the hole,
     * so a forward loop would skip whichever client that was. */
    for (int i = c->client_count - 1; i >= 0 && count < max; --i) {
        struct pollfd pfd = { .fd = c->clients[i].fd, .events = POLLIN };
        if (poll(&pfd, 1, 0) <= 0) continue;
        if (pfd.revents & (POLLIN | POLLHUP | POLLERR))
            count += ctl_read_client(c, i, out + count, max - count);
    }
    return count;
}

static void ctl_close(tp_input_source *self)
{
    tp_ctl *c = (tp_ctl *)self->ud;
    if (c) {
        for (int i = 0; i < c->client_count; ++i) close(c->clients[i].fd);
        if (c->listen_fd >= 0) close(c->listen_fd);
        /* Remove our own socket file. If we crash instead, the next start
         * unlinks a stale one — see below. */
        if (c->path[0]) unlink(c->path);
        tp_free(c);
    }
    tp_free(self);
}

tp_result tp_input_add_ctl(tp_input *in, const char *socket_path)
{
    if (!socket_path || !*socket_path)
        return TP_FAIL(TP_ERR_ARGS, "ctl: empty socket path");

    tp_ctl *c = (tp_ctl *)tp_alloc(sizeof(*c));
    tp_input_source *src = (tp_input_source *)tp_alloc(sizeof(*src));
    if (!c || !src) { tp_free(c); tp_free(src); return TP_ERR_NOMEM; }
    c->listen_fd = -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(addr.sun_path)) {
        tp_free(c); tp_free(src);
        return TP_FAIL(TP_ERR_ARGS, "ctl: socket path longer than %zu bytes",
                       sizeof(addr.sun_path) - 1);
    }
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);
    snprintf(c->path, sizeof(c->path), "%s", socket_path);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        tp_free(c); tp_free(src);
        return TP_FAIL(TP_ERR_IO, "ctl: socket(): %s", strerror(errno));
    }

    /* A previous run that was SIGKILLed leaves the node behind and bind()
     * fails with EADDRINUSE. Probe it first: if something is still listening,
     * refuse rather than stealing another instance's socket. */
    if (access(socket_path, F_OK) == 0) {
        int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        bool live = (probe >= 0 &&
                     connect(probe, (struct sockaddr *)&addr, sizeof(addr)) == 0);
        if (probe >= 0) close(probe);
        if (live) {
            close(fd);
            tp_free(c); tp_free(src);
            return TP_FAIL(TP_ERR_IO,
                           "ctl: '%s' is already served by a running instance",
                           socket_path);
        }
        TP_WARN("ctl: removing stale socket '%s'", socket_path);
        unlink(socket_path);
    }

    /* Create it owner-only. The control socket can drive the renderer, so it
     * should not be world-writable just because the umask was loose. */
    mode_t old_umask = umask(0177);
    int bind_rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    int bind_errno = errno;
    umask(old_umask);

    if (bind_rc < 0) {
        close(fd);
        tp_free(c); tp_free(src);
        return TP_FAIL(TP_ERR_IO, "ctl: bind('%s'): %s",
                       socket_path, strerror(bind_errno));
    }
    if (listen(fd, 4) < 0) {
        close(fd);
        unlink(socket_path);
        tp_free(c); tp_free(src);
        return TP_FAIL(TP_ERR_IO, "ctl: listen(): %s", strerror(errno));
    }

    c->listen_fd = fd;
    src->name  = "ctl";
    src->poll  = ctl_poll;
    src->close = ctl_close;
    src->ud    = c;

    tp_result r = tp_input_add(in, src);
    if (r != TP_OK) { ctl_close(src); return r; }

    TP_INFO("control socket listening on %s", socket_path);
    TP_INFO("  try: printf 'status\\n' | socat - UNIX-CONNECT:%s", socket_path);
    return TP_OK;
}
