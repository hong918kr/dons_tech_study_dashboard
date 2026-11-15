/* OS-B08 추가 연습 — 한 번에 검증
 *
 *   연습 1  macOS vmmap 한 줄 파서 (영역 이름에 공백이 있어도)
 *   연습 2  크래시 주소 되돌리기: 실행 주소 → 어느 이미지 + 링크 시점 주소 (atos 가 하는 일)
 *   연습 3  /proc/maps 통계: 종류별 바이트, 힙 페이지 수, 스레드 스택 크기
 *   연습 4  가장 큰 빈 구멍(gap) 찾기
 *   연습 5  Cortex-M 스택 덤프에서 "리턴 주소처럼 보이는 값" 고르기 (ASLR 없음 → 주소가 곧 심볼)
 *
 * 데이터는 전부 이 레슨에서 실제로 뽑은 출력 (vmmap, Lima VM 의 /proc/self/maps) 을 고정 문자열로 넣었다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
        else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ======================= 연습 1 — vmmap 한 줄 ======================= */
/* "Malloc Small (empty)       72dc004000-72dc400000   [ 4080K     0K     0K     0K] rw-/rwx SM=PRV          Default..."
 * → type "Malloc Small (empty)", start, end, prot "rw-", maxprot "rwx", detail(나머지, 없으면 ""). */
typedef struct {
    char type[32];
    uint64_t start, end;
    char prot[4], maxprot[4];
    char detail[96];
} vmmap_ent_t;

static bool is_hex_range_at(const char *p) {           /* "<hex>-<hex>" 로 시작하나 */
    const char *q = p;
    while ((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'f')) q++;
    if (q == p || *q != '-') return false;
    const char *r = q + 1;
    while ((*r >= '0' && *r <= '9') || (*r >= 'a' && *r <= 'f')) r++;
    return r > q + 1 && (*r == ' ' || *r == '\0');
}

static void copy_trim(char *dst, size_t cap, const char *b, const char *e) {
    while (b < e && *b == ' ') b++;
    while (e > b && (e[-1] == ' ' || e[-1] == '\n')) e--;
    size_t n = (size_t)(e - b);
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, b, n);
    dst[n] = '\0';
}

static int parse_vmmap_line(const char *line, vmmap_ent_t *v) {
    memset(v, 0, sizeof *v);
    const char *p = line;
    /* 영역 이름에는 공백이 들어갈 수 있다 → "hex-hex" 토큰이 나올 때까지 단어 단위로 건너뛴다 */
    while (*p) {
        if ((p == line || p[-1] == ' ') && is_hex_range_at(p)) break;
        p++;
    }
    if (!*p) return -1;
    copy_trim(v->type, sizeof v->type, line, p);
    char *e;
    v->start = strtoull(p, &e, 16);
    v->end = strtoull(e + 1, &e, 16);
    const char *rb = strchr(e, ']');
    if (!rb) return -1;
    p = rb + 1;
    while (*p == ' ') p++;
    if (strlen(p) < 7 || p[3] != '/') return -1;
    memcpy(v->prot, p, 3);
    memcpy(v->maxprot, p + 4, 3);
    p += 7;
    const char *sm = strstr(p, "SM=");
    if (!sm) return -1;
    p = sm;
    while (*p && *p != ' ') p++;                         /* SM=COW 토큰 끝 */
    copy_trim(v->detail, sizeof v->detail, p, p + strlen(p));
    return 0;
}

/* ======================= 연습 2 — 크래시 주소 되돌리기 ======================= */
typedef struct { const char *name; uint64_t load, size, link_base; } image_t;

/* pc 가 속한 이미지 인덱스 (없으면 -1), *link_addr = pc - (load - link_base) */
static int symbolize(const image_t *im, int n, uint64_t pc, uint64_t *link_addr) {
    for (int i = 0; i < n; i++) {
        if (pc >= im[i].load && pc - im[i].load < im[i].size) {
            *link_addr = pc - (im[i].load - im[i].link_base);
            return i;
        }
    }
    return -1;
}

/* ======================= 연습 3·4 — /proc/maps 통계와 구멍 ======================= */
typedef struct { uint64_t start, end; char perms[5]; char path[64]; } ment_t;

static int parse_min(const char *line, ment_t *m) {      /* 본문 Part A 의 짧은 판 (sscanf) */
    int used = 0;
    memset(m, 0, sizeof *m);
    if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %4s %*s %*s %*s %n", &m->start, &m->end, m->perms, &used) < 3)
        return -1;
    copy_trim(m->path, sizeof m->path, line + used, line + strlen(line));
    return m->start < m->end ? 0 : -1;
}

typedef struct { uint64_t exe, heap, stack, lib, anon, guard, vdso, total; } stats_t;

