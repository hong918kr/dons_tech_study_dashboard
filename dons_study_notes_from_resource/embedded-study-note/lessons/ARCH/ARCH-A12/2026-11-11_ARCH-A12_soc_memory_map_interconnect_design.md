---
id: ARCH-A12
time: 120분
updated: 2026-11-11
---
# ARCH-A12 SoC 설계 연습: 메모리 맵과 인터커넥트

## 학습 목표

- 엣지 AI SoC 요구사항(카메라 MIPI CSI-2 → ISP-lite, NPU, CPU 클러스터 2 개, LPDDR, 온칩 SRAM, 부트 ROM, secure SRAM/키, UART·SPI·I2C·GPIO·타이머, 디버그)을 받아 **설계 문서**를 쓴다: PA 폭과 그 근거, 주소 맵 표, AXI 메인 인터커넥트 + APB 브리지 계층, 일관성 도메인, 마스터→슬레이브 접근 행렬.
- 주소 맵을 **데이터**(영역 = {이름, base, size, 슬레이브 포트, 속성, 허용 마스터, 부모 창})로 두고, ARCH-B11 의 디코더를 2 단계(메인 인터커넥트 → APB 브리지)로 넓혀 `decode(addr, master, secure)` 가 OKAY / **DECERR**(인터커넥트 기본 슬레이브) / **SLVERR**(슬레이브·필터·브리지)과 이유를 내게 한다.
- 맵 검사기를 짜서 겹침·정렬·2의 거듭제곱·PA 범위·APB 창·속성·라우팅·보안·일관성·주소 폭 도달 범위를 자동으로 잡고, **오류를 하나씩 주입하면 정확히 그 진단 하나만** 나오는지로 검사기를 검증한다.
- "DRAM 을 8 GB 로, NPU 하나 더" 같은 변경 요청이 순진한 수정에서 무엇을 깨뜨리는지 검사기로 보이고, 고친 맵과 소프트웨어 영향(diff)을 설명한다.

## 1. 문제

"이 SoC 의 주소 맵을 그려 보세요", "NPU 가 secure 메모리를 못 읽게 하려면?", "NPU 를 coherent 로 붙일 건가요?" — 실리콘 검증·SoC 아키텍처·AI 가속기 팀 면접에서 시스템 설계 질문으로 자주 나오고, 실제 칩에서는 **주소 맵 하나가 RTL 디코더, 방화벽 설정, 부트 ROM, 디바이스 트리, 검증 테스트의 공통 원본**이 된다. 맵이 틀리면 테이프아웃 뒤에는 못 고친다.

ARCH-B11 은 MCU 하나의 고정된 맵을 **디코드**했다. 이 레슨은 거꾸로 맵을 **설계**하고, 설계가 규칙을 지키는지 **기계로 검사**한다. 요구사항 (숫자는 대표값/가정):

| 블록 | 요구 | 맵·버스에 주는 영향 |
|---|---|---|
| CPU | 클러스터 2 개: 2×big + 4×little (Cortex-A7x/A5x 급 가정), 리눅스 + secure 모니터/TEE | 40비트 PA 지원 코어, 하드웨어 일관성 필요 |
| 카메라 | MIPI CSI-2 수신 → ISP-lite (4K30, RAW10 → YUV420 + NPU 입력 640×640) | ISP 가 DRAM 에 프레임을 쓰는 버스 마스터 |
| NPU | int8 추론, 가중치·활성값을 DRAM 에서 스트리밍 | 대역폭이 가장 큰 마스터, secure 접근 금지 |
| DRAM | LPDDR4X x32, **4 GB** (rev2 에서 8 GB 요청 예정) | PA 공간의 대부분, 확장 여유 |
| 온칩 SRAM | 2 MB 공용(ISP 라인 버퍼·NPU 스크래치·저전력 코드) | NPU/ISP/CPU 공유 → 일관성 결정 |
| 보안 | 부트 ROM (secure), secure SRAM 256 KB, 키 저장소·OTP·TRNG, DRAM secure carve-out 64 MB | 영역별 secure 속성 + 필터, 키는 디버그로도 불가 |
| 주변장치 | UART×2, SPI×2, I2C×2, GPIO, 타이머×2, WDT, 클럭/리셋 제어 | APB 4 KB 슬롯 |
| 디버그 | JTAG/SWD → DAP (AXI-AP, APB-AP) | 잠금 해제 전에는 아무것도 못 봄 |

```svg
<svg viewBox="0 0 760 470" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a12a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">엣지 AI SoC v1 — 버스 계층 (실선 = coherent 경로, 점선 = non-coherent)</text>
  <g stroke="currentColor">
    <rect x="20" y="34" width="140" height="44" rx="5" class="f-acc"/>
    <rect x="170" y="34" width="140" height="44" rx="5" class="f-acc"/>
    <rect x="320" y="34" width="120" height="44" rx="5" class="f-mut"/>
    <rect x="450" y="34" width="110" height="44" rx="5" class="f-mut"/>
    <rect x="575" y="34" width="165" height="44" rx="5" class="f-warn"/>
    <rect x="20" y="104" width="540" height="40" rx="5" class="f-ok"/>
    <rect x="575" y="104" width="165" height="40" rx="5" class="f-warn"/>
    <rect x="20" y="176" width="720" height="40" rx="5" class="f-ok"/>
    <rect x="20" y="248" width="150" height="54" rx="5" class="f-mut"/>
    <rect x="180" y="248" width="100" height="54" rx="5" class="f-mut"/>
    <rect x="290" y="248" width="130" height="54" rx="5" class="f-mut"/>
    <rect x="430" y="248" width="140" height="54" rx="5" class="f-acc"/>
    <rect x="580" y="248" width="160" height="54" rx="5" class="f-acc"/>
    <rect x="430" y="334" width="140" height="100" rx="5" class="f-mut"/>
    <rect x="580" y="334" width="160" height="58" rx="5" class="f-mut"/>
    <rect x="580" y="398" width="160" height="36" rx="5" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="53" font-weight="bold">CPU big (2×)</text><text x="30" y="69">L1/L2, 40-bit PA</text>
    <text x="180" y="53" font-weight="bold">CPU little (4×)</text><text x="180" y="69">L1/L2, 40-bit PA</text>
    <text x="330" y="53" font-weight="bold">DEBUG</text><text x="330" y="69">DAP → AXI-AP</text>
    <text x="460" y="53" font-weight="bold">DMA</text><text x="460" y="69">시스템 DMA</text>
    <text x="585" y="53" font-weight="bold">카메라 → CSI-2 RX</text><text x="585" y="69">→ ISP-lite (마스터)</text>
    <text x="30" y="122" font-weight="bold">코히런트 인터커넥트 (CCI 계열 가정)</text>
    <text x="30" y="138">CPU 포트 = 완전 coherent(스누프), DEBUG·DMA 포트 = IO-coherent</text>
    <text x="585" y="122" font-weight="bold">NPU0</text><text x="585" y="138">non-coherent, 넓은 포트</text>
    <text x="30" y="194" font-weight="bold">메인 AXI 인터커넥트 (크로스바/NoC) — 주소 디코드 1단계</text>
    <text x="30" y="209">허용 안 된 마스터·빈 주소 → 기본 슬레이브 DECERR</text>
    <text x="28" y="266" font-weight="bold">TZC 류 필터</text><text x="28" y="281">→ DDR ctrl → LPDDR4X</text><text x="28" y="296">MAIN/SHARED/SEC</text>
    <text x="188" y="266" font-weight="bold">SYS_SRAM</text><text x="188" y="281">2 MB OCRAM</text>
    <text x="298" y="266" font-weight="bold">BOOTROM 64K</text><text x="298" y="281">SEC_SRAM 256K</text><text x="298" y="296">(secure 전용)</text>
    <text x="438" y="266" font-weight="bold">CFG (AXI4-Lite)</text><text x="438" y="281">IP 레지스터 버스</text>
    <text x="588" y="266" font-weight="bold">AXI→APB 브리지 ×2</text><text x="588" y="281">2단계 디코드</text><text x="588" y="296">구멍 → SLVERR</text>
    <text x="438" y="352">NPU0_CSR 64K</text><text x="438" y="368">ISP_CSR 64K</text><text x="438" y="384">DMA_CSR 64K</text>
    <text x="438" y="400">GIC 2M</text><text x="438" y="416">CORESIGHT 16M</text>
    <text x="588" y="352" font-weight="bold">APB_NS 1M</text><text x="588" y="368">UART×2 SPI×2 I2C×2</text><text x="588" y="384">GPIO TMR×2 WDT SYSCTRL</text>
    <text x="588" y="414" font-weight="bold">APB_SEC 1M</text><text x="588" y="429">KEY OTP TRNG TZC_CFG</text>
    <text x="20" y="456">포트 폭은 대표값 (메인 128비트, NPU 128–256비트). 논리 계층도 — 실제 토폴로지는 SOCX-B01.</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none" marker-end="url(#a12a)">
    <path d="M90 78 L90 102"/><path d="M240 78 L240 102"/><path d="M380 78 L380 102"/><path d="M505 78 L505 102"/>
    <path d="M290 144 L290 174"/>
    <path d="M95 216 L95 246"/><path d="M230 216 L230 246"/><path d="M355 216 L355 246"/><path d="M500 216 L500 246"/><path d="M660 216 L660 246"/>
    <path d="M500 302 L500 332"/><path d="M660 302 L660 332"/>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none" stroke-dasharray="5 3" marker-end="url(#a12a)">
    <path d="M640 144 L640 174"/><path d="M582 78 L567 96 L567 174"/>
  </g>
</svg>
```

