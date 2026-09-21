# C07. OTA & Secure Boot — 벽돌이 되지 않고, 남의 코드는 절대 돌지 않는 업데이트

> **이 노트를 다 읽으면**: MCUboot의 슬롯·스왑 방식·image trailer를 화이트보드에 그릴 수 있다 · ROM부터 앱까지 chain of trust와 서명 검증 흐름을 C 코드로 설명할 수 있다 · 전원 차단·rollback·다중 프로세서 업데이트를 고려한 OTA 상태 머신을 설계할 수 있다 · 디버그 포트 잠금과 TrustZone-M의 역할을 한 문장으로 말할 수 있다
> **JD 연결**: "Build and maintain OTA update infrastructure for reliable field updates" / Bonus: "Familiarity with secure boot, firmware signing, or hardware root of trust"
> **Don 기준 난이도**: SSD FW의 firmware download/commit(NVMe Firmware Image Download·Firmware Commit), flash 파티션, CRC는 이미 안다 / 공개키 서명·키 관리·MCUboot 스왑 알고리즘·Zephyr DFU API·anti-rollback은 새로 배운다

---

## 0. 큰 그림

OTA(Over-The-Air update)와 secure boot는 한 쌍이다. OTA는 "새 코드를 기기에 넣는 길"이고, secure boot는 "그 길로 들어온 코드가 진짜 우리 코드인지 부팅할 때마다 확인하는 문지기"다. 둘 중 하나만 있으면 의미가 없다. 서명 검증 없는 OTA는 공격자에게 원격 코드 실행 통로를 열어 주는 것이고, OTA 없는 secure boot는 버그를 고칠 수 없는 기기를 만든다.

Hark 같은 SoC + always-on MCU + 무선 콤보칩 구조의 기기라면 업데이트 대상은 하나가 아니다.

```
                         [ Cloud: Release server + Signing HSM ]
                                        |
                                  HTTPS (TLS)
                                        |
   +------------------------------------v-------------------------------------+
   |  Application SoC (Cortex-A, Android/Linux)                               |
   |    update agent (예: Android update_engine)                              |
   |    - SoC 자신: A/B 파티션 (boot_a/boot_b, system_a/system_b ...)          |
   |    - 다른 칩 이미지도 받아서 중계                                         |
   +-----------+-----------------------------+--------------------------------+
               | UART/SPI (SMP, 자체 프로토콜)  | PCIe/SDIO/UART (벤더 FW 다운로드)
               v                             v
   +-----------------------------+   +--------------------------------+
   |  Always-on MCU (Cortex-M)   |   |  Wi-Fi/BT combo, modem, GNSS   |
   |  MCUboot + slot0/slot1      |   |  벤더가 서명한 FW blob          |
   |  RTOS 앱 (wake word 등)      |   |  (호스트가 부팅 때마다 로드하거나 |
   +-----------------------------+   |   칩 내부 flash에 기록)          |
                                     +--------------------------------+

   부팅 시점 (각 칩마다 독립적인 chain of trust)
   ROM(불변) --verify--> 1st bootloader --verify--> 2nd bootloader/OS --verify--> App
```

이 노트의 흐름은 다음과 같다.

1. 왜 OTA가 어렵나 (실패 모드)
2. flash 레이아웃 전략: A/B, single-slot + recovery, MCUboot 스왑 방식
3. 이미지 형식: 헤더, TLV, 버전, trailer
4. 암호학 기초: 해시, 서명(ECDSA P-256/Ed25519), 키 관리
5. Root of trust와 chain of trust
6. anti-rollback, 이미지 암호화, delta update
7. 전원 차단 안전성과 confirm/revert
8. 다중 프로세서 업데이트와 단계적 롤아웃
9. 디버그 포트 잠금, TrustZone-M
10. Zephyr + MCUboot 실전 코드

---

## 1. OTA가 어려운 이유 — 실패 모드부터 본다

OTA 설계는 "무엇이 잘못될 수 있나"를 먼저 나열하는 것에서 시작한다. 컨슈머 기기에서는 사용자가 업데이트 도중 배터리를 다 쓰거나, 주머니 속에서 BLE 연결이 끊기거나, 기기를 떨어뜨린다. 필드에 100만 대가 있으면 0.01% 확률의 실패도 100대의 벽돌이다.

| 실패 모드 | 결과 | 방어 수단 |
|---|---|---|
| 다운로드 중 연결 끊김 | 불완전한 이미지 | 비활성 슬롯에 쓰고, 끝난 뒤 전체 해시 검증. 이어받기(offset resume) |
| 다운로드한 이미지가 손상/변조 | 이상한 코드 실행 | 서명 검증(부트로더가 매 부팅 또는 설치 시) |
| 설치(스왑/복사) 중 전원 차단 | 반쯤 덮어쓴 슬롯 | 스왑 진행 상태를 flash에 기록하고 재부팅 후 이어서 진행 |
| 새 FW가 부팅은 되지만 네트워크/BLE가 안 됨 | 다시 업데이트 불가 = 사실상 벽돌 | test 부팅 후 self-test 통과해야 confirm, 아니면 자동 revert |
| 공격자가 옛날 취약한 버전을 서명째로 재설치 | 이미 고친 취약점 재노출 | anti-rollback(보안 카운터, OTP) |
| SoC는 새 버전, MCU는 옛 버전 | 프로토콜 불일치 | 버전 의존성 검사, 호환 매트릭스, 원자적 번들 |
| 서명 키 유출 | 모든 기기 탈취 가능 | HSM 보관, 키 폐기(revocation) 슬롯, 키 해시를 OTP에 여러 개 |

> Don 경험과 연결: SSD에서 NVMe Firmware Image Download로 이미지 조각을 받고 Firmware Commit으로 슬롯을 활성화하던 흐름이 거의 그대로 OTA의 뼈대다. SSD는 commit action(다음 리셋 때 활성화 vs 즉시 활성화)과 여러 firmware slot이 있었다. 차이는 (1) 호스트 대신 무선/클라우드가 이미지를 보내고, (2) 배터리 기기라 전원 차단이 훨씬 흔하고, (3) 서명 키와 rollback 정책을 우리가 직접 설계해야 한다는 점이다.

---

## 2. Flash 레이아웃 전략

### 2.1 세 가지 기본 패턴

```
(a) A/B (dual bank, 대칭)           (b) Single-slot + recovery          (c) Staging slot (MCUboot식)
+------------------+                +------------------+                +------------------+
| Bootloader       |                | Bootloader       |                | Bootloader       |
+------------------+                +------------------+                +------------------+
| Slot A  (실행중) |                | Recovery image   |  작은 이미지:    | Primary (slot0)  | 항상 여기서 실행
|                  |                | (BLE/USB DFU만)  |  업데이트만 가능 |                  |
+------------------+                +------------------+                +------------------+
| Slot B  (대기)   |                | Main app         |  업데이트 때     | Secondary(slot1) | 새 이미지 도착지
|                  |                | (큰 이미지 1개)  |  지워지고 다시 씀 |                  |
+------------------+                +------------------+                +------------------+
| Data / settings  |                | Data             |                | (scratch)        |
+------------------+                +------------------+                | Data             |
                                                                        +------------------+
```

| 항목 | A/B (실행 위치 교대) | Single-slot + recovery | Staging (MCUboot swap) |
|---|---|---|---|
| flash 사용량 | 앱 크기의 2배 | 앱 1배 + recovery(작음) | 앱 크기의 2배 (+ scratch) |
| 업데이트 중 기기 사용 | 가능 (백그라운드로 B에 씀) | 불가 (recovery 모드에서 씀) | 가능 (slot1에 씀) |
| 실패 시 복구 | 이전 슬롯으로 즉시 전환 | recovery가 다시 받음 | trailer 기반 자동 revert |
| 요구 사항 | 두 위치에서 실행 가능해야 함 (XIP 주소 문제) | recovery가 무선 스택을 가져야 함 | 부트로더가 스왑 수행 (시간·flash 마모) |
| 대표 예 | Android A/B, MCUboot direct-xip | flash가 작은 MCU, 일부 BLE 센서 | MCUboot 기본 모드 |

A/B에서 가장 큰 기술 문제는 **실행 주소**다. Cortex-M은 대개 내부 flash에서 XIP(eXecute In Place)로 실행하고, 링커가 절대 주소를 박아 넣는다. slot A용으로 링크한 바이너리는 slot B 주소에서 돌 수 없다. 해결책은 (1) 슬롯마다 따로 링크한 이미지 두 개를 배포하거나(MCUboot direct-xip 방식), (2) 하드웨어 bank swap 기능(일부 STM32의 dual-bank flash처럼 주소 매핑을 바꾸는 기능, 벤더마다 다름)을 쓰거나, (3) 이미지를 RAM에 복사해서 실행(MCUboot ram-load)하는 것이다. Cortex-A + Linux는 MMU와 파일시스템이 있어서 이 문제가 거의 없다.

### 2.2 MCUboot 업그레이드 모드

MCUboot는 Cortex-M 세계의 사실상 표준 2단 부트로더다. Zephyr, NuttX, Mynewt, ESP-IDF 포트, TF-M(BL2)에서 쓴다. 기본 개념은 **primary slot(slot0)에서만 실행**하고, 새 이미지는 **secondary slot(slot1)**에 받아 둔 뒤 부트로더가 옮기는 것이다. 옮기는 방식이 모드다.

