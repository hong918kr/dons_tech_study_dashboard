# O2. remoteproc · rpmsg · OpenAMP — Linux가 옆 코어에 펌웨어를 올리고 말을 거는 법

> **이 노트를 다 읽으면**: remoteproc가 ELF 펌웨어를 읽어 resource table을 해석하고 carveout · vring · trace를 준비한 뒤 코어를 깨우는 과정(`rproc_boot` → `rproc_fw_boot` → `rproc_start`)을 6.18.55 소스 함수 이름으로 따라갈 수 있다 · Cortex-M ELF에 진짜 resource table(vdev + vring 2개 + trace + carveout)을 넣어 빌드하고 `readelf`로 검사할 수 있다 · 가짜 remoteproc 드라이버와 가짜 원격 코어를 직접 짜서 offline → running → crashed → running, attach/detach, NS announce, `/dev/rpmsgN` 왕복까지 실제로 돌려 볼 수 있다 · OpenAMP/RPMsg-Lite/Zephyr 원격 쪽과 i.MX · STM32MP1 · TI K3 벤더 드라이버가 이 그림의 어디에 들어가는지 말할 수 있다
> **왜 필요한가**: i.MX8M/9, STM32MP1/2, TI AM62/AM64 같은 이종 SoC를 쓰는 로봇 · 드론 · 차량 회사에서 "M4 펌웨어는 어떻게 올라가나", "Linux와 M코어는 어떻게 메시지를 주고받나", "M코어가 죽으면 Linux가 어떻게 아나"는 BSP 엔지니어가 첫 달에 부딪히는 질문이고, 면접에서도 이종 코어 경험을 물을 때 그대로 나온다
> **Don 기준 난이도**: 공유 메모리 링 · doorbell · 펌웨어 이미지 레이아웃 · 링커 스크립트 · watchdog은 SSD 펌웨어(NVMe SQ/CQ + doorbell, 멀티 Cortex-R)로 이미 몸에 있다 / 새로 배울 부분은 **Linux 쪽 프레임워크** — `struct rproc`/`rproc_ops`, resource table이라는 "계약서", virtio vring을 코어 간 통신에 재활용하는 방식, rpmsg 주소 · endpoint · name service, 그리고 sysfs/debugfs/cdev로 이걸 운영하는 법
> **선행 노트**: G10(firmware loading — `request_firmware`), H10(mailbox · hwspinlock — kick의 하드웨어), F10(CMA · reserved-memory — carveout의 출처), N4(Linux vs RTOS vs MCU 분담 — 왜 M코어를 쓰나). 같이 보면 좋은 노트: O1(Cortex-A + Cortex-M 구조), O3(외부 MCU 통신 설계), D-모듈(Device Tree), M-모듈(debugfs · dynamic debug), L4(OTA — 펌웨어 묶음 업데이트)

---

## 0. 큰 그림 — 한 칩 안의 "호스트와 컨트롤러"

Don이 SSD에서 하던 일을 떠올려 보자. 호스트(x86)의 NVMe 드라이버는 컨트롤러 펌웨어가 무엇을 하는지 모른다. 둘이 아는 것은 **약속된 메모리 구조**(Admin/IO Submission Queue, Completion Queue — 호스트 DRAM에 있는 링)와 **doorbell 레지스터**(새 엔트리가 있다는 알림) 뿐이다. 큐 주소와 크기는 초기화 때 Admin 명령으로 합의한다.

이종 SoC에서 Linux(Cortex-A)와 M코어(Cortex-M4/M7/R5F)의 관계가 정확히 이 모양이다. 다만 세 가지가 다르다.

1. **Linux가 상대의 펌웨어를 직접 올리고 리셋을 푼다.** SSD 호스트는 컨트롤러 펌웨어를 로드하지 않지만, 여기서는 Linux가 "부트로더" 역할까지 한다. 이게 **remoteproc**(remote processor framework)다.
2. **큐 주소 합의는 펌웨어 이미지 안의 표로 한다.** 펌웨어 ELF의 `.resource_table` 섹션에 "나는 vring 두 개, trace 버퍼 하나, 공유 메모리 64 KB가 필요하다"를 적어 두면, Linux가 메모리를 잡고 그 주소를 **같은 표에 다시 써 넣는다**. 이게 **resource table**이다.
3. **큐는 NVMe 큐가 아니라 virtio vring이다.** QEMU/KVM 가상 장치에 쓰던 virtio 링을 "Linux = 드라이버, M코어 = 장치"로 보고 그대로 재활용한다. 그 위에 주소 · 채널 개념을 얹은 메시지 프로토콜이 **rpmsg**(remote processor messaging)고, 원격 쪽에서 이 프로토콜을 구현한 오픈소스 라이브러리가 **OpenAMP**(+ 그 HAL인 libmetal)다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="135" y="20" text-anchor="middle" font-size="14">Linux (Cortex-A)</text>
  <text x="545" y="20" text-anchor="middle" font-size="14">원격 코어 (Cortex-M/R)</text>
  <rect x="20" y="32" width="230" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="135" y="57" text-anchor="middle" font-size="13">앱: /dev/rpmsgN, sysfs</text>
  <rect x="20" y="80" width="230" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="135" y="105" text-anchor="middle" font-size="13">rpmsg_char · rpmsg 드라이버</text>
  <rect x="20" y="128" width="230" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="135" y="153" text-anchor="middle" font-size="13">rpmsg core + virtio_rpmsg_bus</text>
  <rect x="20" y="176" width="230" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="135" y="201" text-anchor="middle" font-size="13">virtio_ring (vring 드라이버 쪽)</text>
  <rect x="20" y="224" width="230" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="135" y="249" text-anchor="middle" font-size="13">remoteproc core + rproc_ops</text>
  <rect x="20" y="272" width="230" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="135" y="297" text-anchor="middle" font-size="13">mailbox 드라이버 (H10)</text>
  <rect x="430" y="32" width="230" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="545" y="57" text-anchor="middle" font-size="13">RTOS 앱 (모터 제어 등)</text>
  <rect x="430" y="80" width="230" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="545" y="105" text-anchor="middle" font-size="13">OpenAMP rpmsg / RPMsg-Lite</text>
  <rect x="430" y="128" width="230" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="545" y="153" text-anchor="middle" font-size="13">virtio (vring 장치 쪽)</text>
  <rect x="430" y="176" width="230" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="545" y="201" text-anchor="middle" font-size="13">libmetal (shmem, IPI, 락)</text>
  <rect x="430" y="224" width="230" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="545" y="249" text-anchor="middle" font-size="13">.resource_table (ELF 안)</text>
  <rect x="430" y="272" width="230" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="545" y="297" text-anchor="middle" font-size="13">IPI / MU / IPCC 하드웨어</text>
  <rect x="270" y="128" width="140" height="150" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="148" text-anchor="middle" font-size="13">공유 메모리</text>
  <text x="340" y="172" text-anchor="middle" font-size="12">vring0 (A ← M)</text>
  <text x="340" y="192" text-anchor="middle" font-size="12">vring1 (A → M)</text>
  <text x="340" y="212" text-anchor="middle" font-size="12">버퍼 512 B x N</text>
  <text x="340" y="232" text-anchor="middle" font-size="12">resource table</text>
  <text x="340" y="252" text-anchor="middle" font-size="12">trace 버퍼</text>
  <line x1="250" y1="196" x2="268" y2="196" stroke="currentColor" stroke-dasharray="4 3"/>
  <line x1="412" y1="196" x2="430" y2="196" stroke="currentColor" stroke-dasharray="4 3"/>
  <line x1="250" y1="292" x2="428" y2="292" stroke="#d0564a" stroke-width="2" marker-start="url(#ah1)" marker-end="url(#ah1)"/>
  <text x="340" y="312" text-anchor="middle" font-size="12">kick = doorbell 인터럽트</text>
  <line x1="135" y1="320" x2="135" y2="350" stroke="#3f9a6b" marker-end="url(#ah1)"/>
  <text x="20" y="368" font-size="12">remoteproc: ELF 로드 · 리셋 해제 · 정지 · crash 복구</text>
  <text x="20" y="388" font-size="12">rpmsg: vring 위의 주소 기반 메시지 (src, dst, len, data)</text>
</svg>
```

그림 1 — 이종 SoC 메시징의 층. 데이터는 공유 메모리의 vring으로 가고, mailbox는 "새 데이터가 있다"는 알림(kick)만 나른다. 왼쪽 초록 층(remoteproc)이 원격 코어의 생명주기를, 파란 층(rpmsg/virtio)이 메시지를 맡는다.

Don의 경험과 1:1로 대응시키면 이렇다.

| 이 노트의 개념 | SSD/NVMe에서의 대응 | 다른 점 |
|---|---|---|
| remoteproc `start`/`stop` | 컨트롤러 리셋 해제 · 펌웨어 활성화 | Linux가 펌웨어 로더까지 겸한다 |
| resource table | Admin Queue 속성(ASQ/ACQ 주소, 크기) 합의 | 펌웨어가 요구, Linux가 주소를 채워 되돌려 준다 |
| vring (desc · avail · used) | SQ/CQ 링 + head/tail 포인터 | 버퍼 소유권을 desc 인덱스로 주고받는다 |
| kick (mailbox doorbell) | SQ tail doorbell / MSI-X 인터럽트 | 방향마다 mailbox 채널이 따로 |
| rpmsg endpoint 주소 | NVMe namespace / queue ID | 32비트 주소, 양쪽이 각자 할당 |
| trace 버퍼 | 펌웨어 내부 로그 링(UART 없이 보는 로그) | debugfs `trace0`으로 바로 읽힌다 |
| coredump | 펌웨어 crash dump(SRAM dump) | devcoredump로 ELF core 파일이 나온다 |

### 0.1 이 노트의 랩 — 무엇이 진짜고 무엇이 가짜인가

QEMU `virt` 머신에는 Cortex-M 보조 코어가 없다(N4 §10에서 확인). 그래서 이 노트는 다음처럼 구성했다. 커널 쪽 코드는 **전부 진짜 6.18.55 mainline 코드**가 돈다. 가짜인 것은 맨 아래 두 칸뿐이다.

| 층 | 이 랩에서 | 실물 보드에서 |
|---|---|---|
| 펌웨어 ELF | `arm-none-eabi-gcc`로 빌드한 진짜 Cortex-M4 ELF (QEMU `mps2-an386`에서 실제로 실행됨) | 같은 형태의 ELF (OpenAMP/Zephyr 앱) |
| remoteproc core, ELF loader, resource table 처리 | 진짜 (`drivers/remoteproc/`) | 같음 |
| virtio_rpmsg_bus, rpmsg_ns, rpmsg_char, rpmsg_ctrl, rpmsg_client_sample | 진짜 (`drivers/rpmsg/`, `samples/rpmsg/`) | 같음 |
| 벤더 rproc 드라이버 (`rproc_ops`) | **가짜** `o2_fakerproc.ko`: start/stop은 로그만, `da_to_va`는 커널 버퍼 64 KB 두 개(가짜 TCM/SRAM) | `imx_rproc.c`, `stm32_rproc.c`, `ti_k3_r5_remoteproc.c` |
| 원격 코어 + kick | **가짜**: 같은 모듈 안의 workqueue가 vring을 "장치 쪽"에서 읽고 쓴다 | M4 위의 OpenAMP + mailbox IRQ |

실행 환경: Lima VM `elinux`(호스트 커널 `7.0.0-38-generic`, aarch64) 안의 QEMU TCG 게스트, 게스트 커널은 랩의 `Image-dom` = **6.18.55-dom**. 시간 측정값은 TCG 에뮬레이션 + 19명이 공유하는 VM에서 나온 숫자라 실물 하드웨어 수치가 아니다(경향만 본다).

---

## 1. remoteproc 프레임워크 — 원격 코어 하나 = `struct rproc` 하나

### 1.1 직관: "부트로더 + 전원 관리자 + 장애 관리자"

remoteproc가 해 주는 일은 셋이다.

- **로드**: `request_firmware()`(G10)로 `/lib/firmware/<이름>`을 읽고, ELF의 `PT_LOAD` 세그먼트를 원격 코어 메모리에 복사한다.
- **생명주기**: 리셋 해제(start), 리셋(stop), 이미 떠 있는 코어에 붙기(attach)/떨어지기(detach).
- **장애 처리**: 원격 코어가 죽었다고 드라이버가 알리면(`rproc_report_crash`) coredump를 만들고 다시 부팅(recovery)한다.

벤더마다 다른 부분 — 리셋을 어떤 레지스터로 푸는지, 원격 주소를 Linux 주소로 어떻게 바꾸는지, doorbell을 어떻게 울리는지 — 만 `struct rproc_ops`로 드라이버가 채운다. 나머지는 core가 공통으로 한다. G-모듈의 "프레임워크가 공통 로직, 드라이버는 콜백" 패턴 그대로다.

### 1.2 `struct rproc_ops` — 드라이버가 채우는 표

6.18.55 `include/linux/remoteproc.h`의 실제 정의다(주석 생략).

```c
struct rproc_ops {
	int (*prepare)(struct rproc *rproc);
	int (*unprepare)(struct rproc *rproc);
	int (*start)(struct rproc *rproc);
	int (*stop)(struct rproc *rproc);
	int (*attach)(struct rproc *rproc);
	int (*detach)(struct rproc *rproc);
	void (*kick)(struct rproc *rproc, int vqid);
	void * (*da_to_va)(struct rproc *rproc, u64 da, size_t len, bool *is_iomem);
	int (*parse_fw)(struct rproc *rproc, const struct firmware *fw);
	int (*handle_rsc)(struct rproc *rproc, u32 rsc_type, void *rsc,
			  int offset, int avail);
	struct resource_table *(*find_loaded_rsc_table)(
				struct rproc *rproc, const struct firmware *fw);
	struct resource_table *(*get_loaded_rsc_table)(
				struct rproc *rproc, size_t *size);
	int (*load)(struct rproc *rproc, const struct firmware *fw);
	int (*sanity_check)(struct rproc *rproc, const struct firmware *fw);
	u64 (*get_boot_addr)(struct rproc *rproc, const struct firmware *fw);
	unsigned long (*panic)(struct rproc *rproc);
	void (*coredump)(struct rproc *rproc);
};
```

| op | 언제 불리나 | 하는 일 (실물 예) | 비워 두면 |
|---|---|---|---|
| `prepare` / `unprepare` | 로드 전 / 정지 후 | 원격 메모리 클럭 · 전원 켜기 (i.MX: TCM 접근 준비) | 생략 |
| `start` | 세그먼트 로드 + vdev 준비 후 | 리셋 해제, 부트 주소 설정 (STM32: `hold_boot` syscon 해제) | 필수 |
| `stop` | shutdown · recovery | 코어를 리셋에 묶기 | 필수(attach 전용 아니면) |
| `attach` / `detach` | 부트로더가 이미 띄운 코어 | 리셋 건드리지 않고 연결만 | attach 모드 불가 |
| `kick` | virtio가 상대를 깨울 때 | mailbox에 vqid 쓰기 | vdev 쓰면 필수 (`.kick method not defined` 에러) |
| `da_to_va` | ELF 로드 · trace 읽기 · 표 찾기 | 원격 주소(da) → 커널 가상 주소 | carveout 목록만 검색 |
| `parse_fw` | 로드 전 | resource table 찾아 복사 | ELF 기본값 |
| `find_loaded_rsc_table` | 로드 후 | 원격 메모리 안 표의 위치 | ELF 기본값 |
| `get_loaded_rsc_table` | attach | 이미 떠 있는 코어의 표 위치 | 표 없이 attach |
| `load` / `sanity_check` / `get_boot_addr` | 로드 | ELF 세그먼트 복사 · 검사 · 진입점 | **`load`가 비면 ELF 기본값 일괄 대입** (§3.3 함정) |
| `coredump` | recovery 직전 | 메모리 덤프 | `rproc_coredump` 기본값 |

여기서 `da`는 **device address** — 원격 코어가 보는 주소다. Cortex-M4에게 TCM은 `0x0000_0000`에 있지만, Linux(Cortex-A)에게 같은 SRAM은 `0x007E_0000` 같은 전혀 다른 물리 주소에 있다(주소는 예시). `da_to_va`가 이 번역을 맡는다. SSD 컨트롤러에서 "코어 로컬 TCM 주소 vs 시스템 버스 주소"를 구분하던 것과 같다.

### 1.3 등록: `rproc_alloc` → `rproc_add`

드라이버 probe에서 하는 일은 짧다.

```c
rproc = devm_rproc_alloc(dev, "o2-m4", &o2_ops, "o2_m4_fw.elf", sizeof(*priv));
rproc->auto_boot = false;   /* true면 rproc_add()가 곧바로 request_firmware_nowait() */
ret = rproc_add(rproc);     /* /sys/class/remoteproc/remoteprocN, debugfs, /dev/remoteprocN 생성 */
```

`rproc_alloc()`은 `struct rproc`를 만들고 ops를 **복사**한다(`kmemdup`). 이 복사 과정에서 기본값이 채워진다 — 6.18.55 `rproc_alloc_ops()`의 실제 코드다.

```c
	/* Default to rproc_coredump if no coredump function is specified */
	if (!rproc->ops->coredump)
		rproc->ops->coredump = rproc_coredump;

	if (rproc->ops->load)
		return 0;

	/* Default to ELF loader if no load function is specified */
	rproc->ops->load = rproc_elf_load_segments;
	rproc->ops->parse_fw = rproc_elf_load_rsc_table;
	rproc->ops->find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table;
	rproc->ops->sanity_check = rproc_elf_sanity_check;
	rproc->ops->get_boot_addr = rproc_elf_get_boot_addr;
```

`load`를 비우면 `parse_fw`까지 **덮어쓴다**. 이 랩에서 실제로 이것 때문에 coredump가 안 나왔다(§7에서 증상과 함께 다시 본다). 그래서 `stm32_rproc.c`, `imx_rproc.c`는 `.load = rproc_elf_load_segments`부터 다섯 개를 전부 명시한다.

### 1.4 사용자에게 보이는 세 창구: sysfs · debugfs · cdev

| 경로 | 파일 | 쓰기 | 읽기 |
|---|---|---|---|
| `/sys/class/remoteproc/remoteprocN/` | `firmware` | 파일 이름 (offline일 때만, 아니면 `-EBUSY`) | 현재 이름 |
| 〃 | `state` | `start`, `stop`, `detach` | `offline` `suspended` `running` `crashed` `attached` `detached` |
| 〃 | `name`, `recovery`, `coredump` | recovery: `enabled` `disabled` `recover` | 이름, 설정 |
| `/sys/kernel/debug/remoteproc/remoteprocN/` | `resource_table` | — | 현재 표 해석본 |
| 〃 | `carveout_memories` | — | 할당된 메모리 영역 |
| 〃 | `traceN` | — | 원격 코어 로그 버퍼 |
| 〃 | `crash` | 숫자 0/1/2 (mmufault/watchdog/fatal) | — |
| 〃 | `recovery`, `coredump` | coredump: `disabled` `enabled` `inline` | 설정 |
| `/dev/remoteprocN` (CONFIG_REMOTEPROC_CDEV) | write | `start`, `stop`, `detach` | — |
| 〃 | ioctl | `RPROC_SET_SHUTDOWN_ON_RELEASE` | `RPROC_GET_SHUTDOWN_ON_RELEASE` |

상태 이름은 `remoteproc_sysfs.c`의 `rproc_state_string[]`에서, `state`에 쓸 수 있는 명령은 같은 파일의 `sysfs_streq(buf, "start"/"stop"/"detach")`에서 확인했다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="40" y="40" width="120" height="44" rx="22" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="100" y="67" text-anchor="middle" font-size="13">offline</text>
  <rect x="280" y="40" width="120" height="44" rx="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="67" text-anchor="middle" font-size="13">running</text>
  <rect x="520" y="40" width="120" height="44" rx="22" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="580" y="67" text-anchor="middle" font-size="13">crashed</text>
  <rect x="40" y="230" width="120" height="44" rx="22" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="100" y="257" text-anchor="middle" font-size="13">detached</text>
  <rect x="280" y="230" width="120" height="44" rx="22" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="257" text-anchor="middle" font-size="13">attached</text>
  <line x1="160" y1="52" x2="278" y2="52" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="219" y="34" text-anchor="middle" font-size="12">start</text>
  <text x="219" y="22" text-anchor="middle" font-size="12">rproc_fw_boot</text>
  <line x1="278" y1="74" x2="162" y2="74" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="219" y="96" text-anchor="middle" font-size="12">stop</text>
  <line x1="400" y1="52" x2="518" y2="52" stroke="#d0564a" marker-end="url(#ah2)"/>
  <text x="459" y="34" text-anchor="middle" font-size="12">report_crash</text>
  <line x1="518" y1="74" x2="402" y2="74" stroke="#3f9a6b" marker-end="url(#ah2)"/>
  <text x="459" y="96" text-anchor="middle" font-size="12">recovery</text>
  <text x="459" y="112" text-anchor="middle" font-size="12">stop+dump+start</text>
  <line x1="160" y1="244" x2="278" y2="244" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="219" y="226" text-anchor="middle" font-size="12">start (attach)</text>
  <line x1="278" y1="264" x2="162" y2="264" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="219" y="290" text-anchor="middle" font-size="12">detach</text>
  <line x1="320" y1="228" x2="120" y2="88" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="250" y="150" text-anchor="middle" font-size="12">stop</text>
  <line x1="100" y1="128" x2="100" y2="226" stroke="#e08a3c" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
  <text x="108" y="150" font-size="12">probe에서</text>
  <text x="108" y="166" font-size="12">state=DETACHED</text>
  <text x="108" y="182" font-size="12">(부트로더가 띄움)</text>
  <text x="430" y="200" font-size="12">recovery=disabled면</text>
  <text x="430" y="216" font-size="12">crashed에 머문다</text>
  <text x="430" y="250" font-size="12">attach 중 crash는</text>
  <text x="430" y="266" font-size="12">detach → attach로 복구</text>
  <text x="430" y="282" font-size="12">(FEAT_ATTACH_ON_RECOVERY)</text>
</svg>
```

그림 2 — remoteproc 상태 전이(이 노트 §4, §7, §8에서 전부 실제로 밟아 본다). `suspended`는 생략했다. 같은 코어를 Linux가 직접 부팅하면 `running`, 이미 떠 있는 코어에 붙으면 `attached`로 표시된다.

---

## 2. resource table — 펌웨어와 Linux 사이의 계약서

### 2.1 직관

M4 펌웨어는 빌드할 때 자기가 무엇을 필요로 하는지 안다. "rpmsg를 쓸 거라 vring 두 개(각 16칸)가 필요하다", "로그는 내 SRAM의 이 주소 1 KB에 찍겠다", "센서 데이터용으로 64 KB 공유 메모리를 달라". 하지만 그 공유 메모리가 **DDR의 어디에 잡힐지**는 Linux만 안다. 그래서 펌웨어는 모르는 칸을 `FW_RSC_ADDR_ANY`(0xFFFFFFFF)로 비워 둔 표를 이미지에 넣고, Linux는 메모리를 잡은 뒤 **원격 메모리에 올라간 그 표에 주소를 써 넣는다.** 원격 코어는 부팅 후 자기 표를 읽어서 vring 주소를 알아낸다.

### 2.2 형식 — 6.18.55의 실제 정의

이 트리에서는 구조체가 `include/linux/rsc_table.h`에 있다(`remoteproc.h`가 include한다). 핵심만 옮긴다.

```c
struct resource_table {          /* 표 머리 */
	u32 ver;                     /* 반드시 1 */
	u32 num;                     /* 엔트리 개수 */
	u32 reserved[2];             /* 반드시 0 */
	u32 offset[];                /* 각 엔트리의 표 시작 기준 바이트 오프셋 */
} __packed;

struct fw_rsc_hdr { u32 type; u8 data[]; } __packed;   /* 엔트리마다 type이 먼저 */

enum fw_resource_type {
	RSC_CARVEOUT = 0, RSC_DEVMEM = 1, RSC_TRACE = 2, RSC_VDEV = 3,
	RSC_LAST = 4, RSC_VENDOR_START = 128, RSC_VENDOR_END = 512,
};
#define FW_RSC_ADDR_ANY (-1)

struct fw_rsc_carveout { u32 da, pa, len, flags, reserved; u8 name[32]; } __packed;
struct fw_rsc_devmem   { u32 da, pa, len, flags, reserved; u8 name[32]; } __packed;
struct fw_rsc_trace    { u32 da, len, reserved; u8 name[32]; } __packed;
struct fw_rsc_vdev_vring { u32 da, align, num, notifyid, pa; } __packed;
struct fw_rsc_vdev {
	u32 id, notifyid, dfeatures, gfeatures, config_len;
	u8 status, num_of_vrings, reserved[2];
	struct fw_rsc_vdev_vring vring[];
} __packed;
```

