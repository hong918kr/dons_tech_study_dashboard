// CPP-A04 demo_policy_targets.cpp — 같은 소스, 타깃마다 다른 판정 (asm_targets.sh 가 빌드한다)
//   M3/M4 : g_events = LDREX/STREX, g_bytes = PRIMASK 폴백, -DSHOW_ERROR → static_assert (64비트 strict)
//   M0+   : g_events 부터 static_assert (32비트도 lock-free 아님)
//   host  : 둘 다 atomic (ldaddal)
#include "isr_shared.hpp"

isr_shared<uint32_t> g_events;                                 // strict: lock-free 아니면 빌드 실패
isr_shared<uint64_t, NativeTarget, AllowIrqFallback> g_bytes;  // 허용: 안 되면 인터럽트 차단
#ifdef SHOW_ERROR
isr_shared<uint64_t> g_bad;                                    // M3/M4 에서 64비트 strict → 에러
#endif

extern "C" uint32_t on_rx(uint32_t len) {
    g_bytes.fetch_add(len);
    return g_events.fetch_add(1);
}
extern "C" uint64_t read_bytes() { return g_bytes.load(); }
