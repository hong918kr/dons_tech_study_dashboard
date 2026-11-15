---
id: DS-A10
time: 110분
updated: 2026-11-08
---
# DS-A10 플래시 로그 구조 KV 저장소

## 학습 목표

- NOR 플래시의 세 규칙(섹터 단위 소거 → 전부 `0xFF`, 프로그램은 비트 1→0만, 소거 전에 다시 프로그램 금지)을 **검사기가 있는 모델**로 구현하고, 그 규칙 위에서 append-only KV(레코드 + CRC, RAM 인덱스, 페이지 분할 쓰기)를 만든다.
- **쓰기 원자성을 가정하지 않는** 복구 프로토콜을 설계한다. 섹터 분류 5가지(BLANK / ACTIVE / COPYING / DEAD / GARBAGE), 섹터 헤더의 `commit` 워드를 **목적지 쪽**에 두는 이유, "정확히 이 값일 때만 커밋"이라는 디코딩 규칙을 근거와 함께 설명한다.
- **모든 program/erase 호출마다** 전원 차단을 주입한다. 찢어진 쓰기(앞 k바이트, k번째 바이트 일부 비트, 임의 비트)와 부분 소거(앞/뒤 일부만 소거, 뒤쪽 0x00 = pre-program 단계, 임의 비트)를 전부 돌리고, **복구 중의 2차 차단**까지 확인한다. 불변식은 "모든 키는 마지막 커밋 값 또는 진행 중이던 값, 쓰레기 값 없음"이다.
- 원자적 배치(여러 키를 CRC 하나로 묶기), 링 컴팩션의 마모 평탄화, 부트 카운터 시나리오를 펌웨어 관점에서 구현하고, WAF·소거 횟수를 실측으로 읽는다.
- "데이터시트가 실제로 보장하는 것"과 "내가 모델에서 가정한 것"을 구분해서 말한다. 부분 소거의 pre-program 단계(0으로 채워짐) 같은 실제 NOR 동작이 설계를 어떻게 바꾸는지 설명한다.

## 1. 문제

"플래시에 설정값을 저장하는 KV를 짜 보세요. 전원이 아무 때나 끊겨도 깨지면 안 됩니다." 임베디드 펌웨어 면접의 단골 설계 문제이자, 실제 제품에서 현장 불량의 큰 원인이기도 한 주제다. 답의 뼈대(로그 + CRC)는 누구나 말한다. 시니어 면접은 그 다음을 묻는다. **컴팩션 중에 전원이 끊기면? 소거가 중간에 멈춘 섹터는 어떤 모양인가? 그걸 어떻게 전수로 시험했나?**

NOR 플래시의 제약부터 정리하자. 프로그램은 비트를 1에서 0으로만 바꾼다. 0을 1로 되돌리는 유일한 방법은 **섹터 전체 소거**(수 KB, 수십 ms 단위)다. 그래서 값을 제자리에서 덮어쓸 수 없고, "새 레코드를 뒤에 붙이고 옛 레코드는 쓰레기로 둔 다음 나중에 치운다"는 로그 구조(log-structured)가 자연스럽다. 비용을 한 곳에 몰면 마모도 같이 몰린다. 그래서 공간 회수(컴팩션)와 마모 분산이 설계에 함께 들어간다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">섹터 레이아웃 — 앞에서 뒤로 append, 끝은 0xFF(아직 안 씀)</text>
  <rect x="20" y="40" width="80" height="40" class="f-acc" stroke="currentColor"/>
  <text x="30" y="58" fill="currentColor">magic</text><text x="30" y="74" fill="currentColor" font-family="monospace">+0</text>
  <rect x="100" y="40" width="70" height="40" class="f-acc" stroke="currentColor"/>
  <text x="108" y="58" fill="currentColor">seq</text><text x="108" y="74" fill="currentColor" font-family="monospace">+4</text>
  <rect x="170" y="40" width="80" height="40" class="f-acc" stroke="currentColor"/>
  <text x="178" y="58" fill="currentColor">src_seq</text><text x="178" y="74" fill="currentColor" font-family="monospace">+8</text>
  <rect x="250" y="40" width="80" height="40" class="f-acc" stroke="currentColor"/>
  <text x="258" y="58" fill="currentColor">hdr_crc</text><text x="258" y="74" fill="currentColor" font-family="monospace">+12</text>
  <rect x="330" y="40" width="90" height="40" class="f-warn" stroke="currentColor"/>
  <text x="338" y="58" fill="currentColor">commit</text><text x="338" y="74" fill="currentColor" font-family="monospace">+16</text>
  <rect x="420" y="40" width="130" height="40" class="f-mut" stroke="currentColor"/>
  <text x="428" y="58" fill="currentColor">레코드 레코드…</text><text x="428" y="74" fill="currentColor" font-family="monospace">+20</text>
  <rect x="550" y="40" width="170" height="40" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="560" y="64" fill="currentColor">0xFF … (빈 공간)</text>
  <text x="20" y="104" fill="currentColor">magic+seq+src_seq+crc = program 1번(16B). commit 워드는 컴팩션 완료 시 별도 program.</text>
  <text x="10" y="142" fill="currentColor" font-weight="bold">레코드 (전부 4바이트 정렬, 패딩은 0xFF)</text>
  <rect x="20" y="156" width="80" height="36" class="f-acc" stroke="currentColor"/><text x="28" y="179" fill="currentColor" font-family="monospace">key:16</text>
  <rect x="100" y="156" width="120" height="36" class="f-acc" stroke="currentColor"/><text x="106" y="179" fill="currentColor" font-family="monospace">tomb|batch|len</text>
  <rect x="220" y="156" width="90" height="36" class="f-warn" stroke="currentColor"/><text x="230" y="179" fill="currentColor" font-family="monospace">crc32</text>
  <rect x="310" y="156" width="240" height="36" class="f-mut" stroke="currentColor"/><text x="320" y="179" fill="currentColor">payload (값, 또는 배치 항목들)</text>
  <rect x="550" y="156" width="60" height="36" stroke="currentColor" fill="none" stroke-dasharray="4 3"/><text x="560" y="179" fill="currentColor" font-family="monospace">pad</text>
  <text x="20" y="214" fill="currentColor">CRC = crc32(key, raw, payload). 페이지 경계로 쪼개 써도 CRC 하나로 완결된다.</text>
  <text x="10" y="252" fill="currentColor" font-weight="bold">링: head 는 다음 빈 섹터로 이동, 컴팩션은 가장 오래된 섹터(seq 최소)에서 살아있는 값만 head 로 복사</text>
  <rect x="20" y="266" width="130" height="34" class="f-mut" stroke="currentColor"/><text x="30" y="288" fill="currentColor">S1 (seq 3) 옛것</text>
  <rect x="150" y="266" width="130" height="34" class="f-mut" stroke="currentColor"/><text x="160" y="288" fill="currentColor">S2 (seq 4)</text>
  <rect x="280" y="266" width="130" height="34" class="f-acc" stroke="currentColor"/><text x="290" y="288" fill="currentColor">S3 (seq 5) head</text>
  <rect x="410" y="266" width="130" height="34" stroke="currentColor" fill="none" stroke-dasharray="4 3"/><text x="420" y="288" fill="currentColor">S0 BLANK(예비)</text>
  <text x="20" y="322" fill="currentColor">불변식: BLANK 섹터는 항상 하나 이상 남긴다. 예비가 없으면 컴팩션을 시작할 곳이 없다.</text>
