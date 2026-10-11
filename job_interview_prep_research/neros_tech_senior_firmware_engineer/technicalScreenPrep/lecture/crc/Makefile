# L10 CRC: 테스트 (-Wall -Wextra -Werror + ASan/UBSan) · 손 따라가기 출력
CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: test walk clean
test:
	@mkdir -p build
	$(CC) $(CFLAGS) test_crc.c crc8_table.c crc_lib.c -o build/test_crc && ./build/test_crc

walk:
	@mkdir -p build
	@$(CC) $(CFLAGS) walk.c -o build/walk && ./build/walk

clean:
	rm -rf build
