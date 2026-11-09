// QFIN-M07 OMS: ExecutionReport 처리와 Cancel/Replace 경쟁
//
// 이 프로그램에는 스레드가 하나뿐이다. 경쟁은 스레드 사이가 아니라 비동기 메시지 사이에서 일어난다:
// 클라이언트가 보낸 요청과 거래소가 보낸 체결이 선 위에서 서로 스쳐 지나간다.
//   Part A  ClOrdID 체인, 주문별 상태, CumQty / LeavesQty / 포지션
//   Part B  시나리오: too-late-to-cancel, replace 중 부분 체결, 순서 뒤바뀜, unsolicited cancel
//   Part C  검증기: 짧은 스크립트 전수 순열 + 모의 거래소 seed sweep + 매 스텝 불변식
//
// FIX 태그 번호와 의미는 작성자 기억에 의존한다. 실제 거래소는 Rules of Engagement 문서로 확인할 것.
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 solution.cpp -o so && ./so   (starter.cpp 도 같은 명령)
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

using ClOrdId = std::uint32_t;  // tag 11. 실제 FIX 는 문자열, 저지연 경로는 흔히 정수로 인코딩
using ExecId = std::uint64_t;   // tag 17
using Qty = std::int64_t;
using Px = std::int64_t;  // 틱 단위 정수 (QFIN-B01)

enum class Side : std::uint8_t { Buy, Sell };
enum class ExecType : char {  // tag 150
    New = '0', PendingCancel = '6', Canceled = '4', PendingReplace = 'E', Replaced = '5', Trade = 'F', Rejected = '8'
};
enum class RejectReason : std::uint8_t { TooLate, Unknown };  // tag 102 의 0, 1 에 해당
enum class ReqKind : std::uint8_t { New, Cancel, Replace };   // 35=D / F / G
enum class St : std::uint8_t { Unknown, PendingNew, New, PartiallyFilled, Filled, Canceled, Rejected };
enum class Pending : std::uint8_t { None, Cancel, Replace };

struct Request {
    ReqKind kind; ClOrdId cl; ClOrdId orig; Side side; Qty qty; Px px;
};
struct ExecReport {  // 35=8
    ExecType type; ClOrdId cl; ClOrdId orig; ExecId exec_id;
    Qty last_qty; Px last_px; Qty cum_qty; Qty leaves_qty; Qty order_qty;
};
struct CancelReject {  // 35=9. ExecID 가 없다
    ClOrdId cl; ClOrdId orig; bool was_replace; RejectReason reason;
};
using Msg = std::variant<ExecReport, CancelReject>;

struct OrderView {
    bool found = false;
    St state = St::Unknown;
    Pending pending = Pending::None;
    Qty order_qty = 0, cum = 0, leaves = 0;
    Qty fill_gap = 0;           // ER 가 알려 준 CumQty 중 아직 Trade 를 못 받은 양
    ClOrdId current_id = 0;     // 마지막으로 확정된 ClOrdID. 다음 요청의 OrigClOrdID 가 된다
};

// ======================= Part A/B — OMS =======================
class Oms {
public:
    Request new_order(Side side, Qty qty, Px px) {
        const ClOrdId cl = next_cl_++;
        Order o;
        o.side = side;
        o.chain.push_back(Link{cl, Kind::Order, qty, px, LinkSt::Sent});
        orders_.push_back(std::move(o));
        index_[cl] = orders_.size() - 1;
        return Request{ReqKind::New, cl, 0, side, qty, px};
    }

    std::optional<Request> cancel(ClOrdId any_id) {
        Order *o = find(any_id);
        if (!o || !can_modify(*o)) return std::nullopt;
        const ClOrdId cl = next_cl_++;
        const ClOrdId orig = o->chain[o->eff].cl;
        o->chain.push_back(Link{cl, Kind::Cancel, 0, 0, LinkSt::Sent});
        index_[cl] = index_.at(any_id);
        return Request{ReqKind::Cancel, cl, orig, o->side, 0, 0};
    }

    std::optional<Request> replace(ClOrdId any_id, Qty new_qty, Px new_px) {
        Order *o = find(any_id);
        if (!o || !can_modify(*o)) return std::nullopt;
        const ClOrdId cl = next_cl_++;
        const ClOrdId orig = o->chain[o->eff].cl;
        o->chain.push_back(Link{cl, Kind::Replace, new_qty, new_px, LinkSt::Sent});
        index_[cl] = index_.at(any_id);
        return Request{ReqKind::Replace, cl, orig, o->side, new_qty, new_px};
    }

    void on_exec(const ExecReport &er) {
        Order *o = find(er.cl);
        if (!o && er.orig != 0) o = find(er.orig);
        if (!o) { ++unknown_; return; }
        if (!seen_.insert(er.exec_id).second) { ++dups_; return; }
        o->acked = true;
        o->reported_cum = std::max(o->reported_cum, er.cum_qty);
        Link *l = link_of(*o, er.cl);
        switch (er.type) {
        case ExecType::New:
            break;
        case ExecType::Trade:
            o->cum += er.last_qty;
            position_ += (o->side == Side::Buy ? er.last_qty : -er.last_qty);
            break;
        case ExecType::PendingCancel:
        case ExecType::PendingReplace:
            if (l && l->st == LinkSt::Sent) l->st = LinkSt::Ack;
            break;
        case ExecType::Replaced:
            if (l) {
                l->st = LinkSt::Done;
                l->qty = er.order_qty;
                o->eff = std::max(o->eff, static_cast<std::size_t>(l - o->chain.data()));
            }
            break;
        case ExecType::Canceled:
            if (l && l->kind == Kind::Cancel) l->st = LinkSt::Done;
            if (o->term == Term::None) o->term = Term::Canceled;
            break;
        case ExecType::Rejected:
            if (o->term == Term::None) o->term = Term::Rejected;
            break;
        }
    }

