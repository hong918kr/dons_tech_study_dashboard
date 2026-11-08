// MCPP-M13 메모리 모델 규칙: fence·release sequence
//
// 호스트 모델: std::thread 하나 = 코어 하나 (듀얼 코어 MCU, Cortex-A + M 메일박스, DMA 엔진 흉내).
//   Part A  fence 기반 동기화 (fence–fence 양방향 메일박스) + RMW 가 잇는 release sequence (작업 나눠 갖기)
//   Part B  SC 인터리빙 전수 탐색기: 스텝 목록으로 만든 2~3 스레드 프로그램의 모든 인터리빙을 돌며
//           sb + sw 로 happens-before 를 계산하고 C++ 정의의 data race 를 찾는다 ([intro.races]/21)
//   Part C  DMA 디스크립터 링: C 와 공유하는 plain struct 의 ctrl 워드에 std::atomic_ref,
//           DMA 엔진은 relaxed 스캔 + acquire fence 한 번 (op–fence)
//
// 체크 테스트는 "올바른 구현이면 매 실행 항상 성립하는 것" 만 본다 (정확한 개수, 온전한 데이터, 탐색기 결과).
// TSan 과의 대조는 demo_tsan.cpp. 각 Part 의 멀티스레드 스트레스는 단위 테스트가 다 통과해야 돈다.
//
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread solution.cpp -o so && ./so   (starter.cpp 도 같은 명령)
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <type_traits>
#include <vector>

// ---- TSan 주석: TSan 은 단독 atomic_thread_fence 를 동기화로 보지 않는다 (demo_tsan.cpp 실측: 오탐).
//      fence 로 만든 동기화를 TSan 에게 알려 주는 표준적인 방법 (folly/abseil 도 같은 방식). 일반 빌드에선 빈 함수.
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#include <sanitizer/tsan_interface.h>
#define MCPP_TSAN 1
#endif
#endif
[[maybe_unused]] static inline void tsan_release(const void *p) {
#ifdef MCPP_TSAN
    __tsan_release(const_cast<void *>(p));
#else
    (void)p;
#endif
}
[[maybe_unused]] static inline void tsan_acquire(const void *p) {
#ifdef MCPP_TSAN
    __tsan_acquire(const_cast<void *>(p));
#else
    (void)p;
#endif
}

using std::memory_order_relaxed, std::memory_order_acquire, std::memory_order_release;

// ======================= Part A — fence 동기화와 release sequence =======================
struct Msg { std::uint32_t seq, a, b, sum; };              // sum = seq ^ a ^ b ^ 0xA5A5A5A5 (찢어짐 검사)
static Msg make_msg(std::uint32_t s) { Msg m{s, s * 2654435761u, ~s, 0}; m.sum = m.seq ^ m.a ^ m.b ^ 0xA5A5A5A5u; return m; }
static bool msg_ok(const Msg &m) { return m.sum == (m.seq ^ m.a ^ m.b ^ 0xA5A5A5A5u); }

// 1칸 메일박스. full 의 load/store 는 전부 relaxed — 순서는 오직 fence 가 만든다 ([atomics.fences]/2).
struct FenceMailbox { std::atomic<std::uint32_t> full{0}; Msg slot{}; };

// A1. 비어 있으면 slot 에 쓰고 true. 차 있으면 아무것도 안 하고 false.
//     ① 반납 확인: 소비자가 slot 을 다 읽었다는 것과 동기화 (acquire fence)  ② 게시: slot 쓰기를 full=1 보다 먼저 (release fence)
//     fence 뒤/앞에 tsan_acquire(&m.full) / tsan_release(&m.full) 를 붙인다 (TSan 빌드 전용 주석).
static bool fmb_try_send(FenceMailbox &m, const Msg &v) {
    if (m.full.load(memory_order_relaxed) != 0u) return false;
    std::atomic_thread_fence(memory_order_acquire);    // 소비자의 release fence 와 짝 (반납 방향)
    tsan_acquire(&m.full);
    m.slot = v;                                        // 비원자 쓰기
    tsan_release(&m.full);
    std::atomic_thread_fence(memory_order_release);    // 위의 slot 쓰기가 아래 store 보다 먼저
    m.full.store(1u, memory_order_relaxed);
    return true;
}
// A2. 차 있으면 slot 을 out 에 복사하고 비운 뒤 true.
static bool fmb_try_recv(FenceMailbox &m, Msg &out) {
    if (m.full.load(memory_order_relaxed) != 1u) return false;
    std::atomic_thread_fence(memory_order_acquire);    // 생산자의 release fence 와 짝 (게시 방향)
    tsan_acquire(&m.full);
    out = m.slot;                                      // 비원자 읽기
    tsan_release(&m.full);
    std::atomic_thread_fence(memory_order_release);    // slot 읽기가 반납(full=0)보다 먼저
    m.full.store(0u, memory_order_relaxed);
    return true;
}

// 작업 보드: 생산자가 job[0..n) 을 채우고 remaining 에 n 을 release-store. 작업자들은 fetch_sub 로 하나씩 가져간다.
// 두 번째 이후 작업자의 fetch_sub 는 생산자의 store 가 아니라 "다른 작업자의 RMW" 가 쓴 값을 읽는다.
// 그래도 생산자와 동기화되는 이유 = release sequence ([intro.races]/5): RMW 는 sequence 를 끊지 않는다.
constexpr int kJobs = 100000;
struct JobBoard { std::atomic<int> remaining{0}; std::uint32_t job[kJobs]; };
static std::uint32_t job_value(int i) { return static_cast<std::uint32_t>(i) * 2246822519u + 7u; }

