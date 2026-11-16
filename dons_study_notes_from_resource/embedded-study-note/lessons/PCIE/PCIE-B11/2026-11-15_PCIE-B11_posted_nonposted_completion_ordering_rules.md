---
id: PCIE-B11
time: 60분
updated: 2026-11-15
---
# PCIE-B11 Posted/Non-posted/Completion과 Ordering 규칙

## 학습 목표

- PCIe 트랜잭션을 순서 규칙의 네 종류 — Posted(P), Non-Posted Read(NPR), Non-Posted with Data(NPW), Completion(CPL) — 로 나누고, TLP 의 Fmt/Type 만 보고 어느 종류인지 말한다.
- 순서 규칙 표(Ordering Rules Summary, 행 = 나중 것, 열 = 먼저 것)를 16칸 코드로 옮기고, **어느 칸이 확실하고 어느 칸이 스펙 확인이 필요한지** 출처와 함께 적는다.
- Relaxed Ordering(RO), ID-based Ordering(IDO), Traffic Class(TC), 같은 Transaction ID 의 분할 완료(D5b) 예외를 반영해 "이 도착 순서가 합법인가" 를 판단하는 검사기를 정적 버퍼만으로 짠다.
- "`writel()` 다음 `readl()` 이 쓰기를 flush 한다" 와 "CQE·MSI-X 가 data 보다 먼저 보이면 안 된다" 를 표의 칸 이름(B2a, A2a, D2a)으로 설명한다.
- "Posted 는 Non-Posted 를 앞지를 수 있어야 한다(Yes)" 가 왜 데드락 방지 규칙인지, FIFO 송신기가 실제로 멈추는 2노드 예로 보인다.

## 1. 문제

실리콘 검증·SSD 펌웨어 면접에서 "왜 MMIO 쓰기 뒤에 읽기를 한 번 하나요?", "SSD 가 완료 엔트리를 data 보다 먼저 쓰면 무슨 일이 생기죠?" 가 나오는 이유: **PCIe 는 쓰기를 기다려 주지 않는다(posted)**. 쓰기가 어디쯤 가 있는지, 무엇이 무엇을 앞지를 수 있는지는 오직 순서 규칙(ordering rules)이 정한다.

Don 이 SSD 펌웨어를 할 때 컨트롤러는 호스트 메모리에 read data 를 DMA 로 쓰고(MWr), 이어 Completion Queue Entry(CQE)를 쓰고(MWr), MSI-X 를 쐈다(이것도 MWr). 호스트 드라이버는 반대로 SQ tail 도어벨을 `writel()` 로 쓰고 가끔 `readl()` 로 CSTS 를 읽는다. 이 모든 TLP 가 같은 링크 위에서 섞이는데, 어느 것이 어느 것을 앞질러도 되는지를 이 레슨에서 표 하나로 정리하고 검사기로 만든다.

먼저 종류. 순서 규칙은 TLP 를 "응답을 기다리나, 데이터를 싣나" 로 네 묶음으로 본다.

| 종류 (이 레슨 이름) | TLP 예 (Fmt/Type 은 PCIE-B10) | 응답 | 받는 쪽 credit |
|---|---|---|---|
| Posted Request (**P**) | MWr 32/64, Msg/MsgD (INTx Assert, PME, **MSI/MSI-X 는 그냥 MWr**) | 없음 — 보내면 끝 | P (PH/PD) |
| Non-Posted Read (**NPR**) | MRd, MRdLk, IORd, CfgRd0/1 | CplD (데이터) 또는 Cpl (오류) | NP (NPH) |
| Non-Posted with Data (**NPW**) | IOWr, CfgWr0/1, AtomicOp (FetchAdd/Swap/CAS) | Cpl (상태) / AtomicOp 는 CplD | NP (NPH/NPD) |
| Completion (**CPL**) | Cpl, CplD, CplLk, CplDLk | — | CPL (CplH/CplD) |

credit 이름(PH, NPH, CplH …)과 흐름 제어는 PCIE-M11 에서 다룬다. 여기서는 "받는 쪽이 종류별로 따로 버퍼를 광고한다" 만 알면 된다 — 그래서 한 종류가 막혀도 다른 종류는 지나갈 수 **있고**, 그걸 허용할지 말지가 순서 규칙이다.

순서 규칙 표는 PCIe Base Specification §2.4.1 의 "Ordering Rules Summary" 표다(r3.0 이후 모양; 판마다 표 번호가 다르다). 읽는 법: **행(row) = 나중에 보낸 것, 열(column) = 먼저 보낸 것, 칸 = "행이 열을 앞질러도 되나?"**

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Ordering Rules Summary — 행(나중)이 열(먼저)을 앞질러도 되나? (같은 TC, RO=IDO=0)</text>
  <text x="300" y="44" fill="currentColor" font-size="12">열 = 먼저 보낸 것 (earlier)</text>
  <g fill="currentColor" font-size="13">
    <text x="190" y="66">P (MWr)</text><text x="323" y="66">NPR (MRd)</text><text x="463" y="66">NPW (CfgWr)</text><text x="618" y="66">CPL</text>
    <text x="16" y="104">P (MWr)</text><text x="16" y="150">NPR (MRd)</text><text x="16" y="196">NPW (CfgWr)</text><text x="16" y="242">CPL (CplD)</text>
  </g>
  <text x="16" y="270" fill="currentColor" font-size="12">↑ 행 = 나중에 보낸 것 (later)</text>
  <g stroke="currentColor">
    <rect x="150" y="76" width="145" height="44" class="f-warn"/><rect x="295" y="76" width="145" height="44" class="f-ok"/>
    <rect x="440" y="76" width="145" height="44" class="f-ok"/><rect x="585" y="76" width="145" height="44" class="f-mut"/>
    <rect x="150" y="122" width="145" height="44" class="f-warn"/><rect x="295" y="122" width="145" height="44" class="f-mut"/>
    <rect x="440" y="122" width="145" height="44" class="f-mut"/><rect x="585" y="122" width="145" height="44" class="f-mut"/>
    <rect x="150" y="168" width="145" height="44" class="f-warn"/><rect x="295" y="168" width="145" height="44" class="f-mut"/>
    <rect x="440" y="168" width="145" height="44" class="f-mut"/><rect x="585" y="168" width="145" height="44" class="f-mut"/>
    <rect x="150" y="214" width="145" height="44" class="f-warn"/><rect x="295" y="214" width="145" height="44" class="f-ok"/>
    <rect x="440" y="214" width="145" height="44" class="f-ok"/><rect x="585" y="214" width="145" height="44" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="158" y="94">A2a No</text><text x="158" y="112">A2b: RO, IDO+ID다름</text>
    <text x="303" y="104">A3 Yes (필수)</text><text x="448" y="104">A4 Yes (필수)</text><text x="593" y="104">A5 Y/N</text>
    <text x="158" y="140">B2a No</text><text x="158" y="158">B2b: IDO+ID다름</text>
    <text x="303" y="150">B3 Y/N</text><text x="448" y="150">B4 Y/N</text><text x="593" y="150">B5 Y/N</text>
    <text x="158" y="186">C2a No</text><text x="158" y="204">C2b: RO*, IDO+ID다름</text>
    <text x="303" y="196">C3 Y/N</text><text x="448" y="196">C4 Y/N</text><text x="593" y="196">C5 Y/N</text>
    <text x="158" y="232">D2a No</text><text x="158" y="250">D2b: RO, IDO+ID다름</text>
    <text x="303" y="242">D3 Yes (필수)</text><text x="448" y="242">D4 Yes (필수)</text>
    <text x="593" y="232">D5a Y/N (Tag 다름)</text><text x="593" y="250">D5b No (같은 TxID)</text>
  </g>
  <rect x="150" y="286" width="16" height="14" class="f-warn" stroke="currentColor"/><text x="172" y="298" fill="currentColor" font-size="12">No: 앞지르면 위반</text>
  <rect x="320" y="286" width="16" height="14" class="f-ok" stroke="currentColor"/><text x="342" y="298" fill="currentColor" font-size="12">Yes: 앞지를 수 있어야 함 (데드락 방지)</text>
  <rect x="590" y="286" width="16" height="14" class="f-mut" stroke="currentColor"/><text x="612" y="298" fill="currentColor" font-size="12">Y/N: 자유</text>
  <text x="150" y="322" fill="currentColor" font-size="12">* C2b 의 RO 허용은 이 레슨 모델 — 스펙 확인 권장 (5장 표 참고)</text>
