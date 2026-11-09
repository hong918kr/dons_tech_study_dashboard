// CPP-M06 virtual의 실제 비용: vtable·간접호출·devirtualization
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o a && ./a
//
// 이 파일은 맥에서 도는 "논리와 회계" 부분이다. 실제 비용 측정(nm/size/objdump)은 multi-TU 데모:
//   demo_bus.hpp / demo_uart.cpp / demo_spi.cpp / demo_i2c.cpp / demo_board.cpp / demo_app.cpp / demo_stubs.cpp + measure.sh
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <type_traits>

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using usize = std::size_t;

// ---------------- 가짜 레지스터 버스 (접근을 전부 기록) ----------------
namespace mock {
enum class Op : u8 { UartBaud, UartDr, SpiDiv, SpiCsLow, SpiDr, SpiCsHigh, I2cTiming, I2cAddr, I2cDr, I2cStop };
struct Ev { Op op; u32 v; };
inline Ev log[4096];
inline unsigned log_n = 0;
inline void reset() { log_n = 0; }
inline void record(Op op, u32 v) { if (log_n < 4096) log[log_n++] = Ev{op, v}; }
inline bool log_is(std::span<const Ev> want) {
    if (want.size() != log_n) return false;
    for (usize i = 0; i < want.size(); ++i)
        if (want[i].op != log[i].op || want[i].v != log[i].v) return false;
    return true;
}
}  // namespace mock
using mock::Op;

// ---------------- 인터페이스: protected 비가상 소멸자 (deleting destructor 없음) ----------------
class ByteBus {
public:
    virtual bool  init(u32 hz) = 0;                      // hz == 0 이면 false, 레지스터 접근 없음
    virtual usize write(std::span<const u8> bytes) = 0;  // init 전이면 0, 레지스터 접근 없음
    virtual u32   status() const = 0;                    // init 전 0, 후에는 드라이버 고유 값
protected:
    constexpr ByteBus() = default;
    ~ByteBus() = default;
};

// ---------------- Part A: 드라이버 3종 (final) ----------------
// UART: init → UartBaud(16000000/hz). write → 바이트마다 UartDr(b).                status 1
// SPI : init → SpiDiv(8000000/hz).    write → SpiCsLow(0), 바이트마다 SpiDr(b), SpiCsHigh(0).  status 2
// I2C : init → I2cTiming(4000000/hz). write → I2cAddr(addr<<1), 바이트마다 I2cDr(b), I2cStop(0). status 3
class UartDrv final : public ByteBus {
public:
    constexpr explicit UartDrv(u32 base) : base_(base) {}
    bool init(u32 hz) override {
        if (hz == 0) return false;
        hz_ = hz;
        mock::record(Op::UartBaud, 16000000u / hz);
        return true;
    }
    usize write(std::span<const u8> bytes) override {
        if (hz_ == 0) return 0;
        for (u8 b : bytes) mock::record(Op::UartDr, b);
        return bytes.size();
    }
    u32 status() const override { return hz_ ? 1u : 0u; }
    u32 base() const { return base_; }
private:
    u32 base_;
    u32 hz_ = 0;
};

class SpiDrv final : public ByteBus {
public:
    constexpr explicit SpiDrv(u32 base) : base_(base) {}
    bool init(u32 hz) override {
        if (hz == 0) return false;
        hz_ = hz;
        mock::record(Op::SpiDiv, 8000000u / hz);
        return true;
    }
    usize write(std::span<const u8> bytes) override {
        if (hz_ == 0) return 0;
        mock::record(Op::SpiCsLow, 0);
        for (u8 b : bytes) mock::record(Op::SpiDr, b);
        mock::record(Op::SpiCsHigh, 0);
        return bytes.size();
    }
    u32 status() const override { return hz_ ? 2u : 0u; }
    u32 base() const { return base_; }
private:
    u32 base_;
    u32 hz_ = 0;
};

