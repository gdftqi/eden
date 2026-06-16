CXX        := g++
CXXFLAGS   := -O2 -g -Wall -Wextra -I../../include -MMD -MP -std=c++20

BUILD_DIR  := build
LIB        := ../../build/libtyphon.a
SERVER     := $(BUILD_DIR)/server
BUILD_START := $(shell date +%s)

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all clean

all: $(SERVER)
	@echo "==> [tcp_echo] build done at $$(date '+%F %T'), elapsed $$(($$(date +%s) - $(BUILD_START)))s"

# typhon 主库:不存在 / 过期就回顶层 make 重新构建
$(LIB):
	@$(MAKE) -C ../..

$(SERVER): $(OBJECTS) $(LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LIB) -lpthread -o $@

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR) tcp_echo.pid
