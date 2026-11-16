---
id: PCIE-B12
time: 50분
updated: 2026-11-15
---
# PCIE-B12 NVMe 컨트롤러 레지스터: CAP/VS/CC/CSTS/AQA/ASQ/ACQ

## 학습 목표

- NVMe BAR0 의 레지스터 맵(0x00 CAP … 0x68 CRTO, 0xE00 PMR, 0x1000 도어벨)을 C 구조체로 적고, `_Static_assert(offsetof(...) == ...)` 로 **오프셋이 틀리면 빌드가 깨지게** 만든다.
- CAP 64비트를 필드별로 풀어 숫자로 말한다: MQES(0-based → 엔트리 수), TO(500 ms 단위), DSTRD(도어벨 간격 4 << n), CSS, MPSMIN/MPSMAX(2^(12+n) 바이트).
- SQ y / CQ y 도어벨 주소를 공식으로 계산하고, BAR 크기와 큐 개수 사이의 관계를 설명한다.
- 64비트 레지스터를 32비트 두 번으로 읽는 이유(Linux `lo_hi_readq`)와, 값이 변하는 레지스터에서 생기는 찢어진 읽기(torn read)를 hi-lo-hi 재시도로 막는 법을 안다.
- (CC, CSTS) 덤프 하나로 컨트롤러 상태(동작 중, 초기화 중, 리셋 중, 치명적 오류, 셧다운, **장치 없음 = all-1s**)를 판정하는 프로브 리포트를 만든다. 실제 QEMU NVMe 캡처로 검증한다.

## 1. 문제

NVMe 드라이버가 `probe()` 에서 가장 먼저 하는 일은 BAR0 를 매핑하고 **CAP 을 읽는 것**이다. 큐 깊이, 타임아웃, 도어벨 위치가 전부 여기서 나온다. 그래서 SSD 펌웨어·호스트 드라이버·실리콘 검증 면접에서 "CAP 덤프를 보여 줄 테니 해석해 보세요", "SQ 3 도어벨 주소는?", "CSTS 가 0xFFFFFFFF 면?" 이 단골이다.

Don 이 SSD 컨트롤러 펌웨어 쪽에서 보던 그림을 호스트 쪽에서 다시 본다고 생각하면 된다. 컨트롤러(엔드포인트) 입장에서 CAP 은 펌웨어나 하드웨어 스트랩이 정하는 읽기 전용 값이고, 호스트가 CC.EN 을 1 로 쓰면 펌웨어에 인터럽트가 가서 admin 큐를 준비한 뒤 CSTS.RDY 를 세운다. 펌웨어가 assert 로 죽으면 CSTS.CFS 를 세우는 것도 펌웨어 몫이다. 이 레슨은 그 레지스터들을 **호스트 드라이버가 읽는 쪽**에서 디코드한다. 초기화 순서 자체(EN=0 → 설정 → EN=1 → RDY 대기)는 PCIE-B13, 큐와 도어벨을 실제로 돌리는 건 PCIE-M01 에서 다룬다.

BAR0 자체는 PCIe config space 의 BAR 로 잡힌 64-bit 메모리 영역이다 (BAR sizing 은 PCIE-B05, 클래스 코드 01:08:02 = NVMe 는 PCIE-B03). 이 레슨의 오프셋과 비트 위치는 **Linux `include/linux/nvme.h` (7.0 헤더) 기준**이다 (`NVME_REG_*`, `NVME_CAP_*`, `NVME_CC_*`, `NVME_CSTS_*`). 헤더에 없는 비트는 NVMe Base Spec 2.x 를 기억에 의존해 적었고, 5. 풀이의 표에 "스펙 확인 권장" 으로 표시했다.

```svg
<svg viewBox="0 0 740 540" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">NVMe BAR0 레지스터 맵 (nvme.h NVME_REG_*) — 실측 QEMU nvme 값</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="20" y="52">0x000</text><text x="20" y="74">0x008</text><text x="20" y="96">0x00C</text>
    <text x="20" y="118">0x010</text><text x="20" y="140">0x014</text><text x="20" y="162">0x018</text>
    <text x="20" y="184">0x01C</text><text x="20" y="206">0x020</text><text x="20" y="228">0x024</text>
    <text x="20" y="250">0x028</text><text x="20" y="272">0x030</text><text x="20" y="294">0x038</text>
    <text x="20" y="316">0x03C</text><text x="20" y="338">0x040</text><text x="20" y="360">0x06C</text>
    <text x="20" y="382">0xE00</text><text x="20" y="404">0xE1C</text>
    <text x="20" y="436">0x1000</text><text x="20" y="458">0x1004</text><text x="20" y="480">0x1008</text>
    <text x="20" y="502">0x100C</text>
  </g>
  <g stroke="currentColor">
    <rect x="80" y="38" width="230" height="22" class="f-acc"/><rect x="80" y="60" width="230" height="22" class="f-mut"/><rect x="80" y="82" width="230" height="22" class="f-mut"/>
    <rect x="80" y="104" width="230" height="22" class="f-mut"/><rect x="80" y="126" width="230" height="22" class="f-acc"/><rect x="80" y="148" width="230" height="22" fill="none" stroke-dasharray="4 3"/>
    <rect x="80" y="170" width="230" height="22" class="f-acc"/><rect x="80" y="192" width="230" height="22" class="f-mut"/><rect x="80" y="214" width="230" height="22" class="f-ok"/>
    <rect x="80" y="236" width="230" height="22" class="f-ok"/><rect x="80" y="258" width="230" height="22" class="f-ok"/><rect x="80" y="280" width="230" height="22" class="f-mut"/>
    <rect x="80" y="302" width="230" height="22" class="f-mut"/><rect x="80" y="324" width="230" height="22" class="f-mut"/><rect x="80" y="346" width="230" height="22" fill="none" stroke-dasharray="4 3"/>
    <rect x="80" y="368" width="230" height="22" class="f-mut"/><rect x="80" y="390" width="230" height="22" fill="none" stroke-dasharray="4 3"/><rect x="80" y="422" width="230" height="22" class="f-warn"/>
    <rect x="80" y="444" width="230" height="22" class="f-warn"/><rect x="80" y="466" width="230" height="22" class="f-warn"/><rect x="80" y="488" width="230" height="22" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="88" y="54">CAP (64)  기능·한계, 읽기 전용</text><text x="88" y="76">VS  버전</text><text x="88" y="98">INTMS  인터럽트 마스크 set</text>
    <text x="88" y="120">INTMC  인터럽트 마스크 clear</text><text x="88" y="142">CC  호스트가 쓰는 설정 (EN)</text><text x="88" y="164">예약 (빠뜨리기 쉬운 4 B)</text>
    <text x="88" y="186">CSTS  컨트롤러 상태 (RDY)</text><text x="88" y="208">NSSR  서브시스템 리셋</text><text x="88" y="230">AQA  admin 큐 크기</text>
    <text x="88" y="252">ASQ (64) admin SQ 주소</text><text x="88" y="274">ACQ (64) admin CQ 주소</text><text x="88" y="296">CMBLOC  CMB 위치</text>
    <text x="88" y="318">CMBSZ  CMB 크기</text><text x="88" y="340">BPINFO … CMBMSC … CRTO(0x68)</text><text x="88" y="362">예약 0x6C..0xDFF</text>
    <text x="88" y="384">PMRCAP … PMRMSCU (PMR)</text><text x="88" y="406">예약/명령 집합별 ..0xFFF</text><text x="88" y="438">SQ0 tail 도어벨 (admin)</text>
    <text x="88" y="460">CQ0 head 도어벨 (admin)</text><text x="88" y="482">SQ1 tail 도어벨</text><text x="88" y="504">CQ1 head 도어벨</text>
  </g>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="330" y="54">0x004008200F0107FF</text><text x="330" y="76">0x00010400 = 1.4.0</text><text x="330" y="142">0x00000000 → 0x00460061 (nvme.ko 후)</text>
    <text x="330" y="186">0x00000000 → 0x00000001 (RDY)</text><text x="330" y="230">0x001F001F (32/32 엔트리)</text><text x="330" y="252">0x0000000043F3C000</text>
    <text x="330" y="274">0x0000000043F3B000</text><text x="330" y="296">0 (CMB 없음)</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="330" y="420">도어벨 간격 = 4 &lt;&lt; CAP.DSTRD 바이트</text><text x="330" y="438">DSTRD=0 (QEMU): 4 B → 왼쪽 주소</text><text x="330" y="456">DSTRD=2: SQ0 0x1000, CQ0 0x1010,</text>
    <text x="330" y="474">         SQ1 0x1020, CQ1 0x1030</text><text x="330" y="496">SQ y = 0x1000 + (2y)·간격</text><text x="330" y="514">CQ y = 0x1000 + (2y+1)·간격</text>
  </g>
  <text x="20" y="532" fill="currentColor" font-size="12">QEMU BAR0 = 16 KiB (resource 0x10000000-0x10003fff) → DSTRD=0 이면 큐 쌍 1536 개까지 (연습 3)</text>
</svg>
```

오른쪽 값은 이 레슨에서 직접 캡처한 **QEMU 가 흉내 낸 NVMe 컨트롤러**(실리콘 아님) 값이다: Lima VM 안의 `qemu-system-aarch64 -machine virt` + `pcie-root-port` + `-device nvme`, 게스트(Linux 6.18.55, BusyBox)에서 `devmem` 32-bit 읽기. 드라이버 없이 한 번, `nvme.ko` 를 올린 뒤 한 번 (`capture_qemu_nvme_bar0.txt`, 스크립트 `capture_qemu_bar0*.sh`).

