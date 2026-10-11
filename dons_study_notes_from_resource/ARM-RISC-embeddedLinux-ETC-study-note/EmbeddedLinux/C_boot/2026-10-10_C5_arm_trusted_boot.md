# C5. ARM 신뢰 부팅 체인 — Exception Level · TF-A · PSCI · OP-TEE · secure boot, "Linux 아래에 누가 살고 있나"

> **이 노트를 다 읽으면**: EL0~EL3와 Secure/Non-secure 세계를 표 하나로 그리고 각 칸에서 어떤 펌웨어가 도는지 말할 수 있다 · TF-A BL1→BL2→BL31→BL32→BL33 순서와 로드 주소를 부팅 로그에서 직접 읽고, "-m 512면 왜 멈추나"를 소스로 설명할 수 있다 · Linux의 CPU 핫플러그가 PSCI `CPU_ON`/`CPU_OFF` SMC로 EL3까지 내려가는 경로를 커널 모듈로 직접 확인할 수 있다 · ROTPK → 인증서 → 이미지 해시로 이어지는 chain of trust를 서명된 FIP로 만들어 보고, 1바이트 변조가 거부되는 것을 보일 수 있다
> **왜 필요한가**: 국방·로보틱스·카메라 회사 면접에서 "secure boot를 어떻게 구성하나", "TrustZone이 뭔가", "secondary CPU는 어떻게 켜지나"가 나오고, 실무에서는 BSP의 TF-A/OP-TEE 버전 올리기, 서명 키 관리, `CPU1: failed to come online` 디버깅이 이 층에서 일어난다
> **Don 기준 난이도**: 특권 모드·예외 벡터·SVC·MPU·부트로더 체인·이미지 서명 검증(SSD FW의 서명된 펌웨어 다운로드)은 이미 안다 / AArch64 EL 모델, "보안 세계"라는 두 번째 주소 공간, EL3 상주 펌웨어와 SMC 호출 규약, TBBR 인증서 트리가 새로 배울 부분
> **선행 노트**: C1(부트 전체 지도), C3(U-Boot 사용) — E3(커널 모듈)을 미리 훑어 두면 §5가 쉽다, L5(secure boot 체인)·L6(런타임 보안)이 이 노트의 뒷이야기다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 만든 SSD 컨트롤러를 떠올려 보자. Cortex-R 코어가 리셋되면 ROM 코드가 돌고, ROM이 NAND나 SPI NOR에서 펌웨어를 읽어 서명을 확인한 뒤 점프한다. 펌웨어 안에서는 특권 모드(SVC)와 비특권 모드가 나뉘고, MPU가 영역을 막는다. 여기까지는 "한 세계" 이야기다.

Cortex-A 기반 Linux SoC(i.MX8, AM62, Jetson Orin)는 여기에 두 가지를 더 얹는다.

- **Exception Level(EL) 4단계**: EL0(앱) · EL1(OS 커널) · EL2(하이퍼바이저) · EL3(secure monitor). 숫자가 클수록 특권이 높다. Cortex-R의 USR/SVC 구분이 두 층 더 깊어진 것이라고 보면 된다.
- **두 개의 세계(TrustZone)**: 같은 코어가 Non-secure 세계(Linux가 사는 곳)와 Secure 세계(키·DRM·지문 같은 비밀이 사는 곳)를 시간 나눠 쓴다. 버스 트랜잭션에 NS 비트가 붙어서, Secure 메모리는 Linux가 아무리 특권을 가져도 읽지 못한다.

이 두 축이 만나는 꼭대기, **EL3에는 단 하나의 펌웨어**가 상주한다. Arm 레퍼런스 구현이 **TF-A(Trusted Firmware-A)**의 **BL31**이다. Linux가 "CPU1 켜 줘", "시스템 꺼 줘", "Spectre 완화 적용해 줘", "보안 세계의 TA 불러 줘"를 요청할 때마다 `smc` 명령으로 EL3에 들어가고, BL31이 처리해서 돌려준다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="230" y="24" text-anchor="middle" font-size="14" font-weight="bold">Non-secure world (일반 세계)</text>
  <text x="530" y="24" text-anchor="middle" font-size="14" font-weight="bold">Secure world (보안 세계)</text>
  <text x="44" y="78" text-anchor="middle" font-size="14" font-weight="bold">EL0</text>
  <text x="44" y="152" text-anchor="middle" font-size="14" font-weight="bold">EL1</text>
  <text x="44" y="226" text-anchor="middle" font-size="14" font-weight="bold">EL2</text>
  <text x="44" y="300" text-anchor="middle" font-size="14" font-weight="bold">EL3</text>
  <rect x="90" y="40" width="280" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="230" y="66" text-anchor="middle" font-size="13">앱 · 센서 데몬 · tee-supplicant</text>
  <text x="230" y="88" text-anchor="middle" font-size="12">Linux 유저 공간</text>
  <rect x="390" y="40" width="280" height="64" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="530" y="66" text-anchor="middle" font-size="13">Trusted Application (TA)</text>
  <text x="530" y="88" text-anchor="middle" font-size="12">키 보관 · 암호 연산 · fTPM</text>
  <rect x="90" y="114" width="280" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="230" y="140" text-anchor="middle" font-size="13">Linux 커널 · U-Boot (BL33)</text>
  <text x="230" y="162" text-anchor="middle" font-size="12">smc / hvc를 부르는 쪽</text>
  <rect x="390" y="114" width="280" height="64" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="530" y="140" text-anchor="middle" font-size="13">OP-TEE OS (BL32) · BL2(부팅 중)</text>
  <text x="530" y="162" text-anchor="middle" font-size="12">Trusted OS — TA를 스케줄</text>
  <rect x="90" y="188" width="210" height="64" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5 3"/>
  <text x="195" y="214" text-anchor="middle" font-size="13">KVM / 하이퍼바이저</text>
  <text x="195" y="236" text-anchor="middle" font-size="12">QEMU 옵션으로 켤 때만</text>
  <rect x="460" y="188" width="210" height="64" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5 3"/>
  <text x="565" y="214" text-anchor="middle" font-size="13">S-EL2: SPM (v8.4+)</text>
  <text x="565" y="236" text-anchor="middle" font-size="12">이 랩에는 없음</text>
  <rect x="90" y="262" width="580" height="64" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="380" y="288" text-anchor="middle" font-size="13">EL3 Secure Monitor: BL1(부팅 ROM) · BL31(상주 — PSCI, SMC 디스패처)</text>
  <text x="380" y="310" text-anchor="middle" font-size="12">두 세계를 오가는 유일한 문 — SCR_EL3.NS 비트로 세계를 바꾼다</text>
  <line x1="335" y1="178" x2="335" y2="260" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="342" y="224" font-size="12">smc</text>
  <line x1="425" y1="260" x2="425" y2="180" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="372" y="224" font-size="12">ERET</text>
  <text x="380" y="352" text-anchor="middle" font-size="12">초록 = Linux 쪽(Don이 드라이버를 쓰는 곳) · 빨강 = 보안 세계 · 파랑 = TF-A BL31</text>
  <text x="380" y="374" text-anchor="middle" font-size="12">점선 = 하드웨어·설정에 따라 없을 수 있는 칸</text>
</svg>
```
그림 1 — AArch64의 EL × 세계 행렬. Linux는 왼쪽 EL1에 살고, EL3의 BL31을 거쳐야만 오른쪽(보안 세계)으로 넘어갈 수 있다.

Embedded Linux 엔지니어가 이 층을 만나는 장면은 이렇다.

- **SMP bring-up**: 새 보드에서 `smp: Brought up 1 node, 1 CPU`만 뜨고 나머지 코어가 안 켜진다 → DT의 `enable-method = "psci"`, TF-A의 PSCI 구현, 보조 코어가 대기하는 hold pen 주소를 본다 (§4).
- **전원 관리**: `poweroff`가 안 꺼지거나, suspend가 `PSCI_FEATURES`에서 거부된다 → BL31의 플랫폼 PM 훅 (§4.5).
- **보안 요구사항**: "부트 체인 전체가 서명돼야 한다", "롤백 공격을 막아라" → TBBR 인증서 트리 + OTP fuse + U-Boot FIT 서명 (§7, L5).
- **TEE 연동**: 카메라 영상 암호화 키나 기기 인증 키를 Linux 밖에 두라 → OP-TEE + TA + `/dev/tee0` (§6).

> 실행 환경: 호스트는 Lima VM `elinux`(aarch64, `uname -r` = `7.0.0-38-generic`). 게스트는 공용 랩 `/var/lab`의 QEMU 10.2.1 `virt` 머신(`-cpu cortex-a57`, TCG 에뮬레이션)에서 TF-A v2.15.0 → U-Boot 2026.10 → Linux 6.18.55 → BusyBox initramfs. TF-A는 `/var/lab/src/tfa`를 `/var/tmp/C5/c5_tfa`로 복사해 다시 빌드했고, OP-TEE OS 4.7.0과 Mbed TLS 3.6.6은 이 노트를 위해 `/var/tmp/C5`에 받아서 빌드했다. 아래 명령은 별도 표시가 없으면 VM 안에서 `cd /var/tmp/C5`한 상태다. 타임스탬프·주소 일부는 실행마다 다르다.

---

## 1. Exception Level과 두 세계 — Cortex-R/M 지식에서 출발하기

### 1.1 직관 — "특권 모드가 4층 건물이 되고, 옆에 똑같은 건물이 하나 더 생겼다"

Cortex-M에서 Don은 Thread mode/Handler mode, privileged/unprivileged를 썼다. RTOS 커널은 privileged Handler mode에서, 태스크는 unprivileged Thread mode에서 돌고, 태스크가 커널 서비스를 원하면 `SVC`로 예외를 일으켰다. Cortex-R(ARMv7-R)은 USR/SVC/IRQ/FIQ/ABT/UND/SYS 모드가 있었다.

AArch64는 이것을 **숫자 하나(EL)**로 정리했다. 예외가 나면 같은 EL이나 더 높은 EL로만 올라가고, `ERET`으로만 내려온다. 그리고 각 EL로 올라가는 "문" 명령이 따로 있다.

| 문 명령 | 어디서 → 어디로 | Don이 아는 것과 대응 | 이 노트에서 |
|---|---|---|---|
| `svc #0` | EL0 → EL1 | Cortex-M `SVC`, Linux syscall (A1) | 앱 → 커널 |
| `hvc #0` | EL1 → EL2 | (대응 없음 — Cortex-R52의 EL2가 가장 가깝다) | QEMU 내장 PSCI의 conduit |
| `smc #0` | EL1/EL2 → EL3 | (대응 없음) TrustZone-M의 SG/NSC 진입이 개념상 비슷 | TF-A BL31 호출 |
| `eret` | 높은 EL → 낮은 EL | Cortex-M `BX LR`(EXC_RETURN), ARMv7 `SUBS PC, LR` | 모든 복귀 |

### 1.2 비교표 — Don의 코어와 Linux SoC 코어

| 항목 | Cortex-M33 (ARMv8-M) | Cortex-R52 (ARMv8-R) | Cortex-A53/A57 (ARMv8-A) |
|---|---|---|---|
| 특권 단계 | privileged / unprivileged | EL0 · EL1 · EL2 | EL0 · EL1 · EL2 · EL3 |
| 보안 세계 | TrustZone-M (Secure/Non-secure state) | 없음 | TrustZone (Secure/Non-secure world) |
| 세계 전환 | SG 명령 + NSC 영역, 하드웨어가 직접 | — | EL3 펌웨어가 `SCR_EL3.NS`를 바꾸고 ERET |
| 메모리 보호 | MPU (SAU/IDAU로 보안 영역) | MPU 2단(EL1, EL2) | MMU 2단(stage 1/2) + TZASC 같은 버스 필터 |
| 부팅 펌웨어 | ROM → 내 FW | ROM → 내 FW | ROM → TF-A(BL1/BL2) → BL31 상주 → U-Boot → Linux |

핵심 차이는 마지막 줄이다. Cortex-A에서는 **Linux가 첫 번째 소프트웨어가 아니고, Linux가 도는 동안에도 Linux보다 높은 곳에 펌웨어가 살아 있다.** SSD 펌웨어에서 "부트로더는 점프하고 사라진다"였다면, 여기서는 BL31이 사라지지 않는다.

### 1.3 왜 EL3 펌웨어가 따로 있어야 하나

1. **세계 전환은 누군가 해야 한다.** `SCR_EL3.NS` 비트는 EL3에서만 쓸 수 있다. Linux(NS-EL1)와 OP-TEE(S-EL1) 사이를 오가려면 둘 다 믿는 제3자가 레지스터를 저장·복원하고 세계를 바꿔야 한다. 그게 secure monitor다.
2. **전원·리셋은 보안 문제다.** CPU 전원을 끄고 켜는 레지스터, 시스템 리셋, DRAM 컨트롤러 설정을 Linux가 직접 만지면, Linux를 장악한 공격자가 보안 세계 코어를 끄거나 메모리를 덮어쓸 수 있다. 그래서 이런 레지스터는 Secure 전용으로 두고, Linux는 EL3에 **표준화된 요청(PSCI)**만 보낸다.
3. **표준 인터페이스로 커널을 SoC에서 분리한다.** PSCI 덕분에 arm64 mainline 커널은 SoC마다 "코어 켜는 코드"를 갖지 않는다. 32-bit ARM 시절에는 `arch/arm/mach-<soc>/platsmp.c`가 SoC마다 있었다. 지금은 DT에 `enable-method = "psci"` 한 줄이면 끝이다.
4. **CPU 에라타·Spectre 완화.** 일부 완화는 EL3에서만 할 수 있는 설정(예: 내부 레지스터 비트)이 필요해서 `SMCCC_ARCH_WORKAROUND_1` 같은 SMC로 노출된다(§5에서 직접 호출한다).

### 1.4 코드로 확인 — BL33이 EL1로 들어가나 EL2로 들어가나

TF-A의 디버그 로그는 다음 단계로 넘어갈 때 `SPSR`를 찍는다. SPSR의 하위 4비트 `M[3:0]`이 "ERET하면 어느 EL로 가는지"다: `0b0101`(5)=EL1h, `0b1001`(9)=EL2h, `0b1101`(0xd)=EL3h. 같은 펌웨어를 QEMU의 `virtualization=on` 유무만 바꿔 부팅했다.

```sh
# (1) secure=on 만: EL2가 없는 CPU
grep -a "SPSR\|started at\|kvm \[" c5_dbg_boot.log
# (2) secure=on,virtualization=on: EL2가 있는 CPU
timeout 120 qemu-system-aarch64 -machine virt,secure=on,virtualization=on -cpu cortex-a57 -smp 2 -m 1057 \
  -nographic -bios tfa-build/qemu/debug/qemu_fw.bios -kernel /var/lab/out/Image \
  -initrd /var/lab/out/initramfs.cpio.gz -append "console=ttyAMA0 LAB_CMD=true" </dev/null 2>&1 \
  | tr -d '\r' | grep -a "SPSR\|started at\|kvm \[\|psci: SMC\|BL31: Preparing"
```

```text
INFO:    SPSR = 0x3c5
INFO:    SPSR = 0x3cd
INFO:    SPSR = 0x3c5
[    0.361366] CPU: All CPU(s) started at EL1
[    1.960675] kvm [1]: HYP mode not available
---- (2) virtualization=on ----
INFO:    SPSR = 0x3c5
INFO:    SPSR = 0x3cd
INFO:    BL31: Preparing for EL3 exit to normal world
INFO:    SPSR = 0x3c9
[    0.000000] psci: SMC Calling Convention v1.5
[    0.072470] CPU: All CPU(s) started at EL2
[    0.385278] kvm [1]: nv: 568 coarse grained trap handlers
[    0.386217] kvm [1]: IPA Size Limit: 44 bits
[    0.395902] kvm [1]: vgic interrupt IRQ9
[    0.396524] kvm [1]: Hyp nVHE mode initialized successfully
```

출력에서 볼 것: 세 개의 SPSR은 순서대로 BL2 진입(0x3c5 = **S-EL1**), BL31 진입(0x3cd = **EL3**), BL33 진입이다. EL2가 없으면 BL33(U-Boot)은 0x3c5로 **NS-EL1**에 들어가고 Linux도 EL1에서 시작해 KVM을 못 쓴다. EL2가 있으면 BL31이 0x3c9(**EL2**)로 넘기고, Linux가 EL2에서 시작해 KVM이 켜진다. 위쪽 4비트 `0x3c0`은 DAIF(인터럽트 마스크) 4개가 모두 켜진 상태다. (출력의 `---- (2) ----` 줄은 두 실행을 구분하려고 넣은 것이고, `kvm` 줄 일부는 생략했다.)

> 함정: "Linux는 EL1에서 돈다"는 반만 맞다. EL2가 있으면 arm64 커널은 EL2에서 시작해, VHE가 있으면 EL2에 머물고 없으면 EL1로 내려가며 EL2에 작은 스텁(nVHE)을 남긴다. 부트로더가 커널을 EL3에서 넘기면 arm64 부트 프로토콜 위반이다(커널 문서 `arch/arm64/booting.rst`: EL2 또는 non-secure EL1).

---

## 2. TF-A 부트 단계 — BL1 · BL2 · BL31 · BL32 · BL33 와 FIP

### 2.1 이름부터 — "BL = Boot Loader stage"

TF-A는 부팅을 번호 붙은 단계로 나눈다. Arm의 TBBR(Trusted Board Boot Requirements) 문서에서 온 이름이다.

| 단계 | 정식 이름 | 어디서 돌고 어디에 놓이나 (이 랩) | 하는 일 | 상주? |
|---|---|---|---|---|
| BL1 | AP Trusted ROM | EL3, ROM 0x00000000 (RW는 0x0e0ee000~) | 최소 초기화, FIP에서 BL2를 읽어 (서명 확인 후) S-EL1로 실행 | 아니오(단, BL2가 SMC로 다시 부른다) |
| BL2 | Trusted Boot Firmware | S-EL1, Secure SRAM 0x0e05b000 | 플랫폼 초기화(실제 칩에선 DRAM), BL31·BL32·BL33을 읽고 검증 | 아니오 |
| BL31 | EL3 Runtime Firmware | EL3, Secure SRAM 0x0e090000 | PSCI, SMC 디스패처, 세계 전환, 인터럽트 라우팅 | **예 — 전원 꺼질 때까지** |
| BL32 | Secure-EL1 Payload | S-EL1, Secure DRAM 0x0e100000 | Trusted OS(OP-TEE) | 예(보안 세계 OS) |
| BL33 | Non-trusted Firmware | NS-EL1/EL2, DRAM 0x60000000 | U-Boot 또는 UEFI(EDK2), 혹은 Linux 직접 | 아니오(Linux에 넘기고 끝) |

실제 SoC에서는 BL1 자리를 **칩 벤더의 BootROM**이 차지하는 경우가 많다(i.MX8M: BootROM → SPL이 BL2 역할 → BL31(TF-A) → U-Boot). 그래서 "TF-A를 쓴다"는 말이 보통 "BL31만 TF-A다"를 뜻한다. C1의 부트 지도와 같이 보면 좋다.

### 2.2 FIP — 펌웨어 묶음 파일

BL2·BL31·BL32·BL33과 인증서들은 **FIP(Firmware Image Package)**라는 한 파일로 묶여 플래시에 들어간다. 맨 앞에 목차(ToC)가 있고, 항목마다 UUID·오프셋·크기가 있다. Don이 SSD에서 만들던 "헤더 + 여러 펌웨어 섹션" 이미지와 같은 발상이다. BL1은 FIP의 위치만 알면 되고(이 랩은 `PLAT_QEMU_FIP_BASE = 0x00040000`), 그 안에서 UUID로 이미지를 찾는다. 소스에서는 이미지마다 **image ID**라는 번호를 쓴다.

```text
/* include/export/common/tbbr/tbbr_img_def_exp.h (v2.15, 발췌) */
#define BL2_IMAGE_ID      U(1)     #define TRUSTED_BOOT_FW_CERT_ID        U(6)
#define BL31_IMAGE_ID     U(3)     #define TRUSTED_KEY_CERT_ID            U(7)
#define BL32_IMAGE_ID     U(4)     #define SOC_FW_KEY_CERT_ID             U(9)
#define BL33_IMAGE_ID     U(5)     #define NON_TRUSTED_FW_KEY_CERT_ID     U(11)
#define BL32_EXTRA1_IMAGE_ID U(21) #define SOC_FW_CONTENT_CERT_ID         U(13)
                                   #define NON_TRUSTED_FW_CONTENT_CERT_ID U(15)
```

(헤더의 두 열을 한 블록에 나란히 옮겨 적은 것이다.) 부팅 로그의 `Loading image id=5`는 "BL33을 읽는 중"이라는 뜻이다.

### 2.3 코드로 확인 — 랩의 release 이미지 부팅 (-smp 4)

공용 랩의 `flash.bin`(BL1 + FIP, release 빌드)을 4코어로 부팅해 U-Boot 프롬프트까지 본다.

```sh
timeout 40 qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -smp 4 -m 1057 \
  -nographic -bios /var/lab/out/flash.bin < /dev/null > c5_boot1.log 2>&1
head -16 c5_boot1.log
```

```text
NOTICE:  Booting Trusted Firmware
NOTICE:  BL1: v2.15.0(release):
NOTICE:  BL1: Built : 09:59:36, Oct 10 2026
NOTICE:  BL1: Booting BL2
NOTICE:  BL2: v2.15.0(release):
NOTICE:  BL2: Built : 09:59:37, Oct 10 2026
NOTICE:  BL1: Booting BL31
NOTICE:  BL31: v2.15.0(release):
NOTICE:  BL31: Built : 09:59:37, Oct 10 2026


U-Boot 2026.10 (Oct 10 2026 - 09:59:26 -0700)

DRAM:  1 GiB
using memory 0x8054f000-0x8158f000 for malloc()
Core:  51 devices, 14 uclasses, devicetree: board
```

출력에서 볼 것: release 빌드는 `LOG_LEVEL=20`(NOTICE)이라 단계 이름만 나온다. 이상한 줄이 하나 있다 — BL2 다음에 **`BL1: Booting BL31`**. BL2는 S-EL1에서 돌기 때문에 EL3인 BL31로 직접 점프할 수 없다. 그래서 BL2가 일을 마치면 `smc`(`BL1_SMC_RUN_IMAGE`, `include/bl1/bl1.h`에서 0x4)로 BL1의 예외 벡터에 다시 들어가고, BL1이 BL31로 ERET한다. "낮은 EL은 높은 EL로 점프할 수 없고, 예외로만 올라간다"의 실제 사례다.

