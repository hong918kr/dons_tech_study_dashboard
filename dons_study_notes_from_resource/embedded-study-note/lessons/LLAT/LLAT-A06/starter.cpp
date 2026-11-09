// LLAT-A06 저지연 비동기 로거 (NanoLog 스타일)
//
// 호스트 모델: hot path(로그를 부르는 스레드)는 printf 를 하지 않는다. 호출 지점마다 한 번만 등록한
// "포맷 id" 와 타임스탬프, 원시 인자 바이트만 스레드 전용 SPSC 바이트 링에 복사하고 끝낸다.
// 포맷팅(문자열 만들기)과 파일 쓰기는 백그라운드 스레드(소비자)가 한다.  (Yang et al., USENIX ATC 2018)
//   Part A  사이트 레지스트리(함수 내부 static) + 레코드 인코딩 + SPSC 바이트 링(reserve/commit, 패드 레코드)
//           + 소비자 쪽 포맷팅. 수동 poll() 로 결정적으로 검사한다.
//   Part B  버퍼 가득 참 정책: DROP(정확한 drop 수 + 빈자리에 "dropped N" 마커) vs BLOCK(손실 0, 생산자 정지,
//           상한 시간 후 timeout). 문자열 수명(포인터 저장의 함정). 백그라운드 스레드 + 생산자 4개 스트레스.
//   Part C  펌웨어 모델: 바이너리 프레임 스트림 + 사전(dictionary) + 오프라인 디코더 (Trice / defmt / RTT 방식).
//
// 체크 테스트는 "올바른 구현이면 매 실행 항상 성립하는 것" 만 본다 (정확한 텍스트, 정확한 drop 수,
// 강제로 만든 '가득 참' 상태, 모든 메시지 정확히 한 번). 지연·처리량 숫자는 demo_bench.cpp 가 잰다.
//
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread solution.cpp -o so && ./so   (starter.cpp 도 같은 명령)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

using std::memory_order_acquire, std::memory_order_relaxed, std::memory_order_release;

// ======================= 공용: 시계, 레벨, 인자 종류 =======================
// 이 맥(M6)의 cntvct_el0: cntfrq_el0 = 1 GHz 로 보고되지만 실제 값은 ~42 ns 마다 바뀐다 (24 MHz 원천).
// 유저 모드에서 읽힌다 (mrs). 다른 아키텍처는 steady_clock.
static inline std::uint64_t now_ticks() {
#if defined(__aarch64__)
    std::uint64_t v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#else
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}
using ClockFn = std::uint64_t (*)();

enum Level : std::uint8_t { DBG = 0, INF = 1, WRN = 2, ERR = 3 };
static constexpr char kLevelChar[4] = {'D', 'I', 'W', 'E'};

enum class Kind : std::uint8_t { I64, U64, F64, STR, PTR };       // 링에 들어가는 인자 표현
template <class> inline constexpr bool always_false = false;
template <class T> constexpr Kind kind_of() {
    using U = std::decay_t<T>;
    if constexpr (std::is_floating_point_v<U>) return Kind::F64;
    else if constexpr (std::is_same_v<U, bool>) return Kind::I64;
    else if constexpr (std::is_integral_v<U> || std::is_enum_v<U>) return std::is_signed_v<U> ? Kind::I64 : Kind::U64;
    else if constexpr (std::is_same_v<U, const char *> || std::is_same_v<U, char *> ||
                       std::is_same_v<U, std::string_view> || std::is_same_v<U, std::string>) return Kind::STR;
    else if constexpr (std::is_pointer_v<U>) return Kind::PTR;
    else static_assert(always_false<U>, "log argument type not supported");
}
template <class... A> struct KindList {
    static constexpr std::size_t n = sizeof...(A);
    static constexpr Kind kinds[n + 1] = {kind_of<A>()..., Kind::I64};   // +1: 인자 0개일 때도 배열이 되게
};
template <class... A> KindList<std::decay_t<A>...> kinds_of(A &&...);    // 선언만: decltype 안에서만 쓴다

// ======================= 사이트 레지스트리 (정적 정보는 한 번만) =======================
// 포맷 문자열을 조각(Frag)으로 미리 쪼갠다: 리터럴 + 변환 명세 1개. 소비자는 조각마다 snprintf 한 번.
struct Frag {
    std::string lit;     // 앞쪽 리터럴 ("%%" 는 이미 '%' 로 바꿔 둠)
    std::string spec;    // "%08.3f" 같은 명세 (비면 리터럴만)
    char conv = 0;       // d i u o x X c f F e E g G a A s p
    char len = 0;        // 0, 'h', 'H'(hh), 'l', 'L'(ll), 'z', 'j'
};
struct Site {
    const char *fmt = nullptr, *file = nullptr;
    int line = 0;
    Level level = INF;
    std::uint8_t nargs = 0;
    Kind kinds[8] = {};
    std::vector<Frag> frags;
};
static constexpr std::uint16_t kMaxSites = 4096;
inline constexpr std::uint16_t BAD_SITE = 0xFFFE, PAD_SITE = 0xFFFF, DROP_SITE = 0xFFFD;
static Site g_sites[kMaxSites];
static std::mutex g_site_mu;
static std::atomic<std::uint32_t> g_nsites{0};

