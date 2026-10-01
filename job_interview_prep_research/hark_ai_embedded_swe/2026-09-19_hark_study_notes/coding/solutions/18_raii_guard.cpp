/* 18_raii_guard.cpp — 인터럽트 잠금 RAII 가드 (모범답안)
 *
 *   make sol N=18
 *
 * 빌드: c++ -std=c++17 -Wall -Wextra -O2 -g -fno-exceptions -fno-rtti
 *
 * 핵심
 *   - 생성자에서 인터럽트를 끄고 이전 PRIMASK를 저장, 소멸자에서 그대로 복원한다.
 *   - 스코프를 벗어나는 모든 경로(early return, break, goto)에서 복원이 보장된다.
 *   - 복사·이동을 모두 삭제한다. 가드가 복제되면 복원이 두 번 일어난다.
 *   - 예외가 없는 환경이므로 소멸자는 "정상 경로 전용"이다. 실패를 알릴 수단이 없다.
 *
 * 해설: notes/L3_embedded_cpp.md §2, §3
 */

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <type_traits>

/* ------------------------------------------------------------------ */
/* 1. 가짜 코어 레지스터 계층 (CMSIS 모방. 실제 헤더 대신 예시 구현)   */
/* ------------------------------------------------------------------ */

/* Cortex-M의 PRIMASK: bit0 == 1 이면 configurable priority 인터럽트가 전부 막힌다.
 * 실제로는 MRS/MSR 명령으로 읽고 쓰지만, host 테스트에서는 변수 하나로 대신한다. */
namespace core {

volatile std::uint32_t g_primask = 0u; /* 0 = 인터럽트 허용, 1 = 마스킹 */

/* 계측용 카운터. 타깃에는 없다. 생성자/소멸자가 정말 짝을 맞추는지 세기 위한 것. */
std::uint32_t g_disable_calls = 0u;
std::uint32_t g_restore_calls = 0u;

/* 대기 중인 가짜 인터럽트 요청과, ISR이 실제로 돈 횟수 */
volatile std::uint32_t g_pending = 0u;
volatile std::uint32_t g_isr_runs = 0u;

inline std::uint32_t get_primask() { return g_primask; } /* == __get_PRIMASK() */

inline void disable_irq() /* == __disable_irq() */
{
    g_primask = 1u;
    ++g_disable_calls;
}

inline void set_primask(std::uint32_t v) /* == __set_PRIMASK(v) */
{
    g_primask = v;
    ++g_restore_calls;
}

} /* namespace core */

/* ------------------------------------------------------------------ */
/* 2. 공유 상태 — ISR과 task가 함께 만진다                             */
/* ------------------------------------------------------------------ */

/* credits 와 total 은 한 쌍이다. total == 생산된 누적 개수, credits == 아직 소비되지
 * 않은 개수. 둘을 따로 갱신하는 도중에 ISR이 끼어들면 불변식이 깨진다. */
volatile std::uint32_t g_credits = 0u;
volatile std::uint32_t g_consumed = 0u;

/* ------------------------------------------------------------------ */
/* 3. RAII 가드                                                        */
/* ------------------------------------------------------------------ */

class IrqLock {
public:
    IrqLock() noexcept : saved_(core::get_primask()) { core::disable_irq(); }
    ~IrqLock() { core::set_primask(saved_); }

    /* 복사 금지: 가드가 두 개가 되면 소멸자가 두 번 돌아, 중첩된 바깥 가드가
     * 아직 살아 있는데도 인터럽트가 열린다. */
    IrqLock(const IrqLock &) = delete;
    IrqLock &operator=(const IrqLock &) = delete;

    /* 이동도 금지: "소유권 이전"을 표현하려면 moved-from 상태를 나타내는 플래그가
     * 필요해지고, 가드가 4바이트에서 8바이트로 커지며 소멸자에 분기가 생긴다.
     * 인터럽트 잠금은 스코프에 붙여 두는 것이 목적이므로 이동할 이유가 없다. */
    IrqLock(IrqLock &&) = delete;
    IrqLock &operator=(IrqLock &&) = delete;

    std::uint32_t saved() const noexcept { return saved_; }

private:
    std::uint32_t saved_;
};

/* 가드는 PRIMASK 한 워드만 들고 있다. C 버전의 지역 변수와 크기가 같다. */
static_assert(sizeof(IrqLock) == sizeof(std::uint32_t), "가드에 숨은 비용이 없어야 한다");