// A3. job[0..n) = job_value(i) 를 쓰고 remaining 을 n 으로 게시.
static void board_publish(JobBoard &b, int n) {
    for (int i = 0; i < n; i++) b.job[i] = job_value(i);
    b.remaining.store(n, memory_order_release);        // release sequence 의 머리(head)
}
// A4. 가져갈 작업 번호(n-1, n-2, ..., 0)를 반환. 없으면 -1. (게시 전에 불려도 -1)
static int board_claim(JobBoard &b) {
    int r = b.remaining.fetch_sub(1, memory_order_acquire);   // RMW: 앞 RMW 가 쓴 값을 읽어도 head 와 동기화
    return r > 0 ? r - 1 : -1;
}

// ======================= Part B — SC 인터리빙 전수 탐색기 =======================
// 프로그램 = 스레드별 스텝 목록. 비원자 위치(NA)와 원자 객체(AT)는 이름 공간이 다르다.
enum class Op : std::uint8_t { W, R, ST, LD, AWAIT, RMW, FENCE };   // W/R = 비원자 쓰기/읽기
enum class Mo : std::uint8_t { rlx, acq, rel, acq_rel, sc };
enum class G : std::uint8_t { none, eq, gt };                        // 가드: 레지스터 값이 조건을 만족할 때만 실행
struct Step { Op op; int loc; int val; Mo mo; int dst; G g; int greg; int gval; };

enum : int { D = 0, BUF = 1 };                                      // 비원자 위치
enum : int { F = 0, F2 = 1, CNT = 2, X = 3, Y = 4 };                 // 원자 객체
constexpr int kMaxThreads = 3, kMaxAtomics = 5, kMaxRegs = 4, kMaxEvents = 64;
static const char *const kNaName[] = {"d", "buf"};
static const char *const kAtName[] = {"f", "f2", "c", "x", "y"};
static const char *const kMoName[] = {"rlx", "acq", "rel", "acq_rel", "sc"};

constexpr Step W_(int l) { return {Op::W, l, 0, Mo::rlx, -1, G::none, 0, 0}; }
constexpr Step R_(int l) { return {Op::R, l, 0, Mo::rlx, -1, G::none, 0, 0}; }
constexpr Step ST(int a, int v, Mo m) { return {Op::ST, a, v, m, -1, G::none, 0, 0}; }
constexpr Step LD(int a, Mo m, int dst) { return {Op::LD, a, 0, m, dst, G::none, 0, 0}; }
constexpr Step AWAIT(int a, int v, Mo m) { return {Op::AWAIT, a, v, m, -1, G::none, 0, 0}; }   // 값이 v 일 때만 실행 가능 (스핀 루프 대신)
constexpr Step RMW(int a, int add, Mo m, int dst) { return {Op::RMW, a, add, m, dst, G::none, 0, 0}; }   // fetch_add(add), 옛 값 → dst
constexpr Step FENCE(Mo m) { return {Op::FENCE, -1, 0, m, -1, G::none, 0, 0}; }
constexpr Step if_eq(Step s, int reg, int v) { s.g = G::eq; s.greg = reg; s.gval = v; return s; }
constexpr Step if_gt(Step s, int reg, int v) { s.g = G::gt; s.greg = reg; s.gval = v; return s; }

struct Program { const char *name; int nth; std::vector<Step> th[kMaxThreads]; int init[kMaxAtomics]; };

// 실행 흔적(trace)의 사건 하나. SC 실행이므로 trace 순서 = 각 원자 객체의 수정 순서(mo)의 근거.
struct Event {
    int tid, step;
    Op op; int loc; Mo mo;
    int rf;          // 읽기(LD/AWAIT/RMW)가 값을 가져온 쓰기 사건의 번호. -1 = 초기값
    int wval;        // 쓴 값 (ST/RMW) 또는 읽은 값 (LD/AWAIT)
    bool rmw;
};
struct Rules {
    bool fences = true;     // [atomics.fences]/2-4 적용 (false = fence 를 무시: TSan 이 하는 일)
    bool rmw_rs = true;     // release sequence 를 RMW 가 잇는다 ([intro.races]/5)
    bool cxx11_rs = false;  // C++11 규칙: head 와 같은 스레드의 store 도 sequence 를 잇는다 (C++20 에서 삭제, P0982R1)
};
struct Verdict {
    long execs = 0, racy = 0, blocked = 0;
    char first[200] = "";   // 첫 레이스 쌍
    char trace[400] = "";   // 그 실행의 흔적
};

[[maybe_unused]] static constexpr bool is_write(const Event &e) { return e.op == Op::ST || e.op == Op::RMW; }
[[maybe_unused]] static constexpr bool is_read(const Event &e) { return e.op == Op::LD || e.op == Op::AWAIT || e.op == Op::RMW; }
[[maybe_unused]] static constexpr bool is_na(const Event &e) { return e.op == Op::W || e.op == Op::R; }
[[maybe_unused]] static constexpr bool is_rel(Mo m) { return m == Mo::rel || m == Mo::acq_rel || m == Mo::sc; }
[[maybe_unused]] static constexpr bool is_acq(Mo m) { return m == Mo::acq || m == Mo::acq_rel || m == Mo::sc; }