| type | 이름 | 원격이 요청하는 것 | Linux가 하는 일 (`remoteproc_core.c`) |
|---|---|---|---|
| 0 | `RSC_CARVEOUT` | 연속 메모리 `len` 바이트 (da 지정 또는 ANY) | `rproc_handle_carveout` → 이름이 같은 사전 등록 carveout(DT reserved-memory)이 있으면 그걸 쓰고, 없으면 `dma_alloc_coherent()`로 잡아 da/pa를 표에 기록 |
| 1 | `RSC_DEVMEM` | 주변장치 영역을 원격의 IOMMU에 매핑 | `rproc_handle_devmem` → **IOMMU domain이 없으면 `-EINVAL`**로 부팅 실패 |
| 2 | `RSC_TRACE` | 원격이 로그를 찍는 버퍼 위치 | `rproc_handle_trace` → debugfs `traceN` 파일 생성 (읽을 때 `da_to_va`) |
| 3 | `RSC_VDEV` | virtio 장치 하나 (rpmsg = id 7) + vring 목록 | `rproc_handle_vdev` → `rproc-virtio` 플랫폼 장치 등록 → vring 메모리 할당, notifyid 배정 |
| 128~512 | vendor | 벤더 확장 | `ops->handle_rsc`가 처리, 모르면 경고만 |

"devmem은 IOMMU 없으면 실패"는 소스에서 직접 확인한 것이다.

```c
static int rproc_handle_devmem(struct rproc *rproc, void *ptr, int offset, int avail)
{
	...
	/* no point in handling this resource without a valid iommu domain */
	if (!rproc->domain)
		return -EINVAL;
```

그래서 이 노트의 펌웨어는 devmem 엔트리를 넣지 않았다. IOMMU 없이 M코어가 주변장치를 직접 보는 i.MX · STM32 계열 펌웨어도 보통 devmem을 쓰지 않는다.

### 2.3 예제 1 — 원격 쪽 표 정의 (펌웨어 헤더)

무엇을 확인하는 코드인가: 원격 쪽(펌웨어)이 커널과 **바이트 단위로 같은** 레이아웃을 자기 헤더로 정의한다는 것. OpenAMP도 `rsc_table.h`를 따로 갖고 있다(같은 형식). 여기서는 `type` 필드를 각 엔트리 구조체의 첫 멤버로 넣어 `fw_rsc_hdr`를 겸하게 했다.

```c
/* rsc_table.h — remote-side resource table layout (same bytes as Linux include/linux/rsc_table.h) */
#ifndef O2_RSC_TABLE_H
#define O2_RSC_TABLE_H
#include <stdint.h>
#define RSC_CARVEOUT 0
#define RSC_DEVMEM   1
#define RSC_TRACE    2
#define RSC_VDEV     3
#define FW_RSC_ADDR_ANY   0xFFFFFFFFu
#define VIRTIO_ID_RPMSG   7
#define VIRTIO_RPMSG_F_NS 0          /* feature bit 0: name service */
#define NUM_ENTRIES 3

struct fw_rsc_carveout { uint32_t type, da, pa, len, flags, reserved; char name[32]; };
struct fw_rsc_trace    { uint32_t type, da, len, reserved; char name[32]; };
struct fw_rsc_vring    { uint32_t da, align, num, notifyid, pa; };
struct fw_rsc_vdev {
    uint32_t type, id, notifyid, dfeatures, gfeatures, config_len;
    uint8_t  status, num_of_vrings, reserved[2];
    struct fw_rsc_vring vring[2];
};
struct remote_resource_table {
    uint32_t ver, num, reserved[2];          /* struct resource_table header */
    uint32_t offset[NUM_ENTRIES];            /* byte offsets from table start */
    struct fw_rsc_vdev     vdev;             /* type field = struct fw_rsc_hdr */
    struct fw_rsc_trace    trace;
    struct fw_rsc_carveout shm;
} __attribute__((packed));
#endif
```

출력에서 볼 것(다음 예제들): `packed`라서 엔트리 사이에 패딩이 없다. 패딩이 끼면 Linux의 `offset[]`과 실제 위치가 어긋나 `rsc table is truncated` 같은 에러가 난다.

### 2.4 예제 2 — 펌웨어 본체: 표 + trace 버퍼 + 작은 main

무엇을 확인하는 코드인가: 진짜 Cortex-M4 이미지에 `.resource_table`과 `.trace` 섹션을 넣는 방법. vdev는 rpmsg(id 7), name service 기능 비트(`dfeatures` bit 0), vring 두 개(각 16칸, align 4096, 주소는 ANY)를 요청한다. trace 버퍼는 일부러 **초기값이 있는** 배열로 만들어, Linux의 ELF loader가 이 내용을 복사했는지 trace0에서 눈으로 확인할 수 있게 했다.

```c
/* fw.c — tiny Cortex-M4 "remote" image: vector table + resource table + trace buffer */
#include <stddef.h>
#include "rsc_table.h"
#define TRACE_LEN 1024
#define VRING_NUM 16
#define VRING_ALIGN 4096
#define UART0_DATA (*(volatile uint32_t *)0x40004000)   /* CMSDK UART on mps2-an386 */
#define UART0_CTRL (*(volatile uint32_t *)0x40004008)
#define UART0_BAUD (*(volatile uint32_t *)0x40004010)

/* Trace buffer: Linux shows it as debugfs .../trace0 (initialised so we can see the copy) */
__attribute__((section(".trace"), used))
char trace_buf[TRACE_LEN] = "[o2-fw] boot banner: image v1.0 (written by ELF loader)\n";

__attribute__((section(".resource_table"), used))
struct remote_resource_table resource_table = {
    .ver = 1, .num = NUM_ENTRIES,
    .offset = { offsetof(struct remote_resource_table, vdev),
                offsetof(struct remote_resource_table, trace),
                offsetof(struct remote_resource_table, shm) },
    .vdev = { RSC_VDEV, VIRTIO_ID_RPMSG, 0, 1u << VIRTIO_RPMSG_F_NS, 0, 0, 0, 2, {0, 0},
              { { FW_RSC_ADDR_ANY, VRING_ALIGN, VRING_NUM, FW_RSC_ADDR_ANY, 0 },     /* vring0: Linux RX */
                { FW_RSC_ADDR_ANY, VRING_ALIGN, VRING_NUM, FW_RSC_ADDR_ANY, 0 } } }, /* vring1: Linux TX */
    .trace = { RSC_TRACE, (uint32_t)(uintptr_t)trace_buf, TRACE_LEN, 0, "o2_trace" },
    .shm   = { RSC_CARVEOUT, FW_RSC_ADDR_ANY, FW_RSC_ADDR_ANY, 0x10000, 0, 0, "o2_shm" },
};

static size_t tpos;
static void tputs(const char *s)              /* log to UART and to the shared trace buffer */
{
    for (; *s; s++) {
        UART0_DATA = (uint32_t)*s;
        if (tpos < TRACE_LEN - 1) trace_buf[tpos++] = *s;
    }
}
static void tputx(uint32_t v)
{
    char b[11] = "0x";
    for (int i = 0; i < 8; i++) b[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 0xf];
    b[10] = 0; tputs(b);
}

int main(void)
{
    while (trace_buf[tpos]) tpos++;           /* append after the banner */
    UART0_BAUD = 16; UART0_CTRL = 1;          /* TX enable */
    tputs("[o2-fw] running; resource_table @ "); tputx((uint32_t)(uintptr_t)&resource_table);
    tputs(" vdev.status="); tputx(resource_table.vdev.status);
    tputs("\n[o2-fw] (a real OpenAMP app would now wait for DRIVER_OK and init vrings)\n");
    for (;;) __asm__ volatile("wfi");
}
```

벡터 테이블과 리셋 핸들러, 링커 스크립트는 이렇다. 링커 스크립트의 핵심은 **`.resource_table`과 `.trace`를 `AT > SRAM`으로 두어 LMA(로드 주소)와 VMA(실행 주소)를 같게** 하는 것이다(이유는 예제 5).

```c
/* startup.c — minimal Cortex-M vector table and reset handler */
#include <stdint.h>
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
int main(void);
void Reset_Handler(void)
{
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;           /* .data: flash(LMA) -> RAM(VMA) */
    for (d = &_sbss; d < &_ebss; ) *d++ = 0;
    main();
    for (;;) ;
}
void Default_Handler(void) { for (;;) ; }
__attribute__((section(".isr_vector"), used))
const void *vectors[16] = { &_estack, Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, Default_Handler };
```

```text
/* fw.ld — Cortex-M4: code in "ITCM" @0x0, data in SRAM @0x20000000 (mps2-an386 compatible) */
MEMORY {
  ITCM (rx)  : ORIGIN = 0x00000000, LENGTH = 64K
  SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
ENTRY(Reset_Handler)
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > ITCM
  .text   : { *(.text*) *(.rodata*) } > ITCM
  .resource_table : { KEEP(*(.resource_table)) } > SRAM AT > SRAM  /* LMA == VMA */
  .data   : { _sdata = .; *(.data*) _edata = .; } > SRAM AT > ITCM
  _sidata = LOADADDR(.data);
  .trace  : { . = ALIGN(16); KEEP(*(.trace)) } > SRAM AT > SRAM  /* LMA == VMA */
  .bss    : { _sbss = .; *(.bss*) *(COMMON) _ebss = .; } > SRAM
  _estack = ORIGIN(SRAM) + LENGTH(SRAM);
}
```

메모리 주소를 `0x0`(ITCM 역할)과 `0x2000_0000`(SRAM 역할)으로 고른 이유는 두 가지다. Cortex-M의 표준 메모리 맵(Code 영역, SRAM 영역)과 맞고, QEMU `mps2-an386`(Cortex-M4 보드 모델)에도 같은 주소에 RAM이 있어서 **같은 ELF를 진짜 M4 에뮬레이터에서 돌려 볼 수 있다.**

### 2.5 예제 3 — 빌드하고 진짜 Cortex-M4(QEMU)에서 실행

무엇을 확인하나: 이 ELF가 장식이 아니라 실제로 부팅하는 M4 이미지라는 것, 그리고 원격 쪽에서 본 표 주소.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/O2 && F=/Users/donh/.elinux_scratch/O2/fw
arm-none-eabi-gcc --version | head -1
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -O2 -Wall -Wextra -ffreestanding -nostdlib \
    -T $F/fw.ld $F/startup.c $F/fw.c -o o2_m4_fw.elf
timeout 4 qemu-system-arm -M mps2-an386 -nographic -kernel o2_m4_fw.elf -monitor none -serial stdio'
```

```text
arm-none-eabi-gcc (15:14.2.rel1-1) 14.2.1 20241119
[o2-fw] running; resource_table @ 0x20000000 vdev.status=0x00000000
[o2-fw] (a real OpenAMP app would now wait for DRIVER_OK and init vrings)
qemu-system-arm: terminating on signal 15 from pid 2316120 (timeout)
```

출력에서 볼 것: 경고 0개로 빌드되고, M4가 리셋 벡터에서 출발해 UART에 찍는다. 여기서는 Linux가 없으니 `vdev.status`가 0이다. 실제 OpenAMP 앱은 이 값에 `DRIVER_OK`(bit 2)가 설 때까지 기다린 뒤 vring을 쓴다 — Linux 쪽 virtio 드라이버가 준비됐다는 신호이기 때문이다(§5).

### 2.6 예제 4 — readelf / objdump로 표 검사

무엇을 확인하나: remoteproc가 실제로 쓰는 두 정보 — **섹션 헤더의 `.resource_table`**(표 찾기, `sh_addr` = 원격 주소)과 **프로그램 헤더의 `PT_LOAD`**(무엇을 어디에 복사할지, `p_paddr` 기준).

```sh
arm-none-eabi-readelf -h o2_m4_fw.elf | grep -E "Class|Machine|Entry|Flags"
arm-none-eabi-readelf -S -W o2_m4_fw.elf | sed -n 5,12p
arm-none-eabi-readelf -l o2_m4_fw.elf | sed -n '/Program Headers/,$p'
arm-none-eabi-nm -n o2_m4_fw.elf | grep -E "resource_table|trace_buf|vectors|Reset|main"
arm-none-eabi-objdump -s -j .resource_table o2_m4_fw.elf | tail -14
```

```text
  Class:                             ELF32
  Machine:                           ARM
  Entry point address:               0x45
  Flags:                             0x5000200, Version5 EABI, soft-float ABI
  [ 0]                   NULL            00000000 000000 000000 00      0   0  0
  [ 1] .isr_vector       PROGBITS        00000000 010000 000040 00  WA  0   0  4
  [ 2] .text             PROGBITS        00000040 010040 0001d8 00  AX  0   0  4
  [ 3] .resource_table   PROGBITS        20000000 020000 0000c8 00  WA  0   0  4
  [ 4] .trace            PROGBITS        200000c8 0200c8 000408 00  WA  0   0  4
  [ 5] .bss              NOBITS          200004d0 0204d0 000004 00  WA  0   0  4
  [ 6] .comment          PROGBITS        00000000 0204d0 000026 01  MS  0   0  1
  [ 7] .ARM.attributes   ARM_ATTRIBUTES  00000000 0204f6 00002e 00      0   0  1
Program Headers:
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x010000 0x00000000 0x00000000 0x00218 0x00218 RWE 0x10000
  LOAD           0x020000 0x20000000 0x20000000 0x004d0 0x004d4 RW  0x10000

 Section to Segment mapping:
  Segment Sections...
   00     .isr_vector .text 
   01     .resource_table .trace .bss 
00000000 D vectors
00000044 T Reset_Handler
00000118 T main
20000000 D resource_table
200000d0 D trace_buf
Contents of section .resource_table:
 20000000 01000000 03000000 00000000 00000000  ................
 20000010 1c000000 60000000 90000000 03000000  ....`...........
 20000020 07000000 00000000 01000000 00000000  ................
 20000030 00000000 00020000 ffffffff 00100000  ................
 20000040 10000000 ffffffff 00000000 ffffffff  ................
 20000050 00100000 10000000 ffffffff 00000000  ................
 20000060 02000000 d0000020 00040000 00000000  ....... ........
 20000070 6f325f74 72616365 00000000 00000000  o2_trace........
 20000080 00000000 00000000 00000000 00000000  ................
 20000090 00000000 ffffffff ffffffff 00000100  ................
 200000a0 00000000 00000000 6f325f73 686d0000  ........o2_shm..
 200000b0 00000000 00000000 00000000 00000000  ................
 200000c0 00000000 00000000                    ........        
```

출력에서 볼 것:

- Entry `0x45` = `Reset_Handler`(0x44) + Thumb 비트. remoteproc는 이 값을 `rproc->bootaddr`로 저장한다(`rproc_elf_get_boot_addr`). 랩 dmesg에서 `bootaddr 0x45`로 다시 나온다.
- 표 첫 줄 `01 / 03 / 0 / 0` = ver 1, 엔트리 3개. 다음 `1c 60 90` = 세 엔트리의 오프셋. `0x20000020`의 `07` = virtio id 7(rpmsg), 그 다음 칸 `01` = dfeatures bit 0(NS).
- `0x20000034`의 `00020000`은 바이트 순서로 `00 02 00 00` = status 0, num_of_vrings 2(vdev 시작 0x1c + 24). vring 칸에는 `ffffffff`(ANY) `00100000`(align 0x1000) `10000000`(num 16)이 보인다.

### 2.7 예제 5 — 커널처럼 표 해석하기 (Python)

무엇을 확인하나: `remoteproc_elf_loader.c`의 `find_table()`이 하는 일 — 섹션 이름 테이블에서 `.resource_table`을 찾고, ver · reserved · 크기를 검사하고, `offset[]`을 따라 엔트리를 순회 — 을 똑같이 해 본다. 펌웨어 CI에서 "표가 깨지지 않았나"를 검사하는 스크립트로 그대로 쓸 수 있다.

```python
#!/usr/bin/env python3
"""rsc_dump.py — find .resource_table in an ELF and decode it exactly like remoteproc does"""
import struct, sys
data = open(sys.argv[1], 'rb').read()
assert data[:4] == b'\x7fELF' and data[4] == 1, "ELF32 only in this demo"
shoff, = struct.unpack_from('<I', data, 0x20)
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x2e)
def shdr(i):            # name, type, flags, addr, offset, size
    return struct.unpack_from('<IIIIII', data, shoff + i * shentsize)
stroff = shdr(shstrndx)[4]
def name(n): return data[stroff + n: data.index(b'\0', stroff + n)].decode()
for i in range(shnum):
    n, _, _, addr, off, size = shdr(i)
    if name(n) == '.resource_table':
        break
else:
    sys.exit("no .resource_table section -> remoteproc would boot without resources")
print(f".resource_table: sh_addr(da)=0x{addr:08x} file_off=0x{off:x} size={size}")
ver, num = struct.unpack_from('<II', data, off)
offsets = struct.unpack_from(f'<{num}I', data, off + 16)
print(f"ver={ver} num={num} offsets={[hex(o) for o in offsets]}")
ANY = 0xffffffff
h = lambda v: 'ANY' if v == ANY else f'0x{v:x}'
for o in offsets:
    p = off + o
    t, = struct.unpack_from('<I', data, p)
    if t == 0:
        da, pa, ln, fl, _ = struct.unpack_from('<5I', data, p + 4)
        nm = data[p + 24:p + 56].split(b'\0')[0].decode()
        print(f"  [+0x{o:02x}] CARVEOUT '{nm}' da={h(da)} pa={h(pa)} len=0x{ln:x} flags={fl}")
    elif t == 2:
        da, ln, _ = struct.unpack_from('<3I', data, p + 4)
        nm = data[p + 16:p + 48].split(b'\0')[0].decode()
        print(f"  [+0x{o:02x}] TRACE    '{nm}' da=0x{da:x} len=0x{ln:x}")
    elif t == 3:
        vid, nid, dfeat, gfeat, cfglen = struct.unpack_from('<5I', data, p + 4)
        status, nvr = data[p + 24], data[p + 25]
        print(f"  [+0x{o:02x}] VDEV     id={vid} (7=rpmsg) notifyid={h(nid)} dfeatures=0x{dfeat:x}"
              f" gfeatures=0x{gfeat:x} config_len={cfglen} status=0x{status:x} vrings={nvr}")
        for k in range(nvr):
            da, al, nm_, nid, pa = struct.unpack_from('<5I', data, p + 28 + 20 * k)
            print(f"             vring{k}: da={h(da)} align={al} num={nm_} notifyid={h(nid)} pa={h(pa)}")
    else:
        print(f"  [+0x{o:02x}] type {t} (devmem=1 or vendor >=128)")
```

```sh
python3 -I /Users/donh/.elinux_scratch/O2/rsc_dump.py /var/tmp/O2/o2_m4_fw.elf
```

```text
.resource_table: sh_addr(da)=0x20000000 file_off=0x20000 size=200
ver=1 num=3 offsets=['0x1c', '0x60', '0x90']
  [+0x1c] VDEV     id=7 (7=rpmsg) notifyid=0x0 dfeatures=0x1 gfeatures=0x0 config_len=0 status=0x0 vrings=2
             vring0: da=ANY align=4096 num=16 notifyid=ANY pa=0x0
             vring1: da=ANY align=4096 num=16 notifyid=ANY pa=0x0
  [+0x60] TRACE    'o2_trace' da=0x200000d0 len=0x400
  [+0x90] CARVEOUT 'o2_shm' da=ANY pa=ANY len=0x10000 flags=0
```

출력에서 볼 것: 이 상태가 "부팅 전 계약서"다. ANY로 비어 있는 칸(vring da, notifyid, carveout da/pa)을 §4에서 Linux가 채운다. 이 한 장의 그림으로 정리하면 이렇다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="20" font-size="13">원격 주소 0x20000000 (.resource_table, 200 B)</text>
  <text x="470" y="20" font-size="13">누가 쓰나</text>
  <rect x="20" y="32" width="90" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="65" y="54" text-anchor="middle" font-size="12">+0x00</text>
  <rect x="110" y="32" width="320" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="270" y="54" text-anchor="middle" font-size="12">ver=1 · num=3 · reserved[2]=0</text>
  <rect x="20" y="66" width="90" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="65" y="88" text-anchor="middle" font-size="12">+0x10</text>
  <rect x="110" y="66" width="320" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="270" y="88" text-anchor="middle" font-size="12">offset[] = 0x1c, 0x60, 0x90</text>
  <rect x="20" y="100" width="90" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="65" y="132" text-anchor="middle" font-size="12">+0x1c</text>
  <rect x="110" y="100" width="320" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="270" y="122" text-anchor="middle" font-size="12">VDEV id=7 dfeatures=0x1</text>
  <text x="270" y="142" text-anchor="middle" font-size="12">gfeatures · status ← Linux</text>
  <rect x="20" y="156" width="90" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="65" y="181" text-anchor="middle" font-size="12">+0x38</text>
  <rect x="110" y="156" width="320" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="270" y="181" text-anchor="middle" font-size="12">vring0: da=ANY num=16 notifyid=ANY</text>
  <rect x="20" y="196" width="90" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="65" y="221" text-anchor="middle" font-size="12">+0x4c</text>
  <rect x="110" y="196" width="320" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="270" y="221" text-anchor="middle" font-size="12">vring1: da=ANY num=16 notifyid=ANY</text>
  <rect x="20" y="236" width="90" height="48" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="65" y="264" text-anchor="middle" font-size="12">+0x60</text>
  <rect x="110" y="236" width="320" height="48" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="270" y="264" text-anchor="middle" font-size="12">TRACE da=0x200000d0 len=0x400</text>
  <rect x="20" y="284" width="90" height="48" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="65" y="312" text-anchor="middle" font-size="12">+0x90</text>
  <rect x="110" y="284" width="320" height="48" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="270" y="312" text-anchor="middle" font-size="12">CARVEOUT o2_shm da=ANY len=64K</text>
  <line x1="432" y1="128" x2="468" y2="128" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="472" y="124" font-size="12">Linux: virtio 상태 · 협상 결과</text>
  <text x="472" y="140" font-size="12">(원격은 DRIVER_OK 대기)</text>
  <line x1="432" y1="196" x2="468" y2="196" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="472" y="192" font-size="12">Linux: vring 주소 · notifyid</text>
  <text x="472" y="208" font-size="12">(find_vqs 때 기록)</text>
  <line x1="432" y1="260" x2="468" y2="260" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="472" y="256" font-size="12">펌웨어: 고정 주소</text>
  <text x="472" y="272" font-size="12">(Linux는 읽기만)</text>
  <line x1="432" y1="308" x2="468" y2="308" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="472" y="304" font-size="12">Linux: dma 주소를 da/pa에</text>
  <text x="472" y="320" font-size="12">(alloc_carveout)</text>
  <text x="20" y="360" font-size="12">원본은 ELF 안. Linux는 kmemdup한 사본(cached_table)에서 작업하고,</text>
  <text x="20" y="380" font-size="12">세그먼트를 올린 뒤 원격 메모리의 표(loaded table)에 memcpy → 이후 수정은 그쪽에 직접</text>
</svg>
```

그림 3 — 이 노트 펌웨어의 resource table 레이아웃(바이트 오프셋은 예제 5의 출력)과, 각 칸을 누가 채우는지. Linux가 쓰는 칸은 부팅 중에 원격 메모리의 표에 직접 반영되므로, 원격은 부팅 후 자기 표를 다시 읽어 주소를 얻는다.

### 2.8 예제 6 — 함정: LMA ≠ VMA면 Linux가 엉뚱한 곳에 올린다

무엇을 확인하나: 처음 링커 스크립트에서 `.trace`에 `AT >`를 빼 먹은 버전(`fw_bad.ld`)을 빌드하면 무슨 일이 생기는가.

```sh
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -O2 -Wall -Wextra -ffreestanding -nostdlib \
    -T $F/fw_bad.ld $F/startup.c $F/fw.c -o bad.elf
arm-none-eabi-readelf -l bad.elf | sed -n '/Program Headers/,/^$/p'
```

```text
Program Headers:
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x010000 0x00000000 0x00000000 0x00218 0x00218 RWE 0x10000
  LOAD           0x0200c8 0x200000c8 0x00000218 0x00408 0x0040c RW  0x10000
  LOAD           0x030000 0x20000000 0x20000000 0x000c8 0x000c8 RW  0x10000
