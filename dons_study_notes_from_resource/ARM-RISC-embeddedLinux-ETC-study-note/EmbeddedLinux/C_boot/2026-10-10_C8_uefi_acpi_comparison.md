# C8. x86/UEFI와 비교 — UEFI · GRUB · ACPI vs Device Tree, 그리고 arm64 서버·Jetson 세계

> **이 노트를 다 읽으면**: 레거시 BIOS · UEFI · U-Boot 부팅 사슬을 한 장의 그림으로 비교할 수 있다 · arm64 `Image`가 왜 동시에 PE/COFF EFI 앱인지 헤더 바이트로 보여 줄 수 있다 · 같은 커널을 U-Boot `booti`/`bootefi`, EDK2(ACPI), EDK2(DT)로 부팅해 커널 로그·`/sys/firmware` 차이를 설명할 수 있다 · ACPI의 `_HID`와 DT의 `compatible`이 드라이버 매칭에서 어떻게 대응하는지, SystemReady/EBBR이 무엇을 요구하는지 말할 수 있다
> **왜 필요한가**: NVIDIA(Jetson Orin은 UEFI + DT), arm64 서버(Grace·Graviton·Ampere는 UEFI + ACPI), 로보틱스 회사 면접에서 "UEFI랑 U-Boot 차이?", "ACPI vs Device Tree?", "Secure Boot은 어떻게 동작?"이 나온다. 입사 후에는 "이 보드는 왜 `/proc/device-tree`가 없지?"를 바로 판단해야 한다
> **Don 기준 난이도**: 리셋 벡터·DDR 초기화·부트로더·이미지 서명 검증은 SSD FW에서 이미 해 봤다 / UEFI의 단계 구조(PI), 프로토콜·변수·ESP, ACPI 테이블과 AML, EFI stub의 핸드오버는 새로 배울 부분
> **선행 노트**: C1(부트 전체 지도), C3(U-Boot 사용), C5(TF-A·신뢰 부팅), C6(커널 시작) — D 모듈(Device Tree)과 짝으로 읽으면 좋다

---

## 0. 큰 그림 — 이게 왜 필요한가

C1~C7에서 본 임베디드 arm64 부팅은 이랬다: BootROM → SPL/TF-A → U-Boot → `booti`로 `Image` + DTB를 메모리에 놓고 점프 → 커널. Don의 SSD 펌웨어와 크게 다르지 않다. **부트로더가 하드웨어 설명(DTB)을 손에 쥐여 주고 커널로 점프**한다.

그런데 PC와 서버는 전혀 다른 세계에서 자랐다. 수십 개 회사가 만든 메인보드에 **같은 OS 설치 USB 하나**가 그대로 부팅되어야 한다. 그래서 PC 세계는 "펌웨어가 표준 인터페이스(UEFI)를 제공하고, 하드웨어 설명도 펌웨어가 표준 형식(ACPI)으로 들고 있다"는 방식을 택했다. 보드마다 커널·DTB를 맞춰 빌드하는 임베디드 방식과 정반대다.

최근에는 두 세계가 섞인다. arm64 서버는 x86처럼 UEFI + ACPI로 부팅하고, U-Boot는 UEFI API를 흉내 내서(EBBR) 배포판 설치 이미지를 그대로 부팅하며, NVIDIA Jetson Orin은 UEFI 펌웨어로 부팅하지만 하드웨어 설명은 DT를 쓴다. 지금 Don이 쓰는 Lima VM 자체도 **UEFI + ACPI로 부팅한 arm64 머신**이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="18" text-anchor="middle" font-size="14" font-weight="bold">네 가지 부팅 사슬 (왼쪽 → 오른쪽 = 시간)</text>
  <rect x="8" y="34" width="112" height="48" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="64" y="54" text-anchor="middle" font-size="12">x86 레거시</text>
  <text x="64" y="72" text-anchor="middle" font-size="12">HW 설명: ACPI</text>
  <rect x="128" y="34" width="122" height="48" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="189" y="54" text-anchor="middle" font-size="12">BIOS ROM</text>
  <text x="189" y="72" text-anchor="middle" font-size="12">POST · INT 13h</text>
  <rect x="266" y="34" width="122" height="48" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="327" y="54" text-anchor="middle" font-size="12">MBR 512B</text>
  <text x="327" y="72" text-anchor="middle" font-size="12">0x7C00 실행</text>
  <rect x="404" y="34" width="122" height="48" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="465" y="54" text-anchor="middle" font-size="12">GRUB</text>
  <text x="465" y="72" text-anchor="middle" font-size="12">core.img</text>
  <rect x="542" y="34" width="122" height="48" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="603" y="54" text-anchor="middle" font-size="12">Linux</text>
  <text x="603" y="72" text-anchor="middle" font-size="12">bzImage</text>
  <rect x="8" y="100" width="112" height="48" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="64" y="120" text-anchor="middle" font-size="12">UEFI (x86·서버)</text>
  <text x="64" y="138" text-anchor="middle" font-size="12">HW 설명: ACPI</text>
  <rect x="128" y="100" width="122" height="48" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="189" y="120" text-anchor="middle" font-size="12">UEFI 펌웨어</text>
  <text x="189" y="138" text-anchor="middle" font-size="12">SEC·PEI·DXE</text>
  <rect x="266" y="100" width="122" height="48" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="327" y="120" text-anchor="middle" font-size="12">BDS</text>
  <text x="327" y="138" text-anchor="middle" font-size="12">Boot#### 변수</text>
  <rect x="404" y="100" width="122" height="48" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="465" y="120" text-anchor="middle" font-size="12">shim → GRUB</text>
  <text x="465" y="138" text-anchor="middle" font-size="12">ESP의 .efi</text>
  <rect x="542" y="100" width="122" height="48" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="603" y="120" text-anchor="middle" font-size="12">Linux</text>
  <text x="603" y="138" text-anchor="middle" font-size="12">EFI stub</text>
  <rect x="8" y="166" width="112" height="48" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="64" y="186" text-anchor="middle" font-size="12">임베디드 arm64</text>
  <text x="64" y="204" text-anchor="middle" font-size="12">HW 설명: DT</text>
  <rect x="128" y="166" width="122" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="189" y="186" text-anchor="middle" font-size="12">BootROM</text>
  <text x="189" y="204" text-anchor="middle" font-size="12">SPL · TF-A</text>
  <rect x="266" y="166" width="122" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="327" y="186" text-anchor="middle" font-size="12">U-Boot</text>
  <text x="327" y="204" text-anchor="middle" font-size="12">bootcmd</text>
  <rect x="404" y="166" width="122" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="465" y="186" text-anchor="middle" font-size="12">booti</text>
  <text x="465" y="204" text-anchor="middle" font-size="12">Image + DTB</text>
  <rect x="542" y="166" width="122" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="603" y="186" text-anchor="middle" font-size="12">Linux</text>
  <text x="603" y="204" text-anchor="middle" font-size="12">primary_entry</text>
  <rect x="8" y="232" width="112" height="48" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="64" y="252" text-anchor="middle" font-size="12">U-Boot (EBBR)</text>
  <text x="64" y="270" text-anchor="middle" font-size="12">HW 설명: DT</text>
  <rect x="128" y="232" width="122" height="48" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="189" y="252" text-anchor="middle" font-size="12">BootROM</text>
  <text x="189" y="270" text-anchor="middle" font-size="12">SPL · TF-A</text>
  <rect x="266" y="232" width="122" height="48" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="327" y="252" text-anchor="middle" font-size="12">U-Boot</text>
  <text x="327" y="270" text-anchor="middle" font-size="12">UEFI API 구현</text>
  <rect x="404" y="232" width="122" height="48" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="465" y="252" text-anchor="middle" font-size="12">bootefi</text>
  <text x="465" y="270" text-anchor="middle" font-size="12">bootmgr</text>
  <rect x="542" y="232" width="122" height="48" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="603" y="252" text-anchor="middle" font-size="12">Linux</text>
  <text x="603" y="270" text-anchor="middle" font-size="12">EFI stub</text>
  <line x1="250" y1="58" x2="264" y2="58" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="388" y1="58" x2="402" y2="58" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="526" y1="58" x2="540" y2="58" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="250" y1="124" x2="264" y2="124" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="388" y1="124" x2="402" y2="124" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="526" y1="124" x2="540" y2="124" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="250" y1="190" x2="264" y2="190" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="388" y1="190" x2="402" y2="190" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="526" y1="190" x2="540" y2="190" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="250" y1="256" x2="264" y2="256" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="388" y1="256" x2="402" y2="256" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="526" y1="256" x2="540" y2="256" stroke="currentColor" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — 네 가지 부팅 사슬. 위 두 줄은 PC·서버 세계, 아래 두 줄은 임베디드 세계다. 맨 아래 줄(U-Boot가 UEFI API를 구현)은 두 세계를 잇는 다리로, 커널 입장에서는 둘째 줄과 같은 "EFI stub" 입구로 들어온다.

Don의 경험으로 번역하면 이렇다.

| PC/서버 개념 | Don이 아는 대응물 | 차이 |
|---|---|---|
| UEFI 펌웨어 | SSD 컨트롤러의 ROM 부트로더 + FW 로더 | 표준 API(프로토콜)를 OS 로더에게 **서비스로 제공**한다 |
| UEFI 변수(NVRAM) | SSD의 시스템 영역에 저장한 부트 파라미터 | OS가 런타임에도 읽고 쓴다 (`efibootmgr`) |
| ESP(EFI System Partition) | FW 이미지 슬롯 | 그냥 FAT 파일시스템이다. 파일 경로로 부트 대상을 찾는다 |
| ACPI 테이블 | 레지스터 맵 문서 + 초기화 스크립트를 ROM에 넣어 둔 것 | 커널이 이 "문서"와 바이트코드(AML)를 읽어 장치를 찾는다 |
| Secure Boot | FW 이미지 서명 검증(RSA/ECDSA) | 키 계층(PK/KEK/db)과 폐기 목록(dbx)이 표준화되어 있다 |

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, Ubuntu 26.04, `uname -r` = `7.0.0-38-generic`, Apple Virtualization.framework 위에서 UEFI로 부팅)와 그 안의 공용 QEMU 랩(`/var/lab`, U-Boot v2026.10, Linux 6.18.55, Ubuntu 패키지 EDK2 `AAVMF` 2025.11, QEMU 10.2.1, TCG)에서 실제로 돌린 결과다. 작업 폴더는 VM의 `/var/tmp/C8/`, 소스는 `S=/Users/donh/.elinux_scratch/C8`. 주소·시간·GUID·KASLR 값은 실행마다 다르다.

---

## 1. 레거시 BIOS에서 UEFI로 — 무엇이 바뀌었나

### 1.1 레거시 BIOS — 1981년의 인터페이스

x86 CPU는 리셋되면 물리 주소 `0xFFFFFFF0`(리셋 벡터)에서 실행을 시작한다. 거기 매핑된 SPI NOR 플래시의 BIOS 코드가 **POST**(Power-On Self Test: DRAM·칩셋 초기화, 장치 검사)를 하고, 부팅 디스크의 **첫 섹터 512바이트(MBR)**를 메모리 `0x7C00`에 읽어 16비트 real mode로 점프한다. 디스크 읽기는 BIOS가 제공하는 소프트웨어 인터럽트 `INT 13h`로 한다.

이 구조의 한계는 Don이 보면 바로 보인다.

- **446바이트 부트 코드**: MBR에는 파티션 테이블(64B)과 시그니처(2B)를 빼면 코드가 446바이트밖에 안 들어간다. 그래서 GRUB은 1단계(boot.img) → 파티션 사이 빈틈에 숨긴 core.img로 여러 번 뛰어야 했다.
- **16비트 real mode + INT 호출**: 1 MB 주소 공간, 펌웨어 서비스가 소프트웨어 인터럽트 번호로만 노출된다. 드라이버를 추가할 표준 방법이 없다(Option ROM 정도).
- **MBR 파티션 2 TiB 한계**: 32비트 LBA × 512B.
- **하드웨어 설명 없음**: 그래서 ACPI(1996)가 별도로 생겨 BIOS에 붙었다.

### 1.2 UEFI — "펌웨어가 OS 로더에게 주는 C API"

UEFI(Unified Extensible Firmware Interface)는 Intel의 EFI(Itanium용, 1990년대 말)를 업계 표준으로 만든 것이다. 핵심 발상은 **펌웨어가 C 함수 포인터 테이블(System Table)을 OS 로더에게 넘겨준다**는 것이다.

| 항목 | 레거시 BIOS | UEFI |
|---|---|---|
| CPU 모드 | 16비트 real mode | 64비트 long mode(x86) / AArch64 EL2·EL1 |
| 부트 대상 | 디스크 첫 섹터(MBR) | ESP(FAT) 안의 PE/COFF 실행 파일 `.efi` |
| 부트 선택 | 펌웨어 설정의 디스크 순서 | NVRAM 변수 `BootOrder`, `Boot####` |
| 펌웨어 서비스 | `INT 10h`, `INT 13h` … | Boot Services / Runtime Services 함수 테이블 + GUID로 찾는 프로토콜 |
| 파티션 | MBR (2 TiB 한계) | GPT |
| 드라이버 확장 | Option ROM(16비트) | UEFI 드라이버(.efi, PCIe 카드 ROM도 EFI 드라이버) |
| 서명 검증 | 없음 | Secure Boot (PK/KEK/db/dbx) |
| 하드웨어 설명 | ACPI(별도 규격) | ACPI(+ arm64에서는 DT도 가능) |

UEFI 규격은 **두 개로 나뉜다**는 점을 기억해 두면 면접에서 깊이가 드러난다.

- **UEFI Specification**: 펌웨어가 OS 로더에게 보여 주는 **바깥 인터페이스**(System Table, 프로토콜, 변수, 이미지 형식). Linux EFI stub·GRUB·shim이 의존하는 것은 이것뿐이다.
- **PI(Platform Initialization) Specification**: 펌웨어 **내부 구조**(SEC/PEI/DXE 단계). EDK2(TianoCore)가 이 구조로 구현되어 있고 AMI·Insyde·Phoenix 같은 상용 펌웨어도 대부분 EDK2 기반이다. U-Boot는 PI를 따르지 않고 UEFI 바깥 인터페이스만 구현한다(§8).

---

## 2. UEFI의 단계 — SEC · PEI · DXE · BDS · TSL · RT

### 2.1 직관 — Don의 SSD 부트와 1:1로 맞춰 보기

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="18" text-anchor="middle" font-size="14" font-weight="bold">UEFI(PI) 단계와 임베디드 대응</text>
  <rect x="8" y="32" width="104" height="70" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="60" y="52" text-anchor="middle" font-size="13" font-weight="bold">SEC</text>
  <text x="60" y="72" text-anchor="middle" font-size="12">리셋 벡터</text>
  <text x="60" y="90" text-anchor="middle" font-size="12">CAR 임시 RAM</text>
  <rect x="120" y="32" width="104" height="70" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="172" y="52" text-anchor="middle" font-size="13" font-weight="bold">PEI</text>
  <text x="172" y="72" text-anchor="middle" font-size="12">DRAM 초기화</text>
  <text x="172" y="90" text-anchor="middle" font-size="12">HOB 넘김</text>
  <rect x="232" y="32" width="104" height="70" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="284" y="52" text-anchor="middle" font-size="13" font-weight="bold">DXE</text>
  <text x="284" y="72" text-anchor="middle" font-size="12">드라이버 로드</text>
  <text x="284" y="90" text-anchor="middle" font-size="12">프로토콜 설치</text>
  <rect x="344" y="32" width="104" height="70" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="396" y="52" text-anchor="middle" font-size="13" font-weight="bold">BDS</text>
  <text x="396" y="72" text-anchor="middle" font-size="12">부트 옵션 선택</text>
  <text x="396" y="90" text-anchor="middle" font-size="12">Boot####</text>
  <rect x="456" y="32" width="104" height="70" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="508" y="52" text-anchor="middle" font-size="13" font-weight="bold">TSL</text>
  <text x="508" y="72" text-anchor="middle" font-size="12">OS 로더 실행</text>
  <text x="508" y="90" text-anchor="middle" font-size="12">GRUB · stub</text>
  <rect x="568" y="32" width="104" height="70" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="620" y="52" text-anchor="middle" font-size="13" font-weight="bold">RT</text>
  <text x="620" y="72" text-anchor="middle" font-size="12">OS 실행 중</text>
  <text x="620" y="90" text-anchor="middle" font-size="12">런타임 서비스</text>
  <line x1="564" y1="26" x2="564" y2="200" stroke="#d0564a" stroke-dasharray="5,3"/>
  <text x="560" y="214" text-anchor="end" font-size="12">ExitBootServices()</text>
  <rect x="232" y="114" width="328" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="396" y="132" text-anchor="middle" font-size="12">Boot Services: 메모리 할당 · 프로토콜 · 파일 읽기</text>
  <rect x="232" y="148" width="440" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="452" y="166" text-anchor="middle" font-size="12">Runtime Services: Get/SetVariable · GetTime · ResetSystem</text>
  <line x1="8" y1="186" x2="672" y2="186" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="14" y="202" font-size="12">시간</text>
  <text x="8" y="240" font-size="12" font-weight="bold">임베디드 대응</text>
  <rect x="8" y="250" width="104" height="52" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="60" y="272" text-anchor="middle" font-size="12">BootROM</text>
  <text x="60" y="290" text-anchor="middle" font-size="12">SRAM에서 시작</text>
  <rect x="120" y="250" width="104" height="52" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="172" y="272" text-anchor="middle" font-size="12">SPL · BL2</text>
  <text x="172" y="290" text-anchor="middle" font-size="12">DDR 초기화</text>
  <rect x="232" y="250" width="104" height="52" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="284" y="272" text-anchor="middle" font-size="12">U-Boot</text>
  <text x="284" y="290" text-anchor="middle" font-size="12">driver model</text>
  <rect x="344" y="250" width="104" height="52" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="396" y="272" text-anchor="middle" font-size="12">bootcmd</text>
  <text x="396" y="290" text-anchor="middle" font-size="12">bootflow scan</text>
  <rect x="456" y="250" width="104" height="52" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="508" y="272" text-anchor="middle" font-size="12">booti 직전</text>
  <text x="508" y="290" text-anchor="middle" font-size="12">(대응 없음)</text>
  <rect x="568" y="250" width="104" height="52" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="620" y="272" text-anchor="middle" font-size="12">BL31 상주</text>
  <text x="620" y="290" text-anchor="middle" font-size="12">PSCI 호출</text>
</svg>
```
그림 2 — PI 단계. 빨간 점선(`ExitBootServices()`) 왼쪽에서는 펌웨어가 기계의 주인이고, 오른쪽에서는 OS가 주인이며 펌웨어는 몇 개의 Runtime Services만 남긴다. 임베디드 쪽에서 "부팅 후에도 남는 펌웨어"에 해당하는 것은 TF-A BL31(PSCI)이다.

| 단계 | 하는 일 | 실행 위치 | Don의 대응 |
|---|---|---|---|
| **SEC** (Security) | 리셋 벡터, 캐시를 임시 RAM으로 쓰기(CAR, Cache-As-RAM), 신뢰의 뿌리 | 플래시(XIP) + 캐시 | BootROM, SRAM에서 첫 코드 |
| **PEI** (Pre-EFI Initialization) | DRAM 컨트롤러 학습(Intel은 MRC), 최소 칩셋 초기화, 결과를 HOB(Hand-Off Block)로 다음 단계에 전달 | CAR | SPL / TF-A BL2의 DDR 초기화 |
| **DXE** (Driver Execution Environment) | DRAM에서 수백 개 드라이버 실행, 각 드라이버가 GUID 붙은 **프로토콜**(함수 테이블)을 설치 | DRAM | U-Boot proper의 driver model, `probe()` |
| **BDS** (Boot Device Selection) | `BootOrder`/`Boot####` 변수를 읽어 부트 대상 결정, 콘솔 연결 | DRAM | `bootcmd`, `bootflow scan` |
| **TSL** (Transient System Load) | OS 로더(.efi) 실행. 로더는 Boot Services를 마음껏 쓴다 | DRAM | 해당 없음 — U-Boot는 커널에 바로 점프 |
| **RT** (Runtime) | `ExitBootServices()` 후. 펌웨어 코드 중 Runtime 영역만 남아 OS가 호출 | OS가 매핑 | TF-A BL31(PSCI, SMC 서비스) |

