CXX         := g++
CXXFLAGS    := -O2 -g -Wall -Wextra -I../../Adam/include -MMD -MP -std=c++20

# gperftools profiler 静态自包含链:让 chat 二进制不依赖 libprofiler.so / libunwind.so
# 运行期库(最小化部署镜像里没这俩 .so 也能起)。链序敏感: profiler → unwind → lzma。
# 注意: 这些 distro 静态库非 PIC, 故链接必须 -no-pie —— 该可执行文件放弃 ASLR(安全降级),
#       这是"自包含"换来的代价。若想保留 ASLR, 改回 -lprofiler(动态, 需部署镜像装 .so)。
PROF_LIBS := -l:libprofiler.a -l:libunwind.a -l:liblzma.a

# chat/ 是干净的部署包: 二进制 + config + docker-compose + flame_build.sh;
# 编译中间产物(.o/.d)全部放 obj/, 不进部署包
BUILD_DIR   := chat
OBJ_DIR     := obj
LIB         := ../../Adam/build/libadam.a
SERVER      := $(BUILD_DIR)/chat
CONFIG_DST  := $(BUILD_DIR)/config.yml
COMPOSE_DST := $(BUILD_DIR)/docker-compose.yml
FLAME_DST   := $(BUILD_DIR)/flame_build.sh
BUILD_START := $(shell date +%s)

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)

.PHONY: all clean

all: $(SERVER) $(CONFIG_DST) $(COMPOSE_DST) $(FLAME_DST)
	@echo "==> [chat] build done at $$(date '+%F %T'), elapsed $$(($$(date +%s) - $(BUILD_START)))s"

# adam 主库:不存在 / 过期就回顶层 make 重新构建
$(LIB):
	@$(MAKE) -C ../../Adam

# C++ chat: profiler 静态链
$(SERVER): $(OBJECTS) $(LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -no-pie $(OBJECTS) $(LIB) -lpthread $(PROF_LIBS) -o $@

$(CONFIG_DST): config.yml | $(BUILD_DIR)
	cp $< $@

$(COMPOSE_DST): docker-compose.yml | $(BUILD_DIR)
	cp $< $@

$(FLAME_DST): flame_build.sh | $(BUILD_DIR)
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
