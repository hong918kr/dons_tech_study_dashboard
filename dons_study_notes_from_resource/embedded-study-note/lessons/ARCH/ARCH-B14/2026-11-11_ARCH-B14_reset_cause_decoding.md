---
id: ARCH-B14
time: 70분
updated: 2026-11-11
---
# ARCH-B14 리셋 종류와 리셋 원인 디코딩

## 학습 목표

- POR · BOR · 핀(NRST) · 워치독 · 소프트웨어 · LOCKUP · 저전력 리셋이 **무엇을 지우고 무엇을 남기는지** (파워 리셋 vs 시스템 리셋) 표로 설명할 수 있다.
- 리셋 원인 레지스터가 왜 **sticky** 인지, 두 가지 클리어 방식 — ST 스타일 "RMVF 에 1 쓰면 전부 클리어" 와 nRF 스타일 "비트마다 write-1-to-clear" — 을 C 모델로 정확히 구현한다.
- 한 번의 리셋에 플래그가 여러 개 켜지는 경우(POR → POR+BOR+PIN, 내부 리셋 → +PIN)를 **우선순위 인코더** 로 하나의 주 원인으로 줄이고, "안 지워서 쌓인 플래그" 는 우선순위로 풀 수 없다는 것을 보여준다.
- 부팅 코드의 순서 — **읽기 → 디코드 → .noinit 기록 검사 → 카운터 → 로그 → 클리어 → 결정(cold / warm / safe mode)** — 를 짜고, 리셋 시나리오 시퀀스와 2만 단계 무작위 시퀀스로 검증한다.
- 부트로더가 먼저 지우는 버그, W1C 에 `|=` 쓰는 버그, nRF 에서 `RESETREAS == 0` 의 의미를 면접에서 바로 말할 수 있다.

## 1. 문제

현장에서 돌아온 보드의 첫 질문은 언제나 "왜 리셋됐나?" 다 — 부팅 로그 첫 줄에 리셋 원인을 남기지 않는 펌웨어는 워치독 루프, 브라운아웃, 크래시를 구분할 방법이 없고, 그래서 펌웨어·실리콘 검증 면접에서 "워치독 리셋인지 어떻게 알아요?", "그 레지스터 언제 지워요?" 가 자주 나온다.

리셋은 한 종류가 아니다. 칩 안의 **리셋 컨트롤러** 가 여러 소스를 OR 해서 CPU 를 리셋하고, 동시에 "어느 소스였나" 를 **리셋 원인 레지스터** 에 적어 둔다. 이 레지스터는 보통 **파워 리셋에서만 지워지는 영역** 에 있어서, 시스템 리셋을 몇 번 겪어도 플래그가 계속 쌓인다 (sticky). 그래서 부팅 코드가 읽고 → 기록하고 → 직접 지워야 다음 부팅에서 "이번" 원인을 알 수 있다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b14a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">리셋 소스 → 리셋 컨트롤러 → 무엇이 지워지고 무엇이 남나</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="180" height="30" class="f-warn"/>
    <rect x="10" y="80" width="180" height="30" class="f-mut"/>
    <rect x="10" y="120" width="180" height="30" class="f-mut"/>
    <rect x="10" y="160" width="180" height="30" class="f-mut"/>
    <rect x="10" y="200" width="180" height="30" class="f-mut"/>
    <rect x="245" y="55" width="160" height="160" rx="8" fill="none"/>
    <rect x="460" y="40" width="270" height="52" class="f-acc"/>
    <rect x="460" y="106" width="270" height="52" class="f-mut"/>
    <rect x="460" y="172" width="270" height="52" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="60">VDD 감시: POR / BOR</text>
    <text x="20" y="100">NRST 핀 (버튼·디버거)</text>
    <text x="20" y="140">IWDG / WWDG 만료</text>
    <text x="20" y="180">SYSRESETREQ (AIRCR)</text>
    <text x="20" y="220">코어 LOCKUP (칩마다)</text>
    <text x="262" y="110" font-weight="bold">리셋 컨트롤러</text>
    <text x="262" y="134" font-size="12">소스 OR + 펄스 늘이기</text>
    <text x="262" y="154" font-size="12">소스별 플래그 set</text>
    <text x="472" y="62" font-weight="bold">리셋 원인 레지스터 (sticky)</text>
    <text x="472" y="82" font-size="12">파워 리셋 때만 지워짐 → 원인을 기억</text>
    <text x="472" y="128" font-weight="bold">CPU · 주변장치 레지스터</text>
    <text x="472" y="148" font-size="12">모든 리셋에서 리셋값 (LSION 같은 설정 비트 포함)</text>
    <text x="472" y="194" font-weight="bold">SRAM (.noinit 영역)</text>
    <text x="472" y="214" font-size="12">웜 리셋: 대개 유지 · 파워 리셋: 쓰레기</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#b14a)">
    <line x1="190" y1="55" x2="243" y2="80"/>
    <line x1="190" y1="95" x2="243" y2="105"/>
    <line x1="190" y1="135" x2="243" y2="135"/>
    <line x1="190" y1="175" x2="243" y2="165"/>
    <line x1="190" y1="215" x2="243" y2="195"/>
    <line x1="405" y1="90" x2="458" y2="66"/>
    <line x1="405" y1="132" x2="458" y2="132"/>
  </g>
  <path d="M325,215 L325,255 L100,255 L100,112" fill="none" stroke="currentColor" stroke-dasharray="5 4" marker-end="url(#b14a)"/>
  <text x="120" y="275" fill="currentColor" font-size="12">내부 리셋 펄스가 NRST 핀으로 나가는 칩: 워치독·SW 리셋에도 PIN 플래그가 같이 켜진다 (STM32 다수)</text>
  <text x="10" y="306" fill="currentColor">부팅 코드: ① 원인 레지스터 읽기 ② .noinit 기록과 합치기 ③ 지우기 ④ cold / warm / safe mode 결정</text>
