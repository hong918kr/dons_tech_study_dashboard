# G5. ioctl과 mmap — 유저 인터페이스 설계, remap_pfn_range, 그리고 sysfs가 나은 경우

> **이 노트를 다 읽으면**: 한 드라이버에서 "제어는 어디로, 데이터는 어디로"를 read/write · ioctl · sysfs · mmap · netlink 중에 근거를 대고 고를 수 있다 · 수년간 32/64비트 앱과 따로 업데이트돼도 안 깨지는 ioctl UAPI 헤더(고정 폭 타입 · 예약 비트 0 검사 · size 필드 · `copy_struct_from_user` 확장 패턴)를 설계하고 `compat_ptr_ioctl`로 32비트 앱을 받을 수 있다 · 센서 샘플 링버퍼를 `.mmap` + `remap_pfn_range`/`vm_insert_pages`/`.fault`로 유저에 zero-copy 공유하고, 크기·offset 검사를 빼면 왜 커널 메모리가 새는지 직접 보고 고칠 수 있다
> **왜 필요한가**: "How would you expose this device to user space?"와 "Why is a bad mmap handler a security hole?"는 드라이버 면접 단골이고, V4L2 카메라·IMU FIFO·DMA 링을 Linux SoC에 올릴 때 매 bring-up마다 직접 짜는 코드다
> **Don 기준 난이도**: 링버퍼·DMA·레지스터 매핑·캐시 속성은 SSD 펌웨어에서 이미 했다 / 새로 배울 부분은 그걸 "유저에게 안전하게 노출"하는 커널 쪽 약속 — VMA·`vm_page_prot`·`remap_pfn_range`·`vm_operations_struct`, 그리고 인터페이스 선택의 관례
> **선행 노트**: A5(유저 관점 mmap·VMA·page fault), E6(ioctl 번호 인코딩·`copy_to/from_user`·errno·compat 진입 경로), F5(`READ_ONCE`/`smp_store_release` 순서), F9(페이지 할당·`split_page`), F10(`dma_alloc_coherent`·reserved-memory) — G4(char device `file_operations`), G6(poll), G8(sysfs), H9(dma-buf), UIO로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

드라이버 하나를 열면 유저에게 보여 줄 "구멍"이 여러 개다. IMU(`sensorkit`) 하나를 예로 들면 유저 공간이 하는 일은 네 종류다.

- **제어/설정**: "ODR(출력 데이터율)을 1 kHz로, 레인지를 ±8 g로, accel만 켜라" — 가끔, 작은 데이터, 원자적으로.
- **상태 조회**: "지금 몇 Hz로 돌고 있나, 칩 ID는, 드롭된 샘플 수는" — 사람이 `cat`으로 보고 싶을 때도 있다.
- **대량 데이터**: 초당 수천~수백만 개의 샘플 — 빠르고, 복사 비용이 아까운.
- **비동기 통지**: "FIFO가 찼다", "워터마크 넘었다" — 이벤트.

이걸 전부 `read()`/`write()` 하나에 욱여넣으면 "두 번째 바이트가 3이면 설정, 아니면 데이터" 같은 괴물이 된다. Linux의 관례는 **각 구멍에 맞는 인터페이스를 따로 고르는 것**이다: 제어는 `ioctl`(또는 sysfs), 상태는 sysfs, 대량 데이터는 `read()`나 `mmap` 링, 이벤트는 `poll`/`fasync`. 이 노트는 그중 **ioctl 설계**와 **mmap**을 깊게 판다.

Don의 bare-metal 비유로 깔면 쉽다. SSD 펌웨어에서 호스트(NVMe)가 컨트롤러에게 말을 거는 통로는 두 가지였다: **admin/IO 커맨드**(SQE — 작고, 구조가 정해진 명령, 가끔)와 **data DMA**(호스트 메모리와 NAND 버퍼 사이 대량 전송, PRP/SGL로 주소만 넘기고 복사는 엔진이). ioctl이 바로 SQE다 — 번호(opcode) + 고정 구조체. mmap 링이 바로 data DMA다 — 유저와 커널이 **같은 물리 페이지**를 보게 해 놓고, 복사 없이 포인터만 오간다. "커맨드 구조체를 호스트/디바이스 양쪽이 같은 레이아웃으로 봐야 한다"는 NVMe ABI 문제가 여기서는 "32비트 앱과 64비트 커널이 같은 ioctl 구조체를 봐야 한다"는 compat 문제로 돌아온다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="g5ah0" markerWidth="9" markerHeight="9" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="180" height="230" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="52" font-size="13" text-anchor="middle" font-weight="bold">유저: imu 데몬</text>
  <text x="110" y="90" font-size="12" text-anchor="middle">SET_CONFIG ioctl</text>
  <text x="110" y="120" font-size="12" text-anchor="middle">START / STOP ioctl</text>
  <text x="110" y="150" font-size="12" text-anchor="middle">cat /sys/.../odr</text>
  <text x="110" y="180" font-size="12" text-anchor="middle">poll(fd) 이벤트</text>
  <text x="110" y="222" font-size="12" text-anchor="middle">mmap 링에서</text>
  <text x="110" y="240" font-size="12" text-anchor="middle">샘플 직접 읽기</text>
  <rect x="440" y="30" width="200" height="230" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="540" y="52" font-size="13" text-anchor="middle" font-weight="bold">커널: g5_sensorkit</text>
  <text x="540" y="90" font-size="12" text-anchor="middle">.unlocked_ioctl</text>
  <text x="540" y="150" font-size="12" text-anchor="middle">DEVICE_ATTR (sysfs)</text>
  <text x="540" y="180" font-size="12" text-anchor="middle">.poll + wait queue</text>
  <text x="540" y="222" font-size="12" text-anchor="middle">.mmap → 샘플 링</text>
  <text x="540" y="240" font-size="12" text-anchor="middle">(같은 물리 페이지)</text>
  <line x1="200" y1="85" x2="438" y2="85" stroke="currentColor" marker-end="url(#g5ah0)"/>
  <line x1="200" y1="150" x2="438" y2="150" stroke="currentColor" marker-end="url(#g5ah0)"/>
  <line x1="200" y1="180" x2="438" y2="180" stroke="currentColor" marker-end="url(#g5ah0)"/>
  <line x1="438" y1="228" x2="202" y2="228" stroke="currentColor" marker-end="url(#g5ah0)"/>
  <text x="320" y="78" font-size="11" text-anchor="middle">작은 명령 (복사)</text>
  <text x="320" y="221" font-size="11" text-anchor="middle">대량 데이터 (복사 없음)</text>
</svg>
```
그림 1 — 한 장치, 네 통로. 작고 가끔인 제어는 ioctl/sysfs로 복사해 넘기고, 초당 수천 개의 샘플은 mmap 링으로 복사 없이 공유한다. 이 노트는 가운데 위(ioctl)와 맨 아래(mmap)를 다룬다.

이 노트의 실습 커널 버전은 `uname -r` 기준 **6.18.55**(공용 랩, QEMU `virt`/Cortex-A57)다. 유저 쪽 비유는 A5, ioctl 번호의 비트 구조와 compat 진입 경로는 E6에서 이미 했으니 **반복하지 않고 참조**한다. 여기서는 "번호를 어떻게 인코딩하나"가 아니라 "**어떤 인터페이스를 고르고, UAPI를 어떻게 설계하고, mmap을 어떻게 안전하게 구현하나**"를 본다.

---

## 1. 인터페이스 선택 — read/write · ioctl · sysfs · mmap · netlink · configfs

### 1.1 직관 — "모양 · 빈도 · 크기 · 누가 보나"로 가른다

새 장치를 올릴 때 제일 먼저 하는 결정이다. 네 가지 축으로 생각한다.

- **데이터 모양**: 바이트 스트림인가(오디오·시리얼), 정해진 구조체인가(설정 블록), 그냥 숫자 하나인가(온도).
- **빈도·크기**: 초당 한 번 몇 바이트인가, 초당 수만 번 수 MB인가.
- **누가 조작하나**: 사람이 셸에서 `cat`/`echo` 하나, 전용 데몬이 바이너리로 하나.
- **원자성·트랜잭션**: 여러 값을 "한 번에" 바꿔야 하나(ODR+레인지+플래그), 하나씩 바꿔도 되나.

### 1.2 비교표

| 인터페이스 | 데이터 모양 | 좋은 경우 | 나쁜 경우 | 핵심 메커니즘 |
| --- | --- | --- | --- | --- |
| read/write | 바이트 스트림 | 순차 데이터, FIFO, 로그 | 구조화된 제어 명령 | `.read`/`.write` + `copy_to/from_user` (E6) |
| ioctl | 고정 구조체 + 명령 번호 | 여러 필드 원자적 설정, START/STOP, 트랜잭션 | 사람이 셸로 만질 값, 스트림 | `.unlocked_ioctl` + `_IOR`/`_IOW` (E6) |
| sysfs | 한 파일 = 한 값(텍스트) | 사람이 보고 바꾸는 튜닝 값, 상태 | 큰 데이터, 여러 값 원자 설정 | `DEVICE_ATTR` show/store (G8) |
| mmap | 공유 메모리 영역 | 대량·고속 샘플/프레임, zero-copy | 작고 가끔인 제어 | `.mmap` + `remap_pfn_range` 등 |
| poll/select | 이벤트(준비됨 비트) | "데이터 왔다" 통지 | 데이터 운반 자체 | `.poll` + `poll_wait` (G6) |
| netlink | 가변 메시지(소켓) | 비동기 이벤트 브로드캐스트, 여러 수신자 | 간단한 단일 장치 제어 | `genl` 패밀리, `nlmsg` |
| configfs | 디렉터리 = 객체 | 런타임에 객체를 만들고 지우는 설정 | 고정된 단일 장치 | `config_item`/`config_group` |

### 1.3 결정 트리

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="g5ah1" markerWidth="9" markerHeight="9" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="230" y="20" width="200" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="330" y="42" font-size="12" text-anchor="middle">유저가 뭘 하려 하나?</text>
  <rect x="20" y="90" width="150" height="48" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="110" font-size="12" text-anchor="middle">대량·고속</text>
  <text x="95" y="128" font-size="12" text-anchor="middle">데이터 운반</text>
  <rect x="255" y="90" width="150" height="48" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="330" y="110" font-size="12" text-anchor="middle">장치 제어/설정</text>
  <text x="330" y="128" font-size="12" text-anchor="middle">(명령)</text>
  <rect x="490" y="90" width="150" height="48" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="110" font-size="12" text-anchor="middle">이벤트 통지</text>
  <line x1="300" y1="54" x2="110" y2="88" stroke="currentColor" marker-end="url(#g5ah1)"/>
  <line x1="330" y1="54" x2="330" y2="88" stroke="currentColor" marker-end="url(#g5ah1)"/>
  <line x1="360" y1="54" x2="555" y2="88" stroke="currentColor" marker-end="url(#g5ah1)"/>
  <rect x="20" y="180" width="150" height="48" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="200" font-size="12" text-anchor="middle">순차 스트림?</text>
  <text x="95" y="218" font-size="11" text-anchor="middle">→ read/write</text>
  <rect x="20" y="258" width="150" height="48" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="278" font-size="12" text-anchor="middle">복사 비용 아까움?</text>
  <text x="95" y="296" font-size="11" text-anchor="middle">→ mmap 링</text>
  <line x1="95" y1="138" x2="95" y2="178" stroke="currentColor" marker-end="url(#g5ah1)"/>
  <line x1="95" y1="228" x2="95" y2="256" stroke="currentColor" marker-end="url(#g5ah1)"/>
  <rect x="255" y="180" width="150" height="48" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="330" y="200" font-size="12" text-anchor="middle">사람이 셸로?</text>
  <text x="330" y="218" font-size="11" text-anchor="middle">→ sysfs 한 값씩</text>
  <rect x="255" y="258" width="150" height="48" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="330" y="276" font-size="12" text-anchor="middle">여러 값 원자적/</text>
  <text x="330" y="294" font-size="11" text-anchor="middle">트랜잭션 → ioctl</text>
  <line x1="330" y1="138" x2="330" y2="178" stroke="currentColor" marker-end="url(#g5ah1)"/>
  <line x1="330" y1="228" x2="330" y2="256" stroke="currentColor" marker-end="url(#g5ah1)"/>
  <rect x="490" y="180" width="150" height="48" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="200" font-size="12" text-anchor="middle">한 fd 대기?</text>
  <text x="565" y="218" font-size="11" text-anchor="middle">→ poll/select</text>
  <rect x="490" y="258" width="150" height="48" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="276" font-size="12" text-anchor="middle">여러 수신자 방송?</text>
  <text x="565" y="294" font-size="11" text-anchor="middle">→ netlink</text>
  <line x1="565" y1="138" x2="565" y2="178" stroke="currentColor" marker-end="url(#g5ah1)"/>
  <line x1="565" y1="228" x2="565" y2="256" stroke="currentColor" marker-end="url(#g5ah1)"/>
  <text x="330" y="340" font-size="11" text-anchor="middle">현실의 서브시스템(IIO·V4L2·input)은 이 조합을 이미 정해 뒀다 — 가능하면 그걸 쓴다</text>
</svg>
```
그림 2 — 인터페이스 선택 결정 트리. 중요한 현실: IMU라면 보통 **IIO 서브시스템**(I 모듈)을 쓰고, 카메라면 **V4L2**를 쓴다. 이들이 이미 "설정은 sysfs/ioctl, 데이터는 mmap/read, 이벤트는 poll"을 표준화해 놨다. 이 노트의 raw char device는 **그 서브시스템들이 내부에서 하는 일**을 손으로 해 보는 것이다.

