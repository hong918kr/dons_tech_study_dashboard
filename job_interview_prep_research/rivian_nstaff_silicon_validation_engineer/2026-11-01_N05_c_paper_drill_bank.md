# N05 · C 코딩 온사이트 뱅크 — 종이에 20분씩 푸는 74문제
<!-- theme: pastel -->

> **왜**: 온사이트 Technical 1 = **C 코딩 / 임베디드, 로직 중심** [추정, 스크린 형식 기준] — 스크린 실제 질문은 "니블 순서 뒤집기" [확인됨 10-30] · **형식**: 문제마다 **종이 20분** (clarify 2 → 접근 3 → 코드 10 → trace 3 → 테스트·확장 2) · **구성**: 9개 페이지(A–I) × 7–9문제 = **74문제**, 모범 답은 전부 컴파일·실행 검증 (`onsitePrep/n05/<a–i>/run.sh` — 11-01 전 페이지 ALL PASS) · **재사용**: 이미 한 것 = [C 정리 C1–C10](2026-10-29_c_basics_cram.html) · [N02 니블 회고](2026-10-31_N02_screen_coding_retro.html) · [hmScreenPrep V1–V12](hmScreenPrep/index.html) — 여기선 **겹치지 않는 변형** 위주 · **원본**: [온사이트 플랜 N05](2026-10-31_rivian_onsite_plan.html) · **작성 규칙**: onsitePrep/N05_NOTES_SPEC.md

## 0. TL;DR

- **9개 페이지**: A 비트 기본 · B 비트 응용·엔디안·패킹 · C 레지스터·MMIO 드라이버 · D 버퍼·문자열·메모리 · E 링버퍼·큐·ISR 동시성 · F 메모리 테스트·무결성(CRC/ECC) · G 상태 머신·파서 · H 코드 읽기·C 함정(출력 맞히기·버그 찾기) · I 검증형 C(타임스탬프·통계·shmoo·고정소수점).
- 각 페이지 **§1 = 인쇄용 문제지**(답 없음) → 출력해서 종이에 풀고 → 문제별 `<details>`(힌트 1 → 힌트 2 → 모범 답 → trace 표 → 테스트 → 꼬리 질문)로 채점.
- **★★★ 먼저 할 15문제** (온사이트 확률 높음): A1 · A6 · A8 · B1 · B3 · C1 · C2 · C5 · D1 · D4 · E1 · E3 · F2 · G2 · I1.
- **20분 루틴은 모든 문제 공통** (§1) — 문제를 많이 푸는 것보다 **매번 같은 순서로 말하는 습관**이 스크린 회고(N02)의 핵심 교훈.
- 하루 할당(§3): 평일 **3문제 = 60분**, 주말 **모의 세트 2개**(45분에 2문제 + 꼬리 질문).

## 1. 20분 종이 루틴 (모든 문제 공통 — 외우기)

| 분 | 할 일 | 소리 내서 할 말 (English) |
|---|---|---|
| 0–2 | **Clarify**: 단위(bit/nibble/byte), 입력 형태(정수/배열/포인터+길이), 폭(32/64), 에러 처리, in-place 여부, 하드웨어 동작(W1C? FIFO?) | "Before I start — is the input an integer or a byte buffer? What width? Should I handle errors or can I assume valid input?" |
| 2–5 | **예시 1개를 손으로** 풀고, 접근 한 문장 + 복잡도 | "Let me do the example by hand first… so the idea is X, which is O(n)." |
| 5–15 | **코드**: 시그니처 → 엣지 체크 → 본체. `uint32_t`/`1u`/`volatile`/timeout | "I'll use fixed-width types and unsigned shifts to avoid UB." |
| 15–18 | **Trace 표** 2–3 스텝 (단위 바꾸지 않기 — N02 교훈) | "Let me trace it on the example to check." |
| 18–20 | **테스트 4개 + 확장 1개** 먼저 제안 | "I'd test zero, all-ones, the max boundary, and a round-trip. If we want it faster I can do the O(log n) version." |

- 종이에 쓸 때 맨 위 3줄은 항상: `#include <stdint.h>` · 함수 시그니처 · 오른쪽 위에 **테스트 박스**(케이스 4칸).
- 채점 공통 체크: ① 정답 ② 엣지(0·max·빈 입력) ③ UB 없음(부호 시프트·폭 이상 시프트·오버플로) ④ 이름·주석 명확 ⑤ trace 정확 ⑥ 확장 제안.

