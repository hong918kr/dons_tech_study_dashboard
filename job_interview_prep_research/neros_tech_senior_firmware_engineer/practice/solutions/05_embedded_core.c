// 05_embedded_core.c  —  REFERENCE SOLUTION
// 임베디드 기본기 복습 (레지스터 / 비트 / 엔디안 / 고정소수점 / 메모리 레이아웃)  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=05_embedded_core
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g solutions/05_embedded_core.c -o /tmp/n05 && /tmp/n05
//
// 왜 이 토픽인가 (01~04 와 성격이 다르다):
//   - 01~04 는 Neros 플랫폼 특화(logging/framing/config/IPC)였다면, 05 는 **기본기 복습 레이어**다.
//     회사를 가리지 않고 펌웨어 폰스크린에 나오는 단골 문제만 모았다 — 화려한 트릭 없음.
//   - 드론 펌웨어에서의 실제 등장 위치: 페리페럴 설정은 전부 **레지스터 RMW**(volatile + 임계구역),
//     IMU/바로미터/GNSS 는 대부분 **big-endian 레지스터 맵**이라 바이트 단위 디코드가 필수,
//     FPU 없는(or FPU 를 ISR 에서 못 쓰는) MCU 의 필터/스케일링은 **고정소수점 Q포맷**.
//   - 마지막 두 문제는 "메모리를 정확히 아는가" — strlcpy 계약, memmove 겹침, 구조체 패딩 vs 와이어 포맷.
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
// Q1 : 메모리 맵드 레지스터 접근 (set / clear / toggle / write_field)
// ---------------------------------------------------------------------------
// 왜 volatile 인가?
//   페리페럴 레지스터는 "메모리처럼 생겼지만 메모리가 아니다". 값이 CPU 모르게 바뀌고
//   (상태 플래그, RX 데이터), 읽/쓰기 자체가 부수효과다(read-to-clear, write-1-to-clear, FIFO pop).
//   volatile 이 없으면 컴파일러는 합법적으로:
//     - 같은 주소의 반복 읽기를 레지스터 캐싱으로 1회로 줄이고 (while(!(*SR & RXNE)) 가 무한루프)
//     - 값을 안 쓰는 읽기를 통째로 삭제하고 (read-to-clear 플래그가 안 지워짐)
//     - 연속된 store 를 마지막 하나로 합친다 (중간 시퀀스가 필요한 설정이 깨짐).
//   volatile 은 "이 접근을 지우지도, 합치지도, 서로 재배치하지도 말고 소스에 쓴 대로 하라"는
//   **컴파일러에게 하는 지시**다.
//
// volatile 이 보장하지 '않는' 것 (면접 단골):
//   - 원자성: *reg |= mask 는 load/or/store 3연산이다. 중간에 인터럽트가 끼면 잃어버린 갱신.
//   - CPU/버스 재정렬, 쓰기 버퍼 플러시: 멀티코어/AXI 에서는 DMB/DSB 가 따로 필요.
//   - 캐시 일관성: 레지스터 영역은 MPU 로 Device/Strongly-ordered 로 잡아줘야 한다.
//
// 그래서 RMW 는 공유 레지스터에서 레이스다:
//   태스크:  v = *GPIO_ODR;  v |= (1<<3);            <-- 여기서 인터럽트
//   ISR  :                      *GPIO_ODR |= (1<<7); (완료)
//   태스크:  *GPIO_ODR = v;                           <-- ISR 의 bit7 설정이 사라짐
//   해법: (1) 짧은 임계구역(__disable_irq/PRIMASK 저장·복원, RTOS 면 스핀락)
//         (2) 하드웨어가 주는 원자 경로 — BSRR(set/reset 전용 레지스터), bit-banding,
//             ARMv7-M 의 LDREX/STREX 루프
//         (3) 아예 소유자를 한 명으로 정한다(레지스터를 만지는 컨텍스트를 하나로).
//   아래 함수들은 "원자적이지 않다"는 사실을 호출자가 알고 쓰는 저수준 프리미티브다.
// ===========================================================================

/* ---------------------------------------------------------------------------
 * Q1.  레지스터 비트 set / clear / toggle / 필드 write (read-modify-write)
 *   KO: set 은 mask 비트만 1, clear 는 mask 비트만 0, toggle 은 mask 비트만 반전하고
 *       나머지 비트는 절대 건드리지 않는다. reg_write_field 는 value 를 필드 폭으로
 *       잘라 shift 위치에 넣되 mask 밖으로 새어나가지 않게 한다.
 *   EN: Read-modify-write helpers on a volatile MMIO register; only the masked
 *       bits change, and the field value is clamped to the field width.
 *   ex: reg=0xFFFFFFFF, mask=0x00000F00, shift=8, value=0x5 -> 0xFFFFF5FF
 *   note: *reg = *reg | mask 가 아니라 지역변수로 한 번 읽고 한 번 쓴다.
 *         volatile 접근 횟수를 코드에 명시하는 것이 MMIO 코드의 예의다.
 * ------------------------------------------------------------------------- */
void reg_set_bits(volatile uint32_t *reg, uint32_t mask) {
    if (!reg) return;
    uint32_t v = *reg;      // read  (volatile: 이 읽기는 반드시 일어난다)
    v |= mask;              // modify
    *reg = v;               // write (volatile: 이 쓰기도 반드시 일어난다)
}