    void on_cancel_reject(const CancelReject &cr) {
        Order *o = find(cr.cl);
        if (!o && cr.orig != 0) o = find(cr.orig);
        if (!o) { ++unknown_; return; }
        Link *l = link_of(*o, cr.cl);
        if (l && l->st != LinkSt::Done) l->st = LinkSt::Rej;
    }

    OrderView view(ClOrdId any_id) const {
        OrderView v;
        const Order *o = find(any_id);
        if (!o) return v;
        v.found = true;
        v.order_qty = o->chain[o->eff].qty;
        v.current_id = o->chain[o->eff].cl;
        v.cum = o->cum;
        v.fill_gap = std::max<Qty>(o->reported_cum - o->cum, 0);
        for (const Link &l : o->chain)
            if (l.kind != Kind::Order && in_flight(l))
                v.pending = (l.kind == Kind::Cancel) ? Pending::Cancel : Pending::Replace;
        if (o->term == Term::Canceled) v.state = St::Canceled;
        else if (o->term == Term::Rejected) v.state = St::Rejected;
        else if (o->cum >= v.order_qty) v.state = St::Filled;
        else if (o->cum > 0) v.state = St::PartiallyFilled;
        else v.state = o->acked ? St::New : St::PendingNew;
        const bool open = (v.state == St::New || v.state == St::PartiallyFilled || v.state == St::PendingNew);
        v.leaves = open ? std::max<Qty>(v.order_qty - o->cum, 0) : 0;
        return v;
    }

    // Part C: 이 주문이 앞으로 최대 얼마까지 체결될 수 있나 (리스크 계산용 상한)
    Qty worst_case_fill(ClOrdId any_id) const {
        const Order *o = find(any_id);
        if (!o) return 0;
        if (o->term != Term::None) return std::max(o->cum, o->reported_cum);
        Qty q = o->chain[o->eff].qty;
        for (const Link &l : o->chain)
            if (l.kind == Kind::Replace && in_flight(l)) q = std::max(q, l.qty);
        return std::max({q, o->cum, o->reported_cum});
    }

    Qty position() const { return position_; }
    std::uint64_t duplicates() const { return dups_; }
    std::uint64_t unknown_reports() const { return unknown_; }

private:
    enum class Kind : std::uint8_t { Order, Replace, Cancel };
    enum class LinkSt : std::uint8_t { Sent, Ack, Done, Rej };  // Sent → Ack → (Done | Rej) 앞으로만 간다
    enum class Term : std::uint8_t { None, Canceled, Rejected };
    struct Link { ClOrdId cl; Kind kind; Qty qty; Px px; LinkSt st; };
    struct Order {
        Side side = Side::Buy;
        std::vector<Link> chain;      // ClOrdID 체인. chain[0] = 최초 주문
        std::size_t eff = 0;          // 지금 유효한 수량을 정한 링크 (체인에서 가장 뒤의 확정 링크)
        Qty cum = 0;                  // 받은 Trade 의 합
        Qty reported_cum = 0;         // ER 들이 말한 CumQty 의 최댓값
        bool acked = false;
        Term term = Term::None;
    };

    static bool in_flight(const Link &l) { return l.st == LinkSt::Sent || l.st == LinkSt::Ack; }
    Order *find(ClOrdId id) { auto it = index_.find(id); return it == index_.end() ? nullptr : &orders_[it->second]; }
    const Order *find(ClOrdId id) const { auto it = index_.find(id); return it == index_.end() ? nullptr : &orders_[it->second]; }
    static Link *link_of(Order &o, ClOrdId cl) {
        for (Link &l : o.chain) if (l.cl == cl) return &l;
        return nullptr;
    }
    bool can_modify(const Order &o) const {
        if (o.term != Term::None || o.cum >= o.chain[o.eff].qty) return false;
        for (const Link &l : o.chain) if (l.kind != Kind::Order && in_flight(l)) return false;
        return true;
    }

    std::vector<Order> orders_;
    std::unordered_map<ClOrdId, std::size_t> index_;   // 체인의 모든 ID → 주문
    std::unordered_set<ExecId> seen_;
    ClOrdId next_cl_ = 1;
    Qty position_ = 0;
    std::uint64_t dups_ = 0, unknown_ = 0;
};

// ======================= Part C — 모의 거래소 + seed sweep (시작 코드에 포함) =======================
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s((seed + 1) * 0x9E3779B97F4A7C15ull) { if (s == 0) s = 1; }
    std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    std::uint32_t below(std::uint64_t n) { return static_cast<std::uint32_t>(next() % n); }
};

enum class VSt : std::uint8_t { Live, Filled, Canceled, Rejected };
struct VOrder { ClOrdId first; ClOrdId accepted; Side side; Qty qty; Qty cum; VSt st; int inflight; };
struct Wire { Msg msg; bool dup; };
struct Coverage { std::uint64_t too_late = 0, fill_during_modify = 0, unsolicited = 0, reordered = 0, dups = 0, clipped = 0; };

class Venue {  // 거래소의 진실. 요청은 받기(recv) → 처리(apply) 두 단계라서 그 사이에 체결이 끼어들 수 있다
public:
    std::vector<VOrder> orders;
    std::deque<Request> processing;
    std::deque<Wire> out;
    Coverage cov;

