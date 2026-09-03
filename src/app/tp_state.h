/* tp_state.h — behaviour state machine.
 *
 * PLAN.md §8 draws the line: this module owns "what should the character be
 * doing", and emits animation blend weights. The renderer consumes weights
 * and nothing else. Keeping that boundary sharp is what lets voice input, an
 * LLM, sensors or a web UI arrive later as separate processes without the
 * renderer learning about any of them.
 *
 * Built now: clip registry, a sampler cursor per clip, and cross-fade between
 * two clips — PLAN.md §7 notes cross-fade alone is what makes it look alive.
 * Designed for but not yet built: additive layers for blink and look-at. The
 * layer fields exist and are carried through the update so that adding the
 * evaluation later is not a rewrite.
 */
#ifndef TP_STATE_H
#define TP_STATE_H

#include "../core/tp_common.h"
#include "../core/tp_math.h"

#define TP_MAX_CLIPS      16
#define TP_CLIP_NAME_LEN  32

typedef struct {
    char  name[TP_CLIP_NAME_LEN];
    f32   duration;   /* seconds; 0 for a pose                            */
    bool  loop;
} tp_clip;

typedef struct {
    int   clip;       /* index into clips[], -1 when inactive             */
    f32   time;       /* seconds into the clip                            */
    f32   weight;     /* 0..1, normalised across active slots             */
} tp_track;

typedef struct {
    tp_clip clips[TP_MAX_CLIPS];
    int     clip_count;

    /* Cross-fade is two tracks: `to` rising, `from` falling. */
    tp_track from, to;
    f32      fade_elapsed;
    f32      fade_duration;

    f32      speed;          /* global playback rate                      */
    bool     paused;

    /* Additive layer intent. Evaluated by a future layer stage; carried and
     * smoothed here so the plumbing exists from day one. */
    tp_vec2  look_target;    /* -1..1 yaw, pitch                          */
    tp_vec2  look_current;
    f32      look_weight;
    char     expression[TP_CLIP_NAME_LEN];

    u64      transitions;    /* for the stats line                        */
} tp_state;

void tp_state_init(tp_state *s);

/* Register a clip. Returns its index, or -1 when the table is full. */
int  tp_state_add_clip(tp_state *s, const char *name, f32 duration, bool loop);
int  tp_state_find_clip(const tp_state *s, const char *name);

/* Start a clip, cross-fading over `fade_seconds`. A fade of 0 is a cut.
 * Playing the already-current clip is a no-op rather than a restart, so a
 * controller can send `play idle` on a timer without stuttering. */
tp_result tp_state_play(tp_state *s, const char *name, f32 fade_seconds);

void tp_state_update(tp_state *s, f32 dt);

/* Fill `out_weights` (length clip_count) with the current blend weights.
 * This is the entire renderer-facing surface of this module. */
void tp_state_weights(const tp_state *s, f32 *out_weights, int max);

void tp_state_look_at(tp_state *s, f32 yaw, f32 pitch, f32 weight);
void tp_state_set_expression(tp_state *s, const char *name);
void tp_state_set_speed(tp_state *s, f32 speed);
void tp_state_pause(tp_state *s, bool paused);

/* Name of the dominant clip, for logs and the `status` command. */
const char *tp_state_current_clip(const tp_state *s);
void tp_state_describe(const tp_state *s, char *buf, size_t buflen);

#endif /* TP_STATE_H */
