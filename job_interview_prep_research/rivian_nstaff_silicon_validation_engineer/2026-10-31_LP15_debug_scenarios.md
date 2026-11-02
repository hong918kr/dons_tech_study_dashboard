# LP15 · 디버그 시나리오 10선 (+보너스 2) — 증상 → 볼 것 → 가설 → bisect → 회귀 테스트
<!-- theme: pastel -->

> **묶음**: E 검증·디버그 · **우선**: P0 ★ 모의 면접 형식 (도메인 라운드 단골, 다른 노트를 끌어오는 허브) · **선행 노트**: [LP03 링크 트레이닝](2026-10-31_LP03_link_training_ltssm.html), [LP13 도구](2026-10-31_LP13_linux_pcie_tools.html), [LP14 검증 방법론](2026-10-31_LP14_validation_methodology.html) · **다음 노트**: [LP16 bare-metal vs Linux](2026-10-31_LP16_baremetal_vs_linux_zynq.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html), [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), [N01 §4–§5 디버그 플로우](2026-10-31_N01_pcie_lspci_ltssm.html) · **코드**: `onsitePrep/lp15/pcie_snapshot.sh` (Lima VM에서 실행 확인, 출력 `onsitePrep/lp15/pcie_snapshot.vm_output.txt`)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: "장치가 안 보인다 / 속도가 낮다 / 폭이 줄었다 / 드라이버가 BAR를 못 받는다 / 인터럽트가 안 온다 / DMA 데이터가 깨진다 / 에러가 쏟아진다 / 링크가 끊긴다 / resume 후 사라진다 / 부팅이 멈춘다" + 보너스 "차량 온도에서만 간헐 drop", "throughput이 기대보다 낮다" — **면접관이 시나리오를 던지면 같은 틀로 2분 안에 답하는 법**.
- **외울 것 ① 아래층부터 (bottom-up)**: 전원·클럭·리셋 → PHY/LTSSM → DLL(DL_Active) → config/열거 → 리소스(BAR·윈도우) → 드라이버 → 인터럽트·DMA → 성능. **각 층마다 "이 층이 OK"라는 증거 1개**를 말할 수 있어야 한다 (예: DLL = root port `LnkSta: DLActive+`).
- **외울 것 ② 한 번에 하나만 바꾸는 bisect 손잡이**: Gen1 강제 (`LnkCtl2` Target Link Speed), 폭 제한, ASPM off (`pcie_aspm=off`), IOMMU passthrough (`iommu.passthrough=1`), 다른 슬롯/보드/카드/호스트, 온도·전압 corner. **"좋은 것 하나 + 나쁜 것 하나"** 를 만들고 차이를 반씩 줄인다.
- **외울 것 ③ HW vs SW 증명 = "SW를 빼고도 재현되나"**: bare-metal/다른 OS/다른 호스트에서 재현, 애널라이저로 와이어 위 TLP 확인, 스코프로 PERST#/refclk 확인. 와이어에 TLP가 나갔는데 장치가 틀린 답을 주면 HW, 와이어에 아예 안 나가면 SW/설정.
- **로그 문자열은 커널 버전마다 바뀐다**: 예) AER는 6.6에서 `PCIe Bus Error: severity=Corrected, type=Physical Layer, (Receiver ID)` → 최신 mainline은 `PCIe Bus Error: severity=Correctable` + `[ 0] RxErr | Receiver | Physical Layer` 표 형식 [1][2]. DesignWare는 `Phy link never came up` → 최신은 `Link failed to come up. LTSSM: ...` [3]. 면접에서는 "정확한 문자열"보다 **"어느 드라이버의 어느 함수가 무엇을 보고 찍은 줄인지"** 가 점수.
- **30초 영어 (헤드라인 질문 "How do you debug a PCIe problem?")**: "I go bottom-up and I want one piece of evidence per layer. Power, refclk and PERST# on the scope first; then the root port's Link Status — did the LTSSM reach L0 and is DLActive set, at what speed and width; then enumeration and resources in dmesg and lspci; then the driver, interrupts and DMA. At each step I change one variable — force Gen1, swap the card or slot, disable ASPM or the IOMMU — to split hardware from software. Once I find the cause, I turn the reproduction into an automated regression test that loops boots or resets and checks speed, width and error counters."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "새 아파트 단지에 택배 회사가 입점하는 과정"

PCIe 장치 하나가 동작하기까지는 **아파트 단지에 택배 회사가 입점해서 첫 배달을 하는 과정**과 비슷하다. 어디서 막혔는지에 따라 증상이 완전히 다르다.

| 층 | 택배 비유 | PCIe 실제 | 여기서 막히면 사용자가 보는 것 |
|---|---|---|---|
| 0. 전원·클럭·리셋 | 건물에 전기·수도가 들어오고 경비실이 문을 연다 | 전원 rail, 100 MHz refclk, **PERST#** (리셋 신호) 해제 | 장치가 아예 없음 |
| 1. 물리 링크 (PHY/LTSSM) | 단지와 물류센터 사이 **도로가 개통** (차선 수·제한속도 결정) | **LTSSM**이 Detect → Polling → Configuration → L0, 속도/폭 협상 | 장치 없음, 또는 느린 속도·좁은 폭 |
| 2. 데이터 링크 (DLL) | 등기 시스템: 보낸 상자마다 **수령 확인(ACK)**, 분실 시 재발송 | ACK/NAK, replay, flow-control credit → **DL_Active** | 에러 카운터 증가, 간헐 끊김 |
| 3. 열거 (config) | 관리사무소가 **입주자 명부**에 등록 (몇 동 몇 호인지) | config read로 Vendor ID 확인, 버스 번호 배정 | `lspci`에 안 보임, `rev ff` |
| 4. 리소스 (BAR) | 택배 회사에 **창고 주소** 배정 (크기만큼) | BAR 크기 측정 → 메모리 주소 할당, bridge window | 드라이버 probe 실패 "no space" |
| 5. 드라이버 | 택배 기사(담당 직원) 배치 | probe → `pci_enable_device` → MMIO 매핑 | 장치는 보이는데 기능 없음 |
| 6. 인터럽트·DMA | **초인종**(인터럽트) / 기사가 **직접 집 안에 상자를 넣어 줌**(DMA) | MSI/MSI-X = 메모리 쓰기로 된 초인종, DMA = 장치가 RAM을 직접 읽고 씀 | timeout, 데이터 깨짐 |
| 7. 성능 | 하루 배달량 | throughput / latency | "되긴 하는데 느림" |

- **핵심 직관**: 위층은 아래층이 멀쩡해야만 의미가 있다. 초인종(인터럽트)이 안 울린다고 초인종부터 뜯는 사람은 없다 — **도로가 뚫렸는지, 명부에 등록됐는지부터** 본다. 이게 bottom-up.
- **둘째 직관**: 증상은 위층에서 보여도 원인은 아래층에 있는 경우가 많다. 예) "NVMe가 가끔 timeout" (6층 증상) ← 실제 원인은 고온에서 신호 margin 부족(1층).

### 1.2 계층 그림 — 어디를 보면 그 층이 OK인지 증명되나

```text
 성능            fio / 커스텀 DMA 벤치 / perf counter        "몇 % of 이론치?"
 ─────────────────────────────────────────────────────────────────────────────
 인터럽트·DMA    /proc/interrupts 카운트 증가, 데이터 CRC 비교, SMMU fault 로그
 드라이버        lspci -k "Kernel driver in use", dmesg probe 로그
 리소스          dmesg "BAR n [mem ...]: assigned", /proc/iomem, lspci "Region"
 열거(config)    lspci 에 BDF가 보임, Vendor ID != 0xFFFF
 DLL             root port LnkSta "DLActive+"  (DL_Active = flow-control init 완료)
 PHY / LTSSM     LnkSta Speed/Width, LnkSta2 Equalization*, vendor LTSSM 레지스터
 전원·클럭·리셋   스코프: rail 전압, refclk 100 MHz, PERST# 타이밍 (T_PVPERL ≥ 100 ms)
 ─────────────────────────────────────────────────────────────────────────────
           ▲ 아래부터 하나씩 "OK 증거"를 확보하며 올라간다 (bottom-up)
```

### 1.3 비유 — bisect는 "고장 난 크리스마스 전구 줄 찾기"

- 전구 100개짜리 줄에서 하나가 나가면 줄 전체가 안 켜진다. 하나씩 다 바꾸면 100번. **가운데를 잘라 어느 반쪽이 문제인지** 보면 7번이면 끝난다.
- PCIe에서 "자를 곳" = **변수**: 카드(다른 카드), 슬롯(다른 슬롯), 보드(다른 보드), 호스트(x86 PC에 꽂아 보기), SW(커널 버전, 커널 옵션), 속도(Gen1 강제), 온도. **좋은 조합 하나와 나쁜 조합 하나**를 확보한 다음 변수를 하나씩 옮긴다.

### 1.4 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| RC / Root Port | Root Complex / Root Port | CPU 쪽 PCIe의 "본사"와 그 출입구(포트) | 링크가 죽어도 root port는 항상 `lspci`에 보임 → 첫 관측 지점 |
| EP | Endpoint | 링크 끝에 붙은 실제 장치 (NVMe, NIC, NPU, FPGA) | 링크가 안 뜨면 아예 안 보임 |
| BDF | Bus:Device.Function | 장치의 "동·호수" 주소, 예 `01:00.0` | 모든 명령의 대상 지정 |
| PERST# | PCIe Reset (active low) | 보드가 장치에 주는 리셋 선. 풀려야 링크 학습 시작 | 타이밍 틀리면 장치 없음/간헐 실패 |
| refclk | Reference Clock | 100 MHz 기준 클럭 (양쪽 PHY의 박자) | 없거나 지터 크면 학습 실패·에러 |
| LTSSM | Link Training and Status State Machine | 도로 개통 절차를 진행하는 상태 기계 (Detect→Polling→Config→L0→Recovery) | "어디까지 갔나"가 첫 질문 |
| L0 | (링크 정상 상태) | 도로 개통 완료, 차가 다니는 상태 | L0 + DL_Active 전에는 config 접근 불가 |
| DL_Active / DLActive | Data Link Layer Link Active | 등기 시스템까지 준비됨 (flow-control 초기화 끝) | root port `LnkSta`의 bit 13 [17] |
| LnkCap / LnkSta / LnkCtl2 | Link Capabilities / Status / Control 2 | "할 수 있는 최대" / "지금 협상된 값" / "목표 속도 설정" | 속도·폭 문제의 1차 증거 |
| Equalization (EQ) | 링크 이퀄라이제이션 | Gen3+에서 송신 파형(preset)을 조정해 받는 쪽 눈(eye)을 여는 협상 | 실패하면 Gen1/2에 머묾 |
| BAR | Base Address Register | 장치가 "내 레지스터를 이 크기만큼 메모리 주소에 매핑해 달라"고 요청하는 칸 | 주소 공간 부족 = "no space" |
| bridge window | (브리지 메모리 윈도우) | 스위치/root port가 "이 주소 범위는 내 아래로 보내라"고 정한 구간 | 아래 장치의 BAR는 전부 이 안에 들어가야 함 |
| MSI / MSI-X | Message Signaled Interrupts (eXtended) | 장치가 특정 주소에 **메모리 쓰기**를 해서 초인종을 누르는 방식 | Bus Master 꺼지면 초인종도 못 누름 |
| DMA | Direct Memory Access | 장치가 CPU 거치지 않고 RAM을 직접 읽고 씀 | 캐시·IOMMU 설정 틀리면 데이터 깨짐 |
| IOMMU / SMMU | I/O Memory Management Unit / (Arm) System MMU | 장치가 보는 주소(IOVA)를 실제 RAM 주소로 바꾸고 막는 "출입 통제소" | 매핑 없으면 fault, DMA 실패 |
| AER | Advanced Error Reporting | PCIe 에러 장부: correctable / non-fatal / fatal | 신호 margin 추세를 보는 계기판 |
| DPC | Downstream Port Containment | 심각한 에러 시 포트가 링크를 **끊어서 격리**하는 기능 | "장치가 갑자기 사라짐"의 원인일 수 있음 |
| CRS / RRS | Configuration Request Retry Status | 장치가 "아직 부팅 중, 다시 물어봐"라고 답하는 것 | 너무 빨리 포기하면 장치 없음 |
| ASPM | Active State Power Management | 링크를 놀 때 저전력(L0s/L1)으로 자동 전환 | 진입/탈출 버그 → 간헐 끊김, latency |
| bisect | (이분 탐색) | 좋은 상태와 나쁜 상태 사이를 반씩 좁혀 원인 찾기 | 디버그 시간 단축의 핵심 |
| margin | 마진 | 신호가 실패 기준에서 얼마나 여유가 있나 (eye 폭·높이) | 온도·전압 corner에서 줄어듦 |

## 2. 핵심 개념 (정확한 버전) — 일반 디버그 방법

### 2.1 계층별 "OK 증거" 표 (bottom-up 체크리스트)

| 층 | OK라는 증거 | 어디서 | 막혔을 때 흔한 원인 |
|---|---|---|---|
| 전원·리셋 | rail 안정 후 **≥ 100 ms** 지나 PERST# 해제 (커널 상수 `PCIE_T_PVPERL_MS 100`) [10] | 스코프, PMIC 레지스터, GPIO 상태 | 리셋 GPIO 극성/DT 오류, rail 시퀀스 |
| refclk | 100 MHz, 지터 spec 이내, common clock vs SRIS 설정 일치 | 스코프/클럭 IC 레지스터 | 클럭 버퍼 미설정, SSC 불일치 |
| LTSSM | L0 도달, vendor 레지스터 상태값 | DWC: `PORT_LINK_DEBUG0` (0x728) [5:0] = LTSSM, `PORT_LINK_DEBUG1` (0x72C) bit 4 = link up, bit 29 = in training [4] / ZynqMP NWL: `PS_LINKUP_OFFSET` 0x238 bit0 link up, bit1 PHY ready [6] | Detect 실패(리셋·전원·레인), Polling 반복(SI·refclk) |
| 속도·폭 | `LnkSta` Speed/Width == 양쪽 `LnkCap` 중 작은 값 | `lspci -vvv`, sysfs `current_link_speed` | EQ 실패, 레인 불량 |
| DLL | root port `LnkSta` **DLActive+** (bit 13, `PCI_EXP_LNKSTA_DLLLA 0x2000`) [17] | `lspci -vvv` (root port 쪽) | FC init 실패 (드묾), 링크 flapping |
| 열거 | Vendor ID ≠ 0xFFFF, 링크 업 후 100 ms 대기 (`PCIE_RESET_CONFIG_WAIT_MS 100`) [10] | `lspci`, dmesg `pci 0000:01:00.0: [vvvv:dddd] type 00 class ...` | 너무 이른 접근, CRS 처리, 버스 번호 |
| 리소스 | dmesg `BAR n [mem ...]: assigned`, `/proc/iomem`에 장치 범위 | dmesg, `lspci -v` Region | 윈도우 부족·64-bit 미지원 윈도우 |
| 드라이버 | `lspci -k` "Kernel driver in use", probe 에러 없음 | dmesg | ID 매칭, DMA mask, 펌웨어 |
| 인터럽트 | `/proc/interrupts` 카운트 증가, `lspci` `MSI: Enable+` | procfs, lspci | Bus Master off, MSI 주소 매핑, ITS |
| DMA | 패턴 CRC 일치, SMMU fault 없음 | 테스트 드라이버, dmesg | 캐시 유지 누락, IOMMU 매핑, inbound 윈도우 |
| 성능 | 이론치 대비 % (예: Gen4 x4 ≈ 7.88 GB/s raw) | fio / DMA 벤치 | MPS/MRRS, ASPM, 다운그레이드, 작은 전송 |