void reg_clear_bits(volatile uint32_t *reg, uint32_t mask) {
    if (!reg) return;
    uint32_t v = *reg;
    v &= ~mask;
    *reg = v;
}

void reg_toggle_bits(volatile uint32_t *reg, uint32_t mask) {
    if (!reg) return;
    uint32_t v = *reg;
    v ^= mask;
    *reg = v;
}

void reg_write_field(volatile uint32_t *reg, uint32_t mask, uint32_t shift, uint32_t value) {
    if (!reg) return;
    // shift >= 32 는 << 가 UB 이므로 "필드 없음"으로 취급한다.
    uint32_t placed = (shift >= 32u) ? 0u : (value << shift);
    uint32_t v = *reg;           // 1) read
    v &= ~mask;                  // 2) 필드 자리를 비우고
    v |= (placed & mask);        // 3) mask 밖으로 새지 않도록 잘라 넣는다
    *reg = v;                    // 4) write  (전체가 원자적이지는 않다!)
}

// ===========================================================================
// Q2 : 비트 필드 추출/삽입 + 부호 확장
// ---------------------------------------------------------------------------
// 시프트 UB 3종 (C11 6.5.7):
//   1) 시프트량 >= 피연산자 폭   -> UB. x86 은 shift & 31 로 감고, ARM 은 0 을 내놓고,
//      컴파일타임 상수면 컴파일러가 아무 값이나 접어버린다. "테스트에선 됐는데"의 원흉.
//   2) 음수만큼 시프트           -> UB
//   3) 부호있는 값의 좌시프트 오버플로 -> UB (1 << 31 은 int 면 UB, 1u << 31 은 OK)
//   그래서 (1u << width) - 1 은 width==32 에서 터진다. 아래처럼 반드시 분기한다.
// ===========================================================================

// width 비트짜리 하한 마스크. width 0 -> 0, width >= 32 -> 0xFFFFFFFF (UB 회피).
uint32_t width_mask(uint32_t width) {
    if (width == 0u)  return 0u;
    if (width >= 32u) return 0xFFFFFFFFu;
    return (1u << width) - 1u;
}

/* ---------------------------------------------------------------------------
 * Q2.  비트 필드 extract / insert / 부호 확장
 *   KO: bits_extract 는 word 의 [shift, shift+width) 구간을 하위로 내려 반환.
 *       bits_insert 는 value 를 width 로 자른 뒤 그 구간에만 덮어쓴다(나머지 보존).
 *       sign_extend 는 width 비트 2의 보수 값을 int32_t 로 펼친다.
 *       width==32 / shift==32 에서도 UB 없이 동작해야 한다.
 *   EN: Extract/insert a bit field and sign-extend a narrow two's-complement
 *       value; width==32 must not trigger a 32-bit shift (UB).
 *   ex: 12비트 ADC 가 0xFFF 를 주면 -1, 0x800 이면 -2048, 0x7FF 이면 +2047
 *   note: 센서 레지스터는 12/14/18/20비트 2의 보수가 흔하다. 부호확장을 빠뜨리면
 *         "가속도계가 가끔 4000 으로 튄다" 같은 전형적 버그가 된다.
 * ------------------------------------------------------------------------- */
uint32_t bits_extract(uint32_t word, uint32_t shift, uint32_t width) {
    if (shift >= 32u) return 0u;                 // >> 32 는 UB -> 미리 차단
    return (word >> shift) & width_mask(width);
}

uint32_t bits_insert(uint32_t word, uint32_t shift, uint32_t width, uint32_t value) {
    if (shift >= 32u) return word;               // 넣을 자리가 없다
    uint32_t m  = width_mask(width);
    uint32_t fm = m << shift;                    // shift < 32 이므로 안전 (넘치는 비트는 버려짐)
    return (word & ~fm) | ((value & m) << shift);// value 를 width 로 먼저 자른다
}

int32_t sign_extend(uint32_t value, uint32_t width) {
    if (width == 0u)  return 0;
    if (width >= 32u) return (int32_t)value;     // 이미 32비트: 비트 패턴 그대로 재해석
    uint32_t m    = width_mask(width);
    uint32_t v    = value & m;                   // 폭 밖 쓰레기 제거
    uint32_t sign = 1u << (width - 1u);          // 부호 비트 (width <= 31 이라 안전)
    // (v ^ sign) - sign : 부호비트를 뺐다 더하는 고전 관용구. 오버플로 없이 정의된 연산.
    return (int32_t)(v ^ sign) - (int32_t)sign;
}