### Part A — 레지스터 맵, 도어벨, 디코더

1. **A1** `nvme_bar_regs` 구조체를 완성한다. 시작 코드에는 예약 영역 두 곳(0x18 의 4 B, 0x6C..0xDFF)이 틀려 있다. 고친 뒤 `NVME_LAYOUT_CHECKED` 를 1 로 바꿔 `_Static_assert` 19개를 켠다 (`sizeof == 0x1000` 포함).
2. **A2** `nvme_sq_tail_db(qid, dstrd)`, `nvme_cq_head_db(qid, dstrd)`, `nvme_bar_min_size(nr_io_queues, dstrd)` — qid 0..65535, dstrd 0..15 에서 32-bit 를 넘을 수 있으니 `uint64_t`.
3. **A3** 디코더 5개: `nvme_cap_decode` (MQES, CQR, AMS, TO, DSTRD, NSSRS, CSS, BPS, MPSMIN/MAX, PMRS, CMBS, CRMS + 파생값 max_entries, to_ms, db_stride, mps_*_bytes), `nvme_vs_decode` (major.minor.tertiary), `nvme_cc_decode` (EN, CSS, MPS, AMS, SHN, IOSQES, IOCQES, CRIME + 바이트 값), `nvme_csts_decode` (RDY, CFS, SHST, NSSRO, PP), `nvme_aqa_decode` (ASQS/ACQS, 0-based).

### Part B — 64-bit 접근과 이상값

1. **B1** `nvme_rd64_lohi()`: 32-bit 읽기 두 번(하위 먼저)으로 64-bit 레지스터 읽기, 8 의 배수가 아닌 오프셋은 접근 없이 거부. `rd64_hilohi()`: 값이 바뀌는 64-bit 레지스터(모의 장치의 **가상** 카운터)에서 hi → lo → hi 재시도. 모의 장치는 접근 로그를 남기므로 "32-bit 두 번, 하위 먼저" 를 테스트로 확인한다.
2. **B2** `nvme_cap_check()`: 스펙상 나올 수 없는 CAP 거르기 — all-1s, MQES=0, MPSMIN > MPSMAX, CSS=0.
3. **B3** `nvme_classify(cap, cc, csts)`: GONE(all-1s) → BAD_CAP → FATAL(CFS) → SHST(10/01/11) → (EN, RDY) 2×2 순서로 상태를 정한다. CSTS 하위 6비트 × EN 전수 검사.

### Part C — 프로브 리포트

**C1** `nvme_report(bar, len, out, cap)`: BAR0 바이트 덤프(little-endian)를 받아 VS/CAP/CC/CSTS/AQA/ASQ/ACQ/도어벨 줄과 상태별 진단 문장을 caller 버퍼에 쓴다(넘치면 잘라서 NUL 종료). 입력: 실제 QEMU 캡처 2개(드라이버 전/후), 가상 "소비자 SSD" 덤프 1개와 거기서 만든 고장 덤프(CFS, EN=1·RDY=0, 셧다운 완료, MQES=0, all-1s, 0x40 B 미만).

## 2. Problem (English)