static int fmt_event(char *out, std::size_t cap, const Event &e) {
    switch (e.op) {
    case Op::W: return std::snprintf(out, cap, "T%d:W %s", e.tid, kNaName[e.loc]);
    case Op::R: return std::snprintf(out, cap, "T%d:R %s", e.tid, kNaName[e.loc]);
    case Op::ST: return std::snprintf(out, cap, "T%d:%s=%d %s", e.tid, kAtName[e.loc], e.wval, kMoName[int(e.mo)]);
    case Op::LD: case Op::AWAIT:
        return std::snprintf(out, cap, "T%d:%s->%d %s", e.tid, kAtName[e.loc], e.wval, kMoName[int(e.mo)]);
    case Op::RMW: return std::snprintf(out, cap, "T%d:rmw %s:=%d %s", e.tid, kAtName[e.loc], e.wval, kMoName[int(e.mo)]);
    case Op::FENCE: return std::snprintf(out, cap, "T%d:fence %s", e.tid, kMoName[int(e.mo)]);
    }
    return 0;
}

// B1. 쓰기 사건 w 가 head 가 이끄는 release sequence 에 들어 있나? (head 가 release 가 아니어도 같은 계산 =
//     fence 규칙의 "hypothetical release sequence")
//     C++20: head 다음부터 w 까지, 같은 객체에 대한 쓰기가 전부 RMW 여야 한다. 하나라도 순수 store 면 끊긴다.
static bool in_release_seq(const Event *ev, int head, int w, const Rules &r) {
    if (head < 0 || w < 0 || w < head) return false;
    const Event &A = ev[head];
    if (!is_write(A) || !is_write(ev[w]) || ev[w].loc != A.loc) return false;
    for (int k = head + 1; k <= w; k++) {
        const Event &e = ev[k];
        if (!is_write(e) || e.loc != A.loc) continue;          // 같은 객체의 쓰기만 mo 에 들어간다
        bool continues = (r.rmw_rs && e.rmw) || (r.cxx11_rs && e.tid == A.tid);
        if (!continues) return false;
    }
    return true;
}

// B2. synchronizes-with 간선. sw[i] = i 에서 출발하는 sw 간선의 도착 사건 비트셋.
//     읽기 b 가 쓰기 a 의 (가상) release sequence 안의 값을 읽었으면:
//       출발 = { a (release 면) } ∪ { a 보다 먼저 같은 스레드에서 실행된 release fence }
//       도착 = { b (acquire 면) } ∪ { b 보다 나중에 같은 스레드에서 실행된 acquire fence }
//     출발×도착 네 조합이 각각 op–op [atomics.order]/2, fence–op /3, op–fence /4, fence–fence /2 ([atomics.fences]).
//     반환: 간선 개수.
static int build_sw(const Event *ev, int n, const Rules &r, std::uint64_t *sw) {
    int edges = 0;
    for (int i = 0; i < n; i++) sw[i] = 0;
    for (int b = 0; b < n; b++) {
        if (!is_read(ev[b]) || ev[b].rf < 0) continue;
        for (int a = 0; a <= ev[b].rf; a++) {
            if (!in_release_seq(ev, a, ev[b].rf, r)) continue;
            std::uint64_t targets = 0;
            if (is_acq(ev[b].mo)) targets |= 1ull << b;
            if (r.fences)
                for (int g = b + 1; g < n; g++)
                    if (ev[g].tid == ev[b].tid && ev[g].op == Op::FENCE && is_acq(ev[g].mo)) targets |= 1ull << g;
            if (!targets) continue;
            auto add_from = [&](int s) {
                for (int t = 0; t < n; t++)
                    if (((targets >> t) & 1u) && !((sw[s] >> t) & 1u)) { sw[s] |= 1ull << t; edges++; }
            };
            if (is_rel(ev[a].mo)) add_from(a);
            if (r.fences)
                for (int f = 0; f < a; f++)
                    if (ev[f].tid == ev[a].tid && ev[f].op == Op::FENCE && is_rel(ev[f].mo)) add_from(f);
        }
    }
    return edges;
}

// B3. hb = (sb ∪ sw)+ 를 계산하고, 서로 다른 스레드의 충돌하는 비원자 접근 쌍 중 hb 로 순서가 없는 첫 쌍을 찾는다.
//     모든 간선은 trace 앞 → 뒤로만 간다 (sw 의 출발은 읽힌 쓰기보다 앞, 도착은 읽기보다 뒤) → 뒤에서부터 한 번에 닫힌다.
//     레이스가 있으면 true 와 (*ra, *rb) = 사건 번호 (ra < rb).
static bool find_race(const Event *ev, int n, const Rules &r, int *ra, int *rb) {
    std::uint64_t sw[kMaxEvents], hb[kMaxEvents];
    build_sw(ev, n, r, sw);
    for (int i = n - 1; i >= 0; i--) {
        std::uint64_t succ = sw[i];
        for (int j = i + 1; j < n; j++)
            if (ev[j].tid == ev[i].tid) { succ |= 1ull << j; break; }   // sb: 같은 스레드의 바로 다음 사건 (나머지는 전이로)
        hb[i] = succ;
        for (int j = i + 1; j < n; j++)
            if ((succ >> j) & 1u) hb[i] |= hb[j];
    }
    for (int a = 0; a < n; a++) {
        if (!is_na(ev[a])) continue;
        for (int b = a + 1; b < n; b++) {
            if (!is_na(ev[b]) || ev[b].loc != ev[a].loc || ev[b].tid == ev[a].tid) continue;
            if (ev[a].op == Op::R && ev[b].op == Op::R) continue;           // 읽기끼리는 충돌이 아니다
            if (!((hb[a] >> b) & 1u)) { *ra = a; *rb = b; return true; }
        }
    }
    return false;
}