### 2.2 bisect 손잡이 (한 번에 하나만)

| 손잡이 | 방법 | 무엇을 가르나 |
|---|---|---|
| 속도 낮추기 | root port `LnkCtl2` Target Link Speed = 1 (2.5 GT/s) 후 Retrain: `setpci -s <rp> CAP_EXP+0x30.w=0x1:0xf` → `setpci -s <rp> CAP_EXP+0x10.w=0x20:0x20` | Gen1에선 되면 → 고속 SI/EQ 문제. Gen1도 안 되면 → 전원·리셋·클럭·레인 |
| 폭 줄이기 | DT `num-lanes = <1>` (DWC), 컨트롤러 설정 | 특정 레인 불량 |
| ASPM off | 부팅 인자 `pcie_aspm=off` 또는 sysfs `link/l1_aspm` 등 | 저전력 진입/탈출 버그 |
| IOMMU 우회 | `iommu.passthrough=1` (Arm) | IOMMU 매핑/드라이버 DMA API 오용 |
| 교차 교환 | 카드 A/B × 슬롯 1/2 × 보드 X/Y × 호스트(SoC / x86) | 카드 vs 보드 vs SoC |
| SW 교체 | 커널 버전 bisect (`git bisect`), 다른 OS, bare-metal | SW 회귀 vs HW |
| 환경 | 온도 챔버 corner, 전압 margin (±5%), 다른 케이블/커넥터 | margin 문제 |
| 시간 | 리셋 후 대기 시간 늘리기, 링크 업 polling 연장 | 타이밍/레이스 |

- **bisect 규칙**: ① 실패율을 먼저 숫자로 (예 "1000번 중 3번") ② 손잡이를 바꾸고 **같은 횟수**만큼 돌려 비교 ③ 실패율이 낮으면 통계적으로 충분한 횟수 필요 (1/500 실패를 "0번 봤다"로 결론 내려면 수천 번 [추정: 95% 신뢰로 ~3×500 = 1500회 이상, "rule of three"]).

### 2.3 HW vs SW 증명 매트릭스

| 증거 | HW 쪽을 가리킴 | SW 쪽을 가리킴 |
|---|---|---|
| 다른 OS / bare-metal / 다른 호스트에서 | 똑같이 재현 | 재현 안 됨 |
| 애널라이저/인터포저 | 와이어에 TLP가 정상으로 나갔는데 응답이 틀리거나 없음, 또는 EQ/TS 교환 실패 | 요청 TLP가 와이어에 **아예 안 나감** (설정·주소·드라이버) |
| 스코프 | PERST#/refclk/rail이 spec 위반 | 신호는 spec 이내 |
| 카드/보드 교체 | 특정 개체만 실패 (양품/불량 구분) | 모든 개체 동일 실패 |
| 온도·전압 상관 | 강한 상관 (corner에서만) | 상관 없음 |
| 커널 bisect | 어떤 커널에서도 실패 | 특정 커밋부터 실패 |
| 레지스터 덤프 비교 (good vs bad) | 같은 설정인데 결과가 다름 | 설정값 자체가 다름 |

- 실무 문장: "Prove it with the smallest software stack possible." → Linux에서 재현 → 같은 시퀀스를 bare-metal로 → 그래도 재현되면 HW 쪽 (SI팀/보드팀/RTL팀)으로 **최소 재현 + 파형 + 레지스터 덤프**를 넘긴다.

### 2.4 회귀 테스트로 굳히기

```text
 원인 확정 ─► 재현 스크립트를 테스트로 승격 ─► 자동화 harness에 등록 ─► 기준값(threshold) 정의
             (N회 boot/reset 루프,           (전원 제어·시리얼 콘솔   (speed/width 정확히 일치,
              온도 corner 포함)              ·로그 자동 수집)          AER correctable < X / 시간,
                                                                    DLActive 유지)
```

- 테스트 1개 = **입력 조건 + 반복 횟수 + 판정 기준 + 실패 시 자동 수집 항목**(lspci -vvv, AER 카운터, vendor LTSSM 덤프, dmesg). 수집 스크립트 예시가 §3.1의 `pcie_snapshot.sh`.
- 판정 기준은 "에러 0"보다 **추세/임계값**: correctable은 정상 링크에서도 드물게 나올 수 있다 → "시간당 X 이하, 온도 상승과 무관".

### 2.5 실제 커널 로그 문자열 사전 (소스에서 확인한 것)

| 로그 (형식) | 찍는 곳 | 의미 | 시나리오 |
|---|---|---|---|
| `PCIe Gen.%u x%u link up` | DWC `dw_pcie_wait_for_link()` [3] | DesignWare 링크 업 성공 (속도·폭 포함) | S1, S2, S3 |
| `Phy link never came up` | DWC (v6.6 등 구버전) [3] | 재시도(`PCIE_LINK_WAIT_MAX_RETRIES 10` × 90 ms) 끝까지 링크 업 안 됨 [10] | S1 |
| `Link failed to come up. LTSSM: %s` / `Device not found` / `Device found, but not active` | DWC (최신 mainline) [3][21] | 실패 시 **LTSSM 상태 이름까지** 출력, Detect에 머무르면 "장치 없음"으로 구분 | S1 |
| `PHY link never came up` / `Link is UP` / `Link is DOWN` | Xilinx NWL (ZynqMP PS-PCIe) [6] | 같은 의미, ZynqMP 버전 | S1 |
| `Link Down` / `Correctable error message` | Xilinx AXI PCIe `pcie-xilinx.c` misc IRQ [7] | 런타임 링크 다운 / 에러 메시지 수신 | S7, S8 |
| `PCIe Bus Error: severity=Corrected, type=Physical Layer, (Receiver ID)` | AER v6.6 [2] | Receiver Error 등 물리층 correctable | S7 |
| `%s%s Error message%s from %04x:%02x:%02x.%d` → 예 `Correctable error message received from 0000:01:00.0` | AER mainline `aer_print_source()` [1] | root port가 ERR_COR 메시지 받음 | S7 |
| `[%2d] %-17s \| %-11s \| %-17s` → 예 `[ 0] RxErr \| Receiver \| Physical Layer` | AER mainline [1] | 비트별 에러 이름·agent·layer | S7 |
| `BAR %d: no space for %pR` / `BAR %d: failed to assign %pR` | setup-res.c v6.6 [8] | BAR 할당 실패 | S4 |
| `%s %pR: can't assign; no space` / `not assigned; can't enable device` | setup-res.c mainline [8] | 같은 의미 (최신 형식) | S4 |
| `%s: can't handle BAR above 4GB (bus address %#010llx)` | probe.c [22] | 64-bit 주소를 다룰 수 없는 환경 | S4 |
| `%u.%03u Gb/s available PCIe bandwidth, limited by %s x%d link at %s (capable of ...)` | pci.c `__pcie_print_link_status()` [9] | 경로 중 병목 링크 표시 (드라이버가 호출 시) | S2, S3, B2 |
| `broken device, retraining non-functional downstream link at 2.5GT/s` / `retraining failed` | quirks.c `pcie_failed_link_retrain()` [11] | 커널이 Gen1로 강제 재학습 시도 | S2 |
| `not ready %dms after %s; giving up` | pci.c `pci_dev_wait()` [9] | 리셋/resume 후 장치가 CRS만 계속 | S9 |
| `Unable to change power state from %s to D0, device inaccessible` | pci.c [9] | config read가 all-1s (장치 사라짐) | S9 |
| `Data Link Layer Link Active not set in %d msec` | pci.c `pci_bridge_wait_for_secondary_bus()` [9] | 리셋/resume 후 링크가 안 돌아옴 | S9 |
| `DPC: containment event, status:%#06x: unmasked uncorrectable error detected` | dpc.c [12] | DPC가 링크를 끊어 격리 | S8 |
| `Slot(%s): Link Down` / `Slot(%s): Card present` | pciehp [13] | hotplug 슬롯 링크 이벤트 | S8, B1 |
| `controller is down; will reset: CSTS=0x%x, PCI_STATUS=0x%hx` | nvme/host/pci.c [14] | NVMe 레지스터가 all-1s → 링크/장치 사라짐 | S8, S9 |
| `I/O tag %d (%04x) QID %d timeout, completion polled` | nvme/host/pci.c [14] | **완료는 와 있는데 인터럽트가 안 옴** | S5 |
| `irq %d: nobody cared (try booting with the "irqpoll" option)` | kernel/irq/spurious.c [15] | INTx가 계속 울리는데 아무도 처리 안 함 | S5 |
| `event: %s client: %s sid: %#x ssid: %#x iova: %#llx ...` (예 `F_TRANSLATION`) | arm-smmu-v3.c [16] | SMMU 변환 실패 (DMA 주소가 매핑 안 됨) | S6 |

## 3. Linux에서 볼 것 — 첫 5분 수집 + 시나리오 12선

### 3.1 첫 5분: 증거 스냅샷 (모든 시나리오 공통)

- 스크립트: `onsitePrep/lp15/pcie_snapshot.sh` — 읽기 전용, 실패마다 파일로 떠서 good/bad를 `diff`.

```bash
sudo ./pcie_snapshot.sh            > snap_bad_boot17_85C.txt
sudo ./pcie_snapshot.sh 0000:01:00.0 > snap_good.txt
diff snap_good.txt snap_bad_boot17_85C.txt
```

- 담는 것: `lspci -tv`(토폴로지) · `lspci -nnk`(ID·드라이버) · sysfs `current_link_speed/width`, `max_link_*`, `power_state`, `aer_dev_*` · `lspci -vvv`의 Control/Status/Region/LnkCap/LnkSta/MSI/UESta/CESta 줄 · dmesg(없으면 journal) · `/proc/interrupts` · `/proc/iomem`.
- **실제 출력 (Lima VM, kernel 7.0.0-38-generic, Apple VZ 가상 PCI)** — 일부 발췌, 해석:

```text
===== /proc/interrupts (PCI-MSI / ITS / pcie lines) =====
 29:     265140   0 ... GICv2m-PCI-MSIX-0000:00:06.0   1 Edge      virtio2-req.0
 32:          0   0 ... GICv2m-PCI-MSIX-0000:00:01.0   0 Edge      virtio0-config
 33:       2134   0 ... GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
===== /proc/iomem (PCI windows) =====
40000000-4fffffff : PCI ECAM
50000000-6ffdffff : PCI Bus 0000:00
380000000-77fffffff : PCI Bus 0000:00
    380000000-38000ffff : virtio-pci-modern
```

| 줄 | 해석 |
|---|---|
| `GICv2m-PCI-MSIX-0000:00:06.0 1 ... virtio2-req.0` + 카운트 265140 | 이 장치의 MSI-X vector 1이 Arm **GICv2m** MSI 프레임으로 전달되고 있고 실제로 많이 울렸다 → S5에서 "카운트가 0에 머무나"를 보는 바로 그 줄 |
| `virtio0-config` 카운트 0 | config 변경 인터럽트는 원래 거의 안 옴 → **0이 항상 문제는 아님** (어떤 vector가 와야 하는지 알아야 함) |
| `40000000-4fffffff : PCI ECAM` | config space가 메모리에 매핑된 창 (256 MiB = 256 bus × 1 MiB) → bare-metal에서 직접 읽는 곳 ([LP16](2026-10-31_LP16_baremetal_vs_linux_zynq.html)) |
| `50000000-6ffdffff`, `380000000-77fffffff : PCI Bus 0000:00` | host bridge의 **32-bit / 64-bit 메모리 윈도우** → S4의 "no space"는 BAR가 이 안에 못 들어갈 때 |
| journal: `pci 0000:00:01.0: [1af4:1041] type 00 class 0x020000 conventional PCI endpoint` | 열거 로그. 이 VM은 PCIe capability가 없는 **conventional PCI** 가상 장치라 LnkSta 줄이 없음 → 실제 링크 필드는 QEMU `pcie-root-port` 랩이나 실보드에서 |

### S1 · 장치가 `lspci`에 없음

**증상** — 보드를 켰는데 NVMe/NPU/FPGA가 목록에 없다. root port만 보인다.

```text
[예시 출력]
$ lspci -tv
-[0000:00]---00.0-[01-ff]--          ← root port 아래(bus 01)가 비어 있음
$ dmesg | grep -i pcie
dw-pcie 3c0800000.pcie: Phy link never came up           (구버전 DWC)
dw-pcie 3c0800000.pcie: Link failed to come up. LTSSM: DETECT_QUIET   (최신 DWC)
$ sudo lspci -s 00:00.0 -vvv | grep -A2 LnkSta:
        LnkSta: Speed 2.5GT/s, Width x0
                TrErr- Train- SlotClk+ DLActive- BWMgmt- ABWMgmt-
```

**처음 보는 사람을 위한 배경** — 열거(enumeration) = config read 성공이다. config 요청은 링크가 L0 + DL_Active가 된 다음에만 흐른다. 그래서 링크가 안 뜨면 endpoint는 **"읽을 방법이 없어서"** 안 보인다. root port는 SoC 안쪽이라 항상 보이므로, **root port의 LnkSta와 컨트롤러의 LTSSM 레지스터**가 첫 증거다 ([N01 §4](2026-10-31_N01_pcie_lspci_ltssm.html)).

