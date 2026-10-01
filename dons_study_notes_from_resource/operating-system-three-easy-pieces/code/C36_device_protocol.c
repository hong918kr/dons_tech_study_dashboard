// C36_device_protocol.c — OSTEP Ch.36 "canonical device"를 스레드로 흉내 낸다.
//
//  * 장치(device) = 별도 pthread. 레지스터는 공유 메모리의 _Atomic 변수
//    (= memory-mapped I/O 레지스터처럼 load/store 로 접근).
//  * 드라이버(driver) = main 스레드.
//
//  실험 1: PIO(CPU가 4KB를 워드 단위로 DATA 레지스터에 밀어 넣음) + polling
//  실험 2: DMA(주소/길이만 알려줌) + "interrupt"(condvar 로 잠들었다가 깨어남)
//          → 대기 중 드라이버 스레드가 태운 CPU 시간을 CLOCK_THREAD_CPUTIME_ID 로 비교
//  실험 3: 빠른 장치(처리 ~0us)에서 polling vs interrupt 왕복 지연 비교
//  실험 4: hybrid(잠깐 spin → 안 끝나면 sleep)
//
// build: cc -Wall -Wextra -O0 -pthread code/C36_device_protocol.c -o .work/bin/C36_device_protocol
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ST_BUSY  0x80u   // IDE status 레지스터의 BSY 비트 흉내
#define ST_DRQ   0x08u   // "데이터 넣어도 됨" (IDE DRQ)
#define ST_DONE  0x01u

enum { CMD_NONE = 0, CMD_PIO_WRITE = 1, CMD_DMA_WRITE = 2, CMD_QUIT = 9 };

struct dev_regs {                  // "memory-mapped" 레지스터 블록
    _Atomic uint32_t status;
    _Atomic uint32_t command;      // doorbell 겸 명령
    _Atomic uint32_t data;         // PIO 데이터 포트
    _Atomic uint32_t data_valid;   // CPU가 data 를 썼다는 표시 (핸드셰이크)
    _Atomic uintptr_t dma_addr;    // DMA descriptor: 호스트 메모리 주소
    _Atomic uint32_t dma_len;      //                  길이(byte)
};

static struct dev_regs regs;
static uint8_t  dev_media[4096];   // 장치 내부 버퍼(=NAND/디스크 매체라고 치자)
static long     media_ns = 200000; // 매체 처리 시간(기본 200us ≒ TLC program 시간대)

// "interrupt line": 장치가 끝나면 신호. OS 의 ISR → wakeup() 경로를 condvar 로 대신함
static pthread_mutex_t irq_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  irq_cv   = PTHREAD_COND_INITIALIZER;
static int irq_pending = 0;
static _Atomic int use_irq = 0;

