// ROS-M03 추가 연습 — 한 번에 검증 (본문 코드와 독립)
//   연습 1  정적 판정(lint): 실행하지 않고 구성만 보고 멈춤을 예측, 실측 매트릭스와 대조
//   연습 2  대기 그래프에서 순환 찾기
//   연습 3  rcl 타이머의 다음 호출 시각 규칙: 밀린 주기는 건너뛰고 위상은 유지
//   연습 4  동시 blocking K개, 호출 깊이 d 일 때 필요한 최소 스레드 수 (DFS 로 공식 검증)
//   연습 5  BoundedPromise: 시간 제한 있는 대기 + 늦은 응답을 안전하게 버리기 (진짜 std::thread)
//
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread practice.cpp -o pr && ./pr
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
using ms = std::chrono::milliseconds;

static int g_pass = 0, g_fail = 0;
#define CHECK(expr) do { \
    if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
    else { g_fail++; printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } \
} while (0)

// ---- 연습 1: 정적 판정 ----
enum class Arrange { SameMutEx, SameReentrant, Separate };
enum Why : unsigned { WhyNone = 0, WhyGroup = 1, WhyThread = 2 };
struct Prediction { bool wedge; unsigned why; };

static Prediction predict(int threads, Arrange a) {
    unsigned why = WhyNone;
    if (a == Arrange::SameMutEx) why |= WhyGroup;  // 응답이 쓸 그룹을 타이머가 쥐고 있다
    if (threads <= 1) why |= WhyThread;            // 타이머가 유일한 스레드를 쥐고 있다
    return Prediction{why != WhyNone, why};
}

// ---- 연습 2: 대기 그래프 순환 (없으면 "") ----
using Graph = std::map<std::string, std::vector<std::string>>;

static bool dfs(const Graph &g, const std::string &u, std::set<std::string> &done,
                std::vector<std::string> &path, std::string &out) {
    for (size_t i = 0; i < path.size(); i++) {
        if (path[i] == u) {
            for (size_t j = i; j < path.size(); j++) out += path[j] + " -> ";
            out += u;
            return true;
        }
    }
    if (done.count(u)) return false;
    path.push_back(u);
    auto it = g.find(u);
    if (it != g.end())
        for (const std::string &v : it->second)
            if (dfs(g, v, done, path, out)) return true;
    path.pop_back();
    done.insert(u);
    return false;
}

static std::string find_cycle(const Graph &g) {
    std::set<std::string> done;
    for (const auto &kv : g) {
        std::vector<std::string> path;
        std::string out;
        if (dfs(g, kv.first, done, path, out)) return out;
    }
    return "";
}

// ---- 연습 3: rcl_timer_call 의 다음 호출 시각 규칙 (단위 ms, 가상 시간) ----
static long next_call_after(long next_call, long now, long period) {
    next_call += period;                 // 이전 예정 시각 기준으로 한 주기
    if (next_call <= now) {
        long ahead = now - next_call;
        next_call += (1 + ahead / period) * period;   // 놓친 주기는 한꺼번에 건너뛴다
    }
    return next_call;
}

// 타이머 하나, 콜백 duration ms, 스레드 하나. 시작 시각 목록을 돌려준다.
static std::vector<long> timer_starts(long period, long duration, long horizon) {
    std::vector<long> starts;
    long next = period, free_at = 0;
    for (;;) {
        long now = next > free_at ? next : free_at;
        if (now > horizon) break;
        starts.push_back(now);
        next = next_call_after(next, now, period);
        free_at = now + duration;
    }
    return starts;
}

// ---- 연습 4: 최소 스레드 수 ----
// 각 체인은 깊이 d 까지 내려가며 단계마다 스레드 하나를 쥐고 막힌다. 맨 아래 응답을 처리하려면 스레드가 하나 더 필요하다.
static bool can_wedge(int threads, int chains, int depth, std::vector<int> &held, int used) {
    if (used == threads) return true;                 // 스레드가 다 찼다 → 아무도 더 못 내려간다
    for (int i = 0; i < chains; i++) {
        if (held[static_cast<size_t>(i)] < depth) {
            held[static_cast<size_t>(i)]++;
            bool w = can_wedge(threads, chains, depth, held, used + 1);
            held[static_cast<size_t>(i)]--;
            if (w) return true;
        }
    }
    return false;   // 전부 맨 아래인데 스레드가 남으면 그 스레드가 응답을 처리해서 풀린다 → 멈춤 아님
}
static int min_threads(int chains, int depth) { return chains * depth + 1; }