/* 복사·이동이 정말 막혀 있는지 타입 수준에서 확인한다. */
static_assert(!std::is_copy_constructible<IrqLock>::value, "복사 생성 금지");
static_assert(!std::is_copy_assignable<IrqLock>::value, "복사 대입 금지");
static_assert(!std::is_move_constructible<IrqLock>::value, "이동 생성 금지");
static_assert(!std::is_move_assignable<IrqLock>::value, "이동 대입 금지");
static_assert(std::is_nothrow_destructible<IrqLock>::value, "소멸자는 실패하지 않는다");

/* ------------------------------------------------------------------ */
/* 4. 가드를 쓰는 함수들                                               */
/* ------------------------------------------------------------------ */

/* early return 이 두 개나 있는데 unlock 을 빠뜨릴 방법이 없다. */
static bool try_consume(std::uint32_t n)
{
    IrqLock lock;

    if (n == 0u) {
        return true; /* no-op 경로 */
    }
    if (g_credits < n) {
        return false; /* 실패 경로 — 여기서도 소멸자가 복원한다 */
    }
    g_credits = g_credits - n;
    g_consumed = g_consumed + n;
    return true; /* 성공 경로 */
}

static void produce(std::uint32_t n)
{
    IrqLock lock;
    g_credits = g_credits + n;
}

/* credits 와 consumed 를 "같은 순간"의 값으로 함께 읽는다. 잠금이 없으면
 * 두 읽기 사이에 ISR이 credits 를 바꿔 합이 맞지 않는 스냅샷이 나온다. */
static void snapshot(std::uint32_t *credits, std::uint32_t *consumed)
{
    IrqLock lock;
    *credits = g_credits;
    *consumed = g_consumed;
}

/* 중첩: 이미 잠긴 구간에서 다시 잠금을 잡아도 바깥 구간의 상태가 유지된다.
 * saved_ 에 "이전 PRIMASK"를 넣기 때문에 안쪽 가드의 소멸자는 1을 복원한다. */
static void nested_producer(std::uint32_t *inner_saved_out)
{
    IrqLock outer;
    assert(core::get_primask() == 1u);
    {
        IrqLock inner;
        *inner_saved_out = inner.saved(); /* 바깥이 이미 잠갔으므로 1 */
        g_credits = g_credits + 1u;
    }
    /* 안쪽 가드가 죽었지만 바깥 구간이므로 여전히 잠겨 있어야 한다 */
    assert(core::get_primask() == 1u);
    g_credits = g_credits + 1u;
}

/* ------------------------------------------------------------------ */
/* 5. 가짜 인터럽트 전달                                               */
/* ------------------------------------------------------------------ */

/* 가짜 ISR: credits 를 하나 올린다(UART가 바이트를 받은 것에 해당). */
static void fake_isr()
{
    ++core::g_isr_runs;
    g_credits = g_credits + 1u;
}

static void irq_pend() { core::g_pending = core::g_pending + 1u; }

/* 인터럽트 컨트롤러 역할. PRIMASK가 0일 때만 pending 을 전달한다. */
static void irq_poll()
{
    while (core::g_pending != 0u && core::get_primask() == 0u) {
        core::g_pending = core::g_pending - 1u;
        fake_isr();
    }
}

/* ------------------------------------------------------------------ */
/* 6. 테스트                                                           */
/* ------------------------------------------------------------------ */

static void reset_state()
{
    core::g_primask = 0u;
    core::g_disable_calls = 0u;
    core::g_restore_calls = 0u;
    core::g_pending = 0u;
    core::g_isr_runs = 0u;
    g_credits = 0u;
    g_consumed = 0u;
}

static void test_scope_restores(void)
{
    reset_state();
    assert(core::get_primask() == 0u);
    {
        IrqLock lock;
        assert(lock.saved() == 0u);
        assert(core::get_primask() == 1u); /* 스코프 안에서는 잠겨 있다 */
    }
    assert(core::get_primask() == 0u); /* 스코프를 벗어나면 원래 값 */
    assert(core::g_disable_calls == 1u);
    assert(core::g_restore_calls == 1u);
    puts("ok  1: 스코프 진입에서 잠기고, 벗어날 때 이전 PRIMASK로 복원된다");
}