UEFI의 "프로토콜"은 Don에게 익숙한 패턴이다. **GUID(128비트 ID)로 찾는 함수 포인터 구조체**다. 예를 들어 블록 장치 드라이버는 `EFI_BLOCK_IO_PROTOCOL`(ReadBlocks/WriteBlocks 함수 포인터)을 자기 핸들에 설치하고, FAT 드라이버는 그 위에 `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL`을 얹는다. OS 로더는 `HandleProtocol(handle, &guid, &iface)`로 인터페이스를 얻어 `iface->Read(...)`를 부른다. 리눅스 커널의 `file_operations`와 같은 vtable이고, 다른 점은 이름 대신 GUID로 찾는다는 것뿐이다.

### 2.2 Boot Services와 Runtime Services — `ExitBootServices()`가 경계

- **Boot Services**: `AllocatePages`, `GetMemoryMap`, `LoadImage`/`StartImage`, `HandleProtocol`, 타이머·이벤트. OS 로더가 쓰는 "작은 OS". `ExitBootServices(image, map_key)`를 부르면 전부 사라지고 해당 메모리는 OS 것이 된다. 호출할 때 최신 메모리 맵의 `map_key`를 넘겨야 해서, 맵을 다시 읽고 재시도하는 루프가 필요하다 (커널 stub의 `efi_exit_boot_services()`에 그 루프가 있다).
- **Runtime Services**: `GetVariable`/`SetVariable`(NVRAM), `GetTime`/`SetTime`(RTC), `ResetSystem`, `UpdateCapsule`(펌웨어 업데이트), `SetVirtualAddressMap`. OS가 실행 중에도 호출한다. 리눅스의 `efivarfs`, `rtc-efi`, `efibootmgr`, `fwupd`가 이것을 쓴다.

함정: Runtime Services 코드는 **펌웨어 코드가 커널 권한(EL1/ring 0)에서 도는 것**이다. 펌웨어 버그가 커널을 죽일 수 있어서, 리눅스는 별도 페이지 테이블(`efi_mm`)에서 호출하고 arm64에서는 전용 스택·워크큐로 감싼다(`CONFIG_EFI_RUNTIME_WRAPPERS`). 아래 랩 커널 config에서 그 옵션을 볼 수 있다.

---

## 3. 손으로 확인 1 — 이 VM은 "진짜" UEFI + ACPI arm64 머신이다

### 3.1 `/sys/firmware` — DT 대신 ACPI와 EFI가 있다

무엇을 확인하나: 펌웨어가 커널에 무엇을 넘겼는지 sysfs로 본다. `/proc/device-tree`가 있는지도 본다.

```sh
ls /sys/firmware; ls /sys/firmware/efi
ls /sys/firmware/efi/efivars | wc -l
ls /proc/device-tree
ls -l /sys/firmware/acpi/tables
cat /sys/firmware/efi/fw_platform_size; sudo cat /sys/firmware/efi/systab
```

```text
acpi
devicetree
dmi
efi
fdt
efivars
fw_platform_size
mok-variables
systab
26
ls: cannot access '/proc/device-tree': No such file or directory
total 0
-r-------- 1 root root 588 Oct 10 10:22 APIC
-r-------- 1 root root 974 Oct 10 10:22 DSDT
-r-------- 1 root root 276 Oct 10 10:22 FACP
-r-------- 1 root root 104 Oct 10 10:22 GTDT
-r-------- 1 root root  60 Oct 10 10:22 MCFG
drwxr-xr-x 2 root root   0 Oct 10 10:22 data
drwxr-xr-x 2 root root   0 Oct 10 10:22 dynamic
64
ACPI20=0x36fbf0018
SMBIOS3=0x36fc1a000
SMBIOS=0xfffff000
```

출력에서 볼 것: `/proc/device-tree`가 **없다**. 이 기계의 하드웨어 설명은 ACPI 테이블 5개(APIC=MADT, DSDT, FACP=FADT, GTDT, MCFG)다. `systab`은 EFI System Table의 configuration table 목록으로, ACPI 2.0 RSDP와 SMBIOS 주소를 펌웨어가 OS에 알려 준 것이다. `/sys/firmware/devicetree`는 디렉터리만 있고 `base`가 없다 (§6.4에서 `fdt`의 정체를 본다).

### 3.2 커널 로그 — 부팅 첫 줄부터 EFI와 ACPI

무엇을 확인하나: 커널이 펌웨어 정보를 언제, 어디서 읽는지 본다.

```sh
sudo dmesg | grep -i -E "efi|acpi" | head -60
```

```text
[    0.000000] efi: EFI v2.7 by EDK II
[    0.000000] efi: ACPI 2.0=0x36fbf0018 SMBIOS=0xfffff000 SMBIOS 3.0=0x36fc1a000 MOKvar=0x36fba0000 INITRD=0x36e44d698 RNG=0x36fbffd18 MEMRESERVE=0x36e44d418 
[    0.000000] ACPI: Early table checksum verification disabled
[    0.000000] ACPI: RSDP 0x000000036FBF0018 000024 (v02 APPLE )
[    0.000000] ACPI: XSDT 0x000000036FBFFE98 000044 (v01 APPLE  Apple Vz 00000001      01000013)
[    0.000000] ACPI: FACP 0x000000036FBFFA98 000114 (v06 APPLE  Apple Vz 00000001 AAPL 20180427)
[    0.000000] ACPI: DSDT 0x000000036FBFF698 0003CE (v02 APPLE  Apple Vz 00000001 AAPL 20180427)
[    0.000000] ACPI: GTDT 0x000000036FBFFC18 000068 (v03 APPLE  Apple Vz 00000001 AAPL 20180427)
[    0.000000] ACPI: APIC 0x000000036FBFE998 00024C (v05 APPLE  Apple Vz 00000001 AAPL 20180427)
[    0.000000] ACPI: MCFG 0x000000036FBFFF98 00003C (v01 APPLE  Apple Vz 00000001 AAPL 20180427)
[    0.000000] psci: probing for conduit method from ACPI.
[    0.000144] ACPI: Core revision 20251212
[    0.000688] ACPI PPTT: No PPTT table found, CPU and cache topology may be inaccurate
[    0.001056] Remapping and enabling EFI services.
...
[    0.008602] ACPI: 1 ACPI AML tables successfully acquired and loaded
[    0.008648] ACPI: Interpreter enabled
[    0.008649] ACPI: Using GIC for interrupt routing
[    0.008658] ACPI: MCFG table detected, 1 entries
[    0.008966] ACPI: PCI Root Bridge [PCI0] (domain 0000 [bus 00-ff])
...
[    0.014254] efivars: Registered efivars operations
[    0.063580] rtc-efi rtc-efi.0: registered as rtc0
```
(`...`은 중간 줄 생략.)

출력에서 볼 것: 펌웨어는 "EDK II"(Apple Virtualization.framework의 UEFI는 EDK2 기반), ACPI OEM ID는 `APPLE`. `psci: probing for conduit method from ACPI` — DT 보드에서는 "from DT"로 나오는 줄이다(§8에서 비교). `Remapping and enabling EFI services`가 Runtime Services를 커널 가상 주소에 매핑하는 순간이고, `rtc-efi`는 RTC를 Runtime Service `GetTime()`으로 읽는 드라이버다.

### 3.3 ESP와 부트 엔트리 — `efibootmgr -v`

무엇을 확인하나: NVRAM의 부트 옵션이 ESP의 어떤 파일을 가리키는지 본다. `efibootmgr`는 VM에 이미 설치되어 있었다.

```sh
efibootmgr -v
mokutil --sb-state
lsblk -o NAME,SIZE,FSTYPE,PARTTYPENAME,MOUNTPOINT
sudo ls -R /boot/efi
sudo cat /boot/efi/EFI/ubuntu/grub.cfg
```

```text
BootCurrent: 0003
Timeout: 0 seconds
BootOrder: 0003,0000,0001,0002
Boot0000* UEFI Misc Device	PciRoot(0x0)/Pci(0x6,0x0){auto_created_boot_option}
      dp: 02 01 0c 00 d0 41 03 0a 00 00 00 00 / 01 01 06 00 00 06 / 7f ff 04 00
    data: 4e ac 08 81 11 9f 59 4d 85 0e e2 1a 52 2c 59 b2
Boot0001* UEFI Misc Device 2	PciRoot(0x0)/Pci(0x7,0x0){auto_created_boot_option}
      dp: 02 01 0c 00 d0 41 03 0a 00 00 00 00 / 01 01 06 00 00 07 / 7f ff 04 00
    data: 4e ac 08 81 11 9f 59 4d 85 0e e2 1a 52 2c 59 b2
Boot0002* UEFI Non-Block Boot Device	PciRoot(0x0)/Pci(0x8,0x0){auto_created_boot_option}
      dp: 02 01 0c 00 d0 41 03 0a 00 00 00 00 / 01 01 06 00 00 08 / 7f ff 04 00
    data: 4e ac 08 81 11 9f 59 4d 85 0e e2 1a 52 2c 59 b2
Boot0003* Ubuntu	HD(15,GPT,38827aa1-0269-46ce-b8cf-a3ecb23b8b57,0x800,0x31801)/\EFI\ubuntu\shimaa64.efi
      dp: 04 01 2a 00 0f 00 00 00 00 08 00 00 00 00 00 00 01 18 03 00 00 00 00 00 a1 7a 82 38 69 02 ce 46 b8 cf a3 ec b2 3b 8b 57 02 02 / 04 04 36 00 5c 00 45 00 46 00 49 00 5c 00 75 00 62 00 75 00 6e 00 74 00 75 00 5c 00 73 00 68 00 69 00 6d 00 61 00 61 00 36 00 34 00 2e 00 65 00 66 00 69 00 00 00 / 7f ff 04 00
This system doesn't support Secure Boot
NAME      SIZE FSTYPE  PARTTYPENAME        MOUNTPOINT
vda       100G                             
├─vda1     99G ext4    Linux root (ARM-64) /
├─vda13   923M ext4    Linux extended boot /boot
└─vda15    99M vfat    EFI System          /boot/efi
vdb     273.5M iso9660                     /mnt/lima-cidata
/boot/efi:
EFI

/boot/efi/EFI:
BOOT
ubuntu

/boot/efi/EFI/BOOT:
BOOTAA64.EFI
fbaa64.efi
mmaa64.efi

/boot/efi/EFI/ubuntu:
BOOTAA64.CSV
grub.cfg
grubaa64.efi
mmaa64.efi
shimaa64.efi
search.fs_uuid b98ac962-0393-464c-b887-b8c08c694708 root 
set prefix=($root)'/grub'
configfile $prefix/grub.cfg
```

출력에서 볼 것:

- `Boot0003`의 **디바이스 패스**(device path)는 "디스크의 GPT 15번 파티션(vda15, 시작 LBA 0x800) 안의 `\EFI\ubuntu\shimaa64.efi`"다. UEFI는 **파일 경로**로 부트 대상을 찾는다. MBR처럼 "디스크 첫 섹터"가 아니다.
- `dp:` 바이트는 노드의 연속이다: `04 01`(Media/HardDrive) → `04 04`(Media/FilePath, UTF-16 문자열) → `7f ff`(End). §3.4에서 C로 직접 디코드한다.
- ESP는 그냥 **FAT(vfat) 파티션**이다(PARTTYPENAME "EFI System"). `EFI/BOOT/BOOTAA64.EFI`는 NVRAM에 엔트리가 없을 때 쓰는 **제거식 미디어 경로**(removable media path, arm64 이름은 `BOOTAA64.EFI`, x86-64는 `BOOTX64.EFI`)다. `fbaa64.efi`(fallback)는 `BOOTAA64.CSV`를 읽어 `Boot####`를 다시 만들어 주고, `mmaa64.efi`는 MOK 관리자(§4)다.
- ESP의 `grub.cfg`는 3줄짜리 "점프대"다. 진짜 설정은 `/boot`(ext4)의 `grub/grub.cfg`에 있다.
- 이 VM은 Secure Boot를 지원하지 않는다(Apple Virtualization의 UEFI). 그래도 shim은 그대로 사슬에 끼어 있다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="8" y="20" width="200" height="160" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="108" y="42" text-anchor="middle" font-size="13" font-weight="bold">NVRAM (UEFI 변수)</text>
  <text x="18" y="68" font-size="12">BootCurrent = 0003</text>
  <text x="18" y="88" font-size="12">BootOrder = 0003,0000,…</text>
  <text x="18" y="116" font-size="12">Boot0003 "Ubuntu"</text>
  <text x="28" y="136" font-size="12">HD(15,GPT,…)</text>
  <text x="28" y="156" font-size="12">\EFI\ubuntu\shimaa64.efi</text>
  <rect x="240" y="20" width="200" height="160" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="42" text-anchor="middle" font-size="13" font-weight="bold">ESP (vda15, FAT)</text>
  <text x="250" y="68" font-size="12">EFI/ubuntu/shimaa64.efi</text>
  <text x="250" y="92" font-size="12">EFI/ubuntu/grubaa64.efi</text>
  <text x="250" y="116" font-size="12">EFI/ubuntu/grub.cfg</text>
  <text x="250" y="148" font-size="12">EFI/BOOT/BOOTAA64.EFI</text>
  <text x="250" y="168" font-size="12">(제거식 미디어 경로)</text>
  <rect x="472" y="20" width="200" height="160" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="572" y="42" text-anchor="middle" font-size="13" font-weight="bold">/boot (vda13, ext4)</text>
  <text x="482" y="68" font-size="12">grub/grub.cfg</text>
  <text x="482" y="92" font-size="12">vmlinuz-7.0.0-38-generic</text>
  <text x="482" y="116" font-size="12">initrd.img-7.0.0-38-…</text>
  <line x1="208" y1="152" x2="246" y2="72" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="420" y1="72" x2="420" y2="86" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="420" y1="112" x2="476" y2="72" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="440" y1="92" x2="476" y2="92" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="8" y="200" width="664" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="222" text-anchor="middle" font-size="12">BDS: BootOrder 순서로 Boot####의 device path를 열고 LoadImage → StartImage</text>
  <text x="340" y="244" text-anchor="middle" font-size="12">모든 엔트리가 실패하면 각 장치의 \EFI\BOOT\BOOTAA64.EFI를 시도한다</text>
</svg>
```
그림 3 — UEFI 부트 매니저. NVRAM 변수가 ESP의 파일을 가리키고(shim), shim은 같은 디렉터리의 GRUB을, GRUB은 ext4 `/boot`의 설정·커널·initrd를 읽는다. 펌웨어는 ext4를 몰라도 된다 — FAT만 읽으면 된다.

### 3.4 C로 UEFI 변수 디코드하기 — `efivarfs` 파일 형식

무엇을 확인하는 코드인가: `/sys/firmware/efi/efivars/<이름>-<GUID>` 파일은 "4바이트 속성 + 변수 데이터"다. `Boot####`의 데이터는 `EFI_LOAD_OPTION` 구조체(속성 u32, 디바이스 패스 길이 u16, UTF-16 설명, 디바이스 패스, 옵션 데이터)다. 이걸 직접 풀어 본다.

```c
/* c8_efivar.c - decode BootOrder / BootCurrent / Boot#### from efivarfs (4-byte attr + data) */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define GUID "8be4df61-93ca-11d2-aa0d-00e098032b8c"   /* EFI_GLOBAL_VARIABLE */
static uint8_t b[4096];
static long rd(const char *name)
{
    char p[256];
    snprintf(p, sizeof p, "/sys/firmware/efi/efivars/%s-" GUID, name);
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); exit(1); }
    long n = (long)fread(b, 1, sizeof b, f);
    return fclose(f), n;         /* b[0..3] = attributes, data starts at b+4 */
}
static void devpath(const uint8_t *p, const uint8_t *end)
{
    while (p + 4 <= end && p[0] != 0x7f) {        /* 0x7f = End of device path */
        uint16_t len = p[2] | p[3] << 8;
        if (len < 4) break;
        if (p[0] == 4 && p[1] == 1)                 /* Media / Hard Drive */
            printf("      HD(part=%u, start=0x%llx, size=0x%llx)\n", *(uint32_t *)(p + 4),
                   (unsigned long long)*(uint64_t *)(p + 8), (unsigned long long)*(uint64_t *)(p + 16));
        else if (p[0] == 4 && p[1] == 4) {          /* Media / File path (UTF-16) */
            printf("      File(");
            for (const uint8_t *c = p + 4; c + 1 < p + len && c[0]; c += 2) putchar(c[0]);
            puts(")");
        } else
            printf("      node type=%u subtype=%u len=%u\n", p[0], p[1], len);
        p += len;
    }
}
int main(void)
{
    long n = rd("BootCurrent");
    printf("BootCurrent = Boot%04X (attr=0x%x)\n", *(uint16_t *)(b + 4), *(uint32_t *)b);
    n = rd("BootOrder");
    uint16_t order[32];
    int cnt = (int)((n - 4) / 2) > 32 ? 32 : (int)((n - 4) / 2);
    memcpy(order, b + 4, (size_t)cnt * 2);
    printf("BootOrder   =");
    for (int i = 0; i < cnt; i++) printf(" %04X", order[i]);
    puts("");
    for (int i = 0; i < cnt; i++) {
        char name[16];
        snprintf(name, sizeof name, "Boot%04X", order[i]);
        n = rd(name);
        const uint8_t *lo = b + 4;                  /* EFI_LOAD_OPTION */
        uint32_t attr = *(const uint32_t *)lo;
        uint16_t fpl = *(const uint16_t *)(lo + 4);
        const uint8_t *c = lo + 6;
        printf("%s attr=0x%x (%s) desc=\"", name, attr, attr & 1 ? "ACTIVE" : "inactive");
        for (; c[0] || c[1]; c += 2) putchar(c[0]);
        printf("\" pathlen=%u\n", fpl);
        devpath(c + 2, c + 2 + fpl);
        printf("      optional data: %ld bytes\n", (long)((b + n) - (c + 2 + fpl)));
    }
    return 0;
}
```

```sh
cd /var/tmp/C8 && gcc -std=gnu11 -Wall -Wextra -O2 $S/c8_efivar.c -o c8_efivar && ./c8_efivar
od -An -tx1 /sys/firmware/efi/efivars/BootOrder-8be4df61-93ca-11d2-aa0d-00e098032b8c
```

```text
BootCurrent = Boot0003 (attr=0x6)
BootOrder   = 0003 0000 0001 0002
Boot0003 attr=0x1 (ACTIVE) desc="Ubuntu" pathlen=100
      HD(part=15, start=0x800, size=0x31801)
      File(\EFI\ubuntu\shimaa64.efi)
      optional data: 0 bytes
Boot0000 attr=0x1 (ACTIVE) desc="UEFI Misc Device" pathlen=22
      node type=2 subtype=1 len=12
      node type=1 subtype=1 len=6
      optional data: 16 bytes
Boot0001 attr=0x1 (ACTIVE) desc="UEFI Misc Device 2" pathlen=22
      node type=2 subtype=1 len=12
      node type=1 subtype=1 len=6
      optional data: 16 bytes
Boot0002 attr=0x1 (ACTIVE) desc="UEFI Non-Block Boot Device" pathlen=22
      node type=2 subtype=1 len=12
      node type=1 subtype=1 len=6
      optional data: 16 bytes
 07 00 00 00 03 00 00 00 01 00 02 00
```

출력에서 볼 것: 경고 0개로 빌드되고, `efibootmgr`와 같은 내용이 나온다. 변수 속성 비트는 `NON_VOLATILE=1`, `BOOTSERVICE_ACCESS=2`, `RUNTIME_ACCESS=4`다. `BootOrder`는 `0x7`(NV + BS + RT: 재부팅 후에도 남음), `BootCurrent`는 `0x6`(휘발성 — 이번 부팅에서 펌웨어가 만든 값)이다. type 2/1은 ACPI 노드(`PciRoot`), 1/1은 PCI 노드(`Pci(dev,fn)`)다. 이 경로 표현은 x86 PC와 완전히 같다 — 그것이 표준의 힘이다.

---

## 4. Secure Boot — PK · KEK · db · dbx, 그리고 shim

### 4.1 키 계층

Don이 SSD에서 한 FW 서명 검증(ROM에 공개키 해시를 퓨즈하고, 로더가 다음 이미지를 검증)을 **변수 저장소 안의 키 계층**으로 표준화한 것이 UEFI Secure Boot다.

