# B05. 벤더 코드 통합과 FPGA 시스템 통합 — 남이 준 코드, 남이 만든 실리콘 위에서 제품을 만드는 일

> **시리즈**: BSP 집중 5/5 · **선행**: B01(BSP 해부), B03(7절 "BSP drop"), B04(Q17·Q40), J13(회로도·bring-up 협업) · **JD 근거**: "from board bring-up, **vendor code integrations**, to custom peripheral drivers and integrations, **FPGA-based system integrations**" / "**Working closely with vendors on system integration and validation**" / "**Working with Hardware and Software team to ensure our hardware are built and functioning to the specifications**"
> **Don 상태**: 🟢🟡 반반이다. Apple에서 하는 일이 정확히 "벤더 실리콘이 우리 시스템을 만날 때의 통합·검증"이고, Solidigm의 Cortex-R8/R82/M0+ FPGA pre-silicon bring-up은 5절 (a) 쪽 FPGA와 같은 일이다. 반대로 **제품에 실려 출하되는 FPGA의 비트스트림을 소유해 본 적은 없고**, 벤더 Linux BSP 드롭을 리베이스해 본 적도 없다. 이 노트는 그 경계를 흐리지 않고 또렷하게 말하기 위한 것이다.
> **이 노트를 다 읽으면**: ① 벤더가 실제로 무엇을 배송하고 그것을 우리 트리에 어떻게 넣어야 다음 드롭에서 죽지 않는지 설계안으로 말할 수 있다 ② 벤더에게 보내는 재현 패키지·에스컬레이션 절차와 errata 워크어라운드를 실리콘 리비전으로 게이팅하는 코드 패턴을 쓸 수 있다 ③ "FPGA-based system integrations"가 pre-silicon 검증 FPGA인지 제품에 실리는 FPGA인지 **면접 자리에서 되물어** 갈라내고, 양쪽 모두에 대해 부팅·저장·버전·전력까지 답할 수 있다.

---

## 0. 큰 그림

이 노트는 한 문장으로 묶인다. **"우리가 만들지 않은 것들 위에서 제품을 만든다."** 실리콘은 벤더가 만들었고, 초기 드라이버도 벤더가 썼고, PCB는 HW팀이 그렸고, FPGA 비트스트림은 RTL 엔지니어가 합성했다. 펌웨어는 이 넷이 만나는 지점에 있고, 그래서 **뭐가 틀렸는지 처음 아는 사람이 항상 펌웨어**다.

```
       벤더 (실리콘/모듈/센서)              HW팀 (PCB)            RTL/FPGA팀
   ┌──────────────────────────┐    ┌──────────────────┐   ┌──────────────────┐
   │ SDK · reference driver   │    │ 회로도 · 레이아웃 │   │ RTL · 비트스트림  │
   │ BSP drop · 펌웨어 블롭    │    │ EVT/DVT 보드     │   │ 레지스터 맵 스펙  │
   │ reference schematic      │    │ 전원 트리 · BOM  │   │ ILA 프로브        │
   │ errata · 릴리스 노트      │    │ 실측 · 리워크     │   │ 타이밍 리포트     │
   └────────────┬─────────────┘    └────────┬─────────┘   └────────┬─────────┘
                │ 1절: 무엇을 받나           │ 4절: 스펙대로인가       │ 5절: 어느 FPGA인가
                ▼                            ▼                       ▼
   ╔══════════════════════════════════════════════════════════════════════════╗
   ║                     제품 트리 (우리가 소유하는 저장소)                      ║
   ║   vendor/ (원본 그대로)  +  patches/ (우리 변경, 이유 하나당 한 커밋)        ║
   ║   adapt/  (벤더 API → 우리 인터페이스)   boards/ (우리 보드 기술·리비전)     ║
   ║   2절: 포크해서 죽지 않는 구조                                             ║
   ╚═════════════════════════════════╤════════════════════════════════════════╝
                 ┌───────────────────┴───────────────────┐
                 ▼                                       ▼
        3절: 벤더와 함께 검증                     4절: HW팀에 넘기는 증거
   재현 패키지 · 로그/캡처 · 케이스 · 에스컬레이션    인수 조건 · POST · 마진 데이터 · 사인오프
```

### 0.1 다른 노트와의 역할 분담

| 이미 다른 노트에 있는 것 | 어디 | 이 노트에서 하는 것 |
|---|---|---|
| Linux "BSP drop" 내용물 표, 받은 뒤 첫 주 | B03 7절 | 드롭을 넘어선 **배송물 전체**와 **드롭 사이의 저장소 전략** |
| 벤더 SDK 드라이버 통합 4단계 | B04 Q17 | 그 4단계의 실행 형태: 저장소 레이아웃, 패치 규율, 리베이스 절차, 추적성 |
| 회로도 읽기, HW 버그 리포트, HW냐 FW냐 증명 | J13 2~3절 | **외부 벤더용** 리포트(사내와 규칙이 다르다)와 사인오프 증거 |
| POST / 보드 자가 테스트 | B01 9.2 | POST를 **증거 생산 장치**로 보는 관점 + 마진·특성화 테스트 |
| 보드 리비전 관리 | B01 8절 | **실리콘 리비전**(A0/B0)과 errata 게이팅 — PCB 리비전과 다른 축 |
| EVT/DVT/PVT에서 BSP가 달라지는 것 | B04 Q37 | 단계별 **검증 게이트와 사인오프 산출물** |

---

## 1. 벤더가 실제로 무엇을 배송하나

### 1.1 "벤더"는 한 종류가 아니다

Hark 같은 기기라면 `[추정]` 펌웨어 한 명이 동시에 상대하는 벤더가 대여섯 곳이고, 배송물의 성격과 우리 협상력이 다 다르다.

| 벤더 유형 | 배송물 성격 | 우리 협상력 |
|---|---|---|
| 애플리케이션 SoC (Cortex-A, Android급) | 거대한 BSP drop, 커널 포크, NDA 문서 | 낮음 — 우리가 작은 고객 |
| always-on MCU (저전력 Cortex-M) | SDK + HAL 소스, 예제 중심 | 중간 |
| 무선 모듈/콤보칩 | 펌웨어 블롭 + 호스트 드라이버, 인증 서류 | 낮음~중간 |
| 센서 (IMU, 마이크), 전력(PMIC), 오디오 코덱 | 데이터시트 + "예제 코드" 수준 드라이버, GUI 산출 설정 | 높음 — 대체 가능 |
| FPGA | 툴 + IP + 구성 문서(툴·문서는 공개) | 툴은 공개, IP는 라이선스 |

판단 원칙: **대체 가능한 벤더에는 스펙대로 요구하고, 대체 불가능한 벤더에는 우회로를 미리 설계한다.**

### 1.2 배송물 목록 — 받는 순서대로

| 배송물 | 안에 무엇이 있나 | 펌웨어가 즉시 확인할 것 |
|---|---|---|
| NDA / 라이선스 | 재배포 조건, 오픈소스 의무, 소스 접근 범위 | 출하 이미지에 넣을 수 있는지 |
| 데이터시트 · TRM | 전기적 특성, 타이밍, 레지스터 맵과 리셋 값 | 전원 시퀀스, 두 문서가 **불일치하는 곳** |
| SDK | HAL, 드라이버, 예제, 빌드·링커 스크립트 | 라이선스 헤더, 빌드 가정, RTOS 결합도 |
| reference driver | 그 칩용 드라이버 한 벌 | "예제"인지 "출하 품질"인지 |
| BSP drop | 부트로더, 커널 포크, DTS, 블롭, 툴 (B03 7절) | 릴리스 노트, 알려진 이슈 |
| reference schematic / design guide | 벤더가 검증한 회로·레이아웃 규칙 | 우리 보드가 **어디서 벗어났는지** |
| reference board (EVK) | 동작하는 기준 하드웨어 | 기준선(baseline)을 여기서 만든다 |
| errata / anomaly list | 실리콘 버그와 워크어라운드, 리비전별 | 우리가 쓰는 블록에 해당하는 항목 |
| 릴리스 노트 | 수정·신규·제거·알려진 이슈 | **가장 먼저 읽는 문서** |
| 툴 | flash, 서명, 크래시 파서, 프로파일러 | CI에 넣을 수 있는가 |

### 1.3 reference schematic을 펌웨어가 읽는 이유

회로도 읽기 자체는 J13에 있다. 여기서 중요한 건 **"우리 보드가 reference에서 벗어난 지점 목록"이 초기 펌웨어 버그 목록의 절반**이라는 사실이다. diff를 뜨면 나오는 전형적 항목이다.

- 크리스털/오실레이터가 다른 부품 → 기동 시간·부하 용량 설정이 SDK 기본값과 다르다
- reference에 있던 풀업이 DNP → I2C가 400 kHz에서만 깨진다
- 전원 레일이 합쳐졌거나 always-on에서 스위치드로 바뀜 → 초기화 순서와 저전력 복귀가 달라진다
- 핀 먹싱이 다름 → SDK 기본 핀 테이블을 그대로 쓰면 안 된다
- 안 쓰는 블록의 전원 핀 처리(벤더 가이드의 "반드시 연결" 항목) → 부팅 실패·누설

> "The first artifact I want on a new board is a diff between our schematic and the vendor reference, annotated with what each delta means for firmware. Most of my early bring-up bugs live in that list."

### 1.4 errata(anomaly) 문서 — 읽는 법

errata는 "실리콘이 문서와 다르게 동작하는 목록"이고, **그 수정 책임이 대부분 펌웨어로 온다.** 실리콘을 다시 굽는 것보다 코드로 우회하는 게 싸기 때문이다. Nordic의 nRF52840 errata가 공개 문서라 구조를 그대로 볼 수 있다(검증: 참고 자료 표).

```
[241] SAADC: Static 400 µA current after SAADC is disabled
  Symptoms     : "Static current consumption between 400 µA and 450 µA occurs."
  Conditions   : "SAADC is disabled after sampling with BURST when multiple channels
                  have been enabled."
  Consequences : "Current consumption is higher than expected."
  Workaround   : 문서가 지정한 레지스터 읽기 → 리셋 쓰기 → 복원 시퀀스.
                 적용 후 SAADC 설정이 초기화되므로 전부 다시 설정해야 한다.
```

이 한 항목이 **왜 always-on 배터리 기기에서 치명적인지** 보라. 400 µA는 소형 배터리 기기의 sleep 예산을 혼자 다 먹는 값인데, 증상은 "배터리가 예상보다 빨리 닳는다"뿐이고 코드에는 버그가 없다. errata를 읽지 않으면 몇 주를 태운다.

읽는 실무 순서. ① **우리가 쓰는 블록만** 필터한다(전부 읽으면 아무것도 못 읽는다). ② 각 항목을 Conditions 기준으로 "우리 사용 패턴에 해당하나" 판정하고, 해당 없음도 근거를 적어 둔다. ③ 해당 항목은 워크어라운드를 넣고 **errata 번호를 주석에 남긴다** — 번호 없는 워크어라운드는 6개월 뒤 삭제된다. ④ 리비전마다 다르다는 걸 설계에 반영한다(3.5~3.6절). ⑤ SDK가 이미 워크어라운드를 넣었는지 확인한다 — 중복 적용이 새 버그를 만든다.

### 1.5 patch cadence — 벤더의 시간은 우리 시간과 다르다

| 모델 | 주기 | 우리에게 의미하는 것 |
|---|---|---|
| 정기 드롭 | 월 1회 ~ 분기 1회 | 우리 리베이스 일정을 여기에 맞춰 **예약**한다 |
| LTS / 유지보수 브랜치 | 6~12개월 고정 + 백포트 | 출하 이미지는 여기서 고른다 |
| hotfix / 원샷 패치 | 며칠~몇 주 | 우리 트리에 임시 패치로 들어오고 다음 드롭에서 지워야 한다 |
| 다음 메이저 릴리스에 수정 | 분기~반년 | 우리 일정과 안 맞음 → 워크어라운드 협상 |
| 실리콘 리비전에서 수정 | 6개월~1년+ | 마스크 일정과 양산 일정을 맞춰야 한다 (3.6절) |