| 모드 | 동작 | revert 가능 | 추가 flash | flash 마모 | 비고 |
|---|---|---|---|---|---|
| overwrite-only | slot1을 slot0에 덮어씀 | 불가 | 없음 | 적음 | 가장 단순, 빠름. 옛 이미지는 사라짐 |
| swap-using-scratch | scratch 영역을 거쳐 sector 단위로 slot0과 slot1을 교환 | 가능 | scratch 영역 | scratch에 집중 | 전통적 방식. slot 크기가 달라도 되지만 scratch가 가장 많이 닳음 |
| swap-using-move | slot0 내용을 한 sector씩 위로 밀고(move), 그다음 sector 단위 교환 | 가능 | slot0에 sector 1개 여유 | 고르게 분산 | scratch 불필요. 최근 Zephyr에서 흔히 쓰는 기본값(버전마다 다름) |
| swap-using-offset | slot1 이미지를 한 sector 오프셋으로 저장해 move 단계를 생략 | 가능 | slot1에 sector 1개 여유 | 적음 | 비교적 최근 추가된 모드 (MCUboot 버전 확인 필요) |
| direct-xip | 스왑 없이 slot0 또는 slot1에서 직접 실행. 더 높은 버전을 고름 | 옵션(`MCUBOOT_DIRECT_XIP_REVERT`) | 없음 | 가장 적음 | 슬롯마다 다른 주소로 링크한 이미지 필요 |
| ram-load | 선택한 슬롯 이미지를 RAM으로 복사 후 실행 | 옵션 | RAM | 없음 | 외부 flash + 큰 SRAM 시스템 |

### 2.3 swap-using-scratch를 한 단계씩

slot0 = [A0 A1 A2], slot1 = [B0 B1 B2]이고 sector 크기가 같다고 하자. 부트로더는 **마지막 sector부터** 교환한다(trailer가 slot 끝에 있어서 먼저 처리하는 것이 구현상 편하다는 정도로 이해하면 된다).

```
초기          slot0: [A0][A1][A2]   slot1: [B0][B1][B2]   scratch: [  ]
step 1  B2 -> scratch                                     scratch: [B2]
step 2  A2 -> slot1 의 2번 자리      slot1: [B0][B1][A2]
step 3  scratch -> slot0 의 2번 자리 slot0: [A0][A1][B2]
        ... (각 step 끝나면 swap status 에 "sector 2, state N 완료" 기록)
반복    sector 1, sector 0
결과          slot0: [B0][B1][B2]   slot1: [A0][A1][A2]   <- 새 이미지 실행, 옛 이미지는 slot1 에 보존
```

핵심은 **각 단계가 끝날 때마다 진행 상태(swap status)를 flash에 기록**한다는 것이다. step 2 도중 전원이 나가면, 재부팅한 부트로더가 status를 읽고 "sector 2의 state 1까지 완료"를 알아서 거기서 이어서 한다. 옛 이미지가 slot1에 남아 있으므로, 새 이미지가 confirm되지 않으면 같은 알고리즘으로 한 번 더 스왑해서 되돌린다(revert).

### 2.4 swap-using-move를 한 단계씩

scratch 없이 하려면 "빈 자리 한 칸"이 필요하다. slot0에 sector 하나를 더 둔다.

```
초기     slot0: [A0][A1][A2][--]        slot1: [B0][B1][B2]
move     A2 -> 3번, A1 -> 2번, A0 -> 1번   (위에서부터 한 칸씩 위로)
         slot0: [--][A0][A1][A2]
swap     B0 -> slot0[0], A0(slot0[1]) -> slot1[0]
         B1 -> slot0[1], A1(slot0[2]) -> slot1[1]
         B2 -> slot0[2], A2(slot0[3]) -> slot1[2]
결과     slot0: [B0][B1][B2][..]        slot1: [A0][A1][A2]
```

모든 sector가 비슷한 횟수로 지워지므로 scratch 방식처럼 한 영역만 닳는 문제가 없다. 대신 slot0이 slot1보다 한 sector 커야 하고, 이미지 최대 크기는 그만큼 줄어든다.

### 2.5 nRF52840 예시 파티션 (1 MB 내부 flash, 4 KB page)

아래는 교육용 예시다. 실제 오프셋은 Zephyr 보드 DTS나 nRF Connect SDK의 Partition Manager/sysbuild 설정에 따라 다르다.

```
0x0000_0000 +---------------------------+
            | mcuboot        48 KB      |  boot_partition  (쓰기 보호 권장)
0x0000_C000 +---------------------------+
            | image-0 (slot0) 472 KB    |  slot0_partition
            |  [header 0x200][code...]  |
            |  ...          [TLV][trl]  |  trl = image trailer (slot 끝)
0x0008_2000 +---------------------------+
            | image-1 (slot1) 472 KB    |  slot1_partition
0x000F_8000 +---------------------------+
            | storage        32 KB      |  storage_partition (NVS/settings)
0x0010_0000 +---------------------------+
```

devicetree에서는 `chosen { zephyr,code-partition = &slot0_partition; }`로 앱이 slot0에 링크되도록 한다. MCUboot 빌드는 `zephyr,code-partition = &boot_partition`을 쓴다. sysbuild(`west build --sysbuild`)를 쓰면 두 이미지를 한 번에 빌드한다.

### 2.6 흔한 함정

- slot0과 slot1 크기를 다르게 잡고 swap 모드를 켜면 스왑이 불가능하거나 이미지 크기 제한이 예상과 다르다. imgtool의 `--slot-size`와 실제 파티션이 맞아야 한다.
- 앱의 `--header-size`(보통 0x200)만큼 벡터 테이블이 뒤로 밀린다. 앱을 MCUboot 없이 링크하면 부트로더가 점프한 주소에 벡터 테이블이 없어서 HardFault가 난다. Zephyr에서는 `CONFIG_BOOTLOADER_MCUBOOT=y`가 `CONFIG_ROM_START_OFFSET`을 맞춰 준다.
- storage 파티션을 slot 뒤에 붙였는데 이미지가 커져서 경계를 넘으면 설정이 지워진다. 링크 단계에서 크기 검사를 CI에 넣는다.

---

## 3. 이미지 형식 — 헤더, TLV, trailer

### 3.1 MCUboot 이미지의 세 부분

```
slot 시작
+-------------------------------+  offset 0
| image_header (32 bytes)       |  magic 0x96f3b83d, load_addr, hdr_size,
|   + padding to hdr_size       |  protect_tlv_size, img_size, flags, version
+-------------------------------+  offset hdr_size (예: 0x200)
| image body (벡터 테이블부터)    |  <- 해시 대상: header + body (+ protected TLV)
|                               |
+-------------------------------+  offset hdr_size + img_size
| protected TLV area (선택)      |  magic 0x6908. 예: security counter, dependency
+-------------------------------+
| TLV info (magic 0x6907, len)  |
|   TLV: SHA256 hash            |  이미지 해시
|   TLV: KEYHASH                |  서명한 공개키의 해시 (어느 키인지)
|   TLV: signature (ECDSA/Ed25519/RSA)
+-------------------------------+
|        (빈 공간 0xFF)          |
+-------------------------------+
| image trailer                 |  swap status, swap_size, swap_info,
|   ... copy_done, image_ok     |  copy_done, image_ok, magic(16 bytes)
+-------------------------------+  slot 끝
```

- **image header**: 이미지가 무엇인지(버전, 크기, 로드 주소). MCUboot의 `struct image_header`에서 버전은 `struct image_version { uint8_t iv_major; uint8_t iv_minor; uint16_t iv_revision; uint32_t iv_build_num; }`다. imgtool의 `--version 1.2.3+4`가 major 1, minor 2, revision 3, build 4가 된다.
- **TLV(Type-Length-Value) 영역**: 해시와 서명 같은 메타데이터. 서명 대상 밖에 있어야 하므로(서명이 자기 자신을 포함할 수 없다) body 뒤에 붙는다. **protected TLV**는 해시 계산에 포함되어 서명으로 보호되는 TLV다. security counter 같은 값은 반드시 protected여야 공격자가 못 바꾼다.
- **image trailer**: 이미지 내용이 아니라 **업데이트 상태 머신의 상태**다. slot 맨 끝에 있고, 앱이나 부트로더가 여기 몇 바이트를 써서 "이 이미지를 다음 부팅 때 설치해라(pending)", "설치된 이미지가 정상이다(confirmed)"를 표시한다.

### 3.2 image trailer의 핵심 필드

| 필드 | 의미 | 누가 쓰나 |
|---|---|---|
| magic | 이 trailer가 유효하다. slot1에 magic이 있으면 "업그레이드 요청됨" | 앱 (`boot_request_upgrade`) |
| image_ok | 이 이미지는 확정(confirmed)되었다 | 앱 (`boot_write_img_confirmed`) 또는 permanent 요청 시 |
| copy_done | 스왑/복사가 완료되었다 | 부트로더 |
| swap_info | 스왑 종류(test/perm/revert)와 이미지 번호 | 부트로더 |
| swap_size | 스왑한 크기 (재개할 때 사용) | 부트로더 |
| swap status | sector별 진행 상태 | 부트로더 |

