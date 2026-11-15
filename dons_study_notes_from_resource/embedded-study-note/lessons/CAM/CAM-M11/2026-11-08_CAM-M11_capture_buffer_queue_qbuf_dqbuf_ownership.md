---
id: CAM-M11
time: 90분
updated: 2026-11-08
---
# CAM-M11 캡처 버퍼 큐와 DMA: qbuf/dqbuf 소유권 모델

## 학습 목표

- 캡처 버퍼의 네 상태(DEQUEUED / QUEUED / ACTIVE / DONE)와 **각 상태의 주인 한 명**을 표로 말하고, 그 전이를 `cap_qbuf` · `cap_dqbuf` · SOF/EOF 인터럽트로 구현한다.
- **락은 상태와 리스트만 보호하고, 프레임 내용은 "지금 주인이 누구인가" 로 보호한다** 는 원칙을 코드로 쓰고, 그래서 TSan 이 깨끗하다는 것을 실제로 확인한다.
- 소비자가 느려서 버퍼가 바닥났을 때의 세 정책 — DROP_NEW(새 프레임 버림), REUSE_LAST(마지막 버퍼 재사용), OVERWRITE_OLDEST(가장 오래된 DONE 덮어쓰기) — 을 같은 시나리오로 돌려 **잃는 프레임과 나이(age)** 가 어떻게 다른지 숫자로 비교한다.
- sequence 번호로 공백을 찾고(32비트 wrap 포함), 소유권 위반을 poison 패턴 · 불변식 검사 · TSan 으로 잡는다. 시험은 강제 스케줄로 결정적으로 만든다.
- 실제 Linux videobuf2 코드(6.18.55)에서 같은 상태와 캐시 유지보수 위치를 찾아 읽고 설명할 수 있다.

## 1. 문제

카메라 드라이버 · ISP 펌웨어 · SoC 비디오 파이프라인 검증에서 가장 자주 만나는 버그는 "프레임 하나가 가끔 찢어져요" 와 "드롭이 있는데 앱이 모르고 있었어요" 다. 둘 다 **버퍼 하나를 두 주체가 동시에 자기 것이라고 믿어서** 생긴다. 면접에서는 "v4l2 스트리밍에서 버퍼 소유권이 어떻게 움직이나요? 소비자가 느리면 어떻게 합니까?" 로 시작해서 캐시 유지보수 위치, IRQ 컨텍스트의 락, 드롭 검출까지 꼬리질문이 이어진다.

동시성 가이드 8층 레슨이다. 앞 층에서 배운 것 — 조건 변수(OS-B06), IRQ 와 공유하는 자료의 락(ELX-M04), DMA 캐시 유지보수(ARCH-M12), 생산자/소비자 링(DS-A01) — 이 카메라 드라이버 한 곳에 모두 나온다. 핵심 개념은 **소유권 이전(ownership transfer)** 이다: 프레임 메모리는 커서(1080p 면 수 MB) 락 안에서 복사할 수 없으니, 메모리 자체는 안 건드리고 "누가 이 메모리를 만져도 되는가" 라는 권리만 넘긴다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">버퍼 하나의 생애: 언제나 주인은 정확히 한 명</text>
  <rect x="20" y="50" width="150" height="70" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="45" y="75" fill="currentColor" font-weight="bold">DEQUEUED</text>
  <text x="45" y="95" fill="currentColor" font-size="12">주인: 앱</text>
  <text x="45" y="112" fill="currentColor" font-size="12">앱만 읽기/쓰기</text>
  <rect x="295" y="50" width="150" height="70" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="325" y="75" fill="currentColor" font-weight="bold">QUEUED</text>
  <text x="325" y="95" fill="currentColor" font-size="12">주인: 드라이버</text>
  <text x="325" y="112" fill="currentColor" font-size="12">아무도 안 만짐</text>
  <rect x="570" y="50" width="150" height="70" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="600" y="75" fill="currentColor" font-weight="bold">ACTIVE</text>
  <text x="600" y="95" fill="currentColor" font-size="12">주인: HW (DMA)</text>
  <text x="600" y="112" fill="currentColor" font-size="12">DMA 만 쓰기</text>
  <rect x="295" y="210" width="150" height="70" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="335" y="235" fill="currentColor" font-weight="bold">DONE</text>
  <text x="325" y="255" fill="currentColor" font-size="12">주인: 드라이버</text>
  <text x="325" y="272" fill="currentColor" font-size="12">앱을 기다리는 중</text>
  <path d="M170 85 L293 85" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="185" y="72" fill="currentColor" font-size="12">QBUF (앱)</text>
  <text x="176" y="104" fill="currentColor" font-size="12">poison · 캐시 준비</text>
  <path d="M445 85 L568 85" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="462" y="72" fill="currentColor" font-size="12">EOF/arm</text>
  <text x="452" y="104" fill="currentColor" font-size="12">poison 검사</text>
  <path d="M640 120 L640 245 L447 245" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="510" y="235" fill="currentColor" font-size="12">EOF (완료 IRQ)</text>
  <text x="510" y="262" fill="currentColor" font-size="12">캐시: CPU 쪽으로</text>
  <path d="M295 245 L95 245 L95 122" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="130" y="235" fill="currentColor" font-size="12">DQBUF (앱)</text>
  <path d="M370 210 C 470 170 560 170 610 122" stroke="currentColor" stroke-width="2" fill="none" stroke-dasharray="5 4"/>
  <text x="440" y="165" fill="currentColor" font-size="12">OVERWRITE_OLDEST: DONE 을 빼앗아 ACTIVE 로</text>
  <text x="20" y="310" fill="currentColor" font-size="12">전이는 짧은 락 안에서, 프레임 메모리 쓰기/읽기는 락 밖에서. 락을 쥐고 수 MB 를 만지지 않는다.</text>
  <text x="20" y="328" fill="currentColor" font-size="12">HW 는 쓰기 대상이 없으면 안 된다 → streaming 중 ACTIVE 는 항상 정확히 하나 (불변식)</text>