</svg>
```

| 리셋 | 트리거 | 지우는 범위 | ST 스타일 플래그 (e.g. STM32F4) | nRF 스타일 (e.g. nRF52) |
|---|---|---|---|---|
| POR / PDR | 전원 인가·하강 | 전부 (리셋 원인 레지스터 포함, VBAT 백업 도메인 제외) | PORRSTF + BORRSTF (+ PINRSTF) — RM0090 의 CSR 리셋값 `0x0E00_0000` | `RESETREAS == 0` |
| BOR | VDD 가 임계 아래로 | 파워 리셋 (이 모델에선 플래그도 지움) | BORRSTF | `0` — POR 과 **구분 불가** |
| 핀 | NRST 를 외부에서 Low | 시스템 리셋 | PINRSTF | RESETPIN |
| 워치독 | IWDG(독립 클럭 LSI) / WWDG(윈도우) 만료 | 시스템 리셋 | IWDGRSTF / WWDGRSTF | DOG |
| 소프트웨어 | `AIRCR.SYSRESETREQ` (CMSIS `NVIC_SystemReset()`) | 시스템 리셋 | SFTRSTF | SREQ |
| LOCKUP | HardFault/NMI 안에서 또 fault → 코어 정지, 칩이 리셋으로 바꿈 (칩마다) | 시스템 리셋 | (이 모델엔 없음) | LOCKUP |
| 저전력 | 옵션 바이트 설정에 따라 Stop/Standby 진입을 리셋으로 바꿈 | 시스템 리셋 | LPWRRSTF | — |
| System OFF 깨어남 | GPIO DETECT 등으로 깨어남 = 리셋 | 시스템 리셋 (RAM 보존은 설정에 따름) | — | OFF |

비트 이름·위치는 **e.g.** 다. STM32 도 계열마다 플래그가 다르고(옵션 바이트 로더 리셋 플래그가 있는 계열 등), 다른 벤더는 이름도 다르다. 그래서 이 레슨의 모델은 두 "스타일" 만 흉내 내고, 디코더는 **벤더 중립 마스크** 로 바꾼 뒤 판단한다.

### Part A — 리셋 원인 레지스터 모델 (하드웨어 쪽)

하드웨어 동작을 C 로 만든다 (이 모델이 있어야 부팅 코드를 테스트할 수 있다).

- `st_hw_reset(r, ev)` — ST 스타일 CSR 에 리셋 이벤트 하나를 적용. 파워 리셋(POR, BOR)은 옛 플래그를 지우고 자기 플래그를 켠다 (POR 은 POR **와** BOR 둘 다). 시스템 리셋은 옛 플래그를 **보존** 하고 자기 플래그를 OR. 모든 리셋에서 설정 비트 LSION 은 0. `pin_echo` 면 PIN 이 아닌 이벤트에도 PINRSTF. 없는 이벤트(LOCKUP, WAKEOFF) → `false`.
- `st_csr_write(r, v)` — 플래그는 **읽기 전용** (써도 무시), LSION 은 읽기/쓰기, `v` 에 RMVF=1 이면 플래그 전부 0, RMVF 는 저장되지 않는다 (읽으면 0).
- `nrf_hw_reset(p, ev)` — POR/BOR → 레지스터 0. 나머지 → 자기 비트 OR. WWDG/LPWR 없음 → `false`.
- `nrf_resetreas_write(p, v)` — write-1-to-clear: 1 을 쓴 구현된 비트만 지운다.

### Part B — 디코딩, 우선순위, 함정

- `st_to_rc(csr)`, `nrf_to_rc(rr)` — 벤더 레지스터 → 공통 마스크 `RC_*`. nRF 에서 알려진 비트가 하나도 없으면 `RC_POWER` ("POR 이나 BOR, 구분 불가").
- `primary_cause(rc)` — 같은 리셋 한 번에 켜진 여러 플래그 중 **주 원인** 하나. 우선순위는 `POR > BOR > POWER > IWDG > WWDG > LOCKUP > SW > LPWR > WAKEOFF > PIN`. `RC_*` 비트 번호를 이 순서로 배치했으므로 "가장 낮은 1 비트" 를 찾으면 된다 (우선순위 인코더 = CLZ/CTZ, ARCH-B05 참고). 내장 함수는 쓰지 않는다.
- 테스트로 확인하는 함정: ① 안 지우고 리셋이 두 번 나면 우선순위가 **틀린** 답을 낸다 ② 부트로더가 먼저 지우면 앱은 ST 에선 NONE, nRF 에선 **POWER(전원 인가)** 로 오판 ③ W1C 에 `|=` → 다 지워짐, RO 플래그에 `&= ~` → 아무 일도 안 함.

### Part C — 부팅 코드: 읽기 · 로그 · 클리어 · 결정

`chip_t` = 벤더 레지스터 + `.noinit` RAM 역할의 `retained_t` (magic, boot_seq, streak, 로그 링 8칸, check). 하드웨어 시뮬레이터 `chip_reset()` 은 주어진다 — 파워 리셋이면 retained 영역을 PRNG 쓰레기로 채운다.

- `boot_reset_handler(c)` — ① 읽기 ② 디코드 ③ retained 유효성(magic, head 범위, check) — 파워 리셋이거나 무효면 0 으로 초기화 ④ `boot_seq++`, `streak` 갱신 (IWDG/WWDG/LOCKUP → +1, 파워·PIN → 0, 나머지 유지) ⑤ 로그 링에 `{seq, raw, cause}` + check 갱신 ⑥ 레지스터 클리어 (ST: LSION 을 보존하며 RMVF, nRF: 읽은 비트만 W1C) ⑦ 결정: `streak ≥ 3` → SAFE, 콜드면 COLD, 아니면 WARM.
- `app_mark_healthy(c)` — 앱이 "정상 동작 확인" 을 찍으면 streak = 0.
- 시나리오: S1 (ST: POR → IWDG ×3 → SAFE → PIN → SW), S2 (ST: healthy 표시가 streak 를 끊음, SW 는 streak 유지, BOR 은 콜드), S3 (nRF: 0 = POWER, DOG·LOCKUP 섞어 3연속 → SAFE, PIN, System OFF 깨어남, BOR), retained 손상, 무작위 2만 단계 × 2 벤더 (독립 오라클과 비교).

## 2. Problem (English)

> **Reset-cause register model and boot-time decoding.**
>
> A reset controller ORs several reset sources (power-on, brown-out, external pin, watchdogs, software request, CPU lockup, low-power entry, wake from System OFF) and records which source fired in a *sticky* reset-cause register that is only cleared by a power reset or by software.
>
> 1. Model two register styles: (a) **ST style** — read-only flags in bits 31..25, a write-only `RMVF` bit (24) that clears all flags when written as 1, and an unrelated read/write config bit `LSION` (bit 0) that is reset by every reset; a power reset clears old flags, POR sets both POR and BOR, and on some parts every internal reset also sets the PIN flag. (b) **nRF style** — per-bit write-1-to-clear; a power-on or brown-out reset leaves the register at 0. Implement `st_hw_reset`, `st_csr_write`, `nrf_hw_reset`, `nrf_resetreas_write`.
> 2. Implement `uint32_t st_to_rc(uint32_t)`, `uint32_t nrf_to_rc(uint32_t)` mapping to a vendor-neutral mask, and `cause_t primary_cause(uint32_t rc)` with priority `POR > BOR > POWER > IWDG > WWDG > LOCKUP > SW > LPWR > WAKEOFF > PIN` (no builtins).
> 3. Implement `boot_info_t boot_reset_handler(chip_t *)`: read, decode, validate a `.noinit` record (magic + checksum), count consecutive abnormal resets (watchdog / lockup), log `{seq, raw, cause}` into an 8-entry ring, clear the register correctly for each style, and decide `COLD` / `WARM` / `SAFE` (3 consecutive abnormal resets). `app_mark_healthy()` resets the counter.
>
> Example: ST style after POR reads `0x0E000000` → `POR` (BOR, PIN are secondary). nRF style reading `0` → `POWER`. Three watchdog resets in a row with no healthy mark → `SAFE`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/ARCH/ARCH-B14
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/esn_ARCH-B14/st && /tmp/esn_ARCH-B14/st; echo "exit=$?"
```