| 변수 | 누가 소유 | 역할 |
|---|---|---|
| **PK** (Platform Key) | 보드 제조사(OEM) | 최상위 키. KEK 변경을 서명한다. PK가 없으면 "Setup Mode"(누구나 키 등록 가능) |
| **KEK** (Key Exchange Key) | OEM + Microsoft | db/dbx 갱신을 서명한다 |
| **db** (signature database) | — | **허용** 인증서·해시. PC에는 보통 Microsoft Windows CA와 Microsoft UEFI CA(제3자용)가 들어 있다 |
| **dbx** | — | **폐기** 목록. 취약한 부트로더 해시·인증서 |
| MOK (Machine Owner Key) | 기계 사용자 | shim이 관리하는 추가 허용 목록 (`mokutil`) |

펌웨어는 `LoadImage()`로 `.efi`를 실행하기 전에 PE 파일의 Authenticode 서명을 db/dbx로 검증한다.

### 4.2 shim — 리눅스 배포판이 Microsoft 서명을 받는 방법

PC 메인보드의 db에는 대개 Microsoft 키만 있다. 배포판이 모든 GRUB·커널 빌드를 Microsoft에 서명받을 수는 없으므로 **shim**이라는 작은 1단계 로더를 둔다.

1. shim은 Microsoft UEFI CA로 서명된다 → 펌웨어 db 검증 통과.
2. shim 안에는 배포판(Canonical·Red Hat …)의 인증서가 내장되어 있다. shim은 자기 검증 프로토콜로 `grubaa64.efi`를 검증한다.
3. GRUB은 shim의 검증 프로토콜을 이용해 커널(`vmlinuz`, 배포판 키로 서명된 PE)을 검증한다.
4. 커널은 모듈 서명(`CONFIG_MODULE_SIG`)과 lockdown으로 사슬을 이어 간다.
5. 사용자가 직접 빌드한 커널·모듈은 MOK에 자기 키를 등록(`mokutil --import`, 재부팅 시 `mmaa64.efi` MokManager에서 확인)해서 통과시킨다.
6. **SBAT**: 취약한 GRUB 세대를 해시 하나하나 dbx에 넣는 대신 "세대 번호"로 폐기하는 장치. 위 `efivars` 목록의 `SbatLevelRT`, `MokListRT`가 shim이 OS에 노출한 값이다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" font-size="13" font-weight="bold">키 계층 (NVRAM, 인증된 변수)</text>
  <rect x="10" y="32" width="130" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="75" y="59" text-anchor="middle" font-size="12">PK (OEM)</text>
  <rect x="180" y="32" width="150" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="255" y="59" text-anchor="middle" font-size="12">KEK (OEM · MS)</text>
  <rect x="370" y="32" width="140" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="440" y="59" text-anchor="middle" font-size="12">db (허용)</text>
  <rect x="530" y="32" width="140" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="600" y="59" text-anchor="middle" font-size="12">dbx (폐기)</text>
  <line x1="140" y1="54" x2="178" y2="54" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="330" y1="54" x2="368" y2="54" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="159" y="96" text-anchor="middle" font-size="12">서명</text>
  <text x="349" y="96" text-anchor="middle" font-size="12">서명</text>
  <text x="10" y="132" font-size="13" font-weight="bold">실행 사슬 (각 화살표 = 다음 이미지 서명 검증)</text>
  <rect x="10" y="144" width="120" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="70" y="168" text-anchor="middle" font-size="12">UEFI 펌웨어</text>
  <text x="70" y="186" text-anchor="middle" font-size="12">db/dbx로 검증</text>
  <rect x="150" y="144" width="120" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="168" text-anchor="middle" font-size="12">shim</text>
  <text x="210" y="186" text-anchor="middle" font-size="12">MS UEFI CA 서명</text>
  <rect x="290" y="144" width="120" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="350" y="168" text-anchor="middle" font-size="12">GRUB</text>
  <text x="350" y="186" text-anchor="middle" font-size="12">배포판 키 서명</text>
  <rect x="430" y="144" width="120" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="490" y="168" text-anchor="middle" font-size="12">커널 (PE)</text>
  <text x="490" y="186" text-anchor="middle" font-size="12">배포판 키 · MOK</text>
  <rect x="570" y="144" width="100" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="620" y="168" text-anchor="middle" font-size="12">모듈</text>
  <text x="620" y="186" text-anchor="middle" font-size="12">모듈 서명</text>
  <line x1="130" y1="172" x2="148" y2="172" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="270" y1="172" x2="288" y2="172" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="410" y1="172" x2="428" y2="172" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="550" y1="172" x2="568" y2="172" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="440" y1="76" x2="80" y2="142" stroke="#3f9a6b" stroke-dasharray="4,3" marker-end="url(#ah4)"/>
  <text x="340" y="232" text-anchor="middle" font-size="12">임베디드 대응: BootROM이 퓨즈의 키 해시로 SPL 검증 → FIT 서명 → dm-verity (C5, L 모듈)</text>
</svg>
```
그림 4 — Secure Boot 키 계층과 실행 사슬. 점선은 "펌웨어가 db를 참조해 shim을 검증한다"는 뜻이다. 임베디드 secure boot는 키가 퓨즈(OTP)에 있다는 점만 다르고 사슬의 생각은 같다.

### 4.3 직접 확인 — 서명 없는 랩 커널을 Secure Boot 펌웨어가 거부한다

무엇을 확인하나: Ubuntu의 EDK2 패키지에는 Microsoft 키가 미리 등록된 Secure Boot 변수 템플릿(`AAVMF_VARS.ms.fd`)이 있다. 랩 `Image`(서명 없음)를 ESP의 `\EFI\BOOT\BOOTAA64.EFI`로 넣은 디스크를 만들어 이 펌웨어로 부팅해 본다. 디스크 이미지는 root 없이 `sgdisk` + mtools로 만든다.

```sh
cd /var/tmp/C8
truncate -s 128M c8_esp.img
sgdisk -n 1:2048:0 -t 1:ef00 -c 1:ESP c8_esp.img >/dev/null
mformat -i c8_esp.img@@1M -F -v ESP ::
mmd -i c8_esp.img@@1M ::/EFI ::/EFI/BOOT
mcopy -i c8_esp.img@@1M /var/lab/out/Image ::/EFI/BOOT/BOOTAA64.EFI
mcopy -i c8_esp.img@@1M /var/lab/out/initramfs.cpio.gz ::/initramfs.cpio.gz
mcopy -i c8_esp.img@@1M /var/lab/out/virt.dtb ::/virt.dtb
cp c8_esp.img c8_esp_sb.img; cp /usr/share/AAVMF/AAVMF_VARS.ms.fd c8_vars_ms2.fd
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 1024M -nographic \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.secboot.fd \
  -drive if=pflash,format=raw,file=c8_vars_ms2.fd \
  -drive if=none,file=c8_esp_sb.img,format=raw,id=d0 -device virtio-blk-device,drive=d0
```

```text
UEFI firmware (version 2025.11-3ubuntu7.3 built at 15:40:26 on Sep 23 2026)
BdsDxe: loading Boot0002 "UEFI Misc Device" from VenHw(837DCA9E-E874-4D82-B29A-23FE0E23D1E2,003E000A00000000)
BdsDxe: failed to load Boot0002 "UEFI Misc Device" from VenHw(837DCA9E-E874-4D82-B29A-23FE0E23D1E2,003E000A00000000): Access Denied -- rejected probably by Secure Boot
BdsDxe: No bootable option or device was found.
BdsDxe: Press any key to enter the Boot Manager Menu.
qemu-system-aarch64: terminating on signal 15 from pid 227887 (timeout)
```

출력에서 볼 것: BDS가 virtio 디스크에 엔트리를 자동 생성(`Boot0002 "UEFI Misc Device"`)해 제거식 미디어 경로를 열었지만, `LoadImage()`가 **`Access Denied`**를 돌려준다. 같은 디스크를 Secure Boot 없는 펌웨어(`AAVMF_CODE.no-secboot.fd`)로 돌리면 커널이 로드된다(§5.2, §7.4). 참고로 QEMU의 `-kernel` 옵션(fw_cfg로 커널 전달)을 Secure Boot 펌웨어와 함께 쓰면 이 메시지 대신 `Not Found`로 끝났다 — 직접 커널 로더 경로 자체가 막혀 있는 것으로 보인다(동작만 확인했고 EDK2 소스로 원인을 확인하지는 않았다).

---

## 5. GRUB과 systemd-boot — UEFI 위의 OS 로더

### 5.1 OS 로더는 그냥 "EFI 애플리케이션"이다

UEFI 세계에서 GRUB, shim, systemd-boot, 그리고 **Linux 커널 자신**은 모두 같은 형식의 파일이다: **PE/COFF 실행 파일**(Windows `.exe`와 같은 포맷, `MZ`로 시작)이고 Optional Header의 Subsystem 필드가 10(`EFI_APPLICATION`)이다. 펌웨어는 ELF를 모른다.

무엇을 확인하는 코드인가: PE 헤더를 직접 읽어 Machine, Subsystem, 진입점, 섹션을 출력하고, arm64 Linux `Image` 헤더(`ARM\x64` 매직)도 같이 있는지 본다.

```c
/* c8_pehdr.c - arm64 Image header + PE/COFF header dump (is this file an EFI app?) */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint16_t r16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t r32(const uint8_t *p) { return r16(p) | (uint32_t)r16(p + 2) << 16; }
static uint64_t r64(const uint8_t *p) { return r32(p) | (uint64_t)r32(p + 4) << 32; }

int main(int argc, char **argv)
{
    static uint8_t b[4096];
    if (argc != 2) { fprintf(stderr, "usage: %s file\n", argv[0]); exit(1); }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); exit(1); }
    size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n < 0x200 || b[0] != 'M' || b[1] != 'Z') { printf("%s: no MZ -> not a PE file\n", argv[1]); return 0; }
    printf("%s\n  DOS magic 'MZ', e_lfanew(0x3c) = 0x%x\n", argv[1], r32(b + 0x3c));
    if (!memcmp(b + 0x38, "ARM\x64", 4))   /* arm64 boot protocol header (booti path) */
        printf("  arm64 Image hdr: text_offset=0x%llx image_size=0x%llx flags=0x%llx magic='ARM\\x64'\n",
               (unsigned long long)r64(b + 8), (unsigned long long)r64(b + 16),
               (unsigned long long)r64(b + 24));
    uint32_t pe = r32(b + 0x3c);
    if (pe + 24 > n || memcmp(b + pe, "PE\0\0", 4)) { printf("  no PE signature\n"); return 0; }
    const uint8_t *coff = b + pe + 4, *opt = coff + 20;
    uint16_t nsec = r16(coff + 2), optsz = r16(coff + 16);
    printf("  PE sig at 0x%x  Machine=0x%04x%s  sections=%u\n", pe, r16(coff),
           r16(coff) == 0xaa64 ? " (ARM64)" : "", nsec);
    uint16_t sub = r16(opt + 68);
    printf("  Optional hdr magic=0x%03x (%s) EntryPoint RVA=0x%x\n", r16(opt),
           r16(opt) == 0x20b ? "PE32+" : "PE32", r32(opt + 16));
    printf("  ImageBase=0x%llx SectionAlign=0x%x SizeOfImage=0x%x\n",
           (unsigned long long)r64(opt + 24), r32(opt + 32), r32(opt + 56));
    printf("  Subsystem=%u (%s)\n", sub, sub == 10 ? "EFI_APPLICATION" :
           sub == 11 ? "EFI_BOOT_SERVICE_DRIVER" : sub == 12 ? "EFI_RUNTIME_DRIVER" : "other");
    const uint8_t *sh = opt + optsz;
    for (unsigned i = 0; i < nsec && i < 6 && sh + 40 <= b + n; i++, sh += 40)
        printf("  section %-8.8s VA=0x%08x VSize=0x%08x\n", (const char *)sh, r32(sh + 12), r32(sh + 8));
    if (nsec > 6) printf("  ... (%u more sections)\n", nsec - 6);
    return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 $S/c8_pehdr.c -o c8_pehdr
sudo ./c8_pehdr /boot/efi/EFI/ubuntu/shimaa64.efi
sudo ./c8_pehdr /boot/vmlinuz-7.0.0-38-generic
./c8_pehdr /var/lab/out/u-boot.bin
```

```text
/boot/efi/EFI/ubuntu/shimaa64.efi
  DOS magic 'MZ', e_lfanew(0x3c) = 0x80
  PE sig at 0x80  Machine=0xaa64 (ARM64)  sections=10
  Optional hdr magic=0x20b (PE32+) EntryPoint RVA=0x1e000
  ImageBase=0x0 SectionAlign=0x1000 SizeOfImage=0xd9000
  Subsystem=10 (EFI_APPLICATION)
  section /4       VA=0x00005000 VSize=0x000186a4
  section .text    VA=0x0001e000 VSize=0x0006723c
  section .reloc   VA=0x00086000 VSize=0x0000000a
  section /14      VA=0x00088000 VSize=0x0000006b
  section /26      VA=0x00089000 VSize=0x0000006a
  section .data    VA=0x0008a000 VSize=0x0002c670
  ... (4 more sections)
/boot/vmlinuz-7.0.0-38-generic
  DOS magic 'MZ', e_lfanew(0x3c) = 0x40
  PE sig at 0x40  Machine=0xaa64 (ARM64)  sections=40
  Optional hdr magic=0x20b (PE32+) EntryPoint RVA=0x18f60
  ImageBase=0x1e7c10000 SectionAlign=0x1000 SizeOfImage=0x16cf000
  Subsystem=10 (EFI_APPLICATION)
  section .text    VA=0x00015000 VSize=0x00007ca8
  section .rodata  VA=0x0001d000 VSize=0x00004000
  section .data    VA=0x00021000 VSize=0x00000068
  section .reloc   VA=0x00022000 VSize=0x00000090
  section .dtbauto VA=0x00023000 VSize=0x00034dab
  section .dtbauto VA=0x00058000 VSize=0x00032a65
  ... (34 more sections)
/var/lab/out/u-boot.bin: no MZ -> not a PE file
```

출력에서 볼 것: shim과 Ubuntu 커널은 모두 `Machine=0xaa64`, `Subsystem=10`인 EFI 앱이다. `u-boot.bin`은 PE가 아니다 — U-Boot는 펌웨어 자리에서 리셋 벡터부터 도는 raw 바이너리이기 때문이다(`-bios`로 넣는다). Ubuntu 26.04의 arm64 `vmlinuz`는 흥미롭게도 **arm64 `Image` 헤더가 없다**. `objdump -h`로 섹션 이름을 보면 정체가 보인다:

```sh
sudo objdump -h /boot/vmlinuz-7.0.0-38-generic | awk '/^ +[0-9]/{print $2}' | sort | uniq -c | sort -rn
```

```text
     32 .dtbauto
      1 .uname
      1 .text
      1 .sbat
      1 .rodata
      1 .reloc
      1 .linux
      1 .hwids
      1 .data
```

출력에서 볼 것: 작은 stub 코드(`.text`) 안에 실제 커널(`.linux`), DTB 32개(`.dtbauto`), 하드웨어 ID 표(`.hwids`), SBAT 메타데이터(`.sbat`)를 섹션으로 담은 **UKI(Unified Kernel Image) 스타일의 래퍼**다. `.dtbauto`/`.hwids`는 ACPI가 불완전한 arm64 노트북(Snapdragon 계열)에서 SMBIOS ID로 맞는 DTB를 골라 주기 위한 장치로 알고 있다(기억 기반 — Ubuntu가 쓰는 stub의 이름·세부는 확인하지 않았다). 요점은 "UEFI 세계에서는 커널 파일 자체가 PE 컨테이너가 되어 다른 것까지 담을 수 있다"는 것이다.

### 5.2 GRUB을 직접 만들어 EDK2 → GRUB → 랩 커널로 부팅

무엇을 확인하나: GRUB이 펌웨어 위의 EFI 앱으로서 FAT를 읽고 커널을 "EFI 앱으로" 실행하는 과정을 본다. `grub-mkstandalone`은 모듈과 설정을 메모리 디스크로 한 `.efi`에 묶는다(VM에 `grub-efi-arm64-bin`이 이미 설치되어 있다).

```text
set timeout=0
insmod part_gpt
insmod fat
search --no-floppy --file --set=root /Image
echo "GRUB: root=$root"
linux /Image console=ttyAMA0 efi=debug "LAB_CMD=cat~/proc/cmdline;dmesg~|~grep~-e~Machine~-e~ACPI:~-e~psci:~|~head~-4"
initrd /initramfs.cpio.gz
boot
```

```sh
grub-mkstandalone -O arm64-efi -o c8_grubaa64.efi "boot/grub/grub.cfg=$S/c8_grub.cfg"
./c8_pehdr c8_grubaa64.efi
cp c8_esp.img c8_esp_grub.img
mcopy -o -i c8_esp_grub.img@@1M c8_grubaa64.efi ::/EFI/BOOT/BOOTAA64.EFI
mcopy -i c8_esp_grub.img@@1M /var/lab/out/Image ::/Image
cp /usr/share/AAVMF/AAVMF_VARS.fd c8_vars3.fd
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 1024M -nographic \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.no-secboot.fd \
  -drive if=pflash,format=raw,file=c8_vars3.fd \
  -drive if=none,file=c8_esp_grub.img,format=raw,id=d0 -device virtio-blk-device,drive=d0
```

```text
c8_grubaa64.efi
  DOS magic 'MZ', e_lfanew(0x3c) = 0x80
  PE sig at 0x80  Machine=0xaa64 (ARM64)  sections=4
  Optional hdr magic=0x20b (PE32+) EntryPoint RVA=0x1000
  ImageBase=0x0 SectionAlign=0x1000 SizeOfImage=0x5ca000
  Subsystem=10 (EFI_APPLICATION)
  section .text    VA=0x00001000 VSize=0x0000e000
  section .data    VA=0x0000f000 VSize=0x00010000
  section mods     VA=0x0001f000 VSize=0x005aa000
  section .reloc   VA=0x005c9000 VSize=0x00001000
