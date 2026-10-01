// C49_afs_vs_nfs.c — NFS(폴링 + attribute cache) vs AFS(callback) 를 작은 시뮬레이션으로 비교
//
//  [1] 한 파일, 두 클라이언트 타임라인: C1 은 0.5초마다 open+read, C2 는 t=2.2s 에 새 버전을 close(flush).
//      - NFS: open 때마다 GETATTR 필요. attribute cache(3s) 가 살아 있으면 서버에 안 물어봄 → stale read 가능
//      - AFS: 첫 Fetch 때 callback 등록. 서버가 C2 의 Store 를 받으면 C1 에게 callback break.
//  [2] 서버 부하: 클라이언트 N 개가 각자 자기 파일을 0.5초마다 다시 열 때 60초 동안 서버가 받는 메시지 수
//  [3] Figure 49.4 워크로드 표를 구체적인 숫자(Lnet, Ldisk, Lmem, 블록 수)로 계산
//
// build: cc -Wall -Wextra -O0 code/C49_afs_vs_nfs.c -o .work/bin/C49_afs_vs_nfs && .work/bin/C49_afs_vs_nfs
#include <stdio.h>

#define STEP_MS 500
#define END_MS 6000
#define WRITE_AT 2200   // C2 가 close() 하면서 v2 를 서버에 flush 하는 시각

static void timeline_nfs(int attr_timeout_ms) {
    int server_ver = 1, cached_ver = 1;      // C1 이 처음에 v1 을 이미 캐시하고 있다고 가정
    long attr_fetched_at = -1000000;         // attribute cache 에 넣은 시각
    int getattr = 0, reads = 0, stale = 0;
    printf("NFS (attr cache %dms):\n  ", attr_timeout_ms);
    for (int t = 0; t <= END_MS; t += STEP_MS) {
        if (t >= WRITE_AT) server_ver = 2;
        // open(): 캐시를 써도 되는지 검사
        if (t - attr_fetched_at >= attr_timeout_ms) {     // attr cache 만료 → GETATTR
            getattr++;
            attr_fetched_at = t;
            if (server_ver != cached_ver) cached_ver = server_ver;   // mtime 이 다름 → 무효화 후 재-READ
        }
        reads++;
        int is_stale = cached_ver != server_ver;
        stale += is_stale;
        printf("t=%.1f:v%d%s ", t / 1000.0, cached_ver, is_stale ? "!" : "");
    }
    printf("\n  → GETATTR %d번, open %d번 중 stale read %d번 (! 표시)\n", getattr, reads, stale);
}

static void timeline_afs(void) {
    int server_ver = 1, cached_ver = 1, callback_valid = 1, msgs = 1;   // msgs=1: 처음 Fetch
    int reads = 0, stale = 0, breaks = 0;
    printf("AFS (callback):\n  ");
    for (int t = 0; t <= END_MS; t += STEP_MS) {
        if (t >= WRITE_AT && server_ver == 1) {   // C2 Store → 서버가 C1 의 callback 을 끊음
            server_ver = 2; callback_valid = 0; breaks++; msgs += 2;   // Store + callback break
        }
        if (!callback_valid) { cached_ver = server_ver; callback_valid = 1; msgs++; }  // 재Fetch
        reads++;
        int is_stale = cached_ver != server_ver;
        stale += is_stale;
        printf("t=%.1f:v%d%s ", t / 1000.0, cached_ver, is_stale ? "!" : "");
    }
    printf("\n  → 서버 메시지 %d개 (Fetch/Store/break), callback break %d번, open %d번 중 stale read %d번\n", msgs, breaks, reads, stale);
}

static void load(int nclients) {
    int opens = nclients * (60000 / STEP_MS);
    int nfs_noac = opens;                         // 매 open 마다 GETATTR
    int nfs_ac3 = nclients * (60000 / 3000);      // 3초에 한 번
    int afs = nclients;                           // 처음 Fetch 한 번 (아무도 안 바꾸면 끝)
    printf("  clients=%4d  opens=%7d  NFS(no attr cache)=%7d  NFS(3s attr cache)=%6d  AFS=%5d\n",
           nclients, opens, nfs_noac, nfs_ac3, afs);
}

int main(void) {
    printf("[1] 같은 파일을 C1 은 0.5초마다 열어 읽고, C2 가 t=2.2s 에 v2 로 덮어쓴다\n");
    timeline_nfs(0);
    timeline_nfs(3000);
    timeline_afs();

    printf("\n[2] 서버가 60초 동안 받는 '캐시 검증/가져오기' 메시지 수 (파일 공유 없음 = 흔한 경우)\n");
    load(20); load(50); load(500);

    // [3] Figure 49.4 — 1블록 = 4KB 가정. 단위 ms.
    const double Lnet = 1.0, Ldisk = 0.1, Lmem = 0.001;
    const double Ns = 4, Nm = 1000, NL = 1000000;   // 16KB, 4MB, 4GB(메모리엔 안 들어가고 디스크엔 들어감)
    struct { const char *w; double nfs, afs; } row[] = {
        {"1. small seq read",        Ns * Lnet,  Ns * Lnet},
        {"2. small re-read",         Ns * Lmem,  Ns * Lmem},
        {"3. medium seq read",       Nm * Lnet,  Nm * Lnet},
        {"4. medium re-read",        Nm * Lmem,  Nm * Lmem},
        {"5. large seq read",        NL * Lnet,  NL * Lnet},
        {"6. large re-read",         NL * Lnet,  NL * Ldisk},
        {"7. large single read",     Lnet,       NL * Lnet},
        {"8. small seq write",       Ns * Lnet,  Ns * Lnet},
        {"9. large seq write",       NL * Lnet,  NL * Lnet},
        {"10. large overwrite",      NL * Lnet,  2 * NL * Lnet},
        {"11. large single write",   Lnet,       2 * NL * Lnet},
    };
    printf("\n[3] Figure 49.4 를 숫자로 (Lnet=%.3fms Ldisk=%.3fms Lmem=%.3fms, Ns=%.0f Nm=%.0f NL=%.0f 블록)\n",
           Lnet, Ldisk, Lmem, Ns, Nm, NL);
    printf("  %-24s %14s %14s %12s\n", "workload", "NFS(ms)", "AFS(ms)", "AFS/NFS");
    for (unsigned i = 0; i < sizeof(row) / sizeof(row[0]); i++)
        printf("  %-24s %14.3f %14.3f %12.3g\n", row[i].w, row[i].nfs, row[i].afs, row[i].afs / row[i].nfs);
    return 0;
}
