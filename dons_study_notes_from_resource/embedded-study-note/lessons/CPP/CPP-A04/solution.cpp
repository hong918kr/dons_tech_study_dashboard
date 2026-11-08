/* CPP-A04 std::atomic과 Cortex-M: lock-free 여부와 ISR 공유
 *
 * 맥(호스트)에서 Cortex-M 의 PRIMASK·인터럽트 1개를 흉내 내고, ISR ↔ main 공유 변수 정책 isr_shared<T> 를 만든다.
 *   Part A: 컴파일러가 실제로 내보낸 명령(clang 21, 실측)을 constexpr 표로 + lock-free 판정 정책(static_assert)
 *   Part B: lock-free 가 아닌 T(M3 의 uint64_t, M0+ 의 전부)는 PRIMASK 임계 구역으로 — volatile 반례와 비교
 *   Part C: UART RX 텔레메트리 카운터 (hot ISR) + 일관된 스냅샷
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -pthread solution.cpp -o solution && ./solution
 */
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <type_traits>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ================= mock Cortex-M 코어 (CPP-B06 과 같은 모델) =================
 * 실제 칩: irq_save() = "mrs r0, primask; cpsid i", irq_restore(pm) = "msr primask, r0" (+ "memory" clobber).
 * 호스트: PRIMASK 는 전역 변수, 인터럽트는 "pending 함수 포인터 1개". 마스크가 풀리는 순간 pending ISR 이 실행된다. */
static uint32_t g_primask = 0;
static int g_isr_runs = 0;
static void (*g_pending_isr)() = nullptr;
static void (*g_armed_isr)() = nullptr;

static void dispatch_pending() {
    if (g_pending_isr != nullptr && (g_primask & 1u) == 0) {
        void (*isr)() = g_pending_isr;
        g_pending_isr = nullptr;
        ++g_isr_runs;
        isr();
    }
}
static bool irq_masked() { return (g_primask & 1u) != 0; }
static uint32_t irq_save() {
    uint32_t pm = g_primask;
    g_primask = 1;
    std::atomic_signal_fence(std::memory_order_seq_cst);   // = asm "memory" clobber (컴파일러 배리어)
    return pm;
}
static void irq_restore(uint32_t pm) {
    std::atomic_signal_fence(std::memory_order_seq_cst);
    g_primask = pm;
    dispatch_pending();
}
/* "여기서 인터럽트가 들어올 수 있다" 지점 (실제 칩에서는 no-op). arm_preempt() 로 장전한 ISR 을 한 번 요청한다 */
static void arm_preempt(void (*isr)()) { g_armed_isr = isr; }
static void isr_window() {
    if (g_armed_isr != nullptr) {
        g_pending_isr = g_armed_isr;
        g_armed_isr = nullptr;
        dispatch_pending();                    // 마스크 중이면 pending 으로 남는다 (NVIC 처럼)
    }
}
static void reset_core() { g_primask = 0; g_isr_runs = 0; g_pending_isr = nullptr; g_armed_isr = nullptr; }

