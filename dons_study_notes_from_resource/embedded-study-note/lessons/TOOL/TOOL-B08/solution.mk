# TOOL-B08 — firmware host build: src/ include/ -> out-of-tree build/
# Works with GNU Make 3.81 (macOS /usr/bin/make) and 4.x.   Usage: make [all|clean] [-j8]
CC      := cc
BUILD   := build
TARGET  := $(BUILD)/app
CFLAGS  := -std=c11 -Wall -Wextra -Werror -O2 -Iinclude
LDFLAGS :=
# (1) -MMD: write build/x.d (user headers only)  -MP: add an empty rule per header
DEPFLAGS := -MMD -MP

# (2) every src/*.c is part of the image, no hand-kept list
SRCS    := $(wildcard src/*.c)
OBJS    := $(SRCS:src/%.c=$(BUILD)/%.o)
DEPS    := $(OBJS:.o=.d)

# (3) targets that are names of actions, not files
.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

# (4) one pattern rule: $< = src/x.c, $@ = build/x.o; build dir is order-only (after |)
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

# (5) header dependencies from the last build; '-' = no error if missing (first build)
-include $(DEPS)
