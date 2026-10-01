# OSTEP 한국어 노트 작성 규칙 (NOTES_SPEC)

노트 작성자(사람이든 에이전트든)가 지키는 규칙. 독자는 **Don**: SK hynix SSD 컨트롤러 펌웨어 7년, 현재 Apple RF 칩셋 통합(PCIe/I2C/SPMI 디버깅), UC Berkeley 수학. C와 하드웨어는 강하고, OS 이론은 체계적으로 다시 세우는 중. 목표는 AI 가속기 · 로보틱스 · 온디바이스 LLM 칩/시스템 회사(NVIDIA, Google, OpenAI, Anthropic)의 시스템/펌웨어 면접.

## 1. 파일 · 위치

- 노트: `notes/2026-09-30_CNN_<영문_slug>.md` (CNN = 챕터 번호 두 자리, 부록은 `C0B`, `C0D`, `C0I`). 예: `notes/2026-09-30_C04_process.md`
- 원문 추출본(읽기 전용, 고치지 말 것): `book-md/CNN_*.md`, 원문 텍스트: `.work/chapters/NN.txt` (부록은 `B.txt` 등)
- 원본 PDF: `Operating Systems - Three Easy Pieces.pdf` (페이지 번호는 `extract_book.py` 의 CHAPTERS 표)
- 직접 돌린 C 코드: `code/CNN_<name>.c` (노트에도 코드 전문 + 실제 출력 붙이기)
- OSTEP 공식 숙제 시뮬레이터: `.tools/ostep-homework/<topic>/*.py` (python3로 실행됨), 책 예제 코드: `.tools/ostep-code/`

## 2. 노트 구조 (이 순서, 이 제목)

```markdown
# Ch.04 프로세스 — 실행 중인 프로그램이라는 추상화

> 📖 원문: [04. The Abstraction: The Process](../book-md/C04_the_abstraction_the_process.md) · [PDF p.46](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=46) · ⏱️ 읽기 약 40분 · 🔗 선행: [Ch.02](2026-09-30_C02_intro.md)

## 0. 한눈에 보기
(3~5줄 TL;DR. 원문 CRUX 질문을 영어 그대로 인용 + 한국어 번역)

## 1. 5분 복습표
| 용어 | 한 줄 뜻 | 예시 / 비유 |   ← 핵심 용어 8~15개

## 2. … (원문 섹션 흐름대로 본문. 소제목은 "2. 프로세스 상태 (4.4)" 처럼 원문 섹션 번호 병기)

## N. 직접 해보기
## N+1. 펌웨어 엔지니어의 눈으로
## N+2. 면접 질문
## N+3. 자가 점검 & 숙제
## N+4. 다음으로
```

## 3. 본문 쓰는 법

