---
id: ALGO-A05
time: 120분
updated: 2026-11-11
---
# ALGO-A05 LZ77 압축: 슬라이딩 윈도 인코더/디코더

## 학습 목표

- LZSS 토큰 형식(플래그 비트 + literal 1 B / match 2 B)을 **창 크기 W 와 최소 일치 길이(min match)** 에서 직접 유도하고(offset 비트 = log₂W, 길이 비트 = 16 − log₂W, 최대 길이 = min + 2^lbits − 1), "왜 보통 3 인가"를 손익분기 계산 + 실측 표로 설명한다.
- 창 전체를 훑는 naive 인코더와 **해시 체인(hash chain)** 인코더를 짜고, 체인을 자르지 않으면 두 출력이 **바이트까지 같다**는 것을 증명·검증한다. 체인 길이 제한이 속도와 압축률을 어떻게 바꾸는지 실제 바이너리로 잰다.
- 디코더의 **겹치는 복사**(dist < len)를 바이트 단위로 해야 하는 이유(memmove 는 틀리고 memcpy 는 UB)를 귀납 불변식으로 증명하고, 손상·절단·악의적 스트림에서 범위 밖을 절대 읽고 쓰지 않는 디코더를 결정적 퍼즈 + ASan 으로 확인한다.
- 디코더 RAM = "출력 버퍼 전체"(평면) 또는 "**W 바이트 링 + 상태 52 B**"(스트리밍, Cortex-M4 실측)라는 것을 말하고, OTA 이미지를 256 B 플래시 페이지로 바로 풀어 쓰는 펌웨어 시나리오를 구현한다.

## 1. 문제

펌웨어 면접에서 압축은 "OTA 이미지를 줄여서 보내고 부트로더가 풀어라", "로그를 플래시에 더 많이 남겨라" 라는 모습으로 나온다. 꼬리질문은 거의 같다: **디코더 RAM 은 얼마?**, 망가진 스트림이 오면?, 겹치는 복사는 왜 memcpy 로 안 되나?, 인코더가 느린데 어떻게 빠르게? — 이 레슨은 그 답을 코드와 실측으로 만든다.

LZ77(Ziv–Lempel 1977)의 아이디어는 한 줄이다: **"방금 본 데이터를 다시 말해야 하면, 그 내용 대신 '몇 바이트 뒤로 가서(dist) 몇 바이트(len) 베껴라' 라고 쓴다."** 지난 W 바이트가 사전(dictionary)이고, 그 창이 데이터를 따라 미끄러진다(sliding window). LZSS(Storer–Szymanski 1982)는 여기에 "토큰마다 1비트 플래그로 literal 과 match 를 구분하고, 손해 보는 짧은 일치는 쓰지 않는다" 를 더한 변형이다. DEFLATE(zip/gzip/PNG)는 LZ77 + 허프만(ALGO-A06), 펌웨어용 경량 라이브러리 다수가 LZSS 계열이다.

이 레슨이 정한 바이트 형식 (모든 .c 파일이 같은 형식):

```text
헤더 6 B : [0] obits (8..12, W = 2^obits)  [1] min_match (2..4)  [2..5] 원본 길이 n (uint32 LE)
본문     : [플래그 1 B][토큰 ×8][플래그 1 B][토큰 ×8] …   플래그 bit k (LSB 먼저) = 토큰 k 의 종류
           0 → literal 1 B           1 → match 2 B:  w = b0 | b1<<8
           dist = (w >> lbits) + 1  (1..W)    len = (w & (2^lbits − 1)) + min_match    lbits = 16 − obits
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a05a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">슬라이딩 윈도: 위치 i 에서 지난 W 바이트 안의 가장 긴 일치를 찾는다</text>
  <rect x="20" y="50" width="80" height="34" class="f-mut" stroke="currentColor"/>
  <text x="60" y="72" fill="currentColor" text-anchor="middle">이미 나감</text>
  <rect x="100" y="50" width="400" height="34" class="f-acc" stroke="currentColor"/>
  <text x="350" y="72" fill="currentColor" text-anchor="middle">창 (search buffer) [i − W, i)</text>
  <rect x="500" y="50" width="150" height="34" class="f-ok" stroke="currentColor"/>
  <text x="575" y="72" fill="currentColor" text-anchor="middle">앞보기 ≤ max_len</text>
  <rect x="130" y="50" width="70" height="34" class="f-warn" stroke="currentColor"/>
  <text x="165" y="100" fill="currentColor" text-anchor="middle" font-family="monospace">j</text>
  <text x="505" y="100" fill="currentColor" font-family="monospace">i</text>
  <path d="M500 112 C 430 155, 230 155, 168 112" stroke="currentColor" fill="none" stroke-width="1.5" marker-end="url(#a05a)"/>
  <text x="230" y="170" fill="currentColor">dist = i − j (1..W), len = 일치 길이 (겹쳐서 i 를 넘어가도 됨)</text>
  <text x="10" y="200" fill="currentColor" font-weight="bold">match 2 바이트 (obits = 12 예: W = 4096, lbits = 4, len = 3..18)</text>
  <rect x="20" y="215" width="420" height="34" class="f-acc" stroke="currentColor"/>
  <text x="230" y="237" fill="currentColor" text-anchor="middle" font-family="monospace">dist − 1 : obits 비트 (상위)</text>
  <rect x="440" y="215" width="160" height="34" class="f-ok" stroke="currentColor"/>
  <text x="520" y="237" fill="currentColor" text-anchor="middle" font-family="monospace">len − min : lbits</text>
  <text x="610" y="237" fill="currentColor">= 16 비트, LE</text>
  <text x="10" y="282" fill="currentColor">플래그 비트 1 + literal 8 = 9 비트 / 플래그 1 + match 16 = 17 비트.</text>
  <text x="10" y="302" fill="currentColor">obits 를 1 늘리면 창은 2배, 최대 길이는 대략 절반 (258 → 130 → 66 → 34 → 18, min 3 기준).</text>
  <text x="10" y="322" fill="currentColor">이 맞바꿈 때문에 "W 가 클수록 좋다" 가 항상 참은 아니다 (5. 풀이 표).</text>
</svg>
```

### Part A — 형식, naive 인코더, 평면 디코더

- `lz_find_naive(p, in, n, i, &dist)` — 창 [i−W, i) 의 모든 시작점을 **가까운 것부터** 보고 가장 긴 일치(같으면 가장 가까운 것)를 고른다. min_match 미만이면 0.
- `lz_encode(p, method, h, in, n, out, cap)` — 탐욕(greedy) 파싱. 토큰 8개마다 플래그 바이트 자리를 먼저 잡고 나중에 비트를 채운다. 출력 ≤ `lz_bound(n) = 6 + n + ⌈n/8⌉`.
- `lz_decode(in, n, out, cap)` — 출력 버퍼가 곧 창. 겹치는 match 는 바이트 단위 복사.
- 테스트: 손 계산 골든 벡터(형식 고정), 빈/1 B/2 B/전부 같음/비압축/주기 1..16/창 경계 크기(W−1, W, W+1, 2W−1, 2W, 2W+3) × 파라미터 15조합, {a,b} 위 길이 ≤ 12 문자열 전부(16382 왕복), 무작위 400개.

### Part B — 해시 체인, 창 경계, 최악 크기, 손상 입력

- `lz_find_hash(h, p, in, n, i, &dist)` — head/prev 해시 체인. `max_chain = 0` 이면 naive 와 **같은 바이트열**(증명 + 모든 왕복 테스트에서 비교), 양수면 후보 수 제한.
- 창 경계: 주기 W 데이터는 dist == W match 로 압축되고, 주기 W+1 은 창 밖이라 literal 뿐 — 둘 다 숫자로 확인.
- 손상 입력: 손으로 만든 오류 스트림 11종이 정해진 오류 코드를, 결정적 퍼즈 30000건에서 평면·스트리밍 디코더가 **같은 결과**를 낸다. 입력·출력은 정확한 크기의 힙 버퍼라 1바이트 넘침도 ASan 이 잡는다.

### Part C — OTA 이미지를 플래시로 스트리밍 해제

- `lzs_push(s, byte)` — 패킷이 몇 바이트씩 오든 한 바이트씩 먹는 상태 기계. 지난 W 바이트는 호출자가 준 링에만 있고, 풀린 바이트는 `put()` 으로 256 B 페이지 버퍼 → "플래시 프로그램" 으로 간다.
- 96 KiB 합성 펌웨어 이미지(벡터 표 + 코드 스니펫 + 문자열 + 0xFF 패딩)를 W = 1 KiB 로 압축 → 1..300 B 무작위 패킷으로 흘려 풀기 → 플래시 내용 == 원본, 페이지 384개, 지워진 곳에만 순서대로 썼는지 검사.
- 디코더 RAM 보고(호스트 sizeof + Cortex-M4 asm 의 실제 sizeof), 창이 링보다 큰 스트림·파티션보다 긴 스트림 거부, 합성 UART 로그의 W 별 압축률. 실제 바이너리(/bin/ls, /bin/zsh)는 `demo_ratio.c`.

