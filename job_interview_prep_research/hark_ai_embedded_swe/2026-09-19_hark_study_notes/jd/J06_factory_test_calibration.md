# J06. Develop factory test and calibration firmware for manufacturing

> **분류**: Responsibility 6/7 · **관련 개념 노트**: C09(factory test·calibration), C07(secure boot·잠금), C03(드라이버), C10(디버깅), S04 Q18~Q23
> **Don 현재 상태**: ✅ 강함 — context 3.1의 "designing and leading **factory test-node architecture**", "Silicon/system bring up → NPI → MP", "SSD/MFG Firmware". **JD 7개 Responsibility 중 Don이 가장 확실하게 이기는 항목**이다.
> **이 노트를 다 읽으면**: ① factory test 펌웨어를 "디버그 코드"가 아니라 자체 요구사항을 가진 제품으로 설명할 수 있다 · ② 캘리브레이션 데이터의 수명주기(측정→저장→검증→필드→RMA)를 처음부터 끝까지 설계할 수 있다 · ③ Apple 경험을 기밀 없이 일반화해 스타트업 면접관이 듣고 싶은 언어로 옮길 수 있다

> **⚠ 2026-09-23 JD 개정**: 이 항목은 현재 JD에서 삭제됨 — 우선순위 하향, 개념 수준으로만. Bonus의 EVT/DVT/PVT와 "양산까지 끌고 간 경험" 스토리 각도로만 쓴다. [P02 분석](../plan/P02_jd_change_2026-09-23.html)

---

## 0. 문장 뜯어보기

| 구(句) | 표면적 뜻 | 채용담당자가 이 단어를 고른 이유 |
|---|---|---|
| `Develop` | 개발한다 | Collaborate(J05)가 아니라 Develop이다. **소유권이 이 자리에 있다.** 스타트업이고 1세대 기기라 아직 아무것도 없을 가능성이 높다 |
| `factory test` | 공장 테스트 | 개발용 디버그 코드가 아니라 **양산 라인에서 초 단위로 반복 실행되는 코드**. 실패해도 안 되고, 느려도 안 되고, 애매해도 안 된다 |
| `and calibration` | 그리고 캘리브레이션 | test와 calibration은 다른 일이다. test는 합격/불합격 판정, calibration은 **개체마다 다른 숫자를 만들어 기기에 영구 저장**하는 일. 뒤쪽이 훨씬 위험하다 |
| `firmware` | 펌웨어 | 스테이션 소프트웨어(PC 쪽)가 아니라 **DUT 안에서 도는 코드**. 즉 명령 파서, 측정 루틴, 저장 로직, 잠금 절차가 내 코드다 |
| `for manufacturing` | 양산을 위해 | 대상이 R&D가 아니라 CM(위탁 생산업체) 라인이다. **내가 옆에 없는 상태에서, 영어가 모국어가 아닐 수도 있는 오퍼레이터가, 교대 근무로 돌린다.** 이 제약이 모든 설계 결정을 바꾼다 |

**한 줄 요약**: 이 문장은 "펌웨어에 테스트 명령을 몇 개 추가할 수 있는가"가 아니라 **"수십만 대가 흘러가는 라인에서, 각 기기에 지워지면 안 되는 데이터를 안전하게 써 넣고, 불량을 정해진 시간 안에 판정하는 시스템을 설계할 수 있는가"**를 묻는다.

> Bonus Qualifications의 "Experience shipping consumer electronics through EVT/DVT/PVT milestones"가 이 문장의 짝이다. context 3.1에서 이 항목도 ✅ 강함이다. **두 항목을 한 스토리로 묶어서 말하면 임팩트가 두 배가 된다.**

---

## 1. Hark에서 실제로 하게 될 일 (추정)

[추정] context 2.2·2.4·2.7 근거: 1세대 컨슈머 웨어러블급 기기, Cellular·Wi-Fi·BT·GNSS·NFC·UWB 멀티 라디오, 마이크·센서·햅틱, Qualcomm SoC + always-on MCU, 2026-09 현재 FW·RF·전력·오디오·햅틱·열 공고가 대거 열려 있어 **EVT 전후 단계**로 추정. 확정 사실이 아니다.

### 1.1 지금 Hark의 상황에서 이 역할이 맡을 일

1세대 기기이므로 **아직 factory test 펌웨어가 없을 가능성이 크다**. 즉 이 자리는 "유지보수"가 아니라 "0에서 1"이다.

| 시기 [추정] | 이 역할이 하는 일 | 산출물 |
|---|---|---|
| EVT 이전 | 회로도 리뷰로 test point·strap·디버그 포트·fixture 접점 확보 | 회로도 리뷰 코멘트, DFT 요구사항 문서 |
| EVT | 모든 raw 측정값을 덤프하는 명령 세트 v1, 데이터 수집 시작 | factory 명령 프로토콜 문서, 측정 로그 |
| EVT → DVT | 수집한 분포로 limit 후보 결정, calibration 필요 항목 확정 | cal 데이터 struct v1, limit 표 |
| DVT | CM 테스트 엔지니어와 스테이션 매핑, fixture 브링업, 테스트 시간 단축 | 스테이션별 테스트 리스트, UPH 예산 |
| DVT → PVT | provisioning·잠금 절차 end-to-end 리허설, 수율 데이터 분석 | 잠금 절차 문서, 수율 리포트 |
| PVT → MP | 라인 fail 대응, limit 미세조정, 회귀 방지 | 라인 이슈 트래킹, factory FW 릴리스 |

### 1.2 하루가 이렇게 생겼다 [추정]

- 아침: 야간 라인의 수율 대시보드를 본다. 마이크 스테이션 수율이 97.8% → 94.1%로 떨어져 있다. fail 로그를 받아 raw 측정값 분포를 그려 보니 한 fixture의 측정값만 전체적으로 3dB 낮다. 제품 문제가 아니라 fixture 문제다.
- 오전: CM 테스트 엔지니어와 화상 회의(시차 때문에 이른 아침일 수 있다). fixture #3의 음향 커플러 점검을 요청하고, 그동안 fixture ID를 로그에 반드시 남기도록 명령을 수정한다.
- 오후: 다음 빌드에 들어갈 새 센서의 calibration 루틴을 작성하고, cal 레코드 struct에 필드를 추가한다. 버전을 올리고 마이그레이션 코드를 쓴다.
- 저녁: DVT 데이터 1200대 분량으로 새 limit 후보를 계산해 본다. Cpk가 1.33 이상 나오는지 확인한다.

### 1.3 이 역할이 소유하는 것 / 소유하지 않는 것

- 소유한다: DUT 안에서 도는 테스트 모드 펌웨어, 명령 프로토콜, 측정 루틴, cal 계산과 저장 포맷, provisioning 흐름, 출하 전 잠금 절차, 라인 로그 스키마.
- 소유하지 않는다: 스테이션 PC 소프트웨어(보통 CM 또는 test engineering 팀), fixture 기구 설계, MES 시스템, 라인 오퍼레이션.
- 경계에서 같이 정한다: limit 값, 테스트 순서와 시간 배분, 어떤 스테이션에서 무엇을 할지, fail 코드 체계.

---

## 2. 핵심 개념

### 2.1 factory test 펌웨어는 별도의 요구사항을 가진 "제품"이다

개발용 디버그 셸과 무엇이 다른지가 이 문장의 핵심이다.

| 요구사항 | 개발용 디버그 코드 | factory test 펌웨어 |
|---|---|---|
| 결과의 성격 | 사람이 읽고 판단 | **기계가 파싱해 PASS/FAIL로 기록** |
| 실행 시간 | 신경 안 씀 | 초 단위로 예산이 있음. 1초가 곧 돈 |
| 재현성 | "대충 되면 됨" | 같은 기기·같은 조건이면 항상 같은 결과 |
| 애매한 상태 | 사람이 다시 해 봄 | **금지.** 타임아웃과 명시적 실패 코드가 필요 |
| 버전 관리 | 느슨 | 라인에 어떤 버전이 깔렸는지 항상 추적 가능해야 함 |
| 보안 | 상관없음 | 출하 전에 반드시 비활성화·잠금 |
| 대상 사용자 | 나 | 내가 본 적 없는 오퍼레이터, 24시간 3교대 |
| 실패 비용 | 시간 낭비 | 라인 정지, 재작업, 폐기 |

> **면접에서 쓸 한 문장**: "Factory firmware is a product with its own users, its own SLA and its own release process. The mistake I see is treating it as debug code that happened to ship."

### 2.2 제조 흐름 — 어디에 내 코드가 들어가나