```

출력에서 볼 것: 두 번째 세그먼트의 VirtAddr는 `0x200000c8`인데 PhysAddr가 `0x218`(ITCM의 코드 바로 뒤)이다. GNU ld는 앞 섹션 `.data`의 `AT > ITCM`을 이어받아 `.trace`의 LMA도 ITCM에 둔다. remoteproc의 `rproc_elf_load_segments()`는 **`p_paddr`**(`elf_phdr_get_p_paddr`)로 복사 위치를 정하므로, trace 버퍼 초기값은 ITCM `0x218`에 올라가고 Linux가 `da=0x200000d0`에서 읽는 trace0에는 아무것도 없다. 실물 M4에서 startup이 LMA → VMA 복사를 하는 섹션이라면 그나마 동작하지만, **resource table이 이 꼴이면 치명적**이다: Linux는 `sh_addr`(VMA)의 표에 vring 주소를 써 넣는데, M4의 startup 코드가 부팅 직후 LMA의 원본으로 그 자리를 **덮어써서** Linux가 써 준 주소가 사라진다. 그래서 OpenAMP 예제 링커 스크립트들도 `.resource_table`을 `.data`와 분리해 따로 둔다(기억 기반 — 각 벤더 예제의 `.ld`에서 확인 필요).

---

## 3. 부팅 경로 walk-through — `echo start`부터 `running`까지

### 3.1 호출 그래프

`echo start > /sys/class/remoteproc/remoteproc0/state`가 들어가면 `state_store()` → `rproc_boot()`이다. 6.18.55 `remoteproc_core.c`를 그대로 따라가면 이렇다(함수 이름은 전부 소스에서 확인).

```svg
<svg viewBox="0 0 680 520" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="20" font-size="13">remoteproc core</text>
  <text x="440" y="20" font-size="13">드라이버 rproc_ops / 결과</text>
  <rect x="20" y="30" width="380" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="51" font-size="12">rproc_boot: mutex_lock, power 참조 +1</text>
  <rect x="20" y="70" width="380" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="91" font-size="12">request_firmware(rproc->firmware)  (G10)</text>
  <rect x="20" y="110" width="380" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="131" font-size="12">rproc_fw_boot: rproc_fw_sanity_check</text>
  <text x="440" y="131" font-size="12">sanity_check (ELF 검사)</text>
  <rect x="20" y="150" width="380" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="171" font-size="12">rproc_enable_iommu, rproc_prepare_device</text>
  <text x="440" y="171" font-size="12">prepare</text>
  <rect x="20" y="190" width="380" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="211" font-size="12">bootaddr = rproc_get_boot_addr</text>
  <text x="440" y="211" font-size="12">get_boot_addr (e_entry)</text>
  <rect x="20" y="230" width="380" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="251" font-size="12">rproc_parse_fw → cached_table (사본)</text>
  <text x="440" y="251" font-size="12">parse_fw</text>
  <rect x="20" y="270" width="380" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="291" font-size="12">rproc_handle_resources: carveout/trace/vdev</text>
  <text x="440" y="291" font-size="12">handle_rsc (vendor type만)</text>
  <rect x="20" y="310" width="380" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="331" font-size="12">rproc_alloc_registered_carveouts</text>
  <text x="440" y="331" font-size="12">dma_alloc_coherent / DT</text>
  <rect x="20" y="350" width="380" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="371" font-size="12">rproc_start: rproc_load_segments</text>
  <text x="440" y="371" font-size="12">load → da_to_va + memcpy</text>
  <rect x="20" y="390" width="380" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="411" font-size="12">loaded_table에 cached_table memcpy</text>
  <text x="440" y="411" font-size="12">find_loaded_rsc_table</text>
  <rect x="20" y="430" width="380" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="451" font-size="12">prepare_subdevices → ops->start</text>
  <text x="440" y="451" font-size="12">start (리셋 해제)</text>
  <rect x="20" y="470" width="380" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="491" font-size="12">start_subdevices (virtio 등록) → RUNNING</text>
  <text x="440" y="491" font-size="12">virtio probe 중 kick</text>
  <line x1="402" y1="127" x2="434" y2="127" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="402" y1="167" x2="434" y2="167" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="402" y1="207" x2="434" y2="207" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="402" y1="247" x2="434" y2="247" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="402" y1="287" x2="434" y2="287" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="402" y1="327" x2="434" y2="327" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="402" y1="367" x2="434" y2="367" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="402" y1="407" x2="434" y2="407" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="402" y1="447" x2="434" y2="447" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="402" y1="487" x2="434" y2="487" stroke="currentColor" marker-end="url(#ah4)"/>
</svg>
```

그림 4 — `rproc_boot()`의 실제 호출 순서와 각 단계에서 불리는 드라이버 콜백. 초록 = 준비, 파랑 = resource table 처리, 주황 = 로드와 시작. 하나라도 실패하면 `clean_up_resources:` 라벨로 가서 `rproc_resource_cleanup()`이 지금까지 잡은 것을 모두 푼다.

### 3.2 핵심 코드 두 조각

`rproc_fw_boot()`의 몸통(6.18.55, 에러 처리 일부 생략):

```c
	ret = rproc_fw_sanity_check(rproc, fw);
	dev_info(dev, "Booting fw image %s, size %zd\n", name, fw->size);
	ret = rproc_enable_iommu(rproc);
	ret = rproc_prepare_device(rproc);
	rproc->bootaddr = rproc_get_boot_addr(rproc, fw);
	/* Load resource table, core dump segment list etc from the firmware */
	ret = rproc_parse_fw(rproc, fw);
	rproc->max_notifyid = -1;
	rproc->nb_vdev = 0;
	/* handle fw resources which are required to boot rproc */
	ret = rproc_handle_resources(rproc, rproc_loading_handlers);
	/* Allocate carveout resources associated to rproc */
	ret = rproc_alloc_registered_carveouts(rproc);
	ret = rproc_start(rproc, fw);
```

`rproc_start()`의 앞부분 — "사본에서 작업하다가 로드 후 원격 메모리의 표로 갈아탄다"는 부분이다:

```c
	/* load the ELF segments to memory */
	ret = rproc_load_segments(rproc, fw);
	...
	loaded_table = rproc_find_loaded_rsc_table(rproc, fw);
	if (loaded_table) {
		memcpy(loaded_table, rproc->cached_table, rproc->table_sz);
		rproc->table_ptr = loaded_table;
	}
	ret = rproc_prepare_subdevices(rproc);
	/* power up the remote processor */
	ret = rproc->ops->start(rproc);
	/* Start any subdevices for the remote processor */
	ret = rproc_start_subdevices(rproc);
	rproc->state = RPROC_RUNNING;
	dev_info(dev, "remote processor %s is now up\n", rproc->name);
```

여기서 두 가지를 기억하자.

- `rproc_da_to_va()`는 **드라이버의 `da_to_va`를 먼저** 부르고, NULL이면 carveout 목록을 뒤진다. 그래서 vring처럼 core가 `dma_alloc_coherent()`로 잡은 영역은 드라이버가 몰라도 번역된다.
- vring 주소는 `rproc_handle_resources` 단계가 아니라 **virtio 드라이버가 `find_vqs()`를 부를 때** (`rp_find_vq()` 안에서) 표에 써진다. 그래서 virtio_rpmsg_bus가 안 올라와 있으면 vring da가 ANY로 남는다 — §4 예제 8에서 실제로 보이고, 예제 9에서 모듈을 올리면 채워진다.

### 3.3 함정 — `load`를 안 채우면 `parse_fw`가 사라진다

§1.3의 `rproc_alloc_ops()` 코드를 다시 보자. `.load`가 NULL이면 다섯 개 ELF 기본값을 **무조건** 대입하므로, 드라이버가 정성껏 쓴 `.parse_fw`가 `rproc_elf_load_rsc_table`로 바뀐다. 이 랩의 첫 버전이 정확히 그랬고, 증상은 "coredump를 enabled로 했는데 crash 후 `/sys/class/devcoredump/`에 아무것도 없음"이었다(dump segment를 `parse_fw`에서 등록했기 때문). 벤더 드라이버들이 왜 ELF 함수를 일일이 나열하는지 소스로 확인할 수 있다.

```sh
grep -n "rproc_elf\|parse_fw" /var/lab/src/linux-6.18.55/drivers/remoteproc/stm32_rproc.c | head
```

```text
272:static int stm32_rproc_parse_fw(struct rproc *rproc, const struct firmware *fw)
274:	if (rproc_elf_load_rsc_table(rproc, fw))
670:	.load		= rproc_elf_load_segments,
671:	.parse_fw	= stm32_rproc_parse_fw,
672:	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
674:	.sanity_check	= rproc_elf_sanity_check,
675:	.get_boot_addr	= rproc_elf_get_boot_addr,
```

출력에서 볼 것: STM32는 `parse_fw`에서 표가 없어도 경고만 하고 0을 돌려준다(줄 274 다음). 표 없는 펌웨어(rpmsg 안 쓰는 단순 펌웨어)도 부팅되게 하려는 것이다. 기본값 `rproc_elf_load_rsc_table`은 표가 없으면 `-EINVAL`이라 부팅이 실패한다.

---

## 4. 랩 1 — 가짜 remoteproc 드라이버로 진짜 ELF 올리기

### 4.1 설계

| 실물 하드웨어 | `o2_fakerproc.ko`에서 |
|---|---|
| M4의 TCM `0x0` 64 KB, SRAM `0x2000_0000` 64 KB | `devm_kzalloc()` 버퍼 두 개 (`p->itcm`, `p->sram`) |
| `da_to_va`: TCM/SRAM의 Linux 쪽 `ioremap` 주소 | 위 버퍼 안의 오프셋 |
| `start`: 리셋 컨트롤러 해제 | 로그 + "가짜 코어"가 trace에 한 줄 쓰기 |
| `stop`: 리셋 | 로그 + 가짜 코어 정지 플래그 |
| `kick`: mailbox `mbox_send_message()` | `schedule_work()` (= 원격의 IRQ 핸들러를 깨움) |
| DT 노드 + platform driver | 모듈이 직접 `platform_device_register_simple()` |
| 원격 코어 crash IRQ (watchdog) | 원격이 `"crash"` 메시지를 받으면 `rproc_report_crash()` |

### 4.2 공용 헤더

```c
/* o2.h — shared state of the fake remoteproc (Linux side) and the fake remote core */
#ifndef O2_H
#define O2_H
#include <linux/remoteproc.h>
#include <linux/virtio_ring.h>
#include <linux/workqueue.h>

#define O2_ITCM_DA  0x00000000u   /* "remote" code memory, as the M4 sees it */
#define O2_SRAM_DA  0x20000000u   /* "remote" data memory */
#define O2_MEM_LEN  0x10000u

struct o2_priv {
	struct rproc *rproc;
	void *itcm, *sram;            /* kernel buffers that play the M4's TCM/SRAM */
	struct work_struct work;      /* the fake remote core runs here */
	bool running;
	/* remote-side virtio state (what OpenAMP keeps on the M4) */
	struct vring vr[2];
	u16 last_avail[2];
	u32 nid[2];                   /* notifyid of vring0/1 */
	bool vr_ready, ns_sent;
	unsigned int kicks, rx_msgs, tx_msgs;
	size_t tlen;                  /* bytes already in the trace buffer */
};

void o2_remote_reset(struct o2_priv *p);
void o2_remote_work(struct work_struct *w);
void o2_tprintf(struct o2_priv *p, const char *fmt, ...) __printf(2, 3);
#endif
```

### 4.3 드라이버 (1/3) — `da_to_va`, `parse_fw`, `start`, `stop`

무엇을 확인하는 코드인가: 벤더 드라이버가 실제로 채워야 하는 최소한. `da_to_va`가 범위 밖이면 NULL을 돌려줘야 core가 carveout 목록으로 넘어간다.

```c
// SPDX-License-Identifier: GPL-2.0
/* o2_main.c — a FAKE remoteproc driver: rproc_ops with no real core behind it */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/elf.h>
#include <linux/firmware.h>
#include "o2.h"
#include "remoteproc_internal.h"   /* rproc_vq_interrupt, rproc_elf_* */

static bool attach;
module_param(attach, bool, 0444);
MODULE_PARM_DESC(attach, "pretend the bootloader already started the core");

static void *o2_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct o2_priv *p = rproc->priv;

	if (da >= O2_ITCM_DA && da + len <= O2_ITCM_DA + O2_MEM_LEN)
		return p->itcm + (da - O2_ITCM_DA);
	if (da >= O2_SRAM_DA && da + len <= O2_SRAM_DA + O2_MEM_LEN)
		return p->sram + (da - O2_SRAM_DA);
	return NULL;   /* core then searches the carveout list */
}

static int o2_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	int ret = rproc_elf_load_rsc_table(rproc, fw);   /* default ELF behaviour */

	if (ret)
		return ret;
	/* tell the coredump code which "remote" memory to save on a crash */
	rproc_coredump_add_segment(rproc, O2_ITCM_DA, 0x400);
	rproc_coredump_add_segment(rproc, O2_SRAM_DA, 0x800);
	return 0;
}

static int o2_start(struct rproc *rproc)
{
	struct o2_priv *p = rproc->priv;

	dev_info(&rproc->dev, "o2: start: release core from reset, bootaddr 0x%llx\n",
		 rproc->bootaddr);
	o2_remote_reset(p);
	p->running = true;
	o2_tprintf(p, "[fake-M4] out of reset, PC=0x%llx\n", rproc->bootaddr);
	return 0;
}

static int o2_stop(struct rproc *rproc)
{
	struct o2_priv *p = rproc->priv;

	p->running = false;       /* the work checks this under rproc->lock */
	dev_info(&rproc->dev, "o2: stop: core in reset (kicks=%u rx=%u tx=%u)\n",
		 p->kicks, p->rx_msgs, p->tx_msgs);
	return 0;
}
```

`remoteproc_internal.h`를 include하는 이유: `rproc_vq_interrupt()`와 `rproc_elf_*()`는 EXPORT는 돼 있지만 선언이 `drivers/remoteproc/remoteproc_internal.h`에만 있다. 트리 안의 벤더 드라이버는 같은 디렉터리라 그냥 include하고, 트리 밖 모듈은 Kbuild에서 include 경로를 추가한다(§4.6). 그리고 이 헤더는 `struct rproc` 정의가 먼저 보여야 하므로 `o2.h`(→ `remoteproc.h`) 다음에 와야 한다 — 순서를 바꾸면 `invalid use of undefined type 'struct rproc'`로 빌드가 깨진다(실제로 겪음).

### 4.4 드라이버 (2/3) — `kick`, attach 모드, ops 표

```c
static size_t o2_rsc_sz;
static struct platform_device *o2_pdev;

static void o2_kick(struct rproc *rproc, int vqid)   /* = "ring the mailbox doorbell" */
{
	struct o2_priv *p = rproc->priv;

	if (++p->kicks <= 3)
		dev_info(&rproc->dev, "o2: kick vqid %d (doorbell #%u)\n", vqid, p->kicks);
	schedule_work(&p->work);   /* the remote's IRQ handler */
}

static int o2_attach(struct rproc *rproc)
{
	struct o2_priv *p = rproc->priv;

	o2_remote_reset(p);
	p->running = true;
	o2_tprintf(p, "[fake-M4] Linux attached to me (I was started by the bootloader)\n");
	return 0;
}

static int o2_detach(struct rproc *rproc)
{
	((struct o2_priv *)rproc->priv)->running = false;
	return 0;
}

static struct resource_table *o2_get_loaded_rsc_table(struct rproc *rproc, size_t *sz)
{
	*sz = o2_rsc_sz;              /* table lives at the start of "SRAM" */
	return ((struct o2_priv *)rproc->priv)->sram;
}

static const struct rproc_ops o2_ops = {
	.start = o2_start, .stop = o2_stop, .kick = o2_kick,
	.da_to_va = o2_da_to_va, .parse_fw = o2_parse_fw,
	/* .load set => core does NOT fill the ELF defaults (and would overwrite parse_fw) */
	.load = rproc_elf_load_segments,
	.sanity_check = rproc_elf_sanity_check,
	.get_boot_addr = rproc_elf_get_boot_addr,
	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
};
static const struct rproc_ops o2_attach_ops = {
	.start = o2_start, .stop = o2_stop, .kick = o2_kick, .da_to_va = o2_da_to_va,
	.attach = o2_attach, .detach = o2_detach,
	.get_loaded_rsc_table = o2_get_loaded_rsc_table,
};
```

실물에서 `kick`은 대개 한 줄이다. TI K3는 mailbox 메시지 payload에 vqid를 그대로 넣고(`k3_rproc_kick()` → `mbox_send_message(kproc->mbox, (void *)(uintptr_t)msg)`), STM32는 vring별로 IPCC 채널을 따로 쓴다(DT `mbox-names = "vq0", "vq1", ...`). 둘 다 §10에서 소스로 본다.

### 4.5 드라이버 (3/3) — attach용 선로드, probe/remove, 모듈 init

```c
/* attach mode: play the bootloader (U-Boot "rproc load/start") — copy the ELF in first */
static int o2_preload(struct rproc *rproc)
{
	const struct firmware *fw;
	int ret = request_firmware(&fw, rproc->firmware, rproc->dev.parent);

	if (ret)
		return ret;
	ret = rproc_elf_load_segments(rproc, fw);          /* PT_LOAD -> da_to_va() */
	if (!ret)
		ret = rproc_elf_load_rsc_table(rproc, fw);  /* only to learn the size */
	if (!ret) {
		o2_rsc_sz = rproc->table_sz;
		kfree(rproc->cached_table);
		rproc->cached_table = NULL;
		rproc->table_ptr = NULL;
	}
	release_firmware(fw);
	return ret;
}

static int o2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rproc *rproc;
	struct o2_priv *p;
	int ret;

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32)); /* carveouts, vrings */
	if (ret)
		return ret;
	rproc = devm_rproc_alloc(dev, "o2-m4", attach ? &o2_attach_ops : &o2_ops,
				 "o2_m4_fw.elf", sizeof(*p));
	if (!rproc)
		return -ENOMEM;
	p = rproc->priv;
	p->rproc = rproc;
	p->itcm = devm_kzalloc(dev, O2_MEM_LEN, GFP_KERNEL);
	p->sram = devm_kzalloc(dev, O2_MEM_LEN, GFP_KERNEL);
	if (!p->itcm || !p->sram)
		return -ENOMEM;
	INIT_WORK(&p->work, o2_remote_work);
	rproc_coredump_set_elf_info(rproc, ELFCLASS32, EM_ARM);
	rproc->auto_boot = attach;          /* normal mode: wait for "echo start" */
	if (attach) {
		ret = o2_preload(rproc);
		if (ret)
			return dev_err_probe(dev, ret, "preload failed\n");
		rproc->state = RPROC_DETACHED;  /* core already running */
	}
	platform_set_drvdata(pdev, rproc);
	ret = rproc_add(rproc);
	if (ret)
		return ret;
	dev_info(dev, "o2: registered %s (attach=%d)\n", dev_name(&rproc->dev), attach);
	return 0;
}
```

```c
static void o2_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);

	rproc_del(rproc);                   /* shuts the core down if running */
	cancel_work_sync(&((struct o2_priv *)rproc->priv)->work);
}

static struct platform_driver o2_driver = {
	.probe = o2_probe, .remove = o2_remove,
	.driver = { .name = "o2_fakerproc" },
};

static int __init o2_init(void)
{
	int ret = platform_driver_register(&o2_driver);

	if (ret)
		return ret;
	o2_pdev = platform_device_register_simple("o2_fakerproc", -1, NULL, 0);
	if (IS_ERR(o2_pdev)) {
		platform_driver_unregister(&o2_driver);
		return PTR_ERR(o2_pdev);
	}
	return 0;
}

