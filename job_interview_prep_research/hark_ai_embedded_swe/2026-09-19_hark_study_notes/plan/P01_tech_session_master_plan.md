# P01. 1차 Tech Session 마스터 플랜 — 이 한 판에 집중한다

> **목표**: Hark Embedded Software Engineer **tech session 통과** · **프로세스**: HM 검토 → intro → **tech** → 온사이트 (3단계뿐이라 이 라운드가 관문)
> **⚡ 2026-09-21 변경**: 공고 제목이 **`Embedded Software Engineer, BSP`** 로 바뀜(본문 동일) → BSP 비중 상향. [BSP 집중 시리즈 B00~B04](../bsp/B00_bsp_overview.html)
> **조건**: 임베디드 약 5명 채용 중 · 지원자 적음 · base $250K + 사이닝 $100K + equity 제시 · 지인 추천 경로
> **사용법**: 이 페이지를 매일 열고, 각 항목의 링크를 따라가 보충한다. 링크는 같은 사이트의 노트로 연결된다.

---

## 0. 목표를 한 줄로

**"새 하드웨어를 처음 켜서 동작시키고, 문제가 생기면 신호 레벨까지 내려가 원인을 잡고, 그걸 양산까지 끌고 간 사람"** 이라는 인상을 60분 안에 남긴다.

RTOS API를 외운 사람으로 보이려 하지 않는다. 그 경쟁에서는 진다. **Fitbit·Pixel Watch 출신이 많은 팀에 없는 것**을 판다. 새 실리콘 bring-up, 버스·신호 레벨 디버깅, 공장 테스트 체계다.

통과 판정은 보통 이 셋이다.
- 이 사람에게 보드 하나와 버그 하나를 맡길 수 있는가
- 모르는 걸 모른다고 말하고, 어떻게 알아낼지 설명하는가
- 같이 일하기 편한가 (하드웨어 팀과 싸우지 않고 증거로 말하는가)

---

## 1. 세션 구조 추정과 채점축

60분 기준 추정이다. 리크루터가 형식을 알려주면 이 표를 고친다.

| 시간 | 내용 | 무엇을 보는가 |
|---|---|---|
| 0~5분 | 소개, 아이스브레이킹 | 커뮤니케이션, 자기 설명 능력 |
| 5~20분 | 경험 딥다이브 — "최근에 한 일 하나 설명해 주세요" | 깊이. 얼마나 밑바닥까지 아는가 |
| 20~35분 | 임베디드 개념 Q&A | 기본기. ISR, 메모리, 버스, 전력 |
| 35~50분 | 코딩 또는 디버깅 시나리오 | 실제로 짤 수 있는가, 접근이 체계적인가 |
| 50~60분 | 역질문 | 관심도, 팀에 대한 이해 |

채점축 5개와 Don의 현재 위치다.

| 축 | 나의 상태 | 보충할 노트 |
|---|---|---|
| C / Cortex-M 기본기 | ✅ 강함 (표현만 정리) | [C01 Cortex·부트·툴체인](../concepts/C01_arm_cortex_boot_toolchain.html) · [C02 제약 환경 C/C++](../concepts/C02_c_cpp_constrained.html) |
| HW/SW 디버깅 | ✅ 최강 — 여기서 점수를 번다 | [J07 디버깅 업무](../jd/J07_debug_hw_sw_tools.html) · [C10 디버깅·bring-up](../concepts/C10_debugging_bringup_schematics.html) |
| **BSP (제목에 박힌 축)** | 🟡 통째로 소유한 적 없음 — 정확한 좌표 제시 | [B00 개요](../bsp/B00_bsp_overview.html) · [B01 해부](../bsp/B01_bsp_anatomy.html) · [B04 면접](../bsp/B04_bsp_interview.html) |
| 드라이버 / RTOS | 🟡 갭 — 정직 + 최근 실습 | [C04 RTOS](../concepts/C04_rtos_freertos_zephyr.html) · [J11 RTOS 검증](../jd/J11_rtos_handson.html) |
| 저전력 / 웨어러블 감각 | 🟡 갭 — 숫자 감각만 만들면 됨 | [C05 저전력·발열](../concepts/C05_low_power_thermal.html) · [J03 전력 업무](../jd/J03_power_thermal_always_on.html) |
| 오디오 / 온디바이스 AI | ❌ 갭 — 개념만 방어 | [C08 온디바이스 추론](../concepts/C08_on_device_ml_inference.html) · [J05 AI 협업](../jd/J05_on_device_ai_budgets.html) |

