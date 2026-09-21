// 06_net_parsing.c  —  REFERENCE SOLUTION
// 네트워크 · 프로토콜 파싱 (Networking & Protocol Parsing)  —  Q49~Q58
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra -O1 -g -pthread solutions/06_net_parsing.c -o /tmp/vk06 && /tmp/vk06
//
// Verkada Connectivity 팀의 "하루 일과" 그 자체다. GC31-E 셀룰러 게이트웨이는
// LTE 모뎀과 AT 커맨드로 대화하고, 듀얼 SIM failover를 신호 품질(RSSI/RSRP)로 판단하며,
// 링크 위로 프레임을 실어 나르고 체크섬으로 무결성을 지킨다. 실제 후기에서 보고된
// 코딩 문제도 "Extract IP Addresses"였고, 라운드 하나는 "제품 연결시키고 트러블슈팅"이었다.
// 전부 소켓 없이 "버퍼 위의 순수 함수"로 쪼갤 수 있다 — 그래서 테스트 가능하다.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// 공용 소도구 — ctype.h 는 locale/부호 확장 이슈가 있어 직접 쓴다.
static bool is_dig(char c)   { return c >= '0' && c <= '9'; }
static bool is_alnum_c(char c) {
    return is_dig(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// ===========================================================================
// Q49. IPv4 문자열 -> uint32_t (엄격 파서)
// ---------------------------------------------------------------------------
// "10.0.0.7" -> 0x0A000007 (host order 정수, 상위 바이트 = 첫 옥텟).
// 엄격하다는 것은 다음을 전부 거부한다는 뜻이다:
//   선행 0("01.2.3.4"), 범위 초과("256.1.1.1"), 점 개수 오류("1.2.3" / "1.2.3.4.5"),
//   빈 옥텟("1..2.3"), 공백/꼬리 쓰레기(" 1.2.3.4" / "1.2.3.4x").
// 왜 엄격해야 하나: 선행 0을 허용하는 구현(inet_aton 계열)은 8진수로 해석해
// "010.1.1.1" 을 10.1.1.1 이 아닌 8.1.1.1 로 만든다 — SSRF/ACL 우회의 고전 취약점.
// ===========================================================================
#define IPV4_STR_MAX 16   // "255.255.255.255" + '\0'

bool ipv4_parse(const char *s, uint32_t *out) {
    if (!s || !out) return false;

    uint32_t ip = 0;
    int octets = 0;
    const char *p = s;

    for (;;) {
        if (!is_dig(*p)) return false;            // 빈 옥텟 / 비숫자 / 선행 공백 거부
        if (*p == '0' && is_dig(p[1])) return false;  // 선행 0 거부 ("0" 자체는 허용)

        unsigned v = 0;
        int digits = 0;
        while (is_dig(*p)) {
            v = v * 10u + (unsigned)(*p - '0');
            if (++digits > 3 || v > 255u) return false;   // 범위 초과 즉시 탈출
            p++;
        }
        ip = (ip << 8) | v;
        if (++octets > 4) return false;

        if (*p == '.') {
            if (octets == 4) return false;        // "1.2.3.4.5"
            p++;
            continue;
        }
        break;
    }
    if (octets != 4 || *p != '\0') return false;  // "1.2.3", "1.2.3.4x"
    *out = ip;
    return true;
}

// ===========================================================================
// Q50. uint32_t -> IPv4 문자열 (버퍼 크기 안전)
// ---------------------------------------------------------------------------
// 성공 시 기록한 문자 수(널 제외)를, 버퍼 부족/NULL 이면 -1 을 반환한다.
// 핵심: (1) 먼저 로컬 tmp 에 만들고 크기를 확인한 뒤 복사 — 부분 기록 금지.
//       (2) 실패해도 out 은 항상 널 종료시켜 호출자가 쓰레기를 읽지 않게 한다.
// GC31-E 의 진단 로그·Command 업링크 JSON 이 이 함수를 초당 수십 번 호출한다.
// ===========================================================================
static size_t u8_to_dec(uint8_t v, char *dst) {   // dst 최소 3바이트, 널 미기록
    size_t k = 0;
    if (v >= 100) dst[k++] = (char)('0' + v / 100);
    if (v >= 10)  dst[k++] = (char)('0' + (v / 10) % 10);
    dst[k++] = (char)('0' + v % 10);
    return k;
}

int ipv4_format(uint32_t ip, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return -1;

    char tmp[IPV4_STR_MAX];
    size_t k = 0;
    for (int i = 3; i >= 0; i--) {
        k += u8_to_dec((uint8_t)((ip >> (i * 8)) & 0xFFu), tmp + k);
        if (i) tmp[k++] = '.';
    }
    tmp[k] = '\0';

    if (k + 1 > out_sz) { out[0] = '\0'; return -1; }   // 잘라 쓰지 않는다
    memcpy(out, tmp, k + 1);
    return (int)k;
}

// ===========================================================================
// Q51. CIDR 매칭 — "10.0.0.0/8" 안에 특정 IP가 들어가는가
// ---------------------------------------------------------------------------
// mask = 상위 prefix 비트만 1. 함정은 prefix==0 — C에서 32비트 값을 32비트
// 시프트하는 것은 **정의되지 않은 동작(UB)** 이라 반드시 특수 처리해야 한다.
// Verkada 맥락: 게이트웨이가 LAN(사설 대역)과 WAN(CGNAT 100.64/10, 공인)을
// 구분하고, 카메라를 어느 서브넷으로 내보낼지 라우팅 판단할 때 그대로 쓰인다.
// ===========================================================================
uint32_t cidr_mask(unsigned prefix) {
    if (prefix == 0)  return 0u;                  // 0xFFFFFFFF << 32 는 UB
    if (prefix >= 32) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (32u - prefix);
}

// "a.b.c.d/p" 를 파싱. net_out 에는 마스크가 적용된 네트워크 주소를 넣는다.
bool cidr_parse(const char *s, uint32_t *net_out, unsigned *prefix_out) {
    if (!s || !net_out || !prefix_out) return false;

    const char *slash = strchr(s, '/');
    if (!slash) return false;                     // prefix 는 필수

    size_t iplen = (size_t)(slash - s);
    if (iplen == 0 || iplen >= IPV4_STR_MAX) return false;

    char ipbuf[IPV4_STR_MAX];
    memcpy(ipbuf, s, iplen);
    ipbuf[iplen] = '\0';

    uint32_t ip;
    if (!ipv4_parse(ipbuf, &ip)) return false;    // 옥텟 규칙은 Q49 를 재사용

    const char *q = slash + 1;
    if (!is_dig(*q)) return false;                // "10.0.0.0/" 거부
    if (*q == '0' && is_dig(q[1])) return false;  // "/08" 거부
    unsigned pfx = 0;
    int digits = 0;
    while (is_dig(*q)) {
        pfx = pfx * 10u + (unsigned)(*q - '0');
        if (++digits > 2 || pfx > 32u) return false;
        q++;
    }
    if (*q != '\0') return false;                 // 꼬리 쓰레기 거부

    *prefix_out = pfx;
    *net_out    = ip & cidr_mask(pfx);
    return true;
}

bool cidr_contains(const char *cidr, uint32_t ip) {
    uint32_t net;
    unsigned pfx;
    if (!cidr_parse(cidr, &net, &pfx)) return false;
    uint32_t m = cidr_mask(pfx);
    return (ip & m) == net;
}

// ===========================================================================
// Q52. 로그 한 줄에서 IPv4 주소 모두 추출  ★ 실제 보고된 면접 문제 유형
// ---------------------------------------------------------------------------
// 반환값 = 찾은 총 개수(호출자가 잘림 여부를 알 수 있게). 실제로 out 에 쓰는 것은
// 최대 max_out 개. 경계 규칙:
//   - [0-9.] 덩어리를 통째로 잘라 Q49 의 엄격 파서에 넘긴다 -> "1.2.3.4.5" 자동 탈락.
//   - 덩어리 양옆이 영숫자면 거부 -> "v1.2.3.4", "1.2.3.4a" 같은 버전 문자열 오탐 차단.
//   - 뒤가 ':' '/' ',' 공백 등 구분자면 허용 -> "192.168.0.1:8080", "10.0.0.0/8" OK.
// ===========================================================================
size_t ipv4_extract_all(const char *line, uint32_t *out, size_t max_out) {
    if (!line) return 0;

    size_t found = 0;
    size_t i = 0;
    while (line[i] != '\0') {
        if (!is_dig(line[i])) { i++; continue; }

        size_t j = i;
        while (line[j] != '\0' && (is_dig(line[j]) || line[j] == '.')) j++;

        bool left_ok  = (i == 0) || !is_alnum_c(line[i - 1]);
        bool right_ok = (line[j] == '\0') || !is_alnum_c(line[j]);
        size_t len    = j - i;

        if (left_ok && right_ok && len < IPV4_STR_MAX) {
            char tok[IPV4_STR_MAX];
            memcpy(tok, line + i, len);
            tok[len] = '\0';
            uint32_t ip;
            if (ipv4_parse(tok, &ip)) {
                if (out && found < max_out) out[found] = ip;
                found++;
            }
        }
        i = j;   // 덩어리 전체를 소비 — 안쪽에서 다시 시작하지 않는다
    }
    return found;
}

// ===========================================================================
// Q53. 인터넷 체크섬 (RFC 1071) — 16비트 1의 보수 합의 1의 보수
// ---------------------------------------------------------------------------
// 절차: 버퍼를 빅엔디안 16비트 워드로 더하고, 넘친 캐리를 하위로 접어 넣고(fold),
//       마지막에 비트 반전. 홀수 길이면 마지막 바이트를 상위 바이트로 패딩.
// 두 가지 성질을 반드시 말할 수 있어야 한다:
//   (1) 엔디안 독립 — 바이트를 직접 (p[0]<<8)|p[1] 로 조립하므로 호스트 엔디안 무관.
//   (2) 검증 성질 — 체크섬 필드에 결과를 넣고 다시 계산하면 0 이 나온다.
// IP/UDP/ICMP 헤더가 이 알고리즘을 쓴다. 약점: 자리바꿈(transposition)을 못 잡는다.
// ===========================================================================
uint16_t inet_checksum(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | (uint32_t)p[1];
        p   += 2;
        len -= 2;
    }
    if (len == 1) sum += (uint32_t)p[0] << 8;     // 홀수 바이트는 상위로 패딩

    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);   // 캐리 fold
    return (uint16_t)(~sum & 0xFFFFu);
}

