# I11. MTD · MMC · block — raw flash 계층, MMC 코어, blk-mq 드라이버 구조

> **이 노트를 다 읽으면**: raw flash(MTD → UBI → UBIFS)와 managed flash(eMMC/SD/NVMe → block 계층) 두 스택을 그림으로 그리고 각 층의 구조체·ops를 말할 수 있다 · `/dev/mtdN`을 ioctl로 직접 erase/program/read/OOB/배드 블록 조회하고 "erase 없이 다시 쓰면" 무슨 일이 나는지 설명할 수 있다 · UBI의 EC/VID 헤더, LEB→PEB 매핑, wear leveling, 배드 블록 예약을 SSD FTL과 1:1로 대응시킬 수 있다 · MMC 카드 열거 명령 순서와 `mmc_host_ops`, 그리고 blk-mq의 `queue_rq` → `blk_mq_complete_request` 경로를 직접 만든 드라이버로 증명할 수 있다
> **왜 필요한가**: 스토리지 드라이버·BSP 면접의 단골 질문("raw NAND에 ext4를 올려도 되나?", "eMMC와 raw NAND 차이?", "blk-mq 드라이버의 완료 경로는?")이고, 실무에서는 부팅 매체 bring-up, UBI 볼륨 설계, eMMC HS200 튜닝 실패 디버그로 다시 나온다
> **Don 기준 난이도**: NAND·ECC·배드 블록·wear leveling·NVMe 큐는 Don의 전문 분야다(SSD 펌웨어 쪽에서) / 새로 배울 부분은 **그 일을 Linux에서는 어느 계층의 어떤 구조체가 하는가** — MTD, UBI, MMC core, blk-mq의 이름과 경계
> **선행 노트**: G1(드라이버 종류 — `g1_blk` blk-mq RAM 디스크), C2(BootROM과 부트 미디어 — nandsim 배드 블록·ECC, eMMC boot 파티션·EXT_CSD), H9(DMA) — 이 노트는 그 둘을 반복하지 않고 **커널 스토리지 스택의 구조**로 한 단계 더 들어간다. J1·J4·J5(스토리지·Flash FS·전원 차단)로 이어진다

---

## 0. 큰 그림 — 같은 NAND, 두 개의 세계

Don이 Solidigm에서 만든 SSD를 호스트 Linux가 보면 `/dev/nvme0n1`이다. LBA를 주면 4 KiB를 돌려주는 **블록 장치**. FTL, ECC, 배드 블록, wear leveling, GC는 전부 SSD 안의 Cortex-R 펌웨어(Don의 코드)가 했고, 호스트는 그런 것이 있는지조차 모른다.

그런데 임베디드 보드에는 **FTL이 없는 NAND**가 붙어 있는 경우가 있다. SoC의 NAND 컨트롤러(또는 SPI)에 NAND 칩이 직접 붙은 경우다. 이때는 Don의 펌웨어가 하던 일을 **Linux 커널이 직접** 해야 한다. 그래서 Linux에는 스토리지 세계가 둘 있다.

- **raw flash 세계 (MTD)**: NOR, raw NAND, SPI-NAND, SPI-NOR. 장치가 "erase block을 지우고, page를 프로그램하고, OOB를 읽는" 원시 동작만 제공한다. 커널의 **MTD**(Memory Technology Device) 계층이 이것을 `struct mtd_info`로 추상화하고, 그 위에서 **UBI**가 FTL 역할(LEB→PEB 매핑, wear leveling, 배드 블록 관리)을 하고, **UBIFS**가 파일시스템을 올린다. 블록 장치가 아니다 — `/dev/mtd0`은 **문자 장치**다.
- **managed flash 세계 (block)**: eMMC, SD, UFS, NVMe SSD. 장치 안에 컨트롤러와 FTL이 있다. 커널은 이것을 **블록 장치**로 본다. 버스 쪽 드라이버(MMC 코어 + SDHCI 호스트, NVMe, UFS)가 명령을 나르고, 그 위의 **block 계층(blk-mq)** 이 bio를 request로 만들어 드라이버의 `.queue_rq`에 넣는다. ext4, F2FS가 그 위에 올라간다.

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="20" text-anchor="middle" font-size="14" font-weight="bold">raw flash 세계 (MTD)</text>
  <text x="510" y="20" text-anchor="middle" font-size="14" font-weight="bold">managed flash 세계 (block)</text>
  <rect x="20" y="32" width="640" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="54" text-anchor="middle" font-size="13">유저: open/read/write/fsync → VFS (공통)</text>
  <rect x="30" y="86" width="280" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="108" text-anchor="middle" font-size="13">UBIFS · JFFS2 · squashfs(ubiblock)</text>
  <rect x="30" y="140" width="280" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="158" text-anchor="middle" font-size="13">UBI = Linux 안의 FTL</text>
  <text x="170" y="176" text-anchor="middle" font-size="12">LEB→PEB · wear leveling · bad block</text>
  <rect x="30" y="204" width="280" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="222" text-anchor="middle" font-size="13">MTD core: struct mtd_info</text>
  <text x="170" y="240" text-anchor="middle" font-size="12">/dev/mtdN (char) · mtdblock · 파티션</text>
  <rect x="30" y="268" width="280" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="286" text-anchor="middle" font-size="13">raw NAND · SPI-NAND · SPI-NOR</text>
  <text x="170" y="304" text-anchor="middle" font-size="12">nand_chip · nand_controller · ECC 엔진</text>
  <rect x="30" y="332" width="280" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="354" text-anchor="middle" font-size="13">SoC NAND/QSPI 컨트롤러 (HW)</text>
  <rect x="30" y="386" width="280" height="34" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="408" text-anchor="middle" font-size="13">NAND/NOR 칩 (FTL 없음)</text>
  <rect x="370" y="86" width="280" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="108" text-anchor="middle" font-size="13">ext4 · F2FS · vfat · squashfs</text>
  <rect x="370" y="140" width="280" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="158" text-anchor="middle" font-size="13">block 계층 (blk-mq)</text>
  <text x="510" y="176" text-anchor="middle" font-size="12">bio → request → hctx · I/O 스케줄러</text>
  <rect x="370" y="204" width="280" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="222" text-anchor="middle" font-size="13">mmc_blk · nvme · ufshcd (+SCSI)</text>
  <text x="510" y="240" text-anchor="middle" font-size="12">blk_mq_ops.queue_rq 구현체</text>
  <rect x="370" y="268" width="280" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="286" text-anchor="middle" font-size="13">MMC core + SDHCI / NVMe PCI</text>
  <text x="510" y="304" text-anchor="middle" font-size="12">mmc_host_ops · SQ/CQ doorbell</text>
  <rect x="370" y="332" width="280" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="354" text-anchor="middle" font-size="13">호스트 컨트롤러 (SDHCI, PCIe)</text>
  <rect x="370" y="386" width="280" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="408" text-anchor="middle" font-size="13">장치 안 컨트롤러 + FTL + NAND</text>
  <line x1="170" y1="66" x2="170" y2="84" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="66" x2="510" y2="84" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="120" x2="170" y2="138" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="120" x2="510" y2="138" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="184" x2="170" y2="202" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="184" x2="510" y2="202" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="248" x2="170" y2="266" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="248" x2="510" y2="266" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="312" x2="170" y2="330" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="312" x2="510" y2="330" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="366" x2="170" y2="384" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="366" x2="510" y2="384" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="150" x2="340" y2="420" stroke="#888" stroke-dasharray="5 4"/>
</svg>
```
그림 1 — 두 스토리지 스택. 주황 상자가 "FTL이 사는 곳"이다. 왼쪽은 커널 안(UBI), 오른쪽은 장치 펌웨어 안(Don이 만든 바로 그것). 같은 NAND인데 FTL이 어디 있느냐에 따라 커널 스택 전체가 달라진다.

Don의 경험을 기준으로 다시 정리하면 이렇다.

| Don이 SSD 펌웨어에서 한 일 | raw flash 보드에서 Linux가 하는 곳 | managed flash 보드에서 하는 곳 |
|---|---|---|
| NAND 명령 시퀀서(READ ID, PAGE PROGRAM, BLOCK ERASE) | raw NAND 프레임워크 + SoC NAND 컨트롤러 드라이버의 `exec_op` | eMMC/UFS/SSD 내부 펌웨어 |
| ECC(LDPC/BCH) 엔진 | `nand_ecc_engine` — SW Hamming/BCH, 컨트롤러 HW(on-host), 칩 내장(on-die) | 장치 내부 |
| 배드 블록 테이블 | MTD `block_isbad`/BBT + UBI 예약 PEB | 장치 내부 |
| L2P 매핑(FTL) | UBI의 EBA(LEB→PEB) 테이블 | 장치 내부 |
| wear leveling · GC | UBI WL 서브시스템 + UBIFS GC | 장치 내부 (호스트는 TRIM/discard로 힌트만) |
| NVMe SQ/CQ 처리 | — | 호스트 쪽: blk-mq hctx ↔ NVMe SQ, `queue_rq` = SQ에 쓰고 doorbell |

### 0.1 실습 환경과 예제 1 — 이 노트의 모듈 빌드

모든 게스트 실험은 공용 랩의 **서브시스템 커널** `out/Image-sub`(Linux `6.18.55-sub`, mtdram·nandsim·mtdblock·ubi·ubifs·null_blk·mmc_test가 모듈로 들어 있다)에서 돌렸다. VM 호스트 커널은 `7.0.0-38-generic`(실습 도구 컴파일용)이다. QEMU는 `virt` 머신, `-smp 2` 또는 `-smp 4`, TCG 에뮬레이션이라 **측정값의 절대 크기는 실제 HW와 다르다** — 비교(상대값)만 본다.

무엇을 확인하나: 이 노트에서 쓰는 모듈 네 개가 `Image-sub` 빌드 트리로 경고 없이 빌드되고 vermagic이 `6.18.55-sub`로 맞는지. `Image-sub`에도 `CONFIG_MMC_SDHCI_PCI`는 꺼져 있어서(`# CONFIG_MMC_SDHCI_PCI is not set`) C2 노트처럼 `sdhci-pci-*.c`를 out-of-tree로 빌드했고, UBI는 wear leveling 실험을 위해 **임계값을 모듈 파라미터로 바꾼 사본**(`i11_ubi.ko`)을 따로 만들었다.

```sh
limactl shell elinux -- bash -lc 'S=/var/lab/src/linux-6.18.55
# (1) blk-mq 드라이버: sub 커널용 + dbg 커널용(트레이싱 실험) 두 벌
cd /var/tmp/I11/build_blk && make -j2 -C /var/lab/out/linux-build-sub M=$PWD modules
cd /var/tmp/I11/build_blkdbg && make -j2 -C /var/lab/out/linux-build-dbg M=$PWD modules
# (2) sdhci-pci (sdhci-uhs2는 별도 모듈 - C2에서 본 "multiple definition of init_module" 회피)
cd /var/tmp/I11/sdhci && H=$S/drivers/mmc/host
cp $H/sdhci-pci*.c $H/sdhci-pci*.h $H/sdhci.h $H/cqhci.h $H/sdhci-cqhci.h $H/sdhci-uhs2.[ch] .
printf "obj-m += i11_sdhci_uhs2.o i11_sdhci_pci.o\ni11_sdhci_uhs2-y := sdhci-uhs2.o\ni11_sdhci_pci-y := sdhci-pci-core.o sdhci-pci-o2micro.o sdhci-pci-arasan.o sdhci-pci-dwc-mshc.o sdhci-pci-gli.o\n" > Kbuild
make -j2 -C /var/lab/out/linux-build-sub M=$PWD modules
# (3) UBI 사본: wl.c의 컴파일 타임 상수 CONFIG_MTD_UBI_WL_THRESHOLD를 모듈 파라미터로
cd /var/tmp/I11/ubi && cp $S/drivers/mtd/ubi/*.[ch] .
sed -i "s|^#define UBI_WL_THRESHOLD CONFIG_MTD_UBI_WL_THRESHOLD|static int wl_threshold = CONFIG_MTD_UBI_WL_THRESHOLD;\nmodule_param(wl_threshold, int, 0444);\n#define UBI_WL_THRESHOLD wl_threshold|" wl.c
printf "obj-m += i11_ubi.o\ni11_ubi-y := vtbl.o vmt.o upd.o build.o cdev.o kapi.o eba.o io.o wl.o attach.o misc.o debug.o\n" > Kbuild
make -j2 -C /var/lab/out/linux-build-sub M=$PWD modules
modinfo /var/tmp/I11/build_blk/i11_blk.ko | grep -E "vermagic|parm"; modinfo i11_ubi.ko | grep -E "vermagic|parm: +wl"'
```

```text
  LD [M]  i11_blk.ko
  LD [M]  i11_blk.ko
  LD [M]  i11_sdhci_uhs2.o
  LD [M]  i11_sdhci_pci.o
  LD [M]  i11_sdhci_uhs2.ko
  LD [M]  i11_sdhci_pci.ko
  LD [M]  i11_ubi.o
  LD [M]  i11_ubi.ko
vermagic:       6.18.55-sub SMP preempt mod_unload aarch64
parm:           delay_us:uint
parm:           nr_hwq:uint
parm:           stack:bool
parm:           wl_threshold:int
```

(빌드 로그는 `LD` 줄만 남겼다. 경고·에러 0개를 `grep -E "warning|error"`로 확인했다. 실제로는 세 디렉터리를 따로 빌드했고 `i11_ubi.ko`의 vermagic 줄은 위 `i11_blk.ko`와 같아서 한 번만 보였다. dbg용 `i11_blk.ko`는 `6.18.55-dbg`.)

게스트 부팅 스크립트 `boot.sh`(VM에서 실행). 랩 initramfs에 이 노트의 바이너리·모듈과 `Image-sub`의 스토리지 관련 모듈 트리(`modules.dep` 포함 → BusyBox `modprobe`가 의존성을 푼다)만 넣는다.

```sh
#!/bin/bash
# usage: [KERNEL=sub|dbg] [QEXTRA="..."] [SMP=n] [TMO=s] boot.sh <guest-script.sh>
set -e
G=$1; N=$(basename $G .sh); W=/var/tmp/I11/run_$N; K=${KERNEL:-sub}
rm -rf $W && mkdir -p $W/r && cp -a /var/lab/build/rootfs/. $W/r/ && mkdir -p $W/r/i11 $W/r/lib/modules
if [ $K = sub ]; then
  M=/var/lab/out/modules-sub/lib/modules/6.18.55-sub; D=$W/r/lib/modules/6.18.55-sub
  mkdir -p $D && cp $M/modules.* $D/
  (cd $M && cp --parents -r kernel/drivers/mtd kernel/drivers/block/null_blk kernel/drivers/nvme \
     kernel/drivers/mmc kernel/fs/ubifs kernel/crypto kernel/lib/zstd $D/)
  cp /var/tmp/I11/mod/*.ko $W/r/i11/; IMG=/var/lab/out/Image-sub
else
  cp /var/tmp/I11/moddbg/*.ko $W/r/i11/; IMG=/var/lab/out/Image-dbg
fi
cp /var/tmp/I11/bin/* $W/r/i11/; cp /Users/donh/.elinux_scratch/I11/$G $W/r/i11/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout ${TMO:-300} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp ${SMP:-2} -m 1024M -nographic \
  -no-reboot -kernel $IMG -initrd $W/i.gz $QEXTRA \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/i11/t.sh" > $W/out.txt 2>&1 || echo "qemu rc=$?"
tr -d '\r' < $W/out.txt > /var/tmp/I11/out_$N.txt
```

유저 공간 C 도구는 VM에서 `gcc -static -std=gnu11 -Wall -Wextra -O2`로 빌드해(전부 경고 0개) `/var/tmp/I11/bin/`에 두었다. 게스트의 BusyBox에는 `nanddump`, `nandwrite`, `ubiattach`, `ubimkvol`은 있지만 `flash_erase`, `mtdinfo`, `fio`는 없다 — 그래서 MTD·UBI·벤치마크 도구를 직접 썼다.

---

## 1. MTD — raw flash를 커널이 보는 방식

### 1.1 직관 — "블록 장치가 아닌 저장장치"

블록 장치의 계약은 단순하다: "어떤 섹터든 언제든 덮어쓸 수 있다". raw flash는 이 계약을 지킬 수 없다.

- **덮어쓰기 불가**: NAND/NOR는 비트를 1 → 0으로만 바꿀 수 있다(program). 0 → 1은 **erase block 전체**를 지울 때만(erase) 가능하다.
- **단위가 셋**: 읽기·쓰기는 page(NAND 2~16 KiB, NOR는 바이트), 지우기는 erase block(64 KiB~수 MiB).
- **OOB**: NAND page마다 spare 영역(64 B~수백 B)이 붙어 ECC와 배드 블록 마커를 담는다.
- **배드 블록**: 공장 출하 때부터, 그리고 쓰면서 늘어난다.
- **수명**: erase 횟수 제한(SLC 수만~10만, MLC/TLC 수천).

그래서 Linux는 raw flash를 블록 장치로 위장하지 않고 **별도의 계층 MTD**로 둔다. MTD는 "erase, read, write, read_oob, write_oob, block_isbad, block_markbad"라는 **flash 본연의 동작**을 그대로 노출하는 얇은 추상화다. Don의 펌웨어에서 FTL 아래에 있던 "NAND HAL"과 같은 자리다.

### 1.2 정의 — `struct mtd_info`와 ops

무엇을 확인하나: 랩 커널 소스에서 MTD 장치 하나를 대표하는 `struct mtd_info`(include/linux/mtd/mtd.h:238)의 함수 포인터와, raw NAND 컨트롤러 드라이버가 채우는 `struct nand_controller_ops`(rawnand.h:1097), ECC 엔진 종류, 그리고 MMC 호스트 드라이버가 채우는 `struct mmc_host_ops`(host.h:161)를 한 번에 본다.

```sh
limactl shell elinux -- bash -lc 'S=/var/lab/src/linux-6.18.55
grep -nE "^\s+int \(\*_(erase|read|write|read_oob|write_oob|block_isbad|block_markbad|point|lock|sync)\)" $S/include/linux/mtd/mtd.h
grep -nE "^\s+(int|void) \(\*(exec_op|attach_chip|setup_interface)\)" $S/include/linux/mtd/rawnand.h
grep -nE "NAND_ECC_ENGINE_TYPE_(NONE|SOFT|ON_HOST|ON_DIE)," $S/include/linux/mtd/nand.h
grep -nE "^\s+(void|int)\s+\(\*(request|set_ios|get_ro|get_cd|execute_tuning|card_busy|start_signal_voltage_switch)\)" $S/include/linux/mmc/host.h'
```

```text
316:	int (*_erase) (struct mtd_info *mtd, struct erase_info *instr);
317:	int (*_point) (struct mtd_info *mtd, loff_t from, size_t len,
320:	int (*_read) (struct mtd_info *mtd, loff_t from, size_t len,
322:	int (*_write) (struct mtd_info *mtd, loff_t to, size_t len,
326:	int (*_read_oob) (struct mtd_info *mtd, loff_t from,
328:	int (*_write_oob) (struct mtd_info *mtd, loff_t to,
348:	int (*_lock) (struct mtd_info *mtd, loff_t ofs, uint64_t len);
352:	int (*_block_isbad) (struct mtd_info *mtd, loff_t ofs);
353:	int (*_block_markbad) (struct mtd_info *mtd, loff_t ofs);
1098:	int (*attach_chip)(struct nand_chip *chip);
1100:	int (*exec_op)(struct nand_chip *chip,
1103:	int (*setup_interface)(struct nand_chip *chip, int chipnr,
147:	NAND_ECC_ENGINE_TYPE_NONE,
148:	NAND_ECC_ENGINE_TYPE_SOFT,
149:	NAND_ECC_ENGINE_TYPE_ON_HOST,
150:	NAND_ECC_ENGINE_TYPE_ON_DIE,
173:	void	(*request)(struct mmc_host *host, struct mmc_request *req);
192:	void	(*set_ios)(struct mmc_host *host, struct mmc_ios *ios);
201:	int	(*get_ro)(struct mmc_host *host);
210:	int	(*get_cd)(struct mmc_host *host);
219:	int	(*start_signal_voltage_switch)(struct mmc_host *host, struct mmc_ios *ios);
222:	int	(*card_busy)(struct mmc_host *host);
225:	int	(*execute_tuning)(struct mmc_host *host, u32 opcode);
```

출력에서 볼 것:

- `mtd_info`의 ops 이름 앞에 `_`이 붙어 있다. 드라이버는 `_erase`를 채우고, 사용자(UBI, mtdchar, mtdblock)는 **래퍼 `mtd_erase()`, `mtd_read()`, `mtd_block_isbad()`** 를 부른다. 래퍼가 범위 검사·파티션 오프셋·통계(ECC stats)를 처리한다. 드라이버를 쓸 때 `_` 붙은 필드를 직접 부르지 않는다.
- `mtd_info`의 필드 중 핵심은 `type`(MTD_NORFLASH, MTD_NANDFLASH …), `flags`(MTD_WRITEABLE, MTD_BIT_WRITEABLE …), `size`, `erasesize`, `writesize`(최소 쓰기 단위 = NAND page), `oobsize`, `oobavail`, `ecc_strength`, `ecc_step_size`, `bitflip_threshold`, `ecc_stats`다. 아래 예제에서 전부 sysfs로 보인다.
- raw NAND 컨트롤러 드라이버는 `nand_chip`을 하나 만들고 `nand_controller_ops.exec_op`만 구현하면 된다. `exec_op`은 "명령 사이클, 주소 사이클, 데이터 in/out, R/B 대기"로 쪼갠 **NAND 연산 명령어 목록(`struct nand_operation`)** 을 받아 컨트롤러 HW에 흘려 보낸다. READ ID, READ PAGE, PROGRAM, ERASE의 프로토콜 순서는 코어(`nand_base.c`)가 안다. 예전 커널의 `cmdfunc`/`cmd_ctrl` 방식은 레거시다.
- ECC 엔진 4종: **NONE**(ECC 없음 — 칩이 알아서 하거나 테스트용), **SOFT**(CPU가 Hamming/BCH 계산 — nandsim 기본), **ON_HOST**(SoC NAND 컨트롤러의 HW BCH — 양산 보드 대부분), **ON_DIE**(칩 내부 ECC — SPI-NAND와 일부 parallel NAND). DT 속성 `nand-ecc-engine`, `nand-ecc-strength`, `nand-ecc-step-size`로 고른다.

### 1.3 NOR vs NAND vs SPI 변형

| 항목 | parallel NOR (CFI) | SPI-NOR | raw parallel NAND | SPI-NAND |
|---|---|---|---|---|
| 커널 드라이버 계층 | `drivers/mtd/chips` (cfi_cmdset_0001/0002) + map | `drivers/mtd/spi-nor` (SFDP로 파라미터 자동 파악) | `drivers/mtd/nand/raw` (nand_base + 컨트롤러 드라이버) | `drivers/mtd/nand/spi` (spinand + SPI-MEM) |
| 읽기 | 메모리처럼 랜덤(XIP 가능) | 랜덤, 빠름 | page 단위 | page 단위(칩 캐시로 로드 후 읽기) |
| 쓰기/지우기 단위 | 바이트 / 64~256 KiB 섹터 | page 256 B / 4 KiB·64 KiB | 2~16 KiB page / 128 KiB~수 MiB block | 2~4 KiB page / 128 KiB block |
| OOB · ECC | 없음 | 없음 | 있음, 호스트 ECC 필요 | 있음, 대개 on-die ECC |
| 배드 블록 | 없음(보장) | 없음 | 있음 | 있음 |
| `mtd->type` | MTD_NORFLASH | MTD_NORFLASH | MTD_NANDFLASH / MTD_MLCNANDFLASH | MTD_NANDFLASH |
| 흔한 용도 | 구형 보드, QEMU `virt`의 pflash | 부트로더·DT·환경변수 | 저가 대용량 rootfs (UBI) | 저가 보드 rootfs (UBI) |

QEMU `virt` 머신은 CFI NOR(pflash) 두 개를 달고 나온다. 그래서 아래 실험의 `mtd0`은 늘 `"0.flash"`(128 MiB CFI NOR)이고, 우리가 올린 시뮬레이터는 `mtd1`부터 번호를 받는다.

### 1.4 예제 2 — mtdram과 nandsim을 올리고 `/proc/mtd`, `/sys/class/mtd` 보기

무엇을 확인하나: RAM을 흉내 내는 `mtdram`(1 MiB, erase 64 KiB)과 NAND를 흉내 내는 `nandsim`(Samsung ID `0xec 0xf1` = 2 KiB page / 64 B OOB / 128 KiB block, `overridesize=6` → 64 블록 = 8 MiB, block 3을 공장 배드 블록으로, 첫 16블록을 파티션 0으로)을 올리고, MTD가 같은 인터페이스로 보이는지.

게스트 스크립트 `g_mtd.sh` 앞부분:

```sh
cd /i11
echo "### modprobe"
modprobe mtdram total_size=1024 erase_size=64
modprobe nandsim id_bytes=0xec,0xf1,0x00,0x95 overridesize=6 badblocks=3 parts=16
cat /proc/mtd
echo "### sysfs mtd1"
for a in type name size erasesize writesize oobsize subpagesize ecc_strength ecc_step_size bitflip_threshold bad_blocks bbt_blocks flags; do printf "%-17s %s\n" $a "$(cat /sys/class/mtd/mtd1/$a)"; done
ls --color=never /sys/class/mtd/; ls --color=never /sys/class/mtd/mtd2/; ls -l --color=never /dev/mtd2 /dev/mtd2ro
```

```text
lab initramfs: 6.18.55-sub up 1.32s
### modprobe
dev:    size   erasesize  name
mtd0: 08000000 00040000 "0.flash"
mtd1: 00100000 00010000 "mtdram test device"
mtd2: 00200000 00020000 "NAND simulator partition 0"
mtd3: 00600000 00020000 "NAND simulator partition 1"
### sysfs mtd1
type              ram
name              mtdram test device
size              1048576
erasesize         65536
writesize         1
oobsize           0
subpagesize       1
ecc_strength      0
ecc_step_size     0
bitflip_threshold 0
bad_blocks        0
bbt_blocks        0
flags             0x1c00
mtd0    mtd0ro  mtd1    mtd1ro  mtd2    mtd2ro  mtd3    mtd3ro
bad_blocks         ecc_step_size      numeraseregions    subpagesize
bbt_blocks         ecc_strength       offset             subsystem
bitflip_threshold  erasesize          oobavail           type
corrected_bits     flags              oobsize            uevent
dev                mtd2               power              writesize
ecc_failures       name               size
crw-------    1 0        0          90,   4 Oct 11 23:14 /dev/mtd2
crw-------    1 0        0          90,   5 Oct 11 23:14 /dev/mtd2ro
```

출력에서 볼 것:

- `/proc/mtd`의 `erasesize` 열: NOR 256 KiB, mtdram 64 KiB, nandsim 128 KiB. nandsim은 `parts=16`으로 **파티션 두 개**(앞 16블록 = 2 MiB, 나머지 48블록 = 6 MiB)가 됐다. MTD 파티션은 그 자체가 또 하나의 `mtd_info`다(부모 오프셋만 더해 준다).
- mtdram의 `writesize 1`, `oobsize 0`: RAM/NOR는 바이트 단위로 쓸 수 있고 OOB가 없다. `flags 0x1c00` = `MTD_WRITEABLE(0x400) + MTD_BIT_WRITEABLE(0x800) + MTD_NO_ERASE(0x1000)` — mtdram은 "erase가 필요 없는 장치"라고 스스로 밝힌다(아래 예제 4에서 그 결과를 본다).
- `/dev/mtd2`는 **문자 장치**(`c`, major 90)이고 minor가 짝수(rw)·홀수(`mtd2ro`, 읽기 전용) 쌍으로 붙는다. `/dev/mtdblockN`(major 31)은 `mtdblock` 모듈을 올려야 생긴다(§2).
- sysfs 속성 이름이 곧 `mtd_info` 필드 이름이다: `ecc_strength`, `ecc_step_size`, `bitflip_threshold`, `corrected_bits`, `ecc_failures`, `bad_blocks`, `bbt_blocks`.

