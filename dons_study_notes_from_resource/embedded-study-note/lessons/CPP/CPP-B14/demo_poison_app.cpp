/* CPP-B14 demo — 깊은 곳에서 std::vector 를 쓰는 "앱" (demo_poison_new.cpp 참고) */
#include <cstdio>
#include <vector>
template <class T> static void escape(T& t) { asm volatile("" : : "r"(&t) : "memory"); }
__attribute__((noinline)) void log_sample(int x) {   // 누군가 무심코 넣은 vector
    std::vector<int> v;
    v.push_back(x);
    escape(v);
}
int main() { log_sample(3); std::puts("done"); }
