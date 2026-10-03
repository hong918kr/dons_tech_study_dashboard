# Playground runner — 파일 하나를 바로 빌드·실행한다. 템플릿 없음, 내 코드만.
#   make c F=c/ring.c        C: -Wall -Wextra -Werror + ASan/UBSan 으로 빌드 후 실행
#   make py F=py/ring.py     Python: technicalScreenPrep/.venv 의 python 으로 실행
#   make test F=py/ring.py   pytest: 파일 안의 test_* 함수 실행
#   make clean
CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
PY      := $(firstword $(wildcard ../.venv/bin/python) python3)

.PHONY: c py test clean
c:
	@test -n "$(F)" || { echo "사용: make c F=c/파일.c"; exit 2; }
	@mkdir -p build
	$(CC) $(CFLAGS) $(F) -o build/$(basename $(notdir $(F))) -lpthread
	./build/$(basename $(notdir $(F)))

py:
	@test -n "$(F)" || { echo "사용: make py F=py/파일.py"; exit 2; }
	$(PY) $(F)

test:
	@test -n "$(F)" || { echo "사용: make test F=py/파일.py"; exit 2; }
	$(PY) -m pytest -q $(F)

clean:
	rm -rf build __pycache__ py/__pycache__ .pytest_cache
