/* OS-B08 주소 공간 탐색: 텍스트·데이터·힙·스택과 ASLR
 *
 *   Part A  /proc/<pid>/maps 한 줄 파서 + 주소 → 영역 찾기(이진 탐색) + 영역 종류 분류.
 *           테스트 데이터 = Lima VM(aarch64 Linux 7.0, setarch -R)에서 demo_layout -m 으로 뽑은
 *           실제 maps 를 그대로 붙인 고정 문자열. 그래서 매번 같은 결과.
 *   Part B  페이지 산수와 ASLR 분석: page_down / page_up / pages_spanned, varying_bits, moves_together.
 *           테스트 데이터 = 이 맥에서 demo_layout 을 3 번 실행해 기록한 실제 주소 (고정 배열).
 *   Part C  macOS 라이브 점검 (vmmap 미니): mach_vm_region_recurse 로 "지금 이 프로세스" 의 영역과 권한을 묻는다.
 *           주소 숫자는 실행마다 다르지만 권한·포함 관계·[0, 4 GB) 가 비어 있다는 사실은 매번 같다 → 그것만 CHECK.
 *
 * 실제 주소 표 / 여러 번 실행 비교 / vmmap 출력은 demo_layout.c, demo_aslr.sh 에 있다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      (macOS 전용: Part C 가 Mach API)
 */
#define _DARWIN_C_SOURCE
#include <inttypes.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
        else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ======================= Part A — /proc/<pid>/maps 파서 ======================= */
/* 한 줄 형식 (proc(5)):  start-end perms offset dev inode [path]
 *   aaaaaaaa0000-aaaaaaaa2000 r-xp 00000000 00:26 57270      /tmp/OS-B08/demo_layout
 *   fffff75c0000-fffff7dd0000 rw-p 00000000 00:00 0                   (path 없음 = 익명 매핑) */
typedef struct {
    uint64_t start, end;        /* [start, end) — end 는 포함 안 함 */
    char perms[5];              /* "r-xp" 같은 4 글자 + NUL */
    uint64_t offset;            /* 파일 오프셋 (16 진) */
    char path[64];              /* 없으면 "" . 길면 63 글자에서 자름 */
} maps_ent_t;

typedef enum { K_EXE_TEXT, K_EXE_RO, K_EXE_DATA, K_HEAP, K_STACK, K_LIB, K_ANON, K_GUARD, K_VDSO } kind_t;

/* 한 줄을 파싱한다. 성공 0, 형식이 틀리면 -1.
 * 틀린 형식: 16 진수 아님, '-' 없음, start >= end, perms 가 [r-][w-][x-][ps] 가 아님, 필드 부족.
 * path 앞뒤 공백·줄바꿈은 지운다. */
static int parse_maps_line(const char *line, maps_ent_t *e) {
    memset(e, 0, sizeof *e);
    (void)line;
    return -1; /* TODO */
}

/* v[0..n) 은 start 오름차순·겹침 없음 (커널이 그렇게 출력한다).
 * addr 을 포함하는 항목의 인덱스, 어느 영역에도 없으면(구멍) -1. O(log n) 이진 탐색. */
static int maps_find(const maps_ent_t *v, int n, uint64_t addr) {
    (void)v; (void)n; (void)addr;
    return -1; /* TODO */
}

/* 영역 종류. 규칙 (위에서부터 먼저 맞는 것):
 *   "[heap]" → K_HEAP, "[stack]" → K_STACK, "[vdso]" 또는 "[vvar]" → K_VDSO
 *   perms "---p" (접근 불가) → K_GUARD   (path 가 있어도: 라이브러리 세그먼트 사이 구멍 메우기)
 *   path == exe : x 있음 → K_EXE_TEXT, w 있음 → K_EXE_DATA, 그 밖(r--) → K_EXE_RO
 *   다른 path   → K_LIB,   path 없음 → K_ANON */
static kind_t maps_kind(const maps_ent_t *e, const char *exe) {
    (void)e; (void)exe;
    return K_ANON; /* TODO */
}