    const VOrder *find_first(ClOrdId first) const {
        for (const VOrder &o : orders) if (o.first == first) return &o;
        return nullptr;
    }
    void recv(const Request &r, Rng &rng) {
        if (r.kind == ReqKind::New) {
            const bool rej = rng.below(16) == 0;
            orders.push_back(VOrder{r.cl, r.cl, r.side, r.qty, 0, rej ? VSt::Rejected : VSt::Live, 0});
            ids_[r.cl] = orders.size() - 1;
            report(orders.back(), rej ? ExecType::Rejected : ExecType::New, r.cl, 0, 0);
            return;
        }
        VOrder *o = lookup(r.orig);
        if (!o || o->accepted != r.orig) { reject(r, RejectReason::Unknown); return; }
        if (o->st != VSt::Live) { ++cov.too_late; reject(r, RejectReason::TooLate); return; }
        report(*o, r.kind == ReqKind::Cancel ? ExecType::PendingCancel : ExecType::PendingReplace, r.cl, r.orig, 0);
        ++o->inflight;
        processing.push_back(r);
    }
    void apply_next() {
        if (processing.empty()) return;
        const Request r = processing.front();
        processing.pop_front();
        VOrder *o = lookup(r.orig);
        --o->inflight;
        if (o->st != VSt::Live) { ++cov.too_late; reject(r, RejectReason::TooLate); return; }
        if (r.kind == ReqKind::Cancel) {
            o->st = VSt::Canceled;
            report(*o, ExecType::Canceled, r.cl, r.orig, 0);
            return;
        }
        if (r.qty < o->cum) ++cov.clipped;
        o->qty = std::max(r.qty, o->cum);   // 이미 체결된 양 아래로는 못 줄인다
        o->accepted = r.cl;
        ids_[r.cl] = static_cast<std::size_t>(o - orders.data());
        if (o->qty == o->cum) o->st = VSt::Filled;
        report(*o, ExecType::Replaced, r.cl, r.orig, 0);
    }
    void fill_random(Rng &rng) {
        std::vector<VOrder *> live;
        for (VOrder &o : orders) if (o.st == VSt::Live) live.push_back(&o);
        if (live.empty()) return;
        VOrder &o = *live[rng.below(live.size())];
        const Qty leaves = o.qty - o.cum;
        const Qty q = (rng.below(4) == 0) ? leaves : 1 + rng.below(static_cast<std::uint64_t>(leaves));
        o.cum += q;
        if (o.inflight > 0) ++cov.fill_during_modify;
        if (o.cum == o.qty) o.st = VSt::Filled;
        report(o, ExecType::Trade, o.accepted, 0, q);
    }
    void unsolicited_cancel(Rng &rng) {
        std::vector<VOrder *> live;
        for (VOrder &o : orders) if (o.st == VSt::Live) live.push_back(&o);
        if (live.empty()) return;
        VOrder &o = *live[rng.below(live.size())];
        o.st = VSt::Canceled;
        ++cov.unsolicited;
        report(o, ExecType::Canceled, o.accepted, 0, 0);
    }

private:
    VOrder *lookup(ClOrdId id) { auto it = ids_.find(id); return it == ids_.end() ? nullptr : &orders[it->second]; }
    void report(const VOrder &o, ExecType t, ClOrdId cl, ClOrdId orig, Qty last) {
        const Qty leaves = (o.st == VSt::Live) ? o.qty - o.cum : 0;
        out.push_back(Wire{ExecReport{t, cl, orig, next_exec_++, last, 100, o.cum, leaves, o.qty}, false});
    }
    void reject(const Request &r, RejectReason why) {
        out.push_back(Wire{CancelReject{r.cl, r.orig, r.kind == ReqKind::Replace, why}, false});
    }
    std::unordered_map<ClOrdId, std::size_t> ids_;
    ExecId next_exec_ = 1;
};

[[maybe_unused]] static St truth_state(const VOrder &o) {
    switch (o.st) {
    case VSt::Live: return o.cum > 0 ? St::PartiallyFilled : St::New;
    case VSt::Filled: return St::Filled;
    case VSt::Canceled: return St::Canceled;
    case VSt::Rejected: return St::Rejected;
    }
    return St::Unknown;
}
[[maybe_unused]] static Qty potential_fill(const VOrder *o) {   // 거래소 기준, 이 주문이 도달할 수 있는 최대 체결량
    if (!o) return 0;
    return o->st == VSt::Live ? o->qty : o->cum;
}

// 매 스텝 안전 불변식. 위반이면 설명 문자열, 아니면 빈 문자열.
template <class O>
static std::string check_step(const O &oms, const Venue &v, const std::vector<ClOrdId> &client, std::vector<bool> &was_dead) {
    for (std::size_t i = 0; i < client.size(); ++i) {
        const OrderView w = oms.view(client[i]);
        const VOrder *t = v.find_first(client[i]);
        if (!w.found) return "order lost id=" + std::to_string(client[i]);
        if (w.cum > (t ? t->cum : 0)) return "cum beyond venue (double count) id=" + std::to_string(client[i]);
        if (w.leaves < 0) return "negative leaves id=" + std::to_string(client[i]);
        if (oms.worst_case_fill(client[i]) < potential_fill(t)) return "worst_case below truth id=" + std::to_string(client[i]);
        const bool dead = (w.state == St::Canceled || w.state == St::Rejected);
        if (was_dead[i] && !dead) return "dead order revived id=" + std::to_string(client[i]);
        if (dead) was_dead[i] = true;
    }
    return "";
}

