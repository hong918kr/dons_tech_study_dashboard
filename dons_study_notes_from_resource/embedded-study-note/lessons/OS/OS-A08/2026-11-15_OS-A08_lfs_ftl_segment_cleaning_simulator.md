---
id: OS-A08
time: 110분
updated: 2026-11-15
---
# OS-A08 로그 구조 FS와 FTL: 세그먼트 클리닝 시뮬레이터

## 학습 목표

- 세그먼트 기반 로그 구조 저장소를 직접 만든다. **map(LFS imap / SSD L2P)**, **segment summary(LFS / SSD OOB·P2L)**, **segment usage table(live 수 + mtime)** 세 가지가 서로 어떤 불변식으로 묶이는지 말하고, 그 불변식을 청소할 때마다 검사한다.
- 비트맵 없이 "이 블록이 살아 있나"를 **summary 의 주인 LBA + map 이 여기를 가리키나** 로 판정한다(OSTEP 43.10). 버전 번호가 왜 필요한지는 전원 차단 뒤 재구성(roll-forward / OOB 스캔)으로 보인다.
- greedy, cost-benefit(`(1−u)·age/(1+u)`, Rosenblum & Ousterhout), FIFO 세 victim 정책을 구현한다. 균등·hot-cold·순차 워크로드에서 사용률 u = 0.50..0.90 을 훑어 **쓰기 증폭(WA)** 곡선을 그린다. 균등 워크로드의 측정값은 해석 모델과 1 % 안으로 맞춘다(1024 세그먼트에선 0.3 %).
- LFS 논문의 반직관 세 가지를 실측으로 재현한다. ① greedy 는 지역성이 있으면 오히려 나빠진다. ② cost-benefit 은 균등에선 greedy 보다 나쁘다. ③ cost-benefit 이 청소하는 세그먼트의 사용률은 **두 봉우리(bimodal)** 로 갈린다.
- 결과를 SSD FTL 로 옮겨 말한다. OP 와 WA, TRIM, 클리너 예약과 교착, 호스트 스트림 힌트(Streams/FDP), 마모 편차, 그리고 Linux F2FS 가 실제로 쓰는 정책(6.18.55 소스)까지 다룬다.

## 1. 문제

"SSD 쓰기 증폭을 어떻게 줄이나요?" 는 스토리지·펌웨어 면접에서 시니어를 가르는 질문이다. 꼬리질문은 "OP 를 7 % 에서 28 % 로 늘리면 WA 가 얼마나 줄죠?", "왜 cost-benefit 이죠? 균등 워크로드에서도요?", "TRIM 을 안 보내는 호스트라면?" 으로 이어진다. Don 이 만든 SSD FTL 의 GC 와 1991년 Sprite LFS 의 클리너는 **같은 문제**다. 덮어쓰기가 안 되는 매체(NAND, 또는 "순차로만 쓰겠다"고 정한 디스크)에 쓰려면 새 자리에 쓰고, 옛 자리는 쓰레기로 남기고, 언젠가 살아 있는 것만 옮겨 공간을 되찾아야 한다. 옮기는 양이 곧 비용이다.

이 레슨의 장치는 메모리 안의 시뮬레이터다. 세그먼트 `nseg` 개 × 블록 `S` 개, 논리 블록(LBA) `nlba` 개로 구성되고, 사용률은 `u = nlba / (nseg·S)` 다. 시각 `now` 는 호스트 쓰기 1 번마다 1 씩 오른다. 블록 내용은 `(lba << 32) | version` 이라서, 읽을 때마다 "맞는 LBA 의 최신 버전인가" 를 바로 검사할 수 있다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">로그 구조 저장소 (S = 4, solution.c Part A 테스트의 실제 상태: 쓰기 0,1,2,3,4,1 뒤)</text>
  <text x="10" y="52" fill="currentColor">map[lba] → phys</text>
  <g stroke="currentColor" class="f-mut">
    <rect x="10" y="62" width="34" height="26"/><rect x="44" y="62" width="34" height="26"/><rect x="78" y="62" width="34" height="26"/>
    <rect x="112" y="62" width="34" height="26"/><rect x="146" y="62" width="34" height="26"/><rect x="180" y="62" width="34" height="26"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="27" y="80">0</text><text x="61" y="80">5</text><text x="95" y="80">2</text><text x="129" y="80">3</text><text x="163" y="80">4</text><text x="197" y="80">-</text>
    <text x="27" y="104" font-size="12">0</text><text x="61" y="104" font-size="12">1</text><text x="95" y="104" font-size="12">2</text><text x="129" y="104" font-size="12">3</text><text x="163" y="104" font-size="12">4</text><text x="197" y="104" font-size="12">5</text>
  </g>
  <text x="10" y="124" fill="currentColor" font-size="12">아래 숫자 = lba. LFS 의 imap, SSD 의 L2P</text>
  <text x="10" y="142" fill="currentColor" font-size="12">예: map[1] = 5 → seg1 의 둘째 슬롯 (1 v2)</text>
  <text x="300" y="52" fill="currentColor" font-weight="bold">세그먼트 (phys = seg·S + off)</text>
  <text x="300" y="84" fill="currentColor">seg0</text>
  <g stroke="currentColor">
    <rect x="345" y="66" width="44" height="28" class="f-ok"/><rect x="389" y="66" width="44" height="28" class="f-warn"/>
    <rect x="433" y="66" width="44" height="28" class="f-ok"/><rect x="477" y="66" width="44" height="28" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="351" y="85">0 v1</text><text x="395" y="85">1 v1</text><text x="439" y="85">2 v1</text><text x="483" y="85">3 v1</text>
  </g>
  <text x="530" y="78" fill="currentColor" font-size="12">FULL · live 3</text>
  <text x="530" y="94" fill="currentColor" font-size="12">mtime 4 · victim 후보</text>
  <text x="300" y="134" fill="currentColor">seg1</text>
  <g stroke="currentColor">
    <rect x="345" y="116" width="44" height="28" class="f-ok"/><rect x="389" y="116" width="44" height="28" class="f-ok"/>
    <rect x="433" y="116" width="44" height="28" class="f-mut"/><rect x="477" y="116" width="44" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12"><text x="351" y="135">4 v1</text><text x="395" y="135">1 v2</text></g>
  <text x="530" y="128" fill="currentColor" font-size="12">OPEN (호스트 헤드)</text>
  <text x="530" y="144" fill="currentColor" font-size="12">live 2 · mtime 6 · wp 2</text>
  <text x="300" y="184" fill="currentColor">seg2..7</text>
  <rect x="345" y="166" width="176" height="28" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="355" y="185" fill="currentColor" font-size="12">FREE — FIFO 링, nfree = 6</text>
  <rect x="10" y="220" width="350" height="100" fill="none" stroke="currentColor"/>
  <text x="20" y="242" fill="currentColor" font-weight="bold">segment summary (슬롯마다 주인 lba, 버전)</text>
  <text x="20" y="264" fill="currentColor" font-size="12">phys 1 의 summary 는 여전히 "lba 1 v1" 이다.</text>
  <text x="20" y="282" fill="currentColor" font-size="12">live 판정: map[1] == 5 ≠ 1 → 죽음 (비트맵 불필요)</text>
  <text x="20" y="300" fill="currentColor" font-size="12">SSD: 페이지 OOB 영역 또는 블록 끝 P2L 페이지</text>
  <rect x="380" y="220" width="350" height="100" fill="none" stroke="currentColor"/>
  <text x="390" y="242" fill="currentColor" font-weight="bold">segment usage table (세그먼트마다)</text>
  <text x="390" y="264" fill="currentColor" font-size="12">live = 살아 있는 블록 수 (u = live / S)</text>
  <text x="390" y="282" fill="currentColor" font-size="12">mtime = 담긴 데이터 중 가장 최근 쓰기 시각</text>
  <text x="390" y="300" fill="currentColor" font-size="12">age = now − mtime → cost-benefit 의 재료</text>
  <text x="10" y="350" fill="currentColor">범례: 초록 = live, 경고색 = 죽은 블록(덮어써짐), 회색 = 아직 안 쓴 슬롯</text>
  <text x="10" y="372" fill="currentColor">클리너: victim 의 live 만 다른 헤드로 복사 → victim 을 지움 (SSD: valid page 복사 → block erase)</text>
  <text x="10" y="392" fill="currentColor">WA = (호스트 쓰기 + 클리너 복사) / 호스트 쓰기</text>
