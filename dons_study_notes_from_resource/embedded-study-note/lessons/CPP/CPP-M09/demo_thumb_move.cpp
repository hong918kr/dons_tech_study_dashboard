// CPP-M09 demo_thumb_move — Cortex-M4 에서 copy 와 move 가 같은 코드인지, 핸들은 얼마나 작은지 본다. <헤더 없음>
// build(맥): clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -S demo_thumb_move.cpp -o -
using u8  = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;

template <class T> constexpr T&& mv(T& x) noexcept { return static_cast<T&&>(x); }

struct Frame { u8 bytes[256]; u16 len; };               // trivially copyable, sizeof == 258

// 1) 256바이트 프레임: 복사 대입 vs 이동 대입
void copy_frame(Frame& d, const Frame& s) { d = s; }
void move_frame(Frame& d, Frame& s)        { d = mv(s); }

// 2) move 에서 len 만 비우는 비용 (ExchangeOnMove 가 하는 일)
struct TxDesc {
    u8  bytes[64];
    u16 len;
    TxDesc& operator=(TxDesc&& o) noexcept {
        __builtin_memcpy(bytes, o.bytes, sizeof bytes);
        len = o.len;
        o.len = 0;
        return *this;
    }
};
void move_tx(TxDesc& d, TxDesc& s) { d = mv(s); }

// 3) 1바이트 핸들의 이동 대입 (release 는 외부 함수: 인라인 안 되게)
void pool_free(u8 idx);
struct Handle {
    u8 idx = 0xFF;
    Handle& operator=(Handle&& o) noexcept {
        if (this != &o) {
            if (idx != 0xFF) pool_free(idx);
            idx = o.idx;
            o.idx = 0xFF;
        }
        return *this;
    }
};
void move_handle(Handle& d, Handle& s) { d = mv(s); }

// 4) 값으로 받는 함수에 std::move 로 넘기기: move 여도 스택에 258바이트가 복사된다
void consume_frame(Frame f);
void by_value(Frame& f) { consume_frame(mv(f)); }

// 5) 참조로 넘기면 복사가 없다
void consume_ref(const Frame& f);
void by_ref(const Frame& f) { consume_ref(f); }