</svg>
```

| 파트 | 구현 | 체크 |
|---|---|---|
| A | NOR 물리 규칙, 레코드 인코딩/CRC, 페이지 분할, set/get/del | 물리 규칙 31개 체크(AND 프로그램, 위반 카운터, 0xFF 값, 빈 값, 삭제, 재마운트, CRC 비트 깨짐) |
| B | 섹터 분류, 마운트 복구, 링 컴팩션 프로토콜 | 찢어진 레코드·tombstone·용량 시나리오 + **모든 flash op × 찢어짐 모드 전수 주입** + 복구 중 2차 차단 |
| C | 원자적 배치, 부트 카운터/캘리브레이션 장치, 마모 | 배치 전수 주입, 8000회 부트 장치 시나리오, 마모 분포 |

### Part A — NOR 모델과 append-only 레코드

- `phys_prog`: AND로 프로그램하고, 0→1 시도를 `viol_0to1`에 세고, 같은 워드를 소거 전에 또 프로그램하면 `viol_reprog`를 센다. `phys_erase`: 섹터 전체를 `0xFF`로.
- `rec_encode`, `rec_read`(CRC 검증), `flash_write`(페이지 경계 분할), `kv_set / kv_get / kv_del`(RAM 인덱스 + tombstone).
- 하네스가 `nor_prog / nor_erase` 래퍼와 전원 차단 주입기를 준다.

### Part B — 마운트 복구와 컴팩션

- 헤더 `src_seq` + `commit` 워드: 일반 섹터는 `src_seq = NONE`. 컴팩션 목적지는 `src_seq = victim의 seq`로 열리고(COPYING), 복사가 끝난 뒤 `commit`에 **정확한 값**을 프로그램해야 비로소 커밋된다.
- 마운트: 섹터를 분류 → DEAD/COPYING/GARBAGE 재소거 → seq 순으로 재생 → 머리 위치와 "더러운 꼬리(dirty tail)" 판단.
- **전수 전원 차단**: 스크립트 워크로드의 모든 program/erase 호출마다, 가능한 모든 찢어짐 모드로 끊고 재마운트해서 불변식을 확인한다. 복구 과정(마운트 + 이어지는 op)에도 2차 차단을 넣는다.

### Part C — 펌웨어 시나리오

- `kv_batch_*`: 캘리브레이션 값 두 개를 **레코드 하나, CRC 하나**로 묶어 원자적으로 커밋한다(개수와 크기 제한 있음).
- 부트 카운터(+1씩), gain+offset = 1000 불변식, 무작위 전원 차단 부트 8000회.
- 뜨거운 키 하나만 6만 번 덮어써도 링이 cold 데이터까지 옮겨서 소거 횟수가 고르게 퍼지는지 확인한다.

## 2. Problem (English)

> **Power-fail-safe key-value store on NOR flash.**
>
> Flash model: sector erase sets all bits to 1 (`0xFF`); program can only clear bits (1→0); programming across a page boundary or at a misaligned address is an error. Implement `kv_mount`, `kv_set`, `kv_get`, `kv_del`, and `kv_batch_begin/set/del/commit` on an append-only log with per-record CRC and sector headers.
>
> ```c
> int kv_mount(kv_t *kv, nor_t *flash);
> int kv_set(kv_t *kv, uint16_t key, const void *val, uint16_t len);
> int kv_get(const kv_t *kv, uint16_t key, void *buf, uint16_t cap, uint16_t *out_len);
> int kv_del(kv_t *kv, uint16_t key);
> int kv_batch_begin(kv_t *kv); int kv_batch_set(...); int kv_batch_del(...); int kv_batch_commit(kv_t *kv);
> ```
>
> Requirements:
> 1. Compaction reclaims the oldest sector without ever losing a committed value. Keep at least one blank sector at all times.
> 2. Crash model: power can fail during **any** program or erase. A program may be partially applied (any prefix of bytes, partial bits of one byte, or random bits). An erase may be partially applied in *any* pattern, including a pre-program phase that leaves stale data followed by zeroes.
> 3. After every remount, every key equals its last committed value or the in-flight value (never garbage). A cut during recovery must be survivable too.
> 4. Every word is programmed at most once between erases (assume ECC flash that rejects re-programming).
>
> Follow-ups: what do real NOR datasheets guarantee? Why is the commit word in the destination sector? Exact commit value vs any-non-FF — why does it matter? How would you test this on real hardware? How do you bound worst-case `kv_set` latency? What changes on STM32L4 (8-byte ECC words) vs SPI NOR?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과(실제 출력, Part A 일부):

```text
== Part A: NOR 규칙 + 레코드 + set/get/del
FAIL starter.c:730  nor_prog(&f, 0, w4, 4) == NOR_OK && f.mem[0] == 0xF0 && f.mem[3] == 0x55
FAIL starter.c:734  f.mem[8] == (0xFFu & 0xF0u) && f.mem[10] == (0x0Fu & 0xAAu) && f.viol_reprog == 1u
FAIL starter.c:735  f.viol_0to1 >= 1u
PASS nor_prog(&f, 2, w4c, 4) == NOR_EPARAM && nor_prog(&f, 62, w4c, 4) == NOR_EPARAM && f.viol_param == v0 + 2u
FAIL starter.c:739  nor_erase(&f, 0) == NOR_OK && f.mem[0] == 0xFFu && f.erase_cnt[0] == 1u && f.wmap[0] == 0u
...
SKIP Part B: Part A failed
SKIP Part C: Part A failed
7/74 passed
```

하네스 설계 — 왜 이렇게 만들었는지:
- **NOR 모델은 규칙을 강제하고 위반을 센다.** `viol_0to1`(0을 1로 만들려는 시도), `viol_reprog`(소거 없이 같은 워드 재프로그램), `viol_param`(비정렬·페이지 경계 위반). 끝에서 전부 0이어야 한다. "데이터는 맞게 나왔지만 플래시 규칙을 어기며 얻었다"를 잡기 위해서다. wmap은 STM32L4처럼 ECC가 붙은 플래시의 "프로그램 1회" 규칙을 흉내 낸다.
- **트레이스로 op를 열거한다.** 고장 없는 골든 실행에서 각 op의 종류와 길이를 기록하고, op 번호와 찢어짐 모드를 조합해 주입한다. 그래서 `flash-cuts(1st)=N (armed N)`처럼 **모든 조합이 실제로 발화했는지** 확인한다.
- **스냅샷으로 2차 차단.** 1차 차단 직후 플래시 이미지를 저장해 두고, 복구 창의 op를 차례로 끊는다.
- `val_byte()`는 일부러 값 안에 `0xFF`, `0x00` 구간을 넣는다. 찢어진 레코드의 꼬리가 "우연히 맞아 보이는" 상황을 만들기 위해서다.
- 관찰용 파일: `demo_trace.c`(컴팩션 단계별 차단, Part B), `demo_wear.c`(WAF·마모·마운트 비용, Part C 끝).

## 4. 힌트

<details><summary>힌트 1 — phys_prog 와 wmap 은 왜 분리되어 있나</summary>

플래시 비트 규칙은 "AND"이고, ECC 규칙은 "워드당 한 번"이다. 둘은 별개다. byte 프로그램이 가능한 SPI NOR에서는 같은 워드를 두 번 프로그램해도 규칙 위반이 아니다(0으로만 내려가면 된다). ECC가 붙은 내장 플래시에서는 안 된다. 이 레슨의 설계는 후자도 만족시키려고 "모든 워드 한 번만"으로 짠다. 그래야 commit 워드, 헤더, 레코드가 어떤 파트에든 올라간다.
</details>

<details><summary>힌트 2 — 레코드 헤더의 CRC 는 언제 쓰는가</summary>

단일 CRC가 헤더(key, raw)와 payload 전체를 덮는다. CRC 필드는 레코드 앞쪽에 있지만, 찢어진 쓰기를 보면 "뒤쪽이 안 쓰인(=0xFF인)" payload와 CRC 값이 맞을 확률은 무시할 만큼 작다. 단, 안 쓴 바이트가 원래 0xFF여야 하는 값이었다면 그건 사실 완성된 레코드다. 그 경우는 복구 입장에서도 "완료"로 처리해도 안전하다. 여기서 시험하는 것은 "어떤 tear도 잘못된 값을 '성공'으로 만들지 않는다"이다.
</details>

<details><summary>힌트 3 — 첫 무효 레코드에서 멈춘 뒤 무엇을 하나</summary>

스캔이 멈춘 자리부터 섹터 끝까지 전부 0xFF인지 본다. 전부 0xFF면 여기가 다음 쓰기 위치(깨끗한 꼬리)다. 아니면 찢어진 레코드가 있다는 뜻이고, 이 섹터에는 더 이상 append하면 안 된다(이미 프로그램된 비트 위에 쓰면 AND로 망가진다). "다음 쓰기는 다음 섹터로"로 처리한다.
</details>

<details><summary>힌트 4 — 컴팩션의 순서</summary>

순서가 전부다. ① D에 COPYING 헤더(`src_seq = V.seq`) ② V의 live를 D로 복사 ③ `commit` 워드 프로그램 ④ V 소거. ②까지 끊기면 D를 버리고(V가 아직 권위 있는 원본), ③까지 끝났으면 V는 이미 죽은 것이다. ③이 **유일한 커밋 지점**이다. commit 전에 V를 소거하면 돈을 내기 전에 영수증을 찢는 것과 같다.
</details>

<details><summary>힌트 5 — 커밋이 끝난 V 를 그냥 재생하면 왜 안 되나</summary>

커밋 뒤 V의 live 레코드는 이미 D에 있으니, V를 재생해도 D가 나중에 덮어써서 괜찮아 보인다. 문제는 **tombstone** 이다. V 안에 `K=값` 레코드가 있고 뒤에 `K 삭제` tombstone이 있는데, 소거가 끊겨서 tombstone 쪽만 지워졌다고 하자. 재생하면 K가 되살아난다. D에는 K가 없으니(지워진 키는 복사하지 않는다) 덮어쓸 것도 없다. 그래서 `seq <= committed_src`인 섹터는 내용을 보지 않고 버린다(변이 M5).
</details>

<details><summary>힌트 6 — commit 값이 왜 0xFFFFFFFF도 0x00000000도 아니어야 하나</summary>

0xFFFFFFFF는 "아직 안 씀"이다. 0x00000000은 pre-program 단계의 부분 소거가 만들어 낼 수 있는 값이다(아래 Part B). `hdr_crc`에서 파생한 값에 bit0=1, bit1=0을 강제하면 그 두 값이 될 수 없다.
</details>

## 5. 풀이

### Part A — 레코드와 RAM 인덱스

레코드는 "헤더 + payload + 패딩"이고 전부 4바이트 정렬이다. 정렬은 STM32L4의 ECC 8바이트 경계 같은 하드웨어 제약의 축소판이다(코드의 `align4`는 4바이트 고정이다. 8바이트 파트로 옮기려면 정렬 단위와 헤더 크기를 함께 바꿔야 한다, Q5). 값이 페이지를 넘으면 `flash_write`가 나눠 쓴다. `nor_prog`는 페이지 경계를 거부한다. SPI NOR의 Page Program은 페이지 안에서 wrap-around하므로(대표적 사양; 파트별 데이터시트 확인), 드라이버가 나눠 쓰는 것이 올바른 방어다.

`flash_write`가 나눠 쓴 레코드가 중간에 끊기면 앞 페이지만 프로그램된 상태다. CRC가 그 조각을 무효로 만든다. 프로그램 호출이 몇 번으로 나뉘든 커밋은 CRC 일치 하나로 결정된다.

| 구성요소 | 크기/비용 | 왜 |
|---|---|---|
| 레코드 | 8B + 값(4B 정렬) | key 16비트, raw 16비트(tomb/batch/len), crc32 |
| RAM 인덱스 | 키당 8B × `KV_MAX_KEYS`(32) | 읽기 O(n) 선형 탐색이지만 n이 작다(STOR-B05의 Bitcask keydir와 같은 아이디어) |
| 쓰기 | program 1~2회(페이지 경계) + 컴팩션 비용(아래) | |
| 읽기 | 인덱스 → `nor_read` 한 번 | 값에 CRC 재검사 없음(마운트에서 이미 검증) |

`0xFF`는 데이터로도 유효하다. 값이 전부 `0xFF`인 레코드도 CRC가 맞으면 정상이다. 빈 값(`len=0`)도 유효하다. 플래그 비트(tomb, batch)는 `raw`의 상위 비트이며 CRC가 덮는다.

### Part B — 무엇이 원자적인가

**이 레슨의 가정을 먼저 쓴다.**

| 가정 | 내용 | 실제 하드웨어와의 관계 |
|---|---|---|
| 내구성 | `nor_prog`/`nor_erase`가 정상 리턴하면 그 결과는 전원이 끊겨도 남는다 | SPI NOR는 WIP(Write In Progress)가 클리어될 때까지 기다려야 한다. 메모리맵 XIP 경로의 쓰기 버퍼나 캐시가 있으면 별도 flush가 필요하다 |
| 프로그램 찢어짐 | 어떤 prefix든, 어떤 비트 조합이든 가능(`F_P_PREFIX/PARTIAL/RANDOM`) | 내가 확인한 범위에서 NOR 데이터시트는 전원 차단 중 프로그램 결과를 보장하지 않는다(특정 파트 미확인) — 보수적으로 "아무 비트나" 가정한다 |
| 소거 찢어짐 | 앞/뒤 일부 소거, 뒤쪽 0x00(`SUFFIX_ZERO`, pre-program 단계), 임의 비트 | 실제 NOR의 임베디드 소거 알고리즘은 소거 전에 전부 0으로 프로그램하는 단계를 거치는 것으로 보고된다. linux-mtd 2009 스레드에서 Spansion 확인 내용이 인용됐고, 끊긴 블록 끝쪽이 0으로 채워진 덤프가 올라왔다(일화적 근거). 그래서 `SUFFIX_ZERO`가 이 레슨의 핵심 모드다 |
| 안 하는 가정 | 레코드 하나가 원자적이다 / 소거가 원자적이다 / 0xFF인 칸은 진짜 빈 칸이다(이건 전체 검사로 확인) | 모델은 이 셋을 일부러 깨뜨린다 |
| 모델 밖 | 불안정(marginal) 셀: 프로그램·소거가 부족해서 지금은 맞게 읽혀도 나중에 뒤집히는 셀, 자발적 비트 플립(bit rot) | Q&A 1에서 논의. CRC는 뒤집힘을 **감지**할 뿐 복구하지 못한다 |

**섹터 분류와 복구 프로토콜.**

| 상태 | 판별 | 마운트 행동 |
|---|---|---|
| BLANK | 헤더 무효 + 섹터 **전체** 0xFF | 예비 |
| ACTIVE | 헤더 유효 + (`src_seq == NONE` 또는 commit 정확) | 재생 |
| COPYING | 헤더 유효 + `src_seq != NONE` + commit 값이 정확하지 않음 | 소거 후 BLANK(폐기) |
| DEAD | 헤더 유효 + `seq <= 커밋된 src_seq 최댓값` | 소거 후 BLANK |
| GARBAGE | 헤더 무효 + 섹터가 전부 0xFF는 아님 | 소거 후 BLANK |

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">컴팩션 — 어느 지점에서 끊겨도 커밋된 값은 V 또는 D 중 한 곳에 온전히 있다</text>
  <text x="20" y="48" fill="currentColor">V (victim, seq=s)</text>
  <text x="400" y="48" fill="currentColor">D (목적지)</text>
  <rect x="20" y="56" width="340" height="28" class="f-mut" stroke="currentColor"/>
  <text x="30" y="75" fill="currentColor">로그(옛값 + live 섞임) — 끝까지 건드리지 않는다</text>
  <rect x="400" y="56" width="320" height="28" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="410" y="75" fill="currentColor">전부 0xFF (예비)</text>
  <g fill="currentColor">
    <text x="20" y="116">① D 헤더 program</text><text x="210" y="116">src_seq = s, commit = 0xFF..</text><text x="430" y="116">끊기면 GARBAGE → 소거</text>
    <text x="20" y="144">② V 의 live 복사</text><text x="210" y="144">D 는 COPYING</text><text x="430" y="144">끊기면 COPYING → 폐기, V 가 원본</text>
    <text x="20" y="172">③ commit 워드 program</text><text x="210" y="172">정확한 값 = 커밋 지점</text><text x="430" y="172">찢어지면 "커밋 아님" → ②와 동일</text>
    <text x="20" y="200">④ V 소거</text><text x="210" y="200">V 는 이미 DEAD</text><text x="430" y="200">끊겨도 V 는 seq 로 DEAD → 재소거</text>
  </g>
  <rect x="20" y="222" width="700" height="72" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="30" y="242">핵심: 커밋 판단의 근거가 "지워지는 섹터" 안에 없다. commit 은 D 에 있고, D 는 지금 소거되지 않는다.</text>
    <text x="30" y="262">victim V 의 어떤 부분 소거 결과(앞만 소거, 뒤쪽 0x00, 무작위 비트)도 D 의 commit 을 바꿀 수 없다.</text>
    <text x="30" y="282">V 에 "retired" 표시를 하는 설계는 부분 소거가 그 표시를 지울 수 있어 위험하다.</text>
  </g>
  <text x="10" y="326" fill="currentColor" font-weight="bold">롤오버 (예비 ≥ 2): 새 섹터를 src=NONE 으로 열고 끝. 예비가 하나면 위 컴팩션을 먼저 한다.</text>
  <text x="10" y="350" fill="currentColor">트리거는 "head 에 다음 레코드가 안 들어감" 또는 "head 꼬리가 더러움(dirty tail)".</text>
  <text x="10" y="374" fill="currentColor">kv_set 은 컴팩션이 끝난 뒤에야 사용자 레코드를 쓴다 → 끊겨도 in-flight 는 옛값으로 남는다.</text>
</svg>
```

