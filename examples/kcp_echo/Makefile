CXX      := g++
CXXFLAGS := -O2 -g -Wall -Wextra -I../../include -MMD -MP -std=c++20

BUILD_DIR := build
LIB       := ../../build/libtyphon.a
TARGET    := $(BUILD_DIR)/server

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all clean

all: $(TARGET)

$(LIB):
	@$(MAKE) -C ../..

# spdlog / mimalloc / libbpf 已被打进 libtyphon.a；
# libelf + libz 是 libbpf 的动态依赖，必须由最终链接行提供
$(TARGET): $(OBJECTS) $(LIB)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LIB) -lpthread -lelf -lz -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR)