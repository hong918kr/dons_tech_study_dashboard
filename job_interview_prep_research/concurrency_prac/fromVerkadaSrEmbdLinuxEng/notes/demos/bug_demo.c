/* bug_demo.c — 두 가지 고전 버그의 증상을 눈으로 본다. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* (1) 톰스톤 없이 지우면 뒤에 있던 키가 사라진다 */
#define C 8u
#define M (C - 1u)
static uint32_t k[C];
static bool u[C];
static uint32_t home(uint32_t x) { return x & M; }   /* 일부러 충돌시키는 해시 */
static void ins(uint32_t x)
{
    for (uint32_t p = 0; p < C; p++) { uint32_t s = (home(x) + p) & M;
        if (!u[s]) { u[s] = true; k[s] = x; return; } }
}
static bool find_earlyexit(uint32_t x)
{
    for (uint32_t p = 0; p < C; p++) { uint32_t s = (home(x) + p) & M;
        if (!u[s]) return false;                     /* 첫 빈 칸에서 멈춘다 */
        if (k[s] == x) return true; }
    return false;
}
static bool find_fullwindow(uint32_t x)
{
    for (uint32_t p = 0; p < C; p++) { uint32_t s = (home(x) + p) & M;
        if (u[s] && k[s] == x) return true; }        /* 멈추지 않는다 */
    return false;
}
static void del_plain(uint32_t x)
{
    for (uint32_t p = 0; p < C; p++) { uint32_t s = (home(x) + p) & M;
        if (u[s] && k[s] == x) { u[s] = false; return; } }
}

/* (2) sift_down 에서 자식 하나만 비교하면 힙 성질이 깨진다 */
static int hp[16]; static int n;
static void down_buggy(int i)
{
    for (;;) { int l = 2*i+1, m = i;
        if (l < n && hp[l] < hp[m]) m = l;           /* 오른쪽 자식을 안 본다 */
        if (m == i) return; int t=hp[m]; hp[m]=hp[i]; hp[i]=t; i=m; }
}
static void down_ok(int i)
{
    for (;;) { int l = 2*i+1, r = l+1, m = i;
        if (l < n && hp[l] < hp[m]) m = l;
        if (r < n && hp[r] < hp[m]) m = r;
        if (m == i) return; int t=hp[m]; hp[m]=hp[i]; hp[i]=t; i=m; }
}
static int check_heap(void)
{
    for (int i = 1; i < n; i++) if (hp[(i-1)/2] > hp[i]) return i;
    return 0;
}

int main(void)
{
    /* 0, 8, 16 은 home 이 모두 0 -> 0,1,2 슬롯에 들어간다 */
    ins(0); ins(8); ins(16);
    printf("(1) inserted 0,8,16 -> slots: ");
    for (uint32_t i = 0; i < C; i++) printf("%s", u[i] ? "X" : ".");
    printf("\n");
    del_plain(8);                                    /* 가운데를 그냥 비운다 */
    printf("    after deleting 8   -> slots: ");
    for (uint32_t i = 0; i < C; i++) printf("%s", u[i] ? "X" : ".");
    printf("\n    find(16) early-exit=%s   full-window=%s   <- 16 은 아직 테이블에 있다\n",
           find_earlyexit(16) ? "FOUND" : "LOST", find_fullwindow(16) ? "FOUND" : "LOST");

    /* (2) */
    int seed[7] = { 1, 5, 2, 9, 7, 3, 4 };
    memcpy(hp, seed, sizeof seed); n = 7;
    printf("(2) heap before pop : "); for (int i=0;i<n;i++) printf("%d ", hp[i]); printf("\n");
    hp[0] = hp[--n]; down_buggy(0);
    printf("    buggy sift_down  : "); for (int i=0;i<n;i++) printf("%d ", hp[i]);
    printf("  heap property broken at index %d\n", check_heap());
    memcpy(hp, seed, sizeof seed); n = 7;
    hp[0] = hp[--n]; down_ok(0);
    printf("    correct sift_down: "); for (int i=0;i<n;i++) printf("%d ", hp[i]);
    printf("  violation index %d (0 = OK)\n", check_heap());
    return 0;
}
