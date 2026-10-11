# C2. BootROM과 부트 미디어 — 모드 핀·eFuse·fallback, SPI NOR·NAND·eMMC·SD 레이아웃, USB 복구 모드

> **이 노트를 다 읽으면**: 전원 ON 직후 SoC의 BootROM이 "어디서, 무엇을, 어떤 순서로" 읽는지 설명할 수 있다 · 랩의 `flash.bin`(BL1 + FIP)을 hexdump와 `fiptool`로 바이트 단위까지 해부할 수 있다 · GPT + raw 부트로더 영역 + FAT + ext4 디스크 이미지를 root 없이 만들어 U-Boot로 부팅할 수 있다 · eMMC boot0/boot1/RPMB와 EXT_CSD[179]를 직접 읽고 `bootpart enable`을 ioctl로 할 수 있다 · 벽돌이 된 보드를 USB 복구 모드로 살리는 흐름을 말할 수 있다
> **왜 필요한가**: 새 보드 bring-up 첫날 "시리얼에 아무것도 안 뜬다", 공장 라인 "eMMC 플래싱 스크립트를 짜라", 필드 "업데이트 중 전원이 나가서 벽돌이 됐다" — 모두 BootROM과 부트 미디어 레이아웃을 알아야 푸는 문제다. 면접에서는 C1(부트 전체 지도)의 첫 단계를 깊게 파고드는 꼬리 질문으로 나온다
> **Don 기준 난이도**: SSD 컨트롤러의 ROM 코드 → NAND/SPI에서 FW 로드, 배드 블록·ECC·OOB는 Don이 현업으로 해 온 일이다 / 그 지식을 "애플리케이션 프로세서 SoC의 부트 모드 선택, eMMC 하드웨어 파티션, GPT와 raw 오프셋, 벤더별 이미지 헤더, USB 복구 툴"로 번역하는 것이 새로 배울 부분
> **선행 노트**: C1 (부트 전체 지도). 같이 보면 좋은 노트: C3 (U-Boot 사용), C5 (ARM 신뢰 부팅 체인), J1·J2 (스토리지·MTD)

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어를 할 때를 떠올려 보자. 컨트롤러 칩 안에는 **수정할 수 없는 ROM 코드**가 있었고, 전원이 들어오면 그 ROM이 SPI NOR나 NAND의 정해진 위치에서 펌웨어 이미지를 찾아 SRAM(또는 TCM)에 올리고 점프했다. 이미지가 깨졌으면 두 번째 사본을 찾고, 그것도 없으면 제조 모드(UART/벤더 커맨드)로 빠져 공장에서 다시 쓸 수 있게 했다.

Linux가 도는 애플리케이션 프로세서(i.MX8M, AM62x, RK3588, Jetson Orin…)도 **완전히 같은 구조**다. 이름만 다르다.

| SSD 컨트롤러에서 | Linux SoC에서 | 이 노트의 절 |
|---|---|---|
| ROM 부트 코드 | **BootROM** (mask ROM, PBL·RBL·BROM이라고도 부른다) | §1 |
| 스트랩 핀·OTP 설정 | **boot mode 핀 + eFuse(OTP)** | §2 |
| FW 이미지 헤더(시그니처·CRC·로드 주소) | **IVT/container, X.509 cert, idbloader, FIP** | §3 |
| SPI NOR / NAND 레이아웃 | **SPI NOR, raw NAND, eMMC, SD, UFS** | §4–§9 |
| 제조 모드, 벤더 커맨드 | **USB 복구 모드**(NXP SDP, TI DFU, Rockchip maskrom, Allwinner FEL) | §9 |
| 이중 FW 슬롯 | **A/B 부트로더 사본, 보조 이미지** | §10 |

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="4" y="22" width="672" height="78" rx="8" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-dasharray="6 4"/>
  <text x="12" y="16" font-size="12">C2 범위: 1~4단계 + 미디어 레이아웃</text>
  <rect x="12" y="32" width="150" height="58" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="87" y="56" text-anchor="middle" font-size="13">1. 전원 ON / 리셋</text>
  <text x="87" y="76" text-anchor="middle" font-size="12">CPU가 ROM에서 시작</text>
  <rect x="182" y="32" width="150" height="58" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="257" y="56" text-anchor="middle" font-size="13">2. BootROM</text>
  <text x="257" y="76" text-anchor="middle" font-size="12">모드 핀·eFuse 읽기</text>
  <rect x="352" y="32" width="150" height="58" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="427" y="56" text-anchor="middle" font-size="13">3. 부트 미디어 읽기</text>
  <text x="427" y="76" text-anchor="middle" font-size="12">헤더 확인·서명 검증</text>
  <rect x="522" y="32" width="150" height="58" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="597" y="56" text-anchor="middle" font-size="13">4. 온칩 SRAM 실행</text>
  <text x="597" y="76" text-anchor="middle" font-size="12">SPL / BL2 (수백 KiB)</text>
  <line x1="162" y1="61" x2="180" y2="61" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="332" y1="61" x2="350" y2="61" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="502" y1="61" x2="520" y2="61" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="427" y="124" text-anchor="middle" font-size="12" font-weight="bold">실패 시 → 다음 미디어 → USB 복구 모드</text>
  <line x1="597" y1="90" x2="597" y2="148" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="522" y="150" width="150" height="58" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="597" y="174" text-anchor="middle" font-size="13">5. DRAM 초기화</text>
  <text x="597" y="194" text-anchor="middle" font-size="12">DDR 트레이닝 (SPL)</text>
  <rect x="352" y="150" width="150" height="58" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="427" y="174" text-anchor="middle" font-size="13">6. U-Boot (DRAM)</text>
  <text x="427" y="194" text-anchor="middle" font-size="12">BL31·OP-TEE와 함께</text>
  <rect x="182" y="150" width="150" height="58" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="257" y="174" text-anchor="middle" font-size="13">7. Linux 커널</text>
  <text x="257" y="194" text-anchor="middle" font-size="12">Image + DTB</text>
  <rect x="12" y="150" width="150" height="58" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="87" y="174" text-anchor="middle" font-size="13">8. init / rootfs</text>
  <text x="87" y="194" text-anchor="middle" font-size="12">C6 · C7</text>
  <line x1="522" y1="179" x2="504" y2="179" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="352" y1="179" x2="334" y2="179" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="182" y1="179" x2="164" y2="179" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="340" y="244" text-anchor="middle" font-size="12">1~4는 DRAM 없이 돈다: ROM 코드 + 온칩 SRAM만 쓸 수 있다</text>
  <text x="340" y="266" text-anchor="middle" font-size="12">그래서 첫 단계 이미지는 작아야 하고, 위치·형식은 ROM이 정한다</text>
</svg>
```
그림 1 — 전원 ON부터 init까지. C2는 주황 점선 안(BootROM이 부트 미디어에서 첫 단계 이미지를 꺼내 SRAM에서 실행하기까지)과, 그 이미지가 미디어의 어디에 어떤 모양으로 놓여야 하는지를 다룬다.

Embedded Linux 엔지니어가 이 주제를 만나는 장면은 이렇다.

- **bring-up 첫날**: 보드에 전원을 넣었는데 시리얼이 조용하다. BootROM이 SD를 보고 있는데 이미지를 eMMC user area에 썼거나, 32 KiB가 아니라 33 KiB 오프셋에 써야 하는 SoC였을 수 있다 (§7).
- **공장 플래싱**: 빈 eMMC가 달린 보드를 USB 하나로 깨워서 부트로더는 boot0에, rootfs는 user area에 쓰고, EXT_CSD로 "boot0에서 부팅"을 켜는 스크립트 (§6, §9).
- **필드 업데이트**: 부트로더를 업데이트하다 전원이 나가면 보드가 벽돌이 된다. boot0/boot1을 A/B로 쓰거나 보조 이미지를 두는 설계 (§10, L4).
- **보안**: eFuse에 공개키 해시를 굽고 "서명 안 된 이미지는 거부" 퓨즈를 태우는 순간부터 BootROM이 secure boot의 뿌리(root of trust)가 된다 (§3.4, C5, L5).

> 실행 환경: 모든 출력은 Mac의 Lima VM `elinux`(aarch64, Ubuntu 26.04, `uname -r` = `7.0.0-38-generic`)와 그 안의 QEMU 10.2.1에서 실제로 돌린 결과다. 공용 랩 `/var/lab/`(TF-A v2.15 → U-Boot v2026.10 → Linux 6.18.55 → BusyBox 1.37 initramfs)은 읽기 전용이라 산출물은 전부 `/var/tmp/C2/`에 만들었고, 소스·스크립트는 Mac 쪽 `/Users/donh/.elinux_scratch/C2/`에 있다(아래에서 `S=/Users/donh/.elinux_scratch/C2`). QEMU는 KVM 없이 TCG로 돈다. 실제 보드가 없으므로 **벤더별 BootROM 동작(i.MX, TI K3, Rockchip, Allwinner, Raspberry Pi)은 데이터시트·공개 문서를 바탕으로 한 기억 기반 설명**이고, 해당 문장에 "(기억 기반)"이라고 표시했다. 양산 설계 전에는 반드시 그 SoC의 Reference Manual "System Boot" 장을 확인한다.

---

## 1. BootROM — 칩 안에 박힌, 고칠 수 없는 첫 번째 코드

### 1.1 직관

BootROM은 **칩을 만들 때 금속 배선 마스크로 새겨 넣은 코드**다(mask ROM). 그래서:

- **수정할 수 없다.** 버그가 있으면 칩 리비전(새 마스크)으로만 고친다. 그래서 ROM 코드는 작고 보수적으로 짜이고, 벤더는 "ROM errata"를 따로 문서화한다.
- **리셋 벡터가 ROM을 가리킨다.** Cortex-A의 경우 리셋 직후 CPU는 SoC가 정한 주소(대부분 ROM의 시작)에서 명령을 가져온다. Don이 아는 Cortex-M의 "0x0의 벡터 테이블"과 같은 개념이다.
- **DRAM을 쓸 수 없다.** DDR 컨트롤러와 PHY는 보드마다 다른 타이밍·트레이닝이 필요하고, 그 파라미터를 ROM이 알 수 없다. 그래서 BootROM은 **온칩 SRAM(OCRAM, MSRAM 등 이름은 벤더마다 다름)** 만 쓴다.

비유하면 BootROM은 "SSD 컨트롤러의 ROM 로더 + 제조 모드 처리기"를 그대로 SoC에 옮긴 것이다.

### 1.2 BootROM이 하는 일 (순서대로)

1. 최소한의 클럭·전원 도메인을 켜고 내부 SRAM, 워치독을 설정한다.
2. **boot mode 핀**(전원 ON 순간 래치되는 스트랩 핀)과 **eFuse(OTP)** 를 읽어 부트 소스를 정한다 (§2).
3. 그 장치의 컨트롤러(SD/eMMC 호스트, FlexSPI/OSPI, NAND 컨트롤러, USB)를 **ROM이 가진 최소 드라이버**로 초기화한다. 느리고 안전한 모드(예: SD 1-bit/저속 클럭, SPI 1-bit read)로 시작하는 경우가 많다.
4. 미디어의 **정해진 오프셋**(또는 eMMC boot 파티션, 또는 FAT 파일)에서 **이미지 헤더**를 찾고 검증한다 (§3).
5. 헤더가 가리키는 만큼을 SRAM에 복사한다. 보안 퓨즈가 켜져 있으면 **서명을 검증**한다.
6. 성공하면 이미지의 entry point로 점프한다. 실패하면 **보조 이미지 → 다음 부트 장치 → USB/UART 복구 모드** 순으로 떨어진다(fallback).

### 1.3 랩에서 BootROM 역할을 하는 것 — TF-A BL1

QEMU `virt` 머신은 진짜 SoC BootROM이 없다. 대신 랩은 TF-A의 **BL1**을 그 자리에 놓았다. TF-A 설계에서 BL1은 원래 "AP Trusted ROM", 즉 **ROM에 구울 것을 전제로 만든 첫 단계**다. `-machine virt,secure=on -bios flash.bin`으로 켜면 QEMU는 flash 0을 주소 0x0(secure 전용)에 매핑하고, CPU는 EL3로 리셋되어 BL1을 그 자리에서 실행한다. BL1은 secure SRAM(0x0e000000, 1 MiB)에 BL2를 올리고, BL2가 BL31과 U-Boot(BL33)를 올린다.

TF-A의 QEMU 플랫폼 정의에서 이 숫자들을 확인할 수 있다 (소스에서 grep, 일부만 발췌).

```sh
grep -nE "define (SEC_ROM_BASE|SEC_ROM_SIZE|SEC_SRAM_BASE|SEC_SRAM_SIZE|NS_DRAM0_BASE|NS_IMAGE_OFFSET|PLAT_QEMU_FIP_BASE)" \
  /var/lab/src/tfa/plat/qemu/qemu/include/platform_def.h
```

```text
80:#define SEC_ROM_BASE			0x00000000
81:#define SEC_ROM_SIZE			0x00020000
83:#define NS_DRAM0_BASE			ULL(0x40000000)
86:#define SEC_SRAM_BASE			0x0e000000
87:#define SEC_SRAM_SIZE			0x00100000
192:#define NS_IMAGE_OFFSET			(NS_DRAM0_BASE + 0x20000000)
223:#define PLAT_QEMU_FIP_BASE		0x00040000
```

볼 것: "ROM" 128 KiB(0x0), secure SRAM 1 MiB(0x0e000000), FIP는 flash의 256 KiB(0x40000) 위치, U-Boot(BL33)는 DRAM 0x60000000에 올라간다. 실제 SoC로 치면 "BootROM + OCRAM + 부트 미디어 오프셋 + DRAM 로드 주소"에 해당한다.

### 1.4 예제 1 — 랩에서 BootROM → SRAM → DRAM 체인을 실제로 돌려 보기

무엇을 확인하나: `flash.bin` 하나로 BL1(ROM 역할) → BL2 → BL31 → U-Boot가 차례로 뜨는지, U-Boot가 부트 장치를 순서대로 훑는(fallback) 모습을 본다. README의 주의대로 secure 경로는 `-m 1057` 이상이 필요하다.

```sh
cd /var/tmp/C2
timeout 40 qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -m 1057 -nographic \
  -bios /var/lab/out/flash.bin < /dev/null > c2_tfa_boot.log 2>&1
tr -d '\r' < c2_tfa_boot.log    # 제어 문자를 지운 뒤 앞부분과 bootdev 스캔 부분만 발췌
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
Flash: 32 MiB
Loading Environment from Flash...*** Warning - bad CRC, using default environment
...
Scanning for bootflows in all bootdevs
...
Scanning bootdev 'fw-cfg@9020000.bootdev':
fatal: no kernel available
scanning bus for devices...
Scanning bootdev 'virtio-net#32.bootdev':
BOOTP broadcast 1
DHCP client bound to address 10.0.2.15 (2 ms)
...
TFTP error: 'Access violation' (2)
Not retrying...
No more bootdevs
```

출력에서 볼 것:

- `BL1: Booting BL2` → `BL1: Booting BL31`: BL2가 BL31과 BL33을 로드한 뒤 **BL1에게 SMC로 돌아가서** BL31로 넘어간다(Arm v8-A의 BL2-at-EL1 구성). 자세한 EL 전환은 C5.
- `Flash: 32 MiB`: 비보안 경로(§8, `-bios u-boot.bin`)에서는 같은 U-Boot가 `Flash: 64 MiB`라고 한다. `secure=on`이면 QEMU가 flash 0(BL1·FIP가 든 쪽)을 secure 세계 전용으로 매핑하기 때문에 non-secure인 U-Boot가 보는 flash 구성이 달라진 것이다(정확한 크기 계산은 U-Boot CFI 드라이버와 QEMU 매핑의 세부라 여기서 더 파지 않았다). 요점은 **같은 저장 장치라도 보안 상태에 따라 보이는 범위가 다르다**는 것 — 실제 SoC에서 eFuse·secure SRAM·RPMB가 그렇다.
- `bad CRC, using default environment`: 환경변수 영역이 비어 있어 CRC가 안 맞으니 기본값을 쓴다. U-Boot 환경 영역의 이중화(redundant env)는 §10.
- 마지막 부분은 **U-Boot의 fallback**이다: fw-cfg → virtio-net(TFTP) → 끝. BootROM의 fallback(SD → eMMC → USB)과 같은 생각이 소프트웨어 층에서 반복된다.

### 1.5 왜 단계를 나누나 — SRAM 크기가 결정한다

첫 단계 이미지는 **SRAM에 들어가야** 한다. 실제로 랩의 각 단계 크기를 재 보면(§4의 `fiptool unpack` 결과와 `ls -l`):

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <line x1="170.0" y1="34" x2="170.0" y2="240" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="170.0" y="256" text-anchor="middle" font-size="12">10K</text>
  <line x1="293.2" y1="34" x2="293.2" y2="240" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="293.2" y="256" text-anchor="middle" font-size="12">100K</text>
  <line x1="417.6" y1="34" x2="417.6" y2="240" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="417.6" y="256" text-anchor="middle" font-size="12">1M</text>
  <line x1="540.7" y1="34" x2="540.7" y2="240" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="540.7" y="256" text-anchor="middle" font-size="12">10M</text>
  <line x1="640.0" y1="34" x2="640.0" y2="240" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="640.0" y="256" text-anchor="middle" font-size="12">64M</text>
  <line x1="306.4" y1="30" x2="306.4" y2="240" stroke="#d0564a" stroke-dasharray="4 3"/>
  <text x="310.4" y="26" font-size="12">ROM 128K</text>
  <line x1="417.6" y1="30" x2="417.6" y2="240" stroke="#d0564a" stroke-dasharray="4 3"/>
  <text x="421.6" y="26" font-size="12">SRAM 1M</text>
  <text x="162" y="55" text-anchor="end" font-size="12">BL1 (ROM 역할)</text>
  <rect x="170" y="43" width="42.9" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="218.9" y="55" text-anchor="start" font-size="12">22.3 KiB</text>
  <text x="162" y="83" text-anchor="end" font-size="12">BL2</text>
  <rect x="170" y="71" width="47.9" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="223.9" y="83" text-anchor="start" font-size="12">24.5 KiB</text>
  <text x="162" y="111" text-anchor="end" font-size="12">BL31</text>
  <rect x="170" y="99" width="84.0" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="260.0" y="111" text-anchor="start" font-size="12">48.1 KiB</text>
  <text x="162" y="139" text-anchor="end" font-size="12">U-Boot (BL33)</text>
  <rect x="170" y="127" width="266.9" height="16" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="442.9" y="139" text-anchor="start" font-size="12">1.43 MiB</text>
  <text x="162" y="167" text-anchor="end" font-size="12">initramfs.cpio.gz</text>
  <rect x="170" y="155" width="255.2" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="431.2" y="167" text-anchor="start" font-size="12">1.15 MiB</text>
  <text x="162" y="195" text-anchor="end" font-size="12">Image.gz (gzip -9)</text>
  <rect x="170" y="183" width="390.7" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="566.7" y="195" text-anchor="start" font-size="12">14.54 MiB</text>
  <text x="162" y="223" text-anchor="end" font-size="12">Image</text>
  <rect x="170" y="211" width="445.9" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="609.9" y="223" text-anchor="end" font-size="12">40.76 MiB</text>
</svg>
```
그림 2 — 랩 부트 체인의 단계별 이미지 크기(로그 스케일, 실측: `ls -l`, `fiptool unpack`, `gzip -9 -c Image`로 잰 값). 빨간 점선은 QEMU 플랫폼의 ROM(128 KiB)과 secure SRAM(1 MiB). ROM·SRAM에서 도는 BL1·BL2·BL31은 수십 KiB, DRAM이 생긴 뒤에야 MiB 단위 U-Boot와 수십 MiB 커널을 다룰 수 있다.

실제 SoC도 같은 이유로 단계가 나뉜다: **ROM → (SRAM에 들어가는) SPL 또는 BL2 → DRAM 초기화 → U-Boot proper**. i.MX8M 계열의 OCRAM, TI K3의 MSRAM, Rockchip의 SRAM은 모두 수백 KiB 규모라서(정확한 크기는 SoC별 TRM 확인), DDR 트레이닝 펌웨어까지 포함한 SPL을 그 안에 맞추는 것이 C4(U-Boot 포팅)의 단골 고민이다.

### 1.6 흔한 함정

- "부트로더가 안 뜬다"를 U-Boot 문제로 단정하는 것. ROM 단계에서 실패하면 **U-Boot 배너조차 안 나온다**. 먼저 ROM이 어떤 장치를 보는지(모드 핀), 이미지가 올바른 오프셋·형식인지부터 의심한다.
- ROM 로그를 기대하는 것. 대부분의 BootROM은 콘솔에 아무것도 찍지 않는다. 대신 **USB 복구 모드로 떨어졌는지(호스트에서 `lsusb`로 VID:PID가 보이는지)**, 또는 ROM이 남기는 상태 레지스터·SRAM의 log buffer(벤더별, 예: i.MX ROM event log — 기억 기반)를 JTAG으로 읽는 것이 단서다.

---

## 2. 부트 소스 선택 — 모드 핀, eFuse, fallback 순서

### 2.1 세 가지 입력

| 입력 | 정체 | 언제 바뀌나 | Don에게 익숙한 것 |
|---|---|---|---|
| **boot mode 핀** | 리셋 해제 순간 래치되는 스트랩 핀(저항 풀업/풀다운, DIP 스위치) | 보드 설계·점퍼로 언제든 | SSD 컨트롤러의 strap 핀 |
| **eFuse / OTP** | 한 번만 쓸 수 있는 퓨즈 비트(부트 장치, 버스 폭, 보안 설정, 키 해시) | 공장에서 한 번 굽고 영원히 | 컨트롤러 OTP 영역 |
| **ROM의 고정 순서** | 위 두 가지로 정하지 않은 부분, 실패 시 다음 장치 | 칩 리비전으로만 | ROM의 재시도 정책 |

