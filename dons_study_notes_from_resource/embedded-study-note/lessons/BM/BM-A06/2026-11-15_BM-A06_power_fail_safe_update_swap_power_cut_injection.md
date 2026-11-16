---
id: BM-A06
time: 120분
updated: 2026-11-15
---
# BM-A06 전원 차단 내성 업데이트: 모든 쓰기 지점 power-cut 주입

## 학습 목표

- "가장 새 유효 사본은 절대 건드리지 않는다" 는 불변식 하나로 **이중 메타데이터(시퀀스 번호 + CRC)** 가 어떤 시점의 전원 차단에도 "옛 상태 또는 새 상태" 만 남기는 이유를 증명하고 구현할 수 있다.
- MCUboot 식 **scratch 스왑**(섹터마다 scratch ← secondary, secondary ← primary, primary ← scratch)의 각 단계가 멱등(idempotent)이라 재개(resume)가 안전한 이유와, "데이터 먼저, 상태 나중" 규칙을 말할 수 있다.
- 플래시 모델의 `cut_at` 훅으로 **모든 erase/program 호출에서** 전원을 끊는 전수 테스트(단일 차단 × 3 모드 × 시드, 이중 차단 전부, 무작위 다중 차단)를 짜고, "항상 유효 이미지(옛것 또는 새것)로 부팅" 을 판정할 수 있다.
- test → confirm → (안 하면) revert 수명주기를 구현하고, 차단이 시험 부팅 창에 걸리면 왜 되돌림이 정상인지 설명할 수 있다.
- 업데이트 한 번의 섹터별 erase 횟수·시간·수명을 계산해 **메타데이터와 scratch 가 마모 핫스팟** 임을 숫자로 보이고, append-only 로그 메타로 줄일 수 있다.

## 1. 문제

"업데이트 중 전원이 나가면?" 은 펌웨어 면접의 단골이고, 정답은 말이 아니라 **증명** 이다. 드론·차량 ECU·로봇은 현장에서 배터리가 빠지거나 브라운아웃이 오는 순간에도 업데이트를 하고 있을 수 있다. 이때 "벽돌(brick)" 이 되면 리콜이다. Don 이 SSD 펌웨어에서 했던 전원 손실 보호(PLP)와 FTL 메타데이터 체크포인트 — "시퀀스 번호가 가장 큰 유효 체크포인트를 찾아 거기서 재생" — 가 MCU 부트로더에서는 그대로 이 레슨의 설계가 된다.

이 레슨은 맥에서 돈다. 진짜 칩 대신 **결정적(deterministic) 플래시 모델**(BM-A02 플래시 모델 축약판)을 쓴다: erase = 섹터 전체 0xFF, program = AND(1→0 만), 그리고 `op_index == cut_at` 인 op 에서 전원이 끊긴다. 끊긴 op 는 세 가지 모양 중 하나다 — **BEFORE**(op 가 아예 안 일어남), **AFTER**(op 는 끝났지만 CPU 는 결과를 못 봄), **TORN**(찢김: erase 는 바이트마다 0xFF/옛값/무작위, program 은 앞쪽 unit 까지 완료 + 끊긴 unit 은 1→0 전이의 무작위 일부). 끊긴 뒤에는 `flash_power_on()`(재부팅) 전까지 모든 op 가 `FLASH_POWER_CUT` 이다.

설계는 브리프의 두 선택지 중 **(b) 이중 메타데이터 + 시퀀스 번호 + CRC** 가 **scratch 스왑** 을 구동하는 방식으로 골랐다. 이유: ① "최신 유효 사본이 이긴다" 는 규칙 하나로 메타데이터 자체의 원자성(atomicity)을 증명하기 쉽고, ② 상태 레코드가 32 B 하나라 CRC 로 찢김을 확실히 거른다, ③ SSD FTL 체크포인트와 같은 사고방식이라 면접에서 경험과 연결된다. MCUboot 의 진짜 방식(슬롯 끝 trailer 에 섹터마다 한 번만 쓰는 상태 바이트)은 (a) 이고, 5. 풀이와 면접 Q&A 에서 비교한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">플래시 레이아웃 (이 레슨의 모델: 256 B 섹터 × 12, base 0x08000000)</text>
  <rect x="10" y="32" width="50" height="40" class="f-mut" stroke="currentColor"/>
  <text x="16" y="50" fill="currentColor">S0</text><text x="16" y="65" fill="currentColor">boot</text>
  <rect x="60" y="32" width="50" height="40" class="f-warn" stroke="currentColor"/>
  <text x="66" y="50" fill="currentColor">S1</text><text x="66" y="65" fill="currentColor">meta0</text>
  <rect x="110" y="32" width="50" height="40" class="f-warn" stroke="currentColor"/>
  <text x="116" y="50" fill="currentColor">S2</text><text x="116" y="65" fill="currentColor">meta1</text>
  <rect x="160" y="32" width="220" height="40" class="f-ok" stroke="currentColor"/>
  <text x="168" y="50" fill="currentColor">S3–S6 primary (실행 슬롯)</text><text x="168" y="65" fill="currentColor">헤더 64 B @0x08000300</text>
  <rect x="380" y="32" width="220" height="40" class="f-acc" stroke="currentColor"/>
  <text x="388" y="50" fill="currentColor">S7–S10 secondary</text><text x="388" y="65" fill="currentColor">새 이미지를 내려받는 곳</text>
  <rect x="600" y="32" width="60" height="40" class="f-mut" stroke="currentColor"/>
  <text x="606" y="50" fill="currentColor">S11</text><text x="606" y="65" fill="currentColor">scratch</text>
  <text x="10" y="100" fill="currentColor" font-weight="bold">섹터 i 하나의 스왑 = 3 단계 (i = 3 → 0, 큰 인덱스부터)</text>
  <rect x="40" y="120" width="140" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="50" y="140" fill="currentColor">primary[i]</text><text x="50" y="156" fill="currentColor">옛 이미지 A 조각</text>
  <rect x="300" y="120" width="140" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="310" y="140" fill="currentColor">secondary[i]</text><text x="310" y="156" fill="currentColor">새 이미지 B 조각</text>
  <rect x="560" y="120" width="140" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="570" y="140" fill="currentColor">scratch</text><text x="570" y="156" fill="currentColor">임시 보관</text>
  <path d="M440 132 L560 132" stroke="currentColor" stroke-width="2"/><path d="M552 127 L560 132 L552 137" fill="none" stroke="currentColor"/>
  <text x="458" y="126" fill="currentColor">0: B 조각 →</text>
  <path d="M180 142 L300 142" stroke="currentColor" stroke-width="2"/><path d="M292 137 L300 142 L292 147" fill="none" stroke="currentColor"/>
  <text x="198" y="136" fill="currentColor">1: A 조각 →</text>
  <path d="M630 164 L630 200 L110 200 L110 164" fill="none" stroke="currentColor" stroke-width="2"/>
  <path d="M105 172 L110 164 L115 172" fill="none" stroke="currentColor"/>
  <text x="300" y="194" fill="currentColor">2: scratch(B 조각) → primary[i]</text>
  <text x="10" y="232" fill="currentColor">각 단계 = 대상 erase 1 + program 64 B × 4 + read-back 검증, 그 다음 메타 commit (erase 1 + program 32 B)</text>
  <text x="10" y="252" fill="currentColor">단계 k 의 원본은 단계 k+1 이 시작되기 전까지 멀쩡하다 → 단계 k 는 몇 번이고 다시 해도 같은 결과 (멱등)</text>
  <rect x="10" y="266" width="720" height="54" rx="6" fill="none" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="20" y="286" fill="currentColor">메타 사본 두 개: 새 레코드는 항상 "가장 새 유효 사본이 아닌 쪽" 을 erase 한 뒤 쓴다.</text>
  <text x="20" y="306" fill="currentColor">→ 어느 op 에서 끊겨도 직전 상태의 사본 하나는 손대지 않은 채 남는다 (seq 비교는 wrap-safe).</text>