**볼 정보**
- `lspci -tv` → 어느 root port 아래인지. `sudo lspci -s <rp> -vvv` → `LnkSta` Width(x0?), `DLActive`, `Train`, 슬롯이면 `SltSta PresDet`.
- dmesg: 컨트롤러 드라이버의 link up/down 줄 (§2.5).
- vendor LTSSM: DWC면 `PORT_LINK_DEBUG0` 하위 6비트 [4], 최신 커널은 debugfs `/sys/kernel/debug/dwc_pcie_<dev>/ltssm_status` [5]. ZynqMP는 `PS_LINKUP_OFFSET` 0x238 [6].
- 스코프: PERST# 해제 시점(전원 안정 후 ≥ 100 ms), refclk 100 MHz 존재, 카드 전원.

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | PERST#가 안 풀림 / 극성·GPIO DT 오류 | 스코프로 PERST#, DT `reset-gpios` 극성 확인 | LTSSM이 Detect.Quiet에 고정, PERST# 계속 low |
| 2 | refclk 없음/잘못된 모드 | 스코프, 클럭 IC 설정, common vs SRIS | Detect는 통과했는데 Polling↔Detect 반복 |
| 3 | 장치 전원/카드 미장착/레인 단선 | 전원 rail, 다른 카드, 연속성 측정 | Detect에서 receiver 감지 실패 (x0) |
| 4 | 너무 이른 config 접근 / CRS 처리 | 링크 업 후 100 ms 대기 확인, 장치 FW 부팅 시간 | 링크는 업(DLActive+)인데 vendor ID 0xFFFF → 나중에 rescan하면 보임 |
| 5 | 링크가 부팅 후 늦게 뜸 | `echo 1 > /sys/bus/pci/rescan` | rescan 후 나타남 |
| 6 | 컨트롤러 설정 (lane 수, 모드 RC/EP, PHY init) | DT `num-lanes`, PHY 드라이버 로그 | PHY ready 비트 0 (NWL bit1) |

**Bisect**
1. 다른 카드(알려진 양품, 예: 상용 NVMe)를 같은 슬롯에 → 보이면 원래 카드 문제.
2. 문제 카드를 x86 PC에 꽂기 → 보이면 SoC/보드 쪽.
3. Gen1 강제(DT `max-link-speed = <1>`) → 뜨면 SI/EQ 문제로 이동(S2).
4. PERST# 해제 지연을 늘리기(예 100 → 300 ms) → 뜨면 타이밍/장치 준비 시간.

**결론·회귀 테스트** — 원인이 PERST# 타이밍이었다면: "cold boot 1000회 + warm reset 1000회, 매번 `DLActive+` + 기대 speed/width + vendor ID 확인, 실패 시 LTSSM 덤프 자동 수집" 을 harness에 추가. PERST# 타이밍은 스코프 캡처를 bring-up 체크리스트에 고정.

<details><summary>🎤 English 2-minute answer</summary>

"If the endpoint doesn't show up in lspci, the first thing I remember is that enumeration is just config reads, and config reads only flow after the link reaches L0 and the data link layer is active. So a missing endpoint almost always means the link never came up, or software looked too early. I start at the root port, which is always visible because it lives inside the SoC: lspci -vvv on the root port, and I look at Link Status — width x0 and DLActive minus tells me the link isn't up. Then I read the controller's LTSSM state, for a DesignWare core that's the port-logic debug register or the ltssm_status debugfs file, and the kernel log line — 'Phy link never came up' or, on newer kernels, 'Link failed to come up' with the LTSSM state. If it's stuck in Detect, the other side's receiver isn't seen — that's power, PERST#, or a lane/connector issue, so I go to the scope: PERST# released at least 100 ms after power is stable, refclk present at 100 MHz. If it's bouncing between Polling and Detect, I suspect refclk or signal integrity, and I force Gen1 to see if it trains. If the link is up but the vendor ID reads all ones, the device firmware may not be ready — I check the 100 ms post-training wait and CRS handling, and try a rescan. To split hardware from software I swap in a known-good card and put the suspect card in an x86 host. Once I find it, I add a boot-loop regression that checks DLActive, speed and width every cycle and dumps the LTSSM state on failure."

</details>

<details><summary>🔁 꼬리 질문 체인</summary>

- **Q1. What's the first thing you check?** — "The root port's Link Status and the controller's LTSSM state, because the root port is always visible and tells me how far training got." · 한국어: root port LnkSta + vendor LTSSM 레지스터.
- **Q2. LnkSta shows DLActive+ and Gen3 x4. Still no endpoint. What next?** — "Then the link is fine and it's a config-layer problem: the device returned CRS or wasn't ready when we scanned, or the bus numbers weren't programmed. I'd check how long after link-up the first config read happened, enable CRS software visibility, and do a remove and rescan. I'd also read the endpoint's config space directly through ECAM to rule out the enumeration code." · 한국어: 링크 OK면 config 층 — CRS/대기 시간/rescan/버스 번호.
- **Q3. How do you prove it's hardware and not software?** — "Same card in an x86 host, and the same board with a known-good card. If the link doesn't train with any software, including a bare-metal loop that just releases PERST# and polls the link-up bit, it's hardware. Then the scope on PERST# and refclk, and an interposer to see whether any TS1 ordered sets are exchanged." · 한국어: 교차 교환 + bare-metal 최소 루프 + 스코프/인터포저.
- **Q4. How would you prevent it from coming back?** — "A boot and reset loop in the regression farm — a thousand cycles, checking DLActive, speed and width each time, with automatic LTSSM and lspci dumps on failure — plus the PERST# timing measurement in the bring-up checklist for every new board spin." · 한국어: N회 부팅 루프 + 실패 시 자동 덤프 + 보드 스핀마다 타이밍 측정.

</details>

### S2 · 기대 Gen4/Gen5인데 Gen1/Gen3로만 학습

**증상** — 동작은 하는데 느리다. `LnkSta`에 `(downgraded)`.

```text
[예시 출력]
        LnkCap: Port #0, Speed 16GT/s, Width x4, ASPM L1, Exit Latency L1 <64us
        LnkSta: Speed 8GT/s (downgraded), Width x4
                TrErr- Train- SlotClk+ DLActive+ BWMgmt+ ABWMgmt-
        LnkCtl2: Target Link Speed: 16GT/s, EnterCompliance- SpeedDis-
        LnkSta2: Current De-emphasis Level: -3.5dB, EqualizationComplete+ EqualizationPhase1+
                 EqualizationPhase2- EqualizationPhase3- LinkEqualizationRequest+
                 Retimer- 2Retimers- CrosslinkRes: unsupported, FltMode-
$ dmesg | grep bandwidth
nvme 0000:01:00.0: 31.504 Gb/s available PCIe bandwidth, limited by 8.0 GT/s PCIe x4 link at 0000:00:00.0 (capable of 63.012 Gb/s with 16.0 GT/s PCIe x4 link)
```

**처음 보는 사람을 위한 배경** — 링크는 항상 Gen1(2.5 GT/s)으로 먼저 L0에 간 다음, **Recovery** 상태를 거쳐 속도를 올린다. Gen3(8 GT/s) 이상은 그 과정에서 **equalization(EQ)** — 송신 파형 preset을 바꿔 가며 받는 쪽 눈을 여는 협상 — 이 성공해야 한다. EQ가 실패하거나 에러가 많으면 낮은 속도로 돌아가 머문다 ([LP03](2026-10-31_LP03_link_training_ltssm.html)).

**볼 정보**
- 양쪽(root port + endpoint) `LnkCap` vs `LnkSta` — 기대값 = 둘 중 작은 LnkCap. `LnkCtl2` Target Link Speed가 낮게 설정돼 있지 않은지.
- `LnkSta2` EqualizationPhase1/2/3, `LinkEqualizationRequest+` (재-EQ 요청) [18].
- `BWMgmt+`/`ABWMgmt+` → 런타임에 속도가 **바뀌었다**는 흔적 (bit 14/15) [17].
- 16 GT/s 이상: Physical Layer 16 GT/s extended cap(ID 0x26), Lane Margining(ID 0x27) — `pcilmr`로 레인별 margin [20].
- AER correctable `RxErr`, `BadTLP`, `BadDLLP` 카운트.
- dmesg `limited by ...` 줄 [9], quirk `retraining ... at 2.5GT/s` [11].

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | SI/EQ 실패 (채널 손실, 커넥터, 비아) | LnkSta2 Phase 비트, pcilmr margin, 다른 보드/짧은 채널 | Phase2/3 `-`, margin 작음, 특정 레인만 나쁨 |
| 2 | SW가 목표 속도를 낮게 설정 | `LnkCtl2` Target Link Speed, DT `max-link-speed` | Target = 8GT/s 로 되어 있음 |
| 3 | 상대 장치/중간 스위치·retimer 능력 | 경로 위 모든 포트 LnkCap (`lspci -tv`로 경로) | 중간 링크 LnkCap이 낮음 → dmesg "limited by ... at <중간 포트>" |
| 4 | EQ preset 설정 (컨트롤러 기본값) | vendor EQ preset 레지스터, preset sweep | 특정 preset에서만 Gen4 성공 |
| 5 | refclk 지터 / SSC 불일치 | 스코프, 클럭 설정 | 고속에서만 에러 |
| 6 | 커널 quirk가 Gen1로 묶음 | dmesg quirk 문자열 | `broken device, retraining ... at 2.5GT/s` |

**Bisect**
1. Target Link Speed를 Gen2 → Gen3 → Gen4 순으로 설정하고 각각 Retrain → **어디서 깨지는지** 기록.
2. 레인별로 보기: pcilmr `-l 0,1,2,3` → 특정 레인만 margin 낮으면 그 레인 경로(PCB/커넥터).
3. 같은 장치를 짧은 채널(다른 보드/슬롯)에 → 뜨면 채널 손실. 다른 장치를 같은 슬롯에 → 장치 PHY 문제 분리.
4. EQ preset sweep (지원 시) → preset별 성공률 표.

**결론·회귀 테스트** — "speed/width 매트릭스 테스트 (Gen1–GenN × x1/x2/x4, 매번 Retrain 후 LnkSta 확인) + PVT corner에서 pcilmr margin 기록, 기준 이하 레인 알람"을 추가 ([LP14](2026-10-31_LP14_validation_methodology.html)).

<details><summary>🎤 English 2-minute answer</summary>

"First I confirm what 'expected' means: the negotiated speed should be the lower of the two ports' Link Capabilities, and Link Control 2's target speed must not be capping it. lspci shows 'downgraded' in Link Status, and the kernel's bandwidth message tells me which link in the path is the bottleneck — it might be a switch or retimer in the middle, not the endpoint. If the hardware is capable, the usual culprit is equalization. Every link trains at 2.5 GT/s first and then goes through Recovery to change speed, and from 8 GT/s up that includes the equalization phases. Link Status 2 tells me which phases succeeded and whether a re-equalization was requested. Then I quantify the margin: at 16 GT/s and above every port must support Lane Margining at the Receiver, so I run pcilmr per lane and see if one lane is much worse — that points to a specific trace or connector pin. I bisect by stepping the target speed up one generation at a time and retraining, by moving the device to a shorter channel, and by sweeping TX presets if the controller allows it. AER correctable counts — receiver errors, bad TLPs — should correlate. Once fixed, the regression is a speed-by-width matrix with retrains, plus margining at temperature and voltage corners with a threshold."

</details>

<details><summary>🔁 꼬리 질문 체인</summary>

- **Q1. First thing you check?** — "Both sides' LnkCap against LnkSta, and LnkCtl2's target speed — make sure it's really a downgrade and not a cap." · 한국어: 양쪽 LnkCap·LnkSta·LnkCtl2 먼저.
- **Q2. Target speed is right and both sides are Gen4-capable. What next?** — "LnkSta2 equalization phase bits, then per-lane margining with pcilmr, and AER receiver errors. If phase 2 or 3 failed, equalization couldn't find coefficients that open the eye." · 한국어: EQ phase 비트 → 레인별 margin → AER.
- **Q3. HW or SW?** — "Software only sets the target and kicks a retrain; the PHYs negotiate. If I set Gen4, retrain, and the link still lands at Gen3 with failed EQ phases on any OS, it's the channel or PHY. A shorter channel or a different board that trains at Gen4 confirms it." · 한국어: SW는 목표만 정함, 협상은 PHY → 채널 교체로 확인.
- **Q4. Prevent recurrence?** — "Speed/width matrix in the regression, margining at PVT corners with pass thresholds, and an alarm if the negotiated speed ever differs from expected at boot." · 한국어: 매트릭스 + 코너 margining + 부팅 시 속도 불일치 알람.
- **Q5 (추가). The link trains at Gen4 but drops to Gen3 after an hour. How do you see that?** — "BWMgmt or ABWMgmt set in Link Status, the bandwidth-notification interrupt if enabled, and periodic sampling of current_link_speed in sysfs. Then correlate with temperature." · 한국어: BWMgmt/ABWMgmt + sysfs 주기 샘플링 + 온도 상관.

</details>

### S3 · x4 대신 x2/x1로 학습

**증상** — 속도는 맞는데 폭이 줄었다.

```text
[예시 출력]
        LnkCap: Port #0, Speed 8GT/s, Width x4, ASPM L1
        LnkSta: Speed 8GT/s, Width x2 (downgraded)
                TrErr- Train- SlotClk+ DLActive+ BWMgmt- ABWMgmt-
$ cat /sys/bus/pci/devices/0000:01:00.0/current_link_width
2
dw-pcie 3c0800000.pcie: PCIe Gen.3 x2 link up
```

**처음 보는 사람을 위한 배경** — 폭(width)은 LTSSM **Configuration** 단계에서 정해진다. 양쪽이 레인마다 TS1/TS2를 주고받고, **응답이 있는 레인들로만** 링크를 만든다. 레인 하나가 죽으면 x4 → x2 또는 x1로 "살 수 있는 만큼" 줄인다. 레인 번호를 거꾸로 연결한 보드는 **lane reversal**로 맞추고, 각 레인의 +/− 뒤집힘은 **polarity inversion**으로 자동 보정된다.

**볼 정보**
- `LnkSta` Width, sysfs `current_link_width` / `max_link_width`.
- vendor 레지스터의 레인별 상태 (lane detect, 레인별 에러 카운터 — DWC RAS DES event counter `rasdes_event_counter/*/lane_select` [5]).
- DT `num-lanes`, 보드 회로도의 레인 매핑, AC coupling 커패시터.
- pcilmr로 레인별 margin (Gen4+).

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 레인 단선/커패시터 누락/커넥터 핀 | 레인별 Detect 상태, 연속성 측정, X-ray | 항상 같은 레인이 빠짐 |
| 2 | DT/컨트롤러 lane 설정 (`num-lanes`) | DT 확인, 컨트롤러 레지스터 | 설정이 x2로 되어 있음 |
| 3 | lane reversal 미지원/설정 오류 | 회로도 레인 순서 vs 컨트롤러 reversal 설정 | x1으로만 학습 (레인 0만 맞음) |
| 4 | 특정 레인 SI 불량 (margin 부족) | 레인별 margin/에러 카운터, 온도 상관 | 가끔 x4, 가끔 x2 |
| 5 | 상대 장치가 x2 장치 | 상대 LnkCap | LnkCap Width x2 |