개발 보드는 DIP 스위치(모드 핀)로 SD/eMMC/USB를 바꿔 가며 쓰고, 양산 보드는 eFuse에 부트 장치를 굽고 "핀 무시" 퓨즈를 태워 **현장에서 누가 핀을 건드려도 부트 경로가 바뀌지 않게** 만든다.

### 2.2 벤더별 예 (기억 기반 — 데이터시트로 확인할 것)

| SoC 계열 | 부트 모드 선택 | 실패 시 |
|---|---|---|
| NXP i.MX8M (Mini/Nano/Plus) | `BOOT_MODE[1:0]` 핀: 00 = fuse에 따라, 01 = serial downloader(USB), 10 = internal boot(BOOT_CFG 핀 또는 fuse). `BT_FUSE_SEL` 퓨즈가 켜지면 핀 대신 fuse 값을 쓴다 | 보조 이미지(secondary image) 시도 후 USB serial downloader(SDP) |
| TI K3 (AM62x, AM64x) | `BOOTMODE[15:0]` 핀으로 **primary**와 **backup** 부트 장치를 각각 지정 | primary 실패 → backup(예: USB DFU, UART) |
| Rockchip (RK3399, RK3568, RK3588) | 고정 순서로 SPI NOR/SPI NAND → eMMC → SD 등을 훑는다(모델별 순서 다름) | 아무 것도 없으면 **maskrom** USB 모드 |
| Allwinner (H3, H6, A64 …) | 고정 순서: SD(MMC0) → eMMC(MMC2) → SPI NOR | **FEL** USB 모드 (특정 버튼·핀으로 강제 가능) |
| Raspberry Pi 4/5 | ROM이 SPI EEPROM의 2단계 부트로더를 실행, 그 EEPROM 설정 `BOOT_ORDER`가 SD/USB/NVMe/네트워크 순서를 정한다 | `rpiboot`(usbboot)로 USB 부팅 |

### 2.3 결정 흐름 그림

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="230" y="8" width="220" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="30" text-anchor="middle" font-size="13">리셋 → BootROM 시작</text>
  <line x1="340" y1="42" x2="340" y2="66" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="230" y="68" width="220" height="34" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="90" text-anchor="middle" font-size="13">BOOT_MODE 핀 래치</text>
  <rect x="10" y="140" width="190" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="105" y="160" text-anchor="middle" font-size="13">00: fuse 설정 사용</text>
  <text x="105" y="178" text-anchor="middle" font-size="12">양산 보드의 기본</text>
  <rect x="245" y="140" width="190" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="160" text-anchor="middle" font-size="13">10: internal boot</text>
  <text x="340" y="178" text-anchor="middle" font-size="12">BT_FUSE_SEL 퓨즈 확인</text>
  <rect x="480" y="140" width="190" height="46" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="575" y="160" text-anchor="middle" font-size="13">USB 복구 모드</text>
  <text x="575" y="178" text-anchor="middle" font-size="12">SDP, DFU, maskrom, FEL</text>
  <line x1="290" y1="102" x2="140" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="102" x2="340" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="390" y1="102" x2="540" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="482" y="114" font-size="12">01: serial download</text>
  <rect x="200" y="214" width="280" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="234" text-anchor="middle" font-size="13">부트 장치 결정</text>
  <text x="340" y="252" text-anchor="middle" font-size="12">fuse 값 또는 BOOT_CFG 핀</text>
  <line x1="105" y1="186" x2="250" y2="212" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="186" x2="340" y2="212" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="260" x2="340" y2="282" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="200" y="284" width="280" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="304" text-anchor="middle" font-size="13">헤더 읽기 → SRAM 로드</text>
  <text x="340" y="322" text-anchor="middle" font-size="12">magic · 크기 · 로드 주소 확인</text>
  <line x1="340" y1="330" x2="340" y2="352" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="200" y="354" width="280" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="376" text-anchor="middle" font-size="13">서명 검증 (closed 칩만)</text>
  <text x="340" y="396" text-anchor="middle" font-size="12">OK → 첫 단계(SPL)로 점프</text>
  <rect x="510" y="284" width="160" height="46" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="590" y="304" text-anchor="middle" font-size="13">실패</text>
  <text x="590" y="322" text-anchor="middle" font-size="12">보조 이미지 → 다음 장치</text>
  <line x1="480" y1="307" x2="508" y2="307" stroke="#d0564a" marker-end="url(#ah2)"/>
  <line x1="480" y1="380" x2="560" y2="332" stroke="#d0564a" marker-end="url(#ah2)"/>
  <line x1="590" y1="284" x2="590" y2="188" stroke="#d0564a" marker-end="url(#ah2)"/>
  <text x="598" y="240" font-size="12">모두 실패</text>
</svg>
```
그림 3 — BootROM의 부트 소스 결정 흐름(i.MX8M을 모델로 단순화, 기억 기반). 모드 핀이 1차로 나누고, internal boot에서는 fuse 또는 BOOT_CFG 핀이 장치를 정한다. 헤더·서명 검증이 실패하면 보조 이미지와 다음 장치를 거쳐 결국 USB 복구 모드로 떨어진다 — 그래서 **USB 복구 모드는 벽돌 보드의 마지막 안전망**이다.

### 2.4 예제 2 — 장난감 BootROM으로 fallback 감 잡기

무엇을 확인하나: "모드 핀 → fuse 오버라이드 → primary/secondary 사본 → 다음 장치 → serial download"라는 결정 순서를 C로 흉내 내고, 사본이 깨졌을 때·카드가 빠졌을 때 어떻게 떨어지는지 본다. 헤더는 우리가 정한 가짜 형식(`C2HD` magic + size + CRC32 + load)이다. 실제 ROM 형식은 §3.

```c
/* c2_bootrom.c - toy BootROM: pick boot source from pins/fuses, try primary+secondary copy,
 * check header + CRC, "load" into a 256 KiB SRAM buffer, else fall back to serial download. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>

#define SRAM_SIZE (256 * 1024)
struct hdr { char magic[4]; uint32_t size, crc, load; };     /* our made-up "IVT" */
static uint8_t sram[SRAM_SIZE];

static int try_copy(const char *dev, long off)
{
    FILE *f = fopen(dev, "rb");
    if (!f) { printf("  %-13s @0x%06lx: no media\n", dev, off); return -1; }
    struct hdr h;
    int ok = fseek(f, off, SEEK_SET) == 0 && fread(&h, sizeof h, 1, f) == 1;
    if (!ok || memcmp(h.magic, "C2HD", 4)) {
        printf("  %-13s @0x%06lx: bad magic\n", dev, off); fclose(f); return -1;
    }
    if (h.size > SRAM_SIZE) { printf("  %-13s: size %u > SRAM\n", dev, h.size); fclose(f); return -1; }
    if (fread(sram, 1, h.size, f) != h.size) { perror("  read"); fclose(f); return -1; }
    fclose(f);
    uint32_t c = crc32(0, sram, h.size);
    printf("  %-13s @0x%06lx: magic ok, %u B, crc %08x %s\n", dev, off, h.size, c,
           c == h.crc ? "OK -> jump to SRAM" : "MISMATCH");
    return c == h.crc ? 0 : -1;
}

int main(int argc, char **argv)
{
    /* BOOT_MODE pins: 0=fuses 1=serial-download 2=internal(boot cfg pins)
     * fuse BT_FUSE_SEL=1 means "use fuses, ignore cfg pins" (i.MX-like idea, simplified) */
    if (argc != 4) { fprintf(stderr, "usage: %s mode_pins cfg_pins(0=spi,1=sd) fuse_sel\n", argv[0]); return 1; }
    int mode = atoi(argv[1]), cfg = atoi(argv[2]), fuse_sel = atoi(argv[3]);
    const int fuse_dev = 1;                                    /* fuses say: SD */
    static const char *devs[] = { "c2_spi.img", "c2_sdcard.img" };
    static const long primary = 0x8000, secondary = 0x100000;  /* 32 KiB, 1 MiB */
    printf("BOOT_MODE=%d cfg=%d BT_FUSE_SEL=%d\n", mode, cfg, fuse_sel);
    if (mode == 1) goto serial;
    int dev = (mode == 0 || fuse_sel) ? fuse_dev : cfg;
    printf("boot device: %s\n", devs[dev]);
    if (try_copy(devs[dev], primary) == 0 || try_copy(devs[dev], secondary) == 0) return 0;
    if (dev != 1 && (try_copy(devs[1], primary) == 0)) return 0;  /* ROM's fixed fallback: SD */
serial:
    printf("  -> no valid image: enter USB serial download mode (waiting for host tool)\n");
    return 2;
}
```

미디어 이미지는 작은 Python 스크립트로 만든다: SD 카드에는 **primary(32 KiB)는 일부러 1바이트 깨진 사본**, secondary(1 MiB)는 정상 사본을, SPI 이미지에는 SRAM보다 큰(300000 B) 이미지를 넣는다.

```python
# c2_mkhdr.py - usage: c2_mkhdr.py out.img offset payload_size [corrupt]
import struct, sys, zlib
out, off, n = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3])
payload = bytes((i * 7) & 0xff for i in range(n))
hdr = struct.pack("<4sIII", b"C2HD", n, zlib.crc32(payload), 0x00900000)
if len(sys.argv) > 4: payload = b"\xff" + payload[1:]          # flash a broken copy
with open(out, "r+b") as f:
    f.seek(off); f.write(hdr + payload)
```

```sh
cd /var/tmp/C2 && gcc -std=gnu11 -Wall -Wextra -O2 $S/c2_bootrom.c -o c2_bootrom -lz
truncate -s 2M c2_sdcard.img c2_spi.img
python3 -I $S/c2_mkhdr.py c2_sdcard.img 0x8000 65536 corrupt
python3 -I $S/c2_mkhdr.py c2_sdcard.img 0x100000 65536
python3 -I $S/c2_mkhdr.py c2_spi.img 0x8000 300000
./c2_bootrom 2 0 0; ./c2_bootrom 2 1 0; ./c2_bootrom 2 0 1
mv c2_sdcard.img c2_sdcard.off; ./c2_bootrom 2 1 0; mv c2_sdcard.off c2_sdcard.img
./c2_bootrom 1 0 0
```

```text
### case A: pins say SPI (cfg=0), fuses not selected
BOOT_MODE=2 cfg=0 BT_FUSE_SEL=0
boot device: c2_spi.img
  c2_spi.img   : size 300000 > SRAM
  c2_spi.img    @0x100000: bad magic
  c2_sdcard.img @0x008000: magic ok, 65536 B, crc fdf16445 MISMATCH
  -> no valid image: enter USB serial download mode (waiting for host tool)
rc=2
### case B: pins say SD (cfg=1)
BOOT_MODE=2 cfg=1 BT_FUSE_SEL=0
boot device: c2_sdcard.img
  c2_sdcard.img @0x008000: magic ok, 65536 B, crc fdf16445 MISMATCH
  c2_sdcard.img @0x100000: magic ok, 65536 B, crc 7e711a13 OK -> jump to SRAM
rc=0
### case C: BT_FUSE_SEL=1 (production part, pins ignored)
BOOT_MODE=2 cfg=0 BT_FUSE_SEL=1
boot device: c2_sdcard.img
  c2_sdcard.img @0x008000: magic ok, 65536 B, crc fdf16445 MISMATCH
  c2_sdcard.img @0x100000: magic ok, 65536 B, crc 7e711a13 OK -> jump to SRAM
rc=0
### case D: SD removed, pins say SD
BOOT_MODE=2 cfg=1 BT_FUSE_SEL=0
boot device: c2_sdcard.img
  c2_sdcard.img @0x008000: no media
  c2_sdcard.img @0x100000: no media
  -> no valid image: enter USB serial download mode (waiting for host tool)
rc=2
### case E: BOOT_MODE=1 (serial download strap)
BOOT_MODE=1 cfg=0 BT_FUSE_SEL=0
  -> no valid image: enter USB serial download mode (waiting for host tool)
rc=2
```

출력에서 볼 것:

- case A: SPI 이미지는 **SRAM보다 커서** 거부된다. 실제로 "SPL이 OCRAM 한도를 넘었다"는 빌드는 ROM이 조용히 거부한다. 그리고 ROM의 고정 fallback(SD primary)은 깨진 사본이라 결국 복구 모드로 간다. 이 장난감 ROM은 fallback에서 secondary를 안 보는데, **실제 ROM도 "어떤 경로에서 어떤 사본을 보는지"가 SoC마다 미묘하게 다르다**. 그래서 이중화 설계(§10)는 데이터시트의 정확한 문장을 근거로 해야 한다.
- case B/C: primary가 깨져도 secondary로 부팅된다. C는 핀이 SPI를 가리켜도 퓨즈가 이긴다.
- case D/E: 카드가 없거나 모드 핀이 serial download면 바로 USB 복구 모드 — 호스트 PC에서는 이때 USB 장치가 새로 나타난다(§9).

---

## 3. 부트 이미지 헤더 — ROM이 읽는 "봉투"

### 3.1 왜 헤더가 필요한가

ROM은 미디어에서 바이트를 읽어 올 뿐, 그것이 무엇인지 모른다. 그래서 첫 단계 이미지 앞(또는 옆)에 **ROM이 정한 형식의 헤더**가 붙는다. 최소한 다음이 들어 있다.

- **magic/tag**: "여기 부트 이미지가 있다"는 표시. 없으면 다음 오프셋·다음 장치로 넘어간다.
- **크기와 로드 주소, entry point**: SRAM 어디에 얼마만큼 복사하고 어디로 점프할지.
- **(선택) 초기화 테이블**: 이미지 로드 전에 ROM이 써 줄 레지스터 목록(예: i.MX6의 DCD로 DDR을 ROM이 초기화하던 방식 — 기억 기반).
- **(선택) 서명/인증서**: secure boot에서 ROM이 검증할 서명과 공개키(또는 키 인증서).

Don의 SSD 펌웨어 이미지 헤더(시그니처, 버전, 섹션 테이블, CRC)와 같은 역할이다.

### 3.2 벤더별 헤더 (기억 기반)

| 벤더 | 이미지/헤더 이름 | 핵심 구조 | 만드는 도구 |
|---|---|---|---|
| NXP i.MX6/7/8M | **IVT**(Image Vector Table) + Boot Data (+ DCD, CSF) | IVT에 header tag 0xD1, entry, self 주소, boot data(로드 주소·크기), CSF(HAB 서명) 포인터 | U-Boot `mkimage -T imximage`/`imx8mimage`, NXP imx-mkimage, `binman` |
| NXP i.MX8/8X/93/95 | **container** (AHAB) | 컨테이너 헤더 + 이미지 배열 + 서명 블록. SECO/ELE 보안 코어가 검증 | imx-mkimage, `mkimage -T imx8image` |
| TI K3 (AM62x 등) | **tiboot3.bin** = X.509 인증서 + R5 SPL(+ 시스템 펌웨어) | ROM이 X.509 확장 필드에서 이미지 해시·로드 주소를 읽는다. GP 칩은 임시 키, HS 칩은 고객 키로 서명 | U-Boot `binman`(k3 관련 etype), TI k3-image-gen |
| Rockchip | **idbloader.img** (ID block + DDR init + miniloader/SPL) | ROM이 sector 64에서 ID block을 찾는다. 헤더는 RC4로 스크램블된 형식 | `mkimage -T rksd`/`rkspi`, `binman` |
| Allwinner | **eGON.BT0** 헤더 + SPL | 헤더의 magic "eGON.BT0" + 체크섬. ROM이 SRAM A1에 올린다 | `mkimage -T sunxi_egon` |
| Arm TF-A (랩) | **FIP**(Firmware Image Package) | ToC 헤더(0xAA640001) + UUID별 엔트리. **ROM이 아니라 BL1/BL2가** 읽는다 | `fiptool` |

랩에서 바로 해부할 수 있는 것은 TF-A의 FIP이다. ROM이 읽는 헤더는 아니지만 "magic → 엔트리 테이블 → 오프셋/크기"라는 구조가 같아서 연습용으로 딱 맞다(§4).

### 3.3 함정 — "헤더는 맞는데 오프셋이 틀렸다"

헤더 형식이 맞아도 **ROM이 찾는 위치에 없으면 없는 것과 같다**. i.MX8M Mini와 Plus는 SD에서 찾는 오프셋이 1 KiB 다르고(§7.3), eMMC boot 파티션에서는 또 다르다. 벤더 BSP의 `wic`/`genimage` 설정이나 `dd ... seek=` 값이 SoC와 맞는지 늘 확인한다.

### 3.4 서명 검증 = secure boot의 시작 (C5, L5 미리보기)

eFuse에 **공개키(또는 공개키 해시)** 를 굽고 "closed/secure" 퓨즈를 태우면, ROM은 헤더의 서명을 그 키로 검증한 이미지만 실행한다. ROM은 고칠 수 없고 퓨즈도 되돌릴 수 없으므로 이것이 **root of trust**다. 그 다음 단계(SPL, TF-A, U-Boot, 커널 FIT)가 각자 다음 이미지를 검증하며 신뢰 사슬(chain of trust)을 잇는다. 이 노트에서는 개념만 잡고, TF-A의 TBBR 인증서 체인과 FIT 서명은 C5·L5에서 다룬다. 실무 함정 하나만 기억한다: **퓨즈를 태우기 전에 반드시 "서명 이미지가 open 상태에서 검증 이벤트 없이 부팅되는지"를 확인**한다(i.MX HAB의 `hab_status` 확인 같은 절차 — 기억 기반). 잘못 태우면 그 칩은 영구 벽돌이다.

---

## 4. SPI NOR와 랩의 `flash.bin` 해부

### 4.1 SPI NOR의 성격

- **작고(수 MiB~수백 MiB), 비싸고, 믿을 만하다.** 비트 오류가 거의 없어 ECC·배드 블록 관리가 필요 없다.
- **랜덤 읽기가 쉽다**: 명령(0x03 READ, Quad는 0x6B/0xEB 등) + 주소만 보내면 바로 데이터가 나온다. 그래서 일부 SoC는 메모리 맵(XIP)으로 직접 실행할 수 있다.
- **쓰기는 지우기 단위로**: 4 KiB sector / 64 KiB block erase 후 page program(보통 256 B). 지운 상태는 0xFF.
- 부트 용도: 부트로더 + U-Boot 환경 + (작은 시스템이면) 커널까지. 큰 rootfs는 eMMC/NAND로.

QEMU `virt`의 flash는 CFI parallel NOR(pflash)지만, "주소 0부터 부트 코드가 놓이고 고정 오프셋에 다음 단계가 있다"는 레이아웃 원리는 SPI NOR와 같다.

### 4.2 예제 3 — `flash.bin` 앞부분 hexdump와 구성 확인

무엇을 확인하나: 64 MiB `flash.bin`의 0번지가 BL1 코드인지, 256 KiB(0x40000)에 FIP ToC가 있는지, 각 조각이 `bl1.bin`/`fip.bin`과 바이트 단위로 같은지 본다.

```sh
cd /var/lab/out
ls -l bl1.bin fip.bin flash.bin u-boot.bin
hexdump -C -n 64 flash.bin
hexdump -C -s 0x40000 -n 160 flash.bin
cmp -n 22832 bl1.bin flash.bin && echo bl1-match
dd if=flash.bin bs=1 skip=262144 count=1578711 status=none | cmp - fip.bin && echo fip-match
```

```text
-rwxrwxr-x 1 donh donh    22832 Oct 10 09:59 bl1.bin
-rw-rw-r-- 1 donh donh  1578711 Oct 10 09:59 fip.bin
-rw-rw-r-- 1 donh donh 67108864 Oct 10 09:59 flash.bin
-rw-rw-r-- 1 donh donh  1504208 Oct 10 09:59 u-boot.bin
00000000  00 06 81 d2 a0 18 a6 f2  00 00 6b b2 00 10 1e d5  |..........k.....|
00000010  df 3f 03 d5 f4 0b 00 94  40 00 00 b4 00 00 1f d6  |.?......@.......|
00000020  00 bf 01 10 00 c0 1e d5  df 3f 03 d5 c5 0b 00 94  |.........?......|
00000030  e7 09 00 94 02 04 40 f9  40 00 3f d6 41 01 82 d2  |......@.@.?.A...|
00000040
00040000  01 00 64 aa 78 56 34 12  00 00 00 00 00 00 00 00  |..d.xV4.........|
00040010  5f f9 ec 0b 4d 22 3e 4d  a5 44 c3 9d 81 c7 3f 0a  |_...M">M.D....?.|
00040020  b0 00 00 00 00 00 00 00  f0 61 00 00 00 00 00 00  |.........a......|
00040030  00 00 00 00 00 00 00 00  47 d4 08 6d 4c fe 98 46  |........G..mL..F|
00040040  9b 95 29 50 cb bd 5a 00  a0 62 00 00 00 00 00 00  |..)P..Z..b......|
00040050  67 c0 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |g...............|
00040060  d6 d0 ee a7 fc ea d5 4b  97 82 99 34 f2 34 b6 e4  |.......K...4.4..|
00040070  07 23 01 00 00 00 00 00  d0 f3 16 00 00 00 00 00  |.#..............|
00040080  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00040090  00 00 00 00 00 00 00 00  d7 16 18 00 00 00 00 00  |................|
000400a0
bl1-match
fip-match
```

출력에서 볼 것:

- 0번지의 `00 06 81 d2 …`는 데이터가 아니라 **AArch64 명령어**(리틀 엔디언 32비트)다. ROM(BL1)은 헤더 없이 리셋 벡터부터 바로 코드다 — 진짜 BootROM도 그렇다. 헤더가 필요한 것은 ROM이 **읽어 들이는** 다음 단계부터다.
- 0x40000: `01 00 64 aa` = 리틀 엔디언 `0xAA640001`(FIP ToC magic), `78 56 34 12` = serial `0x12345678`. 이어서 40바이트짜리 엔트리가 3개(UUID 16 B + offset 8 B + size 8 B + flags 8 B), 마지막에 UUID가 0인 끝 표시(offset 0x1816d7 = FIP 전체 크기).
- `cmp` 결과: `flash.bin` = `bl1.bin`을 0에, `fip.bin`을 256 KiB에 둔 것이다. TF-A의 QEMU 플랫폼 Makefile이 정확히 그렇게 만든다: `dd if=fip.bin of=qemu_fw.bios bs=64k seek=4`.

### 4.3 예제 4 — 어디에 데이터가 있고 어디가 비었나

무엇을 확인하나: 64 MiB 중 실제로 쓰인 영역을 4 KiB 단위로 찾는다(플래시 이미지를 다룰 때 "어디까지 써야 하나"를 판단하는 기본 동작).

```python
# c2_extents.py - print which 4 KiB regions of a flash image contain data vs. zeros
import sys
d = open(sys.argv[1], "rb").read()
blk, runs = 4096, []
for i in range(0, len(d), blk):
    nz = any(d[i:i + blk])
    if runs and runs[-1][0] == nz:
        runs[-1][2] = i + blk
    else:
        runs.append([nz, i, i + blk])
