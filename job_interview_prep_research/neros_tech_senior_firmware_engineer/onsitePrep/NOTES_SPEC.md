# onsitePrep — 작성 규칙

Neros 온사이트(Torrance) 대비. 구성 [확인됨 2026-09-16 리크루터 Devin]:
1. Tour 30분
2. **내 경력 발표 1시간** — 가장 큰 challenge, issue resolving/debugging, bring-up 경험
3. **1:1 기술 인터뷰 여러 개** — 기본 C/C++, **ring buffer 구현**, **generic system design**(드론 설계 아님)
- Senior FW Platform HM도 들어올 수 있음. 이전 면접관: Adam Kibit (Director of Firmware, 09-18), Michael Honor (FW Test 리드, 10-01)
- Michael 인터뷰(10-01) 퀴즈: UART/I2C/SPI 차이, "SW에서 PCIe를 어떻게 쓰나", 8b/10b, Git/PR/rebase, "정의 안 된 것의 테스트 케이스", "done의 의미"
- 들은 정보: FW 15명, FW Test 2명, 개발 ~70 / 전체 ~260, 테스트 FW가 가장 급함(연내 최대 5명), **factory test FW 없음(필요)**, **STM32 사용 → 자체 실리콘 없음**
- 회사 공고 원문: `../research/job_board_snapshots/2026-10-01_fw_embedded_jds.md`
- 컨텍스트: `../neros_tech_senior_firmware_engineer_context.md`

## 폴더
- `plan/` 게임 플랜 · `notes/` 노트 `2026-10-01_N0X_topic.md` · `code/` 컴파일되는 C 코드 + Makefile (`make -C code test`)
- `START_HERE.md` 읽는 순서 · `build_notes_site.py` → `site/` (생성물, 직접 수정 금지)
- 기존 자료 재사용 (복제 금지, 링크만):
  - 링버퍼 7레벨 42문제: `../../research/ringbuffer_research/html/index.html` (노트 md: `../../research/ringbuffer_research/notes/*.md`)
  - Neros C 연습 7토픽 42문제: `../../practice/html/index.html` (01_ring_logging, 02_telemetry_framing, 03_config_store, 04_ipc_budget, 05_embedded_core, 06_isr_timing, 07_sensors_actuators)
  - 드론 아키텍처 노트: `../../practice/html/00_drone_architecture.html`
  - FW Test HM 준비 사이트: `../../firmwareTestEngineerPrep/site/start.html` (N08 테스트·디버깅 시나리오, N09 레쥬메 스토리라인 13개)
  - Anduril C 문제은행: `../../../anduril_firmware_engineer/` 

## 마크다운 (렌더러 지원 부분집합)
- 첫 줄 `# <이모지> 제목`, 바로 다음 `> 요약`
- `##`/`###`, 문단, `-`/`1.` 목록(**중첩 금지**), `- [ ]`, 표(한 행 한 줄), 인용, 코드펜스, `---`, 인라인 `code`/**굵게**/[링크]
- 노트끼리 링크: `2026-10-01_N0X_topic.md` (같은 폴더) 또는 `../plan/...md`. C 코드 링크: `../code/ring_buffer.c`. 바깥 자료는 위 `../../...` 경로 그대로

## 톤과 사실
- 한국어 기본, 기술 용어는 영어. **면접에서 말할 문장은 영어**
- 확실하지 않은 건 `[추정]`. Neros 내부 사실은 공고 문구·면접에서 들은 것만 `[확인됨]`
- **Don 경험은 레쥬메 문장 범위만.** 구체 디테일은 `(Don: …)` 칸으로 비우고 지어내지 않는다
  - Apple (2025-12~): RF-Hardware Chipset Integration, 새 실리콘 통합 루트코즈 (PCIe/I2C/SPMI/RFFE), bring-up→NPI→MP, factory test-node 아키텍처, stress 기반 latent defect, DSO·protocol analyzer, HW safety margin sign-off
  - Solidigm Staff FW (2022-07~2025-11): PCIe 6 차세대 production FW, FPGA 이미지/FW bring-up (Cortex R8/R82/M0+, PCIe/NVMe IP), bare-metal C/C++, SoC verification(I2C, SPI, DMA, PCIe controller, SRAM/DRAM, RTL freeze 전), 성능 튜닝, error reporting/handling scheme, Google/Meta NVMe 2.0 기능
  - SK hynix Senior FW (2021-05~2022-07): NVMe telemetry 디버그 기능(MS/Dell/HPE), Xtensa NAND 데이터패스 FW, media defense 알고리즘(FPGA), JTAG/스코프/LA/파워 애널라이저, chip reliability system(NAND V6/7, PCIe 3/4/5), shmoo·health monitoring debug FW
  - SK hynix App SW (2018-10~2021-05): 챔버 테스트 자동화 web 플랫폼 + SDK(온도·스케줄링·eSSD 상태·UART 시퀀스), 사내 SSD 테스트 인프라
  - Skills: Embedded C/C++, Cortex-M, Xtensa, Trace32, NVMe, PCIe, Python, Linux, Git/Perforce/Jira. 학력: UC Berkeley 응용수학
  - 레쥬메에 **없는 것**: STM32 직접 경험(명시 없음), RTOS 명시, Betaflight/PX4, Bazel, embedded Linux 커널/Yocto

## C 코드 규칙 (`code/`)
- C11, `cc -std=c11 -Wall -Wextra -Werror -O2`(+ 가능하면 `-fsanitize=address,undefined` 테스트) 통과
- 각 파일 상단 주석 첫 줄 = 한 줄 설명 (코드 목록 페이지에 표시됨)
- 테스트는 같은 파일 `main()` 또는 `*_test.c`, `make -C code test`로 전부 실행, 실패 시 exit 1
- 화이트보드에서 15~25분 안에 쓸 수 있는 크기. 주석은 "왜"만
