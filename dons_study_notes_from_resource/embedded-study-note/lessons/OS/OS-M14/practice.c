/* OS-M14 추가 연습 — 한 번에 검증 (VSFS 확장 5개)
 *   연습 1  rmdir: 빈 디렉터리만, 부모 nlink -1, 블록·inode 반납
 *   연습 2  큰 디렉터리: 130 개 → 블록 2개, 뒤쪽 이름은 찾는 데 읽기 1번 더 / 빈 칸 재사용이면 부모 inode 안 씀
 *   연습 3  크기 → 블록 수 공식 (indirect 오버헤드), 최대 파일 크기, 2단 간접이면?
 *   연습 4  크래시 주입 스윕: k 번째 쓰기 뒤 전원 차단 → fsck. 올바른 순서는 "새는 것"만, 틀린 순서는 위험
 *   연습 5  vsfs.py 와 같은 형식으로 상태 출력 → OSTEP 시뮬레이터 실제 출력(seed 17)과 글자 그대로 비교
 * 엔진은 solution.c 를 그대로 쓴다 (VSFS_NO_MAIN). 같은 폴더에서 빌드:
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define VSFS_NO_MAIN
#include "solution.c"

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)
#define IO(r, w) (D.nread == (r) && D.nwrite == (w))

static int fresh(uint32_t nblocks, uint32_t ninodes) { g_cache = 1; return disk_open(nblocks) == 0 ? fs_mkfs(ninodes) : -1; }
static int fsck0(void) { fsck_t r; return fsck(0, &r); }

/* ---------------- 연습 1: rmdir ---------------- */
static int fs_rmdir(const char *path) {
    op_begin();
    char dpath[256], name[NAME_MAX_ + 1];
    int rc = split_last(path, dpath, sizeof dpath, name);
    if (rc < 0) return rc;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
    dinode_t pd, d;
    int pinum = namei_(dpath, &pd);
    if (pinum < 0) return pinum;
    uint32_t slot;
    int inum = dir_lookup(&pd, name, &slot);
    if (inum < 0) return inum;
    if ((rc = iget((uint32_t)inum, &d)) < 0) return rc;
    if (d.type != T_DIR) return -ENOTDIR;
    for (uint32_t s = 0; s < d.size / DIRENT_SIZE; s++) {          /* "." ".." 말고 쓰인 칸이 있으면 비지 않음 */
        long b = bmap(&d, s / DPB, 0, NULL);
        if (b <= 0) continue;
        dirent_t e;
        memcpy(&e, bget((uint32_t)b) + (s % DPB) * DIRENT_SIZE, sizeof e);
        if (e.name[0] && strcmp(e.name, ".") && strcmp(e.name, "..")) return -ENOTEMPTY;
    }
    long b = bmap(&pd, slot / DPB, 0, NULL);                        /* ① 부모에서 이름 지우기 */
    uint8_t *p = bget((uint32_t)b);
    memset(p + (slot % DPB) * DIRENT_SIZE, 0, DIRENT_SIZE);
    bwrite((uint32_t)b, p);
    iget((uint32_t)pinum, &pd); pd.nlink--; iput((uint32_t)pinum, &pd);   /* ② 자식의 ".." 가 사라짐 */
    dinode_t z = { 0 };
    iput((uint32_t)inum, &z);                                       /* ③ inode → ④ 블록 → ⑤ inode 비트 */
    uint8_t *bm = bget(SB.dbitmap);
    for (int k = 0; k < NDIRECT; k++)
        if (blk_ok(d.direct[k])) { uint32_t x = d.direct[k] - SB.data_start; bm[x / 8] &= (uint8_t)~(1u << (x % 8)); }
    bwrite(SB.dbitmap, bm);
    return bm_set(SB.ibitmap, (uint32_t)inum, 0);
}
static void ex1(void) {
    printf("-- 연습 1: rmdir\n");
    dinode_t ino;
    CHECK(fresh(64, 80) == 0 && fs_mkdir("/d") == 1 && fs_create("/d/x") == 2);
    int fb = fs_free_blocks(), fi = fs_free_inodes();
    CHECK(fs_rmdir("/d") == -ENOTEMPTY && fs_rmdir("/d/x") == -ENOTDIR && fs_rmdir("/") == -EINVAL);
    CHECK(fs_unlink("/d/x") == 0);
    io_reset();
    CHECK(fs_rmdir("/d") == 0);
    printf("  io rmdir /d  R=%ld W=%ld | %s\n", D.nread, D.nwrite, D.tbuf);
    CHECK(IO(5, 5));
    CHECK(fs_stat(ROOT_INO, &ino) == 0 && ino.nlink == 2 && fs_namei("/d", NULL) == -ENOENT);
    CHECK(fs_free_blocks() == fb + 1 && fs_free_inodes() == fi + 2 && fsck0() == 0);
}

