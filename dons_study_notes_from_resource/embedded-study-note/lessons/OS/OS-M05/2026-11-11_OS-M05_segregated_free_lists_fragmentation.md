---
id: OS-M05
time: 90분
updated: 2026-11-11
---
# OS-M05 malloc 구현 2: segregated free lists와 단편화 측정

## 학습 목표

- OS-M04 의 명시적 free list 할당기(경계 태그, 즉시 병합, 분할, realloc)를 **크기 클래스(size class)별 리스트 17개**로 바꾸고, "자기 클래스에서 first-fit, 없으면 위 클래스의 첫 블록" 이 왜 best-fit 에 가까운지 설명한다.
- 같은 트레이스 T1~T4 에서 first-fit · best-fit · segregated 의 **활용률(utilization)**, 탐색 노드 수, 처리량을 표로 비교한다 (실측: T2 에서 U 0.733 → 0.921, 최악 탐색 156 → 21 노드).
- **내부 단편화**(블록 − 요청), **외부 단편화** E = 1 − 최대 free / 전체 free, **"free 는 충분한데 실패"** 횟수를 정확히 정의하고, 손으로 답을 아는 힙에서 테스트한다.
- 힙 검사기 `mm_check` 에 클래스별 불변식(블록이 자기 크기의 리스트에 있나)을 더하고, 일부러 깨뜨린 힙에서 잡히는지 확인한다.
- 펌웨어 관점: 클래스별 보고서로 "조각이 어느 크기에 쌓였나" 를 진단하고, **요청 크기 반올림**으로 내부 단편화를 내주고 외부 단편화를 없애는 거래를 숫자로 본다 (T4: U 0.538 → 0.947, T2: 0.921 → 0.755).

## 1. 문제

"malloc 을 더 빠르게 하려면? 메모리는 남는데 할당이 실패하는 건 왜죠, 어떻게 재죠?" — 할당기 면접의 두 번째 질문이고, 펌웨어에서는 몇 달 켜 두는 기기(게이트웨이, 모뎀, 차량 ECU 의 Linux 쪽)가 "free 는 40 KB 인데 4 KB 버퍼가 안 잡힌다" 로 죽는 현장 문제다. 답의 뼈대는 늘 같다: **크기별로 리스트를 나누면(segregated) 탐색이 짧아지고 best-fit 에 가까워진다. 단편화는 내부/외부로 나눠서 숫자로 잰다.**