### 1.4 "ioctl은 최후의 수단" — 커뮤니티 관례

커널 커뮤니티는 ioctl을 좋아하지 않는다. 이유: (1) 번호·구조체가 드라이버마다 제각각이라 툴로 자동 검사가 안 되고, (2) 32/64비트 호환을 손으로 챙겨야 하고, (3) 한 번 UAPI로 공개하면 영원히 깨면 안 된다. 그래서 순서가 이렇다.

1. 맞는 **서브시스템**이 있으면 그걸 쓴다(IIO·V4L2·hwmon·input·gpiod). 유저 ABI를 새로 발명하지 않는다.
2. 사람이 만지는 **단일 튜닝 값**은 sysfs(G8).
3. 디버그용 덤프는 debugfs(G8) — ABI 보장 없음.
4. 위로 안 되는 **여러 값 원자 설정·트랜잭션·START/STOP**만 ioctl.
5. 대량 데이터는 mmap 또는 dma-buf(H9).

"그냥 ioctl로 다 하면 안 되나?" — 된다. 하지만 면접에서 "왜 sysfs가 아니고 ioctl인가"에 답을 못 하면 설계를 안 해 본 티가 난다. 기준은 **원자성과 데이터 크기**다: ODR·레인지·플래그를 "셋 다 같이, 스트리밍 멈춘 상태에서" 바꿔야 하면 ioctl 한 방이 맞다. 각각을 따로 `echo` 하는 sysfs면 중간 상태(새 ODR + 옛 레인지)가 잠깐 생긴다.

---

## 2. 안정적인 UAPI 헤더 설계 — 고정 폭 타입 · 예약 필드 · size · 확장

### 2.1 직관 — "이 헤더는 10년 뒤 32비트 레거시 앱도 include 한다"

UAPI(user API) 헤더는 유저와 커널이 **같은 파일**을 `include`하는 계약서다. 실제 드라이버면 `include/uapi/linux/`에 들어가고, 한 번 릴리스되면 **영원히 깨면 안 된다**(Linus의 "we do not break userspace"). E6 §6에서 ioctl 번호 비트 구조와 패딩/32·64비트 함정은 이미 다뤘다. 여기서는 그 위에 **버전·크기·확장**을 얹는다.

규칙 다섯 개:

- **고정 폭 타입만**: `__u32`/`__u64`/`__s16`. `long`·`int`·`size_t`·`enum`·포인터 금지(크기가 ABI마다 다르다). 유저 포인터는 `__u64`로 싣는다.
- **명시적 패딩과 정렬**: 구멍이 없게 큰 필드부터, 또는 `reserved` 필드를 둔다. E6 §6.5의 pahole로 구멍을 확인한다.
- **예약 비트/필드는 0 강제**: 지금 안 쓰는 플래그 비트와 `reserved` 필드는 커널이 "0이 아니면 `-EINVAL`"로 거절한다. 그래야 나중에 그 비트에 의미를 줄 때, 옛 커널이 모르고 통과시킨 쓰레기 값과 구분된다.
- **size/version 필드로 확장**: 구조체 맨 뒤에만 필드를 덧붙이고, 커널은 `copy_struct_from_user`로 "유저가 보낸 크기"를 받아 호환을 맞춘다(§2.3).
- **magic + 번호 등록**: type 문자는 `Documentation/userspace-api/ioctl/ioctl-number.rst`에서 안 겹치게 고른다.

### 2.2 `g5_sensorkit.h` — sensorkit IMU의 UAPI

이게 이 노트의 모든 예제가 유저·커널 양쪽에서 `include`하는 헤더다. 실제 `include/uapi/linux/`에 둘 모양으로 짰다.

```c
/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#define G5_ABI_VERSION	1
#define G5_IOC_MAGIC	0xB5		/* 실습용. 실제로는 ioctl-number.rst 에 등록 */

struct g5_config {			/* 유저 → 커널 (_IOW) */
	__u32 odr_hz;			/* 출력 데이터율 */
	__u16 range_g;			/* 2, 4, 8, 16 */
	__u16 reserved0;		/* 반드시 0 */
	__u32 flags;			/* G5_CFG_F_*, 나머지 비트는 반드시 0 */
	__u32 reserved1;		/* 반드시 0 */
	/* ---- VER0 끝 (16 bytes) ---- */
	__u32 lpf_hz;			/* v2: 디지털 LPF, 0 = 기본값 */
	__u32 reserved2;		/* 반드시 0 */
	/* ---- VER1 끝 (24 bytes) ---- */
};
#define G5_CONFIG_SIZE_VER0	16
#define G5_CONFIG_SIZE_VER1	24

#define G5_IOC_GET_INFO		_IOR(G5_IOC_MAGIC, 1, struct g5_info)
#define G5_IOC_SET_CONFIG	_IOW(G5_IOC_MAGIC, 2, struct g5_config)
#define G5_IOC_START		_IO(G5_IOC_MAGIC, 3)
#define G5_IOC_STOP		_IO(G5_IOC_MAGIC, 4)
#define G5_IOC_FACTORY_CALIB	_IOW(G5_IOC_MAGIC, 5, struct g5_calib)
```

여기서 핵심은 `reserved0/1/2`와 `flags`의 "안 쓰는 비트"다. `odr_hz`를 `__u32`로, `range_g`를 `__u16`로 두고 그 뒤에 `reserved0 __u16`을 박아 4바이트 경계를 맞췄다 — 그래야 arm64/arm32/i386에서 구멍이 안 생긴다(E6 §6.5가 pahole로 증명한 그 문제). 유저 포인터는 이 구조체에 안 넣었다. 넣어야 하면 `__u64`로 싣고 `u64_to_user_ptr()`로 받는다(E6 §6.2 `e6_cfg.name_ptr` 패턴).

### 2.3 확장 패턴 — `copy_struct_from_user` (6.18에 존재 확인)

구조체를 "뒤에만 덧붙여" 확장하면서 옛 앱과 새 앱을 한 커널이 다 받는 표준 헬퍼다. 랩 커널 소스에서 **실제로 존재**한다:

```sh
limactl shell elinux -- grep -n copy_struct_from_user /var/lab/src/linux-6.18.55/include/linux/uaccess.h
```

```text
342: * copy_struct_from_user: copy a struct from userspace
389:copy_struct_from_user(void *dst, size_t ksize, const void __user *src, size_t usize)
```

동작(헤더 주석 그대로): `usize`는 유저가 보낸 크기, `ksize`는 커널이 아는 크기(`sizeof`).

- `usize == ksize`: 그대로 복사.
- `usize < ksize`(옛 앱 → 새 커널): 앞 `usize`만 복사하고 나머지는 **0으로 채운다**. 그래서 옛 앱이 모르는 새 필드(`lpf_hz`)는 0 = "기본값"이 된다.
- `usize > ksize`(새 앱 → 옛 커널): 커널이 모르는 꼬리 바이트가 **전부 0인지 검사**하고, 0이 아니면 `-E2BIG`. "네가 켠 그 새 기능을 난 모른다"는 거절이다.

커널에서 쓰는 모습(`g5_sensorkit.c`의 SET_CONFIG 처리):

```c
static long g5_set_config(struct file *f, void __user *argp, size_t usize)
{
	struct g5_config c = {};		/* 옛 앱이면 꼬리가 0 으로 남는다 */
	int ret;

	if (!(f->f_mode & FMODE_WRITE))
		return -EBADF;
	if (usize < G5_CONFIG_SIZE_VER0)
		return -EINVAL;			/* v0 보다 작으면 말도 안 됨 */
	ret = copy_struct_from_user(&c, sizeof(c), argp, usize);
	if (ret)
		return ret;			/* -E2BIG 또는 -EFAULT */
	if (c.reserved0 || c.reserved1 || c.reserved2 || (c.flags & ~G5_CFG_F_ALL))
		return -EINVAL;			/* 모르는 비트를 조용히 무시하지 않는다 */
	if (c.odr_hz < 10 || c.odr_hz > 2000000)
		return -EINVAL;
	if (c.range_g != 2 && c.range_g != 4 && c.range_g != 8 && c.range_g != 16)
		return -EINVAL;
	if (c.lpf_hz > c.odr_hz / 2)
		return -EINVAL;			/* Nyquist */
	...
}
```

`usize`는 어디서 오나? ioctl 번호 안에 들어 있다. `_IOW(magic, 2, struct foo)`의 size 필드가 `sizeof(struct foo)`다(E6 §6.1). 그래서 같은 nr=2를 **옛/새 구조체로 각각** 매크로를 만들면 번호의 size 필드가 달라지고, 커널은 `_IOC_SIZE(cmd)`로 그 값을 읽는다. `g5_ioctl`은 "type=magic, dir=write, nr=2"면 크기에 상관없이 `g5_set_config`로 보낸다:

```c
	if (_IOC_TYPE(cmd) == G5_IOC_MAGIC && _IOC_DIR(cmd) == _IOC_WRITE &&
	    _IOC_NR(cmd) == _IOC_NR(G5_IOC_SET_CONFIG))
		return g5_set_config(f, argp, _IOC_SIZE(cmd));
```

> 커널 안에는 이보다 흔한 다른 확장 관례도 있다: 구조체 **첫 필드에 `__u32 size`**(또는 `flags`)를 넣고 유저가 직접 채우게 하는 방식(perf_event_attr, clone3, sched_attr가 그렇다). ioctl은 번호에 size가 이미 있으니 `_IOC_SIZE`를 쓰는 게 자연스럽고, 새 syscall이면 명시적 size 필드를 쓴다. 둘 다 원리는 같다 — "유저가 자기 구조체 크기를 알려 주고, 커널이 min으로 맞춘다".

### 2.4 UAPI 확장 레이아웃 그림

