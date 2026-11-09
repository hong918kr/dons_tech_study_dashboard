// QFIN-M07 demo: 버그를 하나씩 심은 OMS 를 같은 seed sweep 에 돌린다 (mutation test).
//   이 sweep 이 무엇을 잡고 무엇을 못 잡는지, FIFO 선로와 뒤섞인 선로에서 어떻게 달라지는지 보여 준다.
//   mode: (없음) 표 출력 | trace <bug번호>  짧은 실패 trace 찾아 출력
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 demo_sweep.cpp -o demo_sweep && ./demo_sweep
#define QFIN_NO_MAIN
#include "solution.cpp"
#include <cstdlib>
#include <cstring>

enum Bug : unsigned {
    NONE = 0,
    FORGET_OLD_ID = 1 << 0,       // replace 를 보낸 뒤 옛 ClOrdID 로 온 ER 은 모르는 주문으로 버린다
    NO_DEDUP = 1 << 1,            // ExecID 를 안 본다
    CUM_LAST_WRITE = 1 << 2,      // cum = 마지막에 도착한 ER 의 CumQty, 포지션은 그 차이로
    REVIVE_ON_NEW = 1 << 3,       // 늦은 New / PendingX 가 종료 상태를 지운다
    REJECT_REVIVES = 1 << 4,      // Cancel Reject 를 받으면 "주문은 살아 있다"
    DROP_AFTER_TERM = 1 << 5,     // 종료된 주문의 Trade 를 버린다
    PENDING_NOT_MONO = 1 << 6,    // 늦은 PendingX 가 끝난 요청을 다시 진행 중으로
    QTY_FROM_REQUEST = 1 << 7,    // Replaced 에서 er.order_qty 대신 요청한 수량을 쓴다
    LEAVES_LAST_ER = 1 << 8,      // LeavesQty = 마지막에 도착한 ER 의 값
    RISK_ON_REQUEST = 1 << 9,     // 리스크 상한을 요청 수량으로 바로 내린다 / 취소 요청 즉시 0 으로 본다
};

// Oms 의 사본. 각 분기가 위 Bug 하나에 대응한다.
class Mutant {
public:
    explicit Mutant(unsigned bugs) : bugs_(bugs) {}
    Request new_order(Side side, Qty qty, Px px) {
        const ClOrdId cl = next_cl_++;
        Order o; o.side = side; o.chain.push_back(Link{cl, 0, qty, 0}); orders_.push_back(std::move(o));
        index_[cl] = orders_.size() - 1;
        return Request{ReqKind::New, cl, 0, side, qty, px};
    }
    std::optional<Request> cancel(ClOrdId id) { return modify(id, 2, 0, 0); }
    std::optional<Request> replace(ClOrdId id, Qty q, Px p) { return modify(id, 1, q, p); }