실제 출력 (Apple M6, Apple clang 21 — 앞부분과 끝만):

```text
FAIL starter.c:265  st_hw_reset(&st, EV_POR) && st.csr == 0x0E000000u
FAIL starter.c:268  st2.csr == (ST_PORRSTF | ST_BORRSTF)
...
FAIL starter.c:340  q.resetreas == 0
[S1] ST 스타일 (pin_echo): 전원 → 워치독 3연속 → safe mode → 핀 리셋
   POR     raw=0x00000000  cause=NONE    mode=COLD streak=0 seq=0  after=0x00000000 <-- 기대와 다름
...
FAIL starter.c:403  fuzz(VEND_ST, 0xC0FFEEu, 20000) == 0
FAIL starter.c:404  fuzz(VEND_NRF, 0xBADF00Du, 20000) == 0
5/45 passed
exit=1
```

처음부터 통과하는 5개는 "지원 안 하는 이벤트면 false" 와 "플래그 없음 → NONE" 처럼 스텁 기본값과 우연히 같은 것들이다.

## 4. 힌트

<details>
<summary>힌트 1 — ST 하드웨어 리셋은 "keep | 새 플래그"</summary>

이벤트별 플래그 `f` 를 switch 로 고르고 (`EV_POR` 는 `PORRSTF | BORRSTF`), `pin_echo` 면 `f |= PINRSTF`. 보존할 것은 `keep = 파워 리셋 ? 0 : (csr & ST_FLAGS)`. 결과는 `keep | f` — LSION 을 안 넣으면 "모든 리셋에서 설정 비트 0" 이 저절로 된다.

</details>

<details>
<summary>힌트 2 — 쓰기 의미론은 "어떤 비트가 무엇을 하나" 표</summary>

ST: 플래그 = RO (현재 값 유지), RMVF = 액션 비트 (1 이면 플래그 0, 저장 안 함), LSION = 일반 RW. nRF: `reg &= ~(v & MASK)`. 쓰기 함수는 "레지스터 = f(현재 값, 쓴 값)" 하나로 끝난다.

</details>

<details>
<summary>힌트 3 — 우선순위 인코더</summary>

`RC_*` 비트 번호가 이미 우선순위 순서다. `for (i = 0; i < 10; i++) if (rc & (1u << i)) return i + 1;` — 테스트는 1024 개 마스크 전부를 `__builtin_ctz` 와 비교한다.

</details>

<details>
<summary>힌트 4 — 부팅 코드는 "읽은 값" 으로만 일한다</summary>

레지스터는 처음에 **한 번** 읽어 `raw` 에 담고, 디코드·로그·클리어 모두 그 값을 쓴다. 클리어는 로그를 retained 에 다 쓰고 check 까지 갱신한 **뒤**. nRF 는 `write(raw)` (읽은 비트만 지움), ST 는 `write(csr | RMVF)` (LSION 보존). 파워 리셋이면 retained 가 유효해 보여도 초기화한다.

</details>

## 5. 풀이

### Part A — 레지스터 모델

ST 스타일 CSR 의 비트 배치와 두 클리어 방식:

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">ST 스타일 CSR (e.g. STM32F4 RCC_CSR) vs nRF 스타일 RESETREAS (e.g. nRF52)</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="135" y="44">31</text><text x="205" y="44">30</text><text x="275" y="44">29</text><text x="345" y="44">28</text>
    <text x="415" y="44">27</text><text x="485" y="44">26</text><text x="555" y="44">25</text><text x="625" y="44">24</text><text x="705" y="44">0</text>
  </g>
  <g stroke="currentColor">
    <rect x="100" y="50" width="70" height="30" class="f-acc"/><rect x="170" y="50" width="70" height="30" class="f-acc"/>
    <rect x="240" y="50" width="70" height="30" class="f-acc"/><rect x="310" y="50" width="70" height="30" class="f-acc"/>
    <rect x="380" y="50" width="70" height="30" class="f-acc"/><rect x="450" y="50" width="70" height="30" class="f-acc"/>
    <rect x="520" y="50" width="70" height="30" class="f-acc"/><rect x="590" y="50" width="70" height="30" class="f-ok"/>
    <rect x="680" y="50" width="50" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="135" y="70">LPWR</text><text x="205" y="70">WWDG</text><text x="275" y="70">IWDG</text><text x="345" y="70">SFT</text>
    <text x="415" y="70">POR</text><text x="485" y="70">PIN</text><text x="555" y="70">BOR</text><text x="625" y="70">RMVF</text>
    <text x="670" y="70">…</text><text x="705" y="70">LSION</text>
  </g>
  <text x="10" y="70" fill="currentColor">CSR</text>
  <g fill="currentColor" font-size="12">
    <text x="100" y="100">bit31..25 플래그: 읽기 전용 · sticky · 파워 리셋 때만 지워짐</text>
    <text x="100" y="118">RMVF: 1 을 쓰면 플래그 전부 0, 읽으면 0  →  클리어 = CSR = CSR | RMVF (LSION 보존)</text>
    <text x="100" y="136">POR 직후 = 0x0E00_0000 (POR | PIN | BOR) — "여러 개 켜짐" 이 정상</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="205" y="172">16</text><text x="415" y="172">3</text><text x="485" y="172">2</text><text x="555" y="172">1</text><text x="625" y="172">0</text>
  </g>
  <g stroke="currentColor">
    <rect x="170" y="178" width="70" height="30" class="f-acc"/>
    <rect x="380" y="178" width="70" height="30" class="f-acc"/><rect x="450" y="178" width="70" height="30" class="f-acc"/>
    <rect x="520" y="178" width="70" height="30" class="f-acc"/><rect x="590" y="178" width="70" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="205" y="198">OFF</text><text x="310" y="198">…</text><text x="415" y="198">LOCKUP</text><text x="485" y="198">SREQ</text>
    <text x="555" y="198">DOG</text><text x="625" y="198">RESETPIN</text>
  </g>
  <text x="10" y="198" fill="currentColor">RESETREAS</text>
  <g fill="currentColor" font-size="12">
    <text x="100" y="228">각 비트 write-1-to-clear  →  클리어 = REG = 읽은 값  (REG |= X 는 다 지워버린다)</text>
    <text x="100" y="246">전부 0 = 온칩 리셋 생성기 = POR 또는 BOR (둘을 구분할 수 없다)</text>
  </g>