## 2. Problem (English)

Implement an LZSS codec with a configurable window `W = 2^obits` (256..4096) and minimum match length `min_match` (2..4), using the byte format above (6-byte header; one flag byte per 8 tokens, LSB first; literal = 1 byte; match = 16-bit little-endian word `(dist-1) << (16-obits) | (len-min_match)`).

```c
long   lz_encode(const lz_params *p, lz_method m, lz_hash_t *h,
                 const uint8_t *in, size_t n, uint8_t *out, size_t cap);   /* bytes written or <0 */
long   lz_decode(const uint8_t *in, size_t n, uint8_t *out, size_t cap);   /* original length or <0 */
size_t lz_find_naive(const lz_params *p, const uint8_t *in, size_t n, size_t i, size_t *dist);
size_t lz_find_hash (const lz_hash_t *h, const lz_params *p, const uint8_t *in, size_t n, size_t i, size_t *dist);
int    lzs_push(lzs_t *s, uint8_t byte);     /* streaming decoder: W-byte ring + O(1) state */
long   lzs_finish(const lzs_t *s);
```

Constraints: no `malloc` inside the codec (hash tables and ring buffers are caller-provided); greedy parsing with "longest match, ties → smallest distance"; the decoder must never read past `in[n)` or write past `out[cap)` for **any** input, and must report truncation, bad header, distance before start of output, length overrun and trailing garbage as distinct errors. The streaming decoder must produce the same result as the flat one for every input, regardless of how the bytes are chunked.

Example: `"abcabcabcabc"`, W = 256, min 3 → `08 03 0C 00 00 00 | 08 | 61 62 63 | 06 02` (12 bytes: three literals, then one match dist 3, len 9 that overlaps its own output).

