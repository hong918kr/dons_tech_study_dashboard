// CPP-M08 클래스 전용 operator new + 정적 메모리 풀
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
//
// 목표: `new Message(...)` 문법은 그대로 두고 저장소만 정적 풀로 돌린다. 힙 없음, O(1), 고갈 시 nullptr.
// free list 알고리즘 자체는 DS-M02 소유 — 여기서는 C++ 언어 쪽 연결(hook)이 주제다.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

// ---------------- 가짜 인터럽트 마스크 (실제 칩: PRIMASK 저장/복원, CPP-B06 참고) ----------------
namespace irq {
inline int      depth = 0;          // 0 이면 인터럽트 허용 상태
inline unsigned enters = 0;
inline unsigned violations = 0;     // 임계 구역 밖에서 풀을 건드린 횟수
}  // namespace irq

class CriticalSection {
public:
    CriticalSection() noexcept { ++irq::depth; ++irq::enters; }
    ~CriticalSection() { --irq::depth; }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
};

enum class FreeResult { Ok, Null, OutOfRange, Interior, DoubleFree };

// ---------------- 고정 블록 풀 (알고리즘은 주어짐: DS-M02 참고) ----------------
// 메타데이터는 블록 밖(next_, live_)에 둔다 → 해제 후 객체에 쓰는 버그가 있어도 free list 는 안 깨진다.
// constexpr 생성자 + 전부 0 초기화 → constinit 가능 (동적 초기화 없음, CPP-B09 참고).
template <std::size_t BlockSize, std::size_t BlockAlign, std::uint16_t N>
class StaticPool {
    static_assert(BlockSize % BlockAlign == 0, "블록 크기는 정렬의 배수여야 배열로 이어 붙일 수 있다");
    static_assert(N > 0 && N < 0xFFFFu);
    static constexpr std::uint16_t kNil = 0xFFFFu;

public:
    constexpr StaticPool() = default;

    void* alloc() noexcept {
        if (irq::depth == 0) ++irq::violations;
        std::uint16_t i;
        if (head_ != kNil)   { i = head_; head_ = next_[i]; }   // 재사용: LIFO
        else if (fresh_ < N) { i = fresh_++; }                  // 아직 한 번도 안 쓴 블록
        else                 { ++alloc_failures_; return nullptr; }
        live_[i] = true;
        if (++used_ > high_water_) high_water_ = used_;
        return &storage_[i * BlockSize];
    }

    // Part B: 검증 후 반납. 잘못된 포인터면 상태를 전혀 바꾸지 않고 이유를 돌려준다.
    //   nullptr → Null (카운트 없음) / 범위 밖 → OutOfRange / 블록 경계 아님 → Interior / 이미 빈 블록 → DoubleFree
    //   Ok 인 경우에만: live_ 해제, used_ 감소, 블록을 0xDD 로 채움(포이즌), next_/head_ 로 free list 앞에 넣기
    FreeResult release(void* p) noexcept {
        if (irq::depth == 0) ++irq::violations;
        if (p == nullptr) return FreeResult::Null;
        // 서로 다른 배열의 포인터를 < 로 비교하는 건 결과가 미지정 → 정수로 바꿔 비교한다
        const auto a = reinterpret_cast<std::uintptr_t>(p);
        const auto b = reinterpret_cast<std::uintptr_t>(storage_);
        if (a < b || a - b >= N * BlockSize) { ++bad_frees_; return FreeResult::OutOfRange; }
        const std::size_t off = a - b;
        if (off % BlockSize != 0) { ++bad_frees_; return FreeResult::Interior; }
        const auto i = static_cast<std::uint16_t>(off / BlockSize);
        if (!live_[i]) { ++double_frees_; return FreeResult::DoubleFree; }
        live_[i] = false;
        --used_;
        std::memset(&storage_[off], 0xDD, BlockSize);
        next_[i] = head_;
        head_ = i;
        return FreeResult::Ok;
    }

    bool contains(const void* p) const noexcept {
        const auto a = reinterpret_cast<std::uintptr_t>(p);
        const auto b = reinterpret_cast<std::uintptr_t>(storage_);
        return a >= b && a - b < N * BlockSize;
    }
    std::byte block_byte(std::uint16_t i, std::size_t off) const noexcept { return storage_[i * BlockSize + off]; }
    std::uint16_t index_of(const void* p) const noexcept {
        return static_cast<std::uint16_t>((reinterpret_cast<std::uintptr_t>(p) -
                                           reinterpret_cast<std::uintptr_t>(storage_)) / BlockSize);
    }

