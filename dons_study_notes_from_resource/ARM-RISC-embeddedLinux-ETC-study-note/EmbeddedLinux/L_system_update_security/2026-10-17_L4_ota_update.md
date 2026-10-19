# L4. OTA 업데이트 — A/B 파티션 · bootcount 롤백 · RAUC/SWUpdate/Mender

> **이 노트를 다 읽으면**: OTA가 지켜야 할 요구사항(원자성 · 전원 차단 내성 · 롤백 · 진위 · 대역폭 · 다운타임 · 저장 비용)으로 전략(single-copy+recovery, A/B, OSTree, 컨테이너, delta)을 고를 수 있다 · "비활성 슬롯에 쓰기 → 한 번만 시도 → bootcount → health check → commit 아니면 altbootcmd 롤백"이라는 boot-commit 프로토콜을 U-Boot 소스와 실제 콘솔 로그로 설명할 수 있다 · RAUC 번들을 직접 서명·검증·변조해 보고, 게스트 안에서 설치 → 시험 부팅 → `mark-good` 확정, 깨진 업데이트 → 자동 롤백, 쓰기 도중 전원 차단 → 기존 슬롯 부팅을 재현할 수 있다 · "1만 대 필드 장비 OTA를 설계하라"에 구조를 갖춰 답할 수 있다
> **왜 필요한가**: 필드 장비(드론 · 로봇 · 카메라)를 다루는 회사의 Embedded Linux 면접에서 거의 반드시 나오는 설계 질문이다 — "업데이트 중에 전원이 나가면?", "업데이트가 '성공'이라는 건 누가 어떻게 판단하나?", "부트로더는 어떻게 업데이트하나?". 실무에서는 첫 양산 전에 반드시 누군가 이걸 설계해야 하고, 잘못하면 장비 회수(RMA)로 돌아온다
> **Don 기준 난이도**: SSD 펌웨어의 A/B 펌웨어 슬롯 · 활성 슬롯 전환(NVMe Firmware Commit) · 메타데이터 이중 기록 · 전원 차단 시험은 Don이 장치 쪽에서 해 본 일이다 / 새로 배울 부분은 같은 생각이 **Linux 시스템 전체(부트로더 env · 커널 · rootfs · 데이터 · 서버 · 수천 대 배포)**로 커질 때 생기는 문제와 그걸 해결하는 도구(RAUC · SWUpdate · Mender)
> **선행 노트**: C3(U-Boot 사용 — env, `fw_setenv`), C2 §10(부트 미디어와 부트로더 이중화), J3(read-only root, dm-verity), J5(전원 차단 안전성 — fsync · rename), K3–K6(이미지 빌더). 같이 보면 좋은 노트: L1(systemd — 서비스 · watchdog), L5(secure boot · 서명 체인), L8(컨테이너 업데이트)

---

## 0. 큰 그림 — "업데이트"가 아니라 "되돌릴 수 있는 전환"

Don이 SSD 펌웨어에서 해 본 펌웨어 업데이트를 떠올려 보자. NVMe에는 `Firmware Image Download`와 `Firmware Commit`이 따로 있다. 호스트는 새 이미지를 **비활성 슬롯**에 조각조각 내려보내고(download), 다 받은 뒤 commit 명령으로 "다음 리셋부터 이 슬롯을 써라"를 지시한다. 컨트롤러는 새 이미지의 서명과 CRC를 확인하고, 활성 슬롯 포인터를 **한 번의 원자적 메타데이터 기록**으로 바꾼다. 새 펌웨어가 부팅에 실패하면 ROM/부트로더가 이전 슬롯으로 돌아간다. 다운로드 도중 전원이 나가도 활성 슬롯은 손대지 않았으니 장치는 그대로 산다.

Embedded Linux의 OTA(Over-The-Air update)는 **정확히 같은 생각**이다. 다만 대상이 커진다.

- 슬롯 하나가 펌웨어 2 MB가 아니라 **rootfs 수백 MB ~ 수 GB**(커널 · 라이브러리 · 앱 전부)다.
- "활성 슬롯 포인터"는 컨트롤러 메타데이터가 아니라 **U-Boot 환경 변수**(또는 GRUB env, EFI 변수, eMMC boot partition 설정)다.
- "부팅 성공"의 판정이 어렵다. 커널이 떴다고 성공이 아니다. 카메라 드라이버가 올라오고, 앱이 서버에 붙고, 센서 데이터가 흐를 때 성공이다. 그 판정은 **userspace의 health check**가 한다.
- 장비 한 대가 아니라 **수천 ~ 수만 대**다. 서버, 단계적 배포, 실패율 모니터링, 서명 키 관리가 따라온다.

이 노트의 실습 랩 전체를 한 장으로 보면 이렇다. QEMU `virt` 보드에 U-Boot(직접 빌드, bootcount 활성) → GPT 디스크(rootA · rootB · data) → Ubuntu 24.04 + systemd rootfs 두 벌을 올리고, env는 NOR flash(pflash1)에 둔다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="20" y="24" font-size="14">NOR flash (QEMU pflash1, 64 MiB)</text>
  <rect x="20" y="34" width="150" height="54" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="95" y="56" text-anchor="middle" font-size="13">U-Boot env</text>
  <text x="95" y="76" text-anchor="middle" font-size="12">0x4000000, 256 KiB</text>
  <rect x="170" y="34" width="150" height="54" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="245" y="56" text-anchor="middle" font-size="13">(redund 사본)</text>
  <text x="245" y="76" text-anchor="middle" font-size="12">0x4040000</text>
  <text x="360" y="52" font-size="12">boot_slot=A · upgrade_available=0</text>
  <text x="360" y="70" font-size="12">bootcount=0 · bootlimit=3</text>
  <text x="360" y="88" font-size="12">l4_state_A=good · l4_state_B=good</text>
  <text x="20" y="130" font-size="14">virtio 디스크 (GPT, 3000 MiB)</text>
  <rect x="20" y="140" width="190" height="64" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="164" text-anchor="middle" font-size="13">p1 rootA (900 MiB)</text>
  <text x="115" y="186" text-anchor="middle" font-size="12">ext4 · /boot/Image 포함</text>
  <rect x="210" y="140" width="190" height="64" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="305" y="164" text-anchor="middle" font-size="13">p2 rootB (900 MiB)</text>
  <text x="305" y="186" text-anchor="middle" font-size="12">ext4 · /boot/Image 포함</text>
  <rect x="400" y="140" width="260" height="64" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="530" y="164" text-anchor="middle" font-size="13">p3 data (1.2 GiB)</text>
  <text x="530" y="186" text-anchor="middle" font-size="12">번들 · rauc.status · 앱 데이터</text>
  <rect x="20" y="248" width="170" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="105" y="270" text-anchor="middle" font-size="13">U-Boot</text>
  <text x="105" y="288" text-anchor="middle" font-size="12">bootcount · altbootcmd</text>
  <rect x="270" y="248" width="150" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="345" y="270" text-anchor="middle" font-size="13">Linux + systemd</text>
  <text x="345" y="288" text-anchor="middle" font-size="12">l4-health.service</text>
  <rect x="520" y="248" width="140" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="590" y="270" text-anchor="middle" font-size="13">RAUC 서비스</text>
  <text x="590" y="288" text-anchor="middle" font-size="12">install · mark-good</text>
  <line x1="190" y1="273" x2="266" y2="273" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="228" y="264" text-anchor="middle" font-size="12">booti</text>
  <line x1="420" y1="273" x2="516" y2="273" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="468" y="264" text-anchor="middle" font-size="12">D-Bus</text>
  <line x1="90" y1="248" x2="90" y2="92" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
  <line x1="590" y1="248" x2="160" y2="92" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
  <text x="430" y="234" font-size="12">fw_setenv (libubootenv)</text>
</svg>
```

그림 1 — 이 노트의 랩 구성. 슬롯 선택 상태(boot_slot · upgrade_available · bootcount)는 NOR flash의 U-Boot env에 있고(redundant 사본은 U-Boot 단독 실험 §4.6~4.7에서 쓰고, Linux까지 엮은 시나리오는 QEMU 제약 때문에 single-copy로 돌렸다 — §5.5), U-Boot와 Linux(RAUC의 custom backend가 부르는 `fw_setenv`)가 **같은 블록**을 읽고 쓴다. 커널은 각 rootfs 슬롯의 `/boot/Image`에 같이 들어 있어서 "커널 + rootfs"가 한 단위로 바뀐다.

bare-metal로 비유하면 이렇다.

| Don이 아는 것 (SSD FW) | Embedded Linux OTA |
|---|---|
| FW slot 1 / slot 2 | rootfs 파티션 A / B (커널 포함) |
| Firmware Image Download | RAUC/SWUpdate가 번들을 받아 비활성 슬롯에 기록 |
| Firmware Commit (activate on next reset) | `fw_setenv boot_slot=B upgrade_available=1` |
| 부트 ROM이 슬롯 헤더 CRC · 서명 확인 | 번들 CMS 서명 + dm-verity / sha256 (L5) |
| 새 FW 부팅 실패 시 이전 슬롯 | U-Boot `bootcount` > `bootlimit` → `altbootcmd` |
| 메타데이터 이중 기록(A/B copy + seq) | U-Boot redundant env (두 벌 + flag 바이트) |
| 전원 차단 시험(PLP 랙) | QEMU kill -9 / 전원 릴레이로 업데이트 도중 차단 |

---

## 1. OTA가 지켜야 할 일곱 가지 요구사항

"새 파일을 내려받아 덮어쓴다"는 개발 보드에서는 되지만 필드에서는 안 된다. 면접에서 OTA 설계를 물으면 먼저 **요구사항을 나열**하고 시작하는 것이 좋다. 그래야 뒤의 선택(A/B냐 recovery냐, full이냐 delta냐)이 근거를 가진다.

| 요구사항 | 뜻 | 깨지면 어떻게 되나 | 대표 해법 |
|---|---|---|---|
| 원자성 (atomicity) | 장비는 "전부 옛 버전" 또는 "전부 새 버전" 둘 중 하나로만 부팅한다 | 라이브러리는 새것, 앱은 옛것 → 이상한 크래시 | 이미지 단위 교체 + 마지막 한 번의 포인터 전환 |
| 전원 차단 내성 | 업데이트의 어느 순간에 전원이 나가도 부팅 가능 | 벽돌 → 현장 출동 · RMA | 활성 슬롯은 절대 안 건드림, env 이중화 |
| 롤백 | 새 버전이 부팅 · 동작에 실패하면 자동으로 이전 버전 | 원격으로 고칠 방법이 없음 | bootcount + health check + commit |
| 진위 · 무결성 | 서명된 우리 이미지만 설치된다 | 공격자가 펌웨어를 바꿈 | CMS/X.509 서명, dm-verity, anti-rollback (L5) |
| 대역폭 | LTE · 위성 링크 비용 | 장비당 수백 MB × 1만 대 | delta, 압축, 청크 dedupe |
| 다운타임 | 업데이트 중 장비가 일을 못 하는 시간 | 로봇이 30분 멈춤 | 백그라운드로 비활성 슬롯에 쓰고 재부팅 1회만 |
| 저장 비용 | 슬롯 두 벌 + 번들 임시 공간 | eMMC 8 GB에 rootfs 3 GB면 A/B 불가 | 비대칭(recovery), 스트리밍 설치, 압축 rootfs |

한 줄로 요약하면 **"활성 슬롯은 건드리지 않는다, 전환은 마지막에 한 번, 성공 판정은 부팅 후 userspace가, 실패하면 부트로더가 되돌린다"**이다. 이 노트 전체가 이 문장의 각 단어를 실제로 구현해 보는 것이다.

### 1.1 흔한 함정

- **"패키지 매니저(apt)로 업데이트하면 되지 않나?"** — apt는 패키지 수백 개를 하나씩 풀고 maintainer script를 돌린다. 도중에 전원이 나가면 반쯤 설치된 상태가 된다(K6 §7.2에서 본 dpkg 상태 DB). 원자성이 없다. 개발 장비나 Jetson 같은 "서버형" 장비에서는 쓰지만, 필드 장비의 기본 경로로는 피한다.
- **"rootfs만 바꾸면 끝"** — 커널, DTB, 부트로더, 펌웨어 blob(Wi-Fi · 모뎀 · MCU), 데이터 스키마가 같이 움직여야 할 때가 많다. 무엇이 슬롯 안에 들어가고(같이 롤백되고) 무엇이 밖에 있는지(롤백 안 됨)를 먼저 정해야 한다(§12).

---

## 2. 업데이트 전략 — 무엇을 얼마나 두 벌로 둘까

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="14">① single-copy + recovery (비대칭)</text>
  <rect x="20" y="32" width="90" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="65" y="57" text-anchor="middle" font-size="12">recovery</text>
  <rect x="110" y="32" width="380" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="300" y="57" text-anchor="middle" font-size="12">rootfs (하나)</text>
  <rect x="490" y="32" width="170" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="575" y="57" text-anchor="middle" font-size="12">data</text>
  <text x="20" y="92" font-size="12">작은 recovery(initramfs)로 재부팅해 rootfs를 덮어씀 · 저장 공간 최소 · 업데이트 중 장비 정지</text>
  <text x="20" y="128" font-size="14">② symmetric A/B</text>
  <rect x="20" y="138" width="235" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="137" y="163" text-anchor="middle" font-size="12">rootfs A (활성)</text>
  <rect x="255" y="138" width="235" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="372" y="163" text-anchor="middle" font-size="12">rootfs B (여기에 기록)</text>
  <rect x="490" y="138" width="170" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="575" y="163" text-anchor="middle" font-size="12">data</text>
  <text x="20" y="198" font-size="12">동작 중에 비활성 슬롯에 쓰고 재부팅 1회 · 롤백 즉시 · 저장 공간 2배</text>
  <text x="20" y="234" font-size="14">③ 파일 단위 (OSTree · 이미지 내 하드링크 트리)</text>
  <rect x="20" y="244" width="470" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="255" y="269" text-anchor="middle" font-size="12">/ostree/repo — 객체 저장소 (deploy 1 · deploy 2가 객체를 공유)</text>
  <rect x="490" y="244" width="170" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="575" y="269" text-anchor="middle" font-size="12">/var (data)</text>
  <text x="20" y="304" font-size="12">바뀐 파일만 저장 · 전송 · 파일시스템 하나 · 부트로더 항목 전환으로 원자성</text>
  <text x="20" y="338" font-size="12">④ 컨테이너: 베이스 OS는 ①~③ 중 하나, 앱은 이미지 레이어로 따로 교체 (L8)</text>
</svg>
```

그림 2 — 네 가지 기본 전략. 실제 제품은 이것들을 섞는다. 예: "베이스 OS는 A/B, 앱은 컨테이너", "A/B + 작은 recovery 파티션(둘 다 깨졌을 때 마지막 보루)".

| 전략 | 원자성 · 롤백 | 업데이트 중 다운타임 | 저장 비용 | 대표 도구 · 사례 |
|---|---|---|---|---|
| single-copy + recovery | recovery가 다시 쓰는 동안 실패하면 recovery로 재진입 (롤백 대상 없음) | 길다 (recovery 부팅 + 전체 기록) | 최소 (recovery 수십 MB) | SWUpdate "single copy" 모드, Android 옛 recovery |
| symmetric A/B | 매우 좋음 (포인터 전환, 이전 슬롯 그대로) | 재부팅 1회 | rootfs × 2 | RAUC, SWUpdate dual copy, Mender, Android A/B |
| asymmetric (A/B + recovery) | 좋음 + 둘 다 깨지면 recovery | 재부팅 1회 | rootfs × 2 + recovery | 국방 · 의료 장비, 큰 플릿 |
| 파일 단위 (OSTree) | 좋음 (deploy 전환) | 재부팅 1회 | 공유 객체라 작다 | OSTree/libostree, Fedora IoT, Torizon OS |
| 컨테이너 | 앱만 원자적, 베이스 OS는 따로 | 앱 재시작 | 이미지 레이어 공유 | Podman/Docker, balena, AWS Greengrass (L8) |
| delta (어느 방식에든 덧붙임) | 원본 슬롯이 정확해야 적용 가능 | 같음 | 같음 (전송량만 줄임) | rdiff, zchunk, casync/desync, RAUC adaptive, Mender delta |

### 2.1 single-copy + recovery

eMMC 4 GB에 rootfs 3 GB인 장비처럼 두 벌을 둘 공간이 없으면 쓴다. 업데이트를 받으면 "recovery로 부팅하라"는 플래그를 쓰고 재부팅한다. recovery(작은 커널 + initramfs, SWUpdate 내장)가 rootfs 파티션 전체를 새 이미지로 덮어쓰고, 다 쓴 다음에 플래그를 지운다. 도중에 전원이 나가면 플래그가 남아 있으니 다시 recovery로 가서 처음부터 다시 쓴다. **롤백은 없다** — 새 버전이 기능적으로 망가졌으면 옛 이미지를 다시 받아 같은 과정을 밟아야 한다. 그래서 다운로드 자체가 끊기면 장비가 recovery에 머물게 되니, 이미지를 data 파티션에 먼저 다 받아 두고 검증한 뒤에 전환하는 경우가 많다.

### 2.2 symmetric A/B

이 노트의 주인공이다. 장점은 (1) 장비가 동작하는 동안 백그라운드로 비활성 슬롯에 쓴다(다운타임 = 재부팅 한 번), (2) 이전 버전이 손대지 않은 채 남아 있어 롤백이 즉시다, (3) 비활성 슬롯 쓰기가 아무리 오래 걸려도 그 사이 전원 차단은 무해하다. 단점은 저장 공간 2배, 그리고 **두 슬롯이 하나의 data 파티션을 공유**하므로 데이터 스키마 마이그레이션 문제가 생긴다는 것(§12.1).

### 2.3 파일 단위 — OSTree

OSTree는 "rootfs를 위한 git"이다. `/ostree/repo`에 내용 주소(content-addressed, sha256) 객체로 파일을 저장하고, 각 배포(deployment)는 그 객체들을 하드링크로 엮은 디렉터리 트리다. 새 커밋을 받으면 바뀐 객체만 내려받고, 새 deployment 트리를 만들고, 부트로더 설정(BLS 항목)을 원자적으로 바꾼다. 파일시스템 하나 안에서 두 버전이 객체를 공유하니 공간 효율이 좋다. 대신 파일시스템 자체가 깨지면 두 버전이 같이 죽고(블록 수준 A/B보다 고장 격리가 약하다), dm-verity 같은 블록 단위 무결성(J3)과 궁합이 덜 맞는다(composefs + fs-verity로 보완하는 흐름이 있다). 이 노트에서는 개념만 다룬다.

### 2.4 컨테이너 업데이트 — L8로

앱을 컨테이너 이미지로 배포하면 앱 업데이트는 `podman pull` + 재시작이 된다. 베이스 OS(커널 · 드라이버 · 컨테이너 런타임) 업데이트는 여전히 이 노트의 방법이 필요하다. 자세한 것은 L8.

### 2.5 delta 업데이트

전송량을 줄이는 기법이고, 위 전략 어느 것과도 섞인다. §13에서 실제로 재 본다.

---

## 3. boot-commit 프로토콜 — 업데이트의 심장

모든 A/B 도구(RAUC, SWUpdate, Mender, Android update_engine)가 같은 프로토콜을 쓴다. 단어만 다르다.

1. **쓰기**: 현재 A로 동작 중. 업데이트 에이전트가 번들을 검증하고, B 슬롯을 먼저 "bad"로 표시한 뒤(쓰는 도중에 B로 부팅하면 안 되니까) B에 이미지를 쓴다. 다 쓰면 다시 읽어 해시를 확인한다.
2. **한 번만 시도 표시**: 부트로더 env에 "다음 부팅은 B, 단 시험(trial)이다"를 기록한다. 이 프로젝트의 env로는 `boot_slot=B`, `upgrade_available=1`, `bootcount=0`을 **한 번의 env 쓰기**로.
3. **재부팅**: U-Boot가 env를 읽는다. `upgrade_available=1`이므로 `bootcount`를 1 올려 저장하고 B를 부팅한다.
4. **판정**: B의 userspace가 올라와 health check를 돌린다. 핵심 서비스가 다 살아 있으면 **commit**: `upgrade_available=0`, `bootcount=0`. RAUC에서는 `rauc status mark-good`, 직접 하면 `fw_setenv upgrade_available 0`.
5. **실패 경로**: 커널 panic, init 실패, watchdog 리셋, health check 실패 후 재부팅 — 어떤 이유로든 commit 없이 다시 U-Boot로 오면 `bootcount`가 또 오른다. `bootcount > bootlimit`이 되는 순간 U-Boot는 `bootcmd` 대신 **`altbootcmd`**를 실행하고, 그 스크립트가 B를 bad로 표시하고 A로 되돌린다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="170" height="60" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="105" y="55" text-anchor="middle" font-size="13">A 확정 (committed)</text>
  <text x="105" y="75" text-anchor="middle" font-size="12">upgrade_available=0</text>
  <rect x="255" y="30" width="170" height="60" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="55" text-anchor="middle" font-size="13">B 기록 중</text>
  <text x="340" y="75" text-anchor="middle" font-size="12">l4_state_B=bad</text>
  <rect x="490" y="30" width="170" height="60" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="575" y="55" text-anchor="middle" font-size="13">B 시험 대기</text>
  <text x="575" y="75" text-anchor="middle" font-size="12">boot_slot=B, upg=1</text>
  <rect x="490" y="170" width="170" height="60" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="575" y="195" text-anchor="middle" font-size="13">B 시험 부팅</text>
  <text x="575" y="215" text-anchor="middle" font-size="12">bootcount=1..3</text>
  <rect x="255" y="300" width="170" height="60" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="325" text-anchor="middle" font-size="13">B 확정 (committed)</text>
  <text x="340" y="345" text-anchor="middle" font-size="12">mark-good, upg=0</text>
  <rect x="20" y="170" width="170" height="60" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="105" y="195" text-anchor="middle" font-size="13">롤백 → A</text>
  <text x="105" y="215" text-anchor="middle" font-size="12">altbootcmd, B=bad</text>
  <line x1="190" y1="60" x2="251" y2="60" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="222" y="50" text-anchor="middle" font-size="12">install</text>
  <line x1="425" y1="60" x2="486" y2="60" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="456" y="50" text-anchor="middle" font-size="12">검증 OK</text>
  <line x1="575" y1="90" x2="575" y2="166" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="583" y="134" font-size="12">reboot</text>
  <path d="M640,230 C680,260 680,150 650,168" fill="none" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="600" y="258" font-size="12">panic · 리셋</text>
  <line x1="530" y1="230" x2="420" y2="300" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="400" y="262" font-size="12">health OK</text>
  <line x1="490" y1="200" x2="194" y2="200" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="340" y="190" text-anchor="middle" font-size="12">bootcount &gt; bootlimit</text>
  <line x1="105" y1="170" x2="105" y2="94" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="113" y="134" font-size="12">A 부팅</text>
  <path d="M340,90 C330,130 160,120 140,94" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
  <text x="250" y="130" text-anchor="middle" font-size="12">전원 차단 → 여전히 A</text>
