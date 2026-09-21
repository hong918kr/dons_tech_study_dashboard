# JD 리서치 노트 작성 스펙 (`jd/*.md`)

기존 `NOTES_SPEC.md`의 **공통 규칙(한국어 기본, 마크다운 부분집합, 중첩 목록 금지, raw HTML 금지, 코드 밖 별표 금지, 정확성 규칙)을 그대로 따른다.** 이 문서는 구조와 배정만 다르다.

## 이 컬렉션이 기존 노트와 다른 점
- `concepts/C01~C10`는 **주제별 교과서**, `study/S01~S06`은 **면접 드릴**이다.
- `jd/J01~J14`는 **JD 문장 하나 = 노트 하나**인 리서치 노트다. Don이 JD를 위에서 아래로 훑으며 "이 문장이 무슨 뜻이고, 실제로 무슨 일이고, 뭘 물어볼까"를 확인하는 용도.
- 따라서 개념 설명은 **이 노트 안에서 자체적으로 이해되도록** 쓰되, 더 깊은 내용은 `C0x`/`S0x` 노트를 가리킨다(마지막 절). 같은 내용을 통째로 복사하지 말고, **JD 문장 관점에서 다시 쓴다**.
- "리서치 노트"이므로 **실제 문서·스펙·릴리스 노트 기반의 근거**를 붙인다. 버전에 따라 달라지는 것은 반드시 표시한다.

## 파일 구조
```markdown
# JNN. <JD 문장 원문 영어 그대로>

> **분류**: Responsibility N/7 (또는 Requirement N/7) · **관련 개념 노트**: C0x, S0x
> **Don 현재 상태**: ✅ 강함 / 🟡 부분 / ❌ 갭 — 한 줄 근거
> **이 노트를 다 읽으면**: <3가지>

---

## 0. 문장 뜯어보기
<JD 문장을 구(句) 단위로 쪼개서 각각이 무슨 뜻인지 표로. 채용담당자가 이 단어를 왜 골랐는지까지.>

## 1. Hark에서 실제로 하게 될 일 (추정)
<context 파일의 제품 구조 추정(Qualcomm SoC + always-on MCU, 멀티 라디오 웨어러블)을 근거로.
 반드시 [추정] 표기. 하루/한 주 업무가 어떤 모습일지 구체적으로.>

## 2. 핵심 개념
<이 문장을 이해하는 데 필요한 개념을 깊게. ASCII 다이어그램, 표, 코드. 자체 완결적으로.>

## 3. 실무 패턴과 함정
<현업에서 어떻게 구현/운영하는지. 코드 예시와 흔한 실패. 표로 정리.>

## 4. 리서치 — 근거 자료
<이 주제의 표준 문서, 스펙, 대표 오픈소스, 벤더 문서를 표로: 자료 / 무엇을 담고 있나 / 어디를 읽어야 하나 / URL.
 실제로 존재하는 URL만. 버전 의존 정보는 "버전마다 다름" 명시.>

## 5. 예상 면접 질문
<10~15문항. 각 문항:
 ### QNN. <영어 질문 원문>
 **왜 묻나** / **30초 답변**(한국어) / **English answer**(그대로 말할 3~6문장) / **꼬리질문**(2~3개와 짧은 답)>

## 6. Don 매핑
<레쥬메 문장(context 파일 3.1절 근거)만 사용. 강점이면 어떤 스토리로 답할지, 갭이면 어떻게 프레이밍할지.
 지어낸 경험 금지. 확인이 필요한 부분은 "<확인 필요: ...>"로 남긴다.>

## 7. 준비 체크리스트
<`- [ ]` 6~10개. 실행 가능한 것으로.>

## 8. 더 읽기
<관련 개념 노트/스터디 노트와 그 안의 절 번호. 예: "C04 §3 우선순위 역전", "S02 Q15~Q24".>
```

## 길이
노트당 400~700줄. 개념·코드·질문이 실하게 들어가야 한다.

## 배정
| 파일 | JD 문장 |
|---|---|
| `jd/J01_firmware_c_cpp_arm.md` | Develop and maintain embedded firmware in C/C++ targeting ARM-based SoCs and microcontrollers |
| `jd/J02_bsp_drivers_rtos_scheduling.md` | Own BSP development, peripheral driver integration (SPI, I2C, UART, I2S), and RTOS task scheduling |
| `jd/J03_power_thermal_always_on.md` | Optimize power consumption and thermal performance for always-on, battery-powered operation |
| `jd/J04_ota_infrastructure.md` | Build and maintain OTA update infrastructure for reliable field updates |
| `jd/J05_on_device_ai_budgets.md` | Collaborate with the on-device AI team to support model inference within memory and latency budgets |
| `jd/J06_factory_test_calibration.md` | Develop factory test and calibration firmware for manufacturing |
| `jd/J07_debug_hw_sw_tools.md` | Debug complex hardware-software interactions using logic analyzers, oscilloscopes, and JTAG |
| `jd/J08_three_years_experience.md` | 3+ years of professional firmware or embedded systems development |
| `jd/J09_c_cpp_resource_constrained.md` | Strong proficiency in C and/or C++ in resource-constrained environments |
| `jd/J10_arm_cortex_toolchains.md` | Experience with ARM Cortex-M or Cortex-A processors and associated toolchains |
| `jd/J11_rtos_handson.md` | Hands-on experience with RTOS (FreeRTOS, Zephyr, or similar) |
| `jd/J12_wireless_ble_wifi_thread.md` | Familiarity with wireless protocols (BLE, Wi-Fi, or Thread) |
| `jd/J13_schematics_bringup_collab.md` | Comfort reading schematics and working alongside hardware engineers during board bring-up |
| `jd/J14_debug_tools_workflows.md` | Experience with embedded debugging tools and workflows |

## 겹치는 항목 처리
J02/J11(RTOS), J07/J14(디버깅), J01/J09/J10(C·Cortex)처럼 쌍이 겹친다. 규칙:
- **Responsibility 노트(J01~J07)** = "업무 관점". 설계·구현·운영·협업·산출물 중심.
- **Requirement 노트(J08~J14)** = "검증 관점". 면접관이 무엇으로 실력을 판별하는지, 경력에서 어떤 증거를 보여줄지, 갭을 어떻게 메울지 중심. 질문 난이도별(기초/중급/심화) 구성.
- 서로 중복될 내용은 한쪽에서 다루고 다른 쪽에서는 한 줄 요약 + 참조만 한다.