---

## 2. 반드시 만들어 둘 산출물 7개

이 7개만 완성되면 준비는 끝이다. 읽는 게 아니라 **말할 수 있는 상태**로 만든다.

| # | 산출물 | 기준 | 참고 |
|---|---|---|---|
| 1 | Tell me about yourself — 영어 90초 | 안 보고 말해서 90초 안에 끝남 | [컨텍스트 4.6](../../../hark_ai_embedded_swe_context.html) |
| 2 | 스토리 A: 인터페이스 장애 root cause | 증상 → 가설 → 측정 → 원인 → 수정 → 재발방지, 4분 | [S06 STAR](../study/S06_debug_scenarios_stories.html) |
| 3 | 스토리 B: factory test-node 설계 | 무엇을 검사했고 왜 그 항목인지, 3분 | [J06 공장 테스트](../jd/J06_factory_test_calibration.html) |
| 4 | 스토리 C: FPGA pre-silicon bring-up | 첫 부팅 실패를 어떻게 좁혔는지, 3분 | [J13 bring-up 협업](../jd/J13_schematics_bringup_collab.html) |
| 5 | RTOS 정직 스크립트 + 실습 근거 | 5문장. 마지막 문장은 "지금 하고 있는 것" | [J11 §6](../jd/J11_rtos_handson.html) |
| 6 | 전력 숫자 감각 | mAh → 평균 전류 → 하루 사용 시간을 암산으로 | [S03 계산 연습](../study/S03_power_wireless_qa.html) |
| 8 | **"BSP를 소유해 본 적 있나?" 답변 스크립트** | 인정 → 인접 경험 → 판단력으로 마무리 | [B00 §3.1](../bsp/B00_bsp_overview.html) · [B04 §11](../bsp/B04_bsp_interview.html) |
| 7 | 역질문 5개 | 팀 구조·아키텍처에 대한 진짜 궁금증 | 아래 §7.4 |

---

## 3. D-7 역산 계획

하루 2~3시간 기준이다. 날짜가 더 촉박하면 §3.2의 압축 버전을 쓴다.

### 3.1 표준 7일

| 날 | 오전 or 점심 (40분) | 저녁 (90분) | 그날의 산출물 |
|---|---|---|---|
| **D-7** | [C00 로드맵](../concepts/C00_roadmap.html) 훑기 + 이 페이지 정독 | 스토리 A 초안 작성 → 소리 내어 3회 | 스토리 A 4분 버전 |
| **D-6** | [J07 디버깅 업무](../jd/J07_debug_hw_sw_tools.html) §2~3 | [S06](../study/S06_debug_scenarios_stories.html) 시나리오 D01~D04 답해 보기 | 디버깅 접근 5단계 암기 |
| **D-5** | **[B01 BSP 해부](../bsp/B01_bsp_anatomy.html)** (C01 내용 포함) | [B04 면접 질문](../bsp/B04_bsp_interview.html) Q01~Q15 | BSP 계층도·bring-up 순서 백지에서 |
| **D-4** | [C04 RTOS](../concepts/C04_rtos_freertos_zephyr.html) 스케줄러·동기화 | **[B02 Zephyr 보드 포팅](../bsp/B02_zephyr_board_port.html)** — blinky에서 멈추지 말고 devicetree overlay 수정까지 | 산출물 5 + overlay로 핀 바꿔 보기 |
| **D-3** | [C05 저전력](../concepts/C05_low_power_thermal.html) sleep·wake·전류 | [S03](../study/S03_power_wireless_qa.html) 계산 E1~E2 손으로 | 산출물 6 |
| **D-2** | [C03 드라이버](../concepts/C03_bsp_peripheral_drivers.html) I2C·SPI·**I2S** | 문제 [01](../problems/01_spsc_ring_isr.html)·[02](../problems/02_i2s_pingpong.html) 손코딩 후 `make run` | 링버퍼·핑퐁 버퍼 백지에서 |
| **D-1** | [B04 면접 질문](../bsp/B04_bsp_interview.html) 나머지 + [B03 방어 범위](../bsp/B03_linux_android_bsp.html) §8 | 스토리 A·B·C 연속 리허설 + 역질문 확정 | 전체 리허설 1회 |
| **D-0** | 이 페이지 §7~9만 | — | 컨디션 |