/* ---------------- 연습 2: 큰 디렉터리 ---------------- */
static void ex2(void) {
    printf("-- 연습 2: directory spanning two blocks, slot reuse\n");
    dinode_t ino;
    char path[32];
    static char ls[2048];
    CHECK(fresh(64, 208) == 0 && fs_mkdir("/big") == 1);
    int ok = 1;
    for (int i = 0; i < 130 && ok; i++) { snprintf(path, sizeof path, "/big/f%d", i); ok = fs_create(path) == 2 + i; }
    CHECK(ok && fs_namei("/big", &ino) == 1 && ino.size == 132 * DIRENT_SIZE && ino.direct[1] != 0);
    CHECK(fs_ls("/big", ls, sizeof ls) == 130);
    g_cache = 0;
    io_reset(); fs_namei("/big/f0", NULL);   long r0 = D.nread;
    io_reset(); fs_namei("/big/f129", NULL); long r1 = D.nread;
    g_cache = 1;
    printf("  namei cache=0: /big/f0 R=%ld, /big/f129 R=%ld\n", r0, r1);
    CHECK(r0 == 5 && r1 == 6);
    CHECK(fs_unlink("/big/f5") == 0);
    io_reset();
    CHECK(fs_create("/big/new") == 7);                                /* f5 가 쓰던 inode 7 도 재사용 (first-fit) */
    printf("  io create into reused slot  R=%ld W=%ld | %s\n", D.nread, D.nwrite, D.tbuf);
    CHECK(D.nwrite == 3 && fs_namei("/big", &ino) == 1 && ino.size == 132 * DIRENT_SIZE);
    CHECK(fs_ls("/big", ls, sizeof ls) == 130 && strstr(ls, "f4 new f6") != NULL && fsck0() == 0);
}

/* ---------------- 연습 3: 크기 → 블록 ---------------- */
static uint32_t blocks_for(uint64_t size) {
    uint64_t d = (size + BSIZE - 1) / BSIZE;
    return (uint32_t)(d + (d > NDIRECT ? 1 : 0));
}
static void ex3(void) {
    printf("-- 연습 3: size -> blocks, max file size\n");
    uint64_t max1 = (uint64_t)(NDIRECT + NINDIRECT) * BSIZE;
    uint64_t max2 = (uint64_t)(NDIRECT + NINDIRECT + (uint64_t)NINDIRECT * NINDIRECT) * BSIZE;
    printf("  max with 1 indirect = %llu B, + double indirect = %llu B\n", (unsigned long long)max1, (unsigned long long)max2);
    CHECK(max1 == 4243456 && max2 == 4299210752ULL && max2 > UINT32_MAX);
    static const uint32_t sizes[] = { 1, 4096, 4097, 12 * 4096, 12 * 4096 + 1, 20 * 4096, 54 * 4096 };
    static uint8_t buf[54 * 4096];
    int ok = 1;
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        if (fresh(64, 80) != 0 || fs_create("/f") != 1 || fs_write(1, 0, buf, sizes[i]) != (long)sizes[i]) { ok = 0; break; }
        int used = 55 - fs_free_blocks();
        printf("  size %6u -> used %2d blocks (formula %u)\n", sizes[i], used, blocks_for(sizes[i]));
        ok &= used == (int)blocks_for(sizes[i]);
    }
    CHECK(ok);
}