- **한국어, 쉬운 말투**(~다/~한다 체). 영어 용어는 처음 나올 때 `한국어(English)` 로 병기, 이후 업계에서 쓰는 쪽(보통 영어)으로.
- 원문을 번역하지 말고 **이해시키기**: 왜 이 문제가 생기는지 → 가장 단순한 해법 → 그 해법의 문제 → 다음 해법, 의 흐름(OSTEP 스타일)을 살린다.
- 원문의 TIP / ASIDE / CRUX 중 중요한 것은 `> **TIP — …**` 인용 블록으로 요약.
- 숫자 예제(스케줄링 평균 반환시간, 주소 변환, 페이지 테이블 크기, 디스크 접근시간, RAID 용량 등)는 **직접 계산 과정을 보여준다**. 원문 예제 + 새 예제 1개 이상.
- 그림: **챕터당 손으로 만든 SVG 2개 이상** (```svg 펜스 → 빌더가 그대로 삽입). 규칙:
  - `<svg viewBox="0 0 W H" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">` 로 시작, 폭 ≤ 720.
  - 다크모드 대응: 선/글자는 `stroke="currentColor"` / `fill="currentColor"`, 강조색은 `style="stroke:var(--accent)"` / `style="fill:var(--accent)"`, 면 채우기는 `style="fill:var(--accent-soft)"` 또는 `fill="none"`. 하드코딩 색은 빨강 계열 경고(`#d9534f`) 정도만.
  - 화살표는 `<defs><marker id="CNN-arrow" ...>` 처럼 **id 앞에 챕터 번호** (한 페이지 내 id 충돌 방지).
  - XML이 파싱되어야 함 (`&` 는 `&amp;`, `<` 는 `&lt;`).
  - 단순 표/트레이스는 ```text 블록 ASCII 도 OK.
- 마크다운 제약(빌더가 단순함): 표 셀 안에 `|` 금지, 목록 중첩은 한 단계까지, 헤딩은 `##`/`###` 만. HTML은 `<details>` `<summary>` 만 줄 단위로 허용 (`<details>` 한 줄, `<summary>…</summary>` 한 줄, 내용(마크다운), `</details>` 한 줄).

## 4. "직접 해보기" 규칙 (중요)

- 개념을 확인하는 **C 프로그램 1개 이상**을 `code/CNN_<name>.c` 로 저장하고 macOS(Apple clang)에서 실제로 컴파일·실행한다:
  `cc -Wall -Wextra -O0 -pthread code/C05_fork.c -o .work/bin/C05_fork && .work/bin/C05_fork`
  - 경고 0개. 노트에는 코드 전문(```c) + **실제 출력**(```text) 을 붙인다. 출력을 지어내지 말 것.
  - Linux 전용 API(futex, epoll, /proc 등)가 핵심이면 macOS 대체(kqueue, sysctl 등)로 돌리거나, 코드만 싣고 "Linux 전용 — macOS에서 미실행"이라고 명시.
- 해당 챕터에 OSTEP 시뮬레이터가 있으면(`.tools/ostep-homework/<topic>/README.md` 확인) **실제로 1~3개 명령을 돌려** 출력과 해석을 싣는다(`-c` 로 정답 확인). 독자가 그대로 따라 칠 수 있게 `cd .tools/ostep-homework/<topic>` 경로를 적는다.

## 5. "펌웨어 엔지니어의 눈으로"

Don의 경험과 연결해 3~6개 bullet: SSD 컨트롤러 FW(FTL, NAND, NVMe 큐, 인터럽트/폴링, 전원 손실 복구, wear leveling), RTOS/베어메탈 vs 범용 OS, Apple 스타일 SoC(코프로세서, IOMMU/DART, PCIe), AI 가속기(디바이스 메모리, DMA, 커맨드 큐, 유니파이드 메모리, GPU 페이지 폴트) 중 **정말로 관련 있는 것만**. 일반론 말고 구체적인 대응 관계.

## 6. "면접 질문"

4~6개. 실제 시스템/펌웨어 면접에서 나올 법한 질문. 형식:

```markdown
### Q1. fork()와 exec()를 왜 따로 두었나?
<details>
<summary>답 보기</summary>

(3~8줄 모범답안. 핵심 키워드 굵게.)

</details>
```

## 7. "자가 점검 & 숙제"

- 퀴즈 3~5개 (답은 `<details>` 로 접기)
- 원문 Homework 문항 중 꼭 해볼 2~3개를 골라 "무엇을 확인하는 문제인지" 한 줄씩.

## 8. 검증 (끝내기 전에 반드시)

1. 모든 C 코드: 경고 0으로 컴파일 + 실행 출력 붙였는지.
2. SVG: `python3 -c "import re,sys,xml.dom.minidom as m;[m.parseString(b) for b in re.findall(r'\`\`\`svg\n(.*?)\`\`\`', open(sys.argv[1]).read(), re.S)]" notes/<file>.md` 가 에러 없이 끝나는지.
3. `python3 build_site.py` 가 BROKEN LINKS 없이 끝나는지 (다른 에이전트가 동시에 노트를 쓰고 있을 수 있으니, 실패가 **내 노트 링크** 때문인지만 확인).
4. `site/` HTML은 손으로 고치지 않는다. 다른 챕터 노트 파일은 건드리지 않는다.

## 9. 분량

챕터 난이도에 비례. 일반 챕터 md 기준 대략 350~700줄. 원문이 길고 중요한 챕터(06 LDE, 18–20 페이징, 28 Locks, 30 CV, 31 세마포어, 40 FS 구현, 42 저널링, 부록 I Flash SSD)는 더 길어도 된다.

## 10. 노트 파일명 표 (다른 챕터 링크할 때 이 이름 그대로)

| 챕터 | 파일 |
|---|---|
| 02 | notes/2026-09-30_C02_intro.md |
| 04 | notes/2026-09-30_C04_process.md |
| 05 | notes/2026-09-30_C05_process_api.md |
| 06 | notes/2026-09-30_C06_limited_direct_execution.md |
| 07 | notes/2026-09-30_C07_scheduling_intro.md |
| 08 | notes/2026-09-30_C08_mlfq.md |
| 09 | notes/2026-09-30_C09_proportional_share.md |
| 10 | notes/2026-09-30_C10_multiprocessor_scheduling.md |
| 13 | notes/2026-09-30_C13_address_spaces.md |
| 14 | notes/2026-09-30_C14_memory_api.md |
| 15 | notes/2026-09-30_C15_address_translation.md |
| 16 | notes/2026-09-30_C16_segmentation.md |
| 17 | notes/2026-09-30_C17_free_space_management.md |
| 18 | notes/2026-09-30_C18_paging_intro.md |
| 19 | notes/2026-09-30_C19_tlb.md |
| 20 | notes/2026-09-30_C20_smaller_page_tables.md |
| 21 | notes/2026-09-30_C21_swapping_mechanisms.md |
| 22 | notes/2026-09-30_C22_swapping_policies.md |
| 23 | notes/2026-09-30_C23_vax_vms.md |
| 26 | notes/2026-09-30_C26_concurrency_intro.md |
| 27 | notes/2026-09-30_C27_thread_api.md |
| 28 | notes/2026-09-30_C28_locks.md |
| 29 | notes/2026-09-30_C29_concurrent_data_structures.md |
| 30 | notes/2026-09-30_C30_condition_variables.md |
| 31 | notes/2026-09-30_C31_semaphores.md |
| 32 | notes/2026-09-30_C32_concurrency_bugs.md |
| 33 | notes/2026-09-30_C33_event_based_concurrency.md |
| 36 | notes/2026-09-30_C36_io_devices.md |
| 37 | notes/2026-09-30_C37_hard_disk_drives.md |
| 38 | notes/2026-09-30_C38_raid.md |
| 39 | notes/2026-09-30_C39_files_and_directories.md |
| 40 | notes/2026-09-30_C40_file_system_implementation.md |
| 41 | notes/2026-09-30_C41_ffs.md |
| 42 | notes/2026-09-30_C42_crash_consistency_journaling.md |
| 43 | notes/2026-09-30_C43_lfs.md |
| 44 | notes/2026-09-30_C44_data_integrity.md |
| 47 | notes/2026-09-30_C47_distributed_systems.md |
| 48 | notes/2026-09-30_C48_nfs.md |
| 49 | notes/2026-09-30_C49_afs.md |
| B | notes/2026-09-30_C0B_virtual_machine_monitors.md |
| D | notes/2026-09-30_C0D_monitors.md |
| I | notes/2026-09-30_C0I_flash_ssd.md |

노트 안에서 다른 노트로 링크할 때는 같은 폴더이므로 `[Ch.18](2026-09-30_C18_paging_intro.md)` 형태. 원문은 `../book-md/CNN_*.md` (정확한 파일명은 `ls book-md`).
대화(Dialogue) 챕터(01, 03, 11, 12, 24, 25, 34, 35, 45, 46, 50)와 랩 부록(A, C, E–H)은 노트를 만들지 않는다 — 원문 링크만.