```
[부품 입고]
   |
   v
[SMT]  보드에 부품 실장, 리플로우
   |
   v
[ICT / boundary scan]  전기적 연결 검사 (쇼트, 오픈, 부품 값)
   |                    -- 아직 펌웨어 없음. bed-of-nails 프로브
   v
[Flash / Provisioning]  부트로더 + factory FW 굽기, 시리얼·키 주입
   |                    <-- 여기부터 내 코드
   v
[FCT (Functional Test)]  전원, 클럭, 각 주변장치, 라디오, 센서 기능 검사
   |                      <-- 내 명령 세트가 실행됨
   v
[조립]  하우징, 마이크, 스피커, 배터리, 안테나 결합
   |
   v
[FATP - Final Assembly, Test, Pack]
   |-- 음향 테스트 (마이크·스피커), 햅틱, 버튼, 디스플레이
   |-- RF 최종 확인 (안테나 포함 상태)
   |-- 센서 calibration (IMU, 조도 등)
   |-- 배터리 게이지 확인
   |-- 충전 테스트
   |
   v
[Run-in / Burn-in]  일정 시간 동작시켜 조기 불량 걸러내기 (제품마다 유무 다름)
   |
   v
[출하 FW 굽기 + 잠금]  양산 펌웨어로 교체, 디버그 포트·테스트 모드 잠금
   |
   v
[OQC / OOBA]  샘플링 검사
   |
   v
[포장 · 출하]
```

핵심 통찰 두 가지.

1. **같은 테스트를 여러 번 한다.** 보드 단계(FCT)에서 마이크 I2S 링크를 확인하고, 조립 후(FATP)에 음향 성능을 다시 본다. 이유는 조립 공정에서 새 불량이 생기기 때문이다. 즉 "왜 두 번 하냐"는 질문에는 "커버 씌운 뒤에만 보이는 불량이 있다"가 답이다.
2. **불량은 최대한 앞 공정에서 잡아야 싸다.** 보드 단계에서 잡으면 보드만 버리지만, FATP에서 잡으면 하우징·배터리·마이크가 함께 날아간다. 이걸 "test earliest, test cheapest" 원칙이라고 부른다. 그래서 **내 코드가 FCT에서 얼마나 많이 잡아 주느냐가 직접적인 비용 절감**이다.

### 2.3 스테이션과 fixture — DUT는 무엇과 대화하나

```
 [스테이션 PC]  테스트 시퀀서 (CM 소유)
      |  USB / Ethernet
      v
 [fixture 컨트롤러]  릴레이, 전원, 계측기 제어
      |
      +-- 전원 공급 (프로그래머블 PSU, 전류 측정 가능)
      +-- 통신 라인 (UART / USB / SWD)  <-- 내 명령 프로토콜이 여기로
      +-- 자극 장비 (스피커, 광원, 회전 스테이지, RF 커플러)
      +-- 측정 장비 (레퍼런스 마이크, 전류계, RF 계측기)
      +-- 기구 (포고핀, 클램프, 차폐 박스, 무향 커플러)
      |
      v
   [DUT]  내 factory 펌웨어가 돌고 있음
```

펌웨어 관점에서 반드시 정의해야 할 것들.

| 항목 | 결정해야 할 것 | 흔한 실수 |
|---|---|---|
| 물리 인터페이스 | UART? USB CDC? SWD? 포고핀 몇 핀? | 조립 후에는 접근 불가능한 핀을 쓰기로 정함 |
| factory mode 진입 | GPIO strap? 부팅 시 매직 명령? 별도 이미지? | 필드 기기에서도 들어가지는 방법을 남김 |
| 전원 상태 | 배터리 없이 fixture 전원만으로 동작 가능한가 | 배터리 연결 전 단계 테스트를 못 하게 설계 |
| 리셋/타임아웃 | 명령이 멈추면 얼마 뒤 무엇을 하나 | 타임아웃 없음 → 라인 정지 |
| 동시성 | 한 fixture에 DUT 여러 대(멀티업)인가 | 나중에 멀티업으로 바꿀 수 없는 프로토콜 |

### 2.4 캘리브레이션 데이터의 수명주기

이 JD 문장에서 가장 위험하고 가장 티가 나는 부분이다. **한 번 잘못 쓰면 필드에서 되돌릴 수 없다.**

```
(1) 측정        레퍼런스 자극 + DUT 측정 -> raw 값
      |          예: 94 dB SPL 기준 톤을 주고 DUT 마이크의 디지털 레벨을 읽음
      v
(2) 계산        offset/gain 계수 산출. 범위 체크(말도 안 되는 값이면 FAIL)
      |
      v
(3) 저장        비휘발성 영역에 버전 + CRC + 이중 사본으로 기록
      |
      v
(4) 즉시 검증   다시 읽어서 CRC 확인 + 같은 자극으로 재측정해 보정 후 값이 맞는지
      |          <-- 이 단계를 빼면 필드에서 터진다
      v
(5) 업로드      MES에 시리얼 + raw + 계수 + 스테이션 ID + 시각 기록
      |
      v
(6) 사용        양산 펌웨어가 부팅 시 읽어 적용. 없거나 깨졌으면 안전한 기본값 + 플래그
      |
      v
(7) 필드        OTA로 펌웨어가 바뀌어도 cal은 절대 지워지지 않아야 함
      |
      v
(8) RMA/수리    부품 교체 시 재캘리브레이션 절차. 시리얼로 원래 값 조회 가능해야
```

**설계 규칙 여섯 가지.**

1. raw 측정값도 함께 저장한다. 계수만 저장하면 나중에 알고리즘이 바뀌었을 때 재계산할 수 없다.
2. 버전 필드를 처음부터 넣는다. 안 넣으면 반드시 후회한다.
3. CRC를 넣고, 사본을 두 개 둔다. 쓰는 도중에 전원이 끊겨도 하나는 살아 있어야 한다.
4. 쓴 직후 반드시 다시 읽어 검증한다.
5. cal 영역은 OTA·factory reset·공장 초기화 어느 것으로도 지워지지 않는 영역에 둔다.
6. cal이 없을 때의 동작을 정의한다. 부팅 실패는 최악의 선택이다. 안전한 기본값으로 동작하되 플래그를 telemetry에 올린다.

### 2.5 테스트 시간과 수율의 경제학

면접에서 이 얘기를 숫자로 할 수 있으면 "양산을 실제로 해 본 사람"으로 분류된다.

**UPH (Units Per Hour).**

```
[가정: 아래 숫자는 설명용 예시다. 실제 값은 제품·라인마다 다르다]
한 스테이션 사이클 = 로딩 3s + 테스트 45s + 언로딩 3s + 여유 2s = 53s
1대 fixture UPH = 3600 / 53 ≈ 68 UPH
목표 라인 UPH = 400  ->  필요한 fixture = 400 / 68 ≈ 6대

테스트를 45s -> 30s로 줄이면
사이클 = 38s, UPH ≈ 95  ->  필요 fixture = 400 / 95 ≈ 4.2 -> 5대
=> fixture 1대분의 장비·공간·유지비를 절약
```

그래서 "테스트 시간 15초 단축"은 엔지니어의 자기만족이 아니라 **설비 투자 감소**다. 이 연결을 말할 수 있는지가 관건이다.

**수율 용어를 정확히 쓴다.**

| 용어 | 정의 | 흔한 오용 |
|---|---|---|
| FPY (First Pass Yield) | 재작업 없이 한 번에 통과한 비율 | 재테스트 통과분을 포함시켜 부풀림 |
| RTY (Rolled Throughput Yield) | 전 공정 FPY의 곱 | 단일 공정 수율만 보고 전체를 낙관 |
| Retest / Rework 비율 | 다시 돌린 비율 | 이게 높으면 수율이 좋아 보여도 라인은 망가진 것 |
| NTF / NFF (No Trouble Found) | fail로 잡혔는데 재현 안 됨 | 대부분 fixture·접촉 문제. **테스트 설계 결함 신호** |
| Cpk | 공정 능력 지수. limit 대비 분포의 여유 | 1.33 이상을 흔히 목표로 삼는다(업계 관행, 제품마다 다름) |
| Gauge R&R | 측정 시스템 자체의 반복성·재현성 | 측정기 오차가 큰데 제품 불량으로 오판 |

**limit을 정하는 법.** 사양(spec)을 그대로 limit으로 쓰면 안 된다. 측정 불확도와 공정 드리프트를 고려해 **사양보다 안쪽(guard band)**에 잡는다. 그리고 limit은 추측이 아니라 EVT/DVT 데이터 분포에서 나온다.

```
                 스펙 하한                                    스펙 상한
   ----|------------|---------[ 실제 분포 ]---------|------------|----
            guard band |<-- 테스트 limit -->|  guard band
```

> **면접 포인트**: "limit을 어떻게 정했나"라는 질문에 "데이터 분포를 보고 Cpk와 측정 불확도를 함께 고려해 guard band를 뒀다"고 답하면, "사양서 값을 썼다"는 답과 완전히 다른 레벨로 들린다.

