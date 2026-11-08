---
id: OS-A03
time: 110분
updated: 2026-11-08
---
# OS-A03 RCU 개념 구현: epoch 기반 메모리 회수

## 학습 목표

- 읽기 다수(read-mostly) 설정 테이블을 **포인터 교체(publish)** 로 갱신하고, 리더별 슬롯 `{active, local epoch}` + 전역 epoch 으로 **grace period(유예 기간)** 를 판단해 옛 버전을 해제하는 **EBR(epoch-based reclamation)** 을 직접 짠다. "retire epoch + 2 ≤ 전역 epoch" 에서 **왜 1 이 아니라 2 인지** 를 강제 스케줄로 보인다.
- "교체하자마자 `free`" 버전의 use-after-free 를 강제 스케줄로 재현하고 ASan 보고서를 읽는다. ASan 없이 돌리면 **체크섬까지 맞는 엉뚱한 버전** 을 읽는다는 것(가장 위험한 형태)을 본다.
- 읽기 진입이 **store→load 순서(SB 모양)** 라서 `seq_cst`(arm64 `stlr` + `ldar`)가 필요하다는 것을 기계어와 리트머스 실측으로 말한다 — acquire 면 `ldapr` 한 글자 차이로 UAF 창이 열린다.
- rwlock / mutex 대비 읽기 처리량(1~10 리더, 주기적 writer)과, **멈춘 리더 한 명이 붙잡는 메모리** (EBR 의 약점)를 실측 숫자로 설명하고, limbo 상한(역압)·막는 리더 찾기로 대응한다.
- 커널 RCU 용어(`rcu_read_lock`, `rcu_dereference`, `rcu_assign_pointer`, `synchronize_rcu`, `call_rcu`, quiescent state)와 liburcu 방식들을 이 레슨의 코드에 1:1 로 대응시키고, MMU 없는 RTOS 에서 **"모든 태스크가 루프 맨 위를 지났다" = QSBR** 로 보정 테이블을 교체한다.

## 1. 문제

"초당 수천만 번 읽히고 가끔 바뀌는 테이블을, 리더를 한 번도 막지 않고 바꾸려면?" — 커널의 라우팅 표·모듈 목록, 거래 시스템의 설정, 펌웨어 제어 루프의 **보정(calibration) 테이블** 이 전부 이 모양이고, 시스템/펌웨어 면접에서 "RCU 의 grace period 가 뭔가요?" 로 나온다. 4층의 앞 레슨들은 리더를 **막거나**(스핀락 OS-M07, futex OS-A01) **재시도시켰다**(seqlock LLAT-M02). RCU 는 리더가 아무것도 기다리지 않는 대신 **writer 가 옛 버전을 언제 버려도 되는지** 를 알아내야 한다. 이 레슨은 그 "언제" 를 epoch 으로 구현한다.

핵심 아이디어 세 줄:

1. 테이블은 **한 번 게시하면 불변(immutable)** 이다. 바꾸고 싶으면 복사본을 만들어 고치고(read-copy-update) 포인터 하나를 원자적으로 바꾼다. 리더는 포인터를 한 번 읽어 그 버전만 본다 → 찢어진 읽기(torn read)가 원리적으로 없다.
2. 옛 버전은 바로 못 지운다 — 교체 직전에 포인터를 읽어 간 리더가 아직 쓰고 있을 수 있다.
3. **grace period** = "교체 시점에 읽기 구간 안에 있던 리더가 **모두 한 번은 밖으로 나왔다**" 가 보장되는 시간. 그 뒤엔 옛 버전을 보는 리더가 있을 수 없으니 해제한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">EBR 구조: 리더 슬롯 · 전역 epoch · 현재 포인터 · limbo(해제 대기)</text>
  <rect x="10" y="40" width="170" height="30" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="18" y="60" fill="currentColor" font-family="monospace">R0 slot = (5&lt;&lt;1)|1</text>
  <rect x="10" y="78" width="170" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="18" y="98" fill="currentColor" font-family="monospace">R1 slot = 0 (밖)</text>
  <rect x="10" y="116" width="170" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="18" y="136" fill="currentColor" font-family="monospace">R2 slot = (4&lt;&lt;1)|1</text>
  <rect x="10" y="154" width="170" height="30" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="18" y="174" fill="currentColor" font-family="monospace">R3 slot = (5&lt;&lt;1)|1</text>
  <text x="10" y="204" fill="currentColor">슬롯마다 캐시 라인 1 개 (128 B 패딩)</text>
  <rect x="250" y="40" width="150" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="262" y="62" fill="currentColor" font-family="monospace">epoch G = 5</text>
  <rect x="250" y="110" width="150" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="262" y="132" fill="currentColor" font-family="monospace">cur ──────►</text>
  <rect x="470" y="104" width="250" height="46" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="480" y="124" fill="currentColor" font-family="monospace">cfg v9 { gain[32], sum }</text>
  <text x="480" y="142" fill="currentColor">LIVE — 새 리더는 이것만 본다</text>
  <path d="M400 127 L466 127" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M458 121 L468 127 L458 133" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="470" y="184" fill="currentColor" font-weight="bold">limbo (writer 만 만짐)</text>
  <rect x="470" y="194" width="120" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="478" y="214" fill="currentColor" font-family="monospace">v7 @ e=3</text>
  <rect x="600" y="194" width="120" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="608" y="214" fill="currentColor" font-family="monospace">v8 @ e=4</text>
  <text x="470" y="244" fill="currentColor">v7: 3+2 ≤ 5 → 해제 가능</text>
  <text x="470" y="262" fill="currentColor">v8: 4+2 &gt; 5 → R2 가 쥐고 있을 수 있음</text>
  <g fill="currentColor">
    <text x="10" y="292">전진 규칙: 활동 중 슬롯이 모두 epoch == G 일 때만 G → G+1.  지금은 R2(4 ≠ 5) 가 막는다.</text>
    <text x="10" y="314">리더: 슬롯에 (G&lt;&lt;1)|1 저장 → cur 읽기 → 사용 → 슬롯 0.  writer: 새 버전 → cur 교체 → 옛 것 limbo.</text>
  </g>
