# Onsite whiteboard C code: build every .c twice (plain -O2 and ASan/UBSan) and run all tests.
CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -O2
SANFLAGS:= -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
LDLIBS  := -lpthread

SRCS := $(wildcard *.c)
BINS := $(SRCS:%.c=build/%)
SANS := $(SRCS:%.c=build/%.san)

.PHONY: all test clean
all: $(BINS) $(SANS)

build:
	@mkdir -p build

build/%: %.c | build
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

build/%.san: %.c | build
	$(CC) $(SANFLAGS) $< -o $@ $(LDLIBS)

test: all
	@set -e; for b in $(BINS) $(SANS); do echo "== $$b"; ./$$b; done; echo "ALL PASS"

clean:
	rm -rf build
