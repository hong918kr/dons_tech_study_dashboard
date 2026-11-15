---
id: ALGO-A04
time: 110분
updated: 2026-11-11
---
# ALGO-A04 스트리밍 패턴 매칭: KMP와 Rabin-Karp

## 학습 목표

- KMP 실패 함수(failure function = prefix function π)를 "경계의 경계도 경계" 보조정리로 증명하고, 바이트를 하나씩 넣는 `kmp_push(byte)` 를 **가변 상태 1바이트(q = 맞은 길이)** 로 짠다. 겹치는 매칭 전부, DMA 덩어리 경계에 걸친 매칭이 한 번에 넣은 결과와 같음을 확인한다.
- 상각(amortized) O(1)/바이트를 **퍼텐셜 논증**(Φ = q)으로 증명하고, 바이트 하나의 최악은 m−1번 후퇴라는 것을 실측한다(p = a^255 → 254번). 그 최악이 문제일 때 고르는 것: KMP DFA ((m+1)×256 표, 바이트당 표 1번) vs 강한 실패 함수 vs 시프트 레지스터.
- Rabin–Karp 롤링 해시는 스트리밍에서 **m 바이트 링이 필요**하다는 것, mod 2^32 와 mod 소수(2^31−1)의 차이, "해시 일치 → 반드시 원문 비교" 를 직접 만든 충돌 두 개(Thue–Morse: 모든 홀수 base 에서 충돌, 생일 공격: mod 소수에서 충돌)로 보인다.
- 잡음 섞인 UART 에서 `55 55 7E` 프레임 동기기를 짜고, **가짜 프리앰블이 진짜 프레임을 삼키는** 문제를 재생(replay)으로 해결한 뒤 오프라인 참조 파서와 바이트 단위·덩어리 단위로 대조한다. 재생 비용을 바이트당 ≤ 13번으로 증명한다.
- 면접에서 "왜 KMP? 3바이트면 시프트 레지스터 아닌가?" "ISR 에서 돌려도 되나?" 같은 판단 질문에 실측 숫자로 답한다.

## 1. 문제

펌웨어 코딩 면접에서 문자열 매칭은 `strstr` 이 아니라 **"끝이 없는 바이트 스트림에서, 버퍼링 없이, 경계에 걸쳐도 놓치지 않고"** 라는 조건으로 나온다 — 이 조건 하나로 KMP 의 상태 정의, 상각 분석, 최악 지연, RAM/플래시 트레이드오프가 한꺼번에 검사된다.

무선 모뎀·GNSS·모터 드라이버·디버그 링크는 프레임 앞에 **동기 패턴**(sync word, 프리앰블 preamble)을 붙인다. 이 레슨의 프레임은 `55 55 7E | len | payload | ck` 다 (0x55 = `01010101` 교대 비트라 수신기 클록 맞추기에 흔히 쓰고, 0x7E 는 HDLC 플래그와 같은 값 — 여기서는 스터핑 없이 쓴다). 실제 선로에서는:

- UART RX 인터럽트는 바이트를 **하나씩**, DMA 는 반쪽/전체 전송 콜백마다 **아무 데서나 끊긴 덩어리**를 준다. 프리앰블이 덩어리 경계에 걸친다.
- 잡음과 유휴 패턴에 0x55 가 많다 → `55 55 55 7E` 처럼 프리앰블의 앞부분이 겹쳐 온다. 불일치하면 처음부터 다시 보는 코드는 이걸 놓친다.
- 잡음 속 우연한 `55 55 7E` 가 len 바이트를 읽고 뒤의 **진짜 프레임을 몸통으로 삼킨다.**

ALGO-B10 의 링 버퍼(고정 창)와 ALGO-M12 의 바이트 단위 FSM(동기 문자가 `'$'` 한 글자라 부분 일치가 없음)을 이어서, 이번엔 **여러 바이트짜리** 동기 패턴의 부분 일치를 다룬다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">덩어리 3개로 들어온 스트림, 프리앰블 55 55 7E — 상태는 q 하나만 넘어간다</text>
  <g stroke="currentColor">
    <rect x="40" y="50" width="150" height="40" rx="4" fill="none" stroke-dasharray="5 3"/>
    <rect x="200" y="50" width="310" height="40" rx="4" fill="none" stroke-dasharray="5 3"/>
    <rect x="520" y="50" width="150" height="40" rx="4" fill="none" stroke-dasharray="5 3"/>
  </g>
  <text x="45" y="45" fill="currentColor">DMA 덩어리 1</text>
  <text x="205" y="45" fill="currentColor">덩어리 2</text>
  <text x="525" y="45" fill="currentColor">덩어리 3</text>
  <g stroke="currentColor">
    <rect x="50" y="58" width="60" height="26" class="f-mut"/><rect x="120" y="58" width="60" height="26" class="f-mut"/>
    <rect x="210" y="58" width="60" height="26" class="f-acc"/><rect x="285" y="58" width="60" height="26" class="f-acc"/>
    <rect x="360" y="58" width="60" height="26" class="f-acc"/><rect x="440" y="58" width="60" height="26" class="f-ok"/>
    <rect x="530" y="58" width="60" height="26" class="f-ok"/><rect x="600" y="58" width="60" height="26" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="80" y="76">55</text><text x="150" y="76">55</text><text x="240" y="76">55</text><text x="315" y="76">55</text>
    <text x="390" y="76">7E</text><text x="470" y="76">55</text><text x="560" y="76">55</text><text x="630" y="76">7E</text>
    <text x="80" y="112">q=1</text><text x="150" y="112">q=2</text><text x="240" y="112">q=2</text><text x="315" y="112">q=2</text>
    <text x="390" y="112">3→0</text><text x="470" y="112">q=1</text><text x="560" y="112">q=2</text><text x="630" y="112">3→0</text>
  </g>
  <text x="10" y="112" fill="currentColor">q</text>
  <text x="10" y="76" fill="currentColor">pos</text>
  <g fill="currentColor" text-anchor="middle" font-size="12"><text x="80" y="98">0</text><text x="150" y="98">1</text><text x="240" y="98">2</text><text x="315" y="98">3</text><text x="390" y="98">4</text><text x="470" y="98">5</text><text x="560" y="98">6</text><text x="630" y="98">7</text></g>
  <path d="M240 120 Q240 150 270 150 L285 150" stroke="currentColor" fill="none"/>
  <text x="200" y="168" fill="currentColor">pos 2,3: 7E 를 기대했는데 55 → q=fail[1]=1 로 물러났다가 55 일치 → 다시 2</text>
  <text x="200" y="188" fill="currentColor">(q=0 으로 리셋하면 pos 2~4 의 프리앰블을 놓친다)</text>
  <text x="10" y="215" fill="currentColor" font-weight="bold">보고: 끝 위치 4 (시작 2), 끝 위치 7 (시작 5) — 둘 다 덩어리 경계에 걸쳐 있다</text>
  <text x="10" y="238" fill="currentColor">덩어리마다 memmem/strstr 을 돌리거나 콜백마다 q=0 으로 리셋하면 둘 다 놓친다</text>
