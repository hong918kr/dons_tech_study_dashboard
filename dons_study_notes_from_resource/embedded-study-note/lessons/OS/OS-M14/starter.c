/* OS-M14 파일 시스템 구현: VSFS (inode·비트맵·디렉터리)
 *
 * OSTEP 40장의 vsfs 를 진짜 바이트로 만든다. 디스크 = /tmp/OS-M14_work/ 안의 mkstemp 파일 (열자마자 unlink 하므로
 * 프로그램이 어떻게 끝나도 남지 않는다). 파일 시스템 코드는 bread(blk)/bwrite(blk) (4 KB 블록) 로만 디스크에 닿고,
 * 둘 다 횟수와 순서(trace)를 기록한다.
 *
 *   배치    [0 superblock][1 inode bitmap][2 data bitmap][3.. inode table: 256 B × 16 개/블록][data ...]
 *   inode   type·nlink·size·direct[12]·indirect(1단) → 최대 (12 + 1024) 블록 = 4,243,456 B
 *   dirent  {uint32 inum; char name[28]} 32 B, 블록당 128 개. name[0]==0 이면 빈 칸. 루트 inode = 0 (vsfs.py 와 같음)
 *   캐시    "한 연산(op) 안에서만" 이미 읽은 메타데이터 블록을 다시 읽지 않는다. g_cache=0 이면 OSTEP 그림처럼 매번 읽는다.
 *           쓰기는 늘 즉시 디스크로 (write-through). 연산이 시작될 때 캐시를 비운다.
 *
 *   Part A  mkfs, bmap(직접 → 1단 간접, 할당 포함), namei, create/mkdir, ls — 배치와 이름 공간
 *   Part B  write, read, unlink (+ 하드 링크는 주어짐)                       — 데이터 경로
 *   Part C  fsck: inode·디렉터리에서 비트맵·링크 수를 다시 계산, 주입한 손상을 찾고 고친다
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 * 데모·연습이 엔진만 쓸 때: #define VSFS_NO_MAIN 후 #include "solution.c"
 */
#define _POSIX_C_SOURCE 200809L     /* mkstemp, pread, pwrite, ftruncate, fstat */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define GIVEN   __attribute__((unused))   /* 시작 코드가 아직 안 부르는 도우미의 -Wunused 경고 끄기 */
#define TODO_FN __attribute__((unused))   /* 시작 코드에서는 아직 아무도 안 부르는 TODO 함수 */

/* ============================ 온디스크 형식 (주어짐) ============================ */
enum { BSIZE = 4096, NDIRECT = 12, NINDIRECT = BSIZE / 4, MAXFILE_BLOCKS = NDIRECT + NINDIRECT,
       INODE_SIZE = 256, IPB = BSIZE / INODE_SIZE, DIRENT_SIZE = 32, DPB = BSIZE / DIRENT_SIZE,
       NAME_MAX_ = 27, ROOT_INO = 0, NCACHE = 16, FS_MAX_BLOCKS = 8192, FS_MAX_INODES = 1024 };
enum { T_FREE = 0, T_FILE = 1, T_DIR = 2 };
#define VSFS_MAGIC 0x53465356u            /* 리틀 엔디언 바이트로 "VSFS" */

typedef struct {
    uint32_t magic, nblocks, ninodes;
    uint32_t ibitmap, dbitmap;            /* 비트맵 블록 번호 */
    uint32_t inode_start, ninode_blocks;
    uint32_t data_start, ndata;           /* 데이터 영역 [data_start, nblocks) */
} superblock_t;

typedef struct {
    uint16_t type, nlink;
    uint32_t size;
    uint32_t direct[NDIRECT];             /* 절대 블록 번호, 0 = 없음(hole) — 블록 0 은 superblock 이라 겹치지 않음 */
    uint32_t indirect;                    /* 블록 번호 1024 개를 담은 블록 */
    uint8_t  pad[INODE_SIZE - 60];
} dinode_t;
_Static_assert(sizeof(dinode_t) == INODE_SIZE, "inode = 256 B");

typedef struct { uint32_t inum; char name[NAME_MAX_ + 1]; } dirent_t;
_Static_assert(sizeof(dirent_t) == DIRENT_SIZE, "dirent = 32 B");

/* ============================ 블록 장치 (주어짐) ============================ */
static struct {
    int fd; uint32_t nblocks;
    long nread, nwrite;                   /* 테스트가 보는 카운터 (io_reset 으로 0) */
    long crash_after;                     /* ≥ 0 이면 그 횟수 이후의 bwrite 는 "전원이 나가서" 디스크에 안 닿는다 */
    long writes_total;
    char tbuf[4096]; size_t tlen;         /* "R I0 R D0 W dbm ..." */
} D = { .fd = -1, .crash_after = -1 };
static superblock_t SB;                   /* 마운트 중에는 메모리에 있다 (OSTEP 40.6 의 가정) */
static int g_cache = 1;

static void blk_label(uint32_t b, char *out, size_t cap) {
    if (b == 0) snprintf(out, cap, "sb");
    else if (SB.magic != VSFS_MAGIC) snprintf(out, cap, "B%u", b);
    else if (b == SB.ibitmap) snprintf(out, cap, "ibm");
    else if (b == SB.dbitmap) snprintf(out, cap, "dbm");
    else if (b >= SB.inode_start && b < SB.inode_start + SB.ninode_blocks) snprintf(out, cap, "I%u", b - SB.inode_start);
    else if (b >= SB.data_start) snprintf(out, cap, "D%u", b - SB.data_start);
    else snprintf(out, cap, "B%u", b);
}
static void trace_add(char rw, uint32_t b) {
    char l[24]; blk_label(b, l, sizeof l);
    if (D.tlen + 32 >= sizeof D.tbuf) return;
    D.tlen += (size_t)snprintf(D.tbuf + D.tlen, sizeof D.tbuf - D.tlen, "%s%c %s", D.tlen ? " " : "", rw, l);
}
static void io_reset(void) { D.nread = D.nwrite = 0; D.tlen = 0; D.tbuf[0] = 0; }

static void disk_close(void) { if (D.fd >= 0) close(D.fd); D.fd = -1; memset(&SB, 0, sizeof SB); }