class CriticalSection {                        // 저장/복원형 (CPP-B06 Part A 의 정답)
public:
    [[nodiscard]] CriticalSection() noexcept : saved_(irq_save()) {}
    ~CriticalSection() { irq_restore(saved_); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
private:
    uint32_t saved_;
};

/* 32비트 코어가 64비트 값을 다루는 방식: 워드 2개 접근 (M0+ ldr×2, M3 ldm, M4 ldrd — 실측, 5. 풀이).
 * 두 접근 사이에 isr_window() — 인터럽트가 끼어들 수 있는 틈. 32비트 이하 T 는 접근 1번이고, 틈은 읽은 "직후"
 * (그래서 읽기 → 쓰기 RMW 사이에는 항상 틈이 있다). */
template <class T> static T load_words(const T& src) {
    if constexpr (sizeof(T) <= 4) {
        const T v = src;
        isr_window();
        return v;
    } else {
        static_assert(sizeof(T) == 8);
        const unsigned char* p = reinterpret_cast<const unsigned char*>(&src);
        uint32_t lo, hi;
        std::memcpy(&lo, p, 4);
        isr_window();
        std::memcpy(&hi, p + 4, 4);
        return static_cast<T>((static_cast<uint64_t>(hi) << 32) | lo);   // 리틀 엔디언 (Cortex-M, arm64)
    }
}
template <class T> static void store_words(T& dst, T v) {
    if constexpr (sizeof(T) <= 4) {
        dst = v;
    } else {
        unsigned char* p = reinterpret_cast<unsigned char*>(&dst);
        const uint32_t lo = static_cast<uint32_t>(v), hi = static_cast<uint32_t>(static_cast<uint64_t>(v) >> 32);
        std::memcpy(p, &lo, 4);
        isr_window();
        std::memcpy(p + 4, &hi, 4);
    }
}

/* ================= Part A: 타깃별 실제 명령 (clang 21, -O2, seq_cst, 정렬된 객체) ================= */
enum class Core { M0plus, M3, M4, HostArm64 };
enum class Op { Load, Store, FetchAdd, Cas };
enum class Lowering {
    Libcall,       // bl __atomic_<op>_<N>             (lock-free 아님 → 누군가 구현해야 한다)
    PlainBarrier,  // dmb + ldr/str (+ dmb)            (정렬된 32비트 이하 접근은 그 자체로 원자적)
    LlscLoop,      // dmb; ldrex / strex / cmp / bne; dmb
    SingleInsn     // arm64: ldar / stlr / ldaddal / casal 한 개
};

/* 이 코어에서 "항상 lock-free" 인 최대 크기 (std::atomic<uintN_t>::is_always_lock_free 실측과 같게) */
constexpr unsigned max_lock_free_bytes(Core c) {
    switch (c) {
    case Core::M0plus:    return 0;        // ARMv6-M: LDREX/STREX 없음
    case Core::M3:
    case Core::M4:        return 4;        // ARMv7-M: LDREX{B,H,} 있음, LDREXD 없음
    case Core::HostArm64: return 16;       // LSE + 128비트 CAS (casp)
    }
    return 0;
}

constexpr Lowering lowering(Core c, Op op, unsigned bytes) {
    if (bytes == 0 || (bytes & (bytes - 1)) != 0 || bytes > max_lock_free_bytes(c)) return Lowering::Libcall;
    if (c == Core::HostArm64) return Lowering::SingleInsn;
    return (op == Op::Load || op == Op::Store) ? Lowering::PlainBarrier : Lowering::LlscLoop;
}

/* 모델 타깃: 호스트에서 "M3 라면 어느 경로를 타는가" 를 시험하려고 lock-free 판정만 바꿔 끼운다 */
template <Core C, bool Multicore = false> struct ModelTarget {
    template <class T> static constexpr bool lock_free =
        sizeof(T) <= max_lock_free_bytes(C) && std::atomic<T>::is_always_lock_free;
    static constexpr bool multicore = Multicore;
};
/* 실제 빌드 타깃: 컴파일러가 아는 진짜 값 */
struct NativeTarget {
    template <class T> static constexpr bool lock_free = std::atomic<T>::is_always_lock_free;
    static constexpr bool multicore = false;
};
struct RequireLockFree  { static constexpr bool irq_fallback = false; };
struct AllowIrqFallback { static constexpr bool irq_fallback = true; };

/* 정책: 이 조합을 ISR ↔ main 공유에 써도 되는가 */
template <class T, class Target, class Policy> constexpr bool policy_ok() {
    if constexpr (!std::is_integral_v<T> || !std::is_unsigned_v<T> || std::is_same_v<T, bool>) {
        return false;                          // 폴백의 old + d 가 부호 있는 오버플로(UB)가 되지 않게
    } else {
        return Target::template lock_free<T> || (Policy::irq_fallback && !Target::multicore);
    }
}

template <class T, class Target = NativeTarget, class Policy = RequireLockFree>
class isr_shared {
    static_assert(policy_ok<T, Target, Policy>(),
                  "isr_shared<T>: T is not always lock-free on this target. Use AllowIrqFallback "
                  "(single core only), a narrower T, or split the value (hi/lo, seqlock).");
public:
    static constexpr bool uses_atomic = Target::template lock_free<T>;