시리즈 위치: OS-M04 가 명시적 free list 를 만들었고(이 레슨은 그 **축약판**을 그대로 넣어서 혼자 돌아간다), 이 레슨이 크기 클래스와 계측을 더하고, OS-M06 이 이것을 arena 별 백엔드로 써서 스레드 캐시를 얹는다. 세 레슨은 **같은 힙 형식, 같은 API, 같은 트레이스 생성기**를 쓰므로 숫자를 서로 비교할 수 있다. 고정 크기 풀은 DS-M02, 슬랩은 DS-M03, 버디는 DS-M04, O(1) TLSF 는 DS-A06, FreeRTOS heap_1~5 는 RTOS-M10 이다. 여기서는 다시 가르치지 않고 비교만 한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">segregated free lists: 클래스 머리 배열 g_head[17] + 클래스마다 이중 연결 리스트</text>
<rect x="10" y="40" width="150" height="24" class="f-acc" stroke="currentColor"/><text x="16" y="57" fill="currentColor">0: 32 B (정확)</text>
<rect x="10" y="64" width="150" height="24" class="f-acc" stroke="currentColor"/><text x="16" y="81" fill="currentColor">1: 48 B (정확)</text>
<rect x="10" y="88" width="150" height="24" class="f-mut" stroke="currentColor"/><text x="16" y="105" fill="currentColor">2: 64 B (비었음)</text>
<rect x="10" y="112" width="150" height="24" class="f-mut" stroke="currentColor"/><text x="16" y="129" fill="currentColor">3..5: 80..112 B</text>
<rect x="10" y="136" width="150" height="24" class="f-acc" stroke="currentColor"/><text x="16" y="153" fill="currentColor">6: 128 B (정확)</text>
<rect x="10" y="160" width="150" height="24" class="f-ok" stroke="currentColor"/><text x="16" y="177" fill="currentColor">7: 129..256 B</text>
<rect x="10" y="184" width="150" height="24" class="f-ok" stroke="currentColor"/><text x="16" y="201" fill="currentColor">8: 257..512 B</text>
<rect x="10" y="208" width="150" height="24" class="f-mut" stroke="currentColor"/><text x="16" y="225" fill="currentColor">9..15: ..64 KiB</text>
<rect x="10" y="232" width="150" height="24" class="f-mut" stroke="currentColor"/><text x="16" y="249" fill="currentColor">16: &gt; 64 KiB</text>
<rect x="200" y="42" width="60" height="20" class="f-acc" stroke="currentColor"/><text x="214" y="57" fill="currentColor">32</text>
<rect x="290" y="42" width="60" height="20" class="f-acc" stroke="currentColor"/><text x="304" y="57" fill="currentColor">32</text>
<path d="M160 52 L198 52" stroke="currentColor" fill="none"/><path d="M260 52 L288 52" stroke="currentColor" fill="none"/>
<rect x="200" y="66" width="60" height="20" class="f-acc" stroke="currentColor"/><text x="214" y="81" fill="currentColor">48</text>
<path d="M160 76 L198 76" stroke="currentColor" fill="none"/>
<rect x="200" y="138" width="60" height="20" class="f-acc" stroke="currentColor"/><text x="210" y="153" fill="currentColor">128</text>
<path d="M160 148 L198 148" stroke="currentColor" fill="none"/>
<rect x="200" y="162" width="60" height="20" class="f-ok" stroke="currentColor"/><text x="210" y="177" fill="currentColor">224</text>
<rect x="290" y="162" width="60" height="20" class="f-ok" stroke="currentColor"/><text x="300" y="177" fill="currentColor">144</text>
<rect x="380" y="162" width="60" height="20" class="f-ok" stroke="currentColor"/><text x="390" y="177" fill="currentColor">256</text>
<path d="M160 172 L198 172" stroke="currentColor" fill="none"/><path d="M260 172 L288 172" stroke="currentColor" fill="none"/><path d="M350 172 L378 172" stroke="currentColor" fill="none"/>
<rect x="200" y="186" width="60" height="20" class="f-ok" stroke="currentColor"/><text x="210" y="201" fill="currentColor">464</text>
<path d="M160 196 L198 196" stroke="currentColor" fill="none"/>
<rect x="470" y="40" width="262" height="150" class="f-warn" stroke="currentColor"/>
<text x="480" y="60" fill="currentColor">mm_malloc(40) → 블록 asize = 64</text>
<text x="480" y="80" fill="currentColor">① 클래스 2 (64 B) 리스트: 비었음</text>
<text x="480" y="100" fill="currentColor">② 3, 4, 5 비었음 → 6 의 머리 128 B</text>
<text x="480" y="120" fill="currentColor">③ 위 클래스는 모든 블록 &gt;= 64</text>
<text x="490" y="138" fill="currentColor">→ 첫 노드에서 끝 (탐색 1)</text>
<text x="480" y="158" fill="currentColor">④ 128 을 64 + 64 로 쪼개고</text>
<text x="490" y="176" fill="currentColor">남은 64 는 클래스 2 리스트로</text>
<text x="10" y="285" fill="currentColor" font-size="12">리스트가 하나(OS-M04)면 같은 요청이 LIFO 머리부터 모든 free 블록을 훑는다. 클래스로 나누면</text>
<text x="10" y="303" fill="currentColor" font-size="12">"작은 클래스부터 위로" 가 곧 "맞는 것 중 작은 것부터" 이므로 best-fit 에 가까워지고, 대부분의 리스트는 짧다.</text>
<text x="10" y="321" fill="currentColor" font-size="12">각 블록은 자기 크기의 클래스에 정확히 한 번 있어야 한다 (mm_check A3).</text>
</svg>
```

**힙 형식 (OS-M04 와 같음, CS:APP 3e §9.9 방식)** — `mmap(MAP_PRIVATE|MAP_ANON)` 64 MiB 를 한 번 잡고 `mem_sbrk(incr)` 이 그 안에서 break 를 올린다(넘으면 NULL). 블록 = 헤더 8 B + 페이로드 + 푸터 8 B, 헤더 = 푸터 = `size | alloc(bit0)`, 페이로드 16 B 정렬, 최소 블록 32 B (헤더 + next + prev + 푸터). 힙 앞 32 B 는 패딩·프롤로그·에필로그다. 이 레슨은 푸터 생략(prev_alloc 비트)을 **쓰지 않는다**(연습 4 에서 효과만 계산). `malloc(0)` → NULL, `free(NULL)` 무동작, `realloc(NULL,n)` = malloc, `realloc(p,0)` = free 후 NULL — 마지막 것은 우리 선택이다(C23 은 크기 0 realloc 을 미정의 동작으로 바꾼 것으로 알고 있다).

**크기 클래스 17개** — 블록 크기(asize) 기준:

| 클래스 | 블록 크기 | 이유 |
|---|---|---|
| 0..6 | 32, 48, 64, 80, 96, 112, 128 (정확히 하나) | 작은 요청이 가장 많다. 리스트 안의 모든 블록이 같은 크기라 **머리가 늘 맞는다** (O(1)) |
| 7..15 | (128,256], (256,512], …, (32K,64K] | 2의 거듭제곱 구간. 클래스 안에서 고른 블록은 요청의 2배 미만 |
| 16 | > 64 KiB | 큰 블록은 드물다. 한 리스트에서 first-fit |

### Part A — 클래스 17개로 나누기

- **A1** `size_class(asize)` — 위 표. `POL_FIRST/POL_BEST` 이면 늘 0 (리스트 하나 = M04 기준선).
- **A2** `seg_find(asize, &visits)` — 자기 클래스에서 first-fit, 없으면 위 클래스로. 본 free 노드 수를 센다(결정적 "속도" 지표).
- **A3** `mm_check` 에 "리스트 c 의 모든 블록은 `size_class(bsize) == c`" 를 더한다. 주어진 검사(헤더=푸터, 정렬, 인접 free 없음, 링크, 리스트 개수·바이트 = 힙 걷기 결과 = 카운터)와 합쳐서 위반 수를 돌려준다.
- 검증: 클래스 경계표, 같은 손 시나리오에서 first-fit 은 LIFO 머리 224 B 를, seg 는 128 B 를 고름, 정확 클래스는 탐색 1 노드(first-fit 21 노드), 엉뚱한 클래스에 끼운 블록은 위반 정확히 1개, T1~T4 를 정책 3개로 재생(64 연산마다 `mm_check`, 페이로드 패턴 확인).

### Part B — 단편화 계측

정의 (이 레슨 전체에서 이 뜻으로만 쓴다):

| 기호 | 정의 | 언제 재나 |
|---|---|---|
| U (활용률) | peak(살아 있는 **요청** 바이트 합) / 힙 high-water(바이트) | 트레이스 끝. 우리 break 는 안 내려가므로 high-water = `mem_heapsize()` |
| I (내부 단편화) | Σ_살아 있는 블록 (블록 크기 − 요청 크기). 헤더/푸터 16 B, 정렬 패딩, 안 쪼갠 꼬리(< 32 B) 포함. 비율은 I / Σ 블록 크기 | 살아 있는 요청 합이 최고일 때 |
| E (외부 단편화) | 1 − (가장 큰 free 블록) / (free 바이트 합). free 가 0 이면 0 | 64 연산마다 표본, 평균·최대 |
| ext_fail | `find_fit` 실패 **그리고** free 바이트 합 ≥ asize — 즉 "전체로는 충분한데 한 덩어리가 없어서 힙을 늘린" 횟수 | 매 malloc, 힙을 늘리기 **전에** |
| visits | `find_fit` 이 본 free 노드 수 (평균·최대) | 매 malloc |

- **B1** `frag_internal(ptr[], req[], n)`, **B2** `frag_external()` (힙을 주소순으로 걷기), **B3** `mm_malloc` 안의 ext_fail 한 줄.
- 손으로 답을 아는 힙: 요청 16, 48, 16, 112, 16, 1 → 블록 32, 64, 32, 128, 32, 32. 두 번째·네 번째를 free → I = 16+16+16+31 = 79, free = 192, 최대 128, E = 1/3. `malloc(150)`(asize 176)은 실패 1회로 세고 힙을 정확히 176 늘린다. `malloc(500)`(528 > 192)은 단편화가 아니라 그냥 부족이라 안 센다.
- 재생 중 64 연산마다 "증분으로 센 내부 단편화 = `frag_internal` 로 다시 센 값" 을 확인한다.

### Part C — 펌웨어: 어디에 조각이 쌓였나, 요청을 어떻게 고치나

LTE 모뎀 게이트웨이 데몬을 상상하자. 제어 메시지 64 B 와 데이터 프레임 448 B 를 번갈아 받다가, 펌웨어 업데이트로 프레임이 512 B 가 됐다. 오래 사는 64 B 사이에 448 B 구멍 500개가 생기고, 512 B 요청은 그 구멍에 하나도 못 들어간다 — T4 가 정확히 이 모양이다.

- **C1** `mm_class_report(rep)` — 클래스마다 free 블록 수, 바이트, 최소/최대 크기. T4 를 448 을 다 푼 시점(연산 1500)에서 멈추고 보고서를 읽어 "클래스 8 에 464 B 블록 500개 = 232000 B, 그런데 요청은 클래스 9(528 B)" 를 확인한다.
- **C2** `fw_round(n)` — 128 B 이하는 그대로(정확 클래스가 있으니까), 그 위는 페이로드를 다음 2의 거듭제곱으로. 448 과 512 가 둘 다 512 가 되어 같은 블록 크기 528 B 를 쓰므로 구멍을 그대로 재사용한다. T4 와 T2 에 켜서 U·I·ext_fail 이 어떻게 움직이는지 본다.

## 2. Problem (English)

> Improve an explicit-free-list allocator (boundary tags, immediate coalescing, splitting, realloc) with **segregated free lists**.
>
> - Heap: one 64 MiB anonymous mapping, grown by `mem_sbrk`. Blocks carry an 8-byte header and footer (`size | alloc`), payloads are 16-byte aligned, the minimum block is 32 bytes.
> - Size classes over the block size: exact classes 32, 48, …, 128; power-of-two ranges (128,256] … (32K,64K]; one class above 64 KiB. Free blocks are kept LIFO in the list of their class.
> - `find_fit`: first fit inside the request's class; if nothing fits, take the first block of the next non-empty larger class.
> - Keep `mm_init / mm_malloc / mm_free / mm_realloc / mm_check(verbose)`. `mm_check` must also report a free block that sits in the wrong class list.
> - Instrument: internal fragmentation `Σ(block − requested)` over live blocks; external fragmentation `1 − largest_free / total_free`; the number of requests that had to grow the heap although `total_free ≥ asize`; free-list nodes visited per malloc.
> - Replay four fixed traces (small-uniform, mixed, realloc-grow, binary-pattern) and compare utilization (peak live requested bytes / heap high-water) and throughput against single-list first-fit and best-fit.
>
> Example: requests 16, 48, 16, 112, 16, 1 then free the 2nd and 4th → internal 79 B, free 192 B, largest 128 B, E = 1/3; `malloc(150)` counts one "failed despite enough total free" and grows the heap by exactly 176 B.
>
> Follow-ups: why is segregated fit close to best fit? Why didn't throughput improve on every trace? Which fragmentation does request rounding remove, and what does it cost?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.14 초, exit 1):

```text
-- given: OS-M04 baseline behaviour (passes already in the starter)
PASS mm_malloc(0) == NULL && mm_check(0) == 0
…
-- Part A: size classes, segregated fit, per-class heap checker
FAIL starter.c:549  size_class(32) == 0 && size_class(48) == 1 && size_class(128) == 6
…
PASS c == b && mm_check(1) == 0
FAIL starter.c:559  c == a && mm_blksize(c) == 64 && g_head[size_class(64)] == a + 64
…
  T2 mixed           seg       U=0.733 heap= 1560384 peak= 1143214  I=0.009  Emean=0.000 Emax=0.000 ext_fail=   0  visits avg=    3.9 max=  156
