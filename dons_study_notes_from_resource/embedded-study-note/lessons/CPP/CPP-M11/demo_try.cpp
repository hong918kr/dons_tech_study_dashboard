// CPP-M11 demo: TRY(statement expression) 의 함정 — 비표준 경고, early return 시 임시 객체/형제 인자, 컴파일 에러.
//   실행:    c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti demo_try.cpp -o demo_try && ./demo_try
//   비표준:  c++ -std=c++20 -pedantic -fsyntax-only demo_try.cpp        (-Wgnu-statement-expression)
//   컴파일 에러: c++ -std=c++20 -DSHOW_ERROR=1 -fsyntax-only demo_try.cpp  (1..4)
#include <cstdio>
#include <utility>

template <class E> struct Unexpected { E error; };
template <class E> Unexpected(E) -> Unexpected<E>;

template <class T, class E>
class [[nodiscard]] Result {
public:
    Result(T v) : val_(v), ok_(true) {}
    template <class G> requires std::is_convertible_v<G, E>
    Result(Unexpected<G> u) : err_(u.error), ok_(false) {}
    bool has_value() const { return ok_; }
    T value() const { return val_; }
    E error() const { return err_; }
private:
    union { T val_; E err_; };
    bool ok_;
};

#define TRY(...)                                                    \
    ({                                                              \
        auto&& try_r_ = (__VA_ARGS__);                              \
        if (!try_r_.has_value()) return Unexpected{try_r_.error()}; \
        std::forward<decltype(try_r_)>(try_r_).value();             \
    })

enum class E1 { Bad = 1 };
enum class E2 { Bad = 2 };

struct Guard {
    const char* n;
    explicit Guard(const char* s) : n(s) { std::printf("  ctor %s\n", n); }
    ~Guard() { std::printf("  dtor %s\n", n); }
};
int side(const char* s, int v) { std::printf("  eval %s\n", s); return v; }
int use2(int a, int b) { return a + b; }
int use3(const Guard&, int b) { return b; }

using R = Result<int, E1>;
R ok(int v) { return v; }
R bad() { return Unexpected{E1::Bad}; }

R siblings() { return use2(side("left", 1), TRY(bad())); }           // 왼쪽 인자는 평가됐을까? (GCC 문서: 미지정)
R siblings_rl() { return use2(TRY(bad()), side("right", 2)); }
R temp_guard() { return use3(Guard("temp"), TRY(bad())); }            // early return 때 이미 만든 임시 객체도 정리되나?
R scope_guard() { Guard g("local"); const int v = TRY(bad()); return v + 1; }
R guard_ok() { Guard g("local-ok"); const int v = TRY(ok(5)); return v + 1; }

int main() {
    std::puts("siblings (left, TRY):");   (void)siblings();
    std::puts("siblings (TRY, right):");  (void)siblings_rl();
    std::puts("temp Guard + TRY:");       (void)temp_guard();
    std::puts("local Guard + TRY:");      (void)scope_guard();
    std::puts("local Guard, ok:");        (void)guard_ok();
#if defined(SHOW_ERROR) && SHOW_ERROR == 1
    R x = TRY(bad());                     // main 은 int 를 반환 — return Unexpected<E1> 가 안 맞는다
#endif
}

#if defined(SHOW_ERROR) && SHOW_ERROR == 2
Result<int, E2> layer() { return TRY(bad()); }     // E1→E2 변환 없음: 층 경계를 그냥 넘을 수 없다
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 3
int g = TRY(ok(1));                                // 함수 밖 — statement expression 불가
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 4
void v() { TRY(bad()); }                           // void 함수 — Unexpected 를 return 불가
#endif