</svg>
```

| 이 레슨의 상태 | 주인 | `mem[]` 을 만져도 되는 쪽 | Linux vb2 에서 가까운 것 (6.18.55 `videobuf2-core.h` 에서 확인) |
|---|---|---|---|
| DEQUEUED | 앱 | 앱 | `VB2_BUF_STATE_DEQUEUED` "buffer under userspace control" |
| QUEUED | 드라이버 | 아무도 | `VB2_BUF_STATE_QUEUED` "queued in videobuf2, but not in driver" |
| ACTIVE | HW(DMA) | DMA 만 | `VB2_BUF_STATE_ACTIVE` "queued in driver and possibly used in a hardware operation" |
| DONE | 드라이버 | 아무도 (앱이 dqbuf 하기 전) | `VB2_BUF_STATE_DONE` "returned from driver to videobuf2, but not yet dequeued" |

주의: 실제 vb2 에는 `PREPARING` · `IN_REQUEST` · `ERROR` 가 더 있고, **이미 스트리밍 중이면 qbuf 한 버퍼가 바로 ACTIVE 로 간다**(`__enqueue_in_driver`, `vb2_core_qbuf` 1949 행) — vb2 의 ACTIVE 는 "드라이버 리스트에 있음" 까지 포함한다. 이 레슨의 QUEUED/ACTIVE 구분은 "큐 대기 vs DMA 가 지금 쓰는 중" 이다. 모델의 이름이 vb2 의 이름과 정확히 같은 뜻은 아니라는 것을 기억해 두자.

### Part A — 상태 머신과 API

- `cap_qbuf(c, idx)`: DEQUEUED 인 버퍼만 받는다. 아니면 `-EINVAL`, 상태 변화 없음. 통과하면 `POISON_WORD` 로 채우고 QUEUED.
- `cap_dqbuf(c, &info, timeout_ms)`: DONE FIFO 맨 앞을 돌려준다. 비었으면 스트리밍 중 `-EAGAIN`(시간 제한까지 대기), 멈춘 큐는 `-EINVAL`.
- `check_ownership(c)`: 각 버퍼가 inq / doneq / active 중 **정확히 한 곳**에 있고 상태와 맞는지, 캐시 시점(cpu_view)이 상태와 맞는지, 스트리밍 중 active 가 있는지 확인. 모든 전이 뒤에 `audit()` 가 호출해 위반을 누적한다.

### Part B — 버퍼가 없을 때: 정책, 강제 스케줄, 공백 검출

- SOF(`hw_start_of_frame`)와 EOF(`hw_end_of_frame`)를 쓴다. EOF 에서 다음 버퍼가 없을 때 세 정책의 동작:
  - **DROP_NEW**: ACTIVE 를 유지하고 끝난 프레임을 그 안에 보관(parked). 이후 오는 프레임은 쓰지 않고 `dropped++`. 다음 버퍼가 생기면 SOF 에서 보관 프레임을 DONE 으로.
  - **REUSE_LAST**: ACTIVE 를 유지, 방금 프레임은 다음 프레임에 덮이므로 `dropped++`.
  - **OVERWRITE_OLDEST**: 끝난 프레임을 DONE 으로, 대신 DONE FIFO 맨 앞(제일 오래된)을 빼앗아 ACTIVE 로. `dropped++`.
- 강제 스케줄: DMA 도중 훅에서 qbuf/dqbuf/streamoff 를 끼워 넣어 타이밍 버그를 결정적으로 재현한다. 앱이 넘긴 뒤에 버퍼에 쓰면 poison 이 그 다음 arm 에서 잡는다.
- `gap_update(g, seq)`: 공백 수, 역행, 32비트 wrap.

### Part C — 진짜 스레드 + 정책 비교

- `app_consume_one`: dqbuf → 내용 검증 → 공백 → (느린 처리) → qbuf.
- `cap_streamoff(c, timeout_ms)`: DMA 가 끝날 때까지 기다린 뒤 버퍼를 회수한다.
- 시험: 진짜 IRQ 스레드를 rendezvous 로 강제해서 Part B 와 똑같은 결과가 나오는지, 자유 실행에서 **보존 법칙**(`frames == dequeued + dropped + flushed`)과 불변식이 어떤 스케줄에서도 지켜지는지, 가상 시간으로 세 정책의 프레임 나이를 비교한다.

## 2. Problem (English)

> Implement a V4L2-like capture queue with `n` buffers. Each buffer is in exactly one of four states: DEQUEUED (owned by the application), QUEUED (owned by the driver, waiting), ACTIVE (owned by the DMA engine), DONE (filled, waiting for the application).
>
> ```c
> int  cap_qbuf(cap_t *c, int idx);                       // 0, or -EINVAL if not DEQUEUED
> int  cap_dqbuf(cap_t *c, cap_info_t *info, int timeout_ms); // index, -EAGAIN, -EINVAL
> void cap_hw_frame(cap_t *c);                            // DMA engine: SOF, write frame, EOF IRQ
> int  cap_streamoff(cap_t *c, int timeout_ms);           // 0 or -EBUSY while DMA runs
> ```
>
> Frame contents are protected by ownership, not by the lock. When a frame ends and no buffer is available, apply one of: (a) keep the ACTIVE buffer and drop the new frame, (b) reuse the last buffer, overwriting the frame just captured, (c) overwrite the oldest DONE buffer. The application detects missing frames from gaps in the sequence number (`uint32_t`, wraps).
>
> Follow-ups: where does cache maintenance go in this state machine? Which lock type does the IRQ path need? How would you test a race that only happens when `qbuf` lands inside one specific frame?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

TODO 8 개를 채운다. 그대로 돌리면 멈추지 않고 실패한다 (실제 출력):

```text
FAIL starter.c:312  cap_qbuf(&c, 0) == 0
FAIL starter.c:313  c.buf[0].state == B_QUEUED
FAIL starter.c:314  c.buf[0].mem[0] == POISON_WORD && c.buf[0].mem[FRAME_WORDS - 1] == POISON_WORD
…
65/143 passed
```

## 4. 힌트

<details><summary>힌트 1 — 주인은 어디에 적혀 있나</summary>

`state` 필드는 한 군데이고, 버퍼가 들어 있는 곳도 한 군데다. `inq` 에 있으면 QUEUED, `doneq` 에 있으면 DONE, `active` 면 ACTIVE, 앱이 가졌으면 어디에도 없다. `check_ownership` 은 이 대응이 맞는지 세면 된다. "앱이 가졌다" 는 사실을 따로 저장하지 않아도 "어디에도 없고 DEQUEUED" 로 표현된다.
</details>

<details><summary>힌트 2 — poison 을 언제 칠하나</summary>

앱이 DEQUEUED 버퍼를 넘길 때 칠하되, 검증을 **통과한 다음에** 칠한다. 순서를 뒤집으면 double qbuf 때 DMA 가 쓰는 버퍼를 덮어쓴다. 검사는 QUEUED → ACTIVE 로 가는 순간(`arm_next`)에 한다. 그 사이 앱이 쓰면 패턴이 깨져 있다.
</details>

<details><summary>힌트 3 — SOF 와 EOF 를 왜 나누나</summary>

HW 는 프레임이 끝날 때 다음 주소를 정한다. 그래서 EOF 에서 "다음 버퍼가 없으면 어쩌지?" 가 결정된다. 프레임 번호(`hw_seq`)는 SOF 에서 소비한다 — **버린 프레임도 번호를 하나 먹어야** 앱이 공백을 볼 수 있다. DROP_NEW 는 SOF 에서도 할 일이 있다: 보관 중인 프레임이 있고 새 버퍼가 생겼으면 그걸 먼저 내보낸다.
</details>

<details><summary>힌트 4 — OVERWRITE_OLDEST 에서 빼앗은 버퍼</summary>

DONE 은 드라이버 소유니까 빼앗아도 된다(앱이 아직 받지 않았다). 다만 EOF 에서 끝난 프레임을 DONE 으로 보낼 때 캐시를 CPU 쪽으로 돌렸으니, DONE → ACTIVE 로 돌아갈 때 장치 쪽으로 되돌려야 한다 (`cache_for_device`). 앱이 n-1 개를 다 들고 있으면 빼앗을 DONE 이 방금 넣은 자기 자신뿐이다 — REUSE_LAST 와 같은 동작이 된다.
</details>

<details><summary>힌트 5 — wrap 과 역행</summary>

`uint32_t diff = seq - expected;` 는 wrap 에서도 정상이다. 차이가 2^31 이상이면 "뒤로 갔다" 로 본다. 입력이 뒤섞이면 공백이 아니라 역행이므로 공백 합계를 늘리지 않는다.
</details>

## 5. 풀이

### Part A — 전이 하나마다 짧은 락, 내용은 락 밖에서

핵심 구조는 이렇다. 락이 지키는 것은 `state` · 리스트 · 카운터뿐이다.

```text
앱:   cap_qbuf()  ──lock──▶ 검증 → poison → 캐시 준비 → QUEUED ──unlock
HW:   cap_hw_frame() ──lock──▶ SOF: 대상 결정, seq 기록 ──unlock
      (락 없이) mem[] 쓰기                                     ← ACTIVE 의 주인은 HW 뿐
      ──lock──▶ EOF: DONE 또는 정책 ──unlock