**왜 "커밋 값은 정확해야 한다"인가.** `commit != 0xFFFFFFFF`면 커밋으로 친다고 해 보자. 소거가 중간에 끊긴 섹터를 생각하면 이렇게 깨진다.

> 복구 마운트가 COPYING 목적지를 폐기하려고 소거하는 **도중에**(1차 차단 다음의 2차 차단), 전원이 다시 끊긴다. 소거의 pre-program 단계가 뒤쪽부터 0x00을 채운다고 하자. 앞 16바이트(헤더)는 멀쩡한데 **commit 워드는 0x00000000**이 된다. 다음 마운트는 "커밋됨!"으로 읽고, 멀쩡한 V를 DEAD로 선언해 지운다. 데이터가 사라진다.

아래 변이 실험(M1)이 실제로 잡아낸 장면이다. 수정 코드와 대조군 모두 실제 출력이다.

```text
(변이 M1: commit 판단을 "cw != 0xFFFFFFFF" 로 바꾼 사본)
  [bad L2] state mismatch after remount (1st cut=68 mode=6 k=16)
  4x512: ops=180 flash-cuts(1st)=14885 (armed 14885) cuts-in-recovery(2nd)=36291 bad=27
FAIL ...  x.bad == 0 && x.cuts1 == x.armed && x.cuts1 > 5000u && x.cuts2 > 5000u
```