    void on_exec(const ExecReport &er) {
        Order *o = find(er.cl);
        if (!o && er.orig) o = find(er.orig);
        if (!o || ((bugs_ & FORGET_OLD_ID) && forgotten_.count(er.cl))) { ++unknown_; return; }
        if (!(bugs_ & NO_DEDUP) && !seen_.insert(er.exec_id).second) return;
        o->acked = true;
        o->reported_cum = std::max(o->reported_cum, er.cum_qty);
        o->last_leaves = er.leaves_qty;
        Link *l = link_of(*o, er.cl);
        const Qty sign = (o->side == Side::Buy) ? 1 : -1;
        switch (er.type) {
        case ExecType::New:
            if (bugs_ & REVIVE_ON_NEW) o->term = 0;
            break;
        case ExecType::Trade:
            if ((bugs_ & DROP_AFTER_TERM) && o->term) break;
            if (bugs_ & CUM_LAST_WRITE) { position_ += sign * (er.cum_qty - o->cum); o->cum = er.cum_qty; }
            else { o->cum += er.last_qty; position_ += sign * er.last_qty; }
            break;
        case ExecType::PendingCancel:
        case ExecType::PendingReplace:
            if (bugs_ & REVIVE_ON_NEW) o->term = 0;
            if (l && (l->st == 0 || (bugs_ & PENDING_NOT_MONO))) l->st = 1;
            break;
        case ExecType::Replaced:
            if (l) {
                l->st = 2;
                if (!(bugs_ & QTY_FROM_REQUEST)) l->qty = er.order_qty;
                o->eff = std::max(o->eff, static_cast<std::size_t>(l - o->chain.data()));
            }
            break;
        case ExecType::Canceled:
            if (l && l->kind == 2) l->st = 2;
            if (!o->term) o->term = 1;
            break;
        case ExecType::Rejected:
            if (!o->term) o->term = 2;
            break;
        }
    }
    void on_cancel_reject(const CancelReject &cr) {
        Order *o = find(cr.cl);
        if (!o && cr.orig) o = find(cr.orig);
        if (!o) { ++unknown_; return; }
        Link *l = link_of(*o, cr.cl);
        if (l && l->st != 2) l->st = 3;
        if (bugs_ & REJECT_REVIVES) o->term = 0;
    }
    OrderView view(ClOrdId id) const {
        OrderView v;
        const Order *o = find(id);
        if (!o) return v;
        v.found = true;
        v.order_qty = o->chain[o->eff].qty;
        v.cum = o->cum;
        v.fill_gap = std::max<Qty>(o->reported_cum - o->cum, 0);
        for (const Link &l : o->chain) if (l.kind && l.st < 2) v.pending = (l.kind == 2) ? Pending::Cancel : Pending::Replace;
        if (o->term == 1) v.state = St::Canceled;
        else if (o->term == 2) v.state = St::Rejected;
        else if (o->cum >= v.order_qty) v.state = St::Filled;
        else if (o->cum > 0) v.state = St::PartiallyFilled;
        else v.state = o->acked ? St::New : St::PendingNew;
        const bool open = (v.state == St::New || v.state == St::PartiallyFilled || v.state == St::PendingNew);
        v.leaves = open ? std::max<Qty>(v.order_qty - o->cum, 0) : 0;
        if ((bugs_ & LEAVES_LAST_ER) && o->acked) v.leaves = o->last_leaves;
        return v;
    }
    Qty worst_case_fill(ClOrdId id) const {
        const Order *o = find(id);
        if (!o) return 0;
        if (o->term) return std::max(o->cum, o->reported_cum);
        Qty q = o->chain[o->eff].qty;
        for (const Link &l : o->chain) {
            if (l.st >= 2) continue;
            if (bugs_ & RISK_ON_REQUEST) { if (l.kind == 1) q = l.qty; if (l.kind == 2) q = o->cum; }
            else if (l.kind == 1) q = std::max(q, l.qty);
        }
        if (bugs_ & RISK_ON_REQUEST) return q;
        return std::max({q, o->cum, o->reported_cum});
    }
    Qty position() const { return position_; }

private:
    struct Link { ClOrdId cl; int kind; Qty qty; int st; };   // kind 0 주문 1 replace 2 cancel; st 0 Sent 1 Ack 2 Done 3 Rej
    struct Order { Side side = Side::Buy; std::vector<Link> chain; std::size_t eff = 0; Qty cum = 0, reported_cum = 0, last_leaves = 0; bool acked = false; int term = 0; };
    Order *find(ClOrdId id) { auto it = index_.find(id); return it == index_.end() ? nullptr : &orders_[it->second]; }
    const Order *find(ClOrdId id) const { auto it = index_.find(id); return it == index_.end() ? nullptr : &orders_[it->second]; }
    static Link *link_of(Order &o, ClOrdId cl) { for (Link &l : o.chain) if (l.cl == cl) return &l; return nullptr; }
    std::optional<Request> modify(ClOrdId id, int kind, Qty q, Px p) {
        Order *o = find(id);
        if (!o || o->term || o->cum >= o->chain[o->eff].qty) return std::nullopt;
        for (const Link &l : o->chain) if (l.kind && l.st < 2) return std::nullopt;
        const ClOrdId cl = next_cl_++;
        const ClOrdId orig = o->chain[o->eff].cl;
        o->chain.push_back(Link{cl, kind, q, 0});
        const std::size_t idx = index_.at(id);
        index_[cl] = idx;
        if (kind == 1) forgotten_.insert(orig);
        return Request{kind == 2 ? ReqKind::Cancel : ReqKind::Replace, cl, orig, o->side, q, p};
    }
    unsigned bugs_;
    std::vector<Order> orders_;
    std::unordered_map<ClOrdId, std::size_t> index_;
    std::unordered_set<ExecId> seen_;
    std::unordered_set<ClOrdId> forgotten_;
    ClOrdId next_cl_ = 1;
    Qty position_ = 0;
    std::uint64_t unknown_ = 0;
};