### 2.6 출하 전 잠금 (end-of-line lock)

factory 펌웨어의 마지막 책임은 **자기 자신을 못 쓰게 만드는 것**이다.

| 잠글 대상 | 방법 | 주의 |
|---|---|---|
| 디버그 포트(SWD/JTAG) | 벤더별 보호 비트 (예: nRF의 APPROTECT, STM32의 RDP) — **벤더·실리콘 리비전마다 이름과 동작이 다름** | 되돌릴 수 없는 경우가 많다. 리허설 필수 |
| factory 명령 인터페이스 | 양산 FW에서 컴파일 아웃 또는 런타임 비활성 | 조건부 활성화는 우회 위험 |
| 테스트 모드 진입 strap | 퓨즈/OTP 비트로 영구 비활성 | RMA 절차와 충돌 가능 |
| secure boot 활성화 | OTP에 키 해시 기록 후 활성 | 한 번 하면 끝. 잘못된 키면 전량 폐기 |
| provisioning 영역 | write-once 또는 lock 비트 | 재작업 불가 |

**잠금은 순서와 되돌릴 수 없음(irreversibility) 때문에 가장 사고가 많이 나는 단계다.** 규칙: ① 잠금은 항상 마지막 스테이션 ② 잠금 전에 모든 데이터가 저장·검증·업로드되었는지 확인 ③ 잠금 절차는 PVT에서 실제 기기로 end-to-end 리허설 ④ 잠긴 기기로 RMA 분석을 어떻게 할지 미리 정한다.

---

## 3. 실무 패턴과 함정

### 3.1 명령 프로토콜 설계 — 여섯 가지 규칙

1. **한 줄 명령, 한 줄(또는 정해진 블록) 응답.** 스테이션 소프트웨어가 파싱하기 쉬워야 한다.
2. **모든 응답에 상태 코드.** PASS/FAIL이 아니라 구체적 실패 코드. "왜 실패했나"를 나중에 로그만 보고 알 수 있어야 한다.
3. **모든 측정 명령은 raw 값을 함께 돌려준다.** PASS/FAIL 판정은 가능하면 스테이션(또는 상위 시스템)이 하게 한다. 그래야 limit을 펌웨어 재빌드 없이 바꿀 수 있다.
4. **모든 명령에 타임아웃.** 응답 없는 상태가 라인 정지의 1번 원인이다.
5. **버전 명령을 반드시 둔다.** `VER` 하나로 FW 버전, 빌드 해시, 프로토콜 버전을 알 수 있어야 한다.
6. **멱등성(idempotency)을 고려한다.** 같은 명령을 두 번 보내도 안전한가. 특히 쓰기 명령.

```c
/* factory 명령 응답 포맷 예시.
   숫자·명령 이름은 모두 [가정]이며, 실제 설계는 팀과 합의해 정한다. */

/* 요청:   MIC_MEAS 0            (0번 마이크 측정)
   응답:   OK MIC_MEAS ch=0 raw=-26.4 dbfs=-26.4 t_ms=120
   실패:   ERR MIC_MEAS code=3 msg=no_i2s_clock
   원칙: 펌웨어는 raw를 주고, PASS/FAIL은 스테이션이 limit으로 판단한다. */

typedef enum {
    FT_OK              = 0,
    FT_ERR_BAD_ARG     = 1,
    FT_ERR_TIMEOUT     = 2,
    FT_ERR_NO_CLOCK    = 3,
    FT_ERR_OUT_OF_RANGE= 4,
    FT_ERR_NVM_WRITE   = 5,
    FT_ERR_NOT_ALLOWED = 6,   /* 잠긴 기기에서 factory 명령 시도 */
    FT_ERR_UNKNOWN_CMD = 7
} ft_status_t;

/* 명령 테이블. 문자열 비교 대신 테이블 디스패치로 두면 추가가 쉽다. */
typedef ft_status_t (*ft_handler_t)(int argc, char **argv, char *out, size_t out_len);

typedef struct {
    const char  *name;
    ft_handler_t fn;
    uint16_t     max_ms;      /* 이 명령의 최대 허용 시간. 넘으면 강제 종료 */
    uint8_t      needs_unlock;/* 잠금 해제 상태에서만 허용되는가 */
} ft_cmd_t;
```

> C09 §3.2에 줄 단위 파서와 디스패치 테이블의 전체 구현이 있다. 여기서는 **설계 규칙**만 다루고 구현은 그쪽을 본다.

### 3.2 cal 레코드 — 전원이 끊겨도 살아남는 구조

```c
/* cal 레코드: 버전 + CRC + 이중 사본.
   필드 구성과 크기는 [가정]이며 제품마다 다르다. */
#include <stdint.h>

#define CAL_MAGIC        0x43414C31u   /* "CAL1" */
#define CAL_VERSION      2u

typedef struct {
    int16_t  mic_gain_q8[2];     /* 마이크별 게인 보정, Q8 고정소수점 */
    int16_t  mic_raw_dbfs_q8[2]; /* raw 측정값도 보관 -> 나중에 재계산 가능 */
    int16_t  accel_offset[3];    /* LSB 단위 */
    int16_t  accel_gain_q12[3];
    int16_t  batt_gauge_offset_mv;
    uint8_t  reserved[16];       /* 미래 확장. 0xFF로 채워 둔다 */
} cal_payload_t;

typedef struct {
    uint32_t      magic;
    uint16_t      version;
    uint16_t      payload_len;
    uint32_t      write_count;   /* 어느 사본이 최신인지 판단용 */
    cal_payload_t payload;
    uint32_t      crc32;         /* magic부터 payload 끝까지 */
} cal_record_t;

/* 두 사본을 서로 다른 flash 섹터(또는 페이지)에 둔다.
   규칙: 항상 오래된 쪽을 먼저 지우고 쓴다. 최신 쪽은 건드리지 않는다.
        읽을 때는 CRC가 맞는 것 중 write_count가 큰 쪽을 택한다. */
```

읽기 로직의 상태 공간을 표로 미리 정해 두면 실수가 없다.

| 사본 A | 사본 B | 결정 |
|---|---|---|
| CRC OK | CRC OK | write_count가 큰 쪽 사용 |
| CRC OK | 손상 | A 사용, B를 A로 복구 |
| 손상 | CRC OK | B 사용, A를 B로 복구 |
| 손상 | 손상 | **cal 없음 상태.** 안전한 기본값 + telemetry 플래그 + factory 재캘 필요 표시 |
| 빈 상태(지워짐) | 빈 상태 | 미캘리브레이션 기기. factory 모드 아니면 경고 |
| version 낮음 | — | 마이그레이션 코드 실행 후 새 버전으로 재기록 |

> **면접 포인트**: "손상/손상"과 "version 낮음" 두 줄을 먼저 말하는 사람은 드물다. 실제로 필드를 겪어 본 사람만 이 케이스를 먼저 떠올린다.

### 3.3 캘리브레이션 항목별 구체 (웨어러블 기준)

| 항목 | 자극 | 측정 | 저장되는 것 | 함정 |
|---|---|---|---|---|
| 마이크 감도 | 음향 커플러로 기준 톤 (예: 1 kHz, 94 dB SPL — IEC 계열 사운드 캘리브레이터의 표준 기준점) | DUT가 읽은 디지털 레벨 | 게인 보정 계수 + raw | 차폐·커플러 누설, fixture별 편차, 주변 소음 |
| 마이크 위상/매칭 (빔포밍용) | 동일 음원 | 두 마이크의 레벨·지연 차 | 채널 간 보정 | 마이크 포트 막힘(조립 불량)과 구분 필요 |
| 가속도계 offset/gain | 중력 (6면 정지) | 축별 출력 | 축별 offset·gain | 기구 정렬 오차, 진동, 시간 소요(6면은 느리다) |
| 자이로 bias | 정지 상태 | 출력 평균 | bias | 온도 의존성이 크다. 온도도 같이 기록 |
| 배터리 게이지 | 알려진 부하/전압 | 측정 전압·전류 | offset | 배터리 개체차, 온도 |
| 조도/근접 센서 | 기준 광원 | 카운트 | 스케일 | 하우징 투과율이 들어가므로 조립 후에 해야 함 |
| RF 출력/주파수 | RF 커플러, 계측기 | 출력 파워, 주파수 오차 | 파워 테이블, 주파수 트림 | 규제 관련이라 절차가 엄격. 보통 RF 팀이 소유 |
| 햅틱 | 구동 | 가속도계 또는 마이크로 응답 | 구동 파라미터 | 기구 결합 상태에 크게 의존 |

**마이크 calibration은 Hark 기기에서 특히 중요하다.** 웨이크워드 모델의 입력이 마이크 레벨이기 때문이다(J05 §3.4). 개체마다 감도가 다른 상태로 나가면 필드 정확도가 들쑥날쑥해지고, 그 원인은 절대 모델 쪽에서 안 보인다. **factory calibration이 곧 on-device AI의 정확도 관리**라는 연결을 면접에서 말하면 J05와 J06을 한 번에 묶을 수 있다.

