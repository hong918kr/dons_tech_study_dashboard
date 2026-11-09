// CPP-M08 demo_lifetime_dse.cpp — operator new 에서 0 으로 채우면 생성자가 안 채워도 되나? / 소멸자의 포이즌 쓰기는 남나?
using size_t = decltype(sizeof 0);
extern "C" void* memset(void*, int, size_t);

extern unsigned char g_block[64];
void pool_free(void*);

struct Zeroed {
    unsigned a;
    unsigned b;
    Zeroed() {}                                  // 일부러 아무것도 초기화하지 않는다
    static void* operator new(size_t n) noexcept { memset(g_block, 0, n); return g_block; }
};

unsigned read_fresh() {
    Zeroed* z = new Zeroed;
    return z->a;                                 // 불확정 값 읽기 — UB. "operator new 가 0 으로 채웠잖아"는 통하지 않는다
}

struct Poisoned {
    unsigned magic = 0x600D;
    ~Poisoned() { magic = 0xDEAD; }              // 소멸자에서 "죽었음" 표시
    static void operator delete(void* p) noexcept { pool_free(p); }
};

void kill(Poisoned* p) { delete p; }
