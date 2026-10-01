# B00. BSP 집중 — 왜 지금 이걸 파야 하는가

> **시리즈**: BSP 집중 0/6 (지도) · **JD 근거**: "Own BSP development, peripheral driver integration (SPI, I2C, UART, I2S), and RTOS task scheduling"
> **⚡ 2026-09-23 갱신**: JD 본문이 개정되어 BSP·bring-up·vendor·FPGA 중심으로 재편됨 → [P02 분석](../plan/P02_jd_change_2026-09-23.html)
> **계기**: 2026-09-21, Hark가 이 공고의 제목을 `Embedded Software Engineer` → **`Embedded Software Engineer, BSP`** 로 변경 (본문은 동일)
> **Don 상태**: 🟡 BSP를 처음부터 소유해 본 적은 없다. 그 아래층(실리콘·주변장치 bring-up)은 강하다
> **이 노트를 다 읽으면**: BSP가 정확히 무엇을 가리키는지 안다 · 제목 변경이 면접에 무엇을 의미하는지 안다 · 내 경험을 BSP 지도 위 어디에 놓을지 안다

---

## 0. 무슨 일이 있었나

| 공고 ID | 9월 20일 제목 | 9월 21일 제목 | 본문 |
|---|---|---|---|
| 4186968009 (지원 대상) | Embedded Software Engineer | **Embedded Software Engineer, BSP** | 한 글자도 안 바뀜 |
| 4201692009 | Embedded Application Engineer | Embedded Software Engineer | 한 글자도 안 바뀜 |

같은 날 다른 공고들도 이름이 정리됐다(Integration Engineer → Member of Technical Staff, Integration 등). 즉 **채용팀이 자리들을 역할축으로 구분하기 시작했고, 이 자리는 BSP 축으로 규정됐다.**

리크루터가 "임베디드를 5명쯤 더 뽑는다"고 한 것과 맞춰 보면 그림이 선명하다. 5자리가 BSP, 애플리케이션(Android), 오디오/DSP, DevOps(빌드·OTA), 그리고 검증 쪽으로 나뉘는 모양새다 `[추정]`.

**면접에 주는 의미는 하나다. tech session에서 BSP 비중이 올라간다.** 이 시리즈는 거기에 대한 답이다.

---

## 1. BSP란 정확히 무엇인가

BSP(Board Support Package)는 **"이 보드에서 소프트웨어가 돌게 만드는 모든 것"** 을 묶은 계층이다. 애플리케이션과 실리콘 사이에 있다.

```
┌──────────────────────────────────────────────┐
│  애플리케이션 / 제품 기능                      │  ← 다른 사람이 만든다
├──────────────────────────────────────────────┤
│  미들웨어 (파일시스템, 통신 스택, 추론 런타임)   │
├──────────────────────────────────────────────┤
│  RTOS 커널 / OS                               │
├══════════════════════════════════════════════┤
│  ★ BSP                                        │  ← 이 자리가 소유
│   · 부트: reset 벡터 → 스타트업 → 커널 진입     │
│   · 클럭·전원 초기화 순서, PMIC, 레일 시퀀스     │
│   · 핀 먹싱 (같은 핀이 GPIO냐 SPI냐)            │
│   · 메모리 맵과 링커 배치                       │
│   · 주변장치 드라이버 (SPI/I2C/UART/I2S/DMA)    │
│   · 디바이스 초기화 순서와 의존성               │
│   · 보드 리비전·변종 관리 (EVT1 / EVT2 / DVT)   │
├──────────────────────────────────────────────┤
│  실리콘 (SoC, MCU) + 보드 (센서, PMIC, 라디오)  │  ← HW 팀이 만든다
└──────────────────────────────────────────────┘
```

핵심 성질 네 가지다.

1. **보드마다 다르다.** 같은 칩이라도 보드가 바뀌면 BSP가 바뀐다. 핀 배치, 전원 트리, 어떤 센서가 어느 버스에 붙었는지가 전부 보드 고유다.
2. **가장 먼저 동작해야 한다.** 애플리케이션 팀도, AI 팀도, 공장 팀도 BSP가 돌아간 다음에야 일을 시작한다. 새 보드가 오면 **모든 사람이 이 자리를 기다린다.**
3. **실패가 요란하다.** BSP 버그는 "기능이 안 된다"가 아니라 "부팅이 안 된다", "가끔 멈춘다"로 나타난다. 그래서 디버깅 능력이 곧 BSP 능력이다.
4. **문서가 아니라 회로도와 데이터시트가 원천이다.** 그래서 JD가 "reading schematics"를 필수로 넣었다.