</svg>
```

외울 것은 사실상 세 줄이다. **(1) 첫 열(먼저 보낸 Posted)은 거의 아무도 못 앞지른다** — 그래서 쓰기 뒤의 읽기, 쓰기 뒤의 완료가 쓰기를 "밀어낸다(flush)". **(2) Posted 와 Completion 은 Non-Posted 를 반드시 앞지를 수 있어야 한다** (A3/A4/D3/D4 Yes) — 데드락 방지. **(3) 나머지는 자유(Y/N)** — Non-Posted 끼리, 완료끼리(같은 요청의 분할 완료만 빼고).

RO 와 IDO 는 TLP 헤더 DW0 의 Attr 비트다. 필드 전체 인코딩은 PCIE-B10 의 몫이고, 여기서는 위치만 본다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">TLP 헤더 DW0 — 순서 규칙이 보는 필드 (예: MWr32, TC=3, IDO=1, RO=1, Length=16 → 40 34 20 10)</text>
  <g fill="currentColor"><text x="80" y="42">byte 0</text><text x="256" y="42">byte 1</text><text x="432" y="42">byte 2</text><text x="608" y="42">byte 3</text></g>
  <g stroke="currentColor">
    <rect x="20" y="52" width="66" height="40" class="f-acc"/><rect x="86" y="52" width="110" height="40" class="f-acc"/>
    <rect x="196" y="52" width="22" height="40" class="f-mut"/><rect x="218" y="52" width="66" height="40" class="f-ok"/>
    <rect x="284" y="52" width="22" height="40" class="f-mut"/><rect x="306" y="52" width="22" height="40" class="f-warn"/>
    <rect x="328" y="52" width="22" height="40" class="f-mut"/><rect x="350" y="52" width="22" height="40" class="f-mut"/>
    <rect x="372" y="52" width="22" height="40" class="f-mut"/><rect x="394" y="52" width="22" height="40" class="f-mut"/>
    <rect x="416" y="52" width="22" height="40" class="f-warn"/><rect x="438" y="52" width="22" height="40" class="f-mut"/>
    <rect x="460" y="52" width="44" height="40" class="f-mut"/><rect x="504" y="52" width="44" height="40" class="f-mut"/>
    <rect x="548" y="52" width="176" height="40" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="53" y="76">Fmt</text><text x="141" y="76">Type</text>
    <text x="207" y="76">T9</text><text x="251" y="76">TC</text><text x="295" y="76">T8</text><text x="317" y="70">A2</text>
    <text x="339" y="76">LN</text><text x="361" y="76">TH</text><text x="383" y="76">TD</text><text x="405" y="76">EP</text>
    <text x="427" y="70">A1</text><text x="449" y="70">A0</text><text x="482" y="76">AT</text><text x="526" y="76">Len</text>
    <text x="636" y="76">Length[7:0]</text>
    <text x="317" y="86" font-size="12">IDO</text><text x="427" y="86" font-size="12">RO</text><text x="449" y="86" font-size="12">NS</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="53" y="110">010</text><text x="141" y="110">00000</text><text x="207" y="110">0</text><text x="251" y="110">011</text>
    <text x="295" y="110">0</text><text x="317" y="110">1</text><text x="339" y="110">0</text><text x="361" y="110">0</text>
    <text x="383" y="110">0</text><text x="405" y="110">0</text><text x="427" y="110">1</text><text x="449" y="110">0</text>
    <text x="482" y="110">00</text><text x="526" y="110">00</text><text x="636" y="110">00010000</text>
    <text x="108" y="132">0x40</text><text x="284" y="132">0x34</text><text x="460" y="132">0x20</text><text x="636" y="132">0x10</text>
  </g>
  <g fill="currentColor">
    <text x="20" y="160">Fmt+Type → 종류: 0x40/0x60 MWr = P, 0x00/0x20 MRd = NPR, 0x44 CfgWr0 = NPW, 0x4A CplD = CPL (연습 1)</text>
    <text x="20" y="180">byte1 bit2 = Attr[2] = IDO,  byte2 bit5 = Attr[1] = RO,  bit4 = Attr[0] = NS(No Snoop, 순서와 무관)</text>
    <text x="20" y="200">TC 가 다르면 순서 관계 없음. RO 는 DevCtl RELAX_EN(0x0010), IDO 는 DevCtl2</text>
    <text x="20" y="218">IDO_REQ_EN(0x0100) / IDO_CMP_EN(0x0200) 이 켜져 있을 때만 (pci_regs.h 값, 연습 3)</text>
  </g>
</svg>
```

### Part A — 표를 코드로, 도착 순서 검사기

- `ORD_TABLE[4][4]` 를 채운다: 값(`V_NO` / `V_YN` / `V_YES`), (b) 예외 종류(`K_RO_IDO`, `K_IDO`, `K_TXID`), 칸 이름(`"A2a"`, 예외면 `"A2b"`).
- `ord_check(issued, n, arrival, m, &viol)`: `issued[]` 는 보낸 순서, `arrival[]` 은 도착 순서(인덱스). 인자 검사(NULL, n > 32, m ≠ n, 범위 밖·중복 인덱스 → `ORD_ERR_ARG`) 후, 먼저 보낸 것을 앞질러 도착한 **모든 쌍**을 `may_pass` 로 검사. 위반이면 `ORD_ILLEGAL` + 첫 위반 쌍과 칸 이름.
- 테스트: 16칸 기본값, MWr → MRd 순서(읽기가 쓰기를 못 앞지름 = read-back flush), posted 끼리 순서 유지, CplD 가 앞선 MWr 을 못 앞지름, CfgWr 대 MWr, 손으로 센 합법 순열 수(1, 3, 6).

### Part B — 예외: RO, IDO, TC, 분할 완료

- `may_pass(later, earlier, &rule)`: TC 가 다르면 무조건 허용(`"TC"`). No 칸이면 (b) 예외 — **나중 것(later)의** RO, 또는 나중 것의 IDO + 출발 ID 다름(요청은 Requester ID, 완료는 Completer ID). B2(읽기)는 IDO 만. D5 칸은 Transaction ID(Requester ID + Tag)가 같으면 No(D5b).
- 테스트: 먼저 것만 RO 면 소용없음, 읽기의 RO 는 소용없음, 같은 함수의 IDO 는 소용없음, 완료의 IDO 는 Completer ID 로 비교, 같은 Tag 분할 완료 역전 금지, 랜덤 3000 열 일관성.

### Part C — 데드락 회피 스케줄러와 드라이버 시나리오

- `sched_pick(q, sent, n, &credits)`: 아직 안 보낸 것 중 credit 이 있고 **앞에 남은 모든 것을 앞질러도 되는** 첫 TLP. 비교용 `fifo_pick` 은 맨 앞이 막히면 전부 멈춘다.
- 테스트: NP credit 0 에서 FIFO 는 멈추고 스케줄러는 뒤의 MWr 을 보냄(A3), P credit 0 에서 읽기가 쓰기를 앞지르지 않음(B2a), 완료가 NP 를 지나감(D3), 랜덤 credit 2000 회 드레인 결과가 전부 검사기 통과.
- 드라이버 예: 호스트 `writel(doorbell); readl(status);` 와 SSD 쪽 data ×3 → CQE → MSI-X. data 에만 RO 를 주면 합법 순서 6개(data 끼리만 섞임), **CQE 에까지 RO 를 주는 펌웨어 버그**면 CQE 가 data 보다 먼저 도착하는 순서가 합법이 된다.

## 2. Problem (English)