static void __exit o2_exit(void)
{
	platform_device_unregister(o2_pdev);
	platform_driver_unregister(&o2_driver);
}
module_init(o2_init);
module_exit(o2_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("O2 fake remoteproc + in-kernel fake remote core");
```

설계 메모 몇 가지:

- `dma_coerce_mask_and_coherent()` — carveout과 vring은 core가 `dma_alloc_coherent(dev->parent, ...)`로 잡는다(`rproc_alloc_carveout`). 우리 platform device에 DMA mask가 없으면 여기서 실패한다. resource table의 주소 칸이 32비트라 32비트 mask를 쓴다(64비트 주소가 나오면 core가 `DMA address cast in 32bit` 경고를 낸다).
- `rproc_coredump_set_elf_info(ELFCLASS32, EM_ARM)` — 안 하면 `rproc->elf_class`가 `ELFCLASSNONE`이라 coredump가 `ELF class is not set` 에러로 끝난다.
- `o2_preload()`는 "부트로더가 이미 펌웨어를 올리고 코어를 띄웠다"는 상황을 흉내 낸다. `request_firmware()`의 device 인자로 `rproc->dev.parent`를 쓴 이유: probe는 `platform_device_register_simple()` **안에서** 동기로 불리므로 그 시점에는 전역 `o2_pdev`가 아직 NULL이다(처음에 이 실수를 했다).
- remove에서 `devm_rproc_add()` 대신 `rproc_del()` → `cancel_work_sync()` 순서를 직접 쓴 것은, 코어가 내려간 **다음에** 가짜 원격 work를 정리하기 위해서다. devm은 remove가 끝난 뒤에 풀리므로 순서를 통제할 수 없다.

### 4.6 예제 7 — 빌드

```make
# out-of-tree remoteproc driver: vendor drivers live in drivers/remoteproc/ and include
# "remoteproc_internal.h" (rproc_vq_interrupt, rproc_elf_*), so we add that directory
ccflags-y += -I$(srctree)/drivers/remoteproc
obj-m += o2_fakerproc.o
o2_fakerproc-y := o2_main.o o2_remote.o
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/O2/mod && make -C /var/lab/out/linux-build-dom M=$PWD W=1 modules 2>&1 | grep -v "^make"
modinfo o2_fakerproc.ko | grep -E "vermagic|parm|depends"'
```

```text
  CC [M]  o2_main.o
  CC [M]  o2_remote.o
  LD [M]  o2_fakerproc.o
  MODPOST Module.symvers
  LD [M]  o2_fakerproc.ko
depends:        
vermagic:       6.18.55-dom SMP preempt mod_unload aarch64
parm:           attach:pretend the bootloader already started the core (bool)
```

출력에서 볼 것: `W=1`(추가 경고)로도 경고 0개. `depends:`가 비어 있는 것은 이 커널에서 `CONFIG_REMOTEPROC=y`(built-in)이라서다. 반면 `virtio_rpmsg_bus`, `rpmsg_char`, `rpmsg_ctrl`, `rpmsg_ns`는 `=m`이다(아래 config 확인).

```sh
grep -E "CONFIG_(REMOTEPROC|REMOTEPROC_CDEV|RPMSG|RPMSG_CHAR|RPMSG_CTRL|RPMSG_NS|RPMSG_VIRTIO|SAMPLE_RPMSG_CLIENT|DEV_COREDUMP|MAILBOX)=" \
    /var/lab/out/linux-build-dom/.config
```

```text
CONFIG_DEV_COREDUMP=y
CONFIG_MAILBOX=y
CONFIG_REMOTEPROC=y
CONFIG_REMOTEPROC_CDEV=y
CONFIG_RPMSG=y
CONFIG_RPMSG_CHAR=m
CONFIG_RPMSG_CTRL=m
CONFIG_RPMSG_NS=m
CONFIG_RPMSG_VIRTIO=m
CONFIG_SAMPLE_RPMSG_CLIENT=m
```

### 4.7 게스트 준비 — BusyBox initramfs에 모듈 · 펌웨어 넣기

kernel-only 실험이라 systemd rootfs 대신 랩의 BusyBox initramfs를 복사해서 쓴다. `rpmsg` 관련 `.ko`와 `modules.dep`만 넣어도 BusyBox `modprobe`가 의존성을 푼다. 펌웨어는 `request_firmware()`의 기본 검색 경로 `/lib/firmware/`에 둔다(G10).

```sh
#!/bin/bash
# mkinitrd.sh — BusyBox lab initramfs + 6.18.55-dom rpmsg modules + our firmware/module/tests
set -e
W=/var/tmp/O2; M=/var/lab/out/modules-dom/lib/modules/6.18.55-dom
rm -rf $W/rootfs && cp -a /var/lab/build/rootfs $W/rootfs
R=$W/rootfs/lib/modules/6.18.55-dom
mkdir -p $R/kernel/drivers $R/kernel/samples $W/rootfs/lib/firmware
cp $M/modules.* $R/
cp -a $M/kernel/drivers/rpmsg $R/kernel/drivers/
cp -a $M/kernel/samples/rpmsg $R/kernel/samples/
cp $W/o2_m4_fw.elf $W/rootfs/lib/firmware/
cp $W/mod/o2_fakerproc.ko $W/bin/* /Users/donh/.elinux_scratch/O2/guest/t*.sh $W/rootfs/
chmod +x $W/rootfs/t*.sh
(cd $W/rootfs && find . | cpio -o -H newc --quiet | gzip -1) > $W/o2_initrd.gz
ls -la $W/o2_initrd.gz
```

```sh
#!/bin/bash
# run.sh <test-script> [timeout] — boot Image-dom + our initramfs, run the script, power off
timeout ${2:-240} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1024M -nographic \
  -kernel /var/lab/out/Image-dom -initrd /var/tmp/O2/o2_initrd.gz \
  -append "console=ttyAMA0 quiet loglevel=4 LAB_CMD=/$1"
```

```text
-rw-rw-r-- 1 donh donh 2787399 Oct 18 09:18 /var/tmp/O2/o2_initrd.gz
```

### 4.8 예제 8 — offline → running → offline, sysfs · debugfs 전부 보기

무엇을 확인하나: 진짜 remoteproc core가 진짜 ELF를 `/lib/firmware`에서 읽어 resource table을 처리하고 코어를 "켰다 끄는" 전 과정. `dynamic_debug`로 core의 `dev_dbg`를 켜서 resource 처리 로그까지 본다(M-모듈).

```sh
#!/bin/sh
# t1.sh — lifecycle: offline -> running -> offline, sysfs + debugfs views
echo "file remoteproc_core.c +p; file remoteproc_virtio.c +p" > /sys/kernel/debug/dynamic_debug/control
insmod /o2_fakerproc.ko
R=/sys/class/remoteproc/remoteproc0; D=/sys/kernel/debug/remoteproc/remoteproc0
echo "== sysfs:"; ls $R | tr "\n" " "; echo
echo "name=$(cat $R/name) firmware=$(cat $R/firmware) state=$(cat $R/state)"
echo "== debugfs:"; ls $D | tr "\n" " "; echo
echo "== recovery=$(cat $D/recovery) coredump=$(cat $D/coredump)"
echo start > $R/state
echo "== state after start: $(cat $R/state)"
echo "== resource_table (after Linux filled it in):"; cat $D/resource_table
echo "== carveout_memories:"; cat $D/carveout_memories
echo "== trace0:"; cat $D/trace0
echo stop > $R/state
echo "== state after stop: $(cat $R/state)"
echo "== dmesg:"; dmesg | grep -E "remoteproc|o2|rproc" | sed 's/^\[ *[0-9.]*\] //'
```

```sh
limactl shell elinux -- bash -lc 'bash ~/.elinux_scratch/O2/guest/mkinitrd.sh && bash ~/.elinux_scratch/O2/guest/run.sh t1.sh'
```

(VM 안의 `~`는 VM 사용자 홈이라 실제로는 `/Users/donh/.elinux_scratch/...` 절대 경로로 실행했다.) 출력(가상 주소 `%p`는 해시된 값이라 실행마다 다르다):

```text
lab initramfs: 6.18.55-dom up 9.50s
== sysfs:
coredump dev device firmware name power recovery state subsystem uevent 
name=o2-m4 firmware=o2_m4_fw.elf state=offline
== debugfs:
carveout_memories coredump crash name recovery resource_table 
== recovery=enabled coredump=disabled
== state after start: running
== resource_table (after Linux filled it in):
Entry 0 is of type vdev
  ID 7
  Notify ID 0
  Device features 0x1
  Guest features 0x0
  Config length 0x0
  Status 0x1
  Number of vrings 2
  Reserved (should be zero) [0][0]

  Vring 0
    Device Address 0xffffffff
    Alignment 4096
    Number of buffers 16
    Notify ID 0
    Physical Address 0x0

  Vring 1
    Device Address 0xffffffff
    Alignment 4096
    Number of buffers 16
    Notify ID 1
    Physical Address 0x0

Entry 1 is of type trace
  Device Address 0x200000d0
  Length 0x400 Bytes
  Reserved (should be zero) [0]
  Name o2_trace

Entry 2 is of type carveout
  Device Address 0x7cc70000
  Physical Address 0x7cc70000
  Length 0x10000 Bytes
  Flags 0x0
  Reserved (should be zero) [0]
  Name o2_shm

== carveout_memories:
Carveout memory entry:
	Name: vdev0vring0
	Virtual address: 000000004bf5a692
	DMA address: 0x000000007cc60000
	Device address: 0x7cc60000
	Length: 0x2000 Bytes

Carveout memory entry:
	Name: vdev0vring1
	Virtual address: 00000000c84051ec
	DMA address: 0x000000007cc62000
	Device address: 0x7cc62000
	Length: 0x2000 Bytes

Carveout memory entry:
	Name: o2_shm
	Virtual address: 00000000b570de58
	DMA address: 0x000000007cc70000
	Device address: 0x7cc70000
	Length: 0x10000 Bytes

== trace0:
[o2-fw] boot banner: image v1.0 (written by ELF loader)
[fake-M4] out of reset, PC=0x45
== state after stop: offline
== dmesg:
[   11.330577][   T77] o2_fakerproc: loading out-of-tree module taints kernel.
[   11.330771][   T77] o2_fakerproc: module verification failed: signature and/or required key missing - tainting kernel
[   11.369469][   T77] remoteproc remoteproc0: o2-m4 is available
[   11.371150][   T77] o2_fakerproc o2_fakerproc: o2: registered remoteproc0 (attach=0)
[   11.773563][   T76] remoteproc remoteproc0: powering up o2-m4
[   11.791915][   T76] remoteproc remoteproc0: Booting fw image o2_m4_fw.elf, size 133684
[   11.792085][   T76] o2_fakerproc o2_fakerproc: iommu not present
[   11.792523][   T76] remoteproc remoteproc0: rsc: type 3
[   11.792625][   T76] remoteproc remoteproc0: vdev rsc: id 7, dfeatures 0x1, cfg len 0, 2 vrings
[   11.819816][   T76] remoteproc remoteproc0: vdev rsc: vring0: da 0xffffffff, qsz 16, align 4096
[   11.819982][   T76] remoteproc remoteproc0: vdev rsc: vring1: da 0xffffffff, qsz 16, align 4096
[   11.824312][   T76] remoteproc remoteproc0: rsc: type 2
[   11.824572][   T76] remoteproc remoteproc0: trace0 added: da 0x200000d0, len 0x400
[   11.824623][   T76] remoteproc remoteproc0: rsc: type 0
[   11.824674][   T76] remoteproc remoteproc0: carveout rsc: name: o2_shm, da 0xffffffff, pa 0xffffffff, len 0x10000, flags 0x0
[   11.856828][   T76] remoteproc remoteproc0: carveout va 000000004bf5a692, dma 0x000000007cc60000, len 0x2000
[   11.867550][   T76] remoteproc remoteproc0: carveout va 00000000c84051ec, dma 0x000000007cc62000, len 0x2000
[   11.874741][   T76] remoteproc remoteproc0: carveout va 00000000b570de58, dma 0x000000007cc70000, len 0x10000
[   11.875319][   T76] remoteproc remoteproc0: o2: start: release core from reset, bootaddr 0x45
[   11.876437][   T76] rproc-virtio rproc-virtio.1.auto: registered virtio1 (type 7)
[   11.876505][   T76] remoteproc remoteproc0: remote processor o2-m4 is now up
[   12.093774][   T76] remoteproc remoteproc0: o2: stop: core in reset (kicks=0 rx=0 tx=0)
[   12.094900][   T76] remoteproc remoteproc0: stopped remote processor o2-m4
[   12.913641][   T95] reboot: Power down
```

출력에서 볼 것 — 한 줄씩 그림 4와 맞춰 보자.

- `Booting fw image o2_m4_fw.elf, size 133684` — `request_firmware()`가 initramfs의 `/lib/firmware`에서 읽었다. 크기 133684 B는 ELF 파일 크기다. 실제 내용은 2 KB 남짓이지만 세그먼트 정렬(Align 0x10000) 때문에 파일 오프셋이 0x20000까지 벌어져 커졌다.
- `rsc: type 3` → `type 2` → `type 0` — `offset[]` 순서대로(vdev, trace, carveout) 핸들러가 불렸다. 순서는 펌웨어가 정한다.
- carveout 할당 로그 세 줄 — vring 두 개(각 `0x2000` = `PAGE_ALIGN(vring_size(16, 4096))`)와 `o2_shm` 64 KB. 이름 `vdev0vring0`은 core가 `"vdev%dvring%d"`로 만든 것이다. DT에 같은 이름의 reserved-memory가 있으면 그걸 쓴다(실물 STM32 DT는 정확히 이 이름을 쓴다, §10).
- **resource_table의 carveout 칸**: ELF에서 `ANY`였던 `Device Address`가 `0x7cc70000`(실제 DMA 주소)으로 바뀌었다. Linux가 계약서에 답을 써 넣은 것이다.
- **vring 칸**: `Notify ID`는 0, 1로 채워졌지만 `Device Address`는 아직 `0xffffffff`, `Status 0x1`(ACKNOWLEDGE만)이다. virtio_rpmsg_bus 모듈을 아직 안 올려서 **virtio 드라이버가 `find_vqs()`를 부르지 않았기 때문**이다(§3.2). 다음 예제에서 모듈을 올리면 바뀐다.
- `trace0`의 첫 줄은 **ELF loader가 복사한 초기값**, 둘째 줄은 `start`에서 가짜 코어가 덧붙인 런타임 로그다. trace0 파일은 `start` 후에만 생긴다(debugfs 목록에 처음엔 없다).
- `iommu not present` — 이 platform device에는 IOMMU가 없어서 `rproc->domain`이 NULL이다. devmem 엔트리가 있었다면 여기서 부팅이 실패했을 것이다(§2.2).

---

## 5. virtio vring과 rpmsg — 메시지가 실제로 지나가는 길

### 5.1 vring 한 개의 해부

virtio는 원래 "게스트 OS의 드라이버 ↔ 하이퍼바이저의 가상 장치"용 큐다. rpmsg는 이걸 그대로 가져와서 **Linux = driver 쪽, 원격 코어 = device 쪽**으로 정했다. vring 하나는 세 부분으로 된 공유 메모리 구조다(`include/uapi/linux/virtio_ring.h`의 `struct vring`, legacy split ring).

- **descriptor table**: `num`개의 `{addr, len, flags, next}`. 버퍼가 어디 있는지(물리 주소)와 길이.
- **avail ring**: driver(Linux)가 "이 desc 번호들을 당신에게 넘긴다"고 쓰는 링. `idx`는 driver만 증가시킨다.
- **used ring**: device(원격)가 "이 desc를 다 썼다(길이 len)"고 돌려주는 링. `idx`는 device만 증가시킨다.

각 인덱스를 한쪽만 쓰므로 **락이 필요 없다**(H10 그림 7의 "단일 생산자 · 단일 소비자 링"). 순서는 메모리 배리어로 맞춘다: 버퍼 내용 → 배리어 → `idx++`.

rpmsg는 vring을 두 개 쓴다. 소스 `virtio_rpmsg_bus.c`에서 `vrp->rvq = vqs[0]; vrp->svq = vqs[1];`이다.

| vring | Linux 입장 | avail에 넣는 것 | used로 돌아오는 것 |
|---|---|---|---|
| vring0 (`rvq`, "input") | 수신 | **빈** 512 B 버퍼 (원격이 채우라고) | 원격이 채운 메시지 |
| vring1 (`svq`, "output") | 송신 | **채운** 메시지 버퍼 | 다 읽은 빈 버퍼 (재사용) |

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="20" font-size="13">vring0 (Linux 수신) — num=16, 0x2000 B</text>
  <rect x="20" y="34" width="200" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="52" text-anchor="middle" font-size="12">desc[i]: addr len flags next</text>
  <rect x="20" y="60" width="200" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="78" text-anchor="middle" font-size="12">0: 0x4a3c0000 512 WRITE</text>
  <rect x="20" y="86" width="200" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="104" text-anchor="middle" font-size="12">1: 0x4a3c0200 512 WRITE</text>
  <rect x="20" y="112" width="200" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="130" text-anchor="middle" font-size="12">... (16개)</text>
  <rect x="250" y="34" width="180" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="52" text-anchor="middle" font-size="12">avail: flags · idx</text>
  <rect x="250" y="60" width="180" height="52" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="80" text-anchor="middle" font-size="12">ring[] = 0,1,2,...,15</text>
  <text x="340" y="100" text-anchor="middle" font-size="12">idx = 16 (Linux가 증가)</text>
  <rect x="460" y="34" width="200" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="560" y="52" text-anchor="middle" font-size="12">used: flags · idx</text>
  <rect x="460" y="60" width="200" height="52" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="560" y="80" text-anchor="middle" font-size="12">ring[] = {id=0, len=56}</text>
  <text x="560" y="100" text-anchor="middle" font-size="12">idx = 1 (원격이 증가)</text>
  <rect x="20" y="170" width="300" height="110" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="34" y="192" font-size="12">Linux (driver) 쪽 — virtio_ring.c</text>
  <text x="34" y="214" font-size="12">1. 빈 버퍼를 desc에 걸고 avail.ring[]에</text>
  <text x="34" y="234" font-size="12">2. 배리어 후 avail.idx++</text>
  <text x="34" y="254" font-size="12">3. kick → rproc_ops.kick</text>
  <text x="34" y="272" font-size="12">6. used를 보고 버퍼 회수 · 재등록</text>
  <rect x="360" y="170" width="300" height="110" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="374" y="192" font-size="12">원격 (device) 쪽 — OpenAMP virtqueue</text>
  <text x="374" y="214" font-size="12">4. last_avail != avail.idx 이면</text>
  <text x="374" y="234" font-size="12">   desc 번호를 꺼내 버퍼에 메시지 기록</text>
  <text x="374" y="254" font-size="12">5. used.ring[]에 {id,len}, 배리어, idx++</text>
  <text x="374" y="272" font-size="12">   그리고 mailbox IRQ로 Linux를 깨움</text>
  <line x1="340" y1="114" x2="340" y2="166" stroke="#4a7bd0" marker-end="url(#ah5)"/>
  <line x1="560" y1="166" x2="560" y2="114" stroke="#e08a3c" marker-end="url(#ah5)"/>
  <text x="20" y="310" font-size="12">rpmsg 메시지 = 버퍼 앞 16 B 헤더 {src, dst, reserved, len, flags} + data (최대 496 B)</text>
  <text x="20" y="330" font-size="12">버퍼는 고정 512 B (MAX_RPMSG_BUF_SIZE), 개수 = vring 크기 x 2 (최대 512): 절반 RX, 절반 TX</text>
  <text x="20" y="350" font-size="12">desc 주소는 예시 값. ACCESS_PLATFORM 협상이 없으면 물리 주소(sg_phys)가 그대로 들어간다</text>
</svg>
```

그림 5 — 수신용 vring0의 상태 예. Linux가 빈 버퍼 16개를 avail에 걸어 두고, 원격이 하나를 채워 used로 돌려준 직후(used.idx = 1). 메시지 길이 56 = 헤더 16 + NS 메시지 40(이름 32 + addr 4 + flags 4).

버퍼 수 공식은 소스 그대로다.

```c
	/* we need less buffers if vrings are small */
	if (virtqueue_get_vring_size(vrp->rvq) < MAX_RPMSG_NUM_BUFS / 2)
		vrp->num_bufs = virtqueue_get_vring_size(vrp->rvq) * 2;
	else
		vrp->num_bufs = MAX_RPMSG_NUM_BUFS;

	vrp->buf_size = MAX_RPMSG_BUF_SIZE;
```

이 랩은 vring 16칸이라 버퍼 32개(RX 16 + TX 16) x 512 B = 16 KB다. `MAX_RPMSG_NUM_BUFS`와 `MAX_RPMSG_BUF_SIZE`는 둘 다 512다. 버퍼가 고정 512 B라서 **한 메시지 payload는 496 B를 넘을 수 없다** — 넘으면 `rpmsg_send_offchannel_raw()`가 `message is too big`과 `-EMSGSIZE`를 낸다. 카메라 프레임 같은 큰 데이터는 rpmsg로 보내지 말고, carveout(공유 메모리)에 두고 **주소와 길이만** rpmsg로 보내는 것이 정석이다.

### 5.2 주소 · endpoint · 채널 · name service

rpmsg의 주소 체계는 UDP 포트와 비슷하다.

- **endpoint** = (로컬 주소, 콜백). Linux 쪽 주소는 `idr`로 1024(`RPMSG_RESERVED_ADDRESSES`)부터 배정된다. 0~1023은 예약.
- **채널(rpmsg device)** = 이름 + (src, dst). `rpmsg_driver`의 `id_table` 이름과 매칭되면 그 드라이버의 `probe`가 불린다 — 버스 매칭(G2) 그대로다.
- **name service(NS)** = 주소 53(`RPMSG_NS_ADDR`)의 특수 endpoint. 원격이 `struct rpmsg_ns_msg { char name[32]; u32 addr; u32 flags; }`를 53번으로 보내면, Linux의 `rpmsg_ns` 드라이버가 그 이름으로 채널을 만든다(`flags = RPMSG_NS_CREATE` 0, `RPMSG_NS_DESTROY` 1). 원격이 "나 이런 서비스 있다"고 광고하는 방식이다. vdev의 `dfeatures` bit 0(`VIRTIO_RPMSG_F_NS`)을 켜야 Linux가 NS endpoint를 만든다.

| 원격이 광고한 이름 | Linux에서 붙는 드라이버 | 결과 |
|---|---|---|
| `rpmsg-client-sample` | `samples/rpmsg/rpmsg_client_sample.c` | "hello world!" 핑퐁 100회 |
| `rpmsg-raw` | `rpmsg_char` (`rpmsg_chrdev_id_table`) | `/dev/rpmsgN` 자동 생성 |
| `rpmsg-tty` | `rpmsg_tty` (이 랩 커널에서는 `CONFIG_RPMSG_TTY` 꺼짐) | `/dev/ttyRPMSGN` |
| (NS 없이) | `rpmsg_ctrl` | `/dev/rpmsg_ctrlN` — 유저가 ioctl로 endpoint를 직접 만든다 |

### 5.3 가짜 원격 코어 (1/3) — 표 순회와 trace 출력

여기부터가 "M4 위에서 OpenAMP가 하는 일"을 Linux 커널 work로 흉내 낸 코드다. 실물 원격 펌웨어와 같은 순서로 동작하도록 짰다: **자기 resource table을 읽어** trace 버퍼와 vring 주소를 알아낸다.

```c
// SPDX-License-Identifier: GPL-2.0
/* o2_remote.c — the FAKE remote core: what OpenAMP/RPMsg-Lite does on the M4, done in a work */
#include <linux/rpmsg/ns.h>
#include <linux/virtio_config.h>
#include <linux/firmware.h>
#include "o2.h"
#include "remoteproc_internal.h"   /* rproc_vq_interrupt, rproc_elf_* */

struct o2_hdr { u32 src, dst, reserved; u16 len, flags; u8 data[]; } __packed; /* = rpmsg_hdr */

static void *o2_find_rsc(struct rproc *rproc, u32 type)    /* walk OUR resource table */
{
	struct resource_table *t = rproc->table_ptr;
	int i;

	for (i = 0; t && i < t->num; i++) {
		struct fw_rsc_hdr *h = (void *)t + t->offset[i];

		if (h->type == type)
			return (void *)h->data;
	}
	return NULL;
}

void o2_tprintf(struct o2_priv *p, const char *fmt, ...)   /* the M4's printf -> trace0 */
{
	struct fw_rsc_trace *tr = o2_find_rsc(p->rproc, RSC_TRACE);
	char *t = tr ? rproc_da_to_va(p->rproc, tr->da, tr->len, NULL) : NULL;
	va_list ap;

	if (!t)
		return;
	if (!p->tlen)
		p->tlen = strnlen(t, tr->len - 1);      /* keep the banner from the ELF */
	va_start(ap, fmt);
	p->tlen += vscnprintf(t + p->tlen, tr->len - p->tlen, fmt, ap);
	va_end(ap);
}

void o2_remote_reset(struct o2_priv *p)
{
	memset(p->last_avail, 0, sizeof(p->last_avail));
	p->vr_ready = p->ns_sent = false;
	p->kicks = p->rx_msgs = p->tx_msgs = 0;
	p->tlen = 0;                  /* ELF loader rewrote the trace buffer */
}
```

`rproc->table_ptr`은 `rproc_start()` 이후 **원격 메모리 안의 표**(`loaded_table`)를 가리킨다(§3.2). 그래서 이 코드가 보는 표는 실물 M4가 자기 SRAM `0x2000_0000`에서 읽는 표와 같은 바이트다.

### 5.4 가짜 원격 코어 (2/3) — 보내기, NS 광고, vring 준비

```c
/* remote -> Linux: take a free buffer from vring0's avail ring, fill it, put it on used */
static int o2_send(struct o2_priv *p, u32 src, u32 dst, const void *d, u16 len)
{
	struct vring *vr = &p->vr[0];
	u16 head, idx = p->last_avail[0];
	struct o2_hdr *m;

	if (idx == READ_ONCE(vr->avail->idx))
		return -ENOBUFS;                  /* Linux gave us no RX buffer */
	smp_rmb();
	head = vr->avail->ring[idx % vr->num];
	m = phys_to_virt(vr->desc[head].addr);    /* lab only: no IOMMU, see text */
	m->src = src; m->dst = dst; m->reserved = 0; m->len = len; m->flags = 0;
	memcpy(m->data, d, len);
	vr->used->ring[vr->used->idx % vr->num].id = head;
	vr->used->ring[vr->used->idx % vr->num].len = sizeof(*m) + len;
	smp_wmb();                               /* payload before used->idx */
	vr->used->idx++;
	p->last_avail[0]++;
	p->tx_msgs++;
	return 0;
}

static void o2_announce(struct o2_priv *p, const char *name, u32 addr)
{
	struct rpmsg_ns_msg ns = { .addr = addr, .flags = RPMSG_NS_CREATE };

	strscpy(ns.name, name, sizeof(ns.name));
	o2_send(p, addr, RPMSG_NS_ADDR, &ns, sizeof(ns));     /* to address 53 */
	o2_tprintf(p, "[fake-M4] NS announce '%s' @0x%x\n", name, addr);
}

static bool o2_vrings_up(struct o2_priv *p)     /* = OpenAMP rproc_virtio_wait_remote_ready */
{
	struct fw_rsc_vdev *v = o2_find_rsc(p->rproc, RSC_VDEV);
	int i;

	if (!v || !(READ_ONCE(v->status) & VIRTIO_CONFIG_S_DRIVER_OK))
		return false;
	for (i = 0; !p->vr_ready && i < 2; i++) {
		struct fw_rsc_vdev_vring *r = &v->vring[i];
		void *va = rproc_da_to_va(p->rproc, r->da, vring_size(r->num, r->align), NULL);

		if (!va)
			return false;
		vring_init(&p->vr[i], r->num, va, r->align);
		p->nid[i] = r->notifyid;              /* Linux wrote it into our table */
		o2_tprintf(p, "[fake-M4] vring%d da=0x%x num=%u notifyid=%u\n",
			   i, r->da, r->num, r->notifyid);
	}
	p->vr_ready = true;
	return true;
}
```

정직한 메모 두 가지.

- `phys_to_virt(desc.addr)` — virtio가 `VIRTIO_F_ACCESS_PLATFORM`을 협상하지 않으면 desc에는 **물리 주소**가 들어간다. 이 랩은 같은 Linux 안이라 물리 주소를 커널 선형 매핑으로 바꿔 접근했다. 실물 M4는 자기 주소 공간(da)으로 같은 DDR을 보므로 OpenAMP는 `metal_io_phys_to_virt()` 같은 libmetal I/O region 변환으로 이 일을 한다(기억 기반 — 함수 이름은 libmetal 문서로 확인). 또 rpmsg 버퍼가 원격이 볼 수 있는 영역에 있도록 실물 DT는 `vdev0buffer` reserved-memory를 준다(§10). 캐시가 일관되지 않은 실물에서는 이 영역을 non-cacheable로 매핑하거나 캐시 유지 연산이 필요하다. QEMU TCG는 캐시를 모델링하지 않아서 이 랩에서는 그런 문제가 드러나지 않는다.
- `DRIVER_OK` 대기 — 원격이 Linux의 virtio 드라이버가 vring을 초기화하기 전에 vring을 건드리면 Linux의 `memset(addr, 0, size)`(`rp_find_vq()`, 링 초기화)에 지워진다. OpenAMP 원격 앱도 vdev status를 폴링하며 기다린다.

### 5.5 가짜 원격 코어 (3/3) — kick을 받으면 도는 "IRQ 핸들러"

```c
void o2_remote_work(struct work_struct *w)       /* runs on every kick ("mailbox IRQ") */
{
	struct o2_priv *p = container_of(w, struct o2_priv, work);
	struct rproc *rproc = p->rproc;
	struct vring *tx = &p->vr[1];

	mutex_lock(&rproc->lock);                    /* same as stm32_rproc_mb_vq_work() */
	if (!p->running || !o2_vrings_up(p))
		goto out;
	if (!p->ns_sent) {
		o2_announce(p, "rpmsg-client-sample", 0x400);
		o2_announce(p, "rpmsg-raw", 0x401);
		p->ns_sent = true;
	}
	while (p->last_avail[1] != READ_ONCE(tx->avail->idx)) {   /* Linux -> remote */
		u16 head = tx->avail->ring[p->last_avail[1]++ % tx->num];
		struct o2_hdr *m = phys_to_virt(tx->desc[head].addr);

		smp_rmb();
		p->rx_msgs++;
		if (m->len >= 5 && !memcmp(m->data, "crash", 5)) {
			o2_tprintf(p, "[fake-M4] HardFault! (asked by 0x%x)\n", m->src);
			rproc_report_crash(rproc, RPROC_WATCHDOG);   /* = WDT/exception IRQ */
		} else {
			o2_send(p, m->dst, m->src, m->data, m->len);  /* echo service */
		}
		tx->used->ring[tx->used->idx % tx->num].id = head;
		tx->used->ring[tx->used->idx % tx->num].len = 0;
		smp_wmb();
		tx->used->idx++;
	}
	rproc_vq_interrupt(rproc, p->nid[0]);  /* "remote -> Linux mailbox IRQ": RX data */
	rproc_vq_interrupt(rproc, p->nid[1]);  /* TX buffers returned */
out:
	mutex_unlock(&rproc->lock);
}
```

여기서 `rproc_vq_interrupt(rproc, notifyid)`가 **실물 드라이버의 mailbox 수신 경로가 마지막에 부르는 바로 그 함수**다. i.MX는 mailbox 콜백에서 work를 큐잉하고 그 work에서 `idr_for_each(&rproc->notifyids, ...)`로 모든 vring에 대해 부르고, STM32는 vq별 work에서 `rproc->lock`을 잡고 부른다(§10에서 소스 확인). hardirq에서 바로 부르지 않는 이유는 그 아래 rpmsg 수신 경로가 `mutex`(`endpoints_lock`, `ept->cb_lock`)를 잡기 때문이다.

### 5.6 예제 9 — rpmsg 올리기: vring 주소, NS 광고, 채널, 샘플 드라이버

무엇을 확인하나: virtio_rpmsg_bus가 붙으면 표의 vring 주소 · status가 채워지고, 원격의 NS 광고로 채널 두 개가 생기며, 각 채널에 드라이버가 매칭되는 것.

```sh
#!/bin/sh
# t2.sh — rpmsg: vrings, name service, rpmsg_client_sample, rpmsg_char endpoints
R=/sys/class/remoteproc/remoteproc0; D=/sys/kernel/debug/remoteproc/remoteproc0
for m in virtio_rpmsg_bus rpmsg_ctrl rpmsg_char rpmsg_client_sample; do modprobe $m; done
echo "file rpmsg_ns.c +p; file rpmsg_core.c +p" > /sys/kernel/debug/dynamic_debug/control
insmod /o2_fakerproc.ko
echo start > $R/state; sleep 3
echo "== state: $(cat $R/state)"
echo "== vdev entry now:"; sed -n 1,25p $D/resource_table
echo "== trace0:"; cat $D/trace0
echo "== rpmsg bus devices:"
for d in /sys/bus/rpmsg/devices/*; do
  echo "$(basename $d): name=$(cat $d/name) src=$(cat $d/src) dst=$(cat $d/dst) driver=$(basename $(readlink $d/driver) 2>/dev/null)"
done
echo "== char devices:"; ls -l /dev/rpmsg* | sed 's/  */ /g'
for c in /sys/class/rpmsg/*; do echo "$(basename $c): $(cat $c/name 2>/dev/null)"; done
echo "== sample driver:"; dmesg | grep rpmsg_client_sample | sed 's/^\[ *[0-9.]*\]\[ *T[0-9]*\] //' | sed -n '1,3p;$p'
echo "== rpmsg_ping:"; /rpmsg_ping 5
/rpmsg_ping 300 > /tmp/rtt.txt && sort -n /tmp/rtt.txt | awk '{a[NR]=$1} END {print "n="NR" min="a[1]" p50="a[int(NR/2)]" p99="a[int(NR*0.99)]" max="a[NR]" us"}'
echo "== raw channel (/dev/rpmsg0 = rpmsg-raw @0x401):"
cat /dev/rpmsg0 > /dev/null & P=$!; sleep 1
echo -n "x" > /dev/rpmsg0; kill $P; wait $P 2>/dev/null     # second open() -> EBUSY
exec 3<>/dev/rpmsg0                                         # one fd for both directions
printf "hello raw" >&3; echo "read back: '$(dd bs=512 count=1 <&3 2>/dev/null)'"; exec 3>&-
echo stop > $R/state; echo "== state: $(cat $R/state)"
echo "== dmesg:"; dmesg | grep -E "o2|rpmsg|virtio" | grep -v rpmsg_client_sample | sed 's/^\[ *[0-9.]*\]\[ *T[0-9]*\] //'
```

출력(앞부분 — 예제 10, 11의 출력은 각 절에서 따로 본다):

```text
lab initramfs: 6.18.55-dom up 12.76s
== state: running
== vdev entry now:
Entry 0 is of type vdev
  ID 7
  Notify ID 0
  Device features 0x1
  Guest features 0x1
  Config length 0x0
  Status 0x7
  Number of vrings 2
  Reserved (should be zero) [0][0]

  Vring 0
    Device Address 0x7cc60000
    Alignment 4096
    Number of buffers 16
    Notify ID 0
    Physical Address 0x0

  Vring 1
    Device Address 0x7cc62000
    Alignment 4096
    Number of buffers 16
    Notify ID 1
    Physical Address 0x0

Entry 1 is of type trace
== trace0:
[o2-fw] boot banner: image v1.0 (written by ELF loader)
[fake-M4] out of reset, PC=0x45
[fake-M4] vring0 da=0x7cc60000 num=16 notifyid=0
[fake-M4] vring1 da=0x7cc62000 num=16 notifyid=1
[fake-M4] NS announce 'rpmsg-client-sample' @0x400
[fake-M4] NS announce 'rpmsg-raw' @0x401
== rpmsg bus devices:
virtio1.rpmsg-client-sample.-1.1024: name=rpmsg-client-sample src=0x400 dst=0x400 driver=rpmsg_client_sample
virtio1.rpmsg-raw.-1.1025: name=rpmsg-raw src=0x401 dst=0x401 driver=rpmsg_chrdev
virtio1.rpmsg_ctrl.0.0: name=rpmsg_ctrl src=0x0 dst=0x0 driver=rpmsg_ctrl
virtio1.rpmsg_ns.53.53: name=rpmsg_ns src=0x35 dst=0x35 driver=rpmsg_ns
== char devices:
crw------- 1 0 0 506, 0 Oct 18 16:29 /dev/rpmsg0
crw------- 1 0 0 505, 0 Oct 18 16:29 /dev/rpmsg_ctrl0
rpmsg0: rpmsg-raw
rpmsg_ctrl0: 
== sample driver:
rpmsg_client_sample virtio1.rpmsg-client-sample.-1.1024: new channel: 0x400 -> 0x400!
rpmsg_client_sample virtio1.rpmsg-client-sample.-1.1024: incoming msg 1 (src: 0x400)
rpmsg_client_sample virtio1.rpmsg-client-sample.-1.1024: incoming msg 2 (src: 0x400)
rpmsg_client_sample virtio1.rpmsg-client-sample.-1.1024: goodbye!
```

dmesg 부분:

```text
virtio-pci 0000:00:01.0: enabling device (0000 -> 0003)
o2_fakerproc: loading out-of-tree module taints kernel.
o2_fakerproc: module verification failed: signature and/or required key missing - tainting kernel
remoteproc remoteproc0: o2-m4 is available
o2_fakerproc o2_fakerproc: o2: registered remoteproc0 (attach=0)
remoteproc remoteproc0: powering up o2-m4
remoteproc remoteproc0: Booting fw image o2_m4_fw.elf, size 133684
remoteproc remoteproc0: o2: start: release core from reset, bootaddr 0x45
remoteproc remoteproc0: o2: kick vqid 0 (doorbell #1)
virtio_rpmsg_bus virtio1: rpmsg host is online
rproc-virtio rproc-virtio.1.auto: registered virtio1 (type 7)
remoteproc remoteproc0: remote processor o2-m4 is now up
NS announcement: 72 70 6d 73 67 2d 63 6c 69 65 6e 74 2d 73 61 6d  rpmsg-client-sam
virtio_rpmsg_bus virtio1: creating channel rpmsg-client-sample addr 0x400
remoteproc remoteproc0: o2: kick vqid 1 (doorbell #2)
NS announcement: 72 70 6d 73 67 2d 72 61 77 00 00 00 00 00 00 00  rpmsg-raw.......
virtio_rpmsg_bus virtio1: creating channel rpmsg-raw addr 0x401
remoteproc remoteproc0: o2: kick vqid 0 (doorbell #3)
remoteproc remoteproc0: o2: stop: core in reset (kicks=814 rx=406 tx=408)
remoteproc remoteproc0: stopped remote processor o2-m4
[   27.573993][  T133] reboot: Power down
```

출력에서 볼 것:

- **표가 바뀌었다**: `Status 0x7` = ACKNOWLEDGE(1) + DRIVER(2) + DRIVER_OK(4), `Guest features 0x1` = Linux가 NS 기능을 수락, vring `Device Address`가 carveout 주소(`0x7cc60000`, `0x7cc62000`)로 채워졌다. 예제 8과 비교하면 이 세 칸이 **virtio 드라이버가 붙어야** 채워진다는 것이 분명하다.
- 순서: `start` → `kick vqid 0`(rpmsg_probe 끝에서 "RX 버퍼 넣었다"고 알림) → `rpmsg host is online` → 원격이 DRIVER_OK를 확인하고 vring을 매핑(trace) → NS 광고 두 번(`NS announcement:` hex dump는 `rpmsg_ns.c`의 dev_dbg) → `creating channel`.
- 채널 장치 이름 `virtio1.rpmsg-client-sample.-1.1024`는 `rpmsg_core.c`의 `"%s.%s.%d.%d"`(부모, 이름, src, dst)다. 채널 생성 시 src는 아직 ANY(-1), dst는 원격 주소 1024(0x400). 그 뒤 sysfs `src=0x400`은 Linux가 새로 배정한 **로컬** endpoint 주소다. 숫자가 같은 0x400인 것은 우연이다 — 양쪽 주소 공간은 따로다.
- `rpmsg_client_sample`은 probe에서 "hello world!"를 보내고, 받을 때마다 다시 보내다가 100번째에 `goodbye!`. 우리 원격은 받은 것을 그대로 돌려주는 echo라 핑퐁이 끝까지 돌았다.
- `kicks=814 rx=406 tx=408` — 원격이 받은 406개(샘플 100 + ping 305 + "hello raw" 1)를 돌려보내고 NS 2개를 더 보내 408. kick이 메시지 수의 약 2배인 이유: Linux는 **보낼 때**(vring1) 한 번, **받은 버퍼를 vring0에 다시 걸 때**(`rpmsg_recv_done()` 끝의 `virtqueue_kick(vrp->rvq)`) 한 번 kick한다.

### 5.7 전체 메시지 흐름 — 시퀀스 다이어그램

예제 8, 9의 로그를 시간 순서로 한 장에 놓으면 이렇다. 번호는 실제 로그 순서와 같다.

```svg
<svg viewBox="0 0 680 640" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="10" width="80" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="60" y="32" text-anchor="middle" font-size="12">유저</text>
  <rect x="130" y="10" width="120" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="190" y="32" text-anchor="middle" font-size="12">remoteproc</text>
  <rect x="270" y="10" width="120" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="330" y="32" text-anchor="middle" font-size="12">virtio_rpmsg</text>
  <rect x="410" y="10" width="120" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="470" y="32" text-anchor="middle" font-size="12">공유 메모리</text>
  <rect x="560" y="10" width="100" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="610" y="32" text-anchor="middle" font-size="12">원격 M4</text>
  <line x1="60" y1="44" x2="60" y2="630" stroke="currentColor" stroke-dasharray="4 4"/>
  <line x1="190" y1="44" x2="190" y2="630" stroke="currentColor" stroke-dasharray="4 4"/>
  <line x1="330" y1="44" x2="330" y2="630" stroke="currentColor" stroke-dasharray="4 4"/>
  <line x1="470" y1="44" x2="470" y2="630" stroke="currentColor" stroke-dasharray="4 4"/>
  <line x1="610" y1="44" x2="610" y2="630" stroke="currentColor" stroke-dasharray="4 4"/>
  <line x1="60" y1="72" x2="186" y2="72" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="66" y="66" font-size="12">1 echo start</text>
  <line x1="190" y1="102" x2="466" y2="102" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="200" y="96" font-size="12">2 ELF 세그먼트 복사 (da_to_va)</text>
  <line x1="190" y1="132" x2="466" y2="132" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="200" y="126" font-size="12">3 vring · carveout 할당, 표에 기록</text>
  <line x1="190" y1="162" x2="606" y2="162" stroke="#3f9a6b" marker-end="url(#ah6)"/>
  <text x="200" y="156" font-size="12">4 ops->start: 리셋 해제 (bootaddr 0x45)</text>
  <line x1="190" y1="192" x2="326" y2="192" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="196" y="186" font-size="12">5 virtio 등록</text>
  <line x1="330" y1="222" x2="466" y2="222" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="336" y="216" font-size="12">6 RX 버퍼 16개</text>
  <line x1="330" y1="252" x2="466" y2="252" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="336" y="246" font-size="12">7 DRIVER_OK</text>
  <line x1="330" y1="282" x2="606" y2="282" stroke="#d0564a" stroke-width="2" marker-end="url(#ah6)"/>
  <text x="340" y="276" font-size="12">8 kick vqid 0 (mailbox)</text>
  <line x1="610" y1="312" x2="474" y2="312" stroke="#e08a3c" marker-end="url(#ah6)"/>
  <text x="482" y="306" font-size="12">9 표 읽기</text>
  <line x1="610" y1="342" x2="474" y2="342" stroke="#e08a3c" marker-end="url(#ah6)"/>
  <text x="482" y="336" font-size="12">10 NS x2</text>
  <line x1="610" y1="372" x2="334" y2="372" stroke="#d0564a" stroke-width="2" marker-end="url(#ah6)"/>
  <text x="340" y="366" font-size="12">11 IRQ: rproc_vq_interrupt</text>
  <rect x="200" y="384" width="260" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="330" y="401" text-anchor="middle" font-size="12">12 rpmsg_ns: 채널 생성</text>
  <text x="330" y="417" text-anchor="middle" font-size="12">→ 드라이버 probe, /dev/rpmsg0</text>
  <line x1="60" y1="452" x2="326" y2="452" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="66" y="446" font-size="12">13 write(/dev/rpmsg1, "ping 0")</text>
  <line x1="330" y1="482" x2="466" y2="482" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="336" y="476" font-size="12">14 vring1 avail</text>
  <line x1="330" y1="512" x2="606" y2="512" stroke="#d0564a" stroke-width="2" marker-end="url(#ah6)"/>
  <text x="340" y="506" font-size="12">15 kick vqid 1</text>
  <line x1="610" y1="542" x2="474" y2="542" stroke="#e08a3c" marker-end="url(#ah6)"/>
  <text x="482" y="536" font-size="12">16 echo→vring0</text>
  <line x1="610" y1="572" x2="334" y2="572" stroke="#d0564a" stroke-width="2" marker-end="url(#ah6)"/>
  <text x="340" y="566" font-size="12">17 IRQ → recv_done → ept cb</text>
  <line x1="330" y1="602" x2="64" y2="602" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="70" y="596" font-size="12">18 read() 반환 "ping 0"</text>
  <text x="70" y="624" font-size="12">빨강 = mailbox 알림 (kick, IRQ). 데이터는 전부 공유 메모리로 간다</text>
</svg>
```

그림 6 — 부팅 · vring 준비 · NS 광고 · 송수신의 실제 순서. 1~4는 remoteproc(생명주기), 5~12는 virtio/rpmsg 초기화, 13~18이 메시지 한 번 왕복이다. 이 랩에서는 빨간 화살표가 `o2_kick()` → `schedule_work()`와 `rproc_vq_interrupt()` 직접 호출로 대체되어 있다.

---

## 6. 유저 공간에서 rpmsg 쓰기 — rpmsg_char, rpmsg_ctrl

### 6.1 두 가지 길

| 방법 | 장치 | 언제 생기나 | endpoint 주소 |
|---|---|---|---|
| 원격이 `rpmsg-raw`를 NS로 광고 | `/dev/rpmsgN` | 광고 즉시 (rpmsg_char가 probe) | 채널의 src/dst 그대로 |
| 유저가 `/dev/rpmsg_ctrlN`에 ioctl | `/dev/rpmsgN` | `RPMSG_CREATE_EPT_IOCTL` 호출 시 | 유저가 `struct rpmsg_endpoint_info`로 지정 |

uapi(`include/uapi/linux/rpmsg.h`, 6.18.55):

```c
#define RPMSG_ADDR_ANY		0xFFFFFFFF
struct rpmsg_endpoint_info {
	char name[32];
	__u32 src;
	__u32 dst;
};
#define RPMSG_CREATE_EPT_IOCTL	_IOW(0xb5, 0x1, struct rpmsg_endpoint_info)
#define RPMSG_DESTROY_EPT_IOCTL	_IO(0xb5, 0x2)
#define RPMSG_CREATE_DEV_IOCTL	_IOW(0xb5, 0x3, struct rpmsg_endpoint_info)
#define RPMSG_RELEASE_DEV_IOCTL	_IOW(0xb5, 0x4, struct rpmsg_endpoint_info)
#define RPMSG_GET_OUTGOING_FLOWCONTROL _IOR(0xb5, 0x5, int)
#define RPMSG_SET_INCOMING_FLOWCONTROL _IOR(0xb5, 0x6, int)
```

`/dev/rpmsgN` 의미론은 **메시지 단위**다. `write()` 한 번 = rpmsg 메시지 하나, `read()` 한 번 = 받은 메시지 하나(커널이 `rpmsg_ept_cb()`에서 skb 큐에 넣어 둔 것). 바이트 스트림인 UART(`ttyRPMSG`)와 다르다. 그래서 프레이밍(O3, N4 §5)을 따로 할 필요가 없지만, 496 B 한계는 지켜야 한다.

### 6.2 예제 10 — ioctl로 endpoint를 만들고 원격 echo와 왕복 (C)

무엇을 확인하는 코드인가: `rpmsg_ctrl0` → `CREATE_EPT` → 새로 생긴 `/dev/rpmsgN` 찾기 → write/read 왕복 → `DESTROY_EPT`. 실무 앱(모터 명령 송신, 상태 수신)의 뼈대 그대로다.

```c
/* rpmsg_ping.c — create an endpoint via /dev/rpmsg_ctrl0, then ping the remote echo service */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/rpmsg.h>

static int find_ept(const char *name)        /* which /dev/rpmsgN did the ioctl create? */
{
    char p[300], buf[64]; struct dirent *d; int n = -1;
    DIR *dir = opendir("/sys/class/rpmsg");
    if (!dir) { perror("opendir"); exit(1); }
    while ((d = readdir(dir))) {
        if (sscanf(d->d_name, "rpmsg%d", &n) != 1) continue;
        snprintf(p, sizeof(p), "/sys/class/rpmsg/%s/name", d->d_name);
        FILE *f = fopen(p, "r");
        if (f && fgets(buf, sizeof(buf), f) && !strncmp(buf, name, strlen(name))) { fclose(f); break; }
        if (f) fclose(f);
        n = -1;
    }
    closedir(dir);
    return n;
}

int main(int argc, char **argv)
{
    int count = argc > 1 ? atoi(argv[1]) : 5;
    struct rpmsg_endpoint_info ei = { .name = "o2-ping", .src = RPMSG_ADDR_ANY, .dst = 0x401 };
    int ctrl = open("/dev/rpmsg_ctrl0", O_RDWR);
    if (ctrl < 0) { perror("open rpmsg_ctrl0"); return 1; }
    if (ioctl(ctrl, RPMSG_CREATE_EPT_IOCTL, &ei) < 0) { perror("CREATE_EPT"); return 1; }
    int n = find_ept("o2-ping");
    char dev[32], msg[64], rx[512];
    snprintf(dev, sizeof(dev), "/dev/rpmsg%d", n);
    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror(dev); return 1; }
    fprintf(stderr, "endpoint %s -> remote 0x%x\n", dev, ei.dst);
    for (int i = 0; i < count; i++) {
        struct timespec a, b;
        int len = snprintf(msg, sizeof(msg), "ping %d", i);
        clock_gettime(CLOCK_MONOTONIC, &a);
        if (write(fd, msg, len) != len) { perror("write"); return 1; }
        ssize_t r = read(fd, rx, sizeof(rx) - 1);  /* one read() = one rpmsg message */
        clock_gettime(CLOCK_MONOTONIC, &b);
        if (r < 0) { perror("read"); return 1; }
        rx[r] = 0;
        long us = (b.tv_sec - a.tv_sec) * 1000000L + (b.tv_nsec - a.tv_nsec) / 1000;
        if (count <= 10) printf("  tx '%s' rx '%s' (%zd B) rtt %ld us\n", msg, rx, r, us);
        else printf("%ld\n", us);
    }
    if (ioctl(fd, RPMSG_DESTROY_EPT_IOCTL) < 0) perror("DESTROY_EPT");
    close(fd); close(ctrl);
    return 0;
}
```

빌드는 VM에서 정적 링크로 했다(BusyBox initramfs에는 glibc가 없다). 경고 0개.

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static rpmsg_ping.c -o /var/tmp/O2/bin/rpmsg_ping
```