여기서 유일하게 중요한 질문은 이것이다. **"이 수정이 우리 출하 이미지에 들어오는 경로가 정확히 무엇인가?"** "다음 릴리스에 넣겠다"는 답이 아니다. 필요한 건 "어느 브랜치, 어느 버전, 언제, 그 전까지 우리가 쓸 워크어라운드는 무엇"이다.

### 1.6 NDA와 라이선스 — 펌웨어가 걸리는 실제 지점

- **문서에 공개 URL이 없다.** 그래서 이 노트도 특정 벤더 내부 구조를 쓰지 않는다. 면접에서도 그 태도 자체가 점수다.
- **재배포 조건**: 펌웨어 블롭을 우리 OTA 이미지로 배포할 수 있는지가 계약 문제다. 늦게 알면 출하 직전에 막힌다.
- **소스 vs 서명된 바이너리**: 일부 스테이지는 소스가 오지 않는다. 디버깅 가능 범위가 여기서 결정된다.
- **컴플라이언스**: 벤더 커널 포크는 GPL이고 우리 수정도 공개 의무가 생긴다. 반대로 SDK는 독점 라이선스가 많아 두 코드를 한 파일에 섞으면 안 된다. **디렉터리 분리는 법적 요구이기도 하다.**
- 안전 문장: "I can talk about how I integrate vendor code in general terms, but not about a specific vendor's internals — that's under NDA."

### 1.7 벤더 코드가 깨는 가정 — 의심 목록

B04 Q17에 통합 4단계가 있으므로 여기서는 점검 목록만 정밀하게 둔다.

| 의심 항목 | 왜 문제인가 | 발견 방법 |
|---|---|---|
| 블로킹 딜레이(busy wait, `k_msleep` 남용) | RTOS 스케줄 위반, 전력 낭비 | 소스 grep, 트레이스 실행 시간 |
| 자체 인터럽트 우선순위 설정 | 우리 실시간 경로를 밀어낸다 | NVIC 설정 덮어쓰기 확인 |
| `malloc`/전역 큰 버퍼 | 메모리 예산 파괴, 단편화 | map 파일, 힙 사용량 |
| 재진입 불가 / 전역 상태 | 두 스레드가 같은 버스를 쓰면 깨진다 | 동시 접근 스트레스 |
| 자체 타이머·스레드 생성 | 우리 스케줄 모델과 충돌 | 스레드 목록 덤프 |
| 저전력 진입/복귀 미고려 | sleep 후 레지스터 소실, 재초기화 누락 | sleep/wake 반복 |
| 에러 경로가 비어 있음 | 실패가 조용히 성공으로 보고된다 | 반환값 무시 지점 grep |
| `volatile` 누락 / 툴체인 가정 차이 | 최적화 레벨·링커를 바꾸면 깨진다 | `-O0`과 `-O2` 양쪽, 경고 전량 확인 |

---

## 2. 통합 — 포크해서 죽지 않는 구조

### 2.1 근본 문제는 타임라인이 두 개라는 것

```
벤더 타임라인   ──drop 1.0──────drop 1.1──────drop 1.2──────drop 2.0──▶
                    │              │              │             │
우리 타임라인   ──EVT──┴─DVT────────┴──PVT─────────┴──MP────────┴──OTA──▶
                    ▲              ▲              ▲             ▲
                 우리 수정 12건   38건          61건     "이제 리베이스 못 함"
```

전략 없이 벤더 코드를 복사해 직접 수정하면 6개월 뒤 상태는 이렇다. 우리 수정 61건이 벤더 코드 전체에 흩어져 있고, 어느 것이 우리 것인지 구분이 안 되고, 새 드롭에 우리가 필요한 수정이 들어 있지만 **리베이스 비용이 기능 개발보다 커서 아무도 안 한다.** 그래서 오래된 SDK로 출하하고, 벤더가 이미 고친 버그를 우리가 다시 디버깅한다. 해결의 핵심은 하나다. **우리 변경을 벤더 코드와 물리적으로 분리하고, 각 변경에 이유와 수명을 붙인다.**

### 2.2 저장소 전략 네 가지

| 전략 | 형태 | 장점 | 비용 | 언제 |
|---|---|---|---|---|
| vendored copy | `third_party/vendor_sdk/` 원본 + `patches/` | 단순, 빌드 재현성 | 충돌을 손으로 해결 | 작은 SDK, MCU HAL |
| git subtree | 벤더 릴리스를 subtree 머지 | 히스토리 보존, 단일 체크아웃 | 머지 커밋이 지저분 | 중간 규모, 잦은 드롭 |
| submodule + 우리 브랜치 | 벤더 포크에 `product/` 브랜치, 리베이스 유지 | 우리 패치가 커밋으로 명확 | 서브모듈 운영, 강제 푸시 | 커널·부트로더급 큰 트리 |
| 오버레이/레이어 (수정 0) | Yocto `bbappend`, AOSP device 디렉터리, Zephyr module + DT overlay, Kconfig | 벤더 코드를 **안 건드린다** = 리베이스 비용 0 | 프레임워크 지원 범위 안에서만 | 가능하면 항상 1순위 |

**판단 규칙: 오버레이로 되는 것은 전부 오버레이로 하고, 안 되는 것만 패치로 내려간다.** 내려가는 순서는 ① 설정(Kconfig/defconfig/빌드 변수) ② 보드 기술(devicetree overlay, `.dts`) ③ 우리 파일 추가(어댑터가 벤더 API 호출) ④ 빌드 우선순위로 파일 교체(`bbappend`, 링커 심볼 오버라이드) ⑤ 그래도 안 되면 **패치**, 그리고 그 패치를 upstream에 보낸다.

### 2.3 패치 규율 — 이게 전부다

```
patches/vendor_sdk/
  0001-i2c-do-not-set-nvic-priority-in-driver-init.patch
  0002-adc-apply-errata-241-workaround-on-rev1-silicon.patch
  0003-uart-add-dma-abort-path-for-our-idle-timeout.patch
  0004-HACK-disable-vendor-watchdog-until-vendor-case-88231.patch
  series                 <- 적용 순서
```

| 패치마다 붙는 메타데이터 | 왜 필요한가 |
|---|---|
| 이유 한 줄 ("무엇이 깨지는가") | 리베이스 때 아직 필요한지 판단 |
| 수명 (`permanent` / `until vendor <case #>` / `until SDK >= x.y`) | 지워도 되는 시점이 코드에 적혀 있다 |
| upstream 상태 (`sent`/`accepted`/`local-only`/`wontfix`) | 영구 유지 비용을 가늠 |
| 영향 범위 (보드·실리콘 리비전) + 검증 방법 | 조건부 적용 판단, 회귀 테스트로 승격 |

규칙 셋. ① **한 커밋 = 한 이유** — 두 이유를 섞은 패치는 다음 드롭에서 반은 필요하고 반은 충돌한다. ② **`HACK:`/`TEMP:` 접두어를 강제**한다 — 임시 패치가 영구처럼 보이는 순간부터 트리가 썩는다. ③ **되는 것은 벤더에 보낸다** — upstream에 들어간 패치는 다음 드롭에서 자동으로 사라진다.

### 2.4 어댑터 층 — 앱이 벤더 API를 직접 부르지 않는다

```c
/* adapt/imu_adapter.c — 앱은 vendor_* 심볼을 절대 보지 않는다.
   SDK 교체의 충격이 이 파일 하나에 갇히고, errata 워크어라운드도 여기에 모인다. */
#include "imu.h"              /* 우리 인터페이스 (앱이 보는 유일한 헤더) */
#include "vendor_imu_sdk.h"   /* 벤더 헤더 — 이 파일 밖으로 나가지 않는다 */

struct imu_dev { vendor_imu_handle_t h; uint8_t si_rev; };  /* 벤더 타입을 숨긴다 */
static struct imu_dev g_imu;

int imu_init(const struct imu_config *cfg)
{
    vendor_imu_cfg_t vcfg = { .odr_hz = cfg->sample_rate_hz,
                              .range_g = cfg->range_g, .use_fifo = 1 };

    /* 벤더 반환 규약을 우리 규약(-errno)으로 번역한다. 앱에 노출하면 앱이 벤더에 묶인다. */
    if (vendor_imu_open(&g_imu.h, &vcfg) != VENDOR_IMU_OK)                 return -EIO;
    if (vendor_imu_read_revision(g_imu.h, &g_imu.si_rev) != VENDOR_IMU_OK) return -EIO;

    if (g_imu.si_rev < 0x02) {
        /* vendor errata #17: FIFO watermark IRQ가 첫 샘플에서 누락된다.
           워크어라운드: 초기화 직후 FIFO 비우기. 수명: until rev B0 (vendor case 88231) */
        (void)vendor_imu_fifo_flush(g_imu.h);
    }
    return 0;
}
/* 읽기 경로도 같은 원칙: 벤더가 블로킹 API만 주면 여기서 우리 계약으로 감싼다. */
```

이 층의 이득 넷: SDK 버전을 올릴 때 깨지는 파일이 하나로 한정된다(컴파일러가 찾아준다) · 칩을 바꿀 때 앱이 그대로다(B04 Q18) · 워크어라운드가 한 곳에 모여 감사 가능하다 · 벤더 API를 스텁으로 바꿔 하드웨어 없이 앱을 테스트할 수 있다(B04 Q21).

### 2.5 업그레이드하는 날의 절차

리베이스는 "언젠가 하는 일"이 아니라 **정해진 절차**여야 한다. 안 그러면 영원히 안 한다.

1. 릴리스 노트와 알려진 이슈를 먼저 읽고, upstream에 들어간 우리 패치를 먼저 **지운다**.
2. 레퍼런스 보드(EVK)에서 **벤더가 준 그대로** 빌드·부팅해 새 기준선을 만든다(B03 7.1과 같은 원칙).
3. 우리 패치를 순서대로 재적용하고, 충돌마다 "아직 필요한가"를 판정한다.
4. 우리 보드로 옮겨 **버스 단위로 순차 확인**: 콘솔 → 전원/클럭 → I2C/SPI → 센서 → 오디오 → 무선.
5. 회귀 테스트와 **전력 프로파일**을 돌린다. SDK 업그레이드의 전형적 후과가 sleep 전류 증가다.
6. 차이를 문서로 남긴다: 무엇이 좋아졌나, 무엇이 깨졌나, 어느 패치를 지웠나.
7. 게이트를 통과하면 병합하고 **출하 이미지에 들어간 SDK 버전을 기록**한다(2.6절).

흔한 실수는 4·5번을 건너뛰는 것이다. SDK 업그레이드는 기능 변화가 없어 보여 "빌드되고 부팅되면 끝"으로 취급되고, 3주 뒤 배터리 수명 회귀로 돌아온다.

### 2.6 버전 추적성 — 이미지가 자기 출처를 말해야 한다

```c
/* build_info.c — 빌드 시스템이 생성한다. 손으로 쓰지 않는다.
   링커에서 고정 주소에 배치하면 크래시 덤프·factory 툴이 펌웨어 없이도 읽는다. */
const struct build_info __attribute__((section(".build_info"), used)) g_build_info = {
    .magic = 0x424C4431u, .fw_version = { 1, 4, 0 },
    .git_sha        = FW_GIT_SHA,             /* 빌드 시 -D로 주입 */
    .vendor_sdk     = VENDOR_SDK_VERSION,     /* 예: "3.7.2" */
    .vendor_patches = VENDOR_PATCH_COUNT,     /* patches/series 줄 수 */
    .patch_hash     = VENDOR_PATCH_HASH,      /* series 전체의 해시 */
    .board_rev_exp  = BOARD_REV_SUPPORTED_MASK,
    .bitstream_ver  = FPGA_BITSTREAM_VERSION, /* 5.3절 */
    .build_utc      = BUILD_UTC,
};
```