flash는 1→0으로만 쓸 수 있고 0→1은 erase가 필요하므로, trailer 필드는 "지워진 상태(0xFF) = 미설정"으로 설계되어 있다. 따라서 필드 하나를 set할 때 erase 없이 한 번 쓰기로 끝난다. 이것이 전원 차단에 강한 이유다. (flash write 단위가 큰 칩, 예를 들어 일부 STM32의 8~16바이트 단위, 에서는 필드가 write alignment만큼 커진다.)

### 3.3 스왑 결정 표 (단순화)

부트로더는 부팅할 때 slot0과 slot1의 trailer를 읽고 무엇을 할지 정한다. MCUboot 설계 문서의 표를 개념만 남기고 줄이면 다음과 같다.

| slot1 magic | slot1 image_ok | slot0 magic | slot0 image_ok / copy_done | 결정 |
|---|---|---|---|---|
| good | unset | (무관) | (무관) | **test 스왑**: 새 이미지를 slot0으로, confirm 안 되면 다음 부팅에 revert |
| good | set | (무관) | (무관) | **permanent 스왑**: 새 이미지를 영구 설치 |
| (무관) | (무관) | good | image_ok unset, copy_done set | **revert**: 지난 부팅에 test로 올라온 이미지가 confirm 안 됨 → 되돌림 |
| 그 외 | | | | 스왑 없음, slot0 부팅 |

### 3.4 OTA 상태 머신

```
                  +-----------+
                  |   IDLE    |<------------------------------------------+
                  +-----+-----+                                           |
                        | 서버가 새 버전 알림 (manifest: 버전, 크기, 해시, 의존성)
                        v                                                 |
                  +-----------+  배터리<30% / 충전 중 아님 / 사용자 통화중  |
                  | PRECHECK  |------------------------> DEFER -----------+
                  +-----+-----+
                        v
                  +-----------+  끊김 -> 받은 offset 부터 재개
                  | DOWNLOAD  |---+  (slot1 에 청크 단위 기록)
                  +-----+-----+   |
                        v         |
                  +-----------+   |  해시/서명 실패 -> slot1 erase, 에러 보고 -> IDLE
                  |  VERIFY   |---+
                  +-----+-----+
                        v  boot_request_upgrade(BOOT_UPGRADE_TEST)
                  +-----------+
                  |  PENDING  |  적절한 시점(사용자 idle)에 reboot
                  +-----+-----+
                        v  MCUboot: 서명 검증 + test 스왑
                  +-----------+
                  | TRIAL RUN |  self-test: 센서, 무선 연결, 서버 체크인
                  +--+-----+--+
            통과     |     | 실패 / watchdog reset / 타임아웃
                     v     v
       boot_write_img_     다음 부팅에 MCUboot 가 revert
       confirmed()         (옛 이미지 복귀, 서버에 실패 보고)
                     |
                     v
                 CONFIRMED -> 서버에 성공 보고 -> IDLE
```

**TRIAL RUN에서 무엇을 확인하고 confirm하나**가 설계의 핵심이다. 최소 기준은 "다음 업데이트를 받을 수 있는 능력이 살아 있는가"다. 부팅 직후 바로 confirm하면 revert의 의미가 없다. 반대로 너무 오래 기다리면 사용자가 그 사이에 기기를 껐다 켜서 괜히 revert될 수 있다. 흔한 절충은 "통신 경로 확인(서버 체크인 또는 SoC와의 링크 확인) + 주요 태스크가 N초 동안 watchdog을 정상적으로 feed"다.

---

## 4. 암호학 최소 지식 — 해시, 서명, 키

### 4.1 CRC vs 해시 vs MAC vs 서명

| 수단 | 막는 것 | 못 막는 것 | 키 |
|---|---|---|---|
| CRC-32 | 우연한 비트 오류 | 고의 변조 (공격자가 CRC도 다시 계산) | 없음 |
| SHA-256 | 우연한 오류 + 해시를 안전한 곳에 두면 변조 탐지 | 해시 자체가 이미지와 같이 오면 변조 가능 | 없음 |
| HMAC / AES-CMAC | 변조 (대칭키) | 기기마다 같은 키면 기기 하나 털리면 전체 위조 가능 | 공유 비밀키 |
| 디지털 서명 (ECDSA, Ed25519, RSA) | 변조 + 출처 증명 | 서명 키 유출 | 비밀키는 서버(HSM)에만, 기기엔 공개키 |

기기에는 **공개키(또는 그 해시)만** 들어간다. 기기를 분해해서 flash를 전부 읽어도 공개키로는 서명을 만들 수 없다. 이것이 대칭키 MAC보다 서명이 펌웨어 인증에 적합한 이유다.

### 4.2 서명 알고리즘 비교

| 알고리즘 | 공개키 | 서명 | 검증 속도(MCU) | MCUboot 지원 | 메모 |
|---|---|---|---|---|---|
| RSA-2048 (PSS) | 256 B | 256 B | 검증은 빠른 편 (공개 지수 작음) | 예 | 키·서명이 커서 flash/TLV 부담 |
| RSA-3072 | 384 B | 384 B | 느려짐 | 예 | |
| ECDSA P-256 | 64 B (+1 B 형식) | 64 B raw (DER은 약 70~72 B) | 중간. HW 가속기(CryptoCell 등) 있으면 빠름 | 예 | 가장 흔한 선택. HW 가속 지원이 넓음 |
| ECDSA P-384 | 96 B | 96 B | 더 느림 | 최근 버전 지원 | 높은 보안 등급 요구 시 |
| Ed25519 | 32 B | 64 B | 소프트웨어로도 빠름 | 예 | 결정적 서명(난수 불필요), 구현 실수 여지 적음. HW 가속 지원은 P-256보다 드묾 |

ECDSA는 **서명할 때** 좋은 난수(nonce k)가 필요하다. k가 재사용되거나 예측 가능하면 비밀키가 계산된다(PS3 서명 키 유출 사건이 이 경우). 검증하는 기기 쪽에는 이 위험이 없지만, 서명 서버를 설계할 때 알아야 한다. 요즘은 결정적 ECDSA(RFC 6979)나 Ed25519로 이 문제를 피한다.

양자 내성(PQC) 서명(ML-DSA, LMS/XMSS 같은 해시 기반 서명)은 장수명 기기의 부트 체인에서 논의가 늘고 있다. 면접에서 "알고는 있다" 수준으로 말할 수 있으면 충분하다.

### 4.3 키 관리 — 기술보다 절차가 어렵다

- 서명용 비밀키는 **HSM(Hardware Security Module)이나 클라우드 KMS**에 두고, 빌드 서버는 해시만 보내서 서명을 받는다. 개발자 노트북에 운영 키를 두지 않는다.
- **개발 키와 운영 키를 분리**한다. 개발 보드(EVT)는 개발 키, 출하 기기는 운영 키만 받는다. MCUboot 저장소에 있는 `root-ec-p256.pem` 같은 샘플 키로 출하하는 실수가 실제로 보고된다.
- **키 폐기(revocation)**를 미리 설계한다. OTP에 공개키 해시를 여러 개(예: 3~4개) 넣고, 폐기 비트를 두면 키 하나가 유출되어도 다음 키로 넘어갈 수 있다. nRF Connect SDK의 NSIB가 이런 식으로 여러 키 해시와 무효화를 지원한다.

---

## 5. Root of Trust와 Chain of Trust

### 5.1 정의

**Root of trust(RoT)**는 "검증 없이 믿을 수밖에 없는 출발점"이다. 보통 두 가지로 이루어진다.

1. **불변 코드**: 마스크 ROM의 boot ROM, 또는 쓰기 보호로 영구히 잠근 1단 부트로더
2. **불변 키 정보**: OTP/eFuse에 기록한 공개키 해시, 또는 secure element 안의 키

RoT가 다음 단계를 검증하고, 그 단계가 또 다음 단계를 검증하는 것이 **chain of trust**다. 체인 중 하나라도 검증 없이 넘어가면 그 뒤는 전부 믿을 수 없다.

```
 +-----------+   verify   +------------------+  verify   +------------------+  verify  +---------+
 | Boot ROM  |----------->| Immutable BL     |---------->| Updatable BL     |--------->| App/OS  |
 | (mask ROM)|  sig w/    | (예: NSIB, 또는  |  sig w/   | (예: MCUboot)    |  sig w/  | (Zephyr,|
 |           |  OTP key   |  잠근 MCUboot)   |  key in   |                  |  key in  |  TF-M NS|
 +-----------+  hash      +------------------+  BL/OTP   +------------------+  MCUboot +---------+
      ^                           ^
      |                           |
  OTP/eFuse: ROTPK hash[0..N], revoke bits, lifecycle state, anti-rollback counter
```

### 5.2 왜 공개키가 아니라 "공개키 해시"를 OTP에 넣나

OTP는 비싸고 작다(수백 비트~수 KB). P-256 공개키는 64바이트, RSA-3072는 384바이트다. SHA-256 해시는 32바이트로 고정된다. 그래서 이미지(또는 부트로더) 안에 공개키 전체를 넣고, 부팅 시 "이 공개키의 해시 == OTP의 해시"를 먼저 확인한 다음 그 공개키로 서명을 검증한다. MCUboot도 공개키를 이미지 TLV에 넣고 해시만 하드웨어에서 가져오는 방식(`MCUBOOT_HW_KEY`)을 지원한다.

### 5.3 RoT 구현 선택지

