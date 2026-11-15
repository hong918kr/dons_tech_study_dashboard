/* CPP-A03 C++ 런타임 의존성 최소화: __cxa_* 심볼 추적
 *
 * 프리스탠딩 펌웨어 오브젝트가 요구하는 런타임 심볼을 "읽고(Part A) → 스텁으로 채우고(Part B) → CI 로 막는다(Part C)".
 *   Part A: nm 출력 한 줄 파서 + 심볼 분류기 (구문 → 심볼 → 출처 → 제거법). 픽스처는 실제 nm 출력을 붙여 넣은 것
 *   Part B: 스텁 층 — __cxa_guard_acquire/release/abort 상태 기계 (단일 코어 + 인터럽트), __cxa_pure_virtual trap 기록
 *   Part C: __cxa_atexit / __aeabi_atexit 등록부 (용량 고정) + 허용 목록 게이트 (check_syms.sh 의 C++ 판)
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

using std::string_view;

/* ================= 실제 nm 출력 (붙여 넣은 픽스처, 5. 풀이에 같은 출력) =================
 * clang = 맥 Apple clang 21 --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -ffreestanding, xcrun llvm-nm
 * gcc   = Lima VM arm-none-eabi-g++ 14.2.1 -mcpu=cortex-m4 -mthumb -O2 -ffreestanding, arm-none-eabi-nm
 * host  = 맥 arm64 Mach-O (이름 앞에 '_' 하나가 더 붙는다) */
static const string_view kClangRow1Full[] = {          /* demo_constructs.cpp ROW 1, llvm-nm (전체) */
    "00000000 b .L_MergedGlobals",
    "00000000 T _Z8cal_gainv",
    "00000000 t _Z8cal_gainv.cold.1",
    "00000004 b _ZGVZ8cal_gainvE1c",
    "         U _ZN3CalC1Ev",
    "00000000 b _ZZ8cal_gainvE1c",
    "         U __cxa_guard_acquire",
    "         U __cxa_guard_release",
};
static const string_view kGccRow1Sized[] = {           /* ROW 1, arm-none-eabi-nm -S (주소 크기 타입 이름) */
    "00000000 00000030 T _Z8cal_gainv",
    "00000000 00000004 b _ZGVZ8cal_gainvE1c",
    "         U _ZN3CalC1Ev",
    "00000004 00000004 b _ZZ8cal_gainvE1c",
    "         U __cxa_guard_acquire",
    "         U __cxa_guard_release",
};
static const string_view kClangLegacy[] = {            /* fw_app.cpp -DLEGACY, llvm-nm -u */
    "         U _ZdlPvj",
    "         U _Znwj",
    "         U __aeabi_memclr8",
    "         U __aeabi_uldivmod",
    "         U __cxa_atexit",
    "         U __cxa_guard_acquire",
    "         U __cxa_guard_release",
    "         U __dso_handle",
    "         U hal_cycles64",
    "         U hal_otp_offset",
    "         U hal_uart_write",
};
static const string_view kClangFixed[] = {             /* fw_app.cpp (고친 판), llvm-nm -u */
    "         U __aeabi_memclr4",
    "         U __aeabi_uldivmod",
    "         U hal_cycles64",
    "         U hal_otp_offset",
    "         U hal_uart_write",
};
static const string_view kGccRow2[] = {                /* ROW 2 순수 가상, GCC: 약한(weak) 미정의 */
    "00000000 T _ZN3Bus5flushEv",
    "00000000 R _ZTV3Bus",
    "         w __cxa_pure_virtual",
};
static const string_view kGccRow3[] = {                /* ROW 3 소멸자 있는 전역, GCC: __aeabi_atexit */
    "00000000 t _GLOBAL__sub_I_g_log",
    "         U _ZN6LoggerC1Ev",
    "         U _ZN6LoggerD1Ev",
    "         U __aeabi_atexit",
    "         U __dso_handle",
    "00000000 B g_log",
};
static const string_view kClangRow7Rtti[] = {          /* ROW 7 -frtti, typeid + dynamic_cast */
    "         U _ZTVN10__cxxabiv117__class_type_infoE",
    "         U _ZTVN10__cxxabiv120__si_class_type_infoE",
    "         U __dynamic_cast",
};
static const string_view kGccRow8Exc[] = {             /* ROW 8 -fexceptions, throw/catch */
    "         U _ZTIi",
    "         U __aeabi_unwind_cpp_pr1",
    "         U __cxa_allocate_exception",
    "         U __cxa_begin_catch",
    "         U __cxa_end_catch",
    "         U __cxa_end_cleanup",
    "         U __cxa_throw",
    "         U __gxx_personality_v0",
};
static const string_view kClangRow11[] = {             /* ROW 11 double 연산 (M4F 는 단정도 FPU) */
    "         U __aeabi_dadd",
    "         U __aeabi_dmul",
};
static const string_view kHostRow3Full[] = {           /* ROW 3, 맥 호스트 Mach-O, llvm-nm (전체) */
    "0000000000000000 s __GLOBAL__sub_I_demo_constructs.cpp",
    "                 U __ZN6LoggerC1Ev",
    "                 U __ZN6LoggerD1Ev",
    "                 U ___cxa_atexit",
    "                 U ___dso_handle",
    "0000000000000048 S _g_log",
    "0000000000000000 t ltmp0",
};
static const string_view kHostRow5U[] = {              /* ROW 5, Mach-O 의 llvm-nm -u 는 이름만 찍는다 */
    "__ZdlPv",
};
template <class T, std::size_t N> constexpr std::size_t count_of(const T (&)[N]) { return N; }

