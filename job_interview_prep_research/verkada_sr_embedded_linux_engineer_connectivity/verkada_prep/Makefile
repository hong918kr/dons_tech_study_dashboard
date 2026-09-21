# Verkada — Senior Embedded Linux Engineer (Connectivity) · C 문제 은행
# ------------------------------------------------------
# 문제 은행 빌드/실행용 Makefile.  각 .c 파일은 자체 main()과 PASS/FAIL 하네스를 가진 독립 실행 파일이다.
#
#   make list                 # 모든 토픽(문제 세트) 목록
#   make prob N=02_condvar_queues # 연습용 stub 컴파일 + 실행  (직접 채워넣고 FAIL->PASS)
#   make sol  N=02_condvar_queues # 정답(solution) 컴파일 + 실행 (전부 PASS 확인)
#   make test                 # 모든 solution 컴파일 + 실행 (회귀 확인)
#   make N=02_condvar_queues  # = make sol N=02_condvar_queues
#   make clean
#
# N 값은 problems/ 와 solutions/ 의 파일 이름(확장자 제외)이다.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O0 -g
LDLIBS  ?= -lpthread          # 동시성 세트가 pthread 사용. 나머지엔 무해.
BUILD   := build

PROBLEMS  := $(sort $(wildcard problems/*.c))
SOLUTIONS := $(sort $(wildcard solutions/*.c))
TOPICS    := $(sort $(notdir $(basename $(SOLUTIONS))))
# N 정규화: N=01_bit_basics.c / problems/01_bit_basics.c 처럼 줘도 동작
N_        := $(basename $(notdir $(N)))

.PHONY: all sol prob test tsan check list clean help dash
all: sol

help:
	@sed -n '1,20p' Makefile

list:
	@echo "사용 가능한 토픽 (N 값):"
	@for t in $(TOPICS); do echo "  $$t"; done

# make sol N=02_condvar_queues   ( .c 붙이거나 problems/ 안에서 실행해도 됨 )
sol:
	@if [ -z "$(N_)" ]; then echo "사용법: make sol N=<topic>  (예: make sol N=02_condvar_queues)"; $(MAKE) list; exit 1; fi
	@if [ ! -f solutions/$(N_).c ]; then echo "없는 토픽: '$(N_)'. 아래 목록에서 고르세요:"; $(MAKE) list; exit 1; fi
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) solutions/$(N_).c -o $(BUILD)/$(N_)_sol $(LDLIBS)
	@echo "── running solutions/$(N_).c ─────────────────────────────"
	@./$(BUILD)/$(N_)_sol

# make prob N=02_condvar_queues   (연습 파일: 직접 구현 후 실행)
prob:
	@if [ -z "$(N_)" ]; then echo "사용법: make prob N=<topic>  (예: make prob N=02_condvar_queues)"; $(MAKE) list; exit 1; fi
	@if [ ! -f problems/$(N_).c ]; then echo "없는 토픽: '$(N_)'. 아래 목록에서 고르세요:"; $(MAKE) list; exit 1; fi
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) problems/$(N_).c -o $(BUILD)/$(N_)_prob $(LDLIBS)
	@echo "── running problems/$(N_).c (your practice) ──────────────"
	@./$(BUILD)/$(N_)_prob

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

# make tsan N=02_condvar_queues  — ThreadSanitizer로 레이스 검출
tsan:
	@if [ -z "$(N_)" ]; then echo "사용법: make tsan N=<topic>"; $(MAKE) list; exit 1; fi
	@src=solutions/$(N_).c; [ -f problems/$(N_).c ] && [ "$(SRC)" = "prob" ] && src=problems/$(N_).c; \
	  mkdir -p $(BUILD); \
	  $(CC) $(CFLAGS) -fsanitize=thread $$src -o $(BUILD)/$(N_)_tsan $(LDLIBS) && ./$(BUILD)/$(N_)_tsan

# make dash — data/*.json 에서 index.html 재생성
dash:
	@python3 build_dashboard.py

# make check — 전체 검증(JSON 스키마 + 컴파일 + 실행 + stub)
check:
	@python3 validate.py
