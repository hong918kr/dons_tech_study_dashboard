// C19_tlb.c — OSTEP Ch.19 숙제: TLB 크기/미스 비용 측정 (Saavedra-Barrera 방식)
// 사용법:  tlb <numpages> <trials>   -> 한 페이지당 평균 접근 시간(ns) 1줄 출력
//          tlb                       -> 1,2,4,...,16384 페이지 스윕 (책의 a[i] += 1 루프)
//          tlb chase                 -> 같은 스윕을 "포인터 추적(pointer chase)" 으로
//                                       (접근끼리 의존 -> CPU가 미스를 겹쳐 숨기지 못함)
// 주의: Apple Silicon 은 페이지가 16KB. PAGESIZE 는 하드코딩하지 않고 getpagesize() 로 얻는다.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

// 페이지마다 int 하나를 건드린다. 단, 캐시 라인 하나(128B)씩 엇갈리게 해서
// "모든 접근이 L1 캐시의 같은 set 으로 몰리는" 캐시 충돌 효과를 줄인다.
// (jump 는 2의 거듭제곱이라 % 대신 & 로 싸게 계산)
static double measure(int *a, long numpages, long trials, long jump, long stagger)
{
    // 1) 미리 한 번 다 써서 demand-zero 페이지 폴트를 측정 밖으로 뺀다 (숙제 Q7)
    for (long p = 0; p < numpages; p++) a[p * jump + ((p * stagger) & (jump - 1))] = 0;

    double t0 = now_ns();
    for (long t = 0; t < trials; t++)
        for (long p = 0; p < numpages; p++)
            a[p * jump + ((p * stagger) & (jump - 1))] += 1;
    double t1 = now_ns();
    return (t1 - t0) / ((double)numpages * (double)trials);
}

// 포인터 추적: 페이지들을 무작위 순서의 원형 리스트로 엮고, p = *p 를 반복.
// 다음 주소를 알려면 이전 load 가 끝나야 하므로 TLB 미스 지연이 그대로 드러난다.
static double chase(char *buf, long numpages, long steps, long pagesize)
{
    long *perm = malloc((size_t)numpages * sizeof(long));
    for (long i = 0; i < numpages; i++) perm[i] = i;
    srandom(42);
    for (long i = numpages - 1; i > 0; i--) {          // Fisher-Yates 셔플
        long j = random() % (i + 1), t = perm[i];
        perm[i] = perm[j]; perm[j] = t;
    }
    for (long i = 0; i < numpages; i++) {
        long cur = perm[i], nxt = perm[(i + 1) % numpages];
        void **slot = (void **)(buf + cur * pagesize + ((cur * 128) & (pagesize - 1)));
        *slot = buf + nxt * pagesize + ((nxt * 128) & (pagesize - 1));
    }
    void **p = (void **)(buf + perm[0] * pagesize + ((perm[0] * 128) & (pagesize - 1)));
    free(perm);
    for (long i = 0; i < numpages; i++) p = (void **)*p;   // 워밍업 1바퀴
    double t0 = now_ns();
    for (long i = 0; i < steps; i++) p = (void **)*p;
    double t1 = now_ns();
    if (p == NULL) printf("impossible\n");                // p 를 사용 -> 루프 제거 방지
    return (t1 - t0) / (double)steps;
}

int main(int argc, char *argv[])
{
    long pagesize = getpagesize();                  // = sysconf(_SC_PAGESIZE)
    long jump     = pagesize / (long)sizeof(int);   // 한 페이지에 들어가는 int 개수
    long stagger  = 128 / (long)sizeof(int);        // 128B 캐시라인 단위로 엇갈림

#ifdef __APPLE__
    // macOS 는 스레드를 특정 코어에 "고정(pin)" 하는 API 가 없다 (affinity 는 힌트뿐).
    // 대신 QoS 를 USER_INTERACTIVE 로 올려 P-core 에서 돌 확률을 높인다 (숙제 Q6).
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

    if (argc == 3) {
        long numpages = atol(argv[1]), trials = atol(argv[2]);
        int *a = calloc((size_t)(numpages * jump), sizeof(int));
        if (!a) { perror("calloc"); return 1; }
        double ns = measure(a, numpages, trials, jump, stagger);
        printf("%ld %.3f\n", numpages, ns);
        // volatile-대신: 결과를 실제로 "사용" 해서 컴파일러가 루프를 지우지 못하게 (숙제 Q5)
        long sum = 0;
        for (long p = 0; p < numpages; p++) sum += a[p * jump + ((p * stagger) & (jump - 1))];
        fprintf(stderr, "checksum %ld\n", sum);
        free(a);
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "chase") == 0) {
        long maxpages = 16384;
        char *buf = calloc((size_t)maxpages, (size_t)pagesize);
        if (!buf) { perror("calloc"); return 1; }
        printf("pointer-chase, pagesize = %ld bytes\n", pagesize);
        printf("%8s %12s %12s\n", "pages", "touch(KB)", "ns/access");
        for (long n = 1; n <= maxpages; n *= 2) {
            double best = 1e30;
            for (int rep = 0; rep < 3; rep++) {
                double ns = chase(buf, n, 20000000L, pagesize);
                if (ns < best) best = ns;
            }
            printf("%8ld %12ld %12.3f\n", n, n * pagesize / 1024, best);
            fflush(stdout);
        }
        free(buf);
        return 0;
    }

    printf("pagesize = %ld bytes (sysconf(_SC_PAGESIZE) = %ld)\n", pagesize, sysconf(_SC_PAGESIZE));
    printf("%8s %12s %10s %12s\n", "pages", "touch(KB)", "trials", "ns/access");
    long maxpages = 16384;
    int *a = calloc((size_t)(maxpages * jump), sizeof(int));
    if (!a) { perror("calloc"); return 1; }
    long total = 0;
    for (long n = 1; n <= maxpages; n *= 2) {
        long trials = 200000000L / n;               // 대략 2억 번 접근이 되도록
        if (trials < 20) trials = 20;
        double best = 1e30;
        for (int rep = 0; rep < 3; rep++) {         // 3번 재서 최소값 (노이즈 제거)
            double ns = measure(a, n, trials, jump, stagger);
            if (ns < best) best = ns;
        }
        printf("%8ld %12ld %10ld %12.3f\n", n, n * pagesize / 1024, trials, best);
        fflush(stdout);
    }
    for (long p = 0; p < maxpages; p++) total += a[p * jump + ((p * stagger) & (jump - 1))];
    printf("checksum %ld\n", total);
    free(a);
    return 0;
}
