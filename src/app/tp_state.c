/* tp_state.c — behaviour state machine. */
#define _POSIX_C_SOURCE 200809L
#include "tp_state.h"

#include <stdio.h>
#include <string.h>

void tp_state_init(tp_state *s)
{
    memset(s, 0, sizeof(*s));
    s->from.clip = -1;
    s->to.clip   = -1;
    s->speed     = 1.0f;
    s->look_weight = 0.0f;
    snprintf(s->expression, sizeof(s->expression), "%s", "neutral");
}

int tp_state_add_clip(tp_state *s, const char *name, f32 duration, bool loop)
{
    if (s->clip_count >= TP_MAX_CLIPS) {
        TP_WARN("clip table full (%d), ignoring '%s'", TP_MAX_CLIPS, name);
        return -1;
    }
    int existing = tp_state_find_clip(s, name);
    if (existing >= 0) {
        TP_WARN("clip '%s' already registered at index %d", name, existing);
        return existing;
    }

    tp_clip *c = &s->clips[s->clip_count];
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->duration = duration > 0.0f ? duration : 0.0f;
    c->loop = loop;

    TP_DEBUG("clip %d: '%s' %.2fs%s", s->clip_count, c->name,
             (double)c->duration, c->loop ? " (loop)" : "");
    return s->clip_count++;
}

int tp_state_find_clip(const tp_state *s, const char *name)
{
    for (int i = 0; i < s->clip_count; ++i)
        if (strcmp(s->clips[i].name, name) == 0) return i;
    return -1;
}

tp_result tp_state_play(tp_state *s, const char *name, f32 fade_seconds)
{
    int idx = tp_state_find_clip(s, name);
    if (idx < 0)
        return TP_FAIL(TP_ERR_ARGS, "no clip named '%s'", name);

    /* Re-requesting the clip that is already fully current would otherwise
     * restart it from t=0 every time a controller repeated the command. */
    if (s->to.clip == idx && s->fade_elapsed >= s->fade_duration) {
        TP_TRACE("clip '%s' already current", name);
        return TP_OK;
    }

    /* Interrupting a fade: the partially-blended result becomes the outgoing
     * track. Snapshotting `to` rather than `from` keeps the visible pose
     * continuous, which is what stops a rapid sequence of commands from
     * looking like a glitch. */
    s->from = s->to;
    s->to.clip   = idx;
    s->to.time   = 0.0f;
    s->to.weight = (fade_seconds > 0.0f && s->from.clip >= 0) ? 0.0f : 1.0f;

    s->fade_duration = (s->from.clip >= 0) ? TP_MAX(fade_seconds, 0.0f) : 0.0f;
    s->fade_elapsed  = 0.0f;
    s->transitions++;

    TP_DEBUG("play '%s' (fade %.2fs, from '%s')", name, (double)s->fade_duration,
             s->from.clip >= 0 ? s->clips[s->from.clip].name : "-");
    return TP_OK;
}

static void advance_track(const tp_state *s, tp_track *t, f32 dt)
{
    if (t->clip < 0) return;
    const tp_clip *c = &s->clips[t->clip];
    t->time += dt;
    if (c->duration > 0.0f) {
        if (c->loop) {
            /* fmodf rather than a while loop: a long stall must not spin. */
            t->time = fmodf(t->time, c->duration);
            if (t->time < 0.0f) t->time += c->duration;
        } else if (t->time > c->duration) {
            t->time = c->duration;
        }
    }
}

void tp_state_update(tp_state *s, f32 dt)
{
    if (s->paused) dt = 0.0f;
    f32 scaled = dt * s->speed;

    advance_track(s, &s->to, scaled);
    advance_track(s, &s->from, scaled);

    if (s->fade_duration > 0.0f && s->fade_elapsed < s->fade_duration) {
        s->fade_elapsed += dt;
        f32 t = TP_CLAMP(s->fade_elapsed / s->fade_duration, 0.0f, 1.0f);
        /* Smoothstep, not linear: a linear cross-fade has a visible velocity
         * discontinuity at both ends. */
        f32 e = t * t * (3.0f - 2.0f * t);
        s->to.weight   = e;
        s->from.weight = 1.0f - e;
        if (t >= 1.0f) {
            s->from.clip   = -1;
            s->from.weight = 0.0f;
            s->to.weight   = 1.0f;
        }
    } else {
        s->to.weight = 1.0f;
        if (s->from.clip >= 0) { s->from.clip = -1; s->from.weight = 0.0f; }
    }

    /* Look-at is smoothed towards its target with a frame-rate independent
     * exponential, so a controller can send discrete look commands and still
     * get continuous motion. */
    const f32 look_rate = 8.0f;
    f32 k = 1.0f - expf(-look_rate * dt);
    s->look_current.x = tp_lerpf(s->look_current.x, s->look_target.x, k);
    s->look_current.y = tp_lerpf(s->look_current.y, s->look_target.y, k);
}

void tp_state_weights(const tp_state *s, f32 *out, int max)
{
    int n = TP_MIN(max, s->clip_count);
    for (int i = 0; i < n; ++i) out[i] = 0.0f;

    if (s->to.clip >= 0 && s->to.clip < n)     out[s->to.clip]   += s->to.weight;
    if (s->from.clip >= 0 && s->from.clip < n) out[s->from.clip] += s->from.weight;

    /* Normalise. Interrupting a fade can leave the sum slightly off 1, and an
     * un-normalised weight set scales the whole pose rather than blending it. */
    f32 sum = 0.0f;
    for (int i = 0; i < n; ++i) sum += out[i];
    if (sum > TP_EPSILON) {
        f32 inv = 1.0f / sum;
        for (int i = 0; i < n; ++i) out[i] *= inv;
    }
}

void tp_state_look_at(tp_state *s, f32 yaw, f32 pitch, f32 weight)
{
    s->look_target = tp_v2(TP_CLAMP(yaw, -1.0f, 1.0f),
                           TP_CLAMP(pitch, -1.0f, 1.0f));
    s->look_weight = TP_CLAMP(weight, 0.0f, 1.0f);
}

void tp_state_set_expression(tp_state *s, const char *name)
{
    snprintf(s->expression, sizeof(s->expression), "%s", name ? name : "neutral");
    TP_DEBUG("expression -> %s", s->expression);
}

void tp_state_set_speed(tp_state *s, f32 speed)
{
    s->speed = TP_CLAMP(speed, 0.0f, 8.0f);
}

void tp_state_pause(tp_state *s, bool paused) { s->paused = paused; }

const char *tp_state_current_clip(const tp_state *s)
{
    const tp_track *dom = (s->to.weight >= s->from.weight) ? &s->to : &s->from;
    if (dom->clip < 0) return "(none)";
    return s->clips[dom->clip].name;
}

void tp_state_describe(const tp_state *s, char *buf, size_t buflen)
{
    snprintf(buf, buflen,
             "clip=%s t=%.2f fade=%.0f%% expr=%s look=%.2f,%.2f speed=%.2f%s",
             tp_state_current_clip(s), (double)s->to.time,
             s->fade_duration > 0.0f
                 ? (double)(100.0f * TP_CLAMP(s->fade_elapsed / s->fade_duration, 0.f, 1.f))
                 : 100.0,
             s->expression,
             (double)s->look_current.x, (double)s->look_current.y,
             (double)s->speed, s->paused ? " [paused]" : "");
}