### 1.5 예제 3 — `MEMGETINFO` · `MEMGETBADBLOCK` · `ECCGETSTATS` · `MEMSETBADBLOCK`

무엇을 확인하나: `mtd-utils`의 `mtdinfo`, `nanddump`가 내부에서 쓰는 mtdchar ioctl을 직접 불러 장치 정보, 블록별 배드 여부, ECC 통계를 읽고, 블록 하나를 배드로 표시한다(`mtd_block_markbad()` → NAND는 OOB의 BBM 바이트에 0을 쓴다).

```c
/* i11_mtdinfo: MEMGETINFO + ECCGETSTATS + MEMGETBADBLOCK scan on /dev/mtdN */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <mtd/mtd-user.h>

static const char *tname(unsigned t)
{
	switch (t) {
	case MTD_RAM: return "RAM";       case MTD_NORFLASH: return "NOR";
	case MTD_NANDFLASH: return "NAND"; case MTD_MLCNANDFLASH: return "MLC-NAND";
	default: return "other";
	}
}

int main(int argc, char **argv)
{
	struct mtd_info_user mi;
	struct mtd_ecc_stats st;
	int fd, nbad = 0;

	if (argc < 2) { fprintf(stderr, "usage: %s /dev/mtdN [markbad_blk]\n", argv[0]); return 1; }
	fd = open(argv[1], O_RDWR);
	if (fd < 0) { perror("open"); exit(1); }
	if (ioctl(fd, MEMGETINFO, &mi)) { perror("MEMGETINFO"); exit(1); }
	printf("%s: type=%s size=%u erasesize=%u writesize=%u oobsize=%u flags=%s%s\n",
	       argv[1], tname(mi.type), mi.size, mi.erasesize, mi.writesize, mi.oobsize,
	       (mi.flags & MTD_WRITEABLE) ? "WRITEABLE " : "",
	       (mi.flags & MTD_BIT_WRITEABLE) ? "BIT_WRITEABLE" : "NO_BIT_WRITE(=must erase)");
	if (argc > 2) {				/* MEMSETBADBLOCK = mtd_block_markbad() */
		loff_t o = (loff_t)atoi(argv[2]) * mi.erasesize;
		if (ioctl(fd, MEMSETBADBLOCK, &o)) perror("MEMSETBADBLOCK");
		else printf("marked block %s bad\n", argv[2]);
	}
	for (unsigned b = 0; b < mi.size / mi.erasesize; b++) {
		loff_t o = (loff_t)b * mi.erasesize;
		int r = ioctl(fd, MEMGETBADBLOCK, &o);	/* 1 = bad, 0 = good, <0 = no BBM */
		if (r < 0) { printf("MEMGETBADBLOCK not supported (no bad blocks on this type)\n"); break; }
		if (r > 0) { printf("  block %u (offset 0x%llx) is BAD\n", b, (unsigned long long)o); nbad++; }
	}
	printf("bad blocks: %d of %u\n", nbad, mi.size / mi.erasesize);
	if (ioctl(fd, ECCGETSTATS, &st) == 0)
		printf("ecc stats: corrected=%u failed=%u badblocks=%u bbtblocks=%u\n",
		       st.corrected, st.failed, st.badblocks, st.bbtblocks);
	close(fd);
	return 0;
}
```

```text
### info
/dev/mtd1: type=RAM size=1048576 erasesize=65536 writesize=1 oobsize=0 flags=WRITEABLE BIT_WRITEABLE
bad blocks: 0 of 16
ecc stats: corrected=0 failed=0 badblocks=0 bbtblocks=0
/dev/mtd2: type=NAND size=2097152 erasesize=131072 writesize=2048 oobsize=64 flags=WRITEABLE NO_BIT_WRITE(=must erase)
  block 3 (offset 0x60000) is BAD
bad blocks: 1 of 16
ecc stats: corrected=0 failed=0 badblocks=1 bbtblocks=0
```

그리고 같은 부팅의 마지막에 block 5를 배드로 표시한 뒤 다시 실행(`./i11_mtdinfo /dev/mtd2 5`, `cat bad_blocks`, `./i11_mtdrw /dev/mtd2 erase 5`):

```text
### markbad block 5 of mtd2
/dev/mtd2: type=NAND size=2097152 erasesize=131072 writesize=2048 oobsize=64 flags=WRITEABLE NO_BIT_WRITE(=must erase)
marked block 5 bad
  block 3 (offset 0x60000) is BAD
  block 5 (offset 0xa0000) is BAD
bad blocks: 2 of 16
ecc stats: corrected=2 failed=6 badblocks=2 bbtblocks=0
2
MEMERASE: Input/output error
```

출력에서 볼 것:

- NAND(mtd2)만 `NO_BIT_WRITE(=must erase)`다. `MTD_BIT_WRITEABLE`이 없다는 것은 "이미 쓴 자리에 비트 단위로 덧쓸 수 없다"는 뜻이다. NOR와 RAM은 이 플래그가 있다.
- mtdram은 `MEMGETBADBLOCK`을 부르면 0(좋음)을 돌려준다 — 배드 블록 개념이 없는 장치는 MTD 코어가 "전부 좋음"으로 답한다(`_block_isbad`가 없으면 0).
- 배드로 표시한 block 5는 이후 **MTD 코어가 erase를 거부**한다(`MEMERASE: Input/output error`). 이것이 공장 배드 블록(block 3)과 런타임 배드 블록이 똑같이 취급되는 지점이다. Don의 펌웨어에서 "grown bad block을 BBT에 추가"한 것과 같다. 차이점: 여기서는 표시가 칩 OOB(또는 flash-based BBT)에 영구히 남고, **누가 그 블록을 피해 다닐지는 MTD 위의 계층**(UBI, nandwrite의 skip-bad, U-Boot)이 결정한다. MTD 자체는 리매핑하지 않는다.
- `ecc stats: corrected=2 failed=6`은 바로 다음 예제에서 만든 오류가 누적된 값이다(같은 부팅).

### 1.6 예제 4 — erase 없이 두 번 쓰면? (NAND AND 의미론과 ECC)

무엇을 확인하나: Don이 펌웨어에서 절대 하지 않았던 일 — 같은 page를 erase 없이 두 번 program — 을 일부러 해서 (1) 데이터가 AND 된다는 것, (2) ECC가 깨진다는 것, (3) **mtdchar의 `read()`는 ECC 실패에도 데이터를 돌려준다**는 것을 본다. 앞 16바이트는 패턴 그대로, 나머지는 패턴 XOR 해시값으로 채운다(전부 같은 바이트면 Hamming ECC가 `ff ff ff`가 되어 실험이 안 된다 — 아래 설명).

```c
/* i11_mtdrw: erase / program one page / read page+OOB on /dev/mtdN (raw MTD semantics) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <mtd/mtd-user.h>

int main(int argc, char **argv)
{
	struct mtd_info_user mi;
	static uint8_t buf[4096], oob[256];
	int fd;

	if (argc < 4) { fprintf(stderr, "usage: %s /dev/mtdN erase|write|read blk [byte]\n", argv[0]); return 1; }
	fd = open(argv[1], O_RDWR);
	if (fd < 0 || ioctl(fd, MEMGETINFO, &mi)) { perror(argv[1]); exit(1); }
	off_t off = (off_t)atoi(argv[3]) * mi.erasesize;	/* first page of the block */
	if (!strcmp(argv[2], "erase")) {
		struct erase_info_user e = { .start = off, .length = mi.erasesize };
		if (ioctl(fd, MEMERASE, &e)) { perror("MEMERASE"); exit(1); }
		printf("erase blk %s: ok\n", argv[3]);
	} else if (!strcmp(argv[2], "write")) {
		unsigned len = mi.writesize < 16 ? 16 : mi.writesize, pat = strtol(argv[4], NULL, 0);
		for (unsigned i = 0; i < len; i++)	/* 16 B of 'pat', then pat^hash(i) so ECC != ff */
			buf[i] = (uint8_t)(i < 16 ? pat : pat ^ ((i * 2654435761u) >> 24));
		ssize_t n = pwrite(fd, buf, len, off);
		printf("write 0x%02x.. x %u at 0x%llx: %zd%s%s\n", buf[0], len,
		       (unsigned long long)off, n, n < 0 ? " " : "", n < 0 ? strerror(errno) : "");
	} else {
		struct mtd_ecc_stats a, b;
		ioctl(fd, ECCGETSTATS, &a);
		ssize_t n = pread(fd, buf, mi.writesize < 16 ? 16 : mi.writesize, off);
		int e = errno;
		ioctl(fd, ECCGETSTATS, &b);
		printf("read 0x%llx: ret=%zd%s%s data[0..3]=%02x %02x %02x %02x  "
		       "ecc corrected +%u failed +%u\n", (unsigned long long)off, n,
		       n < 0 ? " " : "", n < 0 ? strerror(e) : "", buf[0], buf[1], buf[2], buf[3],
		       b.corrected - a.corrected, b.failed - a.failed);
		if (mi.oobsize) {		/* MEMREAD: data + OOB in one request (mtd_read_oob) */
			struct mtd_read_req r = { .start = off, .len = 0, .ooblen = mi.oobsize,
				.usr_oob = (uintptr_t)oob, .mode = MTD_OPS_RAW };
			if (ioctl(fd, MEMREAD, &r)) { perror("MEMREAD"); exit(1); }
			printf("  raw OOB:");
			for (unsigned i = 0; i < mi.oobsize; i++)
				printf("%s%02x", i % 16 ? " " : "\n   ", oob[i]);
			printf("\n");
		}
	}
	close(fd);
	return 0;
}
```

```text
### NAND write-without-erase (mtd2 = nandsim part 1, block 0)
erase blk 0: ok
read 0x0: ret=2048 data[0..3]=ff ff ff ff  ecc corrected +0 failed +0
write 0xf0.. x 2048 at 0x0: 2048
read 0x0: ret=2048 data[0..3]=f0 f0 f0 f0  ecc corrected +0 failed +0
  raw OOB:
   ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
   ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
   ff ff ff ff ff ff ff ff aa 99 67 55 66 ab cf fc
   ff cf 03 33 a5 96 ab f3 0f 0f 30 3c 0f 66 aa a7
write 0x3c.. x 2048 at 0x0: 2048
read 0x0: ret=2048 data[0..3]=30 30 30 30  ecc corrected +2 failed +6
  raw OOB:
   ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
   ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
   ff ff ff ff ff ff ff ff aa 99 67 55 66 ab cf fc
   ff cf 03 33 a5 96 ab f3 0f 0f 30 3c 0f 66 aa a7
[    1.875388] ecc_sw_hamming_correct: uncorrectable ECC error
[    1.875446] ecc_sw_hamming_correct: uncorrectable ECC error
[    1.875452] ecc_sw_hamming_correct: uncorrectable ECC error
[    1.875456] ecc_sw_hamming_correct: uncorrectable ECC error
[    1.875458] ecc_sw_hamming_correct: uncorrectable ECC error
[    1.875493] ecc_sw_hamming_correct: uncorrectable ECC error
[    1.875456] ecc_sw_hamming_correct: uncorrectable ECC error
[    1.875458] ecc_sw_hamming_correct: uncorrectable ECC error
[    1.875493] ecc_sw_hamming_correct: uncorrectable ECC error
2
6
### erase again then write
erase blk 0: ok
write 0x3c.. x 2048 at 0x0: 2048
read 0x0: ret=2048 data[0..3]=3c 3c 3c 3c  ecc corrected +0 failed +0
### mtdram (RAM-backed MTD) write-without-erase
erase blk 0: ok
write 0xf0.. x 16 at 0x0: 16
write 0x3c.. x 16 at 0x0: 16
read 0x0: ret=16 data[0..3]=3c 3c 3c 3c  ecc corrected +0 failed +0
```

(`ecc_sw_hamming_correct` 줄 9개 중 6개는 KERN_ERR라서 커널이 시리얼 콘솔에 직접 찍은 것이고, 마지막 3개가 `dmesg | tail -3` 출력이다. 마지막 두 숫자는 sysfs `corrected_bits`와 `ecc_failures`.)

출력에서 볼 것:

- **erase 직후 읽기는 `ff`**, 0xf0으로 쓰고 다시 0x3c로 쓰니 `30` = `0xf0 & 0x3c`. NAND program은 0인 비트만 만들 수 있다. nandsim은 이것을 `pg_off[i] &= buf[i]`로 그대로 흉내 낸다(nandsim.c:1521).
- **OOB 40~63번째 바이트가 ECC**(256 B step × 8 = 24 B, SW Hamming)이고, 두 번째 쓰기 후에도 **ECC 바이트가 첫 번째와 똑같다**. 우연이 아니다: Hamming은 선형 코드라서 256 B 조각의 모든 바이트에 같은 상수(0xf0 ^ 0x3c)를 XOR 해도 짝수 개 바이트의 패리티는 변하지 않는다. 같은 이유로 "전부 같은 바이트"인 page의 ECC는 지워진 page와 같은 `ff ff ff`가 되어, 첫 시도에서는 오류가 전혀 안 보였다. (실제 BCH/LDPC 엔진도 erased page 검출 때문에 비슷한 함정이 있다 — Don에게 익숙한 이야기.)
- 데이터는 AND 됐는데 ECC는 그대로이니 읽을 때 **8개 step 중 6개가 정정 불가(`failed +6`), 2개는 "정정"**(`corrected +2`) — 다중 비트 오류를 Hamming이 1비트 오류로 오인해 **엉뚱한 비트를 고친 것(miscorrection)** 이다. 정정 능력을 넘는 오류에서 ECC가 조용히 틀린 데이터를 만드는 사례다.
- 그런데 `read()`의 반환값은 `2048`(성공)이다. mtdchar.c는 의도적으로 이렇게 한다: "Nand returns -EBADMSG on ECC errors, but it returns the data. For our userspace tools it is important to dump areas with ECC errors!"(mtdchar.c:183 주석). **유저 공간에서 raw NAND를 읽는 도구는 반드시 `ECCGETSTATS` 전후 비교로 오류를 확인해야 한다.** 커널 내부 사용자(UBI)는 `mtd_read()`의 반환값 `-EBADMSG`(정정 불가)·`-EUCLEAN`(정정했지만 비트 플립이 `bitflip_threshold` 이상)을 직접 받는다.
- erase 후 다시 쓰면 정상(`3c 3c 3c 3c`, 오류 0).
- **mtdram은 덮어쓰기가 그냥 된다**(`3c`). `ram_write()`가 `memcpy`일 뿐이다. 그래서 mtdram은 MTD 위 소프트웨어의 기능 테스트용이지 flash 의미론 테스트용이 아니다. raw flash 동작을 검증하려면 nandsim을 써야 한다.

함정: 이 실험에서 본 것처럼 MTD 계층은 "erase 없이 쓰기"를 **막지 않는다**. 그 규칙을 지키는 것은 위 계층(UBI, JFFS2, mtdblock)의 책임이다. U-Boot에서 `nand write` 전에 `nand erase`를 빼먹으면 이와 똑같이 조용히 깨진 이미지가 만들어진다.

### 1.7 raw NAND 컨트롤러 드라이버의 모양 (설명용 뼈대, 빌드하지 않음)

SoC의 NAND 컨트롤러 드라이버(예: `drivers/mtd/nand/raw/marvell_nand.c`, `denali.c`, `stm32_fmc2_nand.c`)는 대략 이렇게 생겼다. 아래는 구조를 보이기 위한 **기억 기반 축약 뼈대**로, 이 노트에서 빌드·실행하지 않았다.

```c
/* sketch only - not built. Real examples: drivers/mtd/nand/raw/{denali,marvell_nand,stm32_fmc2_nand}.c */
static int foo_exec_op(struct nand_chip *chip, const struct nand_operation *op, bool check_only)
{
	/* walk op->instrs[]: NAND_OP_CMD_INSTR / ADDR / DATA_IN / DATA_OUT / WAITRDY
	 * and program the controller FIFO / sequencer; return -ENOTSUPP if check_only
	 * and the pattern can't be done by this HW */
}
static int foo_attach_chip(struct nand_chip *chip)	/* called after READ ID / ONFI param page */
{
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;	/* HW BCH */
	chip->ecc.size = 1024; chip->ecc.strength = 24;	/* or from DT nand-ecc-* */
	chip->ecc.read_page = foo_read_page_hwecc;		/* DMA + HW ECC status */
	chip->ecc.write_page = foo_write_page_hwecc;
	mtd_set_ooblayout(nand_to_mtd(chip), &foo_ooblayout_ops);
	return 0;
}
static const struct nand_controller_ops foo_ops = { .attach_chip = foo_attach_chip, .exec_op = foo_exec_op };
/* probe: map regs/clk/irq, nand_controller_init(&ctrl); ctrl.ops = &foo_ops;
 * for each DT child "nand@0": nand_set_flash_node(chip, np); nand_scan(chip, 1);
 * mtd_device_register(nand_to_mtd(chip), NULL, 0);   <- parses DT partitions */
```

Don의 펌웨어 관점에서 읽으면: `exec_op`이 "NAND 시퀀서 마이크로코드", `attach_chip`이 "칩 ID를 보고 ECC 설정을 고르는 초기화", `ecc.read_page`가 "DMA + ECC 디코더 결과 확인"이다. `nand_scan()`이 READ ID → ONFI/JEDEC 파라미터 페이지 → 타이밍 설정 → 배드 블록 스캔(또는 BBT 로드)을 해 주고, `mtd_device_register()`가 MTD 장치와 파티션을 만든다.

SPI-NOR와 SPI-NAND는 이 대신 **SPI-MEM** 계층(`drivers/spi/spi-mem.c`) 위에 선다. SoC의 QSPI/OSPI 컨트롤러 드라이버가 `spi_controller_mem_ops.exec_op`(1-1-4, 1-4-4, 8D-8D-8D 같은 명령 형식)를 구현하면, 칩 쪽 로직(`spi-nor/core.c`, `nand/spi/core.c`)은 그대로 재사용된다.

### 1.8 MTD 파티션 — DT와 커맨드라인

MTD 파티션은 세 가지 방법으로 정의한다. 우선순위는 `mtd_device_parse_register()`의 파서 목록 순서(기본 `cmdlinepart`, `ofpart`)다.

```dts
&nand {
	nand@0 {
		reg = <0>;
		nand-ecc-engine = <&nand>;     /* on-host ECC (controller node itself) */
		nand-ecc-strength = <8>;
		nand-ecc-step-size = <512>;
		nand-on-flash-bbt;
		partitions {
			compatible = "fixed-partitions";
			#address-cells = <1>;
			#size-cells = <1>;
			partition@0       { label = "spl";    reg = <0x0 0x80000>; read-only; };
			partition@80000   { label = "uboot";  reg = <0x80000 0x200000>; };
			partition@280000  { label = "ubi";    reg = <0x280000 0x7d80000>; };
		};
	};
};
```

커맨드라인 방식: `mtdparts=<mtd-id>:512k(spl)ro,2m(uboot),-(ubi)` (`CONFIG_MTD_CMDLINE_PARTS` 필요 — 랩 `Image-sub`에서는 꺼져 있어서 실행하지 않았다). U-Boot의 `mtdparts` 변수를 커널에 넘기는 보드가 많다. 위 DT 조각은 128 MiB NAND를 가정한 설명용이며 이 노트에서 부팅에 쓰지 않았다.

---

## 2. mtdblock — 그리고 "raw NAND에 ext4를 올리면 안 되는 이유"

### 2.1 직관

`mtdblock`은 MTD를 **블록 장치처럼 보이게 하는 얇은 어댑터**다. 512 B 섹터 쓰기가 오면 그 섹터가 속한 **erase block 전체를 읽어 RAM 캐시에 두고**, 섹터를 고친 뒤, 캐시를 비울 때 **erase block을 지우고 통째로 다시 쓴다**(read-modify-erase-write). 배드 블록도, wear leveling도, 전원 차단 보호도 없다.

### 2.2 예제 5 — 512 B 쓰기 한 번이 flash에 무엇을 하나

무엇을 확인하나: nandsim의 `log=1` + dynamic debug(`pr_debug`)로 칩이 실제로 받은 명령을 세어, `/dev/mtdblock1`에 512 B를 쓸 때와 `/dev/mtd1`에 직접 2 KiB page를 쓸 때를 비교한다.

```sh
cd /i11
modprobe nandsim id_bytes=0xec,0xf1,0x00,0x95 overridesize=6 log=1
echo "module nandsim +p" > /sys/kernel/debug/dynamic_debug/control
modprobe mtdblock
ls -l --color=never /dev/mtdblock1; cat /sys/block/mtdblock1/queue/logical_block_size /sys/block/mtdblock1/queue/max_hw_sectors_kb
dmesg -c > /dev/null
echo "### 512 B write to mtdblock1 at offset 4096, then sync"
dd if=/dev/zero of=/dev/mtdblock1 bs=512 seek=8 count=1 conv=fsync 2>&1 | tail -1
echo "erases:   $(dmesg | grep -c 'erase sector')"
echo "programs: $(dmesg | grep -c 'programm page')"
echo "reads:    $(dmesg | grep -c 'read page')"
dmesg | grep -E "erase sector|programm page" | sed 's/^\[[ 0-9.]*\] //' | head -3
dmesg -c > /dev/null
echo "### same 512 B through /dev/mtd1 (raw, page-aligned 2 KiB, pre-erased block 1)"
./i11_mtdrw /dev/mtd1 erase 1 >/dev/null; dmesg -c >/dev/null
./i11_mtdrw /dev/mtd1 write 1 0x00
echo "erases:   $(dmesg | grep -c 'erase sector')  programs: $(dmesg | grep -c 'programm page')"
```

```text
lab initramfs: 6.18.55-sub up 0.84s
brw-------    1 0        0          31,   1 Oct 11 23:16 /dev/mtdblock1
512
127
### 512 B write to mtdblock1 at offset 4096, then sync
512 bytes (512B) copied, 0.019633 seconds, 25.5KB/s
erases:   1
programs: 64
reads:    80
[nandsim] log: erase sector 0
[nandsim] log: programm page 0
[nandsim] log: programm page 1
### same 512 B through /dev/mtd1 (raw, page-aligned 2 KiB, pre-erased block 1)
write 0x00.. x 2048 at 0x20000: 2048
erases:   0  programs: 1
```

출력에서 볼 것:

- **512 B 쓰기 → erase 1회 + page program 64회(= 128 KiB 블록 전체) + page read 80회.** 쓰기 증폭(write amplification) 256배. Don이 SSD에서 "호스트 4 KiB 랜덤 쓰기의 WAF"를 1.x로 만들려고 GC·오버프로비저닝을 설계한 것과 정반대 상황이다.
- 그리고 erase와 program 사이에 전원이 나가면 그 블록의 **다른 127 KiB까지 함께 사라진다**. ext4의 저널은 "섹터 쓰기는 원자적이고 다른 섹터를 건드리지 않는다"를 가정하므로 이 가정이 깨진다.
- 같은 데이터를 `/dev/mtd1`에 page 단위로 쓰면 program 1회로 끝난다. flash를 아는 계층(UBI/UBIFS, JFFS2)이 필요한 이유다.

그래서 규칙은 이렇다.

| 매체 | ext4/F2FS 직접 | 올바른 구성 |
|---|---|---|
| raw NAND, SPI-NAND | **금지** (mtdblock 경유도 금지) | UBI + UBIFS, 읽기 전용이면 UBI + `ubiblock` + squashfs |
| NOR (작은 용량) | 금지 | JFFS2 또는 읽기 전용 squashfs(mtdblock은 **읽기 전용**일 때만 무난) |
| eMMC, SD, UFS, NVMe | 가능 (장치 FTL이 처리) | ext4/F2FS, 읽기 전용 루트는 squashfs/EROFS |

---

## 3. UBI — Linux 안에 들어간 FTL

### 3.1 직관 — Don의 FTL을 커널로 옮기면

UBI(Unsorted Block Images)는 MTD 장치 하나(보통 파티션 하나)를 받아서, 그 위에 **볼륨**(논리 장치)을 여러 개 만든다. 볼륨은 **LEB**(logical eraseblock)의 배열이고, UBI가 LEB를 **PEB**(physical eraseblock)에 동적으로 매핑한다. 쓰기가 오면 빈 PEB를 골라(낮은 erase counter 우선) 쓰고 매핑을 바꾸며, 옛 PEB는 백그라운드에서 지운다. 이것은 SSD FTL의 L2P 매핑과 똑같은 발상이다. 다만 단위가 page가 아니라 **erase block 전체**(블록 단위 매핑)다 — page 단위 매핑과 GC는 그 위의 UBIFS가 한다.

UBI의 상태는 **각 PEB 안에** 들어 있다. 매핑 테이블을 별도의 메타데이터 영역에 두지 않는다.

