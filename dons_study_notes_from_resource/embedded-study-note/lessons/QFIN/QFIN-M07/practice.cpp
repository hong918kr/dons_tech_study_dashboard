// QFIN-M07 추가 연습 — 한 파일에서 전부 검증
//   P1 평균 체결가 (정수)   P2 주문 세션 + 드롭카피 병합   P3 리스크 게이트
//   P4 수정 요청 합치기     P5 체결 취소(bust)와 묘비
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 practice.cpp -o pr && ./pr
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using ExecId = std::uint64_t;
using Qty = std::int64_t;
using Px = std::int64_t;

static int g_pass = 0, g_fail = 0;
#define CHECK(expr) do { if (expr) { ++g_pass; std::printf("PASS  %s\n", #expr); } \
                         else { ++g_fail; std::printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } } while (0)

// ---- P1 + P5: 체결 장부. 합과 집합만 쓰므로 도착 순서와 중복에 무관하다 ----
class Ledger {
public:
    void on_trade(ExecId id, Qty q, Px px) {
        if (!seen_.insert(id).second) return;
        if (busted_.count(id)) return;            // 묘비가 먼저 왔다: 없던 체결로 친다
        fills_[id] = Fill{q, px};
        cum_ += q; notional_ += q * px;
    }
    void on_bust(ExecId ref) {
        if (!busted_.insert(ref).second) return;
        auto it = fills_.find(ref);
        if (it == fills_.end()) return;           // 원 체결이 아직 없다: 묘비만 남긴다
        cum_ -= it->second.q; notional_ -= it->second.q * it->second.px;
        fills_.erase(it);
    }
    Qty cum() const { return cum_; }
    Qty notional() const { return notional_; }
    Px avg_px_x100() const {                      // 정수 나눗셈, 소수 둘째 자리까지 (버림)
        return cum_ == 0 ? 0 : notional_ * 100 / cum_;
    }

private:
    struct Fill { Qty q; Px px; };
    std::unordered_map<ExecId, Fill> fills_;
    std::unordered_set<ExecId> seen_, busted_;
    Qty cum_ = 0, notional_ = 0;
};

struct Ev { bool bust; ExecId id; Qty q; Px px; };
static void apply(Ledger &l, const Ev &e) { if (e.bust) l.on_bust(e.id); else l.on_trade(e.id, e.q, e.px); }

static void p1_avg_px() {
    std::printf("--- P1 평균 체결가\n");
    const std::vector<Ev> ev = {{false, 1, 30, 101}, {false, 2, 20, 103}, {false, 3, 50, 100}};
    std::vector<std::size_t> p(ev.size());
    std::iota(p.begin(), p.end(), 0u);
    int bad = 0, n = 0;
    do {
        Ledger l;
        for (std::size_t i : p) apply(l, ev[i]);
        ++n;
        if (l.cum() != 100 || l.notional() != 10090 || l.avg_px_x100() != 10090) ++bad;
    } while (std::next_permutation(p.begin(), p.end()));
    CHECK(n == 6 && bad == 0);
    Ledger z;
    CHECK(z.avg_px_x100() == 0);
}

// ---- P2: 두 세션 병합. 각 세션 안의 순서는 유지, 세션 사이 끼어들기는 전수 ----
static void interleave(const std::vector<Ev> &a, const std::vector<Ev> &b, std::size_t i, std::size_t j, std::vector<Ev> &cur,
                       const std::function<void(const std::vector<Ev> &)> &visit) {
    if (i == a.size() && j == b.size()) { visit(cur); return; }
    if (i < a.size()) { cur.push_back(a[i]); interleave(a, b, i + 1, j, cur, visit); cur.pop_back(); }
    if (j < b.size()) { cur.push_back(b[j]); interleave(a, b, i, j + 1, cur, visit); cur.pop_back(); }
}

static void p2_two_feeds() {
    std::printf("--- P2 주문 세션 + 드롭카피\n");
    const std::vector<Ev> oe = {{false, 1, 30, 100}, {false, 2, 20, 100}, {false, 3, 10, 100}, {false, 4, 40, 100}};
    const std::vector<Ev> dc = {{false, 1, 30, 100}, {false, 2, 20, 100}, {false, 3, 10, 100}, {false, 4, 40, 100}};
    int n = 0, bad = 0;
    std::vector<Ev> cur;
    interleave(oe, dc, 0, 0, cur, [&](const std::vector<Ev> &s) {
        Ledger l; for (const Ev &e : s) apply(l, e);
        ++n; if (l.cum() != 100) ++bad;
    });
    CHECK(n == 70 && bad == 0);          // C(8,4) = 70
    const std::vector<Ev> dc_gap = {{false, 2, 20, 100}, {false, 4, 40, 100}};   // 드롭카피가 일부를 놓쳐도 합집합이면 된다
    n = 0; bad = 0;
    interleave(oe, dc_gap, 0, 0, cur, [&](const std::vector<Ev> &s) {
        Ledger l; for (const Ev &e : s) apply(l, e);
        ++n; if (l.cum() != 100) ++bad;
    });
    CHECK(n == 15 && bad == 0);          // C(6,2) = 15
}

