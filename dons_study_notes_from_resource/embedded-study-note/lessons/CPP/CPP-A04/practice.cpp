/* CPP-A04 추가 연습 — 한 번에 검증
 *   연습 1: M0+ 용 __atomic_* 라이브러리 함수 (ABI 그대로, 호스트 모델)
 *   연습 2: GCC 14 는 M0+ 에서 무엇을 다르게 내보내나 (lowering_gcc)
 *   연습 3: 구조체 단위 정책 — all_lock_free<Target, Ts...> + 첫 위반 필드 번호
 *   연습 4: 듀얼 코어 M0+ (RP2040 류): IRQ 차단 + 하드웨어 스핀락 가드
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -pthread practice.cpp -o practice && ./practice
 */
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <type_traits>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---- mock 코어: PRIMASK 는 코어마다 하나 → thread_local (연습 4 에서 스레드 = 코어) ---- */
static thread_local uint32_t t_primask = 0;
static thread_local void (*t_pending)() = nullptr;
static thread_local void (*t_armed)() = nullptr;
static thread_local int t_isr_runs = 0;
static void dispatch() {
    if (t_pending != nullptr && (t_primask & 1u) == 0) { auto f = t_pending; t_pending = nullptr; ++t_isr_runs; f(); }
}
static uint32_t irq_save() { uint32_t pm = t_primask; t_primask = 1; std::atomic_signal_fence(std::memory_order_seq_cst); return pm; }
static void irq_restore(uint32_t pm) { std::atomic_signal_fence(std::memory_order_seq_cst); t_primask = pm; dispatch(); }
static void arm_preempt(void (*f)()) { t_armed = f; }
static void isr_window() { if (t_armed != nullptr) { t_pending = t_armed; t_armed = nullptr; dispatch(); } }
static void reset_core() { t_primask = 0; t_pending = nullptr; t_armed = nullptr; t_isr_runs = 0; }

