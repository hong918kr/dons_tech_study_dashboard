// C48_nfs_idempotent.c — NFSv2 설계 포인트 3가지를 진짜 파일로 확인
//
//  1) 멱등성: NFS WRITE 처럼 "절대 offset" 을 담은 pwrite() 는 재시도해도 결과가 같다.
//             반면 "현재 위치에 이어 쓰기"(O_APPEND write) 는 재시도하면 데이터가 중복된다.
//  2) 파일 핸들 = (volume, inode, generation): inode 번호가 재사용되면 generation 으로
//             옛 핸들을 걸러낸다 (ESTALE).  — 작은 in-memory 서버로 흉내
//  3) 서버 write buffering: 디스크에 안 쓰고 성공을 돌려주면, 서버 크래시 후
//             "aaa / yyy / ccc" 같은 섞인 파일이 생긴다 (Figure 48 의 x,y,z 예제).
//
// build: cc -Wall -Wextra -O0 code/C48_nfs_idempotent.c -o .work/bin/C48_nfs_idempotent && .work/bin/C48_nfs_idempotent
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BLK 8

static void show(const char *label, const char *path) {
    char buf[128] = {0};
    int fd = open(path, O_RDONLY);
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    printf("  %-34s size=%2zd  \"%s\"\n", label, n, buf);
}

// ------------------------------------------------------------- 1) idempotency
static void part1(void) {
    const char *p = ".work/c48_idem.txt";
    printf("[1] 재시도(retry) 시 멱등성 비교 — 응답이 유실돼 클라이언트가 같은 요청을 3번 보냈다고 가정\n");

    int fd = open(p, O_CREAT | O_TRUNC | O_RDWR, 0644);
    pwrite(fd, "HEADER..", BLK, 0);
    for (int retry = 0; retry < 3; retry++)          // NFS WRITE(fh, offset=8, count=8, data)
        pwrite(fd, "BODY....", BLK, 8);
    close(fd);
    show("NFS식 WRITE(offset=8) x3:", p);

    fd = open(p, O_CREAT | O_TRUNC | O_RDWR, 0644);
    pwrite(fd, "HEADER..", BLK, 0);
    close(fd);
    fd = open(p, O_WRONLY | O_APPEND);
    for (int retry = 0; retry < 3; retry++)          // "현재 위치에 이어서 써라" (서버가 offset 상태를 가짐)
        write(fd, "BODY....", BLK);
    close(fd);
    show("append식 WRITE x3:", p);
    unlink(p);
}

// ------------------------------------------------------------- 2) file handle + generation
struct inode { int in_use; unsigned gen; char name[16]; };
struct fh { unsigned vol, ino, gen; };
static struct inode itab[4];

static struct fh srv_create(const char *name) {
    for (unsigned i = 0; i < 4; i++)
        if (!itab[i].in_use) {
            itab[i].in_use = 1; itab[i].gen++;          // 재사용될 때마다 generation 증가
            snprintf(itab[i].name, sizeof(itab[i].name), "%s", name);
            return (struct fh){1, i, itab[i].gen};
        }
    exit(1);
}
static void srv_remove(struct fh h) { itab[h.ino].in_use = 0; }
static const char *srv_getattr(struct fh h) {         // 서버는 핸들만 보고 판단 (상태 없음)
    if (!itab[h.ino].in_use || itab[h.ino].gen != h.gen) return "ESTALE (stale file handle)";
    return itab[h.ino].name;
}

static void part2(void) {
    printf("\n[2] 파일 핸들 (vol, inode, gen) 과 inode 재사용\n");
    struct fh a = srv_create("foo.txt");
    printf("  client A: LOOKUP foo.txt  → fh=(vol=%u, ino=%u, gen=%u)\n", a.vol, a.ino, a.gen);
    srv_remove(a);
    struct fh b = srv_create("secret.key");
    printf("  (다른 클라이언트가 foo 삭제 후 secret.key 생성) → fh=(vol=%u, ino=%u, gen=%u)  같은 inode 재사용!\n",
           b.vol, b.ino, b.gen);
    printf("  client A: GETATTR(옛 fh)  → %s\n", srv_getattr(a));
    printf("  client B: GETATTR(새 fh)  → %s\n", srv_getattr(b));
}

// ------------------------------------------------------------- 3) server write buffering
static char disk[3][BLK + 1] = {"xxxxxxxx", "yyyyyyyy", "zzzzzzzz"};
static char membuf[3][BLK + 1];
static int dirty[3];

static void srv_write(int blk, const char *data, int sync_to_disk) {
    if (sync_to_disk) memcpy(disk[blk], data, BLK);
    else { memcpy(membuf[blk], data, BLK); dirty[blk] = 1; }
}
static void srv_crash(void) { memset(dirty, 0, sizeof(dirty)); }  // 메모리 내용 증발
static void srv_flush(void) {
    for (int i = 0; i < 3; i++) if (dirty[i]) { memcpy(disk[i], membuf[i], BLK); dirty[i] = 0; }
}

static void part3(int safe) {
    memcpy(disk[0], "xxxxxxxx", BLK); memcpy(disk[1], "yyyyyyyy", BLK); memcpy(disk[2], "zzzzzzzz", BLK);
    printf("\n[3%s] 서버가 %s 성공을 응답\n", safe ? "b" : "a", safe ? "디스크에 커밋한 뒤에야" : "메모리에만 두고");
    srv_write(0, "aaaaaaaa", 1);                 printf("  WRITE blk0 aaaa → OK\n");
    srv_write(1, "bbbbbbbb", safe);              printf("  WRITE blk1 bbbb → OK %s\n", safe ? "" : "(사실은 메모리에만 있음)");
    srv_crash();                                 printf("  *** 서버 크래시 & 재부팅 ***\n");
    srv_write(2, "cccccccc", 1);                 printf("  WRITE blk2 cccc → OK\n");
    srv_flush();
    printf("  디스크 최종: %s / %s / %s   %s\n", disk[0], disk[1], disk[2],
           strcmp(disk[1], "bbbbbbbb") ? "<-- oops: 클라이언트는 성공으로 알고 있음" : "(정상)");
}

int main(void) {
    mkdir(".work", 0755);
    part1();
    part2();
    part3(0);
    part3(1);
    return 0;
}