Follow-ups: why is `memcpy` wrong for the match copy? How much RAM does the decoder need? What is the worst-case output size? How do you make the encoder fast, and what does it cost? What happens with a malicious stream?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 다섯 군데: `lz_find_naive`, `lz_find_hash`, `lz_encode`(헤더 다음 루프), `lz_decode`(헤더 검사 다음 루프), `lzs_push`(헤더 상태 다음). 형식 상수, 헤더 읽기/쓰기, `lz_match_len`, 해시 함수·insert, 스트리밍 디코더의 헤더 처리·`lzs_emit`·`lzs_finish`, 하네스(왕복·퍼즈·플래시 흉내)는 주어진다. 스텁 인코더는 헤더만 쓰고 디코더는 TRUNC 를 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
== Part A: 형식 고정 + 왕복 ==
FAIL starter.c:354  r == 12 && memcmp(g_enc, want, 12) == 0
FAIL starter.c:356  d == 12 && memcmp(g_dec, "abcabcabcabc", 12) == 0
named/period/window-edge: 405 cases, fail bits 56/56/56
…
hand-made malformed streams: 7/11 give the expected error in both decoders
…
fw image 98304 B -> 6 B (0.0%), streamed into 0 flash pages, rc=-3
…
8/37 passed
```

(exit 1. 통과한 8개: memmove 시연, 스트리밍 헤더 단계의 거절(HDR/CAP), `sizeof` 검사, 그리고 두 스텁이 **똑같이** 틀려서 통과하는 비교 검사들(naive == hash, 퍼즈의 두 디코더 일치) — 비교 오라클만으로는 부족하고 골든 벡터·왕복이 함께 있어야 하는 이유.)

## 4. 힌트

<details><summary>힌트 1 — naive 탐색: 가까운 것부터, "엄격히 길 때만" 갱신</summary>

`for (j = i; j-- > lo; )` 로 i−1 부터 내려가면, `L > best` 일 때만 바꾸는 것만으로 "같은 길이면 가까운 쪽" 이 된다. `L == maxlen` 이면 더 볼 필요가 없다. `lz_match_len` 은 j + k 가 i 를 넘어가도 그대로 비교한다 — 그게 겹치는 일치다.

</details>

<details><summary>힌트 2 — 인코더: 플래그 바이트는 "자리 먼저, 비트는 나중"</summary>

`nbit == 8` 이면 토큰을 쓰기 **전에** `flag_at = o; out[o++] = 0; nbit = 0;`. match 면 `out[flag_at] |= 1 << nbit`. 이렇게 하면 마지막 그룹이 8개 미만이어도 남는 플래그 비트는 0 이고, 토큰이 정확히 8의 배수에서 끝나도 빈 플래그 바이트가 붙지 않는다. 쓰기 전마다 `cap` 확인.

</details>

<details><summary>힌트 3 — 디코더: "읽기 전에 남았나", "쓰기 전에 들어가나"</summary>

검사 순서: 플래그를 읽을 바이트가 있나(TRUNC) → match 면 2 바이트가 있나(TRUNC) → `dist ≤ op`(DIST) → `len ≤ total − op`(OVERRUN) → 그다음에야 `out[op] = out[op − dist]` 를 len 번. `total ≤ cap` 은 헤더에서 이미 봤으니 쓰기는 항상 cap 안. 끝나면 `ip == n` 이어야 한다(TRAIL).

</details>

<details><summary>힌트 4 — 해시 체인: 창 밖에 닿으면 멈춘다 (그래야 prev 가 유효)</summary>

`c = head[hash(in+i)]` 에서 시작, `c = prev[c & (W−1)]` 로 따라간다. 조건 `i − c ≤ W` 를 **먼저** 확인해야 한다. prev 는 W 칸 링이라, c 가 창 밖이면 그 칸은 이미 c + W 위치가 덮어썼을 수 있다. 인코더는 match 로 건너뛴 위치 i..i+L−1 도 **전부** insert 해야 naive 와 같아진다.

</details>

<details><summary>힌트 5 — 스트리밍: 평면 디코더의 각 "읽기" 가 상태 하나</summary>

평면 디코더에서 입력을 읽는 곳은 세 군데(플래그, literal 또는 match 첫 바이트, match 둘째 바이트) → 상태 S_FLAG, S_TOK, S_M2. 출력 위치 `op` 대신 `produced`, `out[op − dist]` 대신 `ring[(produced − dist) & mask]`. 토큰 하나가 끝날 때마다 `flags >>= 1; nbit++` 하고 다음 상태를 DONE / FLAG / TOK 중에서 고른다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | 압축은 어디서(호스트? MCU?), 해제는 어디서? 디코더 RAM 예산은? 스트림으로 오나(UART/BLE/OTA) 통째로 있나? 입력은 신뢰할 수 있나(서명 전/후)? 원본 크기 상한은? |
| 2. 가장 단순한 것 + 복잡도 | 형식부터 정한다(토큰·비트 폭). naive: 위치마다 창 W 개 후보 × 최대 max_len 비교 → 최악 O(n·W·max_len), 추가 메모리 0 |
| 3. 더 나은 생각 + 불변식 | 해시 체인: 앞 min 바이트가 같은 위치만 후보 → 같은 결과(증명), 체인 제한으로 바이트당 최악 시간 상한. 디코더 불변식: "out[0..op) = 원본 앞 op 바이트" |
| 4. 코드 | 인코더(플래그 자리 먼저) → 디코더(검사 순서) → 스트리밍 디코더 |
| 5. 소리 내어 말할 테스트 | 빈 입력, 1 바이트, 전부 같은 값(dist 1 겹침), 비압축(최악 크기), 주기 W / W+1(창 경계), 절단·비트 뒤집기 퍼즈, naive == hash 바이트 비교 |
| 6. 복잡도 | 디코더 O(n + c) 시간, RAM = 출력 버퍼 또는 W + 상태. 인코더: 해시 표 + W 칸 prev, 시간은 체인 길이에 비례 |

### Part A — 형식 유도와 "min match = 3" 의 손익분기

비트 비용: literal = 플래그 1 + 8 = **9 비트**, match = 플래그 1 + 16 = **17 비트**. 길이 L 인 일치를 literal L 개 대신 쓰면 `9L − 17` 비트를 아낀다.

| L | literal 로 | match 로 | 이득 |
|---|---|---|---|
| 1 | 9 | 17 | −8 (손해) |
| 2 | 18 | 17 | **+1** (거의 본전) |
| 3 | 27 | 17 | +10 |
| 4 | 36 | 17 | +19 |

그래서 이 형식의 손익분기는 L = 2 다. 그런데도 보통 3 을 쓰는 이유는 세 가지다.

1. **기회비용**: 탐욕 파서가 1비트 아끼는 2-바이트 match 를 잡으면, 바로 다음 위치에서 시작하는 더 긴 match 를 놓칠 수 있다 (연습 1·2 의 lazy/최적 파싱이 이 문제).
2. **길이 범위**: 길이 필드는 `len − min` 을 담는다. min 이 1 크면 최대 길이도 1 커진다 (obits 12 에서 17 → 18).
3. **해시**: 해시 체인은 앞 min 바이트로 후보를 찾는다. 2 바이트 해시는 키가 65536 종류뿐이라 체인이 길어진다.

실측(`demo_ratio.c`, 압축 후 크기 / 원본, 전부 탐색 = naive 와 같은 출력):

| 데이터 | W | min 2 | min 3 | min 4 |
|---|---|---|---|---|
| /bin/ls arm64e (88672 B, 0 바이트 73.8%) | 1024 | 25.1% | **24.9%** | 26.7% |
| /bin/zsh arm64e (685568 B, 0 바이트 24.9%) | 4096 | 59.0% | **57.6%** | 63.5% |
| 합성 UART 로그 (131072 B) | 4096 | 23.6% | **23.1%** | 23.4% |
| 합성 UART 로그 | 256 | 40.4% | 40.4% | **39.9%** |
| 무작위 바이트 10000 B (solution.c) | 4096 | 111.9% | 112.5% | — |

바이너리에서는 min 3 이 가장 좋고 min 4 는 2~6%p 손해다. 무작위 데이터에서는 2-바이트 일치가 우연히 483번 나와 min 2 가 60 B 작다 — 손익분기가 2 라는 계산 그대로다. 로그의 W = 256 에서만 min 4 가 근소하게 이겼다. 결론: "3 은 법칙이 아니라 이 비용 구조와 탐욕 파서에서 실측이 고르는 값" 이고, DEFLATE 도 최소 길이 3 (길이 3..258, RFC 1951) 이다. 이 형식의 obits = 8 이 최대 길이 258 이 되는 것은 우연이다 (8비트 길이 + 기준값 3).

### Part A — 인코더 추적

`"abracadabra abracadabra"` (23 B), W = 256, min 3, 실제 출력 (`lz_find_naive` 를 따라가며 찍은 것):

| i | 토큰 | 이유 | 바이트 |
|---|---|---|---|
| 0..6 | literal a b r a c a d | 창 안에 3 바이트 이상 일치 없음 (i=3 의 "ac…" 는 "ab" 와 1 바이트만) | 61 62 72 61 63 61 64 |
| 7 | match dist 7, len 4 "abra" | j = 0 에서 "abra" 4 바이트, 그다음 'c' ≠ ' ' | w = 6<<8 \| 1 → 01 06 |
| 11 | literal ' ' | 공백은 처음 | 20 |
| 12 | match dist 12, len 11 "abracadabra" | j = 0 에서 끝까지 | w = 11<<8 \| 8 → 08 0B |

출력 20 B: `08 03 17 00 00 00 | 80 | 61 62 72 61 63 61 64 01 06 | 02 | 20 08 0B`. 첫 플래그 0x80 = 토큰 7(8번째)만 match, 둘째 플래그 0x02 = 그룹의 토큰 1 이 match. 해시 인코더 출력도 같았다(`same=1`).

### Part A — 디코더와 겹치는 복사: 왜 바이트 단위인가

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">match(dist 3, len 9) 를 "abc" 뒤에: 겹치는 복사 = 방금 쓴 바이트를 다시 읽는다</text>
  <g font-family="monospace" text-anchor="middle" fill="currentColor">
    <text x="20" y="62" text-anchor="start">바이트 루프</text>
    <text x="20" y="152" text-anchor="start">memmove</text>
  </g>
  <g stroke="currentColor">
    <rect x="140" y="42" width="30" height="30" class="f-ok"/><rect x="170" y="42" width="30" height="30" class="f-ok"/><rect x="200" y="42" width="30" height="30" class="f-ok"/>
    <rect x="230" y="42" width="30" height="30" class="f-acc"/><rect x="260" y="42" width="30" height="30" class="f-acc"/><rect x="290" y="42" width="30" height="30" class="f-acc"/>
    <rect x="320" y="42" width="30" height="30" class="f-acc"/><rect x="350" y="42" width="30" height="30" class="f-acc"/><rect x="380" y="42" width="30" height="30" class="f-acc"/>
    <rect x="410" y="42" width="30" height="30" class="f-acc"/><rect x="440" y="42" width="30" height="30" class="f-acc"/><rect x="470" y="42" width="30" height="30" class="f-acc"/>
    <rect x="140" y="132" width="30" height="30" class="f-ok"/><rect x="170" y="132" width="30" height="30" class="f-ok"/><rect x="200" y="132" width="30" height="30" class="f-ok"/>
    <rect x="230" y="132" width="30" height="30" class="f-acc"/><rect x="260" y="132" width="30" height="30" class="f-acc"/><rect x="290" y="132" width="30" height="30" class="f-acc"/>
    <rect x="320" y="132" width="30" height="30" class="f-warn"/><rect x="350" y="132" width="30" height="30" class="f-warn"/><rect x="380" y="132" width="30" height="30" class="f-warn"/>
    <rect x="410" y="132" width="30" height="30" class="f-warn"/><rect x="440" y="132" width="30" height="30" class="f-warn"/><rect x="470" y="132" width="30" height="30" class="f-warn"/>
  </g>
  <g font-family="monospace" text-anchor="middle" fill="currentColor">
    <text x="155" y="62">a</text><text x="185" y="62">b</text><text x="215" y="62">c</text><text x="245" y="62">a</text><text x="275" y="62">b</text><text x="305" y="62">c</text>
    <text x="335" y="62">a</text><text x="365" y="62">b</text><text x="395" y="62">c</text><text x="425" y="62">a</text><text x="455" y="62">b</text><text x="485" y="62">c</text>
    <text x="155" y="152">a</text><text x="185" y="152">b</text><text x="215" y="152">c</text><text x="245" y="152">a</text><text x="275" y="152">b</text><text x="305" y="152">c</text>
    <text x="335" y="152">0</text><text x="365" y="152">0</text><text x="395" y="152">0</text><text x="425" y="152">0</text><text x="455" y="152">0</text><text x="485" y="152">0</text>
  </g>
  <path d="M155 76 C 190 100, 300 100, 335 76" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <path d="M245 76 C 280 100, 390 100, 425 76" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="512" y="55" fill="currentColor">out[op] = out[op − 3]</text>
  <text x="512" y="73" fill="currentColor">op=6 은 op=3 에서 쓴 'a' 를 읽음</text>
  <text x="512" y="145" fill="currentColor">복사 전 out[0..9) 스냅샷을 옮김:</text>
  <text x="512" y="163" fill="currentColor">그때 out[3..9) 는 아직 0</text>
  <text x="20" y="205" fill="currentColor">memcpy: 겹치면 정의되지 않은 동작(C11 7.24.2.1). 이 맥에선 memmove 와 같은 결과였지만 보장 없음,</text>
  <text x="20" y="225" fill="currentColor">ASan 은 memcpy-param-overlap 으로 즉시 중단 (demo_overlap.c 실제 출력).</text>
  <text x="20" y="250" fill="currentColor">dist = 1, len = L 은 "직전 바이트를 L 번 반복" = 런 길이 부호화(RLE)가 공짜로 들어 있다.</text>
</svg>
```

**불변식**: 토큰 k 개를 처리한 뒤 `out[0..op)` = 원본의 앞 op 바이트.
- literal: 원본 바이트 그대로 → 유지.
- match (dist d, len L) 는 인코더가 `in[i+t] == in[i−d+t]` (t = 0..L−1, 겹침 허용)를 확인하고 낸 것이다. 디코더가 t 번째 바이트를 쓸 때 읽는 `out[op+t−d]` 의 인덱스는 `op+t−d < op+t` 이므로, t 에 대한 귀납으로 이미 원본과 같게 써진 칸이다. 그래서 `out[op+t] = in[i−d+t] = in[i+t]`.

