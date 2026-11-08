/* C-B10 static과 Linkage: 파일 스코프·함수 내 static·extern
 *   Part A: 함수 내 static 카운터 (static storage duration, 연결 없음)
 *   Part B: init-once 지연 초기화 · 정적 버퍼 반환의 재진입 함정
 *   Part C: static 으로 상태를 숨긴 UART 드라이버 모듈 + TX ISR 시뮬레이션
 * (여러 파일 실험은 proj/ 디렉터리 — 레슨 md의 nm 실습 참고)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Part A: 함수 내 static 카운터 ---------------- */
/* 비교용: 자동(automatic) 지역 변수는 호출마다 새로 만들어진다 */
static uint32_t auto_next(void) { uint32_t seq = 0; return ++seq; }

/* 호출할 때마다 1, 2, 3, ... */
static uint32_t seq_next(void) {
    static uint32_t seq;            /* 0으로 단 한 번 초기화, 호출 사이 값 유지 */
    return ++seq;
}
/* 다른 함수의 같은 이름 static 은 별개 객체. 101, 102, ... */
static uint32_t other_next(void) {
    static uint32_t seq = 100;      /* 초기화는 프로그램 시작 전 1회 (대입이 아님) */
    return ++seq;
}
/* 처음 호출에서만 true */
static bool first_time(void) {
    static bool called;
    if (called) return false;
    called = true;
    return true;
}

/* ---------------- Part B: init-once 와 정적 버퍼 ---------------- */
/* (주어짐) CRC-8 (poly 0x07) 한 바이트 값 계산 — 느린 비트 루프 */
static uint8_t crc8_slow(uint8_t c) {
    for (int b = 0; b < 8; b++)
        c = (uint8_t)((c & 0x80u) ? ((c << 1) ^ 0x07u) : (c << 1));
    return c;
}
static unsigned g_table_builds;     /* 테스트용: 테이블을 몇 번 만들었나 */

/* 처음 호출될 때 한 번만 256칸 테이블을 만들고, 그 테이블 주소를 돌려준다 */
static const uint8_t *crc8_table(void) {
    static uint8_t table[256];      /* static: return 후에도 살아 있다 */
    static bool    ready;
    if (!ready) {
        for (unsigned i = 0; i < 256; i++) table[i] = crc8_slow((uint8_t)i);
        ready = true;
        g_table_builds++;
    }
    return table;
}
static uint8_t crc8(const uint8_t *p, size_t n) {
    const uint8_t *t = crc8_table();
    uint8_t c = 0;
    while (n--) c = t[c ^ *p++];
    return c;
}

/* 32비트 값을 "0x0000ABCD" 로. 정적 버퍼를 돌려준다 → 재진입 불가 */
static const char *hex32(uint32_t v) {
    static char buf[11];
    static const char digits[] = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) buf[2 + i] = digits[(v >> (28 - 4 * i)) & 0xFu];
    buf[10] = '\0';
    return buf;
}
/* 재진입 가능한 버전: 버퍼를 호출자가 준다 */
static char *hex32_r(uint32_t v, char out[11]) {
    static const char digits[] = "0123456789ABCDEF";   /* 읽기 전용 static 은 공유해도 안전 */
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 8; i++) out[2 + i] = digits[(v >> (28 - 4 * i)) & 0xFu];
    out[10] = '\0';
    return out;
}

/* ---------------- Part C: static 상태를 숨긴 UART 모듈 ---------------- */
/* ==== "uart.h" 라고 생각하자: 공개 API (외부 연결) ==== */
extern uint32_t uart_bytes_sent;    /* 선언만. 정의는 아래 모듈 안 */
bool     uart_init(void);           /* 첫 호출 true, 이미 초기화됐으면 false */
bool     uart_putc(uint8_t c);      /* 송신 큐에 넣기. 미초기화·가득 참이면 false */
void     uart_tx_isr(void);         /* TXE 인터럽트 흉내: 큐에서 1바이트를 TDR로 */
uint32_t uart_dropped(void);        /* 가득 차서 버린 바이트 수 */