> You are writing the probe path of an NVMe host driver (or a validation tool that inspects a controller over PCIe). BAR0 has been mapped; registers are little-endian.
>
> 1. Define `nvme_bar_regs` covering 0x000–0xFFF and prove the layout at compile time with `_Static_assert(offsetof(...))` (CAP 0x00, VS 0x08, INTMS 0x0C, INTMC 0x10, CC 0x14, CSTS 0x1C, NSSR 0x20, AQA 0x24, ASQ 0x28, ACQ 0x30, CMBLOC 0x38, CMBSZ 0x3C, …, CRTO 0x68, PMRCAP 0xE00; `sizeof == 0x1000`).
> 2. `uint64_t nvme_sq_tail_db(uint32_t qid, uint32_t dstrd)` / `nvme_cq_head_db(...)` — doorbell offsets. Stride is `4 << CAP.DSTRD`.
> 3. Decode CAP, VS, CC, CSTS, AQA into structs (MQES is 0-based, TO is in 500 ms units, page size is `2^(12+MPS)`).
> 4. Read 64-bit registers using two 32-bit accesses (low dword first); for a register that changes, use hi-lo-hi with bounded retries.
> 5. `nv_state nvme_report(const uint8_t *bar, size_t len, char *out, size_t cap)` — print a human-readable report and classify the controller: READY / ENABLING / DISABLING / DISABLED / FATAL / SHUTDOWN_* / GONE (all-ones) / BAD_CAP / TRUNC.
>
> Example (real QEMU capture after `nvme.ko` probe): CAP = `0x004008200F0107FF`, CC = `0x00460061`, CSTS = `0x00000001` →
> `MQES=2047 (2048 entries) CQR=1 TO=15 (7500 ms) DSTRD=0 CSS=0x41 MPS 4096..65536`, `CC EN=1 CSS=6 IOSQES=6 (64 B) IOCQES=4 (16 B)`, state `READY`, SQ1 tail doorbell `0x1008`.
>
> Constraints: no heap; the report goes into a caller buffer; never read past `len`.
>
> Follow-ups: (1) MQES = 1023 — how many entries? How many commands can be outstanding? (2) Why does Linux read CAP with `lo_hi_readq`? (3) CSTS reads `0xFFFFFFFF` — is the controller in fatal state? (4) What does CAP.TO bound, and how long does Linux actually wait? (5) CC.MPS vs PCIe Max_Payload_Size?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO: A1 (구조체 예약 두 곳 + `NVME_LAYOUT_CHECKED`), A2 도어벨 3개, A3 디코더 5개, B1 64-bit 읽기 2개, B2 `nvme_cap_check`, B3 `nvme_classify`, C1 `nvme_report`. 스텁은 0 / false / `CAPCHK_OK` / `NVS_DISABLED` / `NVS_TRUNC` 를 돌려주어 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:346  offsetof(nvme_bar_regs, csts) == NVME_REG_CSTS
FAIL starter.c:347  offsetof(nvme_bar_regs, nssr) == NVME_REG_NSSR && offsetof(nvme_bar_regs, aqa) == NVME_REG_AQA
FAIL starter.c:350  offsetof(nvme_bar_regs, pmrcap) == NVME_REG_PMRCAP
FAIL starter.c:351  sizeof(nvme_bar_regs) == 0x1000u
…
--- QEMU nvme, 드라이버 없음 (실측) ---
STATE TRUNC: TODO C1
…
9/80 passed
```

(exit 1.) 재미있는 점: 시작 코드의 구조체는 0x18 예약을 빠뜨렸는데도 **ASQ/ACQ 오프셋 검사는 통과한다**. 이유는 7. 자주 하는 실수 1.

## 4. 힌트

<details><summary>힌트 1 — 구조체: 오프셋을 주석으로 적고, 빈 곳은 전부 예약 필드로</summary>

필드마다 `/* 0x1C */` 처럼 오프셋을 적어 두면 빈 곳이 보인다. CC(0x14, 4 B) 다음은 0x18 인데 CSTS 는 0x1C 다 → `uint32_t rsvd18;`. CRTO(0x68, 4 B) 다음 0x6C 부터 PMRCAP(0xE00) 전까지는 `uint8_t rsvd6c[0xE00 - 0x6C];` — 크기를 **식으로** 쓰면 실수가 줄어든다. 그다음 `NVME_LAYOUT_CHECKED` 를 1 로 바꾸고 빌드가 통과하는지 본다.

</details>

<details><summary>힌트 2 — 도어벨: SQ 와 CQ 가 번갈아 놓인다</summary>

0x1000 부터 "SQ0 tail, CQ0 head, SQ1 tail, CQ1 head, …" 순서로 간격 `4 << DSTRD` 마다 하나씩. 그러면 SQ y 는 (2y) 번째, CQ y 는 (2y+1) 번째 칸이다. `qid = 65535, dstrd = 15` 면 131071 × 131072 ≈ 1.7 × 10^10 — `uint32_t` 로 곱하면 넘친다. 곱하기 전에 `2ull *` 로 64-bit 로 올린다.

</details>

<details><summary>힌트 3 — CAP: 위 32비트는 32 를 빼고 생각하기</summary>

DSTRD 35:32, NSSRS 36, CSS 44:37, BPS 45, MPSMIN 51:48, MPSMAX 55:52, PMRS 56, CMBS 57, CRMS 60:59. `(cap >> 37) & 0xFF` 처럼 64-bit 그대로 밀면 된다. 실측 상위 dword `0x00400820` 을 손으로 풀어 보라: bit 5 와 bit 11 → 절대 비트 37, 43 → CSS = 0b0100_0001 = 0x41; bit 22 → 절대 54 → MPSMAX 필드(55:52) 안에서 값 4.

</details>

<details><summary>힌트 4 — 0-based 필드와 "단위" 필드</summary>

MQES, AQA.ASQS/ACQS 는 0-based: 값 + 1 이 엔트리 수. TO 는 500 ms 단위, MPS 류는 2^(12+n) 바이트, IOSQES/IOCQES 는 2^n 바이트, DSTRD 는 4 << n 바이트. 디코더 구조체에 **원래 필드 값과 사람이 읽는 값을 둘 다** 넣어 두면 리포트와 테스트가 편하다.

</details>

<details><summary>힌트 5 — 상태 분류는 순서가 전부다</summary>

CSTS = 0xFFFFFFFF 는 CFS 비트도 1, RDY 도 1, SHST 도 11 이다. CFS 를 먼저 보면 "치명적 오류 → 리셋" 이라는 틀린 결론이 나온다. 그래서 GONE(all-1s) 을 맨 앞에 둔다. 그다음 CAP 이 말이 되는지, 그다음 CFS, SHST, 마지막에 EN/RDY 2×2.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 어디서 읽나 | PCIe BAR0(64-bit 메모리 BAR) 를 iomap, Command.Memory Space 켜기 (PCIE-B03/B05). 레지스터는 LE |
| 2. 맵 | 0x00 CAP … 0x3C CMBSZ 는 외워 둔다. 0x18 은 예약. 0x1000 부터 도어벨, 간격은 DSTRD |
| 3. CAP 먼저 | MQES → 큐 깊이, TO → 타임아웃, DSTRD → 도어벨, MPSMIN/MAX → CC.MPS 범위, CSS → CC.CSS |
| 4. 접근 폭 | 32-bit 접근, 64-bit 는 하위 → 상위 두 번 (`lo_hi_readq`). 바이트/16-bit 접근 금지 |
| 5. 상태 | CSTS all-1s 먼저 확인 → CFS → SHST → EN/RDY |
| 6. 다음 | 초기화 순서는 B13, 큐/도어벨 운용은 M01 |

### Part A — 레지스터 맵

| 오프셋 | 이름 | 폭 | 내용 | 근거 |
|---|---|---|---|---|
| 0x00 | CAP | 64 | Controller Capabilities (읽기 전용) | nvme.h |
| 0x08 | VS | 32 | Version | nvme.h |
| 0x0C / 0x10 | INTMS / INTMC | 32 | 인터럽트 마스크 set / clear (MSI-X 쓰면 사용 안 함) | nvme.h |
| 0x14 | CC | 32 | Controller Configuration | nvme.h |
| 0x18 | — | 32 | 예약 | nvme.h 에 이름 없음 (CC 0x14 → CSTS 0x1C 사이) |
| 0x1C | CSTS | 32 | Controller Status | nvme.h |
| 0x20 | NSSR | 32 | NVM Subsystem Reset (`"NVMe"` = 0x4E564D65 쓰기) | nvme.h, 값은 스펙 확인 권장 |
| 0x24 | AQA | 32 | Admin Queue Attributes | nvme.h |
| 0x28 / 0x30 | ASQ / ACQ | 64 | admin SQ / CQ 물리 주소 | nvme.h |
| 0x38 / 0x3C | CMBLOC / CMBSZ | 32 | Controller Memory Buffer 위치/크기 | nvme.h |
| 0x40 / 0x44 / 0x48 | BPINFO / BPRSEL / BPMBL(64) | | Boot Partition | nvme.h |
| 0x50 | CMBMSC | 64 | CMB Memory Space Control | nvme.h |
| 0x58 / 0x5C / 0x60 / 0x64 | CMBSTS / CMBEBS / CMBSWTP / NSSD | 32 | | **스펙 확인 권장** (nvme.h 에 없음) |
| 0x68 | CRTO | 32 | Controller Ready Timeouts | nvme.h |
| 0xE00..0xE10 | PMRCAP / PMRCTL / PMRSTS / PMREBS / PMRSWTP | 32 | Persistent Memory Region | nvme.h |
| 0xE14 / 0xE18 | PMRMSCL / PMRMSCU | 32 | | **스펙 확인 권장** |
| 0x1000 | SQ0TDBL | 32 | 이후 도어벨 | nvme.h `NVME_REG_DBS` |

구조체로 적으면 `sizeof == 0x1000` 이 된다. 도어벨은 간격이 CAP.DSTRD 로 바뀌므로 구조체 밖에서 계산한다 (`dev->dbs = dev->bar + 4096;` — Linux `drivers/nvme/host/pci.c` 와 같은 방식).

`_Static_assert` 가 하는 일: 오프셋이 하나라도 틀리면 **컴파일이 실패**한다. 시작 코드에서 `NVME_LAYOUT_CHECKED` 만 1 로 바꾸면 clang 이 이렇게 말한다 (실제 출력):

```text
error: static assertion failed due to requirement '__builtin_offsetof(nvme_bar_regs, csts) == 28U': CSTS
error: static assertion failed due to requirement '__builtin_offsetof(nvme_bar_regs, nssr) == 32U': NSSR
error: static assertion failed due to requirement '__builtin_offsetof(nvme_bar_regs, aqa) == 36U': AQA
error: static assertion failed due to requirement '__builtin_offsetof(nvme_bar_regs, pmrcap) == 3584U': PMRCAP
error: static assertion failed due to requirement '__builtin_offsetof(nvme_bar_regs, pmrswtp) == 3600U': PMRSWTP
error: static assertion failed due to requirement 'sizeof(nvme_bar_regs) == 4096U': doorbells start at 0x1000
```

ASQ/ACQ/CRTO 는 틀렸다고 나오지 않는다 — 7. 실수 1 에서 이유를 본다.

### Part A — 도어벨

| | DSTRD=0 (간격 4 B) | DSTRD=2 (간격 16 B) |
|---|---|---|
| SQ0 tail | 0x1000 | 0x1000 |
| CQ0 head | 0x1004 | 0x1010 |
| SQ1 tail | 0x1008 | 0x1020 |
| CQ1 head | 0x100C | 0x1030 |
| SQ3 tail / CQ3 head | 0x1018 / 0x101C | 0x1060 / 0x1070 |

공식: `SQ y = 0x1000 + (2y)·(4 << DSTRD)`, `CQ y = 0x1000 + (2y+1)·(4 << DSTRD)`. Linux 는 `u32 __iomem *dbs` 포인터에 `qid * 2 * db_stride` (u32 단위, `db_stride = 1 << DSTRD`) 를 더한다 — 바이트로 바꾸면 같은 식이다. admin + N 개 I/O 큐면 BAR0 는 최소 `0x1000 + (N+1)·2·(4 << DSTRD)` 바이트 (`db_bar_size()` 와 같은 식). 테스트는 DSTRD 0..15 × qid 0..4095 전수로 "SQ→CQ 간격 = stride, CQ(q−1)→SQ(q) 간격 = stride, 최소 크기 = 마지막 CQ 도어벨 + stride" 를 확인한다.

DSTRD 가 왜 있나: 도어벨 하나하나를 서로 다른 캐시 라인/페이지에 떨어뜨려 놓으면, 가상화(에뮬레이션)에서 도어벨 쓰기를 트랩하기 쉽고 여러 CPU 가 서로 다른 큐 도어벨을 칠 때 간섭이 준다. 실제 SSD 는 대부분 DSTRD=0 이다.

### Part A — CAP 비트 배치

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">CAP (0x00, 64-bit) — 실측 QEMU 0x00400820_0F0107FF</text><text x="10" y="50" fill="currentColor" font-size="13">63..32</text><text x="10" y="200" fill="currentColor" font-size="13">31..0</text>
  <g fill="currentColor" font-family="monospace">
    <text x="80" y="34">63</text><text x="702" y="34">32</text>
    <text x="80" y="184">31</text><text x="702" y="184">0</text>
  </g>
  <g stroke="currentColor">
    <rect x="80" y="38" width="60" height="30" class="f-mut"/><rect x="140" y="38" width="40" height="30" class="f-ok"/><rect x="180" y="38" width="20" height="30" class="f-mut"/>
    <rect x="200" y="38" width="20" height="30" class="f-ok"/><rect x="220" y="38" width="20" height="30" class="f-ok"/><rect x="240" y="38" width="80" height="30" class="f-acc"/>
    <rect x="320" y="38" width="80" height="30" class="f-acc"/><rect x="400" y="38" width="40" height="30" class="f-mut"/><rect x="440" y="38" width="20" height="30" class="f-ok"/>
    <rect x="460" y="38" width="160" height="30" class="f-acc"/><rect x="620" y="38" width="20" height="30" class="f-ok"/><rect x="640" y="38" width="80" height="30" class="f-acc"/>
    <rect x="80" y="188" width="160" height="30" class="f-acc"/><rect x="240" y="188" width="100" height="30" class="f-mut"/><rect x="340" y="188" width="40" height="30" class="f-ok"/>
    <rect x="380" y="188" width="20" height="30" class="f-ok"/><rect x="400" y="188" width="320" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="110" y="58">rsvd</text><text x="160" y="58">CRMS</text><text x="280" y="58">MPSMAX</text>
    <text x="360" y="58">MPSMIN</text><text x="420" y="58">CPS</text><text x="540" y="58">CSS 44:37</text>
    <text x="680" y="58">DSTRD</text><text x="160" y="208">TO 31:24</text><text x="290" y="208">rsvd 23:19</text>
    <text x="360" y="208">AMS</text><text x="560" y="208">MQES 15:0 (0-based)</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="182" y="86">58 NSSS</text><text x="205" y="102">57 CMBS</text><text x="226" y="118">56 PMRS</text>
    <text x="420" y="86">45 BPS</text><text x="600" y="86">36 NSSRS</text><text x="372" y="236">16 CQR</text>
  </g>
  <g stroke="currentColor" stroke-width="1">
    <line x1="190" y1="68" x2="190" y2="76"/><line x1="210" y1="68" x2="210" y2="92"/><line x1="230" y1="68" x2="230" y2="108"/>
    <line x1="450" y1="68" x2="450" y2="76"/><line x1="630" y1="68" x2="630" y2="76"/><line x1="390" y1="218" x2="390" y2="226"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="80" y="146">상위 0x00400820: MPSMAX=4 (64 KiB)  MPSMIN=0 (4 KiB)  CSS=0x41  DSTRD=0  나머지 0</text><text x="80" y="266">하위 0x0F0107FF: TO=0x0F (7.5 s)  CQR=1  AMS=0  MQES=0x7FF (2048 엔트리)</text>
  </g>
  <text x="80" y="300" fill="currentColor">진한 칸 = 이 레슨이 디코드하는 주요 필드, 옅은 칸 = 예약 또는 신규(CPS, NSSS — 스펙 확인 권장)</text><text x="80" y="320" fill="currentColor">칸 폭 20 px = 1 bit, 왼쪽이 높은 비트</text>
</svg>
```

