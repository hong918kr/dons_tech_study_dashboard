#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

/* ---- 1. 포인터 기본 ---- */
static void demo_pointer(void)
{
    int x = 42;
    int *p = &x;
    printf("x=%d  *p=%d  same=%d\n", x, *p, (&x == p));
    *p = 7;
    printf("after *p=7 : x=%d\n", x);
}

/* ---- 2. out 파라미터 ---- */
static int sensor_read(float *out)
{
    static int calls = 0;
    calls++;
    if (calls == 2)
        return -1;            /* *out 은 건드리지 않는다 */
    *out = 12.5f * (float)calls;
    return 0;
}

static void demo_out(void)
{
    float v = -1.0f;
    for (int i = 0; i < 3; i++) {
        if (sensor_read(&v) == 0) printf("read ok  v=%.1f\n", (double)v);
        else                      printf("read fail v=%.1f (그대로)\n", (double)v);
    }
}

/* ---- 3. 구조체 ---- */
struct GpsFix {
    int      status;
    double   lat, lon;
    float    hdop;
    uint64_t timestamp;
};

static struct GpsFix make_fix(void)
{
    struct GpsFix f = { 2, 37.5, -122.5, 1.0f, 1000 };
    return f;                 /* 40바이트 전체를 값으로 복사해 돌려준다 */
}

static void bump_by_value(struct GpsFix f) { f.lat += 1.0; }
static void bump_by_ptr(struct GpsFix *f)  { f->lat += 1.0; }

static void demo_struct(void)
{
    struct GpsFix a = make_fix();
    bump_by_value(a);
    printf("by value : lat=%.1f\n", a.lat);
    bump_by_ptr(&a);
    printf("by ptr   : lat=%.1f\n", a.lat);
    struct GpsFix b = a;      /* 구조체 대입 = 필드 전체 복사 (한 번에 되는 건 아니다) */
    printf("copy     : lat=%.1f ts=%llu\n", b.lat, (unsigned long long)b.timestamp);
}

/* ---- 4. 배열 decay + (out, max, found) ---- */
static void array_size_demo(void)
{
    int a[5] = {1,2,3,4,5};
    printf("sizeof a = %zu, 원소 %zu개\n", sizeof a, sizeof a / sizeof a[0]);
}

static void takes_array(int a[5])
{
    printf("함수 안 sizeof a = %zu  <- 포인터 크기다\n", sizeof a);
}

static int scan_aps(int *out, size_t max, size_t *found)
{
    static const int seen[7] = {10,20,30,40,50,60,70};
    *found = 7;                                /* 본 개수 */
    size_t w = (*found < max) ? *found : max;   /* 쓴 개수 */
    for (size_t i = 0; i < w; i++) out[i] = seen[i];
    return 0;
}

static void demo_scan(void)
{
    int buf[4];
    size_t found = 0;
    scan_aps(buf, 4, &found);
    size_t n = (found < 4) ? found : 4;
    printf("found=%zu  written=%zu  first=%d last=%d\n", found, n, buf[0], buf[n-1]);
}

/* ---- 5. const ---- */
static void demo_const(void)
{
    char buf[8] = "abc";
    const char *p  = buf;        /* 대상이 const: *p = 'x' 금지, p++ 허용 */
    char *const q  = buf;        /* 포인터가 const: *q = 'x' 허용, q++ 금지 */
    const char *const r = buf;   /* 둘 다 const */
    p++;
    *q = 'A';
    printf("const: p=%c *q=%c r=%c\n", *p, *q, *r);
}

/* ---- 6. static 세 가지 ---- */
static int  g_file_scope = 1;      /* (1)(3) 파일 스코프 + 내부 링키지 */
static int  counter(void) { static int n = 0; return ++n; }   /* (2) 지역 수명 */

/* ---- 7. 함수 포인터와 ops ---- */
struct clock_ops {
    uint64_t (*now_us)(void);
};
static uint64_t fake_now(void) { return 1234567; }
static const struct clock_ops g_fake_clock = { .now_us = fake_now };

static uint64_t stamp(const struct clock_ops *ops) { return ops->now_us(); }

/* ---- 8. 문자열 ---- */
static void copy_ssid_str(char *dst, size_t dstsz, const char *src)
{
    int n = snprintf(dst, dstsz, "%s", src);
    if (n < 0 || (size_t)n >= dstsz)
        printf("  (잘렸다: %d자 필요, 버퍼 %zu)\n", n, dstsz);
}

static void demo_string(void)
{
    char ssid[8];
    copy_ssid_str(ssid, sizeof ssid, "verkada-guest");
    printf("ssid=\"%s\" len=%zu\n", ssid, strlen(ssid));

    char fixed[33];
    const char src[33] = "lobby-cam";
    memcpy(fixed, src, sizeof fixed);
    fixed[sizeof fixed - 1] = '\0';
    printf("fixed=\"%s\"\n", fixed);
}

int main(void)
{
    demo_pointer();
    demo_out();
    demo_struct();
    array_size_demo();
    takes_array((int[5]){1,2,3,4,5});
    demo_scan();
    demo_const();
    printf("static: %d %d %d %d\n", g_file_scope, counter(), counter(), counter());
    printf("ops: now=%llu\n", (unsigned long long)stamp(&g_fake_clock));
    printf("offsetof(GpsFix, timestamp)=%zu sizeof=%zu\n",
           offsetof(struct GpsFix, timestamp), sizeof(struct GpsFix));
    demo_string();
    return 0;
}
