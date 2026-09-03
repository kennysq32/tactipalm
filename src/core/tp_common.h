/* tp_common.h — basic types, logging, error handling.
 *
 * Tactipalm renderer core. Freestanding of any GL/DRM header on purpose:
 * this is the one header every translation unit may include.
 */
#ifndef TP_COMMON_H
#define TP_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- integer aliases ------------------------------------------------- */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef float    f32;
typedef double   f64;

/* ---- result codes ---------------------------------------------------- */
typedef enum {
    TP_OK = 0,
    TP_ERR_ARGS = -1,       /* bad CLI / config                            */
    TP_ERR_PLATFORM = -2,   /* DRM / GBM / EGL bring-up failed             */
    TP_ERR_GL = -3,         /* shader compile, FBO incomplete, ...         */
    TP_ERR_IO = -4,         /* file / socket                               */
    TP_ERR_CAPS = -5,       /* required capability absent, or a target-profile
                             * violation (see tp_caps.h) — this is the one
                             * that catches "works on desktop, dies on Mali" */
    TP_ERR_NOMEM = -6,
    TP_ERR_UNSUPPORTED = -7
} tp_result;

const char *tp_result_str(tp_result r);

/* ---- logging --------------------------------------------------------- */
typedef enum {
    TP_LOG_TRACE = 0,
    TP_LOG_DEBUG,
    TP_LOG_INFO,
    TP_LOG_WARN,
    TP_LOG_ERROR,
    TP_LOG_OFF
} tp_log_level;

void tp_log_set_level(tp_log_level lvl);
tp_log_level tp_log_get_level(void);
void tp_log_set_color(bool on);
void tp_logf(tp_log_level lvl, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

#define TP_TRACE(...) tp_logf(TP_LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define TP_DEBUG(...) tp_logf(TP_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define TP_INFO(...)  tp_logf(TP_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define TP_WARN(...)  tp_logf(TP_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define TP_ERROR(...) tp_logf(TP_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

/* Log and return a result code in one step. */
#define TP_FAIL(code, ...) (TP_ERROR(__VA_ARGS__), (code))

/* ---- misc ------------------------------------------------------------ */
#define TP_ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define TP_MIN(a, b) ((a) < (b) ? (a) : (b))
#define TP_MAX(a, b) ((a) > (b) ? (a) : (b))
#define TP_CLAMP(x, lo, hi) TP_MIN(TP_MAX((x), (lo)), (hi))
#define TP_UNUSED(x) ((void)(x))

/* Allocation helpers. The renderer must not allocate per frame; these exist
 * for load-time allocation only. tp_alloc zeroes. */
void *tp_alloc(size_t n);
void *tp_realloc(void *p, size_t n);
void  tp_free(void *p);

/* Read an entire file into a NUL-terminated heap buffer. Caller frees. */
char *tp_read_file(const char *path, size_t *out_len);

#endif /* TP_COMMON_H */
