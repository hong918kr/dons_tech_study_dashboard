# Ch.37 하드 디스크 드라이브 — seek · rotation · transfer, 그리고 디스크 스케줄링

> 📖 원문: [37. Hard Disk Drives](../book-md/C37_hard_disk_drives.md) · [PDF p.423](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=423) · ⏱️ 읽기 약 45분 · 🔗 선행: [Ch.36 I/O 장치](2026-09-30_C36_io_devices.md), [Ch.07 스케줄링 (SJF)](2026-09-30_C07_scheduling_intro.md)

## 0. 한눈에 보기

> **CRUX: HOW TO STORE AND ACCESS DATA ON DISK** — "How do modern hard-disk drives store data? What is the interface? How is the data actually laid out and accessed? How does disk scheduling improve performance?"
> (현대 HDD 는 데이터를 어떻게 저장하나? 인터페이스는? 데이터는 실제로 어떻게 배치되고 접근되나? 디스크 스케줄링은 성능을 어떻게 높이나?)

- 디스크의 인터페이스는 **512B 섹터의 배열**(주소 0..n−1)이다. 원자성은 **섹터 하나** 만 보장된다(torn write).
- 한 번의 I/O 시간은 **T_I/O = T_seek + T_rotation + T_transfer**. 랜덤 4KB 는 seek+rotation 이 거의 전부라서, 같은 디스크에서 **순차 vs 랜덤 대역폭이 200~350배** 차이 난다.
- 그래서 OS/디스크는 요청 순서를 바꾼다: **SSTF**(가까운 트랙부터) → 기아 문제 → **SCAN/C-SCAN**(엘리베이터) → 회전까지 고려한 **SPTF**. SPTF 는 헤드 위치를 아는 **드라이브 내부(펌웨어)** 에서 한다.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 섹터 (sector) | 512B 읽기/쓰기 단위, 원자적 쓰기 단위 | 요즘은 4KB 물리 섹터(Advanced Format) |
| torn write | 큰 쓰기 중 전원 손실로 일부 섹터만 써진 상태 | 8섹터 중 3개만 기록 |
| unwritten contract | 인터페이스에 없지만 다들 믿는 가정 | "가까운 LBA 는 빠르다, 순차가 제일 빠르다" |
| 플래터 / 표면 (platter / surface) | 자성 원판 / 그 한 면 | 4 platters = 8 surfaces |
| 스핀들 / RPM | 원판을 일정 속도로 돌리는 축 / 분당 회전 수 | 7200 RPM → 8.33 ms/회전 |
| 트랙 (track) | 한 표면 위 동심원 하나 | 머리카락 폭에 수백 개 |
| 헤드 / 암 (head / arm) | 읽고 쓰는 센서 / 헤드를 옮기는 팔 | 표면마다 헤드 1개 |
| seek | 암을 목표 트랙으로 옮기는 시간 | 가속→등속→감속→settle |
| rotational delay | 목표 섹터가 헤드 밑으로 돌아올 때까지 | 평균 반 바퀴 |
| transfer | 실제로 읽고 쓰는 시간 | 4KB @125MB/s = 31 µs |
| track skew | 트랙 넘어갈 때 다음 섹터를 엇갈려 배치 | seek 동안 지나칠 섹터만큼 |
| multi-zoned | 바깥 트랙에 섹터를 더 많이 배치 | ZBR, 바깥이 더 빠름 |
| track buffer (cache) | 드라이브 내부 DRAM | 8~256MB |
| write-back / write-through | 캐시에 넣고 완료 보고 / 매체 기록 후 완료 보고 | FLUSH CACHE, FUA |
| SSTF / SCAN / C-SCAN / SPTF | 가까운 트랙 우선 / 엘리베이터 / 한 방향 엘리베이터 / 위치결정 시간 최단 우선 | NCQ 는 드라이브가 SPTF |

## 2. 인터페이스 (37.1)

디스크는 OS 에게 **섹터(512B)의 배열** 로 보인다. 섹터 0 ~ n−1 이 디스크의 "주소 공간"이다. 파일시스템은 보통 4KB 단위로 읽고 쓰지만, 제조사가 보장하는 **원자성은 섹터 하나** 뿐이다. 4KB(8섹터) 쓰기 중 전원이 나가면 일부만 기록될 수 있다 — **torn write**. 이 사실이 [Ch.42 저널링](2026-09-30_C42_crash_consistency_journaling.md)의 출발점이 된다.

인터페이스 바깥의 **unwritten contract**(Schlosser & Ganger):

- 주소가 가까운 두 블록은 먼 두 블록보다 접근이 빠르다.
- 연속(순차) 접근이 가장 빠르고, 랜덤보다 훨씬 빠르다.

이 두 가정 위에 [FFS](2026-09-30_C41_ffs.md), [LFS](2026-09-30_C43_lfs.md) 같은 파일시스템 설계가 서 있다. SSD 는 이 계약을 일부 깨는데(부록 I), 그래도 "순차가 더 빠르다"는 여전히 대체로 맞다.

## 3. 기본 구조 (37.2–37.3)

```svg
<svg viewBox="0 0 700 330" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C37-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <circle cx="200" cy="165" r="140" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <circle cx="200" cy="165" r="105" fill="none" stroke="currentColor" stroke-dasharray="3 3"/>
  <circle cx="200" cy="165" r="70" fill="none" stroke="currentColor" stroke-dasharray="3 3"/>
  <circle cx="200" cy="165" r="12" fill="currentColor"/>
  <text x="200" y="195" text-anchor="middle" fill="currentColor" font-size="11">spindle</text>
  <text x="200" y="40" text-anchor="middle" fill="currentColor" font-size="11">outer track (sectors 0-11)</text>
  <text x="200" y="76" text-anchor="middle" fill="currentColor" font-size="11">middle (12-23)</text>
  <text x="200" y="110" text-anchor="middle" fill="currentColor" font-size="11">inner (24-35)</text>
  <path d="M 330 105 A 140 140 0 0 1 336 200" fill="none" style="stroke:var(--accent)" stroke-width="3" marker-end="url(#C37-arrow)"/>
  <text x="345" y="150" fill="currentColor" font-size="12">회전 (반시계)</text>
  <line x1="470" y1="300" x2="270" y2="160" stroke="currentColor" stroke-width="6"/>
  <circle cx="470" cy="300" r="10" fill="none" stroke="currentColor" stroke-width="2"/>
  <rect x="258" y="150" width="18" height="18" style="fill:var(--accent)" stroke="currentColor"/>
  <text x="485" y="305" fill="currentColor" font-size="12">arm pivot (VCM)</text>
  <text x="235" y="140" fill="currentColor" font-size="12" font-weight="bold">head</text>
  <line x1="290" y1="230" x2="320" y2="210" stroke="currentColor" marker-start="url(#C37-arrow)" marker-end="url(#C37-arrow)"/>
  <text x="300" y="250" fill="currentColor" font-size="12">seek = 트랙 사이 이동</text>
  <text x="430" y="40" fill="currentColor" font-weight="bold">T_I/O = T_seek + T_rotation + T_transfer</text>
  <text x="430" y="70" fill="currentColor" font-size="12">① seek: 가속 → 등속 → 감속 → settle</text>
  <text x="430" y="90" fill="currentColor" font-size="12">    (settle 만 0.5–2 ms)</text>
  <text x="430" y="115" fill="currentColor" font-size="12">② rotation: 목표 섹터가 올 때까지</text>
  <text x="430" y="135" fill="currentColor" font-size="12">    평균 = 1회전 / 2</text>
  <text x="430" y="160" fill="currentColor" font-size="12">③ transfer: 크기 / 전송률</text>
  <text x="430" y="200" fill="currentColor" font-size="12">플래터 여러 장, 각 면마다 헤드 1개,</text>
  <text x="430" y="218" fill="currentColor" font-size="12">모든 헤드는 하나의 암 어셈블리로 같이 움직임</text>
</svg>
```

