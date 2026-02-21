#include "ui.h"
#include "editor.h"
#include "buffer.h"
#include "shell_buf.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#define MODELINE_BUF_SIZE 1024
/* prompt (max 128) + input (max 512) + nul */
#define MINIBUF_DISPLAY_SIZE (128 + 512 + 1)
/* Raw escape-sequence buffer: up to 3 bytes + null terminator */
#define RAW_KEY_BUF_SIZE 4
/* Max line length when reading files */
#define MAX_LINE_LENGTH 4096

void ui_init(Editor *e) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    meta(stdscr, TRUE);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(COLOR_MODELINE, COLOR_BLACK, COLOR_WHITE);
        init_pair(COLOR_MSG,      COLOR_GREEN, -1);
        init_pair(COLOR_SHELL,    COLOR_CYAN,  -1);
        init_pair(COLOR_HELP,     COLOR_YELLOW, COLOR_BLUE);
    }

    /* Compute window sizes */
    e->edit_height = LINES - 2;
    e->edit_width  = COLS;

    e->edit_win    = newwin(e->edit_height, e->edit_width, 0, 0);
    e->modeline_win = newwin(1, COLS, LINES - 2, 0);
    e->minibuf_win  = newwin(1, COLS, LINES - 1, 0);

    keypad(e->edit_win, TRUE);
    keypad(e->minibuf_win, TRUE);

    /* Use timeout on edit_win for shell polling */
    wtimeout(e->edit_win, 50);
}

void ui_cleanup(void) {
    endwin();
}

void ui_resize(Editor *e) {
    endwin();
    refresh();
    clear();

    e->edit_height = LINES - 2;
    e->edit_width  = COLS;

    wresize(e->edit_win,    e->edit_height, e->edit_width);
    wresize(e->modeline_win, 1, COLS);
    wresize(e->minibuf_win,  1, COLS);

    mvwin(e->edit_win,     0, 0);
    mvwin(e->modeline_win, LINES - 2, 0);
    mvwin(e->minibuf_win,  LINES - 1, 0);

    /* Notify shell buffers of new size */
    for (int i = 0; i < e->num_buffers; i++) {
        if (e->buffers[i]->is_shell) {
            shell_buf_resize(e->buffers[i], e->edit_height, e->edit_width);
        }
    }
}

void ui_draw_buffer(Editor *e) {
    Buffer *buf = editor_current_buffer(e);
    if (!buf) return;

    werase(e->edit_win);

    /* Adjust scroll so cursor is visible */
    if (buf->cursor_line < buf->top_line)
        buf->top_line = buf->cursor_line;
    if (buf->cursor_line >= buf->top_line + e->edit_height)
        buf->top_line = buf->cursor_line - e->edit_height + 1;

    int screen_row = 0;
    for (int ln = buf->top_line; ln < buf->num_lines && screen_row < e->edit_height;
         ln++, screen_row++) {
        char *line = buf->lines[ln];
        if (!line) continue;
        int len = (int)strlen(line);

        /* Truncate display to window width */
        int disp_len = len < e->edit_width ? len : e->edit_width - 1;
        if (buf->is_shell) {
            wattron(e->edit_win, COLOR_PAIR(COLOR_SHELL));
        }
        mvwaddnstr(e->edit_win, screen_row, 0, line, disp_len);
        if (buf->is_shell) {
            wattroff(e->edit_win, COLOR_PAIR(COLOR_SHELL));
        }
    }

    /* Position cursor */
    int cur_screen_row = buf->cursor_line - buf->top_line;
    int cur_screen_col = buf->cursor_col;
    if (cur_screen_col >= e->edit_width) cur_screen_col = e->edit_width - 1;
    if (cur_screen_row >= 0 && cur_screen_row < e->edit_height) {
        wmove(e->edit_win, cur_screen_row, cur_screen_col);
    }

    /* Show help overlay if requested */
    if (e->show_help) {
        static const char *help_lines[] = {
            " myfancyeditor key bindings ",
            " C-f/C-b/C-n/C-p  : move cursor     ",
            " C-a / C-e        : line start/end  ",
            " C-k              : kill line        ",
            " C-y              : yank             ",
            " C-d              : delete forward   ",
            " C-x C-s          : save file        ",
            " C-x C-f          : find file        ",
            " C-x C-c          : quit             ",
            " C-x b            : switch buffer    ",
            " C-x k            : kill buffer      ",
            " C-x s            : open shell       ",
            " M-x              : execute command  ",
            " C-g              : cancel           ",
            " C-l              : redraw           ",
            " F1               : toggle help      ",
            NULL
        };
        wattron(e->edit_win, COLOR_PAIR(COLOR_HELP) | A_BOLD);
        int row = 1;
        for (int i = 0; help_lines[i]; i++, row++) {
            mvwaddstr(e->edit_win, row, 2, help_lines[i]);
        }
        wattroff(e->edit_win, COLOR_PAIR(COLOR_HELP) | A_BOLD);
    }

    wnoutrefresh(e->edit_win);
}

