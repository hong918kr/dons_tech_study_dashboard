---
id: SOCX-B03
time: 65분
updated: 2026-11-08
---
# SOCX-B03 AXI 응답과 배타적 접근: EXOKAY와 Global Exclusive Monitor

## 학습 목표

- AXI 응답 네 가지(OKAY / EXOKAY / SLVERR / DECERR)를 **누가 만들고, 무슨 뜻이고, 마스터가 받으면 무엇을 하는지** 표 하나로 말한다. 특히 "배타 읽기에 OKAY" 와 "배타 쓰기에 OKAY" 의 뜻이 다르다는 것을 안다.
- 슬레이브 쪽 **global exclusive monitor** 를 마스터 ID 마다 {주소, 크기} 예약 하나로 구현한다: 배타 읽기가 예약을 걸고(같은 ID 면 교체), 배타 쓰기는 예약이 살아 있을 때만 EXOKAY + 메모리 갱신, 실패하면 OKAY + 메모리 그대로, 실제로 일어난 쓰기는 겹치는 예약을 모두 지운다.
- 두 마스터가 같은 락을 두고 경쟁하는 버스 트랜잭션 인터리빙을 **전부 열거** 해서, 매 단계 상호배제·배타 쌍의 원자성·epoch 당 EXOKAY 1번·최종 상태를 검사한다. 2개짜리 작은 경우(6가지)는 손으로 세서 열거기와 맞춘다.
- 고장 난 모니터 3종(남의 쓰기를 못 봄 / ID 구분 없는 예약 1개 / 주소 비교 안 함)을 열거기로 잡고, 반례를 **기대 응답이 붙은 directed test** 로 바꿔 재생한다. 어떤 시나리오가 어떤 버그를 잡는지(kill matrix)로 "테스트가 통과했다 ≠ 맞다" 를 숫자로 본다.
- 코어의 LDREX/STREX(ARM-M11)·LDXR/STXR(ARM-M04)이 언제 버스의 배타 트랜잭션이 되는지, 왜 단일 코어 MCU 에서 DMA 와는 이것으로 원자성이 안 생기는지 설명한다.

## 1. 문제

**문제 소개 (한국어).** 두 버스 마스터(예: Cortex-M7 코어와 두 번째 코어, 또는 코어와 DSP)가 공유 SRAM 의 락 변수 하나를 두고 경쟁한다. 각자 "배타 읽기 → 0 이면 배타 쓰기 1 → 응답이 EXOKAY 면 락 획득" 을 한다. 이게 맞게 동작하려면 **슬레이브 쪽 global exclusive monitor** 가 "이 마스터가 읽은 뒤로 누가 그 주소를 썼나" 를 기억해야 한다. 이 모니터와 응답 로직을 C 로 만들고, 두 마스터의 모든 실행 순서를 열거해서 락이 절대 두 명에게 동시에 가지 않음을 증명하라. 그다음 일부러 고장 낸 모니터들을 같은 열거기로 잡아 보라.

왜 중요한가: "`STREX` 가 성공했다는 건 누가 보장하죠? 코어 하나짜리 MCU 에서 DMA 랑은요?" — 펌웨어 면접에서 LDREX/STREX(ARM-M11) 다음에 나오는 질문이고, 실리콘 검증·SoC 면접에서는 "인터커넥트의 exclusive monitor 를 어떻게 검증하겠나" 로 바뀐다. 답은 버스 프로토콜 한 층 아래, 슬레이브의 모니터에 있다. 동시성 가이드 7층의 질문 "왜 배리어/원자 명령이 하드웨어에서 맞게 동작하나" 의 **인터커넥트 쪽 답** 이 이 레슨이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">배타 접근의 길: 마스터 → 인터커넥트 → 슬레이브의 global exclusive monitor</text>
  <rect x="15" y="50" width="150" height="60" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="25" y="72" fill="currentColor">M0: Cortex-M7 코어</text>
  <text x="25" y="92" fill="currentColor" font-family="monospace" font-size="12">LDREX/STREX</text>
  <rect x="15" y="150" width="150" height="60" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="25" y="172" fill="currentColor">M1: 두 번째 코어</text>
  <text x="25" y="192" fill="currentColor" font-family="monospace" font-size="12">LDREX/STREX</text>
  <rect x="15" y="250" width="150" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="25" y="272" fill="currentColor">DMA (배타 안 씀)</text>
  <text x="25" y="290" fill="currentColor" font-size="12">일반 쓰기만</text>
  <rect x="235" y="50" width="150" height="250" rx="6" fill="none" stroke="currentColor"/>
  <text x="250" y="72" fill="currentColor" font-weight="bold">인터커넥트</text>
  <text x="250" y="95" fill="currentColor" font-size="12">주소 디코드</text>
  <text x="250" y="113" fill="currentColor" font-size="12">ID 에 포트 번호 덧붙임</text>
  <rect x="250" y="240" width="120" height="44" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="258" y="258" fill="currentColor" font-size="12">기본 슬레이브</text>
  <text x="258" y="276" fill="currentColor" font-size="12">→ DECERR</text>
  <path d="M165 80 L235 100" stroke="currentColor" fill="none"/>
  <path d="M165 180 L235 160" stroke="currentColor" fill="none"/>
  <path d="M165 275 L235 220" stroke="currentColor" fill="none"/>
  <text x="170" y="128" fill="currentColor" font-size="12">AR/AW + xLOCK</text>
  <rect x="455" y="50" width="270" height="170" rx="6" fill="none" stroke="currentColor"/>
  <text x="470" y="72" fill="currentColor" font-weight="bold">SRAM 슬레이브 (배타 지원)</text>
  <rect x="470" y="85" width="240" height="90" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="480" y="104" fill="currentColor">global exclusive monitor</text>
  <text x="480" y="124" fill="currentColor" font-family="monospace" font-size="12">ID0: {0x000, 4 B} 살아 있음</text>
  <text x="480" y="142" fill="currentColor" font-family="monospace" font-size="12">ID1: {0x000, 4 B} 살아 있음</text>
  <text x="480" y="160" fill="currentColor" font-family="monospace" font-size="12">ID2: -</text>
  <text x="470" y="200" fill="currentColor" font-size="12">실제 쓰기 → 겹치는 예약 전부 삭제</text>
  <rect x="455" y="240" width="270" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="470" y="262" fill="currentColor">ROM: 쓰기 → SLVERR</text>
  <text x="470" y="284" fill="currentColor">주변장치: 배타 미지원 → 늘 OKAY</text>
  <path d="M385 130 L455 130" stroke="currentColor" fill="none"/>
  <path d="M385 270 L455 270" stroke="currentColor" fill="none"/>
  <text x="392" y="122" fill="currentColor" font-size="12">요청</text>
  <path d="M455 150 L385 150" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="392" y="168" fill="currentColor" font-size="12">xRESP</text>
