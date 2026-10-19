# J1. 임베디드 저장장치 — NOR · raw NAND · eMMC · SD · UFS · NVMe, 누가 FTL을 갖나

> **이 노트를 다 읽으면**: 여섯 가지 저장 매체를 "FTL이 어디 사는가"로 분류하고 각각이 Linux에서 어떤 `/dev` 노드·드라이버 스택(`/dev/mtdN`, `/dev/mmcblkN{,boot0,rpmb}`, `/dev/sdX`, `/dev/nvmeXnY`)으로 보이는지 말할 수 있다 · eMMC의 EXT_CSD(수명·캐시·BKOPS·enhanced 영역·erase 종류)와 NVMe SMART, UFS health descriptor를 직접 ioctl/sysfs로 읽을 수 있다 · TBW = 용량 x P/E ÷ WAF 계산으로 로봇의 저장장치 수명을 산정하고 필드 건강 모니터링을 설계할 수 있다 · "로봇에 eMMC냐 raw NAND냐"를 비용·내구성·전원 차단·부팅·드라이버 관점에서 30초 안에 답할 수 있다
> **왜 필요한가**: BSP·플랫폼 면접의 단골("eMMC vs raw NAND?", "필드에서 flash 마모를 어떻게 감시하나?")이고, 실무에서는 부품 선정 회의, 로그 저장 설계, 필드 장비가 2년 만에 읽기 전용으로 바뀌는 사고 분석으로 다시 나온다
> **Don 기준 난이도**: NAND 물리·FTL·ECC·WAF·PLP·NVMe SMART는 Don이 SSD 펌웨어 쪽에서 직접 만든 것이다 / 새로 배울 부분은 **작은 managed flash(eMMC·SD·UFS)의 표준 레지스터와 Linux가 각 매체를 어떤 계층으로 보여 주는지**, 그리고 "호스트 쪽 엔지니어"로서 고르는 기준
> **선행 노트**: C2(BootROM과 부트 미디어 — eMMC boot 파티션·EXT_CSD[179]·nandsim), I11(MTD · MMC · block — MTD/UBI 내부, MMC 카드 열거, blk-mq). 이 노트는 그 둘을 반복하지 않고 **매체 선택과 건강 관리**로 간다. J2(블록 FS)·J4(Flash FS)·J5(전원 차단 안전성)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 Solidigm에서 만든 SSD 안에는 Cortex-R 몇 개, DRAM, 수십 개의 NAND die, 그리고 PLP 커패시터가 있었다. FTL·ECC·배드 블록·wear leveling·GC·전원 차단 복구가 전부 그 안에서 끝났고, 호스트는 LBA만 보았다.

임베디드 보드에서는 같은 NAND가 **세 가지 방식**으로 붙는다. 어떤 방식이냐에 따라 "FTL을 누가 만들었고, 누가 책임지고, Linux는 무엇을 볼 수 있는가"가 달라진다. 이것이 저장장치 선택의 핵심 질문이다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">누가 FTL을 갖나 — 같은 NAND, 세 가지 소유 방식</text>
  <rect x="10" y="34" width="660" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="54" text-anchor="middle" font-size="13">Linux 호스트(SoC): VFS → FS → UBI 또는 block 계층 → 드라이버</text>
  <line x1="115" y1="64" x2="115" y2="82" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="64" x2="340" y2="82" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="565" y1="64" x2="565" y2="82" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="10" y="84" width="210" height="226" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="115" y="104" text-anchor="middle" font-size="13" font-weight="bold">① FTL = 호스트 커널</text>
  <text x="115" y="128" text-anchor="middle" font-size="12">NOR (parallel CFI · SPI)</text>
  <text x="115" y="148" text-anchor="middle" font-size="12">raw SLC NAND (ONFI)</text>
  <text x="115" y="168" text-anchor="middle" font-size="12">SPI-NAND (on-die ECC만)</text>
  <text x="115" y="196" text-anchor="middle" font-size="12">→ MTD + UBI/UBIFS</text>
  <text x="115" y="216" text-anchor="middle" font-size="12">/dev/mtdN (문자 장치)</text>
  <rect x="24" y="234" width="182" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="115" y="256" text-anchor="middle" font-size="12">Linux가 ECC·BBM·WL·GC</text>
  <text x="115" y="278" text-anchor="middle" font-size="12">버그도 우리 책임</text>
  <rect x="235" y="84" width="210" height="226" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="104" text-anchor="middle" font-size="13" font-weight="bold">② 장치 안 작은 FTL</text>
  <text x="340" y="128" text-anchor="middle" font-size="12">eMMC (JEDEC 5.1)</text>
  <text x="340" y="148" text-anchor="middle" font-size="12">SD · microSD</text>
  <text x="340" y="168" text-anchor="middle" font-size="12">UFS (SCSI over UniPro)</text>
  <text x="340" y="196" text-anchor="middle" font-size="12">→ MMC core / ufshcd+SCSI</text>
  <text x="340" y="216" text-anchor="middle" font-size="12">/dev/mmcblkN · /dev/sdX</text>
  <rect x="249" y="234" width="182" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="256" text-anchor="middle" font-size="12">DRAM 거의 없음, PLP 약함</text>
  <text x="340" y="278" text-anchor="middle" font-size="12">블랙박스 — 레지스터만 봄</text>
  <rect x="460" y="84" width="210" height="226" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="104" text-anchor="middle" font-size="13" font-weight="bold">③ SSD 컨트롤러 (Don)</text>
  <text x="565" y="128" text-anchor="middle" font-size="12">NVMe SSD (M.2 · U.2)</text>
  <text x="565" y="148" text-anchor="middle" font-size="12">DRAM 매핑 + PLP 캡</text>
  <text x="565" y="168" text-anchor="middle" font-size="12">다중 SQ/CQ</text>
  <text x="565" y="196" text-anchor="middle" font-size="12">→ nvme-core + nvme(pci)</text>
  <text x="565" y="216" text-anchor="middle" font-size="12">/dev/nvmeXnY</text>
  <rect x="474" y="234" width="182" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="256" text-anchor="middle" font-size="12">SMART · namespace</text>
  <text x="565" y="278" text-anchor="middle" font-size="12">로그 페이지로 잘 보인다</text>
  <text x="340" y="336" text-anchor="middle" font-size="12">virtio-blk(VM): FTL은 호스트 파일 뒤 어딘가 — 게스트는 아무것도 모른다</text>
</svg>
```
그림 1 — 매체를 "FTL 소유자"로 나눈 지도. ①은 Linux가 FTL(UBI)을 돌리므로 투명하지만 책임도 진다. ②는 칩 벤더 펌웨어가 FTL을 갖고, 호스트는 표준 레지스터(EXT_CSD, UFS descriptor) 몇 바이트로만 속을 본다. ③은 Don이 만든 세계 — 가장 잘 보이고(SMART), 가장 비싸다.

Don의 경험과 1:1로 묶으면 이렇다.

| Don이 SSD에서 한 일 | ① raw NAND 보드 | ② eMMC · SD · UFS | ③ NVMe SSD |
|---|---|---|---|
| L2P 매핑 · GC | UBI EBA + UBIFS GC (I11 §3) | 장치 펌웨어 (작은 SRAM, 페이지/하이브리드 매핑) | 장치 펌웨어 (DRAM 1 GB/TB 급) |
| ECC (LDPC/BCH) | SoC NAND 컨트롤러 BCH 또는 on-die ECC | 장치 내부 | 장치 내부 LDPC |
| 배드 블록 · 예약 블록 | MTD BBT + UBI 예약 PEB | 장치 내부, 소진 정도는 `PRE_EOL_INFO`로만 보임 | `Available Spare` (SMART) |
| 마모 정도 보고 | UBI erase counter를 직접 읽음 | `DEVICE_LIFE_TIME_EST_TYP_A/B` (10% 단위) | `Percentage Used` (1% 단위) |
| PLP | 없음 — FS가 책임 (UBIFS 저널) | 보통 없음, 산업용 일부만 | 엔터프라이즈 SSD는 캡 + 펌웨어 flush |
| 호스트 힌트 | — | discard/TRIM, cache flush, reliable write, BKOPS | DSM deallocate, Flush, FUA |

임베디드 Linux 엔지니어가 이 주제에서 실제로 하는 일은 네 가지다: (1) **부품 선정** — 로봇 한 대에 무엇을 몇 GB 넣나, (2) **bring-up** — 장치가 `/dev`에 제대로 나오나, 타이밍 모드(HS200/HS400)는 올라갔나, (3) **수명 설계** — 로그·녹화가 몇 년이면 flash를 다 쓰나, (4) **필드 감시** — 마모와 에러를 원격으로 수집하고 교체 시점을 예측한다.

### 0.1 실습 환경과 예제 1 — 이 노트의 모듈과 부팅 스크립트

VM 호스트 커널은 `7.0.0-38-generic`(`uname -r`), QEMU 10.2.1. 게스트는 공용 랩의 **스토리지/FS 커널** `out/Image-fs`(Linux `6.18.55-fs`: physmap-of CFI NOR 내장, nvme·scsi_debug 모듈, UFS core 내장)로 부팅했다. QEMU는 TCG 에뮬레이션이라 **시간 측정값은 실제 하드웨어와 무관**하다 — 이 노트의 숫자는 "스택 구조의 차이"를 보는 용도로만 쓴다.

무엇을 확인하나: 랩 커널에 빠져 있는 PCI 호스트 드라이버 두 개(`CONFIG_MMC_SDHCI_PCI`, `CONFIG_SCSI_UFSHCD_PCI` 둘 다 `is not set`)를 C2·I11과 같은 방식으로 out-of-tree 모듈로 빌드한다. QEMU `virt`에는 SD 호스트도 UFS 호스트도 없어서, PCI로 꽂는 `sdhci-pci`와 `ufs` 장치를 쓰기 때문이다.

```sh
limactl shell elinux -- bash -lc 'S=/var/lab/src/linux-6.18.55
mkdir -p /var/tmp/J1/sdhci /var/tmp/J1/ufs /var/tmp/J1/bin
cd /var/tmp/J1/sdhci && H=$S/drivers/mmc/host
cp $H/sdhci-pci*.c $H/sdhci-pci*.h $H/sdhci.h $H/cqhci.h $H/sdhci-cqhci.h $H/sdhci-uhs2.[ch] .
printf "obj-m += j1_sdhci_uhs2.o j1_sdhci_pci.o\nj1_sdhci_uhs2-y := sdhci-uhs2.o\nj1_sdhci_pci-y := sdhci-pci-core.o sdhci-pci-o2micro.o sdhci-pci-arasan.o sdhci-pci-dwc-mshc.o sdhci-pci-gli.o\n" > Kbuild
make -j2 -C /var/lab/out/linux-build-fs M=$PWD modules 2>&1 | grep -E "LD|warning|error"; modinfo j1_sdhci_pci.ko | grep vermagic
cd /var/tmp/J1/ufs && cp $S/drivers/ufs/host/ufshcd-pci.c .
printf "obj-m += j1_ufshcd_pci.o\nj1_ufshcd_pci-y := ufshcd-pci.o\n" > Kbuild
make -j2 -C /var/lab/out/linux-build-fs M=$PWD modules 2>&1 | grep -E "LD|warning|error|ERROR"
grep -n "REDHAT" ufshcd-pci.c'
```

```text
  LD [M]  j1_sdhci_uhs2.o
  LD [M]  j1_sdhci_pci.o
  LD [M]  j1_sdhci_uhs2.ko
  LD [M]  j1_sdhci_pci.ko
vermagic:       6.18.55-fs SMP preempt mod_unload aarch64
  LD [M]  j1_ufshcd_pci.o
  LD [M]  j1_ufshcd_pci.ko