class I2cDrv final : public ByteBus {
public:
    constexpr I2cDrv(u32 base, u8 addr) : base_(base), addr_(addr) {}
    bool init(u32 hz) override {
        if (hz == 0) return false;
        hz_ = hz;
        mock::record(Op::I2cTiming, 4000000u / hz);
        return true;
    }
    usize write(std::span<const u8> bytes) override {
        if (hz_ == 0) return 0;
        mock::record(Op::I2cAddr, static_cast<u32>(addr_) << 1);
        for (u8 b : bytes) mock::record(Op::I2cDr, b);
        mock::record(Op::I2cStop, 0);
        return bytes.size();
    }
    u32 status() const override { return hz_ ? 3u : 0u; }
    u32 base() const { return base_; }
    u8  addr() const { return addr_; }
private:
    u32 base_;
    u32 hz_ = 0;
    u8  addr_;
};

// ---------------- Part A: Itanium ABI 회계 모델 ----------------
struct Abi { u32 ptr; bool rtti; };                 // ptr = 포인터 바이트 수
inline constexpr Abi ILP32_NORTTI{4, false}, ILP32_RTTI{4, true}, LP64_NORTTI{8, false};

struct Shape {
    u32  name_len;      // 맹글링된 타입 이름 길이 ("7UartDrv" → 8), NUL 제외
    u32  virtuals;      // 소멸자를 뺀 가상 함수 슬롯 수
    bool virtual_dtor;  // true 면 슬롯 2개 추가 (complete D1 + deleting D0)
    bool has_base;      // true 면 단일 상속 (__si_class_type_info), false 면 루트 (__class_type_info)
};

// vtable = offset_to_top + typeinfo 슬롯 + 소멸자 슬롯 + 가상 함수 슬롯. RTTI 꺼도 슬롯은 남는다 (CPP-B13 참고).
constexpr u32 vtable_bytes(Abi abi, Shape s) {
    return (2u + (s.virtual_dtor ? 2u : 0u) + s.virtuals) * abi.ptr;
}
// typeinfo 객체(루트 2포인터 / 단일상속 3포인터) + 이름 문자열(NUL 포함). RTTI off 면 0.
constexpr u32 typeinfo_bytes(Abi abi, Shape s) {
    if (!abi.rtti) return 0;
    return (s.has_base ? 3u : 2u) * abi.ptr + s.name_len + 1u;
}
// 객체 크기: vptr + (정렬 패딩) + 멤버, 전체를 max(ptr, field_align) 배수로 올림. 멤버가 없으면(field_bytes == 0) vptr 만.
constexpr u32 object_bytes(Abi abi, u32 field_bytes, u32 field_align) {
    const u32 align = field_align > abi.ptr ? field_align : abi.ptr;
    const u32 start = field_align ? (abi.ptr + field_align - 1u) / field_align * field_align : abi.ptr;
    const u32 end   = start + field_bytes;
    return (end + align - 1u) / align * align;
}

// ---------------- Part B: 손으로 만든 vtable + guarded devirtualization ----------------
// C 드라이버의 struct uart_ops 와 같은 모양. self 는 void* (타입 소거).
struct Ops {
    bool  (*init)(void*, u32);
    usize (*write)(void*, std::span<const u8>);
    u32   (*status)(const void*);
};
struct Dev { const Ops* ops; void* self; };

// 캡처 없는 람다 → 함수 포인터. 안에서 p->D::write() 처럼 **한정 호출**하면 vtable 을 거치지 않는다.
template <typename D>
constexpr Ops make_ops() {
    return Ops{
        [](void* p, u32 hz) -> bool { return static_cast<D*>(p)->D::init(hz); },
        [](void* p, std::span<const u8> s) -> usize { return static_cast<D*>(p)->D::write(s); },
        [](const void* p) -> u32 { return static_cast<const D*>(p)->D::status(); },
    };
}
inline constinit const Ops kUartOps = make_ops<UartDrv>();
inline constinit const Ops kSpiOps  = make_ops<SpiDrv>();
inline constinit const Ops kI2cOps  = make_ops<I2cDrv>();