    T load() const {
        if constexpr (uses_atomic) {
            return v_.load();
        } else {
            CriticalSection cs;
            return load_words(v_);
        }
    }
    void store(T x) {
        if constexpr (uses_atomic) {
            v_.store(x);
        } else {
            CriticalSection cs;
            store_words(v_, x);
        }
    }
    T fetch_add(T d) {
        if constexpr (uses_atomic) {
            return v_.fetch_add(d);
        } else {
            CriticalSection cs;
            const T old = load_words(v_);
            store_words(v_, static_cast<T>(old + d));
            return old;
        }
    }
    T exchange(T x) {
        if constexpr (uses_atomic) {
            return v_.exchange(x);
        } else {
            CriticalSection cs;
            const T old = load_words(v_);
            store_words(v_, x);
            return old;
        }
    }
    /* strong CAS: 실패하면 expected 에 현재 값을 써 준다 (std::atomic 과 같은 계약) */
    bool compare_exchange(T& expected, T desired) {
        if constexpr (uses_atomic) {
            return v_.compare_exchange_strong(expected, desired);
        } else {
            CriticalSection cs;
            const T cur = load_words(v_);
            if (cur != expected) { expected = cur; return false; }
            store_words(v_, desired);
            return true;
        }
    }
    /* *this = max(*this, x), 갱신 전 값을 반환 (최악 지연 워터마크) */
    T fetch_max(T x) {
        if constexpr (uses_atomic) {
            T cur = v_.load(std::memory_order_relaxed);
            while (cur < x && !v_.compare_exchange_weak(cur, x)) {
            }
            return cur;
        } else {
            CriticalSection cs;
            const T cur = load_words(v_);
            if (cur < x) store_words(v_, x);
            return cur;
        }
    }

private:
    std::conditional_t<uses_atomic, std::atomic<T>, T> v_{};
};

/* ================= Part B 비교용 (이미 구현됨): volatile 로 ISR 공유 ================= */
static volatile uint32_t g_vol_count = 0;
static void isr_vol_inc() { g_vol_count = g_vol_count + 1; }
static void main_vol_inc() {                   // volatile ++ = ldr / add / str (실측) — 사이에 틈
    const uint32_t t = g_vol_count;
    isr_window();
    g_vol_count = t + 1;
}
static uint64_t g_vol_ts = 0;                  // "volatile uint64_t" — 32비트 코어에선 워드 2개 접근
static void isr_tick_carry() { g_vol_ts = g_vol_ts + 1; }

/* ================= Part C: UART RX 텔레메트리 (hot ISR) ================= */
using M3 = ModelTarget<Core::M3>;
struct UartTelemetry {
    isr_shared<uint32_t, M3, AllowIrqFallback> frames;      // M3: LDREX/STREX 경로
    isr_shared<uint64_t, M3, AllowIrqFallback> bytes;       // M3: lock-free 아님 → PRIMASK 경로
    isr_shared<uint32_t, M3, AllowIrqFallback> max_lat_us;  // 워터마크
};
struct Snapshot { uint32_t frames; uint64_t bytes; uint32_t max_lat_us; };

static UartTelemetry g_tel;
static uint32_t g_next_lat = 0;

/* ISR: 프레임 1개 수신 — 개수 +1, 바이트 +len, 지연 워터마크 */
static void uart_rx_isr(UartTelemetry& t, uint32_t len, uint32_t lat_us) {
    t.frames.fetch_add(1);
    t.bytes.fetch_add(len);
    t.max_lat_us.fetch_max(lat_us);
}
static void isr_rx_64() { uart_rx_isr(g_tel, 64, g_next_lat); }

/* 비교용 (이미 구현됨): 변수마다 따로 exchange — 각각은 원자적이지만 "세 개를 한 번에" 는 아니다 */
static Snapshot snapshot_naive(UartTelemetry& t) {
    Snapshot s{};
    s.frames = t.frames.exchange(0);
    isr_window();                              // ← 여기서 ISR 이 들어오면?
    s.bytes = t.bytes.exchange(0);
    s.max_lat_us = t.max_lat_us.exchange(0);
    return s;
}
/* main: 1초마다 세 값을 한 번에 가져가고 0 으로. frames*64 == bytes 같은 관계가 스냅샷 안에서 유지돼야 한다.
 * isr_window() 자리는 그대로 둘 것 (테스트가 거기서 ISR 을 일으킨다). */
static Snapshot snapshot_and_clear(UartTelemetry& t) {
    Snapshot s{};
    CriticalSection cs;                        // 세 exchange 를 ISR 기준으로 하나의 원자 단위로
    s.frames = t.frames.exchange(0);
    isr_window();
    s.bytes = t.bytes.exchange(0);
    s.max_lat_us = t.max_lat_us.exchange(0);
    return s;
}

/* ================= 테스트 ================= */
static isr_shared<uint64_t, M3, AllowIrqFallback> g_ts64;
static void isr_ts_inc() { g_ts64.fetch_add(1); }
static uint64_t g_isr_seen = 0;
static void isr_ts_peek() { g_isr_seen = g_ts64.load(); }
static isr_shared<uint32_t, ModelTarget<Core::M0plus>, AllowIrqFallback> g_m0_cnt;
static void isr_m0_inc() { g_m0_cnt.fetch_add(1); }
static isr_shared<uint32_t, M3, AllowIrqFallback> g_m3_cnt;
static void isr_m3_inc() { g_m3_cnt.fetch_add(1); }

int main() {
    /* ---- Part A: 실측 표 ---- */
    CHECK(max_lock_free_bytes(Core::M0plus) == 0 && max_lock_free_bytes(Core::M3) == 4);
    CHECK(max_lock_free_bytes(Core::M4) == 4 && max_lock_free_bytes(Core::HostArm64) == 16);
    CHECK(lowering(Core::M0plus, Op::Load, 4) == Lowering::Libcall);       // clang: __atomic_load_4
    CHECK(lowering(Core::M0plus, Op::FetchAdd, 4) == Lowering::Libcall);   // __atomic_fetch_add_4
    CHECK(lowering(Core::M3, Op::Load, 4) == Lowering::PlainBarrier);      // ldr; dmb sy
    CHECK(lowering(Core::M3, Op::Store, 4) == Lowering::PlainBarrier);     // dmb sy; str; dmb sy
    CHECK(lowering(Core::M3, Op::FetchAdd, 4) == Lowering::LlscLoop);      // ldrex/adds/strex/cmp/bne
    CHECK(lowering(Core::M4, Op::Cas, 4) == Lowering::LlscLoop && lowering(Core::M3, Op::FetchAdd, 2) == Lowering::LlscLoop);
    CHECK(lowering(Core::M3, Op::FetchAdd, 8) == Lowering::Libcall);       // __atomic_fetch_add_8
    CHECK(lowering(Core::M4, Op::Load, 8) == Lowering::Libcall);           // __atomic_load_8
    CHECK(lowering(Core::HostArm64, Op::FetchAdd, 8) == Lowering::SingleInsn);   // ldaddal
    CHECK(lowering(Core::HostArm64, Op::Cas, 4) == Lowering::SingleInsn);        // casal
    CHECK(lowering(Core::M3, Op::Load, 3) == Lowering::Libcall);           // 2의 거듭제곱 아님

    /* ---- Part A: 정책 ---- */
    using M0 = ModelTarget<Core::M0plus>;
    using RP2040 = ModelTarget<Core::M0plus, true>;                         // 듀얼 코어 M0+
    CHECK((policy_ok<uint32_t, M3, RequireLockFree>()));
    CHECK((!policy_ok<uint64_t, M3, RequireLockFree>()));                   // M3 의 64비트 → 거부
    CHECK((policy_ok<uint64_t, M3, AllowIrqFallback>()));
    CHECK((!policy_ok<uint32_t, M0, RequireLockFree>()));                   // M0+ 는 32비트도 거부
    CHECK((policy_ok<uint32_t, M0, AllowIrqFallback>()));
    CHECK((!policy_ok<uint32_t, RP2040, AllowIrqFallback>()));              // PRIMASK 는 다른 코어를 못 막는다
    CHECK((policy_ok<uint64_t, NativeTarget, RequireLockFree>()));          // 호스트 arm64
    CHECK((!policy_ok<int32_t, NativeTarget, RequireLockFree>() && !policy_ok<bool, NativeTarget, AllowIrqFallback>()));
    CHECK((isr_shared<uint32_t, M3, AllowIrqFallback>::uses_atomic));
    CHECK((!isr_shared<uint64_t, M3, AllowIrqFallback>::uses_atomic));
    CHECK((!isr_shared<uint32_t, M0, AllowIrqFallback>::uses_atomic));
    CHECK((isr_shared<uint64_t>::uses_atomic));

    /* ---- Part A: lock-free 경로 (호스트 진짜 atomic) + 4 스레드 스트레스 ---- */
    {
        static isr_shared<uint32_t> cnt, mx;
        uint32_t e = 5;
        CHECK(cnt.fetch_add(3) == 0 && cnt.load() == 3);
        CHECK(!cnt.compare_exchange(e, 9) && e == 3);                       // 실패 → expected 갱신
        CHECK(cnt.compare_exchange(e, 9) && cnt.exchange(1) == 9 && cnt.load() == 1);
        CHECK(mx.fetch_max(7) == 0 && mx.fetch_max(4) == 7 && mx.load() == 7);
        cnt.store(0); mx.store(0);
        std::thread th[4];
        for (int k = 0; k < 4; ++k)
            th[k] = std::thread([k] {
                for (uint32_t i = 0; i < 100000; ++i) { cnt.fetch_add(1); mx.fetch_max(i * 4u + static_cast<uint32_t>(k)); }
            });
        for (auto& t : th) t.join();
        CHECK(cnt.load() == 400000u);
        CHECK(mx.load() == 99999u * 4u + 3u);
    }

    /* ---- Part B: volatile 반례 (이미 구현됨 — 항상 깨진다) ---- */
    reset_core();
    g_vol_count = 0;
    arm_preempt(isr_vol_inc);
    main_vol_inc();                            // main 과 ISR 이 한 번씩 증가
    CHECK(g_vol_count == 1u && g_isr_runs == 1);   // 2 가 아니라 1: lost update
    reset_core();
    g_vol_ts = 0xFFFFFFFFull;
    arm_preempt(isr_tick_carry);               // lo 를 읽은 직후 캐리
    CHECK(load_words(g_vol_ts) == 0x1FFFFFFFFull); // 옛 lo + 새 hi: 존재한 적 없는 값 (tearing)

    /* ---- Part B: 폴백 경로 (M3 의 uint64_t) ---- */
    reset_core();
    g_ts64.store(0xFFFFFFFFull);
    arm_preempt(isr_ts_inc);
    CHECK(g_ts64.load() == 0xFFFFFFFFull);     // 찢어지지 않은 "ISR 이전" 값
    CHECK(g_isr_runs == 1 && g_ts64.load() == 0x100000000ull && !irq_masked());  // ISR 은 복원 순간 실행
    reset_core();
    arm_preempt(isr_ts_inc);
    CHECK(g_ts64.fetch_add(0x10) == 0x100000000ull);
    CHECK(g_ts64.load() == 0x100000011ull && g_isr_runs == 1);               // 두 증가 모두 남는다
    reset_core();
    arm_preempt(isr_ts_peek);
    g_ts64.store(0x2'0000'0005ull);            // 쓰는 도중 ISR 이 읽으려 한다
    CHECK(g_isr_seen == 0x2'0000'0005ull);     // ISR 은 반쯤 쓴 값이 아니라 완성된 값을 본다
    reset_core();
    arm_preempt(isr_ts_inc);
    CHECK(g_ts64.exchange(0) == 0x2'0000'0005ull && g_ts64.load() == 1u);
    reset_core();
    {
        uint64_t e = 7;
        CHECK(!g_ts64.compare_exchange(e, 50) && e == 1u);
        arm_preempt(isr_ts_inc);
        CHECK(g_ts64.compare_exchange(e, 50) && g_ts64.load() == 51u);       // ISR 은 CAS 뒤
        CHECK(g_ts64.fetch_max(40) == 51u && g_ts64.fetch_max(0x1'0000'0000ull) == 51u);
        CHECK(g_ts64.load() == 0x1'0000'0000ull && !irq_masked());
    }
    reset_core();
    g_primask = 1;                             // 이미 임계 구역 안에서 호출 (중첩)
    arm_preempt(isr_ts_inc);
    (void)g_ts64.fetch_add(1);
    CHECK(irq_masked() && g_isr_runs == 0);    // 안쪽이 바깥 보호를 풀면 안 된다
    irq_restore(0);
    CHECK(g_isr_runs == 1 && g_ts64.load() == 0x1'0000'0002ull);
    reset_core();
    g_m0_cnt.store(10);
    arm_preempt(isr_m0_inc);
    CHECK(g_m0_cnt.fetch_add(5) == 10u && g_m0_cnt.load() == 16u);           // M0+ 의 32비트도 폴백
    reset_core();
    arm_preempt(isr_m3_inc);                   // M3 의 32비트는 atomic 경로: 틈이 없다
    CHECK(g_m3_cnt.fetch_add(5) == 0u && g_m3_cnt.load() == 5u && g_armed_isr != nullptr);

    /* ---- Part C: 텔레메트리 ---- */
    reset_core();
    g_next_lat = 120;
    for (int i = 0; i < 3; ++i) uart_rx_isr(g_tel, 64, 80u + static_cast<uint32_t>(i) * 30u);
    {
        Snapshot s = snapshot_and_clear(g_tel);
        CHECK(s.frames == 3u && s.bytes == 192u && s.max_lat_us == 140u);
        CHECK(g_tel.frames.load() == 0u && g_tel.bytes.load() == 0u && g_tel.max_lat_us.load() == 0u);
    }
    reset_core();
    uart_rx_isr(g_tel, 64, 50);
    arm_preempt(isr_rx_64);                    // 스냅샷 도중 프레임 도착
    {
        Snapshot n = snapshot_naive(g_tel);
        CHECK(n.frames == 1u && n.bytes == 128u);  // 반례: 프레임 1개에 128바이트 — 관계가 깨졌다
        (void)g_tel.frames.exchange(0);
    }
    reset_core();
    g_next_lat = 300;
    uart_rx_isr(g_tel, 64, 50);
    uart_rx_isr(g_tel, 64, 70);
    arm_preempt(isr_rx_64);
    {
        Snapshot s1 = snapshot_and_clear(g_tel);
        CHECK(s1.frames == 2u && s1.bytes == 128u && s1.max_lat_us == 70u);  // ISR 은 스냅샷 뒤로 밀렸다
        CHECK(g_isr_runs == 1 && !irq_masked());
        Snapshot s2 = snapshot_and_clear(g_tel);
        CHECK(s2.frames == 1u && s2.bytes == 64u && s2.max_lat_us == 300u);  // 다음 스냅샷에 정확히 1개
    }
    reset_core();
    {
        uint64_t frames = 0, bytes = 0;        // 많이 섞어도 합계 보존 + 스냅샷마다 frames*64 == bytes
        bool rel_ok = true;
        for (uint32_t i = 0; i < 1000; ++i) {
            g_next_lat = i;
            uart_rx_isr(g_tel, 64, i / 2u);
            if (i % 7u == 0u) arm_preempt(isr_rx_64);
            if (i % 5u == 0u) {
                Snapshot s = snapshot_and_clear(g_tel);
                rel_ok = rel_ok && (static_cast<uint64_t>(s.frames) * 64u == s.bytes);
                frames += s.frames; bytes += s.bytes;
            }
        }
        Snapshot s = snapshot_and_clear(g_tel);
        frames += s.frames; bytes += s.bytes;
        CHECK(rel_ok && frames == 1000u + 143u && bytes == frames * 64u);
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
