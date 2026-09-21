# S04. OTA · Secure Boot · Factory 면접 드릴 — 벽돌 안 되는 업데이트, 믿을 수 있는 부팅, 양산 라인 펌웨어

> **목표**: OTA(A/B, MCUboot, 전원 차단 안전성, rollback, 다중 프로세서, 단계적 롤아웃), secure boot(서명, root of trust, anti-rollback, 디버그 잠금), factory test·calibration·provisioning, EVT/DVT/PVT 질문에 30초 답과 영어 답을 바로 낼 수 있게 한다. 마지막에 "SoC + always-on MCU + Wi-Fi/BT 콤보칩 기기의 OTA 설계"를 45분 화이트보드로 연습한다.
> **선행**: C07(OTA·secure boot), C09(factory test·calibration), C01(부트 시퀀스·메모리 맵)
> **사용법**: 질문을 먼저 소리 내어 답해 보고, 그다음 모범 답안을 읽는다.

---

## 0. 이 노트를 쓰는 법

JD 문장은 세 개다. "Build and maintain OTA update infrastructure for reliable field updates", "Develop factory test and calibration firmware for manufacturing", 그리고 우대 사항 "Familiarity with secure boot, firmware signing, or hardware root of trust" + "shipping consumer electronics through EVT/DVT/PVT milestones".

Don의 위치를 먼저 정리한다.

| 주제 | Don이 이미 가진 것 | 이 노트에서 채울 것 |
|---|---|---|
| OTA | SSD FW의 firmware download/commit 흐름(NVMe Firmware Image Download / Firmware Commit 명령 개념), 양산 FW 에러 처리 | A/B 슬롯, MCUboot swap 방식, confirm/revert, 다중 칩 오케스트레이션 |
| Secure boot | 엔터프라이즈 SSD에서 흔한 서명 검증 흐름(본인 경험 여부는 Don이 확인할 것) | chain of trust, OTP 키 해시, anti-rollback counter, 디버그 잠금 |
| Factory | Apple factory test-node 아키텍처 설계, bring-up → NPI → MP | 컨슈머 기기 cal 항목(마이크·IMU·배터리), provisioning, MES 추적성 |
| EVT/DVT/PVT | Apple NPI 사이클 | 단계별로 펌웨어가 무엇을 준비해야 하는지 말로 정리 |

> Hark 기기 구조(Qualcomm 계열 SoC + Ambiq 계열 always-on MCU + 다수 라디오)는 채용 공고에서 나온 **추정**이다. 답할 때는 "예를 들어 SoC와 always-on MCU가 분리된 기기라면"처럼 가정으로 말한다.

---

## 1. OTA 기초와 A/B

### Q01. Walk me through how you would design an OTA update that can never brick the device.

**왜 묻나**: JD "reliable field updates". 면접관은 "전원이 어느 순간 끊겨도 부팅 가능한 이미지가 항상 하나는 있다"는 불변식(invariant)을 말하는지 본다.

**30초 답변**: 핵심 불변식은 "flash 어디에도 부팅 가능한, 검증된 이미지가 최소 하나 항상 존재한다"이다. 그래서 새 이미지는 실행 중이지 않은 슬롯(B)에 받는다. 다운로드 완료 후 해시와 서명을 검증하고, 부트로더에 "다음 부팅에 B를 **시험(test)**으로 올려라"라고 표시한다. 새 이미지는 부팅 후 self-test(연결, 주변장치, 워치독 통과)를 끝내야만 스스로 **confirm**한다. confirm 전에 리셋되면 부트로더가 A로 되돌린다. 모든 상태 전환은 전원 차단 후 재개 가능한 순서로 flash에 기록한다.

**English answer**: The invariant I design around is that there is always at least one verified, bootable image in flash, no matter when power is cut. The new image is written to the inactive slot, then hashed and signature-checked before anything else changes. The bootloader boots it in a trial state, and the application only marks itself confirmed after a health check passes. If it crashes, hangs, or resets before confirming, the bootloader reverts to the previous image. Every state change in the bootloader is recorded in flash so it can resume safely after a power loss.

**꼬리질문**:
- "health check에 무엇을 넣나?" → 부팅 완료, 핵심 task 기동, 워치독이 일정 시간 동안 kick됨, 서버 또는 SoC와 통신 성공. 너무 약하면 나쁜 이미지를 confirm하고, 너무 강하면(예: 클라우드 필수) 네트워크 장애 때 멀쩡한 이미지를 되돌린다.
- "부트로더 자체는 어떻게 업데이트하나?" → 원칙적으로 불변(immutable)로 두고 업데이트하지 않는다. 꼭 필요하면 1단계 ROM → 2단계 부트로더 구조에서 2단계 부트로더를 A/B로 둔다.

**Don 스토리 연결**: SSD FW download/commit에서 "commit 전에 리셋되면 이전 FW로 부팅"하는 원리를 비교로 든다(구체 구현 경험은 Don이 확인 후 말할 것).

### Q02. Compare A/B (dual-slot) updates with single-slot plus recovery. When would you pick each?

**왜 묻나**: flash 비용 vs 안전성 trade-off를 아는지.

**30초 답변**: A/B는 슬롯 두 개라 flash가 두 배 들지만 다운로드 중에도 기기가 정상 동작하고, 롤백이 즉시 된다. single-slot + recovery는 작은 recovery 이미지만 따로 두고 본 이미지를 제자리에서 덮어쓴다. flash는 아끼지만 업데이트 중에는 기능이 멈추고, 실패 시 recovery 모드에서 다시 받아야 한다. MCU 내부 flash가 작으면 외부 SPI NOR에 다운로드 슬롯을 두는 방식도 흔하다.

| 방식 | flash 비용 | 업데이트 중 동작 | 롤백 | 흔한 곳 |
|---|---|---|---|---|
| A/B (두 실행 슬롯) | 이미지 x2 | 계속 동작 | 즉시 | Android SoC, MCUboot direct-xip |
| primary + secondary(swap) | 이미지 x2(+scratch) | 계속 동작 | swap-back | MCUboot 기본 |
| single-slot + recovery | 이미지 + 작은 recovery | 중단 | recovery에서 재다운로드 | flash가 빡빡한 MCU |
| overwrite-only | 이미지 x2 | 계속 동작 | 불가 | 롤백보다 단순함 우선 |

**English answer**: A/B costs twice the flash, but the device keeps running during download and rollback is instant. Single-slot plus recovery saves flash, but the device is unavailable during the update and a failed update means re-downloading from a recovery image. For a consumer wearable I would pay for A/B on anything that must stay functional, and if the MCU's internal flash is tight I would put the download slot in external QSPI NOR.

**꼬리질문**:
- "외부 flash에 둔 이미지는 믿을 수 있나?" → 외부 flash는 공격자가 바꿀 수 있으므로, 부트로더가 내부로 복사하기 전과 후에 서명을 검증한다. 필요하면 이미지 암호화도 한다.

### Q03. Explain how MCUboot's swap modes work and what the image trailer is for.

**왜 묻나**: JD의 OTA + MCU 조합에서 가장 흔한 오픈소스 부트로더. 이름만이 아니라 동작 원리를 아는지.