</svg>
```

그림 3 — A/B 상태 기계. 상태는 전부 U-Boot env에 있다. 점선: B를 쓰는 도중 전원이 나가도 `boot_slot`은 아직 A이므로 그냥 A가 부팅된다. "B 확정"에서 다음 업데이트를 하면 역할이 바뀌어(A가 비활성) 같은 그림이 대칭으로 돈다.

### 3.1 왜 "한 번만 시도"가 중요한가

Android 초기 A/B 설계에서 유명한 표현이 "tries remaining"이다. 새 슬롯에 시도 횟수를 주고, 부트로더가 부팅할 때마다 하나씩 깎고, 0이 되면 그 슬롯을 unbootable로 표시한다. RAUC의 U-Boot 스크립트 관례(`BOOT_ORDER="A B"`, `BOOT_A_LEFT=3`)도 같은 생각이다. 이 노트에서는 U-Boot 내장 기능인 **`bootcount`/`bootlimit`/`altbootcmd`**로 구현한다. 둘 다 본질은 같다: **부트로더는 "시도했다"를 부팅 전에 비휘발 저장소에 적고, 성공 판정은 부팅 후 userspace가 그 기록을 지운다.** 둘 중 하나라도 빠지면 무한 재부팅 루프나 영원히 돌아오지 않는 롤백이 된다.

### 3.2 "부팅 성공"을 누가 판정하나

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="80" y="22" text-anchor="middle" font-size="13">RAUC (슬롯 A)</text>
  <text x="250" y="22" text-anchor="middle" font-size="13">U-Boot env</text>
  <text x="420" y="22" text-anchor="middle" font-size="13">U-Boot</text>
  <text x="590" y="22" text-anchor="middle" font-size="13">Linux (슬롯 B)</text>
  <line x1="80" y1="30" x2="80" y2="390" stroke="#888" stroke-dasharray="3 3"/>
  <line x1="250" y1="30" x2="250" y2="390" stroke="#888" stroke-dasharray="3 3"/>
  <line x1="420" y1="30" x2="420" y2="390" stroke="#888" stroke-dasharray="3 3"/>
  <line x1="590" y1="30" x2="590" y2="390" stroke="#888" stroke-dasharray="3 3"/>
  <line x1="80" y1="56" x2="246" y2="56" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="165" y="50" text-anchor="middle" font-size="12">set-state B bad</text>
  <rect x="20" y="70" width="120" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="80" y="90" text-anchor="middle" font-size="12">B에 이미지 쓰기</text>
  <line x1="80" y1="124" x2="246" y2="124" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="165" y="118" text-anchor="middle" font-size="12">boot_slot=B upg=1</text>
  <line x1="80" y1="150" x2="416" y2="150" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah3)"/>
  <text x="250" y="144" text-anchor="middle" font-size="12">reboot</text>
  <line x1="420" y1="180" x2="254" y2="180" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="335" y="174" text-anchor="middle" font-size="12">bootcount++ 저장</text>
  <line x1="420" y1="210" x2="586" y2="210" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="505" y="204" text-anchor="middle" font-size="12">booti (B)</text>
  <rect x="530" y="226" width="120" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="590" y="244" text-anchor="middle" font-size="12">health check</text>
  <text x="590" y="262" text-anchor="middle" font-size="12">서비스 · 센서</text>
  <line x1="590" y1="296" x2="254" y2="296" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="420" y="290" text-anchor="middle" font-size="12">OK: mark-good → upg=0, bootcount=0</text>
  <line x1="590" y1="340" x2="424" y2="340" stroke="#d0564a" stroke-dasharray="5 3" marker-end="url(#ah3)"/>
  <text x="505" y="334" text-anchor="middle" font-size="12">실패: reboot</text>
  <text x="420" y="370" text-anchor="middle" font-size="12">bootcount &gt; bootlimit → altbootcmd → A</text>
</svg>
```

그림 4 — boot-commit 시퀀스. 실선 화살표는 env 쓰기다. env 쓰기는 정확히 네 번(B=bad · B 시험 예약 · U-Boot의 bootcount++ · commit) 일어나고, 각 쓰기가 원자적이어야 한다(§4.6의 redundant env).

판정 기준을 정하는 것이 실은 가장 어려운 설계 결정이다. 너무 느슨하면(커널만 뜨면 OK) 앱이 죽어 있는 버전이 확정되고, 너무 엄격하면(서버 연결까지 확인) 네트워크가 없는 현장에서 멀쩡한 업데이트가 롤백된다. 실무 감각으로는 이렇게 나눈다.

| 단계 | 확인할 것 | 실패 시 |
|---|---|---|
| 부트로더 | 커널 · DTB 로드, 서명 확인 (L5) | 바로 `altbootcmd` 또는 다음 시도 |
| 커널 | panic 없이 rootfs mount, init 실행 | `panic=5`로 재부팅 → bootcount |
| systemd | 필수 유닛이 active, `systemctl is-system-running`이 degraded가 아님 | 재부팅 (commit 안 함) |
| 앱 · 장치 | 카메라 `/dev/video0` 열림, IMU 데이터 흐름, 앱 자체 진단 OK | 재부팅 또는 mark-bad |
| 네트워크 · 서버 | 보통 **판정에 넣지 않는다** (현장 네트워크는 원래 끊긴다) | 롤백 대신 서버에 보고만 |
| 시간 | 판정 전까지 하드웨어 watchdog을 살려 둔다 (L1) | 행(hang)도 리셋 → bootcount |

---
## 4. U-Boot bootcount — 부트로더 쪽 절반

### 4.1 직관

bootcount는 Don이 펌웨어에서 짜 봤을 법한 "부팅 시도 카운터"와 똑같다. 부트로더가 실행될 때마다 비휘발 카운터를 1 올리고, 앱이 "나 정상"이라고 할 때 0으로 지운다. 카운터가 한도를 넘으면 앱이 계속 정상 신호를 못 냈다는 뜻이니 대체 경로로 간다. U-Boot에서는 이게 Kconfig 옵션 몇 개와 env 변수 세 개(`bootcount`, `bootlimit`, `altbootcmd`)로 끝난다.

### 4.2 정의 — Kconfig와 env 변수

| 이름 | 종류 | 뜻 |
|---|---|---|
| `CONFIG_BOOTCOUNT_LIMIT` | Kconfig | bootcount 기능 전체 스위치 |
| `CONFIG_BOOTCOUNT_ENV` | Kconfig | 카운터를 **env 변수 `bootcount`**에 저장 (`upgrade_available=1`일 때만 저장) |
| `CONFIG_BOOTCOUNT_RAM` · `_GENERIC` · `_FS` · `_AM33XX` · `DM_BOOTCOUNT` | Kconfig | 카운터 저장 위치 대안 — 리셋에 살아남는 RAM, SoC 레지스터, RTC, 파일, I2C EEPROM 등 |
| `CONFIG_BOOTCOUNT_BOOTLIMIT` | Kconfig | `bootlimit`의 기본값 (이 랩: 3) |
| `bootcount` | env | 현재 시도 횟수 |
| `bootlimit` | env | 허용 횟수. `bootcount > bootlimit`이면 실패로 본다 |
| `upgrade_available` | env | `BOOTCOUNT_ENV` 전용 — 1일 때만 카운터를 저장한다 (§4.3) |
| `altbootcmd` | env | 한도를 넘었을 때 `bootcmd` 대신 실행할 명령 |

저장 위치 선택이 중요하다. `BOOTCOUNT_ENV`는 **부팅할 때마다 env 블록 전체를 지우고 다시 쓴다**. NOR/eMMC 수명과 부팅 시간을 생각하면 매 부팅 쓰기는 부담이고, 그 쓰기 도중 전원이 나가면 env가 깨질 위험도 생긴다. 그래서 U-Boot는 `upgrade_available=1`일 때만 저장하는 꾀를 쓴다. 평소(업데이트 확정 상태)에는 env를 전혀 쓰지 않고, 업데이트 직후 시험 부팅 동안에만 쓴다. SoC에 리셋에도 살아남는 레지스터(i.MX의 SNVS, TI AM33xx RTC scratch 등)가 있으면 그쪽(`BOOTCOUNT_AM33XX`, `DM_BOOTCOUNT` 드라이버)이 더 좋다 — 쓰기 비용이 0이니 매 부팅 카운트해도 된다.

### 4.3 U-Boot 소스로 확인 — 실제로 무엇을 하나

랩의 U-Boot 소스(`/var/lab/src/u-boot`, v2026.10)에서 env 백엔드 전체가 이 28줄이다.

```c
/* drivers/bootcount/bootcount_env.c (U-Boot v2026.10, 발췌 없이 전문) */
#include <env.h>

void bootcount_store(ulong a)
{
	int upgrade_available = env_get_ulong("upgrade_available", 10, 0);

	if (upgrade_available) {
		env_set_ulong("bootcount", a);
		env_save();
	}
}

ulong bootcount_load(void)
{
	int upgrade_available = env_get_ulong("upgrade_available", 10, 0);
	ulong val = 0;

	if (upgrade_available)
		val = env_get_ulong("bootcount", 10, 0);

	return val;
}
```

그리고 판정은 `include/bootcount.h`의 inline 함수, 호출은 `common/autoboot.c`의 `bootdelay_process()`다.

```c
/* include/bootcount.h (발췌) */
static inline int bootcount_error(void)
{
	unsigned long bootcount = bootcount_load();
	unsigned long bootlimit = env_get_ulong("bootlimit", 10, 0);

	if (bootlimit && bootcount > bootlimit) {
		printf("Warning: Bootlimit (%lu) exceeded.", bootlimit);
		if (!(gd->flags & GD_FLG_SPL_INIT))
			printf(" Using altbootcmd.");
		printf("\n");
		return 1;
	}
	return 0;
}

static inline void bootcount_inc(void)
{
	unsigned long bootcount = bootcount_load();
	/* ... SPL 분기 생략 ... */
	bootcount_store(++bootcount);
	env_set_ulong("bootcount", bootcount);   /* RAM 사본은 항상 갱신 */
}

/* common/autoboot.c bootdelay_process() (발췌) */
	bootcount_inc();
	/* ... */
	if (bootcount_error())
		s = env_get("altbootcmd");
	else
		s = env_get("bootcmd");
```

읽을 점 세 가지.

- **`bootcount > bootlimit`** — 등호가 아니다. `bootlimit=3`이면 시험 부팅이 **세 번** 허용되고, 네 번째로 U-Boot에 들어왔을 때(카운터 4) `altbootcmd`가 돈다. 아래 실측 로그가 정확히 그렇다.
- `bootcount_inc()`는 `upgrade_available=0`이어도 RAM의 `bootcount`를 1로 만든다(저장은 안 함). 그래서 확정 상태에서도 U-Boot 콘솔에 `bootcount=1`이 보인다. 헷갈리지 말 것 — 플래시에는 0이 그대로 있다.
- **`altbootcmd`는 "되돌리는 방법"을 스스로 구현해야 한다.** U-Boot는 그냥 다른 명령을 실행할 뿐이다. 슬롯을 바꾸고, `upgrade_available`과 `bootcount`를 지우고, `saveenv`까지 하는 것이 스크립트의 몫이다. 이걸 빠뜨리면 다음 부팅에도 또 `altbootcmd`가 돈다.

### 4.4 예제 1 — bootcount를 켠 U-Boot 직접 빌드

**무엇을 확인하나**: 랩 U-Boot 소스로 `qemu_arm64_defconfig`에 bootcount · redundant env · 필요한 명령을 얹어 별도 출력 디렉터리에 빌드한다.

```sh
cat > ~/.elinux_scratch/L4/l4_uboot.frag <<'EOF'
CONFIG_BOOTCOUNT_LIMIT=y
CONFIG_BOOTCOUNT_ENV=y
CONFIG_BOOTCOUNT_BOOTLIMIT=3
CONFIG_ENV_REDUNDANT=y
CONFIG_ENV_ADDR_REDUND=0x4040000
CONFIG_CMD_SETEXPR=y
CONFIG_CMD_GPT=y
CONFIG_CMD_PART=y
CONFIG_CMD_BOOTCOUNT=y
EOF
cd /var/lab/src/u-boot
make O=/var/tmp/L4/build qemu_arm64_defconfig
cd /var/tmp/L4/build
/var/lab/src/u-boot/scripts/kconfig/merge_config.sh -m .config ~/.elinux_scratch/L4/l4_uboot.frag
make -C /var/lab/src/u-boot O=/var/tmp/L4/build olddefconfig
grep -n "BOOTCOUNT\|ENV_REDUND\|ENV_ADDR\|ENV_IS_IN_FLASH" .config
time make -C /var/lab/src/u-boot O=/var/tmp/L4/build -j2
```

```text
202:CONFIG_BOOTCOUNT_BOOTLIMIT=3
232:CONFIG_ENV_ADDR=0x4000000
926:CONFIG_ENV_IS_IN_FLASH=y
931:CONFIG_ENV_REDUNDANT=y
933:CONFIG_ENV_ADDR_REDUND=0x4040000
1030:CONFIG_BOOTCOUNT_LIMIT=y
1033:CONFIG_BOOTCOUNT_ENV=y
1036:CONFIG_BOOTCOUNT_ALTBOOTCMD=""
...
real	1m23.362s
-rw-rw-r-- 1 donh donh 1515776 Oct 17 21:15 u-boot.bin
```

**출력에서 볼 것**: env는 `ENV_IS_IN_FLASH`로 NOR flash의 `0x4000000`(QEMU virt의 두 번째 pflash 뱅크 맨 앞, C3 §4)에, 이중화 사본은 바로 다음 256 KiB 섹터 `0x4040000`에 둔다. 옛 이름 `CONFIG_SYS_REDUNDAND_ENVIRONMENT`는 최신 U-Boot에서 **`CONFIG_ENV_REDUNDANT`**로 바뀌었다(C3 · C4 노트의 옛 이름은 이것과 같은 기능). 빌드 1분 23초는 TCG가 아니라 VM의 실제 컴파일 시간이다.

### 4.5 예제 2 — U-Boot만으로 bootcount 루프 재현

**무엇을 확인하나**: 커널 없이 U-Boot만으로 "부팅 실패(여기서는 `reset`) → bootcount 증가 → 한도 초과 → altbootcmd"를 본다. env 텍스트를 `mkenvimage -r`(redundant 형식)로 만들어 pflash 이미지에 넣는다.

```sh
cat > ~/.elinux_scratch/L4/l4_env_loop.txt <<'EOF'
bootdelay=0
bootlimit=3
upgrade_available=1
bootcount=0
boot_slot=B
bootcmd=echo "L4: bootcmd slot=${boot_slot} bootcount=${bootcount}"; echo "L4: pretend kernel panics -> reset"; reset
altbootcmd=echo "L4: ALTBOOTCMD bootcount=${bootcount} -> rollback to A"; setenv boot_slot A; setenv upgrade_available 0; setenv bootcount 0; saveenv; echo "L4: committed rollback, now poweroff"; poweroff
EOF
cat > ~/.elinux_scratch/L4/l4_mkpflash.sh <<'EOF'
#!/bin/bash
# usage: l4_mkpflash.sh env.txt out.img -> 64MiB pflash1 image, redundant env at 0x0 and 0x40000
set -e
mkenvimage -r -s 0x40000 -o "$2.env" "$1"
truncate -s 64M "$2"
dd if="$2.env" of="$2" conv=notrunc bs=256k seek=0 status=none
dd if="$2.env" of="$2" conv=notrunc bs=256k seek=1 status=none
rm -f "$2.env"
EOF
cd /var/tmp/L4 && ~/.elinux_scratch/L4/l4_mkpflash.sh ~/.elinux_scratch/L4/l4_env_loop.txt l4_pf_loop.img
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 1G -nographic -bios build/u-boot.bin \
  -drive if=pflash,unit=1,format=raw,file=l4_pf_loop.img | tr -d '\r' > l4_loop.log
grep -n "L4\|Bootlimit\|Saving\|Warning\|resetting" l4_loop.log
```

```text
8:*** Warning - some problems detected reading environment; recovered successfully
16:Saving Environment to Flash... Un-Protected 2 sectors
26:L4: bootcmd slot=B bootcount=1
27:L4: pretend kernel panics -> reset
28:resetting ...
41:Saving Environment to Flash... Un-Protected 2 sectors
51:L4: bootcmd slot=B bootcount=2
52:L4: pretend kernel panics -> reset
53:resetting ...
66:Saving Environment to Flash... Un-Protected 2 sectors
76:L4: bootcmd slot=B bootcount=3
77:L4: pretend kernel panics -> reset
78:resetting ...
91:Saving Environment to Flash... Un-Protected 2 sectors
100:Warning: Bootlimit (3) exceeded. Using altbootcmd.
102:L4: ALTBOOTCMD bootcount=4 -> rollback to A
103:Saving Environment to Flash... Un-Protected 2 sectors
112:L4: committed rollback, now poweroff
```

**출력에서 볼 것**: 부팅마다 `Saving Environment`(bootcount 저장)가 **bootcmd보다 먼저** 일어난다 — "시도했다"를 먼저 적고 시도하는 순서다. 1, 2, 3번째는 `bootcmd`가, 카운터가 4가 된 네 번째는 `altbootcmd`가 돈다. QEMU의 `reset`은 pflash 이미지 파일 내용을 그대로 두므로 실제 NOR처럼 카운터가 리셋을 넘어 살아남는다. 8번째 줄 경고는 다음 절의 주제다.

### 4.6 env 이중화 — 두 벌과 flag 바이트

**직관**: Don이 SSD 메타데이터를 A/B 두 영역에 번갈아 쓰고 시퀀스 번호로 최신 것을 고르던 것과 같다. env 블록 형식은 이렇다.

```
single:     [CRC32 4B][key=value\0key=value\0...\0\0 ... padding]   (ENV_SIZE)
redundant:  [CRC32 4B][flags 1B][key=value\0...\0\0 ... padding]    (x2, ENV_SIZE each)
CRC covers data only (ENV_SIZE - 4 - 1 bytes for redundant)
```

`saveenv`는 **현재 쓰지 않는 쪽 사본**을 지우고 새 내용을 쓴 다음 flag를 갱신한다. 그래서 쓰는 도중에 전원이 나가도 다른 쪽 사본은 온전하다. flag의 의미는 저장 매체마다 다르다.

| 저장 매체 | flag 방식 | 의미 |
|---|---|---|
| NOR (`ENV_IS_IN_FLASH`) | boolean: `1` = ACTIVE, `0` = OBSOLETE | NOR는 지우지 않고 1→0만 쓸 수 있으므로, 옛 사본의 flag 바이트를 **제자리에서 0으로 프로그램**해 무효화한다 |
| eMMC/SPI/FAT/UBI | 증가 카운터 | 둘 다 CRC가 맞으면 카운터가 큰(랩어라운드 고려) 쪽이 최신 |

예제 2에서 내가 두 사본을 **같은 flag(1)**로 만들어 넣었기 때문에 U-Boot가 "둘 다 ACTIVE라니 이상하다"며 `some problems detected ... recovered successfully`를 띄운 것이다. 저장이 몇 번 돈 뒤 호스트에서 두 사본의 앞 8바이트를 보면 flag가 정리되어 있다.

```sh
printf "/var/tmp/L4/l4_pf_loop.img 0x0 0x40000 0x40000\n/var/tmp/L4/l4_pf_loop.img 0x40000 0x40000 0x40000\n" > l4_host_fw_env.config
fw_printenv -c l4_host_fw_env.config | sort
for o in 0 262144; do echo "copy@$o:"; xxd -s $o -l 8 l4_pf_loop.img; done
```

```text
altbootcmd=echo "L4: ALTBOOTCMD bootcount=${bootcount} -> rollback to A"; ...
boot_slot=A
bootcmd=echo "L4: bootcmd slot=${boot_slot} bootcount=${bootcount}"; ...
bootcount=0
bootdelay=0
bootlimit=3
ethaddr=52:54:00:12:34:56
fdtcontroladdr=7e34bd80
upgrade_available=0
copy@0:
00000000: 24cb 33b7 0061 6c74                      $.3..alt
copy@262144:
00040000: 6fda eec2 0161 6c74                      o....alt
```

**출력에서 볼 것**: 5바이트째가 flag다. 사본 0은 `00`(OBSOLETE), 사본 1은 `01`(ACTIVE). `fw_printenv`(호스트의 libubootenv)는 ACTIVE 쪽을 읽어 altbootcmd가 남긴 최종 상태 `boot_slot=A`, `upgrade_available=0`을 보여 준다. 그래서 이후 이미지 생성 스크립트는 두 번째 사본의 flag를 처음부터 `00`으로 만들었다(`printf '\x00' | dd ... seek=$((0x40004))`).

### 4.7 예제 3 — 사본 하나 · 둘 다 깨지면 U-Boot는 무엇을 읽나

**무엇을 확인하나**: 사본 1(ACTIVE)과 사본 2(OBSOLETE)에 서로 다른 표시값(`l4_copy=1/2`)을 넣고, 바이트 하나를 바꿔 CRC를 깨뜨린 뒤 U-Boot가 고르는 사본을 본다. 전원 차단으로 `saveenv`가 반쯤 쓴 상황의 재현이다.

