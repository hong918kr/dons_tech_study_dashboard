/* CPP-A07 demo_errors.cpp — 프레임워크가 컴파일 단계에서 막는 실수 4가지 (각 블록을 -DSHOW_ERROR=n 으로 켠다)
 *   for n in 1 2 3 4; do c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=$n demo_errors.cpp; done
 * SHOW_ERROR 없이 빌드하면 정상 (exit 0). */
#define AO_NO_HARNESS
#include "solution.cpp"

static uint32_t read_reset_reason() { return 0; }            // constexpr 아님 (레지스터 읽기 흉내)

struct Other : Active<Other, 2> {
    constexpr explicit Other(uint8_t p) : Active(p, &Other::idle) {}
    Status idle(const Event&) { return Status::Ignored; }
};
struct Mine : Active<Mine, 2> {
    constexpr explicit Mine(uint8_t p) : Active(p, &Mine::idle) {}
    Status idle(const Event& e) {
#if SHOW_ERROR == 4
        if (e.sig == SIG_A) return tran(&Other::idle);           // 다른 AO 의 핸들러로 전이
#endif
        (void)e;
        return Status::Ignored;
    }
};
#if SHOW_ERROR == 1
struct Slow : Active<Slow, 2> {                                 // 생성자에서 하드웨어를 읽는 AO
    Slow() : Active(1, &Slow::idle), reason_(read_reset_reason()) {}
    Status idle(const Event&) { return Status::Ignored; }
    uint32_t reason_;
};
constinit Slow g_slow;                                          // 동적 초기화 → constinit 위반
#endif
struct NonTrivialEvt : Event { ~NonTrivialEvt() {} };

constinit Board g_board;
int main() {
    (void)read_reset_reason();
#if SHOW_ERROR == 2
    (void)g_board.fw.new_evt<NonTrivialEvt>(SIG_A);             // 소멸자 있는 이벤트를 풀에서
#endif
#if SHOW_ERROR == 3
    Button copy = g_board.btn;                                  // AO 복사 (큐 버퍼 포인터가 원본을 가리킴)
    (void)copy;
#endif
    Mine m{1}; Other o{2}; (void)m; (void)o;
    return 0;
}