…
FAIL starter.c:669  r2.ifrag > R[1][POL_SEG].ifrag && r2.corrupt == 0 && r2.check_bad == 0
14/47 passed
```

TODO 는 A1–A3, B1–B3, C1–C2 여덟 곳이다. 스텁 `size_class` 는 늘 0 을 돌려주므로 `POL_SEG` 가 리스트 하나짜리 first-fit 과 **똑같이** 동작한다 — 위 출력에서 seg 줄이 first 줄과 숫자가 같은 이유다. 할당기 자체는 멀쩡하므로 재생은 깨지지 않고(`fails == 0 && corrupt == 0` 은 PASS), 계측 함수는 0 을 돌려줘서 FAIL 한다. 힙은 `mmap` 영역 하나라서 시작 코드도 어떤 경우에도 멈추거나 죽지 않는다.

## 4. 힌트

<details><summary>힌트 1 — 클래스 번호</summary>

정확 클래스는 16 B 간격이니 `asize / 16 − 2` (32 → 0). 그 위는 상한 256 부터 시작해 `asize` 가 들어갈 때까지 상한을 2배씩 키우며 번호를 올린다. 마지막 클래스(16)에서 멈춘다. `asize` 는 늘 16 의 배수이고 32 이상이라는 것을 이용하라. 리스트에 넣을 때와 찾을 때 **같은 함수, 같은 기준(블록 크기)** 을 써야 한다.
</details>

<details><summary>힌트 2 — 위 클래스는 왜 첫 노드면 되나</summary>

클래스 c 의 모든 블록은 c 의 상한 이하, 클래스 c+1 의 모든 블록은 c 의 상한보다 크다. 요청 asize 는 클래스 c 에 속하므로 asize ≤ c 의 상한 < (c+1 의 어떤 블록). 그러니 위 클래스에서는 크기 비교가 필요 없다. 시작 코드의 스텁처럼 자기 클래스만 보고 끝내면, 위에 맞는 블록이 있어도 힙을 늘린다 → U 가 떨어진다.
</details>

<details><summary>힌트 3 — 클래스 불변식 테스트가 1 이어야 하는 이유</summary>

테스트는 블록을 `list_remove` 로 빼고(통계 −1) 엉뚱한 리스트에 직접 끼운 뒤 통계를 되돌린다(+1). 그래서 "개수·바이트 일치" 같은 주어진 검사는 조용하고, 오직 "클래스가 맞나" 만 위반이다. 검사는 리스트를 걸을 때 노드마다 `size_class(bsize(f)) != c` 이면 한 번.
</details>

<details><summary>힌트 4 — 계측은 어디에 두나</summary>

할당기 안에는 **요청 크기 정보가 없다**(블록 크기만 있다). 그래서 내부 단편화는 요청 크기를 아는 쪽(재생기의 `g_req[]`)이 인자로 넘겨야 한다. 외부 단편화는 리스트가 아니라 힙을 주소순으로 걸어서 센다(리스트가 틀려도 정답이 나오게). ext_fail 은 `find_fit` 이 NULL 을 돌려준 직후, **힙을 늘리기 전에** `g_st.free_bytes` 와 비교한다 — 늘린 뒤에는 free 가 이미 늘어 있다.
</details>

<details><summary>힌트 5 — 448 과 512 를 같은 구멍에</summary>

448 B 요청의 블록은 464 B, 512 B 요청의 블록은 528 B 라서 서로의 구멍에 못 들어간다. 블록 크기(asize) 기준으로 반올림하면 464 → 512, 528 → 1024 로 다시 갈라진다. **요청(페이로드) 기준**으로 2의 거듭제곱에 맞추면 448 → 512, 512 → 512 가 되어 둘 다 528 B 블록을 쓴다.
</details>

## 5. 풀이

### Part A — 클래스 설계: 왜 작은 것은 정확, 큰 것은 2배 구간인가

클래스 경계는 두 가지를 맞바꾼다. 클래스가 촘촘할수록 한 클래스 안의 블록 크기가 비슷해서 first-fit 이 곧 best-fit 이 되지만, 클래스 머리 배열이 커지고 빈 클래스를 건너뛰는 일이 늘어난다. 실제 요청 분포는 작은 쪽에 몰려 있다(T1 은 전부 128 B 이하, T2 는 70 % 가 256 B 이하). 그래서 128 B 까지는 블록 크기 하나당 클래스 하나(7개)를 주고, 그 위는 2배 구간으로 묶었다. 결과:

- **정확 클래스** (0..6): 리스트의 모든 블록 크기가 같으므로 머리가 늘 맞는다. 쪼갤 일도 없다. T1 에서 seg 의 최악 탐색이 **1 노드**인 이유다.
- **2배 구간** (7..15): 클래스 안 first-fit 이 고른 블록은 클래스 상한 이하이므로 요청의 2배 미만이다. 남는 부분은 `place` 가 떼어 free 로 돌려주므로 "2배" 가 내부 단편화가 되는 것은 아니다. 다만 어느 큰 블록이 쪼개지느냐가 best-fit 과 조금 다르다(그래서 U 가 best 와 정확히 같지는 않다).
- **마지막 클래스** (> 64 KiB): T2 의 5 % 만 여기 온다. 리스트가 짧아서 first-fit 으로 충분하다.

`size_class` 는 블록 크기를 받는다. 요청 크기로 클래스를 정하면 같은 블록이 넣을 때와 찾을 때 다른 클래스로 계산될 수 있어서 위험하다(7. 자주 하는 실수).

### Part A — seg_find: "자기 클래스 first-fit + 위로" 가 best-fit 에 가까운 이유

best-fit 은 "맞는 블록 중 가장 작은 것" 이다. 클래스를 작은 것부터 올라가며 처음 만나는 맞는 블록을 고르면, 그 블록보다 작은 맞는 블록은 **같은 클래스 안에만** 있을 수 있다(아래 클래스는 다 봤거나 asize 보다 작다). 그래서 오차는 한 클래스 폭 안에 갇힌다. 정확 클래스에서는 오차가 0 이다.

OSTEP 시뮬레이터로도 같은 성질을 볼 수 있다: 리스트를 **크기순**으로 정렬해 두고 first-fit 을 하면 best-fit 과 똑같은 블록을 고른다. 클래스 배열은 이 정렬을 "대충, 그러나 거의 공짜로" 해 두는 셈이다 (OSTEP 17장 §17.3, `vm-freespace/malloc.py` 를 내 작업 폴더에 복사해서 읽기 전용으로 실행):

```text
--- python3 -I malloc.py -S 100 -b 1000 -H 4 -a 4 -n 10 -s 8 -p BEST -l ADDRSORT -c
ptr[3] = Alloc(5) returned 1020 (searched 3 elements)
ptr[4] = Alloc(9) returned 1004 (searched 2 elements)
…
final: Free List [ Size 1 ]: [ addr:1068 sz:32 ]
--- python3 -I malloc.py -S 100 -b 1000 -H 4 -a 4 -n 10 -s 8 -p FIRST -l ADDRSORT -c
ptr[3] = Alloc(5) returned 1004 (searched 1 elements)
ptr[4] = Alloc(9) returned 1040 (searched 3 elements)
…
final: Free List [ Size 3 ]: [ addr:1012 sz:4 ][ addr:1024 sz:4 ][ addr:1076 sz:24 ]
--- python3 -I malloc.py -S 100 -b 1000 -H 4 -a 4 -n 10 -s 8 -p FIRST -l SIZESORT+ -c
ptr[3] = Alloc(5) returned 1020 (searched 1 elements)
ptr[4] = Alloc(9) returned 1004 (searched 1 elements)
…
final: Free List [ Size 1 ]: [ addr:1068 sz:32 ]
```

시드 0~40 중 first 와 best 가 다른 결과를 낸 시드는 8, 25, 34 였고, 셋 모두 `FIRST + SIZESORT+` 의 반환 주소가 `BEST` 와 같았다. 주소순 first-fit 은 4 B 짜리 쓸모없는 조각 두 개를 남겼다. 크기순 first-fit 은 best-fit 과 같은 답을 **더 적게 훑어서**(1 원소) 낸다.

### Part A — 리스트 연산과 검사기 불변식

`list_insert`/`list_remove` 는 블록 크기로 클래스를 계산한다. 그래서 **크기를 바꾸기 전에 빼고, 바꾼 뒤에 넣는다**. 병합(`coalesce`)이 이웃을 먼저 `list_remove` 하고 나서 합친 크기를 쓰는 것, `place` 가 먼저 빼고 나서 쪼개는 것이 모두 이 규칙 때문이다. 우리 `mm_check` 의 불변식:

| # | 불변식 | 깨지면 |
|---|---|---|
| 1 | 프롤로그(16\|1) 두 워드, 에필로그(0\|1) 가 break 바로 앞 | 힙 걷기 자체가 틀림 |
| 2 | 블록마다 페이로드 16 B 정렬, 크기 ≥ 32 이고 16 배수, 힙 안 | 걷기를 멈추고 1 보고 |
| 3 | 헤더 == 푸터 | 이웃 병합이 엉뚱한 크기를 읽음 |
| 4 | 인접한 free 블록이 없다 (즉시 병합) | 단편화가 실제보다 커짐 |
| 5 | 리스트 노드: 힙 안, free 표시, prev 링크 맞음, 사이클 없음 | 이중 할당, 무한 루프 |
| **6 (A3)** | 리스트 c 의 노드는 `size_class(bsize) == c` | 찾을 블록을 엉뚱한 리스트에서 놓침 |
| 7 | 힙 걷기의 free 개수·바이트 = 리스트 전체 = `g_st` 카운터 | 블록 누수(어느 리스트에도 없음) |

검사는 비싸서(O(블록 수)) 테스트에서만 64 연산마다 부른다. demo_bench 는 끈다.

### Part A — 결과: 같은 트레이스, 정책 3개

트레이스 생성기(`xorshift32`, `gen_mix`, `gen_realloc`, `gen_binary`)는 **OS-M04 solution.c 의 것을 그대로 옮겼다**(저장만 정적 배열로 바꿈). 그래서 연산열이 끝의 free 순서까지 OS-M04 와 똑같다. T1/T2 는 2000 연산(live < 50 이면 무조건 alloc, 아니면 `%100 < 60` 이면 alloc, 아니면 임의 live free) 뒤 남은 것을 live 배열 뒤에서부터 free. T3 는 버퍼 200개마다 목표 크기(1..32 KiB)를 정하고 임의 버퍼를 골라 +1..+512 B 씩 realloc, realloc 마다 25 % 작은 alloc(1..64 B), 20 % 작은 것 free. T4 는 64/448 500쌍 → 448 모두 free → 512 B 500개 → 모두 free. 연산 수: T1 2466, T2 2438, T3 19198 (realloc 12504), T4 3000.

solution 실행의 실제 표 (모든 값은 결정적):

| 트레이스 | 정책 | U | 힙 (B) | I@peak | E 평균 | E 최대 | ext_fail | 탐색 평균 | 탐색 최대 |
|---|---|---|---|---|---|---|---|---|---|
| T1 small-uniform | first | 0.665 | 43296 | 0.294 | 0.909 | 0.970 | 299 | 10.1 | 69 |
| | best | 0.695 | 41392 | 0.294 | 0.770 | 0.968 | 287 | 5.4 | 20 |
| | **seg** | **0.696** | 41360 | 0.295 | 0.732 | 0.966 | 267 | **0.7** | **1** |
| T2 mixed | first | 0.733 | 1560384 | 0.009 | 0.831 | 0.924 | 37 | 3.9 | 156 |
| | best | 0.924 | 1236896 | 0.009 | 0.615 | 0.835 | 21 | 22.9 | 56 |
| | **seg** | **0.921** | 1240640 | 0.010 | 0.676 | 0.817 | 24 | **2.2** | **21** |
| T3 realloc-grow | first | 0.302 | 10731200 | 0.006 | 0.978 | 0.996 | 990 | 41.4 | 660 |
| | best | 0.555 | 5838016 | 0.007 | 0.960 | 0.990 | 670 | 117.5 | 238 |
| | **seg** | **0.553** | 5860624 | 0.006 | 0.966 | 0.990 | 694 | **5.4** | **87** |
| T4 binary-pattern | first / best | 0.538 | 535568 | 0.053 | 0.420 | 0.998 | 500 | 166.3 | 500 |
| | **seg** | 0.538 | 535568 | 0.053 | 0.420 | 0.998 | 500 | **0.0** | **0** |

읽는 법:

- **U**: seg 는 T1·T2·T3 모두 best-fit 과 0.003 이내다(T3 0.553 vs 0.555). first-fit 은 LIFO 머리부터 아무 블록이나 쪼개서 큰 블록을 망가뜨린다(T2 0.733, T3 0.302).
- **OS-M04 와 대조**: 트레이스가 같으므로 first/best 열은 OS-M04 의 명시적 first/best 와 맞아야 한다. OS-M04 값은 T1 0.665/0.698, T2 0.732/0.887, T3 0.302/0.572, T4 0.537 이고, 우리는 0.665/0.695, 0.733/0.924, 0.302/0.555, 0.538 이다. 차이는 힙을 늘리는 정책 두 가지 때문이다. 우리 `mm_malloc` 은 마지막 블록이 free 면 모자란 만큼만 늘리고(M04 는 asize 전부), 우리 `mm_realloc` 은 힙 끝에서 정확히 모자란 만큼 늘린다(M04 는 최소 32 B 후 `place`). 내 작업 폴더의 복사본에서 이 두 곳만 M04 식으로 바꿨더니 여덟 값이 모두 OS-M04 와 같아졌다.
- **탐색**: seg 는 best-fit 의 정확도를 내면서 탐색은 first-fit 보다도 짧다. T4 는 극단적이다. 512 요청(블록 528, 클래스 9)이 올 때 448 구멍 500개는 클래스 8 에 있어서 **보지도 않는다**. first/best 는 매번 구멍을 다 훑고 실패한다(평균 166.3 = 512 요청 500번이 구멍 500, 499, 499, … 개를 훑은 합 249501 ÷ malloc 1500번). seg 의 0 은 "빈 클래스 머리를 몇 개 봤나" 는 세지 않은 값이다(연습 1 의 비트맵이 그 비용까지 없앤다).
- **T4 의 U 0.538 은 정책으로 못 고친다.** 오래 사는 64 B 블록이 448 구멍을 갈라놓았고, 우리 할당기는 블록을 옮길 수 없다(C 포인터는 옮기면 깨진다 — OSTEP 17장이 compaction 이 불가능하다고 하는 이유). 고칠 곳은 요청 쪽이다(Part C).
- T3 realloc: seg 는 12504 realloc 중 8846 을 자리에서(뒤 free 블록 흡수, 힙 끝 확장), 3658 을 옮겨서 처리했다(first 7712/4792, best 6958/5546 — 별도 실행으로 센 값). seg 가 덜 옮기는 것은 작은 블록이 작은 클래스 블록으로 가서 버퍼 바로 뒤의 큰 free 블록을 덜 쪼개기 때문으로 본다(추정).

### Part B — 내부 단편화: 손 계산부터

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">손으로 만든 힙: 요청 16, 48, 16, 112, 16, 1 → 2번째와 4번째 free</text>
<rect x="10" y="40" width="40" height="40" class="f-mut" stroke="currentColor"/><text x="14" y="64" fill="currentColor" font-size="12">32</text>
<rect x="50" y="40" width="60" height="40" class="f-acc" stroke="currentColor"/><text x="56" y="58" fill="currentColor" font-size="12">p1 32</text><text x="56" y="74" fill="currentColor" font-size="12">req 16</text>
<rect x="110" y="40" width="110" height="40" class="f-ok" stroke="currentColor"/><text x="118" y="64" fill="currentColor">free 64</text>
<rect x="220" y="40" width="60" height="40" class="f-acc" stroke="currentColor"/><text x="226" y="58" fill="currentColor" font-size="12">p3 32</text><text x="226" y="74" fill="currentColor" font-size="12">req 16</text>
<rect x="280" y="40" width="200" height="40" class="f-ok" stroke="currentColor"/><text x="340" y="64" fill="currentColor">free 128 (최대)</text>
<rect x="480" y="40" width="60" height="40" class="f-acc" stroke="currentColor"/><text x="486" y="58" fill="currentColor" font-size="12">p5 32</text><text x="486" y="74" fill="currentColor" font-size="12">req 16</text>
<rect x="540" y="40" width="60" height="40" class="f-acc" stroke="currentColor"/><text x="546" y="58" fill="currentColor" font-size="12">p6 32</text><text x="546" y="74" fill="currentColor" font-size="12">req 1</text>
<rect x="600" y="40" width="130" height="40" class="f-warn" stroke="currentColor" stroke-dasharray="4 3"/><text x="606" y="58" fill="currentColor" font-size="12">새로 +176</text><text x="606" y="74" fill="currentColor" font-size="12">malloc(150)</text>
<text x="10" y="102" fill="currentColor" font-size="12">프롤로그+패딩+에필로그 32 B</text>
<text x="10" y="132" fill="currentColor">내부 I = 16 + 16 + 16 + 31 = 79 B (헤더/푸터 16 B 가 대부분, malloc(1) 은 31 B 낭비)</text>
<text x="10" y="157" fill="currentColor">외부: free 합 F = 64 + 128 = 192, 최대 L = 128 → E = 1 − 128/192 = 1/3</text>
<text x="10" y="182" fill="currentColor">malloc(150): asize = 176 &gt; L = 128 이지만 F = 192 &gt;= 176 → ext_fail = 1, 힙 +176 (352 → 528)</text>
<text x="10" y="207" fill="currentColor">malloc(500): asize = 528 &gt; F = 192 → 단편화가 아니라 그냥 부족 → ext_fail 그대로 1</text>
<text x="10" y="232" fill="currentColor">malloc(100): asize = 128 → free 128 에 딱 맞음 → 남은 free {64} 하나, E = 0</text>
</svg>
```