```sh
#!/bin/bash
# l4_envcorrupt.sh: redundant env - what does U-Boot load when one / both copies are corrupt?
set -e
cd /var/tmp/L4 && mkdir -p envt && cd envt
for c in 1 2; do
  printf 'bootdelay=0\nl4_copy=%s\nbootcmd=echo "L4: loaded env copy ${l4_copy}"; poweroff\n' $c > e$c.txt
  mkenvimage -r -s 0x40000 -o e$c.bin e$c.txt
done
mk() { truncate -s 0 pf.img; truncate -s 64M pf.img
  dd if=e1.bin of=pf.img conv=notrunc bs=256k seek=0 status=none
  dd if=e2.bin of=pf.img conv=notrunc bs=256k seek=1 status=none
  printf '\x00' | dd of=pf.img bs=1 seek=$((0x40004)) conv=notrunc status=none; }   # copy2 = obsolete
flip() { printf '\x58' | dd of=pf.img bs=1 seek=$(($1)) conv=notrunc status=none; }
boot() { echo "== $1"; timeout 60 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 1G -nographic \
  -bios ../build/u-boot.bin -drive if=pflash,unit=1,format=raw,file=pf.img 2>&1 | tr -d '\r' |
  grep -a "Environment\|Warning\|bad CRC\|L4:\|Hit any\|scanning bus" | head -n 6; }
mk;           boot "both copies valid (copy1 ACTIVE)"
mk; flip 0x20; boot "copy1 corrupted (byte 0x20 changed)"
mk; flip 0x20; flip 0x40020; boot "both copies corrupted"
```

```text
== both copies valid (copy1 ACTIVE)
Loading Environment from Flash...Loading Environment from Flash... OK
Hit any key to stop autoboot: 0
L4: loaded env copy 1
== copy1 corrupted (byte 0x20 changed)
Loading Environment from Flash...Loading Environment from Flash... OK
Hit any key to stop autoboot: 0
L4: loaded env copy 2
== both copies corrupted
Loading Environment from Flash...*** Warning - bad CRC, using default environment
Hit any key to stop autoboot: 2Hit any key to stop autoboot: 1Hit any key to stop autoboot: 0
scanning bus for devices...
*** Warning: no boot file name; using '0A00020F.img'
```

**출력에서 볼 것**: ACTIVE 사본이 깨지면 U-Boot는 **아무 경고 없이** 이전 사본(copy 2)으로 부팅한다 — 즉 "직전 env 쓰기가 없었던 것"이 된다. boot-commit 프로토콜에서는 이게 정확히 원하는 동작이다(예: commit 쓰기 도중 차단 → 아직 시험 중 상태로 남아 다음 부팅에 다시 판정). 둘 다 깨지면 **컴파일된 기본 env**로 떨어져 distro boot가 USB · 네트워크(TFTP `0A00020F.img`)를 뒤진다. 우리 슬롯 선택 로직이 통째로 사라진 것이다. 양산 장비에서는 그래서 기본 env(`CONFIG_BOOTCOMMAND`, 보드 `.env` 파일)에도 **안전한 A/B 부팅 스크립트**를 넣어 둔다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="240" y="16" width="200" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="41" text-anchor="middle" font-size="13">두 사본의 CRC 검사</text>
  <rect x="20" y="110" width="190" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="115" y="134" text-anchor="middle" font-size="13">둘 다 OK</text>
  <text x="115" y="154" text-anchor="middle" font-size="12">flag로 최신 선택</text>
  <rect x="245" y="110" width="190" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="134" text-anchor="middle" font-size="13">하나만 OK</text>
  <text x="340" y="154" text-anchor="middle" font-size="12">그 사본 사용 (경고 없음)</text>
  <rect x="470" y="110" width="190" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="565" y="134" text-anchor="middle" font-size="13">둘 다 깨짐</text>
  <text x="565" y="154" text-anchor="middle" font-size="12">bad CRC → 기본 env</text>
  <line x1="300" y1="56" x2="130" y2="106" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="340" y1="56" x2="340" y2="106" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="380" y1="56" x2="550" y2="106" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="115" y="196" text-anchor="middle" font-size="12">실측: copy 1 (ACTIVE)</text>
  <text x="340" y="196" text-anchor="middle" font-size="12">실측: copy 2 (직전 상태)</text>
  <text x="565" y="196" text-anchor="middle" font-size="12">실측: distro boot · TFTP</text>
  <rect x="20" y="226" width="640" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="250" text-anchor="middle" font-size="13">설계 규칙: 기본 env에도 안전한 A/B 스크립트를 넣는다</text>
  <text x="340" y="270" text-anchor="middle" font-size="12">CONFIG_BOOTCOMMAND · board .env · CONFIG_ENV_WRITEABLE_LIST로 쓰기 가능한 변수 제한</text>
</svg>
```

그림 5 — redundant env의 선택 규칙과 예제 3 실측 결과. 가운데 경우가 "env 쓰기 도중 전원 차단"이고, 결과는 "그 쓰기가 일어나지 않은 것"이다.

### 4.8 함정 — env는 생각보다 약하다

- **Linux와 U-Boot의 위치 · 크기 불일치**: `fw_env.config`의 오프셋 · 크기 · 섹터 크기가 U-Boot Kconfig와 한 바이트라도 다르면 `fw_printenv`가 `Bad CRC`를 내거나, 더 나쁘게 엉뚱한 섹터를 지운다(C3 §4.4).
- **libubootenv 스크립트 문법**: `fw_setenv -s 파일`의 형식은 **`key=value`**다. 옛 문서의 `key value`(공백) 형식을 쓰면 이 버전(0.3.5, 랩 rootfs)은 **아무 에러 없이 rc=0으로 무시**한다. 실제로 그렇게 해서 commit이 안 되는 버그를 만들 뻔했다.

```text
s1 rc=0
l4_state_B=good      <- "l4_state_B bad" (공백 형식) : 조용히 무시됨
s2 rc=0
l4_state_B=bad       <- "l4_state_B=bad" : 적용됨
```

- **env 전체가 공격면이다**: env에 `bootcmd`가 있으면 Linux에서 root가 된 공격자가 `fw_setenv bootcmd ...`로 부팅 경로를 바꿀 수 있다. secure boot 장비는 `CONFIG_ENV_WRITEABLE_LIST` + `CONFIG_ENV_FLAGS_LIST_STATIC`으로 Linux가 쓸 수 있는 변수를 `boot_slot`, `upgrade_available`, `bootcount` 같은 몇 개로 제한하고, 스크립트는 서명된 FIT 안에 넣는다(L5).
- **여러 변수를 한 번에**: `boot_slot`, `upgrade_available`, `bootcount`를 `fw_setenv`로 세 번 따로 쓰면 env 쓰기가 세 번이고, 그 사이 전원 차단은 "B로 부팅하는데 시험 표시가 없음" 같은 어중간한 상태를 만든다. `fw_setenv -s`로 **한 번의 쓰기**에 묶는다.

---

## 5. 실습 랩 만들기 — GPT 디스크 · 슬롯 이미지 · U-Boot 스크립트

여기부터는 이 노트의 큰 실습이다. VM 커널은 `uname -r` = `7.0.0-38-generic`(Ubuntu 26.04.1), QEMU 10.2.1, 게스트 커널은 랩의 `Image-sec`(6.18.55-sec), rootfs는 랩의 `rootfs-sysd.ext4`(Ubuntu Base 24.04.5 + systemd 255, RAUC 1.11.3, SWUpdate 2023.12.1, libubootenv 0.3.5)다. **모든 부팅 시간은 TCG 에뮬레이션(그리고 같은 VM에서 다른 작성자 7명의 QEMU가 동시에 도는 상황) 기준이고, 실제 하드웨어 수치가 아니다.**

### 5.1 슬롯 이미지 다듬기

랩 rootfs는 3 GiB(사용 약 1 GB)인데 그중 360 MB가 커널 모듈이다. 슬롯 두 벌 + 번들 세 개를 디스크 하나에 넣으려고, 내 사본에서 모듈을 지우고(이 실습은 virtio · ext4 · MTD가 전부 built-in인 `Image-sec`만 쓴다) 다음을 넣었다.

| 파일 | 역할 |
|---|---|
| `/boot/Image` | 커널(Image-sec). 슬롯 안에 들어 있어서 커널과 rootfs가 같이 바뀌고 같이 롤백된다 |
| `/etc/fw_env.config` | Linux 쪽 env 위치 (§5.4) |
| `/etc/rauc/system.conf`, `/etc/rauc/l4_ca.cert.pem` | RAUC 설정과 신뢰 루트 (§6) |
| `/usr/local/sbin/l4-rauc-backend` | RAUC custom bootloader backend (§6.4) |
| `/usr/local/sbin/l4-health`, `l4-health.service` | health check + commit (§6.5) |
| `/etc/fstab` 한 줄 | `/dev/vda3 /data ext4 defaults,nofail 0 2` |
| `/etc/l4-version` | 이미지 버전 표시 (1.0.0, 2.0.0, ...) |
| `system.conf.d/l4-slow-tcg.conf` | `DefaultDeviceTimeoutSec=600s` — 첫 시도에서 VM 과부하로 udev coldplug가 90초를 넘겨 `dev-ttyAMA0.device` · `dev-vda3.device`가 timeout으로 실패했다. 실제 장비라면 이것도 health check가 잡아야 할 "느린 부팅" 증상이다 |

podman 타이머 · apt-daily · swupdate 데몬처럼 부팅을 늦추는 유닛은 사본에서 끄고, `mkfs.ext4 -d stage/`로 800 MiB ext4 이미지를 만들었다. 버전별로 네 가지를 만든다.

| 이미지 | 내용 | 용도 |
|---|---|---|
| v1 1.0.0 | 공장 출하 | rootA · rootB 양쪽 초기값 |
| v2 2.0.0 | 정상 업데이트 | 시나리오 1, 4 |
| v3 3.0.0-broken | `/usr/sbin/init`을 "echo 후 exit 1" 스크립트로 바꿈 → 커널 panic | 시나리오 2 |
| v4 4.0.0-unhealthy | `/etc/l4-break-health` 존재 → health check 실패 | 시나리오 3 |

### 5.2 예제 4 — GPT 디스크 만들기

**무엇을 확인하나**: `sfdisk` 스크립트로 이름 붙은 GPT 파티션 세 개를 만들고, 각 파티션 시작 섹터에 ext4 이미지를 `dd`로 넣는다(호스트에 loop 장치를 붙이지 않는 방법).

```sh
#!/bin/bash
# l4_mkdisk.sh: GPT disk: p1 rootA, p2 rootB, p3 data (bundles + .swu), both slots get factory v1
set -e
cd /var/tmp/L4
rm -f l4_disk.img && truncate -s 3000M l4_disk.img
sfdisk -q l4_disk.img <<'EOT'
label: gpt
size=900MiB, type=linux, name=rootA
size=900MiB, type=linux, name=rootB
type=linux, name=data
EOT
sgdisk -p l4_disk.img | tail -n 5
# partition offsets in 512-byte sectors
off() { sfdisk -J l4_disk.img | python3 -c "import json,sys;print(json.load(sys.stdin)['partitiontable']['partitions'][$1]['start'])"; }
dd if=l4_v1.ext4 of=l4_disk.img bs=512 seek=$(off 0) conv=notrunc,sparse status=none
dd if=l4_v1.ext4 of=l4_disk.img bs=512 seek=$(off 1) conv=notrunc,sparse status=none
sudo rm -rf datadir && sudo mkdir datadir && sudo cp bundles/*.raucb swu/l4_2.0.0.swu swu/l4_tampered.swu datadir/
sudo cp rauc/l4_ca.cert.pem datadir/
rm -f l4_data.ext4 && truncate -s 1100M l4_data.ext4 && sudo mkfs.ext4 -q -L data -d datadir l4_data.ext4
dd if=l4_data.ext4 of=l4_disk.img bs=512 seek=$(off 2) conv=notrunc,sparse status=none
rm -f l4_data.ext4; sudo rm -rf datadir
ls -ls l4_disk.img
```

```text
Number  Start (sector)    End (sector)  Size       Code  Name
   1            2048         1845247   900.0 MiB   8300  rootA
   2         1845248         3688447   900.0 MiB   8300  rootB
   3         3688448         6141951   1.2 GiB     8300  data
2283816 -rw-rw-r-- 1 donh donh 3145728000 Oct 17 22:32 l4_disk.img
```

**출력에서 볼 것**: 3000 MiB 이미지가 실제로는 2.2 GB만 차지한다(sparse). data 파티션 파일은 `sudo cp`로 root 소유로 만든다 — RAUC는 번들 파일이 root 소유가 아니면 "unsafe bundle uid"로 거부한다(§6.3에서 실제로 걸렸다). 실제 제품이라면 슬롯 크기를 이미지 크기보다 넉넉히(이 랩: 이미지 800 MiB, 슬롯 900 MiB) 두는 것이 중요하다. 다음 버전 rootfs가 커질 여지다.

### 5.3 U-Boot 슬롯 선택 스크립트

env 텍스트 그대로다(한 변수 = 한 줄; 여기서는 읽기 쉽게 그대로 보인다).

```text
kernel_addr_r=0x40400000
bootdelay=1
bootlimit=3
upgrade_available=0
bootcount=0
boot_slot=A
l4_state_A=good
l4_state_B=good
l4_boot=if test "${boot_slot}" = "B"; then setenv l4_part 2; else setenv l4_part 1; fi; echo "L4: boot_slot=${boot_slot} part=${l4_part} bootcount=${bootcount} upgrade_available=${upgrade_available}"; load virtio 0:${l4_part} ${kernel_addr_r} /boot/Image; setenv bootargs "console=ttyAMA0 root=/dev/vda${l4_part} rw rootwait panic=5 l4.slot=${boot_slot} rauc.slot=${boot_slot}"; booti ${kernel_addr_r} - ${fdtcontroladdr}
bootcmd=virtio scan; run l4_boot
altbootcmd=echo "L4: bootlimit exceeded on slot ${boot_slot} -> rollback"; setenv l4_state_${boot_slot} bad; if test "${boot_slot}" = "B"; then setenv boot_slot A; else setenv boot_slot B; fi; setenv upgrade_available 0; setenv bootcount 0; saveenv; virtio scan; run l4_boot
```

요점:

- **`panic=5`**: 커널 panic 후 5초 뒤 자동 재부팅. 이게 없으면 panic한 장비가 그 자리에서 영원히 멈춰 bootcount가 다시 돌 기회가 없다. 하드웨어 watchdog(L1)이 같은 역할의 최후 보루다.
- **`rauc.slot=`**: RAUC가 "지금 어느 슬롯으로 부팅했나"를 아는 방법. `root=` 장치로도 판단하지만 명시하는 편이 확실하다.
- **`kernel_addr_r`**: 처음에 이 줄을 빠뜨렸다. `mkenvimage`로 만든 env가 저장된 env를 **통째로** 대체하므로 보드 기본 env의 `kernel_addr_r`이 사라져 `load` 사용법만 찍히고 `Bad Linux ARM64 Image magic!`으로 멈췄다. 저장 env는 기본 env와 합쳐지는 게 아니라 **바꿔치기**된다(C4 §3과 같은 이야기).
- 커널을 슬롯 안 ext4에서 읽는다. 커널을 별도 boot 파티션에 두면 boot-A/boot-B도 두 벌이어야 한다. 비대칭(boot 하나 + root A/B)으로 두면 커널과 rootfs 모듈 버전이 어긋날 수 있다.

### 5.4 Linux 쪽 env 접근 — fw_env.config와 QEMU pflash의 함정

게스트 Linux에서 NOR flash는 `physmap-of` CFI 드라이버로 MTD 장치가 된다. 두 pflash 뱅크(각 64 MiB)가 이어 붙어 `mtd0` 하나(128 MiB)로 보이므로, env 오프셋은 `0x4000000`이다.

```text
L4# cat /proc/mtd
dev:    size   erasesize  name
mtd0: 08000000 00040000 "0.flash"
```

처음 설정은 이중화였다.

```text
/dev/mtd0 0x4000000 0x40000 0x40000
/dev/mtd0 0x4040000 0x40000 0x40000
```

그런데 게스트에서 `fw_setenv`가 매번 실패했다.

```text
L4# fw_setenv l4_dbg 1; echo rc=$?; fw_printenv l4_dbg; dmesg | tail -n 5
Error storing the env
rc=255
[   19.993317] 0.flash: buffer write error (status timeout)
l4_dbg=
```

원인을 좁히려고 libubootenv가 NOR에서 하는 일(MEMUNLOCK → MEMERASE → write)을 그대로 하는 작은 C 프로그램을 BusyBox initramfs에 넣어 돌렸다.

### 5.5 예제 5 — MTD에 직접 지우고 쓰기 (fw_setenv의 속)

**무엇을 확인하나**: `/dev/mtd0`에 `MEMGETINFO` · `MEMUNLOCK` · `MEMERASE` ioctl과 `pwrite`로 쓰는 과정. 네 번째 인자를 주면 지우지 않고 제자리에 0을 프로그램한다(NOR redundant env의 flag 바이트 갱신과 같은 동작).

```c
/* l4_mtdw.c: what fw_setenv does on NOR: MEMGETINFO -> MEMUNLOCK -> MEMERASE -> write -> read back */
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/mtd0";
	off_t off = argc > 2 ? strtoul(argv[2], NULL, 0) : 0x4080000;
	size_t len = argc > 3 ? strtoul(argv[3], NULL, 0) : 4096;
	struct mtd_info_user mi;
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror("open"); exit(1); }
	if (ioctl(fd, MEMGETINFO, &mi)) { perror("MEMGETINFO"); exit(1); }
	printf("%s: type=%u size=0x%x erasesize=0x%x writesize=%u\n",
	       dev, mi.type, mi.size, mi.erasesize, mi.writesize);
	struct erase_info_user ei = { .start = off, .length = mi.erasesize };
	if (ioctl(fd, MEMUNLOCK, &ei)) perror("MEMUNLOCK (ignored)");
	if (argc > 4) {
		printf("no erase: program %zu byte(s) in place at 0x%lx\n", len, (long)off);
	} else {
		if (ioctl(fd, MEMERASE, &ei)) { perror("MEMERASE"); exit(1); }
		printf("erased 0x%lx +0x%x\n", (long)off, mi.erasesize);
	}
	char *w = malloc(len), *r = malloc(len);
	if (!w || !r) { perror("malloc"); exit(1); }
	for (size_t i = 0; i < len; i++) w[i] = argc > 4 ? 0 : (char)(i * 7 + 1);
	ssize_t n = pwrite(fd, w, len, off);
	if (n != (ssize_t)len) { perror("pwrite"); exit(1); }
	if (pread(fd, r, len, off) != (ssize_t)len) { perror("pread"); exit(1); }
	printf("wrote %zd bytes, read back %s\n", n, memcmp(w, r, len) ? "MISMATCH" : "OK");
	close(fd);
	return 0;
}
```

`gcc -static -O2 -Wall -Wextra`(경고 0) → initramfs의 `/bin/l4_mtdw`로 넣고, U-Boot의 `qfw load`로 커널 · initramfs를 올려 U-Boot를 거친 실제 경로에서 돌렸다.

```sh
# env 블록의 bootcmd (U-Boot): qfw load ${kernel_addr_r} ${ramdisk_addr_r}; booti ${kernel_addr_r} ${ramdisk_addr_r}:${filesize} ${fdtcontroladdr}
qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 1G -nographic -bios build/u-boot.bin \
  -kernel /var/lab/out/Image-sec -initrd ird/i.gz -drive if=pflash,unit=1,format=raw,file=/tmp/l4_qfw.img
# bootargs: LAB_CMD=/bin/l4_mtdw~/dev/mtd0~<offset>[~<len>[~noerase]]
```

```text
== smp=1 len=0x40000          (offset 0x4000000 = env sector: erase + 256 KiB write)
/dev/mtd0: type=3 size=0x8000000 erasesize=0x40000 writesize=1
erased 0x4000000 +0x40000
wrote 262144 bytes, read back OK
== 0x4040004~1~noerase        (flag byte of the 2nd copy, programmed in place)
MEMUNLOCK (ignored): Invalid argument
no erase: program 1 byte(s) in place at 0x4040004
pwrite: Timer expired
[    1.391116] 0.flash: buffer write error (status timeout)
== 0x4040004~4~noerase
MEMUNLOCK (ignored): Invalid argument
no erase: program 4 byte(s) in place at 0x4040004
pwrite: Timer expired
[    1.344200] 0.flash: buffer write error (status timeout)
== 0x4040000~8~noerase
/dev/mtd0: type=3 size=0x8000000 erasesize=0x40000 writesize=1
no erase: program 8 byte(s) in place at 0x4040000
wrote 8 bytes, read back OK
```

(괄호 설명은 내가 붙인 것이다. 실패한 두 경우는 출력을 `tail -n 4`로 잘라서 첫 `/dev/mtd0:` 줄이 빠졌다.) **출력에서 볼 것**: 섹터를 지우고 통째로 쓰는 것은 된다. 그러나 **섹터 중간(오프셋 4)에 1~4바이트를 제자리 프로그램하는 것은 QEMU의 pflash 에뮬레이션에서 실패**한다(8바이트 정렬 위치는 성공). libubootenv의 NOR redundant 갱신은 정확히 "옛 사본의 flag 바이트(오프셋 4)를 0으로 프로그램"이므로 매번 실패한 것이다. 실제 CFI NOR 칩은 바이트 단위 프로그램을 지원하므로 이것은 **에뮬레이터의 한계**다. 그래서 Linux가 관여하는 시나리오(§6~§9)는 **single-copy env**로 다시 빌드한 U-Boot(`CONFIG_ENV_REDUNDANT` 끔, 출력 `/var/tmp/L4/build1`)와 한 줄짜리 `fw_env.config`를 썼다. 이중화 동작은 §4.6~4.7에서 U-Boot 단독으로 이미 확인했다.

```text
# /etc/fw_env.config (게스트, single-copy)
/dev/mtd0 0x4000000 0x40000 0x40000
```

> **실무**: 이 사건 자체가 좋은 교훈이다. "fw_setenv가 rc=255"는 OTA 에이전트의 **commit 실패**로 이어지고, commit이 안 되면 멀쩡한 업데이트가 bootlimit 후 롤백된다. 그래서 (1) 에이전트는 env 쓰기 결과를 반드시 검사하고 실패를 서버에 보고해야 하며, (2) 새 보드 bring-up 때 `fw_setenv` → 재부팅 → U-Boot `printenv` 왕복 시험을 꼭 해야 한다. 에뮬레이터 · 새 플래시 칩 · 새 eMMC 펌웨어 어디서든 같은 종류의 문제가 나올 수 있다.

---

## 6. RAUC로 하는 정상 업데이트 — 서명부터 commit까지

### 6.1 RAUC 구성 한눈에

RAUC(Robust Auto-Update Controller, Pengutronix, LGPL-2.1)는 **장비 쪽 업데이트 클라이언트**다. 서버는 없다(hawkBit 연동은 `rauc-hawkbit-updater` 별도). 구성 요소는 네 가지다.

| 구성 요소 | 어디 | 역할 |
|---|---|---|
| `system.conf` | 장비 `/etc/rauc/` | compatible 문자열, 슬롯 목록, 부트로더 backend, keyring |
| 번들 (`.raucb`) | 빌드 서버 → 장비 | squashfs(이미지 + manifest) + CMS 서명. 형식 `plain` · `verity` · `crypt` |
| `rauc` 서비스 | 장비 (D-Bus `de.pengutronix.rauc`) | install · status · mark-good 수행 |
| bootloader backend | 장비 | `uboot`, `barebox`, `grub`, `efi`, `custom` — 슬롯 상태를 부트로더에 기록 |

### 6.2 예제 6 — 서명 키 체계 만들기

**무엇을 확인하나**: 루트 CA 하나와 그 CA가 서명한 "업데이트 서명용" 인증서를 만든다. 장비에는 **CA 인증서만** 들어가고, 개인 키는 빌드 서버(실무에서는 HSM)에만 있다.

```sh
cd /var/tmp/L4/rauc
openssl req -x509 -newkey rsa:3072 -nodes -keyout l4_ca.key.pem -out l4_ca.cert.pem \
  -subj "/O=L4 Lab/CN=L4 Dev Root CA" -days 3650 \
  -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign"
