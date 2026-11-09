// ROS-M03 Executor & callback group — rclcpp 실행 모델을 std::thread 로 다시 만든 미니 executor
//
// 주의: ROS 2 는 이 맥에도 Lima VM(Ubuntu 26.04)에도 설치돼 있지 않다. 이 파일은 rclcpp 가 아니라 그 의미론을
// 흉내 낸 모델이다. rclcpp 의 Jazzy 소스(executor.cpp, multi_threaded_executor.cpp)와 문서를 읽고 옮겼고,
// 실제 rclcpp 코드는 레슨 §5 "ROS 2가 있을 때"에 있으며 실행해 보지 않았다.
//
//   Part A  스케줄링 규칙: 콜백 그룹 토큰(can_be_taken_from), 타이머 → 서비스 → 응답 스캔, 타이머 합쳐짐(coalesce)
//   Part B  타이머 콜백 안의 동기 서비스 호출: 6칸 매트릭스, 대기 그래프(wait-for) 보고, 절대 멈추지 않는 bounded wait
//   Part C  고치기: 그룹 분리 + MultiThreaded, async 호출 + continuation, 스레드 고갈(N > 동시 blocking 수)
//
// 판정은 결정적이다. 멈춤은 시간 제한이 아니라 구조(누가 진행 가능한가)로 판단하고, 시간 제한은 안전망이다.
// 타이밍 숫자는 CHECK 에 넣지 않는다 (demo_matrix.cpp 참고).
//
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread solution.cpp -o so && ./so   (starter.cpp 도 같은 명령)
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
using ms = std::chrono::milliseconds;

// ======================= 모델 타입 =======================
enum class GroupType { MutuallyExclusive, Reentrant };

struct Group {
    std::string name;
    GroupType type;
    bool taken = false;      // rclcpp 의 can_be_taken_from == false 에 해당. MutuallyExclusive 만 씀
    std::string holder;      // 지금 토큰을 쥔 콜백 라벨 (보고용)
};

enum class Kind { Timer, Service, Response };
struct Work {
    Kind kind = Kind::Timer;
    int id = -1;             // Timer: timer id, Service/Response: call id
    Group *group = nullptr;
    std::string label;
};

enum class CallStatus { Ok, Timeout, Stuck, Stopped };
enum Why : unsigned { WhyNone = 0, WhyGroup = 1, WhyThread = 2 };

struct CallResult {
    CallStatus status = CallStatus::Stopped;
    int response = 0;
    unsigned why = WhyNone;
    std::string report;
};

using Continuation = std::function<void(int)>;

[[maybe_unused]] static const char *status_name(CallStatus s) {
    switch (s) {
    case CallStatus::Ok: return "OK";
    case CallStatus::Timeout: return "TIMEOUT";
    case CallStatus::Stuck: return "STUCK";
    default: return "STOPPED";
    }
}

[[maybe_unused]] static std::string why_name(unsigned w) {
    if (w == (WhyGroup | WhyThread)) return "GROUP|THREAD";
    if (w == WhyGroup) return "GROUP";
    if (w == WhyThread) return "THREAD";
    return "-";
}

thread_local int tls_tid = -1;   // 작업 스레드 번호. executor 밖 스레드는 -1

class Executor {
public:
    explicit Executor(bool detect = true) : detect_(detect) {}

    // ---- 구성: spin 전에 만든다 (spin 중에는 구조가 안 바뀐다) ----
    Group *create_group(std::string name, GroupType type) {
        groups_.push_back(Group{std::move(name), type, false, ""});
        return &groups_.back();
    }
    int create_timer(std::string name, Group *g, std::function<void()> cb) {
        timers_.push_back(Timer{static_cast<int>(timers_.size()), std::move(name), g, std::move(cb), false});
        return timers_.back().id;
    }
    // g == nullptr 이면 executor 밖 서버(다른 프로세스 흉내): 스레드도 그룹도 쓰지 않고 응답만 돌아온다.
    int create_service(std::string name, Group *g, std::function<int(int)> handler) {
        services_.push_back(Service{std::move(name), g, std::move(handler)});
        return static_cast<int>(services_.size()) - 1;
    }
    int create_client(std::string name, Group *g, int service) {
        clients_.push_back(Client{std::move(name), g, service});
        return static_cast<int>(clients_.size()) - 1;
    }