</svg>
```

ST 모델 상태 전이 (pin_echo = true):

| 직전 CSR (플래그) | 이벤트 | 결과 | 이유 |
|---|---|---|---|
| 아무거나 | POR | `POR\|BOR\|PIN` = `0x0E00_0000` | 파워 리셋: 옛 플래그 삭제, POR 은 BOR 도 켬 |
| `IWDG\|PIN` | BOR | `BOR\|PIN` | 파워 리셋이 옛 IWDG 를 지움 |
| `POR\|BOR\|PIN` (안 지움) | SW | `POR\|BOR\|PIN\|SFT` | 시스템 리셋: sticky 누적 |
| `0`, LSION=1 | IWDG | `IWDG\|PIN`, LSION=0 | 설정 비트는 리셋값으로 |

`st_csr_write` 의 세 경우 (Part A 테스트 그대로):

| 쓴 값 | 결과 | 교훈 |
|---|---|---|
| `LSION` | 플래그 그대로 + LSION | 설정 비트만 바뀐다 |
| `csr \| RMVF` | `LSION` 만 남음 | 올바른 클리어. RMVF 는 다시 읽으면 0 |
| `LSION \| IWDGRSTF` (플래그에 1) | 변화 없음 | 플래그는 하드웨어만 켤 수 있다 |

nRF 쪽은 단순하다: 파워 리셋 = 0, 나머지 = OR, 쓰기 = `reg &= ~(v & MASK)`. 구현 안 된 비트에 1 을 써도 무시된다 (`write(0xFFFFFFFF)` → 전부 0).

### Part B — 디코딩과 우선순위

공통 마스크로 바꾸는 이유: 부팅 정책(safe mode, 로그 포맷, 텔레메트리)은 칩이 바뀌어도 같아야 한다. 벤더 차이는 `*_to_rc()` 두 함수에 가둔다.

| 상황 | raw | rc | 주 원인 | 왜 이 순서 |
|---|---|---|---|---|
| ST POR | `0x0E000000` | POR\|BOR\|PIN | POR | POR 은 BOR 을 같이 켠다 → POR 이 이겨야 |
| ST 워치독 | `0x24000000` | IWDG\|PIN | IWDG | PIN 은 내부 리셋의 "메아리" → 가장 낮게 |
| ST 버튼 | `0x04000000` | PIN | PIN | 다른 게 없으면 진짜 핀 리셋 |
| nRF 전원 | `0` | POWER | POWER | 0 은 "온칩 리셋 생성기" |
| nRF LOCKUP+SREQ | `0x0C` | LOCKUP\|SW | LOCKUP | 결함(fault)이 의도된 리셋보다 중요 |

우선순위가 해결하는 것은 **한 번의 리셋에서 같이 켜진 플래그** 뿐이다. 부팅 코드가 지우지 않아 **여러 번의 리셋이 쌓인** 경우는 순서 정보가 이미 사라졌다:

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 리셋 시퀀스 POR → IWDG → SW, 부팅 코드가 지우나 안 지우나</text>
  <g stroke="currentColor"><line x1="150" y1="40" x2="730" y2="40"/></g>
  <g fill="currentColor" text-anchor="middle">
    <text x="250" y="36">POR</text><text x="440" y="36">IWDG</text><text x="630" y="36">SW</text>
  </g>
  <text x="10" y="80" fill="currentColor">안 지움</text>
  <text x="10" y="160" fill="currentColor">매번 지움</text>
  <g stroke="currentColor">
    <rect x="170" y="56" width="160" height="50" class="f-ok"/>
    <rect x="360" y="56" width="160" height="50" class="f-warn"/>
    <rect x="550" y="56" width="170" height="50" class="f-warn"/>
    <rect x="170" y="136" width="160" height="50" class="f-ok"/>
    <rect x="360" y="136" width="160" height="50" class="f-ok"/>
    <rect x="550" y="136" width="170" height="50" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="178" y="76">POR BOR PIN</text><text x="178" y="96">→ POR (맞음)</text>
    <text x="368" y="76">POR BOR PIN IWDG</text><text x="368" y="96">→ POR (틀림)</text>
    <text x="558" y="76">POR BOR PIN IWDG SFT</text><text x="558" y="96">→ POR (틀림)</text>
    <text x="178" y="156">POR BOR PIN → 지움</text><text x="178" y="176">→ POR</text>
    <text x="368" y="156">IWDG PIN → 지움</text><text x="368" y="176">→ IWDG</text>
    <text x="558" y="156">SFT PIN → 지움</text><text x="558" y="176">→ SW</text>
  </g>
  <text x="10" y="210" fill="currentColor" font-size="12">플래그 집합은 "어떤 리셋들이 있었나" 만 알려주고 "어느 게 마지막인가" 는 모른다 → 매 부팅 읽고·기록하고·지운다.</text>
</svg>
```

테스트에 박아 둔 세 함정:

1. **안 지움** — ST: POR → IWDG 후 디코드 결과 POR (실제 마지막 IWDG). nRF: IWDG → PIN 후 디코드 결과 IWDG (실제 마지막 PIN). 우선순위를 어떻게 바꿔도 둘 다를 맞출 수는 없다.
2. **부트로더가 먼저 지움** — 앱이 읽으면 ST 는 0 → NONE, nRF 는 0 → **POWER**. nRF 에서는 "워치독으로 죽은 보드" 가 "전원 껐다 켠 보드" 로 보고된다. 해결: 부트로더가 읽은 값을 `.noinit` 메일박스로 넘긴다 (추가 연습 2).
3. **쓰기 방식 혼동** — W1C 에 `RESETREAS |= DOG` 는 `write(read | DOG)` 라서 이미 1 인 RESETPIN 까지 다시 써서 지운다. ST 의 RO 플래그에 `CSR &= ~IWDGRSTF` 는 아무 효과가 없다 (컴파일·실행 모두 조용히 "성공"). W1C 일반론은 C-M02 참고.