`L2`(2차 차단), `mode=6`(F_E_SUFFIX_ZERO), `k=16`(앞 16바이트만 생존)이다. 계측 사본의 덤프에서 COPYING 섹터의 `commit=00000000`과 키 1, 3, 5의 실종을 확인했다. 한 번의 전수 스캔에서 27건이 잡혔는데, 1차 차단만 시험했다면 영영 몰랐을 경우다. 고친 규칙은 commit을 `hdr_crc`에서 파생한 **정확한 32비트 값**과 비교하는 것이다. 결과적으로 찢어진 commit은 **커밋이 아닌 것**으로 처리되고(②로 돌아감), 이것 역시 안전하다. commit을 쓰기 시작했다는 것은 복사가 끝났다는 뜻이지만, 복사가 끝났어도 커밋을 "안 했다"고 다시 판단하면 복사를 반복할 뿐 데이터는 잃지 않는다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">그림 3 — COPYING 섹터를 지우다 끊긴 모양 (SUFFIX_ZERO, k=16)</text>
  <rect x="20" y="40" width="340" height="30" class="f-acc" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor">헤더 16B: magic, seq, src_seq, hdr_crc (전부 유효)</text>
  <rect x="360" y="40" width="90" height="30" class="f-warn" stroke="currentColor"/>
  <text x="368" y="60" fill="currentColor">commit=0</text>
  <rect x="450" y="40" width="270" height="30" class="f-warn" stroke="currentColor"/>
  <text x="460" y="60" fill="currentColor">… 0x00 0x00 0x00 …</text>
  <g fill="currentColor">
    <text x="20" y="100">"commit != 0xFF" 디코더 → 커밋됨 → V 를 DEAD 로 선언 → V 소거 → 복사본은 없다 → 데이터 소실</text>
    <text x="20" y="126">"commit == 정확한 값" 디코더 → 커밋 아님 → COPYING → 이 섹터를 다시 소거 → V 그대로 → 안전</text>
    <text x="20" y="162">소거는 원자적이지 않다. 이 모양은 NOR 소거의 pre-program 단계에서 나올 수 있다.</text>
    <text x="20" y="182">linux-mtd 스레드에 뒤쪽부터 0 으로 채워진 덤프가 보고됐다(일화적 근거). 파트별 상세는 데이터시트 확인.</text>
    <text x="20" y="212">일반 규칙: "지금 지워지고 있는 섹터의 내용을 커밋 근거로 쓰지 않는다."</text>
  </g>
</svg>
```

**더러운 꼬리(dirty tail)와 예비 섹터의 충돌.** 찢어진 레코드 뒤로는 append할 수 없다. 그 섹터의 남은 공간은 다음 컴팩션까지 버린다(`head_dirty`). 예비가 하나뿐이면 어떻게 되나? 그 섹터를 다음 쓰기 때 "꽉 찬" 것으로 처리해서 일반 컴팩션 경로를 타면 된다. D를 COPYING으로 열고, V(가장 오래된 섹터)의 live를 복사한다. V가 곧 dirty head일 수도 있다(2섹터 구성). 그래도 `rec_read`는 첫 무효 레코드에서 멈추므로 live 레코드는 전부 그 앞에 있고, 복사된다. 전수 주입의 2섹터 시나리오가 이것을 돌린다.

**복구 중 2차 차단.** 복구 마운트는 소거를 수행한다. 그 소거가 또 끊겨도 된다. 분류는 플래시의 현재 상태만 보므로, 끊긴 뒤 마운트를 다시 돌리면 같은 판단을 다시 한다. 복구가 진행 표시 없이 **멱등(idempotent)** 하게 돈다는 뜻이다. COPYING 섹터를 소거하다 끊기면 다음 마운트는 그것을 COPYING 또는 GARBAGE로 다시 보고, 어느 쪽이든 또 지운다.

실제 출력(solution.c, 전체 74/74 PASS, 1.1 s):

```text
== Part B: 마운트 복구 + 링 컴팩션 + 전수 전원 차단
  stress: 200000 ops, 8x1024 NOR, erases=5945, prog_bytes=5974884
  4x512: ops=180 flash-cuts(1st)=14885 (armed 14885) cuts-in-recovery(2nd)=36347 bad=0
    copying-discard=8097 dead=1274 garbage=2520 dirty-tail=60087 | in-flight old=47141 new=4048
  2x512: ops=120 flash-cuts(1st)=15171 (armed 15171) cuts-in-recovery(2nd)=35074 bad=0
    copying-discard=22194 dead=1360 garbage=2610 dirty-tail=21535 | in-flight old=48158 new=2044
