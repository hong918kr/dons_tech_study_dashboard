/* CPP-A02 추가 연습 — 한 번에 검증
 *   연습 1: 한 워드 게시(single-word publish) 등록 테이블 — 바인딩은 constexpr(플래시), 슬롯은 포인터 하나
 *   연습 2: 공유 IRQ 라인 (EXTI9_5 류) — 한 IRQ 에 여러 소스, 아무도 처리 못 하면 미처리
 *   연습 3: first_duplicate_irq<Tr...>() — 중복된 IRQ 번호를 컴파일 타임에 알려 주기 (fold expression)
 *   연습 4: 지연 디스패치 (bottom half) — ISR 은 비트만, main 이 IRQ 번호 순서로 처리
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
 */
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

constexpr unsigned kNumIrq = 96;

/* 최소 모델: isr_window() 에서 장전된 "ISR" 하나를 한 번 실행 (단일 코어, main 을 선점) */
static void (*g_armed)() = nullptr;
static void arm(void (*isr)()) { g_armed = isr; }
static void isr_window() { if (g_armed != nullptr) { void (*f)() = g_armed; g_armed = nullptr; f(); } }

using Thunk = void (*)(void*);
struct IrqBinding { Thunk fn; void* obj; };

struct Tag { int id; };
static Tag g_tag_a{1}, g_tag_b{2};
static uint32_t g_a = 0, g_b = 0, g_mismatch = 0, g_default = 0;
static void rec_a(void* p) { if (p == &g_tag_a) ++g_a; else ++g_mismatch; }
static void rec_b(void* p) { if (p == &g_tag_b) ++g_b; else ++g_mismatch; }

/* ---------------- 연습 1 ---------------- */
static constexpr IrqBinding kBindA{&rec_a, &g_tag_a};   // 주소 상수 → 상수 초기화 (.rodata, 플래시)
static constexpr IrqBinding kBindB{&rec_b, &g_tag_b};

class SingleWordRegistry {
public:
    void publish(unsigned irq, const IrqBinding* b) {   // nullptr = 해제. 교체(rebind)도 허용
        if (irq >= kNumIrq) return;
        isr_window();                                     // 쓰기 전 틈
        slots_[irq].store(b, std::memory_order_release); // 정렬된 32비트 STR 하나 (M4: dmb; str)
        isr_window();                                     // 쓴 후 틈
    }
    void dispatch(unsigned irq) const {
        const IrqBinding* b = (irq < kNumIrq) ? slots_[irq].load(std::memory_order_acquire) : nullptr;
        if (b != nullptr) b->fn(b->obj); else ++g_default;
    }
private:
    std::atomic<const IrqBinding*> slots_[kNumIrq]{};
};
static_assert(std::atomic<const IrqBinding*>::is_always_lock_free);
static SingleWordRegistry g_swr;
static void isr_irq7() { g_swr.dispatch(7); }

/* ---------------- 연습 2 ---------------- */
static uint32_t g_exti_pr = 0;                          // EXTI 펜딩 레지스터 모델 (W1C)
struct ExtiLine {
    uint32_t mask, hits;
    bool on_irq() {
        if ((g_exti_pr & mask) == 0) return false;
        g_exti_pr &= ~mask;                             // 실제: EXTI->PR = mask (1 을 써서 지움)
        ++hits;
        return true;
    }
};
template <unsigned Max> class SharedIrq {
public:
    using Fn = bool (*)(void*);
    bool add(Fn fn, void* obj) {
        if (n_ == Max || fn == nullptr) return false;
        e_[n_++] = {fn, obj};
        return true;
    }
    void dispatch() {                                   // 첫 번째에서 멈추지 않는다: 여러 소스가 동시에 올 수 있다
        bool any = false;
        for (unsigned i = 0; i < n_; ++i) any = e_[i].fn(e_[i].obj) || any;
        if (!any) ++unhandled;
    }
    uint32_t unhandled = 0;
private:
    struct E { Fn fn; void* obj; } e_[Max]{};
    unsigned n_ = 0;
};
template <class T, bool (T::*M)()> static bool shared_thunk(void* p) { return (static_cast<T*>(p)->*M)(); }

/* ---------------- 연습 3 ---------------- */
template <unsigned N> struct FakeTr { static constexpr unsigned irq = N; };
template <class... Tr> constexpr int first_duplicate_irq() {
    constexpr unsigned v[] = {Tr::irq..., 0u};
    int found = -1;
    unsigned i = 0;
    ((found < 0 ? [&] { for (unsigned j = 0; j < i; ++j) if (v[j] == Tr::irq) { found = static_cast<int>(Tr::irq); } }() : void(), ++i), ...);
    return found;
}
static_assert(first_duplicate_irq<FakeTr<37>, FakeTr<38>, FakeTr<52>>() == -1);
static_assert(first_duplicate_irq<FakeTr<37>, FakeTr<38>, FakeTr<37>, FakeTr<38>>() == 37);
static_assert(first_duplicate_irq<>() == -1);