**Bisect**
1. 상대 LnkCap 먼저 (가장 싼 확인).
2. 다른 카드/다른 슬롯 → 빠지는 레인이 **보드를 따라가나, 카드를 따라가나**.
3. 반복 부팅으로 빠지는 레인이 고정인지 랜덤인지 → 고정 = 물리 결함, 랜덤 = margin.

**결론·회귀 테스트** — 부팅 루프에서 width를 매번 기록, 레인별 에러 카운터 수집. 보드 제조 테스트에 "x4 학습 확인" 추가.

<details><summary>🎤 English 2-minute answer</summary>

"Width is decided in the Configuration state of the LTSSM: both sides exchange training sets on every lane and form the link out of the lanes that respond, so a dead lane makes the link fall back to x2 or x1 instead of failing. First I check the partner's Link Capabilities — maybe it really is an x2 device — and the controller's configured lane count in the device tree. Then I look at per-lane status in the controller's debug registers and per-lane error counters, and with Gen4 and up I can margin each lane. The key bisect is whether the missing lane follows the board or the card: swap cards and slots. If the same lane is missing every boot, it's physical — an open trace, a missing AC-coupling capacitor, a bent connector pin — and continuity measurement or the schematic lane map confirms it. If it's random, it's marginal signal integrity and I'd correlate with temperature. If it trains only as x1, I check lane reversal, because a reversed layout without reversal support leaves only lane 0 matching. The regression is recording width on every boot in the loop and adding an x4 check to board manufacturing test."

</details>

<details><summary>🔁 꼬리 질문 체인</summary>

- **Q1. First check?** — "The partner's LnkCap and our configured num-lanes — the cheap explanations." · 한국어: 상대 능력·DT 설정부터.
- **Q2. Both say x4. Next?** — "Per-lane detect and error status from the controller, then swap card and slot to see whether the missing lane follows the board or the card." · 한국어: 레인별 상태 → 보드/카드 중 누구를 따라가나.
- **Q3. HW vs SW?** — "Software can't remove a lane except through configuration. If config says x4 and the same physical lane is missing across software versions, continuity or X-ray on that lane settles it." · 한국어: 설정이 x4인데 같은 레인이 빠지면 물리.
- **Q4. Prevent?** — "Width check in every boot-loop iteration, plus a manufacturing test that fails boards that train below x4." · 한국어: 부팅 루프 width 체크 + 제조 테스트.

</details>

### S4 · 보이는데 드라이버 probe 실패 — `BAR ... no space`

**증상** — `lspci`에는 보이는데 드라이버가 안 붙거나 probe가 실패한다.

```text
[예시 출력]  (커널 6.6 형식 / 최신 형식)
pci 0000:01:00.0: BAR 0: no space for [mem size 0x10000000 64bit pref]
pci 0000:01:00.0: BAR 0: failed to assign [mem size 0x10000000 64bit pref]
pci 0000:01:00.0: BAR 0 [mem size 0x10000000 64bit pref]: can't assign; no space
pci 0000:01:00.0: BAR 0 [mem size 0x10000000 64bit pref]: failed to assign
accel 0000:01:00.0: BAR 0 [mem size 0x10000000 64bit pref]: not assigned; can't enable device
$ sudo lspci -s 01:00.0 -v | grep Region
        Region 0: Memory at <unassigned> (64-bit, prefetchable) [size=256M]
```

**처음 보는 사람을 위한 배경** — BAR는 장치가 "내 레지스터/메모리 창을 이만큼 주소에 매핑해 달라"고 요청하는 칸이다. 커널은 BAR에 all-1s를 써서 크기를 재고(sizing), **host bridge가 가진 주소 창(window)** 에서 크기만큼(정렬 포함) 잘라 준다. 중간 bridge도 자기 아래 장치들의 BAR를 모두 덮는 window를 가져야 한다. 창이 작거나 64-bit 창이 없으면 "no space" ([LP04](2026-10-31_LP04_enumeration_resources_hotplug.html)).

**볼 정보**
- dmesg 열거 구간 전체: `pci_bus 0000:00: root bus resource [mem ... window]` 줄 (host bridge 창), `BAR n [mem size ...]`, `bridge window [...]`.
- `/proc/iomem` (§3.1 실제 출력처럼 `PCI Bus 0000:00` 범위).
- DT host bridge `ranges` (32-bit non-pref, 64-bit pref 창), ACPI `_CRS`.
- 장치 BAR 크기 (`lspci -v` `[size=256M]`), 64-bit/prefetchable 여부.

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | host bridge 창 자체가 작음 (DT `ranges`) | root bus resource 줄 vs BAR 크기 합 | 창 128 MiB < BAR 256 MiB |
| 2 | 64-bit prefetchable 창 없음 → 큰 64-bit BAR가 32-bit 창에 들어가야 함 | `ranges`에 64-bit pref 항목 유무 | 32-bit 창만 존재 |
| 3 | 펌웨어(부트로더)가 할당한 bridge window를 커널이 존중 → 부족 | `pci=realloc` 또는 `pci=assign-busses` 실험 | realloc 후 성공 |
| 4 | 정렬 낭비 (큰 BAR는 자기 크기로 정렬) | 할당 순서/주소 확인 | 합은 맞는데 정렬 때문에 안 들어감 |
| 5 | inbound/outbound 주소 변환(iATU) 창 설정과 불일치 | 컨트롤러 iATU 레지스터 ([LP05](2026-10-31_LP05_soc_host_controller_integration.html)) | 할당은 됐는데 MMIO 접근 시 abort |
| 6 | 4 GB 이상 주소 처리 불가 환경 | `can't handle BAR above 4GB` [22] | 해당 문자열 |

**Bisect**
1. BAR 크기 줄이기 (FPGA endpoint면 비트스트림에서 BAR 크기 축소, 또는 resizable BAR 축소) → 붙으면 창 크기 문제 확정.
2. DT `ranges`에 64-bit prefetchable 창 추가/확대.
3. `pci=realloc` 부팅 인자 → 커널이 bridge window를 다시 계산.
4. 장치를 하나씩 빼서 어떤 조합에서 넘치는지.

**결론·회귀 테스트** — "최대 BAR 구성(가장 큰 BAR를 가진 카드 + 스위치 뒤 다수 장치)으로 열거 테스트, dmesg에서 `no space`/`failed to assign` grep → 실패" 를 CI에 추가. DT 리뷰 체크리스트에 창 크기 근거 기록.

<details><summary>🎤 English 2-minute answer</summary>

"If the device shows up but the driver can't enable it, the link and enumeration are fine — this is resource assignment. The kernel sizes each BAR by writing all ones and reading back, then carves an aligned range out of the host bridge's memory windows, and every bridge on the path needs a window that covers its children. The log tells me exactly which BAR failed and its size and type — 'BAR 0, 256 megabytes, 64-bit prefetchable, can't assign, no space'. So I compare that against the root bus resource lines at the top of enumeration, which come from the device tree ranges property on an Arm SoC. The classic causes are a window that's simply too small, a missing 64-bit prefetchable window so a big BAR has to squeeze into the 32-bit space below 4 GB, firmware-assigned bridge windows that are too small, or alignment waste because a 256 MB BAR must sit on a 256 MB boundary. To prove it, I shrink the BAR — easy with an FPGA endpoint — or enlarge the window in the DT, or boot with pci=realloc. One subtle follow-up: even if assignment succeeds, the controller's outbound address translation has to match those windows, otherwise the MMIO access aborts. The regression is a maximal-BAR enumeration test that fails on any 'no space' line."

</details>

<details><summary>🔁 꼬리 질문 체인</summary>

- **Q1. First thing?** — "The exact BAR, size and type from the log, versus the root bus resource windows." · 한국어: 실패한 BAR 크기·타입 vs host bridge 창.
- **Q2. Window looks big enough. Next?** — "Alignment and window type: a 64-bit prefetchable BAR needs a prefetchable window, and natural alignment can waste space. Also bridge windows assigned by firmware. Try pci=realloc." · 한국어: 정렬·창 타입·펌웨어 할당 → `pci=realloc`.
- **Q3. HW or SW?** — "This one is almost always configuration — DT ranges, firmware, or controller translation windows. The hardware fact that matters is the BAR size the device requests, which I can read directly." · 한국어: 거의 항상 설정 문제. HW 사실은 BAR 크기뿐.
- **Q4. Prevent?** — "Enumeration test with the largest supported BAR configuration in CI, failing on any 'no space' or 'failed to assign' line, and a documented address map for the SoC." · 한국어: 최대 BAR 구성 CI + 주소 맵 문서화.

</details>

### S5 · MSI가 안 옴 (인터럽트 timeout)

**증상** — 장치는 일을 끝냈는데 드라이버가 모른다. timeout 후 polling으로 완료를 발견.

```text
[예시 출력]
nvme nvme0: I/O tag 12 (100c) QID 1 timeout, completion polled
$ grep nvme /proc/interrupts
 77:          0          0   ITS-PCI-MSIX-0000:01:00.0   1 Edge      nvme0q1
$ sudo lspci -s 01:00.0 -vvv | grep -E 'Control:|MSI'
        Control: I/O- Mem+ BusMaster- SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+
        Capabilities: [b0] MSI-X: Enable+ Count=33 Masked-
```

**처음 보는 사람을 위한 배경** — MSI/MSI-X는 선(wire)이 아니라 **장치가 정해진 주소에 정해진 값을 메모리 쓰기(posted write)** 하는 것이다. Arm에서는 그 주소가 보통 **GIC ITS**(Interrupt Translation Service)의 doorbell 레지스터이고, ITS가 (DeviceID, EventID)를 CPU 인터럽트로 바꾼다. 따라서 ① 장치가 **Bus Master**(자기가 메모리 쓰기를 시작할 권한)를 가져야 하고 ② 그 쓰기가 SoC 안에서 ITS까지 **라우팅**돼야 하고 ③ IOMMU가 그 주소를 막지 않아야 한다 ([LP07](2026-10-31_LP07_interrupts_intx_msi_msix.html)). NVMe의 `completion polled` 메시지는 "완료 큐에는 결과가 있는데 인터럽트가 안 왔다"는 거의 확정 증거 [14].

**볼 정보**
- `lspci -vvv`: Command `BusMaster+`?, `MSI: Enable+` / `MSI-X: Enable+ Masked-`, MSI Address/Data 값.
- `/proc/interrupts`: 해당 vector 카운트가 0에 머무나, 어느 irqchip(`ITS-PCI-MSIX`, `GICv2m`, 컨트롤러 내장 MSI)인가.
- sysfs `msi_irqs/`, `irq`.
- DT: host controller의 `msi-map` / `msi-parent`, ITS 노드 ([LP05](2026-10-31_LP05_soc_host_controller_integration.html)).
- SMMU fault 로그 (MSI 주소가 IOMMU를 통과해야 하는 구성).
- 장치 쪽: MSI-X table/PBA (Pending Bit Array — "보내려 했는데 마스크돼서 대기 중" 비트).

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | Bus Master off | lspci Control | `BusMaster-` (드라이버가 `pci_set_master()` 누락) |
| 2 | MSI 주소가 ITS/MSI 프레임으로 안 감 (DT `msi-map` RID→DeviceID 매핑 오류, iATU/inbound 창) | DT, ITS 테이블, 컨트롤러 inbound 설정 | 카운트 0, 장치 PBA에 pending 없음 |
| 3 | 마스크 상태 | MSI-X `Masked+`, 벡터별 mask, PBA pending | PBA bit 세트 |
| 4 | IOMMU가 MSI 주소 차단 | SMMU fault 로그 | `F_TRANSLATION` 등 이벤트 + MSI 주소 |
| 5 | 드라이버가 다른 vector를 기다림 (vector↔queue 매핑) | 드라이버 로그, 카운트가 엉뚱한 vector에서 증가 | 다른 IRQ 번호만 증가 |
| 6 | INTx로 폴백했는데 INTx 라우팅/`DisINTx` 문제 | `irq %d: nobody cared` [15] | INTx 폭주 또는 무반응 |

**Bisect**
1. 드라이버를 polling 모드로 (예: XDMA `poll_mode=1`) → 데이터는 정상인지 → 인터럽트 경로만 문제임을 확정.
2. MSI-X → MSI → INTx로 바꿔 보기 (`pci=nomsi` 부팅 인자) → INTx는 되면 MSI 주소/ITS 경로.
3. 장치 없이 **SW로 doorbell에 직접 쓰기** (CPU가 ITS 주소에 같은 data를 쓰기) → 인터럽트가 뜨면 ITS 설정 OK, 장치→SoC 경로 문제.
4. 애널라이저로 와이어에 MSI MWr TLP(주소·data)가 나가는지.

**결론·회귀 테스트** — "모든 vector에 대해 장치가 MSI를 발생 → `/proc/interrupts` 카운트 증가 확인, 누락 0" 테스트. endpoint framework의 MSI/MSI-X 테스트(`pci_endpoint_test`)로 SoC 대 SoC 자동화 가능 ([LP16](2026-10-31_LP16_baremetal_vs_linux_zynq.html)). 이 원리는 LP16 C 코드 테스트 [4] ("Bus Master 없으면 MSI 없음")로 직접 확인 가능.

<details><summary>🎤 English 2-minute answer</summary>

"An MSI is just a posted memory write from the device to a doorbell address, so I check the three things that write needs. One, the device must be allowed to master the bus — the Command register's Bus Master bit; a driver that forgets pci_set_master gives you exactly this. Two, MSI must be enabled and unmasked with a sane address and data — lspci shows the MSI or MSI-X capability state, and for MSI-X I check the per-vector mask and the pending bit array, which tells me the device wanted to send but was masked. Three, the write must reach the interrupt controller: on Arm that's usually the GIC ITS, so the device tree msi-map from requester ID to device ID, the ITS tables, the controller's inbound windows, and the SMMU all have to agree. A strong symptom is NVMe's 'timeout, completion polled' — the completion is in memory but no interrupt came — so data is fine and only the interrupt path is broken. To bisect, I switch the driver to polling, fall back from MSI-X to INTx, and I have the CPU write the same data to the ITS doorbell directly; if that interrupt fires, the GIC side is fine and the problem is between the device and the SoC. An analyzer shows whether the MSI write TLP goes out at all. The regression fires every vector and checks every count increments."

</details>

<details><summary>🔁 꼬리 질문 체인</summary>

- **Q1. First check?** — "Bus Master and MSI enable/mask in lspci, and whether the count in /proc/interrupts moves at all." · 한국어: BusMaster·MSI Enable/Mask·카운트.
- **Q2. All enabled, count still zero. Next?** — "Check the pending bit array on the device — if pending, it's masked on the device side; if not, follow the write: msi-map in the DT, ITS device table, controller inbound window, SMMU faults. Then trigger the doorbell from the CPU to split device side from GIC side." · 한국어: PBA → 라우팅(DT msi-map/ITS/inbound/SMMU) → CPU가 doorbell 직접 쓰기.
- **Q3. HW or SW?** — "An analyzer or the controller's TLP trace: if the device puts the MWr on the wire with the right address and data and nothing fires, it's SoC-side — routing or ITS config, which may still be software. If no MWr goes out, it's the device or its enable state." · 한국어: 와이어에 MWr가 나갔나로 장치 쪽/SoC 쪽 분리.
- **Q4. Prevent?** — "An interrupt test that fires every vector and checks every count, run on every kernel and DT change, plus a driver review checklist item for pci_set_master and vector-to-queue mapping." · 한국어: 전 vector 테스트 + 리뷰 체크리스트.