### 2.4 코드로 확인 — TF-A를 INFO 로그로 다시 빌드하기

로드 주소와 image ID를 보려면 `LOG_LEVEL=40`(INFO)으로 다시 빌드한다. `DEBUG=1`이면 assert와 디버그 심볼도 켜진다. 랩 소스는 읽기 전용이므로 복사해서 빌드하고, 출력은 `BUILD_BASE`로 따로 둔다.

```sh
cp -a /var/lab/src/tfa c5_tfa && cd c5_tfa
make -j6 CROSS_COMPILE=aarch64-linux-gnu- PLAT=qemu DEBUG=1 LOG_LEVEL=40 \
     BL33=/var/lab/out/u-boot.bin BUILD_BASE=/var/tmp/C5/tfa-build all fip
```

```text
...
Built /var/tmp/C5/tfa-build/qemu/debug/bl31.bin successfully
...
Trusted Boot Firmware BL2: offset=0xB0, size=0x8204, cmdline="--tb-fw"
EL3 Runtime Firmware BL31: offset=0x82B4, size=0xF89B, cmdline="--soc-fw"
Non-Trusted Firmware BL33: offset=0x17B4F, size=0x16F3D0, cmdline="--nt-fw"

Built /var/tmp/C5/tfa-build/qemu/debug/fip.bin successfully

make: Nothing to be done for 'fip'.
  DD      qemu_fw.bios
  DD      qemu_fw.rom

real	0m8.721s
```

출력에서 볼 것: 빌드는 9초면 끝난다(앞부분 컴파일 줄은 생략). 마지막에 `fiptool`이 FIP 목차를 찍는다 — BL33(U-Boot)이 0x16F3D0 = 1,504,208바이트로 `/var/lab/out/u-boot.bin` 크기와 같다. `--tb-fw`, `--soc-fw`, `--nt-fw`는 `fiptool create/update`에서 그 항목을 넣을 때 쓰는 옵션 이름이다. `qemu_fw.bios`는 BL1과 FIP를 이어 붙인 파일로, `-bios`에 그대로 쓴다.

FIP는 언제든 `fiptool info`로 열어 볼 수 있다. 랩의 release FIP와 비교하면 같은 BL33에 BL2·BL31 크기만 다르다(디버그 코드와 로그 문자열 때문).

```sh
F=tfa-build/qemu/debug/tools/fiptool/fiptool
$F info /var/lab/out/fip.bin
```

```text
Trusted Boot Firmware BL2: offset=0xB0, size=0x61F0, cmdline="--tb-fw"
EL3 Runtime Firmware BL31: offset=0x62A0, size=0xC067, cmdline="--soc-fw"
Non-Trusted Firmware BL33: offset=0x12307, size=0x16F3D0, cmdline="--nt-fw"
```

### 2.5 코드로 확인 — 로드 주소 읽기 (BL1 → BL2 → BL31 → BL33 → Linux)

디버그 빌드로 부팅하면서 `-kernel/-initrd`를 같이 주면, QEMU가 커널을 fw_cfg 장치에 실어 두고 U-Boot의 `qfw` bootmeth가 그것을 읽어 부팅한다.

```sh
timeout 90 qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -smp 4 -m 1057 -nographic \
  -bios tfa-build/qemu/debug/qemu_fw.bios -kernel /var/lab/out/Image \
  -initrd /var/lab/out/initramfs.cpio.gz \
  -append "console=ttyAMA0 LAB_CMD=dmesg~|~grep~-i~-e~psci~-e~smp~-e~CPU[0-9]" < /dev/null > c5_dbg_boot.log 2>&1
grep -v "erratum\|CVE" c5_dbg_boot.log | head -50
```

```text
NOTICE:  Booting Trusted Firmware
NOTICE:  BL1: v2.15.0(debug):
NOTICE:  BL1: Built : 10:25:45, Oct 10 2026
INFO:    BL1: RAM 0xe0ee000 - 0xe0f6000
INFO:    BL1: Loading BL2
INFO:    Loading image id=1 at address 0xe05b000
INFO:    Image id=1 loaded: 0xe05b000 - 0xe063204
NOTICE:  BL1: Booting BL2
INFO:    Entry point address = 0xe05b000
INFO:    SPSR = 0x3c5
NOTICE:  BL2: v2.15.0(debug):
NOTICE:  BL2: Built : 10:25:47, Oct 10 2026
INFO:    BL2: Doing platform setup
INFO:    BL2: Loading image id 3
INFO:    Loading image id=3 at address 0xe090000
INFO:    Image id=3 loaded: 0xe090000 - 0xe09f89b
INFO:    BL2: Loading image id 5
INFO:    Loading image id=5 at address 0x60000000
INFO:    Image id=5 loaded: 0x60000000 - 0x6016f3d0
NOTICE:  BL1: Booting BL31
INFO:    Entry point address = 0xe090000
INFO:    SPSR = 0x3cd
NOTICE:  BL31: v2.15.0(debug):
NOTICE:  BL31: Built : 10:25:48, Oct 10 2026
INFO:    ARM GICv2 driver initialized
INFO:    BL31: Initializing runtime services
INFO:    BL31: Preparing for EL3 exit to normal world
INFO:    Entry point address = 0x60000000
INFO:    SPSR = 0x3c5


U-Boot 2026.10 (Oct 10 2026 - 09:59:26 -0700)

DRAM:  1 GiB
...
Scanning bootdev 'fw-cfg@9020000.bootdev':
loading kernel to address 40400000 size 28c2a00 initrd 44000000 size 1277e2
...
Starting kernel ...

[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
[    0.000000] psci: probing for conduit method from DT.
[    0.000000] psci: PSCIv1.1 detected in firmware.
[    0.000000] psci: Using standard PSCI v0.2 function IDs
[    0.000000] psci: MIGRATE_INFO_TYPE not supported.
[    0.000000] psci: SMC Calling Convention v1.5
```

출력에서 볼 것: 디버그 빌드는 CPU 에라타 적용 여부를 BL1·BL31에서 줄마다 찍는데(`cortex_a57: CPU workaround for erratum ... was applied/missing`), 길어서 `grep -v`로 뺐다. 남은 줄이 **부트 체인의 메모리 지도 그 자체**다: BL2 → `0x0e05b000`, BL31 → `0x0e090000`(id=3), BL33 → `0x60000000`(id=5), 커널 → `0x40400000`, initrd → `0x44000000`. BL31이 "normal world로 나간다"고 말한 다음 줄부터 U-Boot이고, 마지막 Linux 줄은 BL31이 PSCI v1.1과 SMCCC v1.5를 제공한다는 것을 커널이 SMC로 물어 확인한 결과다.

이 주소들은 모두 소스에 정해져 있다. `plat/qemu/qemu/include/platform_def.h`를 읽으면 로그의 숫자가 계산된다.

```text
#define SEC_ROM_BASE      0x00000000     /* BL1 RO (QEMU flash0) */
#define SEC_SRAM_BASE     0x0e000000     /* 1 MB Secure SRAM */
#define SEC_SRAM_SIZE     0x00100000
#define SEC_DRAM_BASE     0x0e100000     /* 15 MB Secure DRAM (BL32) */
#define NS_DRAM0_BASE     ULL(0x40000000)
#define SHARED_RAM_BASE   SEC_SRAM_BASE  /* 4 KB: hold pen (PLAT_QEMU_HOLD_BASE) */
#define BL_RAM_BASE       (SHARED_RAM_BASE + SHARED_RAM_SIZE)          /* 0x0e001000 */
#define BL31_LIMIT        (BL_RAM_BASE + BL_RAM_SIZE - FW_HANDOFF_SIZE)/* 0x0e100000 */
#define BL31_BASE         (BL31_LIMIT - 0x70000)                       /* 0x0e090000 */
#define BL2_BASE          (BL31_BASE - 0x35000)                        /* 0x0e05b000 */
#define NS_IMAGE_OFFSET   (NS_DRAM0_BASE + 0x20000000)                 /* 0x60000000 = BL33 */
#define PLAT_QEMU_FIP_BASE 0x00040000
#define PLAT_QEMU_DT_BASE  NS_DRAM0_BASE                               /* QEMU가 DT를 두는 곳 */
```

(주석의 계산값은 내가 덧붙인 것이다. 로그의 `0xe090000`, `0xe05b000`, `0x60000000`과 일치한다 — `FW_HANDOFF_SIZE`는 이 빌드에서 0이다.)

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="12" y="16" width="116" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="70" y="34" text-anchor="middle" font-size="13">BL1 · EL3</text>
  <text x="70" y="52" text-anchor="middle" font-size="12">ROM 0x0</text>
  <rect x="142" y="16" width="116" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="200" y="34" text-anchor="middle" font-size="13">BL2 · S-EL1</text>
  <text x="200" y="52" text-anchor="middle" font-size="12">0x0e05b000</text>
  <rect x="272" y="16" width="116" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="330" y="34" text-anchor="middle" font-size="13">BL31 · EL3</text>
  <text x="330" y="52" text-anchor="middle" font-size="12">0x0e090000</text>
  <rect x="402" y="16" width="116" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="460" y="34" text-anchor="middle" font-size="13">BL32 · S-EL1</text>
  <text x="460" y="52" text-anchor="middle" font-size="12">0x0e100000</text>
  <rect x="542" y="16" width="126" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="605" y="34" text-anchor="middle" font-size="13">BL33 · NS-EL1</text>
  <text x="605" y="52" text-anchor="middle" font-size="12">0x60000000</text>
  <line x1="70" y1="60" x2="70" y2="415" stroke="currentColor" stroke-dasharray="4 4" stroke-opacity="0.5"/>
  <line x1="200" y1="60" x2="200" y2="415" stroke="currentColor" stroke-dasharray="4 4" stroke-opacity="0.5"/>
  <line x1="330" y1="60" x2="330" y2="415" stroke="currentColor" stroke-dasharray="4 4" stroke-opacity="0.5"/>
  <line x1="460" y1="60" x2="460" y2="415" stroke="currentColor" stroke-dasharray="4 4" stroke-opacity="0.5"/>
  <line x1="605" y1="60" x2="605" y2="415" stroke="currentColor" stroke-dasharray="4 4" stroke-opacity="0.5"/>
  <text x="135" y="92" text-anchor="middle" font-size="12">① FIP에서 id=1 로드</text>
  <line x1="70" y1="100" x2="198" y2="100" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="135" y="116" text-anchor="middle" font-size="12">ERET (SPSR 0x3c5)</text>
  <text x="265" y="146" text-anchor="middle" font-size="12">② id=3 로드</text>
  <line x1="200" y1="154" x2="328" y2="154" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
  <text x="395" y="180" text-anchor="middle" font-size="12">③ id=4, 21 로드</text>
  <line x1="200" y1="188" x2="458" y2="188" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
  <text x="470" y="214" text-anchor="middle" font-size="12">④ id=5 로드 (U-Boot)</text>
  <line x1="200" y1="222" x2="603" y2="222" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
  <line x1="200" y1="256" x2="72" y2="256" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="135" y="250" text-anchor="middle" font-size="12">⑤ smc RUN_IMAGE</text>
  <line x1="70" y1="292" x2="328" y2="292" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="200" y="286" text-anchor="middle" font-size="12">⑥ ERET (SPSR 0x3cd)</text>
  <line x1="330" y1="326" x2="458" y2="326" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="395" y="320" text-anchor="middle" font-size="12">⑦ BL32 초기화</text>
  <line x1="458" y1="344" x2="332" y2="344" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="395" y="360" text-anchor="middle" font-size="12">smc 복귀</text>
  <line x1="330" y1="384" x2="603" y2="384" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="466" y="378" text-anchor="middle" font-size="12">⑧ ERET (SPSR 0x3c5)</text>
  <text x="605" y="406" text-anchor="middle" font-size="12">→ Linux 0x40400000</text>
</svg>
```
그림 2 — 부트 단계 시퀀스. 실선은 실행 이동(ERET/SMC), 점선은 "메모리에 복사만 함". BL32(⑦)는 OP-TEE를 넣은 빌드에서만 있다(§6).

### 2.6 흔한 함정

- **BL33 주소를 커널 주소로 착각.** `0x60000000`은 U-Boot가 처음 놓이는 곳일 뿐이고, U-Boot은 곧 RAM 꼭대기로 자기 자신을 옮긴다(relocation, C3·C4). 위 로그의 `using memory 0x8054f000-...`가 옮겨 간 뒤의 malloc 영역이다.
- **`-bios`에 `fip.bin`만 넣기.** BL1이 없으면 아무것도 안 뜬다. `qemu_fw.bios` 또는 `flash.bin`(BL1 + 0x40000 위치의 FIP)을 쓴다.
- **release 빌드에서 로그가 없다고 멈춘 줄 알기.** release는 NOTICE만 찍는다. 원인을 찾을 땐 `DEBUG=1 LOG_LEVEL=40`으로 다시 빌드하는 게 첫 단계다.

---

## 3. "-m 512에서 멈춘다" — 메모리 지도를 소스로 증명하기

랩 README에는 "secure 경로는 RAM이 넉넉해야 하고 `-m 512`면 `BL2: Built` 뒤에 조용히 멈춘다"고 적혀 있다. 추측하지 말고 확인해 보자.

### 3.1 코드로 확인 — 증상 재현과 예외 추적

QEMU의 `-d int,guest_errors`는 게스트가 받은 예외를 로그 파일에 남긴다. JTAG 없이 "어디서 터졌나"를 보는 가장 싼 방법이다.

```sh
timeout 20 qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -m 512 -nographic \
  -bios tfa-build/qemu/debug/qemu_fw.bios -d guest_errors,int -D c5_m512_int.log < /dev/null > c5_m512_dbg.log 2>&1
tail -4 c5_m512_dbg.log; cat c5_m512_int.log
```

```text
INFO:    BL2: Loading image id 3
INFO:    Loading image id=3 at address 0xe090000
INFO:    Image id=3 loaded: 0xe090000 - 0xe09f89b
INFO:    BL2: Loading image id 5
INFO:    Loading image id=5 at address 0x60000000
qemu-system-aarch64: terminating on signal 15 from pid 220171 (timeout)
Exception return from AArch64 EL3 to AArch64 EL1 PC 0xe05b000
Taking exception 4 [Data Abort] on CPU 0
...from EL1 to EL1
...with ESR 0x25/0x96000050
...with FAR 0x60000000
...with SPSR 0x200002c5
...with ELR 0xe05ff64
...to EL1 PC 0xe060a00 PSTATE 0x3c5
```

출력에서 볼 것: 첫 줄은 BL1이 BL2로 ERET한 것(정상). 그다음 BL2(S-EL1)가 **주소 0x60000000에 쓰다가 Data Abort**를 맞았다. ESR을 풀면: EC=0x25(같은 EL의 data abort), ISS의 WnR=1(쓰기), DFSC=0x10(synchronous external abort) — "그 주소엔 아무것도 없다"는 버스 에러다. Don이 SSD에서 보던 "존재하지 않는 주소에 AXI 쓰기 → SLVERR/DECERR"과 같은 종류다.

### 3.2 왜 0x60000000인가 — 소스

QEMU `virt`의 DRAM은 `0x40000000`에서 시작한다. `-m 512`면 DRAM은 `0x40000000 ~ 0x5fffffff`다. 그런데 TF-A의 qemu 플랫폼은 BL33을 다음 주소에 고정해 둔다.

```text
/* plat/qemu/qemu/include/platform_def.h */
#define NS_IMAGE_OFFSET         (NS_DRAM0_BASE + 0x20000000)   /* = 0x60000000 */
#define NS_IMAGE_MAX_SIZE       (NS_DRAM0_SIZE - 0x20000000)
/* plat/qemu/common/qemu_bl2_mem_params_desc.c — BL33 descriptor */
	  .ep_info.pc = NS_IMAGE_OFFSET,
	  .image_info.image_base = NS_IMAGE_OFFSET,
	  .image_info.image_max_size = NS_IMAGE_MAX_SIZE,
```

즉 BL33 로드 주소는 **DRAM 시작 + 정확히 512 MB**, `-m 512`의 바로 끝 다음 바이트다. 그리고 "조용히" 멈추는 이유도 소스에 있다. BL2의 예외 벡터(`common/aarch64/early_exceptions.S`)는 `plat_report_exception` → `plat_panic_handler`를 부르는데, qemu 플랫폼은 둘 다 기본(weak) 구현을 쓴다.

```text
/* plat/common/aarch64/platform_helpers.S */
func plat_report_exception
	ret                      /* 아무것도 출력하지 않는다 */
endfunc plat_report_exception
func plat_panic_handler
	wfi
	b	plat_panic_handler   /* 영원히 대기 */
endfunc plat_panic_handler
```

에러 메시지 없이 `wfi` 무한 루프 — Don의 펌웨어에서 HardFault 핸들러가 `while(1);`이던 것과 똑같다.

### 3.3 그럼 얼마가 필요한가 — 측정

README는 "≥ 1057 MB"라고 적었지만, 원인이 "0x60000000 + U-Boot 크기"라면 512 MB를 조금만 넘어도 될 것이다. 7개 크기를 동시에 돌려 봤다(release `flash.bin`, 18초 제한).

```sh
for m in 513 514 516 520 530 540 560; do
  (timeout 18 qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -m $m -nographic \
     -bios /var/lab/out/flash.bin < /dev/null > c5_mm$m.log 2>&1 &)
done; sleep 20
for m in 513 514 516 520 530 540 560; do
  echo "-m $m: aborts=$(grep -ac 'Synchronous Abort' c5_mm$m.log) autoboot=$(grep -ac 'Hit any' c5_mm$m.log)"
done
```

```text
-m 513: aborts=0 autoboot=0
-m 514: aborts=0 autoboot=1
-m 516: aborts=0 autoboot=1
-m 520: aborts=0 autoboot=1
-m 530: aborts=0 autoboot=1
-m 540: aborts=65 autoboot=0
-m 560: aborts=0 autoboot=1
```

(`aborts` 수는 18초 동안 리셋을 몇 번 반복했느냐라 실행마다 다르다. 540 근처는 따로 다시 돌려 530·535·545·550은 정상, 540만 재현됨을 확인했다.)

출력에서 볼 것:

- **513 MB**: DRAM 끝이 `0x60100000`인데 U-Boot은 `0x60000000 ~ 0x6016f3d0`(1.43 MB)이라 끝부분이 DRAM 밖 → BL2에서 같은 Data Abort. 1 MB로는 모자란다.
- **514 MB 이상**: U-Boot 전체가 DRAM 안에 들어가 U-Boot 프롬프트까지 간다. 600·1024·1056·1057도 모두 정상이었다.
- **540 MB만 예외**: TF-A는 통과하고, U-Boot이 relocation 직후 `"Synchronous Abort" handler, esr 0x02000000`으로 죽은 뒤 리셋을 반복한다(로그 발췌는 아래). U-Boot은 RAM 꼭대기 쪽으로 자기 코드를 옮기는데, 꼭대기가 처음 로드 위치(0x60000000) 바로 위에 있으면 옮길 자리와 다른 데이터가 꼬일 수 있다. 정확한 원인 분석은 U-Boot 내부(C3·C4)의 일이라 여기선 "여유 없는 메모리 배치는 위험하다"까지만 말한다.

```text
DRAM:  540 MiB
using memory 0x6004f000-0x6108f000 for malloc()
Core:  51 devices, 14 uclasses, devicetree: board
"Synchronous Abort" handler, esr 0x02000000
elr: 000000000004e6d4 lr : 00000000000421c8 (reloc)
elr: 00000000610dd6d4 lr : 00000000610d11c8
...
Resetting CPU ...
ERROR:   QEMU System Reset: with GPIO.
```

결론: "TF-A 경로는 1057 MB가 필요하다"가 아니라 **"BL33을 0x60000000에 놓도록 컴파일됐으니 DRAM이 0x60000000 + BL33 크기를 덮어야 하고, U-Boot relocation 여유까지 생각하면 넉넉히 주는 게 안전하다"**가 정확한 설명이다. 랩에서는 계속 `-m 1057`을 쓴다.

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
  <text x="255" y="22" text-anchor="middle" font-size="14" font-weight="bold">-m 512 (끝 0x5fffffff)</text>
  <text x="545" y="22" text-anchor="middle" font-size="14" font-weight="bold">-m 1057 (끝 0x820fffff)</text>
  <text x="118" y="334" text-anchor="end" font-size="12">0x00000000</text>
  <text x="118" y="264" text-anchor="end" font-size="12">0x0e000000</text>
  <text x="118" y="219" text-anchor="end" font-size="12">0x0e100000</text>
  <text x="118" y="184" text-anchor="end" font-size="12">0x0f000000</text>
  <text x="118" y="169" text-anchor="end" font-size="12">0x40000000</text>
  <text x="118" y="104" text-anchor="end" font-size="12">0x60000000</text>
  <rect x="130" y="290" width="250" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="315" text-anchor="middle" font-size="12">Flash0: BL1 + FIP (0x40000)</text>
  <rect x="130" y="260" width="250" height="30" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="255" y="280" text-anchor="middle" font-size="12">MMIO: GIC · UART · fw_cfg</text>
  <rect x="130" y="215" width="250" height="45" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="234" text-anchor="middle" font-size="12">Secure SRAM 1 MB</text>
  <text x="255" y="251" text-anchor="middle" font-size="12">hold pen · BL2 · BL31 · BL1 RW</text>
  <rect x="130" y="180" width="250" height="35" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="255" y="202" text-anchor="middle" font-size="12">Secure DRAM 15 MB: BL32</text>
  <rect x="130" y="100" width="250" height="65" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="255" y="126" text-anchor="middle" font-size="12">DRAM 512 MB</text>
  <text x="255" y="144" text-anchor="middle" font-size="12">DT · 커널 0x40400000 · initrd</text>
  <rect x="130" y="40" width="250" height="60" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a" stroke-dasharray="6 3"/>
  <text x="255" y="62" text-anchor="middle" font-size="12">메모리 없음</text>
  <text x="255" y="82" text-anchor="middle" font-size="12">BL33 쓰기 → Data Abort (FAR 0x60000000)</text>
  <rect x="420" y="290" width="250" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="545" y="315" text-anchor="middle" font-size="12">Flash0: BL1 + FIP (0x40000)</text>
  <rect x="420" y="260" width="250" height="30" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="545" y="280" text-anchor="middle" font-size="12">MMIO: GIC · UART · fw_cfg</text>
  <rect x="420" y="215" width="250" height="45" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="545" y="234" text-anchor="middle" font-size="12">Secure SRAM 1 MB</text>
  <text x="545" y="251" text-anchor="middle" font-size="12">hold pen · BL2 · BL31 · BL1 RW</text>
  <rect x="420" y="180" width="250" height="35" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="545" y="202" text-anchor="middle" font-size="12">Secure DRAM 15 MB: BL32</text>
  <rect x="420" y="40" width="250" height="125" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="545" y="62" text-anchor="middle" font-size="12">DRAM 1057 MB (U-Boot이 꼭대기로 이동)</text>
  <rect x="440" y="84" width="210" height="26" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
  <text x="545" y="102" text-anchor="middle" font-size="12">BL33 U-Boot @ 0x60000000</text>
  <text x="545" y="144" text-anchor="middle" font-size="12">DT · 커널 0x40400000 · initrd</text>
  <text x="340" y="360" text-anchor="middle" font-size="12">축척 아님 — 아래가 낮은 주소. 0x0e000000~0x0f000000은 QEMU가 secure=on일 때만 만드는 Secure RAM</text>
</svg>
```
그림 3 — 같은 FIP, 다른 RAM 크기. BL33 로드 주소는 컴파일 시점에 `NS_DRAM0_BASE + 0x20000000`으로 박혀 있으므로, `-m 512`에서는 정확히 DRAM 바로 바깥을 가리킨다.