    // ---- 이벤트 입력 ----
    // 타이머는 큐가 아니라 "ready 깃발" 하나다. 아직 안 가져간 타이머를 또 트리거하면 합쳐진다.
    void trigger_timer(int id) {
        std::lock_guard<std::mutex> lk(mu_);
        if (timers_.at(id).ready) coalesced_++;
        timers_.at(id).ready = true;
        cv_.notify_all();
    }
    void set_hold_external(bool on) { std::lock_guard<std::mutex> lk(mu_); hold_external_ = on; }
    void set_keep_alive(bool on) { std::lock_guard<std::mutex> lk(mu_); keep_alive_ = on; cv_.notify_all(); }

    // 붙잡아 둔 외부 응답을 전부 도착시킨다 (네트워크 지연 흉내).
    void deliver_held() {
        std::lock_guard<std::mutex> lk(mu_);
        for (int id : held_) {
            Call &c = calls_.at(id);
            in_flight_--;
            deliver_response_locked(c, services_.at(clients_.at(c.client).service).handler(c.request));
        }
        held_.clear();
        cv_.notify_all();
    }

    // 스레드 n 개가 동기 호출에 막힐 때까지 기다린다 (테스트용, 시간 제한 있음).
    bool wait_blocked(int n, ms budget) {
        std::unique_lock<std::mutex> lk(mu_);
        return cv_.wait_until(lk, Clock::now() + budget, [&] { return blocked_ >= n; });
    }

    // ---- 서비스 호출 ----
    CallResult call_sync(int client, int request, ms budget);
    void async_send(int client, int request, Continuation on_response);

    // 스레드 nthreads 개로 일이 다 끝날 때까지 (또는 budget 까지) 돈다. Single = 1, Multi = 2 이상.
    void spin(int nthreads, ms budget) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            nthreads_ = nthreads;
            slots_.assign(static_cast<size_t>(nthreads), Slot{});
            stop_ = false;
        }
        std::vector<std::thread> pool;
        for (int i = 0; i < nthreads; i++) pool.emplace_back([this, i] { worker(i); });
        {
            std::unique_lock<std::mutex> lk(mu_);
            spin_budget_hit_ = !cv_.wait_until(lk, Clock::now() + budget, [&] { return quiescent_locked(); });
            stop_ = true;
            cv_.notify_all();
        }
        for (auto &t : pool) t.join();
    }

    int coalesced() { std::lock_guard<std::mutex> lk(mu_); return coalesced_; }
    int orphans() { std::lock_guard<std::mutex> lk(mu_); return orphans_; }
    bool spin_budget_hit() { std::lock_guard<std::mutex> lk(mu_); return spin_budget_hit_; }