- **플래터** 는 자성막을 입힌 원판, 양면이 각각 **표면**. 모든 플래터가 **스핀들** 에 묶여 일정한 **RPM** 으로 돈다(7,200~15,000). 10,000 RPM 이면 한 바퀴 6 ms.
- 표면에는 동심원 **트랙** 이 수만~수십만 개. 표면마다 **헤드** 가 하나, 모든 헤드는 **암** 하나에 달려 같이 움직인다.

### 3.1 단일 트랙: rotational delay

트랙 하나에 섹터 12개(0~11), 헤드가 섹터 6 위에 있다. 섹터 0 을 읽으려면 그냥 0 이 헤드 밑으로 올 때까지 기다리면 된다 — **rotational delay**. 한 바퀴를 R 이라 하면 6→0 은 약 R/2, 최악(바로 지나간 섹터 5)은 거의 R 이다.

### 3.2 여러 트랙: seek

트랙이 셋이면(바깥 0–11, 가운데 12–23, 안쪽 24–35), 안쪽 트랙 위의 헤드가 섹터 11 을 읽으려면 암을 바깥 트랙으로 옮겨야 한다 — **seek**. seek 은 가속 → 등속(coasting) → 감속 → **settle**(정확히 트랙 위에 자리 잡기) 단계가 있고, settle 만 0.5–2 ms 다. seek 하는 동안에도 원판은 돈다(책 그림 37.3 에서 약 3섹터). 도착 후 남은 회전을 기다리고, 섹터가 지나가는 동안 **transfer**.

### 3.3 그 밖의 디테일

- **Track skew**: 순차로 읽다가 트랙 경계를 넘을 때, 헤드를 옆 트랙으로 옮기는(짧은 seek) 동안 다음 섹터가 이미 지나가 버리면 거의 한 바퀴를 기다려야 한다. 그래서 다음 트랙의 섹터 번호를 seek 시간만큼 **엇갈려** 둔다. 필요한 skew(섹터 수) = ⌈인접 트랙 seek 시간 ÷ 섹터 하나 지나가는 시간⌉. 아래 시뮬레이터로 직접 확인한다.
- **Multi-zoned**: 바깥 트랙은 둘레가 길어서 섹터를 더 많이 넣는다. 같은 RPM 이면 바깥 zone 의 전송률이 더 높다(그래서 HDD 벤치마크 그래프는 LBA 0 쪽이 빠르다).
- **Cache(track buffer)**: 8~16MB(요즘은 256MB 이상) 드라이브 내부 메모리. 읽을 때 트랙 전체를 미리 읽어 두기(read-ahead)도 한다. 쓰기는 두 정책:
  - **write-back**(immediate reporting): 캐시에 넣는 순간 "완료" 보고. 빠르지만 전원이 나가면 사라질 수 있고, **순서가 바뀔 수 있다** → 저널링의 쓰기 순서 가정이 깨진다.
  - **write-through**: 매체에 기록한 뒤 보고.
  - 그래서 OS 는 `FLUSH CACHE`(ATA) / `SYNCHRONIZE CACHE`(SCSI) / NVMe `Flush`, 그리고 쓰기별 **FUA**(Force Unit Access) 비트로 순서·영속성을 강제한다. [Ch.39](2026-09-30_C39_files_and_directories.md)의 `fsync` 가 결국 이 명령까지 내려와야 의미가 있다.

## 4. 단위 맞추기 (Aside: Dimensional Analysis)

화학 시간의 "단위 소거" 방식으로 계산하면 실수가 없다.

```text
Time(ms)/Rotation = (1 min / 10,000 Rot) x (60 s / 1 min) x (1000 ms / 1 s) = 60,000/10,000 = 6 ms/Rot

Time(ms)/Request  = (512 KB / 1 Req) x (1 MB / 1024 KB) x (1 s / 100 MB) x (1000 ms / 1 s) = 5 ms/Req
```

요령: 원하는 단위를 왼쪽에 쓰고, 아는 비율을 분자/분모가 지워지도록 이어 붙인다.

## 5. I/O 시간 계산 (37.4)

### 5.1 두 디스크

| | Cheetah 15K.5 | Barracuda |
|---|---|---|
| 용량 | 300 GB | 1 TB |
| RPM | 15,000 | 7,200 |
| 평균 seek | 4 ms | 9 ms |
| 최대 전송률 | 125 MB/s | 105 MB/s |
| 플래터 | 4 | 4 |
| 캐시 | 16 MB | 16/32 MB |
| 인터페이스 | SCSI | SATA |

Cheetah 는 "성능 시장"(빨리 돌고 seek 짧음, 비쌈), Barracuda 는 "용량 시장"(GB 당 가격이 최우선).

### 5.2 랜덤 4KB 읽기 — 손으로 계산

**Cheetah**

- T_seek = 4 ms (제조사 평균)
- T_rotation: 15,000 RPM = 250 RPS → 1회전 4 ms → 평균 반 바퀴 **2 ms**
- T_transfer = 4 KB ÷ 125 MB/s = 4/(125×1024) s ≈ **31 µs** (책은 30 µs)
- T_I/O ≈ 4 + 2 + 0.03 = **6.03 ms** → R_I/O = 4 KB / 6.03 ms ≈ **0.66 MB/s**

**Barracuda**

- T_seek = 9 ms
- T_rotation: 7,200 RPM = 120 RPS → 1회전 8.33 ms → 평균 **4.17 ms**
- T_transfer = 4 KB ÷ 105 MB/s ≈ **37 µs**
- T_I/O ≈ 9 + 4.17 + 0.04 = **13.2 ms** → R_I/O ≈ **0.31 MB/s**