for nz, a, b in runs:
    print("%-5s 0x%08x-0x%08x %9d bytes" % ("DATA" if nz else "zero", a, b, b - a))
```

```sh
python3 -I $S/c2_extents.py /var/lab/out/flash.bin
```

```text
DATA  0x00000000-0x00006000     24576 bytes
zero  0x00006000-0x00040000    237568 bytes
DATA  0x00040000-0x001c2000   1581056 bytes
zero  0x001c2000-0x04000000   65265664 bytes
```

출력에서 볼 것: 64 MiB 중 실제 내용은 약 1.6 MiB뿐이다. 빈 곳이 0x00인 이유는 QEMU용 이미지라서다. **실제 NOR라면 지운 상태가 0xFF**이므로 공장 이미지의 패딩도 0xFF로 채워야 불필요한 program이 안 일어나고 프로그래머가 빈 sector를 건너뛸 수 있다.

### 4.4 예제 5 — C로 FIP ToC 걷기

무엇을 확인하나: hexdump로 읽은 구조를 C 구조체로 옮겨, ToC를 끝 표시까지 따라가며 각 이미지의 플래시 상 절대 위치를 계산한다. 0번지(BL1 코드)를 FIP로 착각하면 magic 검사에서 걸러지는지도 본다.

```c
/* c2_fip.c - walk a TF-A FIP Table of Contents inside a flash image */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

struct toc_hdr { uint32_t name, serial; uint64_t flags; };          /* 16 B */
struct toc_ent { uint8_t uuid[16]; uint64_t off, size, flags; };      /* 40 B */

static const struct { uint8_t b0, b1; const char *n; } known[] = {
    { 0x5f, 0xf9, "BL2  (tb-fw)" }, { 0x47, 0xd4, "BL31 (soc-fw)" },
    { 0xd6, 0xd0, "BL33 (nt-fw)" },
};

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s img fip_offset\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); exit(1); }
    long base = strtol(argv[2], NULL, 0);
    struct toc_hdr h;
    if (fseek(f, base, SEEK_SET) || fread(&h, sizeof h, 1, f) != 1) { perror("read hdr"); exit(1); }
    printf("TOC @0x%lx name=0x%08x serial=0x%08x flags=0x%llx -> %s\n", base, h.name,
           h.serial, (unsigned long long)h.flags, h.name == 0xAA640001 ? "valid FIP" : "NOT a FIP");
    if (h.name != 0xAA640001) return 2;
    for (int i = 0; ; i++) {
        struct toc_ent e;
        if (fread(&e, sizeof e, 1, f) != 1) { perror("read ent"); exit(1); }
        static const uint8_t zero[16];
        if (!memcmp(e.uuid, zero, 16)) {               /* ToC end marker */
            printf("end marker: off=0x%llx (= total FIP size %llu B)\n",
                   (unsigned long long)e.off, (unsigned long long)e.off);
            break;
        }
        const char *n = "?";
        for (size_t k = 0; k < sizeof known / sizeof known[0]; k++)
            if (e.uuid[0] == known[k].b0 && e.uuid[1] == known[k].b1) n = known[k].n;
        printf("[%d] %-14s uuid=%02x%02x%02x%02x.. off=0x%06llx size=%7llu -> flash 0x%06llx\n",
               i, n, e.uuid[0], e.uuid[1], e.uuid[2], e.uuid[3],
               (unsigned long long)e.off, (unsigned long long)e.size,
               (unsigned long long)(base + e.off));
    }
    fclose(f);
    return 0;
}
```

```sh
cd /var/tmp/C2 && gcc -std=gnu11 -Wall -Wextra -O2 $S/c2_fip.c -o c2_fip
./c2_fip /var/lab/out/flash.bin 0x40000; ./c2_fip /var/lab/out/flash.bin 0; echo "exit=$?"
```

```text
TOC @0x40000 name=0xaa640001 serial=0x12345678 flags=0x0 -> valid FIP
[0] BL2  (tb-fw)   uuid=5ff9ec0b.. off=0x0000b0 size=  25072 -> flash 0x0400b0
[1] BL31 (soc-fw)  uuid=47d4086d.. off=0x0062a0 size=  49255 -> flash 0x0462a0
[2] BL33 (nt-fw)   uuid=d6d0eea7.. off=0x012307 size=1504208 -> flash 0x052307
end marker: off=0x1816d7 (= total FIP size 1578711 B)
TOC @0x0 name=0xd2810600 serial=0xf2a618a0 flags=0xd51e1000b26b0000 -> NOT a FIP
exit=2
```

출력에서 볼 것: 엔트리 offset은 **FIP 시작 기준 상대 주소**라서 플래시 절대 주소는 0x40000을 더해야 한다. BL1은 바로 이 계산을 해서 `PLAT_QEMU_FIP_BASE + offset`에서 BL2를 읽는다. ROM이 "이미지 헤더의 offset이 어디 기준인가"(헤더 시작? 미디어 시작? 파티션 시작?)를 잘못 맞추면 엉뚱한 데를 읽는데, 이것이 이미지 생성 도구 버그의 단골 원인이다. 0번지를 넣으면 magic이 안 맞아 거부된다 — ROM이 "다음 오프셋"으로 넘어가는 순간이 이것이다.

### 4.5 예제 6 — 읽기 전용 소스에서 `fiptool`을 빌드해 `info`/`unpack`

무엇을 확인하나: 공식 도구로도 같은 결과가 나오는지, 그리고 BL33 조각이 정말 `u-boot.bin`인지 본다. 랩 소스는 읽기 전용이므로 복사해서 빌드한다(TF-A 최상위에서 `make fiptool`; `tools/fiptool`에서 바로 `make`하면 빌드 디렉터리 변수가 비어 `/tools/fiptool/` mkdir에 실패한다).

```sh
cd /var/tmp/C2 && cp -a /var/lab/src/tfa c2_tfa && cd c2_tfa && make fiptool 2>&1 | tail -4
F=/var/tmp/C2/c2_tfa/build/fvp/release/tools/fiptool/fiptool
$F info /var/lab/out/fip.bin
mkdir -p /var/tmp/C2/c2_unpack && cd /var/tmp/C2/c2_unpack && $F unpack --force /var/lab/out/fip.bin
ls -l && cmp nt-fw.bin /var/lab/out/u-boot.bin && echo "nt-fw == u-boot.bin"
```

```text
  HOSTCC      fiptool.c
  HOSTCC      tbbr_config.c
  HOSTLD  /var/tmp/C2/c2_tfa/build/fvp/release/tools/fiptool/fiptool

Built /var/tmp/C2/c2_tfa/build/fvp/release/tools/fiptool/fiptool successfully
Trusted Boot Firmware BL2: offset=0xB0, size=0x61F0, cmdline="--tb-fw"
EL3 Runtime Firmware BL31: offset=0x62A0, size=0xC067, cmdline="--soc-fw"
Non-Trusted Firmware BL33: offset=0x12307, size=0x16F3D0, cmdline="--nt-fw"
total 1552
-rw-rw-r-- 1 donh donh 1504208 Oct 10 10:22 nt-fw.bin
-rw-rw-r-- 1 donh donh   49255 Oct 10 10:22 soc-fw.bin
-rw-rw-r-- 1 donh donh   25072 Oct 10 10:22 tb-fw.bin
nt-fw == u-boot.bin
```

출력에서 볼 것: `fiptool info`의 offset/size가 우리 C 파서(0xb0/25072, 0x62a0/49255, 0x12307/1504208)와 정확히 같다. `cmdline="--nt-fw"`는 `fiptool create --nt-fw u-boot.bin fip.bin`처럼 **다시 묶을 때 쓰는 옵션 이름**이다. 실무에서 "U-Boot만 고쳐서 FIP를 다시 만들기"가 `fiptool update --nt-fw new-u-boot.bin fip.bin` 한 줄이다(빌드 플랫폼이 `fvp`로 찍힌 것은 PLAT를 안 줬기 때문이고, 호스트 도구라서 무관하다).

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="240" y="14" text-anchor="middle" font-size="13" font-weight="bold">flash.bin (64 MiB, 축척 무시)</text>
  <text x="120" y="38" text-anchor="end" font-size="12">0x0000_0000</text>
  <rect x="130" y="24" width="220" height="48" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="240" y="44" text-anchor="middle" font-size="12">BL1 (bl1.bin)</text>
  <text x="240" y="62" text-anchor="middle" font-size="12">22832 B, AArch64 코드</text>
  <text x="120" y="86" text-anchor="end" font-size="12">0x0000_5930</text>
  <rect x="130" y="72" width="220" height="30" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="240" y="92" text-anchor="middle" font-size="12">빈 영역 (0x00)</text>
  <text x="120" y="116" text-anchor="end" font-size="12">0x0004_0000</text>
  <rect x="130" y="102" width="220" height="26" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="240" y="120" text-anchor="middle" font-size="12">FIP ToC (0xb0 B)</text>
  <text x="120" y="142" text-anchor="end" font-size="12">0x0004_00b0</text>
  <rect x="130" y="128" width="220" height="32" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="240" y="149" text-anchor="middle" font-size="12">BL2 tb-fw 25072 B</text>
  <text x="120" y="174" text-anchor="end" font-size="12">0x0004_62a0</text>
  <rect x="130" y="160" width="220" height="32" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="240" y="181" text-anchor="middle" font-size="12">BL31 soc-fw 49255 B</text>
  <text x="120" y="206" text-anchor="end" font-size="12">0x0005_2307</text>
  <rect x="130" y="192" width="220" height="64" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="240" y="219" text-anchor="middle" font-size="12">BL33 nt-fw = u-boot.bin</text>
  <text x="240" y="237" text-anchor="middle" font-size="12">1504208 B</text>
  <text x="120" y="270" text-anchor="end" font-size="12">0x001c_16d7</text>
  <rect x="130" y="256" width="220" height="56" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="240" y="288" text-anchor="middle" font-size="12">빈 영역 (약 62 MiB)</text>
  <text x="120" y="326" text-anchor="end" font-size="12">0x0400_0000</text>
  <line x1="350" y1="48" x2="378" y2="48" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="384" y="44" font-size="12">주소 0에 ROM처럼 매핑,</text>
  <text x="384" y="60" font-size="12">그 자리에서 실행 (EL3)</text>
  <line x1="350" y1="144" x2="378" y2="144" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="384" y="140" font-size="12">BL1이 secure SRAM</text>
  <text x="384" y="156" font-size="12">0x0e000000 영역으로 복사</text>
  <line x1="350" y1="176" x2="378" y2="182" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="384" y="186" font-size="12">BL2가 SRAM에 로드 (EL3 상주)</text>
  <line x1="350" y1="224" x2="378" y2="224" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="384" y="220" font-size="12">BL2가 DRAM 0x60000000에 로드</text>
  <text x="384" y="236" font-size="12">(NS_IMAGE_OFFSET)</text>
  <text x="340" y="350" text-anchor="middle" font-size="12">FIP 오프셋은 FIP 시작(0x40000) 기준 → 절대 주소 = 0x40000 + offset</text>
</svg>
```
그림 4 — 랩 `flash.bin`의 레이아웃(실측 오프셋, 높이는 축척 무시). 실제 SPI NOR 부트 레이아웃도 "0 또는 ROM이 정한 오프셋에 첫 단계 + 고정 오프셋에 다음 단계 패키지 + 환경 영역"이라는 같은 모양이다.

### 4.6 SPI NOR 함정

- **0xFF 대 0x00 패딩**: 위 §4.3.
- **4-byte 주소 모드**: 16 MiB 이상 NOR는 4-byte 주소가 필요하다. ROM이 3-byte로 읽는데 이전 부팅에서 Linux가 4-byte 모드로 바꾼 채 **소프트 리셋**하면, ROM이 엉뚱한 데를 읽어 부팅 실패 — NOR의 리셋 핀을 SoC 리셋에 연결하지 않은 보드에서 나는 고전 버그다.
- **Quad 모드(QE 비트)**: ROM이 Quad read를 쓰는 SoC에서는 NOR 상태 레지스터의 QE 비트가 설정돼 있어야 한다. 반대로 QE가 켜지면 WP#/HOLD# 핀 기능이 사라진다.

---

## 5. raw NAND — 배드 블록과 ECC (Don의 홈 그라운드)

### 5.1 SSD와 무엇이 같고 무엇이 다른가

Don이 SSD에서 다룬 NAND와 원리는 같다: page 단위 program, block 단위 erase, page마다 붙은 **OOB(spare) 영역**, **공장 배드 블록 표시(BBM)**, 시간이 지나며 생기는 비트 오류를 **ECC**로 고치는 것. 차이는 **누가 그것을 관리하느냐**다.

| 항목 | SSD (Don의 경험) | Embedded Linux의 raw NAND |
|---|---|---|
| 배드 블록 관리 | FTL 펌웨어가 전부 숨긴다 | **커널 MTD + UBI/UBIFS**가 직접 한다. 부트로더도 스스로 건너뛴다 |
| ECC | 컨트롤러 HW LDPC/BCH | SoC NAND 컨트롤러의 BCH 엔진 또는 소프트웨어 Hamming/BCH |
| 부트 이미지 | 컨트롤러 ROM이 정한 블록 | **BootROM이 정한 블록·ECC 형식**으로 써야 함 |
| wear leveling | FTL | UBI |

BootROM 관점의 핵심 규칙(기억 기반, 벤더 공통 경향):

- NAND 제조사는 **block 0이 출하 시 좋다**고 보장한다(SLC 기준, 일정 erase 횟수까지). 그래서 ROM은 첫 단계를 block 0 근처에서 찾는다.
- ROM은 **자기만의 고정 ECC 설정**으로 읽는다. 그래서 첫 단계 이미지는 Linux의 ECC 레이아웃이 아니라 **ROM의 ECC 레이아웃**으로 써야 한다(예: i.MX의 `kobs-ng`/`imx-kobs`가 FCB·DBBT와 함께 쓰던 이유 — 기억 기반).
- ROM은 사본을 여러 개 찾는다: 예를 들어 TI AM335x ROM은 처음 4개 블록을 순서대로 시도하고, i.MX는 FCB를 일정 간격(search stride)으로 여러 번 찾는다(기억 기반).

### 5.2 예제 7 — nandsim으로 배드 블록 건너뛰기와 ECC 정정을 보기

무엇을 확인하나: 랩 커널에는 NAND 시뮬레이터가 빠져 있어서, 커널 트리의 `drivers/mtd/nand/raw/nandsim.c`를 **out-of-tree 모듈로 빌드**해 QEMU 게스트에 넣었다. 2 KiB page / 64 B OOB / 128 KiB block인 128 MiB SLC(Samsung ID `0xec 0xf1`)를 흉내 내고, **block 5를 공장 배드 블록**으로, `bitflips=1`로 읽을 때 무작위 1비트 오류를 넣었다. 파티션은 block 0~3(SPL 자리), 4~19(U-Boot 자리), 나머지.

```sh
mkdir -p /var/tmp/C2/c2_nandsim && cd /var/tmp/C2/c2_nandsim
cp /var/lab/src/linux-6.18.55/drivers/mtd/nand/raw/nandsim.c c2_nandsim.c
echo "obj-m += c2_nandsim.o" > Kbuild
make -C /var/lab/out/linux-build M=$PWD modules      # 경고 0개, c2_nandsim.ko 생성
# 랩 rootfs를 복사해 .ko와 게스트 스크립트를 넣고 initramfs로 묶는다
cd /var/tmp/C2 && mkdir c2_rn && cp -a /var/lab/build/rootfs/. c2_rn/
cp c2_nandsim/c2_nandsim.ko $S/c2_nand_guest.sh c2_rn/
(cd c2_rn && find . | cpio -o -H newc --quiet | gzip) > c2_initrd_nand.gz
timeout 250 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd c2_initrd_nand.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/c2_nand_guest.sh" > c2_nand.log 2>&1
```

게스트 안에서 도는 스크립트(BusyBox의 `nandwrite`/`nanddump` 사용):

```sh
#!/bin/sh
# runs inside the QEMU guest: simulated raw NAND (nandsim) with a factory bad block
insmod /c2_nandsim.ko id_bytes=0xec,0xf1,0x00,0x95 badblocks=5 parts=4,16 bitflips=1
dmesg | grep -iE "nand|bad|ecc" | sed 's/^\[[ 0-9.]*\] //' | head -12
echo "== /proc/mtd"; cat /proc/mtd
M=/sys/class/mtd/mtd1
for a in name type size erasesize writesize oobsize ecc_strength ecc_step_size bad_blocks; do
  printf "%s=%s  " $a "$(cat $M/$a)"; done; echo
echo "== mtd2 bad_blocks=$(cat /sys/class/mtd/mtd2/bad_blocks)  (partition = blocks 4..19)"
dd if=/dev/urandom of=/tmp/c2_uboot.bin bs=1k count=300 2>/dev/null
echo "== nandwrite 300 KiB into mtd2 (block 5 of the chip = block 1 of mtd2 is bad)"
nandwrite -p /dev/mtd2 /tmp/c2_uboot.bin
echo "== nanddump page 0 of mtd2 with OOB -> show the 64 B OOB that follows the 2048 B page"
nanddump -o -l 2048 -f /tmp/p0 /dev/mtd2 2>&1 | tail -1
hexdump -C -s 2048 -n 64 /tmp/p0
echo "== read back with skipbad and compare"
nanddump --bb=skipbad -l 307200 -f /tmp/rb /dev/mtd2 2>&1 | tail -1
head -c 307200 /tmp/rb | cmp - /tmp/c2_uboot.bin && echo "readback == written (bad block skipped both ways)"
echo "== 20 full reads (2 MiB each) of mtd2 with random bit flips injected (nandsim bitflips=1)"
for i in $(seq 1 20); do nanddump --bb=skipbad -f /tmp/rb /dev/mtd2 2>/dev/null
  head -c 307200 /tmp/rb | cmp -s - /tmp/c2_uboot.bin || echo "read $i MISMATCH"; done
echo "corrected_bits=$(cat /sys/class/mtd/mtd2/corrected_bits) ecc_failures=$(cat /sys/class/mtd/mtd2/ecc_failures)"
dmesg | grep -c "flipping bit" | sed 's/^/kernel log lines "flipping bit": /'
dmesg | grep "flipping bit" | head -2 | sed 's/^\[[ 0-9.]*\] //'
```

```text
nand: device found, Manufacturer ID: 0xec, Chip ID: 0xf1
nand: Samsung NAND 128MiB 3,3V 8-bit
nand: 128 MiB, SLC, erase size: 128 KiB, page size: 2048, OOB size: 64
== /proc/mtd
dev:    size   erasesize  name
mtd0: 08000000 00040000 "0.flash"
mtd1: 00080000 00020000 "NAND simulator partition 0"
mtd2: 00200000 00020000 "NAND simulator partition 1"
mtd3: 07d80000 00020000 "NAND simulator partition 2"
name=NAND simulator partition 0  type=nand  size=524288  erasesize=131072  writesize=2048  oobsize=64  ecc_strength=1  ecc_step_size=256  bad_blocks=0  
== mtd2 bad_blocks=1  (partition = blocks 4..19)
== nandwrite 300 KiB into mtd2 (block 5 of the chip = block 1 of mtd2 is bad)
Writing at 0x00000000
Skipping bad block at 0x00020000
Writing at 0x00040000
Writing at 0x00060000
== nanddump page 0 of mtd2 with OOB -> show the 64 B OOB that follows the 2048 B page
00000800  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
*
00000820  ff ff ff ff ff ff ff ff  99 65 ab 56 5a a7 33 ff  |.........e.VZ.3.|
00000830  03 59 95 6b 00 33 0f ff  c3 cf 30 00 ff f0 f0 0f  |.Y.k.3....0.....|
00000840
== read back with skipbad and compare
readback == written (bad block skipped both ways)
== 20 full reads (2 MiB each) of mtd2 with random bit flips injected (nandsim bitflips=1)
corrected_bits=1 ecc_failures=0
kernel log lines "flipping bit": 1
[nandsim] warning: read_page: flipping bit 1838 in page 434 reading from 0 ecc: corrected=0 failed=0
```

출력에서 볼 것 (OOB 바이트와 bitflip 횟수는 무작위라 실행마다 다르다):

