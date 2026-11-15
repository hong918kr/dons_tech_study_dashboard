/* CPP-A07 demo_mfp.cpp — 상태 핸들러를 무엇으로 저장할까: 멤버 함수 포인터 vs 일반 함수 포인터 vs 가상 함수.
 * 헤더 없음 (recipe a). 같은 일: "현재 상태 핸들러를 e 로 부르고 결과를 돌려준다".
 *   bash target.sh mfp   → thumbv7em(Cortex-M4) 와 arm64(호스트) asm */
using u16 = unsigned short;
struct Event { u16 sig; };
enum class Status : unsigned char { Handled, Ignored, Tran };

struct A {                                     // (1) 이 레슨 방식: Status (A::*)(const Event&)
    using State = Status (A::*)(const Event&);
    State state_;
    int x = 0;
    Status s1(const Event&);
    Status s2(const Event&);
};
Status call_mfp(A& a, const Event& e) { return (a.*a.state_)(e); }

struct B;                                      // (2) C 스타일: Status (*)(B&, const Event&)
using BState = Status (*)(B&, const Event&);
struct B { BState state_; int x = 0; };
Status call_fp(B& b, const Event& e) { return b.state_(b, e); }

struct C {                                     // (3) 상태 = 가상 함수를 가진 객체 (State 패턴)
    struct St { virtual Status handle(C&, const Event&) const = 0; };
    const St* state_;
    int x = 0;
};
Status call_virt(C& c, const Event& e) { return c.state_->handle(c, e); }

unsigned sizes[] = {sizeof(A::State), sizeof(BState), sizeof(A), sizeof(B), sizeof(C)};