// 메시지가 모두 도착한 뒤: OMS 와 거래소가 완전히 같아야 한다.
template <class O>
static std::string check_quiescent(const O &oms, const Venue &v, const std::vector<ClOrdId> &client) {
    Qty truth_pos = 0;
    for (ClOrdId id : client) {
        const OrderView w = oms.view(id);
        const VOrder *t = v.find_first(id);
        const std::string at = " id=" + std::to_string(id);
        if (!w.found || !t) return "missing order" + at;
        const Qty leaves = (t->st == VSt::Live) ? t->qty - t->cum : 0;
        if (w.cum != t->cum) return "cum mismatch" + at;
        if (w.order_qty != t->qty) return "order_qty mismatch" + at;
        if (w.leaves != leaves) return "leaves mismatch" + at;
        if (w.state != truth_state(*t)) return "state mismatch" + at;
        if (w.pending != Pending::None) return "pending stuck" + at;
        if (w.fill_gap != 0) return "fill gap" + at;
        truth_pos += (t->side == Side::Buy) ? t->cum : -t->cum;
    }
    if (oms.position() != truth_pos) return "position mismatch";
    return "";
}

struct SimCfg { std::uint32_t window = 1; std::uint32_t dup_pct = 0; int steps = 80; std::size_t max_orders = 3; };
struct SimResult { std::uint64_t hash = 0xcbf29ce484222325ull; int steps = 0; std::string violation; };

static void fnv(std::uint64_t &h, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) { h ^= (v >> (8 * i)) & 0xff; h *= 0x100000001b3ull; }
}
static const char *type_name(ExecType t) {
    switch (t) {
    case ExecType::New: return "New";
    case ExecType::PendingCancel: return "PendingCancel";
    case ExecType::Canceled: return "Canceled";
    case ExecType::PendingReplace: return "PendingReplace";
    case ExecType::Replaced: return "Replaced";
    case ExecType::Trade: return "Trade";
    case ExecType::Rejected: return "Rejected";
    }
    return "?";
}
static std::string describe(const Msg &m) {
    char b[128];
    if (const auto *e = std::get_if<ExecReport>(&m))
        std::snprintf(b, sizeof b, "ER %s cl=%u orig=%u exec=%llu last=%lld cum=%lld leaves=%lld qty=%lld", type_name(e->type), e->cl,
                      e->orig, static_cast<unsigned long long>(e->exec_id), static_cast<long long>(e->last_qty),
                      static_cast<long long>(e->cum_qty), static_cast<long long>(e->leaves_qty), static_cast<long long>(e->order_qty));
    else {
        const auto &c = std::get<CancelReject>(m);
        std::snprintf(b, sizeof b, "CxlRej cl=%u orig=%u %s", c.cl, c.orig, c.reason == RejectReason::TooLate ? "TooLate" : "Unknown");
    }
    return b;
}
template <class O> static void apply_msg(O &oms, const Msg &m) {
    if (const auto *e = std::get_if<ExecReport>(&m)) oms.on_exec(*e);
    else oms.on_cancel_reject(std::get<CancelReject>(m));
}

template <class O>
static SimResult run_seed(O &oms, std::uint64_t seed, const SimCfg &cfg, Coverage &cov, std::vector<std::string> *trace = nullptr) {
    Rng rng(seed);
    Venue v;
    std::deque<Request> to_venue;                  // 클라이언트 → 거래소는 FIFO (TCP 한 세션)
    std::vector<ClOrdId> client;
    std::vector<bool> was_dead;
    SimResult res;
    auto note = [&](const std::string &s) { if (trace) trace->push_back(s); };
    auto send = [&](const std::optional<Request> &r, const char *what) {
        if (!r) return;
        to_venue.push_back(*r);
        note(std::string("client send ") + what + " cl=" + std::to_string(r->cl) + " orig=" + std::to_string(r->orig) +
             " qty=" + std::to_string(r->qty));
    };
    auto deliver = [&]() {
        if (v.out.empty()) return;
        const std::uint32_t pos = rng.below(std::min<std::size_t>(cfg.window, v.out.size()));
        const Wire w = v.out[pos];
        v.out.erase(v.out.begin() + pos);
        if (pos != 0) ++v.cov.reordered;
        if (!w.dup && rng.below(100) < cfg.dup_pct) {
            v.out.insert(v.out.begin() + rng.below(v.out.size() + 1), Wire{w.msg, true});
            ++v.cov.dups;
        }
        note("deliver  " + describe(w.msg) + (pos ? "  [reordered]" : "") + (w.dup ? "  [dup]" : ""));
        fnv(res.hash, pos);
        for (char c : describe(w.msg)) fnv(res.hash, static_cast<unsigned char>(c));
        apply_msg(oms, w.msg);
    };
    auto check = [&]() {
        res.violation = check_step(oms, v, client, was_dead);
        return res.violation.empty();
    };
    bool ok = true;
    for (int step = 0; ok && step < cfg.steps; ++step) {
        ++res.steps;
        switch (rng.below(12)) {
        case 0:
            if (client.size() < cfg.max_orders) {
                const Request r = oms.new_order(rng.below(2) ? Side::Buy : Side::Sell, 10 + rng.below(150), 100);
                client.push_back(r.cl);
                was_dead.push_back(false);
                send(r, "New");
            }
            break;
        case 1:
            if (!client.empty()) send(oms.cancel(client[rng.below(client.size())]), "Cancel");
            break;
        case 2:
        case 3:
            if (!client.empty()) send(oms.replace(client[rng.below(client.size())], 10 + rng.below(150), 100), "Replace");
            break;
        case 4:
            if (!to_venue.empty()) { v.recv(to_venue.front(), rng); to_venue.pop_front(); note("venue recv"); }
            break;
        case 5:
        case 6:
            if (!v.processing.empty()) { v.apply_next(); note("venue apply"); }
            break;
        case 7:
        case 8: {
            const std::size_t before = v.out.size();
            v.fill_random(rng);
            if (v.out.size() != before) note("venue fill");
            break;
        }
        case 9:
            if (rng.below(6) == 0) {
                const std::size_t before = v.out.size();
                v.unsolicited_cancel(rng);
                if (v.out.size() != before) note("venue unsolicited cancel");
            }
            break;
        default:
            deliver();
            break;
        }
        ok = check();
    }
    // drain: 새 요청·체결 없이 선 위의 모든 것을 끝낸다
    while (ok && !to_venue.empty()) { v.recv(to_venue.front(), rng); to_venue.pop_front(); note("venue recv (drain)"); ok = check(); }
    while (ok && !v.processing.empty()) { v.apply_next(); note("venue apply (drain)"); ok = check(); }
    while (ok && !v.out.empty()) { deliver(); ok = check(); }
    if (ok) res.violation = check_quiescent(oms, v, client);
    cov.too_late += v.cov.too_late;
    cov.fill_during_modify += v.cov.fill_during_modify;
    cov.unsolicited += v.cov.unsolicited;
    cov.reordered += v.cov.reordered;
    cov.dups += v.cov.dups;
    cov.clipped += v.cov.clipped;
    return res;
}

