---
id: ARCH-M14
time: 95분
updated: 2026-11-11
---
# ARCH-M14 AXI 핸드셰이크 모델: 5채널과 VALID/READY

## 학습 목표

- AXI4 의 다섯 채널(AW / W / B / AR / R)이 **각자 독립된 VALID/READY 핸드셰이크** 를 가진다는 것을 사이클 모델로 만들고, "VALID 는 handshake 까지 내리지도 바꾸지도 않는다 / READY 는 마음대로" 라는 비대칭을 코드 한 줄 단위로 설명한다.
- INCR 버스트의 beat 주소(비정렬 시작 포함)와 4 KB 경계 규칙을 계산하고, 4 KB 규칙이 왜 있는지(버스트가 슬레이브 경계를 넘지 않게)를 말한다.
- 다중 ID 응답 스케줄러를 만들어 **다른 ID 끼리는 순서가 바뀌고(out-of-order), 같은 ID 끼리는 낸 순서대로** 돌아오는 것을 파형으로 확인한다.
- 버스를 보기만 하는 수동(passive) 프로토콜 체커를 작성하고, 규칙 7개(VALID 유지, payload 안정, WLAST, B 의존성 2가지, RLAST, 같은 ID 순서, 4 KB)마다 **버그를 주입해서 체커가 그 규칙으로 잡는지** 검증한다 (kill matrix).
- outstanding 수와 대역폭의 관계(Little 의 법칙)를 닫힌 식으로 구해 시뮬레이션과 사이클 단위로 맞추고, "VALID 가 READY 를 기다리면" 왜 데드락이 되는지 — 그리고 왜 시뮬레이션이 그 버그를 숨기는지 — 를 보인다.

## 1. 문제

"VALID 가 READY 를 기다리면 안 되는 이유는?", "같은 ID 응답 순서는 누가 보장하죠?", "4 KB 는 왜 넘으면 안 되나요?" — SoC·실리콘 검증·AI 가속기 면접에서 AXI 는 *규칙을 아는가* 와 *그 규칙을 검사하는 코드를 짤 수 있는가* 두 가지로 나온다. Rivian/NVIDIA 같은 실리콘 검증 직무에서는 후자가 실제 일이다: IP 하나를 받으면 트래픽을 만들고, 버스를 지켜보는 체커와 스코어보드로 "규칙 위반 0, 데이터 일치" 를 보인다.

AXI 는 읽기와 쓰기를 **다섯 개의 단방향 채널** 로 나눈다. 채널마다 보내는 쪽이 `VALID` 와 내용(payload)을, 받는 쪽이 `READY` 를 낸다. **같은 클럭 상승 엣지에서 VALID 와 READY 가 둘 다 1 이면 전송(handshake) 1번.** 채널끼리는 기본적으로 독립이고, 스펙이 정한 몇 가지 순서 관계만 지키면 된다.

```svg
<svg viewBox="0 0 740 372" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">AXI4 다섯 채널 — 방향, 이 레슨 모델이 싣는 필드, 채널 사이 규칙</text>
  <rect x="15" y="40" width="140" height="270" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="30" y="64" fill="currentColor" font-weight="bold">Manager</text>
  <text x="30" y="84" fill="currentColor" font-size="12">(마스터: CPU, DMA)</text>
  <rect x="585" y="40" width="140" height="270" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="600" y="64" fill="currentColor" font-weight="bold">Subordinate</text>
  <text x="600" y="84" fill="currentColor" font-size="12">(슬레이브: SRAM, DDR)</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M155 105 H580"/><path d="M572 99 L580 105 L572 111"/>
    <path d="M155 150 H580"/><path d="M572 144 L580 150 L572 156"/>
    <path d="M585 195 H160"/><path d="M168 189 L160 195 L168 201"/>
    <path d="M155 245 H580"/><path d="M572 239 L580 245 L572 251"/>
    <path d="M585 290 H160"/><path d="M168 284 L160 290 L168 296"/>
  </g>
  <g fill="currentColor">
    <text x="170" y="98">AW  쓰기 주소: AWID AWADDR AWLEN AWSIZE (INCR)</text>
    <text x="170" y="143">W   쓰기 데이터: WDATA WLAST (WSTRB 생략)</text>
    <text x="170" y="188">B   쓰기 응답: BID BRESP</text>
    <text x="170" y="238">AR  읽기 주소: ARID ARADDR ARLEN ARSIZE (INCR)</text>
    <text x="170" y="283">R   읽기 데이터: RID RDATA RRESP RLAST</text>
  </g>
  <g fill="currentColor" font-size="12" opacity=".8">
    <text x="170" y="119">VALID·payload → / ← READY</text><text x="170" y="164">AW 보다 먼저 와도 됨 · 순서 = AW 순서 (WID 없음)</text>
    <text x="170" y="209">BVALID: AW 와 마지막 W handshake 이후 (AXI4)</text>
    <text x="170" y="259">VALID·payload → / ← READY</text><text x="170" y="304">RVALID: AR handshake 이후 · 다른 RID 끼리 순서 자유</text>
  </g>
  <rect x="15" y="318" width="710" height="48" rx="4" class="f-warn" opacity=".35"/>
  <text x="25" y="337" fill="currentColor">공통 규칙: VALID 는 READY 를 기다리면 안 되고, 올리면 handshake 까지 유지 + payload 고정.</text>
  <text x="25" y="357" fill="currentColor">READY 는 VALID 를 기다려도, 미리 올려도, 내려도 된다.</text>
</svg>
```

| 규칙 (AXI4, IHI 0022) | 뜻 | 체커 이름 | 주입하는 버그 |
|---|---|---|---|
| VALID 유지 | VALID=1 을 올리면 handshake 까지 내리지 않는다 | `VALID_DROP` | 기다리는 중 VALID 를 1 사이클 내림 (AR, W) |
| payload 안정 | VALID=1, READY=0 인 동안 내용 고정 | `PAYLOAD_CHANGE` | 기다리는 중 데이터/주소 비트 하나 뒤집기 (W, AW) |
| WLAST | 버스트의 마지막(AWLEN+1 번째) W beat 에만 WLAST=1 | `WLAST` | 매니저가 하나 일찍 WLAST |
| B ≥ 마지막 W | 마지막 W handshake **이후** 에 BVALID | `B_BEFORE_WLAST` | 서브가 AW 만 보고 B |
| B ≥ AW (AXI4) | AW handshake **이후** 에 BVALID | `B_BEFORE_AW` | 서브가 W 만 보고 (AW 전에) B |
| RLAST | ARLEN+1 번째 R beat 에만 RLAST=1 | `RLAST` | 서브가 하나 일찍 RLAST |
| 같은 ID 순서 | 같은 ID 응답은 낸 순서대로 | `SAME_ID_ORDER` | 서브가 같은 ID 안에서 젊은 응답 먼저 |
| 4 KB | 버스트가 4 KB 경계를 넘지 않는다 | `CROSS_4KB` | 드라이버가 페이지 끝에 걸친 읽기 |

레인(lane): 배타 접근·EXOKAY 는 SOCX-B03, WRAP/FIXED·narrow 전송의 byte lane·WSTRB 는 SOCX-B02(다음 단계), RTL 로 짠 valid/ready 와 SVA 는 RTLDV-B10/RTLDV-A04, 매니저 쪽 ID 재정렬 버퍼는 SOCX-M02 가 맡는다. 여기서는 INCR 만, 데이터는 beat 주소의 2^size 바이트를 하위 정렬로 싣는 **단순화** 를 쓴다. 버스 대기 상태 기초는 ARCH-B12, AHB/APB 사이클 모델은 ARCH-M13.

