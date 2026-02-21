#include "shell_buf.h"
#include "editor.h"
#include "buffer.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>

#define DEFAULT_TERM_ROWS 24
#define DEFAULT_TERM_COLS 80

Buffer *shell_buf_create(Editor *e, const char *shell) {
    static int shell_count = 0;
    char bufname[64];
    snprintf(bufname, sizeof(bufname), "*shell-%d*", ++shell_count);

    Buffer *buf = editor_new_buffer(e, bufname);
    if (!buf) return NULL;

    buf->is_shell = 1;

    struct winsize ws;
    ws.ws_row = (unsigned short)(e->edit_height > 0 ? e->edit_height : DEFAULT_TERM_ROWS);
    ws.ws_col = (unsigned short)(e->edit_width > 0 ? e->edit_width : DEFAULT_TERM_COLS);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    int master_fd;
    pid_t pid = forkpty(&master_fd, NULL, NULL, &ws);
    if (pid < 0) {
        editor_set_message(e, "forkpty failed: %s", strerror(errno));
        editor_kill_buffer(e, e->num_buffers - 1);
        return NULL;
    }

    if (pid == 0) {
        /* Child: exec shell */
        const char *sh = shell ? shell : "/bin/bash";
        char *argv[] = { (char *)sh, NULL };
        execv(sh, argv);
        /* fallback */
        execl("/bin/sh", "/bin/sh", NULL);
        _exit(1);
    }

    /* Parent */
    buf->pty_fd   = master_fd;
    buf->shell_pid = pid;

    /* Set non-blocking */
    int flags = fcntl(master_fd, F_GETFL, 0);
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);

    e->current_buffer = e->num_buffers - 1;
    editor_set_message(e, "Shell started in %s (pid %d)", bufname, (int)pid);
    return buf;
}

void shell_buf_write(Buffer *buf, const char *data, int len) {
    if (!buf || buf->pty_fd < 0 || len <= 0) return;
    int written = 0;
    while (written < len) {
        int n = (int)write(buf->pty_fd, data + written, len - written);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            break;
        }
        written += n;
    }
}

void shell_buf_read(Buffer *buf) {
    if (!buf || buf->pty_fd < 0) return;

    char tmp[4096];
    int n;

    while ((n = (int)read(buf->pty_fd, tmp, sizeof(tmp) - 1)) > 0) {
        tmp[n] = '\0';
        buffer_append_string(buf, tmp);
    }

    if (n < 0 && errno != EAGAIN && errno != EINTR) {
        /* Shell died */
        buffer_append_string(buf, "\n[Process exited]\n");
        close(buf->pty_fd);
        buf->pty_fd = -1;
        if (buf->shell_pid > 0) {
            waitpid(buf->shell_pid, NULL, WNOHANG);
            buf->shell_pid = -1;
        }
    }
}

void shell_buf_resize(Buffer *buf, int rows, int cols) {
    if (!buf || buf->pty_fd < 0) return;
    struct winsize ws;
    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    ioctl(buf->pty_fd, TIOCSWINSZ, &ws);
    if (buf->shell_pid > 0) {
        kill(buf->shell_pid, SIGWINCH);
    }
}
