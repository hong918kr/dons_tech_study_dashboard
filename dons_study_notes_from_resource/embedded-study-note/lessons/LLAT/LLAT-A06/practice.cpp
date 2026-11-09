// LLAT-A06 추가 연습 — 한 번에 검증
//   연습 1  NanoLog 식 압축: varint + zigzag + ts 델타로 바이너리 스트림을 줄이고 무손실 복원
//   연습 2  스레드별 링 출력을 시간순으로 병합 (k-way merge, 같은 ts 는 링 순서)
//   연습 3  호출 지점별 속도 제한: 창(window)당 K 개, 넘친 수는 다음 창 첫 줄 앞에 "suppressed N"
//   연습 4  flush_sync(): 이 스레드가 지금까지 쓴 로그가 소비자를 다 통과할 때까지 기다린다 (크래시 직전 경로)
//
// 로거 본체는 solution.cpp 를 그대로 쓴다 (NLOG_NO_MAIN 으로 그쪽 테스트/main 은 빼고 include).
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread practice.cpp -o pr && ./pr
#define NLOG_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.cpp"

#include <algorithm>
#include <queue>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static std::uint64_t g_fake = 0;
static std::uint64_t fake_clock() { return ++g_fake; }
static std::uint64_t g_set_ts = 0;
static std::uint64_t set_clock() { return g_set_ts; }       // 연습 2: 호출마다 ts 를 직접 정한다
static Logger::Config cfg_with(ClockFn clk, std::uint64_t ring = 1u << 12) {
    Logger::Config c;
    c.ring_bytes = ring;
    c.clock = clk;
    return c;
}

// ============ 연습 1: varint / zigzag / ts 델타 압축 ============
static void put_varint(std::vector<unsigned char> &o, std::uint64_t v) {
    while (v >= 0x80) { o.push_back(static_cast<unsigned char>(v | 0x80)); v >>= 7; }
    o.push_back(static_cast<unsigned char>(v));
}
static bool get_varint(const std::vector<unsigned char> &in, std::size_t &pos, std::uint64_t &v) {
    v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (pos >= in.size()) return false;
        const unsigned char b = in[pos++];
        v |= static_cast<std::uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
    }
    return false;
}
static std::uint64_t zz(std::int64_t n) { return (static_cast<std::uint64_t>(n) << 1) ^ static_cast<std::uint64_t>(n >> 63); }
static std::int64_t unzz(std::uint64_t u) { return static_cast<std::int64_t>(u >> 1) ^ -static_cast<std::int64_t>(u & 1); }
static void put_raw(std::vector<unsigned char> &o, const void *p, std::size_t n) {
    const auto *c = static_cast<const unsigned char *>(p);
    o.insert(o.end(), c, c + n);
}

// raw(BinarySink 형식) → 압축. 프레임 경계는 사전으로 찾는다 (decode_stream 과 같은 규칙).
static bool compress_stream(const Dict &d, const std::vector<unsigned char> &raw, std::vector<unsigned char> &out) {
    if (raw.size() < 8) return false;
    out.assign(raw.begin(), raw.begin() + 8);                       // "NLG1" + 해시 그대로
    std::size_t pos = 8;
    std::uint64_t prev_ts = 0;
    while (pos < raw.size()) {
        if (raw.size() - pos < 10) return false;
        const std::uint16_t site = rd16(raw.data() + pos);
        const std::uint64_t ts = rd64(raw.data() + pos + 2);
        pos += 10;
        put_varint(out, site);
        put_varint(out, zz(static_cast<std::int64_t>(ts - prev_ts)));   // 스레드가 섞이면 음수 델타도 가능
        prev_ts = ts;
        const std::vector<Kind> drop_kind{Kind::U64};
        const std::vector<Kind> &kinds = site == DROP_SITE ? drop_kind : (site < d.size() ? d[site].kinds : drop_kind);
        if (site != DROP_SITE && site >= d.size()) return false;
        for (Kind k : kinds) {
            if (k == Kind::STR) {
                if (raw.size() - pos < 2) return false;
                const std::uint16_t n = rd16(raw.data() + pos);
                if (n > kMaxStr || raw.size() - pos < 2u + n) return false;
                put_varint(out, n);
                put_raw(out, raw.data() + pos + 2, n);
                pos += 2u + n;
                continue;
            }
            if (raw.size() - pos < 8) return false;
            const std::uint64_t w = rd64(raw.data() + pos);
            pos += 8;
            if (k == Kind::I64) put_varint(out, zz(static_cast<std::int64_t>(w)));
            else if (k == Kind::F64) put_raw(out, &w, 8);                // double 은 그대로 (압축 이득 적음)
            else put_varint(out, w);
        }
    }
    return true;
}
static bool decompress_stream(const Dict &d, const std::vector<unsigned char> &c, std::vector<unsigned char> &raw) {
    if (c.size() < 8) return false;
    raw.assign(c.begin(), c.begin() + 8);
    std::size_t pos = 8;
    std::uint64_t prev_ts = 0, v = 0;
    while (pos < c.size()) {
        if (!get_varint(c, pos, v) || v > 0xFFFF) return false;
        const std::uint16_t site = static_cast<std::uint16_t>(v);
        if (!get_varint(c, pos, v)) return false;
        const std::uint64_t ts = prev_ts + static_cast<std::uint64_t>(unzz(v));
        prev_ts = ts;
        put_raw(raw, &site, 2);
        put_raw(raw, &ts, 8);
        const std::vector<Kind> drop_kind{Kind::U64};
        if (site != DROP_SITE && site >= d.size()) return false;
        const std::vector<Kind> &kinds = site == DROP_SITE ? drop_kind : d[site].kinds;
        for (Kind k : kinds) {
            if (k == Kind::STR) {
                if (!get_varint(c, pos, v) || v > kMaxStr || c.size() - pos < v) return false;
                const std::uint16_t n = static_cast<std::uint16_t>(v);
                put_raw(raw, &n, 2);
                put_raw(raw, c.data() + pos, n);
                pos += n;
            } else if (k == Kind::F64) {
                if (c.size() - pos < 8) return false;
                put_raw(raw, c.data() + pos, 8);
                pos += 8;
            } else {
                if (!get_varint(c, pos, v)) return false;
                const std::uint64_t w = k == Kind::I64 ? static_cast<std::uint64_t>(unzz(v)) : v;
                put_raw(raw, &w, 8);
            }
        }
    }
    return true;
}

