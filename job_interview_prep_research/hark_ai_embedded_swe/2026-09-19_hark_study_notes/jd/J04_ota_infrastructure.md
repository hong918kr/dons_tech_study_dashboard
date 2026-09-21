# J04. Build and maintain OTA update infrastructure for reliable field updates

> **분류**: Responsibility 4/7 · **관련 개념 노트**: C07(OTA·secure boot 교과서), C09(factory provisioning), S04(면접 드릴)
> **Don 현재 상태**: 🟡 부분 — 레쥬메에 OTA 명시는 없다. 다만 양산 SSD 펌웨어, telemetry·error reporting 설계, factory test-node 아키텍처가 있어 "필드에서 돌아가는 펌웨어를 운영하는 감각"은 증거가 있다. NVMe firmware download/commit 경험 여부는 확인이 필요하다.
> **이 노트를 다 읽으면**: ① 빌드에서 롤백까지 OTA 파이프라인 전 구간을 단계별 산출물·실패 모드와 함께 그릴 수 있다 ② SoC + MCU + 라디오칩이 섞인 기기의 멀티 이미지 업데이트를 설계할 수 있다 ③ "maintain"이 실제로 무슨 일인지(호환성 매트릭스, 키 수명, 인증서 만료, N-2 업그레이드 테스트) 말할 수 있다.

---

## 0. 문장 뜯어보기

| 구(句) | 표면적 의미 | 채용담당자가 이 단어를 고른 이유 |
|---|---|---|
| `Build` | 만든다 | 아직 없다는 뜻이다. 1세대 기기이고 파이프라인을 **0에서 세울 사람**을 찾는다. 기존 시스템에 기능 추가하는 자리가 아니다. |
| `and maintain` | 그리고 유지한다 | 만드는 것보다 이게 길다. 출하 후 몇 년간 "옛 버전에서 최신으로 올라오는 경로"를 계속 살려 둬야 한다. **운영 부담을 아는 사람**을 찾는 신호. |
| `OTA update infrastructure` | OTA 인프라 | "OTA 기능"이 아니라 "인프라". 부트로더뿐 아니라 **빌드·서명·매니페스트·서버·배포·telemetry까지** 포함한다는 뜻으로 읽는 게 맞다. |
| `for reliable` | 신뢰할 수 있는 | 벽돌(brick) 0이 목표다. 전원 차단·통신 끊김·잘못된 이미지·부분 기록 어디서도 복구 가능해야 한다. |
| `field updates` | 현장 업데이트 | 랩이 아니라 사용자 손 안. 되돌리려면 또 OTA밖에 없고, 실패는 RMA 비용이다. |

**한 문장 요약**: "사용자 손에 있는 기기의 펌웨어를 안전하게 바꾸는 **시스템 전체**를 처음부터 만들고, 몇 년간 굴려라."

이 문장이 무서운 이유: 다른 모든 책임(드라이버, 전력, AI)은 실수하면 고쳐서 다시 배포하면 된다. **OTA가 깨지면 고칠 방법 자체가 사라진다.** 그래서 이 영역은 "동작하게 만들기"보다 "실패해도 되돌아오기"가 설계의 전부다.

---

## 1. Hark에서 실제로 하게 될 일 (추정)

context 파일의 구조 추정(Qualcomm SoC + Android, always-on MCU, Cellular/Wi-Fi/BT/GNSS/NFC/UWB 멀티 라디오)과 Hark의 Embedded DevOps 공고에 A/B·delta update가 언급된다는 단서를 근거로 한다. 전부 `[추정]`이다.

### 1.1 입사 후 6개월의 모습

```
 0~1개월  현황 파악: 지금은 어떻게 기기에 이미지를 넣는가?
          (아마도 USB/SWD 수동 flash + 엔지니어가 직접 들고 다니는 단계)
          칩별 부트 체인 조사 -> SoC(Android/AVB?), MCU(부트로더 뭐 쓰나?), 라디오칩
 1~2개월  MCU 쪽 A/B + 서명 검증 부트로더 세우기 (MCUboot 채택 여부 결정)
          파티션 레이아웃을 flash 예산과 함께 확정 -> 나중에 못 바꾸는 결정이라 신중
 2~3개월  전송 경로: SoC -> MCU 이미지 전달 프로토콜, 재개(resume), 무결성 검증
          CI 에서 서명된 아티팩트가 나오게 하기 (키는 개발용과 양산용 분리)
 3~4개월  매니페스트/번들 정의 (칩별 이미지 + 버전 의존성), 서버/CDN 붙이기
          기기 등록·인증(factory provisioning 과 연결, C09)
 4~5개월  전원 차단 테스트 리그, N-2 업그레이드 매트릭스, 퍼징된 이미지 거부 테스트
 5~6개월  dogfood 코호트로 실제 배포 -> 지표 대시보드 -> 단계적 롤아웃 절차 문서화
          "킬 스위치"와 롤백 런북 작성 (새벽 3시에 누가 눌러야 하는가)
```

### 1.2 주간 업무의 모습 (운영 단계)

- **릴리스 열차 운영**: 주간 또는 격주 릴리스. 어떤 커밋이 들어갔고, 어떤 칩 이미지가 바뀌었고, 의존성이 맞는지 확인한 뒤 번들을 만든다.
- **롤아웃 감시**: 1% → 5% → 25% → 100% 각 단계에서 업데이트 성공률, revert율, 부팅 후 크래시, 배터리 지표(J03의 telemetry와 연결)를 본다. 나빠지면 멈춘다.
- **실패 분석**: "0.3%가 다운로드 단계에서 실패" → 어떤 하드웨어 리비전인가, 어떤 지역인가, flash 벤더인가. 로그 스키마를 미리 설계해 둬야 답이 나온다.
- **호환성 관리**: 새 SoC 이미지가 옛 MCU와 통신 가능한가. 프로토콜 버전 협상 테스트.
- **키·인증서 관리**: 서명 키 접근 권한, 순환 계획, TLS 인증서 만료일 추적. 이건 달력에 알람을 걸어 두는 종류의 일이다.

### 1.3 소유하게 될 산출물

| 산출물 | 내용 |
|---|---|
| 부트로더 구성 + 파티션 맵 | 슬롯 크기, scratch, 신뢰 앵커 위치. 한 번 출하하면 바꾸기 매우 어렵다 |
| 서명 파이프라인 | CI에서 자동 서명, 키는 HSM/KMS. 개발 키와 양산 키 분리 |
| 매니페스트 포맷 + 버전 정책 | 칩별 이미지, 해시, 의존성, 최소 버전 |
| 기기 측 업데이트 에이전트 | 다운로드·검증·전달·적용·confirm 상태 머신 |
| 배포 서버 연동 + 롤아웃 도구 | 코호트 정의, 킬 스위치 |
| 업데이트 telemetry 스키마 | 단계별 성공/실패 코드, reset reason, revert 사유 |
| 테스트 리그 | 전원 차단 반복, 업그레이드 매트릭스, 손상 이미지 거부 |
| 런북 | 롤백 절차, 에스컬레이션, 필드 벽돌 대응 |

---

## 2. 핵심 개념

### 2.1 파이프라인 전체 그림

```
 [1] 빌드          소스 + 툴체인 + 설정 → 이미지 (재현 가능해야 함)
        │            산출물: app.bin, mcu.bin, model.bin, 빌드 메타데이터
        ▼
 [2] 서명          CI 안에서, 키는 HSM/KMS. 개발키/양산키 분리
        │            산출물: 서명된 이미지 (헤더 + 페이로드 + TLV/서명)
        ▼
 [3] 매니페스트    칩별 이미지 목록 + 해시 + 버전 + 의존성, 매니페스트 자체도 서명
        │            산출물: bundle.json(+sig) 또는 SUIT 매니페스트
        ▼
 [4] 서버 / CDN    아티팩트 업로드, 코호트·적격성 규칙, 배포 계획
        │            hawkBit / Mender / 자체 서버 / 클라우드 IoT 서비스
        ▼
 [5] 기기 다운로드  TLS 로 서버 인증 → 매니페스트 확인 → 이미지 청크 다운로드(재개 가능)
        │            전송 경로: Wi-Fi / 셀룰러 / 폰 릴레이(BLE) 중 선택
        ▼
 [6] 검증          해시 + 서명 검증. "다운로드 성공" 과 "이미지가 진짜" 는 다른 문제
        │
        ▼
 [7] 적용          비활성 슬롯 기록 → 업그레이드 요청 플래그 → 재부팅
        │            부트로더가 검증 후 스왑/부팅 (전원 차단에 안전해야)
        ▼
 [8] 확인(confirm) 새 이미지가 스스로 self-test 통과 → confirm. 못 하면 자동 revert
        │
        ▼
 [9] 보고          결과를 서버로 (성공/실패 코드, 이전·현재 버전, reset reason)
        │
        ▼
 [10] 롤아웃 제어  지표 보고 다음 코호트로 확대, 나쁘면 중단(kill switch)
        │
        ▼
 [11] 롤백         새 버전 배포로 되돌리기(앞으로 굴러서 되돌린다) 또는 기기 자동 revert
```

