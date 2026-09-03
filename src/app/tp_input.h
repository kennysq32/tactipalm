/* tp_input.h — abstract event source.
 *
 * PLAN.md §8: "an abstract event source. Implementations: stdin, evdev, GPIO,
 * unix socket. The renderer never knows which one is live." Sources are
 * registered at startup and polled once per frame; the main loop sees a flat
 * array of tp_event and cannot tell where any of them came from.
 *
 * Everything is non-blocking. A renderer that stalls waiting on input has
 * already failed.
 */
#ifndef TP_INPUT_H
#define TP_INPUT_H

#include "../core/tp_common.h"

typedef enum {
    TP_EVENT_NONE = 0,
    TP_EVENT_QUIT,
    TP_EVENT_PLAY,        /* str = clip name, a = fade seconds            */
    TP_EVENT_EXPRESSION,  /* str = expression name                        */
    TP_EVENT_LOOK,        /* a = yaw, b = pitch, c = weight               */
    TP_EVENT_SPEED,       /* a = playback rate                            */
    TP_EVENT_PAUSE,       /* a != 0 = paused                              */
    TP_EVENT_STATUS,      /* request: reply on reply_fd                   */
    TP_EVENT_STATS,       /* request: reply on reply_fd                   */
    TP_EVENT_SHOT,        /* str = path, or empty for the configured one  */
    TP_EVENT_RELOAD
} tp_event_kind;

#define TP_EVENT_STR_LEN 64

typedef struct {
    tp_event_kind kind;
    char str[TP_EVENT_STR_LEN];
    f32  a, b, c;
    int  reply_fd;   /* write a reply here, or -1 */
} tp_event;

const char *tp_event_kind_str(tp_event_kind k);

/* Parse one command line into an event. Returns false and fills `err` for a
 * malformed line. Shared by every source so stdin and the socket accept
 * exactly the same grammar. */
bool tp_command_parse(const char *line, tp_event *out, char *err, size_t errlen);
/* The command grammar, for `help` replies and --help. */
const char *tp_command_help(void);

typedef struct tp_input_source tp_input_source;
struct tp_input_source {
    const char *name;
    int  (*poll)(tp_input_source *self, tp_event *out, int max);
    void (*close)(tp_input_source *self);
    void *ud;
};

#define TP_MAX_INPUT_SOURCES 4

typedef struct {
    tp_input_source *sources[TP_MAX_INPUT_SOURCES];
    int count;
} tp_input;

void tp_input_init(tp_input *in);
void tp_input_shutdown(tp_input *in);
tp_result tp_input_add(tp_input *in, tp_input_source *src);
/* Drain every source. Returns the number of events written. */
int  tp_input_poll(tp_input *in, tp_event *out, int max);

/* Built-in sources. */
tp_result tp_input_add_stdin(tp_input *in);
tp_result tp_input_add_ctl(tp_input *in, const char *socket_path);
/* Reply to a command that asked for one. Safe with fd < 0. */
void tp_input_reply(int fd, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif /* TP_INPUT_H */
