/* CPP-B14 demo — "abort 스텁": 힙을 쓰는 순간 런타임에 정지시킨다
 *   c++ $F demo_abort_stub.cpp -o /tmp/b14_abort && /tmp/b14_abort            # 힙 안 씀 → 정상 종료
 *   c++ $F -DUSE_HEAP demo_abort_stub.cpp -o /tmp/b14_abort && /tmp/b14_abort # std::vector → FATAL + abort
 */
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#ifdef USE_HEAP
#include <vector>
#endif

[[noreturn]] static void heap_forbidden(const char* what, std::size_t n) {
    std::fprintf(stderr, "FATAL: heap use forbidden: %s(%zu)\n", what, n);
    std::abort();   // 펌웨어라면: __BKPT(0); 또는 fault 로그 남기고 NVIC_SystemReset();
}
using std::size_t; using std::align_val_t; using std::nothrow_t;
void* operator new(size_t n) { heap_forbidden("operator new", n); }
void* operator new[](size_t n) { heap_forbidden("operator new[]", n); }
void* operator new(size_t n, align_val_t) { heap_forbidden("operator new(align)", n); }
void* operator new[](size_t n, align_val_t) { heap_forbidden("operator new[](align)", n); }
void* operator new(size_t n, const nothrow_t&) noexcept { heap_forbidden("operator new(nothrow)", n); }
void* operator new[](size_t n, const nothrow_t&) noexcept { heap_forbidden("operator new[](nothrow)", n); }
void* operator new(size_t n, align_val_t, const nothrow_t&) noexcept { heap_forbidden("operator new(align,nothrow)", n); }
void* operator new[](size_t n, align_val_t, const nothrow_t&) noexcept { heap_forbidden("operator new[](align,nothrow)", n); }
// new 가 절대 성공하지 않으므로 delete 에 non-null 이 올 일은 없다. 그래도 정의는 필요 (virtual 소멸자가 참조)
static void bad_delete(void* p) { if (p) heap_forbidden("operator delete", 0); }
void operator delete(void* p) noexcept { bad_delete(p); }
void operator delete[](void* p) noexcept { bad_delete(p); }
void operator delete(void* p, size_t) noexcept { bad_delete(p); }
void operator delete[](void* p, size_t) noexcept { bad_delete(p); }
void operator delete(void* p, align_val_t) noexcept { bad_delete(p); }
void operator delete[](void* p, align_val_t) noexcept { bad_delete(p); }
void operator delete(void* p, size_t, align_val_t) noexcept { bad_delete(p); }
void operator delete[](void* p, size_t, align_val_t) noexcept { bad_delete(p); }
void operator delete(void* p, const nothrow_t&) noexcept { bad_delete(p); }
void operator delete[](void* p, const nothrow_t&) noexcept { bad_delete(p); }
void operator delete(void* p, align_val_t, const nothrow_t&) noexcept { bad_delete(p); }
void operator delete[](void* p, align_val_t, const nothrow_t&) noexcept { bad_delete(p); }

template <class T> static void escape(T& t) { asm volatile("" : : "r"(&t) : "memory"); }

int main() {
    std::array<int, 8> samples{1, 2, 3, 4, 5, 6, 7, 8};          // 스택: 힙 아님
    int sum = 0;
    for (int s : samples) sum += s;
    std::printf("fixed-buffer sum = %d\n", sum);
#ifdef USE_HEAP
    std::vector<int> v(samples.begin(), samples.end());         // 여기서 operator new
    escape(v);
    std::printf("vector size = %zu\n", v.size());
#endif
    std::printf("done, no heap used\n");
    return 0;
}
