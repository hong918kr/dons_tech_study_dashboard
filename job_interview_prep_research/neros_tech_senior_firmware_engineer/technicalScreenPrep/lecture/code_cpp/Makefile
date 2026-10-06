# L04 예제: 임베디드 흔한 설정(-fno-exceptions -fno-rtti) + 경고 0 + ASan/UBSan
CXX      ?= c++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -g -O1 \
            -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: test clean
test: build/l04
	./build/l04

build/l04: l04_embedded_cpp.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -rf build