/* ==== "uart.c" 라고 생각하자: 여기서부터 static 은 이 파일 밖에서 안 보인다 ==== */
#define TXQ_SIZE 8u                 /* 2의 거듭제곱 */
static volatile uint8_t  s_txq[TXQ_SIZE];  /* ISR 과 공유: 데이터도 volatile 이어야 순서 보장 */
static volatile uint32_t s_head, s_tail;  /* free-running 인덱스 */
static bool              s_ready;
static uint32_t          s_dropped;
uint32_t                 uart_bytes_sent;  /* 외부 연결 정의 (tentative → 0) */

/* 가짜 하드웨어: TDR 에 쓴 바이트가 g_wire 에 쌓인다 */
static char     g_wire[32];
static uint32_t g_wire_len;
static void hw_write_tdr(uint8_t b) {
    if (g_wire_len < sizeof g_wire - 1) g_wire[g_wire_len++] = (char)b;
}

bool uart_init(void) {
    if (s_ready) return false;
    s_head = s_tail = 0;
    s_dropped = 0;
    s_ready = true;
    return true;
}
bool uart_putc(uint8_t c) {
    if (!s_ready) return false;
    if (s_head - s_tail == TXQ_SIZE) { s_dropped++; return false; }
    s_txq[s_head % TXQ_SIZE] = c;
    s_head = s_head + 1;            /* 데이터를 먼저 쓰고 인덱스를 나중에 */
    return true;
}
void uart_tx_isr(void) {
    if (s_tail == s_head) return;   /* 보낼 것 없음 */
    hw_write_tdr(s_txq[s_tail % TXQ_SIZE]);
    s_tail = s_tail + 1;
    uart_bytes_sent++;
}
uint32_t uart_dropped(void) { return s_dropped; }

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* Part A */
    CHECK(auto_next() == 1 && auto_next() == 1);    /* 대조군: 항상 1 */
    CHECK(seq_next() == 1);
    CHECK(seq_next() == 2);
    CHECK(other_next() == 101);                     /* seq_next 와 독립 */
    CHECK(seq_next() == 3);
    CHECK(first_time() == true);
    CHECK(first_time() == false);
    CHECK(first_time() == false);

    /* Part B */
    const uint8_t msg[] = "123456789";
    CHECK(g_table_builds == 0);
    CHECK(crc8(msg, 9) == 0xF4);                    /* CRC-8/SMBUS check 값 */
    CHECK(crc8(msg, 9) == 0xF4 && g_table_builds == 1);  /* 두 번째엔 다시 안 만듦 */
    CHECK(crc8_table() == crc8_table());            /* 같은 static 배열 주소 */
    const char *p1 = hex32(0xDEADBEEFu);
    CHECK(strcmp(p1, "0xDEADBEEF") == 0);
    const char *p2 = hex32(0x1234u);
    CHECK(p1 == p2 && strcmp(p1, "0x00001234") == 0);   /* p1 이 덮어써졌다! */
    char a[11], b[11];
    hex32_r(0xDEADBEEFu, a);
    hex32_r(0x1234u, b);
    CHECK(strcmp(a, "0xDEADBEEF") == 0 && strcmp(b, "0x00001234") == 0);

    /* Part C */
    CHECK(uart_putc('x') == false);                 /* init 전 */
    CHECK(uart_init() == true);
    CHECK(uart_init() == false);                    /* init-once */
    CHECK(uart_putc('H') && uart_putc('i'));
    CHECK(g_wire_len == 0);                         /* 아직 ISR 이 안 돌았다 */
    uart_tx_isr(); uart_tx_isr(); uart_tx_isr();    /* 세 번째는 큐가 비어 no-op */
    CHECK(g_wire_len == 2 && memcmp(g_wire, "Hi", 2) == 0);
    for (int i = 0; i < 8; i++) CHECK(uart_putc((uint8_t)('0' + i)));
    CHECK(uart_putc('!') == false && uart_dropped() == 1);  /* 9번째는 버려짐 */
    for (int i = 0; i < 10; i++) uart_tx_isr();
    CHECK(strcmp(g_wire, "Hi01234567") == 0);
    CHECK(uart_bytes_sent == 10);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