</svg>
```

| xRESP[1:0] | 이름 | 누가 만드나 | 뜻 | 마스터가 할 일 (이 레슨의 `master_action`) |
|---|---|---|---|---|
| `0b00` | **OKAY** | 슬레이브 | 일반 접근 성공. **배타 읽기**에 OKAY = 이 슬레이브는 배타를 지원 안 함(데이터는 유효, 감시 안 함). **배타 쓰기**에 OKAY = 배타 실패, 메모리 안 바뀜 | 일반: 성공 / 배타 읽기: "배타 미지원" 에러 / 배타 쓰기: 재시도 |
| `0b01` | **EXOKAY** | 배타를 지원하는 슬레이브 | 배타 읽기: 감시 시작 / 배타 쓰기: 성공, 메모리 갱신됨 | 성공. 일반 접근에 EXOKAY 가 오면 프로토콜 위반 |
| `0b10` | **SLVERR** | 슬레이브 | 슬레이브까지 갔지만 처리 못 함 (읽기 전용에 쓰기, 지원 안 하는 크기, FIFO 넘침 등) | 버스 에러 → Cortex-M BusFault, Cortex-A 외부 abort |
| `0b11` | **DECERR** | 보통 인터커넥트 (기본 슬레이브) | 그 주소에 슬레이브가 없음 (디코드 실패) | 버스 에러 (같은 경로) |

인코딩과 의미는 Arm IHI 0022 (AMBA AXI and ACE Protocol Specification) 의 응답 신호 절 기준이다 (AXI4 판 Issue E 의 A3.4.4 "Read and write response structure" — 절 번호는 판마다 달라서 확인 필요. 최신 판은 Manager/Subordinate 용어를 쓴다). AXI4 에서 `AxLOCK` 은 1비트(0 = 일반, 1 = 배타)이고, AXI3 의 "locked" 접근은 AXI4 에서 빠졌다.

### Part A — 응답 코드와 global exclusive monitor

`axi_read` / `axi_write` 와 모니터 함수 4개(`mon_excl_read`, `mon_excl_write_ok`, `mon_clear_id`, `mon_snoop_write`), 그리고 마스터 쪽 응답 처리 `master_action` 을 채운다. 주소 맵: `0x000–0x0FF` RAM(배타 지원), `0x100–0x1FF` ROM(쓰기 SLVERR), `0x200–0x2FF` 주변장치(배타 미지원), `0x300~` 없음(DECERR). directed 단위 테스트 29개(CHECK 기준)가 규칙 하나씩을 찌른다: 예약 교체, 바이트 단위 겹침, 크기 불일치, ID 간 독립, 배타 미지원 영역에서 배타 쓰기가 **그냥 써지는 것** 등.

### Part B — 두 마스터 스핀락: 인터리빙 전수 열거

하네스가 준 마스터 프로그램(배타 읽기 → 0 이면 배타 쓰기 1 → EXOKAY 면 임계 구역 `data++`(일반 읽기 + 일반 쓰기) → 일반 쓰기 0 으로 해제, 실패하면 최대 3번까지 재시도)을 두 마스터가 돌린다. 열거기는 매 단계 "누구의 트랜잭션을 다음에 처리할까" 로 갈라지는 트리를 끝까지 탐색하고, 다음 불변식을 **매 단계** 검사한다.

| 불변식 | 내용 |
|---|---|
| P1 상호배제 | 락을 가진 마스터는 동시에 최대 1명 |
| P2 배타 쌍의 원자성 | 배타 쓰기가 EXOKAY 를 받았다면, 그 마스터의 배타 읽기 뒤로 그 워드에 다른 쓰기가 없었다 (하네스의 golden 쓰기 카운터로 확인) |
| P3 epoch | 락 한 번 잡힘~해제(한 epoch) 사이에 EXOKAY 는 1번 |
| P4 최종 상태 | 모두 끝나면 락 = 0, `data` = 임계 구역 진입 횟수 (카운터 시나리오는 카운터 = 성공한 증가 횟수) |

시나리오: S0(배타 읽기+쓰기 한 번씩만, 손으로 세는 작은 경우), S1(스핀락), S2(배타 RMW 카운터: M0 2번, M1 1번 증가), S3(M1 이 첫 시도에서 락을 읽은 뒤 **다른 주소를 배타 읽기** — 같은 코어의 다른 코드가 끼어든 상황), S4(락이 배타 미지원 주변장치에 있음).

### Part C — 고장 난 모니터를 잡고, 반례를 directed test 로

하네스에 고장 난 모니터 3종이 있다. 열거기로 S1–S3 을 돌려 **kill matrix**(어떤 시나리오가 어떤 고장을 잡나)를 만들고, 각 고장의 가장 짧은 반례를 한 단계씩 찍는다. 그 반례 스케줄을 정상 모니터에 돌려 기대 응답을 붙이면 "트랜잭션 n 개 + 기대 xRESP" 짜리 directed test 가 된다. 이것을 고장 모니터에 재생하면 몇 번째 트랜잭션에서 어긋나는지 나온다 — 실리콘 검증에서 golden 모델로 테스트를 만드는 흐름 그대로다.

## 2. Problem (English)

> **Implement an AXI4 slave-side global exclusive monitor and prove a two-master spinlock correct by exhaustive interleaving.**
>
> 1. `resp_t axi_read(sys_t*, int id, uint32_t addr, uint32_t size, bool excl, uint64_t *data)` and `axi_write(...)`. Unmapped → `DECERR` (data 0); write to ROM → `SLVERR`, memory unchanged. Exclusive read to an exclusive-capable region with a legal shape (size a power of two ≤ 128 B, address aligned to size) → record `{addr,size}` for that ID (replacing any previous one) and return `EXOKAY`; otherwise return `OKAY` (data valid, not monitored). Exclusive write → `EXOKAY` and update memory only if that ID's reservation is valid with the same address and size; otherwise `OKAY` and memory unchanged. Either way that ID's reservation ends. Every write that actually updates memory clears every reservation that overlaps it by at least one byte. Exclusive write to a region without exclusive support is a normal write returning `OKAY`.
> 2. `act_t master_action(op_t op, resp_t r)`: SLVERR/DECERR → bus error; exclusive read OKAY → "exclusives not supported"; exclusive write OKAY → retry; EXOKAY on a normal access → protocol error.
> 3. With the provided enumerator, all interleavings of two masters' transactions must satisfy mutual exclusion, exclusive-pair atomicity, ≤ 1 EXOKAY per lock epoch, and the final-state check. The hand-countable case (one exclusive read + write each, no retry) must give exactly 6 interleavings, each with exactly one winner.
>
> Example: `M0 ExRD [0x000] → EXOKAY 0`, `M1 ExRD [0x000] → EXOKAY 0`, `M0 ExWR [0x000]=1 → EXOKAY`, `M1 ExWR [0x000]=1 → OKAY` (M0's write cleared M1's reservation).
>
> Follow-ups: How many monitors does a slave need, and what happens if it has fewer? What does `OKAY` on an exclusive read tell software? Why don't LDREX/STREX make a counter shared with a DMA engine atomic on a single-core Cortex-M? How would you verify an interconnect's exclusive monitor in silicon?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   # FAIL, exit 1 (0.1 s 안에 끝남)
```

처음 실행 결과 (실제 출력 일부):

```text
FAIL starter.c:356  rd(&s, 1, 0x020, 4, false, &d) == OKAY && d == 0x11223344u
FAIL starter.c:357  rd(&s, 0, 0x300, 4, false, &d) == DECERR && d == 0
… (Part A 의 FAIL 17줄 생략)
FAIL starter.c:431  r0.leaves == 6 && r0.bad_paths == 0
FAIL starter.c:433  r0b.leaves == 2 && r0b.bad_paths == 4
info: GOOD monitor  S1-lock 0 paths, S2-counter 0, S3-interloper 0, S4-noexcl 0
…
info: 반례 GOOD / S1-lock — P1 상호배제 위반: 두 마스터가 동시에 락 보유
     1. M0 ExRD [0x000]       -> OKAY   data=0    resv: M0{-}     M1{-}
     2. M0 ExWR [0x000] = 1   -> OKAY             resv: M0{-}     M1{-}
     3. M1 ExRD [0x000]       -> OKAY   data=0    resv: M0{-}     M1{-}
     4. M1 ExWR [0x000] = 1   -> OKAY             resv: M0{-}     M1{-}       <-- 여기서 깨짐
…
11/38 passed
```