**초보자가 놓치는 경계 두 개**:

1. **[5] 서버 신뢰와 [6] 이미지 신뢰는 별개다.** TLS는 "서버가 진짜인가"를, 서명은 "이미지가 진짜인가"를 보장한다. 서버가 털려도 서명 검증이 있으면 기기는 안 죽는다. 기기는 **서버를 믿지 않고 서명만 믿어야** 한다.
2. **[7] 적용과 [8] 확인은 별개다.** 부팅에 성공했다고 정상이 아니다. 부팅은 되는데 Wi-Fi가 안 붙어서 다시는 업데이트를 못 받는 이미지가 최악이다. 그래서 confirm 조건에 **"다음 업데이트를 받을 수 있는 상태인가"**를 반드시 넣는다.

### 2.2 단계별 실패 모드와 방어

| 단계 | 대표 실패 | 방어 |
|---|---|---|
| 빌드 | 재현 불가한 빌드, 잘못된 설정으로 나간 이미지 | 재현 가능 빌드, 아티팩트에 빌드 메타데이터 각인, 출하 설정 고정 |
| 서명 | 개발 키로 서명된 이미지가 양산에 나감 | 키 분리, 양산 기기는 양산 공개키만 신뢰, 서명은 CI에서만 |
| 매니페스트 | 칩 이미지 조합이 안 맞음(SoC는 새것, MCU는 옛것) | 의존성 필드 + 기기 측 검증, N/N-1 프로토콜 호환 유지 |
| 서버 | 잘못된 대상에 배포, 서버 침해 | 코호트 규칙, 기기 측 서명 검증, 안티 롤백 |
| 다운로드 | 연결 끊김, 저장 공간 부족, 요금제 소모 | 청크 다운로드 + 재개, 사전 공간 확인, Wi-Fi/충전 중 조건 |
| 검증 | 잘린 이미지가 통과 | 전체 해시 + 길이 검증, 서명 대상에 헤더 포함 |
| 적용 | 스왑 중 전원 차단 | 저널링된 스왑(MCUboot swap 방식), 재부팅 시 재개 |
| 확인 | 부팅은 되는데 통신 불가 | confirm 조건에 네트워크·서버 도달 포함, 워치독 기반 자동 revert |
| 보고 | 실패한 기기가 보고도 못 함 | 이전 버전에서 보고하도록 설계(revert 후 reset reason 보고) |
| 롤아웃 | 나쁜 빌드가 100%로 감 | 단계적 확대 + 지표 게이트 + 킬 스위치 |

### 2.3 저장소 레이아웃 — 가장 되돌리기 어려운 결정

C07 §2에 A/B, single-slot+recovery, MCUboot swap 방식이 자세히 있다. 업무 관점에서 중요한 건 **flash 예산 협상**이다.

```
 MCU 내부 flash 1 MB 예시 (가상)
 ┌──────────┬──────────────────────┬──────────────────────┬─────────┬──────┐
 │ boot     │ slot0 (primary)      │ slot1 (secondary)    │ scratch │ 설정 │
 │ 48 KB    │ 416 KB               │ 416 KB               │ 64 KB   │ 8 KB │
 └──────────┴──────────────────────┴──────────────────────┴─────────┴──────┘
   ↑ 여기에 모델까지 넣어야 한다면? 416 KB 안에 app + 모델이 다 들어가야 한다
```

**설계 시 묻는 질문 순서**:

1. A/B를 둘 다 내부 flash에 둘 수 있나? (못 두면 외부 QSPI flash를 secondary로 쓰거나, single-slot + recovery로 간다)
2. 모델 파일을 이미지 안에 넣을 것인가, 별도 파티션으로 뺄 것인가? (따로 빼면 모델만 업데이트 가능하지만 버전 조합 관리가 늘어난다)
3. swap 방식은? (scratch 사용 여부는 flash 수명과 시간에 영향)
4. 설정/캘리브레이션 파티션은 업데이트에서 **절대 건드리지 않는다**는 걸 어떻게 보장하나? (C09의 factory calibration이 여기 산다. 이걸 날리면 기기가 사실상 불량이 된다)
5. 부트로더 자체는 업데이트할 것인가? (하지 않는 게 기본. 해야 한다면 별도의 훨씬 보수적인 경로)

**황금률**: **부트로더는 유일하게 고칠 수 없는 코드다.** 작게, 단순하게, 기능 적게. 새 기능 아이디어는 애플리케이션으로 민다.

### 2.4 멀티칩 — 진짜 어려운 부분

Hark 기기는 최소 세 종류의 이미지가 있다고 추정할 수 있다.

| 대상 | 업데이트 방법 | 고유한 어려움 |
|---|---|---|
| Application SoC (Android) | A/B 파티션 + `update_engine`, Android Verified Boot(AVB) | 벤더 BSP에 묶임, rollback index 관리 |
| Always-on MCU | 부트로더 A/B 슬롯, SoC가 UART/SPI로 이미지 전달 | 업데이트 중 MCU가 담당하던 wake/센서 기능이 멈춘다 |
| 무선 콤보칩 | 호스트 로드형이면 SoC 파일시스템의 FW 파일 교체, 자체 flash형이면 벤더 도구 | 벤더 서명이라 OEM이 못 만드는 경우가 많다 |
| 모델 파일 | 별도 아티팩트 또는 MCU/SoC 이미지에 포함 | 모델-런타임 버전 호환성 |

**핵심 통찰**: 완벽하게 원자적인 멀티칩 업데이트는 사실상 불가능하다. 그러므로 **프로토콜 하위 호환성**이 진짜 해법이다.

```
 규칙 1. 칩 간 인터페이스에 버전 협상(handshake) 을 넣는다.
 규칙 2. 새 SoC 는 옛 MCU 와, 옛 SoC 는 새 MCU 와 최소한 "업데이트를 계속할 수 있을
         만큼" 은 통신 가능해야 한다 (N 과 N-1 호환).
 규칙 3. 그래서 인터페이스를 바꾸는 변경은 두 릴리스에 나눠 넣는다.
         릴리스 A: 새 필드를 "이해하되 요구하지 않음" 으로 추가
         릴리스 B: 실제로 사용
 규칙 4. 매니페스트에 의존성을 적고, 기기가 스스로 조합 가능 여부를 판단한다.
```

이게 서버-클라이언트 API 버저닝과 똑같은 문제라는 걸 알아차리는 게 포인트다. 다만 여기서는 **클라이언트를 되돌릴 수 없다.**

### 2.5 매니페스트 예시

번들 매니페스트는 "이 릴리스가 무엇으로 구성되는가"의 단일 진실이다. 자체 포맷을 쓸 수도 있고, IoT 표준인 IETF SUIT(아키텍처 RFC 9019, 정보 모델 RFC 9124)를 따를 수도 있다.

```json
{
  "manifest_version": 1,
  "release": "2026.09.3",
  "created": "2026-09-19T00:00:00Z",
  "targets": [
    {
      "component": "soc",
      "version": "3.2.1",
      "size": 268435456,
      "sha256": "…",
      "url": "https://cdn.example/soc-3.2.1.payload",
      "delta_from": ["3.2.0"],
      "requires": { "mcu": ">=1.7.0" }
    },
    {
      "component": "mcu",
      "version": "1.8.0",
      "size": 401408,
      "sha256": "…",
      "url": "https://cdn.example/mcu-1.8.0.bin",
      "requires": { "bootloader": ">=1.0.0" },
      "min_battery_pct": 30
    },
    {
      "component": "wakeword_model",
      "version": "7",
      "size": 61440,
      "sha256": "…",
      "requires": { "mcu": ">=1.8.0" }
    }
  ],
  "eligibility": { "hw_rev": ["DVT2", "PVT", "MP"], "from_version": ">=2026.08.1" },
  "rollout": { "cohort": "canary-1pct" }
}
```