### Part A — 5채널 사이클 모델

- `axi_beat_addr(addr, size, n)`: INCR n 번째 beat 주소. 첫 beat 는 시작 주소 그대로, 이후는 정렬된 시작 주소 + n·2^size.
- `axi_crosses_4k(addr, len, size)`: 마지막 바이트가 다른 4 KB 페이지면 true.
- 소스 `src_drive` / `src_edge`: 큐의 항목을 무작위 지연(0..max) 뒤 VALID 로 내고, **handshake 전에는 VALID·payload 를 바꾸지 않는다**. READY 는 인자로 받지도 않는다.
- 응답 스케줄러 `sub_pick`: 준비된 응답 중 "같은 ID 에 더 오래된 것이 없는" 것만 후보, 후보 중 무작위.
- 매니저·서브오디네이트·시뮬레이터 루프는 주어진다. 테스트: 주소 표 + 2만 개 무작위 대조, 소스 단위 테스트(READY 패턴 무작위 5000 사이클), 스케줄러 단위 테스트, 무작위 트래픽 2개를 끝까지 돌려 메모리·읽은 데이터·응답 코드 일치 + 위반 0.

### Part B — 수동 프로토콜 체커 (면접관이 찌르는 곳)

체커는 매 사이클 **선(wire) 값만** 본다: 채널마다 `{valid, ready, payload}`. 내부 상태(누가 몇 번째 beat 를 보냈는지)는 모른다.

- `chk_stable`: 규칙 1·2 (모든 채널 공통).
- `chk_w_beat`: W beat 를 세고 WLAST 검사. W 가 AW 보다 먼저 온 버스트는 AW 가 올 때 뒤늦게 검사(주어진 `chk_on_ax`).
- `chk_b`: 새로 올라온 BVALID 마다 "같은 BID 의 가장 오래된 미완료 AW" 를 찾아 AW·마지막 W 가 **이전 사이클** 에 끝났는지, BRESP 가 기대값인지.
- `chk_r`: R beat 마다 같은 RID 의 가장 오래된 미완료 AR 에 붙여 RLAST·데이터 검사. 데이터가 틀렸는데 같은 ID 의 더 젊은 AR 데이터와 맞으면 → 순서 위반.
- 테스트: 손으로 만든 사이클 열 12개(합법 3개 포함) + 전체 시뮬레이션에 버그 주입 10가지(8종, 그중 2종은 채널 두 곳) + 버그 없는 대조군 → **처음 잡힌 위반이 정확히 그 규칙** 인지.

### Part C — outstanding, 데드락, 회귀

- `ot_needed(lat, beats)`: R 채널을 쉬지 않게 하는 최소 outstanding. `pred_read_cycles(n, beats, lat, k)`: k 개 outstanding 일 때 총 사이클 닫힌 식. 시뮬레이션과 **사이클 단위로 같아야** 한다 (k = 1, 2, 3, 4, 6, 8).
- 데드락: "READY 를 본 뒤에만 VALID 를 올리는" (규칙 위반) 매니저 × 세 가지 합법 서브. 하나와는 영원히 멈추고, 둘과는 멀쩡히 동작한다.
- 회귀: 시드 200개 × 40 트랜잭션 깨끗한 트래픽 전부 통과 + 버그 8종 × 시드 50개 kill matrix 전부 검출.

## 2. Problem (English)

> **AXI4 handshake model and protocol checker.**
>
> 1. Model the five AXI4 channels (AW, W, B, AR, R) at cycle level. A transfer occurs on a channel in a cycle where both VALID and READY are high. A source must not wait for READY before asserting VALID, and once VALID is asserted it must stay asserted with stable payload until the handshake. Implement INCR beat addresses (unaligned start allowed) and a 4 KB boundary check. Implement a subordinate response scheduler that returns responses out of order across IDs but in issue order within an ID. Drive the model with seeded random VALID delays and READY patterns; final memory, read data and responses must match a reference.
> 2. Write a passive checker that sees only per-cycle `{valid, ready, payload}` of each channel and flags: VALID dropped before handshake; payload changed while `VALID && !READY`; WLAST on the wrong beat; BVALID before the last W handshake; BVALID before the AW handshake (AXI4); RLAST on the wrong beat; same-ID response reordering; a burst crossing 4 KB. For every rule, inject a matching bug into the model and show that the checker's first violation is that rule.
> 3. With zero delays and always-ready, reads of `beats`-beat bursts and subordinate latency `lat`: derive the minimum outstanding count that keeps R busy and a closed form for total cycles with `k` outstanding; match simulation exactly. Show that a manager whose VALID waits for READY deadlocks with a legal subordinate whose READY waits for VALID, and explain why a passive checker cannot see it.
>
> Example (lat = 20, 8-beat bursts, 48 reads): round trip 29 cycles, `ot_needed = 4`; k = 1 → 1392 cycles, k = 4 → 405 cycles (= 20 + 1 + 48·8).

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

실제로 돌린 결과 (처음엔 대부분 FAIL, exit 1):

```text
FAIL starter.c:571  axi_beat_addr(0x1000, 2, 0) == 0x1000 && axi_beat_addr(0x1000, 2, 3) == 0x100C
FAIL starter.c:572  axi_beat_addr(0x1002, 2, 0) == 0x1002 && axi_beat_addr(0x1002, 2, 1) == 0x1004
...
FAIL starter.c:642  g_c.first == V_VALID_DROP && g_c.first_ch == CH_AR && g_c.first_t == 1
...
info: VALID_DROP      0/50
...
info: CROSS_4KB       0/50
FAIL starter.c:748  all
8/55 passed
```

TODO 는 11곳: `axi_beat_addr`, `axi_crosses_4k`, `src_drive`, `src_edge`, `sub_pick` (Part A), `chk_stable`, `chk_w_beat`, `chk_b`, `chk_r` (Part B), `ot_needed`, `pred_read_cycles` (Part C). 시작 코드의 소스는 VALID 를 영영 안 올리므로 모든 시뮬레이션이 "300 사이클 동안 handshake 없음" 감시(watchdog)에 걸려 멈춘다 — 무한 루프는 없다. 처음부터 통과하는 8개는 "위반이 없어야 한다 / 멈춰야 한다" 쪽 테스트(합법 파형, 버그 없는 대조군, 데드락 실험 등)라 빈 스텁으로도 맞는다.

## 4. 힌트

<details>
<summary>힌트 1 — 한 사이클의 순서</summary>

시뮬레이터 루프(주어짐)는 ① 매니저가 새 트랜잭션을 큐에 넣고 서브가 응답 beat 를 준비 ② **소스가 VALID·payload 를 먼저** 정하고 ③ 싱크가 READY 를 정하고(VALID 를 봐도 됨) ④ 체커가 관찰 ⑤ 엣지에서 `hs = valid && ready` 로 상태를 바꾼다. 그래서 `src_drive` 는 READY 를 알 방법이 없다 — 그게 규칙 그 자체다.

</details>

<details>
<summary>힌트 2 — 소스 상태는 valid 와 wait 두 개면 된다</summary>

`valid` 가 이미 1 이면 아무것도 하지 말고 큐 머리를 그대로 낸다. `valid` 가 0 이고 보낼 것이 있으면 `wait` 를 하나 줄이거나(남았으면) VALID 를 올린다. 엣지에서 `hs` 가 아니면 **return** — 이게 "유지" 다. `hs` 면 pop, `valid = false`, 다음 지연 뽑기.

</details>

