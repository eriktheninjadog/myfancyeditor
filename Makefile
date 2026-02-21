CC = gcc
CFLAGS = -Wall -Wextra -g -Isrc
LDFLAGS = -lncursesw -lduktape -lutil -lpthread

SRCS = src/main.c src/editor.c src/buffer.c src/ui.c src/keys.c \
       src/file_ops.c src/shell_buf.c src/script.c

OBJS = $(SRCS:.c=.o)
TARGET = myfancyeditor

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