- **EC 헤더**(Erase Counter, 64 B, PEB의 첫 page 맨 앞): 매직 `UBI#`(0x55424923), 그 PEB의 **erase 횟수**, VID 헤더 오프셋, 데이터 오프셋, image sequence number, CRC. PEB를 지울 때마다 UBI가 EC+1을 새로 쓴다.
- **VID 헤더**(Volume ID, 64 B, 보통 두 번째 min I/O 단위 또는 sub-page): 매직 `UBI!`(0x55424921), **어느 볼륨의 몇 번째 LEB인지**(`vol_id`, `lnum`), 전역 증가 **시퀀스 번호 `sqnum`**, 데이터 CRC, `copy_flag`. 매핑된 PEB에만 있다.
- **attach 때 전체 스캔**: 부팅 시 UBI는 모든 PEB의 EC·VID 헤더를 읽어 EBA(eraseblock association) 테이블을 RAM에 재구성한다. 같은 LEB를 가리키는 PEB가 둘이면 `sqnum`이 큰 쪽이 이긴다(전원 차단 중 LEB 이동의 복구). 큰 NAND에서는 이 스캔이 느려서 **fastmap**(매핑 스냅샷을 flash에 저장, `CONFIG_MTD_UBI_FASTMAP`)을 쓴다 — 랩 커널에서는 꺼져 있다.
- **layout volume**: 볼륨 테이블(이름, 크기, 타입)은 내부 볼륨 `vol_id = 0x7fffefff`의 LEB 0·1에 이중으로 저장된다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="20" font-size="13" font-weight="bold">UBIFS: 파일·인덱스 노드를 LEB에 로그처럼 쓴다</text>
  <text x="20" y="44" font-size="13">볼륨 "data" (vol_id 0)</text>
  <rect x="180" y="30" width="60" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="210" y="48" text-anchor="middle" font-size="12">LEB 0</text>
  <rect x="245" y="30" width="60" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="275" y="48" text-anchor="middle" font-size="12">LEB 1</text>
  <rect x="310" y="30" width="60" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340" y="48" text-anchor="middle" font-size="12">LEB 6</text>
  <rect x="375" y="30" width="60" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="405" y="48" text-anchor="middle" font-size="12">LEB 10</text>
  <rect x="440" y="30" width="60" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="470" y="48" text-anchor="middle" font-size="12">LEB 11</text>
  <text x="510" y="48" font-size="12">… (121개)</text>
  <rect x="20" y="78" width="640" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="96" text-anchor="middle" font-size="13">UBI EBA 테이블 (RAM): LEB → PEB, attach 때 VID 헤더 스캔으로 재구성</text>
  <text x="340" y="112" text-anchor="middle" font-size="12">WL: 빈 PEB를 EC 기준으로 고름 · 배드 PEB 예약 · 비트플립 PEB scrub</text>
  <line x1="210" y1="56" x2="290" y2="148" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="275" y1="56" x2="430" y2="148" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="56" x2="240" y2="148" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="405" y1="56" x2="380" y2="148" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="470" y1="56" x2="335" y2="148" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="20" y="166" font-size="13">MTD (PEB)</text>
  <rect x="90" y="150" width="45" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="112" y="170" text-anchor="middle" font-size="12">0</text>
  <rect x="140" y="150" width="45" height="30" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/><text x="162" y="170" text-anchor="middle" font-size="12">10 BAD</text>
  <rect x="190" y="150" width="45" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="212" y="170" text-anchor="middle" font-size="12">34 L</text>
  <rect x="240" y="150" width="45" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="262" y="170" text-anchor="middle" font-size="12">36</text>
  <rect x="290" y="150" width="45" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="312" y="170" text-anchor="middle" font-size="12">37</text>
  <rect x="335" y="150" width="45" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="357" y="170" text-anchor="middle" font-size="12">38</text>
  <rect x="380" y="150" width="45" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="402" y="170" text-anchor="middle" font-size="12">39</text>
  <rect x="430" y="150" width="45" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="452" y="170" text-anchor="middle" font-size="12">40</text>
  <rect x="480" y="150" width="45" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="502" y="170" text-anchor="middle" font-size="12">41</text>
  <rect x="530" y="150" width="45" height="30" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/><text x="552" y="170" text-anchor="middle" font-size="12">77 BAD</text>
  <text x="590" y="170" font-size="12">…127</text>
  <text x="20" y="206" font-size="12">회색 = 빈 PEB(EC 헤더만), 파랑 L = layout volume, 초록 = 볼륨 data, 빨강 = 배드 (예제 7 실측 번호)</text>
  <text x="20" y="246" font-size="13" font-weight="bold">PEB 하나의 내부 (128 KiB, page 2 KiB, sub-page 512 B — 예제 6 실측)</text>
  <rect x="20" y="260" width="80" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="60" y="280" text-anchor="middle" font-size="12">EC hdr</text><text x="60" y="298" text-anchor="middle" font-size="12">off 0</text>
  <rect x="100" y="260" width="80" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="140" y="280" text-anchor="middle" font-size="12">VID hdr</text><text x="140" y="298" text-anchor="middle" font-size="12">off 512</text>
  <rect x="180" y="260" width="80" height="50" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="220" y="290" text-anchor="middle" font-size="12">패딩</text>
  <rect x="260" y="260" width="400" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="460" y="280" text-anchor="middle" font-size="12">LEB 데이터: off 2048 ~ 끝</text>
  <text x="460" y="298" text-anchor="middle" font-size="12">LEB size = 131072 − 2048 = 129024 B</text>
  <text x="20" y="334" font-size="12">EC hdr: "UBI#" · version · ec(be64) · vid_hdr_offset · data_offset · image_seq · crc</text>
  <text x="20" y="352" font-size="12">VID hdr: "UBI!" · vol_type · copy_flag · vol_id · lnum · data_size · data_crc</text>
  <text x="78" y="370" font-size="12">· sqnum(be64) · hdr_crc</text>
  <text x="20" y="390" font-size="12">attach = 모든 PEB의 두 헤더를 읽는다 → 같은 (vol_id, lnum)이 둘이면 sqnum 큰 쪽이 최신</text>
  <text x="20" y="410" font-size="12">wear 정보(EC)와 매핑 정보(VID)가 데이터와 같은 블록에 있어 별도 메타데이터 블록이 없다</text>
</svg>
```
그림 2 — MTD → UBI → UBIFS 계층과 LEB→PEB 매핑. 화살표의 LEB→PEB 대응(LEB 6 → PEB 36, LEB 0 → 37, LEB 11 → 38, LEB 10 → 39, LEB 1 → 40)은 예제 7의 raw 스캔 결과 그대로다. 매핑이 순서대로가 아니라는 점(UBIFS가 LEB를 쓴 순서대로 빈 PEB를 받았다)을 보라.

### 3.2 예제 6 — `UBI_IOCATT`로 attach하고 볼륨·UBIFS 만들기

무엇을 확인하나: BusyBox `ubiattach`가 내부에서 하는 `/dev/ubi_ctrl`의 `UBI_IOCATT` ioctl을 직접 부르고, 최근 커널에 추가된 `UBI_IOCECNFO`(PEB별 erase counter 조회, 6.18 랩 커널에 있음) ioctl로 EC를 읽는다.

```c
/* i11_ubictl: UBI_IOCATT / UBI_IOCDET on /dev/ubi_ctrl, UBI_IOCECNFO on /dev/ubiN */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <mtd/ubi-user.h>

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s att MTDNUM | det UBINUM | ec /dev/ubiN\n", argv[0]); return 1; }
	if (!strcmp(argv[1], "att") || !strcmp(argv[1], "det")) {
		int fd = open("/dev/ubi_ctrl", O_RDONLY);
		if (fd < 0) { perror("/dev/ubi_ctrl"); exit(1); }
		if (argv[1][0] == 'a') {
			struct ubi_attach_req req;
			memset(&req, 0, sizeof(req));
			req.ubi_num = UBI_DEV_NUM_AUTO;	/* kernel picks ubi0, ubi1, ... */
			req.mtd_num = atoi(argv[2]);
			req.vid_hdr_offset = 0;		/* 0 = default (next min I/O unit) */
			req.max_beb_per1024 = 0;	/* 0 = CONFIG_MTD_UBI_BEB_LIMIT (20) */
			if (ioctl(fd, UBI_IOCATT, &req)) { perror("UBI_IOCATT"); exit(1); }
			printf("attached mtd%s -> ubi%d\n", argv[2], req.ubi_num);
		} else {
			__s32 n = atoi(argv[2]);
			if (ioctl(fd, UBI_IOCDET, &n)) { perror("UBI_IOCDET"); exit(1); }
			printf("detached ubi%d\n", n);
		}
		return 0;
	}
	int fd = open(argv[2], O_RDONLY);	/* ec: per-PEB erase counters (6.13+ ioctl) */
	if (fd < 0) { perror(argv[2]); exit(1); }
	int n = 4096;
	struct ubi_ecinfo_req *r = calloc(1, sizeof(*r) + n * sizeof(__s32));
	if (!r) { perror("calloc"); exit(1); }
	r->start = 0;
	r->length = n;				/* kernel clamps to peb_count */
	if (ioctl(fd, UBI_IOCECNFO, r)) { perror("UBI_IOCECNFO"); exit(1); }
	long sum = 0; int mn = 1 << 30, mx = -1, used = 0;
	printf("EC:");
	for (int i = 0; i < r->read_length; i++) {
		int ec = r->erase_counters[i];	/* -1 (UBI_UNKNOWN) = bad / not in WL */
		printf(" %d", ec);
		if (ec < 0) continue;
		used++; sum += ec;
		if (ec < mn) mn = ec;
		if (ec > mx) mx = ec;
	}
	printf("\npebs=%d counted=%d min=%d max=%d mean=%.1f\n", r->read_length, used, mn, mx,
	       used ? (double)sum / used : 0.0);
	return 0;
}
```

게스트 스크립트 `g_ubi.sh`(16 MiB nandsim = 128 PEB, block 10과 77을 공장 배드로):

```sh
cd /i11
modprobe nandsim id_bytes=0xec,0xf1,0x00,0x95 overridesize=7 badblocks=10,77
modprobe ubi
echo "### attach (UBI_IOCATT)"
./i11_ubictl att 1
dmesg | grep -E "ubi0|UBI" | sed 's/^\[[ 0-9.]*\] //'
echo "### sysfs"
for a in total_eraseblocks avail_eraseblocks eraseblock_size min_io_size max_ec mean_ec reserved_for_bad bad_peb_count bgt_enabled; do printf "%-18s %s\n" $a "$(cat /sys/class/ubi/ubi0/$a 2>/dev/null)"; done
ls -l --color=never /dev/ubi*
echo "### mkvol + ubifs"
ubimkvol /dev/ubi0 -N data -m
ls --color=never /sys/class/ubi/; cat /sys/class/ubi/ubi0_0/reserved_ebs /sys/class/ubi/ubi0_0/usable_eb_size /sys/class/ubi/ubi0_0/type
modprobe zstd; modprobe deflate; modprobe ubifs
mkdir -p /mnt/u && mount -t ubifs ubi0:data /mnt/u
dmesg | grep -E "UBIFS" | sed 's/^\[[ 0-9.]*\] //' | head -12
df -k /mnt/u | tail -1
dd if=/dev/urandom of=/mnt/u/cold.bin bs=64k count=16 2>/dev/null; echo "hello ubifs" > /mnt/u/hello.txt; sync
ls -l --color=never /mnt/u
umount /mnt/u
echo "### EC via UBI_IOCECNFO"
./i11_ubictl ec /dev/ubi0 | tail -1
echo "### debugfs detailed_erase_block_info (head)"
head -5 /sys/kernel/debug/ubi/ubi0/detailed_erase_block_info
./i11_ubictl det 0
```

```text
lab initramfs: 6.18.55-sub up 0.71s
### attach (UBI_IOCATT)
attached mtd1 -> ubi0
ubi0: attaching mtd1
ubi0: scanning is finished
ubi0: empty MTD device detected
ubi0: attached mtd1 (name "NAND simulator partition 0", size 16 MiB)
ubi0: PEB size: 131072 bytes (128 KiB), LEB size: 129024 bytes
ubi0: min./max. I/O unit sizes: 2048/2048, sub-page size 512
ubi0: VID header offset: 512 (aligned 512), data offset: 2048
ubi0: good PEBs: 126, bad PEBs: 2, corrupted PEBs: 0
ubi0: user volume: 0, internal volumes: 1, max. volumes count: 128
ubi0: max/mean erase counter: 0/0, WL threshold: 4096, image sequence number: 311168347
ubi0: available PEBs: 121, total reserved PEBs: 5, PEBs reserved for bad PEB handling: 1
ubi0: background thread "ubi_bgt0d" started, PID 76
### sysfs
total_eraseblocks  126
avail_eraseblocks  121
eraseblock_size    129024
min_io_size        2048
max_ec             1
mean_ec            
reserved_for_bad   1
bad_peb_count      2
bgt_enabled        1
crw-------    1 0        0         510,   0 Oct 11 23:33 /dev/ubi0
crw-------    1 0        0          10, 258 Oct 11 23:33 /dev/ubi_ctrl
### mkvol + ubifs
ubi0     ubi0_0   version
121
129024
dynamic
UBIFS (ubi0:0): default file-system created
UBIFS (ubi0:0): Mounting in unauthenticated mode
UBIFS (ubi0:0): background thread "ubifs_bgt0_0" started, PID 98
UBIFS (ubi0:0): UBIFS: mounted UBI device 0, volume 0, name "data"
UBIFS (ubi0:0): LEB size: 129024 bytes (126 KiB), min./max. I/O unit sizes: 2048 bytes/2048 bytes
UBIFS (ubi0:0): FS size: 14321664 bytes (13 MiB, 111 LEBs), max 121 LEBs, journal size 1032193 bytes (0 MiB, 6 LEBs)
UBIFS (ubi0:0): reserved for root: 676447 bytes (660 KiB)
UBIFS (ubi0:0): media format: w5/r0 (latest is w5/r0), UUID 2EFEED2C-7B50-4423-A308-B3B12C6A7A3F, small LPT model
ubi0:data                12324        20     11644   0% /mnt/u
total 1028
-rw-r--r--    1 0        0          1048576 Oct 11 23:33 cold.bin
-rw-r--r--    1 0        0               12 Oct 11 23:33 hello.txt
### EC via UBI_IOCECNFO
pebs=128 counted=126 min=1 max=2 mean=1.0
### debugfs detailed_erase_block_info (head)
physical_block_number	erase_count
0                     	2          
1                     	2          
2                     	1          
3                     	1          
detached ubi0
```

출력에서 볼 것:

- `PEB size 131072, LEB size 129024`: PEB에서 EC/VID 헤더용 2 KiB(첫 page)를 뺀 것이 LEB다. `sub-page size 512` 덕분에 EC 헤더(offset 0)와 VID 헤더(offset 512)가 **같은 page의 다른 sub-page**에 들어가 2 KiB만 쓴다. sub-page가 없는 NAND(대부분 MLC/TLC)라면 헤더 두 개가 page 두 개를 먹어 LEB = PEB − 2 page가 된다.
- `good PEBs: 126, bad PEBs: 2`: MTD의 배드 블록(10, 77)을 UBI가 처음부터 제외했다. `PEBs reserved for bad PEB handling: 1`: 예약 한도는 `CONFIG_MTD_UBI_BEB_LIMIT=20`(1024 PEB당 20개) → 128 PEB면 3개인데, 이미 배드가 2개라 1개만 더 예약했다(한도는 "장치 전체 수명 동안 생길 배드 블록 수"라는 뜻이다). §3.6에서 이 예약이 바닥나는 장면을 본다.
- `total reserved PEBs: 5` = layout volume 2 + 배드 예약 1 + WL용 1 + EBA용 1(원자적 LEB 변경용). 나머지 121이 볼륨에 쓸 수 있는 양이고, `ubimkvol -m`(최대 크기)으로 만든 볼륨의 `reserved_ebs`가 121이다.
- `max/mean erase counter: 0/0` → **empty MTD device detected**: 처음 붙은 빈 장치라 UBI가 모든 PEB를 지우고 EC 헤더(ec=1 또는 2)를 썼다. `mean_ec`는 sysfs 속성이 아니라서 빈 줄이 나왔다(dmesg에만 있다).
- `/dev/ubi0`(볼륨 관리용 char 장치, 동적 major 510)와 `/dev/ubi_ctrl`(misc 10:258, attach/detach용). 볼륨은 `/dev/ubi0_0`으로 생긴다.
- UBIFS: `FS size 111 LEBs, max 121 LEBs, journal 6 LEBs`. 나머지 LEB는 UBIFS 관리 영역(superblock, master, log, LPT, orphan)이다. UBIFS는 모듈 로드 때 빌드된 압축기(lzo/zlib/zstd)를 전부 초기화하므로 `zstd` 크립토 모듈이 없으면 `cannot initialize compressor zstd, error -2`로 `ubifs.ko` 로드 자체가 실패한다(처음 시도에서 실제로 겪었다 — initramfs에 `kernel/crypto`, `kernel/lib/zstd`를 넣어 해결).
- `UBI_IOCECNFO`와 debugfs `detailed_erase_block_info`가 같은 값을 준다(PEB 0·1 = 2, 나머지 = 1). PEB 0·1은 layout volume을 처음 쓸 때 한 번 더 지워졌다.

### 3.3 예제 7 — raw MTD에서 EC/VID 헤더를 직접 읽기 (UBI attach가 하는 일)

무엇을 확인하나: UBI를 detach한 뒤 `/dev/mtd1`을 읽어 **UBI 없이도** 매핑을 재구성할 수 있음을 보인다. 이것이 attach 스캔이고, Don의 펌웨어가 "부팅 시 각 블록의 메타데이터(spare 영역의 LBA 태그)를 읽어 L2P를 재구성"하던 것과 같다.

```c
/* i11_ubiscan: read UBI EC + VID headers straight from raw /dev/mtdN (what ubi_attach scans) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <sys/ioctl.h>
#include <mtd/mtd-user.h>

struct ec_hdr { uint32_t magic; uint8_t ver, pad1[3]; uint64_t ec; uint32_t vid_off, data_off, image_seq;
		uint8_t pad2[32]; uint32_t crc; } __attribute__((packed));
struct vid_hdr { uint32_t magic; uint8_t ver, vol_type, copy_flag, compat; uint32_t vol_id, lnum;
		 uint8_t pad1[4]; uint32_t data_size, used_ebs, data_pad, data_crc; uint8_t pad2[4];
		 uint64_t sqnum; uint8_t pad3[12]; uint32_t crc; } __attribute__((packed));

int main(int argc, char **argv)
{
	struct mtd_info_user mi;
	int fd = open(argc > 1 ? argv[1] : "/dev/mtd1", O_RDONLY);
	if (fd < 0 || ioctl(fd, MEMGETINFO, &mi)) { perror("mtd"); exit(1); }
	int verbose = argc > 2, nbad = 0, nfree = 0, nmap = 0;
	for (unsigned peb = 0; peb < mi.size / mi.erasesize; peb++) {
		loff_t o = (loff_t)peb * mi.erasesize;
		struct ec_hdr e; struct vid_hdr v;
		if (ioctl(fd, MEMGETBADBLOCK, &o) > 0) { nbad++; if (verbose) printf("PEB %3u BAD\n", peb); continue; }
		if (pread(fd, &e, sizeof(e), o) != sizeof(e)) { perror("pread"); exit(1); }
		if (be32toh(e.magic) != 0x55424923) { if (verbose) printf("PEB %3u no EC hdr\n", peb); continue; }
		if (pread(fd, &v, sizeof(v), o + be32toh(e.vid_off)) != sizeof(v)) { perror("pread"); exit(1); }
		int mapped = be32toh(v.magic) == 0x55424921;	/* "UBI!" */
		mapped ? nmap++ : nfree++;
		if (!verbose) continue;
		printf("PEB %3u ec=%-4llu", peb, (unsigned long long)be64toh(e.ec));
		if (peb == 0) printf(" vid_off=%u data_off=%u", be32toh(e.vid_off), be32toh(e.data_off));
		if (mapped)
			printf(" vol_id=0x%x lnum=%-3u sqnum=%llu", be32toh(v.vol_id), be32toh(v.lnum),
			       (unsigned long long)be64toh(v.sqnum));
		else
			printf(" (free: EC hdr only)");
		printf("\n");
	}
	printf("summary: mapped=%d free=%d bad=%d\n", nmap, nfree, nbad);
	return 0;
}
```

```sh
./i11_ubiscan /dev/mtd1 v | head -14
./i11_ubiscan /dev/mtd1 v | grep -E "BAD|vol_id=" | head -8
./i11_ubiscan /dev/mtd1
dd if=/dev/mtd1 bs=2048 count=1 2>/dev/null | hexdump -C | head -5
./i11_ubictl att 1 >/dev/null; mount -t ubifs ubi0:data /mnt/u && cat /mnt/u/hello.txt
```

```text
### raw scan of /dev/mtd1 (EC + VID headers)
PEB   0 ec=2    vid_off=512 data_off=2048 (free: EC hdr only)
PEB   1 ec=2    (free: EC hdr only)
PEB   2 ec=1    (free: EC hdr only)
PEB   3 ec=1    (free: EC hdr only)
PEB   4 ec=1    (free: EC hdr only)
PEB   5 ec=1    (free: EC hdr only)
PEB   6 ec=1    (free: EC hdr only)
PEB   7 ec=1    (free: EC hdr only)
PEB   8 ec=1    (free: EC hdr only)
PEB   9 ec=1    (free: EC hdr only)
PEB  10 BAD
PEB  11 ec=1    (free: EC hdr only)
PEB  12 ec=1    (free: EC hdr only)
PEB  13 ec=1    (free: EC hdr only)
PEB  10 BAD
PEB  34 ec=1    vol_id=0x7fffefff lnum=0   sqnum=3
PEB  35 ec=1    vol_id=0x7fffefff lnum=1   sqnum=4
PEB  36 ec=1    vol_id=0x0 lnum=6   sqnum=5
PEB  37 ec=1    vol_id=0x0 lnum=0   sqnum=6
PEB  38 ec=1    vol_id=0x0 lnum=11  sqnum=7
PEB  39 ec=1    vol_id=0x0 lnum=10  sqnum=8
PEB  40 ec=1    vol_id=0x0 lnum=1   sqnum=9
summary: mapped=18 free=108 bad=2
### hexdump PEB 0 EC header and VID header
00000000  55 42 49 23 01 00 00 00  00 00 00 00 00 00 00 02  |UBI#............|
00000010  00 00 02 00 00 00 08 00  12 8c 0d 5b 00 00 00 00  |...........[....|
00000020  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00000030  00 00 00 00 00 00 00 00  00 00 00 00 e4 89 a3 7d  |...............}|
00000040  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
### re-attach: scan rebuilds the map, file survives
hello ubifs
ubi0: attached mtd1 (name "NAND simulator partition 0", size 16 MiB)
```

출력에서 볼 것:

- `mapped=18`: layout volume 2개(PEB 34·35, `vol_id=0x7fffefff`, `lnum` 0·1) + UBIFS가 쓴 LEB 16개. `free=108`: EC 헤더만 있는 지워진 PEB. 이 PEB들도 **EC 헤더는 있다** — 지운 직후 바로 EC를 써 두므로 전원이 나가도 erase 횟수를 잃지 않는다.
- LEB 번호와 PEB 번호가 무관하다(LEB 6 → PEB 36, LEB 0 → PEB 37). `sqnum`이 쓰인 순서다.
- hexdump: `55 42 49 23` = `UBI#`, 버전 1, `ec`(be64) = 2, `00 00 02 00` = VID 헤더 오프셋 512, `00 00 08 00` = 데이터 오프셋 2048, 그 다음 4바이트가 image sequence(실행마다 다름), 0x3c의 4바이트가 헤더 CRC. 0x40부터는 `ff` — 64바이트 헤더 뒤는 지워진 상태 그대로다.
- 다시 attach하면 스캔으로 매핑이 복원되고 `hello.txt`가 그대로 읽힌다.

### 3.4 SSD FTL ↔ UBI/UBIFS 대응표

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="160" y="22" text-anchor="middle" font-size="14" font-weight="bold">Don의 SSD FTL (펌웨어)</text>
  <text x="520" y="22" text-anchor="middle" font-size="14" font-weight="bold">Linux raw NAND 스택</text>
  <rect x="20" y="36" width="280" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160" y="61" text-anchor="middle" font-size="13">L2P 테이블 (page 단위)</text>
  <rect x="380" y="36" width="280" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="520" y="54" text-anchor="middle" font-size="12">UBI EBA: LEB→PEB (블록 단위)</text><text x="520" y="70" text-anchor="middle" font-size="12">+ UBIFS 인덱스 트리 (page 단위)</text>
  <rect x="20" y="86" width="280" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160" y="111" text-anchor="middle" font-size="13">부팅 시 메타 스캔 / 체크포인트</text>
  <rect x="380" y="86" width="280" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="520" y="111" text-anchor="middle" font-size="12">attach 스캔(VID 헤더) / fastmap</text>
  <rect x="20" y="136" width="280" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160" y="161" text-anchor="middle" font-size="13">블록별 erase count 테이블</text>
  <rect x="380" y="136" width="280" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="520" y="161" text-anchor="middle" font-size="12">PEB마다 EC 헤더 (UBI_IOCECNFO)</text>
  <rect x="20" y="186" width="280" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160" y="211" text-anchor="middle" font-size="13">dynamic + static wear leveling</text>
  <rect x="380" y="186" width="280" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="520" y="204" text-anchor="middle" font-size="12">free PEB 선택(EC 낮은 쪽) +</text><text x="520" y="220" text-anchor="middle" font-size="12">WL_THRESHOLD 초과 시 cold LEB 이동</text>
  <rect x="20" y="236" width="280" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160" y="261" text-anchor="middle" font-size="13">GC (유효 page 이동 후 erase)</text>
  <rect x="380" y="236" width="280" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="520" y="261" text-anchor="middle" font-size="12">UBIFS GC → LEB unmap → UBI erase worker</text>
  <rect x="20" y="286" width="280" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160" y="311" text-anchor="middle" font-size="13">grown bad block 리타이어 + 예비 블록</text>
  <rect x="380" y="286" width="280" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="520" y="304" text-anchor="middle" font-size="12">mark bad (MTD) + BEB 예약</text><text x="520" y="320" text-anchor="middle" font-size="12">(BEB_LIMIT 20/1024, 바닥나면 R/O)</text>
  <rect x="20" y="336" width="280" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160" y="361" text-anchor="middle" font-size="13">read disturb / retention → refresh</text>
  <rect x="380" y="336" width="280" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="520" y="361" text-anchor="middle" font-size="12">-EUCLEAN(비트플립) → PEB scrub</text>
  <rect x="20" y="386" width="280" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160" y="406" text-anchor="middle" font-size="13">ECC 엔진 (LDPC/BCH HW)</text>
  <rect x="380" y="386" width="280" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="520" y="406" text-anchor="middle" font-size="12">nand_ecc_engine (SOFT/ON_HOST/ON_DIE)</text>
  <line x1="300" y1="56" x2="378" y2="56" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="300" y1="106" x2="378" y2="106" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="300" y1="156" x2="378" y2="156" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="300" y1="206" x2="378" y2="206" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="300" y1="256" x2="378" y2="256" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="300" y1="306" x2="378" y2="306" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="300" y1="356" x2="378" y2="356" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="300" y1="401" x2="378" y2="401" stroke="currentColor" marker-end="url(#ah5)"/>