private:
    struct Timer { int id; std::string name; Group *group; std::function<void()> cb; bool ready; };
    struct Service { std::string name; Group *group; std::function<int(int)> handler; };
    struct Client { std::string name; Group *group; int service; };
    struct Call {
        int id = -1, client = -1, request = 0, response = 0;
        bool completed = false;   // 응답 콜백이 실행됨
        bool abandoned = false;   // 기다리던 쪽이 포기함: 늦은 응답은 버린다
        Continuation cont;        // async 호출의 이어서 할 일
    };
    struct Slot {
        bool busy = false, blocked = false;
        int waiting_call = -1;
        std::string label;
        Group *group = nullptr;
    };

    static bool can_take(const Group *g) { return g->type == GroupType::Reentrant || !g->taken; }

    Group *service_group_locked(const Call &c) const { return services_.at(clients_.at(c.client).service).group; }
    Group *client_group_locked(const Call &c) const { return clients_.at(c.client).group; }

    // 지금 하나라도 가져갈 수 있는 일이 있나 (상태를 바꾸지 않는다)
    bool any_takeable_locked() const {
        for (const Timer &t : timers_) if (t.ready && can_take(t.group)) return true;
        for (int id : service_q_) if (can_take(service_group_locked(calls_.at(id)))) return true;
        for (int id : response_q_) if (can_take(client_group_locked(calls_.at(id)))) return true;
        return false;
    }

    bool quiescent_locked() const {
        if (keep_alive_ || in_flight_ > 0 || busy_ > 0) return false;
        for (const Timer &t : timers_) if (t.ready) return false;
        return service_q_.empty() && response_q_.empty();
    }

    void deliver_response_locked(Call &c, int response) {
        c.response = response;
        response_q_.push_back(c.id);
        cv_.notify_all();
    }

    // 요청을 보낸다. 서버가 executor 안이면 service_q_ 로, 밖이면 곧장 응답(또는 붙잡아 둠).
    int send_locked(int client, int request, Continuation cont) {
        const int id = next_call_++;
        Call &c = calls_[id];
        c.id = id; c.client = client; c.request = request; c.cont = std::move(cont);
        const Service &s = services_.at(clients_.at(client).service);
        if (s.group != nullptr) service_q_.push_back(id);
        else if (hold_external_) { held_.push_back(id); in_flight_++; }
        else deliver_response_locked(c, s.handler(request));   // 외부 서버 흉내: handler 는 짧은 순수 함수
        cv_.notify_all();
        return id;
    }

    // A1. 실행할 다음 일을 고른다. mu_ 를 쥔 채 호출된다.
    //  - 순서: timers_ (ready 인 것, 생성 순) → service_q_ → response_q_ (Jazzy executor.cpp 의 스캔 순서를 줄인 것)
    //  - 그룹을 가져갈 수 없는(can_take false) 항목은 건너뛰고 다음 항목을 본다. 그룹 하나가 막혔다고 전체가 멈추면 안 된다.
    //  - 골랐으면: 타이머 ready 를 내리거나 큐에서 지우고, MutuallyExclusive 면 g->taken = true, g->holder = label
    //  - out 을 채우고 true. 없으면 false.
    //  - label: 타이머는 "timer:<이름>", 서비스는 "service:call#<id>", 응답은 "response:call#<id>"
    bool take_next_ready_locked(Work &out) {
        // TODO: 스타터는 그룹을 전혀 안 본다. 우선 돌아가게만 해 둔 FIFO
        for (Timer &t : timers_) {
            if (t.ready) { t.ready = false; out = Work{Kind::Timer, t.id, t.group, "timer:" + t.name}; return true; }
        }
        if (!service_q_.empty()) {
            int id = service_q_.front(); service_q_.pop_front();
            out = Work{Kind::Service, id, service_group_locked(calls_.at(id)), "service:call#" + std::to_string(id)};
            return true;
        }
        if (!response_q_.empty()) {
            int id = response_q_.front(); response_q_.pop_front();
            out = Work{Kind::Response, id, client_group_locked(calls_.at(id)), "response:call#" + std::to_string(id)};
            return true;
        }
        return false;
    }

    // A2. 콜백이 끝났다. MutuallyExclusive 그룹이면 토큰을 돌려준다 (taken=false, holder 비움). mu_ 를 쥔 채 호출된다.
    void release_group_locked(const Work &w) {
        (void)w;  // TODO
    }

    // B2. executor 전체가 더 이상 진행할 수 없는가? mu_ 를 쥔 채 호출된다. 아래가 모두 참이면 true:
    //  ① blocked_ > 0 (누군가 동기 호출에 막혀 있다)  ② in_flight_ == 0 (네트워크 어딘가에서 응답이 오고 있지 않다)
    //  ③ busy 이면서 blocked 가 아닌 스레드가 없다 (달리는 콜백이 있으면 그 콜백이 뭔가 풀어 줄 수 있다)
    //  ④ 쉬는 스레드(nthreads_ - busy_)가 0 이거나, 가져갈 수 있는 일이 없다 (any_takeable_locked())
    bool is_stuck_locked() const {
        return false;  // TODO
    }

    // B3. call c 의 다음 단계를 누가 막고 있나: 필요한 그룹이 잡혀 있으면 WhyGroup, 쉬는 스레드가 없으면 WhyThread (둘 다 가능).
    //     다음 단계에 필요한 그룹 = 요청이 service_q_ 에 있으면 서버 그룹, 그 밖에는 클라이언트 그룹.
    unsigned why_locked(const Call &c) const {
        (void)c;  // TODO
        return WhyNone;
    }

    // B4. 사람이 읽을 대기 그래프 보고. CHECK 가 아래 조각을 찾는다 (형식 그대로):
    //   "T0 BLOCKED timer:tick call#0"              막힌 스레드마다 한 줄 (T<번호> BLOCKED <콜백 라벨> call#<id>)
    //   "WaitsFor: call#0 needs group:<이름>"        다음 단계가 필요한 그룹. 요청 단계면 "request needs" 를 앞에 붙임
    //   "cycle: timer:tick -> call#0 -> group:<이름> -> <holder>"     WhyGroup 일 때
    //   "cycle: timer:tick -> call#0 -> thread-slot -> timer:tick"    WhyThread 일 때 (둘 다면 두 줄)
    std::string report_locked(const Call &c, unsigned why) const {
        (void)c; (void)why;  // TODO
        return "";
    }

    void execute(const Work &w) {
        switch (w.kind) {
        case Kind::Timer:
            timers_.at(w.id).cb();   // timers_ 의 cb 는 spin 중 읽기 전용이다
            break;
        case Kind::Service: {
            int req;
            const std::function<int(int)> *handler;
            {
                std::lock_guard<std::mutex> lk(mu_);
                const Call &c = calls_.at(w.id);
                req = c.request;
                handler = &services_.at(clients_.at(c.client).service).handler;
            }
            int resp = (*handler)(req);
            std::lock_guard<std::mutex> lk(mu_);
            deliver_response_locked(calls_.at(w.id), resp);
            break;
        }
        case Kind::Response: {
            Continuation cont;
            int resp = 0;
            {
                std::lock_guard<std::mutex> lk(mu_);
                Call &c = calls_.at(w.id);
                if (c.abandoned) { orphans_++; break; }   // 늦은 응답: 기다리는 쪽이 이미 포기했다
                c.completed = true;
                resp = c.response;
                cont = c.cont;
                cv_.notify_all();
            }
            if (cont) cont(resp);
            break;
        }
        }
    }

    void worker(int tid) {
        tls_tid = tid;
        std::unique_lock<std::mutex> lk(mu_);
        while (!stop_) {
            Work w;
            if (take_next_ready_locked(w)) {
                Slot &s = slots_[static_cast<size_t>(tid)];
                s.busy = true; s.label = w.label; s.group = w.group;
                busy_++;
                lk.unlock();
                execute(w);
                lk.lock();
                release_group_locked(w);
                s.busy = false; s.label.clear(); s.group = nullptr;
                busy_--;
                cv_.notify_all();
            } else {
                cv_.wait(lk);   // post/반납/stop 은 전부 mu_ 안에서 notify_all 한다
            }
        }
    }

    std::mutex mu_;
    std::condition_variable cv_;
    [[maybe_unused]] bool detect_;   // 스타터에서 아직 안 쓰는 동안 경고 방지
    std::deque<Group> groups_;       // 포인터가 안 움직이도록 deque
    std::vector<Timer> timers_;
    std::vector<Service> services_;
    std::vector<Client> clients_;
    std::map<int, Call> calls_;
    std::deque<int> service_q_, response_q_;
    std::vector<int> held_;
    std::vector<Slot> slots_;
    int next_call_ = 0, nthreads_ = 0, busy_ = 0, blocked_ = 0, in_flight_ = 0, coalesced_ = 0, orphans_ = 0;
    bool stop_ = false, keep_alive_ = false, hold_external_ = false, spin_budget_hit_ = false;
};