```

읽는 법:
- `flash-cuts(1st)`: 골든 런의 모든 op × 그 op에 가능한 찢어짐 모드 조합 수. `armed`와 같다는 것은 **모든 조합이 실제로 발화했다**는 뜻이다(주입이 안 먹힌 조합 없음).
- `cuts-in-recovery(2nd)`: 1차 차단 뒤의 복구 창(재마운트 + 이어지는 사용자 op 24개, 2섹터 구성은 16개)에서 처음 40개까지의 flash op마다 3~4모드로 2차 차단.
- `copying-discard / dead / garbage / dirty-tail`: 각 복구 경로가 실제로 실행된 횟수. 전부 0보다 크다는 CHECK가 있다. "테스트가 그 경로를 통과하지 않았다"는 위험을 막는 장치다.
- `in-flight old/new`: 복구 후 진행 중이던 op가 옛값으로 보인 횟수와 새값으로 보인 횟수. 찢어짐이 레코드 끝까지 끝났거나(그 뒤로는 0xFF 패딩만 남은 경우) op 완료 직후 끊긴 경우가 `new`다. 둘 다 불변식 위반이 아니다.

**한 번의 컴팩션을 단계별로 끊어 보기.** `demo_trace.c`가 첫 컴팩션(cold 키 2개 + hot 키 1개, 4×512)의 각 단계에서 전원을 끊고, 재마운트가 본 것을 그대로 출력한다.

```text
trace: total flash ops=115; compaction: header op=59, commit op=63 (len 4), erase op=64
cut point                      what mount saw
D header torn (10/16 B)        copying=0 dead=0 garbage=1 dirty=0 | head=S2  key1(cold)=ok key3(hot)=0x50
copying first live rec         copying=1 dead=0 garbage=0 dirty=0 | head=S2  key1(cold)=ok key3(hot)=0x50
commit torn (2/4 B)            copying=1 dead=0 garbage=0 dirty=0 | head=S2  key1(cold)=ok key3(hot)=0x50
commit complete, then cut      copying=0 dead=1 garbage=0 dirty=0 | head=S3  key1(cold)=ok key3(hot)=0x50
erase V: pre-program k=64      copying=0 dead=1 garbage=0 dirty=0 | head=S3  key1(cold)=ok key3(hot)=0x50
erase V: suffix erased k=64    copying=0 dead=1 garbage=0 dirty=0 | head=S3  key1(cold)=ok key3(hot)=0x50
erase V: random bits           copying=0 dead=0 garbage=1 dirty=0 | head=S3  key1(cold)=ok key3(hot)=0x50
erase V: complete, then cut    copying=0 dead=0 garbage=0 dirty=0 | head=S3  key1(cold)=ok key3(hot)=0x50
```

빌드: `cc -std=c11 -O2 -w demo_trace.c -o dt && ./dt` (solution.c 를 include 한다).

(각 줄 뒤의 `check=ok`는 생략했다. 전부 ok였다.) 읽는 순서는 위에서 아래로 시간 순이다.
- commit 전(위 세 줄)에는 `head=S2`다. 목적지 S3을 버리고, 이전 머리 S2로 돌아간다. 값은 전부 그대로다.
- commit이 끝난 줄부터는 `head=S3`다. 같은 차단인데 어느 쪽으로 돌아갔는지가 commit 한 워드로 갈린다. 이것이 커밋 지점이 **한 곳**이라는 말의 의미다.
- 소거 도중 세 모드를 보면, 앞쪽 헤더가 남은 부분 소거(앞 두 모드)에서는 `dead=1`, 임의 비트로 헤더 CRC가 깨진 경우는 `garbage=1`이다. 판단 경로는 달라도 결과(재소거 + 커밋된 값 유지)는 같다.
- `commit torn (2/4 B)`는 커밋이 안 된 것으로 처리된다. 복사는 다 끝났지만 한 번 더 복사하면 될 뿐이다.

**설계 판단: 왜 이 모양인가.** 면접에서 "다른 방식도 있지 않나"에 답할 표다. 비교 대상의 세부는 각 자료 기준이고, 숫자는 이 레슨 코드에서 나온 것만 쓴다.

| 질문 | 이 레슨의 선택 | 다른 선택과 그 비용 |
|---|---|---|
| 컴팩션 범위 | 링(가장 오래된 섹터 하나씩) | 2페이지 스왑(ST AN2594류): 코드가 단순하지만 live 전체를 매번 옮긴다. 이 레슨 코드에서 N=2로 시험한 구성과 같은 형태다 |
| 커밋 표시 위치 | 목적지 헤더의 commit 워드 | 쓰기 섹터에 gc-done 표시(Zephyr NVS), victim에 상태 표시(부분 소거에 취약, 그림 3) |
| 레코드 배치 | 앞에서 뒤로 append, CRC 포함 | 인덱스 항목(ATE)을 섹터 뒤에서 앞으로 쌓기(Zephyr NVS): 스캔이 항목 단위로 빠르지만 형식이 복잡하다 |
| 조회 | RAM 인덱스(키당 8 B, 최대 32키) | 플래시를 거꾸로 스캔: RAM이 적게 들지만 읽기가 O(로그)다 |
| 찢어진 꼬리 | 다음 쓰기를 새 섹터로 | 동기 워드로 건너뛰기(연습 4): 공간은 아끼지만 레코드당 4 B 추가, 잔해 레코드를 유효로 읽을 위험 |
| 커밋 값 | `hdr_crc`에서 파생한 정확한 값 | "0xFF만 아니면": 부분 소거에 속는다(변이 M1) |

**연산 비용 정리.** 고정 크기 구조이므로 말로 쓴다. n = 키 수(최대 32), L = 로그 바이트, U = 섹터 사용 영역이다.

| 연산 | 플래시 | RAM/CPU |
|---|---|---|
| `kv_get` | read 1회 | 선형 탐색 O(n) |
| `kv_set` (자리 있음) | program 1~수 회(페이지 경계) | O(n) + CRC O(값 길이) |
| `kv_set` (컴팩션 발생) | 위 + live 복사(최대 U바이트 program) + commit + **erase 1회** | 최악 지연은 소거 시간에 좌우된다 |
| `kv_mount` | 전체 섹터 읽기, 재소거 최대 N회(보통 0~1회) | CRC O(L) |
| 공간 | 예비 섹터 1개 + `cap` 여유 | 인덱스 256 B(8×32) + 버퍼 약 400 B(rbuf/wbuf/bbuf) |

**tombstone과 컴팩션.** 컴팩션은 tombstone을 복사하지 않는다. V는 가장 오래된 섹터이므로 V 안의 tombstone이 가리킬 수 있는 레코드는 V 안에만 있다. 더 오래된 섹터가 없다. tombstone 없이 V를 지워도 V의 옛값들은 같이 사라진다. 반면 V의 tombstone을 **커밋 전에 지우면 안 되는** 이유는 아래 §7 실수 5에 있다. 한편 중간 섹터의 tombstone은 그 섹터가 V가 될 때까지 남는다. 그 동안은 더 오래된 섹터에 옛값이 있을 수 있으므로 tombstone이 필요하다.

**용량과 진행 보장.** `cap = (N-2) × (섹터 사용 영역 − MAX_REC)`로 미리 잘라 둔다. 근거는 다음과 같다. 예비 1개를 빼면 로그 섹터가 N−1개다. 살아있는 바이트가 cap 이하라면, 최악의 경우에도 컴팩션을 최대 N−1번 돌면 어떤 D는 V의 live를 담고도 레코드 하나가 들어갈 자리가 남는다(모든 D가 꽉 찼다면 live 합이 (N−1)(U−MAX_REC)를 넘어야 하고, 이는 cap과 모순). 2섹터 구성은 N−2=0이라 cap을 U−MAX_REC로 따로 둔다. 시험: cap까지 채우면 `ENOSPC`, 플래시는 건드리지 않는다, 삭제하면 다시 들어간다.

### Part C — 배치, 장치, 마모

**원자적 배치.** "gain과 offset을 함께 갱신"을 생각해 보자. 두 번의 `kv_set` 사이에 끊기면 gain은 새 값, offset은 옛값이 된다. 해법은 두 키를 **한 레코드**에 넣는 것이다. payload가 `{key16, raw16, rsvd32, 값}` 나열이고 CRC가 전체를 덮는다. CRC가 맞으면 전부 보이고, 안 맞으면 하나도 안 보인다. 인덱스는 항목별로 값의 오프셋을 가리킨다. 컴팩션은 **항목 단위로** live를 판별해 단독 레코드로 복사한다(배치의 나머지 항목은 이미 다른 값으로 덮였을 수 있다). 항목 헤더를 일반 레코드 헤더(8B)와 같은 크기로 둔 이유는, live 항목들을 단독 레코드로 옮겨도 크기가 커지지 않게 하기 위해서다. 그래서 V의 live가 D 한 섹터 안에 들어간다는 보장이 깨지지 않는다.

```text
== Part C: 원자적 배치 + 부트 카운터/캘리브레이션 + 마모
  batch 4x512: ops=160 flash-cuts(1st)=14585 (armed 14585) cuts-in-recovery(2nd)=33908 bad=0
    copying-discard=5929 dead=1210 garbage=2407 dirty-tail=59893 | in-flight old=44948 new=3502
  device: boots=8000 cuts=1712 final boot counter=7247 erases=[231 220 228 218 221 219]
  wear: 60000 hot sets, erases min=256 max=257, prog_bytes=2079172 (hot+cold user bytes=1440320)
