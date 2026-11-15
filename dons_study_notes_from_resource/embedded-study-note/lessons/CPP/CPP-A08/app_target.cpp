/* CPP-A08 app_target.cpp — 같은 펌웨어를 세 이음매로 빌드해 크기를 잰다 (-DVARIANT=1/2/3, size_targets.sh).
 *   1 = TempSensor<HwBus>   (템플릿 주입, 이 TU 에서 전부 인스턴스화)
 *   2 = TempSensor<II2c>    (virtual 주입, 본체는 sensor.cpp)
 *   3 = TempSensor<LinkBus> (링크 심, 본체는 sensor.cpp, i2c::* 는 i2c_hw.cpp) */
#include "hw_i2c.hpp"
#include "sensor.hpp"

#ifndef VARIANT
#define VARIANT 1
#endif
#if VARIANT == 1
static HwBus g_bus;
static TempSensor<HwBus> g_dev{g_bus};
#elif VARIANT == 2
static HwBusV g_bus;
static TempSensor<II2c> g_dev{g_bus};
#else
static LinkBus g_bus;
static TempSensor<LinkBus> g_dev{g_bus};
#endif

volatile int32_t g_temp_mc;
volatile uint32_t g_errors;

extern "C" [[noreturn]] void _start() {
    while (g_dev.init() != DrvErr::Ok) g_errors = g_errors + 1;
    for (;;) {
        int32_t t = 0;
        if (g_dev.read_milli_c(t) == DrvErr::Ok) g_temp_mc = t;
        else g_errors = g_errors + 1;
    }
}