- `requires`가 2.4의 규칙 4를 구현한다. 기기는 이 조건을 **스스로 검사**하고, 못 맞추면 거부한다.
- `eligibility.hw_rev`가 중요하다. EVT/DVT 샘플 보드와 MP 보드는 다른 이미지가 필요한 경우가 많고, 잘못 나가면 엔지니어링 보드가 벽돌이 된다.
- `min_battery_pct` 같은 조건을 매니페스트에 두면 정책을 기기 코드 수정 없이 바꿀 수 있다.
- 매니페스트 자체도 서명한다. 서명하지 않으면 "해시 목록"이 공격자의 도구가 된다.

### 2.6 기기 측 상태 머신

```
        ┌────────────┐
        │   IDLE     │◀───────────────────────────────┐
        └─────┬──────┘                                │
              │ 서버가 새 릴리스 알림 / 주기적 확인   │
              ▼                                       │
        ┌────────────┐  조건 불만족(배터리·저장공간·사용중) │
        │  ELIGIBLE  │──────────────────────────────▶ │
        └─────┬──────┘                                │
              │ 조건 OK (충전 중 & Wi-Fi & 유휴)      │
              ▼                                       │
        ┌────────────┐  실패/중단 → 재개 가능          │
        │ DOWNLOAD   │──┐                             │
        └─────┬──────┘  │ (청크 단위, 재부팅해도 재개) │
              │ 완료     └─────────────────────────────┤
              ▼                                       │
        ┌────────────┐  서명/해시 실패 → 폐기 + 보고   │
        │  VERIFY    │──────────────────────────────▶ │
        └─────┬──────┘                                │
              │ OK                                    │
              ▼                                       │
        ┌────────────┐                                │
        │  STAGED    │ (slot1 기록 완료, 아직 옛 FW 동작)│
        └─────┬──────┘                                │
              │ 사용자 유휴 시점 재부팅                │
              ▼                                       │
        ┌────────────┐  부트로더 검증 실패 → 옛 이미지로 │
        │  TESTING   │──────────────────────────────▶ │
        └─────┬──────┘                                │
              │ self-test 통과 (네트워크·센서·IPC 확인) │
              ▼                                       │
        ┌────────────┐                                │
        │ CONFIRMED  │──────────── 결과 보고 ──────────┘
        └────────────┘
        TESTING 에서 워치독/재부팅 반복 → 부트로더가 자동 revert
```

**`TESTING` 상태의 의미**: 새 이미지가 "시험 부팅" 중이고 아직 확정되지 않았다는 것. MCUboot 용어로는 test swap이고, confirm하지 않고 재부팅하면 자동으로 되돌아간다. 이 한 칸이 벽돌 방지의 핵심이다.

### 2.7 무엇을 "성공"이라 부를 것인가 — 지표 정의

인프라를 만든다는 건 **측정 가능하게 만든다**는 뜻이다. 아래가 최소 세트다.

| 지표 | 정의 | 왜 중요한가 |
|---|---|---|
| Update success rate | 적격 기기 중 confirm까지 간 비율 | 파이프라인 전체 건강도 |
| 단계별 이탈률 | 다운로드/검증/적용/confirm 각각의 실패율 | 어디가 깨졌는지 즉시 지목 |
| Revert rate | 자동 되돌림 비율 | 새 이미지 품질의 가장 빠른 신호 |
| Time to 90% | 릴리스 후 90% 기기가 올라오는 데 걸린 일수 | 보안 패치 배포 속도 = 리스크 노출 기간 |
| Stuck-version 기기 수 | 오래된 버전에 멈춰 있는 기기 | "maintain"의 본질. 방치하면 업그레이드 경로가 끊긴다 |
| Bricks per million | 복구 불가 기기 수 | RMA 비용. 0이 목표 |
| 업데이트 후 회귀 | 크래시율, 배터리(J03), 무선 연결 실패율 | 롤아웃 게이트 |

---

## 3. 실무 패턴과 함정

### 3.1 빌드와 서명 — CI 밖에서 서명하지 않는다

```sh
# MCUboot imgtool 로 MCU 이미지 서명 (CI 안에서 실행)
# 실제 옵션 이름/기본값은 MCUboot 버전마다 다르므로 --help 로 확인할 것
imgtool sign \
    --key /secrets/prod-ec-p256.pem \
    --header-size 0x200 \
    --align 4 \
    --version 1.8.0 \
    --slot-size 0x68000 \
    --pad-header \
    mcu-app.bin mcu-app-signed.bin

# 검증 (같은 키의 공개부로)
imgtool verify mcu-app-signed.bin
```

**원칙**:

- **개발 키와 양산 키를 분리한다.** 개발 보드는 개발 공개키를, 양산 기기는 양산 공개키만 신뢰한다. 이 분리를 안 하면 유출된 개발 키로 아무 이미지나 실행된다. MCUboot 기본 저장소에 들어 있는 예제 키는 **공개되어 있으므로 절대 출하에 쓰지 않는다**(이건 실제로 일어나는 사고다).
- **양산 서명 키는 HSM 또는 클라우드 KMS에 두고 CI가 호출만 한다.** 파일로 존재하면 언젠가 노트북에 복사된다.
- **누가 무엇을 서명했는지 로그를 남긴다.** 릴리스마다 서명 요청자·커밋 해시·산출물 해시를 기록.
- **재현 가능한 빌드**를 지향한다. 같은 커밋에서 같은 바이너리가 나와야 "이 기기의 이미지가 이 소스"임을 증명할 수 있다. 타임스탬프·절대 경로·빌드 번호가 방해하므로 의도적으로 제거하거나 별도 메타데이터로 뺀다.

### 3.2 전송 경로 선택 — 이게 제품 결정이다

| 경로 | 장점 | 단점 | 언제 쓰나 |
|---|---|---|---|
| Wi-Fi 직접 | 빠르고 공짜 | 사용자가 Wi-Fi를 안 붙였으면 영원히 못 받음 | 기본 경로 |
| 셀룰러 | 어디서나 | 데이터 요금, 느림, 전력 | 소형 패치, 보안 긴급 |
| 폰 릴레이 (BLE) | 기기가 인터넷 없어도 됨 | BLE 처리량이 낮아 MB급 이미지는 수십 분 | 폰 앱이 필수인 제품, 소형 이미지 |
| USB / 독 | 빠르고 안정적 | 사용자가 해야 함 | 최후 복구 수단 |

BLE로 큰 이미지를 넣는 건 현실적으로 고통스럽다. MTU와 DLE를 키우고 connection interval을 줄여도(C06 참고) 수백 kbps가 한계라 400 KB MCU 이미지도 수 분이 걸린다. 그래서 **MCU 이미지는 SoC가 받아서 전달하는 구조**가 일반적이다.

**델타(차분) 업데이트**를 쓰면 전송량이 크게 줄지만 대가가 있다: 기기의 "현재 정확한 바이트"를 서버가 알아야 하고(그래서 버전별 델타를 모두 만들어야 하고), 패치 적용 중 전원이 나가면 원본도 손상된다. 그래서 델타는 **비활성 슬롯에 재구성 후 전체 해시 검증**하는 방식으로만 쓴다. C07 §7.3 참고.

### 3.3 기기 측 적용 코드 (Zephyr + MCUboot)

```c
#include <zephyr/kernel.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

/* 1) 다운로드+기록이 끝난 뒤: "다음 부팅은 시험 부팅" 으로 표시하고 재부팅 */
int ota_stage_and_reboot(void)
{
    int rc = boot_request_upgrade(BOOT_UPGRADE_TEST);   /* permanent 가 아니라 TEST */
    if (rc != 0) {
        return rc;                                      /* trailer 기록 실패 → 재시도 */
    }
    sys_reboot(SYS_REBOOT_COLD);
    return 0;                                           /* 도달하지 않음 */
}

/* 2) 새 이미지가 부팅된 뒤: self-test 를 통과해야만 확정한다 */
int ota_confirm_if_healthy(void)
{
    if (boot_is_img_confirmed()) {
        return 0;                                       /* 이미 확정된 정상 부팅 */
    }

    /* 여기가 정책의 핵심: 무엇을 "정상" 으로 볼 것인가 */
    if (!self_test_sensors_ok())      { return -EIO;  }
    if (!self_test_ipc_with_soc_ok()) { return -EIO;  }
    if (!self_test_can_reach_update_server()) { return -ENETDOWN; }

    return boot_write_img_confirmed();                  /* 실패하면 다음 부팅에 revert */
}
```

