// LLAT-A06 demo_asm — hot path 가 실제로 무슨 명령이 되는지 (arm64, Apple clang 21)
// build: c++ -std=c++20 -O2 -c demo_asm.cpp -o /tmp/LLAT-A06_work/asm.o
//        xcrun llvm-objdump -d --no-show-raw-insn --symbolize-operands /tmp/LLAT-A06_work/asm.o | c++filt
// 보는 것: 함수 내부 static 가드 확인(ldarb), thread_local 접근(Mach-O TLV 디스크립터 호출), cntvct_el0 읽기,
//          reserve 의 tail relaxed load(ldr) / head acquire load(ldar), commit 의 release store(stlr).
#define NLOG_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.cpp"

__attribute__((noinline)) void hot_log(Logger &lg, int id, double px) {
    NLOG(lg, INF, "order id=%d px=%.2f", id, px);
}
int main() {
    TextSink s;
    Logger lg(Logger::Config{}, s);
    hot_log(lg, 1, 2.0);
    lg.poll();
    std::fputs(s.text.c_str(), stdout);
}
