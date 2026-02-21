#ifndef UI_H
#define UI_H

#include "editor.h"

void ui_init(Editor *e);
void ui_cleanup(void);
void ui_refresh(Editor *e);
void ui_draw_buffer(Editor *e);
void ui_draw_modeline(Editor *e);
void ui_draw_minibuf(Editor *e);
void ui_resize(Editor *e);
int ui_get_key(Editor *e);

/* Color pair definitions */
#define COLOR_MODELINE  1
#define COLOR_MSG       2
#define COLOR_SHELL     3
#define COLOR_HELP      4

#endif /* UI_H */
