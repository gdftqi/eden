CXX      := g++
CXXFLAGS := -O2 -g -Wall -Wextra -std=c++20 -I../../../include -I/usr/local/include -DSPDLOG_COMPILED_LIB

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/x25519
LIB       := ../../../build/libtyphon.a

.PHONY: all clean

all: $(TARGET)

# libtyphon.a 过期/缺失就回顶层重建 (libsodium 已打包在里面)
$(LIB):
	@$(MAKE) -C ../../..

# libelf + libz 是 libtyphon.a 里 libbpf 的动态依赖; libsodium 已在 .a 内, 无需 -lsodium
$(TARGET): main.cpp $(LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) main.cpp $(LIB) -lpthread -lelf -lz -o $@

$(BUILD_DIR):
	@mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
