// C37_disk_math.c — OSTEP Ch.37: 디스크 I/O 시간 계산 + 평균 seek 거리 + 디스크 스케줄러 비교
//
//  1) T_IO = T_seek + T_rotation + T_transfer  (Cheetah 15K.5 vs Barracuda, + 현대 20TB HDD)
//  2) 평균 seek 거리 = N/3 을 몬테카를로로 확인
//  3) FIFO / SSTF / SCAN / LOOK / C-SCAN 의 총 head 이동 거리(트랙 수) 비교 + SSTF starvation 데모
//
// build: cc -Wall -Wextra -O0 code/C37_disk_math.c -o .work/bin/C37_disk_math
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct drive { const char *name; double rpm, seek_ms, xfer_MBps; };

static double io_time(const struct drive *d, double size_KB, int sequential) {
    double rot_full = 60.0 * 1000.0 / d->rpm;          // ms per rotation
    double t_rot = rot_full / 2.0;                       // 평균 반 바퀴
    double t_xfer = size_KB / 1024.0 / d->xfer_MBps * 1000.0; // ms
    double t = d->seek_ms + t_rot + t_xfer;
    double rate = size_KB / 1024.0 / (t / 1000.0);      // MB/s
    printf("  %-22s %-10s size %8.0f KB : seek %5.2f + rot %5.2f + xfer %8.3f = %8.3f ms -> %7.2f MB/s\n",
           d->name, sequential ? "sequential" : "random", size_KB, d->seek_ms, t_rot, t_xfer, t, rate);
    return rate;
}

// ---------- 스케줄링 (트랙 번호만 보는 단순 모델: 이동 거리 = |a-b|)
#define MAXQ 64
static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

// path[] 에 head 가 지나가는 지점(요청 + 끝 트랙 왕복 지점)을 기록하고 총 이동 거리 반환
static int schedule(const char *name, int start, const int *req, int n, int maxtrack) {
    int path[MAXQ * 2], np = 0, s[MAXQ], done[MAXQ] = {0};
    memcpy(s, req, sizeof(int) * (size_t)n);
    if (!strcmp(name, "FIFO")) {
        for (int i = 0; i < n; i++) path[np++] = req[i];
    } else if (!strcmp(name, "SSTF")) {
        int head = start;
        for (int k = 0; k < n; k++) {
            int best = -1;
            for (int i = 0; i < n; i++)
                if (!done[i] && (best < 0 || abs(req[i] - head) < abs(req[best] - head))) best = i;
            done[best] = 1; head = req[best]; path[np++] = head;
        }
    } else {
        qsort(s, (size_t)n, sizeof(int), cmp_int);
        int lo = 0; while (lo < n && s[lo] < start) lo++;      // s[lo..] >= start
        for (int i = lo; i < n; i++) path[np++] = s[i];          // 바깥쪽(큰 번호)으로 sweep
        if (lo > 0) {
            if (!strcmp(name, "SCAN")) {                          // 끝까지 갔다가 방향 전환
                path[np++] = maxtrack;
                for (int i = lo - 1; i >= 0; i--) path[np++] = s[i];
            } else if (!strcmp(name, "LOOK")) {                   // 마지막 요청에서 바로 전환
                for (int i = lo - 1; i >= 0; i--) path[np++] = s[i];
            } else {                                              // C-SCAN: 끝 → 0 으로 복귀 후 같은 방향
                path[np++] = maxtrack; path[np++] = 0;
                for (int i = 0; i < lo; i++) path[np++] = s[i];
            }
        }
    }
    int head = start, total = 0;
    printf("  %-6s: %d", name, start);
    for (int i = 0; i < np; i++) { total += abs(path[i] - head); head = path[i]; printf(" %d", head); }
    printf("\n          total head movement = %d tracks\n", total);
    return total;
}

int main(void) {
    struct drive cheetah = { "Cheetah 15K.5 (SCSI)", 15000, 4.0, 125 };
    struct drive cuda    = { "Barracuda (SATA)",      7200, 9.0, 105 };
    struct drive modern  = { "20TB 7200rpm (2020s)",  7200, 8.5, 270 };   // 새 예제
    printf("[1] I/O time (Eq. 37.1)\n");
    struct drive *ds[] = { &cheetah, &cuda, &modern };
    for (int i = 0; i < 3; i++) {
        double r = io_time(ds[i], 4, 0), sq = io_time(ds[i], 100 * 1024, 1);
        printf("  -> %s: sequential/random = %.0fx, random 4KB IOPS = %.0f\n", ds[i]->name, sq / r, r * 1024 / 4);
    }

    printf("\n[2] average seek distance (Monte Carlo, N=1000 tracks, 10M pairs)\n");
    unsigned long long x = 88172645463325252ull, sum = 0;   // xorshift64
    const int N = 1000; const long PAIRS = 10000000;
    for (long i = 0; i < PAIRS; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17; int a = (int)(x % N);
        x ^= x << 13; x ^= x >> 7; x ^= x << 17; int b = (int)(x % N);
        sum += (unsigned long long)abs(a - b);
    }
    printf("  mean |x-y| = %.2f tracks  (N/3 = %.2f)\n", (double)sum / PAIRS, N / 3.0);

    printf("\n[3] disk scheduling, head at track 53, tracks 0..199\n");
    int q[] = { 98, 183, 37, 122, 14, 124, 65, 67 };
    int n = (int)(sizeof q / sizeof q[0]);
    const char *pol[] = { "FIFO", "SSTF", "SCAN", "LOOK", "C-SCAN" };
    for (int i = 0; i < 5; i++) schedule(pol[i], 53, q, n, 199);

    printf("\n[4] SSTF starvation: head at 50, request for track 190 waits while near requests keep arriving\n");
    int head = 50, served = 0, far_waiting = 1;
    for (int t = 0; t < 12; t++) {
        int near = 45 + (t * 7) % 12;               // 근처 트랙 요청이 계속 들어온다
        if (abs(near - head) < abs(190 - head)) { head = near; served++; }
        else { far_waiting = 0; break; }
    }
    printf("  near requests served = %d, track 190 still waiting = %s\n", served, far_waiting ? "YES" : "no");
    return 0;
}
