#include "buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INITIAL_LINES 64
#define INITIAL_LINE_CAP 16
#define MAX_LINE_LENGTH 4096

Buffer *buffer_create(const char *name) {
    Buffer *buf = calloc(1, sizeof(Buffer));
    if (!buf) return NULL;

    buf->name = strdup(name);
    buf->capacity = INITIAL_LINES;
    buf->lines = malloc(sizeof(char *) * buf->capacity);
    if (!buf->lines) { free(buf->name); free(buf); return NULL; }

    buf->lines[0] = strdup("");
    buf->num_lines = 1;
    buf->cursor_line = 0;
    buf->cursor_col = 0;
    buf->top_line = 0;
    buf->modified = 0;
    buf->is_shell = 0;
    buf->pty_fd = -1;
    buf->shell_pid = -1;
    buf->filename = NULL;
    buf->kill_ring_entry = NULL;
    return buf;
}

void buffer_destroy(Buffer *buf) {
    if (!buf) return;
    for (int i = 0; i < buf->num_lines; i++) free(buf->lines[i]);
    free(buf->lines);
    free(buf->name);
    free(buf->filename);
    free(buf->kill_ring_entry);
    free(buf);
}

static int buffer_grow(Buffer *buf) {
    if (buf->num_lines >= buf->capacity) {
        int new_cap = buf->capacity * 2;
        char **tmp = realloc(buf->lines, sizeof(char *) * new_cap);
        if (!tmp) return -1;
        buf->lines = tmp;
        buf->capacity = new_cap;
    }
    return 0;
}

void buffer_ensure_line(Buffer *buf, int line) {
    while (buf->num_lines <= line) {
        if (buffer_grow(buf) != 0) return;
        buf->lines[buf->num_lines++] = strdup("");
    }
}

void buffer_clamp_cursor(Buffer *buf) {
    if (buf->cursor_line < 0) buf->cursor_line = 0;
    if (buf->cursor_line >= buf->num_lines) buf->cursor_line = buf->num_lines - 1;
    int linelen = (int)strlen(buf->lines[buf->cursor_line]);
    if (buf->cursor_col < 0) buf->cursor_col = 0;
    if (buf->cursor_col > linelen) buf->cursor_col = linelen;
}

void buffer_insert_char(Buffer *buf, char c) {
    buffer_clamp_cursor(buf);

    if (c == '\n') {
        /* Split line at cursor */
        char *cur_line = buf->lines[buf->cursor_line];
        int col = buf->cursor_col;
        char *rest = strdup(cur_line + col);
        cur_line[col] = '\0';

        /* Make room for new line */
        if (buffer_grow(buf) != 0) { free(rest); return; }
        /* Shift lines down */
        memmove(&buf->lines[buf->cursor_line + 2],
                &buf->lines[buf->cursor_line + 1],
                sizeof(char *) * (buf->num_lines - buf->cursor_line - 1));
        buf->num_lines++;
        buf->lines[buf->cursor_line + 1] = rest;
        buf->cursor_line++;
        buf->cursor_col = 0;
    } else {
        char *line = buf->lines[buf->cursor_line];
        int len = (int)strlen(line);
        char *newline = malloc(len + 2);
        memcpy(newline, line, buf->cursor_col);
        newline[buf->cursor_col] = c;
        memcpy(newline + buf->cursor_col + 1, line + buf->cursor_col,
               len - buf->cursor_col + 1);
        free(buf->lines[buf->cursor_line]);
        buf->lines[buf->cursor_line] = newline;
        buf->cursor_col++;
    }
    buf->modified = 1;
}

