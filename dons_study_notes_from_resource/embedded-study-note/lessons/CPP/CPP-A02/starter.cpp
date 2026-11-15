/* CPP-A02 ISR과 C++ 객체 연결: 정적 트램펄린 디스패치
 *
 * 맥(호스트)에서 Cortex-M 의 NVIC·IPSR·VTOR 를 흉내 내고, 벡터 테이블의 C 함수 포인터 → C++ 드라이버 멤버 함수로 잇는다.
 *   Part A: 런타임 등록 테이블 (IRQ 번호 → {thunk, obj}) + extern "C" 트램펄린 하나 (IPSR 로 IRQ 번호를 안다)
 *   Part B: 등록 경쟁(두 워드 쓰기 사이의 IRQ), 등록→enable 순서, RAII 해제, 컴파일 타임 Trampoline<N, 인스턴스, 멤버>
 *   Part C: constexpr 벡터 테이블(플래시) vs RAM 테이블 — UART 4개 + xorshift 가짜 IRQ 루프
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
 */
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <type_traits>
#include <utility>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ================= mock Cortex-M 코어: NVIC + IPSR + VTOR + PRIMASK =================
 * 실제 칩: 예외 진입 시 하드웨어가 IPSR = 16 + IRQ 번호, VTOR 가 가리키는 테이블[16 + n] 으로 점프한다.
 * 모델: 모든 IRQ 같은 우선순위(중첩 없음, 복귀 후 다음 pending 으로 tail-chain), 번호가 작은 것 먼저. */
constexpr unsigned kNumIrq = 96;                 // 이 레슨의 모델: 외부 IRQ 96개 (칩마다 다름)
constexpr unsigned kNumVec = 16 + kNumIrq;
extern "C" { typedef void isr_t(void); }         // C 언어 링크 함수 타입 — 벡터 테이블 엔트리
using VectorTable = std::array<isr_t*, kNumVec>;

struct Core {
    const VectorTable* vtor = nullptr;
    uint32_t ipsr = 0, primask = 0;
    bool enabled[kNumIrq] = {};
    bool pending[kNumIrq] = {};
    uint32_t entries = 0, null_vector = 0;       // null_vector: 0 인 엔트리로 점프 (실제 칩이면 HardFault)
};
static Core g_core;

static void core_service() {
    if (g_core.ipsr != 0 || (g_core.primask & 1u) != 0 || g_core.vtor == nullptr) return;
    for (bool again = true; again;) {
        again = false;
        for (unsigned n = 0; n < kNumIrq; ++n) {
            if (!g_core.pending[n] || !g_core.enabled[n]) continue;
            g_core.pending[n] = false;           // 진입하면 pending 해제 (NVIC)
            g_core.ipsr = 16 + n;
            ++g_core.entries;
            isr_t* h = (*g_core.vtor)[16 + n];
            if (h != nullptr) h(); else ++g_core.null_vector;
            g_core.ipsr = 0;
            again = true;
            break;
        }
    }
}
static uint32_t read_ipsr() { return g_core.ipsr; }            // 실제: __get_IPSR() = "mrs r0, ipsr"
static void nvic_set_pending(unsigned n) { if (n < kNumIrq) { g_core.pending[n] = true; core_service(); } }
static void nvic_enable(unsigned n)      { if (n < kNumIrq) { g_core.enabled[n] = true; core_service(); } }
static void nvic_disable(unsigned n)     { if (n < kNumIrq) g_core.enabled[n] = false; }  // 실제: ICER 쓰기 + DSB + ISB
static bool nvic_is_enabled(unsigned n)  { return n < kNumIrq && g_core.enabled[n]; }
static bool nvic_is_pending(unsigned n)  { return n < kNumIrq && g_core.pending[n]; }

