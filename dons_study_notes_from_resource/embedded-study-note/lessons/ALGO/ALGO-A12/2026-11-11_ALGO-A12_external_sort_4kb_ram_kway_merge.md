---
id: ALGO-A12
time: 120분
updated: 2026-11-11
---
# ALGO-A12 메모리 제약 외부 정렬: 4KB RAM으로 대용량 정렬

## 학습 목표

- RAM 예산을 **코드로 강제**한다: `static uint8_t g_ram[4096]` 하나에서만 버퍼·힙·상태를 잘라 쓰고, 넘으면 `abort()`. Flash 는 파일로 흉내내되 256 B 블록 단위 `pread/pwrite` 만 쓰고 블록 수를 센다 (stdio 는 자기 버퍼 4096 B 를 몰래 잡는다 — 이 맥에서 실측).
- 런 생성 두 가지를 구현하고 비교한다: **load-sort-write**(RAM 을 채워 안정 정렬, 런 = 256 레코드)와 **교체 선택**(replacement selection, 힙 208 레코드 → 무작위 입력에서 평균 런 415 = 1.995×, 정렬 입력 1개, 역순 입력 정확히 208).
- **힙 기반 k-way 병합**에서 k 를 RAM / 블록 크기로 정하고(4 KB, 256 B → k = 13), 패스 수 P = ⌈log_k R⌉ 를 **정수로** 계산한다. 실제 패스 수와 읽기/쓰기 바이트를 이론값 D·(1+P) 와 **바이트 단위로 정확히** 맞춘다 (1M 레코드: 80 MiB 읽기, 80 MiB 쓰기).
- 정확성을 "정렬됨 + 입력의 순열(개수 + 순서 무관 multiset 해시) + payload 무결성 + 안정성"으로 검사하고, 주입한 버그 7개가 모두 잡히는 것을 확인한다.
- 면접 꼬리질문(왜 k 를 그렇게, 블록 크기 trade-off, 안정성, 전원 차단, Flash 수명, 패스 하나 줄이기)에 숫자로 답한다.

## 1. 문제

"RAM 4 KB 짜리 코프로세서가 16 MiB 로그를 정렬해야 한다" 는 극단적이지만, 면접관이 보는 것은 하나다: **메모리 계층에서 데이터를 몇 번 옮기는지를 설계하고 증명할 수 있나.** 펌웨어에서는 그 횟수가 곧 Flash 쓰기량 = 수명이다. 이 레슨은 Don 의 SSD 경험(페이지 단위 I/O, 쓰기 증폭)과 정렬 알고리즘(ALGO-B07 머지소트, DS-B12 힙)을 한 문제로 묶는다.

외부 정렬(external sort)의 뼈대는 두 단계다.

1. **런 생성**(run generation): RAM 에 들어가는 만큼 읽어 정렬한 조각(run)을 Flash 에 쓴다.
2. **k-way 병합**(k-way merge): 런 k 개의 머리를 힙에 넣고 가장 작은 것부터 꺼내 하나의 더 긴 런으로 쓴다. 런이 1개가 될 때까지 반복 — 한 번 반복이 **패스**(pass)다.

모든 패스가 데이터 전체를 한 번 읽고 한 번 쓰므로 비용은 거의 "패스 수 × 데이터 크기" 로 정해진다. 그래서 질문은 결국 **(a) 런을 얼마나 길게 만들 수 있나 (b) 한 번에 몇 개를 합칠 수 있나(k)** 두 개다. 둘 다 RAM 4096 B 가 정한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">4096 B 아레나를 단계마다 어떻게 자르나 (1 px ≈ 6.8 B, 실제 ram_alloc 순서)</text>
  <text x="10" y="58" fill="currentColor">load-sort-write</text>
  <rect x="120" y="40" width="600" height="30" class="f-acc" stroke="currentColor"/>
  <text x="420" y="60" fill="currentColor" text-anchor="middle">레코드 256개 = 4096 B (블록을 여기로 바로 읽고, 정렬하고, 바로 쓴다)</text>
  <text x="10" y="128" fill="currentColor">교체 선택</text>
  <rect x="120" y="110" width="37.5" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="157.5" y="110" width="37.5" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="195" y="110" width="37.5" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="232.5" y="110" width="487.5" height="30" class="f-acc" stroke="currentColor"/>
  <text x="138" y="130" fill="currentColor" text-anchor="middle">in</text>
  <text x="176" y="130" fill="currentColor" text-anchor="middle">out</text>
  <text x="213" y="130" fill="currentColor" text-anchor="middle">dir</text>
  <text x="476" y="130" fill="currentColor" text-anchor="middle">레코드 힙 208개 = 3328 B (이번 런 | 얼린 것)</text>
  <text x="10" y="198" fill="currentColor">k-way 병합</text>
  <rect x="120" y="180" width="487.5" height="30" class="f-ok" stroke="currentColor"/>
  <text x="363" y="200" fill="currentColor" text-anchor="middle">입력 블록 13개 = 3328 B (런마다 하나)</text>
  <rect x="607.5" y="180" width="37.5" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="645" y="180" width="37.5" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="682.5" y="180" width="23.4" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="705.9" y="180" width="5" height="30" class="f-warn" stroke="currentColor"/>
  <text x="626" y="200" fill="currentColor" text-anchor="middle">out</text>
  <text x="663" y="200" fill="currentColor" text-anchor="middle">dir</text>
  <text x="730" y="232" fill="currentColor" text-anchor="end">런 상태 160 B · 힙 32 B · 남는 64 B</text>
  <text x="10" y="262" fill="currentColor">k = 14 면 블록만 16 × 256 = 4096 B → 상태·힙 자리가 없다 → k = 13 (merge_ram_need(13) = 4032)</text>
  <text x="10" y="284" fill="currentColor">스택: 데이터 배열 없음, 스칼라 지역 변수만 (Cortex-M4 프레임 32~68 B, 5. 풀이)</text>