openssl req -newkey rsa:3072 -nodes -keyout l4_dev.key.pem -out l4_dev.csr -subj "/O=L4 Lab/CN=L4 Dev Update Signer"
printf "basicConstraints=CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=codeSigning\n" > l4_leaf.ext
openssl x509 -req -in l4_dev.csr -CA l4_ca.cert.pem -CAkey l4_ca.key.pem -CAcreateserial \
  -out l4_dev.cert.pem -days 730 -extfile l4_leaf.ext
openssl x509 -in l4_dev.cert.pem -noout -subject -issuer -ext keyUsage,extendedKeyUsage
openssl verify -CAfile l4_ca.cert.pem l4_dev.cert.pem
```

```text
Certificate request self-signature ok
subject=O=L4 Lab, CN=L4 Dev Update Signer
subject=O=L4 Lab, CN=L4 Dev Update Signer
issuer=O=L4 Lab, CN=L4 Dev Root CA
X509v3 Key Usage: critical
    Digital Signature
X509v3 Extended Key Usage: 
    Code Signing
l4_dev.cert.pem: OK
```

**출력에서 볼 것**: 서명 인증서에 `extendedKeyUsage=codeSigning`을 넣었다. 이게 다음 예제의 첫 함정이 된다. 키 관리(오프라인 루트, 개발 키 · 양산 키 분리, 폐기)는 L5에서 다룬다.

### 6.3 예제 7 — 번들 만들기 · 검증 · 변조 (VM 호스트의 RAUC 1.15.1)

**무엇을 확인하나**: `rauc bundle`로 verity 형식 번들을 만들고, `rauc info`로 서명을 검증하고, 바이트 하나를 바꿔 어디서 걸리는지 본다.

```sh
cd /var/tmp/L4/rauc && mkdir -p t/content
truncate -s 16M t/content/rootfs.ext4 && mkfs.ext4 -q -L testroot t/content/rootfs.ext4
cat > t/content/manifest.raucm <<EOF
[update]
compatible=l4-sensorkit
version=2.0.0-test

[bundle]
format=verity

[image.rootfs]
filename=rootfs.ext4
EOF
rauc bundle --cert=l4_dev.cert.pem --key=l4_dev.key.pem t/content t/test.raucb
rauc info --keyring=l4_ca.cert.pem t/test.raucb
```

```text
Creating 'verity' format bundle
Failed to create bundle: squashfs size (4096) must be larger than 4096 bytes
```

빈 ext4는 squashfs로 압축하면 4 KiB가 되어 거부된다(1 MiB 난수를 채워 다시 만들었다). 그다음 `rauc info`:

```text
rauc-Message: 21:16:43.956: Verifying bundle signature... 
signature verification failed: Verify error: unsuitable certificate purpose
```

**`unsuitable certificate purpose`**: RAUC는 기본으로 서명 인증서의 용도(purpose)를 검사하는데, 기본값은 `codeSigning`이 아니다. `system.conf`의 `[keyring] check-purpose=codesign`으로 맞추면 통과한다. 장비에 들어갈 `system.conf`가 이것이다.

```text
[system]
compatible=l4-sensorkit
bootloader=custom
statusfile=/data/rauc.status
bundle-formats=-plain

[handlers]
bootloader-custom-backend=/usr/local/sbin/l4-rauc-backend

[keyring]
path=/etc/rauc/l4_ca.cert.pem
check-purpose=codesign

[slot.rootfs.0]
device=/dev/vda1
type=ext4
bootname=A

[slot.rootfs.1]
device=/dev/vda2
type=ext4
bootname=B
```

```sh
rauc --conf=l4_host_system.conf info t/test.raucb     # keyring 경로만 호스트용으로 바꾼 사본
```

```text
rauc-Message: 21:16:51.485: Verified inline signature by 'O = L4 Lab, CN = L4 Dev Update Signer'
Compatible:     'l4-sensorkit'
Version:        '2.0.0-test'
Bundle Format:  verity
  Verity Salt:  'b817bd66e78090c68a03d8e0b3a10a1fadcf5262127884e262e631f15ef57995'
  Verity Hash:  'e80ac5e2cae93ec207d9908541a864f86aa3ddbfae9a63bcdcdd8cd157349b0d'
  Verity Size:  16384
Manifest Hash:  '59b828e81e32185b84219e3be71f47af7c6c35c5d15ef59a782c7b450feebd80'

1 Image:
  [rootfs]
    Filename:  rootfs.ext4
    Type:      ext4 (detected)
    Checksum:  1264dcc2f6bed049b2929354b3b8a2fcc3e1785b937dd09dca261ba79f3b66d6
    Size:      16.8 MB (16777216 bytes)

Certificate Chain:
 0 Subject: O = L4 Lab, CN = L4 Dev Update Signer
   Issuer: O = L4 Lab, CN = L4 Dev Root CA
 1 Subject: O = L4 Lab, CN = L4 Dev Root CA
   Issuer: O = L4 Lab, CN = L4 Dev Root CA
```

(출력 일부 생략.) 이제 변조 시험이다.

```sh
cd /var/tmp/L4/rauc/t; C=../l4_host_system.conf
cp test.raucb tamper_payload.raucb; printf "\xff" | dd of=tamper_payload.raucb bs=1 seek=500000 conv=notrunc status=none
rauc --conf=$C info tamper_payload.raucb                       # verity, payload 1바이트
sz=$(stat -c %s test.raucb); cp test.raucb tamper_sig.raucb
printf "\x00" | dd of=tamper_sig.raucb bs=1 seek=$((sz-200)) conv=notrunc status=none
rauc --conf=$C info tamper_sig.raucb                           # verity, CMS 서명 1바이트
sed -i "s/format=verity/format=plain/" content/manifest.raucm
rauc bundle --cert=../l4_dev.cert.pem --key=../l4_dev.key.pem content plain.raucb
cp plain.raucb tamper_plain.raucb; printf "\xff" | dd of=tamper_plain.raucb bs=1 seek=500000 conv=notrunc status=none
rauc --conf=$C info tamper_plain.raucb                         # plain, payload 1바이트
rauc --keyring=other_ca.pem info test.raucb                    # 남의 CA로 검증
```

```text
== verity bundle, 1 byte flipped in payload:
rauc-Message: 21:17:03.137: Verified inline signature by 'O = L4 Lab, CN = L4 Dev Update Signer'
Compatible:     'l4-sensorkit'
== verity bundle, 1 byte zeroed in CMS signature:
signature verification failed: error:0200008A:rsa routines::invalid padding
== plain bundle intact:
rauc-Message: 21:17:03.486: Verified detached signature by 'O = L4 Lab, CN = L4 Dev Update Signer'
Bundle format 'plain' not allowed
== plain bundle, 1 byte flipped in payload:
signature verification failed: error:1700009E:CMS routines::verification failure
== intact bundle, wrong keyring:
signature verification failed: Verify error: unable to get local issuer certificate
```

**출력에서 볼 것**: 결과가 형식마다 다르다. 이게 RAUC 번들 형식의 핵심 차이다.

| 형식 | 서명이 덮는 것 | payload 변조를 언제 잡나 |
|---|---|---|
| `plain` | squashfs **전체** (detached CMS) | `rauc info` 시점 — 번들 전체를 해시하니 수백 MB를 읽는다 |
| `verity` | manifest(안에 dm-verity root hash) | **읽는 순간** — 블록 단위로 dm-verity가 검사. `info`는 서명만 보니 통과한다 |
| `crypt` | verity + 이미지 암호화 (장비별 키로 암호화된 대칭키) | verity와 같음 + 기밀성 |

`bundle-formats=-plain`이라 plain 번들은 서명이 맞아도 거부되었다. plain 형식은 "검증한 파일"과 "설치할 때 다시 읽는 파일"이 같다는 보장이 약해서(TOCTOU) 새 설계에서는 verity를 쓴다. payload를 바꾼 verity 번들이 정말 잡히는지는 추출로 확인한다.

```sh
sudo mkdir -p rt && sudo cp test.raucb tamper_payload.raucb rt/ && cd rt   # root 소유로
sudo rauc --conf=../../l4_host_system.conf extract test.raucb x_test
sudo rauc --conf=../../l4_host_system.conf extract tamper_payload.raucb x_tamper_payload
```

```text
== extract test
rauc-Message: 21:17:18.498: Verifying bundle payload... 
manifest.raucm
rootfs.ext4
== extract tamper_payload
rauc-Message: 21:17:18.582: Verifying bundle payload... 
rauc-Message: 21:17:18.583: Verification failed at position 499712.
rauc-Message: 21:17:18.583: Verification of data area failed.
Failed to extract bundle: bundle payload is corrupted
```

**출력에서 볼 것**: 바꾼 바이트(500000)가 들어 있는 4 KiB 블록의 시작(499712 = 122 × 4096)에서 정확히 실패한다. 처음엔 사용자 소유 파일로 시도했다가 `cannot check bundle payload without exclusive access: unsafe bundle uid 501`로 거부되었다 — 검증 후 사용자가 파일을 바꿔치기하는 공격을 막는 규칙이다.

### 6.4 custom bootloader backend — RAUC와 우리 U-Boot 스크립트를 잇는 다리

RAUC의 `bootloader=uboot` backend는 RAUC 관례 변수(`BOOT_ORDER`, `BOOT_A_LEFT`, `BOOT_B_LEFT`)를 쓴다. 이 랩은 U-Boot 내장 bootcount를 보여 주려고 변수 체계가 다르므로, `bootloader=custom`으로 RAUC가 부르는 스크립트를 직접 짰다. RAUC는 이 스크립트를 `get-primary`, `set-primary <slot>`, `get-state <slot>`, `set-state <slot> good|bad` 형태로 호출한다.

```sh
#!/bin/sh
# /usr/local/sbin/l4-rauc-backend: RAUC custom backend -> L4 U-Boot env
cmd=$1; slot=$2; val=$3
get() { fw_printenv -n "$1" 2>/dev/null; }
commit() { t=$(mktemp); cat > "$t"; fw_setenv -s "$t"; rc=$?; rm -f "$t"; return $rc; }
logger -t l4-rauc-backend "$cmd $slot $val"
case "$cmd" in
  get-primary) get boot_slot ;;
  set-primary) printf 'boot_slot=%s\nupgrade_available=1\nbootcount=0\n' "$slot" | commit ;;
  get-state)   s=$(get "l4_state_$slot"); echo "${s:-good}" ;;
  set-state)
    if [ "$val" = good ] && [ "$slot" = "$(get boot_slot)" ]; then
      printf 'l4_state_%s=good\nupgrade_available=0\nbootcount=0\n' "$slot" | commit
    else
      printf 'l4_state_%s=%s\n' "$slot" "$val" | commit
    fi ;;
  get-current) sed -n 's/.*l4\.slot=\([AB]\).*/\1/p' /proc/cmdline ;;
  *) echo "unknown command $cmd" >&2; exit 1 ;;
esac
```

`set-primary`가 boot-commit 프로토콜 2단계("B로, 시험으로")이고, 지금 부팅된 슬롯에 대한 `set-state good`이 4단계(commit)다. 각 경우 env 변경을 **`fw_setenv -s` 한 번**으로 묶었다.

Ubuntu의 `rauc` 패키지에는 서비스용 파일(D-Bus 정책 · systemd 유닛)이 `rauc-service` 패키지로 분리되어 있고 랩 이미지에는 없었다. 그래서 첫 시도는 `The name de.pengutronix.rauc was not provided by any .service files`로 실패했고, 패키지를 설치하는 대신 세 파일을 직접 넣었다(D-Bus 정책 `de.pengutronix.rauc.conf`, 활성화 파일, `rauc.service`의 `ExecStart=/usr/bin/rauc --mount=/run/rauc service`). 장비 이미지에 RAUC를 넣을 때 "CLI만 있고 서비스가 없다"는 흔한 누락이다.

### 6.5 health check 서비스

```sh
#!/bin/sh
# /usr/local/sbin/l4-health: commit the slot if healthy, otherwise reboot (bootcount decides)
slot=$(sed -n 's/.*l4\.slot=\([AB]\).*/\1/p' /proc/cmdline)
pending=$(fw_printenv -n upgrade_available 2>/dev/null)
echo "l4-health: version=$(cat /etc/l4-version) slot=$slot upgrade_available=$pending bootcount=$(fw_printenv -n bootcount)"
ok=1
systemctl is-active -q systemd-networkd || { echo "l4-health: FAIL networkd"; ok=0; }
findmnt -n /data >/dev/null           || { echo "l4-health: FAIL /data not mounted"; ok=0; }
[ -e /etc/l4-break-health ]           && { echo "l4-health: FAIL injected fault"; ok=0; }
if [ "$ok" = 1 ]; then
  if [ "$pending" = 1 ]; then rauc status mark-good && echo "l4-health: COMMITTED slot $slot"; fi
  echo "l4-health: healthy"
elif [ "$pending" = 1 ]; then
  echo "l4-health: UNHEALTHY trial boot -> reboot without commit"; systemctl reboot
else
  echo "l4-health: unhealthy but committed slot -> stay up, raise alarm"
fi
```

```text
# /etc/systemd/system/l4-health.service
[Unit]
Description=L4 post-update health check (commit or reboot)
After=network.target systemd-networkd.service data.mount
Wants=systemd-networkd.service

[Service]
Type=oneshot
ExecStartPre=/bin/sleep 5
ExecStart=/usr/local/sbin/l4-health
StandardOutput=journal+console

[Install]
WantedBy=multi-user.target
```

설계 포인트: (1) **시험 부팅(`upgrade_available=1`)일 때만** 실패 → 재부팅을 한다. 확정된 슬롯에서 health가 나쁘다고 재부팅하면 무한 루프다. (2) `After=multi-user.target`를 쓰면 `WantedBy=multi-user.target`와 순서 순환이 생기므로 필요한 유닛에만 순서를 건다(L1). (3) 실제 제품이라면 판정 대상은 networkd가 아니라 **카메라 장치 열림, IMU 샘플 수신, 앱의 자체 진단**이고, 판정 전까지 하드웨어 watchdog을 켜 둔다. upstream RAUC에는 같은 일을 하는 `rauc-mark-good.service` 예제가 있다. systemd 자체도 `boot-complete.target` + `systemd-bless-boot`(systemd-boot 전용 boot counting)이라는 같은 패턴을 가지고 있다.

### 6.6 예제 8 — 시나리오 1: A(1.0.0) → B(2.0.0) 설치 · 시험 부팅 · commit

**무엇을 확인하나**: 공장 상태(A 활성)에서 `rauc install` → env 변화 → 재부팅 → U-Boot가 B를 시험 부팅 → health check가 `mark-good` → 다음 부팅부터 env 쓰기 없음. expect 스크립트로 QEMU 시리얼 콘솔을 조작했다.

```sh
# l4lib.tcl: spawn qemu ... -bios /var/tmp/L4/build1/u-boot.bin -drive if=pflash,unit=1,...,file=l4_pf.img
#            -drive file=l4_disk.img,if=virtio,format=raw -netdev user,id=n0 -device virtio-net-pci,netdev=n0
# s1_good.exp:
source /Users/donh/.elinux_scratch/L4/exp/l4lib.tcl
qemu_start /var/tmp/L4/s1_good.log
shell
run {cat /etc/l4-version; cat /proc/mtd; fw_printenv boot_slot upgrade_available bootcount l4_state_A l4_state_B}
run {rauc status}
run {time rauc install /data/l4_2.0.0.raucb} 1800
run {fw_printenv boot_slot upgrade_available bootcount l4_state_A l4_state_B; rauc status}
send "reboot\r"
shell
run {cat /etc/l4-version; cat /proc/cmdline}
run {journalctl -b -u l4-health --no-pager -o cat}
run {fw_printenv boot_slot upgrade_available bootcount l4_state_A l4_state_B}
run {rauc status --detailed | head -n 40}
send "reboot\r"
shell
run {cat /etc/l4-version; fw_printenv boot_slot upgrade_available bootcount}
send "poweroff\r"
expect -timeout 300 eof
```

설치 전:

```text
L4# cat /etc/l4-version; cat /proc/mtd; fw_printenv boot_slot upgrade_available bootcount l4_state_A l4_state_B
1.0.0
dev:    size   erasesize  name
mtd0: 08000000 00040000 "0.flash"
boot_slot=A
upgrade_available=0
bootcount=0
l4_state_A=good
l4_state_B=good
L4# rauc status
=== System Info ===
Compatible:  l4-sensorkit
Booted from: rootfs.0 (A)
=== Bootloader ===
Activated: rootfs.0 (A)
=== Slot States ===
○ [rootfs.1] (/dev/vda2, ext4, inactive)
	bootname: B
	boot status: good
⏺ [rootfs.0] (/dev/vda1, ext4, booted)
	bootname: A
	mounted: /
	boot status: good
```

설치:

```text
L4# time rauc install /data/l4_2.0.0.raucb
installing
  0% Installing
  0% Determining slot states
 10% Checking bundle
 10% Verifying signature
 20% Verifying signature done.
 20% Checking manifest contents
 30% Determining target install group
 40% Updating slots
 40% Checking slot rootfs.1
 46% Checking slot rootfs.1 done.
 46% Copying image to rootfs.1
 47% Copying image to rootfs.1
...
 99% Copying image to rootfs.1 done.
100% Installing done.
Installing `/data/l4_2.0.0.raucb` succeeded
real	0m27.379s
L4# fw_printenv boot_slot upgrade_available bootcount l4_state_A l4_state_B; rauc status
boot_slot=B
upgrade_available=1
bootcount=0
l4_state_A=good
l4_state_B=bad
=== Bootloader ===
Activated: rootfs.1 (B)
=== Slot States ===
⏺ [rootfs.1] (/dev/vda2, ext4, inactive)
	bootname: B
	boot status: bad
○ [rootfs.0] (/dev/vda1, ext4, booted)
	bootname: A
	boot status: good
```

(진행률 줄 일부 생략.) RAUC가 자동으로 **비활성 슬롯(rootfs.1 = B)**을 골랐다. 설치 후 env: `boot_slot=B`, `upgrade_available=1`(set-primary), 그리고 `l4_state_B=bad` — 쓰기 전에 B를 bad로 표시한 흔적이다. "Activated: B"인데 boot status가 bad인 것은 "다음 부팅은 B지만 아직 증명되지 않았다"는 뜻이다. 800 MiB 이미지 기록이 TCG에서 27초.

재부팅 후 U-Boot와 B의 첫 부팅:

```text
Saving Environment to Flash... Un-Protected 2 sectors
Erasing Flash...
.. done
Erased 2 sectors
Writing to Flash... done
Protected 2 sectors
OK
Hit any key to stop autoboot: 0
L4: boot_slot=B part=2 bootcount=1 upgrade_available=1
54151680 bytes read in 515 ms (100.3 MiB/s)
Starting kernel ...
...
L4# cat /etc/l4-version; cat /proc/cmdline
2.0.0
console=ttyAMA0 root=/dev/vda2 rw rootwait panic=5 l4.slot=B rauc.slot=B
L4# journalctl -b -u l4-health --no-pager -o cat
Starting l4-health.service - L4 post-update health check (commit or reboot)...
l4-health: version=2.0.0 slot=B upgrade_available=1 bootcount=1
rauc status: marked slot rootfs.1 as good
l4-health: COMMITTED slot B
l4-health: healthy
L4# fw_printenv boot_slot upgrade_available bootcount l4_state_A l4_state_B
boot_slot=B
upgrade_available=0
bootcount=0
l4_state_A=good
l4_state_B=good
L4# rauc status --detailed | head -n 40
Booted from: rootfs.1 (B)
Activated: rootfs.1 (B)
⏺ [rootfs.1] (/dev/vda2, ext4, booted)
	boot status: good
      slot status:
          bundle:
              compatible=l4-sensorkit
              version=2.0.0
          checksum:
              sha256=a829ea1beecdbc8e7552319adadd705d040405e0a450561a5dc08180867bc1b2
              size=838.9 MB (838860800 bytes)
          installed:
              timestamp=2026-10-18T05:33:37Z
              count=1
          status=ok