/* nblocks × 4 KB 짜리 빈 디스크. 이름은 만들자마자 지운다 → fd 를 닫으면(프로세스가 죽어도) 공간이 회수된다. */
static int disk_open(uint32_t nblocks) {
    disk_close();
    if (nblocks < 8 || nblocks > FS_MAX_BLOCKS) return -EINVAL;
    int fd = -1;
    for (int tries = 0; tries < 3 && fd < 0; tries++) {
        char path[] = "/tmp/OS-M14_work/disk.XXXXXX";
        if (mkdir("/tmp/OS-M14_work", 0700) != 0 && errno != EEXIST) return -errno;
        fd = mkstemp(path);
        if (fd >= 0) unlink(path);
    }
    rmdir("/tmp/OS-M14_work");            /* 비었으면 지운다 (다른 실행이 쓰는 중이면 실패해도 무방) */
    if (fd < 0) return -errno;
    if (ftruncate(fd, (off_t)nblocks * BSIZE) != 0) { int e = errno; close(fd); return -e; }
    D.fd = fd; D.nblocks = nblocks; D.crash_after = -1; D.writes_total = 0;
    io_reset();
    return 0;
}

static int bread(uint32_t b, void *buf) {
    if (D.fd < 0 || b >= D.nblocks) return -EIO;
    D.nread++; trace_add('R', b);
    return pread(D.fd, buf, BSIZE, (off_t)b * BSIZE) == BSIZE ? 0 : -EIO;
}
static int bwrite(uint32_t b, const void *buf) {
    if (D.fd < 0 || b >= D.nblocks) return -EIO;
    D.nwrite++; trace_add('W', b);
    if (D.crash_after >= 0 && D.writes_total >= D.crash_after) return 0;   /* 전원 차단 이후: 조용히 버려짐 */
    D.writes_total++;
    return pwrite(D.fd, buf, BSIZE, (off_t)b * BSIZE) == BSIZE ? 0 : -EIO;
}

/* ---- 연산 범위 블록 캐시: 슬롯 포인터는 다음 bget 전까지만 쓴다 (넘겨서 쥐고 있지 않는다) ---- */
static struct { int valid; uint32_t blk; unsigned stamp; uint8_t d[BSIZE]; } C[NCACHE];
static unsigned c_clock;
static void op_begin(void) { for (int i = 0; i < NCACHE; i++) C[i].valid = 0; }
static uint8_t *cslot(uint32_t b) {
    int v = 0;
    for (int i = 0; i < NCACHE; i++) {
        if (!C[i].valid) { v = i; break; }
        if (C[i].stamp < C[v].stamp) v = i;
    }
    C[v].valid = 1; C[v].blk = b; C[v].stamp = ++c_clock;
    return C[v].d;
}
static uint8_t *cfind(uint32_t b) {
    if (!g_cache) return NULL;
    for (int i = 0; i < NCACHE; i++)
        if (C[i].valid && C[i].blk == b) { C[i].stamp = ++c_clock; return C[i].d; }
    return NULL;
}
/* 메타데이터 블록 읽기: 이 연산에서 이미 읽었으면 재사용 (g_cache=1), 아니면 디스크에서. 실패하면 NULL */
static uint8_t *bget(uint32_t b) {
    uint8_t *p = cfind(b);
    if (p) return p;
    p = cslot(b);
    if (bread(b, p) != 0) { for (int i = 0; i < NCACHE; i++) if (C[i].d == p) C[i].valid = 0; return NULL; }
    return p;
}
/* 새로 할당한 블록: 읽을 필요 없이 0 으로 채운 버퍼 */
GIVEN static uint8_t *bget_zero(uint32_t b) {
    uint8_t *p = cfind(b);
    if (!p) p = cslot(b);
    memset(p, 0, BSIZE);
    return p;
}

/* ---- inode · 비트맵 도우미 (주어짐) ---- */
static int blk_ok(uint32_t b) { return b >= SB.data_start && b < SB.nblocks; }
static uint32_t iblock(uint32_t inum) { return SB.inode_start + inum / IPB; }
static int iget(uint32_t inum, dinode_t *out) {
    if (SB.magic != VSFS_MAGIC || inum >= SB.ninodes) return -EINVAL;
    uint8_t *p = bget(iblock(inum));
    if (!p) return -EIO;
    memcpy(out, p + (inum % IPB) * INODE_SIZE, sizeof *out);
    return 0;
}
static int iput(uint32_t inum, const dinode_t *in) {        /* read-modify-write 한 블록 (캐시에 있으면 읽기 생략) */
    if (SB.magic != VSFS_MAGIC || inum >= SB.ninodes) return -EINVAL;
    uint8_t *p = bget(iblock(inum));
    if (!p) return -EIO;
    memcpy(p + (inum % IPB) * INODE_SIZE, in, sizeof *in);
    return bwrite(iblock(inum), p);
}
GIVEN static int bm_get(uint32_t bmblk, uint32_t i) {
    uint8_t *p = bget(bmblk);
    return p ? (p[i / 8] >> (i % 8)) & 1 : -EIO;
}
GIVEN static int bm_set(uint32_t bmblk, uint32_t i, int v) {
    uint8_t *p = bget(bmblk);
    if (!p) return -EIO;
    if (v) p[i / 8] |= (uint8_t)(1u << (i % 8)); else p[i / 8] &= (uint8_t)~(1u << (i % 8));
    return bwrite(bmblk, p);
}
/* first-fit: 처음 보이는 0 비트를 1 로 바꾸고 바로 디스크에 쓴다 */
GIVEN static int bm_alloc(uint32_t bmblk, uint32_t n) {
    uint8_t *p = bget(bmblk);
    if (!p) return -EIO;
    for (uint32_t i = 0; i < n; i++)
        if (!((p[i / 8] >> (i % 8)) & 1)) {
            p[i / 8] |= (uint8_t)(1u << (i % 8));
            int rc = bwrite(bmblk, p);
            return rc < 0 ? rc : (int)i;
        }
    return -ENOSPC;
}
GIVEN static int ialloc(void) { return bm_alloc(SB.ibitmap, SB.ninodes); }
GIVEN static long balloc(void) { int d = bm_alloc(SB.dbitmap, SB.ndata); return d < 0 ? d : (long)(SB.data_start + (uint32_t)d); }