// printf 문법의 부분집합을 파싱. 지원 못 하는 것('*' 폭, %n, L, 모르는 변환)은 false.
static bool parse_format(const char *fmt, std::vector<Frag> &out) {
    out.clear();
    Frag f;
    for (const char *p = fmt; *p;) {
        if (*p != '%') { f.lit += *p++; continue; }
        if (p[1] == '%') { f.lit += '%'; p += 2; continue; }
        const char *s = p++;
        while (*p && std::strchr("-+ #0", *p)) p++;
        while (*p >= '0' && *p <= '9') p++;
        if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
        if (*p == '*') return false;
        f.len = 0;
        if (p[0] == 'h' && p[1] == 'h') { f.len = 'H'; p += 2; }
        else if (p[0] == 'l' && p[1] == 'l') { f.len = 'L'; p += 2; }
        else if (*p == 'h' || *p == 'l' || *p == 'z' || *p == 'j') f.len = *p++;
        if (!*p || !std::strchr("diuoxXcfFeEgGaAsp", *p)) return false;
        f.conv = *p++;
        f.spec.assign(s, static_cast<std::size_t>(p - s));
        out.push_back(f);
        f = Frag{};
    }
    if (!f.lit.empty() || out.empty()) out.push_back(f);   // 꼬리 리터럴 (spec 없음)
    return true;
}
static bool kind_fits(char conv, Kind k) {
    if (std::strchr("diuoxXc", conv)) return k == Kind::I64 || k == Kind::U64;
    if (std::strchr("fFeEgGaA", conv)) return k == Kind::F64;
    if (conv == 's') return k == Kind::STR;
    return k == Kind::PTR;   // 'p'
}
// 콜드 경로: 호출 지점마다 딱 한 번 (함수 내부 static 초기화가 부른다). 반환 = 사이트 id 또는 BAD_SITE.
template <class... A>
static std::uint16_t register_site(const char *fmt, const char *file, int line, Level lvl, KindList<A...>) {
    using KL = KindList<A...>;
    std::vector<Frag> frags;
    if (!parse_format(fmt, frags) || KL::n > 8) return BAD_SITE;
    std::size_t nspec = 0;
    for (const Frag &f : frags)
        if (f.conv) { if (nspec >= KL::n || !kind_fits(f.conv, KL::kinds[nspec])) return BAD_SITE; nspec++; }
    if (nspec != KL::n) return BAD_SITE;
    std::lock_guard<std::mutex> g(g_site_mu);
    const std::uint32_t id = g_nsites.load(memory_order_relaxed);
    if (id >= kMaxSites) return BAD_SITE;
    Site &s = g_sites[id];
    s.fmt = fmt; s.file = file; s.line = line; s.level = lvl;
    s.nargs = static_cast<std::uint8_t>(KL::n);
    for (std::size_t i = 0; i < KL::n; i++) s.kinds[i] = KL::kinds[i];
    s.frags = std::move(frags);
    g_nsites.store(id + 1, memory_order_release);
    return static_cast<std::uint16_t>(id);
}

// ======================= 레코드 와이어 형식 =======================
// [RecHdr 16 B][인자 바이트 argbytes][0..7 B 패딩]   — 레코드 시작은 항상 8 B 정렬.
//   스칼라 인자(I64/U64/F64/PTR) = 8 B 그대로(memcpy, 이 기계의 엔디언). 문자열 = u16 길이 + 바이트(NUL 없음).
// 패드 레코드: 링 끝에 남은 칸이 모자라면 생산자가 {size = 남은 칸, site = PAD_SITE} 8 B 만 쓰고 0 으로 간다.
struct RecHdr {
    std::uint32_t size;       // 레코드 전체 (헤더 + 인자 + 패딩), 8 의 배수
    std::uint16_t site;       // 사이트 id / PAD_SITE / DROP_SITE
    std::uint16_t argbytes;   // 인자 바이트 수 (패딩 제외)
    std::uint64_t ts;         // 호출 시각 (원시 카운터)
};
static_assert(sizeof(RecHdr) == 16);
inline constexpr std::size_t kMaxStr = 200;   // 문자열 인자는 최대 200 B 까지 복사 (넘으면 자른다)
static constexpr std::uint32_t round8(std::size_t n) { return static_cast<std::uint32_t>((n + 7u) & ~std::size_t{7}); }

[[maybe_unused]] static std::string_view as_sv(const char *s) { return s ? std::string_view(s) : std::string_view("(null)"); }
[[maybe_unused]] static std::string_view as_sv(std::string_view s) { return s; }
[[maybe_unused]] static std::string_view as_sv(const std::string &s) { return s; }

// ---- TODO A2: 인자 하나의 링 안 크기와 쓰기 ----
template <class T> static std::size_t arg_bytes(const T &v) {
    (void)v;
    return 0;   // TODO: 스칼라 8 B, 문자열 2 + min(길이, kMaxStr)
}
template <class T> static unsigned char *put_arg(unsigned char *p, const T &v) {
    (void)v;
    return p;   // TODO: 종류별로 memcpy (문자열은 u16 길이 + 바이트 복사), 다음 쓸 위치를 돌려준다
}

// ======================= SPSC 바이트 링 (스레드마다 하나) =======================
// tail/head 는 자유 증가 바이트 카운터 (u64, 넘침 없음). used = tail - head, 위치 = & mask.
// 생산자 라인 / 소비자 라인을 128 B(이 맥의 캐시 라인) 로 떨어뜨린다.
struct StagingRing {
    alignas(128) std::atomic<std::uint64_t> tail{0};    // 생산자만 쓴다 (commit 한 바이트 끝)
    std::uint64_t head_cache = 0;                        // 생산자 전용: 마지막으로 본 head
    std::uint64_t pending = 0;                           // 생산자 전용: reserve 가 정한 새 tail
    std::uint64_t pending_drops = 0;                     // 생산자 전용: 아직 마커로 못 쓴 drop 수
    std::atomic<std::uint64_t> dropped{0}, stalls{0}, timeouts{0}, pads{0};   // 생산자가 쓰고 누구나 읽는 통계
    alignas(128) std::atomic<std::uint64_t> head{0};    // 소비자만 쓴다 (다 읽은 바이트 끝)
    alignas(128) std::unique_ptr<unsigned char[]> buf;
    std::uint64_t cap = 0, mask = 0;
    std::thread::id owner;

    explicit StagingRing(std::uint64_t capacity) : buf(new unsigned char[capacity]()), cap(capacity), mask(capacity - 1) {}

