/* tp_config.c — CLI parsing. */
#define _POSIX_C_SOURCE 200809L
#include "tp_config.h"
#include "../gfx/tp_caps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TP_VERSION
#  define TP_VERSION "0.1.0-dev"
#endif

void tp_config_defaults(tp_config *c)
{
    memset(c, 0, sizeof(*c));
    tp_platform_desc_defaults(&c->platform);
    c->frames         = 0;
    c->seconds        = 0.0;
    c->fps_cap        = 0;
    c->stats_interval = 5.0;
    c->log_level      = TP_LOG_INFO;
}

void tp_config_print_usage(const char *argv0)
{
    printf(
"tactipalm " TP_VERSION " — 3D character renderer for Orange Pi Zero 2W\n"
"\n"
"usage: %s [options]\n"
"\n"
"display\n"
"  --backend K       kms | surfaceless | auto        (default: auto)\n"
"  --device PATH     DRM device                      (default: /dev/dri/card0)\n"
"  --res WxH         render resolution               (default: 1280x720)\n"
"  --refresh HZ      preferred mode refresh rate     (default: any)\n"
"  --msaa N          0, 2 or 4                       (default: 4)\n"
"  --no-vsync        do not wait for the page flip\n"
"  --srgb            request an sRGB framebuffer\n"
"\n"
"content\n"
"  --model PATH      .tpm character to load\n"
"  --scene PATH      scene description\n"
"\n"
"run control\n"
"  --frames N        stop after N frames\n"
"  --seconds S       stop after S seconds\n"
"  --fps-cap N       limit to N fps (0 = uncapped)\n"
"  --shot PATH       write a PNG of the last frame and exit\n"
"  --ctl PATH        listen for commands on a unix socket\n"
"\n"
"diagnostics\n"
"  --profile P       mali-g31 | native               (default: mali-g31)\n"
"                    mali-g31 masks this GPU's capabilities down to the\n"
"                    board's, so desktop-only formats fail here too\n"
"  --caps            print GL capabilities and exit\n"
"  --stats S         log frame-time percentiles every S seconds (0 = off)\n"
"  --log LEVEL       trace | debug | info | warn | error\n"
"  -q, --quiet       errors only\n"
"  -v, --verbose     same as --log debug\n"
"  -h, --help        this text\n"
"  -V, --version     version and build configuration\n"
"\n"
"examples\n"
"  %s --caps                              inspect the GPU\n"
"  %s --backend surfaceless --frames 1 --shot f.png\n"
"                                         headless render to a PNG; this is\n"
"                                         the on-device milestone that works\n"
"                                         before HDMI does\n"
"  %s --backend kms --res 1280x720        on-screen, from a real VT\n",
    argv0, argv0, argv0, argv0);
}

static void print_version(void)
{
    printf("tactipalm " TP_VERSION "\n");
    printf("  kms backend      : %s\n",
           tp_backend_kms_available() ? "compiled in" :
           "NOT compiled in (libdrm/libgbm headers missing at build time)");
    printf("  surfaceless      : compiled in\n");
    printf("  GL target        : OpenGL ES 3.0 / GLSL ES 3.00\n");
    printf("  default profile  : mali-g31\n");
#if defined(__aarch64__)
    printf("  arch             : aarch64\n");
#elif defined(__x86_64__)
    printf("  arch             : x86_64 (development host)\n");
#endif
}

static bool need_arg(int i, int argc, const char *flag)
{
    if (i + 1 >= argc) {
        fprintf(stderr, "tactipalm: %s requires an argument\n", flag);
        return false;
    }
    return true;
}

static bool parse_res(const char *s, int *w, int *h)
{
    char *end = NULL;
    long a = strtol(s, &end, 10);
    if (!end || (*end != 'x' && *end != 'X') || a <= 0) return false;
    long b = strtol(end + 1, &end, 10);
    if (!end || *end != '\0' || b <= 0) return false;
    *w = (int)a;
    *h = (int)b;
    return true;
}

static tp_log_level parse_log_level(const char *s)
{
    if (!strcmp(s, "trace")) return TP_LOG_TRACE;
    if (!strcmp(s, "debug")) return TP_LOG_DEBUG;
    if (!strcmp(s, "info"))  return TP_LOG_INFO;
    if (!strcmp(s, "warn"))  return TP_LOG_WARN;
    if (!strcmp(s, "error")) return TP_LOG_ERROR;
    if (!strcmp(s, "off"))   return TP_LOG_OFF;
    fprintf(stderr, "tactipalm: unknown log level '%s', using info\n", s);
    return TP_LOG_INFO;
}