시작 코드의 스텁은 "늘 OKAY, 데이터 0, 마스터는 무엇이든 성공으로 본다" 라서 열거기가 곧바로 **상호배제 위반** 을 찾는다. 열거는 재시도 상한(3번)이 있어서 틀린 코드도 멈추지 않는다. 모델 규칙: 슬레이브는 트랜잭션을 하나씩 처리하고, 마스터는 응답을 받은 뒤 다음 트랜잭션을 낸다(배타 쓰기는 어차피 배타 읽기 응답을 받은 뒤에야 낼 수 있다). 데이터 경로는 단일 beat(≤ 8 B)만 — 버스트 주소 계산은 SOCX-B02.

관찰용 파일 (하네스와 별개):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -mthumb -O2 -S demo_core_asm.c -o -   # 코어 쪽 LDREX/STREX
cc -O2 -S demo_core_asm.c -o -                       # arm64 기본 (LSE)
cc -O2 -mcpu=apple-m1+nolse -S demo_core_asm.c -o -  # arm64 LL/SC
```

## 4. 힌트

<details><summary>힌트 1 — 예약은 ID 마다 "하나" 다</summary>

`r[id] = {valid, addr, size}`. 같은 ID 가 새 배타 읽기를 하면 **덮어쓴다** (이전 주소는 더 이상 감시하지 않는다). 그래서 S3 처럼 락을 읽은 뒤 다른 변수를 배타 읽기하면, 락에 대한 배타 쓰기는 반드시 실패해야 한다.

</details>

<details><summary>힌트 2 — 실패한 배타 쓰기는 "쓰기가 아니다"</summary>

배타 쓰기가 실패하면 메모리를 안 바꾼다. 그러니 다른 ID 의 예약을 지우면 안 된다 (`mon_snoop_write` 는 `mem_commit` 을 부른 경우에만). 반대로 **성공한** 배타 쓰기와 모든 일반 쓰기는 진짜 쓰기라서 겹치는 예약을 지운다 — 지우지 않으면 두 마스터가 둘 다 EXOKAY 를 받는다.

</details>

<details><summary>힌트 3 — 배타 읽기에 OKAY 를 돌려줄 때</summary>

배타를 지원하지 않는 곳(ROM, 주변장치)이나 모양 규칙 위반(정렬 안 됨, 크기가 2의 거듭제곱이 아님, 128 B 초과)이면 데이터는 주되 OKAY 를 돌려준다. 마스터는 이걸 "실패해서 재시도" 가 아니라 **"여기선 배타가 안 된다"** 로 읽어야 한다 — 재시도해도 영원히 안 된다.

</details>

<details><summary>힌트 4 — 겹침은 바이트 단위</summary>

`[a, a+as)` 와 `[b, b+bs)` 가 겹치려면 `a < b+bs && b < a+as`. 8 B 예약 `0x040` 안쪽의 1 B 쓰기(`0x044`)는 예약을 지우고, 바로 옆 `0x048` 쓰기는 지우지 않는다. 하네스에 `overlap()` 이 이미 있다.

</details>

## 5. 풀이

### Part A — 모니터 규칙 (스펙과 이 모델)

| 사건 | 이 모델이 하는 일 | 근거 |
|---|---|---|
| 배타 읽기 (지원 영역, 합법 모양) | `r[id] = {addr,size}` (교체), EXOKAY | IHI 0022 "Exclusive accesses": 슬레이브는 배타 읽기의 주소와 ID 를 기록하고, 같은 ID 의 다른 배타 읽기가 감시 주소를 바꿀 때까지 감시 (2차 출처로 문구 확인) |
| 배타 읽기 (미지원 영역 / 모양 위반) | 데이터 + OKAY, 감시 안 함 | 스펙: 배타 미지원 슬레이브는 배타 읽기에 EXOKAY 대신 OKAY |
| 배타 쓰기, 예약 일치 | 메모리 갱신 + 겹치는 예약 전부 삭제, EXOKAY | 스펙: 배타 읽기 뒤로 그 위치에 쓰기가 없었으면 성공 |
| 배타 쓰기, 예약 없음/불일치 | 메모리 그대로, OKAY | 스펙: 실패한 배타 쓰기는 위치를 갱신하면 안 된다 |
| 배타 쓰기 처리 후 | 그 ID 예약 종료 (성공이든 실패든) | **모델 선택** — 스펙 문구 미확인 |
| 실제 쓰기 (일반 / 성공한 배타) | 바이트 하나라도 겹치는 예약 **전부** 삭제 (자기 ID 포함) | 스펙: "그 위치에 쓰기가 일어나면" 감시 종료. 같은 마스터의 일반 쓰기가 자기 예약을 지우는지는 확인 필요 — Arm ARM 의 global monitor 는 이 경우를 IMPLEMENTATION DEFINED 로 둔다고 알려져 있다 (DDI 0487 "Synchronization and semaphores", 미검증) |
| 배타 미지원 영역의 배타 쓰기 | 일반 쓰기처럼 **써지고** OKAY | 2차 출처들 공통 (스펙 원문 확인 필요) |
| 모양 제한 | 크기 1, 2, 4, …, 128 B · 크기에 정렬 (버스트면 16 beat 이하, 읽기와 쓰기의 주소·크기·길이 일치) | IHI 0022 "Exclusive access restrictions" — 어기면 동작 보장 없음. 이 모델은 보수적으로 실패 처리 |

**스펙 확인 상태:** Arm 문서 사이트(IHI 0022)는 이 세션에서 열리지 않아서(JS 페이지) 원문 문구는 직접 못 봤다. 위 표의 "스펙" 칸은 Arm Community 포럼에 인용된 AXI3 문서 문구(절 6.2.3 "A slave that supports exclusive access must have monitor hardware … recommended that such a slave has a monitor unit for each exclusive-capable master ID") 와 교재형 2차 출처를 기준으로 했다. AXI4 판(Issue E)에서는 A7.2 "Exclusive accesses" 아래에 있을 것으로 보이지만 절 번호는 **확인 필요**.

ID 하나 = 마스터 하나가 되는 이유: 인터커넥트는 보통 마스터 포트 번호를 ID 위쪽 비트에 붙여서 슬레이브에 보낸다. 그래서 슬레이브 입장에서 "ID 별 모니터" 는 곧 "마스터(의 그 ID) 별 모니터" 다 (일반적인 구현 — 인터커넥트 문서 확인).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">S1 의 경쟁 한 장면: M0 의 성공한 쓰기가 M1 의 예약을 지운다</text>
  <text x="10" y="62" fill="currentColor">M0</text>
  <text x="10" y="132" fill="currentColor">M1</text>
  <path d="M40 58 L720 58" stroke="currentColor" fill="none" stroke-dasharray="2 4"/>
  <path d="M40 128 L720 128" stroke="currentColor" fill="none" stroke-dasharray="2 4"/>
  <rect x="50" y="42" width="120" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="58" y="62" fill="currentColor" font-family="monospace" font-size="12">ExRD → EXOKAY 0</text>
  <rect x="190" y="112" width="120" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="198" y="132" fill="currentColor" font-family="monospace" font-size="12">ExRD → EXOKAY 0</text>
  <rect x="330" y="42" width="140" height="30" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="338" y="62" fill="currentColor" font-family="monospace" font-size="12">ExWR 1 → EXOKAY</text>
  <rect x="500" y="112" width="140" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="508" y="132" fill="currentColor" font-family="monospace" font-size="12">ExWR 1 → OKAY</text>
  <text x="650" y="132" fill="currentColor" font-size="12">재시도</text>
  <rect x="50" y="175" width="280" height="22" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="58" y="191" fill="currentColor" font-size="12">monitor ID0 = {0x000, 4}</text>
  <rect x="330" y="175" width="390" height="22" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="338" y="191" fill="currentColor" font-size="12">ID0 종료 (자기 배타 쓰기 처리)</text>
  <rect x="190" y="210" width="140" height="22" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="198" y="226" fill="currentColor" font-size="12">ID1 = {0x000, 4}</text>
  <rect x="330" y="210" width="390" height="22" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="338" y="226" fill="currentColor" font-size="12">ID1 삭제 — 겹치는 실제 쓰기를 봤다 (snoop)</text>
  <path d="M400 72 L400 210" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M394 200 L400 210 L406 200" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="20" y="262" fill="currentColor">고장 NO_CLEAR_OTHER 는 이 화살표가 없다 → M1 도 EXOKAY → 락 보유자 2명 (P1 위반)</text>
  <text x="20" y="284" fill="currentColor" font-size="12">마스터 입장: EXOKAY 면 획득, 배타 쓰기 OKAY 면 "졌다 → 다시 읽기", 배타 읽기 OKAY 면 "여기선 배타 불가"</text>
</svg>
```