<details>
<summary>힌트 3 — 같은 ID 순서는 "가장 오래된 것만 후보"</summary>

항목마다 도착 번호 `seq` 가 있다. 항목 i 가 후보가 되려면 준비됐고(`ready_at ≤ now`), 같은 ID 이면서 `seq` 가 더 작은 살아 있는 항목이 없어야 한다. 준비 안 된 오래된 항목도 막는다 — 그래서 같은 ID 의 빠른 응답이 느린 응답 뒤에서 기다린다 (Part A 파형의 R4).

</details>

<details>
<summary>힌트 4 — 체커는 "어느 트랜잭션의 응답인가" 를 스스로 정한다</summary>

응답에는 ID 밖에 없다. 그러니 체커는 스펙이 보장하는 대로 "그 ID 의 가장 오래된 미완료 요청" 에 붙인다. 이 귀속이 틀어졌다는 증거는 내용뿐이다 — R 은 데이터, B 는 응답 코드. 그래서 순서 위반 검출에는 기대 데이터를 아는 **스코어보드** 가 필요하다 (`pat_word`, `exp_resp`).

</details>

<details>
<summary>힌트 5 — 같은 사이클은 "이후" 가 아니다</summary>

`chk_cycle` 은 B/R 검사를 이번 사이클 AW/W/AR handshake 를 기록하기 **전** 에 부른다. 그래서 마지막 W 와 같은 사이클에 올라온 BVALID 는 "그 AW 의 W 버스트가 아직 안 끝남(`k >= wcur`)" 으로 자연스럽게 위반이 된다. 따로 사이클 번호를 비교할 필요가 없다.

</details>

## 5. 풀이

### Part A — handshake 의 비대칭과 다중 ID

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 채널의 VALID/READY — 합법 3가지와 위반 2가지 (칸 = 1 사이클, 초록 = handshake)</text>
  <rect x="202" y="48" width="46" height="132" class="f-ok" opacity=".45"/>
  <rect x="340" y="48" width="46" height="132" class="f-ok" opacity=".45"/>
  <rect x="432" y="48" width="46" height="132" class="f-ok" opacity=".45"/>
  <g fill="currentColor"><text x="10" y="76">VALID</text><text x="10" y="126">READY</text><text x="10" y="168">payload</text></g>
  <path d="M100 80 H110 V60 H248 V80 H340 V60 H386 V80 H432 V60 H478 V80 H524 V60 H570 V80 H616 V60 H708 V80 H720" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M100 130 H202 V110 H248 V130 H294 V110 H386 V130 H432 V110 H478 V130 H720" stroke="currentColor" fill="none" stroke-width="2"/>
  <g stroke="currentColor">
    <rect x="110" y="152" width="138" height="24" class="f-mut"/><rect x="340" y="152" width="46" height="24" class="f-mut"/>
    <rect x="432" y="152" width="46" height="24" class="f-mut"/><rect x="524" y="152" width="46" height="24" class="f-mut"/>
    <rect x="616" y="152" width="46" height="24" class="f-mut"/><rect x="662" y="152" width="46" height="24" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="179" y="169">D0 (유지)</text><text x="363" y="169">D1</text><text x="455" y="169">D2</text>
    <text x="547" y="169">D3</text><text x="639" y="169">D3</text><text x="685" y="169">D4</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle" opacity=".8">
    <text x="133" y="198">0</text><text x="179" y="198">1</text><text x="225" y="198">2</text><text x="271" y="198">3</text>
    <text x="317" y="198">4</text><text x="363" y="198">5</text><text x="409" y="198">6</text><text x="455" y="198">7</text>
    <text x="501" y="198">8</text><text x="547" y="198">9</text><text x="593" y="198">10</text><text x="639" y="198">11</text><text x="685" y="198">12</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="179" y="44">(a) VALID 먼저, 대기</text><text x="340" y="44">(b) READY 먼저</text><text x="455" y="44">(c) 동시</text>
    <text x="616" y="44">(d) 위반</text>
  </g>
  <text x="593" y="230" fill="currentColor" font-size="12" text-anchor="middle">VALID_DROP</text>
  <text x="685" y="230" fill="currentColor" font-size="12" text-anchor="middle">PAYLOAD_CHANGE</text>
  <path d="M593 216 V84" stroke="currentColor" stroke-dasharray="3 3"/>
  <path d="M685 216 V180" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="258" fill="currentColor" font-size="12">(a) 소스는 READY 가 언제 올지 모른 채 VALID 를 올리고 D0 를 붙잡는다.</text>
  <text x="10" y="280" fill="currentColor" font-size="12">(b) READY 는 VALID 없이 올렸다 내려도 된다. (c) 같은 사이클에 둘 다 1 이면 바로 전송.</text>
  <text x="10" y="302" fill="currentColor" font-size="12">(d) 9 에서 올린 VALID 는 READY 전에 못 내린다(10). 기다리는 중 D3 → D4 로 바꿔도 위반(12).</text>
</svg>
```

정답 소스는 이 그림을 그대로 옮긴 것이다:

```c
static void src_drive(src_t *s, chan_t *c) {          /* READY 는 인자에 없다 */
    if (!s->valid && s->n > 0) {
        if (s->wait > 0) s->wait--;                   /* 무작위 지연 — 이것도 합법 */
        else s->valid = true;
    }
    c->valid = s->valid;
    c->p = s->valid ? s->q[s->head] : (pay_t){ 0 };
}
static void src_edge(src_t *s, bool hs) {
    if (!hs) return;                                  /* handshake 전: 아무것도 안 바꾼다 = 유지 */
    s->head = (s->head + 1u) % QCAP; s->n--; s->valid = false; s->wait = src_draw(s);
}
```

**왜 비대칭인가.** 스펙은 받는 쪽에게 "READY 를 VALID 다음에 올려도 된다" 를 허락한다 (메모리가 주소를 봐야 공간이 있는지 아는 경우 등). 그러면 보내는 쪽까지 "VALID 를 READY 다음에" 를 허락하면 둘이 서로를 기다려 아무도 시작하지 못한다 (Part C). 그래서 한쪽(소스)을 묶었다. 또 VALID 를 올린 뒤 내리거나 바꾸는 것을 금지해야 받는 쪽이 "VALID 를 봤으니 다음 사이클에 READY" 같은 **레지스터 기반** 설계를 할 수 있다 — 그 사이에 소스가 마음을 바꾸면 받는 쪽이 본 것과 실제 전송된 것이 달라진다.

**INCR 주소.** `beat n = (addr & ~(2^size−1)) + n·2^size` (n ≥ 1), beat 0 은 `addr`. 비정렬 시작 `0x1002`, size 2 → `0x1002, 0x1004, 0x1008, …` (첫 beat 은 2 바이트만 의미가 있다 — byte lane 은 SOCX-B02). AXI4 의 INCR 은 1–256 beat(AxLEN 8비트), WRAP/FIXED 는 16 beat 이하다.

**4 KB 규칙** 은 `마지막 바이트 = 정렬된 시작 + (len+1)·2^size − 1` 이 첫 바이트와 같은 `addr >> 12` 에 있는지다. 테스트 표:

| addr | len | size | 바이트 | 결과 |
|---|---|---|---|---|
| 0x0FF0 | 3 | 2 | 0x0FF0–0x0FFF | OK |
| 0x0FF0 | 4 | 2 | 0x0FF0–0x1003 | **넘음** |
| 0x0000 | 255 | 4 | 0x0000–0x0FFF (정확히 4 KB) | OK |
| 0x0FFE | 0 | 2 | 정렬 0x0FFC 의 한 beat | OK |

이유: 인터커넥트는 **버스트 시작 주소 하나로** 목적지 슬레이브를 고른다. 슬레이브 주소 공간의 최소 단위를 4 KB 로 맞춰 두면, 4 KB 안에 머무는 버스트는 중간에 다른 슬레이브로 넘어갈 수 없다. 이 레슨의 서브도 "시작 주소가 매핑돼 있으면 버스트 전체 OKAY" 로 판정하는데, 그게 안전한 것은 4 KB 규칙 덕분이다 (매핑 경계 0x10000 은 페이지 경계). MMU 페이지(4 KB)와도 맞아서 DMA 버스트가 물리적으로 불연속인 페이지를 넘지 않는다.

**다중 ID 스케줄러.** `sub_pick` 은 "준비됐고, 같은 ID 의 더 오래된 항목이 없는" 것 중 무작위를 고른다. 다른 ID 끼리 순서가 바뀌는 것은 무작위 선택과 느린 영역(0xC000~ 은 지연 +6) 때문이다. `demo_axi.c wave` 의 실제 파형:

```text
셀: . 쉼 | r READY만 | w VALID인데 READY 없음(대기) | handshake: AW/AR/B = ID, W = d/L(WLAST), R = ID + d/L
txn: W0 id0 len1 @0x100 · W1 id1 len0 @0x200 · R2 id0 len3 @0xC000(느린 영역 +6) · R3 id2 len0 @0x9000 · R4 id0 len0 @0xA000