| 필드 | 비트 | 의미 | 디코드 | nvme.h |
|---|---|---|---|---|
| MQES | 15:0 | Maximum Queue Entries Supported, **0-based**, I/O 큐 하나의 최대 엔트리 | +1. 0 은 무효 | `NVME_CAP_MQES` |
| CQR | 16 | Contiguous Queues Required (1 이면 큐 메모리가 물리적으로 연속이어야) | 1비트 | 없음 — 스펙 확인 권장 |
| AMS | 18:17 | 지원 중재: bit0 WRR+Urgent, bit1 벤더 고유 (RR 은 항상) | 2비트 | 없음 — 스펙 확인 권장 |
| TO | 31:24 | CC.EN 변경 뒤 CSTS.RDY 가 따라올 때까지 **최악 시간**, 500 ms 단위 | ×500 ms | `NVME_CAP_TIMEOUT` |
| DSTRD | 35:32 | 도어벨 간격 2^(2+n) 바이트 | 4 << n | `NVME_CAP_STRIDE` |
| NSSRS | 36 | NSSR 지원 | 1비트 | `NVME_CAP_NSSRC` |
| CSS | 44:37 | 지원 명령 집합: bit0 NVM, bit6 I/O Command Set(s), bit7 admin 만 | 8비트 | `NVME_CAP_CSS`, `NVME_CAP_CSS_NVM/CSI` (bit7 은 스펙 확인 권장) |
| BPS | 45 | Boot Partition 지원 | 1비트 | 없음 — 스펙 확인 권장 |
| MPSMIN / MPSMAX | 51:48 / 55:52 | CC.MPS 로 고를 수 있는 메모리 페이지 크기 범위 | 2^(12+n) B | `NVME_CAP_MPSMIN/MAX` |
| PMRS / CMBS | 56 / 57 | PMR / CMB 지원 | 1비트 | `NVME_CAP_CMBS` (PMRS 는 스펙 확인 권장) |
| CRMS | 60:59 | bit59 CRWMS, bit60 CRIMS (Controller Ready With/Independent of Media) | 2비트 | `NVME_CAP_CRMS_CRWMS/CRIMS` |

손으로 한 번 풀기 — 실측 `CAP = 0x004008200F0107FF`:

1. 하위 dword `0x0F0107FF`: 비트 15:0 = `0x07FF` = 2047 → **2048 엔트리**. 비트 16 = `0x0F01_07FF` 의 `…01…` 의 1 → CQR=1. 비트 18:17 = 0 → AMS=0 (RR 만). 비트 31:24 = `0x0F` = 15 → **7500 ms**.
2. 상위 dword `0x00400820` = 이진 `0000 0000 0100 0000 0000 1000 0010 0000`. 켜진 비트는 상위 dword 기준 5, 11, 22 → CAP 비트 37, 43, 54.
3. 37 과 43 은 CSS(44:37) 의 bit0, bit6 → CSS = 0x41 = NVM + I/O Command Set. 54 는 MPSMAX(55:52) 의 bit2 → MPSMAX = 4 → 2^16 = **64 KiB**. MPSMIN = 0 → 4 KiB. DSTRD = 0 → 4 B. NSSRS = 0, CMBS = 0, PMRS = 0.

Linux 커널 nvmet(타깃) 코드도 같은 값을 쓴다: `ctrl->cap = (1ULL << 37); ctrl->cap |= (1ULL << 43); /* Controller supports one or more I/O Command Sets */ ctrl->cap |= (15ULL << 24); /* CC.EN timeout in 500msec units */` (`drivers/nvme/target/core.c` 6.18.55). 숫자를 기억에서 꺼낼 때의 좋은 교차 확인이다.

### Part A — CC, CSTS, VS, AQA

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">CC (0x14) = 0x00460061 (QEMU, nvme.ko 가 씀) · CSTS (0x1C) = 0x00000001</text>
  <g fill="currentColor" font-family="monospace"><text x="80" y="36">31</text><text x="708" y="36">0</text></g>
  <text x="20" y="60" fill="currentColor" font-size="13">CC</text><text x="20" y="180" fill="currentColor" font-size="13">CSTS</text>
  <g stroke="currentColor">
    <rect x="80" y="40" width="140" height="30" class="f-mut"/><rect x="220" y="40" width="20" height="30" class="f-ok"/><rect x="240" y="40" width="80" height="30" class="f-acc"/>
    <rect x="320" y="40" width="80" height="30" class="f-acc"/><rect x="400" y="40" width="40" height="30" class="f-ok"/><rect x="440" y="40" width="60" height="30" class="f-ok"/>
    <rect x="500" y="40" width="80" height="30" class="f-ok"/><rect x="580" y="40" width="60" height="30" class="f-acc"/><rect x="640" y="40" width="60" height="30" class="f-mut"/>
    <rect x="700" y="40" width="20" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="150" y="60">rsvd 31:25</text><text x="280" y="60">IOCQES</text><text x="360" y="60">IOSQES</text>
    <text x="420" y="60">SHN</text><text x="470" y="60">AMS</text><text x="540" y="60">MPS</text>
    <text x="610" y="60">CSS</text><text x="670" y="60">rsvd</text>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="230" y="88">0</text><text x="280" y="88">4</text><text x="360" y="88">6</text><text x="420" y="88">0</text>
    <text x="470" y="88">0</text><text x="540" y="88">0</text><text x="610" y="88">6</text><text x="710" y="88">1</text>
  </g>
  <g fill="currentColor">
    <text x="200" y="106">24 CRIME</text><text x="640" y="106">0 EN</text><text x="80" y="126">CQE 16 B · SQE 64 B · 페이지 4 KiB · CSS=6 = CAP.CSS 의 모든 I/O 집합</text>
  </g>
  <g fill="currentColor" font-family="monospace"><text x="80" y="156">31</text><text x="708" y="156">0</text></g>
  <g stroke="currentColor">
    <rect x="80" y="160" width="500" height="30" class="f-mut"/><rect x="580" y="160" width="20" height="30" class="f-mut"/><rect x="600" y="160" width="20" height="30" class="f-ok"/>
    <rect x="620" y="160" width="20" height="30" class="f-ok"/><rect x="640" y="160" width="40" height="30" class="f-acc"/><rect x="680" y="160" width="20" height="30" class="f-warn"/>
    <rect x="700" y="160" width="20" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="330" y="180">예약 31:7</text><text x="660" y="180">SHST</text>
  </g>
  <g fill="currentColor">
    <text x="470" y="208">6 ST(2.x, 스펙 확인) · 5 PP · 4 NSSRO</text><text x="640" y="226">1 CFS · 0 RDY</text><text x="80" y="244">SHST: 00 정상 · 01 셧다운 처리 중 · 10 셧다운 완료 · 11 예약</text>
  </g>
