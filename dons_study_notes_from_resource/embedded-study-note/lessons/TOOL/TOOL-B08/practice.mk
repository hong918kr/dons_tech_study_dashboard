# TOOL-B08 practice — solution.mk + (P1) debug/release build dirs, (P2) unit-test binary that
# shares objects with the app, (P3) rebuild when CFLAGS change. GNU Make 3.81 compatible.
# Usage: make [CONFIG=debug|release] [EXTRA_CFLAGS=...] [all|test|clean|print-VAR]
CC      := cc
CONFIG  ?= debug
BUILD   := build/$(CONFIG)
TARGET  := $(BUILD)/app
TEST    := $(BUILD)/test_dsp

# (P1) per-config flags; anything else is a hard error (typo guard)
ifeq ($(CONFIG),debug)
  OPTFLAGS := -O0 -g
else ifeq ($(CONFIG),release)
  OPTFLAGS := -O2 -DNDEBUG
else
  $(error CONFIG must be debug or release, got '$(CONFIG)')
endif
CFLAGS  := -std=c11 -Wall -Wextra -Werror $(OPTFLAGS) -Iinclude $(EXTRA_CFLAGS)
LDFLAGS :=
DEPFLAGS := -MMD -MP

SRCS      := $(wildcard src/*.c)
OBJS      := $(SRCS:src/%.c=$(BUILD)/%.o)
# (P2) the test links the DSP objects of the app (compiled once) + its own main
TEST_OBJS := $(BUILD)/tests/test_dsp.o $(BUILD)/filter.o $(BUILD)/crc.o
DEPS      := $(OBJS:.o=.d) $(BUILD)/tests/test_dsp.d

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

test: $(TEST)
	./$(TEST)

$(TEST): $(TEST_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/tests/%.o: tests/%.c | $(BUILD)/tests
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD) $(BUILD)/tests:
	mkdir -p $@

# (P3) flags stamp: rewritten at parse time ONLY when the flag string changed,
#      so every object becomes older than it exactly then. (3.81 has no $(file ...))
FLAGS_STAMP := $(BUILD)/cflags.txt
ifeq ($(filter clean print-%,$(MAKECMDGOALS)),)
  ifneq ($(shell cat $(FLAGS_STAMP) 2>/dev/null),$(strip $(CFLAGS)))
    $(shell mkdir -p $(BUILD) && echo '$(strip $(CFLAGS))' > $(FLAGS_STAMP))
  endif
endif
$(OBJS) $(BUILD)/tests/test_dsp.o: $(FLAGS_STAMP)

clean:
	rm -rf build

# make print-CFLAGS  -> shows a variable's final value (Makefile debugging)
print-%:
	@echo '$* = $($*)'

-include $(DEPS)