정답의 핵심은 `axi_write` 의 배타 분기 순서다: ① 모양·예약 확인 → ② 자기 예약 종료 → ③ 실패면 OKAY 로 끝 (쓰기 없음 → 남의 예약 그대로) → ④ 성공이면 `mem_commit` → `mon_snoop_write` → EXOKAY. ③과 ④의 차이가 "실패한 배타 쓰기는 쓰기가 아니다" 이다.

### Part B — 손으로 센 작은 경우부터

S0: 두 마스터가 배타 읽기 한 번, (0 을 읽었으면) 배타 쓰기 한 번. A1/A2 = M0 의 읽기/쓰기, B1/B2 = M1. 상대가 이미 1 을 써 놓은 뒤 읽은 쪽은 쓰기를 안 한다.

| 순서 | M0 이 읽은 값 | M1 이 읽은 값 | 정상 모니터 결과 | NO_CLEAR_OTHER |
|---|---|---|---|---|
| A1 A2 B1 | 0 | 1 → 포기 | M0 획득 | M0 획득 |
| A1 B1 A2 B2 | 0 | 0 | M0 EXOKAY, M1 OKAY | **둘 다 EXOKAY** |
| A1 B1 B2 A2 | 0 | 0 | M1 EXOKAY, M0 OKAY | **둘 다** |
| B1 A1 A2 B2 | 0 | 0 | M0 획득 | **둘 다** |
| B1 A1 B2 A2 | 0 | 0 | M1 획득 | **둘 다** |
| B1 B2 A1 | 1 → 포기 | 0 | M1 획득 | M1 획득 |

그래서 정상 모니터는 6가지 모두 정확히 1명, 고장 모니터는 2가지 정상 + 4가지 위반. 열거기 결과가 이 표와 똑같다 (`r0.leaves == 6`, `r0b.leaves == 2 && r0b.bad_paths == 4`). 이 작은 표를 먼저 맞추는 것이 열거기 자체를 믿을 근거다.

왜 정상 모니터에서는 "둘 다 0 을 읽은" 경우에도 한 명만 이기나: 둘 다 예약을 가진 상태에서 **먼저 처리된 배타 쓰기** 는 그 사이에 쓰기가 없었으니 성공하고, 그 쓰기가 상대 예약을 지운다. 슬레이브가 트랜잭션을 하나씩 처리한다는 것(직렬화 지점)이 원자성의 근원이다 — 코어 안 LL/SC 가 캐시 일관성을 직렬화 지점으로 쓰는 것과 같은 구조다 (ARM-M04).

정상 모니터 전체 결과 (solution 실행):

| 시나리오 | 완료 경로 수 | 위반 | 비고 |
|---|---|---|---|
| S1 스핀락 (재시도 ≤ 3) | 120 | 0 | 모든 경로에서 최소 1명 획득 (`no_progress == 0`) |
| S2 배타 RMW 카운터 | 29 | 0 | 최종 카운터 = 성공한 증가 수 |
| S3 끼어든 배타 읽기 | 360 | 0 | M1 의 첫 배타 쓰기는 늘 실패 (예약이 LOCK_B 로 교체됨) |
| S4 배타 미지원 주변장치 | 2 | 0 | 두 마스터 모두 "배타 미지원" 에러, 아무도 락 안 잡음 |

"매 경로에서 최소 1명은 획득" 의 이유: 실제 쓰기는 성공한 배타 쓰기와 해제뿐이다. 그러니 0 을 읽은 마스터 중 **가장 먼저 배타 쓰기를 낸 쪽** 은 그 사이에 쓰기가 없어서 반드시 성공한다. 버스 수준에서는 서로를 영원히 실패시키는 livelock 이 생기지 않는다 (이 모델 기준 — 슬롯이 모자란 모니터는 연습 2).

S4 가 보여 주는 것: 락 변수를 배타 미지원 영역(많은 MCU 의 주변장치 영역, 일부 SRAM)에 두면 LDREX/STREX 로 만든 락은 **동작하지 않는다**. 순진한 드라이버가 OKAY 를 무시하면 둘 다 락을 잡는다 (연습 1).

### Part C — kill matrix 와 반례

실제 출력 (solution):

```text
info: kill matrix (위반 경로 / 전체 경로)
  GOOD             S1-lock   0/120   S2-counter   0/29    S3-interloper   0/360
  NO_CLEAR_OTHER   S1-lock  16/56    S2-counter  12/15    S3-interloper  40/250
  SHARED_RESV      S1-lock   0/120   S2-counter   2/25    S3-interloper   0/580
  NO_ADDR_CHECK    S1-lock   0/120   S2-counter   0/29    S3-interloper  32/132
```

| 고장 | 무엇이 빠졌나 | 잡는 시나리오 | 왜 그 시나리오만 |
|---|---|---|---|
| NO_CLEAR_OTHER | 실제 쓰기가 남의 예약을 안 지움 | S1, S2, S3 | 둘 다 0 을 읽는 순간 바로 둘 다 성공 — 가장 흔하고 가장 쉽게 잡힌다 |
| SHARED_RESV | ID 구분 없이 예약 1개 | **S2 만** | 스핀락에서 M1 의 배타 쓰기가 남의 예약으로 성공하려면 "락이 잡힌 동안 누군가 배타 읽기" 가 필요한데, 2명뿐이면 그 사이 배타 읽기를 하는 건 M1 자신(1 을 읽고 쓰기를 안 함)뿐이다. 카운터는 값과 무관하게 쓰니 바로 lost update |
| NO_ADDR_CHECK | 배타 쓰기 때 주소·크기 비교 안 함 | **S3 만** | 예약 주소와 쓰기 주소가 다른 경우가 S3 에만 있다 |

열거기가 찍은 가장 짧은 반례 (실제 출력):