내부 단편화는 할당기가 **요청보다 많이 준 바이트**다. 우리 형식에서는 세 가지가 섞여 있다: ① 헤더/푸터 16 B (모든 블록), ② 16 B 정렬 패딩 (요청 1 → 블록 32), ③ 쪼개기에는 너무 작은 꼬리 (남는 게 32 B 미만이면 통째로 준다). 할당기는 요청 크기를 저장하지 않으므로(저장하면 그 자체가 오버헤드), 재는 쪽이 요청 크기를 들고 있어야 한다. 재생기는 두 방법으로 세고 서로 확인한다: 연산마다 `live_blk − live_req` 를 증분으로 갱신하고, 64 연산마다 `frag_internal(g_p, g_req, nids)` 로 다시 센다. 네 트레이스 모두 불일치 0 이다.

결과 표의 I 를 보면 T1 은 0.294(작은 요청에 16 B 헤더/푸터가 크다), T2·T3 는 0.01 이하(큰 요청이라 16 B 가 묻힌다). 작은 객체가 많은 펌웨어에서 헤더 오버헤드가 왜 아픈지, 그리고 슬랩/풀(DS-M02, DS-M03)이 왜 헤더를 없애는지가 이 숫자다. 푸터를 할당 블록에서 생략하면 요청당 평균 23.6 → 16.5 B 로 준다(연습 4).

