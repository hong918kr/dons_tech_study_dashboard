// demo_legacy_main.cpp — host "selftest" for legacy_mc100.cpp (built only by tidy.sh build|san).
// The team ran exactly this for years: it prints plausible numbers and exits 0.
#include <cstdint>
#include <cstdio>
#include <cstring>

struct Telemetry { uint8_t type; uint16_t id; int16_t value; uint16_t crc; };
extern uintptr_t g_periph_base;
extern int g_fault_count;
extern char g_log[64];
extern int g_log_len;
uint8_t duty_from_pct(int pct);
Telemetry decode(const uint8_t* f);
int16_t rpm_to_q3(float rpm);
int rpm_index(uint16_t rpm);
int motor_init(int retries);
int cmd_set_speed(int motor, int rpm);
void report(int motor);
void board_init();
void calibrate_all();

class SampleBuf {                      // same layout as the legacy class (ODR: identical definition)
public:
    SampleBuf(int n) : cap(n), count(0), head(0) { data = new int16_t[n]; }
    ~SampleBuf() { delete data; }
    void push(int16_t v) { data[head] = v; head = (head + 1) % cap; if (count < cap) count++; }
    int16_t mean() const { int s = 0; for (int i = 0; i < count; i++) s += data[i]; return count ? s / count : 0; }
    int16_t* data;
    int cap, count, head;
};

static uint32_t fake_regs[32];
static int probe_fails = 2;
int bus_probe() { return probe_fails-- > 0 ? -1 : 0; }

int main()
{
    g_periph_base = reinterpret_cast<uintptr_t>(fake_regs);
    fake_regs[0x2C / 4] = 1000;                                  // PWM_ARR
    std::printf("duty(50)=%u duty(100)=%u\n", duty_from_pct(50), duty_from_pct(100));
    alignas(4) uint8_t rx[8] = { 0x01, 0x34, 0x12, 0x9C, 0xFF, 0xAD, 0xDE, 0 };   // UART DMA buffer
    Telemetry t = decode(rx);
    std::printf("decode: type=%u id=0x%04X value=%d crc=0x%04X\n", t.type, t.id, t.value, t.crc);
    std::printf("rpm_to_q3(1000)=%d  rpm_index(1000)=%d\n", rpm_to_q3(1000.0f), rpm_index(1000));
    std::printf("motor_init(3)=%d faults=%d\n", motor_init(3), g_fault_count);
    std::printf("cmd_set_speed(0, 6000)=%d CCR0=%u\n", cmd_set_speed(0, 6000), fake_regs[0x34 / 4]);
    board_init();
    calibrate_all();
    { SampleBuf b(4); b.push(10); b.push(20); std::printf("mean=%d\n", b.mean()); }
#ifdef FIELD_CASES                     // inputs the selftest never used — the field did
    std::printf("duty(101)=%u  rpm_to_q3(5000)=%d\n", duty_from_pct(101), rpm_to_q3(5000.0f));
    report(0);
    std::printf("log[%d]: %s", g_log_len, g_log);
    for (int i = 0; i < 4; i++) report(0);                       // log fills up: 21 + 4 * 11 = 65 > 64
    std::printf("log_len=%d (sizeof g_log = 64)\n", g_log_len);
    report(0);                                                   // size argument = 64 - 65 → SIZE_MAX
#endif
    std::printf("selftest ok\n");
    return 0;
}