**30초 답변**: MCUboot는 primary slot(실행)과 secondary slot(다운로드)을 쓴다. 업그레이드 방식은 여러 개다. overwrite-only는 secondary를 primary에 덮어쓰고 롤백이 없다. swap-using-scratch는 scratch 영역을 거쳐 두 슬롯을 sector 단위로 맞바꾼다. swap-using-move는 primary를 한 sector씩 밀어 두고 맞바꿔 scratch가 필요 없다. direct-xip은 두 슬롯 중 버전이 높은 쪽을 그 자리에서 실행하고, ram-load는 RAM으로 복사해 실행한다. 각 슬롯 끝의 **image trailer**에 magic, swap 진행 상태, `copy_done`, `image_ok` 플래그를 기록해서, 전원이 끊겨도 다음 부팅에서 swap을 이어서 한다.

```
 primary slot (실행)                  secondary slot (다운로드)
+------------------+               +------------------+
| image header     |               | image header     |  <- magic, load addr, hdr size, img size, version
| code / data      |               | code / data      |
|                  |               |                  |
| TLV area         |               | TLV area         |  <- SHA-256, signature, (key hash), (security counter)
| ...pad...        |               | ...pad...        |
| image trailer    |               | image trailer    |  <- swap status, swap_size, copy_done, image_ok, magic
+------------------+               +------------------+
```

**English answer**: MCUboot uses a primary slot that runs and a secondary slot that receives the new image. In swap mode it exchanges the two slots sector by sector, either through a scratch area or by shifting the primary image by one sector, which is swap-using-move. Progress is written to the image trailer at the end of the slot, so if power is lost mid-swap the bootloader resumes where it stopped. The trailer also holds the image_ok flag: a test upgrade runs once, and if the application never sets image_ok, the next boot swaps back.

**꼬리질문**:
- "Zephyr 앱에서 confirm은 어떻게 하나?" → `boot_write_img_confirmed()` 호출(`zephyr/dfu/mcuboot.h`). 업그레이드 요청은 `boot_request_upgrade(BOOT_UPGRADE_TEST)` 또는 `BOOT_UPGRADE_PERMANENT`.
- "swap-using-scratch의 단점?" → scratch sector에 쓰기가 집중되어 flash wear가 크다. 그래서 swap-using-move를 선호하는 경우가 많다.

```c
/* Zephyr + MCUboot: 새 이미지로 부팅한 뒤 health check 통과 시 confirm */
#include <zephyr/kernel.h>
#include <zephyr/dfu/mcuboot.h>
#include <stdbool.h>

extern bool app_health_check(void);   /* 제품별 구현: 센서, IPC, 워치독 등 */

void ota_confirm_if_healthy(void)
{
    if (boot_is_img_confirmed()) {
        return;                       /* 이미 confirm된 이미지 */
    }
    if (app_health_check()) {
        int rc = boot_write_img_confirmed();
        if (rc != 0) {
            /* trailer 쓰기 실패: 다음 부팅에서 revert될 수 있으므로 로그 남김 */
        }
    }
    /* 실패 시 confirm하지 않고 두면, 다음 리셋 때 MCUboot가 이전 이미지로 되돌린다 */
}
```

### Q04. What happens if power is lost at each step of an update?

**왜 묻나**: "power-fail safety"를 단계별로 쪼개서 말할 수 있는지. 가장 자주 나오는 꼬리질문.

**30초 답변**: 단계별로 본다. 다운로드 중 끊기면 inactive 슬롯만 망가지고 현재 이미지는 그대로다. 재개(resume) 가능한 다운로드면 offset부터 이어받는다. 검증 전 끊기면 플래그를 안 세웠으니 아무 일도 없다. "test로 부팅하라" 플래그를 쓰는 중이면 그 쓰기가 원자적이어야 한다(한 워드 쓰기 또는 magic을 마지막에 쓰기). swap 중 끊기면 trailer의 진행 상태로 재개한다. 새 이미지 첫 부팅 중 끊기면 confirm 전이므로 revert된다.

```
단계                    끊기면?                        안전 장치
-------------------------------------------------------------------------
1 다운로드 -> slot B    B만 불완전, A는 정상           resume offset, 전체 해시 재검증
2 해시/서명 검증        변화 없음                      검증 통과 전 플래그 금지
3 "pending" 표시        플래그 쓰기 도중               magic을 마지막에 쓰기(원자적 커밋)
4 부트로더 swap         두 슬롯이 반씩 섞임            trailer의 swap status로 재개
5 새 이미지 trial 부팅  confirm 전 리셋                image_ok 미설정 -> revert
6 confirm               image_ok 쓰기 도중             쓰기 실패면 다음 부팅 revert(안전 쪽)
```

**English answer**: I walk through it step by step. During download only the inactive slot is affected. Before verification nothing has been marked, so a reset is harmless. The pending flag is committed by writing a magic value last, so a half-written trailer reads as "no update". During the swap the bootloader records progress per sector and resumes. And during the trial boot, anything short of an explicit confirm leads to a revert, so the failure mode is always "old firmware", never "no firmware".

**꼬리질문**:
- "flash 쓰기 도중 전압이 떨어지면 셀이 불확정 상태가 되지 않나?" → 그래서 brown-out detector(BOD)를 켜고 쓰기 전 전압을 확인한다. 쓰기 후에는 read-back 검증을 한다.
- "배터리 잔량 몇 %에서 막나?" → 숫자는 제품마다 다르다. 원칙은 "최악 업데이트 시간 x 최대 전류 + 여유"를 계산해서 정하고, 충전 중일 때만 허용하는 옵션도 둔다.

### Q05. How do you decide whether a new image is "good" and when to roll back automatically?

**왜 묻나**: rollback 트리거 설계와 오탐 비용 이해.

**30초 답변**: 자동 롤백 트리거는 세 가지다. 첫째, confirm 전 리셋(크래시, 워치독). 둘째, 부팅 횟수 카운터: trial 이미지가 N번 부팅되는 동안 confirm이 없으면 revert. 셋째, confirm 후에도 서버 쪽 지표(크래시율, 연결 실패율)가 나빠지면 서버가 이전 버전으로 "업데이트"를 내려보낸다. 기기 쪽은 빠르고 확실한 신호만 쓰고, 통계적 판단은 fleet 수준에서 한다.

**English answer**: On the device I only use fast, unambiguous signals: a reset before confirm, a watchdog timeout, or a boot counter that exceeds its limit without a confirm. Anything statistical, like a higher crash rate or worse battery life, is judged at the fleet level, and the fix there is to stop the rollout and ship the previous version as a new update. I keep those two layers separate so the device never reverts a healthy image because of a flaky network.

**꼬리질문**:
- "confirm 후 발견된 버그는?" → anti-rollback 때문에 이전 버전을 그대로 못 내리는 경우가 있다. 그래서 "revert 빌드"는 같은 코드에 더 높은 버전 번호를 붙여 낸다.

### Q06. How would you implement delta (differential) updates, and what are the risks?

**왜 묻나**: Embedded DevOps 공고에 delta update 언급이 있다(context 2.7, 추정). 셀룰러·BLE 대역폭과 배터리 절약.

**30초 답변**: 서버가 현재 버전 이미지와 새 이미지의 binary diff(예: bsdiff 계열, detools 같은 도구)를 만들어 보낸다. 기기는 현재 슬롯 + patch로 새 이미지를 inactive 슬롯에 재구성하고, **재구성된 전체 이미지**의 해시와 서명을 검증한다. 위험은 세 가지다. 기기의 현재 이미지가 서버가 가정한 것과 정확히 같아야 하고, 패치 적용에 RAM이 필요하며, 버전 조합마다 패치가 늘어난다. 그래서 base 이미지 해시를 패치 헤더에 넣어 먼저 확인하고, 실패하면 full 이미지로 fallback한다.