### Part B — 외부 단편화: E 와 ext_fail, 그리고 E 의 한계

E = 1 − L/F 는 "free 바이트 중 가장 큰 덩어리 밖에 흩어진 비율" 이다. 0 이면 free 가 한 덩어리, 1 에 가까우면 잘게 쪼개졌다. RTOS-M10 의 heap 감시 코드(`1 − largest/free`)와 같은 정의다. 하지만 표를 보면 T1 의 E 평균이 0.73~0.91 로 T4(0.420)보다 **높다**. T1 의 요청은 전부 128 B 이하라 48 B 짜리 free 조각도 다음 요청에 충분히 쓸모 있는데, E 는 요청 크기를 모르니 "잘게 쪼개졌다" 고만 말한다. 그래서 두 지표를 같이 본다:

- **ext_fail** (사건 수): 실제로 단편화 때문에 힙이 커진 횟수. T4 에서 정확히 500 = 512 요청 500번 모두. 세 정책 모두 500 이다(정책으로 못 고친다는 증거).
- **E(r)** (요청 크기 r 기준): r 보다 작은 free 블록의 바이트 비율. T4 를 연산 1500 에서 멈추면 E = 0.998 인데 E(464) = 0, E(528) = 1. "448 요청에는 완벽, 512 요청에는 전부 쓸모없음" 을 정확히 말한다(연습 2).

끝에서 재면 안 된다: 트레이스 끝에 전부 free 하면 즉시 병합 덕분에 free 블록이 하나(E = 0)가 된다. 테스트도 이것을 확인한다(`e.nfree == 1 && e.free_bytes == mem_heapsize() - 32`). 단편화는 **살아 있는 동안** 표본을 떠서 재야 한다.

### Part B — 처리량: `demo_bench.c`

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

Apple M6, Apple clang 21.0.0, 채우기·검사 끔, 트레이스를 T1 400번 / T2 40번 / T3 4번 / T4 40번 재생한 시간의 5회 중앙값. 실제 출력:

```text
load average: 9.90 13.08 12.63  (shared machine: read ratios, not absolute numbers)
trace                 ops |    first     best      seg     libc |  U.fst  U.bst  U.seg | seg/fst seg/bst
T1 small-uniform     2466 |    127.0    173.4    157.3    192.1 |  0.665  0.695  0.696 |    1.2x    0.9x
T2 mixed             2438 |    149.8     93.9    135.3    103.2 |  0.733  0.924  0.921 |    0.9x    1.4x
T3 realloc-grow     19198 |      6.6      7.1     11.3     18.8 |  0.302  0.555  0.553 |    1.7x    1.6x
T4 binary-pattern    3000 |     12.8     13.1     95.7    132.2 |  0.538  0.538  0.538 |    7.5x    7.3x
(Mops/s = trace ops per second, median of 5 runs; U = peak live requested / heap high-water)
```

같은 명령을 두 번 더 돌렸을 때 seg/first 는 T1 1.1~1.2×, T2 0.7~0.8×, T3 1.8~2.1×, T4 7.4~7.6× 였다(부하 10~13 의 공유 맥이라 ±20 % 이상 흔들림). 정직한 결론:

- **리스트가 짧으면 seg 는 빠르지 않다.** T1·T2 는 살아 있는 블록이 수백 개라 free 리스트도 짧다(first-fit 평균 탐색 3.9~10 노드). 이 정도면 노드 몇 개를 덜 보는 이득과 `size_class` 계산(삽입·제거·탐색마다 한 번씩, 2배 구간은 루프) 비용이 비슷하다. 연산당 6~8 ns 수준이라 차이가 잡음에 묻힌다.
- **리스트가 길면 결정적으로 이긴다.** T4 는 약 25만 노드 탐색을 0 으로 만들어 7~8 배, T3 는 first 대비 1.7~2.1 배, best 대비 1.6~2.9 배.
- **seg 의 가치는 평균보다 최악이다**: 최악 탐색 156 → 21 (T2), 660 → 87 (T3). 실시간 펌웨어에서는 이 숫자가 중요하다. 진짜 O(1) 이 필요하면 TLSF(DS-A06) 처럼 클래스 안까지 비트맵으로 나눈다.
- T3 은 realloc 이동 복사가 시간의 큰 몫이다. 한 번 재생에 이동 복사가 seg 26.5 MB, first 46.3 MB, best 42.4 MB 다(별도 실행으로 셈). seg 의 T3 이득 일부는 탐색이 아니라 덜 옮기는 데서 온다. libc 는 세 번 중 T3 에서 seg 와 비슷하거나 1.7 배 빨랐다. 이유는 확인하지 않았다(macOS libmalloc 이 큰 블록을 페이지 단위로 다뤄 복사를 피할 수 있다고 알고 있으나 미검증).

### Part C — 클래스 보고서: 조각은 어디에 있나

T4 를 연산 1500(448 을 모두 푼 직후)에서 멈추고 `mm_class_report` 를 찍은 실제 출력:

```text
  class  8 (<=   512 B):  500 free blocks,  232000 B, sizes 464..464
```

