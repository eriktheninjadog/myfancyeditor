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

/* --- Mark / region helpers --- */

void buffer_set_mark(Buffer *buf) {
    buf->mark_line   = buf->cursor_line;
    buf->mark_col    = buf->cursor_col;
    buf->mark_active = 1;
}

/* Compute the canonical start/end of the active region. */
static void region_bounds(Buffer *buf,
                           int *sl, int *sc, int *el, int *ec) {
    if (buf->mark_line < buf->cursor_line ||
        (buf->mark_line == buf->cursor_line &&
         buf->mark_col  <= buf->cursor_col)) {
        *sl = buf->mark_line;   *sc = buf->mark_col;
        *el = buf->cursor_line; *ec = buf->cursor_col;
    } else {
        *sl = buf->cursor_line; *sc = buf->cursor_col;
        *el = buf->mark_line;   *ec = buf->mark_col;
    }
}

/* Return a newly-allocated string containing the region text, or NULL. */
char *buffer_get_region(Buffer *buf) {
    if (!buf->mark_active) return NULL;
    int sl, sc, el, ec;
    region_bounds(buf, &sl, &sc, &el, &ec);

    size_t total = 0;
    if (sl == el) {
        total = (size_t)(ec - sc);
    } else {
        total = strlen(buf->lines[sl]) - sc + 1; /* +1 for newline */
        for (int i = sl + 1; i < el; i++)
            total += strlen(buf->lines[i]) + 1;
        total += (size_t)ec;
    }

    char *out = malloc(total + 1);
    if (!out) return NULL;

    size_t pos = 0;
    if (sl == el) {
        memcpy(out, buf->lines[sl] + sc, (size_t)(ec - sc));
        pos = (size_t)(ec - sc);
    } else {
        int flen = (int)strlen(buf->lines[sl]) - sc;
        memcpy(out, buf->lines[sl] + sc, (size_t)flen);
        pos += (size_t)flen;
        out[pos++] = '\n';
        for (int i = sl + 1; i < el; i++) {
            int len = (int)strlen(buf->lines[i]);
            memcpy(out + pos, buf->lines[i], (size_t)len);
            pos += (size_t)len;
            out[pos++] = '\n';
        }
        memcpy(out + pos, buf->lines[el], (size_t)ec);
        pos += (size_t)ec;
    }
    out[pos] = '\0';
    return out;
}

/* Copy region into kill ring without modifying the buffer. */
void buffer_copy_region(Buffer *buf, char **kill_ring) {
    if (!buf->mark_active) return;
    char *region = buffer_get_region(buf);
    if (!region) return;
    if (kill_ring) {
        free(*kill_ring);
        *kill_ring = region;
    } else {
        free(region);
    }
    buf->mark_active = 0;
}

/* Cut region into kill ring, removing the text from the buffer. */
void buffer_kill_region(Buffer *buf, char **kill_ring) {
    if (!buf->mark_active) return;
    char *region = buffer_get_region(buf);
    if (!region) return;
    if (kill_ring) {
        free(*kill_ring);
        *kill_ring = region;
    } else {
        free(region);
    }

    int sl, sc, el, ec;
    region_bounds(buf, &sl, &sc, &el, &ec);

    buf->cursor_line = sl;
    buf->cursor_col  = sc;
    buf->mark_active = 0;

    if (sl == el) {
        char *line = buf->lines[sl];
        int len = (int)strlen(line);
        memmove(line + sc, line + ec, (size_t)(len - ec + 1));
    } else {
        char *first = buf->lines[sl];
        char *last  = buf->lines[el];
        int   first_prefix   = sc;
        int   last_suffix_len = (int)strlen(last) - ec;
        char *merged = malloc((size_t)(first_prefix + last_suffix_len + 1));
        if (!merged) return;
        memcpy(merged, first, (size_t)first_prefix);
        memcpy(merged + first_prefix, last + ec, (size_t)(last_suffix_len + 1));
        free(buf->lines[sl]);
        buf->lines[sl] = merged;
        for (int i = sl + 1; i <= el; i++) free(buf->lines[i]);
        int remove = el - sl;
        memmove(&buf->lines[sl + 1],
                &buf->lines[el + 1],
                sizeof(char *) * (size_t)(buf->num_lines - el - 1));
        buf->num_lines -= remove;
    }
    buf->modified = 1;
}

/* --- Search and replace --- */

/*
 * Search forward from one position past the cursor (wrapping around).
 * Moves cursor to the start of the match if found.
 * Returns 1 if found, 0 otherwise.
 */
int buffer_search_forward(Buffer *buf, const char *query) {
    if (!query || !*query) return 0;
    int nlines = buf->num_lines;
    for (int i = 0; i < nlines; i++) {
        int ln = (buf->cursor_line + i) % nlines;
        char *line = buf->lines[ln];
        int start_col = (i == 0) ? buf->cursor_col + 1 : 0;
        int len = (int)strlen(line);
        if (start_col > len) continue;
        char *found = strstr(line + start_col, query);
        if (found) {
            buf->cursor_line = ln;
            buf->cursor_col  = (int)(found - line);
            return 1;
        }
    }
    return 0;
}

/*
 * Replace all occurrences of `search` with `replace_str` in the buffer.
 * Returns the number of replacements made.
 */
int buffer_replace_all(Buffer *buf, const char *search,
                        const char *replace_str) {
    if (!search || !*search) return 0;
    int slen = (int)strlen(search);
    int rlen = replace_str ? (int)strlen(replace_str) : 0;
    int count = 0;

    for (int ln = 0; ln < buf->num_lines; ln++) {
        char *line = buf->lines[ln];

        /* Count occurrences on this line */
        int occ = 0;
        for (char *p = line; (p = strstr(p, search)) != NULL; p += slen)
            occ++;
        if (occ == 0) continue;

        int old_len = (int)strlen(line);
        int new_len = old_len + occ * (rlen - slen);
        char *newline = malloc((size_t)(new_len + 1));
        if (!newline) continue;

        char *src = line;
        char *dst = newline;
        char *found;
        while ((found = strstr(src, search)) != NULL) {
            int prefix = (int)(found - src);
            memcpy(dst, src, (size_t)prefix);
            dst += prefix;
            if (rlen > 0) {
                memcpy(dst, replace_str, (size_t)rlen);
                dst += rlen;
            }
            src = found + slen;
            count++;
        }
        int rem = old_len - (int)(src - line);
        memcpy(dst, src, (size_t)(rem + 1)); /* +1 for NUL */

        free(buf->lines[ln]);
        buf->lines[ln] = newline;
    }
    if (count > 0) buf->modified = 1;
    return count;
}