/* ======================= Part B — 페이지 산수와 ASLR 분석 ======================= */
/* pg 는 2 의 거듭제곱 (4096, 16384, 65536 ...). 나눗셈 대신 마스크로. */
static uint64_t page_down(uint64_t a, uint64_t pg) {
    (void)pg;
    return a; /* TODO */
}
/* a 이상인 가장 작은 pg 배수. (a + pg - 1 이 넘치지 않는 범위에서만 쓴다) */
static uint64_t page_up(uint64_t a, uint64_t pg) {
    (void)pg;
    return a; /* TODO */
}
/* [a, a+len) 바이트가 걸치는 페이지 수. len == 0 이면 0. */
static uint64_t pages_spanned(uint64_t a, uint64_t len, uint64_t pg) {
    (void)a; (void)len; (void)pg;
    return 0; /* TODO */
}
/* n 번 실행에서 같은 객체의 주소 v[0..n). 한 번이라도 바뀐 비트를 1 로: OR_i (v[i] ^ v[0]). */
static uint64_t varying_bits(const uint64_t *v, int n) {
    (void)v; (void)n;
    return 0; /* TODO */
}
/* 두 객체가 "같은 slide 로 함께 움직이나": 모든 i 에서 a[i] - b[i] 가 같으면 true. n <= 1 이면 true. */
static bool moves_together(const uint64_t *a, const uint64_t *b, int n) {
    (void)a; (void)b; (void)n;
    return false; /* TODO */
}

/* ======================= Part C — macOS 라이브 점검 (vmmap 미니) ======================= */
typedef struct { uint64_t start, end; int prot; } vmr_t;   /* prot: VM_PROT_READ 1 | WRITE 2 | EXECUTE 4 */

/* addr 이 들어 있는 VM 영역을 묻는다.
 *   0  = 매핑됨: out = 그 영역 (start <= addr < end)
 *   1  = 구멍: out = addr 보다 위에 있는 첫 영역 (위에 아무것도 없으면 start = end = 0, prot = 0)
 * mach_vm_region_recurse(task, &address, &size, &depth, info, &cnt) 는 address "이상" 의 첫 영역을
 * 돌려준다 (그 위에 없으면 KERN_INVALID_ADDRESS). 그 영역이 서브맵(info.is_submap — dyld 공유 캐시가
 * 그렇다)이면 depth 를 1 올려 같은 addr 로 다시 묻는다. 매번 address/cnt 를 다시 채워야 한다. */
static int vm_region_of(uintptr_t addr, vmr_t *out) {
    (void)addr;
    out->start = out->end = 0;
    out->prot = 0;
    return 1; /* TODO */
}

/* prot → "r-x" 같은 3 글자 (vmmap 의 PRT 열과 같은 표기). */
static void prot_str(int prot, char out[4]) {
    (void)prot;
    strcpy(out, "???"); /* TODO */
}

/* ======================= 테스트 데이터 ======================= */
/* Lima VM elinux (Ubuntu 26.04 aarch64, Linux 7.0, glibc 2.43), setarch aarch64 -R ./demo_layout -m 실측.
 * 그 실행에서 찍힌 주소: main 0xaaaaaaaa0d40, g_rodata 0xaaaaaaaa14d8, g_data 0xaaaaaaac0010,
 * malloc(32) 0xaaaaaaac1010, malloc(1MB) 0xaaaaaaac1040, mmap anon 0xfffff7dd0000,
 * &printf 0xfffff7e2f8a0, main stack 0xfffffffff8c4, thread stack 0xfffff7dce87c */