/* ================= Part A: nm 한 줄 → 심볼, 심볼 → 종류 · 출처 · 제거법 ================= */
enum class Kind : uint8_t {
    Project,      /* 내 다른 TU 가 줄 것 (hal_*, 생성자 등) */
    Guard,        /* __cxa_guard_acquire/release : 함수 내부 static */
    GuardAbort,   /* __cxa_guard_abort : 예외 켠 빌드에서만 */
    PureVirtual,  /* __cxa_pure_virtual / __cxa_deleted_virtual */
    Atexit,       /* __cxa_atexit / __aeabi_atexit / atexit : 소멸자 있는 정적 객체 */
    DsoHandle,    /* __dso_handle */
    OpNew,        /* _Znw* / _Zna* */
    OpDelete,     /* _Zdl* / _Zda* */
    Rtti,         /* __dynamic_cast, _ZTI*, _ZTS*, _ZTVN10__cxxabiv1* */
    Eh,           /* 예외: __cxa_throw ..., __gxx_personality_v0, __aeabi_unwind_cpp_pr*, _Unwind_* */
    IntHelper,    /* __aeabi_uldivmod / __aeabi_idiv / __aeabi_lmul ... */
    MemFn,        /* memcpy/memset/memmove, __aeabi_memcpy4 ... */
    SoftFloat,    /* __aeabi_dadd / __aeabi_fmul / __aeabi_f2iz ... */
    Atomic,       /* __atomic_* / __sync_* (CPP-A04) */
    Unknown,      /* "__" 로 시작하는데 위에 없음 → 사람이 확인 */
    Count_
};
constexpr std::size_t kKinds = static_cast<std::size_t>(Kind::Count_);
enum class Fix : uint8_t {
    LinkYourCode,          /* 프로젝트 심볼: 그 TU 를 링크 */
    ConstinitOrNoTsStatics,/* constinit/명시적 init, 또는 -fno-threadsafe-statics, 또는 guard 스텁 */
    ExceptionsOff,         /* -fno-exceptions */
    TrapStub,              /* trap + 고장 기록 스텁 */
    NoDestroy,             /* 소멸되지 않는 래퍼 / 자명 소멸자 / -fno-c++-static-destructors */
    StaticStorage,         /* 정적 풀/정적 객체 — 힙 제거 */
    NonVirtualDtor,        /* protected 비가상 소멸자 (CPP-M06), 아니면 delete 를 trap 으로 */
    RttiOff,               /* -fno-rtti + 자체 타입 태그 */
    Libgcc,                /* 툴체인 libgcc 가 준다 (허용 목록) */
    Libc,                  /* newlib/picolibc 가 준다 (허용 목록) */
    FloatOrFixed,          /* float/고정소수점으로 */
    AtomicPolicy,          /* CPP-A04 isr_shared 정책 */
    Investigate
};
enum class Provider : uint8_t { You, Libsupcxx, Libgcc, Libc, Crt, Nobody };

struct NmSym {
    string_view name;
    char type = 0;          /* nm 타입 글자: U, w, T, t, b, B, D, R, S, s ... */
    bool undefined = false; /* U 또는 w(약한 미정의) */
};

/* 형식 4가지: "주소 타입 이름", "주소 크기 타입 이름", "   U 이름", Mach-O -u 의 "이름" 만.
 * 빈 줄, "file.o:" 머리 줄, 타입 글자가 한 글자가 아닌 줄은 false. */
static bool parse_nm_line(string_view line, NmSym& out) {
    /* TODO(A): 공백으로 토큰을 나눠 마지막 = 이름, 그 앞 = 타입 한 글자 (U/w 면 미정의).
     *          토큰 1개 = Mach-O llvm-nm -u 의 "이름만" 줄 (단, "file.o:" 머리 줄은 제외) */
    (void)line; (void)out;
    return false;
}

/* Mach-O 는 C 이름 앞에 '_' 를 하나 더 붙인다 (___cxa_atexit, __Znwm). ELF 는 그대로. */
static string_view normalize(string_view name, bool macho) {
    (void)macho;   /* TODO(A): Mach-O 면 앞의 '_' 하나를 뗀다 */
    return name;
}