struct Dispatch { unsigned direct = 0, indirect = 0; };

// 컴파일러의 speculative devirtualization 을 손으로: ops 가 kUartOps 면 직접 호출(direct++), 아니면 함수 포인터(indirect++).
inline usize guarded_write(Dev d, std::span<const u8> s, Dispatch& st) {
    if (d.ops == &kUartOps) {
        ++st.direct;
        return static_cast<UartDrv*>(d.self)->UartDrv::write(s);
    }
    ++st.indirect;
    return d.ops->write(d.self, s);
}

// 생성/소멸 중의 가상 호출: 생성자가 vptr 을 단계별로 덮어쓴다는 증거.
inline char g_seen_in_dtor = 0;
struct ProbeBase {
    char seen_in_ctor;
    ProbeBase() : seen_in_ctor(kind()) {}
    virtual char kind() const { return 'B'; }
    ~ProbeBase() { g_seen_in_dtor = kind(); }
};
struct ProbeDerived final : ProbeBase {
    char kind() const override { return 'D'; }
};
// TODO(Part B): 아래 세 값을 예측해서 채워라 ('B' 또는 'D')
inline constexpr char kSeenInCtor = 'B';   // ProbeBase() 안에서 kind() 는?
inline constexpr char kSeenAfter  = 'D';   // 생성이 끝난 뒤 b.kind() 는? (ProbeBase& 로)
inline constexpr char kSeenInDtor = 'B';   // ~ProbeBase() 안에서 kind() 는?