void buffer_delete_char(Buffer *buf) {
    /* Backspace: delete char before cursor */
    buffer_clamp_cursor(buf);
    if (buf->cursor_col > 0) {
        char *line = buf->lines[buf->cursor_line];
        int len = (int)strlen(line);
        memmove(line + buf->cursor_col - 1,
                line + buf->cursor_col,
                len - buf->cursor_col + 1);
        buf->cursor_col--;
        buf->modified = 1;
    } else if (buf->cursor_line > 0) {
        /* Merge with previous line */
        char *prev = buf->lines[buf->cursor_line - 1];
        char *cur  = buf->lines[buf->cursor_line];
        int prev_len = (int)strlen(prev);
        int cur_len  = (int)strlen(cur);
        char *merged = malloc(prev_len + cur_len + 1);
        memcpy(merged, prev, prev_len);
        memcpy(merged + prev_len, cur, cur_len + 1);
        free(buf->lines[buf->cursor_line - 1]);
        free(buf->lines[buf->cursor_line]);
        buf->lines[buf->cursor_line - 1] = merged;
        memmove(&buf->lines[buf->cursor_line],
                &buf->lines[buf->cursor_line + 1],
                sizeof(char *) * (buf->num_lines - buf->cursor_line - 1));
        buf->num_lines--;
        buf->cursor_line--;
        buf->cursor_col = prev_len;
        buf->modified = 1;
    }
}

void buffer_delete_forward(Buffer *buf) {
    /* Delete char at cursor (C-d) */
    buffer_clamp_cursor(buf);
    char *line = buf->lines[buf->cursor_line];
    int len = (int)strlen(line);
    if (buf->cursor_col < len) {
        memmove(line + buf->cursor_col,
                line + buf->cursor_col + 1,
                len - buf->cursor_col);
        buf->modified = 1;
    } else if (buf->cursor_line < buf->num_lines - 1) {
        /* Merge with next line */
        char *cur  = buf->lines[buf->cursor_line];
        char *next = buf->lines[buf->cursor_line + 1];
        int cur_len  = (int)strlen(cur);
        int next_len = (int)strlen(next);
        char *merged = malloc(cur_len + next_len + 1);
        memcpy(merged, cur, cur_len);
        memcpy(merged + cur_len, next, next_len + 1);
        free(buf->lines[buf->cursor_line]);
        free(buf->lines[buf->cursor_line + 1]);
        buf->lines[buf->cursor_line] = merged;
        memmove(&buf->lines[buf->cursor_line + 1],
                &buf->lines[buf->cursor_line + 2],
                sizeof(char *) * (buf->num_lines - buf->cursor_line - 2));
        buf->num_lines--;
        buf->modified = 1;
    }
}

void buffer_kill_line(Buffer *buf, char **kill_ring) {
    buffer_clamp_cursor(buf);
    char *line = buf->lines[buf->cursor_line];
    int len = (int)strlen(line);

    if (buf->cursor_col < len) {
        /* Kill to end of line */
        if (kill_ring) {
            free(*kill_ring);
            *kill_ring = strdup(line + buf->cursor_col);
        }
        line[buf->cursor_col] = '\0';
        buf->modified = 1;
    } else if (buf->cursor_line < buf->num_lines - 1) {
        /* Kill the newline */
        if (kill_ring) {
            free(*kill_ring);
            *kill_ring = strdup("\n");
        }
        /* Merge with next line */
        char *cur  = buf->lines[buf->cursor_line];
        char *next = buf->lines[buf->cursor_line + 1];
        int cur_len  = (int)strlen(cur);
        int next_len = (int)strlen(next);
        char *merged = malloc(cur_len + next_len + 1);
        memcpy(merged, cur, cur_len);
        memcpy(merged + cur_len, next, next_len + 1);
        free(buf->lines[buf->cursor_line]);
        free(buf->lines[buf->cursor_line + 1]);
        buf->lines[buf->cursor_line] = merged;
        memmove(&buf->lines[buf->cursor_line + 1],
                &buf->lines[buf->cursor_line + 2],
                sizeof(char *) * (buf->num_lines - buf->cursor_line - 2));
        buf->num_lines--;
        buf->modified = 1;
    }
}

void buffer_yank(Buffer *buf, const char *kill_ring) {
    if (!kill_ring) return;
    for (const char *p = kill_ring; *p; p++) {
        buffer_insert_char(buf, *p);
    }
}