### 3.4 CM / NPI 팀과 일하는 법

| 상대 | 그들이 원하는 것 | 내가 줘야 하는 것 | 마찰 지점 |
|---|---|---|---|
| CM 테스트 엔지니어 | 명확한 명령 문서, 안 멈추는 펌웨어, 빠른 대응 | 프로토콜 문서, 실패 코드 표, 버전 명령 | 시차. 밤에 라인이 돈다 |
| NPI / 제품 엔지니어 | 수율, 테스트 시간, 일정 | 커버리지와 시간의 trade-off를 숫자로 | "테스트 하나 더 넣자" vs UPH |
| HW 엔지니어 | test point 요구를 늦게 받고 싶지 않음 | **EVT 회로도 리뷰 단계에서 DFT 요구를 낸다** | 늦게 요구하면 보드 리스핀 |
| 품질 팀 | 추적성, limit 근거 | 데이터 기반 limit, Cpk | "왜 이 limit인가" 문서화 요구 |
| 보안 팀 | 키·인증서 주입의 안전성 | provisioning 흐름 문서 | HSM 접근, 키가 CM에 노출되면 안 됨 |

**실무에서 가장 중요한 습관 두 가지.**

1. **모든 것을 로그로 남긴다.** 시리얼, FW 버전, 스테이션 ID, fixture ID, 시각, 온도, raw 측정값 전부. "왜 이 로트만 수율이 나쁜가"는 나중에 로그로만 풀린다. 그리고 **필드 telemetry와 같은 스키마로 만들어 시리얼로 조인**할 수 있게 한다.
2. **시차를 설계에 반영한다.** 내가 자는 동안 라인이 돈다. 그래서 펌웨어는 "이상하면 사람을 부른다"가 아니라 "이상해도 안전하게 실패하고 다음 유닛으로 넘어간다"여야 한다.

### 3.5 함정 표

| 함정 | 증상 | 근본 원인 | 대응 |
|---|---|---|---|
| 타임아웃 없는 명령 | 라인 정지, 오퍼레이터가 전원 재투입 | 응답 없는 상태 정의 안 함 | 모든 명령에 max_ms, 워치독 |
| cal 쓰는 중 전원 차단 | 필드에서 센서 이상 동작 | 단일 사본, 검증 없음 | 이중 사본 + CRC + 쓴 뒤 재검증 |
| 펌웨어가 PASS/FAIL 판정 | limit 바꿀 때마다 FW 재릴리스 | 판정 로직을 DUT에 둠 | raw를 올리고 판정은 스테이션에서 |
| fixture ID 미기록 | 특정 fixture 불량을 몇 주 동안 못 찾음 | 로그 스키마 부실 | 스테이션·fixture ID 필수 필드 |
| NTF 비율 높음 | fail인데 재테스트하면 통과 | 접촉 불량, 측정 불확도, limit이 분포 가장자리 | Gauge R&R 먼저, 그다음 limit 재검토 |
| factory 명령이 양산 FW에 남음 | 보안 취약점, 인증 문제 | 컴파일 아웃 안 함 | 빌드 플래그 분리 + 출하 이미지 자동 검사 |
| 디버그 포트 잠금 리허설 안 함 | PVT에서 전량 벽돌 위험 | 되돌릴 수 없는 동작 | PVT 전에 실제 기기로 end-to-end 리허설 |
| 테스트 순서 미최적화 | 테스트 시간 초과 | 느린 테스트를 앞에 둠 | **빨리 많이 걸러내는 테스트를 앞으로** |
| 온도 미기록 | 계절·교대별 수율 변동 원인 불명 | 환경 변수 무시 | 모든 측정에 온도 동반 기록 |
| cal이 OTA로 지워짐 | 업데이트 후 대량 이상 | 파티션 설계 실수 | cal 영역을 업데이트 대상에서 물리적으로 분리 |
| 시리얼 중복 | MES 데이터 꼬임 | provisioning 재시도 처리 미흡 | 멱등성 있는 provisioning, write-once |

### 3.6 테스트 순서 최적화 — 싸고 빠른 것부터

```
나쁜 순서                          좋은 순서
1. 마이크 음향 테스트 (12s)        1. 전원·전류 확인 (0.5s)   <- 불량이면 즉시 중단
2. 배터리 충전 확인 (8s)           2. 통신 링크·버전 (0.3s)
3. 전원·전류 확인 (0.5s)           3. 각 버스 스캔 I2C/SPI (1s) <- 조립 불량 대부분 여기서
4. 통신 링크 (0.3s)                4. 센서 ID 읽기 (0.5s)
                                   5. 마이크 음향 (12s)
불량품에 20s를 다 쓴다             6. 배터리 충전 (8s)
                                   불량품은 평균 2s 안에 걸러진다
```

**원칙**: 실패 확률이 높고 빠른 테스트를 앞으로. 이것만으로 불량품의 평균 테스트 시간이 크게 줄고, 수율이 낮은 시기에 UPH가 덜 무너진다.

---

## 4. 리서치 — 근거 자료

이 주제는 ML이나 프로토콜과 달리 **공개된 단일 표준 문서가 거의 없다.** 대부분 회사 내부 관행이다. 그래서 아래는 (a) 통계·품질 쪽 공개 표준, (b) 실제로 쓰는 툴 문서, (c) 참고할 수 있는 오픈 생태계로 나눠 정리한다.

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| NIST/SEMATECH e-Handbook of Statistical Methods | 측정 시스템 분석(Gauge R&R), 공정 능력(Cpk), 관리도 | 2장(측정 시스템), 6장(공정 모니터링) | https://www.itl.nist.gov/div898/handbook/ |
| IEEE 1149.1 (JTAG boundary scan) | ICT 단계의 boundary scan 표준 | 개요 수준이면 충분 (표준 본문은 유료) | https://standards.ieee.org/ |
| Zephyr Shell 서브시스템 | factory 명령 인터페이스를 만들 때 바로 쓸 수 있는 셸 | 명령 등록, 백엔드(UART/USB) 설정 | https://docs.zephyrproject.org/latest/services/shell/index.html |
| Zephyr Settings 서브시스템 | 키-값 비휘발성 설정 저장 (cal 저장 후보) | 백엔드와 커밋 시맨틱 | https://docs.zephyrproject.org/latest/services/settings/index.html |
| Zephyr NVS (Non-Volatile Storage) | flash 기반 키-값 저장, 전원 차단 내성 설계 | 동작 원리와 섹터 관리 | https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html |
| Zephyr hwinfo API | `hwinfo_get_device_id()` — 칩 고유 ID 읽기(시리얼 연계) | API와 지원 SoC | https://docs.zephyrproject.org/latest/hardware/peripherals/hwinfo.html |
| MCUboot 문서 | 부트로더·서명·슬롯. factory에서 굽는 이미지 구성과 직결 | 이미지 포맷, 업그레이드 전략 | https://docs.mcuboot.com/ |
| pyOCD | 파이썬 기반 플래싱·디버깅. 스테이션 자동화 스크립트에 흔히 쓰임 | 플래시 프로그래밍 API | https://pyocd.io/ |
| OpenOCD | 오픈소스 JTAG/SWD 툴. 대량 플래싱 스크립트 | 설정 파일과 flash 명령 | https://openocd.org/ |
| DAPLink | 저가 프로브 펌웨어. fixture에 내장하기 좋음 | 지원 보드, 대량 사용 시 고려사항 | https://github.com/ARMmbed/DAPLink |
| Microchip ATECC608 시리즈 | 키 저장·인증용 secure element (provisioning 시 사용되는 대표 부품) | 프로비저닝 모델, lock 개념 | https://www.microchip.com/en-us/product/ATECC608B |
| PSA Certified | 임베디드 보안 인증 체계. root of trust·provisioning 요구사항 참고 | 레벨별 요구사항 개요 | https://www.psacertified.org/ |
| Matter (connectedhomeip) | 기기 인증서(DAC) provisioning을 공개적으로 구현한 드문 사례 | factory data provisioning 관련 문서·툴 | https://github.com/project-chip/connectedhomeip |
| Connectivity Standards Alliance | 위 인증서 체계의 상위 문맥 | 인증·보안 개요 | https://csa-iot.org/ |
| Nordic nRF52840 제품 페이지 | UICR, APPROTECT 같은 벤더 고유 provisioning·잠금 기능의 실제 예 | 제품 사양서의 UICR·디버그 보호 절 | https://www.nordicsemi.com/Products/nRF52840 |

**정확성·버전 주의.**