</svg>
```

| 레지스터 | 필드 | 비트 | 의미 (nvme.h 기준) |
|---|---|---|---|
| CC | EN | 0 | 1 = 컨트롤러 동작, 1→0 = 컨트롤러 리셋 |
| CC | CSS | 6:4 | 0 = NVM, 6 = CAP.CSS 가 알리는 모든 I/O 집합 (`NVME_CC_CSS_CSI`), 7 = admin 만 (스펙 확인 권장) |
| CC | MPS | 10:7 | 메모리 페이지 = 2^(12+MPS). MPSMIN ≤ MPS ≤ MPSMAX 이어야 |
| CC | AMS | 13:11 | 0 RR, 1 WRR+Urgent, 7 벤더 |
| CC | SHN | 15:14 | 00 없음, 01 정상 셧다운, 10 급정지(abrupt) |
| CC | IOSQES / IOCQES | 19:16 / 23:20 | I/O SQ/CQ 엔트리 = 2^n 바이트. NVM 은 6 (64 B) / 4 (16 B) |
| CC | CRIME | 24 | 미디어 준비와 무관하게 RDY (Linux 는 끈다) |
| CSTS | RDY / CFS | 0 / 1 | 준비 / Controller Fatal Status |
| CSTS | SHST | 3:2 | 셧다운 상태 |
| CSTS | NSSRO / PP | 4 / 5 | NSSR 발생 / Processing Paused |
| VS | MJR / MNR / TER | 31:16 / 15:8 / 7:0 | `NVME_VS(1,4,0)` = 0x00010400 |
| AQA | ASQS / ACQS | 11:0 / 27:16 | admin SQ/CQ 크기, 0-based (Linux: 두 필드 모두 depth−1 을 씀 → 32 엔트리면 0x001F001F). 비트 폭은 스펙 확인 권장 |
| ASQ / ACQ | 주소 | 63:12 | 하위 12비트는 예약 → 최소 4 KiB 정렬 (CC.MPS 페이지 정렬 요구는 스펙 확인 권장) |

실측 `CC = 0x00460061` 을 손으로: `0x0046_0061` → 비트 0 = 1 (EN), 비트 6:4 = `0x61 >> 4 & 7` = 6 (CSS), 비트 10:7 = 0 (MPS), 19:16 = `0x46 & 0xF` = 6 (IOSQES), 23:20 = 4 (IOCQES). Linux `nvme_enable_ctrl()` 의 코드와 정확히 맞는다: CAP.CSS 에 bit6 이 있으면 `NVME_CC_CSS_CSI`, 페이지 4 KiB(`NVME_CTRL_PAGE_SHIFT - 12 = 0`), `NVME_CC_AMS_RR | NVME_CC_SHN_NONE`, `NVME_CC_IOSQES | NVME_CC_IOCQES` 를 먼저 쓰고 EN 을 켠다 (연습 4 가 CAP 에서 이 값을 재현). AQA `0x001F001F` 는 `NVME_AQ_DEPTH = 32` 그대로다.

### Part B — 64-bit 레지스터를 32-bit 두 번으로

CAP, ASQ, ACQ, BPMBL, CMBMSC 는 64-bit 다. 그런데 Linux `pci.c` 는 `dev->ctrl.cap = lo_hi_readq(dev->bar + NVME_REG_CAP);` 로 읽는다. `lo_hi_readq` 는 `readl(p)` 다음 `readl(p + 1)` — **32-bit 두 번, 하위 먼저**. 이유:

- 모든 호스트 브리지/CPU 가 64-bit MMIO 읽기 한 번을 지원하지는 않는다 (32-bit 플랫폼, 일부 SoC 루트 컴플렉스). 컨트롤러 쪽도 64-bit 접근 지원을 보장받기 어렵다 (스펙의 접근 폭 문구는 스펙 확인 권장).
- CAP 은 값이 바뀌지 않으므로 두 번 나눠 읽어도 찢어질 일이 없다. ASQ/ACQ 는 호스트가 EN=0 일 때 쓰는 값이라 역시 안전하다 (`lo_hi_writeq`).

값이 바뀌는 64-bit 레지스터(예: 실리콘 검증에서 보는 free-running 타임스탬프 카운터)라면 이야기가 다르다. 이 레슨의 모의 장치에는 접근할 때마다 1씩 증가하는 **가상** 카운터(오프셋 0xF00, NVMe 에는 없음)를 두었다. 실측 (`solution.c` B1 테스트):

| 방법 | 카운터 시작값 | 읽은 값 | 판정 |
|---|---|---|---|
| lo → hi | 0x1_FFFFFFFF | 0x2_FFFFFFFF | 찢어짐: lo 를 읽은 직후 올림이 생겨 hi 가 2 → 실제보다 4 Gi 크다 |
| hi → lo → hi | 0x1_FFFFFFFF | 0x2_00000002 | 첫 시도 hi 1 ≠ 2 → 재시도 1회 (접근 6번), 결과는 [시작, 끝] 안 |

```c
for (unsigned t = 0; t < max_try; t++) {          /* 무한 루프 금지: 장치가 사라지면 hi 가 계속 같지 않을 수도 */
    uint32_t hi1 = mmio_rd32(m, off + 4u);
    uint32_t lo  = mmio_rd32(m, off);
    uint32_t hi2 = mmio_rd32(m, off + 4u);
    if (hi1 == hi2) { *out = (uint64_t)hi1 << 32 | lo; return true; }
}
```

장치가 사라지면(`gone`) 모든 32-bit 읽기가 0xFFFFFFFF 이므로 hi1 == hi2 가 되어 `0xFFFFFFFF_FFFFFFFF` 가 나온다 — 그래서 다음 단계(상태 분류)가 all-1s 를 먼저 거른다.

### Part B — 상태 분류

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs>
    <marker id="b12ar" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto">
      <path d="M0,0 L8,4 L0,8 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">nvme_classify(cap, cc, csts): 위에서부터 첫 번째로 맞는 것</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="200" height="34" rx="5" class="f-mut"/><rect x="20" y="92" width="200" height="34" rx="5" class="f-mut"/><rect x="20" y="144" width="200" height="34" rx="5" class="f-mut"/>
    <rect x="20" y="196" width="200" height="34" rx="5" class="f-mut"/><rect x="20" y="248" width="200" height="34" rx="5" class="f-mut"/><rect x="300" y="40" width="160" height="34" rx="5" class="f-warn"/>
    <rect x="300" y="92" width="160" height="34" rx="5" class="f-warn"/><rect x="300" y="144" width="160" height="34" rx="5" class="f-warn"/><rect x="300" y="196" width="160" height="34" rx="5" class="f-acc"/>
    <rect x="300" y="248" width="80" height="34" rx="5" class="f-ok"/><rect x="384" y="248" width="80" height="34" rx="5" class="f-acc"/><rect x="468" y="248" width="86" height="34" rx="5" class="f-acc"/>
    <rect x="558" y="248" width="100" height="34" rx="5" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="62">CSTS==~0 or CAP==~0 ?</text><text x="30" y="114">CAP 불가능 값?</text><text x="30" y="166">CSTS.CFS == 1 ?</text>
    <text x="30" y="218">CSTS.SHST != 00 ?</text><text x="30" y="270">(CC.EN, CSTS.RDY)</text><text x="310" y="62">GONE</text>
    <text x="310" y="114">BAD_CAP</text><text x="310" y="166">FATAL</text><text x="310" y="218">SHST 10/01/11</text>
    <text x="310" y="270">1,1 READY</text><text x="392" y="270">1,0 ENABL.</text><text x="474" y="270">0,1 DISABL.</text>
    <text x="564" y="270">0,0 DISABLED</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#b12ar)">
    <line x1="220" y1="57" x2="298" y2="57"/><line x1="220" y1="109" x2="298" y2="109"/><line x1="220" y1="161" x2="298" y2="161"/>
    <line x1="220" y1="213" x2="298" y2="213"/><line x1="220" y1="265" x2="298" y2="265"/><line x1="120" y1="74" x2="120" y2="90"/>
    <line x1="120" y1="126" x2="120" y2="142"/><line x1="120" y1="178" x2="120" y2="194"/><line x1="120" y1="230" x2="120" y2="246"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="232" y="52">예</text><text x="126" y="86">아니오</text>
    <text x="480" y="60">all-1s 는 CFS·RDY·SHST 도 전부 1 →</text><text x="480" y="78">CFS 를 먼저 보면 "치명적 오류" 로 오진</text><text x="480" y="166">리셋(EN=0) 후 재초기화, PCIE-A03</text><text x="480" y="218">10 DONE · 01 BUSY · 11 RSVD</text>
    <text x="300" y="297">ENABL.(1,0) 이 CAP.TO 넘게 지속 → 타임아웃 (PCIE-B13)</text>
  </g>
</svg>
```

CAP 이 "불가능한 값" 인 경우 (B2): MQES = 0 (스펙상 최소 1, 즉 2 엔트리), MPSMIN > MPSMAX, CSS = 0 (아무 명령 집합도 없음). 실제로는 BAR 매핑이 엉뚱한 곳을 가리키거나(다른 BAR, 다른 장치) 덤프 오프셋이 밀렸을 때 이렇게 보인다. TO = 0 은 이상하지만 거부하지 않았다 (연습 5 에서 0 ms 로 계산됨을 확인만 한다).

전수 검사: CSTS 하위 6비트(64가지) × EN(2) = 128 조합에서 "CFS 면 항상 FATAL, 아니면 SHST, 아니면 2×2 표" 가 성립하는지 본다.

### Part C — 프로브 리포트 (실측)

`solution.c` 실행 출력 중 리포트 부분 (실제 출력):

```text
--- QEMU nvme, 드라이버 없음 (실측) ---
VS   1.4.0
CAP  0x004008200f0107ff MQES=2047 (큐당 최대 2048 엔트리) CQR=1 AMS=0 TO=15 (7500 ms) DSTRD=0 (도어벨 간격 4 B) NSSRS=0 CSS=0x41 NVM IOCS BPS=0 MPS 4096..65536 B CMBS=0 PMRS=0 CRMS=0
CC   0x00000000 EN=0 CSS=0 MPS=0 (4096 B) AMS=0 SHN=0 IOSQES=0 (1 B) IOCQES=0 (1 B)
CSTS 0x00000000 RDY=0 CFS=0 SHST=0 NSSRO=0 PP=0
AQA  0x00000000 ASQ 1 엔트리, ACQ 1 엔트리
ASQ  0x0000000000000000 ACQ 0x0000000000000000
DB   SQ0 tail 0x1000, CQ0 head 0x1004, SQ1 tail 0x1008, CQ1 head 0x100c
STATE DISABLED: EN=0 RDY=0 리셋 상태 — AQA/ASQ/ACQ/CC 설정 후 EN=1 (PCIE-B13)
--- QEMU nvme, nvme.ko probe 후 (실측) ---
VS   1.4.0
CAP  0x004008200f0107ff MQES=2047 (큐당 최대 2048 엔트리) CQR=1 AMS=0 TO=15 (7500 ms) DSTRD=0 (도어벨 간격 4 B) NSSRS=0 CSS=0x41 NVM IOCS BPS=0 MPS 4096..65536 B CMBS=0 PMRS=0 CRMS=0
CC   0x00460061 EN=1 CSS=6 MPS=0 (4096 B) AMS=0 SHN=0 IOSQES=6 (64 B) IOCQES=4 (16 B)
CSTS 0x00000001 RDY=1 CFS=0 SHST=0 NSSRO=0 PP=0
AQA  0x001f001f ASQ 32 엔트리, ACQ 32 엔트리
ASQ  0x0000000043f3c000 ACQ 0x0000000043f3b000
DB   SQ0 tail 0x1000, CQ0 head 0x1004, SQ1 tail 0x1008, CQ1 head 0x100c
STATE READY: EN=1 RDY=1 동작 중 — admin 명령 가능
--- 가상 소비자 SSD (동작 중) ---
VS   1.4.0
CAP  0x00000030140103ff MQES=1023 (큐당 최대 1024 엔트리) CQR=1 AMS=0 TO=20 (10000 ms) DSTRD=0 (도어벨 간격 4 B) NSSRS=1 CSS=0x01 NVM BPS=0 MPS 4096..4096 B CMBS=0 PMRS=0 CRMS=0
CC   0x00460001 EN=1 CSS=0 MPS=0 (4096 B) AMS=0 SHN=0 IOSQES=6 (64 B) IOCQES=4 (16 B)
CSTS 0x00000001 RDY=1 CFS=0 SHST=0 NSSRO=0 PP=0
AQA  0x001f001f ASQ 32 엔트리, ACQ 32 엔트리
ASQ  0x0000000102a40000 ACQ 0x0000000102a41000
DB   SQ0 tail 0x1000, CQ0 head 0x1004, SQ1 tail 0x1008, CQ1 head 0x100c
STATE READY: EN=1 RDY=1 동작 중 — admin 명령 가능
--- 가상: 장치가 버스에서 사라짐 ---
STATE GONE: 레지스터가 all-1s — 장치 응답 없음 (링크 다운/surprise removal/Mem decode off). 값을 해석하지 말 것
```