### Part C — 부팅 코드

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b14b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">boot_reset_handler() — 순서가 곧 정답</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="160" height="50" class="f-acc"/>
    <rect x="195" y="40" width="160" height="50" class="f-mut"/>
    <rect x="380" y="40" width="170" height="50" class="f-mut"/>
    <rect x="575" y="40" width="155" height="50" class="f-mut"/>
    <rect x="10" y="130" width="160" height="50" class="f-mut"/>
    <rect x="195" y="130" width="160" height="50" class="f-warn"/>
    <rect x="380" y="130" width="350" height="50" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="60">① raw = REG (한 번)</text><text x="20" y="78">이후 전부 raw 사용</text>
    <text x="205" y="60">② rc = to_rc(raw)</text><text x="205" y="78">cause = primary(rc)</text>
    <text x="390" y="60">③ retained 유효?</text><text x="390" y="78">파워 리셋이면 무조건 초기화</text>
    <text x="585" y="60">④ seq++, streak</text><text x="585" y="78">WDT +1 · 파워/PIN 0</text>
    <text x="20" y="150">⑤ log[head] = {seq,</text><text x="20" y="168">raw, cause}, check 갱신</text>
    <text x="205" y="150">⑥ 클리어 (기록 뒤!)</text><text x="205" y="168">ST: |RMVF · nRF: =raw</text>
    <text x="390" y="150">⑦ streak ≥ 3 → SAFE (최소 기능 + 업데이트 경로)</text><text x="390" y="168">콜드 → COLD (전체 초기화) · 그 외 → WARM</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#b14b)">
    <line x1="170" y1="65" x2="193" y2="65"/><line x1="355" y1="65" x2="378" y2="65"/>
    <line x1="550" y1="65" x2="573" y2="65"/>
    <path d="M652,90 L652,110 L90,110 L90,128"/>
    <line x1="170" y1="155" x2="193" y2="155"/><line x1="355" y1="155" x2="378" y2="155"/>
  </g>
  <text x="10" y="215" fill="currentColor" font-size="12">⑥ 을 ⑤ 앞에 두면: 기록 중 또 리셋(예: 워치독)이 나면 원인이 레지스터에서도 RAM 에서도 사라진다.</text>
</svg>
```

단계별로 왜 그렇게 하나:

- **① 한 번만 읽기** — nRF 처럼 "읽은 비트만 지우기" 를 하려면 읽은 값이 필요하다. 두 번 읽으면 그 사이 값이 바뀌었을 때 로그와 클리어가 서로 다른 값을 쓴다.
- **③ 파워 리셋이면 무조건 초기화** — 파워업 SRAM 은 쓰레기인데, 쓰레기가 우연히 magic 과 check 를 통과할 확률이 0 은 아니다. "원인이 파워면 retained 를 믿지 않는다" 가 첫 번째 방어, magic + check 가 두 번째 방어(웜 리셋 중 떠돌이 쓰기로 깨진 경우). 실제 칩에서는 SRAM ECC 가 있는 칩이면 쓰레기 읽기가 ECC 오류를 낼 수도 있어서 데이터시트의 초기화 요구를 확인한다.
- **④ streak 규칙** — 워치독·LOCKUP 은 "소프트웨어가 멈췄다" 는 증거라 +1. 파워 리셋과 핀 리셋은 사람/환경 개입이라 0. SW 리셋은 OTA 후 재부팅일 수도, 크래시 핸들러의 재부팅일 수도 있어서 **유지** (구분은 추가 연습 4 의 크래시 마커로).
- **⑤ → ⑥ 순서** — 기록을 retained 에 완전히 쓰고 check 까지 갱신한 뒤에 지운다.
- **⑦ 결정** — SAFE 는 "앱을 다시 돌리면 또 죽는다" 는 판단: 최소 기능 + 업데이트 경로만 켠다. 시간 창(N 분 안에 M 번) 기반의 더 정교한 부트 루프 복구는 PLAT-A06, 리셋 후 크래시 덤프 보존은 BM-A09, 부팅 이미지 선택은 AERO-M10 이 다룬다.

솔루션 실행 결과 (실제 출력, 시나리오 트레이스 포함):

```text
[S1] ST 스타일 (pin_echo): 전원 → 워치독 3연속 → safe mode → 핀 리셋
   POR     raw=0x0E000001  cause=POR     mode=COLD streak=0 seq=1  after=0x00000001 
   IWDG    raw=0x24000001  cause=IWDG    mode=WARM streak=1 seq=2  after=0x00000001 
   IWDG    raw=0x24000001  cause=IWDG    mode=WARM streak=2 seq=3  after=0x00000001 
   IWDG    raw=0x24000001  cause=IWDG    mode=SAFE streak=3 seq=4  after=0x00000001 
   PIN     raw=0x04000001  cause=PIN     mode=WARM streak=0 seq=5  after=0x00000001 
   SW      raw=0x14000001  cause=SW      mode=WARM streak=0 seq=6  after=0x00000001 
[S2] ST: healthy 표시가 streak 를 끊는다 / BOR 은 콜드
   POR     raw=0x0E000001  cause=POR     mode=COLD streak=0 seq=1  after=0x00000001 
   IWDG    raw=0x24000001  cause=IWDG    mode=WARM streak=1 seq=2  after=0x00000001 
   WWDG    raw=0x44000001  cause=WWDG    mode=WARM streak=2 seq=3  after=0x00000001 
   -- app_mark_healthy()
   IWDG    raw=0x24000001  cause=IWDG    mode=WARM streak=1 seq=4  after=0x00000001 
   SW      raw=0x14000001  cause=SW      mode=WARM streak=1 seq=5  after=0x00000001 
   IWDG    raw=0x24000001  cause=IWDG    mode=WARM streak=2 seq=6  after=0x00000001 
   BOR     raw=0x06000001  cause=BOR     mode=COLD streak=0 seq=1  after=0x00000001 
[S3] nRF 스타일: 0 = 전원, DOG 3연속 safe, LOCKUP, System OFF 깨어남
   POR     raw=0x00000000  cause=POWER   mode=COLD streak=0 seq=1  after=0x00000000 
   IWDG    raw=0x00000002  cause=IWDG    mode=WARM streak=1 seq=2  after=0x00000000 
   LOCKUP  raw=0x00000008  cause=LOCKUP  mode=WARM streak=2 seq=3  after=0x00000000 
   IWDG    raw=0x00000002  cause=IWDG    mode=SAFE streak=3 seq=4  after=0x00000000 
   IWDG    raw=0x00000002  cause=IWDG    mode=SAFE streak=4 seq=5  after=0x00000000 
   PIN     raw=0x00000001  cause=PIN     mode=WARM streak=0 seq=6  after=0x00000000 
   WAKEOFF raw=0x00010000  cause=WAKEOFF mode=WARM streak=0 seq=7  after=0x00000000 
   BOR     raw=0x00000000  cause=POWER   mode=COLD streak=0 seq=1  after=0x00000000 
