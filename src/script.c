#include "script.h"
#include "editor.h"
#include "buffer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Helper to get editor from JS context */
static Editor *get_editor(duk_context *ctx) {
    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "editor_ptr");
    Editor *e = (Editor *)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);
    return e;
}

/* editor.message(str) */
static duk_ret_t js_message(duk_context *ctx) {
    const char *msg = duk_require_string(ctx, 0);
    Editor *e = get_editor(ctx);
    if (e) editor_set_message(e, "%s", msg);
    return 0;
}

/* editor.getCurrentBufferName() */
static duk_ret_t js_get_current_buffer_name(duk_context *ctx) {
    Editor *e = get_editor(ctx);
    if (!e) { duk_push_string(ctx, ""); return 1; }
    Buffer *buf = editor_current_buffer(e);
    duk_push_string(ctx, buf ? buf->name : "");
    return 1;
}

/* editor.listBuffers() */
static duk_ret_t js_list_buffers(duk_context *ctx) {
    Editor *e = get_editor(ctx);
    duk_push_array(ctx);
    if (!e) return 1;
    for (int i = 0; i < e->num_buffers; i++) {
        duk_push_string(ctx, e->buffers[i]->name);
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }
    return 1;
}

/* editor.switchBuffer(name) */
static duk_ret_t js_switch_buffer(duk_context *ctx) {
    const char *name = duk_require_string(ctx, 0);
    Editor *e = get_editor(ctx);
    if (e) editor_switch_to_buffer(e, name);
    return 0;
}

/* editor.newBuffer(name) */
static duk_ret_t js_new_buffer(duk_context *ctx) {
    const char *name = duk_require_string(ctx, 0);
    Editor *e = get_editor(ctx);
    if (e) editor_new_buffer(e, name);
    return 0;
}

/* editor.insertText(str) */
static duk_ret_t js_insert_text(duk_context *ctx) {
    const char *str = duk_require_string(ctx, 0);
    Editor *e = get_editor(ctx);
    if (!e) return 0;
    Buffer *buf = editor_current_buffer(e);
    if (!buf) return 0;
    for (const char *p = str; *p; p++) {
        buffer_insert_char(buf, *p);
    }
    return 0;
}

/* editor.getBufferContent() */
static duk_ret_t js_get_buffer_content(duk_context *ctx) {
    Editor *e = get_editor(ctx);
    if (!e) { duk_push_string(ctx, ""); return 1; }
    Buffer *buf = editor_current_buffer(e);
    if (!buf) { duk_push_string(ctx, ""); return 1; }

    /* Compute total size, guarding against overflow */
    size_t total = 0;
    for (int i = 0; i < buf->num_lines; i++) {
        size_t llen = strlen(buf->lines[i]) + 1;
        if (total + llen < total) { duk_push_string(ctx, ""); return 1; } /* overflow */
        total += llen;
    }

    char *content = malloc(total + 1);
    if (!content) { duk_push_string(ctx, ""); return 1; }

    size_t pos = 0;
    for (int i = 0; i < buf->num_lines; i++) {
        size_t len = strlen(buf->lines[i]);
        memcpy(content + pos, buf->lines[i], len);
        pos += len;
        if (i < buf->num_lines - 1) {
            content[pos++] = '\n';
        }
    }
    content[pos] = '\0';
    duk_push_string(ctx, content);
    free(content);
    return 1;
}

/* editor.setBufferContent(str) */
static duk_ret_t js_set_buffer_content(duk_context *ctx) {
    const char *str = duk_require_string(ctx, 0);
    Editor *e = get_editor(ctx);
    if (!e) return 0;
    Buffer *buf = editor_current_buffer(e);
    if (!buf) return 0;

    /* Clear buffer */
    for (int i = 0; i < buf->num_lines; i++) free(buf->lines[i]);
    buf->num_lines = 0;
    buf->lines[0] = strdup("");
    buf->num_lines = 1;
    buf->cursor_line = 0;
    buf->cursor_col  = 0;

    /* Insert content */
    for (const char *p = str; *p; p++) {
        buffer_insert_char(buf, *p);
    }
    buf->modified = 1;
    return 0;
}

/* editor.openFile(filename) */
static duk_ret_t js_open_file(duk_context *ctx) {
    const char *filename = duk_require_string(ctx, 0);
    Editor *e = get_editor(ctx);
    if (e) editor_open_file(e, filename);
    return 0;
}

/* editor.saveFile() */
static duk_ret_t js_save_file(duk_context *ctx) {
    Editor *e = get_editor(ctx);
    if (e) editor_save_current(e);
    return 0;
}

/* editor.getCurrentLine() */
static duk_ret_t js_get_current_line(duk_context *ctx) {
    Editor *e = get_editor(ctx);
    if (!e) { duk_push_int(ctx, 0); return 1; }
    Buffer *buf = editor_current_buffer(e);
    duk_push_int(ctx, buf ? buf->cursor_line + 1 : 0);
    return 1;
}

/* editor.getCurrentCol() */
static duk_ret_t js_get_current_col(duk_context *ctx) {
    Editor *e = get_editor(ctx);
    if (!e) { duk_push_int(ctx, 0); return 1; }
    Buffer *buf = editor_current_buffer(e);
    duk_push_int(ctx, buf ? buf->cursor_col + 1 : 0);
    return 1;
}

/* editor.setMark() */
static duk_ret_t js_set_mark(duk_context *ctx) {
    Editor *e = get_editor(ctx);
    if (!e) return 0;
    Buffer *buf = editor_current_buffer(e);
    if (buf) buffer_set_mark(buf);
    return 0;
}

