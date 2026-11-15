/* CPP-A03 demo_constructs.cpp — 구문 하나 = ROW 하나. ROW 마다 따로 Cortex-M4 오브젝트를 만들어 nm -u 로 본다.
 *   bash nm_table.sh            맥 clang  --target=thumbv7em-none-eabi -ffreestanding (+ arm-none-eabi 헤더, recipe b)
 *   bash nm_table.sh gcc        Lima VM 의 arm-none-eabi-g++ 14.2 (같은 ROW, 비교용)
 * 헤더는 <new>(ROW 4 의 placement new)와 <type_traits>(ROW 4 의 static_assert), <typeinfo>(ROW 7) 만 쓴다. */
#include <new>
#include <type_traits>
using u32 = unsigned int;
using u64 = unsigned long long;

#if ROW == 1   /* 함수 내부 static, 생성자가 constexpr 아님 → guard */
struct Cal { u32 gain; Cal(); };
u32 cal_gain() { static Cal c; return c.gain; }
#elif ROW == 2 /* 순수 가상 함수 + 키 함수(첫 비인라인 가상 함수)가 이 TU 에 → 추상 기반의 vtable 방출 */
struct Bus { virtual void write(u32 v) = 0; virtual void flush(); protected: ~Bus() = default; };
void Bus::flush() {}
#elif ROW == 3 /* 전역 객체, 비자명 소멸자 → 소멸자 등록 */
struct Logger { u32 n; Logger(); ~Logger(); };
Logger g_log;
#elif ROW == 4 /* 같은 전역, 소멸되지 않는 래퍼 (placement new 로 정적 버퍼에 생성) */
struct Logger { u32 n; Logger(); ~Logger(); };
template <class T> class NoDestroy {
    alignas(T) unsigned char buf_[sizeof(T)];
public:
    NoDestroy() { ::new (static_cast<void*>(buf_)) T(); }   /* 소멸자 정의 없음 → 자명 소멸 */
    T& get() { return *reinterpret_cast<T*>(buf_); }       /* (std::launder 생략: CPP-M07 참고) */
};
NoDestroy<Logger> g_log;
static_assert(std::is_trivially_destructible_v<NoDestroy<Logger>>);
#elif ROW == 5 /* 가상 소멸자 → deleting dtor (D0) 가 vtable 에 → operator delete */
struct Drv { virtual ~Drv(); virtual u32 id() const { return 1; } };
Drv::~Drv() {}
#elif ROW == 6 /* new-expression → operator new */
struct Pkt { u32 len; };
Pkt* make_pkt() { return new Pkt{4}; }
#elif ROW == 7 /* RTTI 켜고 typeid / dynamic_cast */
#include <typeinfo>
struct Base { virtual ~Base() = default; virtual u32 k() const { return 0; } };
struct Uart : Base { u32 k() const override { return 1; } };
const void* tid(const Base& b) { return &typeid(b); }
u32 down(Base* b) { auto* u = dynamic_cast<Uart*>(b); return u ? u->k() : 0; }
#elif ROW == 8 /* 예외 켜고 throw / catch */
int may_fail(u32 v) { if (v > 9) throw 42; return int(v); }
int guarded(u32 v) { try { return may_fail(v); } catch (int e) { return -e; } }
#elif ROW == 9 /* 64비트 나눗셈 / 나머지 (변수, 상수) */
u64 div_var(u64 a, u64 b) { return a / b; }
long long sdiv_var(long long a, long long b) { return a / b; }
u64 div_const(u64 a) { return a / 1000u; }
u32 div32(u32 a, u32 b) { return a / b; }      /* M4 는 udiv 명령이 있다 */
#elif ROW == 10 /* 구조체 복사·0 초기화 (큰 것) */
struct Frame { u32 w[32]; };
void copy_frame(Frame* d, const Frame* s) { *d = *s; }
void zero_frame(Frame* d) { *d = Frame{}; }
#elif ROW == 11 /* double 연산 (M4F 의 FPU 는 단정도만) */
double scale(double x, double k) { return x * k + 0.5; }
float scalef(float x, float k) { return x * k + 0.5f; }
#elif ROW == 12 /* clang 전용 속성: 이 변수만 소멸 등록 생략 (GCC 14 는 모르는 속성 → 경고 후 무시) */
struct Logger { u32 n; Logger(); ~Logger(); };
[[clang::no_destroy]] Logger g_log;
#endif
