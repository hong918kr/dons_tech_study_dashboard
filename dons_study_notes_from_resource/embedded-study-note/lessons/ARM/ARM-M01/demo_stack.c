/* ARM-M01 demo_stack.c — lldb 로 "호출 순간의 스택 인자" 를 눈으로 본다 (테스트 아님).
 * build: cc -std=c11 -g -O1 demo_stack.c solution.S -o /tmp/ARM-M01_stack
 * run:   lldb -b -s demo_stack.lldb /tmp/ARM-M01_stack      (기록: demo_stack_lldb.txt)
 */
#include <stdint.h>
#include <stdio.h>
typedef struct { uint64_t ts; int32_t ax, ay, az; uint32_t flags; uint64_t sum; } imu_sample;
long sum10(long, long, long, long, long, long, long, long, long, long);
long mixed_stack(long, long, long, long, long, long, long, long, signed char, short, int, long);
imu_sample make_imu(uint64_t ts, int32_t ax, int32_t ay, int32_t az);
int main(void) {
    long a = sum10(1, 2, 3, 4, 5, 6, 7, 8, 0x99, 0x1010);
    long b = mixed_stack(1, 2, 3, 4, 5, 6, 7, 8, -3, -1000, -100000, 0x123456789L);
    imu_sample m = make_imu(77, -1, 2, -3);
    printf("sum10=%ld mixed=%ld imu.sum=%llu &m=%p\n", a, b, (unsigned long long)m.sum, (void *)&m);
    return 0;
}