</svg>
```

| 방식 | 리더 비용 | 리더가 기다리나 | writer | 이 레슨 |
|---|---|---|---|---|
| rwlock | 공유 카운터 RMW 2 번 (캐시 라인 핑퐁) | writer 가 있으면 | 리더가 다 나갈 때까지 | 벤치 비교 대상 |
| seqlock (LLAT-M02) | load 2 번 + 재시도 | 아니오 (재시도) | 안 기다림 | 대조 |
| **EBR** | 내 슬롯 store 2 번 + load 1 번 | **아니오** | 복사 + 교체 + 나중에 해제 | Part A·B |
| **QSBR** | 루프 한 바퀴에 store 1 번 | **아니오** | 복사 + 교체 + 나중에 해제 | Part C |
| hazard pointer (LLAT-M03) | 포인터마다 게시 + 재확인 | 아니오 | 해제 전 스캔 | 언급만 (5층) |

### Part A — EBR 핵심: 진입/이탈, 전진, retire/reclaim, 교체

- `ebr_enter(d, r)` / `ebr_exit(d, r)` — 리더 `r` 의 슬롯 한 워드에 `(G << 1) | 1` / `0` 을 쓴다. 기다리지 않는다.
- `ebr_try_advance(d)` — 활동 중인 슬롯이 전부 현재 `G` 이면 `G` 를 1 올린다 (CAS).
- `ebr_retire(d, p)` — 옛 버전을 지금 `G` 와 함께 limbo 에 넣는다. `ebr_reclaim(d)` — `epoch + 2 ≤ G` 인 것을 해제.
- `cfg_update(db, version)` — 새 버전 할당·채우기 → `atomic_exchange` 로 교체 → 옛 것 retire → 전진 → 회수. limbo 가 꽉 차면 교체하지 않고 `-1`.
- 스트레스: 리더 6 + writer 1 (20 000 갱신) — 깨진 테이블 0, 버전 역행 0, 할당 수 = 해제 수 + 1. **ASan 빌드로 돌려 use-after-free 보고 없음** 이 이 과제의 "UAF 부재 검증" 이다.

### Part B — 어려운 부분: 1 칸으로는 부족, 멈춘 리더, 상한 있는 대기

- 강제 스케줄 (단일 스레드, 리더 핸들 2 개): 리더 0 이 v3 을 쥔 채 멈춤 → 갱신 → **epoch 은 1 칸 전진하지만 v3 은 해제되면 안 된다** → 두 번째 전진은 리더 0 이 막는다 → 리더 0 이 나가면 그제야 해제.
- `ebr_blocker(d)` — grace period 를 막는 리더 번호 (워치독·진단용). `ebr_synchronize(d, max_spins)` — `synchronize_rcu` 처럼 2 칸 전진을 기다리되 **상한** 이 있다 (읽기 구간 안에서 부르는 실수 → 교착 대신 `false`).
- 멈춘 리더 + 갱신 폭주: limbo 상한 `LIMBO_CAP` 에서 갱신이 거절되고 메모리가 거기서 멈춘다 (역압, backpressure).
- `demo_uaf.c`: "교체 직후 free" 를 같은 강제 스케줄로 돌려 ASan 보고서를 본다.

### Part C — 펌웨어: 제어 루프 + 정적 버퍼 풀 + QSBR

- MMU·`malloc` 없는 MCU. 보정 테이블 버퍼 **3 개** 정적 풀 (LIVE 1 + RETIRED 최대 2). 제어 태스크들은 읽기 구간 표시 없이 `qsbr_get()` 으로 읽고, **루프 맨 위에서** `qsbr_quiescent(t)` 로 "지난 바퀴에 쥔 것은 이제 안 쓴다" 를 알린다.
- `qsbr_publish(q, version)` — FREE 버퍼가 없으면 `-1` (늦은 태스크가 있음 → 다음 주기에 재시도). `qsbr_poll(q)` — 모든 태스크의 `seen ≥ retired_gp` 인 버퍼를 FREE 로.
- 스트레스: 태스크 4 + writer 1 (20 000 게시) — 재사용 중인 버퍼를 읽은 적 0, 역행 0.

## 2. Problem (English)

> **Read-mostly configuration with epoch-based reclamation.** Readers must never block or retry. The table (`cfg_t`: version, 32 gains, checksum) is immutable once published; an update allocates a new copy and swaps one pointer.
>
> A. Implement EBR: per-reader slot word (`0` = outside, `(epoch << 1) | 1` = inside since `epoch`), cache-line padded; `void ebr_enter(ebr_t*, int r)`, `void ebr_exit(ebr_t*, int r)`, `bool ebr_try_advance(ebr_t*)` (advance the global epoch only if every active reader is in the current epoch), `void ebr_retire(ebr_t*, void*)`, `int ebr_reclaim(ebr_t*)` (free everything retired at `e` with `e + 2 <= epoch`). Then `int cfg_update(cfgdb_t*, uint64_t version)` returning `-1` when the limbo list is full. Six readers and one writer doing 20 000 updates: zero corrupted tables, versions never go backwards per reader, allocations == frees + 1, clean under ASan and TSan.
>
> B. Show with a forced schedule why one epoch advance is not a grace period. Implement `int ebr_blocker(ebr_t*)` (which reader holds the grace period back) and `bool ebr_synchronize(ebr_t*, unsigned max_spins)` (wait for two advances, bounded). With one stalled reader, memory must stop growing at `LIMBO_CAP` versions.
>
> C. Firmware: no heap, a 3-buffer static pool, control tasks that report a quiescent state at the top of each loop. Implement `void qsbr_quiescent(qsbr_t*, int task)`, `int qsbr_poll(qsbr_t*)`, `int qsbr_publish(qsbr_t*, uint64_t version)` (returns `-1` if no buffer is free).
>
> Follow-ups: Why `+2`? Why must the reader's slot store and pointer load be `seq_cst` while QSBR needs only acquire/release? What happens if `synchronize` is called inside a read-side section? How much memory does a reader stalled for 200 ms pin? How does this map onto `rcu_read_lock` / `synchronize_rcu` / `call_rcu`, and onto a single-core Cortex-M where readers are ISRs?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, 0.09 초 만에 끝남 — 멈추지 않는다, exit 1):

```text
FAIL starter.c:326  d1.nlimbo == 1 && d1.limbo[0].epoch == 0 && ebr_reclaim(&d1) == 0
FAIL starter.c:327  ebr_try_advance(&d1) && ebr_reclaim(&d1) == 0
FAIL starter.c:328  ebr_try_advance(&d1) && ebr_reclaim(&d1) == 1 && was_freed(50) && d1.nlimbo == 0
FAIL starter.c:333  atomic_load(&db.ebr.slot[0].word) == 1u
FAIL starter.c:336  ebr_try_advance(&db.ebr) && atomic_load(&db.ebr.epoch) == 1u
...
FAIL starter.c:372  atomic_load(&g_live) == 1
SKIP 4 stress checks (Part A/B): unit checks failed
FAIL starter.c:397  qsbr_publish(&q, 2) == 0 && qsbr_get(&q)->version == 2 && atomic_load(&q.gp) == 2
...
FAIL starter.c:407  qsbr_publish(&q, 5) == 0 && qsbr_publish(&q, 6) == -1
SKIP 3 stress checks (Part C): unit checks failed
7/42 passed
```

하네스 구조는 OS-M08 과 같다: Part 마다 단일 스레드 단위 테스트가 다 통과해야 스트레스가 돈다. 단위 테스트는 **리더 "핸들" 번호로 순서를 강제** 한다 (`cfg_read_begin(&db, 0)` 을 부른 뒤 나가지 않으면 리더 0 이 "멈춘" 것). 해제 여부는 포인터가 아니라 **버전 번호 기록** (`was_freed(v)`) 으로 본다 — `free` 뒤의 포인터 값은 C11 6.2.4p2 에 따라 불확정이라 비교조차 하면 안 된다. `cfg_free` 는 해제 직전에 블록을 `0xA5` 로 덮어(독 칠하기) 늦은 리더가 있으면 체크섬이 깨지게 한다.

관찰용 파일 (하네스와 별개, 모두 `solution.c` 를 `main` 이름만 바꿔 포함):

```bash
cc -std=c11 -O2 -pthread -DMODE=0 demo_uaf.c -o uaf0 && ./uaf0                                     # 즉시 free: 엉뚱한 버전
cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer -pthread -DMODE=0 demo_uaf.c -o uaf0a && ./uaf0a
cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer -pthread -DMODE=1 demo_uaf.c -o uaf1a && ./uaf1a
cc -std=c11 -O2 -pthread -DRV=0 -DWV=0 demo_sb.c -o sb00 && ./sb00          # 읽기 진입 SB 리트머스 (RV/WV: 0 seq_cst, 1 rel/acq, 2 fence, 3 relaxed)
cc -std=c11 -O2 -S demo_asm.c -o -                                          # arm64 읽기 경로 기계어
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_asm.c -o -
cc -std=c11 -O2 -pthread -DNTASK=12 demo_bench.c -o bench && ./bench 1000   # EBR/QSBR/rwlock/mutex 처리량
cc -std=c11 -O2 -pthread -DCFG_N=1024 -DLIMBO_CAP=100000 demo_stall.c -o stall && ./stall   # 멈춘 리더의 메모리
```

## 4. 힌트

<details><summary>힌트 1 — 슬롯 한 워드에 두 정보</summary>

`active` 와 `epoch` 을 따로 두면 "active=1 을 썼는데 epoch 은 아직 옛것" 같은 중간 상태가 생긴다. 64 비트 한 워드 `(e << 1) | 1` 로 묶으면 진입·이탈이 store 하나씩이고, writer 는 load 하나로 둘 다 본다. 64 비트 epoch 은 1 ns 마다 올려도 584 년 동안 넘치지 않는다 — 고전 구현의 `epoch % 3` 은 저장 공간 문제(연습 4)지 정확성 문제가 아니다.
</details>

<details><summary>힌트 2 — 진입의 순서: 무엇이 무엇보다 먼저 보여야 하나</summary>

리더: "슬롯에 나 있음" store → `cur` load. writer: `cur` 교체 → 슬롯 load. 두 스레드가 **각자 store 후 상대 것을 load** — C-A03 · ARM-M05 의 SB(store buffering) 모양이다. 둘 다 상대의 store 를 못 보면 "리더는 옛 포인터, writer 는 리더 없음" → 해제 → UAF. release/acquire 는 SB 를 막지 않는다. 무엇을 써야 하나?
</details>

<details><summary>힌트 3 — 왜 +2 인가</summary>

리더가 epoch `e` 로 들어와 옛 포인터를 읽었다. writer 가 교체 후 기록한 retire epoch `r` 은 `e` 이상이다. 전진 규칙은 "활동 중 슬롯 == G" — 이 리더가 남아 있으면 `G = e → e+1` 은 **된다** (리더가 `e` 에 있으니). `e+1 → e+2` 는 안 된다. 그러면 `r + 1` 에서 해제하면? 강제 스케줄 테스트(`starter.c:350`)를 그대로 따라가 보라.
</details>

<details><summary>힌트 4 — limbo 가 꽉 찼을 때</summary>

선택지는 (1) 교체를 거절하고 호출자가 다시 시도(이 레슨), (2) `synchronize` 로 기다림(writer 블록), (3) 메모리를 더 할당(상한 없음 = 멈춘 리더 하나가 힙을 다 먹음). 거절 전에 **한 번 더 전진·회수** 를 시도해야 "방금 리더가 나갔는데 거절" 을 피한다.
</details>

<details><summary>힌트 5 — QSBR 은 왜 seq_cst 가 필요 없나</summary>

태스크가 루프 맨 위에서 `gp` 를 acquire 로 읽어 값 `g` 를 봤다면, writer 의 "교체 → `gp` 증가(release)" 가 그 전에 일어났다 → 그 뒤 `cur` load 는 새 포인터를 본다. writer 는 `seen ≥ g` 를 acquire 로 봐야만 재사용한다. SB 모양(서로의 store 를 못 보는 경우)이 생기려면 "태스크가 store 하고 곧바로 cur 를 읽는" 순서가 문제인데, QSBR 에서는 그 store 가 **옛 것을 놓는다** 는 뜻이라 늦게 보여도 보수적일 뿐이다. 예외: offline → online (연습 3).
</details>

<details><summary>힌트 6 — 정적 풀 크기</summary>

LIVE 1 개는 항상 있다. 교체 직후 옛 버퍼는 최소 한 "태스크 한 바퀴" 동안 묶인다. 3 개면 "게시 → 한 바퀴 안에 또 게시" 까지 버틴다. 그보다 빠른 갱신은 `-1` 로 거절해 **최신값을 늦추는 쪽** 을 택한다 (제어 루프는 1 주기 늦은 보정값보다 깨진 보정값이 훨씬 위험하다).
</details>

## 5. 풀이

### 0. 필요한 규칙 (자세한 건 C-A03 · ARM-M05 · MCPP-M13)

- **게시(publish)**: 새 테이블을 다 채운 뒤 포인터를 release 이상으로 바꾸고, 리더는 acquire 이상으로 읽는다 → 리더는 완성된 내용만 본다 (C-A03 의 1칸 채널, OS-M08 의 MP 와 같은 규칙).
- **SB(store buffering)**: 두 스레드가 각자 store 후 상대 변수를 load 하면, seq_cst(또는 양쪽 seq_cst fence)가 아니면 둘 다 옛 값을 볼 수 있다. Apple M6 에서 `stlr; ldapr` 의 SB 재배치가 실측됐다 (ARM-M05).
- **해제 안전성** = 리더의 마지막 읽기가 writer 의 `free` 보다 happens-before. 이탈 store 를 release, writer 의 슬롯 load 를 acquire(이상)로 하면 synchronizes-with 가 생긴다.

### Part A — 진입·이탈·전진·회수

```c
static void ebr_enter(ebr_t *d, int r) {
    uint64_t e = atomic_load_explicit(&d->epoch, memory_order_relaxed);        /* 옛 값이어도 안전 */
    atomic_store_explicit(&d->slot[r].word, (e << 1) | 1u, memory_order_seq_cst);
}   /* 다음: cfg_read_begin 이 cur 를 seq_cst 로 load */
static void ebr_exit(ebr_t *d, int r) {
    atomic_store_explicit(&d->slot[r].word, 0, memory_order_release);
}
```

| 연산 | 순서 | 이유 |
|---|---|---|
| 진입 때 전역 epoch 읽기 | relaxed | 옛 값 `e` 를 읽어도 "활동 중 슬롯 ≠ G" 라 전진을 막을 뿐(보수적) |
| 진입 슬롯 store | **seq_cst** | ① cur load 와의 SB 순서 ② release 역할: 지난 구간의 읽기 → writer 의 free |
| cur load | **seq_cst** | SB 짝. acquire 면 arm64 `ldapr` = RCpc → store 를 앞지를 수 있다 |
| 이탈 store | release | 이번 구간의 읽기 → writer 의 free |
| writer: 교체 | seq_cst (`swpal`) | 게시(release) + 뒤의 슬롯 load 와 SB 순서 |
| writer: 슬롯·epoch load | seq_cst | SB 짝 + 이탈 release 와 synchronizes-with |

전진과 회수:

```c
uint64_t g = load(epoch);
for each slot: w = load(slot);  if ((w & 1) && (w >> 1) != g) return false;
return CAS(epoch, g, g + 1);            /* 여러 writer 가 동시에 불러도 1 칸씩만 */
/* reclaim: limbo[i].epoch + 2 <= load(epoch) 이면 free_fn */
```

**왜 +2 인가 (정확성 논증).** 모든 동기화 연산이 seq_cst 라 하나의 전순서 S 가 있다. 리더가 옛 버전 `old` 를 읽었다면 S 에서 `슬롯 store(e) < cur load(=old) < writer 교체 < retire 의 epoch load(= r)`. 그래서 교체 이후로 이 리더는 S 에서 "epoch `e` 로 활동 중" 으로 보이고 `e ≤ r` 이다. 리더가 남아 있는 동안 `G` 는 최대 `e + 1` 까지만 간다 (`e → e+1` 은 리더 슬롯 == G 라 허용, `e+1 → e+2` 는 슬롯 `e` ≠ G 라 금지). 해제 조건 `r + 2 ≤ G` 는 `G ≥ e + 2` 를 요구하므로 리더가 나가기 전엔 성립할 수 없다. `r + 1` 이면 `r = e` 인 경우 `G = e + 1` 에서 해제 — 리더가 아직 쥐고 있는데. 진입 때 옛 epoch 을 읽은 리더(`e < G`)도 같은 식으로 더 보수적일 뿐이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">강제 스케줄 (solution.c Part B 단위 테스트): 1 칸 전진은 grace period 가 아니다</text>
  <rect x="80" y="40" width="200" height="24" class="f-mut" stroke="currentColor"/>
  <text x="150" y="57" fill="currentColor" font-family="monospace">G = 5</text>
  <rect x="280" y="40" width="250" height="24" class="f-warn" stroke="currentColor"/>
  <text x="380" y="57" fill="currentColor" font-family="monospace">G = 6</text>
  <rect x="530" y="40" width="190" height="24" class="f-ok" stroke="currentColor"/>
  <text x="600" y="57" fill="currentColor" font-family="monospace">G = 7</text>
  <text x="10" y="100" fill="currentColor">리더 0</text>
  <rect x="100" y="86" width="460" height="22" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="108" y="102" fill="currentColor" font-family="monospace">enter(e=5), v3 쥠 ......................... exit</text>
  <text x="10" y="150" fill="currentColor">리더 1</text>
  <rect x="300" y="136" width="300" height="22" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="308" y="152" fill="currentColor" font-family="monospace">enter(e=6), v4 쥠 ........ exit</text>
  <text x="10" y="200" fill="currentColor">writer</text>
  <path d="M190 120 L190 230" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="120" y="248" fill="currentColor">교체 v3→v4</text>
  <text x="120" y="264" fill="currentColor">retire v3 @5</text>
  <path d="M280 190 L280 230" stroke="currentColor" stroke-width="2"/>
  <text x="232" y="282" fill="currentColor">전진 OK (R0==5)</text>
  <path d="M440 190 L440 230" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="390" y="248" fill="currentColor">전진 실패: R0 5≠6</text>
  <text x="390" y="264" fill="currentColor">blocker = 0, v3 유지</text>
  <path d="M530 190 L530 230" stroke="currentColor" stroke-width="2"/>
  <text x="540" y="248" fill="currentColor">R0 exit 뒤 전진 OK</text>
  <text x="540" y="264" fill="currentColor">5+2 ≤ 7 → v3 해제</text>
  <text x="540" y="282" fill="currentColor">v4(@6) 는 아직 (R1)</text>
  <text x="10" y="230" fill="currentColor" font-family="monospace">r+1 이면</text>
  <text x="10" y="246" fill="currentColor" font-family="monospace">여기서 해제!</text>
</svg>
```

