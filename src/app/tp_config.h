/* tp_config.h — CLI parsing and run configuration.
 *
 * The flag set from PLAN.md §8 plus the ones the two-track split makes
 * necessary: a backend selector, a capability profile, and a headless
 * screenshot mode so the renderer can be proven on the board before HDMI is.
 */
#ifndef TP_CONFIG_H
#define TP_CONFIG_H

#include "../core/tp_common.h"
#include "../platform/tp_platform.h"

typedef struct {
    tp_platform_desc platform;

    const char *model_path;   /* --model  char.tpm                        */
    const char *scene_path;   /* --scene  scene.toml                      */
    const char *shot_path;    /* --shot   frame.png                       */
    const char *ctl_path;     /* --ctl    unix socket path                */

    int    frames;            /* --frames N, 0 = run until signalled      */
    double seconds;           /* --seconds S, 0 = no limit                */
    int    fps_cap;           /* --fps-cap N, 0 = uncapped                */
    double stats_interval;    /* --stats S, 0 = only at exit              */

    bool   caps_only;         /* --caps: print capabilities and exit      */
    bool   quiet;
    tp_log_level log_level;
} tp_config;

void tp_config_defaults(tp_config *c);
/* Returns TP_OK, or TP_ERR_ARGS after printing usage. `*should_exit` is set
 * for --help and --version, which are a success, not a failure. */
tp_result tp_config_parse(tp_config *c, int argc, char **argv, bool *should_exit);
void tp_config_print_usage(const char *argv0);
void tp_config_log(const tp_config *c);

#endif /* TP_CONFIG_H */