[[maybe_unused]] static bool starts(string_view s, string_view p) { return s.substr(0, p.size()) == p; }

static Kind classify(string_view s) {
    /* TODO(A): 5. 풀이의 표대로. 순서 주의 — __aeabi_atexit(Atexit), __aeabi_unwind_cpp_pr*(Eh), __aeabi_mem*(MemFn),
     *          __aeabi_ddiv(SoftFloat, div 인데도), __aeabi_uldivmod(IntHelper). 모르는 "__" 이름은 Unknown */
    (void)s;
    return Kind::Project;
}

static Fix fix_for(Kind k) {
    (void)k;   /* TODO(A): 종류 → 제거법 */
    return Fix::Investigate;
}

/* arm-none-eabi 14.2 아카이브를 nm 으로 찾아본 결과 (link_vm.sh 의 "누가 주나") */
static Provider provider_for(Kind k) {
    (void)k;   /* TODO(A): 종류 → 누가 주나 (link_vm.sh 의 아카이브 nm 결과) */
    return Provider::Nobody;
}

struct Summary {
    uint16_t lines = 0;              /* 파싱된 줄 */
    uint16_t undefined = 0;          /* U + w */
    uint16_t weak_undefined = 0;     /* w: 정의가 없어도 링크되고 주소가 0 이 된다 */
    uint16_t by_kind[kKinds] = {};   /* 미정의 심볼만 센다 */
};
static Summary summarize(const string_view* lines, std::size_t n, bool macho) {
    Summary s{};
    /* TODO(A): 파싱된 줄 수, 미정의(U/w) 수, w 수, 미정의 심볼의 종류별 수 (정규화 후 분류) */
    (void)lines; (void)n; (void)macho;
    return s;
}
static uint16_t of(const Summary& s, Kind k) { return s.by_kind[static_cast<std::size_t>(k)]; }

/* ================= Part B: 스텁 층 — guard 상태 기계와 pure virtual trap =================
 * mock Cortex-M 코어 (CPP-A04 와 같은 모델): PRIMASK = 전역 변수, 인터럽트 = pending 함수 포인터 1개. */
static uint32_t g_primask = 0;
static int g_isr_runs = 0;
static void (*g_pending_isr)() = nullptr;
static void (*g_armed_isr)() = nullptr;
static void dispatch_pending() {
    if (g_pending_isr != nullptr && (g_primask & 1u) == 0) {
        void (*isr)() = g_pending_isr;
        g_pending_isr = nullptr;
        ++g_isr_runs;
        isr();
    }
}
[[maybe_unused]] static uint32_t irq_save() { const uint32_t pm = g_primask; g_primask = 1; return pm; }       /* mrs; cpsid i */
[[maybe_unused]] static void irq_restore(uint32_t pm) { g_primask = pm; dispatch_pending(); }                 /* msr primask */
static void arm_preempt(void (*isr)()) { g_armed_isr = isr; }
static void isr_window() {                       /* "여기서 인터럽트가 들어올 수 있다" (실제 칩에선 모든 명령 경계) */
    if (g_armed_isr != nullptr) { g_pending_isr = g_armed_isr; g_armed_isr = nullptr; dispatch_pending(); }
}
static void reset_core() { g_primask = 0; g_isr_runs = 0; g_pending_isr = nullptr; g_armed_isr = nullptr; }

