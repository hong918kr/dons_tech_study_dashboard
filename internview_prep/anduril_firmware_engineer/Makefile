# Anduril Firmware Engineer — C Interview Problem Bank
# ------------------------------------------------------
# 문제 은행 빌드/실행용 Makefile.  각 .c 파일은 자체 main()과 PASS/FAIL 하네스를 가진 독립 실행 파일이다.
#
#   make list                 # 모든 토픽(문제 세트) 목록
#   make prob N=01_bit_basics # 연습용 stub 컴파일 + 실행  (직접 채워넣고 FAIL->PASS)
#   make sol  N=01_bit_basics # 정답(solution) 컴파일 + 실행 (전부 PASS 확인)
#   make test                 # 모든 solution 컴파일 + 실행 (회귀 확인)
#   make N=01_bit_basics      # = make sol N=01_bit_basics
#   make clean
#
# N 값은 problems/ 와 solutions/ 의 파일 이름(확장자 제외)이다.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O0 -g
LDLIBS  ?= -lpthread          # 일부 파일(원형 버퍼 MPMC)이 pthread 사용. 나머지엔 무해.
BUILD   := build

PROBLEMS  := $(sort $(wildcard problems/*.c))
SOLUTIONS := $(sort $(wildcard solutions/*.c))
TOPICS    := $(sort $(notdir $(basename $(SOLUTIONS))))

.PHONY: all sol prob test list clean help
all: sol

help:
	@sed -n '1,20p' Makefile

list:
	@echo "사용 가능한 토픽 (N 값):"
	@for t in $(TOPICS); do echo "  $$t"; done

# make sol N=05_circular_buffer
sol:
	@if [ -z "$(N)" ]; then echo "사용법: make sol N=<topic>  (예: make sol N=05_circular_buffer)"; make list; exit 1; fi
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) solutions/$(N).c -o $(BUILD)/$(N)_sol $(LDLIBS)
	@echo "── running solutions/$(N).c ─────────────────────────────"
	@./$(BUILD)/$(N)_sol

# make prob N=05_circular_buffer   (연습 파일: 직접 구현 후 실행)
prob:
	@if [ -z "$(N)" ]; then echo "사용법: make prob N=<topic>"; make list; exit 1; fi
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) problems/$(N).c -o $(BUILD)/$(N)_prob $(LDLIBS)
	@echo "── running problems/$(N).c (your practice) ──────────────"
	@./$(BUILD)/$(N)_prob

# 모든 solution 회귀 실행
test:
	@mkdir -p $(BUILD)
	@fail=0; \
	for f in $(SOLUTIONS); do \
	  b=$$(basename $$f .c); \
	  printf "\n########## %s ##########\n" "$$b"; \
	  if $(CC) $(CFLAGS) $$f -o $(BUILD)/$$b\_sol $(LDLIBS) 2> $(BUILD)/$$b.cc.log; then \
	    ./$(BUILD)/$$b\_sol || fail=1; \
	  else \
	    echo "[COMPILE FAIL] $$f"; cat $(BUILD)/$$b.cc.log; fail=1; \
	  fi; \
	done; \
	echo; if [ $$fail -eq 0 ]; then echo "==> ALL SOLUTIONS OK"; else echo "==> SOME FAILURES (see above)"; fi; exit $$fail

clean:
	rm -rf $(BUILD)