### 3.2 압축 버전

**3일밖에 없을 때**: D-7·D-6·D-4·D-2만 한다. 순서는 스토리 A → 디버깅 접근 → RTOS 스크립트 → 코딩 2문제.

**하루밖에 없을 때**: 아래 4개만. 각 30분.
1. 스토리 A를 소리 내어 3회 ([S06](../study/S06_debug_scenarios_stories.html))
2. [J11 §6](../jd/J11_rtos_handson.html) RTOS 정직 스크립트 암기
3. [문제 01 링버퍼](../problems/01_spsc_ring_isr.html) 손코딩 1회 (`make run N=01`)
4. 이 페이지 §6·§7·§8 정독

---

## 4. 코딩 대비 — 이 5문제만

tech session에서 코딩이 나오면 임베디드 전형이다. 리트코드가 아니다. **연습 세트가 준비되어 있다** — [사용법 먼저 읽기](../problems/00_how_to_use.html). 문제만 보고 손으로 풀고, 막히면 해설 노트를 연다. 터미널에서 `coding/` 폴더에 들어가 `make run N=01`로 채점한다.

| 우선순위 | 문제 | 해설 | 왜 이게 1순위인가 |
|---|---|---|---|
| ① | [ISR-safe SPSC 링버퍼](../problems/01_spsc_ring_isr.html) | [해설](../drills/01_spsc_ring_isr.html) | 임베디드 단골 1위. 드라이버·오디오·로그 전부에 나옴 |
| ② | [I2S DMA 핑퐁 버퍼](../problems/02_i2s_pingpong.html) | [해설](../drills/02_i2s_pingpong.html) | Hark가 오디오 기기라 확률 상승 |
| ③ | [레지스터 비트필드와 RMW](../problems/03_reg_bitfield.html) | [해설](../drills/03_reg_bitfield.html) | 5분 워밍업 문제로 자주 나옴 |
| ④ | [고정 크기 memory pool](../problems/04_mem_pool.html) | [해설](../drills/04_mem_pool.html) | "malloc 없이" 요구가 따라붙음 |
| ⑤ | [UART 패킷 파서 FSM](../problems/05_uart_parser.html) | [해설](../drills/05_uart_parser.html) | 통신 + 상태기계 + 복구 한 번에 봄 |

**코딩 중 규칙**: 먼저 말로 설계를 한 문장 말하고 → 시그니처를 쓰고 → 본문을 채운다. 침묵하지 않는다. 경계 조건(가득 참·빔·wrap)을 먼저 말하면 그것만으로 점수가 된다.

---

## 5. 개념 빈출 체크

질문이 나오면 30초 안에 답할 수 있어야 하는 것들이다. 막히는 항목만 링크로 들어간다.