static const char *k_maps_sample[] = {
    "aaaaaaaa0000-aaaaaaaa2000 r-xp 00000000 00:26 57270                      /tmp/OS-B08/demo_layout\n",
    "aaaaaaabf000-aaaaaaac0000 r--p 0000f000 00:26 57270                      /tmp/OS-B08/demo_layout\n",
    "aaaaaaac0000-aaaaaaac1000 rw-p 00010000 00:26 57270                      /tmp/OS-B08/demo_layout\n",
    "aaaaaaac1000-aaaaaac00000 rw-p 00000000 00:00 0                          [heap]\n",
    "fffff75c0000-fffff7dd0000 rw-p 00000000 00:00 0 \n",
    "fffff7dd0000-fffff7de0000 rw-p 00000000 00:00 0 \n",
    "fffff7de0000-fffff7f89000 r-xp 00000000 fd:01 5341                       /usr/lib/aarch64-linux-gnu/libc.so.6\n",
    "fffff7f89000-fffff7f9d000 ---p 001a9000 fd:01 5341                       /usr/lib/aarch64-linux-gnu/libc.so.6\n",
    "fffff7f9d000-fffff7fa0000 r--p 001ad000 fd:01 5341                       /usr/lib/aarch64-linux-gnu/libc.so.6\n",
    "fffff7fa0000-fffff7fa2000 rw-p 001b0000 fd:01 5341                       /usr/lib/aarch64-linux-gnu/libc.so.6\n",
    "fffff7fa2000-fffff7fae000 rw-p 00000000 00:00 0 \n",
    "fffff7fbc000-fffff7fbe000 rw-p 00000000 00:00 0 \n",
    "fffff7fbe000-fffff7fe8000 r-xp 00000000 fd:01 5338                       /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1\n",
    "fffff7ff4000-fffff7ff6000 rw-p 00000000 00:00 0 \n",
    "fffff7ff6000-fffff7ffa000 r--p 00000000 00:00 0                          [vvar]\n",
    "fffff7ffa000-fffff7ffc000 r-xp 00000000 00:00 0                          [vdso]\n",
    "fffff7ffc000-fffff7ffe000 r--p 0002e000 fd:01 5338                       /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1\n",
    "fffff7ffe000-fffff7fff000 rw-p 00030000 fd:01 5338                       /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1\n",
    "fffff7fff000-fffff8000000 rw-p 00000000 00:00 0 \n",
    "fffffffdf000-1000000000000 rw-p 00000000 00:00 0                         [stack]\n",
};
#define N_SAMPLE ((int)(sizeof k_maps_sample / sizeof k_maps_sample[0]))
static const char *k_exe = "/tmp/OS-B08/demo_layout";

/* 이 맥(Apple M6, macOS 27.2)에서 같은 demo_layout 바이너리를 3 번 실행한 실제 주소 */
enum { O_MAIN, O_DATA, O_BSS, O_SMALL, O_LARGE, O_ANON, O_PRINTF, O_STACK, O_TSTACK, O_HDR, N_OBJ };
static const uint64_t k_runs[N_OBJ][3] = {
    [O_MAIN]   = { 0x102fa05e8, 0x1001ec5e8, 0x10483c5e8 },
    [O_DATA]   = { 0x102fa8000, 0x1001f4000, 0x104844000 },
    [O_BSS]    = { 0x102fa8008, 0x1001f4008, 0x104844008 },
    [O_SMALL]  = { 0x103749150, 0x100b89150, 0x104db9150 },
    [O_LARGE]  = { 0x795a800000, 0x7be4800000, 0x7556800000 },
    [O_ANON]   = { 0x102fcc000, 0x100218000, 0x104a4c000 },
    [O_PRINTF] = { 0x18765575c, 0x18765575c, 0x18765575c },
    [O_STACK]  = { 0x16ce5eb4c, 0x16fc12b4c, 0x16b5c2b4c },
    [O_TSTACK] = { 0x16cee6fcc, 0x16fc9afcc, 0x16b64afcc },
    [O_HDR]    = { 0x102fa0000, 0x1001ec000, 0x10483c000 },
};

/* Part C 용 객체들 */
int g_data_c = 42;
int g_bss_c;
const int g_rodata_c = 7;
static void *thread_probe(void *arg) {
    int local = 0;
    vmr_t *r = arg;
    if (vm_region_of((uintptr_t)&local, r) != 0) r->prot = -1;
    return NULL;
}

