# P03. Hark 임베디드 자리 7개 지도 — 나는 어디에 맞나 (2026-09-23)

> **왜 이 노트**: 이번 주에 Hark가 임베디드 관련 공고를 전부 다시 썼다. 채용 페이지에서 **Embedded Software 5개 + 바로 아래 On-Device Models 2개 = 7개**가 한 덩어리로 보인다
> **결론 한 줄**: **BSP가 1순위, Embedded Software Engineer(시스템)가 2순위**, 나머지 5개는 지원 대상이 아니다
> **이 노트를 다 읽으면**: 7자리가 어떻게 나뉘는지 안다 · 각 자리의 요건과 내 적합도를 안다 · 리크루터에게 무엇을 말할지 안다

---

## 0. 이번 주에 벌어진 일

| 날짜(ET) | 공고 | 변화 |
|---|---|---|
| 9/21 | Embedded Software Engineer → **, BSP** | 제목만 변경 |
| 9/21 | Embedded Application Engineer → **Embedded Software Engineer** | 제목만 변경 |
| 9/22 17:35 | Embedded Software Engineer | 본문 전면 재작성 (Android 앱 → 시스템 SW) |
| 9/22 17:40 | Operating System Architect → **Embedded Software Engineer, Platform** | 제목 변경 + 본문 재작성 (Principal 10년+ → IC 5년+) |
| 9/22 17:54 | Frontier UX Engineer | 본문 재작성 |
| **9/23 12:38** | **Embedded Software Engineer, BSP** | **본문 전면 재작성** → [P02 분석](P02_jd_change_2026-09-23.html) |
| 9/23 12:42 | On-Device AI Inference Engineer → **Embedded AI Engineer** | 제목 변경 + 본문 재작성 |

네 공고(BSP·ESE·Platform·UX)가 모두 같은 문장으로 시작한다.

> "The Embedded Software team owns the software running on Hark's next generation of AI hardware."

즉 **하나의 팀을 네 축으로 쪼개 동시에 채용 중**이다. 리크루터가 말한 "임베디드 5명"이 이것이고, DevOps까지 더하면 정확히 5개다.

```
                  Embedded Software 팀 (한 팀, 5 공고)
   ┌────────────┬────────────┬────────────┬───────────┬──────────┐
   │    BSP     │  Systems   │  Platform  │    UX     │  DevOps  │
   │ ★ Don      │            │            │           │          │
   │ 보드·드라이버 │ 커널~유저~MCU│ OS 서비스   │ 디바이스 UI │ CI/CD·OTA │
   │ 벤더·FPGA   │ RTOS+Linux │ 데몬·프로토콜 │ 프레임워크  │ 빌드·플릿  │
   │ 하드웨어 밀착 │            │            │           │          │
   └────────────┴────────────┴────────────┴───────────┴──────────┘
                        ↕ 인접 팀
   ┌──────────────────────┬─────────────────────────┐
   │  On-Device Models    │  Acoustics Engineering   │
   │  Embedded AI Eng     │  Audio DSP Deployment    │
   │  TL, On-Device AI    │  Acoustic Lab Manager    │
   └──────────────────────┴─────────────────────────┘
```

---

## 1. 7자리 비교표