</svg>
```

| LFS (Rosenblum & Ousterhout) | SSD FTL | 이 레슨의 코드 |
|---|---|---|
| inode map (imap) | L2P 매핑 테이블 | `map[lba]` |
| segment (수백 KB~MB) | 블록 / 슈퍼블록 (여러 다이 묶음) | `S` 블록짜리 세그먼트 |
| segment summary block | OOB 영역, P2L 페이지 | `sum_lba[]`, `sum_ver[]` |
| segment usage table | 블록별 valid count (+ 시각) | `live[]`, `mtime[]` |
| cleaner | garbage collection | `clean_pass()` |
| 세그먼트 재사용 | block erase | `seg_erase()` |
| checkpoint + roll-forward | 맵 플러시 + 전원 차단 뒤 OOB 스캔 | `lfs_rebuild_map()` |
| 디스크 사용률 u | `1 / (1 + OP)` 근처 (OP = 여유 / 사용자 용량) | `nlba / (nseg·S)` |

**DS-A09 와의 경계.** DS-A09 는 FTL 의 **자료구조**를 다뤘다. indexed heap 으로 victim 을 찾고, GC 복사와 호스트 쓰기의 경합은 조건부 커밋으로 막고, 워터마크·페이싱을 두고, OP 별 WAF 를 Desnoyers 해석식과 맞췄다. 이 레슨은 **OS(파일 시스템) 쪽 시각**이다. 비트맵 없이 summary 로 판정하고, 데이터 나이를 쓰는 정책과 LFS 논문의 실험을 재현하고, 사용률 스윕과 TRIM·교착·재구성을 본다. victim 은 O(nseg) 선형 스캔으로 찾는다(세그먼트 수백 개면 충분히 싸다. 큰 장치의 O(log n) 구조는 DS-A09).

### Part A — 로그에 붙이기: map · summary · usage table

- `block_is_live(L, phys)`: summary 가 말하는 주인 `lba` 의 `map[lba] == phys` 인가.
- `log_append(L, &head, cleaner, lba, ver, btime)`: 헤드가 없거나 닫혔으면 free 링에서 새 세그먼트를 열고(호스트는 `reserve` 를 남겨야 함), 슬롯 하나에 내용·summary·usage·map 을 기록한다(`put_slot` 은 주어짐).
- `lfs_write(L, lba)`: 헤드가 닫혔으면 (필요하면 청소한 뒤) 새로 열고, **그 다음에** 옛 자리의 live 를 하나 내리고, 버전을 올려 붙인다. 공간이 없으면 `-1` 이고 옛 데이터는 그대로 남는다.
- 테스트: 작은 장치(8 세그먼트 × 4 블록, lba 16 개)에 2 000 번 무작위로 쓰면서 **쓰기마다** 독립 검사기 `lfs_check` 를 돌린다. 끝나면 모든 LBA 를 읽어 기대 버전과 대조한다.

### Part B — 클리너: greedy vs cost-benefit, 워터마크, WA

- `pick_victim`: 후보는 닫힌(FULL) 세그먼트 중 `live < S` 인 것. greedy 는 live 최소, cost-benefit 은 `(S−live)·age/(S+live)` 최대, FIFO 는 가장 먼저 열린 것.
- `clean_pass`: victim 을 `batch` 개까지 고른다(클리너가 쓸 자리 `room` 이 모자라면 덜 고른다). live 블록을 모으고, `age_sort` 면 데이터 나이순으로 정렬하고, 클리너 헤드(`separate`)나 호스트 헤드로 복사한 뒤 victim 을 지운다. victim 사용률 히스토그램도 센다.
- `maybe_clean`: `nfree < wm_low` 에서 시작해 `nfree ≥ wm_high` 까지 청소한다(히스테리시스).
- `stats_wa`, `stats_segs_per_gib`: WA 와 "호스트 1 GiB 당 지운 세그먼트 수".
- 테스트: 손으로 만든 세그먼트 상태에서 세 정책의 선택을 확인한다. 순차 워크로드는 WA = 1.000 정확히. 지울 때마다 불변식을 검사한다. 균등 워크로드 WA 는 해석 모델 ±3 % 안이어야 한다. hot-cold 에선 cost-benefit < greedy, 그리고 greedy(hot-cold) > greedy(균등).

### Part C — FTL 로 읽기: summary 스캔 재구성, 마모, Don 의 SSD

- `lfs_rebuild_map`: map 이 없다고 가정한다(전원 차단 뒤 마운트). FREE 가 아닌 세그먼트의 summary 만 훑어 LBA 마다 **버전이 가장 큰** 슬롯을 고르고, live 수도 다시 센다. 결과는 실제 map·usage 와 같아야 한다.
- 대조 실험: 버전 없이 "물리 주소가 뒤인 슬롯이 이긴다" 로 다시 만들면 틀린 LBA 가 생긴다(세그먼트를 재사용하므로 물리 순서 ≠ 시간 순서).
- 마모: 세그먼트별 erase 횟수의 최소·최대·평균. 정책마다 크게 다르다(§5).

## 2. Problem (English)

> **Segment cleaning in a log-structured store (LFS cleaner = SSD FTL garbage collector).**
>
> A device has `nseg` segments of `S` blocks. Logical blocks `0..nlba-1` are never overwritten in place: every write appends to an open "head" segment, records `(lba, version)` in that slot's segment summary, updates `map[lba]`, and kills the previous copy. A per-segment usage table holds the live count and `mtime` (the most recent host write time of any data in the segment).
>
> A. Implement `block_is_live(phys)` using only the summary and the map, `log_append`, and `lfs_write(lba)` (returns -1 when no space; the old data must survive a failed write). The host may open a new segment only while `nfree > reserve`.
>
> B. Implement victim selection — greedy (min live), cost-benefit (max `(1-u)·age/(1+u)`, `age = now - mtime`), FIFO — skipping open and completely-live segments; a cleaning pass that copies live blocks (optionally age-sorted, optionally to a separate cleaner head) and erases the victims; and a free-segment watermark trigger. Report `WA = (host + copied) / host` and segments cleaned per GiB of host writes.
>
> C. Rebuild the map from segment summaries alone (power-loss mount) and show why a version number is required.
>
> Measure WA vs utilization (0.50–0.90) for uniform, hot-cold (90 % of writes to 10 % of LBAs) and sequential workloads. For uniform writes, compare against analytic models. Expected behaviour: sequential WA = 1.0 exactly; cost-benefit beats greedy on hot-cold but not on uniform.
>
> Follow-ups: Why is LFS write cost `2/(1-u)` but SSD WA `1/(1-u)`? Can the cleaner deadlock? What does TRIM buy you, and what breaks if TRIM is not logged? How would you place data if the host could tell you hot from cold?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.1 초 안에 끝나고 exit 1):

```text
-- Part A: out-of-place append, segment summary, liveness
PASS lfs_init(&L, &c) == 0 && L.nfree == 8 && lfs_read(&L, 0, &v) == -1
FAIL starter.c:386  ok && L.hhead == 0 && L.map[0] == 0 && L.map[3] == 3 && L.live[0] == 4 && L.state[0] == SEG_FULL
FAIL starter.c:387  L.sum_lba[2] == 2 && lfs_read(&L, 2, &v) == 0 && v == stamp(2, 1)
...
-- Part B: victim policies, cleaner, WA
FAIL starter.c:438  pick_on(POL_GREEDY) == 1
...
  uniform u=0.80 FIFO  : WA 0.0000  model 2.7319 (alpha_eff 1.2451, mean free 0.00)
FAIL starter.c:493  approx(r.wa, mf, 0.03)
...
-- Part C: rebuild the map from segment summaries (roll-forward / OOB scan)
  rebuilt -1 lbas, stale summary slots 0, naive physical-order rebuild wrong for 0 lbas
FAIL starter.c:555  sum == L.erase_total && sum > 0 && mx >= mn
8/40 passed
```

주어진 것: `lfs_init/lfs_free`, free 링(`seg_take` — 호스트는 reserve 를 남김), `seg_erase`(지울 때 live 가 남아 있으면 `lost_live` 로 셈, `check_on_erase` 훅), `put_slot`(슬롯 하나 기록), `lfs_read`, 독립 검사기 `lfs_check`, 워크로드 생성기(`xorshift32`), `run_sim`(순차 prefill → warm-up → 지표 리셋 → 측정 → 전체 읽기 대조), 해석 모델(`model_fifo`, `model_greedy`, `eff_alpha`). 스텁은 실패 값(`-1`, `NONE`, `false`, `0.0`)을 돌려주므로 쓰기가 하나도 안 되고, `run_sim` 은 첫 쓰기에서 바로 빠져나온다(멈추지 않는다).

| TODO | 함수 | 핵심 |
|---|---|---|
| A1 | `block_is_live` | summary 주인 + map 대조 |
| A2 | `log_append` | 헤드 열기 (호스트/클리너 권한), `put_slot` |
| A3 | `lfs_write` | 청소 → 옛 자리 죽이기 → 붙이기, 실패 시 무손상 |
| B1 | `pick_victim` | 세 정책, 정수 비교, 후보 제외 규칙 |
| B2 | `clean_pass` | room 계산, 모으기·정렬·복사·지우기, 히스토그램 |
| B3 | `maybe_clean` | 워터마크 히스테리시스, 진전 없음 감지 |
| B4 | `stats_wa`, `stats_segs_per_gib` | 지표 |
| C1 | `lfs_rebuild_map` | 버전 최대 슬롯, 모호하면 -1 |

관찰용 데모 (엔진은 `solution.c` 를 `LFS_NO_MAIN` 으로 포함):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o demo_sweep && ./demo_sweep   # T1..T5 표, 이 맥에서 5.2 초
```

## 4. 힌트

<details><summary>힌트 1 — 비트맵 없이 liveness</summary>

슬롯에는 "나는 lba X 의 버전 v 였다" 가 영원히 남아 있다(지워질 때까지). 그 블록이 지금도 X 의 최신 사본인지는 map 만 보면 안다. `map[X]` 가 정확히 이 phys 를 가리키면 live, 다른 곳을 가리키면 죽은 것이다. OSTEP 43.10 의 의사 코드가 이 한 줄이다. usage table 의 `live[]` 는 "몇 개" 만 알려 주는 요약이고, "어느 것" 은 이 판정으로 안다.
</details>

<details><summary>힌트 2 — lfs_write 의 순서</summary>

헤드가 닫혔으면 새 세그먼트가 필요하다. 그런데 free 가 워터마크 아래면 먼저 청소해야 한다. 청소는 지금 쓰려는 lba 의 **옛 사본도 옮길 수 있다**(아직 live 니까). 그러니 `old = map[lba]` 는 청소 **뒤에** 읽어야 한다. 청소 전에 읽은 `old` 의 세그먼트는 이미 지워졌을 수 있다. 또 하나, 새 자리를 확보하지 못했으면 옛 사본을 죽이기 전에 `-1` 로 돌아가야 데이터가 남는다.
</details>

<details><summary>힌트 3 — cost-benefit 을 부동소수 없이</summary>

`(S−a)·age_a/(S+a) > (S−b)·age_b/(S+b)` 의 양변에 `(S+a)(S+b)` 를 곱하면 `(S−a)·age_a·(S+b) > (S−b)·age_b·(S+a)` 다. `S ≤ 4096`, `age < 2^38` 이면 64 비트에 들어간다. 꽉 찬 세그먼트(`live == S`)는 점수가 0 이지만 어떤 정책에서도 후보에서 빼야 한다. 옮겨도 공간이 하나도 안 생긴다. 헤드(OPEN)도 빼야 한다. 지금 쓰고 있는 곳을 지우게 된다.
</details>

<details><summary>힌트 4 — 청소 패스가 쓸 자리 (room)</summary>