struct Explorer {
    const Program *p; Rules rules; Verdict v;
    Event ev[kMaxEvents]; int n = 0;
    int pc[kMaxThreads] = {}; int reg[kMaxThreads][kMaxRegs] = {};
    int val[kMaxAtomics] = {}; int lastw[kMaxAtomics] = {};
};

static bool guard_ok(const Explorer &x, int t, const Step &s) {
    if (s.g == G::none) return true;
    int rv = x.reg[t][s.greg];
    return s.g == G::eq ? rv == s.gval : rv > s.gval;
}
static void skip_guarded(Explorer &x, int t) {          // 가드가 거짓인 스텝은 사건 없이 건너뛴다 (결정적)
    const std::vector<Step> &th = x.p->th[t];
    while (x.pc[t] < (int)th.size() && !guard_ok(x, t, th[x.pc[t]])) x.pc[t]++;
}
[[maybe_unused]] static void on_leaf(Explorer &x) {
    x.v.execs++;
    int a, b;
    if (!find_race(x.ev, x.n, x.rules, &a, &b)) return;
    if (x.v.racy++ == 0) {
        char ea[48], eb[48];
        fmt_event(ea, sizeof ea, x.ev[a]); fmt_event(eb, sizeof eb, x.ev[b]);
        std::snprintf(x.v.first, sizeof x.v.first, "%s  vs  %s", ea, eb);
        std::size_t off = 0;
        for (int i = 0; i < x.n && off + 50 < sizeof x.v.trace; i++) {
            off += (std::size_t)fmt_event(x.v.trace + off, sizeof x.v.trace - off, x.ev[i]);
            if (i + 1 < x.n) off += (std::size_t)std::snprintf(x.v.trace + off, sizeof x.v.trace - off, " | ");
        }
    }
}

// B4. 깊이 우선 탐색: 실행 가능한 스레드(끝나지 않았고, AWAIT 이면 값이 맞는) 하나를 골라 한 스텝 실행 → 재귀 → 되돌리기.
//     모든 스레드가 끝나면 on_leaf, 남은 스레드가 있는데 아무도 못 움직이면 blocked++.
//     스텝 실행: ST(값 쓰기, lastw 갱신), LD/AWAIT(rf = lastw, dst 레지스터), RMW(옛 값 읽고 old+val 쓰기, rmw=true),
//     W/R/FENCE(상태 변화 없음). 사건을 ev[n++] 에 넣고 pc++ 한 뒤 skip_guarded.
static void dfs(Explorer &x) {
    bool left = false, moved = false;
    for (int t = 0; t < x.p->nth; t++) {
        const std::vector<Step> &th = x.p->th[t];
        if (x.pc[t] >= (int)th.size()) continue;
        left = true;
        const Step s = th[x.pc[t]];
        if (s.op == Op::AWAIT && x.val[s.loc] != s.val) continue;
        if (x.n >= kMaxEvents) { x.v.blocked++; return; }
        moved = true;
        const int save_pc = x.pc[t];
        int save_reg[kMaxRegs];
        std::memcpy(save_reg, x.reg[t], sizeof save_reg);
        const int a = s.loc >= 0 ? s.loc : 0;
        const int save_val = x.val[a], save_lastw = x.lastw[a];
        Event e{t, x.pc[t], s.op, s.loc, s.mo, -1, 0, false};
        switch (s.op) {
        case Op::ST: e.wval = s.val; x.val[a] = s.val; x.lastw[a] = x.n; break;
        case Op::LD: case Op::AWAIT:
            e.rf = x.lastw[a]; e.wval = x.val[a];
            if (s.dst >= 0) x.reg[t][s.dst] = x.val[a];
            break;
        case Op::RMW: {
            int old = x.val[a];
            e.rf = x.lastw[a]; e.rmw = true; e.wval = old + s.val;
            x.val[a] = old + s.val; x.lastw[a] = x.n;
            if (s.dst >= 0) x.reg[t][s.dst] = old;
            break;
        }
        case Op::W: case Op::R: case Op::FENCE: break;
        }
        x.ev[x.n++] = e;
        x.pc[t]++;
        skip_guarded(x, t);
        dfs(x);
        x.n--;
        x.pc[t] = save_pc;
        std::memcpy(x.reg[t], save_reg, sizeof save_reg);
        x.val[a] = save_val; x.lastw[a] = save_lastw;
    }
    if (!left) on_leaf(x);
    else if (!moved) x.v.blocked++;
}

static Verdict explore(const Program &p, Rules r = {}) {
    Explorer x;
    x.p = &p; x.rules = r;
    for (int i = 0; i < kMaxAtomics; i++) { x.val[i] = p.init[i]; x.lastw[i] = -1; }
    for (int t = 0; t < p.nth; t++) skip_guarded(x, t);
    dfs(x);
    return x.v;
}