**English answer**: The server generates a binary diff between the exact image the device is running and the new one. The device applies the patch into the inactive slot and then verifies the signature of the fully reconstructed image, not the patch. The risks are a mismatched base image, RAM needed by the patch algorithm, and a growing matrix of from-to patches, so I include the base image hash in the patch header and always keep a full-image fallback.

**꼬리질문**:
- "서명은 patch에 하나 결과 이미지에 하나?" → 결과 이미지 서명은 필수다. patch 자체에도 전송 무결성용 서명을 붙이면 잘못된 patch 적용 시간을 아낄 수 있다.

### Q07. How would you stage a rollout to a fleet of consumer devices?

**왜 묻나**: "OTA infrastructure"는 기기 코드만이 아니다. 운영 감각을 본다.

**30초 답변**: 내부 dogfood → 1% → 5% → 25% → 100% 같은 단계로 가고, 단계마다 게이트 지표를 본다: 업데이트 성공률, 롤백률, 크래시율(부팅 후 24시간), 배터리 소모, 연결 성공률. 기기는 버전, 이전 버전, 업데이트 결과 코드, 롤백 사유를 텔레메트리로 보고한다. 서버는 kill switch(롤아웃 즉시 중지)를 가진다. 기기 쪽에서도 업데이트 시점을 조건으로 건다: 충전 중, 배터리 임계값 이상, 사용자 비활성, Wi-Fi 연결.

**English answer**: I would roll out in stages, internal dogfood first, then 1, 5, 25 and 100 percent, with a gate at each step on update success rate, rollback rate, crash rate in the first day, battery drain and connectivity. Devices report their version, previous version, the result code of the update and any rollback reason. The server has a kill switch, and devices only install when conditions are safe, for example on the charger or above a battery threshold and while idle.

**꼬리질문**:
- "결과 코드는 어떻게 설계하나?" → 다운로드 실패, 서명 실패, 공간 부족, 배터리 부족, swap 실패, health check 실패, 워치독 revert를 구분하는 enum. Don의 SSD error reporting 체계 설계 경험과 같은 문제다.

**Don 스토리 연결**: Solidigm의 error reporting/handling 설계, SK hynix NVMe telemetry 디버그 기능 → "fleet observability는 텔레메트리 스키마부터 설계해야 한다".

---

## 2. Secure boot · 서명 · root of trust

### Q08. Explain the chain of trust from power-on to the application.

**왜 묻나**: 우대 사항 "secure boot, firmware signing, hardware root of trust".

**30초 답변**: 신뢰는 바꿀 수 없는 것에서 시작한다. 칩 ROM의 boot code(mask ROM)가 첫 번째 단계이고, OTP/eFuse에 기록된 공개키 해시가 신뢰 기준이다. ROM이 1단계 부트로더의 서명을 검증하고, 부트로더가 다음 단계(애플리케이션 또는 OS)를 검증한다. 각 단계는 "다음 단계를 검증한 뒤에만 점프"한다. 하나라도 실패하면 부팅을 멈추거나 recovery로 간다.

```
 [ROM boot code]  -- 불변. OTP의 공개키 해시 = root of trust
       | verify(sig, pubkey), hash(pubkey) == OTP hash ?
       v
 [1st-stage bootloader, 예: MCUboot]  -- 내장된 공개키 또는 키 해시
       | verify image signature + anti-rollback counter
       v
 [Application / RTOS]  -- (SoC라면: bootloader -> kernel -> rootfs, dm-verity 등)
```

**English answer**: Trust starts from something immutable: the boot ROM and a hash of the public key burned into OTP fuses. The ROM verifies the first-stage bootloader's signature against that key, the bootloader verifies the application, and each stage only jumps to the next after verification succeeds. The private key never touches the device, only the public key or its hash. If any stage fails verification, the device stops or falls back to recovery instead of running untrusted code.

**꼬리질문**:
- "왜 공개키 전체가 아니라 해시를 OTP에 넣나?" → OTP 비트가 비싸다. 공개키는 이미지에 같이 실어 보내고, ROM은 그 키의 해시가 OTP 값과 같은지만 본다.
- "MCU에 ROM 검증 기능이 없으면?" → 부트로더를 write-protect된 영역에 두고 불변으로 만든다(flash write protection + 디버그 잠금). 이 경우 부트로더가 사실상 root of trust다.

### Q09. What signature algorithms would you use and why? Hash-then-sign, ECDSA vs Ed25519 vs RSA.

**왜 묻나**: 서명 검증의 기본 이해와 MCU 비용 감각.

**30초 답변**: 이미지 전체에 SHA-256 해시를 계산하고, 그 해시에 서명한다. MCUboot가 지원하는 대표 조합은 RSA-2048/3072, ECDSA P-256, Ed25519다. MCU에서는 공개키와 서명이 작은 ECDSA P-256이나 Ed25519가 흔하다. 하드웨어 crypto 가속기(예: P-256 가속)가 있으면 그 알고리즘을 고른다. RSA는 검증이 빠르지만 키와 서명이 커서 flash·전송량이 늘어난다.

**English answer**: The bootloader hashes the whole image with SHA-256 and verifies a signature over that hash. On a microcontroller I would usually pick ECDSA P-256 or Ed25519 because keys and signatures are small, and I'd prefer whichever one the chip's crypto accelerator supports. RSA verification is fast in software but the keys and signatures are larger. The important part is less the algorithm and more the key management around it.

**꼬리질문**:
- "imgtool로 서명하는 명령은?" → 예: `imgtool sign --key root-ec-p256.pem --header-size 0x200 --align 4 --version 1.2.0 --slot-size 0x60000 --pad-header in.bin out.bin` (정확한 값은 파티션 설정에 맞춘다).
- "해시만 있고 서명 없으면?" → 무결성(깨짐 검출)은 되지만 진위(누가 만들었나)는 보장 못 한다.

### Q10. How do you manage signing keys? What if a key leaks?

**왜 묻나**: 서명 자체보다 운영이 실제 위험. 스타트업은 이게 약하다.

**30초 답변**: private key는 HSM이나 클라우드 KMS에만 두고, 서명은 CI의 제한된 릴리스 파이프라인에서만 한다. 개발 키와 양산 키를 분리한다. 개발 기기는 개발 키 해시, 양산 기기는 양산 키 해시를 fuse한다. 유출 대비로 OTP에 키 해시 슬롯을 여러 개 두고 revoke 비트를 두는 칩이 있다(벤더마다 다름). 그런 기능이 없다면 부트로더 안의 키를 교체하는 서명된 업데이트 경로를 미리 설계해 둔다.

**English answer**: Private keys live only in an HSM or a cloud KMS, and only the release pipeline can request a signature, with an audit log. Development and production keys are separate, so a leaked dev key cannot sign something a production device will boot. For compromise I want revocation designed in from day one, for example multiple key-hash slots with revoke bits in OTP if the silicon supports it. Key rotation you add after launch is usually impossible because the fuses are already blown.

**꼬리질문**:
- "개발 키로 서명된 이미지가 양산 기기에 들어가면?" → 부팅 거부. 그래서 factory 라인에서 "양산 키 이미지"인지 확인하는 테스트 항목을 둔다.