이게 있으면 필드 로그 하나로 "어느 SDK + 어느 패치 세트 + 어느 비트스트림"이 답해진다. 없으면 재현이 불가능하고, **벤더에 버그를 보고할 수도 없다** — 3.2절에서 벤더가 제일 먼저 묻는 값들이 바로 이것이다.

통합이 죽는 방식은 셋뿐이고 전부 6절 표에 증상·예방과 함께 있다. **포크 드리프트**(벤더 최신 대비 뒤처져 리베이스 불가), **벤더 API 누출**(앱 전역이 `vendor_*`를 부름), **임시 패치의 영구화**.

### Don 경험과의 접점

Don이 정직하게 서 있을 수 있는 자리는 2.4~2.5절이다. **"새 실리콘/새 칩이 시스템에 들어올 때 무엇이 깨지는지 안다"**가 Apple 경험의 핵심이고, 2.5절 4번(버스 단위 순차 확인)은 Don이 매번 하는 순서다(J13 3.2와 같은 뼈대). 반대로 2.2절 저장소 전략은 **설계안으로는 말해도 "해봤다"고는 말하지 않는다.** 안전한 형태는 "내가 하겠다는 방식"으로 말하는 것이다. <확인 필요: 벤더 SDK나 드라이버 소스를 우리 트리에 넣고 관리한 경험이 있는지, 있으면 어느 수준까지였는지 — 있으면 이 절의 강도를 크게 올릴 수 있다>

---

## 3. 검증 — 벤더와 함께 버그를 잡는다

JD 문장이 "integration **and validation**"이다. 통합은 코드 문제고, 검증은 **절차와 증거 문제**다. 시니어와 주니어가 제일 크게 갈리는 지점이다.

### 3.1 먼저 3분류로 가른다

벤더에 티켓을 열기 전에 이 분류를 스스로 끝내야 한다. 틀린 티켓은 "works on our EVK"로 돌아오고 2주를 잃는다.

| 분류 | 판정 근거 | 조치 |
|---|---|---|
| 우리 버그 | 벤더 EVK + 벤더 코드 그대로에서 재현 안 됨, 우리 변경을 되돌리면 사라짐 | 우리가 고친다. 보내지 않는다 |
| 통합 버그 | 양쪽 모두 정상인데 합치면 깨짐(타이밍, 전원, 우선순위, 동시성) | 우리가 주도, 벤더에 **질문** 형태로 협조 요청 |
| 벤더/실리콘 버그 | **벤더 EVK + 벤더 코드 그대로**에서 재현됨 | 재현 패키지 만들어 티켓. 유일하게 강한 티켓 |

핵심 기술은 **"벤더 EVK에서 최소 재현"을 만드는 능력**이다. 우리 보드·우리 코드에서만 나는 현상은 벤더에게 증거가 아니다. EVK에서 재현되는 순간 논쟁이 끝난다. 재현이 안 되면 그것도 정보다 — 우리 보드의 무엇이 다른지(1.3절 diff 목록)로 돌아간다.

### 3.2 재현 패키지 — 벤더가 실제로 요구하는 것

| 항목 | 구체적으로 | 왜 요구하나 |
|---|---|---|
| 하드웨어 식별 | EVK 모델·리비전, 실리콘 리비전, 마킹 사진, 리워크 여부 | 리비전별로 errata가 다르다 |
| 소프트웨어 식별 | SDK 버전, 패치 목록, 툴체인 버전, 컴파일 옵션 | 이미 고친 버그인지 즉시 확인 |
| 최소 재현 코드 | 벤더 예제를 최소 수정한 **빌드 가능한** 프로젝트 전체 | 벤더가 자기 책상에서 실행해야 한다 |
| 재현 절차 | 번호 매긴 단계 + 재현율("20회 중 3회") | 재현율이 낮으면 오래 걸린다는 사전 합의 |
| 기대 vs 실제 | 문서의 어느 문장을 근거로 기대했는지 | "문서와 다르다"가 가장 강한 주장 |
| 로그 | 타임스탬프 있는 UART/RTT 로그, 실패 직전 구간 | 시간 순서가 원인 추론의 전부 |
| 레지스터 덤프 | 실패 시점의 관련 블록 레지스터 전체 | 벤더는 우리가 안 보는 비트를 본다 |
| 버스 캡처 · 파형 | 로직 분석기 세션 파일, 스코프 캡처(전원 램프, 리셋 타이밍) | 물리 계층 논쟁을 끝낸다 |
| 크래시 산출물 | 코어 덤프, fault 레지스터, 스택, 심볼 파일 | 벤더 파서로 바로 분석 |
| 영향 진술 | 어느 마일스톤이 막히는지, 언제까지 필요한지 | 우선순위 배정의 유일한 입력 |

이 목록을 사내 템플릿으로 만들면 팀 전체의 벤더 응답 시간이 줄어든다. **면접에서 "내가 팀에 가져올 것"으로 말할 수 있는 구체적 항목**이기도 하다.

### 3.3 그대로 쓸 수 있는 벤더 리포트 템플릿

J13 3.4의 사내 HW 버그 리포트와 다르다. 사내는 "같이 보자"이고, **벤더는 "당신 책상에서 재현된다"를 증명하는 문서**다.

```
Title: [<part>] <block>: <one-line observable symptom> on <hw/sw rev>
Summary   What we observe, on what hardware, with what software, and why we believe it
          is in your code/silicon rather than ours.
Environment  Board: <vendor EVK model + rev> (also reproduced on our board rev B)
             Silicon rev: <read from device + package marking>
             SDK/BSP: <version + our patch count, patches attached>
             Toolchain: <compiler + version + optimization level>
Steps     1. Build attached project (unmodified vendor sample + <N> line diff)
          2. ...  3. Observed in 3 of 20 cold boots; never on warm reset.
Expected  <quote the document>  -- <doc name>, section <x.y>
Actual    <what happens>  (log: attached, line 214; register dump: attached)
Attachments  repro/ (buildable), logs/, regdump.txt, la_capture.sal, scope_*.png
Impact    Blocks <milestone> scheduled <date>. We need a fix or a documented
          workaround by <date>.
What we already ruled out
  - Our patches reverted: still reproduces
  - Power rails measured within spec (attached)   - Reproduces at -O0 and -O2
```

마지막 블록이 이 문서의 힘이다. **"What we already ruled out"이 있으면 벤더가 첫 답장에서 그 질문들을 되묻지 못한다.**

### 3.4 에스컬레이션 — 경로를 미리 알아 둔다

| 단계 | 상대 | 언제 | 무엇을 요구하나 |
|---|---|---|---|
| 0 | 우리 팀 내부 | 항상 먼저 | 3.1 분류 완료 |
| 1 | FAE (field application engineer) | 일상 | 기술 질문, 티켓 생성 |
| 2 | 벤더 지원 포털 케이스 | 재현 패키지 완성 후 | 케이스 번호 = 추적 단위 |
| 3 | 팩토리/설계 엔지니어링 | FAE가 답을 모를 때 | 실리콘 설계팀 의견 |
| 4 | 어카운트 매니저 / 우리 관리자 | 일정이 막힐 때 | 우선순위 재배정 |
| 5 | 경영진 대 경영진 | 마일스톤이 위태로울 때 | 마지막 카드 — 아껴 쓴다 |

원칙 셋. **케이스 번호를 코드와 트래커에 적는다**(임시 패치 수명의 근거, 2.3절). 에스컬레이션은 사람을 비난하는 게 아니라 **우선순위를 올리는 절차**이므로 톤을 사무적으로 유지한다. 3단계로 올리는 건 **재현 패키지가 완성된 뒤**다 — 그 전에 올리면 신뢰를 잃고 다음에 안 먹힌다. 올린 뒤에는 주기적 상태 업데이트를 우리가 요구한다. 조용해지면 멈춘 것이다.

### 3.5 errata 워크어라운드를 코드에 넣는 방식

워크어라운드를 무조건 적용하면 실리콘이 고쳐진 뒤에도 성능·전력 손실을 계속 낸다. **리비전으로 게이팅**하는 게 정답이다.

```c
/* si_quirks.c — 실리콘 리비전별 워크어라운드 테이블.
 *  1) 리비전은 런타임에 읽는다 (빌드 타임 #ifdef 금지: 한 이미지가 여러 리비전 지원)
 *  2) 각 항목에 벤더 문서·케이스 번호를 남긴다
 *  3) 미지의 리비전은 "워크어라운드 없음 + 경고 로그"가 안전한 기본값이다
 *     — 미래 실리콘에 과거 워크어라운드를 적용하는 게 더 위험하다 */
enum si_quirk {
    QUIRK_ADC_DISABLE_LEAK   = 1u << 0,  /* vendor anomaly: ADC 비활성 후 정적 전류 */
    QUIRK_I2C_NEEDS_BUS_KICK = 1u << 1,  /* vendor case 88231 */
    QUIRK_QSPI_NO_DEEP_SLEEP = 1u << 2,  /* vendor errata, 초기 리비전만 */
};

struct si_quirk_entry { uint8_t rev; uint32_t quirks; };

/* [예시] 실제 리비전 코드 값은 벤더 문서에서 확인해야 한다 */
static const struct si_quirk_entry k_table[] = {
    { 0x00, QUIRK_ADC_DISABLE_LEAK | QUIRK_I2C_NEEDS_BUS_KICK | QUIRK_QSPI_NO_DEEP_SLEEP },
    { 0x01, QUIRK_ADC_DISABLE_LEAK | QUIRK_I2C_NEEDS_BUS_KICK },
    { 0x02, 0 },   /* 전부 수정된 리비전 */
};

static uint32_t g_quirks;
static bool     g_rev_known;

void si_quirks_init(uint8_t si_rev)
{
    for (unsigned i = 0; i < sizeof(k_table) / sizeof(k_table[0]); i++) {
        if (k_table[i].rev == si_rev) {
            g_quirks = k_table[i].quirks; g_rev_known = true; return;
        }
    }
    /* 미지의 리비전: 워크어라운드 없이 가고 크게 로그를 남긴다.
       factory에서 이 로그가 뜨면 새 실리콘이 라인에 들어왔다는 신호다. */
    g_quirks = 0; g_rev_known = false;
}
```

이 구조가 주는 것: 한 이미지로 여러 리비전 지원 · 워크어라운드의 감사 가능성 · **새 리비전이 라인에 들어온 순간의 조기 경보**. 반대 패턴(`#ifdef SILICON_A0`)은 리비전이 섞인 재고를 만나면 필드에서 터진다.

### 3.6 실리콘 리비전 — PCB 리비전과 다른 축이다

B01 8절의 보드 리비전과 **독립적인 차원**이고, 실무에서는 두 축의 곱을 관리한다.

| 개념 | 뜻 | 펌웨어에 의미하는 것 |
|---|---|---|
| A0, A1, B0… | 실리콘 리비전. 문자가 base layer, 숫자가 metal 수정 세대 `[일반 관례 — 표기는 벤더마다 다름]` | errata 목록이 리비전마다 다르다 |
| metal fix | 상위 금속층만 수정 — 빠르고 싸다 | 수정 범위 제한적, 일정 몇 개월 |
| base layer 수정 | 전체 마스크 재작업 | 반년+, 보통 마지막 수단 |
| engineering sample (ES) | 양산 전 샘플. Nordic 문서의 Engineering A/B/C/D가 공개 예시 | EVT 보드에 실린다. errata가 가장 많다 |
| production revision | 양산 리비전(Rev 1/2/3 등) | 출하 펌웨어의 기준 |
| 리비전 읽기 | 벤더가 정한 ID 레지스터로 읽는다 | **부팅 로그에 항상 남긴다** |