```svg
<svg viewBox="0 0 660 320" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13" font-weight="bold">struct g5_config — 뒤에만 덧붙여 확장 (1칸 = 4 bytes)</text>
  <text x="20" y="58" font-size="12">v1 커널이 아는 크기 (ksize = 24)</text>
  <rect x="20" y="70" width="80" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="60" y="91" font-size="11" text-anchor="middle">odr_hz</text>
  <rect x="100" y="70" width="80" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="140" y="87" font-size="11" text-anchor="middle">range_g</text><text x="140" y="100" font-size="11" text-anchor="middle">+ resv0</text>
  <rect x="180" y="70" width="80" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="220" y="91" font-size="11" text-anchor="middle">flags</text>
  <rect x="260" y="70" width="80" height="34" fill="#888" fill-opacity="0.18" stroke="#888"/><text x="300" y="91" font-size="11" text-anchor="middle">resv1</text>
  <rect x="340" y="70" width="80" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="380" y="91" font-size="11" text-anchor="middle">lpf_hz</text>
  <rect x="420" y="70" width="80" height="34" fill="#888" fill-opacity="0.18" stroke="#888"/><text x="460" y="91" font-size="11" text-anchor="middle">resv2</text>
  <text x="90" y="128" font-size="11">|← VER0 = 16 →|</text>
  <text x="300" y="128" font-size="11">|← ← VER1 = 24 → →|</text>
  <line x1="20" y1="114" x2="180" y2="114" stroke="#4a7bd0"/>
  <line x1="20" y1="134" x2="420" y2="134" stroke="#e08a3c"/>
  <text x="20" y="178" font-size="12" font-weight="bold">옛 앱(usize=16) → v1 커널(ksize=24)</text>
  <rect x="20" y="190" width="240" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="140" y="210" font-size="11" text-anchor="middle">유저가 보낸 16 B 복사</text>
  <rect x="260" y="190" width="160" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340" y="210" font-size="11" text-anchor="middle">lpf/resv2 = 0 채움 → 기본값</text>
  <text x="20" y="258" font-size="12" font-weight="bold">새 앱(usize=24) → 옛 v0 커널(ksize=16)</text>
  <rect x="20" y="270" width="240" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="140" y="290" font-size="11" text-anchor="middle">앞 16 B 복사</text>
  <rect x="260" y="270" width="160" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="340" y="290" font-size="11" text-anchor="middle">꼬리 8 B 0 아니면 -E2BIG</text>
  <text x="440" y="289" font-size="11">검사 = check_zeroed_user</text>
</svg>
```
그림 3 — `copy_struct_from_user`의 세 경우. 새 필드는 **반드시 맨 뒤에**, 그리고 "0 = 옛 동작"이 되게 의미를 준다. `lpf_hz=0`이 "LPF 기본값"인 게 그래서다.

---

## 3. ioctl 핸들러 설계 — compat · ENOTTY · 권한 · START/STOP

### 3.1 `unlocked_ioctl` vs `compat_ioctl` — `compat_ptr_ioctl`

`file_operations`에는 ioctl이 두 개다. `.unlocked_ioctl`은 64비트(native) 앱용, `.compat_ioctl`은 32비트 앱이 64비트 커널을 부를 때용. E6 §6.6에서 진입 경로(`el0t_32_sync` → `do_el0_svc_compat` → `__arm64_compat_sys_ioctl`)를 이미 봤다. 핵심은 **32비트 앱의 `arg`는 32비트 포인터**라 상위 비트가 비어 있고, 커널은 이걸 `compat_ptr()`로 64비트 포인터로 넓혀야 한다는 것.

우리 UAPI는 구조체 안에 포인터도 `long`도 없게(전부 `__u32`/`__u64`) 설계했으므로, 변환 코드가 필요 없다. 그럴 때 쓰는 커널 제공 헬퍼가 `compat_ptr_ioctl`이다. 랩 소스에서 확인:

```sh
limactl shell elinux -- sed -n '629,637p' /var/lab/src/linux-6.18.55/fs/ioctl.c
```

```text
long compat_ptr_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (!file->f_op->unlocked_ioctl)
		return -ENOIOCTLCMD;
	return file->f_op->unlocked_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
```

그래서 우리 드라이버는 이렇게 등록한다:

```c
static const struct file_operations g5_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= g5_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,	/* 구조체가 ABI 독립이라 충분 */
	...
};
```

만약 구조체에 유저 포인터나 `long`·`struct timespec`(32/64비트 크기 다름)이 있었다면 `compat_ptr_ioctl`로는 안 되고, `.compat_ioctl`에 직접 변환 함수를 짜서 32비트 레이아웃을 64비트로 재구성해야 한다.

### 3.2 모르는 명령은 `-ENOTTY`, 권한 검사는 `capable()`

- **알 수 없는 cmd → `-ENOTTY`**("not a typewriter" — 역사적 이름). `switch`의 `default`에서 돌려준다. 커널이 내부적으로 `-ENOIOCTLCMD`를 쓰면 VFS가 `-ENOTTY`로 바꿔 유저에게 준다(E6 §5.3, `fs/ioctl.c`의 `vfs_ioctl`). 유저가 `isatty()`나 다른 드라이버의 ioctl을 잘못 부르면 여기로 떨어진다.
- **특권 명령 → `capable(CAP_SYS_ADMIN)`**. "공장 캘리브레이션 쓰기"처럼 장치를 망가뜨릴 수 있는 명령은 권한을 본다. `capable()`은 플래그 복사·데이터 접근 **전에** 호출한다 — 권한 없는 호출이 side effect를 남기면 안 된다.
- **파일 모드 검사**: `_IOW`(유저→커널 쓰기)인데 `O_RDONLY`로 연 fd면 거절한다. 위 `g5_set_config`가 `f->f_mode & FMODE_WRITE`로 본다.

### 3.3 START/STOP — 스트리밍 상태 기계

ioctl이 "트랜잭션"을 하는 전형이 START/STOP다. IMU를 켜고 끄는 건 여러 내부 상태(hrtimer, ODR, base 인덱스)를 **원자적으로** 바꾸는 일이라 sysfs 한 값으로는 부족하다.

```c
static long g5_start(void)
{
	long ret = 0;
	mutex_lock(&g5_lock);
	if (g5_streaming) { ret = -EBUSY; goto out; }	/* 두 번 START = EBUSY */
	g5_odr = g5_cfg.odr_hz;
	g5_period = g5_odr >= 1000 ? ms_to_ktime(1) : ns_to_ktime(NSEC_PER_SEC / g5_odr);
	g5_base = g5_produced;
	g5_t0_ns = ktime_get_ns();
	g5_streaming = true;
	hrtimer_start(&g5_timer, g5_period, HRTIMER_MODE_REL);
out:
	mutex_unlock(&g5_lock);
	return ret;
}
```

STOP은 **멱등**(이미 멈춰 있어도 0)으로 뒀다. 설정 변경은 스트리밍 중이면 `-EBUSY`로 막는다("멈추고 바꿔라"). 이게 bare-metal의 "레지스터 쓰기 전에 엔진 disable" 패턴 그대로다.

### 3.4 예제 1 — 버전·크기·예약 비트·모르는 명령·상태 오류 (게스트 실행)

무엇을 확인하나: 한 ioctl 세트가 옛/새 구조체를 모두 받고, 틀린 입력마다 **맞는 errno**를 돌려주는지. `g5_t1_ioctl`은 `/dev/g5_sensorkit`에 GET_INFO·SET_CONFIG(v0/v1/v9)·START/STOP을 던진다.

빌드와 부팅(E6와 같은 방식 — lab 커널 + 모듈/테스트를 initramfs에 넣고 QEMU):

```sh
# VM: 모듈 빌드
cd /var/tmp/G5/mod && make -C /var/lab/out/linux-build M=$PWD -j2 modules
# 테스트 정적 빌드 (게스트 initramfs엔 glibc가 없다)
gcc -std=gnu11 -Wall -Wextra -O2 -static -I.../G5/mod .../G5/user/g5_t1_ioctl.c -o g5_t1_ioctl
# 게스트: insmod 후 ./g5_t1_ioctl   (run.sh 가 모듈+바이너리를 넣고 부팅)
```

```text
lab initramfs: 6.18.55 up 2.27s
crw-------    1 0        0          10, 258 Oct 11 01:37 /dev/g5_sensorkit
GET_INFO=0x8028b501 SET_CONFIG=0x4018b502 SET_V0=0x4010b502 SET_V9=0x4020b502
GET_INFO                             -> 0
  abi=1 chip=0x6c ring=65536 slot=32 nslots=1920
SET_CONFIG v1 (24 B)                 -> 0
SET_CONFIG v0 (16 B, old app)        -> 0
SET_CONFIG v9 (32 B, tail=0)         -> 0
SET_CONFIG v9 (32 B, tail!=0)        -> -1  E2BIG
SET_CONFIG 8 B                       -> -1  EINVAL
reserved0=1                          -> -1  EINVAL
flags bit3 (unknown)                 -> -1  EINVAL
range_g=3                            -> -1  EINVAL
lpf 600 > odr/2                      -> -1  EINVAL
SET_CONFIG on O_RDONLY fd            -> -1  EBADF
bad pointer (EFAULT)                 -> -1  EFAULT
unknown nr 99                        -> -1  ENOTTY
wrong magic                          -> -1  ENOTTY
GET_INFO nr with _IOW dir            -> -1  ENOTTY
TCGETS (isatty)                      -> -1  ENOTTY
START                                -> 0
START again                          -> -1  EBUSY
SET_CONFIG while streaming           -> -1  EBUSY
STOP                                 -> 0
STOP again (idempotent)              -> 0
```

출력에서 볼 것: `SET_V0=0x4010b502`와 `SET_CONFIG=0x4018b502`는 **nr은 같고(02) size만 다르다**(0x10=16 vs 0x18=24) — 번호 안에 size가 들어 있다는 E6 §6.1의 직접 증거다. `v0`(옛 16 B 앱)도, `v9`(미래 32 B 앱, 꼬리=0)도 통과하지만 `v9`의 꼬리에 1을 넣으면 `E2BIG`. 예약 필드·모르는 플래그 비트·잘못된 레인지는 전부 `EINVAL`, 모르는 명령/magic/방향은 전부 `ENOTTY`, `O_RDONLY`는 `EBADF`. 이게 "ioctl을 설계했다"의 증거다.

### 3.5 예제 2 — 권한과 32비트 compat (게스트 실행)

무엇을 확인하나: (1) 권한 없는 uid가 일반 ioctl은 되지만 `FACTORY_CALIB`은 `-EPERM`, (2) 장치 노드 권한(0600 vs 0666)이 접근을 가르고, (3) **32비트 ARM 바이너리**가 64비트 커널에서 `compat_ptr_ioctl`로 그대로 동작.

```sh
arm-linux-gnueabihf-gcc -O2 -static -I.../G5/mod .../G5/user/g5_t1_ioctl.c -o g5_t1_ioctl32
# 게스트: ./g5_t2_unpriv 1000  (uid 1000 으로 권한 버리고 실행); 그 뒤 ./g5_t1_ioctl32
```

```text
--- uid 1000, mode 0600
Uid:	1000	1000	1000	1000
CapEff:	0000000000000000
/dev/g5_sensorkit: Permission denied
--- chmod 0666 (udev rule 역할)
Uid:	1000	1000	1000	1000
CapEff:	0000000000000000
GET_INFO                             -> 0
SET_CONFIG                           -> 0
FACTORY_CALIB                        -> -1  EPERM
--- root
Uid:	0	0	0	0
CapEff:	000001ffffffffff
FACTORY_CALIB                        -> 0
--- 32-bit app (compat_ptr_ioctl)
GET_INFO=0x8028b501 SET_CONFIG=0x4018b502 ...
SET_CONFIG v1 (24 B)                 -> 0
SET_CONFIG v9 (32 B, tail!=0)        -> -1  E2BIG
```

```text
e6와 같은 결과: file 로 보면 ELF 32-bit LSB executable, ARM, EABI5 ... statically linked
[    7.481156] g5: factory calib bias0=12       ← root 일 때만 찍힘
```