- **디버그 포트 잠금은 벤더·실리콘 리비전마다 이름과 동작이 완전히 다르다.** 예를 들어 Nordic의 APPROTECT는 특정 리비전에서 동작 방식이 바뀌었고, ST의 RDP는 레벨별로 되돌림 가능 여부가 다르다. **면접에서 특정 비트 이름을 단정하지 말고 "벤더마다 다르며, 되돌릴 수 없는 경우가 많아 리허설이 필수"라고 말하는 것이 정확하다.**
- Cpk 목표 1.33은 업계에서 널리 쓰이는 관행 값이지 표준이 정한 수치가 아니다. 회사·제품군마다 다르다.
- 94 dB SPL @ 1 kHz는 사운드 캘리브레이터의 대표적 기준점이지만, 실제 factory 음향 테스트는 커플러 구조와 제품 사양에 따라 다른 레벨·주파수를 쓴다.
- ICT/FCT/FATP 같은 용어의 정확한 경계는 회사마다 다르다. 면접에서 정의를 말할 때 "우리가 쓰던 정의로는"이라고 붙이면 안전하다.
- MES 연동 방식, 로그 포맷, limit 관리 도구는 전부 회사 내부 시스템이다. 일반 원리로만 말한다.

---

## 5. 예상 면접 질문

### Q01. How would you design factory test firmware for a brand-new device from scratch?

**왜 묻나**: 이 JD 문장의 핵심. 1세대 기기라 "처음부터"가 실제 상황이다.

**30초 답변**: 먼저 무엇을 검증해야 하는지를 스테이션 단위로 나눈다. 보드 단계에서 잡을 것과 조립 후에만 보이는 것을 구분한다. 그다음 세 가지를 문서로 고정한다. 명령 프로토콜, cal 데이터 계약, 실패 코드 체계. 펌웨어는 raw 측정값을 주고 판정은 스테이션이 하게 해서 limit을 펌웨어 재빌드 없이 바꿀 수 있게 한다. 모든 명령에 타임아웃을 둔다. 그리고 EVT 회로도 리뷰 때 test point와 fixture 접점을 확보하는 것이 가장 먼저 할 일이다.

**English answer**: I start before the board exists, in the schematic review, making sure we have test points, a factory communication path, and strap or fixture access that still works after assembly. Then I split the work by station: what can only be caught on a bare board versus what only appears after the housing goes on. Three things get written down early and versioned: the command protocol, the calibration data contract, and the failure code list. The firmware reports raw measurements and lets the station apply limits, so we can retune limits from data without a firmware release. Every command has a timeout, because a hung command stops a line. And everything gets logged with serial, firmware version, station and fixture ID, so that when yield moves we can actually explain it.

**꼬리질문**: (a) "가장 먼저 만드는 명령은?" → 버전 조회와 통신 확인. 이게 없으면 라인에 어떤 FW가 깔렸는지도 모른다. (b) "별도 factory 이미지인가, 양산 이미지에 포함인가?" → 둘 다 쓴다. 보통 별도 이미지가 깔끔하지만 굽는 시간이 든다. trade-off를 말한다.

---

### Q02. What's the difference between factory test firmware and your normal debug shell?

**왜 묻나**: 2.1절 표 그대로. 양산을 실제로 해 봤는지가 여기서 갈린다.

**30초 답변**: 사용자가 다르다. 디버그 셸은 내가 읽고 판단하지만 factory는 기계가 파싱하고 판정한다. 그래서 출력 포맷이 고정이어야 하고, 애매한 상태가 없어야 하고, 타임아웃이 있어야 하고, 시간 예산이 있고, 버전 추적이 되어야 하고, 출하 전에 잠겨야 한다. 한마디로 factory 펌웨어는 자체 릴리스 프로세스를 가진 제품이다.

**English answer**: The user. A debug shell is read by me; factory firmware is parsed by a machine and turned into a pass or fail record that someone will audit a year later. That changes everything: fixed output format, no ambiguous states, a timeout on every command, a time budget measured in seconds, strict version traceability, and a lockdown step before shipping. I treat factory firmware as a product with its own release process, not as debug code that happened to ship. The most common failure I've seen is teams discovering at PVT that their debug shell can't be automated.

**꼬리질문**: (a) "애매한 상태가 왜 문제인가?" → 오퍼레이터가 판단하게 되고, 판단이 교대마다 달라져 데이터가 오염된다.

---

### Q03. Walk me through calibrating the microphones on a wearable. Where does the data go?

**왜 묻나**: Hark 기기는 마이크가 핵심이다. cal 수명주기를 아는지 본다.

**30초 답변**: 음향 커플러로 기준 톤을 주고 DUT가 읽은 디지털 레벨을 측정해 게인 보정 계수를 계산한다. 계수와 raw 값을 함께 저장한다. 저장은 버전과 CRC를 포함한 레코드를 두 사본으로 쓰고, 쓴 직후 다시 읽어 검증하고, 같은 자극으로 보정 후 값이 맞는지 재확인한다. 그다음 MES에 시리얼과 함께 올린다. 양산 펌웨어는 부팅 시 읽어 적용하고, 없거나 깨졌으면 안전한 기본값으로 동작하면서 플래그를 telemetry에 올린다. cal 영역은 OTA로 절대 지워지지 않는 곳에 둔다.

**English answer**: A coupler delivers a reference tone, the device reports the digital level it measured, and we compute a per-microphone gain correction. I store both the correction and the raw measurement, because if the algorithm changes later I want to recompute without recalling units. The record carries a magic, a version, a length and a CRC, and it's written to two copies in separate sectors so a power cut can't lose both. Immediately after writing I read it back, verify the CRC, and re-measure with the correction applied to prove it actually works. Then the serial, raw value, coefficient, station and fixture ID go to MES. In production firmware, missing or corrupt calibration must not brick the device: we fall back to a safe default and raise a telemetry flag. And the calibration partition is outside anything OTA can touch.

**꼬리질문**: (a) "두 마이크의 편차가 크면?" → 빔포밍이 망가진다. 채널 간 매칭도 limit으로 관리한다. (b) "마이크 포트가 막힌 불량과 감도 편차를 어떻게 구분하나?" → 막힘은 주파수 응답 모양이 달라진다. 단일 톤 레벨만 보면 구분이 안 되므로 스윕이나 다중 톤을 쓴다.

---

### Q04. Factory yield on the microphone station dropped from 98% to 91% overnight. How do you approach it?

**왜 묻나**: 실제로 가장 자주 일어나는 상황. 구조적으로 좁히는지 본다.

**30초 답변**: 먼저 제품 문제인지 측정 문제인지 나눈다. fail한 유닛의 raw 측정값 분포를 본다. 분포 전체가 이동했으면 fixture나 환경, 꼬리만 두꺼워졌으면 부품 로트를 의심한다. 그다음 층별로 쪼갠다. fixture별, 스테이션별, 교대별, 로트별. 한 fixture만 나쁘면 fixture 문제다. 그리고 NTF 비율을 본다. 재테스트하면 통과하는 비율이 높으면 접촉이나 측정 불확도 문제다. 마지막으로 골든 유닛을 각 fixture에 돌려 상관성을 확인한다.

**English answer**: I don't debug the product first — I separate product variation from measurement variation. I pull the raw values for all units, not just pass/fail, and look at the distribution. If the whole distribution shifted, that points at the fixture or the environment; if only the tail got heavier, I suspect a component lot. Then I stratify: by fixture, by station, by shift, by material lot. A single bad fixture shows up immediately that way. I check the retest and no-trouble-found rate, because a high NTF rate means contact or measurement repeatability, not product quality. And I run golden units through every fixture to check correlation. That whole sequence is usually a few hours, and most of the time the answer is a fixture, not the design.

**꼬리질문**: (a) "골든 유닛은 어떻게 관리하나?" → 고정된 몇 대를 교정 주기마다 돌려 fixture 간 상관을 본다. 변형·손상되면 교체하고 이력을 남긴다. (b) "limit을 완화해도 되나?" → 데이터 없이는 안 된다. Cpk와 필드 영향 분석이 먼저다. limit 완화는 되돌리기 어렵다.

---

### Q05. How do you decide the test limits?

**왜 묻나**: "사양서 값을 썼다"고 답하면 초보다.

**30초 답변**: 사양을 그대로 쓰지 않는다. EVT/DVT 데이터로 실제 분포를 보고, 측정 시스템의 반복성(Gauge R&R)을 먼저 확인한 다음, 측정 불확도와 공정 드리프트를 고려해 사양 안쪽에 guard band를 두고 limit을 잡는다. Cpk 목표를 정해 두고 그 아래면 공정이나 설계를 고친다. 그리고 limit은 펌웨어가 아니라 스테이션 설정에 두어 데이터가 쌓이면 바꿀 수 있게 한다.

