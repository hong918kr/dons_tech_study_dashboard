// legacy_mc100.cpp — MC100 BLDC motor controller firmware (fictional), "field-proven since 2016".
// CPP-A09 analysis target: intentionally violating "C with classes" firmware. DO NOT FIX THIS FILE.
// It is built only by tidy.sh (team flags: exceptions + RTTI ON, -Wall -Wextra -Werror → 0 warnings).
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

#define PWM_ARR   0x2C
#define PWM_CCR   0x34
#define HALL_DR   0x10
#define NTC_DR    0x14
#define MAX_RPM   12000
#define N_MOTORS  2

uintptr_t g_periph_base = 0x40010000;      // host selftest points this at a fake register block
int g_fault_count;
char g_log[64];
int g_log_len;

#define REG(off) (*(volatile uint32_t*)(g_periph_base + (off)))

// ---------------------------------------------------------------- logging
void mc_log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    g_log_len += vsnprintf(g_log + g_log_len, sizeof(g_log) - g_log_len, fmt, ap);
    va_end(ap);
}

// ---------------------------------------------------------------- Part A: conversions, casts, unions
uint8_t duty_from_pct(int pct)
{
    uint8_t d = pct * 255 / 100;
    return d;
}

struct Telemetry { uint8_t type; uint16_t id; int16_t value; uint16_t crc; };

// wire frame (little endian): [type][id lo][id hi][value lo][value hi][crc lo][crc hi]
Telemetry decode(const uint8_t* f)
{
    Telemetry t;
    t.type = f[0];
    t.id = *(const uint16_t*)(f + 1);
    t.value = *reinterpret_cast<const int16_t*>(f + 3);
    t.crc = *(uint16_t*)(f + 5);
    return t;
}

union FloatBits { float f; uint32_t u; };

uint32_t float_bits(float x)
{
    FloatBits b;
    b.f = x;
    return b.u;
}

int16_t rpm_to_q3(float rpm)          // telemetry speed in Q12.3
{
    int16_t q = rpm * 8.0f;
    return q;
}

// ---------------------------------------------------------------- Part B: classes and lifetime
class SampleBuf {
public:
    SampleBuf(int n) : cap(n), count(0), head(0) { data = new int16_t[n]; }
    ~SampleBuf() { delete data; }
    void push(int16_t v)
    {
        data[head] = v;
        head = (head + 1) % cap;
        if (count < cap) count++;
    }
    int16_t mean() const
    {
        int s = 0;
        for (int i = 0; i < count; i++) s += data[i];
        return count ? s / count : 0;
    }
    int16_t* data;
    int cap, count, head;
};

class Pid {
public:
    Pid(float p, float i, float d) : kp(p), ki(i), kd(d) {}
    void reset() { integ = 0; d_filt = 0; prev = 0; }
    float step(float err)
    {
        integ += err;
        if (integ > 1000) integ = 1000;
        if (integ < -1000) integ = -1000;
        d_filt = 0.9 * d_filt + 0.1 * (err - prev);
        prev = err;
        return kp * err + ki * integ + kd * d_filt;
    }
private:
    float kp, ki, kd;
    float integ, d_filt, prev;
};

class Sensor {
public:
    virtual int16_t read() = 0;
    ~Sensor() {}
};

class HallSensor : public Sensor {
public:
    HallSensor() : hist(new SampleBuf(8)) {}
    ~HallSensor() { delete hist; }
    int16_t read() { hist->push((int16_t)REG(HALL_DR)); return hist->mean(); }
    SampleBuf* hist;
};

class NtcSensor : public Sensor {
public:
    int16_t read() { return (int16_t)REG(NTC_DR); }
};

Sensor* g_sensors[N_MOTORS];

void board_init()
{
    g_sensors[0] = new HallSensor;
    g_sensors[1] = new NtcSensor;
}

void calibrate_all()
{
    for (int i = 0; i < N_MOTORS; i++) {
        HallSensor* h = dynamic_cast<HallSensor*>(g_sensors[i]);
        if (h) h->hist->count = 0;
    }
}

void set_speed(int motor, int rpm)
{
    if (rpm > MAX_RPM) throw std::out_of_range("rpm");
    REG(PWM_CCR + motor * 4) = rpm * REG(PWM_ARR) / MAX_RPM;
}

int cmd_set_speed(int motor, int rpm)
{
    try {
        set_speed(motor, rpm);
    } catch (...) {
        g_fault_count++;
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------- Part C: control flow
const uint16_t k_rpm_lut[16] = { 0, 300, 600, 900, 1200, 1800, 2400, 3000,
                                 3600, 4800, 6000, 7200, 8400, 9600, 10800, 12000 };

int lut_find(const uint16_t* t, int lo, int hi, uint16_t rpm)   // first index with t[i] >= rpm
{
    if (lo >= hi) return lo;
    int mid = (lo + hi) / 2;
    if (t[mid] < rpm) return lut_find(t, mid + 1, hi, rpm);
    return lut_find(t, lo, mid, rpm);
}

int rpm_index(uint16_t rpm) { return lut_find(k_rpm_lut, 0, 16, rpm); }

int bus_probe();                       // board file: 0 = device answered

int motor_init(int retries)
{
    int tries = 0;
again:
    if (bus_probe() != 0) {
        tries++;
        if (tries < retries) goto again;
        g_fault_count++;
        mc_log("init fail %d\n", tries);
        return -1;
    }
    mc_log("init ok %d\n", tries);
    return tries;
}

void report(int motor)
{
    float duty = REG(PWM_CCR + motor * 4) * 100.0f / REG(PWM_ARR);
    mc_log("m%d duty=%d%%\n", motor, duty);
}