</svg>
```

### Part A — KMP 스트리밍 검출기

- `kmp_build(p, m, fail)` — `fail[i]` = `p[0..i]` 의 가장 긴 진 경계(proper border: 접두사이면서 접미사, 자기 자신 제외). O(m).
- `kmp_push(k, byte)` — 이 바이트에서 패턴이 끝나면 true. 겹치는 매칭도 전부. 통계로 후퇴 횟수(총합·바이트당 최대)를 센다.
- 테스트: 손 계산 4개, 길이 ≤ 12 이진 문자열 전부 + 무작위 2000개를 정의(brute border)와 대조. 이진 패턴 ≤ 5 × 이진 텍스트 ≤ 10 전수, 무작위 1200건(알파벳 2/3/4/256, m 1–16 + 가끔 17–216)을 naive 와 대조 — **한 번에** 넣기와 **1–37 바이트 무작위 덩어리**로 넣기 둘 다. 총 후퇴 ≤ n 을 매 건 확인, p = a^m 의 바이트당 최악 = m−1 확인.

### Part B — DFA 와 Rabin–Karp, 그리고 충돌

- `dfa_build(p, m, fail, dfa)` — `(m+1)×256` 바이트 표. 행 m = "방금 매칭" (겹침용). 정의(δ(q,c) = p[0..q)+c 의 접미사인 가장 긴 접두사)와 칸 단위로 대조.
- `rk_init / rk_push` — 마지막 m 바이트 링 + 롤링 해시. `modp=false`: mod 2^32 (uint32 wrap), `modp=true`: mod P = 2^31−1 (나눗셈 없는 접기). 해시가 같으면 `verify` 로 링과 패턴 비교. 창이 다 차기 전에는 보고하지 않는다.
- 충돌 시연: (1) 길이 128 Thue–Morse 문자열과 그 보수 — 홀수 base 5개 모두 mod 2^32 해시가 같다(증명 포함) (2) mod (2^31−1), m = 8 에서 2^18 개 문자열 생일 공격. 둘 다 verify 가 거르고, verify 를 끄면 가짜 매칭이 나온다.

### Part C — 잡음 섞인 UART 의 프레임 동기

- `sync_push(s, byte, out, maxout)` — 헌팅(KMP) ↔ 수집. len 범위 밖이거나 ck 가 틀리면 **프리앰블 뒤 바이트를 처리 대기열 앞에 되돌려** 다시 헌팅한다.
- 테스트: 손 사례 5개(깨끗한 프레임, `55 55 55 7E`, 가짜 프리앰블이 진짜를 삼키려는 경우, len 0/33, 페이로드 속 프리앰블), 무작위 스트림 120개(약 100만 바이트, 프레임 2.3만 개, 1/3 은 앞에 가짜 프리앰블)를 오프라인 참조 파서와 **바이트 단위 / DMA 덩어리 단위** 둘 다 대조, 처리량 ≤ 13 × 바이트 수, FIFO 넘침 0, 가짜 프리앰블 8겹 최악 사례.

## 2. Problem (English)

Implement a streaming matcher for a fixed byte pattern `p` (1 ≤ m ≤ 255) over an unbounded byte stream. Bytes arrive one at a time from a UART ISR or in arbitrary DMA chunks; you may not buffer the stream.

```c
void kmp_build(const uint8_t *p, int m, uint8_t *fail);         /* prefix function, O(m) */
bool kmp_push(kmp_t *k, uint8_t b);      /* true iff an occurrence of p ends at this byte (report overlaps) */
void dfa_build(const uint8_t *p, int m, const uint8_t *fail, uint8_t *dfa);   /* (m+1) x 256 table */
void rk_init(rk_t *r, const uint8_t *p, int m, bool modp, uint32_t base, bool verify);
bool rk_push(rk_t *r, uint8_t b);        /* rolling hash over an m-byte ring; verify on hash hit */
int  sync_push(sync_t *s, uint8_t b, frame_t *out, int maxout);   /* 55 55 7E | len | payload | ck */
```

Constraints: O(1) amortized time per byte, O(m) state for KMP (only `q` changes), no malloc. Matches must be identical whether the stream is fed as one buffer or in random chunks. For the framer: `len ∈ [1, 32]`, `ck = ~(len + Σpayload) mod 256`; a corrupt frame must not hide a valid frame that starts inside it.

Example: `p = 55 55 7E`, stream `55 55 | 55 55 7E 55 | 55 7E` (three chunks) → matches end at positions 4 and 7. `p = "aa"`, stream `"aaaa"` → ends 1, 2, 3.

Follow-ups: worst-case work for a single byte? Which variant would you put in an ISR? Why must Rabin–Karp verify, and what goes wrong with mod 2^32?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 군데: `kmp_build`, `kmp_push`, `dfa_build`, `rk_init` 의 해시 준비, `rk_push`, `sync_push`. 나머지(naive 검색, 정의대로 만든 brute border/δ, 링 비교, FIFO, 오프라인 참조 파서, 스트림 생성기)는 주어진다. 스텁은 "매칭 없음 / 표 0" 을 돌려줘서 멈추거나 죽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:310  memcmp(f, e1, 7) == 0
...
prefix function: 10190 patterns vs brute border, 10091 wrong
exhaustive binary: 126914 (pattern,text) pairs, mismatches kmp=41966 dfa=41966 rk32=41966 rkp=41966
...
uart streams: 120, 997521 bytes | frames inserted=23180 recovered=23156 ... | stream!=ref: byte=120 chunked=120 | work 0.000 bytes/byte ...
9/40 passed
```

(exit 1. 통과한 9개는 스텁과 무관하거나 "매칭 없음" 이 정답인 검사 — `mod_p` 접기, 주어진 `rk_hash` 로 본 Thue–Morse 충돌, 참조 파서의 회수율, 무작위 바이트에서 해시 일치 0 등.)

## 4. 힌트

<details><summary>힌트 1 — 상태 q 의 정확한 의미</summary>

q = "지금까지 본 바이트의 접미사이면서 패턴의 접두사인 것 중 가장 긴 것의 길이 (단 < m)". 새 바이트 b 가 오면 후보는 "q 의 경계들 + b" 뿐이다. p[q] == b 면 q+1. 아니면 q 보다 짧은 다음 후보 = q 의 가장 긴 진 경계 = `fail[q-1]`.

</details>

<details><summary>힌트 2 — 실패 함수는 패턴을 자기 자신에 매칭하는 것</summary>

`kmp_build` 는 `kmp_push` 와 같은 루프다. k = `p[0..i)` 의 가장 긴 진 경계. `p[i] != p[k]` 이면 `k = fail[k-1]` 로 물러나고, 같으면 k++. `fail[i] = k`. i = 1 부터 (fail[0] = 0).

</details>

<details><summary>힌트 3 — 매칭 직후 상태와 겹침</summary>

q 가 m 이 되면 보고하고 `q = fail[m-1]` 로 둔다. "aa" in "aaaa" 에서 q=0 으로 두면 끝 1, 3 만 나온다 (2 를 놓침). 프레임 동기에서는 반대로 **유효 프레임 뒤에는 q = 0** — 프레임끼리는 겹치지 않으니까.

</details>

<details><summary>힌트 4 — DFA 의 행 하나는 다른 행의 복사</summary>

상태 q 에서 p[q] 가 아닌 바이트 c 를 받으면 KMP 는 fail[q-1] 로 물러나서 c 를 다시 본다. 즉 `δ(q, c) = δ(fail[q-1], c)` (c ≠ p[q]). fail[q-1] < q 이므로 행을 0, 1, 2, … 순서로 만들면 복사할 행이 항상 이미 있다. 행 m 은 `δ(fail[m-1], ·)` 그대로.

</details>

<details><summary>힌트 5 — 롤링 해시와 "창이 찼나"</summary>

h(s_i..s_{i+m-1}) = Σ s·B^(m-1-k). 다음 창: `h = (h − out·B^(m−1))·B + in`. 링을 0 으로 시작하면 처음 m 바이트 동안 out = 0 이라 같은 식이 그대로 통한다. 대신 `seen < m` 이면 보고 금지 (패턴이 `00 00 7E` 이면 첫 바이트 7E 에서 0 으로 채운 창이 일치해 버린다). mod P 에서 뺄셈은 `h + P − x` 로.

</details>

<details><summary>힌트 6 — 깨진 프레임의 바이트는 어디로?</summary>

깨진 프레임의 len·payload·ck 바이트는 버리면 안 된다 — 그 안에 진짜 프리앰블이 있을 수 있다. 작은 FIFO 의 **앞에** 되돌려 넣고, 같은 루프가 다시 헌팅하게 한다. 재귀 금지. KMP 상태는 그대로(이미 `fail[m-1]`).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 질문으로 범위 확정 | 바이트가 ISR 에서 하나씩? DMA 덩어리? 패턴 길이 m 과 개수? 겹치는 매칭도 보고? 패턴이 상수(플래시에 표 가능)? 바이트당 마감 시간(ISR)? 프레임에 스터핑이 있나? |
| 2. 무식한 방법 + 복잡도 | 마지막 m 바이트 링 + 매 바이트 m 비교: O(nm) 시간, m 바이트 RAM. 덩어리마다 `memmem` 은 경계에 걸친 매칭을 놓친다(꼬리 m−1 바이트를 이어 붙이면 해결되지만 복사가 생긴다) |
| 3. 더 나은 설계 + 불변식 | KMP: 불변식 "q = 본 것의 접미사인 가장 긴 패턴 접두사(< m)". 실패 함수로 O(1) 상각. 상태 = q 1바이트. 대안: DFA(표 1번/바이트), RK(m 바이트 링 + 해시) |
| 4. 코드 | `while (q && b != p[q]) q = fail[q-1]; if (b == p[q]) q++; if (q == m) { hit; q = fail[m-1]; }` |
| 5. 소리 내어 말할 테스트 | "aa"/"aaaa" 겹침, `55 55 55 7E`, m = 1, m > n, 덩어리 크기 1, 경계에 걸친 매칭, p = a^m 최악, 패턴에 0x00, 해시 충돌 |
| 6. 복잡도 | KMP: O(n + m) 시간, 바이트 하나 최악 m−1 후퇴. RAM: q 1 B (+ p, fail 은 플래시 가능). DFA: (m+1)·256 B, 바이트당 1. RK: m+9 B 정도의 RAM, 해시 일치 때만 O(m) |

### Part A — 실패 함수: "경계의 경계도 경계"

**보조정리.** s 의 경계(border)들을 길이 내림차순으로 나열하면 `π(s), π(s[0..π(s))), …` 의 사슬이다. 증명: b 가 s 의 경계이고 b' < b 가 또 다른 경계면 b' 은 s[0..b) 의 접미사이기도 하다(둘 다 s 의 접미사니까) → b' 은 s[0..b) 의 경계. 거꾸로 s[0..b) 의 경계는 s 의 경계다. 그래서 b 다음으로 긴 s 의 경계는 정확히 π(s[0..b)) 이다. ∎

`p[0..i]` 의 가장 긴 진 경계는 "p[0..i) 의 어떤 경계 b + p[i]" 이고 p[b] == p[i] 여야 한다. 보조정리에 따라 b 후보를 긴 것부터 사슬(`k → fail[k-1] → …`)로 보면 첫 번째로 맞는 것이 답이다. 코드가 바로 그것이다. "aabaaab" 손 추적:

