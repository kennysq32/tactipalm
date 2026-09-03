/* tp_time.h — monotonic clock and frame-time statistics.
 *
 * PLAN.md §9 is explicit: log a rolling p99, not an average. Stutter is what
 * people notice, and an average hides it perfectly. So the stats window keeps
 * every sample and sorts a scratch copy on demand rather than tracking a
 * running mean.
 */
#ifndef TP_TIME_H
#define TP_TIME_H

#include "tp_common.h"

/* Monotonic seconds. Never jumps; safe to difference across a long run. */
double tp_now(void);
/* Monotonic nanoseconds, for the tighter measurements. */
u64 tp_now_ns(void);
/* Sleep, coalescing EINTR. */
void tp_sleep(double seconds);

#define TP_FRAME_WINDOW 240 /* 4 s at 60 Hz — long enough for a real p99 */

typedef struct {
    double samples[TP_FRAME_WINDOW]; /* milliseconds, ring buffer */
    int    count;                    /* valid samples, <= TP_FRAME_WINDOW  */
    int    head;
    u64    total_frames;
    double last_time;                /* tp_now() at previous tick          */
    double dt;                       /* seconds since previous tick        */
    double elapsed;                  /* seconds since tp_frame_stats_init  */
} tp_frame_stats;

typedef struct {
    double min_ms, max_ms;
    double mean_ms, median_ms;
    double p95_ms, p99_ms;
    double fps_mean;
    int    samples;
} tp_frame_report;

void tp_frame_stats_init(tp_frame_stats *fs);
/* Call once per frame. Returns dt in seconds; the first call returns 0. */
double tp_frame_stats_tick(tp_frame_stats *fs);
void tp_frame_stats_report(const tp_frame_stats *fs, tp_frame_report *out);
/* One INFO line summarising the window. */
void tp_frame_stats_log(const tp_frame_stats *fs, const char *tag);

#endif /* TP_TIME_H */
