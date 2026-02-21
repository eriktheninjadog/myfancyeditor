#include "keys.h"
#include "editor.h"
#include "buffer.h"
#include "ui.h"
#include "shell_buf.h"
#include "script.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/* Escape-sequence raw buffer: up to 3 bytes + null terminator */
#define RAW_KEY_BUF_SIZE 4

/* Forward declarations for minibuf callbacks */
static void cb_find_file(Editor *e, const char *input);
static void cb_switch_buffer(Editor *e, const char *input);
static void cb_kill_buffer(Editor *e, const char *input);
static void cb_mx_command(Editor *e, const char *input);

static void cb_find_file(Editor *e, const char *input) {
    editor_open_file(e, input);
}

static void cb_switch_buffer(Editor *e, const char *input) {
    editor_switch_to_buffer(e, input);
}

static void cb_kill_buffer(Editor *e, const char *input) {
    for (int i = 0; i < e->num_buffers; i++) {
        if (strcmp(e->buffers[i]->name, input) == 0) {
            editor_kill_buffer(e, i);
            editor_set_message(e, "Killed buffer: %s", input);
            return;
        }
    }
    editor_set_message(e, "No buffer named: %s", input);
}

static void cb_mx_command(Editor *e, const char *input) {
    if (strcmp(input, "eval-js") == 0) {
        /* Bare "eval-js" with no code: show usage hint */
        editor_set_message(e, "Usage: M-x eval-js <js-code>  e.g.: eval-js 1+2");
    } else if (strcmp(input, "open-shell") == 0) {
        shell_buf_create(e, "/bin/bash");
        editor_set_message(e, "Opened shell buffer");
    } else if (strcmp(input, "list-buffers") == 0) {
        Buffer *lb = editor_find_buffer(e, "*Buffer List*");
        if (!lb) lb = editor_new_buffer(e, "*Buffer List*");
        if (lb) {
            /* Clear and rebuild */
            for (int i = 0; i < lb->num_lines; i++) free(lb->lines[i]);
            lb->num_lines = 0;
            lb->lines[0] = strdup("Buffer List:");
            lb->num_lines = 1;
            for (int i = 0; i < e->num_buffers; i++) {
                char line[256];
                int n = snprintf(line, sizeof(line), "  [%d] %s%s",
                         i + 1, e->buffers[i]->name,
                         e->buffers[i]->modified ? " (modified)" : "");
                if (e->buffers[i]->filename && n < (int)sizeof(line) - 1) {
                    snprintf(line + n, sizeof(line) - n, " -- %s",
                             e->buffers[i]->filename);
                }
                if (lb->num_lines >= lb->capacity) {
                    lb->capacity *= 2;
                    lb->lines = realloc(lb->lines, sizeof(char *) * lb->capacity);
                }
                lb->lines[lb->num_lines++] = strdup(line);
            }
            lb->modified = 0;
            /* Switch to buffer list */
            for (int i = 0; i < e->num_buffers; i++) {
                if (e->buffers[i] == lb) { e->current_buffer = i; break; }
            }
        }
    } else if (strncmp(input, "eval-js ", 8) == 0) {
        char result[512] = {0};
        script_eval(e->js_ctx, input + 8, result, sizeof(result));
        editor_set_message(e, "JS: %s", result);
    } else {
        editor_set_message(e, "Unknown command: %s", input);
    }
}

/* Handle minibuffer input */
static void handle_minibuf_key(Editor *e, int key) {
    if (key == CTRL('g') || key == 27 /* ESC */) {
        e->minibuf_active = 0;
        e->minibuf_done_cb = NULL;
        e->minibuf_input[0] = '\0';
        e->minibuf_len = 0;
        editor_set_message(e, "Quit");
        return;
    }

    if (key == '\n' || key == '\r' || key == KEY_ENTER) {
        e->minibuf_active = 0;
        void (*cb)(Editor *, const char *) = e->minibuf_done_cb;
        char input_copy[512];
        strncpy(input_copy, e->minibuf_input, sizeof(input_copy) - 1);
        input_copy[sizeof(input_copy) - 1] = '\0';
        e->minibuf_done_cb = NULL;
        e->minibuf_input[0] = '\0';
        e->minibuf_len = 0;
        if (cb) cb(e, input_copy);
        return;
    }

    if (key == KEY_BACKSPACE || key == 127 || key == CTRL('h')) {
        if (e->minibuf_len > 0) {
            e->minibuf_input[--e->minibuf_len] = '\0';
        }
        return;
    }

    if (key >= 32 && key < 127 && e->minibuf_len < 510) {
        e->minibuf_input[e->minibuf_len++] = (char)key;
        e->minibuf_input[e->minibuf_len] = '\0';
    }
}