- `boot_request_upgrade(BOOT_UPGRADE_TEST)`와 `boot_write_img_confirmed()`, `boot_is_img_confirmed()`는 Zephyr의 `zephyr/dfu/mcuboot.h`가 제공하는 실제 API다. `BOOT_UPGRADE_PERMANENT`를 쓰면 시험 부팅 없이 바로 확정되므로 **필드 업데이트에서는 쓰지 않는다.**
- confirm 조건에 **"업데이트 서버에 도달 가능한가"**를 넣은 게 핵심이다. 이게 없으면 "부팅은 되는데 네트워크가 죽은" 이미지가 확정되어 영구 고착된다.
- confirm 하지 않은 채로 워치독 리셋이 반복되면 부트로더가 이전 이미지로 되돌린다. 그래서 **워치독은 confirm 전까지 반드시 살아 있어야 한다.**
- 전송 프로토콜로 Zephyr에서는 SMP/mcumgr를 쓸 수 있다(`smp_svr` 샘플). SMP는 시리얼·BLE·UDP 등 여러 전송 위에서 동작하고, 이미지 목록·업로드·테스트·확정 명령을 제공한다. CLI 도구 이름과 권장 클라이언트는 **Zephyr 버전마다 다르니** 문서를 확인한다.

### 3.4 멀티칩 순서와 워치독

```
 시나리오: SoC v3.2.1 + MCU v1.8.0 을 함께 올린다

 1. SoC 가 번들 매니페스트 검증 (서명 + 의존성 + hw_rev 적격성)
 2. 두 이미지 모두 다운로드 완료될 때까지 아무것도 적용하지 않는다
    → 한쪽만 올라가는 중간 상태를 최소화
 3. MCU 이미지를 MCU slot1 으로 전송 (MCU 는 여전히 v1.7 로 동작 중)
 4. SoC 비활성 파티션에 SoC 이미지 기록
 5. 사용자 유휴 + 충전 중 시점에:
      a. MCU 재부팅 (TEST) → MCU self-test → SoC 와 IPC 확인 → MCU confirm
      b. 실패 시 MCU 자동 revert → 전체 업데이트 중단 + 보고 (SoC 는 아직 안 바뀜)
 6. SoC 슬롯 전환 후 재부팅 → SoC self-test (새 MCU 와 통신 확인 포함) → 확정
 7. 어느 단계든 실패하면 가능한 한 되돌리고 서버에 단계별 코드로 보고
```

**함정**: 5-a에서 MCU가 재부팅되는 동안 MCU가 담당하는 기능(전원 시퀀싱, wake, 버튼)이 멈춘다. MCU가 SoC의 전원을 제어하는 구조라면 **MCU 재부팅이 SoC 전원을 끊지 않도록** 하드웨어/부트로더 설계가 되어 있어야 한다. 이건 보드 설계 단계에서 확인해야 하는 사항이고, 나중에 발견하면 리비전이 필요하다.

### 3.5 테스트 — OTA는 테스트가 곧 제품이다

| 테스트 | 방법 | 무엇을 잡나 |
|---|---|---|
| 전원 차단 반복 | 릴레이로 업데이트 중 무작위 시점에 전원 차단, 수백~수천 회 | 스왑 중단 복구, trailer 손상 |
| 통신 절단 | 다운로드 중 AP 끊기/재연결 | 재개 로직, 부분 파일 처리 |
| 손상 이미지 거부 | 바이트 1개 뒤집기, 잘린 파일, 서명 없는 이미지, 다른 키로 서명 | 검증 경로 |
| 안티 롤백 | 낮은 버전 이미지를 밀어넣기 | 다운그레이드 공격 방어 |
| 업그레이드 매트릭스 | 출하된 모든 버전 → 최신 (N-1, N-2, … 공장 초기 버전 포함) | "오래된 기기가 못 올라오는" 최악의 사고 |
| 하드웨어 리비전 교차 | EVT/DVT/PVT/MP 보드에서 동일 번들 | 적격성 규칙 오류 |
| 저장/전력 경계 | 저장 공간 부족, 배터리 15%, 온도 높음 | 조건 검사 로직 |
| 반복 업데이트 | 같은 기기를 수백 번 업데이트 | flash 마모, 파티션 누수, 카운터 오버플로 |

**업그레이드 매트릭스가 "maintain"의 정체다.** 출하 1년 뒤 서랍에서 꺼낸 기기는 공장 출하 버전이다. 그 버전에서 최신까지 **한 번에 또는 정해진 단계로** 올 수 있어야 한다. 이걸 보장하려면 옛 버전들의 테스트 이미지를 계속 보관하고 CI에서 정기적으로 돌려야 한다. 중간에 파티션 레이아웃이나 매니페스트 포맷을 바꿨다면 **징검다리 릴리스(stepping stone)**를 영구히 유지해야 한다.

### 3.6 기기 전체를 벽돌로 만드는 방법들 (그리고 예방)

| 사고 | 어떻게 일어나나 | 예방 |
|---|---|---|
| 부트로더 업데이트 실패 | 부트로더 자체를 OTA로 갈다가 전원 차단 | 원칙적으로 안 한다. 해야 하면 이중 부트로더 + 최소 변경 |
| 키 분실 | 서명 키를 잃어버려 새 이미지를 못 만듦 | 키 백업·에스크로, 복수 신뢰 앵커 |
| TLS 인증서 만료 | 서버 인증서/루트 CA가 만료되어 기기가 서버에 못 붙음 | 만료일 추적, 루트 CA 갱신 경로를 OTA에 포함, 기기 시계 문제 고려 |
| 기기 시계 오류 | RTC가 1970년이라 인증서 검증 실패 | 시간 없이도 업데이트 가능한 경로 유지(서명은 시간 무관) |
| 잘못된 적격성 규칙 | 개발 보드용 이미지가 MP 기기로 배포 | hw_rev 적격성 + 기기 측 재확인 |
| 안티 롤백 과도 적용 | 롤백 카운터를 올려서 되돌릴 수 없게 됨 | 카운터 증가는 충분히 검증된 뒤(예: 코호트 100% 후 며칠) |
| 설정 파티션 삭제 | 업데이트가 캘리브레이션 영역을 지움 | 파티션 경계 검증, 업데이트 대상 화이트리스트 |
| "고착 버전" 방치 | 옛 버전이 최신으로 가는 경로가 없어짐 | 업그레이드 매트릭스 CI, 징검다리 릴리스 유지 |

특히 **인증서 만료는 실제로 대규모 기기를 죽인 사례가 여러 번 있는 문제**다. 달력에 알람을 걸고, "루트 신뢰를 갱신하는 업데이트"를 만료 훨씬 전에 배포하는 절차를 문서로 남긴다.

### 3.7 단계적 롤아웃과 킬 스위치

```
 day 0   내부 dogfood (직원 기기 수십 대)   ← 여기서 대부분의 사고를 잡는다
 day 3   1%   무작위 + hw_rev/지역 분산
 day 5   5%
 day 8   25%
 day 12  100%

 각 단계 게이트(모두 통과해야 확대):
   update success rate ≥ 98%      revert rate ≤ 0.5%
   부팅 후 크래시율 증가 ≤ 10%    배터리 지표 악화 없음(J03)
   무선 연결 실패율 증가 없음
 게이트 실패 → 즉시 중단(킬 스위치) → 원인 분석 → 수정 버전으로 "앞으로 굴러서" 복구
```

- **롤백은 보통 "이전 버전 재배포"다.** 안티 롤백 때문에 진짜 다운그레이드는 막혀 있는 경우가 많고, 버전 번호를 올린 수정 릴리스를 내는 게 안전하다.
- **킬 스위치는 서버 쪽 스위치**여야 한다. 기기 코드를 고쳐야 멈출 수 있다면 이미 늦다.
- **코호트는 무작위여야 하되 하드웨어 리비전·지역·통신사는 분산**시킨다. 특정 flash 벤더나 특정 AP에서만 나는 문제가 실제로 있다.
- dogfood 단계에서 잡히는 사고의 비율이 압도적으로 높다. 직원 기기에 먼저 배포하는 문화를 만드는 것 자체가 인프라의 일부다.

---