### Q11. What is anti-rollback and how is it different from the version number?

**왜 묻나**: 흔한 혼동 포인트.

**30초 답변**: 버전 번호는 "무엇이 더 새것인가"를 표시할 뿐이고, 공격자는 **서명이 유효한 옛 버전**(알려진 취약점이 있는)을 다시 올릴 수 있다. anti-rollback은 하드웨어의 단조 증가 카운터(OTP 비트나 보호된 카운터)에 "최소 허용 security counter"를 저장하고, 그보다 낮은 이미지는 부팅을 거부하는 것이다. 보안 수정이 있을 때만 security counter를 올린다. 매 릴리스마다 올리면 OTP 비트가 금방 바닥나고, 정상 롤백도 못 한다.

**English answer**: The version number just says which image is newer, but an attacker can reinstall an old, validly signed image with a known vulnerability. Anti-rollback stores a minimum security counter in monotonic hardware, typically OTP bits, and the bootloader refuses any image below it. I only bump the security counter for security fixes, not every release, because OTP bits are finite and bumping it also removes my ability to roll back to the previous image. In MCUboot this is the hardware rollback protection option with a security counter in the image TLVs.

**꼬리질문**:
- "counter는 언제 올리나, 부팅 직후?" → 새 이미지가 confirm된 뒤에 올린다. trial 중에 올리면 revert할 곳이 사라진다.

### Q12. How would you lock down debug access on a production device?

**왜 묻나**: root of trust가 있어도 JTAG/SWD가 열려 있으면 소용없다. factory와 연결된다.

**30초 답변**: 양산 흐름의 마지막 단계에서 디버그 포트를 잠근다. 방법은 칩마다 다르다. 예를 들어 Nordic nRF 계열은 APPROTECT, STM32는 RDP(Readout Protection) 레벨로 한다. 완전 영구 잠금은 RMA 분석을 불가능하게 하므로, 가능하면 **인증된 디버그**(challenge-response로 서명된 토큰을 넣어야 열림)를 쓴다. 잠금 여부는 factory 최종 검사에서 반드시 확인한다. 잠그지 않은 채 출하되는 사고가 실제로 흔하다.

**English answer**: Debug lock is the last step of the factory flow and it's always verified by a final test, because shipping unlocked units is a common real-world mistake. The mechanism is vendor specific, like APPROTECT on Nordic parts or readout protection levels on STM32. I prefer authenticated debug where the silicon supports it, so we can still do failure analysis on returned units with a signed, per-device unlock token instead of a permanent fuse.

**꼬리질문**:
- "잠갔는데 필드 불량 분석은?" → 크래시 덤프를 flash에 남기고 텔레메트리로 올린다. JTAG 없이 분석할 수 있어야 한다.

### Q13. What does TrustZone-M give you, and would you use it on an always-on MCU?

**왜 묻나**: Armv8-M 보안 확장 기본 이해(C07).

**30초 답변**: Armv8-M TrustZone은 메모리와 주변장치를 Secure/Non-secure로 나눈다. SAU(Security Attribution Unit)와 벤더의 IDAU가 주소 영역 속성을 정하고, Non-secure 코드는 NSC(Non-secure callable) 영역의 veneer를 통해서만 Secure 함수를 부른다. 키, crypto, 부트 상태를 Secure 쪽(예: TF-M)에 두면 앱 취약점이 키 유출로 이어지지 않는다. 비용은 복잡도, flash/RAM, 전환 오버헤드다. 기기 인증 키를 MCU가 들고 있다면 쓸 가치가 있다. SoC가 인증을 맡으면 MCU에서는 생략할 수도 있다.

**English answer**: TrustZone for Armv8-M splits memory and peripherals into secure and non-secure worlds, configured through the SAU and the vendor's IDAU, and non-secure code can only enter secure code through veneers in non-secure-callable regions. Putting keys and crypto in the secure side, for example with TF-M, means an application bug doesn't leak device credentials. Whether I'd use it on the always-on MCU depends on whether that MCU holds secrets. If the SoC owns device identity, the complexity may not be worth it.

### Q14. Should firmware images be encrypted, or is signing enough?

**왜 묻나**: 무결성·진위 vs 기밀성 구분.

**30초 답변**: 서명은 "누가 만들었고 바뀌지 않았다"를 보장하고, 암호화는 "내용을 못 본다"를 보장한다. 별개다. 이미지가 외부 flash에 저장되거나 공개 서버에서 받는 경우, IP 보호나 취약점 분석 방지가 필요하면 암호화한다. MCUboot는 이미지 암호화를 지원한다(AES 키를 ECIES-P256, X25519, RSA-OAEP, AES-KW 등으로 감싸서 전달). 기기별 복호화 키를 어떻게 안전하게 저장하느냐가 진짜 문제다.

**English answer**: Signing gives authenticity and integrity, encryption gives confidentiality, and they solve different problems. I always sign. I add encryption when the image sits in external flash or on a public CDN and we care about IP or making vulnerability research harder. MCUboot supports encrypted images where a random AES key is wrapped with the device's key, and the hard part is protecting that device key.

---

## 3. 다중 프로세서 업데이트

### Q15. The device has an application SoC, an always-on MCU and a radio chip. How do you keep their firmware versions compatible during an update?

**왜 묻나**: Hark 추정 구조 그대로(context 2.2). 설계 연습(6절)의 요약판.

**30초 답변**: 하나의 **release bundle**로 묶는다. 매니페스트에 각 구성요소 버전, 해시, 그리고 IPC 프로토콜 버전을 적고, 전체에 서명한다. 원칙은 두 가지다. 첫째, IPC 프로토콜은 N과 N-1을 둘 다 말할 수 있게 해서 업데이트 도중 버전이 섞여도 동작하게 한다. 둘째, 적용 순서를 정한다. 보통 SoC가 오케스트레이터이고, MCU와 라디오 이미지를 먼저 스테이징한 뒤 한 번의 재부팅에서 전부 전환하거나, 역호환 쪽을 먼저 올린다. 각 칩은 자기 이미지 서명을 **스스로** 검증한다. SoC가 검증했다고 MCU가 믿으면 SoC 탈취가 MCU까지 이어진다.

**English answer**: I ship one signed release bundle with a manifest that lists each component's version, hash and the IPC protocol version. The IPC protocol supports version N and N minus 1, so a partially updated system still works. The SoC orchestrates: it stages the MCU and radio images, then switches everything over at one reboot, and each processor verifies its own image signature rather than trusting the SoC. The combination is only committed when every component reports healthy, otherwise all of them roll back together.

**꼬리질문**:
- "MCU만 롤백되고 SoC는 새 버전이면?" → N-1 호환 덕분에 동작은 한다. 그 상태를 텔레메트리로 보고하고, SoC가 다음 부팅에서 MCU 재시도 또는 전체 롤백을 결정한다.

**Don 스토리 연결**: Apple에서 새 무선 칩이 호스트와 만날 때의 인터페이스 장애 root cause → "칩 간 버전 불일치는 인터페이스 버그로 보인다".

### Q16. How does firmware get onto a Wi-Fi/BT combo chip, and how does that change OTA?

**왜 묻나**: 라디오 칩 FW는 형태가 다양하다. Don의 무선 칩셋 통합 경험을 꺼낼 수 있는 질문.

