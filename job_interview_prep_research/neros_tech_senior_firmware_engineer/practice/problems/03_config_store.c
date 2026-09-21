// 03_config_store.c  —  PRACTICE STUB (직접 채워넣기)
// 버전 있는 파라미터 저장소 (Versioned config store in flash)  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=03_config_store
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g 03_config_store.c -o /tmp/n03 && /tmp/n03
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// 왜 이게 Neros 에서 '플랫폼 인터페이스(platform interface)' 문제인가:
//  - 여러 팀·여러 기체(airframe)가 "같은" config store 를 쓴다. 한 팀이 파라미터를
//    추가했다고 다른 팀 빌드가 깨지면 안 된다 -> forward/backward compatibility.
//  - FW 는 계속 올라간다(OTA). 구버전이 쓴 이미지를 신버전이 읽고, 신버전이 쓴
//    이미지를 (롤백된) 구버전이 읽어도 죽지 않아야 한다.
//  - 전원은 언제든 끊긴다(배터리 분리, 브라운아웃). 쓰다 만 config 때문에 기체가
//    부팅 불능이 되면 그건 platform 팀 장애다 -> A/B 슬롯 + CRC + 원자적 커밋.
//  - 그래서 "구조체를 flash 에 통째로 fwrite" 대신 TLV(key-value) + size/version
//    헤더라는 '인터페이스'를 정의하고, 그 규칙을 문서화해서 고정시킨다.
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