> **PCIe ordering checker.** Transactions on one link, one direction, are classified as `P` (posted: MWr, Msg), `NPR` (non-posted read: MRd, IORd, CfgRd), `NPW` (non-posted with data: IOWr, CfgWr, AtomicOp) or `CPL` (Cpl/CplD). Each has `tc` (0..7), `ro`, `ido`, a Transaction ID (`rid`, `tag`) and, for completions, a Completer ID `cid`.
>
> 1. Encode the PCIe "Ordering Rules Summary" table as `ORD_TABLE[later][earlier]` with values `No / Y/N / Yes`, and write
>    `bool may_pass(const txn *later, const txn *earlier, const char **rule)` that applies the (b) exceptions:
>    different TC → allowed; RO on the *later* TLP (rows P, NPW, CPL vs. an earlier P); IDO on the later TLP when the source IDs differ (Requester ID for requests, Completer ID for completions); two completions with the same Transaction ID must stay in order.
> 2. `int ord_check(const txn *issued, size_t n, const uint8_t *arrival, size_t m, ord_viol *v)` returns `ORD_OK`, `ORD_ILLEGAL` (with the first violating pair and the rule id, e.g. `"B2a"`), or `ORD_ERR_ARG` for `n > 32`, `m != n`, or an `arrival` that is not a permutation. No heap.
> 3. `int sched_pick(const txn *q, const bool *sent, size_t n, const credits *cr)` — pick the first unsent TLP that has a credit of its class and may pass every earlier unsent TLP; `-1` if none.
>
> Example: issued `[MWr doorbell, MRd status]`, arrival `[1, 0]` → `ORD_ILLEGAL`, later = 1, earlier = 0, rule `"B2a"` (a read may not pass a posted write — this is why a read-back flushes posted writes).
> Example: issued `[MRd, MWr0, MWr1]`, credits `{P:2, NP:0}` → FIFO stalls (`-1`), `sched_pick` returns 1 (posted must be able to pass non-posted, rule A3).
> Follow-up: an SSD writes data (RO=1) ×3, then a CQE, then MSI-X. How many arrival orders are legal? What changes if firmware also sets RO on the CQE?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 네 개: **A1** 표 16칸, **A2** `ord_check`, **B1** `may_pass` 의 예외, **C1** `sched_pick`. 하네스, `count_legal`(Heap 알고리즘으로 모든 도착 순열을 세는 도우미), `fifo_pick`, `must_be_able_to_pass` 는 주어진다. 스텁은 "표 전부 Y/N, 검사기는 항상 OK, 스케줄러는 FIFO" 라 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
ordering table (row=later passes column=earlier?), RO=IDO=0, same TC, same IDs:
      P             NPR           NPW           CPL
P     ? Y/N         ? Y/N         ? Y/N         ? Y/N
...
FAIL starter.c:201  bad == 0
FAIL starter.c:207  swap2(wr, rd, &v) == ORD_ILLEGAL && v.later == 1 && v.earlier == 0 && strcmp(v.rule, "B2a") == 0
...
C drain: 2000 queues x 10 TLPs, reordered=0, illegal-or-stuck=0
FAIL starter.c:403  bad == 0 && reorders > 1000
FAIL starter.c:407  count_legal(s, 5) == 1
...
16/53 passed
```

(exit 1.) 단계별로: A1+A2 만 채우면 **35/53**, B1 까지 **47/53**, C1 까지 **53/53** (각각 실제로 빌드해 확인).

## 4. 힌트

<details><summary>힌트 1 — 표 읽는 방향부터 고정하라</summary>

칸 = "행이 열을 앞질러도 되나". 행 = **나중에 보낸** 것. "MWr 다음 MRd" 에서 걱정하는 건 MRd(나중)가 MWr(먼저)을 앞지르는 것 → 행 NPR, 열 P → B2a. 첫 열(P)은 네 칸 모두 No(+예외), 둘째·셋째 열의 P 행과 CPL 행은 Yes, 나머지는 Y/N, CPL×CPL 만 Tag 조건.

</details>

<details><summary>힌트 2 — 도착 순서를 "위치 배열"로 뒤집어라</summary>

`pos[arrival[a]] = a` 를 만들면 "i 를 j 가 앞질렀다" ⇔ `i < j && pos[j] < pos[i]`. 역전된 쌍(inversion)만 검사하면 된다. 위반 보고 순서를 정하려면 도착 순서 a 로 바깥 루프, 그 안에서 `i = 0..j-1` 중 `pos[i] > a` 인 것을 검사.

</details>

<details><summary>힌트 3 — 순열 검사는 비트맵 하나</summary>

n ≤ 32 이므로 `uint32_t seen`. `arrival[a] >= n` 이거나 이미 본 인덱스면 `ORD_ERR_ARG`. 이 검사를 **pos[] 에 쓰기 전에** 하면 범위 밖 쓰기가 없다. `m != n` 은 "TLP 가 사라졌다" — 순서 문제가 아니라 인자 오류로 본다.

</details>

<details><summary>힌트 4 — 예외는 "나중 것"의 속성만 본다</summary>

RO/IDO 는 앞지르려는 TLP 가 들고 있어야 한다. 먼저 보낸 data 에 RO 가 있어도, 나중 flag(RO=0)는 그 data 를 못 앞지른다 — 이게 "data 만 RO, CQE 는 RO 없이" 설계의 근거다. IDO 비교 ID 는 요청이면 Requester ID, 완료면 **Completer ID** (`src_id()` 가 준다). 읽기(B2)는 IDO 만.

</details>

<details><summary>힌트 5 — 스케줄러는 "남은 앞의 것"만 비교</summary>

이미 보낸 TLP 는 더 이상 앞지를 대상이 아니다. `!sent[j]` 인 j < i 전부에 대해 `may_pass(&q[i], &q[j])` 가 참이고 `q[i]` 종류의 credit 이 있으면 i 를 고른다. 맨 앞(남은 것 중 가장 오래된 것)은 앞에 남은 게 없으니 credit 만 있으면 항상 뽑힌다 → 진전이 보장된다.

</details>

## 5. 풀이

### 5.1 이 레슨이 코드로 옮긴 표 (출처와 확신도)

출처: PCIe Base Specification r3.0 이후 §2.4.1 "Transaction Ordering Rules" 의 요약 표와 각 칸의 a)/b) 설명, 그리고 Mindshare *PCI Express Technology 3.0* 8장. 아래 문장은 기억에서 옮긴 요약이므로 **"확신" 열이 "확인 권장" 인 칸은 원문을 확인하라.**

| 칸 | 행(나중) 대 열(먼저) | 값 | 내용 (이 레슨 코드) | 확신 |
|---|---|---|---|---|
| A2a/A2b | P 대 P | No / b) Y/N | 쓰기는 앞선 쓰기를 못 앞지른다. **b)** 나중 쓰기가 RO=1 이면 허용, IDO=1 이고 두 Requester ID 가 다르면 허용 | 확신 (PASID 조건은 생략) |
| A3, A4 | P 대 NPR, NPW | **Yes** | 쓰기는 NP 요청을 앞지를 수 **있어야** 한다 (데드락 방지) | 확신 |
| A5 | P 대 CPL | Y/N | 허용, 필수 아님. (b: PCIe→PCI 브리지가 conventional PCI 모드일 때는 필수 — 이 레슨 모델 밖) | 확신 / b) 는 확인 권장 |
| B2a/B2b | NPR 대 P | No / b) Y/N | 읽기는 앞선 쓰기를 못 앞지른다. **b)** IDO=1 이고 Requester ID 가 다르면 허용. **RO 로는 안 된다** | 확신 (RO 미적용은 확인 권장) |
| B3–B5, C3–C5 | NP 대 NP·CPL | Y/N | 자유 | 확신 |
| C2a/C2b | NPW 대 P | No / b) Y/N | CfgWr/IOWr/AtomicOp 는 앞선 쓰기를 못 앞지른다. **b)** 이 레슨은 RO 또는 IDO+ID다름 허용으로 둠 | **C2b 의 RO 허용은 확인 권장** (CfgWr/IOWr 는 Attr 가 0 이라 실제 의미는 AtomicOp 뿐) |
| D2a/D2b | CPL 대 P | No / b) Y/N | 완료는 앞선 쓰기를 못 앞지른다. **b)** 완료가 RO=1 이면 허용, IDO=1 이고 **Completer ID** 가 쓰기의 Requester ID 와 다르면 허용 | 확신 |
| D3, D4 | CPL 대 NPR, NPW | **Yes** | 완료는 NP 요청을 앞지를 수 있어야 한다 | 확신 |
| D5a/D5b | CPL 대 CPL | Y/N / No | Transaction ID 가 다르면 자유, **같으면**(한 읽기의 분할 완료) 순서 유지 | 확신 |
| (TC) | TC 다른 모든 쌍 | Y/N | 순서 규칙은 같은 TC 안에서만 | 확신 |

옛 판(r1.x/2.0)의 표는 칸이 더 잘게 나뉘어 있었다(예: "I/O·Config Write 완료는 MWr 에 막히거나 앞질러도 된다" 같은 칸). r2.1 에서 IDO 와 AtomicOp 가 들어오며 지금의 4×4 로 정리됐다 — 판에 따라 표 번호(2-23, 2-39, 2-40 …)가 달라 인용할 땐 판을 같이 적는다.

`solution.c` 가 표를 찍은 실제 출력 (위 표와 같은지 이걸로 확인):

```text
ordering table (row=later passes column=earlier?), RO=IDO=0, same TC, same IDs:
      P             NPR           NPW           CPL
