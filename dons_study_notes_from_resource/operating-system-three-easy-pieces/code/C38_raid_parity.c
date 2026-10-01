// C38_raid_parity.c — OSTEP Ch.38: RAID 매핑 + XOR parity + small-write + 재구성(rebuild)
//
//  1) RAID-0 / RAID-4 / RAID-5(left-symmetric, left-asymmetric) 매핑표 출력
//     (raid.py 와 같은 공식이라 결과를 시뮬레이터와 대조할 수 있다)
//  2) 4KB 블록 단위 XOR parity: full-stripe write, subtractive(read-modify-write) 갱신,
//     additive 갱신이 같은 parity 를 만드는지 확인 + 각 방식의 물리 I/O 수
//  3) 디스크 1개 고장 → 나머지 XOR 로 재구성 후 원본과 비교
//  4) 책의 S/R 예제(seek 7ms, rot 3ms, 50MB/s)로 Figure 38.8 표 수치화
//
// build: cc -Wall -Wextra -O0 code/C38_raid_parity.c -o .work/bin/C38_raid_parity
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ND   5          // 디스크 수
#define NSTR 5          // stripe 수
#define BS   4096       // 블록 크기

static uint8_t disk[ND][NSTR][BS];

// raid.py __bmap5 와 같은 공식 (chunk = 1 block)
static void map5(int lba, int nd, int ls, int *d, int *pd, int *off) {
    int stripe = lba / (nd - 1);
    int col = stripe % nd;
    int dd = lba % (nd - 1);
    *pd = (nd - 1) - col;
    if (ls) dd = ((dd - col) % nd + nd) % nd;   // left-symmetric
    else if (dd >= *pd) dd++;                    // left-asymmetric
    *d = dd; *off = stripe;
}

static void print_layout(const char *title, int level, int ls) {
    char cell[NSTR][ND][6];
    for (int s = 0; s < NSTR; s++) for (int k = 0; k < ND; k++) strcpy(cell[s][k], "-");
    for (int lba = 0; lba < NSTR * (ND - 1) + (level == 0 ? NSTR : 0); lba++) {
        int d, pd = -1, off;
        if (level == 0) { d = lba % ND; off = lba / ND; }
        else if (level == 4) { d = lba % (ND - 1); off = lba / (ND - 1); pd = ND - 1; }
        else map5(lba, ND, ls, &d, &pd, &off);
        if (off >= NSTR) continue;
        snprintf(cell[off][d], 6, "%d", lba);
        if (pd >= 0) snprintf(cell[off][pd], 6, "P%d", off);
    }
    printf("%s\n       ", title);
    for (int k = 0; k < ND; k++) printf(" Disk%-2d", k);
    printf("\n");
    for (int s = 0; s < NSTR; s++) {
        printf("  row%d ", s);
        for (int k = 0; k < ND; k++) printf(" %6s", cell[s][k]);
        printf("\n");
    }
}

static void xor_into(uint8_t *dst, const uint8_t *src) { for (int i = 0; i < BS; i++) dst[i] ^= src[i]; }

static unsigned long long rng = 0x9E3779B97F4A7C15ull;
static uint8_t rnd8(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint8_t)rng; }

