# TOOL-B08 — firmware host build: src/ include/ -> out-of-tree build/
# Works with GNU Make 3.81 (macOS /usr/bin/make) and 4.x.   Usage: make [all|clean] [-j8]
CC      := cc
BUILD   := build
TARGET  := $(BUILD)/app
CFLAGS  := -std=c11 -Wall -Wextra -Werror -O2 -Iinclude
LDFLAGS :=
# TODO(1): ask the compiler to write build/x.d with header deps (+ phony header targets)
DEPFLAGS :=

# TODO(2): pick up every src/*.c automatically (a new driver file must not need a Makefile edit)
SRCS    := src/main.c src/uart.c src/sensor.c src/log.c src/filter.c src/crc.c
OBJS    := $(SRCS:src/%.c=$(BUILD)/%.o)
DEPS    := $(OBJS:.o=.d)

# TODO(3): mark the action targets


all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

# TODO(4): replace the six copies below with ONE pattern rule using $< and $@;
#          the build dir must be an order-only prerequisite
$(BUILD)/main.o: src/main.c $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c src/main.c -o $(BUILD)/main.o
$(BUILD)/uart.o: src/uart.c $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c src/uart.c -o $(BUILD)/uart.o
$(BUILD)/sensor.o: src/sensor.c $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c src/sensor.c -o $(BUILD)/sensor.o
$(BUILD)/log.o: src/log.c $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c src/log.c -o $(BUILD)/log.o
$(BUILD)/filter.o: src/filter.c $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c src/filter.c -o $(BUILD)/filter.o
$(BUILD)/crc.o: src/crc.c $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c src/crc.c -o $(BUILD)/crc.o

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

# TODO(5): include the generated dependency files (they do not exist on the first build)