| # | 자리 | 핵심 요건 | 밴드 | Don 적합도 |
|---|---|---|---|---|
| 1 | **Embedded SWE, BSP** | 5+ yrs FW · C/C++ · Cortex-M/A · **임베디드 OS(eLinux/AOSP/VxWorks/RTOS)** · 무선 친숙 · 회로도·bring-up · 디버깅 툴 | $120–300K | **⭐⭐⭐⭐½** |
| 2 | Embedded SWE (시스템) | 5+ yrs 출하 HW · C/C++ or **Rust** · **디바이스 드라이버 개발** · **RTOS와 bare-metal** · 성능·전력 최적화 · 컴퓨터 구조 · 크로스컴파일 | $120–300K | ⭐⭐⭐½ |
| 3 | Embedded SWE, Platform | 5+ yrs · OS 서비스·프레임워크 구축 · **Unix/BSD Linux, VxWorks, QNX** · MQTT/CoAP · **멀티코어 SoC** · 가상화 | $120–300K | ⭐⭐ |
| 4 | Embedded DevOps | 5+ yrs 임베디드 DevOps · **CI/CD(GitHub Actions/GitLab CI)** · Python·셸 · 빌드 시스템 · **OTA A/B·델타** · Git | $150–300K | ⭐⭐⭐ |
| 5 | Frontier UX Engineer | **8+ yrs UI 개발** · 온디바이스 UI · UIKit/SwiftUI/Compose/Flutter 런타임 · 빠른 프로토타이핑 | $150–350K | ⭐ |
| 6 | Embedded AI Engineer | **5 yrs ML 엔지니어링**(그중 2년 edge ML) · IMU·마이크 센서 파이프라인 · **TFLite/llama.cpp/QNN** · NPU 배포 | $200–450K | ⭐⭐ |
| 7 | TL, On-Device AI Inference | **8–12+ yrs HPC** · 추론 엔진·ML 컴파일러 설계 · 커널 직접 작성 · 팀 리드 | $300–500K | ⭐ |

---

## 2. 자리별 상세

### 2.1 ★ Embedded Software Engineer, BSP — 1순위 (⭐4.5)
지금 진행 중인 그 자리다. 9/23 개정으로 **OTA·온디바이스 AI·factory test가 빠지고, board bring-up·vendor 통합·FPGA 시스템 통합이 들어왔다.** 상세 분석은 [P02](P02_jd_change_2026-09-23.html).

- **맞는 것**: bring-up, 벤더 협업, FPGA 기반 시스템 통합, 회로도, 디버깅 툴(최강), C/C++ 제약 환경, Cortex-M/R, 전력 마진
- **갭**: 임베디드 OS 폭(eLinux/AOSP), 무선 프로토콜 스택
- **왜 1순위인가**: 7개 중 **하드웨어에 가장 가까운 자리**이고, Don의 경력 전체가 그 축에 쌓여 있다

### 2.2 Embedded Software Engineer (시스템) — 2순위 (⭐3.5)
BSP 바로 옆자리다. 커널·유저 스페이스·MCU를 가로지르며 RTOS·bare-metal·embedded Linux를 모두 다룬다.

- **맞는 것**: "Experience developing on **RTOS and bare metal** environments" — bare-metal은 Don의 본진이다. 디바이스 드라이버, 성능·전력 최적화, 컴퓨터 구조, 크로스컴파일도 근거가 있다
- **갭**: 유저 스페이스 서비스, embedded Linux 실무. Rust는 선택지 중 하나라 문제 없음
- **쓸모**: BSP 자리가 차면 **같은 팀 안에서 갈아탈 수 있는 자리**다. 리크루터에게 "BSP가 가장 맞지만 팀 판단에 따라 Systems도 열려 있다"고 한마디 해 두면 선택지가 넓어진다

### 2.3 Embedded Software Engineer, Platform — ⭐2
OS 서비스·데몬·프레임워크·가상화·백엔드 프로토콜이다. 요구 OS가 Unix/BSD Linux, VxWorks, QNX다. **Don의 가장 약한 영역**이고, 지금 준비로는 tech session을 넘기기 어렵다. 지원하지 않는다.

### 2.4 Embedded DevOps Engineer — ⭐3 (숨은 차선책)
의외로 근거가 있다. 2018~2021년에 **사내 SSD 테스트 인프라와 SDK를 구축**하고, 챔버 온도 제어·시나리오 스케줄링·UART 시퀀스 자동화를 만들었다. CI/CD와 빌드 시스템, 플릿 OTA 운영이 핵심인 자리라 그 경험이 직접 닿는다.

- **다만 커리어 방향이 어긋난다.** AI 하드웨어·온디바이스로 가려는 목표에서 툴링 쪽으로 빠진다. **BSP와 Systems가 모두 막혔을 때만** 고려한다

