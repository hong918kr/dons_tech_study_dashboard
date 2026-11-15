// CPP-A10 demo_heap_trap.cpp — 하네스의 "힙 트랩" 이 실제로 잡는지: std::vector 한 번 push_back.
//   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_heap_trap.cpp -o heap_trap && ./heap_trap; echo "exit=$?"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

void* operator new(std::size_t n) { std::printf("HEAP TRAP: operator new(%zu)\n", n); std::fflush(stdout); __builtin_trap(); }
void* operator new[](std::size_t n) { std::printf("HEAP TRAP: operator new[](%zu)\n", n); std::fflush(stdout); __builtin_trap(); }

int main() {
    std::printf("before\n");
    std::vector<int> v;          // 빈 vector 는 할당하지 않는다
    std::printf("empty vector: no allocation\n");
    v.push_back(42);             // 여기서 operator new(4)
    std::printf("unreachable %d\n", v[0]);
    return 0;
}