예제 9 스크립트 안의 `/rpmsg_ping 5`와 `/rpmsg_ping 300` 결과(TCG + 공유 VM, 실물 수치 아님):

```text
== rpmsg_ping:
endpoint /dev/rpmsg1 -> remote 0x401
  tx 'ping 0' rx 'ping 0' (6 B) rtt 1336 us
  tx 'ping 1' rx 'ping 1' (6 B) rtt 7318 us
  tx 'ping 2' rx 'ping 2' (6 B) rtt 2256 us
  tx 'ping 3' rx 'ping 3' (6 B) rtt 570 us
  tx 'ping 4' rx 'ping 4' (6 B) rtt 376 us
endpoint /dev/rpmsg1 -> remote 0x401
n=300 min=266 p50=374 p99=8068 max=11281 us
```

출력에서 볼 것: `/dev/rpmsg0`은 이미 `rpmsg-raw` 채널이 차지했으므로 ioctl로 만든 endpoint는 `/dev/rpmsg1`이 됐다. 그래서 코드가 이름으로 sysfs를 뒤져 번호를 찾는다(번호를 하드코딩하면 부팅마다 바뀐다). RTT 분포는 중앙값 수백 µs, 꼬리는 수 ms다.

```svg
<svg viewBox="0 0 640 260" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="20" font-size="13">rpmsg_ping RTT, 300회 (µs, 로그 축) — TCG 에뮬레이션</text>
  <line x1="110" y1="40" x2="110" y2="200" stroke="currentColor"/>
  <line x1="110" y1="200" x2="610" y2="200" stroke="currentColor"/>
  <line x1="110" y1="200" x2="110" y2="206" stroke="currentColor"/>
  <text x="110" y="222" text-anchor="middle" font-size="12">100</text>
  <line x1="235" y1="200" x2="235" y2="206" stroke="currentColor"/>
  <text x="235" y="222" text-anchor="middle" font-size="12">1k</text>
  <line x1="360" y1="200" x2="360" y2="206" stroke="currentColor"/>
  <text x="360" y="222" text-anchor="middle" font-size="12">10k</text>
  <line x1="485" y1="200" x2="485" y2="206" stroke="currentColor"/>
  <text x="485" y="222" text-anchor="middle" font-size="12">100k</text>
  <rect x="110" y="50" width="53" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="100" y="68" text-anchor="end" font-size="12">min</text>
  <text x="171" y="68" font-size="12">266</text>
  <rect x="110" y="86" width="72" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="100" y="104" text-anchor="end" font-size="12">p50</text>
  <text x="190" y="104" font-size="12">374</text>
  <rect x="110" y="122" width="238" height="26" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="100" y="140" text-anchor="end" font-size="12">p99</text>
  <text x="356" y="140" font-size="12">8068</text>
  <rect x="110" y="158" width="256" height="26" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="100" y="176" text-anchor="end" font-size="12">max</text>
  <text x="374" y="176" font-size="12">11281</text>
  <text x="20" y="250" font-size="12">경로: write → vring1 → kick → work(가짜 원격) → vring0 → vq 인터럽트 → read</text>
</svg>
```

그림 7 — 예제 10에서 직접 잰 왕복 지연(막대 길이 = log10 값, 100 µs 기준). 가짜 원격이 workqueue라 스케줄러 지연이 그대로 들어가고, TCG가 느려서 절대값은 의미가 없다. 실물에서도 볼 것은 같다: **중앙값보다 꼬리**. 유저 공간 앱이 rpmsg 왕복을 제어 루프 안에 두면 꼬리가 루프 지터가 된다(N4 §6과 같은 결론).

### 6.3 예제 11 — 함정: `/dev/rpmsgN`은 한 번에 한 번만 열린다

무엇을 확인하나: 예제 9 스크립트의 raw 채널 부분. `cat`이 `/dev/rpmsg0`을 열고 있는 동안 다른 프로세스가 열면?

```text
== raw channel (/dev/rpmsg0 = rpmsg-raw @0x401):
/t2.sh: line 22: can't create /dev/rpmsg0: Device or resource busy
read back: 'hello raw'
== state: offline
```

출력에서 볼 것: 두 번째 `open()`이 `EBUSY`다. 소스 `rpmsg_char.c`의 `rpmsg_eptdev_open()`이 `if (eptdev->ept) { ... return -EBUSY; }` — 장치 하나에 endpoint 하나, 열린 fd 하나다. "읽는 프로세스와 쓰는 프로세스를 따로" 설계하면 막힌다. 해결은 **fd 하나로 읽기 · 쓰기**(스크립트의 `exec 3<>/dev/rpmsg0`, C에서는 `O_RDWR` 하나 + `poll()`), 또는 용도별로 endpoint를 여러 개 만드는 것이다. 두 번째 줄 `hello raw`가 그 방식으로 왕복한 결과다.

### 6.4 예제 12 — systemd/udev rootfs에서는 누가 드라이버를 올리나

BusyBox 게스트에서는 `modprobe`를 손으로 했다. 실제 제품 rootfs(udev — G9)에서는 어떻게 되는지 `rootfs-dom`(Ubuntu 24.04 + systemd 255)을 qcow2 오버레이로 부팅해 확인했다. 모듈과 펌웨어는 `mkfs.ext4 -d`로 만든 32 MB 데이터 디스크로 넣었다.

```sh
qemu-img create -q -f qcow2 -F raw -b /var/lab/out/rootfs-dom.ext4 /var/tmp/O2/o2.qcow2
mkfs.ext4 -q -d /var/tmp/O2/share /var/tmp/O2/share.img 32M
# expect 스크립트로 부팅 (-m 1536M, -drive o2.qcow2 + share.img) 후 게스트에서:
mount /dev/vdb /mnt && sh /mnt/t5.sh
```

```sh
#!/bin/sh
mkdir -p /lib/firmware && cp /mnt/o2_m4_fw.elf /lib/firmware/
insmod /mnt/o2_fakerproc.ko
udevadm monitor --kernel --udev --subsystem-match=virtio --subsystem-match=rpmsg > /tmp/mon.txt 2>&1 & M=$!
sleep 2; echo start > /sys/class/remoteproc/remoteproc0/state; sleep 15; kill $M
cat /tmp/mon.txt
lsmod | grep -E "rpmsg" | awk "{print \$1}" | tr "\n" " "; echo
```

출력(경로 앞부분 `/devices/platform/o2_fakerproc/remoteproc/remoteproc0/rproc-virtio.1.auto/`는 `…/`로 줄였다):