이 그림이 바로 `solution.c` Part B 단위 테스트의 순서다 (`CHECK(atomic_load(&db.ebr.epoch) == e0 + 1 && !was_freed(3))` → `CHECK(!ebr_try_advance(...) && ebr_blocker(...) == 0)` → 리더 0 이탈 → `was_freed(3) && !was_freed(4)`).

**+1 로 바꾸면 무엇이 잡나 (실측, 돌연변이 시험).** `ebr_reclaim` 조건만 `epoch + 1 <= g` 로 바꾼 파일 (`/tmp` 의 작업 사본):

- 강제 스케줄 단위 테스트: **매번** 12 개 FAIL. ASan 빌드는 `heap-use-after-free ... READ of size 4 ... in main m1.c:405` (= `cfg_ok(r0)` 줄)에서 멈춘다 — 결정적.
- 단위 테스트를 건너뛰고 스레드 스트레스만 돌리면 (`-O2`, 독 칠한 체크섬으로 검출): 10 회 실행에서 깨진 읽기 `9, 9, 12, 11, 5, 18, 7, 2, 13, 4` 건 (실행당 약 170 만 읽기 중). **10/10 잡혔지만 건수는 한 자리** — 다른 기계, 다른 부하에선 0 일 수 있다. 결정적 강제 스케줄이 1순위, 스트레스는 보조인 이유.

### Part A — 교체와 역압

```c
pthread_mutex_lock(&db->wlock);                 /* writer 끼리만 직렬화 — 리더는 이 락을 모른다 */
ebr_reclaim(&db->ebr);
if (nlimbo == LIMBO_CAP) { ebr_try_advance; ebr_reclaim; if (still full) { unlock; return -1; } }
cfg_t *n = cfg_new(version);                    /* 다 채운 뒤 */
cfg_t *old = atomic_exchange_explicit(&db->cur, n, memory_order_seq_cst);
ebr_retire(&db->ebr, old);                      /* 교체 "뒤" 의 epoch 으로 기록 */
ebr_try_advance(&db->ebr);  ebr_reclaim(&db->ebr);
```

`retire` 가 epoch 을 교체 **뒤에** 읽는 것이 중요하다. 교체 전에 읽은 `r` 을 쓰면, 그 사이 epoch 이 오르고 새로 들어온 리더가 옛 포인터를 읽을 수 있는데 기록은 그보다 작은 epoch 이라 일찍 풀린다.

스트레스 결과 (6 회): 매번 `42/42 passed`, 리더 합계 155만~241만 읽기, 거절된 갱신 4~600 회 — 리더가 E 코어에 밀려 늦게 나가면 limbo 32 칸이 차서 writer 가 양보한다. 그래도 **깨진 테이블 0, 역행 0, `해제 수 == 20000`, 살아 있는 버전 1**.

### Part A — 실제 기계어: `stlr`/`ldar` 와 `ldapr` 한 글자

`demo_asm.c` 는 `ebr_enter + cfg_read_begin + exit` 를 그대로 옮긴 함수와, acquire/release 로 "가볍게" 한 틀린 버전을 나란히 둔다.

```bash
cc -std=c11 -O2 -S demo_asm.c -o -        # Apple clang 21, arm64-apple-macos
```

```text
_ebr_read:                       ; seq_cst store + seq_cst load
	ldr	x8, [x8]                 ; epoch (relaxed)
	orr	x8, x9, x8, lsl #1
	stlr	x8, [x9]              ; slot = (e<<1)|1
	ldar	x8, [x10]             ; cur       ← RCsc: 앞의 stlr 을 앞지르지 못함
	ldr	w0, [x8, #8]
	stlr	xzr, [x9]             ; slot = 0 (release)
_ebr_read_ra:                    ; release store + acquire load
	stlr	x8, [x9]
	ldapr	x8, [x10]             ; cur       ← RCpc: 앞의 stlr 을 앞지를 수 있음
	...
_qsbr_loop_once:
	ldapr	x8, [x8]              ; gp (acquire)
	stlr	x8, [x9]              ; seen = gp (release)
	ldapr	x8, [x8]              ; cur (acquire)
_swap_cur:
	swpal	x0, x0, [x8]          ; atomic_exchange seq_cst = LSE SWPAL (기본 플래그에서 바로 LSE)
```

(일부 줄 생략, 순서·명령은 실제 출력 그대로.) 차이는 `ldar` ↔ `ldapr` 한 글자다. Cortex-M4 (`--target=thumbv7em-none-eabi`)에선 이렇게 나온다:

