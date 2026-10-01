# BSP 시리즈 작성 스펙 (`bsp/`)

기존 `NOTES_SPEC.md`의 공통 규칙(한국어 기본, 마크다운 부분집합, 중첩 목록 금지, raw HTML 금지, 코드 밖 별표 금지, 정확성 규칙, 지어낸 API 금지)을 그대로 따른다.

## 왜 이 시리즈가 생겼나
2026-09-21, Hark가 Don이 지원하는 공고(Greenhouse id 4186968009)의 제목을
`Embedded Software Engineer` → **`Embedded Software Engineer, BSP`** 로 바꿨다. 본문은 한 글자도 바뀌지 않았다.
같은 날 다른 공고(4201692009, 원래 Embedded Application Engineer)가 `Embedded Software Engineer` 이름을 가져갔다.
즉 **자리 5개를 역할별로 구분하기 시작했고, Don의 자리는 BSP 축으로 규정됐다.**

JD 원문의 해당 문장: "Own BSP development, peripheral driver integration (SPI, I2C, UART, I2S), and RTOS task scheduling".

## 독자 상황 (중요)
- Don은 **BSP를 처음부터 만들어 본 적이 없다.** 레쥬메 근거가 없어서 레쥬메에서도 뺐다.
- 대신 그 아래층은 강하다: ARM Cortex-R8/R82/M0+ FPGA pre-silicon bring-up, I2C/SPI/DMA/PCIe/SRAM/DRAM bring-up, bare-metal C/C++, Trace32/JTAG/DSO/LA, Apple에서 새 실리콘 통합과 factory test-node.
- 그러므로 이 시리즈의 목표는 **"BSP를 해봤다"고 말하게 만드는 것이 아니라**, BSP의 전체 지도를 정확히 알고, 자기 경험이 그 지도의 어디에 해당하는지 정확히 짚어 말하게 하는 것이다. 과장은 금지.

## 파일과 배정
| 파일 | 내용 |
|---|---|
| `bsp/B00_bsp_overview.md` | (메인 작성) 시리즈 지도, BSP 정의, 이번 제목 변경의 의미, Don 전략 |
| `bsp/B01_bsp_anatomy.md` | BSP 해부: 부트 체인, 클럭·전원 초기화 순서, 핀 먹싱, 메모리 맵·링커, 디바이스 초기화 순서, HAL/드라이버 계층, 보드 리비전 관리 |
| `bsp/B02_zephyr_board_port.md` | Zephyr에서 보드를 실제로 올리는 법: 디렉토리 구성, devicetree, Kconfig/defconfig, 드라이버 바인딩, west 빌드, 보드 리비전, twister. 실습 중심 |
| `bsp/B03_linux_android_bsp.md` | Cortex-A/Linux·Android BSP: 부트 체인(BootROM→TF-A→U-Boot→kernel), device tree, 커널 드라이버·HAL, Yocto/AOSP 개요. Don이 약한 영역의 방어 범위 설정 |
| `bsp/B05_vendor_fpga_integration.md` | (2026-09-29 추가) 벤더 코드 통합·벤더 검증 협업, 하드웨어 스펙 검증, FPGA 두 의미(pre-silicon vs 출하) |
| `bsp/B06_embedded_os_landscape.md` | (2026-09-29 추가) eLinux·AOSP·VxWorks·QNX·RTOS 지형도, hands-on 5단계, bare-metal 번역표, 램프업 |
| `bsp/B04_bsp_interview.md` | 면접 대비: 질문 30개 이상(기초/중급/심화), Don 경험 매핑, 정직 스크립트, 화이트보드용 그림, 체크리스트 |

## 노트 구조 (B01~B04 공통, 400~700줄)
```markdown
# BNN. <제목> — <한 줄 요약>

> **시리즈**: BSP 집중 N/4 · **선행**: <B00 등> · **JD 근거**: "Own BSP development ..."
> **Don 상태**: <✅/🟡/❌ 한 줄>
> **이 노트를 다 읽으면**: <3가지>

## 0. 큰 그림 (ASCII 블록도 필수)
## 1..N 본문 — 개념 → 원리 → 코드/설정 예 → 함정 → "Don 경험과의 접점"
## N+1. 흔한 실수와 증상 (표 6~10행)
## N+2. 면접에서 말하기 (Q/A + 영어 문장)
## N+3. 직접 해보기 (실행 가능한 명령)
## N+4. 요약 & 체크리스트
## 참고 자료 (실존 URL 표)
```

## 리서치 규칙
- Zephyr의 보드 포팅 구조는 **버전에 따라 크게 바뀌었다**(hardware model v2: `board.yml`, `<board>_defconfig`, `Kconfig.<board>`, 보드 이름 `nrf52840dk/nrf52840`). **반드시 공식 문서를 WebFetch로 확인**하고, 확인한 페이지를 참고 자료 표에 적는다. 확인 못 한 것은 "버전마다 다름"으로 표기.
- Linux/Android 쪽도 마찬가지. AOSP·TF-A·U-Boot·Yocto는 개요 수준으로 하되 용어는 정확하게.
- 벤더 SDK(Qualcomm, Ambiq)는 NDA·포털 배포라 공개 URL이 없으면 링크를 만들지 말 것.
- 예시 수치·주소는 `[예시]`로 표기.
