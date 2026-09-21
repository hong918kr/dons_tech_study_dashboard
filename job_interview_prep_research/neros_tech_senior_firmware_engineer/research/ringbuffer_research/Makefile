# 링버퍼 레벨별 스터디 — 연습 문제 세트
# ---------------------------------------------------------------
# practice/Makefile 과 동일한 사용법. 각 .c 는 자체 main() + PASS/FAIL 하네스를 가진
# 독립 실행 파일이다. 라이브러리도, 링크도 없다 — 파일 하나 = 한 레벨.
#
#   make list                   # 레벨 목록
#   make prob N=L0_modulo       # 연습용 stub 컴파일 + 실행 (직접 채워넣고 FAIL->PASS)
#   make sol  N=L0_modulo       # 정답 컴파일 + 실행 (전부 PASS 확인)
#   make diff N=L0_modulo       # 내 답과 정답의 차이 보기
#   make test                   # 모든 정답 회귀 실행
#   make N=L0_modulo            # = make sol N=L0_modulo
#   make notes                  # notes/*.md -> html/ 재생성 후 대시보드 열기
#   make clean
#
# N 값은 problems/ 와 solutions/ 의 파일 이름(확장자 제외)이다.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O0 -g
BUILD   := build

PROBLEMS  := $(sort $(wildcard problems/*.c))
SOLUTIONS := $(sort $(wildcard solutions/*.c))
TOPICS    := $(sort $(notdir $(basename $(SOLUTIONS))))
N_        := $(basename $(notdir $(N)))

.PHONY: all sol prob diff test list clean help notes
all: sol

help:
	@sed -n '1,18p' Makefile

list:
	@echo "사용 가능한 레벨 (N 값):"
	@for t in $(TOPICS); do echo "  $$t"; done

sol:
	@if [ -z "$(N_)" ]; then echo "사용법: make sol N=<level>  (예: make sol N=L0_modulo)"; $(MAKE) list; exit 1; fi
	@if [ ! -f solutions/$(N_).c ]; then echo "없는 레벨: '$(N_)'. 아래 목록에서 고르세요:"; $(MAKE) list; exit 1; fi
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) solutions/$(N_).c -o $(BUILD)/$(N_)_sol
	@echo "── running solutions/$(N_).c ─────────────────────────────"
	@./$(BUILD)/$(N_)_sol

prob:
	@if [ -z "$(N_)" ]; then echo "사용법: make prob N=<level>  (예: make prob N=L0_modulo)"; $(MAKE) list; exit 1; fi
	@if [ ! -f problems/$(N_).c ]; then echo "없는 레벨: '$(N_)'. 아래 목록에서 고르세요:"; $(MAKE) list; exit 1; fi
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) problems/$(N_).c -o $(BUILD)/$(N_)_prob
	@echo "── running problems/$(N_).c (your practice) ──────────────"
	@./$(BUILD)/$(N_)_prob

diff:
	@if [ -z "$(N_)" ]; then echo "사용법: make diff N=<level>"; exit 1; fi
	@diff -u problems/$(N_).c solutions/$(N_).c || true

test:
	@mkdir -p $(BUILD)
	@fail=0; \
	for f in $(SOLUTIONS); do \
	  b=$$(basename $$f .c); \
	  printf "\n########## %s ##########\n" "$$b"; \
	  if $(CC) $(CFLAGS) $$f -o $(BUILD)/$$b\_sol 2> $(BUILD)/$$b.cc.log; then \
	    ./$(BUILD)/$$b\_sol || fail=1; \
	    if [ -s $(BUILD)/$$b.cc.log ]; then echo "[WARNINGS] $$f"; cat $(BUILD)/$$b.cc.log; fail=1; fi; \
	  else \
	    echo "[COMPILE FAIL] $$f"; cat $(BUILD)/$$b.cc.log; fail=1; \
	  fi; \
	done; \
	echo; if [ $$fail -eq 0 ]; then echo "==> ALL SOLUTIONS OK"; else echo "==> SOME FAILURES (see above)"; fi; exit $$fail

notes:
	python3 build_notes_html.py
	@open html/index.html 2>/dev/null || true

clean:
	rm -rf $(BUILD)