// ===========================================================================
// Q3 : 엔디안 변환과 정렬(alignment) 안전한 와이어 접근
// ---------------------------------------------------------------------------
// 왜 와이어 버퍼의 uint8_t* 를 uint32_t* 로 캐스팅하면 안 되는가 — 이유 3개:
//   1) 정렬(alignment): UART/SPI 링에서 온 페이로드는 임의 오프셋에서 시작한다.
//      Cortex-M0/M0+ 는 비정렬 LDR 자체가 불가능 -> HardFault (UsageFault 조차 못 감).
//      Cortex-M3/M4 는 LDR/STR 은 봐주지만 LDM/STM/LDRD 와 SCB->CCR.UNALIGN_TRP 에서 폴트,
//      게다가 봐주는 경우에도 버스 사이클이 쪼개져 **느리다**. Device 메모리면 무조건 폴트.
//   2) strict aliasing: uint8_t 배열을 uint32_t 로 읽는 것은 형식 규칙 위반(UB).
//      -O2 에서 컴파일러가 "이 두 포인터는 다른 타입이니 서로 안 겹친다"고 가정해
//      쓰기/읽기를 재배치한다 -> 최적화 빌드에서만 깨지는 버그.
//   3) 엔디안: 캐스팅은 **호스트 엔디안**으로 읽는다. 와이어 포맷은 프로토콜이 정한
//      엔디안이 있다(대부분 network byte order = big-endian, 센서는 제각각).
//      바이트 단위로 조립하면 코드가 엔디안을 '선언'하므로 이식성 문제가 사라진다.
//   결론: **와이어는 항상 uint8_t 배열로 다루고 시프트/OR 로 조립한다.**
//         컴파일러는 이 패턴을 알아보고 정렬이 보장된 곳에서는 rev/ldr 한 방으로 접는다.
// ===========================================================================

/* ---------------------------------------------------------------------------
 * Q3.  바이트 스왑 + 정렬 무관 big/little-endian 읽기·쓰기
 *   KO: bswap16/32 는 바이트 순서를 뒤집는다. read_be16/read_le32 는 임의 정렬의
 *       바이트 포인터에서 값을 조립하고, write_be16 은 반대로 분해해 기록한다.
 *       어디에서도 uint8_t 포인터를 uint16_t / uint32_t 포인터로 캐스팅하지 않는다.
 *   EN: Byte swaps plus alignment-agnostic wire accessors built from shifts and
 *       ORs — never a reinterpreting pointer cast.
 *   ex: {0x12,0x34} -BE-> 0x1234,  {0x78,0x56,0x34,0x12} -LE-> 0x12345678
 *   note: 홀수 오프셋(&buf[1])에서도 그대로 동작해야 한다 — 그게 이 방식의 목적.
 * ------------------------------------------------------------------------- */
uint16_t bswap16(uint16_t v) {
    return (uint16_t)(((v & 0x00FFu) << 8) | ((v & 0xFF00u) >> 8));
}

uint32_t bswap32(uint32_t v) {
    return ((v & 0x000000FFu) << 24)
         | ((v & 0x0000FF00u) <<  8)
         | ((v & 0x00FF0000u) >>  8)
         | ((v & 0xFF000000u) >> 24);
}

uint16_t read_be16(const uint8_t *p) {
    if (!p) return 0;
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);   // MSB first
}

uint32_t read_le32(const uint8_t *p) {
    if (!p) return 0;
    return  (uint32_t)p[0]
         | ((uint32_t)p[1] <<  8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);                               // LSB first
}