cycle   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
AW      r  w 0  1   .  .  .  r  r  .  r  r  r  .  r  r  r  r
W       w  d  w  L  .  w  w  w  L  r  r  r  .  .  r  r  .  r
B       r  .  r  r  . 0   .  r  r  r 1   r  r  r  r  r  .  r
AR      r  w 0   . 2   r 0   r  r  r  r  r  .  .  r  r  r  .
R       .  r  r  .  .  r  r  r 2L  .  r  r 0d 0d 0d  w 0L 0L

cycles=18  correct=1  violations=0
```

읽는 법:

1. **채널 독립**: W0 의 첫 beat(1)가 AW0(2)보다 먼저 들어간다. AXI4 는 이를 허용하고, 서브는 W 를 버퍼에 담아 둔다. W 의 순서는 AW 순서와 같다 (AXI4 에서 WID 가 없어졌기 때문에 쓰기 데이터 interleaving 도 없다).
2. **B 의존성**: W0 의 마지막 beat(3) 이후 5 에 B0, W1 의 마지막(8) 이후 10 에 B1.
3. **다른 ID 는 추월**: AR 순서는 R2(id0, 2) → R3(id2, 4) → R4(id0, 6). R3 이 8 에 먼저 끝난다 — R2 는 느린 영역이라 12 부터.
4. **같은 ID 는 대기**: R4(id0) 는 지연이 짧아 9 쯤 준비됐지만 같은 id0 의 R2 가 16 에 끝날 때까지 기다렸다가 17 에 나간다. 이게 "같은 ID = 순서 보장" 의 비용이다. 매니저가 서로 무관한 요청에 다른 ID 를 쓰는 이유.
5. 15 의 `w`: 매니저가 R 에 READY 를 안 줘서 서브가 `0d` 를 붙잡고 있다. AXI 에는 R 에도 백프레셔(backpressure)가 있다.

### Part B — 수동 체커: 보이는 것만으로 판정하기

체커의 상태는 "지금까지 본 handshake 기록" 뿐이다: 직전 사이클의 선 값(`prev[]`), 본 AW/AR 목록(완료 여부, 받은 beat 수), W 버스트 목록(`wb[k]` = k 번째 W 버스트의 beat 수·WLAST 위치·마지막 handshake 사이클).

| 규칙 | 언제 검사 | 판정 |
|---|---|---|
| VALID_DROP / PAYLOAD_CHANGE | 매 사이클, 모든 채널 | 직전이 `V=1,R=0` 이면 지금 `V=0` → DROP, `V≠` payload → CHANGE |
| WLAST | W handshake | `wcur < naw` 면 `last == (idx == len)`. AW 가 아직이면 AW 기록 때 `last_idx == len` 확인 |
| B_BEFORE_AW | 새 BVALID | 같은 BID 의 미완료 AW 가 없다 |
| B_BEFORE_WLAST | 새 BVALID | 그 AW(k 번째)의 W 버스트가 안 끝남 (`k >= wcur`) |
| RLAST | R handshake | 같은 RID 의 가장 오래된 미완료 AR 의 `idx == len` 과 비교 |
| SAME_ID_ORDER | R handshake / 새 BVALID | 내용이 그 요청 기대값과 다르고, 같은 ID 의 더 젊은 요청 기대값과는 맞다 |
| CROSS_4KB | AW/AR handshake | `axi_crosses_4k` |

설계에서 고민한 지점 네 가지:

1. **"새 BVALID" 에서 검사한다.** handshake 때 검사하면 매니저가 BREADY 를 늦게 줘서 그 사이 W 가 끝나 버리면 위반을 놓친다. 스펙 문장이 "BVALID 를 올리기 전에 기다려야 한다" 이므로 VALID 가 처음 올라온 사이클이 맞는 검사 시점이다. "새로" 의 정의: `valid && !(prev.valid && !prev.ready)`.
2. **W 가 AW 보다 먼저** 오면 WLAST 를 판정할 길이(len)가 없다. 그래서 W 버스트를 `wb[k]` 에 쌓아 두고, k 번째 AW 가 올 때 `chk_on_ax` 가 뒤늦게 판정한다 (테스트: WLAST 없는 3 beat 뒤에 len 1 AW → 그 사이클에 WLAST 위반).
3. **같은 ID 재정렬은 선만 보고는 안 보인다.** RID 가 같으니 "가장 오래된 요청의 응답" 으로 보일 뿐이다. 데이터 기대값이 있어야 "이건 다음 요청의 데이터다" 를 안다. B 채널은 더 심하다: BID 와 BRESP 뿐이라, 두 쓰기의 응답 코드가 같으면 바뀐 순서는 **어떤 관찰로도 구별되지 않는다** (그리고 매니저 입장에서도 차이가 없다). 응답 코드가 다를 때(한쪽 DECERR)만 잡힌다.
4. **귀속이 틀리면 다른 규칙이 연쇄로 터진다.** 같은 ID 순서가 바뀐 R beat 는 길이가 다른 버스트에 붙어서 RLAST 도 틀려 보인다. 그래서 `chk_r` 은 "다른 요청의 데이터" 로 판명되면 RLAST 판정을 건너뛴다 — 안 그러면 원인(순서)이 아니라 증상(RLAST)이 첫 위반으로 보고된다. 실제로 이 순서를 바꾸기 전에는 kill matrix 에서 SAME_ID_ORDER 가 44/50 만 정확히 보고됐다 (나머지 6개는 RLAST 가 먼저).

주입 결과 (`./so` 의 info 줄, 시드 3, 같은 트래픽):

```text
info: inject VALID_DROP     -> VALID_DROP     on AR at cycle 5 (total 1)
info: inject VALID_DROP     -> VALID_DROP     on W  at cycle 4 (total 1)
info: inject PAYLOAD_CHANGE -> PAYLOAD_CHANGE on W  at cycle 4 (total 2)
info: inject PAYLOAD_CHANGE -> PAYLOAD_CHANGE on AW at cycle 4 (total 2)
info: inject WLAST          -> WLAST          on W  at cycle 40 (total 3)
info: inject B_BEFORE_WLAST -> B_BEFORE_WLAST on B  at cycle 6 (total 18)
info: inject B_BEFORE_AW    -> B_BEFORE_AW    on B  at cycle 44 (total 3)
info: inject RLAST          -> RLAST          on R  at cycle 80 (total 43)
info: inject SAME_ID_ORDER  -> SAME_ID_ORDER  on R  at cycle 27 (total 102)
info: inject CROSS_4KB      -> CROSS_4KB      on AR at cycle 0 (total 1)
55/55 passed
```

`PAYLOAD_CHANGE total 2`: 주입은 한 사이클만 비트를 뒤집으므로 다음 사이클 원래 값으로 돌아갈 때 **또** 바뀐 것이 된다. `total 102` 같은 큰 숫자는 첫 위반 뒤 귀속이 계속 어긋난 결과 — 체커는 첫 위반 하나만 믿어야 한다. 실제 검증 환경도 첫 에러에서 멈추거나, 첫 에러 시각을 기준으로 디버그한다.

`demo_axi.c viol` 로 본 주입 순간 (B_BEFORE_WLAST, 시드 3):

```text
[B_EARLY 주입] seed 3 → 첫 위반 B_BEFORE_WLAST (B 채널, cycle 6), 위반 합계 18
cycle   0  1  2  3  4  5  6  7  8  9 10 11 12 13
AW      .  .  .  w  w 2  1   .  r  w  w 3  1   .
W       .  d  .  w  w  d  .  r  d  .  .  d  r  .
B       .  .  r  r  r  r 2   r 1   r  r  r  . 3 
AR     1   .  .  .  w 2   w 1  1   r 3   .  w 1 
R       r  r  .  .  .  r  r  .  r  .  .  r  .  r
```

AW(id2) 가 5 에 받아들여지자마자 6 에 B(id2) — 이 시드의 첫 쓰기는 W beat 가 1, 5, 8, … 에 들어가는 중이고 WLAST 는 아직이다.

**kill matrix** (`demo_axi.c kill`, 시드 1000–1199):

```text
깨끗한 회귀: 1000 시드 × 40 txn → 통과 1000/1000 (평균 626 사이클)