</svg>
```

### Part A — 메타데이터와 스왑 한 단계

- `int meta_load(const flash_t *f, meta_t *out)` — 두 사본 중 `meta_valid`(magic + CRC-32 + 필드 범위) 인 것 중 `seq` 가 더 새것. 둘 다 무효면 −1. seq 비교는 `(int32_t)(a − b) > 0` (0xFFFFFFFF 다음 0 이 더 새것).
- `flash_rc_t meta_commit(flash_t *f, meta_t *m)` — seq = 최신 + 1, CRC 계산, **최신이 아닌 쪽** 사본을 erase → program → read-back 검증.
- `flash_rc_t swap_step(flash_t *f, uint32_t idx, uint32_t step)` — 0: scratch ← secondary[idx], 1: secondary[idx] ← primary[idx], 2: primary[idx] ← scratch. 멱등 테스트(같은 단계 두 번 → 플래시 바이트 동일).

### Part B — 재개 상태 기계와 전수 차단 주입

- `flash_rc_t swap_resume(flash_t *f, meta_t *m)` — 메타가 가리키는 (섹터, 단계)부터 끝까지. **데이터를 다 쓰고 검증한 뒤** 다음 위치를 commit. 마지막 단계 뒤 최종 레코드: UPGRADE → `ST_TEST`, REVERT → `ST_IDLE + FL_REVERTED`.
- `boot_rc_t bl_boot(flash_t *f, boot_info_t *bi)` — 매 부팅: 메타 읽기 → `ST_TEST` 면 revert 요청 commit → `ST_SWAP` 이면 (아무것도 안 옮긴 시점에만) secondary 이미지 검증 후 재개 → primary 검증(BM-A04 헤더 축약판, CRC) → 점프.
- 주입·판정: 수명주기(스테이징 → 스왑 → 시험 부팅 → confirm/안 함 → …)의 **모든 op** 에서 BEFORE / AFTER / TORN(시드 4 개) 단일 차단, **이중 차단 전수**(첫 차단 k1 × 복구 중 두 번째 차단 k2 > k1), 1~5 번 무작위 차단 3000 개. 판정: 모든 성공 부팅의 primary 가 A 또는 B 와 바이트 단위로 같고 헤더 검증 통과, 유한 횟수 안에 "새 이미지 확정" 또는 "깨끗한 되돌림" 으로 끝남.
- 이름 붙은 고장: 검증 실패한 secondary → 요청 거절(primary erase 0 번), 진행 중 스왑은 secondary 를 검증하지 않고 재개, erase 없이 덮어쓴 메타(AND) → 무효, commit 하나의 op 1/2 를 3 모드 × 16 시드로 끊기 → 옛/새 상태만.

### Part C — test/confirm/revert 와 비용

- `flash_rc_t app_confirm(flash_t *f)` — `ST_TEST` 일 때만 `ST_IDLE + FL_CONFIRMED` commit (MCUboot 의 `boot_set_confirmed()` 역할 — 이름은 문서 확인 권장).
- `void cost_estimate(erase_cnt[], prog_bytes[], n, timing, cost_t *)` — 총 erase, program 단위 수(섹터별 올림), 시간, 가장 뜨거운 섹터와 "수명까지 업데이트 횟수". 모델 결과를 STM32F4 급(128 KB 섹터, 대표값)으로 늘려 표로.
- `-v` 실행: op 지도 + 섹터별 erase 표 + 이중 차단이 걸린 부팅 로그(현장 엔지니어가 읽는 형태).

## 2. Problem (English)

> **Power-fail-safe firmware update on a NOR-flash MCU.**
>
> The flash has 12 uniform 256-byte sectors: boot, two metadata sectors, a 4-sector primary slot (executes), a 4-sector secondary slot (download target) and one scratch sector. Erase sets a sector to 0xFF; program can only clear bits (AND). A test hook can cut power at the N-th erase/program call: the op may not happen, may complete, or may be torn (random partial result); every later op fails until power-on.
>
> 1. `meta_load` / `meta_commit`: two 32-byte metadata copies `{magic, seq, state, dir, idx, step, flags, n_swaps, crc32}`. Load returns the valid copy with the newest sequence number (wrap-safe). Commit writes seq+1 to the copy that is *not* the newest valid one (erase, program, verify).
> 2. `swap_step(idx, step)`: scratch ← secondary[idx]; secondary[idx] ← primary[idx]; primary[idx] ← scratch. `swap_resume` runs the steps from the recorded position, committing progress *after* each step's data is verified.
> 3. `bl_boot`: resume any swap; on `TEST` (unconfirmed trial) start a revert swap; validate the secondary image before starting a fresh upgrade; boot the primary only if its header and CRC verify.
> 4. Prove: for a power cut at **every** flash op (and every pair of ops), every completed boot runs a valid image (old or new, never neither), and the update ends confirmed or cleanly reverted.
>
> Example: an uncut confirmed update takes 108 flash ops (22 staging, 84 swap, 2 confirm) and erases each metadata sector 7 times. A cut at op 51 (torn erase of secondary[2]) followed by a cut at op 60 still ends with v1.3.0 confirmed after 3 successful boots.
>
> Follow-ups: why must progress be written after the data? Why two metadata copies — why not one with a CRC? What if power fails between the end of the swap and the jump? Which sector wears out first?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "24/60 passed", exit 1 (플래시 모델·CRC·헤더 검증 같은 주어진 부분만 통과) — TODO 7곳을 채우면 60/60
```

TODO 는 7 곳: A1 `meta_load`, A2 `meta_commit`, A3 `swap_step`, B1 `swap_resume`, B2 `bl_boot`, C1 `app_confirm`, C2 `cost_estimate`. 플래시 모델(cut 훅 포함), CRC-32, 헤더 검증, `copy_sector`, `app_stage_update`(이미지를 secondary 에 쓰고 검증한 **뒤** 요청 commit), 수명주기 시뮬레이터와 판정 하네스는 주어진다. 스텁 상태에서도 수명주기 루프는 이벤트 24 개 상한이 있어 멈추지 않는다.

관찰용:

```bash
./solution -v                     # (solution 빌드 후) op 지도, 섹터별 erase 표, 이중 차단 부팅 로그
cc -std=c11 -Wall -Wextra -Werror -O2 demo_cutmap.c -o demo_cutmap && ./demo_cutmap   # op 마다 끊으면 결과 지도
```

## 4. 힌트

<details>
<summary>힌트 1 — A1/A2: "최신이 아닌 쪽" 은 어떻게 정하나</summary>

`meta_load` 가 돌려준 사본 번호 w 를 그대로 쓴다. w == 0 이면 1 에, w == 1 이면 0 에, w == −1(둘 다 무효)이면 아무 데나(0). 핵심은 "지금 이기는 사본" 을 erase 하지 않는 것 — erase 가 찢기면 그 사본이 사라지고, 다른 사본은 아직 안 쓰였으니 **둘 다 없는** 순간이 생긴다.

</details>

<details>
<summary>힌트 2 — A3: 단계 순서가 왜 scratch ← secondary 부터인가</summary>

단계 1 이 secondary[i] 를 지우기 전에 그 내용(B 조각)이 어딘가 살아 있어야 한다 → 단계 0 에서 scratch 로 대피. 단계 2 가 primary[i] 를 지우기 전에 A 조각은 단계 1 에서 이미 secondary[i] 로 갔다. 이렇게 "다음 단계가 지울 데이터는 이전 단계가 이미 복사해 둔" 순서면 각 단계의 원본은 그 단계가 끝날 때까지 손대지 않는다.

</details>