클리너는 reserve 까지 쓸 수 있으니 `room = nfree·S + (클리너 헤드의 남은 칸)` 이다. victim 을 고를 때마다 "지금까지 모은 live + 이 victim 의 live ≤ room" 인지 확인한다. 고른 victim 은 상태를 `SEG_VICTIM` 으로 바꿔 다음 pick 에서 빠지게 한다. 복사할 때마다 victim 의 `live` 를 하나씩 내리면 지울 때 0 이어야 한다(`seg_erase` 가 0 이 아니면 `lost_live` 로 센다). batch 에서 정렬하면 victim 들의 블록이 섞이니, **모두 복사한 뒤에** 지운다.
</details>

<details><summary>힌트 5 — 워터마크와 "진전 없음"</summary>

`nfree < wm_low` 일 때만 시작하고 `nfree ≥ wm_high` 까지 돈다. 패스가 `-1`(victim 없음 또는 room 부족)이거나 `nseg` 번 돌아도 안 되면 멈춘다. 그때 호스트가 아직 세그먼트를 열 수 있으면(`nfree > reserve`) 0, 아니면 -1 이다. `wm_low ≤ reserve` 로 설정하면 어떻게 될까? 호스트는 막혔는데 청소는 시작되지 않는다(연습 2).
</details>

<details><summary>힌트 6 — 재구성: 왜 버전이 필요한가</summary>

같은 lba 의 사본이 여러 세그먼트에 남아 있을 수 있다(죽은 사본은 그 세그먼트가 지워질 때까지 남는다). 어느 것이 최신인지 물리 주소 순서로는 알 수 없다. free 링이 세그먼트를 돌려 쓰므로 seg 3 이 seg 40 보다 나중에 쓰였을 수 있다. 쓰기마다 오르는 버전(또는 전역 시퀀스 번호)이 있어야 "가장 큰 것이 최신" 이 된다. 클리너 복사본은 버전을 그대로 들고 간다. 원본은 복사 후 곧 지워지므로 같은 버전이 두 번 보이면 모호한 상태(-1)다.
</details>

## 5. 풀이

### Part A — liveness: summary 가 말하는 주인 + map

```c
static bool block_is_live(const lfs_t *L, uint32_t phys) {
    uint32_t lba = L->sum_lba[phys];
    return lba != NONE && L->map[lba] == phys;
}
```

이 판정 하나로 세 가지가 해결된다. ① 클리너는 victim 의 각 슬롯이 살았는지 안다. ② 비트맵을 따로 유지하지 않아도 된다. 비트맵은 같은 사실의 캐시일 뿐이다(DS-A09 Q2·연습 4 와 같은 결론). ③ 덮어쓸 때 옛 슬롯을 건드리지 않는다. 옛 summary 는 그대로 두고 map 만 바꾸면 그 슬롯은 자동으로 죽는다. 실제 LFS 는 summary 에 (inode 번호, 파일 내 오프셋) 을 적고, map 대신 inode 의 블록 포인터와 대조한다. imap 의 버전 번호가 다르면(파일이 지워졌다 다시 생김) inode 를 읽지도 않고 죽은 것으로 친다(OSTEP 43.10, 문서 기준).

Part A 테스트의 상태를 따라가면 (그림 1 과 같다):

| 연산 | 헤드 | map | live | 비고 |
|---|---|---|---|---|
| write 0..3 | seg0 (꽉 차서 FULL) | 0→0, 1→1, 2→2, 3→3 | seg0 = 4 | btime 1..4, mtime[0] = 4 |
| write 4 | seg0 닫힘 → seg1 | 4→4 | seg1 = 1 | 새 세그먼트를 열어 nfree = 6 |
| write 1 | seg1 | 1→5 | seg0 = 3, seg1 = 2 | phys 1 의 summary 는 "1 v1" 그대로, `block_is_live(1) == false` |

### Part A — 쓰기 순서: 청소가 먼저, 옛 자리는 그 뒤에 읽는다

```c
if (헤드가 닫힘) { hhead = NONE; if (maybe_clean(L) < 0) return -1;
                   if (아직 헤드 없음) { s = seg_take(L, false); if (s == NONE) return -1; hhead = s; } }
uint32_t old = L->map[lba];               /* 청소 "뒤" */
if (old != NONE) L->live[seg_of(L, old)]--;
put_slot(L, L->hhead, lba, ++L->ver[lba], ++L->now);
```

`separate = false` 면 클리너가 호스트 헤드에 복사하므로, 청소가 끝났을 때 헤드가 이미 열려 있을 수 있다(클리너 데이터로 반쯤 찬 상태). 호스트는 그 뒤에 이어 쓴다. 이것이 "1 스트림" 이다. `old` 를 청소 전에 읽으면, 청소가 그 사본을 옮기고 원래 세그먼트를 지운 경우 **이미 FREE 인 세그먼트의 live 를 내리게** 된다. 검사기가 `cnt[s] != live[s]` 로 잡는다(§7 실수 1).

### Part B — victim 정책 세 가지

cost-benefit 의 유도는 LFS 논문 그대로다. 세그먼트 하나를 청소하는 **비용**은 세그먼트를 읽는 1 과 live 를 다시 쓰는 u, 합쳐 `1 + u` 다. **이득**은 생기는 빈 공간 `1 − u` 다. 그런데 그 공간이 얼마나 오래 "비어 있는 채로 유지될지" 를 데이터 나이로 가중한다. 오래 안 바뀐 데이터(차가운 데이터)는 앞으로도 안 바뀔 테니, 지금 옮겨 두면 그 공간 이득이 오래간다. 반대로 뜨거운 세그먼트는 기다리면 저절로 더 비워진다.

```text
benefit / cost = (1 − u) · age / (1 + u)        age = now − mtime (세그먼트에서 가장 최근 데이터의 나이)
```

손으로 만든 상태 (`S = 100`, `now = 1000`) — 테스트 `pick_on()`:

| seg | 상태 | live (u) | mtime | 열린 순번 | greedy | cost-benefit 점수 | FIFO |
|---|---|---|---|---|---|---|---|
| 0 | FULL | 75 (0.75) | 0 | 3 | | 25·1000/175 = **142.9** ← | |
| 1 | FULL | 50 (0.50) | 900 | 4 | **최소** ← | 50·100/150 = 33.3 | |
| 2 | FULL | 100 (1.00) | 0 | 1 | 제외 (꽉 참) | 제외 | 제외 |
| 3 | OPEN | 10 | 999 | 6 | 제외 (헤드) | 제외 | 제외 |
| 5 | FULL | 95 (0.95) | 10 | 2 | | 5·990/195 = 25.4 | **가장 먼저** ← |

cost-benefit 은 u 0.75 인 차가운 세그먼트를 u 0.50 인 뜨거운 것보다 먼저 고른다. greedy 라면 상상도 못 할 선택이다. 이 선택이 왜 이득인지는 아래 측정이 보여 준다.

**Linux F2FS 는 실제로 둘 다 쓴다.** Linux 6.18.55 `fs/f2fs/gc.c:244-270` `select_gc_type()` 은 백그라운드 GC(`BG_GC`)에는 `GC_CB`(또는 ATGC 가 켜져 있으면 `GC_AT`)를, 공간이 급한 포그라운드 GC 에는 `GC_GREEDY` 를 고른다. 비용 함수는 `gc.c:358-384` `get_cb_cost()` 이다. u 와 age 를 둘 다 0..100 으로 정규화한다(age 는 전체 min/max mtime 사이의 상대 위치). 그다음 `return UINT_MAX - ((100 * (100 - u) * age) / (100 + u));` (`gc.c:383`) 로 같은 식을 "작을수록 좋은 비용" 으로 뒤집는다. 또 `fs/f2fs/f2fs.h:1126-1131` 의 로그 종류에서 `CURSEG_COLD_DATA` 의 주석은 "multimedia or GCed data blocks" 이다. **GC 로 옮긴 데이터를 차가운 로그에 따로 쓴다** — 이 레슨의 `separate` 옵션과 같은 생각이다.

### Part B — 청소 패스

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a8m" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">clean_pass() 한 번 (batch = 2, separate, age_sort)</text>
  <rect x="10" y="40" width="150" height="70" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor">① pick_victim × 2</text>
  <text x="20" y="82" fill="currentColor" font-size="12">고른 것 = SEG_VICTIM</text>
  <text x="20" y="100" fill="currentColor" font-size="12">Σlive ≤ room 일 때만</text>
  <rect x="195" y="40" width="160" height="70" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="205" y="62" fill="currentColor">② 슬롯마다</text>
  <text x="205" y="82" fill="currentColor" font-size="12">block_is_live(p) 면</text>
  <text x="205" y="100" fill="currentColor" font-size="12">gather[] 에 (btime, p)</text>
  <rect x="390" y="40" width="150" height="70" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="400" y="62" fill="currentColor">③ age_sort</text>
  <text x="400" y="82" fill="currentColor" font-size="12">btime 오름차순 →</text>
  <text x="400" y="100" fill="currentColor" font-size="12">나이 비슷한 것끼리</text>
  <rect x="575" y="40" width="155" height="70" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="585" y="62" fill="currentColor">④ 클리너 헤드로 복사</text>
  <text x="585" y="82" fill="currentColor" font-size="12">버전·btime 그대로</text>
  <text x="585" y="100" fill="currentColor" font-size="12">victim live−−, map 갱신</text>
  <path d="M160 75 L192 75" stroke="currentColor" marker-end="url(#a8m)"/>
  <path d="M355 75 L387 75" stroke="currentColor" marker-end="url(#a8m)"/>
  <path d="M540 75 L572 75" stroke="currentColor" marker-end="url(#a8m)"/>
  <rect x="390" y="150" width="340" height="70" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="400" y="172" fill="currentColor">⑤ 복사가 다 끝나면 seg_erase</text>
  <text x="400" y="192" fill="currentColor" font-size="12">live 가 0 이 아니면 lost_live (데이터 유실)</text>
  <text x="400" y="210" fill="currentColor" font-size="12">summary 비움 → FREE 링 뒤에, erases++</text>
  <path d="M652 110 L652 147" stroke="currentColor" marker-end="url(#a8m)"/>
  <rect x="10" y="150" width="345" height="70" rx="4" fill="none" stroke="currentColor"/>
  <text x="20" y="172" fill="currentColor" font-size="12">지표: victim 마다 hist[live·10/S]++</text>
  <text x="20" y="192" fill="currentColor" font-size="12">victim_live += live, 복사마다 clean_w++</text>
  <text x="20" y="210" fill="currentColor" font-size="12">끝나면 clean_w == victim_live 여야 함</text>