/* ---- 경로 문자열 도우미 (주어짐) ---- */
/* 다음 이름 하나를 name 에. 1 = 얻음, 0 = 끝, -ENAMETOOLONG */
GIVEN static int path_next(const char **pp, char name[NAME_MAX_ + 1]) {
    const char *p = *pp;
    while (*p == '/') p++;
    if (*p == 0) { *pp = p; return 0; }
    const char *s = p;
    while (*p && *p != '/') p++;
    size_t n = (size_t)(p - s);
    if (n > NAME_MAX_) return -ENAMETOOLONG;
    memcpy(name, s, n); name[n] = 0; *pp = p;
    return 1;
}
/* "/a/b/c" → dir "/a/b/", name "c". "/" 처럼 마지막 이름이 없으면 -EINVAL */
GIVEN static int split_last(const char *path, char *dir, size_t cap, char name[NAME_MAX_ + 1]) {
    if (!path || path[0] != '/') return -EINVAL;
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') len--;
    size_t i = len;
    while (i > 0 && path[i - 1] != '/') i--;
    size_t nl = len - i;
    if (nl == 0) return -EINVAL;
    if (nl > NAME_MAX_) return -ENAMETOOLONG;
    if (i + 1 > cap) return -ENAMETOOLONG;
    memcpy(name, path + i, nl); name[nl] = 0;
    memcpy(dir, path, i); dir[i] = 0;
    return 0;
}

int fs_mount(void) {                      /* superblock 을 메모리로 (1 read) */
    op_begin();
    uint8_t buf[BSIZE];
    superblock_t s;
    if (bread(0, buf) != 0) return -EIO;
    memcpy(&s, buf, sizeof s);
    if (s.magic != VSFS_MAGIC || s.nblocks != D.nblocks) return -EINVAL;
    SB = s;
    return 0;
}
int fs_stat(uint32_t inum, dinode_t *out) { op_begin(); return iget(inum, out); }
static int count_zero_bits(uint32_t bmblk, uint32_t n) {
    op_begin();
    uint8_t *p = bget(bmblk);
    if (!p) return -EIO;
    int z = 0;
    for (uint32_t i = 0; i < n; i++) z += !((p[i / 8] >> (i % 8)) & 1);
    return z;
}
int fs_free_blocks(void) { return SB.magic == VSFS_MAGIC ? count_zero_bits(SB.dbitmap, SB.ndata) : -EINVAL; }
int fs_free_inodes(void) { return SB.magic == VSFS_MAGIC ? count_zero_bits(SB.ibitmap, SB.ninodes) : -EINVAL; }

/* ======================= Part A — 이름 공간 ======================= */

/* TODO A1: 지금 열린 디스크(D.nblocks)에 빈 파일 시스템을 만든다. ninodes 는 16 의 배수로 올림.
 * 쓰는 블록: superblock, inode bitmap(루트 inode 0 = 1), data bitmap(루트 디렉터리 블록 = 데이터 0번 = 1),
 * inode table 전부(0 으로, 첫 블록에 루트 inode: DIR, nlink 2, size 64, direct[0]=data_start),
 * 루트 디렉터리 블록("." → 0, ".." → 0). 데이터 영역은 지우지 않는다. 읽기 0 회. SB 도 채운다. */
int fs_mkfs(uint32_t ninodes) {
    (void)ninodes;
    return -ENOSYS;
}

/* TODO A2: 파일 블록 번호 fbn → 디스크 블록 번호 (디렉터리도 파일이라 Part A 부터 쓴다).
 * fbn < 12 → direct[fbn], 아니면 indirect 블록의 [fbn-12] 칸. 없으면(0) alloc==0 이면 0(hole) 을,
 * alloc==1 이면 balloc 해서 포인터를 채우고(ino 는 메모리에서 바뀜, 쓰기는 호출자 몫. indirect 블록 안의 칸은 여기서 씀)
 * *fresh = 1. fbn ≥ 1036 → -EFBIG. indirect 블록을 새로 만들 때는 읽지 말고 bget_zero. */
TODO_FN static long bmap(dinode_t *ino, uint32_t fbn, int alloc, int *fresh) {
    (void)ino; (void)fbn; (void)alloc;
    if (fresh) *fresh = 0;
    return -ENOSYS;
}

/* TODO A3a: 디렉터리 dir 에서 name 을 찾아 inode 번호를 돌려준다 (없으면 -ENOENT). slot_out 에 칸 번호.
 * size/32 칸을 차례로 보고, 블록 경계마다 bmap(…, 0, …) + bget. name[0]==0 은 빈 칸. */
static int dir_lookup(const dinode_t *dir, const char *name, uint32_t *slot_out) {
    (void)dir; (void)name; (void)slot_out;
    return -ENOSYS;
}

/* TODO A3b: 절대 경로 → inode 번호 (+ *out 에 그 inode). 루트 inode 를 읽고, 이름마다
 * "지금 inode 가 디렉터리인가(-ENOTDIR) → dir_lookup(-ENOENT) → 다음 inode 읽기". 마지막 inode 도 읽는다
 * (open 이 권한·크기를 보려고 읽는 것과 같음). "/" 는 루트. "//a///b" 처럼 빈 이름은 건너뛴다. */
static int namei_(const char *path, dinode_t *out) {
    (void)path; (void)out;
    return -ENOSYS;
}
int fs_namei(const char *path, dinode_t *out) { op_begin(); return namei_(path, out); }

/* TODO A4a: 디렉터리 dinum 에 (name → inum) 을 넣는다. 빈 칸(name[0]==0)이 있으면 재사용, 없으면 size 끝에 붙인다
 * (블록 경계면 bmap(…, alloc=1) 로 새 블록 → bget_zero). 그리고 size 가 늘었거나 nlink_delta ≠ 0 이면
 * 디렉터리 inode 를 한 번만 쓴다 (mkdir 의 부모 nlink++ 를 여기서 같이). */