UEFI firmware (version 2025.11-3ubuntu7.3 built at 15:40:26 on Sep 23 2026)
BdsDxe: loading Boot0002 "UEFI Misc Device" from VenHw(837DCA9E-E874-4D82-B29A-23FE0E23D1E2,003E000A00000000)
BdsDxe: starting Boot0002 "UEFI Misc Device" from VenHw(837DCA9E-E874-4D82-B29A-23FE0E23D1E2,003E000A00000000)
GRUB: root=hd0,gpt1
EFI stub: Booting Linux Kernel...
EFI stub: EFI_RNG_PROTOCOL unavailable
EFI stub: Loaded initrd from LINUX_EFI_INITRD_MEDIA_GUID device path
EFI stub: Generating empty DTB
EFI stub: Exiting boot services...
lab initramfs: 6.18.55 up 2.59s
BOOT_IMAGE=/Image console=ttyAMA0 efi=debug LAB_CMD=cat~/proc/cmdline;dmesg~|~grep~-e~Machine~-e~ACPI:~-e~psci:~|~head~-4
[    0.000000] ACPI: Early table checksum verification disabled
[    0.000000] ACPI: RSDP 0x000000007F0C9018 000024 (v02 BOCHS )
[    0.000000] ACPI: XSDT 0x000000007F0C9F18 000064 (v01 BOCHS  BXPC     00000001      01000013)
[    0.000000] ACPI: FACP 0x000000007F0C9B18 000114 (v06 BOCHS  BXPC     00000001 BXPC 00000001)
[    3.161079] reboot: Power down
```

출력에서 볼 것: GRUB 섹션 `mods`(약 5.6 MB)에 모듈이 통째로 들어 있다. GRUB은 커널에 `BOOT_IMAGE=/Image`를 붙여 넘긴다(VM 호스트의 `/proc/cmdline`에서도 같은 접두어를 봤다). GRUB 2.12 이후 arm64에서는 GRUB이 커널을 직접 해부하지 않고 **`LoadImage`/`StartImage`로 커널의 EFI stub을 실행**하며, initrd는 `LoadFile2` 프로토콜(§6.2)로 건넨다 — stub의 "Loaded initrd from LINUX_EFI_INITRD_MEDIA_GUID device path"가 그 증거다. 처음 시도에서 `insmod part_gpt`/`fat` 없이 `search`를 하자 `error: no such device: /Image.`, 따옴표 없이 `;`·`|`를 쓰자 `error: syntax error.`가 났다 — GRUB 스크립트도 셸처럼 파싱한다.

### 5.3 systemd-boot와 UKI

| 항목 | GRUB | systemd-boot |
|---|---|---|
| 크기·철학 | 크다. 자체 파일시스템 드라이버(ext4, btrfs, LVM, LUKS…) + 스크립트 언어 | 작다. **펌웨어가 읽을 수 있는 것(ESP의 FAT)만** 읽는다 |
| 설정 | `grub.cfg`(스크립트) | ESP의 `loader/entries/*.conf`(Boot Loader Specification Type #1) |
| 커널 위치 | 어디든(`/boot` ext4 가능) | ESP 또는 XBOOTLDR 파티션 |
| UKI | 체인로드 가능 | `EFI/Linux/*.efi`를 자동 인식(Type #2) |
| BIOS 지원 | 있음 | 없음 (UEFI 전용) |

UKI(Unified Kernel Image)는 stub + 커널 + initrd + cmdline + (DTB)를 PE 한 파일로 묶어 **한 번에 서명**하는 방식이다. cmdline까지 서명 범위에 들어가므로 "init=/bin/sh" 같은 변조를 막는다. 임베디드의 FIT 이미지(커널+DTB+initrd를 서명된 한 파일로)와 같은 발상이다 (C3).

---

## 6. Linux EFI stub — arm64 `Image`는 동시에 PE/COFF EFI 앱이다

### 6.1 하나의 파일, 두 개의 입구

```sh
xxd -l 0x48 /var/lab/out/Image
objdump -D -b binary -m aarch64 --start-address=0 --stop-address=8 /var/lab/out/Image | tail -2
./c8_pehdr /var/lab/out/Image
nm -n /var/lab/build/linux/vmlinux | grep -E " (_text|primary_entry|__efistub_efi_pe_entry)$"
```

```text
00000000: 4d5a 40fa 3ba8 7f14 0000 0000 0000 0000  MZ@.;...........
00000010: 0000 9802 0000 0000 0a00 0000 0000 0000  ................
00000020: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000030: 0000 0000 0000 0000 4152 4d64 4000 0000  ........ARMd@...
00000040: 5045 0000 64aa 0200                      PE..d...
       0:	fa405a4d 	ccmp	x18, #0x0, #0xd, pl	// pl = nfrst
       4:	147fa83b 	b	0x1fea0f0
/var/lab/out/Image
  DOS magic 'MZ', e_lfanew(0x3c) = 0x40
  arm64 Image hdr: text_offset=0x0 image_size=0x2980000 flags=0xa magic='ARM\x64'
  PE sig at 0x40  Machine=0xaa64 (ARM64)  sections=2
  Optional hdr magic=0x20b (PE32+) EntryPoint RVA=0x2075fe8
  ImageBase=0x0 SectionAlign=0x10000 SizeOfImage=0x2980000
  Subsystem=10 (EFI_APPLICATION)
  section .text    VA=0x00010000 VSize=0x020e0000
  section .data    VA=0x020f0000 VSize=0x00890000
ffff800080000000 T _text
ffff800081fea0f0 T primary_entry
ffff800082075fe8 t __efistub_efi_pe_entry
```

출력에서 볼 것 — 이 64바이트 헤더는 Linux arm64의 작은 걸작이다.

- 첫 2바이트 `4d 5a`는 DOS/PE 매직 **`MZ`**다. 그런데 첫 4바이트 `fa405a4d`를 AArch64 명령으로 해석하면 `ccmp x18, #0, #0xd, pl` — 레지스터 x18과 플래그만 건드리는 **사실상 NOP**이다. 일부러 "MZ이면서 실행해도 무해한 명령"을 골랐다.
- 다음 명령 `b 0x1fea0f0`은 `primary_entry`(= `_text` + 0x1fea0f0)로 점프한다. **`booti`는 파일 오프셋 0으로 점프**하므로 이 경로로 들어간다 (Documentation/arch/arm64/booting.rst의 code0/code1).
- 오프셋 0x38의 `ARMd`(= `ARM\x64`)는 arm64 boot protocol 매직, 0x3c의 `0x40`은 PE 헤더 위치(e_lfanew). 그래서 0x40에 `PE\0\0`이 온다.
- PE의 `EntryPoint RVA=0x2075fe8` = `_text` + 0x2075fe8 = **`__efistub_efi_pe_entry`**. **UEFI 펌웨어(또는 GRUB, U-Boot `bootefi`)는 이 입구**로 들어온다. stub 코드는 커널 안에 `__efistub_` 접두어로 격리되어 링크된다.
- `flags=0xa`: bit0=0(리틀 엔디안), bit1-2=01(4K 페이지), bit3=1(물리 주소 어디에 놓아도 됨).

### 6.2 stub이 하는 일 — 펌웨어 세계에서 커널 세계로 건너가기

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="200" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="30" text-anchor="middle" font-size="12">UEFI 펌웨어 · GRUB</text>
  <text x="110" y="46" text-anchor="middle" font-size="12">U-Boot bootefi</text>
  <rect x="470" y="10" width="200" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="570" y="30" text-anchor="middle" font-size="12">U-Boot booti</text>
  <text x="570" y="46" text-anchor="middle" font-size="12">(DTB를 직접 준비)</text>
  <line x1="110" y1="54" x2="110" y2="76" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="120" y="70" font-size="12">PE EntryPoint</text>
  <rect x="10" y="78" width="400" height="214" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="98" text-anchor="middle" font-size="13" font-weight="bold">efi_pe_entry (EFI 앱으로 실행, Boot Services 사용)</text>
  <text x="22" y="124" font-size="12">1. LoadOptions(UTF-16) → 커널 cmdline</text>
  <text x="22" y="148" font-size="12">2. 커널을 정렬된 주소로 재배치 (필요하면)</text>
  <text x="22" y="172" font-size="12">3. initrd: LoadFile2 (INITRD_MEDIA GUID)</text>
  <text x="22" y="196" font-size="12">4. DTB: dtb= 또는 config table, 없으면 빈 DTB</text>
  <text x="22" y="220" font-size="12">5. /chosen에 uefi-system-table · mmap 기록</text>
  <text x="22" y="244" font-size="12">6. GetMemoryMap → ExitBootServices</text>
  <text x="22" y="268" font-size="12">7. SetVirtualAddressMap (Runtime용)</text>
  <line x1="210" y1="292" x2="210" y2="314" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="220" y="308" font-size="12">efi_enter_kernel(x0 = 새 FDT)</text>
  <line x1="570" y1="54" x2="570" y2="314" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="580" y="180" font-size="12">offset 0</text>
  <text x="580" y="196" font-size="12">x0 = DTB</text>
  <rect x="10" y="316" width="660" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="338" text-anchor="middle" font-size="12">primary_entry — 여기서부터 두 경로는 똑같다 (MMU off, x0 = FDT 물리 주소)</text>
</svg>
```
그림 5 — EFI stub 핸드오버. 왼쪽 경로에서 stub은 "작은 부트로더"로서 U-Boot가 `booti` 전에 하던 일(cmdline·initrd·DTB 준비)을 UEFI API로 직접 한 뒤 같은 `primary_entry`로 들어간다. ACPI 머신에서도 커널은 항상 FDT 하나를 받는다 — `/chosen`만 있는 빈 DTB다.

lab 소스(`drivers/firmware/efi/libstub/`)로 따라가면: `efi-stub-entry.c`의 `efi_pe_entry()` → `efi-stub.c`의 `efi_stub_common()`(여기서 `efi_load_initrd()`) → `fdt.c`의 `efi_boot_kernel()` → `allocate_new_fdt_and_exit_boot()`(DTB 선택: "Using DTB from command line" / "Using DTB from configuration table" / "Generating empty DTB", 그리고 `efi_exit_boot_services()` 재시도 루프) → `arm64.c`의 `efi_enter_kernel()`.

initrd 전달 방식이 중요하다. 예전에는 커널 cmdline `initrd=\path\file`을 stub이 같은 장치의 파일시스템에서 직접 읽었다(`CONFIG_EFI_GENERIC_STUB_INITRD_CMDLINE_LOADER`, 이 랩 config에는 꺼져 있다). 지금 권장 방식은 **로더(GRUB·U-Boot·EDK2)가 `LINUX_EFI_INITRD_MEDIA_GUID` 디바이스 패스에 `EFI_LOAD_FILE2_PROTOCOL`을 설치**하고, stub이 "initrd를 달라"고 그 프로토콜을 부르는 것이다. 로더가 파일 위치·검증을 책임지고 stub은 단순해진다.

### 6.3 로그 레벨 함정 — `EFI stub:` 줄은 `efi=debug`를 줘야 보인다

랩 커널을 `bootefi`로 처음 부팅했을 때는 `EFI stub:` 줄이 **하나도** 안 나왔다. 소스를 보면 이유가 있다.

```sh
cd /var/lab/src/linux-6.18.55/drivers/firmware/efi/libstub
grep -n "efi_loglevel =" printk.c efi-stub-helper.c
```

```text
efi-stub-helper.c:77:			efi_loglevel = CONSOLE_LOGLEVEL_QUIET;
efi-stub-helper.c:100:				efi_loglevel = CONSOLE_LOGLEVEL_DEBUG;
printk.c:14:int efi_loglevel = LOGLEVEL_NOTICE;
```

출력에서 볼 것: stub의 기본 출력 레벨은 NOTICE(5)라서 `efi_info()`(INFO=6) 메시지가 숨는다. cmdline에 `efi=debug`가 있으면 DEBUG로 올라가고, `quiet`이면 더 내려간다. 그래서 이 노트의 모든 bootefi 실험은 `efi=debug`를 붙였다.

### 6.4 VM 호스트에서 본 stub의 흔적 — ACPI 머신의 "빈 DTB"

§3.1에서 VM에 `/proc/device-tree`가 없는데 `/sys/firmware/fdt`는 있었다. 이것이 바로 stub이 만든 "빈 DTB"다.

```sh
sudo cat /sys/firmware/fdt > c8_host.fdt; dtc -I dtb -O dts c8_host.fdt
```

```text
/dts-v1/;

/ {
	#size-cells = <0x02>;
	#address-cells = <0x02>;

	chosen {
		linux,uefi-secure-boot = <0x02>;
		kaslr-seed = <0x00 0x00>;
		linux,uefi-mmap-desc-ver = <0x01>;
		linux,uefi-mmap-desc-size = <0x30>;
		linux,uefi-mmap-size = <0x750>;
		linux,uefi-mmap-start = <0x03 0x6fbf7540>;
		linux,uefi-system-table = <0x03 0x6fbd0018>;
		bootargs = "BOOT_IMAGE=/vmlinuz-7.0.0-38-generic root=UUID=e8c86924-872a-4501-b66c-34b21fe8c63f ro console=tty1 console=ttyAMA0";
	};
};
```

출력에서 볼 것: 하드웨어 노드가 하나도 없고 `/chosen`만 있다. stub은 "EFI System Table 주소, EFI 메모리 맵 위치, cmdline"을 이 FDT에 적어 커널에 넘긴다 — **arm64 커널의 입구 규약은 항상 "x0 = FDT"**이고, ACPI 머신에서도 그 규약을 유지하려고 빈 DTB를 만든다. 커널은 이 FDT가 `/chosen`뿐이면(`dt_is_stub()`) ACPI를 쓰기로 결정한다(§9). `linux,uefi-secure-boot`는 VM의 7.0 커널이 넣은 속성으로 랩 6.18 소스에는 없다. 값 2는 커널 `enum efi_secureboot_mode`의 순서(unset, unknown, disabled, enabled)로 보면 "disabled"이고 `mokutil` 결과와 맞는다.

---

## 7. ACPI vs Device Tree — 하드웨어를 설명하는 두 언어

### 7.1 직관

둘 다 "커널이 탐색(enumerate)할 수 없는 하드웨어 — 메모리 맵된 UART, 인터럽트 컨트롤러, 타이머, I2C 센서 — 가 어디에 있는지"를 알려 주는 자료다. 차이는 **누가 들고 있고, 얼마나 똑똑한가**다.

| 측면 | Device Tree | ACPI |
|---|---|---|
| 형태 | 정적 자료 구조(노드·속성 트리), `.dts` → `.dtb` | 테이블(고정 형식 C 구조체) + **AML 바이트코드**(DSDT/SSDT) |
| 누가 제공 | 보통 커널 소스 트리와 함께 빌드, 부트로더가 전달 | **펌웨어가 ROM에 들고 있다** (OS 독립) |
| 장치 식별 | `compatible = "arm,pl011"` 문자열 | `_HID "ARMH0011"`(하드웨어 ID), `_CID`(호환 ID) |
| 자원 | `reg`, `interrupts`, `clocks`, `*-gpios` | `_CRS`(Current Resource Settings) 메서드 |
| 클럭·전원·핀 | 커널 드라이버가 직접 제어(clk, regulator, pinctrl 프레임워크) | 펌웨어 메서드가 숨김: `_PS0`/`_PS3`(전원 상태), `_ON`/`_OFF` |
| 실행 코드 | 없음 | 있음 — 커널의 ACPICA 인터프리터가 AML을 실행 |
| 장점 | 단순, 투명, 커널이 모든 것을 제어, 저전력 SoC 세밀 제어 | OS 하나로 수많은 보드 지원, 펌웨어가 플랫폼 차이를 흡수 |
| 주 무대 | 임베디드 SoC, 로봇·카메라·Jetson | x86 전체, arm64 서버(SBSA/SBBR), Windows on Arm |

Don에게 맞춘 비유: DT는 **"레지스터 맵 문서"**, ACPI는 **"레지스터 맵 문서 + 펌웨어가 제공하는 HAL 함수"**다. SSD에서 상위 FW가 HAL 함수(`hal_nand_power_on()`)만 부르고 실제 레지스터 시퀀스는 모르게 했던 것처럼, ACPI는 클럭·전원 시퀀스를 펌웨어 AML 안에 숨긴다. 서버 벤더에게는 좋고(OS를 고칠 필요 없음), 임베디드 엔지니어에게는 답답하다(디버깅할 때 AML을 디스어셈블해야 함).

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="18" text-anchor="middle" font-size="14" font-weight="bold">ACPI (펌웨어 ROM)</text>
  <text x="520" y="18" text-anchor="middle" font-size="14" font-weight="bold">Device Tree (DTB)</text>
  <rect x="10" y="30" width="90" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="55" y="50" text-anchor="middle" font-size="12">RSDP</text>
  <rect x="120" y="30" width="90" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="50" text-anchor="middle" font-size="12">XSDT</text>
  <line x1="100" y1="45" x2="118" y2="45" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="230" y="30" width="100" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="280" y="48" text-anchor="middle" font-size="12">FADT → DSDT</text>
  <rect x="230" y="62" width="100" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="280" y="80" text-anchor="middle" font-size="12">MADT (GIC)</text>
  <rect x="230" y="94" width="100" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="280" y="112" text-anchor="middle" font-size="12">GTDT (타이머)</text>
  <rect x="230" y="126" width="100" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="280" y="144" text-anchor="middle" font-size="12">SPCR (콘솔)</text>
  <rect x="230" y="158" width="100" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="280" y="176" text-anchor="middle" font-size="12">MCFG · IORT</text>
  <line x1="210" y1="45" x2="228" y2="43" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="210" y1="50" x2="228" y2="75" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="210" y1="54" x2="228" y2="107" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="210" y1="56" x2="228" y2="139" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="210" y1="58" x2="228" y2="171" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="10" y="80" width="200" height="104" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="100" font-size="12">DSDT (AML)</text>
  <text x="20" y="120" font-size="12">Device (COM0) {</text>
  <text x="30" y="138" font-size="12">_HID "ARMH0011"</text>
  <text x="30" y="156" font-size="12">_CRS: Memory32Fixed,</text>
  <text x="30" y="174" font-size="12">Interrupt { 33 } }</text>
  <rect x="380" y="30" width="290" height="154" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="390" y="52" font-size="12">/ {</text>
  <text x="400" y="72" font-size="12">pl011@9000000 {</text>
  <text x="412" y="92" font-size="12">compatible = "arm,pl011",</text>
  <text x="412" y="110" font-size="12">"arm,primecell";</text>
  <text x="412" y="130" font-size="12">reg = &lt;0x0 0x9000000 0x0 0x1000&gt;;</text>
  <text x="412" y="150" font-size="12">interrupts = &lt;0 1 4&gt;;</text>
  <text x="400" y="170" font-size="12">}; chosen { stdout-path … } };</text>
  <rect x="10" y="226" width="300" height="54" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="160" y="248" text-anchor="middle" font-size="12">platform_driver "sbsa-uart"</text>
  <text x="160" y="268" text-anchor="middle" font-size="12">.acpi_match_table = { "ARMH0011" }</text>
  <rect x="370" y="226" width="300" height="54" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="520" y="248" text-anchor="middle" font-size="12">amba_driver "uart-pl011"</text>
  <text x="520" y="268" text-anchor="middle" font-size="12">PrimeCell ID 0x00041011 매칭</text>
  <line x1="110" y1="184" x2="140" y2="224" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="520" y1="184" x2="520" y2="224" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="170" y="306" width="340" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="331" text-anchor="middle" font-size="12">같은 drivers/tty/serial/amba-pl011.c</text>
  <line x1="160" y1="280" x2="250" y2="304" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="520" y1="280" x2="430" y2="304" stroke="currentColor" marker-end="url(#ah6)"/>
</svg>
```
그림 6 — 같은 PL011 UART를 ACPI와 DT가 설명하는 방식. ACPI는 루트 포인터(RSDP)에서 테이블을 따라가고 장치는 DSDT의 AML `Device()` 블록으로 표현된다(왼쪽 DSDT 코드는 개념 예시로, 실제 디스어셈블 결과가 아니다). DT는 노드 하나에 모든 정보가 있다. 커널에서는 같은 소스 파일의 서로 다른 드라이버 구조체가 각각 매칭된다(§7.5에서 실측).

### 7.2 주요 ACPI 테이블 (arm64 기준)

| 테이블 | 이름 | 담는 것 | DT 대응 |
|---|---|---|---|
| RSDP | Root System Description Pointer | XSDT 주소. UEFI config table로 OS에 전달 | DTB 시작 주소(x0) |
| XSDT | Extended System Description Table | 나머지 테이블 포인터 배열 | 루트 노드 |
| FADT (`FACP`) | Fixed ACPI Description Table | DSDT 주소, 플래그(HW_REDUCED_ACPI), arm 부트 플래그(PSCI 지원·HVC/SMC) | `/psci` 노드의 `method` |
| MADT (`APIC`) | Multiple APIC Description Table | arm64에선 GIC: GICC(CPU별), GICD, GICR, ITS, MSI frame | `cpus` + `intc@...` (GIC) 노드 |
| GTDT | Generic Timer Description Table | Arch timer 인터럽트, watchdog | `timer` 노드 |
| SPCR | Serial Port Console Redirection | 콘솔 UART 종류·주소·보율 | `/chosen/stdout-path` |
| DBG2 | Debug Port Table 2 | 디버그 포트 | — |
| MCFG | PCI Memory-mapped Config | PCIe ECAM 주소 | `pcie@...` 노드의 `reg` |
| IORT | IO Remapping Table | SMMU·ITS와 장치 ID 매핑 | `iommu-map`, `msi-map` |
| PPTT | Processor Properties Topology | 클러스터·캐시 토폴로지 | `cpu-map`, 캐시 노드 |
| DSDT / SSDT | Differentiated / Secondary System Description | **AML**: 열거 불가 장치들의 `Device()` 블록, 메서드 | 나머지 모든 장치 노드 |

AML(ACPI Machine Language)은 ASL(ACPI Source Language)을 `iasl`로 컴파일한 바이트코드다. 커널은 ACPICA(Intel이 관리하는 OS 독립 인터프리터, 위 로그의 "ACPI: Core revision 20251212")로 이를 실행한다. arm64 ACPI는 **HW-reduced ACPI**만 쓴다 — x86의 PM 타이머·SCI 같은 "고정 하드웨어"가 없고 모든 것을 GIC·일반 장치로 표현한다.

### 7.3 C로 ACPI 테이블 걸어 다니기

무엇을 확인하는 코드인가: `/sys/firmware/acpi/tables/`의 각 테이블 헤더(36바이트: 시그니처, 길이, 리비전, 체크섬, OEM ID…)를 읽어 체크섬(전체 바이트 합 = 0)을 검증하고, MADT의 GIC 서브테이블 개수, FADT의 HW-reduced/PSCI 플래그, DSDT의 `_HID` 값을 뽑는다. `_HID`는 AML에서 `NameOp(0x08) "_HID"` 다음에 문자열(0x0D) 또는 압축 EISA ID(DWordConst 0x0C)로 나온다. 정적 링크로 빌드해 QEMU 게스트(BusyBox initramfs)에서도 그대로 돌린다.

```c
/* c8_acpi.c - walk /sys/firmware/acpi/tables: header, checksum, MADT GIC entries, DSDT _HID */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
static uint8_t buf[1 << 20];
static long load(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    long n = (long)fread(buf, 1, sizeof buf, f);
    return fclose(f), n;
}
static void eisaid(uint32_t v, char *o)    /* compressed EISA id -> "PNP0A08" */
{
    v = __builtin_bswap32(v);
    o[0] = '@' + ((v >> 26) & 31); o[1] = '@' + ((v >> 21) & 31); o[2] = '@' + ((v >> 16) & 31);
    sprintf(o + 3, "%04X", v & 0xffff);
}
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/sys/firmware/acpi/tables";
    DIR *d = opendir(dir);
    if (!d) { perror(dir); exit(1); }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || e->d_type == DT_DIR) continue;
        long n = load(dir, e->d_name);
        uint8_t sum = 0;
        for (long i = 0; i < n; i++) sum += buf[i];
        printf("%.4s len=%-5u rev=%u OEM='%.6s' table='%.8s' checksum=%s\n", (char *)buf,
               *(uint32_t *)(buf + 4), buf[8], (char *)buf + 10, (char *)buf + 16, sum ? "BAD" : "ok");
        if (!memcmp(buf, "APIC", 4)) {             /* MADT: 44-byte header, then subtables */
            int cnt[256] = {0};
            for (long off = 44; off + 2 <= n && buf[off + 1]; off += buf[off + 1]) cnt[buf[off]]++;
            printf("     MADT: GICC(cpu)=%d GICD=%d GIC-MSI=%d GICR=%d ITS=%d\n",
                   cnt[0x0b], cnt[0x0c], cnt[0x0d], cnt[0x0e], cnt[0x0f]);
        }
        if (!memcmp(buf, "FACP", 4) && n >= 131)   /* FADT flags @112, ARM boot flags @129 */
            printf("     FADT: HW_REDUCED_ACPI=%u  ARM PSCI_COMPLIANT=%u PSCI_USE_HVC=%u\n",
                   (*(uint32_t *)(buf + 112) >> 20) & 1, buf[129] & 1, (buf[129] >> 1) & 1);
        if (!memcmp(buf, "DSDT", 4) || !memcmp(buf, "SSDT", 4)) {
            printf("     _HID:");                     /* NameOp(0x08) "_HID" then String or DWord */
            for (long i = 36; i + 14 < n; i++) {
                if (buf[i] != 0x08 || memcmp(buf + i + 1, "_HID", 4)) continue;
                const uint8_t *p = buf + i + 5;
                char id[16];
                if (*p == 0x0d) snprintf(id, sizeof id, "%s", (const char *)p + 1);
                else if (*p == 0x0c) eisaid(*(const uint32_t *)(p + 1), id);
                if (*p == 0x0d || *p == 0x0c) printf(" %s", id);
            }
            printf("\n");
        }
    }
    return closedir(d);
}
```

VM 호스트(Apple Virtualization)에서:

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static $S/c8_acpi.c -o c8_acpi && sudo ./c8_acpi
```

