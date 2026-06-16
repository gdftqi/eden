CXX      := g++
CXXFLAGS := -O2 -g -Wall -Wextra -std=c++20 -I../../../include -I/usr/local/include -DSPDLOG_COMPILED_LIB

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/x25519
LIB       := ../../../build/libtyphon.a
BUILD_START := $(shell date +%s)

.PHONY: all clean

all: $(TARGET)
	@echo "==> [x25519] build done at $$(date '+%F %T'), elapsed $$(($$(date +%s) - $(BUILD_START)))s"

$(LIB):
	@$(MAKE) -C ../../..

$(TARGET): main.cpp $(LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) main.cpp $(LIB) -lpthread -o $@

$(BUILD_DIR):
	@mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