TODO_FN static int dir_add(uint32_t dinum, const char *name, uint32_t inum, int nlink_delta) {
    (void)dinum; (void)name; (void)inum; (void)nlink_delta;
    return -ENOSYS;
}

/* TODO A4b: create(T_FILE) / mkdir(T_DIR). 부모 경로 찾기 → 부모가 디렉터리인가 → 이름이 이미 있나(-EEXIST) →
 * ialloc → (디렉터리면 balloc + "."·".." 블록 쓰기) → 새 inode 쓰기 → dir_add (디렉터리면 부모 nlink +1).
 * 순서가 중요: 새 inode 를 다 쓴 "뒤에" 이름을 건다. 실패하면 할당한 것을 되돌린다. 성공하면 새 inode 번호. */
static int create_node(const char *path, int type) {
    (void)path; (void)type;
    return -ENOSYS;
}
int fs_create(const char *path) { op_begin(); return create_node(path, T_FILE); }
int fs_mkdir(const char *path)  { op_begin(); return create_node(path, T_DIR); }

/* TODO A5: 디렉터리의 이름들을 칸 순서대로 공백으로 이어 out 에 ("." 과 ".." 은 뺀다). 개수를 돌려준다.
 * 디렉터리가 아니면 -ENOTDIR. */
int fs_ls(const char *path, char *out, size_t cap) {
    op_begin();
    if (cap) out[0] = 0;
    (void)path;
    return -ENOSYS;
}

/* 하드 링크 (주어짐): 순서 = 대상 nlink 를 "먼저" 올리고 그다음 이름을 건다 (크래시 → nlink 가 큼 = 누수일 뿐 안전) */
int fs_link(const char *oldpath, const char *newpath) {
    op_begin();
    dinode_t t, pd;
    int ti = namei_(oldpath, &t);
    if (ti < 0) return ti;
    if (t.type == T_DIR) return -EPERM;
    char dpath[256], name[NAME_MAX_ + 1];
    int rc = split_last(newpath, dpath, sizeof dpath, name);
    if (rc < 0) return rc;
    int pinum = namei_(dpath, &pd);
    if (pinum < 0) return pinum;
    if (pd.type != T_DIR) return -ENOTDIR;
    if (dir_lookup(&pd, name, NULL) >= 0) return -EEXIST;
    t.nlink++;
    if ((rc = iput((uint32_t)ti, &t)) < 0) return rc;
    if ((rc = dir_add((uint32_t)pinum, name, (uint32_t)ti, 0)) < 0) {
        iget((uint32_t)ti, &t); t.nlink--; iput((uint32_t)ti, &t);
        return rc;
    }
    return 0;
}

/* ======================= Part B — 데이터 경로 ======================= */

/* TODO B1: inum 파일의 off 부터 n 바이트를 쓴다. 블록마다 bmap(alloc=1).
 * 블록 전체를 덮으면 읽지 않고 쓴다. 일부만 덮을 때: 새 블록(fresh)이면 0 으로 채운 뒤, 기존 블록이면 읽어서(RMW).
 * 끝나면 size = max(size, off+쓴 양). inode 가 바뀌었을 때만 한 번 쓴다 (덮어쓰기만 하면 inode 쓰기 없음).
 * 반환: 쓴 바이트 (도중에 공간이 떨어지면 거기까지 = 짧은 쓰기), 하나도 못 썼으면 -ENOSPC / -EFBIG.
 * 파일이 아니면 -EISDIR(디렉터리) / -ENOENT(빈 inode). */
long fs_write(uint32_t inum, uint32_t off, const void *buf, uint32_t n) {
    op_begin();
    (void)inum; (void)off; (void)buf; (void)n;
    return -ENOSYS;
}

/* TODO B2: inum 파일의 off 부터 최대 n 바이트를 읽는다 (size 에서 자름). hole(포인터 0)은 0 으로 채우고 I/O 없음.
 * 비트맵은 절대 읽지 않는다 (OSTEP: "reads don't access allocation structures"). 디렉터리면 -EISDIR. */
long fs_read(uint32_t inum, uint32_t off, void *buf, uint32_t n) {
    op_begin();
    (void)inum; (void)off; (void)buf; (void)n;
    return -ENOSYS;
}

/* TODO B3: 파일 이름 하나를 지운다. 순서: ① 디렉터리 칸을 0 으로 (이름 먼저) ② nlink - 1, 남으면 inode 만 쓰고 끝
 * ③ 0 이면 inode 를 0 으로 쓰고 ④ 그 inode 가 쥐던 블록(간접 블록 포함)을 data bitmap 에서 한 번에 지우고
 * ⑤ inode bitmap 비트를 지운다. 디렉터리면 -EISDIR, "." ".." "/" 는 -EINVAL. */
int fs_unlink(const char *path) {
    op_begin();
    (void)path;
    return -ENOSYS;
}

/* ======================= Part C — fsck ======================= */
enum { FK_BAD_PTR, FK_DUP_BLOCK, FK_DANGLING, FK_ORPHAN, FK_NLINK, FK_IBITMAP, FK_LEAKED, FK_UNMARKED, FK_N };
static const char *const FK_NAME[FK_N] = { "bad-ptr", "dup-block", "dangling-dirent", "orphan-inode",
                                           "nlink", "inode-bitmap", "leaked-block", "unmarked-block" };
typedef struct { int n[FK_N]; int total; char log[4096]; size_t len; } fsck_t;

__attribute__((format(printf, 3, 4), unused))
static void fk_note(fsck_t *r, int kind, const char *fmt, ...) {
    r->n[kind]++; r->total++;
    if (r->len + 64 >= sizeof r->log) return;
    int k = snprintf(r->log + r->len, sizeof r->log - r->len, "  [%s] ", FK_NAME[kind]);
    if (k > 0) r->len += (size_t)k;
    va_list ap; va_start(ap, fmt);
    k = vsnprintf(r->log + r->len, sizeof r->log - r->len - 1, fmt, ap);
    va_end(ap);
    if (k > 0) r->len = r->len + (size_t)k < sizeof r->log - 2 ? r->len + (size_t)k : sizeof r->log - 2;
    r->log[r->len++] = '\n'; r->log[r->len] = 0;
}
/* 디렉터리의 k 번째 블록 (fsck 용: 범위를 벗어난 포인터는 0) */
GIVEN static uint32_t fk_dir_block(const dinode_t *d, uint32_t k) {
    uint32_t b = 0;
    if (k < NDIRECT) b = d->direct[k];
    else if (k - NDIRECT < NINDIRECT && blk_ok(d->indirect)) {
        const uint8_t *p = bget(d->indirect);
        if (p) memcpy(&b, p + 4 * (k - NDIRECT), 4);
    }
    return blk_ok(b) ? b : 0;
}

