# demo_ci.mk - TOOL-B02 Part C: one warning list for every compiler,
# GCC-only extras gated by compiler, -Werror switched on by CI (WERROR=1).
#   make -f demo_ci.mk                 # developer build: warnings shown, build continues
#   make -f demo_ci.mk WERROR=1        # CI build with the pinned toolchain: any warning fails
#   make -f demo_ci.mk WERROR=1 bugs   # bugs12.c under CI rules -> must fail
# Works with GNU Make 3.81 (Mac) and 4.4 (VM).  Objects go to /tmp, not here.
CC      ?= cc
OUT     ?= /tmp/TOOL-B02_work/ci
WARN    := $(strip $(shell sed -e 's/\#.*//' solution.flags))
IS_CLANG := $(shell $(CC) --version 2>/dev/null | grep -c clang)
ifeq ($(IS_CLANG),0)
  WARN  += -Wlogical-op -Wduplicated-cond -Wduplicated-branches
endif
WERROR  ?= 0
ifeq ($(WERROR),1)
  WARN  += -Werror
endif
CFLAGS  := -std=c11 -O2 $(WARN)

.PHONY: all bugs show
all: $(OUT)/good
	$(OUT)/good

$(OUT)/good: good.c third_party/vendor_hal.h solution.flags | $(OUT)
	$(CC) $(CFLAGS) good.c -o $@

bugs: | $(OUT)
	$(CC) $(CFLAGS) -c bugs12.c -o $(OUT)/bugs12.o

show:
	@echo "CC=$(CC) IS_CLANG=$(IS_CLANG)"
	@echo "CFLAGS=$(CFLAGS)"

$(OUT):
	mkdir -p $@