/* ---------------- 연습 4 ---------------- */
class DeferredIrq {
public:
    void isr_mark(unsigned irq) {                       // ISR 쪽: 비트 하나 (M3/M4: ldrex/orr/strex)
        if (irq < kNumIrq) bits_[irq / 32].fetch_or(1u << (irq % 32), std::memory_order_release);
    }
    template <class F> unsigned run(F&& handle) {      // main 쪽: 워드별로 한 번에 가져가고 낮은 번호부터
        unsigned done = 0;
        for (unsigned w = 0; w < 3; ++w) {
            uint32_t m = bits_[w].exchange(0, std::memory_order_acquire);
            while (m != 0) {
                const unsigned b = static_cast<unsigned>(std::countr_zero(m));
                m &= m - 1;
                handle(w * 32 + b);
                ++done;
                isr_window();                           // 처리 중에 ISR 이 끼어들 수 있다
            }
        }
        return done;
    }
private:
    std::atomic<uint32_t> bits_[3]{};
};
static DeferredIrq g_def;
static uint32_t g_handled[kNumIrq];
static void isr_mark3() { g_def.isr_mark(3); }
static void isr_mark80() { g_def.isr_mark(80); }

struct Xorshift32 { uint32_t s; uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; } };

int main() {
    /* 연습 1 */
    g_swr.publish(7, &kBindA);
    g_swr.dispatch(7);
    CHECK(g_a == 1 && g_mismatch == 0);
    arm(isr_irq7);
    g_swr.publish(7, &kBindB);                          // 교체 직전 IRQ → 옛 바인딩 전체(A)
    CHECK(g_a == 2 && g_b == 0 && g_mismatch == 0);
    g_swr.dispatch(7);
    CHECK(g_b == 1);
    g_swr.publish(7, &kBindA);
    arm(isr_irq7);
    isr_window();
    CHECK(g_a == 3 && g_mismatch == 0);
    g_swr.publish(7, nullptr);
    g_swr.dispatch(7);
    g_swr.dispatch(500);
    CHECK(g_default == 2 && g_mismatch == 0);

    /* 연습 2 */
    {
        ExtiLine l5{1u << 5, 0}, l6{1u << 6, 0}, l7{1u << 7, 0};
        SharedIrq<4> exti9_5;
        CHECK(exti9_5.add(&shared_thunk<ExtiLine, &ExtiLine::on_irq>, &l5));
        CHECK(exti9_5.add(&shared_thunk<ExtiLine, &ExtiLine::on_irq>, &l6));
        CHECK(exti9_5.add(&shared_thunk<ExtiLine, &ExtiLine::on_irq>, &l7));
        Xorshift32 rng{12345};
        uint32_t e5 = 0, e6 = 0, e7 = 0, eun = 0;
        for (int i = 0; i < 3000; ++i) {
            const uint32_t r = rng.next();
            g_exti_pr = r & ((1u << 5) | (1u << 6) | (1u << 7) | (1u << 9));   // 9 번 라인은 주인 없음
            e5 += (g_exti_pr >> 5) & 1u; e6 += (g_exti_pr >> 6) & 1u; e7 += (g_exti_pr >> 7) & 1u;
            if ((g_exti_pr & 0xE0u) == 0) ++eun;
            exti9_5.dispatch();
            g_exti_pr &= ~(1u << 9);                    // 주인 없는 소스는 따로 정리 (실제: EXTI IMR 에서 마스크)
        }
        CHECK(l5.hits == e5 && l6.hits == e6 && l7.hits == e7);
        CHECK(exti9_5.unhandled == eun && eun > 0 && g_exti_pr == 0);
        SharedIrq<1> one;
        CHECK(one.add(&shared_thunk<ExtiLine, &ExtiLine::on_irq>, &l5) && !one.add(&shared_thunk<ExtiLine, &ExtiLine::on_irq>, &l6));
    }

    /* 연습 3 */
    CHECK((first_duplicate_irq<FakeTr<1>, FakeTr<2>, FakeTr<3>, FakeTr<2>, FakeTr<1>>() == 2));
    CHECK((first_duplicate_irq<FakeTr<95>>() == -1));

    /* 연습 4 */
    {
        auto h = [](unsigned n) { ++g_handled[n]; };
        g_def.isr_mark(5); g_def.isr_mark(5); g_def.isr_mark(64);
        CHECK(g_def.run(h) == 2 && g_handled[5] == 1 && g_handled[64] == 1);   // 같은 IRQ 두 번 = 한 번 (합쳐짐)
        g_def.isr_mark(40);                             // 워드 1
        arm(isr_mark3);                                 // 40 처리 직후 ISR 이 IRQ 3 (워드 0, 이미 지나감) 표시
        CHECK(g_def.run(h) == 1 && g_handled[40] == 1 && g_handled[3] == 0);
        CHECK(g_def.run(h) == 1 && g_handled[3] == 1);  // 잃어버리지 않고 다음 run 에서
        g_def.isr_mark(10);
        arm(isr_mark80);                                // 워드 2 는 아직 안 지났다 → 같은 run 에서
        CHECK(g_def.run(h) == 2 && g_handled[80] == 1);
        Xorshift32 rng{777};
        uint32_t marked[kNumIrq] = {};
        for (auto& c : g_handled) c = 0;
        for (int round = 0; round < 200; ++round) {
            bool set[kNumIrq] = {};
            for (int k = 0; k < 10; ++k) { const unsigned n = rng.next() % kNumIrq; g_def.isr_mark(n); set[n] = true; }
            for (unsigned n = 0; n < kNumIrq; ++n) marked[n] += set[n] ? 1u : 0u;
            g_def.run(h);
        }
        bool eq = true;
        for (unsigned n = 0; n < kNumIrq; ++n) eq = eq && (g_handled[n] == marked[n]);
        CHECK(eq);                                      // 라운드마다 "표시된 IRQ 집합" 을 정확히 한 번씩
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