int main(void) {
    printf("[1] layouts (%d disks, chunk = 1 block)\n", ND);
    print_layout("RAID-0 striping", 0, 0);
    print_layout("RAID-4 (parity on last disk)", 4, 0);
    print_layout("RAID-5 left-symmetric  (= Figure 38.7)", 5, 1);
    print_layout("RAID-5 left-asymmetric", 5, 0);

    // ---- [2] RAID-4 stripe 0 : data disk 0..3, parity disk 4
    printf("\n[2] XOR parity on stripe 0 (RAID-4, 4 data + 1 parity, 4KB blocks)\n");
    for (int k = 0; k < ND - 1; k++) for (int i = 0; i < BS; i++) disk[k][0][i] = rnd8();
    memset(disk[4][0], 0, BS);
    for (int k = 0; k < ND - 1; k++) xor_into(disk[4][0], disk[k][0]);   // full-stripe write: 5 writes, 0 reads
    printf("  full-stripe write : parity = D0^D1^D2^D3   (physical I/O: 0 reads + 5 writes)\n");

    uint8_t newblk[BS], pnew_sub[BS], pnew_add[BS];
    for (int i = 0; i < BS; i++) newblk[i] = rnd8();
    // subtractive: P_new = (C_old ^ C_new) ^ P_old  -> read old data + old parity, write both
    memcpy(pnew_sub, disk[4][0], BS); xor_into(pnew_sub, disk[1][0]); xor_into(pnew_sub, newblk);
    // additive: P_new = XOR(다른 데이터 블록들, 새 블록) -> read 나머지 N-2 개 data, write 2
    memcpy(pnew_add, newblk, BS);
    for (int k = 0; k < ND - 1; k++) if (k != 1) xor_into(pnew_add, disk[k][0]);
    printf("  overwrite block 1 : subtractive parity == additive parity ? %s\n",
           memcmp(pnew_sub, pnew_add, BS) ? "NO (bug)" : "YES");
    for (int n = 3; n <= 8; n++) {
        int sub = 2 + 2, add = (n - 2) + 2;   // n = 전체 디스크 수(data n-1 + parity 1)
        printf("    disks=%d : subtractive %d I/Os (2R+2W), additive %d I/Os (%dR+2W)%s\n",
               n, sub, add, n - 2, add < sub ? "  <- additive cheaper" : add == sub ? "  <- tie" : "");
    }
    memcpy(disk[1][0], newblk, BS); memcpy(disk[4][0], pnew_sub, BS);

    // ---- [3] disk 2 고장 → 재구성
    uint8_t lost[BS], rebuilt[BS];
    memcpy(lost, disk[2][0], BS); memset(disk[2][0], 0xEE, BS);   // fail-stop 이후 쓰레기
    memset(rebuilt, 0, BS);
    for (int k = 0; k < ND; k++) if (k != 2) xor_into(rebuilt, disk[k][0]);
    printf("\n[3] disk 2 failed -> rebuild = D0^D1^D3^P : %s (read %d blocks to rebuild 1)\n",
           memcmp(rebuilt, lost, BS) ? "MISMATCH" : "matches original", ND - 1);

    // ---- [4] Figure 38.8 을 숫자로 (책 예제: seek 7ms, rot 3ms, 50 MB/s, seq 10MB, rand 10KB)
    double S = 10.0 / (0.007 + 0.003 + 10.0 / 50.0);              // MB/s
    double R = (10.0 / 1024) / (0.007 + 0.003 + (10.0 / 1024) / 50.0);
    int N = 4;
    printf("\n[4] S = %.2f MB/s, R = %.3f MB/s  (S/R = %.1f), N = %d disks\n", S, R, S / R, N);
    printf("  %-8s %10s %10s %10s %10s %10s\n", "level", "capacity", "seqRead", "seqWrite", "randRead", "randWrite");
    printf("  %-8s %9dB %10.1f %10.1f %10.2f %10.2f\n", "RAID-0", N, N * S, N * S, N * R, N * R);
    printf("  %-8s %9.1fB %10.1f %10.1f %10.2f %10.2f\n", "RAID-1", N / 2.0, N / 2.0 * S, N / 2.0 * S, N * R, N / 2.0 * R);
    printf("  %-8s %9dB %10.1f %10.1f %10.2f %10.2f\n", "RAID-4", N - 1, (N - 1) * S, (N - 1) * S, (N - 1) * R, R / 2);
    printf("  %-8s %9dB %10.1f %10.1f %10.2f %10.2f\n", "RAID-5", N - 1, (N - 1) * S, (N - 1) * S, N * R, N / 4.0 * R);
    return 0;
}