P     A2a No        A3 Yes        A4 Yes        A5 Y/N
NPR   B2a No        B3 Y/N        B4 Y/N        B5 Y/N
NPW   C2a No        C3 Y/N        C4 Y/N        C5 Y/N
CPL   D2a No        D3 Yes        D4 Yes        D5a Y/N
```

**Yes 와 Y/N 의 차이.** 둘 다 "도착 순서가 바뀌어도 위반이 아님" 이라 검사기(`ord_check`)에서는 같다. 차이는 **구현의 의무**다: Yes 칸은 하드웨어가 그 추월을 *할 수 있어야* 한다 — 즉 P/CPL 을 NP 뒤에 하나의 FIFO 로 줄 세워 NP 가 막혔을 때 같이 막히게 만들면 스펙 위반(Part C). 그래서 `must_be_able_to_pass()` 를 따로 둔다.

### 5.2 Part A — 검사기: 역전 쌍만 보면 충분한 이유

규칙이 전부 **두 TLP 사이의 관계**(pairwise)이므로, 도착 순서가 합법 ⇔ 역전된 모든 쌍 (i < j, j 가 먼저 도착) 에 대해 `may_pass(j, i)` 가 참. 역전되지 않은 쌍은 보낸 순서대로 왔으니 검사할 게 없다. n ≤ 32 이면 최대 496 쌍, O(n²) 로 충분하다.

손 추적 — `s = [W0, W1, R2, W3]` (P, P, NPR, P), 도착 `[2, 3, 0, 1]`:

| 단계 | 값 |
|---|---|
| 순열 검사 | seen = 0b0100 → 0b1100 → 0b1101 → 0b1111, 중복·범위 밖 없음 |
| pos[] | pos[2]=0, pos[3]=1, pos[0]=2, pos[1]=3 |
| a=0, j=2 (R2) | i=0: pos[0]=2 > 0 → R2 가 W0 을 앞지름 → NPR 대 P → **B2a No** → 위반 |
| 보고 | later=2, earlier=0, rule "B2a" (W3 이 W0·W1 을 앞지른 A2a 도 위반이지만 R2 가 먼저 도착했으므로 먼저 보고) |

**손으로 세는 합법 순열 수** (테스트가 `count_legal` 로 확인):

- `[W0, W1, R]` → **1**. W 끼리 순서 고정(A2a), R 은 두 W 를 못 앞지름(B2a). 보낸 순서 하나뿐.
- `[R, W0, W1]` → **3**. W0 < W1 은 고정, W 는 R 을 앞질러도 됨(A3) → R 이 들어갈 자리 3곳.
- `[R, CW, W]` → **6** (=3!). CW 가 R 을 앞지름 C3 Y/N, W 가 둘 다 앞지름 A3/A4 Yes. 역전될 수 있는 쌍이 전부 허용.

인자 검사를 **쓰기 전에** 하는 이유: `arrival[a] = 3` 인데 n = 3 이면 `pos[3]` 은 범위 밖. 펌웨어 검증 도구는 로직 애널라이저/프로토콜 분석기 캡처를 입력으로 받으므로 깨진 입력이 흔하다 — 거부하고 `*v` 는 건드리지 않는다(테스트가 `v.later == 99` 그대로인지 본다).

### 5.3 Part B — 예외 네 가지

```c
if (later->tc != earlier->tc) { *rule = "TC"; return true; }
if (c->v == V_NO) {
    bool ex = false;
    if (c->k == K_RO_IDO && later->ro) ex = true;
    if ((c->k == K_RO_IDO || c->k == K_IDO) && later->ido && src_id(later) != src_id(earlier)) ex = true;
    *rule = ex ? c->idb : c->ida;  return ex;
}
if (c->k == K_TXID && later->rid == earlier->rid && later->tag == earlier->tag) { *rule = c->idb; return false; }
```

1. **TC.** TC 는 VC(가상 채널)로 매핑되고 VC 마다 버퍼와 credit 이 따로다. 규칙은 같은 TC 안에서만 의미가 있다. 실제 시스템은 거의 TC0 하나만 쓴다(Linux 는 TC/VC 를 기본 설정 그대로 둔다) — 그래서 "다른 TC 면 자유" 는 면접에서 함정 질문으로만 나온다.
2. **RO (Attr[1]).** "이 TLP 는 앞선 쓰기와 순서가 상관없다" 는 요청자의 약속. NVMe/NIC 가 **data 쓰기**에 RO 를 붙이면 Root Complex 가 메모리 쓰기를 병렬·비순차로 처리해 대역폭이 오른다. 대신 **flag 역할의 쓰기(CQE, 수신 디스크립터 완료 비트, MSI-X)에는 절대 붙이지 않는다** — 나중 것의 RO 만 보므로, flag 가 RO=0 이면 앞선 RO data 를 전부 밀어낸다.
3. **IDO (Attr[2]).** "출발지가 다르면 순서 관계 없다". 한 스위치 아래 여러 함수/VF 가 있을 때, VF1 의 쓰기 폭주 뒤에 VF2 의 읽기가 줄 서 있는 head-of-line blocking 을 푼다. 비교 대상은 요청 = Requester ID, **완료 = Completer ID** — 완료는 "누가 보냈나" 가 Completer 이기 때문이다(완료의 Requester ID 는 원래 요청자 = 받는 쪽).
4. **D5b.** MRd 하나(예: 512B, RCB 64B)는 여러 CplD 로 쪼개져 돌아올 수 있고(분할 규칙은 PCIE-B10), 같은 Transaction ID(RID + Tag)의 조각은 **주소 순서대로** 와야 한다. Tag 가 다르면(다른 읽기) 섞여도 된다 → 요청자는 Tag 로 재조립한다.

### 5.4 Part C — 왜 "Posted 는 NP 를 앞질러야" 하나 (데드락)

NP 요청은 완료를 받아야 끝나고, 완료는 **앞선 쓰기를 못 앞지른다**(D2a). 만약 쓰기도 NP 뒤에 막힐 수 있다면 "NP → 완료 → 쓰기 → NP" 로 기다림이 고리를 만든다. 연습 4 의 2노드 모델:

```svg
<svg viewBox="0 0 740 336" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs>
    <marker id="pb11a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="9" markerHeight="9" markerUnits="userSpaceOnUse" orient="auto">
      <path d="M0,0 L8,4 L0,8 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">FIFO 송신이면 데드락: 기다림 4개가 고리 (연습 4 시뮬레이션 그대로)</text>
  <rect x="20" y="40" width="200" height="250" class="f-mut" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor" font-weight="bold">X (스위치)</text>
  <rect x="520" y="40" width="200" height="250" class="f-mut" stroke="currentColor"/>
  <text x="530" y="60" fill="currentColor" font-weight="bold">Y (장치: Y1, Y2)</text>
  <text x="250" y="78" fill="currentColor" font-size="12">X→Y 송신 큐 (2칸, FIFO)</text>
  <rect x="250" y="86" width="110" height="34" class="f-warn" stroke="currentColor"/><text x="262" y="108" fill="currentColor">R (MRd) 맨앞</text>
  <rect x="362" y="86" width="110" height="34" class="f-ok" stroke="currentColor"/><text x="374" y="108" fill="currentColor">P1 (MWr)</text>
  <text x="250" y="200" fill="currentColor" font-size="12">Y→X 송신 큐 (2칸)</text>
  <rect x="250" y="208" width="110" height="34" class="f-ok" stroke="currentColor"/><text x="262" y="230" fill="currentColor">P2 (MWr) 맨앞</text>
  <rect x="362" y="208" width="110" height="34" class="f-acc" stroke="currentColor"/><text x="374" y="230" fill="currentColor">C' (CplD)</text>
  <rect x="540" y="86" width="160" height="34" class="f-warn" stroke="currentColor"/><text x="548" y="108" fill="currentColor" font-size="12">NP 수신 1칸: R' 점유</text>
  <rect x="40" y="208" width="160" height="34" class="f-ok" stroke="currentColor"/><text x="48" y="230" fill="currentColor" font-size="12">P 수신 1칸: P0 점유</text>
  <path d="M472,103 H538" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#pb11a)"/>
  <text x="420" y="140" fill="currentColor" font-size="12">① R 은 Y 의 NP credit 대기</text>
  <path d="M620,122 V225 H474" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#pb11a)"/>
  <text x="628" y="178" fill="currentColor" font-size="12">② R' 칸은</text>
  <text x="628" y="194" fill="currentColor" font-size="12">C' 를 보내야 빔</text>
  <text x="290" y="268" fill="currentColor" font-size="12">③ C' 는 P2 를 못 앞지름 (D2a)</text>
  <path d="M250,225 H202" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#pb11a)"/>
  <text x="40" y="268" fill="currentColor" font-size="12">④ P2 는 X 의 P credit 대기</text>
  <path d="M190,206 V103 H248" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#pb11a)"/>
  <text x="28" y="140" fill="currentColor" font-size="12">⑤ P0(P2P 쓰기)는</text>
  <text x="28" y="156" fill="currentColor" font-size="12">X→Y 큐에 자리가</text>
  <text x="28" y="172" fill="currentColor" font-size="12">나야 빠짐</text>
  <text x="28" y="188" fill="currentColor" font-size="12">(큐는 R 에 막힘)</text>
  <text x="20" y="308" fill="currentColor" font-size="12">고리 끊기: P1 이 R 을 앞지름(A3 Yes) → 큐에 자리 → P0 이동 → P2 → C' → R' 칸 빔 → R.</text>
  <text x="20" y="326" fill="currentColor" font-size="12">sched_pick 로 5단계에 전부 빠짐, fifo_pick 은 첫 단계부터 진전 0 (연습 4 실측)</text>