// 체크섬을 버퍼 끝에 빅엔디안으로 붙일 때 쓰는 헬퍼(검증 성질 테스트용).
static void be16_put(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v >> 8);
    dst[1] = (uint8_t)(v & 0xFFu);
}

// ===========================================================================
// Q54. hex dump 유틸 — "offset | hex | ascii" 한 줄
// ---------------------------------------------------------------------------
// 형식(= hexdump -C 호환):
//   "00000010  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  |................|"
//    ^8자리 오프셋  ^2칸  ^8바이트     ^2칸  ^8바이트            ^2칸  ^ASCII
// len<16 이면 hex 칸은 공백으로 패딩하고 ASCII 는 있는 만큼만 찍는다.
// 필드 디버깅 단골 — 모뎀이 뱉은 이상한 바이트를 눈으로 보는 유일한 방법이다.
// ===========================================================================
#define HEXDUMP_BYTES_PER_LINE 16
#define HEXDUMP_LINE_MAX       80   // 60(고정폭) + 1 + 16 + 1 + 1(널)

int hexdump_line(char *out, size_t out_sz,
                 size_t offset, const uint8_t *data, size_t len) {
    if (!out || out_sz == 0) return -1;
    if (len > HEXDUMP_BYTES_PER_LINE) { out[0] = '\0'; return -1; }
    if (len > 0 && !data)             { out[0] = '\0'; return -1; }

    static const char HEX[] = "0123456789abcdef";
    char tmp[HEXDUMP_LINE_MAX];
    size_t k = 0;

    for (int sh = 28; sh >= 0; sh -= 4)           // 오프셋 8자리 (하위 32비트)
        tmp[k++] = HEX[(offset >> sh) & 0xFu];
    tmp[k++] = ' ';
    tmp[k++] = ' ';

    for (size_t c = 0; c < HEXDUMP_BYTES_PER_LINE; c++) {
        if (c < len) {
            tmp[k++] = HEX[(data[c] >> 4) & 0xFu];
            tmp[k++] = HEX[data[c] & 0xFu];
        } else {
            tmp[k++] = ' ';
            tmp[k++] = ' ';
        }
        tmp[k++] = ' ';
        if (c == 7) tmp[k++] = ' ';               // 8바이트마다 한 칸 더
    }
    tmp[k++] = ' ';                               // hex 영역과 ASCII 영역 구분

    tmp[k++] = '|';
    for (size_t c = 0; c < len; c++) {
        uint8_t b = data[c];
        tmp[k++] = (b >= 0x20 && b <= 0x7E) ? (char)b : '.';
    }
    tmp[k++] = '|';
    tmp[k]   = '\0';

    if (k + 1 > out_sz) { out[0] = '\0'; return -1; }
    memcpy(out, tmp, k + 1);
    return (int)k;
}

