// CPP-M05 demo_errors.cpp — CRTP 오용이 어떤 진단을 내는지
//   c++ -std=c++20 -fsyntax-only demo_errors.cpp                    # 기본: 컴파일 성공 (에러 블록 없음)
//   c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp     # 1..6 을 바꿔 가며
//   c++ -std=c++17 -fsyntax-only -DSHOW_CXX17_HOLE demo_errors.cpp  # 성공 (C++17 aggregate 구멍)
//   c++ -std=c++20 -fsyntax-only -DSHOW_CXX17_HOLE demo_errors.cpp  # 에러
using u8 = unsigned char;

template <class D>
class Guarded {
public:
    bool put(u8 b) { return self().put_impl(b); }
private:
    friend D;
    Guarded() = default;
    D& self() {
#if __cplusplus >= 202002L
        static_assert(requires(D& d, u8 b) { { d.put_impl(b) }; }, "Guarded<D>: D must define bool put_impl(u8)");
#endif
        return static_cast<D&>(*this);
    }
};
template <class D>
class Naive {                       // 가드 없는 CRTP
public:
    bool put(u8 b)  { return static_cast<D&>(*this).put_impl(b); }
    bool send(u8 b) { return put(b); }
};

struct Good : Guarded<Good> { bool put_impl(u8) { return true; } };
struct Wrong : Guarded<Good> {};    // 복붙 실수

#if __cplusplus >= 202002L           // requires 절 버전 (C++20): 깊은 instantiation 전에 호출 지점에서 거절
template <class D>
class Constrained {
public:
    bool put(u8 b) requires requires(D& d) { { d.put_impl(b) }; } { return static_cast<D&>(*this).put_impl(b); }
private:
    friend D;
    Constrained() = default;
};
#endif

#if __cplusplus >= 202002L           // private impl 도 requires 절 안에서 friend 로 보이는가
class PrivOk : public Constrained<PrivOk> {
    friend Constrained<PrivOk>;
    bool put_impl(u8) { return true; }
};
inline bool priv_ok() { PrivOk p; return p.put(1); }
#endif

// protected 생성자만으로는 잘못된 기반을 못 막는다 — 아래는 컴파일된다 (기본 빌드 성공)
template <class D> class Prot { public: bool put(u8) { return true; } protected: Prot() = default; };
struct WrongProt : Prot<Good> {};
inline void protected_is_not_enough() { WrongProt w; (void)w; }

#ifdef SHOW_CXX17_HOLE
void hole() { Wrong w{}; (void)w; }
#endif
#if SHOW_ERROR == 1
void e1() { Wrong w; (void)w; }     // 잘못된 기반 → 생성 불가
#elif SHOW_ERROR == 2
struct NoImplN : Naive<NoImplN> {};
bool e2() { NoImplN x; return x.send(1); }   // 가드 없음: 호출 깊은 곳에서 에러
#elif SHOW_ERROR == 3
struct NoImplG : Guarded<NoImplG> {};
bool e3() { NoImplG x; return x.put(1); }    // static_assert 가드
#elif SHOW_ERROR == 4
struct A : Guarded<A> { bool put_impl(u8) { return true; } };
struct B : Guarded<B> { bool put_impl(u8) { return true; } };
void e4() { A a; B b; Guarded<A>* arr[2] = {&a, &b}; (void)arr; }   // 이질 배열
#elif SHOW_ERROR == 6
template <class D> struct Early { static_assert(sizeof(D) > 0, "D complete?"); };  // 기반 클래스 인스턴스화 시점엔 D 가 불완전
struct E6 : Early<E6> {};
#elif SHOW_ERROR == 5
struct NoImplC : Constrained<NoImplC> {};
bool e5() { NoImplC x; return x.put(1); }    // requires 절 — 호출 지점에서 1개 에러
#endif