    std::uint16_t used() const noexcept { return used_; }
    std::uint16_t high_water() const noexcept { return high_water_; }
    unsigned alloc_failures() const noexcept { return alloc_failures_; }
    unsigned double_frees() const noexcept { return double_frees_; }
    unsigned bad_frees() const noexcept { return bad_frees_; }

private:
    alignas(BlockAlign) std::byte storage_[N * BlockSize]{};
    std::uint16_t next_[N]{};
    bool          live_[N]{};
    std::uint16_t head_ = kNil;
    std::uint16_t fresh_ = 0;
    std::uint16_t used_ = 0;
    std::uint16_t high_water_ = 0;
    unsigned      alloc_failures_ = 0;
    unsigned      double_frees_ = 0;
    unsigned      bad_frees_ = 0;
};

// ---------------- Message: new/delete 를 풀로 돌리는 클래스 ----------------
class Message {
public:
    Message(std::uint32_t id_, std::uint8_t source_) noexcept : id(id_), source(source_) { ++ctor_count; }
    ~Message() { ++dtor_count; }

    std::uint32_t id;
    std::uint8_t  source;
    std::uint8_t  len = 0;
    std::uint8_t  payload[22]{};

    static inline unsigned ctor_count = 0;
    static inline unsigned dtor_count = 0;
    static inline unsigned size_rejects = 0;      // 파생 클래스가 블록보다 커서 거절한 횟수
    static inline unsigned wrong_size_frees = 0;  // sized delete 로 알아챈 크기 불일치

    // 선언과 정의의 예외 명세는 같아야 한다.
    static void* operator new(std::size_t sz) noexcept;
    static void* operator new(std::size_t sz, const std::nothrow_t&) noexcept;
    static void  operator delete(void* p, std::size_t sz) noexcept;

    // Part B: 배열 new 는 위의 operator new 로 오지 않는다. 그냥 두면 조용히 전역 힙으로 간다.
    static void* operator new[](std::size_t) = delete;
    static void  operator delete[](void*)    = delete;
};

inline constexpr std::uint16_t kBlocks = 4;
inline constinit StaticPool<sizeof(Message), alignof(Message), kBlocks> g_pool;   // Message 가 완전해진 뒤에야 sizeof 가능

// Part A/B: 임계 구역 안에서 g_pool 에서 꺼낸다. 고갈이면 nullptr.
//           sz 가 블록보다 크면(큰 파생 클래스) 풀을 건드리지 않고 Message::size_rejects++ 후 nullptr.
void* Message::operator new(std::size_t sz) noexcept {
    if (sz > sizeof(Message)) { ++size_rejects; return nullptr; }
    CriticalSection cs;
    return g_pool.alloc();
}

// Part B: new (std::nothrow) Message(...) 용. 클래스에 operator new 가 있으면 전역 nothrow 판은 가려진다.
void* Message::operator new(std::size_t sz, const std::nothrow_t&) noexcept {
    return Message::operator new(sz);
}

// Part A/B: nullptr 은 아무 일도 안 한다. sz 가 sizeof(Message) 보다 크면 wrong_size_frees++ 후 반납하지 않는다.
//           그 외에는 임계 구역 안에서 g_pool.release(p).
void Message::operator delete(void* p, std::size_t sz) noexcept {
    if (p == nullptr) return;
    if (sz > sizeof(Message)) { ++wrong_size_frees; return; }
    CriticalSection cs;
    (void)g_pool.release(p);
}

// Part B용 파생 클래스들: operator new/delete 를 그대로 상속한다
struct SameSizeMessage : Message { using Message::Message; };
struct BigMessage : Message {
    using Message::Message;
    std::uint8_t extra[64]{};
};

// sized delete 관찰용: virtual 소멸자가 있으면 동적 타입의 크기가 넘어온다 (주어진 코드)
inline constinit StaticPool<32, 8, 2> g_vpool;
struct VBase {
    virtual ~VBase() = default;
    std::uint32_t x = 1;
    static inline std::size_t last_delete_size = 0;
    static void* operator new(std::size_t) noexcept { CriticalSection cs; return g_vpool.alloc(); }
    static void  operator delete(void* p, std::size_t sz) noexcept {
        last_delete_size = sz;
        CriticalSection cs;
        (void)g_vpool.release(p);
    }
};
struct VDerived : VBase { std::uint32_t y[3]{}; };