| 방식 | 예 | 장점 | 단점 |
|---|---|---|---|
| SoC boot ROM + eFuse 키 해시 | Qualcomm Snapdragon의 secure boot(PBL이 OEM 공개키 해시를 fuse에서 읽음), i.MX HAB/AHAB, STM32 일부의 RSS/SBSFU 계열 | 칩이 이미 제공, 추가 BOM 없음 | fuse는 한번 태우면 끝. 절차 실수가 곧 벽돌 |
| 잠근 1단 부트로더 | nRF52/nRF53 + NSIB(b0) + MCUboot, 쓰기 보호(ACL/BPROT) | ROM 기능이 없는 MCU에서도 가능 | 부트로더 자체 버그를 고칠 수 없음 |
| 별도 secure element | Microchip ATECC608, NXP SE050, Infineon OPTIGA | 키가 칩 밖으로 안 나옴, 물리 공격 내성 | BOM 비용, I2C 통신, 부트 시간 |
| TrustZone 기반 secure world | Cortex-M33 + TF-M, Cortex-A + TF-A/OP-TEE | 부팅 후에도 키 보호·attestation 서비스 제공 | 복잡도 |

Hark 같은 기기라면 예를 들어 SoC는 벤더 secure boot(ROM → 벤더 부트로더 → OEM 서명된 부트 이미지 → Android Verified Boot), MCU는 MCUboot, 무선 칩은 벤더가 서명한 FW를 칩 ROM이 검증하는 식으로 **칩마다 독립된 체인**이 생길 가능성이 크다. 이 부분은 실제 칩 선택에 따라 달라진다.

### 5.4 Cortex-A 세계와 비교 (Android)

- Qualcomm 계열의 전형적 흐름은 PBL(Primary Boot Loader, ROM) → XBL(eXtensible Boot Loader) → ABL(Android Boot Loader, UEFI 기반) → Linux kernel 이다. 각 단계가 다음 단계의 서명을 검증한다. 세부 이름과 단계는 칩 세대마다 다르다.
- Android는 그 위에 **Android Verified Boot(AVB 2.0)**를 쓴다. `vbmeta` 파티션에 각 파티션의 해시/해시트리(dm-verity)와 **rollback index**가 서명되어 들어간다.
- Android **A/B(seamless) update**: `update_engine`이 비활성 슬롯에 백그라운드로 쓰고, boot control HAL로 슬롯을 전환한다. 새 슬롯에서 부팅이 성공하면 `markBootSuccessful`로 확정하고, 실패가 반복되면 bootloader가 이전 슬롯으로 돌아간다. MCUboot의 test/confirm/revert와 개념이 같다.

> Don 경험과 연결: Apple에서 본 무선 칩 FW는 호스트가 부팅할 때 이미지를 내려보내는 구조일 수 있다. 이런 구조에서는 "칩 ROM이 벤더 서명을 검증하고, 호스트 OS는 그 이미지를 자기 서명된 파일시스템에 싣고 다닌다"는 이중 구조가 된다. 면접에서 콤보칩 FW 다운로드(C06)와 secure boot를 연결해서 말할 수 있으면 좋다.

---

## 6. 서명 검증 흐름을 C로

MCUboot의 실제 코드는 여러 암호 백엔드(mbedTLS, TinyCrypt, PSA, CryptoCell)와 TLV 파싱이 섞여 있어서 처음 읽기엔 복잡하다. 아래는 같은 원리를 가장 단순하게 옮긴 **교육용 부트로더 검증 코드**다. 헤더 형식은 MCUboot를 참고한 자체 형식이고, 암호 연산은 실제 **PSA Crypto API**(Mbed TLS / TF-M이 제공)를 쓴다.

```c
/* verify_image.c -- educational secure-boot verification (not MCUboot's exact format) */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <psa/crypto.h>

#define IMG_MAGIC          0x48524B31u   /* "HRK1" -- our own magic, hypothetical */
#define IMG_HDR_SIZE       0x200u        /* header padded so vector table stays aligned */
#define SIG_LEN            64u           /* ECDSA P-256 raw r||s */
#define PUBKEY_LEN         65u           /* 0x04 || X(32) || Y(32), uncompressed */

struct img_version {
    uint8_t  major;
    uint8_t  minor;
    uint16_t revision;
    uint32_t build;
};

struct img_header {                      /* lives at slot offset 0 */
    uint32_t magic;
    uint32_t hdr_size;                   /* bytes from slot start to body */
    uint32_t body_size;                  /* bytes of code/data */
    uint32_t security_counter;           /* anti-rollback, signed together with body */
    struct img_version ver;
    uint32_t flags;
    uint8_t  pubkey[PUBKEY_LEN];         /* signer's public key, checked against OTP hash */
    uint8_t  _pad[3];
};

/* The signature follows the body: [hdr_size bytes header][body][sig 64 B] */

/* Provided by the board layer (hypothetical): */
extern const uint8_t otp_rotpk_hash[32];          /* SHA-256 of trusted pubkey, in OTP */
extern uint32_t      otp_read_min_security_counter(void);

enum verify_result {
    VERIFY_OK = 0,
    VERIFY_BAD_MAGIC,
    VERIFY_BAD_SIZE,
    VERIFY_UNTRUSTED_KEY,
    VERIFY_ROLLBACK,
    VERIFY_BAD_SIG,
    VERIFY_CRYPTO_ERR,
};

static int ct_memcmp(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff;                                   /* 0 means equal */
}

enum verify_result verify_image(const uint8_t *slot, size_t slot_size)
{
    struct img_header hdr;
    memcpy(&hdr, slot, sizeof(hdr));               /* avoid unaligned access on the flash pointer */

    if (hdr.magic != IMG_MAGIC) {
        return VERIFY_BAD_MAGIC;
    }
    /* Bounds check with overflow-safe arithmetic: attacker controls these fields. */
    if (hdr.hdr_size < sizeof(hdr) || hdr.hdr_size > slot_size ||
        hdr.body_size > slot_size - hdr.hdr_size ||
        SIG_LEN > slot_size - hdr.hdr_size - hdr.body_size) {
        return VERIFY_BAD_SIZE;
    }

    if (psa_crypto_init() != PSA_SUCCESS) {
        return VERIFY_CRYPTO_ERR;
    }

    /* 1) Is the embedded public key the one burned into OTP? */
    uint8_t key_hash[32];
    size_t  hash_len = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, hdr.pubkey, PUBKEY_LEN,
                         key_hash, sizeof(key_hash), &hash_len) != PSA_SUCCESS) {
        return VERIFY_CRYPTO_ERR;
    }
    if (ct_memcmp(key_hash, otp_rotpk_hash, sizeof(key_hash)) != 0) {
        return VERIFY_UNTRUSTED_KEY;
    }

    /* 2) Anti-rollback: counter is inside the signed region, so it cannot be faked. */
    if (hdr.security_counter < otp_read_min_security_counter()) {
        return VERIFY_ROLLBACK;
    }

    /* 3) Hash header + body (everything the signature covers). */
    uint8_t digest[32];
    size_t  signed_len = hdr.hdr_size + hdr.body_size;
    if (psa_hash_compute(PSA_ALG_SHA_256, slot, signed_len,
                         digest, sizeof(digest), &hash_len) != PSA_SUCCESS) {
        return VERIFY_CRYPTO_ERR;
    }

    /* 4) Verify ECDSA P-256 signature over the digest. */
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id = 0;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));

    if (psa_import_key(&attr, hdr.pubkey, PUBKEY_LEN, &key_id) != PSA_SUCCESS) {
        return VERIFY_CRYPTO_ERR;
    }
    const uint8_t *sig = slot + signed_len;
    psa_status_t st = psa_verify_hash(key_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                                      digest, sizeof(digest), sig, SIG_LEN);
    psa_destroy_key(key_id);

    return (st == PSA_SUCCESS) ? VERIFY_OK : VERIFY_BAD_SIG;
}
```

한 줄씩 핵심만 짚는다.

- `memcpy(&hdr, slot, ...)`: flash의 헤더를 구조체 포인터로 바로 캐스팅하지 않는다. 정렬이 보장되지 않을 수 있고, 검증 도중 flash 내용이 바뀌는 TOCTOU(time-of-check to time-of-use) 공격 여지를 줄이려면 한 번 복사한 값만 쓰는 편이 낫다.
- 크기 검사: `hdr_size + body_size > slot_size` 식으로 더하면 32비트 overflow로 검사를 우회할 수 있다. 그래서 **뺄셈으로** 비교한다. 부트로더 취약점의 상당수가 이런 길이 필드 처리에서 나온다.
- 1단계 키 해시 비교: 이미지 안의 공개키를 그대로 믿으면 공격자가 자기 키를 넣고 자기 키로 서명하면 끝이다. **반드시 OTP의 해시와 먼저 비교**한다.
- `ct_memcmp`: 비교 시간이 첫 불일치 위치에 따라 달라지지 않게(constant-time) 한다. 공개키 해시 비교에서는 비밀 정보가 없어 덜 중요하지만, 습관으로 쓴다.
- 2단계 security counter: 헤더에 있지만 3단계 해시 범위(header + body)에 포함되므로 서명으로 보호된다. MCUboot에서 security counter를 protected TLV에 넣는 이유와 같다.
- `psa_verify_hash`에 들어가는 ECDSA 서명은 raw `r||s` 64바이트 형식이다. MCUboot/imgtool은 TLV에 DER 인코딩으로 넣으므로(구현 버전마다 다를 수 있음) 실제로는 변환이 필요하다. 형식 불일치는 "서명이 항상 실패한다" 버그의 흔한 원인이다.
- 검증이 끝난 뒤에도 flash를 다시 읽어 실행하는 사이에 외부 flash라면 내용이 바뀔 수 있다. 외부 flash에서 실행하는 설계에서는 RAM에 복사한 뒤 검증하거나(ram-load), 버스 암호화/인증을 쓴다.