/* ---------------- 연습 4: 크래시 주입 스윕 ---------------- */
static uint8_t g_blk[BSIZE];
static int setup_dir(void)  { return fresh(64, 80) == 0 && fs_mkdir("/d") == 1 ? 0 : -1; }
static int setup_file(void) { return setup_dir() == 0 && fs_create("/d/x") == 2 && fs_write(2, 0, g_blk, BSIZE) == BSIZE ? 0 : -1; }
static void op_create_write(void) { int i = fs_create("/d/x"); if (i >= 0) fs_write((uint32_t)i, 0, g_blk, BSIZE); }
static void op_link_good(void) { fs_link("/d/x", "/y"); }
static void op_link_buggy(void) {                       /* 이름을 먼저 걸고 nlink 를 나중에 올린다 (틀린 순서) */
    op_begin();
    dinode_t t;
    int ti = namei_("/d/x", &t);
    if (ti < 0) return;
    dir_add(ROOT_INO, "y", (uint32_t)ti, 0);
    iget((uint32_t)ti, &t); t.nlink++; iput((uint32_t)ti, &t);
}
/* 위험한 상태: 이름이 빈 inode 를 가리킴 / 블록 이중 소유 / 쓰는 블록·inode 가 비트맵에서 비어 있음 / nlink < 실제 이름 수 */
static int dangerous(const fsck_t *r) {
    int bad = r->n[FK_DANGLING] + r->n[FK_DUP_BLOCK] + r->n[FK_BAD_PTR] + r->n[FK_UNMARKED];
    for (const char *p = r->log; (p = strstr(p, "[")) != NULL; p++) {
        unsigned a, b;
        if (sscanf(p, "[nlink] inode %*u nlink = %u, but directory entries naming it = %u", &a, &b) == 2 && a < b) bad++;
        if (!strncmp(p, "[inode-bitmap]", 14) && strstr(p, "bitmap says free") && strstr(p, "bitmap says free") < strchr(p, '\n')) bad++;
    }
    return bad;
}
static void sweep(const char *what, int (*setup)(void), void (*op)(void), int *n_safe, int *n_bad) {
    *n_safe = *n_bad = 0;
    if (setup() != 0) { *n_bad = -1; return; }
    long w0 = D.writes_total; op(); long nw = D.writes_total - w0;   /* 크래시 없이 몇 번 쓰나 */
    printf("  %s: %ld writes\n", what, nw);
    for (long k = 0; k <= nw; k++) {
        if (setup() != 0) { *n_bad = -1; return; }
        D.crash_after = D.writes_total + k;
        op();
        D.crash_after = -1;                                           /* 전원 복구 → 재마운트 → fsck */
        fsck_t r;
        int n = fs_mount() == 0 ? fsck(0, &r) : -1;
        int bad = n < 0 ? 1 : dangerous(&r);
        printf("    crash after %ld: %d finding(s)%s", k, n, bad ? "  ** DANGEROUS **" : "");
        for (int f = 0; n > 0 && f < FK_N; f++) if (r.n[f]) printf(" %s=%d", FK_NAME[f], r.n[f]);
        printf("\n");
        if (bad) (*n_bad)++; else if (n > 0) (*n_safe)++;
    }
}
static void ex4(void) {
    printf("-- 연습 4: crash after the k-th block write, then fsck\n");
    int s, b;
    for (int i = 0; i < BSIZE; i++) g_blk[i] = (uint8_t)(i * 7);
    sweep("create+write /d/x", setup_dir, op_create_write, &s, &b);
    CHECK(b == 0 && s >= 3);
    sweep("link /d/x /y (nlink first)", setup_file, op_link_good, &s, &b);
    CHECK(b == 0 && s >= 1);
    sweep("link, name first (buggy)", setup_file, op_link_buggy, &s, &b);
    CHECK(b >= 1);
}

/* ---------------- 연습 5: vsfs.py 형식 ---------------- */
static void dump_vsfs(char *out, size_t cap) {          /* "inode bitmap ..\ninodes ..\ndata bitmap ..\n" */
    size_t n = 0;
    op_begin();
    n += (size_t)snprintf(out + n, cap - n, "inode bitmap  ");
    for (uint32_t i = 0; i < SB.ninodes; i++) n += (size_t)snprintf(out + n, cap - n, "%d", bm_get(SB.ibitmap, i));
    n += (size_t)snprintf(out + n, cap - n, "\ninodes       ");
    for (uint32_t i = 0; i < SB.ninodes; i++) {
        dinode_t d; iget(i, &d);
        if (d.type == T_FREE) n += (size_t)snprintf(out + n, cap - n, "[]");
        else n += (size_t)snprintf(out + n, cap - n, "[%c a:%d r:%u]", d.type == T_DIR ? 'd' : 'f',
                                   d.direct[0] ? (int)(d.direct[0] - SB.data_start) : -1, d.nlink);
    }
    n += (size_t)snprintf(out + n, cap - n, "\ndata bitmap   ");
    for (uint32_t i = 0; i < SB.ndata; i++) n += (size_t)snprintf(out + n, cap - n, "%d", bm_get(SB.dbitmap, i));
    snprintf(out + n, cap - n, "\n");
}
static void ex5(void) {
    printf("-- 연습 5: same state as OSTEP vsfs.py -s 17 -n 6 -i 16 -d 16 -c\n");
    /* vsfs.py 는 inode 16 개·데이터 16 블록: 우리 배치로 3 + 1 + 16 = 20 블록 디스크 */
    CHECK(fresh(20, 16) == 0 && SB.ndata == 16);
    CHECK(fs_mkdir("/u") == 1 && fs_create("/a") == 2 && fs_unlink("/a") == 0 &&
          fs_mkdir("/z") == 2 && fs_mkdir("/s") == 3 && fs_create("/z/x") == 4);
    static char got[1024];
    dump_vsfs(got, sizeof got);
    printf("%s", got);
    const char *want =                                               /* vsfs.py 실제 출력의 마지막 상태 */
        "inode bitmap  1111100000000000\n"
        "inodes       [d a:0 r:5][d a:1 r:2][d a:2 r:2][d a:3 r:2][f a:-1 r:1][][][][][][][][][][][]\n"
        "data bitmap   1111000000000000\n";
    CHECK(strcmp(got, want) == 0);
}

int main(void) {
    ex1(); ex2(); ex3(); ex4(); ex5();
    disk_close();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