주입한 버그 (W 채널)   첫 위반이 그 규칙   위반 0 (놓침)   다른 규칙이 먼저
  VALID_DROP              200/200              0                0
  PAYLOAD_CHANGE          200/200              0                0
  WLAST                   200/200              0                0
  B_BEFORE_WLAST          200/200              0                0
  B_BEFORE_AW             200/200              0                0
  RLAST                   200/200              0                0
  SAME_ID_ORDER           200/200              0                0
  CROSS_4KB               200/200              0                0
```

"체커가 아무것도 안 잡았다" 는 "버그가 없다" 가 아니라 "체커가 고장났을 수도" 다. 버그를 일부러 넣어 체커가 잡는 것을 확인하는 것이 **체커의 검증** 이다 (SOCX-B03 의 고장 난 모니터 kill matrix 와 같은 발상). 주입 조건도 신경 써야 한다: B_BEFORE_AW 는 W 가 AW 보다 먼저 끝나야만 생길 수 있어서 AW 를 150 사이클 늦췄고(`aw_lag`), SAME_ID_ORDER 는 같은 ID 응답 둘이 동시에 준비돼야 해서 지연을 12 로 늘렸다. 처음 `aw_lag = 40` 일 때는 시드 50개 중 33개만 잡혔다 — 버그는 넣었는데 **트래픽이 그 버그를 드러낼 상황을 안 만든** 경우다. 커버리지(coverage)가 필요한 이유.

### Part C — outstanding 과 대역폭, 그리고 데드락

**Little 의 법칙.** 처리량 = 동시에 진행 중인 양 ÷ 지연. 이 모델에서 읽기 하나의 왕복: AR handshake(사이클 t) → 서브 지연 `lat` → 첫 R beat 은 `t + 1 + lat` → 마지막 beat `t + lat + beats` → 매니저는 그 다음 사이클에 다음 AR. 왕복 `RT = lat + beats + 1`. R 채널이 매 사이클 바쁘려면 한 왕복 동안 `k·beats ≥ RT` 개의 beat 이 날아다녀야 한다.

```c
static unsigned ot_needed(unsigned lat, unsigned beats) {
    unsigned rt = lat + beats + 1u;
    return (rt + beats - 1u) / beats;                 /* ceil(RT / beats) */
}
```

총 사이클: 부족할 때 k 개 묶음이 왕복마다 한 번 → `(n/k)·RT + (k−1)·beats`, 충분할 때 첫 데이터까지 `lat + 1` 뒤 매 사이클 1 beat → `lat + 1 + n·beats`. 실제는 큰 쪽. (k = RT/beats 일 때 두 식이 정확히 같아진다 — 경계에서 이어진다.) `demo_axi.c ot`:

```text
읽기 48 버스트 × 8 beat, 서브 지연 lat=20, 지연 0 · READY 항상 1 → 왕복 = 20 + 8 + 1 = 29 사이클
ot_needed(20, 8) = 4

  K   sim cycles   식(pred)   R beats/cycle
  1         1392       1392   0.276
  2          704        704   0.545
  3          480        480   0.800
  4          405        405   0.948
  6          405        405   0.948
  8          405        405   0.948

지연별 필요한 outstanding (beats=4, 예: 128비트 버스 64 B 버스트):
  lat:     0    10    50   100   200
  K  :     2     4    14    27    52
```

K = 4 부터는 R 채널이 쉬지 않고(0.948 = 384/405, 남은 것은 첫 지연 21 사이클), 더 늘려도 소용없다. 지연 0 이어도 2 가 필요한 것은 "RLAST 다음 사이클에야 다음 AR" 이라는 1 사이클 거품 때문이다. 실제 숫자로: AXI 클럭 1 GHz 에서 DDR 왕복이 100 ns 정도면 64 B 버스트로 27개 안팎이 필요하다 — DMA 엔진, GPU, NPU 의 load unit 이 수십 개의 outstanding 과 넓은 ID 를 갖는 이유. 반대로 Cortex-M 같은 작은 코어는 outstanding 이 적어 외부 메모리 지연이 그대로 드러난다 (구체적인 코어별 개수는 TRM 마다 다르니 확인 필요). 추가 연습 4 는 이것을 GB/s·ns 단위로 바꾼다.

**데드락.** 매니저가 "READY 가 오면 VALID 를 올리자" 라고 짜면(규칙 위반), 서브가 무엇이냐에 따라 결과가 갈린다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">의존 사이클 = 데드락: 틀린 매니저 × 합법이지만 게으른 서브</text>
  <rect x="30" y="50" width="200" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="44" y="74" fill="currentColor">매니저 ARVALID</text>
  <text x="44" y="94" fill="currentColor" font-size="12">"READY 를 보면 올린다" (위반)</text>
  <rect x="30" y="150" width="200" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="44" y="174" fill="currentColor">서브 ARREADY</text>
  <text x="44" y="194" fill="currentColor" font-size="12">"VALID 를 보면 올린다" (합법)</text>
  <path d="M200 106 C 260 120, 260 136, 200 150" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <path d="M207 144 L200 150 L203 141" stroke="currentColor" fill="none"/>
  <path d="M60 150 C 0 136, 0 120, 60 106" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <path d="M53 112 L60 106 L57 115" stroke="currentColor" fill="none"/>
  <text x="252" y="132" fill="currentColor" font-size="12">기다림</text>
  <text x="66" y="132" fill="currentColor" font-size="12">기다림</text>
  <g fill="currentColor" font-size="12">
    <text x="330" y="66">매니저 AR 소스 × 서브 ARREADY 정책 → 결과</text>
    <text x="330" y="96">polite(위반) × LAZY(합법)   → 300 사이클 handshake 0, 멈춤</text>
    <text x="330" y="120">polite(위반) × ALWAYS(합법) → 20 사이클, 정상</text>
    <text x="330" y="144">polite(위반) × RAND 50%     → 21 사이클, 정상</text>
    <text x="330" y="168">정상 × LAZY(합법)            → 20 사이클, 정상</text>
    <text x="330" y="204">체커 위반 수: 네 경우 모두 0</text>
  </g>
  <text x="10" y="236" fill="currentColor">스펙은 READY→VALID 대기를 허락하므로 VALID→READY 대기를 금지해야 사이클이 생길 수 없다.</text>
</svg>
```