> Don 연결: SSD 펌웨어에서 "링커 스크립트의 섹션 주소가 실제 SRAM 크기를 넘어서 부팅 직후 bus fault"를 겪었다면 같은 문제다. 실제 보드에서는 DRAM 크기를 BL2가 DDR 트레이닝 후 알게 되므로, 이런 고정 주소는 **BSP의 platform_def.h와 보드 DRAM 구성을 맞추는 작업**으로 나타난다(256 MB 보드에 1 GB 가정 TF-A를 올리면 똑같이 멈춘다).

---

## 4. PSCI — OS가 펌웨어에게 "전원"을 요청하는 표준 API

### 4.1 직관 — 전원 관리용 syscall

앱이 파일을 열고 싶으면 커널에 `openat` syscall을 한다. 커널이 CPU를 켜고 싶으면 펌웨어에 **PSCI(Power State Coordination Interface)** 호출을 한다. 번호(function ID)를 `x0`에 넣고 인자를 `x1`~`x3`에 넣은 뒤 `smc #0`(또는 `hvc #0`)을 실행하면, 결과가 `x0`로 돌아온다. syscall과 모양이 똑같다. 차이는 받는 쪽이 EL1 커널이 아니라 EL3 펌웨어라는 것뿐이다.

| 함수 | function ID | 인자 | 하는 일 | 리눅스에서 부르는 곳 |
|---|---|---|---|---|
| PSCI_VERSION | 0x84000000 | — | 버전 (상위 16비트 major) | `psci_probe()` 부팅 초기 |
| CPU_SUSPEND | 0xC4000001 | power_state, entry, ctx | 코어(·클러스터)를 저전력 상태로 | cpuidle `psci_cpu_suspend_enter()` |
| CPU_OFF | 0x84000002 | — | 호출한 코어 자신을 끈다 (돌아오지 않음) | `cpu_psci_cpu_die()` 핫플러그 |
| CPU_ON | 0xC4000003 | target MPIDR, entry, ctx | 다른 코어를 켜서 entry에서 시작 | `cpu_psci_cpu_boot()` SMP 부팅 |
| AFFINITY_INFO | 0xC4000004 | target MPIDR, level | 그 코어가 ON(0)/OFF(1)/ON_PENDING(2)인가 | `cpu_psci_cpu_kill()` |
| SYSTEM_OFF | 0x84000008 | — | 전원 끄기 | `poweroff` → `psci_sys_poweroff()` |
| SYSTEM_RESET | 0x84000009 | — | 리셋 | `reboot` → `psci_sys_reset()` |
| PSCI_FEATURES | 0x8400000A | function ID | 그 함수를 지원하나 | 초기화 때 기능 탐색 |
| SYSTEM_SUSPEND | 0xC400000E | entry, ctx | suspend-to-RAM | `echo mem > /sys/power/state` |

(ID는 커널 `include/uapi/linux/psci.h` 기준. 0xC4…는 64-bit 호출 규약, 0x84…는 32-bit.) 반환값은 `0`=SUCCESS, `-1`=NOT_SUPPORTED, `-2`=INVALID_PARAMETERS, `-3`=DENIED, `-4`=ALREADY_ON, `-5`=ON_PENDING 등 음수 에러 코드다.

### 4.2 커널은 PSCI를 어떻게 찾나 — Device Tree

커널은 PSCI 펌웨어가 있다는 것을 DT로 안다. 두 가지가 필요하다: 루트의 `/psci` 노드(어떤 명령으로 부를지 `method = "smc"` 또는 `"hvc"`), 그리고 각 CPU 노드의 `enable-method = "psci"`. 이 노드를 누가 넣는지가 재미있다 — **TF-A 경로에서는 BL2가** QEMU가 만든 DT(0x40000000)를 고쳐서 넣는다.

```text
/* plat/qemu/common/qemu_bl2_setup.c (v2.15, 발췌) */
	if (dt_add_psci_node(fdt)) {               /* common/fdt_fixup.c: compatible + method="smc" */
	...
	if (dt_add_psci_cpu_enable_methods(fdt)) { /* 모든 cpu 노드에 enable-method = "psci" */
```

### 4.3 코드로 확인 — 게스트 안에서 PSCI 들여다보기 (TF-A vs QEMU 내장 PSCI)

같은 커널·initramfs를 두 방식으로 부팅한다. (A) `-machine virt,secure=on -bios qemu_fw.bios` — EL3에 TF-A가 있다. (B) `-machine virt -kernel Image` — EL3 펌웨어가 없다. 이때는 **QEMU가 PSCI를 직접 흉내 낸다**(QEMU 소스의 `target/arm/psci.c`). 게스트에서 실행할 스크립트는 이렇다.

```sh
#!/bin/sh
# c5_test.sh - QEMU 게스트 안에서 실행: PSCI 정보 수집 + 핫플러그 + c5_smc
echo "--- /proc/device-tree/psci"
ls /proc/device-tree/psci
echo "compatible = $(tr '\0' ' ' < /proc/device-tree/psci/compatible)"
echo "method     = $(tr '\0' ' ' < /proc/device-tree/psci/method)"
for f in cpu_on cpu_off; do [ -e /proc/device-tree/psci/$f ] && echo "$f = 0x$(od -An -tx1 /proc/device-tree/psci/$f | tr -d ' ')"; done
echo "--- cpu0 enable-method: $(cat /proc/device-tree/cpus/cpu@0/enable-method)"
echo "--- debugfs psci"; cat /sys/kernel/debug/psci
echo "--- dmesg psci/smp"; dmesg | grep -i -e psci -e smp: -e "CPU[0-9]:"
echo "--- hotplug cpu1"
cat /sys/devices/system/cpu/online
echo 0 > /sys/devices/system/cpu/cpu1/online; echo "after off: $(cat /sys/devices/system/cpu/online)"
echo 1 > /sys/devices/system/cpu/cpu1/online; echo "after on : $(cat /sys/devices/system/cpu/online)"
dmesg | tail -3
/c5_hotplug 50; dmesg -c > /dev/null
echo "--- c5_smc"; insmod /c5_smc.ko; dmesg | grep "c5_smc:"
```

initramfs는 랩 rootfs를 복사해 파일을 더하고 다시 묶는다(랩은 읽기 전용).

```sh
gcc -static -O2 -Wall -Wextra /Users/donh/.elinux_scratch/C5/c5_hotplug.c -o c5_hotplug
rm -rf rootfs && cp -a /var/lab/build/rootfs rootfs
cp mod/c5_smc.ko c5_hotplug rootfs/ && cp /Users/donh/.elinux_scratch/C5/c5_test.sh rootfs/
(cd rootfs && find . | cpio -o -H newc --quiet | gzip) > c5_initrd.gz
A="console=ttyAMA0 LAB_CMD=/c5_test.sh"
# (A) TF-A
timeout 150 qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -smp 4 -m 1057 -nographic \
  -bios tfa-build/qemu/debug/qemu_fw.bios -kernel /var/lab/out/Image -initrd c5_initrd.gz -append "$A"
# (B) QEMU 내장 PSCI
timeout 150 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 4 -m 1057 -nographic \
  -kernel /var/lab/out/Image -initrd c5_initrd.gz -append "$A"
```

(A) TF-A 경로의 앞부분:

```text
--- /proc/device-tree/psci
compatible   cpu_off      cpu_on       cpu_suspend  method       name
compatible = arm,psci-1.0 arm,psci-0.2 arm,psci 
method     = smc 
cpu_on = 0xc4000003
cpu_off = 0x84000002
--- cpu0 enable-method: psci
--- debugfs psci
PSCIv1.1
SMC Calling Convention v1.5
OSI is not supported
Original StateID format is used
--- dmesg psci/smp
[    0.000000] psci: probing for conduit method from DT.
[    0.000000] psci: PSCIv1.1 detected in firmware.
[    0.000000] psci: Using standard PSCI v0.2 function IDs
[    0.000000] psci: MIGRATE_INFO_TYPE not supported.
[    0.000000] psci: SMC Calling Convention v1.5
[    0.984217] smp: Bringing up secondary CPUs ...
[    1.042295] CPU1: Booted secondary processor 0x0000000001 [0x411fd070]
[    1.235092] CPU2: Booted secondary processor 0x0000000002 [0x411fd070]
[    1.260474] CPU3: Booted secondary processor 0x0000000003 [0x411fd070]
[    1.272585] smp: Brought up 1 node, 4 CPUs
[    1.272665] SMP: Total of 4 processors activated.
```

(B) QEMU 내장 PSCI 경로의 같은 부분:

```text
--- /proc/device-tree/psci
compatible   cpu_on       method       name
cpu_off      cpu_suspend  migrate
compatible = arm,psci-1.0 arm,psci-0.2 arm,psci 
method     = hvc 
cpu_on = 0xc4000003
cpu_off = 0x84000002
--- cpu0 enable-method: psci
--- debugfs psci
PSCIv1.1
SMC Calling Convention v1.0 is assumed
OSI is not supported
Original StateID format is used
Trusted OS migration not required
MIGRATE_INFO_TYPE is supported
--- dmesg psci/smp
[    0.000000] psci: probing for conduit method from DT.
[    0.000000] psci: PSCIv1.1 detected in firmware.
[    0.000000] psci: Using standard PSCI v0.2 function IDs
[    0.000000] psci: Trusted OS migration not required
[    0.000000] psci: SMC Calling Convention v1.0
[    0.512823] smp: Bringing up secondary CPUs ...
[    0.582324] CPU1: Booted secondary processor 0x0000000001 [0x411fd070]
[    0.640069] CPU2: Booted secondary processor 0x0000000002 [0x411fd070]
[    0.655477] CPU3: Booted secondary processor 0x0000000003 [0x411fd070]
[    0.661547] smp: Brought up 1 node, 4 CPUs
[    0.661609] SMP: Total of 4 processors activated.
```

출력에서 볼 것 (`ls`의 색 코드는 지웠다):

| 항목 | TF-A (EL3 펌웨어) | QEMU 내장 PSCI (EL3 없음) | 의미 |
|---|---|---|---|
| `method` | `smc` | `hvc` | 누가 받나: EL3 vs "가상의 하이퍼바이저" |
| PSCI 버전 | 1.1 | 1.1 | 둘 다 같은 표준 구현 |
| SMCCC 버전 | v1.5 (`SMCCC_VERSION`에 응답) | v1.0 "is assumed" | QEMU는 `SMCCC_VERSION`을 모른다 → §5에서 큰 차이를 만든다 |
| `MIGRATE_INFO_TYPE` | not supported | supported (TOS 없음) | Trusted OS 이주 정보 — TF-A 빌드에 BL32가 없어서 미지원 |
| DT `migrate` 속성 | 없음 | 있음 | DT 노드를 쓴 주체가 다르다(TF-A BL2 vs QEMU) |
| secondary 부팅 | 둘 다 `CPU1..3: Booted secondary processor` | 같음 | 커널 코드는 완전히 같은 경로(`cpu_psci_cpu_boot`) |

`/sys/kernel/debug/psci`는 커널 `drivers/firmware/psci/psci.c`의 `psci_debugfs_read()`가 SMC로 펌웨어에 물어 본 결과를 보여 주는 파일이다. BSP를 받으면 제일 먼저 `cat`해 볼 만하다.

### 4.4 secondary CPU는 어떻게 켜지나 — hold pen

리셋이 풀리면 **모든 코어가 동시에** BL1의 리셋 벡터에서 출발한다. BL1은 MPIDR을 보고 "나는 primary(코어 0)인가"를 판단하고, 아니면 `plat_secondary_cold_boot_setup`으로 보내 **hold pen**(대기 우리)에 가둔다. qemu 플랫폼에서는 Secure SRAM 맨 앞 4 KB(`PLAT_QEMU_HOLD_BASE = 0x0e000000`)의 코어별 슬롯을 `wfe`로 기다리며 계속 확인한다. Don이 멀티코어 MCU에서 "코어 1은 메일박스 레지스터를 폴링하다가 주소가 써지면 점프"로 짰던 것과 정확히 같은 구조다.

```text
/* plat/qemu/common/aarch64/plat_helpers.S */
func plat_secondary_cold_boot_setup
	bl	plat_my_core_pos
	mov_imm	x1, PLAT_QEMU_HOLD_BASE
	plat_hold_pen_wait_and_jump x1, x0, x2      /* magic 확인 → entry 읽기 → wfe 반복 */
endfunc plat_secondary_cold_boot_setup

/* plat/qemu/common/qemu_pm.c — PSCI CPU_ON의 플랫폼 부분 */
static int qemu_pwr_domain_on(u_register_t mpidr)
{
	unsigned int pos = plat_core_pos_by_mpidr(mpidr);

	plat_hold_pen_signal((struct hold_slot *)PLAT_QEMU_HOLD_BASE,
			pos, secure_entrypoint);   /* 슬롯에 warm entry 기록 + flush + SEV */
	return PSCI_E_SUCCESS;
}
```

그리고 커널 쪽은 이것이 전부다.

```c
/* arch/arm64/kernel/psci.c (6.18) */
static int cpu_psci_cpu_boot(unsigned int cpu)
{
	phys_addr_t pa_secondary_entry = __pa_symbol(secondary_entry);
	int err = psci_ops.cpu_on(cpu_logical_map(cpu), pa_secondary_entry);
	if (err && err != -EPERM)
		pr_err("failed to boot CPU%d (%d)\n", cpu, err);

	return err;
}
```

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="40" y="14" width="140" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="39" text-anchor="middle" font-size="13">CPU0 · Linux EL1</text>
  <rect x="270" y="14" width="140" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="39" text-anchor="middle" font-size="13">BL31 · EL3</text>
  <rect x="500" y="14" width="140" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="39" text-anchor="middle" font-size="13">CPU1</text>
  <line x1="110" y1="54" x2="110" y2="355" stroke="currentColor" stroke-dasharray="4 4" stroke-opacity="0.5"/>
  <line x1="340" y1="54" x2="340" y2="355" stroke="currentColor" stroke-dasharray="4 4" stroke-opacity="0.5"/>
  <line x1="570" y1="54" x2="570" y2="355" stroke="currentColor" stroke-dasharray="4 4" stroke-opacity="0.5"/>
  <rect x="470" y="70" width="200" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="88" text-anchor="middle" font-size="12">리셋 후 BL1 hold pen</text>
  <text x="570" y="105" text-anchor="middle" font-size="12">슬롯 확인 → wfe 반복</text>
  <line x1="110" y1="140" x2="338" y2="140" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="224" y="132" text-anchor="middle" font-size="12">smc CPU_ON(mpidr=1, entry)</text>
  <rect x="250" y="152" width="180" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="170" text-anchor="middle" font-size="12">slot[1] = warm entry</text>
  <text x="340" y="187" text-anchor="middle" font-size="12">flush + SEV</text>
  <line x1="338" y1="214" x2="112" y2="214" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="224" y="208" text-anchor="middle" font-size="12">ERET, x0 = 0</text>
  <line x1="430" y1="178" x2="568" y2="132" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah3)"/>
  <text x="515" y="172" text-anchor="middle" font-size="12">SEV 이벤트</text>
  <line x1="568" y1="240" x2="342" y2="240" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="455" y="234" text-anchor="middle" font-size="12">warm entry로 점프</text>
  <rect x="250" y="252" width="180" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="276" text-anchor="middle" font-size="12">CPU1 EL3 초기화</text>
  <line x1="342" y1="306" x2="568" y2="306" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="455" y="300" text-anchor="middle" font-size="12">ERET → secondary_entry</text>
  <text x="570" y="336" text-anchor="middle" font-size="12">"CPU1: Booted secondary"</text>
  <rect x="20" y="240" width="180" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="258" text-anchor="middle" font-size="12">CPU1 online을</text>
  <text x="110" y="275" text-anchor="middle" font-size="12">completion으로 대기</text>
</svg>
```
그림 4 — PSCI `CPU_ON`의 실제 동작. SMC 자체는 금방 돌아오고(CPU0), 진짜 일은 CPU1이 EL3에서 자기 초기화를 마친 뒤 Linux의 `secondary_entry`로 ERET하면서 일어난다. OP-TEE가 있으면 그 사이에 BL32의 CPU on 훅을 먼저 거친다(§6).

### 4.5 코드로 확인 — CPU 핫플러그 (`CPU_OFF` ↔ `CPU_ON`)

Linux는 부팅 후에도 코어를 내렸다 올릴 수 있다(`CONFIG_HOTPLUG_CPU`). sysfs에 0을 쓰면 그 코어가 스스로 `CPU_OFF`를 부르고, 다른 코어가 `AFFINITY_INFO`로 "정말 꺼졌나"를 폴링한다. 1을 쓰면 다시 `CPU_ON`이다. (A) TF-A 경로에서 `c5_test.sh`의 그 부분:

```text
--- hotplug cpu1
0-3
[    5.335001] psci: CPU1 killed (polled 0 ms)
after off: 0,2-3
[    5.413329] Detected PIPT I-cache on CPU1
[    5.417158] CPU1: Booted secondary processor 0x0000000001 [0x411fd070]
after on : 0-3
```

출력에서 볼 것: `CPU1 killed (polled 0 ms)`는 `cpu_psci_cpu_kill()`이 `AFFINITY_INFO`를 최대 100 ms 동안 폴링하다가 OFF(1)를 받은 순간 찍는 메시지다. 이 메시지 대신 `CPU1 may not have shut down cleanly (AFFINITY_INFO reports 0)`가 나오면 펌웨어가 코어를 실제로 내리지 못한 것이다. 다시 켤 때 나오는 `Booted secondary processor`는 부팅 때와 **같은 함수**(`cpu_psci_cpu_boot` → `CPU_ON`)의 결과다. TF-A의 qemu 구현은 CPU_OFF 때 코어를 진짜로 끄는 대신 MMU를 끄고 다시 hold pen으로 보낸다(`qemu_pwr_domain_pwr_down_wfi()` → `plat_secondary_cold_boot_setup()`). 실제 SoC라면 이 자리에서 전원 컨트롤러 레지스터를 쓴다.

### 4.6 측정 — secondary CPU online/offline 지연

핫플러그 한 번이 얼마나 걸리는지 재 보자. 실제 제품에서 이 숫자는 "코어를 내려 전력을 아끼는 정책이 지연 요구사항을 깨는가"를 판단할 때 쓴다(N5).

```c
// c5_hotplug.c - cpu1을 N번 offline/online 하며 걸린 시간을 잰다 (PSCI CPU_OFF/CPU_ON 경로)
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

static double now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static double set_online(const char *path, char v)
{
	int fd = open(path, O_WRONLY);
	if (fd < 0) { perror("open"); exit(1); }
	double t0 = now_us();
	if (write(fd, &v, 1) != 1) { perror("write"); exit(1); }
	double t1 = now_us();
	close(fd);
	return t1 - t0;
}

