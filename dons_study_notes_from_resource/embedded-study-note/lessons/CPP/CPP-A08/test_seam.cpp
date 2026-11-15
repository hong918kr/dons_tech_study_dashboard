/* CPP-A08 test_seam.cpp — 진짜로 파일을 나눈 링크 심 테스트 (link_seam.sh 가 sensor.o + i2c_fake.o 와 링크).
 * 이 TU 는 i2c_fake.cpp 의 존재를 모른다: link_fake() 선언만 본다. 드라이버(sensor.o)도 모른다. 링커만 안다. */
#include <cstdio>
#include "fake_tmp.hpp"
#include "sensor.hpp"

FakeTmp& link_fake();   // i2c_fake.cpp 가 정의

static int g_pass = 0, g_total = 0;
#define CHECK(...) do { ++g_total; if (__VA_ARGS__) ++g_pass; \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__); } while (0)

int main() {
    LinkBus bus;
    {   // 1. init → 3 트랜잭션이 i2c_fake.o 의 가짜로 갔다
        link_fake().reset();
        TempSensor<LinkBus> s{bus};
        CHECK(s.init() == DrvErr::Ok);
        CHECK(link_fake().ncalls() == 3u);
        CHECK(link_fake().regs[ts::kRegConfig] == ts::kConfigCont8);
    }
    {   // 2. NACK 2번 → 재시도로 성공
        link_fake().reset();
        TempSensor<LinkBus> s{bus};
        CHECK(s.init() == DrvErr::Ok);
        link_fake().inject(Fault::Nack, 2);
        int32_t t = 0;
        CHECK(s.read_milli_c(t) == DrvErr::Ok);
        CHECK(t == 25'000);
        CHECK(s.stats().retries == 2u);
    }
    {   // 3. 타임아웃은 재시도 없음
        link_fake().reset();
        TempSensor<LinkBus> s{bus};
        CHECK(s.init() == DrvErr::Ok);
        link_fake().inject(Fault::Timeout);
        int32_t t = 0;
        CHECK(s.read_milli_c(t) == DrvErr::Timeout);
        CHECK(link_fake().ncalls() == 4u);
    }
    {   // 4. 테스트 격리: reset 을 빼먹으면 앞 테스트의 로그가 남아 있다
        CHECK(link_fake().ncalls() == 4u);   // reset 안 함 → 3번 테스트의 흔적
        link_fake().reset();
        CHECK(link_fake().ncalls() == 0u);
    }
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