// ===========================================================================
// 공용 유틸 (주어짐 — 직접 구현 대상 아님)
// ---------------------------------------------------------------------------
// little-endian 직렬화 helper. 구조체를 그대로 memcpy 하지 않고 바이트 단위로
// 읽고 쓰는 이유: padding/정렬/엔디안이 컴파일러·MCU 마다 다르기 때문.
// crc32 는 슬롯 무결성 검사용(표준 IEEE 802.3 다항식, 테이블 없는 bitwise 版).
// ===========================================================================
void wr_u16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}
void wr_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}
uint16_t rd_u16le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}
uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) {
        crc ^= (uint32_t)p[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return ~crc;
}

// TLV 레코드 포맷 (이 스토어의 '인터페이스' 정의):
//   [u16 id LE][u8 len][len 바이트 payload]  -> 헤더 3바이트 고정
#define TLV_HDR      3u
#define TLV_MAX_LEN  255u

// 예제용 파라미터 ID (실제로는 팀별 대역을 나눠 배정한다)
#define ID_BAUD  0x0101u   // u32
#define ID_MAC   0x0102u   // 6바이트 blob
#define ID_RATE  0x0103u   // u32

// "flash" 시뮬레이션: malloc 없이 정적 배열 (MCU 에는 힙이 없다고 생각)
static uint8_t g_flash[128];

/* ---------------------------------------------------------------------------
 * Q1.  TLV writer — u32 파라미터와 소형 blob 을 버퍼 끝에 append
 *   KO: off 위치에 [u16 id LE][u8 len][payload] 레코드를 이어붙이고 '새 off' 를
 *       돌려준다. 공간이 모자라면(또는 인자가 이상하면) 0 을 반환해 호출자가
 *       즉시 실패를 알 수 있게 한다. len 은 1바이트라 blob 은 최대 255.
 *   EN: Append a TLV record at `off` and return the new offset; 0 on overflow.
 *   ex: tlv_put_u32(buf, 128, 0, 0x0101, 115200) -> 7   (3 헤더 + 4 값)
 *       tlv_put_bytes(buf, 128, 7, 0x0102, mac, 6) -> 16
 *       tlv_put_u32(buf, 4, 0, 0x0101, 1) -> 0  (7바이트 필요, 용량 4)
 * ------------------------------------------------------------------------- */
size_t tlv_put_u32(uint8_t *buf, size_t cap, size_t off, uint16_t id, uint32_t val) {
    (void)buf; (void)cap; (void)off; (void)id; (void)val;
    // TODO: implement (용량 검사는 off+7 대신 cap-off 로 — 오버플로 회피)
    return 0;   // placeholder
}

size_t tlv_put_bytes(uint8_t *buf, size_t cap, size_t off, uint16_t id,
                     const uint8_t *val, size_t len) {
    (void)buf; (void)cap; (void)off; (void)id; (void)val; (void)len;
    // TODO: implement (len > 255 는 거부)
    return 0;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q2.  TLV reader — 레코드를 걸어가며 id 를 찾는다 (손상 내성)
 *   KO: 앞에서부터 레코드를 순회하다가 id 가 맞으면 payload 포인터/길이를 준다.
 *       핵심은 '잘린 마지막 레코드' 방어: 헤더 3바이트가 안 남았거나 선언된 len
 *       만큼 payload 가 남아있지 않으면 즉시 멈춘다(버퍼 밖을 절대 읽지 않는다).
 *       전원이 쓰기 도중 끊기면 실제로 이런 꼬리가 생긴다.
 *   EN: Walk TLV records; stop safely on a truncated/corrupt trailing record.
 *   ex: [BAUD=115200][MAC=6B][RATE=50] 에서 tlv_find(.., ID_MAC, ..) -> true, len 6
 *       꼬리에 "id,len=200" 만 있고 payload 가 2바이트뿐 -> 그 id 는 false,
 *       앞쪽 정상 레코드 검색은 여전히 true
 * ------------------------------------------------------------------------- */
bool tlv_find(const uint8_t *buf, size_t len, uint16_t id,
              const uint8_t **val_out, uint8_t *len_out) {
    (void)buf; (void)len; (void)id; (void)val_out; (void)len_out;
    // TODO: implement (레코드 순회 + 잘린 꼬리 방어: 버퍼 밖을 절대 읽지 말 것)
    return false;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q3.  타입 안전한 getter — 없거나 길이가 다르면 기본값
 *   KO: id 를 찾아 u32 로 돌려주되, (a) 없으면 fallback, (b) 있는데 길이가 4가
 *       아니면(다른 팀이 같은 id 를 다른 타입으로 썼거나 포맷이 바뀐 경우)
 *       역시 fallback. 절대 잘못된 길이를 u32 로 읽지 않는다.
 *       왜 모르는 id 를 '거부'하지 않고 '건너뛰나' -> forward compatibility:
 *       신버전 FW 가 추가한 파라미터가 섞여 있어도 구버전이 부팅에 성공해야 한다.
 *       모르는 것은 무시하고 보존(가능하면), 아는 것만 해석 = robustness principle.
 *   EN: Return the stored u32, or `fallback` when absent or length != 4.
 *       Unknown IDs are skipped, not rejected -> forward compatibility.
 *   ex: cfg_get_u32(buf, n, ID_BAUD, 9600) -> 115200
 *       cfg_get_u32(buf, n, 0x7FFF, 9600)  -> 9600   (없음)
 *       cfg_get_u32(buf, n, ID_MAC,  42)   -> 42     (길이 6 -> u32 아님)
 * ------------------------------------------------------------------------- */
uint32_t cfg_get_u32(const uint8_t *buf, size_t len, uint16_t id, uint32_t fallback) {
    (void)buf; (void)len; (void)id; (void)fallback;
    // TODO: implement (tlv_find -> 길이가 4일 때만 rd_u32le, 아니면 fallback)
    return 0;   // placeholder
}

// ===========================================================================
// Q4 : size + version 헤더가 붙은 고정 구조체 이미지
// ---------------------------------------------------------------------------
// 온-플래시 레이아웃(직렬화된 바이트, LE):
//   [u16 size][u16 version][u32 a]            -> v1 = 8바이트
//   [u16 size][u16 version][u32 a][u32 b]     -> v2 = 12바이트
//   [u16 size][u16 version][u32 a][u32 b][u32 c] -> v3 = 16바이트
// size 가 맨 앞인 이유: 버전을 몰라도 '이 레코드가 몇 바이트인지'는 항상 알 수 있다.
// ===========================================================================
typedef struct {
    uint16_t size;
    uint16_t version;
    uint32_t a;
    uint32_t b;
    uint32_t c;
} cfg_v3_t;

#define CFG_V1_LEN  8u
#define CFG_V2_LEN  12u
#define CFG_V3_LEN  16u
#define CFG_DEF_B   500u      // v1 이미지에는 없는 필드의 기본값
#define CFG_DEF_C   7u        // v1/v2 이미지에는 없는 필드의 기본값

/* ---------------------------------------------------------------------------
 * Q4.  버전 마이그레이션 — 구버전은 채워주고, 신버전은 잘라서 받는다
 *   KO: raw 이미지를 현재 코드가 아는 v3 구조체로 올린다.
 *       v1(a만)/v2(a,b) -> 없는 필드는 기본값으로 채운다 (backward compatibility).
 *       v4 이상(더 크고 모르는 필드가 뒤에 붙음) -> 앞의 a,b,c 만 읽고 꼬리는
 *       무시한다 (forward compatibility). 단 헤더의 size 가 실제 raw_len 과
 *       다르면 잘렸거나 깨진 것이므로 거부한다.
 *   EN: Upgrade a v1/v2 image by filling defaults, tolerate a newer/larger image
 *       by ignoring trailing unknown bytes, reject size != raw_len.
 *   ex: {size=8, ver=1, a=111}         -> a=111, b=500(기본), c=7(기본)
 *       {size=12,ver=2, a=111,b=222}   -> c=7(기본)
 *       {size=20,ver=4, a,b,c, +4바이트} -> a,b,c 만 사용, 꼬리 무시
 *       {size=16,ver=3} 인데 raw_len=12 -> false
 * ------------------------------------------------------------------------- */
bool cfg_upgrade(const uint8_t *raw, size_t raw_len, cfg_v3_t *out) {
    (void)raw; (void)raw_len; (void)out;
    // TODO: implement (size/version 파싱 -> size != raw_len 거부 -> 버전별 필드 채우기,
    //       ver > 3 이면 앞의 a,b,c 만 읽고 꼬리는 무시)
    return false;   // placeholder
}

// ===========================================================================
// Q5/Q6 : A/B 슬롯 (double-buffered config) + 원자적 커밋
// ---------------------------------------------------------------------------
// flash 는 '쓰는 도중' 전원이 끊기면 중간 상태로 남는다. 슬롯이 하나뿐이면 그
// 순간 마지막 정상 config 가 사라진다. 그래서 슬롯 두 개를 번갈아 쓰고
// (seq 로 최신 판별, crc 로 무결성 판별) 항상 '유효한 것 중 최신'을 고른다.
//   seq == 0 : 한 번도 기록되지 않은 슬롯 (지워진 상태)
//   crc      : seq + data 전체를 덮는다 -> seq 만 깨져도 잡힌다
// ===========================================================================
#define SLOT_DATA_N 32

typedef struct {
    uint32_t seq;                  // 커밋 순번 (클수록 최신)
    uint32_t crc;                  // seq + data 의 CRC32
    uint8_t  data[SLOT_DATA_N];    // 실제 config payload (TLV 든 구조체 이미지든)
} slot_t;

typedef struct {
    slot_t slot[2];                // [0] = A, [1] = B
} store_t;

// 주어짐: 슬롯의 기대 CRC 계산 (seq 를 LE 로 직렬화한 뒤 data 를 이어서 계산)
uint32_t slot_crc(const slot_t *s) {
    uint8_t hdr[4];
    wr_u32le(hdr, s->seq);
    uint32_t c = crc32_update(0u, hdr, sizeof hdr);
    return crc32_update(c, s->data, SLOT_DATA_N);
}

/* ---------------------------------------------------------------------------
 * Q5.  A/B 슬롯 선택 — 유효한 것 중 seq 가 큰 쪽
 *   KO: slot_valid() 는 '기록된 적 있고(seq!=0) CRC 가 맞는' 슬롯만 true.
 *       slot_select() 는 A=0 / B=1 / 둘 다 무효면 -1 을 반환한다.
 *       한쪽만 깨졌으면 자동으로 멀쩡한 쪽으로 fallback 되는 게 포인트.
 *   EN: Pick the valid slot with the higher seq; 0=A, 1=B, -1 if neither valid.
 *   ex: A(seq=4,ok), B(seq=7,ok) -> 1
 *       A(seq=4,ok), B(seq=7,CRC 깨짐) -> 0   (구버전이지만 유효)
 *       둘 다 CRC 깨짐 -> -1  (공장 기본값으로 부팅해야 하는 상황)
 * ------------------------------------------------------------------------- */
bool slot_valid(const slot_t *s) {
    (void)s;
    // TODO: implement (seq != 0 이고 crc == slot_crc(s) 이면 유효)
    return false;   // placeholder
}

int slot_select(const slot_t *a, const slot_t *b) {
    (void)a; (void)b;
    // TODO: implement (유효한 것 중 seq 큰 쪽; 동률이면 A; 둘 다 무효면 -1)
    return -1;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q6.  원자적 커밋 — 항상 '비활성' 슬롯에만 쓴다
 *   KO: 현재 active 슬롯을 고른 뒤 그 '반대편'에 seq = active_seq + 1 과 새 CRC 로
 *       기록한다. 쓰는 동안 active 슬롯은 손대지 않으므로, 도중에 전원이 나가도
 *       최악의 경우 '새 슬롯이 CRC 불일치' 일 뿐 직전 정상 config 는 그대로다.
 *       (실제 flash 라면: 대상 슬롯 erase -> data 기록 -> 마지막에 seq/crc 헤더를
 *        기록해서 '이제 유효' 를 커밋하는 순서. 헤더를 마지막에 쓰는 게 핵심.)
 *   EN: Write into the inactive slot with seq = active_seq + 1 and a fresh CRC,
 *       so a power loss mid-write can never destroy the last good config.
 *   ex: 빈 스토어 -> commit("CFG-A") 는 A 슬롯 seq=1
 *       -> commit("CFG-B") 는 B 슬롯 seq=2 (active 가 B 로 전환)
 *       -> commit("CFG-C") 중 전원 차단(A 손상) -> 여전히 B("CFG-B") 가 로드됨
 * ------------------------------------------------------------------------- */
bool cfg_commit(store_t *st, const uint8_t *data, size_t len) {
    (void)st; (void)data; (void)len;
    // TODO: implement (active 의 반대 슬롯에 seq = active_seq + 1 과 새 CRC 로 기록;
    //       active 가 없으면 슬롯 A 에 seq = 1)
    return false;   // placeholder
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    // -------- Q1. TLV writer --------
    printf("== Q1. TLV writer (put u32 / put bytes / overflow) ==\n");
    size_t off = 0;
    off = tlv_put_u32(g_flash, sizeof g_flash, off, ID_BAUD, 115200u);
    T("tlv_put_u32 appends 7 bytes (3 hdr + 4)", off == 7u);

    const uint8_t mac[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 };
    off = tlv_put_bytes(g_flash, sizeof g_flash, off, ID_MAC, mac, sizeof mac);
    T("tlv_put_bytes appends 9 bytes (3 hdr + 6)", off == 16u);

    uint8_t tiny[4];
    T("tlv_put_u32 overflow -> 0", tlv_put_u32(tiny, sizeof tiny, 0, ID_BAUD, 1u) == 0u);

    // -------- Q2. TLV reader --------
    printf("== Q2. TLV reader (round-trip / blob / truncated) ==\n");
    off = tlv_put_u32(g_flash, sizeof g_flash, off, ID_RATE, 50u);
    const uint8_t *vp = NULL;
    uint8_t vl = 0;
    T("round-trip: find ID_BAUD -> 115200",
      tlv_find(g_flash, off, ID_BAUD, &vp, &vl) && vl == 4u && rd_u32le(vp) == 115200u);
    T("find ID_MAC -> 6-byte blob matches",
      tlv_find(g_flash, off, ID_MAC, &vp, &vl) && vl == 6u && memcmp(vp, mac, 6) == 0);

    // 꼬리에 '헤더는 len=200 인데 payload 는 2바이트뿐' 인 잘린 레코드를 만든다
    g_flash[off + 0] = 0x55; g_flash[off + 1] = 0x00; g_flash[off + 2] = 200;
    g_flash[off + 3] = 0xAA; g_flash[off + 4] = 0xBB;
    size_t trunc_len = off + 5u;
    T("truncated trailing record: stop, no over-read",
      tlv_find(g_flash, trunc_len, 0x0055u, &vp, &vl) == false &&
      tlv_find(g_flash, trunc_len, ID_BAUD, &vp, &vl) == true);

    // -------- Q3. typed getter --------
    printf("== Q3. cfg_get_u32 (fallback on absent / wrong length) ==\n");
    T("cfg_get_u32 returns stored value", cfg_get_u32(g_flash, off, ID_BAUD, 9600u) == 115200u);
    T("cfg_get_u32 absent -> fallback",   cfg_get_u32(g_flash, off, 0x7FFFu, 9600u) == 9600u);
    T("cfg_get_u32 len!=4 -> fallback",   cfg_get_u32(g_flash, off, ID_MAC, 42u) == 42u);

    // -------- Q4. versioned struct migration --------
    printf("== Q4. cfg_upgrade (v1/v2 -> v3, newer image, size mismatch) ==\n");
    cfg_v3_t c3;
    uint8_t img1[CFG_V1_LEN];
    wr_u16le(img1, (uint16_t)CFG_V1_LEN); wr_u16le(img1 + 2, 1u); wr_u32le(img1 + 4, 111u);
    memset(&c3, 0, sizeof c3);
    T("v1 -> v3: a kept, b/c defaulted",
      cfg_upgrade(img1, sizeof img1, &c3) &&
      c3.a == 111u && c3.b == CFG_DEF_B && c3.c == CFG_DEF_C && c3.version == 3u);

    uint8_t img2[CFG_V2_LEN];
    wr_u16le(img2, (uint16_t)CFG_V2_LEN); wr_u16le(img2 + 2, 2u);
    wr_u32le(img2 + 4, 111u); wr_u32le(img2 + 8, 222u);
    memset(&c3, 0, sizeof c3);
    T("v2 -> v3: a,b kept, c defaulted",
      cfg_upgrade(img2, sizeof img2, &c3) &&
      c3.a == 111u && c3.b == 222u && c3.c == CFG_DEF_C);

    uint8_t img4[20];                       // 미래 버전: v3 뒤에 모르는 4바이트가 더 있음
    wr_u16le(img4, 20u); wr_u16le(img4 + 2, 4u);
    wr_u32le(img4 + 4, 111u); wr_u32le(img4 + 8, 222u); wr_u32le(img4 + 12, 333u);
    wr_u32le(img4 + 16, 0xDEADBEEFu);
    memset(&c3, 0, sizeof c3);
    T("newer (v4, larger) image: trailing bytes ignored",
      cfg_upgrade(img4, sizeof img4, &c3) &&
      c3.a == 111u && c3.b == 222u && c3.c == 333u && c3.version == 3u);

    uint8_t bad[CFG_V2_LEN];                // size 는 16 이라고 주장하는데 실제는 12
    memcpy(bad, img2, sizeof bad);
    wr_u16le(bad, (uint16_t)CFG_V3_LEN);
    T("declared size != raw_len -> reject", cfg_upgrade(bad, sizeof bad, &c3) == false);

    // -------- Q5. A/B slot selection --------
    printf("== Q5. slot_select (higher seq / one corrupt / both corrupt) ==\n");
    slot_t sa, sb;
    memset(&sa, 0, sizeof sa);
    memset(&sb, 0, sizeof sb);
    sa.seq = 4u; memcpy(sa.data, "alpha", 5); sa.crc = slot_crc(&sa);
    sb.seq = 7u; memcpy(sb.data, "bravo", 5); sb.crc = slot_crc(&sb);
    T("both valid -> pick higher seq (B)", slot_select(&sa, &sb) == 1);

    sb.data[0] ^= 0xFFu;                    // B 의 한 비트가 깨짐 -> CRC 불일치
    T("B corrupt -> fall back to A", slot_select(&sa, &sb) == 0);

    sa.crc ^= 0x1u;                         // 이제 A 도 깨짐
    T("both invalid -> -1 (use factory defaults)", slot_select(&sa, &sb) == -1);

    // -------- Q6. atomic commit + power-cut --------
    printf("== Q6. cfg_commit (inactive slot, power-cut survival) ==\n");
    store_t st;
    memset(&st, 0, sizeof st);              // 두 슬롯 모두 seq=0 (미기록)
    T("commit #1 -> slot A, seq 1",
      cfg_commit(&st, (const uint8_t *)"CFG-A", 5) &&
      st.slot[0].seq == 1u && slot_select(&st.slot[0], &st.slot[1]) == 0);

    cfg_commit(&st, (const uint8_t *)"CFG-B", 5);   // -> 비활성인 B 슬롯, seq 2
    cfg_commit(&st, (const uint8_t *)"CFG-C", 5);   // -> 다시 A 슬롯, seq 3
    st.slot[0].data[2] ^= 0xFFu;            // 쓰는 도중 전원 차단: A 가 깨진 채로 남음
    int act = slot_select(&st.slot[0], &st.slot[1]);
    T("power cut mid-write -> last good config (CFG-B) still loads",
      act == 1 && st.slot[1].seq == 2u && memcmp(st.slot[1].data, "CFG-B", 5) == 0);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