</svg>
```

P0 는 Y 안의 Y1 → Y2 peer-to-peer 쓰기가 ACS redirect 로 스위치까지 올라왔다가 같은 링크로 다시 내려가야 하는 경우로 잡았다(ACS 는 PCIE-A08). 고리의 모든 화살표가 실제 규칙이다: ① NP credit, ② 완료를 보내야 NP 버퍼가 빈다(받은 읽기를 처리 중), ③ D2a, ④ P credit, ⑤ 스위치 내부 버퍼. 이 중 스펙이 끊을 수 있는 곳은 "큐 안에서 P1 이 R 뒤에 막히는" 부분뿐이라 A3/A4 가 **Yes(필수)** 다. 완료도 같은 이유로 D3/D4 Yes — 완료가 NP 뒤에 막히면 그 완료를 기다리는 요청자의 NP 버퍼가 안 비어 같은 고리가 생긴다.

`sched_pick` 는 그 의무를 구현한다. 테스트의 한 장면 (NP credit 0, P credit 2):

| 큐 | fifo_pick | sched_pick | 근거 |
|---|---|---|---|
| [MRd, MWr0, MWr1] | -1 (MRd 에서 멈춤) | 1 → 2 → -1 | A3 Yes, MWr1 은 MWr0 를 못 앞지르지만 MWr0 가 이미 나감 |
| (NP credit 1 반환) | | 0 | 남은 게 MRd 뿐 |
| [MWr doorbell, MRd] (P credit 0) | | -1 | 읽기는 쓰기를 못 앞지름 (B2a) — 정답이다 |
| 같은 큐, MRd 가 다른 함수 + IDO | | 1 | B2b |

랜덤 2000 큐(10 TLP, 무작위 credit 반환) 실측: 1969 개에서 순서가 실제로 바뀌었고, 만들어진 도착 순서는 **전부** `ord_check` 통과, 막혀 끝나지 못한 큐 0개. 맨 앞 TLP 는 앞에 남은 것이 없어 credit 만 오면 항상 나가므로 진전이 보장된다.

### 5.5 Part C — 드라이버에서: read-back 과 완료 경로

```svg
<svg viewBox="0 0 740 312" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs>
    <marker id="pb11b" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="9" markerHeight="9" markerUnits="userSpaceOnUse" orient="auto">
      <path d="M0,0 L8,4 L0,8 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 링크, 두 방향 — 각 방향 안에서만 순서 규칙이 적용된다</text>
  <text x="10" y="50" fill="currentColor">하향 (CPU → SSD)</text>
  <rect x="150" y="32" width="200" height="30" class="f-ok" stroke="currentColor"/><text x="160" y="52" fill="currentColor" font-size="12">① MWr SQ tail doorbell</text>
  <rect x="360" y="32" width="170" height="30" class="f-warn" stroke="currentColor"/><text x="370" y="52" fill="currentColor" font-size="12">② MRd CSTS (readl)</text>
  <path d="M530,47 H700" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#pb11b)"/><text x="560" y="40" fill="currentColor" font-size="12">SSD 로</text>
  <text x="150" y="84" fill="currentColor" font-size="12">B2a: ② 는 ① 을 못 앞지름 → ② 의 CplD 가 돌아왔다 = ① 은 이미 SSD 에 도착</text>
  <text x="10" y="132" fill="currentColor">상향 (SSD → 호스트)</text>
  <rect x="150" y="114" width="80" height="30" class="f-mut" stroke="currentColor"/><text x="156" y="134" fill="currentColor" font-size="12">data0 RO</text>
  <rect x="234" y="114" width="80" height="30" class="f-mut" stroke="currentColor"/><text x="240" y="134" fill="currentColor" font-size="12">data1 RO</text>
  <rect x="318" y="114" width="80" height="30" class="f-mut" stroke="currentColor"/><text x="324" y="134" fill="currentColor" font-size="12">data2 RO</text>
  <rect x="402" y="114" width="70" height="30" class="f-acc" stroke="currentColor"/><text x="412" y="134" fill="currentColor" font-size="12">CQE</text>
  <rect x="476" y="114" width="70" height="30" class="f-acc" stroke="currentColor"/><text x="482" y="134" fill="currentColor" font-size="12">MSI-X</text>
  <rect x="550" y="114" width="90" height="30" class="f-warn" stroke="currentColor"/><text x="556" y="134" fill="currentColor" font-size="12">CplD (②)</text>
  <path d="M640,129 H700" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#pb11b)"/>
  <path d="M190,150 C210,172 260,172 274,150" fill="none" stroke="currentColor" stroke-dasharray="4,3"/>
  <path d="M274,150 C294,172 344,172 358,150" fill="none" stroke="currentColor" stroke-dasharray="4,3"/>
  <text x="170" y="190" fill="currentColor" font-size="12">data 끼리: 나중 것이 RO → 섞여도 됨 (A2b)</text>
  <line x1="400" y1="108" x2="400" y2="200" stroke="currentColor" stroke-width="3"/>
  <text x="406" y="214" fill="currentColor" font-size="12">CQE·MSI-X(RO=0)는 앞선 data 를 못 앞지름 (A2a)</text>
  <text x="406" y="232" fill="currentColor" font-size="12">CplD 도 앞선 쓰기를 못 앞지름 (D2a)</text>
  <text x="10" y="258" fill="currentColor" font-size="12">합법 도착 순서: RO 없음 1개, data 만 RO 6개(3!),</text>
  <text x="10" y="276" fill="currentColor" font-size="12">CQE 에도 RO(펌웨어 버그) → CQE 가 data 보다 먼저 오는 순서가 합법이 됨</text>
  <text x="10" y="300" fill="currentColor" font-size="12">MSI-X 는 특별한 신호가 아니라 그냥 MWr — 그래서 "인터럽트가 data 를 추월하지 않는다" 가 A2a 로 공짜로 따라온다</text>