**30초 답변**: 두 부류가 있다. 많은 콤보칩은 자체 flash가 없고, 호스트 드라이버가 부팅할 때마다 FW 파일을 SDIO/PCIe/UART로 내려받는다(ROM에 작은 부트 코드만 있음). 이 경우 라디오 FW는 호스트 파일시스템의 일부라서 SoC의 A/B 파티션과 함께 업데이트되고 롤백된다. 반대로 자체 flash가 있는 칩(일부 BLE SoC 등)은 벤더의 업데이트 프로토콜로 따로 써야 하고, 전원 차단 안전성도 그 벤더 방식에 의존한다. 라디오 FW는 보통 벤더가 서명하므로 우리가 재서명할 수 없는 경우가 많다(벤더마다 다름). 추가로 RF calibration 데이터(NVRAM/board file)는 FW와 별도로 기기별로 관리해야 한다.

**English answer**: Many Wi-Fi and Bluetooth combo chips have no flash of their own. The host driver downloads the firmware over SDIO, PCIe or UART at every boot, so the radio firmware is just a file in the SoC's filesystem and it updates and rolls back together with the SoC's A/B slot, which is the easy case. Chips with their own flash need the vendor's update protocol, and their power-fail safety depends on that implementation, so I'd review it early. Radio firmware is usually signed by the vendor, and per-unit RF calibration data has to be kept separate from the firmware so an update never overwrites it.

**꼬리질문**:
- "Apple에서 이런 걸 봤나?" → 레쥬메 범위에서 답한다: "새 무선 칩을 호스트에 붙이며 PCIe/I2C/SPMI/RFFE 장애를 분석했다. FW 다운로드 경로 자체의 세부는 <구체 사례 채우기>."

### Q17. The MCU has only 1 MB of flash but you need A/B for a 400 KB image plus a model. What do you do?

**왜 묻나**: 제약 속 설계. 모델 가중치가 들어가면 파티션 계획이 바뀐다.

**30초 답변**: 먼저 무엇이 같이 바뀌는지 나눈다. 코드와 모델을 **별도 이미지**로 분리하면 모델 업데이트 때 코드를 안 건드린다. 선택지는 이렇다: 다운로드 슬롯을 외부 QSPI NOR로 옮긴다, 모델은 외부 flash에서 XIP 또는 필요 시 SRAM으로 로드한다, 모델은 SoC가 보관하다가 MCU에 내려준다(MCU는 RAM만 쓰고 flash엔 안 둔다), 또는 코드만 A/B이고 모델은 single-slot + 이전 모델로 fallback. 어느 쪽이든 코드와 모델 사이 호환성(입력 feature 형식, op 버전)을 매니페스트에 기록한다.

```
 내부 flash 1 MB (예시)                외부 QSPI NOR 8 MB (예시)
+---------------------+ 0x0000_0000   +-----------------------+
| MCUboot     48 KB   |               | MCU secondary slot    |
| primary    448 KB   |               | model A (active)      |
| cal / NV    32 KB   |               | model B (staged)      |
| (여유)              |               | crash dump / logs     |
+---------------------+               +-----------------------+
```

**English answer**: First I'd separate the code image from the model, so updating the model never touches code and vice versa. Then options are putting the MCU's secondary slot in external QSPI NOR, keeping two model slots externally, or letting the SoC own the model and push it into MCU RAM at boot. Whatever the layout, the manifest records which model versions each firmware version accepts, because feature extraction and operator versions must match.

---

## 4. Factory test · calibration · provisioning

### Q18. How would you design factory test firmware for a new consumer device?

**왜 묻나**: JD "Develop factory test and calibration firmware". Don의 최강점.

**30초 답변**: 원칙은 세 가지다. 첫째, 양산 FW와 테스트 기능을 한 이미지에 두고 **테스트 모드**로 진입하거나, 별도 factory 이미지를 쓴다. 어느 쪽이든 출하 전에 테스트 모드를 닫는 단계가 있어야 한다. 둘째, 기계가 읽는 **명령 프로토콜**(UART/USB 위 요청-응답, 명령 ID, 파라미터, 결과 코드, 측정값)을 정의해서 station 소프트웨어가 자동화하게 한다. 셋째, 모든 결과는 기기 시리얼과 함께 MES에 올라가 추적된다. 테스트 시간은 station당 사이클 타임 안에 넣어야 하므로 병렬화하고, 느린 테스트는 앞 공정으로 옮긴다.

```
 [Station PC / test sequencer] --UART/USB--> [DUT: test mode FW]
        |                                        | 명령: ID, args
        | 결과 -> MES (serial, station, 값, P/F)  | 응답: status, 측정값, 로그
        v                                        v
   limits 파일(버전 관리)                      센서/마이크/라디오/전원 드라이버
```

**English answer**: I'd define a machine-readable command protocol over UART or USB, request and response with a command ID, arguments, a status code and measured values, so the station software drives everything and humans never read logs. Test mode is entered deliberately, for example by a fixture signal plus a command, and it's closed before shipping. Every result is logged against the unit's serial number into the MES, and limits live in a versioned file on the station so we can tune them without reflashing. Cycle time is the main constraint, so I parallelize and move slow tests upstream.

**꼬리질문**:
- "테스트 모드가 필드에서 열리면?" → 보안 구멍. 서명된 factory token으로만 진입, 또는 provisioning 완료 비트가 서면 영구 비활성.

**Don 스토리 연결**: Apple factory test-node 아키텍처 설계 → "무엇을 어느 node에서 검사할지, 커버리지와 시간을 어떻게 나눴는지 <구체 사례 채우기>".

### Q19. What would you calibrate on a wearable with microphones, an IMU and a battery, and where do you store the results?

**왜 묻나**: cal 종류와 저장 설계.

**30초 답변**: 마이크는 기준 음압(보통 94 dB SPL, 1 kHz 톤)에서 감도를 재서 채널별 gain을 맞춘다. 다중 마이크 beamforming이면 채널 간 gain·위상 정합이 중요하다. IMU는 여러 자세에서 accelerometer offset/scale, 정지 상태에서 gyro bias를 잰다. 배터리 fuel gauge는 벤더별 방식(배터리 모델/파라미터 로딩, 학습)을 따른다. 라디오는 벤더 툴로 RF cal. 결과는 보호된 파티션(또는 OTP)에 버전, 길이, CRC를 붙여 저장하고, 가능하면 두 벌로 둔다. **OTA가 절대 덮어쓰지 않는 영역**이어야 한다.

```c
/* 캘리브레이션 레코드 예시: 버전 + 길이 + CRC, A/B 두 벌 저장 */
#include <stdint.h>

#define CAL_MAGIC 0x43414C31u  /* 'CAL1' */

typedef struct {
    uint32_t magic;
    uint16_t version;          /* 레코드 포맷 버전: FW가 이전 포맷도 읽을 수 있어야 함 */
    uint16_t length;           /* payload 길이 */
    uint32_t seq;              /* 두 벌 중 최신 판별 */
    int16_t  mic_gain_q8[4];   /* 채널별 gain, Q8 fixed-point */
    int16_t  accel_offset[3];
    int16_t  gyro_bias[3];
    uint8_t  reserved[16];
    uint32_t crc32;            /* magic..reserved 에 대한 CRC */
} cal_record_t;
```

