// MCPP-M13 추가 연습 — 한 번에 검증
//   연습 1  TSan 흉내 규칙: 탐색기가 demo_tsan.cpp 의 실측 TSan 판정 10개를 그대로 예측하는가
//   연습 2  참조 카운트(fetch_sub release + 마지막에만 acquire fence)를 탐색기로 판정: 맞는 판 1 + 틀린 판 2
//   연습 3  같은 참조 카운트를 C 구조체의 plain 필드에 std::atomic_ref 로 — 진짜 스레드 4개
//   연습 4  탐색 공간: 가드·AWAIT 없는 3×3 프로그램의 실행 수 = 9!/(3!3!3!) = 1680
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread practice.cpp -o pr && ./pr
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#include <sanitizer/tsan_interface.h>
#define MCPP_TSAN 1
#endif
#endif
static inline void tsan_acquire(const void *p) {
#ifdef MCPP_TSAN
    __tsan_acquire(const_cast<void *>(p));
#else
    (void)p;
#endif
}

using std::memory_order_relaxed, std::memory_order_acquire, std::memory_order_release;

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
    bool tsan_like = false; // 연습 1: TSan 흉내 — relaxed store·relaxed RMW 는 sequence 를 끊지 않고, release 순수 store 만 끊는다
};
struct Verdict {
    long execs = 0, racy = 0, blocked = 0;
    char first[200] = "";   // 첫 레이스 쌍
    char trace[400] = "";   // 그 실행의 흔적
};

