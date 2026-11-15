---
id: COMM-A08
time: 120분
updated: 2026-11-11
---
# COMM-A08 제로카피 DMA 드라이버 설계: 디스크립터 링과 캐시 일관성

## 학습 목표

- 이더넷 MAC 같은 DMA 엔진의 **TX/RX 디스크립터 링** 을 OWN 비트 하나로 주고받는 드라이버를 짠다: TX submit / reclaim, RX give / poll, FS·LS 로 여러 디스크립터에 걸친 프레임, 링 가득 참, TX 정지(suspended) → poll demand, RX 버퍼 없음(RBU) → 회복.
- 비일관(non-coherent) 캐시에서 드라이버의 **유지보수 지점 7 곳** 을 이름으로 말하고, 한 곳씩 뺐을 때 생기는 증상을 실제로 재현한다: (a) TX 버퍼 clean, (b) TX 디스크립터 clean, (b') reclaim 전 invalidate, (c') RX 버퍼를 넘기기 전 invalidate, (c) RX 완료 후 invalidate, RX 디스크립터 clean / invalidate.
- **쓰는 순서** 를 증명한다: 디스크립터 안에서는 OWN 이 마지막 store, 여러 디스크립터 프레임에서는 첫(FS) 디스크립터의 OWN 이 마지막. 어기면 엔진이 이전 바퀴의 buf/len 으로 프레임을 보내거나(중복 + 손실), 앱이 들고 있는 버퍼에 새 프레임을 쓴다.
- 제로카피를 malloc 없이 설계한다: LIFO 버퍼 풀, RX 버퍼를 그대로 TX 로 돌리는 에코, 풀 고갈 시 "버리고 같은 버퍼를 다시 건다", 버퍼 소유권 감사(audit)로 누수 0 을 확인한다.
- 랜덤 soak(교체·프리페치·DMA 끼어들기·느린 앱)로 정답은 증상 0, 유지보수를 하나 뺀 드라이버는 20 시드 모두에서 증상이 난다는 것을 숫자로 보이고, 디스크립터를 MPU non-cacheable 영역에 둘 때의 비용 차이를 센다.

## 1. 문제

"부하가 걸리면 가끔 패킷이 두 번 나가요", "RX 가 몇 시간 뒤 멈춰요", "받은 패킷의 앞 24 바이트만 이상해요" — Cortex-M7(STM32F7/H7, i.MX RT)의 이더넷, Cortex-A 의 비일관 DMA 마스터, SoC 검증의 DMA 테스트에서 나오는 버그다. 면접에서는 "디스크립터 링을 설계해 보세요. OWN 비트는 언제 쓰나요? clean 과 invalidate 는 정확히 어디에? 디스크립터는 캐시에 둡니까?" 로 시작해서 링 가득 참, 버퍼 재활용, 정렬까지 꼬리질문이 이어진다.

이 레슨은 앞 레슨 셋을 한 드라이버로 묶는다. ARCH-M12 는 **버퍼 하나** 의 clean/invalidate 순서와 정렬 규칙을 재현했고, COMM-M11 은 디스크립터 없는 circular DMA 의 위치 계산을, CAM-M11 은 버퍼 소유권(qbuf/dqbuf)의 상태 기계를 다뤘다. 여기서는 **링 전체** 다: 디스크립터 자체도 DMA 와 CPU 가 함께 쓰는 메모리라서, 버퍼와 똑같은 규칙이 디스크립터에도 걸린다. 그리고 소유권은 메모리 내용이 아니라 OWN 비트 하나로 넘어간다 — 그 비트가 **언제, 어떤 순서로** 엔진에 보이느냐가 설계의 전부다.

모델 (전부 이 레슨의 정의. Synopsys/STM32 ETH DMA 와 구조는 같지만 레지스터·비트 위치는 대표값 — 실제 칩은 RM 확인 권장):

| 요소 | 모델의 동작 |
|---|---|
| 캐시 | write-back · write-allocate, 32 B 라인, 16 세트 x 2 웨이 = 1 KB, LRU (ARCH-M12 모델 축약판, 쓰기 버퍼 없음) |
| 디스크립터 | 한 라인(32 B)에 하나: `+0 word0`(OWN 31, FS 29, LS 28, ES 15, 길이 13:0), `+4 버퍼 주소`, `+8 버퍼 길이`, 나머지 패딩 |
| TX 엔진 | RAM 에서 디스크립터를 읽고 OWN=1 이면 버퍼를 읽어 프레임을 모으고 LS 에서 선로로. 상태를 write-back(OWN=0). OWN=0 을 만나면 **정지**, poll demand(도어벨)로 재개. 프레임 중간에 정지하면 underflow → 프레임 버림 |
| RX 엔진 | FIFO(2 프레임)에서 꺼내 버퍼 크기만큼 디스크립터마다 쓰고, 첫 조각에 FS, 마지막에 LS + 전체 길이(+ CRC 오류면 ES). OWN=0 을 만나면 **RBU 정지**, 프레임은 FIFO 에 남음. FIFO 가 차면 새 프레임은 놓침(missed). 재개 = poll demand 또는 새 프레임 도착 |
| 버퍼 풀 | 40 개 x 128 B, 0x0800 부터, LIFO. 링: TX 8, RX 8 |
| 하드웨어 훅 | `hw_evict`(아무 때나 교체 → 더티면 write-back), `hw_prefetch`(추측성 채움), `g_adv`(CPU 가 디스크립터 워드를 store 한 **직후** DMA 가 끼어듦) |
| 배리어 | 쓰기 버퍼가 없으므로 DSB 의 효과 대신 **검사기**: 도어벨 순간에 "마지막 DSB 이후 디스크립터 store 나 유지보수가 있었나" 를 센다 (효과의 재현은 ARCH-M12) |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">TX 링 8 칸 — 자유 증가 카운터 head/tail, 엔진 커서 cur (2 개는 끝났고 아직 회수 전)</text>
  <rect x="20" y="40" width="80" height="44" class="f-ok" stroke="currentColor"/>
  <rect x="100" y="40" width="80" height="44" class="f-ok" stroke="currentColor"/>
  <rect x="180" y="40" width="80" height="44" class="f-acc" stroke="currentColor"/>
  <rect x="260" y="40" width="80" height="44" class="f-acc" stroke="currentColor"/>
  <rect x="340" y="40" width="80" height="44" class="f-acc" stroke="currentColor"/>
  <rect x="420" y="40" width="80" height="44" class="f-acc" stroke="currentColor"/>
  <rect x="500" y="40" width="80" height="44" class="f-mut" stroke="currentColor"/>
  <rect x="580" y="40" width="80" height="44" class="f-mut" stroke="currentColor"/>
  <text x="32" y="60" fill="currentColor">0 OWN=0</text><text x="32" y="77" fill="currentColor" font-size="12">끝남, 회수 전</text>
  <text x="112" y="60" fill="currentColor">1 OWN=0</text><text x="112" y="77" fill="currentColor" font-size="12">끝남, 회수 전</text>
  <text x="192" y="60" fill="currentColor">2 OWN=1</text><text x="192" y="77" fill="currentColor" font-size="12">DMA 소유</text>
  <text x="272" y="60" fill="currentColor">3 OWN=1</text>
  <text x="352" y="60" fill="currentColor">4 OWN=1</text>
  <text x="432" y="60" fill="currentColor">5 OWN=1</text>
  <text x="512" y="60" fill="currentColor">6 빈칸</text><text x="512" y="77" fill="currentColor" font-size="12">CPU 소유</text>
  <text x="592" y="60" fill="currentColor">7 빈칸</text>
  <path d="M60 100 L60 88" stroke="currentColor" stroke-width="2"/>
  <text x="30" y="115" fill="currentColor">tail (reclaim)</text>
  <path d="M220 100 L220 88" stroke="currentColor" stroke-width="2"/>
  <text x="190" y="115" fill="currentColor">엔진 cur</text>
  <path d="M540 100 L540 88" stroke="currentColor" stroke-width="2"/>
  <text x="505" y="115" fill="currentColor">head (submit)</text>
  <text x="20" y="138" fill="currentColor" font-size="12">예: 사용 중 = head − tail = 6 (uint32 뺄셈이라 2^32 랩에도 맞음), 슬롯 = 카운터 % 8</text>
  <text x="20" y="155" fill="currentColor" font-size="12">3 세그먼트 프레임은 6 + 3 &gt; 8 → EBUSY, 아무것도 안 씀</text>
  <text x="10" y="175" fill="currentColor" font-weight="bold">디스크립터 1 개 = 캐시 라인 1 개 (32 B)</text>
  <rect x="20" y="190" width="160" height="40" class="f-acc" stroke="currentColor"/>
  <rect x="180" y="190" width="160" height="40" class="f-ok" stroke="currentColor"/>
  <rect x="340" y="190" width="160" height="40" class="f-ok" stroke="currentColor"/>
  <rect x="500" y="190" width="220" height="40" class="f-mut" stroke="currentColor"/>
  <text x="30" y="207" fill="currentColor">+0 word0</text><text x="30" y="223" fill="currentColor" font-size="12">OWN FS LS ES 길이</text>
  <text x="190" y="207" fill="currentColor">+4 버퍼 주소</text><text x="190" y="223" fill="currentColor" font-size="12">CPU 가 씀</text>
  <text x="350" y="207" fill="currentColor">+8 버퍼 길이</text><text x="350" y="223" fill="currentColor" font-size="12">CPU 가 씀</text>
  <text x="510" y="207" fill="currentColor">+12..31 패딩</text><text x="510" y="223" fill="currentColor" font-size="12">옆 디스크립터와 라인 공유 금지</text>
  <text x="20" y="255" fill="currentColor" font-size="12">CPU 의 store 순서: +4 → +8 → +0(OWN=1) 마지막.  엔진의 write-back: +0 만 (OWN=0 + 상태)</text>
  <text x="20" y="275" fill="currentColor" font-size="12">OWN 31 · FS 29 · LS 28 = STM32H7 TDES3 와 비슷하게 고른 모델 정의 (RM 확인 권장)</text>
  <text x="20" y="300" fill="currentColor" font-size="12">CPU 는 OWN=0 인 디스크립터만 고치고, 엔진은 OWN=1 인 디스크립터만 만진다 — 주인이 한 명인 동안만 쓰기</text>
  <text x="20" y="320" fill="currentColor" font-size="12">그런데 "엔진이 보는 OWN" 은 RAM 의 값이고, "CPU 가 보는 OWN" 은 캐시의 값이다 → 이 차이가 이 레슨의 버그 전부</text>