    // ---- TODO A1: 연속된 n 바이트 자리를 예약 (n 은 8 의 배수, n <= cap/2). 자리가 없으면 nullptr. ----
    // 끝에 연속 n 이 안 남으면 남은 칸을 패드 레코드로 채우고 0 부터. 공개는 commit() 이 한다.
    unsigned char *reserve(std::uint32_t n) {
        (void)n;
        return nullptr;   // TODO: tail(relaxed) → 패드 계산 → head_cache 로 판단, 모자라면 head(acquire) 재확인
    }
    void commit() { /* TODO: tail 을 pending 으로 공개 (어떤 memory_order?) */ }

    // ---- TODO A4: 소비자. 공개된 레코드를 최대 max_recs 개 fn(hdr, args) 로 넘기고 head 를 돌려준다. ----
    template <class F> std::size_t consume(F &&fn, std::size_t max_recs) {
        (void)fn;
        (void)max_recs;
        return 0;   // TODO: tail(acquire) 까지 레코드를 읽고 (패드는 건너뜀) head 를 release 로 돌려준다
    }
};

// ======================= 소비자 쪽 포맷팅 =======================
static std::uint64_t rd64(const unsigned char *p) { std::uint64_t v; std::memcpy(&v, p, 8); return v; }
static std::uint16_t rd16(const unsigned char *p) { std::uint16_t v; std::memcpy(&v, p, 2); return v; }

[[maybe_unused]] static void fmt_int(std::string &out, const Frag &f, std::uint64_t bits) {
    char b[64];
    const char *sp = f.spec.c_str();
    const bool sgn = f.conv == 'd' || f.conv == 'i' || f.conv == 'c';
    int n;
    switch (f.len) {
    case 'l': n = sgn ? std::snprintf(b, sizeof b, sp, static_cast<long>(bits)) : std::snprintf(b, sizeof b, sp, static_cast<unsigned long>(bits)); break;
    case 'L': n = sgn ? std::snprintf(b, sizeof b, sp, static_cast<long long>(bits)) : std::snprintf(b, sizeof b, sp, static_cast<unsigned long long>(bits)); break;
    case 'z': n = sgn ? std::snprintf(b, sizeof b, sp, static_cast<std::make_signed_t<std::size_t>>(bits)) : std::snprintf(b, sizeof b, sp, static_cast<std::size_t>(bits)); break;
    case 'j': n = sgn ? std::snprintf(b, sizeof b, sp, static_cast<std::intmax_t>(bits)) : std::snprintf(b, sizeof b, sp, static_cast<std::uintmax_t>(bits)); break;
    default:  n = sgn ? std::snprintf(b, sizeof b, sp, static_cast<int>(bits)) : std::snprintf(b, sizeof b, sp, static_cast<unsigned>(bits)); break;
    }
    if (n > 0) out.append(b, std::min<std::size_t>(static_cast<std::size_t>(n), sizeof b - 1));
}

// ---- TODO A3: 사이트의 조각 순서대로 인자를 꺼내 out 에 메시지를 붙인다. 반환 = 읽은 인자 바이트 수. ----
static std::size_t format_message(const Kind *kinds, const std::vector<Frag> &frags, const unsigned char *a, std::string &out) {
    (void)kinds;
    (void)frags;
    (void)a;
    (void)out;
    return 0;   // TODO: 조각마다 lit 붙이고, conv 가 있으면 인자 하나를 꺼내 spec 으로 snprintf (정수는 fmt_int)
}

// 한 줄: "[ts] L message\n". DROP 마커: "[ts] W nlog: dropped N messages\n".
static void format_line(std::uint16_t site, std::uint64_t ts, const unsigned char *args, std::string &out) {
    char b[48];
    if (site == DROP_SITE) {
        std::snprintf(b, sizeof b, "[%" PRIu64 "] W ", ts);
        out += b;
        out += "nlog: dropped " + std::to_string(rd64(args)) + " messages\n";
        return;
    }
    const Site &s = g_sites[site];
    std::snprintf(b, sizeof b, "[%" PRIu64 "] %c ", ts, kLevelChar[s.level]);
    out += b;
    format_message(s.kinds, s.frags, args, out);
    out += '\n';
}

// ======================= 싱크 =======================
struct Sink {
    virtual void on_record(const RecHdr &h, const unsigned char *args) = 0;
    virtual void flush() {}
    virtual ~Sink() = default;
};
struct TextSink final : Sink {
    std::string text;
    std::FILE *file = nullptr;        // nullptr 이면 메모리에만 (테스트)
    void on_record(const RecHdr &h, const unsigned char *args) override {
        format_line(h.site, h.ts, args, text);
        if (file && text.size() > (64u << 10)) flush();
    }
    void flush() override {
        if (file && !text.empty()) { std::fwrite(text.data(), 1, text.size(), file); text.clear(); }
    }
};

// ======================= 로거 =======================
enum class Policy { Drop, Block };
static std::atomic<std::uint64_t> g_logger_uid{1};

class Logger {
public:
    struct Config {
        std::uint64_t ring_bytes = 1u << 16;               // 스레드당 링 크기 (2 의 거듭제곱)
        Policy policy = Policy::Drop;
        std::chrono::nanoseconds block_timeout{std::chrono::seconds(1)};   // BLOCK 이 영원히 막지 않게
        ClockFn clock = now_ticks;
    };
    Logger(Config c, Sink &sink) : cfg_(c), sink_(sink), uid_(g_logger_uid.fetch_add(1)) {}
    ~Logger() { stop(); for (auto &r : rings_) delete r.load(memory_order_relaxed); }
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    Level min_level = DBG;
    std::atomic<std::uint64_t> bad{0}, oversize{0};

