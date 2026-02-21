#ifndef KEYS_H
#define KEYS_H

#include "editor.h"

void handle_key(Editor *e, int key);

/* Control key helper */
#define CTRL(x) ((x) & 0x1f)

#endif /* KEYS_H */