void write_be16(uint8_t *p, uint16_t v) {
    if (!p) return;
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

// ===========================================================================
// Q4 : 고정소수점 (Q15) — 부동소수점 없이
// ---------------------------------------------------------------------------
// 왜 MCU 에서 float 을 피하나?
//   - FPU 없는 코어(M0/M0+/M3)에서 float 은 소프트 라이브러리 호출 = 수십~수백 사이클 + 코드 크기.
//   - FPU 가 있어도 ISR 에서 쓰면 FPU 컨텍스트(S0-S31, FPSCR) 저장 때문에 지터가 커진다.
//     (lazy stacking 을 꺼야 하는 상황, RTOS 태스크마다 FPU 컨텍스트 추가 등)
//   - float 은 결정적(deterministic)이지 않게 느껴진다: 값 크기에 따라 정밀도가 달라져
//     적분기/필터의 재현성·테스트가 어렵다. 제어 루프는 **비트 단위로 재현 가능**해야 한다.
//
// Q15: int16_t 를 [-1.0, +1.0) 로 해석한다. 실수값 = raw / 32768.
//   0x4000 = +0.5, 0x8000 = -1.0, 0x7FFF = +0.999969...
//   곱셈: (a*b) 는 Q30 이 되므로 >>15 로 Q15 로 되돌린다. 중간값은 반드시 int32_t.
//   반올림: 그냥 >>15 는 항상 0 쪽이 아니라 **-무한대 쪽으로** 내려간다(산술 시프트).
//           필터에 누적되면 DC 오프셋(드리프트)이 생긴다. 그래서 +(1<<14) 후 시프트.
//   포화(saturation): -1.0 * -1.0 = +1.0 은 Q15 로 표현 불가 -> 0x7FFF 로 clamp.
//                     랩어라운드하면 최댓값이 최솟값으로 뒤집혀 제어기가 발산한다.
// ===========================================================================
#define Q15_MIN ((int32_t)(-32768))
#define Q15_MAX ((int32_t)( 32767))

/* ---------------------------------------------------------------------------
 * Q4.  Q15 곱셈(반올림+포화), Q15 포화 덧셈, ADC 카운트 -> 밀리볼트
 *   KO: q15_mul 은 int32 중간값으로 곱하고 (1<<14) 를 더한 뒤 >>15, 마지막에 포화.
 *       q15_sat_add 는 int32 로 더한 뒤 [-32768, 32767] 로 clamp.
 *       adc_to_mv 는 counts * vref_mv / full_scale 을 오버플로/부동소수점 없이,
 *       반올림까지 해서 계산한다.
 *   EN: Q15 multiply with round-to-nearest and saturation, saturating add, and
 *       an integer-only ADC-count to millivolt conversion.
 *   ex: 0.5 x 0.5 = 0.25 (16384 x 16384 -> 8192),  2048/4095 x 3300mV -> 1650mV
 *   note: 함수 어디에도 float/double 이 없다. 나눗셈 하나조차 정수 나눗셈이다.
 * ------------------------------------------------------------------------- */
int16_t q15_mul(int16_t a, int16_t b) {
    int32_t prod = (int32_t)a * (int32_t)b;      // Q15 * Q15 = Q30 (int16 곱은 반드시 확장!)
    prod = (prod + (1 << 14)) >> 15;             // 반올림 후 Q15 로 복귀
    if (prod > Q15_MAX) prod = Q15_MAX;          // -1.0 x -1.0 = +1.0 -> 표현 불가 -> 포화
    if (prod < Q15_MIN) prod = Q15_MIN;
    return (int16_t)prod;
}

int16_t q15_sat_add(int16_t a, int16_t b) {
    int32_t s = (int32_t)a + (int32_t)b;         // int16 끼리 더해도 int 로 승격되지만 의도를 명시
    if (s > Q15_MAX) s = Q15_MAX;
    if (s < Q15_MIN) s = Q15_MIN;
    return (int16_t)s;
}

int32_t adc_to_mv(uint16_t counts, uint32_t vref_mv, uint32_t full_scale) {
    if (full_scale == 0u) return 0;              // 0 나눗셈 방어
    // counts * vref_mv 는 uint32 에서 아슬아슬하게 넘칠 수 있다(65535*65535 + 반올림항).
    // 64비트 중간값이 가장 단순하고 안전하다. 64비트 곱이 비싼 코어라면
    //   (counts/fs)*vref + ((counts%fs)*vref + fs/2)/fs
    // 로 쪼개면 32비트만으로도 같은 결과를 얻는다.
    uint64_t num = (uint64_t)counts * (uint64_t)vref_mv + (uint64_t)(full_scale / 2u);
    return (int32_t)(num / (uint64_t)full_scale);   // +fs/2 덕에 반올림 (버림 아님)
}

// ===========================================================================
// Q5 : 경계 있는 문자열/버퍼 복사
// ---------------------------------------------------------------------------
// strncpy 의 함정 (면접 단골):
//   - src 가 n 이상이면 **NUL 을 안 붙인다** -> 이후 strlen/printf 가 버퍼 밖으로 달린다.
//   - src 가 짧으면 남는 자리를 전부 0 으로 채운다 -> 큰 버퍼에서 쓸데없이 느리다.
//   - 반환값이 dst 라 잘렸는지 알 수 없다.
//   strlcpy 계약(BSD): cap>0 이면 **항상** NUL 종료, 반환값은 "원래 썼어야 할 길이"(=strlen(src)).
//   따라서 `if (safe_copy(d, cap, s) >= cap) { 잘림 처리 }` 로 절단을 **감지**할 수 있다.
//   이 "감지 가능성"이 핵심이다 — 조용한 절단은 설정값/경로/ID 를 망가뜨린다.
//
// memcpy vs memmove:
//   memcpy 는 "영역이 겹치지 않는다"가 **계약**이다. 겹치면 UB — 방향을 정하지 않았으므로
//   구현(SIMD, 역방향, 블록 단위)에 따라 결과가 달라진다. memmove 는 겹침을 허용하고
//   "src 를 먼저 다 읽은 것과 같은 결과"를 보장한다. 구현은 방향 선택: dst < src 면 앞에서부터,
//   dst > src 면 뒤에서부터. 아래 naive_forward_copy 로 그 차이를 실제로 증명한다.
// ===========================================================================

/* ---------------------------------------------------------------------------
 * Q5.  strlcpy 의미의 안전 복사 + 겹침을 처리하는 memmove
 *   KO: safe_copy 는 dst_cap>0 이면 항상 NUL 종료하고, 절대 dst_cap 을 넘지 않으며,
 *       "잘리지 않았다면 썼을 길이"(strlen(src))를 반환한다 -> 절단 감지 가능.
 *       memmove_impl 은 겹침 방향을 보고 복사 방향을 골라 원본 손상을 막는다.
 *       naive_forward_copy 는 일부러 항상 앞에서부터 복사하는 '틀린' 버전이다.
 *   EN: strlcpy semantics (always NUL-terminates, returns would-be length) plus a
 *       correct overlap-safe memmove, contrasted with a naive forward copy.
 *   ex: safe_copy(d,4,"hello") -> d=="hel", 반환 5 (5 >= 4 이므로 잘림)
 *   note: 겹치는 영역에서 naive_forward_copy 는 자기가 방금 쓴 바이트를 다시 읽어
 *         원본을 파괴한다. 아래 테스트가 그 깨진 결과를 정확히 예측해 보여준다.
 * ------------------------------------------------------------------------- */
size_t safe_copy(char *dst, size_t dst_cap, const char *src) {
    if (!src) return 0;
    size_t srclen = 0;
    while (src[srclen] != '\0') srclen++;        // 먼저 진짜 길이를 잰다 (반환값용)
    if (dst && dst_cap > 0u) {
        size_t n = (srclen < dst_cap - 1u) ? srclen : dst_cap - 1u;  // 마지막 1칸은 NUL 몫
        for (size_t i = 0; i < n; ++i) dst[i] = src[i];
        dst[n] = '\0';                           // cap>0 이면 무조건 종료 (strncpy 와의 차이)
    }
    return srclen;                               // 반환 >= dst_cap  <=>  잘렸다
}

void *memmove_impl(void *dst, const void *src, size_t n) {
    if (!dst || !src || n == 0u) return dst;
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s) return dst;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];              // 앞 -> 뒤
    } else {
        for (size_t i = n; i > 0u; --i) d[i - 1u] = s[i - 1u];   // 뒤 -> 앞 (겹침 보호)
    }
    return dst;
}