</svg>
```

**모델 (이 레슨이 정한 것).**

| 항목 | 값 | 이유 |
|---|---|---|
| 레코드 | 16 B: `key`(uint32) + `seq` + `pay0` + `pay1` | 센서 로그 한 줄. `seq` = 원래 위치(안정성 검사), `pay` = `key·seq` 에서 만든 검사값(찢어진 복사 검출) |
| RAM | `g_ram[4096]`, `ram_alloc` 이 넘으면 `abort()` | "코드로 강제". 하네스(검증 쪽) 메모리는 예산 밖 |
| Flash 블록 | 256 B = 레코드 16개 | SPI NOR 의 page program 단위(256 B)와 같게. 읽기/쓰기 모두 블록 단위 |
| Flash 파일 | `argv[1]`, 없으면 `$TMPDIR/algo_a12_flash.bin` | 레슨 폴더에 데이터 파일을 만들지 않는다. 끝나면 `unlink` |
| 런 배치 | 런은 항상 블록 경계에서 시작, 마지막 블록 남는 칸 = `0xFF`(지운 Flash 값) | 블록 단위 쓰기만 허용하니까. 대가: 런마다 최대 1블록 낭비(패딩) |
| 런 디렉터리 | 런 길이(uint32)들을 Flash 의 별도 영역에 | 런 4096개 × 4 B = 16 KB → RAM 에 못 둔다 |
| 영역 | 입력 · 런 영역 2개 · 디렉터리 2개, 패스마다 0 ↔ 1 핑퐁 | 입력은 끝까지 안 건드린다 (Q7 전원 차단) |

### Part A — 예산과 런 생성

- `sort_run(a, n)` — RAM 안 **안정** 정렬(삽입 정렬, 추가 메모리 0).
- `gen_runs_lsw(x)` — 4 KB 를 입력 블록 16개로 채워 `sort_run` → 런 영역 0 에 쓰기. 런 = ⌈N/256⌉ 개. 디렉터리는 런을 다 쓴 뒤 같은 RAM 으로.
- `gen_runs_rs(x)` — 교체 선택. 블록 버퍼 3개(in/out/dir)를 빼고 남는 3328 B = 레코드 208개를 힙으로. 힙 배열 하나를 "이번 런 힙 | 다음 런으로 얼린 것" 으로 나눠 쓴다.
- 테스트: 예산 상수(k = 13, 힙 208), 예산 초과 시 자식 프로세스가 SIGABRT 로 죽는지(`fork`), 정확히 4096 B 는 허용되는지, `sort_run` 400개를 `qsort((key, seq))` 와 레코드 단위 비교.

### Part B — k-way 병합, 패스 수, 증명

- `merge_pass(x, src, R, k)` — 영역 src 의 런 R 개를 k 개씩 묶어 병합 → 영역 src^1. 힙 원소 = 그룹 안 런 번호, 비교 = (머리 key, 런 번호).
- `passes_theory(R, k)` — k^P ≥ R 인 최소 P. 부동소수 `log` 금지.
- 교체 선택의 성질: 정렬 입력 → 런 1개, 역순 → 모든 런이 정확히 208(마지막 빼고), 무작위 → 2× 근처, **"앞에 있는 더 큰 key 가 c−1 개 이하면 런 1개"** (증명 + 경계 사례 c 개 → 런 2개).
- 테스트: 경계 n(0, 1, 15, 16, 17, 207, 208, 209, 255, 256, 257, 4097) × 두 방법 × k ∈ {13, 2}, 무작위 120건(입력 종류 7가지, k ∈ [2, 13]) — 매번 정렬 + 순열 + payload + **단계마다 측정 블록 수 == 디렉터리에서 계산한 기대값** + 패스 수 == 이론 + 참조 `qsort` 일치.

### Part C — 1M 레코드 센서 로그

- 1M(= 2^20) 레코드, 16 MiB, key 중복 많음(2^18 종류). load-sort-write + k = 13 → 런 4096, 패스 4, 읽기 = 쓰기 = **83,886,080 B = D·5 정확히**, 안정. RAM 최고 사용 4096 B.
- 타임스탬프 지터가 작은 거의 정렬된 로그 → 교체 선택이 **런 1개, 병합 0 패스**: 읽기 = 쓰기 = D.
- 같은 1M 의 교체 선택판, 입력 종류별 · k 별 · N 별 표, 시간은 `demo_full.c` (solution.c 는 5 s 예산 안에 넣으려고 1M 두 개만).

## 2. Problem (English)

A device has **4096 bytes of RAM** for this job and a flash device accessed only in **256-byte blocks**. The flash holds `N` fixed-size records (16 bytes, `uint32_t key` first). Sort them by key into one contiguous sorted run on flash.

```c
int64_t gen_runs_lsw(xs_t *x);                       /* fill RAM, sort, write: returns #runs */
int64_t gen_runs_rs(xs_t *x);                        /* replacement selection: returns #runs */
int64_t merge_pass(xs_t *x, int src, uint32_t R, uint32_t k);   /* R runs -> ceil(R/k) runs */
uint32_t passes_theory(uint64_t R, uint32_t k);      /* smallest P with k^P >= R */
```

Constraints: every data buffer, heap and per-run state must come from the 4 KB arena (the allocator aborts on overflow); no stdio buffering; recursion not needed. Count blocks read and written; report passes and compare with theory. `N` up to 2^20.

Example (k = 13, N = 2^20, 256 records per initial run):

```text
runs=4096 passes=4 (theory 4) read=83886080 B write=83886080 B  theory D(1+P) = 83886080 B each
```

Follow-ups: how did you choose k? What changes with a 512-byte or 2 KB flash page? Is it stable? What if power fails during pass 3? How many erase cycles does one sort cost? Can you save a pass?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
SCRATCH=$TMPDIR/a12; mkdir -p "$SCRATCH"          # Flash 파일 둘 곳 (레슨 폴더 말고)
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o "$SCRATCH/st" && "$SCRATCH/st" "$SCRATCH/flash.bin"
```

