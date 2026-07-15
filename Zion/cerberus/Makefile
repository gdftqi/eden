CXX         := g++
CC          := gcc
WIN_CC      := x86_64-w64-mingw32-gcc
CXXFLAGS    := -O2 -g -Wall -Wextra -I../../include -MMD -MP -std=c++20
CFLAGS      := -O2 -fPIC -Wall -Wextra -I../../include
WIN_CFLAGS  := -O2 -Wall -Wextra -I../../include

# cerberus/ 是干净的部署包: 二进制 + config + bpf 对象 + docker-compose;
# 编译中间产物(.o/.d)全部放 obj/, 不进部署包
BUILD_DIR   := cerberus
OBJ_DIR     := obj
LIB         := ../../build/libtyphon.a
BUILD_START := $(shell date +%s)

# 两个 BPF 对象都从顶层 build/bpf 拷过来:
#   kcp.bpf.o      — sk_reuseport 路由 (按 conv 分流到 worker)
#   envelope.bpf.o — XDP MAC 校验 (envelope MAC DoS 过滤)
KCP_BPF_SRC      := ../../build/bpf/kcp.bpf.o
KCP_BPF_DST      := $(BUILD_DIR)/kcp.bpf.o
ENVELOPE_BPF_SRC := ../../build/bpf/envelope.bpf.o
ENVELOPE_BPF_DST := $(BUILD_DIR)/envelope.bpf.o

SERVER      := $(BUILD_DIR)/cerberus
LIBKCP_SO   := libkcp.so
CONFIG_DST  := $(BUILD_DIR)/config.yml
COMPOSE_DST := $(BUILD_DIR)/docker-compose.yml

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)

.PHONY: all clean

all: $(SERVER) $(LIBKCP_SO) $(KCP_BPF_DST) $(ENVELOPE_BPF_DST) $(CONFIG_DST) $(COMPOSE_DST)
	@echo "==> [cerberus] build done at $$(date '+%F %T'), elapsed $$(($$(date +%s) - $(BUILD_START)))s"

# typhon 主库 / BPF 对象:任一过期就回顶层 make 重新构建
$(LIB):
	@$(MAKE) -C ../..

$(KCP_BPF_SRC):
	@$(MAKE) -C ../..

$(ENVELOPE_BPF_SRC):
	@$(MAKE) -C ../..

# C++ cerberus
$(SERVER): $(OBJECTS) $(LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LIB) -lpthread -o $@

# Linux libkcp.so:给 test_kcp.py(在本目录)用 ctypes 加载
$(LIBKCP_SO): ../../src/kcp/ikcp.c
	$(CC) $(CFLAGS) -shared -o $@ $<

# 部署包内容: bpf 对象 / config / docker-compose 全拷进 $(BUILD_DIR),
# cerberus 运行时 cwd 即部署目录, 全部用相对路径加载
$(KCP_BPF_DST): $(KCP_BPF_SRC) | $(BUILD_DIR)
	cp $< $@

$(ENVELOPE_BPF_DST): $(ENVELOPE_BPF_SRC) | $(BUILD_DIR)
	cp $< $@

$(CONFIG_DST): config.yml | $(BUILD_DIR)
	cp $< $@

$(COMPOSE_DST): docker-compose.yml | $(BUILD_DIR)
	cp $< $@

# C++ object 编译(中间产物进 obj/)
$(OBJ_DIR)/%.o: %.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

$(OBJ_DIR):
	@mkdir -p $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR) $(OBJ_DIR) $(LIBKCP_SO)