int main(void) {
    /* ---------- Part A ---------- */
    maps_ent_t m[N_SAMPLE];
    int parsed = 0;
    for (int i = 0; i < N_SAMPLE; i++) parsed += parse_maps_line(k_maps_sample[i], &m[i]) == 0;
    CHECK(parsed == N_SAMPLE);
    CHECK(m[0].start == 0xaaaaaaaa0000u && m[0].end == 0xaaaaaaaa2000u);
    CHECK(strcmp(m[0].perms, "r-xp") == 0 && strcmp(m[0].path, k_exe) == 0);
    CHECK(m[8].offset == 0x1ad000u && strcmp(m[8].perms, "r--p") == 0);
    CHECK(m[4].path[0] == '\0');                                   /* 익명 매핑: path 없음 */
    CHECK(m[N_SAMPLE - 1].end == 0x1000000000000u);                /* 13 자리 16 진수 = 2^48 */
    CHECK(strcmp(m[N_SAMPLE - 1].path, "[stack]") == 0);
    {
        maps_ent_t e;
        CHECK(parse_maps_line("zz-10 r-xp 0 00:00 0", &e) == -1);           /* 16 진수 아님 */
        CHECK(parse_maps_line("2000-1000 r-xp 0 00:00 0", &e) == -1);       /* start >= end */
        CHECK(parse_maps_line("1000-2000 rwzp 0 00:00 0", &e) == -1);       /* perms 이상 */
        CHECK(parse_maps_line("1000-2000 r-xp 0", &e) == -1);               /* 필드 부족 */
        CHECK(parse_maps_line("1000-2000 r-xp 0 00:00 0", &e) == 0 && e.path[0] == '\0');
    }
    /* 영역 찾기 + 종류: 위 실행에서 찍힌 실제 주소들 */
    struct { uint64_t addr; int idx; kind_t kind; } probe[] = {
        { 0xaaaaaaaa0d40, 0, K_EXE_TEXT },    /* main */
        { 0xaaaaaaaa14d8, 0, K_EXE_TEXT },    /* const 전역: .rodata 가 r-x 세그먼트에 같이 실렸다 */
        { 0xaaaaaaac0010, 2, K_EXE_DATA },    /* g_data */
        { 0xaaaaaaac1010, 3, K_HEAP },        /* malloc(32) */
        { 0xaaaaaaac1040, 3, K_HEAP },        /* malloc(1MB) — 이 실행에선 brk 힙에서 나왔다 */
        { 0xfffff7dd0000, 5, K_ANON },        /* mmap anon 64KB */
        { 0xfffff7e2f8a0, 6, K_LIB },         /* &printf */
        { 0xfffff7dce87c, 4, K_ANON },        /* 스레드 스택은 [stack] 이 아니라 익명 mmap */
        { 0xfffffffff8c4, N_SAMPLE - 1, K_STACK },
        { 0xfffff7ffa100, 15, K_VDSO },
        { 0xfffff7f90000, 7, K_GUARD },       /* libc 세그먼트 사이 ---p 구멍 메우기 */
    };
    bool find_ok = true, kind_ok = true;
    for (size_t i = 0; i < sizeof probe / sizeof probe[0]; i++) {
        int idx = maps_find(m, N_SAMPLE, probe[i].addr);
        if (idx != probe[i].idx) { find_ok = false; printf("  find %#" PRIx64 " -> %d\n", probe[i].addr, idx); }
        else if (maps_kind(&m[idx], k_exe) != probe[i].kind) kind_ok = false;
    }
    CHECK(find_ok);
    CHECK(kind_ok);
    CHECK(maps_find(m, N_SAMPLE, 0) == -1);                        /* NULL 근처는 비어 있다 */
    CHECK(maps_find(m, N_SAMPLE, 0xaaaaaaaa2000) == -1);           /* end 는 포함 안 함 → 구멍 */
    CHECK(maps_find(m, N_SAMPLE, 0xaaaaaaaa1fff) == 0);            /* 마지막 바이트 */
    CHECK(maps_find(m, N_SAMPLE, 0xfffffffdf000) == N_SAMPLE - 1); /* 첫 바이트 */
    CHECK(maps_kind(&m[1], k_exe) == K_EXE_RO);                    /* RELRO (.got 등) */
    CHECK(maps_kind(&m[14], k_exe) == K_VDSO);                     /* [vvar] */
    /* 항상 성립하는 순서: 실행 파일 < 힙 < 라이브러리 < 스택 */
    CHECK(m[2].end <= m[3].start && m[3].end <= m[6].start && m[6].end <= m[N_SAMPLE - 1].start);

    /* ---------- Part B ---------- */
    CHECK(page_down(0x16ce5eb4c, 16384) == 0x16ce5c000);
    CHECK(page_down(0x4000, 16384) == 0x4000);
    CHECK(page_up(0, 4096) == 0 && page_up(1, 4096) == 4096 && page_up(4096, 4096) == 4096);
    CHECK(page_up(0x102fa05e8, 16384) == 0x102fa4000);
    CHECK(pages_spanned(0x1000, 1, 4096) == 1);
    CHECK(pages_spanned(0x1fff, 2, 4096) == 2);                    /* 2 바이트가 페이지 경계를 넘음 */
    CHECK(pages_spanned(0x1000, 0, 4096) == 0);
    CHECK(pages_spanned(0x3fff, 0x4001, 16384) == 2);
    CHECK(pages_spanned(0, 1 << 20, 4096) == 256 && pages_spanned(0, 1 << 20, 16384) == 64);
    /* ASLR: 실제 3 회 실행 */
    CHECK(varying_bits(k_runs[O_PRINTF], 3) == 0);                 /* 공유 캐시: 부팅 동안 고정 */
    uint64_t vb_main = varying_bits(k_runs[O_MAIN], 3);
    CHECK(vb_main != 0 && (vb_main & 0x3fff) == 0);               /* 바뀌지만 16 KB 미만 비트는 그대로 */
    CHECK((varying_bits(k_runs[O_STACK], 3) & 0x3fff) == 0);       /* macOS 스택도 페이지 단위로만 */
    CHECK(moves_together(k_runs[O_MAIN], k_runs[O_DATA], 3));      /* 한 이미지 = 한 slide */
    CHECK(moves_together(k_runs[O_MAIN], k_runs[O_BSS], 3));
    CHECK(moves_together(k_runs[O_MAIN], k_runs[O_HDR], 3));
    CHECK(!moves_together(k_runs[O_MAIN], k_runs[O_SMALL], 3));    /* 힙은 따로 무작위 */
    CHECK(!moves_together(k_runs[O_MAIN], k_runs[O_STACK], 3));
    CHECK(!moves_together(k_runs[O_MAIN], k_runs[O_ANON], 3));
    CHECK(moves_together(k_runs[O_PRINTF], k_runs[O_PRINTF], 3));
    {
        uint64_t neg_main[3];                                      /* 0 - x : 2^64 모듈로, 정의된 동작 */
        for (int i = 0; i < 3; i++) neg_main[i] = 0 - k_runs[O_MAIN][i];
        CHECK(moves_together(k_runs[O_STACK], neg_main, 3));       /* macOS: stack + main = 일정 ("거울") */
        CHECK(moves_together(k_runs[O_STACK], k_runs[O_TSTACK], 3)); /* 스레드 스택은 메인 스택과 함께 */
    }
    {
        /* Linux VM 2 회 실행의 main 스택 지역 변수: 페이지 안 오프셋까지 바뀐다 (sub-page 무작위화) */
        const uint64_t lst[2] = { 0xffffeb66a2e4, 0xffffe05f9f94 };
        CHECK((varying_bits(lst, 2) & 0xfff) != 0);
    }

    /* ---------- Part C ---------- */
    {
        char s[4];
        prot_str(VM_PROT_READ | VM_PROT_EXECUTE, s);  CHECK(strcmp(s, "r-x") == 0);
        prot_str(VM_PROT_READ | VM_PROT_WRITE, s);    CHECK(strcmp(s, "rw-") == 0);
        prot_str(VM_PROT_NONE, s);                    CHECK(strcmp(s, "---") == 0);
        prot_str(VM_PROT_ALL, s);                     CHECK(strcmp(s, "rwx") == 0);
    }
    vmr_t r, r2;
    int local = 1;
    void *heap = malloc(32);
    CHECK(vm_region_of((uintptr_t)&main, &r) == 0 && r.prot == (VM_PROT_READ | VM_PROT_EXECUTE));
    CHECK(vm_region_of((uintptr_t)"literal", &r) == 0 && !(r.prot & VM_PROT_WRITE));
    CHECK(vm_region_of((uintptr_t)&g_rodata_c, &r) == 0 && !(r.prot & VM_PROT_WRITE));
    CHECK(vm_region_of((uintptr_t)&g_data_c, &r) == 0 && (r.prot & VM_PROT_WRITE) && !(r.prot & VM_PROT_EXECUTE));
    CHECK(vm_region_of((uintptr_t)&g_bss_c, &r) == 0 && (r.prot & VM_PROT_WRITE));
    CHECK(vm_region_of((uintptr_t)&local, &r) == 0 && (r.prot & VM_PROT_WRITE) && !(r.prot & VM_PROT_EXECUTE));
    CHECK(r.start <= (uintptr_t)&local && (uintptr_t)&local < r.end);
    CHECK(heap && vm_region_of((uintptr_t)heap, &r) == 0 && (r.prot & VM_PROT_WRITE));
    /* __PAGEZERO: [0, 4 GB) 에는 아무것도 없다 → NULL·32 비트로 잘린 포인터는 반드시 폴트 */
    CHECK(vm_region_of(0, &r) == 1 && r.start >= 0x100000000u);
    CHECK(vm_region_of(0x1000, &r) == 1 && vm_region_of(0xffffffffu, &r2) == 1);
    /* 다른 이미지: printf 는 dyld 공유 캐시 안 → main 과 다른 영역, 역시 r-x */
    CHECK(vm_region_of((uintptr_t)&printf, &r) == 0 && r.prot == (VM_PROT_READ | VM_PROT_EXECUTE));
    CHECK(vm_region_of((uintptr_t)&main, &r2) == 0 && (r.start != r2.start));
    /* 스레드 스택: 메인 스택과 다른 영역, rw- */
    {
        vmr_t tr = { 0, 0, 0 }, mr;
        pthread_t th;
        bool ok = pthread_create(&th, NULL, thread_probe, &tr) == 0;
        if (ok) pthread_join(th, NULL);
        CHECK(ok && tr.prot == (VM_PROT_READ | VM_PROT_WRITE));
        CHECK(vm_region_of((uintptr_t)&local, &mr) == 0 && tr.start != mr.start);
    }
    /* mprotect 는 영역을 쪼갠다: 3 페이지 rw- 의 가운데만 r-- 로 */
    {
        size_t pg = (size_t)getpagesize();
        uint8_t *b = mmap(NULL, 3 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        bool mapped = b != MAP_FAILED;
        CHECK(mapped);
        if (mapped) {
            CHECK(mprotect(b + pg, pg, PROT_READ) == 0);
            CHECK(vm_region_of((uintptr_t)(b + pg + 8), &r) == 0 &&
                  r.start == (uintptr_t)(b + pg) && r.end == (uintptr_t)(b + 2 * pg) && r.prot == VM_PROT_READ);
            CHECK(vm_region_of((uintptr_t)b, &r) == 0 && r.end == (uintptr_t)(b + pg) &&
                  r.prot == (VM_PROT_READ | VM_PROT_WRITE));
            CHECK(vm_region_of((uintptr_t)(b + 2 * pg), &r) == 0 && r.start == (uintptr_t)(b + 2 * pg));
            munmap(b, 3 * pg);
            CHECK(vm_region_of((uintptr_t)(b + pg), &r) == 1);     /* munmap 뒤엔 구멍 */
        }
    }
    free(heap);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
