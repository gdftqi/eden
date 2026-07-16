CXX         := g++
CC          := gcc
WIN_CC      := x86_64-w64-mingw32-gcc
CXXFLAGS    := -O2 -g -Wall -Wextra -I../../Adam/include -MMD -MP -std=c++20
CFLAGS      := -O2 -fPIC -Wall -Wextra -I../../Adam/include
WIN_CFLAGS  := -O2 -Wall -Wextra -I../../Adam/include

# moses/ 是干净的部署包: 二进制 + config + bpf 对象 + docker-compose + flame_build.sh;
# 编译中间产物(.o/.d)全部放 obj/, 不进部署包
BUILD_DIR   := moses
OBJ_DIR     := obj
LIB         := ../../Adam/build/libadam.a
BUILD_START := $(shell date +%s)

# 两个 BPF 对象都从 Adam 的 build/bpf 拷过来:
#   kcp.bpf.o      — sk_reuseport 路由 (按 conv 分流到 worker)
#   envelope.bpf.o — XDP MAC 校验 (envelope MAC DoS 过滤)
KCP_BPF_SRC      := ../../Adam/build/bpf/kcp.bpf.o
KCP_BPF_DST      := $(BUILD_DIR)/kcp.bpf.o
ENVELOPE_BPF_SRC := ../../Adam/build/bpf/envelope.bpf.o
ENVELOPE_BPF_DST := $(BUILD_DIR)/envelope.bpf.o

SERVER      := $(BUILD_DIR)/moses
LIBKCP_SO   := libkcp.so
CONFIG_DST  := $(BUILD_DIR)/config.yml
COMPOSE_DST := $(BUILD_DIR)/docker-compose.yml
FLAME_DST   := $(BUILD_DIR)/flame_build.sh

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)

.PHONY: all clean

all: $(SERVER) $(LIBKCP_SO) $(KCP_BPF_DST) $(ENVELOPE_BPF_DST) $(CONFIG_DST) $(COMPOSE_DST) $(FLAME_DST)
	@echo "==> [moses] build done at $$(date '+%F %T'), elapsed $$(($$(date +%s) - $(BUILD_START)))s"

# Adam 主库 / BPF 对象:任一过期就回 Adam make 重新构建
$(LIB):
	@$(MAKE) -C ../../Adam

$(KCP_BPF_SRC):
	@$(MAKE) -C ../../Adam

$(ENVELOPE_BPF_SRC):
	@$(MAKE) -C ../../Adam

# C++ moses (-lprofiler: gperftools CPU profiler, config.flame 开启时采样)
$(SERVER): $(OBJECTS) $(LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LIB) -lpthread -lprofiler -o $@

# Linux libkcp.so:给 test_kcp.py(在本目录)用 ctypes 加载
$(LIBKCP_SO): ../../Adam/src/kcp/ikcp.c
	$(CC) $(CFLAGS) -shared -o $@ $<

# 部署包内容: bpf 对象 / config / docker-compose / flame_build.sh 全拷进 $(BUILD_DIR),
# moses 运行时 cwd 即部署目录, 全部用相对路径加载
$(KCP_BPF_DST): $(KCP_BPF_SRC) | $(BUILD_DIR)
	cp $< $@

$(ENVELOPE_BPF_DST): $(ENVELOPE_BPF_SRC) | $(BUILD_DIR)
	cp $< $@

$(CONFIG_DST): config.yml | $(BUILD_DIR)
	cp $< $@

$(COMPOSE_DST): docker-compose.yml | $(BUILD_DIR)
	cp $< $@

$(FLAME_DST): flame_build.sh | $(BUILD_DIR)
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