```text
MCFG len=60    rev=1 OEM='APPLE ' table='Apple Vz' checksum=ok
GTDT len=104   rev=3 OEM='APPLE ' table='Apple Vz' checksum=ok
APIC len=588   rev=5 OEM='APPLE ' table='Apple Vz' checksum=ok
     MADT: GICC(cpu)=6 GICD=1 GIC-MSI=1 GICR=1 ITS=0
DSDT len=974   rev=2 OEM='APPLE ' table='Apple Vz' checksum=ok
     _HID: PNP0A08 PNP0C02 ACPI0007 ACPI0007 ACPI0007 ACPI0007 ACPI0007 ACPI0007 PNP0C0C ARMH0061
FACP len=276   rev=6 OEM='APPLE ' table='Apple Vz' checksum=ok
     FADT: HW_REDUCED_ACPI=1  ARM PSCI_COMPLIANT=1 PSCI_USE_HVC=1
```

출력에서 볼 것: 경고 0개 빌드. MADT의 GICC 6개 = vCPU 6개(dmesg의 "CPU0~CPU5 has been hot-added"와 일치). DSDT의 `_HID`는 `PNP0A08`(PCIe 루트 브리지), `PNP0C02`(예약 자원 — ECAM 영역), `ACPI0007`(프로세서 장치) ×6, `PNP0C0C`(전원 버튼), `ARMH0061`(PL061 GPIO — 전원 버튼 이벤트용). 나머지 장치(디스크·네트워크)는 전부 PCIe 아래에 있어 ACPI에 적을 필요가 없다 — **열거 가능한 버스(PCIe)는 ACPI/DT 어느 쪽이든 설명이 거의 필요 없다**. FADT는 HW-reduced이고 PSCI를 HVC로 부른다(하이퍼바이저 아래 게스트이므로).

### 7.4 같은 커널, 같은 QEMU 보드 — EDK2로 ACPI 부팅 vs DT 부팅

QEMU `virt` 보드는 EDK2(AAVMF)와 함께 쓰면 **ACPI 테이블을 생성**해 준다. `-machine virt,acpi=off`를 주면 ACPI 없이 DT만 넘긴다. 랩 `Image`를 `-kernel`로 넘기면 EDK2가 그것을 EFI 앱으로 실행한다(QEMU fw_cfg 경유).

```sh
cp /usr/share/AAVMF/AAVMF_VARS.fd c8_vars.fd
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 1024M -nographic \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.no-secboot.fd \
  -drive if=pflash,format=raw,file=c8_vars.fd \
  -kernel /var/lab/out/Image -initrd /var/lab/out/initramfs.cpio.gz \
  -append "console=ttyAMA0 efi=debug LAB_CMD=ls~/sys/firmware~/sys/firmware/acpi/tables~/proc/device-tree;cat~/sys/firmware/efi/systab"
```

```text
UEFI firmware (version 2025.11-3ubuntu7.3 built at 15:40:26 on Sep 23 2026)
EFI stub: Booting Linux Kernel...
EFI stub: EFI_RNG_PROTOCOL unavailable
EFI stub: Loaded initrd from LINUX_EFI_INITRD_MEDIA_GUID device path
EFI stub: Generating empty DTB
EFI stub: Exiting boot services...
[    0.000000] efi: EFI v2.7 by Ubuntu distribution of EDK II
[    0.000000] efi: SMBIOS 3.0=0x7fed0000 MEMATTR=0x7c5b8018 ACPI 2.0=0x7c69d018 INITRD=0x7c5baf18 MEMRESERVE=0x7c5baf98 
[    0.000000] ACPI: RSDP 0x000000007C69D018 000024 (v02 BOCHS )
[    0.000000] ACPI: XSDT 0x000000007C69DF18 000064 (v01 BOCHS  BXPC     00000001      01000013)
[    0.000000] ACPI: FACP 0x000000007C69DB18 000114 (v06 BOCHS  BXPC     00000001 BXPC 00000001)
[    0.000000] ACPI: DSDT 0x000000007C63C018 0014D9 (v02 BOCHS  BXPC     00000001 BXPC 00000001)
[    0.000000] ACPI: APIC 0x000000007C69DC98 0000AC (v04 BOCHS  BXPC     00000001 BXPC 00000001)
[    0.000000] ACPI: PPTT 0x000000007C69DD98 000060 (v02 BOCHS  BXPC     00000001 BXPC 00000001)
[    0.000000] ACPI: GTDT 0x000000007C69D098 000068 (v03 BOCHS  BXPC     00000001 BXPC 00000001)
[    0.000000] ACPI: MCFG 0x000000007C69DA98 00003C (v01 BOCHS  BXPC     00000001 BXPC 00000001)
[    0.000000] ACPI: SPCR 0x000000007C69DE98 000050 (v02 BOCHS  BXPC     00000001 BXPC 00000001)
[    0.000000] ACPI: DBG2 0x000000007C69D818 000057 (v00 BOCHS  BXPC     00000001 BXPC 00000001)
[    0.000000] ACPI: IORT 0x000000007C69D898 000080 (v03 BOCHS  BXPC     00000001 BXPC 00000001)
[    0.000000] ACPI: SPCR: console: pl011,mmio32,0x9000000,9600
[    0.000000] psci: probing for conduit method from ACPI.
[    0.000000] Kernel command line: initrd=initrd console=ttyAMA0 efi=debug LAB_CMD=ls~/sys/firmware~/sys/firmware/acpi/tables~/proc/device-tree;cat~/sys/firmware/efi/systab
lab initramfs: 6.18.55 up 2.33s
ls: /proc/device-tree: No such file or directory
/sys/firmware:
acpi        devicetree  dmi         efi         fdt

/sys/firmware/acpi/tables:
APIC     DSDT     GTDT     MCFG     SPCR     dynamic
DBG2     FACP     IORT     PPTT     data
ACPI20=0x7c69d018
SMBIOS3=0x7fed0000
```
(커널 로그는 `efi|acpi|psci|Kernel command` 관련 줄만 남겼다.)

이번에는 `-machine virt,acpi=off`, 나머지 동일:

```text
UEFI firmware (version 2025.11-3ubuntu7.3 built at 15:40:26 on Sep 23 2026)
EFI stub: Booting Linux Kernel...
EFI stub: EFI_RNG_PROTOCOL unavailable
EFI stub: Loaded initrd from LINUX_EFI_INITRD_MEDIA_GUID device path
EFI stub: Using DTB from configuration table
EFI stub: Exiting boot services...
[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
[    0.000000] Machine model: linux,dummy-virt
[    0.000000] psci: probing for conduit method from DT.
[    2.040285] ACPI: Interpreter disabled.
[    2.215792] pnp: PnP ACPI: disabled
lab initramfs: 6.18.55 up 5.76s
/proc/device-tree:
aliases               pmu                   virtio_mmio@a002000
apb-pclk              psci                  virtio_mmio@a002200
chosen                timer                 virtio_mmio@a002400
...
pl011@9000000         virtio_mmio@a001800   virtio_mmio@a003e00
pl031@9010000         virtio_mmio@a001a00
/sys/firmware:
devicetree  dmi         efi         fdt
SMBIOS3=0x7fed0000
```
(커널 로그는 관련 줄만, `/proc/device-tree` 목록 중간은 생략.)

출력에서 볼 것 — 이 두 실행이 이 노트의 핵심 실험이다.

- 펌웨어(EDK2)와 커널(`Image`)이 **완전히 같은데**, 펌웨어가 ACPI를 내놓으면 stub은 "Generating empty DTB", 커널은 ACPI 테이블을 파싱하고 `psci: ... from ACPI`, 콘솔은 SPCR에서 찾는다. `Machine model:` 줄이 아예 없다(DT의 `model` 속성에서 나오는 줄이다).
- ACPI를 끄면 EDK2는 QEMU가 만든 DTB를 **UEFI configuration table**로 설치하고, stub은 "Using DTB from configuration table", 커널은 `Machine model: linux,dummy-virt`, `psci: ... from DT`, `/proc/device-tree`가 생긴다. 이것이 Jetson Orin 같은 **UEFI + DT** 조합이 동작하는 방식이다.
- EDK2는 QEMU `-kernel` 경로에서 cmdline 앞에 `initrd=initrd`를 붙인다(커널은 이 값을 쓰지 않고, initrd는 LoadFile2로 받았다).

### 7.5 드라이버는 어떻게 매칭되나 — `ARMH0011` vs `arm,pl011`

무엇을 확인하나: 같은 UART가 ACPI 모드와 DT 모드에서 **어느 버스·어느 드라이버**에 붙는지 sysfs로 본다. 게스트 initramfs에 넣을 작은 스크립트다.

```sh
#!/bin/sh
# c8_dev.sh - which bus/driver/ID matched the UART and virtio-mmio devices?
for d in /sys/bus/platform/devices/ARMH0011:00 /sys/bus/amba/devices/9000000.pl011 \
         /sys/bus/platform/devices/LNRO0005:00 /sys/bus/platform/devices/a000000.virtio_mmio; do
  [ -e "$d" ] || continue
  drv=none; [ -e "$d/driver" ] && drv=$(basename "$(readlink "$d/driver")")
  echo "$d"
  echo "    driver=$drv"
  [ -e "$d/modalias" ] && echo "    modalias=$(cat "$d/modalias")"
  [ -e "$d/of_node/compatible" ] && echo "    of compatible=$(tr '\0' ' ' < "$d/of_node/compatible")"
  [ -e "$d/firmware_node" ] && echo "    firmware_node=$(basename "$(readlink "$d/firmware_node")") path=$(cat "$d/firmware_node/path")"
done
```

```sh
mkdir c8_rootfs && cp -a /var/lab/build/rootfs/. c8_rootfs/
cp c8_acpi c8_rootfs/bin/; install -m755 $S/c8_dev.sh c8_rootfs/bin/c8_dev.sh
(cd c8_rootfs && find . | cpio -o -H newc --quiet | gzip) > c8_initrd.gz
# 위 EDK2 명령에서 -initrd c8_initrd.gz -append "console=ttyAMA0 quiet LAB_CMD=c8_dev.sh"
# 를 acpi=on / acpi=off 두 번 실행
```

```text
=== acpi=on
lab initramfs: 6.18.55 up 3.22s
/sys/bus/platform/devices/ARMH0011:00
    driver=sbsa-uart
    modalias=acpi:ARMH0011:
    firmware_node=ARMH0011:00 path=\_SB_.COM0
/sys/bus/platform/devices/LNRO0005:00
    driver=none
    modalias=acpi:LNRO0005:
    firmware_node=LNRO0005:00 path=\_SB_.VR00
=== acpi=off
lab initramfs: 6.18.55 up 3.40s
/sys/bus/amba/devices/9000000.pl011
    driver=uart-pl011
    of compatible=arm,pl011 arm,primecell 
/sys/bus/platform/devices/a000000.virtio_mmio
    driver=none
    modalias=of:Nvirtio_mmioT(null)Cvirtio,mmio
    of compatible=virtio,mmio 
```

출력에서 볼 것:

- **ACPI 모드**: 장치 이름이 `<_HID>:<인스턴스>`(`ARMH0011:00`), ACPI 네임스페이스 경로는 `\_SB_.COM0`, modalias는 `acpi:ARMH0011:`. 붙은 드라이버는 platform 드라이버 **`sbsa-uart`**다.
- **DT 모드**: 장치 이름이 `<unit-address>.<노드 이름>`(`9000000.pl011`), 버스는 **amba**, 드라이버는 **`uart-pl011`**. `arm,primecell` compatible 때문에 AMBA 버스 장치로 만들어지고, AMBA 버스는 하드웨어의 PrimeCell ID 레지스터(오프셋 0xFE0~0xFFC)를 읽어 매칭한다.
- virtio-mmio 슬롯은 장치가 연결되지 않은 빈 슬롯이라 두 모드 모두 `driver=none`이다(드라이버 probe가 "장치 없음"으로 빠진다). `LNRO0005`는 Linaro가 virtio-mmio에 할당한 ACPI ID다.
- QEMU ACPI 게스트에서 `c8_acpi`를 돌리면 이 DSDT에 `ARMH0011`, `LNRO0005`×32, `QEMU0002`(fw_cfg), `ACPI0013`(GED, 일반 이벤트 장치) 등이 보인다:

```text
lab initramfs: 6.18.55 up 1.41s
SPCR len=80    rev=2 OEM='BOCHS ' table='BXPC    ' checksum=ok
MCFG len=60    rev=1 OEM='BOCHS ' table='BXPC    ' checksum=ok
GTDT len=104   rev=3 OEM='BOCHS ' table='BXPC    ' checksum=ok
APIC len=172   rev=4 OEM='BOCHS ' table='BXPC    ' checksum=ok
     MADT: GICC(cpu)=1 GICD=1 GIC-MSI=1 GICR=0 ITS=0
IORT len=128   rev=3 OEM='BOCHS ' table='BXPC    ' checksum=ok
DSDT len=5337  rev=2 OEM='BOCHS ' table='BXPC    ' checksum=ok
     _HID: ACPI0007 ARMH0011 QEMU0002 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 LNRO0005 PNP0C0F PNP0C0F PNP0C0F PNP0C0F PNP0A08 PNP0C02 ACPI0013 PNP0C0C PNP0C33
DBG2 len=87    rev=0 OEM='BOCHS ' table='BXPC    ' checksum=ok
FACP len=276   rev=6 OEM='BOCHS ' table='BXPC    ' checksum=ok
     FADT: HW_REDUCED_ACPI=1  ARM PSCI_COMPLIANT=1 PSCI_USE_HVC=1
PPTT len=96    rev=2 OEM='BOCHS ' table='BXPC    ' checksum=ok
[    1.688135] reboot: Power down
```

드라이버 쪽 코드(랩 소스 `drivers/tty/serial/amba-pl011.c`, 발췌)를 보면 매칭 테이블 두 개가 나란히 있다:

```c
static const struct of_device_id sbsa_uart_of_match[] = {
	{ .compatible = "arm,sbsa-uart", },
	{},
};
MODULE_DEVICE_TABLE(of, sbsa_uart_of_match);

static const struct acpi_device_id sbsa_uart_acpi_match[] = {
	{ "ARMH0011", 0 },
	{ "ARMHB000", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, sbsa_uart_acpi_match);

static struct platform_driver arm_sbsa_uart_platform_driver = {
	.probe		= sbsa_uart_probe,
	.remove		= sbsa_uart_remove,
	.driver	= {
		.name	= "sbsa-uart",
		.of_match_table = sbsa_uart_of_match,
		.acpi_match_table = sbsa_uart_acpi_match,
	},
};
```
(실제 소스에는 `.pm`, `.suppress_bind_attrs` 줄이 더 있다.)

드라이버 작성자에게 주는 교훈: **ACPI 서버에서도 돌아야 하는 드라이버는 `.acpi_match_table`을 추가하고, 자원 읽기를 `of_property_read_u32()` 대신 `device_property_read_u32()`(fwnode API)로 쓴다**. 그러면 DT 속성이든 ACPI `_DSD`(Device Specific Data) 속성이든 같은 코드로 읽힌다. ACPI에는 `_HID "PRP0001"` + `_DSD`에 `compatible` 속성을 넣으면 **DT의 `of_match_table`로 매칭시키는 다리**도 있다 (docs.kernel.org "ACPI Based Device Enumeration"). 깊은 내용은 G 모듈(디바이스 모델)에서 다룬다.

---

## 8. U-Boot가 UEFI를 구현한다 — EBBR과 `bootefi`

### 8.1 왜 U-Boot에 UEFI가?

배포판(Debian, Fedora, Ubuntu, openSUSE)은 arm64 설치 이미지를 **하나**만 만들고 싶다. 서버는 UEFI로 그 이미지를 부팅한다. 임베디드 보드도 같은 이미지를 쓰게 하려면 U-Boot가 UEFI 인터페이스를 제공하면 된다. 그래서 U-Boot에 `CONFIG_EFI_LOADER`가 생겼고(2016년경, Alexander Graf가 시작한 것으로 기억), Arm의 **EBBR**(Embedded Base Boot Requirements)이 "임베디드 보드가 제공해야 할 UEFI 부분집합 + DT 허용"을 규정했다. U-Boot는 PI 단계(SEC/PEI/DXE) 없이 **바깥 인터페이스만** 구현한다: Boot/Runtime Services 일부, 블록·파일시스템·콘솔·네트워크 프로토콜, 변수(파일 또는 플래시), 부트 매니저, Secure Boot 일부, capsule 업데이트.

랩 U-Boot의 설정:

```sh
grep -E "^CONFIG_(EFI_LOADER|CMD_BOOTEFI|EFI_LOAD_FILE2_INITRD|EFI_BOOTMGR|EFI_VARIABLE_FILE_STORE|EFI_EBBR_2_1_CONFORMANCE|CMD_EFIDEBUG|CMD_QFW)=" /var/lab/build/u-boot/.config
```

```text
CONFIG_EFI_LOADER=y
CONFIG_EFI_VARIABLE_FILE_STORE=y
CONFIG_EFI_LOAD_FILE2_INITRD=y
CONFIG_EFI_EBBR_2_1_CONFORMANCE=y
CONFIG_EFI_BOOTMGR=y
CONFIG_CMD_BOOTEFI=y
CONFIG_CMD_EFIDEBUG=y
CONFIG_CMD_QFW=y
```

### 8.2 실험 — `bootefi`로 랩 커널 부팅 (그리고 처음에 실패한 이유)