struct SweepResult { int runs = 0, failures = 0; std::uint64_t first_seed = 0; std::string first_msg; Coverage cov; };

template <class O>
static SweepResult sweep(const O &proto, std::uint64_t from, int n, const SimCfg &cfg) {
    SweepResult r;
    for (int i = 0; i < n; ++i) {
        O oms = proto;
        const SimResult s = run_seed(oms, from + static_cast<std::uint64_t>(i), cfg, r.cov);
        ++r.runs;
        if (!s.violation.empty() && r.failures++ == 0) { r.first_seed = from + static_cast<std::uint64_t>(i); r.first_msg = s.violation; }
    }
    return r;
}

// ======================= 테스트 =======================
#ifndef QFIN_NO_MAIN
static int g_pass = 0, g_fail = 0;
#define CHECK(expr) do { if (expr) { ++g_pass; std::printf("PASS  %s\n", #expr); } \
                         else { ++g_fail; std::printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } } while (0)

static ExecId g_exec = 1000;
static ExecReport er(ExecType t, ClOrdId cl, ClOrdId orig, Qty last, Qty cum, Qty leaves, Qty qty) {
    return ExecReport{t, cl, orig, g_exec++, last, 100, cum, leaves, qty};
}
static ClOrdId id_of(const std::optional<Request> &r) { return r ? r->cl : 0; }

static void part_a() {
    std::printf("--- Part A: 체인 · 상태 · 잔량 · 포지션\n");
    Oms m;
    const Request a = m.new_order(Side::Buy, 100, 500);
    CHECK(m.view(a.cl).state == St::PendingNew);
    m.on_exec(er(ExecType::New, a.cl, 0, 0, 0, 100, 100));
    CHECK(m.view(a.cl).state == St::New && m.view(a.cl).leaves == 100);
    m.on_exec(er(ExecType::Trade, a.cl, 0, 30, 30, 70, 100));
    CHECK(m.view(a.cl).state == St::PartiallyFilled && m.view(a.cl).cum == 30 && m.view(a.cl).leaves == 70);
    m.on_exec(er(ExecType::Trade, a.cl, 0, 70, 100, 0, 100));
    CHECK(m.view(a.cl).state == St::Filled && m.view(a.cl).leaves == 0 && m.position() == 100);
    const Request b = m.new_order(Side::Sell, 40, 500);
    m.on_exec(er(ExecType::Trade, b.cl, 0, 40, 40, 0, 40));
    CHECK(m.position() == 60);

    Oms c;                                                       // 체인: A → B(replace) → C(replace)
    const Request o = c.new_order(Side::Buy, 100, 500);
    c.on_exec(er(ExecType::New, o.cl, 0, 0, 0, 100, 100));
    const auto r1 = c.replace(o.cl, 150, 500);
    CHECK(r1.has_value() && r1 && r1->orig == o.cl && r1->cl != o.cl);
    CHECK(c.view(o.cl).pending == Pending::Replace && c.view(id_of(r1)).found);
    CHECK(!c.replace(o.cl, 120, 500).has_value());               // 한 주문에 요청 하나만
    CHECK(!c.cancel(o.cl).has_value());
    c.on_exec(er(ExecType::Replaced, id_of(r1), o.cl, 0, 0, 150, 150));
    CHECK(c.view(o.cl).order_qty == 150 && c.view(o.cl).pending == Pending::None && c.view(o.cl).current_id == id_of(r1));
    const auto r2 = c.replace(id_of(r1), 90, 500);               // 다음 요청의 orig 는 r1
    CHECK(r2.has_value() && r2 && r2->orig == id_of(r1));
    c.on_cancel_reject(CancelReject{id_of(r2), id_of(r1), true, RejectReason::Unknown});
    CHECK(c.view(o.cl).pending == Pending::None && c.view(o.cl).order_qty == 150);
    const auto r3 = c.replace(o.cl, 80, 500);                    // 거절된 r2 는 건너뛴다
    CHECK(r3.has_value() && r3 && r3->orig == id_of(r1));

    Oms d;                                                       // 정상 취소
    const Request p = d.new_order(Side::Buy, 100, 500);
    d.on_exec(er(ExecType::New, p.cl, 0, 0, 0, 100, 100));
    d.on_exec(er(ExecType::Trade, p.cl, 0, 25, 25, 75, 100));
    const auto q = d.cancel(p.cl);
    CHECK(q.has_value() && d.view(p.cl).pending == Pending::Cancel);
    d.on_exec(er(ExecType::PendingCancel, id_of(q), p.cl, 0, 25, 75, 100));
    d.on_exec(er(ExecType::Canceled, id_of(q), p.cl, 0, 25, 0, 100));
    CHECK(d.view(p.cl).state == St::Canceled && d.view(p.cl).cum == 25 && d.view(p.cl).leaves == 0);
    CHECK(d.view(p.cl).pending == Pending::None && d.position() == 25);

    Oms e;                                                       // 신규 거절
    const Request n = e.new_order(Side::Sell, 10, 500);
    e.on_exec(er(ExecType::Rejected, n.cl, 0, 0, 0, 0, 10));
    CHECK(e.view(n.cl).state == St::Rejected && e.view(n.cl).leaves == 0 && e.position() == 0);
    CHECK(!e.cancel(n.cl).has_value());
}