```

장치 시나리오에서 1712번의 전원 차단 뒤에도 부트 카운터는 0 또는 +1씩만 움직였고, `gain+offset`은 언제나 1000이었다. `erases`가 퍼져 있는 것은 잦은 차단이 복구 소거를 더 만들기 때문이다(그래도 범위는 218–231).

**링 회전의 공짜 마모 평탄화.** 항상 가장 오래된 섹터를 비우므로 cold 데이터도 언젠가는 옮겨진다. `wear` 시험에서 뜨거운 키 하나를 6만 번 덮어써도 소거 횟수 차이는 1이다. FTL의 greedy GC는 이것을 공짜로 얻지 못한다(cold 블록이 영원히 안 지워진다; DS-A09). 대가는 cold 데이터를 계속 복사하는 쓰기 증폭이다.

**WAF(쓰기 증폭) 실측.** `demo_wear.c`로 측정했다(WAF = 프로그램한 전체 바이트 / 사용자 레코드 바이트, 사용자 쓰기 3만 회, 숫자는 결정적). 분석 공식은 쓰지 않았다. 측정값만 본다.

| 구성 | live 키 | 채움률 | WAF | 소거/1k set | 소거 최소/최대 |
|---|---|---|---|---|---|
| 8×512 | 2 | 7% | 1.07 | 170.5 | 639/640 |
| 8×512 | 8 | 27% | 1.26 | 199.8 | 749/750 |
| 8×512 | 17 | 57% | 1.69 | 269.0 | 1008/1009 |
| 8×512 | 23 | 78% | 2.20 | 349.8 | 1311/1312 |
| 8×512 | 29 | 98% | 3.14 | 499.8 | 1874/1875 |
| 4×512 | 8 | 81% | 1.71 | 272.7 | 2045/2045 |
| 16×512 | 8 | 12% | 1.13 | 180.3 | 338/338 |

해석은 측정한 범위 안에서만 한다. 채움률이 올라가면 WAF와 소거 수가 함께 커진다(98%에서 WAF 3.14). 같은 live 데이터(8키)에서 섹터를 늘리면(4→16) WAF가 1.71→1.13으로 내려간다. 섹터가 많을수록 한 번의 컴팩션이 옮기는 live가 평균적으로 적어지기 때문으로 읽힌다. 다만 이 수치는 1 hot + 나머지 cold 부하에서만 잰 것이다. 다른 쓰기 패턴에서도 같다고 말하지 않는다.

마운트 비용(8×4096, 호스트 기준, 5회 측정의 중앙값을 3회 실행한 값의 중앙값; 스프레드는 실행별 중앙값 범위):

| 재생한 로그 바이트 | 유효 섹터 | 마운트 µs | 범위 |
|---|---|---|---|
| 2016 | 1 | 9.8 | 9.3–9.8 |
| 8064 | 2 | 14.0 | 13.2–14.5 |
| 16056 | 4 | 20.2 | 19.0–21.0 |
| 28008 | 7 | 29.4 | 28.1–29.8 |

이 맥은 CRC가 빠르고 flash가 RAM이다. MCU에서는 SPI 읽기 속도와 CRC 비용이 지배한다. 절대값은 의미가 없고, **마운트 시간이 로그 바이트에 비례한다**는 것만 읽는다. 그래서 큰 로그는 마운트가 느려진다. 이 비용과 체크포인트(summary)의 복잡도를 맞바꾸는 것이 실제 설계 결정이다.

```bash
cc -std=c11 -O2 -w demo_wear.c -o dw && ./dw     # 위 표의 원본 (solution.c 를 include)
```

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 74/74 passed, 1.1 s
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o s_asan && ./s_asan   # 74/74, 3.2 s, 진단 메시지 0바이트
```

검증 결과(직접 실행): 일반 빌드 74/74, ASan+UBSan 클린. 테스트 규모는 새니타이저에서도 줄이지 않았다. 일반 빌드 1.1 초, 새니타이저 빌드 3.2 초다.

변이 주입: 사본에 버그를 하나씩 심어 하네스가 잡는지 확인했다(원본 solution.c는 수정하지 않음).

| 변이 | 결과 |
|---|---|
| M1 commit: 0xFF만 아니면 커밋 | FAIL 3건(4x512 bad=27), 복구 중 2차 차단이 잡음 (위 그림 3) |
| M2 빈 섹터 판별을 헤더만으로 | FAIL 3건: 뒤쪽이 지워지지 않은 섹터 위에 프로그램 |
| M3 dirty tail 무시하고 append | Part A 체크 실패 (`kv2.head_dirty` 기대) |
| M4 COPYING 섹터를 폐기 안 하고 재생 | FAIL 5건 |
| M5 DEAD 판정 제거 | FAIL 6건 |
| M6 레코드 CRC 검사 제거 | Part A 실패 |
| M7 복사 전에 commit | FAIL 10건 |
| M8 commit 전에 V 소거 | FAIL 6건 |
| M9 인덱스 확인 없이 복사(옛값까지 복사) | FAIL 8건 |
| M10 kv_set에서 append 전에 인덱스 갱신 | **통과(74/74)** — 전원 모델에서는 RAM 인덱스가 전원과 함께 사라지므로 동등 변이다. ISR에서 읽는 reader가 있었다면 다른 문제가 된다 |
| M11 재마운트 시 꼬리를 무시하고 헤더 뒤부터 append | Part A 실패 |
| M12 replay가 tombstone 무시 | Part A 실패 |
| M13 배치 플래그 안 씀 | 배치·장치 시험 실패 |
| M14 `next_seq`에서 +1 누락 | Part A 실패 (`seq == 1` 체크) |

## 7. 자주 하는 실수

1. **`commit != 0xFFFFFFFF`면 커밋으로 친다.** 변이 M1과 그림 3이 보여 주듯, 부분 소거(pre-program)로 commit이 0이 되면 V를 죽인다. → 커밋은 **정확한 값**으로만. 찢어진 commit은 "커밋 안 됨"으로 처리해도 안전하다.
2. **V에 "retired" 플래그를 쓴다.** V는 소거될 섹터다. 부분 소거가 그 플래그를 지우거나 만들 수 있다. 커밋 근거는 지워지지 않는 섹터(D)에 둔다.
3. **빈 섹터 판별을 헤더만 본다.** 뒤쪽이 지워지지 않은 섹터(앞부터 소거되다 끊김)에 새 헤더와 레코드를 쓰면 AND로 망가진다. 변이 M2에서 이 형태가 잡혔다. → 섹터 전체가 0xFF일 때만 BLANK.
4. **찢어진 레코드 뒤에 계속 append.** 다음 append는 그 자리부터 시작하므로, 이미 일부 프로그램된 비트 위에 AND로 얹힌다. 전 레코드도 새 레코드도 망가진다. → 첫 무효 레코드 뒤가 0xFF가 아니면 `head_dirty`, 다음 쓰기는 새 섹터로.
5. **tombstone을 컴팩션이 항상 지워도 된다고 믿는다.** V가 **가장 오래된** 섹터일 때만 안전하다. 재생 순서(seq 오름차순)가 근거다. 중간 섹터를 먼저 컴팩션하도록 바꾸면 더 오래된 섹터의 옛값이 tombstone 없이 되살아난다. 변이 M9(인덱스 확인 없이 복사)도 같은 부류로, 옛값이 새 위치로 옮겨져 최신값을 덮는다.
6. **컴팩션 복사 중 사용자 쓰기를 끼워 넣는다.** D에 사용자 레코드가 섞이면 "COPYING 폐기"로 사용자 레코드도 사라진다. 이 설계는 컴팩션이 끝난 뒤에만 사용자 레코드를 쓴다. 선점형 RTOS라면 저장소 뮤텍스로 보호한다(컴팩션 중 ISR에서 직접 `kv_set` 금지).
7. **페이지 경계를 안 나눈다.** SPI NOR의 Page Program은 페이지 끝을 넘으면 페이지 앞으로 wrap하는 것이 대표적 동작이라, 앞쪽 데이터를 덮어버린다. 드라이버에서 분할하고, 모델이 거부하게 해서 잡는다(§3의 `viol_param`).

## 8. 면접 Q&A

<details><summary>Q1. NOR의 program/erase에서 무엇이 원자적인가요? 이 설계는 무엇을 가정하나요?</summary>

아무것도 원자적이라고 가정하지 않는다. 프로그램은 임의의 prefix·비트 조합으로 찢어지고, 소거는 앞/뒤/임의 비트로 찢어지며 0x00 구간(pre-program)도 나온다고 모델링했다. 데이터시트가 보장하는 것은 "전원이 유지된 채 WIP가 클리어되면 완료"이지, 끊긴 경우의 결과가 아니다(내가 확인한 범위; 파트별 확인 필요). 그래서 **원자성은 CRC와 commit 워드로 소프트웨어에서 만든다**: 레코드 하나는 CRC 일치가 커밋 지점이고, 컴팩션은 목적지 헤더의 commit 워드가 커밋 지점이다.

모델 밖의 위험이 둘 있다. ① **불안정(marginal) 셀**: 프로그램이나 소거가 충분히 안 되어 지금은 맞게 읽혀도 나중에 바뀌는 셀. CRC가 지금 맞았어도 나중에 깨질 수 있다. 전원 복구 후 "방금 끊긴 그 위치"를 다시 쓰거나(read-back verify), 재마운트로 재평가하는 방식이 있다. ② **bit rot**: 시간에 따른 비트 뒤집힘. CRC가 감지는 하지만 복구하지 못한다(FEC나 다중 사본이 필요). 이 레슨은 ②에 대해 첫 무효에서 멈추는 스캔이라 뒤쪽 레코드도 잃는다. 연습 4의 재동기화 스캔이 그 대안이다.
</details>