## 2. 페이지 · 문제 목록

### A. 비트 기본 (9문제)

📄 [N05A 열기](2026-11-01_N05A_bit_basics.html) · 코드 `onsitePrep/n05/a/`

| ID | 문제 | 난이도 | 핵심 |
|---|---|---|---|
| **A1** | 32-bit 비트 순서 뒤집기 (루프 → mask-swap) | ★★★ | `r = (r<<1) \| (x&1)`, 5단계 swap |
| A2 | popcount 3가지 + parity | ★★ | `x &= x-1`, XOR fold |
| A3 | power of 2 판별 · 다음 2의 거듭제곱 · align up/down | ★★ | `x && !(x&(x-1))`, `(x+a-1) & ~(a-1)` |
| A4 | 최상위/최하위 set bit 위치 · clz (builtin 없이) | ★★ | 이진 탐색, `x & -x` |
| A5 | 32-bit rotate left/right (n=0, n≥32 처리) | ★ | `(x<<n) \| (x>>((32-n)&31))` |
| **A6** | N-bit 2의 보수 필드 sign-extend (12-bit ADC 값) | ★★★ | `(x ^ m) - m` |
| A7 | 홀짝 비트 swap · 바이트 안 니블 swap | ★ | `0x55555555`, `0x0F0F0F0F` |
| **A8** | 임의 (pos, width) 필드 추출/삽입 — width=32 포함 | ★★★ | mask 만들 때 `1u<<32` UB 피하기 |
| A9 | n번째 비트 set/clear/toggle/test 매크로 + 연속 1 개수 최대값 | ★ | `x & (x<<1)` 반복 |

### B. 비트 응용 · 엔디안 · 패킹 (8문제)

📄 [N05B 열기](2026-11-01_N05B_bits_endian_packing.html) · 코드 `onsitePrep/n05/b/`

| ID | 문제 | 난이도 | 핵심 |
|---|---|---|---|
| **B1** | bswap16/32/64 직접 구현 + "이 머신 엔디안?" 판별 | ★★★ | shift+mask, union/char* 검사 |
| B2 | 바이트 배열 ↔ 정수 (LE/BE, 정렬 안 된 주소에서도 안전) | ★★ | shift로 조립, 캐스트 금지 |
| **B3** | 센서 패킷 구조체 → wire 포맷 직렬화/역직렬화 (패딩·엔디안 무관) | ★★★ | put_be16/put_le32 헬퍼 |
| B4 | 비트맵 할당기: 첫 번째 0 비트 찾아 할당/해제 (uint32_t 배열) | ★★ | `~word != 0`, ctz |
| B5 | Gray code ↔ binary 변환 (인코더·FIFO 포인터) | ★★ | `g = b ^ (b>>1)`, prefix XOR |
| B6 | 비트 스트림 writer/reader: 임의 비트 수 쓰기·읽기 (MSB-first) | ★★ | 누적 레지스터 + 남은 비트 수 |
| B7 | 두 레지스터 값 비교 → 바뀐 비트 목록 출력 (`old ^ new`) | ★ | XOR + 비트 순회 |
| B8 | Morton(Z-order) 비트 interleave 16+16 → 32 | ★★ | spread bits 마스크 4단계 |

### C. 레지스터 · MMIO 드라이버 (8문제)

📄 [N05C 열기](2026-11-01_N05C_register_mmio_driver.html) · 코드 `onsitePrep/n05/c/`

| ID | 문제 | 난이도 | 핵심 |
|---|---|---|---|
| **C1** | status 비트 polling + timeout (32-bit tick wrap-around 안전) | ★★★ | `(now - start) >= timeout` unsigned |
| **C2** | RMW 헬퍼 + W1C 인터럽트 status 처리 (왜 W1C에 RMW 하면 안 되나) | ★★★ | `reg = pending` 만 쓰기 |
| C3 | 레지스터 블록 struct overlay (reserved 패딩, offset static_assert) | ★★ | `offsetof`, `volatile` 멤버 |
| C4 | 64-bit free-running counter: latch 방식 vs hi-lo-hi, 경과 시간 계산 | ★★ | C 정리 C4의 변형 |
| **C5** | ISR: status 읽기 → set된 비트마다 handler 테이블 호출 → ack | ★★★ | 함수 포인터 배열, ctz 루프 |
| C6 | UART: putc(TX FIFO full polling) · 논블로킹 getc · print_hex32 (printf 없이) | ★★ | 니블→문자 |
| C7 | SPI full-duplex transfer: TX FIFO 넣고 RX FIFO 빼기 (FIFO depth 고려) | ★★ | in-flight 카운트 |
| C8 | 테이블 기반 레지스터 sanity 테스트: reset 값 + RW 마스크 walking-1 (RO/W1C 건너뛰기) | ★★ | `{off, reset, rw_mask, type}` |