```text
ebr_read:                          ebr_read_ra:
	dmb	sy                          dmb	sy
	str	r1, [r2]       ; slot      str	r1, [r2]       ; slot
	movw	r1, :lower16:g_cur          movw	r1, :lower16:g_cur
	dmb	sy             ; ← SB 를 막는 것    movt	r1, :upper16:g_cur
	movt	r1, :upper16:g_cur          ldr	r1, [r1]       ; cur
	ldr	r1, [r1]       ; cur       dmb	sy
	dmb	sy
```

seq_cst store 는 `dmb sy; str; dmb sy`, release store 는 `dmb sy; str` 뿐 — **store 와 load 사이의 `dmb sy` 하나** 가 SB 를 막는다. `swap_cur` 는 `dmb sy; ldrex; strex; bne` 루프 + `dmb sy` (Cortex-M4 에 LSE 없음).

### Part A — 실측: acquire/release 로 진입하면 UAF 창이 열리나

`demo_sb.c` 는 라운드마다 새 변수 쌍을 쓰고 두 스레드가 스핀 장벽으로 같이 출발하는 SB 리트머스다. 리더 `slot=1; p=load(ptr)`, writer `exchange(ptr, NEW); s=load(slot)`. **금지 결과 = 리더는 OLD, writer 는 slot 0** (그대로 두면 writer 가 옛 버전을 해제). 2백만 라운드 × 6 회, `uptime` load 7.3 (다른 벤치와 동시):

| 리더 (RV) | writer (WV) | 리더 명령 | writer 명령 | UAF 창 관찰 (6 회) |
|---|---|---|---|---|
| seq_cst | seq_cst | `stlr; ldar` | `swpal; ldar` | 0 0 0 0 0 0 (+ 앞선 3 회도 0) |
| rel/acq | rel/acq | `stlr; ldapr` | `swpl; ldapr` | 0 0 0 **2** 0 0 |
| seq_cst fence | seq_cst fence | `str; dmb ish; ldr` | `swp; dmb ish; ldr` | 0 0 0 0 0 0 |
| relaxed | relaxed | `str; ldr` | `swp; ldr` | 78 752 · 73 086 · 88 712 · 107 375 · 123 134 · 114 524 |
| rel/acq | relaxed | | | 0 0 1 1 0 0 |
| relaxed | rel/acq | | | 59 4 26 26 13 18 |
| seq_cst | relaxed | | | 0 1 0 2 0 0 |
| relaxed | seq_cst | | | 11 2 12 3 26 33 |

읽는 법: (1) **rel/acq 양쪽도 1 200 만 라운드 중 2 번 실제로 깨졌다** — C11 이 허용하는 결과를 M6 가 실제로 냈다. (2) 한쪽만 seq_cst 로는 안 된다 (SB 는 양쪽이 다 막아야 금지). (3) seq_cst 양쪽, fence 양쪽은 0 — 아키텍처가 금지(0 은 증명이 아니라 금지 규칙과 일치할 뿐). 이 장벽식 하네스는 두 스레드가 거의 번갈아 실행돼(`r_old ≈ w_idle ≈ 100만`) 겹치는 창이 좁다 — 숫자의 크기보다 **0 이냐 아니냐** 만 보라.

### Part B — "교체하자마자 free" 의 UAF (강제 스케줄 + ASan)

`demo_uaf.c` 의 스케줄: ① 리더가 v1 포인터를 얻고 신호 → ② writer 가 v2 게시, `MODE=0` 이면 v1 을 즉시 `free`, 그리고 같은 크기 할당 하나(v77) → ③ 리더가 같은 포인터로 읽음.

ASan 없이 `-O2` (3 회 모두 같음, 실제 출력):

```text
reader: got v1 (gain[0]=-1640531535)
writer: published v2, freed v1 immediately
reader: after update, same pointer says v77 gain[0]=-1766876611 checksum OK
```

**체크섬이 맞는다.** 해제된 v1 블록을 `malloc` 이 v77 에 그대로 재사용했고, 리더는 "온전한, 그러나 존재해선 안 되는 버전" 을 읽었다. 독 칠하기·체크섬으로도 못 잡는 가장 나쁜 형태다 — 그래서 ASan 이 필요하다. `-fsanitize=address` (실제 출력, 그림자 바이트 표 생략):

```text
==85369==ERROR: AddressSanitizer: heap-use-after-free on address 0x60d000000040 at pc 0x000104d992b0 bp 0x00016b0f2f20 sp 0x00016b0f2f18
READ of size 8 at 0x60d000000040 thread T1
    #0 0x000104d992ac in reader demo_uaf.c:45
    #1 0x0001055d1d64 in asan_thread_start(void*)+0x4c (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x3dd64)
    ...
0x60d000000040 is located 0 bytes inside of 144-byte region [0x60d000000040,0x60d0000000d0)
freed by thread T0 here:
    #0 0x0001055d5258 in free+0x7c (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x41258)
    #1 0x000104d98d58 in main demo_uaf.c:61
previously allocated by thread T0 here:
    #0 0x0001055d5164 in malloc+0x78 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x41164)
    #1 0x000104d971fc in cfgdb_init solution.c:159
    #2 0x000104d98b90 in main demo_uaf.c:56
SUMMARY: AddressSanitizer: heap-use-after-free demo_uaf.c:45 in reader
==85369==ABORTING
```

읽는 법: `READ of size 8 ... thread T1` = 리더가 `p->version` (8 바이트) 을 읽음, `0 bytes inside of 144-byte region` = `cfg_t` 맨 앞, `freed by thread T0 ... main demo_uaf.c:61` = `cfg_update_now` 가 인라인된 writer 의 `free`, `previously allocated ... cfgdb_init` = v1. ASan 은 해제된 블록을 격리(quarantine)해 곧바로 재사용하지 않기 때문에 v77 이 그 자리에 오지 않고, 그림자 바이트 `fd`(freed) 로 잡힌다. `MODE=1` (EBR) 같은 스케줄:

```text
reader: got v1 (gain[0]=-1640531535)
writer: published v2, v1 retired (limbo=1, freed so far=0)
reader: after update, same pointer says v1 gain[0]=-1640531535 checksum OK
writer: after reader left + synchronize: limbo=0, freed=1 (v1 freed: yes)
```

### Part B — 막는 리더 찾기, 상한 있는 synchronize

`ebr_blocker` 는 전진 검사와 같은 루프에서 "활동 중이고 epoch ≠ G" 인 첫 슬롯을 돌려준다. 실제 시스템에선 이 값을 워치독·로그에 남긴다 — Linux 의 RCU CPU stall warning 이 "어느 CPU/태스크가 grace period 를 막고 있나" 를 찍는 것과 같은 역할이다 (문서 기준).

`ebr_synchronize` 는 호출 시점 `G` 에서 **2 칸** 오를 때까지 `try_advance` 를 반복한다 (`synchronize_rcu` 의 뜻 = "지금 이전에 시작한 읽기 구간이 다 끝날 때까지"). 상한 `max_spins` 는 단순한 친절이 아니다: 테스트 `CHECK(!cfg_synchronize(&db, 50))` 처럼 **읽기 구간 안의 스레드가 synchronize 를 부르면** 자기 자신이 grace period 를 막아 영원히 기다린다 (Linux 커널에선 `rcu_read_lock` 안에서 `synchronize_rcu` = 교착, lockdep 이 경고).

### Part B — 실측: 멈춘 리더 한 명이 붙잡는 메모리

`demo_stall.c`: 리더 4 명, 0 번이 t = 50~250 ms 동안 읽기 구간 안에서 멈춤, writer 는 100 µs 마다 갱신, `cfg_t` = 4 112 B (`-DCFG_N=1024`). 실제 출력 (`uptime` load 4.9):

```text
EBR, LIMBO_CAP=100000, cfg_t=4112 B, reader 0 stalls 50..250 ms, update every 100 us
 t(ms)       ok  refused   held(KB)  blocker    epoch
    50      381        0        8.0        0      381
   100      764        0     1546.0        0      381
   150     1146        0     3080.0        0      381
   200     1528        0     4614.0        0      381
   250     1884        0     6043.5        0      381
   275     2071        0        8.0       -1      567
   400     3033        0        8.0       -1     1529
after join + synchronize: limbo=0, live versions=1
EBR, LIMBO_CAP=256 ...
   100      641      129     1032.0        0      386
   250      641     1295     1032.0        0      386
   275      827     1301        8.0       -1      573
QSBR static pool (3 buffers x 4112 B) ...
    75      386      193       12.0        -      387
   250      386     1550       12.0        -      387
   275      577     1552        8.0        -      578
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">멈춘 리더(50~250 ms) 동안 붙잡힌 메모리 — demo_stall.c 실측</text>
  <path d="M80 260 L690 260" stroke="currentColor"/>
  <path d="M80 260 L80 50" stroke="currentColor"/>
  <path d="M80 198.5 L690 198.5 M80 137 L690 137 M80 75.5 L690 75.5" stroke="currentColor" stroke-dasharray="2 4" fill="none"/>
  <text x="42" y="264" fill="currentColor">0</text>
  <text x="28" y="202" fill="currentColor">2 MB</text>
  <text x="28" y="141" fill="currentColor">4 MB</text>
  <text x="28" y="79" fill="currentColor">6 MB</text>
  <text x="76" y="278" fill="currentColor">0</text>
  <text x="220" y="278" fill="currentColor">100</text>
  <text x="370" y="278" fill="currentColor">200</text>
  <text x="520" y="278" fill="currentColor">300</text>
  <text x="664" y="278" fill="currentColor">400 ms</text>
  <rect x="155" y="50" width="300" height="210" class="f-mut" opacity="0.35"/>
  <text x="250" y="66" fill="currentColor">리더 0 멈춤</text>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="117.5,259.8 155,259.8 192.5,236 230,212.4 267.5,188.7 305,165.2 342.5,141.6 380,118 417.5,94.6 455,74 492.5,259.8 680,259.8"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4" points="117.5,259.8 155,259.8 192.5,236 230,228.2 455,228.2 492.5,259.8 680,259.8"/>
  <text x="470" y="80" fill="currentColor">EBR, 상한 없음: 6.0 MB (≈1 500 버전)</text>
  <text x="470" y="222" fill="currentColor">EBR, LIMBO_CAP=256: 1.0 MB 에서 멈춤</text>
  <text x="470" y="238" fill="currentColor">(대신 갱신 1 295 회 거절)</text>
  <text x="470" y="254" fill="currentColor">QSBR 풀 3 개: 12 KB, 갱신 1 550 회 거절</text>
</svg>
```