</svg>
```
그림 3 — SSD FTL 개념과 UBI/UBIFS의 대응. 큰 차이 두 가지: (1) UBI 매핑은 **erase block 단위**라 테이블이 작고(PEB 수만큼), page 단위 배치는 파일시스템(UBIFS)이 직접 한다 — FTL의 두 역할이 두 계층으로 나뉘었다. (2) 별도 메타데이터 블록 없이 **각 PEB 헤더에 분산 저장**하고 attach 때 스캔으로 모은다.

면접에서 이 대응을 말로 할 때는 이렇게 짧게 한다: "UBI is basically a block-level FTL in the kernel: EC headers are the erase-count table, VID headers with sequence numbers are the distributed L2P map rebuilt by scanning, and UBIFS on top does the page-level placement and garbage collection."

### 3.5 예제 8 — wear leveling을 측정하기: 임계값 4096 vs 16

무엇을 확인하나: cold 데이터(6 MiB, 한 번 쓰고 그대로)와 hot 데이터(512 KiB를 쓰고 지우기 1500회)가 섞인 워크로드에서 **PEB별 erase count 분포**를 본다. 기본 UBI(`CONFIG_MTD_UBI_WL_THRESHOLD=4096`)와 임계값을 16으로 낮춘 사본(`i11_ubi.ko wl_threshold=16`, 예제 1에서 빌드)을 같은 부팅에서 차례로 돌린다. 임계값은 "가장 많이 지워진 PEB와 cold 데이터가 앉은 PEB의 EC 차이가 이보다 크면 cold 데이터를 옮긴다"는 **static wear leveling** 트리거다.

```sh
cd /i11
modprobe zstd; modprobe deflate
dd if=/dev/urandom of=/tmp/cold bs=64k count=96 2>/dev/null   # 6 MiB written once
dd if=/dev/urandom of=/tmp/hot bs=64k count=8 2>/dev/null     # 512 KiB rewritten
run() {   # $1 = label, $2 = command that loads a UBI module
  modprobe nandsim id_bytes=0xec,0xf1,0x00,0x95 overridesize=7
  eval "$2"; ./i11_ubictl att 1 >/dev/null
  ubimkvol /dev/ubi0 -N data -m >/dev/null
  insmod /lib/modules/6.18.55-sub/kernel/fs/ubifs/ubifs.ko
  mount -t ubifs ubi0:data /mnt/u && cp /tmp/cold /mnt/u/cold && sync
  t0=$(cut -d. -f1 /proc/uptime)
  for i in $(seq 1 1500); do
    cp /tmp/hot /mnt/u/hot; sync; rm /mnt/u/hot; sync
    [ $((i % 250)) -eq 0 ] && echo "$1 iter=$i $(./i11_ubictl ec /dev/ubi0 | tail -1)"
  done
  echo "$1 elapsed=$(( $(cut -d. -f1 /proc/uptime) - t0 ))s"
  ./i11_ubictl ec /dev/ubi0 | head -1 | sed "s/^/$1 /"
  umount /mnt/u; ./i11_ubictl det 0 >/dev/null
  rmmod ubifs; rmmod $3; rmmod nandsim
}
mkdir -p /mnt/u
run stock "modprobe ubi" ubi
run thr16 "insmod /i11/i11_ubi.ko wl_threshold=16" i11_ubi
dmesg | grep -E "WL threshold" | sed 's/^\[[ 0-9.]*\] //'
```

`ubifs.ko`를 `modprobe`가 아니라 `insmod`로 넣은 이유: `modprobe ubifs`는 `modules.dep`를 따라 원래 `ubi.ko`를 먼저 올리려 한다. 랩 커널은 `CONFIG_MODVERSIONS`가 꺼져 있어서 `ubifs.ko`의 `ubi_*` 심볼이 이름만으로 `i11_ubi.ko`에 연결된다.

```text
lab initramfs: 6.18.55-sub up 1.07s
stock iter=250 pebs=128 counted=128 min=1 max=38 mean=14.8
stock iter=500 pebs=128 counted=128 min=1 max=66 mean=28.5
stock iter=750 pebs=128 counted=128 min=1 max=102 mean=42.2
stock iter=1000 pebs=128 counted=128 min=1 max=130 mean=55.9
stock iter=1250 pebs=128 counted=128 min=1 max=161 mean=69.6
stock iter=1500 pebs=128 counted=128 min=1 max=192 mean=83.4
stock elapsed=83s
stock EC: 176 192 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 189 191 191 170 192 192 192 191 192 191 191 191 104 191 189 166 1 1 159 1 1 1 157 146 191 1 1 1 1 191 1 1 1 1 191 1 1 1 1 191 1 1 1 1 191 1 1 1 1 190 1 1 1 1 191 1 1 1 1 191 1 1 1 1 191 1 1 1 1 191 1 1 1 1 179 1 1 1 1 189 1 1 1 1 191 1 1 184 151 124 191 191 168 184 179 191 189 191 191 186 189 185 187 191 191 191 191 168 191 191 184
thr16 iter=250 pebs=128 counted=128 min=1 max=19 mean=15.2
thr16 iter=500 pebs=128 counted=128 min=4 max=35 mean=29.6
thr16 iter=750 pebs=128 counted=128 min=19 max=50 mean=43.8
thr16 iter=1000 pebs=128 counted=128 min=35 max=66 mean=58.2
thr16 iter=1250 pebs=128 counted=128 min=52 max=83 mean=72.5
thr16 iter=1500 pebs=128 counted=128 min=66 max=97 mean=86.8
thr16 elapsed=89s
thr16 EC: 92 92 66 72 81 90 87 90 90 82 89 93 93 82 89 88 93 89 88 86 87 81 88 81 81 81 81 81 81 81 81 81 81 81 81 81 81 81 92 81 81 81 81 81 82 82 81 82 81 81 81 81 81 81 81 81 81 81 81 85 97 81 81 81 81 82 81 81 81 81 81 82 87 81 97 83 82 97 88 90 82 97 89 81 89 92 97 81 81 89 97 97 82 97 92 92 97 97 97 97 94 96 94 91 93 96 81 83 81 96 81 96 96 96 96 96 92 96 96 81 96 96 97 97 89 96 96 95
ubi0: max/mean erase counter: 0/0, WL threshold: 4096, image sequence number: 650285289
ubi0: max/mean erase counter: 0/0, WL threshold: 4096, image sequence number: 2241361730
```

(EC 배열 줄은 128개 숫자가 한 줄로 이어져 길다. 그대로 두었다.)

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
<text x="330" y="22" text-anchor="middle" font-size="14" font-weight="bold">PEB별 erase counter (1500회 hot 쓰기 후, 낮은 순 정렬)</text>
<line x1="70" y1="290" x2="630" y2="290" stroke="currentColor"/>
<line x1="70" y1="290" x2="70" y2="50" stroke="currentColor"/>
<line x1="66" y1="290.0" x2="70" y2="290.0" stroke="currentColor"/>
<line x1="70" y1="290.0" x2="630" y2="290.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="294.0" text-anchor="end" font-size="12">0</text>
<line x1="66" y1="230.0" x2="70" y2="230.0" stroke="currentColor"/>
<line x1="70" y1="230.0" x2="630" y2="230.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="234.0" text-anchor="end" font-size="12">50</text>
<line x1="66" y1="170.0" x2="70" y2="170.0" stroke="currentColor"/>
<line x1="70" y1="170.0" x2="630" y2="170.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="174.0" text-anchor="end" font-size="12">100</text>
<line x1="66" y1="110.0" x2="70" y2="110.0" stroke="currentColor"/>
<line x1="70" y1="110.0" x2="630" y2="110.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="114.0" text-anchor="end" font-size="12">150</text>
<line x1="66" y1="50.0" x2="70" y2="50.0" stroke="currentColor"/>
<line x1="70" y1="50.0" x2="630" y2="50.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="54.0" text-anchor="end" font-size="12">200</text>
<text x="26" y="170.0" font-size="12" text-anchor="middle" transform="rotate(-90 26 170.0)">erase count</text>
<polyline points="70.0,288.8 74.4,288.8 78.8,288.8 83.2,288.8 87.6,288.8 92.0,288.8 96.5,288.8 100.9,288.8 105.3,288.8 109.7,288.8 114.1,288.8 118.5,288.8 122.9,288.8 127.3,288.8 131.7,288.8 136.1,288.8 140.6,288.8 145.0,288.8 149.4,288.8 153.8,288.8 158.2,288.8 162.6,288.8 167.0,288.8 171.4,288.8 175.8,288.8 180.2,288.8 184.6,288.8 189.1,288.8 193.5,288.8 197.9,288.8 202.3,288.8 206.7,288.8 211.1,288.8 215.5,288.8 219.9,288.8 224.3,288.8 228.7,288.8 233.1,288.8 237.6,288.8 242.0,288.8 246.4,288.8 250.8,288.8 255.2,288.8 259.6,288.8 264.0,288.8 268.4,288.8 272.8,288.8 277.2,288.8 281.7,288.8 286.1,288.8 290.5,288.8 294.9,288.8 299.3,288.8 303.7,288.8 308.1,288.8 312.5,288.8 316.9,288.8 321.3,288.8 325.7,288.8 330.2,288.8 334.6,288.8 339.0,288.8 343.4,288.8 347.8,288.8 352.2,288.8 356.6,288.8 361.0,288.8 365.4,288.8 369.8,288.8 374.3,288.8 378.7,165.2 383.1,141.2 387.5,114.8 391.9,108.8 396.3,101.6 400.7,99.2 405.1,90.8 409.5,88.4 413.9,88.4 418.3,86.0 422.8,78.8 427.2,75.2 431.6,75.2 436.0,69.2 440.4,69.2 444.8,69.2 449.2,68.0 453.6,66.8 458.0,65.6 462.4,63.2 466.9,63.2 471.3,63.2 475.7,63.2 480.1,63.2 484.5,62.0 488.9,60.8 493.3,60.8 497.7,60.8 502.1,60.8 506.5,60.8 510.9,60.8 515.4,60.8 519.8,60.8 524.2,60.8 528.6,60.8 533.0,60.8 537.4,60.8 541.8,60.8 546.2,60.8 550.6,60.8 555.0,60.8 559.4,60.8 563.9,60.8 568.3,60.8 572.7,60.8 577.1,60.8 581.5,60.8 585.9,60.8 590.3,60.8 594.7,60.8 599.1,60.8 603.5,60.8 608.0,60.8 612.4,59.6 616.8,59.6 621.2,59.6 625.6,59.6 630.0,59.6" fill="none" stroke="#4a7bd0" stroke-width="2.5"/>
<polyline points="70.0,210.8 74.4,203.6 78.8,192.8 83.2,192.8 87.6,192.8 92.0,192.8 96.5,192.8 100.9,192.8 105.3,192.8 109.7,192.8 114.1,192.8 118.5,192.8 122.9,192.8 127.3,192.8 131.7,192.8 136.1,192.8 140.6,192.8 145.0,192.8 149.4,192.8 153.8,192.8 158.2,192.8 162.6,192.8 167.0,192.8 171.4,192.8 175.8,192.8 180.2,192.8 184.6,192.8 189.1,192.8 193.5,192.8 197.9,192.8 202.3,192.8 206.7,192.8 211.1,192.8 215.5,192.8 219.9,192.8 224.3,192.8 228.7,192.8 233.1,192.8 237.6,192.8 242.0,192.8 246.4,192.8 250.8,192.8 255.2,192.8 259.6,192.8 264.0,192.8 268.4,192.8 272.8,192.8 277.2,192.8 281.7,192.8 286.1,192.8 290.5,192.8 294.9,192.8 299.3,192.8 303.7,191.6 308.1,191.6 312.5,191.6 316.9,191.6 321.3,191.6 325.7,191.6 330.2,191.6 334.6,191.6 339.0,191.6 343.4,191.6 347.8,190.4 352.2,190.4 356.6,188.0 361.0,186.8 365.4,185.6 369.8,185.6 374.3,185.6 378.7,184.4 383.1,184.4 387.5,184.4 391.9,184.4 396.3,183.2 400.7,183.2 405.1,183.2 409.5,183.2 413.9,183.2 418.3,183.2 422.8,183.2 427.2,182.0 431.6,182.0 436.0,182.0 440.4,182.0 444.8,180.8 449.2,179.6 453.6,179.6 458.0,179.6 462.4,179.6 466.9,179.6 471.3,179.6 475.7,179.6 480.1,178.4 484.5,178.4 488.9,178.4 493.3,178.4 497.7,177.2 502.1,177.2 506.5,176.0 510.9,174.8 515.4,174.8 519.8,174.8 524.2,174.8 528.6,174.8 533.0,174.8 537.4,174.8 541.8,174.8 546.2,174.8 550.6,174.8 555.0,174.8 559.4,174.8 563.9,174.8 568.3,174.8 572.7,173.6 577.1,173.6 581.5,173.6 585.9,173.6 590.3,173.6 594.7,173.6 599.1,173.6 603.5,173.6 608.0,173.6 612.4,173.6 616.8,173.6 621.2,173.6 625.6,173.6 630.0,173.6" fill="none" stroke="#e08a3c" stroke-width="2.5"/>
<text x="70.0" y="308" text-anchor="middle" font-size="12">0</text>
<text x="211.1" y="308" text-anchor="middle" font-size="12">32</text>
<text x="352.2" y="308" text-anchor="middle" font-size="12">64</text>
<text x="493.3" y="308" text-anchor="middle" font-size="12">96</text>
<text x="630.0" y="308" text-anchor="middle" font-size="12">127</text>
<text x="350.0" y="328" text-anchor="middle" font-size="12">PEB 순위 (128개, EC 오름차순)</text>
<line x1="90" y1="62" x2="114" y2="62" stroke="#4a7bd0" stroke-width="2.5"/><text x="120" y="66" font-size="12">stock ubi.ko, WL threshold 4096: min 1 · max 192 · 70개 PEB가 EC≤2</text>
<line x1="90" y1="82" x2="114" y2="82" stroke="#e08a3c" stroke-width="2.5"/><text x="120" y="86" font-size="12">i11_ubi.ko wl_threshold=16: min 66 · max 97</text>
</svg>
```
그림 4 — 1500회 후 PEB별 erase counter(오름차순 정렬). 기본 설정(파랑)은 **PEB 70개가 EC 1~2에 머물러 있고**(cold 데이터 + UBIFS 인덱스가 앉은 블록, 그리고 한 번도 순번이 오지 않은 블록) 나머지가 150~192로 혹사당했다. 임계값 16(주황)은 cold 데이터를 hot 블록으로 옮겨 모든 PEB가 66~97에 모였다.

```svg
<svg viewBox="0 0 660 340" xmlns="http://www.w3.org/2000/svg">
<text x="330" y="22" text-anchor="middle" font-size="14" font-weight="bold">max EC − min EC (마모 편차) 추이</text>
<line x1="70" y1="280" x2="610" y2="280" stroke="currentColor"/>
<line x1="70" y1="280" x2="70" y2="60" stroke="currentColor"/>
<line x1="66" y1="280.0" x2="70" y2="280.0" stroke="currentColor"/>
<line x1="70" y1="280.0" x2="610" y2="280.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="284.0" text-anchor="end" font-size="12">0</text>
<line x1="66" y1="225.0" x2="70" y2="225.0" stroke="currentColor"/>
<line x1="70" y1="225.0" x2="610" y2="225.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="229.0" text-anchor="end" font-size="12">50</text>
<line x1="66" y1="170.0" x2="70" y2="170.0" stroke="currentColor"/>
<line x1="70" y1="170.0" x2="610" y2="170.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="174.0" text-anchor="end" font-size="12">100</text>
<line x1="66" y1="115.0" x2="70" y2="115.0" stroke="currentColor"/>
<line x1="70" y1="115.0" x2="610" y2="115.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="119.0" text-anchor="end" font-size="12">150</text>
<line x1="66" y1="60.0" x2="70" y2="60.0" stroke="currentColor"/>
<line x1="70" y1="60.0" x2="610" y2="60.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="64.0" text-anchor="end" font-size="12">200</text>
<text x="26" y="170.0" font-size="12" text-anchor="middle" transform="rotate(-90 26 170.0)">max − min</text>
<polyline points="70.0,280.0 160.0,239.3 250.0,208.5 340.0,168.9 430.0,138.1 520.0,104.0 610.0,69.9" fill="none" stroke="#4a7bd0" stroke-width="2.5"/>
<circle cx="160.0" cy="239.3" r="3.5" fill="#4a7bd0"/>
<circle cx="250.0" cy="208.5" r="3.5" fill="#4a7bd0"/>
<circle cx="340.0" cy="168.9" r="3.5" fill="#4a7bd0"/>
<circle cx="430.0" cy="138.1" r="3.5" fill="#4a7bd0"/>
<circle cx="520.0" cy="104.0" r="3.5" fill="#4a7bd0"/>
<circle cx="610.0" cy="69.9" r="3.5" fill="#4a7bd0"/>
<polyline points="70.0,280.0 160.0,260.2 250.0,245.9 340.0,245.9 430.0,245.9 520.0,245.9 610.0,245.9" fill="none" stroke="#e08a3c" stroke-width="2.5"/>
<circle cx="160.0" cy="260.2" r="3.5" fill="#e08a3c"/>
<circle cx="250.0" cy="245.9" r="3.5" fill="#e08a3c"/>
<circle cx="340.0" cy="245.9" r="3.5" fill="#e08a3c"/>
<circle cx="430.0" cy="245.9" r="3.5" fill="#e08a3c"/>
<circle cx="520.0" cy="245.9" r="3.5" fill="#e08a3c"/>
<circle cx="610.0" cy="245.9" r="3.5" fill="#e08a3c"/>
<text x="70.0" y="298" text-anchor="middle" font-size="12">0</text>
<text x="160.0" y="298" text-anchor="middle" font-size="12">250</text>
<text x="250.0" y="298" text-anchor="middle" font-size="12">500</text>
<text x="340.0" y="298" text-anchor="middle" font-size="12">750</text>
<text x="430.0" y="298" text-anchor="middle" font-size="12">1000</text>
<text x="520.0" y="298" text-anchor="middle" font-size="12">1250</text>
<text x="610.0" y="298" text-anchor="middle" font-size="12">1500</text>
<text x="340.0" y="318" text-anchor="middle" font-size="12">hot 파일 쓰기·삭제 반복 횟수</text>
<line x1="90" y1="52" x2="114" y2="52" stroke="#4a7bd0" stroke-width="2.5"/><text x="120" y="56" font-size="12">stock (threshold 4096) → 191</text>
<line x1="90" y1="72" x2="114" y2="72" stroke="#e08a3c" stroke-width="2.5"/><text x="120" y="76" font-size="12">wl_threshold=16 → 31 (500회 이후 31에서 유지)</text>
</svg>
```
그림 5 — 마모 편차(max − min)의 시간 변화. 기본 설정은 편차가 선형으로 커진다(1500회에 191). 임계값 4096은 SLC급 수명(수만 회)을 가정한 값이라 이 짧은 실험에서는 static WL이 한 번도 돌지 않았다. 임계값 16은 500회 무렵부터 편차 31에서 멈춘다.

출력에서 볼 것:

- 총 erase 수는 둘이 비슷하다(평균 83.4 vs 86.8). **static WL은 erase를 줄이지 않는다** — cold 데이터를 옮기는 비용(평균 +3.4)을 내고 마모를 **고르게 펼친다**. Don이 SSD에서 static WL의 write amplification 비용과 수명 균등화를 저울질하던 것과 같은 트레이드오프다.
- dmesg의 `WL threshold: 4096`은 두 번 다 4096이다. `build.c`가 출력할 때 `CONFIG_` 상수를 그대로 찍기 때문이다(우리는 `wl.c`만 고쳤다). 측정 결과로만 효과가 보인다.
- 실무에서 이 값은 Kconfig로 고정된다. `drivers/mtd/ubi/Kconfig` 도움말 원문: 기본값은 수명 100000회 이상인 SLC·NOR용이고, "MLC NAND flashes which typically have eraseblock life-cycle less than 10000"에는 128이나 256 정도로 낮추라고 한다.

### 3.6 예제 9 — 런타임 배드 블록과 예약 PEB 고갈 → R/O

무엇을 확인하나: nandsim의 `weakblocks=PEB:N`(N번 erase한 뒤부터 erase 실패)로 **grown bad block**을 만들고, UBI가 (1) erase 실패를 감지하고 (2) PEB를 배드로 표시하고 (3) 예약 풀에서 하나를 꺼내 쓰고, (4) 예약이 바닥나면 **장치 전체를 읽기 전용으로** 돌리는지 본다. 데이터 파일 `keep`의 md5는 처음과 끝에 비교한다.

```sh
cd /i11
dmesg -n 1
modprobe zstd; modprobe deflate
dd if=/dev/urandom of=/tmp/hot bs=64k count=8 2>/dev/null
run() {   # $1 = weakblocks list (each fails on its 4th erase)
  echo "===== weakblocks=$1"
  modprobe nandsim id_bytes=0xec,0xf1,0x00,0x95 overridesize=7 weakblocks=$1
  modprobe ubi; ./i11_ubictl att 1 >/dev/null
  ubimkvol /dev/ubi0 -N data -m >/dev/null; modprobe ubifs
  mount -t ubifs ubi0:data /mnt/u
  S=/sys/class/ubi/ubi0
  echo "before: bad_peb_count=$(cat $S/bad_peb_count) reserved_for_bad=$(cat $S/reserved_for_bad)"
  dd if=/dev/urandom of=/mnt/u/keep bs=64k count=4 2>/dev/null; sync; md5sum /mnt/u/keep
  dmesg -c > /dev/null
  for i in $(seq 1 300); do
    [ "$(cat $S/ro_mode)" = 1 ] && { echo "ubi0 switched to R/O at iter $i"; break; }
    cp /tmp/hot /mnt/u/hot 2>/dev/null; sync; rm -f /mnt/u/hot; sync
  done
  echo "after:  bad_peb_count=$(cat $S/bad_peb_count) reserved_for_bad=$(cat $S/reserved_for_bad) ro_mode=$(cat $S/ro_mode) mtd1 bad_blocks=$(cat /sys/class/mtd/mtd1/bad_blocks)"
  dmesg | grep -E "ubi0" | grep -v retry | sed 's/^\[[ 0-9.]*\] //' | head -${2:-14}
  md5sum /mnt/u/keep
  [ "$(cat $S/ro_mode)" = 1 ] && return
  umount /mnt/u; ./i11_ubictl det 0 >/dev/null; rmmod ubifs ubi nandsim
}
mkdir -p /mnt/u
run 50:3,70:3
run 30:3,50:3,70:3,90:3,110:3 24
```

```text
lab initramfs: 6.18.55-sub up 0.73s
===== weakblocks=50:3,70:3
before: bad_peb_count=0 reserved_for_bad=3
54d0ee885596072c2e9203a816add9ce  /mnt/u/keep
after:  bad_peb_count=2 reserved_for_bad=1 ro_mode=0 mtd1 bad_blocks=2
ubi0 error: do_sync_erase [ubi]: cannot erase PEB 50, error -5
ubi0 error: __erase_worker [ubi]: failed to erase PEB 50, error -5
ubi0: mark PEB 50 as bad
ubi0: 2 PEBs left in the reserve
ubi0 error: do_sync_erase [ubi]: cannot erase PEB 70, error -5
ubi0 error: __erase_worker [ubi]: failed to erase PEB 70, error -5
ubi0: mark PEB 70 as bad
ubi0: 1 PEBs left in the reserve
54d0ee885596072c2e9203a816add9ce  /mnt/u/keep
===== weakblocks=30:3,50:3,70:3,90:3,110:3
before: bad_peb_count=0 reserved_for_bad=3
551b99479ee9b37122faedf6cdda9707  /mnt/u/keep
ubi0 switched to R/O at iter 35
after:  bad_peb_count=3 reserved_for_bad=0 ro_mode=1 mtd1 bad_blocks=3
ubi0 error: do_sync_erase [ubi]: cannot erase PEB 50, error -5
ubi0 error: __erase_worker [ubi]: failed to erase PEB 50, error -5
ubi0: mark PEB 50 as bad
ubi0: 2 PEBs left in the reserve
ubi0 error: do_sync_erase [ubi]: cannot erase PEB 70, error -5
ubi0 error: __erase_worker [ubi]: failed to erase PEB 70, error -5
ubi0: mark PEB 70 as bad
ubi0: 1 PEBs left in the reserve
ubi0 error: do_sync_erase [ubi]: cannot erase PEB 90, error -5
ubi0 error: __erase_worker [ubi]: failed to erase PEB 90, error -5
ubi0: mark PEB 90 as bad
ubi0 warning: __erase_worker [ubi]: last PEB from the reserve was used
ubi0 error: do_sync_erase [ubi]: cannot erase PEB 110, error -5
ubi0 error: __erase_worker [ubi]: failed to erase PEB 110, error -5
ubi0 error: __erase_worker [ubi]: no reserved/available physical eraseblocks
ubi0 warning: ubi_ro_mode.part.0 [ubi]: switch to read-only mode
ubi0 error: do_work.constprop.0 [ubi]: work failed with error code -5
ubi0 error: ubi_thread [ubi]: ubi_bgt0d: work failed with error code -5
551b99479ee9b37122faedf6cdda9707  /mnt/u/keep
[   18.398935] reboot: Power down
```

(각 실패 앞에 `error -5 while erasing PEB 50, retry`가 세 번씩 찍혔는데 `grep -v retry`로 뺐다 — UBI는 erase 실패를 바로 믿지 않고 재시도한다.)

출력에서 볼 것:

- 첫 번째 시나리오: PEB 50, 70이 4번째 erase에서 실패 → **`mark PEB 50 as bad` → `2 PEBs left in the reserve`**. MTD의 `bad_blocks`도 2로 올랐다(OOB BBM이 실제로 기록됨). 파일시스템은 아무것도 모른 채 계속 동작했고 `keep`의 md5가 같다. Don의 펌웨어가 grown bad block을 리타이어하고 예비 블록으로 바꾸던 것과 같은 흐름이다.
- 두 번째 시나리오: 예약 3개를 다 쓰자 `last PEB from the reserve was used`, 네 번째 실패에서 `no reserved/available physical eraseblocks` → **`switch to read-only mode`**. 이후 쓰기는 `-EROFS`로 실패한다(콘솔 로그를 막지 않았던 앞선 시도에서 `ubifs_leb_write ... failed, error -30`이 쏟아졌다). 그래도 **읽기는 된다** — `keep`의 md5가 같다. 필드 장비라면 "로그가 더 이상 안 쌓인다"로 나타나는 증상이다. `/sys/class/ubi/ubi0/ro_mode`를 헬스 모니터에서 감시하는 이유다.
- `MTD_UBI_BEB_LIMIT`(또는 attach 때 `max_beb_per1024`)는 **장치 수명 전체**의 배드 블록 예산이다. 데이터시트의 "최소 유효 블록 수(NVB)"를 보고 정한다 — 예: 1024블록 중 최소 1004 보장이면 20.

### 3.7 UBIFS · ubiblock · JFFS2 한 줄씩

- **UBIFS**: UBI 볼륨 위의 로그 구조 파일시스템. 인덱스는 B+트리(wandering tree), 저널, LPT(LEB properties tree, 각 LEB의 free/dirty 양), 압축(lzo/zlib/zstd), 전원 차단 내성. mount 시간이 장치 크기에 비례하지 않는다.
- **ubiblock**(`CONFIG_MTD_UBI_BLOCK`): UBI 볼륨을 **읽기 전용 블록 장치**로 노출 → squashfs 루트를 raw NAND에 올리는 정석(랩 커널에서는 꺼져 있다).
- **JFFS2**: MTD 위에 직접 올라가는 옛 로그 FS. mount 때 전체 스캔이라 큰 NAND에서는 느리다. 작은 NOR 파티션에 아직 쓴다.

---

## 4. managed flash — 장치가 FTL을 가진 세계

eMMC, SD, UFS, NVMe는 모두 "NAND + 컨트롤러 + 펌웨어 FTL"이다. 호스트 Linux 입장에서는 **블록 장치**이고, 커널이 하는 일은 (1) 버스 프로토콜로 명령을 나르는 것(MMC core/SDHCI, NVMe, UFS+SCSI), (2) block 계층에서 요청을 모으고 나누는 것, (3) 힌트를 주는 것(discard/TRIM, FUA, flush, eMMC의 cache control·packed command·RPMB)이다.

| 장치 | 버스·프로토콜 | 커널 드라이버 경로 | hw 큐 | `/dev` 이름 |
|---|---|---|---|---|
| SD 카드 | SD 버스(CMD/DAT0-3), SPI 모드도 있음 | MMC core(`core/sd.c`) + 호스트(SDHCI 등) + `mmc_blk` | 1 (+CQE 없음) | `mmcblk0`, `mmcblk0p1` |
| eMMC | MMC 버스(DAT0-7), HS200/HS400 | MMC core(`core/mmc.c`) + 호스트 + `mmc_blk`, CQE(`cqhci`) | 1 (CQE면 깊이 32) | `mmcblk0`, `boot0/1`, `rpmb` |
| UFS | MIPI M-PHY + UniPro, SCSI 명령 | `ufshcd` → SCSI 중간층 → `sd` | MCQ면 여러 개 | `sda` |
| NVMe | PCIe, SQ/CQ | `nvme-core` + `nvme`(PCI) | CPU 수만큼 | `nvme0n1` |

C2에서 eMMC의 boot 파티션·RPMB·EXT_CSD를 이미 다뤘으므로 여기서는 **MMC 코어의 구조와 열거 과정**, 그리고 모든 managed flash가 공통으로 서는 **blk-mq**로 간다.

---

## 5. MMC 서브시스템 — 호스트, 코어, 카드

### 5.1 직관 — 세 층

MMC 서브시스템(`drivers/mmc/`)은 SD, eMMC, SDIO(Wi-Fi 모듈 등)를 하나로 다룬다. 세 층으로 나뉜다.