```text
KERNEL[149.537581] add      …/virtio3 (virtio)
KERNEL[154.717454] add      …/virtio3/virtio3.rpmsg_ctrl.0.0 (rpmsg)
KERNEL[154.731510] add      …/virtio3/virtio3.rpmsg_ns.53.53 (rpmsg)
KERNEL[154.742285] bind     …/virtio3/virtio3.rpmsg_ns.53.53 (rpmsg)
KERNEL[154.743278] add      …/virtio3/virtio3.rpmsg-client-sample.-1.1024 (rpmsg)
KERNEL[154.743995] add      …/virtio3/virtio3.rpmsg-raw.-1.1025 (rpmsg)
KERNEL[154.751355] bind     …/virtio3 (virtio)
UDEV  [154.772393] add      …/virtio3 (virtio)
KERNEL[154.957127] bind     …/virtio3/virtio3.rpmsg-client-sample.-1.1024 (rpmsg)
KERNEL[154.985392] add      …/virtio3/virtio3.rpmsg-raw.-1.1025/rpmsg/rpmsg0 (rpmsg)
KERNEL[154.986763] bind     …/virtio3/virtio3.rpmsg-raw.-1.1025 (rpmsg)
KERNEL[155.005489] add      …/virtio3/virtio3.rpmsg_ctrl.0.0/rpmsg/rpmsg_ctrl0 (rpmsg)
KERNEL[155.009360] bind     …/virtio3/virtio3.rpmsg_ctrl.0.0 (rpmsg)
...
rpmsg_ctrl rpmsg_client_sample rpmsg_char virtio_rpmsg_bus rpmsg_ns 
```

같은 게스트에서 본 modalias와 `modules.alias`:

```text
virtio dev: virtio3 modalias=virtio:d00000007v00000000 driver=
alias rpmsg:rpmsg-raw rpmsg_char
alias rpmsg:rpmsg_ctrl rpmsg_ctrl
alias rpmsg:rpmsg_ns rpmsg_ns
alias virtio:d00000007v* virtio_rpmsg_bus
```

출력에서 볼 것:

- 사슬 구조다: virtio 장치 add(modalias `virtio:d00000007…`) → udev가 `virtio_rpmsg_bus` 로드 → 그 probe가 rpmsg 장치들(`rpmsg_ctrl`, `rpmsg_ns`)을 add → 각각의 modalias(`rpmsg:rpmsg_ns` …)로 다음 모듈 로드 → NS 광고로 채널 add → `rpmsg_client_sample`, `rpmsg_char` 로드. 커널 add(149.5 s)부터 virtio 드라이버 bind(154.75 s)까지 **약 5초** 걸렸다(부하 높은 TCG에서 modprobe 시간).
- 같은 테스트를 `sleep 8` 후 바로 확인한 앞선 실행에서는 `driver=`가 비어 있고 `/dev/rpmsg_ctrl0`이 없어서 앱이 `open rpmsg_ctrl0: No such file or directory`로 실패했다(그때는 `udevadm trigger --action=add`로 다시 이벤트를 넣자 붙었다). 원인을 하나로 단정할 수는 없지만(그 실행에서 udev 이벤트를 관찰하지 않았다), 교훈은 같다: **`echo start` 직후 `/dev/rpmsg*`가 있다고 가정하지 말 것.** systemd 유닛이면 `udevadm settle`, 장치 유닛 의존성(`BindsTo=dev-rpmsg_ctrl0.device` 같은), 또는 앱에서 재시도 루프를 둔다.
- 앞선 실행의 또 다른 실패: Ubuntu Base에는 `/lib/firmware` 디렉터리가 없어서 `cp`가 실패했고, remoteproc는 `Direct firmware load for o2_m4_fw.elf failed with error -2` → `Boot failed: -2`를 냈다. 최소 rootfs(K-모듈)에서 펌웨어 디렉터리를 이미지 레시피에 넣는 것을 잊으면 정확히 이 로그가 나온다.

---

## 7. 원격 코어가 죽으면 — crash 보고 · coredump · recovery

### 7.1 흐름

1. 드라이버가 원격 코어의 죽음을 알아챈다. 실물: 원격 watchdog 인터럽트(STM32는 DT의 WWDG1 exti 인터럽트 → `stm32_rproc_wdg()`), 원격이 보낸 crash mailbox 메시지(TI K3는 `RP_MBOX_CRASH`를 받으면 현재 로그만 남긴다 — 소스 주석 "error recovery is not supported"), 또는 IOMMU fault.
2. `rproc_report_crash(rproc, type)` — 스핀락만 잡고 `rproc_recovery_wq`에 work를 넣는다(IRQ 문맥에서 불러도 된다).
3. `rproc_crash_handler_work()` — 상태를 `RPROC_CRASHED`로. recovery가 꺼져 있으면 여기서 멈춘다.
4. `rproc_trigger_recovery()` → `rproc_boot_recovery()`: `rproc_stop(rproc, true)` → `ops->coredump()` → `request_firmware()` → `rproc_start()`.

4번에서 `rproc_fw_boot()`가 아니라 `rproc_start()`를 부른다는 점이 중요하다. **resource table을 다시 해석하지 않고 carveout · vring 할당도 그대로 둔 채** 세그먼트만 다시 올리고 시작한다. 그래서 recovery 후 vring 주소가 이전과 같다(아래 예제의 trace에서 `da=0x7cc60000` 그대로).

### 7.2 예제 13 — 원격에 "crash"를 보내 HardFault 흉내 → coredump → 자동 복구

```sh
#!/bin/sh
# t3.sh — remote crash -> rproc_report_crash -> coredump -> recovery
R=/sys/class/remoteproc/remoteproc0; D=/sys/kernel/debug/remoteproc/remoteproc0
for m in virtio_rpmsg_bus rpmsg_ctrl rpmsg_char; do modprobe $m; done
insmod /o2_fakerproc.ko
echo enabled > $D/coredump
echo start > $R/state; sleep 2
echo "== before: state=$(cat $R/state) channels=$(ls /sys/bus/rpmsg/devices | tr '\n' ' ')"
exec 3<>/dev/rpmsg0; printf "crash" >&3; exec 3>&-     # ask the M4 to fault
sleep 3
echo "== after: state=$(cat $R/state) channels=$(ls /sys/bus/rpmsg/devices | tr '\n' ' ')"
echo "== trace0 after recovery:"; cat $D/trace0
echo "== devcoredump:"; ls /sys/class/devcoredump/
C=$(ls -d /sys/class/devcoredump/devcd* | head -1)
cat $C/data > /tmp/core; echo "size=$(wc -c < /tmp/core)"
echo "BEGIN-B64"; base64 /tmp/core; echo "END-B64"
echo "== disable recovery and crash again:"
echo disabled > $D/recovery; echo 2 > $D/crash; sleep 2    # 2 = RPROC_FATAL_ERROR
echo "state=$(cat $R/state)"
echo enabled > $D/recovery; sleep 2; echo "after re-enable: state=$(cat $R/state)"
echo "== dmesg:"; dmesg | grep -E "remoteproc|o2|rpmsg|virtio1" | sed 's/^\[ *[0-9.]*\]\[ *T[0-9]*\] //' | tail -40
```

출력(base64 본문은 생략, 콘솔에 섞여 찍힌 커널 메시지 줄은 그대로 둠):

```text
== before: state=running channels=virtio1.rpmsg-client-sample.-1.1024 virtio1.rpmsg-raw.-1.1025 virtio1.rpmsg_ctrl.0.0 virtio1.rpmsg_ns.53.53 [   19.682573][   T11] remoteproc remoteproc0: crash detected in o2-m4: type watchdog
[   19.804100][   T12] remoteproc remoteproc0: handling crash #1 in o2-m4
[   19.804494][   T12] remoteproc remoteproc0: recovering o2-m4

== after: state=running channels=virtio1.rpmsg-client-sample.-1.1024 virtio1.rpmsg-raw.-1.1025 virtio1.rpmsg_ctrl.0.0 virtio1.rpmsg_ns.53.53 
== trace0 after recovery:
[o2-fw] boot banner: image v1.0 (written by ELF loader)
[fake-M4] out of reset, PC=0x45
[fake-M4] vring0 da=0x7cc60000 num=16 notifyid=0
[fake-M4] vring1 da=0x7cc62000 num=16 notifyid=1
[fake-M4] NS announce 'rpmsg-client-sample' @0x400
[fake-M4] NS announce 'rpmsg-raw' @0x401
== devcoredump:
devcd1    disabled
size=3188
== disable recovery and crash again:
[   72.220136][   T12] remoteproc remoteproc0: handling crash #2 in o2-m4
[   72.220933][   T76] remoteproc remoteproc0: crash detected in o2-m4: type fatal error
[   74.715845][   T76] remoteproc remoteproc0: recovering o2-m4
state=crashed
after re-enable: state=running
```

dmesg 중 crash ~ 복구 부분:

```text
remoteproc remoteproc0: crash detected in o2-m4: type watchdog
remoteproc remoteproc0: handling crash #1 in o2-m4
remoteproc remoteproc0: recovering o2-m4
remoteproc remoteproc0: o2: stop: core in reset (kicks=3 rx=1 tx=2)
remoteproc remoteproc0: stopped remote processor o2-m4
remoteproc remoteproc0: o2: start: release core from reset, bootaddr 0x45
remoteproc remoteproc0: o2: kick vqid 0 (doorbell #1)
virtio_rpmsg_bus virtio1: rpmsg host is online
rproc-virtio rproc-virtio.1.auto: registered virtio1 (type 7)
remoteproc remoteproc0: remote processor o2-m4 is now up
virtio_rpmsg_bus virtio1: creating channel rpmsg-client-sample addr 0x400
virtio_rpmsg_bus virtio1: creating channel rpmsg-raw addr 0x401
remoteproc remoteproc0: o2: kick vqid 0 (doorbell #2)
```

출력에서 볼 것:

- crash → stop → start가 **한 번의 write로 자동** 진행됐다. 그 사이 rpmsg 채널은 전부 지워졌다가(`rproc_stop()` → subdevice stop → virtio 장치 제거) 원격의 새 NS 광고로 **같은 이름으로 다시 생겼다**. 유저 앱이 열고 있던 `/dev/rpmsgN` fd는 이 때 무효가 된다 — 앱은 `read()`/`write()` 에러를 받으면 장치를 다시 찾아 열어야 한다.
- trace0의 HardFault 줄은 사라지고 부팅 로그만 남았다. recovery가 ELF를 다시 올리며 trace 버퍼 초기값을 덮어썼기 때문이다. **crash 직전 로그를 보고 싶으면 coredump를 켜 둬야 한다** — 이게 coredump의 존재 이유다.
- `recovery=disabled`에서 debugfs `crash`에 `2`(fatal error)를 쓰자 `state=crashed`에서 멈췄고, `enabled`로 바꾸는 순간 복구됐다(`remoteproc_debugfs.c` 주석: "recovered too as soon as recovery is enabled"). 현장 디버깅에서는 recovery를 끄고 crashed 상태를 붙잡아 둔 채 JTAG로 붙는 식으로 쓴다. `crash` 파일은 `kstrtouint`로 **숫자**만 받는다 — 처음에 `fatal`을 썼더니 `write error: Invalid argument`가 났다.
- 두 번째 crash의 로그 순서가 `handling` → `detected`로 뒤집혀 보이는 것은 work가 다른 CPU에서 `dev_err()`보다 먼저 찍었기 때문이다(`rproc_report_crash()`는 큐에 넣은 **뒤** 로그를 찍는다).

### 7.3 예제 14 — coredump 파일 열어 보기

무엇을 확인하나: devcoredump가 만든 파일이 진짜 ELF core이고, 우리가 `rproc_coredump_add_segment()`로 등록한 두 구간(TCM 1 KB, SRAM 2 KB)이 들어 있는지. 게스트 콘솔에 찍은 base64를 VM에서 되돌려 `readelf`로 읽었다.

```sh
sed -n "/BEGIN-B64/,/END-B64/p" t3.log | sed "1d;\$d" | base64 -d > rproc_core.elf
arm-none-eabi-readelf -h -l rproc_core.elf | grep -E "Class|Type|Machine|Entry|LOAD|Number of program"
od -A x -t x1z -j 0x74 -N 16 rproc_core.elf
od -A x -t x4 -j $((0x474)) -N 0x60 rproc_core.elf
```

```text
  Class:                             ELF32
  Type:                              CORE (Core file)
  Machine:                           ARM
  Entry point address:               0x45
  Number of program headers:         2
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x000074 0x00000000 0x00000000 0x00400 0x00400 RWE 0
  LOAD           0x000474 0x20000000 0x20000000 0x00800 0x00800 RWE 0
000074 00 00 01 20 45 00 00 00 41 00 00 00 41 00 00 00  >... E...A...A...<
000084
000474 00000001 00000003 00000000 00000000
000484 0000001c 00000060 00000090 00000003
000494 00000007 00000000 00000001 00000001
0004a4 00000000 00000201 7cc60000 00001000
0004b4 00000010 00000000 00000000 7cc62000
0004c4 00001000 00000010 00000001 00000000
0004d4
```

출력에서 볼 것:

- `ET_CORE`, `EM_ARM`(probe의 `rproc_coredump_set_elf_info()`), entry = `bootaddr`. 크기 3188 = ELF 헤더 52 + phdr 32 x 2 + 0x400 + 0x800.
- 첫 세그먼트 처음 16 B는 **벡터 테이블**이다: 초기 SP `0x20010000`, Reset `0x45`, 나머지 핸들러 `0x41`(Default_Handler + Thumb). 실물 crash dump라면 여기서 스택 포인터와 예외 프레임을 읽고 `arm-none-eabi-gdb fw.elf core`로 분석한다.
- 두 번째 세그먼트 = SRAM = **resource table**이다. vring 주소 `7cc60000`/`7cc62000`과 notifyid `0`/`1`, gfeatures `1`이 **Linux가 써 준 그대로** 보인다. 반면 status 바이트(`0x4a8`의 `01`)는 7이 아니라 1이다. coredump는 `rproc_stop()` **다음**에 찍히고, 그 사이 virtio 장치 제거 경로가 status를 리셋한 뒤 ACKNOWLEDGE(1)를 다시 세웠기 때문이다(`virtio.c`의 `virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE)`). 덤프는 "죽은 순간"이 아니라 "**정지시킨 직후**"의 메모리다 — 실물에서도 stop이 메모리를 건드리는 드라이버라면 덤프 해석에 주의.

---

## 8. 이미 떠 있는 코어에 붙기 — attach / detach, 그리고 cdev

### 8.1 왜 필요한가

N4 §3에서 본 것처럼, 모터 PWM이나 CAN 웨이크업은 **Linux가 부팅하기 전**부터 M코어가 잡고 있어야 한다. 그래서 부트로더(U-Boot의 `rproc` 명령이나 i.MX의 `bootaux` — 기억 기반, 보드 문서 확인)가 M코어 펌웨어를 먼저 올리고 띄운다. 그 뒤 Linux가 부팅해서 "펌웨어를 다시 올리고 리셋"하면 모터가 멈춘다. 그래서 remoteproc에는 **attach** 모드가 있다: 리셋을 건드리지 않고, 원격 메모리에 이미 있는 resource table(`get_loaded_rsc_table`)을 읽어 vdev만 준비한다. 실물 판단 기준은 드라이버마다 다르다 — i.MX/STM32는 원격 코어가 이미 돌고 있는지 레지스터(STM32는 "m4 state" syscon/TAMP 레지스터, 기억 기반)로 보고 probe에서 `rproc->state = RPROC_DETACHED`를 세운다.

### 8.2 예제 15 — attach=1로 올리기, detach, 다시 attach, 그리고 stop

```sh
#!/bin/sh
# t4.sh — attach mode (core started by "bootloader"), detach, and the /dev/remoteproc0 cdev
R=/sys/class/remoteproc/remoteproc0; D=/sys/kernel/debug/remoteproc/remoteproc0
for m in virtio_rpmsg_bus rpmsg_ctrl rpmsg_char; do modprobe $m; done
insmod /o2_fakerproc.ko attach=1; sleep 2
echo "== attach=1: state=$(cat $R/state) channels=$(ls /sys/bus/rpmsg/devices | tr '\n' ' ')"
echo "== trace0:"; cat $D/trace0
/rpmsg_ping 2
echo detach > $R/state; sleep 1
echo "== after detach: state=$(cat $R/state) channels=$(ls /sys/bus/rpmsg/devices | tr '\n' ' ')"
echo start > $R/state; sleep 1
echo "== after start: state=$(cat $R/state)"
echo stop > $R/state; echo "== after stop: state=$(cat $R/state)"
echo "== dmesg:"; dmesg | grep -E "remoteproc|o2" | sed 's/^\[ *[0-9.]*\]\[ *T[0-9]*\] //'
rmmod o2_fakerproc
echo "== normal mode + cdev:"; insmod /o2_fakerproc.ko; /rproc_cdev
dmesg | grep -E "remoteproc" | sed 's/^\[ *[0-9.]*\]\[ *T[0-9]*\] //' | tail -6
```

```text
== attach=1: state=attached channels=virtio1.rpmsg-client-sample.-1.1024 virtio1.rpmsg-raw.-1.1025 virtio1.rpmsg_ctrl.0.0 virtio1.rpmsg_ns.53.53 
== trace0:
[o2-fw] boot banner: image v1.0 (written by ELF loader)
[fake-M4] Linux attached to me (I was started by the bootloader)
[fake-M4] vring0 da=0x7cc60000 num=16 notifyid=0
[fake-M4] vring1 da=0x7cc62000 num=16 notifyid=1
[fake-M4] NS announce 'rpmsg-client-sample' @0x400
[fake-M4] NS announce 'rpmsg-raw' @0x401
endpoint /dev/rpmsg1 -> remote 0x401
  tx 'ping 0' rx 'ping 0' (6 B) rtt 18195 us
  tx 'ping 1' rx 'ping 1' (6 B) rtt 522 us
== after detach: state=detached channels=
== after start: state=attached
== after stop: state=offline
== dmesg:
o2_fakerproc: loading out-of-tree module taints kernel.
o2_fakerproc: module verification failed: signature and/or required key missing - tainting kernel
remoteproc remoteproc0: o2-m4 is available
o2_fakerproc o2_fakerproc: o2: registered remoteproc0 (attach=1)
remoteproc remoteproc0: attaching to o2-m4
remoteproc remoteproc0: o2: kick vqid 0 (doorbell #1)
remoteproc remoteproc0: remote processor o2-m4 is now attached
remoteproc remoteproc0: o2: kick vqid 0 (doorbell #2)
remoteproc remoteproc0: o2: kick vqid 1 (doorbell #3)
remoteproc remoteproc0: detached remote processor o2-m4
remoteproc remoteproc0: attaching to o2-m4
remoteproc remoteproc0: o2: kick vqid 0 (doorbell #1)
remoteproc remoteproc0: remote processor o2-m4 is now attached
remoteproc remoteproc0: o2: kick vqid 0 (doorbell #2)
remoteproc remoteproc0: o2: stop: core in reset (kicks=2 rx=0 tx=2)
remoteproc remoteproc0: stopped remote processor o2-m4
```

출력에서 볼 것:

- `Booting fw image` 줄이 **없다**. attach 경로(`rproc_attach()`)는 `request_firmware()`를 부르지 않고 `rproc_set_rsc_table()` → `get_loaded_rsc_table`로 원격 메모리의 표를 그대로 쓴다. trace 첫 줄(배너)도 "부트로더"(`o2_preload()`)가 올린 것이다.
- `detach` → 채널이 전부 사라지고(`channels=` 빈칸) `detached`. 원격 코어는 계속 돈다는 의미다(실물에서는 원격에 "Linux가 떠난다"를 알리는 신호를 보낸다 — STM32의 `detach` mailbox 채널, H10 §9.2). 다시 `start`를 쓰면 상태가 DETACHED라서 `rproc_boot()`가 **부팅이 아니라 attach**를 한다(`if (rproc->state == RPROC_DETACHED) ... rproc_attach(rproc);`).
- attached 상태에서 `stop`을 쓰면 진짜로 `ops->stop`이 불려 `offline`이 된다. 이후 `start`는 `/lib/firmware`에서 펌웨어를 읽는 일반 부팅이다. "Linux가 M코어를 멈출 권한이 있는가"는 제품 정책으로 정해야 한다 — 안전 기능을 맡은 코어라면 Linux 쪽 드라이버에서 `stop`을 막는 것(또는 ops에 stop을 두지 않는 것)도 방법이다.

### 8.3 예제 16 — `/dev/remoteproc0` cdev: 프로세스 수명에 코어 수명을 묶기

무엇을 확인하나: sysfs 대신 문자 장치로 start하고, `RPROC_SET_SHUTDOWN_ON_RELEASE`를 켜 두면 **이 프로세스가 죽을 때(정상 종료든 kill -9든) 커널이 코어를 내린다**. 원격 코어를 쓰는 데몬 하나가 코어를 "소유"하게 만드는 패턴이다.

```c
/* rproc_cdev.c — boot the remote core through /dev/remoteproc0 and let close() stop it */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/remoteproc_cdev.h>

static void show_state(const char *when)
{
    char buf[32] = "";
    int fd = open("/sys/class/remoteproc/remoteproc0/state", O_RDONLY);
    if (fd < 0) { perror("open state"); exit(1); }
    if (read(fd, buf, sizeof(buf) - 1) < 0) { perror("read state"); exit(1); }
    close(fd);
    printf("%-22s state=%s", when, buf);
}

int main(void)
{
    int on = 1, fd = open("/dev/remoteproc0", O_RDWR);
    if (fd < 0) { perror("open /dev/remoteproc0"); return 1; }
    show_state("before:");
    if (write(fd, "start", 5) != 5) { perror("write start"); return 1; }
    show_state("after write(start):");
    /* if this process dies (crash, kill -9), the kernel shuts the core down for us */
    if (ioctl(fd, RPROC_SET_SHUTDOWN_ON_RELEASE, &on) < 0) { perror("ioctl"); return 1; }
    if (ioctl(fd, RPROC_GET_SHUTDOWN_ON_RELEASE, &on) < 0) { perror("ioctl"); return 1; }
    printf("shutdown_on_release=%d\n", on);
    sleep(1);
    close(fd);                                   /* -> rproc_shutdown() */
    show_state("after close():");
    return 0;
}
```

예제 15 스크립트의 마지막 부분 출력:

```text
== normal mode + cdev:
before:                state=offline
after write(start):    state=running
shutdown_on_release=1
after close():         state=offline
remoteproc remoteproc0: o2: start: release core from reset, bootaddr 0x45
remoteproc remoteproc0: o2: kick vqid 0 (doorbell #1)
remoteproc remoteproc0: remote processor o2-m4 is now up
remoteproc remoteproc0: o2: kick vqid 0 (doorbell #2)
remoteproc remoteproc0: o2: stop: core in reset (kicks=2 rx=0 tx=2)
remoteproc remoteproc0: stopped remote processor o2-m4
```

출력에서 볼 것: `close()` 하나로 `offline`. 반대로 sysfs로 `start`한 코어는 누가 내려 주지 않는 한 계속 돈다. 관리 데몬이 비정상 종료했을 때 원격 코어도 내릴지(cdev + shutdown_on_release) 계속 둘지(sysfs)는 설계 선택이다.

---

## 9. 원격 쪽 스택 — OpenAMP · libmetal, RPMsg-Lite, Zephyr

이 절의 API 이름은 **기억 기반**이다(이 랩에서 원격 쪽 라이브러리를 빌드하지 않았다). 실제 사용 전 각 프로젝트 문서와 버전을 확인할 것. 개념 대응은 이 노트의 가짜 원격 코드로 검증한 것이다.

### 9.1 세 가지 선택지

| 스택 | 무엇 | 원격에서 하는 일 (= 이 노트의 가짜 원격 함수) | 주로 쓰는 곳 |
|---|---|---|---|
| OpenAMP (open-amp + libmetal) | Linux Foundation 산하 프로젝트. remoteproc · virtio · rpmsg를 원격(과 Linux 유저 공간)에 구현. libmetal이 OS 추상화(공유 메모리 I/O region, IRQ, 락) | 표 정의(`rsc_table`), vdev status 대기(`o2_vrings_up`), `rpmsg_create_ept` + NS 광고(`o2_announce`), `rpmsg_send`(`o2_send`), 수신 콜백(`o2_remote_work`) | STM32Cube MPU 패키지, Xilinx/AMD, TI MCU+ SDK 일부, Zephyr 모듈 |
| RPMsg-Lite | NXP가 만든 경량 구현. 같은 wire 형식(rpmsg 헤더 + vring + NS) | `rpmsg_lite_remote_init` → `rpmsg_lite_is_link_up` 대기 → `rpmsg_lite_create_ept` → `rpmsg_ns_announce` → `rpmsg_lite_send` | NXP MCUXpresso SDK (i.MX8M/9의 M코어 예제) |
| Zephyr | RTOS. OpenAMP를 모듈로 포함하고, 그 위에 `ipc_service` 추상화 | 샘플 `samples/subsys/ipc/openamp_rsc_table`이 resource table을 갖고 Linux remoteproc와 붙는 예 | 벤더 중립 RTOS를 원하는 제품 |

### 9.2 OpenAMP 원격 앱의 뼈대 (가이드 — 실행 안 함)

아래는 흐름을 보여 주는 의사 코드다. 함수 이름은 OpenAMP 공개 API를 기억으로 적은 것이라 인자는 생략했다.