static void sensor_calls(Logger &lg) {   // 펌웨어 같은 작은 정수 위주
    for (int i = 0; i < 20; i++) NLOG(lg, INF, "adc ch=%u raw=%d temp_mC=%d", static_cast<unsigned>(i % 4), 2000 + i, -1500 + 10 * i);
    NLOG(lg, WRN, "state %s -> %s", "IDLE", "RUN");
}

static void ex1() {
    std::printf("-- 연습 1: varint + zigzag + ts 델타 압축 --\n");
    BinarySink bin;
    Logger lg(cfg_with(fake_clock), bin);
    g_fake = 5000;
    sensor_calls(lg);
    const Dict d = export_dict();
    bin.begin(d);
    lg.poll();
    std::vector<unsigned char> comp, back;
    CHECK(compress_stream(d, bin.bytes, comp));
    CHECK(decompress_stream(d, comp, back) && back == bin.bytes);      // 무손실
    std::string t1, t2;
    CHECK(decode_stream(d, back, t1) == 21 && decode_stream(d, bin.bytes, t2) == 21 && t1 == t2);
    // raw: 8 + 20×(10+24) + (10 + 6 + 5) = 709.  압축: 첫 프레임 ts 5001 → varint 2 B, 이후 델타 1 → 1 B
    CHECK(bin.bytes.size() == 709u);
    std::printf("  raw %zu B -> compressed %zu B (text %zu B)\n", bin.bytes.size(), comp.size(), t1.size());
    CHECK(comp.size() < bin.bytes.size() / 3);
    // 잘린 압축 스트림은 거절 (경계 밖을 읽지 않는다)
    std::vector<unsigned char> cut(comp.begin(), comp.end() - 3);
    CHECK(!decompress_stream(d, cut, back));
}

// ============ 연습 2: 시간순 병합 ============
struct Line { std::uint64_t ts; std::uint32_t ring; std::string text; };
// 링마다 따로 모은 뒤 (링 안은 이미 ts 순) 최소 힙으로 k-way merge. 같은 ts 면 링 번호가 작은 쪽 먼저.
static std::string poll_merged(Logger &lg) {
    std::vector<std::vector<Line>> per(lg.nrings());
    for (std::uint32_t i = 0; i < lg.nrings(); i++)
        lg.ring_at(i).consume([&](const RecHdr &h, const unsigned char *a) {
            std::string s;
            format_line(h.site, h.ts, a, s);
            per[i].push_back({h.ts, i, std::move(s)});
        }, ~std::size_t{0});
    using Item = std::pair<std::pair<std::uint64_t, std::uint32_t>, std::size_t>;   // ((ts, ring), index)
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    for (std::uint32_t i = 0; i < per.size(); i++)
        if (!per[i].empty()) pq.push({{per[i][0].ts, i}, 0});
    std::string out;
    while (!pq.empty()) {
        const auto [key, idx] = pq.top();
        pq.pop();
        const std::uint32_t r = key.second;
        out += per[r][idx].text;
        if (idx + 1 < per[r].size()) pq.push({{per[r][idx + 1].ts, r}, idx + 1});
    }
    return out;
}
static void ex2() {
    std::printf("-- 연습 2: 스레드별 링 → 시간순 병합 --\n");
    TextSink unused;
    Logger lg(cfg_with(set_clock), unused);
    // 스레드 A: ts 1, 4, 5, 9   스레드 B: ts 2, 3, 5, 8 — 차례로 돌린다 (join 이 happens-before 를 만든다)
    std::thread a([&] { for (std::uint64_t t : {1, 4, 5, 9}) { g_set_ts = t; NLOG(lg, INF, "A%d", static_cast<int>(t)); } });
    a.join();
    std::thread b([&] { for (std::uint64_t t : {2, 3, 5, 8}) { g_set_ts = t; NLOG(lg, INF, "B%d", static_cast<int>(t)); } });
    b.join();
    CHECK(lg.nrings() == 2u);
    const std::string m = poll_merged(lg);
    CHECK(m == "[1] I A1\n[2] I B2\n[3] I B3\n[4] I A4\n[5] I A5\n[5] I B5\n[8] I B8\n[9] I A9\n");
}