한 줄이 진단의 전부다. free 232000 B 가 전부 클래스 8 에, 같은 크기 464 B 로 있다. 다음 요청 512 B 는 블록 528 B = 클래스 9 다. 서로 만날 일이 없다. `vPortGetHeapStats` 식 요약(free 합, 최대 블록, 블록 수 — RTOS-M10)은 "free 는 많은데 최대가 작다" 까지만 말해 주고, 클래스별 보고서는 **어떤 크기**의 조각인지까지 말해 준다. glibc 의 `malloc_info()` 가 bin 별 XML 을 내는 것도 같은 목적이다(Linux, 출력 형식은 확인 안 함).

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">T4: 오래 사는 64 B 가 구멍을 가른다 — 반올림 전/후</text>
<text x="10" y="50" fill="currentColor">전</text>
<rect x="40" y="35" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="46" y="53" fill="currentColor" font-size="12">80</text>
<rect x="80" y="35" width="120" height="26" class="f-ok" stroke="currentColor"/><text x="90" y="53" fill="currentColor" font-size="12">구멍 464</text>
<rect x="200" y="35" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="206" y="53" fill="currentColor" font-size="12">80</text>
<rect x="240" y="35" width="120" height="26" class="f-ok" stroke="currentColor"/><text x="250" y="53" fill="currentColor" font-size="12">구멍 464</text>
<text x="370" y="53" fill="currentColor">… ×500</text>
<rect x="470" y="35" width="136" height="26" class="f-warn" stroke="currentColor"/><text x="478" y="53" fill="currentColor" font-size="12">512 → 528 새로</text>
<text x="620" y="53" fill="currentColor" font-size="12">×500</text>
<text x="40" y="84" fill="currentColor" font-size="12">528 &gt; 464: 구멍 500개(232000 B) 못 씀 → ext_fail 500, 힙 535568 B, U = 0.538</text>
<text x="10" y="130" fill="currentColor">후</text>
<rect x="40" y="115" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="46" y="133" fill="currentColor" font-size="12">80</text>
<rect x="80" y="115" width="136" height="26" class="f-ok" stroke="currentColor"/><text x="88" y="133" fill="currentColor" font-size="12">구멍 528</text>
<rect x="216" y="115" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="222" y="133" fill="currentColor" font-size="12">80</text>
<rect x="256" y="115" width="136" height="26" class="f-ok" stroke="currentColor"/><text x="264" y="133" fill="currentColor" font-size="12">구멍 528</text>
<text x="400" y="133" fill="currentColor">… ×500</text>
<path d="M538 150 L538 176" stroke="currentColor" fill="none"/>
<text x="470" y="192" fill="currentColor" font-size="12">512 → 528: 구멍에 딱 맞음</text>
<text x="40" y="164" fill="currentColor" font-size="12">fw_round: 448 → 512, 512 → 512 (요청 기준 2의 거듭제곱) → 둘 다 블록 528</text>
<text x="40" y="214" fill="currentColor" font-size="12">ext_fail 0, 힙 = 32 + 500 × (80 + 528) = 304032 B, U = 288000 / 304032 = 0.947</text>
<text x="40" y="239" fill="currentColor" font-size="12">대가: 448 요청마다 64 B 를 더 줌(내부 단편화). T4 는 이득, 크기가 흩어진 T2 는 손해 (아래 표)</text>
<text x="40" y="262" fill="currentColor" font-size="12">U 의 분자는 원래 요청 크기 그대로 센다 — 반올림으로 늘어난 바이트는 낭비로 잡힌다</text>
</svg>
```

### Part C — 요청 반올림: 내부 단편화를 내주고 외부 단편화를 없앤다

`fw_round` 를 켠 재생의 실제 줄(solution 출력)과 끈 줄을 나란히:

| | U | 힙 (B) | I@peak | ext_fail | 탐색 최대 |
|---|---|---|---|---|---|
| T4 seg | 0.538 | 535568 | 0.053 | 500 | 0 |
| T4 seg + fw_round | **0.947** | 304032 | 0.053 | **0** | 1 |
| T2 seg | **0.921** | 1240640 | 0.010 | 24 | 21 |
| T2 seg + fw_round | 0.755 | 1515040 | **0.234** | 10 | 1 |

두 힙 크기 535568 과 304032 는 손으로 계산한 값과 바이트 단위로 같다(테스트가 정확한 값을 CHECK 한다). 535568 = 32 + 500 × 544 + 499 × 528 + 64 다. OS-M04 의 손 계산 536032 = 32 + 500 × 544 + 500 × 528 보다 464 B 작다. 첫 512 요청은 맨 끝 구멍(마지막 448 이 풀린 자리)에 모자란 64 B 만 붙여서 쓰기 때문이다(`mm_malloc` 이 "마지막 블록이 free 면 모자란 만큼만 늘린다").

반올림은 **외부 단편화를 내부 단편화로 바꾸는 거래**다. 요청 크기가 몇 가지로 정해진 작업(T4: 64/448/512)에서는 크게 이기고, 크기가 고르게 흩어진 작업(T2)에서는 평균 1.33배를 더 주느라 U 가 0.921 → 0.755 로 떨어진다(I 0.010 → 0.234). 펌웨어 규칙 "할당 크기를 몇 가지로 고정하라" 가 바로 이것이다. LwIP 의 `MEMP` 풀이나 Zephyr `k_mem_slab`, FreeRTOS 위의 고정 블록 풀(DS-M02)은 반올림을 극단까지 밀어서 외부 단편화를 0 으로 만든다. 이 레슨의 seg 할당기는 그 중간이다: 128 B 까지는 정확 클래스(반올림 16 B 단위), 그 위는 분할·병합으로 정확한 크기를 준다.

### Part C — 실제 할당기와 비교

| 할당기 | 클래스(빈, bin) | 클래스 안 | 병합 | 비고 |
|---|---|---|---|---|
| **이 레슨 seg** | 정확 7 + 2배 구간 10 | LIFO, first-fit | 즉시, 경계 태그 | 탐색 노드 수를 셈 |
| OS-M04 기준선 | 리스트 1개 | LIFO first/best | 즉시 | T4 에서 500 노드 탐색 |
| FreeRTOS heap_4 (RTOS-M10) | 리스트 1개, 주소순 | first-fit | 즉시 | 결정성 없음, `vPortGetHeapStats` |
| TLSF (DS-A06) | 2단계(2배 구간 × 선형 세분) + 비트맵 | 머리만 | 즉시 | 최악 O(1), 실시간 RTOS |
| glibc ptmalloc (Linux) | tcache, fastbin, smallbin(16 B 간격), largebin(로그 간격, 크기순) | 빈마다 다름 | fastbin 은 지연 병합 | 기억에 의존 — 버전별 상수는 glibc `malloc.c` 주석 확인 |
| dlmalloc / newlib (Cortex-M 툴체인) | 작은 빈(8 B 간격) + 큰 빈(트리 또는 정렬 리스트) | best-fit 계열 | 즉시 | 기억 기준. newlib 은 dlmalloc 계열의 옛 버전 (미검증) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 47/47 passed, exit 0, 0.16 s
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_asan && ./sol_asan   # 47/47, 보고 0
```

5번 연속 실행과 `/tmp` 를 cwd 로 둔 실행 모두 `47/47 passed`. 스레드를 쓰지 않으므로 TSan 은 해당 없음. ASan 이 조용한 것이 할당기 **안쪽**이 맞다는 증거는 아니다 — 아래 Q8.

## 7. 자주 하는 실수

**1. 크기를 바꾼 뒤에 `list_remove`** — 제거는 `size_class(bsize(bp))` 로 리스트를 고른다. 앞 이웃과 병합할 때 앞 블록의 크기부터 키우고 나서 빼면, 커진 크기의 클래스 머리를 고치게 된다. 앞 블록이 원래 리스트의 머리였다면 그 리스트는 여전히 이미 병합된 블록을 가리키고, 엉뚱한 리스트의 머리는 끊긴다.

```c
/* 틀림 */ set_blk(pv, bsize(pv) + s, 0); list_remove(pv);     /* 새 크기의 클래스에서 빼려 함 */
/* 맞음 */ list_remove(pv); s += bsize(pv); set_blk(pv, s, 0); list_insert(pv);
```

검사기 6번(A3)과 7번이 바로 잡는다. 개수·바이트 카운터를 `list_insert/list_remove` 안에서만 갱신하는 것도 같은 이유다(한 곳에서만 바뀌어야 맞추기 쉽다).

**2. 클래스를 요청 크기로 계산** — `find` 는 `size_class(size)`, `insert` 는 `size_class(asize)` 처럼 기준이 다르면 클래스 경계 근처에서 블록을 놓친다(예: 요청 120 → asize 144 는 클래스 7, 그런데 120 으로 계산하면 정확 클래스 영역). 늘 **블록 크기**로만.

**3. 위 클래스로 안 올라감 / 위 클래스도 끝까지 훑음** — 시작 코드 스텁이 앞의 버그다. 위에 맞는 블록이 있어도 힙을 늘려서 U 가 떨어진다. 뒤의 버그는 틀리지는 않지만 쓸데없이 느리다(위 클래스는 첫 노드가 늘 맞는다, 힌트 2).

**4. 단편화를 트레이스 끝에서 재기** — 끝에는 모두 병합돼서 E = 0, I = 0 이다. 최고점에서(내부), 표본으로(외부), 사건으로(ext_fail) 잰다.

**5. ext_fail 을 힙을 늘린 뒤에 비교** — `extend_heap` 이 free 블록을 만들고 병합까지 하면 `free_bytes` 가 이미 asize 이상이다. 그러면 모든 확장이 "단편화 실패" 로 세진다. `find_fit` 실패 직후, 늘리기 전에 한 줄.

```c
if (!bp) {
    if (g_st.free_bytes >= asize) g_st.ext_fail++;   /* 여기, extend_heap 전 */
    …
    bp = extend_heap(need);
```

**6. U 의 분자에 반올림된 크기를 씀** — `fw_round` 를 켜고 분자를 반올림된 요청으로 세면 반올림 낭비가 "쓴 메모리" 로 둔갑해서 U 가 실제보다 좋아 보인다. 재생기는 `g_req[id]` 에 **원래** 요청을 기록한다.

## 8. 면접 Q&A

<details><summary>Q1. segregated fit 이 왜 best-fit 에 가깝나요? 정확히 best-fit 으로 만들려면?</summary>

클래스를 작은 것부터 올라가며 처음 맞는 블록을 고르므로, 고른 블록보다 작은 맞는 블록은 같은 클래스 안에만 있을 수 있다. 오차는 한 클래스 폭으로 묶이고, 크기 하나짜리 정확 클래스에서는 0 이다. 실측 U: T2 seg 0.921 vs best 0.924, T3 0.553 vs 0.555. 탐색은 평균 2.2 vs 22.9 노드(T2), 5.4 vs 117.5 노드(T3)다. 정확한 best-fit 이 필요하면 클래스 안을 크기순으로 유지한다(삽입 O(클래스 길이)). OSTEP `malloc.py` 에서도 `FIRST + SIZESORT+` 가 `BEST` 와 같은 블록을 고르는 것을 확인했다. 큰 클래스만 크기순으로 두는 게 흔한 절충이다(glibc largebin 이 크기순이라고 알고 있다). 꼬리: "best-fit 이 늘 좋나?" → 보장은 없다. 아주 작은 조각을 남긴다는 걱정이 흔하지만, Wilson 등(1995) 조사는 실제 프로그램 트레이스에서 best-fit 과 주소순 first-fit 이 둘 다 단편화가 적었다고 정리한다(기억 기준). 우리 T1 에서 best 는 LIFO first 보다 U 가 0.03 좋을 뿐이다.
</details>