// ---- P3: 리스크 게이트. 확인되지 않은 취소는 한도를 풀지 못한다 ----
struct OrdRisk {
    Qty order_qty = 0, cum = 0, pending_replace_qty = 0;
    bool pending_replace = false, pending_cancel = false, dead = false;
    Qty worst() const {
        if (dead) return cum;
        Qty q = order_qty;
        if (pending_replace) q = std::max(q, pending_replace_qty);
        (void)pending_cancel;                 // 취소 진행 중이어도 줄이지 않는다
        return std::max(q, cum);
    }
};
static bool allow_buy(const std::vector<OrdRisk> &book, Qty new_qty, Qty limit) {
    Qty sum = 0;
    for (const OrdRisk &o : book) sum += o.worst();
    return sum + new_qty <= limit;
}
static void p3_risk_gate() {
    std::printf("--- P3 리스크 게이트\n");
    std::vector<OrdRisk> b(1);
    b[0].order_qty = 100;
    CHECK(allow_buy(b, 50, 150));
    CHECK(!allow_buy(b, 51, 150));
    b[0].pending_cancel = true;
    CHECK(!allow_buy(b, 51, 150));        // 취소를 보냈을 뿐 아직 살아 있다
    b[0].pending_cancel = false; b[0].dead = true; b[0].cum = 40;
    CHECK(allow_buy(b, 110, 150));        // 취소 확인: 체결분 40 만 남는다
    CHECK(!allow_buy(b, 111, 150));
    OrdRisk r; r.order_qty = 100; r.pending_replace = true; r.pending_replace_qty = 140;
    CHECK(r.worst() == 140);              // 올리는 replace 는 확정 전에도 큰 쪽
    r.pending_replace_qty = 60;
    CHECK(r.worst() == 100);              // 내리는 replace 도 확정 전에는 큰 쪽
}

// ---- P4: 진행 중 수정 요청이 있으면 최신 희망값 하나만 기억한다 ----
class Coalescer {
public:
    std::optional<Qty> want(Qty q) {
        if (inflight_) { held_ = q; return std::nullopt; }
        if (q == confirmed_) return std::nullopt;   // 이미 그 값이다
        inflight_ = q;
        return q;
    }
    std::optional<Qty> resolve(bool accepted) {
        if (!inflight_) return std::nullopt;
        if (accepted) confirmed_ = *inflight_;
        inflight_.reset();
        if (!held_) return std::nullopt;
        const Qty q = *held_;
        held_.reset();
        return want(q);
    }
    Qty confirmed() const { return confirmed_; }

private:
    Qty confirmed_ = 100;
    std::optional<Qty> inflight_, held_;
};
static void p4_coalesce() {
    std::printf("--- P4 수정 요청 합치기\n");
    Coalescer c;
    CHECK(c.want(110) == std::optional<Qty>(110));
    CHECK(!c.want(120));
    CHECK(!c.want(130));                               // 120 은 선로에 나가지 않는다
    CHECK(c.resolve(true) == std::optional<Qty>(130));
    CHECK(c.confirmed() == 110);
    CHECK(!c.resolve(true));
    CHECK(c.confirmed() == 130);
    CHECK(!c.want(130));                               // 같은 값이면 보내지 않는다
    CHECK(c.want(90) == std::optional<Qty>(90));
    CHECK(!c.want(130));
    CHECK(!c.resolve(false));                          // 90 거절. held 130 == confirmed 130 이라 보낼 것 없음
    CHECK(c.confirmed() == 130);
}

static void p5_bust() {
    std::printf("--- P5 체결 취소와 묘비\n");
    const std::vector<Ev> ev = {{false, 1, 30, 100}, {false, 2, 20, 100}, {true, 1, 0, 0}, {false, 1, 30, 100}, {true, 1, 0, 0}};
    std::vector<std::size_t> p(ev.size());
    std::iota(p.begin(), p.end(), 0u);
    int bad = 0, n = 0;
    do {
        Ledger l;
        for (std::size_t i : p) apply(l, ev[i]);
        ++n;
        if (l.cum() != 20 || l.notional() != 2000) ++bad;
    } while (std::next_permutation(p.begin(), p.end()));
    CHECK(n == 120 && bad == 0);          // bust 와 원 체결이 어느 순서로 와도, 중복돼도 20 만 남는다
}

int main() {
    p1_avg_px();
    p2_two_feeds();
    p3_risk_gate();
    p4_coalesce();
    p5_bust();
    std::printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