### 6.1 fault injection(글리치) 고려

전압/클럭 글리치로 `if (st == PSA_SUCCESS)` 분기 하나를 건너뛰게 만드는 공격이 실제로 있다. 대응은 (1) 성공 값을 0/1이 아닌 해밍 거리가 큰 상수로 쓰기, (2) 검증을 두 번 하고 결과를 이중 확인, (3) 무작위 지연, (4) 칩의 글리치 감지기 사용이다. MCUboot에는 이런 목적의 fault injection hardening 옵션(`MCUBOOT_FIH_PROFILE_*`)이 있다. 컨슈머 기기에서 어디까지 할지는 위협 모델에 따라 정한다.

---

## 7. Anti-rollback, 이미지 암호화, delta update

### 7.1 Anti-rollback

"버전 비교"와 "보안 카운터"는 다르다.

| 항목 | 버전 기반 downgrade 방지 | 하드웨어 보안 카운터 |
|---|---|---|
| 저장 위치 | 현재 slot0 이미지의 버전 (flash) | OTP/eFuse 또는 보호된 NV 카운터 |
| 공격자가 slot0을 옛 이미지로 직접 써 넣으면 | 막지 못함 (비교 대상 자체가 바뀜) | 막음 (카운터는 되돌릴 수 없음) |
| 개발 편의 | 테스트용 다운그레이드도 막힘 | 보안 수정이 있는 릴리스에서만 카운터를 올림 |
| MCUboot | `MCUBOOT_DOWNGRADE_PREVENTION` (지원 모드에 제약 있음, 문서 확인) | `MCUBOOT_HW_ROLLBACK_PROT` + imgtool `--security-counter` |

설계 원칙: **보안 카운터는 모든 릴리스마다 올리지 않는다.** eFuse는 비트 수가 한정되어 있다(예: 64비트면 64번). 보안 취약점을 고친 릴리스에서만 올린다. 그리고 카운터는 새 이미지가 **confirm된 뒤에** 올린다. test 부팅 중에 올리면 revert할 옛 이미지가 카운터에 막혀 부팅을 못 하는 벽돌 상황이 된다.

### 7.2 이미지 암호화

서명은 무결성과 출처를 보장하지만 내용은 누구나 읽을 수 있다. 펌웨어 리버스 엔지니어링(모델 가중치, 알고리즘 노출)을 막고 싶으면 암호화를 더한다.

- MCUboot 암호화 이미지: 이미지를 AES-CTR로 암호화하고, 그 AES 키를 기기 키로 감싸서(RSA-OAEP, AES-KW, ECIES-P256, ECIES-X25519 중 선택) TLV에 넣는다.
- slot1(특히 외부 flash)에는 **암호화된 채로** 저장되고, 스왑할 때 slot0(내부 flash)에 복호화되어 들어간다. 내부 flash는 readout protection으로 막는다는 전제다.
- 기기 복호화 키가 모든 기기에 동일하면, 한 대에서 키를 뽑는 순간 모든 이미지가 열린다. 그래서 암호화는 "비밀 유지"보다는 "비용을 올리는 장치"로 이해한다.

### 7.3 Delta update

배터리 기기에서는 무선 전송량이 곧 전력이다. 전체 이미지 대신 이전 버전과의 차이만 보내는 것이 delta update다.

```
서버:  old.bin + new.bin --(bsdiff/detools 등)--> patch.bin  (예: 전체의 5~20%)
기기:  slot0(old, 현재 실행중) + patch.bin --(패치 적용)--> slot1(new) --> 전체 해시·서명 검증
```

- **서명은 결과 이미지(new.bin)에 대해** 검증한다. patch 자체도 서명해서 전달 경로를 보호할 수 있지만, 부트로더가 믿는 것은 최종 이미지 서명이다.
- 기기에 있는 old 이미지가 서버가 가정한 old와 **정확히 같아야** 한다. 버전 + 이미지 해시로 확인한다. 불일치하면 full image로 fallback한다.
- 패치 적용에는 RAM 버퍼와 시간이 든다. MCU에서는 스트리밍 방식(작은 윈도우) 알고리즘을 쓴다.
- 압축(LZ4, heatshrink 같은 작은 메모리 압축)만으로도 전송량이 크게 줄 수 있다. delta는 테스트 매트릭스(모든 old → new 조합)가 늘어나므로 운영 비용과 저울질한다.

---

## 8. 전원 차단 안전성과 confirm/revert

### 8.1 전원 차단 시나리오별 분석

| 시점 | 무엇이 남나 | 결과 |
|---|---|---|
| 다운로드 도중 | slot1에 일부만 기록, magic 없음 | 부트로더는 무시하고 slot0 부팅. 앱이 재개 또는 slot1 재erase |
| 다운로드 완료, pending 표시 전 | slot1 완성, magic 없음 | 위와 같음. 다음 체크인 때 VERIFY부터 다시 |
| pending 표시(trailer 쓰기) 도중 | trailer 일부 기록 | magic이 완전히 기록되지 않으면 무효로 보고 무시 (그래서 magic을 마지막에 쓴다) |
| 부트로더 스왑 도중 | swap status에 진행 상태 | 재부팅 시 이어서 스왑 |
| trial run 도중 | slot0 = 새 이미지, image_ok unset | 재부팅하면 revert. 사용자가 그 순간 껐다 켜도 revert되는 부작용 |
| confirm 쓰기 도중 | image_ok 부분 기록 | flash 단일 write의 원자성에 의존. MCUboot는 한 필드를 하나의 write 단위로 둔다 |

추가 방어:

- **배터리 임계치**: 설치 전에 잔량과 충전 여부를 확인한다. 스왑은 수 초~수십 초 걸릴 수 있다(이미지 크기, flash erase 시간에 비례).
- **brown-out 설정**: 전압이 떨어지는 상태에서 flash write를 하면 잘못된 값이 기록될 수 있다. BOR(brown-out reset) 임계치를 flash 쓰기 최소 전압보다 위로 둔다.
- **watchdog**: 부트로더가 스왑 중에 멈추면 watchdog으로 리셋되어 다시 이어서 하게 한다. 부트로더 안에서도 watchdog을 feed해야 한다(긴 erase 루프).

### 8.2 Zephyr에서 confirm 코드

```c
/* main.c -- Zephyr app running under MCUboot */
#include <zephyr/kernel.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define TRIAL_TIMEOUT_MS  (60 * 1000)

static bool self_test_passed(void)
{
    /* Real product: sensor WHO_AM_I reads, radio link up, server check-in, SoC link ping. */
    return true;
}

int main(void)
{
    if (!boot_is_img_confirmed()) {
        LOG_INF("running in TEST mode, starting self-test");
        int64_t deadline = k_uptime_get() + TRIAL_TIMEOUT_MS;

        while (k_uptime_get() < deadline) {
            if (self_test_passed()) {
                int rc = boot_write_img_confirmed();
                LOG_INF("image confirmed (rc=%d)", rc);
                break;
            }
            k_sleep(K_SECONDS(1));
        }
        if (!boot_is_img_confirmed()) {
            LOG_ERR("self-test failed, rebooting to revert");
            sys_reboot(SYS_REBOOT_COLD);        /* MCUboot swaps the old image back */
        }
    }

    /* normal application ... */
    return 0;
}
```

- `boot_is_img_confirmed()`: 현재 실행 중인(slot0) 이미지의 trailer에 image_ok가 set인지 읽는다.
- `boot_write_img_confirmed()`: image_ok를 set한다. 이후 재부팅해도 revert되지 않는다.
- 새 이미지를 받은 쪽에서는 `boot_request_upgrade(BOOT_UPGRADE_TEST)`(또는 `BOOT_UPGRADE_PERMANENT`)를 호출해 slot1 trailer에 magic을 쓴다. MCUboot 내부(bootutil)의 대응 함수가 `boot_set_pending()`이고, 확정은 `boot_set_confirmed()`다.
- 실제 제품에서는 mcumgr(SMP) 서버를 켜면 이 과정을 원격 명령(`image upload`, `image test`, `image confirm`)으로 할 수 있다.

### 8.3 Zephyr에서 slot1에 이미지 쓰기

BLE나 SoC 링크로 청크를 받는 커스텀 OTA 에이전트라면 Zephyr의 `flash_img` API를 쓴다.

```c
#include <zephyr/kernel.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>

static struct flash_img_context img_ctx;

int ota_begin(void)
{
    /* Opens the secondary slot (upload partition). Erase happens as needed,
       or call boot_erase_img_bank() beforehand for a clean start. */
    return flash_img_init(&img_ctx);
}

int ota_write_chunk(const uint8_t *data, size_t len, bool last)
{
    /* Buffers to the flash write-block size; 'last' flushes the tail. */
    return flash_img_buffered_write(&img_ctx, data, len, last);
}

int ota_finish(void)
{
    size_t written = flash_img_bytes_written(&img_ctx);
    (void)written;               /* compare with manifest size, verify hash here */
    return boot_request_upgrade(BOOT_UPGRADE_TEST);
}
```