전송 시간은 1% 도 안 된다. 랜덤 I/O 는 **기계가 움직이는 시간** 이 전부다.

### 5.3 순차 100 MB 읽기

seek·rotation 은 처음 한 번뿐이고 나머지는 전송이다.

- Cheetah: 4 + 2 + 100/125 s(= 800 ms) = **806 ms** → ≈ 124 MB/s
- Barracuda: 9 + 4.17 + 100/105 s(= 952 ms) = **965 ms** → ≈ 104 MB/s

> 주의: 원문은 "Barracuda and Cheetah is about 800 ms and 950 ms, respectively" 라고 썼는데 **순서가 뒤바뀐 오타** 다. 125 MB/s 인 Cheetah 가 800 ms, 105 MB/s 인 Barracuda 가 950 ms 다.

| | Cheetah | Barracuda |
|---|---|---|
| R_I/O 랜덤 | 0.66 MB/s | 0.31 MB/s |
| R_I/O 순차 | 125 MB/s | 105 MB/s |
| 순차/랜덤 | ≈ 190배 | ≈ 340배 |

> **TIP — USE DISKS SEQUENTIALLY**: 가능하면 순차로. 안 되면 최소한 큰 덩어리로. 작은 랜덤 I/O 는 성능을 극적으로 망친다.

### 5.4 새 예제: 2020년대 20TB HDD

7,200 RPM, 평균 seek 8.5 ms, 최대 전송 270 MB/s 라고 하자.

- 랜덤 4KB: 8.5 + 4.17 + 0.014 = 12.68 ms → **79 IOPS**, 0.31 MB/s
- 순차: ≈ 261 MB/s
- 순차/랜덤 ≈ **850배**

15년 동안 순차 대역폭은 2.5배 늘었지만(선형 기록 밀도↑), seek·rotation 은 기계라서 거의 그대로다. 그래서 **랜덤 IOPS 는 70~80 에 묶여 있고**, 용량이 커질수록 "TB 당 IOPS" 는 오히려 나빠진다. 대용량 HDD 가 **SMR + 순차 쓰기 전용 zone(ZBC/ZAC)** 으로 가는 이유이고, 이 흐름이 SSD 의 ZNS 와도 이어진다.

### 5.5 Aside: 평균 seek 거리가 전체의 1/3 인 이유

트랙을 연속 구간 [0, N] 으로 보고, 출발 x 와 도착 y 가 독립 균등분포라면 평균 거리는

```text
E|x-y| = (1/N^2) ∫0^N ∫0^N |x-y| dy dx

안쪽 적분 (x 고정):
  ∫0^x (x-y) dy + ∫x^N (y-x) dy = x^2/2 + (N-x)^2/2 = x^2 - N x + N^2/2

바깥 적분:
  ∫0^N (x^2 - N x + N^2/2) dx = N^3/3 - N^3/2 + N^3/2 = N^3/3

E|x-y| = (N^3/3) / N^2 = N/3
```

확률로 보면 |x−y| 는 삼각분포 밀도 2(N−d)/N² 를 갖고 평균이 N/3 이다. 단, 이것은 **거리** 의 평균이다. seek **시간** 은 거리에 선형이 아니다(짧은 seek 은 가속·settle 이 지배 → 대략 √d, 긴 seek 은 등속 구간 → 대략 d). 그래서 "평균 seek 시간 = full seek 의 1/3" 은 근사일 뿐이고, 실제로는 1/3 보다 약간 크게 나온다(settle 고정비 때문에). 아래 C 코드에서 몬테카를로로 N/3 을 확인한다.

## 6. 디스크 스케줄링 (37.5)

I/O 가 비싸므로 OS 는 오래전부터 "다음에 어떤 요청을 낼지"를 정해 왔다. 프로세스 스케줄링과 다른 점: 디스크 요청은 **얼마나 걸릴지 추정할 수 있다**(seek 거리, 회전 위치). 그래서 디스크 스케줄러는 [SJF(Shortest Job First)](2026-09-30_C07_scheduling_intro.md)를 흉내 낸다.

### 6.1 SSTF (Shortest Seek Time First)

가장 가까운 트랙의 요청부터. 헤드가 안쪽 트랙에 있고 21(가운데)과 2(바깥) 요청이 있으면 21 → 2.

문제 두 가지:

1. **OS 는 지오메트리를 모른다.** OS 에게 디스크는 블록 배열이다. → 블록 번호가 가까운 것부터: **NBF(Nearest Block First)**. 쉽게 해결.
2. **기아(starvation).** 헤드 근처로 요청이 계속 들어오면 먼 요청은 영원히 밀린다.

> **CRUX — HOW TO HANDLE DISK STARVATION**: "How can we implement SSTF-like scheduling but avoid starvation?"

### 6.2 엘리베이터: SCAN, F-SCAN, C-SCAN

- **SCAN**: 디스크를 한쪽 끝에서 다른 끝으로 쓸고(sweep) 지나가며 그 길에 있는 요청을 처리, 끝에 닿으면 방향을 바꾼다. 이번 sweep 에서 이미 지나간 트랙에 들어온 요청은 **다음 sweep** 까지 기다린다 → 기아 없음.
- **F-SCAN**: sweep 하는 동안 큐를 **얼려서(freeze)**, sweep 도중 들어온(가까운) 요청이 먼 요청을 계속 밀어내지 못하게 한다.
- **C-SCAN(Circular)**: 한 방향으로만 처리하고 끝에 닿으면 처음으로 **되감는다**. 왕복 SCAN 은 가운데 트랙을 두 번 지나가서 가운데에 유리하므로, C-SCAN 이 안쪽·바깥쪽에 더 공평하다.

엘리베이터 비유: 10층에서 1층으로 내려가는데 3층에서 탄 사람이 4층을 눌렀다고 "가까우니까" 4층으로 올라가면 싸움이 난다. 엘리베이터 알고리즘은 엘리베이터에서는 싸움을, 디스크에서는 기아를 막는다.

### 6.3 예제: 큐 = 98, 183, 37, 122, 14, 124, 65, 67, 헤드 = 53, 트랙 0..199

| 정책 | 순서 | 총 헤드 이동 |
|---|---|---|
| FIFO | 53→98→183→37→122→14→124→65→67 | 45+85+146+85+108+110+59+2 = **640** |
| SSTF | 53→65→67→37→14→98→122→124→183 | 12+2+30+23+84+24+2+59 = **236** |
| SCAN (↑ 먼저, 끝 199 까지) | 53→…→183→199→37→14 | 146 + 185 = **331** |
| LOOK (마지막 요청에서 전환) | 53→…→183→37→14 | 130 + 169 = **299** |
| C-SCAN (↑, 199→0 되감기 포함) | 53→…→183→199→0→14→37 | 146 + 199 + 37 = **382** |