<details><summary>Q2. 내부 단편화와 외부 단편화를 어떻게 정의하고 재나요?</summary>

내부 = 할당된 블록 안에서 요청보다 더 준 바이트. Σ(블록 − 요청)이다. 헤더/푸터, 정렬 패딩, 안 쪼갠 꼬리가 포함된다. 할당기는 요청 크기를 모르므로 재는 쪽이 들고 있어야 한다. 외부 = free 바이트가 여러 덩어리로 흩어져서 큰 요청을 못 받는 것. 지표는 E = 1 − 최대/전체(상태), ext_fail = "전체는 충분한데 실패" 횟수(사건), E(r) = 요청 r 보다 작은 조각의 비율(요청 기준)이다. 손 계산 예: 블록 32,64,32,128,32,32 중 64·128 free → I 79 B, E 1/3, 176 B 요청은 ext_fail. 꼬리: "E 만 보면 안 되는 이유?" → 작은 요청 위주면 작은 조각도 쓸모 있는데 E 는 높게 나온다(T1 E 평균 0.73 인데 실제 문제는 거의 없음). 요청 크기를 같이 봐야 한다.
</details>

<details><summary>Q3. 기기가 몇 주 돌다가 "메모리는 남았는데 malloc 이 NULL" 이 됩니다. 어떻게 접근하나요?</summary>

① 숫자부터: free 합, 최대 free 블록, free 블록 수, 최저 수위(min-ever). free 합 ≥ 요청 > 최대 블록이면 외부 단편화다(RTOS-M10 의 `heap_health`). ② 어디에 쌓였나: 클래스별 보고서(C1)나 glibc `malloc_info`. T4 처럼 "한 크기의 구멍이 수백 개" 인지 본다. ③ 원인 패턴: 오래 사는 작은 객체가 짧게 사는 큰 버퍼 사이에 끼는 것, 펌웨어 업데이트로 바뀐 요청 크기, 늦게(lazy) 할당되는 큰 버퍼. ④ 대책: 큰 버퍼는 부팅 때 미리 잡기, 수명별로 힙/풀 나누기(heap_5 영역 분리, 정적 풀), 요청 크기를 몇 가지로 고정·반올림(C2: T4 U 0.538 → 0.947), 고정 블록 풀(DS-M02). 꼬리: "compaction 은?" → C 에서는 포인터를 옮길 수 없어 불가능하다. 핸들(간접 포인터) 기반 할당기만 가능하다.
</details>

<details><summary>Q4. segregated storage, segregated fit, 슬랩, 버디는 어떻게 다른가요?</summary>

**Simple segregated storage**: 클래스마다 같은 크기 블록만 두고 요청을 클래스 크기로 반올림한다. 쪼개기·병합이 없어서 O(1) 이고 헤더도 필요 없다(페이지 단위로 클래스를 정함). 대신 내부 단편화가 크고, 한 클래스에서 풀린 메모리를 다른 클래스가 못 쓴다. 슬랩(DS-M03)이 이 계열이다. **Segregated fit**(이 레슨): 클래스는 검색 색인일 뿐이고 블록은 쪼개고 병합된다. 내부 단편화가 작다. **버디**(DS-M04): 2의 거듭제곱으로 쪼개고 짝(buddy)끼리만 병합한다. 주소 계산으로 짝을 찾으니 빠르다. 대신 반올림 낭비가 크다(연습 3: T2 요청당 814.5 B vs 우리 23.5 B). TLSF(DS-A06)는 segregated fit 에 2단계 비트맵을 붙여 O(1) 로 만든 것이다. CS:APP §9.9 가 앞의 세 가지를 같은 절에서 비교한다.
</details>

<details><summary>Q5. 그런데 왜 처리량이 T1/T2 에서는 거의 안 늘었죠?</summary>

리스트가 이미 짧았기 때문이다. T1·T2 는 살아 있는 블록이 수백 개라 first-fit 도 평균 3.9~10 노드에서 끝난다. seg 는 대신 삽입·제거·탐색마다 `size_class` 를 계산하고(2배 구간은 루프), 빈 클래스 머리도 확인한다. 실측 seg/first: T1 1.1~1.2×, T2 0.7~0.9×. 리스트가 길어지는 T3(1.7~2.1×, best 대비 1.6~2.9×)와 T4(7.4~7.6×)에서만 확실히 이긴다. 개선: 클래스 계산을 `clz` 한 번으로, 빈 클래스 건너뛰기를 비트맵 + `ctz` 로(연습 1). 꼬리: "그럼 seg 를 왜 쓰나?" → 최악 시간과 활용률. 최악 탐색 156 → 21(T2), 660 → 87(T3). 평균이 같아도 꼬리 지연이 줄어든다. 실시간이면 이게 핵심이다.
</details>

<details><summary>Q6. 병합(coalescing)을 segregated list 에서 할 때 주의할 점, 그리고 "지연 병합" 은?</summary>

이웃이 free 인지는 경계 태그(앞 블록의 푸터, 뒤 블록의 헤더)로 O(1) 에 안다. 하지만 이웃이 들어 있는 리스트는 이웃 **크기의 클래스**다. 그래서 크기를 바꾸기 전에 `list_remove(이웃)` → 합치기 → 합친 크기로 `list_insert` 순서가 필수다(실수 1). 즉시 병합은 free 할 때마다 리스트 연산이 2~3번이고, 같은 크기를 금방 다시 할당하는 패턴에서는 쪼개고 합치기를 반복하는 낭비가 생긴다. 그래서 glibc 는 작은 크기(fastbin, tcache)를 병합하지 않고 그대로 두었다가 나중에 한꺼번에 병합한다고 알고 있다(`malloc_consolidate`, 기억 기준). 대가: 그동안 외부 단편화가 커 보이고, 검사기 불변식 4("인접 free 없음")가 성립하지 않는다.
</details>

<details><summary>Q7. 힙 검사기에는 무엇을 넣고, 언제 부르나요?</summary>

표의 7가지(프롤로그/에필로그, 정렬·크기, 헤더=푸터, 인접 free 없음, 리스트 링크·free 표시·힙 범위·사이클, 클래스 소속, 개수·바이트 일치). 핵심은 **같은 사실을 두 경로로 세서 맞춰 보는 것**이다: 힙 걷기의 free 개수 vs 리스트 노드 수 vs 증분 카운터. 셋 중 하나만 틀려도 어느 쪽 코드가 틀렸는지 좁혀진다. 비싸므로(O(블록 수)) 테스트에서 K 연산마다(여기 64), 실제 펌웨어에서는 디버그 빌드나 유휴 태스크에서 부른다. 테스트는 일부러 깨뜨린 힙(엉뚱한 클래스에 끼우기, 병합 누락, 푸터 오염)에서 검사기가 실제로 울리는지 확인해야 한다. 안 그러면 늘 0 을 돌려주는 검사기도 통과한다.
</details>

<details><summary>Q8. ASan 이 깨끗한데 할당기 버그가 없다고 할 수 있나요?</summary>

아니다. ASan 은 시스템 `malloc` 을 가로채서 블록 사이에 레드존을 두는데, 우리 힙은 `mmap` 영역 하나라 ASan 에게는 64 MiB 짜리 정상 메모리다. 페이로드를 1 바이트 넘어 써도 옆 블록의 헤더를 덮을 뿐 ASan 은 모른다. 그래서 이 레슨은 ① `mm_check` 로 구조를, ② 페이로드를 id 패턴으로 채우고 free 직전에 확인해서 내용을 검사한다(realloc 은 앞뒤 바이트, free 에서 전체). 커스텀 할당기를 ASan 에 알리려면 `ASAN_POISON_MEMORY_REGION` / `ASAN_UNPOISON_MEMORY_REGION` (`<sanitizer/asan_interface.h>`)으로 free 블록과 헤더를 독으로 표시하는 방법이 있다(이 레슨에서는 안 함). 이번 ASan/UBSan 실행이 증명하는 것은 하네스·재생기·정렬·정수 연산에 UB 가 없다는 것까지다.
</details>

## 9. 추가 연습

### 연습 1 — 비어 있지 않은 클래스 비트맵

클래스 17개의 "비어 있지 않음" 을 `uint32_t` 비트맵으로 들고 있다면, 클래스 c 이상에서 첫 비어 있지 않은 클래스를 루프 없이 찾는 식은? 그것으로 `seg_find` 의 "위로 올라가기" 를 바꿔도 같은 블록을 고르나?

