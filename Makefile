CC       := gcc
CXX      := g++
AR       := ar
THIRD_PARTY_INCLUDES ?= -I/usr/include
THIRD_PARTY_LIBS ?= /usr/lib/x86_64-linux-gnu/libbpf.a -lelf -lz
INCLUDES := -Iinclude $(THIRD_PARTY_INCLUDES)
CFLAGS   := -O2 -g -Wall -Wextra $(INCLUDES) -MMD -MP
CXXFLAGS := -O2 -g -Wall -Wextra $(INCLUDES) -MMD -MP -std=c++20
ARFLAGS  := rcs

BUILD_DIR := build
LIB       := $(BUILD_DIR)/libtyphon.a

C_SOURCES   := $(filter-out %.bpf.c, $(shell find src -name '*.c'))
CPP_SOURCES := $(shell find src -name '*.cpp')

C_OBJECTS   := $(C_SOURCES:src/%.c=$(BUILD_DIR)/%.o)
CPP_OBJECTS := $(CPP_SOURCES:src/%.cpp=$(BUILD_DIR)/%.o)
OBJECTS     := $(C_OBJECTS) $(CPP_OBJECTS)

.PHONY: all lib examples clean

all: lib examples

lib: $(LIB)

examples: $(LIB)
	@$(MAKE) -C examples THIRD_PARTY_INCLUDES="$(THIRD_PARTY_INCLUDES)" THIRD_PARTY_LIBS="$(THIRD_PARTY_LIBS)"

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
	@$(MAKE) -C examples clean