/* ================= 연습 1: libcall ABI (호스트 이름 m0_*, 타깃에선 __atomic_* — demo_libatomic_m0.c) ================= */
static uint32_t m0_atomic_fetch_add_4(volatile void* p, uint32_t v, int memorder) {
    (void)memorder;                            // 인터럽트 차단 구간 = 단일 코어에서 seq_cst 이상
    const uint32_t pm = irq_save();
    volatile uint32_t* q = static_cast<volatile uint32_t*>(p);
    const uint32_t old = *q;
    isr_window();
    *q = old + v;
    irq_restore(pm);
    return old;
}
/* 라이브러리 ABI 의 CAS 에는 weak 인자가 없다: (ptr, expected*, desired, success, failure) */
static bool m0_atomic_compare_exchange_4(volatile void* p, void* expected, uint32_t desired, int smo, int fmo) {
    (void)smo; (void)fmo;
    const uint32_t pm = irq_save();
    volatile uint32_t* q = static_cast<volatile uint32_t*>(p);
    uint32_t* e = static_cast<uint32_t*>(expected);
    const uint32_t cur = *q;
    isr_window();
    const bool ok = (cur == *e);
    if (ok) *q = desired; else *e = cur;
    irq_restore(pm);
    return ok;
}
static uint64_t m0_atomic_load_8(const volatile void* p, int memorder) {
    (void)memorder;
    const uint32_t pm = irq_save();
    const volatile uint32_t* w = static_cast<const volatile uint32_t*>(p);
    const uint32_t lo = w[0];
    isr_window();                              // LDR 두 개 사이
    const uint32_t hi = w[1];
    irq_restore(pm);
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
static void m0_atomic_store_8(volatile void* p, uint64_t v, int memorder) {
    (void)memorder;
    const uint32_t pm = irq_save();
    volatile uint32_t* w = static_cast<volatile uint32_t*>(p);
    w[0] = static_cast<uint32_t>(v);
    isr_window();
    w[1] = static_cast<uint32_t>(v >> 32);
    irq_restore(pm);
}
alignas(8) static volatile uint32_t g_c32 = 0;
alignas(8) static volatile uint32_t g_t64[2] = {0, 0};   // 리틀 엔디언 64비트 (lo, hi)
static void isr_c32_inc() { (void)m0_atomic_fetch_add_4(&g_c32, 1, __ATOMIC_RELAXED); }
static void isr_t64_tick() { m0_atomic_store_8(g_t64, m0_atomic_load_8(g_t64, __ATOMIC_SEQ_CST) + 1, __ATOMIC_SEQ_CST); }

/* ================= 연습 2: GCC 14 (arm-none-eabi-g++ 14.2, 실측) ================= */
enum class Core { M0plus, M3, M4, HostArm64 };
enum class Op { Load, Store, FetchAdd, Cas };
enum class Lowering { Libcall, PlainBarrier, LlscLoop, SingleInsn };
constexpr Lowering lowering_gcc(Core c, Op op, unsigned bytes) {
    const bool ldst = (op == Op::Load || op == Op::Store);
    if (c == Core::HostArm64) return Lowering::SingleInsn;       // (GCC 호스트는 이 레슨에서 안 봄 — 모델만)
    if (bytes > 4 || (bytes & (bytes - 1)) != 0) return Lowering::Libcall;
    if (c == Core::M0plus) return ldst ? Lowering::PlainBarrier : Lowering::Libcall;   // ldr + dmb ish 는 인라인
    return ldst ? Lowering::PlainBarrier : Lowering::LlscLoop;
}

/* ================= 연습 3: 구조체 단위 정책 ================= */
constexpr unsigned max_lock_free_bytes(Core c) {
    return c == Core::M0plus ? 0u : (c == Core::HostArm64 ? 16u : 4u);
}
template <Core C> struct ModelTarget {
    template <class T> static constexpr bool lock_free = sizeof(T) <= max_lock_free_bytes(C) && std::atomic<T>::is_always_lock_free;
};
template <class Target, class... Ts> constexpr bool all_lock_free() { return (Target::template lock_free<Ts> && ...); }
/* 첫 번째로 lock-free 가 아닌 필드의 번호, 모두 OK 면 -1 (static_assert 메시지/로그용) */
template <class Target, class... Ts> constexpr int first_not_lock_free() {
    int idx = 0, found = -1;
    ((found < 0 && !Target::template lock_free<Ts> ? (found = idx, 0) : 0, ++idx), ...);
    return found;
}
using M0 = ModelTarget<Core::M0plus>;
using M3 = ModelTarget<Core::M3>;
using Host = ModelTarget<Core::HostArm64>;
/* 예: 모터 ISR 이 갱신하는 필드들 — M3 에서 빌드되려면 모두 32비트 이하여야 한다 */
static_assert(all_lock_free<M3, uint32_t, uint16_t, uint8_t>());
static_assert(first_not_lock_free<M3, uint32_t, uint16_t, uint64_t, uint8_t>() == 2);

/* ================= 연습 4: 듀얼 코어 — IRQ 차단 + 하드웨어 스핀락 ================= */
/* RP2040 SIO SPINLOCKn 모델: 읽으면 "잠겨 있지 않았으면 잡고 nonzero, 이미 잠겼으면 0", 쓰면 해제 (데이터시트 기준) */
struct SioSpinlock {
    std::atomic<bool> locked{false};
    uint32_t read_claim() { return locked.exchange(true, std::memory_order_acquire) ? 0u : 1u; }
    void write_release() { locked.store(false, std::memory_order_release); }
};
class IrqSpinGuard {                           // = spin_lock_irqsave: 내 코어의 ISR + 다른 코어 둘 다 막는다
public:
    explicit IrqSpinGuard(SioSpinlock& s) : s_(s), pm_(irq_save()) {
        while (s_.read_claim() == 0u) {
        }
    }
    ~IrqSpinGuard() { s_.write_release(); irq_restore(pm_); }
    IrqSpinGuard(const IrqSpinGuard&) = delete;
    IrqSpinGuard& operator=(const IrqSpinGuard&) = delete;
private:
    SioSpinlock& s_;
    uint32_t pm_;
};
static SioSpinlock g_sio_lock;
static uint64_t g_shared_bytes = 0;            // 일반 변수 — 가드 안에서만 접근
static void add_bytes(uint32_t n) { IrqSpinGuard g(g_sio_lock); g_shared_bytes += n; }
static void isr_add_bytes() { add_bytes(1000); }

int main() {
    /* 연습 1 */
    reset_core();
    g_c32 = 41;
    arm_preempt(isr_c32_inc);
    CHECK(m0_atomic_fetch_add_4(&g_c32, 1, __ATOMIC_SEQ_CST) == 41u && g_c32 == 43u && t_isr_runs == 1);
    reset_core();
    {
        uint32_t e = 0;
        CHECK(!m0_atomic_compare_exchange_4(&g_c32, &e, 7, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) && e == 43u);
        arm_preempt(isr_c32_inc);
        CHECK(m0_atomic_compare_exchange_4(&g_c32, &e, 7, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) && g_c32 == 8u);
    }
    reset_core();
    m0_atomic_store_8(g_t64, 0xFFFFFFFFull, __ATOMIC_SEQ_CST);
    arm_preempt(isr_t64_tick);
    CHECK(m0_atomic_load_8(g_t64, __ATOMIC_SEQ_CST) == 0xFFFFFFFFull);         // 찢어지지 않음
    CHECK(m0_atomic_load_8(g_t64, __ATOMIC_SEQ_CST) == 0x100000000ull && t_isr_runs == 1);
    reset_core();
    t_primask = 1;
    arm_preempt(isr_c32_inc);
    (void)m0_atomic_fetch_add_4(&g_c32, 1, __ATOMIC_SEQ_CST);
    CHECK(t_primask == 1u && t_isr_runs == 0 && g_c32 == 9u);                  // 중첩: 호출자의 마스크 유지
    irq_restore(0);
    CHECK(g_c32 == 10u);

    /* 연습 2: clang 과 다른 점 — M0+ 의 32비트 load/store 는 GCC 가 인라인 (dmb ish) */
    CHECK(lowering_gcc(Core::M0plus, Op::Load, 4) == Lowering::PlainBarrier);
    CHECK(lowering_gcc(Core::M0plus, Op::Store, 4) == Lowering::PlainBarrier);
    CHECK(lowering_gcc(Core::M0plus, Op::FetchAdd, 4) == Lowering::Libcall);
    CHECK(lowering_gcc(Core::M0plus, Op::Cas, 4) == Lowering::Libcall);
    CHECK(lowering_gcc(Core::M3, Op::FetchAdd, 4) == Lowering::LlscLoop && lowering_gcc(Core::M3, Op::Load, 8) == Lowering::Libcall);

    /* 연습 3 */
    CHECK((all_lock_free<M3, uint32_t, uint16_t, uint8_t>()));
    CHECK((!all_lock_free<M3, uint32_t, uint64_t>()));
    CHECK((!all_lock_free<M0, uint8_t>()));
    CHECK((all_lock_free<Host, uint64_t, uint32_t>()));
    CHECK((first_not_lock_free<M3, uint64_t>() == 0 && first_not_lock_free<M3, uint32_t>() == -1));
    CHECK((first_not_lock_free<M0, uint8_t, uint16_t>() == 0));

    /* 연습 4: 같은 코어의 ISR 이 끼어들어도, 다른 코어가 동시에 돌아도 정확 */
    reset_core();
    arm_preempt(isr_add_bytes);
    {
        IrqSpinGuard g(g_sio_lock);
        g_shared_bytes += 1;
        isr_window();                          // 내 코어 ISR: 가드 안이라 pending (스핀락 자기 교착 없음)
        CHECK(t_isr_runs == 0);
    }
    CHECK(t_isr_runs == 1 && g_shared_bytes == 1001u && !g_sio_lock.locked.load());
    g_shared_bytes = 0;
    std::thread core0([] { for (int i = 0; i < 100000; ++i) add_bytes(1); });
    std::thread core1([] { for (int i = 0; i < 100000; ++i) add_bytes(2); });
    core0.join();
    core1.join();
    CHECK(g_shared_bytes == 300000u);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