<details><summary>답</summary>

`m = mask & (~0u << c); return m ? ctz(m) : -1;`. 위 클래스는 첫 노드가 늘 맞으므로, 자기 클래스만 훑은 뒤 `g_head[next_class(mask, c+1)]` 를 돌려주면 된다. practice.c 는 2^17 개 마스크 × 시작점 18개 전부에서 선형 탐색과 같음을 확인하고, T2 를 연산 1200 에서 멈춘 힙(마스크 0x0ff8f)에서 asize 32..70000 모두 `seg_find` 와 같은 블록을 고르는 것을 확인한다. 비트맵은 리스트가 비거나 처음 찰 때 갱신한다. TLSF(DS-A06)는 이것을 2단계로 해서 클래스 안의 탐색까지 없앤다.
</details>

### 연습 2 — 요청 기준 외부 단편화 E(r)

E(r) = (크기 < r 인 free 블록의 바이트) / (free 바이트 합). 손 힙(free 64, 128)에서 E(32), E(65), E(129)? T4 연산 1500 에서 E, E(464), E(528)?

<details><summary>답</summary>

손 힙: E(32) = 0, E(65) = 64/192 = 1/3, E(129) = 1. T4@1500 실측: `E=0.9980  E(464)=0.000  E(528)=1.000`. E 는 "엄청 쪼개졌다" 고만 하지만, E(r) 은 "448 요청에는 문제없고 512 요청에는 전부 쓸모없다" 를 말한다. 펌웨어 감시 코드에서는 "반드시 성공해야 하는 가장 큰 요청"(OTA 버퍼, DMA 프레임)을 r 로 두고 E(r) 또는 단순히 `largest >= r` 을 본다. practice.c `ext_frag_for`.
</details>

### 연습 3 — 2의 거듭제곱 반올림 vs 우리 분할

모든 블록을 2의 거듭제곱(≥ 요청 + 16, 최소 32)으로 준다면(segregated storage / 버디 식) 요청당 내부 단편화는? 네 트레이스의 모든 alloc/realloc 요청으로 평균을 내라.

<details><summary>답</summary>

실측(B/요청): T1 23.6 → 50.9, T2 23.5 → 814.5, T3 23.4 → 3674.0, T4 16.0 → 213.3. 우리 분할은 늘 16 B 헤더/푸터 + 0~15 B 정렬이라 요청 크기와 무관하게 약 24 B 이고, 2의 거듭제곱은 요청 크기에 비례해서 평균 약 1/3 을 버린다(크기가 고르게 흩어질 때). 그래서 버디(DS-M04)는 페이지처럼 원래 2의 거듭제곱인 것에, 슬랩(DS-M03)은 크기가 정해진 객체에 쓴다. practice.c `pow2_block`.
</details>

### 연습 4 — 푸터 생략 (prev_alloc 비트)

할당 블록은 푸터가 필요 없다(병합할 때 앞 블록의 푸터를 읽는 것은 앞 블록이 **free** 일 때뿐이다). 헤더 bit1 에 "앞 블록이 할당됨" 을 두면 할당 블록 크기는 `max(32, ALIGN16(요청 + 8))`. 요청당 얼마나 줄어드나? 왜 T4 에서는 0 인가?

<details><summary>답</summary>

실측(B/요청): T1 23.6 → 16.5, T2 23.5 → 15.8, T3 23.4 → 15.9, T4 16.0 → 16.0. 줄어드는 양은 요청마다 0 또는 16 B 다(16 B 정렬이므로). T4 의 요청 64, 448, 512 는 16 의 배수라 `ALIGN16(r + 8) = r + 16 = ALIGN16(r + 16)` 이 되어 이득이 없다. 대가: free/병합/realloc 이 다음 블록의 bit1 을 함께 고쳐야 하고, 검사기도 그 비트를 검사해야 한다. CS:APP §9.9 의 경계 태그 설명이 이 최적화를 소개한다(절 번호는 기억 기준). practice.c `elided_block`.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 10/10 passed, exit 0
```

실제 출력 일부:

```text
-- 연습 1: non-empty class bitmap
PASS bad == 0
  T2@1200 non-empty classes mask = 0x0ff8f
PASS diff == 0 && mm_check(0) == 0
-- 연습 2: request-aware external fragmentation E(r)
…
  T4@1500: E=0.9980  E(464)=0.000  E(528)=1.000
-- 연습 3/4: internal fragmentation per request (all alloc/realloc requests)
  T1 small-uniform   n= 1233  ours   23.6 B/req  pow2    50.9 B/req  footer-elided   16.5 B/req
  T2 mixed           n= 1219  ours   23.5 B/req  pow2   814.5 B/req  footer-elided   15.8 B/req
  T3 realloc-grow    n=15851  ours   23.4 B/req  pow2  3674.0 B/req  footer-elided   15.9 B/req
  T4 binary-pattern  n= 1500  ours   16.0 B/req  pow2   213.3 B/req  footer-elided   16.0 B/req
…
10/10 passed
```

practice.c 는 `#define MM_NO_MAIN` 후 `#include "solution.c"` 로 엔진을 가져온다(같은 폴더). ASan+UBSan 빌드에서도 10/10.

## 10. 핵심 정리

- 클래스 = 블록 크기의 색인. 작은 크기는 정확 클래스(머리가 늘 맞음), 큰 크기는 2배 구간. 넣을 때와 찾을 때 **같은 함수, 블록 크기 기준**. 크기를 바꾸기 전에 빼고, 바꾼 뒤에 넣는다.
- 자기 클래스 first-fit + 위 클래스 첫 블록 ≈ best-fit (오차는 한 클래스 폭). 실측 U: T2 0.921(best 0.924, first 0.733), 탐색 평균 2.2 노드(best 22.9).
- 처리량 이득은 리스트가 길 때만 크다(T4 7~8×, T3 1.7~2.1×, T1/T2 ≈ 1×). 진짜 가치는 **최악 탐색**(156 → 21, 660 → 87)과 활용률이다.
- 내부 단편화 = Σ(블록 − 요청), 요청 크기는 재는 쪽이 들고 있다. 외부 단편화 = E(상태) + ext_fail(사건) + E(r)(요청 기준). 끝이 아니라 살아 있을 때 잰다.
- 정책은 배치를 못 바꾼다: T4 는 세 정책 모두 U 0.538, ext_fail 500. C 에서는 compaction 불가 → **요청 쪽**을 고친다.
- 요청 반올림은 외부 → 내부 단편화 거래: T4 0.538 → 0.947, T2 0.921 → 0.755. 펌웨어 규칙 "크기를 몇 가지로 고정" 의 근거.
- 검사기는 같은 사실을 두 경로로 세서 맞추고, 일부러 깨뜨린 힙에서 울리는지 테스트한다. 커스텀 힙 안은 ASan 이 못 본다.

## 참고자료

- R. E. Bryant, D. R. O'Hallaron, *Computer Systems: A Programmer's Perspective*, 3rd ed., §9.9 Dynamic Memory Allocation (memlib, 경계 태그, 명시적 리스트, segregated free lists, malloc lab 트레이스·활용률 정의).
- OSTEP 17장 "Free-Space Management" — §17.3 Basic Strategies (best/first/worst/next fit), §17.4 Other Approaches (segregated lists, slab, buddy), compaction 이 어려운 이유. 시뮬레이터 `ostep-homework/vm-freespace/malloc.py` (`-p`, `-l SIZESORT+`).
- P. R. Wilson, M. S. Johnstone, M. Neely, D. Boles, "Dynamic Storage Allocation: A Survey and Critical Review", IWMM 1995 — 단편화 정의와 segregated fit 분류.
- glibc `malloc/malloc.c` 머리 주석 (bins, tcache, fastbin, `malloc_consolidate`) — https://sourceware.org/git/?p=glibc.git;a=blob;f=malloc/malloc.c — 이 레슨의 bin 설명은 기억 기준.
- Doug Lea, "A Memory Allocator" (dlmalloc 설계 노트) — https://gee.cs.oswego.edu/dl/html/malloc.html
- WG14 N2464 "Zero-size Reallocations are Undefined Behavior" — C23 에서 `realloc(p, 0)` 이 미정의 동작이 된 근거 (기억 기준).
- ASan manual poisoning — https://github.com/google/sanitizers/wiki/AddressSanitizerManualPoisoning
- 같은 시리즈·관련 레슨: OS-M04 (명시적 리스트), OS-M06 (arena + tcache), DS-M02 (고정 블록 풀), DS-M03 (슬랩), DS-M04 (버디), DS-A06 (TLSF), RTOS-M10 (FreeRTOS heap_1~5, `vPortGetHeapStats`), C-A05 (정렬 할당).