| i | p[i] | 시작 k | 사슬 | fail[i] |
|---|---|---|---|---|
| 1 | a | 0 | p[0]=a 일치 | 1 |
| 2 | b | 1 | p[1]=a≠b → k=fail[0]=0, p[0]=a≠b | 0 |
| 3 | a | 0 | p[0] 일치 | 1 |
| 4 | a | 1 | p[1] 일치 | 2 |
| 5 | a | 2 | p[2]=b≠a → k=fail[1]=1, p[1]=a 일치 | 2 |
| 6 | b | 2 | p[2]=b 일치 | 3 |

결과 `0 1 0 1 2 2 3` — 테스트의 손 계산 사례. 하네스는 이 함수를 정의(`brute_border`: 모든 길이를 직접 비교)와 10190 패턴에서 대조한다: 0 틀림.

```svg
<svg viewBox="0 0 740 235" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a04a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">실패 링크 사슬: p = a a b a a a b, 상태 q=6 ("aabaaa" 일치)에서 바이트 'x'</text>
  <g stroke="currentColor">
    <rect x="60" y="60" width="50" height="30" class="f-acc"/><rect x="110" y="60" width="50" height="30" class="f-acc"/>
    <rect x="160" y="60" width="50" height="30" class="f-acc"/><rect x="210" y="60" width="50" height="30" class="f-acc"/>
    <rect x="260" y="60" width="50" height="30" class="f-acc"/><rect x="310" y="60" width="50" height="30" class="f-acc"/>
    <rect x="360" y="60" width="50" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="85" y="80">a</text><text x="135" y="80">a</text><text x="185" y="80">b</text><text x="235" y="80">a</text>
    <text x="285" y="80">a</text><text x="335" y="80">a</text><text x="385" y="80">b</text>
    <text x="85" y="108">0</text><text x="135" y="108">1</text><text x="185" y="108">0</text><text x="235" y="108">1</text>
    <text x="285" y="108">2</text><text x="335" y="108">2</text><text x="385" y="108">3</text>
  </g>
  <text x="10" y="80" fill="currentColor">p</text>
  <text x="10" y="108" fill="currentColor">fail</text>
  <text x="420" y="80" fill="currentColor">← p[6]='b' ≠ 'x'</text>
  <g stroke="currentColor" fill="none">
    <path d="M385 118 Q285 175 189 122" marker-end="url(#a04a)"/>
    <path d="M185 118 Q160 150 138 122" marker-end="url(#a04a)"/>
    <path d="M135 118 Q110 150 88 122" marker-end="url(#a04a)"/>
  </g>
  <text x="430" y="140" fill="currentColor">q=6 → fail[5]=2: "aa" 뒤에 p[2]='b' ≠ 'x'</text>
  <text x="430" y="160" fill="currentColor">q=2 → fail[1]=1: "a" 뒤에 p[1]='a' ≠ 'x'</text>
  <text x="430" y="180" fill="currentColor">q=1 → fail[0]=0: p[0]='a' ≠ 'x' → q=0</text>
  <text x="10" y="200" fill="currentColor">화살표 = 상태 q 가 비교하는 칸 p[q] 의 이동 (6 → 2 → 1 → 0)</text>
  <text x="10" y="222" fill="currentColor" font-weight="bold">후퇴 3번 = 이 바이트의 비용. 퍼텐셜 q 를 6 만큼 써 버렸으니 다음 바이트들은 0 부터 다시 쌓는다</text>
</svg>
```

### Part A — 스트리밍 불변식과 덩어리 경계

**불변식.** 바이트 t[0..j] 를 넣은 뒤 `q = max{k < m : p[0..k) 가 t[0..j] 의 접미사}`, 그리고 `kmp_push` 가 true ⇔ p 가 t[0..j] 의 접미사. 증명은 귀납: t[0..j] 의 접미사인 패턴 접두사는 "t[0..j−1] 의 접미사인 접두사(즉 q 의 경계 사슬) + t[j]" 이고, 보조정리에 의해 사슬을 긴 것부터 보는 while 루프가 가장 긴 것을 찾는다. 길이 m 에 닿으면 보고하고, 그 경우 m 미만 중 최대 = p 의 가장 긴 진 경계 = `fail[m-1]` 이라 불변식이 유지된다.

이 불변식은 **t 전체가 아니라 q 만** 언급한다. 그래서 덩어리가 어디서 끊겨도 `kmp_t` 를 넘겨주기만 하면 결과가 같다 — "경계에 걸친 매칭" 은 설계상 공짜다. 하네스는 무작위 1200건을 1–37 바이트 덩어리로 다시 넣어 한 번에 넣은 결과와 비교한다: 0 불일치. 그림 1 의 입력 추적 (위치 2 에서 후퇴 1번):

| pos | 덩어리 | 바이트 | q 전 | 후퇴 | q 후 | 보고 |
|---|---|---|---|---|---|---|
| 0 | 1 | 55 | 0 | – | 1 | |
| 1 | 1 | 55 | 1 | – | 2 | |
| 2 | 2 | 55 | 2 | 7E≠55 → fail[1]=1 | 2 | |
| 3 | 2 | 55 | 2 | 1번 | 2 | |
| 4 | 2 | 7E | 2 | – | 3 → fail[2]=0 | 끝 4 |
| 5 | 2 | 55 | 0 | – | 1 | |
| 6 | 3 | 55 | 1 | – | 2 | |
| 7 | 3 | 7E | 2 | – | 3 → 0 | 끝 7 |

### Part A — 상각 O(1): 퍼텐셜 Φ = q

push 한 번에서 while 이 s 번 돌면 q 는 매번 **엄격히** 줄어든다(`fail[q-1] ≤ q−1`). 그 뒤 q 는 많아야 1 늘고, 매칭 뒤 `q = fail[m-1]` 은 또 줄인다. Φ = q ≥ 0, Φ_0 = 0 이므로

  Σ s_j ≤ Σ(증가량) − Φ_n ≤ n.

즉 n 바이트 전체 후퇴 ≤ n. 바이트당 비교는 (후퇴 수 + 2) 이하라 이 코드 그대로 세면 전체 비교 ≤ 3n. `kmp_build` 도 같은 논증으로 후퇴 ≤ m−1. 하네스가 매 건 `steps ≤ n` 을 확인한다: 무작위 1,797,952 바이트에서 후퇴 380,860번 (0.212/바이트), 위반 0.

**하지만 바이트 하나의 최악은 m−1 이다.** p = a^m 이고 상태 m−1 에서 'b' 가 오면 사슬 m−1, m−2, …, 0 을 다 걷는다 (실측: m = 8, 32, 255 → 바이트당 최대 7, 31, 254번. 그래도 총합은 400/1600/12750 바이트에 350/1550/12700번 ≤ n). 상각은 평균의 보장이지 **마감 시간의 보장이 아니다** — ISR 에서 바이트마다 마감이 있으면 이게 판단 포인트다 (Part B 표).

### Part B — KMP DFA: 표 하나, 바이트당 한 번

힌트 4 의 등식 `δ(q, c) = δ(fail[q-1], c)` (c ≠ p[q]) 가 전부다. 정당성: 상태 q 에서 c ≠ p[q] 면 KMP 루프의 첫 반복이 q 를 fail[q-1] 로 바꾸고 c 를 다시 보는데, 그 뒤 일은 "상태 fail[q-1] 에서 c 를 넣은 것" 과 똑같다. 행 m (매칭 직후)은 `δ(fail[m-1], ·)`. 하네스는 길이 ≤ 8 이진 패턴 전부의 모든 칸 12288개를 정의(brute δ)와 대조하고, 패턴에 없는 바이트 254종이 모두 같은 값인지도 본다: 0 틀림.

`55 55 7E` 의 표는 0 이 아닌 칸이 5개뿐이다: 행 0·3 은 `55→1`, 행 1 은 `55→2`, 행 2 는 `55→2, 7E→3`. 1024 B 짜리 `const` 표라 플래시(.rodata)에 들어가고 RAM 은 q 1바이트.

| 방식 | 가변 RAM | 상수 (플래시 가능) | 바이트 하나 최악 | 비고 |
|---|---|---|---|---|
| naive 링 | m B + 인덱스 | p (m B) | m 비교 | O(nm) |
| KMP (실패 함수, MP 링크) | **q 1 B** | p + fail = 2m B | m−1 후퇴 + 1 비교 (a^m) | 총 후퇴 ≤ n |
| KMP 강한 실패 함수 (연습 2) | q 1 B | p + next (int16) = 3m B | 실측 ≤ 1 + log_φ m | KMP 1977 의 원래 형태 |
| KMP DFA | q 1 B | (m+1)·256 B (m=3 → 1 KB, m=32 → 8.25 KB) | **표 1번** | 분기 없음 |
| Rabin–Karp | 링 m B + h, seen ≈ m+9 B | p, hp, B^(m−1) | O(1) + 해시 일치 시 m 비교 | 여러 길이 동시엔 불리 |
| 시프트 레지스터 (연습 1) | 8 B + 채움 카운터 | 패턴·마스크 | 1 비교 | **m ≤ 8 전용** |