- `mtd0 "0.flash"`는 QEMU의 CFI NOR(그림 4의 그 flash)이고, nandsim이 mtd1~3을 만들었다. **커널은 NOR도 NAND도 같은 MTD 인터페이스**(`/dev/mtdN`, `/sys/class/mtd/`)로 보여 준다.
- `ecc_strength=1 ecc_step_size=256`: 소프트웨어 Hamming ECC — 256 B마다 1비트 정정. 2 KiB page면 8 step × 3 B = 24 B ECC가 OOB **40~63번째 바이트**에 들어간 것이 hexdump(0x828부터)에 보인다. OOB 0~1번째 바이트는 BBM 자리(0xFF = 좋은 블록). 양산 SoC는 보통 HW BCH로 512 B/1 KiB당 8~40비트를 정정한다.
- `Skipping bad block at 0x00020000`: mtd2의 두 번째 블록(칩 block 5)이 배드라서 nandwrite가 건너뛰고 다음 블록에 이어 썼다. **읽을 때도 똑같이 건너뛰어야**(`--bb=skipbad`) 원래 파일이 나온다. 부트로더(U-Boot `nand read`)와 ROM도 이 "skip bad block" 규칙을 같은 방식으로 따라야 하므로, **배드 블록 처리 방식이 쓰는 쪽과 읽는 쪽에서 다르면 부팅 실패**가 난다.
- `corrected_bits=1 ecc_failures=0`, 데이터 불일치 없음: 주입된 1비트 오류를 Hamming이 고쳤다. `nandsim` 로그의 `corrected=0`은 "뒤집기 직전" 카운터 값이다. 2비트 이상이면 Hamming으로는 못 고쳐 `ecc_failures`가 오른다 — UBI는 이런 "정정은 됐지만 비트 오류가 생긴" 블록을 **scrubbing**(다른 블록으로 옮겨 쓰기)한다(J2).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="20" font-size="13" font-weight="bold">한 page = 2048 B 데이터 + 64 B OOB</text>
  <rect x="20" y="30" width="480" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="260" y="55" text-anchor="middle" font-size="12">데이터 2048 B = ECC step 8개 (256 B씩)</text>
  <rect x="500" y="30" width="20" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <rect x="520" y="30" width="70" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="590" y="30" width="70" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="88" text-anchor="middle" font-size="12">BBM</text>
  <text x="510" y="102" text-anchor="middle" font-size="12">0-1</text>
  <text x="555" y="88" text-anchor="middle" font-size="12">free</text>
  <text x="555" y="102" text-anchor="middle" font-size="12">2-39</text>
  <text x="625" y="88" text-anchor="middle" font-size="12">ECC 24 B</text>
  <text x="625" y="102" text-anchor="middle" font-size="12">40-63</text>
  <text x="20" y="140" font-size="13" font-weight="bold">nandwrite 300 KiB → mtd2 (칩 block 4부터)</text>
  <rect x="20" y="152" width="70" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="55" y="179" text-anchor="middle" font-size="12">blk 4</text>
  <rect x="100" y="152" width="70" height="44" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/>
  <text x="135" y="172" text-anchor="middle" font-size="12">blk 5</text>
  <text x="135" y="188" text-anchor="middle" font-size="12">BAD</text>
  <rect x="180" y="152" width="70" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="215" y="179" text-anchor="middle" font-size="12">blk 6</text>
  <rect x="260" y="152" width="70" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="295" y="179" text-anchor="middle" font-size="12">blk 7</text>
  <rect x="340" y="152" width="70" height="44" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="375" y="179" text-anchor="middle" font-size="12">blk 8 …</text>
  <text x="55" y="226" text-anchor="middle" font-size="12">0~128K</text>
  <text x="215" y="226" text-anchor="middle" font-size="12">128~256K</text>
  <text x="295" y="226" text-anchor="middle" font-size="12">256~300K</text>
  <path d="M 90 210 Q 135 250 180 210" fill="none" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="135" y="262" text-anchor="middle" font-size="12">건너뜀</text>
  <text x="430" y="166" font-size="12">쓰는 쪽(nandwrite, 공장 툴)과</text>
  <text x="430" y="184" font-size="12">읽는 쪽(ROM, U-Boot, 커널)이</text>
  <text x="430" y="202" font-size="12">같은 skip 규칙을 써야 한다</text>
  <text x="340" y="290" text-anchor="middle" font-size="12">OOB 위치는 실측 hexdump 기준 (커널 SW Hamming, large page 레이아웃)</text>
</svg>
```
그림 5 — 위: 2 KiB page의 OOB 배치(이 실험의 SW Hamming 레이아웃). 아래: 칩 block 5가 배드일 때 nandwrite가 데이터를 block 4, 6, 7에 나눠 쓰는 모습. ROM이 첫 단계를 이렇게 "건너뛰며" 읽을 수 있는지는 SoC마다 다르므로, 대개 첫 단계는 block 0~N의 **독립된 사본 여러 개**로 쓴다.

### 5.3 SPI NAND

최근 보드는 parallel raw NAND 대신 **SPI NAND**(SPI 버스 + NAND 셀, 칩 내부 ECC 내장이 흔함)를 많이 쓴다. 커널에서는 `drivers/mtd/nand/spi/`의 SPI-NAND 프레임워크로 붙고, 배드 블록·UBI 이야기는 raw NAND와 같다. ROM 지원 여부는 SoC마다 다르다(Rockchip, Allwinner 일부, i.MX8M 일부가 지원 — 기억 기반).

---

## 6. eMMC — 칩 안의 하드웨어 부트 파티션

### 6.1 eMMC는 "작은 SSD"다

eMMC는 NAND + 컨트롤러(FTL, ECC, 배드 블록 관리)를 한 패키지에 넣고 MMC 버스로 노출한 것이다. Don이 만든 SSD의 축소판이다. 호스트(SoC)는 배드 블록이나 ECC를 몰라도 된다. 대신 JEDEC eMMC 표준(JESD84)이 정의한 **하드웨어 파티션**이 있다.

| 영역 | 크기 | Linux 장치 | 용도 |
|---|---|---|---|
| **boot0, boot1** | 각 `BOOT_SIZE_MULT` x 128 KiB (보통 4 MiB) | `/dev/mmcblk0boot0`, `boot1` | 부트로더. ROM이 특별한 "boot operation"으로 읽을 수 있다 |
| **RPMB** | `RPMB_SIZE_MULT` x 128 KiB | `/dev/mmcblk0rpmb` (문자 장치) | 인증된 쓰기만 받는 영역. OP-TEE가 secure storage로 씀(L5) |
| **GP1~GP4** (선택) | 한 번만 설정 가능 | `mmcblk0gp0` 등 | 거의 안 씀 |
| **user area** | 나머지 | `/dev/mmcblk0` → `p1`, `p2` … | 여기에 MBR/GPT와 파티션(부트 FAT, rootfs A/B, data) |

**EXT_CSD**는 eMMC의 512바이트 설정·상태 레지스터다. 부팅과 관련된 핵심 바이트:

| EXT_CSD 바이트 | 이름 | 뜻 |
|---|---|---|
| [179] | PARTITION_CONFIG | bit6 BOOT_ACK, bit5:3 BOOT_PARTITION_ENABLE(0 없음, 1 boot0, 2 boot1, 7 user), bit2:0 PARTITION_ACCESS(지금 명령이 접근할 영역: 0 user, 1 boot0, 2 boot1, 3 RPMB) |
| [177] | BOOT_BUS_CONDITIONS | boot operation 때 버스 폭·속도 |
| [226] | BOOT_SIZE_MULT | boot 파티션 크기 / 128 KiB |
| [168] | RPMB_SIZE_MULT | RPMB 크기 / 128 KiB |
| [162] | RST_n_FUNCTION | RST_n 핀 활성화(한 번 쓰면 영구) |

ROM이 eMMC에서 부팅할 때는 두 가지 길이 있다(기억 기반 일반론): (1) eMMC "boot operation" — CMD 라인을 잡고 있으면(또는 CMD0 특수 인자) 카드가 **BOOT_PARTITION_ENABLE이 가리키는 파티션**의 데이터를 그냥 흘려 준다, (2) 일반 초기화 후 PARTITION_ACCESS를 바꿔 boot0/boot1을 블록 단위로 읽는다. 어느 쪽이든 **"어느 boot 파티션에서 부팅할지"는 EXT_CSD[179]가 정한다** — 그래서 A/B 부트로더 전환이 이 1바이트 쓰기로 끝난다(§10).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="18" text-anchor="middle" font-size="13" font-weight="bold">eMMC 하나 = 하드웨어 파티션 여러 개 (축척 무시)</text>
  <rect x="20" y="30" width="110" height="52" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="75" y="52" text-anchor="middle" font-size="13">boot0</text>
  <text x="75" y="70" text-anchor="middle" font-size="12">4 MiB</text>
  <rect x="130" y="30" width="110" height="52" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="185" y="52" text-anchor="middle" font-size="13">boot1</text>
  <text x="185" y="70" text-anchor="middle" font-size="12">4 MiB</text>
  <rect x="240" y="30" width="90" height="52" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="285" y="52" text-anchor="middle" font-size="13">RPMB</text>
  <text x="285" y="70" text-anchor="middle" font-size="12">128 KiB</text>
  <rect x="330" y="30" width="80" height="52" fill="#888" fill-opacity="0.04" stroke="#888" stroke-dasharray="4 3"/>
  <text x="370" y="52" text-anchor="middle" font-size="12">GP1~4</text>
  <text x="370" y="70" text-anchor="middle" font-size="12">(선택)</text>
  <rect x="410" y="30" width="250" height="52" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="535" y="52" text-anchor="middle" font-size="13">user area 64 MiB</text>
  <text x="535" y="70" text-anchor="middle" font-size="12">MBR/GPT + p1, p2 …</text>
  <text x="75" y="100" text-anchor="middle" font-size="12">mmcblk0boot0</text>
  <text x="185" y="100" text-anchor="middle" font-size="12">mmcblk0boot1</text>
  <text x="285" y="100" text-anchor="middle" font-size="12">mmcblk0rpmb</text>
  <text x="285" y="114" text-anchor="middle" font-size="12">(char dev)</text>
  <text x="535" y="100" text-anchor="middle" font-size="12">mmcblk0, mmcblk0p1 …</text>
  <text x="20" y="140" font-size="12">QEMU 백엔드 파일 오프셋(실측): boot0 = 0, boot1 = 4 MiB, RPMB = 8 MiB, user = 8 MiB + 128 KiB</text>
  <text x="20" y="180" font-size="13" font-weight="bold">EXT_CSD[179] PARTITION_CONFIG</text>
  <rect x="100" y="192" width="60" height="30" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <rect x="160" y="192" width="60" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="220" y="192" width="180" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <rect x="400" y="192" width="180" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="212" text-anchor="middle" font-size="12">bit7</text>
  <text x="190" y="212" text-anchor="middle" font-size="12">bit6</text>
  <text x="310" y="212" text-anchor="middle" font-size="12">bit5:3</text>
  <text x="490" y="212" text-anchor="middle" font-size="12">bit2:0</text>
  <text x="130" y="240" text-anchor="middle" font-size="12">예약</text>
  <text x="190" y="240" text-anchor="middle" font-size="12">BOOT_ACK</text>
  <text x="310" y="240" text-anchor="middle" font-size="12">BOOT_PARTITION_ENABLE</text>
  <text x="490" y="240" text-anchor="middle" font-size="12">PARTITION_ACCESS</text>
  <text x="20" y="272" font-size="12">0x48</text>
  <text x="130" y="272" text-anchor="middle" font-size="12">0</text>
  <text x="190" y="272" text-anchor="middle" font-size="12">1</text>
  <text x="310" y="272" text-anchor="middle" font-size="12">001 = boot0에서 부팅</text>
  <text x="490" y="272" text-anchor="middle" font-size="12">000 = user 접근</text>
  <text x="20" y="300" font-size="12">0x50</text>
  <text x="130" y="300" text-anchor="middle" font-size="12">0</text>
  <text x="190" y="300" text-anchor="middle" font-size="12">1</text>
  <text x="310" y="300" text-anchor="middle" font-size="12">010 = boot1에서 부팅</text>
  <text x="490" y="300" text-anchor="middle" font-size="12">000 = user 접근</text>
</svg>
```
그림 6 — eMMC 하드웨어 파티션과 EXT_CSD[179]. 아래 예제에서 0x48(boot0 + ACK)을 0x50(boot1 + ACK)으로 바꾼다. PARTITION_ACCESS는 "지금 어느 영역을 읽고 쓰나"(커널이 boot0에 접근할 때마다 바꾼다), BOOT_PARTITION_ENABLE은 "전원 ON 때 ROM에 무엇을 줄까"다 — 둘을 헷갈리지 않는 것이 핵심이다.

### 6.2 예제 8 — QEMU에서 진짜 eMMC를 만들어 Linux에 붙이기

QEMU 10에는 `emmc` 장치(`boot-partition-size`, `rpmb-partition-size`, `boot-config` 속성)가 있다. `virt` 머신에는 SD 호스트가 없으므로 **PCI SDHCI**(`-device sdhci-pci`)에 꽂는다. 그런데 랩 커널은 `CONFIG_MMC_SDHCI_PCI`가 꺼져 있다 — 그래서 커널 트리의 `sdhci-pci-*.c`를 복사해 out-of-tree 모듈로 빌드했다(G·H 모듈에서 할 "드라이버를 모듈로 빌드해 붙이기"의 맛보기).

```sh
S_MMC=/var/lab/src/linux-6.18.55/drivers/mmc/host
mkdir -p /var/tmp/C2/c2_sdhci && cd /var/tmp/C2/c2_sdhci
cp $S_MMC/sdhci-pci*.c $S_MMC/sdhci-pci*.h $S_MMC/sdhci.h $S_MMC/cqhci.h \
   $S_MMC/sdhci-cqhci.h $S_MMC/sdhci-uhs2.h $S_MMC/sdhci-uhs2.c .
printf "obj-m += c2_sdhci_uhs2.o c2_sdhci_pci.o\nc2_sdhci_uhs2-y := sdhci-uhs2.o\nc2_sdhci_pci-y := sdhci-pci-core.o sdhci-pci-o2micro.o sdhci-pci-arasan.o sdhci-pci-dwc-mshc.o sdhci-pci-gli.o\n" > Kbuild
make -C /var/lab/out/linux-build M=$PWD modules 2>&1 | grep -E "warning|ERROR"; ls -l *.ko
```

```text
-rw-rw-r-- 1 donh donh 571864 Oct 10 10:25 c2_sdhci_pci.ko
-rw-rw-r-- 1 donh donh 177528 Oct 10 10:24 c2_sdhci_uhs2.ko
```

빌드 중 만난 두 에러가 그 자체로 공부거리다: (1) `sdhci-pci-gli.c`가 UHS-II 함수를 써서 `modpost: "sdhci_uhs2_add_host" undefined` → `sdhci-uhs2.c`를 같이 넣었고, (2) 둘을 한 모듈에 넣자 `multiple definition of init_module`(파일마다 `module_init`이 있음) → **두 모듈로 분리**했다. 커널 Makefile이 `sdhci-uhs2.o`를 별도 모듈로 만드는 이유가 이것이다.

먼저 QEMU가 하드웨어 파티션을 백엔드 파일 어디에 두는지 확인했다. user area는 2의 거듭제곱이어야 하므로 파일 크기 = 64 MiB + boot 4 MiB x 2 + RPMB 128 KiB이고, 파일의 각 후보 위치에 표식을 써 두었다.

```sh
cd /var/tmp/C2 && truncate -s $((64*1024*1024+8*1024*1024+128*1024)) c2_emmc.img
printf "C2-AT-FILE-0MiB" | dd of=c2_emmc.img conv=notrunc status=none
printf "C2-AT-FILE-4MiB" | dd of=c2_emmc.img bs=1M seek=4 conv=notrunc status=none
printf "C2-AT-FILE-8MiB" | dd of=c2_emmc.img bs=1M seek=8 conv=notrunc status=none
printf "C2-AT-FILE-8MiB+128K" | dd of=c2_emmc.img bs=1K seek=$((8*1024+128)) conv=notrunc status=none
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd c2_initrd.gz -device sdhci-pci \
  -drive if=none,id=e0,file=c2_emmc.img,format=raw \
  -device emmc,drive=e0,boot-partition-size=4194304,rpmb-partition-size=131072,boot-config=0x48 \
  -append "console=ttyAMA0 quiet LAB_CMD=insmod~/c2_sdhci_uhs2.ko~&&~insmod~/c2_sdhci_pci.ko~&&~sleep~2~&&~dmesg~|~grep~-i~mmc;~ls~-l~/dev/mmc*;~cat~/proc/partitions;~for~d~in~mmcblk0~mmcblk0boot0~mmcblk0boot1;~do~echo~\$d:;~head~-c~24~/dev/\$d;~echo;~done"
```

```text
[    2.445505] mmc0: SDHCI controller on PCI [0000:00:02.0] using ADMA
[    2.509082] mmc0: new high speed MMC card at address 0001
[    2.512659] mmcblk0: mmc0:0001 QEMU!! 64.0 MiB
[    2.537193] mmcblk0boot0: mmc0:0001 QEMU!! 4.00 MiB
[    2.554135] mmcblk0boot1: mmc0:0001 QEMU!! 4.00 MiB
[    2.566106] mmcblk0rpmb: mmc0:0001 QEMU!! 128 KiB, chardev (511:0)
brw-------    1 0        0         179,   0 Oct 10 17:25 /dev/mmcblk0
brw-------    1 0        0         179,  32 Oct 10 17:25 /dev/mmcblk0boot0
brw-------    1 0        0         179,  64 Oct 10 17:25 /dev/mmcblk0boot1
crw-------    1 0        0         511,   0 Oct 10 17:25 /dev/mmcblk0rpmb
major minor  #blocks  name

  31        0     131072 mtdblock0
 179        0      65536 mmcblk0
 179       32       4096 mmcblk0boot0
 179       64       4096 mmcblk0boot1
mmcblk0:
C2-AT-FILE-8MiB+128K    
mmcblk0boot0:
C2-AT-FILE-0MiB         
mmcblk0boot1:
C2-AT-FILE-4MiB         
```

(`ls`의 색 escape 코드는 지웠다. 시각은 게스트 시계라 실행마다 다르다.)

출력에서 볼 것: 커널이 eMMC를 인식하자마자 **블록 장치 3개 + RPMB 문자 장치 1개**를 만든다. boot 파티션은 `179:32`, `179:64`처럼 별도 minor를 받는다(`CONFIG_MMC_BLOCK_MINORS=32` 간격). RPMB가 블록 장치가 아니라 **문자 장치**인 것은, 일반 read/write가 아니라 인증 프레임(HMAC)을 주고받는 프로토콜이기 때문이다. QEMU 백엔드 파일은 boot0 → boot1 → RPMB → user 순서라는 것도 표식으로 확인됐다(이것은 QEMU 구현의 사정이지 실제 eMMC 내부 배치와는 무관하다).

### 6.3 예제 9 — EXT_CSD 읽기, boot0 쓰기 보호, `bootpart enable`을 ioctl로

무엇을 확인하나: (1) 커널이 boot 파티션을 기본 **읽기 전용(force_ro)** 으로 여는지, (2) EXT_CSD를 debugfs와 `MMC_IOC_CMD` ioctl 두 방법으로 읽는지, (3) mmc-utils의 `mmc bootpart enable`이 내부적으로 하는 **CMD6 SWITCH**를 직접 보내 부트 파티션을 boot0 → boot1로 바꾼다.

`MMC_IOC_CMD`는 유저 공간에서 MMC 명령을 그대로 보내는 ioctl이다. 응답 타입 플래그(R1, R1b)는 UAPI 헤더에 없어서 mmc-utils처럼 커널 `<linux/mmc/core.h>`와 같은 비트 값을 직접 정의한다.

```c
/* c2_mmcboot.c - read EXT_CSD and do "mmc bootpart enable" with MMC_IOC_CMD (like mmc-utils) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/mmc/ioctl.h>

#define RSP_PRESENT (1u << 0)                 /* same bit values as kernel <linux/mmc/core.h> */
#define RSP_CRC     (1u << 2)
#define RSP_BUSY    (1u << 3)
#define RSP_OPCODE  (1u << 4)
#define CMD_AC      (0u << 5)
#define CMD_ADTC    (1u << 5)
#define R1  (RSP_PRESENT | RSP_CRC | RSP_OPCODE)
#define R1B (R1 | RSP_BUSY)

static void read_ext_csd(int fd, uint8_t *ext)
{
    struct mmc_ioc_cmd c = { .opcode = 8, .flags = R1 | CMD_ADTC, .blksz = 512, .blocks = 1 };
    mmc_ioc_cmd_set_data(c, ext);                       /* CMD8 SEND_EXT_CSD */
    if (ioctl(fd, MMC_IOC_CMD, &c) < 0) { perror("CMD8"); exit(1); }
}

static void show(const uint8_t *e)
{
    uint8_t pc = e[179];
    static const char *bp[8] = { "none", "boot0", "boot1", "?", "?", "?", "?", "user" };
    printf("EXT_CSD_REV[192]=%u  BOOT_SIZE_MULT[226]=%u (%u KiB each)  RPMB_SIZE_MULT[168]=%u (%u KiB)\n",
           e[192], e[226], e[226] * 128, e[168], e[168] * 128);
    printf("PARTITION_CONFIG[179]=0x%02x -> BOOT_ACK=%u BOOT_PARTITION_ENABLE=%u(%s) PARTITION_ACCESS=%u\n",
           pc, (pc >> 6) & 1, (pc >> 3) & 7, bp[(pc >> 3) & 7], pc & 7);
    printf("BOOT_BUS_CONDITIONS[177]=0x%02x  SEC_COUNT[212..215]=%u sectors\n", e[177],
           e[212] | e[213] << 8 | e[214] << 16 | (uint32_t)e[215] << 24);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s /dev/mmcblkN [enable PART ACK]\n", argv[0]); return 1; }
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open"); exit(1); }
    uint8_t ext[512] __attribute__((aligned(512)));
    read_ext_csd(fd, ext);
    show(ext);
    if (argc == 5) {                                    /* enable <1|2|7> <0|1> */
        uint8_t v = (uint8_t)((atoi(argv[4]) << 6) | (atoi(argv[3]) << 3) | (ext[179] & 7));
        struct mmc_ioc_cmd c = { .write_flag = 1, .opcode = 6, .flags = R1B | CMD_AC,
            .arg = (3u << 24) | (179u << 16) | ((uint32_t)v << 8) | 1 };  /* CMD6 SWITCH write byte */
        if (ioctl(fd, MMC_IOC_CMD, &c) < 0) { perror("CMD6"); exit(1); }
        printf("CMD6 SWITCH EXT_CSD[179] <- 0x%02x\n", v);
        read_ext_csd(fd, ext);
        show(ext);
    }
    close(fd);
    return 0;
}
```