```text
main():
    metal_init()                                 # libmetal 초기화
    shm_io = metal_io_init(공유 메모리 영역)       # vring·버퍼가 있는 DDR/SRAM을 I/O region으로
    vdev  = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE, ..., &resource_table.vdev, ...)
    rproc_virtio_wait_remote_ready(vdev)         # vdev status에 DRIVER_OK가 설 때까지 (= o2_vrings_up)
    rpmsg_init_vdev(&rpvdev, vdev, ns_bind_cb, shm_io, ...)
    rpmsg_create_ept(&ept, rdev, "rpmsg-raw", ADDR_ANY, ADDR_ANY, rx_cb, unbind_cb)
                                                 # NS 기능이 협상됐으면 이 때 NS 광고가 나간다
    loop:
        IPI(mailbox) 인터럽트 대기 → rproc_virtio_notified() → rx_cb(ept, data, len, src)
rx_cb(): 명령 해석 → rpmsg_send(&ept, reply, len)   # 보내고 mailbox로 Linux에 kick
```

이 노트의 가짜 원격과 비교하면 같은 일을 한다: 표에서 vring 위치를 찾고, DRIVER_OK를 기다리고, NS로 이름을 알리고, kick마다 avail을 비우고 used를 채운다. OpenAMP는 여기에 버퍼 관리(Linux와 같은 512 B 고정 크기가 기본), 원격 쪽 rpmsg 주소 할당, 여러 endpoint 다중화를 얹는다.

### 9.3 원격 쪽 링커 · 메모리 체크리스트 (실무에서 확인할 것)

- `.resource_table`은 LMA = VMA, startup 복사 대상(`.data`)과 분리 (§2.8).
- 표 · vring · 버퍼 주소가 **양쪽에서 같은 DDR**을 가리키는가 — Linux 쪽 DT `reserved-memory`(`vdev0vring0`, `vdev0vring1`, `vdev0buffer`)와 원격 쪽 링커/설정의 주소가 같아야 한다. 이게 어긋나면 Linux는 정상인데 원격이 엉뚱한 메모리를 읽는다.
- 원격 쪽 MPU/캐시: 공유 영역을 non-cacheable로 두거나, 쓰기 후 clean · 읽기 전 invalidate (Cortex-M7은 D-cache가 있다).
- 원격의 주소 변환: 같은 DDR이 M코어 주소 맵에서 다른 주소로 보이는 SoC가 있다(별칭 영역). OpenAMP에서는 libmetal I/O region의 physmap이 이 번역을 맡는다(기억 기반).

---

## 10. 실물 벤더 드라이버 읽기 — i.MX, STM32MP1, TI K3

### 10.1 ops 비교 (6.18.55 소스에서 확인)

```sh
cd /var/lab/src/linux-6.18.55/drivers/remoteproc
for f in imx_rproc.c stm32_rproc.c ti_k3_r5_remoteproc.c ti_k3_m4_remoteproc.c; do
  echo "## $f"; grep -n "^static const struct rproc_ops" -A 16 $f | grep -E "ops|\.[a-z_]+\s*=" | head -18
  grep -n "compatible = " $f | head -8
done
```

```text
## imx_rproc.c
621:static const struct rproc_ops imx_rproc_ops = {
622-	.prepare	= imx_rproc_prepare,
623-	.attach		= imx_rproc_attach,
624-	.detach		= imx_rproc_detach,
625-	.start		= imx_rproc_start,
626-	.stop		= imx_rproc_stop,
627-	.kick		= imx_rproc_kick,
628-	.da_to_va       = imx_rproc_da_to_va,
629-	.load		= rproc_elf_load_segments,
630-	.parse_fw	= imx_rproc_parse_fw,
631-	.find_loaded_rsc_table = imx_rproc_elf_find_loaded_rsc_table,
632-	.get_loaded_rsc_table = imx_rproc_get_loaded_rsc_table,
633-	.sanity_check	= rproc_elf_sanity_check,
634-	.get_boot_addr	= rproc_elf_get_boot_addr,
1291:	{ .compatible = "fsl,imx7ulp-cm4", .data = &imx_rproc_cfg_imx7ulp },
1292:	{ .compatible = "fsl,imx7d-cm4", .data = &imx_rproc_cfg_imx7d },
1293:	{ .compatible = "fsl,imx6sx-cm4", .data = &imx_rproc_cfg_imx6sx },
1294:	{ .compatible = "fsl,imx8mq-cm4", .data = &imx_rproc_cfg_imx8mq },
1295:	{ .compatible = "fsl,imx8mm-cm4", .data = &imx_rproc_cfg_imx8mq },
1296:	{ .compatible = "fsl,imx8mn-cm7", .data = &imx_rproc_cfg_imx8mn },
1297:	{ .compatible = "fsl,imx8mp-cm7", .data = &imx_rproc_cfg_imx8mn },
1298:	{ .compatible = "fsl,imx8mn-cm7-mmio", .data = &imx_rproc_cfg_imx8mn_mmio },
## stm32_rproc.c
663:static const struct rproc_ops st_rproc_ops = {
664-	.prepare	= stm32_rproc_prepare,
665-	.start		= stm32_rproc_start,
666-	.stop		= stm32_rproc_stop,
667-	.attach		= stm32_rproc_attach,
668-	.detach		= stm32_rproc_detach,
669-	.kick		= stm32_rproc_kick,
670-	.load		= rproc_elf_load_segments,
671-	.parse_fw	= stm32_rproc_parse_fw,
672-	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
673-	.get_loaded_rsc_table = stm32_rproc_get_loaded_rsc_table,
674-	.sanity_check	= rproc_elf_sanity_check,
675-	.get_boot_addr	= rproc_elf_get_boot_addr,
679-	{ .compatible = "st,stm32mp1-m4" },
679:	{ .compatible = "st,stm32mp1-m4" },
## ti_k3_r5_remoteproc.c
598:static const struct rproc_ops k3_r5_rproc_ops = {
599-	.prepare	= k3_r5_rproc_prepare,
600-	.unprepare	= k3_r5_rproc_unprepare,
601-	.start		= k3_r5_rproc_start,
602-	.stop		= k3_r5_rproc_stop,
603-	.kick		= k3_rproc_kick,
604-	.da_to_va	= k3_r5_rproc_da_to_va,
1467:	{ .compatible = "ti,am654-r5fss", .data = &am65_j721e_soc_data, },
1468:	{ .compatible = "ti,j721e-r5fss", .data = &am65_j721e_soc_data, },
1469:	{ .compatible = "ti,j7200-r5fss", .data = &j7200_j721s2_soc_data, },
1470:	{ .compatible = "ti,am64-r5fss",  .data = &am64_soc_data, },
1471:	{ .compatible = "ti,am62-r5fss",  .data = &am62_soc_data, },
1472:	{ .compatible = "ti,j721s2-r5fss",  .data = &j7200_j721s2_soc_data, },
## ti_k3_m4_remoteproc.c
24:static const struct rproc_ops k3_m4_rproc_ops = {
25-	.prepare = k3_rproc_prepare,
26-	.unprepare = k3_rproc_unprepare,
27-	.start = k3_rproc_start,
28-	.stop = k3_rproc_stop,
29-	.attach = k3_rproc_attach,
30-	.detach = k3_rproc_detach,
31-	.kick = k3_rproc_kick,
32-	.da_to_va = k3_rproc_da_to_va,
33-	.get_loaded_rsc_table = k3_get_loaded_rsc_table,
140:	{ .compatible = "ti,am64-m4fss", .data = &am64_m4_data, },
```

출력에서 볼 것:

- i.MX와 STM32는 attach/detach와 `get_loaded_rsc_table`을 다 갖고 있다 — 부트로더가 띄운 M코어에 붙는 시나리오를 mainline이 지원한다는 뜻이다. 둘 다 ELF 함수를 명시해 `parse_fw`를 지킨다(§3.3).
- i.MX는 `fsl,imx8mp-cm7` 등 칩마다 compatible이 있고 `.data`(cfg)로 주소 변환 표를 바꾼다. 같은 파일에 `fsl,imx8qm-cm4`, `fsl,imx8ulp-cm33`, `fsl,imx93-cm33`도 있다(줄 1301~1303).
- TI K3 R5F 드라이버(AM62/AM64/J721E)의 정적 ops는 위 출력의 6개가 전부다(원본 확인: 598~605행에서 구조체가 닫힌다). `.load`가 없으니 ELF 함수는 core 기본값이 채운다(§1.3). 대신 attach는 probe 중에 **하드웨어 상태를 보고 ops를 바꿔 끼우는** 방식이다(같은 파일 870~893행):

```c
	if (c_state && !reset_ctrl_status && !halted) {
		dev_info(cdev, "configured R5F for IPC-only mode\n");
		kproc->rproc->state = RPROC_DETACHED;
		ret = 1;
		/* override rproc ops with only required IPC-only mode ops */
		kproc->rproc->ops->prepare = NULL;
		kproc->rproc->ops->unprepare = NULL;
		kproc->rproc->ops->start = NULL;
		kproc->rproc->ops->stop = NULL;
		kproc->rproc->ops->attach = k3_rproc_attach;
		kproc->rproc->ops->detach = k3_rproc_detach;
		kproc->rproc->ops->get_loaded_rsc_table =
						k3_get_loaded_rsc_table;
```

리셋이 풀려 있고 코어가 halt가 아니면 "부트로더가 이미 띄운 코어"(TI는 보통 SPL/U-Boot 단계 — 기억 기반)로 보고 `IPC-only mode`(= 이 노트의 attach 모드)로 간다. `start`/`stop`을 NULL로 만들어 Linux가 코어를 리셋하지 못하게 한다는 점이 §8.2 끝의 "안전 코어는 stop을 막는다"를 실제로 구현한 예다. `ops`를 이렇게 고칠 수 있는 것은 `rproc_alloc()`이 ops를 `kmemdup`으로 복사해 두기 때문이다(§1.3). 같은 파일의 `k3_r5_lockstep_reset()`/`k3_r5_split_release()`는 R5F 두 개를 lockstep(안전용 이중화) 또는 split(독립 코어 두 개)으로 쓰는 모드 처리다.

### 10.2 kick과 수신 경로 — 세 벤더의 실제 코드

i.MX — mailbox 콜백은 work만 큐잉, work에서 모든 notifyid에 대해 `rproc_vq_interrupt()`:

```c
static int imx_rproc_notified_idr_cb(int id, void *ptr, void *data)
{
	struct rproc *rproc = data;

	rproc_vq_interrupt(rproc, id);

	return 0;
}

static void imx_rproc_vq_work(struct work_struct *work)
{
	struct imx_rproc *priv = container_of(work, struct imx_rproc,
					      rproc_work);
	struct rproc *rproc = priv->rproc;

	idr_for_each(&rproc->notifyids, imx_rproc_notified_idr_cb, rproc);
}

static void imx_rproc_rx_callback(struct mbox_client *cl, void *msg)
{
	struct rproc *rproc = dev_get_drvdata(cl->dev);
	struct imx_rproc *priv = rproc->priv;

	queue_work(priv->workqueue, &priv->rproc_work);
}
```

STM32 — vq별 mailbox 채널, work에서 `rproc->lock`을 잡고 상태 확인 후 처리. watchdog 인터럽트는 곧바로 crash 보고:

```c
static irqreturn_t stm32_rproc_wdg(int irq, void *data)
{
	struct platform_device *pdev = data;
	struct rproc *rproc = platform_get_drvdata(pdev);

	rproc_report_crash(rproc, RPROC_WATCHDOG);

	return IRQ_HANDLED;
}

static void stm32_rproc_mb_vq_work(struct work_struct *work)
{
	struct stm32_mbox *mb = container_of(work, struct stm32_mbox, vq_work);
	struct rproc *rproc = dev_get_drvdata(mb->client.dev);

	mutex_lock(&rproc->lock);

	if (rproc->state != RPROC_RUNNING && rproc->state != RPROC_ATTACHED)
		goto unlock_mutex;

	if (rproc_vq_interrupt(rproc, mb->vq_id) == IRQ_NONE)
		dev_dbg(&rproc->dev, "no message found in vq%d\n", mb->vq_id);

unlock_mutex:
	mutex_unlock(&rproc->lock);
}
```

TI K3 — mailbox payload 자체가 vqid, 특수 값(crash, echo)은 따로 처리. kick은 vqid를 mailbox에 쓴다:

```c
	switch (msg) {
	case RP_MBOX_CRASH:
		/*
		 * remoteproc detected an exception, but error recovery is not
		 * supported. So, just log this for now
		 */
		dev_err(dev, "K3 rproc %s crashed\n", rproc->name);
		break;
	...
	default:
		...
		/* msg contains the index of the triggered vring */
		if (rproc_vq_interrupt(rproc, msg) == IRQ_NONE)
			dev_dbg(dev, "no message was found in vqid %d\n", msg);
	}
...
void k3_rproc_kick(struct rproc *rproc, int vqid)
{
	...
	ret = mbox_send_message(kproc->mbox, (void *)(uintptr_t)msg);
```

세 드라이버 모두 **mailbox 콜백(원자 문맥)에서 rpmsg 처리를 직접 하지 않고** work로 미루거나(i.MX, STM32) mailbox 프레임워크의 콜백 문맥에 의존한다(K3 — mailbox 드라이버가 어느 문맥에서 콜백을 부르는지에 달려 있다, H10 §4). 우리 가짜 원격이 work 안에서 `rproc->lock`을 잡은 것은 STM32를 따라 한 것이다.

### 10.3 실물 Device Tree — STM32MP15 DK 보드

`arch/arm/boot/dts/st/stm32mp15xx-dkx.dtsi`(6.18.55 트리, 발췌):

```dts
		mcuram2: mcuram2@10000000 {
			compatible = "shared-dma-pool";
			reg = <0x10000000 0x40000>;
			no-map;
		};
		vdev0vring0: vdev0vring0@10040000 {
			compatible = "shared-dma-pool";
			reg = <0x10040000 0x2000>;
			no-map;
		};
		vdev0vring1: vdev0vring1@10042000 {
			compatible = "shared-dma-pool";
			reg = <0x10042000 0x2000>;
			no-map;
		};
		vdev0buffer: vdev0buffer@10044000 {
			compatible = "shared-dma-pool";
			reg = <0x10044000 0x4000>;
			no-map;
		};
...
&m4_rproc {
	memory-region = <&retram>, <&mcuram>, <&mcuram2>, <&vdev0vring0>,
			<&vdev0vring1>, <&vdev0buffer>;
	mboxes = <&ipcc 0>, <&ipcc 1>, <&ipcc 2>, <&ipcc 3>;
	mbox-names = "vq0", "vq1", "shutdown", "detach";
	pinctrl-names = "default";
	pinctrl-0 = <&m4_leds_orange_pins_a>;
	interrupt-parent = <&exti>;
	interrupts = <68 1>;
	status = "okay";
};
```

이 노트의 랩과 대응시키면:

- `vdev0vring0`/`vdev0vring1` — 우리 dmesg의 carveout 이름과 **정확히 같은 이름**이다. core의 `rproc_alloc_vring()`이 `rproc_find_carveout_by_name(rproc, "vdev%dvring%d", ...)`로 먼저 찾고, 있으면 DT 영역을 쓰고 없으면(우리 랩) `dma_alloc_coherent()`로 잡는다. 크기 `0x2000`도 우리 랩과 같다(16칸 x align 4096).
- `vdev0buffer` 16 KB — rpmsg 버퍼 풀(우리 랩의 32 x 512 B = 16 KB와 같은 크기). `remoteproc_virtio.c`가 이 이름의 carveout을 찾아 virtio 장치에 `dma_declare_coherent_memory()`(또는 `of_reserved_mem_device_init_by_idx()`)로 붙인다. 그래야 rpmsg 버퍼가 **M4가 볼 수 있는 SRAM**에 잡힌다. 우리 랩에는 이 영역이 없어서 버퍼가 일반 DDR(CMA)에서 나왔다.
- `no-map` — Linux 선형 매핑에서 빼서 캐시 속성 충돌을 피한다(F10).
- `mbox-names` 4개 — vq0/vq1 kick, 그리고 shutdown · detach 신호(H10 §9.2에서 타임아웃 값까지 봤다).
- `interrupts = <68 1>` — 바인딩 문서 `st,stm32-rproc.yaml`: "Should contain the WWDG1 watchdog reset interrupt". 위의 `stm32_rproc_wdg()`로 연결되어 crash → recovery가 된다.

---

## 11. 커널 안에서는 무슨 일이 일어나나 — 데이터 경로 한 장

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="20" text-anchor="middle" font-size="13">송신 (Linux → 원격)</text>
  <text x="510" y="20" text-anchor="middle" font-size="13">수신 (원격 → Linux)</text>
  <rect x="30" y="32" width="280" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="53" text-anchor="middle" font-size="12">write(fd) — 유저</text>
  <rect x="30" y="78" width="280" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="99" text-anchor="middle" font-size="12">rpmsg_eptdev_write_iter (rpmsg_char)</text>
  <rect x="30" y="124" width="280" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="145" text-anchor="middle" font-size="12">rpmsg_sendto → virtio_rpmsg_sendto</text>
  <rect x="30" y="170" width="280" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="191" text-anchor="middle" font-size="12">rpmsg_send_offchannel_raw</text>
  <rect x="30" y="216" width="280" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="237" text-anchor="middle" font-size="12">virtqueue_add_outbuf + virtqueue_kick</text>
  <rect x="30" y="262" width="280" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="283" text-anchor="middle" font-size="12">rproc_virtio_notify → ops->kick</text>
  <rect x="30" y="308" width="280" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="329" text-anchor="middle" font-size="12">mbox_send_message (H10)</text>
  <rect x="370" y="308" width="280" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="329" text-anchor="middle" font-size="12">mailbox IRQ → rx_callback</text>
  <rect x="370" y="262" width="280" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="283" text-anchor="middle" font-size="12">work → rproc_vq_interrupt(notifyid)</text>
  <rect x="370" y="216" width="280" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="237" text-anchor="middle" font-size="12">vring_interrupt → rpmsg_recv_done</text>
  <rect x="370" y="170" width="280" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="191" text-anchor="middle" font-size="12">rpmsg_recv_single: idr_find(dst)</text>
  <rect x="370" y="124" width="280" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="145" text-anchor="middle" font-size="12">ept->cb = rpmsg_ept_cb → skb 큐</text>
  <rect x="370" y="78" width="280" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="99" text-anchor="middle" font-size="12">wake_up → read()가 skb 하나 복사</text>
  <rect x="370" y="32" width="280" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="53" text-anchor="middle" font-size="12">read(fd) — 유저</text>
  <line x1="170" y1="64" x2="170" y2="76" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="170" y1="110" x2="170" y2="122" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="170" y1="156" x2="170" y2="168" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="170" y1="202" x2="170" y2="214" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="170" y1="248" x2="170" y2="260" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="170" y1="294" x2="170" y2="306" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="306" x2="510" y2="296" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="260" x2="510" y2="250" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="214" x2="510" y2="204" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="168" x2="510" y2="158" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="122" x2="510" y2="112" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="76" x2="510" y2="66" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="312" y1="324" x2="368" y2="324" stroke="#d0564a" stroke-dasharray="5 3" marker-end="url(#ah7)"/>
  <text x="340" y="362" text-anchor="middle" font-size="12">원격이 처리하고 응답 (OpenAMP)</text>
  <rect x="30" y="380" width="620" height="76" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="44" y="402" font-size="12">드라이버 작성 때 다시 만나는 곳: rpmsg_driver (G2·G3 버스 매칭),</text>
  <text x="44" y="422" font-size="12">rproc_ops (G-모듈), mbox_client (H10), reserved-memory (F10), G10,</text>
  <text x="44" y="442" font-size="12">work로 미루기 (F7), mutex 문맥 (F4), sysfs/debugfs/cdev (G8, M)</text>