```text
info: 반례 SHARED_RESV / S2-counter — P2 원자성: 배타 읽기 뒤 다른 쓰기가 있었는데 EXOKAY
     1. M0 ExRD [0x010]       -> EXOKAY data=0    resv: M0{0x010} M1{-}
     2. M1 ExRD [0x010]       -> EXOKAY data=0    resv: M0{0x010} M1{-}
     3. M0 ExWR [0x010] = 1   -> EXOKAY           resv: M0{-}     M1{-}
     4. M0 ExRD [0x010]       -> EXOKAY data=1    resv: M0{0x010} M1{-}
     5. M1 ExWR [0x010] = 1   -> EXOKAY           resv: M0{-}     M1{-}       <-- 여기서 깨짐
    directed test (5 트랜잭션, 기대 응답 = GOOD 모델): GOOD 통과, SHARED_RESV 는 5번째에서 불일치
info: 반례 NO_ADDR_CHECK / S3-interloper — P1 상호배제 위반: 두 마스터가 동시에 락 보유
     1. M0 ExRD [0x000]       -> EXOKAY data=0    resv: M0{0x000} M1{-}
     2. M1 ExRD [0x000]       -> EXOKAY data=0    resv: M0{0x000} M1{0x000}
     3. M0 ExWR [0x000] = 1   -> EXOKAY           resv: M0{-}     M1{-}
     4. M1 ExRD [0x040]       -> EXOKAY data=0    resv: M0{-}     M1{0x040}
     5. M1 ExWR [0x000] = 1   -> EXOKAY           resv: M0{-}     M1{-}       <-- 여기서 깨짐
    directed test (5 트랜잭션, 기대 응답 = GOOD 모델): GOOD 통과, NO_ADDR_CHECK 는 5번째에서 불일치
```

(SHARED_RESV 의 `resv` 열은 예약 1개를 `r[0]` 에 두므로 M0 칸에만 보인다.) 첫 번째 반례를 읽으면: 2번에서 M1 의 배타 읽기가 공용 예약을 덮고, 4번에서 M0 가 다시 걸어 둔 예약을 5번의 M1 이 **자기 것처럼** 써서 EXOKAY 를 받는다. M1 이 읽은 값은 0 인데 카운터는 이미 1 → 1 을 다시 써서 증가 하나가 증발한다. P4(최종 카운터)보다 P2(원자성)가 먼저, 정확히 그 트랜잭션에서 잡는다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">golden 모델 → 반례 → directed test → DUT 재생</text>
  <rect x="15" y="45" width="160" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="25" y="68" fill="currentColor">열거기 + mutant</text>
  <text x="25" y="88" fill="currentColor" font-size="12">불변식 P1–P4 위반</text>
  <text x="25" y="105" fill="currentColor" font-size="12">→ 반례 스케줄</text>
  <rect x="205" y="45" width="160" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="215" y="68" fill="currentColor">GOOD 모델 재생</text>
  <text x="215" y="88" fill="currentColor" font-size="12">같은 스케줄의</text>
  <text x="215" y="105" fill="currentColor" font-size="12">트랜잭션 + 기대 xRESP</text>
  <rect x="395" y="45" width="160" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="405" y="68" fill="currentColor">directed test</text>
  <text x="405" y="88" fill="currentColor" font-size="12">M1 ExWR [0x000]=1</text>
  <text x="405" y="105" fill="currentColor" font-size="12">expect OKAY</text>
  <rect x="585" y="45" width="140" height="70" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="595" y="68" fill="currentColor">DUT (RTL/FPGA</text>
  <text x="595" y="88" fill="currentColor">/실칩) 재생</text>
  <text x="595" y="105" fill="currentColor" font-size="12">k번째 불일치</text>
  <path d="M175 80 L205 80" stroke="currentColor" fill="none"/>
  <path d="M365 80 L395 80" stroke="currentColor" fill="none"/>
  <path d="M555 80 L585 80" stroke="currentColor" fill="none"/>
  <text x="15" y="150" fill="currentColor">커버리지 포인트 (각각 directed test 1개 이상):</text>
  <text x="25" y="172" fill="currentColor" font-size="12">• 다른 ID 의 일반 쓰기 → 배타 쓰기</text>
  <text x="375" y="172" fill="currentColor" font-size="12">• 같은 ID 재예약 (다른 주소)</text>
  <text x="25" y="192" fill="currentColor" font-size="12">• 성공한 배타 쓰기 → 상대 배타 쓰기</text>
  <text x="375" y="192" fill="currentColor" font-size="12">• 부분 겹침 1 B / 바로 옆 주소</text>
  <text x="25" y="212" fill="currentColor" font-size="12">• 크기·주소 불일치, 예약 없이 배타 쓰기</text>
  <text x="375" y="212" fill="currentColor" font-size="12">• 배타 미지원 영역 / ROM / 맵 밖</text>
  <text x="25" y="232" fill="currentColor" font-size="12">• ID 수 &gt; 모니터 수 (거짓 성공 0 인지)</text>
  <text x="15" y="260" fill="currentColor" font-size="12">주의: 기대 응답을 정확히 비교하면, 안전하지만 일찍 실패하는 합법 구현(연습 2)도</text>
  <text x="15" y="278" fill="currentColor" font-size="12">불일치로 나온다 → 검사기는 "거짓 EXOKAY = 버그" 와 "허용된 거짓 실패" 를 나눠야 한다.</text>
</svg>
```

**실리콘 검증으로 옮기면:** 이 C 모델이 곧 scoreboard 의 reference model 이다. ① 열거기가 만든 반례 스케줄을 directed sequence 로(위 3개 + 커버리지 표), ② 무작위 스트레스: 여러 마스터가 무작위 ID·주소·크기로 배타/일반 접근을 섞고, 모든 EXOKAY 마다 P2(그 사이 쓰기 없음)를 scoreboard 에서 확인, ③ mutant(이 레슨의 3종)를 모델에 넣어 테스트 묶음이 전부 잡는지 = 테스트의 테스트. kill matrix 가 보여 주듯 "스핀락 스트레스 하나" 로는 SHARED_RESV 와 NO_ADDR_CHECK 를 놓친다.

### 코어 쪽: 어떤 명령이 배타 트랜잭션이 되나

`demo_core_asm.c` 의 스핀락 시도·해제·카운터를 실제로 컴파일한 결과 (Apple clang 21.0.0):

| 함수 | thumbv7em (`-mcpu=cortex-m7`) | arm64 기본 | arm64 `-mcpu=apple-m1+nolse` |
|---|---|---|---|
| `lock_try` (CAS 0→1, acquire) | `ldrex` / `cbnz` / `strex` / `bne` 루프 + `dmb sy`, 실패 경로 `clrex` | `casa w8, w9, [x0]` | `ldaxr` / `stxr` 루프, 실패 경로 `clrex` |
| `lock_release` (release store) | `dmb sy` + `str` | `stlr wzr, [x0]` | `stlr wzr, [x0]` |
| `counter_inc` (relaxed fetch_add) | `ldrex` / `adds` / `strex` / `bne` | `ldadd w8, w0, [x0]` | `ldxr` / `add` / `stxr` / `cbnz` |

연결 (전부 **hedge** — 코어별 TRM 확인 필요):

- **Cortex-M (ARM-M11):** LDREX/STREX 는 먼저 코어의 **로컬 모니터** 를 쓴다. Shareable 로 표시된 영역이고 시스템에 global monitor 가 있으면 버스에 배타 신호(AXI 의 `AxLOCK`, AHB5 의 `HEXCL`/`HEXOKAY`)가 나가고, STREX 는 로컬과 글로벌 둘 다 통과해야 성공한다 (DDI 0403 A3.4.2). 대부분의 단일 코어 MCU SRAM 에는 global monitor 가 없어서, **DMA 의 일반 쓰기를 아무도 보지 못한다** → LDREX/STREX 로 DMA 와 공유한 카운터는 원자적이지 않다. 이 레슨의 모니터가 "다른 마스터의 쓰기가 예약을 지운다" 를 하는 바로 그 장치가 없기 때문이다. 해결은 소유권 비트·디스크립터 순서·DSB (C-A02, ARCH-M12).
- **Cortex-A 멀티코어 (ARM-M04):** Normal·cacheable·shareable 메모리의 LDXR/STXR 는 보통 버스 배타 트랜잭션으로 나가지 않고, 코어가 라인을 Unique/Exclusive 로 들고 있다가 다른 코어의 snoop(무효화)이 오면 로컬 모니터가 풀리는 식으로 **캐시 일관성 프로토콜이 global monitor 역할** 을 한다 (ARCH-A04 의 MESI 에서 RFO 가 그 신호). Non-cacheable/Device 메모리의 배타 접근만 인터커넥트·슬레이브의 global monitor 로 간다고 알려져 있다.
- **LSE (`casa`, `ldadd`, `swp`):** 실패·재시도가 없는 단일 원자 명령이다. 인터커넥트 쪽에서는 AXI5/ACE5-Lite 의 atomic 트랜잭션(AtomicStore · AtomicLoad · AtomicSwap · AtomicCompare, `AWATOP`)이나 CHI 의 atomic 요청으로 내려가 홈 노드/슬레이브에서 실행될 수 있다(far atomic). 어느 판에서 도입됐는지, Apple 칩이 실제로 그렇게 하는지는 **미확인**. CHI 의 배타 접근은 `Excl` 속성과 `ExOkay` 응답으로 표현된다 (IHI 0050, 미검증 — SOCX-M11).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

실제 출력 (6번 연속 실행, 출력 바이트까지 동일, 매번 exit 0):

```text
info: GOOD monitor  S1-lock 120 paths, S2-counter 29, S3-interloper 360, S4-noexcl 2
info: kill matrix (위반 경로 / 전체 경로)
  GOOD             S1-lock   0/120   S2-counter   0/29    S3-interloper   0/360
  NO_CLEAR_OTHER   S1-lock  16/56    S2-counter  12/15    S3-interloper  40/250
  SHARED_RESV      S1-lock   0/120   S2-counter   2/25    S3-interloper   0/580
  NO_ADDR_CHECK    S1-lock   0/120   S2-counter   0/29    S3-interloper  32/132
