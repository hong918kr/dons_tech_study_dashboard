// 06_net_parsing.c  —  PRACTICE STUB (직접 채워넣기)
// 네트워크 · 프로토콜 파싱 (Networking & Protocol Parsing)  —  Q49~Q58
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=06_net_parsing
//   또는:     cc -std=c11 -Wall -Wextra -O1 -g -pthread problems/06_net_parsing.c -o /tmp/vk06p && /tmp/vk06p
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다. 블로킹·무한루프 없음.)
//
// Verkada Connectivity 팀의 "하루 일과" 그 자체다. GC31-E 셀룰러 게이트웨이는
// LTE 모뎀과 AT 커맨드로 대화하고, 듀얼 SIM failover를 신호 품질(RSSI/RSRP)로 판단하며,
// 링크 위로 프레임을 실어 나르고 체크섬으로 무결성을 지킨다. 실제 후기에서 보고된
// 코딩 문제도 "Extract IP Addresses"였고, 라운드 하나는 "제품 연결시키고 트러블슈팅"이었다.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// ===========================================================================
// 테스트 하네스 (건드리지 말 것)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// 공용 소도구 — ctype.h 는 locale/부호 확장 이슈가 있어 직접 쓴다. (제공됨)
static bool is_dig(char c)   { return c >= '0' && c <= '9'; }
static bool is_alnum_c(char c) {
    return is_dig(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

#define IPV4_STR_MAX 16   // "255.255.255.255" + '\0'

/* ---------------------------------------------------------------------------
 * Q49.  IPv4 문자열 -> uint32_t (엄격 파서)
 *   KO: "a.b.c.d" 를 host order uint32(상위 바이트 = 첫 옥텟)로 바꾼다. 다음을
 *       전부 거부: 선행 0("01.2.3.4"), 255 초과, 점 개수 오류("1.2.3"/"1.2.3.4.5"),
 *       빈 옥텟("1..2.3"), 선행 공백, 꼬리 쓰레기("1.2.3.4x"), NULL.
 *       선행 0 허용은 8진 해석("010.1.1.1" -> 8.1.1.1) ACL 우회 취약점으로 이어진다.
 *   EN: Strictly parse dotted-quad IPv4 into a host-order uint32; reject leading
 *       zeros, out-of-range octets, wrong dot count, empty octets, trailing junk.
 *   ex: "10.0.0.7" -> true, *out == 0x0A000007 ; "256.1.1.1" -> false
 * ------------------------------------------------------------------------- */
bool ipv4_parse(const char *s, uint32_t *out) {
    (void)s; (void)out;
    // TODO: implement
    return false;
}

/* ---------------------------------------------------------------------------
 * Q50.  uint32_t -> IPv4 문자열 (버퍼 크기 안전)
 *   KO: ip 를 "a.b.c.d" 로 포맷해 out 에 기록. 성공하면 기록한 문자 수(널 제외),
 *       버퍼 부족/NULL/out_sz==0 이면 -1. 부분 기록 금지 — 먼저 로컬에 만들고
 *       크기를 확인한 뒤 복사하고, 실패해도 out 은 널 종료시킬 것.
 *   EN: Format a host-order uint32 as dotted quad; return chars written or -1
 *       if it would not fit. Never write a truncated address.
 *   ex: 0x0A000007 -> "10.0.0.7", 반환 8 ;  out_sz 8 에 0xFFFFFFFF -> -1
 * ------------------------------------------------------------------------- */
int ipv4_format(uint32_t ip, char *out, size_t out_sz) {
    (void)ip;
    if (out && out_sz) out[0] = '\0';
    // TODO: implement
    return -1;
}

/* ---------------------------------------------------------------------------
 * Q51.  CIDR 매칭 — "10.0.0.0/8" 에 특정 IP가 속하는가
 *   KO: cidr_mask(prefix)는 상위 prefix 비트만 1인 마스크. prefix==0 을 특수
 *       처리해야 한다 — 32비트 값을 32비트 시프트하는 것은 UB.
 *       cidr_parse 는 "a.b.c.d/p" 를 파싱해 마스크 적용된 네트워크와 prefix 를
 *       돌려준다(0<=p<=32, "/08" 같은 선행 0과 꼬리 쓰레기는 거부).
 *       cidr_contains 는 (ip & mask) == net 을 검사한다.
 *   EN: Build the prefix mask (beware the UB of a 32-bit shift by 32), parse
 *       "a.b.c.d/p" strictly, and test membership with (ip & mask) == net.
 *   ex: cidr_contains("10.0.0.0/8", 0x0A010203) -> true ; "0.0.0.0/0" -> 항상 true
 * ------------------------------------------------------------------------- */
uint32_t cidr_mask(unsigned prefix) {
    (void)prefix;
    // TODO: implement
    return 0u;
}

bool cidr_parse(const char *s, uint32_t *net_out, unsigned *prefix_out) {
    (void)s; (void)net_out; (void)prefix_out;
    // TODO: implement
    return false;
}

bool cidr_contains(const char *cidr, uint32_t ip) {
    (void)cidr; (void)ip;
    // TODO: implement
    return false;
}

/* ---------------------------------------------------------------------------
 * Q52.  로그 한 줄에서 IPv4 주소 모두 추출   ★ 실제 보고된 면접 문제 유형
 *   KO: line 을 훑어 유효한 IPv4 를 모두 찾는다. 반환값 = 찾은 총 개수(잘림 여부를
 *       호출자가 알 수 있게), out 에는 최대 max_out 개만 기록. 경계/오탐 규칙:
 *         - [0-9.] 덩어리를 통째로 잘라 Q49 엄격 파서에 넘긴다("1.2.3.4.5" 자동 탈락)
 *         - 덩어리 양옆이 영숫자면 거부("v1.2.3.4", "1.2.3.4a")
 *         - ':' '/' ',' 공백 등 구분자 뒤/앞은 허용("192.168.0.1:8080")
 *   EN: Scan a log line and collect every valid IPv4; return the total found,
 *       store at most max_out. Reject version-string false positives.
 *   ex: "conn from 10.0.0.7 to 8.8.8.8 ok" -> 2
 * ------------------------------------------------------------------------- */
size_t ipv4_extract_all(const char *line, uint32_t *out, size_t max_out) {
    (void)line; (void)out; (void)max_out;
    (void)is_alnum_c;   // 구현 시 경계 판정에 쓰게 됨
    // TODO: implement
    return 0;
}

/* ---------------------------------------------------------------------------
 * Q53.  인터넷 체크섬 (RFC 1071)
 *   KO: 버퍼를 빅엔디안 16비트 워드로 더하고, 넘친 캐리를 하위로 접고(fold),
 *       비트 반전해 반환한다. 홀수 길이면 마지막 바이트를 **상위** 바이트로 패딩.
 *       바이트를 직접 (p[0]<<8)|p[1] 로 조립하므로 호스트 엔디안과 무관하다.
 *       성질: 체크섬을 버퍼 뒤에 빅엔디안으로 붙여 다시 계산하면 0 이 나온다.
 *   EN: RFC 1071 one's-complement sum of 16-bit big-endian words, carries
 *       folded back, result inverted. Odd trailing byte pads into the high half.
 *   ex: {00 01 f2 03 f4 f5 f6 f7} -> 0x220d
 * ------------------------------------------------------------------------- */
uint16_t inet_checksum(const void *data, size_t len) {
    (void)data; (void)len;
    // TODO: implement
    return 0u;
}

// 체크섬을 버퍼 끝에 빅엔디안으로 붙일 때 쓰는 헬퍼 (제공됨)
static void be16_put(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v >> 8);
    dst[1] = (uint8_t)(v & 0xFFu);
}

#define HEXDUMP_BYTES_PER_LINE 16
#define HEXDUMP_LINE_MAX       80   // 60(고정폭) + 1 + 16 + 1 + 1(널)

/* ---------------------------------------------------------------------------
 * Q54.  hex dump 유틸 — "offset | hex | ascii" 한 줄
 *   KO: hexdump -C 와 같은 형식으로 한 줄을 만든다.
 *       "00000010  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  |....|"
 *       = 소문자 hex 오프셋 8자리 + 2칸 + 8바이트("xx ") + 1칸 + 8바이트 + 1칸
 *         + "|" + ASCII + "|".  len<16 이면 hex 칸은 공백 패딩, ASCII 는 있는 만큼.
 *       0x20~0x7E 밖의 바이트는 '.'. 반환값 = 기록 길이, 실패 시 -1
 *       (len>16, 버퍼 부족, out==NULL, len>0인데 data==NULL).
 *   EN: Render one hexdump -C style line: 8-digit offset, 16 hex bytes with a
 *       gap after the 8th, then the printable-ASCII gutter. Return length or -1.
 *   ex: offset 0x10, 바이트 00..0f -> 위 문자열(78자)
 * ------------------------------------------------------------------------- */
int hexdump_line(char *out, size_t out_sz,
                 size_t offset, const uint8_t *data, size_t len) {
    (void)offset; (void)data; (void)len;
    if (out && out_sz) out[0] = '\0';
    // TODO: implement
    return -1;
}

#define AT_LINE_MAX 64

typedef enum {
    AT_KIND_OK,        // 최종 응답 성공
    AT_KIND_ERROR,     // 최종 응답 실패 (ERROR / +CME ERROR / +CMS ERROR)
    AT_KIND_URC,       // '+' 로 시작하는 정보/비동기 통지 (+CSQ, +CREG, ...)
    AT_KIND_INFO,      // 에코나 그 밖의 텍스트
    AT_KIND_OVERFLOW   // 줄이 너무 길어 잘림 — 링크 오류 신호
} at_kind_t;

typedef struct {
    at_kind_t kind;
    char      text[AT_LINE_MAX];
} at_line_t;

typedef struct {
    char   buf[AT_LINE_MAX];
    size_t len;
    bool   overflow;   // 현재 줄이 이미 넘쳤는가
} at_parser_t;

// 제공됨 — 문자열 접두 검사
static bool starts_with(const char *s, const char *pre) {
    return strncmp(s, pre, strlen(pre)) == 0;
}

/* ---------------------------------------------------------------------------
 * Q55.  AT 커맨드 응답 파서 FSM — 스트림 재조립 + 분류
 *   KO: 모뎀 UART 는 "줄"이 아니라 바이트를 흘린다. "+CSQ: 2" / "3,99\r\nOK\r" /
 *       "\n" 처럼 아무데서나 잘려도 동작하는, **호출 사이에 상태를 들고 있는** 파서를
 *       만든다. '\r' 또는 '\n' 이 줄 종결자이고 빈 줄은 버린다. 줄이 AT_LINE_MAX 를
 *       넘으면 AT_KIND_OVERFLOW 로 보고하고 다음 종결자까지 버린 뒤 복구한다.
 *       분류: "OK" / "ERROR"·"+CME ERROR"·"+CMS ERROR" / '+' 시작=URC / 그 외=INFO.
 *       반환값 = 이번 feed 로 완성된 줄의 총 개수(out 에는 최대 max_out 개 기록).
 *   EN: A resumable line assembler for modem UART output: split on CR or LF,
 *       drop empty lines, report over-long lines as OVERFLOW and resync, and
 *       classify each complete line. Return the number of lines completed.
 *   ex: feed("+CSQ: 23,99\r\nOK\r\n") -> 2줄 {URC "+CSQ: 23,99"}, {OK "OK"}
 * ------------------------------------------------------------------------- */
void at_init(at_parser_t *p) {
    (void)p;
    // TODO: implement
}

size_t at_feed(at_parser_t *p, const char *chunk, size_t n,
               at_line_t *out, size_t max_out) {
    (void)p; (void)chunk; (void)n; (void)out; (void)max_out;
    // TODO: implement
    return 0;
}

typedef enum {
    SIG_UNKNOWN = 0,
    SIG_POOR,
    SIG_FAIR,
    SIG_GOOD,
    SIG_EXCELLENT
} sig_grade_t;

/* ---------------------------------------------------------------------------
 * Q56.  신호 품질 변환 — AT+CSQ RSSI -> dBm, RSRP/RSRQ 클램프 + 등급
 *   KO: +CSQ 의 첫 값은 dBm 이 아니라 0~31 인덱스다: dBm = -113 + 2*rssi
 *       (0 -> -113, 31 -> -51, 99 = 측정 불가).
 *       - sig_clamp(v, lo, hi): 구간으로 자르기
 *       - at_parse_csq("+CSQ: 23,99", &rssi, &ber): 공백 허용, 형식 틀리면 false
 *       - csq_to_dbm: 0~31 만 true. 99/범위 밖은 false 이고 *dbm_out 은 건드리지 않음
 *       - csq_grade: 99 는 SIG_POOR 가 아니라 SIG_UNKNOWN (이걸 틀리면 SIM 무한 스왑)
 *         dBm >= -70 EXCELLENT, >= -85 GOOD, >= -100 FAIR, 그 외 POOR
 *       - rsrp_grade: [-140,-44] 로 클램프 후 >=-80 EXCELLENT, >=-90 GOOD,
 *         >=-100 FAIR, 그 외 POOR
 *       - rsrq_grade: [-20,-3] 로 클램프 후 >=-10 EXCELLENT, >=-15 GOOD,
 *         >=-18 FAIR, 그 외 POOR
 *   EN: Convert AT+CSQ's 0..31 index to dBm (99 = unknown, not "worst"), clamp
 *       RSRP/RSRQ into their valid ranges, and bucket them into quality grades.
 *   ex: csq_to_dbm(23) -> -67dBm ; csq_grade(99) -> SIG_UNKNOWN
 * ------------------------------------------------------------------------- */
int sig_clamp(int v, int lo, int hi) {
    (void)lo; (void)hi;
    // TODO: implement
    return v;
}

bool at_parse_csq(const char *line, int *rssi_out, int *ber_out) {
    (void)line; (void)rssi_out; (void)ber_out;
    (void)starts_with;   // 구현 시 쓰게 됨
    // TODO: implement
    return false;
}

bool csq_to_dbm(int csq, int *dbm_out) {
    (void)csq; (void)dbm_out;
    // TODO: implement
    return false;
}

sig_grade_t csq_grade(int csq) {
    (void)csq;
    // TODO: implement
    return SIG_UNKNOWN;
}

sig_grade_t rsrp_grade(int rsrp_dbm) {
    (void)rsrp_dbm;
    // TODO: implement
    return SIG_UNKNOWN;
}

sig_grade_t rsrq_grade(int rsrq_db) {
    (void)rsrq_db;
    // TODO: implement
    return SIG_UNKNOWN;
}

typedef enum {
    TLV_OK,          // 프레임 하나를 온전히 꺼냄, *off 전진
    TLV_NEED_MORE,   // 바이트 부족 — *off 그대로, 더 받아서 재시도
    TLV_BAD          // 프로토콜 위반 — 재동기화/연결 리셋 필요
} tlv_status_t;

typedef struct {
    uint8_t        type;
    uint16_t       len;
    const uint8_t *val;   // buf 안을 가리키는 포인터 (복사 없음, zero-copy)
} tlv_t;

#define TLV_HDR_LEN 3

/* ---------------------------------------------------------------------------
 * Q57.  길이-접두(TLV) 프레이밍 디코더 — 부분 버퍼 / 잘린 헤더 / 악의적 길이
 *   KO: 프레임 = [type:1][len:2 big-endian][payload:len].
 *       buf[*off..n) 에서 프레임 하나를 꺼낸다. 계약 3가지:
 *         (1) 바이트가 모자라면 TLV_NEED_MORE 이고 **off 를 전진시키지 않는다**
 *             (호출자가 남은 바이트를 보관해 다음 read 와 이어붙일 수 있어야 함)
 *         (2) len > max_len 이면 TLV_BAD — 공격자가 0xFFFF 로 자원 고갈시키는 것 방어
 *         (3) off + 3 + len 을 더해서 비교하지 말 것 — size_t 오버플로. 뺄셈으로 비교.
 *       val 은 buf 안을 가리키는 zero-copy 포인터(len==0 이면 NULL).
 *   EN: Pull one length-prefixed frame out of a partial buffer. Never advance
 *       the offset on NEED_MORE, reject oversized lengths, avoid size_t overflow.
 *   ex: {01 00 02 'h' 'i'} -> TLV_OK, type=1, len=2, off 0->5
 * ------------------------------------------------------------------------- */
tlv_status_t tlv_next(const uint8_t *buf, size_t n, size_t *off,
                      tlv_t *out, uint16_t max_len) {
    (void)buf; (void)n; (void)off; (void)out; (void)max_len;
    // TODO: implement
    return TLV_BAD;
}

#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

/* ---------------------------------------------------------------------------
 * Q58.  SLIP 바이트 스터핑 (RFC 1055) 인코드 / 디코드
 *   KO: END=0xC0 가 프레임 경계. 페이로드 안의 0xC0 은 0xDB 0xDC 로, 0xDB 은
 *       0xDB 0xDD 로 치환한다. 인코더는 앞뒤로 END 를 붙인다(앞 END = 회선
 *       노이즈를 빈 프레임으로 흘려보내는 재동기화 장치). 최악 크기 2n+2.
 *       디코더는 선행 END 를 건너뛰고 종결 END 에서 멈춘다. -1 을 반환해야 하는 경우:
 *       잘못된 이스케이프(ESC 뒤가 0xDC/0xDD 아님), ESC 로 끝남, 종결 END 없음,
 *       출력 버퍼 부족, 포인터 NULL. 반환값 = 기록한 바이트 수.
 *   EN: RFC 1055 byte stuffing: escape END/ESC in the payload, bracket the frame
 *       with END. Decoder skips leading ENDs, stops at the trailing END, and
 *       rejects bad escapes / truncated frames / undersized output buffers.
 *   ex: {C0 01 DB} -> {C0 DB DC 01 DB DD C0} (7바이트), 왕복하면 원본
 * ------------------------------------------------------------------------- */
long slip_encode(const uint8_t *in, size_t n, uint8_t *out, size_t out_sz) {
    (void)in; (void)n; (void)out; (void)out_sz;
    // TODO: implement
    return -1;
}

long slip_decode(const uint8_t *in, size_t n, uint8_t *out, size_t out_sz) {
    (void)in; (void)n; (void)out; (void)out_sz;
    // TODO: implement
    return -1;
}

// ===========================================================================
// main — 테스트 (건드리지 말 것)
// ===========================================================================
int main(void) {
    printf("== Q49 IPv4 문자열 -> uint32_t (엄격 파서) ==\n");
    {
        uint32_t ip = 0;
        T("\"10.0.0.7\" -> 0x0A000007",
          ipv4_parse("10.0.0.7", &ip) && ip == 0x0A000007u);
        T("\"255.255.255.255\" -> 0xFFFFFFFF",
          ipv4_parse("255.255.255.255", &ip) && ip == 0xFFFFFFFFu);
        T("\"0.0.0.0\" -> 0 (단독 0은 허용)",
          ipv4_parse("0.0.0.0", &ip) && ip == 0u);
        T("선행 0 거부: \"01.2.3.4\"",       !ipv4_parse("01.2.3.4", &ip));
        T("범위 초과 거부: \"256.1.1.1\"",   !ipv4_parse("256.1.1.1", &ip));
        T("점 개수 거부: \"1.2.3\" / \"1.2.3.4.5\"",
          !ipv4_parse("1.2.3", &ip) && !ipv4_parse("1.2.3.4.5", &ip));
        T("빈 옥텟/공백/꼬리 거부",
          !ipv4_parse("1..2.3", &ip) && !ipv4_parse(" 1.2.3.4", &ip) &&
          !ipv4_parse("1.2.3.4x", &ip) && !ipv4_parse("", &ip));
        T("NULL 안전", !ipv4_parse(NULL, &ip) && !ipv4_parse("1.2.3.4", NULL));
    }

    printf("\n== Q50 uint32_t -> IPv4 문자열 (버퍼 안전) ==\n");
    {
        char buf[IPV4_STR_MAX];
        int n = ipv4_format(0x0A000007u, buf, sizeof buf);
        T("0x0A000007 -> \"10.0.0.7\", len 8", n == 8 && strcmp(buf, "10.0.0.7") == 0);

        n = ipv4_format(0xFFFFFFFFu, buf, sizeof buf);
        T("0xFFFFFFFF -> \"255.255.255.255\", len 15 (딱 맞음)",
          n == 15 && strcmp(buf, "255.255.255.255") == 0);

        char small[8];
        T("버퍼 부족 -> -1 (부분 기록 금지)",
          ipv4_format(0xFFFFFFFFu, small, sizeof small) == -1 && small[0] == '\0');
        T("out_sz==0 / NULL -> -1",
          ipv4_format(0u, buf, 0) == -1 && ipv4_format(0u, NULL, 16) == -1);

        uint32_t rt;
        T("왕복: parse(format(x)) == x",
          ipv4_format(0xC0A80102u, buf, sizeof buf) == 11 &&
          ipv4_parse(buf, &rt) && rt == 0xC0A80102u);
    }

    printf("\n== Q51 CIDR 매칭 ==\n");
    {
        T("mask(0)=0, mask(8)=0xFF000000, mask(24)=0xFFFFFF00, mask(32)=0xFFFFFFFF",
          cidr_mask(0) == 0u && cidr_mask(8) == 0xFF000000u &&
          cidr_mask(24) == 0xFFFFFF00u && cidr_mask(32) == 0xFFFFFFFFu);

        uint32_t a = 0x0A010203u, b = 0x0B000001u, c = 0xC0A8012Au;

        T("10.0.0.0/8 는 10.1.2.3 포함, 11.0.0.1 미포함",
          cidr_contains("10.0.0.0/8", a) && !cidr_contains("10.0.0.0/8", b));
        T("0.0.0.0/0 은 전부 포함 (/0 경계)",
          cidr_contains("0.0.0.0/0", a) && cidr_contains("0.0.0.0/0", b) &&
          cidr_contains("0.0.0.0/0", 0xFFFFFFFFu));
        T("/32 는 자기 자신만",
          cidr_contains("192.168.1.42/32", c) && !cidr_contains("192.168.1.42/32", c + 1));

        uint32_t net; unsigned pfx;
        T("호스트 비트는 마스킹됨: 10.1.2.3/8 -> net 10.0.0.0",
          cidr_parse("10.1.2.3/8", &net, &pfx) && net == 0x0A000000u && pfx == 8);
        T("잘못된 CIDR 거부: /33, /08, 슬래시 없음, 빈 prefix",
          !cidr_parse("10.0.0.0/33", &net, &pfx) &&
          !cidr_parse("10.0.0.0/08", &net, &pfx) &&
          !cidr_parse("10.0.0.0",    &net, &pfx) &&
          !cidr_parse("10.0.0.0/",   &net, &pfx));
        T("CGNAT 100.64.0.0/10 판별",
          cidr_contains("100.64.0.0/10", 0x64400001u) &&
          !cidr_contains("100.64.0.0/10", 0x64800001u));
    }

    printf("\n== Q52 로그 줄에서 IPv4 전부 추출 ==\n");
    {
        uint32_t ips[8];
        size_t n;

        n = ipv4_extract_all("conn from 10.0.0.7 to 8.8.8.8 ok", ips, 8);
        T("두 개 추출, 순서 보존",
          n == 2 && ips[0] == 0x0A000007u && ips[1] == 0x08080808u);

        n = ipv4_extract_all("192.168.0.1:8080 -> 172.16.5.4/24", ips, 8);
        T("포트/CIDR 구분자 뒤에서도 추출",
          n == 2 && ips[0] == 0xC0A80001u && ips[1] == 0xAC100504u);

        n = ipv4_extract_all("999.1.1.1 1.2.3.4.5 01.2.3.4 1.2.3", ips, 8);
        T("오탐 0개: 범위초과/점5개/선행0/불완전", n == 0);

        n = ipv4_extract_all("fw v1.2.3.4 build 1.2.3.4a", ips, 8);
        T("버전 문자열 오탐 차단 (양옆 영숫자)", n == 0);

        n = ipv4_extract_all("", ips, 8);
        T("빈 문자열 / NULL -> 0",
          n == 0 && ipv4_extract_all(NULL, ips, 8) == 0);

        ips[0] = ips[1] = 0xDEADBEEFu;
        n = ipv4_extract_all("a 1.1.1.1 b 2.2.2.2 c", ips, 1);
        T("max_out 초과 시 총 개수는 반환하되 기록은 안 넘침",
          n == 2 && ips[0] == 0x01010101u && ips[1] == 0xDEADBEEFu);

        n = ipv4_extract_all("edge 0.0.0.0 255.255.255.255", ips, 8);
        T("경계값 0.0.0.0 / 255.255.255.255",
          n == 2 && ips[0] == 0u && ips[1] == 0xFFFFFFFFu);
    }

    printf("\n== Q53 인터넷 체크섬 (RFC 1071) ==\n");
    {
        // RFC 1071 문서의 예제 바이트열 -> 0x220d
        const uint8_t rfc[] = { 0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7 };
        T("RFC 1071 예제 -> 0x220d", inet_checksum(rfc, sizeof rfc) == 0x220du);

        uint8_t withck[sizeof rfc + 2];
        memcpy(withck, rfc, sizeof rfc);
        be16_put(withck + sizeof rfc, inet_checksum(rfc, sizeof rfc));
        T("검증 성질: 체크섬을 붙여 다시 계산하면 0",
          inet_checksum(withck, sizeof withck) == 0u);

        const uint8_t odd1[] = { 0x01 };
        const uint8_t odd2[] = { 0x01, 0x00 };
        T("홀수 길이 = 마지막 바이트를 상위로 패딩",
          inet_checksum(odd1, 1) == inet_checksum(odd2, 2));

        T("빈 버퍼 -> ~0 = 0xFFFF", inet_checksum(rfc, 0) == 0xFFFFu);

        const uint8_t carry[] = { 0xff, 0xff, 0xff, 0xff };
        T("캐리 fold 동작 (0xFFFF+0xFFFF -> 0xFFFF, ~ -> 0)",
          inet_checksum(carry, sizeof carry) == 0x0000u);

        uint8_t flip[sizeof rfc];
        memcpy(flip, rfc, sizeof rfc);
        flip[2] ^= 0x01u;
        T("1비트 오류는 검출됨",
          inet_checksum(flip, sizeof flip) != inet_checksum(rfc, sizeof rfc));
    }

    printf("\n== Q54 hex dump 한 줄 ==\n");
    {
        char line[HEXDUMP_LINE_MAX];
        uint8_t full[16];
        for (int i = 0; i < 16; i++) full[i] = (uint8_t)i;

        int n = hexdump_line(line, sizeof line, 0x10, full, 16);
        const char *want =
          "00000010  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  "
          "|................|";
        T("전체 16바이트 라인이 hexdump -C 형식과 일치",
          n == (int)strlen(want) && strcmp(line, want) == 0);

        n = hexdump_line(line, sizeof line, 0, (const uint8_t *)"Hello", 5);
        T("짧은 라인: hex는 공백 패딩, ASCII는 있는 만큼",
          n == 67 &&
          strncmp(line, "00000000  48 65 6c 6c 6f ", 25) == 0 &&
          strstr(line, "|Hello|") != NULL);

        const uint8_t np[] = { 0x00, 0x1f, 0x7f, 0x80, 'A' };
        n = hexdump_line(line, sizeof line, 0, np, sizeof np);
        T("비출력 문자는 '.' 로", n > 0 && strstr(line, "|....A|") != NULL);

        T("len>16 / 버퍼 부족 -> -1",
          hexdump_line(line, sizeof line, 0, full, 17) == -1 &&
          hexdump_line(line, 10, 0, full, 16) == -1 && line[0] == '\0');

        n = hexdump_line(line, sizeof line, 0xdeadbeefu, full, 1);
        T("오프셋이 하위 32비트 8자리로 찍힘",
          n > 0 && strncmp(line, "deadbeef  00 ", 13) == 0);
    }

    printf("\n== Q55 AT 커맨드 응답 파서 FSM ==\n");
    {
        at_parser_t p;
        at_line_t   L[8];
        size_t      n;

        at_init(&p);
        n = at_feed(&p, "+CSQ: 23,99\r\nOK\r\n", 17, L, 8);
        T("한 번에 두 줄: URC + OK",
          n == 2 && L[0].kind == AT_KIND_URC &&
          strcmp(L[0].text, "+CSQ: 23,99") == 0 && L[1].kind == AT_KIND_OK);

        at_init(&p);
        size_t t = 0;
        t += at_feed(&p, "+CRE", 4, L, 8);
        T("부분 수신 1: 아직 완성된 줄 없음", t == 0);
        n = at_feed(&p, "G: 0,5\r", 7, L, 8);
        T("부분 수신 2: 경계에서 조립됨",
          n == 1 && L[0].kind == AT_KIND_URC && strcmp(L[0].text, "+CREG: 0,5") == 0);
        n = at_feed(&p, "\nERROR\r\n", 8, L, 8);
        T("남은 LF는 빈 줄로 무시되고 ERROR 만 방출",
          n == 1 && L[0].kind == AT_KIND_ERROR && strcmp(L[0].text, "ERROR") == 0);

        at_init(&p);
        n = at_feed(&p, "+CME ERROR: 10\r\n", 16, L, 8);
        T("+CME ERROR 는 최종 실패로 분류",
          n == 1 && L[0].kind == AT_KIND_ERROR);

        at_init(&p);
        char longline[200];
        memset(longline, 'A', sizeof longline);
        n = at_feed(&p, longline, sizeof longline, L, 8);
        T("과도한 줄: 종결자 전엔 방출 안 함", n == 0);
        n = at_feed(&p, "\r\nOK\r\n", 6, L, 8);
        T("OVERFLOW 보고 후 다음 줄부터 정상 복구",
          n == 2 && L[0].kind == AT_KIND_OVERFLOW && L[1].kind == AT_KIND_OK);

        at_init(&p);
        n = at_feed(&p, "\r\n\r\n\r\n", 6, L, 8);
        T("빈 줄만 오면 0개", n == 0);

        at_init(&p);
        n = at_feed(&p, "AT+CSQ\r\n+CSQ: 2,0\r\nOK\r\n", 23, L, 1);
        T("max_out 초과해도 총 개수는 반환",
          n == 3 && L[0].kind == AT_KIND_INFO && strcmp(L[0].text, "AT+CSQ") == 0);
    }

    printf("\n== Q56 신호 품질 변환 ==\n");
    {
        int rssi = -1, ber = -1, dbm = 0;

        T("+CSQ 파싱: \"+CSQ: 23,99\" -> 23, 99",
          at_parse_csq("+CSQ: 23,99", &rssi, &ber) && rssi == 23 && ber == 99);
        T("공백 변형도 허용: \"+CSQ:5,0\"",
          at_parse_csq("+CSQ:5,0", &rssi, &ber) && rssi == 5 && ber == 0);
        T("형식 오류 거부",
          !at_parse_csq("+CREG: 0,5", &rssi, &ber) &&
          !at_parse_csq("+CSQ: 23", &rssi, &ber) &&
          !at_parse_csq("+CSQ: 23,99,7", &rssi, &ber));

        T("RSSI 경계: 0 -> -113dBm, 31 -> -51dBm",
          csq_to_dbm(0, &dbm) && dbm == -113 && csq_to_dbm(31, &dbm) && dbm == -51);
        T("RSSI 중간: 23 -> -67dBm", csq_to_dbm(23, &dbm) && dbm == -67);

        dbm = 12345;
        T("99(unknown)/범위 밖은 false, 출력 미변경",
          !csq_to_dbm(99, &dbm) && !csq_to_dbm(32, &dbm) && !csq_to_dbm(-1, &dbm) &&
          dbm == 12345);

        T("등급: 99 -> UNKNOWN (POOR 아님!)", csq_grade(99) == SIG_UNKNOWN);
        T("등급: 31 -> EXCELLENT, 0 -> POOR",
          csq_grade(31) == SIG_EXCELLENT && csq_grade(0) == SIG_POOR);

        T("clamp 동작", sig_clamp(5, 0, 3) == 3 && sig_clamp(-5, 0, 3) == 0 &&
                        sig_clamp(2, 0, 3) == 2);
        T("RSRP 클램프+등급: -75 EXCELLENT, -95 FAIR, -200 -> -140 POOR",
          rsrp_grade(-75) == SIG_EXCELLENT && rsrp_grade(-95) == SIG_FAIR &&
          rsrp_grade(-200) == SIG_POOR);
        T("RSRQ 클램프+등급: -8 EXCELLENT, -19 POOR, 0 -> -3 EXCELLENT",
          rsrq_grade(-8) == SIG_EXCELLENT && rsrq_grade(-19) == SIG_POOR &&
          rsrq_grade(0) == SIG_EXCELLENT);
    }

    printf("\n== Q57 TLV 프레이밍 디코더 ==\n");
    {
        // type=0x01 len=2 "hi" | type=0x02 len=0 | type=0x03 len=1 'Z'
        const uint8_t stream[] = {
            0x01, 0x00, 0x02, 'h', 'i',
            0x02, 0x00, 0x00,
            0x03, 0x00, 0x01, 'Z'
        };
        size_t off = 0;
        tlv_t  t;

        T("첫 프레임: type 1, len 2, \"hi\"",
          tlv_next(stream, sizeof stream, &off, &t, 1024) == TLV_OK &&
          t.type == 0x01 && t.len == 2 && memcmp(t.val, "hi", 2) == 0 && off == 5);
        T("len 0 프레임 허용 (val==NULL)",
          tlv_next(stream, sizeof stream, &off, &t, 1024) == TLV_OK &&
          t.len == 0 && t.val == NULL && off == 8);
        T("세 번째 프레임 후 스트림 소진",
          tlv_next(stream, sizeof stream, &off, &t, 1024) == TLV_OK &&
          t.type == 0x03 && off == sizeof stream &&
          tlv_next(stream, sizeof stream, &off, &t, 1024) == TLV_NEED_MORE);

        off = 0;
        T("잘린 헤더(2바이트) -> NEED_MORE, off 불변",
          tlv_next(stream, 2, &off, &t, 1024) == TLV_NEED_MORE && off == 0);

        off = 0;
        T("헤더는 왔지만 페이로드 부족 -> NEED_MORE, off 불변",
          tlv_next(stream, 4, &off, &t, 1024) == TLV_NEED_MORE && off == 0);

        const uint8_t evil[] = { 0x09, 0xFF, 0xFF, 0x00 };   // len=65535
        off = 0;
        T("악의적 길이(0xFFFF > max_len) -> BAD",
          tlv_next(evil, sizeof evil, &off, &t, 256) == TLV_BAD && off == 0);

        off = 0;
        T("max_len 경계: len==max_len 은 허용",
          tlv_next(stream, sizeof stream, &off, &t, 2) == TLV_OK && t.len == 2);

        off = 0;
        T("NULL / off>n 방어",
          tlv_next(NULL, 8, &off, &t, 16) == TLV_BAD &&
          tlv_next(stream, sizeof stream, NULL, &t, 16) == TLV_BAD);
    }

    printf("\n== Q58 SLIP 바이트 스터핑 ==\n");
    {
        uint8_t enc[64], dec[64];

        const uint8_t plain[] = { 'A', 'B', 'C' };
        long e = slip_encode(plain, sizeof plain, enc, sizeof enc);
        T("평범한 페이로드: END + 3바이트 + END = 5",
          e == 5 && enc[0] == SLIP_END && enc[4] == SLIP_END &&
          memcmp(enc + 1, plain, 3) == 0);

        const uint8_t tricky[] = { 0xC0, 0x01, 0xDB };
        e = slip_encode(tricky, sizeof tricky, enc, sizeof enc);
        const uint8_t want[] = { SLIP_END, SLIP_ESC, SLIP_ESC_END, 0x01,
                                 SLIP_ESC, SLIP_ESC_ESC, SLIP_END };
        T("END/ESC 가 정확히 이스케이프됨",
          e == 7 && memcmp(enc, want, sizeof want) == 0);

        long d = slip_decode(enc, (size_t)e, dec, sizeof dec);
        T("왕복: decode(encode(x)) == x",
          d == (long)sizeof tricky && memcmp(dec, tricky, sizeof tricky) == 0);

        uint8_t all256[256];
        for (int i = 0; i < 256; i++) all256[i] = (uint8_t)i;
        uint8_t big[600], back[300];
        e = slip_encode(all256, sizeof all256, big, sizeof big);
        d = slip_decode(big, (size_t)e, back, sizeof back);
        // 256바이트 중 0xC0, 0xDB 두 개가 2바이트로 늘고 앞뒤 END 2개 -> 260
        T("0x00~0xFF 전 바이트 왕복 (인코드 260바이트)",
          e == 260 && d == 256 && memcmp(back, all256, 256) == 0);

        const uint8_t leadend[] = { SLIP_END, SLIP_END, 'X', SLIP_END };
        d = slip_decode(leadend, sizeof leadend, dec, sizeof dec);
        T("선행 END(빈 프레임)는 건너뜀", d == 1 && dec[0] == 'X');

        const uint8_t badesc[]  = { SLIP_END, SLIP_ESC, 0x41, SLIP_END };
        const uint8_t trunc[]   = { SLIP_END, 'A', 'B' };
        const uint8_t dangling[] = { SLIP_END, 'A', SLIP_ESC };
        T("잘못된 이스케이프 / 잘린 프레임 / ESC로 끝남 -> -1",
          slip_decode(badesc, sizeof badesc, dec, sizeof dec) == -1 &&
          slip_decode(trunc, sizeof trunc, dec, sizeof dec) == -1 &&
          slip_decode(dangling, sizeof dangling, dec, sizeof dec) == -1);

        uint8_t tiny[3];
        T("출력 버퍼 부족 -> -1 (encode/decode 모두)",
          slip_encode(plain, sizeof plain, tiny, sizeof tiny) == -1 &&
          slip_decode(enc, 5, tiny, 0) == -1);

        e = slip_encode(NULL, 0, enc, sizeof enc);
        T("빈 페이로드 -> END END (2바이트)", e == 2);
    }

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
