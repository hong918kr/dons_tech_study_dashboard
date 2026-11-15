---
id: ARCH-A06
time: 120분
updated: 2026-11-11
---
# ARCH-A06 비순차 실행 개념 모델: 스코어보드와 ROB

## 학습 목표

- **Tomasulo 알고리즘**(예약 스테이션 + 레지스터 상태표 → ROB 태그 + CDB 1 개)을 사이클 단위로 구현하고, issue 때 피연산자를 읽는 **세 경우**(레지스터 파일 / 끝났지만 commit 전인 ROB 값 / 아직 계산 중이라 태그)를 코드로 말할 수 있다.
- 같은 명령열을 **CDC 6600 식 스코어보드**(리네이밍 없음)로도 돌려서, WAR 은 쓰기 지연으로 · WAW 는 issue 정지로 막는 스코어보드가 21 → 24 사이클로 느려지는 것을 **명령별 타임라인**으로 보인다. "리네이밍이 WAR/WAW 를 없앤다" 를 숫자로 설명한다.
- **in-order commit** 으로 정밀 예외(precise exception)를 만든다: DIV0 / 범위 밖 LD 는 execute 에서 **표시만** 하고 ROB 머리에서 받으며, 젊은 명령은 flush. 무작위 프로그램 3000 개에서 아키텍처 상태 == 순차 기준 인터프리터(예외 명령 직전까지)를 확인한다.
- **ROB 없는 Tomasulo**(CDB 때 레지스터 파일에 바로 씀, IBM 360/91 방식)가 같은 프로그램에서 남기는 부정확한 상태를 세 종류(젊은 명령이 이미 씀 / 오래된 명령이 아직 안 씀 / 틀린 명령을 보고)로 분류한다.
- ROB 크기 · RS 개수 sweep 으로 IPC 표를 만들고 **Little 의 법칙**(점유 = 처리율 × 체류 시간)으로 "독립 MUL 커널은 ROB 9 개면 충분, 의존 사슬은 ROB 를 아무리 키워도 0.2" 를 손으로 예측한다. 실제 코어(Cortex-M in-order, 큰 OoO 코어)와 연결하고, M6 에서 창 크기를 재는 방법을 직접 돌려 본다.

## 1. 문제

"ROB 없이 Tomasulo 만 있으면 왜 정밀 예외가 깨지나요?", "리네이밍은 어떤 해저드를 없애고 어떤 건 못 없애죠?", "ROB 크기는 어떻게 정하죠?" — CPU/SoC 설계, 실리콘 검증(OoO 코어를 ISS 와 commit 단위로 비교), 성능 면접에서 반복되는 질문이다. 펌웨어 쪽에서도 Cortex-A 의 비동기 abort, Cortex-M 의 imprecise BusFault(ARCH-M10 Q2), `DSB`/`ISB` 가 왜 필요한지가 전부 "명령이 **완료되는 순서**와 **보이는 순서**가 다르다" 에서 나온다.

이미 아는 것(다시 가르치지 않는다): in-order 5단 파이프라인과 RAW 스톨(ARCH-M01), 포워딩 · load-use · 분기 예측(ARCH-M02), toy ISA 위의 정밀 예외 · EPC · "검사는 commit 앞"(ARCH-M10), "WAR/WAW 는 지연이 다른 FU 나 비순차 실행에서 생긴다"(ARCH-M01 Q7 — 이 레슨이 그 답). 메모리 순서(서로 다른 주소 간 순서)는 ARCH-A05 · MEM-M07, 캐시 일관성은 ARCH-A04 의 일이다.

**왜 toy ISA(ARCH-B08) 를 안 쓰나.** toy ISA 는 16-bit 단일 사이클 ALU 라 모든 명령의 지연이 같다. WAW 는 **늦게 시작한 짧은 명령이 먼저 끝날 때** 생기므로 지연이 다른 여러 사이클 FU(MUL 6, DIV 12 …)가 꼭 필요하다. 그래서 작은 레지스터 ISA 를 새로 정의했다:

| 명령 | 의미 | FU · 지연 (**가정**, 대표값) | 예외 |
|---|---|---|---|
| `ADD/SUB rd, rs, rt` | rd ← rs ± rt (mod 2^32) | ADD · 2 (pipelined) | — |
| `ADDI rd, rs, imm` | rd ← rs + imm | ADD · 2 | — |
| `MUL rd, rs, rt` | 하위 32 비트 | MUL · 6 (pipelined) | — |
| `DIV rd, rs, rt` | 부호 없는 나눗셈 | DIV · 12 (**not** pipelined) | rt == 0 → **DIV0** |
| `LD rd, imm(rs)` | rd ← mem[rs+imm] | MEM · 3 (pipelined) | 주소 ≥ 64 → **MEM** |
| `ST rt, imm(rs)` | mem[rs+imm] ← rt | MEM · 1 (주소 계산만, 쓰기는 commit 때) | 주소 ≥ 64 → **MEM** |

r0–r7, **r0 = 0 고정**(쓰기 무시 — 절대 주소 `0(r0)` 와 "제수 0" 을 만들기 쉽다). 데이터 메모리 64 워드. 분기는 없다(직선 코드 = 동적 순서가 프로그램 번호와 같다) — 분기 미스 예측 복구는 예외와 **같은 flush 경로**라서 Part B · 연습 3 에서 그 비용으로 다룬다.

기본 구조(`cfg_default`): RS = ADD 3 · MUL 2 · DIV 2 · MEM 3, FU 각 1 개, **ROB 8**, **commit 폭 W = 1**, issue 사이클당 1 개, **CDB 1 개**. 사이클 규칙(한 사이클 안에서 뒤 단계부터 처리, "t−1 까지 생긴 것만 쓴다"):