    // ---- hot path ----
    template <class... A> bool log(std::uint16_t site, const A &...a) {
        const std::uint64_t ts = cfg_.clock();                  // 호출 시각 (막혀도 호출 시각이 남는다)
        if (site >= BAD_SITE) { bad.fetch_add(1, memory_order_relaxed); return false; }
        const std::size_t ab = (std::size_t{0} + ... + arg_bytes(a));
        const std::uint32_t n = round8(sizeof(RecHdr) + ab);
        StagingRing &r = ring();
        if (n > r.cap / 2) { oversize.fetch_add(1, memory_order_relaxed); return false; }
        unsigned char *p = acquire_space(r, n, ts);
        if (!p) return false;
        const RecHdr h{n, site, static_cast<std::uint16_t>(ab), ts};
        std::memcpy(p, &h, sizeof h);
        unsigned char *q = p + sizeof h;
        ((q = put_arg(q, a)), ...);
        (void)q;
        r.commit();
        return true;
    }

    // ---- 소비자: 모든 링을 한 바퀴 돈다. 반환 = 처리한 레코드 수 ----
    std::size_t poll(std::size_t max_per_ring = 256) {
        std::size_t total = 0;
        const std::uint32_t n = nrings_.load(memory_order_acquire);
        for (std::uint32_t i = 0; i < n; i++) {
            StagingRing *r = rings_[i].load(memory_order_acquire);
            total += r->consume([this](const RecHdr &h, const unsigned char *a) { sink_.on_record(h, a); }, max_per_ring);
        }
        return total;
    }
    void start() {
        stop_.store(false, memory_order_relaxed);
        bg_ = std::thread([this] {
            unsigned idle = 0;
            while (!stop_.load(memory_order_acquire)) {
                if (poll()) { idle = 0; continue; }
                if (++idle < 64) std::this_thread::yield();
                else { sink_.flush(); std::this_thread::sleep_for(std::chrono::microseconds(50)); }
            }
            while (poll()) {}                                      // 멈출 때 남은 것 전부
            sink_.flush();
        });
    }
    void stop() {
        if (!bg_.joinable()) return;
        stop_.store(true, memory_order_release);
        bg_.join();
    }
    std::uint32_t nrings() const { return nrings_.load(memory_order_acquire); }
    StagingRing &ring_at(std::uint32_t i) { return *rings_[i].load(memory_order_acquire); }
    StagingRing &my_ring() { return ring(); }
    const Config &config() const { return cfg_; }

private:
    // ---- TODO B1: 가득 참 정책. 성공하면 n 바이트 자리 (commit 은 호출자가). ----
    // 밀린 drop 이 있으면 먼저 DROP 마커 레코드(헤더 + u64 개수)를 써서 "빠진 자리" 에 표시한다.
    unsigned char *acquire_space(StagingRing &r, std::uint32_t n, std::uint64_t ts) {
        (void)ts;
        return r.reserve(n);   // TODO: 밀린 drop 마커 → reserve → 실패하면 Drop: note_drop / Block: stalls++, 돌고 양보, timeout
    }
    static unsigned char *note_drop(StagingRing &r) {
        r.pending_drops++;
        r.dropped.store(r.dropped.load(memory_order_relaxed) + 1, memory_order_relaxed);
        return nullptr;
    }

    // 스레드마다 링 하나. thread_local 1칸 캐시 (logger uid 로 구분) → 빠른 경로는 비교 한 번.
    StagingRing &ring() {
        thread_local std::uint64_t c_uid = 0;
        thread_local StagingRing *c_ring = nullptr;
        if (c_uid == uid_) return *c_ring;
        c_ring = &ring_slow();
        c_uid = uid_;
        return *c_ring;
    }
    // 느린 경로: 이 스레드가 이 로거용 링을 이미 만들었나? — thread_local 목록으로 찾는다.
    // (thread::id 로 찾으면 안 된다: 끝난 스레드의 id 는 새 스레드가 다시 받는다 → 남의 링을 이어 씀)
    StagingRing &ring_slow() {
        thread_local std::vector<std::pair<std::uint64_t, StagingRing *>> mine;
        for (const auto &e : mine)
            if (e.first == uid_) return *e.second;
        std::lock_guard<std::mutex> g(reg_mu_);
        const std::uint32_t n = nrings_.load(memory_order_relaxed);
        if (n >= kMaxThreads) std::abort();
        auto *r = new StagingRing(cfg_.ring_bytes);
        r->owner = std::this_thread::get_id();
        rings_[n].store(r, memory_order_release);
        nrings_.store(n + 1, memory_order_release);
        mine.emplace_back(uid_, r);
        return *r;
    }

    static constexpr std::uint32_t kMaxThreads = 64;
    Config cfg_;
    Sink &sink_;
    const std::uint64_t uid_;
    std::mutex reg_mu_;
    std::atomic<StagingRing *> rings_[kMaxThreads] = {};
    std::atomic<std::uint32_t> nrings_{0};
    std::atomic<bool> stop_{false};
    std::thread bg_;
};

// 호출 지점 매크로: (1) if(false) printf → 컴파일러가 -Wformat 으로 포맷/인자를 검사 (실행 0 비용)
// (2) 함수 내부 static → 첫 호출 때만 register_site (스레드 안전 초기화), 이후엔 guard 확인 한 번
// (3) 레벨이 낮으면 인자 평가도 안 한다.
#define NLOG(lg, lvl, fmt, ...)                                                                          \
    do {                                                                                                 \
        if (false) std::printf(fmt __VA_OPT__(, ) __VA_ARGS__);                                         \
        if ((lvl) >= (lg).min_level) {                                                                   \
            static const std::uint16_t nlog_site_ =                                                      \
                register_site(fmt, __FILE__, __LINE__, lvl, decltype(kinds_of(__VA_ARGS__)){});          \
            (lg).log(nlog_site_ __VA_OPT__(, ) __VA_ARGS__);                                            \
        }                                                                                                \
    } while (0)