## 4. 리서치 — 근거 자료

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| MCUboot 문서 | 부트로더 설계, 이미지 포맷, swap 방식, trailer | design, imgtool 페이지 | https://docs.mcuboot.com/ |
| MCUboot 저장소 | 실제 구현과 릴리스 노트 | `docs/design.md`, `docs/release-notes.md` | https://github.com/mcu-tools/mcuboot |
| MCUboot imgtool | 이미지 서명·검증 CLI | 옵션 목록 — **버전마다 다름** | https://docs.mcuboot.com/imgtool.html |
| Zephyr DFU / MCUboot API | `boot_request_upgrade`, `boot_write_img_confirmed` 등 | Device Management → DFU | https://docs.zephyrproject.org/latest/services/device_mgmt/dfu.html |
| Zephyr MCUmgr | 기기 관리 서브시스템(이미지 업로드/확정) | 전송(BLE/시리얼/UDP)별 설정 | https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html |
| Zephyr SMP 프로토콜 | 관리 명령의 와이어 포맷 | 이미지 관리 그룹 명령 | https://docs.zephyrproject.org/latest/services/device_mgmt/smp_protocol.html |
| Zephyr smp_svr 샘플 | 실제로 동작하는 DFU 기기 예제 | 빌드·업로드 절차 | https://docs.zephyrproject.org/latest/samples/subsys/mgmt/mcumgr/smp_svr/README.html |
| Nordic Device Manager (Android) | SMP 클라이언트 구현 참고 | 라이브러리 사용 예 | https://github.com/NordicSemiconductor/Android-nRF-Connect-Device-Manager |
| Android A/B (Seamless) Updates | SoC 쪽 A/B, `update_engine`, payload 포맷 | 슬롯·마킹·부팅 흐름 | https://source.android.com/docs/core/ota/ab |
| Android Verified Boot | 서명·rollback index·상태 | AVB 개념 | https://source.android.com/docs/security/features/verifiedboot |
| Eclipse hawkBit | 오픈소스 배포 서버(롤아웃, 코호트) | 롤아웃 관리 개념 | https://eclipse.dev/hawkbit/ |
| Mender | 임베디드 Linux OTA 상용/오픈소스 | A/B, 상태 스크립트, 서버 구조 | https://docs.mender.io/ |
| RAUC | 임베디드 Linux 업데이트 프레임워크 | 번들·슬롯 개념 | https://rauc.readthedocs.io/ |
| SWUpdate | 임베디드 Linux 업데이트 도구 | sw-description 포맷 | https://sbabic.github.io/swupdate/ |
| IETF RFC 9019 | SUIT 아키텍처(IoT 업데이트 표준) | 위협 모델과 역할 분리 | https://www.rfc-editor.org/rfc/rfc9019 |
| IETF RFC 9124 | SUIT 정보 모델 | 매니페스트가 담아야 할 정보 목록 | https://www.rfc-editor.org/rfc/rfc9124 |
| The Update Framework (TUF) | 저장소 침해를 견디는 키/역할 설계 | 역할 분리와 키 순환 | https://theupdateframework.io/ |
| Uptane | 자동차용 TUF 확장(멀티 ECU) | 멀티칩 업데이트 위협 모델 | https://uptane.org/ |
| AWS IoT Jobs | 클라우드 배포 작업 모델 | 작업 상태·재시도 설계 참고 | https://docs.aws.amazon.com/iot/latest/developerguide/iot-jobs.html |
| Azure Device Update for IoT Hub | 클라우드 배포 서비스 | 그룹·배포 개념 | https://learn.microsoft.com/azure/iot-hub-device-update/ |
| Memfault 문서 | 기기 관측성 + OTA 배포 지표 | 업데이트 성공률·리부트 지표 설계 | https://docs.memfault.com/ |

**버전 의존 주의**:

- MCUboot의 swap 방식(`swap-using-scratch`, `swap-using-move`, `overwrite-only`, direct-XIP 등)과 기본값은 **버전·Kconfig 설정마다 다르다.** 면접에서 특정 기본값을 단정하지 말고 "설정에 따라 다르다"고 말하는 게 정확하다.
- Zephyr의 mcumgr CLI 클라이언트는 시기마다 권장 도구가 바뀌었다. 도구 이름을 단정하지 말고 문서를 확인한다.
- `imgtool` 옵션(`--slot-size` 대 `--pad`, 기본 헤더 크기 등)도 버전마다 다르다.
- Android `update_engine`의 payload 포맷은 AOSP 버전에 묶여 있다.

---

## 5. 예상 면접 질문

### Q01. Design an OTA system for a device that must never brick.

**왜 묻나**: 이 JD 문장의 정중앙. 시스템 사고를 본다.

**30초 답변**: 세 가지가 핵심이다. 첫째, 옛 이미지는 새 이미지가 확정될 때까지 그대로 둔다(A/B 슬롯 + 시험 부팅). 둘째, 부트로더가 모든 이미지를 서명 검증하고, 검증 실패나 confirm 실패 시 자동으로 되돌린다. 셋째, 스왑 과정이 저널링되어 전원이 어느 시점에 나가도 재부팅하면 재개된다. 여기에 confirm 조건으로 "다음 업데이트를 받을 수 있는 상태"를 넣는 게 실무 포인트다.

**English answer**: The rule is that the working image is never destroyed until the new one has proven itself. So: dual slots, the bootloader verifies a signature before it ever jumps, and the new image boots in a test state that reverts automatically unless the application confirms it. The swap itself has to be journaled in a trailer so that a power cut at any point just resumes on the next boot rather than leaving a half-written image. The part people forget is the definition of "healthy": confirming only because the thing booted is how you ship an image that comes up but cannot reach the update server ever again, so my self-test always includes connectivity and the inter-processor link. And the bootloader stays small and is not itself updatable, because it is the one piece I cannot fix later.

**꼬리질문**: "What if you don't have flash for two slots?" → single-slot + 별도 recovery 이미지로 간다. 다운로드를 외부 flash나 호스트에 두고 recovery가 적용한다. / "How do you test 'never bricks'?" → 릴레이로 전원 차단을 수백 회 반복하는 리그. / "Who confirms?" → 애플리케이션이 self-test 후. 부트로더는 판단하지 않는다.

### Q02. Walk me through the full pipeline, from a commit to a device in a user's pocket.

**왜 묻나**: "infrastructure"라는 단어를 부트로더로만 이해하는 후보를 거른다.

**30초 답변**: 빌드 → CI 안 서명(HSM) → 칩별 이미지를 묶은 서명된 매니페스트 → 서버/CDN 업로드 → 코호트 규칙 → 기기 다운로드(재개 가능) → 해시·서명 검증 → 비활성 슬롯 기록 → 시험 부팅 → self-test → confirm → 결과 telemetry → 지표 게이트를 통과하면 다음 코호트로 확대. 어느 단계든 실패하면 되돌리고 보고한다.

**English answer**: A commit produces a reproducible build with metadata baked in. CI signs it, always inside CI, with a production key held in an HSM, never on anyone's laptop. The per-chip images go into a signed manifest that carries versions, hashes and dependency constraints. That bundle is uploaded to the server and CDN with eligibility rules for hardware revision and source version. On the device, the agent checks eligibility and local conditions, downloads in resumable chunks, verifies hash and signature independently of TLS, writes to the inactive slot, and reboots into a test state. After self-test it confirms and reports a per-stage result code. The server watches success rate, revert rate and post-update crash and battery metrics, and only then widens the cohort. Every one of those stages has its own failure code, because "the update failed" is not actionable.

**꼬리질문**: "Why sign in CI rather than on the server?" → 서명 대상이 정확히 그 빌드 산출물임을 보장하기 위해. 그리고 서버 침해 시 방어선이 된다. / "Why does the device verify signatures if TLS is used?" → TLS는 서버를 인증하지, 이미지를 인증하지 않는다.

### Q03. What does "maintain" mean here? What work continues after launch?

**왜 묻나**: JD가 "build **and maintain**"이라고 썼다. 운영 경험을 본다.

**30초 답변**: 출하된 모든 버전에서 최신으로 올라오는 경로를 계속 살려 둬야 한다. 그래서 업그레이드 매트릭스를 CI에서 정기적으로 돌리고, 포맷이 바뀌면 징검다리 릴리스를 영구 유지한다. 그리고 키 수명, TLS 인증서 만료, 고착된 기기 추적, 롤아웃 절차와 런북 갱신이 계속되는 일이다.

**English answer**: Maintenance is mostly about paths, not features. A device that sat in a drawer for a year is on the factory image, and it has to be able to reach the newest release, so I keep old build artifacts and run an upgrade matrix in CI — factory version to latest, N-1, N-2 — on real hardware. If I ever change the partition layout or the manifest format, that stepping-stone release lives forever. Then there is the boring but fatal category: signing key lifetime, root certificate expiry, device clock problems, and the population of devices stuck on an old version that nobody is watching. I would put certificate expiry on a calendar with months of margin, because an expired root is a way to lose a fleet without a single code bug.

**꼬리질문**: "How long do you keep old versions working?" → 제품 지원 기간 전체. 제품 팀과 명시적으로 합의해야 한다. / "What is a stepping stone release?" → 옛 포맷을 읽고 새 포맷으로 넘어가는 중간 버전. 그것만은 절대 삭제하지 않는다.

### Q04. Power is cut in the middle of the update. What happens at each step?

