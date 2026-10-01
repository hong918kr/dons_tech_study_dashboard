// C21_demand_paging.c — "present bit" 와 페이지 폴트를 사용자 공간에서 관찰하기
//  - mmap 으로 큰 영역을 예약만 하면 물리 프레임은 아직 없다 (present = 0)
//  - 처음 건드릴 때마다 페이지 폴트 -> OS 가 프레임을 할당(zero-fill) -> present = 1
//  - mincore() 로 "지금 메모리에 올라와 있나?" (= present 비트의 사용자 버전) 를 물어본다
//  - getrusage() 의 ru_minflt 로 폴트 횟수를 센다
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/resource.h>

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

static long minflt(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_minflt;
}

static long resident(char *p, size_t len, long pagesize)
{
    size_t n = len / (size_t)pagesize;
    char vec[n];                                     // 페이지당 1바이트
    if (mincore(p, len, vec) != 0) { perror("mincore"); return -1; }
    long cnt = 0;
    for (size_t i = 0; i < n; i++) cnt += (vec[i] & MINCORE_INCORE) ? 1 : 0;
    return cnt;
}

int main(void)
{
    long pagesize = sysconf(_SC_PAGESIZE);
    size_t npages = 4096;                            // 4096 x 16KB = 64MB (Apple Silicon)
    size_t len = npages * (size_t)pagesize;
    printf("pagesize %ld B, region %zu pages = %zu MB\n", pagesize, npages, len >> 20);

    char *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    printf("after mmap          : resident %5ld / %zu pages\n", resident(p, len, pagesize), npages);

    // 1) 절반만 첫 접근 (폴트 발생)
    long f0 = minflt(); double t0 = now_us();
    for (size_t i = 0; i < npages / 2; i++) p[i * (size_t)pagesize] = 1;
    double t1 = now_us(); long f1 = minflt();
    printf("touch 1st half      : resident %5ld, minor faults +%ld, %.0f us (%.2f us/page)\n",
           resident(p, len, pagesize), f1 - f0, t1 - t0, (t1 - t0) / (double)(npages / 2));

    // 2) 같은 절반을 다시 접근 (이미 present -> 폴트 없음)
    f0 = minflt(); t0 = now_us();
    for (size_t i = 0; i < npages / 2; i++) p[i * (size_t)pagesize] += 1;
    t1 = now_us(); f1 = minflt();
    printf("touch 1st half again: resident %5ld, minor faults +%ld, %.0f us (%.3f us/page)\n",
           resident(p, len, pagesize), f1 - f0, t1 - t0, (t1 - t0) / (double)(npages / 2));

    // 3) 읽기만 해도 폴트가 날까? (zero page 매핑 여부는 OS 마다 다름)
    f0 = minflt(); long sum = 0;
    for (size_t i = npages / 2; i < npages; i++) sum += p[i * (size_t)pagesize];
    f1 = minflt();
    printf("read 2nd half       : resident %5ld, minor faults +%ld (sum=%ld)\n",
           resident(p, len, pagesize), f1 - f0, sum);

    // 4) 프레임 반납: munmap 대신 madvise(MADV_FREE) — 커널에 "버려도 됨" 힌트
    madvise(p, len, MADV_FREE);
    printf("after MADV_FREE     : resident %5ld (lazy: kernel reclaims only under pressure)\n",
           resident(p, len, pagesize));

    // 5) PROT_NONE 영역 접근 = valid 하지만 권한 없음 -> SIGBUS/SIGSEGV (여기서는 시도하지 않음)
    munmap(p, len);
    printf("munmap done\n");
    return 0;
}