### D. 버퍼 · 문자열 · 메모리 (8문제)

📄 [N05D 열기](2026-11-01_N05D_buffers_strings_memory.html) · 코드 `onsitePrep/n05/d/`

| ID | 문제 | 난이도 | 핵심 |
|---|---|---|---|
| **D1** | memmove (겹침 방향) + 워드 단위 최적화 이야기 | ★★★ | `d < s` 앞→뒤, 아니면 뒤→앞 |
| D2 | 두 버퍼 비교: 첫 불일치 offset · 불일치 바이트 수 · **비트 에러 수(BER)** | ★★ | `popcount(a^b)` |
| D3 | hexdump 포맷터 (offset · 16바이트 hex · ASCII) — snprintf 없이 | ★★ | 니블→문자, 마지막 줄 패딩 |
| **D4** | "0x1A2b" / "deadbeef" → uint32 파싱, 에러·오버플로 검출 | ★★★ | `v > (UINT32_MAX >> 4)` |
| D5 | 배열 k칸 회전 (3-reverse) · in-place 중복 제거 | ★★ | reverse(0,k) reverse(k,n) reverse(0,n) |
| D6 | 고정 크기 블록 pool allocator (free list를 블록 안에) | ★★ | `*(void**)blk = head` |
| D7 | 명령줄 in-place 토큰화 "wr 0x40000000 0x55" → argc/argv | ★★ | 공백→`'\0'` |
| D8 | 이동 평균 (window N, 정수 running sum, 시작 구간 처리) | ★ | sum += new - old |

### E. 링버퍼 · 큐 · ISR 동시성 (8문제)

📄 [N05E 열기](2026-11-01_N05E_ring_queue_isr.html) · 코드 `onsitePrep/n05/e/`

| ID | 문제 | 난이도 | 핵심 |
|---|---|---|---|
| **E1** | 2의 거듭제곱 링버퍼: free-running index (full/empty 구분) | ★★★ | `head - tail == SIZE` |
| E2 | 임의 크기 링버퍼: count 방식 vs 한 칸 비우기 방식 비교 구현 | ★★ | `(i+1) % N` |
| **E3** | ISR producer / main consumer SPSC — volatile·barrier 어디에 | ★★★ | 데이터 쓰고 → barrier → head 갱신 |
| E4 | 덮어쓰기 trace 로그 링 (가장 오래된 것 덮기) + 오래된 순 dump | ★★ | wrapped 플래그 |
| E5 | 소프트웨어 타이머 리스트: add(delta) · tick() · 만료 콜백 | ★★ | 정렬 리스트 / delta list |
| E6 | 이벤트 플래그: ISR이 비트 set → main이 atomic fetch-and-clear | ★★ | IRQ disable 구간 최소화 |
| E7 | 레이스 찾기 (종이): ISR과 main이 공유하는 counter/flag 코드 3개 → 버그·수정 | ★★ | RMW 비원자성, 32-bit tearing |
| E8 | ping-pong(double) DMA 버퍼 핸드오프 + overrun 검출 | ★★ | 소유권 플래그 |

### F. 메모리 테스트 · 데이터 무결성 (8문제)

📄 [N05F 열기](2026-11-01_N05F_memtest_integrity.html) · 코드 `onsitePrep/n05/f/`

| ID | 문제 | 난이도 | 핵심 |
|---|---|---|---|
| F1 | data bus walking-1 테스트 (stuck/short 비트 리포트) | ★★ | 한 주소에 1<<i 쓰고 읽기 |
| **F2** | address bus 테스트 (2의 거듭제곱 offset, stuck-high/low/short 검출) | ★★★ | Barr Group 방식 |
| F3 | address-in-address 패턴 fill + verify (+ 반전 패스) | ★ | `p[i] = (uintptr)&p[i]` |
| F4 | checksum 3종: 8-bit sum · 2의 보수 checksum · Fletcher-16 | ★★ | mod 255 |
| F5 | CRC-8 (poly 0x07) 비트 단위 — 종이로 1바이트 계산까지 | ★★ | MSB-first shift/XOR |
| F6 | PRBS7 LFSR 생성 + 수신 체커 (self-sync, 에러 수 카운트) | ★★ | x^7+x^6+1 |
| F7 | Hamming(7,4) 인코드 / 1비트 정정 디코드 | ★★ | syndrome = 에러 위치 |
| F8 | 메모리 테스트 결과 요약: 실패 주소 목록 → 고장 비트·주소 라인 추정 | ★★ | XOR 누적, AND/OR 패턴 |