</details>

### S6 · DMA 데이터가 깨짐 (x86에선 OK, Arm SoC에서만)

**증상** — 같은 카드·같은 드라이버가 x86 PC에서는 정상, 새 Arm SoC에서는 가끔 데이터가 틀리거나 오래된 값이 읽힌다.

```text
[예시 출력]
dma_test: buffer mismatch at offset 0x1f40: expected 0xa5a5a5a5 got 0x00000000
arm-smmu-v3 fd800000.iommu: event: F_TRANSLATION client: 0000:01:00.0 sid: 0x100 ssid: 0x0 iova: 0xffff0000 ipa: 0x0
```

**처음 보는 사람을 위한 배경** — DMA는 장치가 RAM을 직접 읽고 쓰는 것인데, CPU는 RAM 앞에 **캐시**를 둔다. x86은 PCIe DMA가 하드웨어적으로 캐시와 일관(coherent)하게 동작하는 경우가 일반적이지만, Arm SoC는 PCIe가 **non-coherent**로 붙는 경우가 많아 SW가 캐시를 비우거나(clean) 무효화(invalidate)해야 한다. Linux에서는 DMA API(`dma_map_single`, `dma_sync_*`)가 이걸 해 준다 — 드라이버가 API를 건너뛰면 Arm에서만 깨진다. 또 IOMMU(SMMU)가 장치 주소(IOVA)를 변환하므로 매핑이 없으면 fault ([LP08](2026-10-31_LP08_dma_iommu_coherency.html)).

**볼 정보**
- 깨지는 패턴: 0 / 오래된 값(stale) / 캐시라인(64 B) 단위 / 4 KiB 페이지 경계?
- SMMU 이벤트 로그 (`F_TRANSLATION`, sid, iova) [16].
- DT `dma-coherent` 속성 유무 (host controller 노드), `dma-ranges` (inbound 창).
- 드라이버의 DMA mask (`dma_set_mask_and_coherent(…, DMA_BIT_MASK(64))` 실패 여부).
- MPS/MRRS(`DevCtl: MaxPayload/MaxReadReq`), Relaxed Ordering 설정.

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 캐시 유지 누락 (드라이버가 DMA API 우회, 또는 `dma-coherent` DT가 실제 HW와 불일치) | 버퍼를 uncached로 할당해 비교, `dma-coherent` 토글 | 캐시라인 단위 stale 데이터, uncached면 정상 |
| 2 | IOMMU 매핑 누락/조기 unmap | SMMU fault 로그, `iommu.passthrough=1` | fault 로그 + passthrough면 정상 |
| 3 | inbound 창(`dma-ranges`)이 RAM 전체를 안 덮음 | 깨지는 주소가 특정 물리 주소 이상인지 | 고주소 버퍼에서만 실패 |
| 4 | DMA mask 부족 (32-bit 장치 + 4 GB 이상 RAM) | 드라이버 로그, bounce buffer(swiotlb) 사용 여부 | 고주소만 실패 또는 swiotlb 고갈 |
| 5 | 순서 문제 (doorbell write 전에 descriptor write가 보이지 않음 — barrier 누락) | `wmb()`/`dma_wmb()` 추가 실험 | 부하 시에만, 드물게 |
| 6 | 링크 레벨 데이터 오류 (드묾 — LCRC로 잡히므로) | AER BadTLP/ECRC | AER 증가 동반 |

**Bisect**
1. `iommu.passthrough=1` → 고쳐지면 IOMMU 매핑 문제.
2. 버퍼를 coherent(`dma_alloc_coherent`)로 → 고쳐지면 streaming DMA 캐시 유지 문제.
3. 버퍼 주소를 저주소/고주소로 고정 → 주소 의존성 → inbound 창 또는 mask.
4. 크기·정렬 sweep (캐시라인 미만, 4 KiB 경계 교차) → 패턴화.

**결론·회귀 테스트** — "DMA 크기(1 B–4 MiB) × 정렬(0–63 B 오프셋) × 4 KiB 경계 교차 × 방향(H2C/C2H) × IOMMU on/off, 패턴 CRC 비교" 테스트. FPGA XDMA endpoint나 `pci-epf-test`로 실행 ([LP16](2026-10-31_LP16_baremetal_vs_linux_zynq.html)).

<details><summary>🎤 English 2-minute answer</summary>

"When the same card and driver work on x86 but corrupt data on our Arm SoC, my first suspect is coherency. On x86, PCIe DMA is normally snooped by the caches; on many Arm SoCs the PCIe path is non-coherent, so software must clean the cache before the device reads a buffer and invalidate it before the CPU reads what the device wrote. The Linux DMA API does that, so a driver that bypasses it — or a device tree that marks the controller dma-coherent when the hardware isn't — breaks only on Arm. I look at the corruption pattern: stale data in 64-byte chunks screams cache. The second suspect is the SMMU: a missing or early-unmapped IOVA shows up as a translation fault with the stream ID and IOVA in the log. Third, address-dependent failures point to the inbound window, dma-ranges, not covering all of DRAM, or a DMA mask that's too small. I bisect with one switch at a time: IOMMU passthrough, coherent buffers instead of streaming ones, and pinning buffers to low or high addresses. I'd also check barriers — descriptor writes must be visible before the doorbell. The regression is a size, alignment and boundary sweep in both directions with CRC checks, with the IOMMU on and off."

</details>

<details><summary>🔁 꼬리 질문 체인</summary>

- **Q1. First check?** — "The corruption pattern and the SMMU fault log, and whether the controller is described as dma-coherent." · 한국어: 깨짐 패턴 + SMMU 로그 + `dma-coherent`.
- **Q2. No SMMU faults, passthrough doesn't help. Next?** — "Cache maintenance: switch to coherent buffers; if that fixes it, a streaming mapping is missing a sync, or dma-coherent is wrong in the DT." · 한국어: coherent 버퍼로 바꿔 보기 → sync 누락 / DT 오류.
- **Q3. HW or SW?** — "If the data on the wire is right — analyzer shows the correct payload in the MWr — but memory has stale data, it's coherency handling, which is software or SoC interconnect configuration. If the payload on the wire is already wrong, it's the device." · 한국어: 와이어 payload가 맞으면 SoC/SW, 틀리면 장치.
- **Q4. Prevent?** — "A DMA sweep test in CI on the Arm platform specifically — sizes, alignments, 4K crossings, both directions, IOMMU on and off — and a DT review rule for dma-coherent and dma-ranges." · 한국어: Arm 플랫폼 DMA sweep + DT 리뷰 규칙.

</details>

### S7 · AER correctable 에러 폭증

**증상** — 기능은 되는데 로그가 쏟아진다.

```text
[예시 출력]  (커널 6.6 계열)
pcieport 0000:00:00.0: AER: Corrected error received: 0000:01:00.0
nvme 0000:01:00.0: PCIe Bus Error: severity=Corrected, type=Physical Layer, (Receiver ID)
nvme 0000:01:00.0:   device [1234:5678] error status/mask=00000001/0000e000
nvme 0000:01:00.0:    [ 0] RxErr                  (First)

[예시 출력]  (최신 mainline 형식)
pcieport 0000:00:00.0: AER: Correctable error message received from 0000:01:00.0
nvme 0000:01:00.0: PCIe Bus Error: severity=Correctable
nvme 0000:01:00.0:   device [1234:5678] error status/mask=00000001/0000e000
nvme 0000:01:00.0:   [ 0] RxErr             | Receiver    | Physical Layer

$ cat /sys/bus/pci/devices/0000:01:00.0/aer_dev_correctable
RxErr 1834
BadTLP 12
BadDLLP 40
Rollover 0
Timeout 3
...
TOTAL_ERR_COR 1889
```

**처음 보는 사람을 위한 배경** — correctable 에러는 **HW가 이미 고친** 에러다. 예: 받은 비트가 깨짐(Receiver Error) → Data Link Layer가 NAK → 재전송(replay). 데이터는 결국 정상이지만 재전송이 늘면 **성능이 떨어지고, 신호 margin이 줄고 있다는 경고등**이다. 에러 종류가 어느 층인지(Physical / Data Link / Transaction) 알려 준다 [1]. 비트 이름은 `RxErr`(bit 0), `BadTLP`(6), `BadDLLP`(7), `Rollover`(8), `Timeout`(12, replay timer) [17] ([LP09](2026-10-31_LP09_aer_dpc_error_handling.html)).

**볼 정보**
- 에러 비트 종류와 **어느 쪽이 Receiver**인가 (endpoint가 보고 → 다운스트림 방향 신호, root port가 보고 → 업스트림 방향).
- `aer_dev_correctable` 카운터를 시간·온도·트래픽과 함께 기록 (sysfs) [1].
- 레이트리밋: 로그는 ratelimit 되므로 **카운터가 진실**. 최신 커널은 `correctable_ratelimit_interval_ms` 등 조절 가능 [1].
- 링크 속도·폭, `LnkSta2` EQ, pcilmr margin.
- ASPM 상태 (`LnkCtl: ASPM L1 Enabled`) — L1 진입/탈출 순간에 에러가 몰리는지.

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 신호 margin 부족 (채널 손실, 커넥터, 크로스토크) | pcilmr, 속도 낮추면 사라지나, 온도 상관 | Gen 낮추면 0, 고온에서 증가 |
| 2 | EQ 결과가 나쁨 (preset 비최적) | preset sweep, 재-EQ | 특정 preset에서 감소 |
| 3 | ASPM L0s/L1 진입·탈출 문제 | `pcie_aspm=off`로 비교 | ASPM off면 감소 |
| 4 | refclk 지터 / 전원 노이즈 | 스코프, 부하 상관 (전원 droop) | 특정 부하(NPU 풀가동)에서 증가 |
| 5 | 장치 PHY 문제 | 다른 카드 | 특정 카드에서만 |
| 6 | 실제로는 정상 범위 (너무 민감한 기준) | 기준 링크와 비교 | 양품 링크도 비슷한 수준 |

**Bisect**
1. 속도 낮추기 → 사라지면 SI. 2. ASPM off → 사라지면 PM. 3. 온도·부하 sweep → 상관 그래프. 4. 레인별 margin → 특정 레인.

**결론·회귀 테스트** — soak 테스트에 "시간당 correctable 증가량 < 기준, 온도와 상관 없음" 판정 추가. 기준은 양품 보드 분포에서 정한다.

<details><summary>🎤 English 2-minute answer</summary>

"Correctable errors were fixed by hardware — typically a receiver error causes a NAK and a replay — so the data is fine, but they're the earliest warning that the link margin is shrinking, and replays cost bandwidth. First I identify which error and who reported it: receiver errors and bad TLP or DLLP are physical and data-link layer, and whether the endpoint or the root port is the receiver tells me which direction of the link is suffering. I rely on the sysfs AER counters rather than the log, because the log is rate-limited. Then I correlate: does the rate follow temperature, traffic, or the ASPM state transitions, and does it disappear at a lower speed? Disappearing at lower speed and growing with temperature means signal integrity — then I margin each lane at 16 GT/s and above and sweep TX presets. Disappearing with ASPM disabled points to L1 entry or exit. If one card is bad and others aren't, it's that device's PHY. Finally I compare to a golden board, because a handful per day may be normal; the pass criterion should be a rate threshold derived from known-good boards, checked in the soak test across temperature."

</details>

<details><summary>🔁 꼬리 질문 체인</summary>

- **Q1. First check?** — "Which error bits, which device is the receiver, and the rate from the counters over time." · 한국어: 비트 종류·Receiver 방향·카운터 추세.
- **Q2. It's all RxErr at the endpoint. Next?** — "That's the downstream direction — root port transmitter, channel, endpoint receiver. Margin the endpoint's receiver per lane, check temperature correlation, sweep the root port's TX preset." · 한국어: 다운스트림 방향 → EP 수신 margin, RP 송신 preset.
- **Q3. HW or SW?** — "Software can't cause receiver errors except via configuration like ASPM or presets. If they persist with ASPM off and default presets, and scale with temperature, it's the channel or PHY." · 한국어: ASPM·preset 설정 외에 SW는 원인이 못 됨.
- **Q4. Prevent?** — "A soak test with a rate threshold from golden boards, margining at corners in the validation plan, and trend monitoring in the fleet if the counters are reachable." · 한국어: 양품 기준 임계값 soak + 코너 margining + 추세 모니터링.
- **Q5 (추가). With Gen6 FEC, does a correctable error mean the same thing?** — "No — at 64 GT/s with PAM4, raw bit errors are expected and FEC corrects most of them inside the flit, so what matters is the rate of FEC corrections and CRC-triggered replays, not 'any error'. See LP17." · 한국어: Gen6는 FEC가 기본으로 고치므로 의미가 다름 → [LP17](2026-10-31_LP17_gen5_gen6_retimer_automotive.html).

</details>

### S8 · 부하·고온에서 링크 끊김

**증상** — NPU 풀가동 또는 챔버 고온에서 장치가 갑자기 사라진다.

```text
[예시 출력]
pcieport 0000:00:00.0: AER: Uncorrectable (Fatal) error message received from 0000:00:00.0
pcieport 0000:00:00.0: PCIe Bus Error: severity=Uncorrectable (Fatal)
pcieport 0000:00:00.0:   [ 5] SDES              | Receiver    | Data Link Layer   (First)
pcieport 0000:00:00.0: DPC: containment event, status:0x1f01: unmasked uncorrectable error detected
nvme nvme0: controller is down; will reset: CSTS=0xffffffff, PCI_STATUS=0xffff
pcieport 0000:00:00.0: pciehp: Slot(0): Link Down
```

**처음 보는 사람을 위한 배경** — `SDES`(Surprise Down Error, bit 5)는 "링크가 예고 없이 끊겼다"는 uncorrectable 에러다 [1]. DPC가 켜져 있으면 포트가 **스스로 링크를 끊어 격리**하므로 "장치가 사라짐"이 DPC의 정상 동작일 수도 있다 [12]. 장치 레지스터를 읽으면 all-1s(`CSTS=0xffffffff`) — 응답이 없다는 뜻 [14]. 고온·부하는 margin을 줄이고(신호), 전원 droop을 만들고(전원), 클럭 지터를 늘린다.