**English answer**: Never straight from the datasheet. First I want to know whether the measurement itself is trustworthy — a Gauge R and R study, because if measurement variation is a big fraction of the tolerance, no limit will behave. Then I look at the real distribution from EVT and DVT units, set a guard band inside the spec to absorb measurement uncertainty and process drift, and check the resulting Cpk against a target. If Cpk is poor, that's a design or process conversation, not a limit conversation. And I keep limits in station configuration rather than compiled into firmware, so retuning doesn't require a firmware release and every change is recorded.

**꼬리질문**: (a) "guard band를 너무 넓게 잡으면?" → 좋은 제품을 버린다. 과폐기(overkill)와 유출(escape)의 trade-off다. (b) "Cpk 1.33은 어디서 나온 숫자인가?" → 업계 관행이고 회사마다 다르다고 정확히 말한다.

---

### Q06. How do you keep factory test time down without losing coverage?

**왜 묻나**: 테스트 시간이 돈이라는 감각. UPH 계산을 할 수 있는지.

**30초 답변**: 네 가지를 쓴다. 첫째 순서를 바꾼다. 빠르고 실패 확률이 높은 테스트를 앞에 둬서 불량품이 일찍 떨어지게 한다. 둘째 병렬화한다. 서로 독립적인 측정은 동시에 돌린다. 셋째 중복을 없앤다. 보드 단계와 조립 후에 같은 것을 두 번 하는 경우, 조립으로 바뀌지 않는 항목은 한 번만 한다. 넷째 샘플링한다. 모든 유닛에 할 필요 없는 항목은 로트 단위 샘플링으로 돌린다. 그리고 절감 효과를 UPH와 필요 fixture 수로 환산해 보여 준다.

**English answer**: Four levers. Reorder so the fast, high-yield-impact tests run first and bad units exit in a couple of seconds instead of twenty. Parallelize independent measurements — a lot of test time is waiting for settling, and you can wait for two things at once. Remove duplication between board-level and assembled-level test where the assembly step can't change the result. And move some checks to lot-level sampling instead of one hundred percent. Then I translate the saving into the number that gets attention: seconds per unit becomes units per hour, which becomes how many fixtures we need to buy. Fifteen seconds off a forty-five second test can remove a whole fixture from the line.

**꼬리질문**: (a) "커버리지를 줄였다가 불량이 유출되면?" → 커버리지 삭감은 데이터로 정당화해야 한다. 그 항목이 최근 N만 대에서 몇 건 잡았는지가 근거다. (b) "병렬화의 위험은?" → 측정 간 간섭. 특히 전류·RF·음향은 서로 영향을 준다.

---

### Q07. What gets provisioned on the line, and how do you keep the keys safe?

**왜 묻나**: 보안 bonus qualification과 겹친다. 보안 팀과 일해 본 감각.

**30초 답변**: 시리얼 번호, 기기 고유 식별자, 기기별 키나 인증서, 지역·SKU 설정, 그리고 필요하면 secure boot용 공개키 해시를 OTP에 넣는다. 핵심 원칙은 **개인키가 CM에 평문으로 노출되지 않게 하는 것**이다. 기기 안에서 키쌍을 생성해 공개키만 내보내고 서버가 인증서를 발급해 돌려주는 방식이 가장 안전하다. 그리고 provisioning은 멱등적이어야 한다. 재시도해도 시리얼이 중복 발급되면 안 된다.

**English answer**: Serial number, a device identifier, per-device keys or certificates, regional and SKU configuration, and sometimes a public key hash burned into OTP for secure boot. The rule I hold is that private key material should never exist in plaintext on the contract manufacturer's floor. The cleanest pattern is on-device key generation: the part generates a key pair in its secure element or its own storage, exports only the public key with a proof, and a signing service returns a certificate. That way a compromised station can't leak keys for the whole build. Provisioning also has to be idempotent, because stations retry — if a retry allocates a second serial number, your MES data is corrupt and you may not notice for weeks.

**꼬리질문**: (a) "OTP는 왜 되돌릴 수 없는데 쓰나?" → 되돌릴 수 없어야 신뢰의 근거가 되기 때문이다. 그래서 굽기 전 검증이 중요하다. (b) "잘못된 키를 구웠다면?" → 대부분 폐기다. 그래서 PVT 리허설과 이중 확인 절차가 필수다.

---

### Q08. How do you lock the device down before it ships, and what could go wrong?

**왜 묻나**: 출하 전 잠금은 사고가 가장 많은 단계.

**30초 답변**: 디버그 포트 보호 비트, factory 명령 비활성화, 테스트 모드 진입 경로 차단, secure boot 활성화를 마지막 스테이션에서 정해진 순서로 한다. 위험은 되돌릴 수 없다는 점이다. 그래서 잠금 전에 모든 cal과 provisioning이 저장·검증·업로드되었는지 확인하는 체크를 펌웨어 안에 넣고, PVT에서 실제 기기로 전체 절차를 리허설한다. 그리고 잠긴 기기를 RMA에서 어떻게 분석할지 미리 정한다.

**English answer**: The last station runs a fixed sequence: verify that calibration and provisioning are stored and read back correctly, confirm the production image is what we expect, disable the factory command interface, block the test-mode entry path, enable secure boot, then set the debug protection bit. Order matters and most of it is irreversible, so the firmware itself refuses to lock if any earlier step didn't verify. What goes wrong is almost always the irreversibility: a wrong key hash, locking before the data upload confirmed, or a vendor protection bit that behaves differently on a new silicon revision than the documentation implied. So we rehearse the whole sequence end to end on real units at PVT, and we decide in advance how a locked unit gets analyzed when it comes back as a field return.

**꼬리질문**: (a) "RMA 분석은 어떻게?" → 잠금 전 단계의 리테인 샘플을 두거나, 서명된 디버그 인증(authenticated debug) 메커니즘을 쓴다(지원 여부는 실리콘마다 다름). (b) "테스트 모드를 남겨 두면 안 되나?" → 남기면 공격면이 된다. 남긴다면 인증이 필요하다.

---

### Q09. Where do you store calibration data, and why not just use a file in the main filesystem?

**왜 묻나**: 파티션 설계와 OTA 상호작용.

**30초 답변**: cal은 펌웨어 업데이트나 공장 초기화로 절대 지워지면 안 되는 데이터라서 별도 영역에 둔다. 메인 파일시스템에 두면 OTA 실수 하나로 전량이 날아갈 수 있고, 파일시스템 자체가 손상되면 같이 손상된다. 선택지는 전용 flash 파티션, 내부 OTP/UICR 같은 영역, 또는 secure element다. 변하지 않는 식별 정보는 OTP, 갱신 가능성이 있는 계수는 보호 파티션이 맞다.

**English answer**: Because calibration has a different lifetime than firmware. Firmware gets replaced over the air many times; calibration is written once on the line and must survive every one of those updates, plus a factory reset. Putting it in the main filesystem means one bad partition layout or one careless erase in the bootloader wipes the fleet, and you cannot recover it in the field — the reference stimulus doesn't exist in a customer's living room. So it lives in a dedicated region that the update path never touches, with its own format, version and CRC. Immutable identity like a serial number can go in OTP; coefficients that might be rewritten during repair go in a protected partition.

**꼬리질문**: (a) "OTP는 다시 못 쓰는데 센서를 교체하면?" → 그래서 계수는 OTP가 아니라 재기록 가능한 영역에 둔다. (b) "secure element에 넣으면?" → 접근 제어는 좋지만 읽기 지연과 부품 비용이 든다.

---

### Q10. Define EVT, DVT, PVT, and tell me what firmware must exist at each.

**왜 묻나**: Bonus Qualification 직격. Don의 강점.

**30초 답변**: EVT는 설계 검증 단계로 보드가 처음 나오고 소량이다. 여기서 필요한 건 bring-up 코드와 모든 raw 측정을 덤프하는 명령이다. DVT는 설계 확정 단계로 수백~수천 대를 만들고, 여기서 factory 명령 세트가 안정되고 cal 절차와 limit 후보가 정해진다. PVT는 양산 공정 검증으로 실제 라인에서 실제 fixture로 돌리며, provisioning과 잠금 절차를 리허설한다. MP는 양산이고, 이때 펌웨어는 변경이 아니라 안정성이 우선이다.

**English answer**: EVT is engineering validation — first real boards, small quantity, lots of things broken. Firmware needs bring-up code and a command set that dumps every raw measurement, because that data is what limits will later be derived from. DVT is design validation at hundreds or thousands of units; the factory command set stabilizes, calibration procedures are defined, and limit candidates come out of the distributions. PVT is process validation — real line, real fixtures, real operators — and this is where provisioning and the lockdown sequence get rehearsed end to end, because that's the last chance to find out the lock bit behaves differently than the datasheet said. By MP the priority flips from adding capability to not changing anything.