**왜 묻나**: C07 §8의 고전. 저널링과 멱등성 개념.

**30초 답변**: 다운로드 중이면 부분 파일만 남고 재개하면 된다. slot1 기록 중이면 검증에서 걸러지고 다시 받는다. 스왑 중이면 trailer의 진행 상태를 보고 부트로더가 이어서 한다. 시험 부팅 중이면 confirm이 안 됐으므로 다음 부팅에 자동 revert. 어느 경우에도 옛 이미지는 살아 있거나 복구 가능한 상태다.

**English answer**: I walk it by stage. During download, only a partial file exists and nothing has been committed, so it resumes or restarts. During the write to the secondary slot, the image is incomplete, but nothing points at it and verification will reject it. During the swap itself, the trailer records how far the operation got, so the bootloader resumes it on the next boot rather than starting over — that is why the swap has to be idempotent at page granularity. During the test boot, the image is simply not confirmed, so the next boot reverts. The failure mode I actually worry about is not power loss, it is a flash write that reports success but did not land, which is why I verify by reading back and hashing rather than trusting the write.

**꼬리질문**: "How many power-cut cycles do you test?" → 무작위 시점으로 수백~수천 회. 자동화 필수. / "What about the battery being flat?" → 배터리 하한 조건을 걸고, 충전 중에만 적용 단계를 진행한다.

### Q05. Three chips, three images. How do you keep them compatible?

**왜 묻나**: Hark 기기 구조 추정과 직결. 가장 현실적인 난제.

**30초 답변**: 완전한 원자성은 포기하고 **하위 호환**으로 푼다. 칩 간 인터페이스에 버전 협상을 넣고, N과 N-1이 최소한 업데이트를 계속할 수 있을 만큼은 통신되게 한다. 인터페이스 변경은 두 릴리스로 쪼갠다(먼저 받아들이기, 다음에 사용하기). 매니페스트에 의존성을 적고 기기가 조합 가능 여부를 스스로 검사한다.

**English answer**: I stop trying to make it atomic, because across a reboot of two processors it cannot be. Instead I make the interfaces tolerant. Each inter-processor link starts with a version handshake, and the rule is that the new SoC firmware must still talk to the previous MCU firmware well enough to finish an update, and vice versa. Interface changes ship in two releases: first a version that accepts the new field but does not require it, then a version that uses it. The manifest carries explicit dependency constraints and the device checks them itself before applying anything. I also sequence deliberately — stage everything first, update the less risky processor first, verify the link, and only then move the second one — so a failure leaves me in a combination I have actually tested.

**꼬리질문**: "Which chip do you update first?" → 되돌리기 쉬운 쪽, 그리고 전원·wake를 담당하지 않는 쪽. 대개 MCU를 먼저 시험하고 실패 시 SoC는 손대지 않는다. / "What about a radio chip with vendor-signed firmware?" → OEM이 서명 못 하므로 벤더 번들을 그대로 싣고 버전만 관리한다.

### Q06. How do you roll back a bad release?

**왜 묻나**: 롤백이 단순 다운그레이드가 아니라는 걸 아는지.

**30초 답변**: 우선 서버에서 롤아웃을 즉시 중단한다(킬 스위치). 이미 받은 기기는 대개 버전 번호를 올린 수정 릴리스로 "앞으로 굴러서" 되돌린다. 안티 롤백 카운터 때문에 진짜 다운그레이드는 막혀 있는 경우가 많기 때문이다. 자동 revert는 부팅 불가·confirm 실패에만 작동하므로, "부팅은 되는데 나쁜" 릴리스는 사람이 판단해야 한다.

**English answer**: Three different mechanisms, and people confuse them. The automatic revert in the bootloader only covers images that fail to boot or fail to confirm. A bad-but-bootable release is not covered, so the first action is the server-side kill switch that stops the rollout for everyone who has not taken it yet. For devices that already took it, I normally roll forward: cut a fixed release with a higher version, because anti-rollback counters often make a true downgrade impossible by design. That is also why I do not advance the anti-rollback counter at the moment of release — I wait until a version has been at full rollout for some days, so I keep the option to go back.

**꼬리질문**: "What if the bad release breaks the update client itself?" → 그래서 업데이트 클라이언트는 self-test 대상이고, 최소한의 복구 경로(recovery 이미지, USB 복구)를 남긴다. / "Who pushes the kill switch at 3 a.m.?" → 런북에 정의된 온콜. 이것도 인프라의 일부다.

### Q07. Why does the device verify the signature if the download used TLS?

**왜 묻나**: 보안 기본기. 우대 사항(secure boot, firmware signing)과 직결.

**30초 답변**: 관심사가 다르다. TLS는 전송 구간에서 서버 신원과 기밀성을 보장할 뿐, 서버에 올라간 이미지가 정당한지는 말해 주지 않는다. 서버나 빌드 시스템이 침해되면 TLS는 그대로 통과한다. 서명은 "이 이미지를 우리 키로 승인했다"를 기기가 독립적으로 확인하는 수단이고, 부트로더가 매 부팅마다 다시 확인한다.

**English answer**: They protect different things. TLS authenticates the server and the channel for the duration of the download, and then it is gone. The signature authenticates the artifact itself, and the bootloader re-checks it on every boot, long after the transfer. If the distribution server or the CDN is compromised, TLS is perfectly happy to deliver malware; the signature is what stops it. That is also why I sign inside CI with a key the server never sees, and why the device stores a hash of the public key in one-time-programmable memory rather than storing the key in updatable flash.

**꼬리질문**: "Why store the hash instead of the key?" → OTP 공간이 작고, 키를 교체할 여지를 남기기 위해. C07 §5.2. / "Signing versus encryption?" → 서명은 무결성·출처, 암호화는 기밀성. 벽돌 방지에는 서명이 필수, 암호화는 IP 보호 목적.

### Q08. What metrics tell you an OTA system is healthy?

**왜 묻나**: 인프라를 운영해 본 사람인지. Don의 telemetry 경험과 연결되는 질문.

**30초 답변**: 업데이트 성공률, 단계별 이탈률, revert율, 90% 도달 시간, 고착 버전 기기 수, 백만 대당 벽돌 수. 그리고 업데이트 후 회귀 지표(크래시율, 배터리, 무선 연결 실패율)를 롤아웃 게이트로 쓴다.

**English answer**: Success rate alone is not enough, because it hides where things break. I instrument each stage separately — eligible, downloaded, verified, staged, booted, confirmed — so a drop points at a component instead of a mood. Revert rate is my fastest quality signal. Time to ninety percent tells me how long a security fix takes to actually land, which is a risk number, not a vanity number. And I track devices stuck on old versions, because that population silently grows and one day they cannot upgrade at all. Post-update regressions — crash rate, battery drain, radio reconnects — are the gate for widening a rollout.

**꼬리질문**: "How does a device report a failure it cannot boot from?" → 이전 이미지로 revert된 뒤 reset reason과 실패 단계 코드를 보고한다. 그래서 그 기록은 두 이미지가 공유하는 영역에 둬야 한다. / "Privacy?" → 집계 코드와 버전만. 사용자 데이터는 올리지 않는다.

### Q09. How would you get a 400 KB image onto an MCU that only talks BLE?

**왜 묻나**: 전송 경로 현실 감각. C06과 연결.

**30초 답변**: 직접 BLE로 넣으면 수 분이 걸린다. MTU와 DLE를 최대로, connection interval을 짧게 해서 처리량을 올리고, 청크 단위로 재개 가능하게 만든다. 하지만 현실적인 답은 **SoC가 Wi-Fi/셀룰러로 받아서 UART/SPI로 MCU에 전달**하는 구조다. BLE 직접 경로는 SoC가 죽었을 때의 복구 수단으로 남긴다.

**English answer**: I would first ask whether it has to be BLE. If the device has an application SoC with Wi-Fi, the sane architecture is that the SoC downloads the bundle and pushes the microcontroller image over a wired link at megabits, and BLE from a phone stays as the recovery path for when the SoC side is broken. If BLE really is the only path, I maximise throughput by negotiating the largest MTU and enabling data length extension, using a short connection interval during the transfer and restoring it afterwards for power, and I make the transfer chunked and resumable so a walk out of range does not cost the whole transfer. Zephyr's SMP over BLE gives that, and the phone side has a reference implementation. I would also make sure the transfer is gated on charging, because that connection interval is expensive.

**꼬리질문**: "How long would it take?" → 실효 처리량에 따라 수 분 규모. 구체 수치는 스택·폰·환경에 따라 크게 다르다. / "Delta update?" → 전송량은 줄지만 버전별 패치 생성과 검증 복잡도가 늘어난다.