- **호스트 드라이버**(`drivers/mmc/host/`): SoC의 SD/MMC 컨트롤러 HW를 다룬다. `struct mmc_host`를 `mmc_alloc_host()`로 만들고 `struct mmc_host_ops`를 채운 뒤 `mmc_add_host()`로 등록한다. 하는 일은 "명령 하나(+데이터)를 버스에 내보내고 응답을 받아 오기"(`.request`), "클럭·버스 폭·전압·타이밍 바꾸기"(`.set_ios`), "카드가 꽂혔나/쓰기 보호인가"(`.get_cd`/`.get_ro`), "샘플링 위상 튜닝"(`.execute_tuning`), "1.8 V 전환"(`.start_signal_voltage_switch`)뿐이다. **SD 프로토콜을 모른다.**
- **코어**(`drivers/mmc/core/`): 프로토콜을 안다. 카드 감지 시 `mmc_rescan()`이 SDIO → SD → MMC 순서로 시도하며 명령 시퀀스(CMD0, CMD8, ACMD41, CMD2, CMD3 …)를 보내 카드를 식별·초기화하고, 레지스터(CID, CSD, SCR, EXT_CSD)를 읽어 `struct mmc_card`를 만들어 **mmc 버스**(`/sys/bus/mmc`)에 등록한다.
- **카드 드라이버**: mmc 버스에 붙는 클라이언트. SD/eMMC면 `mmc_blk`(`core/block.c` — blk-mq 드라이버, `/dev/mmcblkN`), 테스트면 `mmc_test`, SDIO면 각 SDIO 기능 드라이버(예: Wi-Fi의 `brcmfmac`).

**SDHCI**(SD Host Controller Interface)는 SD Association이 정한 **표준 호스트 컨트롤러 레지스터 맵**이다. 많은 SoC 컨트롤러가 SDHCI 호환(+벤더 확장)이라서 Linux에는 `sdhci.c`라는 **라이브러리**가 있고, SoC 드라이버(`sdhci-esdhc-imx.c`, `sdhci-of-arasan.c`, `sdhci-tegra.c` …)는 `struct sdhci_ops`로 벤더 차이(클럭 설정, 튜닝, 리셋 quirk)만 채운다. 이 노트의 QEMU 실험은 PCI 버전인 `sdhci-pci`를 썼다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="10" width="200" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="120" y="30" text-anchor="middle" font-size="13">MMC core (mmc_rescan)</text>
  <rect x="250" y="10" width="190" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="345" y="30" text-anchor="middle" font-size="13">호스트 드라이버 (sdhci)</text>
  <rect x="470" y="10" width="190" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="565" y="30" text-anchor="middle" font-size="13">SD 카드</text>
  <line x1="120" y1="40" x2="120" y2="460" stroke="#888" stroke-dasharray="4 4"/>
  <line x1="345" y1="40" x2="345" y2="460" stroke="#888" stroke-dasharray="4 4"/>
  <line x1="565" y1="40" x2="565" y2="460" stroke="#888" stroke-dasharray="4 4"/>
  <line x1="120" y1="62" x2="340" y2="62" stroke="currentColor" marker-end="url(#ah3)"/><text x="232" y="56" text-anchor="middle" font-size="12">.get_cd() → 카드 있음</text>
  <line x1="120" y1="92" x2="340" y2="92" stroke="currentColor" marker-end="url(#ah3)"/><text x="232" y="86" text-anchor="middle" font-size="12">.set_ios(400 kHz, 1-bit, 3.3 V)</text>
  <line x1="345" y1="122" x2="560" y2="122" stroke="currentColor" marker-end="url(#ah3)"/><text x="452" y="116" text-anchor="middle" font-size="12">CMD52·CMD5 (SDIO?) → 무응답</text>
  <line x1="345" y1="152" x2="560" y2="152" stroke="currentColor" marker-end="url(#ah3)"/><text x="452" y="146" text-anchor="middle" font-size="12">CMD0 리셋 · CMD8 전압 확인</text>
  <line x1="345" y1="182" x2="560" y2="182" stroke="currentColor" marker-end="url(#ah3)"/><text x="452" y="176" text-anchor="middle" font-size="12">ACMD41 (HCS) 반복 → ready</text>
  <line x1="345" y1="212" x2="560" y2="212" stroke="currentColor" marker-end="url(#ah3)"/><text x="452" y="206" text-anchor="middle" font-size="12">CMD2 CID · CMD3 RCA 받기</text>
  <line x1="345" y1="242" x2="560" y2="242" stroke="currentColor" marker-end="url(#ah3)"/><text x="452" y="236" text-anchor="middle" font-size="12">CMD9 CSD · CMD7 선택</text>
  <line x1="345" y1="272" x2="560" y2="272" stroke="currentColor" marker-end="url(#ah3)"/><text x="452" y="266" text-anchor="middle" font-size="12">ACMD51 SCR · CMD6 HS 전환</text>
  <line x1="345" y1="302" x2="560" y2="302" stroke="currentColor" marker-end="url(#ah3)"/><text x="452" y="296" text-anchor="middle" font-size="12">ACMD6 버스 폭 4-bit</text>
  <line x1="120" y1="332" x2="340" y2="332" stroke="currentColor" marker-end="url(#ah3)"/><text x="232" y="326" text-anchor="middle" font-size="12">.set_ios(50 MHz, 4-bit, HS)</text>
  <line x1="340" y1="352" x2="125" y2="352" stroke="currentColor" stroke-dasharray="3 3" marker-end="url(#ah3)"/><text x="232" y="368" text-anchor="middle" font-size="12">명령마다 .request() → 완료 IRQ</text>
  <rect x="20" y="384" width="200" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="120" y="405" text-anchor="middle" font-size="12">mmc_add_card → mmc 버스</text>
  <rect x="20" y="426" width="200" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="120" y="447" text-anchor="middle" font-size="12">mmc_blk probe → gendisk</text>
  <text x="250" y="410" font-size="12">CID·CSD·SCR → /sys/bus/mmc/devices/mmc0:RCA/</text>
  <text x="250" y="447" font-size="12">/dev/mmcblk0 → CMD18 읽기 (파티션 스캔)</text>
</svg>
```
그림 6 — SD 카드 열거 흐름. 코어가 프로토콜 순서를 결정하고, 호스트 드라이버는 `.set_ios`(버스 상태)와 `.request`(명령 하나)만 수행한다. 명령 순서는 예제 10의 실측 trace와 같다.

### 5.2 예제 10 — 카드 열거를 dynamic debug로 보기

무엇을 확인하나: MMC 코어의 `pr_debug("%s: starting CMD%u arg %08x flags %08x\n", ...)`(core.c `mmc_mrq_pr_debug`)를 dynamic debug로 켜고 `sdhci-pci`를 올려, 카드가 붙는 동안 코어가 보낸 **모든 명령**을 순서대로 뽑는다. QEMU 옵션: `-device sdhci-pci -drive if=none,id=sd0,file=/var/tmp/I11/i11_sd.img,format=raw -device sd-card,drive=sd0`(64 MiB 이미지).

```sh
cd /i11
echo "file drivers/mmc/core/core.c +p" > /sys/kernel/debug/dynamic_debug/control
dmesg -n 1
insmod i11_sdhci_uhs2.ko; insmod i11_sdhci_pci.ko; sleep 2
echo "### enumeration (mmc core dyndbg, CMD sequence)"
dmesg | grep -oE "mmc0: starting CMD[0-9]+ arg [0-9a-f]+" | awk '{print $3, $5}' | head -40 | tr '\n' ';'; echo
dmesg | grep -E "mmc0: (new|SDHCI)|mmcblk0" | sed 's/^\[[ 0-9.]*\] //'
dmesg | grep -cE "mmc0: starting CMD" | sed 's/^/total CMDs logged: /'
echo "### sysfs card"
C=$(ls /sys/bus/mmc/devices/); echo "card=$C"; D=/sys/bus/mmc/devices/$C
for a in type name manfid oemid date serial cid csd scr ocr rca dsr; do printf "%-7s %s\n" $a "$(cat $D/$a 2>/dev/null)"; done
readlink $D/driver; readlink /sys/class/mmc_host/mmc0/device; readlink /sys/class/mmc_host/mmc0/device/driver
echo "### debugfs ios"; cat /sys/kernel/debug/mmc0/ios
echo "### caps"; cat /sys/kernel/debug/mmc0/caps /sys/kernel/debug/mmc0/caps2
echo "### block queue"; Q=/sys/block/mmcblk0/queue
for a in scheduler logical_block_size physical_block_size max_hw_sectors_kb max_segments discard_max_hw_bytes write_zeroes_max_bytes nr_requests rotational; do printf "%-21s %s\n" $a "$(cat $Q/$a)"; done
ls --color=never /sys/block/mmcblk0/mq
```

```text
lab initramfs: 6.18.55-sub up 0.73s
### enumeration (mmc core dyndbg, CMD sequence)
CMD52 00000c00;CMD52 80000c08;CMD0 00000000;CMD8 000001aa;CMD5 00000000;CMD55 00000000;CMD41 00000000;CMD0 00000000;CMD8 000001aa;CMD55 00000000;CMD41 48300000;CMD2 00000000;CMD3 00000000;CMD9 7e560000;CMD7 7e560000;CMD55 7e560000;CMD51 00000000;CMD55 7e560000;CMD13 00000000;CMD6 00fffff0;CMD6 80fffff1;CMD55 7e560000;CMD6 00000002;CMD18 00000000;
mmc0: SDHCI controller on PCI [0000:00:02.0] using ADMA
mmc0: new high speed SD card at address 7e56
mmcblk0: mmc0:7e56 QEMU! 64.0 MiB
total CMDs logged: 24
### sysfs card
card=mmc0:7e56
type    SD
name    QEMU!
manfid  0x0000aa
oemid   0x5859
date    02/2006
serial  0xdeadbeef
cid     aa585951454d552101deadbeef006200
csd     002600325f59e03fffffdfff92600000
scr     0225800000000000
ocr     0x00300000
rca     0x7e56
dsr     0x404
../../../../../../../../bus/mmc/drivers/mmcblk
../../../0000:00:02.0
../../../../../bus/pci/drivers/sdhci-pci
### debugfs ios
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
### caps
0x4000000f
0x00024000
### block queue
scheduler             none [mq-deadline] kyber bfq 
logical_block_size    512
physical_block_size   512
max_hw_sectors_kb     512
max_segments          128
discard_max_hw_bytes  524288
write_zeroes_max_bytes 0
nr_requests           128
rotational            0
0
[    3.176526] reboot: Power down
```

명령 순서 해석(SD Physical Layer Spec 기준, 인자는 위 출력 그대로):

| 순서 | 명령 (arg) | 뜻 |
|---|---|---|
| 1 | CMD52 ×2 | SDIO I/O 리셋 시도 — `mmc_rescan_try_freq()`는 SDIO부터 본다 |
| 2 | CMD0, CMD8 (0x1aa) | GO_IDLE, SEND_IF_COND: 전압 범위 2.7–3.6 V(0x1) + 체크 패턴 0xaa — SD 2.0 이상 카드인지 |
| 3 | CMD5 | SDIO 카드인가? 응답 없음 → SDIO 아님 |
| 4 | CMD55+CMD41 (0) | ACMD41 인자 0 = OCR만 묻기 → SD 카드 확정 |
| 5 | CMD0, CMD8, CMD55+CMD41 (0x48300000) | 다시 초기화. 인자 = CCS(bit30, 대용량 지원) + bit27(`SD_OCR_2T`, SDUC) + 3.2–3.4 V 창. S18R(bit24)이 없다 → 1.8 V UHS 요청 안 함 |
| 6 | CMD2 → CMD3 | ALL_SEND_CID, SEND_RELATIVE_ADDR — 카드가 RCA 0x7e56을 고른다 |
| 7 | CMD9 (0x7e560000) → CMD7 | CSD 읽기, 카드 선택(RCA를 상위 16비트에) |
| 8 | CMD55+CMD51, CMD55+CMD13 | ACMD51 SCR(지원 버스 폭·스펙 버전), ACMD13 SD Status |
| 9 | CMD6 (0x00fffff0) → CMD6 (0x80fffff1) | SWITCH_FUNC 조회(mode 0) 후 설정(mode 1): function group 1 = 1 → High Speed |
| 10 | CMD55+CMD6 (2) | ACMD6 SET_BUS_WIDTH = 4-bit |
| 11 | CMD18 | READ_MULTIPLE_BLOCK — `mmc_blk`가 디스크 등록 후 파티션 테이블을 읽음 |

출력에서 볼 것:

- `new high speed SD card at address 7e56` → `mmcblk0: mmc0:7e56 QEMU! 64.0 MiB`. 카드 장치 이름 `mmc0:7e56` = 호스트 번호 + RCA.
- 드라이버 바인딩 세 단계가 `readlink`로 보인다: 카드 → `mmcblk` 드라이버(mmc 버스), 호스트 → PCI 장치 `0000:00:02.0` → `sdhci-pci` 드라이버(PCI 버스). G1에서 본 "컨트롤러 드라이버 + 그 버스 위 클라이언트 드라이버" 구조 그대로다.
- `ios`: 요청 클럭 50 MHz, 실제 26 MHz(SDHCI 분주기 한계 — QEMU의 base clock 52 MHz를 2로 나눔), 4-bit, `sd high-speed`, 3.3 V. 이 파일은 `struct mmc_ios`의 현재 값으로, **`.set_ios`가 마지막으로 받은 것**이다. 실보드에서 "카드가 HS200으로 안 올라간다"를 디버그할 때 가장 먼저 보는 파일이다.
- `caps 0x4000000f` = 4-bit(bit0) + MMC HS(bit1) + SD HS(bit2) + SDIO IRQ(bit3) + CMD23(bit30). `caps2 0x00024000` = `MMC_CAP2_NO_PRESCAN_POWERUP`(bit14) + `MMC_CAP2_SDIO_IRQ_NOTHREAD`(bit17). UHS·HS200 비트가 없다 — QEMU의 `sdhci-pci`는 기본으로 SD 2.0 호스트다(`sd-spec-version=3,uhs=1`로 바꿔도 이 노트의 시험에서는 여전히 `sd high-speed`로 붙었다 — 실행 결과).
- 블록 큐: **하드웨어 큐 1개**(`mq/0`)라서 기본 스케줄러가 `mq-deadline`이다. `max_hw_sectors_kb 512`, `max_segments 128`은 SDHCI ADMA 디스크립터 한계에서 온 값이다. SD의 discard(ERASE)는 지원(`discard_max_hw_bytes`), write_zeroes는 없음.

### 5.3 eMMC는 순서가 다르다 (같은 스크립트, `-device emmc`)

같은 `g_mmc.sh`를 QEMU 옵션만 바꿔 돌렸다: `-device sdhci-pci -drive if=none,id=e0,file=/var/tmp/I11/i11_emmc.img,format=raw -device emmc,drive=e0`(64 MiB). 앞 6줄:

```text
lab initramfs: 6.18.55-sub up 0.72s
### enumeration (mmc core dyndbg, CMD sequence)
CMD52 00000c00;CMD52 80000c08;CMD0 00000000;CMD8 000001aa;CMD5 00000000;CMD55 00000000;CMD55 00000000;CMD55 00000000;CMD55 00000000;CMD1 00000000;CMD1 40ffff00;CMD0 00000000;CMD1 40300000;CMD2 00000000;CMD3 00010000;CMD9 00010000;CMD7 00010000;CMD8 00000000;CMD6 03af0101;CMD13 00010000;CMD6 03b90101;CMD13 00010000;CMD6 03b70101;CMD13 00010000;CMD8 00000000;CMD23 00000008;CMD18 00000000;
mmc0: SDHCI controller on PCI [0000:00:02.0] using ADMA
mmc0: new high speed MMC card at address 0001
mmcblk0: mmc0:0001 QEMU!! 64.0 MiB
```

SD와 다른 점: ACMD41(CMD55)이 4번 실패한 뒤 **CMD1**(SEND_OP_COND)로 OCR을 묻고(인자 0 → 0x40ffff00), 다시 CMD0 후 실제 값 0x40300000(bit30 sector 주소 모드 + 3.2–3.4 V)으로 협상해 MMC 카드임을 확인하고, CMD3은 **호스트가 RCA를 정해 준다**(arg 0x00010000 = RCA 1). CMD7 다음 **CMD8 = SEND_EXT_CSD**(eMMC에서 CMD8은 512 B EXT_CSD 읽기다 — SD의 CMD8과 이름이 다르다), 그리고 `CMD6`(SWITCH) 세 번: 인자 `0x03af0101`은 access=3(write byte), index=0xaf=175 `ERASE_GROUP_DEF`=1, `0x03b90101`은 index 185 `HS_TIMING`=1(High Speed), `0x03b70101`은 index 183 `BUS_WIDTH`=1(4-bit). 각 SWITCH 뒤에 `CMD13`(SEND_STATUS)으로 busy가 끝났는지 확인하고, 버스 폭을 바꾼 뒤 EXT_CSD를 **한 번 더 읽어**(두 번째 CMD8) 새 버스 폭에서 데이터가 맞는지 비교한다. 마지막 `CMD23 (8)` + `CMD18`은 SET_BLOCK_COUNT로 8블록 읽기 — eMMC는 CMD23을 지원하므로 `mmc_blk`가 open-ended 대신 정확한 블록 수를 미리 알린다. `ios`는 52 MHz, `mmc high-speed`였다.

### 5.4 타이밍 모드와 튜닝 (실행하지 않음 — QEMU가 UHS/HS200을 흉내 내지 않는다, 스펙 기반 정리)

| 모드 | 매체 | 클럭 / 전압 | 데이터 | 튜닝 |
|---|---|---|---|---|
| Default / Legacy | SD·eMMC | ≤25 / ≤26 MHz, 3.3 V | SDR | 없음 |
| High Speed | SD·eMMC | 50 / 52 MHz | SDR | 없음 |
| SDR50 · SDR104 (UHS-I) | SD | 100 · 208 MHz, 1.8 V | SDR | CMD19 (SDR104 필수, SDR50 선택) |
| DDR50 · DDR52 | SD · eMMC | 50 · 52 MHz | DDR | 없음 |
| HS200 | eMMC | 200 MHz, 1.8/1.2 V | SDR, 8-bit | CMD21 |
| HS400 · HS400ES | eMMC | 200 MHz | DDR(400 MB/s), data strobe | HS200에서 튜닝 후 전환 · ES는 strobe로 튜닝 생략 |

튜닝은 호스트가 **샘플링 클럭 위상을 바꿔 가며** 카드가 보내는 알려진 패턴(tuning block)을 읽어 보고 통과하는 위상 창의 가운데를 고르는 과정이다. 코어가 `mmc_execute_tuning()` → 호스트의 `.execute_tuning(host, MMC_SEND_TUNING_BLOCK_HS200)`을 부르고, SDHCI 라이브러리는 표준 튜닝 절차(`sdhci_execute_tuning`)를 갖고 있으며 SoC 드라이버가 delay line을 직접 조절하는 경우가 많다. 1.8 V 전환은 `.start_signal_voltage_switch`가 레귤레이터(DT의 `vqmmc-supply`)와 패드 전압을 바꾼다. 실무에서 "HS200 tuning failed" → 보드 신호 무결성(길이, 임피던스), 잘못된 `vqmmc` 레귤레이터, 패드 drive strength(pinctrl) 순으로 의심한다. DT 속성: `bus-width = <8>`, `mmc-hs200-1_8v`, `mmc-hs400-1_8v`, `non-removable`, `cd-gpios`, `max-frequency`.

### 5.5 예제 11 — `mmc_test`로 호스트 드라이버 검증

무엇을 확인하나: 카드를 `mmcblk`에서 떼어(`unbind`) `mmc_test` 드라이버에 붙이고(`bind`), debugfs로 테스트 케이스를 돌린다. 새 SoC의 호스트 드라이버를 bring-up할 때 쓰는 커널 내장 테스트다(**카드 내용을 덮어쓴다** — 실보드에서는 버릴 카드로).

```sh
cd /i11
dmesg -n 1
insmod i11_sdhci_uhs2.ko; insmod i11_sdhci_pci.ko; sleep 2
C=$(ls /sys/bus/mmc/devices/)
modprobe mmc_test
echo $C > /sys/bus/mmc/drivers/mmcblk/unbind
echo $C > /sys/bus/mmc/drivers/mmc_test/bind
readlink /sys/bus/mmc/devices/$C/driver
T=/sys/kernel/debug/mmc0/$C
ls --color=never $T; head -12 $T/testlist; wc -l < $T/testlist
for t in 1 2 3 4 39; do echo $t > $T/test; done
dmesg | grep -E "mmc_test|mmc0: (Test|Result|Starting|Transfer)" | sed 's/^\[[ 0-9.]*\] //' | tail -30
```

```text
lab initramfs: 6.18.55-sub up 0.74s
../../../../../../../../bus/mmc/drivers/mmc_test
quirks    state     test      testlist
0:	Run all tests
1:	Basic write (no data verification)
2:	Basic read (no data verification)
3:	Basic write (with data verification)
4:	Basic read (with data verification)
5:	Multi-block write
6:	Multi-block read
7:	Power of two block writes
8:	Power of two block reads
9:	Weird sized block writes
10:	Weird sized block reads
11:	Badly aligned write
53
mmc0: Starting tests of card mmc0:2928...
mmc0: Test case 1. Basic write (no data verification)...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:2928...
mmc0: Test case 2. Basic read (no data verification)...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:2928...
mmc0: Test case 3. Basic write (with data verification)...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:2928...
mmc0: Test case 4. Basic read (with data verification)...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:2928...
mmc0: Test case 39. Read performance with blocking req 4k to 4MB...
mmc0: Transfer of 8192 x 8 sectors (8192 x 4 KiB) took 11.867559616 seconds (2827 kB/s, 2761 KiB/s, 690.28 IOPS, sg_len 1)
mmc0: Transfer of 4096 x 16 sectors (4096 x 8 KiB) took 10.612848064 seconds (3161 kB/s, 3087 KiB/s, 385.94 IOPS, sg_len 1)
mmc0: Transfer of 2048 x 32 sectors (2048 x 16 KiB) took 9.857519552 seconds (3403 kB/s, 3324 KiB/s, 207.76 IOPS, sg_len 1)
mmc0: Transfer of 1024 x 64 sectors (1024 x 32 KiB) took 9.469425504 seconds (3543 kB/s, 3460 KiB/s, 108.13 IOPS, sg_len 1)
mmc0: Transfer of 512 x 128 sectors (512 x 64 KiB) took 9.236615584 seconds (3632 kB/s, 3547 KiB/s, 55.43 IOPS, sg_len 1)
mmc0: Transfer of 256 x 256 sectors (256 x 128 KiB) took 9.184190704 seconds (3653 kB/s, 3567 KiB/s, 27.87 IOPS, sg_len 2)
mmc0: Transfer of 128 x 512 sectors (128 x 256 KiB) took 9.101514480 seconds (3686 kB/s, 3600 KiB/s, 14.06 IOPS, sg_len 4)
mmc0: Transfer of 64 x 1024 sectors (64 x 512 KiB) took 9.194433216 seconds (3649 kB/s, 3563 KiB/s, 6.96 IOPS, sg_len 8)
mmc0: Transfer of 64 x 1024 sectors (64 x 512 KiB) took 8.667256032 seconds (3871 kB/s, 3780 KiB/s, 7.38 IOPS, sg_len 8)
mmc0: Transfer of 64 x 1024 sectors (64 x 512 KiB) took 8.716638016 seconds (3849 kB/s, 3759 KiB/s, 7.34 IOPS, sg_len 8)
mmc0: Result: OK
mmc0: Tests completed.
[   99.076822] reboot: Power down
```

출력에서 볼 것: 한 장치가 **드라이버를 바꿔 끼울 수 있다**(`mmcblk` → `mmc_test`) — G2의 bind/unbind가 mmc 버스에서도 똑같이 동작한다. 테스트 39는 전송 크기별 순차 읽기 성능을 재는데, 4 KiB에서 690 IOPS, 512 KiB에서 3.8 MB/s다(TCG 위 QEMU 에뮬레이션 수치, 실제 SD와 무관). `sg_len`은 요청 하나가 몇 개의 scatter-gather 조각(→ ADMA 디스크립터)으로 나뉘었는지다.

### 5.6 SoC SDHCI 드라이버의 모양 (설명용 뼈대, 빌드하지 않음)

```c
/* sketch only - real examples: drivers/mmc/host/sdhci-of-arasan.c, sdhci-esdhc-imx.c */
static const struct sdhci_ops foo_sdhci_ops = {
	.set_clock		= foo_set_clock,	/* vendor clock divider / DLL */
	.set_bus_width		= sdhci_set_bus_width,	/* standard */
	.reset			= sdhci_reset,
	.set_uhs_signaling	= sdhci_set_uhs_signaling,
	.platform_execute_tuning = foo_execute_tuning,	/* vendor delay line */
};
static const struct sdhci_pltfm_data foo_pdata = {
	.ops = &foo_sdhci_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
};
static int foo_probe(struct platform_device *pdev)
{
	struct sdhci_host *host = sdhci_pltfm_init(pdev, &foo_pdata, 0);	/* maps regs, irq */
	if (IS_ERR(host)) return PTR_ERR(host);
	/* clocks, pinctrl states (default / state_uhs), regulators ... */
	mmc_of_parse(host->mmc);	/* bus-width, cd-gpios, mmc-hs200-1_8v, non-removable ... */
	return sdhci_add_host(host);	/* -> mmc_add_host() -> mmc_rescan() */
}
```

`sdhci_add_host()` 안에서 SDHCI 라이브러리가 `mmc_host_ops`(`sdhci_request`, `sdhci_set_ios`, `sdhci_get_cd` …)를 채워 준다. 그래서 SoC 드라이버 작성자는 `mmc_host_ops`를 직접 만들지 않고 `sdhci_ops`의 몇 칸만 채운다. eMMC CQE(command queue engine)가 있으면 `cqhci_init()`으로 붙여 **최대 32개 태그**를 하드웨어 큐에 올린다 — blk-mq의 큐 깊이가 1에서 32로 바뀌는 지점이다.

---

## 6. block 계층과 blk-mq — 모든 managed flash가 서는 바닥

### 6.1 구조체 지도

| 구조체 | 뜻 | Don의 NVMe 펌웨어 관점 |
|---|---|---|
| `struct gendisk` | 디스크 하나(이름, 용량, 파티션, fops) | 네임스페이스 하나 |
| `struct request_queue` | 그 디스크의 큐 + `queue_limits` + 스케줄러 | 호스트 쪽 I/O 큐 설정 |
| `struct bio` | 파일시스템이 내려보내는 I/O 단위: 시작 섹터 + `bio_vec`(page, offset, len) 배열 | 아직 커맨드가 되기 전의 요청 |
| `struct request` | 드라이버에 넘어가는 단위: 인접 bio들을 합친 것, 태그 하나 | SQ 엔트리 하나(CID = 태그) |
| `struct blk_mq_tag_set` | 하드웨어 큐 수·깊이·ops·`cmd_size` | SQ 개수 × 깊이 |
| `struct blk_mq_hw_ctx` (hctx) | 하드웨어 큐 하나 + 그 큐를 쓰는 CPU 목록 | SQ/CQ 쌍 하나 |
| `struct blk_mq_ctx` | CPU마다 있는 소프트웨어 큐 | — |
| `struct blk_mq_ops` | `.queue_rq`, `.complete`, `.timeout`, `.poll`, `.init_hctx`, `.queue_rqs`, `.commit_rqs` | 드라이버 진입점 |

```svg
<svg viewBox="0 0 680 450" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="20" font-size="13" font-weight="bold">제출 (submitting CPU, 프로세스 문맥)</text>
  <rect x="20" y="30" width="150" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="95" y="48" text-anchor="middle" font-size="12">FS / O_DIRECT</text><text x="95" y="63" text-anchor="middle" font-size="12">submit_bio(bio)</text>
  <rect x="190" y="30" width="140" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="260" y="48" text-anchor="middle" font-size="12">split · merge</text><text x="260" y="63" text-anchor="middle" font-size="12">queue_limits 적용</text>
  <rect x="350" y="30" width="140" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="420" y="48" text-anchor="middle" font-size="12">태그 할당</text><text x="420" y="63" text-anchor="middle" font-size="12">bio → request</text>
  <rect x="510" y="30" width="150" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="585" y="48" text-anchor="middle" font-size="12">plug 목록</text><text x="585" y="63" text-anchor="middle" font-size="12">(blk_start_plug)</text>
  <line x1="170" y1="50" x2="188" y2="50" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="330" y1="50" x2="348" y2="50" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="490" y1="50" x2="508" y2="50" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="20" y="100" width="300" height="46" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="170" y="119" text-anchor="middle" font-size="12">I/O 스케줄러 (선택)</text><text x="170" y="137" text-anchor="middle" font-size="12">none · mq-deadline · kyber · bfq</text>
  <rect x="360" y="100" width="300" height="46" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="510" y="119" text-anchor="middle" font-size="12">hctx 선택: CPU → hw queue 맵</text><text x="510" y="137" text-anchor="middle" font-size="12">(mq/N/cpu_list)</text>
  <line x1="585" y1="70" x2="510" y2="98" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="360" y1="123" x2="322" y2="123" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="190" y="176" width="300" height="46" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="340" y="195" text-anchor="middle" font-size="12">드라이버 .queue_rq(hctx, bd)</text><text x="340" y="213" text-anchor="middle" font-size="12">blk_mq_start_request → DMA 설정 → doorbell</text>
  <line x1="170" y1="146" x2="250" y2="174" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="146" x2="430" y2="174" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="520" y="204" font-size="12">스케줄러 none이면</text><text x="520" y="220" text-anchor="start" font-size="12">바로 issue_directly</text>
  <rect x="190" y="246" width="300" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340" y="268" text-anchor="middle" font-size="12">장치가 일한다 (i11_blk: hrtimer)</text>
  <line x1="340" y1="222" x2="340" y2="244" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="20" y="306" font-size="13" font-weight="bold">완료 (IRQ 문맥 → 필요하면 제출 CPU로)</text>
  <rect x="20" y="318" width="200" height="46" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="120" y="337" text-anchor="middle" font-size="12">IRQ 핸들러</text><text x="120" y="355" text-anchor="middle" font-size="12">blk_mq_complete_request</text>
  <rect x="240" y="318" width="200" height="46" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340" y="337" text-anchor="middle" font-size="12">다른 CPU면 IPI 또는</text><text x="340" y="355" text-anchor="middle" font-size="12">BLOCK_SOFTIRQ</text>
  <rect x="460" y="318" width="200" height="46" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="560" y="337" text-anchor="middle" font-size="12">.complete(rq)</text><text x="560" y="355" text-anchor="middle" font-size="12">blk_mq_end_request</text>
  <line x1="340" y1="280" x2="140" y2="316" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="220" y1="341" x2="238" y2="341" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="440" y1="341" x2="458" y2="341" stroke="currentColor" marker-end="url(#ah4)"/>
  <path d="M 120 364 Q 340 400 556 366" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
  <text x="340" y="404" text-anchor="middle" font-size="12">같은 CPU(또는 캐시 공유)면 바로 .complete 호출</text>
  <rect x="190" y="414" width="300" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340" y="434" text-anchor="middle" font-size="12">bio_endio → 대기 중인 read()가 깨어남</text>
  <line x1="560" y1="364" x2="470" y2="412" stroke="currentColor" marker-end="url(#ah4)"/>