// B1. 콜백(또는 executor 밖 스레드)에서 쓰는 동기 호출. rclcpp 라면 async_send_request + future.wait_for.
//  - send_locked 로 보낸다 (continuation 은 nullptr)
//  - 작업 스레드면 (tls_tid >= 0) 슬롯에 blocked=true, waiting_call=id, blocked_++ 후 cv_.notify_all() (막힌 것도 상태 변화)
//  - 끝날 때까지 기다린다. 매번 이 순서로 검사: completed → Ok (response 채움) / stop_ → Stopped /
//    detect_ && is_stuck_locked() → Stuck (why, report 채움) / 시간 초과 → Timeout (why, report 채움)
//  - Ok 가 아니면 calls_ 의 abandoned=true (늦은 응답을 안전하게 버린다). 블록 표시를 되돌리고 cv_.notify_all()
CallResult Executor::call_sync(int client, int request, ms budget) {
    (void)client; (void)request; (void)budget;  // TODO
    return CallResult{CallStatus::Timeout, 0, WhyNone, ""};
}

// C1. 막지 않는 호출. 보내기만 하고 바로 돌아온다. 응답 콜백이 on_response(resp) 를 executor 스레드에서 부른다.
void Executor::async_send(int client, int request, Continuation on_response) {
    (void)client; (void)request; (void)on_response;  // TODO
}

// C2. 타이머에서 동기 호출을 해도 MultiThreaded 에서 안 멈추는 그룹 배치 (타이머용, 클라이언트용).
struct NodeSetup { Group *timer_group; Group *client_group; };
[[maybe_unused]] static NodeSetup fixed_groups(Executor &ex) {
    Group *g = ex.create_group("node_default", GroupType::MutuallyExclusive);  // TODO: 한 그룹이면 고장난 구성이다
    return NodeSetup{g, g};
}

// C3. 비동기 호출을 3번 이어서 (반복문의 sync call 을 대신): v0=start 로 client 를 부르고, 그 응답 v1 로 또 부르고,
//     그 응답 v2 로 또 부른다. 세 번째 응답을 done 에 넘긴다. 어디서도 기다리지 않는다.
[[maybe_unused]] static void async_chain(Executor &ex, int client, int start, std::function<void(int)> done) {
    (void)ex; (void)client; (void)start; (void)done;  // TODO
}