주의: **리비전이 섞인다.** 양산 초기에는 ES가 남은 EVT 보드와 새 리비전이 올라간 DVT 보드가 동시에 살아 있다. 한 이미지가 전부 부팅되게 해두지 않으면 개발 속도가 반으로 떨어진다. 부팅 로그 한 줄에 **보드 리비전 + 실리콘 리비전 + SDK 버전 + 펌웨어 SHA**를 함께 찍는 습관이 이 혼란의 해독제다.

### 3.7 벤더 일정을 정직하게 유지하는 법

"벤더를 압박한다"가 아니라 **"모호함을 제거한다"**가 실제 기술이다.

| 레버 | 구체적으로 | 효과 |
|---|---|---|
| 요구를 날짜와 산출물로 변환 | "언제 고쳐 주나" ❌ → "어느 브랜치에 어느 버전으로 <날짜>까지" ✅ | 답이 애매할 수 없게 만든다 |
| 워크어라운드를 항상 병행 요구 | 수정 일정 + 그 전까지의 문서화된 워크어라운드 | 우리 일정이 인질이 되지 않는다 |
| 영향을 마일스톤으로 진술 | "DVT 빌드가 <날짜>에 막힌다" | 우선순위 큐에서 올라간다 |
| 정기 동기 미팅 + 케이스 미러링 | 주 1회, 열린 케이스 표 / 벤더 케이스 ↔ 우리 이슈 1:1 | 조용히 멈추는 걸 막는다 |
| 우리 쪽 숙제를 먼저 끝낸다 | 재현 패키지 완비 | 벤더가 되물을 구실을 없앤다 |
| 우회로를 미리 설계 | 이 기능을 못 쓰면 무엇을 하나(플랜 B) | **협상력이 여기서 나온다** |
| 계약 레버 | 마일스톤·지원 SLA를 계약에 걸기 | 우리 범위 밖이지만 요청할 수 있다 |

거꾸로 **우리가 벤더에게 지켜야 할 것**도 있다. 신뢰가 응답 속도를 결정하기 때문이다. 재현 패키지 품질 유지, 우리 버그를 벤더 버그로 보내지 않기, 고쳐진 걸 확인해 회신하기, NDA 준수, 우리 일정 변경을 먼저 알리기. 이 다섯을 지키는 팀은 같은 FAE에게서 다른 수준의 서비스를 받는다.

### Don 경험과의 접점

3절 전체가 Apple 업무와 가장 가깝다. "새 칩이 전체 시스템을 만날 때의 장애 root cause"는 3.1 분류를 매일 하는 일이고, 3.2의 로그·레지스터 덤프·버스 캡처·스코프 파형은 레쥬메의 도구 목록(DSO, 프로토콜 분석기, 로직 분석기, JTAG)과 정확히 일치한다. <확인 필요: 벤더에게 버그를 증명해 수정 또는 errata 문서화를 받아낸 사례가 있는지. 있으면 3.2~3.4를 관통하는 가장 강한 STAR다. 회사·부품명을 빼고 구조만 말하면 공개 가능 범위>

---

## 4. "하드웨어가 스펙대로 만들어지고 동작하는지" — 펌웨어가 하는 검증

> JD: "Working with Hardware and Software team to ensure our hardware are built and functioning to the specifications"

### 4.1 이 문장은 사실 세 개의 다른 질문이다

면접에서 이 문장이 나오면 **먼저 세 개로 쪼개는 것**이 답변의 절반이다.

| 질문 | 누가 답하나 | 펌웨어의 역할 |
|---|---|---|
| ① 설계가 스펙대로인가 (designed to spec) | HW 설계 + 펌웨어 리뷰 | 회로도 리뷰에서 펌웨어 요구사항 반영(J13 2.8) |
| ② 제작이 설계대로인가 (built to design) | HW + 제조 | 보드 ID·스트랩 읽기, 조립 오류 검출 POST |
| ③ 동작이 스펙대로인가 (functioning to spec) | **펌웨어가 주도** | 인수 조건, 특성화, 마진 테스트, 사인오프 데이터 |

②③이 펌웨어의 일이고 특히 ③은 펌웨어만 할 수 있다. 스코프로는 한 보드의 한 신호를 보지만, **펌웨어는 100장의 보드에서 1000번의 전송을 자동으로 돌릴 수 있다.**

### 4.2 bring-up 인수 조건 — "올라왔다"의 정의를 먼저 합의한다

가장 흔한 분쟁은 **"보드가 좋은가"의 기준이 사람마다 다르다**는 것이다. 게이트를 보드 도착 **전에** 문서로 합의하는 게 시니어의 행동이다.

| 게이트 | 통과 기준 (측정 가능한 형태) | 산출 증거 |
|---|---|---|
| G0 전원 | 모든 레일이 규정 순서·시간 안에 규정 전압 ±허용오차, 무부하 전류 예상 범위 | 스코프 램프 캡처, 레일별 실측 표 |
| G1 디버그 | JTAG/SWD 연결, 코어 ID 읽힘, 정지·재개 동작 | 프로브 로그 |
| G2 부트 | reset → main 도달, 배너에 보드/실리콘 리비전·FW SHA | 부팅 로그 |
| G3 클럭 | 실측 주파수가 목표 ±허용오차, 기동 시간 규정 내 | 주파수 실측 표 |
| G4 버스 | I2C/SPI 각 디바이스 ID 읽기 성공, 목표 속도에서 오류 0 | 스캔 결과, LA 캡처 |
| G5 기능 | 각 주변장치의 최소 기능 왕복(센서 샘플, 마이크 신호, 햅틱 구동) | 기능별 로그 |
| G6 마진 | 4.4절 스윕이 목표 마진 확보 | 마진 표·그래프 |
| G7 전력 | sleep/active 전류가 예산 내, 웨이크 지연 규정 내 | 전류 프로파일 |
| G8 안정성 | N시간 연속, M회 재부팅, 열·전압 코너에서 오류 0 | 스트레스 리포트 |

G0~G2는 순서가 절대적이다. G0을 건너뛰고 G2를 시도하다 보드를 죽이는 게 bring-up의 가장 비싼 실수다(J13 3.2).

### 4.3 POST를 "증거 생산 장치"로 본다

B01 9.2에 POST 구조가 있으므로 여기서는 **관점만 바꾼다.** POST는 보드를 보호하는 장치가 아니라 **HW팀에 넘길 데이터를 자동으로 만드는 장치**다. 핵심 규칙 하나: **pass/fail이 아니라 측정값을 낸다.** 판정 기준은 나중에 바뀔 수 있어야 한다.

```c
/* post_report.c — 한 줄 = 한 측정. CSV로 뽑으면 100장 보드가 한 장의 히스토그램이 된다. */
struct post_item {
    const char *name;        /* "i2c0.imu.whoami", "clk.hfxo.khz" */
    int32_t     value;       /* 측정값 또는 읽은 값 */
    int32_t     lo, hi;      /* 허용 범위 */
    uint8_t     unit;        /* 단위 코드 (raw/mV/kHz/us) */
};

static void post_emit(const struct post_item *it)
{
    int pass = (it->value >= it->lo) && (it->value <= it->hi);
    printf("POST,%s,%ld,%ld,%ld,%u,%s\n", it->name, (long)it->value,
           (long)it->lo, (long)it->hi, it->unit, pass ? "PASS" : "FAIL");
}
```

이 한 가지 변경이 만드는 차이는 크다. "5장 중 1장 실패"가 아니라 "실패한 보드의 값이 경계에서 3% 벗어났고 통과한 보드들도 경계에 몰려 있다"를 말할 수 있다. **후자는 설계 마진 문제를 지적하고, 전자는 조립 불량으로 오해된다.**

### 4.4 마진과 특성화 — 통과/실패가 아니라 여유를 재는 일

| 테스트 | 무엇을 스윕하나 | 무엇을 알아내나 |
|---|---|---|
| 버스 속도 스윕 | I2C 100/400 kHz(그 이상은 지원 모드 확인), SPI 클럭 단계별 | 풀업·용량·레벨 시프터 마진 (J13 2.5~2.6) |
| 전압 코너 | 공급 전압을 규정 상·하한까지 | 저전압에서 먼저 죽는 블록 |
| 온도 코너 | 챔버에서 저온·고온 | 크리스털 오차, 타이밍, 누설 변화 |
| 타이밍 마진 / shmoo | 클럭 위상·지연·샘플 포인트 2차원 스윕 | "동작 창"의 중심과 폭 |
| 기동 시간 | 각 레일·클럭·디바이스의 ready까지 | 초기화 딜레이가 추측이 아니라 근거를 갖는다 |
| 전류 프로파일 | 상태별(sleep/idle/active/peak) 전류 | 예산 대비 실측, errata성 누설 발견(1.4절) |
| 동시성 스트레스 | 모든 버스·DMA·라디오 동시 가동 | 전원 노이즈, 버스 경쟁, 열 |
| 장기 안정성 | N시간 연속 + 반복 재부팅 | 간헐 결함, 냉부팅 전용 실패(B04 Q25) |

**스펙을 "통과"하는 것과 "마진을 갖고 통과"하는 것은 다르다.** 경계에서 통과한 설계는 양산 분포의 꼬리에서 실패한다. 펌웨어가 낼 수 있는 가장 가치 있는 산출물이 "동작 창의 중심이 어디고 우리가 어디에 서 있는지"를 보여주는 그래프다.

```
 동작 창(shmoo) 개념           해석 규칙
  지연 →                       한 보드만 400 kHz에서 실패 → 부품·조립 편차
 전압  . . X X X X . .          모든 보드가 700 kHz에서 실패 → 설계 한계, 정상
  ↑    . X X O X X . .          O가 창 가장자리에 있다 → 스펙은 통과, 마진은 없음
       . X X X X X . .          O = 목표 동작점 · X = 동작 · . = 실패
```

### 4.5 HW팀이 펌웨어에게서 실제로 원하는 것 (사인오프 시점)

| HW팀이 필요한 것 | 펌웨어가 주는 형태 | 없으면 생기는 일 |
|---|---|---|
| "다음 리비전에서 무엇을 바꿔야 하나" | 우선순위 붙은 HW 변경 요청 목록 + 근거 데이터 | 리스핀 기회를 놓친다 |
| 마진 데이터 | 스윕 결과 표·그래프, 보드 간 편차 | 부품 값 변경을 정당화할 수 없다 |
| 실패의 재현 조건 | 절차 + 재현율 + 캡처 | "우리는 재현 안 된다"로 끝난다 |
| 전력 실측 | 상태별 전류, 웨이크 에너지 | 배터리 용량·PMIC 선택을 못 정한다 |
| 조립 검증 수단 | 자동 POST와 CSV 리포트 | 보드마다 손으로 확인 |
| 부품 선택 피드백 | "이 센서는 초기화에 X ms 필요해 UX 예산을 넘는다" | 늦게 발견해 리스핀 |
| 스펙 위반 증거 | 데이터시트 문장 + 실측 캡처 대조 | 벤더와의 논쟁에서 진다 |
| 사인오프 문서 | 게이트 표 + 증거 링크 + 알려진 이슈와 완화책 | 누가 무엇을 승인했는지 불분명 |

### 4.6 단계별로 달라지는 검증의 무게

| 단계 | 펌웨어가 증명해야 하는 것 | 통과 기준의 성격 |
|---|---|---|
| EVT | "설계가 동작 가능하다" — G0~G5, 마진은 샘플 소수 | 하나라도 되면 진전 |
| DVT | "설계가 스펙을 만족한다" — G6~G8, 코너 포함 | 마진이 정량적으로 확보 |
| PVT | "공정이 이 설계를 반복 생산한다" — 보드 간 편차·수율 | 분포가 중심에 있다 |
| MP | "라인에서 모든 유닛이 검증된다" — POST 커버리지·시간 | 초 단위 테스트 예산 |