// ===========================================================================
// Q55. AT 커맨드 응답 파서 FSM — 스트림 재조립 + 분류
// ---------------------------------------------------------------------------
// 모뎀은 UART로 바이트를 흘려보낼 뿐, "줄" 단위로 주지 않는다. read()가
// "+CSQ: 2" / "3,99\r\nOK\r" / "\n" 처럼 아무데서나 잘려 들어와도 동작해야 한다.
// 그래서 파서는 **호출 사이에 상태를 들고 있는 FSM**이어야 한다.
//   - '\r' 또는 '\n' 이 줄 종결자. 빈 줄(연속 CRLF)은 버린다.
//   - 줄이 버퍼보다 길면 AT_KIND_OVERFLOW 로 보고하고 **다음 종결자까지 버린 뒤 복구**.
//   - 분류: "OK" / "ERROR"·"+CME ERROR:"·"+CMS ERROR:" / '+'로 시작 = URC / 그 외 = INFO
// 반환값 = 이번 feed 로 완성된 줄의 총 개수(out 에는 최대 max_out 개만 기록).
// ===========================================================================
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

void at_init(at_parser_t *p) {
    if (!p) return;
    p->len = 0;
    p->overflow = false;
    p->buf[0] = '\0';
}

static bool starts_with(const char *s, const char *pre) {
    return strncmp(s, pre, strlen(pre)) == 0;
}