**볼 정보**
- AER uncorrectable 종류 (SDES, CmpltTO, MalfTLP…), correctable 추세가 **끊기기 전에** 증가했나.
- DPC 이벤트, pciehp Link Down, root port `LnkSta` 재학습 흔적 (`BWMgmt`).
- 장치 내부 로그 (NVMe SMART/telemetry, NPU FW crash log) — **장치가 먼저 죽었나, 링크가 먼저 죽었나**.
- 온도·전압·전류 로그를 같은 시간축에.

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 신호 margin 부족 (고온) | correctable 추세가 끊기기 전 증가, Gen 낮추면 안 끊김 | RxErr 증가 → SDES |
| 2 | 전원 droop (부하 순간) | 스코프로 rail 트리거, 부하 패턴 상관 | 특정 부하 시작 시점에 끊김 |
| 3 | 장치 FW crash/watchdog → 링크 리셋 | 장치 로그, 장치 측 reset 원인 레지스터 | 장치 쪽 리셋 기록 |
| 4 | refclk 지터/SSC 문제 (고온) | 클럭 스코프 | 고온에서 지터 증가 |
| 5 | ASPM L1 탈출 실패 (부하가 들쭉날쭉할 때) | ASPM off 비교 | ASPM off면 안 끊김 |
| 6 | 열로 인한 기계적 문제 (커넥터) | 온도 사이클 + 진동 | 특정 보드·위치 |

**Bisect** — Gen 낮추기 / ASPM off / 부하만(상온) vs 온도만(무부하) 분리 / 다른 카드 / 전원 강화(외부 전원) → 각각 실패율 측정.

**결론·회귀 테스트** — "챔버 corner(예 −40/25/105 °C [추정: 자동차 grade 범위는 부품 등급마다 다름]) × 최대 부하 soak, 링크 다운 0, correctable 임계값 이하" + 실패 시 자동으로 AER/LTSSM/전원 로그 수집. 복구 경로(DPC → 재열거 → 드라이버 recovery) 테스트도 함께 ([LP09](2026-10-31_LP09_aer_dpc_error_handling.html), [LP11](2026-10-31_LP11_reset_types.html)).

<details><summary>🎤 English 2-minute answer</summary>

"A link that drops only under load or heat is a margin problem until proven otherwise, and the timeline is everything. I put AER logs, temperature, rail voltage and the workload on one time axis. The key question is what died first: if correctable receiver errors climbed for minutes before a surprise-down error, the link margin collapsed — signal integrity at temperature. If the device's own logs show a firmware crash or watchdog reset first, the link drop is a consequence. If the drop lines up with a load step, I trigger the scope on the rail to catch a droop. I also check whether DPC fired, because then the port deliberately took the link down to contain an error, and the real question is what the triggering error was. Then I separate the stresses: full load at room temperature, idle at high temperature, lower link speed, ASPM off, external power. Each experiment gets a failure rate, not a single run. When the cause is fixed, the chamber soak at corners with full load becomes a standing regression, with automatic capture of AER, LTSSM and power logs on any drop, and we also test that recovery — DPC, re-enumeration, driver reset — works."

</details>

<details><summary>🔁 꼬리 질문 체인</summary>

- **Q1. First check?** — "The timeline: correctable trend, the first uncorrectable error, DPC, and device-side logs, against temperature and load." · 한국어: 한 시간축에 놓고 무엇이 먼저 죽었나.
- **Q2. Correctable errors were flat, then a sudden surprise down. Next?** — "Sudden without a margin trend suggests power or the device resetting itself — scope the rails triggered on the drop, and read the device's reset-cause registers." · 한국어: 추세 없는 급사 → 전원 droop 또는 장치 자체 리셋.
- **Q3. HW or SW?** — "Reproduce with no driver traffic — a bare-metal or minimal DMA load generator at temperature. If it still drops, it's hardware. If it only drops with our driver's specific power management, it's software or PM interaction." · 한국어: 최소 부하 생성기로 재현되면 HW.
- **Q4. Prevent?** — "Chamber soak at corners with full load in the plan, automatic log capture, and a recovery test so a drop in the field is at least recovered safely." · 한국어: 코너 soak + 자동 수집 + 복구 테스트.

</details>

### S9 · suspend/resume 후 장치가 사라짐 / `rev ff`

**증상** — resume 후 `lspci`에 `(rev ff)`, config가 전부 FF.

```text
[예시 출력]
$ lspci -s 01:00.0
01:00.0 Non-Volatile memory controller: Vendor Device (rev ff)
$ sudo lspci -s 01:00.0 -vvv | head -3
01:00.0 Non-Volatile memory controller: Vendor Device (rev ff) (prog-if ff)
        !!! Unknown header type 7f
nvme 0000:01:00.0: Unable to change power state from D3cold to D0, device inaccessible
pcieport 0000:00:00.0: Data Link Layer Link Active not set in 1000 msec
nvme 0000:01:00.0: not ready 65535ms after resume; giving up
```

**처음 보는 사람을 위한 배경** — `rev ff`는 장치가 **이미 열거된 적이 있어서 목록엔 남아 있지만**, 지금 config read가 all-1s로 돌아온다는 뜻이다 (응답 없음). resume은 장치를 D3(꺼짐)에서 D0(동작)으로 되돌리는데, 그 과정에서 링크 재학습 + 장치 준비 + **config space 복원**이 모두 성공해야 한다. ASPM L1.2 같은 깊은 저전력 상태는 refclk를 끄기 때문에 복귀 타이밍이 더 까다롭다 ([LP10](2026-10-31_LP10_power_mgmt_aspm.html), [LP11](2026-10-31_LP11_reset_types.html)).

