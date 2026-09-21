# Verkada 동시성/설계 연습 — 드릴용 Makefile
#
#   make            # 사용법
#   make list       # 문제 목록
#   make run N=01   # starters/01_*.c 빌드 + 실행 (내가 푼 것 채점)
#   make sol N=01   # solutions/01_*.c 빌드 + 실행 (모범답안 확인)
#   make tsan N=01  # starters를 ThreadSanitizer로 (레이스 탐지)
#   make asan N=01  # starters를 AddressSanitizer+UBSan으로
#   make all-sol    # 모범답안 전체 회귀 실행
#   make all-tsan   # 모범답안 전체 TSan (04는 의도된 race라 제외)
#   make clean

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O1 -g -pthread
TSAN     = -fsanitize=thread
ASAN     = -fsanitize=address,undefined
BUILD    = build

SOLS    := $(sort $(wildcard solutions/*.c))
NUMS    := $(notdir $(basename $(SOLS)))

.PHONY: help list run sol tsan asan all-sol all-tsan starters clean

help:
	@echo "make list | make run N=01 | make sol N=01 | make tsan N=01 | make all-sol"

list:
	@for f in $(SOLS); do echo "  $$(basename $$f .c)"; done

starters:
	@python3 make_starters.py

define find_src
$(firstword $(wildcard $(1)/$(N)_*.c))
endef

run:
	@test -n "$(N)" || { echo "usage: make run N=01"; exit 1; }
	@src=$(call find_src,starters); \
	  test -n "$$src" || { echo "starters/$(N)_*.c 없음 → make starters"; exit 1; }; \
	  mkdir -p $(BUILD); \
	  $(CC) $(CFLAGS) "$$src" -o $(BUILD)/$(N) && $(BUILD)/$(N)

sol:
	@test -n "$(N)" || { echo "usage: make sol N=01"; exit 1; }
	@src=$(call find_src,solutions); \
	  mkdir -p $(BUILD); \
	  $(CC) $(CFLAGS) "$$src" -o $(BUILD)/$(N)_sol && $(BUILD)/$(N)_sol

# 기본은 solutions (바로 돌아간다). 내 구현을 검사하려면 SRC=prob 을 붙인다:
#   make tsan N=01 SRC=prob
tsan:
	@test -n "$(N)" || { echo "usage: make tsan N=01  [SRC=prob]"; exit 1; }
	@src=$(call find_src,solutions); \
	  if [ "$(SRC)" = "prob" ]; then src=$(call find_src,starters); fi; \
	  mkdir -p $(BUILD); \
	  $(CC) $(CFLAGS) $(TSAN) "$$src" -o $(BUILD)/$(N)_tsan && $(BUILD)/$(N)_tsan

asan:
	@test -n "$(N)" || { echo "usage: make asan N=01  [SRC=prob]"; exit 1; }
	@src=$(call find_src,solutions); \
	  if [ "$(SRC)" = "prob" ]; then src=$(call find_src,starters); fi; \
	  mkdir -p $(BUILD); \
	  $(CC) $(CFLAGS) $(ASAN) "$$src" -o $(BUILD)/$(N)_asan && $(BUILD)/$(N)_asan

all-sol:
	@mkdir -p $(BUILD); fail=0; \
	for f in $(SOLS); do n=$$(basename $$f .c); \
	  $(CC) $(CFLAGS) $$f -o $(BUILD)/$$n && $(BUILD)/$$n >/dev/null 2>&1 \
	    && echo "  PASS $$n" || { echo "  FAIL $$n"; fail=1; }; \
	done; exit $$fail

all-tsan:
	@mkdir -p $(BUILD); \
	for f in $(SOLS); do n=$$(basename $$f .c); \
	  case $$n in 04_*) echo "  SKIP $$n (seqlock: 의도된 data race)"; continue;; esac; \
	  $(CC) $(CFLAGS) $(TSAN) $$f -o $(BUILD)/$${n}_tsan && $(BUILD)/$${n}_tsan >/dev/null 2>&1 \
	    && echo "  CLEAN $$n" || echo "  RACE  $$n"; \
	done

clean:
	@rm -rf $(BUILD)