memmove 는 정의상 "**복사 시작 전** 원본 영역" 을 옮긴다. t ≥ d 인 바이트에서 읽어야 할 칸은 아직 안 써진 칸이라 스냅샷에는 옛 값(0)이 있다. 이 맥의 실제 출력 (`demo_overlap.c`, "abc" + match(3, 33)):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_overlap.c -o demo_overlap && ./demo_overlap
cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer demo_overlap.c -o demo_overlap_asan && ./demo_overlap_asan
```

```text
byte loop  "abcabcabcabcabcabcabcabcabcabcabcabc"
memmove    "abcabc.............................."
memcpy     "abcabc.............................."
```

ASan 빌드에서는 memcpy 줄에서 `ERROR: AddressSanitizer: memcpy-param-overlap: memory ranges [...] overlap` 를 찍고 중단했다 (exit 134). `-O2`·`-O0` 일반 빌드는 둘 다 memmove 와 같은 결과였다 — **"잘 돌아가는 것처럼 보이는 UB"** 의 전형이다. dist ≥ len 일 때만 memcpy 로 빠르게 가는 최적화는 정당하다 (겹침 없음).

**디코더 RAM**: 평면 디코더는 `out` 전체가 창이다. 추가 상태는 지역 변수 몇 개. 그래서 "RAM 에 이미지 전체를 풀 수 있는가" 가 곧 RAM 요구량이다. 그게 안 되면 Part C 의 링을 쓴다.

### Part B — 해시 체인이 naive 와 같은 이유

`head[h]` = 해시가 h 인 가장 최근 위치, `prev[pos mod W]` = 같은 해시의 바로 앞 위치. 위치 i 에서 체인은 같은 해시의 위치들을 **가까운 것부터 엄격히 감소하는 순서로** 준다.

1. **후보 집합**: 길이 ≥ min 인 일치가 j 에 있으면 `in[j..j+min) == in[i..i+min)` → 해시가 같다 → j 는 체인에 있다 (j + min ≤ n 이므로 insert 됐다). 해시 충돌로 섞인 위치는 길이 < min 이라 결과에 영향이 없다.
2. **순서**: 체인 순서 = naive 의 j = i−1, i−2, … 순서의 부분열. 둘 다 "엄격히 길 때만 갱신" 이므로 같은 (길이 최대, 그중 가장 가까운) 후보를 고른다. `L == maxlen` 에서 멈추는 위치도 같다.
3. **prev 가 유효한가**: `prev[c mod W]` 를 덮어쓰는 것은 위치 c + W 를 insert 할 때뿐이다. 우리는 `i − c ≤ W`, 즉 c + W ≥ i 일 때만 prev 를 읽고, insert 된 위치는 모두 < i 다 → 아직 안 덮였다. 그래서 창 검사를 prev 읽기보다 **먼저** 해야 한다.
4. **멈춤**: prev 값은 항상 현재보다 작은 위치이거나 −1 → 체인은 유한.

그러므로 `max_chain = 0` 이면 출력이 **바이트까지 같다**. `roundtrip()` 이 모든 테스트 입력(이름 붙은 405건, 전수 16382건, 무작위 400건, Part B·C 입력)에서 naive 출력과 `memcmp` 로 비교한다 — 전부 같았다. 체인을 자르면(`max_chain = 4`) 출력은 달라지지만 같은 디코더로 풀린다(RT_SHORT 비트).

**복잡도**: naive 는 탐색 한 번에 최악 O(W·max_len). 해시 체인(무제한)도 최악은 같다 (모든 위치가 같은 해시이면서 일치가 짧은 데이터). `max_chain = K` 면 탐색당 O(K·max_len) — **바이트당 최악 시간에 상한**이 생긴다는 것이 MCU 로거에서 진짜 이유다. 메모리: head 4096 + prev 4096 칸 × int32 = 32 KiB (호스트용 선택. MCU 라면 Q6).

실측 (`demo_ratio.c`, 중앙값 5회, 압축률은 그 설정의 결과):

| 데이터, W = 4096, min 3 | naive | 체인 전부 | 체인 16 | 체인 4 |
|---|---|---|---|---|
| /bin/zsh 685568 B | 983.84 ms, 57.6% | 15.53 ms, 57.6% | 8.68 ms, 57.8% | 5.43 ms, 58.6% |
| /bin/ls 88672 B | 40.33 ms, 28.3% | 2.15 ms, 28.3% | 0.32 ms, 28.4% | 0.32 ms, 28.7% |
| UART 로그 131072 B | 57.75 ms, 23.1% | 2.29 ms, 23.1% | 1.55 ms, 23.7% | 1.13 ms, 25.7% |

naive 대비 해시(무제한)가 19~63배 빠르고 압축률은 같다. 체인 16 은 0.1~0.6%p 손해로 1.5~7배 더 빠르다. 로그처럼 같은 접두사("[0001" 등)가 아주 많은 데이터에서는 체인이 길어서 체인 4 의 손해(+2.6%p)가 크다.

### Part B — 창 경계, 최악 크기

`solution.c` 실제 출력:

```text
W= 256: period W   ->   297 B (1 matches, 1 with dist==W)
        period W+1 ->   585 B (0 matches, max dist 0), bound 585
W=4096: period W   ->  5099 B (228 matches, 228 with dist==W)
        period W+1 ->  9225 B (0 matches, max dist 0), bound 9225
random 10000 B: min3 -> 11253 B, min2 -> 11193 B (483 two-byte matches), bound 11256
```

- 무작위 블록 R(W 바이트)을 두 번(RR): 두 번째 R 은 전부 dist **정확히 W** 인 match. W = 256 에서는 max_len 258 ≥ 256 이라 match **한 개**(297 = 6 + 256 + 32 플래그 + 플래그 1 + 2). W = 4096 에서는 max_len 18 이라 ⌈4096/18⌉ = 228 개. dist 필드가 `dist − 1` 을 담기 때문에 W 자체가 표현된다 — `dist` 를 그대로 담으면 W 는 못 쓰고 창이 W − 1 이 된다.
- 주기 W+1: 같은 내용이 창 바로 밖에 있어서 일치 0 → 정확히 최악 크기.
- **최악 크기 증명**: 토큰 t 개, 각 토큰은 원본을 ≥ 1 B 덮고 본문 바이트 수 ≤ 덮은 원본 바이트 수 (literal 1:1, match 2 B 로 ≥ min ≥ 2 B). 플래그는 ⌈t/8⌉ ≤ ⌈n/8⌉. 그래서 출력 ≤ 6 + n + ⌈n/8⌉ = `lz_bound(n)`. 무작위 64 B 에서 출력이 정확히 bound 와 같고, cap = bound − 1 이면 `LZ_E_CAP` 인 것도 검사한다. 이 값으로 OTA 송신 버퍼를 컴파일 시간에 잡는다.

### Part B — 손상 입력: 무엇을 어디서 막나

디코더가 믿지 않는 것 네 가지와 검사 위치:

| 위협 | 검사 | 오류 |
|---|---|---|
| 헤더 필드가 범위 밖 (obits 7/13, min 1/5) | `lz_params_ok` | HDR |
| 원본 길이가 버퍼보다 큼 | `total > cap` (스트리밍: `> limit`) | CAP |
| 입력이 중간에 끊김 (플래그·literal·match 2번째 바이트) | 읽기 전마다 `ip < n` | TRUNC |
| 출력 시작보다 앞을 가리키는 dist | `dist > op` | DIST |
| 선언 길이를 넘는 len | `len > total − op` | OVERRUN |
| 끝난 뒤 남은 바이트 | `ip != n` | TRAIL |

손으로 만든 11종 (`dist > produced` = 첫 토큰이 match 등) 은 평면·스트리밍 둘 다 정해진 코드를 냈다. 퍼즈는 정상 스트림 60개를 기준으로 비트 1~4개 뒤집기 / 임의 위치 절단 / 본문 1/8 바이트 무작위화 / 길이 필드 ±32 변조를 30000번 (실제 출력):

```text
fuzz: 30000 mutated streams, decoders agree on 30000; results ok=2858 trunc=12170 hdr=107 cap=157 dist=8652 overrun=4885 trail=1171
      accepted but output != original: 2449  (LZ 는 무결성 검사가 없다 -> CRC 필요)