/* TODO C1: 디스크 전체 일관성 검사. 반환 = 찾은 문제 수 (r->n[] 종류별, r->log 설명). repair=1 이면 고친다.
 * 같은 손상에 대해 repair=0 과 repair=1 이 같은 개수를 보고해야 한다.
 *  1) inode 표: 할당된 inode 마다 direct/indirect/indirect 안의 칸 포인터 검사.
 *     범위 밖(데이터 영역이 아님) → FK_BAD_PTR (고침: 포인터 0). 이미 다른 inode 소유 → FK_DUP_BLOCK
 *     (고침: 둘 다 비어 있는 블록에 복사해 나중 inode 에 준다 = e2fsck 의 clone). 그 밖엔 owner[b] = inode.
 *  2) 루트부터 디렉터리 BFS: 칸이 가리키는 inode 가 범위 밖이거나 비었으면 FK_DANGLING (고침: 칸 지움).
 *     아니면 refs[inum]++ ("." ".." 포함). 하위 디렉터리는 큐에.
 *  3) 할당됐는데 refs == 0 → FK_ORPHAN (고침: 블록·inode 반납). nlink ≠ refs → FK_NLINK (고침: nlink = refs).
 *  4) inode bitmap ≠ (type != FREE) → FK_IBITMAP.  5) data bitmap: 켜짐인데 주인 없음 → FK_LEAKED,
 *     주인 있는데 꺼짐 → FK_UNMARKED. 고침: 계산한 비트맵을 쓴다. */
int fsck(int repair, fsck_t *r) {
    memset(r, 0, sizeof *r);
    (void)repair;
    return 0;                                                   /* "문제 없음" 이라고 거짓말하는 검사기 */
}

#ifndef VSFS_NO_MAIN
/* ============================ 테스트 하네스 ============================ */
static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)
#define IO(r, w) (D.nread == (r) && D.nwrite == (w))
static void show_io(const char *what) { printf("  io %-26s R=%ld W=%ld | %s\n", what, D.nread, D.nwrite, D.tbuf); }

static void pattern(uint8_t *b, size_t n, uint32_t seed) {     /* xorshift32 바이트 */
    uint32_t x = seed ? seed : 1;
    for (size_t i = 0; i < n; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; b[i] = (uint8_t)x; }
}
static int fresh_fs(uint32_t nblocks, uint32_t ninodes) {
    g_cache = 1;
    if (disk_open(nblocks) < 0) return -1;
    return fs_mkfs(ninodes);
}
static int fsck_total(int repair) { fsck_t r; return fsck(repair, &r); }

static void test_block_layer(void) {
    printf("-- 0: block device layer (given)\n");
    uint8_t a[BSIZE], b[BSIZE];
    CHECK(disk_open(16) == 0);
    struct stat st;
    CHECK(fstat(D.fd, &st) == 0 && st.st_size == 16 * BSIZE && st.st_nlink == 0);   /* 이름 없음 = 남지 않음 */
    pattern(a, BSIZE, 7);
    io_reset();
    CHECK(bwrite(5, a) == 0 && bread(5, b) == 0 && memcmp(a, b, BSIZE) == 0);
    CHECK(IO(1, 1) && bread(16, b) == -EIO);
}