강한 실패 함수: p[fail] == p[q] 이면 그 비교는 어차피 실패하니 건너뛴다. a^m 에서는 바로 −1 로 가서 최악이 1 이 된다(연습 2 실측: MP 32 → KMP 1). 피보나치 단어에서는 둘이 같고 log_φ m 정도로 자란다 (m=55 → 8). Knuth–Morris–Pratt 논문의 지연 상한 1 + log_φ m 은 기억에 의존 — 확인 권장. 연습 2 가 길이 ≤ 12 이진 패턴 8190개 전부에서 위반 0 을 확인한다.

### Part B — Rabin–Karp: 스트리밍엔 m 바이트가 필요하다

h(창) = Σ_k s_k·B^(m−1−k). 다음 창으로: `h' = (h − out·B^(m−1))·B + in`. **out** — 창에서 빠지는 바이트 — 을 알아야 하므로 스트리밍 RK 는 마지막 m 바이트를 링에 들고 있어야 한다. KMP 의 "상태 1개" 와 정반대이고, verify 에도 그 링이 쓰인다.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">RK 링 (m=3): 새 바이트가 가장 오래된 칸(head)을 덮고, head 가 한 칸 간다</text>
  <g stroke="currentColor">
    <rect x="40" y="50" width="60" height="34" class="f-warn"/><rect x="100" y="50" width="60" height="34" class="f-mut"/><rect x="160" y="50" width="60" height="34" class="f-mut"/>
    <rect x="380" y="50" width="60" height="34" class="f-ok"/><rect x="440" y="50" width="60" height="34" class="f-mut"/><rect x="500" y="50" width="60" height="34" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="70" y="72">55</text><text x="130" y="72">55</text><text x="190" y="72">55</text>
    <text x="410" y="72">7E</text><text x="470" y="72">55</text><text x="530" y="72">55</text>
  </g>
  <text x="45" y="104" fill="currentColor">head (out = 55)</text>
  <text x="435" y="104" fill="currentColor">head →</text>
  <text x="250" y="72" fill="currentColor">7E 들어옴 ⇒</text>
  <text x="40" y="132" fill="currentColor" font-family="monospace">h' = (h − out·B^(m−1))·B + in        mod 2^32: uint32 wrap 그대로</text>
  <text x="40" y="152" fill="currentColor" font-family="monospace">     mod P: t = h + P − (out·B^(m−1) mod P) &lt; 2^32, h' = (t·B + in) mod P</text>
  <text x="40" y="180" fill="currentColor">h' == hp 이고 seen ≥ m 일 때만: 링을 head 부터 읽어 패턴과 비교(verify) → 같으면 보고, 다르면 false_hits++</text>
</svg>
```

**모듈러 선택.** mod 2^32 는 곱셈 하나에 나머지가 공짜(wrap). mod P = 2^31−1 은 메르센 소수라 나눗셈 없이 `(x & P) + (x >> 31)` 두 번 접기로 줄인다 (x < 2^63 이면 두 번 뒤 < 2^31+4 < 2P, 마지막 조건부 빼기). 하네스는 `mod_p` 를 `%` 와 20만 번 + 경계값(0, P, 2P, 2^62−1)으로 대조한다. Cortex-M4 에서 이 롤링(`demo_m4.c` 의 `rkp_roll`)은 `umull` + `umlal` 이 되고, Cortex-M0(`--target=thumbv6m-none-eabi -mcpu=cortex-m0`)에서는 64비트 곱이 `bl __aeabi_lmul` 두 번이 된다 (둘 다 이 맥의 clang `-O2 -S` 실제 출력).

**왜 mod 소수인가 — 확률 보장.** P 가 소수이고 B 를 [0, P) 에서 균일하게 고르면, 서로 다른 길이 m 문자열 x ≠ y 에 대해 h(x) − h(y) = Σ (x_k − y_k) B^(m−1−k) 는 F_P 위의 0 아닌 다항식(계수 |x_k − y_k| < 256 < P)이고 차수 ≤ m−1 이라 근이 많아야 m−1 개다. 그래서 **창 하나당 충돌 확률 ≤ (m−1)/P**. m = 8 이면 3.3·10^−9, 1 MB 스트림 전체로는 합집합 상계 3.4·10^−3. 작지만 0 이 아니다 → **verify 는 필수**. 그리고 이 보장은 "B 가 비밀이고 무작위" 일 때만이다 — B 가 공개 상수면 아래 생일 공격이 그냥 통한다. 부팅 때 하드웨어 RNG 나 칩 고유 ID 로 B 를 고르면 미리 계산한 공격은 막힌다.

**mod 2^32 는 체(field)가 아니다 — Thue–Morse.** T_0 = a, T_{k+1} = T_k · ~T_k (~ 는 a↔b). D_k = h(T_k) − h(~T_k) 라 하면 T_{k+1} 과 ~T_{k+1} 의 해시 정의에서 바로 D_{k+1} = D_k·(B^{2^k} − 1), 즉 D_k = (a−b)·∏_{i<k}(B^{2^i} − 1). 홀수 B 면 v2(B−1) ≥ 1, 그리고 i ≥ 1 에서 B^{2^i} − 1 = (B−1)(B+1)(B²+1)…(B^{2^{i−1}}+1) 이라 v2 ≥ 3 + (i−1) = i+2. k = 7 (길이 128): 1 + (3+4+5+6+7+8) = 34 ≥ 32 → **모든 홀수 B 에서** D_7 ≡ 0 (mod 2^32). B 가 짝수면 B^32 ≡ 0 이라 32 바이트보다 오래된 바이트는 아예 해시에 안 들어간다 — 더 나쁘다. 실제 출력:

```text
thue-morse 128: mod 2^32 collisions 5/5 bases (len 64: 3/5), mod p differs=1 | no-verify matches=1, verify matches=0 (false_hits=1)
```

base 는 31, 257, 0x01000193(FNV), 0x9E3779B1, 0x5BD1E995. 길이 64 (k = 6, 일반 보장은 v2 ≥ 26)에서는 3개만 충돌했다: 같은 식으로 계산한 v2(∏) 가 41, 63, 26, 39, 27 이고 32 이상인 셋(31, 257, 0x9E3779B1)이 정확히 B ≡ ±1 (mod 8) 인 것들이다 — v2(B²−1) 이 클수록 짧은 Thue–Morse 로도 깨진다. mod P 에서는 길이 128 쌍도 충돌하지 않았다.

**mod 소수도 B 가 공개면 — 생일 공격.** 해시 공간 ≈ 2^31, 8바이트 문자열 n = 2^18 개의 해시를 정렬하면 같은 쌍 기대값 n(n−1)/(2P) ≈ 16:

```text
birthday mod 2^31-1: 262144 strings, 14 colliding pairs (expected ~16.0); first pair #94257/#173890 hash=03bb7501 | no-verify matches=1, verify matches=0
```

두 충돌 모두 verify 가 거르고(`false_hits=1`), verify 를 끄면 가짜 매칭 1개. 무작위 바이트 20만 개에서는 m = 8 로 해시 일치 0 — 정상 데이터에서 verify 비용은 사실상 0 이다.

### Part C — 프레임 동기: 가짜 프리앰블과 재생

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">가짜 프리앰블(len=05)이 진짜 프레임을 몸통으로 읽는다 → ck 틀림 → 몸통을 다시 헌팅</text>
  <g stroke="currentColor">
    <rect x="20" y="50" width="46" height="30" class="f-warn"/><rect x="66" y="50" width="46" height="30" class="f-warn"/><rect x="112" y="50" width="46" height="30" class="f-warn"/>
    <rect x="158" y="50" width="46" height="30" class="f-warn"/>
    <rect x="204" y="50" width="46" height="30" class="f-ok"/><rect x="250" y="50" width="46" height="30" class="f-ok"/><rect x="296" y="50" width="46" height="30" class="f-ok"/>
    <rect x="342" y="50" width="46" height="30" class="f-ok"/><rect x="388" y="50" width="46" height="30" class="f-ok"/><rect x="434" y="50" width="46" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="43" y="70">55</text><text x="89" y="70">55</text><text x="135" y="70">7E</text><text x="181" y="70">05</text>
    <text x="227" y="70">55</text><text x="273" y="70">55</text><text x="319" y="70">7E</text><text x="365" y="70">01</text><text x="411" y="70">42</text><text x="457" y="70">ck</text>
    <text x="43" y="98">0</text><text x="89" y="98">1</text><text x="135" y="98">2</text><text x="181" y="98">3</text><text x="227" y="98">4</text>
    <text x="273" y="98">5</text><text x="319" y="98">6</text><text x="365" y="98">7</text><text x="411" y="98">8</text><text x="457" y="98">9</text>
  </g>
  <path d="M158 112 L480 112" stroke="currentColor" stroke-width="2"/>
  <text x="160" y="130" fill="currentColor">가짜 프레임의 len+payload+ck = pos 3..9 → ck(9) ≠ ~(05+55+55+7E+01+42)</text>
  <path d="M204 150 L480 150" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="206" y="168" fill="currentColor">재생: pos 3..9 를 FIFO 앞에 되돌림 → 헌팅이 pos 4..6 에서 프리앰블 발견</text>
  <text x="206" y="188" fill="currentColor">→ 진짜 프레임(시작 4, len 1, 42) 내보냄. 재생 없으면 이 프레임은 사라진다</text>
  <text x="10" y="222" fill="currentColor">무작위 스트림 120개: 재생 있음 23179 프레임 (= 참조), 재생 없음 15028 — 약 35% 분실 (7. 실수 7)</text>
  <text x="10" y="242" fill="currentColor">대가: 안쪽 진짜 프레임은 바깥 가짜 프레임이 끝날 때까지(≤ 34 바이트, 115200 bps 에서 약 3 ms) 늦게 나온다</text>
</svg>
```