### 1.1 BSP가 아닌 것
- 제품 기능 로직 (wake word 판정, UX 동작)
- 클라우드 통신, 앱
- 모델 최적화 자체
경계가 흐릿한 회색지대는 OTA 부트로더, 전력 상태 관리, 공장 테스트 훅이다. 대개 BSP 담당이 같이 본다.

---

## 2. Hark 맥락에서 이 자리가 만질 BSP `[추정]`

컨텍스트 파일 2.2절의 제품 구조 추정(Qualcomm SoC + Android, always-on 저전력 MCU, 멀티 라디오 웨어러블)을 대입하면 이렇게 갈린다.

| 영역 | 무엇을 하나 | 난이도 | 관련 노트 |
|---|---|---|---|
| always-on MCU 쪽 BSP | RTOS(FreeRTOS/Zephyr 계열) 위에서 센서·마이크·햅틱 드라이버, 전력 상태, SoC와의 IPC | 이 자리의 본진일 가능성 | [B01](B01_bsp_anatomy.html) · [B02](B02_zephyr_board_port.html) |
| SoC 쪽 BSP | 벤더 BSP를 받아 보드에 맞게 device tree·드라이버 수정, 부팅 체인 | 벤더 BSP 기반이라 "수정" 중심 | [B03](B03_linux_android_bsp.html) |
| 보드 bring-up | 새 EVT 보드가 오면 전원 → 클럭 → 콘솔 → 버스 → 센서 순으로 살리기 | **Don의 강점 영역** | [C10](../concepts/C10_debugging_bringup_schematics.html) · [J13](../jd/J13_schematics_bringup_collab.html) |
| 보드 리비전 관리 | EVT1/EVT2/DVT가 동시에 굴러다닐 때 하나의 코드로 대응 | 실무의 진짜 골칫거리 | [B01](B01_bsp_anatomy.html) |

---

## 3. Don의 위치 — 정직한 좌표

레쥬메 근거로만 따지면 이렇다.

| BSP 구성 요소 | Don 경험 | 판정 |
|---|---|---|
| 부트 체인, 스타트업 코드 | Cortex-R/M FPGA 이미지 bring-up, bare-metal | 🟡 개념·실무 모두 인접 |
| 클럭·전원 초기화 | 명시 근거 없음. Apple에서 전력 마진 sign-off | 🟡 부분 |
| 핀 먹싱 | 명시 근거 없음 | ❌ |
| 메모리 맵·링커 | bare-metal 개발, SRAM/DRAM bring-up | 🟡 |
| 주변장치 드라이버 | I2C·SPI·DMA·PCIe 컨트롤러 bring-up과 검증 | ✅ (작성 범위는 본인 확인 필요) |
| 디바이스 초기화 순서 | 명시 근거 없음 | ❌ |
| 보드 리비전 관리 | 명시 근거 없음 | ❌ |
| BSP를 통째로 소유 | **없음** | ❌ |
| 보드 bring-up 디버깅 | Apple 신규 실리콘 통합, 인터페이스 장애 root cause | ✅ 강함 |
| RTOS task 스케줄링 | bare-metal 멀티코어. 상용 RTOS 아님 | 🟡 |

**그래서 전략은 "BSP를 했다"가 아니라 "BSP의 가장 어려운 절반을 했다"이다.** BSP 작업의 난이도는 파일을 채우는 데 있지 않고, **보드가 안 켜질 때 원인을 찾는 데** 있다. 그게 Don의 본업이다.

### 3.1 그대로 쓸 문장

> "I haven't owned a board support package end to end. What I have done is the part of it that usually hurts most — bringing up new silicon on a new board and finding out why it doesn't come up. On the SSD controllers that meant standing up firmware on Cortex-R and M cores on FPGA before RTL freeze, getting I2C, SPI, DMA and the memory interfaces alive. At Apple it means owning the failures at the chipset-to-system boundary. Writing the board files is learnable in weeks; the debugging judgment is what took me years."

이 문장은 **사실만 담고 있다.** 마지막 문장이 핵심이다. 면접관이 BSP 경험 부족을 우려할 때, 그가 진짜로 걱정하는 건 "새 보드에서 막히면 이 사람이 풀 수 있나"이기 때문이다.