TODO 는 다섯 군데(`sort_run`, `gen_runs_lsw`, `gen_runs_rs`, `merge_pass`, `passes_theory`). 아레나, Flash 모델, 런 쓰기(`runw_*`), 디렉터리 쓰기(`dirw_*`), 입력 스트림(`inr_get`), 두 힙의 sift-down(`rheap_down`, `mheap_down`, DS-B12에서 자세히), 드라이버 `ext_sort`, 하네스는 주어진다. 스텁은 "런 0개" / "병합 실패(-1)" 를 돌려줘서 드라이버가 멈추지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
-- Part A: RAM budget 4096 B, block 256 B, merge fan-in k=13 (needs 4032 B), RS heap 208 records
FAIL starter.c:423  passes_theory(0, 13) == 0 && passes_theory(1, 13) == 0 && passes_theory(2, 2) == 1
FAIL starter.c:425  passes_theory(125, 5) == 3 && passes_theory(216, 6) == 3 && passes_theory(16807, 7) == 5
…
random: 120 cases, 315365 records, both methods, k in [2,13]: mismatches=120
…
FAIL starter.c:533  run_good(&a) && a.v.stable && a.r.runs0 == 4096 && a.r.passes == 4
FAIL starter.c:534  a.rd * BLK == D * 5 && a.wr * BLK == D * 5
3/22 passed
```

(0.9 초, exit 1. 통과한 3개는 TODO 와 무관한 예산 검사 — k 상수, 예산 초과 abort, 딱 4096 B 허용.)

## 4. 힌트

<details><summary>힌트 1 — 안정 삽입 정렬은 '>' 한 글자</summary>

`while (j > 0 && a[j-1].key > x.key)` — 같은 key 를 넘어가지 않으니 안정. `>=` 로 쓰면 같은 key 끼리 순서가 뒤집힌다. 256개라 최악 32,640번 이동(256·255/2) — 4 KB 에서는 이걸로 충분하다(Q4 에서 대안).

</details>

<details><summary>힌트 2 — load-sort-write 는 버퍼가 따로 없다</summary>

4096 B 전부를 레코드 배열로 잡고, 입력 블록을 `&a[b*16]` 로 **바로** 읽는다. 정렬 뒤 같은 자리에서 바로 쓴다. 디렉터리는 런 길이가 전부 256(마지막만 N mod 256)이니 런을 다 쓴 다음 아레나를 풀고 256 B 를 다시 잡아 쓰면 된다.

</details>

<details><summary>힌트 3 — 교체 선택: 한 배열을 둘로</summary>

`h[0..hn)` 는 이번 런의 최소 힙, `h[hn..n)` 은 다음 런으로 얼린 레코드. 루트를 내보내고(last = 그 key) 새 레코드 x 를 읽어 `x.key >= last` 면 루트 자리에 넣고 sift-down. 아니면 힙 끝 원소를 루트로 옮기고 `hn--`, sift-down 한 뒤 비운 칸 `h[hn]` 에 x 를 얼린다. `hn == 0` 이면 런 끝 → `hn = n`, heapify. 입력이 끝났으면 "얼릴 x" 가 없으니 구멍을 얼린 구역 맨 끝 원소로 메우고 `n--`.

</details>

<details><summary>힌트 4 — 병합 힙에는 레코드가 아니라 런 번호를</summary>

입력 블록 버퍼 k 개가 이미 RAM 에 있으니 힙에는 `uint16_t` 런 번호만 둔다(13 × 2 B). 비교는 `(ib[j*16 + pos[j]].key, j)`. 버퍼가 바닥나면 그 런의 다음 블록을 읽고, 런이 끝나면 힙 마지막 원소를 루트로. 런 시작 블록은 디렉터리 길이의 `NBLK` 누적합 — 그룹 시작 때 출력 버퍼가 비어 있으니 그걸 디렉터리 읽기 버퍼로 빌려 쓴다.

</details>

<details><summary>힌트 5 — 패스 수는 곱해서 센다</summary>

`cap = 1; while (cap < R) { cap *= k; p++; }`. `ceil(log(R)/log(k))` 는 R = 125, k = 5 에서 3.0000000000000004 → 4 를 돌려준다(이 맥 실측, 5. 풀이).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 |
|---|---|
| 1. 확인 질문 | 레코드 크기·key 폭? Flash 읽기/쓰기 단위, erase 단위? RAM 중 **이 일에** 쓸 수 있는 양? 안정성 필요? 결과를 Flash 에 써야 하나, 바로 내보내도(UART/USB) 되나? 전원 차단 시 재시작 요구? |
| 2. 무식한 방법 | RAM 에 다 올리기 — 불가(16 MiB vs 4 KB). 선택 정렬처럼 "매번 Flash 전체를 훑어 다음 최소" → 읽기 O(N²/블록), 1M 이면 패스 수가 백만 단위 |
| 3. 핵심 아이디어 | 런 생성 + k-way 병합. 비용 ≈ 2D(1+P), P = ⌈log_k R⌉. R 을 줄이고(긴 런) k 를 키운다(RAM/블록) |
| 4. 불변식 | 런 안은 비내림차순. 병합 힙에는 비어 있지 않은 각 런의 머리 하나씩. 교체 선택: 힙 안 모든 key ≥ last |
| 5. 코드 | 아레나 → 블록 I/O → 런 쓰기/디렉터리 → 두 런 생성 → 병합 → 드라이버 |
| 6. 소리 내어 말할 테스트 | n = 0, 1, 블록 경계(15/16/17), RAM 경계(255/256/257), 모두 같은 key, key 0 과 UINT32_MAX, 정렬·역순 입력, k = 2 |
| 7. 복잡도 | 시간 O(N log N) 비교(런 안 + 병합 log k), I/O = 2D(1+P) 바이트, RAM 4096 B 고정, 스택 O(1) |

### Part A — 예산을 코드로: 아레나와 Flash 모델

`ram_alloc` 은 8 B 정렬로 위로만 자르는 bump 할당기다. 예산을 넘으면 메시지를 찍고 `abort()`. 함수는 시작에서 `ram_mark()`, 끝에서 `ram_release(mark)` — 단계가 끝나면 4 KB 가 통째로 다음 단계 것이 된다. 테스트는 `fork()` 한 자식에게 4000 + 97 B 를 요청시켜 **SIGABRT 로 죽는 것**을 확인하고, 부모에서 4000 + 96 = 4096 B 가 정확히 들어가는 것을 확인한다(경계 off-by-one 방지). 매 실행의 `g_ram_peak` 도 CHECK 한다 — 1M 실행 모두 `RAM peak=4096`.

**왜 stdio 를 안 쓰나.** `fopen` + `fread` 1바이트 뒤 이 맥의 `FILE` 버퍼 크기를 찍어 보면(작업용 프로그램, 실제 출력):

```text
BUFSIZ=1024 st_blksize=4096 _bf._size=4096
```

`fread` 한 번이 우리 예산 전체(4096 B)를 libc 힙에 몰래 잡는다. 그래서 `open` + `pread/pwrite`(정확히 256 B 씩)만 쓴다. OS 페이지 캐시는 여전히 파일을 캐시하지만 그건 "장치" 쪽 일이다 — 우리 카운터는 장치가 본 블록 명령 수를 센다. (macOS 의 `fcntl(F_NOCACHE)` 로 캐시를 끌 수 있다고 알고 있으나 이 레슨에선 쓰지 않았다 — 확인 권장.)

**엄밀한 C 메모.** `uint8_t` 배열로 선언된 객체를 `rec_t *`, `uint32_t *` 로 접근하는 것은 C11 6.5p7(유효 타입 규칙)상 엄밀히는 허용 범위 밖이다. 펌웨어에서는 흔한 관행이고(Linux 커널은 `-fno-strict-aliasing` 으로 빌드) 이 코드도 그렇게 쓴다. 디렉터리 항목을 출력 버퍼에서 꺼낼 때는 `memcpy` 를 썼다.

### Part A — load-sort-write: 런 = RAM 크기

RAM 4096 B / 16 B = 256 레코드가 한 런. 입력을 블록 16개씩 아레나로 바로 읽고(`flash_read(in++, &a[b*16])`), `sort_run`, 남는 칸 `0xFF`, 같은 자리에서 바로 쓴다. 런 수 R = ⌈N/256⌉: 1M → 4096, 100K → 391 (실측 `LSW random 391`).

이 단계 I/O: 입력 ⌈N/16⌉ 블록 읽기, 같은 수 쓰기. 256 이 16 의 배수라 패딩은 마지막 런에서만 생길 수 있다.

### Part A — 교체 선택: 런 = 평균 RAM 의 2배

힙 용량 c = (4096 − 3·256)/16 = **208**. 힙 하나에서 꺼낸 최소를 쓰고, 새로 읽은 레코드가 방금 쓴 것보다 크거나 같으면 같은 런에 넣을 수 있다 — 그래서 런이 RAM 보다 길어진다. 작으면 "다음 런" 으로 얼린다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">교체 선택: 레코드 배열 h[0..n) 하나를 두 구역으로 (c = 208)</text>
  <rect x="40" y="45" width="380" height="36" class="f-acc" stroke="currentColor"/>
  <rect x="420" y="45" width="220" height="36" class="f-mut" stroke="currentColor"/>
  <text x="230" y="68" fill="currentColor" text-anchor="middle">h[0..hn) 이번 런의 최소 힙 (모든 key ≥ last)</text>
  <text x="530" y="61" fill="currentColor" text-anchor="middle">h[hn..n) 얼린 것</text>
  <text x="530" y="76" fill="currentColor" text-anchor="middle" font-size="12">key &lt; 그때의 last</text>
  <text x="40" y="100" fill="currentColor" font-family="monospace">0</text>
  <text x="412" y="100" fill="currentColor" font-family="monospace">hn</text>
  <text x="632" y="100" fill="currentColor" font-family="monospace">n</text>
  <text x="40" y="128" fill="currentColor">① 루트 출력, last = 루트 key  ② 다음 x 읽기</text>
  <text x="40" y="150" fill="currentColor">③ x ≥ last → 루트 자리에 x, sift-down (hn, n 그대로)</text>
  <text x="40" y="172" fill="currentColor">④ x &lt; last → 힙 끝을 루트로, hn−1, sift-down, 비운 칸 h[hn] 에 x (경계가 왼쪽으로 한 칸)</text>
  <text x="40" y="194" fill="currentColor">⑤ hn = 0 → 런 끝: hn = n, 얼린 것 전부 heapify → 다음 런</text>
  <text x="40" y="216" fill="currentColor">⑥ 입력 끝 → 구멍 h[hn] 을 h[n−1] 로 메우고 n−1 (빠뜨리면 레코드 손실, 실수 4)</text>
</svg>
```

작은 예 (c = 3, 입력 5 1 8 3 9 2 7 4 6; 작업용 trace 프로그램의 실제 출력):

| 사건 | 이번 런 힙 | 얼린 것 | 지금까지 출력 |
|---|---|---|---|
| 초기 3개 읽고 heapify | [1 5 8] | [] | |
| 출력 1, 읽은 3 ≥ 1 → 힙 | [3 5 8] | [] | 1 |
| 출력 3, 읽은 9 ≥ 3 → 힙 | [5 9 8] | [] | 1 3 |
| 출력 5, 읽은 2 &lt; 5 → 얼림 | [8 9] | [2] | 1 3 5 |
| 출력 8, 읽은 7 &lt; 8 → 얼림 | [9] | [7 2] | 1 3 5 8 |
| 출력 9, 읽은 4 &lt; 9 → 얼림 | [] | [4 7 2] | 1 3 5 8 9 |
| 런 2 시작: 얼린 것 heapify | [2 7 4] | [] | 1 3 5 8 9 / |
| 출력 2, 읽은 6 ≥ 2 → 힙 | [4 7 6] | [] | … / 2 |
| 출력 4, 입력 끝 → 힙 축소 | [6 7] | [] | … / 2 4 |
| 출력 6, 입력 끝 → 힙 축소 | [7] | [] | … / 2 4 6 |
| 출력 7, 입력 끝 → 힙 축소 | [] | [] | 1 3 5 8 9 / 2 4 6 7 |

load-sort-write 라면 3개짜리 런 3개, 교체 선택은 5 + 4 의 2개.

**정확성.** 불변식 (I): 힙 구역의 모든 key ≥ last. 출력은 힙 최소라 last 는 런 안에서 비내림차순이고, 새로 힙에 넣는 x 는 x ≥ last 일 때만이므로 (I) 가 유지된다 → 각 런은 정렬돼 있다. 얼린 x 는 다음 런에서야 나간다. 매 단계 레코드 하나가 나가고 하나가 들어오거나(입력 남음) 하나가 줄어드니 끝난다. 마지막 런을 빼면 모든 런은 **길이 ≥ c** 다: 입력이 남아 있는 동안 시작한 런은 힙이 c 개로 가득 찬 상태에서 시작하고 그 c 개는 전부 그 런으로 나간다.

**언제 런이 1개인가 (정리).** 힙 용량 c, 입력 a_1..a_N 에 대해: 교체 선택이 런 1개를 만든다 ⇔ 모든 t 에 대해 |{j &lt; t : a_j &gt; a_t}| ≤ c − 1 ("앞에 있는 더 큰 key 가 c−1 개 이하").

