/* CPP-B08 추가 연습 — 한 번에 검증
 * 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
 * LP64 (arm64 mac) 과 ILP32 (Cortex-M) 기대값을 함께 적었다. ILP32 값은
 * clang --target=thumbv7em-none-eabi -Xclang -fdump-record-layouts 로 확인한 값.
 */
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

constexpr bool LP64 = sizeof(void*) == 8;

template <class T, class M>
std::size_t addr_off(const T& obj, const M& member) {
    return reinterpret_cast<std::uintptr_t>(&member) - reinterpret_cast<std::uintptr_t>(&obj);
}

/* 연습 1 — virtual 이 있는 클래스의 멤버 재배치 */
struct SensorBad  { virtual void read() {} uint8_t id; uint64_t ts; uint8_t flags; uint32_t val; };
struct SensorGood { virtual void read() {} uint64_t ts; uint32_t val; uint8_t id; uint8_t flags; };

/* 연습 2 — [[no_unique_address]] 로 compressed pair */
struct Empty {};
struct Tag {};
template <class A, class B>
struct Pair { [[no_unique_address]] A a; [[no_unique_address]] B b; };

/* 연습 3 — 꼬리 패딩 재사용: 베이스 크기만큼 memcpy 하면 파생 멤버를 덮는가? */
struct PodBase    { uint32_t a; uint8_t b; };
struct NonPodBase { NonPodBase() {} uint32_t a; uint8_t b; };
struct AfterPod    : PodBase    { uint8_t c; };
struct AfterNonPod : NonPodBase { uint8_t c; };
template <class Derived, class Base>
bool base_copy_would_clobber(const Derived& d) {
    return addr_off(d, d.c) < sizeof(Base);   // 파생 멤버가 베이스 sizeof 범위 안에 있으면 덮인다
}

/* 연습 4 — CRC/해시를 바이트로 계산해도 되는 타입 */
template <class T>
concept CrcSafe = std::is_trivially_copyable_v<T> && std::has_unique_object_representations_v<T>;
struct CfgPadded { uint32_t magic; uint8_t ver; uint8_t flags; };              // 끝에 2바이트 패딩
struct CfgPacked { uint32_t magic; uint8_t ver; uint8_t flags; uint16_t rsvd; }; // 패딩 자리를 명시
struct CfgFloat  { uint32_t magic; float gain; };                               // float 는 표현이 유일하지 않음

/* 연습 5 — 접근 지정자: 섞으면 탈락, 모두 private 이면 standard-layout */
class MixedAccess { public: uint32_t a = 0; uint32_t get_b() const { return b; } private: uint32_t b = 0; };
class AllPrivate  { public: uint32_t get_a() const { return a; } uint32_t get_b() const { return b; }
                    private: uint32_t a = 0; uint32_t b = 0; };

/* 연습 6 — 빈 베이스 둘 (MSVC 는 __declspec(empty_bases) 필요) */
struct E1 {};
struct E2 {};
struct TwoEmptyBases : E1, E2 { uint32_t x; };

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    /* 연습 1: arm64 32 → 24, Cortex-M 은 24 → 24 (uint64_t 가 8 정렬이라 이득 없음) */
    CHECK(sizeof(SensorBad) == (LP64 ? 32u : 24u));
    CHECK(sizeof(SensorGood) == 24u);
    CHECK(alignof(SensorGood) == 8u);

    /* 연습 2 */
    CHECK(sizeof(Pair<Empty, Tag>) == 1);       // 다른 타입 → 겹칠 수 있음
    CHECK(sizeof(Pair<Empty, Empty>) == 2);     // 같은 타입 → 주소가 달라야 함
    CHECK(sizeof(Pair<Empty, uint32_t>) == 4);  // 빈 쪽은 0바이트

    /* 연습 3 */
    AfterPod p{};
    AfterNonPod n{};
    CHECK(!(base_copy_would_clobber<AfterPod, PodBase>(p)));
    CHECK((base_copy_would_clobber<AfterNonPod, NonPodBase>(n)));
    CHECK(addr_off(n, n.c) == 5);

    /* 연습 4 */
    CHECK(!CrcSafe<CfgPadded>);
    CHECK(CrcSafe<CfgPacked>);
    CHECK(!CrcSafe<CfgFloat>);
    CHECK(sizeof(CfgPadded) == sizeof(CfgPacked));   // 크기는 같다 — 차이는 "패딩 바이트 값이 정해져 있나"

    /* 연습 5 */
    CHECK(!std::is_standard_layout_v<MixedAccess>);
    CHECK(std::is_standard_layout_v<AllPrivate>);
    CHECK(sizeof(MixedAccess) == sizeof(AllPrivate));   // 레이아웃 크기는 같아도 "보장"이 다르다

    /* 연습 6 */
    CHECK(sizeof(TwoEmptyBases) == 4);
    CHECK(std::is_standard_layout_v<TwoEmptyBases>);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