/* "여기서 IRQ 가 들어올 수 있다" 지점 (실제 칩에서는 no-op). arm_irq(n) 으로 장전한 IRQ 를 한 번 올린다 */
static int g_armed_irq = -1;
static void arm_irq(unsigned n) { g_armed_irq = static_cast<int>(n); }
static void isr_window() {
    if (g_armed_irq >= 0) {
        const unsigned n = static_cast<unsigned>(g_armed_irq);
        g_armed_irq = -1;
        nvic_set_pending(n);                     // enable 이고 마스크 안 됐으면 바로 진입
    }
}

/* ---- 처리되지 않은 IRQ: 기록 + 그 IRQ 를 꺼서 폭주(같은 IRQ 무한 재진입) 방지 ---- */
struct UnhandledLog { uint32_t count[kNumIrq]; uint32_t total; uint32_t bad_irq; };
static UnhandledLog g_unhandled{};
static void unhandled_irq(unsigned irq) {
    if (irq >= kNumIrq) { ++g_unhandled.bad_irq; return; }
    ++g_unhandled.count[irq];
    ++g_unhandled.total;
    nvic_disable(irq);
}
extern "C" void Default_Handler(void) { unhandled_irq(read_ipsr() - 16u); }

static void reset_core() {
    g_core = Core{};
    g_unhandled = UnhandledLog{};
    g_armed_irq = -1;
}

/* ================= Part A: 런타임 등록 테이블 ================= */
using Thunk = void (*)(void*);
struct IrqBinding { Thunk fn = nullptr; void* obj = nullptr; };

// 두 워드 쓰기 = 컴파일러가 실제로 내는 것 (thumbv7em 실측: str.w fn; str.w obj — 순서 보장은 없음). 사이가 틈이다.
[[maybe_unused]] static void store_binding(IrqBinding& s, Thunk fn, void* obj) {
    s.fn = fn;
    isr_window();
    s.obj = obj;
}

// CPP-M12 의 스텁: 타입 정보는 템플릿 인자에, 런타임에는 void* 하나
template <class T, auto M> static void member_thunk(void* p) { std::invoke(M, *static_cast<T*>(p)); }

// IRQ 핸들러로 쓸 수 있는 멤버: T& 로 호출 가능하고 void 반환 (const 객체에 비 const 멤버 → 거부)
template <auto M, class T>
concept IrqMember = std::is_member_function_pointer_v<decltype(M)> && std::is_invocable_v<decltype(M), T&> &&
                    std::is_void_v<std::invoke_result_t<decltype(M), T&>>;

/* Part B: 이 IRQ 하나만 NVIC 에서 잠시 끈다 (다른 IRQ 지연 0). 원래 enable 상태로 복원 — 그 사이 온 IRQ 는 pending */
class IrqMaskGuard {
public:
    explicit IrqMaskGuard(unsigned irq) : irq_(irq), was_enabled_(nvic_is_enabled(irq)) {
        // TODO(B): 이 IRQ 만 끈다
    }
    ~IrqMaskGuard() {
        // TODO(B): 원래 상태로 복원 (그 사이 pending 이 된 IRQ 는 여기서 진입해야 한다)
        (void)irq_;
    }
    IrqMaskGuard(const IrqMaskGuard&) = delete;
    IrqMaskGuard& operator=(const IrqMaskGuard&) = delete;
private:
    unsigned irq_;
    bool was_enabled_;
};

class IrqRegistry {
public:
    bool attach_raw(unsigned irq, Thunk fn, void* obj) {
        // TODO(A): 범위·null·이미 주인 있음 → false. 쓰기는 store_binding() 으로 (두 워드, 사이에 틈)
        // TODO(B): 그 틈에 같은 IRQ 가 와도 {새 fn, 옛 obj} 를 보는 ISR 이 없게
        (void)irq; (void)fn; (void)obj;
        return false;
    }
    template <auto M, class T> requires IrqMember<M, T>
    bool attach(unsigned irq, T& obj) { return attach_raw(irq, &member_thunk<T, M>, &obj); }
    template <auto M, class T> bool attach(unsigned irq, const T&&) = delete;   // 임시 객체: 문장 끝에서 죽는다