</svg>
```

그림 8 — `/dev/rpmsgN`의 write/read가 커널에서 지나가는 함수들(이름은 6.18.55 `rpmsg_char.c`, `virtio_rpmsg_bus.c`, `virtio_ring.c`, `remoteproc_virtio.c`에서 확인). 수신 쪽 `rpmsg_recv_done()`은 처리한 버퍼를 다시 avail에 걸고 `virtqueue_kick(rvq)`까지 한다 — 예제 9에서 kick 수가 메시지 수의 약 2배였던 이유.

요점을 구조체로 정리한다.

| 구조체 | 정의 위치 | 역할 | 이 노트에서 |
|---|---|---|---|
| `struct rproc` | `include/linux/remoteproc.h` | 원격 코어 하나: 상태, 펌웨어 이름, 표 포인터(`table_ptr`, `cached_table`), carveout 목록, `notifyids` idr, `lock` | 가짜 원격이 `table_ptr`과 `lock`을 직접 씀 |
| `struct rproc_ops` | 〃 | 벤더 콜백 표 | `o2_ops`, `o2_attach_ops` |
| `struct rproc_mem_entry` | 〃 | carveout 한 개 (va, dma, da, len, 이름) | debugfs `carveout_memories` |
| `struct rproc_vdev`, `struct rproc_vring` | 〃 | vdev 하나, vring 하나(va, da, num, align, notifyid) | `rproc-virtio.1.auto` 장치 |
| `struct virtproc_info` | `virtio_rpmsg_bus.c` 내부 | rvq/svq, 버퍼 풀, endpoints idr, `tx_lock`, `sendq` | 버퍼 32개 x 512 B |
| `struct rpmsg_device`, `struct rpmsg_endpoint` | `include/linux/rpmsg.h` | 채널과 endpoint (src, dst, cb) | `/sys/bus/rpmsg/devices/*` |
| `struct rpmsg_driver` | 〃 | `id_table` 이름 매칭, `probe`, `callback` | `rpmsg_client_sample`, `rpmsg_chrdev` |

rpmsg 클라이언트 드라이버를 직접 쓸 때의 최소 형태는 `samples/rpmsg/rpmsg_client_sample.c`가 정답지다: `id_table`에 원격이 광고할 이름, `probe`에서 `rpmsg_send(rpdev->ept, ...)`, `callback`에서 수신 처리. 이 콜백은 위 그림의 `rpmsg_recv_single()`에서 `ept->cb_lock` mutex를 잡은 채 불리므로 **잘 수 있는(sleep 가능한) 문맥이지만 오래 붙잡으면 같은 vring의 다른 메시지가 전부 밀린다.** 무거운 일은 자기 work로 넘긴다.

---

## 12. Embedded 실무에서 다시 보기

### 12.1 로봇 관절 제어: M7 펌웨어 업데이트는 rootfs OTA에 묶는다

예를 들어 i.MX8M Plus(Cortex-A53 + Cortex-M7 — `fsl,imx8mp-cm7` compatible이 트리에 있다) 기반 로봇 팔이라면, M7이 1 kHz 관절 보간 + CAN 모터 드라이버를 맡고 Linux가 경로 계획 · 카메라를 맡는다. M7 펌웨어는 `/lib/firmware/robot_m7.elf`로 rootfs에 들어가므로 **L4의 A/B rootfs 업데이트와 원자적으로 같이 바뀐다**(N4 §9). 업데이트 후 재부팅에서 버전 불일치가 생기지 않는다. 다만 attach 모드(부트로더가 먼저 띄움)로 가면 부트로더가 읽는 펌웨어 사본도 같은 슬롯에 있어야 한다 — 이걸 놓치면 "Linux는 새 버전, M7은 옛 버전" 상태로 attach한다. 앱은 첫 rpmsg 메시지로 버전 핸드셰이크를 해야 한다.

### 12.2 sysfs 한 줄로 끝나는 운영 스크립트

```sh
# 펌웨어 교체 (offline일 때만 가능 — running이면 "can't change firmware while running", -EBUSY)
R=/sys/class/remoteproc/remoteproc0
[ "$(cat $R/state)" = running ] && echo stop > $R/state
echo robot_m7_v2.elf > $R/firmware
echo start > $R/state
# crash를 붙잡아 두고 분석할 때
echo disabled > /sys/kernel/debug/remoteproc/remoteproc0/recovery
echo enabled  > /sys/kernel/debug/remoteproc/remoteproc0/coredump
```

(이 블록은 실행 예가 아니라 운영 패턴 요약이다. 각 줄의 동작은 예제 8, 13에서 개별로 확인했다. `-EBUSY` 문구는 `rproc_set_firmware()` 소스의 `dev_err(dev, "can't change firmware while running\n")`.)

### 12.3 "M코어가 죽었다"와 "Linux가 죽었다"는 비대칭이다

remoteproc는 **원격이 죽은 것**만 다룬다(watchdog IRQ → `rproc_report_crash` → recovery). **Linux가 죽은 것**은 원격이 스스로 알아내야 한다 — rpmsg 위에 heartbeat를 두고, 끊기면 모터를 safe state로(N4 §8). 또 recovery가 돌면 rpmsg 채널이 지워졌다 다시 생기므로(예제 13), Linux 쪽 데몬은 `/dev/rpmsgN`이 사라졌다 다시 생기는 것을 견뎌야 한다(udev 이벤트 감시 또는 재시도).

### 12.4 큰 데이터는 rpmsg가 아니라 carveout으로

센서 키트에서 M4가 IMU 샘플을 1 kHz로 모은다면, 샘플 하나하나를 rpmsg 메시지로 보내면 메시지당 kick 2회 + Linux 쪽 work/스케줄링 비용이 붙는다(예제 10의 RTT 꼬리). 대신 resource table에 carveout(우리 `o2_shm` 64 KB처럼)을 요청해 거기에 링버퍼를 두고, rpmsg로는 "인덱스 N까지 찼다"만 10~100 Hz로 알린다. Linux 쪽은 carveout을 유저에 노출해야 하는데, 이건 별도 드라이버(예: `mmap` 제공 문자 장치, 또는 UIO)의 몫이다 — remoteproc 자체는 carveout을 유저에 mmap해 주지 않는다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| ops에 `.parse_fw`만 쓰고 `.load`는 비움 | 내 `parse_fw`가 안 불림, coredump 세그먼트가 비어 devcoredump가 안 생김 (이 랩에서 실제로 겪음) | `rproc_alloc_ops()`가 `.load` NULL이면 ELF 기본값 5개를 일괄 대입 | `.load = rproc_elf_load_segments`와 나머지 ELF 함수를 명시 (stm32/imx처럼) |
| `.resource_table`을 `.data`와 같은 `AT > FLASH`에 둠 | Linux는 정상 부팅, 원격은 vring 주소를 ANY(0xFFFFFFFF)로 읽고 멈춤 | loader는 `p_paddr`(LMA)에 복사, Linux는 `sh_addr`(VMA)의 표를 갱신, startup이 VMA를 원본으로 덮어씀 | 표를 별도 섹션으로, LMA = VMA (`AT > SRAM`), `readelf -l`로 VirtAddr = PhysAddr 확인 (예제 6) |
| 펌웨어에 `RSC_DEVMEM` 엔트리, IOMMU 없는 SoC | `Failed to process resources: -22`로 부팅 실패 | `rproc_handle_devmem()`이 `rproc->domain` 없으면 `-EINVAL` | devmem 엔트리 제거 (M코어가 주변장치를 직접 보는 SoC) |
| rootfs에 `/lib/firmware` 없음 또는 이름 오타 | `Direct firmware load for X failed with error -2`, `Boot failed: -2` (예제 12에서 실제로 봄) | `request_firmware()` 경로 | 이미지 레시피에 펌웨어 설치, `cat .../firmware`로 이름 확인 |
| 원격이 DRIVER_OK 전에 vring 사용 | 첫 메시지 유실, 간헐적으로 NS 광고가 안 보임 | Linux virtio가 링 초기화 때 `memset(addr, 0, size)` | 원격은 vdev status bit 2를 폴링한 뒤 vring 초기화 |
| `echo start` 직후 `/dev/rpmsg_ctrl0` 사용 | `No such file or directory` (예제 12 앞선 실행) | udev가 virtio_rpmsg_bus · rpmsg_ctrl을 로드하는 데 시간이 걸림 (TCG에서 약 5 s) | `udevadm settle`, systemd 장치 의존성, 재시도 루프 |
| `/dev/rpmsgN`을 읽기용 · 쓰기용으로 두 번 open | `Device or resource busy` (예제 11) | `rpmsg_eptdev_open()`이 endpoint 하나만 허용 | fd 하나 `O_RDWR` + `poll()`, 또는 endpoint를 여러 개 생성 |
| 496 B 넘는 메시지 write | `write` 실패 `-EMSGSIZE`, dmesg `message is too big` | 고정 512 B 버퍼 - 헤더 16 B | 큰 데이터는 carveout + rpmsg로 주소만 |
| 공유 영역을 cacheable로 매핑 (Cortex-M7 D-cache, A쪽 캐시) | QEMU에선 멀쩡, 실물에서 가끔 쓰레기 값/오래된 값 | 캐시 일관성 없음 (QEMU TCG는 캐시를 모델링 안 함) | DT `no-map` reserved-memory, 원격 MPU에서 non-cacheable, 또는 clean/invalidate |
| running 중 `echo new.elf > firmware` | `-EBUSY`, `can't change firmware while running` | `rproc_set_firmware()`는 offline에서만 허용 | stop → firmware 변경 → start (§12.2) |
| recovery 후 앱이 옛 fd를 계속 씀 | read/write 에러, 메시지 안 옴 | recovery가 rpmsg 채널을 지우고 다시 만듦 (예제 13) | 에러 시 장치를 다시 찾아 열기, NS 재광고를 기다리기 |

---

## 14. 면접에서 이렇게 말한다

**Q.** "How does Linux load and start firmware on the Cortex-M core of an i.MX8 or STM32MP1?"

**A.** remoteproc 프레임워크다. 벤더 드라이버가 `rproc_ops`(start/stop/kick/da_to_va)를 채워 `rproc_add`로 등록하면, `echo start > /sys/class/remoteproc/remoteproc0/state`나 auto_boot로 `rproc_boot`이 불린다. `request_firmware`로 ELF를 읽고, resource table을 파싱해 carveout · vring · trace를 준비하고, `PT_LOAD` 세그먼트를 `da_to_va`로 번역한 주소에 복사한 뒤, 표를 원격 메모리에 다시 써 넣고 `ops->start`로 리셋을 푼다. 부트로더가 먼저 띄운 코어면 attach 모드로 리셋 없이 붙는다.

> It's the remoteproc framework. The SoC driver fills in rproc_ops — start, stop, kick, da_to_va — and registers with rproc_add. On boot, either auto-boot or "echo start" to the sysfs state file, the core calls request_firmware, parses the ELF's resource table, allocates the carveouts and vrings it asks for, copies the PT_LOAD segments through da_to_va, writes the filled-in table back into the remote's memory, and finally calls ops->start to release the core from reset. If the bootloader already started the core — for example because it owns motor outputs from power-on — the driver marks it detached and Linux attaches without touching reset.

**꼬리 질문**: "What's da_to_va for?" → 원격 코어가 보는 주소(TCM 0x0 등)와 Linux가 보는 주소가 다르니 그 번역. 범위 밖이면 NULL을 돌려줘서 core가 carveout 목록을 찾게 한다.

**Q.** "What is the resource table and who writes which fields?"

**A.** 펌웨어 ELF의 `.resource_table` 섹션에 있는 표로, 원격이 필요로 하는 자원 목록이다: carveout(메모리), trace(로그 버퍼), vdev(rpmsg용 virtio 장치와 vring 크기), devmem(IOMMU 매핑). 펌웨어는 모르는 주소를 ANY로 두고, Linux가 메모리를 잡은 뒤 vring 주소 · notifyid · carveout 주소 · virtio status를 채운다. 원격은 부팅 후 자기 표를 읽어 vring 위치를 알고, status에 DRIVER_OK가 서면 통신을 시작한다. 링커 스크립트에서 이 섹션의 LMA와 VMA가 같아야 한다.

> It's a section in the firmware ELF that lists what the remote needs: carveouts, a trace buffer, a virtio device for rpmsg with its vring sizes, and optionally IOMMU mappings. The firmware leaves unknown addresses as ANY; Linux allocates the memory and writes the vring addresses, notify IDs, carveout addresses and the virtio status back into the copy that lives in the remote's memory. The remote reads its own table after boot and waits for DRIVER_OK. One classic bug is linking that section with a load address different from its run address — then startup code overwrites what Linux wrote.

**Q.** "Walk me through what happens when user space writes to /dev/rpmsg0."

**A.** `rpmsg_char`가 `rpmsg_sendto`로 넘기고, virtio_rpmsg_bus가 TX 풀에서 512 B 버퍼를 하나 꺼내 16 B 헤더(src, dst, len)와 데이터를 쓰고, vring1의 avail에 넣은 뒤 `virtqueue_kick` → `rproc_virtio_notify` → `ops->kick` → mailbox doorbell. 원격은 IRQ에서 avail을 비우고 처리한 버퍼를 used로 돌려준다. 응답은 반대 방향 vring0로 와서, mailbox IRQ → work → `rproc_vq_interrupt` → `rpmsg_recv_done` → dst 주소로 endpoint를 찾아 콜백 → skb 큐 → `read()`. 데이터는 공유 메모리, 알림만 mailbox다.

> rpmsg_char hands it to rpmsg_sendto; virtio_rpmsg_bus takes a free 512-byte TX buffer, writes a 16-byte header with source, destination and length plus the payload, adds it to the TX vring's available ring and kicks — which ends up in rproc_ops.kick and a mailbox doorbell. The remote drains the available ring and returns the buffer on the used ring. The reply comes back on the RX vring: mailbox interrupt, a work item calls rproc_vq_interrupt, rpmsg_recv_done looks up the endpoint by destination address and runs its callback, which queues an skb that read() returns. Payload goes through shared memory; the mailbox only carries the notification.

**Q.** "How does a new rpmsg channel appear on the Linux side?"

**A.** name service다. vdev의 feature bit 0(NS)이 협상되면 Linux가 주소 53에 NS endpoint를 둔다. 원격이 `{name, addr, flags=CREATE}` 40 B 메시지를 53번으로 보내면 `rpmsg_ns`가 그 이름으로 rpmsg 장치를 만들고, 이름이 `id_table`과 맞는 `rpmsg_driver`가 probe된다. `rpmsg-raw`면 `rpmsg_char`가 `/dev/rpmsgN`을 만든다. NS 없이 유저가 `/dev/rpmsg_ctrl0`에 `RPMSG_CREATE_EPT_IOCTL`로 endpoint를 직접 만들 수도 있다.

> Through the name service. If the NS feature bit is negotiated, Linux listens on address 53. The remote sends a 40-byte announcement — name, its address, create flag — and rpmsg_ns registers an rpmsg device with that name; any rpmsg_driver whose id_table matches is probed, the way a platform driver matches a compatible string. "rpmsg-raw" gets picked up by rpmsg_char and becomes /dev/rpmsgN. Without NS, user space can create an endpoint itself through the RPMSG_CREATE_EPT ioctl on /dev/rpmsg_ctrl0.

**Q.** "The M4 crashes in the field. What does Linux do, and how do you debug it?"

**A.** 드라이버가 watchdog IRQ 등으로 `rproc_report_crash`를 부르면 recovery work가 상태를 crashed로 바꾸고, `rproc_stop` → coredump(enabled면 devcoredump로 ELF core) → 펌웨어 재로드 → `rproc_start`. 표 해석과 메모리 할당은 유지된다. rpmsg 채널은 지워졌다 다시 생기니 앱은 재연결해야 한다. 디버그할 때는 debugfs에서 coredump를 켜고, 필요하면 recovery를 꺼서 crashed 상태를 붙잡아 JTAG로 붙는다. trace 버퍼는 재로드 때 덮어써지므로 crash 직전 로그는 coredump로 남긴다. 반대로 Linux가 죽는 것은 remoteproc가 처리하지 않으니 원격에 heartbeat 감시가 따로 필요하다.

> The driver reports it — STM32 wires the M4 watchdog interrupt straight to rproc_report_crash. The recovery work marks it crashed, stops it, takes a coredump if enabled — that shows up as an ELF core under devcoredump — reloads the firmware and starts it again, keeping the existing carveouts and vrings. rpmsg channels get torn down and re-announced, so user-space daemons must reconnect. For debugging I enable coredump in debugfs and, if I need the core frozen, disable recovery so it stays in "crashed" and attach JTAG. Note the trace buffer is overwritten on reload, so the dump is what preserves the last logs. And remoteproc only covers the remote dying — the M4 still needs its own heartbeat watchdog on Linux.

**Q.** "Why would you choose attach mode, and what are the risks?"

**A.** 원격이 Linux 부팅 전부터 출력을 쥐고 있어야 할 때(모터 PWM, CAN 웨이크업, 빠른 부팅 표시). 부트로더가 펌웨어를 올려 띄우고, Linux는 리셋 없이 원격 메모리의 표로 vdev만 붙인다. 위험은 버전 관리(부트로더가 읽는 펌웨어와 rootfs 펌웨어의 불일치), 그리고 Linux가 stop으로 코어를 내릴 수 있다는 것 — TI K3는 IPC-only 모드에서 start/stop op를 NULL로 바꿔 막는다. detach하면 채널이 사라지지만 코어는 계속 돈다.

> When the remote has to own outputs before Linux is up — motor PWM, CAN wake-up, early display. The bootloader loads and starts it; Linux finds it running, reads the resource table already in its memory and only sets up the virtio device, never touching reset. The risks are version skew between the image the bootloader used and the one in the rootfs, and Linux being able to stop a core that's doing something safety-relevant. TI's K3 R5 driver handles that by nulling start and stop in IPC-only mode. Detach drops the channels but leaves the core running.

**Q.** "How would you move a 2 MB camera buffer or a 1 kHz IMU stream between the cores?"

**A.** rpmsg 메시지는 버퍼가 512 B 고정이라 payload 496 B가 한계고, 메시지마다 kick 두 번과 Linux 쪽 스케줄링 비용이 붙는다. 그래서 대용량은 resource table의 carveout이나 DT reserved-memory에 링버퍼를 두고, rpmsg로는 "인덱스 N까지 썼다" 같은 디스크립터만 보낸다. 캐시 일관성(non-cacheable 또는 clean/invalidate)과 소유권 규칙(누가 head를 쓰고 누가 tail을 쓰는가)을 정한다. NVMe에서 데이터는 PRP로 가리키고 큐에는 명령만 넣는 것과 같은 원리다.

> Not through rpmsg payloads — buffers are a fixed 512 bytes, so 496 bytes of payload, and every message costs two kicks plus scheduling on the Linux side. I'd put a ring buffer in a carveout or reserved-memory region, define who owns head and tail, handle cache coherency with a non-cacheable mapping or explicit clean and invalidate, and use rpmsg only for small descriptors like "filled up to index N". Same idea as NVMe: the data lives in host memory and the queue only carries a pointer.

---

## 15. 직접 해보기

1. (개념) 예제 8과 예제 9의 `resource_table` 출력을 비교해 virtio_rpmsg_bus가 붙은 뒤에만 바뀌는 칸 세 가지를 쓰고, 각각을 바꾸는 커널 함수를 찾아라.
정답/힌트: vring `Device Address`(`rp_find_vq()`), `Status`(`rproc_virtio_set_status()`), `Guest features`(`rproc_virtio_finalize_features()`). `grep -n "rsc->" /var/lab/src/linux-6.18.55/drivers/remoteproc/remoteproc_virtio.c`.

2. (코드) 펌웨어 resource table에 두 번째 trace 엔트리(`o2_trace1`, 256 B, `.trace` 섹션의 다른 배열)를 추가하고, debugfs에 `trace1`이 생기는지 확인하라. `NUM_ENTRIES`와 `offset[]`을 같이 고쳐야 한다.
정답/힌트: 엔트리를 추가하고 offset을 빼먹으면 `rsc table is truncated` 또는 엉뚱한 type이 찍힌다. `rsc_dump.py`로 먼저 검사.

3. (코드) 가짜 원격에 세 번째 서비스 `rpmsg-tty`를 광고해 보라. 이 커널에서는 무엇이 생기고 무엇이 안 생기는가?
정답/힌트: 채널 장치는 생기지만 `CONFIG_RPMSG_TTY`가 꺼져 있어 드라이버가 안 붙는다(`driver=` 비어 있음). 이름 매칭이 드라이버 바인딩의 전부라는 것을 확인하는 실험.

4. (관찰) 예제 9에서 `echo "file virtio_rpmsg_bus.c +p" > /sys/kernel/debug/dynamic_debug/control`을 추가하고 `/rpmsg_ping 1`을 실행해 `TX From 0x..., To 0x401` 줄과 hex dump를 찾아라. 헤더 16 B를 직접 해석하라.
정답/힌트: `rpmsg_send_offchannel_raw()`의 `dev_dbg`와 `dynamic_hex_dump("rpmsg_virtio TX: ", ...)`. 앞 4 B = src, 다음 4 B = dst(0x401), 4 B reserved, 2 B len, 2 B flags.

5. (코드) `o2_send()`를 고쳐 원격 쪽 RX 버퍼가 없을 때(`-ENOBUFS`) 메시지를 버리지 말고 내부 큐에 쌓았다가 다음 kick에서 보내게 하라. 그런 다음 `rpmsg_ping 300`을 돌려 `rx`와 `tx` 카운터가 맞는지 확인하라.
정답/힌트: Linux가 버퍼를 다시 걸 때 `virtqueue_kick(rvq)`를 하므로(§11) 그 kick에서 큐를 비우면 된다. OpenAMP도 같은 이유로 송신 측 대기를 가진다.

6. (조사) `remoteproc_core.c`에서 `rproc_boot_recovery()`가 `rproc_fw_boot()`이 아니라 `rproc_start()`를 부른다. 그 결과 recovery 후 **바뀌지 않는** 것과 **바뀌는** 것을 각각 두 개씩 적어라.
정답/힌트: 유지 — carveout/vring 할당과 주소, `cached_table` 해석 결과(trace · vdev 엔트리). 바뀜 — 원격 메모리 내용(세그먼트 재로드로 trace 초기화), rpmsg 채널(virtio 장치 재등록). 예제 13의 trace 출력이 근거.

7. (설계) 로봇의 M7이 1 kHz로 관절 상태 64 B를 보내야 한다. rpmsg 메시지로 보낼 때와 carveout 링버퍼 + 10 Hz 알림으로 보낼 때, Linux 쪽 kick/인터럽트 횟수를 초당 기준으로 비교하라.
정답/힌트: 메시지 방식은 수신 1000건 + 버퍼 재등록 kick 1000건(§11). 링버퍼 방식은 알림 10건 + 재등록 10건. 대신 링버퍼는 캐시 · 소유권 · 오버플로 처리를 직접 짜야 한다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| remoteproc | remote processor framework | Linux가 같은 SoC의 다른 코어에 펌웨어를 올리고 시작 · 정지 · 복구하는 프레임워크 |
| rpmsg | remote processor messaging | virtio vring 위의 주소 기반 메시지 버스 (헤더 16 B + data, 버퍼 512 B) |
| OpenAMP | Open Asymmetric Multi Processing | 원격(RTOS/bare-metal) 쪽 remoteproc · virtio · rpmsg 구현 라이브러리 |
| libmetal | OpenAMP의 HAL | 공유 메모리 I/O region, IRQ, 락을 OS 독립적으로 감싼 계층 |
| RPMsg-Lite | NXP 경량 rpmsg | 같은 wire 형식의 작은 구현 (MCUXpresso SDK) |
| resource table | `.resource_table` 섹션 | 원격이 요청하는 자원 목록 + Linux가 채워 주는 주소 (계약서) |
| carveout | 연속 메모리 영역 | 원격 전용 또는 공유 메모리, DT reserved-memory 또는 `dma_alloc_coherent` |
| devmem | 장치 메모리 매핑 요청 | 원격의 IOMMU에 주변장치 영역 매핑 (IOMMU 없으면 실패) |
| trace | 원격 로그 버퍼 | debugfs `traceN`으로 읽는 원격 코어의 printf 버퍼 |
| vdev | virtio device 엔트리 | rpmsg용 virtio 장치(id 7)와 vring 목록 |
| vring | virtio ring | desc + avail + used로 된 공유 메모리 링, 버퍼 소유권을 인덱스로 교환 |
| kick | notify | "링에 새 것이 있다"는 알림 = `rproc_ops.kick` → mailbox doorbell |
| notifyid | 알림 번호 | vring마다 Linux가 배정하는 번호, kick/vq 인터럽트의 인자 |
| da | device address | 원격 코어가 보는 주소 (Linux 가상/물리 주소와 다름) |
| `da_to_va` | 주소 번역 op | 원격 주소 → 커널 가상 주소 |
| endpoint | rpmsg 끝점 | (로컬 주소, 콜백). Linux는 1024부터 배정 |
| name service (NS) | 주소 53 | 원격이 채널 이름을 광고하면 Linux가 rpmsg 장치를 만든다 |
| rpmsg_char | 유저 인터페이스 | `/dev/rpmsgN` (메시지 단위 read/write), `/dev/rpmsg_ctrlN` ioctl |
| attach / detach | 붙기 · 떨어지기 | 이미 떠 있는 원격 코어에 리셋 없이 연결 / 연결만 끊기 |
| recovery | 자동 복구 | crash 보고 → stop → coredump → 재로드 → start |
| devcoredump | 장치 덤프 프레임워크 | `/sys/class/devcoredump/devcdN/data`로 덤프를 내보내는 커널 기능 |
| DRIVER_OK | virtio status bit 2 | Linux 쪽 virtio 드라이버 준비 완료, 원격은 이걸 기다린다 |

---

## 17. 요약 & 체크리스트

remoteproc는 이종 SoC의 보조 코어를 Linux 장치처럼 다루는 프레임워크다. 벤더 드라이버는 `rproc_ops`(start · stop · kick · da_to_va, 필요하면 attach · detach · get_loaded_rsc_table)만 채우고, core가 `request_firmware` → ELF 검사 → resource table 해석(carveout · trace · vdev) → 메모리 할당 → 세그먼트 로드 → 표를 원격 메모리에 반영 → start를 한다. resource table은 펌웨어가 요구하고 Linux가 주소를 채워 주는 계약서이며, 링커에서 LMA = VMA여야 한다. 메시지는 vdev가 만든 virtio vring 두 개(Linux 기준 RX vring0, TX vring1) 위의 rpmsg로 가고, mailbox는 kick만 나른다. 원격은 DRIVER_OK를 기다린 뒤 name service로 채널을 광고하고, Linux는 이름으로 rpmsg 드라이버를 붙인다(`rpmsg-raw` → `/dev/rpmsgN`). 원격 crash는 `rproc_report_crash` → coredump → recovery로 처리되지만 채널은 다시 생기고, Linux crash는 원격이 직접 감시해야 한다. 이 노트에서는 진짜 6.18.55 커널 코드 위에 가짜 rproc 드라이버와 가짜 원격 코어를 얹어 이 전부를 실제로 돌려 봤다.

- [ ] `rproc_boot()`부터 `RPROC_RUNNING`까지의 함수 순서와 각 단계의 `rproc_ops` 콜백을 그릴 수 있다
- [ ] resource table의 네 가지 type과, 각 칸을 펌웨어와 Linux 중 누가 채우는지 설명할 수 있다
- [ ] Cortex-M 링커 스크립트에서 `.resource_table`의 LMA/VMA를 맞추고 `readelf -l`로 검증할 수 있다
- [ ] `.load`를 비우면 `parse_fw`가 덮어써지는 이유를 소스로 설명할 수 있다
- [ ] vring의 desc · avail · used를 그리고, rpmsg가 vring0/vring1을 어떻게 쓰는지 말할 수 있다
- [ ] rpmsg 주소 · endpoint · NS(주소 53) · 채널 이름 매칭을 설명하고 `/sys/bus/rpmsg/devices`에서 확인할 수 있다
- [ ] `/dev/rpmsg_ctrl0` + `RPMSG_CREATE_EPT_IOCTL`로 endpoint를 만들어 왕복하는 C 코드를 짤 수 있다
- [ ] crash → coredump → recovery 흐름과 recovery/coredump debugfs 설정을 운영할 수 있다
- [ ] attach 모드가 필요한 상황과 위험(버전 불일치, stop 권한)을 말하고 TI K3 IPC-only 모드를 예로 들 수 있다
- [ ] 실물 DT(`vdev0vring0`, `vdev0buffer`, `mbox-names`, watchdog 인터럽트)를 읽고 core 코드의 어디로 이어지는지 연결할 수 있다
- [ ] 큰 데이터를 rpmsg 대신 carveout으로 보내야 하는 이유(496 B 한계, kick 비용, 꼬리 지연)를 숫자로 말할 수 있다

## 참고 자료

- Linux kernel docs — Remote Processor Framework: https://docs.kernel.org/staging/remoteproc.html
- Linux kernel docs — Remote Processor Messaging (rpmsg) Framework: https://docs.kernel.org/staging/rpmsg.html
- 커널 소스 (랩 트리 `/var/lab/src/linux-6.18.55`, 또는 elixir.bootlin.com의 v6.18): `drivers/remoteproc/remoteproc_core.c`, `remoteproc_elf_loader.c`, `remoteproc_virtio.c`, `remoteproc_debugfs.c`, `remoteproc_sysfs.c`, `remoteproc_cdev.c`, `remoteproc_coredump.c`, `imx_rproc.c`, `stm32_rproc.c`, `ti_k3_common.c`, `ti_k3_r5_remoteproc.c`, `ti_k3_m4_remoteproc.c`; `drivers/rpmsg/virtio_rpmsg_bus.c`, `rpmsg_core.c`, `rpmsg_ns.c`, `rpmsg_char.c`, `rpmsg_ctrl.c`; `samples/rpmsg/rpmsg_client_sample.c`; `include/linux/remoteproc.h`, `include/linux/rsc_table.h`, `include/uapi/linux/rpmsg.h`, `include/uapi/linux/remoteproc_cdev.h`
- Device Tree 바인딩: `Documentation/devicetree/bindings/remoteproc/st,stm32-rproc.yaml`, `fsl,imx-rproc.yaml`; 보드 예 `arch/arm/boot/dts/st/stm32mp15xx-dkx.dtsi`
- OpenAMP 프로젝트: https://www.openampproject.org/ , 소스 https://github.com/OpenAMP/open-amp , https://github.com/OpenAMP/libmetal
- RPMsg-Lite: https://github.com/nxp-mcuxpresso/rpmsg-lite
- Zephyr 샘플 `samples/subsys/ipc/openamp_rsc_table` (Zephyr 소스 트리)
- 이 모듈의 관련 노트: G10(firmware loading), H10(mailbox · hwspinlock), F10(reserved-memory · CMA), N4(Linux vs RTOS vs MCU 분담), O1(이종 코어 구조), O3(외부 MCU 통신)
