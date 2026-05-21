CXX         := g++
CC          := gcc
WIN_CC      := x86_64-w64-mingw32-gcc
CXXFLAGS    := -O2 -g -Wall -Wextra -I../../include -MMD -MP -std=c++20
CFLAGS      := -O2 -fPIC -Wall -Wextra -I../../include
WIN_CFLAGS  := -O2 -Wall -Wextra -I../../include

BUILD_DIR   := build
LIB         := ../../build/libtyphon.a
BPF_OBJ_SRC := ../../build/bpf/kcp.bpf.o
BPF_OBJ_DST := $(BUILD_DIR)/kcp.bpf.o

SERVER      := $(BUILD_DIR)/server
LIBKCP_SO   := libkcp.so
LIBKCP_DLL  := libkcp.dll

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all clean

all: $(SERVER) $(LIBKCP_SO) $(LIBKCP_DLL) $(BPF_OBJ_DST)

# typhon 主库 / BPF 对象:任一过期就回顶层 make 重新构建
$(LIB):
	@$(MAKE) -C ../..

$(BPF_OBJ_SRC):
	@$(MAKE) -C ../..

# C++ echo server
# spdlog / mimalloc / libbpf 已被打进 libtyphon.a;libelf + libz 是 libbpf 的动态依赖
$(SERVER): $(OBJECTS) $(LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LIB) -lpthread -lelf -lz -o $@

# Linux libkcp.so:给 test_kcp.py(在本目录)用 ctypes 加载
$(LIBKCP_SO): ../../src/kcp/ikcp.c
	$(CC) $(CFLAGS) -shared -o $@ $<

# Windows libkcp.dll:跨编译,给 Windows Python 客户端 copy 走用
$(LIBKCP_DLL): ../../src/kcp/ikcp.c
	$(WIN_CC) $(WIN_CFLAGS) -shared -o $@ $< -static-libgcc -Wl,--out-implib,$@.a

# 把 BPF 对象拷到本地 build,server 启动时方便用相对路径加载
$(BPF_OBJ_DST): $(BPF_OBJ_SRC) | $(BUILD_DIR)
	cp $< $@

# C++ object 编译
$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR) $(LIBKCP_SO) $(LIBKCP_DLL) $(LIBKCP_DLL).a
