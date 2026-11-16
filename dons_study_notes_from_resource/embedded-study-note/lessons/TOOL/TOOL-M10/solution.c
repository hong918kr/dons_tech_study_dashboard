/* TOOL-M10 Valgrind 대안: leaks·MallocStackLogging·ASan
 *
 * evlog = 게이트웨이(임베디드 Linux) / 호스트 단위 테스트용 "센서 이벤트 로그" 모듈.
 *   레코드 = (장치, 값, 텍스트, 재시도 횟수). 장치 이름은 전역 캐시에 한 번만 저장(interning).
 *   펌웨어 쪽에서 오는 프레임을 받아 쌓고(add), 오래된 것을 지우고(purge), 메모를 고치고(annotate),
 *   호스트로 "name=value" 줄을 내보낸다(export).
 *
 * 버그 5개가 TODO(...) 로 표시돼 있다. 케이스 하나 = 버그 하나 = 별도 프로세스:
 *   ./ev overflow | uaf | leak | cache | uninit | all
 * 각 케이스는 결과 줄 하나를 찍는다. 도구별 실행 방법은 check.sh / demo_tools.sh 참고.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o ev && ./ev all
 * ASan : cc -std=c11 -g -O1 -fno-omit-frame-pointer -fsanitize=address starter.c -o ev_asan
 * leaks: cc -std=c11 -g -O0 starter.c -o ev_g && MallocStackLogging=1 leaks --atExit -- ./ev_g leak
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct dev {                 /* 장치 이름 캐시 항목 — evlog 수명보다 오래 산다 */
    struct dev *next;
    char *name;
    unsigned hits;
};
struct rec {                 /* 이벤트 레코드 — 단일 연결 리스트, 꼬리 포인터로 O(1) 추가 */
    struct rec *next;
    struct dev *dev;
    char *text;
    int value;
    unsigned retries;
};

static struct dev *g_devs;               /* 캐시 머리 (전역 → 프로그램 끝까지 "도달 가능") */
static struct rec *g_head, *g_tail;
static unsigned g_nrec;

static char *str_dup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static struct dev *dev_intern(const char *name) {
    for (struct dev *d = g_devs; d; d = d->next)
        if (strcmp(d->name, name) == 0) { d->hits++; return d; }
    struct dev *d = malloc(sizeof *d);
    if (!d) return NULL;
    d->name = str_dup(name);
    d->hits = 1;
    d->next = g_devs;
    g_devs = d;
    return d;
}

static unsigned dev_count(void) {
    unsigned n = 0;
    for (struct dev *d = g_devs; d; d = d->next) n++;
    return n;
}

static void dev_cache_clear(void) {
    for (struct dev *d = g_devs, *next; d; d = next) { next = d->next; free(d->name); free(d); }
    g_devs = NULL;   /* cache: 전역에서 닿아도 "다 쓴" 메모리는 직접 돌려준다 */
}

static struct rec *rec_new(struct dev *d, int value) {
    struct rec *r = malloc(sizeof *r);
    if (!r) return NULL;
    r->next = NULL;
    r->dev = d;
    r->text = NULL;
    r->value = value;
    r->retries = 0;            /* uninit: malloc 은 0 을 약속하지 않는다 */
    return r;
}

static void rec_free(struct rec *r) {
    free(r->text);
    free(r);
}

static void rec_set_text(struct rec *r, const char *t) {
    free(r->text);             /* leak: 덮어쓰기 전에 예전 것을 돌려준다 (NULL 이면 무해) */
    r->text = str_dup(t);
}

static int evlog_add(const char *name, int value, const char *text) {
    struct dev *d = dev_intern(name);
    struct rec *r = d ? rec_new(d, value) : NULL;
    if (!r) return -1;
    rec_set_text(r, text);
    if (g_tail) g_tail->next = r; else g_head = r;
    g_tail = r;
    g_nrec++;
    return 0;
}

static struct rec *evlog_find_last(const char *name) {
    struct rec *hit = NULL;
    for (struct rec *r = g_head; r; r = r->next)
        if (strcmp(r->dev->name, name) == 0) hit = r;
    return hit;
}

static void evlog_annotate(const char *name, const char *text) {
    struct rec *r = evlog_find_last(name);
    if (r) rec_set_text(r, text);
}

static void evlog_retry(const char *name) {
    struct rec *r = evlog_find_last(name);
    if (r) r->retries++;
}