</svg>
```

**왜 reserve 1 이면 교착이 없나.** batch = 1 이면 victim 의 live 는 `S − 1` 이하라 복사에 세그먼트가 최대 1 개 필요하고, 끝나면 1 개가 돌아온다(순증 ≥ 0). 호스트는 `nfree > reserve` 일 때만 꺼내므로 청소가 시작될 때 `nfree ≥ reserve ≥ 1` 이다. 그리고 `nlba` 가 데이터를 담을 수 있는 칸보다 작으면 `live < S` 인 FULL 세그먼트가 늘 하나는 있다(비둘기집). 그래서 첫 victim 은 반드시 들어간다. batch > 1 은 "room 이 허락하는 만큼만" 고르니 같은 논리가 유지된다. reserve = 0 이면 호스트가 마지막 free 세그먼트까지 먹고, 클리너는 쓰레기가 산더미인데도 옮길 자리가 없어 멈춘다(연습 2: 쓰기 #32 에서 `-1`). DS-A09 Part C 의 예약 블록 증명과 같은 이야기를 세그먼트 단위로 한 것이다.

### Part B — WA 와 해석 모델 (균등 랜덤)

정상 상태에서 지운 세그먼트 하나가 내놓는 빈 칸은 `S·(1−u_v)` 이고 그만큼 호스트가 쓴다(u_v = victim 사용률). 그래서

```text
WA = (호스트 + 복사) / 호스트 = Σ S / Σ S(1−u_v) = 1 / (1 − ū)         ū = victim 사용률의 평균
LFS write cost = (세그먼트 읽기 + 복사 + 새 데이터) / 새 데이터 = (1 + u + (1−u)) / (1−u) = 2 / (1 − u)
```

LFS 논문의 `2/(1−u)` 는 디스크 시절이라 **victim 을 통째로 읽는 비용**까지 센 값이다. SSD 의 WA 는 NAND **쓰기**만 세므로 `1/(1−u)` 다. 그리고 둘 다 **디스크 사용률이 아니라 victim 의 사용률** 이다(연습 5: 디스크 u 0.80 에서 victim ū = 0.628, write cost 5.374 = 2/(1−0.628)). 디스크 u 와 victim ū 를 잇는 것이 정책과 워크로드다.

FIFO 는 손으로 풀 수 있다. α = T/U (물리/논리 블록). 한 세그먼트가 쓰인 뒤 로그가 한 바퀴 돌아 다시 victim 이 될 때까지 호스트 쓰기는 `T(1−ū)` 번이다. 각 블록이 그동안 한 번도 안 덮어써질 확률은 `(1−1/U)^{T(1−ū)} ≈ e^{−α(1−ū)}` 이다. 그래서 `ū = e^{−α(1−ū)}` 를 이분법으로 풀면 된다. Desnoyers 2012 의 Eq.4(`A = α/(α + W(−α e^{−α}))`)와 같은 식이다. greedy 는 Desnoyers 의 Eq.11–12 를 썼다. 식은 DS-A09 `demo_waf.c` 에서 측정과 0.5 % 안으로 맞춰 본 것을 그대로 옮겼다. T 에는 실제로 데이터를 담지 않는 칸을 뺀다: 평균 free 세그먼트, 열린 헤드 반 개씩(`eff_alpha`).

테스트 출력 (256 × 64, u = 0.80, 실제):

```text
  uniform u=0.80 FIFO  : WA 2.7743  model 2.7940 (alpha_eff 1.2378, mean free 1.50)
  uniform u=0.80 greedy: WA 2.6809  model 2.6918
```

### Part B — 사용률 스윕 (demo_sweep.c T1·T2)

1024 세그먼트 × 64 블록(256 MiB 상당), warm-up 4×nlba, 측정 8×nlba 호스트 쓰기. 모두 정수 시뮬레이션에 고정 시드라 **숫자는 기계 부하와 무관하게 똑같이 나온다**. 실행 시간만 부하를 탄다: 5.2 초, `uptime` load 11.07.

| u | FIFO | 모델 | greedy | 모델 | c-b | greedy 세그먼트/GiB | hot-cold greedy | hot-cold c-b | c-b/greedy |
|---|---|---|---|---|---|---|---|---|---|
| 0.50 | 1.257 | 1.257 | 1.242 | 1.241 | 1.276 | 5 087 | 1.324 | 1.279 | 0.966 |
| 0.60 | 1.484 | 1.484 | 1.459 | 1.459 | 1.519 | 5 975 | 1.683 | 1.345 | 0.799 |
| 0.70 | 1.885 | 1.885 | 1.841 | 1.842 | 1.953 | 7 539 | 2.070 | 1.502 | 0.725 |
| 0.75 | 2.211 | 2.215 | 2.152 | 2.153 | 2.313 | 8 817 | 2.373 | 1.693 | 0.713 |
| 0.80 | 2.716 | 2.717 | 2.620 | 2.621 | 2.860 | 10 733 | 2.825 | 2.041 | 0.723 |
| 0.85 | 3.564 | 3.565 | 3.394 | 3.395 | 3.787 | 13 904 | 3.564 | 2.578 | 0.723 |
| 0.90 | 5.273 | 5.291 | 4.897 | 4.912 | 5.623 | 20 056 | 4.907 | 3.559 | 0.725 |

(0.55·0.65 줄은 데모 출력에 있고 그림에 썼다. 균등 열은 `separate`, 정렬 없음, batch 1. hot-cold 열은 `separate` + `age_sort`, batch 4.) 세그먼트/GiB 는 `S = 64 × 4 KiB = 256 KiB` 세그먼트 기준이다. u 0.80 에서 호스트 1 GiB 마다 1 만 번 넘게 지운다는 뜻이고, 이 숫자가 곧 NAND 수명 소모 속도다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">WA vs 사용률 u — demo_sweep.c T1/T2 실측 (1024 × 64, 고정 시드)</text>
  <path d="M80 300 L700 300 M80 300 L80 40" stroke="currentColor"/>
  <path d="M80 250 L700 250 M80 200 L700 200 M80 150 L700 150 M80 100 L700 100 M80 50 L700 50" stroke="currentColor" stroke-dasharray="2 4" fill="none" opacity="0.5"/>
  <g fill="currentColor">
    <text x="50" y="304">1.0</text><text x="50" y="254">2.0</text><text x="50" y="204">3.0</text><text x="50" y="154">4.0</text><text x="50" y="104">5.0</text><text x="50" y="54">6.0</text>
    <text x="70" y="318">0.50</text><text x="225" y="318">0.60</text><text x="380" y="318">0.70</text><text x="535" y="318">0.80</text><text x="690" y="318">0.90</text>
    <text x="360" y="334">디스크 사용률 u</text>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="80.0,287.9 157.5,283.2 235.0,277.1 312.5,268.9 390.0,257.9 467.5,242.4 545.0,219.0 622.5,180.3 700.0,105.1"/>
  <polyline fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="7 4" points="80.0,286.2 157.5,280.9 235.0,274.1 312.5,264.8 390.0,252.3 467.5,234.3 545.0,207.0 622.5,160.7 700.0,68.8"/>
  <polyline fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="2 3" points="80.0,287.1 157.5,282.2 235.0,275.8 312.5,267.3 390.0,255.8 467.5,239.5 545.0,214.2 622.5,171.8 700.0,86.4"/>
  <g class="f-warn" stroke="currentColor">
    <circle cx="80.0" cy="283.8" r="3.5"/><circle cx="157.5" cy="275.8" r="3.5"/><circle cx="235.0" cy="265.9" r="3.5"/><circle cx="312.5" cy="256.1" r="3.5"/><circle cx="390.0" cy="246.5" r="3.5"/><circle cx="467.5" cy="231.3" r="3.5"/><circle cx="545.0" cy="208.8" r="3.5"/><circle cx="622.5" cy="171.8" r="3.5"/><circle cx="700.0" cy="104.7" r="3.5"/>
  </g>
  <g class="f-ok" stroke="currentColor">
    <rect x="76.5" y="282.6" width="7" height="7"/><rect x="154.0" y="281.3" width="7" height="7"/><rect x="231.5" y="279.3" width="7" height="7"/><rect x="309.0" y="276.3" width="7" height="7"/><rect x="386.5" y="271.4" width="7" height="7"/><rect x="464.0" y="261.9" width="7" height="7"/><rect x="541.5" y="244.4" width="7" height="7"/><rect x="619.0" y="217.6" width="7" height="7"/><rect x="696.5" y="168.6" width="7" height="7"/>
  </g>
  <rect x="100" y="48" width="300" height="104" fill="none" stroke="currentColor" opacity="0.6"/>
  <g fill="currentColor">
    <text x="110" y="66">굵은 실선: greedy, 균등 (모델과 0.3 % 안)</text>
    <text x="110" y="84">파선: cost-benefit, 균등 (greedy 보다 나쁨)</text>
    <text x="110" y="102">점선: FIFO, 균등</text>
    <text x="110" y="120">● greedy, hot-cold 90/10 (균등보다 나쁨!)</text>
    <text x="110" y="138">■ cost-benefit, hot-cold (u ≥ 0.7 에서 −28 %)</text>
  </g>
</svg>
```

모양을 읽는 법: 곡선은 `1/(1−ū)` 꼴이라 u 가 0.8 을 넘으면 가파르게 선다. SSD 에서 "OP 7 % 와 28 % 의 차이" 가 큰 이유다(u ≈ 0.93 vs 0.78 근처). 그 구간에서 정책 차이(hot-cold 에서 −28 %)는 OP 를 몇 % 더 주는 것과 맞먹는다.

### Part B — LFS 논문의 반직관 세 가지 (T2·T3·T4)