```

- 두 디코더가 30000건 모두 같은 코드·같은 출력. ASan/UBSan 빌드에서 보고 0 (입력·출력이 정확한 크기 힙 버퍼라 1 바이트 넘침도 잡힌다).
- **2449건은 "성공" 했는데 원본과 다르다.** 형식이 맞으면 디코더는 틀린 데이터도 그럴듯하게 풀어 낸다. 그래서 OTA 는 풀린 이미지 전체에 CRC-32(ALGO-M02) 또는 서명 검증(SYSD-M02)을 하고 나서야 부트 슬롯을 바꾼다. 디코더 안전성(범위 밖 접근 없음)과 데이터 무결성은 별개의 문제다.
- `dist > op` 검사를 빼면 어떻게 되나: `-O2` 빌드는 조용히 통과(34/37, 퍼즈에서만 불일치), ASan 빌드는 `global-buffer-overflow ... in lz_decode` 로 멈췄다 (7. 실수 4).

### Part C — 스트리밍 디코더: RAM = W + 상태

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a05c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">OTA: 압축 스트림 → lzs_push (바이트 하나씩) → 페이지 버퍼 → 플래시</text>
  <rect x="10" y="50" width="120" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="70" y="72" fill="currentColor" text-anchor="middle">UART/BLE 패킷</text>
  <text x="70" y="90" fill="currentColor" text-anchor="middle">1..300 B</text>
  <rect x="170" y="40" width="200" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="270" y="62" fill="currentColor" text-anchor="middle">lzs_t 상태 52 B (M4)</text>
  <text x="270" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">HDR→FLAG→TOK⇄M2→DONE</text>
  <text x="270" y="98" fill="currentColor" text-anchor="middle">+ 오류 시 ERR (sticky)</text>
  <rect x="170" y="140" width="200" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="270" y="165" fill="currentColor" text-anchor="middle">링 W B = 지난 W 바이트</text>
  <rect x="410" y="50" width="130" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="475" y="72" fill="currentColor" text-anchor="middle">페이지 버퍼</text>
  <text x="475" y="90" fill="currentColor" text-anchor="middle">256 B</text>
  <rect x="580" y="40" width="150" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="655" y="62" fill="currentColor" text-anchor="middle">플래시 (지운 곳에만,</text>
  <text x="655" y="80" fill="currentColor" text-anchor="middle">주소 순서대로</text>
  <text x="655" y="98" fill="currentColor" text-anchor="middle">페이지 프로그램)</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M130 75 L168 75" marker-end="url(#a05c)"/>
    <path d="M370 75 L408 75" marker-end="url(#a05c)"/>
    <path d="M540 75 L578 75" marker-end="url(#a05c)"/>
    <path d="M240 110 L240 138" marker-end="url(#a05c)"/>
    <path d="M300 138 L300 112" marker-end="url(#a05c)"/>
  </g>
  <text x="310" y="128" fill="currentColor">ring[(produced − dist) &amp; (W−1)]</text>
  <text x="10" y="210" fill="currentColor">RAM (Cortex-M4, W = 1 KiB): 52 + 1024 + 256 = 1332 B. 출력 전체(96 KiB)는 RAM 에 한 번도 없다.</text>
  <text x="10" y="232" fill="currentColor">평면 디코더라면 96 KiB 버퍼가 필요하다 — 이것이 "디코더 RAM = W" 의 뜻.</text>
</svg>
```

평면 디코더에서 입력을 읽는 곳마다 상태 하나: S_FLAG(플래그 바이트), S_TOK(literal 이면 바로 내보내고, match 면 첫 바이트를 b0 에 저장), S_M2(match 완성 → 검사 → 링에서 len 바이트 복사). 링 인덱스는 2의 거듭제곱 마스크(DS-B04). dist = W 일 때 읽는 칸 `(produced − W) & (W−1)` 은 이번에 쓸 칸과 같은데, `lzs_emit(s, s->ring[...])` 는 인자를 먼저 평가(읽기)하고 나서 쓰므로 맞다. 주기 W 테스트가 이 경계를 실제로 지나간다 (228개 match 가 전부 dist == W).

실제 출력 (`solution.c` Part C):

```text
fw image 98304 B -> 22883 B (23.3%), streamed into 384 flash pages, rc=98304
decoder RAM: sizeof(lzs_t)=64 + ring W=1024 + page buffer 256 = 1344 B (host, 64-bit pointers)
log 65536 B, min 3:  W=256 40.1%  W=512 33.0%  W=1024 28.3%  W=2048 24.6%  W=4096 23.3%
```