앱:   cap_dqbuf() ──lock──▶ DONE 꺼내 DEQUEUED ──unlock
      (락 없이) mem[] 읽기                                      ← DEQUEUED 의 주인은 앱뿐
```

이것이 맞는 이유는 happens-before 로 설명된다. HW 가 `mem[]` 을 다 쓰고 EOF 에서 뮤텍스를 해제하면, 앱은 dqbuf 에서 같은 뮤텍스를 잡고 들어오므로 쓰기 전체가 앱의 읽기보다 먼저다. 뮤텍스의 해제→획득이 소유권 이전의 경계다. TSan 도 이 경계를 그대로 본다: solution.c 는 락 없이 `mem[]` 을 쓰는데도 TSan 이 깨끗하다 (아래 실측).

`check_ownership` 은 이 불변식을 숫자로 센다. 시험은 먼저 **검사기 자체를 시험한다** — 일부러 망가뜨린 상태 다섯 종류(앱 소유인데 리스트에 있음, DONE 과 ACTIVE 에 동시에 있음, 어디에도 없는 분실 버퍼, 캐시 시점 불일치, 스트리밍 중 ACTIVE 없음)를 `check_ownership` 이 찾아내는지 CHECK 한다. 검사기가 언제나 0 을 돌려줘도 "위반 없음" 으로 통과하는 것이 불변식 시험의 가장 흔한 함정이다.

허용되는 전이는 표로 쓰면 이것뿐이다. 표에 없는 전이는 전부 버그이고, `cap_qbuf` 의 `-EINVAL` 이나 불변식 위반으로 드러난다.

| 현재 → 다음 | 누가 / 언제 | 코드 |
|---|---|---|
| DEQUEUED → QUEUED | 앱의 qbuf (검증 + poison + 캐시 준비) | `cap_qbuf` |
| QUEUED → ACTIVE | EOF 또는 STREAMON 때 HW 에 건다 (poison 검사) | `arm_next` |
| ACTIVE → DONE | EOF, 다음 버퍼가 있을 때 (또는 parked 프레임을 SOF 에서 내보낼 때) | `complete_active` |
| ACTIVE → ACTIVE | EOF, 다음 버퍼 없음 + REUSE_LAST / DROP_NEW(parked) | 전이 없음 |
| DONE → ACTIVE | EOF, 다음 버퍼 없음 + OVERWRITE_OLDEST (빼앗기) | `hw_end_of_frame` |
| DONE → DEQUEUED | 앱의 dqbuf | `cap_dqbuf` |
| QUEUED · ACTIVE · DONE → DEQUEUED | STREAMOFF (DMA 가 끝난 뒤) | `cap_streamoff` |

앱이 DONE 버퍼를 곧바로 qbuf 하거나(`-EINVAL`, 아직 dqbuf 안 함), 같은 버퍼를 두 번 qbuf 하는 것이 시험이 가장 먼저 잡는 경우다. 드라이버 쪽 전이는 `arm_next` / `complete_active` 두 함수로만 일어나게 묶어서, 리스트와 state 를 같이 바꾸는 코드가 흩어지지 않게 한다.

### Part B — 스톨 시나리오로 정책 비교

`nbuf=4`, 앱이 버퍼 네 개를 모두 큐에 넣고 시작한다. 시나리오: 프레임 8개 동안 앱이 멈춤 → 앱이 3개 꺼내 돌려줌 → 프레임 2개 → 앱이 3개 시도. 스크립트는 `"FFFFFFFFDDDFFDDD"` 다. 프로그램 실제 출력:

```text
--- Part B: 스톨 시나리오 (F x8, D x3, F x2, D x3)
[DROP_NEW] delivered: 0 1 2 3 8 9  dropped=4 gaps=4
[REUSE_LAST] delivered: 0 1 2 8 9  dropped=5 gaps=5
[OVERWRITE_OLDEST] delivered: 5 6 7 8 9  dropped=5 gaps=5
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">프레임 0~9 중 앱이 받은 것 (진하게) vs 잃은 것 (경고색) — 버퍼 4개, 프레임 8개 동안 앱 멈춤</text>
  <text x="150" y="44" fill="currentColor" font-size="12">0</text><text x="202" y="44" fill="currentColor" font-size="12">1</text><text x="254" y="44" fill="currentColor" font-size="12">2</text><text x="306" y="44" fill="currentColor" font-size="12">3</text><text x="358" y="44" fill="currentColor" font-size="12">4</text><text x="410" y="44" fill="currentColor" font-size="12">5</text><text x="462" y="44" fill="currentColor" font-size="12">6</text><text x="514" y="44" fill="currentColor" font-size="12">7</text><text x="566" y="44" fill="currentColor" font-size="12">8</text><text x="618" y="44" fill="currentColor" font-size="12">9</text>
  <text x="10" y="72" fill="currentColor" font-size="13">DROP_NEW</text>
  <rect x="135" y="55" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="187" y="55" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="239" y="55" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="291" y="55" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="343" y="55" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="395" y="55" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="447" y="55" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="499" y="55" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="551" y="55" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="603" y="55" width="46" height="26" class="f-ok" stroke="currentColor"/>
  <text x="10" y="122" fill="currentColor" font-size="13">REUSE_LAST</text>
  <rect x="135" y="105" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="187" y="105" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="239" y="105" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="291" y="105" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="343" y="105" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="395" y="105" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="447" y="105" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="499" y="105" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="551" y="105" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="603" y="105" width="46" height="26" class="f-ok" stroke="currentColor"/>
  <text x="10" y="172" fill="currentColor" font-size="13">OVERWRITE_OLDEST</text>
  <rect x="135" y="155" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="187" y="155" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="239" y="155" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="291" y="155" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="343" y="155" width="46" height="26" class="f-warn" stroke="currentColor"/><rect x="395" y="155" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="447" y="155" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="499" y="155" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="551" y="155" width="46" height="26" class="f-ok" stroke="currentColor"/><rect x="603" y="155" width="46" height="26" class="f-ok" stroke="currentColor"/>
  <text x="10" y="215" fill="currentColor" font-size="12">DROP_NEW 는 스톨 시작 직전 프레임을 지키고, OVERWRITE_OLDEST 는 스톨 끝의 가장 새 프레임들을 지킨다.</text>
  <text x="10" y="235" fill="currentColor" font-size="12">REUSE_LAST 는 재개 시점의 최신 프레임 하나를 건지되, 그 전 프레임은 ACTIVE 안에서 덮인다.</text>