void buffer_move_cursor(Buffer *buf, int dline, int dcol) {
    buf->cursor_line += dline;
    buf->cursor_col  += dcol;
    buffer_clamp_cursor(buf);
}

void buffer_move_bol(Buffer *buf) {
    buf->cursor_col = 0;
}

void buffer_move_eol(Buffer *buf) {
    buffer_clamp_cursor(buf);
    buf->cursor_col = (int)strlen(buf->lines[buf->cursor_line]);
}

int buffer_load_file(Buffer *buf, const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    /* Clear existing content */
    for (int i = 0; i < buf->num_lines; i++) free(buf->lines[i]);
    buf->num_lines = 0;

    char linebuf[MAX_LINE_LENGTH];
    while (fgets(linebuf, sizeof(linebuf), f)) {
        /* Strip trailing newline */
        int len = (int)strlen(linebuf);
        if (len > 0 && linebuf[len - 1] == '\n') {
            linebuf[len - 1] = '\0';
        }
        /* Grow lines array if needed */
        if (buf->num_lines >= buf->capacity) {
            int new_cap = buf->capacity * 2;
            char **tmp = realloc(buf->lines, sizeof(char *) * new_cap);
            if (!tmp) { fclose(f); return -1; }
            buf->lines = tmp;
            buf->capacity = new_cap;
        }
        buf->lines[buf->num_lines++] = strdup(linebuf);
    }

    if (buf->num_lines == 0) {
        buf->lines[0] = strdup("");
        buf->num_lines = 1;
    }

    fclose(f);

    free(buf->filename);
    buf->filename = strdup(filename);
    buf->cursor_line = 0;
    buf->cursor_col  = 0;
    buf->top_line    = 0;
    buf->modified    = 0;
    return 0;
}

int buffer_save_file(Buffer *buf) {
    if (!buf->filename) return -1;
    FILE *f = fopen(buf->filename, "w");
    if (!f) return -1;
    for (int i = 0; i < buf->num_lines; i++) {
        fputs(buf->lines[i], f);
        fputc('\n', f);
    }
    fclose(f);
    buf->modified = 0;
    return 0;
}

void buffer_append_string(Buffer *buf, const char *str) {
    if (!str || !*str) return;

    /* Process character by character, handling \r\n */
    for (const char *p = str; *p; p++) {
        char c = *p;
        if (c == '\r') continue;  /* ignore CR */
        if (c == '\n') {
            /* Move to next line */
            buf->cursor_line = buf->num_lines;
            if (buf->num_lines >= buf->capacity) {
                int new_cap = buf->capacity * 2;
                char **tmp = realloc(buf->lines, sizeof(char *) * new_cap);
                if (!tmp) return;
                buf->lines = tmp;
                buf->capacity = new_cap;
            }
            buf->lines[buf->num_lines++] = strdup("");
            buf->cursor_col = 0;
        } else if (c == '\b' || c == 127) {
            /* Backspace in shell output */
            if (buf->num_lines > 0) {
                char *line = buf->lines[buf->num_lines - 1];
                int len = (int)strlen(line);
                if (len > 0) line[len - 1] = '\0';
            }
        } else {
            /* Append char to last line */
            char *line = buf->lines[buf->num_lines - 1];
            int len = (int)strlen(line);
            char *newline = malloc(len + 2);
            memcpy(newline, line, len);
            newline[len] = c;
            newline[len + 1] = '\0';
            free(buf->lines[buf->num_lines - 1]);
            buf->lines[buf->num_lines - 1] = newline;
        }
    }
    buf->cursor_line = buf->num_lines - 1;
    buf->cursor_col  = (int)strlen(buf->lines[buf->cursor_line]);
    buf->modified = 1;
}

void buffer_scroll_to_end(Buffer *buf) {
    buf->cursor_line = buf->num_lines - 1;
    buf->cursor_col  = (int)strlen(buf->lines[buf->cursor_line]);
}
