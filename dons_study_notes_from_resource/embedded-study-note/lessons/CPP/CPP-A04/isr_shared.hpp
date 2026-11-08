// CPP-A04 isr_shared.hpp — ISR ↔ main 공유 변수 정책 (펌웨어에 그대로 넣는 판)
//
// solution.cpp 의 isr_shared 와 같은 정책에서 호스트 테스트용 훅(isr_window, ModelTarget)만 뺐다.
//   isr_shared<T>                                 : T 가 이 타깃에서 항상 lock-free 가 아니면 컴파일 에러
//   isr_shared<T, NativeTarget, AllowIrqFallback> : lock-free 가 아니면 PRIMASK 임계 구역으로 (단일 코어 전용)
// 멀티코어 MCU(RP2040, STM32H7 M7+M4 공유 변수 등)는 -DISR_SHARED_MULTICORE=1 → 폴백 금지 (HW 스핀락을 쓸 것).
//
// 사용 예와 타깃별 빌드: demo_policy_targets.cpp, asm_targets.sh
#pragma once
#include <atomic>
#include <cstdint>
#include <type_traits>

#ifndef ISR_SHARED_MULTICORE
#define ISR_SHARED_MULTICORE 0
#endif

#if defined(__ARM_ARCH_PROFILE) && (__ARM_ARCH_PROFILE == 'M')
// CMSIS 의 __get_PRIMASK() + __disable_irq() / __set_PRIMASK() 와 같은 명령. "memory" = 컴파일러 배리어.
inline uint32_t isr_irq_save() {
    uint32_t pm;
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(pm) : : "memory");
    return pm;
}
inline void isr_irq_restore(uint32_t pm) { __asm volatile("msr primask, %0" : : "r"(pm) : "memory"); }
#else
// 호스트 빌드: 테스트 하네스가 mock 으로 정의한다 (solution.cpp 의 irq_save/irq_restore 참고).
uint32_t isr_irq_save();
void isr_irq_restore(uint32_t pm);
#endif

class IrqCriticalSection {
public:
    [[nodiscard]] IrqCriticalSection() noexcept : saved_(isr_irq_save()) {}
    ~IrqCriticalSection() { isr_irq_restore(saved_); }
    IrqCriticalSection(const IrqCriticalSection&) = delete;
    IrqCriticalSection& operator=(const IrqCriticalSection&) = delete;
private:
    uint32_t saved_;
};

struct NativeTarget {
    template <class T> static constexpr bool lock_free = std::atomic<T>::is_always_lock_free;
    static constexpr bool multicore = ISR_SHARED_MULTICORE != 0;
};
struct RequireLockFree  { static constexpr bool irq_fallback = false; };
struct AllowIrqFallback { static constexpr bool irq_fallback = true; };

template <class T, class Target, class Policy> constexpr bool policy_ok() {
    if constexpr (!std::is_integral_v<T> || !std::is_unsigned_v<T> || std::is_same_v<T, bool>) {
        return false;
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
        if constexpr (uses_atomic) return v_.load();
        else { IrqCriticalSection cs; return v_; }
    }
    void store(T x) {
        if constexpr (uses_atomic) v_.store(x);
        else { IrqCriticalSection cs; v_ = x; }
    }
    T fetch_add(T d) {
        if constexpr (uses_atomic) return v_.fetch_add(d);
        else { IrqCriticalSection cs; const T old = v_; v_ = static_cast<T>(old + d); return old; }
    }
    T exchange(T x) {
        if constexpr (uses_atomic) return v_.exchange(x);
        else { IrqCriticalSection cs; const T old = v_; v_ = x; return old; }
    }
    bool compare_exchange(T& expected, T desired) {
        if constexpr (uses_atomic) return v_.compare_exchange_strong(expected, desired);
        else {
            IrqCriticalSection cs;
            if (v_ != expected) { expected = v_; return false; }
            v_ = desired;
            return true;
        }
    }
    T fetch_max(T x) {
        if constexpr (uses_atomic) {
            T cur = v_.load(std::memory_order_relaxed);
            while (cur < x && !v_.compare_exchange_weak(cur, x)) {
            }
            return cur;
        } else {
            IrqCriticalSection cs;
            const T cur = v_;
            if (cur < x) v_ = x;
            return cur;
        }
    }

private:
    std::conditional_t<uses_atomic, std::atomic<T>, T> v_{};
};