### Don 경험과의 접점

4절은 Don의 강한 영역이다. 레쥬메 근거(context 3.1)에서 직접 연결된다. "sign off on hardware safety margins (reliability vs performance/power)" → 4.4~4.5절 전체이고, **마진 사인오프를 실제로 한 경험은 스타트업에 드물다.** SK hynix의 "shmoo · health monitoring (SI팀 협업)" → 4.4절 shmoo와 동작 창을 **직접 해봤다**고 말할 수 있는 유일한 지점. "factory test-node architecture" → 4.3절 POST/CSV. "bring up → NPI → MP" → 4.6절 표 전체.

주의: JD 개정판에서 factory test 항목이 **삭제**됐다(context 개정 로그). factory 이야기를 길게 하지 말고 **"하드웨어가 스펙대로인지 증명하는 방법론"**으로 프레이밍을 바꿔 말한다. 같은 경험, 다른 제목이다.

---

## 5. FPGA — 완전히 다른 두 가지 이야기

> JD: "FPGA-based system integrations"

이 다섯 단어는 **서로 거의 관련 없는 두 가지 일** 중 하나를 뜻한다. 어느 쪽인지 모른 채 답하면 절반은 헛말이 된다. 그래서 이 절은 두 쪽을 끝까지 분리해서 다루고, 5.4절에 **면접에서 되묻는 스크립트**를 둔다.

### 5.1 먼저 갈라라

| 항목 | (a) pre-silicon 검증 FPGA / 에뮬레이션 | (b) 제품에 실려 출하되는 FPGA |
|---|---|---|
| 목적 | tape-out 전에 RTL과 펌웨어를 함께 검증 | 제품의 실제 기능 블록 |
| 존재하는 기간 | 실리콘이 오기 전까지(수개월~1년) | 제품 수명 전체 |
| 수량 | 사내 몇 대~수십 대 | 출하 수량 전량 |
| 소유자 | 실리콘/RTL 검증팀 + 펌웨어 | HW + 펌웨어 + RTL |
| 펌웨어의 일 | 부팅·드라이버·레지스터 접근을 실물 없이 올린다 | 비트스트림 저장·로드·버전·전원·인터페이스 |
| 클럭 | 실물의 1/10~1/100 (수 MHz~수십 MHz) | 설계된 목표 클럭 |
| 비트스트림 | 매주 바뀐다. 버전 관리는 느슨해도 된다 | **출하 산출물**. 서명·OTA·롤백 대상 |
| 디버깅 | JTAG + 내장 로직 분석기 + RTL 파형 | 내장 로직 분석기 + 호스트 로그 + 스코프 |
| 실패의 의미 | "RTL을 고친다" 가능 | "펌웨어나 비트스트림 업데이트로 고친다" |
| 전력·열 | 관심 없음 | **1차 관심사** (배터리 기기면 특히) |
| Don 경험 | ✅ Cortex-R8/R82/M0+ pre-silicon bring-up = 정확히 이것 | ❌ 해본 적 없음. 설계안으로만 말한다 |

Hark 맥락에서의 판단 `[추정]`: 배터리 웨어러블급 기기에 FPGA가 출하될 가능성은 낮고(정적 전력·면적·비용), **(a)가 더 그럴듯하다** — 자체 실리콘이나 커스텀 블록을 만들고 있다면 pre-silicon 검증 환경이 필요하다. 반대로 소형 저전력 FPGA/CPLD를 **글루 로직·센서 애그리게이션·디스플레이 브리지**로 쓰는 컨슈머 제품도 실재한다. 그래서 단정하지 않고 되묻는다.

### 5.2 (a) pre-silicon 검증 FPGA — Don이 실제로 한 일

RTL을 합성해 FPGA 프로토타입 보드(또는 에뮬레이터)에 올리고, **실리콘이 존재하기 전에 그 위에서 펌웨어를 돌린다.** 부팅 코드·드라이버·레지스터 접근이 tape-out 전에 검증되고, RTL 버그가 마스크 전에 잡힌다. 이 단계에서 잡은 버그와 실리콘 리비전으로 잡는 버그의 비용 차이가 이 환경의 존재 이유다(3.6절 metal fix 일정을 보라).

펌웨어 쪽에서 실제로 부딪히는 것들이다.

| 현실 | 펌웨어에 요구하는 것 |
|---|---|
| 클럭이 실물의 1/10~1/100 | 딜레이·타임아웃을 **클럭 주파수에서 유도**한다. 하드코딩된 루프 딜레이는 여기서 전부 깨진다 |
| 아날로그·PLL·PHY가 없다 | 스텁/모델로 우회하는 부팅 경로가 생긴다 → 이 경로가 post-silicon에서 정리되는지 추적해야 한다 |
| 메모리 용량·구성이 다르다 | 링커 레이아웃을 파라미터화. 성능 측정은 무의미하므로 **기능만** 본다 |
| 비트스트림이 매주 바뀐다 | 펌웨어와 비트스트림 조합을 기록하지 않으면 재현이 안 된다 |
| 리셋·전원 시퀀스가 실물과 다르다 | "FPGA에서만 되는 초기화"를 만들지 않도록 조심 |

디버깅 방식은 실리콘과 다르다. JTAG 디버거(Don의 Trace32 경험이 그대로 쓰인다) 위에 **FPGA 벤더의 내장 로직 분석기**가 얹힌다 — AMD 쪽은 ILA(Integrated Logic Analyzer), Intel/Altera 쪽은 Signal Tap이 그 역할이다. 실무 기법: **펌웨어가 특정 지점에서 GPIO를 토글하고 그것을 ILA 트리거로 쓴다.** 그러면 펌웨어 로그의 한 줄과 RTL 내부 신호 파형이 같은 시간축에 놓인다. 이게 pre-silicon 디버깅의 핵심 기술이고, 실리콘에서는 할 수 없는 일이다.

RTL 엔지니어와의 루프에서 가장 자주 터지는 것은 **레지스터 맵 불일치**다. 스펙 문서, RTL, 펌웨어 헤더가 세 갈래로 갈라지면 드라이버가 존재하지 않는 비트를 쓴다. 해법은 하나다. **레지스터 맵을 손으로 옮기지 않는다** — 단일 소스(스프레드시트나 IP-XACT류 기술)에서 RTL과 C 헤더를 모두 생성한다. 버그를 RTL/FW로 가르는 절차도 실리콘과 같다(J13 3.3): 파형에서 버스 트랜잭션이 스펙대로 나갔는지 보고, 나갔는데 응답이 틀리면 RTL, 안 나갔으면 펌웨어.

pre-silicon에서 post-silicon으로 넘어갈 때 깨지는 것들 — **이걸 예상 목록으로 말할 수 있으면 경험자로 들린다.**

- 속도: 느린 FPGA에서 숨어 있던 경쟁 조건이 실물 속도에서 드러난다(B04 Q23의 반대 방향)
- 아날로그·PLL: 처음으로 실물이 붙는다. 클럭 락 시간, 전원 시퀀스가 여기서 처음 문제가 된다
- errata: 실리콘에만 존재한다. FPGA에는 없던 항목이 갑자기 목록으로 온다(1.4절)
- 캐시·코히런시: FPGA 구성과 실물 구성이 다르면 DMA 일관성 버그가 실물에서만 난다
- FPGA 전용 코드 경로: 스텁·우회가 남아 있으면 실물에서 오동작한다

> **면접용 영어**: "I've done the pre-silicon side of this. At Solidigm I brought up firmware on ARM Cortex-R8, R82 and M0+ cores running on FPGA before the silicon existed, along with peripheral IP — I2C, SPI, DMA, SRAM and DRAM — and then carried those drivers onto production silicon. The interesting part is what changes on the way: the FPGA runs one or two orders of magnitude slower, so every delay has to be derived from the clock rather than hardcoded, analog and PLL blocks aren't there so you have stubbed boot paths you must remember to remove, and errata only exist on the real part."

### 5.3 (b) 제품에 실려 나가는 FPGA

#### 왜 컨슈머 기기에 FPGA를 넣나

| 이유 | 구체적으로 |
|---|---|
| 인터페이스 브리지 | 센서/디스플레이의 인터페이스가 SoC에 없을 때(MIPI↔병렬, LVDS 변환 등) |
| 센서 애그리게이션 | 여러 센서를 모아 한 스트림으로 만들고 타임스탬프를 붙인다 |
| 글루 로직·시퀀싱 | 전원 시퀀스, 리셋 조정, 핀 확장 — 작은 CPLD급이면 충분 |
| 저지연 전처리 | 마이크/이미지 전처리를 CPU 개입 없이 |
| 양이 적어 ASIC이 안 되는 기능 | 1세대 제품에서 흔하다 |
| 출하 후 변경 여지 | 로직을 업데이트로 고칠 수 있다 — 이게 가장 큰 유혹이자 함정 |

#### 구성(configuration)의 기본

대부분의 FPGA는 구성 메모리가 **SRAM 기반이라 휘발성**이다. 즉 **전원이 들어올 때마다 비트스트림을 다시 로드**한다. 일부 계열은 내부 비휘발 구성 메모리를 갖는다 `[벤더·패밀리 의존 — 반드시 해당 디바이스 문서 확인]`. 펌웨어에게 이 사실이 뜻하는 것은 하나다. **FPGA는 부팅할 때마다 "부팅"하는 또 하나의 프로세서이고, 그 부팅을 누군가 책임져야 한다.**

AMD UltraScale 계열 구성 시퀀스는 8단계로 문서화돼 있다(검증: 참고 자료).

```
① Device Power-Up  → ② Clear Configuration Memory (Initialization)
→ ③ Sample Mode Pins → ④ Synchronization → ⑤ Device ID Check
→ ⑥ Load Configuration Data → ⑦ CRC Check → ⑧ Start-up Sequence
```

펌웨어가 여기서 읽어야 하는 것: ③에서 **모드 핀이 샘플링되는 시점이 전원 인가 직후**라는 것(그래서 모드 핀을 GPIO로 늦게 설정하는 설계는 성립하지 않는다), ⑤ **Device ID Check**가 있다는 것(비트스트림이 다른 디바이스용이면 여기서 실패한다), ⑦ **CRC Check**가 있다는 것(비트스트림 손상을 FPGA가 스스로 잡아준다 = 우리가 그 실패를 감지할 수단을 만들어야 한다).

#### 구성 모드 — 누가 데이터를 밀어넣는가

| 계열 | 모드 | 성격 |
|---|---|---|
| AMD UltraScale (UG570, 검증) | Master SPI (serial NOR, x1/x2/x4/dual x4) | FPGA가 **스스로** flash에서 읽는다 (master = FPGA가 클럭 생성) |
| AMD UltraScale | Master BPI (parallel NOR, x8/x16), Master serial, Master SelectMAP (x8/x16) | 같은 self-load, 폭·매체만 다름 |
| AMD UltraScale | Slave serial, Slave SelectMAP (x8/x16/x32) | **외부 호스트**(MCU/SoC/CPLD)가 밀어넣는다 |
| AMD UltraScale | JTAG boundary scan | 개발·양산 프로그래밍용 |
| Intel/Altera `[검색 스니펫 기준 — 원문 미확인]` | active: AS(active serial), AP(active parallel) | FPGA가 스스로 읽는 쪽 |
| Intel/Altera `[동일]` | passive: PS(passive serial), FPP(fast passive parallel), JTAG, Avalon-ST(Stratix 10 계열에서 FPP 대체) | 외부 호스트가 밀어넣는 쪽 |

용어가 벤더마다 다르지만 **축은 하나**다. `FPGA가 스스로 읽는가(master/active)` vs `호스트가 밀어넣는가(slave/passive)`. 모드 선택은 **모드 핀**으로 하고(UG570은 `M[2:0]`), 구체적 비트 값과 전용 구성 핀 목록은 패밀리마다 다르므로 해당 디바이스 문서를 봐야 한다 `[벤더 의존]`.

