// CPP-M12 demo: 호출 어셈블리 비교 (arm64 + thumbv7em). measure.sh asm 으로 실행
#define main solution_main
#include "solution.cpp"
#undef main

struct Motor { int speed; void set(int v); };   // 정의는 다른 TU 라고 가정 → 인라인 불가

extern "C" {
void call_raw(void (*fp)(void*, int), void* ctx, int v) { fp(ctx, v); }
void call_delegate(const Delegate<void(int)>& d, int v) { d(v); }
void call_ref(FunctionRef<void(int)> r, int v) { r(v); }
void call_function(const std::function<void(int)>& f, int v) { f(v); }

Delegate<void(int)> bind_motor(Motor& m) { return Delegate<void(int)>::bind<&Motor::set>(m); }

// 대상이 컴파일 시점에 다 보이면?
static int bump(int x) { return x + 1; }
int known_delegate(int x) { auto d = Delegate<int(int)>::bind<&bump>(); return d(x); }
int known_function(int x) { std::function<int(int)> f = bump; return f(x); }

extern const unsigned sz_delegate = sizeof(Delegate<void(int)>);
extern const unsigned sz_ref      = sizeof(FunctionRef<void(int)>);
extern const unsigned sz_function = sizeof(std::function<void(int)>);
extern const unsigned sz_pmf      = sizeof(void (Motor::*)(int));
}