namespace rt {
enum class Fault : uint8_t { None, PureVirtual, GuardRecursion };
struct FaultLog {
    Fault first = Fault::None;     /* 첫 원인이 제일 중요 — 뒤 고장은 대개 첫 고장의 결과 */
    Fault last = Fault::None;
    uint32_t count = 0;
    uint32_t where = 0;            /* 첫 고장의 위치 태그 (실제 스텁은 LR = __builtin_return_address(0)) */
};
static FaultLog g_fault;
/* 실제 펌웨어: 기록 후 bkpt + for(;;) (WDT 리셋). 호스트 하네스는 기록만 하고 돌아온다. */
[[maybe_unused]] static void trap(Fault f, uint32_t where) {
    if (g_fault.count == 0) { g_fault.first = f; g_fault.where = where; }
    g_fault.last = f;
    ++g_fault.count;
}

/* ARM C++ ABI guard: 4바이트 워드, bit0 = 초기화 완료. 컴파일러가 인라인으로 bit0 을 먼저 본다
 * (clang: ldrb; dmb sy; lsls #31 / GCC: ldr; dmb ish; lsls #31 — 실측). bit0 이외의 비트는 런타임 몫. */
constexpr uint32_t kDone = 1u;
constexpr uint32_t kBusy = 1u << 8;

/* 주어진 접근 함수: 읽은 직후 인터럽트가 들어올 수 있다 (모든 명령 경계를 대표하는 지점) */
[[maybe_unused]] static uint32_t guard_load(const uint32_t* g) { const uint32_t v = *g; isr_window(); return v; }
[[maybe_unused]] static void guard_store(uint32_t* g, uint32_t v) { *g = v; }

/* 1 = 호출자가 초기화해야 함 (busy 표시함), 0 = 이미 끝남.
 * 초기화 중 재진입(같은 코어의 ISR 이거나 생성자 안의 재귀) → 기다릴 수 없다 → trap(GuardRecursion) 후 0.
 * 검사와 busy 표시 사이에 ISR 이 끼면 안 된다. 하지만 생성자는 인터럽트를 켠 채 돌아야 한다. */
static int guard_acquire(uint32_t* g) {
    /* TODO(B): guard_load / guard_store 로. 완료면 0, busy 면 trap(Fault::GuardRecursion, 0xA1) 후 0, 아니면 busy 표시하고 1.
     *          검사~표시 사이에 ISR 이 끼면 안 되고, 호출자의 PRIMASK 는 원래 값으로 돌려 둔다 */
    (void)g;
    return 0;
}
static void guard_release(uint32_t* g) { (void)g; }                 /* TODO(B): busy 지우고 bit0 세움 */
static void guard_abort(uint32_t* g) { (void)g; }                   /* TODO(B): 생성자 실패 → 처음 상태로 */

/* __cxa_pure_virtual 의 몸통: 기록 + trap. Itanium ABI: 동작은 정하지 않지만 "종료할 것으로 기대" */
static void pure_virtual(uint32_t where) { (void)where; }           /* TODO(B): 고장 기록 */
}  // namespace rt

/* 컴파일러가 `static Cal c;` 에 대해 내는 코드를 손으로 쓴 것 (ARM: 빠른 경로는 bit0 검사) */
static uint8_t guard_first_byte(const uint32_t* g) { uint8_t b; std::memcpy(&b, g, 1); return b; }  /* ldrb */
struct LazyStatic {
    uint32_t guard = 0;
    int value = 0;
    int ctor_runs = 0;
    int (*ctor)(LazyStatic&) = nullptr;
    int* get() {
        if ((guard_first_byte(&guard) & 1u) == 0) {                /* ldrb; dmb; lsls #31; beq slow */
            if (rt::guard_acquire(&guard)) { value = ctor(*this); rt::guard_release(&guard); }
        }
        return &value;
    }
};
static LazyStatic g_cal, g_other;
static bool g_busy_seen_in_isr = false;
static int g_isr_value = 0;
static int cal_ctor(LazyStatic& self) { ++self.ctor_runs; isr_window(); return 7; }   /* 생성자 도중에도 IRQ 가능 */
static int other_ctor(LazyStatic& self) { ++self.ctor_runs; return 11; }
static void isr_uses_cal() { g_isr_value = *g_cal.get(); }
static void isr_uses_other() { g_busy_seen_in_isr = (g_cal.guard & rt::kBusy) != 0; g_isr_value = *g_other.get(); }
static int recursive_ctor(LazyStatic& self) { ++self.ctor_runs; return *self.get() + 1; }  /* 생성자가 자기 자신을 부름 */

/* 손으로 만든 vtable (CPP-M06): 구현되지 않은 슬롯 = __cxa_pure_virtual */
struct SensorOps { int (*sample)(const void* self); };
static int pure_slot(const void*) { rt::pure_virtual(0x5A); return 0; }
static int temp_sample(const void*) { return 25; }
static constexpr SensorOps kSensorBaseOps{&pure_slot};   /* 추상 기반 Sensor 의 vtable */
static constexpr SensorOps kTempOps{&temp_sample};       /* final 파생 Temp 의 vtable */

/* ================= Part C: atexit 등록부 + 허용 목록 게이트 ================= */
/* Itanium: __cxa_atexit(f, p, d) — 성공 0, 실패 0 아님. ARM EABI: __aeabi_atexit(object, destroyer, dso) — 앞 두 인자가 뒤바뀜.
 * finalize(d): d 가 같은 항목을 등록 역순(LIFO)으로 한 번씩 실행. 이 모델에서 d == nullptr 이면 전부. */
template <std::size_t N>
class AtexitRegistry {
public:
    int cxa_atexit(void (*f)(void*), void* obj, void* dso) {
        (void)f; (void)obj; (void)dso;   /* TODO(C): 가득 차면 dropped_ 증가 후 -1, 아니면 추가 후 0 */
        return -1;
    }
    int aeabi_atexit(void* obj, void (*f)(void*), void* dso) { (void)obj; (void)f; (void)dso; return -1; }   /* TODO(C) */
    std::size_t finalize(void* dso) {
        std::size_t ran = 0;
        (void)dso;   /* TODO(C): 역순(LIFO), dso 일치 (nullptr = 전부), 항목마다 한 번만 */
        return ran;
    }
    std::size_t size() const { return n_; }
    uint32_t dropped() const { return dropped_; }
private:
    struct Entry { void (*f)(void*); void* obj; void* dso; bool live; };
    Entry e_[N] = {};
    std::size_t n_ = 0;
    uint32_t dropped_ = 0;
};

