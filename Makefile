CC    := gcc
CXX   := g++
AR    := ar
CLANG := clang

BUILD_DIR := build
LIB       := $(BUILD_DIR)/libtyphon.a

# 静态库捆绑进 libtyphon.a。
# mimalloc.o：全功能 override 对象（mi_* API + C malloc/free + C++ new/delete），不能再叠 libmimalloc.a。
# libbpf.a：用户态 BPF 加载库，依赖 libelf + libz（动态，由 examples 链）。
# libyaml-cpp.a：YAML 配置解析（纯静态，无额外动态依赖）；头在 /usr/local/include，已被 INCLUDES 覆盖。
SPDLOG_LIB        := /usr/local/lib/libspdlog.a
LIBBPF_LIB        := /usr/lib/x86_64-linux-gnu/libbpf.a
YAMLCPP_LIB       := /usr/local/lib/libyaml-cpp.a
LIBSODIUM_LIB     := /usr/local/lib/libsodium.a
MIMALLOC_OVERRIDE := /usr/local/lib/mimalloc-3.2/mimalloc.o
BUNDLED_LIBS      := $(SPDLOG_LIB) $(LIBBPF_LIB) $(YAMLCPP_LIB) $(LIBSODIUM_LIB)

INCLUDES := -Iinclude -I/usr/local/include -I/usr/local/include/mimalloc-3.2

# SPDLOG_COMPILED_LIB: 切到 spdlog 的 compiled 模式
#   - typhon 自身 .o 不再 inline spdlog,改为 extern 调用
#   - libspdlog.a 被 addlib 进 libtyphon.a,extern 符号在 libtyphon.a 内自满足
#   - 下游链 libtyphon.a 时,不需要再额外 -lspdlog
CFLAGS   := -O2 -g -Wall -Wextra $(INCLUDES) -MMD -MP
CXXFLAGS := -O2 -g -Wall -Wextra $(INCLUDES) -MMD -MP -std=c++20 -DSPDLOG_COMPILED_LIB

C_SOURCES   := $(filter-out %.bpf.c, $(shell find src -name '*.c'))
CPP_SOURCES := $(shell find src -name '*.cpp')
OBJECTS     := $(C_SOURCES:src/%.c=$(BUILD_DIR)/%.o) \
               $(CPP_SOURCES:src/%.cpp=$(BUILD_DIR)/%.o)

# BPF 程序：clang -target bpf 单独编译，**不**进 libtyphon.a，运行时由 libbpf 加载
BPF_SOURCES := $(shell find src -name '*.bpf.c')
BPF_OBJECTS := $(BPF_SOURCES:src/%.bpf.c=$(BUILD_DIR)/%.bpf.o)

.PHONY: all lib bpf

all: lib bpf
lib: $(LIB)
bpf: $(BPF_OBJECTS)

# 用 ar 的 MRI script 把 typhon 自身 .o 和第三方 .a 合成一个 libtyphon.a
$(LIB): $(OBJECTS) $(BUNDLED_LIBS) $(MIMALLOC_OVERRIDE)
	@mkdir -p $(@D)
	@( echo 'create $@'; \
	   for o in $(OBJECTS) $(MIMALLOC_OVERRIDE); do echo "addmod $$o"; done; \
	   for l in $(BUNDLED_LIBS); do echo "addlib $$l"; done; \
	   echo 'save'; echo 'end' \
	 ) | $(AR) -M

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.bpf.o: src/%.bpf.c
	@mkdir -p $(@D)
	$(CLANG) -O2 -g -Wall -target bpf -I/usr/include/x86_64-linux-gnu -c $< -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR)