- 이어받기(resume)를 하려면 `flash_img_bytes_written()` 값을 서버에 알려 주고 그 offset부터 받는다. 단, 버퍼에 남은 미기록 바이트는 전원 차단 시 사라지므로 write-block 경계에서만 resume offset을 확정한다.
- 앱 단계에서 해시를 한 번 확인하고(`flash_img_check()`로 SHA-256 비교 가능, `CONFIG_IMG_ENABLE_IMAGE_CHECK`), 부트로더가 서명을 다시 검증한다. 앱의 검사는 "빨리 실패하기" 용도이고, 보안 결정은 부트로더가 한다.

---

## 9. 다중 프로세서 업데이트와 단계적 롤아웃

### 9.1 한 기기, 여러 이미지

| 대상 | 전형적 업데이트 방법 | 주의점 |
|---|---|---|
| Application SoC (Linux/Android) | A/B 파티션 + update_engine 또는 SWUpdate/RAUC/Mender 같은 도구 | 부트로더 체인·AVB rollback index |
| Always-on MCU | MCUboot slot1에 SoC가 UART/SPI로 전송 (SMP 프로토콜 또는 자체 프로토콜) | MCU가 SoC를 깨우는 역할이면 MCU 업데이트 중 wake 기능이 멈춤 |
| 무선 콤보칩 (호스트 로드형) | SoC 파일시스템의 FW 파일 교체, 다음 부팅 때 로드 | SoC 업데이트와 원자적으로 묶임 |
| 무선 칩 (자체 flash형), 모뎀 | 벤더 FOTA 도구 / 벤더 서명 필요 | OEM이 서명 못 하는 경우가 많음 |
| 멀티코어 MCU (예: nRF5340 app core + network core) | MCUboot 다중 이미지(`MCUBOOT_IMAGE_NUMBER` > 1), 이미지 간 dependency TLV | 한쪽만 업데이트되면 IPC 불일치 |

**번들(bundle)과 호환성**: 서버는 칩별 이미지를 하나의 서명된 매니페스트로 묶어서 "SoC v3.2는 MCU ≥ v1.7 필요" 같은 의존성을 표현한다. IETF SUIT(Software Updates for IoT, 아키텍처 RFC 9019, 정보 모델 RFC 9124)가 이런 매니페스트의 표준화 시도이고, Nordic은 일부 신규 칩(nRF54H 계열)에서 SUIT를 쓴다.

**업데이트 순서 설계 예**:

```
1. SoC 가 번들 전체 다운로드 + 매니페스트 서명 검증
2. MCU 이미지 먼저 전송 -> MCU slot1 (MCU 는 여전히 옛 FW 로 동작)
3. SoC 비활성 슬롯에 SoC 이미지 기록
4. 사용자 idle 시점: MCU 리부트(test) -> MCU self-test -> confirm
5. SoC 슬롯 전환 후 리부트 -> SoC self-test (MCU 와 통신 확인 포함) -> markBootSuccessful
6. 어느 단계든 실패 -> 이미 넘어간 쪽을 되돌리고(가능하면) 서버에 보고
   (새 SoC 가 옛 MCU 와도, 옛 SoC 가 새 MCU 와도 통신 가능하도록 프로토콜 하위 호환 유지가 핵심)
```

마지막 줄이 가장 중요하다. **완벽한 원자적 멀티칩 업데이트는 어렵다.** 그래서 인터페이스 프로토콜에 버전 협상(handshake)을 넣고, N과 N-1 버전 간 호환을 유지하는 규칙이 실무에서 더 효과적이다.

### 9.2 단계적 롤아웃

```
day 0   내부 dogfood (직원 기기)                 -> 크래시율, revert율, 배터리 소모 확인
day 3   1%  (무작위 cohort, 지역/하드웨어 리비전 분산)
day 5   5%
day 8   25%
day 12  100%
        어느 단계든 지표 악화 -> 롤아웃 중단(kill switch), 필요하면 수정 버전 배포
```

- 지표: 업데이트 성공률, revert 비율, 부팅 후 크래시/리셋 원인(reset reason 레지스터 로그), 배터리 소모 변화, 무선 연결 실패율
- 하드웨어 리비전(EVT/DVT 샘플 vs MP)별로 cohort를 나눈다. 특정 flash 벤더나 보드 리비전에서만 나는 문제가 있다.
- 서버는 기기가 보고한 현재 버전과 하드웨어 ID로 적격성을 판단한다. 기기는 서버를 믿지 않고 서명만 믿는다.

> Don 경험과 연결: SSD 양산 FW에서 telemetry와 error reporting을 설계한 경험은 OTA fleet의 observability(업데이트 결과, reset reason, revert 이유 보고)와 그대로 연결된다. 면접에서 "OTA infrastructure"를 기기 쪽 부트로더만이 아니라 **측정 가능한 배포 파이프라인**으로 설명하면 강하다.

---

## 10. 디버그 포트 잠금과 TrustZone-M

### 10.1 디버그 포트 잠금

secure boot를 아무리 잘 만들어도 SWD/JTAG가 열려 있으면 공격자가 디버거로 flash를 읽고, 검증 분기를 건너뛰고, 키를 덤프한다. 출하 전 잠금은 필수다.

| 벤더/칩 | 메커니즘 | 비고 |
|---|---|---|
| Nordic nRF52/nRF53 | UICR의 APPROTECT (access port protection) | 해제(`nrfjprog --recover` 등)하면 전체 flash가 지워진다. 칩 리비전별 동작 차이가 있으니 해당 errata/문서 확인 |
| STM32 | RDP(Readout Protection) Level 0/1/2 | Level 2는 영구적(되돌릴 수 없음) |
| Qualcomm 등 AP SoC | eFuse로 JTAG 비활성화, 인증된 디버그 | 벤더 문서와 NDA 필요 |
| Cortex-M33 (Armv8-M) 일반 | secure/non-secure 디버그 권한 분리 (벤더가 제어 신호 제공) | TF-M 환경에서 NS 디버그만 허용 가능 |

**인증된 디버그(authenticated debug)**: RMA(반품) 분석을 위해 영구 잠금 대신, 서명된 토큰(기기 고유 ID에 대해 서명)을 제시하면 디버그를 여는 방식이 있다. 벤더마다 구현이 다르다. 이 부분은 C09(출하 전 잠금)와 연결된다.

### 10.2 TrustZone-M 기초

Armv8-M(Cortex-M23/M33/M35P/M55/M85)의 보안 확장이다. Cortex-M4(nRF52840)에는 **없다**. nRF5340, nRF9160, nRF54L, STM32U5/L5 등이 Cortex-M33이다.

```
            Secure world                          Non-secure world
 +----------------------------------+    +----------------------------------+
 | TF-M (Secure Partition Manager)  |    | Zephyr / FreeRTOS + App          |
 |  - Crypto service (keys stay here)|    |  - BLE, sensors, wake word       |
 |  - Attestation (signed token)    |    |                                  |
 |  - Protected/Internal storage    |    |   psa_sign_hash(...)  ---------+ |
 |  - Firmware update service       |    |                                | |
 +----------------+-----------------+    +--------------------------------|-+
                  ^   NSC (Non-Secure Callable) veneer: SG 명령으로만 진입  |
                  +---------------------------------------------------------+
 메모리 속성: SAU(Security Attribution Unit) + IDAU(칩 고정) 가 주소마다 S / NSC / NS 지정
```

- 주소 공간을 Secure, Non-secure-callable(NSC), Non-secure로 나눈다. NS 코드는 NSC 영역의 `SG`(Secure Gateway) 명령이 있는 진입점으로만 secure 함수를 부를 수 있다.
- secure 쪽이 NS 함수로 돌아가거나 호출할 때 `BXNS`/`BLXNS`를 쓰고, 컴파일러는 `cmse_nonsecure_entry` 속성(`-mcmse`)으로 veneer를 만든다.
- 의미: 앱(NS)에 버그가 있어 원격 코드 실행을 당해도, secure world의 키는 꺼낼 수 없다. 기기 인증 키, OTA 서명 검증 로직을 secure 쪽에 둔다.
- Cortex-A는 TrustZone이 EL3(TF-A secure monitor) + Secure EL1(OP-TEE 같은 TEE)로 구성된다. 개념은 같고 구현은 다르다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 앱을 MCUboot 헤더 크기 고려 없이 링크 | MCUboot가 점프한 직후 HardFault | 벡터 테이블이 slot0 + hdr_size에 없음 | `CONFIG_BOOTLOADER_MCUBOOT=y`, imgtool `--header-size`와 `CONFIG_ROM_START_OFFSET` 일치 |
| 서명 안 한 `zephyr.bin`을 업로드 | 업로드는 성공, 재부팅 후 옛 버전 그대로 | 부트로더가 검증 실패로 slot1 무시 | `zephyr.signed.bin`(또는 `.hex`) 업로드, MCUboot 로그 확인 |
| confirm 코드를 빠뜨림 | 업데이트 직후엔 새 버전, 한 번 더 재부팅하면 옛 버전 | test 스왑 후 image_ok 미설정 → revert | self-test 후 `boot_write_img_confirmed()` 호출 |
| 부팅 즉시 무조건 confirm | 네트워크가 깨진 버전이 영구 설치되어 다시 업데이트 불가 | revert 기회 포기 | 통신 경로 확인 후 confirm |
| 샘플 키로 출하 | 누구나 기기용 FW를 서명 가능 | `root-ec-p256.pem` 같은 공개 키 사용 | 빌드에서 샘플 키 사용 시 실패하도록 CI 검사, 운영 키는 HSM |
| 보안 카운터를 test 부팅에서 올림 | revert가 필요한데 옛 이미지가 rollback으로 거부되어 벽돌 | 카운터 갱신 시점 오류 | confirm 이후에만 카운터 갱신 |
| slot 크기 대비 이미지 과대 | imgtool 서명 실패 또는 스왑 불가 | swap-using-move는 sector 하나, trailer 공간이 추가로 필요 | `--slot-size`, `--pad`와 파티션 재계산, 크기 CI 검사 |
| 외부 flash의 slot1 쓰기가 느리다고 버퍼링 과다 | 전원 차단 후 resume 시 이미지 손상 | 미기록 버퍼 바이트를 기록된 것으로 보고 | write-block 경계에서만 진행 offset 확정, 최종 해시 검증 |
| 출하 기기 디버그 포트 열림 | 보안 감사에서 flash 덤프 성공 | APPROTECT/RDP 미설정 | factory 마지막 단계에서 잠금 + 잠금 상태 검증 테스트 |

