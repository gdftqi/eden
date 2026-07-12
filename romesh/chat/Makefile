CXX         := g++
CXXFLAGS    := -O2 -g -Wall -Wextra -I../../include -MMD -MP -std=c++20

# chat/ 是干净的部署包: 二进制 + config + docker-compose;
# 编译中间产物(.o/.d)全部放 obj/, 不进部署包
BUILD_DIR   := chat
OBJ_DIR     := obj
LIB         := ../../build/libtyphon.a
SERVER      := $(BUILD_DIR)/chat
CONFIG_DST  := $(BUILD_DIR)/config.yml
COMPOSE_DST := $(BUILD_DIR)/docker-compose.yml
BUILD_START := $(shell date +%s)

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)

.PHONY: all clean

all: $(SERVER) $(CONFIG_DST) $(COMPOSE_DST)
	@echo "==> [chat] build done at $$(date '+%F %T'), elapsed $$(($$(date +%s) - $(BUILD_START)))s"

# typhon 主库:不存在 / 过期就回顶层 make 重新构建
$(LIB):
	@$(MAKE) -C ../..

$(SERVER): $(OBJECTS) $(LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LIB) -lpthread -o $@

$(CONFIG_DST): config.yml | $(BUILD_DIR)
	cp $< $@

$(COMPOSE_DST): docker-compose.yml | $(BUILD_DIR)
	cp $< $@

$(OBJ_DIR)/%.o: %.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

$(OBJ_DIR):
	@mkdir -p $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR) $(OBJ_DIR) tcp_echo.pid