/* value < min_value 인 레코드를 지운다. 지운 개수를 돌려준다. */
static unsigned evlog_purge(int min_value) {
    unsigned n = 0;
    struct rec *prev = NULL, *r = g_head;
    while (r) {
        struct rec *next = r->next;
        if (r->value < min_value) {
            if (prev) prev->next = next; else g_head = next;
            if (g_tail == r) g_tail = prev;   /* uaf: 지운 꼬리를 가리키는 별칭 정리 */
            rec_free(r);
            n++;
        } else {
            prev = r;
        }
        r = next;
    }
    g_nrec -= n;
    return n;
}

/* 호스트로 보낼 "name=value" 줄 (새로 malloc, 호출자가 free) */
static char *rec_to_line(const struct rec *r) {
    char num[16];
    int k = snprintf(num, sizeof num, "%d", r->value);
    size_t a = strlen(r->dev->name);
    size_t len = a + 1 + (size_t)k;          /* "name" + '=' + "value" */
    char *s = malloc(len + 1);               /* overflow: '\0' 자리까지 */
    if (!s) return NULL;
    memcpy(s, r->dev->name, a);
    s[a] = '=';
    memcpy(s + a + 1, num, (size_t)k);
    s[len] = '\0';
    return s;
}

static void evlog_shutdown(void) {
    struct rec *r = g_head;
    while (r) {
        struct rec *next = r->next;
        rec_free(r);
        r = next;
    }
    g_head = g_tail = NULL;
    g_nrec = 0;
    dev_cache_clear();
}

/* ======================= 드라이버 (수정 금지) ======================= */
static void case_overflow(void) {
    /* 줄 길이 11, 16, 13 바이트 (+ '\0') — 16 은 Guard Malloc 정렬 단위와 딱 맞는다 */
    evlog_add("gw-temp", 215, "boot");
    evlog_add("node-07-hum", 4551, "ok");
    evlog_add("node-12", 90210, "ok");
    printf("overflow: lines=");
    for (struct rec *r = g_head; r; r = r->next) {
        char *s = rec_to_line(r);
        printf("%s%s", s ? s : "(null)", r->next ? "," : "\n");
        free(s);
    }
    evlog_shutdown();
}

static void case_uaf(void) {
    evlog_add("gw-temp", 5, "a");
    evlog_add("gw-temp", 40, "b");
    evlog_add("node-07", 7, "c");
    evlog_add("node-12", 3, "d");             /* 꼬리, 곧 지워진다 */
    unsigned purged = evlog_purge(10);
    evlog_add("node-12", 12, "e");            /* 꼬리 뒤에 붙인다 */
    printf("uaf: purged=%u left=%u values=", purged, g_nrec);
    for (struct rec *r = g_head; r; r = r->next) printf("%d%s", r->value, r->next ? "," : "\n");
    evlog_shutdown();
}

static void case_leak(void) {
    evlog_add("gw-temp", 21, "rx");
    evlog_add("node-07", 22, "rx");
    evlog_add("node-12", 23, "rx");
    evlog_annotate("gw-temp", "rx crc-retry");
    evlog_annotate("node-07", "rx late");
    evlog_annotate("gw-temp", "ack");
    printf("leak: recs=%u texts=", g_nrec);
    for (struct rec *r = g_head; r; r = r->next) printf("%s%s", r->text, r->next ? "," : "\n");
    evlog_shutdown();
}

static void case_cache(void) {
    static const char *names[] = { "gw-temp", "node-07", "node-12", "node-07", "gw-volt", "node-31" };
    for (int i = 0; i < 6; i++) evlog_add(names[i], 100 + i, "x");
    printf("cache: devs=%u recs=%u\n", dev_count(), g_nrec);
    evlog_shutdown();
}

static void case_uninit(void) {
    evlog_add("gw-temp", 1, "a");
    evlog_add("node-07", 2, "b");
    evlog_retry("gw-temp");
    evlog_retry("gw-temp");
    evlog_retry("node-07");
    unsigned total = 0;
    for (struct rec *r = g_head; r; r = r->next) total += r->retries;
    printf("uninit: retries=%u\n", total);
    evlog_shutdown();
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);   /* 도구가 프로세스를 끝내도 결과 줄이 남게 */
    const char *c = argc > 1 ? argv[1] : "all";
    int all = strcmp(c, "all") == 0, ran = 0;
    if (all || !strcmp(c, "overflow")) { case_overflow(); ran = 1; }
    if (all || !strcmp(c, "uaf"))      { case_uaf();      ran = 1; }
    if (all || !strcmp(c, "leak"))     { case_leak();     ran = 1; }
    if (all || !strcmp(c, "cache"))    { case_cache();    ran = 1; }
    if (all || !strcmp(c, "uninit"))   { case_uninit();   ran = 1; }
    if (!ran) { fprintf(stderr, "usage: %s overflow|uaf|leak|cache|uninit|all\n", argv[0]); return 2; }
    return 0;
}
