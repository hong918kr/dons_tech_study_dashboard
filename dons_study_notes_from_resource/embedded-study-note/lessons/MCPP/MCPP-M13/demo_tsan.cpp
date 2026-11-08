// MCPP-M13 demo_tsan.cpp — 탐색기(Part B)가 판정한 프로그램들을 진짜 스레드로 돌려 TSan 과 대조한다.
// 각 모드는 탐색기의 프로그램 하나와 같은 모양이다 (solution.cpp 의 prog_* 와 이름이 같다).
//
//   c++ -std=c++20 -g -O1 -fsanitize=thread -pthread demo_tsan.cpp -o dt && ./dt <mode>
//   c++ -std=c++20 -O2 -pthread demo_tsan.cpp -o dt_n && ./dt_n sb_guarded     (TSan 없이: 실제 SB 관찰 횟수)
//
// mode: mp_relaxed mp_relacq fence_fence fence_op op_fence rs_rmw rs_ldst rs_cxx11 chain3 sb_guarded sb_seqcst
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

static int data;                       // 비원자 "페이로드"
static std::atomic<int> flag{0}, cnt{0}, x{0}, y{0}, flag2{0};
using std::memory_order_relaxed, std::memory_order_acquire, std::memory_order_release,
      std::memory_order_seq_cst, std::memory_order_acq_rel;

static void spin_until(std::atomic<int> &a, int v, std::memory_order mo) {
    while (a.load(mo) != v) std::this_thread::yield();
}

int main(int argc, char **argv) {
    const char *m = argc > 1 ? argv[1] : "mp_relaxed";
    int rounds = 1, seen = 0, both = 0;
    if (!std::strcmp(m, "mp_relaxed") || !std::strcmp(m, "mp_relacq")) {
        bool ra = !std::strcmp(m, "mp_relacq");
        std::thread w([&] { data = 42; flag.store(1, ra ? memory_order_release : memory_order_relaxed); });
        std::thread r([&] { spin_until(flag, 1, ra ? memory_order_acquire : memory_order_relaxed); seen = data; });
        w.join(); r.join();
    } else if (!std::strcmp(m, "fence_fence")) {          // [atomics.fences]/2
        std::thread w([&] { data = 42; std::atomic_thread_fence(memory_order_release); flag.store(1, memory_order_relaxed); });
        std::thread r([&] { spin_until(flag, 1, memory_order_relaxed); std::atomic_thread_fence(memory_order_acquire); seen = data; });
        w.join(); r.join();
    } else if (!std::strcmp(m, "fence_op")) {             // [atomics.fences]/3
        std::thread w([&] { data = 42; std::atomic_thread_fence(memory_order_release); flag.store(1, memory_order_relaxed); });
        std::thread r([&] { spin_until(flag, 1, memory_order_acquire); seen = data; });
        w.join(); r.join();
    } else if (!std::strcmp(m, "op_fence")) {             // [atomics.fences]/4
        std::thread w([&] { data = 42; flag.store(1, memory_order_release); });
        std::thread r([&] { spin_until(flag, 1, memory_order_relaxed); std::atomic_thread_fence(memory_order_acquire); seen = data; });
        w.join(); r.join();
    } else if (!std::strcmp(m, "rs_rmw")) {               // release sequence 를 RMW 가 잇는다 → 레이스 없음
        std::thread p([&] { data = 42; cnt.store(2, memory_order_release); });
        auto cons = [&] {
            while (cnt.load(memory_order_relaxed) <= 0) std::this_thread::yield();
            if (cnt.fetch_sub(1, memory_order_acquire) > 0) (void)*(volatile int *)&data;
        };
        std::thread c1(cons), c2(cons);
        p.join(); c1.join(); c2.join();
    } else if (!std::strcmp(m, "rs_ldst")) {              // RMW 대신 load+store → release sequence 끊김 → 레이스
        std::thread p([&] { data = 42; cnt.store(2, memory_order_release); });
        std::thread relay([&] {
            spin_until(cnt, 2, memory_order_acquire);
            cnt.store(1, memory_order_relaxed);           // RMW 가 아닌 순수 store: P 의 release sequence 밖
        });
        std::thread c([&] { spin_until(cnt, 1, memory_order_acquire); seen = data; });
        p.join(); relay.join(); c.join();
    } else if (!std::strcmp(m, "rs_cxx11")) {             // 같은 스레드의 relaxed store: C++11 은 OK, C++20 은 레이스
        std::thread w([&] { data = 42; flag.store(1, memory_order_release); flag.store(2, memory_order_relaxed); });
        std::thread r([&] { spin_until(flag, 2, memory_order_acquire); seen = data; });
        w.join(); r.join();
    } else if (!std::strcmp(m, "chain3")) {               // T0 →(flag) T1 →(flag2) T2 : 전이적 hb
        std::thread t0([&] { data = 42; flag.store(1, memory_order_release); });
        std::thread t1([&] { spin_until(flag, 1, memory_order_acquire); flag2.store(1, memory_order_release); });
        std::thread t2([&] { spin_until(flag2, 1, memory_order_acquire); seen = data; });
        t0.join(); t1.join(); t2.join();
    } else if (!std::strcmp(m, "sb_guarded") || !std::strcmp(m, "sb_seqcst")) {
        // T1: x=1; if (y==0) data=1   T2: y=1; if (x==0) data=2  — SC 실행에선 둘 다 0 을 못 읽는다.
        std::memory_order st = !std::strcmp(m, "sb_seqcst") ? memory_order_seq_cst : memory_order_release;
        std::memory_order ld = !std::strcmp(m, "sb_seqcst") ? memory_order_seq_cst : memory_order_acquire;
        rounds = 200000;
        std::atomic<int> go{0}, done{0};
        auto body = [&](int me) {
            for (int i = 1; i <= rounds; i++) {
                while (go.load(memory_order_acquire) < i) { }
                bool wrote;
                if (me == 0) { x.store(i, st); wrote = y.load(ld) < i; if (wrote) data = 1; }
                else         { y.store(i, st); wrote = x.load(ld) < i; if (wrote) data = 2; }
                if (wrote) cnt.fetch_add(1, memory_order_relaxed);
                done.fetch_add(1, memory_order_acq_rel);
            }
        };
        std::thread a(body, 0), b(body, 1);
        for (int i = 1; i <= rounds; i++) {
            cnt.store(0, memory_order_relaxed);
            go.store(i, memory_order_release);
            while (done.load(memory_order_acquire) < 2 * i) { }
            if (cnt.load(memory_order_relaxed) == 2) both++;     // 두 스레드 모두 상대의 옛 값을 읽음 = 비-SC 실행
        }
        a.join(); b.join();
    } else { std::printf("unknown mode %s\n", m); return 2; }
    std::printf("mode=%s rounds=%d seen=%d both_wrote=%d\n", m, rounds, seen, both);
    return 0;
}