// 깨진 비교 대상 (주어짐): 문자열 인자를 "포인터로" 저장하는 로거 한 줄. 소비자가 나중에 그 주소를 읽는다.
struct PtrLogEntry { std::uint64_t ts; const char *fmt; const char *arg; };
struct PtrLogger {
    std::vector<PtrLogEntry> q;
    void log(std::uint64_t ts, const char *fmt, const char *arg) { q.push_back({ts, fmt, arg}); }
    std::string drain() {
        std::string out;
        for (auto &e : q) {
            char b[256];
            std::snprintf(b, sizeof b, "[%" PRIu64 "] I ", e.ts);
            out += b;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
            std::snprintf(b, sizeof b, e.fmt, e.arg);
#pragma clang diagnostic pop
            out += b;
            out += '\n';
        }
        q.clear();
        return out;
    }
};

// ======================= Part C: 바이너리 프레임 + 사전 + 오프라인 디코더 =======================
// 스트림 = "NLG1" + u32 사전 해시, 그 뒤 프레임 반복: [u16 site][u64 ts][인자 바이트 (링과 같은 표현)].
// 프레임에는 길이가 없다 → 디코더는 사전(인자 종류, 문자열 길이)이 있어야만 경계를 찾는다.
struct DictEntry { std::string fmt; Level level; std::vector<Kind> kinds; std::vector<Frag> frags; };
using Dict = std::vector<DictEntry>;
static Dict export_dict() {
    Dict d;
    const std::uint32_t n = g_nsites.load(memory_order_acquire);
    for (std::uint32_t i = 0; i < n; i++) {
        const Site &s = g_sites[i];
        d.push_back({s.fmt, s.level, std::vector<Kind>(s.kinds, s.kinds + s.nargs), s.frags});
    }
    return d;
}
static std::uint32_t dict_hash(const Dict &d) {
    std::uint32_t h = 2166136261u;
    auto mix = [&h](unsigned char c) { h ^= c; h *= 16777619u; };
    for (const DictEntry &e : d) {
        for (char c : e.fmt) mix(static_cast<unsigned char>(c));
        mix(0); mix(e.level);
        for (Kind k : e.kinds) mix(static_cast<unsigned char>(k));
        mix(0xFF);
    }
    return h;
}
struct BinarySink final : Sink {
    std::vector<unsigned char> bytes;
    std::uint64_t frames = 0;
    void begin(const Dict &d) {
        const std::uint32_t h = dict_hash(d);
        bytes.insert(bytes.end(), {'N', 'L', 'G', '1'});
        put(&h, 4);
    }
    void put(const void *p, std::size_t n) {
        const auto *c = static_cast<const unsigned char *>(p);
        bytes.insert(bytes.end(), c, c + n);
    }
    // ---- TODO C1: 링 레코드 → 프레임 (size·argbytes·패딩은 버린다) ----
    void on_record(const RecHdr &h, const unsigned char *args) override {
        (void)h;
        (void)args;   // TODO: [u16 site][u64 ts][argbytes 바이트] 를 bytes 에 붙이고 frames++
    }
};
// ---- TODO C2: 오프라인 디코더. 반환 = 디코드한 프레임 수, -1 = 헤더/사전 불일치, -2 = 잘린/잘못된 프레임
//      (-2 여도 그 앞까지의 텍스트는 out 에 남긴다). 입력은 믿지 않는다 — 경계를 넘어 읽지 않는다. ----
static long decode_stream(const Dict &d, const std::vector<unsigned char> &in, std::string &out) {
    (void)d;
    (void)in;
    (void)out;
    return -1;   // TODO: 헤더/해시 확인 → 프레임마다 사전으로 인자 길이 계산(경계 검사) → 텍스트
}

// ============================== 테스트 하네스 ==============================
#ifndef NLOG_NO_MAIN   // demo_*.cpp 가 이 파일을 #include 해서 로거만 쓸 때
static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define FAILS() (g_total - g_pass)
static void skip(int n, const char *what) {
    g_total += n;
    std::printf("SKIP %d threaded checks (%s): unit checks failed\n", n, what);
}
#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define NLOG_SANITIZER 1
#endif
#endif
#ifdef NLOG_SANITIZER
static constexpr int kScale = 10;   // 새니타이저 빌드는 스트레스 양을 1/10 로 (20 초 안에 끝나게)
#else
static constexpr int kScale = 1;
#endif

static std::uint64_t g_fake = 0;
static std::uint64_t fake_clock() { return ++g_fake; }   // 단일 스레드 테스트 전용: 1, 2, 3, ...
static Logger::Config cfg_fake(std::uint64_t ring, Policy p = Policy::Drop) {
    Logger::Config c;
    c.ring_bytes = ring;
    c.policy = p;
    c.clock = fake_clock;
    return c;
}
static std::string expect_line(std::uint64_t ts, char lvl, const std::string &msg) {
    return "[" + std::to_string(ts) + "] " + lvl + " " + msg + "\n";
}

// 같은 호출 묶음을 텍스트 경로와 바이너리 경로에 모두 쓰려고 함수로 둔다 (Part A / Part C 공용).
static int g_dummy_obj = 0;
static void mixed_calls(Logger &lg) {
    NLOG(lg, INF, "boot");
    NLOG(lg, INF, "a=%d b=%u c=%lld d=%x", -5, 7u, 1LL << 40, 255);
    NLOG(lg, WRN, "pi=%.3f tiny=%g sci=%e", 3.14159, 1e-5, -2.5);
    NLOG(lg, INF, "s=%s|%-6s|%.2s", "hello", "ab", "xyz");
    NLOG(lg, ERR, "100%% done, ch=%c sz=%zu i64=%" PRId64 " u8=%hhu", 'A', sizeof(RecHdr), INT64_MIN, 300);
    NLOG(lg, INF, "p=%p", static_cast<void *>(&g_dummy_obj));
}
static std::string mixed_expected(std::uint64_t ts0) {
    char p[64];
    std::snprintf(p, sizeof p, "p=%p", static_cast<void *>(&g_dummy_obj));
    return expect_line(ts0 + 0, 'I', "boot") +
           expect_line(ts0 + 1, 'I', "a=-5 b=7 c=1099511627776 d=ff") +
           expect_line(ts0 + 2, 'W', "pi=3.142 tiny=1e-05 sci=-2.500000e+00") +
           expect_line(ts0 + 3, 'I', "s=hello|ab    |xy") +
           expect_line(ts0 + 4, 'E', "100% done, ch=A sz=16 i64=-9223372036854775808 u8=44") +
           expect_line(ts0 + 5, 'I', p);
}

