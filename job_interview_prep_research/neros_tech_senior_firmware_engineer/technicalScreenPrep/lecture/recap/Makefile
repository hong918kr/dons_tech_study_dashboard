# 10-08 스크린 재구성: Task 1 ring buffer · Task 2 extract_packet (-Wall -Wextra -Werror + ASan/UBSan)
CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: test clean
test:
	@mkdir -p build
	$(CC) $(CFLAGS) test_rb.c rb.c -o build/test_rb && ./build/test_rb
	$(CC) $(CFLAGS) test_packet.c packet.c rb.c crc.c -o build/test_packet && ./build/test_packet

clean:
	rm -rf build