출력에서 볼 것: 0600이면 열기 단계에서 막히고(장치 노드 권한 = udev rule이 정함, G9), 0666이어도 `CAP_SYS_ADMIN` 없는 uid 1000은 `FACTORY_CALIB`에서 `EPERM`. root는 통과해 `dmesg`에 bias0=12가 찍힌다. **32비트 앱의 결과가 64비트와 같다** — UAPI가 ABI 독립이라 `compat_ptr_ioctl`만으로 충분하다는 증거. E6 §6.6의 call trace가 `compat_ptr_ioctl` → `__arm64_compat_sys_ioctl`을 그대로 보여 준다.

---

## 4. mmap — 왜, 그리고 `.mmap` 핸들러 안에서 하는 일

### 4.1 왜 mmap인가 — zero-copy 링/프레임

`read()`로 샘플을 받으면 커널 버퍼 → 유저 버퍼로 **매번 복사**한다(E6의 `copy_to_user`). 초당 수백만 샘플이면 이 복사와 syscall 자체가 비용이다. mmap은 유저와 커널이 **같은 물리 페이지**를 보게 해서 복사를 없앤다 — 유저는 그냥 포인터로 읽는다. 쓰이는 곳:

- **V4L2 카메라**: `VIDIOC_REQBUFS` + `mmap(fd, offset=버퍼쿠키)` — A5 §12.5에서 유저 쪽을 봤다. 커널 쪽 `.mmap`이 이 노트다.
- **DMA 링/프레임버퍼**: 네트워크 RX 링, GPU/디스플레이 프레임버퍼.
- **IMU FIFO**: 하드웨어 FIFO를 미러링한 링을 유저에 노출.

Don의 비유: NVMe의 PRP가 "호스트 메모리 물리 주소를 디바이스에 알려 주고 복사는 DMA 엔진이" 였던 것처럼, mmap은 "커널 버퍼의 물리 페이지를 유저 page table에 꽂아 주고 접근은 유저가 직접".

### 4.2 `.mmap`이 받는 것 — VMA

유저의 `mmap(addr, len, prot, flags, fd, offset)`은 커널에서 `ksys_mmap_pgoff` → `vm_mmap_pgoff` → `do_mmap` → `mmap_region`을 거쳐(A5 §11, F11) VMA(`struct vm_area_struct`)를 하나 만들고, 그 fd가 장치 파일이면 **드라이버의 `.mmap(file, vma)`**을 부른다. 랩 소스에서 호출 지점:

```sh
limactl shell elinux -- sed -n '2403,2409p' /var/lab/src/linux-6.18.55/include/linux/fs.h
```

```text
static inline int vfs_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (file->f_op->mmap_prepare)
		return compat_vma_mmap_prepare(file, vma);
	return file->f_op->mmap(file, vma);
}
```

> 6.x 최신 커널은 `.mmap` 외에 `.mmap_prepare`(VMA가 완성되기 전 `vm_area_desc`로 받는 새 훅)도 생겼다 — 둘은 **상호 배타**(`can_mmap_file`이 둘 다 있으면 `WARN`). 아직 대부분의 드라이버는 `.mmap`을 쓰므로 이 노트도 `.mmap`으로 간다.

`.mmap`이 받은 VMA에서 드라이버가 쓰는 필드:

- `vma->vm_start`, `vma->vm_end`: 유저가 받은 주소 범위. `vma_pages(vma)` = 페이지 수.
- `vma->vm_pgoff`: 유저가 준 offset을 페이지 단위로. **파일 offset이 아니라 "어느 버퍼/무슨 방식"을 고르는 쿠키**로 쓴다(A5 §12.5의 V4L2처럼).
- `vma->vm_page_prot`: PTE에 들어갈 캐시·권한 속성. MMIO면 여기를 `pgprot_noncached`로 바꾼다.
- `vma->vm_flags`: VMA 종류 플래그. `remap_pfn_range`가 `VM_IO|VM_PFNMAP|VM_DONTEXPAND|VM_DONTDUMP`를 세운다.

### 4.3 PTE를 채우는 세 가지 길

커널 버퍼(RAM)를 유저에 매핑하는 방법은 메모리 출처에 따라 다르다. 랩 소스에서 함수 확인:

```sh
limactl shell elinux -- grep -n 'int remap_pfn_range\|int vm_insert_pages\|dma_mmap_coherent' \
  /var/lab/src/linux-6.18.55/mm/memory.c /var/lab/src/linux-6.18.55/include/linux/dma-mapping.h
```

```text
mm/memory.c:3034:int remap_pfn_range(struct vm_area_struct *vma, unsigned long addr, unsigned long pfn, ...)
mm/memory.c:2439:int vm_insert_pages(struct vm_area_struct *vma, unsigned long addr, struct page **pages, ...)
include/linux/dma-mapping.h:593:#define dma_mmap_coherent(d, v, c, h, s) dma_mmap_attrs(d, v, c, h, s, 0)
```

| 방법 | 언제 | 넘기는 것 | VMA 플래그 | 비고 |
| --- | --- | --- | --- | --- |
| `remap_pfn_range` | kmalloc/page 할당 버퍼, MMIO | pfn(`virt_to_phys >> PAGE_SHIFT` 또는 `page_to_pfn`), size, prot | VM_IO · VM_PFNMAP · DONTEXPAND · DONTDUMP | struct page 안 봄. 한 방에 전체 |
| `io_remap_pfn_range` | MMIO 레지스터 | 위와 같음, arch가 prot 보정 | 위와 같음 | `vm_iomap_memory`가 더 간단 |
| `vm_insert_page`/`vm_insert_pages` | struct page 있는 보통 RAM 페이지 | `struct page *` 배열 | VM_MIXEDMAP | refcount 관리됨. reclaim 가능 |
| `dma_mmap_coherent` | `dma_alloc_coherent` 버퍼 | cpu_addr, dma_addr, size | 내부 처리 | 커널·유저 캐시 속성 일치 보장(F10) |
| `vm_ops->fault` | lazy, sparse, 큰 버퍼 | 접근할 때 페이지 하나씩 | DONTEXPAND · DONTDUMP | page fault마다 콜백 |

**캐시 속성이 핵심**이다(Don이 SSD에서 아는 그 문제). `vma->vm_page_prot`를:

- MMIO 레지스터 → `pgprot_noncached` (arm64에서 Device-nGnRnE).
- 프레임버퍼·WC 버퍼 → `pgprot_writecombine` (Normal-NC, 쓰기 모음).
- 보통 RAM 샘플 링 → 그대로(캐시 가능).

랩 arm64에서 확인:

```sh
limactl shell elinux -- sed -n '789,793p' /var/lab/src/linux-6.18.55/arch/arm64/include/asm/pgtable.h
```

```text
#define pgprot_noncached(prot)	 __pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_DEVICE_nGnRnE) | PTE_PXN | PTE_UXN)
#define pgprot_writecombine(prot) __pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_NORMAL_NC) | PTE_PXN | PTE_UXN)
```

DMA 버퍼를 직접 `remap_pfn_range`로 매핑하면 커널 쪽 매핑과 유저 쪽 매핑의 캐시 속성이 어긋나는(aliasing) 버그가 나므로, DMA는 반드시 `dma_mmap_coherent`를 쓴다(F10 `dma_alloc_coherent`와 짝).

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="g5ah2" markerWidth="9" markerHeight="9" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="40" width="180" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="64" font-size="12" text-anchor="middle">유저 VMA</text>
  <text x="110" y="84" font-size="11" text-anchor="middle">vm_start .. vm_end</text>
  <rect x="240" y="40" width="190" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="335" y="60" font-size="12" text-anchor="middle">.mmap 핸들러</text>
  <text x="335" y="80" font-size="11" text-anchor="middle">크기/offset 검사 → prot</text>
  <text x="335" y="96" font-size="11" text-anchor="middle">remap_pfn_range(pfn)</text>
  <rect x="470" y="40" width="170" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="555" y="64" font-size="12" text-anchor="middle">물리 페이지(링)</text>
  <text x="555" y="84" font-size="11" text-anchor="middle">page_to_pfn(g5_pages)</text>
  <line x1="200" y1="70" x2="238" y2="70" stroke="currentColor" marker-end="url(#g5ah2)"/>
  <line x1="430" y1="70" x2="468" y2="70" stroke="currentColor" marker-end="url(#g5ah2)"/>
  <text x="110" y="150" font-size="12" text-anchor="middle">유저 page table</text>
  <rect x="40" y="165" width="140" height="80" fill="#888" fill-opacity="0.10" stroke="#888"/>
  <text x="110" y="188" font-size="11" text-anchor="middle">PTE0 → pfn</text>
  <text x="110" y="208" font-size="11" text-anchor="middle">PTE1 → pfn+1</text>
  <text x="110" y="228" font-size="11" text-anchor="middle">... PTE15</text>
  <rect x="470" y="165" width="170" height="80" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="555" y="188" font-size="11" text-anchor="middle">같은 16 페이지</text>
  <text x="555" y="208" font-size="11" text-anchor="middle">hdr + 1920 slot</text>
  <text x="555" y="228" font-size="11" text-anchor="middle">hrtimer가 씀</text>
  <line x1="180" y1="205" x2="468" y2="205" stroke="currentColor" marker-end="url(#g5ah2)"/>
  <text x="324" y="198" font-size="11" text-anchor="middle">유저 load = 바로 이 RAM 읽기 (복사 없음)</text>
  <line x1="110" y1="100" x2="110" y2="163" stroke="currentColor" stroke-dasharray="3 3" marker-end="url(#g5ah2)"/>