void ui_draw_modeline(Editor *e) {
    Buffer *buf = editor_current_buffer(e);
    werase(e->modeline_win);
    wbkgd(e->modeline_win, COLOR_PAIR(COLOR_MODELINE) | A_REVERSE);
    wattron(e->modeline_win, COLOR_PAIR(COLOR_MODELINE) | A_REVERSE);

    char modeline[MODELINE_BUF_SIZE];
    if (buf) {
        const char *fname = buf->filename ? buf->filename : "no file";
        const char *mod   = buf->modified ? "**" : "--";
        const char *stype = buf->is_shell ? "[shell] " : "";
        snprintf(modeline, sizeof(modeline),
                 "  %s%-20s  %s  %s  L%d C%d  [%d/%d]",
                 stype, buf->name, mod, fname,
                 buf->cursor_line + 1, buf->cursor_col + 1,
                 e->current_buffer + 1, e->num_buffers);
    } else {
        snprintf(modeline, sizeof(modeline), "  No buffer");
    }

    /* Pad to full width, capped at buffer size */
    int len = (int)strlen(modeline);
    int fill_width = COLS - 1;
    if (fill_width >= (int)sizeof(modeline))
        fill_width = (int)sizeof(modeline) - 2;
    if (len < fill_width) {
        memset(modeline + len, ' ', fill_width - len);
        modeline[fill_width] = '\0';
    }
    mvwaddnstr(e->modeline_win, 0, 0, modeline, fill_width);

    wattroff(e->modeline_win, COLOR_PAIR(COLOR_MODELINE) | A_REVERSE);
    wnoutrefresh(e->modeline_win);
}

void ui_draw_minibuf(Editor *e) {
    werase(e->minibuf_win);

    if (e->minibuf_active) {
        char display[MINIBUF_DISPLAY_SIZE];
        snprintf(display, sizeof(display), "%s%s",
                 e->minibuf_prompt, e->minibuf_input);
        mvwaddnstr(e->minibuf_win, 0, 0, display, COLS - 1);
        int cursor_x = (int)strlen(e->minibuf_prompt) + e->minibuf_len;
        if (cursor_x >= COLS) cursor_x = COLS - 1;
        wmove(e->minibuf_win, 0, cursor_x);
    } else if (e->message[0]) {
        wattron(e->minibuf_win, COLOR_PAIR(COLOR_MSG));
        mvwaddnstr(e->minibuf_win, 0, 0, e->message, COLS - 1);
        wattroff(e->minibuf_win, COLOR_PAIR(COLOR_MSG));
    }

    wnoutrefresh(e->minibuf_win);
}

void ui_refresh(Editor *e) {
    ui_draw_buffer(e);
    ui_draw_modeline(e);
    ui_draw_minibuf(e);
    /* Move cursor to proper window */
    if (e->minibuf_active) {
        int cx = (int)strlen(e->minibuf_prompt) + e->minibuf_len;
        if (cx >= COLS) cx = COLS - 1;
        wmove(e->minibuf_win, 0, cx);
        wnoutrefresh(e->minibuf_win);
    }
    doupdate();
}

int ui_get_key(Editor *e) {
    /* Poll shell buffer fds while waiting for keyboard */
    fd_set rfds;
    int maxfd = -1;

    FD_ZERO(&rfds);
    for (int i = 0; i < e->num_buffers; i++) {
        if (e->buffers[i]->is_shell && e->buffers[i]->pty_fd >= 0) {
            FD_SET(e->buffers[i]->pty_fd, &rfds);
            if (e->buffers[i]->pty_fd > maxfd)
                maxfd = e->buffers[i]->pty_fd;
        }
    }

    /* Use wgetch with timeout for input; shell fd polling via select */
    if (maxfd >= 0) {
        struct timeval tv = {0, 20000}; /* 20ms */
        int stdin_fd = fileno(stdin);
        FD_SET(stdin_fd, &rfds);
        if (stdin_fd > maxfd) maxfd = stdin_fd;

        int ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ret > 0) {
            /* Read shell output */
            for (int i = 0; i < e->num_buffers; i++) {
                if (e->buffers[i]->is_shell && e->buffers[i]->pty_fd >= 0 &&
                    FD_ISSET(e->buffers[i]->pty_fd, &rfds)) {
                    shell_buf_read(e->buffers[i]);
                }
            }
            if (FD_ISSET(stdin_fd, &rfds)) {
                return wgetch(e->minibuf_active ? e->minibuf_win : e->edit_win);
            }
            return ERR; /* only shell data */
        }
        return ERR;
    }

    return wgetch(e->minibuf_active ? e->minibuf_win : e->edit_win);
}
