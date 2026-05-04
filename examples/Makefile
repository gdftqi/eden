CXX      := g++
THIRD_PARTY_INCLUDES ?=
THIRD_PARTY_LIBS ?=
CXXFLAGS := -O2 -g -Wall -Wextra -I../include $(THIRD_PARTY_INCLUDES) -MMD -MP -std=c++20

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

$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(LIB)
	$(CXX) $(CXXFLAGS) $< $(LIB) $(THIRD_PARTY_LIBS) -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR)