// ---- 탐색할 프로그램들 (demo_tsan.cpp 의 모드와 이름이 같다). r0 = 레지스터 0.
static const Program prog_mp_relaxed{"mp_relaxed", 2, {{W_(D), ST(F, 1, Mo::rlx)},
                                                     {LD(F, Mo::rlx, 0), if_eq(R_(D), 0, 1)}}, {}};
static const Program prog_mp_relacq{"mp_relacq", 2, {{W_(D), ST(F, 1, Mo::rel)},
                                                   {LD(F, Mo::acq, 0), if_eq(R_(D), 0, 1)}}, {}};
static const Program prog_fence_fence{"fence_fence", 2, {{W_(D), FENCE(Mo::rel), ST(F, 1, Mo::rlx)},
                                                       {LD(F, Mo::rlx, 0), if_eq(FENCE(Mo::acq), 0, 1), if_eq(R_(D), 0, 1)}}, {}};
static const Program prog_fence_op{"fence_op", 2, {{W_(D), FENCE(Mo::rel), ST(F, 1, Mo::rlx)},
                                                 {LD(F, Mo::acq, 0), if_eq(R_(D), 0, 1)}}, {}};
static const Program prog_op_fence{"op_fence", 2, {{W_(D), ST(F, 1, Mo::rel)},
                                                 {LD(F, Mo::rlx, 0), if_eq(FENCE(Mo::acq), 0, 1), if_eq(R_(D), 0, 1)}}, {}};
static const Program prog_fence_one_side{"fence_one_side", 2, {{W_(D), FENCE(Mo::rel), ST(F, 1, Mo::rlx)},
                                                             {LD(F, Mo::rlx, 0), if_eq(R_(D), 0, 1)}}, {}};
// release sequence: 생산자 1 + 소비자 2. 소비자: r0 = c.fetch_sub(1, acquire); if (r0 > 0) read d
static const Program prog_rs_rmw{"rs_rmw", 3, {{W_(D), ST(CNT, 2, Mo::rel)},
                                             {RMW(CNT, -1, Mo::acq, 0), if_gt(R_(D), 0, 0)},
                                             {RMW(CNT, -1, Mo::acq, 0), if_gt(R_(D), 0, 0)}}, {}};
// 중계 스레드가 RMW 대신 순수 store 로 값을 바꾸면 sequence 가 끊긴다
static const Program prog_rs_ldst{"rs_ldst", 3, {{W_(D), ST(CNT, 2, Mo::rel)},
                                               {AWAIT(CNT, 2, Mo::acq), ST(CNT, 1, Mo::rlx)},
                                               {AWAIT(CNT, 1, Mo::acq), R_(D)}}, {}};
// 고친 판: 중계 스레드가 relaxed RMW 로 (순서 인자는 relaxed 여도 RMW 라서 sequence 가 이어진다)
static const Program prog_rs_relay_rmw{"rs_relay_rmw", 3, {{W_(D), ST(CNT, 2, Mo::rel)},
                                                         {AWAIT(CNT, 2, Mo::rlx), RMW(CNT, -1, Mo::rlx, 0)},
                                                         {AWAIT(CNT, 1, Mo::acq), R_(D)}}, {}};
// C++11 에선 OK, C++20 에선 레이스: release store 뒤 같은 스레드의 relaxed store
static const Program prog_rs_cxx11{"rs_cxx11", 2, {{W_(D), ST(F, 1, Mo::rel), ST(F, 2, Mo::rlx)},
                                                 {AWAIT(F, 2, Mo::acq), R_(D)}}, {}};
// 3 스레드 사슬: T0 →(f) T1 →(f2) T2.  broken = T1 이 f 를 relaxed 로 기다림
static const Program prog_chain3{"chain3", 3, {{W_(D), ST(F, 1, Mo::rel)},
                                             {AWAIT(F, 1, Mo::acq), ST(F2, 1, Mo::rel)},
                                             {AWAIT(F2, 1, Mo::acq), R_(D)}}, {}};
static const Program prog_chain3_broken{"chain3_broken", 3, {{W_(D), ST(F, 1, Mo::rel)},
                                                           {AWAIT(F, 1, Mo::rlx), ST(F2, 1, Mo::rel)},
                                                           {AWAIT(F2, 1, Mo::acq), R_(D)}}, {}};
// SB 가드: T0: x=1; if (y==0) d=1   T1: y=1; if (x==0) d=2.  SC 실행에선 둘 다 0 을 못 읽는다 → 탐색기는 "레이스 없음".
// 하지만 acq/rel(또는 relaxed) 이면 C++ 모델상 둘 다 0 인 실행이 허용된다 → 실제로는 레이스 (탐색기의 한계, 5. 풀이)
static const Program prog_sb_guarded{"sb_guarded", 2, {{ST(X, 1, Mo::rel), LD(Y, Mo::acq, 0), if_eq(W_(D), 0, 0)},
                                                     {ST(Y, 1, Mo::rel), LD(X, Mo::acq, 0), if_eq(W_(D), 0, 0)}}, {}};

