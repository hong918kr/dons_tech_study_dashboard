/* OS-B08 demo_layout.c — 내 프로세스의 주소 공간을 한 줄씩 찍는다 (macOS / Linux 공용)
 *
 *   ./demo_layout            주소 표 출력
 *   ./demo_layout -s 30      출력 후 30 초 잠든다 → 다른 터미널에서  vmmap <pid>  (macOS)
 *                                                    또는 cat /proc/<pid>/maps (Linux)
 *   ./demo_layout -q         "이름 주소" 만 (스크립트용, demo_aslr.sh 가 쓴다)
 *   ./demo_layout -m         (Linux) 표 다음에 /proc/self/maps 를 그대로 덧붙인다
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_layout.c -o demo_layout
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

int g_data = 42;                      /* data  (0 아닌 초기값)         */
int g_bss;                            /* bss   (0 초기화)              */
const int g_rodata = 7;               /* rodata                         */

static int quiet;

static void row(const char *name, const void *p) {
    if (quiet) printf("%s %p\n", name, p);
    else       printf("  %-26s %18p\n", name, p);
}

static int sleep_s;
static int tpipe[2];                  /* 스레드 → main: 스레드 스택 주소 전달 */

static void *thread_fn(void *arg) {
    int local_in_thread = 1;
    uintptr_t a = (uintptr_t)&local_in_thread;
    (void)arg;
    if (write(tpipe[1], &a, sizeof a) != (ssize_t)sizeof a) return NULL;
    if (sleep_s > 0) sleep((unsigned)sleep_s);   /* vmmap 볼 때 스레드 스택이 살아 있게 */
    return NULL;
}

int main(int argc, char **argv) {
    int dump_maps = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-q")) quiet = 1;
        else if (!strcmp(argv[i], "-m")) dump_maps = 1;
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) sleep_s = atoi(argv[++i]);
    }
    int local = 0;
    void *small = malloc(32);
    void *large = malloc(1 << 20);               /* 1 MB — 큰 할당은 다른 곳에서 온다 */
    void *anon = mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    uintptr_t tstack = 0;
    pthread_t th;
    int have_thread = 0;
    if (pipe(tpipe) == 0 && pthread_create(&th, NULL, thread_fn, NULL) == 0) {
        have_thread = 1;
        if (read(tpipe[0], &tstack, sizeof tstack) != (ssize_t)sizeof tstack) tstack = 0;
    }
    Dl_info di;
    memset(&di, 0, sizeof di);
    dladdr((void *)&printf, &di);

    if (!quiet) printf("pid %d\n", (int)getpid());
    row("main (text)", (void *)&main);
    row("\"literal\" (rodata)", (const void *)"literal");
    row("g_rodata (const)", &g_rodata);
    row("g_data (data)", &g_data);
    row("g_bss (bss)", &g_bss);
    row("malloc(32)", small);
    row("malloc(1MB)", large);
    row("mmap anon 64KB", anon);
    row("&printf (libc)", (void *)&printf);
    row("libc image base", di.dli_fbase);
    row("main stack (local)", &local);
    row("thread stack (local)", (void *)tstack);
#if defined(__APPLE__)
    row("main image header", (const void *)_dyld_get_image_header(0));
    row("main image slide", (void *)(uintptr_t)_dyld_get_image_vmaddr_slide(0));
#endif
    fflush(stdout);
    if (dump_maps) {
        FILE *f = fopen("/proc/self/maps", "r");          /* macOS 에는 없다 → vmmap */
        if (!f) printf("(no /proc/self/maps here — on macOS use: vmmap %d)\n", (int)getpid());
        else { int c; while ((c = fgetc(f)) != EOF) putchar(c); fclose(f); }
        fflush(stdout);
    }
    if (sleep_s > 0) sleep((unsigned)sleep_s);
    if (have_thread) { pthread_join(th, NULL); close(tpipe[0]); close(tpipe[1]); }
    if (anon != MAP_FAILED) munmap(anon, 1 << 16);
    free(large);
    free(small);
    return 0;
}
