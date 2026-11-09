// CPP-M10 demo_bad_erase — 잘못된 erase/insert 구현을 실제로 돌려 무엇이 깨지는지 본다 (호스트 전용)
// build: c++ -std=c++20 -O1 -fno-exceptions -fno-rtti demo_bad_erase.cpp -o demo_bad_erase && ./demo_bad_erase
// 의도적으로 잘못된 코드다. 객체 수명 위반이므로 실행 결과는 이 컴파일러/옵션에서 관찰한 것일 뿐 보장이 아니다.
#include <cstdio>
#include <cstring>
#include "demo_static_vector.hpp"

static int live = 0, bad = 0;
static const void* alive[32];
static unsigned alive_n = 0;
static bool is_alive(const void* p) { for (unsigned i = 0; i < alive_n; ++i) if (alive[i] == p) return true; return false; }
static void born(const void* p) { if (is_alive(p)) { ++bad; return; } alive[alive_n++] = p; ++live; }
static void dead(const void* p) { for (unsigned i = 0; i < alive_n; ++i) if (alive[i] == p) { alive[i] = alive[--alive_n]; --live; return; } ++bad; }
static void need(const void* p) { if (!is_alive(p)) ++bad; }

struct T {
    int v;
    explicit T(int x) : v(x) { born(this); }
    T(const T& o) : v(o.v) { need(&o); born(this); }
    T(T&& o) noexcept : v(o.v) { need(&o); born(this); }
    T& operator=(const T& o) { need(this); need(&o); v = o.v; return *this; }
    T& operator=(T&& o) noexcept { need(this); need(&o); v = o.v; return *this; }
    ~T() { dead(this); }
};
using V = static_vector<T, 4>;
static void reset() { live = bad = 0; alive_n = 0; }
static void fill(V& v) { for (int i = 1; i <= 4; ++i) (void)v.emplace_back(i); }
static void report(const char* name) { std::printf("%-44s live=%d bad=%d\n", name, live, bad); }

// (1) 지울 칸을 먼저 소멸시키고 그 칸에 대입
static void bad_destroy_then_assign() {
    reset();
    {
        V v; fill(v);
        T* d = v.data();
        std::destroy_at(d + 1);                 // 칸 1 은 이제 죽었다
        std::move(d + 2, d + 4, d + 1);         // 죽은 칸에 대입 → UB
        v.pop_back();
        report("(1) destroy(pos) 후 대입, 끝날 때");
    }
    report("(1) 스코프 종료 후");
}
// (2) 끝 칸 소멸을 빠뜨리고 크기만 줄임. size_ 는 private 이라 같은 구조의 작은 벡터로 재현한다.
struct Mini {
    alignas(T) std::byte s[4 * sizeof(T)];
    unsigned n = 0;
    T* d() { return reinterpret_cast<T*>(s); }
    void add(int x) { std::construct_at(d() + n, x); ++n; }
    void leaky_erase(unsigned i) { std::move(d() + i + 1, d() + n, d() + i); --n; }   // destroy_at 누락
    ~Mini() { std::destroy(d(), d() + n); }
};
static void bad_forget_last() {
    reset();
    {
        Mini m;
        for (int i = 1; i <= 4; ++i) m.add(i);
        m.leaky_erase(1);
        report("(2) 끝 칸 소멸 누락, erase 직후 (size 3)");
    }
    report("(2) 스코프 종료 후 (0 이어야 정상)");
}

// (3) trivially copyable 이 아닌 타입을 memmove 로 밀기: 자기 자신을 가리키는 포인터가 깨진다
struct SelfRef {
    int v;
    int* self;
    explicit SelfRef(int x) : v(x), self(&v) {}
    SelfRef(const SelfRef& o) : v(o.v), self(&v) {}
    SelfRef& operator=(const SelfRef& o) { v = o.v; self = &v; return *this; }
};
static void bad_memmove() {
    static_vector<SelfRef, 4> v;
    for (int i = 1; i <= 4; ++i) (void)v.emplace_back(i);
    SelfRef* d = v.data();
    std::memmove(static_cast<void*>(d), static_cast<const void*>(d + 1), 3 * sizeof(SelfRef));
    std::printf("(3) memmove erase: d[0].v=%d  *d[0].self=%d  self==&v? %s\n", d[0].v, *d[0].self, d[0].self == &d[0].v ? "yes" : "NO");
    static_vector<SelfRef, 4> w;
    for (int i = 1; i <= 4; ++i) (void)w.emplace_back(i);
    w.erase(w.cbegin());
    std::printf("    correct erase: w[0].v=%d  *w[0].self=%d  self==&v? %s\n", w[0].v, *w[0].self, w[0].self == &w[0].v ? "yes" : "NO");
}

// (4) 임시 객체 없이 삽입: 인자가 내 원소를 가리킬 때
static void bad_alias_insert() {
    static_vector<int, 4> v;
    for (int i : {1, 2, 3}) (void)v.emplace_back(i);
    const int& arg = v[2];                      // 값 3 을 가리킨다
    int* d = v.data();
    (void)v.emplace_back(0);                    // 끝 칸 확보
    d = v.data();
    std::move_backward(d, d + 3, d + 4);        // 전부 한 칸 뒤로: arg 가 가리키던 칸의 값이 바뀐다
    d[0] = arg;
    std::printf("(4) 임시 없는 insert(begin, v[2]): {%d, %d, %d, %d} (기대 {3, 1, 2, 3})\n", d[0], d[1], d[2], d[3]);
}

int main() {
    bad_destroy_then_assign();
    bad_forget_last();
    bad_memmove();
    bad_alias_insert();
}