constexpr uint32_t bit(Kind k) { return 1u << static_cast<uint32_t>(k); }
/* allow_syms.txt 와 같은 정책: 프로젝트 + libgcc 정수 도우미 + libc mem* + 우리 trap 스텁 */
constexpr uint32_t kFirmwareAllow = bit(Kind::Project) | bit(Kind::IntHelper) | bit(Kind::MemFn) | bit(Kind::PureVirtual);

struct GateResult {
    uint16_t checked = 0;        /* 미정의 심볼 수 */
    uint16_t violations = 0;     /* 허용 종류 밖 */
    string_view first;           /* 첫 위반 (정규화된 이름) */
};
static GateResult gate(const string_view* lines, std::size_t n, bool macho, uint32_t allow) {
    GateResult r{};
    /* TODO(C): 미정의 심볼마다 정규화 → 분류 → allow 에 그 종류 비트가 없으면 위반. 첫 위반 이름 기록 */
    (void)lines; (void)n; (void)macho; (void)allow;
    return r;
}

/* ---- Part C 시나리오: 부팅 때 .init_array 의 전역 생성자들이 소멸자를 등록한다 ---- */
static char g_dtor_log[8];
static std::size_t g_dtor_len = 0;
static void log_dtor(void* tag) { if (g_dtor_len < sizeof g_dtor_log - 1) g_dtor_log[g_dtor_len++] = *static_cast<char*>(tag); }
static char kA = 'A', kB = 'B', kC = 'C', kD = 'D';
static int g_dso_main = 0, g_dso_plugin = 0;

