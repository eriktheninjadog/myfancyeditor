#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "editor.h"
#include "ui.h"
#include "keys.h"
#include "shell_buf.h"

static void handle_sigwinch(int sig) {
    (void)sig;
    /* ncurses will deliver KEY_RESIZE on next wgetch */
}

static void handle_sigchld(int sig) {
    (void)sig;
    /* Reap zombie shell processes */
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0);
}

int main(void) {
    /* Set up signals */
    signal(SIGWINCH, handle_sigwinch);
    signal(SIGCHLD,  handle_sigchld);
    signal(SIGPIPE,  SIG_IGN);

    /* Create editor */
    Editor *e = editor_create();
    if (!e) {
        fprintf(stderr, "Failed to create editor\n");
        return 1;
    }
    g_editor = e;

    /* Initialize UI */
    ui_init(e);

    /* Main loop */
    while (e->running) {
        /* Poll shell buffers and refresh */
        ui_refresh(e);

        int key = ui_get_key(e);
        if (key == ERR) {
            /* Timeout or only shell data received; loop again */
            continue;
        }

        handle_key(e, key);
    }

    ui_cleanup();

    /* Kill any shell children */
    for (int i = 0; i < e->num_buffers; i++) {
        if (e->buffers[i]->is_shell && e->buffers[i]->shell_pid > 0) {
            kill(e->buffers[i]->shell_pid, SIGTERM);
        }
    }

    editor_destroy(e);
    return 0;
}