호스트의 64 B 는 포인터 3개가 8 B 라서다. Cortex-M4 로 컴파일한 `demo_m4.c` 의 `const unsigned lzs_sizeof = sizeof(lzs_t);` 는 asm 에 `.long 52` 로 나왔다 → **M4 RAM = 52 + W + 페이지 버퍼**. 디코더 코드 크기(`llvm-size demo_m4.o` 의 text 열): `-Os` 410 B, `-O2` 454 B. 스택은 재귀 없음, 복사 함수 프레임은 레지스터 4개 push(16 B) 였다.

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -          # asm (lzs_sizeof, lzs_copy)
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -c demo_m4.c -o demo_m4.o && llvm-size demo_m4.o
```

M4 의 match 복사 루프 (`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c`, 실제 출력 중 루프):

```text
.LBB0_2:
	ldr	r2, [r6, #28]      @ s->mask
	subs	r1, r0, r5        @ produced - dist
	ldr	r3, [r6]           @ s->ring
	ands	r1, r2
	ldrb	r1, [r3, r1]
	ands	r0, r2
	strb	r1, [r3, r0]
	ldr	r2, [r6, #12]      @ s->put
	ldr	r0, [r6, #16]      @ s->ctx
	subs	r4, #1
	blx	r2                  @ put(ctx, byte)
	ldr	r0, [r6, #24]      @ s->produced 다시 읽기
	cmp	r4, #0
	add.w	r0, r0, #1
	str	r0, [r6, #24]
	bne	.LBB0_2
```

(주석 `@ s->mask` 등은 구조체 오프셋을 보고 내가 붙인 것.) 바이트마다 `blx` 로 콜백을 부르고, 콜백이 `*s` 를 바꿀 수 있다고 봐야 해서 mask·ring·produced 를 **매번 메모리에서 다시 읽는다**. 호스트 실측에서도 스트리밍 push(394~638 MB/s)가 평면(702~1285 MB/s)의 약 절반이었다. 개선: 싱크를 "연속 구간 (ptr, len)" 단위로 넘기기, match 를 링의 연속 구간 단위로 쪼개 복사하기 — 연습 아이디어로 남긴다. 펌웨어 관점에서는 이미 플래시 쓰기(페이지당 수백 µs~ms, 칩마다 다름)가 병목이라 이 차이는 대개 안 보인다.

**왜 이 구조가 OTA 에 맞나**: 압축은 호스트에서 한 번(느려도 됨), 해제는 장치에서(빠르고 RAM 작게). LZ 계열은 디코더가 인코더보다 훨씬 단순·빠른 **비대칭** 코덱이다. 헤더의 원본 길이는 파티션 크기(`limit`)와 비교해 첫 6 바이트에서 거절하고, 창이 장치의 링보다 크면(이 장치는 1 KiB 인데 스트림은 W = 4096) 역시 헤더에서 `LZ_E_HDR` 로 거절한다 — 테스트 두 개가 이걸 확인한다. 부트로더 쪽 흐름(받기 → 풀어 쓰기 → CRC/서명 → 슬롯 전환)은 SYSD-M02, BM-A05, PLAT-M07, 페이지 프로그램 제약은 COMM-A06·MEM-B12, 링크 프레이밍(SLIP/COBS)은 COMM-B11/B12.

### 실제 데이터: 압축률 표와 zlib 비교 (`demo_ratio.c`)

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 demo_ratio.c -o demo_ratio -lz
lipo -thin arm64e /bin/ls  -output "$TMPDIR/ls_arm64e"
lipo -thin arm64e /bin/zsh -output "$TMPDIR/zsh_arm64e"
./demo_ratio "$TMPDIR/ls_arm64e" "$TMPDIR/zsh_arm64e"
```

Apple M6, Apple clang 21.0.0 (clang-2100.0.123.102), zlib 1.2.12. 측정 직전·직후 `uptime` load averages **48.44 62.22 40.99 → 49.17 61.89 41.12** (다른 작업 10개가 같이 돌고 있었다 — 절대 ms 보다 **비율**을 믿을 것). 압축 후 크기 / 원본 (min 3, 실제 출력):

| W (max_len) | /bin/ls arm64e | /bin/zsh arm64e | UART 로그 |
|---|---|---|---|
| 256 (258) | 26.2% | 74.0% | 40.4% |
| 512 (130) | **24.9%** | 68.6% | 33.0% |
| 1024 (66) | **24.9%** | 63.9% | 28.0% |
| 2048 (34) | 25.8% | 60.2% | 24.4% |
| 4096 (18) | 28.3% | **57.6%** | **23.1%** |
| zlib level 1 / 5 / 9 | 19.1 / 18.2 / 18.2% | 49.6 / 46.8 / 46.5% | 23.7 / 18.7 / 17.2% |

읽을 것:

- **W 를 키우는 게 항상 이득은 아니다.** /bin/ls 는 74% 가 0 (Mach-O 세그먼트 정렬 패딩 57.7 KB 가 4 덩어리) 이라 긴 0 런을 적은 토큰으로 덮는 게 중요하다. W = 4096 이면 match 하나가 최대 18 B 라 0 런 1 KB 에 토큰 57개, W = 512 면 130 B 씩 8개. 그래서 512~1024 가 최선. 반대로 코드가 대부분인 zsh 와 로그는 먼 과거를 볼수록 좋아서 4096 이 최선. 16비트 고정 match 에서 "창 비트 ↔ 길이 비트" 를 맞바꾸기 때문이다. 펌웨어 이미지의 0xFF 패딩도 같은 효과.
- **zlib 이 6~11%p 더 좋다.** DEFLATE 는 창 32 KiB + 길이 258 을 동시에 갖고(가변 길이 부호라 가능), 허프만으로 literal·길이·거리를 엔트로피 부호화한다(ALGO-A06). 대신 디코더가 32 KiB 창 + 허프만 표가 필요하다 — 1 KiB 링으로 도는 이 디코더와는 RAM 등급이 다르다.
- 디코더 속도(W = 1024 스트림): 평면 702~1285 MB/s, 스트리밍 394~638 MB/s (출력 기준).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 37/37 passed (0.5 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 37/37, 보고 없음 (약 2~4 초, 부하에 따라)
```

마지막 실제 출력 줄들은 5. 풀이에 붙였다. 테스트가 퍼즈 30000건까지 포함해도 0.5 초라 sanitizer 용 축소는 필요 없었다.

## 7. 자주 하는 실수

아래 1~6 은 solution.c 의 **사본**에 한 줄씩 심어서 실제로 돌려 본 결과다 (사본은 레슨 폴더 밖에서 만들고 지웠다).

**1. 겹치는 match 를 memmove/memcpy 로 복사한다.**

```c
memmove(out + op, out + op - dist, len); op += len;     /* 틀림: dist < len 이면 아직 안 쓴 칸을 읽는다 */
```

골든 벡터, 이름 붙은 경계 사례, 전수, 무작위가 줄줄이 FAIL → 27/37. 주기 W 테스트는 통과했다(dist = W ≥ len 이라 겹침이 없다) — 겹침 테스트(전부 같은 값, 주기 1..16)가 꼭 필요한 이유. memcpy 는 UB (위 demo). → 바이트 루프, 또는 `dist ≥ len` 일 때만 memcpy.

**2. 해시 체인의 창 검사를 `<` 로 쓴다 (`i − c < W`).** dist = W 후보를 버린다. 디코드는 멀쩡히 되므로 왕복 테스트만으로는 못 잡고, **naive 와 바이트 비교**(RT_SAME)와 주기 W 테스트가 잡았다 (30/37). 그리고 창 검사는 prev 를 읽기 **전에** 해야 한다(5. 풀이의 3번) — 창 밖 위치의 prev 칸은 이미 다른 위치가 덮어썼을 수 있다.

**3. naive 창을 한 칸 짧게 (`lo = i − W + 1`).** 주기 W 데이터에서 dist == W match 가 0 개가 되어 297 → 582 B (W = 256), 5099 → 9222 B (W = 4096) — 28/37. 압축기 버그는 "풀리긴 하는데 덜 줄어드는" 모양이 많아서 **압축률을 숫자로 검사하는 테스트**가 있어야 잡힌다.

**4. 디코더에서 `dist > op` 검사를 뺀다.** 정상 스트림은 다 통과한다. `-O2` 빌드: 34/37 — 손상 스트림 11종 중 일부와 퍼즈의 두 디코더 일치(21348/30000)만 FAIL, 크래시 없음. 즉 **조용히 출력 버퍼 앞을 읽는다.** ASan 빌드: `runtime error: addition of unsigned offset ... overflowed` 다음 `AddressSanitizer: global-buffer-overflow ... in lz_decode` 로 중단. 펌웨어에서 이 줄 하나가 없으면 OTA 스트림으로 RAM 을 읽어 내는 구멍이 된다.

**5. 해시 모드에서 match 로 건너뛴 위치를 insert 하지 않는다.** 디코드는 되지만 압축률이 떨어지고 naive 와 달라진다 — 26/37 (전수·무작위의 RT_SAME). zlib 은 빠른 레벨에서 일부러 긴 match 안의 위치를 덜 insert 한다고 알고 있다(속도 ↔ 압축률, 세부는 확인 권장). 일부러 그렇게 하려면 동등성 테스트 대신 "디코드 + 압축률 허용치" 테스트로 바꿔야 한다.

**6. 스트리밍 디코더의 OVERRUN 검사를 한 칸 느슨하게 (`len > total − produced + 1`).** 정상 데이터에서는 절대 안 드러나고 퍼즈의 "두 디코더 일치" 하나만 FAIL (29556/30000, 36/37). 같은 형식의 디코더를 두 개(평면·스트리밍) 두고 서로 대조하는 것이 싸고 강한 오라클이다.

**7. 원본 길이를 헤더에 안 둔다.** 그러면 디코더는 "입력이 끝날 때까지" 풀어야 하고, 절단된 스트림을 정상으로 받아들이며(마지막 그룹이 짧은 것과 구분 불가), 출력 크기를 미리 검사할 수 없다(압축 폭탄). 길이 필드 하나로 CAP·OVERRUN·TRUNC 가 다 가능해진다.

## 8. 면접 Q&A

<details><summary>Q1. LZ77, LZSS, LZ78/LZW 는 뭐가 다른가요?</summary>

LZ77(1977)은 사전이 "지난 W 바이트" 그 자체이고 토큰이 (거리, 길이, 다음 문자) 묶음이다. LZSS(1982)는 플래그 비트로 literal 과 (거리, 길이) 를 나눠서, 일치가 없을 때 낭비를 줄이고 손해 보는 짧은 일치는 안 쓴다 — 이 레슨의 형식. LZ78/LZW 는 지나간 데이터에서 **구(phrase) 표**를 따로 만들고 표 번호를 보낸다. 디코더도 같은 표를 다시 만들어야 해서 RAM 이 표 크기만큼 든다(GIF 의 LZW 는 12비트 코드 = 4096 항목). 펌웨어 관점의 차이: LZ77 계열 디코더 RAM = 창(또는 출력 버퍼 자체), 코드가 아주 작다. 꼬리: "DEFLATE 는?" → LZ77 토큰을 허프만으로 다시 부호화(RFC 1951, 창 32 KiB, 길이 3..258) — ALGO-A06 의 허프만이 그 뒷단이다.

</details>

<details><summary>Q2. match 복사를 memcpy 로 하면 안 되는 이유를 정확히 말해 보세요.</summary>

dist < len 이면 출처 [op−d, op−d+L) 와 목적지 [op, op+L) 가 겹친다. LZ 의 의미는 "앞에서부터 한 바이트씩, 방금 쓴 것도 출처가 된다" 이고, 불변식 증명(5. 풀이)이 바로 그 순서를 쓴다. memmove 는 "복사 전 스냅샷" 의미라 결과가 다르고(실측 "abcabc000…"), memcpy 는 겹치면 UB(C11 7.24.2.1)라 구현 마음대로다 — 이 맥에선 우연히 memmove 와 같았고 ASan 은 memcpy-param-overlap 으로 멈췄다. dist = 1 은 "직전 바이트 L 번 반복" = RLE. 빠르게 하려면: dist ≥ len 이면 memcpy, dist < len 이면 바이트 루프 또는 "d 바이트 패턴을 두 배씩 늘려 가며 복사" (dist 가 작으면 word 단위 패턴 복제) 같은 기법을 쓴다 — 고속 LZ 디코더들이 쓰는 방식(세부는 구현마다 다름, 확인 권장).

</details>

<details><summary>Q3. 디코더 RAM 은 정확히 얼마인가요? 더 줄일 수 있나요?</summary>

평면: 출력 버퍼 전체(이미지 크기) — 출력이 곧 창. 스트리밍: 지난 W 바이트 링 + 상태(M4 에서 52 B 실측) + 싱크 버퍼(페이지 256 B) → W = 1 KiB 면 1332 B. 더 줄이는 법: (1) W 를 작게 — 압축률 대가는 표로(로그 W = 256 이면 40% vs 4096 이면 23%). (2) **출력이 메모리 맵 플래시면 링이 필요 없다**: 이미 쓴 바이트는 내부 플래시(XIP 영역)에서 바로 읽으면 된다. 단, 아직 페이지 버퍼에만 있는 최근 바이트는 버퍼에서 읽어야 하고, 외부 SPI NOR 처럼 읽기가 느린 플래시는 match 마다 읽기 명령이 들어 비싸다. (3) 반대로 링을 W 보다 작게는 못 한다 — dist = W 인 토큰이 오면 풀 수 없다. 그래서 헤더의 W 를 장치의 링과 비교해 거절하는 검사가 있다.

</details>

<details><summary>Q4. 최소 일치 길이는 왜 3 인가요? 2 나 4 면?</summary>

이 형식은 literal 9 비트, match 17 비트 → 손익분기 L = 2 (1 비트 이득), L = 3 이면 10 비트 이득. 2 를 허용하면 1 비트 이득 때문에 다음 위치의 더 긴 match 를 놓치는 일이 생기고(탐욕 파서), 2 바이트 해시는 체인이 길어진다. 실측: 바이너리에서 min 3 이 최선(zsh W = 4096: 59.0 / 57.6 / 63.5%), 무작위 데이터는 min 2 가 0.5% 작았다(2-바이트 일치 483개) — 계산과 일치. min 4 는 3 바이트 반복(예: Thumb 명령의 공통 바이트열, 짧은 단어)을 버려서 손해. 꼬리: "match 가 3 바이트면?" → 형식을 바꿔야 한다. 길이 필드를 가변으로(짧은 match 는 짧은 토큰) 만들면 손익분기도 바뀐다 — DEFLATE 가 허프만으로 이걸 자동으로 한다.

</details>

<details><summary>Q5. 악의적인 스트림이 와도 안전한가요? 어떻게 확인했나요?</summary>

디코더가 믿지 않는 값: 헤더 필드, 원본 길이, 매 토큰의 dist·len, 입력 길이. 각각 읽기 전·쓰기 전에 검사한다(5. 풀이 표). 검증: (1) 오류 종류별 손 계산 스트림 11개 (2) 결정적 퍼즈 30000건 — 비트 뒤집기·절단·무작위화·길이 변조 (3) 입력·출력을 **정확한 크기의 힙 버퍼**에 넣어 ASan 이 1 바이트 넘침도 잡게 함 (4) 평면·스트리밍 두 구현의 결과 대조. 하지만 "안전" ≠ "올바름": 퍼즈에서 2449건이 성공했는데 데이터가 틀렸다. 그래서 OTA 는 (a) 서명/CRC 를 압축 해제 **후** 이미지에 대해 확인하고 (b) 확인 전에는 부트 슬롯을 바꾸지 않는다(SYSD-M02, PLAT-M07). 압축 폭탄: 압축 c 바이트가 낼 수 있는 최대 출력 ≤ max_len·8(c−6)/17 (연습 4 증명) — 헤더 길이가 이 상한이나 파티션보다 크면 처음 6 바이트에서 거절한다.

</details>

<details><summary>Q6. 인코더를 MCU 에서 돌려야 한다면 (로그 압축) 무엇을 바꾸겠어요?</summary>

(1) 메모리: 이 레슨의 해시 표는 int32 × (4096 + 4096) = 32 KiB — 호스트용. MCU 는 W = 1024, 해시 2^10 칸, 위치를 uint16(블록 안 상대 위치)으로 → head 2 KiB + prev 2 KiB + 창/앞보기 버퍼 ~1.1 KiB. 블록(예: 4 KiB 플래시 섹터)마다 표를 리셋하면 각 블록이 독립적으로 풀린다 — 로그의 일부가 망가져도 다음 블록부터 복구 가능. (2) 시간: `max_chain` 으로 탐색당 비교 수 상한 → 바이트당 최악 시간이 정해진다(실측: 체인 16 은 압축률 0.1~0.6%p 손해). ISR 에서 하지 말고 로거 태스크에서. (3) 압축률이 더 필요하면 lazy matching(연습 1: 로그에서 6.3% 작아짐). 압축 후 크기는 최악 n + ⌈n/8⌉ + 6 이라 출력 버퍼를 그만큼 잡거나, 블록이 안 줄면 원본을 "저장(stored)" 블록으로 내보내는 플래그를 둔다.

</details>

<details><summary>Q7. 탐욕 파싱이 최적인가요?</summary>

아니다. 연습 2 의 DP(토큰 비용이 고정이면 최적 파싱 = 위치 0 → n 의 최단 경로)와 비교하면: 로그 32 KiB 에서 탐욕 74246 비트, lazy 69580, 최적 69173 (탐욕보다 6.8% 작음). 작은 반례 "abcXbcdefYabcdefZ": 탐욕은 두 번째 "abc"(3) 를 잡은 뒤 "def"(3) 로 두 토큰 34 비트, lazy 는 'a' 를 literal 로 미루고 "bcdef"(5) 한 토큰 → 9 + 17 = 26 비트. lazy 가 항상 낫지도 않다: 알파벳 4 개 무작위 데이터에서는 lazy 가 오히려 컸다(114955 vs 113902 비트). zlib 은 lazy 평가를 레벨에 따라 켠다고 알고 있다(확인 권장). 최적 파싱의 DP 증명: 모든 파싱은 위치 0 에서 n 으로 가는 경로이고 토큰 비용은 위치에만 의존 → 부분 구조 최적. Lmax 의 접두사가 모두 유효한 match 라 L = min..Lmax 만 보면 된다.

</details>

<details><summary>Q8. 압축기를 어떻게 테스트하나요? "왕복 테스트" 만으로 충분한가요?</summary>

왕복은 필요조건일 뿐이다. 7. 실수에서 왕복을 통과하는 버그가 셋(2·3·5: 압축률·동등성만 깨짐), 정상 입력에선 안 보이는 버그가 둘(4·6: 손상 입력에서만)이었다. 그래서 층을 쌓았다: (1) **골든 벡터** — 형식을 바이트 단위로 고정 (2) 이름 붙은 경계(빈, 1 B, 전부 같음, 비압축, 주기 k, 창 경계 크기) × 파라미터 전 조합 (3) 작은 입력 **전수**(길이 ≤ 12 의 {a,b} 문자열 전부) (4) **두 탐색기의 바이트 비교** (naive = 해시) (5) 압축률 숫자 검사(주기 W 는 dist = W match, 최악 = bound) (6) 두 디코더 대조 퍼즈 + ASan. 마지막으로 버그를 일부러 심어 이 테스트들이 잡는지 확인(mutation testing) — 위 6개 변종 모두 FAIL 이 났다.

</details>

<details><summary>Q9. 실무에서 LZ4, zlib, heatshrink 같은 걸 쓰면 되지 않나요? 어떻게 고르죠?</summary>

대부분 그게 맞다 — 이 레슨은 그 내부와 판단 기준을 아는 게 목적. 기준: 디코더 RAM(창 크기), 디코더 코드 크기, 압축률, 해제 속도, 스트리밍 가능 여부, 라이선스·인증 이력. zlib/DEFLATE 는 압축률이 좋지만(실측 6~11%p 더 작음) 창 32 KiB + 허프만 표가 든다. LZ4 는 바이트 정렬 토큰이라 해제가 매우 빠르고 블록 단위 형식이다. heatshrink 는 임베디드용 LZSS 로 창·앞보기 비트를 설정해 수백 바이트 RAM 에서도 돈다고 소개돼 있다. (세 라이브러리의 정확한 RAM·형식 수치는 이 레슨에서 확인하지 않음 — 확인 권장.) 면접 답의 뼈대: "디코더 RAM 예산이 W 를 정하고, W 가 압축률을 정한다. 서명은 해제 후 이미지에, 형식에는 길이 필드를."

</details>

## 9. 추가 연습

### 연습 1 — lazy matching

위치 i 에서 일치 L 을 찾았을 때, i+1 에서 더 긴 일치 L2 > L 이 있으면 지금은 literal 을 내고 한 칸 미룬다. 탐욕과 크기를 비교하라 (로그, 알파벳 4 개 무작위, 완전 무작위, W = 1024, min 3).

<details><summary>답 보기</summary>

```c
if (L && i + 1 < n) {
    size_t d2, L2 = find(p, in, n, i + 1, &d2);
    if (L2 > L) { tw_lit(&t, in[i++]); continue; }   /* 미룬다 */
}
```

실제 출력:

```text
log 32K    W=1024 min3: greedy   9287 B (74246 bit)  lazy   8704 B (69580 bit)  optimal   8653 B (69173 bit)
alpha4 32K W=1024 min3: greedy  14244 B (113902 bit)  lazy  14376 B (114955 bit)  optimal  13840 B (110669 bit)
random 32K W=1024 min3: greedy  36868 B (294892 bit)  lazy  36868 B (294892 bit)  optimal  36868 B (294892 bit)
```

로그에서 lazy 가 6.3% 작다. 알파벳 4 개 무작위에서는 오히려 0.9% 컸다 — 미뤄서 얻은 긴 match 가 literal 1 개 비용(9 비트)을 못 갚는 경우가 많다. 디코더는 그대로다(형식이 같으니까). 비용: 위치마다 탐색이 최대 두 번.

</details>

### 연습 2 — 최적 파싱 DP

토큰 비용이 고정(literal 9, match 17 비트)일 때 비트 수를 최소화하는 파싱을 구하라. 작은 입력에서 모든 파싱을 다 해 보는 전수 탐색과 같음을, 무작위 입력에서 탐욕·lazy 이하임을 확인하라.

<details><summary>답 보기</summary>

cost[n] = 0, cost[i] = min(9 + cost[i+1], 17 + min_{L=min..Lmax(i)} cost[i+L]). Lmax(i) 를 주는 거리 d 에서 더 짧은 L 도 모두 일치(접두사)라 L 마다 거리를 다시 찾지 않는다. 뒤에서부터 채우고 앞에서부터 선택을 따라가며 같은 토큰 쓰개로 스트림을 만든다 → 본문 디코더로 그대로 풀린다. O(n·(W·max_len + max_len)).

```text
optimal DP vs brute force over all parses: 4094 strings, 0 mismatches
"abcXbcdefYabcdefZ": greedy 133 bit, lazy 125 bit, optimal 125 bit
```

무작위 300개에서 최적 ≤ 탐욕, 최적 ≤ lazy, 왕복 모두 OK. 주의: 비트 최소 ≠ 바이트 최소가 정확히 같지는 않다(플래그 바이트는 토큰 8개 단위 올림) — 차이는 최대 1 바이트. 실제 고압축 인코더들은 가변 비용(허프만 길이)까지 넣은 근사 최적 파싱을 쓴다(세부 확인 권장).

</details>

### 연습 3 — 제자리(in-place) 압축 해제 여유

RAM 이 이미지 크기만큼밖에 없다. 버퍼 크기 n + m 의 **끝**에 압축본(c B)을 두고 앞에서부터 풀어도 아직 안 읽은 입력을 덮지 않는 최소 m 을 구하고, m* 에서는 성공, m* − 1 에서는 덮어쓰기가 감지됨을 보여라.

<details><summary>답 보기</summary>

토큰 k 까지 읽은 입력 in_k (헤더·플래그 포함), 쓴 출력 out_k. 토큰 k 의 마지막 쓰기 위치 out_k − 1 이 안 읽은 첫 칸 start + in_k 보다 앞이어야 하므로 out_k ≤ start + in_k, start = n + m − c → **m* = max(0, max_k(out_k − in_k)) + c − n**. 디코더에 "쓰기 전 `op + len > start + ip` 면 중단" 감시를 넣어 확인. 실제 출력:

```text
log          n=16384 c= 4762  n-c= 11622  margin m*=   0  (m*: ok, m*-1: buffer < stream)
zeros|random n=16384 c= 9488  n-c=  6896  margin m*=1023  (m*: ok, m*-1: overwrite detected)
random       n=16384 c=18438  n-c= -2054  margin m*=2054  (m*: ok, m*-1: buffer < stream)
random in-place: 200 streams, m* always ok; m*-1 -> overwrite detected 181, buffer < stream 19
```

고르게 압축되는 로그는 여유 0 (출력이 입력을 끝에서야 따라잡는다). **앞은 잘 줄고 뒤는 안 주는** 데이터(0 패딩 뒤 난수 = 암호화된 섹션이 뒤에 있는 이미지)는 1023 B 가 필요했다. 비압축 데이터는 c − n. 그래서 제자리 해제를 하는 부트로더/커널은 이 여유를 압축 시점에 계산해 헤더에 넣는다(리눅스 커널 자체 압축 해제에도 비슷한 여유 계산이 있다고 알고 있음 — 확인 권장).

</details>

### 연습 4 — 압축 폭탄 상한

압축 스트림 c 바이트가 풀려서 나올 수 있는 최대 원본 길이의 상한을 증명하고, 최대에 가깝게 부푸는 스트림을 만들어 확인하라.

<details><summary>답 보기</summary>

literal t_l 개, match t_m 개, 플래그 f 바이트: c − 6 = f + t_l + 2t_m, 8f ≥ t_l + t_m. 그래서 8(c−6) ≥ 9t_l + 17t_m. 출력 = t_l + Σlen ≤ t_l + max_len·t_m ≤ (max_len/17)(9t_l + 17t_m) (max_len ≥ 3 이면 1 ≤ 9·max_len/17) → **out ≤ max_len · 8(c − 6)/17**. obits = 8, min 3 (max_len 258) 이면 약 121배. 만든 스트림: literal 'Z' 1 개 + (dist 1, len 258) match 63 개:

```text
bomb: 141 B -> 16255 B (x115.3), bound 16390.6 B
```

정상 스트림 300 개 모두 상한 이하, 출력 ≤ lz_bound(n) 도 확인. 실무 의미: 헤더 길이를 이 상한과 파티션 크기로 먼저 거르고, 길이 필드가 없는 형식이라면 출력 쪽에 하드 리밋을 둬야 한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 9/9 passed (0.6 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 9/9, 보고 없음
```

## 10. 핵심 정리

- LZSS 토큰: 플래그 1 비트 + literal 8 / match 16 비트 (dist−1 : log₂W 비트, len−min : 16−log₂W 비트). 창을 2배로 하면 최대 길이가 반으로 — W 는 데이터에 맞춰 고른다(ls 는 512~1024, zsh·로그는 4096).
- 손익분기: 9L > 17 → L ≥ 2, 실무 min = 3 (기회비용·길이 범위·해시). 최악 출력 = 6 + n + ⌈n/8⌉, 압축 폭탄 상한 = max_len·8(c−6)/17.
- 겹치는 복사(dist < len)는 앞에서부터 바이트 단위: 불변식 "out[0..op) = 원본 앞부분" 의 귀납이 그 순서를 쓴다. memmove 는 틀리고 memcpy 는 UB. dist = 1 = RLE.
- 해시 체인(head + prev[pos mod W]): 창 검사를 prev 읽기보다 먼저, 건너뛴 위치도 전부 insert → 무제한이면 naive 와 바이트까지 같다. max_chain 은 바이트당 최악 시간 상한(체인 16: 압축률 손해 ≤ 0.6%p).
- 디코더 RAM: 평면 = 출력 버퍼 자체, 스트리밍 = W 링 + 상태 52 B (M4) + 페이지 버퍼. 코드 410 B (-Os).
- 손상 입력: TRUNC/HDR/CAP/DIST/OVERRUN/TRAIL 을 읽기·쓰기 전에 검사, 두 디코더 대조 퍼즈 + 정확한 크기 버퍼 + ASan. 디코더 안전 ≠ 데이터 무결성 → 해제 후 CRC/서명.
- 압축기 테스트 = 골든 벡터 + 경계 + 전수 + 두 구현 바이트 비교 + 압축률 숫자 + 퍼즈 + 심은 버그로 테스트 검증.

## 참고자료

- J. Ziv, A. Lempel, "A Universal Algorithm for Sequential Data Compression", *IEEE Trans. Information Theory* 23(3), 1977 — LZ77.
- J. A. Storer, T. G. Szymanski, "Data Compression via Textual Substitution", *Journal of the ACM* 29(4), 1982 — LZSS. 서지는 기억에 의존 — 확인 권장.
- RFC 1951, *DEFLATE Compressed Data Format Specification version 1.3* (P. Deutsch, 1996) — 길이 3..258, 거리 1..32768, LZ77 + 허프만: <https://www.rfc-editor.org/rfc/rfc1951>
- zlib `doc/algorithm.txt` — deflate 의 해시 체인, 체인 길이 제한, lazy 평가 설명: <https://github.com/madler/zlib/blob/develop/doc/algorithm.txt> (이 레슨에서 본문을 다시 확인하지는 않음 — 확인 권장)
- H. Okumura, LZSS.C (1989) — 고전적인 LZSS 구현, N = 4096 / F = 18 / THRESHOLD = 2 로 기억(이 레슨의 obits 12, min 3, max_len 18 과 같은 조합) — 확인 권장.
- C11 표준 (N1570) 7.24.2.1 memcpy ("겹치는 객체 사이의 복사는 정의되지 않음"), 7.24.2.2 memmove.
- heatshrink (임베디드용 LZSS 라이브러리), LZ4 블록 형식 — Q9 의 비교는 일반론, 수치는 각 프로젝트 문서로 확인 권장.
- 관련 레슨: COMM-B11 (SLIP), COMM-B12 (COBS) — 압축 스트림을 링크에 싣는 프레이밍 · ALGO-M02 (CRC-32, 해제 후 무결성) · ALGO-A06 (허프만·varint — DEFLATE 의 뒷단) · ALGO-B10 (슬라이딩 윈도 패턴) · DS-B04 (2의 거듭제곱 링 마스크) · DS-B09/DS-B10 (해시 함수·체이닝) · COMM-A06 (SPI NOR 페이지 프로그램) · MEM-B12 (NAND 페이지/블록 제약) · SYSD-M02 (OTA 시스템), BM-A05 (시리얼 부트로더), PLAT-M07 (A/B 슬롯)