// ======================= 하네스 (starter 와 동일) =======================
#ifndef ROS_M03_NO_MAIN
static int g_pass = 0, g_fail = 0;
#define CHECK(expr) do { \
    if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
    else { g_fail++; printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } \
} while (0)
#endif

[[maybe_unused]] static const ms kSpinBudget{8000};
[[maybe_unused]] static const ms kCallBudget{4000};

// 두 콜백이 동시에 안에 있었는지를 확인한다. 시간 제한이 있어서 절대 안 멈춘다.
class Rendezvous {
public:
    explicit Rendezvous(int need) : need_(need) {}
    bool meet(ms budget) {
        std::unique_lock<std::mutex> lk(m_);
        inside_++;
        if (inside_ >= need_) met_ = true;
        cv_.notify_all();
        cv_.wait_until(lk, Clock::now() + budget, [&] { return met_; });
        inside_--;
        return met_;
    }
    bool met() { std::lock_guard<std::mutex> lk(m_); return met_; }
private:
    std::mutex m_;
    std::condition_variable cv_;
    int need_, inside_ = 0;
    bool met_ = false;
};

enum class Arrange { SameMutEx, SameReentrant, Separate };

[[maybe_unused]] static const char *arrange_name(Arrange a) {
    switch (a) {
    case Arrange::SameMutEx: return "same MutuallyExclusive";
    case Arrange::SameReentrant: return "same Reentrant";
    default: return "separate groups";
    }
}

struct Cell { CallResult r; int orphans = 0; bool budget_hit = false; int ran = 0; };

// 한 칸: 타이머 콜백 안에서 동기 서비스 호출 한 번. 서버는 executor 밖 (다른 프로세스 흉내).
[[maybe_unused]] static Cell run_cell(int nthreads, Arrange arr, bool detect = true, ms budget = kCallBudget) {
    Executor ex(detect);
    Group *a = ex.create_group("node_default",
                               arr == Arrange::SameReentrant ? GroupType::Reentrant : GroupType::MutuallyExclusive);
    Group *b = arr == Arrange::Separate ? ex.create_group("client_cb", GroupType::MutuallyExclusive) : a;
    int svc = ex.create_service("add", nullptr, [](int x) { return x + 1; });
    int cli = ex.create_client("cli", b, svc);
    Cell cell;
    int t = ex.create_timer("tick", a, [&] { cell.r = ex.call_sync(cli, 41, budget); cell.ran++; });
    ex.trigger_timer(t);
    ex.spin(nthreads, kSpinBudget);
    cell.orphans = ex.orphans();
    cell.budget_hit = ex.spin_budget_hit();
    return cell;
}

#ifndef ROS_M03_NO_MAIN
static bool has(const std::string &s, const char *needle) { return s.find(needle) != std::string::npos; }