static bool is_write(const Event &e) { return e.op == Op::ST || e.op == Op::RMW; }
static bool is_read(const Event &e) { return e.op == Op::LD || e.op == Op::AWAIT || e.op == Op::RMW; }
static bool is_na(const Event &e) { return e.op == Op::W || e.op == Op::R; }
static bool is_rel(Mo m) { return m == Mo::rel || m == Mo::acq_rel || m == Mo::sc; }
static bool is_acq(Mo m) { return m == Mo::acq || m == Mo::acq_rel || m == Mo::sc; }

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
        bool continues = (r.rmw_rs && e.rmw) || (r.cxx11_rs && e.tid == A.tid) ||
                         (r.tsan_like && (e.rmw || !is_rel(e.mo)));      // 연습 1
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
static void on_leaf(Explorer &x) {
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


// ---- 연습 2: 참조 카운트. 각 스레드: read buf; r0 = ref.fetch_sub(1, mo); if (r0 == 1) { fence(acquire); write buf (재사용) }
static const Program prog_ref_ok{"ref_ok", 2, {{R_(BUF), RMW(CNT, -1, Mo::rel, 0), if_eq(FENCE(Mo::acq), 0, 1), if_eq(W_(BUF), 0, 1)},
                                             {R_(BUF), RMW(CNT, -1, Mo::rel, 0), if_eq(FENCE(Mo::acq), 0, 1), if_eq(W_(BUF), 0, 1)}},
                                  {0, 0, 2, 0, 0}};
static const Program prog_ref_nofence{"ref_nofence", 2, {{R_(BUF), RMW(CNT, -1, Mo::rel, 0), if_eq(W_(BUF), 0, 1)},
                                                       {R_(BUF), RMW(CNT, -1, Mo::rel, 0), if_eq(W_(BUF), 0, 1)}},
                                      {0, 0, 2, 0, 0}};
static const Program prog_ref_relaxed{"ref_relaxed", 2, {{R_(BUF), RMW(CNT, -1, Mo::rlx, 0), if_eq(FENCE(Mo::acq), 0, 1), if_eq(W_(BUF), 0, 1)},
                                                       {R_(BUF), RMW(CNT, -1, Mo::rlx, 0), if_eq(FENCE(Mo::acq), 0, 1), if_eq(W_(BUF), 0, 1)}},
                                      {0, 0, 2, 0, 0}};
// ---- 연습 4: 3 스레드 × 3 스텝, 가드·AWAIT 없음
static const Program prog_reads{"reads_3x3", 3, {{R_(D), R_(D), R_(D)}, {R_(D), R_(D), R_(D)}, {R_(D), R_(D), R_(D)}}, {}};
static const Program prog_one_write{"one_write", 3, {{W_(D), R_(BUF), R_(BUF)}, {R_(D), R_(BUF), R_(BUF)}, {R_(BUF), R_(BUF), R_(BUF)}}, {}};

// ---- 연습 3: C 와 공유하는 프레임 헤더 (atomic 타입 없음). refcnt 는 공유 동안 atomic_ref 로만.
struct frame_hdr { std::uint32_t refcnt; std::uint32_t seq; std::uint8_t payload[56]; };
static_assert(sizeof(frame_hdr) == 64 && std::is_standard_layout_v<frame_hdr>);
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);
static void frame_get(frame_hdr &f) { std::atomic_ref<std::uint32_t>(f.refcnt).fetch_add(1, memory_order_relaxed); }
// 마지막 참조였으면 true. 그때 호출자는 프레임을 재사용(쓰기)해도 된다.
static bool frame_put(frame_hdr &f) {
    if (std::atomic_ref<std::uint32_t>(f.refcnt).fetch_sub(1, memory_order_release) != 1u) return false;
    std::atomic_thread_fence(memory_order_acquire);    // 앞선 모든 put(release, RMW 로 이어진 sequence)과 동기화
    tsan_acquire(&f.refcnt);                           // TSan 은 fence 를 모른다 (TSan 빌드 전용)
    return true;
}
static std::uint8_t frame_sum(const frame_hdr &f) {
    std::uint8_t s = 0;
    for (std::uint8_t b : f.payload) s = static_cast<std::uint8_t>(s + b);
    return s;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static bool expect(const Program &p, Rules r, long execs, long racy) {
    Verdict v = explore(p, r);
    std::printf("  %-15s : execs=%ld racy=%ld blocked=%ld%s%s\n", p.name, v.execs, v.racy, v.blocked,
                v.racy ? "  first: " : "", v.first);
    return v.execs == execs && v.racy == racy && v.blocked == 0;
}
static frame_hdr g_frame;

int main() {
    // 연습 1: TSan 흉내 = fence 무시 + relaxed store 가 release sequence 를 안 끊음. 실측(demo_tsan.cpp) 판정과 비교
    {
        Rules tsan; tsan.fences = false; tsan.tsan_like = true;
        struct Case { const Program *p; bool tsan_reported; } cases[] = {
            {&prog_mp_relaxed, true}, {&prog_mp_relacq, false}, {&prog_fence_fence, true}, {&prog_fence_op, true},
            {&prog_op_fence, true}, {&prog_rs_rmw, false}, {&prog_rs_ldst, false}, {&prog_rs_cxx11, false},
            {&prog_chain3, false}, {&prog_sb_guarded, false}};
        int agree_tsan = 0, agree_std = 0;
        std::printf("연습 1 (TSan 흉내 vs C++20):\n");
        for (const Case &c : cases) {
            bool t = explore(*c.p, tsan).racy > 0, s = explore(*c.p, Rules{}).racy > 0;
            std::printf("  %-12s TSan실측=%d TSan흉내=%d C++20=%d\n", c.p->name, c.tsan_reported, t, s);
            agree_tsan += t == c.tsan_reported;
            agree_std += s == c.tsan_reported;
        }
        CHECK(agree_tsan == 10);    // 흉내 모델은 10/10 일치
        CHECK(agree_std == 5);      // C++20 규칙과 TSan 이 다른 곳 5개: fence 3개(TSan 오탐) + rs_ldst/rs_cxx11(TSan 놓침)
    }
    // 연습 2
    std::printf("연습 2 (참조 카운트):\n");
    CHECK(expect(prog_ref_ok, Rules{}, 6, 0));
    CHECK(expect(prog_ref_nofence, Rules{}, 6, 6));
    CHECK(expect(prog_ref_relaxed, Rules{}, 6, 6));
    // 연습 4
    std::printf("연습 4 (탐색 공간):\n");
    CHECK(expect(prog_reads, Rules{}, 1680, 0));
    CHECK(expect(prog_one_write, Rules{}, 1680, 1680));
    // 연습 3: 4 스레드가 각자 참조 하나씩 들고 payload 를 읽은 뒤 put. 마지막 put 만 프레임을 재사용(덮어쓰기)
    {
        constexpr int NT = 4, ROUNDS = 20000;
        std::atomic<int> round{0}, done{0}, lasts{0}, bad{0};
        std::vector<std::thread> th;
        for (int t = 0; t < NT; t++)
            th.emplace_back([&] {
                for (int r = 1; r <= ROUNDS; r++) {
                    while (round.load(memory_order_acquire) < r) std::this_thread::yield();
                    if (frame_sum(g_frame) != static_cast<std::uint8_t>(g_frame.seq)) bad.fetch_add(1, memory_order_relaxed);
                    if (frame_put(g_frame)) {
                        lasts.fetch_add(1, memory_order_relaxed);
                        std::memset(g_frame.payload, 0xEE, sizeof g_frame.payload);   // 재사용: 다른 스레드의 읽기와 레이스 없어야 함
                    }
                    done.fetch_add(1, memory_order_release);
                }
            });
        for (int r = 1; r <= ROUNDS; r++) {
            std::memset(g_frame.payload, 0, sizeof g_frame.payload);
            g_frame.payload[0] = static_cast<std::uint8_t>(r);
            g_frame.seq = static_cast<std::uint32_t>(r);
            std::atomic_ref<std::uint32_t>(g_frame.refcnt).store(0, memory_order_relaxed);
            for (int k = 0; k < NT; k++) frame_get(g_frame);
            round.store(r, memory_order_release);
            while (done.load(memory_order_acquire) < NT * r) std::this_thread::yield();
        }
        for (auto &t : th) t.join();
        std::printf("연습 3: rounds=%d lasts=%d bad=%d\n", ROUNDS, lasts.load(), bad.load());
        CHECK(lasts.load() == ROUNDS);     // 라운드마다 정확히 한 스레드만 "마지막"
        CHECK(bad.load() == 0);            // 재사용 쓰기가 다른 스레드의 읽기를 덮지 않음
        CHECK(std::atomic_ref<std::uint32_t>(g_frame.refcnt).load() == 0u);
    }
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
