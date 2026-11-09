// CPP-M10 demo_asan_pop — pop_back 한 칸을 읽는 버그를 ASan 이 잡는가
// build: c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address -fno-omit-frame-pointer demo_asan_pop.cpp -o demo_asan_pop
// run:   ./demo_asan_pop plain      정적 바이트 저장소 (보통의 static_vector) → 탐지 안 됨
//        ./demo_asan_pop poisoned   빈 칸을 __asan_poison_memory_region 으로 표시 → 탐지
//        ./demo_asan_pop vector     std::vector (libc++ 컨테이너 주석) → 탐지
#include <cstdio>
#include <cstring>
#include <vector>
#include <sanitizer/asan_interface.h>
#include "demo_static_vector.hpp"

[[gnu::noinline]] static int read_at(const int* p) { return *p; }

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "plain";
    if (!std::strcmp(mode, "plain")) {
        static_vector<int, 4> v;
        (void)v.emplace_back(11);
        (void)v.emplace_back(22);
        const int* last = v.data() + 1;
        v.pop_back();
        std::printf("plain: read popped slot = %d (ASan 조용함)\n", read_at(last));
    } else if (!std::strcmp(mode, "poisoned")) {
        static_vector<int, 4> v;
        (void)v.emplace_back(11);
        (void)v.emplace_back(22);
        const int* last = v.data() + 1;
        v.pop_back();
        ASAN_POISON_MEMORY_REGION(v.data() + v.size(), (v.capacity() - v.size()) * sizeof(int));
        std::printf("poisoned: read = %d\n", read_at(last));
    } else {
        std::vector<int> v{11, 22};
        v.reserve(4);
        const int* last = v.data() + 1;
        v.pop_back();
        std::printf("vector: read = %d\n", read_at(last));
    }
}