**English answer**: For microphones I measure sensitivity with a reference tone, typically 94 dB SPL at 1 kHz, and store per-channel gain, and for a beamforming array the matching between channels matters more than the absolute value. For the IMU I store accelerometer offset and scale from several orientations and gyro bias at rest. Battery gauging follows the gauge vendor's flow. Results go into a protected partition with a magic, a format version, a sequence number and a CRC, stored twice, and that region is never touched by OTA.

**꼬리질문**:
- "cal 포맷이 FW 업데이트로 바뀌면?" → 새 FW는 옛 포맷을 읽어 변환할 수 있어야 한다. cal 데이터는 기기에서 다시 못 만든다(라인에서만 가능).

### Q20. What gets provisioned on the line, and how do you make it secure?

**왜 묻나**: 기기 ID, 키, 인증서 — secure boot와 factory의 접점.

**30초 답변**: 시리얼 번호, MAC 주소(Wi-Fi/BT), 기기 고유 키와 인증서(클라우드 인증용), 지역/SKU 설정, 보안 fuse(키 해시, 보안 부팅 활성화, 디버그 잠금). 가장 좋은 방식은 기기 안에서 키 쌍을 생성하고 공개키로 CSR을 만들어 라인의 서명 서버(또는 HSM)가 인증서를 발급하는 것이다. 그러면 private key가 기기 밖으로 나오지 않는다. fuse는 되돌릴 수 없으므로 순서가 중요하다: 모든 테스트 통과 → provisioning → 검증 → 마지막에 잠금.

**English answer**: On the line we program the serial number, MAC addresses, SKU and region, the device identity key and certificate, and the security fuses. I prefer generating the key pair on the device, sending only a CSR to a signing service backed by an HSM, and writing back the certificate, so the private key never exists outside the device. Fuses are irreversible, so the order is test, provision, verify, and only then lock.

### Q21. Factory yield dropped from 98% to 91% on the mic calibration station. How do you approach it? (짧은 버전, 긴 버전은 S06)

**왜 묻나**: 양산 데이터로 추론하는 능력.

**30초 답변**: 먼저 데이터를 쪼갠다: station별, fixture별, 시간대별, 부품 lot별, FW/limits 버전별. 한 station에 몰리면 fixture(스피커 열화, 챔버 밀폐, 소음)다. lot에 몰리면 부품(마이크 lot, 메쉬/가스켓)이다. 특정 날짜부터면 그날 바뀐 것(FW, limits, 공정)을 찾는다. 실패 값의 분포가 limit 근처에 몰렸는지(마진 문제) 멀리 튀는지(고장)를 본다. golden unit을 매일 돌려 station 드리프트를 감시하는 것이 예방책이다.

**English answer**: I'd slice the failures by station, fixture, time, component lot and firmware or limits version. If it's one station, suspect the fixture, like the reference speaker or the seal. If it's one lot, suspect the part. If it started on a date, find what changed that day. I also look at whether failing values sit just outside the limit, which is a margin problem, or far away, which is a real defect. Running a golden unit every shift catches station drift before it hits yield.

### Q22. Define EVT, DVT and PVT, and what firmware must be ready at each.

**왜 묻나**: 우대 사항. Apple NPI 경험으로 말할 수 있는지.

**30초 답변**: EVT(Engineering Validation Test)는 설계가 기능적으로 동작하는지 보는 단계다. 보드 bring-up, 모든 주변장치 드라이버, 기본 전력 측정, 초기 factory 테스트가 필요하다. DVT(Design Validation Test)는 양산 의도 부품·금형으로 설계가 스펙을 만족하는지 검증한다. 신뢰성·환경 시험, 인증(무선 규제 등), 전력·발열 목표, OTA와 secure boot 동작, factory 테스트 완성도가 핵심이다. PVT(Production Validation Test)는 실제 양산 라인과 속도에서 공정을 검증한다. factory FW와 station이 확정되고, yield와 사이클 타임을 맞추며, 출하 FW와 provisioning·잠금 흐름이 최종이다. 그다음이 MP(Mass Production).

| 단계 | 질문 | 펌웨어가 준비할 것 |
|---|---|---|
| EVT | 설계가 동작하나? | bring-up, 드라이버, 디버그 로그, 초기 전류 측정, 기본 테스트 명령 |
| DVT | 스펙을 만족하나? | 전력/발열 튜닝, OTA + secure boot, 인증용 테스트 모드, factory 테스트 거의 완성 |
| PVT | 라인에서 반복 가능한가? | 최종 factory FW, cal + provisioning + 잠금, yield/사이클 타임, 출하 FW 후보 |
| MP | 출하 | day-1 OTA 준비, fleet 텔레메트리, RMA 분석 흐름 |

**English answer**: EVT asks whether the design works at all, so firmware needs bring-up, drivers, debug logging and basic factory test commands. DVT asks whether the production-intent design meets spec, so power and thermal tuning, OTA, secure boot, certification test modes and near-complete factory tests have to be there. PVT asks whether the line can build it repeatedly at rate, so factory firmware, calibration, provisioning and lock-down are final and we're tuning yield and cycle time. Then mass production, with a day-one OTA path and fleet telemetry ready.

**Don 스토리 연결**: Apple bring-up → NPI → MP 사이클. "EVT에서 인터페이스 장애를 잡고, factory test-node로 MP 전에 latent defect를 거른 경험 <구체 사례 채우기>".

### Q23. How do you keep factory test time low without losing coverage?

**왜 묻나**: 사이클 타임은 곧 비용.

**30초 답변**: 테스트를 공정 단계에 나눠 배치한다(보드 레벨 FCT에서 전기적 테스트, 조립 후에는 음향·센서·무선만). 독립적인 테스트는 DUT 내부에서 병렬로 돌린다(예: 마이크 녹음과 IMU 측정 동시). 초기 부팅 시간을 줄이고, 로그 전송을 요약 값만 보내게 하고, 통계적으로 항상 통과하는 항목은 샘플링으로 바꾼다(데이터로 근거를 남긴 뒤). 커버리지는 "어떤 불량 모드를 어느 station이 잡는가" 표로 관리한다.

**English answer**: I keep a matrix of failure modes versus which station catches them, so every test has a reason to exist. Electrical checks go to board-level test, and the final assembly station only does what needs the assembled product, like acoustics and radio. Independent tests run in parallel on the DUT, results come back as summary values rather than raw logs, and tests that never fail can move to sampling once we have the data to justify it.

**Don 스토리 연결**: Apple factory test-node의 커버리지 vs 테스트 시간 트레이드오프 — 레쥬메의 "stress scenarios that caught latent defects before MP" <구체 수치 채우기>.

---

## 5. 빠른 정리 문항

### Q24. What goes in an image header and manifest?

**30초 답변**: magic, 헤더 크기, 이미지 크기, load/실행 주소, 버전(major.minor.revision+build), 하드웨어 호환 정보(보드 revision, 칩 ID), 의존성(최소 부트로더 버전, IPC 프로토콜 버전, 모델 버전), security counter, 해시, 서명, 서명 키 ID. MCUboot는 고정 헤더 + TLV 영역에 해시·서명·key hash·security counter 등을 넣는다.

**English answer**: At minimum a magic, sizes, load address, version, hardware compatibility such as board revision, dependencies on other components, a security counter, the hash, the signature and which key signed it. I make hardware compatibility explicit because flashing an EVT image onto a DVT board with a different pinout is a very real way to brick units.

### Q25. How do you test an OTA system before shipping?