SSTF 가 이동 거리는 가장 짧지만 기아 위험이 있고, SCAN/C-SCAN 은 조금 더 움직이는 대신 **대기 시간의 상한** 이 생긴다. C-SCAN 의 382 중 199 는 "빈 손으로 되감기"인데, 실제 디스크에서 되감기는 긴 seek 한 번(요청 처리 없음)이라 거리만큼 비싸지는 않다.

```svg
<svg viewBox="0 0 700 400" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
<line x1="130" y1="22" x2="687" y2="22" stroke="currentColor"/>
<text x="10" y="18" fill="currentColor" font-size="11">track →</text>
<line x1="130" y1="18" x2="130" y2="26" stroke="currentColor"/><text x="130" y="14" text-anchor="middle" fill="currentColor" font-size="11">0</text>
<line x1="270" y1="18" x2="270" y2="26" stroke="currentColor"/><text x="270" y="14" text-anchor="middle" fill="currentColor" font-size="11">50</text>
<line x1="410" y1="18" x2="410" y2="26" stroke="currentColor"/><text x="410" y="14" text-anchor="middle" fill="currentColor" font-size="11">100</text>
<line x1="550" y1="18" x2="550" y2="26" stroke="currentColor"/><text x="550" y="14" text-anchor="middle" fill="currentColor" font-size="11">150</text>
<line x1="687" y1="18" x2="687" y2="26" stroke="currentColor"/><text x="687" y="14" text-anchor="middle" fill="currentColor" font-size="11">199</text>
<text x="10" y="79" fill="currentColor" font-weight="bold">FIFO (640)</text>
<rect x="126" y="34" width="565" height="82" fill="none" stroke="currentColor" stroke-opacity="0.25"/>
<polyline points="278,38 404,47 642,56 234,66 472,75 169,84 477,94 312,103 318,112" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
<circle cx="278" cy="38" r="3" fill="currentColor"/>
<circle cx="404" cy="47" r="3" fill="currentColor"/>
<circle cx="642" cy="56" r="3" fill="currentColor"/>
<circle cx="234" cy="66" r="3" fill="currentColor"/>
<circle cx="472" cy="75" r="3" fill="currentColor"/>
<circle cx="169" cy="84" r="3" fill="currentColor"/>
<circle cx="477" cy="94" r="3" fill="currentColor"/>
<circle cx="312" cy="103" r="3" fill="currentColor"/>
<circle cx="318" cy="112" r="3" fill="currentColor"/>
<text x="10" y="171" fill="currentColor" font-weight="bold">SSTF (236)</text>
<rect x="126" y="126" width="565" height="82" fill="none" stroke="currentColor" stroke-opacity="0.25"/>
<polyline points="278,130 312,139 318,148 234,158 169,167 404,176 472,186 477,195 642,204" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
<circle cx="278" cy="130" r="3" fill="currentColor"/>
<circle cx="312" cy="139" r="3" fill="currentColor"/>
<circle cx="318" cy="148" r="3" fill="currentColor"/>
<circle cx="234" cy="158" r="3" fill="currentColor"/>
<circle cx="169" cy="167" r="3" fill="currentColor"/>
<circle cx="404" cy="176" r="3" fill="currentColor"/>
<circle cx="472" cy="186" r="3" fill="currentColor"/>
<circle cx="477" cy="195" r="3" fill="currentColor"/>
<circle cx="642" cy="204" r="3" fill="currentColor"/>
<text x="10" y="263" fill="currentColor" font-weight="bold">SCAN (331)</text>
<rect x="126" y="218" width="565" height="82" fill="none" stroke="currentColor" stroke-opacity="0.25"/>
<polyline points="278,222 312,230 318,238 404,247 472,255 477,263 642,271 687,280 234,288 169,296" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
<circle cx="278" cy="222" r="3" fill="currentColor"/>
<circle cx="312" cy="230" r="3" fill="currentColor"/>
<circle cx="318" cy="238" r="3" fill="currentColor"/>
<circle cx="404" cy="247" r="3" fill="currentColor"/>
<circle cx="472" cy="255" r="3" fill="currentColor"/>
<circle cx="477" cy="263" r="3" fill="currentColor"/>
<circle cx="642" cy="271" r="3" fill="currentColor"/>
<circle cx="687" cy="280" r="3" fill="currentColor"/>
<circle cx="234" cy="288" r="3" fill="currentColor"/>
<circle cx="169" cy="296" r="3" fill="currentColor"/>
<text x="10" y="355" fill="currentColor" font-weight="bold">C-SCAN (382)</text>
<rect x="126" y="310" width="565" height="82" fill="none" stroke="currentColor" stroke-opacity="0.25"/>
<polyline points="278,314 312,321 318,329 404,336 472,344 477,351 642,358 687,366 130,373 169,381 234,388" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
<circle cx="278" cy="314" r="3" fill="currentColor"/>
<circle cx="312" cy="321" r="3" fill="currentColor"/>
<circle cx="318" cy="329" r="3" fill="currentColor"/>
<circle cx="404" cy="336" r="3" fill="currentColor"/>
<circle cx="472" cy="344" r="3" fill="currentColor"/>
<circle cx="477" cy="351" r="3" fill="currentColor"/>
<circle cx="642" cy="358" r="3" fill="currentColor"/>
<circle cx="687" cy="366" r="3" fill="currentColor"/>
<circle cx="130" cy="373" r="3" fill="currentColor"/>
<circle cx="169" cy="381" r="3" fill="currentColor"/>
<circle cx="234" cy="388" r="3" fill="currentColor"/>
<text x="10" y="398" fill="currentColor" font-size="11">세로 = 처리 순서(위→아래), 가로 = 트랙 번호, 헤드 시작 53</text>
</svg>
```

### 6.4 SPTF: 회전까지 계산에 넣기

> **CRUX — HOW TO ACCOUNT FOR DISK ROTATION COSTS**: "How can we implement an algorithm that more closely approximates SJF by taking both seek and rotation into account?"

SSTF/SCAN 은 **회전을 무시** 한다. 책 그림 37.8: 헤드가 안쪽 트랙 섹터 30 위, 후보는 16(가운데 트랙)과 8(바깥 트랙).

- seek 이 회전보다 훨씬 비싸면: 가까운 16 (SSTF 와 같은 답).
- seek 이 회전보다 충분히 싸면: 16 은 막 지나가서 거의 한 바퀴를 기다려야 하므로, **더 멀리 seek 해서 8** 을 잡는 게 빠르다.