붙잡힌 메모리 = **갱신 속도 × 멈춘 시간 × 테이블 크기** (≈ 7.6 k/s × 0.2 s × 4 KB ≈ 6 MB). 이것이 EBR(과 RCU 전반)의 약점이다: **리더 한 명이 전체 회수를 막는다.** 대응은 셋 중 하나를 고르는 것이다 — 메모리 상한(갱신 거절, 위 2번째 줄), writer 블록(`synchronize`), 또는 리더별로 쥔 포인터만 보호하는 hazard pointer (5층 LLAT-M03: 메모리 상한이 `리더 수 × 포인터 수` 로 고정되는 대신 리더 경로가 더 비싸다). 멈춘 리더가 풀리면 다음 갱신 한 번에 전부 해제된다 (t=275).

### Part B — 실측: rwlock / mutex 대비 읽기 처리량

`demo_bench.c` (리더 N 명이 150 ms 동안 "계수 4 개 + version" 읽기 반복, writer 1 명이 1 ms 마다 게시; QSBR 은 읽을 때마다 quiescent — QSBR 에 가장 불리한 경우). 기계: `sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu` → `Apple M6`, `2`, `4`, `6` (Super 2 + Performance 4 + Efficiency 6), `hw.cachelinesize` = 128, `cc --version` → `Apple clang version 21.0.0 (clang-2100.0.123.102)`. 실행 직전·직후 `uptime` load 3.95 / 4.36 (다른 세션 벤치와 동시 실행 → **배수만** 믿을 것). 리더 합계 Mops/s (괄호 = writer 게시 횟수):

| 리더 | EBR | QSBR | rwlock | mutex | EBR / rwlock |
|---|---|---|---|---|---|
| 1 | 1321.4 (122) | 1334.5 (124) | 176.9 (122) | 299.0 (123) | 7.5× |
| 2 | 2516.2 (124) | 2508.4 (124) | 10.3 (121) | 50.8 (120) | 244× |
| 4 | 4269.4 (124) | 4104.3 (124) | 9.1 (123) | 32.5 (121) | 469× |
| 6 | 5832.3 (124) | 5589.7 (125) | 6.6 (120) | 33.8 (120) | 884× |
| 8 | 6548.2 (124) | 6681.7 (125) | 5.8 (118) | 35.8 (120) | 1129× |
| 10 | 7709.2 (123) | 7587.7 (114) | 10.5 (111) | 38.3 (122) | 734× |

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">읽기 처리량 (Mops/s, 로그 축) — writer 1 ms 주기, Apple M6, load ≈ 4</text>
  <path d="M80 280 L700 280 M80 280 L80 40" stroke="currentColor"/>
  <path d="M80 220 L700 220 M80 160 L700 160 M80 100 L700 100 M80 40 L700 40" stroke="currentColor" stroke-dasharray="2 4" fill="none"/>
  <text x="58" y="284" fill="currentColor">1</text>
  <text x="50" y="224" fill="currentColor">10</text>
  <text x="42" y="164" fill="currentColor">100</text>
  <text x="34" y="104" fill="currentColor">1000</text>
  <text x="26" y="44" fill="currentColor">10000</text>
  <text x="76" y="298" fill="currentColor">1</text>
  <text x="145" y="298" fill="currentColor">2</text>
  <text x="283" y="298" fill="currentColor">4</text>
  <text x="421" y="298" fill="currentColor">6</text>
  <text x="559" y="298" fill="currentColor">8</text>
  <text x="690" y="298" fill="currentColor">10</text>
  <text x="330" y="314" fill="currentColor">리더 스레드 수</text>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="80,92.7 148.9,76 286.7,62.2 424.4,54.1 562.2,51 700,46.8"/>
  <polyline fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 4" points="80,92.5 148.9,76 286.7,63.2 424.4,55.2 562.2,50.5 700,47.2"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="2 3" points="80,131.5 148.9,177.6 286.7,189.3 424.4,188.3 562.2,186.8 700,185"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="80,145.1 148.9,219.2 286.7,222.5 424.4,230.8 562.2,234.2 700,218.7"/>
  <g class="f-acc" stroke="currentColor">
    <circle cx="80" cy="92.7" r="3"/><circle cx="148.9" cy="76" r="3"/><circle cx="286.7" cy="62.2" r="3"/>
    <circle cx="424.4" cy="54.1" r="3"/><circle cx="562.2" cy="51" r="3"/><circle cx="700" cy="46.8" r="3"/>
  </g>
  <g class="f-warn" stroke="currentColor">
    <circle cx="80" cy="145.1" r="3"/><circle cx="148.9" cy="219.2" r="3"/><circle cx="286.7" cy="222.5" r="3"/>
    <circle cx="424.4" cy="230.8" r="3"/><circle cx="562.2" cy="234.2" r="3"/><circle cx="700" cy="218.7" r="3"/>
  </g>
  <text x="440" y="78" fill="currentColor">EBR (실선) ≈ QSBR (점선)</text>
  <text x="440" y="178" fill="currentColor">mutex (점점선)</text>
  <text x="440" y="252" fill="currentColor">rwlock: 리더 2 명부터 ~10</text>