#### 시스템 아키텍처 네 가지 — 이 선택이 펌웨어 일의 전부를 결정한다

| 아키텍처 | 비트스트림 위치 | 펌웨어가 하는 일 | 트레이드오프 |
|---|---|---|---|
| ① FPGA self-load | FPGA 전용 flash | 거의 없음. DONE 확인, 업데이트 시 flash 쓰기 경로 필요 | 부품 추가, 업데이트가 번거롭다 |
| ② MCU가 slave 모드로 로드 | MCU flash 또는 외부 flash | **전부**: 저장, 스트리밍, 타이밍, 상태 확인, 실패 처리 | MCU flash 용량·부팅 시간 압박 |
| ③ Linux 호스트가 로드 | rootfs `/lib/firmware` 또는 파티션 | 커널 FPGA manager 사용, DT/오버레이 관리 | 부팅이 늦다(리눅스가 올라온 뒤) |
| ④ SoC 내장 PL (Zynq류) | 부트 이미지 안 | 부트로더 단계에서 로드 | 플랫폼이 정한 흐름을 따른다 |

④의 공개 예시가 Zynq UltraScale+ MPSoC다(검증). 부팅이 **pre-configuration(PMU가 PMU ROM 실행) → configuration(CSU가 FSBL을 OCM으로 로드) → post-configuration** 순서고, FSBL이 PS 초기화와 함께 **PL 구성**을 담당한다. 여기서 펌웨어가 소유하는 것은 "비트스트림을 어느 이미지에 어떻게 넣고 버전을 어떻게 맞추나"이지 로딩 메커니즘 자체가 아니다.

#### 소프트웨어 인터페이스 — 실제 API

**Linux**: 커널에 FPGA 서브시스템이 있고 세 조각으로 나뉜다(검증). **FPGA Manager**가 실제 프로그래밍을, **FPGA Bridge**가 PL↔PS 사이 버스 브리지를, **FPGA Region**이 "매니저 + 브리지 + 재구성 가능 영역"의 묶음을 표현한다. 프로그래밍 진입점은 `fpga_region_program_fpga()`이고, 문서가 명시한 동작 순서가 중요하다.

```
region mutex lock → manager mutex lock → 브리지 목록 구성 → 브리지 disable
→ fpga_region->info 로 FPGA 프로그래밍 → 브리지 re-enable → lock 해제
```

**브리지를 먼저 끄고 나중에 켠다**는 이 한 줄이 설계의 핵심이다. 구성 중인 FPGA는 버스에서 쓰레기를 낼 수 있고, 그게 호스트 버스로 새면 시스템이 죽는다. 이미지 지정은 `struct fpga_image_info`로 하고 `firmware_name`(파일) 또는 `buf`/`count`(메모리), `sgt`(scatter-gather)를 쓰며, `flags`에 `FPGA_MGR_PARTIAL_RECONFIG`(부분 재구성), 암호화·압축 비트스트림 지시, 브리지 타임아웃(`enable_timeout_us`/`disable_timeout_us`)이 들어간다. 할당/해제는 `fpga_image_info_alloc()`/`fpga_image_info_free()`다.

**Zephyr**: MCU 쪽에도 FPGA 드라이버 API가 있다(검증). `fpga_load()`(비트스트림 로드 및 프로그래밍), `fpga_reset()`, `fpga_on()`, `fpga_off()`, `fpga_get_status()`, `fpga_get_info()`이고 상태는 `FPGA_STATUS_ACTIVE`/`FPGA_STATUS_INACTIVE`다. 쉘 서브시스템도 있어 `fpga load <dev> <addr> <size>`, `fpga reset <dev>` 형태로 bring-up 중 손으로 시험할 수 있다. 공식 샘플은 QuickLogic QuickFeather 보드만 지원한다.

```c
/* MCU가 FPGA를 로드하는 경로 (Zephyr API).
   핵심은 "로드했다"가 아니라 "맞는 비트스트림을 로드했고 FPGA가 동의한다"까지다. */
#include <zephyr/drivers/fpga.h>

#define FPGA_IF_VERSION_EXPECTED  0x0104u   /* 펌웨어가 기대하는 레지스터 맵 버전 */

int fpga_bringup(const struct device *fpga, const uint8_t *bs, uint32_t len, uint32_t ver)
{
    /* 0) 호환성 게이트를 로드 전에 본다. 안 맞는 걸 올리고 깨닫는 것보다
          올리지 않고 안전 모드로 부팅하는 게 낫다. */
    if (!bitstream_is_compatible(ver))                     return -ENOTSUP;

    int rc = fpga_load(fpga, (uint32_t *)bs, len);  /* 전송 + 프로그래밍. CRC·ID 실패도 여기 */
    if (rc != 0)                                           return rc;
    if (fpga_get_status(fpga) != FPGA_STATUS_ACTIVE)       return -EIO;

    /* 1) FPGA가 우리가 아는 로직인지 스스로 말하게 한다. 비트스트림 안의 버전
          레지스터를 호스트가 읽는 것이 유일하게 믿을 수 있는 확인이다. */
    if (fpga_if_read_reg(FPGA_REG_IF_VERSION) != FPGA_IF_VERSION_EXPECTED) {
        return -EPROTO;      /* 로드는 됐지만 계약이 다르다 → 안전 모드 */
    }
    return 0;
}
```

#### 버전·호환성 관리 — 펌웨어와 비트스트림은 하나의 계약이다

FPGA가 제품에 들어오면 **소프트웨어가 두 개**가 된다. 그리고 둘 사이의 인터페이스(레지스터 맵, 인터럽트 의미, DMA 규약)가 계약이다. 이걸 관리하는 규칙들이다.

- **비트스트림 안에 버전 레지스터를 둔다.** 호스트가 로드 후 반드시 읽고 대조한다(위 코드 1번).
- **호환 매트릭스를 문서로 유지한다**: 펌웨어 버전 × 비트스트림 버전 → 지원/미지원. 한 쪽만 올라가는 상황이 반드시 생긴다.
- **OTA는 원자적이어야 한다.** 펌웨어와 비트스트림을 **한 패키지**로 묶어 같이 커밋하거나 같이 롤백한다. 따로 업데이트하면 중간 상태에서 부팅하지 못하는 조합이 나온다.
- **롤백 경로를 만든다.** 새 비트스트림이 실패하면 이전 것으로 부팅해야 한다 — A/B 슬롯 개념이 그대로 적용된다(C07의 이미지 슬롯 논리와 같다).
- **빌드 산출물을 추적한다.** 비트스트림의 해시·합성 툴 버전·RTL 커밋을 `build_info`에 넣는다(2.6절의 `bitstream_ver`).
- **비트스트림도 서명 대상이다.** 로직을 바꿀 수 있는 파일이므로 보안 경계가 펌웨어 이미지와 같은 급이다 `[구체적 지원 방식은 벤더 의존]`.

#### partial reconfiguration — 알아는 두고, 기본값으로 쓰지 않는다

AMD는 이 기능을 **Dynamic Function eXchange(DFX)**라 부르고 "동작 중인 설계 안에서 모듈을 재구성하는 것"으로 정의한다(검증). 용어는 **Reconfigurable Partition(재구성 영역)**, **Reconfigurable Module(그 안에 들어가는 모듈)**, **Static Region(전 구성에서 고정, 배치·배선이 공유됨)**, **Partial Bitstream(그 영역만 갱신하는 비트스트림)**이다. 7 Series·UltraScale·UltraScale+·Versal 계열에서 지원되며 디바이스별 제약이 있다.

언제 실제로 쓰나: 상호 배타적인 큰 기능을 번갈아 써서 면적을 절약할 때, 정지 없이 한 블록만 갱신해야 할 때. **왜 보통 안 쓰나**: 설계 흐름이 복잡해지고(구성마다 구현을 따로 돌리고 static 결과를 체크포인트로 공유), 검증 조합이 늘고, 펌웨어 쪽 상태 관리가 어려워진다(재구성 중 그 블록을 쓰던 드라이버는 무엇을 해야 하나). 면접에서의 정답은 **"필요를 증명한 뒤에 도입한다"**다.

#### 전력과 열 — 배터리 기기에서 FPGA가 어려운 진짜 이유

- **정적 전력**이 크다. 아무것도 하지 않아도 구성 메모리와 로직이 전류를 먹는다. always-on 예산에서 이게 지배적일 수 있다.
- 그래서 **전원을 끄고 싶어진다.** 그런데 끄면 구성이 사라지고, 깨울 때 **다시 로드하는 시간과 에너지**가 든다. 즉 `sleep 절약` vs `wake 지연·재구성 에너지`의 트레이드오프가 생기고, 웨이크가 잦으면 끄는 게 손해다.
- **구성 시간이 부팅 예산에 들어간다.** 비트스트림 크기 ÷ 구성 인터페이스 대역폭이 그대로 사용자 체감 지연이 된다. 이게 UX 요구를 넘으면 인터페이스 폭을 늘리거나(x1 → x4) 비트스트림 압축을 검토한다.
- **열**: 작은 밀폐 웨어러블에서 FPGA는 국부 열원이 된다. 스킨 온도 예산과 스로틀링 정책(C05)에 포함해야 한다.
- 설계 결론 `[일반론]`: 배터리 always-on 기기에서 FPGA는 **가능하면 피하고**, 필요하면 가장 작은 저전력 계열로 제한하며, 전원 도메인을 분리해 끌 수 있게 만든다.

#### 디버깅 — 증상에서 원인으로

| 증상 | 유력한 원인 |
|---|---|
| DONE이 올라가지 않음 | 비트스트림 손상(CRC), 잘못된 디바이스용 이미지(ID check), 구성 클럭/전원 문제 |
| 구성은 되는데 로직이 오동작 | 비트스트림 버전 ↔ 펌웨어 레지스터 맵 불일치 |
| 냉부팅만 실패 | 전원 램프 중 구성 시작, 레일 ready 전에 로드 시도 |
| 간헐 실패 | 구성 인터페이스 신호 품질, 클럭 속도 과다, flash 타이밍 |
| 구성 직후 호스트 버스 행 | 브리지를 끄지 않고 프로그래밍(Linux는 FPGA Region이 이걸 처리한다) |
| 다른 칩이 FPGA 핀을 오해 | 구성 전 I/O 상태(Hi-Z/pull)를 고려하지 않은 설계 |
| 부팅이 느려짐 | 비트스트림 크기·구성 폭, 로드 시점이 늦은 아키텍처(③) |

### 5.4 "어느 쪽을 말하는 겁니까" — 면접 스크립트

이 질문을 되묻는 것 자체가 시니어 신호다. 모르는 걸 묻는 게 아니라, **두 쪽 다 알고 있으니 범위를 정하자**는 신호로 들리게 말한다.

> "Before I answer, can I check which kind you mean? I've seen 'FPGA-based system integration' used two very different ways. One is pre-silicon: RTL running on an FPGA prototype so firmware boots and drivers get validated before tape-out. The other is an FPGA that ships inside the product, where firmware owns bitstream storage, loading at boot, version compatibility and power. I've done the first one hands-on — I brought up Cortex-R and Cortex-M firmware on FPGA before silicon existed at Solidigm. On the second I can walk you through how I'd design it, but I want to be straight with you that I haven't shipped a product with an FPGA in it."

한국어 뼈대 (외울 것): ① 두 가지 뜻이 있다고 짚는다 ② 각각 한 문장으로 정의한다 ③ **(a)는 해봤다**고 근거와 함께 말한다 ④ **(b)는 설계안으로 말하겠다**고 선을 긋는다 ⑤ 그 뒤에 5.3절의 아키텍처 네 가지로 답한다.

