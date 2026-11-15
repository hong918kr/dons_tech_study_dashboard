/* CPP-A08 sensor.cpp — virtual 판과 링크 심 판의 드라이버 본체를 이 TU 에서 한 번만 인스턴스화한다.
 * 이 .o 는 i2c::write_read 를 "정의하지 않고" 부르기만 한다 (nm: U). 정의는 링크 때 i2c_hw.o 또는 i2c_fake.o 가 준다. */
#define SENSOR_INSTANTIATE
#include "sensor.hpp"

template class TempSensor<II2c>;
template class TempSensor<LinkBus>;