<details><summary>Q2. 컴팩션 커밋 표시를 왜 목적지 섹터에 두나요? victim에 "retired" 표시를 하면 안 되나요?</summary>

victim은 곧 소거될 섹터다. retired 표시를 쓰고 소거를 시작해서 끊기면, 부분 소거가 그 표시를 지울 수 있다(뒤에서부터 0으로 채우는 단계든 앞에서부터 지우는 단계든). 표시가 사라진 V는 "살아 있는 섹터"로 재생되고, 옛값이 되살아난다. 커밋 근거를 **지금 지워지지 않는 곳**(D)에 두면 V의 어떤 부분 소거 패턴도 판단을 바꾸지 못한다. Zephyr NVS(v3.7.0 `nvs.c`)도 GC가 끝나면 쓰기 섹터에 gc-done 항목을 쓰고, 시작할 때 다음 섹터가 비어 있지 않으면 이 표시를 찾아 없으면 쓰기 섹터를 지우고 GC를 다시 한다(`nvs_add_gc_done_ate`, `nvs_startup`에서 확인). 같은 원리다.
</details>

<details><summary>Q3. CRC는 record 끝에 두나요, 앞에 두나요? 레코드를 두 번에 나눠 쓰면 안 되나요?</summary>

CRC의 위치는 무결성에 영향이 거의 없다. 이 레슨은 파싱을 위해 앞(`+4`)에 두었다. 나눠 쓰면(payload 먼저, CRC 나중에) 좋은 점도 있다: payload가 다 쓰이기 전까지 CRC 칸이 0xFF라 "아직 안 끝남"이 바이트 하나로 드러난다. 다만 찢어진 CRC 쓰기를 또 생각해야 한다. 이 레슨처럼 하나의 CRC로 전 영역을 덮으면 어떤 순서로 찢어져도 불일치로 끝난다. 주의할 함정 하나: CRC-32로 "전부 0"과 "전부 0xFF" 영역을 검사해야 한다. 초기값 `~0`을 쓰는 표준 CRC-32는 0으로 찬 입력의 결과가 0이 아니고(이 레슨은 `rec_read`로 시험), 전부 0xFF인 헤더는 예약 키로 먼저 거절한다.
</details>

<details><summary>Q4. kv_set의 최악 지연은 얼마이고, 어떻게 줄이나요?</summary>

최악은 "head가 꽉 참 → 컴팩션 한 번"이다. 컴팩션 한 번은 live 복사(프로그램 수십 회) + commit 프로그램 + **섹터 소거**다. NOR 4 KB 소거는 파트에 따라 수십 ms 이상이다(정확한 값은 데이터시트의 tSE로 확인). 그동안 호출자는 멈춘다. 줄이는 방법: ① **백그라운드 컴팩션**: 예비 섹터가 부족해지기 전에 낮은 우선순위 태스크에서 미리 돌린다. 단, 이 레슨의 전수 시험이 가정한 "한 번에 한 컴팩션"을 깨지 않아야 한다. ② 소거를 쪼갠다(erase suspend 지원 파트). ③ 임계 쓰기와 저장소를 분리한다: 실시간 경로는 RAM 큐에만 쓰고 플러시 태스크가 flash에 넣는다. 다만 전원 차단 시 RAM 큐의 데이터는 잃는다. 이 레슨은 ②③을 구현하지 않았다.
</details>

<details><summary>Q5. STM32L4 같은 내장 플래시에서는 무엇이 달라지나요?</summary>

ST의 STM32L4 리퍼런스 매뉴얼 기준, 프로그램은 double-word(64비트 데이터 + 8비트 ECC) 단위이고, 이미 프로그램된 주소에 다시 쓰는 것은 "데이터가 전부 0인 경우를 제외하면 허용되지 않고" PROGERR 플래그가 선다(검색 결과의 매뉴얼 발췌로 확인; 페이지 번호는 원문을 볼 것). 따라서 정렬 단위가 8바이트여야 하고, 같은 워드를 두 번 프로그램하면 안 된다. 이 레슨의 설계는 "모든 워드 한 번만"이므로 그대로 옮길 수 있다(align 8, 헤더 24B, commit도 자기 double-word 하나). 대신 **연습 1의 비트 단위 단항 카운터는 그런 파트에서 쓸 수 없다**(같은 워드 재프로그램이 금지). 쓰기 단위가 다른 부품으로 옮길 때는 `align`만 바꾸는 것이 아니라 "한 번만 프로그램" 가정을 먼저 확인해야 한다.
</details>

<details><summary>Q6. 전수 시험이 정말 전수인가요? 뭘 빠뜨렸나요?</summary>

"스크립트 워크로드의 모든 program/erase 호출 × 정의한 찢어짐 모드"에 대해 전수다(4x512 1차 14885회, 복구 중 2차 36347회). 빠뜨린 것은 다음과 같다. (a) 모드는 내가 정의한 집합이다. 실제 부분 프로그램의 비트 패턴 분포는 모른다(무작위 3시드와 prefix는 대표 표본이다). (b) 스크립트 하나의 경로만 본다. 그래서 스크립트를 여러 개(2섹터, 4섹터, 배치)로 돌리고, 장치 시나리오에서 무작위 차단을 8000회 돌렸다. (c) 복구 창의 2차 차단은 대표 모드(3~4개)만 돈다. (d) marginal 셀과 bit rot은 시험 안 함(Q1). 경로 커버리지는 `copying-discard / dead / garbage / dirty-tail > 0` CHECK로 보장했고, 변이 14개 중 13개가 잡혔다(M10은 전원 모델에서 동등 변이).
</details>

<details><summary>Q7. 실제 하드웨어에서는 이걸 어떻게 시험하나요?</summary>

전수 시험은 시뮬레이터에서 한다. 하드웨어에서는 (1) 칩의 Reset/Power 핀이나 전원 FET를 GPIO로 제어해 임의 지연 뒤 끊는 지그, (2) 측정 시작 시점을 플래시 쓰기 시작에 맞추는 동기화(GPIO 토글을 프로그램/소거 직전에), (3) 끊은 뒤 재마운트 + 불변식 검사 + 회귀 카운터 로깅(부트 카운터 사용). 드라이버 래퍼는 시뮬레이터와 같은 인터페이스(`nor_prog/nor_erase`)로 두어 시뮬레이터 로직이 실물에서도 그대로 돌게 한다. littlefs가 RAM 블록 디바이스로 전원 차단 시험을 돌리는 것과 같은 패턴이다(STOR-A03). 여기서 찾는 것은 "재현 가능한 일관성 위반"이고, 나온 것은 시뮬레이터에 모드로 추가해서 회귀 시험으로 고정한다.
</details>

<details><summary>Q8. LittleFS나 Bitcask를 쓰지 않고 이걸 직접 짤 이유가 있나요? 이 구조가 FTL과 뭐가 다른가요?</summary>

설정·캘리브레이션 몇십 개라면 raw KV가 코드와 RAM을 덜 쓴다(STOR-A05가 수치로 비교한다). 파일 형태의 큰 데이터, 많은 파일, 디렉터리가 필요하면 LittleFS 쪽이다. littlefs도 CRC 커밋과 로그 append + 이중 metadata block으로 같은 문제를 푼다(`DESIGN.md`: "An embedded filesystem must be designed to recover from a power loss during any write operation."). Bitcask(STOR-B05)는 keydir + append-only 파일이라 구조는 같지만 파일시스템이 아래에 있고 원자성은 다른 층에서 온다. FTL(DS-A09, MEM-M10~M14)은 **섹터 수가 수만~수십만**이라 greedy/cost-benefit GC, 블록 P2L, 웨어 레벨링이 별도 층이다. 이 레슨의 링은 모든 섹터를 순서대로 돌리는 극단의 단순화다. 그래서 공짜로 마모 평탄화가 되지만 cold 데이터를 계속 복사한다(WAF 표 참고).
</details>

<details><summary>Q9. seq가 오버플로하면요? 재부팅 후 seq 재사용은요?</summary>