이미 아는 것 (다시 가르치지 않는다): 주소 디코더·버스 폴트·DECERR/SLVERR 의 기본 (ARCH-B11), AHB/APB 브리지 타이밍 (ARCH-M13), AXI 채널·4 KB 경계 (ARCH-M14), MPU 영역의 2^n 정렬 규칙 (ARCH-M15), non-coherent DMA 의 clean/invalidate (ARCH-M12), 하드웨어 일관성 프로토콜 (ARCH-A04).

### Part A — 설계 문서와 2단계 디코더

- §5 Part A 의 **설계 문서**를 먼저 직접 써 보라: PA 폭 결정, 주소 맵 표, 버스 계층 그림, 일관성 도메인 표, 접근 행렬. 그다음 같은 내용을 `solution.c` 의 `V1_REGIONS[]` 표와 비교한다 (문서 = 데이터).
- `find_region(s, addr, level)` — 한 단계(최상위 또는 한 브리지 창 안)에서 addr 를 포함하는 영역. `decode(s, addr, master, secure, write)` — 7 단계 순서(디버그 잠금 → NS 고정 → 최상위 디코드 → 라우팅 → TZ → 브리지 자식 → RO)로 `{resp, why, region}`.
- 테스트: 경계 디코드, OKAY 7 경로, DECERR 7 가지, SLVERR 5 가지, 디버그 잠금 3 단계, 독립 참조 디코드와 9408 프로브 비교. 접근 행렬을 **C 데이터에서** 출력.

### Part B — 자동 검사기와 오류 주입

- `map_check(s, diag[], max)` — 영역마다 SIZE / POW2 / ALIGN (기하, 첫 실패 하나) · RANGE · ATTR · APB · ROUTE · SECURE (오류), NONCOH · REACH (경고), 그다음 같은 단계 쌍의 OVERLAP.
- 설계 맵 v1 → 진단 0 개. 오류 14 건(진단 코드 11 종 전부)을 하나씩 주입 → **정확히 그 진단 하나** (코드·영역·상대까지). PA 를 32비트로 줄이면 RANGE 3 개 (= PA 폭 결정의 근거).
- 무작위 맵 2000 개를 **페이지 비트맵 칠하기** 참조(구간 산술을 안 쓰는 독립 구현)와 비교: 겹침 쌍·첫 겹침 주소·디코드 결과.

### Part C — 설계한 맵 검사, 정렬 표, rev2 변경 요청

- `level_sorted`, `map_gaps` — C 표에서 정렬된 맵(브리지 창 바로 밑에 자식)을 출력하고 예약 구멍 11 개를 보고.
- rev2 변경 요청: **LPDDR 4 → 8 GB, NPU1 추가**(외부 IP, 32비트 AXI 주소, non-coherent, CPU·ISP 와 버퍼 공유). 순진한 수정 → 오류 3 + 경고 3. 고친 맵 → 0. 소프트웨어 영향은 추가 연습 1.

## 2. Problem (English)