읽는 법:

- **드라이버 전**: CC=0, CSTS=0 → DISABLED. 이때 AQA=0 은 "1 엔트리" 로 디코드되지만 의미 없는 값이다 — 아직 아무도 안 썼다. 리포트가 숫자를 기계적으로 풀더라도, 판정(STATE)과 같이 읽어야 한다.
- **드라이버 후**: Linux 가 AQA 32/32, ASQ/ACQ 를 4 KiB 정렬 주소로 쓰고 CC 0x00460061 → RDY=1. dmesg: `nvme nvme0: 1/0/0 default/read/poll queues` (`capture_qemu_nvme_bar0.txt`). 도어벨 읽기값은 전부 0 이었다 — 도어벨 읽기 결과는 의미가 없다고 보는 게 안전하다 (스펙 문구는 확인 권장).
- **가상 소비자 SSD**: 특정 제품이 아니라 "흔히 보이는 모양" 으로 지은 값이다 — MQES 1023, TO 10 s 대, MPS 4 KiB 만 지원, CSS NVM 만. 실제 장치는 `nvme show-regs` (nvme-cli, PCIE-M14) 로 직접 보자.
- 고장 덤프 테스트: CSTS 0x3 → FATAL, EN=1·CSTS=0 → ENABLING ("CAP.TO 10000 ms 넘으면 타임아웃"), CC SHN=01 + CSTS 0x9 → SHUTDOWN_DONE, MQES=0 → BAD_CAP(check=2), 0x3C 바이트 → TRUNC, 24바이트 출력 버퍼 → 잘려도 NUL 종료.

검증 엔지니어 관점의 체크리스트로 바꾸면: (1) config space 에서 VID/class 01:08:02, Command.Mem=1 (B03), (2) BAR0 크기 ≥ `0x1000 + (N+1)·2·stride` (연습 3), (3) CAP 이 불가능 값이 아닌지, (4) CSTS all-1s 아닌지, (5) EN/RDY 전이가 CAP.TO 안에 끝나는지 (B13), (6) 링크 폭/속도는 별도로 LnkSta (B04/B06).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 80/80 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
```

출력의 리포트 부분은 위 5. 풀이 Part C 와 같고, 마지막 줄은 `80/80 passed`.

## 7. 자주 하는 실수

**1. 예약 필드를 빠뜨렸는데 컴파일러 패딩이 일부를 "고쳐" 준다.** 시작 코드는 0x18 예약을 빠뜨려 CSTS 0x18, NSSR 0x1C, AQA 0x20 으로 밀렸다. 그런데 다음 필드 ASQ 는 `uint64_t` 라 8 바이트 정렬을 위해 컴파일러가 0x24 에 4 B 패딩을 넣고 **0x28 에 맞게** 놓는다. 실측 (시작 코드 구조체의 offsetof):

```text
cc      0x014
csts    0x018      ← 틀림 (0x1C 이어야)
nssr    0x01c      ← 틀림
aqa     0x020      ← 틀림
asq     0x028      ← 우연히 맞음 (패딩)
acq     0x030
cmbsz   0x03c
crto    0x068
pmrcap  0x070      ← 예약 배열 크기 틀림
sizeof  0x270
```

"ASQ 는 맞으니까 구조체가 맞다" 는 착각을 막는 게 바로 **모든 필드의** `_Static_assert` 다. 고친 코드: `uint32_t rsvd18;` + `uint8_t rsvd6c[0xE00 - 0x6C];` + `#define NVME_LAYOUT_CHECKED 1`.

**2. MQES 를 엔트리 수로 읽는다.** MQES=1023 이면 1024 엔트리. 반대로 AQA 에 32 를 쓰면 33 엔트리 큐를 선언한 셈이 되어 컨트롤러가 호스트 메모리의 큐 끝 다음 엔트리를 읽는다. 고친 코드: `max_entries = mqes + 1`, AQA 에는 `depth - 1`. 그리고 원형 큐는 "꽉 참" 과 "빔" 을 구분하려고 한 칸을 비우므로 동시에 낼 수 있는 명령은 엔트리 수 − 1 (Linux `NVME_AQ_MQ_TAG_DEPTH` 주석, PCIE-M01).

**3. CSTS 의 CFS 를 all-1s 보다 먼저 본다.** 링크가 내려간 장치의 CSTS 는 0xFFFFFFFF → CFS=1 이다. "치명적 오류 → CC.EN=0 리셋" 을 시도해도 쓰기는 사라지고 리셋 대기만 타임아웃난다. 고친 코드: `if (csts == 0xFFFFFFFFu) return NVS_GONE;` 를 맨 앞에. 그다음 config space VID 를 읽어 0xFFFF 인지(PCIE-B08), LnkSta/AER 을 본다.

**4. DSTRD 를 바이트로 쓴다.** `0x1000 + 2*qid*dstrd` 처럼 쓰면 DSTRD=0 에서 모든 도어벨이 0x1000 이 된다. 간격은 `4 << DSTRD` 바이트(또는 u32 단위 `1 << DSTRD`). 고친 코드: `(2ull * qid) * (4ull << dstrd)`.

**5. 64-bit 를 `uint32_t` 로 계산한다.** 도어벨 오프셋은 qid 65535 · DSTRD 15 에서 약 1.7 × 10^10. `2 * qid * (4u << dstrd)` 는 32-bit 에서 넘친다. 고친 코드: `2ull *`. 같은 이유로 CAP 필드를 `(uint32_t)cap >> 37` 처럼 먼저 자르면 상위 필드가 전부 0 이 된다.

**6. `volatile` 구조체로 64-bit 필드를 바로 읽는다.** `r->cap` 은 64-bit 로드 한 번이 되고(지원 안 하는 경로에서 오류/값 깨짐), `r->cc |= EN` 같은 쓰기는 컴파일러가 접근 폭을 바꾸지 않더라도 읽기-수정-쓰기라 의도치 않은 비트를 다시 쓴다(BM-B04). 구조체는 **오프셋 문서 + `_Static_assert`** 용으로 쓰고, 실제 접근은 `readl/writel`, `lo_hi_readq/lo_hi_writeq` 로 한다. 빅엔디안 CPU 에서는 `readl` 이 LE→CPU 변환까지 해 준다.

**7. CC.MPS 와 PCIe MPS 를 섞는다.** CC.MPS 는 **메모리 페이지 크기**(PRP 엔트리 하나가 가리키는 단위, PCIE-M04), PCIe Device Control 의 MPS 는 **TLP 최대 payload** (128/256/512 B, PCIE-B04/B10). 같은 약어라 면접관이 일부러 섞어 묻는다.

## 8. 면접 Q&A

<details><summary>Q1. CAP.MQES = 1023 이면 큐 하나에 엔트리가 몇 개고, 명령은 동시에 몇 개까지 낼 수 있나요?</summary>

MQES 는 0-based 라 1024 엔트리. 원형 큐에서 head == tail 을 "빔" 으로 쓰기 때문에 꽉 찬 상태는 tail + 1 == head 로 정의되고, 그래서 동시에 넣을 수 있는 명령은 1023 개다. Linux 는 `q_depth = min(MQES + 1, io_queue_depth)` 로 잡고 (기본 io_queue_depth 1024), 태그 수는 depth − 1. 꼬리질문 "MQES 가 0 이면?" → 스펙상 무효(최소 2 엔트리). 이 레슨의 `nvme_cap_check` 는 BAD_CAP 으로 거른다. 참고로 MQES 는 I/O 큐 한도이고 admin 큐는 AQA 에서 최대 4096 (12비트, 스펙 확인 권장).

</details>

<details><summary>Q2. SQ 3 의 tail 도어벨과 CQ 3 의 head 도어벨 주소는? DSTRD 는 왜 있나요?</summary>