// 일부러 틀린 버전: 방향을 따지지 않고 항상 앞에서부터 복사한다 (memcpy 스타일).
void *naive_forward_copy(void *dst, const void *src, size_t n) {
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

// ===========================================================================
// Q6 : 메모리 안의 구조체 vs 와이어 위의 바이트
// ---------------------------------------------------------------------------
// 컴파일러는 각 멤버를 자기 정렬 경계에 놓기 위해 **패딩**을 끼워넣는다. 아래 구조체는
// (일반적인 32/64비트 ABI 기준)
//     id(1) flags(1) [pad 2] t_us(4) ax(2) ay(2) az(2) volt_mv(2)  = 16 바이트
// 인데 와이어 포맷은 1+1+4+2+2+2+2 = 14 바이트다. 그래서
//     memcpy(out, &pkt, sizeof pkt)  <-- 절대 금지
// 이유: (1) 패딩 2바이트가 같이 나가 길이가 틀어지고, (2) 패딩 내용은 **불확정값**이라
// 프로토콜에 쓰레기(잠재적 정보 유출)가 실리고, (3) 호스트/타깃 엔디안이 다르면 값이 뒤집히고,
// (4) 컴파일러/옵션/ABI 가 바뀌면 조용히 레이아웃이 달라진다.
//
// __attribute__((packed)) 로 때우면 되지 않나? -> 되지만 대가가 크다:
//   - 비표준(컴파일러 확장), 그리고 packed 멤버의 **주소를 잡으면 비정렬 포인터**가 된다.
//     그 포인터를 넘겨받은 코드는 정렬을 가정하고 LDR 을 쓰다가 Cortex-M0 에서 HardFault.
//   - ARM 에서 packed 필드 접근은 바이트 단위 코드로 풀려 오히려 느릴 수 있다.
//   - 엔디안 문제는 여전히 해결되지 않는다.
//   정답은 언제나 **필드 단위 명시적 직렬화**다. 구조체는 메모리 표현, 와이어는 별도 계약.
// ===========================================================================
typedef struct {
    uint8_t  id;        // 센서 ID
    uint8_t  flags;     // 상태 비트
    uint32_t t_us;      // 타임스탬프 (us)
    int16_t  ax, ay, az;// 가속도 (Q15 or raw LSB)
    uint16_t volt_mv;   // 배터리 전압 (mV)
} sensor_pkt_t;

// 와이어 크기 = 필드 크기의 단순 합. 구조체 sizeof 와는 무관하다.
#define SENSOR_PKT_WIRE 14u

_Static_assert(SENSOR_PKT_WIRE == 1u + 1u + 4u + 2u + 2u + 2u + 2u,
               "와이어 크기 상수가 필드 합과 불일치");
_Static_assert(sizeof(sensor_pkt_t) >= SENSOR_PKT_WIRE,
               "구조체가 와이어보다 작을 수는 없다 (패딩은 늘리기만 한다)");

/* ---------------------------------------------------------------------------
 * Q6.  sensor_pkt_t <-> 14바이트 little-endian 와이어 포맷
 *   KO: pkt_serialize 는 cap 이 부족하면 한 바이트도 안 쓰고 0, 성공하면 14 반환.
 *       pkt_deserialize 는 len 이 정확히 14 일 때만 true (잘린/초과 입력 거부).
 *       모든 필드를 바이트 단위로 쓰고 읽는다 — memcpy(struct) 금지.
 *   EN: Explicit field-by-field little-endian (de)serialization; struct padding
 *       never reaches the wire and wire bytes never alias the struct.
 *   ex: id=0x2A,flags=0x81,t_us=0x01020304 -> 2A 81 04 03 02 01 ...
 *   note: int16 필드는 uint16 로 조립한 뒤 Q2 의 sign_extend 로 되돌린다
 *         (uint16 -> int16 직접 캐스팅은 구현정의 동작).
 * ------------------------------------------------------------------------- */
size_t pkt_serialize(uint8_t *out, size_t cap, const sensor_pkt_t *in) {
    if (!out || !in) return 0;
    if (cap < SENSOR_PKT_WIRE) return 0;         // 부분 기록 금지

    out[0]  = in->id;
    out[1]  = in->flags;
    out[2]  = (uint8_t)( in->t_us        & 0xFFu);   // u32 LE
    out[3]  = (uint8_t)((in->t_us >>  8) & 0xFFu);
    out[4]  = (uint8_t)((in->t_us >> 16) & 0xFFu);
    out[5]  = (uint8_t)((in->t_us >> 24) & 0xFFu);

    uint16_t ax = (uint16_t)in->ax;              // int16 -> uint16 는 항상 정의됨(모듈로 2^16)
    uint16_t ay = (uint16_t)in->ay;
    uint16_t az = (uint16_t)in->az;
    out[6]  = (uint8_t)( ax       & 0xFFu);
    out[7]  = (uint8_t)((ax >> 8) & 0xFFu);
    out[8]  = (uint8_t)( ay       & 0xFFu);
    out[9]  = (uint8_t)((ay >> 8) & 0xFFu);
    out[10] = (uint8_t)( az       & 0xFFu);
    out[11] = (uint8_t)((az >> 8) & 0xFFu);
    out[12] = (uint8_t)( in->volt_mv       & 0xFFu);
    out[13] = (uint8_t)((in->volt_mv >> 8) & 0xFFu);
    return SENSOR_PKT_WIRE;
}

bool pkt_deserialize(const uint8_t *in, size_t len, sensor_pkt_t *out) {
    if (!in || !out) return false;
    if (len != SENSOR_PKT_WIRE) return false;    // 링크에서 온 길이는 먼저 검증

    out->id      = in[0];
    out->flags   = in[1];
    out->t_us    =  (uint32_t)in[2]
                 | ((uint32_t)in[3] <<  8)
                 | ((uint32_t)in[4] << 16)
                 | ((uint32_t)in[5] << 24);
    uint32_t rax = (uint32_t)in[6]  | ((uint32_t)in[7]  << 8);
    uint32_t ray = (uint32_t)in[8]  | ((uint32_t)in[9]  << 8);
    uint32_t raz = (uint32_t)in[10] | ((uint32_t)in[11] << 8);
    out->ax      = (int16_t)sign_extend(rax, 16);   // 16비트 2의 보수 -> 부호 복원
    out->ay      = (int16_t)sign_extend(ray, 16);
    out->az      = (int16_t)sign_extend(raz, 16);
    out->volt_mv = (uint16_t)((uint32_t)in[12] | ((uint32_t)in[13] << 8));
    return true;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    // -------- Q1 : 레지스터 RMW --------
    printf("== Q1. MMIO register set / clear / toggle / write_field ==\n");
    volatile uint32_t reg = 0x00000000u;
    reg_set_bits(&reg, 0x0000000Fu);
    bool set_ok = (reg == 0x0000000Fu);
    reg_set_bits(&reg, 0x0000000Fu);                 // 멱등: 이미 1인 비트는 그대로
    set_ok &= (reg == 0x0000000Fu);
    reg_clear_bits(&reg, 0x00000003u);
    set_ok &= (reg == 0x0000000Cu);
    T("Q1 set/clear: mask 비트만 바뀌고 나머지는 보존 (멱등)", set_ok);

    reg = 0x0000000Cu;
    reg_toggle_bits(&reg, 0x000000FFu);
    T("Q1 toggle: 0x0C ^ 0xFF == 0xF3", reg == 0x000000F3u);

    reg = 0xFFFFFFFFu;
    reg_write_field(&reg, 0x00000F00u, 8, 0x5u);     // 4비트 필드에 5
    bool fld_ok = (reg == 0xFFFFF5FFu);
    reg = 0xFFFFFFFFu;
    reg_write_field(&reg, 0x00000F00u, 8, 0x1Fu);    // 5비트 값 -> 4비트로 잘려야 한다
    fld_ok &= (reg == 0xFFFFFFFFu);                  // 0x1F & 0xF == 0xF -> 필드는 원래대로
    reg = 0x12345678u;
    reg_write_field(&reg, 0x00FF0000u, 16, 0xABu);
    fld_ok &= (reg == 0x12AB5678u);                  // 필드 밖 바이트는 그대로
    T("Q1 write_field: value 를 필드 폭으로 자르고 다른 비트는 불변", fld_ok);

    // -------- Q2 : 비트 필드 + 부호 확장 --------
    printf("== Q2. bit field extract / insert / sign extend ==\n");
    bool ext_ok = (bits_extract(0xDEADBEEFu, 4, 8) == 0xEEu);
    ext_ok &= (bits_extract(0xDEADBEEFu, 0, 32) == 0xDEADBEEFu);  // width 32 (1u<<32 UB 회피)
    ext_ok &= (bits_extract(0xDEADBEEFu, 28, 8) == 0xDu);         // 폭이 넘쳐도 상위는 0
    ext_ok &= (bits_extract(0xDEADBEEFu, 32, 4) == 0u);           // shift 32 (>>32 UB 회피)
    T("Q2 bits_extract: width==32 / shift==32 에서도 UB 없이 정확", ext_ok);

    bool ins_ok = (bits_insert(0x00000000u, 8, 4, 0xFu) == 0x00000F00u);
    ins_ok &= (bits_insert(0xFFFFFFFFu, 8, 4, 0x0u) == 0xFFFFF0FFu);
    ins_ok &= (bits_insert(0x00000000u, 4, 4, 0x1Fu) == 0x000000F0u); // value 를 폭으로 자름
    ins_ok &= (bits_insert(0x12345678u, 0, 32, 0xAABBCCDDu) == 0xAABBCCDDu);
    T("Q2 bits_insert: value 절단 + 필드 밖 비트 보존", ins_ok);

    bool sx_ok = (sign_extend(0xFFFu, 12) == -1);
    sx_ok &= (sign_extend(0x800u, 12) == -2048);     // 12비트 ADC 최솟값
    sx_ok &= (sign_extend(0x7FFu, 12) == 2047);      // 12비트 ADC 최댓값
    sx_ok &= (sign_extend(0x000u, 12) == 0);
    sx_ok &= (sign_extend(0xFFFFFFFFu, 32) == -1);   // width 32 경로
    sx_ok &= (sign_extend(0x0080u, 8) == -128);
    T("Q2 sign_extend: 12비트 ADC 2의 보수 -> int32 (width 32 포함)", sx_ok);

    // -------- Q3 : 엔디안 / 비정렬 접근 --------
    printf("== Q3. endianness + unaligned wire access ==\n");
    bool sw_ok = (bswap16(0x1234u) == 0x3412u);
    sw_ok &= (bswap32(0x12345678u) == 0x78563412u);
    sw_ok &= (bswap16(bswap16(0xBEEFu)) == 0xBEEFu);        // 왕복
    sw_ok &= (bswap32(bswap32(0xDEADBEEFu)) == 0xDEADBEEFu);
    T("Q3 bswap16/32: 바이트 순서 반전 + 왕복 항등", sw_ok);

    // 일부러 홀수 오프셋에서 시작 -> 포인터 캐스팅이었다면 Cortex-M0 에서 HardFault
    uint8_t wire[12] = {0xAA, 0x12, 0x34, 0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0, 0};
    bool rd_ok = (read_be16(&wire[1]) == 0x1234u);          // BE: 12 34
    rd_ok &= (read_le32(&wire[3]) == 0x12345678u);          // LE: 78 56 34 12
    rd_ok &= (read_be16(&wire[3]) == 0x7856u);              // 같은 바이트, 다른 해석
    T("Q3 read_be16/read_le32: 홀수 오프셋(비정렬)에서도 정확", rd_ok);

    write_be16(&wire[7], 0xCAFEu);
    bool wr_ok = (wire[7] == 0xCAu && wire[8] == 0xFEu);    // MSB 먼저
    wr_ok &= (read_be16(&wire[7]) == 0xCAFEu);              // 왕복
    wr_ok &= (wire[6] == 0x12u && wire[9] == 0x00u);        // 이웃 바이트 불변
    T("Q3 write_be16: MSB first 기록 + 이웃 바이트 불변", wr_ok);

    // -------- Q4 : 고정소수점 --------
    printf("== Q4. fixed-point Q15 math (no floating point) ==\n");
    bool mul_ok = (q15_mul(16384, 16384) == 8192);          // 0.5 x 0.5 = 0.25
    mul_ok &= (q15_mul(-16384, 16384) == -8192);            // -0.5 x 0.5 = -0.25
    mul_ok &= (q15_mul(32767, 32767) == 32766);             // ~1.0 x ~1.0
    mul_ok &= (q15_mul(0, 12345) == 0);
    T("Q4 q15_mul: Q15 x Q15 -> Q15 (int32 중간값)", mul_ok);

    // 1 x 0.5 = 0.5 LSB.  단순 >>15 는 0 으로 버리지만 반올림하면 1 이 되어야 한다.
    bool rnd_ok = (q15_mul(1, 16384) == 1);
    rnd_ok &= ((int32_t)(((int32_t)1 * 16384) >> 15) == 0); // 반올림 없는 버전은 0 (대조군)
    rnd_ok &= (q15_mul(-32768, -32768) == 32767);           // -1.0 x -1.0 = +1.0 -> 포화
    T("Q4 q15_mul: 반올림(+1<<14) 과 +1.0 포화", rnd_ok);

    bool sat_ok = (q15_sat_add(100, -50) == 50);
    sat_ok &= (q15_sat_add(30000, 10000) == 32767);         // 랩어라운드 대신 포화
    sat_ok &= (q15_sat_add(-30000, -10000) == -32768);
    sat_ok &= (adc_to_mv(2048, 3300, 4095) == 1650);        // 12비트 ADC 중간값
    sat_ok &= (adc_to_mv(4095, 3300, 4095) == 3300);        // 풀스케일
    sat_ok &= (adc_to_mv(0, 3300, 4095) == 0);
    sat_ok &= (adc_to_mv(1, 3300, 4095) == 1);              // 0.806mV -> 반올림하면 1
    sat_ok &= (adc_to_mv(65535, 3300, 65535) == 3300);      // 오버플로 없이
    sat_ok &= (adc_to_mv(1234, 3300, 0) == 0);              // 0 나눗셈 방어
    T("Q4 q15_sat_add 포화 + adc_to_mv 정수 스케일링/반올림", sat_ok);

    // -------- Q5 : 경계 있는 복사 --------
    printf("== Q5. bounded copy: strlcpy semantics + memmove ==\n");
    char dst[8];
    memset(dst, 'X', sizeof dst);
    bool sc_ok = (safe_copy(dst, sizeof dst, "hello") == 5);
    sc_ok &= (strcmp(dst, "hello") == 0);
    sc_ok &= (dst[6] == 'X');                               // cap 밖은 건드리지 않았다
    memset(dst, 'X', sizeof dst);
    sc_ok &= (safe_copy(dst, 6, "hello") == 5);             // 딱 맞음 (5 + NUL)
    sc_ok &= (strcmp(dst, "hello") == 0 && dst[6] == 'X');

    memset(dst, 'X', sizeof dst);
    size_t need = safe_copy(dst, 4, "hello");               // 절단 케이스
    sc_ok &= (need == 5);                                   // "썼어야 할 길이" 를 반환
    sc_ok &= (need >= 4);                                   // >= cap  =>  잘림 감지 가능
    sc_ok &= (strcmp(dst, "hel") == 0);                     // 3글자 + NUL (strncpy 와 달리 종료됨)
    sc_ok &= (dst[4] == 'X');                               // cap 밖 오염 없음
    char guard[4] = {'A', 'B', 'C', 'D'};
    sc_ok &= (safe_copy(guard, 0, "hello") == 5);           // cap==0 -> 아무것도 안 쓴다
    sc_ok &= (guard[0] == 'A' && guard[3] == 'D');
    T("Q5 safe_copy: 항상 NUL 종료 + would-be 길이 반환(절단 감지) + cap==0 안전", sc_ok);

    char ovl[11] = "0123456789";                            // [0..9] + NUL
    memmove_impl(&ovl[2], &ovl[0], 5);                      // 앞 5바이트를 2칸 뒤로
    bool mv_ok = (strcmp(ovl, "0101234789") == 0);
    char ovl2[11] = "0123456789";
    memmove_impl(&ovl2[0], &ovl2[3], 5);                    // 반대 방향 (dst < src)
    mv_ok &= (strcmp(ovl2, "3456756789") == 0);
    T("Q5 memmove_impl: 양방향 겹침에서 원본 손상 없음", mv_ok);

    char bad[11] = "0123456789";
    naive_forward_copy(&bad[2], &bad[0], 5);                // memcpy 스타일 = 항상 앞에서부터
    // 방금 쓴 바이트를 다시 읽어 '01' 이 반복 전파된다 -> 0101010789
    T("Q5 naive forward copy 는 겹침에서 데이터를 파괴한다 (memmove 가 필요한 이유)",
      strcmp(bad, "0101010789") == 0 && strcmp(bad, "0101234789") != 0);

    // -------- Q6 : 구조체 레이아웃 vs 와이어 --------
    printf("== Q6. struct padding vs wire layout ==\n");
    printf("     sizeof(sensor_pkt_t) = %zu, wire = %u\n",
           sizeof(sensor_pkt_t), (unsigned)SENSOR_PKT_WIRE);
    T("Q6 sizeof(struct) > 와이어 크기 (내부 패딩) -> memcpy 직렬화 금지",
      sizeof(sensor_pkt_t) > (size_t)SENSOR_PKT_WIRE);

    sensor_pkt_t p = { .id = 0x2A, .flags = 0x81, .t_us = 0x01020304u,
                       .ax = -1000, .ay = 250, .az = -32768, .volt_mv = 11100 };
    uint8_t buf[20];
    memset(buf, 0xCC, sizeof buf);
    size_t n6 = pkt_serialize(buf, sizeof buf, &p);
    bool ser_ok = (n6 == SENSOR_PKT_WIRE);
    ser_ok &= (buf[0] == 0x2A && buf[1] == 0x81);
    ser_ok &= (buf[2] == 0x04 && buf[3] == 0x03 && buf[4] == 0x02 && buf[5] == 0x01);
    ser_ok &= (buf[6] == 0x18 && buf[7] == 0xFC);           // -1000 = 0xFC18, LE
    ser_ok &= (buf[8] == 0xFA && buf[9] == 0x00);           // 250
    ser_ok &= (buf[10] == 0x00 && buf[11] == 0x80);         // -32768 = 0x8000
    ser_ok &= (buf[14] == 0xCC);                            // 14바이트만 썼다 (패딩 안 나감)
    ser_ok &= (pkt_serialize(buf, SENSOR_PKT_WIRE - 1u, &p) == 0);  // cap 부족 -> 0
    T("Q6 pkt_serialize: 14바이트 LE 필드 배치, cap 부족이면 0바이트", ser_ok);

    sensor_pkt_t q;
    memset(&q, 0, sizeof q);
    bool de_ok = pkt_deserialize(buf, n6, &q);
    de_ok &= (q.id == p.id && q.flags == p.flags && q.t_us == p.t_us);
    de_ok &= (q.ax == -1000 && q.ay == 250 && q.az == -32768); // 부호 복원
    de_ok &= (q.volt_mv == 11100);
    de_ok &= !pkt_deserialize(buf, SENSOR_PKT_WIRE - 1u, &q);  // 잘림 거부
    de_ok &= !pkt_deserialize(buf, SENSOR_PKT_WIRE + 1u, &q);  // 초과 거부
    T("Q6 pkt_deserialize: 왕복 복원(음수 포함) + 길이 불일치 거부", de_ok);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
