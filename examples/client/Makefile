CC := gcc
CFLAGS := -O2 -fPIC -Wall -Wextra -I../../include

TARGET := libkcp.so

.PHONY: all clean

all: $(TARGET)

$(TARGET): ../../src/kcp/ikcp.c
	$(CC) $(CFLAGS) -shared -o $@ $<

clean:
	rm -f $(TARGET)