U-Boot 문서(`doc/usage/cmd/bootefi.rst`, v2026.10)의 문법은 `bootefi <image_addr>[:<image_size>] [<initrd_addr>:<initrd_size>] [<fdt_address>]`다. 커널과 initramfs는 QEMU `-kernel`/`-initrd`로 넘기고 U-Boot의 `qfw load`(fw_cfg에서 메모리로 복사, `filesize`에 initrd 크기를 넣는다)로 메모리에 올린다. U-Boot 콘솔은 `expect` 스크립트로 조작한다.

```text
#!/usr/bin/expect -f
# usage: c8_uboot.exp <cmdfile> [extra qemu args...]
# boots U-Boot with -kernel/-initrd (fw_cfg), stops autoboot, runs each line of cmdfile
set timeout 180
set cmdfile [lindex $argv 0]
set extra [lrange $argv 1 end]
set f [open $cmdfile r]; set cmds [split [string trim [read $f]] "\n"]; close $f
eval spawn qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -bios /var/lab/out/u-boot.bin -kernel /var/lab/out/Image -initrd /var/lab/out/initramfs.cpio.gz $extra
expect "Hit any key to stop autoboot"
send "\r"
foreach c $cmds {
  expect "=> "
  send "$c\r"
}
expect {
  "reboot: Power down" {}
  eof {}
  timeout {puts "\n*** TIMEOUT"}
}
```

실행: `expect $S/c8_uboot.exp $S/c8_bootefi.cmds > c8_bootefi.log 2>&1` (명령 파일 한 줄 = U-Boot 명령 하나). §8.5·§8.6의 디스크 실험은 `-kernel`/`-initrd` 대신 `-drive if=none,file=c8_esp_ub.img,format=raw,id=d0 -device virtio-blk-device,drive=d0`를 넘기는 같은 형태의 스크립트(`c8_ub.exp`)로 돌렸다.

첫 시도(명령 파일 내용과 콘솔 출력):

```text
=> printenv kernel_addr_r ramdisk_addr_r fdt_addr fdt_addr_r fdtcontroladdr bootcmd
kernel_addr_r=0x40400000
ramdisk_addr_r=0x44000000
fdt_addr=0x40000000
## Error: "fdt_addr_r" not defined
fdtcontroladdr=5e34ed80
bootcmd=bootflow scan -lb
=> qfw load ${kernel_addr_r} ${ramdisk_addr_r}
loading kernel to address 40400000 size 28c2a00 initrd 44000000 size 1277e2
=> printenv filesize
filesize=1277e2
=> bootefi ${kernel_addr_r} ${ramdisk_addr_r}:${filesize} ${fdt_addr}
Cannot persist EFI variables without system partition
Missing TPMv2 device for EFI_TCG_PROTOCOL
Missing RNG device for EFI_RNG_PROTOCOL
No UEFI binary known at 0x40400000
=> setenv bootargs console=ttyAMA0 efi=debug LAB_CMD=cat~/proc/cmdline;ls~/sys/firmware~...
syntax error
```
(위 첫 블록은 별도 실행의 출력, `setenv` 줄은 길어서 `...`로 줄였다.)

출력에서 볼 것 — 정직하게 기록한 두 가지 실패:

- **`No UEFI binary known at 0x40400000`**: `bootefi`는 `load`(파일시스템에서 읽기)로 올린 이미지를 기억해서 그 장치 경로를 LoadedImage 프로토콜에 넣는다. `qfw load`는 그 기록을 남기지 않으므로 주소만으로는 거부된다. 문서대로 **`:<image_size>`를 붙이면** 메모리 디바이스 패스(`MemoryMapped(...)`)로 실행된다. 이미지 크기는 `ls -l Image` = 42740224 = `0x28c2000`.
- **`syntax error`**: U-Boot hush 셸에서 `;`는 명령 구분자다. `bootargs`에 `;`·`|`를 넣으려면 큰따옴표로 감싸야 한다.
- QEMU virt `fdt_addr=0x40000000`: QEMU가 DTB를 RAM 시작에 놓아 둔다. `fdt_addr_r`은 이 보드 기본 환경에 없다.

고친 명령과 결과:

```text
=> qfw load ${kernel_addr_r} ${ramdisk_addr_r}
=> setenv bootargs "console=ttyAMA0 efi=debug LAB_CMD=cat~/proc/cmdline;ls~/sys/firmware~/sys/firmware/efi;cat~/sys/firmware/efi/systab;ls~/proc/device-tree/chosen;..."
=> bootefi ${kernel_addr_r}:0x28c2000 ${ramdisk_addr_r}:${filesize} ${fdt_addr}
Cannot persist EFI variables without system partition
Missing TPMv2 device for EFI_TCG_PROTOCOL
Missing RNG device for EFI_RNG_PROTOCOL
Booting <NULL>
EFI stub: Booting Linux Kernel...
EFI stub: EFI_RNG_PROTOCOL unavailable
EFI stub: Loaded initrd from LINUX_EFI_INITRD_MEDIA_GUID device path
EFI stub: Using DTB from configuration table
EFI stub: Exiting boot services...
[    0.000000] Machine model: linux,dummy-virt
[    0.000000] efi: Getting UEFI parameters from /chosen in DT:
[    0.000000] efi:   System Table         : 0x000000005f48ff98
[    0.000000] efi:   MemMap Address       : 0x000000005fec1068
[    0.000000] efi:   MemMap Size          : 0x0000000000000410
[    0.000000] efi:   MemMap Desc. Size    : 0x0000000000000028
[    0.000000] efi:   MemMap Desc. Version : 0x0000000000000001
[    0.000000] efi: EFI v2.11 by Das U-Boot
[    0.000000] efi: RTPROP=0x5ffd9040 SMBIOS 3.0=0x5e48b000 INITRD=0x5fec3040 MEMRESERVE=0x5fec2040 
[    0.000000] efi: Processing EFI memory map:
[    0.000000] psci: probing for conduit method from DT.
[    0.116965] Remapping and enabling EFI services.
[    0.288407] DMI: linux dummy-virt/dummy-virt, BIOS 2026.10 10/01/2026
[    0.529307] ACPI: Interpreter disabled.
[    0.573599] efivars: Registered efivars operations
lab initramfs: 6.18.55 up 2.31s
/sys/firmware:
devicetree  dmi         efi         fdt

/sys/firmware/efi:
efivars           fw_platform_size  systab
SMBIOS3=0x5e48b000
bootargs                   linux,uefi-system-table
kaslr-seed                 name
linux,uefi-mmap-desc-size  smbios3-entrypoint
linux,uefi-mmap-desc-ver   stdout-path
linux,uefi-mmap-size       u-boot,version
linux,uefi-mmap-start
```
(커널 로그는 `efi|acpi|Machine|psci|DMI` 관련 줄만, `bootargs`는 `...`로 줄였다. 게스트 `ls` 출력의 색 코드는 제거했다.)

출력에서 볼 것: "EFI v2.11 **by Das U-Boot**" — 커널은 U-Boot를 UEFI 펌웨어로 인식했다. U-Boot는 ACPI가 아니라 **DT를 configuration table로 설치**했고("Using DTB from configuration table"), 커널은 `Machine model`, `psci ... from DT`, `ACPI: Interpreter disabled`로 DT 모드다. 그런데도 `/sys/firmware/efi`가 있고 `/chosen`에 `linux,uefi-*` 속성이 있다. initrd는 `bootefi` 두 번째 인자로 준 것이 LoadFile2로 전달되었다. U-Boot가 SMBIOS 테이블도 만들어서 `DMI: ... BIOS 2026.10`이 찍힌다.

### 8.3 비교 — 같은 커널을 `booti`로

```text
=> qfw load ${kernel_addr_r} ${ramdisk_addr_r}
=> setenv bootargs "console=ttyAMA0 efi=debug LAB_CMD=cat~/proc/cmdline;ls~/sys/firmware~/sys/firmware/efi;ls~/proc/device-tree/chosen;..."
=> booti ${kernel_addr_r} ${ramdisk_addr_r}:${filesize} ${fdt_addr}
## Flattened Device Tree blob at 40000000
   Booting using the fdt blob at 0x40000000
Working FDT set to 40000000
   Loading Ramdisk to 5d206000, end 5d32d7e2 ... OK
   Loading Device Tree to 000000005d103000, end 000000005d205fff ... OK
Working FDT set to 5d103000

Starting kernel ...

[    0.000000] Machine model: linux,dummy-virt
[    0.000000] efi: Getting UEFI parameters from /chosen in DT:
[    0.000000] efi: UEFI not found.
[    0.000000] psci: probing for conduit method from DT.
[    0.118204] EFI services will not be available.
[    0.830839] ACPI: Interpreter disabled.
lab initramfs: 6.18.55 up 2.41s
ls: /sys/firmware/efi: No such file or directory
/sys/firmware:
devicetree  fdt
bootargs            linux,initrd-start  stdout-path
kaslr-seed          name                u-boot,version
linux,initrd-end    smbios3-entrypoint
```

| 관찰 지점 | `booti` | `bootefi` (U-Boot EFI) | EDK2 + ACPI |
|---|---|---|---|
| 커널 입구 | 오프셋 0 → `primary_entry` | PE 진입점 → stub → `primary_entry` | 같음(stub) |
| `EFI stub:` 줄 | 없음 | 있음 (`efi=debug` 시) | 있음 |
| DTB | U-Boot가 복사·수정해 x0로 | U-Boot config table → stub이 복사·수정 | stub이 빈 DTB 생성 |
| initrd 전달 | `/chosen/linux,initrd-start`·`-end` | LoadFile2 → `INITRD=` config table | LoadFile2 |
| `efi:` 로그 | `UEFI not found.` | `EFI v2.11 by Das U-Boot` | `EFI v2.7 by Ubuntu distribution of EDK II` |
| `/sys/firmware/efi` | 없음 | 있음 | 있음 |
| `psci: probing ... from` | DT | DT | ACPI |
| `Machine model:` | 있음 | 있음 | 없음 |
| 커널이 쓰는 HW 설명 | DT | DT | ACPI |

### 8.4 `efi=debug`와 EFI 메모리 맵

`bootefi` 실행에서 `efi=debug`는 stub 메시지 외에 **펌웨어가 넘긴 메모리 맵 전체**를 찍는다(26줄 중 앞부분):

```text
[    0.000000] efi:   0x000040000000-0x0000579fffff [Conventional|   |  |  |  |  |  |  |  |  |  |   |WB|  |  |  ]
[    0.000000] efi:   0x000057a00000-0x000057bfffff [Loader Data |   |  |  |  |  |  |  |  |  |  |   |WB|  |  |  ]
[    0.000000] efi:   0x000057c00000-0x00005a57ffff [Loader Code |   |  |  |  |  |  |  |  |  |  |   |WB|  |  |  ]
[    0.000000] efi:   0x00005a580000-0x00005a67ffff [Conventional|   |  |  |  |  |  |  |  |  |  |   |WB|  |  |  ]
[    0.000000] efi:   0x00005a680000-0x00005cffffff [Loader Code |   |  |  |  |  |  |  |  |  |  |   |WB|  |  |  ]
[    0.000000] efi:   0x00005d000000-0x00005d000fff [Runtime Data|RUN|  |  |  |  |  |  |  |  |  |   |WB|  |  |  ]
...
[    0.000000] efi:   0x00005d32d000-0x00005e48afff [Boot Code   |   |  |  |  |  |  |  |  |  |  |   |WB|  |  |  ]
[    0.000000] efi:   0x00005f480000-0x00005f49ffff [Runtime Code|RUN|  |  |  |  |  |  |  |  |  |   |WB|  |  |  ]
[    0.000000] efi:   0x00005fecf000-0x00005ffd1fff [ACPI Reclaim|   |  |  |  |  |  |  |  |  |  |   |WB|  |  |  ]
```

출력에서 볼 것: DT 보드에서는 `/memory` 노드가 RAM 범위를 알려 주지만, UEFI 부팅에서는 커널이 **EFI 메모리 맵**을 우선한다(`/chosen/linux,uefi-mmap-*`). `Boot Code`/`Boot Data`/`Loader *`는 `ExitBootServices()` 후 커널이 회수하는 영역, `RUN` 표시가 있는 `Runtime Code`/`Runtime Data`는 Runtime Services를 위해 **절대 건드리면 안 되는** 영역이다. U-Boot는 런타임 코드(예: `ResetSystem`, `GetTime`)를 이 영역에 따로 복사해 둔다. 커널 메모리 레이아웃 문제(예: "DT 보드에서는 되는데 UEFI 부팅에서는 특정 RAM 영역이 안 보인다")를 볼 때 이 로그가 첫 단서다.

### 8.5 EBBR 방식 그대로 — UEFI 변수 + 부트 매니저

`bootefi`에 주소를 손으로 주는 것은 개발용이다. EBBR이 기대하는 "진짜" 방식은 **`Boot####` 변수 + ESP**다. §4.3에서 만든 ESP 디스크를 virtio-blk로 붙이고 U-Boot에서 부트 옵션을 만든다.

```text
=> virtio scan
=> part list virtio 0

Partition Map for virtio device 0  --   Partition Type: EFI

Part	Start LBA	End LBA		Name
	Attributes
	Type GUID
	Partition GUID
  1	0x00000800	0x0003ffde	"ESP"
	attrs:	0x0000000000000000
	type:	c12a7328-f81f-11d2-ba4b-00a0c93ec93b
	guid:	a8a3a96a-410f-4125-acfd-6d4d8beb737a
=> efidebug boot add -b 1 LabLinux virtio 0:1 /EFI/BOOT/BOOTAA64.EFI -i virtio 0:1 /initramfs.cpio.gz -s 'console=ttyAMA0 efi=debug LAB_CMD=cat~/proc/cmdline;cat~/sys/firmware/efi/systab;mount~-t~efivarfs~none~/sys/firmware/efi/efivars;ls~/sys/firmware/efi/efivars'
No EFI variables loaded
Missing TPMv2 device for EFI_TCG_PROTOCOL
Missing RNG device for EFI_RNG_PROTOCOL
=> efidebug boot order 1
=> efidebug boot dump
Boot0000:
attributes: A-- (0x00000001)
  label: virtio 0
  file_path: /VenHw(e61d73b9-a384-4acc-aeab-82e828f3628b,0000000000000000)/VenHw(e61d73b9-a384-4acc-aeab-82e828f3628b,8b0000001f000000)/VenHw(e61d73b9-a384-4acc-aeab-82e828f3628b,17008b0000000000)
  data:
    00000000: 4e ac 08 81 11 9f 59 4d 85 0e e2 1a 52 2c 59 b2  N.....YM....R,Y.
Boot0001:
attributes: A-- (0x00000001)
  label: LabLinux
  file_path: /HD(1,GPT,a8a3a96a-410f-4125-acfd-6d4d8beb737a,0x800,0x3f7df)/\EFI\BOOT\BOOTAA64.EFI
  initrd_path: /HD(1,GPT,a8a3a96a-410f-4125-acfd-6d4d8beb737a,0x800,0x3f7df)/\initramfs.cpio.gz
  data:
    00000000: 63 00 6f 00 6e 00 73 00 6f 00 6c 00 65 00 3d 00  c.o.n.s.o.l.e.=.
    00000010: 74 00 74 00 79 00 41 00 4d 00 41 00 30 00 20 00  t.t.y.A.M.A.0. .
...
=> printenv -e -n BootOrder
BootOrder:
    8be4df61-93ca-11d2-aa0d-00e098032b8c (EFI_GLOBAL_VARIABLE_GUID)
    NV|BS|RT, DataSize = 0x2
=> bootefi bootmgr
Booting: Label: LabLinux Device path: /HD(1,GPT,a8a3a96a-410f-4125-acfd-6d4d8beb737a,0x800,0x3f7df)/\EFI\BOOT\BOOTAA64.EFI
EFI stub: Booting Linux Kernel...
EFI stub: EFI_RNG_PROTOCOL unavailable
EFI stub: Loaded initrd from LINUX_EFI_INITRD_MEDIA_GUID device path
EFI stub: Using DTB from configuration table
EFI stub: Exiting boot services...
lab initramfs: 6.18.55 up 7.74s
console=ttyAMA0 efi=debug LAB_CMD=cat~/proc/cmdline;cat~/sys/firmware/efi/systab;mount~-t~efivarfs~none~/sys/firmware/efi/efivars;ls~/sys/firmware/efi/efivars
SMBIOS3=0x5e48b000
AuditMode-8be4df61-93ca-11d2-aa0d-00e098032b8c
Boot0000-8be4df61-93ca-11d2-aa0d-00e098032b8c
Boot0001-8be4df61-93ca-11d2-aa0d-00e098032b8c
BootCurrent-8be4df61-93ca-11d2-aa0d-00e098032b8c
BootOrder-8be4df61-93ca-11d2-aa0d-00e098032b8c
DeployedMode-8be4df61-93ca-11d2-aa0d-00e098032b8c
OsIndicationsSupported-8be4df61-93ca-11d2-aa0d-00e098032b8c
PlatformLang-8be4df61-93ca-11d2-aa0d-00e098032b8c
PlatformLangCodes-8be4df61-93ca-11d2-aa0d-00e098032b8c
SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c
SetupMode-8be4df61-93ca-11d2-aa0d-00e098032b8c
VendorKeys-8be4df61-93ca-11d2-aa0d-00e098032b8c
```
(`efidebug boot dump`의 옵션 데이터 hex는 앞 2줄만 남겼다.)

출력에서 볼 것:

- 부트 옵션의 cmdline(`-s`)은 **UTF-16**으로 `Boot0001` 변수의 optional data에 저장된다. 이것이 UEFI에서 "커널 cmdline"이 사는 곳이다(EDK2·x86에서도 같다).
- `initrd_path`: U-Boot 확장으로, 부트 옵션에 initrd 디바이스 패스를 같이 넣으면 U-Boot가 LoadFile2 프로토콜로 stub에 건넨다.
- 커널에서 `efivarfs`를 마운트하면 U-Boot가 만든 변수들(`Boot0001`, `SecureBoot`, `SetupMode`…)이 x86 PC처럼 그대로 보인다. U-Boot는 이 변수들을 ESP의 `ubootefi.var` 파일에 저장한다(`CONFIG_EFI_VARIABLE_FILE_STORE`). 실행 후 디스크를 열어 보면:

```sh
mdir -i c8_esp_ub.img@@1M ::/
```

```text
 Volume in drive : is ESP        
 Volume Serial Number is 6137-1E2F
Directory for ::/

EFI          <DIR>     2026-10-10  10:30 
INITRA~1 GZ    1210338 2026-10-10  10:30  initramfs.cpio.gz
virt     dtb   1048576 2026-10-10  10:30 
UBOOTEFI VAR       928 2026-10-10  17:31  ubootefi.var
        4 files           2 259 842 bytes
                         87 116 800 bytes free
```

`-bios`만 쓰는 실험 1(ESP 없음)에서 본 "Cannot persist EFI variables without system partition"이 이 파일이 없어서 나온 경고였다. 실제 제품에서는 eMMC RPMB나 OP-TEE의 보안 저장소에 변수를 두기도 한다(StandAloneMM 등 — 상세는 C5·L 모듈).

### 8.6 `bootflow scan` — 손대지 않아도 EFI 경로를 찾는다

U-Boot 기본 `bootcmd=bootflow scan -lb`(standard boot)는 ESP의 제거식 미디어 경로를 자동으로 찾는다.

```text
=> bootflow scan -l
Scanning for bootflows in all bootdevs
Seq  Method       State   Uclass    Part  Name                      Filename
---  -----------  ------  --------  ----  ------------------------  ----------------
Scanning global bootmeth 'efi_mgr':
No EFI variables loaded
Missing TPMv2 device for EFI_TCG_PROTOCOL
Missing RNG device for EFI_RNG_PROTOCOL
  0  efi_mgr      ready   (none)       0  <NULL>                    
Scanning bootdev 'fw-cfg@9020000.bootdev':
fatal: no kernel available
scanning bus for devices...
Scanning bootdev 'virtio-blk#31.bootdev':
  1  efi          ready   virtio       1  virtio-blk#31.bootdev.par /EFI/BOOT/BOOTAA64.EFI
Scanning bootdev 'virtio-net#32.bootdev':
BOOTP broadcast 1
DHCP client bound to address 10.0.2.15 (4 ms)
*** Warning: no boot file name; using '0A00020F.img'
Using virtio-net#32 device
TFTP from server 10.0.2.2; our IP address is 10.0.2.15
Filename '0A00020F.img'.
Load address: 0x40400000
Loading: *
TFTP error: 'Access violation' (2)
Not retrying...
No more bootdevs
---  -----------  ------  --------  ----  ------------------------  ----------------
(2 bootflows, 2 valid)
```