게스트 스크립트(가짜 SPL 32 KiB를 boot0에 쓰고, boot1로 부트 파티션을 바꾼다):

```sh
#!/bin/sh
# runs inside the QEMU guest (lab initramfs + emulated eMMC on sdhci-pci)
insmod /c2_sdhci_uhs2.ko && insmod /c2_sdhci_pci.ko && sleep 2
echo "== /proc/partitions"; cat /proc/partitions | grep -E "name|mmc"
echo "== sysfs"; for a in type name; do echo "$a=$(cat /sys/class/mmc_host/mmc0/mmc0:0001/$a)"; done
echo "boot0 force_ro=$(cat /sys/block/mmcblk0boot0/force_ro) ro=$(cat /sys/block/mmcblk0boot0/ro)"
X=/sys/kernel/debug/mmc0/mmc0:0001/ext_csd
echo "== debugfs ext_csd: $(wc -c < $X) chars (512 bytes as hex), byte 179 = 0x$(cut -c 359-360 $X)"
echo "== c2_mmcboot read"; /c2_mmcboot /dev/mmcblk0
echo "== write SPL to boot0 while force_ro=1:"
dd if=/c2_spl.bin of=/dev/mmcblk0boot0 bs=1k 2>&1 | head -1
echo 0 > /sys/block/mmcblk0boot0/force_ro
dd if=/c2_spl.bin of=/dev/mmcblk0boot0 bs=1k conv=fsync 2>&1 | tail -1
echo 1 > /sys/block/mmcblk0boot0/force_ro
head -c 16 /dev/mmcblk0boot0 | hexdump -C
echo "== mmc bootpart enable 2 1 (boot1, ack)"; /c2_mmcboot /dev/mmcblk0 enable 2 1
echo "== user area still readable after switch:"; head -c 16 /dev/mmcblk0 | hexdump -C
```

```sh
cd /var/tmp/C2 && gcc -std=gnu11 -Wall -Wextra -O2 -static $S/c2_mmcboot.c -o c2_mmcboot
printf "C2SPL-v1\x00" > c2_spl.bin && head -c 32759 /dev/zero >> c2_spl.bin
mkdir c2_r && cp -a /var/lab/build/rootfs/. c2_r/ && cp c2_sdhci/*.ko c2_mmcboot c2_spl.bin $S/c2_emmc_guest.sh c2_r/
(cd c2_r && find . | cpio -o -H newc --quiet | gzip) > c2_initrd.gz
rm -f c2_emmc.img && truncate -s $((72*1024*1024+128*1024)) c2_emmc.img
printf "USER-AREA-LBA0" | dd of=c2_emmc.img bs=1K seek=$((8*1024+128)) conv=notrunc status=none
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd c2_initrd.gz -device sdhci-pci \
  -drive if=none,id=e0,file=c2_emmc.img,format=raw \
  -device emmc,drive=e0,boot-partition-size=4194304,rpmb-partition-size=131072,boot-config=0x48 \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/c2_emmc_guest.sh" > c2_emmc.log 2>&1
for o in 0 $((4*1024*1024)) $((8*1024*1024)) $((8*1024*1024+128*1024)); do
  printf "file off 0x%07x: " $o; head -c $((o+16)) c2_emmc.img | tail -c 16 | od -An -c | head -1; done
```

```text
== /proc/partitions
major minor  #blocks  name
 179        0      65536 mmcblk0
 179       32       4096 mmcblk0boot0
 179       64       4096 mmcblk0boot1
== sysfs
type=MMC
name=QEMU!!
boot0 force_ro=1 ro=1
== debugfs ext_csd: 1025 chars (512 bytes as hex), byte 179 = 0x48
== c2_mmcboot read
EXT_CSD_REV[192]=5  BOOT_SIZE_MULT[226]=32 (4096 KiB each)  RPMB_SIZE_MULT[168]=1 (128 KiB)
PARTITION_CONFIG[179]=0x48 -> BOOT_ACK=1 BOOT_PARTITION_ENABLE=1(boot0) PARTITION_ACCESS=0
BOOT_BUS_CONDITIONS[177]=0x00  SEC_COUNT[212..215]=131072 sectors
== write SPL to boot0 while force_ro=1:
dd: error writing '/dev/mmcblk0boot0': Operation not permitted
32768 bytes (32.0KB) copied, 0.081263 seconds, 393.8KB/s
00000000  43 32 53 50 4c 2d 76 31  00 00 00 00 00 00 00 00  |C2SPL-v1........|
00000010
== mmc bootpart enable 2 1 (boot1, ack)
EXT_CSD_REV[192]=5  BOOT_SIZE_MULT[226]=32 (4096 KiB each)  RPMB_SIZE_MULT[168]=1 (128 KiB)
PARTITION_CONFIG[179]=0x48 -> BOOT_ACK=1 BOOT_PARTITION_ENABLE=1(boot0) PARTITION_ACCESS=0
BOOT_BUS_CONDITIONS[177]=0x00  SEC_COUNT[212..215]=131072 sectors
CMD6 SWITCH EXT_CSD[179] <- 0x50
EXT_CSD_REV[192]=5  BOOT_SIZE_MULT[226]=32 (4096 KiB each)  RPMB_SIZE_MULT[168]=1 (128 KiB)
PARTITION_CONFIG[179]=0x50 -> BOOT_ACK=1 BOOT_PARTITION_ENABLE=2(boot1) PARTITION_ACCESS=0
BOOT_BUS_CONDITIONS[177]=0x00  SEC_COUNT[212..215]=131072 sectors
== user area still readable after switch:
00000000  55 53 45 52 2d 41 52 45  41 2d 4c 42 41 30 00 00  |USER-AREA-LBA0..|
00000010
file off 0x0000000:    C   2   S   P   L   -   v   1  \0  \0  \0  \0  \0  \0  \0  \0
file off 0x0400000:   \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0
file off 0x0800000:   \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0
file off 0x0820000:    U   S   E   R   -   A   R   E   A   -   L   B   A   0  \0  \0
```

출력에서 볼 것:

- `force_ro=1`, 첫 `dd`는 `Operation not permitted`: **커널은 boot 파티션을 기본 읽기 전용으로 연다**. 실수로 `dd of=/dev/mmcblk0boot0` 해서 부트로더를 날리는 사고를 막기 위해서다. 업데이트 스크립트는 `echo 0 > /sys/block/mmcblk0boot0/force_ro` → 쓰기 → `echo 1`을 반드시 짝으로 한다.
- debugfs와 ioctl이 같은 값(0x48)을 보여 준다. `BOOT_SIZE_MULT=32` → 32 x 128 KiB = 4 MiB, `SEC_COUNT=131072` → user area 64 MiB. `EXT_CSD_REV=5`는 QEMU가 흉내 낸 eMMC 버전 값이다.
- CMD6 SWITCH 후 0x50: **다음 전원 ON 때 ROM은 boot1을 받는다**. 이 한 바이트가 A/B 부트로더 전환의 전부다(§10). 이 노트의 QEMU 실험은 레지스터 값까지만 보여 준다 — QEMU `virt`에는 eMMC에서 부팅하는 ROM이 없다.
- 호스트 쪽 파일 오프셋 0에 `C2SPL-v1`이 들어갔다: 게스트에서 boot0에 쓴 것이 정확히 boot0 영역에 들어갔다.

같은 일을 하는 표준 도구(이 VM에는 설치돼 있지 않아 실행하지 않음, 명령 형식만):

```sh
mmc extcsd read /dev/mmcblk0 | grep -i partition_config   # mmc-utils
mmc bootpart enable 1 1 /dev/mmcblk0                       # boot0에서 부팅, BOOT_ACK=1
```

```text
=> mmc partconf 0 1 1 0      (U-Boot: dev 0, boot_ack=1, boot_partition=1(boot0), access=0)
=> mmc dev 0 1               (U-Boot: hw 파티션 1 = boot0 선택 후 mmc write로 쓰기)
```

U-Boot의 `mmc partconf <dev> [[varname] | [<boot_ack> <boot_partition> <partition_access>]]` 형식은 랩의 U-Boot 소스 `cmd/mmc.c` 도움말에서 확인했다.

---

## 7. SD 카드와 디스크 이미지 — MBR/GPT와 raw 오프셋

### 7.1 SD에는 하드웨어 boot 파티션이 없다

SD 카드는 eMMC와 달리 boot0/boot1이 없다. 그래서 BootROM은 둘 중 하나로 첫 단계를 찾는다.

- **raw 오프셋 방식**: 카드의 **정해진 바이트 오프셋**(예: 32 KiB)에서 헤더를 찾는다. 파티션 테이블과 첫 파티션 사이 "빈 틈"에 부트로더를 `dd`로 넣는다. NXP i.MX, Rockchip, Allwinner가 이 방식.
- **파일시스템 방식**: 첫 FAT 파티션에서 **파일 이름**으로 찾는다. TI K3의 `tiboot3.bin`(MMCSD 파일시스템 부트 모드), Raspberry Pi 펌웨어(`start*.elf` 등)가 이 방식(기억 기반).

### 7.2 MBR과 GPT의 바이트 배치

- **MBR**: LBA 0(512 B). 0x1BE부터 16 B x 4 파티션 엔트리, 0x1FE에 `55 AA`. 파티션 테이블이 첫 512 B에만 있어서 그 뒤가 비어 있다 — 그래서 옛날 SoC는 1 KiB, 8 KiB 같은 작은 오프셋을 쓸 수 있었다.
- **GPT**: LBA 0 = protective MBR(타입 0xEE 하나가 디스크 전체를 덮음), LBA 1 = GPT 헤더(`EFI PART`), LBA 2~33 = 파티션 엔트리 128개 x 128 B = 16 KiB. 디스크 끝에 백업 엔트리와 백업 헤더. 헤더와 엔트리 배열 각각에 CRC32가 있다. 즉 **GPT는 처음 17 KiB(0x0~0x43FF)를 차지**한다.

### 7.3 SoC별 raw 오프셋 (기억 기반 — BSP 문서로 확인)

| SoC | SD/eMMC user area에서 첫 단계 위치 | GPT와 충돌? |
|---|---|---|
| i.MX6/7 | 1 KiB (IVT가 0x400) | 충돌. MBR만 쓰거나 GPT 엔트리 위치를 옮겨야 |
| i.MX8MQ, 8MM | 33 KiB | 충돌 없음 |
| i.MX8MN, 8MP, i.MX93 | 32 KiB (eMMC boot 파티션에서는 0) | 충돌 없음 |
| Rockchip (RK33xx, RK35xx) | sector 64 = 32 KiB (idbloader), U-Boot FIT은 보통 sector 16384 = 8 MiB | 충돌 없음 |
| Allwinner (대부분) | 8 KiB (일부 최신 SoC는 128 KiB도 찾음) | **충돌** — GPT 엔트리(1~17 KiB)와 겹침 |

### 7.4 예제 10 — root 없이 SD/eMMC 스타일 디스크 이미지 만들기

무엇을 확인하나: 128 MiB 이미지에 GPT(`sgdisk`), 32 KiB의 raw 부트로더 영역(랩의 BL1+FIP 1.76 MiB를 그대로 사용), 8 MiB부터 FAT32 부트 파티션(커널 `Image` + U-Boot `boot.scr`), 56 MiB부터 ext4 rootfs(랩 BusyBox 트리)를 넣는다. `losetup`/`mount` 없이 **파티션 파일시스템을 별도 파일로 만들어 `dd`로 박는 방식**이라 sudo가 필요 없다 — Yocto `wic`, Buildroot `genimage`도 내부적으로 같은 일을 한다(K 모듈).

```sh
#!/bin/bash
# c2_mksd.sh - build an SD/eMMC-style disk image without root: GPT + raw boot area + FAT + ext4
set -euo pipefail
cd /var/tmp/C2
IMG=c2_sd.img; MiB=$((1024*1024))
rm -f $IMG c2_p1.vfat c2_p2.ext4
truncate -s $((128*MiB)) $IMG

# 1) GPT: p1 = FAT boot (8 MiB .. 56 MiB), p2 = rootfs (56 MiB .. end-of-disk minus backup GPT)
sgdisk --clear \
  --new=1:8M:+48M  --typecode=1:0700 --change-name=1:boot   --attributes=1:set:2 \
  --new=2:56M:0    --typecode=2:8300 --change-name=2:rootfs \
  $IMG >/dev/null

# 2) raw bootloader area: BL1+FIP (first 0x1c2000 B of the lab NOR image) at 32 KiB = LBA 64
head -c $((0x1c2000)) /var/lab/out/flash.bin > c2_bootloader.bin
dd if=c2_bootloader.bin of=$IMG bs=1K seek=32 conv=notrunc status=none

# 3) FAT32 boot partition built as a file, then copied in at 8 MiB
mkfs.vfat -F 32 -n BOOT -C c2_p1.vfat $((48*1024)) >/dev/null
mkimage -A arm64 -T script -C none -n "c2 boot script" -d /Users/donh/.elinux_scratch/C2/c2_boot.cmd c2_boot.scr >/dev/null
mcopy -i c2_p1.vfat /var/lab/out/Image c2_boot.scr ::
mmove -i c2_p1.vfat ::c2_boot.scr ::boot.scr
dd if=c2_p1.vfat of=$IMG bs=1M seek=8 conv=notrunc status=none

# 4) ext4 rootfs from the lab BusyBox tree, copied in at 56 MiB
P2_SECT=$(sgdisk -i 2 $IMG | awk '/Partition size/{print $3}')
mkfs.ext4 -q -L rootfs -d /var/lab/build/rootfs c2_p2.ext4 $((P2_SECT/2))k
dd if=c2_p2.ext4 of=$IMG bs=1M seek=56 conv=notrunc status=none
ls -l $IMG c2_bootloader.bin c2_p1.vfat c2_p2.ext4
```

`boot.scr`의 원본(`c2_boot.cmd`)은 U-Boot 명령 몇 줄이다(§8에서 실행된다).

```text
echo ">>> c2 boot.scr running from ${devtype} ${devnum}:${distro_bootpart}"
load virtio 0:1 ${kernel_addr_r} Image
setenv bootargs "console=ttyAMA0 root=/dev/vda2 rootwait ro init=/init LAB_CMD=cat~/proc/cmdline;~mount~-t~ext4;~cat~/proc/partitions"
booti ${kernel_addr_r} - ${fdtcontroladdr}
```

```sh
bash $S/c2_mksd.sh
sgdisk -p c2_sd.img; fdisk -l c2_sd.img
```

```text
-rw-rw-r-- 1 donh donh   1843200 Oct 10 10:27 c2_bootloader.bin
-rw-rw-r-- 1 donh donh  50331648 Oct 10 10:27 c2_p1.vfat
-rw-rw-r-- 1 donh donh  75476992 Oct 10 10:27 c2_p2.ext4
-rw-rw-r-- 1 donh donh 134217728 Oct 10 10:27 c2_sd.img
Disk c2_sd.img: 262144 sectors, 128.0 MiB
Sector size (logical): 512 bytes
Disk identifier (GUID): ACA8CF79-6247-46B0-B124-AF5C575A0984
Partition table holds up to 128 entries
Main partition table begins at sector 2 and ends at sector 33
First usable sector is 34, last usable sector is 262110
Partitions will be aligned on 2048-sector boundaries
Total free space is 16350 sectors (8.0 MiB)

Number  Start (sector)    End (sector)  Size       Code  Name
   1           16384          114687   48.0 MiB    0700  boot
   2          114688          262110   72.0 MiB    8300  rootfs
Disk c2_sd.img: 128 MiB, 134217728 bytes, 262144 sectors
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / 512 bytes
Disklabel type: gpt
Disk identifier: ACA8CF79-6247-46B0-B124-AF5C575A0984

Device      Start    End Sectors Size Type
c2_sd.img1  16384 114687   98304  48M Microsoft basic data
c2_sd.img2 114688 262110  147423  72M Linux filesystem
```

출력에서 볼 것: `Total free space is 16350 sectors (8.0 MiB)` — 이 "파티션에 속하지 않은 8 MiB"(LBA 34~16383)가 바로 **raw 부트로더 영역**이다. 파티션 도구는 이 영역을 빈 공간으로 보므로, 누가 `sgdisk --new=1:0:...`처럼 시작을 기본값(LBA 2048 = 1 MiB)으로 두면 **파티션이 부트로더를 덮는다**. 그래서 i.MX·Rockchip 이미지의 첫 파티션은 4~16 MiB에서 시작한다. GUID는 실행마다 다르다.

### 7.5 예제 11 — 바이트 단위로 확인

무엇을 확인하나: protective MBR, GPT 헤더, 첫 엔트리, 32 KiB의 BL1, FIP magic, FAT 부트 섹터, ext4 superblock magic(0xEF53), 디스크 끝의 백업 GPT 헤더를 hexdump로 직접 본다.

```sh
hexdump -C -s 0x1b0 -n 80 c2_sd.img            # protective MBR 파티션 엔트리 + 55 AA
hexdump -C -s 512 -n 96 c2_sd.img              # LBA1: GPT header
hexdump -C -s 1024 -n 128 c2_sd.img            # LBA2: entry #1
hexdump -C -s $((0x8000)) -n 32 c2_sd.img      # 32 KiB: BL1 코드
hexdump -C -s $((0x8000+0x40000)) -n 16 c2_sd.img   # 32 KiB + 256 KiB: FIP magic
hexdump -C -s $((8*1024*1024)) -n 96 c2_sd.img | head -6   # p1: FAT32 boot sector
hexdump -C -s $((56*1024*1024+1024+0x38)) -n 2 c2_sd.img   # p2: ext4 s_magic
hexdump -C -s $((128*1024*1024-512)) -n 16 c2_sd.img       # 마지막 LBA: backup GPT header
```

```text
000001b0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
000001c0  02 00 ee 51 01 10 01 00  00 00 ff ff 03 00 00 00  |...Q............|
000001d0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000001f0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 55 aa  |..............U.|
00000200
00000200  45 46 49 20 50 41 52 54  00 00 01 00 5c 00 00 00  |EFI PART....\...|
00000210  9c 4f df 9d 00 00 00 00  01 00 00 00 00 00 00 00  |.O..............|
00000220  ff ff 03 00 00 00 00 00  22 00 00 00 00 00 00 00  |........".......|
00000230  de ff 03 00 00 00 00 00  79 cf a8 ac 47 62 b0 46  |........y...Gb.F|
00000240  b1 24 af 5c 57 5a 09 84  02 00 00 00 00 00 00 00  |.$.\WZ..........|
00000250  80 00 00 00 80 00 00 00  bb b2 a0 7e 00 00 00 00  |...........~....|
00000260
00000400  a2 a0 d0 eb e5 b9 33 44  87 c0 68 b6 b7 26 99 c7  |......3D..h..&..|
00000410  e0 80 00 a8 31 b8 15 4e  93 44 5f 7f ad bf 9a 1b  |....1..N.D_.....|
00000420  00 40 00 00 00 00 00 00  ff bf 01 00 00 00 00 00  |.@..............|
00000430  04 00 00 00 00 00 00 00  62 00 6f 00 6f 00 74 00  |........b.o.o.t.|
00000440  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000480
00008000  00 06 81 d2 a0 18 a6 f2  00 00 6b b2 00 10 1e d5  |..........k.....|
00008010  df 3f 03 d5 f4 0b 00 94  40 00 00 b4 00 00 1f d6  |.?......@.......|
00008020
00048000  01 00 64 aa 78 56 34 12  00 00 00 00 00 00 00 00  |..d.xV4.........|
00048010
00800000  eb 58 90 6d 6b 66 73 2e  66 61 74 00 02 01 20 00  |.X.mkfs.fat... .|
00800010  02 00 00 00 00 f8 00 00  20 00 08 00 00 00 00 00  |........ .......|
00800020  00 80 01 00 f4 02 00 00  00 00 00 00 02 00 00 00  |................|
00800030  01 00 06 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00800040  80 00 29 b8 95 0d 57 42  4f 4f 54 20 20 20 20 20  |..)...WBOOT     |
00800050  20 20 46 41 54 33 32 20  20 20 0e 1f be 77 7c ac  |  FAT32   ...w|.|
03800438  53 ef                                             |S.|
0380043a
07fffe00  45 46 49 20 50 41 52 54  00 00 01 00 5c 00 00 00  |EFI PART....\...|
07fffe10
```

출력에서 볼 것:

- 0x1C2의 `ee`: protective MBR의 타입 0xEE. 시작 LBA `01 00 00 00`, 크기 `ff ff 03 00` = 262143 섹터. 0x1FE `55 aa`.
- GPT 헤더: `EFI PART`, revision `00 00 01 00`(1.0), 헤더 크기 0x5c(92), 헤더 CRC `9c4fdf9d`, 자기 위치 LBA 1, 백업 위치 LBA 0x3ffff, 사용 가능 0x22(34)~0x3ffde, 엔트리 시작 LBA 2, 엔트리 수 0x80(128), 크기 0x80(128 B), 엔트리 CRC `7ea0b2bb`.
- 엔트리 #1: 타입 GUID가 `a2 a0 d0 eb …`로 저장돼 있는데, 이는 `EBD0A0A2-B9E5-4433-87C0-68B6B72699C7`(Microsoft basic data)를 **앞 세 필드만 리틀 엔디언으로** 쓴 GUID 고유의 혼합 엔디언이다. 시작 LBA 0x4000(16384), 속성 `04`(bit 2 = legacy BIOS bootable, U-Boot 부트 스캔의 힌트), 이름은 UTF-16LE "boot".
- 32 KiB에 BL1 명령어, 그 뒤 256 KiB에 FIP magic — **raw 영역 안에서도 오프셋 관계가 NOR 때와 똑같이 유지**된다(그래서 이미지를 미디어 간에 옮겨도 FIP 기준 상대 주소가 깨지지 않는다).
- ext4 superblock은 파티션 시작 + 1024, 그 안 0x38에 magic `53 ef`(0xEF53). 디스크 끝 LBA에는 백업 GPT 헤더.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="250" y="16" text-anchor="middle" font-size="13" font-weight="bold">c2_sd.img (128 MiB, 축척 무시, 실측 오프셋)</text>
  <text x="112" y="40" text-anchor="end" font-size="12">0x0000</text>
  <rect x="120" y="26" width="260" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="44" text-anchor="middle" font-size="12">LBA0 protective MBR (0xEE)</text>
  <text x="112" y="66" text-anchor="end" font-size="12">0x0200</text>
  <rect x="120" y="52" width="260" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="70" text-anchor="middle" font-size="12">LBA1 GPT 헤더 "EFI PART"</text>
  <text x="112" y="92" text-anchor="end" font-size="12">0x0400</text>
  <rect x="120" y="78" width="260" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="100" text-anchor="middle" font-size="12">LBA2~33 엔트리 128 x 128 B</text>
  <text x="112" y="126" text-anchor="end" font-size="12">0x4400</text>
  <rect x="120" y="112" width="260" height="22" fill="#888" fill-opacity="0.04" stroke="#888" stroke-dasharray="4 3"/>
  <text x="250" y="128" text-anchor="middle" font-size="12">빈 틈</text>
  <text x="112" y="148" text-anchor="end" font-size="12">0x8000</text>
  <rect x="120" y="134" width="260" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="250" y="152" text-anchor="middle" font-size="12">raw 부트로더 (BL1 + FIP)</text>
  <text x="250" y="168" text-anchor="middle" font-size="12">1.76 MiB, 파티션 아님</text>
  <text x="112" y="188" text-anchor="end" font-size="12">0x1CA000</text>
  <rect x="120" y="174" width="260" height="22" fill="#888" fill-opacity="0.04" stroke="#888" stroke-dasharray="4 3"/>
  <text x="250" y="190" text-anchor="middle" font-size="12">빈 틈 (~8 MiB까지)</text>
  <text x="112" y="210" text-anchor="end" font-size="12">0x800000</text>
  <rect x="120" y="196" width="260" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="250" y="220" text-anchor="middle" font-size="12">p1 FAT32 "BOOT" 48 MiB</text>
  <text x="250" y="238" text-anchor="middle" font-size="12">Image, boot.scr</text>
  <text x="112" y="266" text-anchor="end" font-size="12">0x3800000</text>
  <rect x="120" y="252" width="260" height="64" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="250" y="280" text-anchor="middle" font-size="12">p2 ext4 "rootfs" 72 MiB</text>
  <text x="250" y="298" text-anchor="middle" font-size="12">BusyBox 트리</text>
  <text x="112" y="330" text-anchor="end" font-size="12">0x7FFBE00</text>
  <rect x="120" y="316" width="260" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="338" text-anchor="middle" font-size="12">백업 엔트리 + 백업 헤더</text>
  <text x="112" y="364" text-anchor="end" font-size="12">0x8000000</text>
  <line x1="472" y1="70" x2="384" y2="70" stroke="#d0564a" marker-end="url(#ah5)"/>
  <text x="478" y="66" font-size="12">i.MX6: 1 KiB</text>
  <text x="478" y="82" font-size="12">(엔트리 영역과 겹침)</text>
  <line x1="472" y1="102" x2="384" y2="102" stroke="#d0564a" marker-end="url(#ah5)"/>
  <text x="478" y="98" font-size="12">Allwinner: 8 KiB</text>
  <text x="478" y="114" font-size="12">(엔트리 영역과 겹침)</text>
  <line x1="472" y1="146" x2="384" y2="146" stroke="#3f9a6b" marker-end="url(#ah5)"/>
  <text x="478" y="142" font-size="12">i.MX8MN/MP, Rockchip:</text>
  <text x="478" y="158" font-size="12">32 KiB (이 이미지)</text>
  <text x="478" y="182" font-size="12">i.MX8MQ/MM: 33 KiB</text>
  <text x="340" y="390" text-anchor="middle" font-size="12">오른쪽 오프셋은 기억 기반 — 반드시 해당 SoC 문서로 확인</text>
</svg>
```
그림 7 — 이 노트에서 만든 디스크 이미지의 바이트 배치와 SoC별 raw 부트로더 오프셋. GPT는 처음 17 KiB를 쓰므로 1 KiB·8 KiB에 부트로더를 두는 SoC는 GPT 엔트리를 다른 곳으로 옮겨야 한다(예제 13).

### 7.6 예제 12 — C로 GPT를 읽고 CRC32를 검증하기

무엇을 확인하나: GPT 헤더와 엔트리 배열의 CRC32를 직접 계산해 저장된 값과 비교한다. 엔트리 영역 1바이트를 뒤집으면 어떻게 되는지, `sgdisk -v`가 백업 테이블로 어떻게 버티는지도 본다(부트로더를 잘못된 오프셋에 썼을 때 실제로 일어나는 일이다).

```c
/* c2_gpt.c - read a GPT header + entries from a disk image and verify both CRC32s */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>

struct __attribute__((packed)) gpt_hdr {
    char sig[8]; uint32_t rev, hsize, hcrc, rsvd;
    uint64_t my_lba, alt_lba, first_lba, last_lba;
    uint8_t disk_guid[16];
    uint64_t ent_lba; uint32_t n_ent, ent_size, ent_crc;
};
struct __attribute__((packed)) gpt_ent {
    uint8_t type[16], uniq[16]; uint64_t first, last, attr; uint16_t name[36];
};

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: %s disk.img\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); exit(1); }
    uint8_t lba1[512];
    if (fseek(f, 512, SEEK_SET) || fread(lba1, 512, 1, f) != 1) { perror("read LBA1"); exit(1); }
    struct gpt_hdr h; memcpy(&h, lba1, sizeof h);
    uint32_t want = h.hcrc;
    ((struct gpt_hdr *)lba1)->hcrc = 0;                 /* CRC is computed with field = 0 */
    uint32_t got = crc32(0, lba1, h.hsize);
    printf("sig=%.8s rev=0x%08x hdr_size=%u my_lba=%llu alt_lba=%llu usable=%llu..%llu\n",
           h.sig, h.rev, h.hsize, (unsigned long long)h.my_lba, (unsigned long long)h.alt_lba,
           (unsigned long long)h.first_lba, (unsigned long long)h.last_lba);
    printf("header crc32 stored=0x%08x computed=0x%08x %s\n", want, got, want == got ? "OK" : "BAD");

    size_t tbl = (size_t)h.n_ent * h.ent_size;
    uint8_t *t = malloc(tbl);
    if (!t) { perror("malloc"); exit(1); }
    if (fseek(f, (long)h.ent_lba * 512, SEEK_SET) || fread(t, tbl, 1, f) != 1) { perror("read tbl"); exit(1); }
    uint32_t ecrc = crc32(0, t, tbl);
    printf("entries @LBA%llu: %u x %u B, crc32 stored=0x%08x computed=0x%08x %s\n",
           (unsigned long long)h.ent_lba, h.n_ent, h.ent_size, h.ent_crc, ecrc,
           h.ent_crc == ecrc ? "OK" : "BAD");
    for (uint32_t i = 0; i < h.n_ent; i++) {
        struct gpt_ent *e = (struct gpt_ent *)(t + i * h.ent_size);
        static const uint8_t z[16];
        if (!memcmp(e->type, z, 16)) continue;          /* unused slot */
        char name[37] = {0};
        for (int k = 0; k < 36 && e->name[k]; k++) name[k] = (char)e->name[k];
        printf("  p%u %-7s LBA %6llu..%6llu  byte 0x%08llx  attr=0x%llx\n", i + 1, name,
               (unsigned long long)e->first, (unsigned long long)e->last,
               (unsigned long long)e->first * 512, (unsigned long long)e->attr);
    }
    free(t); fclose(f);
    return 0;
}
```

```sh
cd /var/tmp/C2 && gcc -std=gnu11 -Wall -Wextra -O2 $S/c2_gpt.c -o c2_gpt -lz && ./c2_gpt c2_sd.img
cp c2_sd.img c2_sd_bad.img
printf "\x01" | dd of=c2_sd_bad.img bs=1 seek=$((1024+0x30)) conv=notrunc status=none
echo "--- after flipping 1 byte in entry #1:"; ./c2_gpt c2_sd_bad.img
sgdisk -v c2_sd_bad.img 2>&1 | head -12
```

```text
sig=EFI PART rev=0x00010000 hdr_size=92 my_lba=1 alt_lba=262143 usable=34..262110
header crc32 stored=0x9ddf4f9c computed=0x9ddf4f9c OK
entries @LBA2: 128 x 128 B, crc32 stored=0x7ea0b2bb computed=0x7ea0b2bb OK
  p1 boot    LBA  16384..114687  byte 0x00800000  attr=0x4
  p2 rootfs  LBA 114688..262110  byte 0x03800000  attr=0x0
--- after flipping 1 byte in entry #1:
sig=EFI PART rev=0x00010000 hdr_size=92 my_lba=1 alt_lba=262143 usable=34..262110
header crc32 stored=0x9ddf4f9c computed=0x9ddf4f9c OK
entries @LBA2: 128 x 128 B, crc32 stored=0x7ea0b2bb computed=0x07a71197 BAD
  p1 boot    LBA  16384..114687  byte 0x00800000  attr=0x1
  p2 rootfs  LBA 114688..262110  byte 0x03800000  attr=0x0
Caution! After loading partitions, the CRC doesn't check out!
Warning! Main partition table CRC mismatch! Loaded backup partition table
instead of main partition table!

Warning! One or more CRCs don't match. You should repair the disk!
Main header: OK
Backup header: OK
Main partition table: ERROR
Backup partition table: OK
```

출력에서 볼 것: 헤더 CRC는 **CRC 필드를 0으로 두고** 계산한다(Don이 펌웨어 이미지 헤더에서 쓰던 방식과 같다). 엔트리 1바이트(속성 0x4 → 0x1)가 바뀌자 엔트리 CRC가 BAD가 되고, `sgdisk`는 디스크 끝의 **백업 테이블**을 대신 쓴다. 커널의 GPT 파서도 주 테이블이 깨지면 백업을 찾는다(§11). GPT 자체가 "주 + 백업" 이중화 구조라는 점이 부트로더 이중화(§10)와 같은 발상이다.

### 7.7 예제 13 — 8 KiB 부트로더(Allwinner 방식)와 GPT의 충돌, 그리고 해결

무엇을 확인하나: 부트로더를 8 KiB에 쓰면 GPT 엔트리 배열(1~17 KiB)을 덮어 CRC가 깨진다. `sgdisk -j`(`--adjust-main-table`)로 주 엔트리 배열을 1 MiB(LBA 2048)로 옮기면 공존할 수 있다.

```sh
cp c2_sd.img c2_sd_8k.img
dd if=c2_bootloader.bin of=c2_sd_8k.img bs=1K seek=8 count=64 conv=notrunc status=none
./c2_gpt c2_sd_8k.img | sed -n 2,3p
echo "--- fix: move main table to LBA 2048 (1 MiB), then rewrite bootloader at 8K"
cp c2_sd.img c2_sd_8k.img && sgdisk -j 2048 c2_sd_8k.img
dd if=c2_bootloader.bin of=c2_sd_8k.img bs=1K seek=8 count=64 conv=notrunc status=none
./c2_gpt c2_sd_8k.img | sed -n 1,3p
sgdisk -p c2_sd_8k.img | grep -E "Main partition|First usable"
```

```text
header crc32 stored=0x9ddf4f9c computed=0x9ddf4f9c OK
entries @LBA2: 128 x 128 B, crc32 stored=0x7ea0b2bb computed=0x6c3053eb BAD
--- fix: move main table to LBA 2048 (1 MiB), then rewrite bootloader at 8K
Warning: The kernel is still using the old partition table.
The new table will be used at the next reboot or after you
run partprobe(8) or kpartx(8)
The operation has completed successfully.
sig=EFI PART rev=0x00010000 hdr_size=92 my_lba=1 alt_lba=262143 usable=2080..262110
header crc32 stored=0x6a6b5f52 computed=0x6a6b5f52 OK
entries @LBA2048: 128 x 128 B, crc32 stored=0x7ea0b2bb computed=0x7ea0b2bb OK
```

```text
Main partition table begins at sector 2048 and ends at sector 2079
First usable sector is 2080, last usable sector is 262110
```

출력에서 볼 것: 헤더는 LBA 1에 그대로 있어 OK지만 엔트리가 깨진다. `-j 2048` 후에는 헤더의 "엔트리 시작 LBA" 필드가 2048로 바뀌고(그래서 헤더 CRC도 바뀜), 8 KiB 부트로더와 공존한다. 엔트리 CRC 값은 내용이 같으니 그대로다. (`kernel is still using the old partition table` 경고는 VM 커널과 무관한 이미지 파일이라 무시해도 된다.) U-Boot에도 같은 목적의 옵션 `CONFIG_EFI_PARTITION_ENTRIES_OFF`가 있다(랩 U-Boot의 `disk/Kconfig`에 정의돼 있고 `.config` 값은 0 = 기본 위치). 다른 해법은 MBR을 쓰는 것이다 — 많은 Allwinner/i.MX6 이미지가 아직 MBR인 이유다.

---

## 8. U-Boot가 그 디스크를 읽는 모습 — QEMU에서 확인

### 8.1 예제 14 — `part list`, `ls`, raw 섹터 읽기, bootflow, 그리고 부팅

무엇을 확인하나: 비보안 경로(`-bios u-boot.bin`)로 U-Boot를 띄우고 §7의 이미지를 virtio 디스크로 붙인다. expect로 autoboot를 멈추고 파티션과 파일, raw 부트로더 영역(LBA 64, LBA 576)을 읽은 뒤, U-Boot의 표준 부트(bootstd)가 `boot.scr`를 찾아 커널을 띄우고 **커널이 vda2를 rootfs로 마운트**하는 데까지 본다.

```text
#!/usr/bin/expect -f
# c2_uboot.exp - stop U-Boot autoboot, inspect the virtio disk, then let bootstd boot it
set timeout 120
log_user 1
spawn qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
    -bios /var/lab/out/u-boot.bin \
    -drive if=virtio,file=/var/tmp/C2/c2_sd.img,format=raw
expect "Hit any key to stop autoboot"
send " "
expect "=> "
foreach cmd {
    "virtio scan"
    "virtio info"
    "part list virtio 0"
    "ls virtio 0:1"
    "ls virtio 0:2 /"
    "virtio read ${loadaddr} 0x40 1"
    "md.b ${loadaddr} 0x10"
    "virtio read ${loadaddr} 0x240 1"
    "md.l ${loadaddr} 4"
    "bootflow scan -l virtio"
} {
    send "$cmd\r"
    expect "=> "
}
send "boot\r"
expect "Power down"
```

```sh
cd /var/tmp/C2 && expect $S/c2_uboot.exp > c2_uboot.log 2>&1
tr -d '\r' < c2_uboot.log | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' > c2_uboot.txt   # 핵심 줄만 아래에 발췌
```

```text
U-Boot 2026.10 (Oct 10 2026 - 09:59:26 -0700)

DRAM:  512 MiB
...
Flash: 64 MiB
...
=> virtio info
Device 0: 00001af4 VirtIO Block Device
            Type: Hard Disk
            Capacity: 128.0 MB = 0.1 GB (262144 x 512)
=> part list virtio 0

Partition Map for virtio device 0  --   Partition Type: EFI

Part	Start LBA	End LBA		Name
	Attributes
	Type GUID
	Partition GUID
  1	0x00004000	0x0001bfff	"boot"
	attrs:	0x0000000000000004
	type:	ebd0a0a2-b9e5-4433-87c0-68b6b72699c7
	guid:	a80080e0-b831-4e15-9344-5f7fadbf9a1b
  2	0x0001c000	0x0003ffde	"rootfs"
	attrs:	0x0000000000000000
	type:	0fc63daf-8483-4772-8e79-3d69d8477de4
	guid:	01cdba5e-d9b5-42f5-84a5-5b2f177a4d66
=> ls virtio 0:1
 42740224   Image
      365   boot.scr

2 file(s), 0 dir(s)

=> ls virtio 0:2 /
            ./
            ../
            lost+found/
            bin/
            dev/
            etc/
      493   init
            lib/
    <SYM>   linuxrc
...
2 file(s), 14 dir(s)

=> virtio read ${loadaddr} 0x40 1

virtio read: device 0 block # 64, count 1 ... 1 blocks read: OK
=> md.b ${loadaddr} 0x10
40200000: 00 06 81 d2 a0 18 a6 f2 00 00 6b b2 00 10 1e d5  ..........k.....
=> virtio read ${loadaddr} 0x240 1

virtio read: device 0 block # 576, count 1 ... 1 blocks read: OK
=> md.l ${loadaddr} 4
40200000: aa640001 12345678 00000000 00000000  ..d.xV4.........
=> bootflow scan -l virtio
Scanning for bootflows with label 'virtio'
Seq  Method       State   Uclass    Part  Name                      Filename
---  -----------  ------  --------  ----  ------------------------  ----------------
Hunting with: virtio
Scanning bootdev 'virtio-blk#33.bootdev':
  0  script       ready   virtio       1  virtio-blk#33.bootdev.par /boot.scr
No more bootdevs
---  -----------  ------  --------  ----  ------------------------  ----------------
(1 bootflow, 1 valid)
=> boot
Scanning for bootflows in all bootdevs
...
Scanning bootdev 'virtio-blk#33.bootdev':
  1  script       ready   virtio       1  virtio-blk#33.bootdev.par /boot.scr
** Booting bootflow 'virtio-blk#33.bootdev.part_1' with script
>>> c2 boot.scr running from virtio 0:1
42740224 bytes read in 103 ms (395.7 MiB/s)
...
Starting kernel ...