static void test_part_a() {
    printf("\n== Part A: 스케줄링 규칙 ==\n");
    {   // 같은 MutuallyExclusive: 스레드가 2개여도 동시에 안 돈다 (강제로 만나려 해도 못 만난다)
        Executor ex;
        Group *g = ex.create_group("g", GroupType::MutuallyExclusive);
        Rendezvous rv(2);
        int ran = 0;
        auto cb = [&] { rv.meet(ms(150)); ran++; };
        int t1 = ex.create_timer("t1", g, cb), t2 = ex.create_timer("t2", g, cb);
        ex.trigger_timer(t1); ex.trigger_timer(t2);
        ex.spin(2, kSpinBudget);
        CHECK(!rv.met());
        CHECK(ran == 2);
    }
    {   // 같은 Reentrant + 스레드 2개: 둘이 반드시 만난다
        Executor ex;
        Group *g = ex.create_group("g", GroupType::Reentrant);
        Rendezvous rv(2);
        auto cb = [&] { rv.meet(ms(4000)); };
        int t1 = ex.create_timer("t1", g, cb), t2 = ex.create_timer("t2", g, cb);
        ex.trigger_timer(t1); ex.trigger_timer(t2);
        ex.spin(2, kSpinBudget);
        CHECK(rv.met());
    }
    {   // 서로 다른 MutuallyExclusive 그룹: 동시에 돌 수 있다
        Executor ex;
        Group *a = ex.create_group("a", GroupType::MutuallyExclusive);
        Group *b = ex.create_group("b", GroupType::MutuallyExclusive);
        Rendezvous rv(2);
        auto cb = [&] { rv.meet(ms(4000)); };
        int t1 = ex.create_timer("t1", a, cb), t2 = ex.create_timer("t2", b, cb);
        ex.trigger_timer(t1); ex.trigger_timer(t2);
        ex.spin(2, kSpinBudget);
        CHECK(rv.met());
    }
    {   // Reentrant 여도 스레드가 1개면 동시에 못 돈다
        Executor ex;
        Group *g = ex.create_group("g", GroupType::Reentrant);
        Rendezvous rv(2);
        auto cb = [&] { rv.meet(ms(150)); };
        int t1 = ex.create_timer("t1", g, cb), t2 = ex.create_timer("t2", g, cb);
        ex.trigger_timer(t1); ex.trigger_timer(t2);
        ex.spin(1, kSpinBudget);
        CHECK(!rv.met());
    }
    {   // 타이머는 ready 깃발: spin 전 3번 트리거 → 1번 실행, 2번 합쳐짐
        Executor ex;
        Group *g = ex.create_group("g", GroupType::MutuallyExclusive);
        int ran = 0;
        int t = ex.create_timer("t", g, [&] { ran++; });
        ex.trigger_timer(t); ex.trigger_timer(t); ex.trigger_timer(t);
        ex.spin(1, kSpinBudget);
        CHECK(ran == 1);
        CHECK(ex.coalesced() == 2);
    }
    {   // 이 모델의 스캔 순서: 타이머 → 서비스 → 응답 (spin 전에 전부 준비, 스레드 1개)
        Executor ex;
        Group *g = ex.create_group("g", GroupType::Reentrant);
        std::string order;
        int svc = ex.create_service("svc", g, [&](int x) { order += "S"; return x; });
        int cli = ex.create_client("cli", g, svc);
        ex.async_send(cli, 1, [&](int) { order += "R"; });
        int t = ex.create_timer("t", g, [&] { order += "T"; });
        ex.trigger_timer(t);
        ex.spin(1, kSpinBudget);
        CHECK(order == "TSR");
    }
}