static void test_part_a() {
    std::printf("-- Part A: 사이트 등록 + 인코딩 + SPSC 바이트 링 + 포맷팅 --\n");
    // 레지스트리 (주어진 코드: 처음부터 통과)
    CHECK(register_site("x=%d y=%s", "t", 1, INF, KindList<int, const char *>{}) < BAD_SITE);
    CHECK(register_site("x=%d", "t", 2, INF, KindList<double>{}) == BAD_SITE);          // 종류 불일치
    CHECK(register_site("x=%d %d", "t", 3, INF, KindList<int>{}) == BAD_SITE);          // 개수 불일치
    CHECK(register_site("w=%*d", "t", 4, INF, KindList<int, int>{}) == BAD_SITE);       // '*' 미지원

    // 인자 크기 규칙
    CHECK(arg_bytes(42) == 8u && arg_bytes(2.5) == 8u && arg_bytes("abc") == 5u && arg_bytes(std::string(300, 'z')) == 202u &&
          arg_bytes(std::string_view("abcd")) == 6u);
    unsigned char tmp[16] = {};
    CHECK(put_arg(tmp, "hi") == tmp + 4 && rd16(tmp) == 2u && tmp[2] == 'h' && tmp[3] == 'i');
    CHECK(put_arg(tmp, -1) == tmp + 8 && rd64(tmp) == ~std::uint64_t{0});

    // 링 단위: 예약 → commit 전에는 소비자가 못 본다 → commit 후 본다
    {
        StagingRing r(256);
        unsigned char *p = r.reserve(64);
        CHECK(p == r.buf.get() && r.tail.load() == 0u);
        RecHdr h{64, 7, 0, 99};
        if (p) std::memcpy(p, &h, sizeof h);
        std::size_t seen = 0;
        CHECK(r.consume([&](const RecHdr &, const unsigned char *) { seen++; }, 10) == 0u);
        r.commit();
        std::uint64_t ts_seen = 0;
        CHECK(r.consume([&](const RecHdr &x, const unsigned char *) { seen++; ts_seen = x.ts; }, 10) == 1u &&
              seen == 1u && ts_seen == 99u && r.head.load() == 64u);
        // 192 B 까지 차 있으면 거절 (cap 256): 64 쓰고 아직 소비 안 한 상태에서 3 개 더 = 256 → 그 다음 실패
        int ok = 0;
        for (int i = 0; i < 5; i++) {
            unsigned char *q = r.reserve(64);
            if (!q) break;
            RecHdr hh{64, 1, 0, 0};
            std::memcpy(q, &hh, sizeof hh);
            r.commit();
            ok++;
        }
        CHECK(ok == 4 && r.tail.load() - r.head.load() == 256u);
    }
    // 끝에서 감싸기: off 224 에 64 B → 32 B 패드 + 0 에 64 B
    {
        StagingRing r(256);
        for (int i = 0; i < 7; i++) {        // 7 × 32 B = 224
            unsigned char *q = r.reserve(32);
            if (!q) break;
            RecHdr hh{32, 1, 0, static_cast<std::uint64_t>(i)};
            std::memcpy(q, &hh, sizeof hh);
            r.commit();
        }
        r.consume([](const RecHdr &, const unsigned char *) {}, 100);
        unsigned char *q = r.reserve(64);
        CHECK(q == r.buf.get() && r.pads.load() == 1u);
        if (q) { RecHdr hh{64, 2, 0, 77}; std::memcpy(q, &hh, sizeof hh); r.commit(); }
        std::uint64_t got = 0;
        std::size_t k = r.consume([&](const RecHdr &x, const unsigned char *) { got = x.ts; }, 100);
        CHECK(k == 1u && got == 77u && r.tail.load() == 224u + 32u + 64u && r.head.load() == r.tail.load());
    }

    // 로거 전체: 정확한 텍스트
    {
        TextSink sink;
        Logger lg(cfg_fake(1u << 12), sink);
        g_fake = 999;
        mixed_calls(lg);
        CHECK(lg.poll() == 6u);
        CHECK(sink.text == mixed_expected(1000));
        if (sink.text != mixed_expected(1000)) std::printf("  got:\n%s  want:\n%s", sink.text.c_str(), mixed_expected(1000).c_str());
        CHECK(lg.nrings() == 1u && lg.ring_at(0).head.load() == lg.ring_at(0).tail.load());
    }
    // 레코드 크기: 헤더 16 + int 8 + double 8 + "abc"(2+3) = 37 → 40
    {
        TextSink sink;
        Logger lg(cfg_fake(1u << 12), sink);
        NLOG(lg, INF, "%d %f %s", 1, 2.0, "abc");
        CHECK(lg.my_ring().tail.load() == 40u);
        const std::string longs(300, 'q');
        NLOG(lg, INF, "long=%s", longs.c_str());             // 200 B 로 잘린다
        lg.poll();
        CHECK(sink.text.size() > 20 && sink.text.find("long=" + std::string(200, 'q') + "\n") != std::string::npos &&
              sink.text.find(std::string(201, 'q')) == std::string::npos);
    }
    // 작은 링에서 여러 번 감싸기: 50 개, 3 개마다 poll → 전부 순서대로, 패드 발생
    {
        TextSink sink;
        Logger lg(cfg_fake(256), sink);
        g_fake = 0;
        std::string want;
        for (int i = 0; i < 50; i++) {
            NLOG(lg, INF, "i=%d name=%s", i, i % 2 ? "odd" : "even-number");
            want += expect_line(static_cast<std::uint64_t>(i + 1), 'I', "i=" + std::to_string(i) + " name=" + (i % 2 ? "odd" : "even-number"));
            if (i % 3 == 2) lg.poll();
        }
        lg.poll();
        CHECK(sink.text == want);
        CHECK(lg.my_ring().pads.load() == 3u && lg.my_ring().dropped.load() == 0u);
    }
    // 레벨 필터: 낮은 레벨은 링에 아무것도 안 쓴다 (인자 평가도 안 함)
    {
        TextSink sink;
        Logger lg(cfg_fake(1u << 12), sink);
        lg.min_level = WRN;
        int evals = 0;
        auto side = [&evals] { return ++evals; };
        NLOG(lg, INF, "hidden %d", side());
        NLOG(lg, ERR, "shown %d", side());
        lg.poll();
        CHECK(evals == 1 && sink.text.find("hidden") == std::string::npos && sink.text.find("shown 1") != std::string::npos);
    }
}