    bool detach(unsigned irq) {
        // TODO(A/B): 범위 밖이거나 주인이 없으면 false. 지우기도 store_binding() 으로, attach 와 같은 보호
        (void)irq;
        return false;
    }
    void dispatch(unsigned irq) const {          // ISR 문맥에서 불린다
        // TODO(A): 등록돼 있으면 fn(obj), 아니면(범위 밖 포함) unhandled_irq(irq)
        (void)irq;
    }
    bool bound(unsigned irq) const { return irq < kNumIrq && slots_[irq].fn != nullptr; }
private:
    IrqBinding slots_[kNumIrq]{};
};
static IrqRegistry g_registry;

/* 모든 외부 IRQ 벡터가 가리키는 C 링크 함수 하나: 하드웨어가 알려 준 IPSR 로 번호를 얻어 등록 테이블로 */
extern "C" void irq_trampoline(void) {
    // TODO(A): 지금 처리 중인 IRQ 번호는? → g_registry 로
}

static VectorTable g_ram_vectors{};              // RAM 테이블 (실제: VTOR 재배치, 정렬 512 — BM-A01)
static void install_ram_table() {
    for (unsigned i = 0; i < kNumVec; ++i) g_ram_vectors[i] = (i < 16) ? &Default_Handler : &irq_trampoline;
    g_core.vtor = &g_ram_vectors;
}

/* ================= 드라이버: mock UART (레지스터 비트는 STM32 USART 대표값) ================= */
struct UartRegs { uint32_t SR; uint32_t DR; };
constexpr uint32_t SR_RXNE = 1u << 5;

class Uart {
public:
    Uart(UartRegs& r, unsigned irq) : regs_(r), irq_(irq) {}
    ~Uart() { stop(); }
    Uart(const Uart&) = delete;
    Uart& operator=(const Uart&) = delete;

    bool start(IrqRegistry& reg) {               // 순서: 등록 → enable
        // TODO(B): 이미 시작했으면 false. 등록(attach<&Uart::on_irq>)과 NVIC enable — 어느 쪽이 먼저?
        (void)reg;
        return false;
    }
    void stop() {                                // 순서: disable → 해제 (start 의 역순)
        // TODO(B): start 의 역순. 시작 안 했으면 아무것도 안 한다
        (void)irq_; (void)reg_;
    }
    void on_irq() {                              // 핸들러 본체 (ISR 문맥)
        if ((regs_.SR & SR_RXNE) != 0) {
            const uint8_t b = static_cast<uint8_t>(regs_.DR);
            regs_.SR &= ~SR_RXNE;                // 실제 칩: DR 읽기가 RXNE 를 지운다
            ++rx_count;
            checksum = checksum * 31u + b;
        } else {
            ++spurious;                          // 내 IRQ 인데 할 일이 없음
        }
    }
    uint32_t status() const { return regs_.SR; }
    void reset_stats() { rx_count = checksum = spurious = 0; }
    uint32_t rx_count = 0, checksum = 0, spurious = 0;
private:
    UartRegs& regs_;
    unsigned irq_;
    IrqRegistry* reg_ = nullptr;
};
static void inject_rx(UartRegs& r, unsigned irq, uint8_t b) {   // 주변장치 쪽: 바이트 도착 → IRQ 라인
    r.DR = b;
    r.SR |= SR_RXNE;
    nvic_set_pending(irq);
}

/* ================= Part B: 컴파일 타임 바인딩 ================= */
constexpr bool trampoline_ok(unsigned n) {
    (void)n;                                     // TODO(B): 존재하는 IRQ 번호만
    return true;
}
template <unsigned N, auto& Inst, auto M>
    requires (trampoline_ok(N) && IrqMember<M, std::remove_reference_t<decltype(Inst)>>)
struct Trampoline {                              // 템플릿은 C 링크 불가 [temp.pre] → 정적 멤버 함수를 테이블에
    static constexpr unsigned irq = N;
    static void isr() {
        // TODO(B): Inst 의 멤버 M 을 호출 (테이블 조회 없이)
    }
};