1. **greedy 는 지역성이 있으면 오히려 나빠진다.** u 0.80 에서 greedy 는 균등 2.620, hot-cold 2.825 다. 시드 1~5 모두 같은 방향이었다(T5: 2.813~2.825 vs 2.620~2.623). 차가운 세그먼트는 쓰레기가 아주 천천히 쌓여서 greedy 의 "최소 live" 순위에 좀처럼 오르지 않는다. 그 바람에 쓰레기 일부가 차가운 세그먼트에 묶인 채 공간을 깔고 앉는다. 남은 여유를 뜨거운 세그먼트들이 나눠 쓰니, 뜨거운 쪽은 덜 비워진 상태(u 0.5~0.6)에서 청소된다. 지역성이 있는데 더 나빠지는 것, 이것이 Rosenblum 이 cost-benefit 을 만든 출발점이다(논문 기억, 원문 수치는 확인 못 함).
2. **cost-benefit 은 균등에서 greedy 보다 나쁘다** (u 0.80: 2.860 vs 2.620, +9 %). hot/cold 정보가 없는 트래픽에서 age 는 잡음이다. 오래된 세그먼트를 u 가 더 높을 때 고르게 만들 뿐이다. DS-A09 도 같은 방향을 보고했다(더 작은 차이). "항상 cost-benefit" 은 정답이 아니다.
3. **cost-benefit 의 victim 사용률은 두 봉우리다.** T4 (hot-cold, u = 0.75):

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">청소한 세그먼트의 사용률 분포 (% of victims) — demo_sweep.c T4 실측, hot-cold u = 0.75</text>
  <path d="M70 250 L700 250 M70 250 L70 60" stroke="currentColor"/>
  <path d="M70 160 L700 160 M70 70 L700 70" stroke="currentColor" stroke-dasharray="2 4" opacity="0.5"/>
  <g fill="currentColor"><text x="36" y="254">0 %</text><text x="30" y="164">50 %</text><text x="24" y="74">100 %</text></g>
  <g class="f-mut" stroke="currentColor">
    <rect x="390" y="74.3" width="24" height="175.7"/><rect x="452" y="245.7" width="24" height="4.3"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <rect x="102" y="235.6" width="24" height="14.4"/><rect x="164" y="164.0" width="24" height="86.0"/><rect x="226" y="244.2" width="24" height="5.8"/>
    <rect x="474" y="247.1" width="24" height="2.9"/><rect x="536" y="209.3" width="24" height="40.7"/><rect x="598" y="227.3" width="24" height="22.7"/><rect x="660" y="242.8" width="24" height="7.2"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="101" y="268">0.0</text><text x="163" y="268">0.1</text><text x="225" y="268">0.2</text><text x="287" y="268">0.3</text><text x="349" y="268">0.4</text>
    <text x="411" y="268">0.5</text><text x="473" y="268">0.6</text><text x="535" y="268">0.7</text><text x="597" y="268">0.8</text><text x="659" y="268">0.9</text>
    <text x="385" y="290">victim 사용률 u 구간 (live / S)</text>
    <text x="402" y="68">97.6</text><text x="176" y="158">47.8</text><text x="548" y="203">22.6</text><text x="610" y="221">12.6</text>
  </g>
  <rect x="440" y="90" width="250" height="56" fill="none" stroke="currentColor" opacity="0.6"/>
  <rect x="450" y="100" width="12" height="12" class="f-mut" stroke="currentColor"/><text x="470" y="111" fill="currentColor">greedy — 한 봉우리, WA 2.373</text>
  <rect x="450" y="124" width="12" height="12" class="f-acc" stroke="currentColor"/><text x="470" y="135" fill="currentColor">cost-benefit — 두 봉우리, WA 1.693</text>
</svg>
```

greedy 는 victim 의 97.6 % 를 u 0.5~0.6 에서 청소한다. cost-benefit 은 56 % 를 u < 0.2(뜨거운 세그먼트를 거의 빌 때까지 기다림), 40 % 를 u ≥ 0.6(차가운 세그먼트를 일찍 정리)에서 청소한다. 평균 ū 는 0.579 → 0.409 다. 차가운 세그먼트를 u 0.7~0.9 에 청소하는 것은 비싸 보이지만, 한 번 정리하면 오래 안 건드려도 된다. 그 덕에 뜨거운 세그먼트가 거의 빈 상태까지 기다릴 수 있다. LFS 논문이 그린 bimodal 분포가 그대로 나왔다.

**분리·정렬은 기대만큼 효과가 없었다 (T3, hot-cold u = 0.80, 실측):**

| 정책 | separate | age_sort (batch 4) | WA | 평균 victim u |
|---|---|---|---|---|
| greedy | off | off | 2.860 | 0.650 |
| greedy | on | on | 2.825 | 0.646 |
| cost-benefit | off | off | 2.009 | 0.502 |
| cost-benefit | on | off | **1.960** | 0.490 |
| cost-benefit | on | on | 2.041 | 0.510 |

정직하게 말하면 이 시뮬레이터에서 클리너 출력 분리는 cost-benefit 을 2.4 % 개선했고, 나이 정렬은 오히려 4 % 나빠졌다. 반면 DS-A09 는 "스트림 분리가 없으면 cost-benefit 이득이 사라진다" 를 측정했다. 차이의 원인은 실험으로 분리하지 않았다. 추정은 두 가지다. ① 여기서는 청소가 한 번에 몰아서 일어난다(워터마크까지 연속 패스). 그래서 1 스트림이어도 클리너 복사본이 세그먼트 몇 개를 통째로 채워 사실상 분리된다. DS-A09 는 호스트 쓰기마다 조금씩 복사하는 페이싱이라 잘게 섞인다. ② age 의 정의가 다르다(여기는 LFS 식 "데이터의 원래 쓰기 시각" 을 들고 다니고, DS-A09 기본값은 블록이 닫힌 시각). 교훈은 **"분리가 중요하다" 는 결론은 청소 방식(몰아서 vs 페이싱)과 age 정의에 따라 달라진다** 는 것이다. 실제 장치에서는 대표 트레이스로 재야 한다.

### Part C — summary 만으로 map 다시 만들기

전원이 나가면 DRAM 의 map 과 usage table 이 사라진다. LFS 는 checkpoint region(imap 조각의 위치)에서 시작해, 그 뒤 로그를 따라가며 summary 로 갱신분을 다시 적용한다(roll-forward, OSTEP 43.12). SSD 는 맵 스냅샷 + 그 뒤 블록들의 OOB/P2L 을 스캔한다(MEM-M14 의 범위, 다음 단계). 이 레슨은 가장 단순한 극단, **체크포인트 없이 전부 스캔** 을 한다:

```c
for (FREE 아닌 세그먼트 s) for (off < wp[s]) {
    lba = sum_lba[p];  cur = map_out[lba];
    if (cur == NONE) map_out[lba] = p;
    else if (sum_ver[p] == sum_ver[cur]) return -1;      /* 같은 버전 둘 = 모호 */
    else if (sum_ver[p] >  sum_ver[cur]) map_out[lba] = p;
}
```

테스트 출력 (64 × 32, hot-cold, cost-benefit, 실제):

```text
  rebuilt 1638 lbas, stale summary slots 340, naive physical-order rebuild wrong for 118 lbas
  wear: erases min 10 max 28 mean 17.0
```

죽은 summary 340 개가 살아 있는 세그먼트에 남아 있고, "물리 주소가 뒤인 것이 이긴다" 로 만들면 118 개 LBA 가 옛 버전을 가리킨다. 버전(또는 전역 시퀀스 번호)이 없으면 재구성이 불가능하다. 연습 1 은 반대 방향의 함정이다. **TRIM 을 로그에 남기지 않으면** 스캔이 지운 데이터를 되살린다(TRIM 직후 4 178 개 전부).

### Part C — 마모: 정책이 erase 분포를 바꾼다

demo T5 (hot-cold, u 0.80, 1024 세그먼트, 시드 5, 실측):

| 정책 | erase 최소 | 최대 | 평균 | 최대/평균 |
|---|---|---|---|---|
| greedy | 2 | 71 | 25.1 | 2.8 |
| cost-benefit | 8 | 36 | 18.6 | 1.9 |
| FIFO | 29 | 34 | 33.0 | 1.03 |

greedy 는 차가운 데이터가 사는 세그먼트를 거의 지우지 않고(최소 2 회), 뜨거운 쪽만 반복해서 지운다(최대 71 회). NAND 는 블록마다 P/E 수명이 정해져 있어서, 장치 수명은 평균이 아니라 **최대**로 정해진다. 그래서 FTL 에는 차가운 블록을 일부러 옮기는 **정적 웨어 레벨링** 이 따로 있다(MEM-M12, 다음 단계). cost-benefit 은 차가운 세그먼트도 주기적으로 정리하니 편차가 저절로 줄었다. FIFO 는 완벽하게 고르지만 WA 가 가장 나쁘다. WA(총 erase 수)와 편차(최대 erase 수)는 서로 다른 축이다.

### Part C — Don 의 SSD 로 옮기기

| 이 레슨 | 실제 SSD FTL 에서 | 차이·주의 |
|---|---|---|
| 세그먼트 | 슈퍼블록 (다이·플레인마다 블록 하나씩 묶음) | erase 단위는 블록, GC 단위는 보통 슈퍼블록 |
| 블록 = 매핑 단위 | 4 KiB 매핑, NAND 페이지 16 KiB (TLC/QLC 는 더 큼) | 부분 프로그램 패딩도 WA 에 더해짐 |
| summary | 페이지 OOB 의 LPA + 시퀀스 번호, 또는 블록 끝 P2L | 이 레슨의 `sum_ver` = 시퀀스 번호 |
| 워터마크 | GC 시작/정지 free 블록 임계값, 포그라운드 vs 백그라운드 GC | F2FS 도 BG=CB, FG=greedy |
| `separate` | GC 전용 오픈 블록 (host/GC 스트림 분리) | F2FS `CURSEG_COLD_DATA` = "GCed data" |
| 연습 3 힌트 | NVMe Streams 디렉티브, FDP 배치 핸들, ZNS (호스트가 배치) | 스펙 이름·세부는 버전마다 다름, 문서 기준 |
| 연습 1 TRIM | NVMe Dataset Management (Deallocate), SATA TRIM, `fstrim` | 맵/저널에 기록해야 전원 차단 뒤 유지 |
| `lfs_rebuild_map` | 마운트 시 체크포인트 + OOB 스캔 | 스캔 시간 = 부팅 시간 (MEM-M14) |
| 해석 모델 | OP 설계, 수명(TBW) 계산 | 실제 WA 에는 메타데이터·읽기 교란 재기록·SLC 캐시 이주도 들어감 |

면접에서 "당신이 만든 FTL 의 GC 는?" 이라는 질문이 오면 이 표의 열 순서로 답하면 된다. ① 매핑 단위와 GC 단위, ② victim 정책과 그 이유(트래픽 특성), ③ 트리거·페이싱(지연 목표), ④ GC 출력 배치(스트림), ⑤ 측정한 WA 와 그 측정 방법(SMART 의 host writes vs NAND writes), ⑥ 전원 차단 복구. 각 칸에 Don 의 실제 숫자를 넣는다. 이 레슨은 각 칸의 "왜" 를 측정값으로 받쳐 준다.

**MCU 쪽.** SPI NOR/NAND 위의 로그 구조 저장소는 크기만 작을 뿐 같은 구조다. 페이지 단위로 append 하고 섹터 단위로 erase 한다. 예로 DS-A10(플래시 로그 구조 KV), SPIFFS 계열, littlefs 의 메타데이터 쌍 compaction 이 있다. littlefs 는 데이터 블록에 COW 를 쓰고 블록 단위 동적 웨어 레벨링을 한다. 세그먼트 클리너와는 모양이 다르다(STOR-A03, 다음 단계, 세부는 littlefs 설계 문서 기준). RAM 이 수십 KB 면 map 전체를 RAM 에 못 두니, summary 스캔이 곧 마운트이고 그 시간이 부팅 시간이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san
```

