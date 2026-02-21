#ifndef BUFFER_H
#define BUFFER_H

#include <sys/types.h>

typedef struct Buffer {
    char **lines;
    int num_lines;
    int capacity;
    char *name;
    char *filename;
    int modified;
    int cursor_line;
    int cursor_col;
    int top_line;
    int is_shell;
    int pty_fd;
    pid_t shell_pid;
    char *kill_ring_entry;
    int mark_line;
    int mark_col;
    int mark_active;
} Buffer;

Buffer *buffer_create(const char *name);
void buffer_destroy(Buffer *buf);
void buffer_insert_char(Buffer *buf, char c);
void buffer_delete_char(Buffer *buf);
void buffer_delete_forward(Buffer *buf);
void buffer_kill_line(Buffer *buf, char **kill_ring);
void buffer_yank(Buffer *buf, const char *kill_ring);
void buffer_move_cursor(Buffer *buf, int dline, int dcol);
void buffer_move_bol(Buffer *buf);
void buffer_move_eol(Buffer *buf);
int buffer_load_file(Buffer *buf, const char *filename);
int buffer_save_file(Buffer *buf);
void buffer_append_string(Buffer *buf, const char *str);
void buffer_scroll_to_end(Buffer *buf);
void buffer_ensure_line(Buffer *buf, int line);
void buffer_clamp_cursor(Buffer *buf);

/* Mark / region operations */
void buffer_set_mark(Buffer *buf);
char *buffer_get_region(Buffer *buf);
void buffer_copy_region(Buffer *buf, char **kill_ring);
void buffer_kill_region(Buffer *buf, char **kill_ring);

/* Search and replace */
int buffer_search_forward(Buffer *buf, const char *query);
int buffer_replace_all(Buffer *buf, const char *search, const char *replace_str);

#endif /* BUFFER_H */