45/45 passed
```

읽는 법: ST 시나리오의 raw 끝자리 `1` 은 부트로더가 IWDG 용으로 켠 LSION (하네스가 리셋 직후 켠다). `after=0x00000001` = 플래그는 지워지고 LSION 은 살아 있다 — `CSR = RMVF` 처럼 통째로 써 버렸다면 LSI 가 꺼졌을 것이다. 무작위 테스트 `fuzz()` 는 벤더마다 2만 단계(10% 는 healthy 표시)를 돌리며 주 원인·모드·streak·seq·클리어 여부·로그 마지막 칸을 **하네스 안의 독립 오라클** 과 비교한다.

### 실제 Cortex-M 에서는 — demo_cm4_asm.c

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cm4_asm.c -o -
```

실제 출력 (지시어 줄은 걸러냄):

```text
st_read_and_clear:                      @ @st_read_and_clear
	movw	r1, #14452
	movt	r1, #16386
	ldr	r0, [r1]
	orr	r2, r0, #16777216
	str	r2, [r1]
	bx	lr
nrf_read_and_clear:                     @ @nrf_read_and_clear
	movw	r1, #1024
	movt	r1, #16384
	ldr	r0, [r1]
	str	r0, [r1]
	bx	lr
nrf_clear_dog_wrong:                    @ @nrf_clear_dog_wrong
	movw	r0, #1024
	movt	r0, #16384
	ldr	r1, [r0]
	orr	r1, r1, #2
	str	r1, [r0]
	bx	lr
nrf_clear_dog_right:                    @ @nrf_clear_dog_right
	movw	r0, #1024
	movt	r0, #16384
	movs	r1, #2
	str	r1, [r0]
	bx	lr
system_reset:                           @ @system_reset
	movw	r0, #60684
	dsb	sy
	movt	r0, #57344
	ldr	r1, [r0]
	movs	r2, #4
	and	r1, r1, #1792
	movt	r2, #1530
	orrs	r1, r2
	str	r1, [r0]
	dsb	sy
.LBB4_1:                                @ =>This Inner Loop Header: Depth=1
	nop
	b	.LBB4_1
```

- `#14452 / #16386` = `0x3874 / 0x4002` → `0x4002_3874` (e.g. STM32F4 RCC_CSR). `#16777216` = `1<<24` = RMVF. 읽은 값에 OR 해서 쓰는 RMW 지만 플래그는 RO 라 안전하고, LSION 을 보존한다.
- `nrf_clear_dog_wrong` 은 `ldr / orr #2 / str` — 읽은 1 들을 전부 다시 쓴다. `right` 는 `movs #2 / str` 한 번. 기계어 수준에서 버그가 그대로 보인다.
- `system_reset`: `0xE000_ED0C` (AIRCR) 에 `0x05FA_0004` (VECTKEY `0x05FA` << 16 | SYSRESETREQ bit 2) 와 PRIGROUP(`#1792` = `0x700`, bit 10:8) 보존. 앞뒤 `dsb sy` 와 끝의 무한 루프는 CMSIS `NVIC_SystemReset()` 과 같은 순서다 — 쓰기가 끝나기 전에 다음 코드가 실행되지 않게, 리셋이 실제로 걸릴 때까지 아무 일도 안 하게. 첫 `dsb` 가 `movw` 뒤로 간 것은 메모리 접근이 아닌 명령이라 컴파일러가 옮긴 것이고 의미는 같다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_ARCH-B14/so && /tmp/esn_ARCH-B14/so   # 45/45 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_ARCH-B14/san && /tmp/esn_ARCH-B14/san
# ASan/UBSan 경고 없음, 45/45 passed, exit 0
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 리셋 원인을 읽기만 하고 안 지움 | sticky 라 다음 부팅에 옛 플래그가 남아 우선순위가 틀린 답을 낸다 (Part B 함정 1) | 읽고 → 기록 → `CSR = CSR \| RMVF` / `RESETREAS = raw` |
| 부트로더와 앱이 둘 다 읽고 지움 | 먼저 지운 쪽만 안다. nRF 앱은 0 을 보고 POR 로 오판 | 한 곳(부트로더)에서 읽고 `.noinit` 메일박스로 전달 (연습 2) |
| `RESETREAS \|= DOG` 로 한 비트만 지우려 함 | W1C 에 RMW → 읽은 1 이 전부 다시 쓰여 다 지워짐 | `RESETREAS = DOG;` |
| `RCC->CSR &= ~RCC_CSR_IWDGRSTF` | 플래그는 RO — 아무 일도 안 하고 에러도 없음 | `RCC->CSR \|= RCC_CSR_RMVF;` (전체 클리어만 가능) |
| `RCC->CSR = RCC_CSR_RMVF` 로 클리어 | 같은 레지스터의 LSION 같은 설정 비트를 0 으로 만든다 (IWDG 의 클럭이 LSI) | `\|=` 로 RMVF 만 추가 |
| `if (csr & PINRSTF) → "버튼 리셋"` 을 먼저 검사 | 내부 리셋도 PIN 을 같이 켜는 칩에서 워치독이 전부 "버튼" 으로 보인다 | PIN 은 우선순위 **맨 끝** |
| POR 인데 `.noinit` 카운터를 그대로 믿음 | 파워업 SRAM 은 쓰레기, magic 이 우연히 맞을 수 있다 | 원인이 파워면 무조건 초기화 + magic/check |
| 매 부팅 플래시에 리셋 카운터 저장 | 부트 루프 하나로 수천 번 지우기·쓰기 → 플래시 수명 소모, 쓰다가 또 리셋 | `.noinit` RAM / 백업 레지스터, 플래시엔 요약만 가끔 |

## 8. 면접 Q&A

<details>
<summary>Q1. 리셋 종류를 말하고, 각각 무엇이 초기화되는지 설명해 보세요.</summary>