static int cmp(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
	const char *p = "/sys/devices/system/cpu/cpu1/online";
	int n = argc > 1 ? atoi(argv[1]) : 20;
	double off[200], on[200];
	if (n > 200) n = 200;
	for (int i = 0; i < n; i++) {
		off[i] = set_online(p, '0');
		on[i]  = set_online(p, '1');
	}
	qsort(off, n, sizeof(double), cmp);
	qsort(on, n, sizeof(double), cmp);
	printf("n=%d  offline(us): min %.0f med %.0f max %.0f\n", n, off[0], off[n/2], off[n-1]);
	printf("n=%d  online (us): min %.0f med %.0f max %.0f\n", n, on[0], on[n/2], on[n-1]);
	return 0;
}
```

세 가지 펌웨어 구성(QEMU 내장 PSCI / TF-A / TF-A + OP-TEE)에서 `LAB_CMD=/c5_hotplug~50`으로 **하나씩 순서대로** 두 번씩 부팅했다(OP-TEE 구성은 §6의 expect 스크립트로 부팅).

```text
== run 1 QEMU-PSCI(hvc)
n=50  offline(us): min 7622 med 14265 max 60250
n=50  online (us): min 1407 med 1966 max 25309
== run 1 TF-A(smc)
n=50  offline(us): min 7541 med 17341 max 46920
n=50  online (us): min 1519 med 2226 max 26468
== run 1 TF-A+OP-TEE(smc)
n=50  offline(us): min 8757 med 16586 max 43160
n=50  online (us): min 1935 med 2512 max 30593
== run 2 QEMU-PSCI(hvc)
n=50  offline(us): min 7464 med 15946 max 43549
n=50  online (us): min 1559 med 2447 max 20961
== run 2 TF-A(smc)
n=50  offline(us): min 7327 med 18749 max 48816
n=50  online (us): min 1513 med 2230 max 26392
== run 2 TF-A+OP-TEE(smc)
n=50  offline(us): min 8433 med 19494 max 60204
n=50  online (us): min 1984 med 3607 max 38921
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <text x="200" y="24" text-anchor="middle" font-size="14" font-weight="bold">online (CPU_ON 경로)</text>
  <line x1="60" y1="250" x2="340" y2="250" stroke="currentColor"/>
  <line x1="60" y1="50" x2="60" y2="250" stroke="currentColor"/>
  <line x1="56" y1="250.0" x2="60" y2="250.0" stroke="currentColor"/>
  <text x="52" y="254.0" text-anchor="end" font-size="12">0</text>
  <line x1="56" y1="202.5" x2="60" y2="202.5" stroke="currentColor"/>
  <text x="52" y="206.5" text-anchor="end" font-size="12">1</text>
  <line x1="56" y1="155.0" x2="60" y2="155.0" stroke="currentColor"/>
  <text x="52" y="159.0" text-anchor="end" font-size="12">2</text>
  <line x1="56" y1="107.5" x2="60" y2="107.5" stroke="currentColor"/>
  <text x="52" y="111.5" text-anchor="end" font-size="12">3</text>
  <line x1="56" y1="60.0" x2="60" y2="60.0" stroke="currentColor"/>
  <text x="52" y="64.0" text-anchor="end" font-size="12">4</text>
  <rect x="88.0" y="145.0" width="56" height="105.0" fill="#888" fill-opacity="0.7" stroke="#888"/>
  <text x="116.0" y="139.0" text-anchor="middle" font-size="12">2.2 ms</text>
  <rect x="172.0" y="144.1" width="56" height="105.9" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="200.0" y="138.1" text-anchor="middle" font-size="12">2.2 ms</text>
  <rect x="256.0" y="104.6" width="56" height="145.4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="284.0" y="98.6" text-anchor="middle" font-size="12">3.1 ms</text>
  <text x="30" y="42" font-size="12">ms</text>
  <text x="530" y="24" text-anchor="middle" font-size="14" font-weight="bold">offline (CPU_OFF 경로)</text>
  <line x1="390" y1="250" x2="670" y2="250" stroke="currentColor"/>
  <line x1="390" y1="50" x2="390" y2="250" stroke="currentColor"/>
  <line x1="386" y1="250.0" x2="390" y2="250.0" stroke="currentColor"/>
  <text x="382" y="254.0" text-anchor="end" font-size="12">0</text>
  <line x1="386" y1="202.5" x2="390" y2="202.5" stroke="currentColor"/>
  <text x="382" y="206.5" text-anchor="end" font-size="12">5</text>
  <line x1="386" y1="155.0" x2="390" y2="155.0" stroke="currentColor"/>
  <text x="382" y="159.0" text-anchor="end" font-size="12">10</text>
  <line x1="386" y1="107.5" x2="390" y2="107.5" stroke="currentColor"/>
  <text x="382" y="111.5" text-anchor="end" font-size="12">15</text>
  <line x1="386" y1="60.0" x2="390" y2="60.0" stroke="currentColor"/>
  <text x="382" y="64.0" text-anchor="end" font-size="12">20</text>
  <rect x="418.0" y="106.5" width="56" height="143.5" fill="#888" fill-opacity="0.7" stroke="#888"/>
  <text x="446.0" y="100.5" text-anchor="middle" font-size="12">15.1 ms</text>
  <rect x="502.0" y="78.6" width="56" height="171.4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="530.0" y="72.6" text-anchor="middle" font-size="12">18.0 ms</text>
  <rect x="586.0" y="78.6" width="56" height="171.4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="614.0" y="72.6" text-anchor="middle" font-size="12">18.0 ms</text>
  <text x="360" y="42" font-size="12">ms</text>
  <rect x="70" y="278" width="14" height="14" fill="#888" fill-opacity="0.7" stroke="#888"/>
  <text x="90" y="290" font-size="12">QEMU PSCI (hvc)</text>
  <rect x="270" y="278" width="14" height="14" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="290" y="290" font-size="12">TF-A (smc)</text>
  <rect x="450" y="278" width="14" height="14" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="470" y="290" font-size="12">TF-A + OP-TEE</text>
  <text x="340" y="312" text-anchor="middle" font-size="12">QEMU TCG, -smp 4, cpu1 50회 왕복, 2회 실행 중앙값의 평균</text>
</svg>
```
그림 5 — 핫플러그 지연(중앙값). 막대 높이는 위 출력의 두 실행 중앙값을 평균한 값이다.

출력에서 볼 것:

- **online이 offline보다 훨씬 빠르다(약 2 ms vs 15~18 ms)**. offline은 그 코어의 태스크·타이머·IRQ를 다른 코어로 옮기고, RCU 등 여러 hotplug 상태 콜백을 거꾸로 밟은 뒤에야 `CPU_OFF`를 부르고, 그 후 `AFFINITY_INFO`로 꺼졌는지 확인한다(아직 ON이면 `usleep_range(100, 1000)`으로 쉬며 다시 묻는다). 지연의 대부분은 PSCI가 아니라 **커널의 hotplug 상태 머신**이다.
- **펌웨어 차이는 작다.** QEMU 내장 PSCI(펌웨어 코드 없음)와 TF-A의 차이는 수백 µs 수준이고, 두 실행 사이 흔들림과 비슷한 크기다. OP-TEE를 넣으면 online이 조금 늘어난다(BL31이 BL32의 CPU on 훅을 먼저 부른다 — §6에서 OP-TEE 로그로 확인).
- **측정 함정**: 처음엔 `c5_test.sh` 안에서 같은 측정을 했는데 online 중앙값이 22~27 ms로 10배 느렸다. 그때는 QEMU 두 개를 **동시에** 돌렸고 같은 VM에서 다른 작업도 돌고 있었다. TCG 에뮬레이션은 호스트 CPU 경쟁에 매우 민감하다 — 숫자를 비교할 때는 하나씩, 같은 조건에서 돌린다. 그리고 이것은 에뮬레이터 숫자다. 실제 실리콘의 절댓값을 이 그래프로 말하면 안 된다.

### 4.7 시스템 전원 — SYSTEM_OFF, SYSTEM_RESET, suspend

`LAB_CMD`가 끝나면 init이 `poweroff -f`를 부른다. 커널은 `psci_sys_poweroff()` → `SYSTEM_OFF` SMC를 보내고, BL31은 돌아오지 않는다. 디버그 BL31은 끄기 직전에 전원 도메인 지도를 찍는다(c5_test 실행의 마지막 줄들, `MPID 0xffff...` 빈 슬롯 줄은 생략).

```text
[   10.842210] reboot: Power down
INFO:    PSCI Power Domain Map:
INFO:      Domain Node : Level 1, parent_node 4294967295, State ON (0x0)
INFO:      Domain Node : Level 1, parent_node 4294967295, State OFF (0x2)
INFO:      CPU Node : MPID 0x0, parent_node 0, State ON (0x0)
INFO:      CPU Node : MPID 0x1, parent_node 0, State ON (0x0)
INFO:      CPU Node : MPID 0x2, parent_node 0, State ON (0x0)
INFO:      CPU Node : MPID 0x3, parent_node 0, State ON (0x0)
ERROR:   QEMU System Power off: with GPIO.
```

출력에서 볼 것: BL31이 관리하는 PSCI 전원 트리 — 클러스터(Level 1) 2개, 그 아래 CPU 노드. qemu 플랫폼(AArch64)은 `PLATFORM_CLUSTER_COUNT = 2`, 클러스터당 16코어 슬롯(`PLATFORM_CPU_PER_CLUSTER_SHIFT = 4`)으로 총 32개 CPU 노드를 잡아 두고, `-smp 4`라 첫 클러스터의 MPID 0~3만 실제 코어다. 마지막 줄은 `qemu_system_off()`가 QEMU의 **secure GPIO**(`SECURE_GPIO_BASE = 0x090b0000`) 핀을 토글해 QEMU에 전원 차단을 알리는 것이다(ERROR 레벨로 찍히는 것은 그냥 로그 레벨 선택이다). 실제 보드에서는 이 자리가 PMIC에 I2C/SPMI로 "전원 꺼"를 보내는 코드다 — Don이 SPMI 버스를 디버깅했던 바로 그 PMIC다.

suspend는 이 플랫폼에서 지원하지 않는다. §5의 모듈이 `PSCI_FEATURES(SYSTEM_SUSPEND64)`에 `-1`(NOT_SUPPORTED)을 받는데, 소스를 보면 이유가 명확하다: `qemu_pwr_domain_suspend()`의 본문이 `assert(0);`이다. 실제 SoC의 TF-A는 여기서 DDR self-refresh 진입, 웨이크업 소스 설정을 한다. 리눅스 쪽 `CPU_SUSPEND`는 cpuidle 드라이버(`CONFIG_ARM_PSCI_CPUIDLE`)가 DT의 `idle-states`(`arm,idle-state`, `arm,psci-suspend-param`)를 보고 부른다(N5).

---

## 5. SMC Calling Convention — Linux에서 EL3를 직접 불러 보기

### 5.1 function ID의 구조

PSCI는 SMCCC(SMC Calling Convention, Arm 문서 DEN0028) 위에 정의된 "서비스 하나"다. SMCCC는 32비트 function ID를 이렇게 나눈다.

```
 31   30   29 ......... 24  23 ..... 17  16   15 ............... 0
+----+----+----------------+------------+----+--------------------+
| F  | CC |  OEN (owner)   |  reserved  | SV |  function number   |
+----+----+----------------+------------+----+--------------------+
 F  = 1 fast call (atomic, no preemption) / 0 yielding call
 CC = 1 SMC64 (x registers) / 0 SMC32 (w registers)
 OEN: 0 Arm arch, 1 CPU, 2 SiP, 3 OEM, 4 Standard secure (PSCI),
      5 Standard hyp, 6 Vendor hyp, 48-49 Trusted App, 50-63 Trusted OS
 SV = SVE live-state hint (SMCCC v1.3+), otherwise 0
```

OEN(Owning Entity Number)이 "어느 서비스에 보내는 요청인가"다. BL31의 디스패처는 이 6비트로 서비스 핸들러를 고른다. 칩 벤더가 자기 SoC 전용 서비스(예: 클럭 설정, eFuse 읽기)를 만들 땐 **SiP(2)** 영역을 쓴다. 디코더로 확인해 보자.

```c
// c5_fid.c - SMCCC function ID 32비트를 필드별로 해독한다 (SMCCC 스펙의 비트 배치)
#include <stdio.h>
#include <stdint.h>

static const char *owner(unsigned oen)
{
	if (oen == 0) return "Arm Architecture";
	if (oen == 1) return "CPU Service";
	if (oen == 2) return "SiP (SoC vendor)";
	if (oen == 3) return "OEM";
	if (oen == 4) return "Standard Secure (PSCI...)";
	if (oen == 5) return "Standard Hypervisor";
	if (oen == 6) return "Vendor Hypervisor";
	if (oen >= 48 && oen <= 49) return "Trusted Application";
	if (oen >= 50 && oen <= 63) return "Trusted OS";
	return "reserved";
}

int main(void)
{
	const struct { uint32_t fid; const char *name; } t[] = {
		{ 0x84000000, "PSCI_VERSION" },
		{ 0xC4000003, "PSCI CPU_ON (64)" },
		{ 0x84000002, "PSCI CPU_OFF" },
		{ 0x84000008, "PSCI SYSTEM_OFF" },
		{ 0x80000000, "SMCCC_VERSION" },
		{ 0x80008000, "SMCCC_ARCH_WORKAROUND_1" },
		{ 0x8200ff00, "(c5 test: unknown SiP)" },
		{ 0xBF00FF01, "OPTEE CALLS_UID" },
		{ 0x32000004, "OPTEE CALL_WITH_ARG" },
	};
	printf("%-10s %-26s %-5s %-5s %-3s %-26s %s\n",
	       "fid", "name", "type", "conv", "oen", "owner", "func");
	for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		uint32_t f = t[i].fid;
		printf("0x%08X %-26s %-5s %-5s %-3u %-26s 0x%04X\n", f, t[i].name,
		       (f >> 31) & 1 ? "fast" : "yield",
		       (f >> 30) & 1 ? "SMC64" : "SMC32",
		       (f >> 24) & 0x3f, owner((f >> 24) & 0x3f), f & 0xffff);
	}
	return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/C5/c5_fid.c -o /tmp/C5/c5_fid && /tmp/C5/c5_fid
```

```text
fid        name                       type  conv  oen owner                      func
0x84000000 PSCI_VERSION               fast  SMC32 4   Standard Secure (PSCI...)  0x0000
0xC4000003 PSCI CPU_ON (64)           fast  SMC64 4   Standard Secure (PSCI...)  0x0003
0x84000002 PSCI CPU_OFF               fast  SMC32 4   Standard Secure (PSCI...)  0x0002
0x84000008 PSCI SYSTEM_OFF            fast  SMC32 4   Standard Secure (PSCI...)  0x0008
0x80000000 SMCCC_VERSION              fast  SMC32 0   Arm Architecture           0x0000
0x80008000 SMCCC_ARCH_WORKAROUND_1    fast  SMC32 0   Arm Architecture           0x8000
0x8200FF00 (c5 test: unknown SiP)     fast  SMC32 2   SiP (SoC vendor)           0xFF00
0xBF00FF01 OPTEE CALLS_UID            fast  SMC32 63  Trusted OS                 0xFF01
0x32000004 OPTEE CALL_WITH_ARG        yield SMC32 50  Trusted OS                 0x0004
```

출력에서 볼 것: PSCI는 OEN 4, SMCCC 자체 정보는 OEN 0이다. OP-TEE의 실제 TA 호출(`CALL_WITH_ARG`)만 **yielding call**(bit 31 = 0)이다 — 오래 걸릴 수 있어서 보안 세계 실행 중에 Linux 인터럽트가 오면 중단됐다가 다시 이어진다. fast call은 원자적으로 끝나야 하는 짧은 요청이다. (이 두 OP-TEE ID는 커널 `drivers/tee/optee/optee_smc.h`의 매크로 정의에서 계산했다.)

### 5.2 레지스터 규약

| 레지스터 | 호출 시 | 복귀 시 | 비고 |
|---|---|---|---|
| x0 (w0) | function ID | 결과 0 (보통 상태 코드) | |
| x1 ~ x7 | 인자 1~7 | x1~x3: 결과 1~3 | SMCCC v1.2부터 x1~x17까지 인자·결과로 확장 |
| x4 ~ x17 | — | v1.0: 망가질 수 있음 / v1.1+: 결과로 안 쓰면 보존 | v1.1의 이 약속 덕분에 커널이 인라인 asm으로 가볍게 부를 수 있다 |
| x18 ~ x30, sp | — | 보존 | 일반 AAPCS64 callee-saved와 같다 |

Don에게 익숙하게 말하면: **SMC는 "레지스터로 인자를 넘기는 동기 RPC"**다. 메모리 포인터를 넘길 때는 반드시 상대가 접근할 수 있는(그리고 캐시 일관성이 맞는) 공유 메모리여야 한다 — OP-TEE의 `optee_shm` 예약 영역이 그것이다(§6).

### 5.3 커널 API

| API | 하는 일 | 언제 쓰나 |
|---|---|---|
| `arm_smccc_smc(a0..a7, &res)` | 무조건 `smc #0` | conduit이 smc임을 확실히 알 때 (함수 호출 형태, x4~x17 보존 가정 없음) |
| `arm_smccc_hvc(a0..a7, &res)` | 무조건 `hvc #0` | 하이퍼바이저 서비스 |
| `arm_smccc_1_1_invoke(fid, args..., &res)` | 커널이 알아낸 conduit(smc/hvc)으로 인라인 호출 | SMCCC v1.1+ 펌웨어의 서비스를 부를 때의 표준 |
| `arm_smccc_1_1_get_conduit()` | `SMCCC_CONDUIT_NONE/SMC/HVC` | **v1.1 미만이면 NONE을 돌려준다** |
| `arm_smccc_get_version()` | 펌웨어가 응답한 SMCCC 버전 | 기능 판단 |

### 5.4 코드로 확인 — 커널 모듈 `c5_smc`로 EL3 직접 호출

PSCI 드라이버와 같은 방식(DT `/psci`의 `method`)으로 conduit을 고르고, PSCI·SMCCC 함수 몇 개를 부른 뒤 왕복 시간을 잰다. 비교를 위해 `arm_smccc_1_1_invoke()`도 한 번 부른다.

```c
// c5_smc.c - EL1(Linux)에서 SMCCC로 펌웨어(EL3 TF-A 또는 QEMU)를 직접 부른다
#include <linux/module.h>
#include <linux/arm-smccc.h>
#include <linux/of.h>
#include <linux/ktime.h>
#include <uapi/linux/psci.h>
static bool use_hvc;      /* DT /psci method 값으로 결정 (커널 PSCI 드라이버와 같은 방식) */

static long fw(u32 fid, unsigned long a1, unsigned long a2)
{
	struct arm_smccc_res r;
	if (use_hvc)
		arm_smccc_hvc(fid, a1, a2, 0, 0, 0, 0, 0, &r);
	else
		arm_smccc_smc(fid, a1, a2, 0, 0, 0, 0, 0, &r);
	return (long)r.a0;
}
static int __init c5_smc_init(void)
{
	struct device_node *np = of_find_node_by_path("/psci");
	const char *method = "?";
	struct arm_smccc_res r;
	u64 t, best = U64_MAX;
	long v; int i;

	if (!np || of_property_read_string(np, "method", &method))
		return -ENODEV;
	of_node_put(np);
	use_hvc = !strcmp(method, "hvc");

	arm_smccc_1_1_invoke(PSCI_0_2_FN_PSCI_VERSION, &r);  /* 1.1 helper: conduit 자동 */
	pr_info("c5_smc: DT method=%s, 1_1 conduit=%d, 1_1_invoke(PSCI_VERSION)=%ld\n",
		method, arm_smccc_1_1_get_conduit(), (long)r.a0);

	v = fw(PSCI_0_2_FN_PSCI_VERSION, 0, 0);
	pr_info("c5_smc: PSCI_VERSION  0x%08x -> 0x%lx (v%ld.%ld)\n",
		PSCI_0_2_FN_PSCI_VERSION, v, v >> 16, v & 0xffff);
	v = fw(ARM_SMCCC_VERSION_FUNC_ID, 0, 0);
	pr_info("c5_smc: SMCCC_VERSION 0x%08x -> 0x%lx\n", ARM_SMCCC_VERSION_FUNC_ID, v);
	pr_info("c5_smc: FEATURES(CPU_ON64)=%ld FEATURES(SYSTEM_SUSPEND64)=%ld\n",
		fw(PSCI_1_0_FN_PSCI_FEATURES, PSCI_0_2_FN64_CPU_ON, 0),
		fw(PSCI_1_0_FN_PSCI_FEATURES, PSCI_1_0_FN64_SYSTEM_SUSPEND, 0));
	pr_info("c5_smc: AFFINITY_INFO(mpidr=1,lvl=0)=%ld  (lvl=garbage 7)=%ld\n",
		fw(PSCI_0_2_FN64_AFFINITY_INFO, 1, 0), fw(PSCI_0_2_FN64_AFFINITY_INFO, 1, 7));
	pr_info("c5_smc: ARCH_FEATURES(WORKAROUND_1)=%ld  unknown 0x8200ff00=%ld\n",
		fw(ARM_SMCCC_ARCH_FEATURES_FUNC_ID, ARM_SMCCC_ARCH_WORKAROUND_1, 0),
		fw(0x8200ff00, 0, 0));

	for (i = 0; i < 2000; i++) {          /* EL1 -> 펌웨어 -> EL1 왕복 최솟값 */
		t = ktime_get_ns();
		fw(PSCI_0_2_FN_PSCI_VERSION, 0, 0);
		t = ktime_get_ns() - t;
		best = min(best, t);
	}
	pr_info("c5_smc: PSCI_VERSION round trip min = %llu ns (2000 calls)\n", best);
	return 0;
}
static void __exit c5_smc_exit(void) { }
module_init(c5_smc_init); module_exit(c5_smc_exit);
MODULE_LICENSE("GPL"); MODULE_DESCRIPTION("C5 SMCCC/PSCI probe");
```

```sh
# Makefile 한 줄: obj-m := c5_smc.o
cd /var/tmp/C5/mod && make -C /var/lab/out/linux-build M=$PWD modules
```

```text
make[1]: Entering directory '/var/tmp/C5/mod'
  CC [M]  c5_smc.o
  MODPOST Module.symvers
  CC [M]  c5_smc.mod.o
  CC [M]  .module-common.o
  LD [M]  c5_smc.ko
make[1]: Leaving directory '/var/tmp/C5/mod'
```

(이 빌드 출력은 첫 버전 모듈의 것이고, 최종 버전도 같은 줄이 나오며 경고는 0개다.) 위 `c5_test.sh`의 마지막 줄이 이 모듈을 `insmod`한다. 콘솔과 `dmesg`가 같은 줄을 두 번 보여 주므로 한 벌만 옮긴다.

(A) TF-A (EL3 펌웨어, `smc`):

```text
[    9.387054] c5_smc: DT method=smc, 1_1 conduit=1, 1_1_invoke(PSCI_VERSION)=65537
[    9.387297] c5_smc: PSCI_VERSION  0x84000000 -> 0x10001 (v1.1)
[    9.387370] c5_smc: SMCCC_VERSION 0x80000000 -> 0x10005
[    9.387454] c5_smc: FEATURES(CPU_ON64)=0 FEATURES(SYSTEM_SUSPEND64)=-1
[    9.387548] c5_smc: AFFINITY_INFO(mpidr=1,lvl=0)=0  (lvl=garbage 7)=-2
[    9.387642] c5_smc: ARCH_FEATURES(WORKAROUND_1)=0  unknown 0x8200ff00=-1
[    9.455150] c5_smc: PSCI_VERSION round trip min = 19360 ns (2000 calls)
```

(B) QEMU 내장 PSCI (`hvc`):

```text
[   11.668643] c5_smc: DT method=hvc, 1_1 conduit=0, 1_1_invoke(PSCI_VERSION)=-1
[   11.668851] c5_smc: PSCI_VERSION  0x84000000 -> 0x10001 (v1.1)
[   11.668886] c5_smc: SMCCC_VERSION 0x80000000 -> 0xffffffffffffffff
[   11.668985] c5_smc: FEATURES(CPU_ON64)=0 FEATURES(SYSTEM_SUSPEND64)=-1
[   11.669061] c5_smc: AFFINITY_INFO(mpidr=1,lvl=0)=0  (lvl=garbage 7)=0
[   11.669099] c5_smc: ARCH_FEATURES(WORKAROUND_1)=-1  unknown 0x8200ff00=-1
[   11.673906] c5_smc: PSCI_VERSION round trip min = 416 ns (2000 calls)
```

출력에서 볼 것 — 한 줄씩 비교하면 이 노트의 핵심이 다 들어 있다.

