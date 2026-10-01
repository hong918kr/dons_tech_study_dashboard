/*
 * C40_vsfs_math.c — vsfs 의 "숫자" 를 직접 계산해 보기
 *   1) inode 번호 → inode 블록 → 섹터 번호 (OSTEP 40.3 공식)
 *   2) multi-level index 로 표현 가능한 최대 파일 크기
 *   3) 파일 오프셋 → 어느 포인터(direct/indirect/double/triple)를 타야 하나 + 메타데이터 읽기 수
 *   4) 경로 깊이에 따른 open()/create() I/O 수 (캐시 없음 가정, 그림 40.3/40.4 모델)
 *   5) 포인터 방식 vs extent 방식의 메타데이터 양
 *
 * build: cc -Wall -Wextra -O0 code/C40_vsfs_math.c -o .work/bin/C40_vsfs_math
 */
#include <stdint.h>
#include <stdio.h>

#define KB 1024ULL

/* vsfs 파라미터 (원문 그대로) */
static const uint64_t blockSize = 4 * KB;
static const uint64_t sectorSize = 512;
static const uint64_t inodeSize = 256;
static const uint64_t inodeStartAddr = 12 * KB; /* S(0KB) i-bmap(4KB) d-bmap(8KB) 다음 */

static void inode_location(uint64_t inum) {
    uint64_t blk = (inum * inodeSize) / blockSize;           /* inode 테이블 안의 블록 번호 */
    uint64_t byteAddr = blk * blockSize + inodeStartAddr;    /* 그 블록의 바이트 주소 */
    uint64_t sector = byteAddr / sectorSize;                 /* 디스크가 아는 섹터 번호 */
    uint64_t slot = (inum * inodeSize) % blockSize / inodeSize;
    printf("inode %2llu -> iblock %llu (byte %3lluKB) -> sector %3llu, slot %2llu in block\n",
           (unsigned long long)inum, (unsigned long long)blk, (unsigned long long)(byteAddr / KB),
           (unsigned long long)sector, (unsigned long long)slot);
}

static void max_file_size(uint64_t bs, uint64_t ptrSize, int ndirect) {
    uint64_t p = bs / ptrSize; /* 간접 블록 하나에 들어가는 포인터 수 */
    uint64_t d = (uint64_t)ndirect, s = p, dd = p * p, t = p * p * p;
    printf("block %4lluB, ptr %lluB (%llu ptr/blk):\n", (unsigned long long)bs,
           (unsigned long long)ptrSize, (unsigned long long)p);
    printf("  direct only          : %12llu blocks = %10.2f MB\n", (unsigned long long)d,
           (double)(d * bs) / (KB * KB));
    printf("  + single indirect    : %12llu blocks = %10.2f MB\n", (unsigned long long)(d + s),
           (double)((d + s) * bs) / (KB * KB));
    printf("  + double indirect    : %12llu blocks = %10.2f GB\n", (unsigned long long)(d + s + dd),
           (double)((d + s + dd) * bs) / (KB * KB * KB));
    printf("  + triple indirect    : %12llu blocks = %10.2f TB\n",
           (unsigned long long)(d + s + dd + t), (double)((d + s + dd + t) * bs) / (KB * KB * KB * KB));
}