</svg>
```
그림 4 — `.mmap`이 `remap_pfn_range`로 유저 VMA의 PTE를 드라이버 링의 물리 pfn으로 채운다. 그 뒤 유저의 메모리 읽기는 syscall도 복사도 없이 바로 그 RAM을 읽는다.

### 4.4 g5_sensorkit의 `.mmap` — offset으로 세 방식 선택

```c
static int g5_mmap(struct file *f, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
	struct page *pages[G5_RING_PAGES];
	unsigned long i, n;

	if (insecure)				/* 일부러 틀린 코드 (§6) */
		return remap_pfn_range(vma, vma->vm_start,
				       page_to_pfn(g5_pages) + vma->vm_pgoff,
				       size, vma->vm_page_prot);
	if (vma->vm_flags & VM_WRITE)
		return -EACCES;			/* 링은 유저에게 읽기 전용 */
	vm_flags_clear(vma, VM_MAYWRITE);	/* mprotect 로도 못 바꾸게 */
	if (size > G5_RING_BYTES)
		return -EINVAL;			/* 크기 검사 (보안!) */

	switch (off) {
	case G5_OFF_REMAP:			/* 0 : 전체를 한 방에 */
		return remap_pfn_range(vma, vma->vm_start, page_to_pfn(g5_pages),
				       size, vma->vm_page_prot);
	case G5_OFF_LAZY:			/* .fault 로 한 페이지씩 */
		vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
		vma->vm_ops = &g5_lazy_ops;
		return 0;			/* PTE 는 지금 안 만든다 */
	case G5_OFF_INSERT:			/* struct page 배열로 */
		n = vma_pages(vma);
		for (i = 0; i < n; i++)
			pages[i] = g5_pages + i;
		vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
		return vm_insert_pages(vma, vma->vm_start, pages, &n);
	default:
		return -EINVAL;			/* 모르는 offset */
	}
}
```

설계 포인트:

- **읽기 전용 강제**: `VM_WRITE`면 `-EACCES`, 그리고 `vm_flags_clear(vma, VM_MAYWRITE)`로 유저가 나중에 `mprotect`로 쓰기로 바꾸는 것까지 막는다. 센서 링은 커널만 쓴다.
- **크기 검사**: `size > G5_RING_BYTES`면 거절. 이 한 줄이 없으면 §6의 사고가 난다.
- **offset을 쿠키로**: V4L2가 "몇 번 버퍼"로 쓰는 그 자리를, 여기선 "어느 매핑 방식"으로 썼다.
- `vm_flags_set`/`vm_flags_clear`: 6.3부터 `vma->vm_flags`에 직접 대입하지 않고 이 접근자를 쓴다(F11 §참조).

링버퍼는 커널에서 `alloc_pages(order=5)` + `split_page`로 16페이지(64 KiB)를 잡고(F9의 페이지 할당·`split_page`), 0페이지는 헤더(head 인덱스), 1~15페이지는 1920개 슬롯이다:

```c
struct g5_ring_hdr {			/* 0 페이지 */
	__u32 magic; __u32 abi_version; __u32 nslots; __u32 slot_bytes;
	__u64 head;			/* 커널이 쓴 샘플 총수 */
};
struct g5_sample { __u64 t_ns; __u32 seq; __s16 acc[3]; __s16 gyr[3]; __s16 temp; __u16 reserved; __u32 pad; };  /* 32 B */
```

### 4.5 head/tail 동기화 — F5의 배리어가 여기서 나온다

커널(hrtimer)이 생산자, 유저가 소비자인 **단일 생산자/단일 소비자 링**이다. 핵심은 "샘플을 다 쓴 다음에 head를 올린다"는 **쓰기 순서**와 유저가 "head를 먼저 읽고 그 아래 샘플만 믿는다"는 **읽기 순서**다. F5의 `smp_store_release`/`smp_load_acquire`가 그대로 쓰인다.

```c
/* 커널 hrtimer */			/* 유저 (mmap 소비자) */
g5_slots[h % N] = sample;		head = smp_load_acquire(&hdr->head);
smp_store_release(&hdr->head, h+1);	for (; tail < head; tail++) use(slots[tail % N]);
```

```svg
<svg viewBox="0 0 660 230" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="g5ah3" markerWidth="9" markerHeight="9" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13" font-weight="bold">mmap 공유 링 (유저·커널이 같은 페이지)</text>
  <rect x="30" y="40" width="80" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="70" y="64" font-size="11" text-anchor="middle">hdr.head</text>
  <rect x="130" y="40" width="60" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160" y="64" font-size="11" text-anchor="middle">slot0</text>
  <rect x="190" y="40" width="60" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="220" y="64" font-size="11" text-anchor="middle">slot1</text>
  <rect x="250" y="40" width="60" height="40" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="280" y="64" font-size="11" text-anchor="middle">slot2</text>
  <rect x="310" y="40" width="60" height="40" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="340" y="64" font-size="11" text-anchor="middle">slot3</text>
  <rect x="370" y="40" width="60" height="40" fill="#888" fill-opacity="0.1" stroke="#888"/><text x="400" y="64" font-size="11" text-anchor="middle">...</text>
  <rect x="430" y="40" width="80" height="40" fill="#888" fill-opacity="0.1" stroke="#888"/><text x="470" y="64" font-size="11" text-anchor="middle">slot1919</text>
  <text x="280" y="105" font-size="11" text-anchor="middle">tail=2 (유저)</text>
  <line x1="280" y1="108" x2="280" y2="82" stroke="#3f9a6b" marker-end="url(#g5ah3)"/>
  <text x="340" y="125" font-size="11" text-anchor="middle">head=4 (커널)</text>
  <line x1="370" y1="128" x2="370" y2="82" stroke="#e08a3c" marker-end="url(#g5ah3)"/>
  <text x="30" y="160" font-size="12" font-weight="bold">커널:</text>
  <text x="85" y="160" font-size="12">slot 쓰기 → smp_store_release(head)   "데이터 먼저, 발행 나중"</text>
  <text x="30" y="185" font-size="12" font-weight="bold">유저:</text>
  <text x="85" y="185" font-size="12">head = smp_load_acquire → tail..head 슬롯만 읽기</text>
  <text x="30" y="212" font-size="11">head-tail &gt; N 이면 덮어쓰기 발생(overrun) — 유저가 tail 을 head-N 으로 점프, 손실 기록</text>
</svg>
```
그림 5 — mmap 공유 링. head/tail은 인덱스일 뿐 데이터가 아니다. 커널은 release로 발행하고 유저는 acquire로 읽어 "슬롯 내용이 head보다 먼저 보이는" 순서를 보장한다(F5). 유저가 느리면 커널이 덮어써서 손실(overrun)이 난다.

---

## 5. mmap 실측 — 복사 없음 vs read() 복사

### 5.1 예제 3 — mmap 링에서 read() 없이 소비 + 잘못된 매핑 거절 (게스트 실행)

무엇을 확인하나: 유저가 `mmap`한 링에서 `read()` 한 번 없이 2초간 샘플을 소비하고, 잘못된 매핑 요청(쓰기·크기 초과·엉뚱한 offset·mprotect)이 전부 거절되는지. `/proc/self/smaps`로 VMA 플래그도 확인한다.

```text
mmap PROT_WRITE                      -> -1  EACCES
mmap 128 KiB                         -> -1  EINVAL
mmap offset 4096                     -> -1  EINVAL
mprotect -> PROT_WRITE               -> -1  EACCES
ffffb206b000-ffffb207b000 r--s 00000000 00:06 145                        /dev/g5_sensorkit
VmFlags: rd sh mr me ms pf io de dd 
hdr: magic=0x47354b53 abi=1 nslots=1920 slot=32 head=0
consumed 2009 samples in 2 s, seq gaps 0, last seq 2008, acc=(984,-984,1000)
```

출력에서 볼 것: 쓰기 매핑·64 KiB 초과·헤더만 건너뛰려는 offset·`mprotect`가 전부 막힌다(설계대로). smaps의 `VmFlags`에 **`pf`(VM_PFNMAP)와 `io`(VM_IO), `de`(DONTEXPAND), `dd`(DONTDUMP)**가 보인다 — `remap_pfn_range`가 세운 그 플래그다(A5 §12.6, F11의 표와 일치). ODR 1000으로 2초 → 2009샘플 소비, **seq gap 0**(손실 없음), 전부 mmap 포인터 읽기로.

### 5.2 예제 4 — .fault lazy 매핑 vs 미리 채움 (게스트 실행, fault 수 세기)

무엇을 확인하나: 같은 링을 세 offset으로 매핑하고 페이지를 하나씩 건드리며 **minor fault 수**(`getrusage`의 `ru_minflt`)와 **드라이버 `.fault` 호출 수**(GET_INFO의 `lazy_faults`)를 센다. `remap_pfn_range`/`vm_insert_pages`는 미리 채우므로 fault 0, `.fault` 방식은 접근할 때마다 1.

`.fault` 핸들러는 snd_pcm의 데이터 fault와 같은 패턴이다:

```c
static vm_fault_t g5_fault(struct vm_fault *vmf)
{
	pgoff_t idx = vmf->pgoff - (G5_OFF_LAZY >> PAGE_SHIFT);
	struct page *p;
	if (idx >= G5_RING_PAGES)
		return VM_FAULT_SIGBUS;		/* 범위 밖 접근 = SIGBUS */
	p = g5_pages + idx;
	get_page(p);				/* 매핑이 참조 하나를 가진다 */
	vmf->page = p;
	atomic64_inc(&g5_lazy_faults);
	return 0;
}
```

```text
== REMAP (remap_pfn_range)
VmFlags: rd sh mr me ms pf io de dd 
  touch page  0 (byte  83): minflt +0, driver .fault +0
  touch page  1 (byte  48): minflt +0, driver .fault +0
  touch page  5 (byte  48): minflt +0, driver .fault +0
  touch page  0 (byte  83): minflt +0, driver .fault +0
  touch page 15 (byte   0): minflt +0, driver .fault +0
== INSERT (vm_insert_pages)
VmFlags: rd sh mr me ms de dd mm 
  touch page  0 (byte  83): minflt +0, driver .fault +0
  ...
== LAZY (.fault)
VmFlags: rd sh mr me ms de dd 
  touch page  0 (byte  83): minflt +1, driver .fault +1
  touch page  1 (byte  48): minflt +1, driver .fault +1
  touch page  5 (byte  48): minflt +1, driver .fault +1
  touch page  0 (byte  83): minflt +0, driver .fault +0
  touch page 15 (byte   0): minflt +1, driver .fault +1