**30초 답변**: 자동화된 전원 차단 테스트가 핵심이다. 프로그래머블 전원 공급기나 릴레이로 업데이트의 각 단계(다운로드, swap sector마다, 첫 부팅)에서 무작위로 전원을 끊고 수천 번 반복해 항상 부팅되는지 본다. 그 외: 잘못된 서명, 잘린 이미지, 잘못된 하드웨어 revision용 이미지, 낮은 배터리, 공간 부족, 네트워크 중단, 버전 건너뛰기(N-2 → N), 롤백 후 재시도.

**English answer**: The most valuable test is automated power cutting: a relay or programmable supply kills power at random points during download, swap and first boot, thousands of times, and the rig checks the unit always comes back. On top of that I test bad signatures, truncated images, images for the wrong board revision, low battery, full storage, network drops, skipped versions and rollback followed by a retry.

---

## 6. 설계 연습 — "Design OTA for a device with an application SoC, an always-on MCU and a Wi-Fi/BT combo chip"

면접관이 이렇게 말한다고 가정한다. "Our device has an application SoC running Linux or Android, an always-on MCU running an RTOS, and a Wi-Fi/BT combo chip. Design the OTA system." 45분 중 이 흐름으로 간다.

```
 0-5분   요구사항 확인          5-12분  구성요소와 저장 위치
 12-22분 업데이트 흐름(단계별)   22-30분 전원 차단 · 롤백 · 호환성
 30-38분 보안(서명, 키, 카운터)  38-45분 롤아웃 · 관측 · 테스트 · trade-off 요약
```

### 6.1 Step 1 — 요구사항 확인 (먼저 질문한다)

바로 그리지 말고 5분 동안 질문한다. 면접관이 모른다고 하면 가정을 말하고 적어 둔다.

1. 연결 경로는? Wi-Fi 직접, 셀룰러, 또는 폰 앱 경유 BLE? (크기·시간·비용이 달라진다)
2. 이미지 크기는? SoC 수백 MB~GB급, MCU 수백 KB, 라디오 FW 수 MB라고 가정한다.
3. 업데이트 중 기기가 계속 동작해야 하나? (always-on wake word는 끊기면 안 된다고 가정)
4. 배터리·충전 조건은? 충전 중에만 설치해도 되나?
5. 보안 요구: secure boot가 이미 있나, 키는 누가 관리하나, anti-rollback 필요?
6. fleet 규모와 롤아웃 속도, 롤백 정책은?
7. MCU flash 크기와 외부 flash 유무?

> 말하는 요령: "I'll assume Wi-Fi as the primary path with cellular as a fallback, the MCU has 1 MB internal flash plus external QSPI NOR, and the always-on wake word should keep running during download. Tell me if any of that is wrong."

### 6.2 Step 2 — 블록도와 저장 위치

```
                        +-------------------- Cloud --------------------+
                        | release service: signed bundle + manifest      |
                        | rollout service: cohorts, gates, kill switch   |
                        +-----------------------+------------------------+
                                                | HTTPS (Wi-Fi / cellular)
+-----------------------------------------------v-------------------------------+
| Application SoC (Linux/Android)                                               |
|  OTA agent (orchestrator)                                                     |
|   - bundle 다운로드, manifest 서명 검증                                        |
|   - SoC 이미지 -> inactive slot (A/B: boot_a/b, system_a/b ...)               |
|   - radio FW 파일 -> inactive rootfs 안 (호스트 로드형이라고 가정)             |
|   - MCU 이미지 -> IPC로 MCU에 전송                                             |
|  UFS/eMMC:  [boot_a][system_a] [boot_b][system_b] [persist: cal, keys, logs] |
+--------------+----------------------------------------------+-----------------+
               | SPI or UART IPC (+ wake GPIO, reset GPIO)    | SDIO / PCIe / UART
               v                                              v
+------------------------------+                +-----------------------------+
| Always-on MCU (RTOS)         |                | Wi-Fi/BT combo chip         |
|  MCUboot (immutable)         |                |  ROM loader + RAM FW        |
|  primary slot (internal)     |                |  (host loads FW each boot)  |
|  secondary slot (ext. NOR)   |                |  per-unit RF cal in SoC     |
|  cal/NV (never touched)      |                |  persist partition          |
+------------------------------+                +-----------------------------+
```

말할 것: "세 구성요소의 업데이트 메커니즘이 다르다. SoC는 A/B 파티션, MCU는 MCUboot 슬롯, 라디오는 호스트 로드형이라 SoC 슬롯에 딸려 간다. 그래서 오케스트레이터는 SoC에 두고, 신뢰는 각 칩이 따로 검증한다."

### 6.3 Step 3 — Bundle과 manifest

```
bundle v2.4.0
 ├─ manifest.json (signed)
 │    bundle_version: 2.4.0
 │    hw_compat: [board_rev >= DVT2]
 │    components:
 │      soc:   version 2.4.0, hash, size, slot_images[...]
 │      mcu:   version 1.9.0, hash, size, security_counter 3
 │      radio: version vendor-x.y, hash (vendor-signed)
 │    ipc_protocol: 7   (MCU 1.9.0 speaks 6 and 7)
 │    min_battery_pct, requires_charging: true/false
 ├─ soc payload (full or delta)
 ├─ mcu.signed.bin   (imgtool로 MCU 키 서명)
 └─ radio fw file(s)
```

핵심 포인트: manifest 서명은 SoC가 확인하지만, MCU 이미지는 **MCU 키로 따로 서명**되어 MCUboot가 다시 검증한다. SoC가 탈취되어도 MCU에 임의 코드를 넣을 수 없다.

### 6.4 Step 4 — 업데이트 흐름 (정상 경로)

```
 SoC OTA agent                 MCU                      Radio
 ------------------------------------------------------------------
 1 check-in, 새 bundle 확인
 2 조건 확인(배터리, 충전, idle)
 3 다운로드(resume 지원) -> 검증
 4 SoC 이미지 -> inactive slot 기록, 검증
   (radio FW 파일은 이 slot 안에 포함)
 5 MCU 이미지 청크 전송 ---------> 6 secondary slot(ext NOR)에 기록
                                    7 전체 해시+서명 검증 (MCU가 직접)
                            <------ 8 "staged OK, v1.9.0"
 9 SoC: inactive slot을 다음 부팅 대상으로 표시 (unconfirmed)
10 MCU에 "test upgrade on next reset" 요청
                                   11 boot_request_upgrade(TEST)
12 조율된 재부팅: SoC가 MCU reset 후 자기 reset
                                   13 MCUboot swap -> v1.9.0 trial 부팅
14 SoC 새 slot 부팅, 새 radio FW 로드 --------------------------> 15 radio FW 실행
16 health check: MCU IPC 핸드셰이크(protocol 7), 라디오 연결, 오디오 경로
17 모두 OK -> MCU에 "confirm" --> 18 boot_write_img_confirmed()
19 SoC 자기 slot confirm (예: Android boot control의 markBootSuccessful 계열)
20 텔레메트리: 결과 코드, 버전, 소요 시간
```

말할 것: "MCU 전송 중에도 MCU는 계속 wake word를 돌린다. 쓰기는 외부 NOR에 하므로 실행 중인 내부 flash를 건드리지 않는다. 전송은 청크 + CRC + ACK, 그리고 offset 기반 재개."