<확인 필요: Solidigm FPGA 환경이 FPGA 프로토타입 보드였는지 상용 에뮬레이터였는지, 비트스트림을 직접 로드했는지 검증팀이 했는지, ILA/Signal Tap류를 직접 써 봤는지 — 답에 넣으면 구체성이 크게 올라가는 항목들이다>

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 벤더 코드를 제자리에서 수정 | 6개월 뒤 리베이스 불가, 오래된 SDK로 출하 | 우리 변경과 벤더 코드가 섞임 | 원본 격리 + `patches/` + 오버레이 우선(2.2) |
| 임시 패치에 수명 표기 없음 | `HACK:` 패치 2년 생존, 이유 불명 | 메타데이터 없음 | 수명·케이스 번호 필수화(2.3) |
| 앱이 벤더 API 직접 호출 | 칩 교체·SDK 업그레이드에 앱 전체가 깨짐 | 어댑터 층 없음 | 어댑터 강제 + CI 심볼 금지 규칙(2.4) |
| errata를 읽지 않음 | 배터리가 예상보다 빨리 닳는다, 간헐 버스 실패 | 실리콘이 문서와 다름 | 사용 블록 필터링 후 전수 판정(1.4) |
| 워크어라운드를 전 리비전에 적용 | 새 실리콘에서도 성능·전력 손실 | 무조건 적용 | 런타임 리비전 게이팅(3.5) |
| 우리 보드에서만 재현되는 걸 벤더에 보고 | "works on our EVK"로 2주 손실 | 3.1 분류 생략 | EVK 최소 재현을 먼저 만든다 |
| 부팅 로그에 버전 정보 없음 | 필드 로그로 재현 불가, 벤더 보고 불가 | 추적성 부재 | 보드·실리콘 리비전 + SDK + SHA + 비트스트림(2.6) |
| 인수 조건을 합의하지 않음 | "보드가 좋은가"로 끝없는 논쟁 | 기준이 사람마다 다름 | 보드 도착 전 게이트 표 합의(4.2) |
| POST가 pass/fail만 출력 | 설계 마진 문제가 조립 불량으로 오해됨 | 측정값을 안 남김 | 값 + 허용범위를 CSV로(4.3) |
| 비트스트림과 펌웨어를 따로 업데이트 | 부팅하지 못하는 조합이 필드에 나감 | 계약을 관리하지 않음 | 한 패키지, 버전 레지스터, 롤백(5.3) |
| FPGA 전원을 껐다 켜며 절약 시도 | 웨이크 지연 급증, 오히려 에너지 증가 | 재구성 비용 미계산 | 웨이크 빈도 × 재구성 에너지로 판단(5.3) |
| pre-silicon 전용 코드 경로 잔존 | 실물에서만 나는 오동작 | 스텁·우회 미정리 | FPGA 전용 코드에 표식 + 이행 체크리스트(5.2) |

---

## 7. 면접에서 이렇게 말한다

**Q1. How do you integrate a vendor SDK without forking it to death?**
**A.** 벤더 코드를 원본 그대로 격리하고 우리 변경을 패치로 분리한다. 수정은 항상 가장 얕은 층에서 시도한다 — 설정 → 보드 기술 → 우리 파일 추가 → 빌드 우선순위 교체 → 마지막이 패치. 패치마다 이유·수명·upstream 상태를 붙이고, 되는 건 벤더에 보내 다음 드롭에서 사라지게 한다. 그리고 리베이스를 일정에 **예약**한다.
> "Isolate the vendor tree pristine and keep our changes as an ordered patch series. Always try the shallowest layer first — config, then board description, then adding our own file, then build-level override, and only then a patch. Every patch carries a reason, a lifetime and an upstream status, and anything upstreamable gets sent so it disappears on the next drop. Rebases are scheduled, not aspirational."

**Q2. The vendor says "it works on our EVK". What do you do?**
**A.** 그 답이 오기 전에 EVK 최소 재현을 이미 만들어 뒀어야 한다. 못 만들었다면 그건 정보다 — 우리 보드가 reference에서 벗어난 지점 목록으로 돌아가 전원·풀업·클럭·핀 먹싱·레벨 시프터를 하나씩 배제한다. 그 결과가 "통합 버그"로 판정되면 티켓의 성격을 바꿔서 질문 형태로 협조를 요청한다.
> "Ideally I've already reproduced it on their EVK before I file. If I can't, that's data: it means the delta is ours, and I go back to the schematic diff against their reference and eliminate power, pull-ups, clocking, pin mux and level shifting one at a time. If it lands as an integration issue rather than a silicon issue, I change the ticket from a defect report to a specific technical question."

**Q3. What goes into a bug report to a silicon vendor?**
**A.** 3.2절 목록. 하드웨어·소프트웨어 식별, 빌드 가능한 최소 재현 프로젝트, 번호 매긴 절차와 재현율, 문서 인용으로 표현한 기대값, 타임스탬프 로그, 레지스터 덤프, 버스·파형 캡처, 영향 진술. 그리고 **"이미 배제한 것" 목록** — 이게 왕복 한 번을 줄인다.
> "Hardware and software identity, a buildable minimal repro on their own EVK, numbered steps with a reproduction rate, the expectation quoted from their documentation, timestamped logs, a register dump, bus and scope captures, and the milestone impact. Plus a 'what we already ruled out' section — that single section saves a round trip."

**Q4. An errata workaround costs power. How do you handle that?**
**A.** 런타임에 실리콘 리비전을 읽어 quirk 테이블로 게이팅한다. 빌드 타임 `#ifdef`는 쓰지 않는다 — 한 이미지가 섞인 리비전 재고를 다 부팅해야 하기 때문이다. 미지의 리비전은 워크어라운드 없이 가고 경고를 남긴다. 미래 실리콘에 과거 워크어라운드를 적용하는 게 더 위험하고, 그 로그가 "새 실리콘이 라인에 들어왔다"는 조기 경보가 된다.
> "Read the silicon revision at runtime and gate workarounds through a quirk table — never a build-time ifdef, because one image has to boot every revision that's alive in the lab and on the line. Unknown revisions get no workarounds plus a loud log; applying an old workaround to new silicon is the more dangerous mistake, and that log is your early warning that new parts arrived."

**Q5. How do you keep a vendor on schedule?**
**A.** 압박이 아니라 모호함 제거다. 요구를 "어느 브랜치, 어느 버전, 언제까지"로 변환하고, 수정과 **워크어라운드를 항상 같이** 요구한다. 영향은 마일스톤 날짜로 진술한다. 케이스를 우리 트래커에 미러링하고 주 1회 동기한다. 그리고 플랜 B를 설계해 둔다 — 협상력은 거기서 나온다.
> "It's not pressure, it's removing ambiguity. I convert 'when will this be fixed' into 'which branch, which version, by what date', and I always ask for a documented workaround alongside the fix so my schedule isn't hostage to theirs. Impact is stated as a milestone date. Cases are mirrored in our tracker and reviewed weekly. And I design a plan B — that's where the leverage actually comes from."

**Q6. What does "make sure the hardware is built to spec" mean to you as a firmware engineer?**
**A.** 세 개의 질문으로 쪼갠다. 설계가 스펙대로인가, 제작이 설계대로인가, 동작이 스펙대로인가. 뒤의 두 개가 펌웨어 일이고 특히 세 번째는 펌웨어만 할 수 있다 — 100장의 보드에서 자동으로 수천 번 돌릴 수 있는 건 소프트웨어뿐이다. 실무 형태는 보드 도착 전에 합의한 인수 게이트, 측정값을 내는 POST, 그리고 통과/실패가 아니라 마진을 보여주는 스윕이다.
> "I split it into three questions: is it designed to spec, is it built to the design, and is it functioning to spec. The last two are firmware's, and the third one only firmware can do — software is the only thing that can run a thousand transactions across a hundred boards automatically. Concretely that's acceptance gates agreed before boards arrive, a POST that emits measurements rather than pass/fail, and sweeps that show margin instead of a single pass."

**Q7. What do you hand the hardware team when bring-up is done?**
**A.** 게이트 표와 증거, 우선순위 붙은 HW 변경 요청, 마진 스윕 결과와 보드 간 편차, 전력 실측, 실패의 재현 조건, 남은 리스크와 완화책. 핵심은 **다음 리스핀 전에** 준다는 것이다. 늦게 주는 완벽한 리포트는 쓸모가 없다.
> "The gate table with evidence, a prioritized list of hardware change requests with the data behind each one, margin sweeps including board-to-board spread, measured power by state, reproduction conditions for every failure, and remaining risks with mitigations. The important part is timing — it has to land before the respin decision, not after."

**Q8. "FPGA-based system integrations" — tell me about your experience.**
**A.** 5.4절 스크립트를 그대로. 두 뜻을 구분하고, (a)는 근거와 함께 해봤다고, (b)는 설계안으로 답하겠다고 선을 긋는다.

**Q9. How does an FPGA get its bitstream at boot, and what can go wrong?**
**A.** 대부분 SRAM 기반이라 매 부팅마다 로드한다. 축은 두 개 — FPGA가 스스로 flash에서 읽는가(master/active), 호스트가 밀어넣는가(slave/passive). 시스템 선택지는 넷: FPGA self-load, MCU가 slave 모드로 로드, Linux가 FPGA manager로 로드, SoC 내장 PL을 부트로더가 로드. 틀어지는 것들: 레일 ready 전 로드 시작(냉부팅만 실패), 다른 디바이스용 이미지(ID check 실패), CRC 실패, 구성 중 브리지를 끄지 않아 호스트 버스가 행, 구성 전 I/O 상태를 옆 칩이 오해, 그리고 구성 시간이 부팅 예산을 먹는 것.
> "Most are SRAM-based, so the bitstream loads on every power-up. There are really two axes: the FPGA fetching from its own flash, or a host pushing it in. Architecturally that's self-load from dedicated flash, an MCU pushing it in slave mode, Linux programming it through the kernel FPGA manager, or an embedded PL loaded by the bootloader. Failure modes: starting configuration before rails are ready, which shows up as cold-boot-only failures; a bitstream for the wrong device failing the ID check; CRC failures; hanging the host bus by programming without disabling the bridges; neighbouring chips misreading the FPGA's pre-configuration I/O state; and configuration time eating the boot budget."

**Q10. How do you version a bitstream against firmware?**
**A.** 둘을 하나의 계약으로 본다. 비트스트림 안에 버전 레지스터를 두고 호스트가 로드 후 읽어 대조한다. 호환 매트릭스를 문서로 유지하고, OTA는 펌웨어+비트스트림을 한 패키지로 원자적으로 커밋·롤백한다. 비트스트림 해시와 RTL 커밋을 빌드 정보에 넣는다.
> "I treat them as one contract. The bitstream exposes a version register the host reads after loading and refuses to proceed on mismatch. A compatibility matrix is maintained as a document, OTA ships firmware and bitstream as one atomic package that commits or rolls back together, and the bitstream hash plus the RTL commit go into the build info block so a field log identifies the exact pair."

**Q11. Pre-silicon FPGA vs real silicon — what breaks on the way?**
**A.** 5.2절 목록: 속도가 10~100배 올라가며 숨어 있던 경쟁 조건이 드러난다, 아날로그·PLL이 처음 붙는다, errata가 처음 생긴다, 캐시·코히런시 구성이 달라 DMA 일관성 버그가 실물에서만 난다, FPGA 전용 우회 코드가 남아 있으면 오동작한다. 그래서 딜레이를 클럭에서 유도하고 FPGA 전용 경로에 표식을 남기는 습관이 중요하다.

---

## 8. 직접 해보기