`sync_push` 의 루프: live 바이트를 FIFO 뒤에 넣고, FIFO 가 빌 때까지 앞에서 꺼내 헌팅(`kmp_push`) 또는 수집(`fb[]`)을 한다. 프레임이 끝나면 ck 확인 → 유효하면 내보내고 `k.q = 0`, 깨졌으면 `fb[]` 를 FIFO **앞에** 되돌린다. 프리앰블 뒤 바이트만 되돌리는 것으로 충분한 이유: 프리앰블 안쪽에서 시작하는 다른 후보는 KMP 상태 `fail[m-1]` 이 이미 들고 있다(`55 55 7E` 는 경계가 없어 0 이지만, 코드는 일반형).

**참조 파서와의 동치.** 오프라인 참조는 "왼쪽부터: 위치 i 에 프리앰블 + 유효 프레임이면 내보내고 i += 프레임 길이, 아니면 i++" 다. 스트리밍은 프레임이 깨지면 i+1 부터의 후보를 (KMP 상태 + 재생으로) 전부 다시 보고, 유효하면 프레임 뒤로 간다 — 같은 규칙이다. 단 스트림 끝에서 덜 끝난 프레임은 스트리밍이 기다리는 중이므로 참조도 거기서 멈춘다(테스트 스트림 끝에는 유휴 0xFF 40 바이트를 둔다). 실측:

```text
uart streams: 120, 997521 bytes | frames inserted=23180 recovered=23156 swallowed-by-valid-false-frame=24 other=0 | false syncs=12892 | stream!=ref: byte=0 chunked=0 | work 1.152 bytes/byte, max per push=64, fifo overflow=0
```

심은 프레임 중 24개는 **우연히 ck 까지 맞은 가짜 프레임**(확률 1/256)이 삼켰다 — 가짜 프리앰블 약 7700개 × 1/256 ≈ 30 과 맞다. 하네스는 회수 못 한 프레임이 모두 "앞에 내보낸 프레임의 범위 안" 인지 확인한다(other=0). 8비트 합 체크섬의 한계이고, CRC (ALGO-M01) 나 길이 필드 자체의 보호로 줄인다.

**FIFO 크기 증명.** L = (FIFO 바이트 수) + fn. push 진입 시 FIFO 는 비어 있고 fn ≤ 33 (34 가 되는 순간 프레임이 판정되므로) → push_back 후 L ≤ 34. 루프의 각 동작은 L 을 늘리지 않는다: 헌팅 바이트는 버려지고(−1), 수집은 FIFO→fb 이동(0), 실패는 fb→FIFO 이동(0), 성공은 fb 를 비움(−). 그래서 64 칸 FIFO 는 넘치지 않는다 (실측 overflow 0).

**재생 비용 증명 — 바이트당 ≤ 13번.** 바이트 j 는 live 로 한 번, 그리고 j 를 몸통에 담았다가 깨진 프레임마다 한 번 더 처리된다. 그런 프레임의 프리앰블 시작 s 는 s+3 ≤ j ≤ s+36 → 34개 위치 중 하나. 검출된 프리앰블 시작은 단조 증가하고(검출 뒤에는 그 뒤 바이트만 처리하니까), `55 55 7E` 는 경계가 없어 두 출현이 겹칠 수 없다 → 간격 ≥ 3 → 34개 위치에 많아야 ⌈34/3⌉ = 12개. 그래서 1 + 12 = 13. 실측 평균 1.152 바이트/바이트, 하네스가 ≤ 13 을 확인한다. (경계가 있는 프리앰블이면 출현이 겹칠 수 있어 이 상수가 커진다.)

**하지만 push 한 번의 최악은 크다.** 오프셋 4z 에 `55 55 7E (32−4z)` 를 8겹 쌓으면 8개 가짜 프레임의 ck 가 모두 같은 바이트에 온다. 그 바이트 하나가 연쇄 재생을 일으킨다:

```text
nested false preambles (lens 4..32): 37 bytes, total work=197, max work in one push=161, false syncs=8
```

평균은 O(1) 인데 바이트 하나가 161 단계. 그래서 **ISR 에는 `sync_push` 를 넣지 않는다**: ISR(또는 DMA)은 바이트를 링 버퍼(DS-M10, ALGO-B10 Part C)에 넣기만 하고, 태스크가 꺼내 `sync_push` 를 돌린다. ISR 안에서 프리앰블 "검출" 만 해야 한다면 DFA(바이트당 표 1번)가 맞다. 이 프리앰블에서 KMP 의 바이트당 최대 후퇴는 2번이지만, 패턴이 바뀌면 m−1 까지 간다.

### Cortex-M4 에서 — `demo_m4.c`

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -c -fstack-usage demo_m4.c -o demo_m4.o && cat demo_m4.su
```

`dfa_push` 전체 (실제 출력, 지시어 제외):

```text
dfa_push:
	mov	r2, r0
	ldrb	r0, [r0]
	orr.w	r0, r1, r0, lsl #8
	movw	r1, :lower16:DFA
	movt	r1, :upper16:DFA
	ldrb	r1, [r1, r0]
	subs	r0, r1, #3
	clz	r0, r0
	lsrs	r0, r0, #5
	strb	r1, [r2]
	bx	lr
```

분기 0개, 인덱스는 `(q << 8) | b` 를 `orr.w … lsl #8` 한 번으로, 매칭 여부는 `clz`/`lsrs` 로 분기 없이 만든다. `-fstack-usage` 결과: `kmp_push 8 static`, `dfa_push 0 static`, `rk32_push 16 static`, `rkp_roll 8 static`. `kmp_push` 는 후퇴 루프가 있고(컴파일러가 4번 펼쳤다), `rk32_push` 는 `h − out·B²` 와 `·B + b` 가 `mla` 두 개가 됐다 (B² 의 음수를 상수로 미리 접었다).

### 처리량 — `demo_bench.c`

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

Apple M6, Apple clang 21.0.0, 8 MiB 텍스트, 매처마다 워밍업 1 + 11회, ns/바이트 **중앙값 (최소)**. 측정 전후 `uptime` load average **55–67** (다른 작성 에이전트 10개가 같이 돌고 있음) — 분산이 커서 2배 이내 차이는 믿지 말 것. 실제 출력 정리:

| 매처 | 무작위 바이트, m=8 | MP 최악: a^32, (a^31 b)* | UART 잡음, 55 55 7E |
|---|---|---|---|
| naive-ring | 1.19 (0.93) | **12.34 (9.97)** | 4.41 (3.17) |
| kmp-mp | 1.34 (1.15) | 2.83 (1.84) | 6.46 (4.03) |
| kmp-strong | 2.19 (1.40) | 0.75 (0.73) | 5.02 (3.68) |
| dfa | 2.82 (1.21) | 1.95 (1.58) | 2.72 (2.42) |
| rk32 | 1.55 (1.24) | 1.38 (1.22) | 1.75 (1.40) |
| rkp | 4.60 (3.51) | 4.02 (3.34) | 5.16 (3.30) |
| shift64 | 0.57 (0.48) | n/a (m > 8) | 0.73 (0.60) |

매칭 수는 세 텍스트 모두 매처 7개가 같았다 (65 / 0 / 105852). 읽을 것:

- naive 는 m 에 비례: m = 32 최악 텍스트에서 다른 것들의 3–16배. 무작위 텍스트에서는 첫 바이트에서 거의 바로 틀려서 빠르다 — **평균 입력으로 고른 알고리즘은 최악 입력에서 무너진다.**
- KMP(MP) 는 "최악" 텍스트에서도 2.8 ns 로 잡음 텍스트보다 빠르다: 후퇴 총합이 ≤ n 이고, 주기적인 입력이라 분기 예측이 잘 맞는 것으로 보인다(카운터로 확인하지는 않음). 0x55 가 35% 섞인 잡음에서는 분기가 예측 불가라 가장 느린 축(6.5 ns)이고, 분기 없는 DFA 가 2.7 ns 로 앞선다.
- mod P 는 mod 2^32 보다 2.5–3배 느리다 (64비트 곱 + 접기). 이 호스트는 64비트 곱이 한 명령이고, Cortex-M0 이면 `__aeabi_lmul` 호출이라 차이가 더 벌어질 것이다(측정 안 함).
- m ≤ 8 이면 시프트 레지스터가 가장 단순하고 빠르다. 면접의 정답은 종종 "KMP 를 짤 줄 알지만 3바이트면 이걸 쓴다" 다.

### 복잡도 정리