| 단계 (사이클 t) | 조건 · 동작 |
|---|---|
| commit | ROB 머리가 t−1 까지 완료 → 레지스터 파일/메모리에 반영, 최대 W 개. 예외 표시면 여기서 받고 flush |
| CDB | 실행이 t−1 까지 끝난 것 중 **가장 오래된 1 개**만 방송 → ROB 값 + 기다리던 RS 깨우기. RS 해제 (H&P 방식). ST 는 CDB 없이 "확정" |
| execute | 피연산자가 t−1 까지 모였고 FU 가 비면 시작(오래된 것 먼저). 끝 = t + lat − 1. LD 는 더 오래된 ST 가 모두 확정돼야 (같은 주소면 그 값을 forwarding) |
| issue | 순서대로 1 개. ROB 빈칸(사이클 **시작** 기준) + RS 빈칸(t−1 까지 해제된 것). 같은 사이클 CDB 값은 issue 가 잡는다(bypass) |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs>
    <marker id="a06arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0 0 L10 5 L0 10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">모델 구조 — issue 는 순서대로, 실행 · CDB 는 순서 없이, commit 은 다시 순서대로</text>
  <g stroke="currentColor">
    <rect x="10" y="34" width="130" height="46" rx="4" class="f-mut"/>
    <rect x="170" y="34" width="170" height="46" rx="4" class="f-acc"/>
    <rect x="170" y="96" width="170" height="56" rx="4" class="f-mut"/>
    <rect x="560" y="34" width="170" height="170" rx="4" class="f-warn"/>
    <rect x="10" y="172" width="120" height="36" rx="4" class="f-mut"/>
    <rect x="140" y="172" width="120" height="36" rx="4" class="f-mut"/>
    <rect x="270" y="172" width="120" height="36" rx="4" class="f-mut"/>
    <rect x="400" y="172" width="140" height="36" rx="4" class="f-mut"/>
    <rect x="10" y="228" width="120" height="36" rx="4" class="f-acc"/>
    <rect x="140" y="228" width="120" height="36" rx="4" class="f-acc"/>
    <rect x="270" y="228" width="120" height="36" rx="4" class="f-acc"/>
    <rect x="400" y="228" width="140" height="36" rx="4" class="f-acc"/>
    <rect x="10" y="288" width="530" height="26" rx="4" class="f-ok"/>
    <rect x="560" y="236" width="170" height="78" rx="4" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="54">명령 (프로그램 순서)</text><text x="20" y="72">pc → 사이클당 1 개</text>
    <text x="180" y="54" font-weight="bold">Issue + rename</text><text x="180" y="72">ROB · RS 빈칸이 있어야</text>
    <text x="180" y="114">레지스터 상태표 (RAT)</text>
    <text x="180" y="130" font-family="monospace">r1 → ROB#3 (태그)</text>
    <text x="180" y="146" font-family="monospace">r2 → -1 (파일이 최신)</text>
    <text x="570" y="54" font-weight="bold">ROB (원형, 크기 R)</text>
    <text x="570" y="76" font-family="monospace">head #0 DIV  done 예외?</text>
    <text x="570" y="94" font-family="monospace">     #1 ADD  대기</text>
    <text x="570" y="112" font-family="monospace">     #2 MUL  done</text>
    <text x="570" y="130" font-family="monospace">tail #3 LD   실행 중</text>
    <text x="570" y="156">값 · 예외 표시 · 주소</text>
    <text x="570" y="174">머리부터 commit ≤ W/사이클</text>
    <text x="570" y="192">예외면 젊은 것 전부 flush</text>
    <text x="18" y="194">ADD RS × 3</text><text x="148" y="194">MUL RS × 2</text>
    <text x="278" y="194">DIV RS × 2</text><text x="408" y="194">MEM RS × 3 (LD/ST)</text>
    <text x="18" y="250">ADD 2c, pipe</text><text x="148" y="250">MUL 6c, pipe</text>
    <text x="278" y="250">DIV 12c</text><text x="408" y="250">LD 3c / ST 주소 1c</text>
    <text x="20" y="306">CDB: 사이클당 1 개, 가장 오래된 것 먼저 → RS + ROB 로 방송</text>
    <text x="570" y="256" font-weight="bold">레지스터 파일 + 메모리</text>
    <text x="570" y="274">= 아키텍처 상태</text>
    <text x="570" y="292">ROB 모드: commit 때만 씀</text>
    <text x="570" y="308">ROB 없음: CDB 때 씀</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none" marker-end="url(#a06arr)">
    <path d="M140 57 L168 57"/>
    <path d="M340 57 L558 57"/>
    <path d="M255 80 L255 94"/>
    <path d="M200 152 L200 170"/>
    <path d="M70 208 L70 226"/><path d="M200 208 L200 226"/><path d="M330 208 L330 226"/><path d="M470 208 L470 226"/>
    <path d="M70 264 L70 286"/><path d="M200 264 L200 286"/><path d="M330 264 L330 286"/><path d="M470 264 L470 286"/>
    <path d="M540 300 L548 300 L548 190 L558 190"/>
    <path d="M645 204 L645 234"/>
  </g>
  <path d="M520 288 L520 216" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#a06arr)"/>
  <text x="440" y="306" fill="currentColor" font-size="12">점선 ↑ = RS 깨우기</text>
</svg>
```

### Part A — 리네이밍: WAR/WAW 를 없애는 것을 사이클로 보이기

- TODO `rename_operands`: issue 때 소스마다 (r0 → 0) / 상태표 −1 → 레지스터 파일 값 / 태그의 ROB 엔트리가 done → **ROB 의 값** / 아니면 **태그(q)** 를 기다림. 목적지가 있으면 상태표[rd] ← 이 ROB 번호.
- TODO `cdb_write`: ROB 엔트리 완료 표시, 이 태그를 기다리는 RS 전부 깨우기(값 채우고 `rdy` 갱신), ROB 없는 모드면 상태표가 아직 이 태그일 때만 레지스터 파일에 씀.
- TODO `sb_issue_ok` · `sb_write_ok`: 스코어보드의 구조 · **WAW** 검사(issue 정지)와 **WAR** 검사(결과 쓰기 지연).
- 시퀀스 `DIV r1,r2,r3 / ADD r4,r1,r5 / MUL r5,r6,r7 / LD r1,0(r0) / ADDI r6,r6,1` (RAW · WAR · WAW 가 다 있다): Tomasulo **21** 사이클, 스코어보드 **24**, 직렬 25 — 명령별 타임라인이 손계산과 같아야 한다. 무작위 예외 없는 프로그램 2000 개 × 무작위 설정: 세 모델 모두 최종 상태 == 순차 기준, 타임라인 불변식(issue 순서, x.s > iss, wr > x.e, cmt > wr, CDB ≤ 1/사이클, commit ≤ W/사이클) 통과.

### Part B — 정밀 예외: commit 에서 받기 vs ROB 없는 Tomasulo

- TODO `commit`: 머리부터 W 개까지, 완료된 것만. 예외 표시면 `exc_idx` 기록 + `flush_all` (그 명령은 아무것도 안 씀). ST 는 **여기서** 메모리에 쓴다. 레지스터를 쓰고, 상태표가 아직 나를 가리키면 −1.
- DIV0 프로그램(`DIV r1,r2,r0` 뒤에 짧은 ADD · ST · MUL): ROB → 상태 == 기준, 젊은 3 개 flush. ROB 없음 → 젊은 3 개가 이미 레지스터와 **메모리**를 바꾼 상태.
- 젊은 LD 가 먼저 폴트(시간상 먼저 발견): ROB 는 오래된 DIV 를 끝내고 LD 에서 정밀하게, ROB 없음은 DIV 결과가 빠진 상태. 둘 다 폴트면 ROB 는 **프로그램 순서상 먼저인** DIV0 를 받는다.
- 무작위 3000 개(예외 섞임) × 무작위 설정: ROB 모드 불일치 0, ROB 없는 모드의 부정확 개수 출력.

### Part C — 구조적 한계: ROB 크기 · RS 개수 → IPC

- 커널 3 개(각 120 명령): **chain** (MUL/ADD 가 모두 앞 결과에 의존), **indep** (서로 무관한 MUL, 목적지 4 개를 돌려 써서 WAW 투성이), **dot** (`LD, LD, MUL, ADD` 로 s += a[i]·b[i], r1–r3 재사용).
- ROB ∈ {4 … 64} × RS ∈ {1 … 16} → IPC 표, 스코어보드 IPC 와 비교. 분기 미스 예측 복구는 "flush-at-commit" 으로 개념만(연습 3 이 그 비용을 잰다).

## 2. Problem (English)

> **Out-of-order core model: scoreboard vs Tomasulo + ROB.** Define a small register ISA with multi-cycle functional units (ADD 2, MUL 6 pipelined, DIV 12 non-pipelined, LD 3 cycles — assumed values). Implement Tomasulo's algorithm with per-class reservation stations, a register status (rename) table that points to ROB entries, a single common data bus (one broadcast per cycle, oldest first), a reorder buffer of size R and in-order commit of up to W instructions per cycle. Print a per-instruction timeline: issue / execute start / execute end / write-CDB / commit.
>
> A. Show that renaming removes WAR and WAW hazards: on a sequence with RAW, WAR and WAW dependences, compare cycle counts against an in-order-issue CDC 6600-style scoreboard (WAW stalls issue, WAR delays write-back). Verify all models against a sequential reference interpreter on random programs.
>
> B. Precise exceptions: divide-by-zero and out-of-range loads/stores are detected at execute but taken at commit; flush all younger instructions. Check, on random programs with a fixed seed, that registers and memory equal the sequential interpreter run up to (not including) the faulting instruction. Then show that "Tomasulo without a ROB" (register file written at CDB time) leaves an imprecise state on the same programs.
>
> C. Sweep ROB size and RS count and report IPC for a dependent chain, independent multiplies and a dot-product loop. Explain the numbers with Little's law.
>
> Follow-ups: Why can't renaming remove RAW? What exactly breaks without a ROB — give three distinct failure modes. How big must the ROB be to hide a 6-cycle multiply at 1 IPC? Where would you recover from a branch misprediction, and what does recovering at commit cost? When may a load bypass an older store?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

TODO 는 5 개 함수의 본문뿐이다: `rename_operands`, `cdb_write`(Part A), `sb_issue_ok`, `sb_write_ok`(Part A, 스코어보드), `commit`(Part B). 나머지(ROB/RS 할당, execute 선택, LD 메모리 의존 검사, ST 확정, flush, 스코어보드 본체, 어셈블러, 무작위 생성기, 타임라인 출력과 불변식 검사)는 starter 와 solution 이 같다. 처음 실행(실제 출력 일부, exit 1):

```text
   #  instr              iss  x.s  x.e   wr  cmt
   0  DIV  r1, r2, r3      1    -    -    -    -
   1  ADD  r4, r1, r5      2    -    -    -    -
...
  Tomasulo+ROB: cycles=-1 committed=0 IPC=0.000 cdb=0
...
  scoreboard  : cycles=19  stalls struct=0 WAW=0 WAR=0 RAW=13
...
  random 2000 (예외 없음): ROB 불일치 2000, 타임라인 위반 0, 교착 2000, ROB 없음 불일치 2000, 스코어보드 불일치 1040, 비순차 실행이 일어난 프로그램 0
...
7/34 passed
```

빈 `rename_operands` 는 RS 를 0 으로 둔 채라 모든 명령이 "ROB#0 태그를 기다림" 이 되고, 100 사이클 동안 아무 일도 안 일어나면 하네스가 교착(cycles=−1)으로 끊는다. 빈 스코어보드 검사는 19 사이클로 **더 빠르지만** 2000 개 중 1040 개에서 틀린 값을 낸다 — 해저드 검사를 빼면 빨라지고 틀린다는 것 자체가 Part A 의 요점이다.

## 4. 힌트

<details><summary>힌트 1 — issue 때 피연산자는 세 곳 중 하나에 있다</summary>

상태표[reg] == −1 이면 아무도 그 레지스터를 쓰려고 기다리지 않으니 레지스터 파일이 최신이다. 상태표가 ROB 엔트리 e 를 가리키면: e 가 이미 CDB 를 탔다면(done) 값은 **ROB 에** 있다(아직 commit 전이라 파일엔 없다). 아니면 값 대신 **태그 e** 를 들고 기다린다. 마지막으로 내가 목적지를 쓰면 상태표[rd] ← 나 — 그 순간부터 rd 를 읽는 뒤 명령은 나를 본다. 옛 값이 필요한 앞 명령은? 이미 issue 때 값이나 다른 태그를 챙겼다 = WAR 이 사라진 이유.
</details>

<details><summary>힌트 2 — CDB 는 "태그 비교 방송"</summary>

방송 한 번에 모든 RS 의 `qj`, `qk` 를 태그 e 와 비교해 같으면 값을 넣고 −1 로. `rdy` 는 "피연산자가 다 모인 사이클" 이라 max(기존, t) — 실행은 t+1 부터. ROB 없는 모드에서 레지스터 파일을 쓸 때 조건이 하나 있다: 상태표가 **아직 나**를 가리켜야 한다. 내 뒤에 같은 rd 를 쓰는 명령이 issue 됐다면 내 값은 이미 낡았다(WAW).
</details>

<details><summary>힌트 3 — commit 은 "머리만 본다"</summary>

머리가 안 끝났으면 뒤가 다 끝났어도 아무도 못 나간다 — 그게 in-order commit 이다. 예외 표시가 있으면 기록하고 `flush_all` 후 즉시 return(그 명령은 레지스터도 메모리도 안 쓴다). 정상 엔트리는 레지스터 파일에 **항상** 쓴다 — 상태표가 더 젊은 엔트리를 가리켜도. 왜? 그 젊은 엔트리가 나중에 예외로 버려지면 파일엔 내 값이 있어야 하니까. 상태표를 −1 로 되돌리는 건 "나를 가리킬 때만".
</details>

<details><summary>힌트 4 — 스코어보드의 두 검사</summary>

스코어보드엔 리네이밍이 없어서 레지스터 이름 = 저장 위치 하나. WAW: 같은 rd 를 쓰려는 활성 명령이 있으면 issue 를 멈춘다(늦게 쓴 쪽이 이겨야 하는데 순서를 보장할 방법이 없으니). WAR: 내 rd 를 아직 **안 읽은** 더 오래된 명령이 있으면 결과 쓰기를 미룬다. "안 읽음" = `ro == 0 || ro >= t` (같은 사이클 read 는 쓰기보다 뒤에 처리된다).
</details>

<details><summary>힌트 5 — 타임라인이 한 칸 다를 때</summary>

`x.s` 가 하나 이르면 `rdy >= t` 비교(t−1 규칙)를 확인. `wr` 이 하나 늦으면 같은 사이클에 더 오래된 명령이 CDB 를 탔는지 본다(예: Part A 의 ADDI 는 x.e = 7 인데 LD 가 사이클 8 의 CDB 를 가져가서 wr = 9). commit 이 하나 이르면 `done_cyc >= t` 조건을 빠뜨린 것.
</details>

## 5. 풀이

### Part A — 리네이밍이 WAR/WAW 를 없애는 순간

**issue 때 피연산자 읽기 (rename_operands):**

| 상태표[reg] | 태그 엔트리 | 가져가는 것 | 뜻 |
|---|---|---|---|
| −1 | — | 레지스터 파일 값 | 쓰려는 명령이 없다 |
| e | done (CDB 탔음) | **ROB[e].val** | 끝났지만 commit 전 — 파일에 아직 없다 |
| e | 실행 중/대기 | 태그 e (`q = e`) | RAW — 방송을 기다린다 |

그리고 목적지가 있으면 `regstat[rd] = 나`. 이 한 줄이 리네이밍이다. 이름 r5 는 이제 "ROB#2 가 만들 값" 을 뜻하고, 옛 r5 가 필요했던 앞 명령(ADD)은 issue 때 이미 값 5 를 RS 에 복사해 갔다.

**손으로 따라가기** (`cfg_default`, 초기값 r2=100 r3=7 r5=5 r6=3 r7=4):

| 사이클 | 일어난 일 | 해저드가 어떻게 됐나 |
|---|---|---|
| 1 | issue 0 DIV → 상태표 r1 = #0 | |
| 2 | DIV 실행 2–13. issue 1 ADD: r1 = 태그 #0, **r5 = 5 (값 복사)** | RAW 는 태그로 기다림 |
| 3 | issue 2 MUL → 상태표 r5 = #2 | **WAR 소멸**: ADD 는 이미 5 를 가짐 |
| 4 | MUL 실행 4–9. issue 3 LD → 상태표 r1 = **#3** | **WAW 소멸**: r1 의 최신 생산자는 이제 #3 |
| 5 | LD 실행 5–7. issue 4 ADDI (r6 = 3 복사) → r6 = #4 | |
| 6 | ADDI 실행 6–7 | |
| 8 / 9 / 10 | CDB: LD(x.e 7) → ADDI(x.e 7, 더 젊어서 한 사이클 밀림) → MUL | CDB 1 개 = 구조 해저드 |
| 14 | CDB: DIV → ADD 의 RS 깨어남 (r1 = 14) | |
| 15 | commit DIV: 파일 r1 ← 14 (상태표 r1 은 #3 그대로). ADD 실행 15–16 | |
| 17 | CDB ADD | |
| 18–21 | commit 1, 2, 3, 4 (W = 1) | LD 의 commit 이 r1 = 1000 으로 덮음 = 순차 결과 |

실제 출력 (`./so`):

```text
   #  instr              iss  x.s  x.e   wr  cmt
   0  DIV  r1, r2, r3      1    2   13   14   15
   1  ADD  r4, r1, r5      2   15   16   17   18
   2  MUL  r5, r6, r7      3    4    9   10   19
   3  LD   r1, 0(r0)       4    5    7    8   20
   4  ADDI r6, r6, 1       5    6    7    9   21
  Tomasulo+ROB: cycles=21 committed=5 IPC=0.238 cdb=5
   #  instr              iss   ro  x.s  x.e   wr
   0  DIV  r1, r2, r3      1    2    3   14   15
   1  ADD  r4, r1, r5      2   16   17   18   19
   2  MUL  r5, r6, r7      3    4    5   10   17
   3  LD   r1, 0(r0)      16   17   18   20   21
   4  ADDI r6, r6, 1      20   21   22   23   24
  scoreboard  : cycles=24  stalls struct=3 WAW=12 WAR=5 RAW=13
  직렬(겹침 없음, 지연 합) = 25
```

```svg
<svg viewBox="0 0 760 396" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">같은 5 명령 (Part A 실제 출력): 위 = Tomasulo+ROB 21 사이클, 아래 = 스코어보드 24 사이클</text>
  <g fill="currentColor" font-size="12"><text x="162" y="44" text-anchor="middle">1</text><text x="186" y="44" text-anchor="middle">2</text><text x="210" y="44" text-anchor="middle">3</text><text x="234" y="44" text-anchor="middle">4</text><text x="258" y="44" text-anchor="middle">5</text><text x="282" y="44" text-anchor="middle">6</text><text x="306" y="44" text-anchor="middle">7</text><text x="330" y="44" text-anchor="middle">8</text><text x="354" y="44" text-anchor="middle">9</text><text x="378" y="44" text-anchor="middle">10</text><text x="402" y="44" text-anchor="middle">11</text><text x="426" y="44" text-anchor="middle">12</text><text x="450" y="44" text-anchor="middle">13</text><text x="474" y="44" text-anchor="middle">14</text><text x="498" y="44" text-anchor="middle">15</text><text x="522" y="44" text-anchor="middle">16</text><text x="546" y="44" text-anchor="middle">17</text><text x="570" y="44" text-anchor="middle">18</text><text x="594" y="44" text-anchor="middle">19</text><text x="618" y="44" text-anchor="middle">20</text><text x="642" y="44" text-anchor="middle">21</text><text x="666" y="44" text-anchor="middle">22</text><text x="690" y="44" text-anchor="middle">23</text><text x="714" y="44" text-anchor="middle">24</text></g>
  <text x="10" y="44" fill="currentColor" font-weight="bold">Tomasulo</text>
  <text x="10" y="65" fill="currentColor" font-family="monospace">0 DIV  r1, r2, r3</text><rect x="151" y="52" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="162" y="65" text-anchor="middle" fill="currentColor">I</text><rect x="175" y="52" width="286" height="18" class="f-acc" stroke="currentColor"/><rect x="463" y="52" width="22" height="18" class="f-ok" stroke="currentColor"/><text x="474" y="65" text-anchor="middle" fill="currentColor">W</text><rect x="487" y="52" width="22" height="18" class="f-warn" stroke="currentColor"/><text x="498" y="65" text-anchor="middle" fill="currentColor">C</text>
  <text x="10" y="87" fill="currentColor" font-family="monospace">1 ADD  r4, r1, r5</text><rect x="175" y="74" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="186" y="87" text-anchor="middle" fill="currentColor">I</text><path d="M200 83 L484 83" stroke="currentColor"/><rect x="487" y="74" width="46" height="18" class="f-acc" stroke="currentColor"/><rect x="535" y="74" width="22" height="18" class="f-ok" stroke="currentColor"/><text x="546" y="87" text-anchor="middle" fill="currentColor">W</text><rect x="559" y="74" width="22" height="18" class="f-warn" stroke="currentColor"/><text x="570" y="87" text-anchor="middle" fill="currentColor">C</text>
  <text x="10" y="109" fill="currentColor" font-family="monospace">2 MUL  r5, r6, r7</text><rect x="199" y="96" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="210" y="109" text-anchor="middle" fill="currentColor">I</text><rect x="223" y="96" width="142" height="18" class="f-acc" stroke="currentColor"/><rect x="367" y="96" width="22" height="18" class="f-ok" stroke="currentColor"/><text x="378" y="109" text-anchor="middle" fill="currentColor">W</text><path d="M392 105 L580 105" stroke="currentColor" stroke-dasharray="3 3"/><rect x="583" y="96" width="22" height="18" class="f-warn" stroke="currentColor"/><text x="594" y="109" text-anchor="middle" fill="currentColor">C</text>
  <text x="10" y="131" fill="currentColor" font-family="monospace">3 LD   r1, 0(r0)</text><rect x="223" y="118" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="234" y="131" text-anchor="middle" fill="currentColor">I</text><rect x="247" y="118" width="70" height="18" class="f-acc" stroke="currentColor"/><rect x="319" y="118" width="22" height="18" class="f-ok" stroke="currentColor"/><text x="330" y="131" text-anchor="middle" fill="currentColor">W</text><path d="M344 127 L604 127" stroke="currentColor" stroke-dasharray="3 3"/><rect x="607" y="118" width="22" height="18" class="f-warn" stroke="currentColor"/><text x="618" y="131" text-anchor="middle" fill="currentColor">C</text>
  <text x="10" y="153" fill="currentColor" font-family="monospace">4 ADDI r6, r6, 1</text><rect x="247" y="140" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="258" y="153" text-anchor="middle" fill="currentColor">I</text><rect x="271" y="140" width="46" height="18" class="f-acc" stroke="currentColor"/><path d="M320 149 L340 149" stroke="currentColor" stroke-dasharray="3 3"/><rect x="343" y="140" width="22" height="18" class="f-ok" stroke="currentColor"/><text x="354" y="153" text-anchor="middle" fill="currentColor">W</text><path d="M368 149 L628 149" stroke="currentColor" stroke-dasharray="3 3"/><rect x="631" y="140" width="22" height="18" class="f-warn" stroke="currentColor"/><text x="642" y="153" text-anchor="middle" fill="currentColor">C</text>
  <g fill="currentColor" font-size="12"><text x="162" y="194" text-anchor="middle">1</text><text x="186" y="194" text-anchor="middle">2</text><text x="210" y="194" text-anchor="middle">3</text><text x="234" y="194" text-anchor="middle">4</text><text x="258" y="194" text-anchor="middle">5</text><text x="282" y="194" text-anchor="middle">6</text><text x="306" y="194" text-anchor="middle">7</text><text x="330" y="194" text-anchor="middle">8</text><text x="354" y="194" text-anchor="middle">9</text><text x="378" y="194" text-anchor="middle">10</text><text x="402" y="194" text-anchor="middle">11</text><text x="426" y="194" text-anchor="middle">12</text><text x="450" y="194" text-anchor="middle">13</text><text x="474" y="194" text-anchor="middle">14</text><text x="498" y="194" text-anchor="middle">15</text><text x="522" y="194" text-anchor="middle">16</text><text x="546" y="194" text-anchor="middle">17</text><text x="570" y="194" text-anchor="middle">18</text><text x="594" y="194" text-anchor="middle">19</text><text x="618" y="194" text-anchor="middle">20</text><text x="642" y="194" text-anchor="middle">21</text><text x="666" y="194" text-anchor="middle">22</text><text x="690" y="194" text-anchor="middle">23</text><text x="714" y="194" text-anchor="middle">24</text></g>
  <text x="10" y="194" fill="currentColor" font-weight="bold">스코어보드</text>
  <text x="10" y="215" fill="currentColor" font-family="monospace">0 DIV  r1, r2, r3</text><rect x="151" y="202" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="162" y="215" text-anchor="middle" fill="currentColor">I</text><rect x="175" y="202" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="186" y="215" text-anchor="middle" fill="currentColor">R</text><rect x="199" y="202" width="286" height="18" class="f-acc" stroke="currentColor"/><rect x="487" y="202" width="22" height="18" class="f-ok" stroke="currentColor"/><text x="498" y="215" text-anchor="middle" fill="currentColor">W</text>
  <text x="10" y="237" fill="currentColor" font-family="monospace">1 ADD  r4, r1, r5</text><rect x="175" y="224" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="186" y="237" text-anchor="middle" fill="currentColor">I</text><path d="M200 233 L508 233" stroke="currentColor"/><rect x="511" y="224" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="522" y="237" text-anchor="middle" fill="currentColor">R</text><rect x="535" y="224" width="46" height="18" class="f-acc" stroke="currentColor"/><rect x="583" y="224" width="22" height="18" class="f-ok" stroke="currentColor"/><text x="594" y="237" text-anchor="middle" fill="currentColor">W</text>
  <text x="10" y="259" fill="currentColor" font-family="monospace">2 MUL  r5, r6, r7</text><rect x="199" y="246" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="210" y="259" text-anchor="middle" fill="currentColor">I</text><rect x="223" y="246" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="234" y="259" text-anchor="middle" fill="currentColor">R</text><rect x="247" y="246" width="142" height="18" class="f-acc" stroke="currentColor"/><path d="M392 255 L532 255" stroke="currentColor" stroke-dasharray="3 3"/><rect x="535" y="246" width="22" height="18" class="f-ok" stroke="currentColor"/><text x="546" y="259" text-anchor="middle" fill="currentColor">W</text>
  <text x="10" y="281" fill="currentColor" font-family="monospace">3 LD   r1, 0(r0)</text><rect x="511" y="268" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="522" y="281" text-anchor="middle" fill="currentColor">I</text><rect x="535" y="268" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="546" y="281" text-anchor="middle" fill="currentColor">R</text><rect x="559" y="268" width="70" height="18" class="f-acc" stroke="currentColor"/><rect x="631" y="268" width="22" height="18" class="f-ok" stroke="currentColor"/><text x="642" y="281" text-anchor="middle" fill="currentColor">W</text>
  <text x="10" y="303" fill="currentColor" font-family="monospace">4 ADDI r6, r6, 1</text><rect x="607" y="290" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="618" y="303" text-anchor="middle" fill="currentColor">I</text><rect x="631" y="290" width="22" height="18" class="f-mut" stroke="currentColor"/><text x="642" y="303" text-anchor="middle" fill="currentColor">R</text><rect x="655" y="290" width="46" height="18" class="f-acc" stroke="currentColor"/><rect x="703" y="290" width="22" height="18" class="f-ok" stroke="currentColor"/><text x="714" y="303" text-anchor="middle" fill="currentColor">W</text>
  <text x="10" y="332" fill="currentColor">I = issue · R = 피연산자 읽기 · 막대 = 실행 · W = 결과 쓰기 · C = commit</text>
  <text x="10" y="350" fill="currentColor">실선 = 피연산자/FU 대기, 점선 = CDB·commit 순서(위) / WAR(아래) 대기</text>
  <text x="10" y="368" fill="currentColor">스코어보드: 3 은 WAW(r1) 로 issue 가 16 까지, 2 는 WAR(r5) 로 쓰기가 17 까지,</text>
  <text x="10" y="386" fill="currentColor">4 는 ADD FU 를 1 이 쓰기 전까지 잡고 있어 20 에 issue (구조 해저드).</text>
</svg>
```

**스코어보드가 멈춘 이유를 하나씩** (stall 카운터는 "시도했다가 막힌 사이클 수"):

| 카운터 | 누가 · 언제 | 원인 |
|---|---|---|
| WAW = 12 | LD(3) issue 시도, 사이클 4–15 | r1 을 쓸 DIV 가 아직 활성. 리네이밍이 없으니 두 쓰기의 순서를 보장할 방법은 "먼저 것이 끝날 때까지 뒤 것을 안 들인다" 뿐. **in-order issue 라 뒤의 ADDI 도 같이 멈춘다** |
| WAR = 5 | MUL(2) 쓰기 시도, 11–14 · 16 | ADD(1) 이 옛 r5 를 아직 안 읽음(ADD 는 r1 을 기다리느라 피연산자 읽기를 못 함). 15 는 DIV 가 결과 버스를 써서 시도 자체가 없었다 |
| RAW = 13 | ADD(1) 피연산자 읽기, 3–15 | DIV 결과 대기 — **Tomasulo 도 똑같이 기다린다**(ADD x.s = 15). RAW 는 진짜 의존이라 리네이밍이 못 없앤다 |
| struct = 3 | ADDI(4) issue, 17–19 | 스코어보드 FU 는 issue 부터 write 까지 잡힌다(예약 스테이션 = 버퍼가 없다). ADD FU 를 명령 1 이 19 까지 점유 |

두 모델의 차이를 한 줄로: 스코어보드는 **이름(레지스터 번호) 충돌**을 시간으로 풀고(멈춤), Tomasulo 는 **저장 공간(RS 의 값 복사 + ROB 엔트리)** 으로 푼다. 대가는 하드웨어: 모든 RS 에 값 필드와 태그 비교기, 방송 버스. 스코어보드의 장점은 작다는 것 — 그래서 in-order 코어와 GPU 류 설계에서 "레지스터가 준비됐나" 를 추적하는 스코어보드 개념은 지금도 쓰인다(세부 구현은 제품마다 다르고 공개 정도도 다르다).

**ROB 없는 모드도 예외만 없으면 맞다.** WAW 테스트 `DIV r1,… / ADDI r1,r0,42 / ADD r2,r1,r1`: ADDI 가 먼저 CDB 를 타서 r1 = 42 를 쓰고 상태표를 비운다. 12 사이클 뒤 DIV 가 방송할 때 상태표[r1] 은 더 이상 DIV 가 아니므로 **쓰지 않는다** (cdb_write 의 조건). 이게 360/91 원래 알고리즘의 WAW 처리다. 결과는 세 모델 다 r1 = 42, r2 = 84.

**무작위 검증** (실제 출력):

```text
  random 2000 (예외 없음): ROB 불일치 0, 타임라인 위반 0, 교착 0, ROB 없음 불일치 0, 스코어보드 불일치 0, 비순차 실행이 일어난 프로그램 1258
```

생성기: xorshift32 고정 시드, 4–43 명령, ADD/SUB/ADDI/MUL/LD/ST 섞음(예외 없는 모드는 DIV 를 ADDI 로 바꾸고 LD/ST 는 `imm(r0)`, 주소 0–15 로 겹치게). 설정도 무작위(지연 ADD 1–3 · MUL 2–8 · DIV 4–16 · MEM 1–4, FU 1–2 개, pipelined 여부, RS 1–4, ROB 2–16, W 1–3). 마지막 숫자 **1258** 이 커버리지다 — "x.s 가 프로그램 순서보다 앞선 명령이 하나라도 있었던 프로그램" 수. 비순차 실행이 실제로 일어나지 않았다면 "불일치 0" 은 의미가 없다 (CHECK: > 500).

### Part B — 정밀 예외는 commit 에서

`commit` 의 본체는 짧다:

```c
if (!R->done || R->done_cyc >= t) break;          /* 머리가 안 끝났으면 뒤도 못 나간다 */
if (R->exc != EXC_NONE) { s->exc_idx = R->idx; s->exc = R->exc; flush_all(s, t); return; }
if (s->prog[R->idx].op == OP_ST) s->a.mem[R->addr] = R->val;
else if (R->rd >= 0) { s->a.r[R->rd] = R->val; if (s->regstat[R->rd] == h) s->regstat[R->rd] = -1; }
```

정밀 예외의 정의(ARCH-M10): 예외 시점에 "앞의 명령은 모두 완료, 예외 명령과 그 뒤는 하나도 반영 안 됨". ROB 는 이걸 **구조로** 보장한다 — 아키텍처 상태(레지스터 파일 · 메모리)에 쓰는 곳이 commit 하나뿐이고, commit 은 프로그램 순서다. 실행이 아무리 뒤죽박죽이어도 뒤죽박죽인 건 ROB · RS 안의 **추측(speculative) 상태**뿐이다.

**DIV0 프로그램** (실제 출력, 초기 r[i] = 10·i):

```text
  [ROB] DIV0 프로그램
   0  DIV  r1, r2, r0      1    2   13   14  EXC  (DIV0 발견)
   1  ADD  r3, r4, r5      2    3    4    5 flush
   2  ST   r3, 5(r0)       3    6    6    7 flush
   3  MUL  r6, r6, r6      4    5   10   11 flush
  [ROB] exc at #0 (DIV0) cycle 15, r3=30 mem[5]=1005 flushed=3
  [no ROB] DIV0 프로그램
   0  DIV  r1, r2, r0      1    2   13    -  EXC  (DIV0 발견)
   1  ADD  r3, r4, r5      2    3    4    5    -
   2  ST   r3, 5(r0)       3    6    6    7    -
   3  MUL  r6, r6, r6      4    5   10   11    -
  [no ROB] exc at #0 (DIV0) cycle 14, r3=90 mem[5]=90 r6=3600 (기준: r3=30 mem[5]=1005 r6=60)
```

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">같은 DIV0 프로그램, 예외를 받는 순간의 아키텍처 상태 (실제 출력)</text>
  <g stroke="currentColor"><path d="M40 60 L700 60"/></g>
  <g fill="currentColor" text-anchor="middle">
    <text x="40" y="52">1</text><text x="216" y="52">5</text><text x="304" y="52">7</text>
    <text x="480" y="52">11</text><text x="612" y="52">14</text><text x="656" y="52">15</text>
  </g>
  <g stroke="currentColor">
    <circle cx="216" cy="60" r="5" class="f-warn"/><circle cx="304" cy="60" r="5" class="f-warn"/>
    <circle cx="480" cy="60" r="5" class="f-warn"/><circle cx="612" cy="60" r="5" class="f-acc"/>
    <circle cx="656" cy="60" r="5" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="160" y="82">ADD r3=90 완료</text><text x="260" y="98">ST mem[5]=90 확정</text>
    <text x="430" y="82">MUL r6=3600 완료</text><text x="560" y="98">DIV0 방송</text>
    <text x="640" y="82">commit</text>
  </g>
  <g stroke="currentColor">
    <rect x="20" y="116" width="340" height="128" rx="5" class="f-ok"/>
    <rect x="380" y="116" width="340" height="128" rx="5" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="32" y="138" font-weight="bold">ROB: 사이클 15, 머리(DIV)에서 받음</text>
    <text x="32" y="160">젊은 3 개의 결과는 ROB 안에만 있었다 → flush</text>
    <text x="32" y="182" font-family="monospace">r3=30  mem[5]=1005  r6=60</text>
    <text x="32" y="204">== 순차 기준(DIV 직전까지) — 정밀</text>
    <text x="32" y="226">EPC = #0, 핸들러 뒤 재실행/건너뛰기 가능</text>
    <text x="392" y="138" font-weight="bold">ROB 없음: 사이클 14, 발견 즉시</text>
    <text x="392" y="160">젊은 3 개가 이미 파일 · 메모리에 씀</text>
    <text x="392" y="182" font-family="monospace">r3=90  mem[5]=90  r6=3600</text>
    <text x="392" y="204">≠ 기준 — 부정확 (되돌릴 기록 없음)</text>
    <text x="392" y="226">"DIV 를 고치고 재실행" 이 불가능</text>
  </g>
</svg>
```

ROB 없는 쪽이 망가지는 방식은 세 가지이고, 테스트가 각각을 따로 고정한다:

| 종류 | 프로그램 | ROB 없음의 결과 | ROB 의 결과 |
|---|---|---|---|
| ① 젊은 명령이 이미 씀 | DIV0 + 짧은 ADD/ST/MUL | r3 · mem[5] · r6 이 바뀜 (**메모리까지**) | 기준과 같음 |
| ② 오래된 명령이 아직 안 씀 | `DIV r1 (정상) / LD r4, 99(r0)` | LD 예외를 사이클 6 에 받음, **r1 = 10 (옛 값)** | DIV 를 15 에 commit 해 r1 = 14, 그다음 LD 예외 |
| ③ 틀린 명령을 보고 | `DIV r1,r2,r0 / LD r4,99(r0)` (둘 다 폴트) | 시간상 먼저인 **LD(#1)** 를 보고 | 프로그램 순서상 먼저인 **DIV0(#0)** |

③ 은 ARCH-M10 Q2 의 "젊은 명령의 ILLEGAL 이 ID 에서 늙은 LD 의 DMEM 보다 먼저 보인다" 함정이 OoO 에서는 **훨씬 흔해진다**는 뜻이다. 해법은 같다: 발견한 곳에서는 표시만 하고, 순서를 아는 곳(ROB 머리)에서 받는다.

**무작위 3000 (실제 출력):**

```text
  random 3000: 예외 있는 프로그램 1955 | ROB: 불일치 0, 타임라인 위반 0 | ROB 없음: 부정확 1483 (그중 틀린 명령 보고 72)
```

DIV 의 1/4 은 제수 r0, LD/ST 의 1/8 은 임의 레지스터 기준(대개 범위 밖). ROB 모드는 예외 번호 · 종류 · 레지스터 · 메모리 · commit 개수가 모두 순차 기준과 일치. ROB 없는 모드는 예외가 있는 1955 개 중 1483 개(76%)가 부정확 — 나머지는 우연히 맞은 것(예외 시점에 젊은 명령이 아무것도 안 썼거나 쓴 값이 기준과 같음)이다.

**commit 폭 W** — 끝난 채 머리 뒤에서 기다리던 엔트리(Part A 의 2, 3, 4)가 한꺼번에 나간다 (실제 출력, 결과는 셋 다 기준과 같음):

```text
  commit W=1: cycles=21  cmt = 15 18 19 20 21
  commit W=2: cycles=19  cmt = 15 18 18 19 19
  commit W=4: cycles=18  cmt = 15 18 18 18 18
```

**store 는 왜 commit 때 쓰나.** 메모리는 되돌릴 수 없다(MMIO 레지스터 쓰기라면 부작용까지 있다). 그래서 ST 는 execute 에서 주소 · 데이터만 확정하고 ROB 에 들고 있다가 commit 에 쓴다. 실제 코어는 commit 된 store 를 **store buffer** 에 넣고 나중에 캐시로 내보낸다(그래서 store→load 재배치가 보이고 ARCH-A05 의 SB 리트머스가 (0,0) 을 낸다). 이 모델의 LD 는 더 오래된 ST 가 모두 확정될 때까지 기다리고, 같은 주소면 그 ST 의 값을 받는다(store-to-load forwarding, 연습 4).

**분기 미스 예측 = 같은 flush 경로.** 미스 예측된 분기 뒤의 명령은 "예외 뒤의 명령" 과 똑같이 버려야 할 추측 상태다. 가장 단순한 설계는 분기도 commit 에서 확인하고 flush 한다(이 모델이 예외에 쓰는 방식). 비용은 "발견 → commit" 사이클과 그동안 issue 된 명령 수다 — 연습 3 에서 DIV0 를 x.s = 2 에 알고도 15 에 받고, ROB 가 클수록 더 많이(최대 13 개) 버리는 것을 잰다. 실제 고성능 코어는 분기를 **execute 에서** 해결하고 그 시점의 상태표 스냅샷(checkpoint)으로 즉시 되돌려 이 지연을 줄인다(구현 세부는 코어마다 다르다).

### Part C — ROB · RS 가 IPC 를 묶는 방식

실제 출력:

```text
  IPC vs ROB size (RS=16 each)          4      8     16     32     64
  chain  MUL/ADD dependent          0.199  0.199  0.199  0.199  0.199
  indep  MUL x4 independent         0.440  0.845  0.938  0.938  0.938
  dot    LD,LD,MUL,ADD              0.432  0.638  0.889  0.889  0.889
  IPC vs RS per class (ROB=64)          1      2      4      8     16
  chain  MUL/ADD dependent          0.199  0.199  0.199  0.199  0.199
  indep  MUL x4 independent         0.125  0.249  0.492  0.938  0.938
  dot    LD,LD,MUL,ADD              0.353  0.632  0.889  0.889  0.889
  scoreboard (FU 1 each, same lat)  chain=0.143 indep=0.111 dot=0.299
```

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">IPC vs ROB 크기 (RS 16/클래스, 실제 출력) — 점선 = Little 의 법칙 예측 R/9 (indep)</text>
  <g stroke="currentColor" fill="none">
    <path d="M60 250 L500 250"/><path d="M60 250 L60 40.0"/>
    <path d="M60 200 L500 200" stroke-dasharray="1 4"/>
    <path d="M60 150 L500 150" stroke-dasharray="1 4"/>
    <path d="M60 100 L500 100" stroke-dasharray="1 4"/>
    <path d="M60 50 L500 50" stroke-dasharray="1 4"/>
  </g>
  <g fill="currentColor" text-anchor="end"><text x="54" y="254">0.00</text><text x="54" y="204">0.25</text><text x="54" y="154">0.50</text><text x="54" y="104">0.75</text><text x="54" y="54">1.00</text></g>
  <g fill="currentColor" text-anchor="middle"><text x="80" y="268">4</text><text x="180" y="268">8</text><text x="280" y="268">16</text><text x="380" y="268">32</text><text x="480" y="268">64</text><text x="280" y="286">ROB 엔트리 수 (로그 간격)</text></g>
  <path d="M80 161.1 L180 72.2 L280 50.0 L380 50.0 L480 50.0" stroke="currentColor" fill="none" stroke-width="1" stroke-dasharray="5 4"/>
  <path d="M80 162.0 L180 81.0 L280 62.4 L380 62.4 L480 62.4" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M80 163.6 L180 122.4 L280 72.2 L380 72.2 L480 72.2" stroke="currentColor" fill="none" stroke-width="2" stroke-dasharray="8 3"/>
  <path d="M80 210.2 L180 210.2 L280 210.2 L380 210.2 L480 210.2" stroke="currentColor" fill="none" stroke-width="2"/>
  <g stroke="currentColor"><circle cx="80" cy="162.0" r="4" class="f-acc"/><circle cx="180" cy="81.0" r="4" class="f-acc"/><circle cx="280" cy="62.4" r="4" class="f-acc"/><circle cx="380" cy="62.4" r="4" class="f-acc"/><circle cx="480" cy="62.4" r="4" class="f-acc"/></g>
  <g stroke="currentColor"><circle cx="80" cy="163.6" r="4" class="f-warn"/><circle cx="180" cy="122.4" r="4" class="f-warn"/><circle cx="280" cy="72.2" r="4" class="f-warn"/><circle cx="380" cy="72.2" r="4" class="f-warn"/><circle cx="480" cy="72.2" r="4" class="f-warn"/></g>
  <g stroke="currentColor"><circle cx="80" cy="210.2" r="4" class="f-ok"/><circle cx="180" cy="210.2" r="4" class="f-ok"/><circle cx="280" cy="210.2" r="4" class="f-ok"/><circle cx="380" cy="210.2" r="4" class="f-ok"/><circle cx="480" cy="210.2" r="4" class="f-ok"/></g>
  <g fill="currentColor"><text x="508" y="54">indep (독립 MUL) 0.938</text><text x="508" y="88">dot (LD,LD,MUL,ADD) 0.889</text><text x="508" y="202">chain (의존 사슬) 0.199</text></g>
  <g fill="currentColor"><text x="508" y="234">스코어보드: dot 0.299,</text><text x="508" y="250">chain 0.143, indep 0.111</text></g>
  <text x="90" y="184" fill="currentColor">ROB 4: 4/9 = 0.44</text>
</svg>
```

**Little 의 법칙**: 평균 점유 = 처리율 × 평균 체류 시간. 이 모델에서 ROB 엔트리의 체류 = issue(t) 부터 commit 까지, 그리고 commit 으로 비운 칸은 **다음 사이클**부터 쓸 수 있다.

- **indep** (독립 MUL): issue t → x.s t+1 → x.e t+6 → CDB t+7 → commit t+8. IPC 1 을 유지하려면 사이클 시작 때 이미 8 개가 들어 있고 하나가 더 들어와야 하므로 **ROB ≥ 9**. ROB R < 9 면 IPC ≈ R/9: R = 4 → 0.444 (측정 0.440), R = 8 → 0.889 (0.845 — 채움 · 비움 구간 때문에 조금 낮음). 120 명령의 상한은 120 / (120 + 8) = **0.938** (마지막 MUL 이 120 에 issue, 128 에 commit) — ROB 16 부터 정확히 이 값.
- **RS 는 issue 부터 CDB 까지** 잡힌다(H&P 방식): t … t+7, 다음 사이클 재사용 → MUL RS 하나가 8 사이클에 1 명령. RS k 개면 IPC ≈ k/8: 1 → 0.125, 2 → 0.249, 4 → 0.492 (측정값 그대로). RS 를 실행 시작 때 해제하는 설계(실행 중 결과 대기는 FU 파이프라인 래치가 맡음)면 이 숫자가 달라진다 — 실제 코어의 "스케줄러 크기" 가 ROB 보다 훨씬 작아도 되는 이유.
- **chain**: 모든 명령이 앞 결과를 기다린다. 한 단계 = 지연 + 1 (CDB 다음 사이클 실행) → MUL 7, ADD 3, 평균 5 → **0.2**. ROB 를 64 로 키우면 점유는 3.75 → 26.9 로 늘지만(연습 1) 기다리는 명령만 쌓일 뿐 IPC 는 그대로. **큰 창은 독립적인 일을 찾을 때만 도움이 된다.**
- **dot**: 반복마다 4 명령인데 CDB 방송도 4 번(LD, LD, MUL, ADD) → CDB 와 issue 가 둘 다 1/사이클로 포화. ADD 누적 사슬은 3 사이클/반복이라 병목이 아니다. LD 3 + MUL 6 + ADD 를 가리려면 체류가 길어(평균 14.6) ROB 16 이 필요하다(연습 1 에서 최소 ROB = 16 을 확인).
- **스코어보드**: indep 0.111 — FU 가 issue → write 동안 잡히고 같은 목적지 재사용마다 WAW 정지. dot 0.299 — MEM FU 1 개가 LD 마다 6 사이클 잡힘. chain 0.143 — 같은 의존 사슬인데 Tomasulo(0.199)보다 느린 건 "피연산자 읽기" 단계가 따로 있어(결과 쓰기 → 다음 사이클 읽기 → 다음 사이클 실행) 단계마다 한 사이클이 더 들기 때문.

**실제 코어와 연결** (숫자는 공개 자료 기준으로 흔히 인용되는 것, 세부 미검증):

| 코어 | 실행 방식 | 이 레슨과의 관계 |
|---|---|---|
| Cortex-M0+/M3/M4 | in-order, 2–3 단 | ROB 없음. 여러 사이클 명령(나눗셈 등)은 끝날 때까지 기다리는 단순한 구조. 정밀성은 in-order 로 자동 (단 쓰기 버퍼 때문에 imprecise BusFault 는 있다 — ARCH-M10) |
| Cortex-M7, Cortex-A53/A55 | in-order dual-issue (M7 은 6 단으로 알려짐) | 지연이 다른 파이프가 있어 WAW/RAW 추적(스코어보드류)은 필요, 리네이밍 · ROB 는 없음 |
| Cortex-A7x/X, Apple 고성능 코어 | 비순차, 리네이밍 + 큰 ROB | Arm 의 Cortex-A76 소개 자료는 128 엔트리급 instruction window 를 말하고, Apple M1 고성능 코어는 공개 측정 기사에서 600 엔트리 이상으로 추정됐다. M6 수치는 공개되지 않았다 |

현대 OoO 코어가 이 모델과 다른 점: ROB 에 값을 두지 않고 **물리 레지스터 파일(PRF) + RAT** 로 리네이밍하고 ROB 는 순서 · 상태만 든다, CDB 가 여러 개(issue 폭만큼), load/store 큐와 **추측적 load**(먼저 실행하고 주소 충돌이면 flush), 분기를 execute 에서 checkpoint 로 복구. 개념(rename → OoO execute → in-order retire)은 같다.

**M6 에서 창 크기를 재 보기 — `demo_rob_size.c`** (arm64 인라인 asm, 타이밍은 여기서만): 두 개의 256 MB 무작위 포인터 체이스 `ldr A ← [A]` 와 `ldr B ← [B]` 사이에 필러 N 개를 넣는다. B 의 load 가 A 의 DRAM 미스가 끝나기 전에 창에 들어오면 두 미스가 겹쳐 반복당 ≈ 미스 1 번, 필러가 창을 넘치면 ≈ 미스 2 번. "N 이 얼마일 때 시간이 뛰나" 가 **그 필러에 대해 먼저 바닥나는 자원**의 크기다.

```bash
cc -std=c11 -O2 -Wall -Wextra -D_DARWIN_C_SOURCE demo_rob_size.c -o demo_rob && ./demo_rob
```

실제 출력 (Apple M6, Apple clang 21.0.0, `uptime` load average **16.9–17.4** 로 다른 세션이 돌고 있는 공유 머신 — 숫자가 흔들린다. QoS 를 USER_INTERACTIVE 로 요청했지만 코어 고정이 아니라 힌트일 뿐이다. 반복 100000 회, 5 번의 중앙값 / 최소 / 최대):

```text
filler = nop
     N   ns/iter   min    max
      0    149.5   103.8  157.8
    128    111.9   107.8  117.4
    256    107.6   102.2  108.9
    512    114.3   105.9  143.0
    768    119.0   101.6  152.2
    896    116.0   107.5  150.2
   1024    137.1   124.8  149.7
   1280    175.8   148.7  200.2
   2048    200.4   145.4  215.5
   3072    315.7   228.1  366.9
filler = add x9, x10, #1
     N   ns/iter   min    max
      0    108.1    99.8  114.2
    128    111.9   108.5  122.8
    160    121.1   116.1  141.5
    176    139.4   117.9  184.1
    192    145.3   142.3  182.7
    216    193.3   133.9  215.9
    240    182.1   172.1  212.8
    256    211.2   173.5  218.8
    320    209.3   175.3  216.6
    512    240.4   229.3  256.5
```

(일부 행 생략. 같은 프로그램을 load ≈ 60 에서 먼저 돌렸을 때도 모양은 같았다: add 는 160 ≈ 118 ns → 232 ≈ 214 ns, nop 은 896 까지 ≈ 110–150 ns.)

읽는 법 — **확실히 말할 수 있는 것과 없는 것을 나눈다**:
- N 이 작을 때 ≈ 105–110 ns = 미스 하나(두 체인의 미스가 겹친다). 이 버퍼 크기에서의 DRAM + TLB 미스 지연이다.
- **add 필러**: 160 근처까지 평평하다가 176–256 사이에서 ≈ 2 배(≈ 210 ns, 미스 두 번이 직렬)로 올라선다. 꺾이는 곳 ≈ **170–220 개의 레지스터를 쓰는 명령**. add 는 매번 새 물리 레지스터가 필요하므로 정수 물리 레지스터(또는 그와 묶인 rename 자원)가 먼저 바닥났다고 해석하는 게 일반적이지만, 이 측정만으로 어느 자원인지 단정할 수는 없다.
- **nop 필러**: 900 개 가까이까지 평평하다 — add 보다 **훨씬 큰 창**. 레지스터를 안 쓰는 명령은 ROB 류 자원만 쓰기 때문이라고 보는 것이 자연스럽다. 1024 이후 오르지만, 이 크기에선 필러 수천 개를 실행하는 시간 자체(수백 사이클)가 섞여 창 크기와 깨끗이 분리되지 않는다. 그래서 "M6 ROB = ○○○" 같은 숫자는 이 데이터로 주장하지 않는다.
- 교훈은 Part C 와 같다: **창의 실제 크기 = 여러 자원(ROB, 물리 레지스터, 스케줄러, load/store 큐) 중 그 코드가 먼저 바닥내는 것.** 시뮬레이터에서 RS 1 개가 ROB 64 를 무력화한 것과 같은 현상을 실제 칩에서 본 것이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 34/34 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san   # 깨끗, 출력 동일
```

(Apple M6, Apple clang 21.0.0. 단일 스레드 결정적 모델 — 타이밍 없음, TSan 해당 없음. 세 번 실행한 출력의 md5 가 같다.)

설계 메모:
- **"t−1 까지 생긴 것만" 규칙을 한 군데도 어기지 않는다.** 단계를 뒤에서부터(commit → CDB → execute → issue) 처리하고, 모든 조건을 `x < t` 로 쓴다. 예외는 하나 — issue 가 같은 사이클 CDB 값을 잡는 bypass — 이고, issue 한 명령은 어차피 t+1 에야 실행하므로 타이밍이 바뀌지 않는다. 이 규칙이 한 곳이라도 깨지면 처리 순서에 따라 결과가 달라진다(처음 작성 때 LD 의 "오래된 ST 확정" 검사가 `done` 만 보고 `done_cyc < t` 를 안 봐서 연습 4 의 LD 가 ST 확정과 같은 사이클에 시작했다 — 손계산과 대조해 잡았다).
- **ROB 슬롯 번호 = 태그.** ROB 없는 모드도 같은 슬롯을 태그로 빌려 쓰고 순서대로 돌려받는다(`retire_tags`, 상태에 영향 없음). 두 모드의 차이가 정확히 "아키텍처 상태를 언제 쓰나" 하나만 되도록 — 비교 실험의 기본.
- **예외 명령도 CDB 를 탄다** (값 0 + 예외 표시). 그 값을 받은 젊은 명령은 쓰레기로 계산하지만 어차피 flush 된다. 실제 하드웨어도 "완료 + 예외 표시" 를 ROB 에 기록하는 경로가 필요하다.
- **스코어보드의 MEM FU 는 1 개로 고정** — FU 가 issue → write 동안 잡히므로 메모리 연산이 자동으로 직렬화된다. 메모리 순서 문제를 피하는 가장 단순한 방법이고, 그래서 dot 커널에서 특히 느리다.

## 7. 자주 하는 실수

1. **commit 에서 "상태표가 나를 가리킬 때만" 레지스터 파일에 쓴다** — 그건 ROB 없는 Tomasulo 의 WAW 규칙이다. ROB 모드에서 그러면 `DIV r1 / LD r1, 99(r0)` 처럼 젊은 쓰기가 예외로 버려질 때 파일에 DIV 결과가 없다 → 부정확. 고친 코드: 항상 `s->a.r[R->rd] = R->val;`, 상태표만 조건부로 −1.
2. **issue 때 "생산자가 끝났으면 레지스터 파일에서 읽는다"** — 끝났어도(done) commit 전이면 값은 ROB 에만 있다. 파일을 읽으면 한 세대 전 값. 고친 코드: `else if (s->rob[tag].done) *v = s->rob[tag].val;`
3. **CDB 에서 깨운 RS 가 같은 사이클에 실행한다** — 방송 → 비교 → 선택이 한 사이클에 다 들어가면 그만큼 클럭이 길어진다. 이 모델은 `rdy = t` 로 다음 사이클부터. 실제 코어는 wakeup-select 루프를 1 사이클에 맞추는 것이 핵심 타이밍 경로라 이 부분을 공들여 설계한다(back-to-back 실행).
4. **예외를 발견한 순간 받는다** — ROB 가 있어도 그러면 ROB 없는 모드와 같아진다(③ 틀린 명령 보고). 표시만 하고 머리에서 받는다.
5. **ST 를 execute 때 메모리에 쓴다** — 레지스터는 ROB 로 보호했는데 메모리가 새서 ① 이 그대로 남는다(DIV0 프로그램의 mem[5] = 90). store 는 commit 뒤에만.
6. **스코어보드 WAR 검사에서 "피연산자를 아직 안 읽음" 을 `ro == 0` 으로만 본다** — 같은 사이클에 읽기와 쓰기가 겹칠 때 처리 순서에 결과가 달려 버린다. `ro == 0 || ro >= t`.
7. **"ROB 를 키우면 다 빨라진다"** — chain 커널은 ROB 4 와 64 가 같다. 큰 창은 독립 명령이 있을 때만 쓸모가 있고, 비용(엔트리마다 저장 공간, 방송 비교기, flush 때 버리는 일)은 늘 낸다.

## 8. 면접 Q&A

<details><summary>Q1. 레지스터 리네이밍은 어떤 해저드를 없애고, 어떤 건 못 없애나?</summary>

WAR 과 WAW 는 **이름**이 겹쳐서 생기는 가짜 의존(name dependence)이다. 리네이밍은 쓰기마다 새 저장소(ROB 엔트리 또는 물리 레지스터)를 주고, 읽는 쪽은 issue 때 "어느 저장소" 인지(태그)나 값을 챙긴다. 그래서 뒤 명령이 같은 이름에 써도 앞 명령은 영향이 없다. Part A 에서 스코어보드의 WAW 정지 12 사이클 · WAR 지연 5 사이클이 Tomasulo 에선 0. RAW 는 **진짜 데이터 의존**이라 못 없앤다 — ADD 는 두 모델 모두 DIV 결과를 기다렸다(x.s 15 / ro 16).

꼬리질문: "그럼 RAW 는 영영 못 줄이나?" → 기다리는 동안 다른 일을 하는 것(OoO 의 본질)이 첫째, 지연 자체를 줄이는 것(포워딩 · 빠른 FU)이 둘째. 연구 · 일부 제품에서 값 예측(value prediction)도 거론되지만 일반적이지 않다. 메모리를 통한 RAW(ST → LD)는 store-to-load forwarding 으로 메모리 왕복만 줄인다(연습 4).
</details>

<details><summary>Q2. ROB 없이 Tomasulo 만 있으면 정밀 예외가 왜 깨지나? 정확히 무엇이 깨지나?</summary>

결과가 CDB 때 바로 레지스터 파일 · 메모리에 들어가므로 아키텍처 상태가 **완료 순서**로 바뀐다. 예외가 나는 순간 세 가지가 섞인다: ① 젊은 명령이 이미 썼다(DIV0 뒤 ADD/ST/MUL — 메모리 포함), ② 오래된 명령이 아직 안 썼다(젊은 LD 폴트 때 DIV 의 r1 이 옛 값), ③ 시간상 먼저 발견된 젊은 예외를 보고한다. 되돌릴 기록이 없으니 "예외 명령 직전 상태" 를 만들 수 없다. IBM 360/91 이 imprecise interrupt 로 유명한 이유다. 무작위 1955 개 중 1483 개가 부정확했다.

꼬리질문: "ROB 말고 다른 해법은?" → Smith & Pleszkun(1985)이 ROB 와 함께 **history buffer**(덮어쓴 옛 값을 기록해 두고 예외 때 되감기), **future file**(추측용 파일과 아키텍처 파일을 따로) 을 제시했다. 현대 코어는 PRF + RAT checkpoint + ROB 조합. 또 하나의 "해법" 은 정밀성을 포기하고 문서화하는 것 — 일부 FP 예외나 Arm 의 비동기 SError 처럼 "어디서 났는지 정확히 모름" 을 허용하는 경우가 아키텍처에 남아 있다.
</details>

<details><summary>Q3. 스코어보드와 Tomasulo 의 차이를 한 표로.</summary>

| | 스코어보드 (CDC 6600) | Tomasulo (+ROB) |
|---|---|---|
| WAW | issue 정지 | 리네이밍 (상태표가 최신 생산자만 가리킴) |
| WAR | 결과 쓰기 지연 | issue 때 값/태그 복사 |
| 피연산자 | 레지스터 파일에서, 생산자가 쓴 뒤 | RS 에 복사, CDB 방송으로 받음 |
| 대기 장소 | FU 자체 (issue → write 점유) | 예약 스테이션 (버퍼) |
| 결과 전달 | 레지스터 파일 경유 (+1 사이클) | CDB 방송 |
| 정밀 예외 | 없음 | ROB 가 있을 때만 |

이 모델에서 Part A 24 vs 21 사이클, Part C indep 0.111 vs 0.938.

꼬리질문: "스코어보드는 이제 박물관 유물인가?" → 아니다. in-order 코어에서 "이 레지스터가 아직 계산 중인가" 를 비트 하나씩 추적해 스톨을 결정하는 구조를 흔히 스코어보드라고 부르고, 지연이 긴 연산(load, 특수 함수)이 많은 GPU 류 설계에서도 같은 이름의 의존 추적이 쓰인다고 알려져 있다(세부는 공개 자료 범위에서만).
</details>

<details><summary>Q4. ROB 크기는 어떻게 정하나? 키우면 무엇을 내나?</summary>

Little 의 법칙: 필요한 엔트리 ≈ 목표 처리율(IPC) × 명령이 issue 에서 commit 까지 머무는 시간. 이 모델의 6 사이클 MUL 을 IPC 1 로 가리려면 체류 8 + 1 = **9**(연습 1 에서 정확히 9). 실제 코어는 가려야 할 것이 DRAM 미스(수백 사이클)이고 issue 폭이 4–8 이라 수백 엔트리가 된다. 단 ROB 만 키우면 소용없다 — RS/스케줄러, 물리 레지스터, load/store 큐 중 가장 작은 것이 실제 창을 정한다(Part C 의 RS sweep, demo 의 add 필러).

꼬리질문: "비용은?" → 엔트리 저장 공간, 깨우기 방송의 비교기 수와 배선(타이밍 경로), commit/flush 로직, 그리고 **미스 예측 때 버리는 일**(연습 3: ROB 가 클수록 flush 되는 명령이 많다) — 전력 낭비. chain 처럼 의존이 긴 코드는 창이 커도 빨라지지 않는다.
</details>

<details><summary>Q5. 분기 미스 예측은 어디서 복구하나? commit 에서 하면 얼마나 손해인가?</summary>

개념적으로 예외와 같다: 분기 뒤의 명령은 추측 상태이고, 틀렸으면 그 뒤 ROB 엔트리 · RS · 상태표 변경을 모두 버린다. 가장 단순한 방법은 분기가 ROB 머리에 올 때 확인하고 flush(이 모델의 예외 처리 방식). 손해 = "분기 해결 → commit" 대기 + 그동안의 잘못된 경로 issue. 연습 3 에서 DIV0 는 x.s = 2 에 알 수 있는데 15 에 받았고, ROB 16 이면 13 개를 버렸다. 그래서 실제 코어는 분기를 execute 에서 해결하고 분기마다 저장해 둔 상태표 스냅샷(checkpoint)으로 즉시 복구하며, 분기보다 젊은 것만 선택적으로 지운다.

꼬리질문: "checkpoint 를 분기마다 다 저장할 수 있나?" → 개수가 제한되므로 신뢰도가 낮은 분기에만 두거나, 없으면 ROB 를 거꾸로 훑어 상태표를 복원하는 방식을 섞는다고 알려져 있다(구현은 코어마다 다름). 예외는 드물어서 commit 에서 받아도 성능 문제가 없다 — 분기와 예외가 같은 경로를 쓰지만 최적화 대상이 다른 이유.
</details>

<details><summary>Q6. load 가 더 오래된 store 보다 먼저 실행해도 되나?</summary>

주소가 다르면 된다. 문제는 store 주소를 아직 모를 때다. 이 모델은 가장 보수적: 더 오래된 ST 가 **모두** 확정될 때까지 LD 가 기다린다. 게다가 ST 는 데이터까지 준비돼야 "확정" 이라, 연습 4 에서 다른 주소(4)의 LD 도 DIV(12 사이클)를 기다렸다(x.s 18 vs ST 데이터가 일찍 있을 때 6). 개선 순서: ① store 주소와 데이터를 따로 확정(STA/STD 분리) → 주소만 알면 다른 주소 LD 가 지나감, ② 주소를 몰라도 **추측적으로** 먼저 실행하고, 나중에 확정된 store 주소가 같으면 그 load 부터 flush(memory ordering violation), ③ 자주 충돌하는 load–store 쌍을 학습하는 예측기(store set 등).

꼬리질문: "MMIO 레지스터 load 도 추측 실행해도 되나?" → 안 된다. 장치 메모리 읽기는 부작용(FIFO pop, W1C 상태 레지스터)이 있을 수 있다. 그래서 Arm 의 Device 메모리 속성은 추측 접근을 금지하고, 그런 load 는 비추측(non-speculative)이 될 때까지 기다린다(세부 규칙은 Arm ARM 의 메모리 타입 절). 펌웨어에서 MMIO 를 Normal 메모리로 매핑하면 안 되는 이유 중 하나다.
</details>

<details><summary>Q7. 실리콘 검증에서 OoO 코어를 어떻게 검증하나?</summary>

내부 타이밍은 수없이 다르지만 **commit 스트림**은 순차 모델과 같아야 한다 — 이 레슨의 검사 구조 그대로다: 무작위 명령열(고정 시드) → DUT 의 commit 로그(PC, 쓴 레지스터와 값, 메모리 쓰기, 예외)를 명령 집합 시뮬레이터(ISS)와 lock-step 으로 비교. 예외 · 인터럽트 · flush 를 일부러 많이 만들고(이 레슨은 DIV 의 1/4 을 r0 제수로), 커버리지(비순차 실행이 일어난 프로그램 1258/2000 같은)를 측정해 "통과" 가 의미 있는지 확인한다. 내부 불변식(CDB ≤ 1/사이클, commit 순서, ROB 점유 ≤ R)은 assertion 으로 건다.

꼬리질문: "인터럽트는 비동기인데 어디서 받나?" → commit 경계에서. 머리 명령까지 commit 하고 나머지를 flush 한 뒤 진입하면 "그 앞은 모두 완료, 뒤는 하나도 안 됨" 이 된다(ARCH-M10 의 인터럽트 = 다음 명령 PC 규칙). ISS 비교에서는 DUT 가 인터럽트를 받은 commit 지점을 ISS 에 알려 주는 방식으로 맞춘다.
</details>

## 9. 추가 연습

1. **Little 의 법칙은 근사가 아니다.** 사이클마다 ROB 점유를 더한 값과 Σ(commit − issue)가 정확히 같음을 세 커널 × ROB 4–64 에서 확인하라. 그리고 IPC 가 ROB 64 와 같아지는 최소 ROB 를 커널마다 찾고 indep 의 값을 손으로 예측하라.
   <details><summary>답</summary>

   엔트리는 issue 사이클 t 에 들어오고 commit 사이클 c 에 나가므로 "사이클 시작 때 점유" 에 정확히 c − t 번 잡힌다 → Σ점유 == Σ(c − t), 항등식(15 경우 모두 일치). 평균으로 나누면 점유 = IPC × 체류. indep: 체류 8.00, ROB 64 에서 IPC 0.938 × 8.00 = 7.50 = 평균 점유. 최소 ROB: chain 2, **indep 9**(= 체류 8 + 비운 칸은 다음 사이클), dot 16(체류가 14.6 으로 길다). chain 은 ROB 64 에서 평균 점유 26.9 · 체류 135 — 창이 기다리는 명령으로만 찬다.
   </details>
2. **손 예측.** `MUL r1,r2,r3 / ADD r2,r4,r5 / ADD r6,r1,r2 / ST r6,0(r0)` (기본 설정, r[i] = 10·i) 의 Tomasulo 타임라인 5 열과 스코어보드 총 사이클을 종이로 예측하라.
   <details><summary>답</summary>

   Tomasulo: MUL 1/2/7/8/9, ADD(r2) 2/3/4/5/**10**(commit 은 MUL 뒤), ADD(r6) 3/9/10/11/12, ST 4/12/12/13/14 → **14** 사이클. ADD(r2) 는 MUL 이 옛 r2 = 20 을 읽은 뒤라 WAR 걱정 없이 4 에 끝난다. 스코어보드 **16**: ADD(r6) 는 ADD FU 를 ADD(r2) 가 6 까지 잡아서 7 에 issue, MUL 쓰기(9) 뒤 10 에 읽기, ST 는 13 쓰기 뒤 14 에 읽고 16 에 메모리. mem[0] = 20·30 + (40+50) = 690 (두 모델 같음).
   </details>
3. **flush 로 버리는 일.** DIV0 하나 뒤에 독립 ADDI 40 개를 두고(ADD RS 16 으로 넉넉히) ROB 4/8/16/32 에서 flush 된 명령 수, 발견(x.s) → 예외 받은 사이클을 재고 식으로 설명하라.
   <details><summary>답</summary>

   발견 2, 받음 15 (지연 13) — ROB 크기와 무관(DIV 의 지연이 정한다). flush = min(ROB − 1, 13): issue 는 사이클 2–14 에 1 개씩 최대 13 개이고, 머리(DIV)가 막고 있어 ROB 가 차면 거기서 멈춘다. 실제 출력 3, 7, 13, 13. 분기 미스 예측을 commit 에서 복구하면 같은 숫자를 낸다 → execute 시점 복구(checkpoint)의 동기.
   </details>
4. **메모리 의존.** `DIV r1,r2,r3 / ST r1,3(r0) / LD r4,3(r0) / LD r5,4(r0)` 에서 두 LD 의 x.s 와 값을 예측하라. ST 의 데이터를 이미 준비된 r6 으로 바꾸면?
   <details><summary>답</summary>

   ST 는 데이터(r1)가 14 에 방송된 뒤 15 에 실행, 16 에 확정 → 같은 주소 LD 는 17 에 시작해 forwarding 으로 r4 = 14, **다른 주소 LD 도 18** 에 시작(r5 = mem[4] = 1004) — "오래된 ST 가 모두 확정" 규칙이 주소를 모르는 동안 모든 LD 를 막는다. ST 데이터가 r6 이면 ST 가 3 에 실행 · 4 에 확정 → LD 는 5, 6 에 시작, r4 = 60. 개선은 Q6 의 STA/STD 분리와 추측 load.
   </details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr      # solution.c 를 포함하므로 같은 폴더에서
```

실제 출력 (일부):

```text
== 연습 1: Little 의 법칙 (점유 = IPC x 체류) 와 최소 ROB ==
  chain  MUL/ADD dependent   ROB= 4  IPC=0.199  avg occ=3.75  avg resid=18.80  IPC*resid=3.75
  chain  MUL/ADD dependent   ROB=64  IPC=0.199  avg occ=26.91  avg resid=134.98  IPC*resid=26.91
  indep  MUL x4 independent  ROB= 4  IPC=0.440  avg occ=3.52  avg resid=8.00  IPC*resid=3.52
  indep  MUL x4 independent  ROB=64  IPC=0.938  avg occ=7.50  avg resid=8.00  IPC*resid=7.50
  dot    LD,LD,MUL,ADD       ROB= 4  IPC=0.432  avg occ=3.48  avg resid=8.07  IPC*resid=3.48
  dot    LD,LD,MUL,ADD       ROB=64  IPC=0.889  avg occ=12.95  avg resid=14.57  IPC*resid=12.95
  min ROB for full IPC: chain  MUL/ADD dependent   2
  min ROB for full IPC: indep  MUL x4 independent  9
  min ROB for full IPC: dot    LD,LD,MUL,ADD       16
== 연습 2: 손으로 타임라인 예측 ==
  Tomasulo 14 cycles, scoreboard 16 cycles, mem[0]=690
== 연습 3: 예외를 commit 에서 받을 때 버려지는 명령 수 ==
  ROB  flushed  detect(x.s)  taken  delay
    4        3            2     15     13
    8        7            2     15     13
   16       13            2     15     13
   32       13            2     15     13
== 연습 4: store-to-load forwarding 과 보수적 메모리 순서 ==
  ST data from DIV : LD(3) x.s=17, LD(4) x.s=18 | ST data ready: LD(3) x.s=5, LD(4) x.s=6
10/10 passed
```

## 10. 핵심 정리

- OoO = **순서대로 issue(rename) → 순서 없이 실행 · CDB → 순서대로 commit**. 뒤죽박죽인 건 ROB · RS 안의 추측 상태뿐, 아키텍처 상태는 commit 만 쓴다.
- issue 때 피연산자는 세 곳: 레지스터 파일 / 끝났지만 commit 전인 ROB / 아직이면 태그. 목적지 상태표 ← 나 = 리네이밍. **WAR · WAW 소멸, RAW 는 남는다.**
- 스코어보드: WAW → issue 정지, WAR → 쓰기 지연, FU 가 issue~write 점유. Part A 24 vs 21, indep IPC 0.111 vs 0.938.
- 정밀 예외 = 발견은 execute, 받기는 ROB 머리, 젊은 것 flush, store 는 commit 뒤. ROB 없으면 ① 젊은 것이 이미 씀 ② 오래된 것이 아직 안 씀 ③ 틀린 명령 보고 (무작위 1955 중 1483 부정확).
- commit 은 상태표와 무관하게 **항상** 레지스터 파일에 쓴다 (상태표 해제만 조건부).
- Little: 창 크기 ≈ 처리율 × 체류. 6 사이클 MUL 을 IPC 1 로 → ROB 9. RS 가 issue~CDB 를 잡으면 RS k 개 → IPC k/8. 의존 사슬은 창을 키워도 그대로.
- 분기 미스 예측 복구는 예외와 같은 flush 경로. commit 에서 하면 느리다 → 실제 코어는 execute 에서 checkpoint 로.

## 참고자료

- R. M. Tomasulo, "An Efficient Algorithm for Exploiting Multiple Arithmetic Units", *IBM Journal of Research and Development* 11(1), 1967 — 예약 스테이션 · CDB · 태그 (360/91 부동소수점 유닛).
- J. E. Thornton, *Design of a Computer: The Control Data 6600*, 1970 — 스코어보드.
- J. E. Smith, A. R. Pleszkun, "Implementation of Precise Interrupts in Pipelined Processors", ISCA 1985 — ROB, history buffer, future file.
- G. S. Sohi, "Instruction Issue Logic for High-Performance, Interruptible, Multiple Functional Unit, Pipelined Computers", *IEEE Trans. Computers*, 1990 — RUU (ROB + RS 통합).
- J. L. Hennessy, D. A. Patterson, *Computer Architecture: A Quantitative Approach*, 6th ed. — 3 장 (Tomasulo, 하드웨어 추측과 ROB), 부록 C (스코어보드).
- H. Wong, "Measuring Reorder Buffer Capacity" (블로그, 2013) — demo_rob_size.c 의 "두 캐시 미스 사이 필러" 측정법.
- Arm Cortex-A76 / Cortex-M7 기술 소개 자료 (instruction window, 파이프라인 단계 수 — 세부는 각 TRM · 발표 자료 확인 필요).
- 이 노트의 관련 레슨: ARCH-M01 (in-order 파이프라인, Q7 WAR/WAW), ARCH-M02 (포워딩 · 분기 예측), ARCH-M10 (정밀 예외 · EPC · imprecise BusFault), ARCH-A05 · MEM-M07 (store buffer 와 메모리 순서), ARCH-A04 (일관성), MEM-B10 (포인터 체이싱 지연), SIM-M01 (사이클 근사 모델, 다음 단계).