magic via LAZY = 0x47354b53, same as REMAP = yes
```

출력에서 볼 것: `REMAP`과 `INSERT`는 `.mmap`에서 PTE를 다 채워서 접근해도 fault 0. `LAZY`는 **처음 건드린 페이지마다 minor fault 1 + 드라이버 `.fault` 1**, 두 번째로 같은 페이지 0(이미 PTE에 있음). VmFlags도 다르다: REMAP은 `pf io`(PFNMAP), INSERT는 `mm`(MIXEDMAP, struct page 기반), LAZY는 둘 다 없음. 세 방식 모두 결국 **같은 물리 페이지**를 보여 준다(`magic ... same as REMAP = yes`). lazy는 큰·sparse 버퍼에서 "실제로 건드린 페이지만 매핑"할 때 쓴다(A5 §12.6, F11 §의 `.fault` 갈래).

### 5.3 예제 5 — 처리량·CPU: mmap 폴링 vs read() 블로킹 (게스트 실행, 차트)

무엇을 확인하나: ODR를 1k~2M로 올리며, mmap 링을 1 ms 폴링으로 소비할 때와 `read()`(wait queue 블로킹)로 소비할 때의 **손실률**과 **소비자 CPU%**(`CLOCK_THREAD_CPUTIME_ID`). QEMU는 TCG라 `-icount shift=0`으로 명령 수 기반의 결정적 시간을 쓴다.

```text
mmap odr=   1000 got/s=    1000 lost=      0 torn=    0 wake/s= 950 cpu= 0.18% ns/sample= 1846.1
read odr=   1000 got/s=    1000 lost=      0 torn=    0 wake/s=1000 cpu= 0.16% ns/sample= 1622.5
mmap odr=  10000 got/s=    9999 lost=      0 torn=    0 wake/s= 950 cpu= 0.21% ns/sample=  205.5
read odr=  10000 got/s=   10000 lost=      0 torn=    0 wake/s=1000 cpu= 0.16% ns/sample=  164.9
mmap odr= 100000 got/s=   99990 lost=      0 torn=    0 wake/s= 947 cpu= 0.38% ns/sample=   38.5
read odr= 100000 got/s=  100000 lost=      0 torn=    0 wake/s=1000 cpu= 0.20% ns/sample=   20.2
mmap odr= 400000 got/s=  399859 lost=      0 torn=    0 wake/s= 941 cpu= 0.94% ns/sample=   23.5
read odr= 400000 got/s=  399996 lost=      0 torn=    0 wake/s=1000 cpu= 0.32% ns/sample=    8.1
mmap odr=1000000 got/s=  993887 lost=  22720 torn=    0 wake/s= 929 cpu= 2.02% ns/sample=   20.3
read odr=1000000 got/s=  999977 lost=      0 torn=    0 wake/s=1000 cpu= 0.57% ns/sample=    5.7
mmap odr=2000000 got/s= 1759155 lost= 961202 torn=19200 wake/s= 916 cpu= 3.37% ns/sample=   19.2
read odr=2000000 got/s= 1919914 lost= 159920 torn=    0 wake/s=1000 cpu= 0.94% ns/sample=    4.9
```

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="40" font-size="13" font-weight="bold">손실률 % (2초 동안)</text>
<line x1="40" y1="300" x2="320" y2="300" stroke="currentColor"/>
<line x1="40" y1="300" x2="40" y2="80" stroke="currentColor"/>
<line x1="36" y1="300.0" x2="40" y2="300.0" stroke="currentColor"/>
<text x="32" y="304.0" font-size="12" text-anchor="end">0</text>
<line x1="36" y1="256.0" x2="40" y2="256.0" stroke="currentColor"/>
<text x="32" y="260.0" font-size="12" text-anchor="end">5</text>
<line x1="36" y1="212.0" x2="40" y2="212.0" stroke="currentColor"/>
<text x="32" y="216.0" font-size="12" text-anchor="end">10</text>
<line x1="36" y1="168.0" x2="40" y2="168.0" stroke="currentColor"/>
<text x="32" y="172.0" font-size="12" text-anchor="end">15</text>
<line x1="36" y1="124.0" x2="40" y2="124.0" stroke="currentColor"/>
<text x="32" y="128.0" font-size="12" text-anchor="end">20</text>
<line x1="36" y1="80.0" x2="40" y2="80.0" stroke="currentColor"/>
<text x="32" y="84.0" font-size="12" text-anchor="end">25</text>
<rect x="46.0" y="300.0" width="16" height="0.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<rect x="64.0" y="300.0" width="16" height="0.5" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="63.3" y="318" font-size="12" text-anchor="middle">1k</text>
<rect x="92.7" y="300.0" width="16" height="0.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<rect x="110.7" y="300.0" width="16" height="0.5" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="110.0" y="318" font-size="12" text-anchor="middle">10k</text>
<rect x="139.3" y="300.0" width="16" height="0.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<rect x="157.3" y="300.0" width="16" height="0.5" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="156.7" y="318" font-size="12" text-anchor="middle">100k</text>
<rect x="186.0" y="300.0" width="16" height="0.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<rect x="204.0" y="300.0" width="16" height="0.5" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="203.3" y="318" font-size="12" text-anchor="middle">400k</text>
<rect x="232.7" y="290.1" width="16" height="9.9" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<text x="240.7" y="286.1" font-size="12" text-anchor="middle">1.1</text>
<rect x="250.7" y="300.0" width="16" height="0.5" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="250.0" y="318" font-size="12" text-anchor="middle">1M</text>
<rect x="279.3" y="111.2" width="16" height="188.8" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<text x="287.3" y="107.2" font-size="12" text-anchor="middle">21</text>
<rect x="297.3" y="264.8" width="16" height="35.2" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="305.3" y="260.8" font-size="12" text-anchor="middle">4</text>
<text x="296.7" y="318" font-size="12" text-anchor="middle">2M</text>
<text x="180.0" y="338" font-size="12" text-anchor="middle">ODR (samples/s)</text>
<text x="350" y="40" font-size="13" font-weight="bold">소비자 CPU % (thread cputime)</text>
<line x1="380" y1="300" x2="660" y2="300" stroke="currentColor"/>
<line x1="380" y1="300" x2="380" y2="80" stroke="currentColor"/>
<line x1="376" y1="300.0" x2="380" y2="300.0" stroke="currentColor"/>
<text x="372" y="304.0" font-size="12" text-anchor="end">0</text>
<line x1="376" y1="245.0" x2="380" y2="245.0" stroke="currentColor"/>
<text x="372" y="249.0" font-size="12" text-anchor="end">1</text>
<line x1="376" y1="190.0" x2="380" y2="190.0" stroke="currentColor"/>
<text x="372" y="194.0" font-size="12" text-anchor="end">2</text>
<line x1="376" y1="135.0" x2="380" y2="135.0" stroke="currentColor"/>
<text x="372" y="139.0" font-size="12" text-anchor="end">3</text>
<line x1="376" y1="80.0" x2="380" y2="80.0" stroke="currentColor"/>
<text x="372" y="84.0" font-size="12" text-anchor="end">4</text>
<rect x="386.0" y="290.1" width="16" height="9.9" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<rect x="404.0" y="291.2" width="16" height="8.8" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="403.3" y="318" font-size="12" text-anchor="middle">1k</text>
<rect x="432.7" y="288.4" width="16" height="11.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<rect x="450.7" y="291.2" width="16" height="8.8" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="450.0" y="318" font-size="12" text-anchor="middle">10k</text>
<rect x="479.3" y="279.1" width="16" height="20.9" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<rect x="497.3" y="289.0" width="16" height="11.0" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="496.7" y="318" font-size="12" text-anchor="middle">100k</text>
<rect x="526.0" y="248.3" width="16" height="51.7" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<rect x="544.0" y="282.4" width="16" height="17.6" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="543.3" y="318" font-size="12" text-anchor="middle">400k</text>
<rect x="572.7" y="188.9" width="16" height="111.1" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<rect x="590.7" y="268.6" width="16" height="31.3" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="590.0" y="318" font-size="12" text-anchor="middle">1M</text>
<rect x="619.3" y="114.7" width="16" height="185.3" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<rect x="637.3" y="248.3" width="16" height="51.7" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="636.7" y="318" font-size="12" text-anchor="middle">2M</text>
<text x="520.0" y="338" font-size="12" text-anchor="middle">ODR (samples/s)</text>
<rect x="200" y="356" width="14" height="12" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><text x="220" y="367" font-size="12">mmap + usleep(1ms) 폴링</text>
<rect x="400" y="356" width="14" height="12" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/><text x="420" y="367" font-size="12">read() 블로킹 (wait queue)</text>
</svg>
```
그림 6 — 측정 차트. **직관과 어긋나는 결과**라 중요하다.

### 5.4 숫자 읽는 법 — "zero-copy가 항상 이긴다"는 틀렸다

이 측정의 교훈이 면접에서 그대로 통한다.

- **낮은~중간 ODR(≤400k)**: 둘 다 손실 0. CPU는 **read()가 오히려 낮다**(ns/sample 8.1 vs 23.5). 왜? 이 장치의 샘플은 32 B라 복사가 싸고, mmap 쪽은 1 ms마다 깨서 `head`를 폴링하는 **폴링 오버헤드**가 있다. read()는 커널이 깨워 줄 때(wake queue)만 돈다.
- **높은 ODR(≥1M)**: mmap 폴링은 1 ms 간격 × 1920슬롯으로는 다 못 퍼내서 **덮어쓰기 손실**이 난다(2M에서 mmap 손실 21%, `torn`=덮어쓰는 중 읽은 슬롯). read()는 블로킹이라 커널 페이스에 맞춰 깨서 손실이 적다.
- **그래서 mmap의 진짜 이점은 "복사 제거"가 아니라 "큰 프레임"이다**. 4 MB 카메라 프레임을 `read()`로 받으면 4 MB 복사가 매 프레임 생기지만, mmap이면 0. 32 B 샘플에선 복사가 애초에 싸서 이점이 안 보인다. mmap 링은 또 **poll()과 묶어** 폴링 대신 블로킹 대기로 써야 CPU가 안 샌다(G6).

정직한 결론: **mmap은 "큰 버퍼 + 이벤트 통지(poll)"와 함께 쓸 때 이긴다. 작은 레코드를 바쁜 폴링으로 긁으면 read()보다 나쁠 수 있다.** 측정 없이 "mmap이 빠르다"고 말하면 안 된다 — Don의 측정 기반 디버깅 습관이 여기서 맞다. (TCG 에뮬레이션 값이라 절대 수치가 아니라 **경향**만 본다.)

---

## 6. 안전하지 않은 mmap — 커널 메모리 누수와 그 수정

### 6.1 예제 6 — 크기 검사를 뺀 `.mmap`이 내주는 것 (일회용 게스트 전용)

무엇을 확인하나: `insecure=1`이면 `.mmap`이 크기·쓰기 검사를 전부 건너뛰고 `remap_pfn_range`만 부른다. 유저가 64 KiB 링보다 **크게** 매핑하면 링 뒤의 커널 페이지까지 유저 주소 공간에 노출되고, 유저가 **쓰기**까지 할 수 있다.

```c
	if (insecure)				/* 일부러 틀린 코드 */
		return remap_pfn_range(vma, vma->vm_start,
				       page_to_pfn(g5_pages) + vma->vm_pgoff,
				       size, vma->vm_page_prot);
```

링 바로 뒤 페이지에 커널이 "비밀"을 심어 두고(`g5_secret = "G5-SECRET calib-key=0xC0FFEE"`), 일반 uid 1000 유저가 128 KiB로 매핑해 본다:

```text
--- insecure=1
uid 1000 mapped 128 KiB RW at 0xffff... (ring is 64 KiB)
page 16 (beyond ring): "G5-SECRET calib-key=0xC0FFEE"
non-zero pages beyond ring+secret: 2 of 15
[    1.869016] g5: secret page now "PWNED-by-uid-1000"
--- fixed (insecure=0)
mmap RW beyond ring                  -> -1  EACCES
mmap PROT_WRITE                      -> -1  EACCES
mmap 128 KiB                         -> -1  EINVAL
mmap offset 4096                     -> -1  EINVAL
```

출력에서 볼 것: `insecure=1`이면 uid 1000이 링 밖 커널 페이지의 비밀 문자열을 **읽고**, 거기에 "PWNED"를 **써서** 커널이 그걸 다시 읽는다(dmesg). 권한 없는 유저가 커널 메모리를 읽고 쓴 것 — CVE급 버그다. `remap_pfn_range`는 pfn만 보고 매핑하므로 "요청 크기가 버퍼보다 큰지"는 **드라이버가 반드시** 검사해야 한다. `insecure=0`(고친 버전)이면 크기 초과 `EINVAL`, 쓰기 `EACCES`로 전부 막힌다.

### 6.2 안전한 `.mmap` 체크리스트

- `size = vm_end - vm_start`가 버퍼 크기 이하인지(`> G5_RING_BYTES` → `-EINVAL`).
- `vm_pgoff`(offset)가 유효 범위인지 — 유저가 임의 offset으로 버퍼 밖을 가리키지 못하게.
- 쓰기가 필요 없으면 `VM_WRITE` 거절 + `vm_flags_clear(VM_MAYWRITE)`.
- MMIO를 매핑할 땐 `pgprot_noncached`, 그리고 **매핑할 레지스터 영역만** — 옆 장치 레지스터가 같은 페이지에 있으면 통째로 노출된다.
- `remap_pfn_range`에 넘기는 pfn이 **내가 소유한** 메모리인지. `virt_to_phys`/`page_to_pfn`을 유저가 준 값으로 계산하지 않는다.

### 6.3 왜 UIO가 존재하나 (미리보기)

레지스터 전체를 유저에 매핑해 "유저 공간 드라이버"를 짜고 싶을 때가 있다(산업 I/O, 고속 거래). 그걸 안전하게 하라고 **UIO**(Userspace I/O) 프레임워크가 있다 — `/dev/uioN`를 mmap하면 정해진 MMIO 영역만 노출하고, 인터럽트는 `read()`로 받는다. 랩 커널엔 `CONFIG_UIO`가 꺼져 있다(`# CONFIG_UIO is not set`). 직접 `.mmap`으로 MMIO를 노출하는 것보다 UIO가 **영역 경계·인터럽트 전달을 표준화**해 안전하다. `/dev/mem`은 `CONFIG_STRICT_DEVMEM=y`라 RAM 영역 접근이 막혀 있어 범용 통로가 못 된다. 자세한 건 UIO 노트에서.

---

## 7. 커널 안에서는 무슨 일이 일어나나 — 한 장 요약

유저의 `ioctl(fd, cmd, arg)`와 `mmap(fd, ...)`이 드라이버까지 오는 길(E6·A5·F11에서 각 구간을 이미 깊게 봤으니 여기선 연결만).

| 유저 호출 | 커널 경로 (6.18) | 드라이버가 만나는 지점 |
| --- | --- | --- |
| ioctl (64비트) | `sys_ioctl` → `do_vfs_ioctl` → `vfs_ioctl` → `f_op->unlocked_ioctl` | `g5_ioctl` (E6 §6) |
| ioctl (32비트 앱) | `el0t_32_sync` → `compat_sys_ioctl` → `compat_ptr_ioctl` → `unlocked_ioctl` | 같은 `g5_ioctl`, arg는 `compat_ptr` 변환 |
| 모르는 cmd | `vfs_ioctl`이 `-ENOIOCTLCMD`를 `-ENOTTY`로 변환 | `default: return -ENOTTY` |
| mmap | `ksys_mmap_pgoff` → `vm_mmap_pgoff` → `do_mmap` → `mmap_region` → `vfs_mmap` → `f_op->mmap` | `g5_mmap(file, vma)` |
| mmap PTE 채움 | `remap_pfn_range`/`vm_insert_pages` (즉시) 또는 `vm_ops->fault` (지연) | VMA에 `VM_IO`/`VM_PFNMAP` 또는 `VM_MIXEDMAP` |
| 첫 접근(lazy) | `do_mem_abort` → `do_page_fault` → `handle_mm_fault` → `vm_ops->fault` | `g5_fault` (F11) |
| 공유 상태 | 유저·커널이 같은 struct page를 봄 | head/tail + `smp_store_release`/`smp_load_acquire` (F5) |