출력에서 볼 것: `efi_mgr`(UEFI 부트 매니저 = BDS 역할), `efi` bootmeth가 virtio 디스크 1번 파티션에서 `/EFI/BOOT/BOOTAA64.EFI`를 찾았다. 배포판 설치 USB를 꽂으면 U-Boot 보드가 PC처럼 부팅되는 원리가 이것이다. 네트워크 bootdev는 DHCP 후 TFTP를 시도하다 실패했다(TFTP 서버 없음 — 정상).

---

## 9. 커널 안에서는 무슨 일이 일어나나

arm64 커널은 진입 경로가 무엇이든 `primary_entry`(x0 = FDT)에서 만난다. 이후 `setup_arch()`(`arch/arm64/kernel/setup.c`)가 ACPI/DT를 결정한다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="190" y="10" width="300" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="32" text-anchor="middle" font-size="12">primary_entry (x0 = FDT 물리 주소)</text>
  <line x1="340" y1="44" x2="340" y2="58" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="190" y="60" width="300" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="82" text-anchor="middle" font-size="12">setup_machine_fdt() — /chosen, /memory</text>
  <line x1="340" y1="94" x2="340" y2="108" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="150" y="110" width="380" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="132" text-anchor="middle" font-size="12">efi_init() — /chosen의 uefi-system-table, mmap 읽기</text>
  <line x1="340" y1="144" x2="340" y2="158" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="150" y="160" width="380" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="182" text-anchor="middle" font-size="12">acpi_boot_table_init() — ACPI를 쓸지 결정</text>
  <line x1="340" y1="194" x2="340" y2="208" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="60" y="210" width="560" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="229" text-anchor="middle" font-size="12">acpi=off 이거나, DT에 /chosen 말고도 노드가 있고 acpi=on/force가 아니면</text>
  <text x="340" y="246" text-anchor="middle" font-size="12">→ acpi_disabled = 1 (DT 사용)   그 밖에는 ACPI 사용</text>
  <line x1="200" y1="254" x2="160" y2="278" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="480" y1="254" x2="520" y2="278" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="10" y="280" width="320" height="110" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="300" text-anchor="middle" font-size="13" font-weight="bold">DT 경로</text>
  <text x="20" y="322" font-size="12">unflatten_device_tree()</text>
  <text x="20" y="342" font-size="12">psci_dt_init()</text>
  <text x="20" y="362" font-size="12">of_platform_populate() → platform_device</text>
  <text x="20" y="382" font-size="12">매칭: of_match_table (compatible)</text>
  <rect x="350" y="280" width="320" height="110" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="300" text-anchor="middle" font-size="13" font-weight="bold">ACPI 경로</text>
  <text x="360" y="322" font-size="12">acpi_table_init() · SPCR → 콘솔</text>
  <text x="360" y="342" font-size="12">psci_acpi_init() (FADT 플래그)</text>
  <text x="360" y="362" font-size="12">acpi_bus_scan() → AML 실행 → 장치</text>
  <text x="360" y="382" font-size="12">매칭: acpi_match_table (_HID/_CID)</text>