static at_kind_t at_classify(const char *line) {
    if (strcmp(line, "OK") == 0)    return AT_KIND_OK;
    if (strcmp(line, "ERROR") == 0) return AT_KIND_ERROR;
    if (starts_with(line, "+CME ERROR") || starts_with(line, "+CMS ERROR"))
        return AT_KIND_ERROR;
    if (line[0] == '+') return AT_KIND_URC;
    return AT_KIND_INFO;
}

// 완성된 줄 하나를 방출. 기록 여부와 무관하게 개수는 센다.
static void at_emit(at_parser_t *p, at_line_t *out, size_t max_out, size_t *count) {
    if (p->len == 0 && !p->overflow) return;      // 빈 줄은 무시 (CRLF 연속)

    p->buf[p->len] = '\0';
    if (out && *count < max_out) {
        at_line_t *L = &out[*count];
        L->kind = p->overflow ? AT_KIND_OVERFLOW : at_classify(p->buf);
        memcpy(L->text, p->buf, p->len + 1);
    }
    (*count)++;
    p->len = 0;
    p->overflow = false;                          // 종결자를 봤으니 복구
}

size_t at_feed(at_parser_t *p, const char *chunk, size_t n,
               at_line_t *out, size_t max_out) {
    if (!p || (!chunk && n > 0)) return 0;

    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        char c = chunk[i];
        if (c == '\r' || c == '\n') {
            at_emit(p, out, max_out, &count);
            continue;
        }
        if (p->len + 1 >= AT_LINE_MAX) {          // 널 자리 확보
            p->overflow = true;                   // 이후 바이트는 버리고 종결자만 기다림
            continue;
        }
        if (!p->overflow) p->buf[p->len++] = c;
    }
    return count;
}

