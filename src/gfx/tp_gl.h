/* tp_gl.h — the single place GL headers are included.
 *
 * We target GLES 3.0 (CONTRACT.md §2: Panfrost advertises 3.1 on Mali-G31, but
 * compute shaders and SSBOs are where its driver bugs concentrate and skinned
 * character rendering needs neither). gl3.h is the 3.0 header; gl2ext.h is
 * included only for extension *enums* — ASTC formats, timer queries. Including
 * gl31.h or gl32.h here would be a way to use 3.1+ entry points by accident,
 * so don't.
 */
#ifndef TP_GL_H
#define TP_GL_H

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include "../core/tp_common.h"

/* Drain the GL error queue, logging anything found. Returns true if clean.
 * `where` names the call site in the log. */
bool tp_gl_check(const char *where);

/* Human-readable GLenum for the errors and formats we actually handle. */
const char *tp_gl_enum_str(GLenum e);

/* In debug builds this traps after every wrapped call; in release it is free. */
#ifdef TP_GL_DEBUG
#  define TP_GL(call) do { call; tp_gl_check(#call); } while (0)
#else
#  define TP_GL(call) do { call; } while (0)
#endif

#endif /* TP_GL_H */