tp_result tp_config_parse(tp_config *c, int argc, char **argv, bool *should_exit)
{
    tp_config_defaults(c);
    *should_exit = false;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];

        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            tp_config_print_usage(argv[0]);
            *should_exit = true;
            return TP_OK;
        }
        if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
            print_version();
            *should_exit = true;
            return TP_OK;
        }

        if (!strcmp(a, "--backend")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->platform.backend = tp_backend_parse(argv[++i]);
        } else if (!strcmp(a, "--device")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->platform.device = argv[++i];
        } else if (!strcmp(a, "--res")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            if (!parse_res(argv[++i], &c->platform.width, &c->platform.height)) {
                fprintf(stderr, "tactipalm: --res wants WxH, e.g. 1280x720\n");
                return TP_ERR_ARGS;
            }
        } else if (!strcmp(a, "--refresh")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->platform.refresh_hz = atoi(argv[++i]);
        } else if (!strcmp(a, "--msaa")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->platform.msaa = atoi(argv[++i]);
            if (c->platform.msaa != 0 && c->platform.msaa != 2 &&
                c->platform.msaa != 4 && c->platform.msaa != 8) {
                fprintf(stderr, "tactipalm: --msaa wants 0, 2, 4 or 8\n");
                return TP_ERR_ARGS;
            }
        } else if (!strcmp(a, "--no-vsync")) {
            c->platform.vsync = false;
        } else if (!strcmp(a, "--srgb")) {
            c->platform.srgb = true;
        } else if (!strcmp(a, "--profile")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->platform.profile = tp_profile_parse(argv[++i]);
        } else if (!strcmp(a, "--model")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->model_path = argv[++i];
        } else if (!strcmp(a, "--scene")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->scene_path = argv[++i];
        } else if (!strcmp(a, "--shot")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->shot_path = argv[++i];
        } else if (!strcmp(a, "--ctl")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->ctl_path = argv[++i];
        } else if (!strcmp(a, "--frames")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->frames = atoi(argv[++i]);
        } else if (!strcmp(a, "--seconds")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->seconds = atof(argv[++i]);
        } else if (!strcmp(a, "--fps-cap")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->fps_cap = atoi(argv[++i]);
        } else if (!strcmp(a, "--stats")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->stats_interval = atof(argv[++i]);
        } else if (!strcmp(a, "--caps")) {
            c->caps_only = true;
        } else if (!strcmp(a, "--log")) {
            if (!need_arg(i, argc, a)) return TP_ERR_ARGS;
            c->log_level = parse_log_level(argv[++i]);
        } else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) {
            c->quiet = true;
            c->log_level = TP_LOG_ERROR;
        } else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
            c->log_level = TP_LOG_DEBUG;
        } else {
            fprintf(stderr, "tactipalm: unknown option '%s'\n", a);
            fprintf(stderr, "try '%s --help'\n", argv[0]);
            return TP_ERR_ARGS;
        }
    }

    /* --shot with no frame limit would render forever and never write the
     * file. One frame is the useful default. */
    if (c->shot_path && c->frames == 0 && c->seconds == 0.0) {
        c->frames = 1;
        c->platform.vsync = false;
    }
    if (c->caps_only) {
        c->frames = 0;
        c->seconds = 0.0;
    }
    return TP_OK;
}

void tp_config_log(const tp_config *c)
{
    TP_DEBUG("config: backend=%s device=%s res=%dx%d msaa=%d vsync=%d profile=%s",
             tp_backend_str(c->platform.backend),
             c->platform.device ? c->platform.device : "(auto)",
             c->platform.width, c->platform.height, c->platform.msaa,
             (int)c->platform.vsync, tp_profile_str(c->platform.profile));
    if (c->frames)  TP_DEBUG("config: stop after %d frames", c->frames);
    if (c->seconds > 0) TP_DEBUG("config: stop after %.2f s", c->seconds);
    if (c->fps_cap) TP_DEBUG("config: fps cap %d", c->fps_cap);
    if (c->shot_path) TP_DEBUG("config: screenshot -> %s", c->shot_path);
    if (c->ctl_path)  TP_DEBUG("config: control socket %s", c->ctl_path);
}