info: 반례 NO_CLEAR_OTHER / S1-lock — P1 상호배제 위반: 두 마스터가 동시에 락 보유
     1. M0 ExRD [0x000]       -> EXOKAY data=0    resv: M0{0x000} M1{-}
     2. M1 ExRD [0x000]       -> EXOKAY data=0    resv: M0{0x000} M1{0x000}
     3. M0 ExWR [0x000] = 1   -> EXOKAY           resv: M0{-}     M1{0x000}
     4. M1 ExWR [0x000] = 1   -> EXOKAY           resv: M0{-}     M1{-}       <-- 여기서 깨짐
    directed test (4 트랜잭션, 기대 응답 = GOOD 모델): GOOD 통과, NO_CLEAR_OTHER 는 4번째에서 불일치
… (SHARED_RESV, NO_ADDR_CHECK 반례는 5. 풀이 Part C 에 붙인 것과 같음)
38/38 passed
```

S1/S2/S3 의 경로 수(120 · 29 · 360)는 solution 실행으로 얻은 **회귀값** 이다. 손으로 검증한 값은 S0 의 6 / 2+4 다. 모니터를 다르게 구현해도(예: 같은 ID 의 일반 쓰기가 자기 예약을 안 지우게) 이 시나리오들에서는 자기 예약이 이미 배타 쓰기로 끝난 뒤라 경로 수가 바뀌지 않는다.

새니타이저 (단일 스레드 모델이라 TSan 은 해당 없음):

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san   # 38/38, 보고 0, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 14/14, 보고 0, exit 0
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 실패한 배타 쓰기에도 `mon_snoop_write` 호출 | 메모리를 안 바꿨는데 남의 예약을 지운다 → 두 마스터가 서로를 끝없이 실패시킬 수 있다 (안전하지만 진행 손실) | `if (!ok) return OKAY;` 를 commit/snoop **앞에** |
| 성공한 배타 쓰기 뒤 snoop 생략 | "배타 쓰기는 일반 쓰기가 아니니까" — 하지만 메모리는 바뀌었다. NO_CLEAR_OTHER 와 같은 버그 → 둘 다 EXOKAY | 성공 경로도 `mem_commit` → `mon_snoop_write` |
| 예약을 ID 구분 없이 1개로 | 남의 예약으로 내 배타 쓰기가 성공 (SHARED_RESV, S2 lost update) | `r[id]`, 혹은 슬롯이 모자라면 **ID 태그** 를 붙이고 태그가 다르면 실패 (연습 2) |
| 배타 쓰기 때 주소·크기 비교 생략 | 같은 ID 가 다른 주소를 재예약한 뒤의 쓰기가 성공 (NO_ADDR_CHECK, S3 상호배제 위반) | `r->valid && r->addr == addr && r->size == size` |
| 겹침을 시작 주소 같음으로만 판정 | 8 B 예약 안쪽의 1 B 쓰기를 놓친다 | 구간 겹침 `a < b+bs && b < a+as` |
| 배타 읽기의 OKAY 를 "재시도" 로 처리 | 배타 미지원 영역이면 영원히 실패 → 무한 루프 (상한이 없으면 hang) | `ACT_NOEXCL` 로 즉시 에러 보고 |
| 배타 쓰기의 OKAY 를 성공으로 처리 | 미지원 영역에서는 쓰기가 실제로 일어나므로 두 마스터 다 "획득" (연습 1 실측) | `r == EXOKAY ? SUCCESS : RETRY` |
| 락 변수 옆에 자주 쓰는 데이터 | 모니터가 granule 단위면 옆 쓰기가 예약을 지워 재시도 폭증 (연습 3) | 락을 granule(예: 64 B)에 단독 배치 — false sharing 과 같은 처방 (ARCH-M08) |

## 8. 면접 Q&A

<details><summary>Q1. AXI 응답 네 가지를 설명해 보세요. 누가 만들고 마스터는 무엇을 하나요?</summary>

`OKAY`(00) 일반 성공, `EXOKAY`(01) 배타 접근 성공, `SLVERR`(10) 슬레이브가 받았지만 처리 못 함(읽기 전용 레지스터 쓰기, 지원 안 하는 크기 등), `DECERR`(11) 그 주소에 슬레이브가 없음 — 보통 인터커넥트의 기본 슬레이브가 만든다. 마스터는 SLVERR/DECERR 를 버스 에러로 올린다: Cortex-M 은 BusFault(쓰기 버퍼 때문에 imprecise 일 수 있음), Cortex-A 는 외부 abort(비동기면 SError). 꼬리: "버스트 중간 beat 만 에러면?" → 읽기는 beat 마다 RRESP 가 있고, 쓰기는 버스트 전체에 BRESP 하나다. 에러가 나도 버스트는 정해진 길이만큼 끝까지 전송한다 (스펙 확인 필요).

</details>

<details><summary>Q2. 배타 읽기에 OKAY 가 왔습니다. 무슨 뜻이고, 그 뒤 배타 쓰기는 어떻게 되나요?</summary>

그 슬레이브가 배타 접근을 **지원하지 않는다** 는 뜻이다. 데이터는 유효하지만 아무도 감시하지 않는다. 그 상태로 배타 쓰기를 내면 미지원 슬레이브는 그냥 일반 쓰기로 처리해서 **메모리가 바뀌고** OKAY 를 돌려준다(2차 출처 공통, 원문 확인 필요). 그래서 드라이버가 배타 쓰기 OKAY 를 "실패, 재시도" 로만 보면 결국 루프를 돌고, "성공" 으로 보면 두 마스터 다 락을 잡는다 (연습 1 실측: 56경로 중 16경로 위반). 꼬리: "소프트웨어는 실행 중에 이걸 감지하나?" → 실무에서는 보통 미리 안다. 메모리 맵 문서에서 배타 지원 영역을 확인하고 락은 그곳에만 둔다 (Arm Community 토론도 같은 의견).

</details>

<details><summary>Q3. 슬레이브에는 모니터가 몇 개 필요한가요? 모자라면요?</summary>

스펙 권고는 "배타를 쓸 수 있는 ID 마다 하나" 다 (AXI3 문서 6.2.3 인용 기준). 모자라면 예약을 교체할 수밖에 없고, 그때 규칙은 하나다: **거짓 실패는 허용, 거짓 성공은 금지.** 슬롯에 ID 태그를 붙이고 태그가 다르면 실패시키면 안전하다 (연습 2: 위반 0, 대신 ExWR 실패가 GOOD 16 → 50 회로 늘어남). 태그 없이 슬롯 하나를 공유하면 SHARED_RESV 버그 — 남의 예약으로 성공한다 (S2 에서 lost update). 꼬리: "거짓 실패는 왜 괜찮나?" → 소프트웨어가 어차피 재시도 루프를 돌기 때문. 다만 실패가 많으면 성능, 극단적이면 진행(livelock) 문제가 된다.

</details>

<details><summary>Q4. 단일 코어 Cortex-M4 에서 DMA 와 공유하는 카운터를 LDREX/STREX 로 증가시키면 원자적인가요?</summary>

아니다. STREX 의 성공 판단은 **코어의 로컬 모니터** 가 하고, 로컬 모니터는 자기 코어의 배타 접근과 예외만 본다. DMA 의 일반 쓰기를 보려면 그 메모리에 global monitor 가 있어야 하는데(이 레슨의 `mon_snoop_write` 가 하는 일), 대부분의 단일 코어 MCU SRAM 에는 없다 (칩 RM 확인). 그래서 LDREX 와 STREX 사이에 들어온 DMA 쓰기를 STREX 가 덮어쓴다. QEMU 는 STREX 를 값 비교로 흉내 내서 이 버그를 숨긴다 (ARM-M11 실측). 꼬리: "그럼 어떻게?" → DMA 와는 원자 RMW 를 공유하지 않는다. 소유권 비트 + 디스크립터 순서 + DSB, 또는 DMA 완료 인터럽트에서만 갱신 (C-A02, ARCH-M12).

</details>

<details><summary>Q5. Cortex-A 멀티코어에서 LDXR/STXR 는 매번 버스에 배타 트랜잭션을 내나요?</summary>

보통 아니다 (코어 TRM 확인 필요). Normal cacheable shareable 메모리라면 코어는 라인을 Unique 로 가져와 로컬 모니터로 감시하고, 다른 코어가 쓰려고 snoop 무효화를 보내면 모니터가 풀린다. 즉 **캐시 일관성 프로토콜이 global monitor 역할** 을 한다 (MESI 의 RFO/BusRdX — ARCH-A04, MEM-M04). 버스의 `AxLOCK` 배타 트랜잭션과 슬레이브 쪽 global monitor 는 Non-cacheable/Device 메모리처럼 캐시로 감시할 수 없는 경우를 위한 것이다. 꼬리: "LSE 는?" → `casa`/`ldadd` 는 재시도 없는 명령이고, AXI5/CHI 의 atomic 트랜잭션으로 홈 노드에서 실행될 수 있다(far atomic, 구현마다 다름). 이 맥에서 `casa` CAS 루프가 경합 시 LL/SC 보다 느렸다는 ARM-M04 실측이 그 차이를 보여 주는 사례일 수 있다 (원인은 가설).

</details>

<details><summary>Q6. 인터커넥트의 exclusive monitor 를 실리콘(또는 RTL)에서 어떻게 검증하겠습니까?</summary>

① 이 레슨 같은 **reference model**(C/SystemVerilog) 을 scoreboard 에 둔다. ② **directed:** 커버리지 포인트마다 테스트 — 다른 ID 일반 쓰기 후 배타 쓰기, 성공한 배타 쓰기 후 상대의 배타 쓰기, 같은 ID 재예약, 1 B 부분 겹침과 바로 옆 주소, 크기 불일치, 예약 없는 배타 쓰기, 미지원 영역·ROM·맵 밖, ID 수 > 모니터 수. 작은 열거기의 반례 스케줄을 그대로 directed sequence 로 바꾼다 (Part C). ③ **constrained random:** 여러 마스터·ID·주소를 섞고, 모든 EXOKAY 마다 "그 ID 의 배타 읽기 이후 겹치는 쓰기 없음" 을 scoreboard 가 확인. ④ **mutation:** 모델에 고장을 넣어 테스트 묶음이 다 잡는지 — kill matrix 에서 보듯 스핀락 스트레스 하나로는 3개 중 1개만 잡힌다. ⑤ 실칩: 두 코어가 같은 주소로 LDREX/STREX 카운터를 돌리고 최종값 확인 + 디버거/DMA 로 사이에 쓰기 주입. 꼬리: "기대 응답을 정확히 비교하면 문제는?" → 합법적인 거짓 실패(슬롯 부족, granule)도 불일치로 뜬다. 검사기는 "거짓 EXOKAY = 버그", "거짓 OKAY = 허용(카운트만)" 으로 나눠야 한다 (연습 2: ONE_SLOT 은 3개 directed test 모두 3번째에서 불일치).

</details>

<details><summary>Q7. 배타 접근의 크기·정렬 제한은 왜 있나요?</summary>

모니터가 감시할 범위를 단순하게 만들기 위해서다: 크기는 1~128 B 의 2의 거듭제곱, 주소는 크기에 정렬, 버스트는 16 beat 이하, 배타 쓰기는 배타 읽기와 주소·크기·길이가 같아야 한다 (IHI 0022 "Exclusive access restrictions", 원문 확인 필요). 정렬된 2의 거듭제곱이면 감시 범위가 "주소 상위 비트 비교" 하나로 끝난다. 슬레이브는 더 넓은 범위(최대 128 B 로 알려짐)를 감시해도 되는데, 그러면 옆 주소 쓰기로 거짓 실패가 생긴다 — Arm 코어의 ERG(Exclusives Reservation Granule)와 같은 생각이다 (연습 3, ARM-M04 는 `CTR_EL0` 을 못 읽어 ERG 미측정).

</details>

## 9. 추가 연습

### 연습 1 — 순진한 드라이버: OKAY 도 성공으로 본다

정상 모니터를 쓰되 마스터의 응답 처리를 "에러만 아니면 성공" 으로 바꾸고, S1 과 S4 를 열거하라. 위반이 나오나? 반례는?

<details><summary>답</summary>

둘 다 위반: S1 16/56, S4 16/56 (실제 출력). S4 반례는 4단계 — 두 마스터 다 배타 읽기에 OKAY(배타 미지원)를 받고 무시, 배타 쓰기도 OKAY(**실제로 써짐**)를 성공으로 본다:

```text
info: 반례 (naive, S4) — P1 상호배제 위반: 두 마스터가 동시에 락 보유
     1. M0 ExRD [0x200]       -> OKAY   data=0    resv: M0{-}     M1{-}
     2. M1 ExRD [0x200]       -> OKAY   data=0    resv: M0{-}     M1{-}
     3. M0 ExWR [0x200] = 1   -> OKAY             resv: M0{-}     M1{-}
     4. M1 ExWR [0x200] = 1   -> OKAY             resv: M0{-}     M1{-}       <-- 여기서 깨짐