</svg>
```

해석:

- **EBR 은 리더 수에 거의 비례해 는다** (1 → 10 명에 5.8 배; 12 코어 중 E 코어·다른 작업이 섞여 완전 비례는 아님). 리더는 자기 캐시 라인의 슬롯에만 쓰고, 공유하는 `cur`·`epoch` 은 읽기만 하므로 모든 코어가 그 라인을 Shared 상태로 같이 갖는다. 1 리더 1321 Mops/s = 읽기 구간 하나 ≈ 0.76 ns — 루프 기계어를 확인했다 (`stlr` / `ldar` / `stlr` 이 실제로 루프 안에 있음, `cc -S demo_bench.c`).
- **rwlock 은 리더가 2 명이 되는 순간 17 배 떨어진다** (176.9 → 10.3). 리더끼리 서로 막지 않아도 `rdlock`/`unlock` 이 **공유 카운터를 RMW** 하므로 그 캐시 라인이 코어 사이를 핑퐁한다 (LLAT-B06 의 경합 카운터와 같은 현상). macOS `pthread_rwlock` 이 내부에서 정확히 무엇을 하는지는 확인하지 않았다 — 측정값만 믿을 것. mutex 가 rwlock 보다 빠른 것도 이 구현의 특성이다 (리눅스 glibc 에선 다를 수 있음, 미검증).
- writer 를 50 µs 주기로 바꿔도 (`./bench 50`, load 4.1) EBR 1233 → 7762, rwlock 148 → 9.4 로 모양이 같다. rwlock 쪽 writer 게시 횟수는 리더가 많을수록 줄었다 (2179 → 1222, 8 리더) — **writer 굶주림** 이 숫자로 보인다. EBR writer 는 리더와 상관없이 2014~2318 회.
- QSBR 이 EBR 보다 빠르지 않은 이유: 여기선 읽을 때마다 quiescent(`stlr` 1 번)를 불러 EBR(`stlr` 2 번 + `ldar`)과 비슷하다. 실제 제어 루프는 한 바퀴에 여러 번 읽고 맨 위에서 한 번만 보고하므로 읽기 자체는 `ldapr` 하나다. 커널 비선점 RCU 의 `rcu_read_lock()` 은 아예 **명령 0 개** 다 (아래).

### Part C — QSBR: 루프 맨 위 = quiescent state

```c
static void qsbr_quiescent(qsbr_t *q, int t) {
    uint64_t g = atomic_load_explicit(&q->gp, memory_order_acquire);
    atomic_store_explicit(&q->task[t].seen, g, memory_order_release);
}
/* publish: poll → FREE 버퍼에 채움 → exchange(cur, acq_rel) → g = fetch_add(gp, release) + 1 → old.retired_gp = g */
/* poll:    min(seen) 계산 → retired_gp <= min 인 RETIRED 를 FREE 로 */
```

정확성: 태스크가 `seen ≥ g` 를 썼다면 그 태스크는 `gp` 에서 `g` 이상을 acquire 로 읽은 것이고, writer 의 교체가 `gp` 증가(release) 전에 있었으므로 그 뒤의 `cur` load 는 새 버퍼를 본다. 또 `seen` 의 release store 이전의 읽기(지난 바퀴에 옛 버퍼를 읽은 것)는 writer 의 acquire load 와 synchronizes-with → writer 가 그 버퍼를 다시 채우는 쓰기보다 먼저 일어난다. **seq_cst 가 필요 없다** — EBR 과 달리 "들어간다" 를 알리는 store 가 없고, 보고하는 store 는 "놓았다" 는 뜻이라 늦게 보여도 보수적일 뿐이다. (단 offline → online 으로 되돌아오는 순간은 EBR 진입과 같은 SB 모양이 돼서 seq_cst 가 필요하다 — 연습 3.)

강제 스케줄 단위 테스트가 확인하는 것: 태스크 3 개 중 하나라도 루프 맨 위를 안 지나면 버퍼가 안 돌아온다 (`qsbr_poll == 0`), 셋 다 지나면 2 개 반환, "태스크 2 가 v3 시절에 멈춤" 이면 v3 버퍼가 묶여 두 번째 게시가 `-1`. 스트레스 (태스크 4 + writer 20 000 게시): 매번 깨짐 0, 역행 0, 20 000 회 전부 게시.

**펌웨어 매핑.** FreeRTOS/Zephyr 제어 태스크(예: 1 kHz 모터 전류 루프)가 보정 테이블(ADC 오프셋, 게인, 룩업 테이블)을 읽고, 통신 태스크가 호스트에서 받은 새 테이블을 게시하는 구조다.

| 질문 | 답 |
|---|---|
| 왜 IRQ 를 끄고 `memcpy` 하지 않나 | 4 KB 복사 동안 인터럽트 금지 = 제어 루프 지터. 포인터 교체는 정렬된 32 비트 store 1 개 (Cortex-M 에서 원자적) |
| 왜 seqlock 이 아니고 | seqlock 리더는 재시도한다 — 1 kHz 루프에 "가끔 두 번 읽기" 지터. RCU 리더는 재시도 없음. 대신 메모리 3 배 |
| 잠드는 태스크는 | 큐에서 블록되는 태스크는 루프 맨 위를 안 지나 grace period 를 영원히 막는다 → 블록 전에 offline 표시 (연습 3, liburcu `rcu_thread_offline` 과 같은 생각) |
| ISR 이 리더면 (단일 코어) | 태스크(writer)는 ISR 이 끝나야 다시 돈다 → ISR 은 writer 가 회수할 때 이미 끝났다. 남는 위험은 **선점된 하위 우선순위 태스크 리더** 뿐 → 그 태스크들만 quiescent 보고. 다중 코어(RP2040, STM32H7 듀얼)면 ISR 도 다른 코어에서 돌 수 있으니 ISR 끝 = quiescent 로 보고 |
| "충분히 기다렸다" (시간 기반) | 가장 긴 루프 주기보다 오래 기다린 뒤 재사용 — 간단하지만 주기 초과(디버거 정지, 플래시 쓰기로 인한 스톨)에서 깨진다. 이 레슨의 gp 카운터는 시간 가정을 하지 않는다 |
| 워치독과의 관계 | "모든 태스크가 체크인해야 워치독을 찬다" 는 태스크 워치독과 구조가 같다. `seen` 이 오래 안 바뀌는 태스크 = 멈춘 태스크 (진단 겸용) |

### 커널 RCU · liburcu 용어 대응 (개념 수준, 문서 기준)

| 커널 / liburcu | 이 레슨 | 비고 |
|---|---|---|
| `rcu_read_lock()` / `rcu_read_unlock()` | `ebr_enter` / `ebr_exit` | 비선점 커널(`CONFIG_PREEMPT_NONE`)에선 선점 금지 정도 (명령 거의 0). 선점형 RCU(`PREEMPT_RCU`)는 태스크별 중첩 카운터 (연습 1) |
| `rcu_dereference(p)` | `atomic_load(&cur)` | 커널은 `READ_ONCE` + 주소 의존성(consume 성격)에 기댄다 — C11 에선 acquire/seq_cst 로 대신 (C-A03) |
| `rcu_assign_pointer(p, v)` | `atomic_exchange(&cur, n)` | release store (`smp_store_release`) |
| `synchronize_rcu()` | `ebr_synchronize` | 블록. 읽기 구간 안에서 부르면 교착 |
| `call_rcu(head, fn)` / `kfree_rcu` | `ebr_retire` + 나중 `ebr_reclaim` | 비동기 콜백. 커널은 grace period 끝을 콜백으로 처리 |
| quiescent state | 이탈 / 루프 맨 위 | 커널 고전 RCU: 문맥 전환, idle, 사용자 모드 실행 = QS. 그래서 리더 표시가 필요 없다 (= QSBR) |
| grace period | `e + 2 ≤ G` / `seen ≥ retired_gp` | 커널은 Tree RCU 로 CPU 가 많아도 확장 (계층으로 QS 집계) |
| SRCU | — | 읽기 구간 안에서 잘 수 있는 변형 (domain 별 카운터) |
| liburcu flavours | — | `urcu-memb`(기본, `membarrier()` 로 리더의 fence 를 writer 쪽으로 옮김), `urcu-mb`(리더가 직접 `smp_mb`), `urcu-signal`(신호로 리더에 fence), `urcu-qsbr`(이 레슨 Part C, 리더 비용 최소, 스레드가 직접 QS 보고·offline), `urcu-bp`(스레드 등록 불필요) — 이름·기본값은 liburcu 버전에 따라 다름, 미검증 |

`urcu-memb` 의 아이디어는 Part A 의 숫자와 연결된다: 리더 진입의 `stlr; ldar`(또는 `dmb ish`) 비용을 없애고, writer 가 `membarrier()` 시스템 콜로 **모든 리더 CPU 에 한 번에 배리어를 강제** 한다 — 비대칭 배리어. 리더가 많이 읽고 writer 가 드물 때 맞는 거래다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_san && ./so_san
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan
```

실제 출력 (5 회 연속, load 4.3):

```text
  (A stress: 1873237 reads, 600 refused updates)
42/42 passed
  (A stress: 2397160 reads, 4 refused updates)
42/42 passed
  (A stress: 1557919 reads, 17 refused updates)
42/42 passed
  (A stress: 2089951 reads, 6 refused updates)
42/42 passed
  (A stress: 2407550 reads, 86 refused updates)
42/42 passed
```

ASan+UBSan: `42/42 passed`, 보고 없음, exit 0 — 20 000 번 교체·해제하는 동안 6 리더가 200 만 번 넘게 읽었는데 **use-after-free 0**. TSan 3 회: `42/42 passed`, 보고 없음, exit 0 (리더 33만~56만 읽기 — TSan 이 느려서). TSan 이 조용한 것도 의미가 있다: `cfg_free` 가 해제 직전 블록을 `memset` 으로 덮으므로, 늦은 리더가 있었다면 "리더의 read vs writer 의 memset" 데이터 레이스로 보고됐을 것이다.

## 7. 자주 하는 실수

1. **교체 직후 `free`** — "포인터 교체가 원자적이니 끝" 이 아니다. 교체 전에 포인터를 읽어 간 리더가 있다. `demo_uaf.c` 처럼 ASan 없이는 **체크섬까지 맞는 다른 버전(v77)** 을 읽어 조용히 틀린다. → retire + grace period.
2. **1 칸 전진으로 해제 (`e + 1`)** — 리더가 `e` 에 있으면 `e → e+1` 전진은 허용되므로 아직 쥐고 있다. 돌연변이 시험: 강제 스케줄 12 FAIL·ASan UAF, 스트레스는 실행당 2~18 건. → `e + 2 ≤ G`.
3. **진입을 release/acquire 로 "최적화"** — `stlr; ldapr` 는 SB 를 막지 않는다. 실측 1 200 만 라운드 중 2 번 UAF 창. → 슬롯 store 와 `cur` load 를 seq_cst (arm64 `stlr; ldar`, Cortex-M `str; dmb sy; ldr`), 또는 양쪽 seq_cst fence. writer 쪽도 짝이 필요하다.
4. **retire epoch 을 교체 전에 읽기** — 그 사이 epoch 이 오르면 기록이 실제보다 작아 일찍 풀린다. → 교체 뒤에 읽는다.
5. **읽기 구간 안에서 `synchronize`** — 자기 슬롯이 grace period 를 막아 교착. 커널에선 `rcu_read_lock` 안 `synchronize_rcu`. → 구간 밖에서, 또는 비동기 `retire`(`call_rcu`).
6. **읽기 구간 안에서 블록/긴 작업** — I/O·락 대기·200 ms 계산 동안 회수가 멈추고 메모리가 `갱신률 × 시간 × 크기` 로 는다 (실측 6 MB / 200 ms). → 필요한 값만 복사하고 구간을 나가거나, limbo 상한 + 역압.
7. **한 구간에서 `cur` 를 두 번 읽기** — 두 번째 load 는 다른 버전일 수 있다 (v8 의 gain 과 v9 의 version 을 섞음). → 한 번 읽어 지역 변수에 두고 그것만 쓴다.
8. **게시한 테이블을 제자리에서 고치기** — RCU 의 전제(불변)를 깨는 순간 리더는 찢어진 읽기를 한다. 한 필드만 바꿔도 복사 → 수정 → 교체. 쓰기가 잦거나 테이블이 크면 RCU 가 맞지 않는다 (seqlock, 분할 락).
9. **QSBR 에서 잠드는 태스크를 잊기** — 블록된 태스크는 `seen` 을 안 올려 grace period 가 영원히 안 끝나고 풀이 고갈된다. → 블록 전 offline, 깨어나서 online (연습 3).

## 8. 면접 Q&A

<details>
<summary>Q1. RCU 의 grace period 를 한 문장으로? 그리고 그걸 어떻게 "감지" 하나요?</summary>

"교체 시점에 읽기 구간 안에 있던 모든 리더가 적어도 한 번 구간 밖으로 나온(= quiescent state 를 지난) 시간" 이다. 그 뒤엔 옛 버전 포인터를 가진 리더가 있을 수 없다. 감지 방법이 구현을 가른다: EBR 은 리더가 진입 때 전역 epoch 을 슬롯에 적고 writer 가 "활동 중 슬롯 == G" 일 때만 epoch 을 올려 **2 칸** 오르면 grace period. QSBR 은 각 스레드가 주기적으로 "지금 아무것도 안 쥠" 을 보고하고 모두가 교체 뒤에 보고하면 끝. 커널 고전 RCU 는 문맥 전환·idle·사용자 모드를 QS 로 쳐서 리더가 아무 표시도 안 한다. 꼬리: "grace period 가 끝나면 모든 리더가 나갔나?" — 아니다. **교체 전에 시작한** 구간만 끝났다. 그 뒤 시작한 리더는 새 버전을 보므로 상관없다.
</details>