### G. 상태 머신 · 파서 (8문제)

📄 [N05G 열기](2026-11-01_N05G_state_machines_parsers.html) · 코드 `onsitePrep/n05/g/`

| ID | 문제 | 난이도 | 핵심 |
|---|---|---|---|
| G1 | 버튼 debounce 상태 머신 (1 ms 샘플, N회 연속이면 확정) | ★ | 카운터 + 안정 상태 |
| **G2** | UART 프레임 파서: SOF 0xAA · LEN · payload · checksum, 바이트 단위 FSM + resync | ★★★ | 상태 enum, 에러 시 IDLE |
| G3 | TLV 파서 (bounds 체크, 알 수 없는 type 건너뛰기) | ★★ | `len > remaining` 거부 |
| G4 | 디버그 쉘: "rd addr" / "wr addr val" / "help" 명령 테이블 디스패치 | ★★ | `{name, fn, argc}` |
| G5 | 단순화 PCIe 링크 FSM: Detect → Polling → Config → L0, timeout·retry·Recovery | ★★ | 테이블 or switch |
| G6 | 테이블 기반 FSM: (state, event) → (next, action) 전원 상태 머신 | ★★ | 2차원 배열 |
| G7 | 센서 프레임 시퀀스 체커: 16-bit 카운터 gap · 중복 · wrap 검출 | ★★ | `(uint16_t)(cur - prev)` |
| G8 | 고정 길이 레코드 로그 파서 "T=123 V=0x1F" 라인 → 구조체 (sscanf 없이) | ★ | 포인터 스캔 |

### H. 코드 읽기 · C 함정 (9문제) — 종이에 가장 잘 맞음

📄 [N05H 열기](2026-11-01_N05H_c_pitfalls_read_code.html) · 코드 `onsitePrep/n05/h/` (실제 출력 기록)

| ID | 문제 | 난이도 | 핵심 |
|---|---|---|---|
| H1 | 출력 맞히기: integer promotion (`~u8`, `u16*u16`, `char` 부호) | ★★ | int로 승격 |
| H2 | `sizeof` 배열 vs 포인터 · struct 패딩/정렬 계산 · packed | ★★ | 가장 큰 멤버 정렬 |
| H3 | `volatile` 없으면 깨지는 루프 3개 고르기 (+ volatile로 안 되는 것) | ★★ | 컴파일러 vs CPU 재배치 |
| H4 | 매크로 함정: `SQUARE(x++)`, 괄호, `do{}while(0)`, 다중 평가 | ★ | inline 함수로 |
| H5 | 시프트 UB: `1<<31`, 폭 이상 시프트, 음수 >> | ★★ | `1u`, `n & 31` |
| **H6** | 드라이버 함수의 버그 5개 찾기 (제공 코드) | ★★★ | 체크리스트로 훑기 |
| H7 | 선언 읽기: `const int *p` vs `int *const p`, 함수 포인터 typedef, 배열 포인터 | ★ | 오른쪽-왼쪽 규칙 |
| H8 | signed/unsigned 비교, `size_t` 역방향 루프, `-1 < sizeof(x)` | ★★ | 산술 변환 |
| H9 | 포인터 캐스트로 엔디안·타입 바꾸기 — strict aliasing, 정렬, memcpy가 정답인 이유 | ★★ | `memcpy(&u, &f, 4)` |

### I. 검증형 C — 측정 · 통계 · 마진 (8문제)

📄 [N05I 열기](2026-11-01_N05I_validation_c.html) · 코드 `onsitePrep/n05/i/`