| | 시간 | 바이트 하나 최악 | RAM (가변) | 스택 |
|---|---|---|---|---|
| `kmp_build` | O(m) | – | fail m B | 상수 |
| `kmp_push` | 상각 O(1), 총 후퇴 ≤ n | m−1 후퇴 | q 1 B | 8 B (M4) |
| `dfa_build` / `dfa_push` | O(256·m) / O(1) | 1 표 접근 | q 1 B (+ 표 (m+1)·256 B, 플래시) | 0 B (M4) |
| `rk_push` | O(1) + 해시 일치 시 O(m) | O(m) (verify) | m + 9 B 정도 | 16 B (M4, m=3) |
| `sync_push` | 상각 ≤ 13 단계/바이트 | 실측 161 단계 | fb 34 + FIFO 64 + 상태 ≈ 120 B | 상수 (재귀 없음) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san
```

실측 출력 (`./solution` 0.19 s; ASan+UBSan 빌드 0.45 s, 보고 없음):

```text
prefix function: 10190 patterns vs brute border, 0 wrong
exhaustive binary: 126914 (pattern,text) pairs, mismatches kmp=0 dfa=0 rk32=0 rkp=0
random: 1200 cases, 1797952 bytes, 63186 matches | mismatches one-shot=0 chunked=0 dfa=0 rk=0 | fallback steps total=380860 (0.212/byte), cases over n: 0
worst byte: p=a^8, text=(a^7 b)^50: max fallback/byte=7, total=350 over 400 bytes, matches=0
worst byte: p=a^32, text=(a^31 b)^50: max fallback/byte=31, total=1550 over 1600 bytes, matches=0
worst byte: p=a^255, text=(a^254 b)^50: max fallback/byte=254, total=12700 over 12750 bytes, matches=0
dfa vs definition: 12288 cells checked, 0 wrong
thue-morse 128: mod 2^32 collisions 5/5 bases (len 64: 3/5), mod p differs=1 | no-verify matches=1, verify matches=0 (false_hits=1)
birthday mod 2^31-1: 262144 strings, 14 colliding pairs (expected ~16.0); first pair #94257/#173890 hash=03bb7501 | no-verify matches=1, verify matches=0
random 200000 bytes, m=8: rk32 hash hits=0 false=0 | rkp hash hits=0 false=0
uart streams: 120, 997521 bytes | frames inserted=23180 recovered=23156 swallowed-by-valid-false-frame=24 other=0 | false syncs=12892 | stream!=ref: byte=0 chunked=0 | work 1.152 bytes/byte, max per push=64, fifo overflow=0
nested false preambles (lens 4..32): 37 bytes, total work=197, max work in one push=161, false syncs=8
40/40 passed
```

**하네스가 버그를 잡는지 — 일부러 넣어 본 변형** (solution.c 의 사본, 레슨 폴더 밖에서): 아래 7. 의 실수들을 한 줄씩 넣고 돌린 결과를 각 항목에 적었다. 하나는 잡히지 않았다 — 유효 프레임 뒤 `k.q = 0` 을 지워도 40/40. `55 55 7E` 는 경계가 없어 `fail[2] = 0` 이라 그 줄이 원래 아무 일도 안 하기 때문이다(동치 변형). 경계 있는 프리앰블(예: `55 55 55`)로 바꾸면 의미가 생긴다.

## 7. 자주 하는 실수

**1. 불일치하면 q = 0 (처음부터 다시).** "부분 일치를 버려도 다음 바이트부터 보면 되지" — `55 55 55 7E` 에서 세 번째 55 가 7E 와 안 맞을 때 이미 맞은 `55 55` 의 뒷부분 `55` 가 다음 후보인데 버린다. 실제 변형: 전수 이진 매칭 4552건 불일치, 프레임 사례 C2 실패, 무작위 UART 120개 전부 참조와 다름, 33/40. → `q = fail[q-1]` 사슬.

**2. 매칭 직후 q = 0.** 겹치는 매칭을 놓친다 ("aa" in "aaaa" → 1, 3 만). 실제: 손 사례 2개 + 전수 4620건 + 무작위 98건, 35/40. → `q = fail[m-1]`. (프레임처럼 "겹치면 안 되는" 대상이면 그 결정은 **상위 계층**(Part C)이 한다.)

**3. 실패 링크 인덱스를 하나 밀린 것으로.** `fail[q]` 를 쓰면(정의는 `fail[q-1]` = 길이 q 접두사의 경계) 엉뚱한 상태로 가고, 경우에 따라 q 가 줄지 않아 무한 루프가 된다. 실제 변형(무한 루프를 1000번에서 끊음): 상각 위반 281건, 최악 바이트 1001번, 33/40.

**4. 덩어리(DMA 버퍼)마다 따로 검색하거나 콜백마다 상태 리셋.** 경계에 걸친 매칭이 사라진다 (그림 1 의 두 매칭이 모두 그렇다). 버퍼 기반이 꼭 필요하면 이전 버퍼의 마지막 m−1 바이트를 앞에 붙여 검색하고 그 구간의 중복 보고를 빼야 한다 — 상태 q 하나 넘기는 편이 훨씬 싸다.

**5. RK 에서 "창이 찼나" 를 안 본다.** 0 으로 채운 링이 창 역할을 해서 패턴 `00 00 7E` 가 스트림 첫 바이트 7E 에서 매칭된다. 실제: 그 테스트 2개 실패, 38/40. 시프트 레지스터도 같은 함정(연습 1: 카운터 없이 1, 있으면 0).

**6. RK 에서 verify 를 생략한다.** "32비트 해시면 충분하다" — Thue–Morse 128 바이트는 **모든 홀수 base** 에서 충돌한다. 실제: 그 변형은 Thue–Morse·생일 충돌 테스트에서 가짜 매칭을 보고, 38/40. mod P 의 뺄셈에서 `+ P` 를 빠뜨린 변형은 무작위 156건 불일치(39/40) — 전수 이진 테스트는 통과했다(작은 값에서는 wrap 이 드러나지 않음). 무작위 테스트가 필요한 이유.

**7. 깨진 프레임의 바이트를 버린다.** 가장 흔한 프레이머 버그. 실제 변형: 무작위 UART 스트림에서 참조 23179 프레임 중 15028 개만 회수(약 35% 분실 — 가짜 프리앰블 뒤의 진짜 프레임이 통째로), 36/40. → FIFO 앞에 재생.

**8. 재생을 재귀로 짠다.** "깨진 몸통을 `sync_feed(fb, fn)` 로 다시 넣자" — 몸통 안에서 또 깨지면 재귀가 중첩된다(8겹 사례면 깊이 8, 길이 제한이 없으면 더). 위의 FIFO 는 L ≤ 34 를 증명할 수 있고 스택이 일정하다.

## 8. 면접 Q&A

<details><summary>Q1. KMP 가 O(n + m) 인 이유를 증명해 보세요. 그럼 바이트 하나당 상수 시간인가요?</summary>

퍼텐셜 Φ = q. 후퇴 한 번마다 q 가 엄격히 줄고(fail[q−1] ≤ q−1), 바이트마다 q 는 많아야 1 늘어난다. Φ ≥ 0 이므로 전체 후퇴 ≤ 전체 증가 ≤ n. 그래서 매칭은 O(n), 전처리는 같은 논증으로 O(m). 하지만 **상각**이다: 바이트 하나는 m−1번 후퇴할 수 있다(p = a^255 에서 실측 254번). 마감 시간이 있는 ISR 이면 상각으로는 부족하다 → DFA(바이트당 표 1번, 1 KB/3바이트 패턴) 또는 강한 실패 함수(최악 1 + log_φ m 정도, 연습 2 실측). 꼬리: "평균은?" → 하네스의 무작위 텍스트(알파벳 2–256 섞음, 패턴 일부러 심음)에서 0.21 후퇴/바이트(실측).

</details>

<details><summary>Q2. 3바이트 프리앰블에 정말 KMP 를 쓰겠어요?</summary>

아니다. m ≤ 8 이면 `w = (w << 8) | b; if (filled && (w & mask) == pat)` 시프트 레지스터가 가장 단순하고(연습 1), 이 맥에서도 가장 빨랐다(0.6–0.7 ns/바이트). KMP 를 꺼내는 경우: 패턴이 8바이트보다 길거나(헤더 + 고정 필드), 실행 중에 바뀌거나, 여러 개라 Aho–Corasick 로 갈 때(연습 3), 비트 단위가 아니라 "상태 하나로 경계를 넘긴다" 는 구조가 필요할 때. 비트 단위 동기 워드(무선 모뎀)는 보통 수신기 하드웨어가 sync word 레지스터로 검출한다 — 칩마다 다르니 데이터시트 확인. 면접 답의 핵심은 "KMP 를 짤 수 있다 + 언제 안 쓰는지 안다".

</details>

<details><summary>Q3. DFA 와 실패 함수 중 무엇을 펌웨어에 넣겠어요?</summary>

트레이드오프는 "표 크기 vs 바이트당 최악". 실패 함수: 상수 2m B, 바이트당 최악 m−1 후퇴. DFA: (m+1)·256 B (m = 3 → 1 KB, m = 32 → 8.25 KB), 바이트당 정확히 표 1번, Cortex-M4 에서 분기 0개·스택 0 B (실제 asm). 패턴이 컴파일 타임 상수면 DFA 를 `const` 로 플래시에 굽는다 — RAM 은 q 1바이트. 플래시가 빠듯하거나 패턴이 런타임 설정이면 실패 함수. 중간 해법: 알파벳 압축(패턴에 나오는 바이트 k 종 + "그 밖" 1열 → (m+1)(k+1) B, 바이트→열 매핑 표 256 B 를 하나 더 둔다). 꼬리: "플래시 대기 상태(wait state)는?" → 표 접근이 플래시 읽기라 캐시/ART 가속기 없으면 몇 사이클 더 든다 — 바이트당 1번이라 여전히 예측 가능.

</details>

<details><summary>Q4. Rabin–Karp 에서 해시가 같으면 바로 매칭이라고 해도 되나요? 모듈러는 뭘 쓰죠?</summary>

안 된다. 해시는 m 바이트를 32비트로 줄이니 충돌은 반드시 있다. mod 소수 P + 무작위 비밀 base 면 창 하나당 충돌 ≤ (m−1)/P 라는 보장이 있다(차수 m−1 다항식의 근 개수). mod 2^32 는 체가 아니라서 이 논증이 안 되고, 실제로 Thue–Morse 128 바이트 쌍은 **어떤 홀수 base 에서도** 충돌한다(증명 + 실측 5/5). base 가 공개면 mod 소수도 생일 공격으로 2^18 개 중 14 쌍(실측). 그래서 해시 일치 → 링과 패턴을 비교(verify). 정상 데이터에서 해시 일치는 드물어 verify 비용은 거의 0. 꼬리: "verify 를 하면 최악은?" → 공격자가 해시 일치를 계속 만들면 O(nm). 비밀 base 가 그걸 막는다.

</details>

<details><summary>Q5. 스트리밍 KMP 는 상태 1바이트인데 RK 는 왜 m 바이트가 필요하죠? 그럼 RK 를 언제 쓰나요?</summary>

롤링 식 `h' = (h − out·B^(m−1))·B + in` 에 **빠지는 바이트 out** 이 들어간다. 스트림을 버퍼링하지 않으니 직접 링에 들고 있어야 하고, verify 에도 필요하다. RK 가 유리한 곳: (1) 같은 길이의 패턴 **여러 개**를 해시 집합으로 한 번에 (2) 2D 패턴, 중복 부분 문자열 찾기(이진 탐색 + 해시) 같은 오프라인 문제 (3) 콘텐츠 정의 청킹(롤링 해시가 특정 값일 때 자르기 — 백업/중복 제거). 고정 단일 패턴 스트리밍이면 KMP/DFA/시프트 레지스터가 RAM·최악 모두 낫다.