**볼 정보**
- 위 커널 문자열 3종 [9]: 링크가 안 돌아왔나(`Data Link Layer Link Active not set`), 장치가 CRS만 주나(`not ready ... giving up`), 아예 응답 없나(`device inaccessible`).
- root port `LnkSta` (resume 후), PM 상태 sysfs `power_state`, `d3cold_allowed`.
- ASPM/L1 substates 설정 (`L1SubCtl1`), PERST#가 resume에서 다시 토글되는지 (스코프).
- 장치가 D3cold(전원 차단)였나 D3hot였나.

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 링크 재학습 실패 (D3cold 후 PERST#/전원 시퀀스) | `DLActive` 안 돌아옴 로그, 스코프 | `Data Link Layer Link Active not set` |
| 2 | 장치 준비 시간 > 대기 시간 | `not ready`/`ready %dms` 로그, 대기 시간 늘리기 | 대기 늘리면 성공 |
| 3 | L1.2 탈출 문제 (CLKREQ#, T_POWER_ON) | L1.2 비활성화 비교 | L1.2 off면 정상 |
| 4 | config 복원 누락 (BAR·MSI·벤더 레지스터) | resume 후 config 덤프 vs suspend 전 | 링크 OK인데 BAR 0, MSI off |
| 5 | 플랫폼 전원 레일이 안 돌아옴 | PMIC 로그 | rail off |

**Bisect** — `d3cold_allowed=0` (D3hot만) / ASPM·L1.2 off / runtime PM off(`power/control=on`) / resume 대기 시간 증가 → 각각 반복 suspend/resume N회 실패율.

**결론·회귀 테스트** — "suspend/resume 및 runtime D3↔D0 루프 N=1000, 매번 vendor ID·BAR·MSI·링크 속도 확인 + 간단 I/O" 테스트.

<details><summary>🎤 English 2-minute answer</summary>

"'rev ff' means the device is still in the kernel's list from boot, but config reads now return all ones — nobody answers. Resume has to succeed at three things: the link must retrain, the device must become ready, and its config state must be restored. The kernel log usually tells me which one failed: 'Data Link Layer Link Active not set' means the link never came back; 'not ready after resume, giving up' means the link is up but the device kept answering with retry status; 'device inaccessible' means config reads fail. I check the root port's Link Status after resume, whether the device went to D3cold — power removed, so PERST# and the power sequence must be redone — or only D3hot, and whether L1 substates were enabled, since L1.2 turns off the reference clock and has tight exit timing. To bisect, I disallow D3cold, disable ASPM and L1.2, disable runtime PM, and lengthen the wait, each with a few hundred cycles to get a failure rate. If the link is fine but BARs or MSI are zero after resume, it's a restore problem in software. The regression is a suspend/resume and runtime-PM loop that verifies config, link and a small I/O each cycle."

</details>

<details><summary>🔁 꼬리 질문 체인</summary>

- **Q1. First check?** — "Which of the three kernel messages appeared, and the root port's Link Status after resume." · 한국어: 3종 로그 중 무엇 + RP LnkSta.
- **Q2. Link is back, DLActive+, still ff. Next?** — "Then the device isn't responding to config: either still booting — lengthen the wait and watch for CRS — or it lost power or is stuck in reset, so check its rail and PERST#." · 한국어: 링크는 OK → 장치 준비/전원/리셋.
- **Q3. HW or SW?** — "Do the same D3cold-to-D0 sequence by hand — power off, PERST#, power on, wait, read vendor ID — with no driver. If that fails, it's the platform power or reset sequence or the device. If it works, the kernel's PM path or the driver's restore is the bug." · 한국어: 드라이버 없이 수동 시퀀스로 재현.
- **Q4. Prevent?** — "A PM loop test in CI with every PM feature on — L1.2, D3cold — because that's what ships for power, and a check after every cycle." · 한국어: 모든 PM 기능 켠 루프 테스트.

</details>

### S10 · 첫 config read에서 시스템 hang (bare-metal/부트로더/커널 초기)

**증상** — 부팅 중 PCIe 열거 시점에서 멈춤. JTAG로 보면 CPU가 load 명령에서 멈춰 있거나 SError/external abort.

```text
[예시 출력]
[    2.314159] dw-pcie 3c0800000.pcie: host bridge /soc/pcie@3c0800000 ranges:
[    2.314200] dw-pcie 3c0800000.pcie:       IO 0x0060000000..0x00600fffff -> 0x0000000000
[    2.314230] dw-pcie 3c0800000.pcie:      MEM 0x0060100000..0x006fffffff -> 0x0060100000
               (여기서 출력 멈춤)
-- 또는 --
[    2.401000] SError Interrupt on CPU0, code 0x00000000bf000002 -- SError
```

**처음 보는 사람을 위한 배경** — config read도 결국 **CPU의 메모리 load**다 (ECAM 창 주소를 읽음). 컨트롤러는 그 load를 config TLP로 바꿔 링크로 보내고 completion을 기다린다. 링크가 down이거나 주소 변환(iATU) 창이 설정 안 됐거나 completion이 안 오면, SoC 설계에 따라 ① all-1s를 돌려주거나 ② **에러 응답(abort)** 을 내거나 ③ **영원히 기다린다(hang)**. 그래서 많은 컨트롤러 드라이버는 **링크가 업일 때만** 다운스트림 config 접근을 허용한다 — 예: `pcie-xilinx.c`의 "Check if link is up when trying to access downstream pcie ports" [7] ([LP05](2026-10-31_LP05_soc_host_controller_integration.html)).

**볼 정보**
- JTAG: PC, 마지막 load 주소 → ECAM 창 안인가, BAR 창인가.
- 컨트롤러 레지스터: link up 비트, LTSSM, iATU 창 설정(outbound config 창), completion timeout 설정.
- SoC 인터커넥트 에러 레지스터 (bus error/timeout 기록).
- 부트로더(U-Boot)에서 같은 접근이 되는지.

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 링크 down 상태에서 다운스트림 config 접근 (가드 없음) | link up 비트 확인 후 접근하도록 수정 | 링크 업일 때만 hang 없음 |
| 2 | ECAM/iATU outbound 창 미설정 또는 크기 오류 | iATU 레지스터 덤프 | 접근 주소가 어떤 창에도 안 걸림 |
| 3 | 컨트롤러 클럭/리셋이 안 풀린 상태에서 컨트롤러 레지스터 접근 | 클럭·리셋 컨트롤러 상태 | 컨트롤러 레지스터 읽기 자체에서 hang |
| 4 | completion timeout이 너무 김/비활성 + 장치 무응답 | completion timeout 설정 | 장치 응답 없을 때만 |
| 5 | 존재하지 않는 device 번호 접근 (일부 RC는 dev≠0 접근 시 문제) | dev 0만 스캔하도록 | dev>0 접근에서 hang |

**Bisect**
1. 가장 작은 bare-metal 루프: 클럭·리셋 해제 → 컨트롤러 ID 레지스터 read → link up polling → **root port 자신의 config**(bus 0) read → 링크 업이면 bus 1 dev 0 vendor ID read. 어디서 멈추는지 한 줄씩 ([LP16 C 코드](2026-10-31_LP16_baremetal_vs_linux_zynq.html)).
2. 링크를 일부러 down (카드 제거) → hang 재현 → 가드 코드 확인.

**결론·회귀 테스트** — "카드 없는 부팅", "링크 실패 상태 부팅"을 정식 테스트로: hang 없이 'link down' 로그 후 부팅 계속. 드라이버에 link-up 가드 + completion timeout 설정 리뷰 항목.

<details><summary>🎤 English 2-minute answer</summary>

"A config read is just a CPU load from the ECAM window; the controller turns it into a configuration TLP and waits for the completion. If the link is down, or the outbound translation window for config isn't programmed, or the device never answers, the SoC either returns all ones, raises an abort, or — worst case — stalls the CPU forever. So when boot hangs at enumeration, I attach JTAG and look at the PC and the faulting address: is it inside the ECAM window, and for which bus? Then I check the controller: is the link-up bit set, what's the LTSSM state, are the iATU windows programmed, is completion timeout enabled? The most common bug is accessing bus 1 while the link is down; good host drivers only forward downstream config accesses when the link is up — the Xilinx driver does exactly that check. Second is a missing or wrong translation window, third is touching controller registers before its clocks and resets are released. I bisect with a minimal bare-metal sequence — controller registers, link poll, root port config, then bus 1 — and see which line hangs. The regression is booting with no card and with a failed link: the system must log 'link down' and continue, never hang."

</details>

<details><summary>🔁 꼬리 질문 체인</summary>

- **Q1. First check?** — "JTAG: PC and the address being loaded — which window and which bus." · 한국어: JTAG로 PC·접근 주소.
- **Q2. It's bus 1, link-up bit is set. Next?** — "Then the translation window for config — is the outbound region for bus 1 programmed with the right type (Type 0 vs Type 1) — and whether completion timeout is enabled. Also check the device isn't still in reset." · 한국어: iATU config 창 타입(Type0/1)·completion timeout·장치 리셋.
- **Q3. HW or SW?** — "If a minimal bare-metal access with correct windows still hangs with the link up, and an analyzer shows the config TLP went out with no completion, it's the device. If the TLP never goes out, it's our controller config, or an RTL issue in the bridge — that's when I give the design team a minimal repro." · 한국어: 와이어에 TLP 나갔나로 장치/컨트롤러 분리.
- **Q4. Prevent?** — "No-card and link-fail boot tests in CI, a link-up guard in the access path, and completion timeouts always enabled with a bounded value." · 한국어: 카드 없는 부팅 테스트 + 가드 + timeout.

</details>

### B1 · 보너스: 차량 온도 범위에서만 간헐 링크 drop (자동차 특화)

**증상** — 상온 랩에서는 수천 번 OK. 챔버 −40 °C 콜드 스타트 또는 +85 °C 이상에서 **수백 번에 한 번** 링크가 안 뜨거나 런타임에 끊긴다. 차량 진동 테스트에서만 나오기도 한다.

```text
[예시 출력]  (harness 요약 로그)
cycle 0412  T=-40C  Vcore=0.72V  link=Gen4 x4  DLActive+  RxErr=0
cycle 0413  T=-40C  Vcore=0.72V  link=DOWN     LTSSM=POLL_ACTIVE (timeout)   <-- FAIL, snapshot saved
cycle 0414  T=-40C  Vcore=0.72V  link=Gen4 x4  DLActive+  RxErr=2
pcieport 0000:00:00.0: pciehp: Slot(0): Link Down
pcieport 0000:00:00.0: pciehp: Slot(0): Card present
```

**처음 보는 사람을 위한 배경** — 자동차 부품은 넓은 온도 범위(부품 등급마다 다름, 예 AEC-Q100 Grade 2 = −40 ~ +105 °C [23])와 진동·전원 과도 상태를 견뎌야 한다. 온도는 채널 손실·PHY 특성·refclk·전원 레귤레이터를 동시에 바꾼다. **간헐(1/수백)** 이 핵심 난이도 — 한 번 재현에 몇 시간이 걸리므로 통계 설계와 자동 수집이 디버그의 절반이다 ([LP17](2026-10-31_LP17_gen5_gen6_retimer_automotive.html)).

**볼 정보**
- 실패 사이클의 LTSSM 상태 (Detect/Polling/Recovery 어디서 멈췄나) — 자동 스냅샷 필수.
- 온도별 실패율 표, 레인별 margin vs 온도 (pcilmr), EQ preset 결과 vs 온도.
- refclk(SSC 포함) 지터 vs 온도, 전원 램프 vs 온도 (콜드 스타트는 레귤레이터 램프가 느림 → PERST# 타이밍 위반 가능).
- retimer가 있으면 retimer 쪽 레지스터 (각 segment 상태) — `LnkSta2` `Retimer+` [18].

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 콜드 스타트 전원 램프 지연 → PERST#가 rail 안정 전에 해제 | 스코프 −40 °C에서 rail + PERST# | T_PVPERL 위반, Detect/Polling 실패 |
| 2 | 온도에 따른 margin 감소 (채널·PHY) | 온도별 pcilmr, Gen 낮추면 사라지나 | 특정 온도에서 margin 급감 |
| 3 | EQ preset이 상온 기준으로만 최적 | 온도별 preset sweep | 저온/고온에서 최적 preset이 다름 |
| 4 | refclk/SSC 온도 특성 | 클럭 지터 측정 | 고온에서 지터 증가 |
| 5 | 커넥터/케이블 진동 (차량 하네스) | 진동 테이블 + 링크 모니터 | 진동 중에만 drop |

**Bisect** — 온도만(무부하) / 진동만(상온) / Gen 낮추기 / 특정 보드만? / PERST# 지연 증가. 실패율 1/400이면 "0 실패" 판정에 ~1200 사이클 이상 [추정: rule of three].

**결론·회귀 테스트** — 챔버 프로파일(콜드 스타트, 온도 램프, 고온 soak) + 진동을 **정식 회귀 스위트**로, 실패 시 LTSSM·AER·전원 파형 자동 캡처, 결과를 온도×실패율 히트맵(shmoo)으로.

<details><summary>🎤 English 2-minute answer</summary>

"Intermittent failures only at temperature are mostly a test-design problem before they're a debug problem. I'd first make it measurable: an automated chamber loop that records, for every cycle, temperature, rail voltages, negotiated speed and width, error counters, and — on failure — the LTSSM state and register snapshots, so I know whether it died in Detect, Polling or Recovery. Then I build a failure-rate map across temperature, like a shmoo plot. Cold-start failures often aren't signal integrity at all: regulators ramp slower at minus forty, and if PERST# is released before the rails are stable, you break the 100-millisecond power-to-PERST# rule — so I scope the rails and PERST# in the chamber. If it's margin, lane margining at each temperature shows which lane collapses, and a preset sweep at temperature tells me whether the equalization settings that are optimal at room temperature are wrong at the extremes. Vibration gets its own run because a harness connector can drop a link without any thermal effect. Statistically, with a one-in-four-hundred failure, I need well over a thousand clean cycles before I can claim a fix. The chamber profile then stays in the regression suite. This is very close to what I did at SK hynix, automating reliability tests across chamber corners."

</details>

<details><summary>🔁 꼬리 질문 체인</summary>

- **Q1. First thing?** — "Make it measurable: automated cycles with per-cycle logging and failure snapshots, then a failure rate per temperature." · 한국어: 자동 루프 + 스냅샷 + 온도별 실패율.
- **Q2. It's only at −40 °C cold start. Next?** — "Power sequencing: scope rails and PERST# in the chamber — slower regulator ramps can violate the power-stable-to-PERST# time. Then refclk startup." · 한국어: 콜드 스타트 = 전원 램프·PERST# 타이밍 먼저.
- **Q3. HW or SW?** — "Software only controls PERST# timing and training parameters. If extending the PERST# delay in software fixes it, the root cause is still a hardware timing spec that software must honor — I'd fix both the timing and document the requirement." · 한국어: SW로 우회돼도 원인은 HW 타이밍 → 둘 다 고침.
- **Q4. Prevent?** — "Chamber and vibration profiles as a standing regression, margining at temperature in the validation plan, and a requirement on the power-sequencing spec for every board revision." · 한국어: 챔버·진동 상시 회귀 + 온도 margining + 보드 전원 시퀀스 요구사항.

</details>

### B2 · 보너스: NVMe/NPU throughput이 기대보다 낮음

**증상** — Gen4 x4 NVMe인데 순차 읽기 3 GB/s (기대 ~7 GB/s), 또는 NPU의 PCIe DMA가 이론치의 40%.

```text
[예시 출력]
$ fio --name=seq --rw=read --bs=128k --iodepth=32 --direct=1 --filename=/dev/nvme0n1 --runtime=30
   READ: bw=3012MiB/s (3158MB/s)
$ dmesg | grep "available PCIe bandwidth"
nvme 0000:01:00.0: 31.504 Gb/s available PCIe bandwidth, limited by 8.0 GT/s PCIe x4 link at 0000:00:00.0 (capable of 63.012 Gb/s with 16.0 GT/s PCIe x4 link)
$ sudo lspci -s 01:00.0 -vvv | grep -E 'MaxPayload|MaxReadReq'
        DevCtl: ... MaxPayload 128 bytes, MaxReadReq 512 bytes
```

**처음 보는 사람을 위한 배경** — 이론치 계산: Gen4 = 16 GT/s × 128/130 인코딩 ÷ 8 ≈ **1.97 GB/s/레인/방향** → x4 ≈ 7.88 GB/s. 실제로는 TLP 헤더·DLLP·flow control 때문에 **페이로드 효율**이 MPS(Max Payload Size)에 따라 달라진다 (작은 MPS → 헤더 비율↑). 또 읽기는 "요청 → 완료"라 **동시에 몇 개 요청이 날아가 있나(outstanding)** 가 중요 — Little's law: throughput = outstanding bytes ÷ latency ([성능 메트릭 노트](2026-10-30_perf_metrics_ssd_vs_soc.html)).

**볼 정보**
- 링크: `LnkSta` speed/width, dmesg `limited by` 줄 [9] — **S2/S3이 숨어 있지 않은지 먼저**.
- `DevCtl` MaxPayload / MaxReadReq, `DevCap` MaxPayload (지원 최대), 커널 `pci=pcie_bus_perf` 등 MPS 정책 [22].
- ASPM 상태 (L1 exit latency가 작은 I/O 사이마다 붙음).
- AER correctable (replay가 대역폭을 먹음).
- IOMMU on/off 차이 (IOTLB miss), 인터럽트 coalescing, CPU 사용률·IRQ affinity.
- 장치 쪽: 큐 깊이, DMA 디스크립터 크기, NPU 쪽 버스트 크기·outstanding 수.

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 링크 다운그레이드 (S2/S3) | LnkSta, `limited by` | 속도·폭이 기대 이하 |
| 2 | MPS/MRRS 작음 | DevCtl, 바꿔서 측정 | MPS 128 → 256/512로 올리면 개선 |
| 3 | outstanding 부족 (QD, 태그 수, DMA 엔진 동시 요청 수) | QD sweep, 엔진 설정 | QD 늘리면 선형 증가 후 포화 |
| 4 | ASPM L1 진입/탈출 | ASPM off 비교 | 작은 I/O에서 큰 개선 |
| 5 | IOMMU 오버헤드 | passthrough 비교 | passthrough에서 개선 |
| 6 | CPU/인터럽트 병목 | CPU 사용률, IRQ 분산 | 한 코어 100% |
| 7 | replay (correctable 에러) | AER 카운터 | RxErr/Replay 증가 |
| 8 | 장치/메모리 쪽 병목 (DDR 대역폭, NPU 내부) | 장치 내부 카운터, DDR 성능 카운터 | 링크는 놀고 있음 |

**Bisect** — 링크 확인 → 큰 블록·높은 QD로 **링크 상한**부터 측정 → MPS/MRRS → ASPM off → IOMMU passthrough → 블록 크기·QD sweep로 곡선(latency vs throughput) 그리기.

**결론·회귀 테스트** — 성능 회귀 테스트: 고정 워크로드(블록 크기 × QD × 방향)의 throughput/latency 분포를 기준값과 비교, ±X% 벗어나면 실패. 링크 속도·폭·MPS를 결과에 같이 기록.

<details><summary>🎤 English 2-minute answer</summary>

"I start by computing what 'expected' is and checking the link isn't the problem. Gen4 is 16 GT/s with 128b/130b encoding, about 1.97 GB/s per lane per direction, so an x4 link is about 7.9 GB/s raw, and realistically a bit less after TLP headers and flow control. The kernel prints the available bandwidth and which link limits it; if it says limited by an 8 GT/s link, it's actually scenario two, a downtrained link. If the link is right, I look at transaction efficiency: Max Payload Size and Max Read Request Size — a 128-byte payload wastes a lot on headers. Reads are request–completion, so throughput equals outstanding bytes divided by latency — Little's law — and I sweep queue depth to see if we're latency-bound. Then I toggle one thing at a time: ASPM off, because L1 exit latency hits every gap; IOMMU passthrough to see translation overhead; interrupt coalescing and CPU affinity. Correctable errors cause replays and steal bandwidth too. If the link is idle while throughput is low, the bottleneck is on the device or memory side, so I go to its internal counters. Finally I keep a performance regression with fixed workloads and recorded link parameters. This is the same methodology I used tuning SSD firmware with the silicon validation team at Solidigm."

</details>

<details><summary>🔁 꼬리 질문 체인</summary>

- **Q1. First check?** — "The link speed and width and the kernel's 'limited by' message, then compute the theoretical number." · 한국어: 링크 + 이론치 계산.
- **Q2. Link is Gen4 x4, still half. Next?** — "MPS/MRRS, then a queue-depth sweep to see if we're limited by outstanding requests, then ASPM and IOMMU toggles." · 한국어: MPS/MRRS → QD sweep → ASPM/IOMMU.
- **Q3. HW or SW?** — "If a minimal DMA loop on bare-metal with large transfers and max outstanding requests hits line rate, the hardware is capable and the software stack is the limiter. If even that stalls, I look at the device and the SoC fabric with performance counters." · 한국어: bare-metal 최대 부하가 line rate면 SW 병목.
- **Q4. Prevent?** — "A performance regression suite with fixed workloads, thresholds, and the link and MPS settings recorded with every result." · 한국어: 고정 워크로드 성능 회귀 + 설정 기록.

</details>

## 4. 꼬리 질문 체인 (시나리오를 넘는 공통 체인)

### 4.1 LP00 기본 패턴 — "Walk me through how you'd debug a PCIe issue."

<details><summary>답 펼치기</summary>

- **Q. Walk me through how you'd debug a PCIe issue on a new board.** — "Bottom-up with one piece of evidence per layer: scope power, refclk and PERST#; root port Link Status and the controller's LTSSM register; enumeration and resource logs; driver probe; interrupts and DMA; then performance. I change one variable at a time to split hardware from software." · 한국어: 층별 증거 1개씩, 변수 하나씩.
- **꼬리1. What's the first thing you check?** — "The root port's Link Status — DLActive, speed, width — because it's always visible and tells me how far link training got." · 한국어: root port LnkSta.
- **꼬리2. That looks fine. What next?** — "Then the link is not the problem; I move up: is the endpoint enumerated, were its BARs assigned, did the driver probe, do interrupt counts move, does a DMA pattern compare." · 한국어: 한 층 위로.
- **꼬리3. How do you prove it's hardware and not software?** — "Remove software: reproduce with a bare-metal minimal sequence or in another host; look at the wire with an analyzer; check the analog with a scope. If the failure survives with the smallest software, it's hardware." · 한국어: SW 최소화 재현 + 와이어 + 스코프.
- **꼬리4. How would you prevent it from coming back?** — "Turn the reproduction into an automated regression with a pass criterion and automatic evidence capture on failure, and add the root-cause check to the bring-up checklist." · 한국어: 재현 → 자동 회귀 + 체크리스트.

</details>

### 4.2 "1-in-500 failure" — 통계와 자동화

<details><summary>답 펼치기</summary>

- **Q. A link-training failure shows up once every 500 boots. How do you debug it?** — "First make each failure self-documenting: automate the boot loop and capture LTSSM state, Link Status, AER and power waveforms on failure, so one occurrence gives me data. Then bisect variables, each with enough cycles to be statistically meaningful." · 한국어: 실패 한 번이 증거를 남기게 자동화.
- **꼬리1. How many cycles to claim it's fixed?** — "With a 1-in-500 rate, zero failures in about 1,500 cycles gives roughly 95% confidence the rate dropped below the old one — the rule of three. I'd run more if time allows." · 한국어: rule of three → 약 3×500. [추정: 근사 통계]
- **꼬리2. You can't afford that many cycles per experiment. Now what?** — "Make it fail more often: stress the suspected variable — temperature, voltage margin, a longer channel, a shorter PERST# delay. If a stress multiplies the failure rate, I've found the sensitive axis and can bisect faster." · 한국어: 의심 변수를 스트레스해서 실패율을 올림.
- **꼬리3. How do you know the test itself is right?** — "Inject known failures — pull a card, hold PERST#, force a low target speed — and confirm the harness flags each one. A test that never fails proves nothing." · 한국어: 알려진 고장을 주입해 테스트를 검증.

</details>

### 4.3 "누구에게 넘기나" — 에스컬레이션

<details><summary>답 펼치기</summary>

- **Q. When do you hand it to the SI team or the RTL designers?** — "When I have a minimal, repeatable reproduction that excludes software: the bare-metal sequence, the register dump, the analyzer trace or scope capture, and the conditions — temperature, voltage, which lane. At Solidigm I learned the design engineer's time is best spent on a five-line repro, not a full system log." · 한국어: SW를 배제한 최소 재현 + 증거 묶음.
- **꼬리1. What if the RTL team says it's software?** — "Then the evidence decides: if the TLP on the wire is correct and the response is wrong, it's the design. If our access is wrong, I fix it. I keep the discussion on the trace, not opinions." · 한국어: 트레이스로 판단.
- **꼬리2. Pre-silicon vs post-silicon — how does debug differ?** — "On FPGA or emulation I have full visibility — internal signals, ILA captures — but the PHY is often different or slower, so link and SI bugs don't reproduce. On silicon I have real PHYs but limited visibility, so I depend on debug registers, counters and analyzers. That's why I design the bring-up tests to run on both." · 한국어: FPGA는 가시성↑·PHY 다름 / 실리콘은 PHY 진짜·가시성↓.

</details>

### 4.4 행동+기술 브릿지 — "Tell me about the hardest interface bug you debugged."

<details><summary>답 펼치기</summary>

- **Q. Tell me about a hard interface bug.** — Don 버전 (숫자 채우기): "At Apple, a new wireless chipset intermittently failed [PCIe/I2C/SPMI — 고르기] during [상황 채우기]. I [측정 도구: DSO/protocol analyzer]로 [관측 채우기], isolated it to [원인 채우기], and the fix was [수정 채우기]. Failure rate went from [숫자 채우기] to [숫자 채우기], and I added [회귀 테스트 채우기]." · 한국어: STAR 형식, 숫자는 Don이 채움.
- **꼬리1. What did you try that didn't work?** — 준비: 처음 세운 가설 1개와 그걸 버린 증거 [채우기].
- **꼬리2. What would you do differently?** — 준비: "자동 스냅샷을 더 일찍 만들었으면 재현 대기 시간이 줄었을 것" 류 [채우기].

</details>

## 5. Don 경험 연결

- "At SK hynix I automated reliability tests across temperature chambers for high-speed interfaces — NAND and PCIe Gen3 through Gen5 — including shmoo plots over voltage, temperature and frequency. That's exactly how I'd attack an intermittent link drop in the vehicle temperature range: make every cycle self-documenting and turn failures into a rate map." (→ B1, S8)
- "At Solidigm I was a Staff engineer on a PCIe Gen6 SSD controller and brought up the PCIe/NVMe subsystem IP on FPGA prototypes before RTL freeze, so I'm used to splitting a failure between our firmware, the IP configuration and the RTL, and handing the design team a minimal repro." (→ S10, §4.3)
- "I also tuned firmware performance with the silicon validation team — throughput and latency against the theoretical link rate, queue depth and payload sizes — which is the method in the low-throughput scenario." (→ B2, [숫자 채우기]: 개선 폭 %)
- "At Apple I own root-cause when a new wireless chip meets the full system — PCIe, I2C, SPMI, RFFE — from the scope and protocol analyzer up to the driver, so the hardware-versus-software proof is my daily work." (→ S1, S5, §4.4, [숫자 채우기]: 사례 1개)

## 6. 실습 / 코드

### 6.1 검증된 것

- `onsitePrep/lp15/pcie_snapshot.sh` — Lima VM `elinux` (Ubuntu aarch64, kernel 7.0.0-38-generic)에서 `sudo sh pcie_snapshot.sh`로 실행, 377줄 출력 `onsitePrep/lp15/pcie_snapshot.vm_output.txt` [확인됨]. VM은 conventional PCI 가상 장치라 `LnkCap/LnkSta`·AER 줄은 비어 있음 (스크립트 자체 동작 확인 목적).
- 실제 VM 부팅 로그에서 확인한 열거 형식 (journal):

```text
acpi PNP0A08:00: ECAM at [mem 0x40000000-0x4fffffff] for [bus 00-ff]
PCI host bridge to bus 0000:00
pci_bus 0000:00: root bus resource [mem 0x50000000-0x6ffdffff window]
pci_bus 0000:00: root bus resource [mem 0x380000000-0x77fffffff window]
pci 0000:00:01.0: [1af4:1041] type 00 class 0x020000 conventional PCI endpoint
pci 0000:00:01.0: BAR 0 [mem 0x380060000-0x38006ffff 64bit]
```

| 줄 | 해석 (S4 연결) |
|---|---|
| `ECAM at [mem 0x40000000-0x4fffffff] for [bus 00-ff]` | config space 창: 256 bus × 1 MiB |
| `root bus resource [mem ... window]` 2줄 | host bridge의 32-bit / 64-bit 창 — 모든 BAR는 이 안에서 할당 |
| `BAR 0 [mem 0x380060000-0x38006ffff 64bit]` | 최신 형식의 BAR 로그 (`BAR n [mem 범위 속성]`) — 실패 시 같은 형식 뒤에 `: can't assign; no space` |

- 관련 실행 검증 코드: [LP16](2026-10-31_LP16_baremetal_vs_linux_zynq.html)의 `onsitePrep/lp16/bm_pcie_bringup.c` — S1(링크 없음 → root port만 보임), S4("no space"), S5(Bus Master 없으면 MSI 없음), S10(링크 업 후 100 ms 대기·가드)을 가짜 ECAM으로 재현 (ASan/UBSan, ALL PASS).

### 6.2 랩 레시피 `[미실행]` — QEMU로 시나리오 흉내 내기

```bash
# VM 안 QEMU (TCG, 느림 → timeout 필수). /var/lab/README.txt 의 커널+initramfs 사용.
timeout 120 qemu-system-aarch64 -M virt -cpu cortex-a57 -m 1G -nographic \
  -kernel Image -initrd rootfs.cpio.gz -append "console=ttyAMA0" \
  -device pcie-root-port,id=rp1,chassis=1 -device nvme,serial=lp15,drive=d0,bus=rp1 \
  -drive file=disk.img,if=none,id=d0,format=raw
# S2 흉내: root port 목표 속도 낮추고 retrain → LnkSta 변화 관찰 (QEMU는 링크 속도 에뮬레이션이 제한적)
setpci -s 00:01.0 CAP_EXP+0x30.w=0x1:0xf ; setpci -s 00:01.0 CAP_EXP+0x10.w=0x20:0x20
# S9 흉내: 장치 제거 후 rescan
echo 1 > /sys/bus/pci/devices/0000:01:00.0/remove ; echo 1 > /sys/bus/pci/rescan
# S7 흉내: aer-inject (CONFIG_PCIEAER_INJECT 필요) 로 correctable 주입 → aer_dev_correctable 증가
```

## 7. 자주 틀리는 것

1. **endpoint의 `DLActive-`를 보고 링크 다운이라고 결론** — DLActive 보고는 주로 downstream port(root port/switch) 기능이고 `LLActRep+`가 있어야 유효. **root port 쪽을 봐야 한다** ([N01](2026-10-31_N01_pcie_lspci_ltssm.html)).
2. **로그 문자열을 외워서 grep만** — 커널 버전마다 바뀐다 (AER, BAR, DWC 메시지 모두 최근 변경) [1][2][3][8]. 함수 이름과 의미로 기억.
3. **AER 로그 개수를 에러 개수로 착각** — 로그는 ratelimit. sysfs 카운터가 진실 [1].
4. **correctable 0개를 목표로 설정** — 양품 링크도 드물게 나옴. 양품 분포 기반 **임계값/추세**로 판정. Gen6 FEC 환경에선 더더욱 ([LP17](2026-10-31_LP17_gen5_gen6_retimer_automotive.html)).
5. **한 번 성공으로 "고쳤다"** — 간헐 버그는 실패율과 반복 횟수로만 판단 (rule of three).
6. **여러 손잡이를 동시에 바꿈** — ASPM off + Gen1 + 다른 카드 동시 → 무엇이 고쳤는지 모름.
7. **`rev ff`를 "장치 없음"과 같은 것으로 취급** — `rev ff`는 *열거된 적은 있는데 지금 응답 없음* (런타임 링크 다운/전원 상실). 처음부터 없음(S1)과 원인 영역이 다르다.
8. **MSI를 "선(wire)"처럼 생각** — MSI는 메모리 쓰기 → Bus Master, IOMMU, 주소 라우팅이 모두 필요 (S5).
9. **DPC가 링크를 끊은 것을 버그로 오인** — DPC는 의도된 격리. 진짜 질문은 "무엇이 DPC를 트리거했나" [12].
10. **증상 층에서 디버그 시작** — NVMe timeout(위층 증상)의 원인이 고온 margin(아래층)인 경우가 흔함. 항상 아래층 증거부터 확보.

## 8. 출처

1. Linux kernel, `drivers/pci/pcie/aer.c` (mainline, 2026-10 확인: `aer_print_error`, `aer_print_source`, 에러 이름 배열, sysfs `aer_dev_*`, ratelimit 속성) — https://github.com/torvalds/linux/blob/master/drivers/pci/pcie/aer.c
2. Linux kernel v6.6, `drivers/pci/pcie/aer.c` (구 형식 `PCIe Bus Error: severity=%s, type=%s, (%s)`, `"Corrected"`) — https://github.com/torvalds/linux/blob/v6.6/drivers/pci/pcie/aer.c
3. Linux kernel, `drivers/pci/controller/dwc/pcie-designware.c` (mainline `Link failed to come up. LTSSM: %s`, `PCIe Gen.%u x%u link up`; v6.6 `Phy link never came up`) — https://github.com/torvalds/linux/blob/master/drivers/pci/controller/dwc/pcie-designware.c , https://github.com/torvalds/linux/blob/v6.6/drivers/pci/controller/dwc/pcie-designware.c
4. Linux kernel, `drivers/pci/controller/dwc/pcie-designware.h` (`PORT_LINK_DEBUG0` 0x728, `PORT_LINK_DEBUG1` 0x72C, LTSSM enum) — https://github.com/torvalds/linux/blob/master/drivers/pci/controller/dwc/pcie-designware.h
5. Linux kernel, `drivers/pci/controller/dwc/pcie-designware-debugfs.c` (`ltssm_status`, `rasdes_event_counter`, `rasdes_err_inj`) — https://github.com/torvalds/linux/blob/master/drivers/pci/controller/dwc/pcie-designware-debugfs.c
6. Linux kernel, `drivers/pci/controller/pcie-xilinx-nwl.c` (`PS_LINKUP_OFFSET` 0x238, `PHY link never came up`, `Link is UP/DOWN`) — https://github.com/torvalds/linux/blob/master/drivers/pci/controller/pcie-xilinx-nwl.c
7. Linux kernel, `drivers/pci/controller/pcie-xilinx.c` (downstream config 접근 전 link-up 확인, misc IRQ 메시지) — https://github.com/torvalds/linux/blob/master/drivers/pci/controller/pcie-xilinx.c
8. Linux kernel, `drivers/pci/setup-res.c` (mainline `can't assign; no space`, v6.6 `BAR %d: no space for %pR`) — https://github.com/torvalds/linux/blob/master/drivers/pci/setup-res.c , https://github.com/torvalds/linux/blob/v6.6/drivers/pci/setup-res.c
9. Linux kernel, `drivers/pci/pci.c` (`pci_dev_wait`, `Unable to change power state`, `Data Link Layer Link Active not set`, `__pcie_print_link_status`) — https://github.com/torvalds/linux/blob/master/drivers/pci/pci.c
10. Linux kernel, `drivers/pci/pci.h` (`PCIE_T_PVPERL_MS 100`, `PCIE_RESET_CONFIG_WAIT_MS 100`, `PCIE_LINK_WAIT_MAX_RETRIES 10`, `PCIE_LINK_WAIT_SLEEP_MS 90`) — https://github.com/torvalds/linux/blob/master/drivers/pci/pci.h
11. Linux kernel, `drivers/pci/quirks.c` (`pcie_failed_link_retrain`) — https://github.com/torvalds/linux/blob/master/drivers/pci/quirks.c
12. Linux kernel, `drivers/pci/pcie/dpc.c` — https://github.com/torvalds/linux/blob/master/drivers/pci/pcie/dpc.c
13. Linux kernel, `drivers/pci/hotplug/pciehp_ctrl.c` — https://github.com/torvalds/linux/blob/master/drivers/pci/hotplug/pciehp_ctrl.c
14. Linux kernel, `drivers/nvme/host/pci.c` (`controller is down; will reset`, `timeout, completion polled`) — https://github.com/torvalds/linux/blob/master/drivers/nvme/host/pci.c
15. Linux kernel, `kernel/irq/spurious.c` (`nobody cared`) — https://github.com/torvalds/linux/blob/master/kernel/irq/spurious.c
16. Linux kernel, `drivers/iommu/arm/arm-smmu-v3/arm-smmu-v3.c` (event 로그 형식, `F_TRANSLATION`) — https://github.com/torvalds/linux/blob/master/drivers/iommu/arm/arm-smmu-v3/arm-smmu-v3.c
17. Linux kernel, `include/uapi/linux/pci_regs.h` (LNKSTA 비트, `PCI_ERR_COR_*`) — https://github.com/torvalds/linux/blob/master/include/uapi/linux/pci_regs.h
18. pciutils `lib/header.h` + `ls-caps.c` (`LnkSta2` Retimer/2Retimers/FltMode 출력) — https://github.com/pciutils/pciutils/blob/master/lib/header.h , https://github.com/pciutils/pciutils/blob/master/ls-caps.c
19. Linux kernel docs, PCI Express AER driver guide — https://docs.kernel.org/PCI/pcieaer-howto.html
20. pcilmr(8) man page (Lane Margining at Receiver, 16 GT/s 이상 필수) — https://man7.org/linux/man-pages/man8/pcilmr.8.html
21. LKML, "PCI: dwc: Rework the error print of dw_pcie_wait_for_link()" (2026-01) — https://lkml.iu.edu/2601.2/02869.html
22. Linux kernel, `drivers/pci/probe.c` (`can't handle BAR above 4GB`, MPS 메시지) — https://github.com/torvalds/linux/blob/master/drivers/pci/probe.c
23. Microchip PCI100x Switchtec 소개 (AEC-Q100 Grade 2, −40 ~ +105 °C) — https://www.mouser.in/new/microchip/microchip-pci1001-3-4-5-pcie-switches