| ID | 문제 | 난이도 | 핵심 |
|---|---|---|---|
| **I1** | 32-bit 타임스탬프 delta (wrap 안전) → 주기 min/max/mean/jitter | ★★★ | `(uint32_t)(b - a)` |
| I2 | 온라인 평균/분산 (Welford) — 샘플 저장 없이 | ★★ | mean += d/n |
| I3 | latency 히스토그램 버킷 → p50/p99 | ★★ | 누적합 ≥ ceil(p·N) |
| I4 | golden 비교: 허용오차(abs/rel) 기반 mismatch 리포트, 첫 N개만 출력 | ★ | `abs(a-b) > tol` 오버플로 주의 |
| I5 | shmoo 2D pass/fail 배열 → 전압별 pass window · 마진 중심 | ★★ | 최장 연속 pass 구간 |
| I6 | 처리량 계산: bytes·cycles·clock Hz → MB/s (오버플로 없이, 정수만) | ★ | 64-bit, 곱하기 먼저 |
| I7 | Q15 고정소수점 곱셈 (반올림 + 포화) | ★★ | `(a*b + (1<<14)) >> 15`, clamp |
| I8 | 최대 통과 주파수 이진 탐색 (단조 가정, flaky 재시도) | ★★ | lo/hi 불변식 |

## 3. 연습 일정 (D = 온사이트)

| 시점 | 문제 (3개 = 60분) | 비고 |
|---|---|---|
| 1일차 | A1 · C1 · H1 | 루틴(§1) 몸에 익히기 — 타이머 꼭 켜기 |
| 2일차 | A6 · C2 · D1 | |
| 3일차 | A8 · B1 · E1 | |
| 4일차 | B3 · D4 · H6 | |
| 5일차 (주말) | **모의 세트 1**: C5 + G2 (45분) → 꼬리 질문 소리 내 답하기 · E3 · F2 · I1 | |
| 6일차 | A2 · A3 · H2 · H5 | H는 10분짜리 |
| 7일차 | B2 · C3 · E7 · F5 | |
| 8일차 | D2 · G7 · I5 · H8 | |
| 9일차 (주말) | **모의 세트 2**: B4 + F7 (45분) · 남은 ★★ 아무거나 4개 | |
| D-2 | 틀렸던 문제만 다시 (각 10분) | 오답 표에 체크 |
| D-1 | ★★★ 15개 **모범 답 읽기만** + §1 루틴 2회 소리 내서 | 새 문제 X |

- 나머지(★ 문제)는 시간 남을 때 — 출력해 두고 이동 중에 보기.

## 4. 오답 · 시간 기록 표 (Don이 채우기)

| ID | 날짜 | 걸린 시간 | 정답 | 엣지 | UB 없음 | 테스트 말함 | 다시 볼 것 |
|---|---|---|---|---|---|---|---|
| A1 | | | | | | | |
| C1 | | | | | | | |
| H1 | | | | | | | |
| | | | | | | | |

## 5. 기존 자료와 겹치는 부분 (중복 피하기)

| 주제 | 이미 있는 곳 | N05에서는 |
|---|---|---|
| strcpy·strlen·memcpy | [C 정리 C1](2026-10-29_c_basics_cram.html) | D1 memmove 겹침만 |
| 문자열 뒤집기·atoi | [C 정리 C2·C3](2026-10-29_c_basics_cram.html) | D4 hex 파싱+오버플로, D7 토큰화 |
| hi-lo-hi 64-bit 타이머 | [C 정리 C4](2026-10-29_c_basics_cram.html) | C4 latch 방식 비교 + 경과 시간 |
| 링버퍼 기본 | [C 정리 C5](2026-10-29_c_basics_cram.html), [L5 SPSC](hmScreenPrep/problems/L5_spsc_sensor_ring/index.html) | E1–E4 변형 + ISR barrier |
| GENMASK/FIELD_PREP | [C 정리 C6](2026-10-29_c_basics_cram.html), [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html) | A8 임의 폭 + C8 테이블 테스트 |
| 니블 뒤집기 | [N02](2026-10-31_N02_screen_coding_retro.html) | A1 비트 뒤집기, A7 swap |
| March C- | [V1](hmScreenPrep/problems/V1_march_memtest/index.html), [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html) | F1–F3 bus/패턴 테스트, F8 결과 해석 |
| PRBS·CRC-32 | [V6](hmScreenPrep/problems/V6_prbs_crc_checker/index.html) | F5 CRC-8 손계산, F6 PRBS7 self-sync |
| I2C 복구 | [V8](hmScreenPrep/problems/V8_i2c_scan_recover/index.html) | (생략) |
| shmoo | [V7](hmScreenPrep/problems/V7_shmoo_margin/index.html), [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) | I5 종이용 축소판 |