</details>

<details><summary>Q6. 가짜 프리앰블 문제를 설명하고, 해결책의 비용은?</summary>

잡음 속 우연한 `55 55 7E` 가 len 을 읽고 뒤의 진짜 프레임을 몸통으로 삼킨다. ck 가 틀려도 몸통을 버리면 진짜 프레임이 사라진다(실측 35% 분실). 해결: 깨진 프레임의 프리앰블 **뒤** 바이트를 다시 헌팅(재생). 비용: 상각 바이트당 ≤ 13 처리(증명: 경계 없는 프리앰블의 출현은 겹치지 않으므로 바이트 하나를 덮는 깨진 프레임 ≤ 12), 실측 1.15. 하지만 push 한 번은 161 단계까지(8겹 사례) → 태스크에서 실행. 지연: 안쪽 진짜 프레임은 바깥 가짜 프레임이 끝날 때(≤ 34 바이트, 115200 bps 에서 약 3 ms) 나온다. 다른 해법: 바이트 스터핑(HDLC: 0x7E 플래그, 0x7D 이스케이프 — RFC 1662)이면 플래그가 페이로드에 안 나와 재생이 필요 없지만 최악 2배 길이가 된다. COBS 도 같은 계열.

</details>

<details><summary>Q7. 동기 워드는 어떻게 고르나요? 55 55 55 와 55 55 7E 중 뭐가 낫죠?</summary>

자기 겹침(경계)이 없는 쪽. `55 55 55` 는 경계가 1, 2 라 `55 55 55 55` 에서 어디가 프레임 시작인지 모호하고, 출현이 겹칠 수 있어 재생 비용 상계도 커진다. 무작위 바이트에서 처음 나올 때까지 기대 바이트 수는 Conway 식 Σ 256^k (k 는 m 자신을 포함한 모든 경계 길이): `55 55 7E` 16,777,216, `55 55 55` 16,843,008 (연습 4 실측) — 경계가 있으면 평균 대기는 조금 **길어지지만** 한 번 나오면 뭉쳐서 나온다. 115200 bps 순수 잡음이면 3바이트 동기가 우연히 나오는 평균 간격은 약 1456 초 — 실제 잡음은 0x55·0x00·0xFF 가 많아 훨씬 자주 나온다(Part C 테스트 스트림에서 깨진 동기 12892번/100만 바이트 — 일부러 넣은 가짜 프리앰블 약 7700개 포함). 그래서 길이 + 체크섬/CRC 로 2차 확인이 필수. 무선에서는 자기상관이 낮은 Barker 계열 코드를 쓴다(세부 확인 권장). QPROB-M02 의 Penney 게임이 같은 KMP 자동자 계산이다.

</details>

<details><summary>Q8. 이 검출기를 ISR, DMA, 멀티코어 환경에 어떻게 배치하나요?</summary>

ISR 은 바이트를 링 버퍼(ALGO-B10 Part C, DS-M10)에 넣기만 하거나, DMA 순환 버퍼의 반쪽/전체 콜백에서 태스크를 깨운다. 태스크가 덩어리를 `sync_push` 로 흘린다 — 상태가 구조체에 있으니 덩어리 경계는 문제없다. ISR 에서 꼭 검출해야 하면(예: 프리앰블 시각을 타임스탬프로 찍기) DFA 를 쓴다: 분기 0, 스택 0, 바이트당 고정 사이클. UART 가 여러 개면 `sync_t` 를 UART 마다 하나(공유 없음 → 락 불필요). 단일 생산자–단일 소비자 링이면 코어 간에도 락 없이 되고(DS-A01), 상태를 한 코어만 만지게 한다. 프레임이 끝없이 덜 끝나는 경우(마지막 프레임이 잘림)는 UART 유휴(IDLE line) 인터럽트나 바이트 간 타임아웃으로 `fb` 를 강제 실패시켜 재생한다(이 레슨 코드는 스트림 끝 유휴 바이트로 대신).

</details>

<details><summary>Q9. 정수 폭에서 깨지는 곳은?</summary>

(1) q·fail 을 `uint8_t` 로 두면 m ≤ 255 — DFA 표 칸도 `uint8_t` 라 같은 제한. 더 길면 `uint16_t` 와 표 2배. (2) RK mod 2^32 는 unsigned wrap 이 정의된 동작이라 안전하지만 `int` 로 하면 오버플로 UB(C11 6.5p5). (3) mod P 의 `out·B^(m−1)` 은 8+31 비트, `t·B` 는 32+31 비트 → `uint64_t` 중간값 필수. M4 는 `umull`/`umlal`, M0 는 `__aeabi_lmul` 호출(실제 asm). (4) `ck` 는 `uint8_t` 로 더해야 한다 — `int` 합에 `~` 를 하면 상위 비트까지 뒤집혀 비교가 항상 틀린다. (5) 스트림 위치 `uint32_t` 는 115200 bps 로 약 4.3일이면 wrap — 위치를 비교에 쓰면 부호 없는 차이로.

</details>

## 9. 추가 연습

### 연습 1 — m ≤ 8 이면 시프트 레지스터

`uint64_t` 하나로 마지막 8바이트를 들고 `(w & mask) == pat` 로 매칭하라. 패턴에 0x00 이 있어도 시작 직후 가짜 매칭이 없게 하라. naive 와 3000건 대조.

<details><summary>답 보기</summary>

`w = (w << 8) | b` 후 하위 8m 비트를 비교. `mask = (1 << 8m) − 1` 은 m = 8 에서 시프트 양이 64 라 UB(C11 6.5.7p3) → `~0ull` 로 따로. 함정은 처음 m−1 바이트: w 의 상위가 0 이라 `00 00 7E` 가 첫 바이트 7E 에서 맞아 버린다 → `filled` 카운터. 실측: 카운터 없이 1 (가짜), 있으면 0. 바이트당 비교 1번, 분기 1개, RAM 9 바이트. m ≤ 4 면 `uint32_t` 로 Cortex-M 레지스터 하나.

</details>

### 연습 2 — 강한 실패 함수와 바이트당 최악 비교

KMP 원래의 next: `strong[q] = (p[fail[q-1]] == p[q]) ? strong[fail[q-1]] : fail[q-1]`, strong[0] = −1. 모든 상태 × 256 바이트에 대해 "바이트 하나의 비교 수" 를 재서 MP 와 비교하라 (길이 ≤ 12 이진 패턴 전부, 피보나치 단어, a^32).

<details><summary>답 보기</summary>