/* Handle C-x prefix sequence */
static void handle_ctrl_x_key(Editor *e, int key) {
    e->pending_ctrl_x = 0;

    switch (key) {
    case CTRL('s'):
        editor_save_current(e);
        break;
    case CTRL('f'):
        editor_start_minibuf(e, "Find file: ", cb_find_file);
        break;
    case CTRL('c'):
        e->running = 0;
        break;
    case 'b':
        editor_start_minibuf(e, "Switch to buffer: ", cb_switch_buffer);
        break;
    case 'k':
        editor_start_minibuf(e, "Kill buffer: ", cb_kill_buffer);
        break;
    case 's':
        shell_buf_create(e, "/bin/bash");
        editor_set_message(e, "Opened shell buffer");
        break;
    case '2':
        editor_set_message(e, "Window splitting not yet implemented");
        break;
    default:
        editor_set_message(e, "C-x %c is undefined", key);
        break;
    }
}

/* Handle M- (meta/ESC) prefix */
static void handle_meta_key(Editor *e, int key) {
    e->pending_meta = 0;
    Buffer *buf = editor_current_buffer(e);

    switch (key) {
    case 'x':
    case 'X':
        editor_start_minibuf(e, "M-x ", cb_mx_command);
        break;
    case 'f': /* M-f: forward word */
        if (buf) {
            char *line = buf->lines[buf->cursor_line];
            int len = (int)strlen(line);
            /* Skip non-word chars then word chars */
            while (buf->cursor_col < len && line[buf->cursor_col] == ' ')
                buf->cursor_col++;
            while (buf->cursor_col < len && line[buf->cursor_col] != ' ')
                buf->cursor_col++;
        }
        break;
    case 'b': /* M-b: backward word */
        if (buf) {
            char *line = buf->lines[buf->cursor_line];
            if (buf->cursor_col > 0) buf->cursor_col--;
            while (buf->cursor_col > 0 && line[buf->cursor_col] == ' ')
                buf->cursor_col--;
            while (buf->cursor_col > 0 && line[buf->cursor_col - 1] != ' ')
                buf->cursor_col--;
        }
        break;
    case '<': /* M-<: beginning of buffer */
        if (buf) {
            buf->cursor_line = 0;
            buf->cursor_col  = 0;
            buf->top_line    = 0;
        }
        break;
    case '>': /* M->: end of buffer */
        if (buf) {
            buf->cursor_line = buf->num_lines - 1;
            buf->cursor_col  = (int)strlen(buf->lines[buf->cursor_line]);
        }
        break;
    case 'd': /* M-d: kill word forward */
        if (buf) {
            char *line = buf->lines[buf->cursor_line];
            int len = (int)strlen(line);
            int start = buf->cursor_col;
            while (buf->cursor_col < len && line[buf->cursor_col] == ' ')
                buf->cursor_col++;
            while (buf->cursor_col < len && line[buf->cursor_col] != ' ')
                buf->cursor_col++;
            /* Delete from start to cursor_col */
            memmove(line + start, line + buf->cursor_col,
                    len - buf->cursor_col + 1);
            buf->cursor_col = start;
            buf->modified = 1;
        }
        break;
    default:
        editor_set_message(e, "M-%c is undefined", key);
        break;
    }
}