// ======================= Part C — DMA 디스크립터 링 + atomic_ref =======================
// C 드라이버·하드웨어와 공유하는 레이아웃: atomic 타입이 없는 plain struct. 공유가 시작된 뒤 ctrl 은 오직 atomic_ref 로만 접근한다
// ([atomics.ref.generic]/3: atomic_ref 가 살아 있는 동안 그 객체의 모든 접근은 atomic_ref 를 통해야 한다).
struct dma_desc {
    std::uint32_t buf_off;   // 버퍼 위치 (모의: pool 인덱스)
    std::uint16_t len;
    std::uint16_t flags;
    std::uint32_t ctrl;      // bit31 OWN (1 = DMA 소유), bit30 DONE (완료, CPU 소유), 하위 16비트 seq. 0 = 빈 칸
    std::uint32_t status;    // DMA 가 쓰는 결과 (모의: 체크섬)
};
static_assert(sizeof(dma_desc) == 16 && offsetof(dma_desc, ctrl) == 8, "HW/C 레이아웃과 같아야 한다");
static_assert(std::is_standard_layout_v<dma_desc> && std::is_trivially_copyable_v<dma_desc>);
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free, "ISR/DMA 경로에서 락 기반 atomic 은 안 된다");
static_assert(alignof(std::uint32_t) >= std::atomic_ref<std::uint32_t>::required_alignment);

[[maybe_unused]] constexpr std::uint32_t OWN = 1u << 31, DONE = 1u << 30;
constexpr unsigned kRing = 8, kPkt = 64;
struct TxRing {
    alignas(64) dma_desc d[kRing];
    std::uint8_t pool[kRing][kPkt];
    unsigned head = 0, reap = 0;     // CPU 전용
    unsigned dma_tail = 0;           // DMA 엔진 전용
    unsigned max_batch = 0;          // 통계 (엔진 전용)
};
[[maybe_unused]] static inline std::atomic_ref<std::uint32_t> ctrl_of(dma_desc &d) { return std::atomic_ref<std::uint32_t>(d.ctrl); }
static std::uint32_t pkt_sum(const std::uint8_t *p, unsigned len) {   // FNV-1a ^ len
    std::uint32_t h = 2166136261u;
    for (unsigned i = 0; i < len; i++) h = (h ^ p[i]) * 16777619u;
    return h ^ len;
}

// C1. CPU: 다음 칸이 비었으면(ctrl == 0) 패킷을 pool 에 복사하고 디스크립터를 채운 뒤 OWN|seq 로 넘긴다. 꽉 차면 false.
static bool tx_submit(TxRing &r, const std::uint8_t *pkt, std::uint16_t len, std::uint16_t seq) {
    unsigned i = r.head % kRing;
    dma_desc &d = r.d[i];
    if (ctrl_of(d).load(memory_order_relaxed) != 0u) return false;   // 0 은 내가(CPU) reap 에서 쓴 값 → relaxed 로 충분
    std::memcpy(r.pool[i], pkt, len);
    d.buf_off = i; d.len = len; d.flags = 0; d.status = 0;           // 비원자 필드
    ctrl_of(d).store(OWN | seq, memory_order_release);               // 게시: 위의 쓰기가 이 store 보다 먼저
    r.head++;
    return true;
}
// C2. DMA 엔진: dma_tail 부터 OWN 인 디스크립터를 relaxed 로 연속 스캔 → 하나라도 있으면 acquire fence 한 번(op–fence) →
//     각 칸의 status = pkt_sum(pool[buf_off], len) 를 쓰고 DONE|seq 를 release-store. 처리한 개수 반환.
//     (TSan 빌드용: fence 뒤에 스캔한 각 칸의 &ctrl 에 tsan_acquire)
static unsigned dma_poll(TxRing &r) {
    std::uint32_t seen[kRing];
    unsigned n = 0;
    while (n < kRing) {
        std::uint32_t c = ctrl_of(r.d[(r.dma_tail + n) % kRing]).load(memory_order_relaxed);
        if (!(c & OWN)) break;
        seen[n++] = c;
    }
    if (n == 0) return 0;
    std::atomic_thread_fence(memory_order_acquire);    // 위 n 개 relaxed load 가 읽은 release store 전부와 동기화
    for (unsigned k = 0; k < n; k++) tsan_acquire(&r.d[(r.dma_tail + k) % kRing].ctrl);
    for (unsigned k = 0; k < n; k++) {
        dma_desc &d = r.d[(r.dma_tail + k) % kRing];
        d.status = pkt_sum(r.pool[d.buf_off], d.len);
        ctrl_of(d).store(DONE | (seen[k] & 0xFFFFu), memory_order_release);   // 완료 게시
    }
    r.dma_tail += n;
    if (n > r.max_batch) r.max_batch = n;
    return n;
}
// C3. CPU: reap 칸이 DONE 이면 *status 를 꺼내고 칸을 비운다(ctrl = 0). seq 반환, 아직이면 -1.
static int tx_reap(TxRing &r, std::uint32_t *status) {
    dma_desc &d = r.d[r.reap % kRing];
    std::uint32_t c = ctrl_of(d).load(memory_order_acquire);         // 엔진의 DONE release-store 와 동기화
    if (!(c & DONE)) return -1;
    *status = d.status;
    ctrl_of(d).store(0u, memory_order_relaxed);                      // 이후 이 칸은 CPU 만 만진다 (엔진은 OWN 만 본다)
    r.reap++;
    return static_cast<int>(c & 0xFFFFu);
}

