/* TOOL-M10 추가 연습 — kvs: 설정 key=value 저장소 (게이트웨이 설정 파일 / 펌웨어 provisioning 파서)
 *
 * 고친 버전이 기본. -DBUGGY 로 빌드하면 연습 문제의 버그 3개가 되살아난다:
 *   grow    : 정렬 삽입 위치 포인터를 realloc "전에" 잡고 늘린 뒤에도 쓴다 (vector 무효화)
 *   parse   : 잘못된 토큰에서 이미 복사한 문자열을 버리고 return (오류 경로 누수)
 *   replace : 같은 key 를 다시 넣을 때 예전 값을 free 하고 포인터는 그대로 → 종료 때 또 free
 *
 *   ./kv grow | parse | replace | all
 * check.sh practice 가 고친 버전(모든 도구 깨끗)과 BUGGY 버전(도구마다 잡힘)을 둘 다 확인한다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o kv && ./kv all
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct kv { char *key; char *val; };
struct kvs { struct kv *items; size_t n, cap; };

static char *str_ndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (p) { memcpy(p, s, n); p[n] = '\0'; }
    return p;
}

static struct kv *kvs_find(struct kvs *s, const char *key) {
    for (size_t i = 0; i < s->n; i++)
        if (strcmp(s->items[i].key, key) == 0) return &s->items[i];
    return NULL;
}

/* key 순서로 정렬해 넣는다. key/val 소유권을 가져간다 (성공 0, 실패 -1 이면 호출자가 그대로 가진다) */
static int kvs_put_owned(struct kvs *s, char *key, char *val) {
    struct kv *old = kvs_find(s, key);
    if (old) {
        free(old->val);
#ifdef BUGGY
        (void)val;                         /* 예전 값 포인터를 그대로 둔다 */
        free(key);                         /* → old->val 은 이미 free 된 메모리를 가리킨다 */
        return 0;
#else
        old->val = val;                    /* 새 값으로 교체 */
        free(key);
        return 0;
#endif
    }
    size_t i = 0;                          /* 들어갈 자리 (lower bound) */
    while (i < s->n && strcmp(s->items[i].key, key) < 0) i++;
#ifdef BUGGY
    struct kv *pos = s->items ? s->items + i : NULL;   /* 늘리기 "전에" 잡은 포인터 */
#endif
    if (s->n == s->cap) {
        size_t ncap = s->cap ? s->cap * 2 : 2;
        struct kv *p = realloc(s->items, ncap * sizeof *p);
        if (!p) return -1;                 /* 실패해도 예전 배열은 살아 있다 */
        s->items = p;
        s->cap = ncap;
    }
#ifdef BUGGY
    if (!pos) pos = s->items + i;
#else
    struct kv *pos = s->items + i;         /* 늘린 "뒤에" 인덱스로 다시 계산 */
#endif
    memmove(pos + 1, pos, (s->n - i) * sizeof *pos);
    pos->key = key;
    pos->val = val;
    s->n++;
    return 0;
}

/* "k=v;k=v;..." — 잘못된 토큰(= 없음, 빈 key)은 건너뛰고 bad 로 센다 */
static int kvs_parse(struct kvs *s, const char *text, unsigned *bad) {
    int ok = 0;
    const char *p = text;
    while (*p) {
        size_t len = strcspn(p, ";");
        char *tok = str_ndup(p, len);
        if (!tok) return -1;
        char *eq = strchr(tok, '=');
        if (!eq || eq == tok) {
            (*bad)++;
#ifndef BUGGY
            free(tok);                     /* 오류 경로에서도 돌려준다 */
#endif
        } else {
            *eq = '\0';
            char *k = str_ndup(tok, (size_t)(eq - tok)), *v = str_ndup(eq + 1, strlen(eq + 1));
            free(tok);
            if (!k || !v || kvs_put_owned(s, k, v) != 0) { free(k); free(v); return -1; }
            ok++;
        }
        p += len;
        if (*p == ';') p++;
    }
    return ok;
}

static void kvs_free(struct kvs *s) {
    for (size_t i = 0; i < s->n; i++) { free(s->items[i].key); free(s->items[i].val); }
    free(s->items);
    s->items = NULL;
    s->n = s->cap = 0;
}

static void print_keys(const char *tag, struct kvs *s) {
    printf("%s: n=%zu", tag, s->n);
    for (size_t i = 0; i < s->n; i++) printf("%s%s=%s", i ? "," : " ", s->items[i].key, s->items[i].val);
    printf("\n");
}

static void case_grow(void) {
    struct kvs s = {0};
    unsigned bad = 0;
    kvs_parse(&s, "uart=115200;spi=8M;i2c=400k;can=500k;eth=1G", &bad);   /* cap 2 → 4 → 8 */
    print_keys("grow", &s);
    kvs_free(&s);
}

static void case_parse(void) {
    struct kvs s = {0};
    unsigned bad = 0;
    int ok = kvs_parse(&s, "mode=rx;noeq;=empty;ch=11;also-bad", &bad);
    printf("parse: ok=%d bad=%u\n", ok, bad);
    kvs_free(&s);
}

static void case_replace(void) {
    struct kvs s = {0};
    unsigned bad = 0;
    kvs_parse(&s, "mode=rx;ch=11", &bad);
    kvs_parse(&s, "mode=tx", &bad);
    printf("replace: n=%zu mode=%s\n", s.n, kvs_find(&s, "mode") ? "set" : "missing");
    kvs_free(&s);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *c = argc > 1 ? argv[1] : "all";
    int all = strcmp(c, "all") == 0, ran = 0;
    if (all || !strcmp(c, "grow"))    { case_grow();    ran = 1; }
    if (all || !strcmp(c, "parse"))   { case_parse();   ran = 1; }
    if (all || !strcmp(c, "replace")) { case_replace(); ran = 1; }
    if (!ran) { fprintf(stderr, "usage: %s grow|parse|replace|all\n", argv[0]); return 2; }
    return 0;
}