static void part_a(void) {
    printf("-- Part A: mkfs, bmap, namei, create/mkdir, ls (+ I/O counts)\n");
    dinode_t ino;
    char ls[512];
    CHECK(disk_open(64) == 0);
    io_reset();
    CHECK(fs_mkfs(80) == 0);
    show_io("mkfs(64 blk, 80 inodes)");
    CHECK(IO(0, 9));
    if (SB.magic != VSFS_MAGIC) { printf("  (skip rest of Part A: fs_mkfs not implemented)\n"); return; }
    CHECK(fs_mount() == 0 && SB.inode_start == 3 && SB.ninode_blocks == 5 && SB.data_start == 8 && SB.ndata == 56);
    CHECK(fs_stat(ROOT_INO, &ino) == 0 && ino.type == T_DIR && ino.nlink == 2 && ino.size == 64);
    CHECK(fs_free_inodes() == 79 && fs_free_blocks() == 55);
    CHECK(fs_ls("/", ls, sizeof ls) == 0 && strcmp(ls, "") == 0);

    CHECK(fs_mkdir("/a") == 1 && fs_mkdir("/a/b") == 2 && fs_create("/a/b/c") == 3);
    CHECK(fs_namei("/a/b/c", &ino) == 3 && ino.type == T_FILE && ino.nlink == 1 && ino.size == 0);
    CHECK(fs_namei("//a///b/", &ino) == 2 && ino.type == T_DIR);
    CHECK(fs_namei("/a/./b/../b/c", NULL) == 3);
    CHECK(fs_namei("/a/x", NULL) == -ENOENT && fs_namei("/a/b/c/d", NULL) == -ENOTDIR);
    CHECK(fs_namei("a/b", NULL) == -EINVAL && fs_namei("/abcdefghijklmnopqrstuvwxyz0123", NULL) == -ENAMETOOLONG);
    CHECK(fs_stat(1, &ino) == 0 && ino.nlink == 3 && fs_stat(ROOT_INO, &ino) == 0 && ino.nlink == 3);

    /* 경로 깊이와 읽기 횟수: 캐시 없음(OSTEP 그림 40.3 방식) vs 연산 안 재사용 */
    static const char *const paths[4] = { "/", "/a", "/a/b", "/a/b/c" };
    static const long want_off[4] = { 1, 3, 5, 7 }, want_on[4] = { 1, 2, 3, 4 };
    for (int cache = 0; cache <= 1; cache++) {
        g_cache = cache;
        for (int k = 0; k < 4; k++) {
            io_reset();
            int rc = fs_namei(paths[k], NULL);
            char what[64]; snprintf(what, sizeof what, "namei %s cache=%d", paths[k], cache);
            show_io(what);
            CHECK(rc >= 0 && IO(cache ? want_on[k] : want_off[k], 0));
        }
    }
    g_cache = 0; io_reset(); fs_namei("/a/b/c", NULL);
    CHECK(strcmp(D.tbuf, "R I0 R D0 R I0 R D1 R I0 R D2 R I0") == 0);
    g_cache = 1;

    io_reset();
    CHECK(fs_create("/a/b/d") == 4);
    show_io("create /a/b/d");
    CHECK(IO(5, 4) && strcmp(D.tbuf, "R I0 R D0 R D1 R D2 R ibm W ibm W I0 W D2 W I0") == 0);
    io_reset();
    CHECK(fs_mkdir("/a/b/e") == 5);
    show_io("mkdir /a/b/e");
    CHECK(IO(6, 6));
    g_cache = 0; io_reset();
    CHECK(fs_create("/a/b/f") == 6);
    show_io("create /a/b/f cache=0");
    CHECK(IO(12, 4));
    g_cache = 1;

    CHECK(fs_ls("/a/b", ls, sizeof ls) == 4 && strcmp(ls, "c d e f") == 0);
    CHECK(fs_ls("/a/b/c", ls, sizeof ls) == -ENOTDIR);
    CHECK(fs_create("/a/b/d") == -EEXIST && fs_mkdir("/a/b/c/z") == -ENOTDIR && fs_create("/q/z") == -ENOENT);
    CHECK(fs_create("/") == -EINVAL && fs_mkdir("/a/b/.") == -EEXIST);
    CHECK(fs_stat(2, &ino) == 0 && ino.nlink == 3 && ino.size == 6 * DIRENT_SIZE);   /* b: . .. c d e f, e 가 ".." 로 +1 */
    CHECK(fs_stat(5, &ino) == 0 && ino.type == T_DIR && ino.nlink == 2);
    CHECK(fs_namei("/a/b/e/..", NULL) == 2);
    CHECK(fs_free_inodes() == 73 && fs_free_blocks() == 52);   /* inode 7개, 블록 4개 (/, a, b, e) */
    CHECK(fsck_total(0) == 0);

    /* inode 고갈: 80 개 중 루트 포함 79 개 → 80 번째는 -ENOSPC, 되돌리기 후에도 일관 */
    CHECK(fresh_fs(64, 80) == 0);
    int ok = 0, last = 0;
    for (int i = 0; i < 90; i++) {
        char p[32]; snprintf(p, sizeof p, "/f%d", i);
        last = fs_create(p);
        if (last < 0) break;
        ok++;
    }
    CHECK(ok == 79 && last == -ENOSPC && fs_free_inodes() == 0 && fsck_total(0) == 0);
}