```svg
<svg viewBox="0 0 660 250" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="g5ah4" markerWidth="9" markerHeight="9" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="140" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="90" y="52" font-size="12" text-anchor="middle">ioctl(fd,cmd,arg)</text>
  <rect x="260" y="30" width="160" height="34" fill="#888" fill-opacity="0.1" stroke="#888"/><text x="340" y="52" font-size="11" text-anchor="middle">do_vfs_ioctl → vfs_ioctl</text>
  <rect x="500" y="30" width="140" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="570" y="52" font-size="12" text-anchor="middle">g5_ioctl</text>
  <line x1="160" y1="47" x2="258" y2="47" stroke="currentColor" marker-end="url(#g5ah4)"/>
  <line x1="420" y1="47" x2="498" y2="47" stroke="currentColor" marker-end="url(#g5ah4)"/>
  <rect x="20" y="100" width="140" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="90" y="122" font-size="12" text-anchor="middle">mmap(fd,...)</text>
  <rect x="260" y="100" width="160" height="34" fill="#888" fill-opacity="0.1" stroke="#888"/><text x="340" y="122" font-size="11" text-anchor="middle">do_mmap → vfs_mmap</text>
  <rect x="500" y="100" width="140" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="570" y="122" font-size="12" text-anchor="middle">g5_mmap(vma)</text>
  <line x1="160" y1="117" x2="258" y2="117" stroke="currentColor" marker-end="url(#g5ah4)"/>
  <line x1="420" y1="117" x2="498" y2="117" stroke="currentColor" marker-end="url(#g5ah4)"/>
  <rect x="500" y="160" width="140" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="570" y="182" font-size="11" text-anchor="middle">remap_pfn_range</text>
  <line x1="570" y1="134" x2="570" y2="158" stroke="currentColor" marker-end="url(#g5ah4)"/>
  <rect x="260" y="200" width="380" height="34" fill="#4a7bd0" fill-opacity="0.08" stroke="#4a7bd0"/><text x="450" y="222" font-size="11" text-anchor="middle">유저 PTE ↔ 드라이버 링 (같은 물리 페이지, 복사 없음)</text>
  <line x1="570" y1="194" x2="570" y2="200" stroke="currentColor" marker-end="url(#g5ah4)"/>
</svg>
```
그림 7 — ioctl과 mmap 두 경로가 드라이버의 `g5_ioctl`/`g5_mmap`으로 들어오는 전체 그림.

---

## 8. Embedded 실무에서 다시 보기

### 8.1 IMU FIFO를 mmap 링으로 (IIO가 하는 일)

현장에서 IMU는 보통 IIO 서브시스템으로 노출한다. IIO의 **버퍼**가 바로 이 노트의 mmap 링이다 — `/dev/iio:deviceN`를 열고 `read()` 또는 mmap으로 샘플 블록을 받고, 워터마크 인터럽트를 `poll()`로 기다린다. 우리가 손으로 짠 head/tail·`remap_pfn_range`·배리어를 IIO가 내부에서 한다. 직접 char device로 짤 일은 드물지만, **IIO 드라이버를 디버깅하거나 성능 튜닝할 때** 이 내부를 알아야 한다.

### 8.2 카메라 프레임 — V4L2 MMAP과 dma-buf

Jetson/i.MX8 카메라는 `VIDIOC_REQBUFS`로 버퍼를 잡고 `mmap(fd, offset=버퍼쿠키)`로 각 프레임을 유저에 노출한다(A5 §12.5). 커널 쪽 `.mmap`이 `dma_mmap_coherent`(DMA 버퍼라 캐시 속성 일치 필수, F10)로 매핑한다. 4 MB 프레임을 `read()`로 복사하면 프레임당 4 MB 복사 + 지연 — 여기서 §5.4의 "큰 버퍼라 mmap이 확실히 이긴다"가 성립한다. GPU/NPU로 넘길 땐 `VIDIOC_EXPBUF`로 dma-buf fd를 받아 복사 없이 공유한다(H9).

### 8.3 캘리브레이션 ioctl — 버전·예약·권한

E6 §9.2에서 본 캘리브레이션 업로드가 여기서 `G5_IOC_FACTORY_CALIB`다. 설계 그대로: `CAP_SYS_ADMIN` 검사 먼저 → `reserved[]` 0 검사 → 길이 상한을 `memdup_user` **전에**. 몇 년간 앱과 커널이 따로 업데이트되니 `copy_struct_from_user`로 뒤에만 확장한다.

### 8.4 "개발 보드에선 됐는데 양산 SoC에서 유저가 커널을 덮어썼다"

§6의 크기 검사 누락이 현장에서 나는 전형이다. 개발 중엔 유저가 정확한 크기로만 mmap해서 안 터지다가, 악의적(또는 버그 있는) 앱이 큰 크기로 매핑하면 커널 메모리가 샌다. 보안 리뷰에서 `.mmap` 핸들러의 크기·offset 검사는 1순위 점검 항목이다.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
| --- | --- | --- | --- |
| `.mmap`에서 size/offset 미검사 | 유저가 커널 메모리 read/write | `remap_pfn_range`는 pfn만 봄 | `size > buf` → `-EINVAL`, offset 범위 검사 |
| 예약 비트/필드 0 검사 안 함 | 나중에 그 비트 의미 줄 때 옛 쓰레기 값과 충돌 | 확장 여지를 안 지킴 | `reserved != 0` 또는 `flags & ~KNOWN` → `-EINVAL` |
| 모르는 cmd에 `-EINVAL` 반환 | 유저가 "지원 안 됨"과 "인자 틀림"을 구분 못 함 | 관례 위반 | `default: return -ENOTTY` |
| ioctl 구조체에 `long`/포인터 | 32비트 앱에서 레이아웃 깨짐 | 크기가 ABI마다 다름 | 고정 폭 타입, 포인터는 `__u64` (E6 §6) |
| `.compat_ioctl` 미설정 | 32비트 앱 ioctl이 `-ENOTTY` | compat 진입 경로 없음 | ABI 독립이면 `compat_ptr_ioctl` |
| DMA 버퍼를 `remap_pfn_range`로 | 간헐적 데이터 깨짐 | 커널·유저 캐시 속성 불일치 | `dma_mmap_coherent` (F10) |
| head만 쓰고 배리어 없음 | 유저가 안 쓴 슬롯을 읽음 | 쓰기 재배치 | `smp_store_release`/`smp_load_acquire` (F5) |
| `.mmap`에서 `mmap_lock` 잡고 `copy_to_user` | 데드락/lockdep 경고 | fault가 같은 락 요구 | `.mmap` 안에서 유저 메모리 접근 금지 (F11) |
| 권한 검사를 데이터 접근 뒤에 | 권한 없는 호출이 side effect 남김 | 순서 틀림 | `capable()`를 맨 앞에 |

---

## 10. 면접에서 이렇게 말한다

**Q.** When would you use ioctl versus sysfs versus a read/write interface for a device?

**A.** 데이터 모양·빈도·원자성으로 가른다. 순차 스트림은 read/write, 사람이 셸로 만지는 단일 튜닝 값은 sysfs 한 파일, 여러 값을 원자적으로 바꾸거나 START/STOP 같은 트랜잭션은 ioctl, 대량 고속 데이터는 mmap. 가능하면 ioctl을 새로 만들지 말고 IIO·V4L2 같은 서브시스템을 쓴다.

> I pick by data shape and atomicity. A byte stream is read/write. A single human-tunable value is one sysfs file. Several fields that must change together, or a start/stop transaction, is an ioctl. Bulk high-rate data is mmap. And before inventing an ioctl I check whether a subsystem like IIO or V4L2 already defines the ABI.

**꼬리 질문**: Why does the community discourage new ioctls? → 번호·구조체가 드라이버마다 제각각이라 툴 검사가 안 되고, 32/64 compat을 손으로 챙겨야 하고, 한 번 공개하면 영원히 못 깬다.

**Q.** How do you design an ioctl struct that won't break when a 32-bit app talks to a 64-bit kernel?

**A.** 고정 폭 타입(`__u32`/`__u64`)만 쓰고, `long`·포인터·`enum`·`size_t`는 금지, 유저 포인터는 `__u64`로 싣는다. 명시적 패딩으로 구멍을 없애 모든 ABI에서 같은 레이아웃·같은 크기가 되게 하고, pahole로 확인한다. 그러면 `.compat_ioctl = compat_ptr_ioctl` 하나로 32비트 앱을 받는다.

> Fixed-width types only, no long or pointers or enums in the struct, user pointers carried as __u64. Explicit padding so there are no holes and the layout is identical on arm64, arm32 and i386 — I verify with pahole. Then compat_ptr_ioctl handles 32-bit callers because the struct is ABI-independent.

**꼬리 질문**: What if the struct must contain a pointer or a timespec? → `compat_ptr_ioctl`로는 안 되고 `.compat_ioctl`에 직접 변환 함수를 짠다.

**Q.** How do you make an ioctl ABI extensible?

**A.** 새 필드는 반드시 구조체 **맨 뒤에만** 덧붙이고, "0 = 옛 동작"이 되게 의미를 준다. 커널은 `copy_struct_from_user(dst, ksize, src, usize)`로 유저가 보낸 크기를 받아, 작으면 나머지를 0으로 채우고 크면 꼬리가 0인지 검사해 아니면 `-E2BIG`. ioctl이면 번호의 size 필드(`_IOC_SIZE`)가 usize를 준다.

> New fields only at the tail, with zero meaning the old behavior. The kernel uses copy_struct_from_user with the user-supplied size: it zero-fills when the user struct is smaller and rejects with E2BIG when the extra tail bytes aren't zero. For ioctls the size lives in the command number, read with _IOC_SIZE.

**Q.** What does a driver's mmap handler actually do, and what are the ways to populate the mapping?

**A.** VMA를 받아 크기·offset을 검사하고 캐시 속성(`vm_page_prot`)을 정한 뒤 PTE를 채운다. page 할당 버퍼나 MMIO는 `remap_pfn_range`(pfn 넘김, VMA에 VM_IO/VM_PFNMAP), struct page 있는 RAM은 `vm_insert_pages`, DMA 버퍼는 캐시 속성을 맞춰 주는 `dma_mmap_coherent`. 지연 매핑은 `vm_ops->fault`로 접근할 때 페이지를 하나씩 준다.

> It receives the VMA, validates size and offset, sets vm_page_prot, and populates page tables. For page-allocated or MMIO memory, remap_pfn_range by pfn, which marks the VMA VM_IO and VM_PFNMAP. For normal pages with a struct page, vm_insert_pages. For coherent DMA buffers, dma_mmap_coherent so kernel and user mappings share cache attributes. For lazy mapping, a vm_ops fault handler supplies pages on demand.

**Q.** Why is a missing size check in an mmap handler a security hole?

**A.** `remap_pfn_range`는 넘긴 pfn부터 요청한 크기만큼 그냥 매핑한다. 유저가 버퍼보다 크게 매핑 요청하면 버퍼 뒤의 커널 페이지가 유저 주소 공간에 들어오고, 쓰기까지 열려 있으면 권한 없는 유저가 커널 메모리를 읽고 쓴다. `size > buffer` 검사와 offset 범위 검사, 필요 없으면 VM_WRITE 거절이 필수다. 직접 봤는데 uid 1000이 링 뒤 커널 페이지의 비밀을 읽고 덮어썼다.