</svg>
```

**호스트 쪽 — `writel(); readl();`.** MMIO 쓰기는 CPU 입장에서 store 한 번으로 끝나지만 실제 TLP 는 Root Complex, 스위치의 쓰기 버퍼에 머물 수 있다(posted). "리셋 비트 쓰고 10 µs 기다리기" 를 그냥 `writel(); udelay(10);` 로 하면 기다림이 쓰기 도착 **전에** 시작될 수 있다. 같은 장치의 레지스터를 `readl()` 하면, 그 MRd 는 앞선 MWr 을 못 앞지르고(B2a) CPU 는 CplD 가 올 때까지 멈추므로 **readl 이 돌아온 시점 = 앞선 쓰기가 장치에 도착한 뒤**. 이것이 Linux `Documentation/driver-api/device-io.rst` 가 말하는 "PCI posting" 과 그 해법(read-back)이다. 주의: (1) **같은 장치(같은 경로)** 를 읽어야 한다 — 다른 장치를 읽으면 다른 경로라 아무것도 보장 안 됨. (2) 읽은 값은 버려도 되지만 읽기 자체는 해야 한다(`readl` 은 volatile 접근이라 컴파일러가 지우지 않는다).

arm64 Linux 의 실제 구현(VM 의 `/usr/src/linux-headers-7.0.0-38-generic/arch/arm64/include/asm/io.h` 에서 확인): `__io_bw()` = `dma_wmb()`, 즉 `writel` 은 **store 전에** 배리어 — "DMA 디스크립터를 메모리에 다 쓴 뒤 도어벨" 을 CPU 쪽에서 보장한다(ARCH-M12, BM-M08). `readl` 뒤의 `__io_ar()` 는 `dma_rmb()` + 읽은 값에 대한 가짜 제어 의존성으로, 주석에 "a subsequent call to udelay() will be ordered" 라고 적혀 있다 — read-back 후 delay 관용구를 CPU 쪽에서도 지켜 주는 장치다. 즉 **CPU 쪽 순서(배리어)** 와 **링크 위 순서(이 레슨의 표)** 는 다른 층이고, 드라이버는 둘 다 필요하다.

**장치 쪽 — 완료 경로.** SSD 펌웨어(또는 NVMe 컨트롤러 HW)가 data → CQE → MSI-X 순으로 보내면, 셋 다 posted 이므로 A2a 가 순서를 지켜 준다. 호스트가 MSI-X 핸들러에서 CQE 를 보고 data 를 읽을 때 data 는 이미 메모리에 있다. 테스트가 확인한 숫자: RO 없음 → 합법 순서 1개, data 에만 RO → 6개(data 끼리만 섞임), CQE 에 RO 를 실수로 켜면 `[CQE, data0, data1, data2, MSI-X]` 가 합법 → 호스트는 Phase Tag(PCIE-M01)가 뒤집힌 CQE 를 보고 아직 안 온 data 를 읽는다. **데이터 손상인데 재현이 어렵다**(RC 가 실제로 순서를 바꿀 때만) — SSD FW 의 최악의 버그 유형이다.

드라이버가 장치의 상태 레지스터를 **폴링**해서 완료를 아는 경우도 같다: 상태 읽기의 CplD 는 장치가 앞서 보낸 data MWr 을 못 앞지른다(D2a). 그래서 "상태 읽기 → done → data 읽기" 가 안전하다. 단, 그 CplD 에 RO 가 실리면(요청의 RO 가 완료로 복사됨) D2b 로 깨진다 — 연습 2(e).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 53/53 passed, exit 0 (0.1 초 미만)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
```

`solution.c` 실행 출력 중 표 아래 줄 (실제 출력):

```text
B random: 3000 sequences (n=2..8), illegal=1202, mismatches=0
C drain: 2000 queues x 10 TLPs, reordered=1969, illegal-or-stuck=0
53/53 passed
```

`B random` 은 xorshift32 고정 시드로 만든 3000 개 열(종류·RO·IDO·TC·ID·Tag 무작위, 도착 순서 무작위 셔플)에서 검사기 결과를 "역전 쌍 전수 확인" 과 대조한 것 — 1202 개가 위반, 불일치 0. `C drain` 은 5.4 의 랜덤 드레인.

## 7. 자주 하는 실수

**1. 행과 열을 뒤집는다.** "MWr 다음 MRd" 를 표에서 행 P, 열 NPR(A3 Yes)로 읽으면 "읽기가 쓰기를 앞질러도 된다" 는 정반대 결론. 고친 코드: 항상 `ORD_TABLE[later->cls][earlier->cls]` — 인자 이름 자체를 `later`, `earlier` 로.

**2. 먼저 보낸 것의 RO 를 본다.** `if (earlier->ro || later->ro)` 로 짜면 "data(RO) 다음 CQE(RO=0)" 에서 CQE 가 data 를 앞질러도 된다고 판정 → 펌웨어 버그를 못 잡는 검사기. 고친 코드: `later->ro` 만. RO 는 "**내가** 앞선 것과 순서 무관" 이라는 선언이다.

**3. 읽기에도 RO 예외를 준다.** B2b 는 IDO 만이다. 읽기 요청의 RO 는 그 읽기의 **완료**에 복사돼 완료가 쓰기를 앞지르는 데(D2b) 쓰인다. 고친 코드: B2 칸의 예외 종류를 `K_IDO` 로 따로.

**4. 완료의 IDO 비교에 Requester ID 를 쓴다.** 완료의 `rid` 는 원래 요청자(= 완료를 받는 쪽)다. "출발지가 다르냐" 는 Completer ID 로 봐야 한다. 테스트: 같은 장치(01:00.0)가 보낸 MWr 과 CplD 는 IDO 라도 No. 고친 코드: `src_id()`.

**5. Yes 를 Y/N 처럼 구현한다.** 검사기만 보면 같지만 하드웨어/모델에서 P 를 NP 뒤에 단일 FIFO 로 세우면 NP credit 이 마를 때 데드락(연습 4: FIFO 는 진전 0 으로 멈춤). 고친 코드: 종류별 큐 또는 `sched_pick` 처럼 "credit 있고 앞지를 수 있는 것" 을 고르는 중재.

**6. read-back 을 다른 장치에서 한다.** 장치 A 에 `writel`, 장치 B 에서 `readl` — 둘은 다른 경로라 B 의 MRd 가 A 의 MWr 을 기다릴 이유가 없다(순서 규칙은 같은 경로·같은 방향·같은 TC 의 TLP 사이에서만). 고친 코드: 방금 쓴 장치의 아무 레지스터(가능하면 부작용 없는 ID/상태 레지스터)를 읽는다. clear-on-read 레지스터를 read-back 용으로 읽으면 상태를 잃는다(BM-B04).

**7. 분할 완료의 순서를 잊는다.** 검사기에서 CPL×CPL 을 그냥 Y/N 으로 두면 512B 읽기의 두 번째 CplD 가 첫 번째보다 먼저 와도 통과. 고친 코드: D5 칸에 `K_TXID`, `(rid, tag)` 같으면 No.

## 8. 면접 Q&A

<details><summary>Q1. 왜 MMIO 쓰기 뒤에 같은 장치를 한 번 읽으면 쓰기가 "flush" 되나요?</summary>

MWr 은 posted 라 CPU 는 쓰기가 어디 있는지 모른다(RC·스위치 버퍼에 있을 수 있음). 그 뒤 같은 장치로 보낸 MRd 는 순서 규칙 B2a 로 앞선 MWr 을 앞지를 수 없고, CPU 의 load 는 CplD 가 올 때까지 끝나지 않는다. 그러니 `readl` 이 돌아왔다 = MRd 가 장치에 도착했다 = 그 앞의 MWr 도 도착했다. 꼬리질문 "그럼 MWr 이 장치 내부에서 '적용'된 것까지 보장하나?" → 링크 규칙은 도착까지다. 장치 내부에서 쓰기가 효과를 내는 시점은 장치 설계(같은 레지스터 블록이면 보통 순서대로 처리)에 달렸다. "다른 TC 나 IDO 를 쓰면?" → 깨질 수 있다; 드라이버의 CPU 접근은 TC0, IDO 없음이 기본.

</details>

<details><summary>Q2. Posted 는 왜 Non-Posted 를 앞지를 수 "있어야" 하나요? (Yes vs Y/N)</summary>

NP 요청은 완료가 와야 끝나고, 완료는 앞선 쓰기를 못 앞지른다(D2a). 쓰기까지 NP 뒤에 막힐 수 있으면 NP → (완료 대기) → 완료 → (D2a) → 쓰기 → (같은 큐) → NP 로 기다림이 고리를 만든다. 쓰기는 응답이 필요 없는 유일한 종류라 "받는 쪽이 소비만 하면 반드시 빠지는" 고리의 출구가 된다. 연습 4 의 2노드 모델에서 FIFO 송신은 진전 0 으로 멈추고, P1 이 R 을 앞지르게 하면 5단계에 다 빠진다. Y/N 은 "해도 되고 안 해도 됨", Yes 는 "할 수 있게 만들어야 함(필수)".

</details>

<details><summary>Q3. Relaxed Ordering 은 누가 켜고, 언제 위험한가요?</summary>

장치가 TLP 의 Attr[1] 을 세운다. 단, PCIe Capability 의 Device Control `PCI_EXP_DEVCTL_RELAX_EN`(0x0010, pci_regs.h) 이 켜져 있을 때만. 이득: RC 가 메모리 쓰기를 병렬로 커밋해 DMA 쓰기 대역폭이 오른다(특히 다중 메모리 컨트롤러 시스템). 위험: flag 역할 쓰기(CQE, 디스크립터 done 비트)에 RO 를 실으면 data 보다 먼저 보일 수 있다 — 연습 2(b). 또 일부 RC 는 RO 를 받으면 성능이 오히려 나빠지거나 정상 동작하지 않아 Linux 가 `PCI_DEV_FLAGS_NO_RELAXED_ORDERING` quirk 로 막고, 드라이버는 `pcie_relaxed_ordering_enabled()` 로 확인한다(둘 다 VM 의 `include/linux/pci.h` 에 있음; 어느 플랫폼이 quirk 대상인지는 기억 — 소스 확인 권장).

