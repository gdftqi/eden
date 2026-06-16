CXX         := g++
CC          := gcc
WIN_CC      := x86_64-w64-mingw32-gcc
CXXFLAGS    := -O2 -g -Wall -Wextra -I../../include -MMD -MP -std=c++20
CFLAGS      := -O2 -fPIC -Wall -Wextra -I../../include
WIN_CFLAGS  := -O2 -Wall -Wextra -I../../include

BUILD_DIR   := build
LIB         := ../../build/libtyphon.a
BUILD_START := $(shell date +%s)

# 两个 BPF 对象都从顶层 build/bpf 拷过来:
#   kcp.bpf.o      — sk_reuseport 路由 (按 conv 分流到 worker)
#   envelope.bpf.o — XDP MAC 校验 (envelope MAC DoS 过滤)
KCP_BPF_SRC      := ../../build/bpf/kcp.bpf.o
KCP_BPF_DST      := $(BUILD_DIR)/kcp.bpf.o
ENVELOPE_BPF_SRC := ../../build/bpf/envelope.bpf.o
ENVELOPE_BPF_DST := $(BUILD_DIR)/envelope.bpf.o

SERVER      := $(BUILD_DIR)/server
LIBKCP_SO   := libkcp.so

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all clean

all: $(SERVER) $(LIBKCP_SO) $(KCP_BPF_DST) $(ENVELOPE_BPF_DST)
	@echo "==> [kcp_echo] build done at $$(date '+%F %T'), elapsed $$(($$(date +%s) - $(BUILD_START)))s"

# typhon 主库 / BPF 对象:任一过期就回顶层 make 重新构建
$(LIB):
	@$(MAKE) -C ../..

$(KCP_BPF_SRC):
	@$(MAKE) -C ../..

$(ENVELOPE_BPF_SRC):
	@$(MAKE) -C ../..

# C++ echo server
$(SERVER): $(OBJECTS) $(LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LIB) -lpthread -o $@

# Linux libkcp.so:给 test_kcp.py(在本目录)用 ctypes 加载
$(LIBKCP_SO): ../../src/kcp/ikcp.c
	$(CC) $(CFLAGS) -shared -o $@ $<

# 把 BPF 对象拷到本地 build,server 启动时方便用相对路径加载 (build/kcp.bpf.o 等)
$(KCP_BPF_DST): $(KCP_BPF_SRC) | $(BUILD_DIR)
	cp $< $@

$(ENVELOPE_BPF_DST): $(ENVELOPE_BPF_SRC) | $(BUILD_DIR)
	cp $< $@

# C++ object 编译
$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR) $(LIBKCP_SO)