> remap_pfn_range maps exactly the size you give starting at that pfn. If you don't clamp the requested size to the buffer, the user maps kernel pages past the buffer, and if writable, an unprivileged user reads and writes kernel memory. I demonstrated this — uid 1000 read and overwrote a secret page just past the ring. The fix is a size check, offset validation, and rejecting VM_WRITE when not needed.

**Q.** Is mmap always faster than read for a device? 

**A.** 아니다. mmap의 이점은 **복사 제거**라 샘플이 클 때(카메라 4 MB 프레임) 확실히 이긴다. 32 B IMU 샘플은 복사가 싸서, mmap을 바쁜 폴링으로 긁으면 오히려 read() 블로킹보다 CPU가 더 들고 손실도 날 수 있다 — 직접 측정했다. mmap 링은 poll()과 묶어 블로킹 대기로 써야 제값을 한다. 측정 없이 "mmap이 빠르다"고 말하면 안 된다.

> No. The win is eliminating the copy, so it clearly helps for large payloads like a 4MB camera frame. For a 32-byte IMU sample the copy is cheap, and busy-polling an mmap ring can cost more CPU than a blocking read and even drop samples — I measured exactly that. An mmap ring pays off when paired with poll() for blocking wakeups, not busy polling.

**Q.** Why return ENOTTY for an unknown ioctl command?

**A.** 관례다. `-ENOTTY`("inappropriate ioctl for device")가 "이 장치는 그 명령을 모른다"를 뜻하고, `-EINVAL`은 "명령은 알지만 인자가 틀렸다"를 뜻한다. 유저 라이브러리가 둘을 구분해 "이 기능 지원 안 함 vs 호출 잘못"을 판단한다. 커널 내부 `-ENOIOCTLCMD`는 VFS가 `-ENOTTY`로 바꿔 유저에 준다.

> Convention. ENOTTY means the device doesn't know that command, while EINVAL means it knows the command but the argument is wrong. User libraries rely on that to distinguish "feature unsupported" from "bad call." Internally the driver can return ENOIOCTLCMD and the VFS translates it to ENOTTY.

**Q.** How do user and kernel stay consistent on a shared mmap ring buffer?

**A.** 단일 생산자(커널)/단일 소비자(유저) 링이면 head/tail 인덱스를 공유 페이지에 두고, 커널은 샘플을 다 쓴 뒤 `smp_store_release`로 head를 올리고, 유저는 `smp_load_acquire`로 head를 읽은 뒤 그 아래 슬롯만 읽는다. 이 release/acquire가 "슬롯 내용이 head보다 먼저 보이는" 순서를 보장한다. 유저가 느려 head-tail이 링 크기를 넘으면 덮어쓰기(overrun)니 유저가 tail을 점프하고 손실을 기록한다.

> Single-producer single-consumer: head and tail live in the shared page. The kernel writes the slot then does smp_store_release on head; the user does smp_load_acquire on head then reads only slots below it. That release/acquire pair orders the slot contents before the head update. If the user falls behind and head minus tail exceeds the ring size, that's an overrun, so the user jumps tail forward and counts the loss.

---

## 11. 직접 해보기

1. **개념**: `_IOW(magic, 2, struct foo)`에서 `sizeof(struct foo)`를 16→24로 늘리면 ioctl 번호의 어느 비트가 바뀌나? 커널이 그걸 어떻게 읽나? / 정답: size 필드(bit 16~29)가 바뀐다. 커널은 `_IOC_SIZE(cmd)`로 읽는다(예제 1의 `0x4010...` vs `0x4018...`).

2. **개념**: 유저가 `.mmap`으로 받은 영역을 `mprotect`로 쓰기 가능하게 바꾸려 한다. 드라이버가 어떻게 막나? / 정답: `vm_flags_clear(vma, VM_MAYWRITE)` — MAYWRITE가 없으면 커널이 PROT_WRITE로의 mprotect를 `-EACCES`로 거절한다(예제 3에서 확인).

3. **코드**: `G5_IOC_GET_STATS` ioctl(`_IOR`)을 추가해 `produced`·`lazy_faults`·현재 ODR를 구조체로 돌려줘라. GET_INFO와 중복을 피하려면 어떻게 나눌까? / 힌트: 변하지 않는 정보(GET_INFO)와 계속 변하는 카운터(GET_STATS)를 나눈다.

4. **코드**: `.mmap`에 `vm_iomap_memory` 버전을 추가하고 `remap_pfn_range` 버전과 결과(VmFlags, fault 수)를 비교하라. / 힌트: `vm_iomap_memory(vma, phys, len)`은 offset·크기 검사를 내부에서 해 준다(랩 `mm/memory.c:3095`).

5. **관찰**: 예제 3을 돌리고 다른 셸에서 `cat /proc/<pid>/smaps`로 그 VMA의 `Rss`·`Pss`·`VmFlags`를 보라. REMAP·INSERT·LAZY에서 어떻게 다른가? / 힌트: LAZY는 건드리기 전엔 Rss가 0, PFNMAP(REMAP)은 Rss에 안 잡힐 수 있다.

6. **관찰**: `strace -e ioctl ./g5_t1_ioctl`로 각 ioctl의 cmd 번호와 반환값을 보라. `strace`가 번호를 어떻게 해석하나? / 힌트: strace가 `_IOC` 디코딩을 일부 해 준다. 모르는 번호는 raw로 찍힌다.

7. **코드(보안)**: 예제 6의 `insecure` 경로에서 size 검사만 추가하고 offset 검사는 빼면 어떤 공격이 남나? / 정답: `vm_pgoff`를 크게 줘서 `page_to_pfn(g5_pages) + vm_pgoff`가 버퍼 밖을 가리키게 할 수 있다 — offset도 반드시 검사.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
| --- | --- | --- |
| UAPI | user API 헤더 | 유저·커널이 같이 include하는 ABI 계약, `include/uapi/` |
| `copy_struct_from_user` | 크기 인지 복사 | usize/ksize로 구조체 확장 호환을 맞추는 헬퍼 |
| `compat_ptr_ioctl` | compat 헬퍼 | 32비트 포인터 arg를 넓혀 `unlocked_ioctl` 재호출 |
| ENOTTY | errno 25 | "이 장치는 그 ioctl 명령을 모른다" |
| VMA | `vm_area_struct` | 프로세스 주소 공간의 한 매핑 구간(A5) |
| `remap_pfn_range` | 커널 함수 | pfn부터 size만큼 유저 PTE를 즉시 채움, VM_IO/VM_PFNMAP |
| `vm_insert_pages` | 커널 함수 | struct page 배열을 유저에 매핑, VM_MIXEDMAP |
| `dma_mmap_coherent` | 커널 함수 | DMA 버퍼를 커널과 같은 캐시 속성으로 매핑(F10) |
| `vm_page_prot` | VMA 필드 | PTE에 들어갈 캐시·권한 속성 |
| `pgprot_noncached` | prot 매크로 | MMIO용 Device-nGnRnE 속성(arm64) |
| `vm_operations_struct.fault` | VMA 콜백 | 접근 시 페이지를 하나씩 주는 지연 매핑 |
| VM_PFNMAP | VMA 플래그 | struct page 없이 pfn으로만 관리되는 매핑 |
| head/tail | 링 인덱스 | 생산자/소비자 위치, release/acquire로 동기화(F5) |
| UIO | Userspace I/O | MMIO를 안전하게 유저에 노출하는 프레임워크 |

---

## 13. 요약 & 체크리스트

한 장치를 유저에 노출할 때 "제어는 어디로, 데이터는 어디로"를 먼저 가른다: 순차 스트림은 read/write, 단일 튜닝 값은 sysfs, 여러 값 원자 설정·START/STOP은 ioctl, 대량 고속 데이터는 mmap, 이벤트는 poll. 가능하면 IIO·V4L2 같은 서브시스템을 쓴다. ioctl UAPI는 고정 폭 타입·명시적 패딩·예약 비트 0 검사·`copy_struct_from_user`로 10년 뒤 32비트 레거시 앱도 안 깨지게 설계하고, ABI 독립이면 `compat_ptr_ioctl` 하나로 32비트를 받는다. mmap `.mmap` 핸들러는 크기·offset을 반드시 검사하고(안 하면 커널 메모리 누수) 캐시 속성을 정한 뒤 `remap_pfn_range`/`vm_insert_pages`/`dma_mmap_coherent`/`.fault` 중에 메모리 출처에 맞게 PTE를 채운다. "mmap이 항상 빠르다"는 틀렸다 — 큰 버퍼 + poll일 때 이기고, 작은 레코드 바쁜 폴링은 read()보다 나쁠 수 있다(직접 측정).

- [ ] read/write·ioctl·sysfs·mmap·poll·netlink·configfs를 데이터 모양·빈도·원자성으로 고를 수 있다
- [ ] "ioctl은 최후의 수단, 가능하면 서브시스템"이라는 관례와 그 이유를 설명할 수 있다
- [ ] 고정 폭 타입·명시적 패딩·예약 비트 0 검사로 ABI 독립 UAPI 구조체를 설계할 수 있다
- [ ] `copy_struct_from_user`의 세 경우(verbatim/zero-fill/E2BIG)와 뒤에만 확장하는 규칙을 설명할 수 있다
- [ ] `unlocked_ioctl`/`compat_ioctl`의 차이와 `compat_ptr_ioctl`이 충분한 조건을 안다
- [ ] 모르는 cmd는 `-ENOTTY`, 특권 명령은 `capable()`를 맨 앞에 두는 이유를 안다
- [ ] `.mmap` 핸들러가 받는 VMA 필드(vm_start/end, vm_pgoff, vm_page_prot, vm_flags)를 쓸 수 있다
- [ ] `remap_pfn_range`/`vm_insert_pages`/`dma_mmap_coherent`/`.fault`를 메모리 출처에 맞게 고를 수 있다
- [ ] MMIO·WC·캐시 가능 버퍼에 맞는 `pgprot_*`를 고를 수 있다
- [ ] `.mmap`의 size/offset 미검사가 왜 커널 메모리 누수인지 코드로 보이고 고칠 수 있다
- [ ] mmap 공유 링의 head/tail을 `smp_store_release`/`smp_load_acquire`로 동기화할 수 있다(F5)
- [ ] "mmap이 항상 빠르다"가 틀린 이유를 측정으로 설명할 수 있다

---

## 참고 자료

- docs.kernel.org: "ioctl based interfaces"(`Documentation/driver-api/ioctl.rst`), `Documentation/userspace-api/ioctl/ioctl-number.rst`, `Documentation/filesystems/vfs.rst`(`file_operations`)
- Corbet, Rubini, Kroah-Hartman, `Linux Device Drivers 3rd` — ch.6 "Advanced Char Driver Operations"(ioctl), ch.15 "Memory Mapping and DMA"(`remap_pfn_range`, `vm_operations`) — 오래됐지만 mmap 원리는 유효
- elixir.bootlin.com (linux v6.18): `fs/ioctl.c`(`vfs_ioctl`, `compat_ptr_ioctl`), `include/linux/uaccess.h`(`copy_struct_from_user`), `mm/memory.c`(`remap_pfn_range`, `vm_insert_pages`, `vmf_insert_pfn`), `mm/util.c`(`vm_mmap_pgoff`), `arch/arm64/include/asm/pgtable.h`(`pgprot_*`), `drivers/uio/uio.c`, `sound/core/pcm_native.c`(`.fault` 예)
- man7.org: `ioctl(2)`, `mmap(2)`, `capabilities(7)`
- LWN: "The mmap_prepare() file operation" 류의 VMA API 변화 기사(6.x 흐름 확인용)
- 선행·연관 노트: A5(유저 mmap·VMA·fault), E6(ioctl 번호·copy_to/from_user·errno·compat), F5(배리어·release/acquire), F9(페이지 할당·split_page), F10(dma_alloc_coherent·reserved-memory), G4(char device), G6(poll/select), G8(sysfs/debugfs/configfs), H9(dma-buf), I(IIO)