// ============ 연습 3: 호출 지점별 속도 제한 ============
// 창 = [start, start + W) 틱. 창마다 처음 K 개만 기록. 새 창의 첫 기록 앞에 "suppressed N" (N > 0 일 때).
// 상태는 호출 지점마다 thread_local → 스레드끼리 경쟁이 없다 (한도는 스레드별).
struct RateState { std::uint64_t start = 0, count = 0, suppressed = 0; bool init = false; };
static bool rate_admit(RateState &s, std::uint64_t now, std::uint64_t k, std::uint64_t w, std::uint64_t &report) {
    report = 0;
    if (!s.init || now - s.start >= w) {
        report = s.suppressed;
        s = RateState{now, 0, 0, true};
    }
    if (s.count < k) { s.count++; return true; }
    s.suppressed++;
    return false;
}
#define NLOG_RATELIMIT(lg, lvl, K, W, fmt, ...)                                                       \
    do {                                                                                              \
        thread_local RateState nlog_rs_;                                                              \
        std::uint64_t nlog_rep_ = 0;                                                                  \
        if (rate_admit(nlog_rs_, (lg).config().clock(), (K), (W), nlog_rep_)) {                      \
            if (nlog_rep_) NLOG(lg, lvl, "suppressed %llu", static_cast<unsigned long long>(nlog_rep_)); \
            NLOG(lg, lvl, fmt __VA_OPT__(, ) __VA_ARGS__);                                           \
        }                                                                                             \
    } while (0)

static std::uint64_t g_rl_now = 0;
static std::uint64_t rl_clock() { return g_rl_now; }
static void ex3() {
    std::printf("-- 연습 3: 호출 지점별 속도 제한 --\n");
    TextSink sink;
    Logger lg(cfg_with(rl_clock), sink);
    for (int i = 1; i <= 30; i++) { g_rl_now = static_cast<std::uint64_t>(i); NLOG_RATELIMIT(lg, WRN, 3, 10, "overcurrent i=%d", i); }
    lg.poll();
    const std::string want =
        "[1] W overcurrent i=1\n[2] W overcurrent i=2\n[3] W overcurrent i=3\n"
        "[11] W suppressed 7\n[11] W overcurrent i=11\n[12] W overcurrent i=12\n[13] W overcurrent i=13\n"
        "[21] W suppressed 7\n[21] W overcurrent i=21\n[22] W overcurrent i=22\n[23] W overcurrent i=23\n";
    CHECK(sink.text == want);
    if (sink.text != want) std::printf("%s", sink.text.c_str());
    CHECK(std::count(sink.text.begin(), sink.text.end(), '\n') == 11);
}

// ============ 연습 4: flush_sync ============
// 이 스레드 링의 지금 tail 을 찍고, 소비자가 head 를 그 이상으로 옮길 때까지 기다린다 (acquire).
// 소비자는 레코드를 sink 에 넘긴 "다음" head 를 release 로 쓴다 → 돌아온 시점엔 그 레코드들이 텍스트에 있다.
static bool flush_sync(Logger &lg, std::chrono::milliseconds timeout) {
    StagingRing &r = lg.my_ring();
    const std::uint64_t target = r.tail.load(memory_order_relaxed);   // 내가 쓴 변수
    const auto dl = std::chrono::steady_clock::now() + timeout;
    while (r.head.load(memory_order_acquire) < target) {
        if (std::chrono::steady_clock::now() > dl) return false;
        std::this_thread::yield();
    }
    return true;
}
static void ex4() {
    std::printf("-- 연습 4: flush_sync (크래시 직전 동기 비우기) --\n");
    TextSink sink;                       // file 없음 → 소비자의 flush() 는 text 를 건드리지 않는다
    Logger::Config c;
    c.ring_bytes = 1u << 14;
    Logger lg(c, sink);
    lg.start();
    for (int i = 0; i < 500; i++) NLOG(lg, INF, "step %d", i);
    NLOG(lg, ERR, "fatal: watchdog in %s", "motor_task");
    const bool ok = flush_sync(lg, std::chrono::seconds(5));
    // 이제 이 스레드는 더 쓰지 않고, 다른 링도 없다 → 소비자는 text 를 더 바꾸지 않는다 → 읽어도 race 아님
    CHECK(ok);
    const std::string last = "E fatal: watchdog in motor_task\n";
    CHECK(sink.text.size() > last.size() && sink.text.compare(sink.text.size() - last.size(), last.size(), last) == 0);
    CHECK(std::count(sink.text.begin(), sink.text.end(), '\n') == 501);
    lg.stop();
}

int main() {
    ex1();
    ex2();
    ex3();
    ex4();
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