1. **`arm_smccc_1_1_invoke`가 QEMU에서는 아무것도 안 부르고 -1을 돌려준다.** QEMU는 `SMCCC_VERSION`에 NOT_SUPPORTED(-1, 64비트로 0xffff…)로 답한다 → 커널 `psci_init_smccc()`가 "SMCCC v1.0"으로 판단 → `arm_smccc_1_1_get_conduit()`가 `SMCCC_CONDUIT_NONE`(0) → 1.1 헬퍼는 트랩 없이 바로 NOT_SUPPORTED를 채운다. TF-A에서는 conduit=1(SMC)이고 같은 호출이 `0x10001`(65537)을 돌려준다. **드라이버에서 1.1 헬퍼를 쓸 때 "-1이 왔다"가 "펌웨어가 거부했다"인지 "아예 안 불렀다"인지 구별해야 한다.**
2. **PSCI_VERSION은 둘 다 0x10001 = v1.1.** raw `arm_smccc_hvc`/`arm_smccc_smc`는 버전과 무관하게 명령을 실행하므로 QEMU에서도 답이 온다.
3. **`SMCCC_VERSION` 0x10005 = v1.5**(major는 bit 30:16, minor는 15:0). 부팅 로그의 `psci: SMC Calling Convention v1.5`와 같다.
4. **CPU_ON은 지원(0), SYSTEM_SUSPEND는 미지원(-1)** — §4.7의 `assert(0)`과 일치.
5. **`AFFINITY_INFO`의 두 번째 인자(level)에 쓰레기 7을 넣으면** TF-A는 `-2`(INVALID_PARAMETERS)로 거부하고, QEMU는 무시하고 0(ON)을 준다. 에뮬레이터에서 통과한 코드가 실제 펌웨어에서 실패하는 전형적 사례다. (1.1 헬퍼를 인자 개수를 줄여 부르면 안 넘긴 레지스터의 값은 정의되지 않는다는 점도 같이 기억하자.)
6. **`SMCCC_ARCH_WORKAROUND_1`**(Spectre v2 완화용 펌웨어 호출)은 TF-A가 구현(0), QEMU는 미구현(-1).
7. **아무도 모르는 SiP 함수는 -1**. BL31 디스패처가 OEN 2에 등록된 서비스가 없어 `smc_unknown()`으로 응답한 것이다.
8. **왕복 비용**: TF-A 19.4 µs vs QEMU 0.42 µs. QEMU 내장 PSCI는 TCG가 `hvc`를 만나면 호스트 C 함수로 바로 처리하지만, TF-A는 EL3 벡터 → 레지스터 저장 → C 디스패처 → 복원 → ERET을 **명령어 단위로 에뮬레이션**한다. 실리콘에서의 실제 SMC 비용과는 아무 관계가 없다(실리콘에서는 직접 재야 한다). 이 숫자가 말해 주는 건 "EL3 왕복은 공짜가 아니다, 그래서 핫패스에서 SMC를 부르지 않는다" 정도다.

```svg
<svg viewBox="0 0 680 220" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">PSCI_VERSION 한 번 왕복 (최솟값, 2000회)</text>
  <line x1="190.0" y1="40" x2="190.0" y2="170" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="190.0" y="188" text-anchor="middle" font-size="12">0</text>
  <line x1="295.0" y1="40" x2="295.0" y2="170" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="295.0" y="188" text-anchor="middle" font-size="12">5</text>
  <line x1="400.0" y1="40" x2="400.0" y2="170" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="400.0" y="188" text-anchor="middle" font-size="12">10</text>
  <line x1="505.0" y1="40" x2="505.0" y2="170" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="505.0" y="188" text-anchor="middle" font-size="12">15</text>
  <line x1="610.0" y1="40" x2="610.0" y2="170" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="610.0" y="188" text-anchor="middle" font-size="12">20</text>
  <text x="400.0" y="208" text-anchor="middle" font-size="12">µs (QEMU TCG 에뮬레이션 — 실리콘 수치가 아니다)</text>
  <text x="180" y="69" text-anchor="end" font-size="13">QEMU PSCI (hvc)</text>
  <rect x="190" y="50" width="8.7" height="28" fill="#888" fill-opacity="0.7" stroke="#888"/>
  <text x="206.7" y="69" font-size="12">416 ns</text>
  <text x="180" y="109" text-anchor="end" font-size="13">TF-A BL31 (smc)</text>
  <rect x="190" y="90" width="406.6" height="28" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="604.6" y="109" font-size="12">19.36 µs</text>
  <text x="180" y="149" text-anchor="end" font-size="13">TF-A + OP-TEE (smc)</text>
  <rect x="190" y="130" width="328.0" height="28" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="526.0" y="149" font-size="12">15.62 µs</text>
  <line x1="190" y1="40" x2="190" y2="172" stroke="currentColor"/>
</svg>
```
그림 6 — EL1 → 펌웨어 → EL1 왕복 최솟값. TF-A + OP-TEE 값(15.6 µs)은 §6 부팅에서 같은 모듈을 넣어 잰 것으로, 실행마다 수 µs씩 흔들린다(첫 버전 모듈로 잰 TF-A 값은 18.8 µs였다).

### 5.5 커널 안에서 EL3까지 — 호출 경로

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="150" y="22" text-anchor="middle" font-size="14" font-weight="bold">Linux · NS-EL1</text>
  <text x="530" y="22" text-anchor="middle" font-size="14" font-weight="bold">TF-A BL31 · EL3</text>
  <rect x="20" y="36" width="260" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="55" text-anchor="middle" font-size="12">echo 1 &gt; cpu1/online</text>
  <text x="150" y="73" text-anchor="middle" font-size="12">cpu_up() → cpu_psci_cpu_boot()</text>
  <rect x="20" y="100" width="260" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="119" text-anchor="middle" font-size="12">psci_ops.cpu_on(mpidr, pa)</text>
  <text x="150" y="137" text-anchor="middle" font-size="12">→ __invoke_psci_fn_smc()</text>
  <rect x="20" y="164" width="260" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="183" text-anchor="middle" font-size="12">arm_smccc_smc(): smc #0</text>
  <text x="150" y="201" text-anchor="middle" font-size="12">x0=0xC4000003 x1=mpidr x2=entry</text>
  <line x1="150" y1="82" x2="150" y2="98" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="150" y1="146" x2="150" y2="162" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="400" y="36" width="260" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="530" y="55" text-anchor="middle" font-size="12">벡터: lower EL sync (A64)</text>
  <text x="530" y="73" text-anchor="middle" font-size="12">prepare_el3_entry: 레지스터 저장</text>
  <rect x="400" y="100" width="260" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="530" y="119" text-anchor="middle" font-size="12">handler_sync_exception()</text>
  <text x="530" y="137" text-anchor="middle" font-size="12">EC=SMC64 → OEN 4 핸들러 찾기</text>
  <rect x="400" y="164" width="260" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="530" y="183" text-anchor="middle" font-size="12">std_svc_smc_handler()</text>
  <text x="530" y="201" text-anchor="middle" font-size="12">→ psci_smc_handler()</text>
  <rect x="400" y="228" width="260" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="530" y="247" text-anchor="middle" font-size="12">psci_cpu_on() → pwr_domain_on</text>
  <text x="530" y="265" text-anchor="middle" font-size="12">qemu: hold pen에 entry + SEV</text>
  <rect x="400" y="292" width="260" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="530" y="311" text-anchor="middle" font-size="12">el3_exit: 레지스터 복원</text>
  <text x="530" y="329" text-anchor="middle" font-size="12">x0 = 0 (PSCI_E_SUCCESS), ERET</text>
  <line x1="530" y1="82" x2="530" y2="98" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="530" y1="146" x2="530" y2="162" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="530" y1="210" x2="530" y2="226" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="530" y1="274" x2="530" y2="290" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="280" y1="187" x2="398" y2="70" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="352" y="140" text-anchor="middle" font-size="12">트랩</text>
  <rect x="20" y="292" width="260" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="311" text-anchor="middle" font-size="12">smc 다음 명령으로 복귀, x0 확인</text>
  <text x="150" y="329" text-anchor="middle" font-size="12">CPU1이 online 되기를 대기</text>
  <line x1="398" y1="315" x2="282" y2="315" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="340" y="360" text-anchor="middle" font-size="12">파일: arch/arm64/kernel/psci.c · drivers/firmware/psci/psci.c (Linux)</text>
  <text x="340" y="380" text-anchor="middle" font-size="12">runtime_exceptions.S · runtime_svc.c · lib/psci/ · qemu_pm.c (TF-A)</text>