[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
...
[    1.817660] virtio_blk virtio1: [vda] 262144 512-byte logical blocks (134 MB/128 MiB)
[    1.860173]  vda: vda1 vda2
...
[    2.616890] EXT4-fs (vda2): mounted filesystem 710e7826-fc4f-486c-94c9-e19b110a747d ro with ordered data mode. Quota mode: none.
[    2.617345] VFS: Mounted root (ext4 filesystem) readonly on device 254:2.
[    2.712186] Run /init as init process
lab initramfs: 6.18.55 up 2.52s
console=ttyAMA0 root=/dev/vda2 rootwait ro init=/init LAB_CMD=cat~/proc/cmdline;~mount~-t~ext4;~cat~/proc/partitions
/dev/root on / type ext4 (ro,relatime)
major minor  #blocks  name
...
 254        0     131072 vda
 254        1      49152 vda1
 254        2      73711 vda2
...
[    3.266391] reboot: Power down
```

출력에서 볼 것:

- `part list`의 시작 LBA 0x4000 = 16384 = 8 MiB, 타입 GUID가 사람이 읽는 순서로 풀려 있다(hexdump의 혼합 엔디언과 비교). `attrs 0x4`는 우리가 준 bootable 속성.
- **U-Boot도 raw 영역을 섹터 번호로 읽을 수 있다**: block 64(32 KiB)에 BL1 명령어, block 576(= 64 + 512 = 32 KiB + 256 KiB)에 FIP magic `aa640001`. 실기에서 "SD의 부트로더가 제대로 쓰였나"를 U-Boot 콘솔에서 확인하는 방법이 이것이다(`mmc read` 동일).
- `bootflow scan`이 파티션 1의 `/boot.scr`를 찾았다. U-Boot의 bootstd는 **장치 → 파티션 → 부트 방식(script, extlinux, EFI)** 을 순서대로 훑는다 — BootROM fallback의 소프트웨어 버전이다(C3에서 자세히). `boot` 전체 스캔에서는 EFI boot manager가 먼저 시도하다 실패하고(로그 생략 부분) 스크립트로 넘어간다.
- 커널이 `vda: vda1 vda2`로 GPT를 파싱했고 `root=/dev/vda2`를 ext4로 마운트했다. 우리가 손으로 만든 이미지가 **ROM 역할 영역 + 부트 파티션 + rootfs**를 갖춘 "부팅 가능한 미디어"라는 것이 확인됐다. (단, QEMU `virt`의 비보안 경로는 `-bios`로 U-Boot를 직접 올리므로 32 KiB의 BL1은 실행되지 않는다 — 실제 SoC라면 ROM이 그 자리를 읽는다.)

### 8.2 예제 15 — `mkimage -l`로 U-Boot 이미지 포맷 들여다보기

무엇을 확인하나: 부트 미디어에 올라가는 U-Boot용 이미지 세 종류 — legacy uImage(64 B 헤더), FIT(DT 형식 컨테이너), 스크립트 — 의 헤더를 본다. FIT의 해시는 C5/L5에서 서명으로 확장된다.

```text
/dts-v1/;
/ {
    description = "c2 FIT: lab kernel + QEMU virt DTB";
    #address-cells = <1>;
    images {
        kernel-1 {
            description = "Linux 6.18.55";
            data = /incbin/("/var/lab/out/Image");
            type = "kernel"; arch = "arm64"; os = "linux"; compression = "none";
            load = <0x40400000>; entry = <0x40400000>;
            hash-1 { algo = "sha256"; };
        };
        fdt-1 {
            description = "qemu virt";
            data = /incbin/("/var/lab/out/virt.dtb");
            type = "flat_dt"; arch = "arm64"; compression = "none";
            hash-1 { algo = "sha256"; };
        };
    };
    configurations {
        default = "conf-1";
        conf-1 { kernel = "kernel-1"; fdt = "fdt-1"; };
    };
};
```

```sh
cd /var/tmp/C2 && mkimage -V
mkimage -f $S/c2_fit.its c2_image.itb > /dev/null && ls -l c2_image.itb && mkimage -l c2_image.itb
mkimage -A arm64 -O linux -T kernel -C none -a 0x40400000 -e 0x40400000 -n "c2 legacy uImage" \
  -d /var/lab/out/Image c2_uImage >/dev/null && mkimage -l c2_uImage && hexdump -C -n 64 c2_uImage
mkimage -l c2_boot.scr
```

```text
mkimage version 2025.10
-rw-rw-r-- 1 donh donh 43789760 Oct 10 10:32 c2_image.itb
FIT description: c2 FIT: lab kernel + QEMU virt DTB
Created:         Sat Oct 10 10:32:57 2026
 Image 0 (kernel-1)
  Description:  Linux 6.18.55
  Created:      Sat Oct 10 10:32:57 2026
  Type:         Kernel Image
  Compression:  uncompressed
  Data Size:    42740224 Bytes = 41738.50 KiB = 40.76 MiB
  Architecture: AArch64
  OS:           Linux
  Load Address: 0x40400000
  Entry Point:  0x40400000
  Hash algo:    sha256
  Hash value:   6718e4e6cbea7fe66fbd3eaf1c1986f306d2ed5c134a2df43a2a7c05337f8448
 Image 1 (fdt-1)
  Description:  qemu virt
  Created:      Sat Oct 10 10:32:57 2026
  Type:         Flat Device Tree
  Compression:  uncompressed
  Data Size:    1048576 Bytes = 1024.00 KiB = 1.00 MiB
  Architecture: AArch64
  Hash algo:    sha256
  Hash value:   ee7fd97ea202f8038658389347dac836bbd6f229edee259d7f74e84a12f36e6b
 Default Configuration: 'conf-1'
 Configuration 0 (conf-1)
  Description:  unavailable
  Kernel:       kernel-1
  FDT:          fdt-1
Image Name:   c2 legacy uImage
Created:      Sat Oct 10 10:32:57 2026
Image Type:   AArch64 Linux Kernel Image (uncompressed)
Data Size:    42740224 Bytes = 41738.50 KiB = 40.76 MiB
Load Address: 40400000
Entry Point:  40400000
00000000  27 05 19 56 cf 2f e8 18  6a ca 76 c9 02 8c 2a 00  |'..V./..j.v...*.|
00000010  40 40 00 00 40 40 00 00  14 85 fb e9 05 16 02 00  |@@..@@..........|
00000020  63 32 20 6c 65 67 61 63  79 20 75 49 6d 61 67 65  |c2 legacy uImage|
00000030  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00000040
Image Name:   c2 boot script
Created:      Sat Oct 10 10:27:13 2026
Image Type:   AArch64 Linux Script (uncompressed)
Data Size:    301 Bytes = 0.29 KiB = 0.00 MiB
Load Address: 00000000
Entry Point:  00000000
Contents:
   Image 0: 293 Bytes = 0.29 KiB = 0.00 MiB
```

출력에서 볼 것 (날짜·CRC는 실행마다 다르다): legacy 헤더는 **빅 엔디언**이다 — magic `27 05 19 56`(0x27051956), 그 뒤 헤더 CRC, 시간, 크기 `02 8c 2a 00`(= 42740224), 로드 `40 40 00 00`, entry, 데이터 CRC, OS/arch/type/comp 바이트, 이름 32 B. Don이 다룬 펌웨어 헤더와 모양이 같다. FIT는 이미지마다 sha256 해시를 품고, 여러 커널·DTB 조합을 configuration으로 고를 수 있다. `virt.dtb`가 1 MiB인 것은 QEMU `dumpdtb`가 패딩한 크기다. `mkimage`는 VM의 Ubuntu 패키지(2025.10)이고 랩 U-Boot는 v2026.10이지만 이 포맷들은 오래 안정적이라 서로 읽힌다.

---

## 9. UFS · USB/시리얼 복구 모드 · 공장 플래싱

### 9.1 UFS (실행하지 않음, 기억 기반)

고성능 SoC(스마트폰, 일부 차량·Jetson급)는 eMMC 대신 **UFS**를 쓴다. SCSI 명령 집합 위에서 여러 **LU(Logical Unit)** 를 노출하고, 부팅용 **Boot LU A/B** 두 개가 있으며 `bBootLunEn` 속성이 어느 쪽에서 부팅할지 정한다 — eMMC의 boot0/boot1 + PARTITION_CONFIG와 같은 개념이다. RPMB도 well-known LU로 있다. Linux에서는 `/dev/sdX`(SCSI 디스크)로 보이고, 속성은 `ufs-utils` 같은 도구나 `/sys/bus/platform/drivers/ufshcd/*/attributes/`로 다룬다.

### 9.2 USB 복구 모드 — 벽돌 보드의 마지막 안전망 (실행하지 않음, 하드웨어 필요, 기억 기반)

BootROM에는 **USB 장치(device) 모드 드라이버**가 들어 있다. 부트 미디어가 없거나 모드 핀이 serial download면, SoC가 USB 장치로 호스트 PC에 나타나고 **호스트가 첫 단계 이미지를 RAM으로 밀어 넣어 실행**시킨다. 그 첫 단계(보통 U-Boot SPL + U-Boot)가 다시 USB로 나머지 이미지를 받아 eMMC/NAND에 쓴다.

| 벤더 | ROM 모드 이름 | 호스트 도구 | 대표 명령 (형식 예) |
|---|---|---|---|
| NXP i.MX | Serial Download Protocol (SDP, USB HID) | `uuu` (mfgtools) | `uuu -b emmc_all flash.bin image.wic` |
| TI K3 | USB DFU (backup boot mode) | `dfu-util` | `dfu-util -a bootloader -D tiboot3.bin` |
| Rockchip | maskrom (USB) | `rkdeveloptool` | `rkdeveloptool db loader.bin`, `rkdeveloptool wl 0 image.img` |
| Allwinner | FEL | `sunxi-fel` | `sunxi-fel uboot u-boot-sunxi-with-spl.bin` |
| Raspberry Pi (CM4 등) | USB boot | `rpiboot` (usbboot) | `rpiboot` → eMMC가 USB 저장 장치로 보임 |

U-Boot 쪽에도 같은 역할의 명령이 있다: `ums 0 mmc 0`(eMMC를 USB 저장 장치로 노출 → PC에서 `dd`), `dfu 0 mmc 0`, `fastboot usb 0`. "ROM 복구 모드 → U-Boot 로드 → U-Boot의 ums/fastboot로 대용량 쓰기"가 공장·현장 복구의 표준 2단계다.

### 9.3 공장 플래싱 흐름

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="120" y="20" text-anchor="middle" font-size="13" font-weight="bold">공장 PC (uuu 등)</text>
  <text x="520" y="20" text-anchor="middle" font-size="13" font-weight="bold">빈 보드 (SoC + 빈 eMMC)</text>
  <line x1="120" y1="30" x2="120" y2="320" stroke="currentColor" stroke-dasharray="3 3"/>
  <line x1="520" y1="30" x2="520" y2="320" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="320" y="44" text-anchor="middle" font-size="12">① 보드 ROM: 이미지 없음 → USB 장치로 전환</text>
  <line x1="510" y1="70" x2="130" y2="70" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="320" y="64" text-anchor="middle" font-size="12">USB 열거 (VID:PID 보임)</text>
  <line x1="130" y1="102" x2="510" y2="102" stroke="#4a7bd0" marker-end="url(#ah6)"/>
  <text x="320" y="96" text-anchor="middle" font-size="12">② SPL/U-Boot를 SRAM·DRAM으로 전송</text>
  <text x="320" y="130" text-anchor="middle" font-size="12">③ 보드: U-Boot 실행 (DRAM 초기화)</text>
  <line x1="130" y1="160" x2="510" y2="160" stroke="#3f9a6b" marker-end="url(#ah6)"/>
  <text x="320" y="154" text-anchor="middle" font-size="12">④ fastboot/ums로 부트로더 → boot0</text>
  <line x1="130" y1="196" x2="510" y2="196" stroke="#3f9a6b" marker-end="url(#ah6)"/>
  <text x="320" y="190" text-anchor="middle" font-size="12">⑤ 디스크 이미지 → user area (GPT)</text>
  <line x1="130" y1="232" x2="510" y2="232" stroke="#e08a3c" marker-end="url(#ah6)"/>
  <text x="320" y="226" text-anchor="middle" font-size="12">⑥ mmc partconf: boot0에서 부팅</text>
  <line x1="130" y1="268" x2="510" y2="268" stroke="#d0564a" marker-end="url(#ah6)"/>
  <text x="320" y="262" text-anchor="middle" font-size="12">⑦ (선택) 키 해시 fuse, 보안 잠금</text>
  <text x="320" y="300" text-anchor="middle" font-size="12">⑧ 보드: 모드 핀 → internal, 재부팅</text>
</svg>
```
그림 8 — 공장 플래싱 시퀀스(일반화, 기억 기반). ①은 ROM, ②~⑥은 USB로 받은 U-Boot가 한다. ⑥은 이 노트 예제 9에서 한 EXT_CSD[179] 쓰기이고, ⑦은 되돌릴 수 없으므로 마지막에 검증 후 한다.

---

## 10. 이중화 — A/B 부트로더 사본, 보조 이미지

부트로더 업데이트는 **rootfs 업데이트보다 위험하다**. rootfs는 A/B 파티션과 U-Boot의 bootcount로 되돌릴 수 있지만(L4), 부트로더가 깨지면 그걸 되돌릴 코드가 없다. 그래서 첫 단계부터 두 벌을 두는 설계를 쓴다.

| 방법 | 원리 | 업데이트 절차 |
|---|---|---|
| **eMMC boot0/boot1 A/B** | 두 boot 파티션에 각각 부트로더, EXT_CSD[179]로 활성 쪽 선택 | 비활성 쪽에 쓰기 → 검증(읽어서 해시 비교) → `bootpart enable`로 전환 (예제 9). 전환 1바이트 쓰기는 사실상 원자적 |
| **ROM의 보조 이미지(secondary image)** | ROM이 primary 실패 시 정해진 다른 오프셋을 본다 (예: i.MX의 secondary image table, 기억 기반) | primary와 secondary를 번갈아 업데이트 |
| **NAND 다중 사본** | ROM이 block 0~N을 순서대로 시도 | 한 번에 한 사본만 지우고 쓰기 |
| **SPI NOR 2벌 + 외부 선택** | 두 칩 또는 두 영역, GPIO나 FPGA가 선택 | 고신뢰 장비(국방·항공)에서 흔함 |
| **TI primary/backup 부트 모드** | 모드 핀으로 다른 장치를 백업으로 지정 | 백업 장치(예: SPI NOR)에 골든 이미지 고정 |
| **U-Boot 환경 이중화** | `CONFIG_ENV_OFFSET_REDUND`로 환경 두 벌, 플래그로 최신 쪽 선택 | 쓸 때마다 번갈아 쓰기 (예제 1의 `bad CRC` 메시지가 환경 CRC 검사) |

Don에게 익숙한 원칙 그대로다: **쓰는 중에는 항상 다른 한 벌이 온전해야 하고, 전환은 마지막에 원자적으로**. 추가로 **"골든(공장) 이미지는 절대 덮어쓰지 않는다"** 를 두면, 최악의 경우에도 USB 복구 없이 살아난다.

---

## 11. 커널 안에서는 무슨 일이 일어나나

이 노트에서 유저 공간 도구가 한 일을 커널 경로로 따라가면 이렇다(함수 이름은 Linux 6.18.55 소스에서 확인).

- **eMMC 하드웨어 파티션 생성**: 카드 초기화 때 `mmc_read_ext_csd()` → `mmc_decode_ext_csd()`가 `EXT_CSD_BOOT_MULT`를 읽어 `mmc_part_add(..., "boot%d", ..., MMC_BLK_DATA_AREA_BOOT)`로 boot0/boot1을 등록한다(`drivers/mmc/core/mmc.c`). 블록 드라이버가 `mmc_blk_alloc_parts()` → `mmc_blk_alloc_part()`로 `mmcblk0bootN` gendisk를 만들고, boot 영역은 기본 `force_ro`로 연다(`drivers/mmc/core/block.c`의 `force_ro_show/store`, `ro_lock_until_next_power_on` 속성).
- **boot0 읽기·쓰기**: 요청이 `mmcblk0boot0`로 오면 블록 드라이버가 먼저 **CMD6로 PARTITION_ACCESS를 1로 바꾼 뒤**(`mmc_blk_part_switch` 계열) 일반 블록 명령을 보낸다. 즉 boot 파티션은 "주소 공간이 따로 있는 디스크"가 아니라 **같은 버스에서 레지스터로 창을 바꾸는 영역**이다 — Don이 아는 bank switching과 같다.
- **`MMC_IOC_CMD`**: `mmc_blk_ioctl()` → (블록 큐에 드라이버 전용 요청으로 넣었다가) `__mmc_blk_ioctl_cmd()`가 유저가 준 opcode/arg/flags를 그대로 `mmc_wait_for_req()`로 보낸다. CMD6이 EXT_CSD[179]를 바꾸면 커널은 캐시(`card->ext_csd.part_config`, `part_curr`)를 갱신한다 — 소스에 "Make sure the cache of the PARTITION_CONFIG register and PARTITION_ACCESS bits is updated" 주석이 있다. 그래서 예제 9에서 전환 후에도 user area 읽기가 정상이었다.
- **SDHCI 호스트**: 우리가 빌드한 `sdhci_pci_probe()`가 PCI BAR를 매핑하고 `sdhci_add_host()`(`drivers/mmc/host/sdhci.c`, `EXPORT_SYMBOL_GPL`)로 mmc 코어에 호스트를 등록 → 코어가 카드를 열거 → 위 경로. G·H 모듈의 "버스 드라이버 probe → 서브시스템 등록" 패턴의 실례다.
- **GPT 파싱**: 디스크가 등록되면 `block/partitions/efi.c`의 `efi_partition()` → `find_valid_gpt()` → `is_gpt_valid()`가 헤더·엔트리 CRC를 검사하고, 주 테이블이 깨지면 백업을 찾는다(예제 12와 같은 로직). 결과가 `vda1`, `vda2`.
- **MTD/NAND**: `nandwrite`는 `/dev/mtd2`에 `write()`와 `ioctl(MEMGETBADBLOCK)`을 쓴다 → `mtdchar_write()` / `mtd_block_isbad()` → raw NAND 코어의 `nand_block_isbad()` → BBT 또는 OOB의 BBM 검사(`nand_isbad_bbt()`, `nand_isbad_bbm()`). ECC 정정 횟수는 `mtd->ecc_stats`에 쌓여 sysfs `corrected_bits`, `ecc_failures`로 보인다(`drivers/mtd/mtdcore.c`).

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="12" y="20" font-size="12">유저</text>
  <rect x="60" y="8" width="180" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="28" text-anchor="middle" font-size="12">dd of=/dev/mmcblk0boot0</text>
  <rect x="250" y="8" width="180" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="28" text-anchor="middle" font-size="12">ioctl(MMC_IOC_CMD)</text>
  <rect x="440" y="8" width="230" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="555" y="28" text-anchor="middle" font-size="12">nandwrite /dev/mtd2</text>
  <line x1="10" y1="50" x2="670" y2="50" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="12" y="66" font-size="12">커널</text>
  <rect x="60" y="76" width="370" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="245" y="94" text-anchor="middle" font-size="12">mmc_blk: force_ro 검사, 파티션 전환</text>
  <text x="245" y="110" text-anchor="middle" font-size="12">__mmc_blk_ioctl_cmd, part_config 캐시 갱신</text>
  <rect x="440" y="76" width="230" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="555" y="94" text-anchor="middle" font-size="12">mtdchar_write</text>
  <text x="555" y="110" text-anchor="middle" font-size="12">mtd_block_isbad</text>
  <rect x="60" y="136" width="370" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="245" y="154" text-anchor="middle" font-size="12">mmc core: CMD6 SWITCH, CMD8, 블록 명령</text>
  <text x="245" y="170" text-anchor="middle" font-size="12">mmc_decode_ext_csd → mmc_part_add</text>
  <rect x="440" y="136" width="230" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="555" y="154" text-anchor="middle" font-size="12">raw NAND core</text>
  <text x="555" y="170" text-anchor="middle" font-size="12">nand_block_isbad, ECC</text>
  <rect x="60" y="196" width="370" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="245" y="214" text-anchor="middle" font-size="12">호스트 드라이버: sdhci_pci_probe</text>
  <text x="245" y="230" text-anchor="middle" font-size="12">→ sdhci_add_host (예제 8의 모듈)</text>
  <rect x="440" y="196" width="230" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="555" y="214" text-anchor="middle" font-size="12">컨트롤러 드라이버</text>
  <text x="555" y="230" text-anchor="middle" font-size="12">(예제 7은 nandsim)</text>
  <line x1="150" y1="38" x2="150" y2="74" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="340" y1="38" x2="340" y2="74" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="555" y1="38" x2="555" y2="74" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="245" y1="116" x2="245" y2="134" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="555" y1="116" x2="555" y2="134" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="245" y1="176" x2="245" y2="194" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="555" y1="176" x2="555" y2="194" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="10" y1="250" x2="670" y2="250" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="12" y="266" font-size="12">HW</text>
  <rect x="60" y="276" width="370" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="245" y="300" text-anchor="middle" font-size="12">eMMC: EXT_CSD[179], boot0/1, RPMB, user</text>
  <rect x="440" y="276" width="230" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="555" y="300" text-anchor="middle" font-size="12">NAND: page + OOB, BBM</text>
  <line x1="245" y1="236" x2="245" y2="274" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="555" y1="236" x2="555" y2="274" stroke="currentColor" marker-end="url(#ah7)"/>
</svg>
```
그림 9 — 이 노트의 유저 공간 실험이 지나간 커널 경로. 왼쪽이 MMC 서브시스템(블록 드라이버 → 코어 → 호스트 드라이버), 오른쪽이 MTD/raw NAND.

드라이버를 쓸 때 다시 만나는 지점:

- **호스트 컨트롤러 드라이버**(SDHCI 변형, NAND 컨트롤러, FlexSPI/OSPI) 포팅·디버그는 BSP 엔지니어의 단골 일이다 → G(드라이버 모델), H(버스 서브시스템), J1(MMC/블록), J2(MTD·UBI).
- **Device Tree**: 실제 보드에서 eMMC 호스트는 `mmc@...` 노드에 `bus-width = <8>`, `non-removable`, `mmc-hs200-1_8v` 같은 속성으로, NAND는 `nand-ecc-strength`, `nand-ecc-step-size`, `partitions` 하위 노드로 기술된다 → D 모듈.
- **커널의 부트 파티션 쓰기 정책**(`force_ro`)과 U-Boot의 `mmc partconf`가 같은 레지스터를 두고 서로 다른 층에서 일한다 — 업데이트 에이전트(L4)를 짤 때 이 둘을 같이 생각해야 한다.

---

## 12. Embedded 실무에서 다시 보기

### 12.1 센서 키트 보드의 "시리얼이 조용하다" 디버그 순서

1. **모드 핀**: 회로도의 BOOT_MODE 저항/스위치 확인. 개발 보드에서 SD로 두었는지 eMMC로 두었는지.
2. **USB에 무엇이 뜨나**: 호스트에서 `lsusb`. i.MX SDP, Rockchip maskrom 장치가 보이면 **ROM은 살아 있고 미디어에서 이미지를 못 찾은 것**이다. 아무것도 안 보이면 전원·클럭·리셋 하드웨어 문제 쪽(P1).
3. **이미지 위치 확인**: PC에서 SD를 꽂고 `hexdump -C -s 32K -n 64 /dev/sdX`(예제 11처럼). magic이 기대 오프셋에 있나.
4. **파티션이 덮지 않았나**: `sgdisk -p`로 첫 파티션 시작이 부트로더 끝보다 뒤인지(예제 10).
5. **eMMC라면**: 부트로더가 boot0에 있는데 BOOT_PARTITION_ENABLE이 user(7)이거나 0인지(예제 9). U-Boot 콘솔이 있으면 `mmc partconf 0`.

### 12.2 eMMC 부트로더 A/B 업데이트 스크립트의 뼈대

예제 9의 도구와 sysfs를 엮으면 이런 모양이 된다(실제 장비에서는 mmc-utils를 쓰고 쓰기 후 검증을 꼭 넣는다; 아래는 설명용으로 실행하지 않음).

```sh
#!/bin/sh
# 활성 boot 파티션 반대쪽에 쓰고 검증 후 전환 (설명용 뼈대, 이 노트에서 실행하지 않음)
CUR=$(mmc extcsd read /dev/mmcblk0 | sed -n 's/.*Boot Partition \([12]\) enabled.*/\1/p')
NEW=$([ "$CUR" = 1 ] && echo 2 || echo 1); DEV=/dev/mmcblk0boot$((NEW-1))
echo 0 > /sys/block/${DEV#/dev/}/force_ro
dd if=flash.bin of=$DEV bs=1k conv=fsync
cmp -n $(stat -c %s flash.bin) flash.bin $DEV || exit 1      # 읽어서 검증
echo 1 > /sys/block/${DEV#/dev/}/force_ro
mmc bootpart enable $NEW 1 /dev/mmcblk0                      # 원자적 전환
```

`mmc extcsd read` 출력 문자열 형식은 mmc-utils 버전에 따라 다르므로 실제로는 바이트 179를 직접 파싱하는 편이 안전하다(예제 9의 `c2_mmcboot`처럼).

### 12.3 Yocto/Buildroot 이미지와의 연결

`wic`(Yocto)의 `.wks`에서 `part u-boot --source rawcopy --sourceparams="file=imx-boot" --no-table --align 32`나, Buildroot `genimage.cfg`의 `offset = 32K` 같은 줄이 바로 예제 10의 `dd ... seek=32`다(형식은 기억 기반, K3·K4에서 실습). 이미지 생성 설정을 읽을 때 "이 숫자가 SoC ROM의 오프셋과 맞나"를 확인하는 습관이 bring-up 시간을 아낀다.

### 12.4 raw NAND를 쓰는 장비(구형 산업용·드론 FC 보드)

부트로더 영역은 ROM 형식으로(벤더 도구), 그 뒤는 UBI로. 커널 파티션 레이아웃은 DT의 `partitions` 또는 `mtdparts=` 커널 인자로 정한다. 출하 테스트에서 `/sys/class/mtd/mtdN/bad_blocks`와 `corrected_bits`를 로깅하면(예제 7) 불량 칩 로트를 일찍 잡는다 — Don의 SSD 품질 업무와 똑같은 감각이다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 부트로더를 SoC가 찾는 오프셋과 다른 곳에 씀 (32K vs 33K) | 콘솔 무반응, USB 복구 장치로 열거됨 | ROM이 magic을 못 찾음 | SoC RM의 오프셋 확인, `hexdump -s`로 검증 |
| 첫 파티션을 1 MiB에서 시작 | 처음엔 부팅되다 파티션 리사이즈·재생성 후 벽돌 | 파티션이 raw 부트로더 영역을 덮음 | 첫 파티션을 4~16 MiB에서 시작 |
| GPT + 8 KiB(또는 1 KiB) 부트로더 | 부팅은 되는데 커널·PC가 "GPT 손상, 백업 사용" | 부트로더가 GPT 엔트리를 덮음 | MBR 사용 또는 `sgdisk -j`로 엔트리 이동 |
| eMMC boot0에 썼는데 BOOT_PARTITION_ENABLE이 user | 이전 부트로더가 계속 뜸 / 안 뜸 | EXT_CSD[179] 미설정 | `mmc bootpart enable 1 1` 또는 U-Boot `mmc partconf` |
| boot0에 `dd` 했는데 "Operation not permitted" | 쓰기 실패 | 커널 기본 `force_ro=1` | `echo 0 > .../force_ro` 후 쓰고 다시 1 |
| SPL이 SRAM보다 큼 | ROM이 조용히 거부, 복구 모드 | 디버그 옵션·DDR 펌웨어로 크기 증가 | `size`/맵 파일로 확인, 기능 축소, 압축 |
| NAND 첫 단계를 Linux ECC 레이아웃으로 씀 | NAND 부팅만 안 됨 | ROM ECC 형식과 불일치 | 벤더 도구(kobs-ng 등)·U-Boot의 ROM 호환 쓰기 사용 |
| 16 MiB 초과 NOR, 소프트 리셋 후 부팅 실패 | 콜드 부팅은 되고 `reboot`만 실패 | NOR가 4-byte 모드로 남음 | NOR 리셋 연결, 커널이 종료 시 3-byte 복귀 |
| 보안 퓨즈를 서명 검증 전에 태움 | 영구 벽돌 | 키 해시·서명 불일치 | open 상태에서 인증 이벤트 0개 확인 후 잠금 |
| NOR 이미지 패딩을 0x00으로 | 플래싱이 느리고 마모 증가 | 지운 상태(0xFF)와 다른 값을 program | 패딩 0xFF |

---

## 14. 면접에서 이렇게 말한다

**Q.** "Walk me through what the SoC does from power-on until the first bootloader runs."

**A.** 리셋이 풀리면 CPU가 칩 안의 마스크 ROM에서 시작한다. ROM은 모드 핀과 eFuse로 부트 장치를 정하고, 그 장치를 최소 드라이버로 열어 정해진 오프셋이나 eMMC boot 파티션에서 이미지 헤더를 찾는다. 헤더의 크기·로드 주소대로 첫 단계를 온칩 SRAM에 복사하고, 보안 퓨즈가 켜져 있으면 서명을 검증한 뒤 점프한다. 실패하면 보조 사본, 다음 장치, 마지막엔 USB 복구 모드로 떨어진다. DRAM은 아직 없으니 첫 단계는 SRAM 크기에 맞아야 하고, 그 단계가 DDR을 초기화한다.

> On reset the core starts executing the on-chip mask ROM. The ROM samples the boot-mode pins and eFuses to pick a boot device, brings that device up with a minimal driver, and looks for an image header at a fixed offset or in the eMMC boot partition. It copies the first stage into on-chip SRAM — DRAM isn't initialized yet — verifies its signature if secure boot is fused, and jumps to it. If that fails it tries a secondary copy, then the next device, and finally falls into a USB recovery mode. That first stage, SPL or TF-A BL2, is what trains DDR and loads the full bootloader.

**꼬리 질문**: "Why can't the ROM just load U-Boot directly into DRAM?" → DDR 타이밍·트레이닝은 보드마다 다르고 ROM은 그걸 모른다. 그래서 SRAM에 들어가는 작은 단계가 필요하다.

**Q.** "How does the SoC decide which boot device to use, and what's the role of eFuses?"

**A.** 개발 단계에서는 스트랩 핀(DIP 스위치)으로 고르고, 양산에서는 eFuse에 부트 장치와 버스 설정을 굽고 "핀 무시" 퓨즈를 태워 현장에서 바뀌지 않게 한다. ROM에는 고정 fallback 순서가 있고 마지막은 USB serial download다. eFuse에는 secure boot용 공개키 해시도 들어가서 ROM이 root of trust가 된다.

> During development we select the boot source with strap pins; in production we program the boot configuration into eFuses and blow a fuse that tells the ROM to ignore the pins, so the field configuration can't be changed. The ROM still has a fixed fallback order ending in USB serial download. The same fuse bank holds the hash of the signing key, which is what makes the ROM the root of trust.

**Q.** "What are eMMC boot partitions and why would you put the bootloader there instead of the user area?"

**A.** eMMC에는 user area와 별개로 boot0/boot1 하드웨어 파티션(보통 4 MiB씩)과 RPMB가 있다. ROM이 boot operation으로 바로 읽을 수 있고, 파티션 테이블이나 파일시스템 작업으로 실수로 덮일 일이 없으며, 두 개라서 A/B로 쓸 수 있다. 어느 쪽에서 부팅할지는 EXT_CSD[179] PARTITION_CONFIG의 BOOT_PARTITION_ENABLE이 정한다. Linux는 이걸 `mmcblk0boot0/1`로 보여 주고 기본 읽기 전용으로 연다.

> eMMC exposes two hardware boot partitions, typically 4 MB each, plus RPMB and the user area. The ROM can stream the bootloader straight out of the active boot partition, repartitioning the user area can't clobber it, and having two lets you do A/B bootloader updates. Which one boots is the BOOT_PARTITION_ENABLE field of EXT_CSD byte 179; you flip it with `mmc bootpart enable` or U-Boot's `mmc partconf`. Linux shows them as mmcblk0boot0/1 and keeps them read-only via force_ro by default.

**꼬리 질문**: "PARTITION_ACCESS와 BOOT_PARTITION_ENABLE의 차이는?" → 전자는 지금 명령이 어느 영역에 가는지(커널이 수시로 바꿈), 후자는 다음 부팅 때 ROM에 줄 영역.

**Q.** "Your new board prints nothing on the serial console. How do you debug it?"

**A.** ROM 단계인지 그 다음인지부터 가른다. 호스트에 USB 복구 장치가 뜨면 ROM은 살아 있고 이미지를 못 찾은 것이니 모드 핀, 이미지 오프셋, eMMC 부트 파티션 설정을 확인한다. 아무것도 안 뜨면 전원 시퀀스·리셋·클럭 같은 하드웨어를 본다. SPL까지 갔는지 보려면 SPL에서 UART를 가장 먼저 켜게 하고, 필요하면 JTAG으로 PC 값을 읽는다.

> First I split ROM versus post-ROM. If a USB recovery device enumerates on the host, the ROM is alive but didn't find a valid image, so I check boot-mode pins, the image offset with a hexdump, and the eMMC boot-partition config. If nothing enumerates, I look at hardware: rails, reset, clocks. To see whether SPL started, I make SPL bring up the UART as early as possible and, if needed, halt the core over JTAG to read the PC.

**Q.** "How would you recover a bricked board, and how does factory flashing work?"

**A.** 모드 핀을 serial download로 두거나 미디어가 비어 있으면 ROM이 USB 장치로 뜬다. 벤더 도구(NXP uuu, rkdeveloptool, dfu-util, sunxi-fel)로 U-Boot를 RAM에 올리고, 그 U-Boot의 fastboot/ums로 boot0에 부트로더, user area에 GPT 이미지를 쓰고, partconf로 부트 파티션을 켠다. 보안 퓨즈는 검증 후 마지막에.

> Force the SoC into its ROM USB recovery mode — strap or empty media — then push U-Boot into RAM with the vendor tool, like uuu, rkdeveloptool, dfu-util or sunxi-fel. From that U-Boot I write the bootloader to boot0 and the disk image to the user area over fastboot or UMS, enable the boot partition, and only after validation fuse the keys. The factory flow is the same thing scripted.

**Q.** "Raw NAND versus eMMC for booting — what's different?"

**A.** raw NAND는 배드 블록과 ECC를 호스트가 관리해야 한다. ROM은 자기 고정 ECC 형식으로 block 0 근처의 여러 사본을 읽으므로 첫 단계는 ROM 형식으로 써야 하고, 그 뒤는 MTD + UBI가 배드 블록·웨어 레벨링을 맡는다. eMMC는 내부 컨트롤러가 그걸 다 숨겨서 블록 장치처럼 쓰면 된다. 저는 SSD에서 FTL·ECC를 직접 다뤄 봐서, raw NAND에서 누가 무엇을 책임지는지 경계를 명확히 할 수 있다.

> With raw NAND the host owns bad-block handling and ECC. The ROM reads multiple copies near block 0 with its own fixed ECC layout, so the first stage must be written in the ROM's format, and everything after that sits on MTD plus UBI for bad blocks and wear leveling. eMMC hides all of that behind its own controller, so it's just a block device. Coming from SSD firmware, I'm used to exactly that boundary — what the FTL hides versus what the host must manage.

**Q.** "How do you make bootloader updates safe in the field?"

**A.** 항상 온전한 한 벌을 남기고 전환을 원자적으로 한다. eMMC면 비활성 boot 파티션에 쓰고 읽어서 검증한 뒤 EXT_CSD 1바이트로 전환한다. ROM이 보조 이미지를 지원하면 primary/secondary를 번갈아 쓴다. U-Boot 환경도 redundant로 두고, 가능하면 덮어쓰지 않는 골든 이미지를 둔다.

> Always keep one known-good copy and make the switch atomic. On eMMC I write the inactive boot partition, read it back to verify, then flip the single EXT_CSD byte. If the ROM supports a secondary image, I alternate primary and secondary. The U-Boot environment is redundant too, and ideally there's a golden image that's never overwritten.

**Q.** "Why does the BootROM matter for security?"

**A.** ROM은 수정 불가이고 eFuse의 키 해시로 첫 이미지를 검증하므로 신뢰 사슬의 뿌리다. 반대로 ROM에 버그가 있으면 패치할 수 없어 칩 리비전으로만 고친다. 그래서 퓨즈를 태우기 전 검증 절차와 ROM errata 확인이 중요하다.

> Because it's immutable and verifies the first image against a key hash in eFuses, the ROM is the root of trust for the whole chain. The flip side is that a ROM bug can't be patched — only a silicon respin fixes it — so you check the ROM errata and validate signed images in the open state before you blow the lock fuses.

---

## 15. 직접 해보기

1. **개념**: SD에 부트로더를 32 KiB에 두는 SoC에서 GPT를 쓸 때, 첫 파티션이 1 MiB에서 시작하면 부트로더의 최대 크기는? 정답/힌트: 1 MiB − 32 KiB = 992 KiB. 랩의 BL1+FIP(1.76 MiB)는 들어가지 않는다 — 그래서 예제 10은 8 MiB에서 시작했다.
2. **hexdump**: `c2_sd.img`의 GPT 엔트리 #2(바이트 0x480)를 hexdump해서 시작 LBA, 끝 LBA, 이름을 손으로 해독하라. 정답/힌트: 시작 0x1c000, 끝 0x3ffde, 이름 UTF-16LE "rootfs" — `part list` 출력과 비교.
3. **코드**: `c2_fip.c`를 고쳐 각 엔트리의 SHA-256을 출력하라(`openssl/sha.h`의 `SHA256()`, `-lcrypto`). `fiptool unpack`한 파일의 `sha256sum`과 같은지 확인. 정답/힌트: nt-fw는 `5d744397d874…`(VM에서 `sha256sum nt-fw.bin`으로 확인한 값).
4. **eMMC**: 예제 9 스크립트를 고쳐 boot1에 다른 SPL을 쓰고 `enable 1 1`로 되돌린 뒤, 호스트에서 `c2_emmc.img`의 4 MiB 오프셋을 hexdump하라. 정답/힌트: boot1 = 파일 오프셋 0x400000.
5. **NAND**: `nandsim`의 `badblocks=0,5`로 block 0까지 배드로 만들고 mtd1에 쓰면? 정답/힌트: nandwrite는 건너뛰지만, 실제 ROM은 block 0을 좋다고 가정하는 경우가 많아 그런 칩은 부트 미디어로 불합격이다 — 그래서 block 0 보장이 데이터시트에 있다.
6. **U-Boot**: 예제 14의 expect에 `part start virtio 0 1 x; printenv x`와 `part size virtio 0 2 y; printenv y`를 넣어 보라. 그리고 `gpt verify virtio 0`이 왜 "Unknown command"가 될지 예측하라. 정답/힌트: 시작은 16진 LBA 4000(= 8 MiB). 랩 U-Boot `.config`는 `# CONFIG_CMD_GPT is not set`이라 `gpt` 명령이 없다(`part`는 `CONFIG_CMD_PART=y`).
7. **관찰**: 예제 8의 게스트에서 `ls /sys/block/mmcblk0boot0/`로 `ro_lock_until_next_power_on`이 보이는지 확인하고, `drivers/mmc/core/block.c`의 `mmc_disk_attrs_is_visible()`과 `power_ro_lock_store()`를 읽어 force_ro와의 차이를 설명하라. 정답/힌트: force_ro는 커널의 소프트 보호, ro_lock은 eMMC의 boot 영역 write protect(EXT_CSD BOOT_WP)를 거는 하드웨어 보호이고, 카드가 `boot_ro_lockable`일 때만 속성이 보인다(QEMU eMMC가 지원하는지는 직접 확인할 것).

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| BootROM (mask ROM) | 칩 내장 고정 부트 코드 | 리셋 직후 실행, 수정 불가, 부트 장치 선택과 첫 이미지 로드 |
| boot mode 핀 (strap) | 리셋 때 래치되는 설정 핀 | 개발 중 부트 장치 선택 |
| eFuse / OTP | 한 번만 쓰는 퓨즈 비트 | 양산 부트 설정, 보안 키 해시, 잠금 |
| fallback | 실패 시 다음 후보 시도 | 보조 이미지 → 다음 장치 → USB 복구 |
| OCRAM / SRAM | 온칩 SRAM | DRAM 전 단계가 도는 곳, 크기가 첫 단계 한도 |
| SPL | Secondary Program Loader | U-Boot의 작은 첫 단계, DRAM 초기화 |
| IVT | Image Vector Table | i.MX ROM이 읽는 이미지 헤더 |
| container (AHAB) | i.MX8/93의 이미지 형식 | 서명 블록 포함, 보안 코어가 검증 |
| tiboot3.bin | TI K3 첫 이미지 | X.509 인증서로 감싼 R5 SPL |
| idbloader | Rockchip 첫 이미지 | sector 64의 ID block + DDR init + SPL |
| FIP | Firmware Image Package | TF-A의 BL2/BL31/BL33 묶음, ToC + UUID 엔트리 |
| boot0 / boot1 | eMMC 하드웨어 부트 파티션 | ROM이 boot operation으로 읽는 영역 |
| RPMB | Replay Protected Memory Block | HMAC 인증 쓰기 영역, secure storage |
| EXT_CSD | eMMC 확장 CSD 레지스터 | 512 B 설정·상태, [179]가 부트 파티션 설정 |
| PARTITION_CONFIG | EXT_CSD[179] | BOOT_ACK, BOOT_PARTITION_ENABLE, PARTITION_ACCESS |
| GPT | GUID Partition Table | LBA1 헤더 + 엔트리 배열 + 백업, CRC32 |
| protective MBR | GPT 디스크의 LBA0 | 타입 0xEE로 전체를 덮어 옛 도구 보호 |
| OOB | Out-Of-Band (spare) | NAND page 뒤 영역, BBM·ECC 저장 |
| BBM | Bad Block Marker | OOB의 배드 블록 표시 |
| MTD | Memory Technology Device | 커널의 raw flash 추상화 (`/dev/mtdN`) |
| SDP | Serial Download Protocol | NXP ROM의 USB 복구 프로토콜 (uuu) |
| DFU | Device Firmware Upgrade | USB 표준 펌웨어 다운로드 클래스 |
| maskrom / FEL | Rockchip / Allwinner 복구 모드 | ROM의 USB 다운로드 모드 |
| Boot LU | UFS 부팅용 논리 유닛 | A/B 두 개, bBootLunEn으로 선택 |

---

## 17. 요약 & 체크리스트

BootROM은 칩 안의 고칠 수 없는 첫 코드로, 모드 핀과 eFuse로 부트 장치를 고르고, 그 장치의 정해진 위치(raw 오프셋, eMMC boot 파티션, 또는 FAT 파일)에서 벤더 형식의 헤더를 찾아 첫 단계를 온칩 SRAM에 올리고, 보안 퓨즈가 있으면 서명을 검증한 뒤 점프한다. 실패하면 보조 사본 → 다음 장치 → USB 복구 모드로 떨어진다. 부트 미디어마다 레이아웃 규칙이 다르다: SPI NOR는 작은 고정 오프셋, raw NAND는 ROM 형식 ECC와 다중 사본, eMMC는 boot0/boot1 + EXT_CSD[179], SD는 GPT/MBR과 겹치지 않는 raw 오프셋. 이 노트에서는 랩의 `flash.bin`을 바이트 단위로 해부하고, GPT + raw 영역 + FAT + ext4 이미지를 만들어 U-Boot → 커널 → ext4 rootfs까지 부팅했으며, QEMU eMMC로 boot 파티션과 EXT_CSD를, nandsim으로 배드 블록과 ECC를 직접 확인했다.

- [ ] 전원 ON부터 첫 단계 점프까지 BootROM의 일을 순서대로 설명할 수 있다
- [ ] 모드 핀, eFuse, 고정 fallback 순서의 역할 차이를 말할 수 있다
- [ ] 왜 첫 단계가 SRAM에 들어가야 하는지(DRAM 미초기화) 설명할 수 있다
- [ ] `flash.bin`의 BL1/FIP 위치와 FIP ToC 구조를 hexdump로 찾을 수 있다
- [ ] GPT의 바이트 배치(LBA0~33, 백업)와 CRC 규칙을 설명하고 C로 검증할 수 있다
- [ ] SD raw 오프셋과 GPT 충돌 문제를 설명하고 해결책(파티션 시작, MBR, `sgdisk -j`)을 댈 수 있다
- [ ] root 없이 GPT + raw 부트로더 + FAT + ext4 디스크 이미지를 만들 수 있다
- [ ] eMMC boot0/boot1/RPMB/user와 EXT_CSD[179] 비트를 설명하고 `bootpart enable`을 할 수 있다
- [ ] 커널이 boot 파티션을 `force_ro`로 여는 이유와 쓰는 절차를 안다
- [ ] raw NAND에서 배드 블록 skip 규칙과 ECC 정정이 어디서 일어나는지 설명할 수 있다
- [ ] 벤더별 USB 복구 모드와 도구(uuu, dfu-util, rkdeveloptool, sunxi-fel, rpiboot)를 짝지을 수 있다
- [ ] 부트로더 A/B 이중화 설계와 원자적 전환을 설명할 수 있다

---

## 참고 자료

- TF-A 문서 — Firmware Design (BL1/BL2/BL31, FIP), Tools: fiptool: https://trustedfirmware-a.readthedocs.io/
- TF-A 소스(랩): `/var/lab/src/tfa/plat/qemu/qemu/include/platform_def.h`, `plat/qemu/qemu/platform.mk`, `tools/fiptool/`
- U-Boot 문서 — QEMU ARM 보드, bootstd, `mmc` 명령: https://docs.u-boot.org/
- U-Boot 소스(랩): `/var/lab/src/u-boot/cmd/mmc.c` (`partconf`, `bootbus`, `bootpart-resize`), `env/Kconfig` (`ENV_OFFSET_REDUND`)
- Linux 커널 문서 — SD and MMC Device Partitions: https://docs.kernel.org/driver-api/mmc/mmc-dev-parts.html
- Linux 커널 문서 — MTD NAND ECC: https://docs.kernel.org/driver-api/mtd/nand_ecc.html
- Linux 소스(랩, 6.18.55): `drivers/mmc/core/mmc.c` (`mmc_decode_ext_csd`), `drivers/mmc/core/block.c` (`__mmc_blk_ioctl_cmd`, `force_ro`), `block/partitions/efi.c` (`efi_partition`), `drivers/mtd/nand/raw/nand_base.c`, `drivers/mtd/nand/raw/nandsim.c` — 온라인: https://elixir.bootlin.com/linux/v6.18.55/source
- UAPI 헤더: `/usr/include/linux/mmc/ioctl.h` (`struct mmc_ioc_cmd`, `MMC_IOC_CMD`)
- JEDEC JESD84-B51, Embedded MultiMediaCard (e•MMC) Electrical Standard 5.1 — EXT_CSD 정의 (JEDEC 등록 후 열람)
- UEFI Specification, "GUID Partition Table (GPT) Disk Layout" 장: https://uefi.org/specifications
- Bootlin Embedded Linux 교육 슬라이드 (부트로더 장): https://bootlin.com/doc/training/embedded-linux/
- NXP mfgtools (uuu): https://github.com/nxp-imx/mfgtools
- Raspberry Pi usbboot (rpiboot): https://github.com/raspberrypi/usbboot
- linux-sunxi 위키 — BROM, FEL: https://linux-sunxi.org/BROM , https://linux-sunxi.org/FEL
- Rockchip 오픈소스 위키 — Boot option: https://opensource.rock-chips.com/wiki_Boot_option
- 각 SoC Reference Manual의 "System Boot"(NXP i.MX8M), "Initialization"(TI AM62x TRM) 장 — 벤더 사이트에서 등록 후 다운로드