증명 (⇐, 귀납): 지금까지 런이 하나라고 하자. a_t (t &gt; c) 를 읽기 직전 힙에는 a_1..a_{t−1} 중 큰 쪽 c 개가 있고(작은 것부터 나갔으니), 방금 나간 last 는 그중 가장 작은 것 = a_1..a_{t−1} 의 c 번째로 큰 값 v 다. a_t &lt; v 라면 v 이상인 c 개가 모두 a_t 보다 크니 앞의 더 큰 원소가 c 개 이상 — 가정에 모순. 따라서 a_t ≥ last, 얼려지지 않는다. (⇒) 대우: 어떤 t 에서 앞의 더 큰 원소가 c 개 이상이고 그 전까지 런이 하나였다면 위와 같이 last = v 이고, v 보다 큰 원소는 많아야 c−1 개이므로 a_t &lt; v → 얼려져 런 2개. ∎ (t ≤ c 인 원소는 처음 heapify 에 들어가 첫 런에 나간다 — 앞의 원소가 t−1 ≤ c−1 개라 조건도 자동 성립.)

이 정리는 연습 1 에서 길이 ≤ 7, 알파벳 4개의 **모든** 입력 × c = 1..4 (87,380쌍)로 전수 확인했다(위반 0). solution.c 는 경계를 직접 찌른다: `G_NEARLY`(key = 16i + r, r ∈ [0, 16·208) → 앞의 더 큰 key ≤ 207) 5만 개 → 런 1개, 그리고 `1, 2, …, 208, 0, 209, …` (0 앞에 더 큰 것 정확히 208개) → 런 2개.

**평균 2배.** 무작위 입력이면 런 길이 기댓값이 2c 로 수렴한다는 것이 고전적 결과다(Knuth TAOCP 3권 5.4.1, "눈 치우는 차(snowplow)" 비유: 정상 상태에서 힙 안 key 분포가 일정해지고, 한 바퀴 도는 동안 쌓인 눈의 두 배를 치운다). 실측:

| 입력 (n = 100,000) | load-sort-write 런 | 교체 선택 런 | 교체 선택 평균 길이 |
|---|---|---|---|
| 무작위 32-bit | 391 | 241 | 414.9 = **1.995 × 208** |
| 정렬 | 391 | **1** | 100,000 |
| 역순 | 391 | **481** = ⌈100000/208⌉, 마지막 빼고 전부 정확히 208 | 208 |

역순이 교체 선택의 최악이다: 첫 c 개 다음부터 들어오는 모든 원소가 last 보다 작아 얼려진다. 그리고 이때 교체 선택은 load-sort-write 보다 **나쁘다**(481 > 391) — I/O 버퍼 3개(768 B)를 따로 떼야 해서 힙이 256 이 아니라 208 이기 때문이다. 4 KB 에서는 버퍼 비용이 RAM 의 19% 라 "2배" 가 실제로는 415/256 = 1.62배다.

### Part B — k-way 병합: k 는 RAM 이 정한다

병합 한 그룹에 필요한 RAM: 런마다 입력 블록 1개(k × 256), 출력 블록 1개, 출력 디렉터리 블록 1개, 런 상태(`next, left, pos, cnt` = 12 B × k), 힙(`uint16_t` × k). 정렬해 더하면 `merge_ram_need(13)` = 3328 + 256 + 256 + 160 + 32 = **4032 ≤ 4096**, k = 14 는 블록만으로 4096 이라 불가. 일반식으로 k ≈ M/B − 2 (교과서식 "M/B − 1" 에서 디렉터리 버퍼 하나가 더 빠진 것).

**정확성.** 그룹 안 각 런은 정렬돼 있고, 힙에는 비어 있지 않은 런마다 현재 머리 하나씩 있다(불변식). 남은 레코드 중 최소는 어떤 런의 머리이고 힙 루트가 머리들 중 최소이므로 루트를 쓰는 것이 옳다. 꺼낸 런은 다음 레코드(필요하면 다음 블록)로 머리를 바꾸고 sift-down, 런이 끝나면 힙에서 뺀다. 출력 하나당 비교 O(log k), Flash 블록은 각 런에서 정확히 한 번씩 순서대로 읽힌다.

**안정성.** 비교가 `(key, 런 번호)` 라서 같은 key 면 앞 런이 먼저 나간다. load-sort-write 의 런은 입력의 연속 구간이 순서대로 놓인 것이고(런 안은 안정 삽입 정렬), 병합 그룹도 연속한 런을 순서대로 묶으니 새 런 번호 순서 = 원래 위치 순서가 유지된다 → **전체가 안정**. 1M 실행에서 `stable=1` (같은 key 끼리 `seq` 증가)을 CHECK 한다. 교체 선택은 아니다 — Q4.

**패스 수.** 한 패스가 R → ⌈R/k⌉ 이고 정수에서 ⌈⌈R/k⌉/k⌉ = ⌈R/k²⌉ 이므로 P 패스 뒤 ⌈R/k^P⌉ 개. 1 이 되는 최소 P = k^P ≥ R 인 최소 P = ⌈log_k R⌉. 1M load-sort-write: 4096 → 316 → 25 → 2 → 1, **P = 4** (13³ = 2197 &lt; 4096 ≤ 13⁴).

부동소수로 계산하면 틀린다. k = 2..64 에서 R = k^e (≤ 4×10^9) 인 모든 경우를 `ceil(log(R)/log(k))` 로 계산해 보면(작업용 프로그램, 실제 출력 일부):

```text
k=5 R=125 log=3.0000000000000004 ceil=4 (true 3)
k=6 R=216 log=3.0000000000000004 ceil=4 (true 3)
k=7 R=16807 log=5.0000000000000009 ceil=6 (true 5)
k=8 R=2097152 log=7.0000000000000009 ceil=8 (true 7)
bad=24
```

정확한 거듭제곱에서 24번 틀린다 — 하필 "런 수가 딱 k^P" 인 가장 흔한 설계점에서. solution.c 는 이 값들과 k = 2..40, R = 1..5000 전부를 "⌈R/k⌉ 를 1 이 될 때까지" 기준과 비교한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">1M 레코드, load-sort-write, k = 13: 영역 핑퐁과 런 수 (실측)</text>
  <rect x="10" y="40" width="110" height="40" class="f-mut" stroke="currentColor"/>
  <text x="65" y="58" fill="currentColor" text-anchor="middle">입력</text>
  <text x="65" y="74" fill="currentColor" text-anchor="middle">16 MiB</text>
  <rect x="160" y="40" width="100" height="40" class="f-acc" stroke="currentColor"/>
  <text x="210" y="58" fill="currentColor" text-anchor="middle">영역 0</text>
  <text x="210" y="74" fill="currentColor" text-anchor="middle">4096 런</text>
  <rect x="300" y="40" width="100" height="40" class="f-ok" stroke="currentColor"/>
  <text x="350" y="58" fill="currentColor" text-anchor="middle">영역 1</text>
  <text x="350" y="74" fill="currentColor" text-anchor="middle">316 런</text>
  <rect x="440" y="40" width="80" height="40" class="f-acc" stroke="currentColor"/>
  <text x="480" y="58" fill="currentColor" text-anchor="middle">영역 0</text>
  <text x="480" y="74" fill="currentColor" text-anchor="middle">25 런</text>
  <rect x="560" y="40" width="70" height="40" class="f-ok" stroke="currentColor"/>
  <text x="595" y="58" fill="currentColor" text-anchor="middle">영역 1</text>
  <text x="595" y="74" fill="currentColor" text-anchor="middle">2 런</text>
  <rect x="660" y="40" width="70" height="40" class="f-acc" stroke="currentColor"/>
  <text x="695" y="58" fill="currentColor" text-anchor="middle">영역 0</text>
  <text x="695" y="74" fill="currentColor" text-anchor="middle">1 런</text>
  <g stroke="currentColor" stroke-width="1.5">
    <line x1="120" y1="60" x2="158" y2="60"/><line x1="260" y1="60" x2="298" y2="60"/>
    <line x1="400" y1="60" x2="438" y2="60"/><line x1="520" y1="60" x2="558" y2="60"/>
    <line x1="630" y1="60" x2="658" y2="60"/>
  </g>
  <text x="120" y="102" fill="currentColor">런 생성</text>
  <text x="270" y="102" fill="currentColor">패스 1</text>
  <text x="410" y="102" fill="currentColor">패스 2</text>
  <text x="525" y="102" fill="currentColor">패스 3</text>
  <text x="632" y="102" fill="currentColor">패스 4</text>
  <text x="10" y="132" fill="currentColor">단계마다 16 MiB 읽고 16 MiB 쓴다 → 읽기 = 쓰기 = 16 MiB × (1 + 4) = 80 MiB</text>
  <text x="10" y="152" fill="currentColor">= 83,886,080 B, 측정과 바이트 단위로 일치. 아래: 같은 1M 의 fan-in 스윕 (실측 읽기량)</text>
  <text x="10" y="168" fill="currentColor" font-size="12">k=2 → P=12 · k=4 → P=6 · k=8 → P=4 · k=13 → P=4</text>
  <rect x="10" y="176" width="208" height="14" class="f-warn" stroke="currentColor"/>
  <text x="225" y="187" fill="currentColor">k=2 읽기 208 MiB</text>
  <rect x="10" y="195" width="112" height="14" class="f-warn" stroke="currentColor"/>
  <text x="129" y="207" fill="currentColor">k=4 112 MiB</text>
  <rect x="10" y="215" width="80" height="14" class="f-ok" stroke="currentColor"/>
  <text x="97" y="227" fill="currentColor">k=8 · k=13 80 MiB (8⁴ = 4096 이라 k=8 로도 4패스 — k 를 13 으로 늘려도 이 N 에선 이득 없음)</text>
  <text x="10" y="246" fill="currentColor">입력 영역은 끝까지 읽기만 한다 → 전원이 꺼져도 처음부터 다시 할 수 있다 (Q7)</text>