<details>
<summary>힌트 3 — B1: commit 의 위치</summary>

`swap_step` 이 OK 를 돌려준 **뒤에** 다음 (idx, step) 을 commit 한다. 반대로 하면(상태 먼저) 상태 commit 뒤·데이터 쓰기 전에 끊긴 경우 재부팅한 부트로더가 "이 단계는 끝났다" 고 믿고 건너뛴다 → primary 에 지워진 섹터. 마지막 단계(idx 0, step 2) 뒤의 commit 은 진행 레코드 대신 최종 레코드(TEST 또는 IDLE+REVERTED) 하나로 끝낸다.

</details>

<details>
<summary>힌트 4 — B2: secondary 검증은 언제만</summary>

`idx == 3 && step == 0` 이면 아직 아무것도 안 옮겼으므로 secondary 는 온전한 B(또는 revert 때 A)여야 한다 → `img_verify`. 그 뒤로는 secondary 가 A 조각과 B 조각이 섞인 상태라 CRC 가 틀리는 게 정상이다. 매번 검증하면 진행 중인 스왑을 "잘못된 이미지" 로 거절해 primary 를 반쯤 섞인 채로 둔다.

</details>

<details>
<summary>힌트 5 — C1/C2: 작은 것들</summary>

`app_confirm`: TEST 가 아니면 op 를 하나도 하지 않는다(테스트가 op_index 로 확인). `cost_estimate`: 섹터별로 `units = ceil(bytes / unit)` 를 따로 올림한 뒤 더하고, `time += erase_cnt·erase_us[s] + units·prog_unit_us`. hot 섹터는 "더 큰 것" 일 때만 바꿔 동률이면 번호 작은 쪽, erase 가 0 이면 수명 UINT32_MAX.

</details>

## 5. 풀이

### 5.1 불변식부터 — 메타데이터의 원자성

증명할 것: "commit 도중 어느 op 에서 끊겨도, 재부팅 후 `meta_load` 는 commit 전 레코드 X 또는 commit 하려던 레코드 Y 만 돌려준다."

commit 직전, X 는 사본 w 에 있고 유효하다. commit 은 사본 1−w 만 건드린다(erase 1 번 + program 1 번).

| 끊긴 op | 모드 | 사본 1−w 의 모습 | 사본 w | meta_load 결과 |
|---|---|---|---|---|
| erase | BEFORE | 옛 레코드(더 오래된 seq) 또는 쓰레기 | X | X (seq 가 더 새것) |
| erase | TORN | 0xFF/옛값/무작위 섞임 → CRC 실패(확률 2⁻³² 제외) | X | X |
| erase | AFTER | 0xFF → magic 없음 | X | X |
| program | BEFORE | 0xFF | X | X |
| program | TORN | 앞 unit 일부만 쓰임 → CRC 실패 | X | X |
| program | AFTER | Y 완성 | X | **Y** (seq+1) |

`torn meta commit: 96 cuts -> old state 80, new state 16, other 0` — 3 모드 × 16 시드 × op 2 개 = 96 번 끊었고, "새 상태" 16 번은 전부 AFTER 모드의 program 차단(op 는 끝났는데 CPU 가 결과를 못 본 경우)이다. 중요한 함의: **commit 이 FLASH_POWER_CUT 을 돌려줘도 그 commit 이 반영됐을 수 있다.** 그래서 재부팅 후의 결정은 반환값이 아니라 항상 플래시에 남은 상태로만 한다.

seq 비교가 wrap-safe 여야 하는 이유: 32비트 seq 는 사실상 안 넘치지만(commit 14 번/업데이트), 공장 테스트가 seq 를 크게 시작하거나 메타를 다른 장치에서 복사해 오면 넘칠 수 있다. `(int32_t)(a − b) > 0` 은 두 값의 거리가 2³¹ 미만이면 맞다 — BM-B13 의 wrap-safe 시간 비교와 같은 식. `a > b` 로 바꾼 돌연변이는 이름 붙은 wrap 테스트 3 개로만 잡히고 전수 차단 테스트로는 안 잡힌다(아래 돌연변이 표) — 전수 차단 테스트가 모든 버그를 잡는 건 아니다.

### 5.2 스왑 한 단계와 재개 — "데이터 먼저, 상태 나중"

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">메타 상태 기계 (레코드 하나 = 상태 하나, commit 이 화살표)</text>
  <rect x="20" y="40" width="130" height="50" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor">IDLE</text><text x="30" y="78" fill="currentColor">CONFIRMED (A)</text>
  <rect x="220" y="40" width="170" height="50" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="230" y="60" fill="currentColor">SWAP UPGRADE</text><text x="230" y="78" fill="currentColor">(idx,step) 3,0 … 0,2</text>
  <rect x="460" y="40" width="120" height="50" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="470" y="60" fill="currentColor">TEST</text><text x="470" y="78" fill="currentColor">B 시험 부팅</text>
  <rect x="620" y="40" width="110" height="50" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="630" y="60" fill="currentColor">IDLE</text><text x="630" y="78" fill="currentColor">CONFIRMED (B)</text>
  <rect x="400" y="170" width="170" height="50" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="410" y="190" fill="currentColor">SWAP REVERT</text><text x="410" y="208" fill="currentColor">같은 3 단계, 거꾸로 내용</text>
  <rect x="120" y="170" width="170" height="50" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="130" y="190" fill="currentColor">IDLE</text><text x="130" y="208" fill="currentColor">REVERTED (A)</text>
  <rect x="20" y="240" width="160" height="44" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="30" y="258" fill="currentColor">IDLE REJECTED</text><text x="30" y="275" fill="currentColor">secondary 검증 실패</text>
  <path d="M150 65 L220 65" stroke="currentColor"/><path d="M212 60 L220 65 L212 70" fill="none" stroke="currentColor"/>
  <text x="152" y="58" fill="currentColor">앱: 요청</text>
  <path d="M390 65 L460 65" stroke="currentColor"/><path d="M452 60 L460 65 L452 70" fill="none" stroke="currentColor"/>
  <text x="394" y="58" fill="currentColor">BL: 끝</text>
  <path d="M580 65 L620 65" stroke="currentColor"/><path d="M612 60 L620 65 L612 70" fill="none" stroke="currentColor"/>
  <text x="572" y="34" fill="currentColor">앱: confirm</text>
  <path d="M520 90 L500 170" stroke="currentColor"/><path d="M496 161 L500 170 L507 163" fill="none" stroke="currentColor"/>
  <text x="515" y="140" fill="currentColor">BL: TEST 인 채 재부팅</text>
  <path d="M400 195 L290 195" stroke="currentColor"/><path d="M298 190 L290 195 L298 200" fill="none" stroke="currentColor"/>
  <text x="310" y="188" fill="currentColor">BL: 끝</text>
  <path d="M260 90 L120 240" stroke="currentColor" stroke-dasharray="4 3"/><path d="M122 230 L120 240 L129 235" fill="none" stroke="currentColor"/>
  <text x="140" y="130" fill="currentColor">BL: (3,0) 에서 검증 실패</text>
  <path d="M305 40 C 330 10, 280 10, 290 40" fill="none" stroke="currentColor"/>
  <text x="330" y="30" fill="currentColor">단계마다 commit (12 번)</text>