static stats_t maps_stats(const ment_t *m, int n, const char *exe) {
    stats_t s = { 0 };
    for (int i = 0; i < n; i++) {
        uint64_t sz = m[i].end - m[i].start;
        s.total += sz;
        if (!strcmp(m[i].path, "[heap]")) s.heap += sz;
        else if (!strcmp(m[i].path, "[stack]")) s.stack += sz;
        else if (m[i].path[0] == '[') s.vdso += sz;
        else if (!strcmp(m[i].perms, "---p")) s.guard += sz;
        else if (!strcmp(m[i].path, exe)) s.exe += sz;
        else if (m[i].path[0]) s.lib += sz;
        else s.anon += sz;
    }
    return s;
}

/* 이웃한 두 영역 사이 가장 큰 구멍. *gap_start 에 구멍 시작, 반환 = 크기. 0 이면 구멍 없음. */
static uint64_t largest_gap(const ment_t *m, int n, uint64_t *gap_start) {
    uint64_t best = 0;
    *gap_start = 0;
    for (int i = 1; i < n; i++) {
        if (m[i].start > m[i - 1].end && m[i].start - m[i - 1].end > best) {
            best = m[i].start - m[i - 1].end;
            *gap_start = m[i - 1].end;
        }
    }
    return best;
}

/* ======================= 연습 5 — Cortex-M 스택 덤프 ======================= */
/* STM32F4 예: FLASH 0x0800_0000 .. +1 MB 중 코드가 실제로 있는 곳 [text_lo, text_hi).
 * Thumb 코드의 리턴 주소(LR)는 bit0 = 1. ASLR 이 없으니 이 값에서 1 을 빼면 그대로 addr2line 에 넣을 수 있다. */
static bool looks_like_return_addr(uint32_t w, uint32_t text_lo, uint32_t text_hi) {
    return (w & 1u) && w >= text_lo && w < text_hi;
}

/* ======================= 테스트 데이터 ======================= */
static const char *k_vmmap[] = {   /* vmmap <pid> 실측 (demo_layout -s 8) 에서 고른 줄 */
    "__TEXT                      104e68000-104e6c000    [   16K    16K     0K     0K] r-x/r-x SM=COW          /tmp/OS-B08_work/demo_layout",
    "__DATA_CONST                104e6c000-104e70000    [   16K    16K     0K     0K] r--/rw- SM=COW          /tmp/OS-B08_work/demo_layout",
    "Malloc Small (empty)       72dc004000-72dc400000   [ 4080K     0K     0K     0K] rw-/rwx SM=PRV          DefaultMallocZone_0x105264000",
    "Stack Guard                 166f98000-16a79c000    [ 56.0M     0K     0K     0K] ---/rwx SM=NUL          stack guard for thread 0",
    "shared memory               104e78000-104e84000    [   48K    48K    48K     0K] r--/r-- SM=SHM  ",
};

static const char *k_maps[] = {    /* Lima VM, setarch -R ./demo_layout -m 실측 (solution.c 와 같은 샘플) */
    "aaaaaaaa0000-aaaaaaaa2000 r-xp 00000000 00:26 57270                      /tmp/OS-B08/demo_layout",
    "aaaaaaabf000-aaaaaaac0000 r--p 0000f000 00:26 57270                      /tmp/OS-B08/demo_layout",
    "aaaaaaac0000-aaaaaaac1000 rw-p 00010000 00:26 57270                      /tmp/OS-B08/demo_layout",
    "aaaaaaac1000-aaaaaac00000 rw-p 00000000 00:00 0                          [heap]",
    "fffff75c0000-fffff7dd0000 rw-p 00000000 00:00 0 ",
    "fffff7dd0000-fffff7de0000 rw-p 00000000 00:00 0 ",
    "fffff7de0000-fffff7f89000 r-xp 00000000 fd:01 5341                       /usr/lib/aarch64-linux-gnu/libc.so.6",
    "fffff7f89000-fffff7f9d000 ---p 001a9000 fd:01 5341                       /usr/lib/aarch64-linux-gnu/libc.so.6",
    "fffff7f9d000-fffff7fa0000 r--p 001ad000 fd:01 5341                       /usr/lib/aarch64-linux-gnu/libc.so.6",
    "fffff7fa0000-fffff7fa2000 rw-p 001b0000 fd:01 5341                       /usr/lib/aarch64-linux-gnu/libc.so.6",
    "fffff7fa2000-fffff7fae000 rw-p 00000000 00:00 0 ",
    "fffff7fbc000-fffff7fbe000 rw-p 00000000 00:00 0 ",
    "fffff7fbe000-fffff7fe8000 r-xp 00000000 fd:01 5338                       /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1",
    "fffff7ff4000-fffff7ff6000 rw-p 00000000 00:00 0 ",
    "fffff7ff6000-fffff7ffa000 r--p 00000000 00:00 0                          [vvar]",
    "fffff7ffa000-fffff7ffc000 r-xp 00000000 00:00 0                          [vdso]",
    "fffff7ffc000-fffff7ffe000 r--p 0002e000 fd:01 5338                       /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1",
    "fffff7ffe000-fffff7fff000 rw-p 00030000 fd:01 5338                       /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1",
    "fffff7fff000-fffff8000000 rw-p 00000000 00:00 0 ",
    "fffffffdf000-1000000000000 rw-p 00000000 00:00 0                         [stack]",
};
#define N_MAPS ((int)(sizeof k_maps / sizeof k_maps[0]))