실제 출력 (PASS 줄 생략):

```text
-- Part A: out-of-place append, segment summary, liveness
  small device: 752 cleans, 1028 copied, erase-time checks 752
-- Part B: victim policies, cleaner, WA
  check-on-erase run: 939 erases checked, 373 passes (batch 3)
  uniform u=0.80 FIFO  : WA 2.7743  model 2.7940 (alpha_eff 1.2378, mean free 1.50)
  uniform u=0.80 greedy: WA 2.6809  model 2.6918
  hot-cold u=0.80: greedy WA 2.777 (11378 seg/GiB)  cost-benefit WA 2.109 (8638 seg/GiB); greedy uniform 2.682
-- Part C: rebuild the map from segment summaries (roll-forward / OOB scan)
  rebuilt 1638 lbas, stale summary slots 340, naive physical-order rebuild wrong for 118 lbas
  wear: erases min 10 max 28 mean 17.0
40/40 passed
```

5 회 연속 `40/40 passed`, exit 0. 실행 시간은 0.05~0.18 초이고, `/` 에서 실행해도 같다(데이터 파일 없음). ASan+UBSan 빌드는 `40/40 passed`, 보고 없음, 0.41 초. "지울 때마다 검사" 실행에서 939 번 지우는 동안 불변식 위반 0, 유실 0 이었다. 모든 실행은 끝에서 전체 LBA 를 읽어 기대 버전과 대조한다.

## 7. 자주 하는 실수

1. **옛 자리를 청소 전에 읽기** — `old = map[lba]` 를 청소 앞에 두면, 청소가 그 사본을 옮기고 세그먼트를 지운 뒤에 FREE 세그먼트의 `live` 를 내린다. → 청소 뒤에 읽는다. 검사기의 `cnt[s] != live[s]` 와 FREE 세그먼트 `live == 0` 검사가 잡는다.
2. **batch 에서 victim 을 하나씩 지우기** — 나이 정렬로 victim 들의 블록이 섞여 있으니, victim 1 의 마지막 블록을 복사하기 전에 victim 1 을 지울 수 있다. → 모두 복사한 뒤 지운다. `seg_erase` 의 `lost_live` 가 0 이 아니면 데이터 유실이다.
3. **헤드나 꽉 찬 세그먼트를 후보로** — 헤드를 지우면 지금 쓰는 곳이 사라진다. `live == S` 를 고르면 복사만 하고 공간이 안 생겨 무한 루프에 빠진다(cost-benefit 점수 0 이라 보통은 안 뽑히지만, 다른 후보가 모두 점수 0 이면 뽑힌다). → 두 경우 모두 제외하고, `maybe_clean` 에 진전 없음 가드를 둔다.
4. **버전 없는 summary** — 재구성에서 물리 순서로 최신을 정하면 118 개 LBA 가 틀린다(Part C). 세그먼트는 재사용되므로 물리 주소는 시간이 아니다. → 쓰기마다 오르는 버전 또는 시퀀스 번호를 summary 에.
5. **reserve 0, 또는 wm_low ≤ reserve** — 호스트가 마지막 free 세그먼트까지 먹거나, 호스트는 막혔는데 청소가 시작되지 않는다. 쓰레기가 많아도 교착이다(연습 2). → `reserve ≥ 1`(batch 1 기준), `wm_low > reserve`.
6. **warm-up 없이 WA 측정** — 순차 prefill 직후부터 재면 아직 정상 상태가 아니다. u 0.80 greedy 균등에서 warm-up 0 이면 2.513, 1×nlba 이상이면 2.66~2.69 로, 6 % 낮게 나온다(측정 2×nlba 기준, `/tmp` 작업 사본 실측). → prefill + warm-up 뒤 지표 리셋.
7. **균등 워크로드 하나로 정책 결론** — 균등에선 cost-benefit 이 9 % 나쁘고 hot-cold 에선 28 % 좋다. 한쪽만 재면 정반대 결론이 나온다. → 최소 균등·편향·순차 셋, 여러 사용률, 여러 시드(T5).
8. **LFS 의 `2/(1−u)` 를 SSD WA 로 인용** — 그 식은 읽기까지 센 write cost 이고, u 는 디스크가 아니라 victim 의 사용률이다. → SSD WA ≈ `1/(1−ū)`, 그리고 ū 는 디스크 u·정책·워크로드로 정해진다.

## 8. 면접 Q&A

<details><summary>Q1. LFS 는 어떤 블록이 살아 있는지 어떻게 아나요? 비트맵이 있나요?</summary>

세그먼트마다 summary 가 있어서 각 슬롯이 "누구의 몇 번째 블록(그리고 버전)" 이었는지 적혀 있다. 클리너는 그 주인의 현재 위치(LFS 는 imap → inode → 블록 포인터, 이 레슨은 `map[lba]`)가 이 주소를 가리키는지 본다. 가리키면 live, 아니면 죽은 것이다. 비트맵은 필수가 아니다. 덮어쓸 때 옛 슬롯을 건드릴 필요가 없다는 것이 이 설계의 장점이다(옛 슬롯에 "죽음" 을 쓰는 것도 쓰기다. NAND 에선 아예 불가능하다). 세그먼트별 live **수** 는 usage table 에 따로 두는데, victim 고르기에 필요하기 때문이다. 꼬리: "그럼 SSD 는 왜 valid 비트맵을 두나요?" — 판정마다 P2L(OOB)을 읽는 대신 DRAM 비트 연산으로 끝내려고. 같은 사실의 캐시이고, 전원 차단 뒤 L2P 에서 다시 계산할 수 있다(DS-A09).
</details>

<details><summary>Q2. cost-benefit 식을 유도하고, 왜 age 를 곱하는지 설명해 보세요.</summary>

비용: victim 읽기 1 + live 다시 쓰기 u = `1 + u`. 이득: 생기는 빈 공간 `1 − u`. 여기서 끝내면 greedy 와 순서가 같다(u 의 단조 함수). age 를 곱하는 이유는 **그 공간이 얼마나 오래 유지되느냐** 다. 오래 안 바뀐 데이터는 앞으로도 안 바뀔 가능성이 크다. 그러니 차가운 세그먼트를 정리해 두면 그 데이터는 이제 꽉 찬 세그먼트에 모여 오래 그대로 있고, 쓰레기를 품은 채 공간을 붙잡지 않는다. 뜨거운 세그먼트는 기다리면 저절로 더 비워진다. 그래서 `(1−u)·age/(1+u)` 최대를 고른다. 실측(T4)에서 cost-benefit 은 차가운 세그먼트를 u 0.7~0.9 에서, 뜨거운 세그먼트를 u < 0.2 에서 청소했다. 평균 ū 는 greedy 0.579 → 0.409, WA 는 2.373 → 1.693 이다. SSD 에선 읽기가 쓰기보다 훨씬 싸므로 비용을 `u` 로 보는 변형도 쓴다(문서 기준, 미검증). F2FS 는 `100·(100−u)·age/(100+u)` 를 그대로 쓴다(`fs/f2fs/gc.c:383`, 6.18.55).
</details>

<details><summary>Q3. 지역성이 있으면 GC 가 쉬워야 할 것 같은데, greedy 는 왜 hot-cold 에서 더 나빠지죠?</summary>

greedy 는 "지금 가장 비어 있는 세그먼트" 만 본다. 차가운 세그먼트는 쓰레기가 아주 천천히 쌓여 순위에 거의 오르지 않는다. 그래서 차가운 세그먼트 수백 개가 각자 10~20 % 쓰레기를 품은 채 공간을 깔고 앉는다. 남은 여유는 뜨거운 세그먼트들이 나눠 써야 하므로, 뜨거운 세그먼트는 덜 비워진 상태(u 0.5~0.6)에서 청소된다. 실측 u 0.80 에서 균등 2.620, hot-cold 2.825 였고 시드 5 개 모두 같은 방향이었다. 해결은 차가운 세그먼트를 일부러 일찍 정리하는 정책(cost-benefit), 또는 처음부터 hot/cold 를 다른 세그먼트에 쓰는 배치(연습 3)다. 이 현상이 LFS 논문이 cost-benefit 을 만든 동기였다.
</details>

<details><summary>Q4. OP 를 7 % 에서 28 % 로 늘리면 WA 는 얼마나 줄죠? 대충이라도 계산할 수 있나요?</summary>

