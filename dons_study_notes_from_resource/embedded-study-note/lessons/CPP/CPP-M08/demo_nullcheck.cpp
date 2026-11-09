// CPP-M08 demo_nullcheck.cpp — 헤더 없음 (맥 clang --target=thumbv7em-none-eabi 로도 컴파일됨)
//   USE_NOEXCEPT 없음: operator new 가 noexcept 가 아니다 → 컴파일러는 "절대 nullptr 아님"으로 믿는다
//   -DUSE_NOEXCEPT   : noexcept 있음 → new-식이 생성자 앞에서 null 검사 ([expr.new])
using size_t = decltype(sizeof 0);

void* pool_alloc();          // 다른 TU: 고갈이면 nullptr
void  pool_free(void*);
void  oom();                 // 다른 TU: 고갈 처리

#ifdef USE_NOEXCEPT
#define NE noexcept
#else
#define NE
#endif

struct Message {
    unsigned id;
    Message() : id(0xAA) {}
    static void* operator new(size_t) NE { return pool_alloc(); }
    static void  operator delete(void* p) noexcept { pool_free(p); }
};

Message* make() { return new Message; }

// 호출자가 정성껏 null 검사를 해도 …
Message* make_checked() {
    Message* m = new Message;
    if (m == nullptr) oom();
    return m;
}
