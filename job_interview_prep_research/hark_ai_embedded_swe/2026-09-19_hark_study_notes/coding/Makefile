# Hark tech session 코딩 연습
#   make run N=01      내 구현(starters/) 빌드·실행
#   make sol N=01      모범답안(solutions/) 빌드·실행
#   make all           모범답안 5개 전부 실행
#   make clean
CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
BUILD   := build
SOLS    := $(sort $(wildcard solutions/*.c))

.PHONY: all run sol clean list
list:
	@echo "문제 목록:"; ls problems/*.md | sed 's#problems/##;s#\.md##' | sed 's/^/  /'

$(BUILD):
	@mkdir -p $(BUILD)

run: $(BUILD)
	@f=$$(ls starters/$(N)_*.c 2>/dev/null | head -1); \
	 if [ -z "$$f" ]; then echo "starters/$(N)_*.c 없음"; exit 1; fi; \
	 $(CC) $(CFLAGS) $$f -o $(BUILD)/my_$(N) || exit 1; \
	 ./$(BUILD)/my_$(N); rc=$$?; \
	 if [ $$rc -ne 0 ]; then \
	   echo ""; \
	   echo "  ^ 아직 통과하지 못했습니다 (구현 전이면 정상입니다)."; \
	   echo "    starters/$(N)_*.c 의 TODO를 채우고 다시 'make run N=$(N)'."; \
	   echo "    막히면: notes/$(N)_*.md · 답안: make sol N=$(N)"; \
	 fi; exit 0

sol: $(BUILD)
	@f=$$(ls solutions/$(N)_*.c 2>/dev/null | head -1); \
	 if [ -z "$$f" ]; then echo "solutions/$(N)_*.c 없음"; exit 1; fi; \
	 $(CC) $(CFLAGS) $$f -o $(BUILD)/sol_$(N) && ./$(BUILD)/sol_$(N)

all: $(BUILD)
	@for f in $(SOLS); do n=$$(basename $$f .c); \
	  $(CC) $(CFLAGS) $$f -o $(BUILD)/sol_$$n || exit 1; \
	  printf "\n=== %s ===\n" $$n; ./$(BUILD)/sol_$$n || exit 1; done

clean:
	@rm -rf $(BUILD)