// ======================= 테스트 하네스 =======================
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define FAILS() (g_total - g_pass)
static void skip(int n, const char *what) {
    g_total += n;
    std::printf("SKIP %d stress checks (%s): unit checks failed\n", n, what);
}

static FenceMailbox g_mb;
static JobBoard g_board;
static TxRing g_ring;

static bool expect(const Program &p, Rules r, long execs, long racy) {
    Verdict v = explore(p, r);
    std::printf("  %-15s fences=%d rmw_rs=%d cxx11=%d : execs=%ld racy=%ld blocked=%ld%s%s\n", p.name, r.fences, r.rmw_rs,
                r.cxx11_rs, v.execs, v.racy, v.blocked, v.racy ? "  first: " : "", v.first);
    if (v.racy) std::printf("      trace: %s\n", v.trace);
    return v.execs == execs && v.racy == racy && v.blocked == 0;
}

int main() {
    // ---------- Part A ----------
    int fa = FAILS();
    {
        FenceMailbox m;
        Msg out{};
        CHECK(!fmb_try_recv(m, out));                              // 빈 메일박스
        CHECK(fmb_try_send(m, make_msg(1)));
        CHECK(!fmb_try_send(m, make_msg(2)));                      // 차 있으면 덮어쓰지 않는다
        CHECK(fmb_try_recv(m, out) && out.seq == 1u && msg_ok(out));
        CHECK(fmb_try_send(m, make_msg(2)) && fmb_try_recv(m, out) && out.seq == 2u);
        JobBoard *b = new JobBoard{};                   // {} = 0 초기화 (미초기화 읽기는 UB)
        CHECK(board_claim(*b) == -1);                              // 게시 전
        b->remaining.store(0);
        board_publish(*b, 3);
        CHECK(b->job[0] == job_value(0) && b->job[2] == job_value(2));
        int c1 = board_claim(*b), c2 = board_claim(*b), c3 = board_claim(*b), c4 = board_claim(*b);
        CHECK(c1 == 2 && c2 == 1 && c3 == 0 && c4 == -1);
        delete b;
    }
    if (FAILS() == fa) {
        constexpr std::uint32_t N = 200000;
        std::uint32_t bad = 0, next = 0;
        std::thread prod([] {
            for (std::uint32_t s = 0; s < N; s++)
                while (!fmb_try_send(g_mb, make_msg(s))) std::this_thread::yield();
        });
        while (next < N) {
            Msg m;
            if (!fmb_try_recv(g_mb, m)) { std::this_thread::yield(); continue; }
            if (!msg_ok(m) || m.seq != next) bad++;
            next++;
        }
        prod.join();
        CHECK(next == N && bad == 0u);                             // 20만 개, 순서대로, 안 찢어짐

        static std::uint8_t taken[kJobs];
        std::atomic<std::uint32_t> wrong{0};
        std::atomic<int> started{0};
        std::vector<std::thread> w;
        for (int t = 0; t < 4; t++)
            w.emplace_back([&] {
                started.fetch_add(1, memory_order_relaxed);
                while (g_board.remaining.load(memory_order_relaxed) == 0) std::this_thread::yield();  // relaxed: 동기화는 claim 이 한다
                for (int i; (i = board_claim(g_board)) >= 0;) {
                    if (g_board.job[i] != job_value(i)) wrong.fetch_add(1, memory_order_relaxed);
                    taken[i]++;
                }
            });
        while (started.load(memory_order_relaxed) < 4) std::this_thread::yield();
        board_publish(g_board, kJobs);
        for (auto &t : w) t.join();
        int once = 0;
        for (int i = 0; i < kJobs; i++) once += taken[i] == 1;
        CHECK(once == kJobs);                                      // 모든 작업이 정확히 한 번
        CHECK(wrong.load() == 0u);                                 // 두 번째 이후 claim 도 생산자의 쓰기를 본다
    } else skip(3, "Part A");

    // ---------- Part B ----------
    {   // B1–B3 단위: 손으로 만든 흔적 (탐색과 무관). mp_relacq 의 "flag 를 본" 실행
        Event t1[4] = {{0, 0, Op::W, D, Mo::rlx, -1, 0, false}, {0, 1, Op::ST, F, Mo::rel, -1, 1, false},
                       {1, 0, Op::LD, F, Mo::acq, 1, 1, false}, {1, 1, Op::R, D, Mo::rlx, -1, 0, false}};
        std::uint64_t sw[kMaxEvents];
        int ra = -1, rb = -1;
        CHECK(build_sw(t1, 4, Rules{}, sw) == 1 && sw[1] == (1ull << 2));   // ST rel → LD acq
        CHECK(!find_race(t1, 4, Rules{}, &ra, &rb));
        t1[2].mo = Mo::rlx;                                                  // relaxed 로 바꾸면
        CHECK(build_sw(t1, 4, Rules{}, sw) == 0 && find_race(t1, 4, Rules{}, &ra, &rb) && ra == 0 && rb == 3);
        // release sequence: ST rel(0) → rmw(1, 다른 스레드) → ST rlx(2, head 와 같은 스레드)
        Event t2[3] = {{0, 0, Op::ST, CNT, Mo::rel, -1, 2, false}, {1, 0, Op::RMW, CNT, Mo::rlx, 0, 1, true},
                       {0, 1, Op::ST, CNT, Mo::rlx, -1, 5, false}};
        Rules c11; c11.cxx11_rs = true;
        Rules normm; normm.rmw_rs = false;
        CHECK(in_release_seq(t2, 0, 1, Rules{}) && !in_release_seq(t2, 0, 2, Rules{}));   // C++20: RMW 만 잇는다
        CHECK(in_release_seq(t2, 0, 2, c11));                                             // C++11: 같은 스레드 store 도
        CHECK(!in_release_seq(t2, 0, 1, normm) && in_release_seq(t2, 0, 0, Rules{}));     // head 자신은 항상 포함
    }
    int fb = FAILS();
    {
        Rules cxx20, nofence, normw, cxx11;
        nofence.fences = false; normw.rmw_rs = false; cxx11.cxx11_rs = true;
        std::printf("Part B explorer:\n");
        CHECK(expect(prog_mp_relaxed, cxx20, 3, 1));
        CHECK(expect(prog_mp_relacq, cxx20, 3, 0));
        CHECK(expect(prog_fence_fence, cxx20, 4, 0));
        CHECK(expect(prog_fence_fence, nofence, 4, 1));      // fence 를 모르는 도구(TSan)는 레이스로 본다
        CHECK(expect(prog_fence_op, cxx20, 4, 0));
        CHECK(expect(prog_op_fence, cxx20, 3, 0));
        CHECK(expect(prog_fence_one_side, cxx20, 4, 1));
        CHECK(expect(prog_rs_rmw, cxx20, 16, 0));
        CHECK(expect(prog_rs_rmw, normw, 16, 6));          // release sequence 규칙이 없으면 두 번째 소비자가 레이스
        CHECK(expect(prog_rs_ldst, cxx20, 1, 1));
        CHECK(expect(prog_rs_relay_rmw, cxx20, 1, 0));
        CHECK(expect(prog_rs_cxx11, cxx20, 1, 1));
        CHECK(expect(prog_rs_cxx11, cxx11, 1, 0));
        CHECK(expect(prog_chain3, cxx20, 1, 0));
        CHECK(expect(prog_chain3_broken, cxx20, 1, 1));
        CHECK(expect(prog_sb_guarded, cxx20, 10, 0));       // SC 만 보는 탐색기의 거짓 음성 (실제론 레이스)
    }
    (void)fb;

    // ---------- Part C ----------
    int fc = FAILS();
    {
        TxRing *r = new TxRing{};
        std::uint8_t p1[5] = {1, 2, 3, 4, 5}, p2[3] = {9, 9, 9};
        std::uint32_t st = 0;
        CHECK(tx_reap(*r, &st) == -1);                              // 아무것도 없음
        CHECK(tx_submit(*r, p1, 5, 10) && tx_submit(*r, p2, 3, 11));
        CHECK(r->d[0].ctrl == (OWN | 10u) && r->d[0].len == 5u);
        CHECK(tx_reap(*r, &st) == -1);                              // OWN 은 아직 DMA 것
        CHECK(dma_poll(*r) == 2u && dma_poll(*r) == 0u);            // 한 번에 2개 (fence 1번)
        CHECK(tx_reap(*r, &st) == 10 && st == pkt_sum(p1, 5));
        CHECK(tx_reap(*r, &st) == 11 && st == pkt_sum(p2, 3) && r->d[0].ctrl == 0u);
        int ok = 0;
        for (unsigned i = 0; i < kRing + 1; i++) ok += tx_submit(*r, p1, 5, static_cast<std::uint16_t>(i));
        CHECK(ok == (int)kRing);                                    // 9번째는 실패 (링 가득)
        delete r;
    }
    if (FAILS() == fc) {
        constexpr std::uint32_t NPKT = 100000;
        std::atomic<bool> stop{false};
        std::thread dma([&] {
            while (!stop.load(memory_order_acquire))
                if (dma_poll(g_ring) == 0u) std::this_thread::yield();
        });
        std::uint32_t sent = 0, got = 0, bad = 0;
        std::uint8_t pkt[kPkt];
        while (got < NPKT) {
            if (sent < NPKT) {
                std::uint16_t len = static_cast<std::uint16_t>(1u + sent % kPkt);
                for (unsigned k = 0; k < len; k++) pkt[k] = static_cast<std::uint8_t>(sent * 31u + k);
                if (tx_submit(g_ring, pkt, len, static_cast<std::uint16_t>(sent))) sent++;
            }
            std::uint32_t st;
            int seq = tx_reap(g_ring, &st);
            if (seq < 0) { std::this_thread::yield(); continue; }
            std::uint16_t len = static_cast<std::uint16_t>(1u + got % kPkt);
            for (unsigned k = 0; k < len; k++) pkt[k] = static_cast<std::uint8_t>(got * 31u + k);
            if (seq != static_cast<int>(got & 0xFFFFu) || st != pkt_sum(pkt, len)) bad++;
            got++;
        }
        stop.store(true, memory_order_release);
        dma.join();
        std::printf("Part C stress: packets=%u max_batch=%u\n", got, g_ring.max_batch);
        CHECK(got == NPKT && sent == NPKT);                         // 전부 회수
        CHECK(bad == 0u);                                           // 순서대로, status(체크섬) 일치
    } else skip(2, "Part C");

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