| 주제 | 반드시 답할 수 있어야 하는 것 | 노트 |
|---|---|---|
| ISR | ISR에서 하면 안 되는 일, ISR→task 전달 방법 | [C04](../concepts/C04_rtos_freertos_zephyr.html) |
| volatile | 무엇을 보장하고 무엇을 보장하지 않는가 | [C02](../concepts/C02_c_cpp_constrained.html) |
| 부트 | reset → startup → main, .data/.bss가 언제 준비되나 | [C01](../concepts/C01_arm_cortex_boot_toolchain.html) |
| 우선순위 역전 | 언제 생기고 어떻게 막나 | [C04](../concepts/C04_rtos_freertos_zephyr.html) |
| 스택 | 스택 오버플로를 어떻게 감지하나 | [C04](../concepts/C04_rtos_freertos_zephyr.html) |
| I2C | 버스가 멈췄을 때 복구 절차, 풀업이 왜 필요한가 | [C03](../concepts/C03_bsp_peripheral_drivers.html) |
| SPI | mode 0~3, CS 타이밍 | [C03](../concepts/C03_bsp_peripheral_drivers.html) |
| I2S | BCLK/LRCLK 관계, DMA 더블 버퍼링이 왜 필요한가 | [C03](../concepts/C03_bsp_peripheral_drivers.html) |
| DMA | 캐시·정렬 문제, 완료 인터럽트 처리 | [C01](../concepts/C01_arm_cortex_boot_toolchain.html) |
| 저전력 | sleep 단계, wake source, 평균 전류 계산 | [C05](../concepts/C05_low_power_thermal.html) |
| OTA | 전원이 끊겨도 안 죽는 구조, A/B와 롤백 | [C07](../concepts/C07_ota_secure_boot.html) |
| HardFault | 어떤 레지스터를 보고 무엇을 읽나 | [C10](../concepts/C10_debugging_bringup_schematics.html) |
| 공장 | 양산 테스트에서 무엇을 검사하고 캘 값을 어디 저장하나 | [C09](../concepts/C09_factory_test_calibration.html) |
| 온디바이스 AI | INT8 양자화가 왜 필요한가, arena가 뭔가 | [C08](../concepts/C08_on_device_ml_inference.html) |

---

## 6. 갭 3개 — 그대로 쓸 대응

### 6.1 RTOS

핵심은 **인정 → 전이 → 증명** 3단이다. 자세한 문장은 [J11 §6](../jd/J11_rtos_handson.html)에 있다.

> "I haven't shipped on FreeRTOS or Zephyr. What I have done is the layer underneath — on SSD controllers I worked in a bare-metal multi-core environment: ISR-to-task handoff, DMA completion paths, shared-state synchronization, and priority decisions between latency-critical and background work. The concepts map directly; the API surface is what I'm picking up now. I've been running Zephyr on an nRF52840 for the past week — BLE peripheral, deep sleep, and measuring current."

마지막 문장은 **실제로 했을 때만** 말한다. 오늘 시작하면 이번 주 안에 사실이 된다.

### 6.2 무선

JD가 "familiarity"라고만 썼다. 스택을 짰다고 하지 않는다. 강한 지점은 **호스트와 무선칩 사이 경계**다. 문장은 [J12 §6](../jd/J12_wireless_ble_wifi_thread.html)에 있다.

### 6.3 오디오 / 온디바이스 AI

"모델을 만들지는 않았지만, 메모리와 레이턴시 예산 안에 무언가를 밀어 넣는 일은 계속 해 왔다"로 간다. SRAM 배분, DMA 스케줄링, 성능 튜닝이 같은 종류의 문제다. [J05 §6](../jd/J05_on_device_ai_budgets.html) 참고.

---

## 7. 세션 중 행동 규칙

### 7.1 모르는 질문이 나오면
"That one I don't know" 다음에 **반드시** 붙인다. "Here's how I'd find out" 또는 "Here's the closest thing I've done". 모른다고만 끝내면 감점, 접근을 말하면 가점이다.

### 7.2 코딩 중 막히면
말로 돌아간다. "Let me restate the invariant." 불변식을 소리 내어 말하면 대개 길이 보이고, 면접관도 힌트를 주기 쉬워진다.

### 7.3 설계·디버깅 문제 접근 순서
```
1) 질문 되묻기 — 제약이 뭔가 (전력? 레이턴시? 비용?)
2) 관찰 가능한 사실부터 — 증상은 무엇이고 재현율은?
3) 가설 3개를 확률 순으로 세우기
4) 각 가설을 가장 싸게 죽이는 측정 하나씩
5) 원인 → 수정 → 재발 방지(테스트·모니터링)
```
이 5단계를 **말하면서** 푸는 것이 정답보다 중요하다.

