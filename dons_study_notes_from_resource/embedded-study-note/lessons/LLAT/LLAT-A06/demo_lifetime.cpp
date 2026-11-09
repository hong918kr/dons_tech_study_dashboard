// LLAT-A06 demo_lifetime — 문자열 인자를 "포인터로" 저장하면: 소비자가 읽을 때 이미 해제된 메모리 (ASan)
// build: c++ -std=c++20 -g -O1 -fsanitize=address -fno-omit-frame-pointer -pthread demo_lifetime.cpp -o /tmp/LLAT-A06_work/life
//        /tmp/LLAT-A06_work/life ptr    → heap-use-after-free 보고 (exit ≠ 0)
//        /tmp/LLAT-A06_work/life copy   → 이 레슨의 로거(바이트 복사): 깨끗
#define NLOG_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.cpp"

static std::string make_name(int i) { return "sensor-" + std::to_string(i) + "-with-a-long-name"; }   // SSO 를 넘겨 힙에

int main(int argc, char **argv) {
    const bool ptr_mode = argc > 1 && std::strcmp(argv[1], "ptr") == 0;
    if (ptr_mode) {
        PtrLogger lg;                                   // 주어진 깨진 로거: const char* 만 큐에 넣는다
        {
            std::string name = make_name(7);
            lg.log(1, "name=%s", name.c_str());         // hot path: 포인터만 저장
        }                                               // name 소멸 → 힙 버퍼 해제
        std::fputs(lg.drain().c_str(), stdout);         // 백그라운드 포맷팅 시점: 해제된 메모리를 읽는다
    } else {
        TextSink s;
        Logger lg(Logger::Config{}, s);
        {
            std::string name = make_name(7);
            NLOG(lg, INF, "name=%s", name.c_str());     // 바이트를 링에 복사
        }
        lg.poll();
        std::fputs(s.text.c_str(), stdout);
    }
    return 0;
}