static void test_every_return_path_restores(void)
{
    reset_state();

    assert(try_consume(0u) == true); /* n == 0 경로 */
    assert(core::get_primask() == 0u);

    assert(try_consume(5u) == false); /* credits 부족 경로 */
    assert(core::get_primask() == 0u);

    produce(10u);
    assert(try_consume(4u) == true); /* 성공 경로 */
    assert(core::get_primask() == 0u);
    assert(g_credits == 6u);
    assert(g_consumed == 4u);

    /* 가드 4개(try_consume 3회 + produce 1회) = 생성자 4회, 소멸자 4회 */
    assert(core::g_disable_calls == 4u);
    assert(core::g_restore_calls == 4u);
    puts("ok  2: early return 3가지 경로 모두에서 소멸자가 정확히 1번씩 돈다");
}

static void test_nesting(void)
{
    reset_state();
    std::uint32_t inner_saved = 0xFFFFFFFFu;

    nested_producer(&inner_saved);

    assert(inner_saved == 1u);        /* 안쪽 가드가 본 이전 상태는 "잠김" */
    assert(core::get_primask() == 0u); /* 바깥까지 풀리면 원래대로 */
    assert(g_credits == 2u);
    assert(core::g_disable_calls == 2u);
    assert(core::g_restore_calls == 2u);
    puts("ok  3: 중첩 잠금 — 안쪽 가드는 1을 복원하고 바깥 가드만 0으로 되돌린다");
}

static void test_deep_nesting(void)
{
    reset_state();
    {
        IrqLock a;
        assert(a.saved() == 0u);
        {
            IrqLock b;
            assert(b.saved() == 1u);
            {
                IrqLock c;
                assert(c.saved() == 1u);
                assert(core::get_primask() == 1u);
            }
            assert(core::get_primask() == 1u);
        }
        assert(core::get_primask() == 1u);
    }
    assert(core::get_primask() == 0u);
    assert(core::g_disable_calls == 3u && core::g_restore_calls == 3u);
    puts("ok  4: 3중 중첩에서도 생성자 3회 / 소멸자 3회, 마지막에만 열린다");
}

static void test_isr_is_blocked_inside_lock(void)
{
    reset_state();

    irq_pend();
    irq_poll(); /* 열려 있으므로 전달된다 */
    assert(core::g_isr_runs == 1u);
    assert(g_credits == 1u);

    {
        IrqLock lock;
        irq_pend();
        irq_pend();
        irq_poll(); /* 잠겨 있으므로 아무것도 전달되지 않는다 */
        assert(core::g_isr_runs == 1u);
        assert(core::g_pending == 2u);
        g_credits = g_credits + 10u; /* 방해받지 않는 read-modify-write */
    }
    assert(g_credits == 11u);

    irq_poll(); /* 이제 밀린 두 건이 전달된다 */
    assert(core::g_isr_runs == 3u);
    assert(g_credits == 13u);
    puts("ok  5: 잠긴 구간에서는 ISR이 돌지 못하고, 풀린 뒤 밀린 요청이 전달된다");
}

static void test_snapshot_is_consistent(void)
{
    reset_state();
    produce(7u);
    assert(try_consume(3u) == true);

    std::uint32_t c = 0u;
    std::uint32_t d = 0u;
    snapshot(&c, &d);
    assert(c == 4u && d == 3u);
    assert(c + d == 7u); /* 불변식: credits + consumed == 생산 총량 */
    assert(core::get_primask() == 0u);
    puts("ok  6: 두 변수를 한 잠금 안에서 읽어 일관된 스냅샷을 얻는다");
}

static void test_guard_is_balanced_under_loop(void)
{
    reset_state();
    produce(1000u);

    std::uint32_t taken = 0u;
    for (std::uint32_t i = 0u; i < 1000u; ++i) {
        if (try_consume(1u)) {
            ++taken;
        }
        assert(core::get_primask() == 0u); /* 매 바퀴마다 열려 있어야 한다 */
    }
    assert(taken == 1000u);
    assert(try_consume(1u) == false);
    /* produce 1 + try_consume 1001 = 1002 쌍 */
    assert(core::g_disable_calls == 1002u);
    assert(core::g_restore_calls == 1002u);
    assert(core::g_disable_calls == core::g_restore_calls);
    puts("ok  7: 1000바퀴를 돌아도 생성자/소멸자 횟수가 정확히 같다");
}

int main(void)
{
    printf("IrqLock: sizeof=%zu, 가짜 PRIMASK 변수 하나로 잠금을 관찰한다\n",
           sizeof(IrqLock));

    test_scope_restores();
    test_every_return_path_restores();
    test_nesting();
    test_deep_nesting();
    test_isr_is_blocked_inside_lock();
    test_snapshot_is_consistent();
    test_guard_is_balanced_under_loop();

    puts("ALL TESTS PASSED");
    return 0;
}