```

모니터가 맞아도 드라이버가 응답을 잘못 읽으면 끝이다. `master_action` 으로 되돌리면 S1, S4 모두 위반 0.

</details>

### 연습 2 — 모니터 슬롯이 1개뿐이라면 (ID 태그 있음)

슬롯 하나에 `{slot_id, addr, size}` 를 두고, 다른 ID 의 배타 읽기는 슬롯을 빼앗는다. 배타 쓰기는 태그가 자기 ID 이고 주소·크기가 맞을 때만 성공. S1–S3 위반 수, S1 의 ExWR 실패 횟수를 GOOD 과 비교하라. Part C 의 directed test 3개는 통과하나?

<details><summary>답</summary>

위반 0 (안전). S1 경로 226개, 진행 없는 경로 0, ExWR 실패 50회 (GOOD: 120경로, 16회). 서로 예약을 빼앗아 실패가 3배 늘지만, 재시도 상한에 먼저 닿아 포기한 쪽이 생기면 남은 쪽은 반드시 성공하므로 이 모델에선 "아무도 못 잡는" 경로는 없다. directed test 3개는 **모두 3번째 트랜잭션에서 불일치** — GOOD 은 EXOKAY 를 기대하는데 ONE_SLOT 은 예약을 빼앗겨 OKAY. 합법적인 구현이 정확 비교 테스트에 떨어지는 예다 (Q6 꼬리).

</details>

### 연습 3 — 64 B granule 모니터

예약을 `addr & ~63`, 64 B 로 넓혀 저장하는 모니터를 만들어라. S1–S3 위반 수는? M0 가 `0x000` 을 배타 읽기한 뒤 M1 이 `0x020`(같은 64 B) / `0x040`(다음 64 B)에 쓰면 M0 의 배타 쓰기는?

<details><summary>답</summary>

위반 0. `0x020` 쓰기 → M0 배타 쓰기 **OKAY (거짓 실패)**, GOOD 은 EXOKAY. `0x040` 쓰기 → EXOKAY. 넓게 감시하면 안전 쪽으로만 틀린다. Part C directed test 3개는 GRANULE64 에서 모두 통과했다 (그 시나리오들엔 같은 granule 의 다른 워드 쓰기가 없어서). 교훈: 락 변수는 granule 에 혼자 둔다 — false sharing(ARCH-M08)과 같은 배치 규칙.

</details>

### 연습 4 — 합법적인 배타 접근 모양은 몇 개?

주소 0..255, 크기 1..256 의 조합 중 `excl_shape_ok` 를 만족하는 것은 몇 개인가? `(0x80,128)`, `(0x00,256)`, `(0x20,64)`, `(0x03,3)` 은 각각?

<details><summary>답</summary>

크기 2^k (k = 0..7) 마다 정렬된 주소가 256/2^k 개 → 256+128+64+32+16+8+4+2 = **510**. `(0x80,128)` 합법, `(0x00,256)` 128 B 초과, `(0x20,64)` 정렬 안 됨(0x20 % 64 = 32), `(0x03,3)` 2의 거듭제곱 아님.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 14/14 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pa && ./pa   # 14/14, 깨끗
```