// 배열 가드가 없는 클래스: new T[n] 이 풀로 가지 않는다는 걸 보여 준다 (주어진 코드)
inline constinit StaticPool<8, 8, 2> g_npool;
struct NoArrayGuard {
    std::uint64_t v = 0;
    static void* operator new(std::size_t) noexcept { CriticalSection cs; return g_npool.alloc(); }
    static void  operator delete(void* p) noexcept { CriticalSection cs; (void)g_npool.release(p); }
};

// ---------------- Part C: ISR → 링 → 메인 루프 ----------------
namespace rx {
struct Ring {                                  // 풀 블록 수와 같은 용량 → 할당에 성공하면 자리는 반드시 있다
    Message* slot[kBlocks]{};
    unsigned head = 0, tail = 0, count = 0;
    void push(Message* m) noexcept { slot[tail] = m; tail = (tail + 1) % kBlocks; ++count; }
    Message* pop() noexcept {
        if (count == 0) return nullptr;
        Message* m = slot[head];
        head = (head + 1) % kBlocks;
        --count;
        return m;
    }
};
inline Ring          ring;
inline unsigned      received = 0, dropped = 0, processed = 0;
inline std::uint32_t sum = 0;
inline std::uint32_t next_id = 1;

// UART 수신 ISR 이라고 치자: Message 를 만들고 payload[0] = byte, len = 1 로 채워 링에 넣는다.
//   received 는 항상 +1. 풀 고갈이면 dropped +1 하고 끝 (블로킹·abort 금지).
inline void uart_rx_isr(std::uint8_t byte) noexcept {
    ++received;
    Message* m = new (std::nothrow) Message(next_id++, 1);
    if (m == nullptr) { ++dropped; return; }
    m->payload[0] = byte;
    m->len = 1;
    ring.push(m);
}

// 메인 루프 1회: 링에서 하나 꺼내 sum += payload[0], processed +1, delete. 처리했으면 true.
inline bool main_loop_once() noexcept {
    Message* m = ring.pop();
    if (m == nullptr) return false;
    sum += m->payload[0];
    ++processed;
    delete m;
    return true;
}
}  // namespace rx

// ---------------- 테스트 하네스 ----------------
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

// requires-expression 으로 "이 new 식이 컴파일되는가?"를 bool 로 얻는다
// (주의: `new T[2]` 그대로 쓰면 기본 생성자가 없는 Message 는 가드와 무관하게 false 가 된다 → 인자로 원소를 직접 만든다)
template <class T, class... A> constexpr bool can_array_new = requires(A... a) { new T[2]{T(a...), T(a...)}; };
template <class T> constexpr bool can_placement_new = requires(void* b) { new (b) T(0u, std::uint8_t{0}); };
template <class T> constexpr bool can_global_placement_new = requires(void* b) { ::new (b) T(0u, std::uint8_t{0}); };