static const struct { unsigned bug; const char *name; } kBugs[] = {
    {NONE, "none (Mutant = 정답과 같은 논리)"},
    {FORGET_OLD_ID, "FORGET_OLD_ID"}, {NO_DEDUP, "NO_DEDUP"}, {CUM_LAST_WRITE, "CUM_LAST_WRITE"},
    {REVIVE_ON_NEW, "REVIVE_ON_NEW"}, {REJECT_REVIVES, "REJECT_REVIVES"}, {DROP_AFTER_TERM, "DROP_AFTER_TERM"},
    {PENDING_NOT_MONO, "PENDING_NOT_MONO"}, {QTY_FROM_REQUEST, "QTY_FROM_REQUEST"}, {LEAVES_LAST_ER, "LEAVES_LAST_ER"},
    {RISK_ON_REQUEST, "RISK_ON_REQUEST"},
};

static void print_table() {
    const int N = 2000;
    const SimCfg fifo{.window = 1, .dup_pct = 0, .steps = 80, .max_orders = 3};
    const SimCfg reorder{.window = 8, .dup_pct = 0, .steps = 80, .max_orders = 3};
    const SimCfg rough{.window = 8, .dup_pct = 20, .steps = 80, .max_orders = 3};
    std::printf("%-34s %9s %9s %9s   %s\n", "planted bug", "FIFO", "reorder", "reord+dup", "first violation (reord+dup)");
    for (const auto &b : kBugs) {
        const Mutant proto(b.bug);
        const SweepResult f = sweep(proto, 0, N, fifo), r = sweep(proto, 0, N, reorder), d = sweep(proto, 0, N, rough);
        std::printf("%-34s %9d %9d %9d   %s\n", b.name, f.failures, r.failures, d.failures,
                    d.failures ? ("seed " + std::to_string(d.first_seed) + ": " + d.first_msg).c_str() : "-");
    }
    const Oms sol;
    const SweepResult f = sweep(sol, 0, N, fifo), r = sweep(sol, 0, N, reorder), d = sweep(sol, 0, N, rough);
    std::printf("%-34s %9d %9d %9d   %s\n", "solution.cpp Oms", f.failures, r.failures, d.failures, "-");
    std::printf("(각 칸 = 실패한 seed 수 / %d)\n", N);
}

// 짧은 실패 trace: steps 를 1 부터 늘려 가며 처음 실패하는 seed 를 찾는다
static int print_short_trace(unsigned bit) {
    const unsigned bug = kBugs[bit].bug;
    for (int steps = 1; steps <= 80; ++steps) {
        const SimCfg cfg{.window = 8, .dup_pct = 20, .steps = steps, .max_orders = 1};
        for (std::uint64_t seed = 0; seed < 20000; ++seed) {
            Mutant m(bug);
            Coverage c;
            const SimResult s = run_seed(m, seed, cfg, c);
            if (s.violation.empty()) continue;
            std::vector<std::string> t;
            Mutant a(bug), b(bug);
            const SimResult s1 = run_seed(a, seed, cfg, c, &t);
            const SimResult s2 = run_seed(b, seed, cfg, c);
            std::printf("bug=%s steps=%d seed=%llu violation: %s\n", kBugs[bit].name, steps, static_cast<unsigned long long>(seed), s1.violation.c_str());
            std::printf("replay hash %016llx %s %016llx\n", static_cast<unsigned long long>(s1.hash), s1.hash == s2.hash ? "==" : "!=", static_cast<unsigned long long>(s2.hash));
            for (const std::string &l : t) std::printf("  %s\n", l.c_str());
            return 0;
        }
    }
    std::printf("no failure found\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 3 && std::strcmp(argv[1], "trace") == 0) return print_short_trace(static_cast<unsigned>(std::atoi(argv[2])));
    print_table();
    return 0;
}