</svg>
```

### Part A — OWN 핸드오프: TX/RX 경로

`desc_write`(OWN 이 마지막 store), `tx_give_index`(여러 디스크립터 프레임은 뒤에서부터), `drv_tx_submit`(인자·링 공간 검사 → 버퍼 clean → 디스크립터 → 디스크립터 clean → 배리어 → 도어벨), `drv_tx_reclaim`(invalidate → OWN 확인 → 버퍼 반납, 순서대로만), `rx_give`(버퍼 invalidate → 디스크립터 → clean), `drv_rx_init`, `drv_rx_poll`(FS..LS 가 **모두** CPU 소유일 때만 소비, 버퍼는 위로, 슬롯엔 새 버퍼). 테스트: 단일·3 세그먼트 TX, 300 B RX 가 디스크립터 3 개에 걸침, 반쯤 쓴 프레임은 아무것도 안 먹음, 링 가득 참이 상태를 바꾸지 않음, 앱이 늦을 때 RBU → 회복.

### Part B — 유지보수 한 줄씩 빼기, 순서 어기기, 배치 규칙

결함 주입 스위치 `g_omit` 로 유지보수 지점과 순서 규칙을 하나씩 끄고 **정해진 증상** 이 나는지, 같은 시나리오에서 정답은 멀쩡한지 확인한다. `pool_layout_check`(버퍼 풀이 라인 정렬·라인 배수 간격인가)와 `rx_recycle`(오류 프레임·풀 고갈 → 같은 버퍼를 같은 슬롯에 다시 건다)도 여기서 짠다.

### Part C — 제로카피 에코 soak, 소유권 감사, 디스크립터 NC 비교

`drv_audit`(모든 버퍼가 풀 / TX 링 / RX 링 / 앱 중 **정확히 한 곳** 에 있나)와 `app_echo`(받은 버퍼의 헤더를 제자리에서 고쳐 그대로 TX). 4000 틱 soak: 프레임 30% 확률 도착(16–384 B, 3% CRC 오류), 20% 교체 / 20% 프리페치 훅, 디스크립터 store 의 1/4 에서 DMA 끼어들기, 앱이 가끔 바쁨(RBU·놓침 유발), 풀 버퍼를 임시로 써서 더티로 반납, 최대 30 개 쌓아 두기(풀 고갈). 모든 프레임이 "전달 + 오류 + 풀 부족 + 놓침" 으로 정확히 계산되는지, 에코가 바이트 단위로 맞는지, 끝나면 누수 0 인지 본다.

## 2. Problem (English)

> You are given a deterministic model of a non-coherent system: RAM, a write-back/write-allocate D-cache with 32-byte lines, and an Ethernet-MAC-like DMA engine that reads and writes **RAM only**. Each descriptor occupies one cache line: `word0` (OWN bit31, FS bit29, LS bit28, ES bit15, frame length bits13:0), buffer address at +4, buffer length at +8. The TX engine walks its 8-entry ring while OWN=1, assembles FS..LS segments into one frame, writes back word0 with OWN cleared, and **suspends** on OWN=0 until a poll demand. The RX engine writes a frame across as many 128-byte buffers as needed (FS on the first, LS + total length on the last) and suspends with "receive buffer unavailable" on OWN=0. Cache maintenance: `cache_clean(addr,len)`, `cache_inval(addr,len)`; barrier: `dsb()`.
>
> 1. `desc_write(d, w0, buf, len)` — the engine must never observe OWN=1 together with stale buffer fields, even if the line is evicted after any single store.
> 2. `int drv_tx_submit(const seg_t *seg, unsigned nseg)` — 1..3 segments from a buffer pool; returns `DRV_EINVAL`, `DRV_EBUSY` (ring has no room for **all** segments; nothing modified) or `DRV_OK`. A partially published multi-segment frame must never start.
> 3. `unsigned drv_tx_reclaim(void)` — return completed descriptors' buffers to the pool, in order.
> 4. `int drv_rx_init(void)`, `int drv_rx_poll(pkt_t *out)` — zero-copy: hand filled buffers up and refill the slot with a fresh pool buffer; consume nothing until FS..LS are all CPU-owned; drop error frames and frames for which no replacement buffers exist by re-arming the same buffers.
> 5. `int pool_layout_check(base, stride, size)`, `unsigned drv_audit(app_bufs, n)`, `int app_echo(const pkt_t *p)`.
>
> Justify every clean/invalidate in a comment. Example: a 300-byte RX frame produces descriptors `FS|len0`, `0`, `LS|300` (OWN cleared) and `drv_rx_poll` returns one packet with segments of 128, 128 and 44 bytes. Follow-ups: what changes if descriptors live in an MPU non-cacheable region? Why does the first descriptor's OWN go last? How do you detect a buffer leak in the field? What is the Linux DMA API equivalent of each maintenance point?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter       # exit 1
./starter -v                                                                 # 시나리오별 하드웨어 추적
```

처음 실행 결과 (실제 출력 일부):

```text
FAIL starter.c:470  cpu_rd32(d) == (D_OWN | D_FS | D_LS) && cpu_rd32(d + 4) == 0x1234u && cpu_rd32(d + 8) == 77u
FAIL starter.c:478  g_nsnap == 3u && torn == 0u && (g_snap[2][0] & D_OWN)
FAIL starter.c:480  tx_give_index(0, 1) == 0u && tx_give_index(0, 3) == 2u && tx_give_index(1, 3) == 1u && tx_give_index(2, 3) == 0u
…
soak seed 1         : injected 1217 delivered 0 (multi-desc 0) echoed 0, drops: missed 1215 err 0 nobuf 0 echo-busy 0, RBU 2 -> symptoms 10
…
17/100 passed
```

TODO 는 A1–A7, B1–B2, C1–C2. 하드웨어 모델·버퍼 풀·테스트 하네스는 고치지 않는다. 통과하는 17 개는 대부분 "결함을 넣으면 깨진다" 쪽 검사가 시작 코드에서도 우연히 맞는 것이다 (드라이버가 아무것도 안 하니 당연히 깨져 있다). 결함 주입 분기(`F_OWN_FIRST`, `F_FWD_ORDER`)는 주어져 있고, **올바른 쪽** 을 채우는 것이 과제다. 유지보수는 반드시 `MAINT(id, ...)` / `desc_clean(d, id)` / `desc_inval(d, id)` 로 불러야 Part B 의 결함 주입이 그 줄을 끌 수 있다.

## 4. 힌트

<details><summary>힌트 1 — 누가 무엇을 보나</summary>

엔진은 `ram_rd32` 로 RAM 만 본다. CPU 가 `cpu_wr32` 로 쓴 값은 캐시 라인에 더티로 있다가 clean(또는 우연한 교체) 때 RAM 에 간다. 반대로 엔진이 RAM 에 쓴 값은 CPU 의 캐시에 남은 옛 줄 때문에 안 보일 수 있다 → 읽기 전에 invalidate. 이 두 문장을 버퍼 2 종(TX, RX) x 디스크립터 2 종(TX, RX) x 방향 2 개에 적용하면 유지보수 지점이 나온다.
</details>

<details><summary>힌트 2 — OWN 은 왜 마지막인가</summary>

`g_adv` 훅은 store 직후 라인을 교체한다. 그 순간의 RAM 이 엔진이 보는 디스크립터다. word0(OWN=1)를 먼저 쓰면 그 순간 buf/len 은 이전 바퀴의 값이다. 여러 디스크립터 프레임이면 같은 논리가 디스크립터 사이에 적용된다: FS 디스크립터가 먼저 OWN=1 이 되면 엔진은 프레임을 시작했는데 다음 조각이 아직 CPU 소유다.
</details>

<details><summary>힌트 3 — 링 가득 참은 "전부 아니면 전무"</summary>

`tx_head - tx_tail + nseg > TX_RING` 이면 거절. 일부 디스크립터만 쓰고 실패를 반환하면, 엔진이 FS 만 있는 반쪽 프레임을 언젠가 만난다. 거절할 때는 버퍼 소유권도 호출자에게 그대로 남는다 (app_echo 가 풀에 돌려준다).
</details>

<details><summary>힌트 4 — RX poll 의 두 단계</summary>

(1) 훑기: rx_next 부터 디스크립터마다 invalidate 후 word0. OWN=1 을 하나라도 만나면 **아무것도 바꾸지 않고** 0. LS 를 만나면 멈춤. (2) 소비: 검사(FS 위치, 길이, ES, 새 버퍼 n 개 있나) → 통과하면 버퍼마다 invalidate 하고 위로, 슬롯엔 `rx_give(slot, pool_get())`. 실패하면 `rx_recycle(n)`. 둘 다 마지막에 배리어 + 도어벨(RBU 로 멈춘 엔진을 깨움).
</details>

<details><summary>힌트 5 — RX 버퍼에 invalidate 가 왜 두 번인가</summary>

