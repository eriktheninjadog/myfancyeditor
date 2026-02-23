#include "editor.h"
#include "buffer.h"
#include "script.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

Editor *g_editor = NULL;

Editor *editor_create(void) {
    Editor *e = calloc(1, sizeof(Editor));
    if (!e) return NULL;

    e->running = 1;
    e->kill_ring = NULL;
    e->pending_ctrl_x = 0;
    e->pending_meta = 0;
    e->minibuf_active = 0;
    e->minibuf_len = 0;
    e->show_help = 0;

    /* Create scratch buffer */
    Buffer *scratch = buffer_create("*scratch*");
    if (scratch) {
        buffer_append_string(scratch,
            ";; Welcome to myfancyeditor\n"
            ";; C-x C-f: open file  C-x C-s: save  C-x b: switch buffer\n"
            ";; C-x C-c: quit       C-x s: shell   M-x: execute command\n"
            ";; C-x e: run macro    M-x run: run buffer as JS\n"
            ";; F1: help\n");
        scratch->modified = 0;
        e->buffers[e->num_buffers++] = scratch;
    }

    e->current_buffer = 0;
    e->js_ctx = script_init(e);

    return e;
}

void editor_destroy(Editor *e) {
    if (!e) return;
    for (int i = 0; i < e->num_buffers; i++) {
        buffer_destroy(e->buffers[i]);
    }
    free(e->kill_ring);
    if (e->js_ctx) script_destroy(e->js_ctx);
    free(e);
}

Buffer *editor_current_buffer(Editor *e) {
    if (e->num_buffers == 0) return NULL;
    return e->buffers[e->current_buffer];
}

Buffer *editor_find_buffer(Editor *e, const char *name) {
    for (int i = 0; i < e->num_buffers; i++) {
        if (strcmp(e->buffers[i]->name, name) == 0)
            return e->buffers[i];
    }
    return NULL;
}

Buffer *editor_new_buffer(Editor *e, const char *name) {
    if (e->num_buffers >= MAX_BUFFERS) return NULL;
    Buffer *buf = buffer_create(name);
    if (!buf) return NULL;
    e->buffers[e->num_buffers++] = buf;
    return buf;
}

void editor_kill_buffer(Editor *e, int idx) {
    if (idx < 0 || idx >= e->num_buffers) return;
    buffer_destroy(e->buffers[idx]);
    memmove(&e->buffers[idx], &e->buffers[idx + 1],
            sizeof(Buffer *) * (e->num_buffers - idx - 1));
    e->num_buffers--;
    if (e->num_buffers == 0) {
        e->buffers[0] = buffer_create("*scratch*");
        e->num_buffers = 1;
    }
    if (e->current_buffer >= e->num_buffers)
        e->current_buffer = e->num_buffers - 1;
}

void editor_switch_to_buffer(Editor *e, const char *name) {
    for (int i = 0; i < e->num_buffers; i++) {
        if (strcmp(e->buffers[i]->name, name) == 0) {
            e->current_buffer = i;
            editor_set_message(e, "Switched to buffer: %s", name);
            return;
        }
    }
    /* Create new buffer */
    Buffer *buf = editor_new_buffer(e, name);
    if (buf) {
        e->current_buffer = e->num_buffers - 1;
        editor_set_message(e, "Created new buffer: %s", name);
    }
}

void editor_set_message(Editor *e, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->message, sizeof(e->message), fmt, ap);
    va_end(ap);
}

void editor_open_file(Editor *e, const char *filename) {
    if (!filename || !*filename) return;

    /* Check if buffer for this file already open */
    for (int i = 0; i < e->num_buffers; i++) {
        if (e->buffers[i]->filename &&
            strcmp(e->buffers[i]->filename, filename) == 0) {
            e->current_buffer = i;
            editor_set_message(e, "Switched to existing buffer for %s", filename);
            return;
        }
    }

    /* Create new buffer */
    const char *bname = strrchr(filename, '/');
    bname = bname ? bname + 1 : filename;

    Buffer *buf = editor_new_buffer(e, bname);
    if (!buf) {
        editor_set_message(e, "Too many buffers open");
        return;
    }
    if (buffer_load_file(buf, filename) == 0) {
        e->current_buffer = e->num_buffers - 1;
        editor_set_message(e, "Opened %s", filename);
    } else {
        /* New file */
        buf->filename = strdup(filename);
        e->current_buffer = e->num_buffers - 1;
        editor_set_message(e, "New file: %s", filename);
    }
}

void editor_save_current(Editor *e) {
    Buffer *buf = editor_current_buffer(e);
    if (!buf) return;
    if (buf->is_shell) {
        editor_set_message(e, "Cannot save shell buffer");
        return;
    }
    if (!buf->filename) {
        editor_set_message(e, "No filename -- use C-x C-w to write to file");
        return;
    }
    if (buffer_save_file(buf) == 0) {
        editor_set_message(e, "Wrote %s", buf->filename);
    } else {
        editor_set_message(e, "Error saving %s", buf->filename);
    }
}

void editor_start_minibuf(Editor *e, const char *prompt,
                          void (*done_cb)(Editor *, const char *)) {
    strncpy(e->minibuf_prompt, prompt, sizeof(e->minibuf_prompt) - 1);
    e->minibuf_prompt[sizeof(e->minibuf_prompt) - 1] = '\0';
    e->minibuf_input[0] = '\0';
    e->minibuf_len = 0;
    e->minibuf_active = 1;
    e->minibuf_done_cb = done_cb;
}