static void producer_n(Logger &lg, int t, int n) {
    for (int i = 0; i < n; i++) NLOG(lg, INF, "t=%d i=%d", t, i);
}

static void test_part_b(bool unit_ok) {
    std::printf("-- Part B: 가득 참 정책(drop/block) + 문자열 수명 + 백그라운드 스레드 --\n");
    // DROP: 1024 B 링, 레코드 24 B (헤더 16 + int 8) → 42 개 (1008 B) 들어가고 나머지 58 개는 정확히 drop
    {
        TextSink sink;
        Logger lg(cfg_fake(1024), sink);
        g_fake = 0;
        int accepted = 0;
        for (int i = 0; i < 100; i++) {
            const std::uint16_t id = []{ static const std::uint16_t s = register_site("n=%d", __FILE__, __LINE__, INF, KindList<int>{}); return s; }();
            accepted += lg.log(id, i) ? 1 : 0;
        }
        CHECK(accepted == 42 && lg.my_ring().dropped.load() == 58u);
        CHECK(lg.poll() == 42u);
        NLOG(lg, INF, "after %d", 1);
        NLOG(lg, INF, "after %d", 2);
        lg.poll();
        // 마커는 빈자리 바로 다음, 다음 레코드 앞에 나온다. ts = 마커를 쓴 호출의 시각 (101)
        const std::string tail_want = expect_line(42, 'I', "n=41") + expect_line(101, 'W', "nlog: dropped 58 messages") +
                                      expect_line(101, 'I', "after 1") + expect_line(102, 'I', "after 2");
        CHECK(sink.text.size() >= tail_want.size() && sink.text.compare(sink.text.size() - tail_want.size(), tail_want.size(), tail_want) == 0);
        CHECK(lg.my_ring().pending_drops == 0u && lg.my_ring().dropped.load() == 58u);
    }
    // 문자열 수명: 포인터 저장 로거는 소비 시점의 내용을 찍는다 (틀림), 복사 로거는 호출 시점 내용 (맞음)
    {
        char buf[16];
        std::strcpy(buf, "first");
        PtrLogger bad;
        bad.log(1, "s=%s", buf);
        TextSink sink;
        Logger lg(cfg_fake(1u << 12), sink);
        g_fake = 0;
        NLOG(lg, INF, "s=%s", buf);
        std::strcpy(buf, "SECOND");                          // 호출한 뒤, 소비 전에 버퍼가 바뀐다
        lg.poll();
        CHECK(bad.drain() == "[1] I s=SECOND\n");            // 주어진 깨진 로거: 바뀐 내용이 찍힌다
        CHECK(sink.text == "[1] I s=first\n");              // 복사했으니 호출 시점 내용
    }
    if (!unit_ok) { skip(6, "Part B block/stress"); return; }
    // BLOCK (강제): 소비자 없음 → 생산자는 42 개 뒤에 멈춘다 → main 이 소비하면 100 개 전부, 손실 0
    {
        TextSink sink;
        Logger::Config c;
        c.ring_bytes = 1024;
        c.policy = Policy::Block;
        c.block_timeout = std::chrono::seconds(10);
        Logger lg(c, sink);
        std::thread prod([&] { for (int i = 0; i < 100; i++) NLOG(lg, INF, "b=%d", i); });
        const auto t0 = std::chrono::steady_clock::now();
        while ((lg.nrings() == 0 || lg.ring_at(0).stalls.load() == 0) && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3))
            std::this_thread::yield();
        const bool stalled = lg.nrings() == 1 && lg.ring_at(0).stalls.load() >= 1;
        CHECK(stalled && lg.ring_at(0).tail.load() == 42u * 24u);     // 정확히 42 개에서 멈춰 있다
        auto lines = [&] { return std::count(sink.text.begin(), sink.text.end(), '\n'); };
        while (stalled && lines() < 100 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10)) {
            lg.poll();                                                 // 이제 main 이 소비자
            std::this_thread::yield();
        }
        prod.join();
        lg.poll();
        std::string want;
        for (int i = 0; i < 100; i++) want += "b=" + std::to_string(i) + "\n";
        std::string got;
        for (std::size_t p = 0; p < sink.text.size();) {          // ts 를 떼고 메시지만 비교 (실제 시계)
            const std::size_t e = sink.text.find('\n', p);
            const std::size_t m = sink.text.find("] I ", p);
            if (e == std::string::npos || m == std::string::npos || m > e) break;
            got += sink.text.substr(m + 4, e + 1 - (m + 4));
            p = e + 1;
        }
        CHECK(got == want && lg.ring_at(0).dropped.load() == 0u && lg.ring_at(0).timeouts.load() == 0u);
    }
    // BLOCK 상한: 소비자가 죽어 있으면 timeout 후 drop 으로 바뀐다 (영원히 막히지 않는다)
    {
        TextSink sink;
        Logger::Config c = cfg_fake(1024, Policy::Block);
        c.block_timeout = std::chrono::milliseconds(20);
        Logger lg(c, sink);
        int ok = 0;
        for (int i = 0; i < 43; i++) ok += lg.log([]{ static const std::uint16_t s = register_site("k=%d", __FILE__, __LINE__, INF, KindList<int>{}); return s; }(), i) ? 1 : 0;
        CHECK(ok == 42 && lg.my_ring().timeouts.load() == 1u && lg.my_ring().dropped.load() == 1u && lg.my_ring().stalls.load() == 1u);
    }
    // 백그라운드 소비자 + 생산자 4 개, 작은 링(4 KB) BLOCK: 모두 정확히 한 번, 스레드별 순서 유지
    {
        TextSink sink;
        Logger::Config c;
        c.ring_bytes = 4096;
        c.policy = Policy::Block;
        c.block_timeout = std::chrono::seconds(10);
        Logger lg(c, sink);
        lg.start();
        const int T = 4, N = 20000 / kScale;
        std::vector<std::thread> th;
        for (int t = 0; t < T; t++) th.emplace_back(producer_n, std::ref(lg), t, N);
        for (auto &x : th) x.join();
        lg.stop();
        std::vector<int> next(T, 0);
        long lines = 0, bad_order = 0, bad_parse = 0;
        for (std::size_t p = 0; p < sink.text.size();) {
            const std::size_t e = sink.text.find('\n', p);
            if (e == std::string::npos) break;
            int t = -1, i = -1;
            const std::size_t m = sink.text.find("t=", p);
            if (m == std::string::npos || m > e || std::sscanf(sink.text.c_str() + m, "t=%d i=%d", &t, &i) != 2 || t < 0 || t >= T) bad_parse++;
            else if (i != next[t]++) bad_order++;
            lines++;
            p = e + 1;
        }
        std::uint64_t drops = 0;
        for (std::uint32_t i = 0; i < lg.nrings(); i++) drops += lg.ring_at(i).dropped.load();
        CHECK(lines == static_cast<long>(T) * N && bad_parse == 0 && bad_order == 0);
        CHECK(next[0] == N && next[1] == N && next[2] == N && next[3] == N && drops == 0u && lg.nrings() == 4u);
        std::uint64_t stalls = 0;
        for (std::uint32_t i = 0; i < lg.nrings(); i++) stalls += lg.ring_at(i).stalls.load();
        std::printf("  [stress] %d x %d msgs, ring 4 KB: lines %ld, stalls %" PRIu64 " (출력만)\n", T, N, lines, stalls);
    }
    // 한 스레드가 로거 두 개에 번갈아 쓰기: thread_local 캐시가 섞이지 않는다
    {
        TextSink s1, s2;
        Logger l1(cfg_fake(1u << 12), s1), l2(cfg_fake(1u << 12), s2);
        g_fake = 0;
        for (int i = 0; i < 4; i++) { NLOG(l1, INF, "one %d", i); NLOG(l2, INF, "two %d", i); }
        l1.poll();
        l2.poll();
        CHECK(s1.text.find("two") == std::string::npos && s2.text.find("one") == std::string::npos &&
              s1.text.find("one 3") != std::string::npos && s2.text.find("two 3") != std::string::npos);
    }
}