static void part_b(void) {
    printf("-- Part B: write, read, indirect growth, holes, link/unlink, ENOSPC\n");
    static uint8_t big[16 * BSIZE], got[16 * BSIZE];
    dinode_t ino;
    CHECK(fresh_fs(64, 80) == 0);
    if (SB.magic != VSFS_MAGIC) { printf("  (skip Part B: fs_mkfs not implemented)\n"); return; }
    int f = fs_create("/f");
    CHECK(f == 1);
    pattern(big, sizeof big, 42);
    io_reset();
    CHECK(fs_write(1, 0, big, BSIZE) == BSIZE);
    show_io("write blk0 (alloc)");
    CHECK(IO(2, 3) && strcmp(D.tbuf, "R I0 R dbm W dbm W D1 W I0") == 0);   /* OSTEP: 할당 쓰기 = 5 I/O */
    io_reset();
    CHECK(fs_read(1, 0, got, BSIZE) == BSIZE && memcmp(got, big, BSIZE) == 0);
    show_io("read blk0");
    CHECK(IO(2, 0));                                                          /* 비트맵 안 읽음 */
    io_reset();
    CHECK(fs_write(1, 0, big, BSIZE) == BSIZE);
    show_io("overwrite full blk0");
    CHECK(IO(1, 1));                                                          /* inode 안 바뀜 → 안 씀 */
    io_reset();
    CHECK(fs_write(1, 10, big + 10, 100) == 100);
    show_io("overwrite 100 B in blk0");
    CHECK(IO(2, 1));                                                          /* 부분 덮어쓰기 = RMW */

    CHECK(fs_write(1, BSIZE, big + BSIZE, 11 * BSIZE) == 11 * BSIZE);        /* 블록 1..11 → direct 다 참 */
    CHECK(fs_stat(1, &ino) == 0 && ino.size == 12 * BSIZE && ino.indirect == 0 && ino.direct[11] != 0);
    io_reset();
    CHECK(fs_write(1, 12 * BSIZE, big + 12 * BSIZE, BSIZE) == BSIZE);
    show_io("write blk12 (new indirect)");
    CHECK(IO(2, 5));
    io_reset();
    CHECK(fs_write(1, 13 * BSIZE, big + 13 * BSIZE, BSIZE) == BSIZE);
    show_io("write blk13");
    CHECK(IO(3, 4));
    io_reset();
    CHECK(fs_read(1, 13 * BSIZE, got, BSIZE) == BSIZE && memcmp(got, big + 13 * BSIZE, BSIZE) == 0);
    show_io("read blk13");
    CHECK(IO(3, 0));
    io_reset();
    CHECK(fs_read(1, 0, got, 14 * BSIZE) == 14 * BSIZE && memcmp(got, big, 14 * BSIZE) == 0);
    printf("  io read 14 blocks cache=1    R=%ld W=%ld\n", D.nread, D.nwrite);
    CHECK(IO(16, 0));
    g_cache = 0; io_reset();
    CHECK(fs_read(1, 0, got, 14 * BSIZE) == 14 * BSIZE);
    printf("  io read 14 blocks cache=0    R=%ld W=%ld\n", D.nread, D.nwrite);
    CHECK(IO(17, 0));
    g_cache = 1;
    CHECK(fs_stat(1, &ino) == 0 && ino.size == 14 * BSIZE && ino.indirect != 0 && fs_free_blocks() == 55 - 15);
    CHECK(fs_read(1, 14 * BSIZE, got, 10) == 0 && fs_read(1, 14 * BSIZE - 3, got, 10) == 3);
    CHECK(fsck_total(0) == 0);

    /* hole 과 최대 크기: 마지막 블록(1035)만 쓰면 indirect + 1 블록만 할당 */
    CHECK(fs_create("/sparse") == 2);
    CHECK(fs_write(2, (MAXFILE_BLOCKS - 1) * BSIZE, big, BSIZE) == BSIZE);
    CHECK(fs_stat(2, &ino) == 0 && ino.size == MAXFILE_BLOCKS * BSIZE && fs_free_blocks() == 55 - 15 - 2);
    memset(got, 0xAA, BSIZE);
    io_reset();
    CHECK(fs_read(2, 5 * BSIZE, got, 100) == 100 && got[0] == 0 && got[99] == 0 && IO(1, 0));
    CHECK(fs_write(2, MAXFILE_BLOCKS * BSIZE, big, 1) == -EFBIG);
    CHECK(fs_write(2, MAXFILE_BLOCKS * BSIZE - 1, big, 2) == 1);

    /* 링크 수와 unlink */
    int before = fs_free_blocks();
    CHECK(fs_link("/f", "/g") == 0 && fs_stat(1, &ino) == 0 && ino.nlink == 2);
    CHECK(fs_unlink("/f") == 0 && fs_namei("/f", NULL) == -ENOENT && fs_free_blocks() == before);
    CHECK(fs_namei("/g", &ino) == 1 && ino.nlink == 1 && fs_read(1, 0, got, BSIZE) == BSIZE && memcmp(got, big, BSIZE) == 0);
    io_reset();
    CHECK(fs_unlink("/g") == 0);
    show_io("unlink /g (14 blk + ind)");
    CHECK(IO(5, 4) && fs_free_blocks() == before + 15 && fs_stat(1, &ino) == 0 && ino.type == T_FREE);
    CHECK(fs_unlink("/g") == -ENOENT && fs_unlink("/") == -EINVAL && fs_unlink("/sparse/x") == -ENOTDIR);
    CHECK(fs_mkdir("/d") >= 0 && fs_unlink("/d") == -EISDIR && fs_unlink("/d/..") == -EINVAL);
    CHECK(fs_read(0, 0, got, 10) == -EISDIR && fs_write(0, 0, big, 10) == -EISDIR);
    CHECK(fsck_total(0) == 0);

    /* 공간 부족: 짧은 쓰기 (55 블록 = direct 12 + indirect 1 + 42) */
    CHECK(fresh_fs(64, 80) == 0 && fs_create("/fill") == 1);
    static uint8_t huge[60 * BSIZE];
    long w = fs_write(1, 0, huge, sizeof huge);
    printf("  short write on full disk: %ld bytes (= %ld blocks)\n", w, w / BSIZE);
    CHECK(w == 54 * BSIZE && fs_free_blocks() == 0 && fs_write(1, (uint32_t)w, huge, 1) == -ENOSPC);
    CHECK(fs_stat(1, &ino) == 0 && ino.size == 54 * BSIZE && fsck_total(0) == 0);
    CHECK(fs_mkdir("/nospace") == -ENOSPC && fs_free_inodes() == 78);         /* 되돌리기 확인 */
    CHECK(fs_unlink("/fill") == 0 && fs_free_blocks() == 55 && fsck_total(0) == 0);
}

/* ---- Part C 손상 주입 도우미 (fsck 를 거치지 않고 디스크를 직접 고친다) ---- */
static void raw_inode(uint32_t inum, dinode_t *io, int write) {
    op_begin();
    if (write) iput(inum, io); else if (iget(inum, io) < 0) memset(io, 0, sizeof *io);
}
static void raw_dbm(uint32_t d, int v) { op_begin(); bm_set(SB.dbitmap, d, v); }
static void raw_clear_dirent(uint32_t dinum, const char *name) {
    op_begin();
    dinode_t d;
    uint8_t *p = iget(dinum, &d) == 0 && blk_ok(d.direct[0]) ? bget(d.direct[0]) : NULL;
    if (!p) return;
    for (int s = 0; s < DPB; s++)
        if (!strcmp((char *)p + s * DIRENT_SIZE + 4, name)) memset(p + s * DIRENT_SIZE, 0, DIRENT_SIZE);
    bwrite(d.direct[0], p);
}
/* 기본 트리: /etc(1) /etc/cfg(2, 2 블록) /log(3, 1 블록) /empty(4) /etc/sub(5) */
static uint8_t g_cfg[2 * BSIZE], g_log[BSIZE];
static int build_base(void) {
    if (fresh_fs(64, 80) != 0) return -1;
    pattern(g_cfg, sizeof g_cfg, 11); pattern(g_log, sizeof g_log, 22);
    if (fs_mkdir("/etc") != 1 || fs_create("/etc/cfg") != 2 || fs_create("/log") != 3 ||
        fs_create("/empty") != 4 || fs_mkdir("/etc/sub") != 5) return -1;
    if (fs_write(2, 0, g_cfg, sizeof g_cfg) != (long)sizeof g_cfg || fs_write(3, 0, g_log, BSIZE) != BSIZE) return -1;
    return 0;
}
static int cfg_ok(void) {
    static uint8_t got[2 * BSIZE];
    return fs_namei("/etc/cfg", NULL) == 2 && fs_read(2, 0, got, sizeof got) == (long)sizeof got &&
           memcmp(got, g_cfg, sizeof got) == 0;
}
/* 손상 하나: 검사만 → 기대한 종류·개수, 고치기 → 같은 개수, 다시 검사 → 0 */
static void expect_fsck(const char *what, const int want[FK_N]) {
    fsck_t r, r2;
    int n = fsck(0, &r);
    printf("  fsck %s: %d problem(s)\n%s", what, n, r.log);
    int same = 1, wt = 0;
    for (int k = 0; k < FK_N; k++) { same &= r.n[k] == want[k]; wt += want[k]; }
    CHECK(same && n == wt);
    int n1 = fsck(1, &r2);
    CHECK(n1 == n && fsck(0, &r2) == 0);
}

