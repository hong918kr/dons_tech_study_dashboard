// CPP-A05 demo_errors.cpp — thin 래퍼가 컴파일 단계에서 막는 것들. 기본 빌드는 통과, -DSHOW_ERROR 로 실제 에러를 본다.
//   c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only demo_errors.cpp               # 통과
//   c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR demo_errors.cpp  # 에러 5개
#include "demo_vec.hpp"

struct CanFrame { std::uint32_t id; std::uint8_t dlc; std::uint8_t pad[3]; std::uint8_t data[8]; };
struct CanFrameLoose { std::uint32_t id; std::uint8_t dlc; std::uint8_t data[8]; };   // 3 B 컴파일러 패딩
struct MoveOnlyPod { std::uint32_t v; MoveOnlyPod(const MoveOnlyPod&) = delete; MoveOnlyPod(MoveOnlyPod&&) = default; };

static_assert(std::is_trivially_copyable_v<MoveOnlyPod> && !thin::MemcpyElem<MoveOnlyPod>);
static_assert(!std::has_unique_object_representations_v<CanFrameLoose>);

std::size_t count_ids(thin::vec_ref<CanFrame> q) { return q.size(); }

void ok_uses() {
    thin::static_vector<CanFrame, 16> a;
    thin::static_vector<CanFrame, 4> b;
    (void)count_ids(a);          // N 이 달라도 같은 함수
    (void)count_ids(b);
    (void)a.bytes();
}

#ifdef SHOW_ERROR
void bad_uses() {
    thin::static_vector<CanFrameLoose, 4> loose;
    (void)loose.bytes();                          // ① 패딩 있는 T 의 바이트 뷰 (CRC/DMA 금지)
    (void)count_ids(loose);                       // ② 다른 T 의 벡터를 vec_ref<CanFrame> 으로
    thin::static_vector<std::uint8_t, 70000> big; // ③ 용량이 uint16_t 를 넘음
    (void)big;
    thin::static_vector<CanFrame, 16> a;
    auto copy = a;                                // ④ 복사 금지 (fat 과 같은 계약)
    (void)copy;
    thin::static_vector<MoveOnlyPod, 4> m;        // ⑤ 복사 불가 T (trivially copyable 이어도) — std::copyable 제약
    (void)m;
}
#endif
