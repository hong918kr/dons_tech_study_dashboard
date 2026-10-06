# L03 예제: 경고 0 (-Werror) + ASan/UBSan 으로 실행
CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: test clean
test: build/l03
	./build/l03

build/l03: l03_embedded_c.c
	@mkdir -p build
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf build