// ===========================================================================
// Q56. 신호 품질 변환 — AT+CSQ RSSI -> dBm, RSRP/RSRQ 클램프 + 등급
// ---------------------------------------------------------------------------
// +CSQ 의 첫 값은 dBm 이 아니라 0~31 인덱스다. 매핑: dBm = -113 + 2*rssi
//   0 -> -113dBm(최악), 31 -> -51dBm(최상), 99 -> "측정 불가"(안테나 미연결/검색중).
// LTE 는 RSSI 대신 RSRP(-140~-44 dBm)/RSRQ(-20~-3 dB)를 본다.
// GC31-E 듀얼 SIM failover 판단이 정확히 이 등급 위에서 돈다 — 그래서 히스테리시스와
// "unknown 은 poor 가 아니다"를 구분하는 게 중요하다(99를 -113으로 오해 → 무한 스왑).
// ===========================================================================
typedef enum {
    SIG_UNKNOWN = 0,
    SIG_POOR,
    SIG_FAIR,
    SIG_GOOD,
    SIG_EXCELLENT
} sig_grade_t;

int sig_clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// "+CSQ: 23,99" -> rssi=23, ber=99. 공백 허용, 형식이 틀리면 false.
bool at_parse_csq(const char *line, int *rssi_out, int *ber_out) {
    if (!line || !rssi_out || !ber_out) return false;
    if (!starts_with(line, "+CSQ:")) return false;

    const char *p = line + 5;
    int vals[2];
    for (int f = 0; f < 2; f++) {
        while (*p == ' ') p++;
        if (!is_dig(*p)) return false;
        int v = 0, digits = 0;
        while (is_dig(*p)) {
            v = v * 10 + (*p - '0');
            if (++digits > 3) return false;       // 3자리 초과는 스펙 위반
            p++;
        }
        vals[f] = v;
        if (f == 0) {
            while (*p == ' ') p++;
            if (*p != ',') return false;
            p++;
        }
    }
    while (*p == ' ') p++;
    if (*p != '\0') return false;                 // 꼬리 쓰레기 거부

    *rssi_out = vals[0];
    *ber_out  = vals[1];
    return true;
}

// 0~31 -> dBm. 99(unknown) 및 범위 밖은 false (dbm_out 미변경).
bool csq_to_dbm(int csq, int *dbm_out) {
    if (!dbm_out) return false;
    if (csq < 0 || csq > 31) return false;        // 99 포함 — "모름"은 값이 아니다
    *dbm_out = -113 + 2 * csq;
    return true;
}

sig_grade_t csq_grade(int csq) {
    int dbm;
    if (!csq_to_dbm(csq, &dbm)) return SIG_UNKNOWN;
    if (dbm >= -70)  return SIG_EXCELLENT;
    if (dbm >= -85)  return SIG_GOOD;
    if (dbm >= -100) return SIG_FAIR;
    return SIG_POOR;
}

// RSRP: 유효 범위 [-140, -44] dBm 로 클램프 후 등급.
sig_grade_t rsrp_grade(int rsrp_dbm) {
    int v = sig_clamp(rsrp_dbm, -140, -44);
    if (v >= -80)  return SIG_EXCELLENT;
    if (v >= -90)  return SIG_GOOD;
    if (v >= -100) return SIG_FAIR;
    return SIG_POOR;
}

// RSRQ: 유효 범위 [-20, -3] dB 로 클램프 후 등급.
sig_grade_t rsrq_grade(int rsrq_db) {
    int v = sig_clamp(rsrq_db, -20, -3);
    if (v >= -10) return SIG_EXCELLENT;
    if (v >= -15) return SIG_GOOD;
    if (v >= -18) return SIG_FAIR;
    return SIG_POOR;
}

// ===========================================================================
// Q57. 길이-접두(TLV) 프레이밍 디코더 — 부분 버퍼 / 잘린 헤더 / 악의적 길이
// ---------------------------------------------------------------------------
// 프레임: [type:1][len:2 big-endian][payload:len]
// 스트림 디코더의 3대 계약:
//   (1) 데이터가 모자라면 TLV_NEED_MORE 를 주고 **오프셋을 전진시키지 않는다**
//       (호출자가 남은 바이트를 보관했다가 다음 read 와 이어붙일 수 있어야 하므로).
//   (2) len 이 정책 상한(max_len)을 넘으면 TLV_BAD — 공격자가 0xFFFF 를 보내
//       수십 KB 할당/대기를 유발하는 것을 막는다.
//   (3) off + 3 + len 계산에서 **절대 오버플로가 나면 안 된다** — 덧셈 대신 뺄셈으로 비교.
// ===========================================================================
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

