# L05 몸풀기 정답 검증: 파일마다 그대로 실행 (Python: python3 파일 / C: -Wall -Wextra -Werror + ASan/UBSan)
#   make -C lecture/warmup test
CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
PY     := $(firstword $(wildcard ../../.venv/bin/python) python3)

.PHONY: test py c clean
test: py c

py:
	@set -e; for f in $(sort $(wildcard py/[0-9]*.py)); do printf '%-40s ' $$f; $(PY) $$f; done
	@$(PY) -m pytest -q -p no:cacheprovider --rootdir py $(sort $(wildcard py/[0-9]*.py)) | tail -1

c:
	@mkdir -p build
	@set -e; for f in $(sort $(wildcard c/[0-9]*.c)); do printf '%-40s ' $$f; $(CC) $(CFLAGS) $$f -o build/t; ./build/t; done

clean:
	rm -rf build