### Q10. A device fails the update at the download stage, 0.3% of the fleet. How do you investigate?

**왜 묻나**: 필드 디버깅 절차. 로그 스키마 설계의 중요성.

**30초 답변**: 실패 코드를 세분화해 뒀다면 바로 분포를 본다. 하드웨어 리비전, 지역/통신사, AP 종류, 저장 공간, 배터리 조건별로 쪼개서 편향을 찾는다. 편향이 없으면 타임아웃·재시도 정책 문제일 가능성이 높다. 필요하면 소수 코호트에 진단 빌드를 배포한다.

**English answer**: Zero point three percent is a real number on a consumer fleet, so I treat it as a bug, not noise. First I slice the failure codes — connection reset, checksum mismatch, out of storage, timeout — because a single "download failed" bucket makes this impossible. Then I slice the population by hardware revision, region and carrier, storage headroom and battery state, looking for a bias. If one flash vendor or one carrier dominates, that is my answer. If it is perfectly uniform, it is more likely my retry and timeout policy being too aggressive on slow links. When the existing telemetry cannot separate the hypotheses, I ship a diagnostic build to a small cohort, which is exactly the capability the OTA system exists to provide.

**꼬리질문**: "What if it only happens on cellular?" → 요금제·MTU·NAT 타임아웃·전력 상태 전환 문제를 본다. / "How do you avoid blaming the user's Wi-Fi?" → 성공한 기기의 분포와 비교해 상대 위험을 본다.

### Q11. How do you manage signing keys?

**왜 묻나**: 우대 사항(firmware signing, hardware root of trust). 절차 감각을 본다.

**30초 답변**: 기술보다 절차다. 개발 키와 양산 키를 분리하고, 양산 키는 HSM/KMS에 두고 CI만 호출한다. 접근 권한은 최소 인원, 모든 서명은 감사 로그로 남긴다. 키 유출에 대비해 복수 신뢰 앵커와 순환 계획을 미리 세워 둔다. OTP에는 공개키가 아니라 공개키 해시를 넣어 교체 여지를 남긴다.

**English answer**: The hard parts are organisational. Development and production keys are different, and a production device trusts only the production public key, so a leaked development key cannot run anything on a shipped unit. The production private key lives in an HSM or a cloud KMS; CI calls a signing service and never holds the key material, because a key in a file eventually ends up on a laptop. Every signature is logged with the requester, the commit and the artifact hash. For the worst case I want a rotation story before I need it: more than one trust anchor provisioned at the factory, or a revocation mechanism, and public key hashes in one-time-programmable memory rather than the keys themselves, so I keep room to move.

**꼬리질문**: "What if the production key leaks?" → 신뢰 앵커를 교체하는 업데이트를 내보내야 하고, 이미 유출 키로 서명된 이미지의 롤백을 막아야 한다. 준비 없으면 매우 고통스럽다. / "Who can trigger a production signature?" → 릴리스 권한을 가진 소수 + 승인 절차.

### Q12. How do you decide when to apply an update on the device?

**왜 묻나**: 사용자 경험과 안전 조건. 정책 설계 감각.

**30초 답변**: 조건은 배터리 잔량(또는 충전 중), 저장 공간, 사용 중이 아님, 네트워크 종류다. always-on 음성 기기는 "쓰지 않는 시간"이 애매하므로 야간 + 충전 중을 기본으로 삼고, 사용자에게 연기 수단을 준다. 조건은 매니페스트나 원격 설정으로 조정 가능하게 둔다.

**English answer**: I gate on battery or charging state, free storage, network type, and whether the user is actually using the device, and I make those thresholds remotely adjustable rather than compiled in, so I can tune them without another release. For an always-on voice device there is no obvious idle window, so the default is charging plus a quiet overnight period, with a deferral path for anyone who does not want it. The apply step also has to be interruptible and restartable, because the user will pick the device up in the middle of it. And I never apply while the device is hot or charging fast, because a reboot in the middle of thermal mitigation is a bad time.

**꼬리질문**: "Forced updates?" → 보안 긴급 시에는 조건을 완화하는 별도 경로가 필요하고, 그건 제품/법무와 합의 사항이다. / "What if the user never charges it overnight?" → 고착 기기 지표로 잡히고, 조건을 완화하거나 알림을 준다.

### Q13. What is anti-rollback and when would you advance the counter?

**왜 묻나**: 보안과 운영의 긴장 관계를 이해하는지.

**30초 답변**: 안티 롤백은 취약점이 있는 옛 이미지로 되돌아가는 것을 막는 장치로, 보통 OTP나 보호 카운터에 최소 버전을 기록한다. 버전 번호와 다른 건 "물리적으로 되돌릴 수 없게" 만든다는 점이다. 그래서 카운터는 릴리스 즉시 올리지 않고, 해당 버전이 100% 롤아웃 후 며칠간 안정적일 때 올린다.

**English answer**: Anti-rollback records a minimum acceptable version in memory the application cannot rewrite — a one-time-programmable counter or a protected region — so that an attacker cannot install a genuinely signed but vulnerable old image. It is different from a version number because it is enforced by the bootloader and it is irreversible. The operational tension is obvious: the moment I advance it, I lose the ability to go back. So I decouple it from release. The release ships with the new version, and only after it has been at full rollout for several days with healthy metrics does a later update advance the counter. For an actual security fix, I advance it sooner and accept the cost.

**꼬리질문**: "Where is the counter stored?" → OTP/fuse, 또는 보호된 카운터 영역. 벤더마다 다르다. / "How many times can it advance?" → OTP 비트 수만큼. 유한하므로 릴리스마다 올리지 않는다.

### Q14. How is updating an SSD's firmware different from OTA on a consumer device?

**왜 묻나**: Don의 배경을 알고 던지는 질문이 될 가능성이 높다. 전이 가능성을 스스로 설명할 기회.

**30초 답변**: 공통점은 많다. 슬롯 개념, 서명 검증, 전원 차단 안전성, 활성화 후 검증, 실패 시 이전 이미지 유지. 차이는 환경이다. SSD는 호스트가 관리하고 전원이 비교적 안정적이며 관리자가 개입할 수 있다. 컨슈머 기기는 배터리, 불안정한 무선, 사용자 개입 불가, 수백만 대 규모의 단계적 롤아웃이 추가된다.

**English answer**: The device-side problem is closer than people expect. An enterprise SSD also has firmware slots, an activation step separate from the download, signature and integrity checks, and a hard requirement that a power loss during activation cannot leave an unbootable drive. What changes on a consumer device is everything around it: the link is a flaky radio instead of a host bus, there is a battery that can die mid-operation, no administrator will ever intervene, and the deployment is millions of units, so staged rollout, cohorts, kill switches and fleet telemetry become as important as the bootloader. The reliability instincts carry over; the distribution and observability side is what I would be building new.

**꼬리질문**: "So what would you have to learn?" → 배포 서버·코호트 운영, 무선 전송 경로, 매니페스트/의존성 관리. / <확인 필요: Don이 SSD에서 firmware download/commit 경로(NVMe Firmware Image Download / Firmware Commit)를 직접 구현하거나 검증했는지. 했다면 이 답이 훨씬 구체적이 된다.>

### Q15. If you could only build half of this system in the first six months, what would you build?

**왜 묻나**: 우선순위 판단. 스타트업에서 중요한 질문.

**30초 답변**: 되돌릴 수 없는 것부터 만든다. 파티션 레이아웃, 부트로더와 서명 검증, 신뢰 앵커 provisioning, 그리고 기기 측 A/B + confirm/revert. 서버·롤아웃·대시보드는 나중에 바꿀 수 있지만, 출하된 기기의 부트로더와 파티션은 못 바꾼다. 초기에는 수동 배포라도 상관없다.

**English answer**: I build the parts that ship into silicon and flash and can never be changed afterwards: the partition layout, the bootloader with signature verification and revert, the trust anchor provisioned in the factory, and the on-device confirm logic. Those decisions are permanent the moment the first unit leaves the line. The server, the cohort tooling and the dashboards are software I control and can replace next quarter, so in the first months a script that uploads to a bucket is acceptable. The one thing I would not defer is the telemetry schema, because I cannot retroactively collect data from devices that already failed, and the first field failures are the most informative ones I will ever get.

**꼬리질문**: "What about factory provisioning?" → 신뢰 앵커·기기 ID·인증서는 공장 라인에서 넣어야 하므로 초기에 확정해야 한다(C09와 연결). / "What would you explicitly not build?" → 델타 업데이트, 복잡한 스케줄링 정책. 나중에 추가 가능하다.

---

## 6. Don 매핑