---

## 12. 면접에서 이렇게 말한다

**Q.** How would you design a brick-proof OTA for a battery-powered device?

**A.** 핵심은 세 가지다. 실행 중인 이미지를 절대 먼저 지우지 않는다(두 번째 슬롯에 받는다). 설치는 진행 상태를 flash에 기록하며 재개 가능하게 한다. 새 이미지는 test로 부팅해서 self-test를 통과해야 confirm하고, 아니면 자동으로 되돌린다. 여기에 배터리 임계치 검사와 부트로더의 서명 검증을 더한다.

> "I never touch the running image. The new image goes to a secondary slot, is hash- and signature-checked, and then the bootloader swaps it in with progress recorded in flash, so a power loss just resumes the swap. The new image boots in test mode and must pass a self-test — at minimum proving it can still receive the next update — before it confirms itself. If it resets or times out without confirming, the bootloader reverts to the previous image. I also gate installation on battery level and use a watchdog in both the bootloader and the app."

**Q.** Explain secure boot and chain of trust.

**A.** 불변의 root of trust(ROM + OTP의 공개키 해시)에서 시작해 각 단계가 다음 단계의 서명을 검증한 뒤에만 넘긴다. 기기에는 공개키(해시)만 있고 비밀키는 HSM에 있다. 한 단계라도 검증 없이 넘어가면 그 이후는 믿을 수 없다.

> "Secure boot starts from something that cannot be changed — the boot ROM and a public-key hash burned into OTP. Each stage verifies the next stage's signature before jumping to it: ROM verifies the first bootloader, the bootloader verifies the application. Only the public key, or its hash, lives on the device; the private key stays in an HSM. If any link skips verification, everything after it is untrusted."

**Q.** What's the difference between swap-using-scratch and swap-using-move in MCUboot?

**A.** 둘 다 slot0과 slot1을 교환해서 revert를 가능하게 한다. scratch는 별도 scratch 영역을 거쳐 교환하므로 scratch sector가 가장 많이 닳는다. move는 slot0에 sector 하나를 여유로 두고 내용을 한 칸씩 민 다음 교환해서 scratch가 필요 없고 마모가 고르다.

> "Both swap the primary and secondary slots so the old image is preserved for revert. Swap-using-scratch copies sector by sector through a dedicated scratch area, which concentrates wear on that area. Swap-using-move reserves one extra sector in the primary slot, shifts the primary image up by one sector, then swaps, so there's no scratch partition and wear is spread evenly. Overwrite-only is simpler and faster but loses the ability to revert."

**Q.** How do you prevent rollback attacks?

**A.** 버전 비교만으로는 부족하다. 서명된 영역 안에 보안 카운터를 넣고, 그 최솟값을 OTP나 되돌릴 수 없는 카운터에 저장한다. 카운터는 보안 수정 릴리스에서만, 그리고 새 이미지가 confirm된 뒤에만 올린다.

> "I put a security counter inside the signed part of the image and keep the minimum accepted value in a monotonic store like eFuses. The bootloader rejects any image whose counter is lower. Because fuses are limited, I bump the counter only for security fixes, and only after the new image has confirmed itself — otherwise a needed revert would be blocked and we'd brick the device."

**Q.** Our device has an Android SoC, an MCU, and a Wi-Fi/BT chip. How do you update all of them?

**A.** SoC가 서명된 번들(칩별 이미지 + 의존성 매니페스트)을 받고, MCU 이미지는 MCUboot slot1로 전송, SoC는 A/B 비활성 슬롯에 기록, 무선 FW는 SoC 파일시스템에 포함하거나 벤더 FOTA로 처리한다. 원자성은 완벽하게 만들기 어려우므로 칩 간 프로토콜에 버전 협상과 N/N-1 호환을 넣는 것이 핵심이다.

> "The SoC downloads one signed bundle with per-chip images and a manifest that states dependencies. The MCU image is streamed to the MCU's secondary slot, the SoC image to its inactive A/B slot, and host-loaded radio firmware rides along in the SoC's file system. Each chip then does its own test boot and confirm. Since true multi-chip atomicity is hard, I design the inter-chip protocol with version negotiation and keep N and N-minus-one compatible, so a partial update never leaves the device unable to talk to itself."

**Q.** Why ECDSA P-256 or Ed25519 instead of a CRC or HMAC?

**A.** CRC는 우연한 오류만 잡는다. HMAC은 기기에 비밀키가 있어야 하므로 기기 하나가 털리면 위조 가능하다. 서명은 기기에 공개키만 있으면 되므로 기기를 완전히 분석해도 위조할 수 없다. P-256은 HW 가속이 흔하고, Ed25519는 소프트웨어로 빠르고 결정적이다.

> "A CRC only catches accidental corruption. An HMAC needs the secret key on every device, so extracting it from one unit lets you forge images for all of them. With a signature, the device only holds a public key, so even full reverse-engineering doesn't let you sign. I'd pick P-256 when the MCU has a crypto accelerator for it, and Ed25519 when verification runs in software, because it's fast, compact, and deterministic."

**Q.** What must be done on the factory line to make secure boot real?

**A.** 운영 공개키 해시를 OTP에 기록, 부트로더 쓰기 보호, 보안 카운터 초기화, secure boot 활성 fuse, 마지막으로 디버그 포트 잠금. 그리고 잠금이 실제로 되었는지 테스트 스테이션이 확인한다. 순서가 틀리면 기기를 벽돌로 만들 수 있으니 fuse 작업은 검증된 순서로 한 번에 한다.

> "Secure boot is only as good as the provisioning. On the line we program the production key hash into OTP, write-protect the bootloader, initialize the anti-rollback counter, enable the secure-boot fuse, and finally lock the debug port — and a test station verifies each lock actually took effect. Fuse steps are irreversible, so they run as one validated sequence, late in the flow, after all functional tests have passed."

---

## 13. 직접 해보기

### 13.1 실습 1 — nRF52840 DK에 MCUboot + smp_svr 올리기 (시리얼 DFU)

준비: nRF52840 DK, Zephyr 개발 환경(west, Zephyr SDK), `mcumgr` CLI(Go, `go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest`) 또는 Python `smpmgr`.

```sh
# 1) MCUboot + SMP server 샘플을 sysbuild 로 함께 빌드
west build -b nrf52840dk/nrf52840 --sysbuild -d build_v1 \
    zephyr/samples/subsys/mgmt/mcumgr/smp_svr -- \
    -DEXTRA_CONF_FILE=overlay-serial.conf \
    -DSB_CONFIG_BOOTLOADER_MCUBOOT=y
west flash -d build_v1

# 2) 버전만 바꿔서 두 번째 이미지 빌드 (prj.conf 또는 명령행에서 버전 지정)
#    CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="1.1.0+0"  (Zephyr 버전에 따라 VERSION 파일 방식 사용)
west build -b nrf52840dk/nrf52840 --sysbuild -d build_v2 \
    zephyr/samples/subsys/mgmt/mcumgr/smp_svr -- \
    -DEXTRA_CONF_FILE=overlay-serial.conf

# 3) 연결 설정 후 업로드 -> test -> reset -> confirm
mcumgr conn add dk type="serial" connstring="dev=/dev/tty.usbmodem0010500000001,baud=115200"
mcumgr -c dk image list
mcumgr -c dk image upload build_v2/smp_svr/zephyr/zephyr.signed.bin
mcumgr -c dk image list              # slot1 의 hash 확인
mcumgr -c dk image test <slot1-hash>
mcumgr -c dk reset
mcumgr -c dk image list              # 새 이미지가 active, confirmed=false
mcumgr -c dk image confirm           # 또는 reset 해서 revert 되는 것도 관찰
```