### 3.2 하면 안 되는 말
- "BSP를 개발했습니다" — 근거 없음
- "Zephyr로 보드를 포팅해 봤습니다" — 실습 전에는 금지. 실습 후에는 "개인 프로젝트로"를 붙여서 가능
- "device tree를 다뤄 봤습니다" — 실습 전에는 금지
- 벤더 BSP를 "만들었다"와 "수정했다"를 섞어 말하는 것

---

## 4. 이 시리즈 읽는 순서

| 노트 | 내용 | 우선순위 |
|---|---|---|
| [B01 BSP 해부](B01_bsp_anatomy.html) | 부트 체인, 클럭·전원, 핀 먹싱, 메모리 맵, 초기화 순서, 보드 리비전 | ① 먼저 |
| [B02 Zephyr 보드 포팅](B02_zephyr_board_port.html) | 실제로 보드를 올리는 법. devicetree, Kconfig, 드라이버 바인딩, west | ② 손으로 따라 하기 |
| [B03 Linux·Android BSP](B03_linux_android_bsp.html) | Cortex-A 쪽. 부트 체인, device tree, HAL, AOSP/Yocto. 약한 영역의 방어선 | ③ 개념만 |
| [B04 BSP 면접](B04_bsp_interview.html) | 질문 41개, Don 매핑, 정직 스크립트 | ④ |
| [B05 벤더·FPGA 통합](B05_vendor_fpga_integration.html) | **9/23 JD 신설 항목** — 벤더 코드 통합·검증, 하드웨어 스펙 검증, FPGA 두 의미 | ②½ 스토리 재료 |
| [B06 임베디드 OS 지형도](B06_embedded_os_landscape.html) | **9/23 요건 확대** — eLinux·AOSP·VxWorks·QNX·RTOS, hands-on 등급, 램프업 | ③½ 갭 방어 |

기존 노트 중 겹치는 것은 [C01 Cortex·부트](../concepts/C01_arm_cortex_boot_toolchain.html), [C03 드라이버](../concepts/C03_bsp_peripheral_drivers.html), [C04 RTOS](../concepts/C04_rtos_freertos_zephyr.html), [J02 BSP 업무](../jd/J02_bsp_drivers_rtos_scheduling.html)다. 이 시리즈는 **BSP 관점으로 다시 묶은 것**이라 중복 설명 대신 링크로 연결한다.

---

## 5. 마스터 플랜에 반영할 것

[마스터 플랜](../plan/P01_tech_session_master_plan.html)의 D-7 일정에 이렇게 끼워 넣는다.

| 기존 | 변경 |
|---|---|
| D-5 C01 예외·부트 | **B01 BSP 해부**로 대체 (C01 내용을 포함한다) |
| D-4 C04 RTOS + Zephyr 설치 | **B02 보드 포팅 실습**으로 확장 — blinky에서 멈추지 말고 devicetree를 직접 수정해 볼 것 |
| D-1 온디바이스 AI 개념 | **B04 면접 질문** 우선, 시간 남으면 AI |

**실습 목표를 한 단계 올린다.** 원래는 "Zephyr로 blinky"였지만, 제목이 BSP로 바뀐 이상 **devicetree overlay로 핀을 바꾸고 센서를 하나 붙여 보는 것**까지 가야 한다. 그래야 "board file을 만져 봤다"가 사실이 된다.

---

## 6. 리크루터에게 물을 것

제목 변경은 질문하기 좋은 소재다. 관심과 관찰력을 동시에 보여준다.

> "I noticed the posting is now titled Embedded Software Engineer, BSP. Is that the role we discussed? And how are the five embedded openings split — BSP, application, audio, tooling?"

답을 들으면 준비 범위가 절반으로 줄어든다.

---

## 7. 요약 & 체크리스트

BSP는 보드에서 소프트웨어가 돌게 만드는 계층 전체다. Don은 그 계층을 통째로 소유해 본 적은 없지만, **그 계층이 실패할 때 원인을 찾는 일**을 수년간 해 왔다. 면접 전략은 과장이 아니라 **정확한 좌표 제시**다. 거기에 Zephyr 실습 하나를 얹으면 "배우는 중"이 아니라 "이미 손대고 있다"가 된다.

- [ ] BSP 블록도를 백지에 그릴 수 있다
- [ ] BSP와 애플리케이션의 경계를 한 문장으로 말할 수 있다
- [ ] §3.1의 영어 문장을 안 보고 말할 수 있다
- [ ] §3.2의 금지 표현을 기억한다
- [ ] B01~B04를 순서대로 읽었다
- [ ] Zephyr에서 devicetree overlay를 직접 수정해 봤다
- [ ] 리크루터에게 5자리 구분을 물었다