넘기기 전: 앞 주인(앱의 임시 사용, TX 경로의 헤더 수정)이 남긴 **더티** 줄이 DMA 도중 교체되면 DMA 데이터를 덮는다. 완료 후: DMA 도중 프리페치가 **깨끗한 옛** 줄을 다시 채웠을 수 있다. 위험이 다르므로 둘 다 필요하고, Part B 의 시나리오 두 개가 각각 하나만 잡는다.
</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **소유권은 OWN 하나로, 주인은 항상 한 명.** CPU 는 OWN=0 인 디스크립터·버퍼만 만지고, 엔진은 OWN=1 만 만진다. 버퍼의 주인은 디스크립터를 따라간다.
2. **엔진은 RAM 을 본다.** 그래서 CPU→엔진 방향(쓴 것을 넘길 때)은 clean, 엔진→CPU 방향(엔진이 쓴 것을 읽을 때)은 invalidate, RX 버퍼는 넘기기 전에도 더티를 없앤다.
3. **OWN 은 마지막에, 배리어 다음에 도어벨.** 디스크립터 안에서도, 프레임의 디스크립터 사이에서도.
4. **버퍼는 라인 정렬 + 라인 배수, 디스크립터는 라인 하나씩(또는 NC 영역).** 라인을 같이 쓰면 어떤 유지보수 순서로도 못 막는다.
5. **누수는 감사로 잡는다.** 모든 버퍼가 정확히 한 곳에 있는지 세는 함수 하나가 테스트와 현장 진단을 다 한다.