</svg>
```

재개가 안전한 이유를 단계별로 쓰면(메타가 (i, s) 를 가리킬 때 = 단계 s 는 아직 완료가 기록되지 않음):

| 메타가 가리키는 곳 | 그 단계의 원본 | 원본이 멀쩡한 이유 | 다시 하면 |
|---|---|---|---|
| (i, 0) | secondary[i] | 단계 1 (secondary[i] erase) 은 (i,1) 이 commit 된 뒤에야 시작 | scratch 를 다시 지우고 다시 복사 — 같은 결과 |
| (i, 1) | primary[i] | 단계 2 (primary[i] erase) 는 (i,2) commit 뒤 | secondary[i] 다시 |
| (i, 2) | scratch | 다음 섹터의 단계 0 (scratch erase) 은 (i−1,0) commit 뒤 | primary[i] 다시 |

즉 "단계 s 의 원본을 파괴하는 op 는 단계 s 의 완료가 commit 된 뒤에만 실행된다". 상태를 데이터보다 먼저 쓰면 이 사슬이 끊긴다 — 돌연변이 M1 이 단일 차단 1800 개 중 668 개에서 "부팅할 이미지 없음" 으로 깨진다. 이 레슨의 메타는 "다음에 할 일" 을 가리키므로 commit 시점이 늦을수록(데이터 뒤) 안전하고, 최악은 한 단계를 한 번 더 하는 것뿐이다(재작업 최대 86 op — 아래 차단 지도).

MCUboot 과 같은 점·다른 점(MCUboot design 문서 기준, 확인 권장): MCUboot swap-scratch 도 섹터(영역) 인덱스 큰 쪽부터, 단계 순서 "scratch ← secondary, secondary ← primary, primary ← scratch" 이고 단계마다 상태를 쓴다. 다만 상태는 이 레슨처럼 erase 가 필요한 레코드가 아니라 **trailer 의 한 번만 쓰는 바이트**(섹터당 3 개, 0xFF → 0x01)라 erase 가 없다. 대신 trailer 가 슬롯 끝 섹터에 있어서 그 섹터 자체를 옮길 때의 처리가 복잡하다.

### 5.3 부트로더 결정과 수명주기

`bl_boot` 순서: ① `meta_load` (없으면 IDLE), ② TEST → "시험 부팅한 이미지가 confirm 을 못 했다" → `SWAP REVERT (3,0)` commit, ③ SWAP 이면 (3,0) 일 때만 secondary 검증 → 무효면 `IDLE + REJECTED`, 아니면 `swap_resume`, ④ primary 검증 → 점프. 어떤 op 든 `FLASH_POWER_CUT` 이면 `BOOT_CUT` 으로 즉시 빠진다(재부팅).

차단 없는 수명주기의 op 지도(`./solution -v` 실제 출력):

```text
op map (confirm): stage 1..22 | swap 23..106 | confirm 107..108
uncut lifecycle ops: confirm=108 no-confirm(revert)=192
```

스테이징 22 = secondary erase 4 + program 16 + 요청 commit 2. 스왑 84 = 12 단계 × (erase 1 + program 4 + 메타 2). 확인 2. confirm 을 안 하는 시나리오는 TEST→REVERT commit 2 + 되돌림 스왑 84 가 더해져 192.

**시험 부팅 창(trial window)** — 처음 돌렸을 때 내 하네스가 실패를 보고했다: 확인 시나리오에서 op 106(최종 TEST 레코드의 program)을 AFTER 모드로 끊으면, TEST 는 플래시에 남았지만 부트로더는 점프하기 전에 죽었다. 다음 부팅은 TEST 를 보고 "confirm 안 한 시험 이미지" 로 판단해 되돌린다 — B 는 한 번도 안 돌았는데. 이건 버그가 아니라 설계의 경계다: "시험 기회를 줬다" 는 영구 기록은 점프 **전에** 써야 하고(그러지 않으면 B 가 부팅 직후 죽을 때 무한 시험 루프), 그 기록과 점프 사이의 아주 짧은 구간에서 전원이 나가면 시험 기회가 소모된다. 결과는 안전(옛 이미지 A, REVERTED 표시)하고, 서버가 업데이트를 다시 보내면 된다. 그래서 판정을 "확인 시나리오에서는 NEW, 단 차단 직후 메타가 TEST 였던 경우에만 되돌림도 허용" 으로 정밀하게 고쳤다. 단일 차단에서 이런 경우는 12 개(op 106 AFTER 1 + op 107 6 + op 108 5)다. MCUboot 도 trailer 에 copy_done 이 있고 image_ok 가 없으면 다음 부팅에 revert 하므로 같은 경계를 가진다고 이해하고 있다(문서 확인 권장).

```text
--- boot log: confirm scenario, torn erase at op 51, then torn program at op 60 ---
[BL] meta: copy 0 seq=1 IDLE - idx=0 step=0 flags=1
[BL] jump primary v1.2.0 (meta IDLE)
[APP] v1.2.0 running, staging v1.3.0 into secondary
[APP] update requested -> reset
[BL] meta: copy 1 seq=2 SWAP UPGRADE idx=3 step=0 flags=0
[BL] swap UPGRADE resume at sector 3 step 0
[PWR] power cut at op 51 -> reboot
[BL] meta: copy 1 seq=6 SWAP UPGRADE idx=2 step=1 flags=0
[BL] swap UPGRADE resume at sector 2 step 1
[PWR] power cut at op 60 -> reboot
[BL] meta: copy 0 seq=7 SWAP UPGRADE idx=2 step=2 flags=0
[BL] swap UPGRADE resume at sector 2 step 2
[BL] swap done -> TEST seq=14
[BL] jump primary v1.3.0 (meta TEST)
[APP] v1.3.0 self-test OK -> confirmed
[BL] meta: copy 0 seq=15 IDLE - idx=0 step=0 flags=1
[BL] jump primary v1.3.0 (meta IDLE)
--- result: outcome=NEW(v1.3.0 confirmed) boots=3 cuts=2 violations=- ---
```

읽는 법: op 51 = 다섯 번째 단계(섹터 2, step 1)의 첫 op, secondary[2] 의 erase 가 찢겼다. 재부팅한 부트로더는 seq 6 레코드(섹터 2 step 1)를 보고 그 단계를 처음부터 다시 한다. 재작업 때문에 op 번호가 밀려, op 60 은 다음 단계(섹터 2 step 2)의 primary[2] program 이었다 — 또 찢김, 다시 재개. 현장 로그로 이것만 남아도 "어디서 몇 번 끊겼고 어디서 재개했는지" 가 보인다. 실제 제품이라면 이 줄들을 `.noinit` 링 버퍼(BM-A09)나 다음 부팅의 텔레메트리로 올린다.

### 5.4 전수 차단 결과 (solution 실제 출력)

```text
torn meta commit: 96 cuts -> old state 80, new state 16, other 0
single cut x {before,after,torn x4}: scenarios=1800 boots=5660 (max 4/scenario) new=636 reverted=1164 (confirm-scenario reverts 12) violations=0
  cut points: N=108+192 ops (erase 84, program 216)
double cut (k1 x k2, torn): scenarios=25802 boots=84974 (max 5/scenario) new=6154 reverted=19648 (confirm-scenario reverts 384) violations=0
storm (1-5 random cuts): scenarios=3000 boots=10482 (max 8/scenario) new=1486 reverted=1514 (confirm-scenario reverts 36) violations=0
  power cuts actually hit: 8157