OP 가 `(T−U)/U` 면 α = T/U = 1 + OP 다. 균등 랜덤 + FIFO 면 `ū = e^{−α(1−ū)}` 를 풀어 `WA = 1/(1−ū)` 로 계산한다. greedy 는 그보다 조금 낮다(Desnoyers 2012). 이 레슨의 스윕으로 보면 u 0.90(α ≈ 1.11)에서 greedy 4.897, u 0.80(α = 1.25)에서 2.620, u 0.75(α ≈ 1.33)에서 2.152 다. OP 7 % 는 α 1.07 이라 표보다 더 왼쪽이다. DS-A09 측정은 greedy 7.647(7 %) → 2.462(28 %), 약 3 배다. 핵심은 곡선이 `1/(1−ū)` 꼴이라 여유가 적을 때 아주 가파르다는 것이다. 꼬리: "실제 드라이브에서 이 숫자를 어떻게 확인하죠?" — SMART/벤더 로그의 host writes 와 NAND writes 비율을 정상 상태(프리컨디셔닝 후)에서 잰다. 이 레슨도 warm-up 없이 재면 6 % 낮게 나왔다.
</details>

<details><summary>Q5. 클리너가 교착에 빠질 수 있나요? 예약은 얼마나 두죠?</summary>

빠질 수 있다. 클리너도 live 를 옮길 **빈 자리**가 필요하다. 호스트가 마지막 free 세그먼트까지 써 버리면, 쓰레기가 많아도 옮길 데가 없어 아무것도 지울 수 없다. 연습 2 에서 reserve 0 은 쓰기 #32 에서 `-1` 을 냈다. 실패한 쓰기는 옛 데이터를 망가뜨리지 않았고, 검사기도 통과했다. 증명: victim 하나의 live 는 `S − 1` 이하라 복사에 최대 1 세그먼트가 들고, 지우면 1 세그먼트가 돌아온다. 그러니 호스트가 1 개를 남기면(reserve 1) 청소는 늘 진전한다. batch 를 키우면 room 이 허락하는 만큼만 고른다. 운영 규칙이 하나 더 있다. **wm_low > reserve** 여야 한다. 아니면 호스트는 reserve 때문에 막혔는데 청소는 "아직 워터마크 위" 라며 시작하지 않는다. SSD 는 여기에 배드 블록·런타임 결함 몫까지 예약에 더하고, 다 쓰면 read-only 모드로 내려간다(제품 정책, 문서 기준).
</details>

<details><summary>Q6. TRIM 이 WA 를 왜 줄이나요? TRIM 을 받고 아무것도 기록하지 않으면 무슨 일이 생기죠?</summary>

TRIM 이 없으면 FTL 은 파일 시스템이 지운 데이터를 여전히 live 로 보고 GC 때마다 옮긴다. 장치 입장의 사용률 u 가 실제보다 높다. 연습 1 에서 LBA 30 % 를 TRIM 하자 WA 가 2.724 → 1.454 로 줄었다. 모델로 계산한 값(2.709 → 1.457)과도 맞았다. 여유 공간이 늘어나는 것과 정확히 같은 효과다. 그런데 TRIM 을 map 에만 반영하고 로그·체크포인트에 남기지 않으면, 전원 차단 뒤 summary 스캔이 그 LBA 의 옛 사본을 찾아 **되살린다**. TRIM 직후 재구성하면 4 178 개가 전부 되살아났다. 세그먼트가 청소되어 옛 사본이 지워지면 문제는 사라지지만, 그때까지는 위험 창이 열려 있다. 그래서 실제 FTL 은 deallocate 를 맵 업데이트처럼 저널/체크포인트에 기록하고, 호스트에게는 "TRIM 된 LBA 를 읽으면 0" 같은 동작을 보장한다(NVMe DLFEAT 등, 세부는 문서 기준).
</details>

<details><summary>Q7. (staff) 호스트가 hot/cold 를 알려 줄 수 있다면 컨트롤러 설계를 어떻게 바꾸겠나요?</summary>

호스트 스트림마다 오픈 세그먼트를 따로 둔다(NVMe Streams, FDP 배치 핸들, F2FS 의 hot/warm/cold 다중 로그 — 이름과 세부는 스펙 버전마다 다름). 연습 3(오라클 힌트, u 0.80)에서 greedy 는 2.920 → 2.745(−6 %)로 좋아졌지만, cost-benefit 은 2.027 → 2.035 로 그대로였다. 이미 나이로 거의 같은 정보를 얻고 있었기 때문이다. 다른 스윕(`/tmp` 실험)에서는 u 0.9 에서 cost-benefit 이 힌트로 2~4 % 나빠지기도 했다. 그래서 설계는 이렇게 하겠다. ① 힌트는 배치에 쓰고, ② victim 정책은 힌트와 별도로 데이터 나이를 계속 쓰며, ③ GC 출력도 원래 스트림을 따르게 해서(이 레슨은 클리너 헤드 하나라 다시 섞인다) 분리를 GC 이후까지 유지한다. ④ 스트림마다 여유 공간을 얼마나 나눠 줄지는 고정 비율이 아니라 측정으로 정한다. 그리고 힌트가 틀린 호스트(전부 hot 이라고 보내는 경우)에 대비해 스트림별 WA 를 텔레메트리로 본다.
</details>

<details><summary>Q8. (staff) 이 시뮬레이터의 결론이 실제 SSD 나 F2FS 에서 틀리는 경우는?</summary>

① 비용 모델: 복사만 세고 읽기 시간, 다이 병렬성, 프로그램 지연은 없다. 실제 GC 비용은 지연(tail latency)으로도 나타난다(DS-A09 의 페이싱). ② 매핑 단위 = 쓰기 단위라고 가정했다. 16 KiB 페이지에 4 KiB 매핑이면 부분 프로그램 패딩과 읽기-수정-쓰기가 WA 를 더한다. ③ 메타데이터 쓰기(맵 플러시, summary, 체크포인트)가 빠졌다. 작은 랜덤 쓰기에서 이 몫이 크다. ④ 워크로드: 균일 2 영역 hot-cold 는 실제 트레이스의 시간 지역성(파일 단위로 같이 죽음)을 담지 못한다. T3 처럼 분리·정렬의 효과도 청소 방식에 따라 뒤집혔다. ⑤ 정적 데이터와 마모: 웨어 레벨링 이동, 읽기 교란 재기록, 보존(retention) 리프레시가 WA 에 더해진다. ⑥ SLC 캐시 이주(SLC → TLC 접기)는 그 자체로 WA 2× 가까이를 만들 수 있다(제품마다 다름, 미검증). 그래서 이 숫자는 "정책과 OP 가 만드는 하한" 이고, 실제 장치에서는 SMART 로 다시 잰다.
</details>

<details><summary>Q9. 이 구현이 맞다는 걸 어떻게 확인했나요? "WA 가 그럴듯하다" 이상으로요.</summary>

네 층이다. ① **독립 검사기**: map ↔ summary ↔ 내용 ↔ usage(live 수, mtime) ↔ free 링을 TODO 함수 없이 다시 계산해 대조한다. 작은 장치에선 쓰기마다, 큰 실행에선 세그먼트를 지울 때마다 돌렸다(939 회, 위반 0). 지울 때 live 가 남았으면 `lost_live` 로 센다. ② **데이터 무결성**: 내용 = `(lba << 32) | version`, 모든 실행 끝에 호스트 쪽 기대 버전과 전체 대조한다. ③ **알려진 답**: 손으로 만든 세그먼트 상태의 victim 선택, 순차 워크로드 WA = 1.000 정확히, 균등 WA 가 해석 모델 ±3 % 안(실제 0.4~0.7 %), `WA = 1/(1−ū)` 회계 항등식 ±1 %. ④ **재현성**: 시드 5 개에서 단언한 순서(greedy hc > uni, cb < greedy)가 모두 성립했다. 해석 모델이 맞는다는 것은 "측정이 맞다" 의 증거이기도 하지만, 모델과 시뮬레이터가 같은 가정(균등 랜덤, 블록 = 매핑 단위)을 공유한다는 뜻이기도 하다. 그래서 모델이 맞는 구간 밖(hot-cold)에서는 정책 간 **순서**만 단언했다.
</details>

## 9. 추가 연습

### 연습 1 — TRIM

`lfs_trim(L, lba)` 을 구현하라. 앞 70 % LBA 만 덮어쓰고 뒤 30 % 는 prefill 뒤 손대지 않는 워크로드(u 0.85, greedy)에서, 뒤 30 % 를 TRIM 했을 때와 안 했을 때의 WA 를 비교하라. 두 경우 모두 해석 모델로 예측하라. 그리고 TRIM 직후 `lfs_rebuild_map` 으로 다시 만들면 무엇이 되살아나는지 세라.

<details><summary>답 보기</summary>

`live[seg]--`, `map = NONE`, `ver++`(다음 쓰기의 버전이 옛 summary 보다 커지도록). TRIM 이 없으면 뒤 30 % 는 prefill 때 순차로 쓰여 꽉 찬 세그먼트로 굳고, 앞 70 % 가 **나머지 공간**에서 돈다. 그러니 모델의 α = `(T − 0.3U)/0.7U`. TRIM 하면 그 세그먼트들이 live 0 이 되어 첫 청소에 돌아오므로 α = `T/0.7U`. 실측 2.724(모델 2.709) → 1.454(모델 1.457). 재구성: TRIM 직후엔 trim 된 4 178 개 전부 되살아남, 워크로드가 끝난 뒤엔 0(그 세그먼트들이 청소되어 옛 사본이 지워짐). 위험 창이 "청소될 때까지" 열려 있다는 뜻이고, 그래서 TRIM 은 기록해야 한다(Q6).
</details>

### 연습 2 — 클리너 예약과 교착

16 × 8 장치, u 0.75 에서 `reserve = 0`(wm 1/2)과 `reserve = 1`(wm 2/3)로 20 000 번 쓰라. 0 은 언젠가 `-1` 이 나야 하고(멈추지 않고), 그때도 모든 데이터가 마지막 성공 버전으로 읽혀야 한다. 1 은 실패가 없어야 한다.

<details><summary>답 보기</summary>

