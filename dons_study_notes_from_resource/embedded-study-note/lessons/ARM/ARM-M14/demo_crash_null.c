/* ARM-M14 demo 1 — NULL 구조체 멤버 접근 (fault address = 멤버 offset)
 *   cc -std=c11 -O1 -g -fno-stack-protector demo_crash_null.c -o /tmp/ARM-M14_null
 *   lldb -b -s demo_crash_null.lldb /tmp/ARM-M14_null   > crash_null.txt
 * 센서 테이블에서 id 로 찾고, 없으면 NULL 을 돌려준다. 호출자는 NULL 검사를 빼먹었다.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct sensor {                 /* offset */
    uint32_t id;                /* 0x00 */
    uint32_t flags;             /* 0x04 */
    const char *name;           /* 0x08 */
    int32_t  last_mC;           /* 0x10  마지막 온도 (milli-°C) */
    uint32_t pad;               /* 0x14 */
    int32_t (*read)(void);      /* 0x18  드라이버 콜백 */
    uint64_t sample_count;      /* 0x20 */
};

static int32_t read_tmp117(void) { return 23125; }

static struct sensor g_sensors[] = {
    {1, 0, "tmp117", 0, 0, read_tmp117, 0},
    {2, 0, "board", 0, 0, read_tmp117, 0},
};

__attribute__((noinline))
struct sensor *find_sensor(uint32_t id) {
    for (size_t i = 0; i < sizeof g_sensors / sizeof g_sensors[0]; i++)
        if (g_sensors[i].id == id) return &g_sensors[i];
    return NULL;                                   /* 못 찾으면 NULL */
}

__attribute__((noinline))
int32_t poll_sensor(struct sensor *s) {
    int32_t v = s->read();                         /* ← s == NULL 이면 여기서 죽는다 */
    s->last_mC = v;
    s->sample_count++;
    return v;
}

int main(int argc, char **argv) {
    uint32_t id = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 1;
    struct sensor *s = find_sensor(id);            /* id=7 → NULL */
    printf("poll id=%u -> %d\n", id, poll_sensor(s));
    return 0;
}