</svg>
```

### Part B — 바이트 공식 (내 버전)

D = N·S (S = 16 B), 블록 B = 256 B. 단계 i(런 생성 = 0, 패스 1..P)가 만드는 런 길이를 ℓ_{i,1..R_i} 라 하면:

- 쓰기 블록(단계 i) = Σ_j ⌈ℓ_{i,j}·S / B⌉, 읽기 블록(패스 i) = 단계 i−1 의 쓰기 블록, 런 생성의 읽기 = ⌈N/16⌉ (교체 선택) 또는 Σ_j ⌈ℓ_{0,j}/16⌉ (load-sort-write, 같은 값).
- 따라서 **읽기 = 쓰기 = D·(1 + P) + 패딩**, 패딩 ≤ (B − S) · Σ_i R_i (런 하나당 마지막 블록 남는 칸).
- 메타(디렉터리)는 따로 센다: 1M load-sort-write 에서 읽기 407 블록, 쓰기 72 블록 = 약 120 KB, 데이터 160 MiB 대비 0.07%.

하네스는 이 식을 그대로 CHECK 한다: 각 단계 전후 카운터 차이 == 디렉터리에서 다시 계산한 Σ⌈ℓ/16⌉. N = 2^20 이면 load-sort-write 의 모든 런 길이(256·13^p 와 그 합)가 256 의 배수라 패딩 0 → `read = write = 83,886,080 B = D·5` 가 정확히 나온다. 교체 선택 1M 에서는 런 길이가 제각각이라 패딩이 327,936 B(전체의 0.4%) 생긴다(아래 표).

### Part C — 1M 센서 로그: 실측

`demo_full.c` 실제 출력 (열 정리). 매 줄 끝까지 정렬하고 `run_good`(정렬 + 순열 해시 + payload + 단계별 블록 수 + 패스 == 이론 + RAM ≤ 4096) 확인, 전부 `ok=1`, `peak=4096`:

| 입력 (N = 2^20) | 방법 | 런 | 평균 런 | P (이론) | 읽기 = 쓰기 | 패딩 | 안정 |
|---|---|---|---|---|---|---|---|
| 무작위 | LSW | 4096 | 256.0 | 4 (4) | 80.000 MiB | 0 B | 예 |
| 무작위 | RS | 2524 | 415.4 | 4 (4) | 80.313 MiB | 327,936 B | (key 중복 거의 없음) |
| 중복 많음 (2^18 종류) | LSW | 4096 | 256.0 | 4 (4) | 80.000 MiB | 0 B | 예 |
| 중복 많음 | RS | 2520 | 416.1 | 4 (4) | 80.318 MiB | 333,824 B | **아니오** |
| 정렬 | RS | **1** | — | **0** | **16.000 MiB** | 0 B | 아니오 (같은 key 3개씩) |
| 역순 | RS | 5042 | 208.0 | 4 (4) | 80.000 MiB | 0 B | — |
| 거의 정렬 (지터 &lt; 8칸) | RS | **1** | — | **0** | **16.000 MiB** | 0 B | — |

시간 (Apple M6, Apple clang 21.0.0, -O2, 워밍업 1회 후 3회 중앙값 [최소–최대], 측정 전후 `uptime` load averages **56.36 → 58.29** — 다른 작업 10개가 같이 돌아 절대값보다 비율을 볼 것): 무작위 LSW **2130 ms** [2006–2521], 무작위 RS **2453 ms** [2364–2478]. 블록 I/O 가 655,360번(LSW) 이라 시간 대부분이 `pread/pwrite` 시스템 호출이다(solution.c 전체: user 0.36 s, sys 2.75 s). 즉 이 모델에서 시간 ≈ I/O 명령 수 — 실제 SPI NOR 도 명령 오버헤드 + 바이트 전송 시간이라 같은 모양이다.

**교체 선택이 패스를 줄이는 지점.** 1M 에서는 2520 &gt; 13³ = 2197 이라 패스가 같다(4). N 을 바꾸면 (실측):

| N | LSW 런 → P, 읽기 | RS 런 → P, 읽기 |
|---|---|---|
| 400,000 | 1563 → 3, 24.414 MiB | 963 → 3, 24.535 MiB (패딩만큼 손해) |
| 600,000 | 2344 → **4**, 45.776 MiB | 1444 → **3**, 36.801 MiB (−20%) |
| 900,000 | 3516 → **4**, 68.665 MiB | 2165 → **3**, 55.199 MiB (−20%) |

런 수가 k^P 경계를 넘느냐가 전부다. 1M 에서 한 패스를 줄이는 방법은 연습 3(첫 패스 부분 병합: 4 → 3.139 패스).

**Flash 현실성.**

- **SPI NOR**: page program 256 B, 지우기는 보통 4 KB 섹터(W25Q 계열 등 — 부품 데이터시트 확인 권장). 같은 섹터를 다시 쓰려면 지워야 한다. 핑퐁 영역 0 은 런 생성·패스 2·패스 4 에서 3번, 영역 1 은 2번 쓰인다 → 정렬 1회 = 영역 섹터당 erase 2~3회. NOR 의 erase 내구성은 흔히 10만 회 급이라(데이터시트 확인 권장) 하루 한 번 정렬은 문제가 안 되지만, **패스 하나 = 데이터 전체 1회 쓰기 = erase 1회** 라는 등식은 그대로다.
- **NAND**: 페이지 2~16 KB, 지우기는 블록(수십~수백 페이지), 페이지는 erase 사이에 한 번만 program, 블록 안에서는 순서대로 program (MEM-B12). 페이지가 RAM(4 KB)보다 크면 페이지 버퍼 2개도 못 들고 있다 → 이 레슨의 k-way 병합이 성립하지 않는다. 칩 안의 페이지 레지스터에서 일부만 꺼내는 random data output 같은 명령으로 "칩이 버퍼를 대신 들게" 하는 방법이 있다(ONFI 명령 세부는 확인 권장). FTL(MEM-M10, DS-A09) 위라면 쓰기 증폭(WAF)이 패스마다 곱해진다 — Don 의 SSD 경험 그대로: **패스 수를 줄이는 게 곧 수명**.
- 결과를 Flash 에 쓰지 않고 마지막 패스 출력을 바로 UART/USB 로 내보내면 쓰기 한 번(D)이 통째로 빠진다 — 확인 질문 1 에서 물어볼 가치.

### Part C — 스택: 정직하게

데이터 배열은 스택에 없다(모두 아레나). 지역 변수는 인덱스·카운터·레코드 한 개(`rec_t x`, 16 B) 정도다. 이 맥 arm64 `-O2 -fstack-usage` (도우미가 인라인된 뒤): `gen_runs_rs` 192 B, `merge_pass` 272 B — 여기에 `pread` 쪽 libc 프레임은 안 들어간다. RAM 안 핵심 루틴만 Cortex-M4 로 컴파일하면(`demo_m4.c`, 실제 `.su` 출력):

```text
demo_m4.c:13:sort_run	68	static
demo_m4.c:22:rheap_down	32	static
demo_m4.c:39:mheap_down	44	static
```

세 함수 코드 688 B(`size` 의 text). asm 에서 `sort_run` 의 레코드 한 칸 이동은 `ldr` 4개 + `str` 4개였고(LDM/STM 이 아님 — 주소 계산이 달라서), 안쪽 루프는 4배 풀려 있었다. 펌웨어 태스크 스택을 정한다면 이 숫자 + Flash 드라이버(SPI + DMA 대기) 깊이를 더해야 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o "$SCRATCH/so" && "$SCRATCH/so" "$SCRATCH/flash.bin"     # 22/22 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o "$SCRATCH/san" \
  && "$SCRATCH/san" "$SCRATCH/flash.bin"                                                                    # 22/22, 보고 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_full.c -o "$SCRATCH/demo_full" && "$SCRATCH/demo_full" "$SCRATCH/flash.bin"
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -fstack-usage -c demo_m4.c -o "$SCRATCH/demo_m4.o" && cat "$SCRATCH/demo_m4.su"
```