void handle_key(Editor *e, int key) {
    /* Handle minibuf mode */
    if (e->minibuf_active) {
        handle_minibuf_key(e, key);
        return;
    }

    /* Handle C-x prefix */
    if (e->pending_ctrl_x) {
        handle_ctrl_x_key(e, key);
        return;
    }

    /* Handle Meta prefix */
    if (e->pending_meta) {
        handle_meta_key(e, key);
        return;
    }

    Buffer *buf = editor_current_buffer(e);
    if (!buf) return;

    /* Shell buffer: send raw input to pty (except C-x) */
    if (buf->is_shell && buf->pty_fd >= 0) {
        char raw[RAW_KEY_BUF_SIZE];
        int rawlen = 0;
        int passthru = 1;

        switch (key) {
        case CTRL('x'):
            e->pending_ctrl_x = 1;
            editor_set_message(e, "C-x-");
            passthru = 0;
            break;
        case KEY_UP:    raw[0] = '\033'; raw[1] = '['; raw[2] = 'A'; rawlen = 3; break;
        case KEY_DOWN:  raw[0] = '\033'; raw[1] = '['; raw[2] = 'B'; rawlen = 3; break;
        case KEY_RIGHT: raw[0] = '\033'; raw[1] = '['; raw[2] = 'C'; rawlen = 3; break;
        case KEY_LEFT:  raw[0] = '\033'; raw[1] = '['; raw[2] = 'D'; rawlen = 3; break;
        case KEY_BACKSPACE: raw[0] = 127; rawlen = 1; break;
        case '\n': case '\r': raw[0] = '\r'; rawlen = 1; break;
        default:
            if (key >= 0 && key < 256) {
                raw[0] = (char)key;
                rawlen = 1;
            } else {
                passthru = 0;
            }
            break;
        }
        if (passthru && rawlen > 0) {
            shell_buf_write(buf, raw, rawlen);
        }
        return;
    }

    /* Normal buffer key handling */
    switch (key) {
    /* Movement */
    case KEY_UP:
    case CTRL('p'):
        buffer_move_cursor(buf, -1, 0);
        break;
    case KEY_DOWN:
    case CTRL('n'):
        buffer_move_cursor(buf, 1, 0);
        break;
    case KEY_LEFT:
    case CTRL('b'):
        if (buf->cursor_col > 0) {
            buf->cursor_col--;
        } else if (buf->cursor_line > 0) {
            buf->cursor_line--;
            buffer_move_eol(buf);
        }
        break;
    case KEY_RIGHT:
    case CTRL('f'):
        {
            int linelen = (int)strlen(buf->lines[buf->cursor_line]);
            if (buf->cursor_col < linelen) {
                buf->cursor_col++;
            } else if (buf->cursor_line < buf->num_lines - 1) {
                buf->cursor_line++;
                buf->cursor_col = 0;
            }
        }
        break;
    case CTRL('a'):
    case KEY_HOME:
        buffer_move_bol(buf);
        break;
    case CTRL('e'):
    case KEY_END:
        buffer_move_eol(buf);
        break;

    /* Page up/down */
    case KEY_PPAGE:
        buf->cursor_line -= e->edit_height;
        buf->top_line    -= e->edit_height;
        if (buf->top_line < 0) buf->top_line = 0;
        buffer_clamp_cursor(buf);
        break;
    case KEY_NPAGE:
        buf->cursor_line += e->edit_height;
        buf->top_line    += e->edit_height;
        if (buf->top_line >= buf->num_lines)
            buf->top_line = buf->num_lines - 1;
        buffer_clamp_cursor(buf);
        break;

    /* Editing */
    case KEY_BACKSPACE:
    case 127:
    case CTRL('h'):
        buffer_delete_char(buf);
        break;
    case CTRL('d'):
    case KEY_DC:
        buffer_delete_forward(buf);
        break;
    case CTRL('k'):
        buffer_kill_line(buf, &e->kill_ring);
        break;
    case CTRL('y'):
        buffer_yank(buf, e->kill_ring);
        break;
    case '\t':
        buffer_insert_char(buf, '\t');
        break;
    case '\n':
    case '\r':
    case KEY_ENTER:
        buffer_insert_char(buf, '\n');
        break;

    /* C-x prefix */
    case CTRL('x'):
        e->pending_ctrl_x = 1;
        editor_set_message(e, "C-x-");
        break;

    /* ESC = Meta prefix */
    case 27:
        e->pending_meta = 1;
        break;

    /* M-x via Alt+x on some terminals sends as 'x' after ESC */
    case CTRL('g'):
        e->pending_ctrl_x = 0;
        e->pending_meta   = 0;
        editor_set_message(e, "Quit");
        break;

    case CTRL('l'):
        clearok(e->edit_win, TRUE);
        editor_set_message(e, "");
        break;

    case KEY_F(1):
        e->show_help = !e->show_help;
        break;

    case KEY_RESIZE:
        ui_resize(e);
        break;

    default:
        if (key >= 32 && key < 256) {
            buffer_insert_char(buf, (char)key);
            /* Clear message after typing */
            if (e->message[0]) e->message[0] = '\0';
        }
        break;
    }
}