// ---------------- Part C: 보드 레지스트리 + 예산 ----------------
inline constinit UartDrv g_uart{0x4000'4400u};
inline constinit SpiDrv  g_spi{0x4001'3000u};
inline constinit I2cDrv  g_i2c{0x4000'5400u, 0x50};
inline constinit ByteBus* const g_buses[] = {&g_uart, &g_spi, &g_i2c};

struct Report { usize sent; unsigned skipped; };

// status() == 0 인 버스는 건너뛰고 skipped++. 나머지는 msg 전체를 쓴다.
inline Report fanout(std::span<ByteBus* const> buses, std::span<const u8> msg) {
    Report r{0, 0};
    for (ByteBus* b : buses) {
        if (b->status() == 0) { ++r.skipped; continue; }
        r.sent += b->write(msg);
    }
    return r;
}

struct ClassRow { Shape shape; u32 instances; u32 field_bytes; u32 field_align; };
struct Budget { u32 flash_vtable, flash_typeinfo, ram_objects, ram_vptr; };

// 클래스별로 vtable/typeinfo 는 1번, 객체 크기와 vptr 은 instances 번 더한다. instances == 0 인 행은 전부 0
// (실제로도 객체가 없으면 gc-sections 가 vtable 을 버린다).
constexpr Budget board_budget(Abi abi, std::span<const ClassRow> rows) {
    Budget b{0, 0, 0, 0};
    for (const ClassRow& r : rows) {
        if (r.instances == 0) continue;
        b.flash_vtable   += vtable_bytes(abi, r.shape);
        b.flash_typeinfo += typeinfo_bytes(abi, r.shape);
        b.ram_objects    += r.instances * object_bytes(abi, r.field_bytes, r.field_align);
        b.ram_vptr       += r.instances * abi.ptr;
    }
    return b;
}

// ---------------- 하네스 ----------------
static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

// 템플릿 안의 requires 여야 false 가 된다 (템플릿 밖이면 하드 에러)
template <typename T> concept Deletable = requires(T* p) { delete p; };

// 컴파일타임 증명: 구조 규칙 (TODO 와 무관하게 항상 참)
static_assert(std::is_polymorphic_v<ByteBus> && std::is_abstract_v<ByteBus>);
static_assert(!std::has_virtual_destructor_v<ByteBus>);           // virtual 소멸자 없음 → D0/D1 슬롯 없음
static_assert(!std::is_destructible_v<ByteBus>);                  // protected → 밖에서 소멸 불가
static_assert(!Deletable<ByteBus>);                               // delete (ByteBus*) 는 컴파일 안 됨
static_assert(std::is_final_v<UartDrv> && std::is_final_v<SpiDrv> && std::is_final_v<I2cDrv>);
static_assert(std::is_trivially_destructible_v<UartDrv>);         // → 전역 객체에 atexit 등록 불필요
static_assert(std::is_base_of_v<ByteBus, I2cDrv>);

// vptr 값 엿보기 — Itanium ABI 구현 세부(첫 포인터 크기 칸이 vptr). 표준이 보장하지 않는다.
template <typename T> const void* vptr_of(const T& obj) {
    const void* p;
    std::memcpy(&p, static_cast<const void*>(&obj), sizeof p);
    return p;
}

static u32 xorshift32(u32& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

int main() {
    using mock::Ev;
    static constexpr u8 AB[] = {0x41, 0x42};

    // ---- Part A: 드라이버 동작 (기반 참조로 호출)
    {
        UartDrv u{0x1000u}; SpiDrv s{0x2000u}; I2cDrv i{0x3000u, 0x50};
        ByteBus* buses[] = {&u, &s, &i};

        // init 전: write 는 0 이고 버스에 아무것도 안 나간다
        mock::reset();
        for (ByteBus* b : buses) { CHECK(b->write(AB) == 0); CHECK(b->status() == 0); }
        CHECK(mock::log_n == 0);

        // hz == 0 거부 (0 나눗셈 방지) — 모두 레지스터 접근 없음
        for (ByteBus* b : buses) CHECK(!b->init(0));
        CHECK(mock::log_n == 0);

        CHECK(u.init(115200)); CHECK(s.init(1000000)); CHECK(i.init(400000));
        static constexpr Ev ini[] = {{Op::UartBaud, 16000000u / 115200u}, {Op::SpiDiv, 8u}, {Op::I2cTiming, 10u}};
        CHECK(mock::log_is(ini));
        CHECK(u.status() == 1 && s.status() == 2 && i.status() == 3);

        mock::reset(); CHECK(buses[0]->write(AB) == 2);
        static constexpr Ev w0[] = {{Op::UartDr, 0x41}, {Op::UartDr, 0x42}};
        CHECK(mock::log_is(w0));
        mock::reset(); CHECK(buses[1]->write(AB) == 2);
        static constexpr Ev w1[] = {{Op::SpiCsLow, 0}, {Op::SpiDr, 0x41}, {Op::SpiDr, 0x42}, {Op::SpiCsHigh, 0}};
        CHECK(mock::log_is(w1));
        mock::reset(); CHECK(buses[2]->write(AB) == 2);
        static constexpr Ev w2[] = {{Op::I2cAddr, 0xA0}, {Op::I2cDr, 0x41}, {Op::I2cDr, 0x42}, {Op::I2cStop, 0}};
        CHECK(mock::log_is(w2));

        // 빈 span: SPI CS 는 그래도 토글 (write 호출 자체는 일어났으므로)
        mock::reset(); CHECK(buses[1]->write({}) == 0);
        static constexpr Ev w3[] = {{Op::SpiCsLow, 0}, {Op::SpiCsHigh, 0}};
        CHECK(mock::log_is(w3));
    }

    // ---- Part A: vptr 은 객체마다 있고, 같은 클래스면 값이 같다 (arm64 Mac 의 실제 객체)
    {
        UartDrv a{1}, b{2}; SpiDrv s{3};
        CHECK(vptr_of(a) == vptr_of(b));
        CHECK(vptr_of(a) != vptr_of(s));
        CHECK(sizeof(UartDrv) == object_bytes(LP64_NORTTI, 8, 4));
        CHECK(sizeof(SpiDrv)  == object_bytes(LP64_NORTTI, 8, 4));
        CHECK(sizeof(I2cDrv)  == object_bytes(LP64_NORTTI, 9, 4));
    }

    // ---- Part A: 회계 모델 (기대값 = measure.sh 로 측정한 ELF 의 nm -S 값)
    {
        struct Row { Abi abi; Shape s; u32 vt, ti; };
        static constexpr Row rows[] = {
            {ILP32_NORTTI, {8, 4, false, true},  24, 0},   // vtable for UartDrv     : nortti 0x18
            {ILP32_RTTI,   {8, 4, false, true},  24, 21},  // typeinfo 0xc + name "7UartDrv\0" 9
            {ILP32_RTTI,   {7, 4, false, true},  24, 20},  // SpiDrv: 0xc + "6SpiDrv\0" 8
            {ILP32_RTTI,   {8, 4, false, false}, 24, 17},  // ByteBus 루트: 8 + "7ByteBus\0" 9
            {ILP32_NORTTI, {8, 4, true,  true},  32, 0},   // virtual dtor: 슬롯 2개 더 (0x20)
            {LP64_NORTTI,  {8, 3, true,  true},  56, 0},   // 호스트 예: (2+2+3)*8
            {LP64_NORTTI,  {8, 0, false, false}, 16, 0},   // 가상 함수 0개여도 앞 2칸
        };
        for (const Row& r : rows) {
            CHECK(vtable_bytes(r.abi, r.s) == r.vt);
            CHECK(typeinfo_bytes(r.abi, r.s) == r.ti);
        }
        CHECK(object_bytes(ILP32_NORTTI, 8, 4) == 12);    // g_uart / g_spi 실측
        CHECK(object_bytes(ILP32_NORTTI, 9, 4) == 16);    // g_i2c 실측: 끝 패딩 3
        CHECK(object_bytes(ILP32_NORTTI, 1, 1) == 8);     // vptr 4 + u8 + 패딩 3
        CHECK(object_bytes(ILP32_NORTTI, 8, 8) == 16);    // double 멤버: vptr 뒤 패딩 4
        CHECK(object_bytes(LP64_NORTTI, 1, 1) == 16);
        CHECK(object_bytes(ILP32_NORTTI, 0, 1) == 4);     // 멤버 없음: vptr 만
    }

    // ---- Part B: 손 vtable 이 virtual 과 같은 동작을 하나
    {
        UartDrv u{1}; SpiDrv s{2}; I2cDrv i{3, 0x50};
        Dev d[] = {{&kUartOps, &u}, {&kSpiOps, &s}, {&kI2cOps, &i}};
        for (Dev& x : d) CHECK(x.ops->status(x.self) == 0);
        CHECK(kUartOps.init(&u, 115200) && kSpiOps.init(&s, 1000000) && kI2cOps.init(&i, 400000));
        CHECK(u.status() == 1 && s.status() == 2 && i.status() == 3);
        CHECK(kI2cOps.status(&i) == 3);
        mock::reset();
        CHECK(kSpiOps.write(&s, AB) == 2);
        static constexpr Ev w1[] = {{Op::SpiCsLow, 0}, {Op::SpiDr, 0x41}, {Op::SpiDr, 0x42}, {Op::SpiCsHigh, 0}};
        CHECK(mock::log_is(w1));
    }

    // ---- Part B: guarded dispatch (고정 시드 혼합 부하 1000회)
    {
        UartDrv u{1}; SpiDrv s{2}; I2cDrv i{3, 0x50};
        u.init(115200); s.init(1000000); i.init(400000);
        Dev d[] = {{&kUartOps, &u}, {&kSpiOps, &s}, {&kI2cOps, &i}};
        Dispatch st; u32 seed = 0x2545'F491u; unsigned want_direct = 0, want_ind = 0; usize sent = 0;
        for (int n = 0; n < 1000; ++n) {
            u32 pick = xorshift32(seed) % 3u;
            if (pick == 0) ++want_direct; else ++want_ind;
            mock::reset();
            sent += guarded_write(d[pick], AB, st);
        }
        CHECK(st.direct == want_direct);
        CHECK(st.indirect == want_ind);
        CHECK(st.direct + st.indirect == 1000u);
        CHECK(sent == 2000u);
        // 초기화 안 된 드라이버도 가드를 지나 0 을 돌려준다
        UartDrv cold{9}; Dispatch st2;
        CHECK(guarded_write(Dev{&kUartOps, &cold}, AB, st2) == 0 && st2.direct == 1 && st2.indirect == 0);
    }

    // ---- Part B: 생성/소멸 중 동적 타입
    {
        g_seen_in_dtor = 0;
        char ctor = 0, after = 0;
        {
            ProbeDerived p;
            ProbeBase& b = p;
            ctor = b.seen_in_ctor;
            after = b.kind();
        }
        CHECK(ctor == 'B');          // 런타임 사실
        CHECK(after == 'D');
        CHECK(g_seen_in_dtor == 'B');
        CHECK(ctor == kSeenInCtor);  // 학생의 예측과 비교
        CHECK(after == kSeenAfter);
        CHECK(g_seen_in_dtor == kSeenInDtor);
    }

    // ---- Part C: 레지스트리 fanout (전역 UartDrv 등은 constinit)
    {
        CHECK(g_uart.base() == 0x4000'4400u && g_i2c.base() == 0x4000'5400u && g_i2c.addr() == 0x50);
        mock::reset();
        Report r0 = fanout(g_buses, AB);          // 모두 init 전
        CHECK(r0.sent == 0 && r0.skipped == 3 && mock::log_n == 0);
        g_uart.init(115200); g_i2c.init(400000);  // SPI 는 일부러 안 켠다
        mock::reset();
        Report r1 = fanout(g_buses, AB);
        CHECK(r1.sent == 4 && r1.skipped == 1);
        static constexpr Ev want[] = {
            {Op::UartDr, 0x41}, {Op::UartDr, 0x42},
            {Op::I2cAddr, 0xA0}, {Op::I2cDr, 0x41}, {Op::I2cDr, 0x42}, {Op::I2cStop, 0},
        };
        CHECK(mock::log_is(want));
        CHECK(fanout({}, AB).sent == 0);
    }

    // ---- Part C: 보드 예산 — 기대값은 measure.sh 의 ELF 실측
    {
        static constexpr ClassRow board[] = {
            {{8, 4, false, true}, 1, 8, 4},   // UartDrv
            {{7, 4, false, true}, 1, 8, 4},   // SpiDrv
            {{7, 4, false, true}, 1, 9, 4},   // I2cDrv
        };
        Budget off = board_budget(ILP32_NORTTI, board);
        CHECK(off.flash_vtable == 72);        // nortti: vtables=3 (72B)
        CHECK(off.flash_typeinfo == 0);
        CHECK(off.ram_objects == 40);         // .data = 40 바이트
        CHECK(off.ram_vptr == 12);
        Budget on = board_budget(ILP32_RTTI, board);
        CHECK(on.flash_vtable == 72);
        CHECK(on.flash_typeinfo == 61);       // 21 + 20 + 20: 드라이버 3개 몫 (ByteBus 17 B 별도)

        static constexpr ClassRow gone[] = {{{8, 4, false, true}, 0, 8, 4}};
        Budget none = board_budget(ILP32_NORTTI, gone);
        CHECK(none.flash_vtable == 0 && none.ram_objects == 0 && none.ram_vptr == 0);

        static constexpr ClassRow many[] = {{{8, 4, false, true}, 10, 8, 4}};
        Budget m = board_budget(ILP32_NORTTI, many);
        CHECK(m.flash_vtable == 24 && m.ram_objects == 120 && m.ram_vptr == 40);   // vtable 은 1벌, vptr 은 10벌

        // constexpr 로도 동작 (예산을 컴파일타임에 static_assert 할 수 있다)
        constexpr u32 v = vtable_bytes(ILP32_NORTTI, Shape{8, 4, false, true});
        CHECK(v == 24);
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
