// CPP-B09 demo — 함수 내부 static (Meyers singleton) 의 guard 비용 관찰 (컴파일만, 링크 X)
//   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_guard.cpp                         # 기본: thread-safe
//   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -S -o - demo_guard.cpp  # guard 바이트만
//   clang++ --target=armv7em-none-eabi -mthumb -mcpu=cortex-m4 ... -c 후 objdump -d -r            # Cortex-M 예상
struct SensorCal {
    unsigned offset;
    SensorCal();                              // 다른 TU: OTP 레지스터를 읽는 런타임 생성자
};
SensorCal& sensor_cal() {                     // 동적 초기화 → guard 변수 + __cxa_guard_*
    static SensorCal inst;
    return inst;
}

struct BoardInfo {
    unsigned rev;
    constexpr BoardInfo(unsigned r) : rev(r) {}
};
BoardInfo& board_info() {                     // 상수 초기화 → guard 없음, 그냥 .data 주소 반환
    static BoardInfo inst{3};
    return inst;
}