/* ================= Part C: constexpr 벡터 테이블 ================= */
template <class... Tr> constexpr bool has_duplicate_irq() {
    // TODO(C): Tr::irq 중 같은 값이 두 번 이상 있으면 true (빈 팩도 컴파일돼야 한다)
    return false;
}
template <class... Tr> consteval VectorTable make_flash_table() {
    static_assert(!has_duplicate_irq<Tr...>(), "two Trampolines bind the same IRQ");
    VectorTable t{};
    // TODO(C): 모든 엔트리 = Default_Handler, 각 Tr 의 자리(16 + Tr::irq) = Tr::isr
    return t;
}

/* ================= 테스트 ================= */
// 레코더: obj 포인터만 비교 (역참조 없음 → 찢어진 바인딩도 UB 없이 관찰)
struct Tag { int id; };
static Tag g_tag_a{1}, g_tag_b{2};
static uint32_t g_a_hits = 0, g_b_hits = 0, g_mismatch = 0;
static void rec_a(void* p) { if (p == &g_tag_a) ++g_a_hits; else ++g_mismatch; }
static void rec_b(void* p) { if (p == &g_tag_b) ++g_b_hits; else ++g_mismatch; }
static void reset_rec() { g_a_hits = g_b_hits = g_mismatch = 0; }

static UartRegs g_r1{}, g_r2{}, g_r3{}, g_r4{};
constexpr unsigned USART1_IRQn = 37, USART2_IRQn = 38, USART3_IRQn = 39, UART4_IRQn = 52;   // STM32F4 대표값
static Uart g_u1{g_r1, USART1_IRQn}, g_u2{g_r2, USART2_IRQn}, g_u3{g_r3, USART3_IRQn}, g_u4{g_r4, UART4_IRQn};

using T1 = Trampoline<USART1_IRQn, g_u1, &Uart::on_irq>;
using T2 = Trampoline<USART2_IRQn, g_u2, &Uart::on_irq>;
using T3 = Trampoline<USART3_IRQn, g_u3, &Uart::on_irq>;
using T4 = Trampoline<UART4_IRQn, g_u4, &Uart::on_irq>;
using T1dup = Trampoline<USART1_IRQn, g_u2, &Uart::on_irq>;   // 같은 IRQ 에 다른 인스턴스 — 설정 실수

template <unsigned N> concept ValidIrqTrampoline = requires { typename Trampoline<N, g_u1, &Uart::on_irq>; };
template <class U> concept AttachableUart = requires(IrqRegistry& r, U&& u) {
    r.attach<&Uart::on_irq>(37u, std::forward<U>(u));
};
// 컴파일 타임 증명 (스타터에서도 성립): 임시·const 객체 등록 금지, 핸들러 시그니처 검사
static_assert(AttachableUart<Uart&>);
static_assert(!AttachableUart<Uart>);            // prvalue/xvalue → deleted 오버로드
static_assert(!AttachableUart<const Uart&>);     // const 객체에 비 const on_irq
static_assert(IrqMember<&Uart::on_irq, Uart> && !IrqMember<&Uart::status, Uart>);   // status() 는 uint32_t 반환
static_assert(sizeof(IrqBinding) == 2 * sizeof(void*));
static_assert(sizeof(void (Uart::*)()) == 2 * sizeof(void*));   // 멤버 함수 포인터 = 2워드: 벡터 엔트리(1워드)에 못 들어감

static constexpr VectorTable kFlashTable = make_flash_table<T1, T2, T3, T4>();   // constexpr → .rodata/.isr_vector

struct Xorshift32 { uint32_t s; uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; } };

struct RunStats {
    uint32_t rx[4], sum[4], spurious[4], unhandled_total, null_vector;
    bool operator==(const RunStats&) const = default;
};
static Uart* const g_uarts[4] = {&g_u1, &g_u2, &g_u3, &g_u4};
static UartRegs* const g_regs[4] = {&g_r1, &g_r2, &g_r3, &g_r4};
static constexpr unsigned g_uirq[4] = {USART1_IRQn, USART2_IRQn, USART3_IRQn, UART4_IRQn};