```

- 단일: 차단 지점 N = 108 + 192 = 300 op(erase 84, program 216) × 6 변형(BEFORE, AFTER, TORN 시드 4 개) = 1800 시나리오, 성공 부팅 5660 번 전부 A 또는 B 로 검증.
- 이중: 첫 차단 k1 을 모든 op 에, 두 번째 k2 를 "그 시나리오에서 k1 뒤에 실제로 실행되는 모든 op" 에 → 25 802 시나리오, 모든 시나리오에서 두 차단이 실제로 일어났는지도 확인(`not_hit == 0`). 복구 중의 차단, 복구 중의 메타 commit 차단, confirm 중의 차단이 모두 포함된다.
- 폭풍: 1~5 번, 차단 간격 1~60 op, 모드·시드 무작위 3000 개 → 실제 차단 8157 번.

op 하나하나의 결과 지도(`demo_cutmap` 실제 출력 일부, N = 새 이미지 확정, R = 되돌림):

```text
CONFIRM scenario: N=108 ops. op kind+sector | before / after / torn seed1 / torn seeds2-4  (N=new R=reverted X=violation)
  ops   1- 22 stage: E7-E10, P x16, meta E2 P2  | Nx22    Nx22    Nx22    Nx22   
  ops  23- 29 E11 P11 P11 P11 P11 E1 P1         | NNNNNNN NNNNNNN NNNNNNN NNNNNNN
  ops  30- 36 E10 P10 P10 P10 P10 E2 P2         | NNNNNNN NNNNNNN NNNNNNN NNNNNNN
  ops  37- 43 E6 P6 P6 P6 P6 E1 P1              | NNNNNNN NNNNNNN NNNNNNN NNNNNNN
  ...
  ops  93- 99 E7 P7 P7 P7 P7 E1 P1              | NNNNNNN NNNNNNN NNNNNNN NNNNNNN
  ops 100-106 E3 P3 P3 P3 P3 E2 P2              | NNNNNNN NNNNNNR NNNNNNN NNNNNNN
  ops 107-108 E1 P1                             | RR      RN      RR      RR     
  extra ops caused by one cut (redo work): max 86, avg 6.7 over 108 single cuts x 3 modes
NO-CONFIRM scenario: N=192 ops. ...
  extra ops caused by one cut (redo work): max 22, avg 4.5 over 192 single cuts x 3 modes
```

E11 = scratch erase, E10/P10 = secondary[3], E6/P6 = primary[3] … 처럼 MCUboot 순서가 op 열에 그대로 보인다. 확인 시나리오에서 R 은 시험 부팅 창(op 106 AFTER, 107, 108)에만 나온다. 스테이징 중 차단은 다음 부팅의 앱이 다시 내려받으므로(재작업 최대 22 op) 결과가 같다.

### 5.5 버그를 넣으면 잡히나 (비공개 사본으로 돌린 실제 결과)

| 돌연변이 | 바꾼 것 | 결과 |
|---|---|---|
| M1 상태 먼저 | `swap_resume` 에서 commit 후 데이터 | 56/60, 단일 668 / 이중 10 485 / 폭풍 1853 위반, 첫 위반 cut 24 "boot-fail" |
| M2 사본 하나 | 항상 사본 0 에 commit | 49/60, 단일 400 위반(첫 cut 21 "unexpected state"), 교대 테스트도 실패 |
| M3 CRC 없음 | `meta_valid` 가 magic 만 봄 | 52/60, 단일 **1** 위반(cut 185, 찢김에서만), 이중 33, 폭풍 2 |
| M4 매번 검증 | 진행 중 스왑에서도 secondary 검증 | 56/60, 단일 1092 위반, 진행 중 재개 테스트 실패 |
| M5 wrap 무시 | `seq_newer` 를 `a > b` 로 | 57/60, **이름 붙은 wrap 테스트 3 개만** 실패, 전수 차단은 0 위반 |

M3 이 교훈이다: CRC 를 빼도 1800 개 단일 차단 중 단 1 개만 깨진다. 찢긴 program 이 "magic 은 완성, 뒤쪽 필드는 반쯤" 인 레코드를 만들 때만 문제가 드러나기 때문 — 시드를 하나만 썼다면 놓쳤을 수 있다. 그래서 TORN × 여러 시드 + 이중 차단 + 폭풍을 겹쳐 쓴다. M5 는 반대로 "차단 주입이 못 찾는 버그" 의 예다.

### 5.6 비용 — 무엇이 먼저 닳나 (Part C)

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">업데이트 한 번의 섹터별 erase 횟수 (solution -v 실제 값, 막대 1칸 = 1회)</text>
  <text x="10" y="40" fill="currentColor">진한 막대 = 확인(confirm) 경로, 옅은 막대 = 되돌림(revert) 경로</text>
  <text x="10" y="70" fill="currentColor">S1 meta0</text>
  <rect x="110" y="58" width="140" height="14" class="f-warn" stroke="currentColor"/><text x="256" y="70" fill="currentColor">7</text>
  <rect x="110" y="74" width="260" height="10" class="f-mut" stroke="currentColor"/><text x="376" y="84" fill="currentColor">13</text>
  <text x="10" y="106" fill="currentColor">S2 meta1</text>
  <rect x="110" y="94" width="140" height="14" class="f-warn" stroke="currentColor"/><text x="256" y="106" fill="currentColor">7</text>
  <rect x="110" y="110" width="260" height="10" class="f-mut" stroke="currentColor"/><text x="376" y="120" fill="currentColor">13</text>
  <text x="10" y="142" fill="currentColor">S3–S6 primary</text>
  <rect x="110" y="130" width="20" height="14" class="f-ok" stroke="currentColor"/><text x="136" y="142" fill="currentColor">1</text>
  <rect x="110" y="146" width="40" height="10" class="f-mut" stroke="currentColor"/><text x="156" y="156" fill="currentColor">2</text>
  <text x="10" y="178" fill="currentColor">S7–S10 second.</text>
  <rect x="110" y="166" width="40" height="14" class="f-acc" stroke="currentColor"/><text x="156" y="178" fill="currentColor">2</text>
  <rect x="110" y="182" width="60" height="10" class="f-mut" stroke="currentColor"/><text x="176" y="192" fill="currentColor">3</text>
  <text x="10" y="214" fill="currentColor">S11 scratch</text>
  <rect x="110" y="202" width="80" height="14" class="f-acc" stroke="currentColor"/><text x="196" y="214" fill="currentColor">4</text>
  <rect x="110" y="218" width="160" height="10" class="f-mut" stroke="currentColor"/><text x="276" y="228" fill="currentColor">8</text>
  <text x="420" y="70" fill="currentColor">메타: commit 14 번 = erase 14 번</text>
  <text x="420" y="88" fill="currentColor">(요청 1 + 단계 12 + confirm 1)</text>
  <text x="420" y="142" fill="currentColor">슬롯: 섹터당 1~3 번</text>
  <text x="420" y="214" fill="currentColor">scratch: 섹터 수만큼 (4)</text>
  <text x="10" y="258" fill="currentColor">수명 10k 사이클(대표값): 메타 7 회/업데이트 → 1428 번, scratch 4 회 → 2500 번, 되돌림 포함이면 769 번</text>
  <text x="10" y="278" fill="currentColor">append-only 로그 메타(연습 4)로 바꾸면 메타 erase 1 회 → 핫스팟이 scratch 로 이동</text>
</svg>
```

```text
model update (confirm): erases=30 prog_units=568 hot=sector 1 x7 -> 1428 updates to wear-out
F4-scale confirm: erases=30 (meta 14, scratch 4) words=524400 time=27.890 s hot=sector 1 -> 1428 updates
F4-scale revert : erases=54 (meta 26, scratch 8) words=917712 time=49.183 s hot=sector 1 -> 769 updates
```

F4 급 가정(대표값, STM32F405/407 데이터시트 확인 권장): 슬롯·scratch 섹터 128 KB erase 1 s(×32 병렬 기준 typ), 메타 섹터 16 KB erase 0.25 s, 32비트 word program 16 µs, 수명 10 000 사이클. 확인 경로 27.9 s 중 데이터 erase 16 s(섹터 16 개), 메타 erase 3.5 s(14 × 0.25), program 8.4 s(524 400 word). 면접에서 말할 결론:

1. **업데이트 시간은 erase 가 지배** 한다 — 그 동안 같은 뱅크에서 코드를 못 읽는 단일 뱅크 F4 라면 워치독(BM-M10)과 인터럽트 정책도 같이 설계해야 한다.
2. **이중 사본 메타가 가장 먼저 닳는다**(업데이트당 7 회). 메타 섹터가 128 KB 였다면 시간도 3.5 s → 14 s. 그래서 MCUboot 는 trailer 의 한 번만 쓰는 바이트, SSD FTL 은 append-only 저널을 쓴다(연습 4: 메타 erase 14 → 1).
3. scratch 는 이미지 크기/scratch 크기 번 닳는다 — MCUboot 문서의 식 `num_upgrades = cycles / (image_size / scratch_size)` 그대로(연습 5).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 60/60 passed (이 맥 -O2 약 0.6 s)
./solution -v                                                                      # op 지도 + erase 표 + 부팅 로그
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 깨끗, 약 1.3 s
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 규칙 |
|---|---|---|
| 진행 상태를 데이터보다 먼저 commit | 상태 commit 뒤·데이터 전에 끊기면 재개가 그 단계를 건너뜀 → primary 에 빈 섹터 (M1: 668 위반) | `swap_step` OK → 그 다음 `meta_commit` |
| 메타 사본 하나만 erase 후 재기록 | erase 와 program 사이에 끊기면 유효 레코드가 0 개 → 스왑 중인지 모름 (M2) | 최신이 아닌 쪽에만 쓴다: `target = (w == 0) ? 1 : 0` |
| 메타에 CRC 없이 magic 만 | 찢긴 program 이 magic 은 완성·나머지는 반쯤 → 엉뚱한 (idx, step) 신뢰 (M3) | magic + CRC-32 + 필드 범위 검사 |
| `seq` 를 `a > b` 로 비교 | 0xFFFFFFFF → 0 에서 옛 사본을 최신으로 오판 (M5) | `(int32_t)(a - b) > 0` |
| 재개할 때마다 secondary 이미지 검증 | 진행 중엔 A/B 조각이 섞여 CRC 실패가 정상 → 요청 거절 → primary 반쯤 섞인 채 (M4) | `(3,0)` 에서만 검증 |
| commit 이 CUT 을 돌려줬으니 "안 써졌다" 고 가정 | AFTER 모드: op 는 끝났다. 반환값과 플래시 상태가 다를 수 있다 | 결정은 재부팅 후 `meta_load` 결과로만 |
| erase 없이 메타를 덮어쓰기 | AND 의미론 → 비트가 섞여 CRC 실패 (Part B 테스트) | 항상 erase → program → read-back |
| 새 이미지를 다 쓰기 전에 업데이트 요청 | 다운로드 중 끊기면 반쪽 이미지로 스왑 시작 (부트로더 검증이 막아 주지만 이중 안전장치가 하나 줄어든다) | `app_stage_update`: 쓰기 → 검증 → 요청 commit |
| confirm 을 부트로더가 대신 해 버림 | 새 이미지가 부팅 직후 죽어도 확정됨 → 벽돌 같은 상태 | 앱이 자가 진단(통신·센서·워치독 정상) 뒤에 confirm |

## 8. 면접 Q&A

<details>
<summary>Q1. 업데이트 중 아무 때나 전원이 나가도 부팅된다는 걸 어떻게 보장하고, 어떻게 증명하나요?</summary>

설계: ① 새 이미지는 실행 중인 슬롯이 아닌 secondary 에 받고, 다 쓰고 검증한 뒤에만 요청을 기록한다. ② 실행 슬롯을 바꾸는 작업은 섹터 단위 스왑으로 쪼개고, 각 단계는 원본이 살아 있는 동안만 진행되는 멱등 작업이다. ③ 진행 상황은 원자적으로 갱신되는 메타(이중 사본 + seq + CRC)에 "데이터 먼저, 상태 나중" 으로 기록한다. 증명: 플래시 드라이버 아래에 op 카운터 훅을 두고, 차단 없는 수명주기의 op 수 N(여기서 108/192)에 대해 k = 1..N 모든 op 에서 BEFORE/AFTER/TORN 으로 끊고, 복구 중 두 번째 차단까지 모든 쌍을 돌린다. 판정은 부트로더와 독립적으로 "primary 가 A 또는 B 와 바이트 단위로 같다". 결과: 단일 1800, 이중 25 802, 폭풍 3000 시나리오 위반 0. 꼬리: "모델이 현실과 다르면?" → Q8.

</details>

<details>
<summary>Q2. 메타데이터를 왜 두 개나 두나요? 하나에 CRC 를 달면 안 되나요?</summary>

하나면 갱신이 "erase → program" 두 op 인데, erase 가 끝나고 program 전에 끊기면 유효 레코드가 하나도 없다. CRC 는 "이 레코드가 망가졌다" 를 알려 줄 뿐 "직전 상태가 뭐였는지" 는 복원하지 못한다. 두 사본이면 쓰는 동안 다른 사본이 직전 상태를 들고 있다. M2(항상 사본 0)는 단일 차단에서 400 개 위반 — 깨끗한 차단(BEFORE/AFTER)만으로도 80 개. 반대로 CRC 를 빼면(M3) 사본이 둘이어도 찢긴 레코드를 믿어서 깨진다. 둘 다 필요하다: 이중 사본은 **가용성**, CRC 는 **판별**. SSD 에서 FTL 체크포인트를 두 위치에 번갈아 쓰고 시퀀스 번호 + CRC 로 최신 유효본을 고르는 것과 같은 구조다.

</details>

<details>
<summary>Q3. 스왑 각 단계 순서는 왜 그렇고, 재개할 때 "어디까지 했는지" 를 어떻게 아나요?</summary>

scratch ← secondary[i] (B 조각 대피), secondary[i] ← primary[i] (A 조각 대피), primary[i] ← scratch (B 조각 설치). 다음 단계가 지울 데이터는 이전 단계가 이미 복사해 둔 순서라, 메타가 (i, s) 를 가리키는 동안 단계 s 의 원본은 손대지 않은 상태다. 그래서 메타는 "다음에 할 일" 만 저장하고, 재개는 그 단계를 처음부터 다시 한다(erase 부터). 찢긴 erase·program 의 흔적은 다시 하는 erase 가 지운다. 비용은 최악 한 단계 재작업. MCUboot swap-scratch 도 같은 순서·같은 역방향 섹터 순서다(MCUboot design 문서 — 확인 권장). 꼬리: "섹터마다 erase 가 3 번인데 줄일 방법은?" → swap-move(Q6).

</details>

<details>
<summary>Q4. 새 이미지가 부팅은 되는데 현장에서 바로 죽는다면? test/confirm/revert 를 설명해 보세요.</summary>

스왑이 끝나면 상태는 TEST — "한 번 시험 부팅을 허락한다". 새 이미지는 자가 진단(주변장치 초기화, 통신 링크, 센서 값 범위, 몇 초간 워치독 정상)을 통과한 뒤 `app_confirm` 으로 IDLE+CONFIRMED 를 쓴다. 확정 전에 리셋되면(크래시, 워치독, 전원) 부트로더는 TEST 를 보고 같은 스왑으로 되돌린다 — 옛 이미지가 secondary 에 그대로 있으니 가능하다. 덮어쓰기(overwrite-only) 방식은 옛 이미지를 지우므로 이 안전망이 없다. 이 레슨의 확인 안 함 시나리오: 192 op, 되돌림 후 A + REVERTED. 꼬리: "한 번의 시험 기회는 너무 적지 않나?" → TEST 에 시도 카운터를 넣어 N 번까지 허용할 수 있지만, 그 카운터 갱신도 commit 이라 같은 원자성 규칙을 따라야 한다.

</details>

<details>
<summary>Q5. 스왑이 다 끝나고 새 이미지로 점프하기 직전에 전원이 나가면요?</summary>