### 6.5 Step 5 — 실패 경로와 롤백 조율

| 실패 지점 | 결과 상태 | 처리 |
|---|---|---|
| 다운로드 중 전원 차단 | 모든 칩 옛 버전 | 재부팅 후 offset부터 재개 |
| MCU 서명 검증 실패 | SoC 새 이미지 스테이징만 됨 | 설치 중단, SoC slot 표시 안 함, 에러 보고 |
| 재부팅 후 MCU trial 이미지 크래시 | MCU 자동 revert(옛 버전) | SoC는 protocol 6으로 통신(N-1 호환), 결과 보고 후 전체 롤백 여부 결정 |
| SoC 새 slot 부팅 실패 | 부트로더가 옛 slot으로 | SoC가 MCU에 confirm 안 보냄 -> MCU도 다음 리셋에 revert |
| 라디오 FW 로드 실패 | 연결 불가 | health check 실패 -> SoC confirm 안 함 -> 전체 revert |
| health check 통과 후 필드 문제 | 전부 새 버전 | 서버가 롤아웃 중지, 수정본(높은 버전) 배포 |

핵심 규칙: **confirm은 SoC가 마지막에 한 번**. MCU는 SoC가 confirm하라고 할 때까지 trial 상태로 남는다. 그래서 "SoC는 롤백됐는데 MCU만 새 버전으로 굳는" 조합이 생기지 않는다.

주의점(말해 둘 것): MCU trial 이미지는 SoC가 부팅을 끝내는 동안 여러 번 리셋될 수 있다. MCUboot test 모드는 "confirm 없이 리셋되면 revert"라서, SoC가 MCU를 리셋할 일이 있으면 revert가 너무 일찍 일어날 수 있다. 그래서 health check 창 동안 SoC는 MCU를 리셋하지 않도록 하고, MCU 앱 쪽에서 confirm을 늦추는 타이머를 둔다.

### 6.6 Step 6 — 전원 차단 안전성 (칩별)

```
 SoC      : A/B slot. 부트로더/boot control이 "unbootable/successful" 상태와 재시도 횟수 관리
 MCU      : MCUboot swap-using-move 또는 scratch. trailer로 재개. image_ok로 revert
 Radio    : 호스트 로드형이면 전원 차단 위험 없음(매 부팅 로드). 자체 flash형이면 벤더 방식 검토 필요
 Cal/NV   : 어떤 OTA 단계도 쓰지 않음. 포맷 변경 시 새 FW가 읽기 호환
 공통     : BOD 켜기, 배터리 임계값 + 최악 설치 시간 계산, flash read-back 검증
```

### 6.7 Step 7 — 보안

1. 두 개의 신뢰 루트: SoC는 SoC 벤더의 secure boot(OTP 키 해시), MCU는 MCUboot(내장 키, 부트로더 write-protect, 디버그 잠금). 가능하면 MCU도 ROM 검증 기능을 쓴다(벤더마다 다름).
2. 키 분리: SoC 키, MCU 키, manifest 키를 분리하고 모두 HSM/KMS에 둔다. dev/prod 분리.
3. anti-rollback: 각 칩 별도 security counter. confirm 이후에만 증가.
4. 전송: TLS + manifest 서명(전송 보안과 이미지 진위는 따로).
5. IPC 경로: MCU는 SoC가 보낸 이미지를 믿지 않고 자기 키로 검증. MCU의 업데이트 명령은 "staged 이미지가 유효할 때만" 동작.

### 6.8 Step 8 — 롤아웃·관측·테스트

- 롤아웃: dogfood → 1% → 5% → 25% → 100%, 게이트 지표는 설치 성공률, 자동 revert율, 24시간 크래시율, 배터리 소모, wake word 오탐/미탐 지표(모델 포함 시).
- 텔레메트리: bundle 버전, 구성요소별 버전, 결과 enum, revert 사유, 설치 소요 시간, 설치 시 배터리.
- 테스트: 전원 차단 rig(각 단계 무작위 차단), 버전 매트릭스(N-2 → N, N → N-1 롤백), 잘못된 서명·잘린 이미지·잘못된 보드 revision, MCU만 실패/SoC만 실패 조합.

### 6.9 Trade-off 요약 (마지막 3분에 말할 것)

| 결정 | 선택 | 대가 |
|---|---|---|
| MCU secondary slot 위치 | 외부 NOR | 외부 flash 공격면 → 내부 복사 후 재검증 필요 |
| 재부팅 방식 | 조율된 1회 재부팅 | 재부팅 순간 always-on 기능 수 초 중단 |
| 버전 호환 | IPC N/N-1 | 프로토콜 코드 복잡도, 테스트 매트릭스 증가 |
| confirm 주체 | SoC가 마지막에 일괄 | SoC 장애 시 MCU도 revert(보수적) |
| delta vs full | SoC는 delta, MCU는 full | 서버 patch 관리 비용, 실패 시 full fallback |
| 라디오 FW | SoC slot에 포함 | 라디오 단독 핫픽스 불가(전체 bundle로 나감) |

> 마무리 영어: "To summarize, the SoC orchestrates, each chip verifies its own image, the IPC protocol tolerates one version of skew, and nothing gets confirmed until the whole system is healthy. The main trade-offs are the external flash attack surface on the MCU and a few seconds of always-on downtime at the coordinated reboot."

---

## 7. 최종 점검

- [ ] "항상 부팅 가능한 검증된 이미지가 하나는 있다"는 불변식을 첫 문장으로 말할 수 있다
- [ ] 업데이트 6단계 각각에서 전원이 끊기면 무슨 일이 생기는지 표로 그릴 수 있다
- [ ] MCUboot overwrite-only, swap-using-scratch, swap-using-move, direct-xip 차이와 image trailer 역할을 설명할 수 있다
- [ ] chain of trust(ROM → 부트로더 → 앱)와 OTP 키 해시를 화이트보드에 그릴 수 있다
- [ ] version number와 security counter의 차이, counter를 언제 올리는지 말할 수 있다
- [ ] SoC + MCU + 라디오 bundle의 manifest와 조율된 confirm 순서를 설명할 수 있다
- [ ] factory 테스트 명령 프로토콜, cal 레코드 구조(magic, version, CRC, 두 벌), provisioning 순서(test → provision → verify → lock)를 말할 수 있다
- [ ] EVT/DVT/PVT/MP 각각에서 펌웨어가 준비할 것을 표로 말할 수 있다
- [ ] Apple factory test-node 스토리에 실제 수치를 채웠다

| 자주 틀리는 포인트 | 바른 설명 |
|---|---|
| "서명 검증했으니 안전" | 키 관리·dev/prod 분리·anti-rollback·디버그 잠금이 없으면 무력하다 |
| confirm을 부팅 직후 바로 | health check 통과 후에 해야 revert가 의미 있다 |
| security counter를 매 릴리스마다 증가 | OTP 고갈 + 정상 롤백 불가. 보안 수정 때만 |
| MCU가 SoC 검증 결과를 신뢰 | 각 칩이 자기 이미지를 스스로 검증해야 한다 |
| cal 데이터를 앱 파티션에 저장 | OTA가 덮어쓴다. 별도 보호 영역 + 버전 + CRC |
| 디버그 잠금을 "나중에" | 출하 전 최종 테스트 항목으로 반드시 확인 |
| delta patch에 서명 | 재구성된 전체 이미지의 서명을 검증해야 한다 |