int main(void) {
    /* ---------- 연습 1 ---------- */
    vmmap_ent_t v;
    CHECK(parse_vmmap_line(k_vmmap[0], &v) == 0 && !strcmp(v.type, "__TEXT") &&
          v.start == 0x104e68000u && v.end == 0x104e6c000u && !strcmp(v.prot, "r-x"));
    CHECK(parse_vmmap_line(k_vmmap[1], &v) == 0 && !strcmp(v.prot, "r--") && !strcmp(v.maxprot, "rw-"));
    CHECK(!strcmp(v.detail, "/tmp/OS-B08_work/demo_layout"));
    CHECK(parse_vmmap_line(k_vmmap[2], &v) == 0 && !strcmp(v.type, "Malloc Small (empty)") &&
          v.end - v.start == 4080u * 1024 && !strcmp(v.maxprot, "rwx"));
    CHECK(parse_vmmap_line(k_vmmap[3], &v) == 0 && !strcmp(v.type, "Stack Guard") &&
          !strcmp(v.prot, "---") && !strcmp(v.detail, "stack guard for thread 0"));
    CHECK(parse_vmmap_line(k_vmmap[4], &v) == 0 && !strcmp(v.type, "shared memory") && v.detail[0] == '\0');
    CHECK(parse_vmmap_line("no range here", &v) == -1);

    /* ---------- 연습 2 ---------- */
    const image_t im[] = {   /* 크래시 리포트의 Binary Images 표처럼: 같은 vmmap 실행에서 */
        { "demo_layout", 0x104e68000, 0x4000, 0x100000000 },       /* __TEXT vmaddr (otool -l) */
        { "libsystem_c.dylib", 0x187628000, 0x83000, 0x187628000 }, /* 공유 캐시 안: 여기선 load 그대로 */
    };
    uint64_t la = 0;
    CHECK(symbolize(im, 2, 0x104e685e8, &la) == 0 && la == 0x1000005e8u);   /* main: slide 0x4e68000 */
    CHECK(symbolize(im, 2, 0x18765575c, &la) == 1 && la - im[1].link_base == 0x2d75cu);
    CHECK(symbolize(im, 2, 0x104e6c000, &la) == -1);                        /* __TEXT 끝 = 밖 */
    CHECK(symbolize(im, 2, 0, &la) == -1);

    /* ---------- 연습 3 ---------- */
    ment_t m[N_MAPS];
    int ok = 0;
    for (int i = 0; i < N_MAPS; i++) ok += parse_min(k_maps[i], &m[i]) == 0;
    CHECK(ok == N_MAPS);
    stats_t s = maps_stats(m, N_MAPS, "/tmp/OS-B08/demo_layout");
    CHECK(s.exe == 0x4000u);                          /* 2 + 1 + 1 페이지 */
    CHECK(s.heap / 4096 == 319);                      /* brk 힙이 2 MB 경계까지 */
    CHECK(m[4].end - m[4].start == 8u * 1024 * 1024 + 64 * 1024);   /* 스레드 스택 8 MB + 가드 64 KB */
    CHECK(s.guard == 0x14000u);                       /* libc 의 ---p 한 덩어리 */
    CHECK(s.stack == 0x21000u);                       /* 132 KB: 아직 자라지 않은 메인 스택 */
    CHECK(s.exe + s.heap + s.stack + s.lib + s.anon + s.guard + s.vdso == s.total);

    /* ---------- 연습 4 ---------- */
    uint64_t gs = 0, gsz = largest_gap(m, N_MAPS, &gs);
    CHECK(gs == 0xaaaaaac00000u && gsz == 0xfffff75c0000u - 0xaaaaaac00000u);
    {
        ment_t two[2] = { { 0x1000, 0x2000, "r-xp", "" }, { 0x2000, 0x3000, "rw-p", "" } };
        CHECK(largest_gap(two, 2, &gs) == 0);
    }

    /* ---------- 연습 5 ---------- */
    const uint32_t text_lo = 0x08000000u, text_hi = 0x08012000u;      /* .text 72 KB 라고 하자 */
    const uint32_t dump[] = { 0x20001ff0u, 0x080004c9u, 0x00000007u, 0x08011ffdu,
                              0x080004c8u, 0x08012001u, 0xfffffff9u, 0x0800a1b3u };
    int hits = 0, last = -1;
    for (int i = 0; i < (int)(sizeof dump / sizeof dump[0]); i++)
        if (looks_like_return_addr(dump[i], text_lo, text_hi)) { hits++; last = i; }
    CHECK(hits == 3 && last == 7);           /* 0x080004c9, 0x08011ffd, 0x0800a1b3 */
    CHECK(!looks_like_return_addr(0xfffffff9u, text_lo, text_hi));   /* EXC_RETURN: 주소 아님 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