</svg>
```
그림 7 — `echo 1 > online`이 EL3까지 내려갔다 돌아오는 경로. 왼쪽은 Don이 앞으로 드라이버에서 다룰 커널 코드, 오른쪽은 BSP 팀이 고치는 TF-A 코드다.

TF-A 쪽 C 디스패처의 핵심은 몇 줄이다(`common/runtime_svc.c`, v2.15).

```c
void handler_sync_exception(cpu_context_t *ctx)
{
	uint32_t smc_fid = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X0);
	...
	if (exc_class == EC_AARCH32_SMC || exc_class == EC_AARCH64_SMC) {
		...
		return sync_handler(ctx, smc_fid, scr_el3);   /* OEN으로 rt_svc 핸들러를 찾아 호출 */
	} else if (exc_class == EC_AARCH64_SYS) {
	...
```

Don 연결: 이것은 RTOS의 SVC 핸들러에서 "SVC 번호로 서비스 테이블을 인덱싱"하던 것과 같다. 차이는 서비스 테이블이 OEN별로 나뉘고(`DECLARE_RT_SVC` 매크로로 등록), 호출자가 다른 세계(NS)일 수 있다는 것뿐이다. 서비스가 `SCR_EL3.NS` 플래그를 보고 "Secure에서만 부를 수 있는 함수"를 거부할 수도 있다.

---

## 6. OP-TEE — BL32로 들어가는 보안 세계 OS

### 6.1 직관 — "Linux 옆의 작은 RTOS"

OP-TEE OS는 S-EL1에서 도는 작은 OS다. 그 위 S-EL0에서 **Trusted Application(TA)**이 돈다. TA는 UUID로 구분되는 작은 프로그램으로, 키를 들고 서명하거나, 암호화된 저장소를 관리하거나, 펌웨어 TPM(fTPM)을 구현한다. Linux 앱은 GlobalPlatform TEE Client API(`libteec`의 `TEEC_OpenSession`, `TEEC_InvokeCommand`)로 TA를 부르고, 커널의 `optee` 드라이버가 이를 SMC로 바꿔 BL31에 넘기고, BL31의 SPD(Secure Payload Dispatcher, `opteed`)가 세계를 바꿔 OP-TEE에 전달한다.

Don의 경험으로 치면: SSD 컨트롤러 안에서 암호 엔진과 키를 담당하던 별도 보안 코어(또는 보안 펌웨어 영역)를, **같은 Cortex-A 코어를 시간 나눠 쓰는 별도 OS**로 만든 것이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="160" y="22" text-anchor="middle" font-size="14" font-weight="bold">Non-secure</text>
  <text x="520" y="22" text-anchor="middle" font-size="14" font-weight="bold">Secure</text>
  <rect x="20" y="36" width="280" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160" y="57" text-anchor="middle" font-size="12">앱: TEEC_InvokeCommand(UUID, cmd)</text>
  <text x="160" y="75" text-anchor="middle" font-size="12">tee-supplicant: 파일·RPC 대행</text>
  <rect x="20" y="110" width="280" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160" y="131" text-anchor="middle" font-size="12">/dev/tee0 (클라이언트)</text>
  <text x="160" y="149" text-anchor="middle" font-size="12">/dev/teepriv0 (supplicant 전용)</text>
  <rect x="20" y="184" width="280" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160" y="205" text-anchor="middle" font-size="12">커널 optee 드라이버</text>
  <text x="160" y="223" text-anchor="middle" font-size="12">smc 0x32000004 + optee_shm 버퍼</text>
  <rect x="380" y="36" width="280" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="520" y="57" text-anchor="middle" font-size="12">TA (S-EL0)</text>
  <text x="520" y="75" text-anchor="middle" font-size="12">UUID로 식별 · 키·암호·fTPM</text>
  <rect x="380" y="110" width="280" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="520" y="131" text-anchor="middle" font-size="12">OP-TEE OS (S-EL1, BL32)</text>
  <text x="520" y="149" text-anchor="middle" font-size="12">0x0e100000 · 15 MB Secure DRAM</text>
  <rect x="20" y="258" width="640" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="279" text-anchor="middle" font-size="12">BL31 (EL3) + opteed SPD: NS 레지스터 저장 → SCR_EL3.NS=0 → S-EL1로 ERET</text>
  <text x="340" y="297" text-anchor="middle" font-size="12">돌아올 때는 OP-TEE가 smc로 "끝났다"를 알리고 반대로 전환</text>
  <line x1="160" y1="86" x2="160" y2="108" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="160" x2="160" y2="182" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="234" x2="160" y2="256" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="520" y1="256" x2="520" y2="162" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="520" y1="108" x2="520" y2="88" stroke="currentColor" marker-end="url(#ah5)"/>
</svg>
```
그림 8 — OP-TEE 호출 경로. Linux 쪽은 문자 디바이스 + 커널 드라이버라는 익숙한 구조이고(G4), 보안 세계로 넘어가는 유일한 길은 EL3의 SPD다.

### 6.2 코드로 확인 — OP-TEE 빌드와 TF-A에 BL32로 넣기

OP-TEE OS를 QEMU armv8a 플랫폼용으로 빌드한다(1분 정도 걸렸다). 64-bit TA 개발 키트만 만든다.

```sh
git clone -q --depth 1 -b 4.7.0 https://github.com/OP-TEE/optee_os.git c5_optee_os
cd c5_optee_os && make -j6 CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE64=aarch64-linux-gnu- \
  PLATFORM=vexpress-qemu_armv8a CFG_ARM64_core=y ta-targets=ta_arm64 CFG_TEE_CORE_LOG_LEVEL=2 \
  O=/var/tmp/C5/optee-out
ls -la /var/tmp/C5/optee-out/core/tee-*_v2.bin
grep -n "CFG_SHMEM_START\|CFG_TZDRAM_START" /var/tmp/C5/optee-out/conf.mk
```

```text
-rw-rw-r-- 1 donh donh     28 Oct 10 10:39 /var/tmp/C5/optee-out/core/tee-header_v2.bin
-rw-rw-r-- 1 donh donh      0 Oct 10 10:39 /var/tmp/C5/optee-out/core/tee-pageable_v2.bin
-rw-rw-r-- 1 donh donh 500184 Oct 10 10:39 /var/tmp/C5/optee-out/core/tee-pager_v2.bin
221:CFG_SHMEM_START=0x42000000
270:CFG_TZDRAM_START=0x0e100000
```

(`ls`와 `grep`의 출력을 한 블록에 이어 붙였다.) OP-TEE는 헤더(28바이트)·본체(pager)·페이징 부분(이 설정에선 0바이트)으로 나뉜다. TF-A는 이 셋을 BL32·BL32_EXTRA1·BL32_EXTRA2로 받고, `SPD=opteed`로 OP-TEE용 디스패처를 BL31에 넣는다.

```sh
O=/var/tmp/C5/optee-out/core
make -j6 CROSS_COMPILE=aarch64-linux-gnu- PLAT=qemu DEBUG=1 LOG_LEVEL=40 BL33=/var/lab/out/u-boot.bin \
  BL32=$O/tee-header_v2.bin BL32_EXTRA1=$O/tee-pager_v2.bin BL32_EXTRA2=$O/tee-pageable_v2.bin \
  SPD=opteed BL32_RAM_LOCATION=tdram BUILD_BASE=/var/tmp/C5/tee-build all fip
tfa-build/qemu/debug/tools/fiptool/fiptool info tee-build/qemu/debug/fip.bin
```

```text
Trusted Boot Firmware BL2: offset=0x128, size=0x942C, cmdline="--tb-fw"
EL3 Runtime Firmware BL31: offset=0x9554, size=0x1089B, cmdline="--soc-fw"
Secure Payload BL32 (Trusted OS): offset=0x19DEF, size=0x1C, cmdline="--tos-fw"
Secure Payload BL32 Extra1 (Trusted OS Extra1): offset=0x19E0B, size=0x7A1D8, cmdline="--tos-fw-extra1"
Non-Trusted Firmware BL33: offset=0x93FE3, size=0x16F3D0, cmdline="--nt-fw"
```

출력에서 볼 것: FIP에 `--tos-fw`(0x1C = 28바이트 헤더)와 `--tos-fw-extra1`(0x7A1D8 = 500,184바이트 본체)이 추가됐다. 크기 0인 EXTRA2는 들어가지 않았다.

### 6.3 코드로 확인 — 부팅: BL32 로드, 예약 메모리, `/dev/tee0`

처음에는 U-Boot의 자동 부팅이 `Failed to allocate memory for the image at 0x40400000`로 실패했다. OP-TEE가 DT에 **`optee_shm@42000000` 예약 영역(no-map)**을 추가했는데, U-Boot이 커널(0x40400000 + 42 MB)을 그 영역에 겹쳐 놓으려 했기 때문이다. 그래서 expect로 자동 부팅을 멈추고, 커널을 0x48000000에 직접 올린다.

```text
#!/usr/bin/expect -f
# c5_tee.exp - TF-A + OP-TEE + U-Boot: autoboot 멈추고 커널을 OP-TEE 예약 영역 밖에 올려 부팅
set timeout 120
spawn qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -smp 4 -m 1057 -nographic -serial mon:stdio -serial file:/var/tmp/C5/c5_optee_uart.log \
  -bios /var/tmp/C5/tee-build/qemu/debug/qemu_fw.bios \
  -kernel /var/lab/out/Image -initrd /var/tmp/C5/c5_initrd.gz -append "x"
expect "Hit any key"
send " "
expect "=> "
send "fdt addr \$fdtcontroladdr; fdt print /reserved-memory; fdt print /firmware\r"
expect "=> "
send "qfw load 0x48000000 0x50000000\r"
expect "=> "
send "setenv bootargs \"console=ttyAMA0 LAB_CMD=/c5_tee.sh;~insmod~/c5_smc.ko\"\r"
expect "=> "
send "booti 0x48000000 0x50000000:\$filesize \$fdtcontroladdr\r"
expect "reboot: Power down"
expect eof
```

두 번째 `-serial`은 QEMU의 두 번째 UART(secure UART)로, OP-TEE가 자기 로그를 여기에 찍는다. 게스트의 `/c5_tee.sh`는 `ls -l /dev/tee*`, `/proc/device-tree/firmware/optee` 확인, `dmesg | grep -i -e optee -e tee`, `ls /sys/bus/tee/devices/`를 한다. 결과(앞부분의 BL31 errata 줄과 U-Boot 배너는 생략):

```text
INFO:    BL2: Loading image id 4
INFO:    Loading image id=4 at address 0xe100000
INFO:    Image id=4 loaded: 0xe100000 - 0xe10001c
INFO:    OPTEE ep=0xe100000
INFO:    OPTEE header info:
INFO:          magic=0x4554504f
INFO:          version=0x2
INFO:          arch=0x1
INFO:          flags=0x0
INFO:          nb_images=0x1
INFO:    Handoff to BL32
INFO:    Using default arguments
INFO:    BL2: Loading image id 21
INFO:    Loading image id=21 at address 0xe100000
INFO:    Image id=21 loaded: 0xe100000 - 0xe17a1d8
INFO:    BL2: Skip loading image id 22
INFO:    BL2: Loading image id 5
...
INFO:    BL31: Initializing runtime services
INFO:    BL31: Initializing BL32
INFO:    BL31: Preparing for EL3 exit to normal world
...
=> fdt addr $fdtcontroladdr; fdt print /reserved-memory; fdt print /firmware
Working FDT set to 8054c9e0
reserved-memory {
	ranges;
	#size-cells = <0x00000002>;
	#address-cells = <0x00000002>;
	optee_core@e100000 {
		no-map;
		reg = <0x00000000 0x0e100000 0x00000000 0x00f00000>;
	};
	optee_shm@42000000 {
		no-map;
		reg = <0x00000000 0x42000000 0x00000000 0x00200000>;
	};
};
firmware {
	optee {
		interrupts = <0x00000000 0x000000bb 0x00000001>;
		method = "smc";
		compatible = "linaro,optee-tz";
	};
};
...
lab initramfs: 6.18.55 up 1.37s
crw-------    1 0        0         246,   0 Oct 10 17:42 /dev/tee0
crw-------    1 0        0         246,  16 Oct 10 17:42 /dev/teepriv0
compatible  interrupts  method      name
method=smc
[    0.000000] OF: reserved mem: 0x000000000e100000..0x000000000effffff (15360 KiB) nomap non-reusable optee_core@e100000
[    0.000000] OF: reserved mem: 0x0000000042000000..0x00000000421fffff (2048 KiB) nomap non-reusable optee_shm@42000000
[    1.114449] optee: probing for conduit method.
[    1.116211] optee: revision 4.7 (86846f4fdf14f25b)
[    1.122563] optee: Asynchronous notifications enabled
[    1.123096] optee: dynamic shared memory is enabled
[    1.135445] optee: initialized driver
optee-ta-d96a5b40-c3e5-21e3-8794-1002a5d5c61b
```

OP-TEE 자신의 로그(두 번째 UART, 앞부분):

```text
I/TC: Non-secure external DT found
I/TC: Embedded DTB found
I/TC: OP-TEE version: 4.7.0 (gcc version 15.2.0 (Ubuntu 15.2.0-16ubuntu1)) #1 Sat Oct 10 17:39:20 UTC 2026 aarch64
I/TC: WARNING: This OP-TEE configuration might be insecure!
I/TC: WARNING: Please check https://optee.readthedocs.io/en/latest/architecture/porting_guidelines.html
I/TC: Primary CPU initializing
I/TC: Primary CPU switching to normal world boot
I/TC: Secondary CPU 1 initializing
I/TC: Secondary CPU 1 switching to normal world boot
I/TC: Secondary CPU 2 initializing
I/TC: Secondary CPU 2 switching to normal world boot
I/TC: Secondary CPU 3 initializing
I/TC: Secondary CPU 3 switching to normal world boot
```

출력에서 볼 것:

- BL2가 **id=4(BL32 헤더)**를 읽고 헤더의 magic `0x4554504f`("OPTE"의 리틀엔디언)를 확인한 뒤, 본체 **id=21(BL32_EXTRA1)**을 Secure DRAM `0x0e100000`에 올렸다. BL31은 `Initializing BL32`로 OP-TEE를 먼저 한 번 돌려 초기화한 뒤에야 normal world로 나간다(그림 2의 ⑦).
- **OP-TEE가 Linux용 DT를 고쳤다**: `reserved-memory`에 자기 영역(Secure DRAM 15 MB)과 공유 메모리(`0x42000000`, 2 MB)를 `no-map`으로 넣고, `/firmware/optee` 노드(`compatible = "linaro,optee-tz"`, `method = "smc"`)를 추가했다. 커널의 `optee` 드라이버는 이 노드로 probe된다 — 일반 platform 드라이버가 DT 노드로 probe되는 것과 똑같다(D, G3).
- **`/dev/tee0`(major 246, minor 0)과 `/dev/teepriv0`(minor 16)** — 각각 클라이언트용과 tee-supplicant용 문자 디바이스다. supplicant는 OP-TEE가 Linux 파일시스템에 암호화된 데이터를 저장하거나 TA 바이너리를 읽어 올 때 대신 일해 주는 데몬이다.
- **`optee-ta-d96a5b40-...`**는 TEE bus(`/sys/bus/tee/devices/`)에 열거된 장치다. OP-TEE가 "열거해 달라"고 표시한 TA/pseudo TA를 커널이 장치로 만들어, 그 UUID에 맞는 커널 드라이버가 붙을 수 있게 한다(fTPM, RNG 같은 TA가 이렇게 커널 드라이버와 연결된다). 이 UUID는 OP-TEE 소스의 `lib/libutee/include/pta_invoke_tests.h`에 정의된 테스트용 pseudo TA다.
- **OP-TEE 로그의 `Secondary CPU N initializing`** — PSCI CPU_ON이 BL31을 거쳐 OP-TEE에도 전달된다. §4.6의 OP-TEE 핫플러그 측정(50회) 뒤 이 줄은 51번(부팅 1회 + 핫플러그 50회) 찍혀 있었다. 그래서 OP-TEE 구성에서 online이 조금 더 느렸다.
- `WARNING: This OP-TEE configuration might be insecure!` — QEMU 플랫폼은 하드웨어 키·보안 저장소가 없는 개발용 설정이라는 경고다. 제품에서는 이 경고가 사라지도록 플랫폼 포팅(HUK, RPMB 등)을 한다.
- 같은 부팅에서 `insmod /c5_smc.ko`도 했다: `SMCCC_VERSION 0x10005`, PSCI v1.1로 TF-A 단독과 같고, 왕복 최솟값은 15.6 µs였다(그림 6).

> 함정: OP-TEE를 넣은 뒤 "U-Boot이 커널을 못 올린다", "커널이 부팅 직후 멈춘다"면 **예약 메모리와 로드 주소의 겹침**부터 본다. 위에서 U-Boot이 `ERROR: reserving fdt memory region failed (addr=42000000 size=200000 flags=2): -17`을 찍은 것도 같은 영역을 두 번 예약하려 해서다(동작에는 지장 없었다). Linux가 `no-map` 영역을 건드리면 TZASC가 막아 external abort가 난다 — §3의 Data Abort와 같은 종류의 증상이다.

---

## 7. Secure boot — chain of trust를 직접 만들고 깨 보기

### 7.1 직관 — "앞 단계가 다음 단계의 지문을 확인하고서야 넘겨준다"

Don이 SSD에서 했던 "서명된 펌웨어 다운로드"를 떠올리자. 호스트가 새 펌웨어를 보내면 현재 펌웨어가 서명을 확인하고 나서야 NAND에 쓴다. secure boot는 이것을 **부팅의 모든 단계에 연쇄로** 적용한 것이다.

1. **뿌리(Root of Trust)**: 칩 안의 바꿀 수 없는 것 — BootROM 코드와, OTP fuse에 태운 **ROTPK(Root Of Trust Public Key)의 해시**. 공개키 자체는 커서 fuse에 다 못 넣으므로 해시(SHA-256, 32바이트)만 넣는다.
2. BL1(또는 BootROM)이 FIP에서 공개키가 든 인증서를 읽고, **그 공개키의 해시가 fuse 값과 같은지** 확인한다. 같으면 그 키로 인증서 서명을 검증하고, 인증서 안의 **BL2 해시**와 실제 BL2 이미지 해시를 비교한다.
3. BL2가 같은 방식으로 BL31·BL32·BL33을 검증한다.
4. BL33(U-Boot)이 **FIT 이미지 서명**으로 커널·DTB·initramfs를 검증한다(L5).
5. 커널이 dm-verity로 rootfs 블록 해시를 검증한다(L6).

한 고리라도 검증 없이 넘기면 그 아래는 전부 신뢰할 수 없다. 그래서 "chain"이다.

### 7.2 TBBR 인증서 트리 — 키를 왜 여러 개로 나누나

TF-A가 구현하는 Arm TBBR(Trusted Board Boot Requirements) CoT는 X.509v3 인증서를 쓴다. 인증서마다 서명용 키가 다르고, 해시·공개키·카운터를 **확장 필드(OID 1.3.6.1.4.1.4128.2100.x)**에 담는다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="150" y="12" width="380" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="37" text-anchor="middle" font-size="13">ROTPK 해시 — OTP fuse (QEMU: BL1에 내장)</text>
  <rect x="10" y="80" width="150" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="99" text-anchor="middle" font-size="12">TB FW cert (id 6)</text>
  <text x="85" y="117" text-anchor="middle" font-size="12">서명: ROT 키</text>
  <rect x="168" y="80" width="506" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="421" y="99" text-anchor="middle" font-size="12">Trusted Key cert (id 7) · 서명: ROT 키</text>
  <text x="421" y="117" text-anchor="middle" font-size="12">담은 것: Trusted world(TW) 공개키 · Non-trusted world(NTW) 공개키</text>
  <rect x="168" y="150" width="165" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="169" text-anchor="middle" font-size="12">SoC FW key cert (9)</text>
  <text x="250" y="187" text-anchor="middle" font-size="12">서명: TW 키</text>
  <rect x="341" y="150" width="165" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-dasharray="5 3"/>
  <text x="423" y="169" text-anchor="middle" font-size="12">TOS FW key cert (10)</text>
  <text x="423" y="187" text-anchor="middle" font-size="12">서명: TW 키</text>
  <rect x="514" y="150" width="160" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="594" y="169" text-anchor="middle" font-size="12">NT FW key cert (11)</text>
  <text x="594" y="187" text-anchor="middle" font-size="12">서명: NTW 키</text>
  <rect x="168" y="220" width="165" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="239" text-anchor="middle" font-size="12">SoC FW content (13)</text>
  <text x="250" y="257" text-anchor="middle" font-size="12">BL31 해시</text>
  <rect x="341" y="220" width="165" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-dasharray="5 3"/>
  <text x="423" y="239" text-anchor="middle" font-size="12">TOS FW content (14)</text>
  <text x="423" y="257" text-anchor="middle" font-size="12">BL32 해시</text>
  <rect x="514" y="220" width="160" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="594" y="239" text-anchor="middle" font-size="12">NT FW content (15)</text>
  <text x="594" y="257" text-anchor="middle" font-size="12">BL33 해시</text>
  <rect x="10" y="290" width="150" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="313" text-anchor="middle" font-size="12">BL2</text>
  <rect x="168" y="290" width="165" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="313" text-anchor="middle" font-size="12">BL31</text>
  <rect x="341" y="290" width="165" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5 3"/>
  <text x="423" y="313" text-anchor="middle" font-size="12">BL32 (OP-TEE)</text>
  <rect x="514" y="290" width="160" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="594" y="313" text-anchor="middle" font-size="12">BL33 (U-Boot)</text>
  <rect x="341" y="356" width="333" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="507" y="381" text-anchor="middle" font-size="12">U-Boot: FIT 서명 검증 (공개키는 U-Boot DTB 안)</text>
  <rect x="341" y="420" width="333" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="507" y="443" text-anchor="middle" font-size="12">kernel · DTB · initramfs (→ L5, rootfs는 L6)</text>
  <rect x="10" y="356" width="320" height="40" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a" stroke-dasharray="5 3"/>
  <text x="170" y="373" text-anchor="middle" font-size="12">anti-rollback: 인증서의 NV counter가</text>
  <text x="170" y="390" text-anchor="middle" font-size="12">OTP 카운터 이상이어야 통과</text>
  <line x1="250" y1="52" x2="100" y2="78" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="400" y1="52" x2="420" y2="78" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="250" y1="126" x2="250" y2="148" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="423" y1="126" x2="423" y2="148" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="594" y1="126" x2="594" y2="148" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="250" y1="196" x2="250" y2="218" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="423" y1="196" x2="423" y2="218" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="594" y1="196" x2="594" y2="218" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="85" y1="126" x2="85" y2="288" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="250" y1="266" x2="250" y2="288" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="423" y1="266" x2="423" y2="288" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="594" y1="266" x2="594" y2="288" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="594" y1="326" x2="594" y2="354" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="507" y1="396" x2="507" y2="418" stroke="currentColor" marker-end="url(#ah6)"/>
</svg>
```
그림 9 — TBBR chain of trust. 화살표는 "이 인증서/키가 다음 것을 보증한다". 점선(BL32)은 이번 TBB 빌드에 OP-TEE를 넣지 않아 생기지 않은 고리다. 아래 주황색은 TF-A 밖, U-Boot가 이어받는 고리다.

키를 층층이 나누는 이유는 **운영**이다. ROT 개인키는 칩 수명 동안 바꿀 수 없으니 HSM 금고에 넣고 거의 쓰지 않는다. 그 키로는 "Trusted world 키"와 "Non-trusted world 키"만 한 번 서명한다. 이후 BL31 업데이트는 TW 키로, U-Boot 업데이트는 NTW 키로 서명하면 된다. U-Boot 팀이 BL31을 서명할 수는 없고, NTW 키가 새도 BL31·OP-TEE는 안전하다. 마지막의 "key cert → content cert" 두 단도 같은 이유다(content cert의 키는 자주 바꿔도 된다).

### 7.3 코드로 확인 — Trusted Board Boot 빌드

`TRUSTED_BOARD_BOOT=1 GENERATE_COT=1`이면 TF-A가 `cert_create` 도구를 빌드해 키·인증서를 만들고 FIP에 넣는다. 암호 라이브러리로 Mbed TLS가 필요하다. TF-A v2.15의 `default_mbedtls_config.h`가 `MBEDTLS_CONFIG_VERSION 0x03060600`을 요구해서, 처음 받은 3.6.4는 `#error "Invalid config version"`으로 실패했고 3.6.6으로 바꿔 성공했다.

```sh
curl -sfL -o c5_mbedtls.tar.bz2 https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.6/mbedtls-3.6.6.tar.bz2
mkdir -p c5_mbedtls && tar xjf c5_mbedtls.tar.bz2 -C c5_mbedtls --strip-components=1
cd c5_tfa && make -j6 CROSS_COMPILE=aarch64-linux-gnu- PLAT=qemu DEBUG=1 LOG_LEVEL=40 \
  BL33=/var/lab/out/u-boot.bin TRUSTED_BOARD_BOOT=1 GENERATE_COT=1 \
  MBEDTLS_DIR=/var/tmp/C5/c5_mbedtls BUILD_BASE=/var/tmp/C5/tbb-build all fip
```

```text
...
  OPENSSL /var/tmp/C5/tbb-build/qemu/debug/rotpk_sha256.bin
...
NOTICE:  CoT Generation Tool: Built : 10:34:09, Oct 10 2026
NOTICE:  Target platform: TBBR -DGeneric
NOTICE:  Creating new key for 'Trusted World key'
NOTICE:  Creating new key for 'Non Trusted World key'
NOTICE:  Creating new key for 'SCP Firmware Content Certificate key'
NOTICE:  Creating new key for 'SoC Firmware Content Certificate key'
NOTICE:  Creating new key for 'Trusted OS Firmware Content Certificate key'
NOTICE:  Creating new key for 'Non Trusted Firmware Content Certificate key'

Built certificates successfully
Certificates can be found in /var/tmp/C5/tbb-build/qemu/debug

Trusted Boot Firmware BL2: offset=0x1A0, size=0x133AC, cmdline="--tb-fw"
EL3 Runtime Firmware BL31: offset=0x1354C, size=0xF89B, cmdline="--soc-fw"
Non-Trusted Firmware BL33: offset=0x22DE7, size=0x16F3D0, cmdline="--nt-fw"
Trusted key certificate: offset=0x1921B7, size=0x616, cmdline="--trusted-key-cert"
SoC Firmware key certificate: offset=0x1927CD, size=0x4E2, cmdline="--soc-fw-key-cert"
Non-Trusted Firmware key certificate: offset=0x192CAF, size=0x4F2, cmdline="--nt-fw-key-cert"
Trusted Boot Firmware BL2 certificate: offset=0x1931A1, size=0x4BE, cmdline="--tb-fw-cert"
SoC Firmware content certificate: offset=0x19365F, size=0x438, cmdline="--soc-fw-cert"
Non-Trusted Firmware content certificate: offset=0x193A97, size=0x448, cmdline="--nt-fw-cert"

Built /var/tmp/C5/tbb-build/qemu/debug/fip.bin successfully
```

출력에서 볼 것: FIP에 인증서 6개가 추가됐다. BL2는 0x8204 → 0x133AC로 두 배 넘게 커졌다 — Mbed TLS의 RSA·X.509 파서가 들어갔기 때문이다(BL1도 마찬가지). 부트 ROM 크기 예산이 빠듯한 실제 칩에서는 이것이 진짜 제약이 된다. 그리고 `Creating new key`: 개발 빌드라 빌드할 때마다 키를 새로 만든다. 제품에서는 절대 이렇게 하지 않는다 — `ROT_KEY=`, `TRUSTED_WORLD_KEY=` 같은 변수로 HSM에서 관리하는 고정 키를 넘기거나, 서명은 별도 서명 서버에서 한다.

ROTPK 해시는 어디서 오나? qemu 플랫폼 Makefile이 ROT 키 공개부를 DER로 뽑아 SHA-256을 내고, 그 32바이트를 BL1·BL2에 `.incbin`으로 박는다. 실제 칩이라면 이 32바이트를 **공장에서 OTP fuse에 태운다**.

```text
# plat/qemu/qemu/platform.mk (발췌)
    $(ROTPK_HASH): $(ROT_KEY) | $$(@D)/
	$(q)${OPENSSL_BIN_PATH}/openssl rsa -in $< -pubout -outform DER 2>/dev/null |\
	${OPENSSL_BIN_PATH}/openssl dgst -sha256 -binary > $@ 2>/dev/null
# plat/qemu/common/qemu_rotpk.S
qemu_rotpk_hash:
	/* DER header */
	.byte 0x30, 0x31, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86, 0x48
	.byte 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
	/* SHA256 */
	.incbin ROTPK_HASH
```

```sh
xxd tbb-build/qemu/debug/rotpk_sha256.bin
openssl rsa -in tbb-build/qemu/debug/rot_key.pem -pubout -outform DER 2>/dev/null | openssl dgst -sha256
```

```text
00000000: c746 7895 d1a5 0edd eca9 bca0 89bb f450  .Fx............P
00000010: 6f85 1df7 2461 1693 0d9d 8174 f4c4 eee3  o...$a.....t....
SHA2-256(stdin)= c7467895d1a50eddeca9bca089bbf4506f851df7246116930d9d8174f4c4eee3
```

출력에서 볼 것: BL1에 박힌 32바이트와 ROT 키 공개부의 SHA-256이 같다. 이 값이 이 "가상 칩"의 신뢰 뿌리다. 플랫폼은 `plat_get_rotpk_info()`로 이 값을 `ROTPK_IS_HASH` 플래그와 함께 넘기고, 인증 모듈은 인증서에서 꺼낸 공개키를 해시해 비교한다.

### 7.4 코드로 확인 — 검증 부팅 로그

```sh
timeout 60 qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -m 1057 -nographic \
  -bios tbb-build/qemu/debug/qemu_fw.bios < /dev/null > c5_tbb_boot.log 2>&1
grep -v "erratum\|CVE" c5_tbb_boot.log | sed -n '1,/U-Boot 20/p'
```

```text
NOTICE:  Booting Trusted Firmware
NOTICE:  BL1: v2.15.0(debug):
NOTICE:  BL1: Built : 10:34:01, Oct 10 2026
INFO:    BL1: RAM 0xe0ee000 - 0xe0f8000
INFO:    Using crypto library 'mbed TLS'
INFO:    BL1: Loading BL2
INFO:    Loading image id=6 at address 0xe05b000
INFO:    Image id=6 loaded: 0xe05b000 - 0xe05b4be
INFO:    Loading image id=1 at address 0xe05b000
INFO:    Image id=1 loaded: 0xe05b000 - 0xe06e3ac
NOTICE:  BL1: Booting BL2
INFO:    Entry point address = 0xe05b000
INFO:    SPSR = 0x3c5
NOTICE:  BL2: v2.15.0(debug):
NOTICE:  BL2: Built : 10:34:04, Oct 10 2026
INFO:    Using crypto library 'mbed TLS'
INFO:    BL2: Doing platform setup
INFO:    BL2: Loading image id 3
INFO:    Loading image id=7 at address 0xe090000
INFO:    Image id=7 loaded: 0xe090000 - 0xe090616
INFO:    Loading image id=9 at address 0xe090000
INFO:    Image id=9 loaded: 0xe090000 - 0xe0904e2
INFO:    Loading image id=13 at address 0xe090000
INFO:    Image id=13 loaded: 0xe090000 - 0xe090438
INFO:    Loading image id=3 at address 0xe090000
INFO:    Image id=3 loaded: 0xe090000 - 0xe09f89b
INFO:    BL2: Loading image id 5
INFO:    Loading image id=11 at address 0x60000000
INFO:    Image id=11 loaded: 0x60000000 - 0x600004f2
INFO:    Loading image id=15 at address 0x60000000
INFO:    Image id=15 loaded: 0x60000000 - 0x60000448
INFO:    Loading image id=5 at address 0x60000000
INFO:    Image id=5 loaded: 0x60000000 - 0x6016f3d0
NOTICE:  BL1: Booting BL31
...
U-Boot 2026.10 (Oct 10 2026 - 09:59:26 -0700)
```

출력에서 볼 것 — 그림 9의 화살표가 로그 순서 그대로다.

- BL1은 BL2보다 **id=6(TB FW cert)**을 먼저 읽는다. 인증서도 BL2와 같은 주소 `0xe05b000`에 먼저 올려서 검증한 다음, 그 자리에 BL2를 덮어 올리고 해시를 비교한다(메모리가 귀한 SRAM에서 쓰는 기법).
- BL31(id 3)을 얻기 위해 BL2는 **7 → 9 → 13 → 3** 순서로 읽는다: Trusted Key cert(ROT 키로 검증) → SoC FW key cert(TW 키) → SoC FW content cert(BL31 해시) → BL31 본체.
- BL33(id 5)은 **11 → 15 → 5**: NT FW key cert(NTW 키) → NT FW content cert(BL33 해시) → U-Boot. 인증서들은 다음 이미지가 놓일 주소(0x60000000)에 잠깐 올라갔다가 덮인다. id 7(Trusted Key cert)은 BL31 경로에서 이미 검증했고, 로그에서도 BL33 경로에서 다시 읽지 않는다.
- 로그에 "verified" 같은 말은 없다. **검증 실패가 없으면 조용히 진행한다**. 실패하면 어떻게 되는지는 7.6에서 본다.

### 7.5 코드로 확인 — 인증서 안을 열어 보기

```sh
openssl asn1parse -inform DER -in tbb-build/qemu/debug/nt_fw_content.crt | grep -A3 "2100.1201"
sha256sum /var/lab/out/u-boot.bin
```

```text
  628:d=5  hl=2 l=  11 prim: OBJECT            :1.3.6.1.4.1.4128.2100.1201
  641:d=5  hl=2 l=   1 prim: BOOLEAN           :255
  644:d=5  hl=2 l=  51 prim: OCTET STRING      [HEX DUMP]:3031300D0609608648016503040201050004205D744397D87497419B54ECAD23209B14E73FCA7DB92A7634784D9F3C55598B59
  697:d=4  hl=2 l=  69 cons: SEQUENCE
5d744397d87497419b54ecad23209b14e73fca7db92a7634784d9f3c55598b59  /var/lab/out/u-boot.bin
```

출력에서 볼 것: OID `...2100.1201`은 TF-A `include/tools_share/tbbr_oid.h`의 `NON_TRUSTED_WORLD_BOOTLOADER_HASH_OID`, 즉 "BL33 해시" 칸이다. 값은 DER `DigestInfo`(앞 19바이트 `3031300D...0420`이 "SHA-256, 32바이트"라는 헤더) + 32바이트 해시이고, 뒤 32바이트가 `sha256sum u-boot.bin`과 **정확히 같다**. `BOOLEAN :255`는 이 확장이 critical이라는 뜻이다. 다른 OID들도 같은 헤더에 정의돼 있다.

| OID (1.3.6.1.4.1.4128.2100.) | 이름 (tbbr_oid.h) | 들어 있는 인증서 |
|---|---|---|
| .1 | TRUSTED_FW_NVCOUNTER_OID | Trusted 쪽 인증서들 |
| .2 | NON_TRUSTED_FW_NVCOUNTER_OID | NT FW key/content cert |
| .201 | TRUSTED_BOOT_FW_HASH_OID | TB FW cert (BL2 해시) |
| .302 / .303 | TRUSTED_WORLD_PK_OID / NON_TRUSTED_WORLD_PK_OID | Trusted Key cert |
| .1101 | NON_TRUSTED_FW_CONTENT_CERT_PK_OID | NT FW key cert |
| .1201 | NON_TRUSTED_WORLD_BOOTLOADER_HASH_OID | NT FW content cert (BL33 해시) |

### 7.6 코드로 확인 — U-Boot 1바이트 변조

FIP 안의 BL33만 1바이트 바꾼 U-Boot으로 교체한다. 인증서는 그대로다.

```sh
mkdir -p tamper && cp /var/lab/out/u-boot.bin tamper/bl33_evil.bin
printf "\xaa" | dd of=tamper/bl33_evil.bin bs=1 seek=4096 conv=notrunc status=none
cmp -l /var/lab/out/u-boot.bin tamper/bl33_evil.bin
cp tbb-build/qemu/debug/fip.bin tamper/fip.bin
tbb-build/qemu/debug/tools/fiptool/fiptool update --nt-fw tamper/bl33_evil.bin tamper/fip.bin
cp tbb-build/qemu/debug/bl1.bin tamper/bios.bin && truncate -s 256K tamper/bios.bin
cat tamper/fip.bin >> tamper/bios.bin          # BL1 + (0x40000 위치에) FIP
timeout 30 qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -m 1057 -nographic \
  -bios tamper/bios.bin < /dev/null > c5_tamper.log 2>&1
grep -v "erratum\|CVE" c5_tamper.log | sed -n '/BL2: Loading image id 5/,$p'
```

```text
   4097   0 252
INFO:    BL2: Loading image id 5
INFO:    Loading image id=11 at address 0x60000000
INFO:    Image id=11 loaded: 0x60000000 - 0x600004f2
INFO:    Loading image id=15 at address 0x60000000
INFO:    Image id=15 loaded: 0x60000000 - 0x60000448
INFO:    Loading image id=5 at address 0x60000000
INFO:    Image id=5 loaded: 0x60000000 - 0x6016f3d0
ERROR:   BL2: Failed to load image id 5 (-80)
qemu-system-aarch64: terminating on signal 15 from pid 317603 (timeout)
```

(첫 줄은 `cmp -l` 출력이다: 4097번째 바이트(1부터 셈)가 8진수 0에서 252(=0xAA)로 바뀌었다. 처음엔 0을 써서 바뀐 게 없었다 — 원래 값이 0이었다. 변조 실험은 반드시 `cmp`로 실제로 바뀌었는지 확인하자.)

출력에서 볼 것: 인증서 11·15는 통과했지만(서명은 멀쩡하다), BL33 해시가 인증서 속 값과 달라 **`-80`**으로 거부됐다. TF-A libc의 `errno.h`에서 `EAUTH`가 80이다. BL2는 여기서 멈추고 U-Boot은 실행되지 않는다. 실제 제품이라면 이 자리에서 "복구 모드로 가기"나 "B 슬롯으로 가기"(L4)를 설계한다.


### 7.7 코드로 확인 — anti-rollback (NV counter)

서명이 진짜인 **옛 버전**을 다시 올리는 공격(롤백)은 서명 검증만으로 못 막는다. 옛 U-Boot에 알려진 취약점이 있다면 그것도 "정품"이기 때문이다. 그래서 인증서마다 **NV counter**(버전 번호)를 넣고, 칩의 OTP에 "지금까지 본 최댓값"을 둔다. TF-A의 판정 로직(`drivers/auth/auth_mod.c`):

```c
	if (*cert_nv_ctr < plat_nv_ctr) {
		/* Invalid NV-counter */
		return 1;                          /* 롤백 → 인증 실패 */
	} else if (*cert_nv_ctr > plat_nv_ctr) {
		...
		*need_nv_ctr_upgrade = true;       /* 새 버전 → 서명 검증 성공 후 OTP 카운터 올리기 */
	}
	...
	if (need_nv_ctr_upgrade && sig_auth_done) {
		rc = plat_set_nv_ctr2(nv_ctr_param->plat_nv_ctr->cookie,
				      img_desc, cert_nv_ctr);
		if (rc != 0) {
			...
			return rc;
		}
	}
```

그리고 qemu 플랫폼의 카운터 구현(`plat/qemu/common/qemu_trusted_boot.c`)은 "항상 0, 올리기는 실패"다 — OTP가 없으니까.

```c
int plat_get_nv_ctr(void *cookie, unsigned int *nv_ctr)
{
	*nv_ctr = 0;
	return 0;
}

int plat_set_nv_ctr(void *cookie, unsigned int nv_ctr)
{
	return 1;
}
```

그렇다면 Non-trusted 카운터를 1로 올린 인증서를 만들면, QEMU는 카운터를 1로 "올리지 못해서" 거부해야 한다. 확인해 보자.

```sh
make -j6 CROSS_COMPILE=aarch64-linux-gnu- PLAT=qemu DEBUG=1 LOG_LEVEL=40 BL33=/var/lab/out/u-boot.bin \
  TRUSTED_BOARD_BOOT=1 GENERATE_COT=1 NTFW_NVCTR_VAL=1 MBEDTLS_DIR=/var/tmp/C5/c5_mbedtls \
  BUILD_BASE=/var/tmp/C5/tbb-nv-build all fip
for b in tbb-build tbb-nv-build; do echo "== $b"
  openssl asn1parse -inform DER -in $b/qemu/debug/nt_fw_key.crt | grep -A2 "4128.2100.2$"; done
timeout 30 qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -m 1057 -nographic \
  -bios tbb-nv-build/qemu/debug/qemu_fw.bios < /dev/null > c5_nv.log 2>&1
grep -v "erratum\|CVE" c5_nv.log | sed -n '/BL2: Loading image id 5/,$p'
```

```text
== tbb-build
  600:d=5  hl=2 l=  10 prim: OBJECT            :1.3.6.1.4.1.4128.2100.2
  612:d=5  hl=2 l=   1 prim: BOOLEAN           :255
  615:d=5  hl=2 l=   3 prim: OCTET STRING      [HEX DUMP]:020100
== tbb-nv-build
  600:d=5  hl=2 l=  10 prim: OBJECT            :1.3.6.1.4.1.4128.2100.2
  612:d=5  hl=2 l=   1 prim: BOOLEAN           :255
  615:d=5  hl=2 l=   3 prim: OCTET STRING      [HEX DUMP]:020101
INFO:    BL2: Loading image id 5
INFO:    Loading image id=11 at address 0x60000000
INFO:    Image id=11 loaded: 0x60000000 - 0x600004f2
ERROR:   BL2: Failed to load image id 5 (-80)
qemu-system-aarch64: terminating on signal 15 from pid 341066 (timeout)
```

출력에서 볼 것: 카운터 확장 값이 DER INTEGER `02 01 00`(=0)에서 `02 01 01`(=1)로 바뀌었다. 이번엔 **NT FW key cert(id 11)** 단계에서 바로 `-80`이다 — 서명은 맞지만 카운터를 1로 올려야 하는데 `plat_set_nv_ctr()`가 1(실패)을 돌려줬기 때문이다. 실제 칩에서는 여기서 OTP 비트가 하나 태워지고(되돌릴 수 없다), 그 후에는 카운터 0짜리 옛 인증서가 `cert_nv_ctr < plat_nv_ctr`로 거부된다.

> 실무 함정: 카운터를 **언제** 올리느냐가 A/B 업데이트(L4)와 충돌한다. 새 이미지로 처음 부팅하자마자 OTP를 올리면, 새 이미지가 현장에서 문제가 있어도 B 슬롯(옛 버전)으로 돌아갈 수 없다. 그래서 TF-A는 `PSA_FWU_SUPPORT`일 때 "뱅크가 accepted 상태일 때만 올린다"는 분기를 둔다(위 `auth_mod.c`의 생략된 `#if` 부분).

### 7.8 TF-A 다음 고리 — U-Boot FIT 서명 (L5 미리 보기)

BL2가 검증한 것은 BL33(U-Boot)까지다. 커널부터는 U-Boot이 검증해야 체인이 이어진다. U-Boot의 verified boot는 이렇게 동작한다.

- 커널·DTB·initramfs를 하나의 **FIT(Flattened Image Tree, `.itb`)**로 묶고, `mkimage -k <키 디렉터리> -K u-boot.dtb -r`로 **configuration 노드**에 서명한다. `-K`는 공개키를 U-Boot의 control DTB의 `/signature` 노드에 넣고, `-r`은 그 키를 `required`로 표시한다.
- 그 U-Boot 바이너리(공개키 포함)의 해시가 바로 위 `nt_fw_content.crt`의 `.1201` 칸에 들어간다. 그래서 **"BL2가 U-Boot을 검증 → U-Boot에 박힌 공개키가 FIT를 검증"**으로 체인이 이어진다.
- `CONFIG_FIT_SIGNATURE=y`인 U-Boot은 `bootm` 때 서명을 확인하고, 실패하면 부팅을 거부한다. 서명되지 않은 `booti`/`bootz` 경로나 U-Boot 콘솔(환경 변수로 `bootcmd`를 바꾸기)을 열어 두면 체인이 그 자리에서 끊긴다 — 제품에서는 콘솔을 막고 환경을 고정한다.

FIT 이미지 만들기와 서명 실습은 L5에서, rootfs까지 잇는 dm-verity는 L6에서 한다.

---

## 8. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="200" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="39" text-anchor="middle" font-size="12">DT /psci</text>
  <text x="120" y="56" text-anchor="middle" font-size="12">method = "smc" or "hvc"</text>
  <rect x="240" y="20" width="200" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="39" text-anchor="middle" font-size="12">psci_dt_init()</text>
  <text x="340" y="56" text-anchor="middle" font-size="12">set_conduit() → invoke_psci_fn</text>
  <rect x="460" y="20" width="200" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="39" text-anchor="middle" font-size="12">psci_init_smccc()</text>
  <text x="560" y="56" text-anchor="middle" font-size="12">SMCCC 버전 → 1.1 conduit</text>
  <rect x="240" y="96" width="200" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="115" text-anchor="middle" font-size="12">struct psci_operations</text>
  <text x="340" y="132" text-anchor="middle" font-size="12">psci_ops.cpu_on / cpu_off ...</text>
  <rect x="20" y="96" width="200" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="115" text-anchor="middle" font-size="12">cpu_psci_ops (arm64)</text>
  <text x="120" y="132" text-anchor="middle" font-size="12">cpu_boot · cpu_die · cpu_kill</text>
  <rect x="460" y="96" width="200" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="115" text-anchor="middle" font-size="12">arm_smccc_1_1_invoke()</text>
  <text x="560" y="132" text-anchor="middle" font-size="12">Spectre · TRNG · SoC ID · 드라이버</text>
  <rect x="20" y="172" width="200" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="191" text-anchor="middle" font-size="12">SMP 부팅 · CPU hotplug</text>
  <text x="120" y="208" text-anchor="middle" font-size="12">cpuidle-psci · poweroff/reboot</text>
  <rect x="20" y="248" width="200" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="267" text-anchor="middle" font-size="12">DT /firmware/optee</text>
  <text x="120" y="284" text-anchor="middle" font-size="12">reserved-memory no-map</text>
  <rect x="240" y="248" width="200" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="267" text-anchor="middle" font-size="12">optee 드라이버 probe</text>
  <text x="340" y="284" text-anchor="middle" font-size="12">tee_device 등록</text>
  <rect x="460" y="248" width="200" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="267" text-anchor="middle" font-size="12">/dev/tee0 · /dev/teepriv0</text>
  <text x="560" y="284" text-anchor="middle" font-size="12">TEE bus 장치 (TA UUID)</text>
  <line x1="220" y1="42" x2="238" y2="42" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="440" y1="42" x2="458" y2="42" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="340" y1="64" x2="340" y2="94" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="560" y1="64" x2="560" y2="94" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="240" y1="118" x2="222" y2="118" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="120" y1="140" x2="120" y2="170" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="220" y1="270" x2="238" y2="270" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="440" y1="270" x2="458" y2="270" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="340" y="320" text-anchor="middle" font-size="12">회색 = 펌웨어가 써 준 DT · 초록 = 커널 코드 (drivers/firmware, drivers/tee)</text>
</svg>
```
그림 10 — 커널이 펌웨어 서비스를 발견하고 쓰는 구조. 모두 "DT 노드 → 드라이버 초기화 → 함수 포인터/디바이스 등록"이라는 드라이버의 기본 패턴이다.

- **PSCI 드라이버** (`drivers/firmware/psci/psci.c`): 부팅 초기에 `psci_dt_init()`이 `/psci` 노드를 찾아 `method`에 따라 `invoke_psci_fn`을 `__invoke_psci_fn_smc` 또는 `__invoke_psci_fn_hvc`로 정한다. 버전에 맞춰 `psci_ops`(struct psci_operations: `cpu_on`, `cpu_off`, `cpu_suspend`, `affinity_info`, `migrate_info_type`…)를 채우고, `psci_init_smccc()`가 `SMCCC_VERSION`을 물어 v1.1 이상이면 `arm_smccc_version_init()`으로 전역 conduit을 등록한다. §5에서 QEMU가 conduit NONE이었던 이유가 바로 이 함수다.
- **arm64 SMP** (`arch/arm64/kernel/psci.c`): `cpu_psci_ops`가 `cpu_operations` 인터페이스(`.cpu_boot`, `.cpu_die`, `.cpu_kill`)를 PSCI로 구현한다. DT의 `enable-method = "psci"`가 이 ops를 고른다. 다른 값(`"spin-table"`)이면 펌웨어 없이 메모리 주소를 폴링하는 방식이다 — §4.4의 hold pen을 커널과 부트로더가 직접 약속하는 형태다.
- **SMCCC 라이브러리** (`include/linux/arm-smccc.h`, `drivers/firmware/smccc/`): `struct arm_smccc_res`(a0~a3), `arm_smccc_smc()`(asm 함수 `__arm_smccc_smc`, `arch/arm64/kernel/smccc-call.S`), 인라인 `arm_smccc_1_1_invoke()`. Spectre 완화(`arch/arm64/kernel/proton-pack.c`), `SMCCC_ARCH_SOC_ID`로 SoC 정보를 sysfs에 내보내는 `soc_id` 드라이버, 펌웨어 TRNG 등이 이것을 쓴다.
- **SoC 벤더 드라이버의 SiP 호출**: 일부 SoC는 클럭·전원·eFuse 같은 Secure 자원을 SiP SMC로만 노출한다. 예: Xilinx ZynqMP 펌웨어 드라이버(`drivers/firmware/xilinx/zynqmp.c`), Intel Stratix10 서비스 드라이버(`drivers/firmware/stratix10-svc.c`). 드라이버를 쓰다가 "이 레지스터는 Linux에서 쓰면 abort가 난다"를 만나면, 그 SoC는 SiP 서비스를 거쳐야 한다는 뜻일 가능성이 높다.
- **TEE 서브시스템** (`drivers/tee/`): `tee_core.c`가 `tee_device`를 문자 디바이스(`/dev/teeN`, G4의 `cdev` 패턴)로 등록하고, `optee/` 드라이버가 SMC 전송을 구현한다. TA를 커널 드라이버로 쓰는 `tee_client_*` API와 TEE bus(G2의 bus/device/driver 모델)가 있다.
- **reserved-memory**: 펌웨어가 쓴 `no-map` 영역은 memblock에서 빠져서 커널 선형 매핑에도 안 들어간다(F9). BL31이 DRAM에 사는 SoC는 반드시 DT에 그 영역을 예약해야 한다 — 빼먹으면 Linux가 BL31을 덮어쓰고, 다음 PSCI 호출에서 시스템이 사라진다.

드라이버를 쓸 때 다시 만나는 지점: probe에서 펌웨어 서비스 존재를 확인할 때(`arm_smccc_1_1_get_conduit()`가 NONE인지), 전원 관리 콜백(N6)에서 suspend가 결국 `CPU_SUSPEND`/`SYSTEM_SUSPEND`로 내려갈 때, 그리고 보안 키를 다루는 드라이버가 TEE를 통해야 할 때(L6).

---

## 9. Embedded 실무에서 다시 보기

### 9.1 "CPU1: failed to come online" — 새 보드 bring-up

예를 들어 센서 키트용 4코어 SoC를 새 보드에 올렸는데 `smp: Brought up 1 node, 1 CPU`만 나온다. 볼 순서:

1. `dmesg | grep -i psci` — PSCI 버전이 찍혔나? 안 찍히면 DT `/psci` 노드나 `method`가 틀렸다.
2. `/proc/device-tree/cpus/cpu@N/enable-method`가 `psci`인가, `reg`(MPIDR)가 실제 코어와 맞나? CPU_ON의 첫 인자가 바로 이 값이다. 클러스터가 있는 SoC는 MPIDR이 `0x100`, `0x101`처럼 생겨서 자주 틀린다.
3. `failed to boot CPU1 (-2)`처럼 에러 코드가 찍히면 펌웨어가 INVALID_PARAMETERS로 거부한 것 — TF-A의 `PLATFORM_CORE_COUNT`, `plat_core_pos_by_mpidr()`를 본다.
4. 에러 없이 `CPU1: failed to come online`이면 SMC는 성공했는데 코어가 Linux로 안 돌아온 것 — 전원 컨트롤러, 코어 리셋 벡터 설정(RVBAR), hold pen 주소, BL31 영역이 DT에서 예약돼 있는지.
5. TF-A를 `DEBUG=1 LOG_LEVEL=40`으로 다시 빌드하고 secure UART(있다면)를 본다.

### 9.2 키 관리와 공장 — fuse는 한 번뿐이다

secure boot를 켠다는 것은 공장에서 **ROTPK 해시를 OTP에 태우고, 서명 안 된 부팅을 막는 fuse(예: i.MX의 HAB "closed" 설정)를 태운다**는 뜻이다. 되돌릴 수 없다. 그래서 실무 순서는: (1) 개발 키로 open 상태에서 서명된 이미지가 실제로 검증되는지 로그로 확인(i.MX라면 HAB 이벤트가 없는지) → (2) 제품 키를 HSM에 생성, 서명은 빌드 서버가 아니라 서명 서버에서 → (3) 소량을 close해서 검증 → (4) 생산. 키를 잃어버리면 그 칩에는 영원히 새 펌웨어를 못 넣는다. 국방·로보틱스 회사 면접에서 "키는 누가 어디에 보관하나"를 묻는 이유다.

### 9.3 OTA와 anti-rollback의 순서

필드 장비의 A/B 업데이트(L4)에서: 새 이미지 기록 → 재부팅 → 새 이미지가 부팅·헬스체크 통과 → "accepted" 표시 → **그때서야** NV counter 증가. §7.7에서 본 것처럼 TF-A 기본 동작은 "더 큰 카운터의 인증서를 처음 검증한 순간 올린다"이므로, 롤백 가능한 기간을 두려면 이 정책을 설계에 넣어야 한다.

### 9.4 SoC 전용 펌웨어 서비스를 부르는 드라이버

예를 들어 센서 키트 SoC가 장치 고유 ID(eFuse)를 Secure 전용으로 두고 SiP SMC로만 읽게 한다면, 드라이버 코드는 이런 모양이 된다(패턴 예시 — 함수 ID와 의미는 SoC 문서에 따라 다르다).

```c
#define C5_SIP_READ_UID  ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32, \
                                            ARM_SMCCC_OWNER_SIP, 0x0101)