static void part_b() {
    std::printf("--- Part B: 경쟁 시나리오\n");
    {   // too-late-to-cancel: Trade 와 거절의 도착 순서를 둘 다
        Oms m; const Request o = m.new_order(Side::Buy, 100, 500);
        m.on_exec(er(ExecType::New, o.cl, 0, 0, 0, 100, 100));
        const auto c = m.cancel(o.cl);
        m.on_exec(er(ExecType::Trade, o.cl, 0, 100, 100, 0, 100));
        m.on_cancel_reject(CancelReject{id_of(c), o.cl, false, RejectReason::TooLate});
        CHECK(m.view(o.cl).state == St::Filled && m.view(o.cl).pending == Pending::None && m.position() == 100);

        Oms n; const Request p = n.new_order(Side::Buy, 100, 500);
        n.on_exec(er(ExecType::New, p.cl, 0, 0, 0, 100, 100));
        const auto d = n.cancel(p.cl);
        n.on_cancel_reject(CancelReject{id_of(d), p.cl, false, RejectReason::TooLate});
        CHECK(n.view(p.cl).state == St::New && n.view(p.cl).pending == Pending::None);   // 거절이 체결을 앞질렀다
        n.on_exec(er(ExecType::Trade, p.cl, 0, 100, 100, 0, 100));
        CHECK(n.view(p.cl).state == St::Filled && n.position() == 100);
    }
    {   // 취소 진행 중 부분 체결 후 취소 성공
        Oms m; const Request o = m.new_order(Side::Buy, 100, 500);
        const auto c = m.cancel(o.cl);
        m.on_exec(er(ExecType::Trade, o.cl, 0, 40, 40, 60, 100));
        CHECK(m.view(o.cl).pending == Pending::Cancel && m.view(o.cl).leaves == 60);     // 취소 전까지 잔량은 살아 있다
        m.on_exec(er(ExecType::Canceled, id_of(c), o.cl, 0, 40, 0, 100));
        CHECK(m.view(o.cl).state == St::Canceled && m.view(o.cl).cum == 40 && m.position() == 40 && m.view(o.cl).leaves == 0);
    }
    {   // replace 중 부분 체결: 옛 ID 로 온 Trade + 새 수량은 총량
        Oms m; const Request o = m.new_order(Side::Buy, 100, 500);
        m.on_exec(er(ExecType::New, o.cl, 0, 0, 0, 100, 100));
        const auto r = m.replace(o.cl, 150, 500);
        m.on_exec(er(ExecType::Trade, o.cl, 0, 30, 30, 70, 100));
        m.on_exec(er(ExecType::Replaced, id_of(r), o.cl, 0, 30, 120, 150));
        const OrderView v = m.view(id_of(r));
        CHECK(v.order_qty == 150 && v.cum == 30 && v.leaves == 120 && v.pending == Pending::None && m.position() == 30);
    }
    {   // replace-down 인데 이미 더 많이 체결: 거래소가 준 order_qty 를 따른다
        Oms m; const Request o = m.new_order(Side::Buy, 100, 500);
        m.on_exec(er(ExecType::New, o.cl, 0, 0, 0, 100, 100));
        const auto r = m.replace(o.cl, 40, 500);
        m.on_exec(er(ExecType::Trade, o.cl, 0, 60, 60, 40, 100));
        m.on_exec(er(ExecType::Replaced, id_of(r), o.cl, 0, 60, 0, 60));
        CHECK(m.view(o.cl).order_qty == 60 && m.view(o.cl).state == St::Filled && m.view(o.cl).leaves == 0);
    }
    {   // 새 ID 로 온 Trade 가 Replaced 보다 먼저 도착
        Oms m; const Request o = m.new_order(Side::Buy, 100, 500);
        const auto r = m.replace(o.cl, 150, 500);
        m.on_exec(er(ExecType::Trade, id_of(r), o.cl, 120, 120, 30, 150));
        CHECK(m.view(o.cl).cum == 120 && m.position() == 120 && m.unknown_reports() == 0);
        CHECK(m.view(o.cl).state == St::Filled);                                         // 이 시점 OMS 시야의 수량은 아직 100
        m.on_exec(er(ExecType::Replaced, id_of(r), o.cl, 0, 120, 30, 150));
        CHECK(m.view(o.cl).state == St::PartiallyFilled && m.view(o.cl).leaves == 30);   // 다시 열린다: 계산된 상태라서
    }
    {   // 순서 뒤바뀐 Trade, ExecID 중복, gap 관찰
        Oms m; const Request o = m.new_order(Side::Buy, 100, 500);
        const ExecReport t1 = er(ExecType::Trade, o.cl, 0, 30, 30, 70, 100);
        const ExecReport t2 = er(ExecType::Trade, o.cl, 0, 20, 50, 50, 100);
        m.on_exec(t2);
        CHECK(m.view(o.cl).cum == 20 && m.view(o.cl).fill_gap == 30);                    // 앞선 30 이 어디 있는지 안다
        m.on_exec(t1);
        CHECK(m.view(o.cl).cum == 50 && m.view(o.cl).fill_gap == 0 && m.position() == 50);
        m.on_exec(t1);
        CHECK(m.position() == 50 && m.duplicates() == 1);
    }
    {   // 늦은 상태 메시지는 되돌리지 못한다
        Oms m; const Request o = m.new_order(Side::Buy, 100, 500);
        const auto r = m.replace(o.cl, 150, 500);
        m.on_exec(er(ExecType::Replaced, id_of(r), o.cl, 0, 0, 150, 150));
        m.on_exec(er(ExecType::PendingReplace, id_of(r), o.cl, 0, 0, 100, 100));
        CHECK(m.view(o.cl).pending == Pending::None && m.view(o.cl).order_qty == 150);

        Oms n; const Request p = n.new_order(Side::Buy, 100, 500);
        const auto c = n.cancel(p.cl);
        n.on_cancel_reject(CancelReject{id_of(c), p.cl, false, RejectReason::TooLate});
        n.on_exec(er(ExecType::PendingCancel, id_of(c), p.cl, 0, 0, 100, 100));
        CHECK(n.view(p.cl).pending == Pending::None);
    }
    {   // unsolicited cancel
        Oms m; const Request o = m.new_order(Side::Buy, 100, 500);
        m.on_exec(er(ExecType::New, o.cl, 0, 0, 0, 100, 100));
        m.on_exec(er(ExecType::Trade, o.cl, 0, 10, 10, 90, 100));
        m.on_exec(er(ExecType::Canceled, o.cl, 0, 0, 10, 0, 100));
        CHECK(m.view(o.cl).state == St::Canceled && m.view(o.cl).leaves == 0 && m.position() == 10);
        CHECK(!m.cancel(o.cl).has_value() && !m.replace(o.cl, 200, 500).has_value());
        m.on_exec(er(ExecType::New, o.cl, 0, 0, 0, 100, 100));                           // 늦은 New 로 살아나면 안 된다
        CHECK(m.view(o.cl).state == St::Canceled);
    }
    {   // replace 진행 중 unsolicited cancel → replace 는 나중에 거절
        Oms m; const Request o = m.new_order(Side::Buy, 100, 500);
        const auto r = m.replace(o.cl, 150, 500);
        m.on_exec(er(ExecType::Canceled, o.cl, 0, 0, 0, 0, 100));
        CHECK(m.view(o.cl).state == St::Canceled && m.view(o.cl).pending == Pending::Replace);
        m.on_cancel_reject(CancelReject{id_of(r), o.cl, true, RejectReason::TooLate});
        CHECK(m.view(o.cl).state == St::Canceled && m.view(o.cl).pending == Pending::None && m.view(o.cl).leaves == 0);
    }
    {   // 취소 확인 뒤에 도착한 체결: 포지션은 체결에서만 나온다
        Oms m; const Request o = m.new_order(Side::Sell, 100, 500);
        const auto c = m.cancel(o.cl);
        m.on_exec(er(ExecType::Canceled, id_of(c), o.cl, 0, 35, 0, 100));
        CHECK(m.view(o.cl).state == St::Canceled && m.view(o.cl).fill_gap == 35 && m.position() == 0);
        m.on_exec(er(ExecType::Trade, o.cl, 0, 35, 35, 65, 100));
        CHECK(m.view(o.cl).cum == 35 && m.view(o.cl).fill_gap == 0 && m.position() == -35 && m.view(o.cl).state == St::Canceled);
    }
    {   // 모르는 ID
        Oms m; m.on_exec(er(ExecType::Trade, 777, 0, 10, 10, 0, 10));
        m.on_cancel_reject(CancelReject{778, 0, false, RejectReason::Unknown});
        CHECK(m.unknown_reports() == 2 && m.position() == 0);
    }
}