</svg>
```

한 칸씩 따라가 보면 이렇다 (DROP_NEW, 버퍼 b0~b3, 프레임 0 = b0 부터):

| 프레임 | 일어난 일 | ACTIVE | DONE FIFO | dropped |
|---|---|---|---|---|
| 0 | b0 에 씀, EOF: 다음(b1) 있음 → b0 DONE | b1 | [0] | 0 |
| 1 | b1 에 씀 → DONE, 다음(b2) | b2 | [0,1] | 0 |
| 2 | b2 에 씀 → DONE, 다음(b3) | b3 | [0,1,2] | 0 |
| 3 | b3 에 씀, EOF: inq 빔 → **parked** (b3 에 프레임 3 보관) | b3 (보관) | [0,1,2] | 0 |
| 4 ~ 7 | SOF: 보관 중 + inq 빔 → 안 씀, `dropped++` | b3 | [0,1,2] | 4 |
| 앱 | D D D: 0,1,2 받고 b0,b1,b2 qbuf | b3 | [] | 4 |
| 8 | SOF: 보관 중 + inq 있음 → b3(프레임3) DONE, b0 ACTIVE, 8 을 b0 에 | b1 | [3,8] | 4 |
| 9 | b1 에 씀 → DONE | b2 | [3,8,9] | 4 |

각 프레임의 운명 = (전달) 또는 (버림) 또는 (STREAMOFF 때 못 받아 감). 그래서 **어떤 정책이든 `frames == dequeued + dropped + flushed`** 가 성립한다 — Part C 가 CHECK 하는 보존 법칙이다.

| 정책 | 잃는 프레임 | 전달 프레임의 나이 | HW 쪽 동작 | 언제 쓰나 |
|---|---|---|---|---|
| DROP_NEW | 스톨 동안 새로 온 것 | 가장 늙음 | ACTIVE 유지, 쓰지 않음 | 프레임 기록기처럼 끊김 없는 "앞부분" 이 중요할 때 |
| REUSE_LAST | 스톨 동안 ACTIVE 에 덮인 것 | 중간 | ACTIVE 유지, 같은 버퍼에 계속 씀 | 드라이버에서 가장 단순한 방식 |
| OVERWRITE_OLDEST | DONE 에서 가장 오래된 것 | 가장 젊음 | DONE 을 빼앗아 ACTIVE | 자율주행 인지, 제어처럼 **최신 프레임** 이 가치인 경우 |

어떤 정책이든 **`sequence` 는 버린 프레임에도 올라가야** 공백이 보인다. V4L2 스펙(`Documentation/userspace-api/media/v4l/buffer.rst`, 6.18.55)은 이렇게 말한다: "The count starts at zero and includes dropped or repeated frames. A dropped frame was received by an input device but could not be stored due to lack of free buffer space."

#### 진짜 드라이버는 어떻게 하나 (6.18.55 소스에서 읽음)

- **rcar-vin**(`rcar-dma.c:981`): 버퍼가 없으면 HW 주소를 **스크래치 버퍼**로 돌린다. 주석: "The scratch buffer is never returned to userspace, its only function is to enable the capture loop to keep running." 완료 시 `/* Scratch buffer was used, dropping frame. */` 로 버리고 `vin->sequence++` 는 그대로 한다 → 앱에 공백이 보인다. DROP_NEW 와 닮았는데 보관하는 버퍼 대신 쓰기 전용 스크래치를 쓴다. 
- **stm32-dcmi**(`stm32-dcmi.c`): 버퍼가 없으면 `"Capture restart is deferred to next buffer queueing"` 로 캡처를 멈추고 `WAIT_FOR_BUFFER` 상태가 된다. `dcmi->sequence++` 는 소스에서 `dcmi_buffer_done` 한 곳뿐이다(`stm32-dcmi.c:215`) — 멈춘 동안의 프레임은 번호를 받지 않아 **앱은 공백을 볼 수 없다**. 공백 검출이 드라이버의 협조에 달려 있다는 좋은 예다.

이 차이를 면접에서 말할 수 있으면 좋다: "sequence 공백은 드라이버가 드롭을 세어 줄 때만 믿을 수 있다."

참고로 OVERWRITE_OLDEST 는 **표준 vb2 드라이버 안에서는 만들 수 없다.** 6.18.55 `videobuf2-core.c` 의 `vb->state =` 대입을 전부 확인하면 ACTIVE 로 가는 곳은 `__enqueue_in_driver`(1541 행) 하나고, 호출자(1785, 1949 행)는 QUEUED 버퍼만 넘긴다 — DONE 을 되돌리는 경로가 없다. 그래서 V4L2 앱에서 최신 프레임을 원하면 **앱이** dqbuf 를 빨리 돌면서 오래된 것을 바로 돌려준다. 드라이버가 아닌 RTOS 펌웨어나 사용자 공간 파이프라인에서는 이 레슨의 방식이 그대로 가능하다.

#### EOF 한 곳에서 정책을 고른다

세 정책의 차이는 코드로 보면 이 `switch` 하나다. 위의 "다음 버퍼가 있으면 그냥 완료" 경로는 공통이고, 다음 버퍼가 없을 때만 갈라진다.

```c
switch (c->policy) {
case POLICY_DROP_NEW:                   /* X 는 ACTIVE 그대로, 프레임 보관. 다음 프레임부터 SOF 에서 버린다 */
    c->parked = true;
    break;
case POLICY_REUSE_LAST:                 /* X 는 ACTIVE 그대로, 다음 프레임이 방금 프레임을 덮는다 */
    c->dropped++;
    break;
case POLICY_OVERWRITE_OLDEST: {         /* X 를 내보내고 가장 오래된 DONE 을 ACTIVE 로 빼앗는다 */
    complete_active(c);
    int z = fifo_pop(&c->doneq);
    cache_for_device(c, z);             /* 빼앗기는 곧 "장치에게 다시 준다" 는 뜻 */
    c->buf[z].state = B_ACTIVE;
    c->active = z;
    c->dropped++;
    break;
}
```

세 줄 모두 불변식 "streaming 중 active 는 항상 하나" 를 지킨다는 점을 눈여겨보자. HW 가 쓸 곳을 잃는 경로가 없다. DROP_NEW 만 EOF 에서 드롭을 세지 않는 이유: 그 프레임은 아직 ACTIVE 안에 멀쩡히 있고 나중에 내보내진다. 드롭은 그다음 SOF 에서 세어진다.

#### 강제 스케줄로 "그 프레임 안에서만" 나는 경쟁을 재현

경쟁은 스레드 타이밍으로 만들지 않는다. `mid_dma_hook` 이 DMA 절반 뒤에 호출되므로, qbuf 를 "이 프레임 도중" 에 정확히 넣을 수 있다.

| 강제 스케줄 | 결과 (실제 CHECK) | 뜻 |
|---|---|---|
| 프레임 도중 hook 이 qbuf | `dropped == 0`, 다음 EOF 가 그 버퍼로 넘어감 | DMA 중이라도 EOF 전이면 제 시간 |
| 같은 qbuf 가 EOF 직후 | `dropped == 1`, 다음 프레임의 seq 는 2 로 건너뜀 | 윈도는 "EOF 까지" 다 |
| 프레임 도중 hook 이 dqbuf | `-EAGAIN`, `cap_ptr(active) == NULL` | ACTIVE 는 앱이 못 받는다 |
| 프레임 도중 hook 이 streamoff(0) | `-EBUSY`, 프레임 완료 후 streamoff 성공 | DMA 중에 회수 금지 |
| qbuf 후 `stale[7]=...` (버그 앱) | 다음 arm 에서 `poison_hits==1`, `idx==0` | poison 이 문제 버퍼를 지목 |

타이밍을 벽시계로 흔들어서 만든 테스트는 "가끔" 터지지만, 훅으로 강제하면 매번 똑같이 터진다. 8층 면접 체크의 "타이밍 의존 버그를 재현 가능하게" 가 바로 이 방식이다 (seed 로 일반화한 것은 TEST-A05).

#### 공백 검출

`gap_update` 는 `uint32_t` 산술 하나로 wrap 을 처리한다. 예: `expected=0xFFFFFFFE` 에서 seq `0xFFFFFFFE` 를 받고, 다음에 seq `1` 이 오면 `1 - 0xFFFFFFFF == 2` — `0xFFFFFFFF` 와 `0` 두 프레임이 빠졌다는 뜻이다(시험 표). 차이가 `0x7FFFFFFF` 를 넘으면 역행으로 본다(재정렬된 입력을 공백으로 세지 않기 위해). 시작 시 `expected=0` 이므로 첫 프레임이 seq 5 면 공백 5 가 바로 잡힌다 — OVERWRITE_OLDEST 의 첫 전달이 그 경우다.

### Part C — 진짜 스레드에서도 같은가

두 단계로 시험한다. (1) IRQ 스레드를 게이트로 강제(`gate_fire`): 진짜 `pthread` 지만 프레임 하나씩만 돌려 Part B 와 **완전히 같은 결과**가 나오는지 본다 (TSan 대상). (2) 자유 실행: 생산자 1500 프레임, 소비자는 프레임당 80 µs 느리게. 스케줄이 매번 다르니 **어떤 스케줄에서도 성립하는 것만** CHECK 한다.

```text
--- Part C: 자유 실행 스트레스 (보존 법칙 + 불변식)
[DROP_NEW] frames=1500 delivered=513 dropped=986 flushed=1 gaps=982 (자유 실행: 수치는 매번 다름)
[REUSE_LAST] frames=1500 delivered=504 dropped=996 flushed=0 gaps=995 (자유 실행: 수치는 매번 다름)
[OVERWRITE_OLDEST] frames=1500 delivered=464 dropped=1036 flushed=0 gaps=1036 (자유 실행: 수치는 매번 다름)
```

CHECK 하는 것: `frames == dequeued + dropped + flushed`, 내용 깨짐 0, seq 단조 증가, 불변식 위반 0, poison 0, 캐시 순서 오류 0, `gaps <= dropped + flushed`(앱이 마지막으로 받은 프레임 뒤에서 잃은 것은 공백으로 나타나지 않으므로 등호가 아니다 — 위 DROP_NEW · REUSE_LAST 줄이 그 경우).

#### 프레임 나이: 가상 시간으로 비교

벽시계를 빼고 프레임 번호 차이로 "나이(age, 방금 끝난 프레임 = 0)" 를 잰다. 숫자가 매번 같으니 CHECK 할 수 있다.

```text
--- Part C: 정책 비교 (nbuf=4, 소비자 3 프레임에 1개, 60 프레임, 가상 시간)
policy               deliv    drop  age_avg age_max
DROP_NEW                20      37     9.75      11
REUSE_LAST              20      38     7.40       8
OVERWRITE_OLDEST        20      38     2.00       2
policy               deliv    drop  age_avg age_max  (nbuf=3, 소비자 4 프레임에 1개, 80 프레임)
DROP_NEW                20      58    10.25      11
REUSE_LAST              20      59     6.75       7
OVERWRITE_OLDEST        20      59     1.00       1
```

정상 상태의 최대 나이는 공식으로 나온다 (n = 버퍼 수, P = 소비 주기 프레임). 시험은 위 두 조합 모두를 CHECK 한다.

| 정책 | age_max | 왜 |
|---|---|---|
| DROP_NEW | n·P − 1 | FIFO 에 쌓인 것 + parked 까지 n 개가 차례로 소비되므로, 가장 오래 기다린 프레임은 n·P 프레임 전에 끝난 것 |
| REUSE_LAST | (n−1)·P − 1 | parked 가 없고 ACTIVE 는 계속 새 것 |
| OVERWRITE_OLDEST | n − 2 | DONE 은 항상 최신 n−1 프레임, 가장 오래된 것의 나이는 n−2. **소비 주기와 무관** |

전달 개수와 드롭 수는 세 정책이 거의 같다 — 소비자가 느린 이상 프레임은 어차피 버려진다. 정책이 고르는 것은 **어느 프레임을 버리느냐**, 즉 앱이 받는 프레임의 나이다.

벽시계로 재면 (데모 `demo_policy_bench.c`, 생산자 약 1 ms 주기, 소비자 프레임당 3 ms + 50 프레임마다 30 ms 멈춤, nbuf=4):

<!-- code: demo_policy_bench.c -->

```text
Apple M6 (Super 2 + Performance 4 + Efficiency 6), Apple clang version 21.0.0 — load average 25~45 (다른 작업이 동시 실행 중: 비율만 보자)
policy             frames delivered  dropped flushed   gaps   p50_ms   p99_ms   max_ms
DROP_NEW             1800      552     1247       1   1221     10.7     44.8     48.8
REUSE_LAST           1800      551     1249       0   1249      6.8     41.4     42.6
OVERWRITE_OLDEST     1800      541     1259       0   1259      2.0      3.9      6.1
```

(같은 조건 두 번째 실행: p50 10.9 / 6.7 / 2.1 ms, p99 48.4 / 41.3 / 6.9 ms, max 51.1 / 42.6 / 28.7 ms — 두 번째의 28.7 ms 는 부하로 소비자가 밀린 것으로 추정.) 멈춤(30 ms) 이후 p99 에서 차이가 크게 벌어진다 — 드롭 수는 비슷한데 OVERWRITE_OLDEST 만 낡은 프레임을 내보내지 않는다.

#### 버그를 실제로 재현하고 도구로 잡기 — `demo_bugs.c`

<!-- code: demo_bugs.c -->

```bash
cc -std=c11 -Wall -Wextra -O2 -pthread demo_bugs.c -o demo_bugs && ./demo_bugs ok && ./demo_bugs torn && ./demo_bugs after_qbuf
cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_bugs.c -o demo_bugs_tsan && ./demo_bugs_tsan after_qbuf 1500
```

실제 출력 (OVERWRITE_OLDEST 설정, 3 가지 모드):

```text
mode=ok frames=4000 delivered=1570 dropped=2430 flushed=0
  검출 도구: payload 찢어짐=0  poison_hits=0  소유권 불변식 위반=0  stale 쓰기 시도=0
mode=torn frames=4000 delivered=1489 dropped=0 flushed=0
  검출 도구: payload 찢어짐=6  poison_hits=8  소유권 불변식 위반=15230  stale 쓰기 시도=0
mode=after_qbuf frames=4000 delivered=1624 dropped=2376 flushed=0
  검출 도구: payload 찢어짐=0  poison_hits=1621  소유권 불변식 위반=0  stale 쓰기 시도=1624
```

- `torn` — 드라이버 버그. EOF 때 다음 버퍼가 없는데 DONE 으로 앱에 넘기고, HW 는 **같은 버퍼에 계속 쓴다.** 내용 찢어짐은 6 건뿐이다(시간차를 잡아야 보인다). 그러나 불변식 검사는 같은 상태를 15230 번 잡는다 — **상태 불변식은 결정적으로, 내용 검사는 운이 좋을 때만** 잡는다. 드물게 보이는 현상을 쫓기 전에 불변식을 켜라. `dropped=0` 도 눈여겨보자: 프레임을 안 버린 것처럼 보이지만 사실은 앱에게 쓰는 도중의 버퍼를 준 것이다.
- `after_qbuf` — 앱 버그. qbuf 한 뒤 들고 있던 포인터로 썼다. `poison_hits=1621` 이 그 버퍼를 지목한다.
- `ok` — 위 두 버그를 고친 경로. 같은 도구로 0 이다. TSan 도 깨끗하다.

TSan 이 잡는 모습 (실제 리포트, 스택 일부만 잘라 냄):

```text
==================
WARNING: ThreadSanitizer: data race (pid=44254)
  Write of size 4 at 0x00016b5065a4 by main thread:
    #0 main demo_bugs.c:90 (db_t:arm64+0x100002158)

  Previous write of size 4 at 0x00016b5065a4 by thread T1:
    #0 cap_hw_frame solution.c:276 (db_t:arm64+0x100001278)
    #1 prod demo_bugs.c:55 (db_t:arm64+0x1000022f0)
…
SUMMARY: ThreadSanitizer: data race demo_bugs.c:90 in main
```

`demo_bugs.c:90` 이 qbuf 뒤의 쓰기, `solution.c:276` 이 DMA 흉내(`cap_hw_frame` 의 `mem[]` 쓰기)다. 이 TSan 실행의 종료 코드는 134 (맥 TSan 은 race 가 있으면 134 로 끝난다). `ok` 모드는 1500 프레임에서 경고 0, 종료 코드 0 이었다 — 똑같이 락 없이 `mem[]` 을 쓰는데도.

이 레슨의 세 단계를 한 표로 묶으면 (8층의 공식: 버그 재현 → 도구로 검출 → 같은 도구로 수정 확인):

| 재현한 버그 | 검출 도구 | 실제 결과 | 수정 | 같은 도구의 결과 |
|---|---|---|---|---|
| 다음 버퍼 없이 완료 (`torn`) | 불변식, 내용 패턴, TSan | 불변식 위반 15230, 찢어짐 6, TSan 경고 2 (1500 프레임) | EOF 정책 3종 중 하나 | 위반 0, 찢어짐 0, TSan 0 (`ok`) |
| qbuf 뒤 stale 포인터 쓰기 (`after_qbuf`) | poison, TSan | poison 1621 (TSan 빌드 114), TSan 경고 1 | qbuf 뒤 포인터 버리기 | poison 0, TSan 0 |
| qbuf 가 EOF 직후 도착 (강제 스케줄) | sequence 공백 | `dropped==1`, seq 1 소실 | qbuf 를 EOF 전에(프레임 도중 훅) | `dropped==0` |
| DMA 도중 STREAMOFF (버퍼 회수 시도, 강제 스케줄) | `cap_streamoff` 반환값, 보존 법칙 | DMA 중이면 `-EBUSY` 로 거절 | `dma_busy` 가 풀린 뒤 회수 | 프레임 완료 후 성공, `frames == dequeued + dropped + flushed` |

참고로 `torn` 의 TSan 경고 수와 불변식 숫자는 1500 프레임 TSan 빌드와 4000 프레임 일반 빌드가 섞여 있으니 모드별 절대값으로 비교하지 말자 (표의 "TSan 빌드 114" 처럼 빌드별로 다르다).

실제 HW 의 DMA 엔진은 스레드가 아니라서 TSan 이 못 본다. 진짜 시스템에서는 **poison + 불변식 + 내용 패턴**이 도구고, 이 모델의 TSan 은 "소유권 이전이 올바르게 만들어졌는지" 를 스레드 모델에서 검증해 주는 보조 수단이다.

#### 캐시 유지보수는 상태 머신의 어디에 들어가나

ARCH-M12 가 배운 "RX: 시작 전 clean(또는 clean+invalidate), 완료 후 invalidate" 를 이 상태 머신에 붙이면:

| 전이 | 무엇을 | vb2 에서 실제 위치 (6.18.55) |
|---|---|---|
| QBUF (DEQUEUED → QUEUED) | 장치에게 넘기기 전 (dirty 라인 정리) | `vb2_core_qbuf` → `__buf_prepare` → `__vb2_buf_mem_prepare` (1596 행) → memop `prepare` |
| ACTIVE → DONE (완료) | CPU 가 읽기 전에 stale 라인 제거 | `vb2_buffer_done` → `__vb2_buf_mem_finish` (1206 행) → memop `finish` |
| DONE → ACTIVE (빼앗기, 이 모델만) | 다시 장치에게 | vb2 에는 이 전이가 없음 |

이 레슨에서는 `cache_for_device` / `cache_for_cpu` 가 **호출 순서만** 검사한다(실제 캐시 시뮬레이션은 ARCH-M12). 시험이 잡는 것: 빼앗기 경로에서 `cache_for_device` 를 빠뜨리면 다음 완료에서 `sync_err` 가 올라가고, 불변식(`cpu_view` 가 상태와 맞아야 함)도 어긋난다.

vb2_buffer_done 이 **완료 IRQ 안에서** `finish` 를 호출한다는 점을 주의하자. 앱이 dqbuf 하기 전에 이미 캐시가 CPU 쪽으로 돌아 있는 것이다. 동일 소스에서 `done_lock` 은 `spin_lock_irqsave` 로 잡는다(`vb2_buffer_done`, 1208 행) — IRQ 컨텍스트에서 mutex 를 쓸 수 없기 때문이고, 이 레슨 모델의 뮤텍스는 그 자리를 대신한다 (IRQ 와 공유할 때의 락 선택은 ELX-M04).

`vb2_dc_finish` 는 `non_coherent_mem` 일 때만 sync 하므로(`videobuf2-dma-contig.c`), 비일관 메모리일 때와 coherent 할당일 때 비용이 다르다는 것도 같이 말할 수 있다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution   # 143/143 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o s_asan && ./s_asan
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o s_tsan && ./s_tsan
```

검증 결과: strict 빌드 경고 0, 6회 연속 143/143 통과, ASan+UBSan 깨끗, TSan 깨끗(경고 0, 종료 코드 0).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고침 |
|---|---|---|
| 검증 전에 poison 칠하기 | double qbuf 에서 DMA 중인 버퍼를 덮어쓴다 | 상태 검증 → 통과 후 poison |
| 다음 버퍼 없이 완료 처리 | HW 가 계속 그 버퍼에 쓰는데 앱도 가져간다 (찢어진 프레임, `demo_bugs torn`) | 다음 버퍼가 없으면 정책대로. "HW 는 언제나 대상을 가진다" 를 불변식으로 |
| 버린 프레임에 sequence 를 안 올림 | 앱이 공백을 볼 수 없다 | SOF 에서 무조건 `hw_seq++` |
| `mem[]` 을 락 안에서 씀 | 프레임이 수 MB 면 락을 수 ms 잡는다. IRQ 지연이 터진다 | 락은 상태 전이만, 내용은 소유권으로 |
| dqbuf 대기를 `if` 로 | 깨어났는데 다른 이유일 수 있다 (OS-B06) | `while (fifo_empty)` |
| 앱이 포인터를 qbuf 뒤에도 사용 | 소유권이 넘어갔다. 읽으면 poison, 쓰면 프레임 파괴 | qbuf 는 "이 포인터를 잊겠다" 는 약속 |
| STREAMOFF 가 DMA 안 기다림 | 회수한 버퍼에 DMA 가 계속 씀 (해제한 메모리일 수도) | `dma_busy` 끝난 뒤 회수. vb2 는 stop_streaming 뒤 `owned_by_drv_count` 를 WARN 으로 확인한다 |
| 빼앗은 DONE 버퍼에 캐시 준비 누락 | 완료 때 CPU 쪽으로 돌린 뒤 장치에 다시 줌 — stale 라인 위험 (ARCH-M12) | 빼앗을 때 `cache_for_device` |
| `(int)(seq-expected) < 0` 으로 wrap 판단 | 2^31 이상 변환은 implementation-defined (C11 6.3.1.3) | `uint32_t diff` 를 `0x7FFFFFFF` 와 비교 |

## 8. 면접 Q&A

<details><summary>Q1. 캡처 버퍼의 소유권을 말로 설명해 보세요. 왜 프레임 내용에는 락을 안 거나요?</summary>

DEQUEUED(앱) → QBUF → QUEUED(드라이버) → EOF 때 ACTIVE(HW) → 완료 IRQ → DONE(드라이버) → DQBUF → DEQUEUED. 언제나 주인은 하나다. 프레임은 수 MB 라 복사하거나 락으로 감싸면 IRQ 지연과 전력이 터진다. 그래서 내용 대신 **권리**를 넘긴다: 상태 전이는 짧은 락(드라이버의 spinlock), 내용 접근은 주인만. 꼬리: "그럼 이게 왜 race 가 아닌가?" → 락 해제→획득이 경계가 되어 HW 의 쓰기 전체가 앱의 읽기보다 먼저다. 스레드 모델에서 TSan 으로 실제로 확인했다(`mem[]` 쓰기를 락 없이 해도 경고 0). 실제 HW 는 스레드가 아니므로 이 경계를 **완료 인터럽트와 캐시 유지보수**가 맡는다 (Q3).
</details>

<details><summary>Q2. 소비자가 느려서 버퍼가 바닥나면 어떻게 합니까?</summary>

세 가지 선택. **새 프레임 버림**(ACTIVE 유지 — 스톨 시작 직전 프레임 보존), **마지막 버퍼 재사용**(ACTIVE 덮어쓰기), **가장 오래된 DONE 덮어쓰기**(최신 프레임 우선). 측정: 버퍼 4개, 소비자 3 프레임에 1개일 때 최대 나이는 각각 11 / 8 / 2 프레임이고, 드롭 수는 거의 같다(37/38/38). 고르는 것은 "어느 프레임을 버리느냐" 이고 인지·제어라면 최신, 기록기면 끊김 없는 쪽이다. 꼬리: "vb2 드라이버에서 OVERWRITE_OLDEST 를?" → 6.18.55 소스에서 DONE → ACTIVE 경로가 없으므로 드라이버로는 못 한다. 앱이 빨리 dqbuf 해서 가장 새 것만 쓰고 나머지를 바로 돌려줘야 한다. 꼬리2: "실제 드라이버 예?" → rcar-vin 은 스크래치 버퍼로 돌리며 `sequence` 를 올리고, stm32-dcmi 는 버퍼가 올 때까지 멈춘다(소스 확인).
</details>

<details><summary>Q3. 캐시 유지보수는 어디에 들어가요? 왜 거기죠?</summary>

비일관 DMA 라면 QBUF 때 장치 쪽으로(더티 라인 정리), 완료 때 CPU 쪽으로(stale 라인 제거) — ARCH-M12 의 순서 그대로. 6.18.55 vb2 는 `__buf_prepare` → `__vb2_buf_mem_prepare`(qbuf 경로), `vb2_buffer_done` → `__vb2_buf_mem_finish`(IRQ 경로)다. dqbuf 가 아니라 완료 때 처리하는 이유: CPU 가 처음 볼 수 있는 시점 전에 끝내야 하고, 이미 DONE 이 된 순간부터 앱이 언제든 꺼낼 수 있으니까. 꼬리: "coherent 메모리면?" → dma-contig 의 `vb2_dc_finish` 는 non-coherent 일 때만 sync 한다. 대가는 uncached 접근의 CPU 비용 (ARCH-M12 비용 표).
</details>

<details><summary>Q4. 프레임 드롭을 앱이 어떻게 아나요? 믿을 수 있나요?</summary>

`sequence` 공백: `missed = seq - expected`(uint32_t wrap 자연 처리, 2^31 이상 차이는 역행). V4L2 문서는 sequence 가 "includes dropped or repeated frames" 라고 명시한다. 단, **드라이버가 드롭을 세어 줄 때만** 믿을 수 있다. 소스를 보면 stm32-dcmi 는 완료할 때만 `sequence++` 해서 버퍼를 기다리는 동안의 프레임은 공백이 안 된다. rcar-vin 은 스크래치 경로에서도 `sequence++` 한다. 그래서 타임스탬프 간격 검사를 함께 쓴다(CAM-M12 참고). 꼬리: "공백 합계가 드롭 합계와 같나요?" → 아니다. 시험 결과에서처럼 마지막 전달 뒤에 잃은 프레임은 공백으로 안 보인다 (`gaps <= dropped + flushed`).
</details>

<details><summary>Q5. IRQ 와 공유하는 이 큐에 어떤 락을 쓰죠? mutex 는요?</summary>

리눅스 커널이면 프로세스 컨텍스트와 IRQ 컨텍스트가 같은 자료를 만지므로 `spin_lock_irqsave`(프로세스 쪽). mutex 는 잠들 수 있으므로 IRQ 컨텍스트에서 쓰면 안 된다 (ELX-M04). 실제 vb2 도 `done_list` 는 `done_lock` 을 `spin_lock_irqsave` 로 지키고(`vb2_buffer_done`), dqbuf 대기는 프로세스 컨텍스트의 `wait_event_interruptible` 이다. 이 레슨의 맥 모델은 IRQ 가 스레드이므로 mutex + condvar 로 대신했다. 꼬리: "왜 짧아야 하나요?" → 락 안에서 프레임을 건드리면 IRQ 지연과 다른 코어 스핀이 프레임 크기에 비례해 늘어난다. 내용은 소유권으로 처리.
</details>

<details><summary>Q6. STREAMOFF 를 안전하게 구현하려면?</summary>

순서가 중요하다: (1) HW 시작을 멈추고, (2) 진행 중인 DMA 가 끝나길 기다리고, (3) 그다음 모든 버퍼를 DEQUEUED 로 돌리고, (4) 앱 대기자를 깨운다. (2) 없이 회수하면 앱이 해제한 메모리에 DMA 가 계속 쓴다(가장 찾기 어려운 오염 부류). 이 레슨은 `dma_busy` 로 강제 시험한다 — DMA 도중 streamoff 는 `-EBUSY`. vb2 도 `__vb2_queue_cancel` 에서 `stop_streaming` 뒤 드라이버가 ACTIVE 버퍼를 돌려줬는지 WARN 으로 확인한다 (`owned_by_drv_count`, "driver bug: stop_streaming operation is leaving buffer %u in active state"). 꼬리: "늦게 도착하는 IRQ 는요?" → epoch/세대 번호로 버린다 (추가 연습 2).
</details>

<details><summary>Q7. "이 프레임 안에서만" 나는 경쟁 같은 타이밍 버그는 어떻게 재현합니까?</summary>

벽시계로 흔들지 않는다. HW 시뮬레이션에 **훅을 심어 정확한 지점에 원하는 동작을 끼운다** — 이 레슨의 `mid_dma_hook` 은 DMA 절반 뒤 qbuf 를 넣어 "EOF 전에 온 qbuf 는 살고, EOF 직후는 드롭" 을 매번 똑같이 만든다. 진짜 스레드가 필요하면 rendezvous 로 순서를 강제한다(`gate_fire`). 더 넓게는 시드로 순서를 탐색하는 결정론적 시뮬레이션(TEST-A05)이고, 진짜 HW 에서는 트래픽을 겹쳐 거는 스트레스(SIV-M09)다. 꼬리: "자유 실행 시험은 뭘 CHECK 합니까?" → 스케줄에 의존하지 않는 것만 — 보존 법칙, 불변식, 내용 패턴, 단조성.
</details>

<details><summary>Q8. poison 패턴은 무엇을 잡고, 무엇을 못 잡나요?</summary>

앱이 qbuf 뒤 버퍼에 **쓰면** 다음 arm 에서 깨진 패턴으로 잡힌다. 넘긴 뒤 **읽으면** poison 또는 다른 프레임이 보이므로 앱의 내용 검증이 잡는다. 못 잡는 것: 패턴과 같은 값을 쓰는 경우, ACTIVE 동안 쓰기(이건 내용 검증이 잡음), 프레임이 커서 poison 비용이 부담스러운 양산 빌드(디버그 전용이다). 실제 시스템의 비슷한 도구로는 커널의 KASAN/slab poison, 버퍼 앞뒤의 guard 패턴이 있고, MMU 있는 시스템에서는 큐에 있는 동안 unmap/PROT_NONE 으로 바로 fault 를 낼 수도 있다 (dmabuf 기반 파이프라인에서 쓰이는 방식이라고 알려져 있음 — 미검증).
</details>

## 9. 추가 연습

### 연습 1 — Triple buffer: 최신 프레임만 교환하는 lock-free 메일박스

생산자(DMA 완료 쪽)가 **절대 기다리지 않아야** 하면 버퍼 3개와 원자적 교환 하나로 된다: 생산자가 다 쓴 버퍼와 교환 슬롯을 `atomic_exchange` 로 맞바꾼다. 소비자도 새 프레임 플래그가 있으면 맞바꾼다. practice.c 의 P1 에서 게시 3번 뒤 소비자는 최신 프레임만 받고, 자유 실행에서 찢어진 프레임 0 임을 TSan 으로 확인한다.

<details><summary>답</summary>

`atomic_exchange_explicit(&slot, w | NEW, memory_order_acq_rel)` — 내가 쓴 내용을 내보내고(release), 받아 온 버퍼를 소비자가 다 읽은 뒤의 것으로 본다(acquire). 그래서 양쪽 다 `acq_rel` 이다 (C-A03, DS-A01). 생산자는 항상 쓸 수 있는 버퍼 하나를 갖고 있어 기다리지 않는다. 대가: 전달 보장이 없다(중간 프레임은 사라진다). 카메라 → 표시 경로의 "latest frame" 과 같은 구조다 (ROS 2 의 KEEP_LAST depth 1 과 의도가 비슷하지만 메커니즘은 다르다). 실측: 자유 실행에서 소비자는 200000 프레임 중 76020 개를 받았다 (매번 다름). 이 경로는 OVERWRITE_OLDEST 처럼 **순서가 있는 FIFO 가 아니다.** 중간에 무엇이 있었는지 알 필요가 없을 때 쓴다.
</details>

### 연습 2 — 늦게 도착한 완료 IRQ를 버리기

STREAMOFF → STREAMON 사이에 지난 프레임의 완료 IRQ 가 늦게 도착하면, 이번에 막 건 버퍼를 "완료" 로 처리해 버린다. IRQ 에 epoch(세대) 번호를 붙여 버려라.

<details><summary>답</summary>

`q2_irq_done(q, irq_epoch)` 에서 `irq_epoch != q->epoch` 면 무시. 시험: epoch 없이 같은 순서로 돌리면 시작도 안 한 버퍼 6 이 완료 목록에 들어간다(`ncomp==1`, 버그 재현), epoch 를 쓰면 `stale==1`, `ncomp==0`. 본문 Q6 의 "늦은 IRQ" 답이고, TEST-A05 의 "한 요청에 완료가 둘" 모양과 같다. 실제 HW 는 세대 번호를 레지스터 태그나 DMA 디스크립터에 넣거나, streamoff 때 IRQ 를 동기화(`synchronize_irq`)해서 같은 일을 한다.
</details>

### 연습 3 — 소비자 둘이 같은 버퍼를 읽는 fan-out

인코더와 ISP 통계가 같은 프레임을 동시에 읽는다. "주인은 한 명" 규칙을 어떻게 바꾸나? practice.c 의 P3 으로 답한다.

<details><summary>답</summary>

쓰기 주인은 여전히 한 명(HW)이지만, 읽기는 여러 명이 동시에 가능하다. 대신 **돌려줄 책임**을 하나로 합친다: 참조 카운트를 소비자 수로 올리고, 각자 끝나면 내리며, 0 이 된 사람이 qbuf 한다. `q3_release` 가 0 인 버퍼를 또 내리면 `-EINVAL` — double-free 와 같은 종류의 버그 검출. DMA-BUF fd 공유가 이 구조에 가깝다 (참조 수 관리는 파일 디스크립터가 맡는다).
</details>

### 연습 4 — NEXT 주소 레지스터: qbuf 는 한 프레임 일찍

HW 가 다음 프레임 주소를 NEXT 레지스터에 미리 들고 프레임 경계에서 스스로 갈아탄다면(STM32 DMA 의 double buffer mode, 많은 ISP 의 shadow 레지스터 — CAM-A04), 버퍼 부족 시 어떤 일이 생기나?

<details><summary>답</summary>

HW 는 프레임 경계에서 NEXT 만 본다. 드라이버는 경계 뒤 ISR 에서 inq 를 비워 NEXT 를 다시 채운다. 그래서 어떤 프레임이 끝날 때 쓸 버퍼는 **그 전 프레임 동안** 이미 NEXT 에 있어야 한다 — 소프트웨어의 qbuf 는 한 프레임 일찍 필요하다. practice P4: 같은 타이밍의 qbuf 가 1-슬롯 모델에서는 살아남고(`done=1, drops=0`) NEXT 슬롯 모델에서는 그 경계를 놓친다(`drops=1, next=3`, 한 프레임 뒤에야 갈아탐). 면접 답: 그래서 NEXT 슬롯이 있는 하드웨어는 **최소 버퍼 수가 하나 더** 필요하고, V4L2 의 `min_queued_buffers` 가 이런 조건을 드러낸다 (대응 관계는 드라이버마다 다름 — 일반화는 주의).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice   # 19/19 passed
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o p_tsan && ./p_tsan
```

## 10. 핵심 정리

- 상태 네 개, 주인 한 명: 앱(DEQUEUED) · 드라이버(QUEUED, DONE) · HW(ACTIVE).
- 락은 상태 전이만, 프레임 내용은 소유권으로 보호한다 — 락 해제→획득이 소유권 이전의 경계.
- 다음 버퍼 없이 완료하면 HW 가 앱 버퍼에 계속 쓴다(찢어짐). streaming 중 "ACTIVE 는 항상 하나" 가 불변식.
- 정책: DROP_NEW(새것 버림) · REUSE_LAST(ACTIVE 덮어쓰기) · OVERWRITE_OLDEST(오래된 DONE 빼앗기). 드롭 수는 비슷하고 **나이가 다르다** (11 / 8 / 2 프레임).
- 드롭 프레임도 sequence 를 소비해야 공백이 보인다. 공백 검출은 드라이버의 협조에 달려 있다. wrap 은 uint32_t 뺄셈.
- 도구: 소유권 불변식(결정적), poison(앱의 넘긴 뒤 쓰기), 내용 패턴(찢어짐), TSan(스레드 모델), 강제 스케줄(재현).
- 캐시: QBUF 에서 장치 쪽(`prepare`), 완료(IRQ, `vb2_buffer_done`)에서 CPU 쪽(`finish`) — dqbuf 가 아니다.
- STREAMOFF 는 DMA 가 끝난 뒤에 회수. 늦은 IRQ 는 epoch 로 버린다.

## 참고자료

- Linux 6.18.55 소스 (`/var/lab/src/linux-6.18.55` 에서 직접 읽음): `include/media/videobuf2-core.h`(`enum vb2_buffer_state`), `drivers/media/common/videobuf2/videobuf2-core.c`(`vb2_buffer_done`, `__enqueue_in_driver`, `vb2_core_qbuf`, `__vb2_buf_mem_prepare`, `__vb2_buf_mem_finish`, `__vb2_wait_for_done_vb`, `__vb2_queue_cancel`), `videobuf2-dma-contig.c`(`vb2_dc_prepare/finish`), `drivers/media/platform/renesas/rcar-vin/rcar-dma.c`(스크래치 버퍼), `drivers/media/platform/st/stm32/stm32-dcmi.c`(`WAIT_FOR_BUFFER`).
- V4L2 Userspace API — buffer 구조체, `VIDIOC_QBUF/DQBUF`, sequence 의미 (`Documentation/userspace-api/media/v4l/buffer.rst`, `vidioc-qbuf.rst`, 6.18.55): https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html
- 관련 레슨: OS-B06 (condvar), ELX-M04 (컨텍스트별 락), ARCH-M12 (DMA 캐시 유지보수), DS-A01 (SPSC 링), CAM-A01 (실제 V4L2 mmap 캡처 앱), CAM-M12 (타임스탬프), SIV-M09 (동시성 스트레스), TEST-A05 (결정론적 시뮬레이션).