</svg>
```
그림 7 — arm64 `setup_arch()`의 ACPI/DT 갈림길. 판단 근거는 `arch/arm64/kernel/acpi.c`의 `dt_is_stub()`와 `acpi=` 파라미터다. 갈라진 뒤 두 경로 모두 결국 `struct device` + `fwnode`를 만들어 같은 드라이버 모델(G 모듈)로 들어간다.

lab 소스로 확인:

```sh
cd /var/lab/src/linux-6.18.55
grep -n "efi_init\|acpi_boot_table_init\|unflatten_device_tree\|acpi_disabled\|psci_dt_init\|psci_acpi_init" arch/arm64/kernel/setup.c
grep -n "dt_is_stub\|param_acpi_force\|early_param" arch/arm64/kernel/acpi.c | head -5
```

```text
323:	efi_init();
339:	acpi_boot_table_init();
341:	if (acpi_disabled)
342:		unflatten_device_tree();
352:	if (acpi_disabled)
353:		psci_dt_init();
355:		psci_acpi_init();
48:static bool param_acpi_force __initdata;
62:		param_acpi_force = true;
70:early_param("acpi", parse_acpi);
72:static bool __init dt_is_stub(void)
208:	    (!param_acpi_on && !param_acpi_force && !dt_is_stub()))
```

드라이버 작성 때 다시 만나는 지점:

- **`struct fwnode_handle`과 `device_property_*()`**: DT 노드(`of_node`)와 ACPI 장치(`acpi_device`)를 감싸는 공통 핸들. G 모듈(`probe()`가 불리기까지)과 H 모듈(I2C/SPI 장치 열거 — ACPI에서는 `I2cSerialBusV2` 자원으로 표현)에서 다시 나온다.
- **`/sys/firmware/efi/efivars` = efivarfs**: `fs/efivarfs/`. 커널이 `GetVariable`/`SetVariable` Runtime Service를 호출한다. 펌웨어 업데이트(`UpdateCapsule`, `/dev/efi_capsule_loader` — 랩 config의 `CONFIG_EFI_CAPSULE_LOADER=y`)도 같은 Runtime Services를 쓴다 (L 모듈 OTA).
- **`efi_mm`과 Runtime 래퍼**: Runtime Services는 커널 페이지 테이블과 분리된 `efi_mm`에서 실행된다(`drivers/firmware/efi/arm-runtime.c`, `arch/arm64/kernel/efi.c`).
- **`/sys/firmware/acpi/tables`, `/sys/bus/acpi/devices`**: ACPI 디버깅 출발점. `acpidump`/`iasl`(패키지 acpica-tools — 이 VM에는 없다)로 DSDT를 디스어셈블한다.

---

## 10. Arm SystemReady · SBBR · EBBR — "표준을 따르는 arm64"

PC는 "Windows/Linux 설치 USB가 그냥 부팅된다"는 것을 수십 년 동안 당연하게 여겼다. arm64에서 같은 것을 보장하려고 Arm이 만든 규격 묶음이 있다.

| 규격 | 무엇을 규정 | 누구 |
|---|---|---|
| **SBSA** (Server Base System Architecture) | 하드웨어: GIC 버전, Generic Timer, SBSA UART(PL011 부분집합), PCIe ECAM, SMMU 등 | 서버 SoC |
| **BSA** (Base System Architecture) | SBSA의 일반화(클라이언트·임베디드 포함) | 범용 |
| **SBBR** (Server Base Boot Requirements) | 펌웨어: UEFI 필수, **ACPI 필수**(DT 금지), SMBIOS 필수 | 서버 |
| **EBBR** (Embedded Base Boot Requirements) | 펌웨어: UEFI 부분집합 + **DT 허용**, 런타임 서비스 일부 생략 허용 | 임베디드 (U-Boot가 대표 구현) |
| **BBR** (Base Boot Requirements) | SBBR/EBBR 등을 "recipe"로 묶은 상위 문서 | — |
| **SystemReady** | 위 규격 준수를 테스트(ACS: Architecture Compliance Suite)로 인증하는 프로그램 | 보드·서버 벤더 |

SystemReady 인증 밴드(기억 기반 — 2024년 무렵 개편되었다):

- 예전 이름: **SR**(Server Ready: SBSA + SBBR, UEFI + ACPI), **ES**(Embedded Server: BSA + SBBR — 예: ACPI로 부팅하는 Raspberry Pi 4 UEFI 펌웨어가 ES 인증을 받았던 것으로 기억), **IR**(IoT Ready: BSA + EBBR, UEFI + DT — U-Boot 보드들), **LS**(LinuxBoot).
- 개편 후: SR/ES가 **"SystemReady band"**(ACPI)로 합쳐지고, IR은 **"SystemReady Devicetree band"**가 되었다고 알고 있다. 면접에서는 "서버는 SBBR(ACPI), 임베디드는 EBBR(DT 허용), 둘 다 UEFI 인터페이스"만 정확히 말하면 충분하다.

Embedded Linux 엔지니어에게 의미: 고객이 "SystemReady IR/Devicetree 보드로 Fedora·Debian 공식 이미지를 그대로 부팅하고 싶다"고 하면, BSP 일은 **U-Boot의 `EFI_LOADER`·`bootefi`·ESP 레이아웃·변수 저장소·capsule 업데이트**를 맞추는 일이 된다. 이 노트의 §8 실험이 정확히 그 일의 축소판이다.

---

## 11. 세계별 부팅 흐름 비교표

"(기억)"은 이 VM·랩에서 확인하지 못하고 기억에 의존한 사실이다. 정확한 세부(버전·이름)는 해당 벤더 문서로 다시 확인할 것.

| 세계 | 1단 펌웨어 | OS 로더 | 커널 입구 | HW 설명 | 표준 | 이 노트에서 확인? |
|---|---|---|---|---|---|---|
| x86 PC (레거시) | BIOS(POST) | MBR → GRUB core.img | bzImage real-mode 진입 | ACPI | — | 아니오 (기억) |
| x86 PC (UEFI) | UEFI(EDK2 기반 상용 펌웨어) | shim → GRUB 또는 systemd-boot | bzImage의 EFI stub (x86 커널도 PE) | ACPI | UEFI + ACPI | 아니오 (기억) |
| arm64 서버 (Grace·Graviton·Ampere) | TF-A → UEFI(EDK2 계열) | shim → GRUB | `Image` EFI stub | **ACPI** (SBBR) | SystemReady SR/band | 같은 구조를 VM(Apple Vz EDK2)과 QEMU+AAVMF로 확인. 벤더별 세부는 (기억) |
| 임베디드 arm64 (i.MX8, AM62 …) | BootROM → SPL → TF-A | U-Boot `booti` | `Image` 오프셋 0 | **DT** | 없음(벤더 BSP) | 예 (랩 `booti`) |
| U-Boot + UEFI (EBBR) | BootROM → SPL → TF-A | U-Boot `bootefi`/`bootmgr` (+ 선택적으로 GRUB) | `Image` EFI stub | **DT** (config table) | EBBR, SystemReady IR/Devicetree band | 예 (랩 `bootefi`, `bootmgr`) |
| NVIDIA Jetson Orin (JetPack 5 이후) | BootROM → MB1 → MB2 → UEFI(NVIDIA edk2 기반) | L4TLauncher(EFI 앱) + `extlinux.conf` | `Image` EFI stub | **DT** | UEFI + DT | 아니오 (기억). UEFI + DT 조합 자체는 §7.4 `acpi=off`로 재현 |
| Jetson Xavier·TX2·Nano (JetPack 4) | BootROM → MB1 → MB2(Xavier) → CBoot | CBoot 또는 U-Boot | `Image` | DT | — | 아니오 (기억) |
| Raspberry Pi 4/5 | 부트 EEPROM + VideoCore 펌웨어(`config.txt`) | (기본) 펌웨어가 커널·DTB·overlay를 직접 로드, 선택적으로 U-Boot | `Image` (`kernel8.img` 등) | **DT** (펌웨어가 수정해 전달) | — / 커뮤니티 UEFI 펌웨어로 ACPI·SystemReady 가능 | 아니오 (기억) |
| 이 Lima VM | Apple Virtualization UEFI(EDK2) | shim → GRUB | Ubuntu UKI형 stub → 커널 | **ACPI** | UEFI + ACPI | 예 (§3) |

정리하면 **"UEFI냐 아니냐"와 "ACPI냐 DT냐"는 독립된 두 축**이다. UEFI + ACPI(서버), UEFI + DT(Jetson Orin, EBBR 보드), 비UEFI + DT(전통 임베디드)가 모두 실제로 쓰인다. 비UEFI + ACPI는 arm64에서는 사실상 없다(ACPI 테이블 전달 수단이 UEFI config table이기 때문).

---

## 12. Embedded 실무에서 다시 보기

### 12.1 "Jetson에서 `/proc/device-tree`가 왜 있죠? UEFI라면서요?"

센서 키트를 Jetson Orin으로 옮겼더니 부팅 로그에 "EFI stub"이 보이고 `/sys/firmware/efi`도 있다. 그런데 드라이버는 여전히 DT overlay로 카메라를 붙인다. 이것은 모순이 아니다 — §7.4 `acpi=off` 실험과 같다: UEFI 펌웨어가 DT를 configuration table로 넘기고, 커널은 DT 모드로 돈다. 카메라·IMU를 붙이는 일은 DT(D 모듈) 일이고, 부트 순서·cmdline·커널 선택은 UEFI 변수와 `extlinux.conf` 일이다(기억 — 세부는 NVIDIA L4T 문서 확인). 디버깅 첫 명령:

```sh
ls /sys/firmware; ls /proc/device-tree >/dev/null 2>&1 && echo DT || echo no-DT
dmesg | grep -E "EFI stub|efi: EFI v|Machine model|psci: probing"
```

### 12.2 arm64 서버용 드라이버를 DT 보드 코드에서 가져왔더니 probe가 안 된다

ACPI 서버에는 `compatible`이 없다. 해결 순서: (1) 펌웨어 팀이 DSDT/SSDT에 장치와 `_HID`를 넣었는지 `/sys/bus/acpi/devices/*/hid`로 확인 (2) 드라이버에 `.acpi_match_table` 추가 (3) `of_*` API를 `device_property_*`/`fwnode_*`로 교체 (4) 임시로는 `_HID "PRP0001"` + `_DSD` compatible로 DT 매칭 재사용. 커널에 SSDT를 추가해 테스트하는 방법(`CONFIG_EFI_CUSTOM_SSDT_OVERLAYS`, `CONFIG_ACPI_TABLE_UPGRADE` — 둘 다 랩 config에서 `=y`)도 있다.

### 12.3 "공식 Debian arm64 설치 이미지로 우리 보드를 부팅하고 싶다"

U-Boot에 `CONFIG_EFI_LOADER`, `CMD_BOOTEFI_BOOTMGR`, `BOOTSTD`가 켜져 있으면, 설치 USB의 `\EFI\BOOT\BOOTAA64.EFI`(GRUB)를 `bootflow scan`이 찾는다(§8.6). 남은 일은 (a) U-Boot가 넘기는 DT가 **메인라인 커널과 호환**되는지(벤더 커널 전용 바인딩이면 배포판 커널이 장치를 못 찾는다) (b) UEFI 변수 저장소(파일·SPI·RPMB) (c) `ResetSystem`/`GetTime` 같은 Runtime Services가 동작하는지다.

### 12.4 Secure Boot가 켜진 x86/arm64 서버에서 직접 빌드한 커널 모듈이 안 올라간다

lockdown 때문에 서명 없는 모듈이 거부된다(`Key was rejected by service`). 방법: 모듈 서명 키를 만들어 `mokutil --import`로 MOK에 등록 → 재부팅 시 MokManager에서 승인 → `scripts/sign-file`로 모듈 서명. VM에서는 Secure Boot가 꺼져 있어(§3.3) 이 문제가 안 생긴다 — 개발 환경과 배포 환경의 차이를 기억해 둘 것.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `qfw load`/`tftp` 뒤 `bootefi <addr>`만 씀 | `No UEFI binary known at 0x40400000` | bootefi가 마지막으로 `load`한 EFI 이미지만 기억 | `bootefi <addr>:<size> ...` 또는 `load` 명령으로 읽기 |
| U-Boot `setenv bootargs`에 `;`나 파이프 문자를 따옴표 없이 | `syntax error` | hush 셸이 `;`를 명령 구분자로 해석 | 값 전체를 큰따옴표로 감싼다 |
| `EFI stub:` 메시지를 기다림 | 아무 줄도 안 나옴 | stub 기본 로그 레벨이 NOTICE | cmdline에 `efi=debug` |
| DT 보드용 드라이버를 ACPI 서버에 | probe 안 됨, 장치는 `/sys/bus/acpi/devices`에만 | `of_match_table`만 있음 | `.acpi_match_table` + `device_property_*` |
| 서명 없는 커널·로더를 Secure Boot 기계에 | `Access Denied -- rejected probably by Secure Boot` | db에 서명 키 없음 | shim + MOK 또는 자체 PK/KEK/db 등록 |
| ESP를 ext4로 만들거나 경로 오타 | `No bootable option or device was found` | 펌웨어는 FAT만 읽고, 제거식 경로는 `\EFI\BOOT\BOOTAA64.EFI`로 고정 | ESP는 FAT, 파티션 타입 `ef00` |
| GRUB `linux` 줄에 `;`나 파이프 문자를 그대로 | `error: syntax error.` / `you need to load the kernel first.` | GRUB 스크립트도 셸 문법 | 해당 인자를 따옴표로 |
| `grub-mkstandalone` 이미지에서 `search`가 디스크를 못 찾음 | `error: no such device: /Image.` | `part_gpt`·`fat` 모듈이 로드 안 됨 | `insmod part_gpt; insmod fat` |
| `/sys/firmware/efi/efivars`의 파일을 `rm -rf` | 일부 노트북이 부팅 불가가 되었던 사건(2016년경, 기억) | 펌웨어가 필수 변수 삭제를 견디지 못함 | 커널이 이후 비표준 변수를 immutable로 만든다. 변수 삭제는 `efibootmgr`로만 |
| ACPI 머신에 `dtb=`나 DT를 같이 넘김 | 커널이 DT 모드로 부팅, ACPI 장치가 안 보임 | arm64는 DT에 노드가 있으면 DT를 우선 | ACPI 시스템에는 DT를 주지 않는다(U-Boot 문서: "When using ACPI no device-tree shall be specified") |

---

## 14. 면접에서 이렇게 말한다

**Q.** Walk me through how a UEFI arm64 server boots Linux, and contrast it with a typical embedded ARM board.

**A.** 서버는 TF-A 뒤에 UEFI 펌웨어(EDK2 계열)가 DRAM·PCIe를 초기화하고, BDS가 NVRAM의 `BootOrder`/`Boot####`를 따라 ESP(FAT)의 shim → GRUB을 실행한다. GRUB은 커널을 EFI 앱으로 실행하고, 커널의 EFI stub이 initrd·cmdline을 받아 `ExitBootServices()` 후 커널로 들어간다. 하드웨어 설명은 펌웨어가 들고 있는 ACPI 테이블이다. 임베디드 보드는 BootROM → SPL → TF-A → U-Boot가 `booti`로 `Image`와 보드 전용 DTB를 메모리에 놓고 바로 점프한다. 차이는 "표준 펌웨어 인터페이스 + 펌웨어 소유 HW 설명" 대 "보드별 부트로더 + 커널과 같이 빌드하는 DT"다.

> A server goes TF-A, then UEFI firmware that brings up DRAM and PCIe; the boot manager reads BootOrder and Boot#### variables and launches shim and GRUB from the FAT ESP. GRUB starts the kernel as an EFI application — the kernel's EFI stub collects the command line and initrd, calls ExitBootServices and jumps into the kernel, and hardware is described by ACPI tables owned by the firmware. An embedded board goes BootROM, SPL, TF-A, U-Boot, and U-Boot's booti places the Image and a board-specific DTB in memory and jumps directly. So it's a standardized firmware interface with firmware-owned hardware description, versus a board-specific bootloader with a device tree built alongside the kernel.

**꼬리 질문**: "What happens at ExitBootServices?" → Boot Services 메모리를 OS가 회수하고, 이후 펌웨어는 Runtime Services(변수·시간·리셋·capsule)만 남는다. 최신 메모리 맵 키가 필요해 재시도 루프가 있다.

**Q.** ACPI versus Device Tree — what's the difference, and when is each used?

**A.** 둘 다 열거할 수 없는 장치를 OS에 알려 주는 수단이다. DT는 정적 자료 구조라 `compatible`, `reg`, `interrupts`만 있고, 클럭·전원·핀 제어는 커널 드라이버가 직접 한다. ACPI는 테이블 + AML 바이트코드라서 장치를 `_HID`로 식별하고 `_CRS`로 자원을, `_PS0`/`_PS3` 같은 메서드로 전원 처리를 펌웨어에 숨긴다. 그래서 OS 하나로 많은 서버를 지원해야 하는 x86·arm64 서버(SBBR)는 ACPI, 커널이 SoC를 세밀하게 제어해야 하는 임베디드·Jetson은 DT를 쓴다. 같은 arm64 커널이 둘 다 지원하고, arm64에서는 DT에 실제 노드가 있으면 DT를, `/chosen`만 있는 stub DT면 ACPI를 고른다.

> Both describe non-discoverable hardware. Device tree is a static data structure — compatible, reg, interrupts — and the kernel drivers themselves control clocks, regulators and pins. ACPI is tables plus AML bytecode: devices are identified by _HID, resources come from _CRS, and power sequencing is hidden behind firmware methods like _PS0 and _PS3. That's why servers, which must run one OS image across many platforms, use ACPI under SBBR, while embedded SoCs and Jetson use device tree for fine-grained control. The same arm64 kernel supports both; it picks DT if the tree has real nodes and ACPI if it only got a stub tree with /chosen.

**꼬리 질문**: "How do you make one driver work on both?" → `of_match_table`과 `acpi_match_table`을 둘 다 두고, 속성은 `device_property_read_*()`로 읽는다. ACPI에서 `PRP0001` + `_DSD` compatible로 DT 매칭을 재사용할 수도 있다.

**Q.** What is the EFI stub? How can the same arm64 Image be booted by both U-Boot's booti and a UEFI firmware?

**A.** arm64 `Image`의 첫 64바이트는 두 헤더를 겹쳐 놓았다. 첫 바이트가 `MZ`인데 그 4바이트를 명령으로 실행하면 무해한 `ccmp`이고, 다음 명령이 `primary_entry`로 분기한다 — `booti`는 오프셋 0으로 점프해서 이 길로 간다. 한편 0x3c의 PE 오프셋이 PE/COFF 헤더를 가리키고 Subsystem이 EFI_APPLICATION, 진입점이 `efi_pe_entry`라서 UEFI 펌웨어는 이것을 일반 EFI 앱으로 실행한다. stub은 LoadOptions에서 cmdline, LoadFile2에서 initrd, config table에서 DTB를 얻거나 빈 DTB를 만들고, `/chosen`에 EFI 시스템 테이블·메모리 맵 위치를 적은 뒤 `ExitBootServices()`를 하고 같은 `primary_entry`로 들어간다.

> The first 64 bytes of the arm64 Image overlay two headers. It starts with "MZ", but those four bytes decode to a harmless ccmp instruction, followed by a branch to primary_entry — that's the path booti takes by jumping to offset zero. Offset 0x3c points to a PE/COFF header whose subsystem is EFI application and whose entry point is efi_pe_entry, so UEFI firmware runs it as a normal EFI app. The stub gets the command line from LoadOptions, the initrd through LoadFile2, the DTB from the configuration table or generates an empty one, records the EFI system table and memory map in /chosen, calls ExitBootServices and enters the same primary_entry.

**Q.** How does UEFI Secure Boot work for Linux?

**A.** 펌웨어 변수에 키 계층이 있다: OEM의 PK가 KEK 변경을 서명하고, KEK가 허용 목록 db와 폐기 목록 dbx를 서명한다. 펌웨어는 `.efi`를 로드하기 전에 Authenticode 서명을 db/dbx로 검증한다. PC의 db에는 보통 Microsoft 키만 있어서 배포판은 Microsoft가 서명한 shim을 쓰고, shim은 내장된 배포판 인증서와 MOK로 GRUB·커널을 검증한다. 커널은 모듈 서명과 lockdown으로 이어 간다. 폐기는 dbx와 SBAT 세대 번호로 한다. 임베디드 secure boot와 생각은 같고 키가 퓨즈 대신 인증된 변수에 있다는 점이 다르다.

> The firmware holds a key hierarchy in authenticated variables: the OEM's PK signs KEK updates, and KEK signs the allow list db and the deny list dbx. Before loading any EFI binary the firmware checks its Authenticode signature against db and dbx. PCs usually only trust Microsoft keys, so distributions ship a Microsoft-signed shim, which embeds the distro certificate and Machine Owner Keys and verifies GRUB and the kernel; the kernel continues with module signing and lockdown. Revocation is via dbx and SBAT generation numbers. It's the same chain-of-trust idea as embedded secure boot, but the root keys live in authenticated variables rather than fuses.

**Q.** What are UEFI runtime services and why would an embedded Linux engineer care?

**A.** `ExitBootServices()` 뒤에도 남는 펌웨어 함수들이다: `GetVariable`/`SetVariable`, `GetTime`/`SetTime`, `ResetSystem`, `UpdateCapsule`, `SetVirtualAddressMap`. 리눅스는 efivarfs, `rtc-efi`, 재부팅, capsule 기반 펌웨어 업데이트(fwupd)에 쓴다. 펌웨어 코드가 커널 권한으로 도는 것이라 버그가 커널을 죽일 수 있어 커널은 별도 `efi_mm`에서 래퍼로 호출한다. 임베디드에서 U-Boot의 EFI 구현을 쓸 때 변수 저장소(파일·SPI·RPMB/OP-TEE)와 capsule 업데이트가 OTA 설계에 직접 걸린다.

> They're the firmware functions that survive ExitBootServices: Get/SetVariable, Get/SetTime, ResetSystem, UpdateCapsule and SetVirtualAddressMap. Linux uses them for efivarfs, the rtc-efi driver, reboot, and capsule-based firmware updates through fwupd. Since it's firmware code running at kernel privilege, Linux calls it through wrappers in a separate efi_mm. On embedded boards using U-Boot's EFI implementation, the variable store — a file, SPI flash, or RPMB through OP-TEE — and capsule updates feed directly into the OTA design.

**Q.** Why did U-Boot implement the UEFI API, and what are EBBR and SystemReady?

**A.** 배포판이 arm64 설치 이미지 하나로 서버와 보드를 모두 부팅하게 하려고다. U-Boot는 PI 단계 없이 UEFI의 바깥 인터페이스(Boot/Runtime Services 일부, 블록·파일 프로토콜, 변수, 부트 매니저)를 구현하고 `bootefi`로 EFI 앱을 실행한다. EBBR은 임베디드 보드가 제공해야 할 UEFI 부분집합을 정하고 DT를 허용한다. 서버용은 SBBR로 ACPI를 요구한다. SystemReady는 Arm의 인증 프로그램으로, ACS 테스트로 SBSA/BSA 하드웨어와 SBBR/EBBR 펌웨어 준수를 확인한다. 나는 QEMU에서 U-Boot `efidebug boot add`로 `Boot0001`을 만들고 `bootefi bootmgr`로 커널 EFI stub을 띄워 initrd가 LoadFile2로 넘어가는 것까지 확인했다.

> So distributions can boot one arm64 install image on both servers and boards. U-Boot implements the outward-facing UEFI interface — a subset of boot and runtime services, block and file protocols, variables and a boot manager — without the PI phases, and bootefi runs EFI apps. EBBR defines the UEFI subset an embedded board must provide and allows device tree; SBBR is the server counterpart and requires ACPI. SystemReady is Arm's certification program that checks hardware and firmware compliance with the ACS test suites. I've exercised this in QEMU: created a Boot0001 option with efidebug, ran bootefi bootmgr, and watched the kernel's EFI stub receive the initrd through LoadFile2.

**Q.** Jetson Orin boots with UEFI but uses a device tree. How does that work?

**A.** UEFI와 ACPI는 독립된 축이다. 펌웨어가 ACPI 대신 DTB를 UEFI configuration table로 설치하면 커널 stub이 "Using DTB from configuration table"로 그것을 받아 DT 모드로 부팅한다. 나는 QEMU에서 같은 EDK2와 같은 커널로 `acpi=on`이면 "Generating empty DTB" + ACPI, `acpi=off`면 config table의 DTB + `Machine model` 줄이 나오는 것을 확인했다. Orin은 그 DT 쪽 조합이고, 그래서 overlay 같은 DT 작업은 그대로 하면서 부트 관리는 UEFI 방식(L4TLauncher, 변수, capsule)으로 한다.

> UEFI and ACPI are independent axes. If the firmware installs a DTB as a UEFI configuration table instead of ACPI tables, the kernel's stub reports "Using DTB from configuration table" and the kernel boots in DT mode. I reproduced this in QEMU with the same EDK2 and the same kernel: with ACPI on, the stub generates an empty DTB and the kernel uses ACPI; with ACPI off, it picks up the DTB from the config table and prints the machine model. Orin is the DT flavor of that, so device-tree work like overlays stays the same while boot management follows UEFI conventions.

**꼬리 질문**: "How would you check which mode a running system is in?" → `/sys/firmware/efi` 존재 여부(UEFI), `/proc/device-tree` 또는 `/sys/firmware/acpi/tables`(HW 설명), `dmesg`의 `psci: probing for conduit method from DT/ACPI`.

---

## 15. 직접 해보기

1. **개념**: PI의 SEC/PEI/DXE/BDS/TSL/RT 각각에 대해 U-Boot·TF-A의 어떤 단계가 대응하는지 표로 다시 써 보라. RT에 대응하는 임베디드 컴포넌트는 무엇이고, 왜 그것이 "부팅 후에도 남는 펌웨어"인가?
정답/힌트: 그림 2. RT ↔ TF-A BL31(PSCI, SMC 서비스). 둘 다 OS가 실행 중에 특권 호출로 부르는 상주 코드다.

2. **코드 과제**: `c8_efivar.c`의 `devpath()`에 ACPI 노드(type 2/1: HID u32 + UID u32)와 PCI 노드(type 1/1: function u8, device u8)를 추가해 `Boot0000`이 `PciRoot(0x0)/Pci(0x6,0x0)`으로 출력되게 하라. `efibootmgr -v` 출력과 비교한다.
정답/힌트: `dp: 02 01 0c 00 d0 41 03 0a 00 00 00 00` — HID `0x0a0341d0`은 EISA ID `PNP0A03`(PCI 루트)이다. `c8_acpi.c`의 `eisaid()`를 재사용(바이트 순서 주의).

3. **QEMU 과제**: §8.5의 U-Boot `efidebug boot add`에 `-d virtio 0:1 /virt.dtb`를 추가해 DTB를 디스크에서 읽게 하고, stub 메시지가 무엇으로 바뀌는지 확인하라. 이어서 `bootefi hello`와 `env set efi_selftest list; bootefi selftest`를 돌려 U-Boot UEFI 구현을 시험해 보라.
정답/힌트: U-Boot 문서 `doc/usage/cmd/efidebug.rst`와 `bootefi.rst`. DTB는 여전히 configuration table로 전달되므로 stub 메시지는 같을 가능성이 높다 — 직접 확인하고 `fdt` 내용으로 차이를 본다.

4. **관찰 과제**: EDK2 ACPI 게스트(§7.4)에서 `ls /sys/bus/acpi/devices`와 각 장치의 `hid`, `path`를 출력하는 셸 스크립트를 initramfs에 넣어 돌리고, `c8_acpi`가 찾은 `_HID` 목록과 대응시켜라.
정답/힌트: `for d in /sys/bus/acpi/devices/*; do echo $d $(cat $d/hid 2>/dev/null) $(cat $d/path); done`. `\_SB_.COM0` ↔ `ARMH0011`.

5. **관찰 과제**: VM 호스트에서 `sudo cat /sys/firmware/acpi/tables/DSDT > dsdt.aml`로 뽑고, `c8_acpi`의 `_HID` 스캔을 확장해 `_UID`와 `_CRS`의 Memory32Fixed(태그 0x86) 주소도 출력하라. (`iasl`은 설치되어 있지 않으니 설치하지 말고 바이트로 풀어 본다.)
정답/힌트: ACPI 스펙의 "Large Resource Data Type" — Memory32Fixed는 `86 09 00 <rw> <base u32> <len u32>`.

6. **설계 문제**: 센서 키트 보드를 SystemReady Devicetree band(EBBR)로 만들어 Fedora 공식 이미지를 부팅하려면 U-Boot에서 무엇을 켜고 무엇을 확인해야 하는가? 5개 이상 적어라.
정답/힌트: `EFI_LOADER`, `BOOTSTD`/`bootflow`, 변수 저장소 위치, `EFI_LOAD_FILE2_INITRD`, Runtime `ResetSystem`·`GetTime`, capsule 업데이트, 메인라인 호환 DT, 콘솔 `stdout-path`.

7. **도전 과제**: EDK2 Secure Boot 펌웨어(§4.3)에서 거부된 랩 `Image`를 통과시키려면 무엇이 필요한가? (VM에 `sbsigntool`이 없다면 설치하지 말고 절차만 설계하라.)
정답/힌트: 자체 PK/KEK/db 키 생성 → 빈 VARS(`AAVMF_VARS.fd`)에 등록(Setup Mode) → `sbsign`으로 `Image` 서명 → 재부팅. 또는 Microsoft 서명 shim + MOK 경로.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| UEFI | Unified Extensible Firmware Interface | 펌웨어가 OS 로더에게 제공하는 표준 C 인터페이스 |
| PI | Platform Initialization spec | 펌웨어 내부 단계(SEC/PEI/DXE…) 규격. EDK2가 구현 |
| EDK2 / TianoCore | EFI Development Kit II | UEFI 레퍼런스 구현. AAVMF/OVMF는 QEMU용 빌드 |
| Boot Services | — | `ExitBootServices()` 전까지만 쓰는 펌웨어 함수(메모리·프로토콜·이미지 로드) |
| Runtime Services | — | OS 실행 중에도 남는 함수(변수·시간·리셋·capsule) |
| Protocol | — | GUID로 찾는 함수 포인터 구조체(UEFI의 vtable) |
| ESP | EFI System Partition | GPT 타입 `ef00`의 FAT 파티션. `.efi` 파일이 사는 곳 |
| Device Path | — | 장치·파일 위치를 노드 연쇄로 표현(`HD(...)/\EFI\...`) |
| `Boot####` / `BootOrder` | UEFI 전역 변수 | 부트 옵션과 순서. 옵션 데이터에 커널 cmdline(UTF-16) |
| PE/COFF | Portable Executable | Windows 실행 파일 형식. 모든 `.efi`의 형식 |
| EFI stub | `CONFIG_EFI_STUB` | 커널 안의 작은 EFI 앱. 펌웨어에서 커널로 넘어가는 다리 |
| LoadFile2 initrd | `LINUX_EFI_INITRD_MEDIA_GUID` | 로더가 initrd를 stub에 건네는 표준 방법 |
| shim | — | Microsoft 서명 1단계 로더. 배포판 키·MOK로 다음 단계 검증 |
| MOK | Machine Owner Key | 사용자가 등록하는 추가 신뢰 키(`mokutil`) |
| PK / KEK / db / dbx | Secure Boot 키 | 최상위 / 키 교환 / 허용 / 폐기 |
| SBAT | Secure Boot Advanced Targeting | 세대 번호 기반 부트로더 폐기 |
| UKI | Unified Kernel Image | stub + 커널 + initrd + cmdline을 묶은 PE 하나 |
| ACPI | Advanced Configuration and Power Interface | 펌웨어가 제공하는 HW 설명 테이블 + AML |
| AML / ASL | ACPI Machine/Source Language | ACPI 바이트코드 / 그 소스 언어(`iasl`로 컴파일) |
| RSDP / XSDT | — | ACPI 루트 포인터 / 테이블 목록 |
| DSDT / SSDT | — | 장치 정의 AML 테이블(주 / 보조) |
| MADT / GTDT / SPCR | — | GIC 구성 / 타이머 / 콘솔 UART 테이블 |
| `_HID` / `_CID` / `_CRS` / `_DSD` | ACPI 객체 | 하드웨어 ID / 호환 ID / 자원 / 장치별 속성 |
| HW-reduced ACPI | — | 고정 하드웨어 없이 일반 장치로만 표현하는 ACPI(arm64 필수) |
| SBSA / BSA | (Server) Base System Architecture | arm64 시스템 하드웨어 최소 요구 |
| SBBR / EBBR | Server / Embedded Base Boot Requirements | 펌웨어 요구: UEFI+ACPI / UEFI 부분집합+DT 허용 |
| SystemReady | — | Arm의 BSA·BBR 준수 인증 프로그램 |
| `bootefi` | U-Boot 명령 | U-Boot의 UEFI 구현 위에서 EFI 앱(커널 stub, GRUB) 실행 |

---

## 17. 요약 & 체크리스트

UEFI는 "펌웨어가 OS 로더에게 주는 표준 C API"이고, 내부적으로는 PI 단계(SEC→PEI→DXE→BDS→TSL→RT)로 동작한다. 부트 대상은 NVRAM의 `Boot####` 변수가 가리키는 ESP(FAT)의 PE/COFF 파일이며, Secure Boot는 PK/KEK/db/dbx 키 계층으로 그 파일들의 서명을 검증한다(리눅스는 shim + MOK). arm64 `Image`는 `MZ`로 시작하는 PE 헤더와 arm64 부트 헤더를 겹쳐 놓아 `booti`(오프셋 0)와 UEFI(`efi_pe_entry`)의 두 입구를 가지며, EFI stub은 cmdline·initrd(LoadFile2)·DTB(config table 또는 빈 DTB)를 모아 `ExitBootServices()` 후 같은 `primary_entry`로 들어간다. 하드웨어 설명은 UEFI와 별개의 축으로, 서버는 ACPI(`_HID`, AML), 임베디드·Jetson은 DT(`compatible`)를 쓰고, 같은 커널이 `dt_is_stub()` 판단으로 둘 중 하나를 고르며 드라이버는 `of_match_table`/`acpi_match_table`로 각각 매칭된다. U-Boot는 EBBR을 위해 UEFI 바깥 인터페이스를 구현해 `bootefi`·`bootmgr`로 배포판 이미지를 부팅한다.

- [ ] 레거시 BIOS와 UEFI의 차이(부트 대상, CPU 모드, 파티션, 서명)를 표로 설명할 수 있다
- [ ] SEC/PEI/DXE/BDS/TSL/RT를 U-Boot·TF-A 단계와 대응시켜 말할 수 있다
- [ ] Boot Services와 Runtime Services, `ExitBootServices()`의 의미를 설명할 수 있다
- [ ] `efibootmgr -v`의 device path를 읽고 efivarfs 파일을 C로 디코드할 수 있다
- [ ] Secure Boot 키 계층과 shim·MOK·SBAT의 역할을 설명할 수 있다
- [ ] arm64 `Image`의 `MZ`/`ARM\x64`/PE 헤더를 hexdump로 보여 주며 두 입구를 설명할 수 있다
- [ ] EFI stub이 하는 일 7단계와 `efi=debug`가 필요한 이유를 말할 수 있다
- [ ] ACPI 주요 테이블(RSDP·XSDT·FADT·MADT·GTDT·SPCR·DSDT)과 DT 대응을 말할 수 있다
- [ ] `_HID`와 `compatible`이 드라이버 매칭에서 어떻게 쓰이는지, 두 모드를 모두 지원하는 드라이버를 어떻게 쓰는지 설명할 수 있다
- [ ] U-Boot에서 `bootefi`(주소+크기+initrd)와 `efidebug boot add` + `bootefi bootmgr`로 커널을 부팅할 수 있다
- [ ] 실행 중인 시스템이 UEFI인지, ACPI/DT 중 무엇을 쓰는지 30초 안에 판별할 수 있다
- [ ] SBBR/EBBR/SystemReady의 차이와 Jetson Orin(UEFI + DT)의 위치를 설명할 수 있다

## 참고 자료

- UEFI Forum, UEFI Specification · ACPI Specification · PI Specification — https://uefi.org/specifications
- Linux kernel docs, "Booting AArch64 Linux" (Image 헤더, code0/code1) — https://docs.kernel.org/arch/arm64/booting.html
- Linux kernel docs, "The EFI Boot Stub" — https://docs.kernel.org/admin-guide/efi-stub.html
- Linux kernel docs, "ACPI on Arm systems" — https://docs.kernel.org/arch/arm64/arm-acpi.html
- Linux kernel docs, "ACPI Based Device Enumeration" (`PRP0001`, `_DSD`) — https://docs.kernel.org/firmware-guide/acpi/enumeration.html
- 커널 소스(랩 6.18.55): `drivers/firmware/efi/libstub/`(`efi-stub-entry.c`, `efi-stub.c`, `fdt.c`, `arm64.c`), `arch/arm64/kernel/setup.c`, `arch/arm64/kernel/acpi.c`, `drivers/tty/serial/amba-pl011.c` — https://elixir.bootlin.com/linux/v6.18/source
- U-Boot docs, "UEFI on U-Boot" — https://docs.u-boot.org/en/latest/develop/uefi/uefi.html, 그리고 랩 소스의 `doc/usage/cmd/bootefi.rst`
- Arm, EBBR (Embedded Base Boot Requirements) — https://github.com/ARM-software/ebbr
- TianoCore EDK2 — https://github.com/tianocore/edk2
- shim — https://github.com/rhboot/shim
- 관련 노트: C1(부트 지도), C3(U-Boot 사용·FIT), C5(TF-A·PSCI·신뢰 부팅), C6(커널 시작), D 모듈(Device Tree), G 모듈(디바이스 모델·매칭), L 모듈(Secure Boot·OTA)
