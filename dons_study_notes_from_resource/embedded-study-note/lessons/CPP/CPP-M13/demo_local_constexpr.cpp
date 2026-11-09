// CPP-M13 demo_local_constexpr.cpp — 함수 안 constexpr 배열: static 을 안 붙이면 어떻게 되나
//  (std 헤더 없음 → 맥 clang --target=thumbv7em-none-eabi 로 바로 컴파일 가능)
using u32 = unsigned int;
struct Table { u32 v[256]; };

constexpr Table make() {
    Table t{};
    for (u32 i = 0; i < 256; ++i) {
        u32 c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
        t.v[i] = c;
    }
    return t;
}

void use(const Table&);               // 외부 함수: 표의 "주소"가 필요해지는 경우

extern "C" u32 lut_local(u32 i) {     // automatic 변수: 호출마다 새 객체
    constexpr Table t = make();
    return t.v[i & 0xFFu];
}

extern "C" u32 lut_static(u32 i) {    // static storage: 객체 1개
    static constexpr Table t = make();
    return t.v[i & 0xFFu];
}

extern "C" void pass_local()  { constexpr Table t = make();        use(t); }
extern "C" void pass_static() { static constexpr Table t = make(); use(t); }