static void part_c(void) {
    printf("-- Part C: fsck finds and repairs injected corruption\n");
    dinode_t ino, a;
    int want[FK_N];
    CHECK(build_base() == 0);
    if (SB.magic != VSFS_MAGIC) { printf("  (skip Part C: fs_mkfs not implemented)\n"); return; }
    fsck_t r;
    CHECK(fsck(0, &r) == 0 && r.total == 0);

    /* C-1 leaked block: 비트맵만 켜짐 */
    CHECK(build_base() == 0);
    raw_dbm(40, 1);
    memset(want, 0, sizeof want); want[FK_LEAKED] = 1;
    expect_fsck("leaked block", want);
    CHECK(cfg_ok() && fs_free_blocks() == 55 - 5);

    /* C-2 double-allocated block: /empty 가 /log 의 블록을 가리킴 */
    CHECK(build_base() == 0);
    raw_inode(3, &a, 0); raw_inode(4, &ino, 0);
    ino.direct[0] = a.direct[0]; ino.size = BSIZE; raw_inode(4, &ino, 1);
    memset(want, 0, sizeof want); want[FK_DUP_BLOCK] = 1;
    expect_fsck("double-allocated block", want);
    static uint8_t got[BSIZE];
    CHECK(fs_stat(4, &ino) == 0 && ino.direct[0] != a.direct[0] && ino.direct[0] != 0);
    CHECK(fs_read(4, 0, got, BSIZE) == BSIZE && memcmp(got, g_log, BSIZE) == 0);   /* 복제본 = 같은 내용 */
    CHECK(fs_write(4, 0, "X", 1) == 1 && fs_read(3, 0, got, BSIZE) == BSIZE && memcmp(got, g_log, BSIZE) == 0);

    /* C-3 bad block pointer: 범위 밖 + 메타데이터(inode 표) 를 가리킴 */
    CHECK(build_base() == 0);
    raw_inode(3, &ino, 0); ino.direct[2] = 9999; ino.direct[3] = SB.inode_start; raw_inode(3, &ino, 1);
    memset(want, 0, sizeof want); want[FK_BAD_PTR] = 2;
    expect_fsck("bad block pointers", want);
    CHECK(fs_stat(3, &ino) == 0 && ino.direct[2] == 0 && ino.direct[3] == 0 && fs_read(3, 0, got, BSIZE) == BSIZE &&
          memcmp(got, g_log, BSIZE) == 0);

    /* C-4 dir entry → free inode: 잘못된 unlink 가 inode·블록은 반납했는데 이름을 안 지움 */
    CHECK(build_base() == 0);
    raw_inode(2, &a, 0);
    memset(&ino, 0, sizeof ino); raw_inode(2, &ino, 1);
    raw_dbm(a.direct[0] - SB.data_start, 0); raw_dbm(a.direct[1] - SB.data_start, 0);
    op_begin(); bm_set(SB.ibitmap, 2, 0);
    memset(want, 0, sizeof want); want[FK_DANGLING] = 1;
    expect_fsck("dangling dir entry", want);
    char ls[256];
    CHECK(fs_ls("/etc", ls, sizeof ls) == 1 && strcmp(ls, "sub") == 0 && fs_namei("/etc/cfg", NULL) == -ENOENT);

    /* C-5 wrong link count: 파일 nlink 3 (실제 1), 디렉터리 /etc nlink 2 (실제 3) */
    CHECK(build_base() == 0);
    raw_inode(3, &ino, 0); ino.nlink = 3; raw_inode(3, &ino, 1);
    raw_inode(1, &ino, 0); ino.nlink = 2; raw_inode(1, &ino, 1);
    memset(want, 0, sizeof want); want[FK_NLINK] = 2;
    expect_fsck("wrong link counts", want);
    CHECK(fs_stat(3, &ino) == 0 && ino.nlink == 1 && fs_stat(1, &ino) == 0 && ino.nlink == 3);

    /* C-6 고아 inode: 이름만 사라짐 (unlink 가 ① 다음에 멈춤) → 블록·inode 반납 */
    CHECK(build_base() == 0);
    raw_clear_dirent(ROOT_INO, "log");
    memset(want, 0, sizeof want); want[FK_ORPHAN] = 1;
    expect_fsck("orphan inode", want);
    CHECK(fs_stat(3, &ino) == 0 && ino.type == T_FREE && fs_free_blocks() == 55 - 4 && fs_free_inodes() == 80 - 5);

    /* C-7 다섯 가지를 한꺼번에 + 비트맵 꺼짐 */
    CHECK(build_base() == 0);
    raw_dbm(40, 1);                                                                 /* leaked */
    raw_inode(3, &a, 0); raw_inode(4, &ino, 0);
    ino.direct[0] = a.direct[0]; ino.direct[5] = 7777; ino.size = BSIZE; raw_inode(4, &ino, 1);   /* dup + bad */
    raw_inode(3, &ino, 0); ino.nlink = 9; raw_inode(3, &ino, 1);                     /* nlink */
    raw_inode(5, &a, 0); memset(&ino, 0, sizeof ino); raw_inode(5, &ino, 1);         /* /etc/sub → free inode */
    raw_dbm(a.direct[0] - SB.data_start, 0); op_begin(); bm_set(SB.ibitmap, 5, 0);
    raw_inode(2, &a, 0); raw_dbm(a.direct[1] - SB.data_start, 0);                   /* cfg 블록 1 꺼짐 */
    memset(want, 0, sizeof want);
    want[FK_LEAKED] = 1; want[FK_DUP_BLOCK] = 1; want[FK_BAD_PTR] = 1; want[FK_NLINK] = 2;   /* /etc 도 3 → 2 */
    want[FK_DANGLING] = 1; want[FK_UNMARKED] = 1;
    expect_fsck("all at once", want);
    CHECK(cfg_ok() && fs_ls("/etc", ls, sizeof ls) == 1 && strcmp(ls, "cfg") == 0);
}

int main(void) {
    test_block_layer();
    part_a();
    part_b();
    part_c();
    disk_close();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
#endif /* VSFS_NO_MAIN */