이 레슨에서 실제로 발견한 경계다. 최종 TEST 레코드는 점프 전에 써야 한다(안 그러면 부팅 직후 죽는 이미지가 매번 다시 시험되는 무한 루프). 그래서 "TEST 기록 ~ 점프" 사이에서 끊기면 새 이미지는 한 번도 안 돌았는데 다음 부팅에서 되돌려진다. 결과는 안전(옛 이미지, REVERTED 표시)하고 서버가 다시 시도하면 된다. 단일 차단 1800 개 중 12 개, 이중 차단에서 384 개가 이 경우다 — 하네스는 "차단 직후 메타가 TEST 였을 때만 되돌림 허용" 으로 정확히 판정한다. 꼬리(staff): "이걸 없애고 싶다면?" → 시험 부팅 시도 카운터(최대 2~3)를 두어 "점프 전 차단" 한 번은 기회를 소모하지 않게 하거나, 앱이 부팅 초반에 "started" 를 쓰고 부트로더는 started 가 없으면 한 번 더 시험하게 할 수 있다. 대신 상태가 늘고, 부팅 직후 죽는 이미지에 대한 재시도 상한을 반드시 둬야 한다.

</details>

<details>
<summary>Q6. MCUboot 의 swap-scratch, swap-move, overwrite-only 는 어떻게 다르고 무엇을 고르겠나요?</summary>

(MCUboot design 문서 기준 — 확인 권장) swap-scratch: 이 레슨과 같은 3 단계, scratch 가 이미지 크기/scratch 크기만큼 닳는다(문서 예: 10 000 사이클, 150 KB 이미지, 4 KB scratch → 약 267 번). swap-move: scratch 없이 primary 를 먼저 한 섹터 위로 밀고, 섹터 N 마다 secondary N → primary N, primary N+1 → secondary N. primary 슬롯이 한 섹터 더 커야 하고, 업데이트당 erase 는 primary 2 번·secondary 1 번. 문서는 최근 swap-using-offset 을 권하고 swap-scratch 는 앞으로 없어질 수 있다고 적는다. overwrite-only: secondary 를 primary 에 덮어쓰기만 — 가장 단순하고 빠르며 소프트웨어 다운그레이드 방지가 이 모드에서만 지원되지만, 되돌림이 없다. 선택: F4 처럼 섹터가 128 KB 로 크고 수가 적으면 scratch 하나가 큰 섹터라 swap-move 의 "한 섹터 여유" 가 싸다. 되돌림이 필수(현장 접근 불가 로봇·차량)면 swap 계열, 듀얼 뱅크 칩(F42x/F43x 의 dual bank, 확인 권장)이면 뱅크 전환이 더 좋은 답일 수 있다.

</details>

<details>
<summary>Q7. 플래시 수명은요? 업데이트를 몇 번 할 수 있나요?</summary>

실제로 세어 보면 데이터 슬롯보다 메타·scratch 가 먼저 닳는다. 이 레슨 설계: 확인 경로 업데이트 한 번에 메타 섹터 각 7 회, scratch 4 회, primary 1 회, secondary 2 회 → 10k 사이클이면 메타 기준 1428 번, 되돌림까지 가면 769 번. 그래서 ① 메타는 append-only 로그로(연습 4: erase 14 → 1, 이제 scratch 가 핫스팟), ② scratch 는 크게(문서 식: 4 KB → 16 KB 면 266 → 1066 번), ③ 업데이트 정책(하루 몇 번까지)을 수명 예산에 맞춘다. SSD 에서 메타데이터 영역을 별도 wear-leveling 풀로 관리했던 것과 같은 판단이다. 꼬리: "10k 는 어디서 온 숫자?" → STM32F4 데이터시트 최소 보장값으로 기억(확인 권장), 온도에 따라 다르고 데이터 보존 기간과 맞바꾼다.

</details>

<details>
<summary>Q8. (staff) 이 테스트가 틀릴 수 있는 경우 — 모델이 현실과 다른 곳은?</summary>

① **약하게 지워진/쓰인 셀**: 진짜 NOR 는 찢긴 erase 뒤에 0xFF 로 읽히다가 나중에(온도·시간) 비트가 뒤집힐 수 있다. 이 모델은 한 번 읽은 값이 영원하다 → CRC 가 통과한 레코드가 나중에 깨지는 경우를 못 본다. 대책: erase 후 blank-check 를 margin read(지원 칩), 레코드를 쓸 때 "다음 칸이 비어 있는지" 도 확인, 부팅마다 이미지 CRC 재검증. ② **program 단위 내 순서**: 칩마다 찢김 모양이 다르다(이 모델: 앞 unit 완료 + 한 unit 일부). ECC 가 있는 플래시(STM32H7/L4 등)는 찢긴 워드를 읽으면 ECC 오류/버스 폴트가 날 수 있다 → 읽기 경로가 폴트를 견뎌야 한다. ③ **op 사이의 차단만 모델링**: 실제로는 CPU 가 반쯤 죽은 상태(브라운아웃)에서 잘못된 값을 쓸 수도 있다 → BOR 레벨 설정과 쓰기 전 전압 확인. ④ 부트로더 자체 업데이트는 다루지 않았다. ⑤ 데이터 오류가 아닌 **나쁜 이미지**(서명 없는 악성 이미지)는 CRC 로 못 막는다 → SEC 트랙(서명 검증). 테스트가 증명한 건 "이 모델 아래에서" 의 정리다.

</details>

<details>
<summary>Q9. (staff) 메타 commit 마다 섹터를 지우는 비용이 크다면 어떻게 바꾸겠나요?</summary>

세 가지. ① MCUboot 식 write-once 상태 바이트: 섹터마다 단계 플래그 3 개를 0xFF → 0x01 로 한 번씩만 program → 스왑 중 메타 erase 0 번. 판정은 "0xFF 가 아니면 완료" 로 하면 찢긴 플래그도 안전하다(플래그는 데이터가 끝난 뒤에만 쓰므로, 찢겼어도 단계는 끝난 것). ② append-only 로그(연습 4): 32 B 레코드를 섹터에 이어 쓰고 꽉 차면 다른 섹터로, 최신 유효 레코드는 절대 지우지 않음 — 같은 이중 차단 테스트 위반 0, 메타 erase 14 → 1. FTL 저널과 같은 구조. ③ 메타를 작은 섹터(F4 의 16 KB 섹터)나 외부 FRAM/EEPROM 에. 어느 것이든 바꾼 뒤 같은 전수 차단 하네스를 그대로 돌리는 게 핵심 — 이 레슨의 하네스가 백엔드에 무관하게 짜인 이유다.

</details>

## 9. 추가 연습

### 연습 1 — "상태 먼저" 버그 사냥

`swap_resume` 에서 commit 을 데이터보다 먼저 하도록 바꾸면(practice.c 의 `g_bug = BUG_STATUS_FIRST`) 단일 차단에서 몇 개가 깨지고, 찢김 없는 깨끗한 차단(BEFORE/AFTER)만으로도 깨지는가? 첫 위반은?

<details>
<summary>답 보기</summary>

```text
ex1 status-before-data: single-cut violations all=668 (before/after=230, torn=438) first: cut 24 boot-fail(no valid image)
```

깨끗한 차단만으로 230 개. 이 버그는 찢김과 무관한 **순서** 버그라 op 사이에서 깨끗이 끊겨도 드러난다. 첫 위반 cut 24: 버그 순서에선 스왑 첫 단계가 "(3,1) commit(op 23–24) → 단계 0 데이터" 다. op 24(commit 의 program)를 AFTER 로 끊으면 (3,1) 은 기록됐는데 단계 0 데이터(scratch ← secondary[3])는 한 번도 안 쓰였다 → 재개가 (3,1) 부터 → scratch 는 빈 채 → 단계 2 가 빈 scratch 를 primary[3] 에 설치 → 부팅할 이미지 없음.

</details>

### 연습 2 — 사본 하나

`meta_commit` 이 항상 사본 0 에 쓰면(`BUG_SINGLE_COPY`)?