static void test_part_c() {
    std::printf("-- Part C: 바이너리 프레임 + 사전 + 오프라인 디코더 --\n");
    BinarySink bin;
    Logger lg(cfg_fake(1u << 12), bin);
    g_fake = 999;
    mixed_calls(lg);                    // 사이트가 처음 쓰였다면 여기서 등록된다 → 그 뒤에 사전을 뽑는다
    const Dict d = export_dict();
    bin.begin(d);
    lg.poll();
    CHECK(bin.frames == 6u);
    std::string text;
    CHECK(decode_stream(d, bin.bytes, text) == 6);
    CHECK(text == mixed_expected(1000));                     // 오프라인 디코드 == 텍스트 경로
    // 크기: 프레임 = 10 B + 인자. "boot" = 10, "a=%d b=%u c=%lld d=%x" = 10 + 32 = 42
    const std::size_t text_bytes = mixed_expected(1000).size();
    CHECK(bin.bytes.size() == 8u + 10u + 42u + 34u + (10u + 7u + 4u + 5u) + 42u + 18u);
    std::printf("  [size] binary %zu B vs text %zu B (사전 %zu 사이트는 호스트에만)\n", bin.bytes.size(), text_bytes, d.size());
    // 사전이 다르면 (다른 빌드) 거절
    Dict d2 = d;
    d2.back().fmt += "!";
    std::string t2;
    CHECK(decode_stream(d2, bin.bytes, t2) == -1 && t2.empty());
    // 잘린 스트림: 앞의 완전한 프레임은 살리고 -2
    std::vector<unsigned char> cut(bin.bytes.begin(), bin.bytes.end() - 5);
    std::string t3;
    CHECK(decode_stream(d, cut, t3) == -2 && t3 == mixed_expected(1000).substr(0, mixed_expected(1000).rfind("[1005]")));
    // 깨진 문자열 길이 (0xFFFF) → 경계 밖을 읽지 않고 -2
    std::vector<unsigned char> bad = bin.bytes;
    const std::size_t s_frame = 8u + 10u + 42u + 34u;      // 4 번째 프레임 시작 (문자열 3 개짜리)
    std::string t4;
    if (bad.size() > s_frame + 11) { bad[s_frame + 10] = 0xFF; bad[s_frame + 11] = 0xFF; }
    CHECK(bad.size() > s_frame + 11 && decode_stream(d, bad, t4) == -2);
}

int main() {
    test_part_a();
    const bool unit_ok = FAILS() == 0;
    test_part_b(unit_ok);
    test_part_c();
    std::printf("%d/%d passed\n", g_pass, g_total);
    return FAILS() ? 1 : 0;
}
#endif   // NLOG_NO_MAIN