실제 출력:

```text
매니저 AR 소스 × 서브 ARREADY 정책  →  결과
  hung=1 cycles=300  AR hs=0 R hs=0  violations=0   <- polite(틀림) x LAZY(합법)
  hung=0 cycles=20   AR hs=4 R hs=16 violations=0   <- polite(틀림) x ALWAYS(합법)
  hung=0 cycles=21   AR hs=4 R hs=16 violations=0   <- polite(틀림) x RAND 50%(합법)
  hung=0 cycles=20   AR hs=4 R hs=16 violations=0   <- 정상 x LAZY(합법)
```

세 가지를 읽어야 한다.

1. **버그는 상대를 잘 만나면 숨는다.** READY 를 미리 올려 두는 서브(흔한 SRAM 컨트롤러 스타일)와 붙이면 멀쩡하다. IP 단독 검증에서 통과하고, 통합 후 다른 슬레이브와 붙였을 때 멈춘다 — 실리콘 bring-up 에서 가장 비싼 종류의 버그.
2. **수동 체커는 이걸 못 본다.** 데드락 상태의 버스는 "VALID=0, READY=0" 이 계속되는 **조용한 버스** 다. 아무 규칙도 위반하지 않았다. 잡는 것은 테스트벤치의 watchdog("할 일이 남았는데 300 사이클 동안 handshake 0") 이고, 그것도 *증상* 이지 원인이 아니다.
3. **원인은 조합(combinational) 의존이다.** RTL 에서 `ARVALID = f(ARREADY)` 가 같은 사이클 조합 경로면, 상대가 `ARREADY = g(ARVALID)` 일 때 조합 루프가 된다. 사이클 시뮬레이션은 이것을 "직전 사이클을 본다" 로밖에 표현 못 해서 구조 자체는 검사하지 못한다. 구조 검사는 RTL lint / 정형 검증(formal) 의 몫이다 — 예: "ARVALID 가 ARREADY 에 의존하지 않는다" 를 증명 (RTLDV-B10/RTLDV-A04). 추가 연습 3 은 이 의존 관계를 그래프로 만들어 사이클을 찾는다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
# info: inject ... 10줄 + 55/55 passed   (exit 0, 약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san
# 55/55 passed   (ASan/UBSan 경고 없음)
cc -std=c11 -Wall -Wextra -Werror -O2 demo_axi.c -o demo && ./demo wave    # viol | ot | deadlock | kill | (없으면 전부)
```

`demo_axi.c` 는 `#define main solution_main` 후 `solution.c` 를 include 해서 **정답 모델 그대로** 파형과 표를 찍는다 (체크 없음). 파형은 텍스트다. Surfer/GTKWave 로 보고 싶으면 `g_sim.tap` 콜백에서 VCD 를 쓰면 되지만 이 레슨에는 넣지 않았다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `c->valid = s->n > 0 && ready_prev;` (READY 를 보고 VALID) | 게으른(합법) 슬레이브와 붙으면 데드락. 혼자 테스트할 땐 안 보인다 | VALID 는 큐와 자기 지연만 보고 결정 |
| 엣지에서 `if (!hs) s->valid = false;` (타임아웃 후 철회) | VALID_DROP. 받는 쪽은 이미 "다음 사이클 READY" 를 예약했을 수 있다 | `if (!hs) return;` |
| 대기 중에 큐 머리를 더 급한 항목으로 교체 | PAYLOAD_CHANGE. 우선순위는 VALID 를 올리기 **전에** 정한다 | 머리는 handshake 때만 pop |
| 서브가 AW 를 받자마자 B (쓰기 버퍼에 넣었으니 OK) | 마지막 W 전에 B → 매니저는 "쓰기 끝" 으로 믿고 다음 일을 한다. AXI4 는 AW·마지막 W 둘 다 기다리라고 한다 | B 는 마지막 W handshake 다음 사이클 이후 |
| 같은 ID 응답을 준비된 순서대로 | 매니저는 "그 ID 의 가장 오래된 요청" 으로 받아 엉뚱한 버퍼에 데이터를 쓴다 — 조용한 데이터 오염 | 같은 ID 에 더 오래된 항목이 있으면 후보 제외 |
| 체커가 B 를 handshake 때 검사 | BREADY 가 늦으면 그 사이 W 가 끝나 위반을 놓친다 | 새 BVALID 사이클에 검사 |
| 체커가 RLAST 로 버스트 끝을 판단 | RLAST 가 고장 난 DUT 를 검사하는데 그 RLAST 를 믿으면 귀속이 연쇄로 어긋난다 | ARLEN 으로 스스로 센다 |
| `crosses = (addr & 0xFFF) + len * nb > 0x1000` | len 은 beats−1. 그리고 비정렬 시작이면 정렬 주소 기준 | `last = 정렬(addr) + (len+1)·nb − 1`, 페이지 번호 비교 |

## 8. 면접 Q&A

<details>
<summary>Q1. VALID 가 READY 를 기다리면 왜 안 되나요? READY 는 VALID 를 기다려도 되나요?</summary>

스펙은 받는 쪽에 "READY 를 VALID 를 본 다음에 올려도 된다" 를 허락한다. 보내는 쪽까지 기다리면 둘 다 0 인 상태에서 영원히 멈춘다(Part C: 300 사이클 handshake 0). 한쪽을 금지해야 의존 사이클이 원천적으로 불가능해지고, 그 쪽이 VALID 다. RTL 로는 READY → VALID 조합 경로가 상대의 VALID → READY 조합 경로와 만나 **조합 루프** 가 되는 문제이기도 하다.

꼬리질문: "READY 를 기본으로 1 로 두는 게 좋나요?" → 지연은 1 사이클 줄지만, READY=1 이면 그 사이클에 들어오는 것을 반드시 받을 공간이 있어야 한다. 버퍼가 꽉 찰 수 있는 설계면 READY 를 VALID 다음에 올리는(또는 공간이 있을 때만 올리는) 편이 단순하다. 둘 다 합법이고, READY 는 VALID 없이 올렸다 내려도 된다.

</details>

<details>
<summary>Q2. AXI 는 어떻게 out-of-order 응답을 하나요? 같은 ID 를 쓰면 뭐가 달라지나요?</summary>

요청마다 ID(AWID/ARID)를 붙이고 응답에 같은 ID(BID/RID)가 돌아온다. **다른 ID 끼리는 순서 보장이 없고, 같은 ID 끼리는 낸 순서대로** 다. 그래서 순서가 필요 없는 요청은 다른 ID 로 내서 느린 슬레이브 뒤에 줄 서지 않게 한다 (파형의 R4 가 R2 뒤에서 기다린 것). 읽기 데이터는 서로 다른 ID 사이에 beat 단위로 섞일(interleave) 수도 있다. 쓰기 데이터는 AXI4 에서 WID 가 빠져 AW 순서 그대로다.