<details>
<summary>답 보기</summary>

```text
ex2 single-meta-copy  : single-cut violations all=400 (before/after=80, torn=320) first: cut 21 unexpected state
```

cut 21 은 스테이징 끝의 요청 commit 의 erase(사본 0 = 공장 IDLE 레코드)다. 지워졌는데 새 레코드는 안 써졌다 → 유효 메타 없음 → 부트로더는 IDLE 로 간주하고 A 로 부팅하지만, 앱은 메타를 읽을 수 없어 "unexpected state". 스왑 중이었다면 반쯤 섞인 primary 로 부팅을 시도한다(BEFORE/AFTER 80 개).

</details>

### 연습 3 — CRC 없는 메타

`meta_valid` 가 magic 만 보면(`BUG_NO_CRC`) 어떤 차단 모드에서만 깨지는가? 왜 단일 차단에서 겨우 1 개인가?

<details>
<summary>답 보기</summary>

```text
ex3 meta-without-CRC  : single-cut violations all=1 (before/after=0, torn=1) first: cut 185 unexpected state
```

깨끗한 차단에선 레코드가 "완전히 옛것" 또는 "완전히 새것" 이라 CRC 가 없어도 맞다. 찢긴 program 이 첫 unit(magic + seq)은 완성하고 뒤 unit(state/idx/step)은 반쯤 쓴 경우에만 엉뚱한 레코드가 "최신" 으로 뽑힌다 — 첫 unit 이 완성될 확률 × 나머지가 범위 검사를 통과할 확률이라 드물다. 본문 비공개 돌연변이 실행에서는 이중 차단 33 개, 폭풍 2 개로 더 잘 잡혔다. 교훈: 찢김 모델 + 여러 시드 + 이중 차단이 없으면 이 버그는 테스트를 통과한다.

</details>

### 연습 4 — append-only 로그 메타 (FTL 식)

메타 두 섹터를 32 B 칸 8 개짜리 로그로 쓰자: 최신 레코드가 있는 섹터의 "마지막으로 뭔가 쓰인 칸 다음" 에 추가, 칸이 없으면 다른 섹터를 지우고 칸 0. 최신 유효 레코드는 절대 덮거나 지우지 않는다. 같은 전수 차단을 통과하는가? 확인 경로 업데이트당 메타 erase 는?

<details>
<summary>답 보기</summary>

```text
ex4 meta erases per confirmed update: dual-copy=14 log(8 slots/sector)=1 (sector1 0, sector2 1), scratch=4
ex4 log meta: single scenarios=1584 violations=0, double scenarios=19983 violations=0
```

메타 erase 14 → 1(공장 레코드 1 + commit 14 = 15 개가 칸 16 개에 들어감; 정상 상태로 반복하면 업데이트당 약 14/8 ≈ 1.75 회를 두 섹터가 나눠 가짐). 이제 scratch(4 회)가 가장 뜨겁다. 차단 지점 수가 줄어든 것(1584 = 6 × (95+169), 본문은 6 × (108+192))은 commit 이 op 2 개 → 대부분 1 개(program 만)가 됐기 때문. "칸 다음에 쓰기" 규칙이 중요한 이유: 찢긴 추가가 남긴 쓰레기 칸은 비어 있지 않으니 건너뛴다 — 0xFF 가 아닌 칸에 다시 program 하면 AND 로 섞인다. 약하게 쓰인 셀(Q8)까지 고려하면 "다음 칸이 완전히 0xFF 인지" 검사도 넣는다.

</details>

### 연습 5 — scratch 수명

MCUboot 문서의 식 `num_upgrades = cycles / (image_size / scratch_size)` 로 10 000 사이클, 150 KB 이미지, scratch 4 KB / 16 KB 를 계산하고(내림), F4 급(이미지 512 KB, scratch 128 KB)과 이 레슨의 메타 한계(10 000/7)를 비교하라.

<details>
<summary>답 보기</summary>

```text
ex5 scratch life: 150K image/4K scratch=266, /16K=1066, F4 512K image/128K scratch=2500 updates (dual-copy meta limits to 1428)
```

문서는 반올림해 "약 267 / 1067" 이라 적는다(정수 내림은 266/1066). F4 급에선 scratch 하나가 128 KB 라 2500 번이지만, 이중 사본 메타가 1428 번에서 먼저 닳는다 → 이 설계의 수명은 메타가 정한다. 연습 4 의 로그 메타로 바꾸면 scratch 가 한계가 된다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 13/13 passed (약 1.2 s)
```

## 10. 핵심 정리

- 전원 차단 내성 = **"가장 새 유효 상태는 절대 건드리지 않는다"** + 모든 쓰기 op 에서 끊어 보는 전수 테스트. 말로 하지 말고 N(여기서 108/192)과 시나리오 수(1800 / 25 802 / 3000)로 말한다.
- 메타: 이중 사본 + wrap-safe seq + CRC-32. 이중 사본은 가용성, CRC 는 찢김 판별 — 둘 중 하나만으로는 깨진다(M2, M3).
- scratch 스왑: scratch ← secondary, secondary ← primary, primary ← scratch. 각 단계는 원본이 살아 있는 동안 멱등 → 재개는 "그 단계 처음부터".
- **데이터 먼저, 상태 나중.** 반대로 하면 깨끗한 차단만으로도 깨진다(M1: 668).
- commit 이 CUT 을 돌려줘도 반영됐을 수 있다(AFTER). 결정은 재부팅 후 플래시 상태로만.
- test → confirm → (없으면) revert. TEST 기록 ~ 점프 사이 차단은 시험 기회를 소모하고 안전하게 되돌린다.
- 비용: 시간은 erase 가 지배(F4 급 약 28 s), 수명은 메타·scratch 가 정한다(1428 / 2500 번). write-once 플래그나 append-only 로그로 메타 erase 를 없앤다.
- 테스트의 한계: 약한 셀, ECC, 브라운아웃 중 잘못된 쓰기, 서명 — 모델 밖은 별도 대책.

## 참고자료

- MCUboot design 문서 — swap using scratch(내림차순 영역, 단계 a–i, swap status 3 레코드/섹터), swap using move, overwrite-only, test/permanent/revert 와 image_ok/copy_done, scratch 수명 식과 267/1067 예: <https://docs.mcuboot.com/design.html>
- MCUboot bootutil API — `boot_set_confirmed()` / `boot_set_pending()` (이름·시그니처 확인 권장): <https://github.com/mcu-tools/mcuboot>
- ST RM0090 (STM32F405/407/42x/43x) — Flash 섹터 구성(16/64/128 KB), erase/program 절차, dual bank(F42x/43x); STM32F405/407 데이터시트 Flash 특성(섹터 erase 시간, word program 16 µs typ, 10 kcycles) — 이 레슨의 시간·수명 숫자는 기억에 의존한 대표값, 확인 권장: <https://www.st.com/resource/en/reference_manual/rm0090.pdf>
- 관련 레슨: BM-A02 (플래시 드라이버 시뮬레이터 — 이 레슨의 모델은 그 축약판), BM-A04 (이미지 헤더 + CRC/SHA-256 — 헤더 축약판), BM-A05 (YMODEM 수신 + 플래시 쓰기), BM-A03 (부트로더 → 앱 점프), BM-A09 (`.noinit` 크래시 덤프), BM-M10 (워치독 — 긴 erase 중 kick), BM-B06 (슬롯 레이아웃 링커 스크립트), BM-B13 (wrap-safe 비교), COMM-A06 (SPI NOR program/erase 프로토콜), COMM-B08/B12·ALGO-M01/M02 (CRC)
- 다음 단계: PLAT-M07 (A/B 슬롯 정책), SEC-* (서명 검증·secure boot·롤백 방지 카운터), TEST-B09 (워치독 태스크 체크인 — confirm 전 자가 진단)