static void test_part_b() {
    printf("\n== Part B: 타이머 콜백 안의 동기 호출 매트릭스 ==\n");
    struct Row { int threads; Arrange arr; CallStatus want; unsigned why; };
    const Row rows[] = {
        {1, Arrange::SameMutEx, CallStatus::Stuck, WhyGroup | WhyThread},
        {1, Arrange::SameReentrant, CallStatus::Stuck, WhyThread},
        {1, Arrange::Separate, CallStatus::Stuck, WhyThread},
        {2, Arrange::SameMutEx, CallStatus::Stuck, WhyGroup},
        {2, Arrange::SameReentrant, CallStatus::Ok, WhyNone},
        {2, Arrange::Separate, CallStatus::Ok, WhyNone},
    };
    printf("%-22s %-24s %-8s %s\n", "executor", "timer/client group", "result", "why");
    for (const Row &r : rows) {
        Cell c = run_cell(r.threads, r.arr);
        std::string ex_name = r.threads == 1 ? "SingleThreaded" : "MultiThreaded(" + std::to_string(r.threads) + ")";
        printf("%-22s %-24s %-8s %s\n", ex_name.c_str(), arrange_name(r.arr), status_name(c.r.status), why_name(c.r.why).c_str());
        CHECK(c.r.status == r.want);
        CHECK(c.r.why == r.why);
        CHECK(c.ran == 1);                   // 콜백은 돌아왔다: 안 멈췄다
        CHECK(!c.budget_hit);
        CHECK(c.orphans == (r.want == CallStatus::Stuck ? 1 : 0));   // 늦은 응답은 정확히 1번, 버려짐
        if (r.want == CallStatus::Ok) CHECK(c.r.response == 42);
    }
    {   // 스레드를 늘려도 그룹이 같으면 그대로다
        Cell c = run_cell(8, Arrange::SameMutEx);
        CHECK(c.r.status == CallStatus::Stuck);
        CHECK(c.r.why == WhyGroup);
    }
    {   // 대기 그래프 보고의 내용
        Cell c = run_cell(2, Arrange::SameMutEx);
        printf("--- 보고 (Multi(2), same MutuallyExclusive) ---\n%s", c.r.report.c_str());
        CHECK(has(c.r.report, " BLOCKED timer:tick call#0"));
        CHECK(has(c.r.report, "WaitsFor: call#0 needs group:node_default"));
        CHECK(has(c.r.report, "cycle: timer:tick -> call#0 -> group:node_default -> timer:tick"));
        Cell d = run_cell(1, Arrange::SameReentrant);
        printf("--- 보고 (Single, same Reentrant) ---\n%s", d.r.report.c_str());
        CHECK(has(d.r.report, "cycle: timer:tick -> call#0 -> thread-slot -> timer:tick"));
    }
    {   // 탐지기를 끄면 시간 제한이 대신 끊는다 (실제 rclcpp 에서 쓸 수 있는 건 이쪽뿐)
        Cell c = run_cell(1, Arrange::SameMutEx, false, ms(50));
        CHECK(c.r.status == CallStatus::Timeout);
        CHECK(has(c.r.report, "cycle: timer:tick -> call#0 -> group:node_default -> timer:tick"));
    }
    {   // 응답이 오는 중이면 멈춤으로 판정하면 안 된다: 응답을 붙잡았다가 막힌 뒤에 풀어 준다
        Executor ex;
        Group *a = ex.create_group("a", GroupType::MutuallyExclusive);
        Group *b = ex.create_group("b", GroupType::MutuallyExclusive);
        int svc = ex.create_service("add", nullptr, [](int x) { return x + 1; });
        int cli = ex.create_client("cli", b, svc);
        CallResult r;
        int t = ex.create_timer("tick", a, [&] { r = ex.call_sync(cli, 1, kCallBudget); });
        ex.set_hold_external(true);
        ex.trigger_timer(t);
        std::thread net([&] { ex.wait_blocked(1, ms(3000)); ex.deliver_held(); });
        ex.spin(2, kSpinBudget);
        net.join();
        CHECK(r.status == CallStatus::Ok && r.response == 2);
    }
    {   // 같은 시나리오를 Single 에서: 응답이 도착하는 순간 멈춤 확정
        Executor ex;
        Group *a = ex.create_group("a", GroupType::MutuallyExclusive);
        int svc = ex.create_service("add", nullptr, [](int x) { return x + 1; });
        int cli = ex.create_client("cli", a, svc);
        CallResult r;
        int t = ex.create_timer("tick", a, [&] { r = ex.call_sync(cli, 1, kCallBudget); });
        ex.set_hold_external(true);
        ex.trigger_timer(t);
        std::thread net([&] { ex.wait_blocked(1, ms(3000)); ex.deliver_held(); });
        ex.spin(1, kSpinBudget);
        net.join();
        CHECK(r.status == CallStatus::Stuck);
        CHECK(r.why == (WhyGroup | WhyThread));
    }
    {   // 서버가 같은 노드·같은 그룹이면 요청 단계에서 막힌다
        Executor ex;
        Group *a = ex.create_group("node_default", GroupType::MutuallyExclusive);
        int svc = ex.create_service("add", a, [](int x) { return x + 1; });
        int cli = ex.create_client("cli", a, svc);
        CallResult r;
        int t = ex.create_timer("tick", a, [&] { r = ex.call_sync(cli, 1, kCallBudget); });
        ex.trigger_timer(t);
        ex.spin(2, kSpinBudget);
        CHECK(r.status == CallStatus::Stuck);
        CHECK(has(r.report, "request needs call#0 needs group:node_default"));
    }
    {   // executor 밖 스레드에서의 동기 호출은 괜찮다
        Executor ex;
        Group *a = ex.create_group("node_default", GroupType::MutuallyExclusive);
        int svc = ex.create_service("add", nullptr, [](int x) { return x + 1; });
        int cli = ex.create_client("cli", a, svc);
        ex.set_keep_alive(true);
        CallResult r;
        std::thread caller([&] { r = ex.call_sync(cli, 1, kCallBudget); ex.set_keep_alive(false); });
        ex.spin(1, kSpinBudget);
        caller.join();
        CHECK(r.status == CallStatus::Ok && r.response == 2);
    }
}