**꼬리질문**: (a) "각 단계에서 펌웨어 때문에 일정이 밀린 경험이 있나?" → Don의 실제 사례로 답할 것. (b) "EVT에서 limit을 정하면 안 되는 이유는?" → 표본이 너무 적고 공정이 아직 대표성이 없다.

---

### Q11. The test firmware works on your desk but hangs on the line once every few hundred units. How do you debug it?

**왜 묻나**: 재현 안 되는 양산 문제. Don의 root cause 경험과 직결.

**30초 답변**: 먼저 로그로 좁힌다. 언제 멈추는지, 어느 명령에서 멈추는지, 특정 fixture나 교대에 몰려 있는지. 그다음 desk와 라인의 차이를 나열한다. 전원 투입 속도, 접촉 저항, 케이블 길이, 노이즈, 온도, 명령 타이밍, 이전 유닛의 잔여 상태. 대부분 이 목록 안에 있다. 그리고 재현이 어려우면 계측을 붙인다. 문제가 나는 fixture에 로직 분석기를 걸어 두고 트리거를 걸어 잡는다.

**English answer**: Intermittent line failures are almost never the logic; they're the environment. First I narrow with data: which command, which fixture, which shift, how long after power-up. Then I list everything different between my desk and the line — power ramp rate through a fixture, pogo pin contact resistance, cable length and noise, ambient temperature, how fast the sequencer sends the next command, and whether state from the previous unit leaks through. Then I instrument: park a logic analyzer on the suspect fixture with a trigger on the stall condition and let it sit overnight. That's exactly how I've chased interface failures before — you don't reproduce it, you catch it. And regardless of the root cause, the firmware should have had a watchdog and a command timeout so a hang costs one unit, not a line stoppage.

**꼬리질문**: (a) "잔여 상태가 왜 문제인가?" → 유닛마다 완전한 리셋이 보장되지 않으면 이전 테스트의 설정이 남는다. (b) "워치독이 오히려 문제를 숨기지 않나?" → 그래서 워치독 리셋을 반드시 카운트하고 로그에 남긴다.

---

### Q12. How do you connect factory data to what you see in the field?

**왜 묻나**: fleet 운영 감각. J04(OTA telemetry)와 연결.

**30초 답변**: factory 로그와 필드 telemetry를 같은 스키마로 만들고 시리얼로 조인할 수 있게 한다. 그러면 필드에서 반품된 기기의 라인 측정값을 바로 조회할 수 있고, 반대로 "특정 로트의 cal 값이 한쪽으로 치우친 기기들이 필드에서 더 많이 실패하는가" 같은 질문에 답할 수 있다. 이게 되면 limit을 데이터로 조일 수 있다.

**English answer**: I design the factory log and the field telemetry so they can be joined on the serial number, with the same field names and units. Then a returned unit isn't a mystery — I can pull its line measurements, its calibration coefficients, which fixture tested it and when. More usefully, it works the other way: I can ask whether units whose calibration sat near the edge of the limit fail more often in the field, and if they do, that's the evidence for tightening the limit. Without that join, limits stay guesses forever. I've shipped telemetry features into production firmware before, and the hard part is never the logging — it's agreeing on the schema early enough that both sides actually match.

**꼬리질문**: (a) "프라이버시 문제는?" → 필드 telemetry는 개인정보가 아닌 기기 지표만. 정책 팀과 합의 필요. (b) "저장 공간·대역폭은?" → 집계와 샘플링, 이벤트 기반 업로드.

---

### Q13. If you joined and there was no factory firmware yet, what would you do in the first 90 days?

**왜 묻나**: 스타트업 면접의 단골. 이 자리의 실제 상황일 가능성이 높다.

**30초 답변**: 첫 2주는 듣기다. HW 일정, 어떤 CM인지, 무엇을 calibration 해야 하는지, 보안 요구가 무엇인지. 그다음 회로도 리뷰에서 test point와 factory 통신 경로를 확보한다. 이게 가장 시급하다. 놓치면 보드를 다시 만들어야 한다. 그다음 명령 프로토콜 v1과 cal 데이터 계약을 문서로 고정하고, EVT 보드가 나오면 모든 raw를 덤프하는 스크립트로 데이터 수집을 시작한다. 90일 끝에는 DVT에서 쓸 수 있는 명령 세트와, 데이터로 뒷받침된 limit 후보 표가 있어야 한다.

**English answer**: Weeks one and two are listening: the hardware schedule, which contract manufacturer, what actually needs calibrating, what security wants provisioned. Then the urgent thing is the schematic review, because test points, a factory communication path and fixture access are the decisions you cannot undo without a board respin. In parallel I write two documents and get them agreed: the factory command protocol and the calibration data contract, including versioning. When EVT boards arrive, I want raw data flowing from day one, even before any limits exist. By day ninety I'd want a stable command set DVT can run, a calibration record format that's already survived a power-cut test, and a table of limit candidates backed by real distributions rather than datasheet numbers.

**꼬리질문**: (a) "CM이 아직 안 정해졌다면?" → 프로토콜을 스테이션 소프트웨어에 종속되지 않게 설계한다. (b) "HW팀이 test point를 거부하면?" → 비용을 숫자로 말한다. test point 없이 라인에서 불량 분석에 드는 시간과 폐기 비용.

---

### Q14. Tell me about factory test work you've actually done. (경험 질문)

**왜 묻나**: Don의 최대 강점. 여기서 확실히 점수를 내야 한다.

**30초 답변**: Apple에서 factory test-node 아키텍처를 설계하고 리드했다. 새 무선 실리콘을 제품에 통합하면서 bring-up부터 NPI, 양산까지 가져갔고, 테스트 노드가 무엇을 어떻게 검사할지, 커버리지와 테스트 시간을 어떻게 맞출지를 결정했다. 특히 스트레스 시나리오를 넣어 정상 조건에서는 안 보이는 marginal한 부품을 MP 전에 걸러냈다. 그 전에는 엔터프라이즈 SSD의 양산 펌웨어를 했고, 거기서 에러 리포팅과 telemetry를 설계했다.

**English answer**: At Apple I designed and led the factory test node architecture for wireless chipset integration — deciding what each node exercises, what gets measured, and how to balance coverage against test time. I carried new silicon from first power-on through NPI into mass production, so I've lived the whole arc where the test content matures alongside the hardware. One thing I pushed for was stress scenarios rather than only checking pass-fail at nominal conditions, because marginal parts look fine at nominal and fail in the field; we caught latent defects before MP that way. Before Apple I worked on production SSD firmware, including error reporting and telemetry features that shipped to enterprise customers, which is the same instinct applied to the field instead of the line. I should say I can't go into specifics of Apple's products or internal tooling, but I can talk through the architecture and the trade-offs in general terms.

**꼬리질문**: (a) "테스트 시간을 얼마나 줄였나?" → 구체적 수치는 기밀일 수 있으니 접근법과 상대적 개선으로 답한다. (b) "무엇을 잡았나?" → 일반화한 실패 유형으로 답한다. (c) "다시 한다면 뭘 바꾸겠나?" → 좋은 답을 준비해 두면 성숙해 보인다. <확인 필요: Don이 "다시 한다면 바꾸고 싶은 것" 한 가지를 정해 두었는지>

---

## 6. Don 매핑

### 6.1 왜 이 항목이 가장 강한가

context 3.1 매칭표에서 ✅ 강함으로 표시된 항목은 여섯 개인데, 그중 **"Factory test & calibration FW"는 경쟁자가 가장 적은 항목**이다. 이유는 간단하다. 스타트업에 지원하는 펌웨어 엔지니어 대부분은 R&D 경험은 있어도 양산 라인을 끝까지 겪어 본 사람이 드물다. context 3.2에서도 "스타트업은 이 경험이 약한 경우가 많아 차별화 포인트"라고 명시하고 있다.

그리고 Hark는 지금 **1세대 하드웨어를 EVT 전후에서 밀고 있는 것으로 추정**된다(context 2.4). 즉 factory 인프라를 0에서 세울 사람이 곧 필요하다. 타이밍이 맞는다.

### 6.2 사용할 수 있는 레쥬메 근거 (context 3.1·4.4만)