실제 출력:

```text
info: naive master  S1 위반 16/56, S4(배타 미지원) 위반 16/56
… (연습 1 의 반례 4줄)
info: ONE_SLOT  위반 0, S1 진행 없는 경로 0/226 (GOOD 0/120), ExWR 실패 50 (GOOD 16)
info: directed(NO_CLEAR_OTHER/S1-lock) on ONE_SLOT: 3, GRANULE64: 0  (0 = 통과, k = k번째 불일치)
info: directed(SHARED_RESV/S2-counter) on ONE_SLOT: 3, GRANULE64: 0  (0 = 통과, k = k번째 불일치)
info: directed(NO_ADDR_CHECK/S3-interloper) on ONE_SLOT: 3, GRANULE64: 0  (0 = 통과, k = k번째 불일치)
14/14 passed
```

## 10. 핵심 정리

- xRESP: `OKAY`(00) 일반 성공 · `EXOKAY`(01) 배타 성공 · `SLVERR`(10) 슬레이브 에러 · `DECERR`(11) 디코드 에러(인터커넥트). 마스터는 뒤의 둘을 버스 에러로.
- **배타 읽기 OKAY = 배타 미지원** (재시도 무의미, 미지원 슬레이브의 배타 쓰기는 그냥 써진다). **배타 쓰기 OKAY = 졌다, 메모리 그대로 → 다시 읽기.**
- global monitor = ID 마다 {주소, 크기} 하나. 배타 읽기가 걸고(같은 ID 면 교체), 일치하는 배타 쓰기만 성공, **실제로 일어난 모든 쓰기가 겹치는 예약을 지운다.** 실패한 배타 쓰기는 쓰기가 아니다.
- 슬레이브가 트랜잭션을 하나씩 처리하는 것이 직렬화 지점 → 둘 다 0 을 읽어도 먼저 쓴 쪽만 이긴다. 손으로 센 S0: 6경로 모두 1명.
- 규칙: 거짓 실패는 허용(슬롯 부족, 넓은 granule), **거짓 성공은 금지**. ID 태그 없는 공용 예약과 주소 비교 생략은 거짓 성공 버그.
- kill matrix: 스핀락 스트레스는 3개 고장 중 1개만 잡는다. 카운터(S2)·같은 ID 재예약(S3) 시나리오가 있어야 다 잡힌다 → 반례를 directed test 로.
- 단일 코어 Cortex-M 의 LDREX/STREX 는 로컬 모니터뿐 → DMA 와는 원자적이지 않다. Cortex-A 의 cacheable 메모리는 캐시 일관성이 global monitor 역할, LSE 는 AXI5/CHI atomic 으로 (hedge).

## 참고자료

- Arm, *AMBA AXI and ACE Protocol Specification* (IHI 0022) — 응답 신호(RRESP/BRESP 인코딩), "Exclusive accesses" (exclusive access process / from the perspective of the master / slave / restrictions / slave without exclusive support), AXI5 atomic transactions(`AWATOP`). 이 세션에서는 원문을 열지 못해 절 번호는 확인 필요 (AXI4 Issue E 기준 A3.4.4, A7.2 로 알려짐): <https://developer.arm.com/documentation/ihi0022/latest>
- Arm, *AMBA CHI Architecture Specification* (IHI 0050) — Exclusive accesses(`Excl`, `ExOkay`), Atomic transactions: <https://developer.arm.com/documentation/ihi0050/latest>
- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403) — A3.4 Synchronization and semaphores (A3.4.1 local monitor, A3.4.2 global monitor): <https://developer.arm.com/documentation/ddi0403/latest/>
- Arm, *Arm Architecture Reference Manual for A-profile* (DDI 0487) — "Synchronization and semaphores": local/global exclusive monitors, Exclusives Reservation Granule: <https://developer.arm.com/documentation/ddi0487/latest/>
- 2차 출처 (스펙 문구 인용·해설): Arm Community, "AMBA3 AXI - Exclusive access" 토론 (AXI3 문서 6.2.3 인용): <https://community.arm.com/support-forums/f/architectures-and-processors-forum/5096/amba3-axi---exclusive-access---04-16-2015> · VLSI Verify, "Exclusive accesses": <https://vlsiverify.com/protocols/axi/exclusive-accesses/>
- 관련 레슨: ARM-M11 (Cortex-M 로컬 모니터, 예외 시 클리어, QEMU 값 비교), ARM-M04 (LDXR/STXR vs LSE 실측), ARM-M05 (DMB/DSB), C-A03 (C11 atomics, thumbv7em `clrex`), C-A02 · ARCH-M12 (DMA 순서·캐시 유지보수), ARCH-A04 (MESI — cacheable 배타의 실제 직렬화), MEM-M04 · ARCH-M08 (일관성 트래픽·false sharing), RISCV-M04 (LR/SC 예약 집합), SOCX-B02 (버스트 주소), SOCX-M09 (ACE), SOCX-M11 (CHI 흐름)