// ---- 연습 5: BoundedPromise ----
enum class Waited { Ok, Timeout };
class BoundedPromise {
    struct State {
        std::mutex m;
        std::condition_variable cv;
        bool has = false, abandoned = false;
        int value = 0, late = 0;
    };
    std::shared_ptr<State> s_ = std::make_shared<State>();
public:
    bool set(int v) const {   // 호출 스레드는 기다리는 쪽보다 오래 살 수 있다
        std::lock_guard<std::mutex> lk(s_->m);
        if (s_->abandoned) { s_->late++; return false; }
        s_->value = v;
        s_->has = true;
        s_->cv.notify_all();
        return true;
    }
    Waited wait(ms budget, int &out) const {
        std::unique_lock<std::mutex> lk(s_->m);
        s_->cv.wait_until(lk, Clock::now() + budget, [&] { return s_->has; });
        if (!s_->has) { s_->abandoned = true; return Waited::Timeout; }
        out = s_->value;
        return Waited::Ok;
    }
    int late() const { std::lock_guard<std::mutex> lk(s_->m); return s_->late; }
};

int main() {
    printf("== 연습 1: 정적 판정 vs 실측 매트릭스 ==\n");
    struct Row { int t; Arrange a; bool wedge; unsigned why; };
    // solution.cpp 실행 결과를 그대로 옮긴 기댓값
    const Row rows[] = {{1, Arrange::SameMutEx, true, WhyGroup | WhyThread}, {1, Arrange::SameReentrant, true, WhyThread},
                        {1, Arrange::Separate, true, WhyThread},             {2, Arrange::SameMutEx, true, WhyGroup},
                        {2, Arrange::SameReentrant, false, WhyNone},         {2, Arrange::Separate, false, WhyNone},
                        {8, Arrange::SameMutEx, true, WhyGroup}};
    for (const Row &r : rows) {
        Prediction p = predict(r.t, r.a);
        CHECK(p.wedge == r.wedge && p.why == r.why);
    }

    printf("== 연습 2: 대기 그래프 순환 ==\n");
    Graph g1{{"timer:tick", {"call#0"}}, {"call#0", {"group:cb"}}, {"group:cb", {"timer:tick"}}};
    CHECK(find_cycle(g1) == "call#0 -> group:cb -> timer:tick -> call#0");
    Graph g2{{"a", {"b"}}, {"b", {"c"}}, {"c", {}}};
    CHECK(find_cycle(g2).empty());
    Graph g3{{"x", {"a"}}, {"a", {"b"}}, {"b", {"a"}}};
    CHECK(find_cycle(g3) == "a -> b -> a");

    printf("== 연습 3: 타이머 다음 호출 시각 ==\n");
    CHECK(next_call_after(10, 10, 10) == 20);
    CHECK(next_call_after(20, 35, 10) == 40);   // 30 은 이미 지났으니 건너뛴다
    CHECK(next_call_after(40, 60, 10) == 70);   // now 가 예정 시각과 같으면 그것도 지나간 것
    CHECK(timer_starts(10, 1, 50) == (std::vector<long>{10, 20, 30, 40, 50}));
    CHECK(timer_starts(10, 25, 120) == (std::vector<long>{10, 35, 60, 85, 110}));

    printf("== 연습 4: 최소 스레드 ==\n");
    for (int k = 1; k <= 3; k++) {
        for (int d = 1; d <= 3; d++) {
            int need = min_threads(k, d);
            std::vector<int> h1(static_cast<size_t>(k), 0), h2(static_cast<size_t>(k), 0);
            CHECK(can_wedge(need - 1, k, d, h1, 0));    // 하나 모자라면 멈출 수 있는 순서가 있다
            CHECK(!can_wedge(need, k, d, h2, 0));       // 이만큼 있으면 어떤 순서로도 안 멈춘다
        }
    }
    CHECK(min_threads(1, 1) == 2);   // 본문 Part B: 타이머 하나 + 응답 처리 스레드
    CHECK(min_threads(2, 1) == 3);   // 본문 Part C: 타이머 둘 + 응답 처리 스레드

    printf("== 연습 5: BoundedPromise ==\n");
    {   // 먼저 온 응답: 기다리지 않고 받는다
        BoundedPromise p;
        CHECK(p.set(7));
        int v = 0;
        CHECK(p.wait(ms(1000), v) == Waited::Ok && v == 7);
    }
    {   // 시간 초과 후에 도착한 응답은 버린다 (순서는 코드로 고정: wait 가 끝난 뒤 set)
        BoundedPromise p;
        int v = -1;
        CHECK(p.wait(ms(5), v) == Waited::Timeout);
        std::thread late([p] { (void)p.set(9); });
        late.join();
        CHECK(v == -1);
        CHECK(p.late() == 1);
    }
    {   // 다른 스레드가 값을 넣어 준다 (시간 제한은 넉넉하게)
        BoundedPromise p;
        std::thread t([p] { (void)p.set(11); });
        int v = 0;
        CHECK(p.wait(ms(4000), v) == Waited::Ok && v == 11);
        t.join();
    }

    printf("\n%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
