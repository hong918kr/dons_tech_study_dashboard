// CPP-B13 demo_flags.cpp — 같은 소스를 4가지 플래그 조합으로 빌드해 크기/섹션을 비교한다.
//   build.sh 가 {exc on/off} x {rtti on/off} 4개 바이너리를 만들고 size -m / nm 결과를 보여준다.
// 소스는 하나. 기능 테스트 매크로(__cpp_exceptions, __cpp_rtti)로 "꺼졌을 때의 대체 경로"를 고른다
// — 실제 라이브러리(ETL, CMSIS-C++ 래퍼 등)가 쓰는 방식 그대로.
#if defined(DEMO_FREESTANDING)        // build.sh arm: 헤더 없는 Cortex-M 타깃용 (.o 만 만든다)
#include <stdint.h>
extern "C" int puts(const char*);
extern "C" int printf(const char*, ...);
extern "C" [[noreturn]] void abort();
namespace std { using ::abort; using ::puts; using ::printf; using ::uint8_t; }
#else
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#if defined(__cpp_rtti)
#include <typeinfo>
#endif
#endif

struct SensorError { int code; };

enum class Kind : std::uint8_t { Imu, Baro };

struct Sensor {                               // 다형 클래스 → vtable 필요
    explicit Sensor(Kind k) : kind(k) {}
    virtual ~Sensor() = default;
    virtual int read() = 0;
    const Kind kind;
};
struct Imu final : Sensor {
    Imu() : Sensor(Kind::Imu) {}
    int read() override { return ++samples; }
    int samples = 0;
};
struct Baro final : Sensor {
    Baro() : Sensor(Kind::Baro) {}
    int read() override { return 1013; }
};

// 소멸자가 있는 지역 객체: 예외가 켜져 있으면 "throw 가 지나갈 때 정리" 하는 landing pad 가 생긴다
struct BusLock {
    BusLock()  { std::puts("lock"); }
    ~BusLock() { std::puts("unlock"); }
};

// "던질 수도 있는" 함수
[[gnu::noinline]] int read_checked(Sensor& s) {
    BusLock lock;
    int v = s.read();
    if (v > 1000 && s.kind == Kind::Imu) {
#if defined(__cpp_exceptions)
        throw SensorError{-5};
#else
        std::abort();                         // -fno-exceptions: 던질 수 없으니 멈춘다
#endif
    }
    return v;
}

[[gnu::noinline]] int imu_samples(Sensor* s) {
#if defined(__cpp_rtti)
    if (auto* imu = dynamic_cast<Imu*>(s)) return imu->samples;   // RTTI 사용
    return -1;
#else
    return s->kind == Kind::Imu ? static_cast<Imu*>(s)->samples : -1; // 태그 + static_cast
#endif
}

int main(int argc, char**) {
    Imu imu; Baro baro;
    Sensor* list[] = {&imu, &baro};
    int sum = 0;
#if defined(__cpp_exceptions)
    try {
        for (Sensor* s : list) sum += read_checked(*s);
    } catch (const SensorError& e) {
        sum = e.code;
    }
#else
    for (Sensor* s : list) sum += read_checked(*s);
#endif
    sum += imu_samples(list[argc & 1]);
#if defined(__cpp_rtti) && !defined(DEMO_FREESTANDING)
    std::printf("typeid: %s\n", typeid(*list[0]).name());
#endif
    std::printf("sum=%d\n", sum);
    return 0;
}