| 레쥬메 근거 | 이 JD 문장에서 쓰이는 지점 | 면접 문장 |
|---|---|---|
| "designing and leading factory test-node architecture" | Q01, Q14의 핵심 근거 | "I designed and led the factory test node architecture for a new wireless chipset." |
| "Silicon/system bring up → NPI → MP" | Q10 (EVT/DVT/PVT) | "I've carried new silicon from first power-on to mass production." |
| 스트레스 시나리오로 latent defect 사전 검출 (context 3.2·4.4) | Q05 (limit), Q06 (커버리지) | "We stressed interfaces instead of only checking pass-fail at nominal." |
| "root-cause analysis of fundamental and interface level failures" | Q04, Q11 (라인 이슈) | "Separating a fixture problem from a product problem is the same skill." |
| "JTAG, Oscilloscope, Logic Analyzer, Power Analyzer", "DSOs and protocol Analyzer" | Q11 (계측을 붙여 잡는다) | "You don't reproduce an intermittent failure — you instrument for it." |
| "SSD/MFG Firmware" (SK hynix) | Q02 (factory FW는 제품이다) | "I've written manufacturing firmware, not just debug shells." |
| "NVMe telemetry 디버그 기능" | Q12 (factory ↔ field 조인) | "I've shipped telemetry into production firmware." |
| "error reporting/handling 설계" (Solidigm) | Q01 (실패 코드 체계) | "A good failure code taxonomy is what makes line data usable." |
| "shmoo·health monitoring (SI 협업)" | Q05 (분포와 margin으로 limit을 정한다) | "Shmoo work is exactly the same idea as guard banding a test limit." |
| "sign off on hardware safety margins (reliability vs performance/power)" | Q05, Q06 (trade-off 결정 권한을 가져 봄) | "I've had to sign off on margin decisions, not just recommend them." |

> **shmoo → guard band 연결은 이 노트에서 만든 새 프레이밍이다.** shmoo plot으로 동작 영역을 그려 margin을 정하는 일과, 분포를 보고 guard band를 두어 limit을 정하는 일은 사고 구조가 같다. 이 연결을 말하면 SSD 경험이 컨슈머 양산 경험으로 자연스럽게 번역된다.

### 6.3 STAR 스토리 틀 (context 4.4 근거, 기밀 없이)

**스토리 A — factory test-node 아키텍처.**
Situation: 새 무선 실리콘을 출하 제품에 통합하는 프로그램에서 양산 테스트 체계가 필요했다.
Task: 테스트 노드가 무엇을 검사하고, 커버리지와 테스트 시간을 어떻게 맞출지 설계·리드.
Action: 검사 항목을 스테이션별로 나누고, 정상 조건 통과만이 아니라 스트레스 조건을 넣어 marginal 부품이 드러나게 했다. 측정은 raw로 수집해 분포를 보고 판단 기준을 정했다.
Result: MP 이전에 잠재 불량을 걸러냈다. (수치는 기밀일 수 있으므로 접근법 중심으로 말한다.)

**스토리 B — 인터페이스 장애 root cause.**
context 4.4의 "특정 버스 장애 1건: 증상 → 가설 → 측정(DSO/프로토콜 분석기) → 원인 → 수정 → 재발 방지" 틀을 그대로 쓴다. Q11(라인 간헐 행) 답변의 뒷받침으로 붙인다.

**스토리 C — 양산 펌웨어의 에러 처리·telemetry.**
Q12의 근거. factory 로그와 필드 데이터를 같은 체계로 본다는 주장을 뒷받침한다.

### 6.4 주의할 점

- **Apple 관련 구체 정보는 말하지 않는다.** 제품명, 수치, 내부 도구 이름, 공정 세부. Q14 영어 답변에 넣어 둔 "I can't go into specifics of Apple's products or internal tooling, but I can talk through the architecture and the trade-offs" 문장을 준비해 두면 자연스럽게 선을 그을 수 있다. 이 선을 스스로 긋는 사람은 오히려 신뢰를 얻는다.
- **SSD와 컨슈머 기기의 차이를 인정한다.** SSD는 음향·기구·안테나 같은 아날로그/기구 의존 테스트가 거의 없다. 마이크·햅틱·RF 커플러 같은 항목은 처음이라는 점을 솔직히 말하고, 대신 "측정 시스템을 먼저 의심한다"는 방법론은 동일하다고 연결한다.
- **강점 항목이라고 길게 말하지 않는다.** 30초로 핵심을 말하고 꼬리질문을 유도하는 편이 낫다.

### 6.5 확인 필요 항목

- <확인 필요: Apple의 factory test-node가 "DUT 안에서 도는 펌웨어"였는지, "스테이션 쪽 아키텍처"였는지 — JD는 firmware를 요구하므로 DUT 쪽 코드를 직접 짰는지가 중요하다>
- <확인 필요: calibration 데이터(계수)를 기기에 저장하는 포맷·절차를 설계한 경험이 있는지 — 없다면 Q03·Q09는 지식 기반으로만 답해야 한다>
- <확인 필요: CM(위탁 생산업체) 테스트 엔지니어와 직접 소통한 경험이 있는지, 아니면 사내 제조였는지 — Hark는 CM을 쓸 가능성이 높다>
- <확인 필요: limit 설정이나 수율 분석에 직접 관여했는지, 아니면 품질/PE 팀 담당이었는지>
- <확인 필요: SSD 펌웨어에서 secure boot 또는 이미지 서명 검증 경험이 있는지 — context 3.1에 "있으면 추가"로 남아 있는 항목이며, 출하 전 잠금(Q08) 답변을 크게 강화한다>
- <확인 필요: "다시 한다면 바꾸고 싶은 것" 한 가지 (Q14 꼬리질문 대비)>

---

## 7. 준비 체크리스트

- [ ] 2.2절 제조 흐름도를 백지에 3분 안에 그린다 (SMT → ICT → 플래싱 → FCT → 조립 → FATP → 잠금 → 출하)
- [ ] 2.4절 cal 수명주기 8단계를 순서대로 말한다 (특히 "쓴 직후 재검증" 단계를 빠뜨리지 않는다)
- [ ] 2.5절 UPH 계산을 손으로 한 번 해 본다 (사이클 53s → 68 UPH → 필요 fixture 수)
- [ ] FPY / RTY / NTF / Cpk / Gauge R&R을 각각 한 문장으로 정의한다
- [ ] 3.2절 cal 레코드 읽기 상태표(6줄)를 외운다 — Q03·Q09의 차별화 포인트
- [ ] Q04(수율 하락) 답변의 좁히기 순서를 암기한다: raw 분포 → 층별(fixture/shift/lot) → NTF → 골든 유닛
- [ ] 6.3절 STAR 스토리 A를 영어로 90초 안에 말하는 연습 (기밀 선 긋는 문장 포함)
- [ ] 6.5절 확인 필요 항목 6개를 본인 경험에 비추어 답을 확정한다
- [ ] shmoo ↔ guard band 연결을 한 문장으로 정리한다 (SSD 경험을 컨슈머 양산 언어로 번역)
- [ ] 역질문 준비: "지금 CM은 정해졌나요? factory FW는 누가 소유하고 있나요? EVT는 언제인가요?" (context 4.7 2번과 연결)

---

## 8. 더 읽기

- **C09 §1** EVT/DVT/PVT/MP 단계별 정의와 단계마다 factory FW에 요구되는 변화 — Q10의 상세 버전
- **C09 §2** 테스트 모드 진입 방법(strap, 매직 명령, 별도 이미지) 비교 — Q01 꼬리질문 근거
- **C09 §3.2** factory 명령 줄 파서와 디스패치 테이블 전체 C 구현 — 이 노트 3.1절의 구현
- **C09 §3.3** Zephyr shell 서브시스템으로 만드는 방법
- **C09 §4** fixture 구성, 테스트 시간·UPH, 수율 통계 — 이 노트 2.5절의 상세 버전
- **C09 §5** 캘리브레이션 종류별 상세 (가속도계 6면, 마이크 감도, RF) — 이 노트 3.3절의 상세 버전
- **C09 §6** cal 데이터 저장 레이아웃과 버전·CRC·이중 사본·마이그레이션 전체 구현 — 이 노트 3.2절의 구현
- **C09 §7** provisioning(시리얼, 키, 기기 인증서) 흐름 — Q07 보강
- **C09 §8** 추적성과 MES — Q12 보강
- **C09 §9** 출하 전 잠금 — Q08 보강
- **C09 §10** Don의 Apple 경험 매핑표와 "첫 90일" 계획 — Q13·Q14 보강
- **C07** secure boot, root of trust, 디버그 포트 잠금 — Q08의 보안 배경
- **C10** 로직 분석기·스코프·JTAG, 보드 bring-up 체크리스트, 회로도 읽기 — Q11과 EVT 회로도 리뷰
- **S04 Q18~Q23** factory test·calibration·provisioning·EVT/DVT/PVT·테스트 시간 문항 — 이 노트 5절과 짝으로 풀 것
- **S06** 디버깅 시나리오와 Don STAR 스토리 매핑 — 6.3절 스토리를 여기서 다듬는다
- **J04** OTA — cal 영역을 업데이트에서 분리하는 파티션 설계, 필드 telemetry (Q09·Q12와 연결)
- **J05** on-device AI 예산 — factory 마이크 calibration이 곧 모델 정확도 관리라는 연결 (3.3절)
- **J07** 로직 분석기·스코프·JTAG로 HW-SW 상호작용 디버깅 (Q11과 연결)
