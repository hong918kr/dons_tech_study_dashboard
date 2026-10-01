// C14_memory_bugs.c — 14.4절 "흔한 실수"를 하나씩 일부러 저지르고 도구로 잡아 보기
// 정상 빌드 : cc -Wall -Wextra -O0 -g code/C14_memory_bugs.c -o .work/bin/C14_bugs
// ASan 빌드 : cc -Wall -Wextra -O0 -g -fsanitize=address code/C14_memory_bugs.c -o .work/bin/C14_bugs_asan
// 실행      : .work/bin/C14_bugs_asan <mode>   (mode: sizeof overflow uaf double invalid leak uninit null)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void demo_sizeof(void) {
    int *x = malloc(10 * sizeof(int));
    int arr[10];
    printf("sizeof(x)   = %zu  (포인터 크기일 뿐, 40이 아니다)\n", sizeof(x));
    printf("sizeof(arr) = %zu  (배열은 컴파일러가 크기를 안다)\n", sizeof(arr));
    free(x);
}

// 버그 1: strlen(src) 만큼만 할당 → '\0' 1바이트가 넘친다 (heap-buffer-overflow)
static void bug_overflow(void) {
    const char *src = "hello";
    char *dst = malloc(strlen(src));          // too small! (+1 빠짐)
    strcpy(dst, src);
    printf("dst=%s\n", dst);
    free(dst);
}

// 버그 2: free 한 뒤에 사용 (dangling pointer → heap-use-after-free)
static void bug_uaf(void) {
    int *data = malloc(100 * sizeof(int));
    data[0] = 7;
    free(data);
    printf("data[0]=%d\n", data[0]);
}

// 버그 3: 같은 포인터를 두 번 free (double free)
static void bug_double(void) {
    int *p = malloc(sizeof(int));
    free(p);
    free(p);
}

// 버그 4: malloc이 준 포인터가 아닌 값을 free (invalid free)
static void bug_invalid(void) {
    int *data = malloc(100 * sizeof(int));
    free(data + 50);                          // 배열 한가운데
}

// 버그 5: 해제 깜빡 (memory leak)
static void bug_leak(void) {
    for (int i = 0; i < 3; i++) {
        char *p = malloc(64);
        p[0] = 'x';
        (void)p;                              // free 안 함
    }
    printf("leaked 3 x 64 bytes\n");
}

// 버그 6: 초기화하지 않은 heap 읽기 (uninitialized read) — ASan은 못 잡는다
static void bug_uninit(void) {
    int *p = malloc(4 * sizeof(int));
    printf("uninit p[0..3] = %d %d %d %d\n", p[0], p[1], p[2], p[3]);
    int *q = calloc(4, sizeof(int));
    printf("calloc q[0..3] = %d %d %d %d\n", q[0], q[1], q[2], q[3]);
    free(p); free(q);
}

// 버그 7: 할당을 깜빡 — NULL 역참조 (숙제 Q1의 null.c)
static void bug_null(void) {
    int *p = NULL;
    printf("about to deref NULL\n");
    fflush(stdout);
    *p = 1;
}

int main(int argc, char *argv[]) {
    const char *m = argc > 1 ? argv[1] : "sizeof";
    if (!strcmp(m, "sizeof"))   demo_sizeof();
    else if (!strcmp(m, "overflow")) bug_overflow();
    else if (!strcmp(m, "uaf"))      bug_uaf();
    else if (!strcmp(m, "double"))   bug_double();
    else if (!strcmp(m, "invalid"))  bug_invalid();
    else if (!strcmp(m, "leak"))     bug_leak();
    else if (!strcmp(m, "uninit"))   bug_uninit();
    else if (!strcmp(m, "null"))     bug_null();
    else { fprintf(stderr, "unknown mode %s\n", m); return 2; }
    printf("[%s] 끝까지 실행됨\n", m);
    return 0;
}