int main() {
    /* ---- Part A: 파서 ---- */
    {
        NmSym s;
        CHECK(parse_nm_line("00000000 T _Z8cal_gainv", s) && s.name == "_Z8cal_gainv" && s.type == 'T' && !s.undefined);
        CHECK(parse_nm_line("         U __cxa_guard_acquire", s) && s.name == "__cxa_guard_acquire" && s.undefined);
        CHECK(parse_nm_line("00000000 00000004 b _ZGVZ8cal_gainvE1c", s) && s.name == "_ZGVZ8cal_gainvE1c" && s.type == 'b');
        CHECK(parse_nm_line("         w __cxa_pure_virtual", s) && s.undefined && s.type == 'w');
        CHECK(parse_nm_line("__ZdlPv", s) && s.name == "__ZdlPv" && s.undefined);           /* Mach-O -u 형식 */
        CHECK(parse_nm_line("0000000000000048 S _g_log", s) && s.name == "_g_log" && !s.undefined);
        CHECK(!parse_nm_line("", s) && !parse_nm_line("   ", s) && !parse_nm_line("r.o:", s));
        CHECK(!parse_nm_line("00000000 TT bogus", s));
    }
    /* ---- Part A: 정규화 + 분류 ---- */
    CHECK(normalize("___cxa_atexit", true) == "__cxa_atexit" && normalize("__Znwm", true) == "_Znwm");
    CHECK(normalize("__cxa_atexit", false) == "__cxa_atexit");
    CHECK(classify("__cxa_guard_acquire") == Kind::Guard && classify("__cxa_guard_abort") == Kind::GuardAbort);
    CHECK(classify("__cxa_pure_virtual") == Kind::PureVirtual && classify("__cxa_deleted_virtual") == Kind::PureVirtual);
    CHECK(classify("__cxa_atexit") == Kind::Atexit && classify("__aeabi_atexit") == Kind::Atexit && classify("atexit") == Kind::Atexit);
    CHECK(classify("__dso_handle") == Kind::DsoHandle);
    CHECK(classify("_Znwj") == Kind::OpNew && classify("_ZnajRKSt9nothrow_t") == Kind::OpNew && classify("_Znwm") == Kind::OpNew);
    CHECK(classify("_ZdlPvj") == Kind::OpDelete && classify("_ZdaPv") == Kind::OpDelete);
    CHECK(classify("__dynamic_cast") == Kind::Rtti && classify("_ZTVN10__cxxabiv117__class_type_infoE") == Kind::Rtti);
    CHECK(classify("_ZTIi") == Kind::Rtti && classify("_ZTV3Bus") == Kind::Project);           /* 내 vtable 은 프로젝트 몫 */
    CHECK(classify("__cxa_throw") == Kind::Eh && classify("__cxa_end_cleanup") == Kind::Eh && classify("__gxx_personality_v0") == Kind::Eh);
    CHECK(classify("__aeabi_unwind_cpp_pr0") == Kind::Eh && classify("_Unwind_Resume") == Kind::Eh);
    CHECK(classify("__aeabi_uldivmod") == Kind::IntHelper && classify("__aeabi_idiv") == Kind::IntHelper && classify("__aeabi_lmul") == Kind::IntHelper);
    CHECK(classify("__aeabi_ddiv") == Kind::SoftFloat && classify("__aeabi_fdiv") == Kind::SoftFloat);   /* div 지만 부동소수점 */
    CHECK(classify("__aeabi_f2iz") == Kind::SoftFloat && classify("__aeabi_ul2d") == Kind::SoftFloat && classify("__aeabi_cdcmple") == Kind::SoftFloat);
    CHECK(classify("__aeabi_memclr8") == Kind::MemFn && classify("memset") == Kind::MemFn);
    CHECK(classify("__atomic_fetch_add_8") == Kind::Atomic && classify("__sync_synchronize") == Kind::Atomic);
    CHECK(classify("hal_uart_write") == Kind::Project && classify("_ZN3CalC1Ev") == Kind::Project);
    CHECK(classify("__cxa_bad_cast") == Kind::Unknown && classify("__aeabi_read_tp") == Kind::Unknown && classify("__stack_chk_fail") == Kind::Unknown);
    /* ---- Part A: 제거법 · 출처 ---- */
    CHECK(fix_for(Kind::Guard) == Fix::ConstinitOrNoTsStatics && fix_for(Kind::GuardAbort) == Fix::ExceptionsOff);
    CHECK(fix_for(Kind::PureVirtual) == Fix::TrapStub && fix_for(Kind::Atexit) == Fix::NoDestroy && fix_for(Kind::DsoHandle) == Fix::NoDestroy);
    CHECK(fix_for(Kind::OpNew) == Fix::StaticStorage && fix_for(Kind::OpDelete) == Fix::NonVirtualDtor);
    CHECK(fix_for(Kind::Rtti) == Fix::RttiOff && fix_for(Kind::Eh) == Fix::ExceptionsOff);
    CHECK(fix_for(Kind::IntHelper) == Fix::Libgcc && fix_for(Kind::MemFn) == Fix::Libc && fix_for(Kind::SoftFloat) == Fix::FloatOrFixed);
    CHECK(fix_for(Kind::Atomic) == Fix::AtomicPolicy && fix_for(Kind::Unknown) == Fix::Investigate && fix_for(Kind::Project) == Fix::LinkYourCode);
    CHECK(provider_for(Kind::Guard) == Provider::Libsupcxx && provider_for(Kind::OpNew) == Provider::Libsupcxx);
    CHECK(provider_for(Kind::Atexit) == Provider::Libc && provider_for(Kind::DsoHandle) == Provider::Crt);
    CHECK(provider_for(Kind::IntHelper) == Provider::Libgcc && provider_for(Kind::SoftFloat) == Provider::Libgcc && provider_for(Kind::MemFn) == Provider::Libc);
    CHECK(provider_for(Kind::Atomic) == Provider::Nobody && provider_for(Kind::Project) == Provider::You);
    /* ---- Part A: 실제 픽스처 요약 ---- */
    {
        const Summary s = summarize(kClangLegacy, count_of(kClangLegacy), false);
        CHECK(s.lines == 11 && s.undefined == 11 && s.weak_undefined == 0);
        CHECK(of(s, Kind::Guard) == 2 && of(s, Kind::Atexit) == 1 && of(s, Kind::DsoHandle) == 1);
        CHECK(of(s, Kind::OpNew) == 1 && of(s, Kind::OpDelete) == 1 && of(s, Kind::IntHelper) == 1 && of(s, Kind::MemFn) == 1);
        CHECK(of(s, Kind::Project) == 3);
    }
    {
        const Summary s = summarize(kClangRow1Full, count_of(kClangRow1Full), false);
        CHECK(s.lines == 8 && s.undefined == 3 && of(s, Kind::Guard) == 2 && of(s, Kind::Project) == 1);
        const Summary g = summarize(kGccRow1Sized, count_of(kGccRow1Sized), false);
        CHECK(g.lines == 6 && g.undefined == 3 && of(g, Kind::Guard) == 2);
    }
    {
        const Summary w = summarize(kGccRow2, count_of(kGccRow2), false);
        CHECK(w.undefined == 1 && w.weak_undefined == 1 && of(w, Kind::PureVirtual) == 1);
        const Summary e = summarize(kGccRow8Exc, count_of(kGccRow8Exc), false);
        CHECK(e.undefined == 8 && of(e, Kind::Eh) == 7 && of(e, Kind::Rtti) == 1);      /* _ZTIi: -fno-rtti 여도 throw 타입의 typeinfo */
        const Summary r = summarize(kClangRow7Rtti, count_of(kClangRow7Rtti), false);
        CHECK(of(r, Kind::Rtti) == 3);
        const Summary h = summarize(kHostRow3Full, count_of(kHostRow3Full), true);
        CHECK(h.lines == 7 && h.undefined == 4 && of(h, Kind::Atexit) == 1 && of(h, Kind::DsoHandle) == 1 && of(h, Kind::Project) == 2);
        const Summary h5 = summarize(kHostRow5U, count_of(kHostRow5U), true);
        CHECK(h5.undefined == 1 && of(h5, Kind::OpDelete) == 1);
        const Summary h5wrong = summarize(kHostRow5U, count_of(kHostRow5U), false);   /* 정규화를 잊으면 */
        CHECK(of(h5wrong, Kind::OpDelete) == 0);
    }

    /* ---- Part B: guard — 정상 경로 ---- */
    reset_core();
    g_cal = LazyStatic{}; g_cal.ctor = cal_ctor;
    CHECK(*g_cal.get() == 7 && g_cal.ctor_runs == 1);
    CHECK(g_cal.guard == rt::kDone && (guard_first_byte(&g_cal.guard) & 1u) == 1u);   /* busy 지워짐, bit0 세워짐 */
    CHECK(*g_cal.get() == 7 && g_cal.ctor_runs == 1 && rt::guard_acquire(&g_cal.guard) == 0);
    CHECK(g_primask == 0 && rt::g_fault.count == 0);
    {   /* acquire 는 호출자의 PRIMASK 를 그대로 돌려 둔다 */
        uint32_t g = 0;
        g_primask = 1;
        CHECK(rt::guard_acquire(&g) == 1 && g_primask == 1 && (g & rt::kBusy) != 0);
        g_primask = 0;
        rt::guard_release(&g);
        CHECK(g == rt::kDone);
    }
    /* ---- Part B: 검사~표시 사이에 같은 static 을 쓰는 ISR → 이중 생성 금지, 재진입 trap ---- */
    reset_core(); rt::g_fault = {};
    g_cal = LazyStatic{}; g_cal.ctor = cal_ctor;
    arm_preempt(isr_uses_cal);                     /* acquire 안의 guard_load 직후에 IRQ 요청 */
    CHECK(*g_cal.get() == 7);
    CHECK(g_cal.ctor_runs == 1);                   /* 마스크가 없으면 ISR 과 main 이 둘 다 생성 → 2 */
    CHECK(g_isr_runs == 1 && rt::g_fault.count == 1 && rt::g_fault.first == rt::Fault::GuardRecursion);
    CHECK(g_cal.guard == rt::kDone && g_primask == 0);
    /* ---- Part B: 생성자 도중 ISR 이 "다른" static 을 쓰면 정상 — 생성자는 인터럽트를 켠 채 돈다 ---- */
    reset_core(); rt::g_fault = {};
    g_cal = LazyStatic{}; g_cal.ctor = cal_ctor;
    g_other = LazyStatic{}; g_other.ctor = other_ctor;
    g_busy_seen_in_isr = false;
    {   /* cal_ctor 의 isr_window 에서 IRQ: acquire 안의 window 를 지난 뒤(생성자 시작 때) 장전한다 */
        struct Arm { static int ctor(LazyStatic& self) { arm_preempt(isr_uses_other); return cal_ctor(self); } };
        g_cal.ctor = Arm::ctor;
        CHECK(*g_cal.get() == 7 && g_cal.ctor_runs == 1);
    }
    CHECK(g_isr_runs == 1 && g_busy_seen_in_isr);  /* ISR 이 생성 "도중" 에 돌았다 (PRIMASK 를 생성자 내내 잡지 않음) */
    CHECK(g_isr_value == 11 && g_other.ctor_runs == 1 && rt::g_fault.count == 0);
    /* ---- Part B: 생성 도중 ISR 이 "같은" static → trap, 생성은 한 번 ---- */
    reset_core(); rt::g_fault = {};
    {
        struct Arm { static int ctor(LazyStatic& self) { arm_preempt(isr_uses_cal); return cal_ctor(self); } };
        g_cal = LazyStatic{}; g_cal.ctor = Arm::ctor;
        CHECK(*g_cal.get() == 7 && g_cal.ctor_runs == 1);
    }
    CHECK(rt::g_fault.count == 1 && rt::g_fault.first == rt::Fault::GuardRecursion && g_cal.guard == rt::kDone);
    /* ---- Part B: 생성자 안의 재귀 ([stmt.dcl]: UB) → trap ---- */
    reset_core(); rt::g_fault = {};
    g_cal = LazyStatic{}; g_cal.ctor = recursive_ctor;
    (void)g_cal.get();
    CHECK(g_cal.ctor_runs == 1 && rt::g_fault.count == 1 && rt::g_fault.first == rt::Fault::GuardRecursion);
    /* ---- Part B: IRQ 켜기 전에 미리 초기화 → ISR 은 빠른 경로, 고장 없음 ---- */
    reset_core(); rt::g_fault = {};
    g_cal = LazyStatic{}; g_cal.ctor = cal_ctor;
    (void)g_cal.get();
    arm_preempt(isr_uses_cal);
    isr_window();
    CHECK(g_isr_runs == 1 && g_isr_value == 7 && g_cal.ctor_runs == 1 && rt::g_fault.count == 0);
    /* ---- Part B: abort → 다음 호출이 다시 초기화 ---- */
    reset_core(); rt::g_fault = {};
    {
        uint32_t g = 0;
        CHECK(rt::guard_acquire(&g) == 1);
        rt::guard_abort(&g);
        CHECK(g == 0u && rt::guard_acquire(&g) == 1);
        rt::guard_release(&g);
        CHECK(g == rt::kDone && rt::guard_acquire(&g) == 0 && rt::g_fault.count == 0);
    }
    /* ---- Part B: pure virtual trap, 첫 고장 보존 ---- */
    rt::g_fault = {};
    {
        const SensorOps* vptr = &kSensorBaseOps;             /* 기반 생성자 실행 중: 동적 타입 = Sensor */
        (void)vptr->sample(nullptr);
        CHECK(rt::g_fault.count == 1 && rt::g_fault.first == rt::Fault::PureVirtual && rt::g_fault.where == 0x5Au);
        vptr = &kTempOps;                                    /* 파생 생성자: vptr 교체 */
        CHECK(vptr->sample(nullptr) == 25 && rt::g_fault.count == 1);
        uint32_t g = rt::kBusy;                              /* 이어서 다른 고장 → 첫 고장은 그대로 */
        CHECK(rt::guard_acquire(&g) == 0);
        CHECK(rt::g_fault.count == 2 && rt::g_fault.first == rt::Fault::PureVirtual && rt::g_fault.last == rt::Fault::GuardRecursion);
        CHECK(rt::g_fault.where == 0x5Au);
    }

    /* ---- Part C: atexit 등록부 ---- */
    {
        AtexitRegistry<3> reg;
        g_dtor_len = 0;
        CHECK(reg.cxa_atexit(log_dtor, &kA, &g_dso_main) == 0);
        CHECK(reg.aeabi_atexit(&kB, log_dtor, &g_dso_main) == 0);          /* ARM 순서: (객체, 소멸자, dso) */
        CHECK(reg.cxa_atexit(log_dtor, &kC, &g_dso_plugin) == 0 && reg.size() == 3);
        CHECK(reg.aeabi_atexit(&kD, log_dtor, &g_dso_main) != 0 && reg.dropped() == 1 && reg.size() == 3);   /* 가득 참 */
        CHECK(reg.finalize(&g_dso_plugin) == 1 && g_dtor_len == 1 && g_dtor_log[0] == 'C');
        CHECK(reg.finalize(&g_dso_main) == 2 && g_dtor_len == 3 && g_dtor_log[1] == 'B' && g_dtor_log[2] == 'A');  /* LIFO */
        CHECK(reg.finalize(nullptr) == 0 && g_dtor_len == 3);                                                  /* 한 번씩만 */
    }
    /* ---- Part C: 게이트 (check_syms.sh 의 판정을 실제 nm 출력으로) ---- */
    {
        const GateResult fixed = gate(kClangFixed, count_of(kClangFixed), false, kFirmwareAllow);
        CHECK(fixed.checked == 5 && fixed.violations == 0);
        const GateResult legacy = gate(kClangLegacy, count_of(kClangLegacy), false, kFirmwareAllow);
        CHECK(legacy.checked == 11 && legacy.violations == 6 && legacy.first == "_ZdlPvj");
        CHECK(gate(kGccRow2, count_of(kGccRow2), false, kFirmwareAllow).violations == 0);                       /* 스텁이 준다 */
        CHECK(gate(kGccRow2, count_of(kGccRow2), false, bit(Kind::Project)).violations == 1);                   /* w 도 미정의다 */
        const GateResult host = gate(kHostRow3Full, count_of(kHostRow3Full), true, kFirmwareAllow);
        CHECK(host.checked == 4 && host.violations == 2 && host.first == "__cxa_atexit");
        const GateResult dbl = gate(kClangRow11, count_of(kClangRow11), false, kFirmwareAllow);
        CHECK(dbl.violations == 2 && fix_for(classify(dbl.first)) == Fix::FloatOrFixed);
        CHECK(gate(kGccRow3, count_of(kGccRow3), false, kFirmwareAllow).first == "__aeabi_atexit");
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