관찰할 것: 시리얼 콘솔에서 MCUboot 로그(`Swap type: test`, `Starting swap using move algorithm` 류의 메시지, 버전에 따라 문구 다름), confirm 전후 재부팅 결과 차이. 경로(`build_v2/smp_svr/zephyr/...`)와 sysbuild 옵션 이름은 Zephyr 버전마다 조금씩 다르므로 빌드 출력의 파일 위치를 확인한다.

### 13.2 실습 2 — imgtool로 직접 키 만들고 서명·검증

```sh
pip install imgtool            # 또는 mcuboot/scripts 의 imgtool.py
imgtool keygen -k my-p256.pem -t ecdsa-p256
imgtool getpub -k my-p256.pem              # C 배열 형태 공개키 출력 (부트로더에 넣는 값)

imgtool sign --key my-p256.pem \
    --header-size 0x200 --pad-header \
    --align 4 \
    --version 1.2.3+4 \
    --security-counter 2 \
    --slot-size 0x76000 \
    zephyr.bin app.signed.bin

imgtool dumpinfo app.signed.bin            # header, TLV, trailer 를 사람이 읽을 수 있게 출력
imgtool verify -k my-p256.pem app.signed.bin
```

- 한 바이트를 hex 편집기로 바꾼 뒤 `imgtool verify`가 실패하는 것을 확인한다.
- MCUboot 빌드에 이 키를 쓰려면 MCUboot 이미지의 `CONFIG_BOOT_SIGNATURE_KEY_FILE`, 앱 쪽에서는 `CONFIG_MCUBOOT_SIGNATURE_KEY_FILE`(sysbuild에서는 `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`)을 지정한다. 버전마다 이름이 바뀌어 왔으니 사용하는 Zephyr/NCS 버전 문서를 확인한다.

### 13.3 실습 3 — 전원 차단 중 스왑 재개 관찰

1. 실습 1 상태에서 `image test` 후 `reset`을 보내고, MCUboot 로그가 스왑을 시작하는 순간 DK의 전원 스위치를 끈다.
2. 다시 켜고 MCUboot가 스왑을 이어서 하는지 로그를 본다.
3. 새 이미지가 뜬 뒤 confirm하지 않고 reset → revert되는 것도 확인한다.

### 13.4 실습 4 — confirm 로직 직접 넣기

`smp_svr` 샘플 `main()`에 8.2절의 self-test + `boot_write_img_confirmed()` 코드를 넣고, self-test가 일부러 실패하게 만든 버전을 올려 자동 revert를 재현한다. (`CONFIG_MCUBOOT_BOOTUTIL_LIB=y`가 필요할 수 있다. `boot_write_img_confirmed`는 `zephyr/dfu/mcuboot.h`.)

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| OTA / FOTA | Over-the-air (firmware) update | 무선으로 FW를 교체하는 전체 과정 |
| DFU | Device Firmware Update | 유선/무선 구분 없이 FW 업데이트를 부르는 말 (USB DFU 클래스도 있음) |
| MCUboot | Cortex-M용 오픈소스 보안 부트로더 | slot 관리, 서명 검증, swap/revert |
| primary / secondary slot | slot0 / slot1 | 실행 위치 / 새 이미지 도착 위치 |
| image trailer | slot 끝의 상태 영역 | magic, image_ok, copy_done, swap status |
| TLV | Type-Length-Value | 이미지 뒤에 붙는 해시·서명·카운터 메타데이터 |
| test / permanent / revert | MCUboot 스왑 종류 | 시험 설치 / 영구 설치 / 되돌리기 |
| imgtool | MCUboot 이미지 서명 도구 | keygen, sign, verify, dumpinfo |
| SMP / mcumgr | Simple Management Protocol | Zephyr의 기기 관리 프로토콜 (이미지 업로드, 리셋, 통계) |
| RoT | Root of Trust | 검증 없이 믿는 불변의 출발점 (ROM + OTP 키) |
| chain of trust | 신뢰 사슬 | 각 부트 단계가 다음 단계를 검증 |
| OTP / eFuse | One-Time Programmable | 한 번만 쓸 수 있는 비휘발 비트 |
| ROTPK | Root of Trust Public Key | 부트 체인의 최상위 공개키 (보통 해시만 OTP에) |
| HSM / KMS | Hardware Security Module / Key Management Service | 서명 비밀키를 보관하고 서명만 수행 |
| ECDSA P-256 | NIST 곡선 타원곡선 서명 | 가장 흔한 FW 서명 알고리즘 |
| Ed25519 | Edwards 곡선 서명 | 결정적, 소프트웨어로 빠름 |
| anti-rollback | 다운그레이드 방지 | 보안 카운터로 옛 취약 버전 거부 |
| delta update | 차분 업데이트 | 이전 버전과의 차이만 전송 |
| XIP | eXecute In Place | flash에서 직접 명령어 실행 |
| AVB | Android Verified Boot | vbmeta, dm-verity, rollback index |
| SUIT | Software Updates for IoT (IETF) | 업데이트 매니페스트 표준 |
| TF-M / TF-A | Trusted Firmware-M / -A | Armv8-M / Armv8-A 보안 월드 참조 구현 |
| SAU / IDAU | Security/Implementation Defined Attribution Unit | TrustZone-M 주소별 보안 속성 지정 |
| APPROTECT / RDP | 디버그·readout 보호 | Nordic / ST의 디버그 잠금 메커니즘 |
| FIH | Fault Injection Hardening | 글리치 공격 대비 코드 기법 |

---

## 15. 요약 & 체크리스트

OTA는 "비활성 위치에 받고, 검증하고, 재개 가능하게 설치하고, 시험 부팅 후 스스로 확인해야 확정"하는 상태 머신이다. secure boot는 ROM과 OTP 키 해시라는 불변의 출발점에서 각 단계가 다음 단계의 서명을 검증하는 사슬이다. MCUboot는 이 둘을 Cortex-M에서 구현한 표준 도구이며, slot0/slot1, 스왑 방식(scratch/move/overwrite/direct-xip), image trailer(magic, image_ok, copy_done)를 이해하면 대부분의 설계 질문에 답할 수 있다. 실무에서 진짜 어려운 부분은 키 관리, 보안 카운터 갱신 시점, 멀티칩 호환성, 롤아웃 관측성, 그리고 factory에서의 키·fuse·디버그 잠금 절차다.

- [ ] MCUboot flash 레이아웃(boot, slot0, slot1, scratch/storage)과 이미지 내부(header, body, TLV, trailer)를 화이트보드에 그릴 수 있다
- [ ] swap-using-scratch와 swap-using-move의 단계를 sector 그림으로 설명하고, overwrite-only·direct-xip와 비교할 수 있다
- [ ] test → confirm → revert 흐름과 관련 Zephyr API(`boot_request_upgrade`, `boot_is_img_confirmed`, `boot_write_img_confirmed`)를 말할 수 있다
- [ ] ROM → 부트로더 → 앱 chain of trust와 "OTP에 공개키 해시를 두는 이유"를 설명할 수 있다
- [ ] CRC, 해시, HMAC, 서명의 차이와 P-256 vs Ed25519 선택 기준을 말할 수 있다
- [ ] 보안 카운터를 언제 올려야 하는지(보안 수정 시, confirm 이후) 이유와 함께 말할 수 있다
- [ ] 전원 차단 시점별로 무엇이 남고 어떻게 복구되는지 표로 설명할 수 있다
- [ ] SoC + MCU + 무선 칩 기기의 업데이트 순서와 N/N-1 호환 원칙을 설명할 수 있다
- [ ] 단계적 롤아웃의 지표와 kill switch를 설명할 수 있다
- [ ] 출하 전 잠금(APPROTECT/RDP, fuse, 키 해시)과 TrustZone-M의 역할을 한 문장씩 말할 수 있다

---

## 참고 자료

- [MCUboot 문서 (design, image format, swap 방식)](https://docs.mcuboot.com/)
- [MCUboot design.md (GitHub)](https://github.com/mcu-tools/mcuboot/blob/main/docs/design.md)
- [MCUboot imgtool 문서](https://docs.mcuboot.com/imgtool.html)
- [Zephyr: MCUboot API (zephyr/dfu/mcuboot.h)](https://docs.zephyrproject.org/latest/services/device_mgmt/dfu.html)
- [Zephyr: Device Management / MCUmgr](https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html)
- [Zephyr: smp_svr 샘플](https://docs.zephyrproject.org/latest/samples/subsys/mgmt/mcumgr/smp_svr/README.html)
- [Zephyr: sysbuild](https://docs.zephyrproject.org/latest/build/sysbuild/index.html)
- [PSA Crypto API 명세](https://arm-software.github.io/psa-api/crypto/)
- [Trusted Firmware-M 문서](https://trustedfirmware-m.readthedocs.io/)
- [Arm: TrustZone for Armv8-M](https://developer.arm.com/documentation/100690/latest/)
- [Android: A/B (Seamless) System Updates](https://source.android.com/docs/core/ota/ab)
- [Android Verified Boot](https://source.android.com/docs/security/features/verifiedboot)
- [IETF RFC 9019 — SUIT Architecture](https://www.rfc-editor.org/rfc/rfc9019)
- [IETF RFC 9124 — SUIT Information Model](https://www.rfc-editor.org/rfc/rfc9124)
- [RFC 6979 — Deterministic ECDSA](https://www.rfc-editor.org/rfc/rfc6979)
- [nRF Connect SDK: Bootloaders and DFU](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/app_dev/bootloaders_dfu/index.html)