간격 s = 4 << DSTRD. SQ y = 0x1000 + 2y·s, CQ y = 0x1000 + (2y+1)·s. DSTRD=0 이면 SQ3 = 0x1018, CQ3 = 0x101C, DSTRD=2 면 0x1060, 0x1070 (테스트에 있음). SQ 는 호스트가 **tail** 을, CQ 는 호스트가 **head** 를 쓴다 — 생산자/소비자 방향이 반대이기 때문 (PCIE-M01). DSTRD 는 도어벨을 캐시 라인이나 페이지 단위로 떨어뜨리기 위한 것이다: 가상화에서 도어벨 쓰기 트랩을 큐별로 나누거나, 여러 코어가 다른 큐 도어벨을 칠 때 간섭을 줄인다 (설계 의도는 기억 기반). 반대 비용은 BAR 크기: 큐 쌍마다 2·s 바이트.

</details>

<details><summary>Q3. Linux 는 CAP 을 왜 readq 한 번이 아니라 lo_hi_readq 로 읽나요? 찢어진 읽기는 문제 안 되나요?</summary>

64-bit MMIO 읽기를 한 번에 못 하는 경로(32-bit CPU, 일부 루트 컴플렉스/브리지)가 있고, 컨트롤러가 64-bit 접근을 받아 주는지도 장담하기 어렵다. 32-bit 두 번이면 어디서나 된다. CAP 은 값이 고정이라 두 번 나눠 읽어도 찢어질 수 없다. ASQ/ACQ 는 EN=0 일 때만 쓰므로 쓰기 순서(하위 먼저)도 문제가 없다. 값이 바뀌는 64-bit 카운터라면 hi-lo-hi 로 읽고 hi 가 같을 때만 받아들인다 (이 레슨 B1, 재시도 횟수 제한 필수). 반대로 "하위를 읽으면 상위가 래치되는" 하드웨어도 있는데, 그때는 **반드시 하위 먼저** 라는 순서 자체가 규약이다 — 데이터시트 확인.

</details>

<details><summary>Q4. CSTS 를 읽었더니 0xFFFFFFFF 입니다. CFS 비트가 1 이니 치명적 오류인가요?</summary>

아니다. 0xFFFFFFFF 는 **아무도 응답하지 않을 때** PCIe 가 돌려주는 값이다: 링크 다운, surprise removal, 장치가 리셋 중, Command.Memory Space=0 이라 BAR 디코드 안 됨, 또는 UR/CA completion 을 루트 컴플렉스가 all-1s 로 바꿔 준 것. Linux 도 CFS 를 보기 전에 `csts == ~0` 를 먼저 확인한다. 다음으로 볼 것: config space VID (0xFFFF 면 장치 자체가 없음 — PCIE-B08), Command 레지스터의 Mem 비트 (B03), LnkSta 의 DLActive / 속도·폭 (B04/B06), AER 상태 (PCIE-M12). 진짜 CFS 는 CSTS 가 정상 값(예: 0x00000003)일 때만 믿는다.

</details>

<details><summary>Q5. CAP.TO 는 정확히 무엇의 한도인가요? Linux 는 실제로 얼마나 기다리죠?</summary>

CC.EN 을 0→1 또는 1→0 으로 바꾼 뒤 CSTS.RDY 가 따라 바뀔 때까지 호스트가 기다려야 하는 **최악 시간**, 500 ms 단위. QEMU 는 15 → 7.5 s. Linux 는 `nvme_wait_ready(..., (NVME_CAP_TIMEOUT(cap) + 1) / 2, ...)` 로 **초 단위로 올림** → 8 s, 그동안 1~2 ms 마다 CSTS 를 폴링하고 넘으면 "Device not ready; aborting reset, CSTS=0x…" 를 찍고 -ENODEV. NVMe 2.0 의 CAP.CRMS.CRWMS 가 켜져 있으면 CRTO.CRWMT(역시 500 ms 단위)를 쓰는데, CRWMT < CAP.TO 인 고장 장치를 위해 Linux 는 큰 쪽을 쓴다 (연습 5). 셧다운(SHN)은 별도 타임아웃(`shutdown_timeout`, 또는 Identify 의 RTD3E)을 쓴다 — PCIE-B13 추가 연습.

</details>

<details><summary>Q6. CC.EN 을 1에서 0으로 쓰면 컨트롤러에 무슨 일이 생기나요?</summary>

컨트롤러 리셋(Controller Reset): 모든 I/O SQ/CQ 가 삭제되고 진행 중이던 명령은 중단되며, 컨트롤러는 RDY 를 0 으로 내린다. admin 큐를 포함해 큐를 다시 쓰려면 AQA/ASQ/ACQ 설정 → EN=1 → RDY=1 → Create I/O CQ/SQ 명령부터 다시 해야 한다. AQA/ASQ/ACQ 레지스터 값 자체는 리셋으로 지워지지 않는다고 알고 있다 (스펙 확인 권장). PCIe 링크와 config space 는 영향이 없다 — 그건 FLR(Function Level Reset)이나 핫 리셋의 영역. 그리고 RDY 가 0 이 되기 전에 다른 레지스터를 건드리지 않는다 (상태 DISABLING). SSD 펌웨어 쪽에서 보면 EN 1→0 인터럽트를 받아 큐 컨텍스트를 버리고 진행 중 명령을 정리한 다음 RDY 를 내리는 핸들러다.

</details>

<details><summary>Q7. 레지스터 맵을 C 구조체로 정의하는 게 좋은가요, 오프셋 #define 이 좋은가요?</summary>

둘 다 쓴다. 구조체는 문서 역할과 `_Static_assert` 로 레이아웃을 고정하는 데 좋고(이 레슨), 디버거에서 덤프를 겹쳐 보기 편하다. 위험: (1) 컴파일러 패딩이 빠진 필드를 부분적으로 가린다(실수 1), (2) 64-bit 필드를 직접 읽으면 접근 폭이 바뀐다(실수 6), (3) `__attribute__((packed))` 를 붙이면 컴파일러가 바이트 단위 접근을 낼 수 있어 MMIO 에 치명적. 그래서 Linux 는 `NVME_REG_*` 오프셋 + `readl(bar + off)` 를 쓴다 (예전 커널에는 `struct nvme_bar` 가 있었다). 펌웨어(컨트롤러 쪽)에서는 레지스터 블록 구조체 + 정적 검사가 흔하다 (CMSIS 의 `XXX_TypeDef` 스타일).

</details>

<details><summary>Q8. CC.MPS 와 PCIe 의 MPS(Max Payload Size)는 같은 건가요?</summary>

이름만 같다. CC.MPS 는 NVMe 의 **메모리 페이지 크기** 2^(12+MPS) — PRP 엔트리가 가리키는 단위, CAP.MPSMIN..MPSMAX 안에서 고르며 Linux 는 4 KiB 고정. PCIe Device Control 의 Max_Payload_Size 는 TLP 하나에 실을 수 있는 데이터 최대 크기(128/256/512 B…), 경로 위 모든 장치의 최소값으로 맞춘다 (PCIE-B04, 대역폭 영향은 PCIE-B02). MRRS(Max Read Request Size)까지 같이 물어보면 "NVMe 읽기 성능은 PCIe MPS/MRRS 에, PRP 목록 모양은 CC.MPS 에 영향" 이라고 정리하면 된다.

</details>

## 9. 추가 연습

### 연습 1 — CMBLOC / CMBSZ 디코드

`nvme_cmb_decode(cmbloc, cmbsz, &o)`: BIR(2:0), OFST(31:12), SZ(31:12), SZU(11:8), 플래그(4:0: SQS, CQS, LISTS, RDS, WDS). 단위 = 4 KiB × 16^SZU (0..6 → 4K, 64K, 1M, 16M, 256M, 4G, 64G). SZU > 6 이나 BIR 1/6/7 은 거부. `nvme_cmb_fits(&o, bar_size)`: 오프셋 + 크기가 BIR 이 가리키는 BAR 안에 들어가는지. 가상 CMB `CMBLOC=0x00000002, CMBSZ=(64<<12)|(2<<8)|0x1F` 와 오프셋이 1 MiB 밀린 경우를 확인하라.

<details><summary>답</summary>

마스크는 nvme.h 의 `NVME_CMB_BIR/OFST`, `NVME_CMBSZ_SZ_SHIFT/MASK`, `NVME_CMBSZ_SZU_SHIFT/MASK` 그대로. 첫 예: BIR 2, 단위 1 MiB, 크기 64 MiB, 플래그 0x1F → 64 MiB BAR2 에 꼭 맞음, 32 MiB BAR 에는 안 들어감. `CMBLOC=0x1002` 면 오프셋 1 MiB → 64 MiB BAR 를 1 MiB 넘침 (128 MiB 면 OK). 덧셈 대신 `size <= bar - ofst` 로 비교해 오버플로를 피한다. QEMU 실측은 CMBSZ = 0 (CMB 없음, CAP.CMBS = 0). BIR 1 은 64-bit BAR0 의 위쪽 절반이라 무효로 처리했다 (BIR 허용값은 스펙 확인 권장). CMB 를 실제로 쓰려면 CMBMSC 로 켜야 한다 (NVMe 1.4+).

</details>

### 연습 2 — AQA / ASQ / ACQ 점검

`nvme_admin_regs_check(aqa, asq, acq, page_bytes)`: AQA 예약 비트(15:12, 31:28) → `ADM_RSVD`, ASQS 나 ACQS 가 0 (= 1 엔트리) → `ADM_SIZE`, ASQ/ACQ 가 4 KiB 와 CC.MPS 페이지에 정렬 안 됨 → `ADM_ALIGN`. QEMU 실측 값은 통과해야 한다.