solution.c 는 1M 정렬을 두 번(LSW 무작위 중복, RS 거의 정렬) 포함해 이 맥(load 수십~130)에서 벽시계 3.6~4.0 s, ASan/UBSan 판 5.5 s. 시간 대부분이 시스템 호출이라 sanitizer 가 크게 늘리지 않는다. 1M 교체 선택 무작위판은 시간 예산 때문에 `demo_full.c` 로 옮겼다.

## 7. 자주 하는 실수

**1. `fread` 로 읽는다.** 이 맥에서 `FILE` 버퍼가 4096 B(`_bf._size`, 실측) — 예산 전체를 몰래 쓴다. → `open` + `pread/pwrite`, 아니면 `setvbuf(f, NULL, _IONBF, 0)`.

**2. 패스 수를 `ceil(log(R)/log(k))` 로.** R = 125, k = 5 → 4 (실측). 실제로 이 변형을 넣으면 20/22 (공식 표 CHECK 와 전수 비교가 FAIL). → 정수 곱셈 루프.

**3. 비교를 뺄셈으로.** `(int32_t)(ka - kb) < 0` 은 key 가 0 과 UINT32_MAX 처럼 2^31 이상 떨어지면 부호가 뒤집힌다. 병합 비교에 넣은 변형: 21/22, `extreme` 입력 무작위 사례들이 FAIL. → `ka < kb` 직접 비교.

**4. 입력이 끝난 뒤 구멍을 안 메운다.** 교체 선택에서 `else { n--; }` 로만 쓰면 힙 끝 칸과 얼린 구역 사이에 낡은 사본이 남고 얼린 레코드 하나가 사라진다. 실제 변형: 17/22 — 순열 해시가 잡는다.

**5. 병합 동률을 뒤 런 먼저.** `(ka == kb && a > b)` 로 바꾸면 정렬은 맞고 순열도 맞는데 **안정성만** 깨진다 → 19/22 (참조 비교와 1M `stable` CHECK). 정렬 결과만 보는 테스트로는 못 잡는다.

**6. 런 시작 블록을 내림으로.** `src_blk += len / 16` 은 길이가 16 의 배수가 아닌 런 뒤부터 모든 런을 엉뚱한 블록에서 읽는다. load-sort-write 런(256 배수)에서는 멀쩡하고 **교체 선택에서만** 터진다: 18/22. → `NBLK(len)` (올림).

**7. 마지막 디렉터리 블록을 안 쓴다.** 런 수가 64 의 배수가 아니면 마지막 런 길이들이 사라진다: 14/22. 블록 버퍼를 쓰는 모든 writer 에는 flush 가 있다.

**8. 교체 선택에서 `x.key > last` (같은 key 를 얼림).** 결과는 여전히 정렬된 순열이지만 모든 key 가 같은 입력에서 런이 1개 → ⌈N/208⌉ 개가 된다. 처음엔 테스트가 이걸 못 잡았다(정확성 검사만 있었으니까) — "모두 같은 key → 런 1개" CHECK 를 추가해 21/22 로 잡힌다. 성능 버그는 성능 불변식을 CHECK 해야 잡힌다.

**9. multiset 검사를 XOR 로.** 해시의 XOR 은 같은 레코드 두 개가 서로 지운다 — {10, 20, 20} 과 {10, 30, 30} 이 같은 값(연습 4, 실측). → 합(mod 2^64) + 개수.

## 8. 면접 Q&A

<details><summary>Q1. 왜 런 생성 + k-way 병합인가요? 더 좋은 방법이 있나요?</summary>

I/O 를 세면 답이 나온다. 블록 크기 B, RAM M 인 모델에서 비교 기반 정렬의 I/O 하한은 Θ((N/B)·log_{M/B}(N/B)) 블록이고 다중 병합 정렬이 이를 달성한다(Aggarwal–Vitter 1988, 외부 메모리 모델 — 서지는 기억 기반, 확인 권장). 우리 숫자로: N/B = 65,536 블록, M/B = 16 → log_16(65536) = 4 — 우리의 "런 생성 1 + 병합 4" 와 같은 크기다. 외부 퀵소트(분배 정렬)도 같은 점근이지만 피벗 운에 따라 패스 수가 흔들리고 최악이 나쁘다. 펌웨어에선 **최악이 보장되는** 쪽(병합)을 고른다. 꼬리: "key 가 작은 정수면?" → 분배(버킷) 정렬로 k 개 버킷에 블록 버퍼 하나씩 → 같은 k 제한, 패스 수 log_k(key 범위)(ALGO-B08 기수 정렬의 외부판).

</details>

<details><summary>Q2. k 는 어떻게 정했고, 블록 크기를 바꾸면요?</summary>

k 개 입력 블록 + 출력 1 + 디렉터리 1 + 상태 14 B/런 ≤ 4096 → k = 13 (`merge_ram_need(13)` = 4032). 블록 크기를 바꾸면(연습 5, 1M LSW 런 4096개, 실제 출력): B=64 → k=50, P=3, 128 MiB, I/O 2,097,152회 · B=128 → k=27, P=3, 128 MiB, 1,048,576회 · B=256 → k=13, P=4, 160 MiB, 655,360회 · B=512 → k=5, P=6, 224 MiB, 458,752회 · B=1024 → k=1, 병합 불가. 작은 블록은 바이트를 줄이고 명령 수를 늘린다. 실제 장치의 비용 ≈ 명령 수 × 명령 오버헤드 + 바이트 × 전송 시간이므로 둘을 데이터시트 숫자로 저울질한다. 단 NOR 쓰기는 256 B page 보다 작게 쪼개도 erase 단위는 그대로라 수명에는 바이트 쪽이 중요하다. 꼬리: "k 를 최대로 하면 항상 좋나?" → 1M 에서는 k=8 과 13 이 같은 4패스였다(8⁴ = 4096). 필요 이상 k 는 힙 비교만 늘린다.

</details>

<details><summary>Q3. 교체 선택은 왜 2배인가요? 언제 손해죠?</summary>

무작위 입력에서 평균 런 = 2c 는 Knuth 5.4.1 의 결과(눈 치우는 차 비유). 실측 1.995c (n = 10^5), 1M 에서 415.4/208 = 1.997. 손해: (1) 역순 입력 → 런 = c 정확히, 그런데 c(208) &lt; LSW 런(256)이라 오히려 나쁘다(481 vs 391 런). (2) 블록 버퍼 3개를 따로 떼야 한다 — 4 KB 에서 RAM 의 19%. (3) 런 길이가 블록 배수가 아니라 패딩이 생긴다(1M 에서 0.4%). (4) 불안정. 이득이 큰 곳: **거의 정렬된 로그**. 정리(5. 풀이): 앞의 더 큰 key 가 c−1 개 이하면 런 1개 → 병합 0 패스, 쓰기 D 한 번. 여러 센서 FIFO 가 조금씩 늦게 쌓은 타임스탬프 로그가 정확히 이 경우다(1M 실측: 런 1, 16 MiB). 꼬리: "LSW 처럼 버퍼 없이 할 수 없나?" → 입력 블록을 힙 빈자리로 직접 읽는 식의 변형이 가능하지만 코드가 복잡해진다 — 4 KB 에서는 단순함을 택했다.

</details>

<details><summary>Q4. 결과가 안정한가요? 아니면 어떻게 안정하게 만드나요?</summary>