답은 "**it depends**"(Livny's law). 현대 디스크는 seek 과 rotation 이 비슷한 크기라서 둘을 합친 **위치결정 시간(positioning time)** 이 가장 짧은 요청을 고르는 **SPTF**(= SATF, shortest access time first)가 유리하다. 하지만 OS 는 트랙 경계도, 헤드의 회전 각도도 모른다. 그래서 SPTF 는 **드라이브 펌웨어** 가 한다.

### 6.5 그 밖의 쟁점

- **어디서 스케줄하나?** 옛날엔 OS 가 하나씩 골라 냈다. 지금은 디스크가 여러 요청을 동시에 받고(SCSI TCQ, SATA **NCQ** 32개) 내부에서 SPTF 로 정렬한다. OS 는 "괜찮아 보이는 몇 개(예: 16)"를 내려보낼 뿐이다.
- **I/O 병합(merging)**: 33, 8, 34 요청이 오면 33+34 를 2블록 요청 하나로 합친다. 요청 수 자체를 줄이므로 OS 레벨에서 특히 중요하다(Linux 블록 계층의 front/back merge).
- **work-conserving vs non-work-conserving**: 요청이 하나라도 있으면 바로 내는 것이 work-conserving. **anticipatory scheduling** 은 "곧 바로 옆 블록 요청이 올 것 같으면" 잠깐 **기다린다**. 동기 순차 읽기를 하는 프로세스가 다음 요청을 내기 직전에 디스크가 먼 요청으로 가 버리는 "deceptive idleness" 를 막는다. 언제·얼마나 기다릴지가 어렵다.
- 오늘날 Linux 는 HDD 에 `mq-deadline`/`bfq`, NVMe SSD 에는 보통 `none`(스케줄링 안 함 — seek 이 없고, 장치 내부 병렬성을 FW 가 더 잘 안다)을 쓴다.

## 7. 직접 해보기

### 7.1 C: 공식 계산 + 평균 seek 몬테카를로 + 스케줄러 비교

```bash
cc -Wall -Wextra -O0 code/C37_disk_math.c -o .work/bin/C37_disk_math
.work/bin/C37_disk_math
```

```c
// C37_disk_math.c — OSTEP Ch.37: 디스크 I/O 시간 계산 + 평균 seek 거리 + 디스크 스케줄러 비교
//
//  1) T_IO = T_seek + T_rotation + T_transfer  (Cheetah 15K.5 vs Barracuda, + 현대 20TB HDD)
//  2) 평균 seek 거리 = N/3 을 몬테카를로로 확인
//  3) FIFO / SSTF / SCAN / LOOK / C-SCAN 의 총 head 이동 거리(트랙 수) 비교 + SSTF starvation 데모
//
// build: cc -Wall -Wextra -O0 code/C37_disk_math.c -o .work/bin/C37_disk_math
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct drive { const char *name; double rpm, seek_ms, xfer_MBps; };

static double io_time(const struct drive *d, double size_KB, int sequential) {
    double rot_full = 60.0 * 1000.0 / d->rpm;          // ms per rotation
    double t_rot = rot_full / 2.0;                       // 평균 반 바퀴
    double t_xfer = size_KB / 1024.0 / d->xfer_MBps * 1000.0; // ms
    double t = d->seek_ms + t_rot + t_xfer;
    double rate = size_KB / 1024.0 / (t / 1000.0);      // MB/s
    printf("  %-22s %-10s size %8.0f KB : seek %5.2f + rot %5.2f + xfer %8.3f = %8.3f ms -> %7.2f MB/s\n",
           d->name, sequential ? "sequential" : "random", size_KB, d->seek_ms, t_rot, t_xfer, t, rate);
    return rate;
}

// ---------- 스케줄링 (트랙 번호만 보는 단순 모델: 이동 거리 = |a-b|)
#define MAXQ 64
static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

// path[] 에 head 가 지나가는 지점(요청 + 끝 트랙 왕복 지점)을 기록하고 총 이동 거리 반환
static int schedule(const char *name, int start, const int *req, int n, int maxtrack) {
    int path[MAXQ * 2], np = 0, s[MAXQ], done[MAXQ] = {0};
    memcpy(s, req, sizeof(int) * (size_t)n);
    if (!strcmp(name, "FIFO")) {
        for (int i = 0; i < n; i++) path[np++] = req[i];
    } else if (!strcmp(name, "SSTF")) {
        int head = start;
        for (int k = 0; k < n; k++) {
            int best = -1;
            for (int i = 0; i < n; i++)
                if (!done[i] && (best < 0 || abs(req[i] - head) < abs(req[best] - head))) best = i;
            done[best] = 1; head = req[best]; path[np++] = head;
        }
    } else {
        qsort(s, (size_t)n, sizeof(int), cmp_int);
        int lo = 0; while (lo < n && s[lo] < start) lo++;      // s[lo..] >= start
        for (int i = lo; i < n; i++) path[np++] = s[i];          // 바깥쪽(큰 번호)으로 sweep
        if (lo > 0) {
            if (!strcmp(name, "SCAN")) {                          // 끝까지 갔다가 방향 전환
                path[np++] = maxtrack;
                for (int i = lo - 1; i >= 0; i--) path[np++] = s[i];
            } else if (!strcmp(name, "LOOK")) {                   // 마지막 요청에서 바로 전환
                for (int i = lo - 1; i >= 0; i--) path[np++] = s[i];
            } else {                                              // C-SCAN: 끝 → 0 으로 복귀 후 같은 방향
                path[np++] = maxtrack; path[np++] = 0;
                for (int i = 0; i < lo; i++) path[np++] = s[i];
            }
        }
    }
    int head = start, total = 0;
    printf("  %-6s: %d", name, start);
    for (int i = 0; i < np; i++) { total += abs(path[i] - head); head = path[i]; printf(" %d", head); }
    printf("\n          total head movement = %d tracks\n", total);
    return total;
}

int main(void) {
    struct drive cheetah = { "Cheetah 15K.5 (SCSI)", 15000, 4.0, 125 };
    struct drive cuda    = { "Barracuda (SATA)",      7200, 9.0, 105 };
    struct drive modern  = { "20TB 7200rpm (2020s)",  7200, 8.5, 270 };   // 새 예제
    printf("[1] I/O time (Eq. 37.1)\n");
    struct drive *ds[] = { &cheetah, &cuda, &modern };
    for (int i = 0; i < 3; i++) {
        double r = io_time(ds[i], 4, 0), sq = io_time(ds[i], 100 * 1024, 1);
        printf("  -> %s: sequential/random = %.0fx, random 4KB IOPS = %.0f\n", ds[i]->name, sq / r, r * 1024 / 4);
    }

    printf("\n[2] average seek distance (Monte Carlo, N=1000 tracks, 10M pairs)\n");
    unsigned long long x = 88172645463325252ull, sum = 0;   // xorshift64
    const int N = 1000; const long PAIRS = 10000000;
    for (long i = 0; i < PAIRS; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17; int a = (int)(x % N);
        x ^= x << 13; x ^= x >> 7; x ^= x << 17; int b = (int)(x % N);
        sum += (unsigned long long)abs(a - b);
    }
    printf("  mean |x-y| = %.2f tracks  (N/3 = %.2f)\n", (double)sum / PAIRS, N / 3.0);

    printf("\n[3] disk scheduling, head at track 53, tracks 0..199\n");
    int q[] = { 98, 183, 37, 122, 14, 124, 65, 67 };
    int n = (int)(sizeof q / sizeof q[0]);
    const char *pol[] = { "FIFO", "SSTF", "SCAN", "LOOK", "C-SCAN" };
    for (int i = 0; i < 5; i++) schedule(pol[i], 53, q, n, 199);

    printf("\n[4] SSTF starvation: head at 50, request for track 190 waits while near requests keep arriving\n");
    int head = 50, served = 0, far_waiting = 1;
    for (int t = 0; t < 12; t++) {
        int near = 45 + (t * 7) % 12;               // 근처 트랙 요청이 계속 들어온다
        if (abs(near - head) < abs(190 - head)) { head = near; served++; }
        else { far_waiting = 0; break; }
    }
    printf("  near requests served = %d, track 190 still waiting = %s\n", served, far_waiting ? "YES" : "no");
    return 0;
}
```

실제 출력:

```text
[1] I/O time (Eq. 37.1)
  Cheetah 15K.5 (SCSI)   random     size        4 KB : seek  4.00 + rot  2.00 + xfer    0.031 =    6.031 ms ->    0.65 MB/s
  Cheetah 15K.5 (SCSI)   sequential size   102400 KB : seek  4.00 + rot  2.00 + xfer  800.000 =  806.000 ms ->  124.07 MB/s
  -> Cheetah 15K.5 (SCSI): sequential/random = 192x, random 4KB IOPS = 166
  Barracuda (SATA)       random     size        4 KB : seek  9.00 + rot  4.17 + xfer    0.037 =   13.204 ms ->    0.30 MB/s
  Barracuda (SATA)       sequential size   102400 KB : seek  9.00 + rot  4.17 + xfer  952.381 =  965.548 ms ->  103.57 MB/s
  -> Barracuda (SATA): sequential/random = 350x, random 4KB IOPS = 76
  20TB 7200rpm (2020s)   random     size        4 KB : seek  8.50 + rot  4.17 + xfer    0.014 =   12.681 ms ->    0.31 MB/s
  20TB 7200rpm (2020s)   sequential size   102400 KB : seek  8.50 + rot  4.17 + xfer  370.370 =  383.037 ms ->  261.07 MB/s
  -> 20TB 7200rpm (2020s): sequential/random = 848x, random 4KB IOPS = 79

[2] average seek distance (Monte Carlo, N=1000 tracks, 10M pairs)
  mean |x-y| = 333.51 tracks  (N/3 = 333.33)

[3] disk scheduling, head at track 53, tracks 0..199
  FIFO  : 53 98 183 37 122 14 124 65 67
          total head movement = 640 tracks
  SSTF  : 53 65 67 37 14 98 122 124 183
          total head movement = 236 tracks
  SCAN  : 53 65 67 98 122 124 183 199 37 14
          total head movement = 331 tracks
  LOOK  : 53 65 67 98 122 124 183 37 14
          total head movement = 299 tracks
  C-SCAN: 53 65 67 98 122 124 183 199 0 14 37
          total head movement = 382 tracks

[4] SSTF starvation: head at 50, request for track 190 waits while near requests keep arriving
  near requests served = 12, track 190 still waiting = YES
```

해석: 5.2–5.4 의 손계산이 그대로 나온다(1MB = 1024KB 로 계산해 원문 반올림과 소수점이 조금 다르다). 1,000만 쌍의 평균 |x−y| 는 333.51 로 N/3 = 333.33 에 붙는다. 스케줄러 표는 6.3 과 같고, [4] 는 SSTF 가 근처 요청 12개를 처리하는 동안 트랙 190 요청이 한 번도 선택되지 않는 기아를 보여 준다.

### 7.2 OSTEP 시뮬레이터: disk.py (텍스트 모드)

기본 디스크: 트랙 3개 × 섹터 12개, 섹터당 30°, 회전 1°/시간단위(1바퀴 360), 트랙 간 거리 40, seek 속도 1(→ 인접 트랙 seek 40). 헤드는 바깥 트랙 섹터 6 의 한가운데에서 시작. `-G` 를 빼면 그래픽 없이 돈다. (참고: 이 버전의 disk.py 는 디버그용 `z 0 30`, `0 30 0` 같은 지오메트리 줄을 먼저 찍는다. 아래 출력에서는 OPTIONS/지오메트리 줄을 생략했다.)

**① FIFO vs SSTF (숙제 1, 4)**

```bash
cd .tools/ostep-homework/file-disks
python3 disk.py -a 7,30,8 -c
python3 disk.py -a 7,30,8 -p SSTF -c
```

```text
REQUESTS [7, 30, 8]
Block:   7  Seek:  0  Rotate: 15  Transfer: 30  Total:  45
Block:  30  Seek: 80  Rotate:220  Transfer: 30  Total: 330
Block:   8  Seek: 80  Rotate:310  Transfer: 30  Total: 420
TOTALS      Seek:160  Rotate:545  Transfer: 90  Total: 795

REQUESTS [7, 30, 8]
Block:   7  Seek:  0  Rotate: 15  Transfer: 30  Total:  45
Block:   8  Seek:  0  Rotate:  0  Transfer: 30  Total:  30
Block:  30  Seek: 80  Rotate:190  Transfer: 30  Total: 300
TOTALS      Seek: 80  Rotate:205  Transfer: 90  Total: 375
```

손으로 맞춰 보기(FIFO):

- 블록 7: 헤드가 섹터 6 의 중간 → 6/7 경계까지 15, 전송 30. 끝나면 헤드는 7/8 경계.
- 블록 30(안쪽 트랙, 2트랙 거리): seek 80. 안쪽 트랙은 바깥 섹터 6 과 30 이 같은 각도에 있으므로(가운데 트랙은 +12, 안쪽은 +24), 7/8 경계 = 안쪽 31/32 경계. 거기서 30 시작(29/30 경계)까지 32,33,34,35,24,…,29 = 10섹터 = 300. 그중 80 은 seek 중에 지나가니 회전 대기 = 300 − 80 = **220**.
- 블록 8(다시 바깥): 30 이 끝나면 30/31 경계 = 바깥 6/7 경계. 8 의 시작(7/8 경계)까지 1섹터 = 30 인데, seek 80 이 그보다 길어서 놓친다 → 30 + 360 − 80 = **310**.

SSTF 는 8 을 7 바로 다음에 읽어 회전 0, 그 뒤 30 으로 가서 270 − 80 = 190. 총 795 → 375.

**② SSTF 와 SATF 가 갈리는 경우 (숙제 5)**

`-a 7,30,8` 에서는 SATF 도 SSTF 와 똑같이 375 가 나온다(직접 돌려 확인). 차이를 만들려면 "같은 트랙이지만 회전으로는 멀리 있는 요청" vs "다른 트랙이지만 회전으로 가까운 요청" 을 주고, **seek 을 회전보다 싸게** 만들어야 한다(6.4 의 조건).

```bash
python3 disk.py -a 5,20 -S 40 -p SSTF -c
python3 disk.py -a 5,20 -S 40 -p SATF -c
```

```text
Block:   5  Seek:  0  Rotate:315  Transfer: 30  Total: 345
Block:  20  Seek:  1  Rotate: 59  Transfer: 30  Total:  90
TOTALS      Seek:  1  Rotate:374  Transfer: 60  Total: 435

Block:  20  Seek:  1  Rotate: 44  Transfer: 30  Total:  75
Block:   5  Seek:  1  Rotate:239  Transfer: 30  Total: 270
TOTALS      Seek:  2  Rotate:283  Transfer: 60  Total: 345
```

SSTF 는 같은 트랙(seek 0)인 5 를 고르는데, 5 는 헤드 바로 뒤라 315 를 돌아야 한다. SATF 는 seek 1 + 회전 44 로 20 을 먼저 집고 돌아온다. 435 → 345, **21% 단축**. 조건: seek 비용이 회전 비용에 비해 작고, 같은 트랙 요청이 회전상 불리할 때.

**③ Track skew (숙제 6)**

```bash
python3 disk.py -a 10,11,12,13 -c
python3 disk.py -a 10,11,12,13 -o 2 -c
```

```text
Block:  10  Seek:  0  Rotate:105  Transfer: 30  Total: 135
Block:  11  Seek:  0  Rotate:  0  Transfer: 30  Total:  30
Block:  12  Seek: 40  Rotate:320  Transfer: 30  Total: 390
Block:  13  Seek:  0  Rotate:  0  Transfer: 30  Total:  30
TOTALS      Seek: 40  Rotate:425  Transfer:120  Total: 585

Block:  10  Seek:  0  Rotate:105  Transfer: 30  Total: 135
Block:  11  Seek:  0  Rotate:  0  Transfer: 30  Total:  30
Block:  12  Seek: 40  Rotate: 20  Transfer: 30  Total:  90
Block:  13  Seek:  0  Rotate:  0  Transfer: 30  Total:  30
TOTALS      Seek: 40  Rotate:125  Transfer:120  Total: 285
```

skew 없이: 11 이 끝난 순간 헤드는 가운데 트랙 12 의 시작 각도에 정확히 있다. 하지만 seek 40 동안 12 가 지나가 버려서 360 − 40 = **320** 을 기다린다. skew 2 섹터(60 단위)를 주면 12 가 60 만큼 늦게 오므로 seek 40 뒤 **20** 만 기다린다. 585 → 285.

일반식: `skew(섹터) = ceil( (트랙 간 거리 / seek 속도) / (섹터당 각도 / 회전 속도) )`. 기본값은 ⌈40/30⌉ = **2**. `-S 2`(seek 20)이면 ⌈20/30⌉ = 1, `-S 4`(seek 10)이면 1 이면 충분하다.

## 8. 펌웨어 엔지니어의 눈으로

- **SPTF 는 "펌웨어가 안다"는 논리의 원조.** HDD FW 는 헤드 각도·트랙 경계·seek 프로파일을 알기 때문에 NCQ 32개 안에서 RPO(rotational position ordering)를 한다. SSD FW 에는 seek 이 없지만 같은 구조의 문제가 있다: 어떤 **die/plane 이 지금 idle 인지**, 어떤 die 가 tPROG(수백 µs)·tBERS(수 ms)로 묶여 있는지 FW 만 안다. 그래서 NVMe SSD 는 호스트 스케줄러를 `none` 으로 두고, FW 의 die-level 스케줄러(읽기 우선, program/erase **suspend-resume**, GC 와 호스트 I/O 의 대역폭 배분)가 "SPTF 의 SSD 판"을 한다.
- **write-back cache 의 전원 손실 문제는 SSD 가 그대로 물려받았다.** HDD 의 immediate reporting 처럼 SSD 도 DRAM/SRAM 쓰기 버퍼에서 완료를 보고한다. 엔터프라이즈 SSD 는 **PLP(power-loss protection) 캐패시터** 로 버퍼를 NAND 에 덤프할 시간을 벌고(그래서 `Volatile Write Cache` 를 not present 로 보고하기도 함), 클라이언트 SSD 는 VWC 를 보고하고 호스트의 `Flush`/FUA 에 의존한다. Flush 를 받으면 FW 는 버퍼 데이터 + **매핑 테이블 변경분** 까지 NAND 에 내려야 진짜 영속이다.
- **torn write 와 원자 쓰기 단위.** HDD 는 512B 섹터만 원자적이다. NVMe 는 이를 **AWUN/AWUPF/NAWUPF**(Atomic Write Unit Normal / Power Fail)로 명시한다. FW 입장에서 "4KB 쓰기가 전원 손실에도 전부-아니면-전무" 를 보장하려면 L2P 갱신을 데이터 program 완료 **뒤에** 한 번에(혹은 저널로) 해야 한다 — log-structured FTL 이면 자연스럽게 된다.
- **512e / 4Kn.** HDD 가 4KB 물리 섹터로 바뀌면서 512B 논리 섹터를 흉내 내는 512e 는 정렬 안 된 쓰기에 내부 read-modify-write 를 한다. SSD FW 도 4KB 매핑 단위보다 작은 쓰기(512B LBA format)에 같은 RMW 를 하고, 이게 WAF 를 키운다. 파티션 정렬(1MiB)이 여전히 중요한 이유다.
- **zone 의 귀환.** multi-zoned 디스크에서 "바깥이 빠르다"던 zone 개념이 SMR 에서는 "순차로만 써야 하는 zone"이 되었고(ZBC/ZAC), 같은 인터페이스가 NVMe **ZNS** 로 SSD 에 왔다. ZNS 는 FTL 의 GC 를 호스트로 넘기는 설계라 부록 I 의 WAF 이야기와 직결된다.

## 9. 면접 질문

### Q1. 7,200 RPM, 평균 seek 9 ms, 150 MB/s 인 디스크의 랜덤 4KB IOPS 와 순차 대역폭은? 랜덤 대역폭은?
<details>
<summary>답 보기</summary>

1회전 = 60/7200 s = **8.33 ms** → 평균 회전 지연 **4.17 ms**. 4KB 전송 = 4/(150×1024) s ≈ **0.026 ms**. T = 9 + 4.17 + 0.03 ≈ **13.2 ms** → **약 76 IOPS**, 랜덤 대역폭 ≈ 76 × 4KB ≈ **0.30 MB/s**. 순차는 seek/rotation 이 상각되므로 ≈ **150 MB/s**. 비율 약 500배 → "랜덤을 순차로 바꾸는" 설계(로그 구조, 쓰기 버퍼링, 병합)의 동기.

</details>

### Q2. SSTF 의 문제점과 해결책은? 그리고 SCAN 마저 최적이 아닌 이유는?
<details>
<summary>답 보기</summary>

SSTF 는 **기아**(가까운 요청이 계속 오면 먼 요청이 영원히 대기)와 **지오메트리 미지**(OS 는 트랙을 모름 → NBF 로 근사) 문제가 있다. 기아는 **SCAN/C-SCAN/F-SCAN** 처럼 sweep 단위로 처리해 대기 상한을 둔다. 하지만 SSTF·SCAN 모두 **회전 지연을 무시** 한다. seek 과 회전이 비슷한 현대 디스크에서는 둘을 합친 positioning time 최소를 고르는 **SPTF** 가 낫고, 이는 헤드 위치를 아는 **드라이브 내부** 에서 NCQ 로 구현된다.

</details>

### Q3. 디스크 쓰기 캐시(write-back)가 파일시스템 일관성에 왜 위험한가? OS 는 어떻게 대처하나?
<details>
<summary>답 보기</summary>

write-back 은 캐시에 들어가자마자 완료를 보고하므로 (1) 전원 손실 시 "완료된" 쓰기가 사라지고 (2) 드라이브가 **순서를 바꿔** 기록할 수 있다. 저널링은 "저널 블록 → commit 블록 → 제자리 쓰기" 순서에 의존하므로 순서가 깨지면 복구가 틀린다. 대처: **FLUSH CACHE**/NVMe Flush 로 장벽을 만들고, commit 블록은 **FUA** 로 쓰거나, 쓰기 캐시를 끈다. 엔터프라이즈 장치는 PLP 로 캐시를 사실상 비휘발로 만든다.

</details>

### Q4. 평균 seek 거리가 전체의 1/3 인 걸 유도해 보라. 평균 seek "시간"도 1/3 인가?
<details>
<summary>답 보기</summary>

x, y ~ U[0,N] 독립이면 E|x−y| = (1/N²)∫∫|x−y| = (1/N²)·N³/3 = **N/3**. 안쪽 적분이 x² − Nx + N²/2, 바깥 적분이 N³/3. 하지만 seek **시간** 은 거리의 선형 함수가 아니다: 짧은 seek 은 가속·감속·**settle 고정비** 가 지배(≈ a + b√d), 긴 seek 은 등속 구간이 지배(≈ a + c·d). 그래서 평균 seek 시간은 full seek 의 1/3 보다 다소 크다(고정비 때문). 제조사 "평균 seek" 은 보통 랜덤 seek 실측 평균이다.

</details>

### Q5. NVMe SSD 에서는 왜 OS 의 I/O 스케줄러를 보통 none 으로 두나? 그래도 OS 가 하는 일은?
<details>
<summary>답 보기</summary>

SSD 엔 seek/rotation 이 없어 **요청 순서 재배치로 얻는 이득이 작고**, 스케줄러 자체의 락·CPU 비용이 µs 급 장치에서는 오히려 병목이다. 내부 병렬성(채널·die·plane)과 GC 상태는 **FW 가 더 잘 안다**(멀티 큐 + 깊은 QD 를 주면 FW 가 die 별로 배분). 그래도 OS 는 **merging**(작은 순차 요청 병합), **blk-mq 의 CPU 별 큐 매핑**, 필요하면 **공정성/QoS**(BFQ, io.cost, io.latency cgroup), 그리고 discard/flush 순서 관리를 한다.

</details>

## 10. 자가 점검 & 숙제

### 퀴즈

**퀴즈 1.** 10,000 RPM 디스크의 평균 회전 지연은?

<details>
<summary>정답</summary>

1회전 6 ms → 평균 **3 ms**.

</details>

**퀴즈 2.** 랜덤 4KB 에서 seek·rotation·transfer 중 가장 작은 것은? 대략 몇 %?

<details>
<summary>정답</summary>

transfer. Cheetah 기준 0.03/6.03 ≈ **0.5%**.

</details>

**퀴즈 3.** 디스크가 원자적으로 보장하는 쓰기 단위는? 그보다 큰 쓰기가 반만 된 상태의 이름은?

<details>
<summary>정답</summary>

섹터(512B) 하나. **torn write**.

</details>

**퀴즈 4.** disk.py 기본 설정(seek 40, 섹터당 30)에서 `-a 10,11,12,13` 을 최적화하는 skew 는? `-S 4` 라면?

<details>
<summary>정답</summary>

⌈40/30⌉ = **2**. `-S 4` 면 인접 트랙 seek 이 10 이라 ⌈10/30⌉ = **1**.

</details>

**퀴즈 5.** C-SCAN 이 SCAN 보다 공평한 이유는?

<details>
<summary>정답</summary>

SCAN 은 왕복하며 가운데 트랙을 두 번 지나가므로 가운데 요청의 평균 대기가 짧다. C-SCAN 은 한 방향으로만 처리하고 되감으므로 모든 트랙의 재방문 간격이 같다.

</details>

### 숙제 (원문 Homework 에서 고른 것)

- **Q2/Q3 (`-S`, `-R` 변화)**: seek 속도와 회전 속도를 바꾸면 어느 요청 스트림에서 seek 이, 어느 스트림에서 rotation 이 지배하는지 → 6.4 "it depends" 의 감각을 숫자로 잡는 문제.
- **Q8 (scheduling window `-w`)**: `-A 1000,-1,0 -p SATF -w 1 … 1000` 로 window 크기와 총 시간의 곡선을 그려, NCQ 32 정도면 최적에 얼마나 가까운지 확인하는 문제. `-w 1` 이면 정책이 무의미해진다는 것도 확인.
- **Q9 (BSATF)**: SATF 에서 특정 섹터를 계속 굶기는 요청열을 만들고, `-p BSATF -w 4` 가 기아를 막는 대신 성능을 얼마나 잃는지 보는 문제 → 공정성 vs 처리량 trade-off.

## 11. 다음으로

디스크 하나의 성능 모델(T_I/O)이 생겼으니, 이제 디스크 **여러 개** 를 묶어 더 크고, 빠르고, 믿을 만한 "가상의 디스크"를 만드는 방법 → [Ch.38 RAID](2026-09-30_C38_raid.md). 그리고 이 장의 "seek 이 없는 장치라면?" 이라는 질문의 답은 [부록 I Flash SSD](2026-09-30_C0I_flash_ssd.md)에 있다.