32비트 seq는 섹터가 새로 열릴 때마다 1씩 늘어난다. 초당 한 번 섹터를 열어도 136년이다. 플래시 수명(섹터당 10만 회 정도가 대표 값; 파트별 확인)이 먼저 끝난다. 오버플로 처리는 쓰지 않았다. 대신 `next_seq`는 마운트 때 **유효 헤더들의 최댓값+1**로 계산한다. `DEAD`/`COPYING` 섹터의 seq도 포함해야 한다. 그러지 않으면 방금 버린 seq를 재사용해 다음 컴팩션의 `src_seq`와 충돌할 수 있다(변이 M14는 이것을 Part A에서 잡는다).
</details>

## 9. 추가 연습

### 연습 1 — 단항 부트 카운터 (비트 1→0)

32비트 워드 하나로 비트 하나씩 내려가며 카운트한다. `uc_read`(0 비트 수), `uc_inc`(다음 비트 0으로). 4 워드면 소거 전까지 128번 올릴 수 있다. ECC 플래시(STM32L4 double-word 재프로그램 금지)에서는 왜 못 쓰고, 쓸 수 있는 파트에서 얻는 이득이 뭔가?

<details><summary>답</summary>

`uc_read` = 앞쪽 워드부터 0 비트 수를 더하되 첫 불완전 워드에서 멈춘다. `uc_inc` = `w[c/32] &= ~(1 << c%32)`. 한 번에 비트 **하나**만 내리면 찢어져도 count는 그대로이거나 +1이다. 쓰기 예산이 32배다(레코드 하나당 8바이트 + 소거 사이 수십 번 → 비트 하나당 1카운트). ECC가 있으면 같은 워드를 두 번 프로그램할 수 없으므로 워드 하나당 count 하나다. `uc_words_needed()`가 차이를 계산한다. NOR/SPI라도 "같은 워드 여러 번 프로그램"이 허용되는지 데이터시트로 확인해야 한다(페이지당 program 횟수 제한 NOP가 있는 파트가 있다).
</details>

### 연습 2 — CRC가 찢어진 쓰기를 진짜로 잡는가

2000개의 무작위 레코드에 대해 **모든 prefix 찢어짐**을 만들어 CRC가 통과하는지 센다. 통과한 찢어짐은 전부 안전한가?

<details><summary>답</summary>

실제 결과: 2000개에서 2983개의 prefix tear가 CRC를 통과했고, **전부** 안 쓴 뒤쪽이 원래 0xFF(주로 패딩)인 경우였다. 즉 사실상 완성된 레코드다. CRC-32의 오탐 확률은 무작위 찢어짐 하나당 약 2⁻³²이며, 이 표본 크기로는 그걸 검증할 수 없다. 통계적 추정은 CRC의 표준 속성에 기대는 것이다. 고전적인 함정(전부 0xFF인 레코드가 유효)은 소거 상태 자체가 레코드 헤더로 인정되는지로 확인한다.
</details>

### 연습 3 — commit 디코더 두 가지

`commit_exact(w, v)` vs `commit_nonff(w)`. 찢어진 commit 값(일부 비트만 0) 10만 개와 pre-program 잔해(0x00000000)를 넣어 각각 오판 횟수를 센다.

<details><summary>답</summary>

`commit_exact`의 오판 0건, `commit_nonff`는 찢어진 값을 거의 전부 "커밋"으로 오판한다(`wrong_nonff > 90000`을 CHECK). 그러나 **찢어진 commit을 커밋으로 친 것 자체**는 데이터 손실이 없다(commit 쓰기가 시작됐다면 복사는 끝났다). 진짜 위험은 commit을 쓴 적 없는데 pre-program이 0을 만든 경우다. 그래서 두 디코더 차이의 본체는 `0x00000000` 케이스다(메인 레슨 변이 M1).
</details>

### 연습 4 — 재동기화 스캔: 깨진 레코드 하나가 뒤를 다 지우지 않게

메인 레슨은 첫 무효 레코드에서 멈춘다. 그 뒤의 멀쩡한 레코드는 사라진다(섹터 하나 수준에서는 받아들인 절충). 레코드 앞에 동기 워드(`SYNC`)를 넣고, 4바이트 단위로 훑어 다음 레코드를 찾게 만든다.

<details><summary>답</summary>

`scan_resync`: SYNC를 찾으면 그 뒤 CRC 검증을 시도하고, 실패하면 4바이트 전진한다. payload 안의 우연한 SYNC는 CRC로 거른다(`got == 1` 체크). 대가는 레코드당 4바이트 + 스캔 복잡도다. 비트 썩음(Q1)이 걱정되면 합리적이지만, **전원 차단에 대한 복구 시맨틱은 달라진다**: 찢어진 레코드 뒤의 오래된 잔해 레코드를 "유효"로 읽을 위험이 있다. 그러려면 "첫 무효 이후 레코드도 적용할지"를 섹터 상태와 seq로 판단해야 한다. 기본 스캐너가 첫 무효에서 멈추는 것은 이 위험을 없애는 단순한 선택이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice        # 15/15 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o p_asan && ./p_asan
```

## 10. 핵심 정리

- NOR 제약: 소거는 섹터 전체(`0xFF`), 프로그램은 1→0만. 로그 + 컴팩션이 자연스러운 구조다. 레코드 커밋 = CRC 일치.
- **쓰기 원자성을 가정하지 않는다.** 찢어진 프로그램과 부분 소거(pre-program의 0x00 포함)를 전부 모드로 만들어 **모든 op에 주입**한다. 복구 중 2차 차단까지.
- 컴팩션 순서: D 헤더(COPYING) → live 복사 → **`commit` 정확한 값** → V 소거. 커밋 근거는 **지워지지 않는 D**에 둔다.
- 마운트 분류: DEAD(seq ≤ 커밋된 src 최댓값)는 내용을 보지 않고 재소거한다(재생하면 부분 소거로 tombstone만 지워진 키가 되살아난다). BLANK는 섹터 **전체** 0xFF. GARBAGE도 재소거.
- commit 디코딩은 정확한 값 비교다. "0xFF만 아니면"은 pre-program 잔해에 속는다(변이 M1, 2차 차단에서 27건).
- 첫 무효 레코드 뒤가 0xFF가 아니면 dirty tail이다. 그 섹터에 append 금지, 다음 쓰기는 새 섹터로.
- tombstone은 가장 오래된 섹터에서만 버릴 수 있다. 재생 순서가 근거다.
- 링 회전은 공짜 마모 평탄화(소거 차이 1)를 주지만 cold 데이터 복사 비용(WAF)이 든다. 8×512에서 채움 27%→98%면 WAF 1.26→3.14.

## 참고자료

- littlefs `DESIGN.md` — https://github.com/littlefs-project/littlefs (전원 차단 복구 요구사항 원문, 메타데이터 페어, CRC 커밋; STOR-A03)
- Zephyr NVS `subsys/fs/nvs/nvs.c` (v3.7.0 태그) — `nvs_add_gc_done_ate`, `nvs_gc`, `nvs_startup`의 "restarting gc" 경로를 직접 확인; 이 레슨의 컴팩션 커밋과 같은 원리
- linux-mtd 메일링 리스트, "UBIFS Corrupt during power failure" (2009-07) — https://lists.infradead.org/pipermail/linux-mtd/2009-July/026376.html — NOR 소거가 끊긴 블록의 덤프와 Spansion 답변 인용(일화적 근거)
- ST STM32L4x6 reference manual의 Flash programming 절 (manualslib 발췌로 확인, 절 번호는 원문 확인 필요) — double-word 프로그램, ECC, PROGERR
- ST AN4894 "EEPROM emulation techniques" — 페이지 상태(header 4라인, ERASING 등) 기반 설계; 세부 값은 원문 확인 필요
- ST AN2594 (STM32F1 EEPROM 에뮬레이션) — 2페이지 스왑의 고전 설계. 이 레슨의 2섹터 구성과 비교(상태값 세부는 AN 원문 확인 필요)
- Winbond W25Q 계열 데이터시트 — Page Program 256B, 4 KB sector erase, tSE 등 파트별 값은 데이터시트에서 확인(이 레슨의 숫자는 모델 값이며 실측이 아니다)
- 관련 레슨: BM-A02 (플래시 드라이버 시뮬레이터), BM-A06 (업데이트 전원 차단), STOR-B05 (Bitcask), STOR-A03 (LittleFS 내부), STOR-A05 (저장 계층 선택), PLAT-M08 (스키마 마이그레이션), DS-A09 (FTL GC), DS-M09 (고정 블록 풀)