### 7.4 역질문 5개 (하나는 반드시 아키텍처 질문)
1. 기기의 컴퓨트 분할이 애플리케이션 SoC와 always-on MCU 구조인가요? 이 역할은 어느 쪽인가요?
2. 오디오 경로(마이크 → 전처리 → wake word)는 어디가 주인인가요?
3. RTOS와 부트로더는 정해졌나요, 초기 멤버가 정하나요?
4. 지금 하드웨어는 어느 빌드 단계이고, 첫 양산까지 펌웨어 쪽 최대 리스크는 무엇인가요?
5. 임베디드를 여러 명 뽑는다고 들었습니다. 자리들이 어떤 축으로 나뉘나요? (공고 제목이 BSP로 바뀐 걸 봤습니다)

---

## 8. 말하면 안 되는 것

근거가 없거나 확인되지 않은 주장이다. 하나라도 꼬리질문에서 무너지면 전체 신뢰가 깎인다.

| 하지 말 것 | 대신 |
|---|---|
| "FreeRTOS/Zephyr로 제품을 출하했다" | "bare-metal 멀티코어에서 같은 문제를 다뤘다" |
| "BSP를 만들었다" / "보드를 포팅했다" | "실리콘·보드 bring-up과 주변장치 검증을 했다" ([B00 §3.2](../bsp/B00_bsp_overview.html)) |
| "BLE/Wi-Fi 스택을 개발했다" | "무선 칩을 시스템에 통합하고 호스트 경계를 디버깅했다" |
| "OTA 인프라를 구축했다" | "양산 펌웨어의 에러 처리와 telemetry를 설계했다" |
| Apple 내부 수치·코드네임·툴 이름 | "공개할 수 있는 범위에서 말씀드리면" 하고 구조만 |
| 확인 안 된 본인 경험 ([J00](../jd/J00_open_questions.html) 72건) | 기억이 흐리면 "정확히는 기억나지 않지만 구조는" |

---

## 9. 당일 체크리스트

- [ ] 원격이면 카메라·마이크·화면공유 15분 전 점검, 유선 인터넷
- [ ] 코딩 환경 확인 (CoderPad인지 화면공유인지 리크루터에게 미리 질문)
- [ ] 종이와 펜 (버스 파형·블록도를 그리면서 설명하면 훨씬 잘 전달된다)
- [ ] 이력서 출력본 한 장, 옆에 스토리 A·B·C 키워드 메모
- [ ] 물, 그리고 JD 원문 한 장
- [ ] 시작 전 스토리 A를 입으로 한 번 (목 풀기)
- [ ] 역질문 5개를 화면 밖 메모에

---

## 10. 세션 직후 15분

기억이 날아가기 전에 적는다. 다음 라운드 준비가 여기서 나온다.

- [ ] 받은 질문 전부 원문에 가깝게 기록
- [ ] 막혔던 질문과 그때 한 답
- [ ] 면접관 이름·직함·관심사 (온사이트 대비)
- [ ] 그가 흘린 제품 정보 (칩, RTOS, 일정, 팀 구조)
- [ ] 컨텍스트 파일 6. 인터뷰 노트에 붙여넣기

---

## 11. 오늘 당장 할 3가지

1. **지원서 제출.** 자리 5개에 지원자가 적은 지금이 최적이다. 레쥬메는 다듬은 Apple 블록 버전으로 충분하다.
2. **Zephyr 시작.** 보드가 없으면 `west build -b native_sim` 으로라도 오늘 첫 빌드를 돌린다. 이번 주 안에 §6.1의 마지막 문장이 사실이 된다.
3. **[J00 확인 필요](../jd/J00_open_questions.html) 우선 5건 답 정하기.** 특히 공장 test-node의 성격과 SSD 스케줄러 방식. 답변 정확도가 여기서 갈린다.