</details>

<details><summary>Q4. ID-based Ordering 은 무슨 문제를 푸나요? 함정은?</summary>

한 링크에 여러 함수/VF 의 트래픽이 섞일 때, VF1 의 쓰기 폭주 뒤에 VF2 의 읽기·완료가 줄 서는 head-of-line blocking 을 푼다 — 출발 ID 가 다르면 서로 순서 의존이 없다고 보고 앞지르게 한다(A2b/B2b/C2b/D2b). 켜는 비트는 Device Control 2 `IDO_REQ_EN`(0x0100, 요청), `IDO_CMP_EN`(0x0200, 완료). 함정: "다른 함수끼리 독립" 이라는 가정이 깨지는 설계 — 예를 들어 함수 1 이 함수 0 의 작업 완료를 대신 보고하면 IDO 완료가 함수 0 의 data 를 앞지를 수 있다(연습 2(d)).

</details>

<details><summary>Q5. MSI-X 가 data 보다 먼저 도착할 수 있나요? INTx 라면?</summary>

MSI/MSI-X 는 특정 주소에 대한 MWr 이므로 같은 경로·TC 의 앞선 data MWr 을 앞지를 수 없다(A2a, MSI-X 쓰기는 RO=0). data 에 RO 를 써도 MSI-X(RO=0)가 data 를 밀어낸다. 그래서 MSI 핸들러는 data 를 바로 믿어도 된다(RC 가 순서대로 메모리에 보이게 한다는 가정 — 플랫폼 문서 확인). INTx 는 다르다: PCIe 에선 Assert_INTx 메시지(posted)지만 RC 이후 인터럽트 컨트롤러로 가는 경로가 data 경로와 달라 순서가 보장되지 않는다. 그래서 legacy 인터럽트 핸들러는 "장치 상태 레지스터를 먼저 읽는" 관용구를 쓴다 — 그 읽기의 CplD 가 앞선 data 쓰기를 밀어낸다(D2a).

</details>

<details><summary>Q6. arm64 Linux 에서 writel 과 writel_relaxed 는 무엇이 다른가요? readl 은?</summary>

VM 의 `arch/arm64/include/asm/io.h` 기준: `writel` = `__io_bw()`(= `dma_wmb()`) + store, `readl` = load + `__io_ar()`(= `dma_rmb()` + 읽은 값에 대한 가짜 제어 의존성 — 뒤의 `udelay()` 가 읽기 완료 뒤에 시작되게). `_relaxed` 판은 이 배리어가 없다. 링크 위의 순서(같은 장치로의 MWr/MRd 사이)는 Device 메모리 속성과 PCIe 규칙이 지켜 주지만, **일반 메모리(DMA 버퍼)와 MMIO 사이** 순서는 배리어가 지킨다. 그래서 "디스크립터 쓰고 도어벨" 은 `writel`(또는 `wmb()` + `writel_relaxed`), 빈번한 같은 장치 레지스터 접근은 `_relaxed` 로 최적화한다. 메모리 배리어 원리는 ARCH-M12.

</details>

<details><summary>Q7. 한 MRd 에 CplD 가 여러 개 오면 순서는? 다른 읽기의 완료와는?</summary>

같은 Transaction ID(Requester ID + Tag) 의 분할 완료는 주소 순서를 지켜야 한다(D5b). 다른 Tag 의 완료와는 섞여도 된다(D5a). 그래서 요청자(SSD 컨트롤러의 DMA 엔진, RC)는 Tag 마다 재조립 버퍼를 두고, Byte Count/Lower Address 로 위치를 맞춘다(PCIE-B10). Tag 수가 동시에 띄울 수 있는 읽기 수를 정하므로(8-bit Tag 256 개, Gen4 이후 10-bit Tag 로 확장) Gen4/5 SSD 의 읽기 대역폭과 직결된다 — 대역폭 계산은 PCIE-B02.

</details>

<details><summary>Q8. 검증 엔지니어로서 ordering 위반을 어떻게 잡나요?</summary>

(1) 프로토콜 분석기/시뮬레이션 모니터가 링크의 TLP 열을 캡처 → 이 레슨의 `ord_check` 같은 체커가 방향·TC 별로 역전 쌍을 검사(UVM 이면 scoreboard). (2) 스트레스: NP credit 을 일부러 0 으로 묶고 posted 가 지나가는지(Yes 칸), RO/IDO 를 켠 상태와 끈 상태 비교. (3) 시스템 수준: 장치가 data → flag 를 쓰고 호스트가 flag 를 보자마자 data 를 검사하는 producer/consumer 테스트를 수억 번 — CQE 에 RO 가 실리는 버그는 이런 테스트로만 보인다. (4) 설정 확인: `lspci -vvv` 의 `DevCtl: ... RlxdOrd+/-`, `DevCtl2: ... IDOReq+/-` 로 실제로 켜졌는지(lspci 해석은 PCIE-B06).

</details>

## 9. 추가 연습

### 연습 1 — TLP DW0 → 순서 종류

`classify_tlp(const uint8_t dw0[4], txn *out)`: byte0 의 Fmt[2:0]/Type[4:0] 으로 P/NPR/NPW/CPL 을 정하고, byte1 에서 TC 와 Attr[2](IDO), byte2 에서 Attr[1](RO) 를 뽑아라. MRd/MRdLk/MWr(3·4DW), IORd/IOWr, CfgRd/Wr 0·1, Cpl/CplD/CplLk/CplDLk, Msg/MsgD(Type 10rrr), FetchAdd/Swap/CAS 를 지원하고, Fmt 1xx(TLP Prefix)와 말이 안 되는 조합(데이터 있는 MRdLk, 4DW Cfg/IO/Cpl, 데이터 없는 AtomicOp, 3DW Msg)은 -1.

<details><summary>답</summary>

Fmt[1] = "데이터 있음", Fmt[0] = "4DW 헤더". 종류는 Type 으로 먼저 가르고 데이터 유무로 P/NPR 또는 NPW/NPR 을 고른다: `0x00`/`0x20` MRd → NPR, `0x40`/`0x60` MWr → P, `0x01` MRdLk → NPR, `0x02` IORd → NPR, `0x42` IOWr → NPW, `0x04`·`0x05` CfgRd → NPR, `0x44`·`0x45` CfgWr → NPW, `0x0A`/`0x4A`/`0x0B`/`0x4B` → CPL, `0x30`–`0x37`(Msg)·`0x70`–`0x77`(MsgD) → P, `0x4C`/`0x6D`/`0x4E` AtomicOp → NPW. 예: `40 34 20 10` → MWr, TC 3, IDO 1, RO 1. `4A 00 10 01` → CplD, RO 0(bit4 는 NS). 순서 규칙이 Fmt/Type 만 보고 판단 가능하다는 점이 중요하다 — 스위치는 페이로드를 보지 않고 헤더만으로 큐를 고른다. Fmt/Type 값 표는 PCIE-B10 에서 확인.

</details>

### 연습 2 — RO/IDO 버그 찾기: flag 가 data 보다 먼저 보일 수 있나

`flag_before_data_possible(s, n, data_mask, flag)`: 모든 순열 중 **합법인 것** 가운데 flag 가 data 중 하나보다 먼저 오는 것이 있으면 true. 다섯 경우를 판정하라: (a) data×3 RO, CQE RO=0 / (b) CQE 도 RO / (c) 01:00.0 의 data MWr 뒤 같은 함수의 상태 CplD(IDO) / (d) 상태 CplD 를 01:00.1 이 보냄(IDO) / (e) 상태 CplD 에 RO.

<details><summary>답</summary>

(a) false — CQE 가 RO=0 이라 A2a. (b) **true** — CQE 가 RO 로 data 를 앞지를 수 있음(A2b). (c) false — 완료의 Completer ID(01:00.0) = data 의 Requester ID → IDO 예외 불성립(D2a). (d) **true** — Completer ID 가 달라 D2b IDO 가 성립. "다른 함수 = 독립" 이라는 IDO 가정이 깨진 설계. (e) **true** — 완료 RO 는 D2b 로 앞선 쓰기를 앞지를 수 있다. 실전 교훈: flag·완료·인터럽트 역할 TLP 에는 RO/IDO 를 쓰지 않거나, 쓰려면 "누가 data 를 썼나" 와 "누가 flag 를 보내나" 가 같은 ID 인지 확인한다. 전수 탐색은 n ≤ 8 (8! = 40320) 이라 즉시 끝난다.

