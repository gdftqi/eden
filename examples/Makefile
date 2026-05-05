CXX      := g++
CXXFLAGS := -O2 -g -Wall -Wextra -I../include -MMD -MP -std=c++20

BUILD_DIR := build
LIB       := ../build/libtyphon.a

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=$(BUILD_DIR)/%.o)
TARGETS := $(SOURCES:%.cpp=$(BUILD_DIR)/%)

.PHONY: all clean
.SECONDARY: $(OBJECTS)

all: $(TARGETS)

$(LIB):
	@$(MAKE) -C ..

# spdlog / mimalloc 已经被打进 libtyphon.a，使用方只需链 libtyphon.a + pthread
$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(LIB)
	$(CXX) $(CXXFLAGS) $< $(LIB) -lpthread -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR)