// 가짜 IRQ 루프: 3/4 는 UART 수신, 1/4 는 아무 IRQ 번호 (등록 안 된 것 포함). 기대값은 하드웨어 모델과 독립적으로 계산
static RunStats fire_loop(uint32_t seed, unsigned iters, RunStats& expect) {
    Xorshift32 rng{seed};
    expect = RunStats{};
    bool raised_unbound[kNumIrq] = {};
    for (unsigned i = 0; i < iters; ++i) {
        const uint32_t r = rng.next();
        if ((r & 3u) != 0) {
            const unsigned k = (r >> 8) & 3u;
            const uint8_t b = static_cast<uint8_t>(r >> 16);
            inject_rx(*g_regs[k], g_uirq[k], b);
            ++expect.rx[k];
            expect.sum[k] = expect.sum[k] * 31u + b;
        } else {
            const unsigned n = (r >> 8) % kNumIrq;
            bool is_uart = false;
            for (unsigned k = 0; k < 4; ++k) if (g_uirq[k] == n) { is_uart = true; ++expect.spurious[k]; }
            if (!is_uart && !raised_unbound[n]) { raised_unbound[n] = true; ++expect.unhandled_total; }
            nvic_set_pending(n);
        }
    }
    RunStats got{};
    for (unsigned k = 0; k < 4; ++k) { got.rx[k] = g_uarts[k]->rx_count; got.sum[k] = g_uarts[k]->checksum; got.spurious[k] = g_uarts[k]->spurious; }
    got.unhandled_total = g_unhandled.total;
    got.null_vector = g_core.null_vector;
    return got;
}