### 유지보수 지점 7 곳 — 왜 필요하고, 빼면 무엇이 나나

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">TX 경로 (CPU → 엔진 → CPU)</text>
  <rect x="10" y="30" width="95" height="40" class="f-mut" stroke="currentColor"/><text x="16" y="48" fill="currentColor">앱이 버퍼에</text><text x="16" y="63" fill="currentColor">프레임 씀</text>
  <rect x="115" y="30" width="80" height="40" class="f-warn" stroke="currentColor"/><text x="121" y="48" fill="currentColor">(a) clean</text><text x="121" y="63" fill="currentColor">버퍼</text>
  <rect x="205" y="30" width="95" height="40" class="f-mut" stroke="currentColor"/><text x="211" y="48" fill="currentColor">buf, len,</text><text x="211" y="63" fill="currentColor">OWN 마지막</text>
  <rect x="310" y="30" width="80" height="40" class="f-warn" stroke="currentColor"/><text x="316" y="48" fill="currentColor">(b) clean</text><text x="316" y="63" fill="currentColor">디스크립터</text>
  <rect x="400" y="30" width="80" height="40" class="f-acc" stroke="currentColor"/><text x="406" y="48" fill="currentColor">DSB →</text><text x="406" y="63" fill="currentColor">도어벨</text>
  <rect x="490" y="30" width="80" height="40" class="f-ok" stroke="currentColor"/><text x="496" y="48" fill="currentColor">엔진: 읽고</text><text x="496" y="63" fill="currentColor">OWN=0 씀</text>
  <rect x="580" y="30" width="70" height="40" class="f-warn" stroke="currentColor"/><text x="586" y="48" fill="currentColor">(b') inval</text><text x="586" y="63" fill="currentColor">디스크립터</text>
  <rect x="660" y="30" width="70" height="40" class="f-mut" stroke="currentColor"/><text x="666" y="48" fill="currentColor">OWN=0 →</text><text x="666" y="63" fill="currentColor">버퍼 반납</text>
  <text x="10" y="92" fill="currentColor">빼면: (a) 선로에 RAM 의 옛 바이트 (96 B 중 95 B 다름)</text>
  <text x="10" y="108" fill="currentColor">(b) 엔진이 OWN=0 → 아무것도 안 나감, 나중에 우연한 교체가 "살려" 줌</text>
  <text x="10" y="124" fill="currentColor">(b') 캐시의 옛 줄이 OWN=1 → 회수 0, 링이 영영 참 (DMA 는 이미 끝냈는데)</text>
  <text x="10" y="148" fill="currentColor" font-weight="bold" font-size="13">RX 경로 (CPU → 엔진 → CPU, 버퍼는 위로)</text>
  <rect x="10" y="160" width="95" height="40" class="f-warn" stroke="currentColor"/><text x="16" y="178" fill="currentColor">(c') inval</text><text x="16" y="193" fill="currentColor">버퍼 (넘기기 전)</text>
  <rect x="115" y="160" width="80" height="40" class="f-mut" stroke="currentColor"/><text x="121" y="178" fill="currentColor">buf, len,</text><text x="121" y="193" fill="currentColor">OWN 마지막</text>
  <rect x="205" y="160" width="95" height="40" class="f-warn" stroke="currentColor"/><text x="211" y="178" fill="currentColor">(bR) clean</text><text x="211" y="193" fill="currentColor">디스크립터</text>
  <rect x="310" y="160" width="80" height="40" class="f-acc" stroke="currentColor"/><text x="316" y="178" fill="currentColor">DSB →</text><text x="316" y="193" fill="currentColor">도어벨</text>
  <rect x="400" y="160" width="80" height="40" class="f-ok" stroke="currentColor"/><text x="406" y="178" fill="currentColor">엔진: 데이터</text><text x="406" y="193" fill="currentColor">+ OWN=0</text>
  <rect x="490" y="160" width="80" height="40" class="f-warn" stroke="currentColor"/><text x="496" y="178" fill="currentColor">(c") inval</text><text x="496" y="193" fill="currentColor">디스크립터</text>
  <rect x="580" y="160" width="70" height="40" class="f-warn" stroke="currentColor"/><text x="586" y="178" fill="currentColor">(c) inval</text><text x="586" y="193" fill="currentColor">버퍼</text>
  <rect x="660" y="160" width="70" height="40" class="f-mut" stroke="currentColor"/><text x="666" y="178" fill="currentColor">앱이 읽음</text><text x="666" y="193" fill="currentColor">(제로카피)</text>
  <text x="10" y="222" fill="currentColor">빼면: (c') 앱이 남긴 더티 줄이 DMA 뒤 교체되며 RAM 을 덮음 — 완료 후 invalidate 로도 못 살림</text>
  <text x="10" y="238" fill="currentColor">(bR) 엔진은 OWN=0 → RBU, 그리고 poll 의 invalidate 가 드라이버 자신의 OWN=1 을 버림 → "유령 오류 프레임"</text>
  <text x="10" y="254" fill="currentColor">(c") 캐시의 옛 줄이 OWN=1 → 프레임이 와 있는데 poll 이 0 · (c) 프리페치된 옛 줄을 앱이 읽음</text>
  <text x="10" y="284" fill="currentColor">노란 상자 = 캐시 유지보수 (디스크립터가 NC 영역이면 (b) (b') (bR) (c") 는 사라지고 순서·배리어만 남는다)</text>
</svg>
```

| 지점 | 무엇을 | 왜 (solution.c 의 주석 요약) | Linux 스트리밍 DMA API 의 같은 자리 |
|---|---|---|---|
| (a) `M_TX_BUF_CLEAN` | TX 버퍼 clean | CPU 가 쓴 프레임이 캐시에만 더티, 엔진은 RAM 을 읽음 | `dma_map_single(.., DMA_TO_DEVICE)` |
| (b) `M_TX_DESC_CLEAN` | TX 디스크립터 clean | 방금 쓴 OWN=1 을 RAM 으로 | 디스크립터는 보통 `dma_alloc_coherent` 메모리 → 대신 `dma_wmb()` |
| (b') `M_TX_DESC_INVAL` | reclaim 전 invalidate | submit 의 clean 은 줄을 남김(OWN=1), 프리페치도 가능 | (coherent 메모리 → `dma_rmb()`) |
| (c') `M_RX_BUF_PREP` | RX 버퍼 invalidate (넘기기 전) | 앞 주인의 더티 줄이 DMA 도중 교체되면 DMA 데이터를 덮음 | `dma_map_single(.., DMA_FROM_DEVICE)` / `dma_sync_single_for_device` |
| (c) `M_RX_BUF_INVAL` | RX 버퍼 invalidate (완료 후) | DMA 도중 프리페치된 옛 줄 | `dma_unmap_single` / `dma_sync_single_for_cpu(.., DMA_FROM_DEVICE)` |
| (bR) `M_RX_DESC_CLEAN` | RX 디스크립터 clean | OWN=1 을 RAM 으로; 더티로 두면 나중의 교체가 엔진의 상태 write-back 을 덮음 | (`dma_wmb()`) |
| (c") `M_RX_DESC_INVAL` | poll 전 invalidate | 엔진이 쓴 OWN=0 을 보려면 | (`dma_rmb()`) |

(TX 버퍼는 완료 후 아무것도 안 한다 — DMA 는 읽기만 했다. arm64 Linux 의 `arch_sync_dma_for_cpu` 도 `DMA_TO_DEVICE` 면 바로 return 한다 — ARCH-M12 에서 소스로 확인. arm64 는 (c') 자리에서 invalidate 가 아니라 **clean** 을 한다; 이 레슨의 정렬된 풀에선 둘 다 정답이다 — 아래 변형 M5.)

### Part A — TX: submit 과 reclaim

```c
if (g_drv.tx_head - g_drv.tx_tail + nseg > TX_RING) { g_drv.tx_busy++; return DRV_EBUSY; }   /* 전부 아니면 전무 */
for (unsigned i = 0; i < nseg; i++)
    MAINT(M_TX_BUF_CLEAN, cache_clean(buf_addr(seg[i].buf), seg[i].len));
for (unsigned k = 0; k < nseg; k++) {
    unsigned i = tx_give_index(k, nseg);                  /* nseg-1, ..., 0 : FS 의 OWN 이 마지막 */
    ...
    desc_write(d, w0, buf_addr(seg[i].buf), seg[i].len);  /* +4, +8, 그다음 +0(OWN) */
    desc_clean(d, M_TX_DESC_CLEAN);
}
g_drv.tx_head += nseg;
barrier();              /* clean 완료 + 디스크립터 store 가 도어벨보다 먼저 */
dma_tx_poll_demand();
```

3 세그먼트 프레임(A-TX2, `./solution -v` 실제 출력):

```text
-- A-TX2: 세그먼트 3 개 프레임 (FS / - / LS)
    · DMA TX: 디스크립터 0 읽음 (RAM) OWN=1 FS buf=0x1b80 len=14
    · DMA TX: 디스크립터 1 읽음 (RAM) OWN=1 buf=0x1b00 len=128
    · DMA TX: 디스크립터 2 읽음 (RAM) OWN=1 LS buf=0x1a80 len=50
    · 선로 TX: 프레임 192 바이트 [02 00 00 00 00 ..]
    · DMA TX: 디스크립터 3 OWN=0 → 정지(suspended)
```

헤더(14 B)와 페이로드를 다른 버퍼에서 바로 보내는 것이 TX 제로카피(scatter-gather)다. 마지막 줄이 정상 동작이다: 엔진은 OWN=0 을 만나면 멈추고, 다음 submit 의 도어벨이 깨운다. reclaim 은 tail 부터 **순서대로만** 회수한다 — 링 중간이 먼저 끝났어도(그런 엔진은 없지만) tail 이 OWN=1 이면 멈춘다. 순서를 지키면 "회수 가능한 것 = tail 부터 연속한 OWN=0" 이라는 불변식 하나로 충분하다.

링 가득 참(A-TX4): 8 개를 넣고 엔진을 2 걸음만 돌린 뒤 reclaim → 사용 중 6. 3 세그먼트 submit 은 `EBUSY` 이고 head·풀 크기가 그대로인지 확인한다. 1 걸음 더 → 사용 중 5 → 5 + 3 = 8 이 딱 맞아 성공. 자유 증가 카운터라서 "가득(8)" 과 "빔(0)" 이 구분되고 칸을 하나 비워 둘 필요가 없다 (연습 2: 대신 링 크기는 2 의 거듭제곱이어야 한다).

### Part A — RX: give 와 poll

```text
-- A-RX2: 300 B 프레임이 디스크립터 3 개에 걸침
    · DMA RX: 디스크립터 0 → RAM 0x1b80..+128 씀
    · DMA RX: 디스크립터 1 → RAM 0x1b00..+128 씀
    · DMA RX: 디스크립터 2 → RAM 0x1a80..+44 씀
```

테스트는 엔진을 2 걸음만 돌린 상태(FS 와 가운데만 CPU 소유, LS 는 아직 DMA 소유)에서 poll 이 0 을 돌려주고 `rx_next` 가 그대로인지 확인한다. 훑기와 소비를 나누는 이유: 반쯤 온 프레임의 앞 조각을 먼저 위로 보내면, 그 슬롯을 재장전하는 순간 엔진이 아직 쓰는 프레임과 섞인다.

| 단계 (A-RX2) | 디스크립터 0 | 1 | 2 | poll 결과 |
|---|---|---|---|---|
| init 직후 | OWN | OWN | OWN | 0 |
| 엔진 2 걸음 | FS (OWN=0) | 0 (OWN=0) | OWN | **0** — 2 에서 OWN=1, 아무것도 안 먹음 |
| 엔진 1 걸음 더 | FS | 0 | LS + 300 | 1: 세그먼트 128 / 128 / 44, 슬롯 0–2 에 새 버퍼 |

길이 검사 `(n−1)·128 < len ≤ n·128` 은 엔진이 쓴 길이와 디스크립터 수가 맞는지 본다. 맞지 않거나 ES 면 `rx_recycle(n)` — 같은 버퍼를 같은 슬롯에 다시 건다(B2 테스트: CRC 오류 200 B 프레임 → `rx_err 1`, 버퍼 번호 그대로, RAM 의 OWN=1 이 8 개).

**RBU 와 회복 (A-RX4).** 앱이 poll 하지 않는 동안 프레임 12 개가 온다: 링 8 칸 + FIFO 2 개 = 10 개 보관, 2 개 놓침(`rx_missed == 2`). 실제 추적:

```text
    · DMA RX: 디스크립터 7 → RAM 0x1800..+100 씀
    · DMA RX: 디스크립터 0 OWN=0 → 버퍼 없음(RBU), 정지
    · DMA RX: 디스크립터 0 OWN=0 → 버퍼 없음(RBU), 정지
    · DMA RX: 디스크립터 0 → RAM 0x1780..+100 씀
    · DMA RX: 디스크립터 1 OWN=0 → 버퍼 없음(RBU), 정지
    · DMA RX: 디스크립터 1 → RAM 0x1b80..+100 씀
```

첫 poll 이 슬롯 0 에 새 버퍼(0x1780)를 걸고 도어벨 → 엔진이 FIFO 의 프레임을 쓴다. 다음 poll 은 슬롯 1 에, 앱이 방금 풀에 돌려준 버퍼(0x1b80, LIFO)를 건다. 이후 10 개가 seq 1..10 순서대로 나온다. 드라이버가 재장전마다 도어벨을 치는 이유가 이것이다: 엔진이 RBU 로 멈췄는지 따로 묻지 않아도 된다(실제 MAC 은 상태 레지스터에 RBU 비트가 있다 — STM32F4 ETH 의 RBUS 같은 것, RM 확인 권장).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">엔진 상태 기계 (이 모델)</text>
  <rect x="30" y="40" width="130" height="50" rx="8" class="f-ok" stroke="currentColor"/><text x="45" y="62" fill="currentColor">TX RUNNING</text><text x="45" y="80" fill="currentColor" font-size="12">OWN=1 이면 처리</text>
  <rect x="300" y="40" width="150" height="50" rx="8" class="f-mut" stroke="currentColor"/><text x="315" y="62" fill="currentColor">TX SUSPENDED</text><text x="315" y="80" fill="currentColor" font-size="12">도어벨만 기다림</text>
  <path d="M160 55 L298 55" stroke="currentColor" stroke-width="2" fill="none"/><text x="175" y="50" fill="currentColor" font-size="12">OWN=0 (프레임 사이)</text>
  <path d="M298 80 L160 80" stroke="currentColor" stroke-width="2" fill="none"/><text x="180" y="98" fill="currentColor" font-size="12">poll demand (도어벨)</text>
  <rect x="520" y="40" width="200" height="50" rx="8" class="f-warn" stroke="currentColor"/><text x="532" y="62" fill="currentColor">underflow: 프레임 버림</text><text x="532" y="80" fill="currentColor" font-size="12">FS 뒤 OWN=0 (e2 결함)</text>
  <path d="M450 65 L518 65" stroke="currentColor" stroke-width="2" fill="none"/>
  <rect x="30" y="140" width="130" height="50" rx="8" class="f-ok" stroke="currentColor"/><text x="45" y="162" fill="currentColor">RX RUNNING</text><text x="45" y="180" fill="currentColor" font-size="12">FIFO 의 프레임을 씀</text>
  <rect x="300" y="140" width="150" height="50" rx="8" class="f-mut" stroke="currentColor"/><text x="315" y="162" fill="currentColor">RX SUSPENDED</text><text x="315" y="180" fill="currentColor" font-size="12">RBU, 프레임은 FIFO</text>
  <path d="M160 155 L298 155" stroke="currentColor" stroke-width="2" fill="none"/><text x="175" y="150" fill="currentColor" font-size="12">OWN=0 (버퍼 없음)</text>
  <path d="M298 180 L160 180" stroke="currentColor" stroke-width="2" fill="none"/><text x="168" y="198" fill="currentColor" font-size="12">poll demand 또는 새 프레임 도착</text>
  <rect x="520" y="140" width="200" height="50" rx="8" class="f-warn" stroke="currentColor"/><text x="532" y="162" fill="currentColor">FIFO 가 차면 missed++</text><text x="532" y="180" fill="currentColor" font-size="12">(데이터는 조용히 사라짐)</text>
  <path d="M450 165 L518 165" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="10" y="222" fill="currentColor" font-size="12">재개 조건은 이 모델의 정의. 실제 Synopsys 계열은 RX 를 poll demand /</text>
  <text x="10" y="240" fill="currentColor" font-size="12">다음 프레임 도착에 다시 시도한다고 알려짐 (RM 확인 권장)</text>
</svg>
```

### Part B — 한 줄씩 빼기: 실제 추적

모든 추적은 `./solution -v` 의 실제 출력이다(결함 쪽 줄 뒤에 같은 시나리오의 정답 줄이 이어서 나온다).

**(a) TX 버퍼 clean 빠짐** — 앱이 쓴 96 B 는 캐시에만 있다:

```text
    · 선로 TX: 프레임 96 바이트 [7c 7c 7c 7c 7c ..]      ← 결함: RAM 의 옛 무늬 (96 B 중 95 B 다름)
    · 선로 TX: 프레임 96 바이트 [05 00 00 00 00 ..]      ← 정답: seq 5
```

**(b) TX 디스크립터 clean 빠짐** — "가끔 된다" 의 정체:

```text
    · DMA TX: 디스크립터 0 OWN=0 → 정지(suspended)          ← 도어벨을 쳤는데 RAM 엔 OWN=0
    · 교체(훅): 더티 라인 0x0000 → RAM write-back             ← 한참 뒤, 우연한 교체
    · DMA TX: 디스크립터 0 읽음 (RAM) OWN=1 FS LS buf=0x1b80 len=64
    · 선로 TX: 프레임 64 바이트 [06 00 00 00 00 ..]           ← 다음 도어벨에 늦게 나감
```

캐시 압력이 높은 시스템에서는 이 버그가 "지연이 가끔 튄다" 로만 보인다. 테스트는 첫 도어벨 직후 선로가 0 개, 교체 + 도어벨 후 1 개임을 확인한다.

**(b') reclaim 전 invalidate 빠짐** — 엔진은 끝냈다(선로에 1 개, RAM 의 OWN=0)는데 `drv_tx_reclaim()` 은 0 을 돌려주고 버퍼가 풀로 안 돌아온다. submit 의 clean 이 OWN=1 인 줄을 캐시에 남겼기 때문이다. 링이 8 번 차면 TX 가 영영 멈춘다 — "몇 시간 뒤 멈춤" 의 전형.

**(c) RX 완료 후 invalidate 빠짐 + 프리페치:**

```text
    · 프리페치(훅): 라인 0x1b80 를 지금의 RAM 값으로 채움     ← 넘긴 뒤, DMA 쓰기 전
    · 프리페치(훅): 라인 0x1ba0 를 지금의 RAM 값으로 채움
    · DMA RX: 디스크립터 0 → RAM 0x1b80..+64 씀              ← 앱은 캐시의 옛 줄을 읽음
```

**(c') RX 버퍼를 넘기기 전 invalidate 빠짐** — 앱이 풀 버퍼를 잠깐 쓰고(0x5A 128 B) 반납, LIFO 라서 그 버퍼가 슬롯 0 에 걸린다:

```text
    · DMA RX: 디스크립터 0 → RAM 0x1b80..+100 씀
    · 교체(훅): 더티 라인 0x1b80 → RAM write-back            ← 앱의 0x5A 가 DMA 데이터를 덮음
    · 교체(훅): 더티 라인 0x1ba0 → RAM write-back
    · 교체(훅): 더티 라인 0x1bc0 → RAM write-back
    · 교체(훅): 더티 라인 0x1be0 → RAM write-back
```

정답에선 같은 자리에 `invalidate 0x1b80 — 더티 데이터 버려짐!` 이 4 줄 나온다 — 여기서 "버려짐" 은 **원하는** 일이다(풀에 돌아온 버퍼의 쓰레기). 테스트 셋: prep 만 빼면 손상, 완료 후 invalidate 만 빼면 **이 시나리오에선** 무사(프리페치가 없어서), 정답 무사 — (c) 와 (c') 는 다른 위험이라는 뜻이다.

**(bR) RX 디스크립터 clean 빠짐** — 가장 이상하게 보이는 증상. 엔진은 RAM 의 OWN=0 을 보고 RBU 로 멈춘다(`DMA RX: 디스크립터 0 OWN=0 → 버퍼 없음(RBU), 정지`). 그런데 poll 은 (c") invalidate 로 드라이버 **자신이 쓴** OWN=1 을 버리고(`invalidate 0x0100 — 더티 데이터 버려짐!` x 8 줄씩 반복) RAM 의 init 값 0 을 읽어 "끝난 디스크립터, 그런데 FS 가 없음" → 오류 프레임으로 처리한다. 이 시나리오에서 실제로 `rx_err = 8`, 받은 프레임 0. 현장에서는 "트래픽이 없는데 RX 오류 카운터가 오른다" 로 보인다.

**(c") poll 전 RX 디스크립터 invalidate 빠짐** — 엔진은 프레임을 다 썼고 RAM 의 word0 은 `0x30000032`(FS | LS | 50, OWN=0)인데 poll 은 0. 캐시엔 rx_give 의 clean 이 남긴 OWN=1 줄이 있다.

**(e1) OWN 을 먼저 store — TX.** 링을 한 바퀴 돌린 뒤 프레임 A(seq 207)를 디스크립터 7 에 넣고 도어벨(엔진 달리는 중), 이어서 B(seq 208, 88 B)를 디스크립터 0 에 쓰는 순간 끼어들기:

```text
    · 교체(훅): 더티 라인 0x0000 → RAM write-back
    · DMA TX: 디스크립터 7 읽음 (RAM) OWN=1 FS LS buf=0x1b80 len=47
    · 선로 TX: 프레임 47 바이트 [cf 00 00 00 00 ..]                ← A
    · DMA TX: 디스크립터 0 읽음 (RAM) OWN=1 FS LS buf=0x1b80 len=40  ← OWN 은 새것, buf/len 은 지난 바퀴
    · 선로 TX: 프레임 40 바이트 [cf 00 00 00 00 ..]                ← A 의 앞 40 B 가 한 번 더
```

정답 순서에선 같은 끼어들기에서 `디스크립터 0 읽음 (RAM) OWN=1 FS LS buf=0x1b00 len=88` 이 나오고 B 가 정상 송신된다. 결함 쪽에서 B 는 **영영 안 나간다**: 엔진이 디스크립터 0 에 OWN=0 을 써 버렸고, 드라이버의 나머지 store 는 그 줄 위에 buf/len 만 고친다. reclaim 은 OWN=0 을 보고 B 의 버퍼를 "보냈음" 으로 반납한다 — 중복 하나 + 조용한 손실 하나. 테스트: 선로 2 개 중 정상 1 개.

**(e1) OWN 을 먼저 store — RX.** 링 8 칸이 다 찬 상태에서 poll 이 슬롯 0 의 프레임(버퍼 0x1b80)을 앱에 넘기고 새 버퍼를 거는 순간, 프레임 99 가 도착:

```text
    · 교체(훅): 더티 라인 0x0100 → RAM write-back
    · 끼어들기: 프레임 seq 99 도착
    · DMA RX: 디스크립터 0 → RAM 0x1b80..+60 씀     ← 앱이 들고 있는 버퍼에!
```

정답: `DMA RX: 디스크립터 0 OWN=0 → 버퍼 없음(RBU), 정지` 후 도어벨에 `RAM 0x1780..+60 씀`(새 버퍼). 제로카피 드라이버에서 OWN 순서를 어기면 **use-after-handoff** 가 된다.

**(e2) 2 세그먼트 프레임을 앞에서부터 넘김:**

```text
    · DMA TX: 디스크립터 1 읽음 (RAM) OWN=1 FS buf=0x1b00 len=100
    · DMA TX: 프레임 중간 디스크립터 2 가 OWN=0 → underflow, 프레임 버림
```

정답(뒤에서부터)은 디스크립터 2(LS)를 먼저 넘긴다 → 엔진은 디스크립터 1 에서 OWN=0 을 보고 프레임 **사이** 에서 정지 → 디스크립터 1 의 OWN 이 들어가고 도어벨 → 200 B 프레임 한 번에. Linux `stmmac`(Synopsys DWMAC 드라이버)의 `stmmac_xmit` 도 같은 순서다 — 2026-11-11 에 kernel.org 의 `drivers/net/ethernet/stmicro/stmmac/stmmac_main.c` 를 받아 확인한 주석: `/* Prepare the first descriptor without setting the OWN bit */` … `/* Set the OWN bit on the first descriptor now that all descriptors for this skb are populated. */` 그리고 `stmmac_set_tx_owner(priv, first_desc);`. 같은 파일의 다른 경로엔 `/* The own bit must be the latest setting done when prepare the descriptor and then barrier is needed to make sure that all is coherent before granting the DMA engine. */` 와 `wmb();`, XDP 경로엔 `dma_wmb(); stmmac_set_tx_owner(priv, desc);` 가 있다.

**배리어 검사기.** `F_NO_DSB` 면 도어벨 순간 `barrier_viol == 1`. 이 모델엔 쓰기 버퍼가 없어서 데이터는 멀쩡하다 — 효과의 재현(clean 이 쓰기 버퍼에 있는 동안 장치가 RAM 을 읽음)은 ARCH-M12 변형 표 1 행에서 했다.

### Part B — 배치 규칙: 120 B 버퍼를 붙여 놓으면

```svg
<svg viewBox="0 0 740 190" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">stride = size = 120 B 일 때 버퍼 30 과 31 (실제 주소, ./solution -v)</text>
  <rect x="40" y="40" width="160" height="40" class="f-mut" stroke="currentColor"/>
  <rect x="200" y="40" width="160" height="40" class="f-mut" stroke="currentColor"/>
  <rect x="360" y="40" width="160" height="40" class="f-warn" stroke="currentColor"/>
  <rect x="520" y="40" width="160" height="40" class="f-mut" stroke="currentColor"/>
  <text x="48" y="65" fill="currentColor">라인 0x1640</text><text x="208" y="65" fill="currentColor">라인 0x1660</text>
  <text x="368" y="65" fill="currentColor">라인 0x1680 (공유)</text><text x="528" y="65" fill="currentColor">라인 0x16a0</text>
  <rect x="40" y="95" width="360" height="30" class="f-ok" stroke="currentColor"/>
  <text x="48" y="115" fill="currentColor">버퍼 30 (RX 슬롯에 걸림) … 0x1687</text>
  <rect x="400" y="95" width="280" height="30" class="f-acc" stroke="currentColor"/>
  <text x="408" y="115" fill="currentColor">버퍼 31 0x1688 … (앱의 TX 프레임)</text>
  <text x="40" y="150" fill="currentColor" font-size="12">rx_give(버퍼 30) 의 invalidate 가 라인 0x1680 을 버림 → 버퍼 31 의 앞 24 B (앱이 막 쓴 더티 데이터) 소실</text>
  <text x="40" y="170" fill="currentColor" font-size="12">선로: [14 14 14 14 14 ..] = RAM 의 옛 무늬, 100 B 중 바이트 0–23 이 틀림. 128 B 간격이면 무사</text>
</svg>
```

실제 추적:

```text
-- B-d: 120 B 버퍼를 빈틈없이 붙인 풀 (정렬 규칙 위반)
    · DMA RX: 디스크립터 0 → RAM 0x1a48..+60 씀
    · invalidate 0x1680 — 더티 데이터 버려짐!
    · 버퍼 30 = 0x1610..0x1687, 버퍼 31 = 0x1688..
    · 선로 TX: 프레임 100 바이트 [14 14 14 14 14 ..]
```

`pool_layout_check(POOL_BASE, 120, 120)` 은 `LAYOUT_ESTRIDE` 를 돌려준다 — 부팅 때 한 번 부르면 이 버그는 생기지 않는다. 검사 순서는 정렬(EALIGN) → 간격(ESTRIDE) → 크기(ESIZE: 0 이거나 간격보다 큼) → 범위(ERANGE: 디스크립터·앱 영역과 겹치거나 RAM 밖). 버퍼 **안의 크기** 는 라인 배수가 아니어도 된다(`(base, 128, 100)` 은 OK) — 중요한 건 간격이다.

### 변형(mutation) — 테스트가 진짜 버그를 잡나

정답의 사본(스크래치 디렉터리)에 그럴듯한 버그를 하나씩 넣어 돌린 실제 결과:

| 변형 | 결과 | 처음 실패한 검사 |
|---|---|---|
| M1 poll: 뒤 디스크립터가 아직 DMA 소유여도 프레임 끝으로 취급 | 92/100 | A-RX2 "반쯤 온 프레임은 안 먹음" |
| M2 reclaim: OWN 을 안 보고 회수 | 87/100 | A-TX4 `drv_tx_reclaim() == 2u` |
| M3 submit: 링 공간 검사 `>=` (off-by-one) | 96/100 | A-TX4 8 개 넣기 |
| M4 app_echo: EBUSY 때 버퍼 반납 안 함 | 97/100 | C2 감사(누수) |
| M5 rx_give: prep 를 invalidate 대신 **clean** | 99/100 | B-d 만 — 아래 설명 |

M5 는 버그가 아니라 **다른 정답** 이다(Linux arm64 가 `arch_sync_dma_for_device` 에서 방향과 무관하게 clean). 정렬된 풀에선 모든 soak 가 통과하고, 실패한 B-d 는 "정렬 위반이 이 드라이버에서 손상을 낸다" 를 확인하는 테스트다: clean prep 은 공유 라인의 더티 데이터를 버리지 않고 RAM 에 써 주므로 **이 순간** 의 손상이 사라진다. 그래도 정렬 위반이 안전해지지는 않는다 — DMA 도중 CPU 가 옆 버퍼를 쓰면 완료 후 invalidate 가 그것을 버린다(ARCH-M12 Part B 에서 "어떤 명령을 골라도 둘 중 하나가 손상" 으로 재현).

### Part C — soak 와 실험 결과

정답 soak(`./solution` 실제 출력 한 줄):

```text
soak seed 1         : injected 1217 delivered 1168 (multi-desc 587) echoed 1168, drops: missed 25 err 24 nobuf 0 echo-busy 0, RBU 22 -> symptoms 0
```

1217 = 1168 + 25 + 24 + 0 — 모든 프레임이 계산된다. 테스트는 시드 3 개 x 4000 틱에서 증상 0 과 함께 "실제로 무언가 일어났나" 도 확인한다: 다중 디스크립터 프레임 > 100, RBU > 0, 놓침 > 0, 오류 프레임 > 0, 교체 훅 > 100, 프리페치 훅 > 100, 임시 사용(더티 반납) > 50. 증상 = rx 손상 + tx 손상 + 미계산 프레임 + 링 이상(끝났을 때 RAM 의 RX OWN 이 8 이 아니거나 TX 에 OWN 이 남음) + 누수 + 에코 불일치 + 배리어 위반 + underflow/badseq + DMA 주소 오류 + 풀 오류.

`demo_faults.c` — 같은 soak 를 결함마다 시드 1..20 으로 (실제 출력을 표로 옮김):

<!-- code: demo_faults.c -->

```bash
cc -std=c11 -O2 -Wall -Wextra demo_faults.c -o demo_faults && ./demo_faults    # 0.29 s user
```

| 빠진 것 | rx 손상 | tx 손상 | 미계산 | 링 이상 | underflow+badseq | 배리어 | RBU | 증상 난 시드 | 전달 / 주입 |
|---|---|---|---|---|---|---|---|---|---|
| (없음 = 정답) | 0 | 0 | 0 | 0 | 0 | 0 | 361 | **0/20** | 22813 / 24080 |
| (a) TX 버퍼 clean | 0 | 22457 | 0 | 0 | 0 | 0 | 361 | 20/20 | 22813 |
| (b) TX 디스크립터 clean | 0 | 0 | 0 | 168 | 17 | 0 | 321 | 20/20 | 22778 |
| (b') TX 디스크립터 inval | 0 | 0 | 0 | 20 | 0 | 0 | 343 | 20/20 | 22789 |
| (c') RX 버퍼 넘기기 전 | 486 | 480 | 0 | 0 | 0 | 0 | 361 | 20/20 | 22813 |
| (c) RX 버퍼 완료 후 | 878 | 149 | 0 | 0 | 0 | 0 | 361 | 20/20 | 22813 |
| (bR) RX 디스크립터 clean | 0 | 0 | 38 | 58 | 0 | 0 | 39 | 20/20 | **4** |
| (c") RX 디스크립터 inval | 0 | 0 | 124 | 160 | 0 | 0 | 296 | 20/20 | 470 |
| (e1) OWN 먼저 store | 40 | 980 | 0 | 0 | 0 | 0 | 329 | 20/20 | 22815 |
| (e2) 앞에서부터 넘김 | 0 | 0 | 0 | 0 | 678 | 0 | 361 | 20/20 | 22815 |
| 도어벨 앞 DSB | 0 | 0 | 0 | 0 | 0 | 46317 | 361 | 20/20 | 22813 |

(누수 열은 모든 행이 0 이라 뺐다. 정답의 RBU 361 은 "앱이 바쁨" 이 만든 정상적인 정지다.) 읽는 법:

- **데이터가 틀리는 결함** — (a), (c), (c'), (e1) — 은 rx/tx 손상으로 바로 보인다. (c) 와 (c') 가 tx 손상도 내는 이유: 손상된 RX 버퍼를 그대로 에코하기 때문(제로카피는 오류도 제로카피로 전파한다).
- **소유권이 꼬이는 결함** — (b), (b'), (bR), (c") — 은 데이터가 아니라 **흐름** 이 망가진다: 미계산 프레임, 끝났을 때의 링 상태, 전달 수의 급감((bR) 은 24080 중 4 개). 데이터 비교만 하는 테스트로는 못 잡고, 계산 등식과 링 상태 검사가 필요하다.
- **순서 결함** — (e2) — 은 underflow 카운터로만 보인다. 손상 프레임은 선로에 안 나가고(버려짐) 그냥 **빠진다**. 실제 MAC 의 TX underflow 상태 비트를 모니터링해야 하는 이유.

**비용 — 디스크립터를 캐시 가능 + 유지보수 vs MPU non-cacheable** (같은 데모, 에코 1000 프레임, 60 B 와 300 B 번갈아, 랜덤 훅 없음, 프레임당 평균):

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">프레임당 연산 수 (demo_faults.c 실측) — 위 막대: 캐시 가능 디스크립터, 아래: NC 디스크립터</text>
  <text x="10" y="50" fill="currentColor">CMO 디스크립터</text>
  <rect x="150" y="38" width="96" height="12" class="f-acc"/><text x="252" y="49" fill="currentColor">8.00</text>
  <rect x="150" y="52" width="1" height="12" class="f-ok"/><text x="156" y="63" fill="currentColor">0</text>
  <text x="10" y="88" fill="currentColor">CMO 버퍼</text>
  <rect x="150" y="76" width="264" height="12" class="f-acc"/><text x="420" y="87" fill="currentColor">22.00</text>
  <rect x="150" y="90" width="264" height="12" class="f-ok"/><text x="420" y="101" fill="currentColor">22.00</text>
  <text x="10" y="126" fill="currentColor">라인 채움</text>
  <rect x="150" y="114" width="60" height="12" class="f-acc"/><text x="216" y="125" fill="currentColor">5.01</text>
  <rect x="150" y="128" width="12" height="12" class="f-ok"/><text x="168" y="139" fill="currentColor">1.00</text>
  <text x="10" y="164" fill="currentColor">write-back</text>
  <rect x="150" y="152" width="60" height="12" class="f-acc"/><text x="216" y="163" fill="currentColor">5.00</text>
  <rect x="150" y="166" width="12" height="12" class="f-ok"/><text x="168" y="177" fill="currentColor">1.00</text>
  <text x="10" y="202" fill="currentColor">NC 워드 접근</text>
  <rect x="150" y="190" width="1" height="12" class="f-acc"/><text x="156" y="201" fill="currentColor">0</text>
  <rect x="150" y="204" width="192" height="12" class="f-ok"/><text x="348" y="215" fill="currentColor">16.00</text>
  <text x="10" y="240" fill="currentColor">DSB 는 둘 다 2.00 (RX 1 + TX 1). 막대 1 단위 = 12 px. 앱은 페이로드를 읽지 않음(헤더 1 바이트만 씀)</text>
</svg>
```

| | 디스크립터 캐시 가능 + CMO | 디스크립터 MPU non-cacheable |
|---|---|---|
| 정확성 부담 | 유지보수 4 지점 + OWN 순서 + 디스크립터당 라인 하나 | 유지보수 0, OWN 순서 + 배리어(연습 4: NC 쓰기도 재배열될 수 있다)만 |
| 디스크립터 접근 비용 (모델) | 프레임당 라인 연산 8 + 채움 4 + write-back 4 | 프레임당 단일 워드 버스 접근 16 |
| 메모리 | 디스크립터당 32 B (16 B 패딩) | 16 B 로 붙여도 됨 (연습 1 의 공유 문제가 없음) |
| 실무 | MPU 영역이 모자랄 때 | STM32H7 이더넷 예제에서 흔한 구성으로 알려짐, Linux 는 `dma_alloc_coherent` 로 같은 효과 |

버퍼는 반대다: CPU 가 페이로드를 여러 번 읽는다면 캐시 가능 + 유지보수가 싸다(ARCH-M12 비용 표: 64 B 두 번 읽기 = 라인 채움 2 vs NC 읽기 32). 그래서 흔한 구성이 "디스크립터는 NC, 버퍼는 캐시 가능 + clean/invalidate" 이고, 이 레슨의 Part B 중 버퍼 쪽 결함 (a)(c)(c') 은 그 구성에서도 그대로 남는다.

**제로카피의 대가.** 버퍼의 주인이 앱까지 올라가므로 (1) 앱이 늦으면 풀이 마른다 — soak 의 쌓아 두기 30 개가 그 상황이고 드라이버는 `rx_nobuf` 로 프레임을 버리고 같은 버퍼를 다시 걸어 RX 링을 절대 비우지 않는다, (2) 누수는 앱 코드에서 난다(M4) — 그래서 감사 함수가 드라이버에 있어야 한다, (3) 작은 프레임은 복사가 더 싸다 — 연습 3 의 copybreak. Linux 에서는 `page_pool` 이 RX 버퍼 재활용을, 드라이버별 `rx_copybreak` 파라미터가 3 번을 맡는 것으로 알려져 있다(세부 API 는 확인 권장).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 100/100 passed, 0.02 s user
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o so_san && ./so_san   # 100/100, 보고 없음
./solution -v                                                                 # 시나리오별 하드웨어 추적
```

단일 스레드 결정적 모델이라 TSan 은 해당 없다. 실행 시간은 다른 작성 작업과 같이 돈 상태의 값이라 참고만.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 디스크립터를 `{OWN, buf, len}` 순서로 채움 | 그 사이 교체·엔진 fetch 가 OWN=1 + 지난 바퀴 buf/len 을 봄 → 중복 + 손실 (e1) | `+4`, `+8` 먼저, `word0` 마지막 |
| 다중 디스크립터 프레임을 앞에서부터 넘김 | 엔진이 FS 를 잡고 다음이 CPU 소유 → underflow, 프레임 증발 (e2) | `tx_give_index` = 뒤에서부터, FS 의 OWN 이 마지막 |
| 링에 자리가 모자라면 들어가는 만큼만 넣음 | 반쪽 프레임이 링에 남음 | `head - tail + nseg > N` 이면 아무것도 안 하고 EBUSY |
| reclaim 이 캐시의 디스크립터를 그대로 읽음 | clean 은 줄을 남김 → OWN=1 영원히 (b') | 읽기 전 `desc_inval` |
| RX 재장전 후 디스크립터 clean 안 함 | 엔진 RBU + poll 의 invalidate 가 자기 OWN=1 을 버림 → 유령 오류 프레임 (bR) | `desc_clean` 후 배리어, 도어벨 |
| RX 버퍼는 "완료 후 invalidate" 만 | 재사용 버퍼의 더티 줄이 DMA 뒤 교체되며 RAM 을 덮음 (c') | 넘기기 전에도 invalidate (또는 clean) |
| 반쯤 온 RX 프레임의 앞 조각을 먼저 위로 | 슬롯 재장전과 엔진의 쓰기가 섞임 (M1) | FS..LS 가 모두 CPU 소유일 때만 소비 |
| 풀이 비면 슬롯을 빈 채로 둠 | RX 링이 줄어들다 0 이 되면 영구 RBU | 프레임을 버리고 같은 버퍼를 다시 건다 (`rx_recycle`) |
| 버퍼 크기 120 B 를 빈틈없이 배치 | 경계 라인 공유 → invalidate 가 옆 버퍼의 더티 데이터를 버림 (B-d, 24 B) | 간격을 라인 배수로, 부팅 때 `pool_layout_check` |
| 16 B 디스크립터를 두 개씩 한 라인에 (캐시 가능) | CPU 의 clean 이 옆 디스크립터의 DMA 상태를 덮음, 어떤 순서로도 못 막음 (연습 1) | 디스크립터를 라인 크기로 패딩하거나 NC 영역 |
| TX 링 가득 참에서 앱이 버퍼를 안 돌려줌 | 누수 → 풀 고갈 → RX 까지 멈춤 (M4) | 실패 경로에서 `pool_put`, 감사 함수로 검증 |

## 8. 면접 Q&A

<details><summary>Q1. 디스크립터 링 드라이버에서 OWN 비트를 정확히 언제 쓰나요?</summary>

CPU 소유(OWN=0)인 디스크립터의 버퍼 주소와 길이를 먼저 쓰고, OWN=1 을 **마지막 store** 로 쓴다. 여러 디스크립터 프레임이면 마지막 조각부터 넘기고 첫 조각(FS)의 OWN 을 맨 마지막에. 그다음 디스크립터가 캐시 가능 메모리면 clean, 배리어, 도어벨. 이 모델에서 OWN 을 먼저 쓰면 엔진이 지난 바퀴의 buf/len 으로 프레임을 하나 더 보내고 새 프레임은 사라졌고(TX), 앱이 들고 있는 버퍼에 새 프레임을 썼다(RX). 꼬리: "디스크립터가 NC 면 순서는 상관없나?" → 아니다. Normal NC 쓰기도 쓰기 버퍼에서 순서가 바뀔 수 있어 OWN 앞에 `dmb`(Linux `dma_wmb()`)가 필요하다(연습 4).
</details>

<details><summary>Q2. clean 과 invalidate 가 필요한 곳을 전부 말해 보세요.</summary>

TX 버퍼: 넘기기 전 clean, 완료 후 없음. TX 디스크립터: 쓴 뒤 clean, 회수 전 invalidate. RX 버퍼: 넘기기 전 invalidate(또는 clean), 완료 후 invalidate. RX 디스크립터: 쓴 뒤 clean, 읽기 전 invalidate. 일곱 곳 각각을 빼면 증상이 다르다(데모 표): 데이터 손상 (a)(c)(c'), 흐름 정지 (b)(b')(bR)(c"). 꼬리: "TX 버퍼를 완료 후 invalidate 하면 안 되나?" → 해도 데이터는 안 깨지지만(줄이 깨끗하므로) 쓸데없는 비용이고, 만약 그 사이 CPU 가 버퍼를 썼다면 그 쓰기를 버린다.
</details>

<details><summary>Q3. RX 버퍼는 왜 넘기기 전에도 처리하나요? 완료 후 invalidate 면 충분하지 않나요?</summary>

넘기기 전 버퍼에 더티 줄이 있으면(앱이 임시로 썼거나, 제로카피 에코에서 헤더를 고쳤거나, 부팅 때 memset) 그 줄은 DMA 도중이나 뒤 **언제든** 교체되며 RAM 을 덮는다. 완료 후 invalidate 는 이미 덮인 RAM 을 못 고친다. B-c' 시나리오에서 정확히 재현했다: prep 만 빼면 손상, 완료 후 invalidate 만 빼면(프리페치 없는 그 시나리오에선) 무사. 반대 위험(프리페치)은 B-c 가 잡는다. 둘은 다른 버그다.
</details>

<details><summary>Q4. 디스크립터를 캐시에 둡니까, non-cacheable 에 둡니까?</summary>

기본 답은 NC(MPU 영역, Linux 는 `dma_alloc_coherent`). 이유 셋: (1) 디스크립터는 작고 CPU 가 한두 워드만 읽고 쓰므로 캐시 이득이 없다 — 모델에서 캐시 가능이면 프레임당 라인 연산 8 + 채움 4 + write-back 4, NC 면 워드 접근 16, (2) 유지보수 4 지점이 사라져 버그 표면이 줄어든다, (3) 16 B 디스크립터를 붙여 둘 수 있다 — 캐시 가능이면 두 개가 한 라인을 공유해 CPU 의 clean 이 옆 디스크립터의 DMA 상태를 덮는다(연습 1, 어떤 유지보수 순서로도 못 막음). 캐시에 둘 때는 디스크립터를 라인 크기로 패딩한다(Synopsys 계열의 "descriptor skip length" 같은 설정이 이런 용도로 알려짐 — RM 확인 권장).
</details>

<details><summary>Q5. TX 링이 가득 찼을 때, RX 버퍼 풀이 비었을 때 드라이버는?</summary>

TX: "전부 아니면 전무" — 프레임 전체가 들어갈 자리가 없으면 아무것도 안 쓰고 EBUSY, 버퍼 소유권은 호출자에게(Linux 라면 `netif_stop_queue` 로 상위 계층을 멈추고 reclaim 에서 깨움). RX: 새 버퍼를 못 구하면 **그 프레임을 버리고 같은 버퍼를 같은 슬롯에 다시 건다**. 슬롯을 비워 두면 링이 줄어들다 영구 RBU 가 된다. 카운터(`rx_nobuf`)로 드러내고, 감사로 버퍼 수가 보존되는지 확인한다. 꼬리: "RX 중 앱이 버퍼를 오래 들고 있으면?" → 풀 크기 = RX 링 + TX 링 + 앱의 최대 보유량 + 여유. soak 에서 앱이 30 개를 쌓아 두는 경우를 넣어 둔 이유.
</details>

<details><summary>Q6. 부하에서 가끔 패킷이 두 번 나가고 하나가 빠집니다. 어디부터 보나요?</summary>

TX 디스크립터를 채우는 store 순서와 배리어. OWN 이 먼저 보이면 엔진이 지난 바퀴의 buf/len 을 읽는다 — 정확히 "이전 프레임의 일부가 한 번 더 + 새 프레임 손실" 이다(B-e1 추적). 다음은 다중 디스크립터 순서(underflow 카운터), 그다음 디스크립터 clean. 확인 방법: 엔진이 디스크립터를 fetch 하는 순간을 늘리는 스트레스(작은 프레임 연속), LA 나 MAC 통계의 underflow, 그리고 이 레슨처럼 "store 직후 교체 + 엔진 한 걸음" 을 주입하는 모델 테스트.
</details>

<details><summary>Q7. 버퍼 누수는 현장에서 어떻게 잡나요?</summary>

모든 버퍼는 정확히 한 곳(풀, TX 링 tail..head, RX 슬롯, 앱)에 있어야 한다. 그 개수를 세는 감사 함수를 드라이버에 두고, 디버그 빌드는 주기적으로, 제품은 진단 명령으로 부른다. 0 곳 = 누수, 2 곳 = 이중 소유(같은 버퍼가 RX 슬롯과 앱에 — use-after-handoff). 변형 M4(에코 실패 경로에서 반납 안 함)는 데이터 테스트를 다 통과하고 감사에서만 잡혔다. 보조로 풀 최저 수위(low watermark) 카운터.
</details>

<details><summary>Q8. Linux DMA API 로는 이 레슨의 각 지점이 무엇인가요?</summary>

버퍼(스트리밍 매핑): TX 넘기기 = `dma_map_single(dev, buf, len, DMA_TO_DEVICE)`, RX 넘기기 = `dma_map_single(.., DMA_FROM_DEVICE)` 또는 재사용 시 `dma_sync_single_for_device`, RX 완료 = `dma_unmap_single` 또는 `dma_sync_single_for_cpu`. arm64 에서 for_device 는 방향과 무관하게 clean, for_cpu 는 `DMA_TO_DEVICE` 가 아니면 invalidate(ARCH-M12 에서 소스 확인). 디스크립터: `dma_alloc_coherent` 로 잡고(비일관 arm64 에서는 NC 매핑으로 알려짐), 채운 뒤 OWN 앞에 `dma_wmb()`, OWN 을 읽은 뒤 나머지 필드 앞에 `dma_rmb()`. `memory-barriers.txt` 의 예시가 정확히 이 모양이다: "the dma_wmb() allows us to guarantee the data is written to the descriptor before the device can see it now has ownership" (2026-11-11 확인). 꼬리: "`dma_rmb` 는 왜?" → OWN=0 을 본 뒤 길이 필드를 읽는데, 그 읽기가 OWN 읽기보다 먼저 실행되면 엔진이 쓰기 전의 길이를 볼 수 있다.
</details>

<details><summary>Q9. 제로카피가 항상 이득인가요?</summary>

아니다. 큰 프레임은 복사를 없애 CPU·메모리 대역을 아끼지만, (1) 버퍼 주인이 앱까지 올라가 풀 고갈과 누수 위험이 생기고, (2) 작은 프레임은 복사 + 같은 버퍼 재장전이 풀 연산 두 번과 캐시 오염보다 싸다(연습 3 copybreak: 600 프레임 중 96 B 이하 296 개는 풀 연산 0), (3) 오류도 제로카피로 전파된다 — 데모에서 손상된 RX 버퍼가 그대로 에코돼 tx 손상으로도 나타났다. 꼬리: "TX 제로카피의 함정?" → 앱이 넘긴 버퍼를 reclaim 전에 다시 쓰면 선로 데이터가 바뀐다. 소유권 규칙은 TX 에도 똑같다.
</details>

## 9. 추가 연습

### 연습 1 — 16 B 디스크립터 두 개가 한 라인

디스크립터 0(PK)과 1(PK+16)이 한 라인. DMA 가 0 을 끝냄 → CPU 가 0 을 읽고 재장전하는 사이 DMA 가 1 의 상태(FS|LS|70)를 RAM 에 씀 → CPU 의 clean. 전략 4 개(그냥 / 고치기 직전 invalidate / 32 B 패딩 / NC 영역)에서 디스크립터 1 의 상태가 살아남는지 `packed_survives(strategy)` 로 확인하라.

<details><summary>답</summary>

0, 1 은 손실, 2, 3 은 무사. 1 이 핵심이다: invalidate 직후 CPU 의 첫 store 가 라인을 다시 채우는데 그 순간 디스크립터 1 은 아직 OWN=1 이고, DMA 가 그 뒤에 1 을 끝내면 CPU 의 clean 이 라인 **전체** 를 RAM 에 써서 OWN=1 로 되돌린다. CPU 의 유지보수 단위(라인)가 소유권 단위(디스크립터)보다 크면 순서로는 못 푼다 — 단위를 맞추거나(패딩) 캐시를 빼야(NC) 한다. ARCH-M12 Part B 의 "옆 변수" 문제와 같은 구조다.
</details>

### 연습 2 — 링 인덱스 수학

(A) 자유 증가 uint32 카운터: `used = head − tail`, `slot = counter % N`. (B) 0..N−1 인덱스 + 한 칸 비우기. 둘 다 전수 검사하고, 카운터가 2^32 에서 0 으로 돌 때 `slot` 이 이어지는 N 을 찾아라.

<details><summary>답</summary>

부호 없는 뺄셈은 모듈로 2^32 산술이라 랩 근처에서도 `head − tail` 이 정확하다(테스트: UINT32_MAX−20 부터 40 개 x 0..8). (B) 는 used = (h − t + N) % N, full = (h+1) % N == t 로 용량이 N−1. (A) 의 함정: `UINT32_MAX % 6 = 3` 인데 다음 카운터 0 의 슬롯은 0 → 4 를 건너뛴다(실제 출력). 2^32 ≡ 0 (mod N) 이어야 하므로 N 은 2^32 의 약수 = 2 의 거듭제곱. 그래서 링 크기는 2 의 거듭제곱으로 두고 `% N` 대신 `& (N−1)` 을 쓴다.
</details>

### 연습 3 — rx_copybreak

96 B 이하 단일 디스크립터 프레임은 앱 버퍼로 복사하고 DMA 버퍼를 같은 슬롯에 다시 건다(`rx_recycle(1)`), 큰 프레임은 제로카피. 600 프레임(16–96 B 와 97–384 B 반반)으로 내용·개수·감사를 확인하라.

<details><summary>답</summary>

`rx_poll_copybreak` 는 첫 디스크립터만 invalidate 해서 보고, FS|LS 이고 ES 가 아니며 길이 ≤ 96 이면 버퍼를 invalidate(복사도 CPU 읽기라 완료 후 invalidate 는 그대로 필요) → 복사 → `rx_recycle(1)`. 실제 출력: `600 프레임 중 296 개 복사(<= 96 B, 풀 연산 0), 304 개 제로카피`, 감사 0, 배리어 위반 0. 문턱값은 "복사 비용 < 풀 연산 + 캐시 미스 + 상위 계층의 버퍼 관리" 인 지점 — 실제 값은 측정으로 정한다.
</details>

### 연습 4 — NC 디스크립터와 쓰기 버퍼 재배열

디스크립터가 NC 영역이어도 Normal-NC 쓰기는 쓰기 버퍼에서 합쳐지거나 순서가 바뀌어 장치에 보일 수 있다(배리어 없을 때 — 아키텍처 세부는 Arm ARM 확인 권장). 4 칸 쓰기 버퍼가 **역순** 으로 하나씩 비워지는 모델에서, buf/len/OWN 을 쓰는 `nc_publish(use_dmb)` 가 "OWN=1 인데 내용은 옛것" 인 순간을 몇 번 만드는지 세라.

<details><summary>답</summary>

`dmb` 없이: OWN 이 가장 먼저 비워져 찢어진 순간이 생긴다(> 0). buf/len 뒤에 `dmb()`(지금까지의 쓰기를 순서대로 비움)를 넣으면 OWN 은 혼자 남아 0. Linux 의 `dma_wmb()` 가 이 자리이고 arm64 에선 `dmb oshst` 다(C-A02, ARCH-M12). 캐시 유지보수가 사라져도 순서 문제는 남는다 — NC 는 캐시를 없앨 뿐 배리어를 없애지 않는다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice      # 같은 디렉터리에서 (solution.c 를 include)
```

실제 출력:

```text
연습 2: UINT32_MAX % 6 = 3, 0 % 6 = 0 → 랩에서 슬롯이 3 → 0 으로 건너뜀
연습 3: 600 프레임 중 296 개 복사(<= 96 B, 풀 연산 0), 304 개 제로카피
12/12 passed
```

ASan/UBSan 빌드(`-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined`)도 12/12, 보고 없음.

## 10. 핵심 정리

- 엔진은 RAM 을, CPU 는 캐시를 본다. CPU→엔진은 clean, 엔진→CPU 는 invalidate, RX 버퍼는 넘기기 전에도 더티 0.
- 유지보수 7 지점: TX 버퍼 clean / TX 디스크립터 clean·invalidate / RX 버퍼 invalidate 두 번 / RX 디스크립터 clean·invalidate. 디스크립터가 NC 면 4 개가 사라지고 배리어만 남는다.
- OWN 은 마지막 store, 다중 디스크립터 프레임은 뒤에서부터(FS 의 OWN 이 마지막), 그다음 DSB, 그다음 도어벨.
- 링 공간은 "전부 아니면 전무", 회수는 tail 부터 순서대로, RX 는 FS..LS 가 모두 CPU 소유일 때만 소비.
- 풀 고갈·오류 프레임이면 버리고 같은 버퍼를 다시 건다 — RX 링을 절대 비우지 않는다.
- 버퍼는 라인 정렬 + 라인 배수 간격, 디스크립터는 라인 하나씩(또는 NC). 라인 공유는 어떤 순서로도 못 막는다.
- 데이터 비교만으로는 소유권 버그(흐름 정지)를 못 잡는다: 계산 등식(주입 = 전달 + 오류 + 부족 + 놓침), 링 상태, 버퍼 감사를 같이.

## 참고자료

- ARCH-M12 (이 레슨의 캐시 모델 원본, CMSIS `SCB_*DCache_by_Addr` 소스, Linux arm64/arm32 `arch_sync_dma_*`·`dcache_inval_poc` 소스 확인, MPU NC 비용 표), COMM-M11 (디스크립터 없는 circular DMA), CAM-M11 (버퍼 소유권 상태 기계, videobuf2), C-A02 (`dma_wmb`/`dma_rmb`), ARCH-M15 (MPU 영역), C-A05 (정렬 할당기)
- 다음 단계: BM-M08 (실제 MCU DMA 드라이버), NET-B01 (pcap 링크 하네스)
- Linux `Documentation/core-api/dma-api-howto.rst` — 스트리밍 vs coherent 매핑, `dma_wmb()`/`dma_rmb()` 의 디스크립터 예시, 캐시 라인 정렬 경고: https://docs.kernel.org/core-api/dma-api-howto.html
- Linux `Documentation/memory-barriers.txt` — `desc->status != DEVICE_OWN` → `dma_rmb()` → 데이터 읽기/쓰기 → `dma_wmb()` → `desc->status = DEVICE_OWN` → `writel(DESC_NOTIFY, doorbell)` 예시 (2026-11-11 확인): https://docs.kernel.org/core-api/wrappers/memory-barriers.html
- Linux `drivers/net/ethernet/stmicro/stmmac/stmmac_main.c` (Synopsys DWMAC 드라이버 — "첫 디스크립터의 OWN 을 마지막에" 주석과 `wmb()`/`dma_wmb()`, 2026-11-11 kernel.org master 에서 확인): https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/net/ethernet/stmicro/stmmac/stmmac_main.c
- ST RM0433 (STM32H7) Ethernet DMA 디스크립터 / RM0090 (STM32F4) ETH DMA — 디스크립터 비트, poll demand, RBU (RM 확인 권장)
- ST AN4839 "Level 1 cache on STM32F7 Series and STM32H7 Series" (캐시 유지보수, MPU 속성 — 확인 권장)
- Arm Cortex-M7 TRM (D-cache 32 B 라인), CMSIS-Core `cachel1_armv7.h`