### 2.5 Frontier UX Engineer — ⭐1
8년 이상 UI 개발 경력이 필수다. 해당 없음.

### 2.6 Embedded AI Engineer — ⭐2
밴드가 $200–450K로 매력적이지만 **"5 years of experience in machine learning engineering"** 이 필수다. Don은 ML 엔지니어링 경력이 없다. 센서 파이프라인·NPU 배포·TFLite/QNN 실무도 없다. 지금은 대상이 아니다.

다만 **2~3년 뒤 목표로는 정확히 여기다.** BSP로 들어가 온디바이스 팀과 붙어 일하면서 추론 런타임 경험을 쌓으면 사내 이동 경로가 된다. 컨텍스트 파일의 커리어 방향 ⭐4가 이 경로를 전제로 한 점수다.

### 2.7 Technical Lead, On-Device AI Inference — ⭐1
8~12년 HPC와 추론 엔진·ML 컴파일러 설계, 팀 리드 경험이 필수다. 해당 없음.

---

## 3. 밴드로 본 회사의 값매김

| 밴드 | 자리 |
|---|---|
| $300–500K | TL, On-Device AI Inference |
| $200–450K | Embedded AI Engineer |
| $150–350K | Frontier UX Engineer |
| $150–300K | Embedded DevOps |
| $120–300K | **BSP · Systems · Platform** |

임베디드 3형제가 같은 밴드다. **리크루터가 구두로 말한 base $250K는 이 밴드 상단 구간**이고, 그 위 두 자리(AI 쪽)는 요건상 닿지 않는다. 즉 **$250K는 이 팀에서 받을 수 있는 현실적 상단에 가깝다.** 협상 여지는 base보다 equity와 사이닝에 있다.

---

## 4. 리크루터에게 할 말

지금 상황(연락 대기 중, 공고가 이번 주 내내 개편됨)에서 한 통으로 정리한다.

> "I saw the Embedded Software postings were reorganized this week — BSP, Systems, Platform, UX, DevOps. The BSP role is the closest match to what I do: new-silicon and board bring-up, vendor integration, FPGA-based system integration before silicon, and debugging at the signal and bus level. That's the one we discussed, correct?
>
> If the team ends up filling BSP first, I'd also be open to the Embedded Software Engineer role — the RTOS and bare-metal, driver, and power-optimization requirements there line up with the same work."

두 자리를 동시에 지원하지는 않는다. **리크루터가 배정하게 두는 편이 낫다.** 위 문장은 "BSP 우선, Systems 열려 있음"을 알리는 정도로 충분하다.

---

## 5. 준비에 미치는 영향

| 항목 | 판단 |
|---|---|
| BSP 준비 | 그대로 간다. [B00~B04](../bsp/B00_bsp_overview.html), [P01 마스터 플랜](P01_tech_session_master_plan.html) |
| 추가로 볼 것 | Systems 자리 대비로 **디바이스 드라이버 + 성능·전력 프로파일링**을 한 번 더 — [C03](../concepts/C03_bsp_peripheral_drivers.html), [C05](../concepts/C05_low_power_thermal.html) |
| 버릴 것 | Platform·UX·AI 자리용 준비(가상화, MQTT, UI 프레임워크, ML 런타임 심화)는 하지 않는다 |
| 장기 | Embedded AI Engineer를 2~3년 목표로 두고, 입사 후 온디바이스 팀과 접점을 만든다 |

## 6. 체크리스트

- [ ] BSP가 내가 이야기한 자리가 맞는지 리크루터에게 확인
- [ ] Systems 자리에 열려 있다는 뜻만 전달 (동시 지원은 안 함)
- [ ] Platform·UX·AI 자리는 준비 대상에서 제외
- [ ] $250K가 밴드 상단이라는 점을 협상 기준선으로 기억
- [ ] 2~3년 뒤 Embedded AI Engineer 이동 경로를 커리어 계획에 반영