> **Design an edge-AI SoC address map and interconnect, then build a checker for it.**
>
> Requirements: two CPU clusters (2 big + 4 little), a MIPI CSI-2 camera feeding an ISP that writes frames to memory, an NPU, 4 GB of LPDDR (8 GB in a later revision), 2 MB on-chip SRAM, boot ROM, secure SRAM and a key store, UART/SPI/I2C/GPIO/timers, and a debug port.
>
> 1. Write a design document: physical address width (justify), address map, AXI main interconnect + APB peripheral subsystem behind bridges, coherency domains (which masters snoop CPU caches, which rely on software cache maintenance), and a master→slave access matrix (the NPU must never reach secure memory; debug only when unlocked).
> 2. Encode the map as data: `{name, base, size, slave port, attributes (device/normal, secure, cacheable, read-only, power-of-two rule, bridge), allowed masters, parent bridge}`. Implement `decode(addr, master, secure, write)` returning OKAY, DECERR (no slave or no route — the interconnect's default slave answers) or SLVERR (the slave, its security filter, or the bridge rejects), with a reason.
> 3. Implement `map_check()` that reports: zero / non-4 KB sizes, non-power-of-two sizes and misaligned bases where the rule applies, regions outside the PA width, device regions marked cacheable, peripherals outside their APB window, masters allowed on a peripheral but not routed to its bridge, non-secure-only masters allowed on secure regions, overlaps within the same decode level; warnings for non-coherent masters on cacheable regions without a software-maintenance contract and for masters whose address width cannot reach a region. Inject each error and expect exactly that diagnosis.
> 4. Print the final map sorted from the data, list reserved gaps, and apply a revision-2 change request (DRAM 8 GB, second NPU with a 32-bit address bus). Show what breaks and fix it.
>
> Follow-ups: why 40 bits and not 36 or 32? Why is the NPU non-coherent? Who returns DECERR vs SLVERR and why does the distinction help bring-up? How many independent mechanisms keep the NPU out of secure memory?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/ARCH/ARCH-A12
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있다
mkdir -p /tmp/esn_ARCH-A12
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/esn_ARCH-A12/st && /tmp/esn_ARCH-A12/st; echo "exit $?"
```

실제 출력 (일부):

```text
== Part A: decode (v1, 40-bit PA) ==
FAIL starter.c:383  find_region(&v1, 0x0000000000ull, -1) == IX(v1, "BOOTROM")
FAIL starter.c:384  find_region(&v1, 0x000000FFFFull, -1) == IX(v1, "BOOTROM")
...
  decode vs reference: 9408 probes, 3552 mismatches
FAIL starter.c:465  dmis == 0
== Part B: map_check fault injection ==
  expect_one(OVERLAP, SPI0) got 0 diag(s):
FAIL starter.c:472  expect_one(&s, D_OVERLAP, "SPI0", IX(s, "SPI1"))
...
FAIL starter.c:577  ng == 11 && g[8].lo == 0x0280000000ull
14/81 passed
exit 1
```

TODO 는 5 개: `find_region`, `decode` (Part A), `map_check` (Part B), `level_sorted`, `map_gaps` (Part C). 영역 표 `V1_REGIONS[]`, rev2 생성 함수, 출력·참조 모델·하네스는 주어진다. 지나가는 14 개는 "v1 은 진단 0 개", "미매핑은 DECERR" 처럼 스텁이 우연히 맞히는 것들이다 — 그래서 오류 주입 테스트가 필요하다 (검사기가 아무것도 안 해도 깨끗한 맵은 통과한다).

## 4. 힌트

<details><summary>힌트 1 — 포함 검사는 64비트 뺄셈 한 번</summary>

40비트 주소라 `uint64_t` 를 쓴다. `addr - base < size` 는 addr 가 base 보다 작으면 감겨서 아주 큰 수 → 자동으로 거짓 (ARCH-B11 의 32비트 요령을 64비트로). 단계(level) 비교는 `parent_of(s, i) == level` — 자식은 최상위 탐색에 끼지 않는다.
</details>

<details><summary>힌트 2 — DECERR 와 SLVERR 의 경계는 "슬레이브에 도착했나"</summary>

인터커넥트가 그 마스터의 요청을 보낼 슬레이브를 못 고르면(빈 주소, 그 마스터에게 연결되지 않은 슬레이브) 인터커넥트 안의 기본 슬레이브가 DECERR 를 낸다. 슬레이브 포트까지 갔는데 거기 있는 것(TZ 필터, APB 브리지, ROM)이 거부하면 SLVERR. 그래서 "라우팅" 검사는 DECERR, "secure 영역에 NS" 는 SLVERR. 디버그 잠금은 DAP 쪽에서 막으니 DECERR 로 모델링했다 (모델 선택 — §5 참고).
</details>

<details><summary>힌트 3 — 기하 검사는 영역당 첫 실패 하나만</summary>

size 가 4 KB 배수가 아니면 "2^k 인가", "크기 정렬인가" 는 의미가 없다. SIZE → POW2 → ALIGN 을 `else if` 로 묶어야 오류 하나에 진단 하나가 된다. 반면 RANGE·ATTR·보안·라우팅은 서로 독립인 사실이라 따로 낸다.
</details>

<details><summary>힌트 4 — 겹침은 같은 단계끼리만, 모든 쌍</summary>

UART0 은 APB_NS 창 안에 있다 — 이건 겹침이 아니라 **포함**이다. 같은 부모(최상위끼리, 같은 창의 자식끼리)만 비교한다. 표가 정렬돼 있다고 가정하지 말고 모든 쌍 i&lt;j 를 본다 (n ≤ 40 이라 O(n²) 로 충분). 첫 겹침 바이트 = `max(base_i, base_j)`. 부모를 못 찾는 자식(parent_of = -2)은 APB 오류로 따로 보고하고 겹침 비교에서 뺀다.
</details>

<details><summary>힌트 5 — 구멍은 커서 하나로</summary>

정렬된 영역을 앞에서부터 보며 `cur` (지금까지 덮인 끝)를 들고 간다. `base > cur` 이면 [cur, base) 가 구멍. 마지막에 `cur < 한계` 면 끝 구멍. 한계는 최상위면 2^pa, 브리지 창이면 창의 끝.
</details>

## 5. 풀이

### Part A — 설계 문서 (v1)

#### A.1 PA 폭: 40비트

| 후보 | 공간 | 판정 |
|---|---|---|
| 32비트 | 4 GB | DRAM 4 GB 만으로 꽉 참 + MMIO 자리 없음. 검사기: `v1 with 32-bit PA: 3 diagnostics, 3 RANGE` (DRAM 세 영역 전부) |
| 36비트 | 64 GB | rev2(8 GB)까지는 충분. 다만 이 세대 Cortex-A 급 코어는 보통 40비트 PA 를 구현한다 (Cortex-A55/A76 TRM 기준 — 코어마다 확인 필요) — 인터커넥트만 36 으로 자를 이유가 약하다 |
| **40비트** | 1 TB | 코어 PA 폭과 일치, DRAM 성장 여유가 크다. 비용 = 인터커넥트 주소 경로·디코더 비교기 몇 비트 (면적 미미). AArch64 는 TCR_ELx 의 PA 크기 필드로 40비트를 설정 |

32비트 마스터(외부 IP, 오래된 DMA)는 4 GB 위를 못 본다 → **"32비트 마스터와 공유할 버퍼는 4 GB 아래"** 가 이 맵의 숨은 제약이다 (리눅스 `ZONE_DMA32` 와 같은 생각). v1 은 이걸 놓쳤고 rev2 에서 대가를 치른다 (Part C).

#### A.2 주소 맵 원칙

1. **하위 2 GB = 크기가 고정된 온칩 것들** (ROM, SRAM, MMIO), **DRAM 은 0x00_8000_0000 부터 위로**. DRAM 이 커져도 MMIO 를 건드리지 않는다. Arm 의 메모리 맵 백서(DEN0001)가 권하는 것도 비슷한 모양이다 (세부 숫자는 백서 참고).
2. **POW2 규칙** (`A_POW2`): 크기 2^k, base 는 크기의 배수. 하드웨어 디코더가 상위 `40 − k` 비트 상수 비교 하나로 끝나고 (연습 2: BOOTROM 24 비트, DRAM_SEC 14 비트), MPU/방화벽 영역 하나로 덮인다 (ARCH-M15). DRAM_MAIN/SHARED 는 크기가 2^k 가 아니라 범위 비교(비교기 2 개)를 쓴다 — 큰 DRAM 창에서는 흔한 선택.
3. **모든 영역 4 KB 배수·4 KB 정렬**, 주변장치는 **4 KB 슬롯 하나씩**: MMU 페이지 하나 = 장치 하나라서 드라이버마다 따로 매핑·보호(가상화 시 stage-2 로 장치 하나만 넘기기)가 된다. 큰 IP 의 CSR 은 64 KB 슬롯 — 64 KB 페이지 커널에서도 IP 하나가 페이지 하나.
4. **클래스 사이에 구멍을 남긴다**: SEC_SRAM 뒤 64 MB, SYS_SRAM 뒤 128 MB, CSR 블록 뒤 128 MB … 다음 리비전이 들어올 자리.
5. **secure 와 non-secure 를 다른 창에**: APB 브리지를 NS / S 둘로 나눠 창 단위로 필터링 (APB4 는 PPROT 로 보안 비트를 실어 나를 수 있어 한 브리지로도 되지만, 창을 나누면 필터가 단순해진다 — 설계 선택).

#### A.3 주소 맵 표 (C 표에서 출력, 실제 출력)

`attr`: NRM/DEV, S = secure 전용, C = cacheable, C\* = cacheable + 소프트웨어 유지보수 계약(SWCMO), RO, BR = 브리지 창. 마스터 열: B = CPU_BIG, L = CPU_LIT, D = DMA, I = ISP, N = NPU0, G = DEBUG, 1 = NPU1(rev2).

```text
  base            last             size  name           port    attr         B L D I N G 1
  0x00_0000_0000  0x00_0000_ffff    64K  BOOTROM        ROM     NRM S -  RO  x x . . . x .
  0x00_0400_0000  0x00_0403_ffff   256K  SEC_SRAM       SRAM_S  NRM S C      x x . . . x .
  0x00_0800_0000  0x00_081f_ffff     2M  SYS_SRAM       OCRAM   NRM - C*     x x x x x x .
  0x00_1000_0000  0x00_100f_ffff     1M  APB_NS         APB0    DEV - -  BR  x x x . . x .
  0x00_1000_0000  0x00_1000_0fff     4K    UART0        APB0    DEV - -      x x x . . x .
  0x00_1000_1000  0x00_1000_1fff     4K    UART1        APB0    DEV - -      x x x . . x .
  0x00_1000_2000  0x00_1000_2fff     4K    SPI0         APB0    DEV - -      x x x . . x .
  0x00_1000_3000  0x00_1000_3fff     4K    SPI1         APB0    DEV - -      x x x . . x .
  0x00_1000_4000  0x00_1000_4fff     4K    I2C0         APB0    DEV - -      x x x . . x .
  0x00_1000_5000  0x00_1000_5fff     4K    I2C1         APB0    DEV - -      x x x . . x .
  0x00_1000_6000  0x00_1000_6fff     4K    GPIO         APB0    DEV - -      x x . . . x .
  0x00_1000_7000  0x00_1000_7fff     4K    TIMER0       APB0    DEV - -      x x . . . x .
  0x00_1000_8000  0x00_1000_8fff     4K    TIMER1       APB0    DEV - -      x x . . . x .
  0x00_1000_9000  0x00_1000_9fff     4K    WDT          APB0    DEV - -      x x . . . x .
  0x00_1000_a000  0x00_1000_afff     4K    SYSCTRL      APB0    DEV - -      x x . . . x .
  0x00_1100_0000  0x00_110f_ffff     1M  APB_SEC        APB1    DEV S -  BR  x x . . . x .
  0x00_1100_0000  0x00_1100_0fff     4K    KEYSTORE     APB1    DEV S -      x x . . . . .
  0x00_1100_1000  0x00_1100_1fff     4K    OTP          APB1    DEV S -      x x . . . x .
  0x00_1100_2000  0x00_1100_2fff     4K    TRNG         APB1    DEV S -      x x . . . . .
  0x00_1100_3000  0x00_1100_3fff     4K    TZC_CFG      APB1    DEV S -      x x . . . . .
  0x00_2000_0000  0x00_2000_ffff    64K  NPU0_CSR       CFG     DEV - -      x x . . . x .
  0x00_2001_0000  0x00_2001_ffff    64K  ISP_CSR        CFG     DEV - -      x x . . . x .
  0x00_2002_0000  0x00_2002_ffff    64K  DMA_CSR        CFG     DEV - -      x x . . . x .
  0x00_2800_0000  0x00_281f_ffff     2M  GIC            GIC     DEV - -      x x . . . x .
  0x00_3000_0000  0x00_30ff_ffff    16M  CORESIGHT      DBGAPB  DEV - -      x x . . . x .
  0x00_8000_0000  0x01_3fff_ffff     3G  DRAM_MAIN      DDR     NRM - C      x x x . . x .
  0x01_4000_0000  0x01_7bff_ffff   960M  DRAM_SHARED    DDR     NRM - C*     x x x x x x .
  0x01_7c00_0000  0x01_7fff_ffff    64M  DRAM_SEC       DDR     NRM S C      x x . . . x .
```

설계 메모:
- **DRAM 은 한 컨트롤러, 영역 셋.** 인터커넥트 디코더 입장에선 0x00_8000_0000–0x01_7FFF_FFFF 가 DDR 포트 하나다. 셋으로 나눈 것은 그 앞 **TZC 류 영역 필터**(DRAM 앞에서 영역별 secure/마스터 권한을 거는 블록 — Arm CoreLink TZC-400 같은 것, 세부는 TRM 참고)의 설정이다. 이 레슨의 표는 "디코더 맵 + 방화벽 설정" 을 한 표로 합쳤다.
- **DRAM_SEC 은 DRAM 맨 위 64 MB, POW2.** TF-A BL31 / TEE 가 사는 carve-out 같은 자리 (실제 위치는 플랫폼마다). 맨 위에 둔 대가: DRAM 이 커지면 **이 주소가 움직인다** (rev2 diff 에 나온다). 맨 아래에 두면 고정되지만 4 GB 아래 귀한 자리를 먹는다 — 트레이드오프.
- **GIC 2 MB**: GICv3 의 distributor + 코어당 redistributor 를 담을 크기로 잡은 대표값 (실제 프레임 크기는 GIC 아키텍처·구현 문서로 확인).
- **CORESIGHT 16 MB**: 디버그 APB (ROM table + 코어별 디버그/ETM/CTI). 크기는 대표값.

#### A.4 일관성 도메인

| 도메인 | 마스터 | 방법 | 이유 |
|---|---|---|---|
| 완전 coherent | CPU big, little | 코히런트 인터커넥트가 스누프 (ACE/CHI 계열, 프로토콜은 ARCH-A04 의 MOESI 류) | 락·공유 자료구조 — 소프트웨어 유지보수 불가능 |
| IO-coherent | DMA, DEBUG | ACE-Lite 같은 포트: 이 마스터의 읽기는 CPU 캐시를 스누프하고 쓰기는 CPU 캐시의 해당 라인을 무효화. 마스터 자신은 캐시가 없어 스누프 당하지 않는다 | 디스크립터·작은 버퍼, 드라이버가 CMO 를 잊을 위험 제거. 대역폭이 작아 스누프 비용 감당 가능 |
| non-coherent | ISP, NPU0 (NPU1) | **소프트웨어 CMO**: CPU→NPU 넘길 때 clean, NPU→CPU 받을 때 invalidate (ARCH-M12 의 순서) — 또는 그 버퍼를 CPU 에서 non-cacheable 로 매핑 | 수 GB/s 스트리밍을 전부 스누프하면 CPU 클러스터의 스누프 대역폭·지연을 잡아먹는다. 데이터(프레임, 가중치, 활성값)는 CPU 가 거의 안 만지고, 손을 바꾸는 시점은 프레임/추론당 한 번이라 CMO 가 싸다 |

이 결정을 **맵에 박는 장치**가 `A_SWCMO` 와 `W_NONCOH` 경고다: non-coherent 마스터는 C\*(SWCMO) 영역(SYS_SRAM, DRAM_SHARED)에만 허용하고, 평범한 cacheable 영역(DRAM_MAIN, SEC_SRAM)에 붙이면 검사기가 경고한다. "드라이버가 CMO 를 할 것" 이라는 약속이 있는 곳에서만 non-coherent 접근을 허용한다는 뜻이다.

#### A.5 접근 행렬 (decode 결과, 실제 출력)

각 영역의 base 를 읽었을 때. B-NS = big 클러스터 normal world, B-S = secure world, G-S = 디버그 완전 해제 + secure, G-lk = 디버그 잠김.

```text
  region       B-NS  B-S L-NS  DMA  ISP NPU0  G-S G-lk NPU1
  BOOTROM        SE   ok   SE   DE   DE   DE   ok   DE   DE
  SEC_SRAM       SE   ok   SE   DE   DE   DE   ok   DE   DE
  SYS_SRAM       ok   ok   ok   ok   ok   ok   ok   DE   DE
  UART0          ok   ok   ok   ok   DE   DE   ok   DE   DE
  GPIO           ok   ok   ok   SE   DE   DE   ok   DE   DE
  KEYSTORE       SE   ok   SE   DE   DE   DE   SE   DE   DE
  OTP            SE   ok   SE   DE   DE   DE   ok   DE   DE
  TZC_CFG        SE   ok   SE   DE   DE   DE   SE   DE   DE
  NPU0_CSR       ok   ok   ok   DE   DE   DE   ok   DE   DE
  DRAM_MAIN      ok   ok   ok   ok   DE   DE   ok   DE   DE
  DRAM_SHARED    ok   ok   ok   ok   ok   ok   ok   DE   DE
  DRAM_SEC       SE   ok   SE   DE   DE   DE   ok   DE   DE
```

(전체 26 행 중 대표 행. UART1–I2C1 은 UART0 과, TIMER·WDT·SYSCTRL 은 GPIO 와, TRNG 는 TZC_CFG 와, ISP/DMA_CSR·GIC·CORESIGHT 는 NPU0_CSR 과 같은 행이다.)

읽는 법:
- **NPU0/ISP 는 SYS_SRAM 과 DRAM_SHARED 두 곳만** — 최소 권한. NPU 펌웨어 버그나 악성 모델이 커널 메모리(DRAM_MAIN)를 덮을 수 없다. 더 세밀한 버퍼 단위 격리는 SMMU 몫 (SOCX-M12, 다음 단계).
- **DMA 는 직렬 FIFO(UART/SPI/I2C)까지만**, GPIO·타이머·WDT·SYSCTRL 은 SE(브리지 필터). DMA 디스크립터가 오염돼도 클럭/리셋 레지스터를 못 건드린다.
- **KEYSTORE·TRNG·TZC_CFG 는 디버그 완전 해제여도 SE.** 키는 어떤 디버그 상태로도 안 나간다. TZC_CFG(방화벽 설정 자체)를 secure CPU 만 쓸 수 있어야 이 행렬 전체가 의미가 있다.
- secure CPU(B-S)는 NS 영역도 읽는다 (일반적인 TrustZone 시스템 동작으로 모델링. Armv8-A 는 Secure/Non-secure 물리 주소 공간을 구분하므로 실제로 같은 메모리가 보이는지는 시스템 설계에 달렸다 — 모델 단순화).

#### A.6 디코드 순서

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a12b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">decode(addr, master, secure, write) — 위에서 처음 걸리는 곳이 응답을 정한다</text>
  <g stroke="currentColor">
    <rect x="10" y="34" width="96" height="50" rx="4" class="f-mut"/>
    <rect x="116" y="34" width="96" height="50" rx="4" class="f-mut"/>
    <rect x="222" y="34" width="96" height="50" rx="4" class="f-mut"/>
    <rect x="328" y="34" width="96" height="50" rx="4" class="f-mut"/>
    <rect x="434" y="34" width="96" height="50" rx="4" class="f-mut"/>
    <rect x="540" y="34" width="110" height="50" rx="4" class="f-mut"/>
    <rect x="660" y="34" width="90" height="50" rx="4" class="f-mut"/>
    <rect x="10" y="130" width="96" height="50" rx="4" class="f-warn"/>
    <rect x="222" y="130" width="96" height="50" rx="4" class="f-warn"/>
    <rect x="328" y="130" width="96" height="50" rx="4" class="f-warn"/>
    <rect x="434" y="130" width="96" height="50" rx="4" class="f-acc"/>
    <rect x="540" y="130" width="110" height="50" rx="4" class="f-acc"/>
    <rect x="660" y="130" width="90" height="50" rx="4" class="f-acc"/>
    <rect x="660" y="200" width="90" height="36" rx="4" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="54">① 디버그</text><text x="18" y="72">잠김?</text>
    <text x="124" y="54">② NS 전용</text><text x="124" y="72">→ secure=0</text>
    <text x="230" y="54">③ 최상위</text><text x="230" y="72">디코드 / 2^pa</text>
    <text x="336" y="54">④ 마스터</text><text x="336" y="72">라우팅?</text>
    <text x="442" y="54">⑤ secure</text><text x="442" y="72">영역에 NS?</text>
    <text x="548" y="54">⑥ 브리지 창</text><text x="548" y="72">자식·필터·TZ</text>
    <text x="668" y="54">⑦ RO 에</text><text x="668" y="72">쓰기?</text>
    <text x="18" y="150">DECERR</text><text x="18" y="168">DBGLOCK</text>
    <text x="230" y="150">DECERR</text><text x="230" y="168">UNMAPPED</text>
    <text x="336" y="150">DECERR</text><text x="336" y="168">NOROUTE</text>
    <text x="442" y="150">SLVERR</text><text x="442" y="168">TZ</text>
    <text x="548" y="150">SLVERR</text><text x="548" y="168">HOLE/FILTER/TZ</text>
    <text x="668" y="150">SLVERR</text><text x="668" y="168">RO</text>
    <text x="680" y="223">OKAY</text>
    <text x="10" y="212">DECERR = 인터커넥트가 슬레이브를 못 고름 (기본 슬레이브)</text>
    <text x="10" y="230">SLVERR = 슬레이브 포트까지 갔고 거기서 거부 (필터 · 브리지 · ROM)</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none" marker-end="url(#a12b)">
    <path d="M106 59 L114 59"/><path d="M212 59 L220 59"/><path d="M318 59 L326 59"/><path d="M424 59 L432 59"/><path d="M530 59 L538 59"/><path d="M650 59 L658 59"/>
    <path d="M58 84 L58 128"/><path d="M270 84 L270 128"/><path d="M376 84 L376 128"/><path d="M482 84 L482 128"/><path d="M595 84 L595 128"/><path d="M705 84 L705 128"/>
    <path d="M745 84 L745 198"/>
  </g>
</svg>
```

```c
if (!(r->masters & BIT(m))) { d.why = WHY_NOROUTE; return d; }   /* ④ 경로 없음 → DECERR (d.resp 초기값) */
d.resp = RESP_SLVERR;                                             /* 여기부터는 슬레이브 쪽 거부 */
if ((r->attr & A_SEC) && !secure) { d.why = WHY_TZ; return d; }
```

왜 이 순서인가:
- **①② 가 맨 앞**: 디버그 잠금은 요청이 버스에 나가기 전(DAP)의 일이고, NS 고정은 마스터 포트의 배선(AxPROT[1])이다 — 주소를 보기 전에 정해진다.
- **④ 라우팅이 ⑤ 보안보다 앞**: 연결이 없으면 요청이 필터에 도착조차 못 한다. 실제 인터커넥트도 마스터별 디코더가 "이 마스터에게 보이는 슬레이브" 만 안다 (sparse connectivity).
- **DECERR vs SLVERR 는 디버그 단서**: DECERR 면 "맵/라우팅 문제 — 주소나 마스터를 의심", SLVERR 면 "슬레이브 쪽 정책 — 보안 상태, 브리지 구멍, ROM 쓰기를 의심". 단, **TZ 필터가 무엇을 돌려줄지는 구현·설정에 따라 다르다** (오류 응답 대신 읽기 0 / 쓰기 무시로 조용히 막도록 설정할 수 있는 필터도 있다). 이 모델은 SLVERR 로 고정했다.
- **⑥ APB 구멍 = SLVERR**: APB_NS 창은 메인 인터커넥트에겐 슬레이브 하나(브리지)다. 창 안의 빈 슬롯은 브리지가 판단한다 — 이 모델은 SLVERR, 실제 브리지는 OKAY + RAZ/WI 로 구현되기도 한다 (APB 의 PSLVERR 는 APB3 부터 있는 신호이고 브리지가 그것을 AXI 응답으로 올린다).

참조 디코드(포함 체인을 64비트 비교로 따로 모으는 구현)와 모든 영역 경계 ±1 × 마스터 7 × secure 2 × R/W 2 × 디버그 상태 3 을 비교: `decode vs reference: 9408 probes, 0 mismatches`.

### Part B — 검사기 규칙과 오류 주입

| 코드 | 종류 | 규칙 | 왜 필요한가 |
|---|---|---|---|
| SIZE | 오류 | size = 0 또는 4 KB 배수 아님 | 페이지 단위 매핑·필터의 최소 단위 |
| POW2 | 오류 | `A_POW2` 인데 크기가 2^k 아님 | 상위 비트 비교 디코더·방화벽 영역 하나 |
| ALIGN | 오류 | base 가 4 KB 미정렬, 또는 `A_POW2` 인데 크기 배수 아님 | 〃 (`0x0810_0000` 은 4 KB 정렬이지만 2 MB 정렬 아님) |
| RANGE | 오류 | [base, base+size) ⊄ [0, 2^pa) | PA 폭 밖은 디코더가 볼 수 없다 |
| ATTR | 오류 | device 인데 cacheable | device 메모리는 캐시·추측 접근 금지 |
| APB | 오류 | 부모 창이 없음 / 브리지 아님 / 창 밖 | 2단계 디코드에서 영원히 안 보이는 주변장치 |
| ROUTE | 오류 | 자식이 허용한 마스터를 부모 창이 라우팅 안 함 | 행렬이 "된다" 고 해도 실제로는 DECERR |
| SECURE | 오류 | secure 영역에 NS 전용 마스터 허용 | 행렬이 보안 정책과 모순 ("NPU 금지") |
| OVERLAP | 오류 | 같은 단계 두 영역이 바이트 하나라도 겹침 | 디코더는 하나만 고른다 — 다른 하나는 죽은 영역 |
| NONCOH | 경고 | non-coherent 마스터 + cacheable(SWCMO 아님) | stale 데이터 (ARCH-M12) |
| REACH | 경고 | PA 안 영역인데 끝 &gt; 2^마스터 주소 폭 | 32비트 마스터가 못 닿는 버퍼 |

**오류 주입 — 각 줄이 따로 만든 맵 하나의 유일한 진단** (실제 출력):

```text
  ERROR OVERLAP  SPI0         @0x00_1000_2000  with SPI1          ← SPI1 을 SPI0 슬롯에
  ERROR OVERLAP  DRAM_SHARED  @0x01_7c00_0000  with DRAM_SEC      ← SHARED +64 MB
  ERROR ALIGN    SYS_SRAM     @0x00_0810_0000                     ← 2 MB 영역을 1 MB 경계에
  ERROR ALIGN    SYSCTRL      @0x00_1000_a800                     ← 4 KB 미정렬 주변장치
  ERROR POW2     GIC          @0x00_2800_0000                     ← 3 MB
  ERROR SIZE     UART0        @0x00_1000_0000                     ← 2 KB
  ERROR RANGE    DRAM_SEC     @0x100_0000_0000                    ← 2^40 에서 시작
  ERROR APB      GPIO         @0x00_1010_0000  with APB_NS        ← 창 밖
  ERROR APB      TIMER0       @0x00_1000_7000  with (no such window)
  ERROR ATTR     NPU0_CSR     @0x00_2000_0000                     ← device + cacheable
  ERROR ROUTE    UART0        @0x00_1000_0000  master NPU0
  ERROR SECURE   BOOTROM      @0x00_0000_0000  master NPU0
  WARN  NONCOH   DRAM_MAIN    @0x00_8000_0000  master ISP
  WARN  REACH    DRAM_SHARED  @0x01_4000_0000  master NPU1
```

(화살표 뒤 설명은 붙인 것. 출력 자체는 진단 줄만.) 경계 테스트: DRAM_SEC 을 0xFF_FC00_0000 에 두면 끝이 **정확히 2^40** → 진단 0 (64비트 산술 덕분, `base + size` 를 32비트로 했다면 ARCH-B11 의 감김 버그가 40비트에서도 그대로 난다).

**실수 하나가 진단 두 개인 경우**도 있다 — 그게 맞다:

```text
  ERROR SECURE   SEC_SRAM     @0x00_0400_0000  master NPU0
  WARN  NONCOH   SEC_SRAM     @0x00_0400_0000  master NPU0
```

NPU0 을 SEC_SRAM 에 허용하면 보안 정책 위반이면서 동시에 non-coherent 마스터가 일반 cacheable 영역에 붙은 것이다. 서로 다른 두 사실이니 둘 다 보고한다. 그래서 "정확히 하나" 테스트에는 BOOTROM(캐시 안 함)을 썼다.

**심층 방어 확인**: 행렬을 틀리게(BOOTROM 에 NPU0 허용) 만들어도 NPU0 이 `secure=true` 를 내면 NS 로 고정되어 `SLVERR TZ` 로 막힌다 (테스트에 있음). 라우팅·AxPROT 배선·TZ 필터가 **각자** 막는다 — Q4.

**검사기를 검사하기 — 무작위 맵 vs 페이지 칠하기**: PA 24비트(4096 페이지)에 4–12 개 영역을 무작위로 놓고, 영역마다 페이지 비트맵을 칠한다. 겹침 쌍 = 비트맵 AND ≠ 0, 첫 겹침 주소 = AND 의 첫 비트, 디코드 주인 = 그 페이지를 칠한 첫 영역. 구간 비교식을 전혀 안 쓰는 독립 구현이다.

```text
  random maps vs page-painting reference: 2000 maps (1294 with overlaps), 0 mismatches
```

겹침 있는 맵 1294 / 없는 맵 706 — 두 경우가 다 충분히 나왔다는 것도 CHECK 한다 (생성기가 겹침을 못 만들면 "불일치 0" 은 의미가 없다, ARCH-A04 의 커버리지 원칙).

### Part C — v1 검사, 예약 구멍, rev2

v1 → `map_check` 진단 0 개. 예약 구멍 (실제 출력):

```text
  reserved gaps: 11
    top      0x00_0001_0000 .. 0x00_03ff_ffff
    top      0x00_0404_0000 .. 0x00_07ff_ffff
    top      0x00_0820_0000 .. 0x00_0fff_ffff
    top      0x00_1010_0000 .. 0x00_10ff_ffff
    top      0x00_1110_0000 .. 0x00_1fff_ffff
    top      0x00_2003_0000 .. 0x00_27ff_ffff
    top      0x00_2820_0000 .. 0x00_2fff_ffff
    top      0x00_3100_0000 .. 0x00_7fff_ffff
    top      0x01_8000_0000 .. 0xff_ffff_ffff
    APB_NS   0x00_1000_b000 .. 0x00_100f_ffff
    APB_SEC  0x00_1100_4000 .. 0x00_110f_ffff
```

구멍 목록은 설계 산출물이다: (1) 디코더가 DECERR 를 내야 하는 주소 목록 = 검증 테스트의 입력 (SIV-B06 의 aliasing 테스트가 여기를 찌른다), (2) 다음 리비전의 빈 자리 지도. APB_NS 창 안에는 4 KB 슬롯 245 개가 남았다.

**rev2 변경 요청**: LPDDR 4 → 8 GB. NPU1 추가 (외부 IP, **32비트 AXI 주소**, non-coherent, NPU0 처럼 CPU·ISP 와 버퍼 공유, 모델이 커서 가중치가 많다).

순진한 수정 (`make_rev2_naive`): DRAM_MAIN 크기 += 4 GB, NPU1_CSR 를 "NPU0 다음 64 KB" 에, NPU1 을 DRAM_SHARED·SYS_SRAM 에, "가중치가 크니" DRAM_MAIN 에도 허용. 실제 출력:

```text
  WARN  NONCOH   DRAM_MAIN    @0x00_8000_0000  master NPU1
  WARN  REACH    DRAM_MAIN    @0x00_8000_0000  master NPU1
  WARN  REACH    DRAM_SHARED  @0x01_4000_0000  master NPU1
  ERROR OVERLAP  ISP_CSR      @0x00_2001_0000  with NPU1_CSR
  ERROR OVERLAP  DRAM_MAIN    @0x01_4000_0000  with DRAM_SHARED
  ERROR OVERLAP  DRAM_MAIN    @0x01_7c00_0000  with DRAM_SEC
```

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">DRAM 창 0x00_8000_0000 위쪽 (높이는 용량 비례, 1 GB ≈ 30 px)</text>
  <g stroke="currentColor">
    <rect x="60" y="210" width="170" height="90" class="f-ok"/>
    <rect x="60" y="181" width="170" height="29" class="f-acc"/>
    <rect x="60" y="179" width="170" height="2" class="f-warn"/>
    <rect x="300" y="90" width="170" height="210" class="f-ok" opacity=".8"/>
    <rect x="300" y="181" width="170" height="29" class="f-warn"/>
    <rect x="300" y="179" width="170" height="2" class="f-warn"/>
    <rect x="550" y="270" width="170" height="30" class="f-acc"/>
    <rect x="550" y="62" width="170" height="208" class="f-ok"/>
    <rect x="550" y="60" width="170" height="2" class="f-warn"/>
  </g>
  <g stroke="currentColor" stroke-dasharray="6 4">
    <path d="M40 240 L740 240"/>
  </g>
  <g fill="currentColor">
    <text x="60" y="36" font-weight="bold">v1 (4 GB)</text>
    <text x="300" y="36" font-weight="bold">rev2 순진한 수정</text>
    <text x="550" y="36" font-weight="bold">rev2 고친 맵 (8 GB)</text>
    <text x="70" y="275">DRAM_MAIN 3 GB</text><text x="70" y="200">SHARED 960 MB</text><text x="70" y="172">SEC 64 MB (0x01_7C…)</text>
    <text x="310" y="140">DRAM_MAIN 7 GB</text><text x="310" y="155">→ 0x02_4000_0000</text>
    <text x="310" y="200">SHARED·SEC 와 겹침</text>
    <text x="560" y="290">SHARED 1 GB (0x00_8…)</text>
    <text x="560" y="165">DRAM_MAIN 7104 MB</text><text x="560" y="180">0x00_C000_0000–</text>
    <text x="560" y="54">SEC 64 MB (0x02_7C…)</text>
    <text x="10" y="236">4 GB</text>
    <text x="40" y="320">점선 = 0x01_0000_0000 (32비트 마스터 NPU1 의 한계). v1 의 SHARED 는 그 위라 NPU1 이 못 닿는다 (REACH).</text>
  </g>
</svg>
```

고친 맵 (`make_rev2_fixed`), 이유와 함께:

| 문제 | 고침 | 근거 |
|---|---|---|
| MAIN 이 제자리에서 자라 SHARED·SEC 를 덮음 | DRAM 창 안을 다시 배치: SHARED → SEC 순서가 아니라 **SHARED(맨 아래) → MAIN → SEC(맨 위)** | 크기가 자라는 영역(MAIN)을 고정 크기 영역 사이에 끼우지 않는다 |
| NPU1(32비트)이 SHARED(0x01_4000_0000~)에 못 닿음 | SHARED 1 GB 를 **0x00_8000_0000 (4 GB 아래)** 로 | 32비트 마스터 버퍼는 4 GB 아래 (ZONE_DMA32 같은 생각). 대안: NPU1 앞에 SMMU / 주소 확장 레지스터 → SOCX-M12 |
| NPU1 이 cacheable DRAM_MAIN 에 (non-coherent + 도달 불가) | MAIN 에서 NPU1 제거, 가중치는 SHARED(1 GB 로 키움)에 | 최소 권한 + SWCMO 계약 영역에만 non-coherent |
| NPU1_CSR 이 ISP_CSR 슬롯을 덮음 | 다음 빈 64 KB 슬롯 0x00_2003_0000 | CSR 슬롯을 표로 관리, "NPU0 + 64K" 같은 상대 배치 금지 |

```text
  rev2 fixed: 0 diagnostics
  ...
  0x00_2003_0000  0x00_2003_ffff    64K  NPU1_CSR       CFG     DEV - -      x x . . . x .
  ...
  0x00_8000_0000  0x00_bfff_ffff     1G  DRAM_SHARED    DDR     NRM - C*     x x x x x x x
  0x00_c000_0000  0x02_7bff_ffff  7104M  DRAM_MAIN      DDR     NRM - C      x x x . . x .
  0x02_7c00_0000  0x02_7fff_ffff    64M  DRAM_SEC       DDR     NRM S C      x x . . . x .
```

검사: DDR 포트 영역 합 = 8 GB, NPU1 이 `0x00_BFFF_FFFC` 에 쓰기 OKAY, `0x00_C000_0000`(MAIN) 은 DECERR NOROUTE, 구멍 11 개 그대로(마지막 구멍이 0x02_8000_0000 부터). 고친 맵의 대가는 **소프트웨어가 보는 주소가 바뀐 것** — SHARED 와 SEC 가 움직였으니 디바이스 트리의 reserved-memory, TEE 로드 주소, 부트로더가 다 바뀐다 (연습 1 이 그 목록을 뽑는다). v1 에서 SHARED 를 처음부터 4 GB 아래 맨 앞에 뒀다면 rev2 는 MAIN 크기와 SEC 위치만 바뀌었을 것이다 — **설계 리뷰에서 "32비트 마스터가 생기면?" 을 미리 물었어야** 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_ARCH-A12/so && /tmp/esn_ARCH-A12/so    # 81/81 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_ARCH-A12/san && /tmp/esn_ARCH-A12/san   # 깨끗, 출력 동일
```

(Apple M6, Apple clang 21.0.0. 결정적 모델이라 타이밍 없음 — 실행 0.1 초 미만, `uptime` load 9 대의 공유 머신이어도 결과 무관.)

바뀐 곳은 TODO 5 개뿐이다. 나머지(영역 표 = 설계 문서, rev2 생성기, 출력, 참조 디코드, 페이지 칠하기 퍼저, 하네스)는 starter 와 같다. 설계 메모:
- **표가 원본, 코드는 규칙**: 영역 표는 문서와 1:1 이고 규칙은 데이터와 분리돼 있다 — 그래서 rev2 는 표를 몇 줄 바꾸는 함수일 뿐이고 같은 검사기를 그대로 돈다. 실무의 IP-XACT / SystemRDL 맵 → 헤더·RTL·문서 생성 흐름의 축소판 (연습 4).
- **부모를 이름으로 참조**: 인덱스로 참조하면 표에 줄 하나 끼울 때 모든 자식이 틀린 부모를 가리킨다. 이름이면 "없는 창" 이 APB 오류로 드러난다.
- **진단은 (코드, 영역, 상대) 로 구조화**: 테스트가 문자열이 아니라 필드를 비교하므로 메시지 문구를 바꿔도 테스트가 안 깨진다.

## 7. 자주 하는 실수

1. **겹침을 정렬된 이웃끼리만 비교** — 표가 정렬돼 있다는 가정, 그리고 큰 영역 하나가 이웃 너머 여러 영역을 덮는 경우를 놓친다 (rev2 순진한 수정에서 MAIN 이 SHARED 와 SEC 를 **둘 다** 덮었다). 고침: 같은 단계 모든 쌍 `for i, for j > i`.
2. **브리지 창과 그 자식을 겹침으로 신고** — UART0 ⊂ APB_NS 는 포함이다. 단계를 구분하지 않으면 깨끗한 맵에서 오류 11 개가 쏟아지고, 엔지니어는 검사기를 끈다. 고침: `parent_of(i) == parent_of(j)` 일 때만 비교.
3. **32비트 `base + size`** — 40비트 맵에서는 컴파일조차 경고 없이 잘린다 (`uint32_t` 에 0x01_4000_0000 대입). 모든 주소·크기 `uint64_t`, 끝은 `rend()` 하나로.
4. **허용 마스크만 믿는다** — "행렬에 NPU 가 없으니 안전". 마스크를 고치는 실수 하나(Part B 의 SECURE 주입)로 뚫린다. 라우팅 + AxPROT 배선 + TZ 필터 + (SMMU) 를 겹쳐야 하고, 검사기는 그 모순을 **오류**로 잡아야 한다.
5. **non-coherent 마스터를 그냥 cacheable 영역에** — 드라이버가 CMO 를 한 번 빠뜨리면 NPU 가 옛 가중치를 읽는다 (ARCH-M12 의 stale 버그가 SoC 규모로). 고침: SWCMO 계약 영역에만 허용 + NONCOH 경고.
6. **DRAM 위에 바로 MMIO 를 붙이거나, 고정 영역 사이에 자라는 영역을 둔다** — 다음 리비전에서 맵 전체가 움직인다. 고침: 자라는 것은 위쪽 끝이 열린 곳에, 고정 크기 것은 아래에.
7. **주소 폭이 다른 마스터를 잊는다** — 외부 IP 의 32비트 포트는 흔하다. REACH 경고 없이 넘어가면 실리콘에서 "NPU1 이 엉뚱한 주소에 쓴다"(상위 비트가 잘린 주소 = 다른 영역)로 나타난다.

## 8. 면접 Q&A

<details><summary>Q1. 왜 40비트 PA 인가? 32비트로 안 되나? 36비트는?</summary>

32비트(4 GB)는 DRAM 4 GB 만으로 꽉 찬다 — MMIO·SRAM·ROM 자리가 없고, 검사기도 DRAM 세 영역 전부 RANGE. 옛 방식(DRAM 일부만 4 GB 아래, 나머지는 위쪽 별칭)은 결국 32비트 너머가 필요하다. 36비트(64 GB)면 rev2 까지 충분하지만 CPU 코어가 보통 40비트를 구현하니 인터커넥트만 줄일 이유가 약하다. 비용은 주소 경로 몇 비트로 미미하다.

꼬리질문: "그럼 32비트 마스터는?" → 4 GB 위를 못 본다. (1) 공유 버퍼를 4 GB 아래에 두거나(rev2 의 고침, 리눅스 ZONE_DMA32 같은 생각), (2) 주소 확장 레지스터(상위 비트를 덧붙이는 창)나 (3) SMMU 로 32비트 IOVA 를 40비트 PA 로 변환한다 (SOCX-M12). (1) 은 공짜지만 맵을 제약하고, (3) 은 유연하지만 TLB 미스 지연·면적이 든다.
</details>

<details><summary>Q2. NPU 를 coherent 로 붙이지 않은 이유는? 언제 IO-coherent 가 낫나?</summary>

NPU 는 수 GB/s 를 스트리밍한다(연습 3: NPU 하나 ≈ 0.97 GB/s, 가정 수치). 모든 접근이 CPU 캐시를 스누프하면 CPU 클러스터의 스누프 대역폭과 태그 조회를 잡아먹고 NPU 지연도 는다. 그런데 그 데이터(가중치·활성값·프레임)는 CPU 가 거의 만지지 않고, 소유권이 바뀌는 시점은 추론/프레임마다 한 번이다 → 그때 clean/invalidate (ARCH-M12) 하는 게 훨씬 싸다. 그래서 non-coherent + SWCMO 영역.

꼬리질문: "그럼 DMA 는 왜 IO-coherent?" → 대역폭이 작고, 디스크립터·작은 버퍼가 CPU 와 자주 오간다. 드라이버마다 CMO 를 정확히 하게 만드는 것보다 하드웨어 스누프가 싸고 안전하다. 절충: NPU 의 **명령 큐·완료 플래그** 같은 작은 공유 상태만 IO-coherent 포트나 non-cacheable 로, 대용량 데이터는 non-coherent — 경로를 둘로 나누는 설계도 흔하다 (구현마다 다름).
</details>

<details><summary>Q3. DECERR 와 SLVERR 는 누가 내고, bring-up 에서 어떻게 쓰나?</summary>

DECERR = 인터커넥트가 그 주소(그 마스터에게 보이는 슬레이브 중)에 맞는 슬레이브를 못 고름 → 인터커넥트 안의 기본 슬레이브가 응답. SLVERR = 슬레이브(또는 그 앞 필터·브리지)까지 갔는데 거부. 이 모델: 빈 주소·경로 없음·디버그 잠김 = DECERR, TZ·브리지 구멍·브리지 필터·ROM 쓰기 = SLVERR. 의미: DECERR 면 "주소/맵/라우팅" 을, SLVERR 면 "보안 상태·IP 설정·클럭" 을 먼저 본다.

꼬리질문: "CPU 에서는 어떻게 보이나?" → Cortex-M 은 BusFault(ARCH-B11), AArch64 는 동기 External Abort(읽기) 또는 비동기 SError(쓰기 버퍼 뒤의 쓰기 — ARCH-B11 의 imprecise 와 같은 이유)로 보인다. 둘 다 DECERR/SLVERR 를 구분해 주지 않을 수 있어서(구현에 따라 syndrome 에 일부 정보) 실리콘 검증에선 버스 모니터·트레이스로 응답 코드를 직접 본다. 또 TZ 필터는 오류 대신 RAZ/WI 로 조용히 막게 설정될 수도 있다 — 그러면 "0 만 읽힌다" 가 단서다.
</details>

<details><summary>Q4. NPU 가 secure 메모리에 접근 못 하게 하려면? 몇 겹인가?</summary>

이 설계는 네 겹: (1) **라우팅** — 인터커넥트가 NPU 포트에서 BOOTROM/SEC_SRAM/APB_SEC 로 가는 경로 자체를 안 만든다 (DECERR). (2) **AxPROT[1] 배선** — NPU 는 secure 트랜잭션을 낼 수 없다(항상 NS). (3) **TZ 필터** — DRAM 앞 TZC 류 필터가 DRAM_SEC 을 secure 전용으로, APB_SEC 창은 브리지 앞에서 secure 전용 (SLVERR). (4) **SMMU**(다음 단계 SOCX-M12) — NPU 의 스트림을 non-secure 로 두고 허용 버퍼만 매핑. 그리고 (0) **설계 시 검사기** — 행렬이 정책과 모순되면 SECURE 오류. Part B 는 (0) 이 뚫려도 (2)+(3) 이 막는 것을 테스트로 보였다.

꼬리질문: "필터 설정(TZC_CFG) 자체는 누가 바꾸나?" → secure CPU 만 (행렬에서 TZC_CFG 는 B-S 만 ok, 디버그도 SE). 설정 레지스터를 normal world 가 쓸 수 있으면 모든 겹이 무의미하다 — 보안 설계에서 가장 먼저 보는 곳. 부트 시 secure 부트 코드가 설정하고 잠그는(lock) 기능을 두는 필터도 있다 (구현마다 다름).
</details>

<details><summary>Q5. 디버그 접근은 어떻게 설계했나? 양산 칩에서는?</summary>

DAP 의 AXI-AP 가 IO-coherent 포트로 들어온다. 잠금 3 단계로 모델링: LOCKED(모든 접근 DECERR), NS(비보안만 — Arm 디버그 인증의 DBGEN 에 해당), FULL(secure 포함 — SPIDEN 추가). 그래도 KEYSTORE·TRNG·TZC_CFG 는 FULL 이어도 행렬에서 빠져 있다 — 키는 어떤 디버그 상태로도 안 나가야 한다.

꼬리질문: "양산 칩의 잠금 해제는?" → 보통 OTP 의 라이프사이클 상태(개발/양산/RMA)가 기본 잠금을 정하고, 양산 칩은 secure 펌웨어가 서명된 challenge-response 인증을 확인한 뒤 디버그 인증 신호를 켠다 (방식은 벤더마다 다름 — 일반론). 면접 포인트: 디버그 포트는 공격 표면이라 **기본값이 잠김**이어야 하고, 잠금 해제가 키 접근까지 열면 안 된다.
</details>

<details><summary>Q6. rev2 에서 DRAM 을 늘렸더니 맵이 깨졌다. v1 을 어떻게 설계했어야 했나?</summary>

(1) 자라는 영역(DRAM_MAIN)의 위쪽 끝이 열려 있어야 한다 — v1 은 MAIN 위에 SHARED·SEC 를 쌓아 막았다. (2) 32비트 마스터 가능성을 고려해 공유 버퍼를 4 GB 아래에. (3) DRAM 창 위에 큰 예약 구멍(v1 의 0x01_8000_0000–0xFF_FFFF_FFFF)은 잘했다 — MMIO 를 DRAM 바로 위에 붙였다면 MMIO 까지 옮겨야 했다. 대안으로 Arm 백서 스타일처럼 DRAM 을 "하위 2 GB 창 + 상위 큰 창" 으로 나눠 두고 상위 창이 자라게 하는 방식도 있다.

꼬리질문: "맵 변경의 소프트웨어 비용은?" → 연습 1 의 diff: 움직인 영역 3 개 + 새 영역 1 개 + 권한 변경 2 개. 디바이스 트리 memory/reserved-memory 노드, TEE 로드 주소, 부트 ROM 이 하드코딩한 주소(ROM 은 고칠 수 없다!), 검증 테스트가 영향받는다. 그래서 부트 ROM 이 쓰는 주소(SRAM, 부트 장치 컨트롤러)는 리비전 사이에 절대 안 움직이게 한다.
</details>

<details><summary>Q7. CSR(설정 레지스터)을 메인 크로스바에 바로 달지 않고 AXI4-Lite/APB 로 내린 이유는?</summary>

레지스터 접근은 작고(32비트 단일) 드물며 느린 슬레이브가 많다. 메인 크로스바의 넓은 포트(128–256비트, 버스트, outstanding 다수)를 레지스터 블록마다 다는 것은 면적 낭비이고, 느린 슬레이브가 큰 마스터 경로를 막는(HOL) 위험도 있다 (SOCX-M01). 그래서 메인 → 설정 버스(AXI4-Lite) → APB 로 폭·기능을 줄여 간다. 대가: APB 접근 하나가 브리지 지연(ARCH-M13: 최소 2 PCLK + 브리지 단계)이라 수십 ns — 인터럽트 핸들러에서 레지스터를 많이 만지면 보인다.

꼬리질문: "APB 브리지를 NS/S 둘로 나눈 이유?" → 창 단위로 필터를 걸면 단순하다(창 전체가 secure). APB4 의 PPROT 로 한 브리지에서 슬롯별 보안을 거는 방법도 있지만, 슬롯별 보안 설정 레지스터(TZPC 류)가 필요하고 그 설정 자체가 공격 대상이 된다.
</details>

<details><summary>Q8. 이 맵 검사기는 실무에서 어디에 쓰이나? 검사기를 어떻게 믿나?</summary>

맵 하나가 RTL 디코더 파라미터, 방화벽 초기 설정, 부트 ROM 상수, C 헤더, 디바이스 트리, 검증 테스트(구멍 = DECERR 기대, SIV-B06 aliasing)의 원본이다. IP-XACT(IEEE 1685)나 SystemRDL 같은 기술 언어로 맵을 쓰고 생성기로 산출물을 만드는 흐름에서, 이 검사기 같은 규칙 검사는 생성 전에 돈다 (도구 세부는 일반론). 믿는 방법: (1) 오류를 하나씩 주입해 **정확히 그 진단**이 나오는지(14 건, 코드 11 종), (2) 독립 구현(페이지 칠하기)과 무작위 2000 맵 비교, (3) 그 무작위가 겹침 있는/없는 경우를 둘 다 충분히 만들었는지(1294/706) — ARCH-A04 의 돌연변이·커버리지 원칙과 같다.

꼬리질문: "맵이 맞는데 실리콘에서 엉뚱한 슬레이브가 응답한다면?" → 디코더가 상위 비트를 다 비교하지 않는 **불완전 디코드**(aliasing)일 수 있다 — 맵 검사기는 의도를, SIV-B06 같은 aliasing 테스트는 구현을 검사한다. 둘 다 필요하다.
</details>

## 9. 추가 연습

1. **v1 → rev2 diff.** 두 맵을 이름으로 맞춰 NEW / REMOVED / MOVED / RESIZED / ATTR / MASTERS 를 뽑고, "같은 PA 가 다른 영역이 된" 주소(v1 영역 base 기준)를 찾아라.
   <details><summary>답</summary>

   실제 출력: `SYS_SRAM MASTERS`, `DRAM_MAIN MOVED RESIZED`, `DRAM_SHARED MOVED RESIZED MASTERS`, `DRAM_SEC MOVED`, `NPU1_CSR NEW` (5 개). 같은 PA 다른 뜻 3 개: `0x00_8000_0000: v1 DRAM_MAIN -> rev2 DRAM_SHARED`, `0x01_4000_0000: DRAM_SHARED -> DRAM_MAIN`, `0x01_7c00_0000: DRAM_SEC -> DRAM_MAIN`. 셋째가 가장 위험하다 — v1 의 secure carve-out 주소를 하드코딩한 TEE/부트로더가 rev2 에서 **normal world 메모리**에 secure 데이터를 놓게 된다. 리비전 사이 맵 diff 는 소프트웨어 팀에 보내는 필수 산출물이다.
   </details>
2. **상위 비트 비교 디코더와 2^k 블록 분해.** POW2 최상위 영역마다 (mask, match) 와 비교 비트 수를 구하고 mask 비교 == 범위 비교를 무작위로 확인하라. POW2 가 아닌 DRAM_MAIN/SHARED 를 정렬된 2^k 블록으로 쪼개면 몇 개인가 (v1, rev2)?
   <details><summary>답</summary>

   11 개 POW2 영역, 불일치 0. 비교 비트: BOOTROM PA[39:16] 24 비트 … CORESIGHT 16 비트, DRAM_SEC 14 비트 — 큰 영역일수록 비교할 비트가 적다. 블록 분해(탐욕: 현재 주소의 정렬과 남은 길이 중 작은 2^k): v1 MAIN 3 GB → `2G 1G` (2 개), v1 SHARED 960 MB → `512M 256M 128M 64M` (4 개), rev2 MAIN 0x00_C000_0000 + 7104 MB → `1G 4G 1G 512M 256M 128M 64M` (7 개), rev2 SHARED → 1 개. 블록 수 = 그 영역을 POW2 전용 필터/MPU 로 덮을 때 필요한 영역 슬롯 수. 그래서 큰 DRAM 창은 base/limit(범위) 비교를 쓰고, POW2 규칙은 작은 영역과 secure carve-out 에만 걸었다 (ARMv8-M PMSAv8 이 base/limit 로 바뀐 것과 같은 이유, ARCH-M15).
   </details>
3. **DRAM 대역폭 예산.** 4K30 카메라(RAW10 덤프 + ISP 읽기 + YUV420 출력 + 640×640 RGB), NPU(입력, 가중치 6 MB/추론, 활성값 spill 24 MB/추론, 30 추론/s), CPU 1 GB/s — 모두 가정치 — 를 LPDDR4X-4266 x32 (효율 70% 가정)와 비교하라. rev2 의 NPU1 은 얼마를 더하나?
   <details><summary>답</summary>

   실제 출력: RAW10 311.0 + 311.0, YUV420 373.2, RGB 36.9, NPU 입력 36.9, 가중치 180.0, spill 720.0, CPU 1000.0 MB/s. 피크 17064.0 MB/s (4266 MT/s × 4 B), 쓸 수 있는 70% = 11944.8. **v1 2969.1 MB/s = 24.9%, rev2 3942.8 = 33.0%** (NPU1 +973.7). 평균으로는 여유가 있다 — 그래도 버스트·지연이 문제다: NPU spill 이 몰리는 순간 ISP 의 실시간 쓰기가 밀리면 프레임이 깨진다. 그래서 QoS(ISP 우선순위·NPU 대역폭 제한)가 필요하고 그건 ARCH-A10 / SOCX-M03 의 주제다. RAW 덤프를 없애는(ISP 인라인) 설계면 622 MB/s 가 준다.
   </details>
4. **헤더 생성 (single source of truth).** 정렬된 맵에서 `#define SOC_<NAME>_BASE` / `_SIZE` 를 생성하고, 이름이 C 식별자(대문자·숫자·_)이며 중복이 없는지 검사하라.
   <details><summary>답</summary>

   rev2 맵 → 2187 바이트, 예: `#define SOC_UART0_BASE 0x0010000000ULL`, `#define SOC_DRAM_SEC_BASE 0x027c000000ULL`, `#define SOC_DRAM_MAIN_SIZE 0x1bc000000ULL`. 이름 중복 0, 잘못된 식별자 0. 손으로 쓴 헤더와 RTL 디코더 파라미터가 따로 놀면 "문서엔 0x2003_0000, RTL 엔 0x2001_0000" 같은 버그가 생긴다 — 한 원본에서 생성하고, 검사기는 생성 전에 돈다.
   </details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_ARCH-A12/pr && /tmp/esn_ARCH-A12/pr   # solution.c 를 포함하므로 같은 폴더에서
```

실제 출력 (일부):

```text
== 연습 1: v1 -> rev2 diff ==
  SYS_SRAM     MASTERS
  DRAM_MAIN    MOVED RESIZED
  DRAM_SHARED  MOVED RESIZED MASTERS
  DRAM_SEC     MOVED
  NPU1_CSR     NEW
  PA 0x00_8000_0000: v1 DRAM_MAIN -> rev2 DRAM_SHARED
  PA 0x01_4000_0000: v1 DRAM_SHARED -> rev2 DRAM_MAIN
  PA 0x01_7c00_0000: v1 DRAM_SEC -> rev2 DRAM_MAIN
== 연습 2: top-bits decoder ==
  BOOTROM    compare PA[39:16] (24 bits) == 0x0
  ...
  DRAM_SEC   compare PA[39:26] (14 bits) == 0x5f
  11 POW2 top-level regions, mask-vs-range mismatches 0
  v1   DRAM_MAIN   0x00_8000_0000 +3G -> 2 blocks: 2G 1G
  v1   DRAM_SHARED 0x01_4000_0000 +960M -> 4 blocks: 512M 256M 128M 64M
  rev2 DRAM_MAIN   0x00_c000_0000 +7104M -> 7 blocks: 1G 4G 1G 512M 256M 128M 64M
  rev2 DRAM_SHARED 0x00_8000_0000 +1G -> 1 blocks: 1G
== 연습 3: DRAM bandwidth budget (대표값/가정) ==
  ...
  peak 17064.0 MB/s, usable(70%) 11944.8 MB/s
  v1   total  2969.1 MB/s = 24.9% of usable
  rev2 total  3942.8 MB/s = 33.0% of usable (NPU1 adds 973.7)
== 연습 4: header generation ==
  2187 bytes, first lines:
#define SOC_BOOTROM_BASE 0x0000000000ULL
#define SOC_BOOTROM_SIZE 0x10000ULL
...
17/17 passed
```

## 10. 핵심 정리

- 주소 맵 = **디코더 맵 + 방화벽 설정 + 소프트웨어 계약**. 데이터(표)로 두고 규칙은 검사기로 — 문서·헤더·RTL·테스트가 한 원본에서 나온다.
- PA 폭은 DRAM + MMIO + 성장 여유로 정한다: 4 GB DRAM 이면 32비트 불가, 코어와 맞춰 40비트. **32비트 마스터 버퍼는 4 GB 아래.**
- 배치 원칙: 고정 크기 온칩 것은 아래, DRAM 은 위로 자라게, 클래스 사이 구멍, 주변장치 4 KB 슬롯, 작은 영역은 POW2(상위 비트 비교·MPU 영역 하나), 큰 DRAM 창은 범위 비교.
- 일관성: CPU 끼리 완전 coherent, DMA/디버그 IO-coherent, NPU/ISP non-coherent + SWCMO 영역에서만 (ARCH-M12). non-coherent 마스터가 일반 cacheable 영역에 붙으면 경고.
- 응답: 슬레이브를 못 고르면 **DECERR**(기본 슬레이브: 빈 주소·경로 없음), 도착했는데 거부면 **SLVERR**(TZ 필터·브리지 구멍·ROM 쓰기). 디버그 단서로 쓴다 — 단 필터가 RAZ/WI 로 조용히 막을 수도 있다.
- NPU → secure 금지는 여러 겹: 라우팅 · AxPROT 배선 · TZ 필터 · SMMU · 설계 시 검사기. 방화벽 설정 레지스터는 secure CPU 만.
- 검사기를 믿는 법: 오류 하나 주입 → 진단 정확히 하나, 독립 참조(페이지 칠하기)와 무작위 비교, 생성기 커버리지 확인.
- 리비전: 자라는 영역을 고정 영역 사이에 끼우지 말 것. 맵 diff(같은 PA 다른 뜻)는 소프트웨어 팀의 필수 입력이고, 부트 ROM 이 쓰는 주소는 영원히 고정.

## 참고자료

- Arm, *Principles of Arm Memory Maps* (white paper, DEN0001) — 32/36/40비트 이상 시스템의 권장 맵 모양, DRAM 창 배치 (세부 숫자는 원문 확인).
- Arm, *AMBA AXI and ACE Protocol Specification* (IHI 0022) — RRESP/BRESP 의 OKAY / EXOKAY / SLVERR / DECERR, AxPROT[1] (Non-secure), ACE-Lite(IO coherency).
- Arm, *AMBA APB Protocol Specification* (IHI 0024) — PSLVERR(APB3), PPROT(APB4).
- Arm, *CoreLink TZC-400 TrustZone Address Space Controller TRM* — DRAM 앞 영역 필터의 예 (영역 수·응답 설정은 TRM 확인).
- Arm, *CoreSight Architecture Specification* (IHI 0029) — 디버그 인증 신호(DBGEN/SPIDEN 등), DAP/AP.
- Arm, *Arm Architecture Reference Manual for A-profile* (DDI 0487) — PA 크기 설정(TCR_ELx), Secure/Non-secure 물리 주소 공간, External Abort / SError.
- Cortex-A55 / Cortex-A76 Technical Reference Manuals — 구현된 PA 폭 (코어별 확인).
- Linux kernel documentation — DMA API(`Documentation/core-api/dma-api.rst`), ZONE_DMA32, devicetree `reserved-memory` 바인딩.
- IEEE 1685 (IP-XACT), Accellera SystemRDL 2.0 — 레지스터/주소 맵 기술 언어와 생성 흐름.
- 이 노트의 관련 레슨: ARCH-B11 (디코더·버스 폴트 — 이 레슨의 출발점), ARCH-M12 (non-coherent DMA CMO), ARCH-M13 (AHB/APB 브리지 지연), ARCH-M14 (AXI 채널·4 KB 경계), ARCH-M15 (MPU 2^n 정렬·PMSAv8 base/limit), ARCH-A04 (하드웨어 일관성 프로토콜), ARCH-A10 (중재·QoS), SIV-B06 (aliasing 테스트). 다음 단계: SOCX-B01 (인터커넥트 토폴로지), SOCX-M12 (SMMUv3), SOCX-A10 (칩렛 SoC 인터커넥트), SOCX-M03 (QoS 레귤레이터).