파워 리셋(POR/PDR, BOR)은 칩 전체를 초기화하고 리셋 원인 레지스터도 새로 시작한다 — SRAM 내용은 보장 없음. 시스템 리셋(NRST 핀, 워치독, SYSRESETREQ, LOCKUP 리셋, 저전력 리셋)은 CPU 와 주변장치 레지스터를 리셋값으로 되돌리지만 리셋 원인 플래그, (VBAT 가 있으면) 백업 도메인, 그리고 실제로는 대부분 SRAM 내용이 남는다. **꼬리질문: "SRAM 이 웜 리셋에서 남는 건 보장인가요?"** → 아키텍처가 보장하는 건 아니고 칩·설정에 달렸다 (System OFF 의 RAM retention 설정, ECC SRAM 초기화 요구 등). 그래서 `.noinit` 데이터는 항상 magic + CRC 로 검증한다.

</details>

<details>
<summary>Q2. 리셋 원인 레지스터는 왜 sticky 인가요? 언제, 누가 지워야 하나요?</summary>

리셋 순간 CPU 는 아무것도 못 하므로 하드웨어가 "리셋에도 지워지지 않는 곳" 에 원인을 남겨야 다음 부팅이 읽을 수 있다. 같은 이유로 소프트웨어가 지우지 않으면 계속 쌓인다. 부팅 초기에 **한 곳에서** 한 번 읽어 RAM 변수/`.noinit`/로그에 기록한 뒤 지운다. **꼬리질문: "부트로더가 있으면?"** → 부트로더가 읽고 지우고, 값을 `.noinit` 메일박스(magic + 값 + ~값)로 앱에 넘긴다. 앱은 메일박스가 무효일 때만 하드웨어 레지스터로 대체한다 (연습 2).

</details>

<details>
<summary>Q3. 워치독 리셋 후에 IWDG 플래그와 PIN 플래그가 둘 다 켜져 있습니다. 왜죠?</summary>

많은 STM32 계열은 내부 리셋 펄스를 NRST 핀으로 내보낸다 (외부 부품도 같이 리셋되게). 그 펄스를 핀 리셋 감지 회로가 다시 보니 PINRSTF 가 같이 켜진다. 그래서 디코더는 PIN 을 **가장 낮은 우선순위** 로 두고 "PIN 만 있을 때만 진짜 핀 리셋" 으로 본다. **꼬리질문: "디버거로 리셋하면 무엇이 켜지나요?"** → 프로브 설정에 따라 다르다. NRST 를 당기면 PIN, SYSRESETREQ 를 쓰면 SW 로 보인다. 그래서 랩에서 디버거로 리셋한 로그를 현장 통계와 섞지 않는다.

</details>

<details>
<summary>Q4. nRF52 에서 RESETREAS 가 0 이면 무엇을 뜻하나요? POR 과 BOR 을 구분할 수 있나요?</summary>

0 은 "기록된 소스 없음 = 온칩 리셋 생성기", 즉 전원 인가 또는 브라운아웃이다. 이 레지스터만으로는 둘을 구분할 수 없다. 함정은 앞 단계(부트로더, 이전 부팅)가 이미 지운 경우에도 0 이라 워치독 리셋이 전원 인가로 보고된다는 점이다. **꼬리질문: "그래도 브라운아웃을 구분하고 싶다면?"** → 전원 실패 비교기(power-fail comparator) 경고 이벤트에서 `.noinit` 에 표시를 남기거나, 외부 전압 감시 IC 의 출력을 따로 읽는 식으로 별도 증거를 만든다. 정확한 주변장치 이름과 동작은 칩 데이터시트에서 확인한다.

</details>

<details>
<summary>Q5. 부트 루프(boot loop)를 어떻게 감지하고 빠져나오나요? 카운터는 어디에 두나요?</summary>

리셋 원인이 워치독/LOCKUP/크래시면 `.noinit` (또는 백업 레지스터)의 연속 카운터를 올리고, N 번(예: 3) 넘으면 safe mode(최소 기능 + 업데이트/복구 경로)로 부팅한다. 카운터는 앱이 "정상 동작 확인"(일정 시간 무사 + 통신 OK)을 찍을 때 0 으로 — MCUboot 의 이미지 confirm 과 같은 발상이다. **꼬리질문 1: "카운터를 플래시에 두면?"** → 부트 루프 동안 매번 지우기/쓰기 → 수명 소모, 쓰는 중에 또 리셋. **꼬리질문 2: "크래시가 retained RAM 을 망가뜨리면?"** → check 실패로 콜드 취급되어 카운터가 매번 0 이 되고 safe mode 에 영영 못 간다 — 이 레슨의 retained 손상 테스트가 그 상황. 대책은 카운터를 두 벌(값 + 반전값) 두거나 별도 백업 레지스터에 두고, 연속 대신 최근 8 부팅 히스토리(연습 1)나 시간 창(PLAT-A06)을 쓰는 것.

</details>

<details>
<summary>Q6. Cortex-M 에서 소프트웨어 리셋은 어떻게 거나요? 왜 DSB 와 무한 루프가 붙나요?</summary>

`SCB->AIRCR = (0x05FA << 16) | (AIRCR & PRIGROUP_Msk) | SYSRESETREQ;` — 상위 16 비트에 키 `0x05FA` 가 없으면 쓰기가 무시되고, PRIGROUP 은 보존한다. 앞의 DSB 는 그때까지의 메모리 쓰기(예: `.noinit` 에 남긴 크래시 기록)를 끝내고, 뒤의 DSB 는 AIRCR 쓰기 자체를 완료시킨다. 리셋 요청에서 실제 리셋까지 몇 사이클이 걸릴 수 있어 그동안 다음 코드가 돌지 않도록 무한 루프로 막는다 (demo 의 asm 참고). **꼬리질문: "SYSRESETREQ 가 리셋하는 범위는?"** → 요청은 코어가 내지만 무엇을 리셋할지는 칩의 리셋 컨트롤러가 정한다. 보통 디버그 로직을 뺀 시스템 리셋이다.

</details>

<details>
<summary>Q7. 수천 대 필드 장비에서 리셋 원인 데이터를 어떻게 활용하나요?</summary>

부팅마다 `{원인, raw 값, 부팅 번호, 펌웨어 버전, 업타임}` 을 텔레메트리로 올려 버전별 원인 히스토그램을 만든다. 새 버전에서 워치독·LOCKUP 비율이 오르면 회귀다 (INFRA-A06 의 통계 검정). raw 값을 같이 올리는 이유: 디코더 버그나 "PIN 메아리" 같은 칩 특성을 나중에 다시 해석할 수 있게. **꼬리질문: "BOR 이 특정 지역에서 많다면?"** → 펌웨어보다 전원(배터리 노화, 저온, 부하 순간 전류)을 먼저 의심하고 BOR 임계 설정과 전원 설계를 본다.

</details>

## 9. 추가 연습

