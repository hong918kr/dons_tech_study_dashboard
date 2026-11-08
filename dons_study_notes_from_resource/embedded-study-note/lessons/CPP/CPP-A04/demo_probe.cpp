// CPP-A04 demo_probe.cpp — lock-free 값을 "일부러 실패하는 템플릿" 으로 출력 (asm_targets.sh)
//   에러 메시지의 Show<...> 인자 = is_always_lock_free(u8,u16,u32,u64), ATOMIC_INT/LLONG/POINTER_LOCK_FREE
#include <atomic>
#include <cstdint>
template <bool b8, bool b16, bool b32, bool b64, int m_int, int m_llong, int m_ptr> struct Show;   // 정의 없음
Show<std::atomic<uint8_t>::is_always_lock_free, std::atomic<uint16_t>::is_always_lock_free,
     std::atomic<uint32_t>::is_always_lock_free, std::atomic<uint64_t>::is_always_lock_free,
     ATOMIC_INT_LOCK_FREE, ATOMIC_LLONG_LOCK_FREE, ATOMIC_POINTER_LOCK_FREE> probe;