꼬리질문: "인터커넥트에서 ID 는 어떻게 되나요?" → 여러 매니저의 같은 ID 를 구분하려고 인터커넥트가 포트 번호 비트를 덧붙인다 → 슬레이브 쪽 ID 폭이 넓어진다 (SOCX-B03 그림). 응답이 돌아오면 그 비트로 라우팅하고 떼어 낸다. "매니저가 프로그램 순서가 필요하면?" → 응답을 재정렬 버퍼에 모은다 (SOCX-M02).

</details>

<details>
<summary>Q3. 4 KB 경계 규칙은 왜 있고, DMA 드라이버는 어떻게 지키나요?</summary>

인터커넥트는 버스트의 **시작 주소** 로 슬레이브를 고르고, 슬레이브 영역의 최소 단위가 4 KB 라서, 4 KB 안에 머무는 버스트는 중간에 다른 슬레이브로 넘어가지 않는다. MMU 페이지와도 맞아서 가상→물리 변환 후 불연속인 페이지를 한 버스트가 넘지 않는다. DMA 엔진/드라이버는 긴 전송을 "≤ 256 beat(AXI4 INCR), 4 KB 안" 으로 쪼갠다 — 추가 연습 1.

꼬리질문: "넘으면 실제로 무슨 일이?" → 스펙 위반이라 결과가 정의되지 않는다. 디코더가 첫 주소만 보고 한 슬레이브에 보내면 그 슬레이브가 자기 영역 밖 주소를 쓰거나 wrap 할 수 있다. 검증에서는 체커 에러, 실리콘에서는 엉뚱한 메모리 오염. AXI3 는 버스트 최대 16 beat 였다.

</details>

<details>
<summary>Q4. W 가 AW 보다 먼저 와도 되나요? B 는 언제 보낼 수 있나요?</summary>

된다 — 채널이 독립이고 매니저는 AWREADY 를 기다리지 않고 WVALID 를 올려야 한다. 슬레이브는 AW 를 볼 때까지 WREADY 를 안 줄 수도 있고(합법) 받아서 버퍼에 둘 수도 있다. B 는 AXI4 에서 AW handshake 와 마지막 W(WLAST) handshake 를 **둘 다** 본 뒤에만 올릴 수 있다. AXI3 문서는 W 쪽 의존만 명시했고 AXI4 에서 AW 의존이 추가됐다고 알려져 있다 (이 레슨의 `B_BEFORE_AW` 규칙). 판별 시점은 BVALID 가 처음 올라오는 사이클이다.

꼬리질문: "같은 사이클이면?" → handshake 는 그 사이클 끝(엣지)에 일어나므로 같은 사이클에 BVALID 를 올리는 건 "이후" 가 아니다. 체커는 이번 사이클 handshake 를 기록하기 전에 B 를 검사해서 자연스럽게 위반으로 잡는다.

</details>

<details>
<summary>Q5. 새 AXI 슬레이브 IP 를 받았습니다. 어떻게 검증하겠습니까?</summary>

① 수동 프로토콜 체커(이 레슨의 규칙들 + 리셋 중 VALID=0, X 값 금지 등)를 모든 포트에 붙인다. ② 스코어보드: 참조 메모리 모델로 읽은 데이터·응답 코드를 비교. ③ 제약 무작위(constrained random) 트래픽: 길이·크기·ID·주소(경계 근처 포함)·VALID 지연·READY 패턴을 시드로 흔든다. ④ 커버리지: outstanding 깊이, 같은 ID 연속, W-before-AW, 백프레셔 길이, 4 KB 바로 앞 버스트… 를 실제로 쳤는지. ⑤ **체커 자체 검증**: 버그 주입 kill matrix. ⑥ 상대 다양화: 게으른 READY, 항상 READY 등 합법 정책 여러 개와 붙여 본다 (Part C 처럼 버그가 상대에 따라 숨는다). 실리콘에서는 같은 체커를 에뮬레이터나 버스 트레이스(CoreSight 등)로 돌리기도 한다. Arm 은 AXI 프로토콜 assertion 세트를 제공하고, 상용 VIP 도 같은 역할을 한다 (이름·범위는 제품마다).

꼬리질문: "위반 0 이면 끝인가요?" → 아니다. 위반 0 은 체커가 안 잡았다는 뜻일 뿐이다. 주입한 버그를 다 잡는지, 커버리지가 닫혔는지를 같이 본다. 이 레슨에서도 `aw_lag` 가 작을 때 B_BEFORE_AW 주입이 50개 중 33개만 드러났다.

</details>

<details>
<summary>Q6. 메모리 지연이 100 ns 인데 대역폭을 다 쓰려면 outstanding 이 몇 개 필요하나요?</summary>

Little 의 법칙: 날아다녀야 할 바이트 = 대역폭 × 지연. 25.6 GB/s × 100 ns = 2560 B, 64 B 버스트면 40 개 (추가 연습 4). 사이클 모델에서는 왕복 = 지연 + beat 수 + 1 이고 `ceil(왕복 / beats)` 개 (lat 20, 8 beat → 4, 표). outstanding 이 부족하면 대역폭은 `k × 버스트 / 왕복` 으로 선형으로 떨어진다 (K=1: 0.276, K=2: 0.545 beat/cycle).

꼬리질문: "outstanding 을 늘리는 비용은?" → 매니저는 그만큼 응답 버퍼·추적 테이블(ID, 주소, 길이)이 필요하고, 같은 ID 를 쓰면 응답 순서 때문에 재정렬 버퍼가 커진다. 인터커넥트·슬레이브도 그만큼 받을 수 있어야 한다 (accept capability). 그래서 SoC 문서에는 포트마다 "최대 outstanding 읽기/쓰기 수" 가 적혀 있다.

</details>

<details>
<summary>Q7. 같은 ID 로 쓰기를 내고 바로 같은 주소를 읽으면 새 값이 보장되나요?</summary>

AXI 의 순서 규칙은 **같은 ID, 같은 방향(읽기끼리 / 쓰기끼리)** 에 대한 것이다. 읽기와 쓰기는 다른 채널이라 같은 ID 여도 서로 순서가 보장되지 않는다. 쓴 값을 확실히 읽으려면 B 응답을 받은 뒤 읽기를 내야 한다 (B 는 "이 쓰기가 다른 매니저에게도 보이는 지점" 에 도달했다는 뜻으로 쓰인다). 그래서 펌웨어에서 "MMIO 쓰기 후 같은 레지스터를 읽어 반영 확인" 이 흔하고, 코어 쪽에서는 DSB 같은 배리어가 응답을 기다리는 역할을 한다 (ARCH-A05, ARCH-M12).

꼬리질문: "B 를 받았으면 DRAM 에 써진 건가요?" → 아니다. 버퍼러블(bufferable) 쓰기는 중간 버퍼가 B 를 일찍 줄 수 있다 (AxCACHE 속성). 장치 레지스터처럼 끝점 응답이 필요하면 non-bufferable 로 낸다.

</details>

<details>
<summary>Q8. 길이(AxLEN)를 아는데 WLAST/RLAST 는 왜 있나요?</summary>

슬레이브·인터커넥트가 beat 를 세지 않고도 버스트 끝을 알 수 있게 한다 (W 는 AW 보다 먼저 올 수 있어서 W 만 보는 경로에는 길이 정보가 없다). 읽기에서 매니저는 RLAST 로 "이 ID 의 다음 응답부터는 다음 요청" 을 안다. 검증 관점에서는 중복 정보라 **교차 검사** 가 된다: 체커는 길이로 세고 LAST 와 비교한다. LAST 를 믿고 세지 않으면 고장 난 LAST 가 귀속을 연쇄로 망친다 (7. 자주 하는 실수).