### 연습 1 — 최근 8 부팅 히스토리로 부트 루프 감지

"정상 부팅 → 몇 분 뒤 워치독" 을 반복하는 보드는 매번 healthy 를 찍으므로 연속 streak 가 1 을 넘지 못한다. `uint8_t` 히스토리 비트맵(bit0 = 가장 최근, 1 = 비정상)으로 "최근 8 부팅 중 3 번 이상 비정상" 을 감지하라.

<details>
<summary>답 보기</summary>

`hist_push(h, ab) = (h << 1) | ab` (8 비트로 잘림 = 가장 오래된 부팅이 빠짐), `hist_safe(h, k) = popcount(h) >= k`. popcount 는 `h &= h - 1` 반복 (C-B01). 테스트 패턴 `정상, WDT, 정상, WDT, 정상, WDT` 에서 streak 최대 1, 히스토리는 `0b010101` 이 되어 세 번째 WDT 에서 감지한다. 8 번 정상 부팅하면 창에서 빠져 safe mode 가 풀린다.

</details>

### 연습 2 — 부트로더 → 앱 리셋 원인 핸드오프

부트로더가 레지스터를 읽고 지운 뒤 `.noinit` 메일박스 `{magic, raw, ~raw}` 로 넘긴다. 앱의 `app_take_cause(mb, hw_reg, &out)` 은 메일박스가 유효하면 그 값을 쓰고 무효화(한 번만 소비), 아니면 하드웨어 값을 쓴다.

<details>
<summary>답 보기</summary>

쓰기는 `raw`, `inv` 를 먼저 쓰고 **magic 을 마지막에** — 중간에 리셋이 나도 반쯤 쓴 메일박스가 유효로 보이지 않게. 검사는 `magic == M && inv == ~raw`. 읽은 뒤 `magic = 0` 으로 소비해야 다음 부팅에서 옛 값을 다시 쓰지 않는다. 파워업 쓰레기(0x5A 반복)와 비트 하나 깨진 경우는 하드웨어 값으로 대체되는 것을 테스트한다.

</details>

### 연습 3 — CSR → 사람이 읽는 문자열

`st_flags_str(csr, buf, n)` 이 우선순위 순서로 `"POR BOR PIN"` 같은 문자열을 만들고 `snprintf` 처럼 **필요한 길이** 를 돌려주게 하라 (버퍼가 작으면 잘라도 항상 NUL 종료).

<details>
<summary>답 보기</summary>

`{bit, name}` 표를 우선순위 순서로 두고 켜진 것만 `snprintf(buf + len, n - len, "%s%s", len ? " " : "", name)`. `len >= n` 이 되면 `snprintf(NULL, 0, ...)` 으로 길이만 센다 (C11 7.21.6.5: n == 0 이면 아무것도 안 쓰고 길이만 돌려준다). `0x0E000000` → `"POR BOR PIN"` (11), 버퍼 4 → `"POR"`, 버퍼 6 → `"POR B"`.

</details>

### 연습 4 — 크래시 마커로 SW 리셋 나누기

HardFault 핸들러가 `.noinit` 에 `{CRASH_MAGIC, pc}` 를 쓰고 SYSRESETREQ 로 리셋한다. 부팅 때 원인이 SW 이고 마커가 있으면 CRASH(그리고 PC 보고), 마커가 없으면 의도한 리셋(OTA 등)으로 분류하라. 파워 리셋이면 마커를 믿지 않는다.

<details>
<summary>답 보기</summary>

마커는 **어떤 경우든 읽자마자 소비**(0)한다 — 안 그러면 다음 의도한 SW 리셋이 또 CRASH 로 보인다. 원인이 POR/BOR/POWER 면 RAM 이 쓰레기라 마커를 무시하고 COLD. 이렇게 하면 Part C 의 streak 규칙에서 "SW 는 유지" 대신 "CRASH 는 +1, INTENTIONAL 은 유지" 로 더 정확해진다. 크래시 기록 전체(레지스터, 스택 일부, CRC)는 BM-A09 참고.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_ARCH-B14/pr && /tmp/esn_ARCH-B14/pr   # 22/22 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o /tmp/esn_ARCH-B14/sanp && /tmp/esn_ARCH-B14/sanp   # 22/22, 경고 없음
```

## 10. 핵심 정리

- 리셋 = 리셋 컨트롤러가 여러 소스를 OR. 파워 리셋(POR/BOR)은 전부, 시스템 리셋(핀·WDT·SW·LOCKUP)은 CPU·주변장치만 — 원인 플래그와 대개 SRAM 은 남는다.
- 원인 레지스터는 **sticky**: 부팅 때 한 곳에서 **한 번 읽고 → 기록 → 지운다**. 안 지우면 쌓여서 "마지막 원인" 을 잃는다.
- 클리어 방식 두 가지: ST 스타일 `CSR |= RMVF` (플래그 RO, 전체만 지움, 설정 비트 보존), nRF 스타일 `REG = raw` (W1C, `|=` 금지).
- 한 리셋에 여러 플래그: POR → +BOR (+PIN), 내부 리셋 → +PIN. 우선순위 POR > BOR > … > PIN, 구현은 우선순위 인코더(가장 낮은 1 비트).
- nRF `RESETREAS == 0` = POR 또는 BOR (구분 불가) — 누가 먼저 지웠을 때도 0.
- `.noinit` 기록은 magic + check, 파워 리셋이면 무조건 초기화. 연속 WDT/LOCKUP N 회 → safe mode, healthy 표시로 리셋.
- SW 리셋: `AIRCR = 0x05FA<<16 | PRIGROUP 보존 | SYSRESETREQ`, 앞뒤 DSB + 무한 루프.

## 참고자료

- [STM32F4 Reference Manual RM0090 — Reset (power reset / system reset), RCC_CSR](https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- Nordic Semiconductor, *nRF52832 Product Specification* — POWER: Reset behavior, RESETREAS 레지스터
- [Armv7-M Architecture Reference Manual (DDI 0403) — AIRCR, SYSRESETREQ, Lockup](https://developer.arm.com/documentation/ddi0403/latest)
- [ARM CMSIS-Core — NVIC_SystemReset()](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
- MCUboot 문서 — 이미지 confirm / revert (부트 루프에서 이전 이미지로 돌아가는 설계)
- 관련 레슨: ARCH-B05 (우선순위 인코더), ARCH-B11 (메모리 맵·버스 폴트), C-M02 (RMW/W1C), BM-M10 (워치독 타임아웃), BM-A09 (크래시 덤프), PLAT-A06 (부트 루프 복구), SIV-B03 (리셋값 테스트)