static bool same(const OrderView &a, const OrderView &b) {
    return a.found == b.found && a.state == b.state && a.pending == b.pending && a.order_qty == b.order_qty &&
           a.cum == b.cum && a.leaves == b.leaves && a.fill_gap == b.fill_gap;
}
// msgs 의 모든 순열을 적용했을 때 최종 뷰와 포지션이 같은가. 틀린 순열 수를 돌려준다.
template <class Setup>
static long permutation_mismatches(Setup setup, const std::vector<Msg> &msgs, ClOrdId id, const OrderView &golden, Qty pos, long *total) {
    std::vector<std::size_t> p(msgs.size());
    std::iota(p.begin(), p.end(), 0u);
    long bad = 0, n = 0;
    do {
        Oms m = setup();
        for (std::size_t i : p) apply_msg(m, msgs[i]);
        ++n;
        if (!same(m.view(id), golden) || m.position() != pos) ++bad;
    } while (std::next_permutation(p.begin(), p.end()));
    *total = n;
    return bad;
}

static void part_c() {
    std::printf("--- Part C: 순열 전수 + seed sweep\n");
    long total = 0;
    {   // too-late: 5 메시지 120 순열
        auto setup = [] { Oms m; (void)m.new_order(Side::Buy, 100, 500); (void)m.cancel(1); return m; };
        const std::vector<Msg> msgs = {er(ExecType::New, 1, 0, 0, 0, 100, 100),        er(ExecType::Trade, 1, 0, 40, 40, 60, 100),
                                       er(ExecType::Trade, 1, 0, 60, 100, 0, 100),     er(ExecType::PendingCancel, 2, 1, 0, 40, 60, 100),
                                       CancelReject{2, 1, false, RejectReason::TooLate}};
        OrderView g; g.found = true; g.state = St::Filled; g.order_qty = 100; g.cum = 100;
        CHECK(permutation_mismatches(setup, msgs, 1, g, 100, &total) == 0 && total == 120);
    }
    {   // replace 중 부분 체결 + 중복 Trade: 7 메시지 5040 순열
        auto setup = [] { Oms m; (void)m.new_order(Side::Buy, 100, 500); (void)m.replace(1, 150, 500); return m; };
        const ExecReport t = er(ExecType::Trade, 1, 0, 30, 30, 70, 100);
        const std::vector<Msg> msgs = {er(ExecType::New, 1, 0, 0, 0, 100, 100),        t, t,
                                       er(ExecType::PendingReplace, 2, 1, 0, 30, 70, 100),
                                       er(ExecType::Replaced, 2, 1, 0, 30, 120, 150),
                                       er(ExecType::Trade, 2, 1, 20, 50, 100, 150),    er(ExecType::Trade, 2, 1, 100, 150, 0, 150)};
        OrderView g; g.found = true; g.state = St::Filled; g.order_qty = 150; g.cum = 150;
        CHECK(permutation_mismatches(setup, msgs, 1, g, 150, &total) == 0 && total == 5040);
    }
    {   // replace 진행 중 unsolicited cancel: 5 메시지 120 순열
        auto setup = [] { Oms m; (void)m.new_order(Side::Sell, 100, 500); (void)m.replace(1, 150, 500); return m; };
        const std::vector<Msg> msgs = {er(ExecType::New, 1, 0, 0, 0, 100, 100),        er(ExecType::Trade, 1, 0, 40, 40, 60, 100),
                                       er(ExecType::Canceled, 1, 0, 0, 40, 0, 100),    er(ExecType::PendingReplace, 2, 1, 0, 40, 60, 100),
                                       CancelReject{2, 1, true, RejectReason::TooLate}};
        OrderView g; g.found = true; g.state = St::Canceled; g.order_qty = 100; g.cum = 40;
        CHECK(permutation_mismatches(setup, msgs, 1, g, -40, &total) == 0 && total == 120);
    }

    const Oms proto;
    const SimCfg fifo{.window = 1, .dup_pct = 0, .steps = 80, .max_orders = 3};
    const SimCfg rough{.window = 8, .dup_pct = 20, .steps = 80, .max_orders = 3};
    const SweepResult a = sweep(proto, 0, 2000, fifo);
    if (a.failures) std::printf("      first failure: seed=%llu %s\n", static_cast<unsigned long long>(a.first_seed), a.first_msg.c_str());
    CHECK(a.runs == 2000 && a.failures == 0);
    const SweepResult b = sweep(proto, 0, 2000, rough);
    if (b.failures) std::printf("      first failure: seed=%llu %s\n", static_cast<unsigned long long>(b.first_seed), b.first_msg.c_str());
    CHECK(b.runs == 2000 && b.failures == 0);
    // sweep 이 시나리오를 실제로 건드렸는가 (안 건드린 sweep 의 0 failures 는 아무 뜻이 없다)
    std::printf("      coverage: too_late=%llu fill_during_modify=%llu unsolicited=%llu clipped=%llu reordered=%llu dups=%llu\n",
                static_cast<unsigned long long>(b.cov.too_late), static_cast<unsigned long long>(b.cov.fill_during_modify),
                static_cast<unsigned long long>(b.cov.unsolicited), static_cast<unsigned long long>(b.cov.clipped),
                static_cast<unsigned long long>(b.cov.reordered), static_cast<unsigned long long>(b.cov.dups));
    CHECK(b.cov.too_late > 0 && b.cov.fill_during_modify > 0 && b.cov.unsolicited > 0);
    CHECK(b.cov.clipped > 0 && b.cov.reordered > 0 && b.cov.dups > 0);

    // 같은 seed = 같은 trace (재현 가능), 다른 seed = 다른 trace
    int same_hash = 0;
    std::unordered_set<std::uint64_t> uniq;
    for (std::uint64_t s = 0; s < 200; ++s) {
        Oms x = proto, y = proto;
        Coverage c;
        const SimResult r1 = run_seed(x, s, rough, c);
        const SimResult r2 = run_seed(y, s, rough, c);
        if (r1.hash == r2.hash && r1.steps == r2.steps) ++same_hash;
        uniq.insert(r1.hash);
    }
    CHECK(same_hash == 200);
    CHECK(uniq.size() >= 195);
}

int main() {
    part_a();
    part_b();
    part_c();
    std::printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
#endif  // QFIN_NO_MAIN