static int c5_read_uid(u64 *uid)
{
	struct arm_smccc_res res;

	if (arm_smccc_1_1_get_conduit() == SMCCC_CONDUIT_NONE)
		return -ENODEV;                 /* 펌웨어가 SMCCC 1.1을 모른다 */
	arm_smccc_1_1_invoke(C5_SIP_READ_UID, &res);
	if ((long)res.a0 < 0)
		return -EIO;                    /* 펌웨어가 거부 */
	*uid = ((u64)res.a1 << 32) | res.a2;
	return 0;
}
```

§5의 교훈 두 가지가 들어 있다: conduit NONE을 먼저 확인하고, 결과 `a0`의 음수를 에러로 본다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| TF-A의 BL33 주소보다 RAM이 작다 | `BL2: Built` 뒤 무소식 (§3) | `NS_IMAGE_OFFSET`이 DRAM 밖 → Data Abort → 조용한 panic | DRAM 구성에 맞게 platform_def.h 수정, `-d int`로 FAR 확인 |
| release TF-A로 디버깅 | 단계 이름만 나오고 원인 없음 | `LOG_LEVEL=20` | `DEBUG=1 LOG_LEVEL=40` 재빌드 |
| DT에 BL31/OP-TEE 영역 예약 누락 | 부팅은 되다가 CPU hotplug·suspend·TA 호출 때 멈춤 | Linux가 펌웨어 메모리를 덮어씀 | `reserved-memory`에 `no-map`으로 예약 |
| `arm_smccc_1_1_invoke`의 -1을 "펌웨어 거부"로 해석 | QEMU·구형 펌웨어에서만 기능이 꺼짐 | SMCCC v1.0 → conduit NONE → 호출 자체를 안 함 (§5) | `arm_smccc_1_1_get_conduit()` 먼저 확인 |
| 에뮬레이터에서만 테스트한 SMC 인자 | 실보드 TF-A에서 `-2` | QEMU 내장 PSCI는 인자 검증이 느슨함 (§5 AFFINITY_INFO) | 실제 펌웨어 또는 TF-A가 있는 QEMU로 테스트 |
| CPU 노드 `reg`(MPIDR) 오기재 | `failed to boot CPU1 (-2)` 또는 엉뚱한 코어 | CPU_ON 인자가 MPIDR | 클러스터 비트(Aff1) 포함해 `reg` 수정 |
| 개발 키로 서명한 채 출하 | 누구나 서명된 펌웨어를 만들 수 있음 | `GENERATE_COT`가 매번 새 키 생성, 키 파일이 저장소에 | 고정 키를 HSM에, 서명 서버 분리 |
| 서명은 했는데 U-Boot 콘솔·`booti` 열어 둠 | 서명 안 된 커널로 부팅 가능 | 체인이 BL33에서 끊김 | FIT 서명 `required`, 콘솔·환경 잠금 (L5) |
| 부팅 직후 NV counter 증가 | 새 버전 문제 시 B 슬롯 부팅도 거부 | 롤백 창 없음 | "accepted" 후 증가 정책 (§9.3) |
| OP-TEE 추가 후 U-Boot이 커널 로드 실패 | `Failed to allocate memory for the image` | 커널 로드 범위가 `optee_shm`과 겹침 (§6.3) | 로드 주소 변경(`kernel_addr_r`) 또는 shm 위치 변경 |

---

## 11. 면접에서 이렇게 말한다

**Q.** "Walk me through the ARMv8-A exception levels. Where does each piece of firmware run?"

**A.** EL0은 앱, EL1은 OS 커널, EL2는 하이퍼바이저, EL3는 secure monitor다. 여기에 TrustZone으로 Secure/Non-secure 두 세계가 있다. Linux는 NS-EL1(또는 EL2), 앱은 NS-EL0, OP-TEE 같은 Trusted OS는 S-EL1, TA는 S-EL0, TF-A의 BL31은 EL3에 상주한다. 부팅 중에는 BL1이 EL3, BL2가 S-EL1에서 돌고, U-Boot은 BL33으로 NS-EL1/EL2에서 돈다. 직접 TF-A 디버그 로그의 SPSR 값으로 BL2 0x3c5(EL1), BL31 0x3cd(EL3), BL33 0x3c5/0x3c9(EL1/EL2)를 확인해 봤다.

> EL0 is applications, EL1 the OS kernel, EL2 the hypervisor and EL3 the secure monitor, and TrustZone splits the system into a Secure and a Non-secure world. Linux runs at non-secure EL1, or EL2 if virtualization is available; a trusted OS like OP-TEE runs at secure EL1 with trusted applications at secure EL0; and TF-A's BL31 stays resident at EL3. During boot, BL1 runs at EL3, BL2 at secure EL1, and U-Boot is BL33 in the non-secure world. You can see it in the TF-A debug log: the SPSR it programs before each ERET encodes the target level — 0x3c5 is EL1h, 0x3cd is EL3h, 0x3c9 is EL2h.

**꼬리 질문**: "How is that different from Cortex-M TrustZone?" → Cortex-M은 Secure/Non-secure 상태 전환을 하드웨어가 SG 명령과 NSC 영역으로 직접 하고 EL3 같은 모니터 소프트웨어가 없다. Cortex-A는 세계 전환이 EL3 펌웨어를 거친다.

**Q.** "Why does ARM need EL3 firmware like TF-A at all? Why can't Linux turn CPUs on by itself?"

**A.** 세 가지 이유다. 첫째, 전원·리셋·DRAM 보호 같은 레지스터는 보안상 Secure 전용이어야 한다 — Linux가 직접 만지면 Linux를 장악한 공격자가 보안 세계를 무력화할 수 있다. 둘째, 세계 전환은 `SCR_EL3`를 쓸 수 있는 EL3만 할 수 있다. 셋째, PSCI라는 표준 인터페이스 덕분에 커널이 SoC마다 다른 전원 코드를 갖지 않아도 된다 — arm64 커널은 DT에 `enable-method = "psci"`만 있으면 어느 SoC에서든 같은 코드로 코어를 켠다.

> Three reasons. Power, reset and memory-protection controls need to be secure-only, otherwise a compromised kernel could switch off or corrupt the secure world. Switching worlds requires writing SCR_EL3, which only EL3 can do. And PSCI gives the kernel one standard interface instead of SoC-specific SMP and power code — on arm64 the same cpu_psci_ops boots secondary cores on any SoC as long as the device tree says enable-method = "psci". The cost is that a firmware bug now lives below the kernel, so you need TF-A debug builds and a secure UART in your bring-up toolbox.

**Q.** "How are secondary cores brought up on an arm64 Linux system?"

**A.** 리셋 직후 모든 코어가 BL1에서 출발하고, primary가 아닌 코어는 hold pen에서 `wfe`로 대기한다. Linux의 `smp_init`이 각 CPU마다 `cpu_psci_cpu_boot()`를 부르면 PSCI `CPU_ON`(x0=0xC4000003, x1=대상 MPIDR, x2=`secondary_entry` 물리주소)이 SMC로 BL31에 간다. BL31은 그 코어의 hold pen 슬롯에 warm entry를 쓰고 SEV한 뒤 SUCCESS를 돌려준다. 깨어난 코어는 BL31에서 자기 EL3 상태를 초기화하고 Linux의 `secondary_entry`로 ERET한다. QEMU TF-A에서 핫플러그로 직접 확인했고, online은 약 2 ms였는데 대부분이 커널 hotplug 상태 머신 비용이었다.

> All cores come out of reset into BL1; the non-primary ones park in a holding pen, waiting with WFE. When Linux brings up CPUs, cpu_psci_cpu_boot issues a PSCI CPU_ON SMC — function ID 0xC4000003 in x0, the target MPIDR in x1 and the physical address of secondary_entry in x2. BL31 writes the warm-boot entry into that core's pen slot, cleans it to memory, sends SEV and returns success. The woken core initialises its EL3 state and ERETs into the kernel's secondary_entry. Hotplug uses the same path: offlining calls CPU_OFF on the dying core and the survivor polls AFFINITY_INFO until it reports OFF, which is the "CPU1 killed (polled 0 ms)" message.

**꼬리 질문**: "What if the board has no PSCI firmware?" → `enable-method = "spin-table"`: 부트로더가 코어를 메모리 주소 폴링 루프에 두고, 커널이 `cpu-release-addr`에 주소를 써서 깨운다. 대신 hotplug로 코어를 끌 수 없다.

**Q.** "What is the SMC Calling Convention? What goes in x0?"

**A.** Arm이 정한 SMC/HVC 호출 규약이다. x0에 32비트 function ID를 넣는데, bit 31이 fast/yielding, bit 30이 SMC32/SMC64, bit 29:24가 OEN(서비스 주인 — 0 Arm, 2 SiP, 4 PSCI 같은 표준, 50~63 Trusted OS), 하위 16비트가 함수 번호다. 인자는 x1부터, 결과는 x0~x3(v1.2부터 x17까지)로 돌아온다. v1.1부터 결과로 안 쓰는 x4~x17을 보존해서 커널이 인라인으로 싸게 부를 수 있다. 커널에서는 `arm_smccc_1_1_invoke()`를 쓰는데, 펌웨어가 v1.0이면 conduit이 NONE이라 호출 없이 -1을 돌려준다는 걸 모듈로 확인했다.

> SMCCC defines how software calls into EL3 or EL2 with SMC or HVC. x0 holds a 32-bit function ID: bit 31 says fast or yielding, bit 30 SMC32 or SMC64, bits 29 to 24 the owning entity — Arm architecture, SiP, OEM, standard services like PSCI, or a trusted OS — and the low 16 bits the function number. Arguments go in x1 onward and results come back in x0 to x3, extended to x17 from v1.2. Since v1.1 the callee preserves x4 to x17 unless they're results, which is what makes the kernel's inline arm_smccc_1_1_invoke cheap. One gotcha: if firmware only implements SMCCC 1.0, the 1.1 helpers don't issue the call at all and return NOT_SUPPORTED, so drivers check arm_smccc_1_1_get_conduit first.

**Q.** "Explain secure boot on an ARM SoC. Where is the root of trust, and how do you prevent rollback?"

**A.** 뿌리는 BootROM과 OTP fuse에 태운 ROTPK 해시다. BootROM 또는 BL1이 인증서의 공개키를 해시해 fuse 값과 비교하고, 그 키로 다음 단계 인증서를 검증하고, 인증서 속 이미지 해시와 실제 이미지를 비교한다. TF-A의 TBBR CoT는 ROT 키 → Trusted/Non-trusted world 키 → 이미지별 key cert → content cert로 나뉘어 있어서, U-Boot 서명 키가 BL31을 서명할 수 없다. U-Boot은 자기 DTB에 박힌 공개키로 FIT 이미지를 검증해 커널까지 체인을 잇는다. 롤백은 인증서의 NV counter와 OTP 카운터를 비교해서 막는다. 직접 FIP의 U-Boot을 1바이트 바꿔 `-80`(EAUTH)으로 거부되는 것과, 카운터를 올린 인증서가 거부되는 것을 확인했다.

> The root of trust is the boot ROM plus a hash of the root public key burned into OTP fuses. The ROM or BL1 hashes the public key in the first certificate, compares it to the fuses, verifies the certificate signature with it, and then compares the image hash in the certificate with the image it loaded. TF-A's TBBR chain splits keys into a trusted-world key and a non-trusted-world key, each delegating to per-image key and content certificates, so the team that signs U-Boot can't sign BL31. U-Boot continues the chain by verifying a signed FIT image with a public key embedded in its own device tree, and dm-verity carries it into the root filesystem. Rollback protection comes from NV counters: every certificate carries a version counter that must be at least the value in OTP, and the OTP counter is bumped when a newer image is accepted. I tested it in QEMU — flipping one byte of U-Boot inside the FIP makes BL2 fail with EAUTH.

**꼬리 질문**: "What's the difference between secure boot and measured boot?" → secure boot은 검증 실패 시 **부팅을 막는다**. measured boot은 각 단계의 해시를 TPM PCR이나 이벤트 로그에 **기록만** 하고, 나중에 원격 증명(attestation)으로 판단한다. 둘은 같이 쓴다.

**Q.** "What is OP-TEE, and how does a Linux application use a TEE?"

**A.** OP-TEE는 S-EL1에서 도는 오픈소스 Trusted OS이고, 그 위 S-EL0에서 UUID로 식별되는 TA가 돈다. Linux 앱은 GlobalPlatform Client API(`libteec`)로 세션을 열고 명령을 보내며, 커널 `optee` 드라이버가 `/dev/tee0`를 통해 받은 요청을 공유 메모리와 SMC로 바꿔 EL3의 SPD(opteed)에 넘기면 SPD가 세계를 바꿔 OP-TEE로 전달한다. 파일 저장 같은 일은 `tee-supplicant`가 `/dev/teepriv0`로 대행한다. 키 보관, 기기 인증, fTPM, DRM에 쓴다. QEMU에서 OP-TEE 4.7을 BL32로 넣어 `/dev/tee0`과 DT의 `reserved-memory`, `/firmware/optee` 노드가 생기는 것을 확인했다.

> OP-TEE is an open-source trusted OS running at secure EL1, hosting trusted applications at secure EL0 identified by UUID. A Linux application uses the GlobalPlatform client API in libteec to open a session and invoke commands; the kernel's optee driver turns that into an SMC with arguments in shared memory, the secure payload dispatcher in BL31 switches worlds, and OP-TEE runs the TA. tee-supplicant serves OP-TEE's requests back into Linux, such as storing encrypted objects in the filesystem. Typical uses are key storage, device identity and attestation, firmware TPM and DRM. When I added it as BL32 in QEMU, OP-TEE patched the device tree with no-map reserved memory and a /firmware/optee node, and the kernel created /dev/tee0 and /dev/teepriv0.

**Q.** "A new board boots Linux on one core only. How do you debug it?"

**A.** 먼저 `dmesg | grep psci`로 PSCI가 감지됐는지 보고, DT의 `/psci` method와 CPU 노드 `enable-method`, `reg`(MPIDR)를 확인한다. `failed to boot CPU1 (-2)` 같은 에러 코드가 있으면 펌웨어가 인자를 거부한 것이고, 에러 없이 안 올라오면 SMC는 성공했는데 코어가 돌아오지 못한 것이라 전원 컨트롤러·리셋 벡터·BL31 메모리 예약을 본다. TF-A를 디버그 로그로 다시 빌드하고 `/sys/kernel/debug/psci`로 펌웨어가 무엇을 지원하는지 확인한다.

> First check dmesg for the psci probe lines and the device tree: the /psci method, each CPU's enable-method and its reg value, which is the MPIDR passed to CPU_ON — cluster affinity bits are a classic mistake. An error code like "failed to boot CPU1 (-2)" means firmware rejected the arguments; if CPU_ON succeeded but the core never shows up, the problem is after the SMC — power controller, reset vector, or BL31's memory not being reserved so Linux overwrote it. Then rebuild TF-A with DEBUG=1 and LOG_LEVEL=40, watch the secure console, and use /sys/kernel/debug/psci to see which functions the firmware actually advertises.

---

## 12. 직접 해보기

1. **개념**: `SPSR = 0x3c9`를 해석하라. 이 값으로 ERET하면 어느 EL, 어느 스택 포인터, 인터럽트 상태는? 그리고 BL2가 BL31로 직접 점프하지 못하고 `BL1: Booting BL31`이 찍히는 이유를 설명하라.
   정답/힌트: M[3:0]=0b1001 → EL2h(SP_EL2), DAIF=1111 전부 마스크. BL2는 S-EL1이라 EL3로는 예외(SMC)로만 올라갈 수 있다 — `BL1_SMC_RUN_IMAGE`.
2. **소스 읽기**: `platform_def.h`에서 BL31_BASE를 0x10000 낮추면(다른 값은 그대로) BL2의 위치는 어떻게 바뀌나? 실제로 바꿔 빌드하고 `Loading image id=3 at address`로 확인하라.
   정답/힌트: `BL2_BASE = BL31_BASE - 0x35000`이라 BL2도 같이 0x10000 내려간다. 단 `BL31_LIMIT`는 그대로라 BL31 공간이 늘어난다.
3. **코드 과제**: `c5_smc`에 모듈 파라미터 `fid`와 `arg1`을 추가해 임의의 SMC를 부르고 결과 a0~a3를 찍게 하라. `PSCI_FEATURES`로 0x84000000~0x84000015를 전부 물어 TF-A와 QEMU 내장 PSCI의 지원 표를 만들어라.
   정답/힌트: `module_param(fid, uint, 0444)`. QEMU에서는 DT method가 hvc이므로 `arm_smccc_hvc`를 써야 한다(1.1 helper는 NONE).
4. **관찰**: TF-A + OP-TEE 구성에서 `echo 0 > /sys/devices/system/cpu/cpu0/online`을 시도하라. 무슨 일이 일어나나? 커널 `cpu_psci_cpu_can_disable()`와 `psci_tos_resident_on()`을 읽고 설명하라.
   정답/힌트: 먼저 `ls /sys/devices/system/cpu/cpu0/`에 `online` 파일이 있는지부터 확인하라. 소스상 `cpu_psci_cpu_disable()`는 Trusted OS가 그 코어에 상주한다고 보고되면(`MIGRATE_INFO_TYPE`/`MIGRATE_INFO_UP_CPU`) `-EPERM`으로 거부한다 — 이 랩의 OP-TEE가 그렇게 보고하는지 `/sys/kernel/debug/psci`로 확인할 것.
5. **측정**: `c5_hotplug`를 `-smp 2`와 `-smp 8`에서 돌려 online/offline 중앙값이 어떻게 변하는지 그래프로 그려라. QEMU 하나만 띄우고, 같은 조건에서 두 번씩.
   정답/힌트: offline 비용 대부분은 태스크·IRQ 이주와 hotplug 콜백이라 코어 수의 영향을 받는다. TCG는 호스트 부하에 민감하니 `uptime`도 같이 적어라.
6. **secure boot**: TBB 빌드에서 BL33 대신 **NT FW content cert**(`--nt-fw-cert`)를 다른 빌드(tbb-nv-build)의 것으로 바꿔 넣어라. 어느 image id에서 실패하나? 이유는?
   정답/힌트: 두 빌드는 키가 다르다(`Creating new key`). 바꾼 content cert는 NTW 쪽 키가 다른 체인으로 서명돼 있어서 id 15의 서명 검증에서 실패해야 한다 — 로그로 확인하라.
7. **QEMU의 `-d int`**: TF-A + Linux 부팅 중 `-d int`로 SMC 예외만 세어 보라(`grep -c "Taking exception 7 \[SMC\]"` 같은 식으로 먼저 로그 형식을 확인). 부팅 동안 SMC는 몇 번 일어나나? 핫플러그 한 번은?
   정답/힌트: 로그가 매우 크니 `-D`로 파일에 쓰고 `-smp 1`로 줄여라. 예외 이름 문자열은 직접 확인할 것.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| EL (Exception Level) | 예외 수준 0~3 | AArch64의 특권 단계. 숫자가 클수록 특권이 높다 |
| TrustZone | Arm 보안 확장 | 코어·버스·메모리를 Secure/Non-secure 두 세계로 나눈다 |
| SCR_EL3.NS | Secure Configuration Register의 NS 비트 | EL3 아래가 어느 세계인지 정한다. EL3만 쓸 수 있다 |
| SPSR | Saved Program Status Register | ERET할 때 복원할 PSTATE. M[3:0]이 목적지 EL |
| TF-A | Trusted Firmware-A | Arm 레퍼런스 secure 펌웨어(BL1/BL2/BL31) |
| BL1 / BL2 / BL31 / BL32 / BL33 | 부트 단계 | ROM · 트러스티드 부트 · EL3 런타임 · Trusted OS · 일반 부트로더 |
| FIP | Firmware Image Package | BL2~BL33과 인증서를 묶은 파일. `fiptool`로 다룬다 |
| image ID | TF-A 이미지 번호 | BL2=1, BL31=3, BL32=4, BL33=5, 인증서 6~15 |
| SPD | Secure Payload Dispatcher | BL31 안에서 Trusted OS로 요청을 넘기는 모듈(opteed 등) |
| PSCI | Power State Coordination Interface | CPU_ON/OFF/SUSPEND, SYSTEM_OFF/RESET 표준 펌웨어 API |
| SMCCC | SMC Calling Convention | function ID 구조와 레지스터 규약 |
| OEN | Owning Entity Number | function ID의 bit 29:24. 어느 서비스로 보낼지 |
| SiP | Silicon Provider | SoC 벤더 전용 SMC 서비스 영역(OEN 2) |
| conduit | 호출 수단 | SMC(EL3로) 또는 HVC(EL2로) |
| MPIDR | Multiprocessor Affinity Register | 코어 주소. CPU_ON의 대상 지정 |
| hold pen | 대기 우리 | secondary 코어가 entry 주소를 기다리며 도는 루프·메모리 |
| OP-TEE | Open Portable TEE | S-EL1에서 도는 오픈소스 Trusted OS |
| TA | Trusted Application | S-EL0의 보안 앱. UUID로 식별 |
| tee-supplicant | 유저 공간 데몬 | OP-TEE의 파일·RPC 요청을 Linux 쪽에서 대행 |
| ROTPK | Root Of Trust Public Key | 신뢰 뿌리 공개키. 해시를 OTP fuse에 둔다 |
| CoT | Chain of Trust | 각 단계가 다음 단계를 검증하는 연쇄 |
| TBBR | Trusted Board Boot Requirements | Arm의 secure boot 요구사항·인증서 구조 |
| NV counter | Non-volatile counter | anti-rollback용 버전 카운터(OTP) |
| EAUTH (-80) | 인증 에러 | TF-A가 이미지/인증서 검증 실패 시 돌려주는 값 |
| FIT | Flattened Image Tree | U-Boot의 다중 이미지 포맷. 서명 가능 |
| measured boot | 측정 부팅 | 해시를 기록만 하고 막지는 않음(TPM 등) |

---

## 14. 요약 & 체크리스트

AArch64는 특권을 EL0~EL3로 나누고 TrustZone으로 두 세계를 나눈다. 두 축의 꼭대기 EL3에는 TF-A의 BL31이 상주하면서, Linux가 `smc`로 보내는 PSCI(CPU 켜기·끄기, 전원 끄기, 리셋)와 SMCCC 서비스를 처리하고, Trusted OS(OP-TEE)로 가는 세계 전환을 맡는다. 부팅은 BL1(ROM, EL3) → BL2(S-EL1, 다음 이미지 로드·검증) → BL31(EL3 상주) → BL32(OP-TEE) → BL33(U-Boot) → Linux 순이고, 각 이미지의 위치는 플랫폼의 `platform_def.h`에 고정돼 있어서 RAM 구성과 어긋나면 `-m 512`처럼 조용히 멈춘다. Linux의 secondary CPU 부팅과 핫플러그는 `cpu_psci_ops` → `psci_ops` → `arm_smccc_smc` → BL31 디스패처 → 플랫폼 PM 훅 → hold pen으로 이어지고, 이 지연의 대부분은 펌웨어가 아니라 커널 hotplug 상태 머신이다. secure boot는 OTP의 ROTPK 해시에서 시작해 TBBR 인증서 트리로 BL2·BL31·BL32·BL33을 검증하고, U-Boot의 FIT 서명이 커널까지 잇는다. 1바이트 변조는 EAUTH(-80)로, 카운터 문제는 NV counter 검사로 거부된다.

- [ ] EL × 세계 행렬을 그리고 각 칸의 소프트웨어(앱, Linux, KVM, BL31, OP-TEE, TA)를 말할 수 있다
- [ ] SPSR 값(0x3c5, 0x3c9, 0x3cd)으로 다음 단계의 EL을 읽을 수 있다
- [ ] BL1·BL2·BL31·BL32·BL33의 역할, EL, 상주 여부를 표로 설명할 수 있다
- [ ] TF-A를 `DEBUG=1 LOG_LEVEL=40`으로 빌드하고 FIP를 `fiptool info`로 열 수 있다
- [ ] "-m 512에서 멈춘다"를 `NS_IMAGE_OFFSET`과 `-d int`의 FAR로 증명할 수 있다
- [ ] PSCI 주요 함수의 ID·인자·반환값과 리눅스 호출 지점을 말할 수 있다
- [ ] secondary CPU가 hold pen → CPU_ON → warm entry → `secondary_entry`로 켜지는 과정을 그림으로 설명할 수 있다
- [ ] SMCCC function ID를 비트 단위로 해독하고, 커널 모듈에서 `arm_smccc_smc`/`arm_smccc_1_1_invoke`로 펌웨어를 부를 수 있다
- [ ] SMCCC v1.0 펌웨어에서 1.1 헬퍼가 conduit NONE으로 -1을 돌려주는 이유를 설명할 수 있다
- [ ] OP-TEE의 위치(S-EL1), TA, `/dev/tee0`, tee-supplicant, reserved-memory의 관계를 설명할 수 있다
- [ ] TBBR chain of trust(ROTPK → key cert → content cert → 이미지 해시)를 그리고 변조·롤백이 어디서 막히는지 말할 수 있다
- [ ] U-Boot FIT 서명이 TF-A 체인을 커널까지 잇는 방법(공개키가 U-Boot DTB 안)과 체인이 끊기는 흔한 구멍을 말할 수 있다

## 참고 자료

- Trusted Firmware-A 문서: https://trustedfirmware-a.readthedocs.io/ — "Firmware Design"(BL 단계, FIP), "Trusted Board Boot", "QEMU virt Armv8-A" 플랫폼 문서. 소스는 랩의 `/var/lab/src/tfa`(v2.15.0): `plat/qemu/`, `bl1/`, `bl2/`, `bl31/`, `common/runtime_svc.c`, `lib/psci/`, `drivers/auth/auth_mod.c`, `include/tools_share/tbbr_oid.h`
- Arm, "Power State Coordination Interface" (DEN0022) — PSCI 함수·반환값 정의
- Arm, "SMC Calling Convention" (DEN0028) — function ID 비트 배치, 레지스터 규약, 버전별 차이
- Linux 커널 문서 "Booting AArch64 Linux": https://docs.kernel.org/arch/arm64/booting.html — 부트로더가 커널에 넘길 때의 EL·레지스터 조건, `enable-method`
- DT 바인딩: 커널 소스 `Documentation/devicetree/bindings/arm/psci.yaml`, `Documentation/devicetree/bindings/arm/firmware/linaro,optee-tz.yaml`
- 커널 소스(6.18): `drivers/firmware/psci/psci.c`, `arch/arm64/kernel/psci.c`, `include/linux/arm-smccc.h`, `drivers/firmware/smccc/smccc.c`, `drivers/tee/optee/` — https://elixir.bootlin.com/linux/v6.18/source 에서 탐색
- OP-TEE 문서: https://optee.readthedocs.io/ — 아키텍처(core, TA, secure storage), QEMU v8 빌드
- U-Boot 문서, FIT 서명: https://docs.u-boot.org/en/latest/usage/fit/signature.html
- 이어지는 노트: C1(부트 지도), C3·C4(U-Boot), C6(커널 시작), E3(커널 모듈), G4(문자 디바이스), N5·N6(전원 관리), L4(OTA), L5(secure boot 체인), L6(런타임 보안)