load-sort-write + 안정 삽입 정렬 + 병합 동률 시 앞 런 → **안정** (5. 풀이 증명, 1M CHECK). 교체 선택은 런 안에서 힙이 같은 key 순서를 섞는다(1M 중복 입력에서 순서가 뒤집힌 인접 쌍 431개, solution 초기판 실측; demo 표 `stable=0`). 런 사이 순서는 지켜진다: 같은 key 의 a 가 b 보다 먼저 왔으면 run(a) ≤ run(b) — a 가 얼려졌다면 b 가 올 때 last 는 그때보다 크거나 같으니 b 도 얼려진다. 따라서 힙 비교를 (key, 도착 번호) 로 바꾸면 전체가 안정해진다. 대가는 레코드당 4 B → 힙 208 → 166 (연습 2: 300건 중 key 만 비교하면 292건 불안정, stamp 를 쓰면 0건). 꼬리: "삽입 정렬 O(L²) 괜찮나?" → L = 256 이면 런당 최악 32,640번 이동, 1M 전체 최악 1.3억 번 레코드 이동(역순 입력) — 측정상 시간은 I/O 가 지배했다. RAM 이 더 크면 안정성이 필요 없을 때 힙 정렬(제자리, O(L log L)), 필요하면 블록 단위 제자리 병합(복잡) 또는 인덱스 정렬.

</details>

<details><summary>Q5. 패스를 하나 줄일 수 있나요?</summary>

세 가지. (1) **런을 줄인다**: 교체 선택(600K 에서 4 → 3 패스, 읽기 −20% 실측). (2) **첫 패스를 일부만**: R = 2520, k = 13 이면 13³ = 2197 까지만 줄이면 되니 첫 패스에서 13개 묶음 26번 + 12개 묶음 1번(줄이는 양 323 = 26·12 + 11)만 병합하고 나머지 런은 건드리지 않는다. 런 단위 읽기량 10,080 → 7,910 (= 3.139 패스). 이것이 무게 1인 잎 R 개의 k-진 허프만 트리(더미 런으로 (R−1) mod (k−1) = 0 맞추기)와 같다는 것을 R ≤ 3000, k = 2..13 전부에서 확인했다(연습 3). 런 길이가 제각각이면 짧은 런부터 합치는 허프만이 최적(최적 병합 패턴). (3) **마지막 패스를 쓰지 않는다**: 소비자가 스트림을 받으면 출력 D 를 Flash 에 안 쓴다. 꼬리: "패스 수 공식을 부동소수로 짜면?" → R = 125, k = 5 에서 4 (실측) — 정수로.

</details>

<details><summary>Q6. 정렬이 맞았다는 걸 어떻게 테스트하죠?</summary>

네 겹. (1) 정렬: 인접 key 비내림. (2) 순열: 개수 + 순서 무관 multiset 해시 = Σ mix64(레코드) mod 2^64 — XOR 은 쌍 손상을 못 본다(연습 4). (3) 무결성: 각 레코드의 `pay` 가 (key, seq) 에서 다시 계산한 값과 같은지 — 블록 경계에서 찢어진 복사를 잡는다. (4) 안정성: 같은 key 끼리 `seq` 증가. 여기에 작은 n 은 `qsort((key, seq))` 참조와 레코드 단위 비교, **I/O 계측 자체도** 단계마다 디렉터리에서 다시 계산한 기대 블록 수와 비교, 패스 수 == 이론. 마지막으로 그럴듯한 버그 7개를 넣어 봤고 전부 FAIL 로 잡혔다(7. 실수 2~8). 처음엔 `>` 변형(실수 8)이 살아남았다 — 그래서 "모두 같은 key → 런 1개" 를 추가했다. 꼬리: "1M 을 참조 정렬로 비교하려면 16 MiB 가 필요한데?" → 그래서 1M 은 해시 + 순서 검사만, 참조 비교는 n ≤ 5000.

</details>

<details><summary>Q7. 패스 3 도중에 전원이 꺼지면요?</summary>

이 설계는 **입력 영역을 끝까지 읽기만** 하고, 패스마다 반대 영역에 쓰므로 직전 패스 결과(영역 + 디렉터리)는 다음 패스가 끝날 때까지 온전하다. 필요한 것은 "어느 패스까지 끝났나" 를 원자적으로 남기는 커밋 레코드 하나: 패스가 끝나면 (패스 번호, 영역, 런 수, CRC) 를 별도 섹터 두 개에 번갈아 쓰고(시퀀스 번호가 큰 것 + CRC 맞는 것이 유효), 부팅 시 그 패스부터 재시작한다. 쓰다 만 영역은 지우고 다시 쓴다. 비용은 패스당 작은 쓰기 한 번. 이것이 LittleFS/로그 구조 저장소의 "새 곳에 쓰고 포인터를 원자적으로 바꾼다" 와 같은 원리다(STOR-A05). 꼬리: "한 영역만으로(제자리) 하면?" → 공간은 절반이지만 전원 차단에 취약하고 NOR/NAND 는 덮어쓰기 전에 erase 가 필요해 어차피 새 섹터가 필요하다.

</details>

<details><summary>Q8. 정수 폭에서 터질 곳은?</summary>

(1) 바이트 오프셋: uint32 블록 번호 × 256 을 32-bit 로 곱하면 블록 2^24 (= 4 GiB 지점)부터 넘치고, 32-bit 부호 있는 오프셋을 쓰는 파일 API 라면 2 GiB 에서 이미 음수가 된다 → `(off_t)blk * BLK` 처럼 곱하기 **전에** 넓힌다. (2) key 비교를 뺄셈으로(실수 3). (3) 런 길이·누적 레코드 수 uint32 — N &lt; 2^32 가정을 명시. (4) 병합 힙 원소 `uint16_t` → k ≤ 65535, 상태의 `pos/cnt` uint16 은 블록당 레코드 ≤ 65535. (5) `passes_theory` 의 `cap *= k` 는 uint64 라 k ≤ 2^16, R ≤ 2^32 에서 넘치지 않는다. Cortex-M0 처럼 나눗셈이 없는 코어에서도 이 코드의 나눗셈은 16·64 같은 2의 거듭제곱(시프트)뿐이다. 꼬리: "key 가 64-bit 면?" → 레코드 24 B 로 커져 블록당 10.67개 → 블록 경계에 걸치는 레코드가 생긴다. 레코드를 2의 거듭제곱 크기로 패딩하거나 블록 안에 남는 칸을 두는 쪽이 단순하다.

</details>

<details><summary>Q9. 다른 코어나 DMA 로 더 빨리 할 수 없나요?</summary>

이중 버퍼링(DMA 가 다음 블록을 읽는 동안 CPU 가 병합)이 정석인데, 런마다 버퍼가 2개 필요해 4 KB 에서는 k 가 13 → 6 근처로 떨어진다(블록 2k + 2개 ≤ 16). 그러면 1M 에서 6^4 = 1296 &lt; 4096 이라 P = 5 — 패스 하나(16 MiB 쓰기)를 더 하고 겹치기로 시간을 버는 거래다. 이 모델에서 시간은 명령 수가 지배했으니(sys 2.75 s / user 0.36 s) 출력 버퍼 하나만 이중화(+256 B → k = 12, 1M 에선 여전히 P = 4)가 현실적 절충이다. 둘째 코어가 있으면 런 생성(CPU)과 병합(I/O)을 파이프라인으로 나눌 수 있지만 RAM 을 나눠야 하므로 각자의 런 길이·k 가 줄어든다 — 결국 같은 4 KB 를 누가 쓰느냐의 문제. 꼬리: "ISR 이 정렬 중 레코드를 추가하면?" → 정렬 대상은 스냅샷(입력 영역)으로 고정하고, 새 레코드는 다음 영역에 쌓아 다음 정렬 때 합친다(LSM 트리의 사고방식, STOR-M06).

</details>

## 9. 추가 연습

### 연습 1 — 런 1개 정리를 전수로

5. 풀이의 정리("앞의 더 큰 key ≤ c−1 ⇔ 런 1개")를 길이 0~7, 알파벳 {0,1,2,3}(중복 포함)의 모든 입력과 c = 1..4 에서 확인하라. 덤으로 "모든 런은 정렬돼 있다", "마지막 런 빼고 길이 ≥ c" 도.

<details><summary>답 보기</summary>

RAM 배열 위의 교체 선택 시뮬레이터(본문과 같은 힙 나눠 쓰기)와 O(n²) 왼쪽 역전 최댓값을 비교한다. 실제 출력:

```text
ex1: 87380 (input, c) pairs: iff violations=0, unsorted runs=0, short non-last runs=0
```

중복 key 가 있을 때도 성립한다(조건이 "엄격히 큰" 원소 수라서 — 같은 key 는 `>=` 로 이번 런에 들어간다). 이 정리 덕에 "지터가 c 칸보다 작은 로그는 병합이 필요 없다" 를 설계 문서에 숫자로 쓸 수 있다.

</details>

### 연습 2 — 안정한 교체 선택