/* editor.copyRegion() -- copy region into kill ring */
static duk_ret_t js_copy_region(duk_context *ctx) {
    Editor *e = get_editor(ctx);
    if (!e) return 0;
    Buffer *buf = editor_current_buffer(e);
    if (buf) buffer_copy_region(buf, &e->kill_ring);
    return 0;
}

/* editor.killRegion() -- cut region into kill ring */
static duk_ret_t js_kill_region(duk_context *ctx) {
    Editor *e = get_editor(ctx);
    if (!e) return 0;
    Buffer *buf = editor_current_buffer(e);
    if (buf) buffer_kill_region(buf, &e->kill_ring);
    return 0;
}

/* editor.yank() -- paste from kill ring */
static duk_ret_t js_yank(duk_context *ctx) {
    Editor *e = get_editor(ctx);
    if (!e) return 0;
    Buffer *buf = editor_current_buffer(e);
    if (buf) buffer_yank(buf, e->kill_ring);
    return 0;
}

/* editor.find(query) -- search forward; returns true if found */
static duk_ret_t js_find(duk_context *ctx) {
    const char *query = duk_require_string(ctx, 0);
    Editor *e = get_editor(ctx);
    if (!e) { duk_push_boolean(ctx, 0); return 1; }
    Buffer *buf = editor_current_buffer(e);
    if (!buf) { duk_push_boolean(ctx, 0); return 1; }
    duk_push_boolean(ctx, buffer_search_forward(buf, query));
    return 1;
}

/* editor.replace(search, replacement) -- replace all; returns count */
static duk_ret_t js_replace(duk_context *ctx) {
    const char *search      = duk_require_string(ctx, 0);
    const char *replacement = duk_require_string(ctx, 1);
    Editor *e = get_editor(ctx);
    if (!e) { duk_push_int(ctx, 0); return 1; }
    Buffer *buf = editor_current_buffer(e);
    if (!buf) { duk_push_int(ctx, 0); return 1; }
    duk_push_int(ctx, buffer_replace_all(buf, search, replacement));
    return 1;
}

duk_context *script_init(Editor *e) {
    duk_context *ctx = duk_create_heap_default();
    if (!ctx) return NULL;

    /* Store editor pointer in global stash */
    duk_push_global_stash(ctx);
    duk_push_pointer(ctx, e);
    duk_put_prop_string(ctx, -2, "editor_ptr");
    duk_pop(ctx);

    /* Create 'editor' global object */
    duk_push_object(ctx);

    static const struct { const char *name; duk_c_function fn; } methods[] = {
        { "message",              js_message              },
        { "getCurrentBufferName", js_get_current_buffer_name },
        { "listBuffers",          js_list_buffers         },
        { "switchBuffer",         js_switch_buffer        },
        { "newBuffer",            js_new_buffer           },
        { "insertText",           js_insert_text          },
        { "getBufferContent",     js_get_buffer_content   },
        { "setBufferContent",     js_set_buffer_content   },
        { "openFile",             js_open_file            },
        { "saveFile",             js_save_file            },
        { "getCurrentLine",       js_get_current_line     },
        { "getCurrentCol",        js_get_current_col      },
        { "setMark",              js_set_mark             },
        { "copyRegion",           js_copy_region          },
        { "killRegion",           js_kill_region          },
        { "yank",                 js_yank                 },
        { "find",                 js_find                 },
        { "replace",              js_replace              },
        { NULL, NULL }
    };

    for (int i = 0; methods[i].name; i++) {
        duk_push_c_function(ctx, methods[i].fn, DUK_VARARGS);
        duk_put_prop_string(ctx, -2, methods[i].name);
    }

    duk_put_global_string(ctx, "editor");
    return ctx;
}

int script_eval_buffer(duk_context *ctx, Buffer *buf, char *result, int result_len) {
    if (!ctx || !buf) return -1;

    /* Compute total length of all lines joined by newlines */
    size_t total = 0;
    for (int i = 0; i < buf->num_lines; i++) {
        total += strlen(buf->lines[i]) + 1; /* +1 for '\n' or '\0' */
    }

    char *code = malloc(total + 1);
    if (!code) {
        if (result) snprintf(result, result_len, "Error: out of memory");
        return -1;
    }

    size_t pos = 0;
    for (int i = 0; i < buf->num_lines; i++) {
        size_t len = strlen(buf->lines[i]);
        memcpy(code + pos, buf->lines[i], len);
        pos += len;
        if (i < buf->num_lines - 1)
            code[pos++] = '\n';
    }
    code[pos] = '\0';

    int rc = script_eval(ctx, code, result, result_len);
    free(code);
    return rc;
}

void script_destroy(duk_context *ctx) {
    if (ctx) duk_destroy_heap(ctx);
}

int script_eval(duk_context *ctx, const char *code, char *result, int result_len) {
    if (!ctx || !code) return -1;

    int rc = duk_peval_string(ctx, code);
    if (rc != 0) {
        /* Error */
        const char *err = duk_safe_to_string(ctx, -1);
        if (result) snprintf(result, result_len, "Error: %s", err);
        duk_pop(ctx);
        return -1;
    }

    /* Success: get result as string */
    const char *res = duk_safe_to_string(ctx, -1);
    if (result) snprintf(result, result_len, "%s", res);
    duk_pop(ctx);
    return 0;
}
