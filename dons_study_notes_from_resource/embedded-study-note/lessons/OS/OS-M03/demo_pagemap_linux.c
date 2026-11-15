/* ENV: linux */
/* OS-M03 demo_pagemap_linux.c — 자식이 "읽기만" 해도 폴트가 나는 이유를 Linux 에서 확인한다.
 *
 *   질문: fork 직후 자식의 페이지 테이블에 부모의 PTE 가 복사되어 있나? 있다면 읽기 폴트는 왜 나나?
 *   방법: 자식이 아무것도 건드리기 전에 /proc/self/pagemap 의 present 비트(bit 63)를 센다.
 *         그다음 페이지마다 1바이트 읽기 → ru_minflt 증가량, 한 번 더 읽기 → 증가량.
 *         부모도 fork 뒤에 읽기만 해 본다. 마지막으로 MAP_SHARED 영역(파일 같은 shmem) 읽기 → fault-around.
 *   (pagemap 의 PFN 은 권한 없으면 0 으로 가려지지만 present 비트는 보인다 — Linux pagemap 문서)
 *
 * build (Lima VM): gcc -std=c11 -Wall -Wextra -Werror -O2 demo_pagemap_linux.c -o pm && ./pm
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define N 256

static long pg;
static long minflt(void) { struct rusage r; getrusage(RUSAGE_SELF, &r); return r.ru_minflt; }

static int present_pages(const char *b) {
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) return -1;
    int c = 0;
    for (size_t i = 0; i < N; i++) {
        uint64_t e = 0;
        if (pread(fd, &e, 8, (off_t)(((uintptr_t)b / (uintptr_t)pg + i) * 8)) == 8 && (e >> 63)) c++;
    }
    close(fd);
    return c;
}
static long read_all(const char *b) {
    volatile const char *p = b;
    long f0 = minflt();
    char s = 0;
    for (size_t i = 0; i < N; i++) s = (char)(s + p[i * (size_t)pg]);
    (void)s;
    return minflt() - f0;
}

static void run(const char *name, int flags) {
    char *b = mmap(NULL, N * (size_t)pg, PROT_READ | PROT_WRITE, flags | MAP_ANONYMOUS, -1, 0);
    memset(b, 1, N * (size_t)pg);
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) {
        int pres = present_pages(b);
        long r1 = read_all(b), r2 = read_all(b);
        printf("[%s] child : present before any touch %d/%d, read pass 1 +%ld, pass 2 +%ld\n", name, pres, N, r1, r2);
        fflush(stdout);
        _exit(0);
    }
    waitpid(c, NULL, 0);
    printf("[%s] parent: read after fork +%ld\n", name, read_all(b));
    munmap(b, N * (size_t)pg);
}

int main(void) {
    pg = sysconf(_SC_PAGESIZE);
    printf("page size %ld\n", pg);
    run("PRIVATE", MAP_PRIVATE);
    run("SHARED ", MAP_SHARED);
    return 0;
}