</svg>
```
그림 7 — blk-mq 요청 경로. 위: bio가 request가 되어 하드웨어 큐(hctx)를 거쳐 `.queue_rq`로 간다. 아래: 완료는 IRQ에서 `blk_mq_complete_request()`로 시작해, 제출 CPU와 다르면 IPI/softirq로 넘긴 뒤 드라이버의 `.complete`가 `blk_mq_end_request()`를 부른다. 예제 14의 ftrace가 이 순서를 그대로 보여 준다.

### 6.2 queue limits — 드라이버가 block 계층에 거는 약속

6.9 이후 드라이버는 `struct queue_limits`를 채워 `blk_mq_alloc_disk(&set, &lim, data)`에 넘긴다(G1 참고). 주요 필드:

| 필드 | sysfs (`/sys/block/X/queue/`) | 뜻 | 예 |
|---|---|---|---|
| `logical_block_size` | `logical_block_size` | 주소 지정 최소 단위 | 512 (4Kn 장치는 4096) |
| `physical_block_size` | `physical_block_size` | 장치 내부 원자 쓰기 단위 — 이보다 작은 쓰기는 장치 안에서 RMW | NAND page / 4096 |
| `io_min`, `io_opt` | `minimum_io_size`, `optimal_io_size` | 성능상 권장 단위 | RAID stripe, eMMC 8 KiB 등 |
| `max_hw_sectors` | `max_hw_sectors_kb` | 요청 하나의 최대 크기(HW 한계) | DMA 디스크립터 한계 |
| `max_segments`, `max_segment_size` | 같은 이름 | scatter-gather 항목 수·크기 | SDHCI ADMA 128 |
| `max_hw_discard_sectors` | `discard_max_hw_bytes` | discard(TRIM/ERASE) 지원·최대 크기, 0이면 미지원 | eMMC ERASE/TRIM |
| `max_write_zeroes_sectors` | `write_zeroes_max_bytes` | "0으로 채우기"를 데이터 전송 없이 | NVMe Write Zeroes |
| `features` | `rotational`, `write_cache`, `fua` | `BLK_FEAT_WRITE_CACHE` (→ FLUSH 요청이 온다), `BLK_FEAT_FUA`, `BLK_FEAT_ROTATIONAL` … | 휘발성 캐시가 있는 eMMC·SSD |

Don의 관점: `BLK_FEAT_WRITE_CACHE`를 켜는 드라이버는 **FLUSH 요청을 받아 장치 캐시를 비울 책임**을 진다(NVMe Flush, eMMC CMD6 FLUSH_CACHE). 이 약속을 어기면 `fsync()`가 거짓말을 한다(J5).

### 6.3 I/O 스케줄러

| 스케줄러 | 아이디어 | 언제 |
|---|---|---|
| `none` | 스케줄링 없음, 바로 hctx로 | NVMe 같은 빠른 다중 큐 장치 (기본) |
| `mq-deadline` | 읽기/쓰기 FIFO + 마감시간, 섹터 정렬, 읽기 우선 | 단일 큐 장치(eMMC/SD, SATA)의 기본 |
| `kyber` | 읽기·동기 쓰기의 목표 지연을 정하고 큐 깊이를 조절 | 빠른 장치에서 지연 제어 |
| `bfq` | 프로세스별 예산 기반 공정성, 대화형 지연 우선 | 데스크톱·느린 장치, cgroup I/O 비율 |

기본값은 커널이 정한다: 하드웨어 큐가 하나면 `mq-deadline`, 여러 개면 `none`(예제 10의 mmcblk0 = `[mq-deadline]`, 예제 13의 i11_blk0(2개) = `[none]`, NVMe = `[none]`). 바꾸는 법: `echo bfq > /sys/block/X/queue/scheduler`, 영구 설정은 udev 규칙.

### 6.4 plug/unplug와 merge

프로세스가 여러 bio를 연속으로 내보낼 때 커널은 **plug**(`blk_start_plug()` … `blk_finish_plug()`, 태스크마다 하나)에 request를 잠시 모은다. plug 안에서 인접한 bio는 하나의 request로 합쳐지고(merge), plug를 빼는 순간(또는 태스크가 잠들 때) 모인 request를 한꺼번에 드라이버로 보낸다 — 드라이버가 `.queue_rqs`(복수형)를 구현하면 목록째 받는다(null_blk·NVMe가 구현). `.commit_rqs`는 "마지막 request까지 큐에 넣었으니 doorbell을 울려라" 신호다. NVMe 펌웨어 쪽에서 보면 **doorbell 쓰기 횟수를 줄이는 배칭**이다.

### 6.5 예제 12 — null_blk로 블록 계층 자체의 비용 재기

`null_blk`는 데이터를 저장하지 않는(기본값) **참조 blk-mq 드라이버**다. 파라미터로 완료 방식과 큐 구조를 바꿀 수 있어 block 계층 오버헤드를 재는 표준 도구다. 

- `irqmode=0`: `.queue_rq` 안에서 바로 완료(인라인) · `1`: softirq로 완료(기본) · `2`: hrtimer로 `completion_nsec` 뒤 완료(장치 지연 흉내)
- `submit_queues=N`: 하드웨어 큐 수, `hw_queue_depth`: 큐 깊이(기본 64), `poll_queues`: 폴링용 큐(기본 1)
- `queue_mode=0`(bio 기반)은 더 이상 지원되지 않는다 — 출력에서 확인한다

측정 도구: N개 스레드가 각자 QD1로 4 KiB 랜덤 `pread()`를 `O_DIRECT`로 4000번씩 하고 지연 분포를 낸다(fio가 게스트에 없어서 직접 작성).

```c
/* i11_bench: N threads x random 4 KiB O_DIRECT preads (QD1 each) -> IOPS, mean/p99 latency */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#define NOPS 4000			/* per thread */
static int fd, nthr;
static unsigned long long nsec_sectors;
static double lat[16][NOPS];

static double now_us(void)
{
	struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
static void *worker(void *arg)
{
	long id = (long)arg;
	void *buf;
	unsigned seed = 1234 + id;
	if (posix_memalign(&buf, 4096, 4096)) { perror("posix_memalign"); exit(1); }
	for (int i = 0; i < NOPS; i++) {
		off_t off = (off_t)(rand_r(&seed) % (nsec_sectors / 8)) * 4096;
		double t0 = now_us();
		if (pread(fd, buf, 4096, off) != 4096) { perror("pread"); exit(1); }
		lat[id][i] = now_us() - t0;
	}
	free(buf);
	return NULL;
}
static int cmp(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y);
}
int main(int argc, char **argv)
{
	pthread_t th[16];
	unsigned long long bytes;
	if (argc < 3) { fprintf(stderr, "usage: %s /dev/X threads [label]\n", argv[0]); return 1; }
	nthr = atoi(argv[2]);
	if (nthr < 1 || nthr > 16) { fprintf(stderr, "threads 1..16\n"); return 1; }
	fd = open(argv[1], O_RDONLY | O_DIRECT);	/* O_DIRECT: bypass page cache */
	if (fd < 0 || ioctl(fd, BLKGETSIZE64, &bytes)) { perror(argv[1]); exit(1); }
	nsec_sectors = bytes / 512;
	double t0 = now_us();
	for (long i = 0; i < nthr; i++) pthread_create(&th[i], NULL, worker, (void *)i);
	for (int i = 0; i < nthr; i++) pthread_join(th[i], NULL);
	double el = now_us() - t0, sum = 0;
	qsort(lat, nthr * NOPS, sizeof(double), cmp);	/* rows are contiguous */
	for (int i = 0; i < nthr * NOPS; i++) sum += lat[0][i];
	printf("%-22s thr=%d IOPS=%7.0f mean=%6.1fus p50=%6.1fus p99=%7.1fus\n",
	       argc > 3 ? argv[3] : argv[1], nthr, nthr * NOPS / (el / 1e6),
	       sum / (nthr * NOPS), lat[0][nthr * NOPS / 2], lat[0][nthr * NOPS * 99 / 100]);
	return 0;
}
```

```sh
cd /i11
dmesg -n 1
B=./i11_bench
nb() { rmmod null_blk 2>/dev/null; modprobe null_blk gb=1 "$@" || echo "modprobe failed: $*"; }
echo "### defaults"
nb
ls --color=never /sys/block/nullb0/mq; cat /sys/block/nullb0/queue/scheduler /sys/block/nullb0/queue/nr_requests /sys/block/nullb0/queue/max_hw_sectors_kb /sys/block/nullb0/queue/logical_block_size
cat /sys/module/null_blk/parameters/irqmode /sys/module/null_blk/parameters/completion_nsec /sys/module/null_blk/parameters/submit_queues
echo "### queue_mode=0 (bio) request"
nb queue_mode=0; dmesg | grep -i null_blk | tail -2 | sed 's/^\[[ 0-9.]*\] //'
echo "### irqmode sweep (1 thread, sched none)"
for m in "irqmode=0" "irqmode=1" "irqmode=2 completion_nsec=10000" "irqmode=2 completion_nsec=50000" "irqmode=2 completion_nsec=200000"; do
  nb $m; echo none > /sys/block/nullb0/queue/scheduler; $B /dev/nullb0 1 "$(echo $m | tr ' ' ,)"; done
echo "### submit_queues x threads (irqmode=2 50us, sched none)"
for q in 1 2; do for t in 1 2 4 8; do nb irqmode=2 completion_nsec=50000 submit_queues=$q hw_queue_depth=64
  echo none > /sys/block/nullb0/queue/scheduler; $B /dev/nullb0 $t "sq=$q"; done; done
echo "### schedulers (irqmode=2 50us, submit_queues=2, 4 threads)"
for s in none mq-deadline kyber bfq; do nb irqmode=2 completion_nsec=50000 submit_queues=2
  echo $s > /sys/block/nullb0/queue/scheduler; cat /sys/block/nullb0/queue/scheduler; $B /dev/nullb0 4 "$s"; done
echo "### schedulers (irqmode=0, 1 thread)"
for s in none mq-deadline kyber bfq; do nb irqmode=0
  echo $s > /sys/block/nullb0/queue/scheduler; $B /dev/nullb0 1 "$s"; done
```

```text
lab initramfs: 6.18.55-sub up 1.11s
### defaults
0  1
[none] mq-deadline kyber bfq 
64
127
512
1
10000
1
### queue_mode=0 (bio) request
null_blk: disk nullb0 created
null_blk: module loaded
### irqmode sweep (1 thread, sched none)
irqmode=0              thr=1 IOPS=  19919 mean=  46.7us p50=  41.5us p99=  134.4us
irqmode=1              thr=1 IOPS=  21074 mean=  45.8us p50=  42.4us p99=  119.5us
irqmode=2,completion_nsec=10000 thr=1 IOPS=   7744 mean= 127.3us p50= 120.3us p99=  249.7us
irqmode=2,completion_nsec=50000 thr=1 IOPS=   5136 mean= 192.7us p50= 187.9us p99=  289.8us
irqmode=2,completion_nsec=200000 thr=1 IOPS=   2149 mean= 462.6us p50= 454.0us p99=  627.3us
### submit_queues x threads (irqmode=2 50us, sched none)
sq=1                   thr=1 IOPS=   4968 mean= 199.0us p50= 190.5us p99=  426.9us
sq=1                   thr=2 IOPS=   8049 mean= 243.5us p50= 228.5us p99=  525.2us
sq=1                   thr=4 IOPS=  12399 mean= 315.6us p50= 289.5us p99=  570.4us
sq=1                   thr=8 IOPS=  17411 mean= 441.1us p50= 367.0us p99= 1280.3us
sq=2                   thr=1 IOPS=   5138 mean= 192.8us p50= 187.7us p99=  295.3us
sq=2                   thr=2 IOPS=   7936 mean= 247.7us p50= 233.6us p99=  520.6us
sq=2                   thr=4 IOPS=  12926 mean= 302.9us p50= 290.6us p99=  548.5us
sq=2                   thr=8 IOPS=  15333 mean= 504.4us p50= 407.2us p99= 1697.8us
### schedulers (irqmode=2 50us, submit_queues=2, 4 threads)
[none] mq-deadline kyber bfq 
none                   thr=4 IOPS=  13116 mean= 299.2us p50= 286.0us p99=  556.6us
none [mq-deadline] kyber bfq 
mq-deadline            thr=4 IOPS=   9347 mean= 419.6us p50= 325.0us p99= 1424.4us
none mq-deadline [kyber] bfq 
kyber                  thr=4 IOPS=  12462 mean= 313.4us p50= 300.6us p99=  554.9us
none mq-deadline kyber [bfq] 
bfq                    thr=4 IOPS=  11619 mean= 338.6us p50= 325.0us p99=  600.1us
### schedulers (irqmode=0, 1 thread)
none                   thr=1 IOPS=  21381 mean=  45.1us p50=  41.9us p99=  105.5us
mq-deadline            thr=1 IOPS=  18733 mean=  51.7us p50=  48.6us p99=  116.2us
kyber                  thr=1 IOPS=  18185 mean=  53.3us p50=  49.9us p99=  116.9us
bfq                    thr=1 IOPS=  13960 mean=  69.8us p50=  64.1us p99=  198.2us
[   25.109601] reboot: Power down
```

(`-smp 4` 게스트. `queue_mode=0`의 경고 줄은 `tail -2`에 걸리지 않았다 — 다음 예제 13의 첫 줄에서 다시 확인했다: `null_blk: BIO-based IO path is no longer available, using blk-mq instead.`)

출력에서 볼 것:

- 기본 `null_blk`: `mq/`에 큐 2개(`0 1`) — `submit_queues=1` + `poll_queues=1`. 스케줄러 기본 `none`, `irqmode` 기본 1(softirq), `completion_nsec` 기본 10000.
- **인라인/softirq 완료 ≈ 46 us, hrtimer 10/50/200 us → 127/193/463 us.** 장치 지연에 대략 고정 오버헤드가 더해진다. 여기서 TCG 에뮬레이션의 타이머 해상도 때문에 "10 us 지연"이 실제로는 80 us 가까이 더해진다 — 절대값이 아니라 기울기를 볼 것.
- QD1 스레드 수를 늘리면 IOPS가 늘지만(5k → 17k), 이 측정에서는 `submit_queues` 1 vs 2의 차이가 오차 수준이다. 4 vCPU TCG에서 병목이 하드웨어 큐 락이 아니라 CPU 에뮬레이션이기 때문이다. 실제 NVMe에서 hctx를 CPU 수만큼 두는 이유(큐 락 경합·캐시 라인 바운싱 제거)는 이런 환경에서는 잘 안 보인다는 것도 하나의 교훈이다.
- 스케줄러: 1 스레드 인라인 완료에서 none 21.4k > mq-deadline 18.7k ≈ kyber 18.2k > bfq 14.0k — **스케줄러 자체의 CPU 비용**이다. 4 스레드에서는 mq-deadline이 가장 낮고(9.3k) p99가 1.4 ms로 튀었다. 빠른 장치에 스케줄러를 붙이면 손해라는 것이 NVMe 기본값 `none`의 이유다.

```svg
<svg viewBox="0 0 660 340" xmlns="http://www.w3.org/2000/svg">
<text x="330" y="22" text-anchor="middle" font-size="14" font-weight="bold">I/O 스케줄러별 IOPS (null_blk, 4 KiB 랜덤 읽기)</text>
<line x1="80" y1="280" x2="630" y2="280" stroke="currentColor"/>
<line x1="80" y1="280" x2="80" y2="60" stroke="currentColor"/>
<line x1="76" y1="280.0" x2="80" y2="280.0" stroke="currentColor"/>
<line x1="80" y1="280.0" x2="630" y2="280.0" stroke="#888" stroke-opacity="0.3"/>
<text x="72" y="284.0" text-anchor="end" font-size="12">0</text>
<line x1="76" y1="236.0" x2="80" y2="236.0" stroke="currentColor"/>
<line x1="80" y1="236.0" x2="630" y2="236.0" stroke="#888" stroke-opacity="0.3"/>
<text x="72" y="240.0" text-anchor="end" font-size="12">5000</text>
<line x1="76" y1="192.0" x2="80" y2="192.0" stroke="currentColor"/>
<line x1="80" y1="192.0" x2="630" y2="192.0" stroke="#888" stroke-opacity="0.3"/>
<text x="72" y="196.0" text-anchor="end" font-size="12">10000</text>
<line x1="76" y1="148.0" x2="80" y2="148.0" stroke="currentColor"/>
<line x1="80" y1="148.0" x2="630" y2="148.0" stroke="#888" stroke-opacity="0.3"/>
<text x="72" y="152.0" text-anchor="end" font-size="12">15000</text>
<line x1="76" y1="104.0" x2="80" y2="104.0" stroke="currentColor"/>
<line x1="80" y1="104.0" x2="630" y2="104.0" stroke="#888" stroke-opacity="0.3"/>
<text x="72" y="108.0" text-anchor="end" font-size="12">20000</text>
<line x1="76" y1="60.0" x2="80" y2="60.0" stroke="currentColor"/>
<line x1="80" y1="60.0" x2="630" y2="60.0" stroke="#888" stroke-opacity="0.3"/>
<text x="72" y="64.0" text-anchor="end" font-size="12">25000</text>
<text x="36" y="170.0" font-size="12" text-anchor="middle" transform="rotate(-90 36 170.0)">IOPS</text>
<rect x="107.5" y="91.8" width="38.5" height="188.2" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="126.8" y="86.8" text-anchor="middle" font-size="12">21.4k</text>
<rect x="148.8" y="164.6" width="38.5" height="115.4" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="168.0" y="159.6" text-anchor="middle" font-size="12">13.1k</text>
<text x="148.8" y="298" text-anchor="middle" font-size="12">none</text>
<rect x="245.0" y="115.1" width="38.5" height="164.9" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="264.2" y="110.1" text-anchor="middle" font-size="12">18.7k</text>
<rect x="286.2" y="197.7" width="38.5" height="82.3" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="305.5" y="192.7" text-anchor="middle" font-size="12">9.3k</text>
<text x="286.2" y="298" text-anchor="middle" font-size="12">mq-deadline</text>
<rect x="382.5" y="120.0" width="38.5" height="160.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="401.8" y="115.0" text-anchor="middle" font-size="12">18.2k</text>
<rect x="423.8" y="170.3" width="38.5" height="109.7" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="443.0" y="165.3" text-anchor="middle" font-size="12">12.5k</text>
<text x="423.8" y="298" text-anchor="middle" font-size="12">kyber</text>
<rect x="520.0" y="157.2" width="38.5" height="122.8" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="539.2" y="152.2" text-anchor="middle" font-size="12">14.0k</text>
<rect x="561.2" y="177.8" width="38.5" height="102.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="580.5" y="172.8" text-anchor="middle" font-size="12">11.6k</text>
<text x="561.2" y="298" text-anchor="middle" font-size="12">bfq</text>
<rect x="100" y="44" width="14" height="12" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/><text x="120" y="55" font-size="12">irqmode=0, 1 스레드 (스케줄러 자체 비용)</text>
<rect x="100" y="62" width="14" height="12" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/><text x="120" y="73" font-size="12">irqmode=2 50 us, submit_queues=2, 4 스레드</text>
<text x="355.0" y="318" text-anchor="middle" font-size="12">QEMU TCG 위 측정이라 절대값보다 상대 순서를 볼 것</text>
</svg>
```
그림 8 — 스케줄러별 IOPS(null_blk, 이 노트의 실측). 파랑은 장치 지연이 0일 때(순수 소프트웨어 비용), 주황은 50 us 장치 + 4 스레드.

### 6.6 예제 13 — 직접 만든 blk-mq 드라이버 `i11_blk`

G1의 `g1_blk`는 `.queue_rq` 안에서 바로 완료하는 단일 큐 RAM 디스크였다. `i11_blk`는 진짜 HW 드라이버의 모양에 한 걸음 더 다가간다.

- **하드웨어 큐 2개**(`nr_hw_queues = 2`) — CPU가 두 큐에 나뉘어 매핑되는지 본다
- **비동기 완료**: `.queue_rq`는 데이터를 복사한 뒤 hrtimer를 걸고 바로 반환한다("doorbell을 울렸다"). 타이머 콜백(= "완료 IRQ 핸들러")이 `blk_mq_complete_request()`를 부르고, 블록 계층이 드라이버의 `.complete`를 부른다
- **요청별 드라이버 데이터**: `tag_set.cmd_size = sizeof(struct i11_cmd)` → `blk_mq_rq_to_pdu(rq)`로 request 바로 뒤의 메모리를 얻는다(NVMe 드라이버가 `struct nvme_iod`를 붙이는 방식과 같다). 할당이 없다
- **queue limits**: 512 B logical / 4 KiB physical, 요청당 최대 128 KiB, discard·write_zeroes 지원
- 완료가 어느 문맥(hardirq/softirq/task)에서 왔는지 세고, `stack=1`이면 첫 완료에서 `dump_stack()`

선언과 완료 경로:

```c
// SPDX-License-Identifier: GPL-2.0
/* i11_blk: 16 MiB RAM disk, N hw queues, completion after an hrtimer delay */
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/hrtimer.h>
#include <linux/vmalloc.h>

#define I11_BYTES (16u << 20)
static uint delay_us = 20, nr_hwq = 2;
module_param(delay_us, uint, 0444);	/* 0 = complete inline in ->queue_rq */
module_param(nr_hwq, uint, 0444);
static bool stack;
module_param(stack, bool, 0444);	/* dump_stack() in the first ->complete */
static u8 *i11_data;
static struct blk_mq_tag_set i11_set;
static struct gendisk *i11_disk;
static atomic_t n_q[8], n_hard, n_soft, n_task;

struct i11_cmd {			/* per-request driver data (tag_set.cmd_size) */
	struct hrtimer timer;		/* stands in for "device finished -> IRQ" */
	struct request *rq;
};

static void i11_complete(struct request *rq)	/* blk_mq_ops.complete */
{
	if (in_hardirq()) atomic_inc(&n_hard);
	else if (in_serving_softirq()) atomic_inc(&n_soft);
	else atomic_inc(&n_task);
	if (stack && atomic_read(&n_hard) + atomic_read(&n_soft) + atomic_read(&n_task) == 1)
		dump_stack();
	blk_mq_end_request(rq, BLK_STS_OK);
}

static enum hrtimer_restart i11_timer_fn(struct hrtimer *t)	/* "IRQ handler" */
{
	struct i11_cmd *cmd = container_of(t, struct i11_cmd, timer);

