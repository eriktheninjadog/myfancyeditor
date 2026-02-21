#ifndef SHELL_BUF_H
#define SHELL_BUF_H

#include "editor.h"
#include "buffer.h"

Buffer *shell_buf_create(Editor *e, const char *shell);
void shell_buf_write(Buffer *buf, const char *data, int len);
void shell_buf_read(Buffer *buf);
void shell_buf_resize(Buffer *buf, int rows, int cols);

#endif /* SHELL_BUF_H */