```

그리고 한 번 더 재부팅:

```text
Loading Environment from Flash...Loading Environment from Flash... OK
Hit any key to stop autoboot: 0
L4: boot_slot=B part=2 bootcount=1 upgrade_available=0
...
L4# cat /etc/l4-version; fw_printenv boot_slot upgrade_available bootcount
2.0.0
boot_slot=B
upgrade_available=0
bootcount=0
```

**출력에서 볼 것**: (1) 시험 부팅에서는 U-Boot가 bootcmd보다 먼저 `Saving Environment`(bootcount=1 저장)를 했다. (2) B의 health check가 `rauc status mark-good` → custom backend `set-state B good` → `upgrade_available=0 bootcount=0`. (3) commit 후 재부팅에서는 **`Saving Environment`가 없다** — 확정 상태에서는 env를 전혀 쓰지 않는다(콘솔의 `bootcount=1`은 RAM 값, §4.3). (4) RAUC의 슬롯 상태 파일(`/data/rauc.status`)에 설치된 번들 버전 · sha256 · 시각이 남는다. 서버 보고 · 감사 로그의 근거가 된다. 전체 시나리오(부팅 3회 포함)가 TCG에서 1분 44초 걸렸다.

---

## 7. 깨진 업데이트 → 자동 롤백 (커널 panic)

### 7.1 예제 9 — 시나리오 2: B(2.0.0)에서 A에 3.0.0-broken 설치

**무엇을 확인하나**: init이 즉시 종료하는 이미지를 설치하면 커널이 `Attempted to kill init!`으로 panic → `panic=5`로 재부팅 → bootcount 1, 2, 3 → 네 번째에 `altbootcmd` → B로 롤백. 이번에는 B가 활성이니 RAUC가 **A**에 쓴다(대칭).

```text
L4# cat /etc/l4-version; fw_printenv boot_slot upgrade_available bootcount
2.0.0
boot_slot=B
upgrade_available=0
bootcount=0
L4# rauc install /data/l4_3.0.0-broken.raucb 2>&1 | tail -n 3
100% Installing done.
Installing `/data/l4_3.0.0-broken.raucb` succeeded
L4# fw_printenv boot_slot upgrade_available bootcount l4_state_A l4_state_B
boot_slot=A
upgrade_available=1
bootcount=0
l4_state_A=bad
l4_state_B=good
L4# reboot
```

재부팅 이후 콘솔에서 핵심 줄만 골랐다(커널 부팅 메시지 생략).

```text
U-Boot 2026.10 (Oct 17 2026 - 22:31:07 -0700)
Saving Environment to Flash... Un-Protected 2 sectors
L4: boot_slot=A part=1 bootcount=1 upgrade_available=1
[    1.316472] Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000100
[    1.321239] Rebooting in 5 seconds..
U-Boot 2026.10 (Oct 17 2026 - 22:31:07 -0700)
Saving Environment to Flash... Un-Protected 2 sectors
L4: boot_slot=A part=1 bootcount=2 upgrade_available=1
L4 v3-broken: init exits on purpose
[    1.288394] Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000100
[    1.292812] Rebooting in 5 seconds..
U-Boot 2026.10 (Oct 17 2026 - 22:31:07 -0700)
Saving Environment to Flash... Un-Protected 2 sectors
L4: boot_slot=A part=1 bootcount=3 upgrade_available=1
L4 v3-broken: init exits on purpose
[    1.316937] Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000100
[    1.321702] Rebooting in 5 seconds..
U-Boot 2026.10 (Oct 17 2026 - 22:31:07 -0700)
Saving Environment to Flash... Un-Protected 2 sectors
Erasing Flash...
Writing to Flash... done
OK
Warning: Bootlimit (3) exceeded. Using altbootcmd.
Hit any key to stop autoboot: 1Hit any key to stop autoboot: 0
L4: bootlimit exceeded on slot A -> rollback
Saving Environment to Flash... Un-Protected 2 sectors
Erasing Flash...
Writing to Flash... done
OK
L4: boot_slot=B part=2 bootcount=0 upgrade_available=0
54151680 bytes read in 250 ms (206.6 MiB/s)
Starting kernel ...
...
L4# cat /etc/l4-version; cat /proc/cmdline; fw_printenv boot_slot upgrade_available bootcount l4_state_A l4_state_B
2.0.0
console=ttyAMA0 root=/dev/vda2 rw rootwait panic=5 l4.slot=B rauc.slot=B
boot_slot=B
upgrade_available=0
bootcount=0
l4_state_A=bad
l4_state_B=good
L4# rauc status
Booted from: rootfs.1 (B)
Activated: rootfs.1 (B)
⏺ [rootfs.1] (/dev/vda2, ext4, booted)
	boot status: good
○ [rootfs.0] (/dev/vda1, ext4, inactive)
	boot status: bad
```

**출력에서 볼 것**: 네 번째 U-Boot 진입에서 bootcount 저장(4) → `Bootlimit (3) exceeded` → altbootcmd가 **두 번째 env 저장**(slot=B, upg=0, count=0, A=bad)을 하고 B를 부팅했다. B로 돌아온 뒤 RAUC도 A를 `bad`로 본다 — 서버에 "3.0.0은 이 장비에서 부팅 실패"라고 보고할 정보가 남아 있다. 첫 부팅에서 `init exits on purpose` 줄이 안 보이는 것은 콘솔 출력이 panic 메시지와 섞였기 때문이다(두 번째부터 보인다).

### 7.2 예제 10 — 시나리오 3: 부팅은 되지만 health check가 실패

**무엇을 확인하나**: 커널도 systemd도 멀쩡하지만 앱 수준 판정이 실패하는 업데이트(4.0.0-unhealthy). 실무에서 더 흔한 경우다 — "카메라 드라이버가 새 커널과 안 맞아 `/dev/video0`이 없다" 같은 것.

```text
L4# rauc install /data/l4_4.0.0-unhealthy.raucb 2>&1 | tail -n 2
100% Installing done.
Installing `/data/l4_4.0.0-unhealthy.raucb` succeeded
L4# reboot
[   49.375827] reboot: Restarting system
U-Boot 2026.10 (Oct 17 2026 - 22:31:07 -0700)
Saving Environment to Flash... Un-Protected 2 sectors
L4: boot_slot=A part=1 bootcount=1 upgrade_available=1
[   21.134926] l4-health[320]: l4-health: version=4.0.0-unhealthy slot=A upgrade_available=1 bootcount=1
[   21.232987] l4-health[320]: l4-health: FAIL injected fault
[   21.234237] l4-health[320]: l4-health: UNHEALTHY trial boot -> reboot without commit
[   23.357114] reboot: Restarting system
U-Boot 2026.10 (Oct 17 2026 - 22:31:07 -0700)
Saving Environment to Flash... Un-Protected 2 sectors
L4: boot_slot=A part=1 bootcount=2 upgrade_available=1
[   19.091830] l4-health[321]: l4-health: version=4.0.0-unhealthy slot=A upgrade_available=1 bootcount=2
[   19.195595] l4-health[321]: l4-health: FAIL injected fault
[   19.196850] l4-health[321]: l4-health: UNHEALTHY trial boot -> reboot without commit
[   21.177327] reboot: Restarting system
U-Boot 2026.10 (Oct 17 2026 - 22:31:07 -0700)
Saving Environment to Flash... Un-Protected 2 sectors
L4: boot_slot=A part=1 bootcount=3 upgrade_available=1
[   19.180899] l4-health[323]: l4-health: version=4.0.0-unhealthy slot=A upgrade_available=1 bootcount=3
[   19.322633] l4-health[323]: l4-health: FAIL injected fault
[   19.323737] l4-health[323]: l4-health: UNHEALTHY trial boot -> reboot without commit
[   21.489572] reboot: Restarting system
U-Boot 2026.10 (Oct 17 2026 - 22:31:07 -0700)
Saving Environment to Flash... Un-Protected 2 sectors
Warning: Bootlimit (3) exceeded. Using altbootcmd.
L4: bootlimit exceeded on slot A -> rollback
Saving Environment to Flash... Un-Protected 2 sectors
L4: boot_slot=B part=2 bootcount=0 upgrade_available=0
[   19.239615] l4-health[322]: l4-health: version=2.0.0 slot=B upgrade_available=0 bootcount=0
[   19.344703] l4-health[322]: l4-health: healthy
L4# cat /etc/l4-version; fw_printenv boot_slot upgrade_available bootcount l4_state_A l4_state_B
2.0.0
boot_slot=B
upgrade_available=0
bootcount=0
l4_state_A=bad
```

(커널 메시지 생략, 줄은 콘솔 출력 그대로.) **출력에서 볼 것**: 이번에는 시험 부팅마다 systemd가 끝까지 올라오고(health 판정이 커널 시각 약 19~21초), health check가 commit 없이 재부팅한다. 결과는 panic 경우와 똑같이 bootcount → altbootcmd → B. **"재부팅 = 실패 신호"로 통일하면** 부트로더는 실패 원인(panic, 행, health 실패)을 구분할 필요가 없다. 원인은 pstore/journal에 남겨 나중에 서버로 올린다(L7).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="14">시나리오 2 — 3.0.0-broken (커널 panic)</text>
  <rect x="20" y="34" width="118" height="62" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="79" y="56" text-anchor="middle" font-size="12">bootcount=1</text>
  <text x="79" y="76" text-anchor="middle" font-size="12">panic 1.32 s</text>
  <rect x="148" y="34" width="118" height="62" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="207" y="56" text-anchor="middle" font-size="12">bootcount=2</text>
  <text x="207" y="76" text-anchor="middle" font-size="12">panic 1.29 s</text>
  <rect x="276" y="34" width="118" height="62" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="335" y="56" text-anchor="middle" font-size="12">bootcount=3</text>
  <text x="335" y="76" text-anchor="middle" font-size="12">panic 1.32 s</text>
  <rect x="404" y="34" width="118" height="62" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="463" y="56" text-anchor="middle" font-size="12">bootcount=4</text>
  <text x="463" y="76" text-anchor="middle" font-size="12">altbootcmd</text>
  <rect x="532" y="34" width="128" height="62" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="596" y="56" text-anchor="middle" font-size="12">B 2.0.0 부팅</text>
  <text x="596" y="76" text-anchor="middle" font-size="12">A=bad 기록</text>
  <text x="20" y="128" font-size="12">각 칸 = U-Boot 진입 1회. 진입마다 env 저장(bootcount++)이 먼저, 그다음 부팅 시도</text>
  <text x="20" y="176" font-size="14">시나리오 3 — 4.0.0-unhealthy (systemd OK, health 실패)</text>
  <rect x="20" y="188" width="118" height="62" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="79" y="210" text-anchor="middle" font-size="12">bootcount=1</text>
  <text x="79" y="230" text-anchor="middle" font-size="12">FAIL 21.2 s</text>
  <rect x="148" y="188" width="118" height="62" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="207" y="210" text-anchor="middle" font-size="12">bootcount=2</text>
  <text x="207" y="230" text-anchor="middle" font-size="12">FAIL 19.2 s</text>
  <rect x="276" y="188" width="118" height="62" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="335" y="210" text-anchor="middle" font-size="12">bootcount=3</text>
  <text x="335" y="230" text-anchor="middle" font-size="12">FAIL 19.3 s</text>
  <rect x="404" y="188" width="118" height="62" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="463" y="210" text-anchor="middle" font-size="12">bootcount=4</text>
  <text x="463" y="230" text-anchor="middle" font-size="12">altbootcmd</text>
  <rect x="532" y="188" width="128" height="62" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="596" y="210" text-anchor="middle" font-size="12">B 2.0.0 healthy</text>
  <text x="596" y="230" text-anchor="middle" font-size="12">19.3 s</text>
  <text x="20" y="282" font-size="12">시각은 각 부팅의 커널 타임스탬프(panic 또는 l4-health FAIL 줄), TCG 기준</text>
  <text x="20" y="302" font-size="12">bootlimit=3 → 시험 3회 + 롤백 1회. 실패 판정이 늦을수록(health) 롤백까지 총 시간이 길다</text>
</svg>
```

그림 6 — 예제 9 · 10의 실제 콘솔 로그에서 뽑은 bootcount 타임라인. panic은 부팅 1.3초 만에 실패가 드러나 롤백까지가 짧고, health 실패는 매번 systemd가 끝까지 올라와야 해서 길다. 필드에서 bootlimit을 정할 때 이 "실패 1회의 비용"을 고려한다(배터리 장비면 특히).

---

## 8. 업데이트 도중 전원 차단

### 8.1 예제 11 — 시나리오 4: 비활성 슬롯 쓰기 중 kill -9

**무엇을 확인하나**: B(2.0.0)가 활성인 상태에서 A에 설치를 시작하고, `Copying image to rootfs.0`가 뜬 12초 뒤 QEMU 프로세스를 `kill -9`로 죽인다(전원 차단 — 게스트 커널이 아무것도 flush하지 못한다, J5). 다시 켜면?

```text
# s4_powercut.exp (핵심 부분)
send "rauc install /data/l4_2.0.0.raucb\r"
expect -timeout 1800 "Copying image to rootfs"
sleep 12
send_user "\n### L4: kill -9 QEMU now (power cut during slot write)\n"
exec kill -9 [exp_pid]
catch {close}; catch {wait}
qemu_start /var/tmp/L4/s4_cut2.log
...
```

```text
L4# rauc install /data/l4_2.0.0.raucb
installing
...
 40% Checking slot rootfs.0
 46% Checking slot rootfs.0 done.
 46% Copying image to rootfs.0
### L4: kill -9 QEMU now (power cut during slot write)
spawn qemu-system-aarch64 -machine virt ... -bios /var/tmp/L4/build1/u-boot.bin ...
U-Boot 2026.10 (Oct 17 2026 - 22:31:07 -0700)
Loading Environment from Flash...Loading Environment from Flash... OK
Hit any key to stop autoboot: 0
L4: boot_slot=B part=2 bootcount=1 upgrade_available=0
...
L4# cat /etc/l4-version; cat /proc/cmdline; fw_printenv boot_slot upgrade_available bootcount l4_state_A l4_state_B
2.0.0
console=ttyAMA0 root=/dev/vda2 rw rootwait panic=5 l4.slot=B rauc.slot=B
boot_slot=B
upgrade_available=0
bootcount=0
l4_state_A=bad
l4_state_B=good
L4# rauc status
Booted from: rootfs.1 (B)
Activated: rootfs.1 (B)
⏺ [rootfs.1] (/dev/vda2, ext4, booted)
	boot status: good
○ [rootfs.0] (/dev/vda1, ext4, inactive)
	boot status: bad
L4# dumpe2fs -h /dev/vda1 2>&1 | head -n 3; fsck.ext4 -n /dev/vda1 2>&1 | tail -n 3
dumpe2fs 1.47.0 (5-Feb-2023)
Filesystem volume name:   root-2.0.0
Last mounted on:          <not available>
e2fsck 1.47.0 (5-Feb-2023)
root-2.0.0: clean, 12023/51296 files, 168264/204800 blocks
```

**출력에서 볼 것**: (1) env는 설치 전과 같다(`boot_slot=B`, `upgrade_available=0`) — 전환(set-primary)은 쓰기가 **끝난 뒤**에야 일어나므로 U-Boot는 아무 일 없었다는 듯 B를 부팅했다. (2) A는 설치 시작 시 기록된 `bad` 그대로다. RAUC든 U-Boot든 A로 갈 이유가 없다. (3) **무서운 부분**: 반쯤 쓰인 A를 `fsck -n`으로 보면 `clean`이다. 앞부분(슈퍼블록 · 그룹 디스크립터)은 새 이미지(label `root-2.0.0`)로 덮였고, 뒷부분은 이전 4.0.0의 블록이 남은 **혼합물**인데 메타데이터만 보면 멀쩡해 보인다. "파일시스템이 clean이니 그 슬롯을 써도 된다"는 판단은 틀렸다는 뜻이다. 슬롯의 진짜 상태는 **기록 완료 후의 해시 확인**(RAUC 상태 파일의 sha256)과 `bad`/`good` 표시만이 말해 준다. 다음 업데이트는 A를 처음부터 다시 쓴다.

### 8.2 차단 지점별 정리

| 차단 지점 | 그 순간의 env | 다음 부팅 | 위험 |
|---|---|---|---|
| 번들 다운로드 중 | 변화 없음 | A 정상 | 없음 (data 공간만) |
| B를 bad로 표시하는 env 쓰기 중 | redundant면 이전 사본 | A 정상 | single env면 env 전체 손상 → 기본 env (§4.7) |
| B 이미지 기록 중 (예제 11) | B=bad, slot=A | A 정상 | 없음 |
| set-primary env 쓰기 중 | redundant면 이전(= A 확정) 또는 새(= B 시험) | 둘 중 하나, 둘 다 안전 | single env면 위험 |
| B 시험 부팅 중 | slot=B, upg=1 | bootcount 진행 | 없음 (그게 설계) |
| commit env 쓰기 중 | redundant면 이전(= 시험 중) | 다시 시험 → 다시 commit | 없음 |
| **부트로더 자체 업데이트 중** | — | **부트로더가 없을 수 있음** | **벽돌** (§12.2) |

single-copy env가 위험한 이유가 이 표에 다 있다 — 이 랩의 통합 시나리오가 single env를 쓴 것은 에뮬레이터 제약 때문이지 권장 구성이 아니다. 실제 보드에서는 redundant env가 사실상 필수다.

---
## 9. RAUC 더 깊이

### 9.1 슬롯 · 슬롯 그룹 · 슬롯 타입

`system.conf`의 `[slot.<class>.<index>]`가 슬롯 하나다. class가 같은 슬롯끼리 A/B를 이루고(`rootfs.0`/`rootfs.1`), `parent=`로 "rootfs.0이 선택되면 appfs.0도 같이"처럼 묶는다(슬롯 그룹 — 예: rootfs + 앱 파티션 + 펌웨어 파티션이 함께 전환). `type=`은 쓰는 방법을 정한다.

| type | 쓰는 방법 | 쓸 때 |
|---|---|---|
| `raw` | 이미지를 블록 장치에 그대로 | 커널 파티션, 펌웨어 blob |
| `ext4` · `vfat` | 이미지가 같은 형식이면 raw 복사, tar면 mkfs 후 풀기 | 이 랩 |
| `ubivol` · `ubifs` · `nand` · `nor` | UBI 볼륨 업데이트(원자적), MTD 직접 | raw NAND 장비 (J4) |
| `boot-emmc` | eMMC boot0/boot1 중 비활성 쪽에 쓰고 EXT_CSD로 전환 | **부트로더 업데이트** (§12.2) |
| `boot-mbr-switch` · `boot-gpt-switch` · `boot-raw-fallback` | 파티션 테이블 항목 전환, ROM의 fallback 오프셋 이용 | 부트로더 영역이 파티션 하나뿐인 SoC |

### 9.2 bootloader backend

| backend | 상태 저장 | 비고 |
|---|---|---|
| `uboot` | `BOOT_ORDER`, `BOOT_x_LEFT` env (`fw_setenv`) | RAUC 제공 U-Boot 스크립트 예제(`contrib/uboot.sh`)와 짝 |
| `barebox` | barebox state 프레임워크 (`bootchooser`) | Pengutronix 계열 보드 |
| `grub` | grubenv 파일 | x86 장비 |
| `efi` | EFI `BootOrder`/`BootNext` 변수 | UEFI 장비 (C8) |
| `custom` | 직접 짠 스크립트 | 이 랩 (U-Boot 내장 bootcount와 연결) |

### 9.3 hooks와 handlers

번들에 `hooks.sh`를 넣고 manifest에 `[hooks] filename=hooks.sh`, 이미지 쪽에 `hooks=pre-install;post-install` 식으로 선언한다. `install-check`(번들 수준 — 설치 전에 장비가 이 번들을 받아도 되는지 직접 판단, 예: 버전 하한 확인), 슬롯 `pre-install`/`post-install`(예: 새 rootfs를 mount해서 장비별 설정 · 인증서를 옛 슬롯에서 복사), `install`(기본 쓰기 방법을 대체)이 있다. 장비 쪽 `system.conf [handlers]`에는 `system-info`(장비 시리얼 등 변수 제공), `pre-install`, `post-install` 같은 시스템 handler를 둔다. **데이터 마이그레이션 · 장비별 설정 이월은 대개 post-install hook에서 한다**.

### 9.4 큰 번들을 다루는 기능

- **스트리밍 설치**(verity 번들 + HTTP): 번들을 장비 저장소에 내려받지 않고 HTTP range 요청으로 필요한 블록만 읽으며 dm-verity로 검증한다(nbd 경유). data 파티션에 번들 공간(이 랩에서 번들 하나 186 MiB)이 필요 없어진다.
- **adaptive update**(`adaptive=block-hash-index`): 대상 슬롯 · 다른 슬롯에 이미 같은 해시의 4 KiB 블록이 있으면 그걸 복사하고 없는 블록만 번들(스트리밍이면 네트워크)에서 읽는다. delta를 미리 만들 필요가 없는 "자동 delta"다.
- **casync 번들**(`rauc convert`): 이미지를 content-defined chunk로 쪼개 chunk store에 두고 인덱스만 번들에 넣는다. 장비는 없는 chunk만 받는다(desync를 쓴다).
- **crypt 번들**: 번들 payload를 대칭키로 암호화하고 그 키를 장비별 공개키로 암호화한다(`rauc encrypt`). 펌웨어 기밀성이 필요한 국방 장비에서 나오는 요구다.

---

## 10. SWUpdate — 프레임워크형 업데이터

### 10.1 개념

SWUpdate(Stefano Babic, DENX, GPL-2.0)는 "업데이트 **프레임워크**"다. RAUC가 "슬롯을 정의하고 번들을 그 슬롯에 넣는다"는 고정된 모델이라면, SWUpdate는 `sw-description`이라는 설명서와 **handler**(무엇을 어떻게 쓸지)의 조합으로 거의 무엇이든 표현한다. 단일 복사(recovery) 모드도, A/B도, 둘 다 쓸 수 있다.