p[q] 에서 틀렸다면 바이트 c ≠ p[q]. 다음 후보 r = fail[q−1] 에서 p[r] == p[q] 면 c ≠ p[r] 도 확실하니 비교를 건너뛰고 strong[r] 로. 실측(practice.c): 이진 패턴 8190개 중 최악 MP 12 vs strong 5, a^32 는 MP 32 → strong 1. 피보나치 단어(f = f_{k−1} f_{k−2})는 둘이 같다: m = 8/13/21/34/55 → 4/5/6/7/8 — log_φ m 에 가깝게 자란다. 1 + log_φ m 위반 0 (상한의 출처는 KMP 1977 — 확인 권장). 매칭 결과는 naive 와 같아야 한다(확인). 펌웨어 의미: 표는 실패 함수 크기 그대로(int16 m 개)인데 최악이 m 에서 로그로.

</details>

### 연습 3 — Aho–Corasick: 동기 워드 여러 개

`55 55 7E`, `AA 7E`, `7E 7E 7E 55`, `7E` 를 한 번에 찾는 오토마톤을 만들어라 (트라이 + BFS 로 실패 링크, goto 를 256 폭으로 완성 = DFA). 출력은 (패턴 번호, 끝 위치), 짧은 패턴이 긴 패턴의 접미사여도 보고.

<details><summary>답 보기</summary>

트라이를 만든 뒤 루트 자식부터 BFS: 노드 v 의 자식 u (글자 c)의 실패 링크 = `go[link[v]][c]`, 없는 전이는 `go[v][c] = go[link[v]][c]` — KMP DFA 의 "행 복사" 를 트리로 일반화한 것. 출력 집합은 `out[v] |= out[link[v]]` (7E 가 7E 7E 7E 55 안에도, AA 7E 끝에도 있다). 실측: 상태 10개, 무작위 400 패턴 집합 × 2000 바이트에서 naive 와 (바이트, 패턴) 불일치 0, 적중 237467. 표는 상태 수 × 256 B. Aho & Corasick 1975.

</details>

### 연습 4 — 처음 나올 때까지 몇 바이트? (Penney, Conway)

KMP 자동자를 마르코프 연쇄로 보고 "균일 무작위 기호에서 패턴이 처음 끝날 때까지의 기대 기호 수" 를 연립방정식으로 풀어라. 공식 Σ_{k: 길이 k 경계, k = m 포함} A^k 와 비교하라 (이진 패턴 길이 1–10 전부). `55 55 7E` 는?

<details><summary>답 보기</summary>

E_q = 1 + (1/A) Σ_c E_{δ(q,c)}, E_m = 0, 미지수 m 개 → 가우스 소거. 이진 패턴 2046개 전부 공식과 일치. 동전: HTH = 10, HHH = 14, HHT = 8 — QPROB-M02 Penney 게임의 그 숫자들. 바이트(A = 256): `55 55 7E` = 256³ = 16,777,216, `55 55 55` = 256³ + 256² + 256 = 16,843,008. 115200 bps 8N1(11520 바이트/초)이면 약 1456 초. 공식의 직관: 경계가 있으면 "거의 맞았다가 놓친" 상태에서 다시 시작하는 비용이 더 든다.

</details>

### 연습 5 — 애너그램 창 (AlgoMonster M03 §15)

텍스트의 길이 m 창 중 패턴과 **같은 바이트 다중집합**(순서 무관)인 것을 O(1)/바이트로 모두 찾아라.

<details><summary>답 보기</summary>

`diff[c]` = 창의 c 개수 − 패턴의 c 개수, `nz` = 0 이 아닌 칸 수. 들어오는 바이트와 나가는 바이트에서 diff 를 ±1 하고 0 을 지날 때만 nz 를 고친다 → 바이트당 O(1), nz == 0 이면 보고. RK 의 "덧셈형" 판(순서 무관 해시)과 같은 생각이고, LeetCode 438 Find All Anagrams in a String 이다. 2000건 brute force 와 불일치 0.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san -lm && ./practice_san
```

```text
ex1 shift-register: 3000 cases vs naive, 0 mismatches | 00 00 7E on first byte 7E: without fill=1 with fill=0
ex2 binary patterns len 1..12 (8190): worst comparisons/byte MP=12 KMP(strong)=5, over 1+log_phi(m): 0, match mismatches 0
    fibonacci words: m=1 MP=1 KMP=1 (1+log_phi m=1.00)  m=2 MP=2 KMP=2 (1+log_phi m=2.44)  ...  m=55 MP=8 KMP=8 (1+log_phi m=9.33)
    a^32: MP=32 KMP=1
ex3 aho-corasick sync words {55 55 7E, AA 7E, 7E 7E 7E 55, 7E}: states=10 hits=14/82/1/403
    400 pattern sets x 2000 bytes: 237467 total hits, 0 (byte,pattern) disagreements with naive
ex4 Markov(KMP automaton) vs border sum: 2046 binary patterns, 0 mismatches | HTH=10.0 HHH=14.0 HHT=8.0
    uniform random bytes: 55 55 7E first after 16777216 bytes on average (1456 s at 115200 8N1), 55 55 55 after 16843008
ex5 anagram windows: 2000 cases, 69872 windows, 0 mismatches
12/12 passed
```

(0.21 s; ASan+UBSan 0.70 s, 보고 없음.)

## 10. 핵심 정리

- KMP 상태 q = "본 것의 접미사인 가장 긴 패턴 접두사(< m)". 불일치 → `q = fail[q-1]` 사슬(경계의 경계도 경계), 매칭 → 보고 + `q = fail[m-1]`(겹침). 가변 상태 1바이트라 덩어리 경계는 공짜.
- 상각: Φ = q 로 전체 후퇴 ≤ n. 바이트 하나 최악은 m−1 (a^255 → 254 실측). 마감이 있으면 DFA(표 1번) 또는 강한 실패 함수(≈ log_φ m).
- DFA: `δ(q,c) = δ(fail[q-1],c)`, (m+1)·256 B 를 플래시에. M4 에서 분기 0·스택 0. m ≤ 8 이면 시프트 레지스터 + 채움 카운터가 가장 싸다.
- RK 스트리밍 = m 바이트 링 필수. mod 소수 + 비밀 무작위 base 면 창당 충돌 ≤ (m−1)/P, mod 2^32 는 Thue–Morse 128 B 가 모든 홀수 base 에서 충돌. 해시 일치는 후보일 뿐 — 항상 verify.
- 프레임 동기: 깨진 프레임의 몸통을 재생해야 가짜 프리앰블이 진짜를 못 삼킨다(재생 없으면 35% 분실). 상각 ≤ 13 단계/바이트, push 한 번 최악 161 → ISR 은 링에 넣기만, 파싱은 태스크.
- 테스트 = 정의대로 만든 기준(brute border/δ, naive, 오프라인 파서) × (전수 작은 입력 + 무작위 + 무작위 덩어리 분할) + 일부러 만든 최악/충돌 입력.

## 참고자료

- D. E. Knuth, J. H. Morris, V. R. Pratt, "Fast pattern matching in strings", *SIAM Journal on Computing* 6(2), 1977 — 실패 함수, 강한 next, 지연 상한(log_φ m 형태는 기억에 의존 — 확인 권장).
- R. M. Karp, M. O. Rabin, "Efficient randomized pattern-matching algorithms", *IBM Journal of Research and Development* 31(2), 1987 — 무작위 지문(fingerprint) 매칭.
- A. V. Aho, M. J. Corasick, "Efficient string matching: an aid to bibliographic search", *Communications of the ACM* 18(6), 1975 — 연습 3.
- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, 32장 "String Matching" — 유한 오토마톤 매처(DFA), prefix function 의 상각 분석, Rabin–Karp.
- R. Sedgewick, K. Wayne, *Algorithms* 4판, 5.3절 — KMP 를 R×m DFA 로 만드는 방식(이 레슨의 행 복사와 같은 생각).
- RFC 1662, "PPP in HDLC-like Framing" — 0x7E 플래그와 0x7D 이스케이프(바이트 스터핑): <https://www.rfc-editor.org/rfc/rfc1662>
- Penney 게임과 Conway 의 "leading number"(경계 합 공식) — QPROB-M02. 공식의 원 출처 서지는 확인 안 함 — 확인 권장.
- LeetCode 28 Find the Index of the First Occurrence in a String, LeetCode 1392 Longest Happy Prefix (prefix function 자체), LeetCode 438 Find All Anagrams in a String (연습 5).
- AlgoMonster 노트 M01 §1 (Don 정리본) — "KMP 는 일반 코딩 면접에서 기대하지 않음, Rabin–Karp 롤링 해시는 유용" 이라는 출제 통계. 펌웨어 면접은 스트리밍 조건 때문에 예외인 경우가 이 레슨. M03 §15 Find All Anagrams · §27 Minimum Window Substring (해시 카운트 창, 연습 5), M04 §8 Subtree of Another Tree (직렬화 + KMP 심화 메모).
- 관련 레슨: ALGO-B10 (링 버퍼·고정 창, ISR→태스크), ALGO-M12 (바이트 단위 FSM, 한 글자 동기 `'$'`), ALGO-M01 (CRC — 8비트 합보다 강한 프레임 확인), DS-M10 (트레이스/수신 링 버퍼), DS-A01 (SPSC 큐), QPROB-M02 (Penney 게임 = KMP 자동자), COMM 트랙 (UART 프레이밍).