	blk_mq_complete_request(cmd->rq);	/* may bounce to submit CPU via IPI/softirq */
	return HRTIMER_NORESTART;
}
```

`.queue_rq`:

```c
static blk_status_t i11_queue_rq(struct blk_mq_hw_ctx *hctx,
				 const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	struct i11_cmd *cmd = blk_mq_rq_to_pdu(rq);
	loff_t pos = (loff_t)blk_rq_pos(rq) << SECTOR_SHIFT;
	struct req_iterator iter;
	struct bio_vec bv;

	if (pos + blk_rq_bytes(rq) > I11_BYTES)
		return BLK_STS_IOERR;		/* core ends the request with an error */
	blk_mq_start_request(rq);		/* starts the timeout clock */
	atomic_inc(&n_q[hctx->queue_num & 7]);
	switch (req_op(rq)) {
	case REQ_OP_READ:
	case REQ_OP_WRITE:
		rq_for_each_segment(bv, rq, iter) {	/* a real HW driver: blk_rq_map_sg + DMA */
			void *p = bvec_kmap_local(&bv);

			if (req_op(rq) == REQ_OP_WRITE) memcpy(i11_data + pos, p, bv.bv_len);
			else memcpy(p, i11_data + pos, bv.bv_len);
			kunmap_local(p);
			pos += bv.bv_len;
		}
		break;
	case REQ_OP_DISCARD:
	case REQ_OP_WRITE_ZEROES:
		memset(i11_data + pos, 0, blk_rq_bytes(rq));
		break;
	default:
		return BLK_STS_NOTSUPP;
	}
	if (!delay_us) {
		blk_mq_end_request(rq, BLK_STS_OK);
		return BLK_STS_OK;
	}
	cmd->rq = rq;
	hrtimer_setup(&cmd->timer, i11_timer_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_start(&cmd->timer, ns_to_ktime(delay_us * 1000ULL), HRTIMER_MODE_REL);
	return BLK_STS_OK;			/* "doorbell rung"; completion comes later */
}

static const struct blk_mq_ops i11_mq_ops = {
	.queue_rq = i11_queue_rq,
	.complete = i11_complete,
};
static const struct block_device_operations i11_bdops = { .owner = THIS_MODULE };
```

등록·해제:

```c
static int __init i11_init(void)
{
	struct queue_limits lim = {
		.logical_block_size	= 512,
		.physical_block_size	= 4096,
		.io_min			= 4096,
		.max_hw_sectors		= 256,		/* 128 KiB per request */
		.max_hw_discard_sectors	= UINT_MAX,
		.max_write_zeroes_sectors = UINT_MAX,
	};
	int ret;

	i11_data = vzalloc(I11_BYTES);
	if (!i11_data)
		return -ENOMEM;
	i11_set.ops = &i11_mq_ops;
	i11_set.nr_hw_queues = clamp(nr_hwq, 1u, 8u);
	i11_set.queue_depth = 64;
	i11_set.numa_node = NUMA_NO_NODE;
	i11_set.cmd_size = sizeof(struct i11_cmd);
	ret = blk_mq_alloc_tag_set(&i11_set);
	if (ret)
		goto err_mem;
	i11_disk = blk_mq_alloc_disk(&i11_set, &lim, NULL);
	if (IS_ERR(i11_disk)) {
		ret = PTR_ERR(i11_disk);
		goto err_set;
	}
	i11_disk->fops = &i11_bdops;		/* major 0 -> dynamic (259 blkext) */
	strscpy(i11_disk->disk_name, "i11_blk0", DISK_NAME_LEN);
	set_capacity(i11_disk, I11_BYTES >> SECTOR_SHIFT);
	ret = add_disk(i11_disk);
	if (ret)
		goto err_disk;
	pr_info("i11_blk: %u hw queues, depth %u, delay %u us\n",
		i11_set.nr_hw_queues, i11_set.queue_depth, delay_us);
	return 0;
err_disk:
	put_disk(i11_disk);
err_set:
	blk_mq_free_tag_set(&i11_set);
err_mem:
	vfree(i11_data);
	return ret;
}

static void __exit i11_exit(void)
{
	del_gendisk(i11_disk);
	put_disk(i11_disk);
	blk_mq_free_tag_set(&i11_set);
	vfree(i11_data);
	pr_info("i11_blk: queue_rq hctx0=%d hctx1=%d; complete in hardirq=%d softirq=%d task=%d\n",
		atomic_read(&n_q[0]), atomic_read(&n_q[1]), atomic_read(&n_hard),
		atomic_read(&n_soft), atomic_read(&n_task));
}
module_init(i11_init);
module_exit(i11_exit);
MODULE_DESCRIPTION("I11 blk-mq RAM disk with hrtimer completion");
MODULE_LICENSE("GPL");
```

게스트 스크립트 `g_i11blk.sh`(`-smp 4`):

```sh
cd /i11
dmesg -n 1
modprobe null_blk queue_mode=0 gb=1; dmesg | grep -E "BIO-based" | sed 's/^\[[ 0-9.]*\] //'; rmmod null_blk
echo "### insmod (stack=1)"
insmod i11_blk.ko delay_us=50 nr_hwq=2 stack=1
dmesg | grep i11_blk | sed 's/^\[[ 0-9.]*\] //'
ls -l --color=never /dev/i11_blk0
Q=/sys/block/i11_blk0/queue
for a in scheduler logical_block_size physical_block_size minimum_io_size max_hw_sectors_kb max_sectors_kb discard_max_hw_bytes write_zeroes_max_bytes nr_requests rotational; do printf "%-23s %s\n" $a "$(cat $Q/$a)"; done
for h in 0 1; do echo "mq/$h cpu_list=$(cat /sys/block/i11_blk0/mq/$h/cpu_list) nr_tags=$(cat /sys/block/i11_blk0/mq/$h/nr_tags)"; done
ls --color=never /sys/kernel/debug/block/i11_blk0/ /sys/kernel/debug/block/i11_blk0/hctx0 | tr '\n' ' '; echo
echo "### first completion stack"
dd if=/dev/i11_blk0 of=/dev/null bs=4k count=1 iflag=direct 2>/dev/null
dmesg | sed -n '/Call trace/,/^\[.*---\|el0t_64_irq\|el1h_64_irq/p' | sed 's/^\[[ 0-9.]*\] //' | head -30
echo "### discard + write_zeroes"
blkdiscard /dev/i11_blk0 && echo "blkdiscard ok"
echo "### bench"
for t in 1 4; do ./i11_bench /dev/i11_blk0 $t "i11 50us"; done
head -6 /sys/kernel/debug/block/i11_blk0/hctx1/tags
rmmod i11_blk; dmesg | grep "i11_blk: queue_rq" | sed 's/^\[[ 0-9.]*\] //'
echo "### delay sweep (1 thread)"
for d in 0 10 50 200; do insmod i11_blk.ko delay_us=$d nr_hwq=2; ./i11_bench /dev/i11_blk0 1 "i11 delay=${d}us"; rmmod i11_blk; done
dmesg | grep "i11_blk: queue_rq" | tail -4 | sed 's/^\[[ 0-9.]*\] //'
```

```text
lab initramfs: 6.18.55-sub up 0.83s
null_blk: BIO-based IO path is no longer available, using blk-mq instead.
### insmod (stack=1)
i11_blk: loading out-of-tree module taints kernel.
 i11_complete+0x120/0x128 [i11_blk]
 i11_timer_fn+0x14/0x24 [i11_blk]
 i11_init+0x12c/0xf58 [i11_blk]
i11_blk: 2 hw queues, depth 64, delay 50 us
brw-------    1 0        0         259,   0 Oct 12 01:03 /dev/i11_blk0
scheduler               [none] mq-deadline kyber bfq 
logical_block_size      512
physical_block_size     4096
minimum_io_size         4096
max_hw_sectors_kb       128
max_sectors_kb          128
discard_max_hw_bytes    2199023255040
write_zeroes_max_bytes  2199023255040
nr_requests             64
rotational              0
mq/0 cpu_list=0, 1 nr_tags=64
mq/1 cpu_list=2, 3 nr_tags=64
/sys/kernel/debug/block/i11_blk0/: hctx0 hctx1 pm_only poll_stat requeue_list state zone_wplugs  /sys/kernel/debug/block/i11_blk0/hctx0: active busy cpu0 cpu1 ctx_map dispatch dispatch_busy flags sched_tags sched_tags_bitmap state tags tags_bitmap type 
### first completion stack
Call trace:
 show_stack+0x28/0x7c (C)
 dump_stack_lvl+0x60/0x80
 dump_stack+0x18/0x24
 i11_complete+0x120/0x128 [i11_blk]
 blk_mq_complete_request+0x30/0x40
 i11_timer_fn+0x14/0x24 [i11_blk]
 __hrtimer_run_queues+0x130/0x184
 hrtimer_interrupt+0xe8/0x258
 arch_timer_handler_virt+0x34/0x44
 handle_percpu_devid_irq+0x7c/0x120
 handle_irq_desc+0x34/0x58
 generic_handle_domain_irq+0x1c/0x28
 gic_handle_irq+0x38/0xc0
 call_on_irq_stack+0x30/0x48
 do_interrupt_handler+0x80/0x84
 el1_interrupt+0x38/0x60
 el1h_64_irq_handler+0x18/0x24
### discard + write_zeroes
blkdiscard ok
### bench
i11 50us               thr=1 IOPS=   4856 mean= 201.1us p50= 179.9us p99=  478.7us
i11 50us               thr=4 IOPS=  11179 mean= 348.0us p50= 312.4us p99=  820.5us
nr_tags=64
nr_reserved_tags=0
active_queues=0

bitmap_tags:
depth=64
i11_blk: queue_rq hctx0=11128 hctx1=8875; complete in hardirq=20003 softirq=0 task=0
### delay sweep (1 thread)
i11 delay=0us          thr=1 IOPS=  19399 mean=  49.8us p50=  46.8us p99=  132.5us
i11 delay=10us         thr=1 IOPS=   7976 mean= 123.7us p50= 117.7us p99=  227.0us
i11 delay=50us         thr=1 IOPS=   4835 mean= 204.6us p50= 197.4us p99=  348.9us
i11 delay=200us        thr=1 IOPS=   2095 mean= 474.3us p50= 475.7us p99=  656.0us
i11_blk: queue_rq hctx0=0 hctx1=4001; complete in hardirq=0 softirq=0 task=0
i11_blk: queue_rq hctx0=1510 hctx1=2491; complete in hardirq=4001 softirq=0 task=0
i11_blk: queue_rq hctx0=2186 hctx1=1815; complete in hardirq=4001 softirq=0 task=0
i11_blk: queue_rq hctx0=2759 hctx1=1242; complete in hardirq=4001 softirq=0 task=0
[    7.671038] reboot: Power down
```

출력에서 볼 것:

- `259, 0`: `register_blkdev`를 생략하고 `disk->major = 0`으로 두었더니 **확장 major 259(blkext)** 를 받았다.
- `mq/0 cpu_list=0, 1`, `mq/1 cpu_list=2, 3`: blk-mq가 CPU 4개를 하드웨어 큐 2개에 나눴다(`blk_mq_map_queues`). 같은 CPU에서 낸 요청은 늘 같은 hctx로 간다. 마지막 줄들의 `queue_rq hctx0=… hctx1=…`이 실행마다 다른 것은 벤치마크 스레드가 어느 CPU에서 돌았느냐에 따른 것이다.
- 스케줄러 기본이 `[none]`(hctx가 2개), `nr_requests 64` = 우리가 준 큐 깊이. 스케줄러가 붙으면 이 값이 달라진다(G1에서 32 = 2 × 16을 봤다).
- limits가 sysfs에 그대로 보인다: `physical_block_size 4096`, `max_hw_sectors_kb 128`, `discard_max_hw_bytes`·`write_zeroes_max_bytes`가 0이 아님 → `blkdiscard`가 성공(REQ_OP_DISCARD가 우리 `switch`로 왔다).
- **첫 완료 스택**: `el1h_64_irq_handler` → `gic_handle_irq` → `arch_timer_handler_virt` → `hrtimer_interrupt` → `i11_timer_fn` → `blk_mq_complete_request` → `i11_complete`. 즉 **hardirq 문맥에서 바로** `.complete`가 불렸다. 이 첫 요청은 `add_disk()`의 파티션 스캔 읽기였다(그래서 `i11_init` 줄이 위에 섞여 있다 — `stack=1`이 첫 완료 하나만 찍는다).
- 카운터 `complete in hardirq=20003 softirq=0`: hrtimer는 시작한 CPU(= 제출 CPU)에서 터지므로 `blk_mq_complete_need_ipi()`가 "같은 CPU"로 판단해 IPI도 softirq도 쓰지 않았다. 실제 장치의 MSI-X 인터럽트가 다른 CPU로 오면 그때 IPI(`blk_mq_complete_send_ipi`)가 쓰인다. `delay_us=0`이면 `.queue_rq`에서 `blk_mq_end_request()`를 바로 불러 `.complete` 카운터가 0이다.
- debugfs `hctx1/tags`: 태그 비트맵 깊이 64. `queued`, `run` 같은 옛 debugfs 카운터는 6.18에는 없다(처음 시도에서 `cat`이 실패해서 `tags`로 바꿨다).

```svg
<svg viewBox="0 0 660 340" xmlns="http://www.w3.org/2000/svg">
<text x="330" y="22" text-anchor="middle" font-size="14" font-weight="bold">완료 지연 설정별 평균 지연시간 (4 KiB O_DIRECT, 1 스레드)</text>
<line x1="70" y1="280" x2="630" y2="280" stroke="currentColor"/>
<line x1="70" y1="280" x2="70" y2="60" stroke="currentColor"/>
<line x1="66" y1="280.0" x2="70" y2="280.0" stroke="currentColor"/>
<line x1="70" y1="280.0" x2="630" y2="280.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="284.0" text-anchor="end" font-size="12">0</text>
<line x1="66" y1="236.0" x2="70" y2="236.0" stroke="currentColor"/>
<line x1="70" y1="236.0" x2="630" y2="236.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="240.0" text-anchor="end" font-size="12">100</text>
<line x1="66" y1="192.0" x2="70" y2="192.0" stroke="currentColor"/>
<line x1="70" y1="192.0" x2="630" y2="192.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="196.0" text-anchor="end" font-size="12">200</text>
<line x1="66" y1="148.0" x2="70" y2="148.0" stroke="currentColor"/>
<line x1="70" y1="148.0" x2="630" y2="148.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="152.0" text-anchor="end" font-size="12">300</text>
<line x1="66" y1="104.0" x2="70" y2="104.0" stroke="currentColor"/>
<line x1="70" y1="104.0" x2="630" y2="104.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="108.0" text-anchor="end" font-size="12">400</text>
<line x1="66" y1="60.0" x2="70" y2="60.0" stroke="currentColor"/>
<line x1="70" y1="60.0" x2="630" y2="60.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="64.0" text-anchor="end" font-size="12">500</text>
<text x="26" y="170.0" font-size="12" text-anchor="middle" transform="rotate(-90 26 170.0)">mean latency (us)</text>
<rect x="98.0" y="259.5" width="39.2" height="20.5" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="117.6" y="254.5" text-anchor="middle" font-size="12">47</text>
<rect x="140.0" y="258.1" width="39.2" height="21.9" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="159.6" y="253.1" text-anchor="middle" font-size="12">50</text>
<text x="140.0" y="298" text-anchor="middle" font-size="12">inline(0)</text>
<rect x="238.0" y="224.0" width="39.2" height="56.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="257.6" y="219.0" text-anchor="middle" font-size="12">127</text>
<rect x="280.0" y="225.6" width="39.2" height="54.4" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="299.6" y="220.6" text-anchor="middle" font-size="12">124</text>
<text x="280.0" y="298" text-anchor="middle" font-size="12">10 us</text>
<rect x="378.0" y="195.2" width="39.2" height="84.8" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="397.6" y="190.2" text-anchor="middle" font-size="12">193</text>
<rect x="420.0" y="190.0" width="39.2" height="90.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="439.6" y="185.0" text-anchor="middle" font-size="12">205</text>
<text x="420.0" y="298" text-anchor="middle" font-size="12">50 us</text>
<rect x="518.0" y="76.5" width="39.2" height="203.5" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="537.6" y="71.5" text-anchor="middle" font-size="12">463</text>
<rect x="560.0" y="71.3" width="39.2" height="208.7" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="579.6" y="66.3" text-anchor="middle" font-size="12">474</text>
<text x="560.0" y="298" text-anchor="middle" font-size="12">200 us</text>
<text x="350.0" y="318" text-anchor="middle" font-size="12">null_blk: irqmode=0/1 · irqmode=2 completion_nsec ↔ i11_blk: delay_us</text>
<rect x="90" y="44" width="14" height="12" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/><text x="110" y="55" font-size="12">null_blk (0 = irqmode=0)</text>
<rect x="300" y="44" width="14" height="12" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/><text x="320" y="55" font-size="12">i11_blk (직접 만든 드라이버)</text>
</svg>
```
그림 9 — 완료 지연 설정별 평균 지연시간. null_blk(irqmode=0 인라인 / irqmode=2 hrtimer)와 직접 만든 i11_blk(delay_us)가 거의 같은 값을 낸다 — 같은 구조(`.queue_rq` → hrtimer → `blk_mq_complete_request`)이기 때문이다. 이 노트의 드라이버가 참조 드라이버와 같은 경로를 탄다는 실측 증거다.

### 6.7 예제 14 — 트레이스포인트로 bio → request → queue_rq → complete 보기

무엇을 확인하나: 디버그 커널(`Image-dbg`, ftrace 포함)에서 블록 트레이스포인트(`block_bio_queue`, `block_getrq`, `block_rq_issue`, `block_rq_complete`)와 함수 트레이서(우리 함수 3개 + `blk_mq_end_request`)를 같이 켜고, CPU 2에서 8 KiB `O_DIRECT` 읽기 한 번을 따라간다. dbg 커널용으로 따로 빌드한 `i11_blk.ko`(vermagic `6.18.55-dbg`)를 썼다 — sub 커널용 모듈을 넣으면 vermagic 불일치로 거부된다.

```sh
cd /i11
dmesg -n 1
insmod i11_blk.ko delay_us=50 nr_hwq=2
T=/sys/kernel/tracing; mount -t tracefs none $T
echo "i11_queue_rq i11_timer_fn i11_complete blk_mq_end_request" > $T/set_ftrace_filter
echo function > $T/current_tracer
for e in block_bio_queue block_getrq block_rq_issue block_rq_complete; do echo 1 > $T/events/block/$e/enable; done
echo 1 > $T/events/irq/softirq_raise/enable
echo > $T/trace
taskset -c 2 dd if=/dev/i11_blk0 of=/dev/null bs=8k count=1 skip=100 iflag=direct 2>/dev/null
echo 0 > $T/tracing_on
grep -v "^#" $T/trace | sed 's/^ *//' | cut -c1-150
```

```text
dd-89      [002] .....     4.900001: block_bio_queue: 259,0 RS 1600 + 16 [dd]
dd-89      [002] .....     4.900337: block_getrq: 259,0 RS 1600 + 16 [dd]
dd-89      [002] .....     4.900463: i11_queue_rq <-__blk_mq_issue_directly
dd-89      [002] .....     4.900583: block_rq_issue: 259,0 RS 8192 () 1600 + 16 be,0,4 [dd]
dd-89      [002] d.h1.     4.900954: i11_timer_fn <-__hrtimer_run_queues
dd-89      [002] d.h1.     4.900963: i11_complete <-blk_mq_complete_request
dd-89      [002] d.h1.     4.900973: blk_mq_end_request <-i11_complete
dd-89      [002] d.h1.     4.901023: block_rq_complete: 259,0 RS () 1600 + 16 be,0,4 [0]
```

(전체 출력 50줄 중 다른 CPU들의 타이머·RCU `softirq_raise` 줄 41개를 빼고 dd의 요청 줄만 남겼다. 블록 softirq(vec=4 BLOCK)는 한 번도 올라오지 않았다.)

출력에서 볼 것:

- `block_bio_queue 259,0 RS 1600 + 16`: 섹터 1600(= 100 × 8 KiB)부터 16섹터, `R`ead `S`ync. → `block_getrq`(태그·request 할당) → `i11_queue_rq <-__blk_mq_issue_directly`: **스케줄러가 none이고 plug가 없으니 제출 경로에서 바로 드라이버로** 갔다 → `block_rq_issue`.
- 약 0.37 ms 뒤 같은 CPU 2에서 `d.h1.`(irq 비활성, **hardirq 문맥**) 플래그로 `i11_timer_fn` → `i11_complete <-blk_mq_complete_request` → `blk_mq_end_request` → `block_rq_complete`. 50 us로 걸었는데 0.37 ms인 것은 TCG의 타이머 처리 지연이다.
- `vec=4`(BLOCK_SOFTIRQ)가 없다: 같은 CPU 완료라 softirq 없이 끝났다(예제 13의 카운터와 일치). 실보드에서 `blktrace`/`perf trace`로 보는 D(issue)·C(complete) 이벤트가 바로 이 두 트레이스포인트다(랩 dbg 커널은 `CONFIG_BLK_DEV_IO_TRACE`가 꺼져 있어 `blktrace`는 못 쓰지만 트레이스포인트는 있다).

### 6.8 예제 15 — 진짜 하드웨어 큐 드라이버 비교: NVMe와 SD

무엇을 확인하나: 같은 벤치마크를 QEMU의 NVMe(`-drive if=none,id=nv0,file=…,format=raw -device nvme,serial=i11nvme,drive=nv0`)와 SD 카드(sdhci-pci)에 돌리고, NVMe의 hctx가 CPU마다 하나씩 생기는지 본다.

```sh
cd /i11
dmesg -n 1
modprobe nvme; sleep 2
dmesg | grep -E "nvme" | sed 's/^\[[ 0-9.]*\] //'
ls --color=never /sys/block/nvme0n1/mq; for h in $(ls /sys/block/nvme0n1/mq); do echo "mq/$h cpu_list=$(cat /sys/block/nvme0n1/mq/$h/cpu_list) nr_tags=$(cat /sys/block/nvme0n1/mq/$h/nr_tags)"; done
Q=/sys/block/nvme0n1/queue
for a in scheduler logical_block_size max_hw_sectors_kb discard_max_hw_bytes write_zeroes_max_bytes nr_requests; do printf "%-23s %s\n" $a "$(cat $Q/$a)"; done
for t in 1 4; do ./i11_bench /dev/nvme0n1 $t "nvme (qemu)"; done
```

```text
lab initramfs: 6.18.55-sub up 0.84s
nvme nvme0: pci function 0000:00:02.0
nvme 0000:00:02.0: enabling device (0000 -> 0002)
nvme nvme0: 4/0/0 default/read/poll queues
nvme nvme0: Ignoring bogus Namespace Identifiers
0  1  2  3
mq/0 cpu_list=0 nr_tags=1023
mq/1 cpu_list=1 nr_tags=1023
mq/2 cpu_list=2 nr_tags=1023
mq/3 cpu_list=3 nr_tags=1023
scheduler               [none] mq-deadline kyber bfq 
logical_block_size      512
max_hw_sectors_kb       512
discard_max_hw_bytes    2147483136
write_zeroes_max_bytes  524288
nr_requests             1023
nvme (qemu)            thr=1 IOPS=   4289 mean= 229.1us p50= 194.4us p99=  582.3us
nvme (qemu)            thr=4 IOPS=  11695 mean= 331.3us p50= 312.5us p99=  661.7us
```

SD 카드(별도 부팅, `g_sdbench.sh` — sdhci-pci 모듈 두 개를 올린 뒤 같은 벤치마크):

```text
saved /var/tmp/I11/out_g_sdbench.txt (4 lines)
lab initramfs: 6.18.55-sub up 1.02s
sd-card (sdhci-pci)    thr=1 IOPS=    904 mean=1101.8us p50=1077.2us p99= 1357.9us
sd-card (sdhci-pci)    thr=4 IOPS=    962 mean=4104.5us p50=4011.7us p99= 8555.2us
```

출력에서 볼 것:

- `4/0/0 default/read/poll queues`: NVMe 드라이버가 **CPU 4개에 SQ/CQ 쌍 4개**를 만들었고, `mq/0..3`의 `cpu_list`가 CPU 하나씩이다. `nr_tags 1023` = SQ 깊이 1024 − 1(NVMe 큐는 한 칸을 비워 full/empty를 구분한다 — Don이 펌웨어 쪽에서 본 바로 그 규칙). 스케줄러 기본 `none`.
- SD는 하드웨어 큐 1개·명령 하나씩이라 4 스레드로 늘려도 IOPS가 거의 그대로(904 → 962)이고 지연만 4배가 됐다. NVMe는 4 스레드에서 2.7배. **큐 하나 장치에서 동시성은 지연으로 바뀐다** — eMMC CQE(깊이 32)가 왜 중요한지 보여 준다.
- NVMe의 절대값은 null_blk 50 us와 비슷하다. QEMU NVMe가 TCG에서 돌면서 생기는 에뮬레이션 비용이 대부분이다 — 실제 SSD 성능과 비교하지 말 것.

---

## 7. 커널 안에서는 무슨 일이 일어나나

세 경로를 함수 이름으로 다시 읽는다. 예제에서 실제로 본 이름(스택·ftrace·dmesg)은 굵게 표시하지 않고 그대로 적었다.

```
(1) raw NAND, user tool:  write(/dev/mtd2) -> mtdchar_write -> mtd_write -> mtd_write_oob
        -> nand_write_oob -> nand_do_write_ops -> chip->ecc.write_page (ECC calc)
        -> nand_prog_page_op -> controller->ops->exec_op   [nandsim: ns_exec_op -> prog_page: AND]
    read side: nand_read_page_swecc -> ecc_sw_hamming_correct -> -EBADMSG / -EUCLEAN
        -> mtdchar_read returns data anyway, user checks ECCGETSTATS

(2) raw NAND, filesystem: write(file) -> ubifs_write_end ... -> ubifs_leb_write
        -> ubi_leb_write -> ubi_eba_write_leb (pick PEB via ubi_wl_get_peb, write VID hdr)
        -> ubi_io_write -> mtd_write ...                      (same as 1 below this point)
    erase side: ubi_eba_unmap_leb -> schedule_erase -> erase_worker (ubi_bgt0d thread)
        -> do_sync_erase -> mtd_erase ; failure -EIO -> ubi_io_mark_bad -> mtd_block_markbad
    wear: wear_leveling_worker moves cold LEB when ec(max free) - ec(min used) >= UBI_WL_THRESHOLD

(3) managed flash: read(/dev/X, O_DIRECT) -> blkdev_read_iter -> __blkdev_direct_IO(_simple)
        -> submit_bio [trace: block_bio_queue] -> blk_mq_submit_bio -> (plug / elevator)
        -> blk_mq_get_new_requests [block_getrq] -> __blk_mq_issue_directly
        -> ops->queue_rq   (i11_queue_rq / mmc_mq_queue_rq / nvme_queue_rq) [block_rq_issue]
        ... device works ...
        IRQ -> driver handler (i11_timer_fn / sdhci_irq / nvme_irq)
        -> blk_mq_complete_request -> (IPI/BLOCK_SOFTIRQ if other CPU) -> ops->complete
        -> blk_mq_end_request [block_rq_complete] -> bio_endio -> wake waiting task
    MMC specifics: mmc_mq_queue_rq -> mmc_blk_mq_issue_rq -> mmc_start_request
        -> host->ops->request (sdhci_request) -> SDHCI regs + ADMA ; IRQ -> mmc_request_done
```

드라이버를 쓸 때 이 지식이 다시 나오는 곳:

| 쓰게 될 드라이버 | 채울 구조체 | 등록 함수 | 관련 노트 |
|---|---|---|---|
| SoC NAND 컨트롤러 | `nand_controller_ops`(`exec_op`, `attach_chip`), `nand_chip.ecc` | `nand_scan()`, `mtd_device_register()` | 이 노트 §1.7, H9(DMA) |
| QSPI/OSPI 컨트롤러 (SPI-NOR/NAND용) | `spi_controller_mem_ops` | `devm_spi_register_controller()` | H5(SPI) |
| SoC SD/eMMC 컨트롤러 | `sdhci_ops` (또는 `mmc_host_ops` 직접) | `sdhci_add_host()` / `mmc_add_host()` | 이 노트 §5.6, D(DT 속성), H6(pinctrl·regulator) |
| 새 블록 장치(가상 디스크, FPGA 스토리지) | `blk_mq_ops`, `queue_limits`, `tag_set` | `blk_mq_alloc_disk()`, `add_disk()` | G1, 이 노트 §6.6 |
| 완료 IRQ | `request_irq` 핸들러 → `blk_mq_complete_request` | — | F(IRQ·softirq), M(ftrace) |

Don의 펌웨어 감각과 다른 점 하나: 펌웨어에서는 완료 처리 위치를 직접 정했지만, Linux에서는 `blk_mq_complete_request()`가 **제출 CPU 캐시 지역성**을 위해 완료를 다른 CPU로 옮길 수 있다(`QUEUE_FLAG_SAME_COMP`, sysfs `rq_affinity`). 그래서 `.complete`는 hardirq·softirq 어느 쪽에서든 불릴 수 있다고 가정하고 **잠들지 않는 코드**로 써야 한다.

---

## 8. Embedded 실무에서 다시 보기

### 8.1 장면 1 — raw NAND 보드의 rootfs 설계

예를 들어 센서 키트의 저가 SoC 보드가 512 MiB SPI-NAND 하나만 갖고 있다면: MTD 파티션을 `spl`(ROM이 읽는 영역, 여러 사본) · `uboot` · `env`(이중화) · `ubi`(나머지 전부)로 나누고, `ubi` 위에 UBI 볼륨 `rootfs_a`, `rootfs_b`(squashfs를 `ubiupdatevol`로 쓰고 `ubiblock`으로 마운트), `data`(UBIFS, 로그·설정)를 만든다. 업데이트는 반대쪽 볼륨에 쓰고 `ubirename`(UBI_IOCRNVOL, **원자적**)으로 바꾼다. UBI를 MTD 파티션 여러 개에 나눠 붙이지 않는 이유: wear leveling과 배드 블록 예약은 **UBI 장치 안에서만** 공유되므로, 하나의 큰 UBI에 볼륨을 여러 개 두어야 전체 블록에 마모가 퍼진다.

### 8.2 장면 2 — eMMC가 HS200에서 가끔 CRC 에러

로그에 `mmc0: tuning execution failed` 또는 데이터 CRC 에러(`-84`, EILSEQ)가 보이면: `/sys/kernel/debug/mmc0/ios`로 현재 타이밍·클럭·전압 확인 → DT에서 `mmc-hs200-1_8v`를 잠시 빼고 HS(52 MHz)로 안정되는지 → 된다면 보드 신호·패드 drive strength·`vqmmc` 레귤레이터 쪽 문제다. 양산 대응으로 `max-frequency`를 낮추거나 `no-1-8-v`를 넣기도 한다. `mmc_test`(예제 11)는 이런 호스트 드라이버 bring-up에 쓴다.

### 8.3 장면 3 — 로그 저장소의 마모 예산

센서 키트가 eMMC에 IMU 로그를 초당 수백 번 `write()+fsync()`한다면 장치 FTL이 아무리 좋아도 쓰기 증폭과 마모가 크다. eMMC는 EXT_CSD의 `DEVICE_LIFE_TIME_EST_TYP_A/B`(life time 추정)와 `PRE_EOL_INFO`를 제공하므로 `mmc extcsd read`(mmc-utils)나 C2 예제 9의 `MMC_IOC_CMD` 방식으로 읽어 헬스 모니터에 올린다. raw NAND + UBI라면 예제 6·8의 `max_ec`와 `UBI_IOCECNFO`로 같은 일을 한다. 어느 쪽이든 앱 쪽 해결책은 같다: RAM에 모았다가 큰 단위로 쓰기, `fsync` 빈도 줄이기(J5).

### 8.4 장면 4 — "UBI attach가 부팅을 3초 늦춘다"

attach는 모든 PEB의 헤더를 읽는다(예제 7). 1 GiB NAND(8192 PEB)면 page 16384개를 읽어야 한다. 해결: fastmap(`CONFIG_MTD_UBI_FASTMAP` + `ubi.fm_autoconvert=1`) — 매핑 스냅샷을 flash에 저장해 일부만 읽는다. 대신 fastmap 자체가 전원 차단 시나리오를 늘리므로 충분히 테스트한다(nandsim + UBI debugfs의 `tst_emulate_power_cut`이 그런 테스트용이다).

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| raw NAND 파티션에 ext4를 `mtdblock`으로 올림 | 처음엔 되다가 전원 차단 후 FS 손상, 배드 블록 생기면 I/O 에러 | mtdblock은 RMW·배드 블록·WL이 없다(예제 5: 512 B → 128 KiB erase) | UBI + UBIFS, 읽기 전용이면 ubiblock + squashfs |
| erase 없이 `nandwrite`/U-Boot `nand write` | 읽을 때 ECC 에러, 데이터가 AND 된 값 | NAND program은 1→0만 가능(예제 4) | `flash_erase` / `nand erase` 먼저. `nandwrite`는 erase된 블록 전제 |
| mtdchar로 읽고 반환값만 확인 | 깨진 데이터를 정상으로 처리 | ECC 실패에도 `read()`가 성공을 반환(mtdchar.c 주석) | `ECCGETSTATS` 전후 비교 또는 `MEMREAD`의 `ecc_stats` 사용 |
| UBI를 MTD 파티션마다 따로 attach | 일부 파티션만 빨리 닳음 | WL·배드 블록 예약이 UBI 장치 단위 | 큰 UBI 하나 + 볼륨 여러 개 |
| `MTD_UBI_BEB_LIMIT`을 데이터시트와 무관하게 둠 | 몇 년 뒤 UBI가 R/O로 전환, 로그 중단(예제 9) | 예약 PEB 고갈 | 데이터시트 NVB로 계산, `ro_mode` 감시 |
| MLC NAND에 `WL_THRESHOLD=4096` 그대로 | 일부 블록만 수명 도달(예제 8의 파랑 곡선) | static WL이 사실상 안 돔 | Kconfig 도움말대로 128~256 |
| eMMC/SD 호스트 DT에 `vqmmc-supply`·pinctrl `state_uhs` 누락 | HS200/SDR104 튜닝 실패, HS로 떨어짐 | 1.8 V 전환 불가 | 레귤레이터·pinctrl 상태 추가, `ios`로 확인 |
| blk-mq 드라이버의 `.complete`에서 잠드는 함수 호출 | `BUG: scheduling while atomic` | `.complete`는 hardirq/softirq에서 불림(예제 13·14) | 잠드는 일은 workqueue로, `.complete`는 끝내기만 |
| `queue_limits`의 `max_segments`·`max_hw_sectors`를 HW보다 크게 | 큰 I/O에서 DMA 디스크립터 오버런, 데이터 손상 | block 계층은 limits만 믿고 쪼갠다 | 컨트롤러 데이터시트 값 그대로, 의심되면 작게 |
| `BLK_FEAT_WRITE_CACHE`를 켜고 FLUSH 미구현 | 전원 차단 후 `fsync`한 데이터 유실 | FLUSH 요청을 무시 | REQ_OP_FLUSH에서 장치 캐시 flush 명령 |

---

## 10. 면접에서 이렇게 말한다

**Q.** What is the difference between raw NAND and eMMC from the Linux kernel's point of view?

**A.** raw NAND는 FTL이 없어서 커널이 MTD 문자 장치로 보고, ECC·배드 블록·wear leveling을 MTD+UBI가 맡는다. eMMC는 장치 안에 컨트롤러와 FTL이 있어서 MMC 코어 위의 `mmc_blk`가 만든 블록 장치이고, 커널은 명령 전달과 discard·flush 같은 힌트만 준다. 그래서 파일시스템도 다르다: UBIFS 대 ext4/F2FS.

> Raw NAND has no translation layer, so Linux exposes it through MTD as a character device, and the kernel itself handles ECC, bad blocks and wear leveling — MTD for the raw operations and UBI as the FTL, with UBIFS on top. eMMC has its own controller and FTL, so it shows up as a normal block device through the MMC core and mmc_blk; the kernel just transports commands and passes hints like discard and cache flush. That's why you run UBIFS on raw NAND but ext4 or F2FS on eMMC.

**꼬리 질문**: "Can you put ext4 on raw NAND via mtdblock?" → 안 된다: 512 B 쓰기가 erase block 전체 RMW가 되고(측정: erase 1회 + program 64회), 배드 블록·WL·전원 차단 보호가 없다.

**Q.** How does UBI compare to an SSD's FTL?

**A.** UBI는 커널 안의 블록 단위 FTL이다. 각 PEB 맨 앞의 EC 헤더가 erase 횟수 테이블, VID 헤더(볼륨 ID, LEB 번호, 시퀀스 번호)가 분산된 L2P 매핑이고 attach 때 스캔으로 RAM 테이블을 재구성한다. wear leveling은 free PEB 선택과 임계값 기반 cold 데이터 이동, 배드 블록은 예약 풀로 처리한다. page 단위 배치와 GC는 위의 UBIFS가 한다.

> UBI is essentially an eraseblock-granularity FTL inside the kernel. Each physical eraseblock carries an erase-counter header and a volume-ID header with the logical block number and a sequence number, so the mapping is distributed on flash and rebuilt by scanning at attach time — or from fastmap. It does dynamic wear leveling when picking free blocks, static wear leveling when the erase-count gap exceeds a threshold, and keeps a reserve pool for grown bad blocks. Page-level placement and garbage collection live one layer up, in UBIFS. Coming from SSD firmware, the main difference is that the FTL is split across two layers and has no dedicated metadata area.

**꼬리 질문**: "What happens when the bad-block reserve runs out?" → UBI가 장치를 read-only로 바꾼다. 읽기는 계속 되고 쓰기는 `-EROFS` — 예제 9에서 재현했다.

**Q.** Walk me through how an SD card gets enumerated in Linux.

**A.** 호스트 드라이버가 `mmc_add_host`로 등록되고 카드 감지가 되면 코어의 `mmc_rescan`이 400 kHz에서 SDIO, SD, MMC 순으로 시도한다. SD는 CMD0, CMD8로 전압 확인, ACMD41을 반복해 ready와 용량 타입을 받고, CMD2로 CID, CMD3으로 RCA, CMD9로 CSD, CMD7로 선택, ACMD51로 SCR, CMD6으로 high speed, ACMD6으로 4-bit 전환 후 `set_ios`로 클럭을 올린다. 그다음 `mmc_card`가 mmc 버스에 등록되고 `mmc_blk`가 블록 장치를 만든다.

> The host controller driver registers an mmc_host with ops like request, set_ios and get_cd. When a card is detected, the core's mmc_rescan powers up at 400 kHz and tries SDIO, then SD, then MMC. For SD it's CMD0, CMD8 to check voltage, ACMD41 in a loop until the card is ready and reports capacity, CMD2 for the CID, CMD3 to get the relative address, CMD9 for the CSD, CMD7 to select, ACMD51 for the SCR, CMD6 to switch to high speed and ACMD6 for 4-bit bus, then set_ios to raise the clock. The card is added to the mmc bus and mmc_blk binds and creates the gendisk. I've traced exactly that sequence with dynamic debug on the MMC core.

**꼬리 질문**: "Where does HS200 tuning happen?" → eMMC를 HS200으로 전환한 뒤 코어가 `mmc_execute_tuning` → 호스트 `.execute_tuning`(CMD21)으로 샘플링 위상을 고른다. 실패하면 신호 무결성·1.8 V 레귤레이터·패드 설정을 본다.

**Q.** Describe the blk-mq submission and completion path for a driver.

**A.** bio가 block 계층에서 쪼개지고 합쳐져 태그가 붙은 request가 되고, plug와 스케줄러(있으면)를 거쳐 CPU에 매핑된 하드웨어 큐(hctx)에서 드라이버의 `.queue_rq`가 불린다. 드라이버는 `blk_mq_start_request` 후 DMA를 설정하고 doorbell을 울리고 반환한다. 완료 인터럽트에서 `blk_mq_complete_request`를 부르면 필요하면 제출 CPU로 IPI/softirq를 보낸 뒤 `.complete`가 불리고 거기서 `blk_mq_end_request`로 끝낸다. 큐가 꽉 차면 `BLK_STS_RESOURCE`를 반환한다.

> A bio gets split or merged and turned into a tagged request; after the plug and the optional I/O scheduler, it's dispatched on the hardware context mapped to the submitting CPU, which calls the driver's queue_rq. The driver calls blk_mq_start_request, sets up DMA from the scatter list, rings the doorbell and returns. On the completion interrupt it calls blk_mq_complete_request; the block layer may bounce that to the submitting CPU via IPI or softirq, then calls the driver's complete callback, which ends the request. If the hardware queue is full, queue_rq returns BLK_STS_RESOURCE and the core retries. I wrote a small two-queue driver with timer-based completion and confirmed the path with block tracepoints.

**꼬리 질문**: "Why per-CPU hardware queues?" → 큐 락 경합과 캐시 라인 바운싱을 없애고 NVMe SQ/CQ에 1:1로 매핑하기 위해. 예제 15에서 NVMe가 CPU 4개에 큐 4개를 만들었다.

**Q.** Which I/O scheduler would you use for eMMC versus NVMe, and why?

**A.** NVMe는 `none`: 장치가 내부 병렬성과 큐가 깊어서 스케줄러는 CPU 비용만 더한다(측정에서 none이 가장 높은 IOPS). 단일 큐인 eMMC/SD는 기본 `mq-deadline`이 읽기 지연을 보호하고, 대화형·혼합 부하에서 공정성이 필요하면 `bfq`를 쓴다. 결국 측정으로 정한다.

> For NVMe I'd use none: the device has deep, parallel queues and a scheduler only adds CPU overhead — in my null_blk measurements none gave the highest IOPS and mq-deadline the worst tail under concurrency. For single-queue devices like eMMC or SD, mq-deadline is the default and protects read latency from write bursts; bfq makes sense when you need per-process fairness, for example a logger shouldn't starve the main application. I'd validate with fio on the target.

**Q.** What does the ECC engine type mean in the Linux NAND framework?

**A.** ECC를 누가 계산하느냐다. SOFT는 CPU가 Hamming/BCH 계산, ON_HOST는 SoC NAND 컨트롤러의 HW 엔진, ON_DIE는 칩 내부 ECC(SPI-NAND에 흔함), NONE은 없음. DT의 `nand-ecc-engine`, strength, step size로 고르고, 부트 ROM이 읽는 영역은 ROM이 기대하는 ECC 레이아웃과 같아야 한다.

> It's about who computes and checks the ECC: software engines run Hamming or BCH on the CPU, on-host means the SoC's NAND controller does BCH in hardware during DMA, on-die means the NAND chip corrects internally, which is common for SPI-NAND. You select it in the device tree with nand-ecc-engine plus strength and step size. A practical gotcha is that the boot ROM has its own fixed ECC layout, so the first-stage image must be written in the ROM's format, not Linux's.

**Q.** How would you bring up a new SDHCI-based controller on a custom board?

**A.** DT에 클럭·리셋·`bus-width`·`vmmc/vqmmc`·pinctrl 상태를 넣고, 컨트롤러가 SDHCI 호환이면 `sdhci-pltfm` 기반으로 벤더 차이만 `sdhci_ops`에 채운다. 처음엔 `max-frequency`를 낮추고 HS로 붙이고, debugfs `ios`와 dynamic debug로 명령 순서를 보고, `mmc_test`로 읽기·쓰기를 검증한 뒤 HS200 튜닝을 켠다.

> Start from the device tree — clocks, resets, bus-width, vmmc and vqmmc regulators and pinctrl states — and if the IP is SDHCI-compatible, write a thin sdhci-pltfm driver that only fills sdhci_ops for vendor-specific clocking, reset quirks and tuning. I'd bring it up conservatively at high-speed with a capped max-frequency, check debugfs ios and the command trace with dynamic debug, run mmc_test to validate transfers, and only then enable HS200 or HS400 and debug tuning.

---

## 11. 직접 해보기

1. **개념**: 예제 6에서 LEB 크기가 129024인 이유를 설명하라. sub-page를 지원하지 않는 4 KiB page NAND(PEB 256 KiB)라면 LEB 크기는 얼마인가? 정답/힌트: EC 헤더와 VID 헤더가 각각 min I/O 단위 하나씩 → 256 KiB − 2 × 4 KiB = 253952 B.
2. **코드**: `i11_mtdrw`에 `oobwrite` 명령을 추가해 `MEMWRITE`(`struct mtd_write_req`, `mode = MTD_OPS_RAW`)로 OOB 0번 바이트에 0x00을 쓰고, `i11_mtdinfo`가 그 블록을 배드로 보고하는지 확인하라. 정답/힌트: large-page NAND의 BBM은 첫 page OOB의 0번 바이트. `MEMGETBADBLOCK`은 BBT가 없으면 OOB를 읽어 판단한다.
3. **측정**: 예제 8에서 `wl_threshold`를 16, 64, 256으로 바꿔 1500회 후 max − min을 그래프로 그려라. cold 데이터 크기를 2 MiB와 10 MiB로 바꾸면 어떻게 되나? 정답/힌트: 임계값이 작을수록 편차가 작고 총 erase가 약간 늘어난다. cold가 클수록 hot 블록이 적어져 기본 설정의 편차가 더 빨리 커진다.
4. **관찰**: 예제 10을 eMMC로 돌린 뒤 `/sys/kernel/debug/mmc0/mmc0:0001/ext_csd`를 읽어 바이트 185(HS_TIMING)와 183(BUS_WIDTH)이 CMD6 trace의 값과 같은지 확인하라. 정답/힌트: debugfs `ext_csd`는 512 바이트를 16진 문자열로 준다 — 바이트 N은 문자열의 2N 위치.
5. **코드**: `i11_blk`에 `.timeout` 콜백을 추가하고 `delay_us`를 40초(기본 request timeout 30초보다 길게)로 올려 타임아웃 경로를 관찰하라. `BLK_EH_RESET_TIMER`와 `BLK_EH_DONE`의 차이를 설명하라. 정답/힌트: `/sys/block/i11_blk0/queue/io_timeout`으로 timeout을 줄이면 빨리 볼 수 있다. DONE을 반환하면 드라이버가 직접 완료할 책임을 진다(hrtimer와 경합 주의).
6. **관찰**: `null_blk`를 `submit_queues=4`로 올리고 `taskset -c 3`으로 벤치마크를 돌리면서 `/sys/kernel/debug/block/nullb0/hctx*/tags`의 `busy`를 비교하라. 어느 hctx만 일하는가? 정답/힌트: `mq/N/cpu_list`가 CPU 3을 가진 hctx 하나.
7. **설계**: 512 MiB SPI-NAND 하나인 보드에 A/B 업데이트 + 로그 영역을 설계하라(MTD 파티션, UBI 볼륨, 파일시스템, 원자적 전환 방법, BEB 예산). 정답/힌트: §8.1. `ubirename`과 `max_beb_per1024`.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| MTD | Memory Technology Device | raw flash(NOR/NAND)를 erase/read/write/OOB 동작 그대로 노출하는 커널 계층 |
| `mtd_info` | MTD 장치 구조체 | 크기·erasesize·writesize·oobsize와 `_erase`/`_read`/`_write`/`_block_isbad` ops |
| mtdchar | `/dev/mtdN` | MTD의 문자 장치 인터페이스. `MEMERASE`, `MEMGETBADBLOCK`, `MEMREAD` ioctl |
| mtdblock | `/dev/mtdblockN` | MTD를 블록 장치로 위장하는 얇은 RMW 어댑터 — 쓰기용으로 쓰지 말 것 |
| OOB | out-of-band (spare) | NAND page마다 붙은 여분 영역. ECC·BBM 저장 |
| BBM / BBT | bad block marker / table | OOB의 배드 표시 / flash에 저장한 배드 블록 테이블 |
| ECC engine | `nand_ecc_engine` | SOFT / ON_HOST / ON_DIE / NONE |
| `exec_op` | raw NAND 컨트롤러 op | 명령·주소·데이터·대기 명령어 목록을 HW로 실행 |
| SPI-MEM | SPI 메모리 계층 | SPI-NOR/SPI-NAND 칩 로직과 QSPI 컨트롤러를 잇는 `exec_op` 인터페이스 |
| UBI | Unsorted Block Images | MTD 위의 볼륨 관리 + LEB→PEB 매핑 + WL + 배드 블록 처리 (커널 FTL) |
| PEB / LEB | physical / logical eraseblock | 실제 erase block / 볼륨이 보는 논리 블록 (LEB = PEB − 헤더) |
| EC 헤더 | erase counter header | PEB 맨 앞 64 B, `UBI#`, erase 횟수 |
| VID 헤더 | volume identifier header | `UBI!`, vol_id·lnum·sqnum — 분산 매핑 정보 |
| fastmap | UBI 매핑 스냅샷 | attach 스캔을 줄이는 옵션 |
| BEB 예약 | bad eraseblock reserve | grown bad block 대체용 PEB 풀, 바닥나면 R/O |
| UBIFS | UBI File System | UBI 볼륨 위의 로그 구조 FS (인덱스 트리, 저널, 압축) |
| ubiblock | UBI 읽기 전용 블록 장치 | squashfs를 raw NAND에 올릴 때 |
| `mmc_host` / `mmc_host_ops` | MMC 호스트 | 호스트 컨트롤러 드라이버가 등록. `request`, `set_ios`, `get_cd`, `execute_tuning` |
| `mmc_card` | 카드 | 코어가 열거 후 mmc 버스에 등록 (CID, CSD, EXT_CSD …) |
| `mmc_ios` | 버스 상태 | 클럭·버스 폭·타이밍·전압. debugfs `ios` |
| SDHCI | SD Host Controller Interface | 표준 호스트 레지스터 맵 + Linux 라이브러리 `sdhci.c` |
| RCA | relative card address | CMD3로 정하는 카드 주소 (SD는 카드가, eMMC는 호스트가 정함) |
| HS200 / HS400 | eMMC 고속 모드 | 200 MHz SDR / DDR, 튜닝(CMD21) 필요 |
| CQE | command queue engine | eMMC 5.1 HW 명령 큐(깊이 32), `cqhci` |
| gendisk | 디스크 | 블록 장치 하나의 커널 표현 |
| bio | block I/O | 섹터 + `bio_vec` 조각 목록 — FS가 내려보내는 단위 |
| request | 요청 | 드라이버로 가는 단위, 태그 하나, 인접 bio 묶음 |
| blk-mq | multi-queue block layer | CPU별 SW 큐 + 하드웨어 큐(hctx) 구조 |
| tag set | `blk_mq_tag_set` | hw 큐 수·깊이·ops·`cmd_size` |
| hctx | `blk_mq_hw_ctx` | 하드웨어 큐 하나 (NVMe SQ/CQ 쌍에 대응) |
| `queue_limits` | 큐 제약 | 블록 크기, 최대 전송, 세그먼트, discard/write_zeroes |
| plug | `blk_plug` | 태스크가 request를 모아 한 번에 내보내는 배칭 |
| I/O 스케줄러 | elevator | none / mq-deadline / kyber / bfq |
| null_blk | 참조 드라이버 | 저장 안 하는 blk-mq 드라이버, 블록 계층 벤치마크용 |
| discard / TRIM | 해제 힌트 | "이 LBA는 더 이상 안 쓴다" → 장치 FTL의 GC를 돕는다 |

---

## 13. 요약 & 체크리스트

Linux 스토리지는 두 세계로 나뉜다. FTL이 없는 raw flash(NOR, raw NAND, SPI-NAND)는 MTD가 `mtd_info`로 erase/read/write/OOB/배드 블록 동작을 그대로 노출하고(`/dev/mtdN` 문자 장치), UBI가 그 위에서 커널 안 FTL 역할 — PEB마다 EC·VID 헤더를 두고 attach 스캔으로 LEB→PEB 매핑을 재구성하며, free PEB 선택과 임계값 기반 이동으로 마모를 고르게 하고, 예약 풀로 grown bad block을 대체(바닥나면 R/O) — 을 하고, UBIFS가 page 단위 배치와 GC를 한다. mtdblock 위 ext4는 512 B 쓰기를 erase block 전체 RMW로 만들기 때문에 쓰면 안 된다. FTL을 가진 managed flash(SD, eMMC, UFS, NVMe)는 블록 장치다. MMC 서브시스템은 호스트 드라이버(`mmc_host_ops`: request/set_ios/get_cd/execute_tuning, 보통 SDHCI 라이브러리 + `sdhci_ops`), 프로토콜을 아는 코어(`mmc_rescan`의 CMD0/CMD8/ACMD41/CMD2/CMD3/CMD9/CMD7/CMD6 열거), 카드 드라이버(`mmc_blk`, `mmc_test`)로 나뉜다. 그 아래 공통 바닥인 blk-mq는 bio를 태그 붙은 request로 만들어 CPU에 매핑된 하드웨어 큐에서 `.queue_rq`로 넘기고, 완료 IRQ의 `blk_mq_complete_request` → `.complete` → `blk_mq_end_request`로 끝낸다. 이 노트에서 nandsim으로 AND 의미론과 ECC 실패를, UBI 사본으로 wear leveling 효과를, weak block으로 예약 고갈을, dynamic debug로 SD/eMMC 열거를, null_blk와 직접 만든 `i11_blk`로 완료 경로와 스케줄러 비용을 측정했다.

- [ ] raw flash 스택과 managed flash 스택을 그리고 FTL이 어디 있는지 설명할 수 있다
- [ ] `mtd_info`의 주요 필드와 `_erase`/`_read_oob`/`_block_isbad` ops, 래퍼 함수의 역할을 말할 수 있다
- [ ] `/dev/mtdN`에 ioctl로 erase/program/read/OOB/배드 블록 조회를 하는 C 코드를 짤 수 있다
- [ ] erase 없이 다시 쓰면 왜 AND가 되고 ECC가 깨지는지, mtdchar가 왜 그래도 데이터를 돌려주는지 설명할 수 있다
- [ ] raw NAND 컨트롤러 드라이버의 `exec_op`·`attach_chip`·ECC 엔진 선택을 설명할 수 있다
- [ ] mtdblock 위 ext4가 안 되는 이유를 측정값으로 말할 수 있다
- [ ] UBI의 EC/VID 헤더, LEB/PEB, attach 스캔, WL 임계값, BEB 예약을 SSD FTL 개념과 대응시킬 수 있다
- [ ] SD와 eMMC의 열거 명령 순서와 차이(ACMD41 vs CMD1, RCA, CMD8의 두 의미, CMD6)를 설명할 수 있다
- [ ] `mmc_host_ops`와 `sdhci_ops`의 관계, HS200 튜닝이 어디서 일어나는지 설명할 수 있다
- [ ] gendisk·request_queue·bio·request·tag set·hctx의 관계를 그리고 queue limits 필드를 설명할 수 있다
- [ ] 비동기 완료를 하는 다중 큐 blk-mq 드라이버를 직접 짜고 완료 문맥을 증명할 수 있다
- [ ] 장치 종류에 맞는 I/O 스케줄러를 근거와 함께 고를 수 있다

## 참고 자료

- docs.kernel.org — MTD NAND Driver Programming Interface: https://docs.kernel.org/driver-api/mtdnand.html
- docs.kernel.org — Multi-Queue Block IO Queueing Mechanism (blk-mq): https://docs.kernel.org/block/blk-mq.html
- docs.kernel.org — null_blk device driver: https://docs.kernel.org/block/null_blk.html
- docs.kernel.org — Switching Scheduler / Deadline, Kyber, BFQ: https://docs.kernel.org/block/switching-sched.html
- docs.kernel.org — MMC/SD/SDIO card support (mmc-test 포함): https://docs.kernel.org/driver-api/mmc/index.html
- docs.kernel.org — UBI File System: https://docs.kernel.org/filesystems/ubifs.html
- linux-mtd 프로젝트 문서(UBI, UBIFS, NAND 설계 노트): http://www.linux-mtd.infradead.org/doc/ubi.html
- 커널 소스(랩 6.18.55): `include/linux/mtd/mtd.h`, `include/linux/mtd/rawnand.h`, `drivers/mtd/mtdchar.c`, `drivers/mtd/nand/raw/nandsim.c`, `drivers/mtd/ubi/{wl.c,attach.c,ubi-media.h,Kconfig}`, `include/uapi/mtd/{mtd-abi.h,ubi-user.h}`, `drivers/mmc/core/{core.c,sd.c,mmc.c,block.c}`, `include/linux/mmc/host.h`, `drivers/mmc/host/sdhci.c`, `block/blk-mq.c`, `include/linux/blk-mq.h`, `drivers/block/null_blk/main.c` — elixir.bootlin.com에서 같은 경로로 볼 수 있다
- Bootlin, "Embedded Linux system development" 교육 자료의 flash 파일시스템·블록 장치 장(무료 슬라이드)
- JEDEC JESD84-B51 (eMMC 5.1), SD Association "Physical Layer Simplified Specification" — 명령·레지스터 정의 원문
- 선행·후속 노트: G1(blk-mq 기초), C2(nandsim 배드 블록·ECC, eMMC boot 파티션·EXT_CSD), J1(임베디드 저장장치), J4(Flash FS), J5(전원 차단 안전성), M(ftrace)
