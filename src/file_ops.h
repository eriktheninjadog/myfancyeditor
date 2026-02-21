#ifndef FILE_OPS_H
#define FILE_OPS_H

#include "editor.h"

int file_load(Buffer *buf, const char *filename);
int file_save(Buffer *buf);

#endif /* FILE_OPS_H */