684:	{ PCI_VENDOR_ID_REDHAT, 0x0013, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
```

출력에서 볼 것: 경고·에러 없이 두 모듈이 `6.18.55-fs` vermagic으로 빌드됐다. `ufshcd-pci.c`의 ID 표에 **Red Hat 1b36:0013 = QEMU UFS 장치**가 이미 들어 있다 — 커널 커뮤니티가 QEMU UFS로 ufshcd를 테스트한다는 뜻이고, 덕분에 하드웨어 없이 진짜 UFS 스택을 볼 수 있다.

유저 공간 도구는 전부 VM에서 `gcc -static -std=gnu11 -Wall -Wextra -O2`(경고 0개)로 빌드해 `/var/tmp/J1/bin/`에 두었다. 게스트 BusyBox에는 `mmc-utils`, `nvme-cli`, `sg3_utils`, `fio`, `flash_erase`가 없다 — 그래서 각 매체의 표준 명령을 **직접 ioctl로 보내는 작은 C 도구**를 만들었다(실무에서 도구가 없는 최소 rootfs를 만났을 때도 똑같이 한다). 게스트 부팅 스크립트:

```sh
#!/bin/bash
# usage: [QEXTRA="..."] [SMP=n] [TMO=s] [KERNEL=fs] boot.sh <guest-script.sh>
set -e
G=$1; N=$(basename $G .sh); W=/var/tmp/J1/run_$N
rm -rf $W && mkdir -p $W/r && cp -a /var/lab/build/rootfs/. $W/r/ && mkdir -p $W/r/j1 $W/r/lib/modules
M=/var/lab/out/modules-fs/lib/modules/6.18.55-fs; D=$W/r/lib/modules/6.18.55-fs
mkdir -p $D && cp $M/modules.* $D/
(cd $M && cp --parents -r kernel/drivers/mtd kernel/drivers/nvme kernel/drivers/scsi kernel/drivers/mmc \
   kernel/lib kernel/crypto $D/ 2>/dev/null || true)
cp /var/tmp/J1/sdhci/*.ko $W/r/j1/; cp /var/tmp/J1/bin/* $W/r/j1/ 2>/dev/null || true
cp /Users/donh/.elinux_scratch/J1/$G $W/r/j1/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout ${TMO:-300} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp ${SMP:-2} -m 1024M -nographic \
  -no-reboot -kernel /var/lab/out/Image-fs -initrd $W/i.gz $QEXTRA \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/j1/t.sh" > $W/out.txt 2>&1 || echo "qemu rc=$?"
tr -d '\r' < $W/out.txt > /var/tmp/J1/out_$N.txt
echo "saved /var/tmp/J1/out_$N.txt ($(wc -l < /var/tmp/J1/out_$N.txt) lines)"
```

---

## 1. NOR flash — 메모리처럼 읽히는 flash

### 1.1 직관

NOR는 **"읽기는 RAM처럼, 쓰기는 EEPROM처럼"** 이다. 셀이 비트라인에 병렬로 붙어 있어서(그래서 이름이 NOR) 아무 주소나 바로 읽을 수 있다. CPU가 NOR를 메모리 맵에 놓고 명령을 직접 fetch하는 것을 **XIP**(eXecute In Place)라 한다. Don이 Cortex-M에서 내장 flash의 코드를 그대로 실행하던 것과 같은 성질이다.

대신 쓰기가 느리고 용량이 작다. 비트를 1→0으로만 바꿀 수 있고(program), 0→1로 되돌리려면 **섹터 전체를 지워야**(erase) 한다. 그래서 NOR는 보통 **부트 flash**(BL2·U-Boot·환경 변수, C2 §4)와 작은 설정 영역에만 쓴다.

### 1.2 정의 — parallel NOR와 SPI-NOR

| 항목 | parallel NOR (CFI) | SPI-NOR (QSPI/OSPI) |
|---|---|---|
| 인터페이스 | 주소·데이터 버스 (x8/x16) | 1/2/4/8 데이터 선, 직렬 명령 |
| 파라미터 발견 | **CFI** 질의 — 0x55에 0x98을 쓰면 "QRY" 표가 나온다 | **SFDP**(JESD216) — 명령 0x5A로 파라미터 표를 읽는다 |
| erase 단위 | 섹터(보통 64~256 KiB) | 4 KiB 서브섹터(0x20) · 32 KiB(0x52) · 64 KiB(0xD8) |
| program 단위 | 워드(버퍼 program 있음) | 페이지 최대 256 B |
| XIP | 버스에 그대로 맵 | 컨트롤러의 memory-mapped 모드로 가능 |
| 용량 | 수십~수백 MB (요즘 드뭄) | 8~64 MB가 흔함, 최대 수 Gb |
| 내구성 (일반적 데이터시트 값) | 섹터당 약 10만 회 | 섹터당 약 10만 회 |
| Linux | `physmap-of` + `cfi_cmdset_0001/0002` | `spi-nor` 프레임워크 (`drivers/mtd/spi-nor`) |

(표의 수치는 일반적인 데이터시트 범위다 — 기억 기반, 부품별로 확인할 것.)

Linux에서는 둘 다 **MTD 장치**(`/dev/mtdN`, 문자 장치)가 된다. 블록 장치가 아니다 — 지우기 단위가 노출되기 때문이다(I11 §1).

### 1.3 NOR와 NAND의 입자 크기 — 그림으로

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">읽기 · 쓰기 · 지우기 단위 (축척 무시)</text>
  <text x="110" y="48" text-anchor="middle" font-size="13" font-weight="bold">동작</text>
  <text x="300" y="48" text-anchor="middle" font-size="13" font-weight="bold">NOR</text>
  <text x="545" y="48" text-anchor="middle" font-size="13" font-weight="bold">NAND (SLC 기준)</text>
  <line x1="10" y1="58" x2="670" y2="58" stroke="currentColor"/>
  <text x="110" y="92" text-anchor="middle" font-size="13">읽기</text>
  <rect x="200" y="74" width="40" height="28" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="220" y="93" text-anchor="middle" font-size="12">1 B</text>
  <text x="330" y="93" text-anchor="middle" font-size="12">랜덤 · XIP 가능</text>
  <rect x="430" y="74" width="100" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="480" y="93" text-anchor="middle" font-size="12">페이지 2~16K</text>
  <text x="605" y="93" text-anchor="middle" font-size="12">+ OOB · ECC</text>
  <text x="110" y="142" text-anchor="middle" font-size="13">program (1→0)</text>
  <rect x="200" y="124" width="60" height="28" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="230" y="143" text-anchor="middle" font-size="12">B~256B</text>
  <text x="340" y="143" text-anchor="middle" font-size="12">아무 순서나</text>
  <rect x="430" y="124" width="100" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="480" y="143" text-anchor="middle" font-size="12">페이지 통째</text>
  <text x="605" y="143" text-anchor="middle" font-size="12">블록 안 순서대로</text>
  <text x="110" y="192" text-anchor="middle" font-size="13">erase (→1)</text>
  <rect x="200" y="174" width="120" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="260" y="193" text-anchor="middle" font-size="12">섹터 4K~256K</text>
  <text x="375" y="193" text-anchor="middle" font-size="12">수백 ms</text>
  <rect x="430" y="174" width="140" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="500" y="193" text-anchor="middle" font-size="12">블록 128K~수 MB</text>
  <text x="625" y="193" text-anchor="middle" font-size="12">수 ms</text>
  <text x="110" y="242" text-anchor="middle" font-size="13">결함 관리</text>
  <text x="300" y="242" text-anchor="middle" font-size="12">공장 배드 없음, ECC 보통 불필요</text>
  <text x="545" y="242" text-anchor="middle" font-size="12">배드 블록 · ECC 필수</text>
  <text x="110" y="282" text-anchor="middle" font-size="13">용도</text>
  <text x="300" y="282" text-anchor="middle" font-size="12">부트로더 · 작은 설정</text>
  <text x="545" y="282" text-anchor="middle" font-size="12">rootfs · 데이터 (UBI 필요)</text>
</svg>
```
그림 2 — NOR와 NAND의 입자 크기. NOR는 읽기가 바이트 단위라 코드 실행에 맞고, NAND는 모든 것이 페이지·블록 단위라 FTL(또는 UBI) 없이는 파일시스템을 올릴 수 없다. 시간은 일반적인 데이터시트 크기 순서(기억 기반)다.

### 1.4 예제 2 — QEMU virt의 CFI NOR를 MTD로 지우고 쓰기

QEMU `virt` 머신은 DT에 `flash@0 { compatible = "cfi-flash"; bank-width = <4>; }` 노드를 넣고 64 MiB pflash 두 뱅크(0x0, 0x4000000)를 만든다. `Image-fs`에는 `physmap-of`가 내장돼 있어서 별도 DT 없이 MTD가 생긴다.

한 가지 함정을 먼저 만났다: 처음에 `-drive if=pflash,unit=0`에 데이터 파일을 줬더니 **콘솔이 아무것도 출력하지 않고 timeout**됐다. QEMU virt는 pflash0에 내용이 있으면 그것을 **펌웨어로 보고 거기서 부팅**하려 한다(`-kernel`은 fw_cfg로 넘긴다). 그래서 데이터는 **unit=1(두 번째 뱅크)** 에만 넣었다. 실제 보드에서 "부트 NOR에 데이터 파티션을 같이 두는" 설계가 왜 조심스러운지와 같은 이야기다.

무엇을 확인하나: CFI 질의로 칩이 인식되는 과정, MTD가 보고하는 erase/write 단위, sector erase 후 0xFF, 정렬되지 않은 erase가 거부되는지, 그리고 CPU가 NOR를 메모리로 직접 읽는 XIP 시점의 모습.

```c
/* j1_nor.c - CFI NOR via /dev/mtdN: geometry, sector erase, byte program, AND semantics */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <mtd/mtd-user.h>

static double now_us(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
static void dump(int fd, off_t off, const char *tag)
{
    unsigned char b[8];
    if (pread(fd, b, 8, off) != 8) { perror("pread"); exit(1); }
    printf("  %-22s @0x%07lx: %02x %02x %02x %02x %02x %02x %02x %02x\n", tag, (long)off,
           b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
}
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s /dev/mtdN sector\n", argv[0]); return 1; }
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open"); exit(1); }
    struct mtd_info_user mi;
    if (ioctl(fd, MEMGETINFO, &mi) < 0) { perror("MEMGETINFO"); exit(1); }
    printf("type=%u (%s) flags=0x%x size=%u MiB erasesize=%u KiB writesize=%u oobsize=%u\n",
           mi.type, mi.type == MTD_NORFLASH ? "MTD_NORFLASH" : "other", mi.flags,
           mi.size >> 20, mi.erasesize >> 10, mi.writesize, mi.oobsize);
    off_t base = (off_t)atoi(argv[2]) * mi.erasesize;
    dump(fd, base, "before erase");
    struct erase_info_user ei = { .start = (unsigned)base, .length = mi.erasesize };
    double t0 = now_us();
    if (ioctl(fd, MEMERASE, &ei) < 0) { perror("MEMERASE"); exit(1); }
    double t1 = now_us();
    dump(fd, base, "after MEMERASE");
    dump(fd, base + mi.erasesize - 8, "end of sector");
    unsigned char a[4] = { 0xf0, 0x0f, 0x55, 0xaa };      /* program 4 bytes at an odd offset */
    if (pwrite(fd, a, 4, base + 3) != 4) { perror("pwrite1"); exit(1); }
    dump(fd, base, "write f0 0f 55 aa @+3");
    unsigned char b[4] = { 0x3c, 0x3c, 0xff, 0x00 };      /* rewrite WITHOUT erase */
    if (pwrite(fd, b, 4, base + 3) != 4) { perror("pwrite2"); exit(1); }
    dump(fd, base, "rewrite 3c 3c ff 00");
    static unsigned char buf[4096];
    memset(buf, 0x00, sizeof buf);
    double t2 = now_us();
    if (pwrite(fd, buf, sizeof buf, base + 4096) != (ssize_t)sizeof buf) { perror("pwrite3"); exit(1); }
    double t3 = now_us();
    printf("timing (QEMU TCG, not real HW): erase %u KiB = %.0f us, program 4 KiB = %.0f us\n",
           mi.erasesize >> 10, t1 - t0, t3 - t2);
    struct erase_info_user bad = { .start = (unsigned)base + 4096, .length = 4096 };
    if (ioctl(fd, MEMERASE, &bad) < 0) perror("MEMERASE 4 KiB at +4096 (not sector aligned)");
    close(fd);
    return 0;
}
```

게스트 스크립트와 실행(호스트에서 bank1 이미지에 표식 두 개를 미리 써 둔다):

```sh
#!/bin/sh
# QEMU virt pflash (CFI) -> physmap-of -> MTD.  bank0 = empty pflash0, bank1 = j1_nor1.img (mtd0 offset 0x4000000)
cd /j1
echo "== dmesg (cfi / physmap)"; dmesg | grep -iE "cfi|physmap|flash|mtd" | sed 's/^\[[ 0-9.]*\] //'
echo "== /proc/mtd"; cat /proc/mtd
echo "== DT node"; for p in compatible bank-width; do printf "%s: " $p; tr '\0' ' ' < /proc/device-tree/flash@0/$p | od -An -c | head -1; done
echo "== sysfs mtd0"; for a in type size erasesize writesize oobsize flags name; do echo "$a=$(cat /sys/class/mtd/mtd0/$a)"; done
ls -l /dev/mtd0 /dev/mtd0ro 2>&1 | sed 's/  */ /g'
echo "== bank1 marker via /dev/mtd0 at 0x4000000:"; dd if=/dev/mtd0 bs=64k skip=1024 count=1 2>/dev/null | head -c 15; echo
echo "== XIP view: CPU loads from physical 0x04040000 (bank1 sector 1) via devmem"
devmem 0x04040000 32; devmem 0x04040004 32
echo "== j1_nor mtd0 sector 257 (= bank1 sector 1)"; ./j1_nor /dev/mtd0 257
echo "== XIP view again after MTD erase/program"
devmem 0x04040000 32; devmem 0x04040004 32
```

```sh
cd /var/tmp/J1 && truncate -s 64M j1_nor1.img
printf "J1-NOR-BANK1-S0" | dd of=j1_nor1.img conv=notrunc status=none
printf "J1SEC1" | dd of=j1_nor1.img bs=256K seek=1 conv=notrunc status=none
QEXTRA="-drive if=pflash,unit=1,format=raw,file=/var/tmp/J1/j1_nor1.img" TMO=100 bash ~/.elinux_scratch/J1/boot.sh g_nor.sh
od -An -tx1 -j $((0x40000)) -N 8 j1_nor1.img      # host side, after the guest powered off
```

```text
lab initramfs: 6.18.55-fs up 1.31s
== dmesg (cfi / physmap)
physmap-flash 0.flash: physmap platform flash device: [mem 0x00000000-0x03ffffff]
0.flash: Found 2 x16 devices at 0x0 in 32-bit bank. Manufacturer ID 0x000000 Chip ID 0x000000
physmap-flash 0.flash: physmap platform flash device: [mem 0x04000000-0x07ffffff]
0.flash: Found 2 x16 devices at 0x0 in 32-bit bank. Manufacturer ID 0x000000 Chip ID 0x000000
Concatenating MTD devices:
(0): "0.flash"
(1): "0.flash"
into device "0.flash"
== /proc/mtd
dev:    size   erasesize  name
mtd0: 08000000 00040000 "0.flash"
== DT node
compatible:    c   f   i   -   f   l   a   s   h    
bank-width:              004
== sysfs mtd0
type=nor
size=134217728
erasesize=262144
writesize=1
oobsize=0
flags=0xc00
name=0.flash
crw------- 1 0 0 90, 0 Jan 1 1970 /dev/mtd0
crw------- 1 0 0 90, 1 Jan 1 1970 /dev/mtd0ro
== bank1 marker via /dev/mtd0 at 0x4000000:
J1-NOR-BANK1-S0
== XIP view: CPU loads from physical 0x04040000 (bank1 sector 1) via devmem
0x4553314A
0x00003143
== j1_nor mtd0 sector 257 (= bank1 sector 1)
type=3 (MTD_NORFLASH) flags=0xc00 size=128 MiB erasesize=256 KiB writesize=1 oobsize=0
  before erase           @0x4040000: 4a 31 53 45 43 31 00 00
  after MEMERASE         @0x4040000: ff ff ff ff ff ff ff ff
  end of sector          @0x407fff8: ff ff ff ff ff ff ff ff
  write f0 0f 55 aa @+3  @0x4040000: ff ff ff f0 0f 55 aa ff
  rewrite 3c 3c ff 00    @0x4040000: ff ff ff 3c 3c ff 00 ff
timing (QEMU TCG, not real HW): erase 256 KiB = 1282 us, program 4 KiB = 475 us
MEMERASE 4 KiB at +4096 (not sector aligned): Invalid argument
== XIP view again after MTD erase/program
0x00800080
0x00800080
 ff ff ff 3c 3c ff 00 ff
```

(마지막 줄은 게스트가 꺼진 뒤 VM 호스트에서 읽은 이미지 파일 바이트다.)

출력에서 볼 것:

- `Found 2 x16 devices at 0x0 in 32-bit bank`: CFI 질의 결과 16비트 칩 두 개가 32비트 버스에 **interleave**로 붙어 있다(`bank-width = <4>`). 그래서 erase 단위가 칩 하나의 128 KiB가 아니라 **256 KiB**(`erasesize=262144`)로 보인다 — 두 칩이 같이 지워지기 때문이다. 실제 보드에서도 interleave하면 erase 단위가 배가 된다.
- 두 뱅크가 `Concatenating MTD devices`로 128 MiB `mtd0` 하나가 됐다(DT `reg`에 두 영역). bank1은 `mtd0`의 0x4000000부터다.
- `type=nor`, `writesize=1`, `oobsize=0`: NOR는 1바이트도 program할 수 있고 OOB가 없다. NAND였다면 `writesize=2048`, `oobsize=64` 같은 값이 나온다(I11 예제 2).
- `MEMERASE 4 KiB ... Invalid argument`: erase는 반드시 erasesize 정렬·배수여야 한다. MTD가 막는다.
- **XIP 뷰**: 처음 `devmem 0x04040000`은 이미지에 쓴 `J1SEC1`(`0x4553314A` = "J1SE" little-endian)을 그대로 읽었다 — CPU의 load가 NOR 셀을 직접 읽는다. 그런데 MTD로 erase/program한 **뒤**에는 `0x00800080`이 나왔다. 이것은 데이터가 아니라 두 칩의 **status register**(bit7 = WSM ready)다. CFI Intel 명령 세트에서 칩은 program 후 "status 읽기 모드"에 머물고, 커널 `cfi_cmdset_0001`은 다음에 자기가 읽을 때가 돼서야 Read Array(0xFF) 명령을 보낸다. 즉 **flash에 쓰는 동안 같은 칩에서 XIP로 코드를 실행하면 코드 대신 status 값을 fetch한다**. 이것이 XIP 커널(ARM32 `XIP_KERNEL` + `CONFIG_MTD_XIP`)이 쓰기 루틴을 RAM에 두고 인터럽트를 막는 이유다(`__xipram` 표시가 그것).
- **에뮬레이터의 거짓말**: `f0 0f 55 aa` 위에 erase 없이 `3c 3c ff 00`을 썼더니 `3c 3c ff 00`이 됐다. 진짜 NOR라면 비트를 1→0으로만 바꿀 수 있으므로 결과는 AND인 `30 0c 55 00`이어야 한다. QEMU의 `pflash_cfi01` 모델은 이 물리 제약을 흉내 내지 않는다. 그래서 "에뮬레이터에서 됐다"는 flash 코드의 정확성 증거가 될 수 없다 — 실제 칩이나 `nandsim`/`mtdram`처럼 의미론을 구현한 시뮬레이터로 확인해야 한다(I11 예제 4에서 nandsim은 AND 결과를 보였다).
- 타이밍(erase 256 KiB = 약 1.3 ms)은 QEMU 값이라 의미 없다. 실제 NOR 섹터 erase는 수백 ms~1초 대다(기억 기반 일반값).

### 1.5 함정

- SPI-NOR의 **4-byte 주소 모드**: 16 MB를 넘는 SPI-NOR는 4-byte 주소가 필요하다. 리셋 핀이 없는 보드에서 Linux가 4-byte 모드로 바꿔 놓고 soft reset하면 BootROM이 3-byte로 읽다가 부팅에 실패한다(C2 §4.6에서 다룬 함정). 
- **쓰기 보호 비트**(BP0~BP3, SRWD): 공장에서 잠근 채 오면 erase가 조용히 실패하거나 EIO가 난다. `spi-nor`는 부팅 때 BP를 풀지 여부를 결정한다(`CONFIG_MTD_SPI_NOR_SWP_*`).
- NOR는 erase 시간이 길어서 **환경 변수 저장이 부팅을 수백 ms 늦출 수** 있다. U-Boot env를 redundant 두 섹터로 두는 이유는 전원 차단 안전성이다(C3).

---

## 2. raw NAND와 SPI-NAND — FTL이 없는 NAND

### 2.1 정의

raw NAND는 Don이 SSD 안에서 다루던 바로 그 칩이 SoC의 NAND 컨트롤러(또는 SPI)에 **직접** 붙은 것이다. 호스트가 READ ID, PAGE PROGRAM, BLOCK ERASE를 직접 보낸다. ONFI 파라미터 페이지(칩이 자기 geometry·타이밍을 알려 주는 256 B 표)로 칩을 식별하는 것도 같다.

| 종류 | ECC 위치 | Linux 경로 | 비고 |
|---|---|---|---|
| parallel SLC NAND | SoC NAND 컨트롤러의 BCH 엔진 또는 SW BCH | `nand_base` + 컨트롤러 드라이버(`exec_op`) | 공장 배드 블록 표시, OOB에 ECC |
| SPI-NAND | **on-die ECC**(칩 안에서 정정, 상태 비트로 보고) | `spinand` 프레임워크 (`drivers/mtd/nand/spi`) | 핀 수 적음, 1~4 Gb 흔함 |
| MLC/TLC raw NAND | 강한 ECC 필요, paired-page 문제 | 사실상 비권장 | UBI/UBIFS는 SLC를 전제로 설계됐다(기억 기반) |

on-die ECC는 FTL이 아니다. 비트 오류는 칩이 고쳐 주지만 **배드 블록, wear leveling, 매핑은 여전히 호스트(UBI) 몫**이다. Don에게 익숙한 말로 하면 "ECC 엔진만 NAND 안으로 들어갔고 FTL 펌웨어는 Linux 커널이 된 SSD"다.

### 2.2 Linux에서 보이는 모습 — 이미 본 것

`/dev/mtdN`, `/proc/mtd`, `MEMGETINFO`·`MEMGETBADBLOCK`·`ECCGETSTATS`, erase 없이 두 번 쓰면 AND가 되는 의미론, UBI attach·EC/VID 헤더·wear leveling 측정, 런타임 배드 블록으로 예약 PEB가 고갈돼 R/O가 되는 과정은 **I11 §1~§3 예제 2~9**와 C2 예제 7(nandsim 배드 블록·ECC)에서 이미 실행했다. 이 노트는 반복하지 않는다. UBI/UBIFS·JFFS2의 파일시스템 쪽 설계는 **J4**에서 다룬다.

### 2.3 언제 raw NAND를 고르나

- 수량이 많고 BOM 단가가 중요하며, 저장할 것이 작다(256 MB~1 GB SLC).
- 극한 온도·긴 수명 요구로 **SLC의 P/E 내구성**(일반적으로 수만 회 이상)이 필요하다.
- SoC BootROM이 NAND 부팅을 지원하고, 팀에 UBI/UBIFS 경험이 있다.
- 반대로: 용량이 4 GB 이상 필요하거나, 팀이 FTL을 책임질 여력이 없으면 eMMC로 간다(§11).

---

## 3. eMMC — 납땜하는 작은 SSD

### 3.1 직관

eMMC는 NAND die 몇 개 + 작은 컨트롤러(FTL·ECC·배드 블록) + MMC 버스 인터페이스를 BGA 패키지 하나에 넣은 것이다. Don이 만든 SSD를 **DRAM 없이, 큐 하나로, 손톱만 하게** 줄였다고 보면 된다. SoC에 SD/MMC 호스트가 거의 항상 있고 BootROM이 거의 항상 eMMC 부팅을 지원하기 때문에, 오늘날 임베디드 Linux 장비(i.MX8, AM62, Jetson AGX Orin 모듈 등)의 기본 저장장치다.

### 3.2 JEDEC eMMC 5.1이 정한 것 — 하드웨어 파티션과 레지스터

표준은 JEDEC JESD84-B51(eMMC 5.1)이다. C2 §6에서 boot0/boot1·RPMB·`PARTITION_CONFIG[179]`(어느 boot 파티션으로 부팅할지)는 이미 다뤘다. 여기서는 **수명과 신뢰성에 관련된** 나머지를 본다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">eMMC 하나의 속 — 파티션 · 컨트롤러 · 레지스터</text>
  <text x="130" y="44" text-anchor="middle" font-size="13" font-weight="bold">하드웨어 파티션 (NAND 위)</text>
  <rect x="20" y="54" width="220" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="130" y="74" text-anchor="middle" font-size="12">boot0 · boot1 (보통 4 MiB씩)</text>
  <rect x="20" y="90" width="220" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="130" y="110" text-anchor="middle" font-size="12">RPMB (인증 쓰기, 문자 장치)</text>
  <rect x="20" y="126" width="220" height="30" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="130" y="146" text-anchor="middle" font-size="12">GP1~GP4 (선택, 한 번만 설정)</text>
  <rect x="20" y="162" width="220" height="150" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="182" text-anchor="middle" font-size="12">user area (MBR/GPT · p1 p2 …)</text>
  <rect x="36" y="194" width="188" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="130" y="212" text-anchor="middle" font-size="12">enhanced area = pSLC</text>
  <text x="130" y="230" text-anchor="middle" font-size="12">ENH_SIZE_MULT로 크기 지정</text>
  <text x="130" y="262" text-anchor="middle" font-size="12">나머지는 MLC/TLC 그대로</text>
  <text x="130" y="296" text-anchor="middle" font-size="12">PARTITION_SETTING_COMPLETED=1</text>
  <text x="130" y="336" text-anchor="middle" font-size="12">설정 후 전원 재인가 → 영구</text>
  <text x="490" y="44" text-anchor="middle" font-size="13" font-weight="bold">컨트롤러 + EXT_CSD (512 B)</text>
  <rect x="320" y="54" width="340" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="490" y="72" text-anchor="middle" font-size="12">FTL · ECC · 배드 블록 · wear leveling</text>
  <text x="490" y="90" text-anchor="middle" font-size="12">BKOPS: BKOPS_EN[163] · BKOPS_STATUS[246]</text>
  <rect x="320" y="106" width="340" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="490" y="124" text-anchor="middle" font-size="12">휘발성 캐시: CACHE_SIZE[252:249]</text>
  <text x="490" y="142" text-anchor="middle" font-size="12">CACHE_CTRL[33] · FLUSH_CACHE[32]</text>
  <rect x="320" y="158" width="340" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="490" y="176" text-anchor="middle" font-size="12">수명: PRE_EOL_INFO[267]</text>
  <text x="490" y="194" text-anchor="middle" font-size="12">LIFE_TIME_EST_TYP_A[268] · TYP_B[269]</text>
  <rect x="320" y="210" width="340" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="490" y="228" text-anchor="middle" font-size="12">신뢰성: WR_REL_PARAM[166] · REL_WR_SEC_C[222]</text>
  <text x="490" y="246" text-anchor="middle" font-size="12">erase: SEC_FEATURE_SUPPORT[231]</text>
  <rect x="320" y="262" width="340" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="490" y="280" text-anchor="middle" font-size="12">버스: DEVICE_TYPE[196] · HS_TIMING[185]</text>
  <text x="490" y="298" text-anchor="middle" font-size="12">BUS_WIDTH[183] (HS200 · HS400)</text>
  <line x1="490" y1="360" x2="490" y2="310" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="490" y="378" text-anchor="middle" font-size="12">호스트: CMD8 읽기 · CMD6 SWITCH 쓰기 · CMD23 reliable</text>
  <text x="490" y="396" text-anchor="middle" font-size="12">CMD35/36/38 erase · trim · discard · sanitize</text>
</svg>
```
그림 3 — eMMC 안의 두 세계. 왼쪽은 NAND 위의 하드웨어 파티션(Linux에 각각 별도 장치로 보인다), 오른쪽은 컨트롤러가 EXT_CSD 바이트로 노출하는 상태와 설정. 대괄호 숫자는 EXT_CSD 바이트 오프셋이다.

핵심 필드를 하나씩:

| 기능 | EXT_CSD | 뜻 | Linux에서 |
|---|---|---|---|
| **enhanced area (pSLC)** | `ENH_START_ADDR[139:136]`, `ENH_SIZE_MULT[142:140]`, `PARTITIONS_ATTRIBUTE[156]`, `PARTITION_SETTING_COMPLETED[155]`, `MAX_ENH_SIZE_MULT[159:157]` | user area 일부(또는 GP)를 SLC처럼 1비트/셀로 쓰게 한다. 용량은 MLC면 1/2, TLC면 1/3로 줄고 내구성·전원 차단 강도가 크게 오른다 | sysfs `enhanced_area_offset/size`, 설정은 `mmc enh_area set` (mmc-utils) |
| **수명 추정** | `DEVICE_LIFE_TIME_EST_TYP_A[268]`, `TYP_B[269]` | 0x01 = 0~10% 사용 … 0x0A = 90~100%, 0x0B = 추정 수명 초과. A는 SLC(또는 pSLC) 영역, B는 MLC/TLC 영역 | sysfs `life_time` ("A B") — eMMC 5.0(rev 7)부터 |
| **예약 블록 상태** | `PRE_EOL_INFO[267]` | 0x01 정상, 0x02 경고(예약 블록 80% 소모), 0x03 긴급 | sysfs `pre_eol_info` — 5.0부터 |
| **캐시** | `CACHE_SIZE`, `CACHE_CTRL[33]`, `FLUSH_CACHE[32]` | 장치 내부 휘발성 쓰기 캐시. 켜면 빠르지만 flush 전 전원 차단 시 유실 | 커널이 **초기화 때 자동으로 켠다**, REQ_PREFLUSH → `FLUSH_CACHE` (J5) |
| **BKOPS** | `BKOPS_SUPPORT[502]`, `BKOPS_EN[163]`(bit0 manual, bit1 auto), `BKOPS_STATUS[246]` | GC 같은 배경 작업. 호스트가 시간을 주지 않으면 쓰기 도중 GC로 지연 급증 | 커널은 manual BKOPS가 켜진 카드에서 urgent(레벨 2 이상)일 때만 `mmc_run_bkops` |
| **reliable write** | `WR_REL_PARAM[166]`, `REL_WR_SEC_C[222]` | CMD23의 reliable 비트: 전원이 끊겨도 해당 섹터들이 옛 값 또는 새 값 중 하나로만 남는다 | `mmc_blk`가 **REQ_FUA를 reliable write로 구현** (아래 예제에서 확인) |
| **erase 종류** | `SEC_FEATURE_SUPPORT[231]`, `ERASE_GROUP_DEF[175]`, `HC_ERASE_GRP_SIZE[224]` | ERASE(erase group 단위), TRIM(write block 단위), DISCARD(4.5+, 장치가 원하면 나중에), secure erase/trim(4.51부터 비권장), SANITIZE(`SANITIZE_START[165]`, 4.5+) | `BLKDISCARD` → `card->erase_arg`(DISCARD → TRIM → ERASE 순으로 지원되는 것), `BLKSECDISCARD` → secure trim |
| **버스 속도** | `DEVICE_TYPE[196]`, `HS_TIMING[185]`, `BUS_WIDTH[183]` | HS(52 MHz SDR), DDR52, HS200(200 MHz SDR 8bit 1.8 V, 튜닝 CMD21), HS400(200 MHz DDR + Data Strobe) | DT `mmc-hs200-1_8v`, `mmc-hs400-1_8v`, debugfs `ios` |

(바이트 오프셋과 rev별 도입 시점은 커널 `include/linux/mmc/mmc.h`와 `drivers/mmc/core/mmc.c`에서 확인했다. 속도 수치·deprecated 시점은 JEDEC 일반 지식 — 기억 기반.)

**한 번만 쓸 수 있는 설정**이 eMMC의 가장 무서운 점이다. enhanced area, GP 파티션, `RST_n_FUNCTION`, boot 파티션 영구 쓰기 보호는 `PARTITION_SETTING_COMPLETED` 또는 해당 비트를 쓰고 전원을 재인가하면 **되돌릴 수 없다**. 그래서 이 설정은 개발자 책상이 아니라 **공장 프로비저닝 스크립트**(C2 §9.3)에서 한 번, 검증된 값으로만 한다.

Don의 시각으로 pSLC를 보면: SSD 펌웨어가 TLC 블록 일부를 SLC 모드로 써서 쓰기 캐시로 쓰던 것(SLC cache)을, eMMC에서는 **호스트가 LBA 범위를 지정해 영구적으로** SLC로 만드는 것이다. 로그·DB처럼 자주 쓰는 데이터 파티션을 그 범위에 두면 그 영역은 내구성이 높아지고, rootfs(거의 읽기 전용)는 TLC 영역에 둔다.

### 3.3 예제 3 — eMMC를 붙이고 Linux가 만드는 노드와 sysfs 보기

무엇을 확인하나: eMMC 하나가 블록 장치 3개 + RPMB 문자 장치 1개로 나뉘는 것(C2에서 본 것의 재확인은 짧게), 그리고 **수명·신뢰성 관련 sysfs 속성**.

```sh
#!/bin/sh
# eMMC or SD card behind sdhci-pci (out-of-tree j1_sdhci_pci.ko)
cd /j1
insmod j1_sdhci_uhs2.ko && insmod j1_sdhci_pci.ko && sleep 2
echo "== dmesg"; dmesg | grep -E "mmc" | sed 's/^\[[ 0-9.]*\] //'
echo "== /dev"; ls -l /dev/mmc* | sed 's/  */ /g' | cut -d" " -f1,5-6,10
D=/sys/block/mmcblk0/device
echo "== sysfs $D (type=$(cat $D/type))"
for a in name manfid oemid date fwrev hwrev serial cid csd erase_size preferred_erase_size \
         pre_eol_info life_time enhanced_area_offset enhanced_area_size rel_sectors raw_rpmb_size_mult ocr cmdq_en; do
  [ -f $D/$a ] && printf "%-22s %s\n" $a "$(cat $D/$a)"; done
echo "== debugfs ios"; cat /sys/kernel/debug/mmc0/ios
if [ "$(cat $D/type)" = MMC ]; then echo "== j1_extcsd"; ./j1_extcsd /dev/mmcblk0; fi
echo "== block layer view"; ./j1_blkinfo mmcblk0 discard
[ -e /dev/mmcblk0boot0 ] && ./j1_blkinfo mmcblk0boot0
```

```sh
cd /var/tmp/J1 && truncate -s $((64*1024*1024+8*1024*1024+128*1024)) j1_emmc.img
QEXTRA="-device sdhci-pci -drive if=none,id=e0,file=/var/tmp/J1/j1_emmc.img,format=raw \
 -device emmc,drive=e0,boot-partition-size=4194304,rpmb-partition-size=131072,boot-config=0x48" \
 TMO=150 bash ~/.elinux_scratch/J1/boot.sh g_mmc.sh
```

```text
lab initramfs: 6.18.55-fs up 1.30s
== dmesg
mmc0: SDHCI controller on PCI [0000:00:02.0] using ADMA
mmc0: new high speed MMC card at address 0001
mmcblk0: mmc0:0001 QEMU!! 64.0 MiB
mmcblk0boot0: mmc0:0001 QEMU!! 4.00 MiB
mmcblk0boot1: mmc0:0001 QEMU!! 4.00 MiB
mmcblk0rpmb: mmc0:0001 QEMU!! 128 KiB, chardev (511:0)
== /dev
brw------- 179, 0 /dev/mmcblk0
brw------- 179, 32 /dev/mmcblk0boot0
brw------- 179, 64 /dev/mmcblk0boot1
crw------- 511, 0 /dev/mmcblk0rpmb
== sysfs /sys/block/mmcblk0/device (type=MMC)
name                   QEMU!!
manfid                 0x0000aa
oemid                  0x0158
date                   02/2022
fwrev                  0x0
hwrev                  0x0
serial                 0xdeadbeef
cid                    aa015851454d55212101deadbeef2900
csd                    d00e000b0f5a801fffffdfff92600000
erase_size             524288
preferred_erase_size   524288
pre_eol_info           0x00
life_time              0x00 0x00
enhanced_area_offset   18446744073709551594
enhanced_area_size     4294967274
rel_sectors            0x1
raw_rpmb_size_mult     0x1
ocr                    0x00300000
cmdq_en                0
== debugfs ios
clock:		52000000 Hz
actual clock:	52000000 Hz
vdd:		21 (3.3 ~ 3.4 V)
bus mode:	2 (push-pull)
chip select:	0 (don't care)
power mode:	2 (on)
bus width:	2 (4 bits)
timing spec:	1 (mmc high-speed)
signal voltage:	0 (3.30 V)
driver type:	0 (driver type B)
```

출력에서 볼 것:

- `cid`의 앞 바이트 `aa`가 `manfid 0x0000aa`, 뒤의 `deadbeef`가 `serial`이다. 실제 장비에서는 **manfid + 제품명 + fwrev**로 부품을 식별해 필드 데이터를 묶는다. 같은 모델이라도 펌웨어 리비전마다 버그가 다르기 때문이다(커널 `drivers/mmc/core/quirks.h`가 그 목록이다).
- `pre_eol_info 0x00`, `life_time 0x00 0x00`: QEMU eMMC는 EXT_CSD rev 5(eMMC 4.41)를 흉내 낸다. 커널은 `ext_csd.rev >= 7`(eMMC 5.0)일 때만 이 바이트를 읽으므로 0이다. 실제 5.x 부품이면 `0x01`과 `0x01 0x01` 같은 값이 나온다.
- `enhanced_area_offset 18446744073709551594`: 이상한 숫자가 아니라 **-22(-EINVAL)를 u64로 찍은 것**이다. 커널 `mmc.c`는 enhanced area가 정의되지 않았으면 이 값을 넣어 둔다. 모니터링 스크립트가 이 값을 그대로 "오프셋"으로 믿으면 안 된다.
- `rel_sectors 0x1`: reliable write 지원. 아래 예제 10에서 이 때문에 `fua=1`이 된다.
- `erase_size 524288`: erase group 512 KiB. discard를 이보다 작게 보내면 장치가 무시하거나 커널이 잘라 낸다.
- `ios`: `timing spec: 1 (mmc high-speed)`, 52 MHz, 4비트. QEMU의 sdhci-pci는 HS200/HS400과 8비트를 흉내 내지 않는다(I11 §5.4). 실제 보드라면 여기서 `timing spec: 9 (mmc HS200)`이나 `10 (mmc HS400)`이 나와야 하고, 안 나오면 DT의 `bus-width = <8>`, `mmc-hs200-1_8v`, 1.8 V 레귤레이터(vqmmc)부터 의심한다.

### 3.4 예제 4 — EXT_CSD를 직접 읽고 해석하기

무엇을 확인하나: mmc-utils의 `mmc extcsd read`가 하는 일 — `MMC_IOC_CMD`로 CMD8(SEND_EXT_CSD)을 보내 512 B를 받고, 수명·캐시·BKOPS·enhanced area 필드를 해석한다. C2 예제 9는 부팅 관련 바이트만 봤고, 여기서는 **필드 건강 모니터링에 필요한 바이트**를 본다.

```c
/* j1_extcsd.c - read eMMC EXT_CSD with MMC_IOC_CMD (CMD8) and decode health/feature fields */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/mmc/ioctl.h>

#define RSP_PRESENT (1u << 0)        /* same values as kernel <linux/mmc/core.h> (not in UAPI) */
#define RSP_CRC     (1u << 2)
#define RSP_OPCODE  (1u << 4)
#define CMD_ADTC    (1u << 5)
#define R1 (RSP_PRESENT | RSP_CRC | RSP_OPCODE)

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static const char *eol[4] = { "undefined", "normal", "warning(80% reserved used)", "urgent" };

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s /dev/mmcblkN\n", argv[0]); return 1; }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { perror("open"); exit(1); }
    uint8_t e[512] __attribute__((aligned(512)));
    struct mmc_ioc_cmd c = { .opcode = 8, .flags = R1 | CMD_ADTC, .blksz = 512, .blocks = 1 };
    mmc_ioc_cmd_set_data(c, e);
    if (ioctl(fd, MMC_IOC_CMD, &c) < 0) { perror("MMC_IOC_CMD CMD8"); exit(1); }
    static const char *rev[] = { "4.0", "4.1", "4.2", "4.3", "obsolete", "4.41", "4.5", "5.0", "5.1" };
    printf("EXT_CSD_REV[192]           = %u (eMMC %s)  resp[0]=0x%08x\n", e[192],
           e[192] <= 8 ? rev[e[192]] : "5.1+", c.response[0]);
    printf("SEC_COUNT[215:212]         = %u sectors (%u MiB user area)\n", le32(e + 212), le32(e + 212) >> 11);
    printf("DEVICE_TYPE[196]           = 0x%02x  HS_TIMING[185]=%u  BUS_WIDTH[183]=%u\n", e[196], e[185], e[183]);
    printf("BOOT_SIZE_MULT[226]        = %u x128K  RPMB_SIZE_MULT[168]=%u x128K\n", e[226], e[168]);
    printf("PARTITIONING_SUPPORT[160]  = 0x%02x  PARTITION_SETTING_COMPLETED[155]=%u\n", e[160], e[155]);
    printf("MAX_ENH_SIZE_MULT[159:157] = %u  ENH_SIZE_MULT[142:140]=%u  ENH_START_ADDR[139:136]=%u\n",
           e[157] | e[158] << 8 | e[159] << 16, e[140] | e[141] << 8 | e[142] << 16, le32(e + 136));
    printf("HC_ERASE_GRP_SIZE[224]     = %u x512K  HC_WP_GRP_SIZE[221]=%u  ERASE_GROUP_DEF[175]=%u\n",
           e[224], e[221], e[175]);
    printf("SEC_FEATURE_SUPPORT[231]   = 0x%02x (bit0 SECURE_ER_EN, bit4 SEC_GB_CL_EN, bit6 SEC_SANITIZE)\n", e[231]);
    printf("WR_REL_PARAM[166]          = 0x%02x  REL_WR_SEC_C[222]=%u\n", e[166], e[222]);
    printf("CACHE_SIZE[252:249]        = %u KiB  CACHE_CTRL[33]=%u\n", le32(e + 249), e[33]);
    printf("BKOPS_SUPPORT[502]         = %u  BKOPS_EN[163]=0x%02x  BKOPS_STATUS[246]=%u\n", e[502], e[163], e[246]);
    printf("PRE_EOL_INFO[267]          = %u (%s)\n", e[267], e[267] < 4 ? eol[e[267]] : "reserved");
    printf("DEVICE_LIFE_TIME_EST_TYP_A[268] = %u  TYP_B[269] = %u  (1 = 0-10%% used ... 11 = exceeded)\n",
           e[268], e[269]);
    printf("FW_VERSION[261:254]        = %02x%02x%02x%02x%02x%02x%02x%02x\n",
           e[261], e[260], e[259], e[258], e[257], e[256], e[255], e[254]);
    close(fd);
    return 0;
}
```

```text
== j1_extcsd
EXT_CSD_REV[192]           = 5 (eMMC 4.41)  resp[0]=0x00000900
SEC_COUNT[215:212]         = 131072 sectors (64 MiB user area)
DEVICE_TYPE[196]           = 0x03  HS_TIMING[185]=1  BUS_WIDTH[183]=1
BOOT_SIZE_MULT[226]        = 32 x128K  RPMB_SIZE_MULT[168]=1 x128K
PARTITIONING_SUPPORT[160]  = 0x07  PARTITION_SETTING_COMPLETED[155]=0
MAX_ENH_SIZE_MULT[159:157] = 0  ENH_SIZE_MULT[142:140]=0  ENH_START_ADDR[139:136]=0
HC_ERASE_GRP_SIZE[224]     = 1 x512K  HC_WP_GRP_SIZE[221]=1  ERASE_GROUP_DEF[175]=1
SEC_FEATURE_SUPPORT[231]   = 0x00 (bit0 SECURE_ER_EN, bit4 SEC_GB_CL_EN, bit6 SEC_SANITIZE)
WR_REL_PARAM[166]          = 0x00  REL_WR_SEC_C[222]=1
CACHE_SIZE[252:249]        = 0 KiB  CACHE_CTRL[33]=0
BKOPS_SUPPORT[502]         = 0  BKOPS_EN[163]=0x00  BKOPS_STATUS[246]=0
PRE_EOL_INFO[267]          = 0 (undefined)
DEVICE_LIFE_TIME_EST_TYP_A[268] = 0  TYP_B[269] = 0  (1 = 0-10% used ... 11 = exceeded)
FW_VERSION[261:254]        = 0000000000000000
```

출력에서 볼 것:

- `resp[0]=0x00000900`: CMD8의 R1 응답(card status). bit 12:9 CURRENT_STATE = 4 → TRAN 상태, bit8 READY_FOR_DATA. 응답이 정상이라는 뜻이다. 이 값을 확인하지 않으면 엉뚱한 상태에서 읽은 0 바이트를 "정상 값"으로 믿게 된다.
- `PARTITIONING_SUPPORT=0x07`(enhanced·GP 지원, 확장 속성)인데 `MAX_ENH_SIZE_MULT=0`: QEMU는 pSLC 영역을 만들 수 없다. 실제 부품에서는 `MAX_ENH_SIZE_MULT x HC_WP_GRP_SIZE x HC_ERASE_GRP_SIZE x 512 KiB`가 만들 수 있는 최대 enhanced 크기다.
- `CACHE_SIZE=0`: 캐시 없음. 실제 eMMC 5.x는 보통 수백 KiB~수 MiB 캐시가 있고 커널이 `CACHE_CTRL=1`로 켠다(`mmc_init_card`에서 `cache_size > 0`이면 CMD6).
- `SEC_FEATURE_SUPPORT=0x00`: secure erase도 TRIM도 sanitize도 없다. 그래서 커널은 discard를 **일반 ERASE**(erase group 단위)로 구현한다 — 예제 10에서 `BLKSECDISCARD`가 `Operation not supported`인 이유.
- 실제 장비 값으로 읽는 법: 사용 비율 = `(TYP_B - 1) x 10%`에서 `TYP_B x 10%` 사이. 10% 단위라 해상도가 낮다. **변화 시점을 기록**하는 것이 중요하다(§10).

### 3.5 eMMC 함정

- **캐시 + 전원 차단**: 커널은 캐시를 켜고, FS는 `fsync`·저널 커밋 때 flush를 보낸다. 전원이 갑자기 끊기면 flush되지 않은 쓰기는 사라진다 — 부품에 따라 FTL 메타데이터까지 깨져 장치 전체가 읽기 전용이 되는 사례도 있다(벤더 power-loss 테스트 보고서를 요구할 것). J5에서 자세히.
- **erase group 정렬**: 파티션 시작을 erase group(보통 512 KiB~수 MiB) 정렬하지 않으면 discard가 무시되고 WAF가 올라간다. 1 MiB 또는 4 MiB 정렬이 관례다.
- **discard 마운트 옵션**: `-o discard`(온라인)는 eMMC에서 delete마다 CMD38을 보내 지연을 만든다. 주기적 `fstrim`이 보통 낫다(J2).
- **HS400 튜닝 실패**: 온도·보드 리비전에 따라 튜닝이 실패해 HS200이나 DDR52로 떨어지면 dmesg에 남는다. 필드 텔레메트리에 `ios`의 timing을 넣어 두면 하드웨어 문제를 원격으로 잡는다(I11 §8.2).

---

## 4. SD 카드 — 꺼낼 수 있는 eMMC의 사촌

### 4.1 정의와 등급

SD 카드는 프로토콜이 eMMC와 비슷하지만(같은 MMC 코어, `core/sd.c`) **하드웨어 boot 파티션도 RPMB도 EXT_CSD도 없다**. 대신 CID/CSD/SCR/SSR 레지스터와 등급 표시가 있다.

| 등급 | 뜻 (SD Association 정의, 기억 기반) |
|---|---|
| Speed Class C2/C4/C6/C10 | 최소 순차 쓰기 2/4/6/10 MB/s |
| UHS Speed Class U1/U3 | 최소 순차 쓰기 10/30 MB/s (UHS 버스) |
| Video Speed Class V6~V90 | 최소 순차 쓰기 6~90 MB/s (녹화용) |
| **Application Performance A1** | 랜덤 4 KiB 읽기 1500 IOPS, 쓰기 500 IOPS, 순차 10 MB/s |
| **A2** | 랜덤 읽기 4000, 쓰기 2000 IOPS — 단 **호스트가 command queue와 cache를 켜야** 나온다 |

임베디드에서 중요한 것은 Speed Class가 아니라 **A1/A2**다. rootfs와 로그는 랜덤 4 KiB 쓰기이기 때문이다. 그리고 A2 카드를 꽂아도 호스트 드라이버가 SD의 CQ/cache 기능을 쓰지 않으면 A1 수준이다.

### 4.2 SD를 제품 저장장치로 쓰면 안 되는 이유

- **전원 차단 보장이 없다**. 소비자용 카드의 FTL은 쓰기 도중 전원이 끊기는 것을 거의 고려하지 않는다. 쓰던 블록뿐 아니라 매핑 테이블이 깨져 카드 전체가 죽는 경우가 흔하다(Raspberry Pi 커뮤니티의 오래된 문제).
- **진동·접촉**: 소켓 접점이 로봇 진동에 약하다. 카드가 순간적으로 빠졌다 들어오면 card detect가 제거 이벤트를 낸다.
- **부품 변경**: 같은 상품명이라도 내부 컨트롤러와 NAND가 수시로 바뀐다. 검증 결과가 다음 로트에 적용되지 않는다.
- 쓸 수 있는 경우: 개발용, 교체 가능한 데이터 매체(녹화 카드), 그리고 **산업용 SD**(pSLC 모드, 넓은 온도, 일부는 전원 차단 보호 회로, 고정 BOM, 건강 정보 조회 벤더 명령) — 가격이 소비자용의 몇 배다.

DT에서는 card detect를 이렇게 표현한다(설명용 조각, 이 노트에서 실행하지 않음):

```dts
&usdhc2 {
	bus-width = <4>;
	cd-gpios = <&gpio2 12 GPIO_ACTIVE_LOW>;	/* card detect pin */
	wp-gpios = <&gpio2 13 GPIO_ACTIVE_HIGH>;	/* write protect switch (full-size SD) */
	/* or: broken-cd;  (poll)   non-removable;  (soldered eMMC/SD NAND) */
	status = "okay";
};
```

### 4.3 예제 5 — 같은 스크립트로 SD 카드를 붙이면 무엇이 달라지나

무엇을 확인하나: 같은 `g_mmc.sh`를 `-device sd-card`로 돌려 eMMC와 비교한다.

```sh
QEXTRA="-device sdhci-pci -drive if=none,id=s0,file=/var/tmp/J1/j1_sd.img,format=raw -device sd-card,drive=s0" \
  TMO=150 bash ~/.elinux_scratch/J1/boot.sh g_mmc.sh
```

```text
lab initramfs: 6.18.55-fs up 1.27s
== dmesg
mmc0: SDHCI controller on PCI [0000:00:02.0] using ADMA
mmc0: new high speed SD card at address f31f
mmcblk0: mmc0:f31f QEMU! 64.0 MiB
== /dev
brw------- 179, 0 /dev/mmcblk0
== sysfs /sys/block/mmcblk0/device (type=SD)
name                   QEMU!
manfid                 0x0000aa
oemid                  0x5859
date                   02/2006
fwrev                  0x1
hwrev                  0x0
serial                 0xdeadbeef
cid                    aa585951454d552101deadbeef006200
csd                    002600325f59e03fffffdfff92600000
erase_size             512
preferred_erase_size   524288
ocr                    0x00300000
== debugfs ios
clock:		50000000 Hz
actual clock:	26000000 Hz
vdd:		21 (3.3 ~ 3.4 V)
bus mode:	2 (push-pull)
chip select:	0 (don't care)
power mode:	2 (on)
bus width:	2 (4 bits)
timing spec:	2 (sd high-speed)
signal voltage:	0 (3.30 V)
driver type:	0 (driver type B)
== block layer view
mmcblk0  size=64 MiB lbs=512 pbs=512
         rotational=0 write_cache=write through fua=0 scheduler=none [mq-deadline] kyber bfq 
         discard_granularity=524288 discard_max_hw_bytes=524288 write_zeroes_max_bytes=0
  BLKDISCARD    1024 KiB @33554432: OK
  BLKSECDISCARD 1024 KiB @33554432: Operation not supported
  BLKZEROOUT    1024 KiB @33554432: OK
```

출력에서 볼 것:

- 노드가 `mmcblk0` **하나뿐**이다 — boot0/boot1/rpmb가 없다. 그래서 SD 부팅 SoC는 user area의 raw 오프셋(예: 32 KiB)에 부트로더를 쓴다(C2 §7).
- sysfs에 `pre_eol_info`, `life_time`, `rel_sectors`가 **없다** — SD 표준에는 eMMC 같은 수명 레지스터가 없다(산업용 카드는 벤더 전용 명령으로 제공한다). 필드에서 SD의 마모를 볼 표준 방법이 없다는 뜻이다.
- `write_cache=write through fua=0`: SD는 reliable write가 없고 cache 확장(SD 6.0의 performance enhancement)도 꺼져 있다. 커널은 이 카드에 flush를 보낼 필요가 없다고 판단한다. "flush가 없으니 안전하다"가 아니라 **"카드가 내부에서 무엇을 하든 호스트가 알 길이 없다"** 로 읽어야 한다.
- `erase_size 512` vs `preferred_erase_size 524288`: SD는 섹터 단위 erase를 받지만 AU(allocation unit) 크기 단위가 효율적이라고 알려 준다.
- `actual clock: 26000000 Hz`: 50 MHz를 요청했지만 SDHCI 분주기로 26 MHz가 됐다(QEMU 값). 실제 보드에서 SDR104(208 MHz)가 안 올라가면 1.8 V 전환(vqmmc)과 튜닝부터 본다.

---

## 5. UFS — 휴대폰에서 온 SCSI 저장장치

### 5.1 정의

UFS(JEDEC Universal Flash Storage)는 eMMC의 후계자다. 물리층은 MIPI **M-PHY**(차동 직렬, 레인 1~2개), 링크층은 MIPI **UniPro**, 그 위 전송 단위가 **UPIU**(UFS Protocol Information Unit)이고, 명령 세트는 **SCSI**다. 그래서 Linux에서 UFS는 SCSI 디스크(`/dev/sda`)로 보인다.

| 개념 | 뜻 | eMMC 대응 |
|---|---|---|
| LU (logical unit) | 최대 8개(또는 32개) 독립 LBA 공간 | user area · GP 파티션 |
| Boot LU A/B | `bBootLunEn`으로 고르는 부트 LU | boot0/boot1 + `PARTITION_CONFIG` |
| W-LUN | 잘 알려진 특수 LU: REPORT LUNS(0x81), UFS Device(0xD0), Boot(0xB0), RPMB(0xC4) | — |
| 프로비저닝 | Configuration Descriptor로 LU 크기·종류(enhanced 등)를 정하고 잠근다 | enhanced area · GP 설정 (역시 사실상 한 번) |
| WriteBooster | UFS 3.1+: SLC 쓰기 버퍼(LU 전용 또는 공유) | 없음 (SSD의 SLC cache와 같은 생각) |
| Health Descriptor | `bPreEOLInfo`, `bDeviceLifeTimeEstA/B` | `PRE_EOL_INFO`, `LIFE_TIME_EST_A/B` (같은 의미) |
| 큐 | 깊이 32 태그, UFS 4.0 MCQ(다중 큐) | CQE 깊이 32 |

UFS 4.0은 2레인 기준 순차 읽기 4 GB/s 안팎으로 NVMe 영역에 들어왔다(기억 기반). 임베디드에서는 자동차 SoC(Qualcomm, Renesas R-Car 등)와 고성능 모듈에 쓰인다.

### 5.2 예제 6 — QEMU UFS로 진짜 ufshcd 스택 띄우기

무엇을 확인하나: UFS 호스트 컨트롤러(UFSHCI) → `ufshcd` → SCSI mid-layer → `sd`로 이어지는 경로, W-LUN, 그리고 health/geometry descriptor와 WriteBooster sysfs.

```sh
#!/bin/sh
# QEMU UFS host controller (PCI) + 2 logical units -> ufshcd -> SCSI -> sd
cd /j1; dmesg -n 1
insmod j1_ufshcd_pci.ko; sleep 4
echo "== dmesg"; dmesg | grep -iE "ufs|scsi|sd [0-9]" | sed 's/^\[[ 0-9.]*\] //'
echo "== scsi devices"; for s in /sys/class/scsi_device/*; do echo "$(basename $s) $(cat $s/device/vendor) $(cat $s/device/model) -> $(ls $s/device/block 2>/dev/null || echo '(no block dev)')"; done
for d in sda sdb; do echo "$d -> $(readlink /sys/block/$d | sed 's#^.*/devices/##')"; done
./j1_sg /dev/sda; ./j1_sg /dev/sda 1; ./j1_sg /dev/sda 2
U=$(dirname $(readlink -f /sys/class/scsi_host/host0/device))
echo "== ufshcd sysfs at $U"; ls $U | tr '\n' ' '; echo
for g in device_descriptor health_descriptor geometry_descriptor; do echo "-- $g"; for f in $U/$g/*; do printf "%-36s %s\n" $(basename $f) "$(cat $f 2>&1)"; done | head -14; done
echo "-- flags/attributes"; for f in $U/flags/wb_enable $U/flags/wb_flush_en $U/attributes/bkops_status $U/attributes/active_icc_level; do printf "%-36s %s\n" $(basename $f) "$(cat $f 2>&1)"; done
echo "-- wb_on"; cat $U/wb_on 2>&1
./j1_blkinfo sda discard
```

```sh
QEXTRA="-device ufs,serial=j1ufs01,id=ufs0 \
 -drive if=none,id=u0,file=/var/tmp/J1/j1_ufs0.img,format=raw -device ufs-lu,drive=u0,bus=ufs0,lun=0 \
 -drive if=none,id=u1,file=/var/tmp/J1/j1_ufs1.img,format=raw -device ufs-lu,drive=u1,bus=ufs0,lun=1" \
 TMO=200 bash ~/.elinux_scratch/J1/boot.sh g_ufs.sh
```

```text
lab initramfs: 6.18.55-fs up 1.71s
== dmesg
SCSI subsystem initialized
Block layer SCSI generic (bsg) driver version 0.4 loaded (major 244)
j1_ufshcd_pci: loading out-of-tree module taints kernel.
ufshcd 0000:00:02.0: enabling device (0000 -> 0002)
ufshcd 0000:00:02.0: Undefined ref clk gating wait time, use default 255us
ufshcd 0000:00:02.0: dme-get: attr-id 0x1581 failed 0 retries
ufshcd 0000:00:02.0: dme-get: attr-id 0x1561 failed 0 retries
ufshcd 0000:00:02.0: ufshcd_get_max_pwr_mode: invalid connected lanes value. rx=0, tx=0
ufshcd 0000:00:02.0: ufshcd_device_params_init: Failed getting max supported power mode
ufshcd 0000:00:02.0: ufshcd_set_timestamp_attr: failed to set timestamp 1
scsi host0: ufshcd
scsi 0:0:0:49488: Well-known LUN    QEMU     QEMU UFS              PQ: 0 ANSI: 6
scsi 0:0:0:49476: Well-known LUN    QEMU     QEMU UFS              PQ: 0 ANSI: 6
scsi 0:0:0:49456: Well-known LUN    QEMU     QEMU UFS              PQ: 0 ANSI: 6
scsi 0:0:0:0: Direct-Access     QEMU     QEMU HARDDISK    2.5+ PQ: 0 ANSI: 5
scsi 0:0:0:1: Direct-Access     QEMU     QEMU HARDDISK    2.5+ PQ: 0 ANSI: 5
sd 0:0:0:0: [sda] 16384 4096-byte logical blocks: (67.1 MB/64.0 MiB)
sd 0:0:0:0: [sda] Write Protect is off
sd 0:0:0:0: [sda] Mode Sense: 00 5e 00 10
sd 0:0:0:0: [sda] Write cache: enabled, read cache: enabled, supports DPO and FUA
sd 0:0:0:1: [sdb] 8192 4096-byte logical blocks: (33.6 MB/32.0 MiB)
sd 0:0:0:1: [sdb] Write Protect is off
sd 0:0:0:1: [sdb] Mode Sense: 00 5e 00 10
sd 0:0:0:1: [sdb] Write cache: enabled, read cache: enabled, supports DPO and FUA
sd 0:0:0:0: [sda] Attached SCSI disk
sd 0:0:0:1: [sdb] Attached SCSI disk
== scsi devices
0:0:0:0 QEMU     QEMU HARDDISK    -> sda
0:0:0:1 QEMU     QEMU HARDDISK    -> sdb
0:0:0:49456 QEMU     QEMU UFS         -> (no block dev)
0:0:0:49476 QEMU     QEMU UFS         -> (no block dev)
0:0:0:49488 QEMU     QEMU UFS         -> (no block dev)
sda -> platform/4010000000.pcie/pci0000:00/0000:00:02.0/host0/target0:0:0/0:0:0:0/block/sda
sdb -> platform/4010000000.pcie/pci0000:00/0000:00:02.0/host0/target0:0:0/0:0:0:1/block/sdb
/dev/sda INQUIRY: type=0x00 vendor='QEMU    ' product='QEMU HARDDISK   ' rev='2.5+' version=0x05
  READ CAPACITY(10): last LBA=16383 block=4096 -> 64 MiB
  REPORT LUNS (select 0): 2 LUN(s): 0x0000 0x0001
/dev/sda INQUIRY: type=0x00 vendor='QEMU    ' product='QEMU HARDDISK   ' rev='2.5+' version=0x05
  READ CAPACITY(10): last LBA=16383 block=4096 -> 64 MiB
  scsi status=0x2 sense key=0x5
  REPORT LUNS (select 1): 0 LUN(s):
/dev/sda INQUIRY: type=0x00 vendor='QEMU    ' product='QEMU HARDDISK   ' rev='2.5+' version=0x05
  READ CAPACITY(10): last LBA=16383 block=4096 -> 64 MiB
  scsi status=0x2 sense key=0x5
  REPORT LUNS (select 2): 0 LUN(s):
== ufshcd sysfs at /sys/devices/platform/4010000000.pcie/pci0000:00/0000:00:02.0
aer ari_enabled attributes auto_hibern8 broken_parity_status capabilities class config consistent_dma_mask_bits critical_health d3cold_allowed device device_descriptor device_lvl_exception_count device_lvl_exception_id devspec dma_mask_bits driver driver_override enable enable_wb_buf_flush flags geometry_descriptor health_descriptor hid host0 interconnect_descriptor irq link local_cpulist local_cpus modalias monitor msi_bus numa_node pm_qos_enable power power_descriptor power_info power_state remove rescan resource resource0 revision rpm_lvl rpm_target_dev_state rpm_target_link_state rtc_update_ms spm_lvl spm_target_dev_state spm_target_link_state string_descriptors subsystem subsystem_device subsystem_vendor uevent ufshci_capabilities vendor wb_flush_threshold wb_on wb_resize_enable 
-- device_descriptor
bkops_termination_latency            0x00
boot_enable                          0x00
descriptor_access_enable             0x00
device_class                         0x00
device_sub_class                     0x01
device_type                          0x00
device_version                       0x0000
ext_feature_sup                      0x00000030
ffu_timeout                          0x00
high_priority_lun                    0x7F
initial_active_icc_level             0x00
initial_power_mode                   0x01
manufacturer_id                      0x0000
manufacturing_date                   0x0000
-- health_descriptor
eol_info                             0x00
life_time_estimation_a               0x00
life_time_estimation_b               0x00
-- geometry_descriptor
allocation_unit_size                 0x01
data_ordering                        0x00
dyn_capacity_resource_policy         0x00
enh1_memory_capacity_adjustment_factor 0x0000
enh1_memory_max_alloc_units          0x00000000
enh2_memory_capacity_adjustment_factor 0x0000
enh2_memory_max_alloc_units          0x00000000
enh3_memory_capacity_adjustment_factor 0x0000
enh3_memory_max_alloc_units          0x00000000
enh4_memory_capacity_adjustment_factor 0x0000
enh4_memory_max_alloc_units          0x00000000
max_in_buffer_size                   0x08
max_number_of_contexts               0x05
max_number_of_luns                   0x01
-- flags/attributes
wb_enable                            false
wb_flush_en                          false
bkops_status                         0x00000000
active_icc_level                     0x00000000
-- wb_on
0
sda      size=64 MiB lbs=4096 pbs=4096
         rotational=1 write_cache=write back fua=1 scheduler=none [mq-deadline] kyber bfq 
         discard_granularity=4096 discard_max_hw_bytes=34359734272 write_zeroes_max_bytes=0
  BLKDISCARD    1024 KiB @33554432: OK
  BLKSECDISCARD 1024 KiB @33554432: Operation not supported
  BLKZEROOUT    1024 KiB @33554432: OK
```

(sysfs 디렉터리 목록 한 줄은 길어서 잘리지 않고 그대로 두었다. 각 descriptor는 스크립트에서 `head -14`로 앞부분만 찍었다.)

출력에서 볼 것:

- `scsi 0:0:0:49488: Well-known LUN`: 49488 = 0xC150. UFS W-LUN 0xD0(UFS Device)을 SCSI LUN 주소 방식으로 바꾼 값(0xC100 + (0xD0 & 0x7F))이다. 0xC144 = RPMB(0xC4), 0xC130 = Boot(0xB0). 커널 `ufshcd_scsi_add_wlus()`가 이 세 개를 직접 추가한다 — REPORT LUNS로 찾는 것이 아니다. 그래서 일반 LU에 select report 1/2를 보내자 `ILLEGAL REQUEST`(sense key 0x5)가 났다.
- 일반 LU 두 개가 `sda`, `sdb`가 됐다. `/sys/block/sda` 경로가 `.../0000:00:02.0/host0/target0:0:0/0:0:0:0`이다 — PCI 장치 밑에 **SCSI host·target·LUN** 계층이 그대로 있다. NVMe(`.../nvme/nvme0/nvme0n1`)나 eMMC(`.../mmc_host/mmc0/mmc0:0001`)와 비교해 보면 "UFS = SCSI"가 경로로 드러난다.
- `4096-byte logical blocks`: UFS는 4 KiB 논리 블록이 표준이다(eMMC는 512 B). 4 KiB 미만 정렬 쓰기가 원천적으로 없어진다.
- `dme-get ... failed`, `invalid connected lanes`: QEMU가 UniPro/M-PHY 속성(DME)을 흉내 내지 않아서 나는 경고다. 실제 보드에서 같은 줄이 나오면 PHY 초기화·레인 연결 실패이므로 **심각한 bring-up 문제**다.
- `health_descriptor`의 `eol_info`, `life_time_estimation_a/b`: eMMC의 `PRE_EOL_INFO`, `LIFE_TIME_EST_A/B`와 같은 의미의 값이다. 같은 모니터링 코드를 재사용할 수 있다.
- `wb_enable false`, `wb_on 0`: WriteBooster 꺼짐. 장치가 지원하면 `echo 1 > wb_on`으로 켤 수 있다(QEMU는 미지원).
- 실제 장비에서 LU 구성 변경·descriptor 쓰기는 bsg 노드(`/dev/bsg/ufs-bsg0`)와 `ufs-utils` 같은 도구로 한다(이 VM에 없음, 실행하지 않음).

### 5.3 예제 7 — UFS가 없을 때의 대용품: scsi_debug와 raw SCSI 명령

무엇을 확인하나: UFS든 USB 스틱이든 SATA든 Linux `sd` 드라이버가 보는 것은 SCSI 명령이다. `scsi_debug`(RAM 기반 가상 SCSI 호스트)로 같은 경로를 만들고, `SG_IO` ioctl로 INQUIRY·READ CAPACITY·REPORT LUNS를 직접 보낸다(`sg3_utils`의 `sg_inq`, `sg_readcap`, `sg_luns`가 하는 일).

```c
/* j1_sg.c - raw SCSI INQUIRY + READ CAPACITY(10) + REPORT LUNS via SG_IO (what ufshcd/sd speak) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>

static int sgio(int fd, uint8_t *cdb, int cdblen, uint8_t *d, int len)
{
    uint8_t sense[32];
    sg_io_hdr_t h = { .interface_id = 'S', .dxfer_direction = SG_DXFER_FROM_DEV, .cmd_len = cdblen,
                      .mx_sb_len = sizeof sense, .dxfer_len = len, .dxferp = d, .cmdp = cdb,
                      .sbp = sense, .timeout = 5000 };
    if (ioctl(fd, SG_IO, &h) < 0) { perror("SG_IO"); exit(1); }
    if (h.status) printf("  scsi status=0x%x sense key=0x%x\n", h.status, sense[2] & 0xf);
    return h.status;
}
int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s /dev/sdX [select_report]\n", argv[0]); return 1; }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { perror("open"); exit(1); }
    uint8_t d[512];
    uint8_t inq[6] = { 0x12, 0, 0, 0, 96, 0 };
    memset(d, 0, sizeof d);
    sgio(fd, inq, 6, d, 96);
    printf("%s INQUIRY: type=0x%02x vendor='%.8s' product='%.16s' rev='%.4s' version=0x%02x\n",
           argv[1], d[0] & 0x1f, d + 8, d + 16, d + 32, d[2]);
    uint8_t rc[10] = { 0x25 };
    sgio(fd, rc, 10, d, 8);
    uint32_t last = (uint32_t)d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3];
    uint32_t bl = (uint32_t)d[4] << 24 | d[5] << 16 | d[6] << 8 | d[7];
    printf("  READ CAPACITY(10): last LBA=%u block=%u -> %u MiB\n", last, bl,
           (unsigned)(((uint64_t)last + 1) * bl >> 20));
    uint8_t rl[12] = { 0xa0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };   /* alloc len 256 */
    if (argc > 2) rl[2] = (uint8_t)strtoul(argv[2], NULL, 0);    /* 0 normal, 1 well-known, 2 all */
    memset(d, 0, sizeof d);
    sgio(fd, rl, 12, d, 256);
    uint32_t n = ((uint32_t)d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3]) / 8;
    printf("  REPORT LUNS (select %u): %u LUN(s):", rl[2], n);
    for (uint32_t i = 0; i < n && i < 30; i++) printf(" 0x%04x", d[8 + 8 * i] << 8 | d[9 + 8 * i]);
    printf("\n");
    close(fd);
    return 0;
}
```

```sh
#!/bin/sh
cd /j1; dmesg -n 1
modprobe scsi_debug dev_size_mb=64 max_luns=2 num_tgts=1; sleep 2
for s in /sys/class/scsi_device/*; do echo "$(basename $s) $(cat $s/device/vendor) $(cat $s/device/model) -> $(ls $s/device/block)"; done
for d in sda sdb; do echo "$d -> $(readlink /sys/block/$d | sed 's#^.*/devices/##')"; done
./j1_sg /dev/sda; ./j1_sg /dev/sdb 2
```

```text
lab initramfs: 6.18.55-fs up 1.51s
0:0:0:0 Linux    scsi_debug       -> sda
0:0:0:1 Linux    scsi_debug       -> sdb
sda -> pseudo_0/adapter0/host0/target0:0:0/0:0:0:0/block/sda
sdb -> pseudo_0/adapter0/host0/target0:0:0/0:0:0:1/block/sdb
/dev/sda INQUIRY: type=0x00 vendor='Linux   ' product='scsi_debug      ' rev='0191' version=0x07
  READ CAPACITY(10): last LBA=131071 block=512 -> 64 MiB
  REPORT LUNS (select 0): 2 LUN(s): 0x0000 0x0001
/dev/sdb INQUIRY: type=0x00 vendor='Linux   ' product='scsi_debug      ' rev='0191' version=0x07
  READ CAPACITY(10): last LBA=131071 block=512 -> 64 MiB
  REPORT LUNS (select 2): 3 LUN(s): 0x0000 0x0001 0xc101
```

출력에서 볼 것: `scsi_debug`는 가상 버스(`pseudo_0/adapter0`) 밑에 붙는다. 경로만 다르고 `sda` 위의 스택은 UFS와 같다. select report 2(모든 LU)에서 W-LUN `0xc101`(REPORT LUNS W-LUN)이 함께 보고된다. UFS 드라이버를 짜거나 디버그할 때 `SG_IO`로 원하는 CDB를 보내는 이 패턴이 그대로 쓰인다.

---

## 6. NVMe — Don의 세계가 임베디드로 들어올 때

### 6.1 어디서 만나나

Jetson Orin Nano/NX 개발 키트의 M.2 슬롯, x86 기반 로봇 컴퓨터, 고해상도 카메라·LiDAR를 녹화하는 데이터 로거. 원시 센서 데이터(예: 카메라 4대 + LiDAR로 수백 MB/s)를 몇 시간씩 기록해야 하면 eMMC로는 대역폭도 용량도 TBW도 부족하다. 이때 NVMe SSD가 들어온다. (Jetson AGX Orin 모듈은 64 GB eMMC를 내장하고 NVMe를 추가로 붙이는 구성이 흔하다 — 기억 기반.)

Don에게 새로운 것은 프로토콜이 아니라 **호스트 쪽에서 보는 시점**이다.

- **namespace**: 한 컨트롤러 안의 독립 LBA 공간. Linux에서 `/dev/nvme0n1`, `/dev/nvme0n2`. 컨트롤러 자체는 문자 장치 `/dev/nvme0`(admin 명령용).
- **SMART / Health 로그(Log Page 02h)**: Critical Warning, Composite Temperature, Available Spare, **Percentage Used**, Data Units Read/Written(1000 x 512 B 단위), Power Cycles, Power On Hours, **Unsafe Shutdowns**, Media Errors.
- **PLP**: 엔터프라이즈 SSD는 커패시터로 휘발성 캐시를 flush한다. 클라이언트(M.2) SSD는 보통 PLP가 없다 — 로봇에 넣는 M.2 SSD는 대부분 이쪽이다. `VWC`(Volatile Write Cache) 필드가 1이면 호스트는 Flush를 보내야 한다. Critical Warning bit4("volatile memory backup device failed")는 PLP 회로 고장 보고다.

### 6.2 예제 8 — Identify와 SMART를 admin passthrough로 읽기

무엇을 확인하나: `nvme-cli`의 `nvme id-ctrl`, `nvme list-ns`, `nvme id-ns`, `nvme smart-log`가 하는 일 — `NVME_IOCTL_ADMIN_CMD`로 Identify(06h, CNS 1/2/0)와 Get Log Page(02h)를 보낸다. 그리고 32 MiB를 쓴 뒤 SMART 카운터가 어떻게 변하는지.

```c
/* j1_nvme.c - NVMe Identify (controller, ns list, namespace) + SMART/Health log via admin passthrough */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/nvme_ioctl.h>

static uint8_t buf[4096] __attribute__((aligned(4096)));
static unsigned long long le(const uint8_t *p, int n) { unsigned long long v = 0; while (n--) v = v << 8 | p[n]; return v; }
static void admin(int fd, uint8_t op, uint32_t nsid, uint32_t cdw10, uint32_t len)
{
    struct nvme_admin_cmd c = { .opcode = op, .nsid = nsid, .addr = (uintptr_t)buf,
                                .data_len = len, .cdw10 = cdw10 };
    memset(buf, 0, sizeof buf);
    int r = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &c);
    if (r != 0) { fprintf(stderr, "admin op 0x%02x: ioctl=%d status=0x%x\n", op, r, r); exit(1); }
}
int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s /dev/nvme0 [smart-only]\n", argv[0]); return 1; }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { perror("open"); exit(1); }
    if (argc < 3) {
        admin(fd, 0x06, 0, 1, 4096);                        /* Identify Controller (CNS=1) */
        printf("ctrl: VID=0x%04llx SN=%.20s MN=%.40s FR=%.8s\n", le(buf, 2), buf + 4, buf + 24, buf + 64);
        printf("      VER=0x%08llx MDTS=%u ONCS=0x%04llx VWC=0x%02x NN=%llu WCTEMP=%lluK CCTEMP=%lluK\n",
               le(buf + 80, 4), buf[77], le(buf + 520, 2), buf[525], le(buf + 516, 4),
               le(buf + 266, 2), le(buf + 268, 2));
        admin(fd, 0x06, 0, 2, 4096);                        /* Active Namespace ID list (CNS=2) */
        uint32_t ids[8]; int n = 0;
        for (int i = 0; i < 8 && le(buf + 4 * i, 4); i++) ids[n++] = (uint32_t)le(buf + 4 * i, 4);
        for (int i = 0; i < n; i++) {
            admin(fd, 0x06, ids[i], 0, 4096);               /* Identify Namespace (CNS=0) */
            uint8_t f = buf[26] & 0xf;
            printf("ns %u: NSZE=%llu blocks NUSE=%llu NLBAF=%u FLBAS=%u -> LBA %u B (meta %llu B)\n", ids[i],
                   le(buf, 8), le(buf + 16, 8), buf[25] + 1, f, 1u << buf[128 + 4 * f + 2],
                   le(buf + 128 + 4 * f, 2));
        }
    }
    admin(fd, 0x02, 0xffffffff, (127u << 16) | 0x02, 512); /* Get Log Page 02h SMART, 512 B */
    printf("smart: crit_warn=0x%02x temp=%lluK avail_spare=%u%% thresh=%u%% pct_used=%u%%\n",
           buf[0], le(buf + 1, 2), buf[3], buf[4], buf[5]);
    printf("       data_units_read=%llu written=%llu (x 512000 B) host_rd_cmds=%llu host_wr_cmds=%llu\n",
           le(buf + 32, 8), le(buf + 48, 8), le(buf + 64, 8), le(buf + 80, 8));
    printf("       power_cycles=%llu poh=%llu unsafe_shutdowns=%llu media_errs=%llu err_log=%llu\n",
           le(buf + 112, 8), le(buf + 128, 8), le(buf + 144, 8), le(buf + 160, 8), le(buf + 176, 8));
    close(fd);
    return 0;
}
```

이 예제부터 예제 10·11은 **한 게스트에 네 가지 블록 스택**(virtio-blk, NVMe 2 namespace, eMMC, scsi_debug)을 동시에 붙인 부팅에서 나왔다.

```sh
#!/bin/sh
# one guest, four block stacks: virtio-blk, NVMe (2 namespaces), eMMC (sdhci-pci), SCSI (scsi_debug)
cd /j1; dmesg -n 1
insmod j1_sdhci_uhs2.ko && insmod j1_sdhci_pci.ko
modprobe nvme; modprobe scsi_debug dev_size_mb=64 max_luns=2 num_tgts=1; sleep 3
echo "== dmesg"; dmesg | grep -E "nvme|scsi|sd [0-9]|virtio_blk|mmcblk0:" | sed 's/^\[[ 0-9.]*\] //'
echo "== /sys/block -> device path"
for b in /sys/block/*; do n=$(basename $b); case $n in vd*|nvme*|sd*|mmcblk*) echo "$n -> $(readlink $b | sed 's#^.*/devices/##')";; esac; done
echo "== block layer view"; for d in vda nvme0n1 sda mmcblk0; do ./j1_blkinfo $d; done
./j1_blkinfo nvme0n1 discard | tail -3
echo "== nvme sysfs"; for a in model serial firmware_rev transport state; do printf "%-13s %s\n" $a "$(cat /sys/class/nvme/nvme0/$a)"; done
echo "== j1_nvme"; ./j1_nvme /dev/nvme0
dd if=/dev/zero of=/dev/nvme0n1 bs=1M count=32 conv=fsync 2>&1 | tail -1
echo "== SMART after 32 MiB write"; ./j1_nvme /dev/nvme0 smart
echo "== scsi"; for s in /sys/class/scsi_device/*; do echo "$(basename $s) $(cat $s/device/vendor) $(cat $s/device/model) -> $(ls $s/device/block)"; done
./j1_sg /dev/sda; ./j1_sg /dev/sdb
echo "== bench (QD1, O_DIRECT, QEMU TCG)"
for d in vda nvme0n1 mmcblk0 sda; do
  ./j1_bench /dev/$d rr 4 2000 $d; ./j1_bench /dev/$d rw 4 2000 $d
  ./j1_bench /dev/$d sr 1024 48 $d; ./j1_bench /dev/$d sw 1024 48 $d
done
```

```sh
cd /var/tmp/J1 && truncate -s 128M j1_vda.img j1_ns2.img && truncate -s 256M j1_ns1.img
truncate -s $((128*1024*1024+8*1024*1024+128*1024)) j1_emmc.img
QEXTRA="-drive if=none,id=v0,file=/var/tmp/J1/j1_vda.img,format=raw -device virtio-blk-device,drive=v0 \
 -device nvme,serial=j1nvme01,id=nv0 \
 -drive if=none,id=n1,file=/var/tmp/J1/j1_ns1.img,format=raw -device nvme-ns,drive=n1,bus=nv0,nsid=1 \
 -drive if=none,id=n2,file=/var/tmp/J1/j1_ns2.img,format=raw -device nvme-ns,drive=n2,bus=nv0,nsid=2 \
 -device sdhci-pci -drive if=none,id=e0,file=/var/tmp/J1/j1_emmc.img,format=raw \
 -device emmc,drive=e0,boot-partition-size=4194304,rpmb-partition-size=131072" \
 TMO=500 bash ~/.elinux_scratch/J1/boot.sh g_all.sh
```

(이 부팅의 `== scsi` 부분 출력은 `REPORT LUNS` 형식을 바꾸기 전의 `j1_sg`로 찍혔으므로 싣지 않았다 — 같은 내용을 예제 7에서 현재 도구로 다시 실행했다.)

dmesg와 장치 경로:

```text
lab initramfs: 6.18.55-fs up 1.46s
== dmesg
virtio_blk virtio0: 1/0/0 default/read/poll queues
virtio_blk virtio0: [vda] 262144 512-byte logical blocks (134 MB/128 MiB)
nvme nvme0: pci function 0000:00:02.0
nvme 0000:00:02.0: enabling device (0000 -> 0002)
nvme nvme0: 2/0/0 default/read/poll queues
nvme nvme0: Ignoring bogus Namespace Identifiers
nvme nvme0: Ignoring bogus Namespace Identifiers
mmcblk0: mmc0:0001 QEMU!! 128 MiB
scsi_debug:sdebug_driver_probe: scsi_debug: trim poll_queues to 0. poll_q/nr_hw = (0/1)
scsi host0: scsi_debug: version 0191 [20210520]
scsi 0:0:0:0: Direct-Access     Linux    scsi_debug       0191 PQ: 0 ANSI: 7
scsi 0:0:0:0: Power-on or device reset occurred
scsi 0:0:0:1: Direct-Access     Linux    scsi_debug       0191 PQ: 0 ANSI: 7
scsi 0:0:0:1: Power-on or device reset occurred
sd 0:0:0:0: [sda] 131072 512-byte logical blocks: (67.1 MB/64.0 MiB)
sd 0:0:0:0: [sda] Write Protect is off
sd 0:0:0:0: [sda] Mode Sense: 73 00 10 08
sd 0:0:0:0: [sda] Write cache: enabled, read cache: enabled, supports DPO and FUA
sd 0:0:0:1: [sdb] 131072 512-byte logical blocks: (67.1 MB/64.0 MiB)
sd 0:0:0:1: [sdb] Write Protect is off
sd 0:0:0:1: [sdb] Mode Sense: 73 00 10 08
sd 0:0:0:1: [sdb] Write cache: enabled, read cache: enabled, supports DPO and FUA
sd 0:0:0:0: [sda] permanent stream count = 5
sd 0:0:0:0: [sda] Preferred minimum I/O size 512 bytes
sd 0:0:0:0: [sda] Optimal transfer size 524288 bytes
sd 0:0:0:1: [sdb] permanent stream count = 5
sd 0:0:0:1: [sdb] Preferred minimum I/O size 512 bytes
sd 0:0:0:1: [sdb] Optimal transfer size 524288 bytes
sd 0:0:0:0: [sda] Attached SCSI disk
sd 0:0:0:1: [sdb] Attached SCSI disk
== /sys/block -> device path
mmcblk0 -> platform/4010000000.pcie/pci0000:00/0000:00:03.0/mmc_host/mmc0/mmc0:0001/block/mmcblk0
mmcblk0boot0 -> platform/4010000000.pcie/pci0000:00/0000:00:03.0/mmc_host/mmc0/mmc0:0001/block/mmcblk0/mmcblk0boot0
mmcblk0boot1 -> platform/4010000000.pcie/pci0000:00/0000:00:03.0/mmc_host/mmc0/mmc0:0001/block/mmcblk0/mmcblk0boot1
nvme0n1 -> platform/4010000000.pcie/pci0000:00/0000:00:02.0/nvme/nvme0/nvme0n1
nvme0n2 -> platform/4010000000.pcie/pci0000:00/0000:00:02.0/nvme/nvme0/nvme0n2
sda -> pseudo_0/adapter0/host0/target0:0:0/0:0:0:0/block/sda
sdb -> pseudo_0/adapter0/host0/target0:0:0/0:0:0:1/block/sdb
vda -> platform/a003e00.virtio_mmio/virtio0/block/vda
```

NVMe 부분:

```text
== nvme sysfs
model         QEMU NVMe Ctrl                          
serial        j1nvme01            
firmware_rev  10.2.1  
transport     pcie
state         live
== j1_nvme
ctrl: VID=0x1b36 SN=j1nvme01             MN=QEMU NVMe Ctrl                           FR=10.2.1  
      VER=0x00010400 MDTS=7 ONCS=0x075d VWC=0x07 NN=256 WCTEMP=343K CCTEMP=373K
ns 1: NSZE=524288 blocks NUSE=524288 NLBAF=8 FLBAS=0 -> LBA 512 B (meta 0 B)
ns 2: NSZE=262144 blocks NUSE=262144 NLBAF=8 FLBAS=0 -> LBA 512 B (meta 0 B)
smart: crit_warn=0x00 temp=323K avail_spare=0% thresh=0% pct_used=0%
       data_units_read=1 written=0 (x 512000 B) host_rd_cmds=2 host_wr_cmds=0
       power_cycles=0 poh=0 unsafe_shutdowns=0 media_errs=0 err_log=0
33554432 bytes (32.0MB) copied, 0.119855 seconds, 267.0MB/s
== SMART after 32 MiB write
smart: crit_warn=0x00 temp=323K avail_spare=0% thresh=0% pct_used=0%
       data_units_read=1 written=66 (x 512000 B) host_rd_cmds=2 host_wr_cmds=64
       power_cycles=0 poh=0 unsafe_shutdowns=0 media_errs=0 err_log=0
```

출력에서 볼 것:

- `2/0/0 default/read/poll queues`: `-smp 2`라 I/O 큐 쌍이 2개. hctx가 CPU마다 하나씩 생기는 것은 I11 예제 15에서 봤다. `nvme0n1`, `nvme0n2` — namespace가 각각 블록 장치가 됐다. 경로가 `.../nvme/nvme0/nvme0n1`로 컨트롤러 밑에 있다.
- `Ignoring bogus Namespace Identifiers`: QEMU가 NGUID/EUI64를 0으로 보고해서 커널이 무시한 것. 실제 SSD에서 이 메시지가 나오면 펌웨어 버그로 multipath·udev 이름이 꼬일 수 있다.
- `VER=0x00010400` = NVMe 1.4. `MDTS=7` → 최대 전송 2^7 x 4 KiB(CAP.MPSMIN) = 512 KiB. 그래서 1 MiB `dd` 한 번이 명령 2개가 되고, 32 MiB에 **`host_wr_cmds=64`**.
- `written=66`: 32 MiB = 33,554,432 B ÷ 512,000 B = 65.5 → 올림 66. Don이 펌웨어에서 구현했던 바로 그 "1000 x 512 B 단위, 올림" 규칙이 호스트에서 그대로 보인다.
- `VWC=0x07`: bit0 = 휘발성 쓰기 캐시 있음 → 커널이 `write_cache=write back`으로 설정하고 Flush를 보낸다(예제 10).
- `avail_spare=0% thresh=0%`: QEMU는 spare를 모델링하지 않는다. 실제 SSD라면 100%에서 시작해 내려간다. 모니터링 코드는 "0%면 즉시 경보"로 짜면 QEMU와 일부 저가 SSD에서 오탐이 난다 — `thresh`와 함께 판단해야 한다.
- SMART 카운터는 **QEMU 프로세스가 재시작하면 0으로 돌아간다**(이미지 파일에 저장되지 않음). 실제 SSD는 비휘발이다.

### 6.3 예제 9 — Critical Warning 주입

무엇을 확인하나: 모니터링 코드가 경보 비트를 제대로 읽는지 하드웨어 고장 없이 시험한다. QEMU nvme의 `smart_critical_warning` 속성으로 bit2(NVM subsystem reliability degraded)를 켠다.

```sh
#!/bin/sh
cd /j1; dmesg -n 1; modprobe nvme; sleep 3
sh ./j1_health.sh; ./j1_nvme /dev/nvme0 smart | head -1
dmesg | grep -iE "nvme.*(warn|smart|crit|aen)" | sed 's/^\[[ 0-9.]*\] //'
```

```sh
QEXTRA="-device nvme,serial=j1nvme01,id=nv0,smart_critical_warning=4 \
 -drive if=none,id=n1,file=/var/tmp/J1/j1_ns1.img,format=raw -device nvme-ns,drive=n1,bus=nv0,nsid=1" \
 TMO=150 bash ~/.elinux_scratch/J1/boot.sh g_warn.sh
```

```text
lab initramfs: 6.18.55-fs up 1.72s
{"dev":"nvme0n1","type":"NVMe","pct_used":0,"crit_warn":"0x04","dev_wr_mib":0,"host_wr_mib":0}
smart: crit_warn=0x04 temp=323K avail_spare=0% thresh=0% pct_used=0%
```

출력에서 볼 것: `crit_warn=0x04`가 SMART 로그와 §10의 건강 스크립트(JSON) 양쪽에 나온다. dmesg에는 아무 메시지가 없다(grep 결과 0줄). 커널 nvme 드라이버(`drivers/nvme/host/core.c`의 `nvme_complete_async_event`)는 SMART AEN(Asynchronous Event)을 받아도 dmesg에 찍지 않고 **uevent `NVME_AEN=0x...`와 트레이스 이벤트 `nvme_async_event`** 로만 알린다. 게다가 부팅 전부터 켜져 있던 경고는 이벤트 자체가 오지 않는다 — **호스트가 주기적으로 물어봐야** 안다. 필드 장비에서 "dmesg를 보면 된다"는 틀린 답이다.

---

## 7. virtio-blk와 "Linux가 보는 약속" 비교

### 7.1 virtio-blk

VM 안의 디스크(`/dev/vda`)는 FTL도 NAND도 없다. 게스트의 요청이 virtqueue를 통해 호스트 QEMU로 가고, 호스트 파일(그 아래는 Mac의 APFS, 그 아래는 Apple SSD의 FTL)에 써진다. 개발·CI에서는 편하지만, **flush·FUA·discard의 의미가 호스트 설정(`cache=`, `discard=`)에 달려 있다**는 것을 기억해야 한다. 이 랩에서 J2·J5의 파일시스템 실험이 virtio-blk를 쓰는 이유는 간단함 때문이다.

### 7.2 예제 10 — 네 스택의 queue 속성과 discard 변형

무엇을 확인하나: 드라이버가 block 계층에 거는 약속(queue limits, I11 §6.2) — 회전 여부, 쓰기 캐시, FUA, discard 단위 — 이 매체마다 어떻게 다른지, 그리고 `BLKDISCARD`·`BLKSECDISCARD`·`BLKZEROOUT`이 실제로 지원되는지.

```c
/* j1_blkinfo.c - what does the block layer promise for this device? + try discard variants */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

static void q(const char *dev, const char *attr)
{
    char p[256], v[128] = "-";
    snprintf(p, sizeof p, "/sys/block/%s/queue/%s", dev, attr);
    FILE *f = fopen(p, "r");
    if (f) { if (!fgets(v, sizeof v, f)) strcpy(v, "?"); fclose(f); v[strcspn(v, "\n")] = 0; }
    printf(" %s=%s", attr, v);
}
static void try(int fd, unsigned long req, const char *name, uint64_t off, uint64_t len)
{
    uint64_t r[2] = { off, len };
    if (ioctl(fd, req, r) == 0) printf("  %-13s %4llu KiB @%llu: OK\n", name,
                                       (unsigned long long)(len >> 10), (unsigned long long)off);
    else printf("  %-13s %4llu KiB @%llu: %s\n", name, (unsigned long long)(len >> 10),
                (unsigned long long)off, strerror(errno));
}
int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s sda|nvme0n1|mmcblk0|vda [discard]\n", argv[0]); return 1; }
    char path[64];
    snprintf(path, sizeof path, "/dev/%s", argv[1]);
    int fd = open(path, O_RDWR);
    if (fd < 0) { perror(path); exit(1); }
    uint64_t sz; int lbs, pbs;
    if (ioctl(fd, BLKGETSIZE64, &sz) < 0 || ioctl(fd, BLKSSZGET, &lbs) < 0 || ioctl(fd, BLKPBSZGET, &pbs) < 0)
        { perror("BLK*GET"); exit(1); }
    printf("%-8s size=%llu MiB lbs=%d pbs=%d\n", argv[1], (unsigned long long)(sz >> 20), lbs, pbs);
    printf("        "); q(argv[1], "rotational"); q(argv[1], "write_cache"); q(argv[1], "fua");
    q(argv[1], "scheduler"); printf("\n        ");
    q(argv[1], "discard_granularity"); q(argv[1], "discard_max_hw_bytes");
    q(argv[1], "write_zeroes_max_bytes"); printf("\n");
    if (argc > 2) {
        uint64_t off = sz / 2;
        try(fd, BLKDISCARD, "BLKDISCARD", off, 1 << 20);
        try(fd, BLKSECDISCARD, "BLKSECDISCARD", off, 1 << 20);
        try(fd, BLKZEROOUT, "BLKZEROOUT", off, 1 << 20);
    }
    close(fd);
    return 0;
}
```

네 스택(위 `g_all.sh` 부팅, 마지막 세 줄은 `nvme0n1`에 discard 변형을 시험한 결과):

```text
== block layer view
vda      size=128 MiB lbs=512 pbs=512
         rotational=1 write_cache=write back fua=0 scheduler=none [mq-deadline] kyber bfq 
         discard_granularity=512 discard_max_hw_bytes=2147483136 write_zeroes_max_bytes=2147483136
nvme0n1  size=256 MiB lbs=512 pbs=512
         rotational=0 write_cache=write back fua=1 scheduler=[none] mq-deadline kyber bfq 
         discard_granularity=512 discard_max_hw_bytes=2147483136 write_zeroes_max_bytes=524288
sda      size=64 MiB lbs=512 pbs=512
         rotational=0 write_cache=write back fua=1 scheduler=none [mq-deadline] kyber bfq 
         discard_granularity=0 discard_max_hw_bytes=0 write_zeroes_max_bytes=33553920
mmcblk0  size=128 MiB lbs=512 pbs=512
         rotational=0 write_cache=write back fua=1 scheduler=none [mq-deadline] kyber bfq 
         discard_granularity=1048576 discard_max_hw_bytes=3670016 write_zeroes_max_bytes=0
  BLKDISCARD    1024 KiB @134217728: OK
  BLKSECDISCARD 1024 KiB @134217728: Operation not supported
  BLKZEROOUT    1024 KiB @134217728: OK
```

eMMC 64 MiB(예제 3 부팅)·SD(예제 5)·UFS(예제 6)에서 같은 도구로 `discard`까지 시험한 결과는 각각 위 출력의 `== block layer view` 부분에 있다. 한 표로 모으면:

| 장치 | rotational | write_cache | fua | discard 단위 | BLKSECDISCARD (— 는 시험 안 함) | 왜 그런가 |
|---|---|---|---|---|---|---|
| vda (virtio) | 1 | write back | 0 | 512 B | — | virtio는 회전 여부를 알리지 않으면 1로 둔다, FUA 미지원이라 커널이 flush로 흉내 낸다 |
| nvme0n1 | 0 | write back | 1 | 512 B | 미지원 | VWC=1, NVMe Write의 FUA 비트, DSM deallocate |
| sda (scsi_debug) | 0 | write back | 1 | 0 (없음) | — | 기본 파라미터에서 UNMAP 미지원 |
| sda (UFS) | 1 | write back | 1 | 4 KiB | 미지원 | QEMU UFS LU는 회전 여부 미보고, UNMAP 지원 |
| mmcblk0 (eMMC) | 0 | write back | 1 | erase group (512 KiB~1 MiB) | 미지원 | reliable write로 FUA 구현, `SEC_FEATURE_SUPPORT=0` |
| mmcblk0 (SD) | 0 | write through | 0 | 512 KiB | 미지원 | reliable write 없음, cache 확장 꺼짐 |

출력에서 볼 것:

- **eMMC의 `write_cache=write back fua=1`은 캐시 때문이 아니다**. `CACHE_SIZE=0`인데도 그렇다. 커널 `mmc_blk_alloc_req()`(drivers/mmc/core/block.c)를 보면 카드가 CMD23과 reliable write(`rel_sectors` 또는 `EXT_CSD_WR_REL_PARAM_EN`)를 지원하면 `BLK_FEAT_WRITE_CACHE | BLK_FEAT_FUA`를 켜고, 그렇지 않을 때만 캐시 여부를 본다. 즉 **FUA 쓰기 = eMMC reliable write**로 매핑된다. ext4 저널 커밋 블록(FUA)이 reliable write로 나간다는 뜻이다(J5).
- discard 단위가 매체의 erase 단위를 드러낸다: eMMC는 erase group(64 MiB 카드 512 KiB, 128 MiB 카드 1 MiB — 용량에 따라 `HC_ERASE_GRP_SIZE`가 달라졌다), UFS는 4 KiB 논리 블록, NVMe·virtio는 512 B.
- `BLKSECDISCARD`는 어디서도 안 됐다. "삭제한 데이터를 확실히 지운다"가 요구 사항이면(보안 장비 폐기) 부품이 secure erase/sanitize를 지원하는지 데이터시트로 먼저 확인하고, 안 되면 암호화 + 키 폐기(L5)로 설계한다.
- 스케줄러 기본값: NVMe는 `none`, 나머지는 `mq-deadline`. 큐가 하나인 장치(eMMC·SD)에서는 스케줄러가 의미 있고, 다중 큐 NVMe에서는 보통 끈다.

---

## 8. 성능 — 스택의 오버헤드만 본다

### 8.1 예제 11 — QD1 O_DIRECT 벤치마크

무엇을 확인하나: 같은 도구로 4 KiB 랜덤과 1 MiB 순차를 큐 깊이 1로 재서 **드라이버 스택마다 요청 하나에 드는 고정 비용**을 비교한다. 다시 강조한다: QEMU TCG에서 백엔드는 모두 VM 디스크의 파일이고 에뮬레이션 비용이 지배한다. **이 숫자는 NVMe나 eMMC 실물의 속도가 아니다**. 실물 비교는 §11 표의 일반값을 본다.

```c
/* j1_bench.c - QD1 O_DIRECT benchmark: rr|rw (4 KiB random) or sr|sw (sequential), N ops */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage: %s dev rr|rw|sr|sw bs_kib nops label\n", argv[0]); return 1; }
    const char *m = argv[2];
    size_t bs = (size_t)atoi(argv[3]) << 10; int n = atoi(argv[4]);
    int wr = m[1] == 'w', rnd = m[0] == 'r';
    int fd = open(argv[1], (wr ? O_RDWR : O_RDONLY) | O_DIRECT);
    if (fd < 0) { perror("open"); exit(1); }
    uint64_t sz;
    if (ioctl(fd, BLKGETSIZE64, &sz) < 0) { perror("BLKGETSIZE64"); exit(1); }
    uint64_t span = sz / 2 / bs;                      /* use the second half of the device */
    void *buf;
    if (posix_memalign(&buf, 4096, bs)) { perror("posix_memalign"); exit(1); }
    memset(buf, 0x5a, bs);
    double *lat = malloc(sizeof(double) * n);
    if (!lat) { perror("malloc"); exit(1); }
    srand(1);
    double t0 = now();
    for (int i = 0; i < n; i++) {
        uint64_t blk = rnd ? (uint64_t)rand() % span : (uint64_t)i % span;
        off_t off = (off_t)(sz / 2 + blk * bs);
        double a = now();
        ssize_t r = wr ? pwrite(fd, buf, bs, off) : pread(fd, buf, bs, off);
        if (r != (ssize_t)bs) { perror("io"); exit(1); }
        lat[i] = (now() - a) * 1e6;
    }
    double el = now() - t0;
    if (wr && fdatasync(fd) < 0) { perror("fdatasync"); exit(1); }
    qsort(lat, n, sizeof(double), cmp);
    printf("%-16s %s bs=%4zuK n=%5d IOPS=%7.0f MB/s=%7.1f p50=%7.1fus p99=%7.1fus\n", argv[5], m,
           bs >> 10, n, n / el, n * (double)bs / el / 1e6, lat[n / 2], lat[n * 99 / 100]);
    free(lat); free(buf); close(fd);
    return 0;
}
```

네 스택(`g_all.sh`의 bench 부분):

```text
== bench (QD1, O_DIRECT, QEMU TCG)
vda              rr bs=   4K n= 2000 IOPS=   4081 MB/s=   16.7 p50=  235.9us p99=  404.8us
vda              rw bs=   4K n= 2000 IOPS=   3224 MB/s=   13.2 p50=  242.5us p99= 1917.6us
vda              sr bs=1024K n=   48 IOPS=   1045 MB/s= 1095.5 p50=  722.0us p99= 3195.6us
vda              sw bs=1024K n=   48 IOPS=   1013 MB/s= 1062.0 p50=  781.3us p99= 5062.6us
nvme0n1          rr bs=   4K n= 2000 IOPS=   4806 MB/s=   19.7 p50=  199.8us p99=  319.7us
nvme0n1          rw bs=   4K n= 2000 IOPS=   4725 MB/s=   19.4 p50=  202.0us p99=  337.5us
nvme0n1          sr bs=1024K n=   48 IOPS=   1016 MB/s= 1064.9 p50=  630.2us p99= 5449.5us
nvme0n1          sw bs=1024K n=   48 IOPS=   1874 MB/s= 1965.3 p50=  506.3us p99= 1124.0us
mmcblk0          rr bs=   4K n= 2000 IOPS=    891 MB/s=    3.6 p50= 1089.4us p99= 1453.3us
mmcblk0          rw bs=   4K n= 2000 IOPS=    875 MB/s=    3.6 p50= 1129.8us p99= 1346.6us
mmcblk0          sr bs=1024K n=   48 IOPS=      5 MB/s=    5.3 p50=196241.9us p99=225956.0us
mmcblk0          sw bs=1024K n=   48 IOPS=      4 MB/s=    3.8 p50=272226.1us p99=418652.8us
sda              rr bs=   4K n= 2000 IOPS=    212 MB/s=    0.9 p50= 4525.9us p99= 6019.4us
sda              rw bs=   4K n= 2000 IOPS=    207 MB/s=    0.8 p50= 4652.6us p99= 7542.7us
sda              sr bs=1024K n=   48 IOPS=     62 MB/s=   64.7 p50=14939.3us p99=45646.9us
sda              sw bs=1024K n=   48 IOPS=     67 MB/s=   70.7 p50=14494.0us p99=25109.0us
```

`sda`가 4.5 ms로 이상하게 느리다. 원인은 `scsi_debug`의 기본 `delay=1`(1 jiffy, `CONFIG_HZ=250`이면 4 ms) — 명령마다 일부러 지연을 넣는다. 0으로 바꿔 다시 쟀다:

```sh
#!/bin/sh
# scsi_debug: default delay=1 jiffy vs delay=0 (pure SCSI mid-layer + sd cost)
cd /j1; dmesg -n 1
echo "HZ-ish: CONFIG_HZ=$(zcat /proc/config.gz 2>/dev/null | grep '^CONFIG_HZ=' | cut -d= -f2)"
modprobe scsi_debug dev_size_mb=64; sleep 2
echo "delay=$(cat /sys/bus/pseudo/drivers/scsi_debug/delay) ndelay=$(cat /sys/bus/pseudo/drivers/scsi_debug/ndelay)"
./j1_bench /dev/sda rr 4 500 "sda delay=1"
echo 0 > /sys/bus/pseudo/drivers/scsi_debug/delay
echo "delay=$(cat /sys/bus/pseudo/drivers/scsi_debug/delay) ndelay=$(cat /sys/bus/pseudo/drivers/scsi_debug/ndelay)"
./j1_bench /dev/sda rr 4 2000 "sda delay=0"; ./j1_bench /dev/sda rw 4 2000 "sda delay=0"
./j1_bench /dev/sda sr 1024 48 "sda delay=0"; ./j1_bench /dev/sda sw 1024 48 "sda delay=0"
```

```text
lab initramfs: 6.18.55-fs up 2.82s
HZ-ish: CONFIG_HZ=250
delay=1 ndelay=0
sda delay=1      rr bs=   4K n=  500 IOPS=    214 MB/s=    0.9 p50= 4511.3us p99= 6737.4us
delay=0 ndelay=0
sda delay=0      rr bs=   4K n= 2000 IOPS=   6582 MB/s=   27.0 p50=  123.8us p99=  560.5us
sda delay=0      rw bs=   4K n= 2000 IOPS=   6014 MB/s=   24.6 p50=  127.7us p99=  971.3us
sda delay=0      sr bs=1024K n=   48 IOPS=     81 MB/s=   85.3 p50=11941.5us p99=20309.1us
sda delay=0      sw bs=1024K n=   48 IOPS=     67 MB/s=   70.0 p50=14956.0us p99=17259.8us
```

UFS(별도 부팅, LU 하나):

```sh
#!/bin/sh
cd /j1; dmesg -n 1; insmod j1_ufshcd_pci.ko; sleep 4
./j1_bench /dev/sda rr 4 2000 "sda (ufs)"; ./j1_bench /dev/sda rw 4 2000 "sda (ufs)"
./j1_bench /dev/sda sr 1024 48 "sda (ufs)"; ./j1_bench /dev/sda sw 1024 48 "sda (ufs)"
```

```text
lab initramfs: 6.18.55-fs up 1.65s
sda (ufs)        rr bs=   4K n= 2000 IOPS=   3884 MB/s=   15.9 p50=  247.9us p99=  395.8us
sda (ufs)        rw bs=   4K n= 2000 IOPS=   3292 MB/s=   13.5 p50=  245.5us p99=  904.3us
sda (ufs)        sr bs=1024K n=   48 IOPS=   1095 MB/s= 1147.9 p50=  676.7us p99= 3941.7us
sda (ufs)        sw bs=1024K n=   48 IOPS=   1341 MB/s= 1406.0 p50=  753.0us p99= 1484.3us
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">4 KiB QD1 IOPS — QEMU TCG (스택 비용 비교, 실물 속도 아님)</text>
  <line x1="70" y1="250" x2="670" y2="250" stroke="currentColor"/>
  <line x1="70" y1="40" x2="70" y2="250" stroke="currentColor"/>
  <line x1="66" y1="190" x2="670" y2="190" stroke="#888" stroke-dasharray="3 4"/>
  <line x1="66" y1="130" x2="670" y2="130" stroke="#888" stroke-dasharray="3 4"/>
  <line x1="66" y1="70" x2="670" y2="70" stroke="#888" stroke-dasharray="3 4"/>
  <text x="62" y="254" text-anchor="end" font-size="12">0</text>
  <text x="62" y="194" text-anchor="end" font-size="12">2000</text>
  <text x="62" y="134" text-anchor="end" font-size="12">4000</text>
  <text x="62" y="74" text-anchor="end" font-size="12">6000</text>
  <rect x="90" y="127.6" width="36" height="122.4" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <rect x="130" y="153.3" width="36" height="96.7" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
  <text x="108" y="123.6" text-anchor="middle" font-size="12">4081</text>
  <text x="148" y="149.3" text-anchor="middle" font-size="12">3224</text>
  <text x="128" y="268" text-anchor="middle" font-size="12">virtio-blk</text>
  <rect x="200" y="105.8" width="36" height="144.2" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <rect x="240" y="108.2" width="36" height="141.8" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
  <text x="218" y="101.8" text-anchor="middle" font-size="12">4806</text>
  <text x="258" y="104.2" text-anchor="middle" font-size="12">4725</text>
  <text x="238" y="268" text-anchor="middle" font-size="12">NVMe</text>
  <rect x="310" y="133.5" width="36" height="116.5" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <rect x="350" y="151.2" width="36" height="98.8" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
  <text x="328" y="129.5" text-anchor="middle" font-size="12">3884</text>
  <text x="368" y="147.2" text-anchor="middle" font-size="12">3292</text>
  <text x="348" y="268" text-anchor="middle" font-size="12">UFS</text>
  <rect x="420" y="52.5" width="36" height="197.5" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <rect x="460" y="69.6" width="36" height="180.4" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
  <text x="438" y="48.5" text-anchor="middle" font-size="12">6582</text>
  <text x="478" y="65.6" text-anchor="middle" font-size="12">6014</text>
  <text x="458" y="268" text-anchor="middle" font-size="12">scsi_debug</text>
  <rect x="530" y="223.3" width="36" height="26.7" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <rect x="570" y="223.8" width="36" height="26.2" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
  <text x="548" y="219.3" text-anchor="middle" font-size="12">891</text>
  <text x="588" y="219.8" text-anchor="middle" font-size="12">875</text>
  <text x="568" y="268" text-anchor="middle" font-size="12">eMMC/SDHCI</text>
  <rect x="200" y="290" width="14" height="12" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <text x="220" y="300" font-size="12">랜덤 읽기</text>
  <rect x="320" y="290" width="14" height="12" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
  <text x="340" y="300" font-size="12">랜덤 쓰기</text>
  <text x="430" y="300" font-size="12">scsi_debug는 delay=0</text>
</svg>
```
그림 4 — 같은 VM, 같은 도구, 큐 깊이 1. 차이는 매체가 아니라 **에뮬레이션된 버스와 드라이버 경로**에서 온다.

출력에서 볼 것:

- **scsi_debug(delay=0)가 가장 빠르다**: 하드웨어 에뮬레이션이 없고(RAM memcpy, 모듈 안에서 완료), SCSI mid-layer와 `sd`의 소프트웨어 비용만 남는다. 4 KiB 요청 하나에 약 124 us — TCG에서 Linux 블록 계층 + SCSI 계층을 한 번 도는 값이다.
- NVMe·virtio·UFS는 200~250 us로 비슷하다: QEMU가 각각의 장치 모델(doorbell·virtqueue·UTRD)을 에뮬레이트하고 파일 I/O를 한다.
- **eMMC/SDHCI가 4~5배 느리고, 1 MiB 순차는 5 MB/s로 폭락**했다: SD/MMC 프로토콜은 명령마다 CMD/응답/데이터 단계가 있고, QEMU의 SDHCI 모델은 이를 레지스터 단위로 흉내 낸다(ADMA 디스크립터를 따라가며 블록마다 처리). 실물에서도 eMMC는 큐 하나·명령 하나씩(CQE 없으면)이라 랜덤 QD에서 NVMe와 비교할 수 없지만, 5 MB/s는 순전히 에뮬레이션의 값이다. 실물 HS400 eMMC의 순차 읽기는 일반적으로 250~300 MB/s대다(기억 기반).
- 교훈: **QEMU 벤치마크는 스택 구조 차이(큐 개수, 요청당 고정 비용)를 보는 데만 쓴다**. 부품 선정 근거로 쓰려면 실물 보드에서 `fio`로 잰다.

---

## 9. 수명 계산 — TBW, P/E, WAF

### 9.1 공식

Don에게는 익숙한 식이다.

- **TBW**(Total Bytes Written, 호스트 기준 총 쓰기 허용량) ≈ 사용자 용량 x 정격 P/E 사이클 ÷ WAF
- **WAF**(Write Amplification Factor) = NAND에 실제로 쓴 양 ÷ 호스트가 쓴 양
- **수명(년)** = TBW ÷ (하루 호스트 쓰기 x 365)

임베디드에서 WAF를 키우는 것: 작은 동기 쓰기(로그 한 줄마다 `fsync`), 정렬 안 된 파티션, discard 없음, 가득 찬 장치(GC 여유 공간 부족), 4 KiB 미만 쓰기를 16 KiB 페이지에 패딩, 파일시스템 저널(데이터 저널링 모드면 2배). 줄이는 것: 로그를 RAM에 모았다가 큰 단위로 쓰기, tmpfs에 휘발 로그(J3), F2FS 같은 로그 구조 FS(J2), 오버프로비저닝(파티션을 다 쓰지 않기), 주기적 `fstrim`.

### 9.2 예제 12 — 수명 계산기와 차트

무엇을 확인하나: 같은 하루 4 GB 쓰기(로그 + rosbag 조각 + OS 업데이트를 합친 예시 값)에서 매체·WAF별 수명. P/E 값은 **예시 값**(일반적인 데이터시트 등급 범위)이며 실제 부품은 데이터시트의 TBW 또는 P/E를 쓴다.

```python
# j1_endurance.py - flash lifetime = capacity x P/E / (WAF x daily host writes); prints table + SVG chart
import math, sys

media = [  # name, user capacity GB, rated P/E cycles  (illustrative datasheet-class values)
    ("8GB eMMC MLC 3k", 8, 3000),
    ("32GB eMMC TLC 1k", 32, 1000),
    ("32GB TLC as pSLC 20k", 32 / 3, 20000),
    ("256MB SLC NAND 50k", 0.25, 50000),
]
DAILY_GB = 4.0                       # host writes per day (logs + rosbag chunks + OS)
WAFS = [1, 1.5, 2, 3, 4, 6, 8, 10]

def years(cap, pe, waf):
    return cap * pe / waf / DAILY_GB / 365.0

print(f"daily host writes = {DAILY_GB} GB/day")
print(f"{'media':22s} {'TBW@WAF1':>9s} " + " ".join(f"WAF{w:<4g}" for w in WAFS))
for n, c, pe in media:
    print(f"{n:22s} {c * pe / 1000:8.1f}T " + " ".join(f"{years(c, pe, w):7.1f}" for w in WAFS))

if len(sys.argv) > 1:                # emit chart: x = WAF (linear 1..10), y = years (log 0.1..100)
    X0, X1, Y0, Y1 = 70, 470, 30, 290
    col = ["#4a7bd0", "#e08a3c", "#3f9a6b", "#d0564a"]
    px = lambda w: X0 + (w - 1) / 9 * (X1 - X0)
    py = lambda y: Y1 - (math.log10(max(min(y, 100), 0.1)) + 1) / 3 * (Y1 - Y0)
    for i, (n, c, pe) in enumerate(media):
        pts = " ".join(f"{px(w):.1f},{py(years(c, pe, w)):.1f}" for w in WAFS)
        print(f'<polyline points="{pts}" fill="none" stroke="{col[i]}" stroke-width="2"/>')
    for y in (0.1, 1, 10, 100):
        print(f"ytick {y} {py(y):.1f}")
    for w in (1, 2, 4, 6, 8, 10):
        print(f"xtick {w} {px(w):.1f}")
    print(f"y5 {py(5):.1f}")
```

```sh
limactl shell elinux -- python3 -I ~/.elinux_scratch/J1/j1_endurance.py
```

```text
daily host writes = 4.0 GB/day
media                   TBW@WAF1 WAF1    WAF1.5  WAF2    WAF3    WAF4    WAF6    WAF8    WAF10  
8GB eMMC MLC 3k            24.0T    16.4    11.0     8.2     5.5     4.1     2.7     2.1     1.6
32GB eMMC TLC 1k           32.0T    21.9    14.6    11.0     7.3     5.5     3.7     2.7     2.2
32GB TLC as pSLC 20k      213.3T   146.1    97.4    73.1    48.7    36.5    24.4    18.3    14.6
256MB SLC NAND 50k         12.5T     8.6     5.7     4.3     2.9     2.1     1.4     1.1     0.9
```

차트 좌표는 같은 스크립트를 `svg` 인자로 실행해 얻었다(로그 y축).

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="18" text-anchor="middle" font-size="14" font-weight="bold">하루 4 GB 쓰기에서 수명(년) vs WAF — 예시 P/E 값</text>
  <line x1="70" y1="290" x2="470" y2="290" stroke="currentColor"/>
  <line x1="70" y1="30" x2="70" y2="290" stroke="currentColor"/>
  <line x1="70" y1="203.3" x2="470" y2="203.3" stroke="#888" stroke-dasharray="3 4"/>
  <line x1="70" y1="116.7" x2="470" y2="116.7" stroke="#888" stroke-dasharray="3 4"/>
  <line x1="70" y1="30" x2="470" y2="30" stroke="#888" stroke-dasharray="3 4"/>
  <line x1="70" y1="142.8" x2="470" y2="142.8" stroke="#d0564a" stroke-dasharray="6 3"/>
  <text x="466" y="138" text-anchor="end" font-size="12">목표 5년</text>
  <text x="64" y="294" text-anchor="end" font-size="12">0.1</text>
  <text x="64" y="207" text-anchor="end" font-size="12">1</text>
  <text x="64" y="121" text-anchor="end" font-size="12">10</text>
  <text x="64" y="34" text-anchor="end" font-size="12">100</text>
  <text x="70" y="306" text-anchor="middle" font-size="12">1</text>
  <text x="114.4" y="306" text-anchor="middle" font-size="12">2</text>
  <text x="203.3" y="306" text-anchor="middle" font-size="12">4</text>
  <text x="292.2" y="306" text-anchor="middle" font-size="12">6</text>
  <text x="381.1" y="306" text-anchor="middle" font-size="12">8</text>
  <text x="470" y="306" text-anchor="middle" font-size="12">10</text>
  <text x="270" y="326" text-anchor="middle" font-size="12">WAF</text>
  <text x="22" y="160" text-anchor="middle" font-size="12">년</text>
  <polyline points="70.0,98.0 92.2,113.2 114.4,124.0 158.9,139.3 203.3,150.1 292.2,165.4 381.1,176.2 470.0,184.6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <polyline points="70.0,87.1 92.2,102.4 114.4,113.2 158.9,128.5 203.3,139.3 292.2,154.6 381.1,165.4 470.0,173.8" fill="none" stroke="#e08a3c" stroke-width="2"/>
  <polyline points="70.0,30.0 92.2,31.0 114.4,41.8 158.9,57.1 203.3,67.9 292.2,83.2 381.1,94.0 470.0,102.4" fill="none" stroke="#3f9a6b" stroke-width="2"/>
  <polyline points="70.0,122.5 92.2,137.8 114.4,148.6 158.9,163.9 203.3,174.7 292.2,190.0 381.1,200.8 470.0,209.2" fill="none" stroke="#d0564a" stroke-width="2"/>
  <line x1="490" y1="60" x2="514" y2="60" stroke="#3f9a6b" stroke-width="2"/>
  <text x="520" y="64" font-size="12">32GB TLC → pSLC</text>
  <line x1="490" y1="90" x2="514" y2="90" stroke="#e08a3c" stroke-width="2"/>
  <text x="520" y="94" font-size="12">32GB eMMC TLC 1k</text>
  <line x1="490" y1="120" x2="514" y2="120" stroke="#4a7bd0" stroke-width="2"/>
  <text x="520" y="124" font-size="12">8GB eMMC MLC 3k</text>
  <line x1="490" y1="150" x2="514" y2="150" stroke="#d0564a" stroke-width="2"/>
  <text x="520" y="154" font-size="12">256MB SLC NAND 50k</text>
  <text x="490" y="190" font-size="12">TBW = 용량 x P/E ÷ WAF</text>
  <text x="490" y="210" font-size="12">수명 = TBW ÷ (4 GB x 365)</text>
  <text x="490" y="240" font-size="12">32GB 위 첫 점은 146년 →</text>
  <text x="490" y="258" font-size="12">100에서 잘림</text>
</svg>
```
그림 5 — 수명 대 WAF. SLC raw NAND가 P/E는 가장 높지만 **용량이 작아서** 수명이 가장 짧다. 같은 32 GB TLC eMMC도 pSLC로 설정하면(용량 1/3) 수명이 몇 배가 된다. 그리고 모든 선이 WAF에 반비례한다 — 소프트웨어 설계(쓰기 패턴)가 부품 선택만큼 중요하다.

출력에서 볼 것:

- 8 GB MLC eMMC는 WAF 3이면 5.5년으로 겨우 5년 목표를 넘고, WAF 6이면 2.7년이다. "로그를 줄마다 fsync"하는 코드 하나가 WAF를 몇 배로 만든다는 것을 생각하면, **부품을 바꾸기 전에 쓰기 패턴을 측정하라**는 결론이 나온다.
- 측정 방법: `/sys/block/<dev>/stat`의 7번째 필드(쓴 섹터 수)로 호스트 쓰기량을 재고(§10), NVMe면 SMART `Data Units Written`, eMMC라면 벤더 전용 명령(일부 벤더가 NAND 쓰기량을 보고)이나 `life_time` 단계 변화 시점으로 실제 마모 속도를 역산한다.
- 하루 쓰기량 4 GB가 정말인지도 측정한다: 로봇 한 대를 하루 돌리고 `stat`의 차이를 본다. 추정이 아니라 측정값으로 계산해야 면접에서도 설득력이 있다.

---

## 10. 필드 건강 모니터링 설계

### 10.1 무엇을 모으나

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="18" text-anchor="middle" font-size="14" font-weight="bold">저장장치 건강 감시 — 수집 → 판단 → 행동</text>
  <text x="110" y="42" text-anchor="middle" font-size="13" font-weight="bold">수집 (하루 1회 + 부팅 시)</text>
  <rect x="10" y="52" width="200" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="74" text-anchor="middle" font-size="12">eMMC: life_time · pre_eol</text>
  <rect x="10" y="94" width="200" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="116" text-anchor="middle" font-size="12">UFS: health_descriptor</text>
  <rect x="10" y="136" width="200" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="158" text-anchor="middle" font-size="12">NVMe: SMART 02h</text>
  <rect x="10" y="178" width="200" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="200" text-anchor="middle" font-size="12">/sys/block/X/stat 쓰기량</text>
  <rect x="10" y="220" width="200" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="242" text-anchor="middle" font-size="12">dmesg I/O 에러 · ios timing</text>
  <rect x="10" y="262" width="200" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="284" text-anchor="middle" font-size="12">CID · fwrev (부품 식별)</text>
  <line x1="212" y1="174" x2="258" y2="174" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="260" y="104" width="180" height="140" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="350" y="126" text-anchor="middle" font-size="13" font-weight="bold">health agent</text>
  <text x="350" y="150" text-anchor="middle" font-size="12">이전 값과 차이 계산</text>
  <text x="350" y="172" text-anchor="middle" font-size="12">하루 쓰기량 추세</text>
  <text x="350" y="194" text-anchor="middle" font-size="12">잔여 수명 예측</text>
  <text x="350" y="216" text-anchor="middle" font-size="12">JSON 한 줄로 기록</text>
  <line x1="442" y1="174" x2="488" y2="174" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="580" y="42" text-anchor="middle" font-size="13" font-weight="bold">행동</text>
  <rect x="490" y="60" width="180" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="580" y="78" text-anchor="middle" font-size="12">정상: 텔레메트리 전송</text>
  <text x="580" y="96" text-anchor="middle" font-size="12">플릿 대시보드 추세</text>
  <rect x="490" y="120" width="180" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="580" y="138" text-anchor="middle" font-size="12">경고: 로그 수준 낮춤</text>
  <text x="580" y="156" text-anchor="middle" font-size="12">교체 예약 티켓</text>
  <rect x="490" y="180" width="180" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="580" y="198" text-anchor="middle" font-size="12">긴급: 기록 중지</text>
  <text x="580" y="216" text-anchor="middle" font-size="12">데이터 RO · 즉시 교체</text>
  <rect x="490" y="240" width="180" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="580" y="258" text-anchor="middle" font-size="12">I/O 에러 급증: 원격</text>
  <text x="580" y="276" text-anchor="middle" font-size="12">덤프 후 안전 모드</text>
</svg>
```
그림 6 — 필드 감시의 구조. 장치가 주는 수명 값은 해상도가 낮고(10% 단위) 늦게 바뀌므로, **호스트가 직접 잰 쓰기량 추세**와 함께 봐야 교체 시점을 몇 달 전에 예측할 수 있다.

설계 원칙:

- **매체별 원천을 하나의 스키마로**: eMMC `life_time`/`pre_eol_info`, UFS `life_time_estimation_a/b`/`eol_info`, NVMe `pct_used`/`crit_warn`/`avail_spare` — 모두 "사용 비율 + 예약 블록 상태 + 경보"로 정규화한다.
- **호스트 쓰기량은 장치와 무관하게 잰다**: `/sys/block/X/stat`은 부팅마다 0부터 시작하므로, 에이전트가 마지막 값과 누적값을 비휘발로 저장한다(그 저장 자체가 쓰기를 만들므로 하루 1회).
- **부품 식별을 같이 보낸다**: CID의 manfid·제품명·fwrev. 플릿 데이터에서 "특정 fwrev만 마모가 빠르다"를 찾아낼 수 있다.
- **임계값은 보수적으로**: `pre_eol_info=0x02`(예약 80%)가 되면 이미 늦을 수 있다. life_time 단계가 바뀐 날짜들로 기울기를 구해 "목표 수명 전에 0x0A에 도달하는가"를 판단한다.
- **행동은 단계적으로**: 로그 수준을 낮추고(쓰기 감소), 녹화를 다른 매체로 돌리고, 최후에는 데이터 파티션을 읽기 전용으로 바꿔 rootfs와 부팅 능력을 지킨다.

### 10.2 예제 13 — 최소 건강 에이전트(셸)

무엇을 확인하나: 위 원칙을 BusyBox 셸만으로 구현한 스케치. 장치마다 JSON 한 줄을 출력하고, 쓰기 전후로 호스트 쓰기량이 정확히 늘어나는지 본다.

```sh
#!/bin/sh
# j1_health.sh - one JSON line per storage device: wear + host-write accounting (field telemetry sketch)
for b in /sys/block/mmcblk[0-9] /sys/block/nvme[0-9]n[0-9]; do
  [ -e $b ] || continue; n=$(basename $b)
  set -- $(cat $b/stat); wr_sect=$7                     # field 7 = sectors written (512 B units)
  case $n in
  mmcblk*) d=$b/device
    set -- $(cat $d/life_time); a=$(($1)); bb=$(($2)); eol=$(($(cat $d/pre_eol_info)))
    echo "{\"dev\":\"$n\",\"type\":\"$(cat $d/type)\",\"life_a\":$a,\"life_b\":$bb,\"pre_eol\":$eol,\"host_wr_mib\":$((wr_sect / 2048))}";;
  nvme*) c=/dev/${n%n[0-9]}
    s=$(/j1/j1_nvme $c smart | tr -s ' \n' ' ')
    pu=$(echo "$s" | sed 's/.*pct_used=\([0-9]*\)%.*/\1/'); cw=$(echo "$s" | sed 's/.*crit_warn=\(0x[0-9a-f]*\).*/\1/')
    du=$(echo "$s" | sed 's/.* written=\([0-9]*\) .*/\1/')
    echo "{\"dev\":\"$n\",\"type\":\"NVMe\",\"pct_used\":$pu,\"crit_warn\":\"$cw\",\"dev_wr_mib\":$((du * 512000 / 1048576)),\"host_wr_mib\":$((wr_sect / 2048))}";;
  esac
done
```

```sh
#!/bin/sh
cd /j1; dmesg -n 1
insmod j1_sdhci_uhs2.ko && insmod j1_sdhci_pci.ko; modprobe nvme; sleep 3
sh ./j1_health.sh
dd if=/dev/zero of=/dev/mmcblk0 bs=1M count=8 conv=fsync 2>/dev/null; dd if=/dev/zero of=/dev/nvme0n1 bs=1M count=16 conv=fsync 2>/dev/null
echo "-- after writing 8 MiB to mmcblk0 and 16 MiB to nvme0n1"; sh ./j1_health.sh
```

```text
lab initramfs: 6.18.55-fs up 1.54s
{"dev":"mmcblk0","type":"MMC","life_a":0,"life_b":0,"pre_eol":0,"host_wr_mib":0}
{"dev":"nvme0n1","type":"NVMe","pct_used":0,"crit_warn":"0x00","dev_wr_mib":0,"host_wr_mib":0}
-- after writing 8 MiB to mmcblk0 and 16 MiB to nvme0n1
{"dev":"mmcblk0","type":"MMC","life_a":0,"life_b":0,"pre_eol":0,"host_wr_mib":8}
{"dev":"nvme0n1","type":"NVMe","pct_used":0,"crit_warn":"0x00","dev_wr_mib":16,"host_wr_mib":16}
```

출력에서 볼 것: 호스트 쓰기량(`host_wr_mib`)이 eMMC 8, NVMe 16으로 정확히 늘었고, NVMe는 장치가 센 쓰기량(`dev_wr_mib`, SMART)도 16으로 일치한다. 실제 SSD에서는 `dev_wr_mib`이 호스트 기준(SMART Data Units Written)이라 NAND 쓰기량이 아니다 — 진짜 WAF를 보려면 벤더 확장 로그(예: OCP 클라우드 SSD 스펙의 Physical Media Units Written)가 필요하다. 예제 9의 경고 주입과 함께 돌리면 `crit_warn`이 `"0x04"`로 바뀐다(위 예제 9 출력의 첫 JSON 줄).

---

## 11. 선택 가이드 — 한 표로

아래 표의 비용·내구성·속도는 **2020년대 중반 일반적인 경향**(기억 기반 상대값)이다. 실제 선정은 벤더 데이터시트, 장기 공급(longevity) 약속, 전원 차단 시험 보고서로 한다.

| 매체 | FTL 위치 | GB당 비용 (상대) | 내구성 | 전원 차단 시 | 부팅 지원 | Linux 스택 · 노드 |
|---|---|---|---|---|---|---|
| SPI/parallel NOR | 없음 (필요 없음) | 매우 높음 (용량 작음) | 섹터당 약 10만 회 | erase 중단 시 그 섹터만 깨짐 | 거의 모든 SoC BootROM | MTD · `/dev/mtdN` |
| raw SLC NAND | 호스트 (UBI) | 낮음~중간 (용량 작음) | 높음 (SLC, 수만 회 이상) | UBI/UBIFS가 설계상 견딤 | 많은 SoC (배드 블록 주의) | MTD + UBI · `/dev/mtdN`, `/dev/ubiX_Y` |
| SPI-NAND | 호스트 (UBI), ECC는 칩 안 | 낮음 | SLC급 | UBI 의존 | 일부 SoC | MTD(spinand) + UBI |
| eMMC (TLC/MLC) | 장치 | 낮음 | 중간 (TLC 수천 회), pSLC로 상향 | 부품마다 다름, 캐시 flush 필요 | 거의 모든 SoC, boot0/1 HW 파티션 | MMC core + `mmc_block` · `/dev/mmcblkN{,bootN,rpmb}` |
| SD / microSD | 장치 | 가장 낮음 | 낮음~중간 (소비자용) | 보장 없음 (산업용 제외) | 많은 SoC (개발·복구용) | MMC core · `/dev/mmcblkN` |
| UFS | 장치 | 중간 | 중간, WriteBooster·pSLC LU | 부품마다 다름 | 고급 SoC (Boot LU A/B) | ufshcd + SCSI + `sd` · `/dev/sdX` |
| NVMe SSD (M.2) | 장치 (DRAM 매핑) | 중간 | TBW 명시 (수백 TB급) | 클라이언트는 PLP 없음, 엔터프라이즈는 PLP | 일부 SoC/UEFI만 직접 부팅 | nvme-core + nvme(pci) · `/dev/nvmeXnY` |
| virtio-blk | 호스트 OS의 파일 | — | — | 호스트 `cache=` 설정에 따름 | 펌웨어/QEMU | virtio_blk · `/dev/vdX` |

### 11.1 로봇·센서 키트에 대입하면

예를 들어 센서 키트(Jetson이나 i.MX8 + 카메라 4대 + IMU + LiDAR 로그)라면:

- **부트**: SoC가 eMMC boot0/boot1을 지원하면 부트로더 A/B를 거기에(C2 §10). SPI-NOR가 있으면 BL2·U-Boot를 NOR에 두고 eMMC가 죽어도 복구 모드로 들어갈 수 있게 한다.
- **rootfs**: eMMC TLC 영역, 읽기 전용(SquashFS/EROFS, J3) + A/B 슬롯(L4). 쓰기가 거의 없으니 TLC로 충분하다.
- **설정·작은 DB·짧은 로그**: eMMC의 **enhanced(pSLC) 영역**에 별도 파티션, F2FS 또는 ext4(J2).
- **대용량 녹화**: NVMe(M.2). 녹화는 큰 순차 쓰기라 WAF가 1에 가깝고 TBW 계산이 쉽다. 전원 차단 시 마지막 수 초는 잃을 수 있다고 설계에 명시한다(J5).
- **SD**: 개발용·현장 데이터 회수용 교체 매체로만. 루트로 쓰지 않는다.
- **raw NAND**: 수만 대 양산, 저장할 것이 작고(수백 MB), 넓은 온도·긴 수명이 필요한 보조 컨트롤러 보드에서만.

---

## 12. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="18" text-anchor="middle" font-size="14" font-weight="bold">매체별 Linux 노드와 드라이버 스택 (위 = 유저 쪽)</text>
  <text x="63" y="42" text-anchor="middle" font-size="12" font-weight="bold">NOR (CFI)</text>
  <rect x="10" y="52" width="106" height="28" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="63" y="71" text-anchor="middle" font-size="12">/dev/mtdN</text>
  <line x1="63" y1="80" x2="63" y2="90" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="92" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="63" y="111" text-anchor="middle" font-size="12">JFFS2·UBI</text>
  <line x1="63" y1="120" x2="63" y2="130" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="132" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="63" y="151" text-anchor="middle" font-size="12">MTD core</text>
  <line x1="63" y1="160" x2="63" y2="170" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="172" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="63" y="191" text-anchor="middle" font-size="12">cfi_cmdset</text>
  <line x1="63" y1="200" x2="63" y2="210" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="212" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="63" y="231" text-anchor="middle" font-size="12">physmap-of</text>
  <line x1="63" y1="240" x2="63" y2="250" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="252" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="63" y="271" text-anchor="middle" font-size="12">메모리 버스</text>
  <line x1="63" y1="280" x2="63" y2="290" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="292" width="106" height="28" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/>
  <text x="63" y="311" text-anchor="middle" font-size="12">FTL 없음</text>
  <text x="175" y="42" text-anchor="middle" font-size="12" font-weight="bold">raw NAND</text>
  <rect x="122" y="52" width="106" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="175" y="71" text-anchor="middle" font-size="12">/dev/mtdN</text>
  <line x1="175" y1="80" x2="175" y2="90" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="122" y="92" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="175" y="111" text-anchor="middle" font-size="12">UBI·UBIFS</text>
  <line x1="175" y1="120" x2="175" y2="130" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="122" y="132" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="175" y="151" text-anchor="middle" font-size="12">MTD core</text>
  <line x1="175" y1="160" x2="175" y2="170" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="122" y="172" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="175" y="191" text-anchor="middle" font-size="12">nand_base</text>
  <line x1="175" y1="200" x2="175" y2="210" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="122" y="212" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="175" y="231" text-anchor="middle" font-size="12">nand ctrl</text>
  <line x1="175" y1="240" x2="175" y2="250" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="122" y="252" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="175" y="271" text-anchor="middle" font-size="12">ONFI 버스</text>
  <line x1="175" y1="280" x2="175" y2="290" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="122" y="292" width="106" height="28" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
  <text x="175" y="311" text-anchor="middle" font-size="12">UBI = FTL</text>
  <text x="287" y="42" text-anchor="middle" font-size="12" font-weight="bold">eMMC · SD</text>
  <rect x="234" y="52" width="106" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="287" y="71" text-anchor="middle" font-size="12">/dev/mmcblkN</text>
  <line x1="287" y1="80" x2="287" y2="90" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="234" y="92" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="287" y="111" text-anchor="middle" font-size="12">blk-mq</text>
  <line x1="287" y1="120" x2="287" y2="130" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="234" y="132" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="287" y="151" text-anchor="middle" font-size="12">mmc_block</text>
  <line x1="287" y1="160" x2="287" y2="170" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="234" y="172" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="287" y="191" text-anchor="middle" font-size="12">MMC core</text>
  <line x1="287" y1="200" x2="287" y2="210" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="234" y="212" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="287" y="231" text-anchor="middle" font-size="12">sdhci-*</text>
  <line x1="287" y1="240" x2="287" y2="250" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="234" y="252" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="287" y="271" text-anchor="middle" font-size="12">SD/MMC 버스</text>
  <line x1="287" y1="280" x2="287" y2="290" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="234" y="292" width="106" height="28" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
  <text x="287" y="311" text-anchor="middle" font-size="12">장치 FTL</text>
  <text x="399" y="42" text-anchor="middle" font-size="12" font-weight="bold">UFS</text>
  <rect x="346" y="52" width="106" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="399" y="71" text-anchor="middle" font-size="12">/dev/sdX</text>
  <line x1="399" y1="80" x2="399" y2="90" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="346" y="92" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="399" y="111" text-anchor="middle" font-size="12">blk-mq</text>
  <line x1="399" y1="120" x2="399" y2="130" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="346" y="132" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="399" y="151" text-anchor="middle" font-size="12">sd + SCSI</text>
  <line x1="399" y1="160" x2="399" y2="170" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="346" y="172" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="399" y="191" text-anchor="middle" font-size="12">ufshcd core</text>
  <line x1="399" y1="200" x2="399" y2="210" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="346" y="212" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="399" y="231" text-anchor="middle" font-size="12">ufs-qcom 등</text>
  <line x1="399" y1="240" x2="399" y2="250" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="346" y="252" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="399" y="271" text-anchor="middle" font-size="12">UniPro</text>
  <line x1="399" y1="280" x2="399" y2="290" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="346" y="292" width="106" height="28" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
  <text x="399" y="311" text-anchor="middle" font-size="12">장치 FTL</text>
  <text x="511" y="42" text-anchor="middle" font-size="12" font-weight="bold">NVMe</text>
  <rect x="458" y="52" width="106" height="28" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="511" y="71" text-anchor="middle" font-size="12">/dev/nvmeXnY</text>
  <line x1="511" y1="80" x2="511" y2="90" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="458" y="92" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="511" y="111" text-anchor="middle" font-size="12">blk-mq</text>
  <line x1="511" y1="120" x2="511" y2="130" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="458" y="132" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="511" y="151" text-anchor="middle" font-size="12">nvme-core</text>
  <line x1="511" y1="160" x2="511" y2="170" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="458" y="172" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="511" y="191" text-anchor="middle" font-size="12">PCI core</text>
  <line x1="511" y1="200" x2="511" y2="210" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="458" y="212" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="511" y="231" text-anchor="middle" font-size="12">nvme (pci)</text>
  <line x1="511" y1="240" x2="511" y2="250" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="458" y="252" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="511" y="271" text-anchor="middle" font-size="12">PCIe</text>
  <line x1="511" y1="280" x2="511" y2="290" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="458" y="292" width="106" height="28" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
  <text x="511" y="311" text-anchor="middle" font-size="12">SSD FTL</text>
  <text x="623" y="42" text-anchor="middle" font-size="12" font-weight="bold">virtio-blk</text>
  <rect x="570" y="52" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="623" y="71" text-anchor="middle" font-size="12">/dev/vdX</text>
  <line x1="623" y1="80" x2="623" y2="90" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="570" y="92" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="623" y="111" text-anchor="middle" font-size="12">blk-mq</text>
  <line x1="623" y1="120" x2="623" y2="130" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="570" y="132" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="623" y="151" text-anchor="middle" font-size="12">virtio_blk</text>
  <line x1="623" y1="160" x2="623" y2="170" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="570" y="172" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="623" y="191" text-anchor="middle" font-size="12">virtio core</text>
  <line x1="623" y1="200" x2="623" y2="210" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="570" y="212" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="623" y="231" text-anchor="middle" font-size="12">virtio-mmio</text>
  <line x1="623" y1="240" x2="623" y2="250" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="570" y="252" width="106" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="623" y="271" text-anchor="middle" font-size="12">virtqueue</text>
  <line x1="623" y1="280" x2="623" y2="290" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="570" y="292" width="106" height="28" fill="#888" fill-opacity="0.25" stroke="#888"/>
  <text x="623" y="311" text-anchor="middle" font-size="12">호스트 파일</text>
  <text x="340" y="352" text-anchor="middle" font-size="12">행: /dev 노드 → 상위(FS·UBI·blk-mq) → 클래스 드라이버 → 프로토콜 코어 → 호스트 드라이버 → 버스 → FTL 위치</text>
</svg>
```
그림 7 — 여섯 매체가 Linux에서 지나는 층. MTD 쪽(왼쪽 둘)은 블록 계층을 타지 않고, 나머지 넷은 모두 blk-mq를 지나 각자의 `queue_rq`로 간다. eMMC는 같은 카드에서 `mmcblkNboot0/1`(블록)과 `mmcblkNrpmb`(문자 장치)가 함께 나온다. SPI-NOR라면 `cfi_cmdset` 자리에 `spi-nor` 코어, `physmap-of` 자리에 SoC의 QSPI 컨트롤러 드라이버가 온다.

이 노트에서 쓴 유저 API가 닿는 커널 함수(랩 소스 6.18.55에서 이름 확인):

| 유저가 한 일 | 커널 경로 | 다시 만나는 곳 |
|---|---|---|
| `ioctl(/dev/mtd0, MEMERASE)` | `mtdchar_ioctl()` → `mtd_erase()` → `cfi_intelext_erase_varsize()` → `do_erase_oneblock()` | I11 §1, J4 |
| NOR 인식 | `physmap_flash_probe()` → `do_map_probe("cfi_probe")` → `cfi_cmdset_0001()` | D(DT `cfi-flash`), I11 |
| `ioctl(/dev/mmcblk0, MMC_IOC_CMD)` | `mmc_blk_ioctl_cmd()` → `__mmc_blk_ioctl_cmd()` (요청을 blk-mq에 drv_op로 넣어 다른 I/O와 직렬화) | C2 예제 9 |
| eMMC 블록 장치 생성 | `mmc_blk_probe()` → `mmc_blk_alloc_req()`(여기서 reliable write → `BLK_FEAT_FUA` 결정) → `mmc_blk_alloc_rpmb_part()` | I11 §5, J5 |
| eMMC I/O | `mmc_mq_queue_rq()` → `mmc_blk_mq_issue_rq()` → 호스트의 `mmc_host_ops.request` | I11 §5~6 |
| eMMC 캐시 켜기 / flush | `mmc_init_card()`에서 `CACHE_CTRL=1` / REQ_OP_FLUSH → `mmc_flush_cache()` | J5 |
| `ioctl(/dev/nvme0, NVME_IOCTL_ADMIN_CMD)` | `nvme_dev_ioctl()` → `nvme_user_cmd()` → `nvme_submit_user_cmd()` (admin 큐) | I11 예제 15 |
| SMART 경고 이벤트 | `nvme_complete_async_event()` → uevent `NVME_AEN` | L(텔레메트리) |
| `ioctl(/dev/sda, SG_IO)` | `sd_ioctl()` → `scsi_ioctl()` → `sg_io()` → blk-mq passthrough → `scsi_queue_rq()` → `ufshcd_queuecommand()` | Q(카메라 USB 저장 등) |
| UFS W-LUN 추가 | `ufshcd_scsi_add_wlus()` | — |
| `BLKDISCARD` | `blkdev_common_ioctl()` → `blk_ioctl_discard()` → `REQ_OP_DISCARD` → 드라이버(eMMC: `mmc_blk_issue_discard_rq()` → CMD35/36/38) | J2(fstrim) |

드라이버 작성자 관점의 요점:

- **block 드라이버가 정하는 queue limits가 상위 FS의 동작을 바꾼다**. eMMC가 `BLK_FEAT_FUA`를 켜면 ext4·F2FS의 커밋이 FUA 쓰기(= reliable write)가 되고, 끄면 커널이 "쓰기 + flush"로 흉내 낸다. 새 저장장치 드라이버를 쓸 때 `queue_limits`의 `features`, `max_discard_sectors`, `discard_granularity`, `physical_block_size`를 정직하게 채우는 것이 전원 차단 안전성과 WAF에 직접 영향을 준다(G·I11 §6.2).
- **passthrough ioctl(`MMC_IOC_CMD`, `NVME_IOCTL_ADMIN_CMD`, `SG_IO`)도 blk-mq를 지난다**. 그래서 일반 I/O와 순서가 보장되고, 동시에 잘못된 명령(예: eMMC CMD6로 `PARTITION_CONFIG`를 바꾸기)이 커널 상태와 어긋날 위험이 있다. 커널이 RPMB와 boot 파티션 접근을 별도 장치·`force_ro`로 감싸는 이유다.

---

## 13. Embedded 실무에서 다시 보기

### 13.1 장면 1 — "18개월 된 로봇 30대가 동시에 부팅 실패"

증상: `EXT4-fs error`, `mmc0: ... timeout`, 일부는 rootfs가 읽기 전용으로 리마운트. 공통점: 같은 8 GB eMMC, 같은 로깅 설정. 분석 순서: (1) 살아 있는 장비에서 `life_time`과 `pre_eol_info`를 읽는다 — `0x0b 0x0b`(수명 초과)와 `0x03`(긴급)이면 마모가 원인이다. (2) `/sys/block/mmcblk0/stat`으로 하루 쓰기량을 측정한다 — 예를 들어 하루 15 GB면 8 GB x 3000 ÷ WAF 3 ÷ 15 GB ≈ 1.5년이라 계산이 맞는다. (3) 쓰기 원천을 찾는다(`iotop`, `/proc/<pid>/io`의 `write_bytes`) — 대개 디버그 로그를 줄마다 `fsync`하는 데몬이다. (4) 고치는 법: 로그를 tmpfs + 주기적 압축 업로드로(J3), 데이터 파티션을 pSLC로, 남은 장비에 §10의 감시를 넣는다.

### 13.2 장면 2 — 부품 선정 회의에서 "그냥 SD로 하죠"

반박 근거를 숫자로 준비한다: SD는 수명 레지스터가 없어 필드 감시가 불가능하고(예제 5에서 `life_time` 속성 자체가 없음), 전원 차단 보장이 없고, 같은 품번에서 내부 부품이 바뀐다. 대안: eMMC 32 GB(단가 차이는 보통 몇 달러 수준 — 기억 기반), 또는 산업용 SD(pSLC, 고정 BOM)와 그 벤더의 건강 조회 명령.

### 13.3 장면 3 — Jetson 녹화 SSD 선택

카메라 4대 x 압축 영상 + LiDAR로 하루 8시간 x 50 MB/s = 1.44 TB/일. 1 TB SSD의 TBW가 600 TB면 600 ÷ 1.44 ≈ 417일 — 1년 남짓이다. 그래서 (1) TBW가 높은 모델(또는 2 TB로 용량을 키워 TBW를 늘림), (2) 녹화 정책(원시 데이터는 이벤트 전후만), (3) SMART `pct_used`를 텔레메트리로 보내 교체를 예약한다. 이 계산을 면접에서 즉석으로 할 수 있으면 "하드웨어를 아는 소프트웨어 엔지니어"로 보인다.

### 13.4 장면 4 — 공장 프로비저닝 스크립트의 한 줄

eMMC enhanced area와 boot 파티션 설정은 **한 번만** 가능하다. 공장 스크립트는 (1) EXT_CSD를 읽어 이미 설정됐는지 확인하고(`PARTITION_SETTING_COMPLETED`), (2) 안 됐으면 검증된 값으로 쓰고, (3) 전원을 재인가한 뒤 (4) 다시 읽어 검증한다. mmc-utils로는 대략 다음과 같다(형식만 — 이 VM에 mmc-utils가 없고 QEMU eMMC가 enhanced를 지원하지 않아 실행하지 않음, 정확한 인자는 해당 버전의 `mmc help`로 확인):

```sh
mmc extcsd read /dev/mmcblk0 | grep -iE "PARTITION_SETTING|MAX_ENH|ENH_SIZE"
mmc enh_area set -y 0 4194304 /dev/mmcblk0      # start 0 KiB, length 4 GiB (in KiB), -y = complete
# power cycle the board here - setting takes effect only after power-on
mmc extcsd read /dev/mmcblk0 | grep -i ENH_SIZE_MULT
```

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 소비자용 SD를 rootfs로 출하 | 필드에서 전원 차단 후 부팅 불가, 카드 전체가 읽기 전용 | 전원 차단 보장 없는 FTL, 매핑 테이블 손상 | eMMC/산업용 SD, RO rootfs(J3), A/B(L4) |
| 로그를 줄마다 `fsync` | 1~2년 만에 eMMC 수명 초과 (`life_time 0x0b`) | 작은 동기 쓰기 → WAF 폭증 | RAM 버퍼 + 큰 단위 쓰기, tmpfs, F2FS, 쓰기량 측정 |
| 개발 보드에서 enhanced area를 시험 삼아 설정 | user area가 1/3로 줄어 되돌릴 수 없음 | `PARTITION_SETTING_COMPLETED`는 한 번만 | 공장 스크립트로만, 값 검증 후 |
| eMMC `life_time` 값을 그대로 %로 해석 | 0x01을 1%로 보고 "새것"이라 판단 | 0x01 = 0~10% 구간, 0x0B = 초과 | `(v-1) x 10%`~`v x 10%` 구간으로, 단계 변화 날짜 기록 |
| `enhanced_area_offset` 값을 믿음 | 1.8e19 같은 오프셋 | 미정의 시 커널이 -EINVAL을 넣음 | `PARTITION_SETTING_COMPLETED`와 함께 확인 |
| NVMe 경고를 dmesg로만 감시 | 수명·spare 경고를 놓침 | SMART는 로그를 찍지 않음 (uevent·trace만) | 주기적 SMART 조회 + `NVME_AEN` uevent 수신 |
| pflash0에 데이터 파일을 줌 (QEMU) | 콘솔 무출력, timeout | virt가 pflash0을 펌웨어로 부팅 | 데이터는 pflash1, 또는 `-bios` 체인 |
| 에뮬레이터에서 flash 쓰기 의미론 검증 | 실제 칩에서 데이터 깨짐 | QEMU pflash는 1→0 AND를 흉내 내지 않음 | nandsim/mtdram 또는 실물로 검증 |
| 파티션을 erase group에 정렬하지 않음 | discard가 효과 없음, 쓰기 지연 증가 | 부분 erase group은 장치가 무시 | 1 MiB 또는 4 MiB 정렬 |
| QEMU 벤치 숫자로 부품 비교 | eMMC가 5 MB/s라는 잘못된 결론 | TCG 에뮬레이션 비용 | 실물 보드에서 `fio` |

---

## 15. 면접에서 이렇게 말한다

**Q.** "For a field robot, would you choose eMMC or raw NAND? Why?"

**A.** 기본은 eMMC다. FTL·ECC·배드 블록을 검증된 벤더 펌웨어가 맡고, 용량당 가격이 싸고, BootROM이 boot0/boot1에서 부팅하고, 수명 레지스터로 필드 감시가 된다. raw NAND는 용량이 작고 SLC 내구성이 필요하고 양산 수량이 커서 BOM 몇 달러가 중요하며, 팀이 UBI/UBIFS를 책임질 수 있을 때 고른다. 대신 FTL 버그가 우리 버그가 된다.

> I'd default to eMMC. The FTL, ECC and bad-block management live in vendor firmware that's been qualified at scale, cost per gigabyte is low, every SoC boots from its boot partitions, and EXT_CSD gives me life-time estimates for fleet monitoring. I'd pick raw SLC NAND only when the data is small, I need SLC endurance or extended temperature, volume makes the BOM delta matter, and the team can own UBI/UBIFS — because then the FTL bugs are ours. In both cases I'd keep the rootfs read-only and put frequently written data in an SLC or pSLC region.

**꼬리 질문**: "What does eMMC give you that raw NAND doesn't, in terms of boot?" → HW boot 파티션 두 개 + `PARTITION_CONFIG`로 A/B 부트로더 전환, RPMB.

**Q.** "How do you monitor flash wear in the field?"

**A.** 장치가 주는 값과 호스트가 직접 재는 값을 같이 본다. eMMC는 `life_time`(A/B, 10% 단위)과 `pre_eol_info`, UFS는 health descriptor, NVMe는 SMART의 percentage used·available spare·critical warning. 여기에 `/sys/block/X/stat`의 쓴 섹터 수를 하루 단위로 누적해 추세를 구하고, 부품 식별(CID, 펌웨어 리비전)과 함께 JSON으로 보낸다. 장치 값은 해상도가 낮고 늦게 바뀌므로 쓰기량 추세로 잔여 수명을 예측하고, 단계별 행동(로그 줄이기 → 교체 예약 → 데이터 RO)을 정해 둔다.

> I collect two kinds of data. Device-reported: eMMC life_time A/B and PRE_EOL_INFO from EXT_CSD, the UFS health descriptor, or the NVMe SMART log — percentage used, available spare, critical warning. Host-measured: sectors written from `/sys/block/<dev>/stat`, accumulated daily. A small agent normalizes them into one JSON record with the part's manufacturer ID and firmware revision, and ships it with telemetry. Since the device values move in 10 percent steps, I use the write-rate trend to project end of life, and I define actions in stages: reduce logging, schedule replacement, and finally make the data partition read-only to protect boot.

**Q.** "Who owns the FTL in eMMC, UFS and NVMe, and what can the host still influence?"

**A.** 셋 다 장치가 FTL을 갖는다. 호스트가 줄 수 있는 것은 힌트와 설정이다: discard/TRIM(어떤 LBA가 무효인지), flush와 FUA(언제 내구화가 필요한지), eMMC reliable write, BKOPS 시간, pSLC/enhanced 영역이나 UFS LU 프로비저닝, WriteBooster 켜기, NVMe 네임스페이스·오버프로비저닝. 그리고 쓰기 패턴 자체 — 이것이 WAF를 가장 크게 바꾼다.

> All three keep the FTL inside the device. The host influences it through hints and configuration: discard or TRIM to tell it which LBAs are dead, flush and FUA to say when durability matters, reliable write on eMMC, giving it idle time for background operations, provisioning SLC regions — eMMC enhanced area or UFS LUs — enabling WriteBooster, and over-provisioning by not using the full capacity. But the biggest lever is the write pattern: large aligned writes and fewer syncs keep write amplification near one.

**Q.** "Why is an SD card a poor choice for a production root filesystem?"

**A.** 전원 차단 보장이 없고(매핑 테이블이 깨져 카드 전체가 죽음), 수명 레지스터가 없어 필드 감시가 안 되고, 같은 품번에서 내부 부품이 바뀌어 검증이 유지되지 않고, 소켓이 진동에 약하다. 개발용이나 교체 가능한 데이터 매체로만 쓰고, 꼭 써야 하면 산업용 SD + 읽기 전용 rootfs.

> Consumer SD cards have no power-loss guarantee, so an interrupted write can corrupt the card's mapping tables and kill the whole card. There's no standard health register, so I can't monitor wear in the field. The controller and NAND inside can change between lots under the same part number, so qualification doesn't hold, and the socket is a weak point under vibration. I'd use SD for development or removable data offload; if I must boot from it, I'd use an industrial pSLC card with a fixed BOM and a read-only root.

**Q.** "Walk me through TBW and how you'd size storage for a logging robot."

**A.** TBW ≈ 용량 x P/E ÷ WAF. 먼저 하루 호스트 쓰기량을 실제 장비에서 잰다. 수명 목표(예: 5년)를 곱하고 WAF 가정을 넣으면 필요한 TBW가 나온다. 예를 들어 하루 4 GB, WAF 3, 5년이면 4 x 365 x 5 x 3 ≈ 22 TB의 NAND 쓰기 → 8 GB MLC eMMC(3000 P/E, 24 TB)는 여유가 없다. 용량을 키우거나 pSLC를 쓰거나 쓰기를 줄인다.

> Endurance is roughly capacity times rated P/E cycles divided by write amplification. I'd first measure daily host writes on a real unit, then multiply by the service life and an assumed WAF. For example, 4 GB a day for five years at WAF 3 is about 22 TB of NAND writes. An 8 GB MLC eMMC rated at 3,000 cycles gives about 24 TB of raw endurance, so there's no margin. I'd go to a larger part, configure pSLC for the hot partition, or cut the write rate — and then verify with the device's life-time counters on pilot units.

**Q.** "What's the difference between discard, trim, erase, secure erase and sanitize on eMMC?"

**A.** ERASE는 erase group 단위로 즉시 지우는 명령, TRIM은 write block 단위로 "이 섹터는 무효", DISCARD(4.5+)는 TRIM과 비슷하지만 장치가 내용을 나중에 지워도 되고 읽으면 옛 값이 나올 수 있다. secure erase/trim은 물리적으로 지우라는 요구였는데 4.51부터 비권장이고, SANITIZE가 무효화된 모든 영역의 물리 삭제를 대신한다. Linux `BLKDISCARD`는 카드가 지원하는 것 중 DISCARD → TRIM → ERASE 순으로 고르고, `BLKSECDISCARD`는 secure trim이 있어야 된다.

> On eMMC, ERASE wipes whole erase groups, TRIM marks write blocks as unused, and DISCARD — added in 4.5 — is like TRIM but lets the device keep the old data until it decides to reclaim it. Secure erase and secure trim asked for physical removal but were deprecated from 4.51 in favor of SANITIZE, which physically purges all unmapped areas. In Linux, BLKDISCARD uses the best of DISCARD, TRIM or ERASE that the card supports, and BLKSECDISCARD needs secure trim. If a product requirement says "data must be unrecoverable", I check sanitize support in the datasheet or use encryption with key destruction.

**Q.** "Why does a UFS device show up as /dev/sda?"

**A.** UFS의 명령 세트가 SCSI이기 때문이다. 호스트 컨트롤러 드라이버 `ufshcd`가 SCSI host를 등록하고, 각 LU가 SCSI LUN이 되어 `sd` 드라이버가 블록 장치로 만든다. UFS Device·Boot·RPMB 같은 well-known LU는 블록 장치 없이 SCSI 장치로만 붙는다. 그래서 UFS 디버그 도구는 `SG_IO`로 CDB를 보내는 SCSI 도구와 bsg 노드다.

> Because UFS uses the SCSI command set over UniPro. The ufshcd driver registers a SCSI host, each logical unit becomes a SCSI LUN, and the sd driver turns it into a block device. Well-known LUs like the device, boot and RPMB W-LUNs attach as SCSI devices without block nodes. So debugging UFS uses SCSI tooling — SG_IO pass-through — plus the UFS bsg node for query requests and descriptors.

---

## 16. 직접 해보기

1. **개념**: 16 MiB SPI-NOR에 U-Boot 환경 변수를 두 사본(redundant env)으로 둘 때 각 사본은 최소 몇 바이트를 차지하나(4 KiB 서브섹터 erase를 쓸 수 있을 때와 64 KiB만 쓸 수 있을 때)? 정답/힌트: erase 단위가 사본의 최소 크기다 — 4 KiB 또는 64 KiB, 사본은 서로 다른 섹터에.
2. **코드**: `j1_nor.c`를 고쳐 sector 하나를 1000번 erase·program하고 매번 시간을 기록하라. QEMU에서 시간이 변하는가? 정답/힌트: 변하지 않는다 — 실제 칩은 마모에 따라 erase 시간이 늘어나고, 이것이 수명 진단 신호가 되기도 한다.
3. **코드**: `j1_extcsd.c`에 `BKOPS_STATUS`가 2 이상이면 경고를 찍고, `PRE_EOL_INFO`와 `LIFE_TIME_EST`를 §10 JSON 형식으로 내보내는 옵션을 추가하라. 정답/힌트: 필드 에이전트가 sysfs 대신 ioctl을 써야 하는 경우(오래된 커널, rev 7 이전 카드)를 대비하는 코드다.
4. **관찰**: 예제 10의 게스트에서 `cat /sys/block/mmcblk0/queue/max_discard_segments`와 `max_hw_sectors_kb`를 장치별로 비교하라. 정답/힌트: eMMC는 discard 세그먼트 1개, NVMe는 여러 개 — 드라이버가 queue limits로 약속한 값이다(I11 §6.2).
5. **계산**: 1 TB NVMe의 TBW가 600 TB이고 로봇이 하루 400 GB를 기록한다. 5년을 버티려면 녹화량을 얼마로 줄여야 하나? 정답/힌트: 600 TB ÷ (5 x 365) ≈ 329 GB/일 (WAF 1 가정, 순차 녹화라 근사 가능).
6. **관찰**: QEMU nvme에 `smart_critical_warning=16`(bit4)을 주고 §10 스크립트를 돌려라. 이 비트는 무엇을 뜻하고 어떤 행동으로 연결해야 하나? 정답/힌트: volatile memory backup(PLP) 고장 — 전원 차단 시 캐시 데이터 손실 가능, 쓰기 캐시를 끄거나 교체.
7. **설계**: 8 GB eMMC를 쓰는 기존 제품의 로그 쓰기를 측정 없이 줄여 달라는 요청을 받았다. 무엇부터 측정하고 어떤 순서로 바꿀지 5단계로 적어라. 정답/힌트: stat 기반 일일 쓰기량 → 프로세스별 `write_bytes` → fsync 빈도(strace) → 버퍼링/tmpfs → pSLC 파티션은 마지막(되돌릴 수 없음).

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| FTL | Flash Translation Layer | LBA를 NAND 물리 위치로 바꾸고 GC·wear leveling·배드 블록을 처리하는 계층 |
| managed flash | 장치 안에 FTL이 있는 flash | eMMC, SD, UFS, SSD |
| raw flash | FTL 없는 flash | NOR, raw NAND, SPI-NAND — Linux에서 MTD |
| XIP | eXecute In Place | NOR를 메모리 맵에 두고 CPU가 직접 명령을 fetch |
| CFI | Common Flash Interface | parallel NOR가 자기 geometry를 알려 주는 질의 표준 |
| SFDP | Serial Flash Discoverable Parameters | SPI-NOR의 파라미터 표(JESD216) |
| ONFI | Open NAND Flash Interface | raw NAND의 명령·파라미터 페이지 표준 |
| on-die ECC | 칩 내장 ECC | SPI-NAND 등에서 칩이 비트 오류를 정정 — FTL은 아님 |
| EXT_CSD | eMMC Extended CSD | 512 B 설정·상태 레지스터, CMD8로 읽고 CMD6로 쓴다 |
| enhanced area / pSLC | eMMC의 SLC 모드 영역 | 용량을 줄이고 내구성·신뢰성을 높인다, 한 번만 설정 |
| PRE_EOL_INFO | eMMC 예약 블록 상태 | 1 정상, 2 경고(80%), 3 긴급 |
| DEVICE_LIFE_TIME_EST | eMMC 수명 추정 | 10% 단위 사용률, A = SLC 영역, B = MLC/TLC 영역 |
| BKOPS | Background Operations | 장치의 GC 등 배경 작업, 호스트가 시간을 줄 수 있다 |
| reliable write | eMMC 원자적 쓰기 | 전원 차단 시 옛 값 또는 새 값만 남는다, Linux FUA에 쓰임 |
| sanitize | 물리적 삭제 | 무효화된 모든 영역을 물리적으로 지운다 |
| HS200 / HS400 | eMMC 고속 타이밍 | 200 MHz SDR / DDR, 1.8 V, 튜닝 필요 |
| A1 / A2 | SD Application Performance Class | 랜덤 4 KiB IOPS 최저 보증 |
| UniPro / M-PHY | MIPI 링크·물리층 | UFS의 하위 계층 |
| UPIU | UFS Protocol Information Unit | UFS의 전송 패킷 |
| LU / W-LUN | Logical Unit / Well-known LUN | UFS의 독립 저장 공간 / 특수 기능 LU |
| WriteBooster | UFS SLC 쓰기 버퍼 | 버스트 쓰기 성능을 높이는 UFS 3.1+ 기능 |
| namespace | NVMe LBA 공간 | `/dev/nvme0n1`, `n2` … |
| SMART log | NVMe Log Page 02h | 온도·spare·사용률·쓰기량·전원 차단 횟수 |
| PLP | Power Loss Protection | 커패시터로 전원 차단 시 캐시를 flush |
| TBW | Total Bytes Written | 보증 총 쓰기량 |
| P/E cycle | Program/Erase 사이클 | 블록 하나를 지우고 다시 쓸 수 있는 횟수 |
| WAF | Write Amplification Factor | NAND 쓰기량 ÷ 호스트 쓰기량 |
| scsi_debug | 가상 SCSI 호스트 모듈 | RAM 기반 SCSI 장치로 sd 스택 시험 |
| SG_IO | SCSI generic ioctl | 블록 장치에 임의 CDB를 보내는 passthrough |

---

## 18. 요약 & 체크리스트

임베디드 저장장치는 "FTL이 어디 있나"로 나뉜다. NOR와 raw NAND·SPI-NAND는 FTL이 없어 Linux가 MTD + UBI로 직접 관리하고(`/dev/mtdN`), eMMC·SD·UFS는 작은 장치 FTL을 갖고 표준 레지스터(EXT_CSD, descriptor)로만 속을 보여 주며(`/dev/mmcblkN`, `/dev/sdX`), NVMe SSD는 Don이 만든 큰 FTL과 SMART를 가진다(`/dev/nvmeXnY`). 로봇 제품의 기본 조합은 부트용 eMMC boot 파티션(또는 SPI-NOR) + 읽기 전용 rootfs + pSLC 데이터 파티션 + 대용량 녹화용 NVMe이고, SD는 개발·교체 매체다. 수명은 TBW = 용량 x P/E ÷ WAF로 계산하되 하루 쓰기량은 반드시 측정하고, 필드에서는 장치 수명 값과 호스트 쓰기량 추세를 함께 수집해 단계적으로 대응한다. QEMU는 스택 구조를 보여 주지만 flash 물리(1→0 AND)와 속도는 흉내 내지 않는다.

- [ ] NOR, raw NAND, SPI-NAND, eMMC, SD, UFS, NVMe, virtio-blk 각각의 FTL 위치와 Linux 노드를 말할 수 있다
- [ ] NOR와 NAND의 read/program/erase 단위 차이와 XIP가 가능한 이유를 설명할 수 있다
- [ ] CFI NOR를 MTD ioctl(`MEMGETINFO`, `MEMERASE`)로 지우고 쓰는 C 코드를 짤 수 있다
- [ ] eMMC의 boot0/boot1/RPMB/GP/user와 enhanced(pSLC) 영역, 그리고 "한 번만 설정"의 위험을 설명할 수 있다
- [ ] `MMC_IOC_CMD`로 EXT_CSD를 읽고 `PRE_EOL_INFO`, `LIFE_TIME_EST_A/B`, 캐시, BKOPS, `SEC_FEATURE_SUPPORT`를 해석할 수 있다
- [ ] eMMC에서 `fua=1`이 reliable write에서 온다는 것을 커널 코드로 설명할 수 있다
- [ ] discard·trim·erase·secure erase·sanitize의 차이와 Linux ioctl 대응을 말할 수 있다
- [ ] SD 카드를 제품 rootfs로 쓰면 안 되는 이유 네 가지를 말할 수 있다
- [ ] UFS가 `/dev/sda`로 보이는 이유와 W-LUN을 설명하고, `SG_IO`로 INQUIRY를 보낼 수 있다
- [ ] NVMe Identify와 SMART 로그를 admin passthrough로 읽고 Data Units 단위를 계산할 수 있다
- [ ] TBW·P/E·WAF로 로봇 저장장치 수명을 계산하고 결과를 부품 선택에 연결할 수 있다
- [ ] 필드 건강 모니터링 에이전트의 수집 항목, 정규화, 행동 단계를 설계할 수 있다

---

## 참고 자료

- docs.kernel.org — MMC: `Documentation/driver-api/mmc/mmc-dev-attrs.rst`(SD/MMC 장치 속성), `mmc-dev-parts.rst`(boot 파티션·force_ro), `mmc-tools.rst`(mmc-utils), sysfs ABI `Documentation/ABI/testing/sysfs-bus-mmc`
- docs.kernel.org — MTD: `Documentation/driver-api/mtd/` (spi-nor, nand_ecc), UBI/UBIFS 문서 (`Documentation/filesystems/ubifs.rst`)
- docs.kernel.org — UFS: `Documentation/scsi/ufs.rst`, sysfs `Documentation/ABI/testing/sysfs-driver-ufs`
- 랩 커널 소스 6.18.55: `drivers/mmc/core/{mmc.c,block.c,core.c,queue.c}`, `drivers/mtd/maps/physmap-core.c`, `drivers/mtd/chips/cfi_cmdset_0001.c`, `drivers/nvme/host/{core.c,ioctl.c}`, `drivers/ufs/core/ufshcd.c`, `drivers/ufs/host/ufshcd-pci.c`, `drivers/scsi/scsi_debug.c` (elixir.bootlin.com 에서도 같은 경로)
- UAPI 헤더: `include/uapi/linux/mmc/ioctl.h`, `include/uapi/linux/nvme_ioctl.h`, `include/uapi/mtd/mtd-abi.h`, `include/scsi/sg.h`
- JEDEC JESD84-B51 (eMMC 5.1), JESD220 (UFS), JESD216 (SFDP), JESD68 (CFI) — 표준 문서(JEDEC 사이트, 무료 등록 필요)
- NVM Express Base Specification — Identify, Get Log Page(SMART/Health), Critical Warning 비트 (nvmexpress.org)
- SD Association Physical Layer Simplified Specification — Speed Class, Application Performance Class (sdcard.org)
- QEMU 문서: `docs/system/devices/nvme.rst`, `-device emmc`/`sd-card`/`ufs`/`ufs-lu`/`nvme-ns` (qemu.org documentation)
- mmc-utils (git.kernel.org의 `mmc-utils` 저장소), nvme-cli, sg3_utils — 이 노트의 C 도구들이 흉내 낸 표준 도구
- 이 시리즈: C2(부트 미디어), I11(MTD · MMC · block), J2(블록 FS), J3(읽기전용 FS), J4(Flash FS), J5(전원 차단 안전성), L4(OTA A/B)