</details>

## 9. 추가 연습

### 연습 1 — 긴 전송을 합법 버스트로 쪼개기

`split_bursts(addr, bytes, size, out, cap)` — 2^size 정렬, 길이는 2^size 배수. 각 버스트 ≤ 256 beat, 4 KB 를 넘지 않게. 버스트 수가 최소인지 검산하라.

<details>
<summary>답 보기</summary>

탐욕(greedy): 이번 beat 수 = min(남은 beat, 256, 페이지 끝까지 남은 beat). 페이지 안의 beat 수를 b 라 하면 그 페이지에 최소 ceil(b/256) 개가 필요하고, 탐욕은 정확히 그만큼 쓴다 — 하한과 같으니 최소. 검산기는 연속성·각 버스트 합법·총 바이트·하한 일치를 본다 (무작위 5000 케이스). 예: `0x0FF0` 에서 64 B (size 2) → `{0x0FF0, len 3}`, `{0x1000, len 11}`. 1 B beat 로 4 KB 면 256 제한 때문에 16개. 처음에 결과 배열을 64 칸으로 잡았다가 1 B beat 의 긴 전송에서 넘쳐 실패했다 — cap 을 넘는 경우를 테스트가 잡아 준 것.

</details>

### 연습 2 — 허용되는 응답 순서는 몇 가지?

낸 순서의 ID 열 `ids[]` 가 있을 때 응답 순서(순열) 중 "같은 ID 는 낸 순서대로" 인 것의 개수를 열거로 세고 식과 비교하라.

<details>
<summary>답 보기</summary>

`n! / Π(ID 별 개수)!` (다항계수). ID 가 모두 같으면 1, 모두 다르면 n!. `{0,1,0,2,1,0,3,2}` → 8!/(3!·2!·2!·1!) = 1680. Heap 알고리즘으로 8! = 40320 개를 다 돌려 같은 수가 나오는 것을 확인한다. 의미: ID 를 다양하게 쓸수록 슬레이브가 고를 수 있는 순서가 기하급수로 늘어난다 = 스케줄링 자유도. 검증 관점에서는 테스트가 그 순서들을 충분히 쳤는지가 커버리지 문제가 된다.

</details>

### 연습 3 — 의존 그래프로 데드락 찾기

신호 AWVALID, AWREADY, WVALID, WREADY 를 노드로, "a 를 올리려면 b 가 먼저" 를 간선으로 놓고 사이클을 찾아라. 매니저 "WVALID 는 AWREADY 뒤" (흔한 드라이버/브리지 버그) 를 서로 다른 합법 서브 두 가지와 붙여 보라.

<details>
<summary>답 보기</summary>

DFS 로 back edge 를 찾는다. 서브 "AWREADY ← AWVALID, WREADY ← WVALID" 와는 사이클이 없어서 동작한다 — 버그가 숨는다. 서브가 "AWREADY ← WVALID" (데이터를 봐야 주소를 받는 합법 설계)면 `AWREADY → WVALID → AWREADY` 사이클 → 데드락. 스펙이 서브에게 허용하는 간선(READY → VALID)만 전부 넣으면 사이클이 없다는 것도 확인한다: 모든 간선이 READY 에서 VALID 로만 가니 길이 2 이상의 경로가 없다. 사이클이 생기려면 VALID → READY 간선이 하나는 필요하고, 그건 매니저의 위반이다. 사이클 시뮬레이션보다 이렇게 **구조를 보는** 검사가 이 버그에 맞는 도구다.

</details>

### 연습 4 — Little 의 법칙을 실제 단위로

`ot_for_bw(MB/s, ns, burst_bytes)` = 필요한 outstanding 수 (정수 연산, 올림).

<details>
<summary>답 보기</summary>

비행 중 바이트 = MB/s × ns / 1000 (10⁶ B/s × 10⁻⁹ s = 10⁻³ B). 25.6 GB/s × 100 ns = 2560 B → 64 B 버스트 40 개. 1.6 GB/s 면 160 B → 3 개(2.5 올림). 51.2 GB/s × 120 ns → 96 개. 지연이 두 배면 outstanding 도 두 배 — 메모리 컨트롤러 앞 큐 깊이, NPU DMA 의 동시 요청 수를 이 식으로 잡는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
# 17/17 passed   (ASan/UBSan 빌드도 17/17, 경고 없음)
```

## 10. 핵심 정리

- 다섯 채널 AW/W/B/AR/R 은 각자 VALID/READY. 둘 다 1 인 엣지 = 전송 1번. W 는 AW 보다 먼저 와도 되고 순서는 AW 와 같다.
- **VALID 는 READY 를 기다리지 않고, 올리면 handshake 까지 유지 + payload 고정.** READY 는 VALID 를 기다려도, 미리 올려도, 내려도 된다. 이 비대칭이 의존 사이클(데드락)을 막는다.
- BVALID 는 AW 와 마지막 W handshake **이후** (AXI4), RVALID 는 AR 이후. 체커는 "새로 올라온 VALID" 사이클에 검사한다.
- 같은 ID = 낸 순서대로, 다른 ID = 자유. 재정렬은 ID 만으로는 안 보이고 스코어보드(데이터·응답 코드)가 있어야 보인다. B 는 응답 코드가 같으면 영원히 안 보인다.
- INCR beat 주소 = 정렬 시작 + n·2^size (첫 beat 만 원래 주소). 버스트는 4 KB 를 못 넘는다 — 시작 주소로 슬레이브를 고르기 때문.
- 처리량 = outstanding × 버스트 / 왕복. 최소 outstanding = ceil(왕복 / beats). 지연 100 ns 급이면 수십 개.
- 체커가 조용하다 ≠ 맞다. 버그 주입(kill matrix)과 커버리지로 체커와 트래픽을 같이 검증한다. 데드락은 체커가 아니라 watchdog·구조 검사(lint/formal)가 잡는다.

## 참고자료

- [AMBA AXI and ACE Protocol Specification (Arm IHI 0022)](https://developer.arm.com/documentation/ihi0022/latest) — 채널 handshake, 채널 간 의존성(Issue E 기준 A3.2–A3.3 부근, 판마다 절 번호 다름), 버스트 주소 식, 4 KB 규칙, ID 순서 규칙. 최신 판은 Manager/Subordinate 용어 사용
- Arm AMBA AXI 프로토콜 assertion / 프로토콜 체커 문서 — `..._STABLE` 류 규칙 이름 (버전별로 이름·범위가 다를 수 있음)
- [AMBA AHB Protocol Specification (Arm IHI 0033)](https://developer.arm.com/documentation/ihi0033/latest) — 비교용: HREADY 기반 단일 파이프라인
- John D. C. Little, "A Proof for the Queuing Formula L = λW", *Operations Research* 9(3), 1961
- 관련 레슨: ARCH-B12 (wait state·처리량), ARCH-M13 (AHB-Lite/APB 사이클 모델), SOCX-B03 (AXI 응답·EXOKAY·exclusive monitor, kill matrix), ARCH-M12 (DMA 캐시 일관성), ARCH-A05 (메모리 순서), 다음 단계: SOCX-B02 (버스트 주소·WSTRB·WRAP), SOCX-M02 (ID 재정렬 버퍼), RTLDV-B10 / RTLDV-A04 (valid/ready RTL + SVA)
