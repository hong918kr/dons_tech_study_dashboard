// CPP-M13 demo_fold_probe.cpp — constexpr 를 안 썼을 때 최적화기가 동적 초기화를 지울 수 있나? (보장 없음)
// (std 헤더 없음) -DCASE=1: 단순 함수 호출 / -DCASE=2: 작은 루프로 만든 표
using u32 = unsigned int;
#if CASE == 1
inline u32 make_value() { return 40u + 2u; }
extern const u32 g = make_value();
#else
struct T { u32 v[16]; };
inline T make_small() { T t{}; for (u32 i = 0; i < 16; ++i) t.v[i] = i * i; return t; }
extern const T g = make_small();
#endif