<details>
<summary>Q2. 왜 epoch 이 1 이 아니라 2 칸 올라야 해제하나요? "3-epoch EBR" 의 3 은 뭐죠?</summary>

리더가 epoch `e` 로 들어와 옛 포인터를 읽은 뒤 writer 가 retire(`r ≥ e`)한다. 전진 규칙은 "활동 중 슬롯이 모두 현재 G" 라서, 이 리더가 남아 있어도 `e → e+1` 은 된다 (리더가 `e` = G 이니까). `e+1 → e+2` 에서야 막힌다. 그래서 `G ≥ r + 2` 가 되어야 "그 리더는 나갔다" 가 보장된다 — 강제 스케줄 테스트에서 G 가 1 칸 오른 뒤에도 v3 은 살아 있어야 하고, +1 돌연변이는 ASan UAF 로 잡혔다. 3 은 저장 구조 이야기다: 어느 순간 limbo 항목은 epoch `G`(방금), `G-1`(아직 위험), `G-2`(안전) 세 부류뿐이라 목록 3 개를 `epoch % 3` 으로 돌려 쓰면 된다 (연습 4, 선형 limbo 와 해제 집합이 매 단계 같음을 확인). 이 레슨은 64 비트 epoch + 선형 limbo 로 같은 규칙을 쓴다.
</details>

<details>
<summary>Q3. 리더 진입에서 acquire/release 면 충분하지 않나요? 왜 seq_cst 죠?</summary>

진입은 "내 슬롯 store → cur load", writer 는 "cur 교체 → 슬롯 load". 서로 store 한 뒤 상대 것을 load 하는 SB 모양이고, 금지해야 할 결과는 "리더는 옛 포인터 + writer 는 리더 못 봄" → 해제 → UAF. C11 release/acquire 는 store→load 순서를 주지 않는다. arm64 에서 acquire load 는 `ldapr`(RCpc)라 앞의 `stlr` 을 앞지를 수 있고, seq_cst load 는 `ldar`(RCsc)라 못 앞지른다 — 실제 기계어 차이는 한 글자다. 실측: rel/acq 양쪽 1 200 만 라운드 중 2 번 창이 열렸고 seq_cst 는 0. 대안: relaxed + 양쪽 `atomic_thread_fence(seq_cst)`(`dmb ish`), 또는 liburcu memb 처럼 리더 배리어를 없애고 writer 가 `membarrier()` 로 강제. 꼬리: "writer 쪽만 seq_cst 면?" — 실측 `RV=3 WV=0` 에서 3~33 건. SB 는 양쪽 다 막아야 한다.
</details>

<details>
<summary>Q4. rwlock 도 리더끼리는 안 막는데 왜 리더 2 명에서 17 배 느려지죠?</summary>

"안 막는다" 와 "안 부딪친다" 는 다르다. `rdlock` 은 리더 수 카운터를 원자 RMW 로 올리고 `unlock` 이 내린다. 그 카운터가 있는 캐시 라인을 모든 리더가 **쓰기** 때문에 라인이 코어 사이를 오가며(Modified ↔ Invalid) 한 번에 수십~수백 ns 가 든다 — 실측 1 리더 176.9 → 2 리더 10.3 Mops/s. EBR 리더는 자기 라인(128 B 패딩)에만 쓰고 `cur`/`epoch` 은 읽기만 해서 모든 코어가 Shared 로 공유한다 → 10 리더까지 거의 선형 (7709 Mops/s, rwlock 대비 700~1100 배). 같은 이유로 "읽을 때마다 참조 카운트 ++/--" 방식(공유 refcount)도 확장이 안 된다. 꼬리: 슬롯 패딩을 빼면? 슬롯들이 한 라인에 모여 false sharing — ARCH-M08 에서 측정.
</details>

<details>
<summary>Q5. 리더 하나가 읽기 구간에서 멈추면 어떻게 되나요? 어떻게 막죠?</summary>

grace period 가 끝나지 않아 **그 뒤 retire 된 모든 버전** 이 해제되지 않는다 — 실측 200 ms 멈춤에 4 KB 테이블 1 500 개, 6 MB. 대응: (1) limbo 상한 + 역압 — 상한 256 이면 1 MB 에서 멈추고 갱신이 거절된다 (설정은 늦어지지만 메모리는 안전), (2) writer 가 `synchronize` 로 블록, (3) 막는 리더를 찾아 경보 (`ebr_blocker`, 커널의 RCU stall warning), (4) 메모리 상한이 꼭 필요하면 hazard pointer (보호 대상이 "리더가 지금 쥔 포인터" 로 한정 → 미해제 수 ≤ 리더 × HP 수, 대신 리더마다 게시+재확인 비용, LLAT-M03). 근본 원칙은 "읽기 구간은 짧게": 블록·I/O·락 대기를 구간 안에 두지 않는다.
</details>

<details>
<summary>Q6. 커널에서 rcu_read_lock() 은 비선점 커널에서 거의 아무 일도 안 한다던데, 그럼 grace period 는 어떻게 알죠?</summary>

비선점 커널에선 RCU 읽기 구간 안에서 잠들거나 선점되지 않는다는 규칙이 있다. 그래서 어떤 CPU 가 **문맥 전환을 한 번 했다** 는 것 자체가 "그 CPU 의 이전 읽기 구간은 끝났다" 는 증거다 (quiescent state). idle 진입·사용자 모드 실행도 QS. 커널은 CPU 별로 QS 를 기록하고 (Tree RCU 가 계층적으로 집계) 모든 CPU 가 QS 를 지나면 grace period 끝. 이게 Part C 의 QSBR 그대로다 — 리더 비용 0, 대신 "모든 참여자가 주기적으로 QS 를 지난다" 는 가정이 필요하다. 선점형 커널(`PREEMPT_RCU`)은 읽기 구간 안에서 선점될 수 있어서 태스크별 중첩 카운터를 두고 선점된 리더를 따로 추적한다 (세부 구현은 문서 기준, 미검증). 잠들어야 하면 SRCU.
</details>

<details>
<summary>Q7. 펌웨어 제어 루프의 보정 테이블을 런타임에 바꿔야 합니다. RCU 식으로 설계해 보세요. malloc 은 없습니다.</summary>

정적 버퍼 3 개(LIVE + RETIRED 2) + 원자 포인터 `cur` + 게시 카운터 `gp` + 태스크별 `seen`. 제어 태스크: 루프 맨 위에서 `seen = gp`(release), 그 바퀴에서는 `cur` 를 한 번 읽어 그 테이블만 쓴다 (`ldapr`/Cortex-M `ldr; dmb`). 통신 태스크: 새 테이블을 FREE 버퍼에 다 채운 뒤 `cur` 교체 → `gp++`(release) → 옛 버퍼에 `retired_gp` 기록. 모든 태스크의 `seen ≥ retired_gp` 이면 FREE. 버퍼가 없으면 게시를 미루고 "적용 대기" 상태를 호스트에 알린다 (실측: 멈춘 태스크 동안 12 KB 고정, 갱신 1 550 회 거절). 왜 이 설계인가: IRQ 금지 + memcpy 는 지터, seqlock 은 리더 재시도 지터, 뮤텍스는 우선순위 역전(RTOS-M03/M04). 블록되는 태스크는 offline 처리, 체크섬을 테이블에 넣어 플래시에서 읽은 테이블의 무결성도 같이 본다. 꼬리: "단일 코어 Cortex-M 이고 리더가 ISR 뿐이면?" — writer 태스크는 ISR 이 끝나야 실행되므로 교체 직후 ISR 리더는 이미 없다; 남는 위험은 선점된 하위 우선순위 **태스크** 리더뿐이다.
</details>

<details>
<summary>Q8. RCU, seqlock, rwlock 중 무엇을 고를지 기준은?</summary>

| 기준 | RCU/EBR | seqlock (LLAT-M02) | rwlock |
|---|---|---|---|
| 리더 비용·확장 | 최저, 선형 확장 | 낮음, 쓰기 중 재시도 | 공유 RMW → 확장 안 됨 (실측) |
| 리더가 보는 것 | 옛 버전일 수 있음 (최신 아님) | 최신, 일관 | 최신 |
| 쓰기 비용 | 복사 + 할당 + 지연 해제 | 제자리, 싸다 | 제자리 + 리더 대기 |
| 데이터 모양 | 포인터로 갈아끼울 수 있는 구조 (리스트, 트리, 테이블) | 작은 POD 스냅샷 (포인터 따라가기 불가 — 해제 문제) | 아무거나 |
| 메모리 | 최대 (미해제 버전) | 1 벌 | 1 벌 |

"리더가 잠깐 옛 값을 봐도 되는가" 가 첫 질문이다. 라우팅·설정·보정 테이블은 되고, 계좌 잔고 같은 것은 안 된다.
</details>

<details>
<summary>Q9. 이 구현을 어떻게 테스트했나요? "ASan 이 조용했다" 가 증명인가요?</summary>

세 겹이다. (1) **강제 스케줄 단위 테스트**: 리더 핸들 번호로 "들어가서 안 나감" 을 만들고, 해제 여부를 포인터가 아닌 버전 번호 기록으로 CHECK (+1 돌연변이를 매번 잡음). (2) **스트레스 + 독 칠하기**: 해제 직전 `memset(0xA5)` 하고 리더가 체크섬 검사 → 늦은 리더가 있으면 체크섬이 깨지거나 TSan 이 memset 과의 레이스로 보고. (3) **ASan 빌드로 같은 스트레스**: 실행된 접근 중 해제된 블록 접근이 있으면 보고. 증명은 아니다 — 실행되지 않은 스케줄은 못 본다 (+1 돌연변이 스트레스 건수가 실행당 2~18 건인 것이 그 증거). 그래서 (1) 의 결정적 시나리오가 핵심이고, 정확성 논증(5. 풀이 "왜 +2") 이 그 위에 있다. 더 나아가면 모델 검사(CDSChecker, GenMC 류)나 TLA+ 로 상태 공간을 전수 탐색한다 (MCPP-M13 의 작은 탐색기 참고).
</details>

