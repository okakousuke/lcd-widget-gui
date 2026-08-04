# lcd-widget-gui — build the terminal-mock demo
# 記事と同じフラグでビルドします。

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic
TARGET   = demo
SRCS     = gui.c demo.c
HDRS     = gui.h

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