static uint64_t now_ns(void) {        // macOS: CLOCK_MONOTONIC 은 1us 해상도라 raw tick 기반 ns 사용
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}
static uint64_t thread_cpu_ns(void) {
    struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static void busy_wait_ns(long ns) {           // 장치 내부 "물리 동작" 시간
    uint64_t end = now_ns() + (uint64_t)ns;
    while (now_ns() < end) { }
}

// ---------------------------------------------------------------- device FW
static void raise_irq(void) {
    if (!use_irq) return;
    pthread_mutex_lock(&irq_lock);
    irq_pending = 1;
    pthread_cond_signal(&irq_cv);
    pthread_mutex_unlock(&irq_lock);
}

static void *device_main(void *arg) {
    (void)arg;
    for (;;) {
        uint32_t cmd;
        while ((cmd = atomic_load(&regs.command)) == CMD_NONE) { } // doorbell 대기 (HW라서 spin OK)
        if (cmd == CMD_QUIT) return NULL;
        atomic_store(&regs.status, ST_BUSY);
        if (cmd == CMD_PIO_WRITE) {
            // CPU가 워드 하나씩 data 포트에 써 주는 것을 받아 간다
            uint32_t *dst = (uint32_t *)dev_media;
            for (int i = 0; i < 1024; i++) {
                atomic_store(&regs.status, ST_BUSY | ST_DRQ);      // 다음 워드 주세요
                while (!atomic_load(&regs.data_valid)) { }
                dst[i] = atomic_load(&regs.data);
                atomic_store(&regs.data_valid, 0);
            }
            atomic_store(&regs.status, ST_BUSY);
        } else if (cmd == CMD_DMA_WRITE) {
            // DMA 엔진: CPU 개입 없이 호스트 메모리에서 직접 끌어온다
            memcpy(dev_media, (const void *)atomic_load(&regs.dma_addr), atomic_load(&regs.dma_len));
        }
        busy_wait_ns(media_ns);                      // 매체에 실제로 쓰는 시간
        atomic_store(&regs.command, CMD_NONE);
        atomic_store(&regs.status, ST_DONE);          // 완료 (status 레지스터)
        raise_irq();                                  // + 인터럽트
    }
}

// ---------------------------------------------------------------- driver
static uint32_t host_buf[1024];

static long poll_until_done(void) {               // OSTEP: while (STATUS == BUSY) ;
    long spins = 0;
    while (!(atomic_load(&regs.status) & ST_DONE)) spins++;
    return spins;
}
static void sleep_until_irq(void) {               // OS: 프로세스를 재우고 ISR 이 깨움
    pthread_mutex_lock(&irq_lock);
    while (!irq_pending) pthread_cond_wait(&irq_cv, &irq_lock);
    irq_pending = 0;
    pthread_mutex_unlock(&irq_lock);
}

static void issue_pio_write(void) {
    atomic_store(&regs.status, ST_BUSY);
    atomic_store(&regs.command, CMD_PIO_WRITE);
    for (int i = 0; i < 1024; i++) {              // CPU가 직접 4KB 를 옮긴다 (PIO)
        while (!(atomic_load(&regs.status) & ST_DRQ) || atomic_load(&regs.data_valid)) { }
        atomic_store(&regs.data, host_buf[i]);
        atomic_store(&regs.data_valid, 1);
    }
}
static void issue_dma_write(void) {
    atomic_store(&regs.dma_addr, (uintptr_t)host_buf);   // descriptor 기록
    atomic_store(&regs.dma_len, (uint32_t)sizeof host_buf);
    atomic_store(&regs.status, ST_BUSY);
    atomic_store(&regs.command, CMD_DMA_WRITE);          // doorbell
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

int main(void) {
    for (int i = 0; i < 1024; i++) host_buf[i] = 0xC0DE0000u | (uint32_t)i;
    pthread_t dev;
    pthread_create(&dev, NULL, device_main, NULL);

    const int N = 200;
    // ---- 실험 1: PIO + polling
    use_irq = 0;
    uint64_t cpu0 = thread_cpu_ns(), t0 = now_ns(); long spins = 0;
    for (int r = 0; r < N; r++) { issue_pio_write(); spins += poll_until_done(); }
    uint64_t cpu_pio = thread_cpu_ns() - cpu0, wall_pio = now_ns() - t0;
    int ok = memcmp(dev_media, host_buf, sizeof host_buf) == 0;

    // ---- 실험 2: DMA + interrupt
    use_irq = 1; memset(dev_media, 0, sizeof dev_media);
    cpu0 = thread_cpu_ns(); t0 = now_ns();
    for (int r = 0; r < N; r++) { issue_dma_write(); sleep_until_irq(); }
    uint64_t cpu_dma = thread_cpu_ns() - cpu0, wall_dma = now_ns() - t0;
    ok &= memcmp(dev_media, host_buf, sizeof host_buf) == 0;

    printf("[1/2] 4KB write x %d, media time %ld us per I/O\n", N, media_ns / 1000);
    printf("  PIO + polling   : wall %7.2f ms, driver-thread CPU %7.2f ms (%.0f%% of wall), avg spins/IO %ld\n",
           wall_pio / 1e6, cpu_pio / 1e6, 100.0 * cpu_pio / wall_pio, spins / N);
    printf("  DMA + interrupt : wall %7.2f ms, driver-thread CPU %7.2f ms (%.0f%% of wall)\n",
           wall_dma / 1e6, cpu_dma / 1e6, 100.0 * cpu_dma / wall_dma);
    printf("  data check      : %s\n", ok ? "device buffer == host buffer (OK)" : "MISMATCH");

    // ---- 실험 3: 아주 빠른 장치(매체 시간 0) — polling vs interrupt 왕복 지연
    media_ns = 0;
    enum { M = 2000 };
    static uint64_t lp[M], li[M], lh[M];
    for (int r = 0; r < M; r++) {
        use_irq = 0; uint64_t s = now_ns(); issue_dma_write(); poll_until_done(); lp[r] = now_ns() - s;
        use_irq = 1; s = now_ns(); issue_dma_write(); sleep_until_irq(); li[r] = now_ns() - s;
    }
    // ---- 실험 4: hybrid — 최대 20us spin, 그래도 안 끝나면 sleep (Linux hybrid polling 과 같은 발상)
    long dev_us[2] = {10, 50};
    uint64_t hyb_p50[2], hyb_cpu[2]; long slept[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
        media_ns = dev_us[k] * 1000;
        uint64_t c0 = thread_cpu_ns();
        for (int r = 0; r < M / 10; r++) {
            use_irq = 1; uint64_t s = now_ns(); issue_dma_write();
            while (!(atomic_load(&regs.status) & ST_DONE) && now_ns() - s < 20000) { }
            if (!(atomic_load(&regs.status) & ST_DONE)) slept[k]++;
            sleep_until_irq();   // spin 중에 끝났으면 이미 pending → 바로 리턴(irq 소비)
            lh[r] = now_ns() - s;
        }
        hyb_cpu[k] = (thread_cpu_ns() - c0) / (M / 10);
        qsort(lh, M / 10, sizeof lh[0], cmp_u64);
        hyb_p50[k] = lh[M / 20];
    }
    qsort(lp, M, sizeof lp[0], cmp_u64); qsort(li, M, sizeof li[0], cmp_u64);
    printf("[3] fast device (media 0 us), %d round trips\n", M);
    printf("  polling   latency : p50 %6.2f us   p99 %7.2f us\n", lp[M / 2] / 1e3, lp[M * 99 / 100] / 1e3);
    printf("  interrupt latency : p50 %6.2f us   p99 %7.2f us\n", li[M / 2] / 1e3, li[M * 99 / 100] / 1e3);
    for (int k = 0; k < 2; k++)
        printf("[4] hybrid spin<=20us then sleep, %2ld us device: p50 %6.2f us, driver CPU/IO %6.2f us, slept %ld/%d\n",
               dev_us[k], hyb_p50[k] / 1e3, hyb_cpu[k] / 1e3, slept[k], M / 10);

    atomic_store(&regs.command, CMD_QUIT);
    pthread_join(dev, NULL);
    return 0;
}
