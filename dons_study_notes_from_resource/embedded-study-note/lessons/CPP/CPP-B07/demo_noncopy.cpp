/* CPP-B07 demo — 복사 가능한 가드가 왜 위험한지, 복사/이동 금지가 무엇을 막는지
 *   정상 실행:   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_noncopy.cpp && ./a.out
 *   에러 보기:   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -DSHOW_ERROR demo_noncopy.cpp
 */
#include <cstdio>
#include <utility>

static int g_locked;                         // 0/1 뮤텍스 흉내
static void lock()   { std::printf("  lock   (was %d)\n", g_locked);  g_locked = 1; }
static void unlock() { std::printf("  unlock (was %d)%s\n", g_locked, g_locked ? "" : "  <-- 이미 풀린 락을 또 풀었다!"); g_locked = 0; }

/* (1) 순진한 가드: 소멸자만 있고 복사를 막지 않음 → 컴파일러가 복사 생성자를 만들어 준다 */
struct NaiveLock {
    NaiveLock()  { lock(); }
    ~NaiveLock() { unlock(); }
};

/* (2) 올바른 가드 */
struct Bus {};
class [[nodiscard]] BusLock {
public:
    explicit BusLock(Bus&) { lock(); }
    ~BusLock() { unlock(); }
    BusLock(const BusLock&) = delete;
    BusLock& operator=(const BusLock&) = delete;
};

/* C++17 보장된 복사 생략: prvalue 반환은 이동 생성자가 없어도 된다 */
static BusLock acquire(Bus& b) { return BusLock{b}; }

static void naive_by_value(NaiveLock) {}     // 값으로 넘기면 복사본이 함수 끝에서 unlock

int main() {
    std::printf("[1] NaiveLock 을 값으로 넘김\n");
    {
        NaiveLock g;
        naive_by_value(g);                   // 복사본 소멸 → unlock (아직 임계 구역 안인데!)
        std::printf("  ...여기는 아직 g 의 스코프 (락이 이미 풀림: g_locked=%d)\n", g_locked);
    }                                        // g 소멸 → 두 번째 unlock
    std::printf("[2] acquire() 팩토리 — 이동 불가 타입도 prvalue 로 반환 가능\n");
    {
        Bus bus;
        BusLock l = acquire(bus);            // 객체는 l 자리에 바로 만들어진다 (복사/이동 0회)
        (void)l;
    }
#ifdef SHOW_ERROR
    Bus bus;
    BusLock a(bus);
    BusLock b = a;                           // E1: 복사 생성
    BusLock c(bus);
    c = a;                                   // E2: 복사 대입
    BusLock d = std::move(a);                // E3: 이동 (복사 삭제 → 이동 생성자 암시 선언 안 됨)
    BusLock{bus};                            // E4: 임시 객체 — 즉시 lock+unlock ([[nodiscard]])
    {
        BusLock(bus);                        // E5: "bus 라는 이름의 BusLock 변수" 선언으로 해석됨
    }
    struct F { static BusLock make(Bus& x) { BusLock l(x); return l; } };   // E6: 이름 있는 지역 반환(NRVO)은 이동 필요
#endif
    return 0;
}
