#include "file_ops.h"
#include "buffer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int file_load(Buffer *buf, const char *filename) {
    return buffer_load_file(buf, filename);
}

int file_save(Buffer *buf) {
    return buffer_save_file(buf);
}