## 9. 추가 연습

### 연습 1 — 읽기 구간 중첩

콜백 안에서 또 설정을 읽는 코드처럼 `ebrn_enter` 가 중첩돼도 되게 하라. 바깥 구간만 슬롯을 바꾸고, 안쪽 `exit` 뒤에도 grace period 는 계속 막혀야 한다.

<details>
<summary>답 보기</summary>

리더 자기만 쓰는 깊이 카운터 (원자일 필요 없음, 캐시 라인 분리): `if (depth++ == 0) ebr_enter(...)`, `if (--depth == 0) ebr_exit(...)`. 안쪽 진입이 슬롯을 새 epoch 으로 덮어쓰면 **바깥 구간이 쥔 옛 포인터 보호가 풀린다** — 그래서 안쪽은 아무것도 안 한다. 테스트: 안쪽만 나간 뒤 슬롯 == `(0<<1)|1`, `try_advance` 실패. 커널 `rcu_read_lock` 도 중첩 가능하고 선점형 RCU 는 이 카운터를 `task_struct` 에 둔다 (문서 기준).
</details>

### 연습 2 — 리더 동적 등록

스레드가 생겼다 없어지는 서버: 슬롯 3 개를 8 스레드가 `ebr_register`(CAS 로 owner 0 → me) / `ebr_unregister` 로 돌려 쓴다. 3 000 회 × 8 스레드 동안 한 슬롯을 두 명이 쥔 적이 없어야 한다.

<details>
<summary>답 보기</summary>

`compare_exchange_strong(owner, 0 → me, acquire)` 로 빈 슬롯을 차지하고, 반납은 **구간 밖에서** `owner = 0`(release). acquire/release 덕에 이전 주인의 슬롯 사용(마지막 `word = 0`)이 새 주인보다 먼저다. 검증은 슬롯별 `inside` 카운터의 `fetch_add` 가 항상 0 을 돌려주는지 + owner 재확인. 꽉 차면 -1 → 호출자가 양보 후 재시도. 실무 함정: 등록 해제 전에 구간 밖인지(슬롯 word == 0) 반드시 확인 — 아니면 그 슬롯을 물려받은 스레드가 남의 epoch 을 덮어써 보호가 사라진다. liburcu 는 `rcu_register_thread()` 가 필수이고, `urcu-bp` 는 이걸 자동으로 한다 (문서 기준).
</details>

### 연습 3 — QSBR offline / online

태스크 2 가 큐에서 오래 블록된다. 블록 전 `qs_offline`, 깨어나서 `qs_online` 하게 하고, offline 동안 다른 태스크만으로 게시·회수가 계속 되는지, online 뒤에는 태스크 2 가 다시 grace period 에 참여하는지 확인하라.

<details>
<summary>답 보기</summary>

offline = `seen = UINT64_MAX`(release) — 최솟값 계산에서 빠진다. online 이 함정이다: "seen 을 쓰고 → cur 를 읽는" 순서가 writer 의 "cur 교체 → seen 읽기" 와 **SB 모양** 이라, release/acquire 면 "writer 는 아직 OFFLINE 을 보고 버퍼를 재사용 + 태스크는 옛 cur 를 읽음" 이 가능하다. 그래서 online 은 `seen` store 를 seq_cst + `atomic_thread_fence(seq_cst)`, writer 의 `qs_poll` 앞에도 seq_cst fence, 교체·`gp` 증가도 seq_cst. liburcu `rcu_thread_online()` 도 내부에 메모리 배리어가 있다 (문서 기준). 테스트 결과: offline 동안 v2~v11 게시 성공, online 직후 v11 을 보고, 그 v11 버퍼가 태스크 2 가 루프를 돌 때까지 묶여 v14 게시가 한 번 `-1`.
</details>

### 연습 4 — 고전 3-bucket EBR

limbo 를 `epoch % 3` 버킷 3 개로 바꿔라: retire 는 `bucket[G % 3]`, epoch 이 `G'` 로 오르는 순간 `bucket[(G' + 1) % 3]` (= `(G' - 2) % 3`) 을 통째로 해제. PRNG 강제 스케줄 3 000 단계 동안 본문 선형 limbo 와 **해제 집합이 매 단계 같아야** 한다.

<details>
<summary>답 보기</summary>

G' 로 오른 순간 안전한 것은 `e ≤ G' - 2`. 그보다 오래된 것은 이전 전진 때 이미 풀렸으므로 버킷 `(G'-2) % 3` 만 비우면 되고, 그 버킷은 바로 다음 epoch `G'+1` 의 retire 가 다시 쓴다 (그래서 3 개면 충분). 장점: 회수가 O(버킷 크기)로 스캔이 없고, epoch 을 2 비트로 저장 가능 (Fraser 2004 의 원래 형태). 단점: 회수가 "전진 시점" 에만 일어나 retire 와 해제 사이가 최대 1 epoch 더 늦을 수 있다 — 본문은 retire 때도 reclaim 을 부른다. 테스트는 선형 limbo 도 전진 시점에만 회수하도록 맞춰 비교했고 불일치 0 (전진 100 회 이상, 항목 500 개 이상).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_san && ./pr_san
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan
```

실제 출력: `16/16 passed` (3 회, exit 0), ASan+UBSan `16/16 passed`, TSan `16/16 passed` 보고 없음.

## 10. 핵심 정리

- RCU = **불변 버전 + 포인터 교체 + grace period 후 해제**. 리더는 기다리지도 재시도하지도 않고, 대가는 writer 의 복사와 지연 해제 메모리.
- EBR: 슬롯 `(e<<1)|1`, "활동 중 슬롯 == G" 일 때만 전진, **`retire_epoch + 2 ≤ G`** 에서 해제. 1 칸 전진은 리더가 아직 쥔 상태에서도 일어난다.
- 리더 진입 = SB 모양 → **seq_cst** (arm64 `stlr; ldar`, Cortex-M `str; dmb sy; ldr`). `ldapr` 면 실측 UAF 창 (1 200 만 중 2). QSBR 의 보고는 acquire/release 로 충분 (online 만 예외).
- 교체 직후 free 는 ASan 없이 **체크섬 맞는 엉뚱한 버전** 을 읽는다 (malloc 재사용). 검증 = 강제 스케줄 + 독 칠하기 + ASan/TSan.
- 실측 (M6, load ≈ 4): EBR 1 → 10 리더 1321 → 7709 Mops/s, rwlock 176.9 → 10 대 (공유 카운터 핑퐁), 700~1100 배 차이.
- 약점: 멈춘 리더 1 명 = 전체 회수 정지 (200 ms × 7.6 k/s × 4 KB ≈ 6 MB). limbo 상한·역압, `blocker` 경보, 또는 hazard pointer(LLAT-M03).
- 커널 대응: `rcu_read_lock/unlock` = enter/exit, `rcu_assign_pointer` = release 교체, `synchronize_rcu` = 기다림(구간 안 호출 = 교착), `call_rcu` = retire, 문맥 전환 = quiescent state.
- 펌웨어: 정적 버퍼 3 개 + "모든 태스크가 루프 맨 위를 지남" (QSBR), 블록되는 태스크는 offline.

## 참고자료

- Paul E. McKenney, Jonathan Walpole, "What is RCU, Fundamentally?" (LWN, 2007) — <https://lwn.net/Articles/262464/>
- Paul E. McKenney, *Is Parallel Programming Hard, And, If So, What Can You Do About It?* (perfbook), 9장 Deferred Processing (참조 카운트·hazard pointer·RCU 비교) — <https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook.html>
- Linux kernel `Documentation/RCU/whatisRCU.rst`, `Documentation/RCU/stallwarn.rst` — <https://www.kernel.org/doc/html/latest/RCU/whatisRCU.html>
- Keir Fraser, *Practical lock-freedom*, PhD thesis, Univ. of Cambridge, 2004 (UCAM-CL-TR-579) — epoch 기반 회수의 원형
- Thomas E. Hart, Paul E. McKenney, Angela Demke Brown, Jonathan Walpole, "Performance of memory reclamation for lockless synchronization", JPDC 2007 (QSBR · EBR · hazard pointer 비교)
- Mathieu Desnoyers et al., "User-Level Implementations of Read-Copy Update", IEEE TPDS 23(2), 2012 — liburcu 방식들. 라이브러리: <https://liburcu.org/>
- Maged M. Michael, "Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects", IEEE TPDS 2004 (5층 LLAT-M03)
- C11 (N1570): 5.1.2.4 (happens-before, release sequence p10), 6.2.4p2 (free 뒤 포인터 값 불확정), 7.17.3 (seq_cst 전순서), 7.17.4 (fence) — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Arm Architecture Reference Manual (DDI 0487) — LDAR/STLR(RCsc) vs LDAPR(RCpc, FEAT_LRCPC), SWPAL(FEAT_LSE): <https://developer.arm.com/documentation/ddi0487/latest/>
- AddressSanitizer 문서 (heap-use-after-free, quarantine): <https://clang.llvm.org/docs/AddressSanitizer.html>
- 관련 레슨: C-A03 (release/acquire 게시, SB 리트머스), ARM-M05 (`stlr;ldapr` SB 실측), ARM-M04 (LSE `swpal`), LLAT-B06 (경합 카운터 비용), MCPP-M13 (C++20 release sequence), OS-M08 (DCLP·게시 패턴), OS-M07 (스핀락), OS-A01 (futex), MCPP-A03 (wait/notify), LLAT-M02 (seqlock 대조), LLAT-M03 (hazard pointer), DS-A04 (ABA), ARCH-M08 (false sharing), C-M14 (ISR ↔ main), RTOS-M03/M04 (우선순위 역전), LPERF-M07 (RCU 식 읽기 경로로 락 경합 개선)
