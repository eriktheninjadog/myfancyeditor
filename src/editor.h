#ifndef EDITOR_H
#define EDITOR_H

#include <ncurses.h>
#include <duktape.h>
#include "buffer.h"

#define MAX_BUFFERS 32

typedef struct Editor Editor;

struct Editor {
    Buffer *buffers[MAX_BUFFERS];
    int num_buffers;
    int current_buffer;
    int running;

    WINDOW *edit_win;
    WINDOW *modeline_win;
    WINDOW *minibuf_win;
    int edit_height;
    int edit_width;

    char *kill_ring;

    int pending_ctrl_x;
    int pending_meta;

    char minibuf_input[512];
    int minibuf_len;
    char minibuf_prompt[128];
    int minibuf_active;
    void (*minibuf_done_cb)(Editor *, const char *);

    duk_context *js_ctx;

    int show_help;

    char message[512];
};

Editor *editor_create(void);
void editor_destroy(Editor *e);
Buffer *editor_current_buffer(Editor *e);
Buffer *editor_find_buffer(Editor *e, const char *name);
Buffer *editor_new_buffer(Editor *e, const char *name);
void editor_kill_buffer(Editor *e, int idx);
void editor_switch_to_buffer(Editor *e, const char *name);
void editor_set_message(Editor *e, const char *fmt, ...);
void editor_open_file(Editor *e, const char *filename);
void editor_save_current(Editor *e);
void editor_start_minibuf(Editor *e, const char *prompt,
                          void (*done_cb)(Editor *, const char *));

/* Global editor pointer for signal/JS callbacks */
extern Editor *g_editor;

#endif /* EDITOR_H */
