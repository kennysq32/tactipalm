/* tp_time.c — monotonic clock and frame-time statistics. */
#define _POSIX_C_SOURCE 200809L
#include "tp_time.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

double tp_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

u64 tp_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

void tp_sleep(double seconds)
{
    if (seconds <= 0.0) return;
    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - (double)req.tv_sec) * 1e9);
    while (nanosleep(&req, &req) == -1 && errno == EINTR)
        ; /* resume with the remainder nanosleep wrote back */
}

void tp_frame_stats_init(tp_frame_stats *fs)
{
    memset(fs, 0, sizeof(*fs));
    fs->last_time = tp_now();
}

double tp_frame_stats_tick(tp_frame_stats *fs)
{
    double now = tp_now();
    double dt = now - fs->last_time;
    fs->last_time = now;

    /* Guard against the clock and against a first frame that includes all of
     * startup: a 3-second "frame" would poison the window for its whole
     * lifetime. */
    if (dt < 0.0) dt = 0.0;
    if (fs->total_frames == 0) dt = 0.0;

    fs->dt = dt;
    fs->elapsed += dt;
    fs->total_frames++;

    if (dt > 0.0) {
        fs->samples[fs->head] = dt * 1000.0;
        fs->head = (fs->head + 1) % TP_FRAME_WINDOW;
        if (fs->count < TP_FRAME_WINDOW) fs->count++;
    }
    return dt;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Nearest-rank percentile on the sorted window. */
static double percentile(const double *sorted, int n, double p)
{
    if (n <= 0) return 0.0;
    int idx = (int)(p * (double)n);
    if (idx >= n) idx = n - 1;
    if (idx < 0) idx = 0;
    return sorted[idx];
}

void tp_frame_stats_report(const tp_frame_stats *fs, tp_frame_report *out)
{
    memset(out, 0, sizeof(*out));
    if (fs->count == 0) return;

    double sorted[TP_FRAME_WINDOW];
    memcpy(sorted, fs->samples, sizeof(double) * (size_t)fs->count);
    qsort(sorted, (size_t)fs->count, sizeof(double), cmp_double);

    double sum = 0.0;
    for (int i = 0; i < fs->count; ++i) sum += sorted[i];

    out->samples   = fs->count;
    out->min_ms    = sorted[0];
    out->max_ms    = sorted[fs->count - 1];
    out->mean_ms   = sum / (double)fs->count;
    out->median_ms = percentile(sorted, fs->count, 0.50);
    out->p95_ms    = percentile(sorted, fs->count, 0.95);
    out->p99_ms    = percentile(sorted, fs->count, 0.99);
    out->fps_mean  = out->mean_ms > 0.0 ? 1000.0 / out->mean_ms : 0.0;
}

void tp_frame_stats_log(const tp_frame_stats *fs, const char *tag)
{
    tp_frame_report r;
    tp_frame_stats_report(fs, &r);
    if (r.samples == 0) { TP_INFO("%s: no frames yet", tag); return; }

    TP_INFO("%s: %.1f fps | med %.2f ms  p95 %.2f  p99 %.2f  max %.2f  (n=%d, %llu total)",
            tag, r.fps_mean, r.median_ms, r.p95_ms, r.p99_ms, r.max_ms,
            r.samples, (unsigned long long)fs->total_frames);
}