</details>

### 연습 3 — Device Control 이 허용할 때만 RO/IDO

`sanitize_attrs(t, devctl, devctl2)`: 요청은 `PCI_EXP_DEVCTL_RELAX_EN`(0x0010)이 꺼져 있으면 RO 를 지우고, `PCI_EXP_DEVCTL2_IDO_REQ_EN`(0x0100)이 꺼져 있으면 IDO 를 지운다. 완료는 IDO 만 `PCI_EXP_DEVCTL2_IDO_CMP_EN`(0x0200)으로 거르고 RO 는 요청에서 복사된 값을 둔다(이 레슨 모델 — 스펙 확인 권장). DevCtl `0x2810`(MaxReadReq 512, NoSnoop+, RlxdOrd+) 과 `0x2800`(RlxdOrd-) 에서 "data ×3(RO) → CQE → MSI-X" 의 합법 순서 수를 비교하라.

<details><summary>답</summary>

실측: RlxdOrd+ → 6, RlxdOrd- → 1. 즉 RELAX_EN 이 꺼진 장치는 펌웨어가 RO 를 원해도 못 싣는다 → 정확하지만 RC 쪽 병렬 커밋 이득이 사라진다. Linux 가 특정 RC 아래 장치의 RELAX_EN 을 끄는 이유가 이것(Q3). 비트 값은 `include/uapi/linux/pci_regs.h` 기준(VM 에서 확인): `PCI_EXP_DEVCTL 0x08`, `RELAX_EN 0x0010`, `NOSNOOP_EN 0x0800`, `READRQ_512B 0x2000`, `PCI_EXP_DEVCTL2 0x28`, `IDO_REQ_EN 0x0100`, `IDO_CMP_EN 0x0200`. 실제 장치에서는 `setpci -s <BDF> CAP_EXP+0x08.w` 로 읽는다(PCIE-B06).

</details>

### 연습 4 — 2노드 데드락 시뮬레이션

5.4 의 그림 그대로: X→Y 큐 [R, P1], Y→X 큐 [P2, C'], X 의 P 수신 1칸에 P0, Y 의 NP 수신 1칸에 R'. 한 단계마다 각 방향에서 최대 하나를 `pick` 함수로 보내고, X 는 P 수신 버퍼를 X→Y 큐로 옮기고, Y 는 받은 읽기의 완료를 큐에 넣는다. `fifo_pick` 과 `sched_pick` 으로 각각 돌려라.

<details><summary>답</summary>

실측: FIFO → 첫 단계부터 진전 0, 상태 그대로(xq 2, yq 2, P 수신 1, NP 수신 1) = 데드락. sched → 5단계에 전부 비움, X→Y 로 나간 순서 `P1 Pfwd Pfwd R` — P1 이 R 을 앞지르고(A3), 자리가 난 큐로 P0 가 옮겨 가 X 의 P credit 이 돌아오고, P2 → C' 가 나가 Y 의 NP 칸이 비고, 마지막에 R. 같은 테스트에서 `must_be_able_to_pass(P, NPR)`, `(CPL, NPR)` 는 참, `(CPL, P)` 는 거짓도 확인. 실제 칩에서 이 고리는 credit 이 작은 저가 스위치/브리지, P2P 트래픽, NP credit 을 늦게 돌려주는 엔드포인트 FW 에서 나타난다 — SSD 컨트롤러 FW 가 NP 요청(호스트의 BAR 읽기)을 처리하느라 posted 수신을 막으면 같은 모양이 된다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 15/15 passed
```

```text
practice 3: legal orders with RlxdOrd+ = 6, RlxdOrd- = 1
practice 4 FIFO : DEADLOCK (xq=2 yq=2 x_rx_p=1 y_rx_np=1)
practice 4 sched: drained in 5 steps, X->Y order: P1 Pfwd Pfwd R
15/15 passed
```

## 10. 핵심 정리

- 종류 4개: P(MWr, Msg, MSI) / NPR(MRd, CfgRd, IORd) / NPW(CfgWr, IOWr, AtomicOp) / CPL. 표의 행 = 나중, 열 = 먼저, 칸 = "행이 열을 앞질러도 되나".
- 첫 열(먼저 보낸 Posted)은 No: 쓰기는 쓰기를(A2a), 읽기는 쓰기를(B2a), 완료는 쓰기를(D2a) 못 앞지른다 → read-back flush, data → CQE → MSI-X 순서의 근거.
- A3/A4/D3/D4 는 Yes(필수): P 와 CPL 은 NP 를 앞지를 수 있어야 한다. 아니면 NP → CPL → P → NP 고리로 데드락. 나머지는 Y/N.
- 예외는 **나중 TLP** 의 속성: RO(Attr[1], DevCtl RELAX_EN 0x0010) — 읽기에는 없음. IDO(Attr[2], DevCtl2 0x0100/0x0200) — 출발 ID(완료는 Completer ID)가 달라야. TC 가 다르면 무관. 같은 Tag 분할 완료는 순서 유지(D5b).
- 검사기 = 순열 검사(비트맵) + 역전 쌍마다 may_pass, O(n²), 정적 버퍼. 스케줄러 = credit 있고 앞의 남은 것을 다 앞지를 수 있는 첫 TLP.
- 드라이버: `writel(doorbell); readl(same device)` 로 도착 보장, arm64 `writel` 앞의 `dma_wmb()` 는 DMA 메모리 → 도어벨 순서(다른 층). SSD FW: data 에만 RO, CQE·MSI-X 는 RO/IDO 없이.

## 참고자료

- PCI-SIG, *PCI Express Base Specification* Revision 3.0 이후 — §2.4.1 Transaction Ordering Rules ("Ordering Rules Summary" 표, a)/b) 설명), §2.2.6.4 Relaxed Ordering / ID-Based Ordering Attributes, §7.5.3 Device Control / Device Control 2. 표 번호는 판마다 다름. C2b 의 RO 허용, A5b 브리지 예외, 완료 RO 복사 규칙은 원문 확인 권장 (회원 전용: <https://pcisig.com/specifications>).
- Mike Jackson, Ravi Budruk, *PCI Express Technology 3.0* (MindShare, 2012) — 8장 Transaction Ordering (데드락 예와 producer/consumer 모델).
- Linux `include/uapi/linux/pci_regs.h` — `PCI_EXP_DEVCTL 0x08`, `PCI_EXP_DEVCTL_RELAX_EN 0x0010`, `PCI_EXP_DEVCTL_NOSNOOP_EN 0x0800`, `PCI_EXP_DEVCTL2 0x28`, `PCI_EXP_DEVCTL2_IDO_REQ_EN 0x0100`, `PCI_EXP_DEVCTL2_IDO_CMP_EN 0x0200` (Lima VM `elinux` 의 `/usr/include/linux/pci_regs.h` 에서 확인).
- Linux `include/linux/pci.h` — `PCI_DEV_FLAGS_NO_RELAXED_ORDERING`, `pcie_relaxed_ordering_enabled()` (VM 의 linux-headers-7.0.0-38 에서 확인); `arch/arm64/include/asm/io.h` — `__io_bw()` = `dma_wmb()`, `__io_ar()` 와 udelay 주석.
- Linux 문서 `Documentation/driver-api/device-io.rst` (PCI posting 과 read-back), `Documentation/memory-barriers.txt` (I/O 배리어): <https://docs.kernel.org/driver-api/device-io.html>
- NVM Express Base Specification 2.x — Completion Queue Entry, Phase Tag, MSI-X 사용 (CQE·인터럽트 순서 맥락; 큐 구조는 PCIE-M01).
- 관련 레슨: PCIE-B01 (계층), PCIE-B02 (대역폭), PCIE-B06 (lspci DevCtl 해석), PCIE-B10 (TLP 헤더 Fmt/Type/Attr, 분할 완료), PCIE-M01 (SQ/CQ, Phase Tag), PCIE-M10 (MSI/MSI-X), PCIE-M11 (credit 흐름 제어), PCIE-M13 (Linux 드라이버), PCIE-A08 (ACS, P2P), ARCH-M12 (DMA 와 캐시 일관성, 배리어), BM-M08 (DMA 스트림 드라이버), BM-B04 (W1C/clear-on-read 레지스터), C-M14 (volatile 과 공유 변수)