실측: reserve 0 은 쓰기 #32 에서 처음 실패했다. 시드 1~5 모두 같은 번호였다(`/tmp` 실험). 랜덤 쓰기 초반에 호스트가 free 를 다 먹는 시점이 시드와 무관하기 때문이다. 데이터는 온전했고 검사기도 통과했다. reserve 1 은 실패 0 이었다. 교착의 증상은 "쓰레기는 많은데 옮길 자리가 없다" 이다. 해석은 Q5 를 보라. wm 을 reserve 에 맞춰 올린 이유는 힌트 5 와 같다. `wm_low ≤ reserve` 면 reserve 1 이어도 실패한다(호스트는 막혔는데 청소가 시작되지 않음).
</details>

### 연습 3 — 호스트 스트림 힌트 (hot/cold 헤드 두 개)

호스트가 각 쓰기에 hot/cold 힌트를 준다고 하자(오라클: 앞 10 % LBA = hot). cold 쓰기는 별도 헤드에 쓰게 하라(`hhead` 를 잠깐 바꿔 끼우면 엔진을 안 고치고도 된다). greedy 와 cost-benefit 의 WA 변화를 재라.

<details><summary>답 보기</summary>

`write_hinted()`: cold 면 `L->hhead` 와 저장해 둔 cold 헤드를 바꿔 `lfs_write` 를 부르고 되돌린다. 파킹된 헤드는 OPEN 이라 victim 후보에서 자동으로 빠진다. 실측 u 0.80: greedy 2.920 → 2.745(−6 %), cost-benefit 2.027 → 2.035(변화 없음). 시드·u 를 바꾼 `/tmp` 스윕에서 greedy 는 늘 4~11 % 좋아졌고, cost-benefit 은 u 0.7 에서 −5 %, u 0.9 에서 +2~4 % 였다. 힌트가 있어도 클리너 출력은 다시 한 헤드로 섞이고, 스트림별 여유 배분도 정책이 정하지 않는다. 그래서 "힌트만 있으면 끝" 이 아니다(Q7).
</details>

### 연습 4 — victim 사용률 분포 단언

hot-cold, u 0.75 에서 greedy 와 cost-benefit 의 `hist[]` 를 비교해 "greedy 는 한 봉우리(최대 구간 > 80 %), cost-benefit 은 두 봉우리(u < 0.3 이 30 % 초과, u ≥ 0.6 이 20 % 초과), 그런데 평균 ū 는 cost-benefit 이 낮다" 를 단언하라.

<details><summary>답 보기</summary>

실측(256 × 64, batch 4): greedy 최대 구간 83.5 %, u < 0.3 은 0 %. cost-benefit 은 u < 0.3 이 58.0 %, u ≥ 0.6 이 41.8 %, 평균 ū 는 0.573 → 0.419. 높은 u 에서 청소하는 일이 많은데도 WA 가 낮은 이유는, 그 덕에 뜨거운 세그먼트를 거의 빌 때까지 기다릴 수 있기 때문이다. 데모 T4(1024 세그먼트)의 그림과 같은 모양이다.
</details>

### 연습 5 — LFS 논문의 write cost

균등 워크로드 u 0.60, 0.80 에서 `(segs_cleaned·S + clean_w + host_w)/host_w` 를 재고, `2/(1−ū)` 와 1 % 안으로 맞는지 확인하라.

<details><summary>답 보기</summary>

실측: 디스크 u 0.60 → victim ū 0.320, write cost 2.940 vs 2.941. 디스크 u 0.80 → ū 0.628, 5.374 vs 5.375. WA 는 각각 1.470, 2.687 로 `1/(1−ū)` 와 같다. 항등식이 거의 정확한 이유: 정상 상태에서 지운 세그먼트의 빈 칸 = 호스트 쓰기. 논문의 식은 **victim 의 u** 로 쓴 회계식이고, 디스크 u 에서 victim ū 를 예측하는 것이 정책·모델의 몫이다. 디스크 u 0.80 을 그대로 넣으면 `2/(1−0.8) = 10` 이 되는데, 실제로는 5.37 이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san
```

실제 출력 (PASS 줄 생략, 0.07 초):

```text
-- ex1 TRIM
  no TRIM: WA 2.724 (model 2.709)   TRIM 30%: WA 1.454 (model 1.457)
  summary-scan rebuild resurrects trimmed lbas: right after TRIM 4178 of 4178, after the workload 0
-- ex2 cleaner reserve
  reserve 0: first failed write at #32 (data intact: yes)   reserve 1: first failure -1
-- ex3 host stream hint (hot/cold heads)
  greedy: 1 stream 2.920 -> 2 streams 2.745   cost-benefit: 2.027 -> 2.035
-- ex4 bimodal victim utilization
  greedy: u<0.3 0.0%  u>=0.6 15.0%  top bucket 83.5%  mean u 0.573 | cost-benefit: u<0.3 58.0%  u>=0.6 41.8%  mean u 0.419
-- ex5 LFS write cost = 2/(1-u)
  disk u 0.60: victim u 0.320  write cost 2.940  2/(1-u) 2.941  (WA 1.470 = 1/(1-u) 1.471)
  disk u 0.80: victim u 0.628  write cost 5.374  2/(1-u) 5.375  (WA 2.687 = 1/(1-u) 2.688)
17/17 passed
```

ASan+UBSan 빌드도 `17/17 passed`, 보고 없음.

## 10. 핵심 정리

- 로그 구조 = 제자리 덮어쓰기 금지. **map(imap/L2P) + summary(OOB/P2L) + usage table(live, mtime)** 세 가지로 굴러가고, live 판정은 `map[summary.lba] == phys` 한 줄이다(비트맵은 캐시).
- WA = (호스트 + 복사)/호스트 = **1/(1−ū)**, ū = victim 사용률. LFS 의 `2/(1−u)` 는 읽기까지 센 write cost. 곡선은 u 0.8 이후 가파르다(greedy 균등 0.80 → 2.62, 0.90 → 4.90).
- greedy = live 최소. cost-benefit = `(1−u)·age/(1+u)` 최대: hot-cold 에서 −28 %, 균등에서 +9 %. **트래픽이 정답을 정한다.** F2FS 는 BG=CB, FG=greedy (`fs/f2fs/gc.c:244-270, 383`).
- 반직관: greedy 는 지역성이 있으면 나빠지고(2.62 → 2.83), cost-benefit 의 victim u 는 두 봉우리(뜨거운 것 u < 0.2, 차가운 것 u ≥ 0.6)다.
- 해석 모델(균등): FIFO `ū = e^{−α(1−ū)}`, greedy Desnoyers Eq.12. T 에서 free·헤드 몫을 빼면 측정과 1 % 안이다(1024 세그먼트에선 0.3 %).
- 클리너 예약 ≥ 1 세그먼트, `wm_low > reserve`. 아니면 쓰레기가 많아도 교착이다. TRIM 은 WA 를 크게 줄이지만(2.72 → 1.45) **기록하지 않으면 재구성 때 되살아난다**.
- 재구성엔 버전/시퀀스 번호가 필수다(물리 순서 ≠ 시간 순서, 118 개 틀림). 마모 편차는 정책이 만든다(greedy 최대/평균 2.8, FIFO 1.03) → 정적 웨어 레벨링.

## 참고자료

- OSTEP 43장 Log-structured File Systems (43.5 imap, 43.9–43.11 GC·liveness·정책, 43.12 roll-forward), 부록 I Flash-based SSDs (I.7 로그 구조 FTL, I.8 GC, I.10 웨어 레벨링) — 디스크의 `book-md/C43_log_structured_file_systems.md`, `book-md/C0I_flash_based_ssds.md`. lfs.py 시뮬레이터(`.tools/ostep-homework/file-lfs/`)는 liveness 표시까지만 있고 클리너는 없어 교차 확인에는 쓰지 않았다.
- Mendel Rosenblum, John K. Ousterhout, "The Design and Implementation of a Log-Structured File System", SOSP 1991 / ACM TOCS 10(1), 1992 — write cost `2/(1−u)`, cost-benefit 정책, hot-and-cold 시뮬레이션, segment usage table (수치는 원문 재확인 안 함).
- Jeanna Neefe Matthews et al., "Improving the Performance of Log-Structured File Systems with Adaptive Methods", SOSP 1997 (OSTEP [MR+97], 이후의 정책 개선).
- Peter Desnoyers, "Analytic Modeling of SSD Write Performance", SYSTOR 2012 — 균등 랜덤 LRU/greedy 의 WA 해석식 (DS-A09 에서 측정과 대조).
- Changman Lee et al., "F2FS: A New File System for Flash Storage", USENIX FAST 2015 — 다중 헤드 로그, 포그라운드 greedy / 백그라운드 cost-benefit.
- Linux 6.18.55: `fs/f2fs/gc.c:244-270` (`select_gc_type`), `fs/f2fs/gc.c:358-384` (`get_cb_cost`), `fs/f2fs/f2fs.h:1126-1131` (`enum log_type`, `CURSEG_COLD_DATA` = "GCed data blocks"), `fs/f2fs/segment.h:128-135` (`GC_CB`/`GC_GREEDY`/`GC_AT`) — Lima VM `/var/lab/src/linux-6.18.55/` 에서 읽음.
- NVM Express Base Specification — Dataset Management(Deallocate), Directives(Streams); Flexible Data Placement (TP4146) — <https://nvmexpress.org/specifications/> (세부는 미검증, 문서 기준).
- 관련 레슨: OS-M14 (VSFS: 제자리 갱신 FS 와 fsck — 대조점), DS-A09 (FTL 자료구조·indexed heap·조건부 커밋·워터마크 페이싱·Desnoyers 식), DS-A10 (플래시 로그 구조 KV), DS-M11 (indexed heap). 다음 단계: OS-A07 (저널링 — 제자리 갱신 FS 의 크래시 일관성), MEM-M10–M14 (L2P, GC, 웨어 레벨링, 배드 블록, 전원 차단 복구), CAP-M05 (미니 FTL), STOR-A01 (앱 WA × FTL WA), STOR-A03 (littlefs 내부), SIM-M02 (SSD/NAND 성능 DES).