```sh
# 1) Linux FPGA 서브시스템의 실제 코드를 읽는다 (클론 없이)
#    확인할 것: 브리지 disable → program → enable 순서가 코드에 어떻게 나타나는가
BASE=https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain
curl -s $BASE/drivers/fpga/fpga-region.c | grep -n "fpga_region_program_fpga" -A 30
curl -s $BASE/include/linux/fpga/fpga-mgr.h | grep -n "FPGA_MGR_" | head -20

# 2) Zephyr FPGA API와 쉘을 읽고 빌드한다 (보드 이름은 Zephyr 버전마다 다르다)
grep -rn "fpga_load\|FPGA_STATUS_" zephyr/include/zephyr/drivers/fpga.h
west boards | grep -i feather
west build -b <board> zephyr/samples/drivers/fpga/fpga_controller   # 보드가 있으면
# 없으면 drivers/fpga/ 구현 하나를 읽고 fpga_load 가 실제로 하는 일을 적어 본다

# 3) 벤더 패치 규율 리허설 (30분): 두 가지 이유로 고치고 커밋을 반드시 둘로 나눈다
git clone --depth 1 https://github.com/zephyrproject-rtos/hal_nordic.git vendor_hal
cd vendor_hal && git switch -c product/ours     # ... 수정 후 커밋 2개
git format-patch main -o ../patches/            # 패치 시리즈로 뽑는다
git fetch origin && git rebase --onto origin/main main   # 드롭 리베이스 시뮬레이션

# 4) errata 읽기 연습 (30분): docs.nordicsemi.com 의 nRF52840 errata 를 리비전별로 열고
#    "SAADC + I2C + QSPI 를 쓴다"고 가정해 해당 항목만 뽑는다. 항목마다 판정:
#    우리 패턴에 해당하나 / 워크어라운드의 전력·성능 비용 / 어느 리비전에서 사라지나
#    → 그 결과로 3.5절 si_quirks 테이블을 실제로 채운다

# 5) 재현 패키지 연습 (1시간): 실패를 인위적으로 만들고(풀업 제거, 클럭 과다)
#    3.3절 템플릿을 첨부물까지 빈칸 없이 채운다.
#    체크: 이 문서를 처음 보는 사람이 내 손 없이 재현할 수 있는가
```

---

## 9. 요약 & 체크리스트

이 노트의 한 문단 요약이다. 벤더 통합의 기술은 코드가 아니라 **경계 관리**다 — 벤더 코드를 원본 그대로 격리하고 우리 변경에 이유와 수명을 붙이면 다음 드롭에 올라탈 수 있고, 그 반대는 오래된 SDK로 출하하는 결말이다. 벤더 검증의 기술은 **증거**다 — EVK에서 재현시키고, 이미 배제한 것을 적어 보내고, 요구를 날짜와 브랜치로 변환한다. 하드웨어가 스펙대로인지 증명하는 일은 펌웨어만 할 수 있고, 그 산출물은 pass/fail이 아니라 **마진**이다. 그리고 "FPGA-based system integration"은 두 가지 뜻이므로 **먼저 되묻는다**: pre-silicon 검증 FPGA는 Don이 해본 일이고, 제품에 실리는 FPGA는 비트스트림 저장·부팅 로드·버전 계약·전원과 열의 문제다.

- [ ] 벤더 배송물 10종을 나열하고 각각에서 펌웨어가 즉시 확인할 것을 말할 수 있다
- [ ] 저장소 전략 네 가지를 비교하고 "오버레이 우선, 패치 최후" 5단계 순서를 설명할 수 있다
- [ ] 패치 메타데이터 네 개(이유·수명·upstream·영향범위)와 SDK 업그레이드 7단계를 말할 수 있다
- [ ] 어댑터 층이 주는 이득 네 개를 즉석에서 말할 수 있다
- [ ] 벤더 이슈 3분류와 "EVK 최소 재현"의 의미, 재현 패키지 항목 8개 이상을 말할 수 있다
- [ ] errata 워크어라운드를 런타임 리비전으로 게이팅하는 코드를 화이트보드에 쓸 수 있다
- [ ] 실리콘 리비전(A0/B0, metal fix)과 PCB 리비전이 다른 축임을 설명할 수 있다
- [ ] bring-up 인수 게이트 G0~G8과 각 게이트의 증거, POST가 측정값을 내야 하는 이유를 말할 수 있다
- [ ] FPGA 두 가지 의미를 비교표로 그리고 되묻기 스크립트를 영어로 말할 수 있다
- [ ] FPGA 구성 8단계, master/slave(active/passive) 축, 로딩 아키텍처 네 가지를 설명할 수 있다
- [ ] 비트스트림 ↔ 펌웨어 버전 계약, 원자적 OTA, FPGA 전원 차단의 트레이드오프를 말할 수 있다

---

## 참고 자료

WebFetch로 내용을 직접 확인한 페이지에 ✅를 표시했다. ⚠는 검색 결과 스니펫으로만 확인해 원문 대조를 못 한 것이고, 표기 없는 항목은 **벤더·버전 의존**으로 취급해야 한다.

| 자료 | 확인 | 무엇을 확인했나 | URL |
|---|---|---|---|
| AMD UG570 — UltraScale Configuration, Overview | ✅ 2026-09-29 | 구성 8단계(Power-Up → Clear Memory → Sample Mode Pins → Synchronization → Device ID Check → Load → CRC → Start-up), 모드 7종(Slave serial, Slave SelectMAP x8/16/32, JTAG, Master SPI x1/x2/x4/dual x4, Master BPI x8/16, Master serial, Master SelectMAP x8/16)을 `M[2:0]`으로 선택. **개별 M[2:0] 비트 값은 이 페이지에 없음** | https://docs.amd.com/r/en-US/ug570-ultrascale-configuration/Overview |
| AMD UG570 — SelectMAP Configuration Modes | ⚠ 2026-09-29 | SelectMAP이 병렬 인터페이스이고 `CSI_B`/`RDWR_B`/`CCLK`/`D[31:0]` 계열 신호를 쓰며 master/slave 양쪽이 있음. 세부 신호 목록·타이밍은 패밀리마다 다르므로 디바이스 문서 필수 | https://docs.amd.com/r/en-US/ug570-ultrascale-configuration/SelectMAP-Configuration-Modes |
| AMD UG909 — Dynamic Function eXchange, Introduction | ✅ 2026-09-29 | DFX = "reconfiguration of modules within an active design". 용어: Reconfigurable Partition, Reconfigurable Module, Static Region, Partial Bitstream. 7 Series/UltraScale/UltraScale+/Versal 지원, 디바이스별 제약 | https://docs.amd.com/r/en-US/ug909-vivado-partial-reconfiguration/Introduction |
| AMD UG1137 — Zynq UltraScale+ Boot Process Overview | ✅ 2026-09-29 | pre-configuration(PMU가 PMU ROM 실행) → configuration(CSU가 FSBL을 OCM으로 로드, secure/non-secure) → post-configuration. FSBL은 부트 헤더에 따라 Cortex-R5F 또는 A53에서 실행. **PCAP 등 PL 로딩 세부 경로는 이 페이지에 없음** | https://docs.amd.com/r/en-US/ug1137-zynq-ultrascale-mpsoc-swdev/Boot-Process-Overview |
| Linux — In-kernel API for FPGA Programming | ✅ 2026-09-29 | `fpga_region_program_fpga()` 순서: region mutex → manager mutex → 브리지 목록 → **브리지 disable → 프로그래밍 → 브리지 re-enable** → unlock. `struct fpga_image_info`의 `flags`/`firmware_name`/`buf`/`count`/`sgt`/`enable_timeout_us`/`disable_timeout_us`, `FPGA_MGR_PARTIAL_RECONFIG`, 암호화·압축 플래그, `fpga_image_info_alloc/free()` | https://docs.kernel.org/driver-api/fpga/fpga-programming.html |
| Linux — FPGA Region | ✅ 2026-09-29 | region = 매니저 + 브리지 + 재구성 가능 영역의 묶음. DT 레이어는 `of-fpga-region.c`. 부분 재구성은 `fpga_image_info` 플래그로 표현 | https://docs.kernel.org/driver-api/fpga/fpga-region.html |
| Linux — FPGA Subsystem 색인 | ✅ 2026-09-29 | FPGA Manager / FPGA Bridge / FPGA Region 세 조각 구성 | https://docs.kernel.org/driver-api/fpga/index.html |
| Zephyr — FPGA driver API (Doxygen) | ✅ 2026-09-29 | `fpga_load()`, `fpga_reset()`, `fpga_on()`, `fpga_off()`, `fpga_get_status()`, `fpga_get_info()`, `enum FPGA_status`의 `FPGA_STATUS_ACTIVE`/`FPGA_STATUS_INACTIVE` | https://docs.zephyrproject.org/latest/doxygen/html/group__fpga__interface.html |
| Zephyr — FPGA controller 샘플 | ✅ 2026-09-29 | 쉘 명령 `fpga load FPGA <addr> <size>`, `fpga reset FPGA`, `devmem load -e <addr>`. 지원 보드는 QuickLogic QuickFeather뿐 | https://docs.zephyrproject.org/latest/samples/drivers/fpga/fpga_controller/README.html |
| Nordic nRF52840 errata — anomaly [241] | ✅ 2026-09-29 | Symptoms/Conditions/Consequences/Workaround 4필드 구조. "Static current consumption between 400 µA and 450 µA occurs." BURST 다중 채널 후 SAADC disable 조건. 워크어라운드 적용 후 SAADC 재설정 필요 | https://docs.nordicsemi.com/r/bundle/errata_nrf52840_rev3/page/err/nrf52840/rev3/latest/anomaly_840_241.html |
| Nordic nRF52840 errata — 리비전별 번들 | ✅ 2026-09-29 | 같은 부품의 errata가 Engineering A/B/C/D와 Rev 1/2/3로 **리비전마다 별도 문서**로 존재하고 "Fixed anomalies" 페이지가 따로 있다 → 3.5~3.6절 리비전 게이팅의 공개 근거 | https://docs.nordicsemi.com/bundle/errata_nRF52840_Rev3/page/ERR/nRF52840/Rev3/latest/ |
| Intel/Altera 구성 스킴 분류 | ⚠ 2026-09-29 | active(AS, AP) vs passive(PS, FPP, JTAG) 분류, Avalon-ST가 Stratix 10 계열에서 FPP를 대체하며 외부 호스트가 구성을 주도한다는 서술. **intel.com 원문 페이지가 리다이렉트로 직접 확인 실패** — 해당 디바이스 Configuration Handbook으로 재확인 필요 | https://www.intel.com/content/www/us/en/docs/programmable/683762/22-1/avalon-st-configuration.html |
| B03 / B04 / J13 / B01 (이 워크스페이스) | — | BSP drop 내용물, 벤더 SDK 통합 4단계, 회로도·bring-up 협업, POST·보드 리비전 | `bsp/B03`, `bsp/B04`, `jd/J13`, `bsp/B01` |

> **벤더 내부 정보 없음**: Qualcomm·Ambiq 등의 SDK·BSP 구조, 레지스터 이름, 부트 스테이지 명칭은 NDA·포털 배포라 공개 1차 문서가 없다. 이 노트는 의도적으로 쓰지 않았고, 면접에서도 쓰지 않는 게 맞다.
> **`[벤더 의존]` 표기 항목**: FPGA 전용 구성 핀 목록과 `M[2:0]` 비트 값, 비휘발 구성 메모리 보유 여부, 비트스트림 서명·암호화 지원 방식, 실리콘 리비전 표기 규칙(A0/B0), 구성 인터페이스 최대 클럭 — 전부 패밀리·세대마다 다르므로 해당 디바이스 문서에서 확인해야 한다.
> **Don 경험 표기**: 5.2절의 "해봤다"는 context 3.1·3.5절의 레쥬메 인용(Cortex-R8/R82/M0+ FPGA pre-silicon bring-up, I2C/SPI/DMA/SRAM/DRAM)에만 근거한다. 5.3절(제품 FPGA)은 **경험이 없으므로 전부 설계안**으로 표기했다. `<확인 필요: ...>` 항목은 Don만 답할 수 있는 사실이다.