### 6.1 쓸 수 있는 근거 (context 3.1절 레쥬메 기반만)

| 레쥬메 근거 | 이 JD 문장과의 연결 | 어떻게 말할지 |
|---|---|---|
| 엔터프라이즈 SSD **양산 펌웨어** (Senior→Staff) | 필드에 나간 펌웨어를 책임진 경험 | "출하된 펌웨어가 고객 데이터센터에서 실패할 때의 무게를 안다" |
| "NVMe **telemetry** 디버그 기능", "error reporting/handling 설계" | 2.7 지표·단계별 실패 코드 설계와 같은 종류 | "무엇을 기록해야 나중에 원인을 알 수 있는지를 설계해 본 경험" — OTA 실패 코드 스키마로 직결 |
| "designing and leading **factory test-node architecture**" | 전원 차단 리그·업그레이드 매트릭스 자동화 | "DUT + 계측 + 자동화 리그를 설계해 본 경험이 OTA 테스트 리그에 그대로 적용된다" |
| "Silicon/system bring up → NPI → **MP**" | 공장 provisioning(신뢰 앵커·기기 ID)과 출하 절차 | "양산 라인에 들어가는 절차를 설계해 봤다" (C09와 연결) |
| "root-cause analysis ... when a new chip meets the full HW/SW system" | 멀티칩 호환성 문제(2.4)의 디버깅 | "칩 경계에서 생기는 불일치를 추적하는 게 지난 1년의 일이었다" |
| 고객(MS/DELL/HPE/Meta/Google) 기한 대응 | 릴리스 열차 운영 | "정해진 일정에 검증된 펌웨어를 내보내는 리듬을 겪어 봤다" |

### 6.2 강점 스토리 (STAR 골격)

**스토리 A — telemetry/error reporting → OTA 관측성**

- Situation: 양산 SSD에서 필드 실패를 진단할 방법이 필요했다.
- Task: 무엇을 기록하고 어떻게 내보낼지 설계.
- Action: telemetry 디버그 기능과 에러 분류·보고 체계를 설계하고 고객 요구 기한에 맞춰 출시.
- Result: 필드 이슈 진단 경로 확보.
- **연결 문장**: "OTA에서도 같은 질문이다. 업데이트가 실패했을 때 어느 단계에서 왜 실패했는지 기기가 스스로 말할 수 있어야 한다. 단계별 실패 코드 스키마를 처음부터 설계하겠다."

**스토리 B — factory test-node → OTA 테스트 리그**

- 전원 차단 수백 회 반복, 업그레이드 매트릭스, 손상 이미지 거부 테스트는 전부 자동화 리그 문제다. 이 경험을 가진 펌웨어 후보는 드물다.
- **연결 문장**: "벽돌이 안 난다는 걸 주장으로 증명할 수는 없다. 리그로 증명해야 한다."

**스토리 C — MP 출하 절차 → factory provisioning**

- 신뢰 앵커(공개키 해시), 기기 ID, 인증서를 공장에서 넣는 일은 OTA의 전제 조건이다. Don의 NPI→MP 경험이 여기에 붙는다(C09).

### 6.3 갭과 프레이밍

| 갭 | 솔직한 현실 | 프레이밍 |
|---|---|---|
| MCUboot 실무 | 써 본 적 없음 | "MCUboot의 슬롯·trailer·swap 방식과 `boot_request_upgrade`/`boot_write_img_confirmed` 흐름은 공부했고, nRF52840 DK에서 직접 올려 볼 계획/중이다" — 실제로 해 본 뒤에만 말할 것 |
| 배포 서버·클라우드 | 경험 없음 | "서버 쪽은 새로 배워야 한다. 다만 기기 측 계약(무엇을 보고하고 무엇을 믿는가)을 정의하는 일은 내가 해 온 인터페이스 설계와 같다" |
| 무선 전송 경로 | BLE/Wi-Fi 스택 경험 없음 | Apple 무선 칩셋 통합 경험으로 연결하되 과장하지 않음 |
| 단계적 롤아웃 운영 | 소비자 fleet 운영 경험 없음 | "수백만 대 롤아웃은 안 해 봤다. 대신 고객 기한에 맞춰 검증된 펌웨어를 내보내는 릴리스 규율은 몸에 있다" |
| secure boot / 서명 | <확인 필요: 엔터프라이즈 SSD 펌웨어에 이미지 서명·검증 흐름이 있었고 Don이 관여했는지. context 3.5절이 "해당 경험이 있다면" 레쥬메에 추가하라고 제안하고 있다. 있으면 우대 사항 두 개를 한 번에 채운다.> | 있으면: "양산 SSD에서 이미지 검증 흐름을 다뤘다"로 직접 답. 없으면: C07 학습 내용으로 개념 답변 |

### 6.4 절대 하지 말 것

OTA 파이프라인을 만들어 봤다고 말하지 않는다(레쥬메에 근거가 없다). "SSD도 OTA랑 똑같다"고 단정하지 않는다 — Q14처럼 **공통점과 차이를 나눠서** 말해야 신뢰를 얻는다. 특정 도구의 기본값(MCUboot swap 방식 등)을 단정하지 않는다.

---

## 7. 준비 체크리스트

- [ ] 2.1의 11단계 파이프라인을 화이트보드에 2분 안에 그리고 각 단계의 실패 모드를 하나씩 말한다.
- [ ] nRF52840 DK에 MCUboot + `smp_svr`를 올려 실제로 DFU를 한 번 완수한다(C07 §13.1 실습).
- [ ] `imgtool`로 직접 키를 만들고 서명·검증해 보고, 바이트 하나를 뒤집어 거부되는 것을 확인한다(C07 §13.2).
- [ ] 스왑 중 전원을 끊었다가 다시 켜서 재개되는 걸 직접 본다(C07 §13.3) — 면접에서 "직접 봤다"고 말할 수 있는 몇 안 되는 항목.
- [ ] `boot_request_upgrade(BOOT_UPGRADE_TEST)` → self-test → `boot_write_img_confirmed()` 흐름을 직접 코드로 넣고, confirm을 일부러 빼서 revert되는 것을 관찰한다.
- [ ] 2.5의 매니페스트를 자기 버전으로 다시 써 본다(의존성·적격성·조건 필드 포함).
- [ ] 3.4의 멀티칩 업데이트 순서를 Hark 구조(SoC + MCU + 라디오) 가정으로 설명하는 연습(2분).
- [ ] 2.7 지표 7개와 3.7 롤아웃 게이트를 외운다. Q08은 거의 확실히 나온다.
- [ ] 6.3의 <확인 필요> 항목(SSD 이미지 서명·검증 관여 여부, NVMe firmware download/commit 경험)을 정리하고, 있으면 레쥬메에 반영한다(context 3.5).
- [ ] Q01·Q03·Q05의 English answer를 소리 내어 읽고 각 60~75초로 맞춘다.
- [ ] 역질문 준비: "OTA는 이미 정해진 구조가 있나요, 이 역할이 결정하나요? SoC와 MCU 부트 체인은 각각 무엇을 쓰나요?"

---

## 8. 더 읽기

- **C07 §1~§2** OTA가 어려운 이유와 flash 레이아웃 전략(A/B, single-slot+recovery, MCUboot swap 방식 단계별 설명).
- **C07 §3** 이미지 헤더·TLV·trailer 구조와 OTA 상태 머신, **§4** 해시·서명·키의 최소 지식.
- **C07 §5** Root of Trust와 chain of trust(공개키 해시를 OTP에 넣는 이유), **§6** 서명 검증 흐름 C 코드.
- **C07 §7** 안티 롤백·이미지 암호화·delta update, **§8** 전원 차단 안전성과 confirm/revert 코드.
- **C07 §9** 다중 프로세서 업데이트와 단계적 롤아웃 — 이 노트 2.4/3.7의 개념적 근거.
- **C07 §10** 디버그 포트 잠금과 TrustZone-M, **§13** 실습 4개.
- **S04 Q01~Q07** OTA 기초와 A/B, **Q08~Q14** secure boot·서명·키, **Q15~Q17** 멀티 프로세서 업데이트, **Q18~Q20** factory provisioning.
- **C09** factory test·calibration — 신뢰 앵커와 기기 ID를 라인에서 넣는 절차, 캘리브레이션 파티션 보호.
- **J03 §3.3** residency/telemetry 구조 — OTA telemetry 스키마와 같은 설계 문제. **J03 §3.6** thermal 정책을 데이터로 두고 OTA로 조정하는 패턴.
- **J06** factory test firmware, **J05** on-device AI — 모델 파일도 OTA 대상이라는 점에서 연결된다.