static std::uint32_t xorshift32(std::uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

static auto addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

int main() {
    // ---- Part A: new 가 풀에서 나오고, 고갈되면 nullptr, 지우면 재사용 ----
    CHECK(noexcept(Message::operator new(sizeof(Message))));     // 핵심: noexcept 여야 new-식이 null 검사를 한다
    CHECK(sizeof(Message) == 28u && alignof(Message) == 4u);

    unsigned c0 = Message::ctor_count, d0 = Message::dtor_count, f0 = g_pool.alloc_failures();
    Message* a[kBlocks + 1] = {};
    for (unsigned i = 0; i < kBlocks; ++i) a[i] = new Message(100 + i, 7);
    bool all_in_pool = true, all_aligned = true;
    for (unsigned i = 0; i < kBlocks; ++i) {
        all_in_pool = all_in_pool && a[i] != nullptr && g_pool.contains(a[i]);
        all_aligned = all_aligned && addr(a[i]) % alignof(Message) == 0;
    }
    CHECK(all_in_pool);
    CHECK(all_aligned);
    CHECK(a[0] != a[1] && a[1] != a[2] && a[2] != a[3] && a[0] != a[3]);
    CHECK(addr(a[1]) - addr(a[0]) == sizeof(Message));            // 블록이 빈틈없이 이어진다
    CHECK(g_pool.used() == kBlocks);
    CHECK(Message::ctor_count == c0 + kBlocks);
    CHECK(a[0] && a[0]->id == 100u && a[3] && a[3]->id == 103u);

    a[4] = new Message(999, 9);                                    // 다섯 번째: 고갈
    CHECK(a[4] == nullptr);
    CHECK(Message::ctor_count == c0 + kBlocks);                    // 생성자가 돌지 않았다!
    CHECK(g_pool.alloc_failures() == f0 + 1);
    CHECK(g_pool.used() == kBlocks);

    const auto slot1 = addr(a[1]);                                 // delete 뒤의 포인터 값은 쓰지 않으려고 정수로 저장
    if (a[2]) a[2]->payload[0] = 0x5A;
    delete a[1];
    a[1] = nullptr;
    CHECK(Message::dtor_count == d0 + 1);
    CHECK(g_pool.used() == kBlocks - 1);
    CHECK(a[2] && a[2]->payload[0] == 0x5A);                       // 옆 블록은 그대로
    Message* again = new Message(200, 1);
    CHECK(again != nullptr && addr(again) == slot1);               // LIFO 재사용
    CHECK(again && again->id == 200u && again->len == 0u);         // 생성자가 다시 초기화했다
    a[1] = again;

    for (auto*& m : a) { delete m; m = nullptr; }
    CHECK(g_pool.used() == 0u);
    CHECK(g_pool.high_water() == kBlocks);
    CHECK(Message::ctor_count == Message::dtor_count);

    Message* none = nullptr;
    unsigned df0 = g_pool.double_frees(), bf0 = g_pool.bad_frees();
    delete none;                                                   // delete nullptr: 안전해야 한다
    Message::operator delete(nullptr, sizeof(Message));            // operator delete 를 직접 null 로 불러도
    CHECK(g_pool.double_frees() == df0 && g_pool.bad_frees() == bf0);

    // ---- Part B: 언어 쪽 함정 — nothrow 판, 이름 가림, 배열, 파생 ----
    Message* nt = new (std::nothrow) Message(300, 2);
    CHECK(nt != nullptr && g_pool.contains(nt) && nt->id == 300u);
    delete nt;
    CHECK(g_pool.used() == 0u);

    CHECK((!can_array_new<Message, std::uint32_t, std::uint8_t>));                                // new Message[2] 는 컴파일 에러여야 한다
    CHECK(can_array_new<NoArrayGuard>);                            // 가드가 없으면 컴파일된다
    CHECK(!can_placement_new<Message>);                            // 클래스 operator new 가 전역 placement 를 가린다
    CHECK(can_global_placement_new<Message>);                      // ::new (buf) 로 쓰면 된다

    auto* arr = new NoArrayGuard[2];                               // 전역 힙으로 간다 — 풀은 그대로
    CHECK(g_npool.used() == 0u);
    delete[] arr;
    auto* single = new NoArrayGuard;
    CHECK(g_npool.used() == 1u);
    delete single;
    CHECK(g_npool.used() == 0u);

    unsigned r0 = Message::size_rejects, c1 = Message::ctor_count;
    BigMessage* big = new BigMessage(1, 1);                        // 블록보다 큰 파생 클래스
    CHECK(big == nullptr);
    CHECK(Message::size_rejects == r0 + 1);
    CHECK(Message::ctor_count == c1);
    CHECK(g_pool.used() == 0u);
    SameSizeMessage* same = new SameSizeMessage(2, 2);             // 크기가 같은 파생은 허용
    CHECK(same != nullptr && g_pool.contains(same));
    delete same;
    CHECK(g_pool.used() == 0u);

    VBase* vb = new VDerived;                                      // virtual 소멸자: sized delete 에 동적 크기가 온다
    CHECK(vb != nullptr);
    delete vb;
    CHECK(VBase::last_delete_size == sizeof(VDerived));
    VBase* vb2 = new VBase;
    delete vb2;
    CHECK(VBase::last_delete_size == sizeof(VBase));

    // 잘못된 해제 — operator delete 를 직접 부른다 (delete 식을 두 번 쓰면 소멸자까지 두 번 돌아 그 자체로 UB)
    Message* m1 = new Message(1, 0);
    Message* m2 = new Message(2, 0);
    void* raw1 = m1;
    const auto i1 = g_pool.contains(m1) ? g_pool.index_of(m1) : std::uint16_t{0};
    m1->~Message();
    { CriticalSection cs; CHECK(g_pool.release(raw1) == FreeResult::Ok); }
    CHECK(g_pool.block_byte(i1, 0) == std::byte{0xDD} && g_pool.block_byte(i1, sizeof(Message) - 1) == std::byte{0xDD});
    unsigned df1 = g_pool.double_frees();
    Message::operator delete(raw1, sizeof(Message));               // 이중 해제
    CHECK(g_pool.double_frees() == df1 + 1);
    CHECK(g_pool.used() == 1u);                                    // 상태 불변

    int outside = 0;
    unsigned bf1 = g_pool.bad_frees();
    { CriticalSection cs; CHECK(g_pool.release(&outside) == FreeResult::OutOfRange); }
    { CriticalSection cs; CHECK(g_pool.release(reinterpret_cast<std::byte*>(m2) + 1) == FreeResult::Interior); }
    { CriticalSection cs; CHECK(g_pool.release(nullptr) == FreeResult::Null); }
    CHECK(g_pool.bad_frees() == bf1 + 2);

    unsigned wf0 = Message::wrong_size_frees;
    Message::operator delete(m2, sizeof(BigMessage));              // sized delete 로 크기 불일치 포착: 반납 안 함
    CHECK(Message::wrong_size_frees == wf0 + 1);
    CHECK(g_pool.used() == 1u);

    // 이중 해제 뒤에도 free list 가 안 깨졌다: 서로 다른 블록이 나와야 한다
    Message* p1 = new Message(11, 0);
    Message* p2 = new Message(12, 0);
    CHECK(p1 && p2 && p1 != p2 && p1 != m2 && p2 != m2);
    delete p1; delete p2; delete m2;
    CHECK(g_pool.used() == 0u);

    // ---- Part C: ISR 할당 / 메인 해제, 고갈 시 드롭, 임계 구역 ----
    unsigned v0 = irq::violations;
    for (std::uint8_t b = 10; b < 16; ++b) rx::uart_rx_isr(b);     // 블록 4개인데 6바이트 폭주
    CHECK(rx::received == 6u);
    CHECK(rx::dropped == 2u);
    CHECK(rx::ring.count == kBlocks);
    CHECK(g_pool.used() == kBlocks);
    unsigned drained = 0;
    while (rx::main_loop_once()) ++drained;
    CHECK(drained == kBlocks);
    CHECK(rx::processed == kBlocks);
    CHECK(rx::sum == 10u + 11u + 12u + 13u);                       // 드롭된 건 뒤의 두 바이트
    CHECK(g_pool.used() == 0u);
    rx::uart_rx_isr(77);                                           // 비운 뒤엔 다시 받는다
    CHECK(rx::main_loop_once() && rx::sum == 46u + 77u);
    CHECK(!rx::main_loop_once());
    CHECK(irq::violations == v0);                                  // 풀은 항상 임계 구역 안에서 접근됐다
    CHECK(irq::depth == 0);

    // 고정 시드 스트레스: 모델(shadow)과 풀의 상태가 항상 일치
    std::uint32_t seed = 0x2F6E2B1u;
    Message*      live[kBlocks] = {};
    std::uint32_t expect[kBlocks] = {};
    unsigned      n = 0;
    bool          soak_ok = true;
    for (unsigned it = 0; it < 20000u; ++it) {
        const bool do_alloc = (xorshift32(seed) & 1u) != 0u;
        if (do_alloc) {
            const std::uint32_t id = xorshift32(seed);
            Message* m = new Message(id, 3);
            if (n == kBlocks) {
                soak_ok = soak_ok && m == nullptr;
                if (m != nullptr) break;                           // 모델과 어긋남: 더 돌지 않는다
            } else {
                soak_ok = soak_ok && m != nullptr;
                if (m == nullptr) break;
                for (unsigned k = 0; k < n; ++k) soak_ok = soak_ok && live[k] != m;
                live[n] = m; expect[n] = id; ++n;
            }
        } else if (n > 0) {
            const unsigned k = xorshift32(seed) % n;
            soak_ok = soak_ok && live[k]->id == expect[k];
            delete live[k];
            --n;
            live[k] = live[n]; expect[k] = expect[n];
        }
        soak_ok = soak_ok && g_pool.used() == n;
    }
    CHECK(soak_ok);
    for (unsigned k = 0; k < n; ++k) delete live[k];
    CHECK(g_pool.used() == 0u);
    CHECK(Message::ctor_count == Message::dtor_count);
    CHECK(irq::depth == 0);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
