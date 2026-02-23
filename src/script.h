#ifndef SCRIPT_H
#define SCRIPT_H

#include <duktape.h>

typedef struct Editor Editor;

typedef struct Buffer Buffer;

duk_context *script_init(Editor *e);
void script_destroy(duk_context *ctx);
int script_eval(duk_context *ctx, const char *code, char *result, int result_len);
int script_eval_buffer(duk_context *ctx, Buffer *buf, char *result, int result_len);

#endif /* SCRIPT_H */