| 요소 | 내용 |
|---|---|
| `.swu` | **cpio**(newc + CRC, 매직 `070702`) 아카이브. 첫 파일이 `sw-description`, 서명이면 둘째가 `sw-description.sig`, 그 뒤 이미지들 |
| `sw-description` | libconfig 또는 JSON. 버전, 하드웨어 호환성, 이미지 목록(파일 · 장치 · handler · sha256), bootenv, 스크립트 |
| handler | `raw`, `rawfile`, `flash`(MTD), `ubivol`, `archive`(tar), `remote`, `shellscript`, `lua`, `rdiff_image`, `delta`(zchunk), `docker_imageload` … |
| 무결성 | 각 이미지의 `sha256`을 sw-description에 쓰고, **sw-description 하나만 서명**한다 (서명 → 해시 → 이미지 체인) |
| 서명 | RSA(PKCS#1.5), CMS(PKCS#7), GPG 중 빌드 옵션으로 선택 |
| 선택 | `software = { 보드 = { stable = { copy1: {...}; copy2: {...}; }; }; }` 구조 + `-e stable,copy2`로 A/B 대상 선택 |
| 서버 연동 | `suricatta` 모드 — Eclipse hawkBit(DDI API), 일반 HTTP 서버, Lua로 짠 커스텀 |
| 스크립팅 | Lua 내장 — 설치 전후 스크립트, 커스텀 handler, 이미지 선택 로직 |

### 10.2 예제 12 — .swu를 손으로 만들고 서명하기

**무엇을 확인하나**: 도구 없이 `openssl` + `cpio`만으로 서명된 .swu를 만든다. 서명 체계는 §6.2의 CA를 그대로 쓴다(CMS).

```sh
#!/bin/bash
# l4_mkswu.sh: build a signed SWUpdate .swu by hand: sw-description + CMS signature + gz rootfs
set -e
cd /var/tmp/L4 && rm -rf swu && mkdir swu && cd swu
echo 2.0.0-swu | sudo tee ../stage/etc/l4-version >/dev/null
truncate -s 800M rootfs.ext4 && sudo mkfs.ext4 -q -F -L root-2.0.0-swu -d ../stage rootfs.ext4
echo 1.0.0 | sudo tee ../stage/etc/l4-version >/dev/null
gzip -1 -c rootfs.ext4 > rootfs.ext4.gz && rm rootfs.ext4
SHA=$(sha256sum rootfs.ext4.gz | cut -d" " -f1)
cat > sw-description <<EOD
software =
{
    version = "2.0.0";
    description = "L4 sensor kit rootfs (slot B)";
    hardware-compatibility: [ "1.0" ];
    images: (
        {
            filename = "rootfs.ext4.gz";
            device = "/dev/vda2";
            type = "raw";
            compressed = "zlib";
            sha256 = "$SHA";
        }
    );
    bootenv: (
        { name = "boot_slot"; value = "B"; },
        { name = "upgrade_available"; value = "1"; },
        { name = "bootcount"; value = "0"; }
    );
};
EOD
openssl cms -sign -in sw-description -out sw-description.sig -signer ../rauc/l4_dev.cert.pem \
  -inkey ../rauc/l4_dev.key.pem -outform DER -nosmimecap -binary
printf '%s\n' sw-description sw-description.sig rootfs.ext4.gz | cpio -o -H crc --quiet > l4_2.0.0.swu
# tampered copy: change the target device inside sw-description, keep the old signature
mkdir tampered && sed 's#/dev/vda2#/dev/vda1#' sw-description > tampered/sw-description
cp sw-description.sig rootfs.ext4.gz tampered/
(cd tampered && printf '%s\n' sw-description sw-description.sig rootfs.ext4.gz | cpio -o -H crc --quiet > ../l4_tampered.swu)
rm -rf tampered rootfs.ext4.gz
cpio -itv < l4_2.0.0.swu
```

```text
-rw-rw-r--   1 donh     donh          560 Oct 17 22:42 sw-description
-rw-rw-r--   1 donh     donh         1778 Oct 17 22:42 sw-description.sig
-rw-rw-r--   1 donh     donh     214571645 Oct 17 22:42 rootfs.ext4.gz
419091 blocks
```

SWUpdate가 하는 서명 검증을 호스트의 openssl로 똑같이 해 본다.

```sh
cd /var/tmp/L4/swu; mkdir -p t && (cd t && cpio -id --quiet < ../l4_tampered.swu sw-description sw-description.sig)
for d in . t; do echo "== $d"; openssl cms -verify -inform DER -in $d/sw-description.sig -content $d/sw-description \
  -CAfile ../rauc/l4_ca.cert.pem -purpose any -binary -out /dev/null 2>&1 | head -n 2; done
diff sw-description t/sw-description
```

```text
== .
CMS Verification successful
== t
CMS Verification failure
E00CD612ECE00000:error:1700009E:CMS routines:CMS_SignerInfo_verify_content:verification failure:../crypto/cms/cms_sd.c:1083:
9c9
<             device = "/dev/vda2";
---
>             device = "/dev/vda1";
```

**출력에서 볼 것**: 공격자가 이미지를 그대로 두고 **설치 대상만 `/dev/vda1`(활성 슬롯)로 바꾼** sw-description은 서명이 깨진다. 이미지 자체를 바꾸면 sha256이 sw-description과 안 맞는다. 그래서 sw-description 하나만 서명해도 전체가 보호된다.

### 10.3 예제 13 — 게스트에서 `swupdate -c` (check 모드)

**무엇을 확인하나**: 게스트의 SWUpdate 2023.12.1(Ubuntu 빌드, wolfSSL PKCS#7)로 서명 · 하드웨어 호환성 · 이미지 해시를 설치 없이 검사한다.

```text
L4# TMPDIR=/data/tmp swupdate -c -v -k /data/l4_ca.cert.pem -H l4kit:1.0 --cert-purpose codeSigning -i /data/l4_2.0.0.swu 2>&1 | grep -a -i 'verified\|hardware\|Found compressed\|hash\|success\|fail\|error' | head -n 20
[TRACE] : SWUPDATE running :  [swupdate_verify_file] : Verified OK
[TRACE] : SWUPDATE running :  [_parse_images] : Found compressed Image: rootfs.ext4.gz in device : /dev/vda2 for handler raw
[TRACE] : SWUPDATE running :  [check_hw_compatibility] : Hardware l4kit Revision: 1.0
[TRACE] : SWUPDATE running :  [check_hw_compatibility] : Hardware compatibility verified
[INFO ] : SWUPDATE successful ! SWUPDATE successful !
[INFO ] : SWUPDATE running :  [endupdate] : SWUpdate was successful !
L4# TMPDIR=/data/tmp swupdate -c -k /data/l4_ca.cert.pem -H l4kit:1.0 --cert-purpose codeSigning -i /data/l4_tampered.swu 2>&1 | grep -a -i 'verif\|error\|fail' | head -n 6
[ERROR] : SWUPDATE failed [0] ERROR : Signature verification failed
[ERROR] : SWUPDATE failed [0] ERROR : Compatible SW not found
swupdate_image_write failed: Connection reset by peer
[ERROR] : SWUPDATE failed [1] Image invalid or corrupted. Not installing ...
[ERROR] : SWUPDATE failed [0] ERROR : SWUpdate *failed* !
```

여기까지 오는 데 걸린 함정을 순서대로 적는다(전부 실제로 겪었다).

| 시도 | 결과 | 원인 · 해결 |
|---|---|---|
| 호스트에서 `chroot stage swupdate -c ...` | `Error loading certificate chain` · `Crypto cannot be initialized` | chroot 안에서만 실패(`/proc`·`/dev` 없는 chroot에서 wolfSSL 초기화 문제로 추정). 실제 부팅한 게스트에서는 같은 파일로 `Verified OK` |
| `-k` 없이 | `SWUpdate is built for signed images, provide a public key file.` | 이 빌드는 서명 필수 (`CONFIG_SIGNED_IMAGES`) — 좋은 기본값 |
| hardware-compatibility 없이 | `HW compatibility not found` → `Found nothing to install` | 이 빌드는 하드웨어 호환성 검사가 켜져 있다. sw-description에 `hardware-compatibility` + 장비에 `/etc/hwrevision` 또는 `-H 보드:리비전` |
| `/data` 여유 8.5 MB | `Not enough free space to extract rootfs.ext4.gz (needed 214571645 ...)` | check 모드는 이미지를 임시 디렉터리에 풀어 해시를 검사한다. 실제 설치는 기본적으로 스트리밍이지만, 이미지를 미리 풀어야 하는 경우를 대비해 공간 계산이 필요하다 |

**출력에서 볼 것**: 서명 검증(`Verified OK`) → 이미지 목록 해석(handler `raw`, 대상 `/dev/vda2`) → 하드웨어 호환성 → (check 모드라 쓰기 생략) → 성공. 변조본은 서명 단계에서 끝난다. `--cert-purpose` 없이(기본 emailProtection) 돌린 경우에도 오류 줄이 없었다 — 이 wolfSSL 빌드가 purpose를 RAUC처럼 엄격히 보지 않는다는 뜻이니, 서명 인증서 정책은 도구 기본값에 기대지 말고 CA 설계로 지킨다.

> **주의**: 이 `.swu`는 `/dev/vda2`를 고정으로 적었다. 게스트가 마침 B(=vda2)로 부팅해 있었으므로 **실제 설치를 했으면 활성 rootfs를 덮어썼을 것이다**. 그래서 check 모드만 돌렸다. 실제 A/B 구성에서는 `copy1`/`copy2` 두 이미지 세트를 만들고 현재 슬롯에 따라 `-e stable,copy2`처럼 고르거나(`swupdate -g`로 현재 root 확인), Lua로 고른다. 이 실수가 "A/B를 직접 짜면 생기는 대표 버그"다 — RAUC는 비활성 슬롯 선택을 프레임워크가 해 준다.

---

## 11. Mender, 그리고 셋 비교

### 11.1 Mender

Mender(Northern.tech, 클라이언트 Apache-2.0)는 **클라이언트 + 서버**를 한 제품으로 준다. 서버(오픈소스판 · 호스팅 · Enterprise)에 장비가 붙고, 웹 UI에서 그룹을 만들어 배포한다.

| 요소 | 내용 |
|---|---|
| Artifact (`.mender`) | tar 기반 형식(버전 3). 헤더(호환 장비 타입, 의존성 · 제공 값) + payload + 서명 |
| update module | `rootfs-image`(A/B 전체 교체), `single-file`, `directory`, `deb`, `docker` 등. 플러그인 스크립트로 확장 |
| state script | `Download_Enter`, `ArtifactInstall_Leave`, `ArtifactReboot_Enter`, `ArtifactCommit_Enter`, `ArtifactRollback_Leave`, `ArtifactFailure_Enter` … 각 상태 전후에 장비 쪽 스크립트 실행 (health check, 사용자 동의, 데이터 마이그레이션) |
| U-Boot 연동 | `mender_boot_part`, `upgrade_available`, **`bootcount`**, `altbootcmd` — 이 노트가 직접 만든 것과 같은 U-Boot 내장 bootcount 메커니즘을 쓴다 |
| 통합 | Yocto `meta-mender`, Debian/Ubuntu 이미지는 `mender-convert` |
| 유료 기능 | binary delta, 동적 그룹, RBAC, 감사 로그 등 일부는 Professional/Enterprise 플랜 (기능 경계는 버전마다 바뀌니 도입 때 확인) |

### 11.2 비교

| 항목 | RAUC | SWUpdate | Mender |
|---|---|---|---|
| 성격 | 장비 쪽 업데이트 클라이언트 (슬롯 모델) | 업데이트 프레임워크 (handler 모델) | 클라이언트 + 서버 + UI |
| 라이선스 | LGPL-2.1 | GPL-2.0 (일부 LGPL) | 클라이언트 Apache-2.0, 서버 오픈소스 + 상용 |
| 패키지 형식 | `.raucb` = squashfs + CMS (plain/verity/crypt) | `.swu` = cpio + sw-description(+sig) | `.mender` = tar + 헤더 |
| 무결성 | CMS 서명 + dm-verity(블록 단위) | sw-description 서명 + 이미지별 sha256 | 서명 + 체크섬 |
| A/B 대상 선택 | 프레임워크가 비활성 슬롯 자동 선택 | 직접 (`-e`, Lua) | 프레임워크 |
| 부트로더 | uboot · barebox · grub · efi · custom | uboot · grub · EFI Boot Guard · 직접 | uboot · grub (통합 필요) |
| 서버 | 없음 (hawkBit 연동 별도) | suricatta: hawkBit · HTTP · Lua | 자체 서버 |
| delta | adaptive(block-hash-index), casync | rdiff, zchunk(delta handler) | binary delta (상용) |
| 단일 복사 · recovery | 가능하지만 주력 아님 | 강함 (initramfs recovery 흔함) | 주로 A/B |
| 이 랩에서 | 설치 · 롤백 · 차단 전부 실행 | 서명 · check 모드 실행 | 개념만 (실행 안 함) |

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <line x1="60" y1="290" x2="650" y2="290" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="60" y1="290" x2="60" y2="24" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="560" y="314" text-anchor="middle" font-size="12">서버 · 플릿 관리 포함 정도 →</text>
  <text x="70" y="20" font-size="12">유연성 (무엇이든 표현) ↑</text>
  <rect x="90" y="48" width="200" height="66" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="190" y="74" text-anchor="middle" font-size="13">SWUpdate</text>
  <text x="190" y="96" text-anchor="middle" font-size="12">handler · Lua · 단일 복사</text>
  <rect x="90" y="170" width="200" height="66" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="190" y="196" text-anchor="middle" font-size="13">RAUC</text>
  <text x="190" y="218" text-anchor="middle" font-size="12">슬롯 모델 · verity 번들</text>
  <rect x="420" y="170" width="200" height="66" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="520" y="196" text-anchor="middle" font-size="13">Mender</text>
  <text x="520" y="218" text-anchor="middle" font-size="12">클라이언트 + 서버 + UI</text>
  <rect x="420" y="48" width="200" height="66" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="520" y="74" text-anchor="middle" font-size="13">hawkBit (서버만)</text>
  <text x="520" y="96" text-anchor="middle" font-size="12">RAUC · SWUpdate와 짝</text>
  <line x1="290" y1="203" x2="416" y2="90" stroke="currentColor" stroke-dasharray="4 3"/>
  <line x1="290" y1="81" x2="416" y2="81" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="352" y="70" text-anchor="middle" font-size="12">suricatta</text>
  <text x="330" y="160" text-anchor="middle" font-size="12">rauc-hawkbit-updater</text>
</svg>
```

그림 7 — 세 도구의 위치. 고르는 질문은 대개 둘이다. "서버를 직접 만들 건가(또는 hawkBit을 쓸 건가), 사 올 건가?" 그리고 "업데이트 형태가 단순한 A/B인가, 이것저것 섞인 복잡한 장비인가?" 단순 A/B + 견고함이면 RAUC, 이상한 하드웨어(MCU 펌웨어 · FPGA 비트스트림 · 여러 저장소)를 한 패키지로 다루려면 SWUpdate, 서버까지 빨리 갖추려면 Mender가 흔한 선택이다.

---

## 12. delta 업데이트 — 대역폭과 바꾸는 것

### 12.1 방식

| 방식 | 원리 | 장비 쪽 요구 | 대표 |
|---|---|---|---|
| 바이너리 diff (bsdiff, xdelta3/VCDIFF) | 옛 이미지 → 새 이미지 패치를 서버에서 미리 계산 | 정확히 그 옛 이미지가 있어야 함 · bsdiff는 메모리를 많이 씀 | Android 옛 OTA(bsdiff/imgdiff), Mender binary delta |
| rsync 계열 (rdiff/librsync) | 옛 이미지의 서명(블록 해시) 대비 delta | 옛 이미지 | SWUpdate `rdiff_image` handler |
| zstd `--patch-from` | 옛 파일을 사전(dictionary)으로 새 파일을 압축 | 옛 이미지 + 메모리 (`--long`) | 아래 실측 |
| 청크 dedupe (casync/desync, zchunk) | 이미지를 chunk로 쪼개 해시 인덱스만 받고 없는 chunk만 다운로드 | 로컬 chunk 출처(옛 슬롯) | RAUC casync, SWUpdate delta(zchunk) |
| 블록 해시 재사용 | 슬롯에 이미 있는 블록 재사용 | 옛 슬롯 | RAUC adaptive |
| 파일 단위 | 바뀐 파일 객체만 | OSTree 저장소 | OSTree static delta |

공통 함정: **delta는 "출발점"이 정확할 때만 적용된다**. 장비마다 rootfs가 미세하게 다르면(누가 rw로 mount해서 파일을 바꿨다든가) 패치가 실패한다. 그래서 delta는 read-only rootfs(J3)와 짝이고, 실패하면 full로 떨어지는 경로가 필요하다.

### 12.2 예제 14 — 실측: 앱 8 MiB + 버전 파일만 바뀐 v1 → v2

**무엇을 확인하나**: 같은 stage 트리에서 8 MiB 난수 "앱 바이너리"와 `/etc/l4-version`만 바꿔 ext4 이미지 두 개를 만들고, (1) 실제로 달라진 4 KiB 블록 수, (2) full 압축 vs `zstd --patch-from` delta 크기를 잰다. mkfs의 UUID · hash seed를 랜덤으로 둘 때와 고정할 때를 비교한다.

```sh
#!/bin/bash
# l4_delta.sh: full vs delta for v1 -> v2 (8 MiB app + /etc/l4-version change)
set -e
cd /var/tmp/L4 && mkdir -p delta && cd delta
mk() { echo "$1" | sudo tee ../stage/etc/l4-version >/dev/null
  rm -f "$2"; truncate -s 800M "$2"; sudo mkfs.ext4 -q -F -L rootfs $3 -d ../stage "$2"; }
cmpblk() { python3 -I - "$1" "$2" <<'PY'
import sys
a, b = open(sys.argv[1], 'rb'), open(sys.argv[2], 'rb'); n = d = 0
while True:
    x, y = a.read(4096), b.read(4096)
    if not x: break
    n += 1; d += (x != y)
print(f"4K blocks: {n}, differing: {d} ({d*4/1024:.1f} MiB)")
PY
}
U=3f0c3a52-1b7e-4d1a-9c39-0c0ffee014ab
head -c 8M /dev/urandom > app_v1.bin; head -c 8M /dev/urandom > app_v2.bin
sudo cp app_v1.bin ../stage/usr/local/bin/l4_app.bin; mk 1.0.0 v1.ext4
sudo cp app_v2.bin ../stage/usr/local/bin/l4_app.bin; mk 2.0.0 v2.ext4
echo "[random UUID]"; cmpblk v1.ext4 v2.ext4
sudo cp app_v1.bin ../stage/usr/local/bin/l4_app.bin; mk 1.0.0 v1f.ext4 "-U $U -E hash_seed=$U"
sudo cp app_v2.bin ../stage/usr/local/bin/l4_app.bin; mk 2.0.0 v2f.ext4 "-U $U -E hash_seed=$U"
echo "[fixed UUID + hash_seed]"; cmpblk v1f.ext4 v2f.ext4
/usr/bin/time -f "full zstd -19: %es" zstd -q -19 -T2 -f v2f.ext4 -o v2f.full.zst
/usr/bin/time -f "delta (random UUID): %es" zstd -q -19 -T2 -f --long=31 --patch-from=v1.ext4 v2.ext4 -o v2.delta.zst
/usr/bin/time -f "delta (fixed UUID): %es" zstd -q -19 -T2 -f --long=31 --patch-from=v1f.ext4 v2f.ext4 -o v2f.delta.zst
zstd -q -d -f --long=31 --patch-from=v1f.ext4 v2f.delta.zst -o v2f.rebuilt.ext4 && cmp v2f.rebuilt.ext4 v2f.ext4 && echo "delta apply: bit-identical"
ls -l *.zst | awk '{printf "%-18s %10d bytes (%.1f MiB)\n", $9, $5, $5/1048576}'
```

```text
[random UUID]
4K blocks: 204800, differing: 4567 (17.8 MiB)
[fixed UUID + hash_seed]
4K blocks: 204800, differing: 2805 (11.0 MiB)
full zstd -19: 127.24s
delta (random UUID): 21.78s
delta (fixed UUID): 19.11s
delta apply: bit-identical
v2.delta.zst          8812643 bytes (8.4 MiB)
v2f.delta.zst         8810877 bytes (8.4 MiB)
v2f.full.zst        154300088 bytes (147.2 MiB)
```

(실제 실행본은 UUID 문자열을 셸 치환으로 만들었다. 같은 값이다. 정리용 `rm` 줄은 생략.)

**출력에서 볼 것**:

- full 전송은 147 MiB(zstd -19), RAUC verity 번들(squashfs 기본 압축)은 186 MiB인데, 실제로 바뀐 내용은 8 MiB 앱 + 수 KiB다. delta는 **8.4 MiB** — 거의 "바뀐 앱 크기"까지 줄었다. 1만 대 × LTE라면 1.47 TB 대 84 GB 차이다.
- **mkfs가 UUID와 hash seed를 랜덤으로 고르면** 내용이 같아도 메타데이터 블록(슈퍼블록, 그룹 디스크립터, 디렉터리 해시, `metadata_csum`이 UUID로 seed된 체크섬)이 달라져 달라진 블록이 17.8 MiB로 늘어난다. 고정하면 11.0 MiB(8 MiB 앱 + 할당 위치가 바뀐 블록). zstd는 내용 매칭이라 둘 다 8.4 MiB였지만, **블록 해시 기반 방식(RAUC adaptive, casync)은 바로 이 "달라진 블록 수"에 비례**한다. 재현 가능한 이미지 빌드(K5의 SOURCE_DATE_EPOCH, 고정 UUID)가 delta 효율과 직결된다.
- full 압축 127초, delta 19~22초는 VM CPU 시간이다(TCG 아님). 장비 쪽 적용 비용(메모리 · CPU)도 따로 재야 한다. zstd `--long=31`은 패치 적용에도 큰 윈도 메모리가 든다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13">v1 → v2 전송량 (MiB, 실측) — 바뀐 것: 앱 8 MiB + 버전 파일</text>
  <line x1="250" y1="34" x2="250" y2="266" stroke="currentColor"/>
  <text x="240" y="62" text-anchor="end" font-size="12">ext4 이미지 원본</text>
  <rect x="250" y="46" width="330" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="588" y="63" font-size="12">800</text>
  <text x="240" y="102" text-anchor="end" font-size="12">RAUC verity 번들</text>
  <rect x="250" y="86" width="76.8" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="334" y="103" font-size="12">186.3</text>
  <text x="240" y="142" text-anchor="end" font-size="12">zstd -19 전체</text>
  <rect x="250" y="126" width="60.7" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="318" y="143" font-size="12">147.2</text>
  <text x="240" y="182" text-anchor="end" font-size="12">달라진 블록 (랜덤 UUID)</text>
  <rect x="250" y="166" width="7.3" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="266" y="183" font-size="12">17.8</text>
  <text x="240" y="222" text-anchor="end" font-size="12">달라진 블록 (고정 UUID)</text>
  <rect x="250" y="206" width="4.5" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="262" y="223" font-size="12">11.0</text>
  <text x="240" y="262" text-anchor="end" font-size="12">zstd --patch-from</text>
  <rect x="250" y="246" width="3.5" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="261" y="263" font-size="12">8.4</text>
  <text x="250" y="290" font-size="12">선형 축 (330 px = 800 MiB). 번들은 앱 없는 v2 기준</text>
</svg>
```

그림 8 — full과 delta의 전송량 비교(예제 8 · 14 실측). "달라진 블록"은 블록 해시 기반 delta(RAUC adaptive, casync)의 하한에 해당한다.

---

## 13. 슬롯 밖에 있는 것들 — 데이터 · 부트로더 · anti-rollback

### 13.1 data 파티션과 마이그레이션

A와 B는 하나의 `/data`를 공유한다. v2가 첫 부팅에 설정 DB 스키마를 v2 형식으로 바꿨는데 그 부팅이 health check에 실패해 v1으로 롤백되면, **v1은 자기가 모르는 형식의 데이터**를 만난다. 롤백은 슬롯만 되돌리지 데이터는 되돌리지 않는다.

실무 규칙:

- 스키마 변경은 **commit 이후**에 한다(health check가 통과하고 mark-good 한 다음). 시험 부팅 중에는 데이터를 읽기만 하거나 새 위치에 복사본으로 쓴다.
- 데이터에 버전 번호를 둔다(`/data/config/schema_version`). 새 버전은 옛 형식을 읽을 줄 알아야 하고(forward migration), 가능하면 옛 버전도 새 형식을 **무시하고 안전하게 실패**해야 한다.
- 버전별 디렉터리(`/data/app/v2/`)로 나누고 commit 후에 옛것을 지우는 방법도 흔하다(공간 2배).
- 장비 고유 데이터(시리얼, 인증서, 캘리브레이션 값 — 예: IMU 바이어스, 카메라 렌즈 보정)는 별도 파티션이나 read-only 영역에 두고 업데이트가 절대 건드리지 않게 한다.
- `/etc`가 rootfs 안에 있으면 장비별 설정이 업데이트로 사라진다. RAUC post-install hook으로 이월하거나, 설정을 `/data`로 빼고 rootfs는 read-only(J3)로 둔다.

### 13.2 부트로더 업데이트 — 가장 무서운 부분

rootfs는 부트로더가 되돌려 준다. 그런데 **부트로더를 업데이트하다 실패하면 되돌려 줄 주체가 없다**. 대응은 하드웨어(BootROM)가 무엇을 지원하는지에 달려 있다(C2 §10).

| 방법 | 원리 | 남는 위험 |
|---|---|---|
| eMMC boot0/boot1 | 비활성 boot 파티션에 쓰고, 읽어 검증한 뒤 EXT_CSD `BOOT_PARTITION_ENABLE` 1바이트로 전환 (RAUC `boot-emmc`) | 전환 바이트 쓰기는 eMMC 내부적으로 원자적. 새 부트로더가 "부팅은 되는데 env를 못 읽는" 식의 기능 버그 |
| ROM의 이중 이미지 지원 | i.MX 계열 secondary image, 일부 SoC의 fallback 오프셋 · 매직 검사 | ROM의 fallback 조건(서명 실패 · 헤더 손상만인지)을 정확히 알아야 함 |
| SPI NOR 두 영역 + 고정 1단 | 아주 작은 1단 로더(절대 업데이트 안 함)가 2단 A/B를 고름 | 1단 버그는 영원히 남음 |
| 업데이트 안 함 | 부트로더는 출하 상태 고정, 정말 필요할 때만 서비스 센터 | 보안 취약점 패치 불가 |

설계 원칙: (1) 부트로더 업데이트는 **rootfs 업데이트와 분리**하고 빈도를 최소화한다. (2) 새 부트로더 + 옛 rootfs, 옛 부트로더 + 새 rootfs 조합이 모두 부팅되게 **호환성을 유지**한다(env 형식 · 변수 이름 · 부팅 스크립트). (3) 쓰기 → 다시 읽어 해시 검증 → 그다음에만 전환. (4) 전원 차단 시험을 부트로더 업데이트 경로에 대해 별도로 수백 번 돌린다. (5) 최후의 수단(USB 복구 모드 · JTAG · 제조 지그)을 문서화한다.

### 13.3 anti-rollback과 서명 — L5와 연결

서명은 "우리가 만든 이미지"만 보증하지 "최신 이미지"는 보증하지 않는다. 공격자가 **취약점이 있던 옛 버전의 정상 서명 번들**을 설치하면 서명 검증은 통과한다. 그래서:

- 업데이트 에이전트 수준: 버전 하한 검사 — SWUpdate `-N/--no-downgrading <version>`(게스트 `swupdate -h`에 있다), RAUC `install-check` hook, Mender 의존성 · 제공 값.
- 부팅 체인 수준: 하드웨어 단조 카운터(eFuse, eMMC RPMB, TPM NV counter)에 "최소 보안 버전"을 두고 부트로더 · 커널 이미지(FIT)의 버전과 비교한다. 이 체인과 키 관리는 L5.
- **롤백(이 노트의 자동 복귀)과 anti-rollback은 충돌하지 않게** 설계한다. 보안 카운터는 새 버전이 **commit된 뒤**에만 올린다. 시험 부팅 전에 올리면 롤백할 슬롯이 부팅 불가가 된다.

---

## 14. 플릿 롤아웃 — 1만 대에 배포하기

장비 한 대에서 도는 boot-commit 프로토콜은 "한 대가 벽돌이 되지 않게" 한다. 플릿 롤아웃은 "**나쁜 버전이 1만 대 전부에 가지 않게**" 한다.

| 단계 | 내용 |
|---|---|
| 내부 장비 (dogfood) | 사내 · 시험 랙 장비 수십 대에 먼저. 전원 차단 · 롤백 시험 포함 |
| canary 1% | 하드웨어 리비전 · 지역 · 고객을 섞어 무작위 표본 |
| 단계 확대 | 1% → 5% → 25% → 100%, 단계마다 관찰 기간(예: 24~72시간) |
| 게이트 지표 | 설치 성공률, 자동 롤백률, 부팅 시간, 크래시율(L7), 앱 KPI(센서 데이터 수신률, 카메라 프레임 드롭) — **canary 그룹 대 아직 안 바꾼 대조군**을 비교 |
| 자동 중단 | 롤백률이 임계값(예: 0.5%)을 넘으면 배포 정지 · 알림 |
| 조건부 설치 | 배터리 > 50%, 충전 중, 작업 중 아님(로봇이 비행 · 주행 중이 아님), 유지보수 시간대 |
| 호환 규칙 | "v3는 v2.5 이상에서만" 같은 경로 제약 → 디딤돌(stepping-stone) 업데이트 |
| 보고 | 장비는 설치 결과 · 슬롯 상태 · 롤백 원인(pstore, journal 요약)을 서버로 |

시간 차원도 있다. 필드 장비 상당수는 몇 주씩 오프라인이다. 그래서 서버는 "지금 버전 분포"를 늘 보여 줘야 하고, 아주 오래된 버전에서 최신으로 바로 올라오는 경로를 시험해 둬야 한다.

### 14.1 OTA 테스트 — 무엇을 자동화하나

| 시험 | 방법 (이 노트 · 관련 노트) |
|---|---|
| 업데이트 중 전원 차단 | 상태 전이마다 차단: 다운로드 · 기록(예제 11) · env 쓰기 · 시험 부팅 · commit. 실제 보드는 전원 릴레이 · 프로그래머블 전원, QEMU는 kill -9. 수백~수천 회 반복 |
| 저장장치 결함 | dm-flakey · fault injection(J5), eMMC 쓰기 오류 |
| 나쁜 번들 | 서명 오류 · 다른 CA(예제 7), compatible 불일치, 잘린 파일, 변조 payload(예제 7 extract) |
| 나쁜 이미지 | panic(예제 9), health 실패(예제 10), 행(watchdog), 부팅 시간 초과 |
| 다운그레이드 시도 | 옛 서명 번들 → 거부되는지 |
| env 손상 | 사본 하나 · 둘(예제 3) |
| 자원 부족 | data 파티션 가득(예제 13에서 실제로 겪은 것), 메모리 부족 중 설치 |
| 장기 반복 | 1000회 연속 업데이트 · 롤백 루프 — flash 마모와 드문 경쟁 조건 |
| 시계 | RTC 없는 장비의 1970년 시각 → 인증서 "not yet valid" |

---
## 15. 커널 안에서는 무슨 일이 일어나나

OTA는 대부분 userspace 일이지만, 각 단계가 커널의 어느 경로를 지나는지 알아 두면 실패 메시지를 읽을 수 있다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="110" y="20" text-anchor="middle" font-size="13">env 쓰기</text>
  <text x="340" y="20" text-anchor="middle" font-size="13">번들 검증 · 슬롯 기록</text>
  <text x="570" y="20" text-anchor="middle" font-size="13">재부팅 · panic</text>
  <rect x="20" y="32" width="180" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="55" text-anchor="middle" font-size="12">fw_setenv (libubootenv)</text>
  <rect x="250" y="32" width="180" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="55" text-anchor="middle" font-size="12">rauc service</text>
  <rect x="480" y="32" width="180" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="570" y="55" text-anchor="middle" font-size="12">systemctl reboot</text>
  <line x1="20" y1="90" x2="660" y2="90" stroke="#888" stroke-dasharray="5 4"/>
  <text x="664" y="86" text-anchor="end" font-size="12">syscall</text>
  <rect x="20" y="104" width="180" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="110" y="124" text-anchor="middle" font-size="12">mtdchar: MEMERASE</text>
  <text x="110" y="142" text-anchor="middle" font-size="12">ioctl · write</text>
  <rect x="20" y="170" width="180" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="110" y="190" text-anchor="middle" font-size="12">mtd_erase · mtd_write</text>
  <text x="110" y="208" text-anchor="middle" font-size="12">mtd core</text>
  <rect x="20" y="236" width="180" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="110" y="256" text-anchor="middle" font-size="12">cfi_cmdset_0001</text>
  <text x="110" y="274" text-anchor="middle" font-size="12">do_write_buffer</text>
  <rect x="250" y="104" width="180" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="124" text-anchor="middle" font-size="12">loop (LOOP_CONFIGURE)</text>
  <text x="340" y="142" text-anchor="middle" font-size="12">번들 파일 → 블록 장치</text>
  <rect x="250" y="170" width="180" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="190" text-anchor="middle" font-size="12">dm-verity (DM ioctl)</text>
  <text x="340" y="208" text-anchor="middle" font-size="12">블록 읽을 때 해시 검사</text>
  <rect x="250" y="236" width="180" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="256" text-anchor="middle" font-size="12">squashfs mount</text>
  <text x="340" y="274" text-anchor="middle" font-size="12">→ /dev/vda1에 write</text>
  <rect x="480" y="104" width="180" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="124" text-anchor="middle" font-size="12">reboot(2) RESTART</text>
  <text x="570" y="142" text-anchor="middle" font-size="12">kernel_restart()</text>
  <rect x="480" y="170" width="180" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="190" text-anchor="middle" font-size="12">panic() → panic=5</text>
  <text x="570" y="208" text-anchor="middle" font-size="12">emergency_restart()</text>
  <rect x="480" y="236" width="180" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="256" text-anchor="middle" font-size="12">PSCI SYSTEM_RESET</text>
  <text x="570" y="274" text-anchor="middle" font-size="12">SMC/HVC → 펌웨어</text>
  <line x1="110" y1="154" x2="110" y2="166" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="110" y1="220" x2="110" y2="232" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="340" y1="154" x2="340" y2="166" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="340" y1="220" x2="340" y2="232" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="570" y1="154" x2="570" y2="166" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="570" y1="220" x2="570" y2="232" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="310" width="640" height="54" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="332" text-anchor="middle" font-size="12">하드웨어: CFI NOR (pflash) · virtio-blk 디스크 · 리셋 — U-Boot가 다시 env를 읽고 bootcount</text>
  <text x="340" y="352" text-anchor="middle" font-size="12">예제 5의 "buffer write error (status timeout)"는 왼쪽 맨 아래 상자에서 나온 메시지</text>
</svg>
```

그림 9 — OTA 각 단계가 지나는 커널 경로. 세 경로 모두 결국 "하드웨어 상태를 바꾸고 부트로더가 다음에 그것을 읽는다"로 끝난다.

- **env 쓰기**: `fw_setenv`는 `/dev/mtd0`을 열고 `MEMGETINFO` → `MEMUNLOCK` → `MEMERASE` ioctl → `write()`를 한다(예제 5가 그대로 재현). 커널에서는 `drivers/mtd/mtdchar.c`의 `mtdchar_ioctl()`/`mtdchar_write()` → `mtd_erase()`/`mtd_write()`(`drivers/mtd/mtdcore.c`) → CFI 칩 드라이버 `drivers/mtd/chips/cfi_cmdset_0001.c`(Intel/Sharp 명령 세트)의 `cfi_intelext_erase_varsize()`, `cfi_intelext_write_buffers()` → `do_write_buffer()`. 예제 5의 `buffer write error (status timeout)`는 `do_write_buffer()`가 칩 상태 레지스터의 ready 비트를 기다리다 포기할 때 찍는 메시지다. Don이 NAND 펌웨어에서 status polling timeout을 디버깅하던 그 구조가 그대로 커널 드라이버에 있다. eMMC env라면 같은 일이 블록 장치 `write()` + `fsync()`로 일어나고, 그 내구성 경로는 J5에서 본 FLUSH/FUA다.
- **번들 검증 · 설치**: RAUC는 verity 번들을 loop 장치(`LOOP_CONFIGURE` ioctl)로 블록 장치로 만들고, device-mapper ioctl로 verity 테이블(root hash = 서명된 manifest 안의 값)을 올린 뒤, 그 위에 squashfs를 read-only mount한다(J3의 dm-verity가 rootfs 대신 번들에 쓰이는 것). 이미지를 읽는 순간 `drivers/md/dm-verity-target.c`가 블록 해시를 확인하고 틀리면 I/O 에러를 낸다 — 예제 7의 `Verification failed at position 499712`가 그 결과다. 슬롯 기록은 평범한 블록 장치 `write()`이고 끝에 `fsync()`로 장치 캐시까지 내린다.
- **재부팅**: `systemctl reboot` → `reboot(2)`(`LINUX_REBOOT_CMD_RESTART`) → `kernel_restart()`(`kernel/reboot.c`) → arm64 `machine_restart()` → `do_kernel_restart()` → PSCI `SYSTEM_RESET`(`drivers/firmware/psci/psci.c`) SMC/HVC 호출 → QEMU(실제 보드라면 TF-A BL31)가 시스템을 리셋. 커널 panic이면 `panic()`(`kernel/panic.c`)이 `panic_timeout`(커널 인자 `panic=5`) 동안 기다린 뒤 `emergency_restart()`를 부른다. panic 후 리셋이 없으면 bootcount 루프가 돌지 않으므로 **`panic=` 설정과 하드웨어 watchdog**이 OTA 롤백의 전제 조건이다. 예제 8 로그의 `watchdog: watchdog0: watchdog did not stop!`은 systemd가 종료 직전까지 watchdog을 살려 두어, 종료 과정이 멈춰도 리셋되게 하는 동작이다(L1).

드라이버를 쓸 때 다시 만나는 지점: MTD 칩 드라이버 · `mtd_info` 콜백(H · J4), 블록 드라이버의 FLUSH/FUA 처리(I11 · J5), watchdog 드라이버의 `watchdog_ops`(I 모듈), 그리고 부트로더와 커널이 공유하는 장치(같은 NOR를 U-Boot와 Linux가 다르게 프로그램할 때의 호환성 — 예제 5).

---

## 16. Embedded 실무에서 다시 보기

### 16.1 센서 키트(드론)에 OTA 넣기 — 첫 설계 문서의 뼈대

예를 들어 i.MX8 + eMMC 16 GB 센서 키트라면 이렇게 시작한다.

```
eMMC boot0 / boot1 : SPL + U-Boot (A/B via EXT_CSD, rarely updated)
eMMC user area:
  p1 env (redundant, 2 x 16 KiB at fixed offsets)   <- or raw offsets before p1
  p2 rootA  (2 GiB, erofs/squashfs read-only + dm-verity, kernel+DTB inside FIT)
  p3 rootB  (2 GiB)
  p4 factory (1 MiB, read-only: serial, calib, device cert)
  p5 data   (rest, ext4/f2fs: logs, maps, config, update download area)
RAUC verity bundles, streaming over HTTPS from hawkBit
health = camera + IMU + GNSS + app self-test within 120 s, watchdog armed
bootlimit = 3, rollback reported via pstore + journal upload on next connect
```

이 뼈대에서 면접관이 파고드는 지점은 정해져 있다: env 위치와 이중화, rootfs read-only 여부(delta · verity와 연결), 캘리브레이션 데이터 보존, health 판정 기준, 부트로더 업데이트 방법, 서명 키 보관 위치.

### 16.2 "왜 롤백됐지?" 현장 디버깅

서버 대시보드에 롤백률 2%가 떴다. 순서:

1. 롤백된 장비의 env: `fw_printenv l4_state_A l4_state_B bootcount` — 어느 슬롯이 bad인가.
2. `rauc status --detailed` — 설치 시각 · 번들 버전 · sha256(설치가 온전했나).
3. 실패한 시험 부팅의 로그: journal이 영구 저장(`Storage=persistent`, `/data`나 `/var/log`가 공유 파티션)이면 `journalctl --list-boots`로 이전 부팅들을 본다. panic이면 pstore/ramoops(L7).
4. 공통점: 하드웨어 리비전? 특정 카메라 모듈? 부팅 시간이 health 타임아웃 근처? (이 랩에서 실제로 VM 과부하 때문에 udev coldplug가 90초를 넘어 장치 timeout이 났다 — 느린 eMMC 장비에서 그대로 일어날 수 있는 일이다.)

### 16.3 컨테이너 앱과 섞을 때

Jetson 같은 AI 장비는 베이스 OS(L4T, 커널 · CUDA 드라이버)를 A/B로, 추론 앱을 컨테이너로 배포하는 경우가 많다. 이때 **드라이버 · 라이브러리 ABI**(예: CUDA 드라이버 버전)가 베이스 OS와 앱 컨테이너 사이의 계약이 되므로, OS 업데이트의 health check에 "현재 앱 컨테이너가 새 OS에서 뜨는가"를 넣는다(L8).

---

## 17. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| health check 없이 커널만 뜨면 commit | 앱이 죽은 버전이 필드에 확정됨 | 판정 기준이 너무 느슨 | 앱 · 장치 수준 판정 + 판정 전까지 watchdog |
| 확정 슬롯에서도 health 실패 시 재부팅 | 무한 재부팅 | 시험 부팅 여부(`upgrade_available`)를 안 봄 | 시험 부팅일 때만 reboot, 확정이면 경보만 |
| `altbootcmd`에서 `saveenv`를 안 함 | 롤백 후에도 매 부팅 altbootcmd · bootcount 증가 | RAM에서만 바꿈 | 슬롯 · 플래그 · 카운터를 바꾸고 `saveenv` |
| `panic=` 미설정, watchdog 없음 | panic한 장비가 그대로 멈춤, 롤백 안 됨 | 리셋이 안 일어남 | `panic=5` + 하드웨어 watchdog |
| `fw_env.config`가 U-Boot와 불일치 | `fw_printenv` Bad CRC, commit이 안 먹음 | 오프셋 · 크기 · 섹터 다름 | 두 설정을 같은 값으로, 보드 bring-up 때 왕복 시험 |
| `fw_setenv -s`에 `key value` 형식 | rc=0인데 값이 안 바뀜 | 이 버전은 `key=value`만 해석 | 형식 확인 + 쓰고 나서 `fw_printenv`로 재확인 |
| 여러 env 변수를 따로따로 씀 | 전원 차단 시 어중간한 상태 | 쓰기가 여러 번 | `fw_setenv -s`로 한 번에 |
| single-copy env | 저장 중 차단 → 기본 env → distro boot | 이중화 없음 | redundant env (+ 기본 env에도 A/B 스크립트) |
| 저장 env가 기본 env를 대체한다는 걸 모름 | `kernel_addr_r` 등이 사라져 `load` 실패 | mkenvimage로 만든 env에 필수 변수 누락 | 기본 env를 바탕으로 덮어쓰기 목록을 만든다 |
| sw-description에 활성 슬롯 장치를 고정 | 동작 중인 rootfs를 덮어씀 | A/B 대상 선택 로직 없음 | copy1/copy2 + `-e`, 또는 RAUC처럼 프레임워크에 맡김 |
| 번들 서명 인증서 purpose 불일치 | `unsuitable certificate purpose` | CA의 EKU와 도구 기본값이 다름 | `check-purpose=codesign` 등으로 명시 |
| 데이터 스키마를 시험 부팅에서 변경 | 롤백 후 옛 버전이 데이터를 못 읽음 | 슬롯만 롤백됨 | commit 후 마이그레이션, 버전별 디렉터리 |
| 보안 카운터를 시험 부팅 전에 올림 | 롤백 슬롯이 anti-rollback에 막혀 벽돌 | 순서 오류 | commit 후 카운터 증가 |

---

## 18. 면접에서 이렇게 말한다

**Q.** Design an OTA update system for a fleet of 10,000 field units.

**A.** 요구사항부터 말한다: 원자성, 전원 차단 내성, 자동 롤백, 서명 검증, 대역폭, 다운타임. 장비 쪽은 A/B rootfs(커널 포함, read-only + dm-verity), redundant U-Boot env에 슬롯 · 시험 플래그 · bootcount, RAUC 같은 검증된 클라이언트로 verity 번들을 비활성 슬롯에 스트리밍 설치, 재부팅 후 health check가 commit, 실패하면 bootlimit 후 altbootcmd 롤백. 데이터는 별도 파티션, 마이그레이션은 commit 후. 서버 쪽은 hawkBit이나 Mender로 단계 배포(1% canary → 단계 확대), 롤백률 · 크래시율로 자동 중단, delta로 전송량 절감, 서명 키는 HSM. 부트로더 업데이트는 별도 경로로 드물게.

> I'd start from requirements: atomic, power-fail safe, automatic rollback, signed, bandwidth-aware, minimal downtime. On the device: symmetric A/B root partitions with the kernel inside the slot, read-only and protected by dm-verity, a redundant U-Boot environment holding the active slot, a trial flag and a boot counter. A proven client such as RAUC streams a signed verity bundle into the inactive slot, flips the slot with a single environment write and reboots. After boot a health check — not just "kernel came up" but the camera, IMU and the application's self-test — marks the slot good; if the device keeps rebooting, U-Boot's bootlimit triggers altbootcmd and we fall back. Persistent data lives in a separate partition and schema migrations happen only after commit. On the server side I'd use hawkBit or Mender for staged rollouts: internal units, a one-percent canary, then widening waves, with automatic halt on rollback or crash rate compared to a control group. Deltas cut LTE cost, signing keys stay in an HSM, and the bootloader gets its own rarely used, separately tested update path.

**꼬리 질문**: "canary가 1%면 1만 대 중 100대인데, 롤백률 0.5% 임계값을 어떻게 통계적으로 판단하나?" → 100대에서 0.5%는 0.5대다. 표본이 작은 첫 단계에서는 "롤백 1건이라도 나면 정지 후 분석", 단계가 커지면 비율 기준으로 바꾼다.

**Q.** What happens if power dies during the update? And during the bootloader update?

**A.** rootfs 업데이트는 어느 순간이든 안전하다. 활성 슬롯은 건드리지 않고, 슬롯 전환은 쓰기와 검증이 끝난 뒤의 env 쓰기 한 번이며, env는 두 벌이라 그 쓰기 도중 차단돼도 이전 사본이 남는다. 실제로 QEMU에서 슬롯 기록 도중 kill -9를 해 보니 그대로 기존 슬롯이 부팅됐고, 반쯤 쓴 슬롯은 fsck로는 clean으로 보였지만 bad로 표시돼 있었다. 부트로더는 다르다. 되돌려 줄 주체가 없으니 하드웨어가 지원하는 이중화(eMMC boot0/boot1 + EXT_CSD 전환, ROM의 secondary image)를 쓰고, 쓰기 → 읽기 검증 → 원자적 전환 순서를 지키고, 빈도를 최소화하고, 복구 모드를 마련한다.

> For the root filesystem, any point is safe by construction: we never touch the active slot, the switch is a single environment write that happens only after the new slot is written and verified, and the environment is redundant so an interrupted write leaves the previous copy valid. I actually tested this in QEMU by killing the VM while RAUC was copying the image — it came back on the old slot, and interestingly the half-written slot looked clean to fsck, which is why we trust the recorded hash and the bad flag, not the filesystem. The bootloader is the scary part because nothing below it can roll back. There I rely on whatever redundancy the boot ROM supports — eMMC boot0/boot1 with the EXT_CSD switch, or a secondary image — write the inactive copy, read it back and verify, then flip atomically, keep bootloader updates rare and backward compatible, and keep a documented recovery path like USB serial download.

**꼬리 질문**: "env 쓰기가 원자적이라는 건 정확히 무슨 뜻인가?" → 두 벌 중 안 쓰는 쪽을 지우고 쓰고, 마지막에 flag(NOR는 옛 사본 flag를 0으로, 블록 장치는 카운터 증가)로 유효 사본을 바꾼다. 읽는 쪽은 CRC가 맞는 사본 중 최신을 쓰므로, 어느 순간 끊겨도 "이전 값" 또는 "새 값" 둘 중 하나다.

**Q.** How do you decide that an update is "good"?

**A.** 단계별 기준과 시간 제한을 둔다. 부트로더는 서명된 이미지 로드, 커널은 panic 없는 rootfs mount(panic=N으로 리셋 보장), systemd는 필수 유닛 active, 그리고 핵심은 제품 기능 — 카메라 열림, IMU 데이터 흐름, 앱 자체 진단. 이 판정은 정해진 시간 안에 끝나야 하고, 그동안 하드웨어 watchdog을 켜 둔다. 네트워크 · 서버 연결은 현장에서 원래 끊기니 판정에 넣지 않고 보고에만 쓴다. 통과하면 mark-good으로 commit, 실패하면 commit 없이 재부팅해서 bootcount에 맡긴다.

> I define layered criteria with a deadline. The bootloader must load a correctly signed kernel; the kernel must mount root without panicking, and panic= plus a hardware watchdog guarantee that any failure turns into a reset. Then systemd's critical units must be active, and most importantly the product must work: the camera opens, IMU samples arrive, the application's self-test passes. All of that has to finish within, say, two minutes while the watchdog is armed. I deliberately don't require server connectivity, because field networks are flaky and I don't want a good update rolled back for that — connectivity is reported, not gated. Pass means mark-good, which clears the trial flag and boot counter; fail means reboot without committing, and the bootloader's counter decides when to fall back.

**Q.** Walk me through U-Boot's bootcount mechanism.

**A.** `CONFIG_BOOTCOUNT_LIMIT`과 저장 백엔드(예: `BOOTCOUNT_ENV`)를 켠다. 매 부팅 autoboot 직전에 `bootcount_inc()`가 카운터를 올려 저장하고, `bootcount > bootlimit`이면 `bootcmd` 대신 `altbootcmd`를 실행한다. env 백엔드는 `upgrade_available=1`일 때만 저장해서 평소 부팅에는 flash 쓰기가 없다. Linux는 health check 통과 후 `upgrade_available=0`, `bootcount=0`으로 지운다. altbootcmd는 슬롯 전환과 saveenv를 직접 해야 한다.

> You enable CONFIG_BOOTCOUNT_LIMIT and a storage backend — a reset-surviving register if the SoC has one, otherwise the environment. Before autoboot, bootcount_inc increments and stores the counter; then if bootcount exceeds bootlimit, U-Boot runs altbootcmd instead of bootcmd. Note it's strictly greater-than, so bootlimit=3 allows three trial boots and the fourth entry rolls back — that's exactly what I saw on the console. The environment backend only persists the counter while upgrade_available is 1, so committed devices don't write flash on every boot. Userspace clears upgrade_available and bootcount after a successful health check, and altbootcmd itself must switch the slot, clear the flags and saveenv, otherwise you loop.

**Q.** A/B or single-copy with a recovery partition — how do you choose?

**A.** 저장 공간과 다운타임 · 롤백 요구로 고른다. A/B는 rootfs × 2 공간이 들지만 백그라운드 설치 · 재부팅 1회 · 즉시 롤백. recovery는 공간이 적게 들지만 업데이트 중 장비가 멈추고 롤백 대상이 없다. eMMC가 넉넉하고 원격 장비면 A/B, 공간이 빡빡하고 현장 서비스가 가능하면 recovery, 국방 · 의료처럼 실패 비용이 크면 A/B + recovery.

> It's mostly storage versus downtime and rollback. A/B costs twice the root filesystem but installs in the background, needs one reboot, and rolls back instantly because the old slot is untouched. Single-copy with a small recovery system saves space, but the unit is down while it rewrites root and there's no previous version to fall back to — only a retry. For remote units with enough eMMC I pick A/B; for very constrained storage I'd use recovery; and where a dead unit is very expensive I'd do both — A/B plus a minimal recovery image as the last line of defense.

**Q.** How do you protect against someone installing an old, vulnerable but validly signed image?

**A.** 서명만으로는 못 막는다. 업데이트 에이전트에서 버전 하한 검사(SWUpdate `--no-downgrading`, RAUC install-check hook), 부팅 체인에서는 eFuse · RPMB · TPM NV의 단조 카운터와 이미지 보안 버전 비교(L5). 카운터는 새 버전이 commit된 뒤에만 올려야 롤백과 충돌하지 않는다.

> A signature only proves origin, not freshness. I'd enforce a minimum version in the update agent — SWUpdate has a no-downgrading option, RAUC can do it in an install-check hook — and, more importantly, in the boot chain with a hardware monotonic counter in eFuses, RPMB or a TPM NV index compared against a security version embedded in the signed images. The subtle part is ordering: bump the counter only after the new slot is committed, otherwise your automatic rollback target becomes unbootable.

**Q.** Full image or delta?

**A.** full은 단순하고 출발점과 무관하게 적용된다. delta는 전송량을 크게 줄인다 — 실측으로 앱 8 MiB만 바뀐 800 MiB rootfs에서 full 147 MiB 대 delta 8.4 MiB. 대신 정확한 출발 이미지가 필요하므로 read-only rootfs와 재현 가능한 빌드(고정 UUID 등)가 전제이고, 실패하면 full로 떨어지는 경로가 있어야 한다. 그래서 보통 "기본은 delta, 실패나 큰 버전 차이는 full".

> Full images are simple and don't depend on the device's current state; deltas save a lot of bandwidth. In a quick experiment, an 800 MiB root image where only an 8 MiB application changed was 147 MiB compressed as a full image versus 8.4 MiB as a zstd patch. But a delta needs an exact base, so it goes together with a read-only, verity-protected root and reproducible builds — even random filesystem UUIDs changed about 7 MiB more blocks in my test. I'd ship deltas by default with an automatic fallback to the full image, or use block- or chunk-reuse schemes like RAUC's adaptive updates or casync that degrade gracefully.

---

## 19. 직접 해보기

1. **개념**: `bootlimit=2`, `BOOTCOUNT_ENV`일 때 시험 부팅은 몇 번 허용되고, 몇 번째 U-Boot 진입에서 altbootcmd가 도는가? 확정 상태에서 U-Boot 콘솔의 `bootcount` 값이 1인 이유는?
   정답/힌트: 두 번 허용, 세 번째 진입(카운터 3 > 2). 확정 상태에서는 `bootcount_inc()`가 RAM 값만 1로 만들고 저장하지 않는다(§4.3).
2. **U-Boot 과제**: 예제 2의 env를 바꿔, altbootcmd가 RAUC 관례(`BOOT_ORDER="A B"`, `BOOT_A_LEFT=3`)처럼 동작하도록 bootcmd만으로 구현해 보라(bootcount 기능을 끄고 `setexpr`로 감소).
   정답/힌트: `setexpr BOOT_B_LEFT ${BOOT_B_LEFT} - 1; saveenv` 후 0이면 순서에서 제외. RAUC의 `contrib/uboot.sh`와 비교.
3. **env 과제**: 예제 3을 확장해 "copy 1을 지우기만 하고(0xFF) 쓰지 않은" 상태(= saveenv의 erase 직후 차단)를 만들고 U-Boot가 무엇을 읽는지 확인하라.
   정답/힌트: `dd if=/dev/zero bs=256k count=1 | tr '\0' '\377'`로 섹터를 0xFF로. CRC가 깨지므로 copy 2를 읽는다.
4. **health 과제**: `l4-health`에 "부팅 후 120초 안에 판정을 못 하면 reboot" 규칙을 넣어라. systemd 쪽에서는 어떤 설정으로 구현할 수 있나?
   정답/힌트: 서비스 `TimeoutStartSec=120` + `FailureAction=reboot`(시험 부팅일 때만 유닛을 활성화하려면 `ConditionPathExists`나 generator), 또는 하드웨어 watchdog을 health 통과 때까지 ping하지 않기(L1).
5. **RAUC 과제**: 번들에 `install-check` hook을 넣어 "현재 버전보다 낮은 번들은 거부"를 구현하고, 1.0.0 번들 설치를 시도해 실패 메시지를 확인하라.
   정답/힌트: hook은 `RAUC_MF_VERSION`(번들 버전) 환경 변수를 받는다. 현재 버전은 `/etc/l4-version`. `sort -V`로 비교 후 실패면 `exit 10`과 메시지.
6. **관찰 과제**: 예제 8 도중 다른 터미널(게스트 두 번째 콘솔이 없으면 백그라운드)로 `ls -l /dev/mapper; losetup -a; findmnt -t squashfs`를 찍어, RAUC가 만든 loop · dm-verity · squashfs 3단을 직접 확인하라.
   정답/힌트: `rauc install &` 후 즉시 명령 실행. §15 그림 9의 가운데 열.
7. **설계 문제**: 배터리 드론에서 bootlimit을 1로 둘 때와 3으로 둘 때의 장단점은? 시험 부팅 1회의 비용(그림 6)을 근거로 답하라.
   정답/힌트: 1은 일시적 실패(첫 부팅의 긴 마이그레이션, 느린 센서 초기화)에도 롤백, 3은 확실히 깨진 이미지에서 전력 · 시간을 더 씀. 실패가 재현성이 높은 panic형인지, 간헐적 health형인지에 따라.

---

## 20. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| OTA | Over-The-Air update | 네트워크로 장비 소프트웨어를 원격 교체 |
| 슬롯 (slot) | 교체 단위 저장 영역 | rootfs A/B, 커널 A/B, 부트로더 boot0/boot1 |
| A/B (symmetric) | 동일 슬롯 두 벌 | 비활성 슬롯에 쓰고 전환, 이전 슬롯이 롤백 대상 |
| recovery (asymmetric) | 작은 복구 시스템 | 재부팅해서 메인 rootfs를 덮어씀 |
| boot-commit | 시험 부팅 후 확정 | "한 번 시도" 표시 → 부팅 → health → commit |
| bootcount | 부팅 시도 카운터 | U-Boot가 증가, userspace가 0으로 |
| bootlimit | 허용 시도 횟수 | `bootcount > bootlimit`이면 altbootcmd |
| altbootcmd | 대체 부팅 명령 | 롤백 스크립트를 넣는 곳 |
| upgrade_available | 시험 부팅 플래그 | `BOOTCOUNT_ENV`가 저장 여부 판단에 씀 |
| redundant env | env 두 벌 | flag(NOR boolean · 블록 카운터)로 최신 사본 선택 |
| `fw_env.config` | Linux 쪽 env 위치 | 장치 · 오프셋 · 크기 · 섹터 크기 |
| libubootenv | env 접근 라이브러리 | `fw_printenv`/`fw_setenv` 제공 |
| health check | 부팅 성공 판정 | 서비스 · 장치 · 앱 진단, 통과 시 commit |
| mark-good / mark-bad | RAUC 슬롯 상태 | 부트로더 backend를 통해 env 등에 기록 |
| RAUC bundle | `.raucb` | squashfs + CMS 서명 (plain/verity/crypt) |
| verity bundle | dm-verity 보호 번들 | 서명은 manifest(root hash), 데이터는 읽을 때 검증 |
| `.swu` | SWUpdate 패키지 | cpio(newc+crc) + sw-description(+sig) + 이미지 |
| sw-description | SWUpdate 설명서 | 이미지 · 장치 · handler · sha256 · bootenv |
| handler | SWUpdate 설치기 | raw · ubivol · archive · rdiff · lua … |
| suricatta | SWUpdate 서버 모드 | hawkBit · HTTP 서버를 폴링 |
| hawkBit | Eclipse 롤아웃 서버 | DDI API, 단계 배포 |
| Mender Artifact | `.mender` | tar + 헤더 + payload + 서명 |
| state script | Mender 상태 훅 | 상태 전후 장비 스크립트 |
| delta | 차분 업데이트 | 옛 이미지 기준 바뀐 부분만 전송 |
| casync / desync | 청크 기반 동기화 | content-defined chunk + 인덱스 |
| zchunk | 청크 압축 형식 | 청크별 해시, HTTP range로 필요한 것만 |
| adaptive update | RAUC 블록 재사용 | 이미 있는 블록은 복사, 없는 것만 읽기 |
| anti-rollback | 다운그레이드 방지 | 단조 카운터 · 최소 보안 버전 |
| canary | 소규모 선행 배포 | 지표를 대조군과 비교 후 확대 |
| PSCI SYSTEM_RESET | ARM 리셋 호출 | 커널 → 펌웨어로 시스템 리셋 요청 |

---

## 21. 요약 & 체크리스트

OTA의 본질은 "덮어쓰기"가 아니라 **되돌릴 수 있는 전환**이다. 활성 슬롯은 건드리지 않고, 비활성 슬롯에 서명 · 해시가 확인된 이미지를 쓰고, env 한 번의 쓰기로 "다음 부팅은 B, 시험"을 예약한다. U-Boot는 시험 부팅마다 bootcount를 먼저 저장하고 시도하며, userspace health check가 제품 수준 기능을 확인해 commit하지 않으면 `bootcount > bootlimit`에서 altbootcmd가 이전 슬롯으로 되돌린다. 이 노트의 QEMU 랩에서 직접 빌드한 U-Boot(bootcount · redundant env)와 GPT A/B 디스크, Ubuntu + systemd 슬롯 이미지, RAUC 1.11 custom backend로 정상 업데이트 commit(27초 기록, 시험 부팅 후 `mark-good`), panic 이미지 롤백(1.3초 panic × 3 → 4번째 altbootcmd), health 실패 롤백, 슬롯 기록 중 kill -9 후 기존 슬롯 부팅을 모두 실제 로그로 확인했다. 그 과정에서 env 이중화의 선택 규칙(사본 하나가 깨지면 조용히 이전 값, 둘 다 깨지면 기본 env), QEMU pflash가 flag 바이트 제자리 프로그램을 못 하는 에뮬레이터 한계, `fw_setenv -s` 형식 함정, RAUC 서비스 누락, 인증서 purpose, 반쯤 쓴 슬롯이 fsck로 clean해 보이는 위험을 실제로 만났다. 도구는 슬롯 모델의 RAUC, handler 프레임워크의 SWUpdate, 서버 일체형 Mender로 정리되고, delta는 실측으로 147 MiB → 8.4 MiB였으며 그 효율은 재현 가능한 이미지 빌드에 달려 있다. 슬롯 밖의 것(데이터 마이그레이션, 부트로더, anti-rollback 카운터)과 플릿 단계 배포가 남은 설계 과제다.

- [ ] OTA 요구사항 일곱 가지를 말하고 각각의 대표 해법을 댈 수 있다
- [ ] single-copy + recovery, A/B, OSTree, 컨테이너, delta의 장단점을 표로 비교할 수 있다
- [ ] boot-commit 프로토콜을 env 쓰기 네 번(B=bad, 시험 예약, bootcount++, commit)으로 설명할 수 있다
- [ ] U-Boot `bootcount_inc()`/`bootcount_error()`와 `bootcount > bootlimit`의 의미를 소스로 설명할 수 있다
- [ ] redundant env의 형식과 flag 규칙, 사본이 깨졌을 때 U-Boot가 읽는 사본을 말할 수 있다
- [ ] `fw_env.config`를 쓰고 `fw_setenv -s`로 여러 변수를 한 번에 바꿀 수 있다
- [ ] RAUC `system.conf`, verity 번들, 서명 검증과 변조 탐지 시점을 설명할 수 있다
- [ ] custom backend와 health check 서비스를 짜서 시험 부팅을 commit할 수 있다
- [ ] 업데이트 도중 각 지점에서 전원이 나가면 무슨 일이 일어나는지 표로 말할 수 있다
- [ ] sw-description과 .swu 구조를 설명하고 openssl로 서명할 수 있다
- [ ] full과 delta의 전송량 · 전제 조건 차이를 실측 숫자로 말할 수 있다
- [ ] 부트로더 업데이트 · 데이터 마이그레이션 · anti-rollback의 순서 문제를 설명할 수 있다

---

## 참고 자료

- U-Boot 문서 — Boot Count Limit: https://docs.u-boot.org/en/latest/api/bootcount.html (소스: `drivers/bootcount/`, `include/bootcount.h`, `common/autoboot.c`, `env/Kconfig` — 랩 `/var/lab/src/u-boot`)
- U-Boot 문서 — Environment Variables: https://docs.u-boot.org/en/latest/usage/environment.html
- libubootenv (`fw_printenv`/`fw_setenv`): https://github.com/sbabic/libubootenv
- RAUC 문서: https://rauc.readthedocs.io/ — 특히 "Integration"(bootloader backends, custom backend), "Reference"(system.conf, manifest), "Advanced Topics"(streaming, adaptive updates, encryption)
- RAUC 소스: https://github.com/rauc/rauc (`contrib/uboot.sh`, `data/de.pengutronix.rauc.conf`, `data/rauc.service.in`)
- SWUpdate 문서: https://sbabic.github.io/swupdate/ — "SWUpdate: syntax and tags"(sw-description), "Update images from verified source"(서명), "Suricatta daemon mode"
- Mender 문서: https://docs.mender.io/ — Artifact, State scripts, U-Boot integration
- Eclipse hawkBit: https://eclipse.dev/hawkbit/
- OSTree: https://ostreedev.github.io/ostree/
- casync: https://github.com/systemd/casync , desync: https://github.com/folbricht/desync , zchunk: https://github.com/zchunk/zchunk
- Linux 커널 소스(elixir): `drivers/mtd/mtdchar.c`, `drivers/mtd/chips/cfi_cmdset_0001.c`, `drivers/md/dm-verity-target.c`, `kernel/reboot.c`, `kernel/panic.c`, `drivers/firmware/psci/psci.c` — https://elixir.bootlin.com/linux/v6.18/source
- Linux 커널 문서 — dm-verity: https://docs.kernel.org/admin-guide/device-mapper/verity.html
- Bootlin, "Embedded Linux system development" 교육 자료(업데이트 · 부트로더 장): https://bootlin.com/training/embedded-linux/
- 관련 노트: C2 §10 · C3 §4 · C4 §3(U-Boot env), J3(dm-verity · RO root), J5(전원 차단), K5(재현 가능 빌드), L1(systemd · watchdog), L5(secure boot · 서명 · anti-rollback), L7(크래시 수집), L8(컨테이너)
