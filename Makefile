CC       := gcc
CXX      := g++
AR       := ar
CFLAGS   := -O2 -g -Wall -Wextra -Iinclude -MMD -MP
CXXFLAGS := -O2 -g -Wall -Wextra -Iinclude -MMD -MP -std=c++20
ARFLAGS  := rcs

BUILD_DIR := build
LIB       := $(BUILD_DIR)/libtyphon.a

C_SOURCES   := $(shell find src -name '*.c')
CPP_SOURCES := $(shell find src -name '*.cpp')

C_OBJECTS   := $(C_SOURCES:src/%.c=$(BUILD_DIR)/%.o)
CPP_OBJECTS := $(CPP_SOURCES:src/%.cpp=$(BUILD_DIR)/%.o)
OBJECTS     := $(C_OBJECTS) $(CPP_OBJECTS)

.PHONY: all examples clean

all: $(LIB)

examples: $(LIB)
	$(MAKE) -C examples

$(LIB): $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $^

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C examples clean