/* 오프셋 → 포인터 경로. 캐시가 전혀 없을 때 inode 외에 읽어야 하는 간접 블록 수 */
static void offset_path(uint64_t off) {
    const uint64_t p = blockSize / 4, nd = 12;
    uint64_t lbn = off / blockSize; /* 논리 블록 번호 */
    const char *kind;
    int extra;
    char idx[64];
    if (lbn < nd) {
        kind = "direct"; extra = 0;
        snprintf(idx, sizeof idx, "ptr[%llu]", (unsigned long long)lbn);
    } else if ((lbn -= nd) < p) {
        kind = "single"; extra = 1;
        snprintf(idx, sizeof idx, "ind[%llu]", (unsigned long long)lbn);
    } else if ((lbn -= p) < p * p) {
        kind = "double"; extra = 2;
        snprintf(idx, sizeof idx, "dind[%llu][%llu]", (unsigned long long)(lbn / p),
                 (unsigned long long)(lbn % p));
    } else {
        lbn -= p * p;
        kind = "triple"; extra = 3;
        snprintf(idx, sizeof idx, "tind[%llu][%llu][%llu]", (unsigned long long)(lbn / (p * p)),
                 (unsigned long long)(lbn / p % p), (unsigned long long)(lbn % p));
    }
    printf("offset %14llu B : %-6s %-20s reads = inode + %d indirect + 1 data = %d\n",
           (unsigned long long)off, kind, idx, extra, extra + 2);
}

int main(void) {
    printf("== 1. inode number -> sector (vsfs: 256B inode, 4KB block, table @12KB) ==\n");
    uint64_t inums[] = { 0, 2, 15, 16, 32, 79 };
    for (unsigned i = 0; i < sizeof inums / sizeof inums[0]; i++) inode_location(inums[i]);

    printf("\n== 2. multi-level index max file size (12 direct) ==\n");
    max_file_size(4 * KB, 4, 12);
    max_file_size(1 * KB, 4, 12);

    printf("\n== 3. file offset -> pointer path (4KB blocks, 1024 ptr/blk) ==\n");
    uint64_t offs[] = { 0, 40 * KB, 48 * KB, 4 * KB * KB, 4 * KB * KB + 48 * KB,
                        1ULL << 30, 5ULL << 30 };
    for (unsigned i = 0; i < sizeof offs / sizeof offs[0]; i++) offset_path(offs[i]);

    printf("\n== 4. I/O count without cache (Fig 40.3 / 40.4 model) ==\n");
    printf("depth  path                 open(read)  create   +3 alloc writes  read 3 blks\n");
    for (int depth = 1; depth <= 5; depth++) {
        char path[64] = "";
        for (int i = 1; i < depth; i++) snprintf(path + i * 2 - 2, sizeof path - (size_t)(i * 2 - 2), "/d");
        snprintf(path + (depth - 1) * 2, sizeof path - (size_t)(depth - 1) * 2, "/f");
        /* open: 경로의 디렉터리마다 inode+data 2회, 마지막 파일 inode 1회 */
        int open_ios = 2 * depth + 1;
        /* create: 부모까지 탐색 2*depth, ibmap R/W 2, 부모 dir data W 1, 새 inode R/W 2, 부모 inode W 1 */
        int create_ios = 2 * depth + 6;
        int writes3 = 3 * 5; /* allocating write 1회 = inode R, dbmap R/W, data W, inode W */
        int reads3 = 3 * 3;  /* read 1회 = inode R, data R, inode W(atime) */
        printf("%5d  %-20s %10d %7d %16d %12d\n", depth, path, open_ios, create_ios, writes3, reads3);
    }
    printf("(depth=2 is /foo/bar: open=5, create=10, write=5 each — matches the book)\n");

    printf("\n== 5. metadata for a contiguous 1 GB file ==\n");
    uint64_t nblk = (1ULL << 30) / blockSize, p = blockSize / 4;
    uint64_t rest = nblk - 12;
    uint64_t single = rest < p ? rest : p;
    rest -= single;
    uint64_t dind_children = (rest + p - 1) / p;
    uint64_t ind_blocks = 1 + 1 + dind_children; /* single + double-root + 2단계 간접 블록 */
    printf("pointer-based: %llu data ptrs, %llu indirect blocks = %llu KB of metadata\n",
           (unsigned long long)nblk, (unsigned long long)ind_blocks,
           (unsigned long long)(ind_blocks * blockSize / KB));
    printf("extent-based : 1 extent (start, len=%llu) = 12 bytes inside the inode\n",
           (unsigned long long)nblk);
    return 0;
}
