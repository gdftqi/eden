CC    := gcc
CXX   := g++
AR    := ar
CLANG := clang

# 构建开始时刻 (解析时立即求值), 供 all 完成时算总耗时
BUILD_START := $(shell date +%s)

BUILD_DIR := build
LIB       := $(BUILD_DIR)/libadam.a

# 静态库捆绑进 libadam.a
SPDLOG_LIB        := /usr/local/lib/libspdlog.a
LIBBPF_LIB        := /usr/lib/x86_64-linux-gnu/libbpf.a
ELF_LIB           := /usr/lib/x86_64-linux-gnu/libelf.a
Z_LIB             := /usr/lib/x86_64-linux-gnu/libz.a
ZSTD_LIB          := /usr/lib/x86_64-linux-gnu/libzstd.a
YAMLCPP_LIB       := /usr/local/lib/libyaml-cpp.a
LIBSODIUM_LIB     := /usr/local/lib/libsodium.a
LIBCURL_LIB       := /usr/local/lib/libcurl.a
SSL_LIB           := /usr/lib/x86_64-linux-gnu/libssl.a
CRYPTO_LIB        := /usr/lib/x86_64-linux-gnu/libcrypto.a
JITTER_LIB        := /usr/lib/x86_64-linux-gnu/libjitterentropy.a
SIMDJSON_LIB      := /usr/local/lib/libsimdjson.a
MIMALLOC_OVERRIDE := /usr/local/lib/mimalloc-3.2/mimalloc.o
ABSL_LIBS         := $(patsubst -l%,/usr/local/lib/lib%.a,$(filter -labsl_%,$(shell pkg-config --libs --static absl_flat_hash_map)))


BUNDLED_LIBS      := $(SPDLOG_LIB) $(LIBBPF_LIB) $(ELF_LIB) $(Z_LIB) $(ZSTD_LIB) $(YAMLCPP_LIB) $(LIBSODIUM_LIB) $(ABSL_LIBS) \
                     $(LIBCURL_LIB) $(SSL_LIB) $(CRYPTO_LIB) $(JITTER_LIB) $(SIMDJSON_LIB)

INCLUDES := -Iinclude -I/usr/local/include -I/usr/local/include/mimalloc-3.2

CFLAGS   := -O2 -g -Wall -Wextra $(INCLUDES) -MMD -MP
CXXFLAGS := -O2 -g -Wall -Wextra $(INCLUDES) -MMD -MP -std=c++20 -DSPDLOG_COMPILED_LIB

C_SOURCES   := $(filter-out %.bpf.c, $(shell find src -name '*.c'))
CPP_SOURCES := $(shell find src -name '*.cpp')
OBJECTS     := $(C_SOURCES:src/%.c=$(BUILD_DIR)/%.o) \
               $(CPP_SOURCES:src/%.cpp=$(BUILD_DIR)/%.o)

# BPF 程序：clang -target bpf 单独编译，**不**进 libadam.a，运行时由 libbpf 加载
BPF_SOURCES := $(shell find src -name '*.bpf.c')
BPF_OBJECTS := $(BPF_SOURCES:src/%.bpf.c=$(BUILD_DIR)/%.bpf.o)

.PHONY: all lib bpf

all: lib bpf
	@echo "==> [adam] build done at $$(date '+%F %T'), elapsed $$(($$(date +%s) - $(BUILD_START)))s"
lib: $(LIB)
bpf: $(BPF_OBJECTS)

# 用 ar 的 MRI script 把 adam 自身 .o 和第三方 .a 合成一个 libadam.a
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