int main() {
    /* ---- Part A: 등록 테이블 + extern "C" 트램펄린 ---- */
    reset_core();
    install_ram_table();
    {
        IrqRegistry reg;
        CHECK(!reg.attach_raw(kNumIrq, &rec_a, &g_tag_a));             // 범위 밖
        CHECK(!reg.attach_raw(5, nullptr, &g_tag_a));                  // null 핸들러
        CHECK(reg.attach_raw(5, &rec_a, &g_tag_a) && reg.bound(5));
        CHECK(!reg.attach_raw(5, &rec_b, &g_tag_b));                   // 이미 주인이 있다
        CHECK(reg.detach(5) && !reg.bound(5) && !reg.detach(5));
        CHECK(!reg.detach(kNumIrq));
    }
    reset_rec();
    CHECK(g_registry.attach_raw(10, &rec_a, &g_tag_a));
    nvic_enable(10);
    nvic_set_pending(10);
    CHECK(g_a_hits == 1 && g_mismatch == 0 && g_core.ipsr == 0);    // IPSR 26 → IRQ 10 → rec_a(&g_tag_a)
    nvic_set_pending(10);
    CHECK(g_a_hits == 2 && g_unhandled.total == 0);
    nvic_enable(11);
    nvic_set_pending(11);                                              // 등록 안 된 IRQ
    CHECK(g_unhandled.count[11] == 1 && g_unhandled.total == 1);
    CHECK(!nvic_is_enabled(11));                                       // 폭주 방지: 꺼 버린다
    nvic_set_pending(11);
    CHECK(g_unhandled.count[11] == 1 && nvic_is_pending(11));          // 꺼진 IRQ 는 pending 으로만 남는다
    g_registry.dispatch(200);                                          // 말도 안 되는 번호 (IPSR 오염 등)
    CHECK(g_unhandled.bad_irq == 1 && g_unhandled.total == 1);
    CHECK(g_registry.detach(10));
    {
        UartRegs r{};
        Uart u{r, 20};
        CHECK(g_registry.attach<&Uart::on_irq>(20, u));
        nvic_enable(20);
        inject_rx(r, 20, 0x41);
        CHECK(u.rx_count == 1 && u.checksum == 0x41 && (r.SR & SR_RXNE) == 0);
        nvic_set_pending(20);                                          // 데이터 없이 IRQ → 드라이버의 spurious
        CHECK(u.spurious == 1 && g_unhandled.count[20] == 0);
        CHECK(g_registry.detach(20));
    }

    /* ---- Part B: IrqMaskGuard ---- */
    reset_core(); install_ram_table(); reset_rec();
    CHECK(g_registry.attach_raw(12, &rec_a, &g_tag_a));
    nvic_enable(12);
    {
        IrqMaskGuard g(12);
        CHECK(!nvic_is_enabled(12));
        nvic_set_pending(12);
        CHECK(g_a_hits == 0 && nvic_is_pending(12));                   // 막혀 있는 동안은 pending
    }
    CHECK(nvic_is_enabled(12) && g_a_hits == 1 && !nvic_is_pending(12));   // 복원 순간 진입
    nvic_disable(12);
    { IrqMaskGuard g(12); }
    CHECK(!nvic_is_enabled(12));                                       // 원래 꺼져 있었으면 꺼진 채로
    CHECK(g_registry.detach(12));

    /* ---- Part B: 등록 경쟁 — 두 워드 쓰기 사이에 그 IRQ 가 온다 ---- */
    reset_rec();
    nvic_enable(13);                                                   // 이미 enable 된 IRQ 에 등록 (재초기화, 부트로더 인계 등)
    arm_irq(13);
    CHECK(g_registry.attach_raw(13, &rec_b, &g_tag_b));
    CHECK(g_mismatch == 0);                                            // {새 fn, 옛 obj(null)} 를 본 ISR 이 없어야
    CHECK(g_b_hits == 1 && g_unhandled.count[13] == 0 && nvic_is_enabled(13));   // IRQ 는 사라지지 않고 새 바인딩으로
    arm_irq(13);
    CHECK(g_registry.detach(13));
    CHECK(g_mismatch == 0 && g_b_hits == 1);                           // 해제 중 IRQ → 핸들러가 아니라 기본 핸들러
    CHECK(g_unhandled.count[13] == 1 && !nvic_is_enabled(13));

    /* ---- Part B: 등록 → enable 순서, RAII ---- */
    reset_core(); install_ram_table();
    {
        UartRegs r{};
        Uart u{r, 30};
        r.DR = 0x5A; r.SR |= SR_RXNE;
        nvic_set_pending(30);                                          // enable 전에 이미 pending (부트로더가 남긴 것)
        CHECK(u.start(g_registry));
        CHECK(u.rx_count == 1 && u.checksum == 0x5A && g_unhandled.count[30] == 0);   // enable 순간 진입 — 이미 등록됨
        CHECK(!u.start(g_registry));                                   // 두 번 start 금지
        inject_rx(r, 30, 0x10);
        CHECK(u.rx_count == 2);
        arm_irq(30);                                                   // stop 도중 IRQ (데이터 없음)
        u.stop();
        CHECK(!g_registry.bound(30) && !nvic_is_enabled(30));
        CHECK(g_unhandled.count[30] == 0 && nvic_is_pending(30));      // 먼저 껐으니 pending 으로 남을 뿐, 기본 핸들러 X
        CHECK(u.start(g_registry) && g_registry.bound(30));            // stop 후 재시작 가능
        CHECK(u.spurious == 1 && !nvic_is_pending(30));                // 남은 pending 은 새 주인에게 (데이터 없음 → spurious)
    }                                                                  // ~Uart → stop()
    CHECK(!g_registry.bound(30) && !nvic_is_enabled(30));
    {
        UartRegs r{};
        r.SR |= SR_RXNE;
        nvic_set_pending(30);                                          // 죽은 드라이버의 IRQ
        nvic_enable(30);                                               // 누군가 실수로 다시 켜도
        CHECK(g_unhandled.count[30] == 1);                             // 기본 핸들러로 (해제된 객체를 건드리지 않음 — ASan)
    }
    {
        UartRegs r{};                                                  // 잘못된 순서: enable 먼저, 등록 나중
        Uart u{r, 31};
        r.SR |= SR_RXNE;
        nvic_set_pending(31);
        nvic_enable(31);                                               // 아직 주인 없음 → 기본 핸들러 → IRQ 꺼짐
        CHECK(g_registry.attach<&Uart::on_irq>(31, u));
        inject_rx(r, 31, 0x77);
        CHECK(u.rx_count == 0 && g_unhandled.count[31] == 1 && !nvic_is_enabled(31));   // 첫 바이트 유실 + 조용히 죽은 UART
        CHECK(g_registry.detach(31));
    }

    /* ---- Part B: 컴파일 타임 Trampoline ---- */
    CHECK(trampoline_ok(0) && trampoline_ok(kNumIrq - 1));
    CHECK(!trampoline_ok(kNumIrq) && !trampoline_ok(200));
    CHECK(ValidIrqTrampoline<95>);
    CHECK(!ValidIrqTrampoline<96>);
    g_u1.reset_stats();
    g_r1.DR = 0x31; g_r1.SR |= SR_RXNE;
    T1::isr();                                                         // 직접 호출: 테이블 조회 없음
    CHECK(g_u1.rx_count == 1 && g_u1.checksum == 0x31);
    T1::isr();
    CHECK(g_u1.spurious == 1 && T1::irq == 37u);

    /* ---- Part C: constexpr 벡터 테이블 ---- */
    CHECK((!has_duplicate_irq<T1, T2, T3, T4>()));
    CHECK((has_duplicate_irq<T1, T2, T1dup>()));
    CHECK((!has_duplicate_irq<>()));
    CHECK(kFlashTable[16 + 37] == &T1::isr && kFlashTable[16 + 52] == &T4::isr);
    CHECK(kFlashTable[0] == &Default_Handler && kFlashTable[16 + 40] == &Default_Handler && kFlashTable[kNumVec - 1] == &Default_Handler);

    /* ---- Part C: UART 4개 + 가짜 IRQ 루프, RAM 테이블(등록) vs 플래시 테이블(Trampoline) ---- */
    RunStats exp_ram{}, exp_flash{};
    reset_core(); install_ram_table();
    for (Uart* u : g_uarts) { u->reset_stats(); CHECK(u->start(g_registry)); }
    for (unsigned n = 0; n < kNumIrq; ++n) nvic_enable(n);            // 설정 실수 모델: 전부 enable
    const RunStats ram = fire_loop(0xC0FFEEu, 20000, exp_ram);
    for (Uart* u : g_uarts) u->stop();
    CHECK(ram.rx[0] > 3000 && ram.rx[1] > 3000 && ram.rx[2] > 3000 && ram.rx[3] > 3000);
    CHECK(ram == exp_ram);                                             // 인스턴스별 수신 수·체크섬·spurious·미처리 수 정확
    CHECK(ram.unhandled_total == exp_ram.unhandled_total && ram.unhandled_total > 50 && ram.null_vector == 0);

    reset_core();
    g_core.vtor = &kFlashTable;                                        // VTOR = 플래시 테이블 (등록 없음)
    for (Uart* u : g_uarts) u->reset_stats();
    for (unsigned n = 0; n < kNumIrq; ++n) nvic_enable(n);
    const RunStats flash = fire_loop(0xC0FFEEu, 20000, exp_flash);
    CHECK(flash == exp_flash);
    CHECK(flash == ram);                                               // 두 디스패치 방식이 같은 결과
    std::printf("info: rx = %u %u %u %u, spurious = %u %u %u %u, unhandled IRQs = %u, entries = %u\n",
                flash.rx[0], flash.rx[1], flash.rx[2], flash.rx[3], flash.spurious[0], flash.spurious[1],
                flash.spurious[2], flash.spurious[3], flash.unhandled_total, g_core.entries);
    CHECK(g_unhandled.count[USART1_IRQn] == 0 && g_unhandled.count[UART4_IRQn] == 0);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