<details><summary>답</summary>

QEMU (`AQA 0x001F001F, ASQ 0x43F3C000, ACQ 0x43F3B000`, 4 KiB) → OK. `0x001F101F` → RSVD, `0x001F0000` → SIZE (ACQ 1 엔트리), `0x0FFF0FFF` → OK (4096/4096 이 최대), ASQ `…C800` → ALIGN. 같은 주소라도 MPS = 16 KiB 면 0x43F3C000 은 16 KiB 정렬이 아니므로 ALIGN — CC.MPS 를 바꾸면 큐 메모리 정렬 요구도 바뀐다는 점이 포인트 (페이지 정렬 요구 문구는 스펙 확인 권장). 실제 설정 순서와 "EN=0 일 때만 쓴다" 는 PCIE-B13.

</details>

### 연습 3 — BAR 크기로 큐 쌍 수 구하기

`nvme_max_qpairs_in_bar(bar_size, dstrd)`: admin 포함, 도어벨 두 개(SQ tail, CQ head)가 전부 BAR 안에 들어가는 큐 쌍 수. QEMU BAR0 (16 KiB) 에서 DSTRD 0 과 2 의 답을 구하고, Part A 의 `nvme_bar_min_size` 의 역함수임을 랜덤 10만 건으로 확인하라.

<details><summary>답</summary>

`(bar − 0x1000) / (2 · (4 << DSTRD))`. QEMU 16 KiB: DSTRD 0 → 12288 / 8 = **1536** (qid 0..1535, admin + I/O 1535), DSTRD 2 → 12288 / 32 = 384. 0x1007 → 0, 0x1008 → 1. 역함수 성질: n 쌍이 들어가면 `min_size(n−1) ≤ bar < min_size(n)` — 랜덤 BAR 크기(0x1000..+16 MiB) × DSTRD 0..15 에서 성립. 실전: 드라이버는 큐를 많이 만들기 전에 BAR 를 다시 매핑해 도어벨 영역을 넓히기도 한다 (Linux `nvme_remap_bar(dev, db_bar_size(dev, nr_io_queues))`).

</details>

### 연습 4 — Linux 가 쓰는 CC 값 재현

`linux_cc_value(cap, host_page_shift, &cc)`: Linux `nvme_enable_ctrl()` 처럼 CSS = CAP.CSS bit6 ? 6 : 0, MPS = page_shift − 12 (MPSMIN..MPSMAX 밖이면 −1), AMS RR, SHN 없음, IOSQES 6, IOCQES 4, EN 은 아직 0. QEMU CAP 으로 계산한 값에 EN 을 더하면 실측 CC 와 같아야 한다.

<details><summary>답</summary>

QEMU CAP → `0x00460060`, `| EN` = `0x00460061` = **실측 CC 와 일치**. 가상 SSD (CSS NVM 만) → `0x00460000`. MPSMIN=1 (8 KiB) 인 CAP 에 4 KiB 호스트 → −1 (Linux 메시지 "Minimum device page size 8192 too large for host (4096)" 와 같은 상황). QEMU CAP 에 page_shift 16 (64 KiB) 을 주면 MPSMAX=4 와 같아 MPS=4 로 통과, 17 (128 KiB) 은 범위 밖이라 −1. Linux 는 CC 를 쓴 뒤 CAP 을 **다시 읽는다** ("CAP value may change after initial CC write") — CSS 선택에 따라 CAP 이 바뀔 수 있다는 스펙 조항 때문.

</details>

### 연습 5 — 준비 타임아웃 계산

`nvme_ready_timeout_ms(cap, crto, &crto_bad)`: 기본은 CAP.TO × 500 ms. CAP.CRMS.CRWMS(bit 59)가 켜져 있으면 CRTO.CRWMT(15:0, 500 ms 단위)를 쓰되, CRWMT < CAP.TO 면 이상값으로 표시하고 CAP.TO 를 쓴다. `linux_wait_s(to) = (to + 1) / 2` 도 확인하라.

<details><summary>답</summary>

QEMU (CRMS 없음) → 7500 ms, Linux 대기 8 s. TO=20 + CRWMS + CRWMT 60 → 30000 ms. CRWMT 4 < TO 20 → 10000 ms 와 `crto_bad` (Linux `dev_warn_once("bad crto:…")`). TO=0 이면 0 ms — 이런 장치는 첫 폴링에서 바로 타임아웃나므로 실전 드라이버는 하한을 두기도 한다. 시간 단위가 섞이는 곳(500 ms 단위, 초 올림, jiffies)이 버그가 잘 숨는 곳이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 32/32 passed
```

```text
32/32 passed
```

## 10. 핵심 정리

- BAR0 맵: 0x00 CAP(64) · 0x08 VS · 0x0C/0x10 INTMS/INTMC · 0x14 CC · **0x18 예약** · 0x1C CSTS · 0x20 NSSR · 0x24 AQA · 0x28 ASQ(64) · 0x30 ACQ(64) · 0x38/0x3C CMB · 0x68 CRTO · 0xE00 PMR · 0x1000 도어벨. 구조체 + 모든 필드 `_Static_assert` (패딩이 실수를 가린다).
- CAP: MQES 15:0 (0-based, +1), TO 31:24 (×500 ms), DSTRD 35:32 (간격 4<<n), NSSRS 36, CSS 44:37 (bit0 NVM, bit6 IOCS), MPSMIN/MAX 51:48/55:52 (2^(12+n)), CMBS 57, CRMS 60:59.
- 도어벨: SQ y tail = 0x1000 + 2y·s, CQ y head = 0x1000 + (2y+1)·s, s = 4 << DSTRD. 64-bit 로 계산.
- CC: EN 0, CSS 6:4, MPS 10:7, AMS 13:11, SHN 15:14, IOSQES 19:16 (6 = 64 B), IOCQES 23:20 (4 = 16 B). CSTS: RDY 0, CFS 1, SHST 3:2, NSSRO 4, PP 5.
- 64-bit 레지스터는 32-bit 두 번, 하위 먼저 (`lo_hi_readq`). 변하는 값이면 hi-lo-hi + 재시도 상한.
- 상태 판정 순서: all-1s(GONE) → CAP 불가능값 → CFS → SHST → (EN, RDY). 0xFFFFFFFF 는 오류 상태가 아니라 **응답 없음**.
- 실측 QEMU: CAP 0x004008200F0107FF (2048 엔트리, 7.5 s, 4K..64K), VS 1.4.0, Linux 가 쓴 CC 0x00460061, AQA 0x001F001F (32/32).

## 참고자료

- Linux `include/linux/nvme.h` (Ubuntu linux-headers 7.0.0-38, Lima VM) — `NVME_REG_*`, `NVME_CAP_*`, `NVME_CC_*`, `NVME_CSTS_*`, `NVME_CMB_*`, `NVME_CRTO_*`, `NVME_VS()`. 이 레슨의 오프셋·비트 기준.
- Linux 6.18.55 `drivers/nvme/host/pci.c` (`lo_hi_readq` 로 CAP 읽기, `db_stride`, `dbs = bar + 4096`, AQA 쓰기, `db_bar_size`), `drivers/nvme/host/core.c` (`nvme_enable_ctrl`, `nvme_disable_ctrl`, `nvme_wait_ready`, CRTO 처리), `drivers/nvme/target/core.c` (`nvmet_init_cap`). `include/linux/io-64-nonatomic-lo-hi.h`.
- NVM Express, *NVM Express Base Specification* Revision 2.0/2.1, 3.1 "Register Definition" (CAP/CC/CSTS/AQA/ASQ/ACQ/CMB/CRTO) — 헤더에 없는 비트(CQR, AMS, BPS, PMRS, CPS, NSSS, CSTS.ST, 0x58..0x64, PMRMSC, AQA 폭, 정렬·접근 폭 문구)는 여기서 확인: <https://nvmexpress.org/specifications/>
- QEMU `hw/nvme` 에뮬레이터 문서 (`-device nvme` 속성: `mqes`, `cmb_size_mb` 등): <https://www.qemu.org/docs/master/system/devices/nvme.html>
- 이 레슨의 캡처: `capture_qemu_nvme_bar0.txt` (QEMU 10.2.1 TCG, 게스트 Linux 6.18.55, BusyBox `devmem`), 스크립트 `capture_qemu_bar0.sh`, `capture_qemu_bar0_driver.sh` — 실리콘이 아니라 QEMU 가 흉내 낸 값.
- nvme-cli `nvme show-regs -H /dev/nvme0` — 실제 장치에서 같은 디코드를 보는 도구 (PCIE-M14).
- 관련 레슨: PCIE-B02 (PCIe MPS·대역폭), PCIE-B03 (Type 0 헤더, 클래스 01:08:02, Command.Mem), PCIE-B04 (PCIe Capability, LnkSta), PCIE-B05 (BAR sizing), PCIE-B06 (lspci/sysfs), PCIE-B08 (0xFFFF = 장치 없음), PCIE-B13 (초기화 시퀀스), PCIE-M01 (SQ/CQ 링·도어벨), PCIE-M04 (PRP 와 CC.MPS), PCIE-M12 (AER), PCIE-M14 (nvme-cli), PCIE-A03 (에러 복구·리셋), BM-B04 (W1C/RMW 함정), C-B01 (비트 연산), ARCH-M12 (MMIO 순서)
