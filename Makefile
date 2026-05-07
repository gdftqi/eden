CC  := gcc
CXX := g++
AR  := ar

BUILD_DIR := build
LIB       := $(BUILD_DIR)/libtyphon.a

# 静态库捆绑进 libtyphon.a，使用方只需链 libtyphon.a 一个文件。
# mimalloc.o 是 mimalloc 的"全功能 override 对象"，自己就含完整 mi_* API + C malloc/free
# + C++ new/delete 覆盖，**不能再叠加 libmimalloc.a 否则符号重复**。
SPDLOG_LIB        := /usr/local/lib/libspdlog.a
MIMALLOC_OVERRIDE := /usr/local/lib/mimalloc-3.2/mimalloc.o
BUNDLED_LIBS      := $(SPDLOG_LIB)

INCLUDES := -Iinclude -I/usr/local/include -I/usr/local/include/mimalloc-3.2

CFLAGS   := -O2 -g -Wall -Wextra $(INCLUDES) -MMD -MP
CXXFLAGS := -O2 -g -Wall -Wextra $(INCLUDES) -MMD -MP -std=c++20

C_SOURCES   := $(filter-out %.bpf.c, $(shell find src -name '*.c'))
CPP_SOURCES := $(shell find src -name '*.cpp')
OBJECTS     := $(C_SOURCES:src/%.c=$(BUILD_DIR)/%.o) \
               $(CPP_SOURCES:src/%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all lib

all: lib
lib: $(LIB)

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

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR)