static void test_part_c() {
    printf("\n== Part C: 고치기 ==\n");
    {   // 그룹 분리 + MultiThreaded: 응답은 다른 스레드에서 돈다
        Executor ex;
        NodeSetup n = fixed_groups(ex);
        int svc = ex.create_service("add", nullptr, [](int x) { return x + 1; });
        int cli = ex.create_client("cli", n.client_group, svc);
        CallResult r;
        int t = ex.create_timer("tick", n.timer_group, [&] { r = ex.call_sync(cli, 1, kCallBudget); });
        ex.trigger_timer(t);
        ex.spin(2, kSpinBudget);
        CHECK(n.timer_group != n.client_group);
        CHECK(r.status == CallStatus::Ok && r.response == 2);
    }
    {   // async: 6칸 전부 통과. 콜백은 먼저 끝나고 그다음에 응답이 돈다
        const int threads[] = {1, 2};
        const Arrange arrs[] = {Arrange::SameMutEx, Arrange::SameReentrant, Arrange::Separate};
        int ok = 0;
        for (int n : threads) for (Arrange arr : arrs) {
            Executor ex;
            Group *a = ex.create_group("a", arr == Arrange::SameReentrant ? GroupType::Reentrant : GroupType::MutuallyExclusive);
            Group *b = arr == Arrange::Separate ? ex.create_group("b", GroupType::MutuallyExclusive) : a;
            int svc = ex.create_service("add", nullptr, [](int x) { return x + 1; });
            int cli = ex.create_client("cli", b, svc);
            std::mutex m;
            int got = 0;
            int t = ex.create_timer("tick", a, [&] {
                ex.async_send(cli, 41, [&](int v) { std::lock_guard<std::mutex> lk(m); got = v; });
            });
            ex.trigger_timer(t);
            ex.spin(n, kSpinBudget);
            if (got == 42) ok++;
        }
        CHECK(ok == 6);
    }
    {   // Single + 같은 그룹: 타이머가 끝난 "뒤에" 응답이 돈다 (스레드 1개라 순서가 정해져 있다)
        Executor ex;
        Group *a = ex.create_group("a", GroupType::MutuallyExclusive);
        int svc = ex.create_service("add", nullptr, [](int x) { return x + 1; });
        int cli = ex.create_client("cli", a, svc);
        std::string order;
        int t = ex.create_timer("tick", a, [&] { ex.async_send(cli, 41, [&](int) { order += "R"; }); order += "T"; });
        ex.trigger_timer(t);
        ex.spin(1, kSpinBudget);
        CHECK(order == "TR");
    }
    {   // async 를 3번 잇기: Single 에서도 끝까지 간다
        Executor ex;
        Group *a = ex.create_group("a", GroupType::MutuallyExclusive);
        int svc = ex.create_service("add", nullptr, [](int x) { return x * 2; });
        int cli = ex.create_client("cli", a, svc);
        int result = 0;
        int t = ex.create_timer("tick", a, [&] { async_chain(ex, cli, 5, [&](int v) { result = v; }); });
        ex.trigger_timer(t);
        ex.spin(1, kSpinBudget);
        CHECK(result == 40);
    }
    {   // 스레드 고갈: Reentrant 라도 동시에 막힌 콜백 수만큼 스레드가 차면 응답을 돌릴 스레드가 없다
        Executor ex;
        Group *g = ex.create_group("re", GroupType::Reentrant);
        int svc = ex.create_service("add", nullptr, [](int x) { return x + 1; });
        int cli = ex.create_client("cli", g, svc);
        std::mutex m;
        int stuck = 0, thread_why = 0, oks = 0;
        auto cb = [&] {
            CallResult r = ex.call_sync(cli, 1, kCallBudget);
            std::lock_guard<std::mutex> lk(m);
            if (r.status == CallStatus::Stuck) { stuck++; if (r.why == WhyThread) thread_why++; }
            if (r.status == CallStatus::Ok) oks++;
        };
        int t1 = ex.create_timer("t1", g, cb), t2 = ex.create_timer("t2", g, cb);
        ex.trigger_timer(t1); ex.trigger_timer(t2);
        ex.spin(2, kSpinBudget);
        CHECK(stuck == 1);             // 하나가 포기하면 그 스레드가 풀려서 나머지 응답을 처리한다
        CHECK(thread_why == 1);
        CHECK(oks == 1);
    }
    {   // 스레드 3개면 응답을 돌릴 자리가 남는다
        Executor ex;
        Group *g = ex.create_group("re", GroupType::Reentrant);
        int svc = ex.create_service("add", nullptr, [](int x) { return x + 1; });
        int cli = ex.create_client("cli", g, svc);
        std::mutex m;
        int oks = 0;
        auto cb = [&] {
            CallResult r = ex.call_sync(cli, 1, kCallBudget);
            std::lock_guard<std::mutex> lk(m);
            if (r.status == CallStatus::Ok) oks++;
        };
        int t1 = ex.create_timer("t1", g, cb), t2 = ex.create_timer("t2", g, cb);
        ex.trigger_timer(t1); ex.trigger_timer(t2);
        ex.spin(3, kSpinBudget);
        CHECK(oks == 2);
    }
}

int main() {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("\n%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
#endif
