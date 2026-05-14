CC := gcc
CFLAGS := -O2 -fPIC -Wall -Wextra -I../../include

TARGET := libkcp.so

# Windows DLL 交叉编译用 mingw-w64
# sudo apt-get install -y mingw-w64
WIN_CC     := x86_64-w64-mingw32-gcc
WIN_CFLAGS := -O2 -Wall -Wextra -I../../include
WIN_TARGET := libkcp.dll

.PHONY: all clean win

all: $(TARGET)

$(TARGET): ../../src/kcp/ikcp.c
	$(CC) $(CFLAGS) -shared -o $@ $<

win: $(WIN_TARGET)

$(WIN_TARGET): ../../src/kcp/ikcp.c
	$(WIN_CC) $(WIN_CFLAGS) -shared -o $@ $< -static-libgcc -Wl,--out-implib,libkcp.dll.a

clean:
	rm -f $(TARGET) $(WIN_TARGET) libkcp.dll.a