레코드에 도착 번호 stamp 를 붙여 힙 비교를 (key, stamp) 로 바꾸고, 런들을 (key, 런 번호) 로 병합한 결과가 `qsort((key, seq))` 와 레코드 단위로 같은지 중복 많은 무작위 300건에서 확인하라. key 만 비교하는 판은 몇 건에서 틀리나? 4 KB 에서 힙 용량은 얼마로 줄어드나?

<details><summary>답 보기</summary>

```text
ex2: 300 dup-heavy cases: key-only RS unstable in 292, (key,stamp) RS unstable in 0; 4 KB heap capacity 16 B -> 208 records, 20 B -> 166 records
```

런 안 순서는 stamp 가, 런 사이 순서는 "run(a) ≤ run(b)" 보조정리(Q4)와 병합의 런 번호 동률 규칙이 보장한다. 대가: 힙 20% 감소 → 평균 런 ≈ 2·166 = 332, 1M 에서 런 ≈ 3160 — 13³ = 2197 보다 크니 여전히 4패스. stamp 를 32-bit 로 두면 2^32 레코드에서 넘친다. 런마다 0 부터 다시 세면 되는데, 얼린 레코드는 다음 런에 속하니 "이번 런 번호 + 다음 런 번호" 두 카운터가 필요하다.

</details>

### 연습 3 — 패스 하나 줄이기: 첫 패스 부분 병합

같은 길이 런 R 개를 k-way 로 합칠 때 "런 단위 읽기량"(= 각 원래 런의 깊이 합)을 최소로 하는 계획을 공식으로 쓰고, k-진 허프만(무게 1 잎 + 무게 0 더미)과 R ≤ 3000, k = 2..13 에서 비교하라. R = 2520, k = 13 의 값은?

<details><summary>답 보기</summary>

P = ⌈log_k R⌉, T = k^{P−1}. 첫 패스에서 줄일 런 수 r = R − T. 13개 묶음 하나가 12개를 줄이니 q = ⌊r/(k−1)⌋ 번 + 나머지 m = r mod (k−1) > 0 이면 (m+1)개 묶음 1번. 첫 패스에 참여하는 런 deep = qk + (m+1) 은 깊이 P, 나머지는 깊이 P−1:

```c
cost = deep * P + (R - deep) * (P - 1);
```

```text
ex3: plan == k-ary Huffman for R<=3000, k=2..13: mismatches=0;  R=2520 k=13: uniform 10080 run-reads (4 passes), partial first pass 7910 (= 3.139 passes)
```

1M 교체 선택(R ≈ 2520)이라면 쓰기량 80 MiB → 약 66 MiB(런 생성 16 + 병합 50.2). 더미 런 개념은 Knuth 5.4 의 다상(polyphase)·최적 병합 논의에 나온다(절 번호 확인 권장).

</details>

### 연습 4 — XOR 은 왜 안 되나

multiset 검사를 해시 XOR 로 하면 놓치는 손상 예를 하나 들고, 합은 그 예를 잡는다는 것과 합이 순서와 무관하다는 것을 보여라.

<details><summary>답 보기</summary>

입력 {10, 20, 20}, 출력 {10, 30, 30}: x ⊕ x = 0 이라 XOR 은 둘 다 h(10). 개수도 3개로 같다. 합은 h(10) + 2h(20) ≠ h(10) + 2h(30). 실제 출력:

```text
ex4: xor in=088712be8a582fca bad=088712be8a582fca | sum in=74cbc5df1a8532e2 bad=5a63c1b46f3c4b66
```

외부 정렬에서 "같은 블록을 두 번 읽어 쓰는" 버그가 정확히 이 모양(레코드 쌍 복제)이다. 합은 덧셈의 교환법칙으로 순서 무관(셔플 1000번 동일 확인). 합도 충돌은 가능하지만 64-bit 무작위 해시면 우연 일치 확률은 2^-64 수준이다(적대적 입력은 별개).

</details>

### 연습 5 — 블록 크기 스윕

본문 `merge_ram_need` 식에서 블록 크기만 64~1024 B 로 바꿔 k, 1M LSW(런 4096) 의 패스 수, 읽기+쓰기 바이트, I/O 명령 수를 표로.

<details><summary>답 보기</summary>

```text
ex5:   B   k   P   bytes read+write   I/O ops
       64  50   3          128 MiB     2097152
      128  27   3          128 MiB     1048576
      256  13   4          160 MiB      655360
      512   5   6          224 MiB      458752
     1024   1   -   (merge impossible: k < 2)
```

B = 1024 면 입력 1 + 출력 1 + 디렉터리 1 = 3 KB 에 상태를 더하면 2-way 도 안 된다 — NAND 페이지(2 KB 이상)를 RAM 4 KB 로 다루기 어려운 이유(5. 풀이 Flash 현실성). 고르는 기준은 장치의 명령 오버헤드 대 바이트 전송 시간, 그리고 수명(바이트).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o "$SCRATCH/practice" && "$SCRATCH/practice"      # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o "$SCRATCH/practice_san" && "$SCRATCH/practice_san"   # 11/11, 보고 없음
```

## 10. 핵심 정리

- 외부 정렬 비용 = 패스 수 × 데이터: 읽기 = 쓰기 = D·(1 + P) + 패딩, P = ⌈log_k R⌉ (정수로 — `ceil(log)` 는 R = 125, k = 5 에서 틀린다).
- k 는 RAM 이 정한다: 입력 블록 k + 출력 1 + 디렉터리 1 + 상태 ≤ 4096 → 256 B 블록에서 k = 13. 1M → 런 4096 → 4패스 → 80 MiB 읽고 80 MiB 씀 (바이트 단위로 측정 일치).
- load-sort-write: 런 = RAM(256), 버퍼 없이 제자리, 안정 삽입 정렬 + 동률 시 앞 런 → 전체 안정.
- 교체 선택: 런 평균 2c(실측 1.995c), 정렬·거의 정렬 입력 → 런 1개(조건: 앞의 더 큰 key ≤ c−1), 역순 → 런 = c. 4 KB 에선 버퍼 768 B 때문에 c = 208 &lt; 256. 런 안에서 불안정 — stamp 로 고친다(166).
- 정확성 = 정렬 + 개수 + **합** 해시(XOR 아님) + payload + 안정성 + I/O 계측의 기대값 일치. 버그 7개 주입 → 전부 FAIL.
- 예산은 코드로: 아레나 + abort, stdio 금지(이 맥 `FILE` 버퍼 4096 B), 스택엔 스칼라만(M4: 32~68 B/함수).
- Flash: 패스 하나 = 데이터 전체 1회 쓰기 = erase 1회. 패스 줄이기(긴 런, 부분 첫 패스, 마지막 패스 스트리밍)가 곧 수명. 입력을 안 건드리고 핑퐁 + 커밋 레코드면 전원 차단에도 재시작 가능.

## 참고자료

- D. E. Knuth, *The Art of Computer Programming*, Vol. 3 *Sorting and Searching*, 2nd ed., §5.4 External Sorting — 5.4.1 다중 병합과 교체 선택(눈 치우는 차 비유, 평균 런 2P), 다상 병합·더미 런. 절 세부는 기억에 의존 — 확인 권장.
- A. Aggarwal, J. S. Vitter, "The input/output complexity of sorting and related problems", *Communications of the ACM* 31(9), 1988 — 외부 메모리 모델의 정렬 I/O 하한. 서지는 기억에 의존 — 확인 권장.
- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms* — 6장 힙, 연습문제의 k-way 병합(O(n log k)).
- LeetCode 23 Merge k Sorted Lists — 병합 단계의 RAM 판 (ALGO-B07 연습 5 참고).
- Winbond W25Q 계열 SPI NOR 데이터시트 — 256 B page program, 4 KB sector erase, erase 내구성 (부품마다 다름 — 사용하는 칩 데이터시트 확인).
- ONFI(Open NAND Flash Interface) 규격 — NAND 페이지/블록, program/erase 제약, 페이지 레지스터 부분 읽기 명령 (세부 확인 권장).
- Linux 커널 Makefile 의 `-fno-strict-aliasing` — 아레나 재사용과 유효 타입 규칙(C11 6.5p7).
- 관련 레슨: ALGO-B07 (머지소트·안정 병합·외부 정렬 개요), ALGO-B08 (기수 정렬), DS-B12 (이진 힙), DS-M11 (인덱스 PQ), MEM-B12 (NAND 기초), MEM-M10 (page-level FTL), DS-A09 (FTL 매핑·GC), STOR-M06 (LSM SSTable), STOR-A05 (FatFs/LittleFS/raw KV), SYSD-M13 (NAND 저장장치 설계)