tlv_status_t tlv_next(const uint8_t *buf, size_t n, size_t *off,
                      tlv_t *out, uint16_t max_len) {
    if (!buf || !off || !out) return TLV_BAD;
    if (*off > n) return TLV_BAD;

    size_t avail = n - *off;                      // 뺄셈 — 오버플로 없음
    if (avail < TLV_HDR_LEN) return TLV_NEED_MORE;    // 헤더조차 안 왔다

    const uint8_t *h = buf + *off;
    uint8_t  type = h[0];
    uint16_t len  = (uint16_t)(((uint16_t)h[1] << 8) | (uint16_t)h[2]);  // BE

    if (len > max_len) return TLV_BAD;            // 악의적/손상된 길이 방어

    if (avail - TLV_HDR_LEN < (size_t)len) return TLV_NEED_MORE;  // 페이로드 미도착

    out->type = type;
    out->len  = len;
    out->val  = (len > 0) ? (h + TLV_HDR_LEN) : NULL;
    *off += TLV_HDR_LEN + (size_t)len;
    return TLV_OK;
}

// ===========================================================================
// Q58. SLIP 바이트 스터핑 (RFC 1055) 인코드 / 디코드
// ---------------------------------------------------------------------------
//   END     = 0xC0   프레임 경계
//   ESC     = 0xDB   이스케이프 도입
//   ESC_END = 0xDC   "원래 0xC0 이었음"
//   ESC_ESC = 0xDD   "원래 0xDB 이었음"
// 인코더는 앞뒤로 END 를 붙인다(앞쪽 END = 회선 노이즈로 생긴 쓰레기를 빈 프레임으로
// 흘려보내 재동기화시키는 장치). 최악의 경우 출력 크기는 2*n + 2.
// 길이 접두와 달리 **길이를 미리 알 필요가 없다** — 대신 크기가 최대 2배로 늘 수 있다.
// 반환값: 기록한 바이트 수, 실패 시 -1.
// ===========================================================================
#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

long slip_encode(const uint8_t *in, size_t n, uint8_t *out, size_t out_sz) {
    if ((!in && n > 0) || !out) return -1;

    size_t k = 0;
    #define PUT(b) do { if (k >= out_sz) return -1; out[k++] = (uint8_t)(b); } while (0)
    PUT(SLIP_END);
    for (size_t i = 0; i < n; i++) {
        if (in[i] == SLIP_END)      { PUT(SLIP_ESC); PUT(SLIP_ESC_END); }
        else if (in[i] == SLIP_ESC) { PUT(SLIP_ESC); PUT(SLIP_ESC_ESC); }
        else                        { PUT(in[i]); }
    }
    PUT(SLIP_END);
    #undef PUT
    return (long)k;
}

// in 은 END 로 감싸진 프레임 하나. 선행 END 들은 건너뛰고, 종결 END 에서 멈춘다.
// 잘못된 이스케이프 시퀀스(ESC 뒤에 ESC_END/ESC_ESC 가 아님, 또는 ESC 로 끝남) -> -1.
// 종결 END 가 없으면(잘린 프레임) -> -1.
long slip_decode(const uint8_t *in, size_t n, uint8_t *out, size_t out_sz) {
    if ((!in && n > 0) || !out) return -1;

    size_t i = 0;
    while (i < n && in[i] == SLIP_END) i++;       // 선행 END / 빈 프레임 스킵

    size_t k = 0;
    while (i < n) {
        uint8_t b = in[i++];
        if (b == SLIP_END) {
            return (long)k;                       // 정상 종료
        }
        if (b == SLIP_ESC) {
            if (i >= n) return -1;                // ESC 로 끝남 = 잘린 프레임
            uint8_t e = in[i++];
            if      (e == SLIP_ESC_END) b = SLIP_END;
            else if (e == SLIP_ESC_ESC) b = SLIP_ESC;
            else    return -1;                    // 프로토콜 위반
        }
        if (k >= out_sz) return -1;               // 목적지 버퍼 부족
        out[k++] = b;
    }
    return -1;                                    // 종결 END 미도착
}

// ===========================================================================
// main — 테스트
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
