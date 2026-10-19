# L5. Secure boot 체인 — ROM 키 → 서명된 부트로더 → 서명된 FIT → 커널, 키 관리

> **이 노트를 다 읽으면**: secure boot가 막는 공격과 못 막는 공격(물리 접근, JTAG, 다운그레이드, 런타임 침해)을 위협 모델로 말할 수 있다 · RSA 키로 FIT configuration에 서명하고 공개키를 U-Boot control DTB에 넣어, TF-A TBB(BL1→BL2→U-Boot) → U-Boot verified boot(FIT) → 커널까지 이어지는 체인을 QEMU에서 직접 만들고 1바이트 변조·남의 키·서명 없는 FIT·mix-and-match·환경변수 탈취로 깨 볼 수 있다 · 커널 모듈 서명(`sign-file`, `module.sig_enforce`, lockdown)과 measured boot(TPM PCR)를 secure boot와 구분해 설명할 수 있다 · 키 계층·HSM·fuse 공정·키 유출 대응·롤백 방지를 설계 질문 수준으로 답할 수 있다
> **왜 필요한가**: 국방·로보틱스·보안 장비 회사의 Embedded Linux 면접에서 "Walk me through secure boot on your last product", "Why sign the configuration, not the image?", "Your signing key leaked — what now?"는 거의 반드시 나온다. 실무에서는 양산 직전에 "fuse 태우기 전에 체인이 진짜 닫혔는지" 증명하는 사람이 필요하다
> **Don 기준 난이도**: SSD 펌웨어의 서명된 FW 다운로드·부트로더의 이미지 검증·OTP 키 해시는 이미 해 본 일이다 / FIT·U-Boot control DTB에 공개키 넣기, configuration 서명의 의미, 커널 cmdline·환경변수 잠금, 모듈 서명 keyring, TPM PCR, 키 운영(HSM·revocation)이 새로 배울 부분
> **선행 노트**: C3(U-Boot·FIT 기초), C5(TF-A·TBBR·NV counter), J3(dm-verity 입문), K7(모듈 taint) — 같이 보면 좋은 노트: L4(OTA 서명·A/B), L6(dm-verity 심화·TPM/TEE·IMA)

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD에서 했던 일을 떠올리자. 호스트가 새 펌웨어를 내려보내면 현재 펌웨어가 서명을 확인하고서야 NAND에 썼다. 부트로더는 매 부팅마다 메인 펌웨어의 헤더·CRC·서명을 확인하고 점프했다. 그 "부트로더가 다음 단계를 확인하고 넘긴다"를 **칩이 켜지는 순간부터 리눅스 모듈 하나까지** 끊김 없이 이어 붙인 것이 secure boot 체인이다.

Linux SoC에서 단계가 많아질 뿐 원리는 같다. 각 단계는 **다음 단계를 실행하기 전에** 그 단계의 서명을 자기가 믿는 공개키로 검증한다. 맨 앞의 공개키(정확히는 그 해시)는 칩 안의 바꿀 수 없는 곳(OTP fuse)에 있고, 그것을 읽는 코드도 바꿀 수 없는 곳(mask ROM)에 있다. 이것이 **Root of Trust**다.

```svg
<svg viewBox="0 0 680 520" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="320" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="180" y="45" text-anchor="middle" font-size="13">BootROM + OTP fuse (ROTPK 해시)</text>
  <rect x="20" y="90" width="320" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="180" y="115" text-anchor="middle" font-size="13">BL1 · BL2 (TF-A, Mbed TLS)</text>
  <rect x="20" y="160" width="320" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="180" y="185" text-anchor="middle" font-size="13">BL31 EL3 런타임 · BL32 OP-TEE</text>
  <rect x="20" y="230" width="320" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="180" y="255" text-anchor="middle" font-size="13">BL33 U-Boot (FIT 공개키 내장)</text>
  <rect x="20" y="300" width="320" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="180" y="325" text-anchor="middle" font-size="13">FIT: kernel + DTB + initramfs</text>
  <rect x="20" y="370" width="320" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="180" y="395" text-anchor="middle" font-size="13">Linux 커널 → dm-verity rootfs</text>
  <rect x="20" y="440" width="320" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="180" y="465" text-anchor="middle" font-size="13">서명된 모듈 · IMA/EVM (런타임)</text>
  <line x1="180" y1="60" x2="180" y2="88" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="180" y1="130" x2="180" y2="158" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="180" y1="200" x2="180" y2="228" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="180" y1="270" x2="180" y2="298" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="180" y1="340" x2="180" y2="368" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="180" y1="410" x2="180" y2="438" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="360" y="55" width="300" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="75" text-anchor="middle" font-size="12">TB FW cert → BL2 해시 (C5 §7)</text>
  <rect x="360" y="125" width="300" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="145" text-anchor="middle" font-size="12">Trusted Key · SoC FW cert → BL31 해시</text>
  <rect x="360" y="195" width="300" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="215" text-anchor="middle" font-size="12">NT FW content cert → BL33 해시 (§6)</text>
  <rect x="360" y="265" width="300" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="285" text-anchor="middle" font-size="12">FIT configuration RSA 서명 (§2~§5)</text>
  <rect x="360" y="335" width="300" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="355" text-anchor="middle" font-size="12">verity root hash: 서명된 DT 안 (L6)</text>
  <rect x="360" y="405" width="300" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="425" text-anchor="middle" font-size="12">MODULE_SIG · IMA appraise (§7, L6)</text>
  <text x="20" y="505" font-size="12">빨강 = 바꿀 수 없는 하드웨어, 파랑 = 펌웨어, 주황 = 부트 이미지, 초록 = OS</text>
</svg>
```
그림 1 — 이 노트의 chain of trust. 회색 상자는 "위 단계가 아래 단계를 무엇으로 검증하나"다. 이 노트는 QEMU에서 BL1부터 커널·모듈까지를 실제로 연결한다. TF-A 내부(TBBR 인증서 트리, NV counter)는 C5 §7에서 이미 깊게 다뤘으므로 여기서는 U-Boot와 이어지는 고리만 본다.

이 노트의 실습 환경은 Mac(M2) 위 Lima VM `elinux`(aarch64, Ubuntu 26.04, `uname -r` = `7.0.0-38-generic`)이고, 그 안에서 QEMU 10.2.1(TCG, KVM 없음)로 `virt` 보드를 돌린다. 펌웨어는 TF-A v2.15.0, U-Boot 2026.10, 커널은 랩의 `Image-sec`(6.18.55-sec)이다. 작업 폴더는 VM의 `/var/tmp/L5/`. **부팅 시간 숫자는 TCG 에뮬레이션 값이라 실제 하드웨어와 무관하다.**

| 실습 | 무엇을 보이나 | 절 |
|---|---|---|
| 키 생성 · .its · `mkimage -K` | 공개키가 U-Boot DTB의 `/signature`로 들어가는 과정 | §2 |
| control DTB를 U-Boot에 붙이기 | qemu_arm64에서 `OF_BOARD`를 끄고 `EXT_DTB`로 빌드한 이유 | §3 |
| 정상 부팅 · 변조 3종 | `sha256,rsa2048:dev+ OK` / Bad hash / 남의 키 / 서명 없음 | §4 |
| mix-and-match | 이미지 서명은 뚫리고 configuration 서명은 막는다 | §5 |
| env 탈취 · `booti` 우회 · 잠근 U-Boot | 체인이 "검증 경로 밖"에서 끊기는 장면과 막는 설정 | §5 |
| TF-A TBB + 잠근 U-Boot | BL1→BL2→U-Boot→FIT→커널 전체 체인, 키 바꿔치기 거부 | §6 |
| 모듈 서명 | 서명/남의 키/무서명 모듈 × enforce 없음/`sig_enforce`/lockdown | §7 |
| swtpm measured boot | PCR 읽기·extend 계산 검증 | §8 |

---

## 1. 위협 모델 — secure boot는 무엇을 막고, 무엇을 못 막나

### 1.1 직관 — "누가 플래시에 무엇을 쓸 수 있다고 가정하나"

보안 설계는 항상 "공격자가 무엇을 할 수 있다고 가정하나"에서 출발한다. secure boot의 기본 가정은 이것이다: **공격자가 저장장치(eMMC, SPI NOR, SD)의 내용을 마음대로 바꿀 수 있다.** 장비를 주워서 eMMC를 떼어 다시 쓸 수도 있고, 원격 침해로 root를 얻어 `/boot`를 덮어쓸 수도 있다. secure boot의 약속은 "그래도 **우리가 서명한 코드만** 실행된다"이다. 그 이상은 약속하지 않는다.

### 1.2 막는 것 / 못 막는 것

| 공격 | secure boot로 막히나 | 이유 · 추가로 필요한 것 |
|---|---|---|
| 플래시의 커널·부트로더를 다른 것으로 교체 | 막힌다 | 서명 검증 실패 → 부팅 거부 (§4) |
| 서명된 이미지 일부를 변조 (1바이트) | 막힌다 | 해시 불일치 (§4) |
| 서명된 이미지들을 섞어 새 조합 만들기 | configuration 서명이면 막힌다 | 이미지별 서명이면 뚫린다 (§5) |
| 서명이 진짜인 **옛 버전**(취약점 있음)으로 되돌리기 | **못 막는다** | anti-rollback(NV counter, 보안 버전) 별도 필요 (C5 §7.7, §9) |
| U-Boot 콘솔·환경변수로 cmdline 바꾸기 | **설정에 달렸다** | env 고정·콘솔 차단·bootargs를 서명 안에 (§5.3) |
| JTAG로 CPU를 멈추고 메모리 수정 | **못 막는다** | JTAG fuse 잠금 / 인증 디버그 (§9.4) |
| 부팅 후 런타임 침해 (원격 코드 실행) | **못 막는다** | 최소 권한, LSM, dm-verity로 재부팅 시 복구 (L6) |
| rootfs 파일 변조 | 체인을 rootfs까지 잇지 않으면 못 막는다 | dm-verity 또는 IMA appraise (L6) |
| 비밀(펌웨어 IP, 키) 추출 | 못 막는다 — secure boot는 무결성이지 기밀성이 아니다 | 이미지 암호화(NXP DEK, Jetson SBK), dm-crypt, TEE (L6) |
| 전압·클럭 글리치로 검증 분기 건너뛰기 | ROM·하드웨어 대책에 달렸다 | 글리치 대책 ROM, 이중 검사, 부트 상태 확인 |
| 서명 키 유출 | 못 막는다 | 키 관리·폐기(revocation) 설계 (§9) |

> 면접 포인트: "secure boot = 장비가 우리 코드만 **부팅**한다"이다. "장비가 안전하다"가 아니다. 이 구분을 먼저 말하면 면접관이 다음 질문(롤백, 런타임, 디버그 포트)으로 자연스럽게 넘어온다.

### 1.3 Don 경험과 연결

SSD 펌웨어에서 "서명된 FW만 다운로드 허용"을 했을 때도 같은 질문을 받았을 것이다. 옛 펌웨어로 다운그레이드를 허용하나? 제조 테스트 모드(벤더 명령)는 양산 후 닫히나? JTAG는? Linux SoC에서도 정확히 그 세 가지(롤백·제조 모드·디버그 포트)가 체인을 깨는 단골 구멍이다.

---

## 2. U-Boot verified boot — 키, .its, 서명, 공개키 넣기

### 2.1 역할 분담 — 누가 무엇을 들고 있나

- **개인키**: 빌드 서버(실제로는 HSM)에만 있다. `mkimage`가 FIT에 서명할 때만 쓴다.
- **공개키**: U-Boot 바이너리 끝에 붙는 **control DTB**(U-Boot 자신이 하드웨어 설명과 설정에 쓰는 Device Tree)의 `/signature/key-<이름>` 노드에 들어간다. U-Boot 바이너리 자체는 앞 단계(TF-A BL2)가 검증하므로, 공개키도 덩달아 보호된다 — 이게 §6의 핵심이다.
- **FIT**: 커널·DTB·initramfs를 담은 하나의 FDT 형식 파일(C3 §10). 각 이미지에는 `hash-1`(sha256), 각 configuration에는 `signature-1`(RSA 서명)이 붙는다.

### 2.2 코드로 확인 — 키 만들기

**무엇을 확인하나**: mkimage가 기대하는 키 파일 형식(`<keydir>/<key-name-hint>.key`와 `.crt`). 공격자 역할의 키도 **같은 이름 `dev`**로 하나 더 만든다(이름만 같으면 통과하는지 보려고).

```sh
cd /var/tmp/L5 && mkdir -p keys evilkeys
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out keys/dev.key
openssl req -batch -new -x509 -key keys/dev.key -out keys/dev.crt -days 3650 -subj "/CN=L5 dev FIT key"
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out evilkeys/dev.key
openssl req -batch -new -x509 -key evilkeys/dev.key -out evilkeys/dev.crt -days 3650 -subj "/CN=attacker"
ls -l keys evilkeys
```

```text
evilkeys:
total 8
-rw-rw-r-- 1 donh donh 1111 Oct 17 21:15 dev.crt
-rw------- 1 donh donh 1708 Oct 17 21:15 dev.key

keys:
total 8
-rw-rw-r-- 1 donh donh 1127 Oct 17 21:15 dev.crt
-rw------- 1 donh donh 1704 Oct 17 21:15 dev.key
```

**출력에서 볼 것**: 개인키는 `0600`이다. 실습이라 디스크에 있지만, 제품 키는 절대 이렇게 두지 않는다(§9). RSA-2048은 U-Boot에서 가장 흔한 선택이고, 같은 방법으로 `rsa_keygen_bits:4096` + `algo = "sha256,rsa4096"`도 된다(키가 커지면 검증 시간과 control DTB 크기가 는다).

### 2.3 .its — 이미지 4개, configuration 2개

**무엇을 확인하나**: 서명이 configuration 노드 안에 있고, `sign-images`가 서명 범위에 넣을 이미지 종류를 고른다. configuration이 두 개인 이유: 하나는 TF-A 없이(`-bios u-boot.bin`, PSCI `hvc`), 하나는 TF-A 밑에서(`secure=on`, PSCI `smc`) 부팅할 DT를 쓴다. 이 둘은 §5의 mix-and-match 실험 재료도 된다.

```dts
/dts-v1/;
/ {
    description = "L5 signed FIT: kernel + fdt + initramfs";
    #address-cells = <1>;
    images {
        kernel-1 {
            description = "Linux @KVER@";
            data = /incbin/("@KERNEL@");
            type = "kernel"; arch = "arm64"; os = "linux"; compression = "none";
            load = <0x40400000>; entry = <0x40400000>;
            hash-1 { algo = "sha256"; };
        };
        fdt-ns {
            description = "QEMU virt DT (non-secure, PSCI hvc)";
            data = /incbin/("/var/tmp/L5/l5_fit_ns.dtb");
            type = "flat_dt"; arch = "arm64"; compression = "none";
            hash-1 { algo = "sha256"; };
        };
        fdt-s {
            description = "QEMU virt DT (secure=on, PSCI smc via TF-A)";
            data = /incbin/("/var/tmp/L5/l5_fit_s.dtb");
            type = "flat_dt"; arch = "arm64"; compression = "none";
            hash-1 { algo = "sha256"; };
        };
        ramdisk-1 {
            description = "BusyBox initramfs";
            data = /incbin/("@RAMDISK@");
            type = "ramdisk"; arch = "arm64"; os = "linux"; compression = "none";
            hash-1 { algo = "sha256"; };
        };
    };
    configurations {
        default = "conf-ns";
        conf-ns {
            description = "boot without TF-A";
            kernel = "kernel-1"; fdt = "fdt-ns"; ramdisk = "ramdisk-1";
            signature-1 {
                algo = "sha256,rsa2048";
                key-name-hint = "dev";
                sign-images = "fdt", "kernel", "ramdisk";
            };
        };
        conf-s {
            description = "boot under TF-A (BL33)";
            kernel = "kernel-1"; fdt = "fdt-s"; ramdisk = "ramdisk-1";
            signature-1 {
                algo = "sha256,rsa2048";
                key-name-hint = "dev";
                sign-images = "fdt", "kernel", "ramdisk";
            };
        };
    };
};
```

`@KERNEL@` 등은 작은 셸 스크립트가 `sed`로 채운다(`/var/lab/out/Image-sec`, `/var/lab/out/initramfs.cpio.gz`). DT 두 개는 QEMU가 만든 DT를 `-machine virt,dumpdtb=...`로 떠서 썼고, 각 DT의 `/chosen/bootargs`에 커널 cmdline을 `fdtput`으로 넣었다 — **cmdline이 서명 범위 안에 들어가게** 하려는 것이다(§5.3).

```sh
qemu-system-aarch64 -machine virt,dumpdtb=l5_qemu_ns.dtb -cpu cortex-a57 -m 1057 -nographic -bios /var/lab/out/u-boot.bin
qemu-system-aarch64 -machine virt,secure=on,dumpdtb=l5_qemu_s.dtb -cpu cortex-a57 -m 1057 -nographic -bios /var/lab/out/flash.bin
fdtget l5_qemu_ns.dtb /psci method; fdtget l5_qemu_s.dtb /psci method
```

```text
hvc
Error at '/psci': FDT_ERR_NOTFOUND
```

**출력에서 볼 것**: `secure=on`이면 QEMU는 `/psci` 노드를 만들지 않는다 — 실제 보드에서는 TF-A BL2가 실행 중에 DT를 고쳐 넣는다. 서명된 FIT 안의 DT는 부팅 중에 아무도 고치지 않으므로, `fdt-s`에는 `/psci { compatible = "arm,psci-1.0"; method = "smc"; }`를 `fdtput`으로 직접 넣었다. **"부팅 중 누군가 DT를 고쳐 주던 것"을 서명된 DT로 옮기면 그 수정을 빌드 시점에 해야 한다**는 것이 실무 교훈이다.

### 2.4 코드로 확인 — 서명하면서 공개키를 control DTB에 넣기

**무엇을 확인하나**: `mkimage -f l5.its -k keys -K l5_ctrl.dtb -r out.itb` 한 번에 세 가지가 일어난다: 해시 계산, configuration 서명, 그리고 `-K`로 공개키를 control DTB에 쓰기. `-r`은 그 키를 `required`로 표시한다.

```sh
cp build/dts/dt.dtb l5_ctrl.dtb          # U-Boot이 쓸 control DTB 원본 (QEMU DT, §3)
./build/tools/mkimage -f l5.its -k keys -K l5_ctrl.dtb -r l5_test.itb | tail -16
```

```text
 Configuration 0 (conf-ns)
  Description:  boot without TF-A
  Kernel:       kernel-1
  Init Ramdisk: ramdisk-1
  FDT:          fdt-ns
  Sign algo:    sha256,rsa2048:dev
  Sign value:   35037b51a1ddbe22e7b8027ef8c97772699afe991a2fa49e464431f20d6f5f1f3efe99c1...
  Timestamp:    Sat Oct 17 21:18:24 2026
 Configuration 1 (conf-s)
  ...
Signature written to 'l5_test.itb', node '/configurations/conf-s/signature-1'
Public key written to 'l5_ctrl.dtb', node '/signature/key-dev'
```

(서명 값은 길어서 잘랐다. `mkimage`는 U-Boot 빌드 트리의 2026.10 도구를 썼다 — VM에 깔린 `u-boot-tools`는 2025.10이라 버전을 맞췄다.)

### 2.5 코드로 확인 — control DTB의 `/signature` 노드 열어 보기

**무엇을 확인하나**: U-Boot이 런타임에 RSA 검증을 빠르게 하려고 미리 계산해 둔 값들(`rsa,r-squared`, `rsa,n0-inverse` — Montgomery 곱셈용)과 정책(`required`)이 DTB 속성으로 들어 있다.

```sh
fdtget -p l5_ctrl.dtb /signature/key-dev
for p in required algo rsa,num-bits key-name-hint; do echo "$p = $(fdtget l5_ctrl.dtb /signature/key-dev $p)"; done
fdtget -t x l5_ctrl.dtb /signature/key-dev rsa,exponent
fdtget -t bx l5_ctrl.dtb /signature/key-dev rsa,modulus | cut -d" " -f1-16
openssl rsa -in keys/dev.key -noout -modulus | cut -c1-40
```

```text
required
algo
rsa,r-squared
rsa,modulus
rsa,exponent
rsa,n0-inverse
rsa,num-bits
key-name-hint
required = conf
algo = sha256,rsa2048
rsa,num-bits = 2048
key-name-hint = dev
0 10001
c7 50 54 66 5 7a cc 1f 76 6c 2c d8 d0 1a 78 d9
Modulus=C7505466057ACC1F766C2CD8D01A78D9
```

**출력에서 볼 것**: `rsa,modulus` 앞 16바이트가 openssl이 보여 준 개인키의 modulus와 같다(`fdtget`은 앞자리 0을 생략해 `05`를 `5`로 찍는다). 지수는 65537(0x10001). `required = conf`는 "**configuration 서명이 이 키로 검증되지 않으면 부팅하지 마라**"는 정책이다. `-r` 없이 넣으면 키는 있지만 강제되지 않는다 — 서명 없는 FIT도 그냥 부팅된다. 키가 여러 개면 `/signature`의 `required-mode = "any"` 또는 `"all"`로 정책을 정한다(U-Boot `doc/usage/fit/signature.rst`).

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="380" height="410" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c"/>
  <text x="200" y="32" text-anchor="middle" font-size="13">l5_good.itb (FIT = FDT 형식 파일)</text>
  <rect x="25" y="45" width="350" height="150" rx="4" fill="#4a7bd0" fill-opacity="0.08" stroke="#4a7bd0"/>
  <text x="40" y="65" font-size="12">/images</text>
  <text x="55" y="87" font-size="12">kernel-1: data (54 MB), load, entry</text>
  <text x="75" y="107" font-size="12">hash-1: algo sha256, value</text>
  <text x="55" y="129" font-size="12">fdt-ns · fdt-s: data (DT + /chosen/bootargs)</text>
  <text x="75" y="149" font-size="12">hash-1: value</text>
  <text x="55" y="171" font-size="12">ramdisk-1: data, hash-1</text>
  <rect x="25" y="210" width="350" height="200" rx="4" fill="#3f9a6b" fill-opacity="0.08" stroke="#3f9a6b"/>
  <text x="40" y="230" font-size="12">/configurations (default = conf-ns)</text>
  <text x="55" y="252" font-size="12">conf-ns: kernel, fdt, ramdisk = 이름</text>
  <rect x="55" y="262" width="305" height="110" rx="4" fill="#d0564a" fill-opacity="0.10" stroke="#d0564a"/>
  <text x="70" y="282" font-size="12">signature-1</text>
  <text x="85" y="302" font-size="12">algo = sha256,rsa2048</text>
  <text x="85" y="322" font-size="12">key-name-hint = dev</text>
  <text x="85" y="342" font-size="12">hashed-nodes = 서명한 노드 목록</text>
  <text x="85" y="362" font-size="12">value = RSA 서명 (256 B)</text>
  <text x="55" y="395" font-size="12">conf-s: 같은 구조, fdt = fdt-s</text>
  <rect x="420" y="60" width="250" height="190" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="545" y="82" text-anchor="middle" font-size="13">U-Boot control DTB</text>
  <text x="545" y="100" text-anchor="middle" font-size="12">(u-boot.bin 끝에 붙음)</text>
  <text x="435" y="125" font-size="12">/signature/key-dev</text>
  <text x="450" y="147" font-size="12">required = conf</text>
  <text x="450" y="167" font-size="12">algo = sha256,rsa2048</text>
  <text x="450" y="187" font-size="12">rsa,modulus · rsa,exponent</text>
  <text x="450" y="207" font-size="12">rsa,r-squared · n0-inverse</text>
  <text x="450" y="227" font-size="12">key-name-hint = dev</text>
  <rect x="420" y="290" width="250" height="90" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="545" y="312" text-anchor="middle" font-size="12">서명 범위 = hashed-nodes의</text>
  <text x="545" y="332" text-anchor="middle" font-size="12">속성 전부, 단 data 속성은 제외</text>
  <text x="545" y="352" text-anchor="middle" font-size="12">→ data는 hash-1 값으로 묶인다</text>
  <line x1="360" y1="300" x2="418" y2="200" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
  <text x="392" y="275" font-size="12">검증</text>
</svg>
```
그림 2 — FIT와 control DTB의 관계. configuration 서명은 "어떤 이미지 이름들을 고르는지"와 "그 이미지들의 해시 값"을 함께 서명한다. 이미지 데이터 자체(수십 MB)는 서명 계산에서 빠지고, 대신 각 이미지의 `hash-1`이 데이터를 대표한다 — 그래서 검증은 "서명 확인 → 해시 확인" 두 단계다(§4.3에서 실제로 이 순서가 보인다).

### 2.6 서명 범위 — `hashed-nodes`를 직접 보기

**무엇을 확인하나**: configuration 서명이 실제로 어느 노드를 덮는지.

```sh
fdtdump l5_good.itb 2>/dev/null | sed -n "/conf-ns {/,/};/p" | head -16
```

```text
        conf-ns {
            description = "boot without TF-A";
            kernel = "kernel-1";
            fdt = "fdt-ns";
            ramdisk = "ramdisk-1";
            signature-1 {
                hashed-strings = <0x00000000 0x0000008e>;
                hashed-nodes = "/", "/configurations/conf-ns", "/images/fdt-ns", "/images/fdt-ns/hash-1", "/images/kernel-1", "/images/kernel-1/hash-1", "/images/ramdisk-1", "/images/ramdisk-1/hash-1";
                timestamp = <0x6ad44d38>;
                signer-version = "2026.10";
                signer-name = "mkimage";
                value = <0x18258d52 0xb0fa4fa9 0x4d658810 ... 0x63ff1df7>;
                algo = "sha256,rsa2048";
                key-name-hint = "dev";
                sign-images = "fdt", "kernel", "ramdisk";
            };
```

(`value` 배열은 64워드라 가운데를 `...`로 줄였다.)

**출력에서 볼 것**: `hashed-nodes`에 `/configurations/conf-ns`와 그 configuration이 고른 이미지 노드 + 각 `hash-1` 노드가 들어 있다. `fdt-s`는 없다 — conf-ns가 고르지 않았으니까. 서명은 이 노드들의 FDT 구조 영역(속성 이름·값)과 문자열 테이블 일부(`hashed-strings`)를 이어 붙인 바이트에 대한 RSA 서명이다. U-Boot 소스 `boot/image-fit-sig.c`의 `fit_config_check_sig()`는 이때 `data`, `data-size`, `data-position`, `data-offset` 속성을 제외 목록(`exc_prop[]`)으로 빼고 해시한다.

---

## 3. 공개키를 U-Boot에 "진짜로" 넣기 — qemu_arm64의 함정

### 3.1 문제 — qemu_arm64는 DTB를 앞 단계에서 받는다

`qemu_arm64_defconfig`는 `CONFIG_OF_BOARD=y`다. U-Boot이 자기 DTB를 바이너리에 갖고 다니지 않고, **QEMU가 RAM 시작(0x40000000)에 놓아 준 DTB**를 control DTB로 쓴다(`board/emulation/qemu-arm/qemu-arm.c`의 `board_fdt_blob_setup()`). 실제 보드에서 TF-A가 DTB를 넘겨주는 구조(`OF_HAS_PRIOR_STAGE`)와 같다.

U-Boot 문서(`doc/develop/devicetree/dt_qemu.rst`)는 `-dtb merged.dtb`로 QEMU에 공개키가 든 DTB를 넘기는 방법을 소개한다. 개발 중 시험에는 편하다. 그러나 **보안상으로는 의미가 없다**: 공개키가 U-Boot 바이너리 밖에서 오므로, 저장장치(또는 앞 단계 입력)를 바꿀 수 있는 공격자는 자기 공개키가 든 DTB를 넘겨 자기가 서명한 FIT를 부팅시킬 수 있다. 공개키는 **검증된 바이너리 안**에 있어야 한다.

### 3.2 해결 — `OF_SEPARATE` + `EXT_DTB`

그래서 이 노트는 U-Boot을 다시 빌드해 control DTB를 `u-boot.bin` 끝에 붙였다(`u-boot.bin = u-boot-nodtb.bin + dtb`). 이러면 TF-A BL2가 `u-boot.bin` 전체의 해시를 검증할 때 공개키도 함께 검증된다(§6). 처음 시도에서 두 번 막혔다 — 그 기록이 실무 공부가 된다.

```text
# dev.frag (merge_config.sh로 qemu_arm64_defconfig 위에 덮어쓴 조각)
# CONFIG_OF_BOARD is not set
CONFIG_OF_SEPARATE=y
# CONFIG_OF_OMIT_DTB is not set
```

```sh
cd /var/lab/src/u-boot
make O=/var/tmp/L5/build qemu_arm64_defconfig
./scripts/kconfig/merge_config.sh -m -O /var/tmp/L5/build /var/tmp/L5/build/.config dev.frag
make O=/var/tmp/L5/build olddefconfig
make O=/var/tmp/L5/build -j2 EXT_DTB=/var/tmp/L5/l5_ctrl.dtb u-boot.bin
ls -l /var/tmp/L5/build/u-boot*.bin
```

```text
  RELOC   u-boot-nodtb.bin
  CAT     u-boot-dtb.bin
  COPY    u-boot.bin
-rw-rw-r-- 1 donh donh 1511343 Oct 17 21:17 /var/tmp/L5/build/u-boot-dtb.bin
-rw-rw-r-- 1 donh donh 1504160 Oct 17 21:17 /var/tmp/L5/build/u-boot-nodtb.bin
-rw-rw-r-- 1 donh donh 1511343 Oct 17 21:17 /var/tmp/L5/build/u-boot.bin
```

**출력에서 볼 것**: `u-boot.bin`이 `nodtb`보다 7,183바이트 크다 = 붙인 DTB 크기(공개키를 넣기 전 QEMU DT; 키를 넣은 최종본은 9,743바이트). 막혔던 두 지점:

- `OF_BOARD`만 끄고 `make all`을 하면 `scripts/check-of.sh`가 "This board uses a prior stage to provide the device tree. Please enable CONFIG_OF_BOARD"로 빌드를 실패시킨다. `ARCH_QEMU`가 `imply OF_HAS_PRIOR_STAGE`를 하고, 이 심볼은 프롬프트가 없어 끌 수 없다. 이 검사는 `all` 타깃에만 붙어 있으므로 **`u-boot.bin` 타깃을 직접** 만들면 된다(의도적으로 정책을 벗어난다는 것을 알고 하는 것이다).
- `OF_HAS_PRIOR_STAGE`가 켜져 있으면 `OF_OMIT_DTB`가 기본 y라서 DTB가 붙지 않는다(첫 빌드에서 `u-boot.bin` = `nodtb`와 같은 1,504,160바이트였다). `# CONFIG_OF_OMIT_DTB is not set`으로 끈다.
- `EXT_DTB=`는 `dts/Makefile`에서 기본 DTB 대신 지정 파일을 `dts/dt.dtb`로 쓰게 한다. U-Boot 문서의 BeagleBone 예제(`doc/usage/fit/beaglebone_vboot.rst`)와 같은 방법이다.

DT를 QEMU가 만든 것으로 고정했으므로 **QEMU 인자(-m, -cpu, secure=on)를 바꾸면 control DTB를 다시 떠야 한다**. 부팅 배너에 `devicetree: separate`와 함께 "Unexpected devicetree source (not from a prior stage)" 경고가 뜨는데, 이 보드가 원래 앞 단계 DTB를 기대하기 때문이다. 실제 i.MX·TI 보드는 처음부터 `OF_SEPARATE`라 이런 경고가 없다.

> 실제 보드에서는: NXP·TI·Rockchip 보드의 U-Boot은 원래 control DTB를 바이너리에 붙인다(`u-boot-dtb.bin`, `u-boot.itb`). 그래서 `mkimage -K u-boot.dtb`로 키를 넣고 U-Boot을 다시 패키징하면 끝이다. SPL이 U-Boot proper를 FIT로 싣는 구조라면 SPL의 DTB에도 키를 넣어 SPL이 U-Boot FIT를 검증하게 한다(`CONFIG_SPL_FIT_SIGNATURE`).

---

## 4. 부팅하고 깨 보기 — 정상, 1바이트 변조, 남의 키, 서명 없음

### 4.1 실험 장치

FIT는 QEMU의 `-device loader`로 RAM 0x48000000에 바로 올린다(저장장치 드라이버를 빼고 검증만 보려는 것). U-Boot 프롬프트에서 `bootm 0x48000000`을 친다. 대화는 `expect`로 자동화했다.

```sh
qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 1057 -nographic -bios l5_ub_dev.bin \
    -device loader,file=l5_good.itb,addr=0x48000000,force-raw=on
# U-Boot 프롬프트에서:  bootm 0x48000000
```

### 4.2 코드로 확인 — 정상 부팅

**무엇을 확인하나**: 서명된 FIT가 `required` 키로 검증되는 로그.

```text
=> bootm 0x48000000
## Loading kernel (any) from FIT Image at 48000000 ...
   Using 'conf-ns' configuration
   Verifying Hash Integrity ... sha256,rsa2048:dev+ OK
   Trying 'kernel-1' kernel subimage
     Description:  Linux Image-sec
   Verifying Hash Integrity ... sha256+ OK
## Loading ramdisk (any) from FIT Image at 48000000 ...
   Using 'conf-ns' configuration
   Verifying Hash Integrity ... sha256,rsa2048:dev+ OK
   Trying 'ramdisk-1' ramdisk subimage
   Verifying Hash Integrity ... sha256+ OK
## Loading fdt (any) from FIT Image at 48000000 ...
   Using 'conf-ns' configuration
   Verifying Hash Integrity ... sha256,rsa2048:dev+ OK
   Trying 'fdt-ns' fdt subimage
   Verifying Hash Integrity ... sha256+ OK
   Booting using the fdt blob at 0x8058c010
Working FDT set to 8058c010
   Loading Kernel Image to 40400000
   Loading Ramdisk to 7f404000, end 7f52b7e2 ... OK
   Loading Device Tree to 000000007f3ff000, end 000000007f403c4f ... OK
Starting kernel ...
[    0.000000] Linux version 6.18.55-sec (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #1 SMP PREEMPT Sat Oct 17 21:24:20 PDT 2026
[    0.000000] Kernel command line: console=ttyAMA0 LAB_CMD=cat~/proc/cmdline
console=ttyAMA0 LAB_CMD=cat~/proc/cmdline
[    8.374799] reboot: Power down
```

(Image-sec로 만든 FIT의 실행 로그다. 이미지마다 찍히는 세부 항목(Created, Data Size, Hash value 등)은 지웠다. 주소는 FIT 크기에 따라 달라진다.)

**출력에서 볼 것**:

- `sha256,rsa2048:dev+ OK` = "configuration 서명을 `key-dev`로 검증 성공". `+`가 성공, `-`가 실패다. 이 줄이 **이미지마다 반복**된다 — `bootm`이 kernel·ramdisk·fdt를 각각 로드할 때마다 configuration 서명을 다시 확인한다.
- 그다음 `sha256+ OK`는 이미지 데이터의 해시 확인이다. 그림 2의 "서명 → 해시" 2단계가 그대로 보인다.
- 커널 cmdline은 U-Boot 환경변수가 아니라 **서명된 `fdt-ns`의 `/chosen/bootargs`**에서 왔다(이 U-Boot에는 `bootargs` 환경변수가 없다: 첫 시험 부팅에서 `printenv bootargs` → `## Error: "bootargs" not defined`).

### 4.3 코드로 확인 — 변조 3종

**무엇을 확인하나**: (1) 커널 데이터 1바이트 반전, (2) 같은 내용을 같은 이름(`dev`)의 **다른 키**로 서명, (3) 서명 노드를 아예 지운 FIT. 먼저 호스트에서 `fit_check_sign`(U-Boot의 검증 코드를 호스트용으로 빌드한 도구)으로 확인하고, 그다음 실제 U-Boot에서 부팅한다.

```sh
OFF=$(./build/tools/fit_info -f l5_good.itb -n /images/kernel-1 -p data | sed -n 's/^OFF: //p')
cp l5_good.itb l5_t1_flip.itb
python3 -I -c 'import sys; p=int(sys.argv[2]); f=open(sys.argv[1],"r+b"); f.seek(p); b=f.read(1); f.seek(p); f.write(bytes([b[0]^0xff])); print("byte@%#x: %#04x -> %#04x" % (p, b[0], b[0]^0xff))' l5_t1_flip.itb $((OFF + 0x100000))
mkfit.sh /var/lab/out/Image-sec /var/lab/out/initramfs.cpio.gz l5_t2_evil.itb "$ARGS" -k evilkeys   # 남의 키
sed '/signature-1 {/,/};/d' l5.its > l5_unsigned.its && ./build/tools/mkimage -q -f l5_unsigned.its l5_t3_unsigned.itb
for f in l5_good l5_t1_flip l5_t2_evil l5_t3_unsigned; do echo "#### $f"
  ./build/tools/fit_check_sign -f $f.itb -k l5_ctrl.dtb -c conf-ns 2>&1 | grep -E "Verif|error|fail|Fail|OK$|Sign|Bad" | head -6; done
```

```text
byte@0x1000d8: 0xf3 -> 0x0c
#### l5_good
Signature check OK
Verifying Hash Integrity for node 'conf-ns'... sha256,rsa2048:dev+ 
Verified OK, loading images
#### l5_t1_flip
Signature check bad (error 1)
Verifying Hash Integrity for node 'conf-ns'... sha256,rsa2048:dev+ 
Verified OK, loading images
...
sha256 error!
Bad hash value for 'hash-1' hash node in 'kernel-1' image node
Bad Data Hash
#### l5_t2_evil
Signature check bad (error 1)
Verifying Hash Integrity for node 'conf-ns'... sha256,rsa2048:dev- 
 error!
Verification failed for 'conf-ns' config node
Failed to verify required signature 'key-dev'
#### l5_t3_unsigned
Signature check bad (error 1)
Verifying Hash Integrity for node 'conf-ns'...  error!
Failed to verify required signature 'key-dev'
```

(`l5_t1_flip`의 아래 세 줄은 같은 명령을 `grep -i "bad\|error"`로 다시 뽑은 것이다. `mkfit.sh`는 §2.3의 `sed` + `mkimage` 래퍼다.)

U-Boot에서 실제로 `bootm` 했을 때:

```text
######## t1_flip
=> bootm 0x48000000
   Using 'conf-ns' configuration
   Verifying Hash Integrity ... sha256,rsa2048:dev+ OK
   Trying 'kernel-1' kernel subimage
   Verifying Hash Integrity ... sha256 error!
Bad hash value for 'hash-1' hash node in 'kernel-1' image node
Bad Data Hash
ERROR -2: can't get kernel image!
######## t2_evil
=> bootm 0x48000000
   Using 'conf-ns' configuration
   Verifying Hash Integrity ... sha256,rsa2048:dev-  error!
Verification failed for 'conf-ns' config node
Failed to verify required signature 'key-dev'
Bad Data Hash
ERROR -2: can't get kernel image!
######## t3_unsigned
=> bootm 0x48000000
   Using 'conf-ns' configuration
   Verifying Hash Integrity ...  error!
No 'signature' subnode found for 'conf-ns' config node
Failed to verify required signature 'key-dev'
Bad Data Hash
ERROR -2: can't get kernel image!
```

**출력에서 볼 것**:

- **1바이트 변조**: 서명 검증은 `dev+ OK`로 **통과**하고, 그다음 데이터 해시에서 걸린다. 서명이 덮는 것은 hash 값이지 데이터가 아니기 때문이다(그림 2). 공격자가 hash 값까지 고치면? 그러면 configuration 서명이 깨진다 — 두 단계가 서로를 묶는다.
- **남의 키**: 키 이름(`key-name-hint = dev`)이 같아도 `dev-`로 실패한다. 이름은 "어느 키로 검증해 볼지"의 힌트일 뿐, 검증은 control DTB 안의 modulus로 한다.
- **서명 없음**: `required = conf` 덕분에 "서명이 없다"가 곧 "부팅 거부"다. `required`를 빼고 키만 넣었다면 이 FIT는 해시 확인만 하고 부팅된다 — 양산 빌드에서 가장 흔한 실수 중 하나다.
- 실패 후 U-Boot은 `=>` 프롬프트로 돌아간다. 이것이 다음 절의 문제다.

---

## 5. 체인이 "옆에서" 끊기는 곳 — mix-and-match, 환경변수, 서명 없는 부트 명령

### 5.1 mix-and-match 공격 — 왜 configuration에 서명하나

이미지마다 따로 서명하는 방식(`required = "image"`, 이미지 노드 안에 `signature-1`)도 U-Boot이 지원한다. 문제는 **각 이미지는 진짜인데 조합이 가짜**인 경우를 못 막는다는 것이다. 예를 들어:

- 하나의 FIT에 여러 보드용 DT를 넣는 경우가 흔하다. 그중에 실험실 보드용 디버그 DT(`bootargs`에 디버그 셸, `init=/bin/sh` 같은 것)가 섞여 있으면, 공격자는 configuration의 `fdt = "fdt-prod"`를 `"fdt-dbg"`로 문자열 하나만 바꾼다.
- 옛 릴리스 FIT의 커널(서명된 진짜, 하지만 취약점 있음)을 새 릴리스의 DT·initramfs와 섞는다.

이미지별 서명이면 셋 다 "서명이 맞다". configuration 서명이면 configuration 노드 자체(어떤 이름을 고르는지)가 서명 안에 있으므로 조합을 바꾸는 순간 깨진다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <rect x="10" y="10" width="320" height="340" rx="6" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a"/>
  <text x="170" y="34" text-anchor="middle" font-size="13">이미지별 서명 (required = image)</text>
  <rect x="25" y="50" width="135" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="92" y="70" text-anchor="middle" font-size="12">kernel-1 + 서명</text>
  <rect x="175" y="50" width="140" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="245" y="70" text-anchor="middle" font-size="12">ramdisk-1 + 서명</text>
  <rect x="25" y="90" width="135" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="92" y="110" text-anchor="middle" font-size="12">fdt-prod + 서명</text>
  <rect x="175" y="90" width="140" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="245" y="110" text-anchor="middle" font-size="12">fdt-dbg + 서명</text>
  <rect x="25" y="140" width="290" height="60" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="162" text-anchor="middle" font-size="12">conf-prod (서명 없음)</text>
  <text x="170" y="184" text-anchor="middle" font-size="12">fdt = "fdt-prod" → "fdt-dbg" 로 수정</text>
  <rect x="25" y="220" width="290" height="110" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="245" text-anchor="middle" font-size="12">U-Boot: 이미지 3개 각각 서명 OK</text>
  <text x="170" y="268" text-anchor="middle" font-size="12">→ 아무도 의도하지 않은 조합이</text>
  <text x="170" y="291" text-anchor="middle" font-size="12">부팅됨: "MIXED: lab debug fdt"</text>
  <text x="170" y="314" text-anchor="middle" font-size="12">(§5.2 실측)</text>
  <rect x="350" y="10" width="320" height="340" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b"/>
  <text x="510" y="34" text-anchor="middle" font-size="13">configuration 서명 (required = conf)</text>
  <rect x="365" y="50" width="290" height="70" rx="4" fill="#4a7bd0" fill-opacity="0.08" stroke="#4a7bd0"/>
  <text x="510" y="75" text-anchor="middle" font-size="12">이미지들: hash-1만 (서명 없음)</text>
  <text x="510" y="98" text-anchor="middle" font-size="12">kernel · ramdisk · fdt-prod · fdt-dbg</text>
  <rect x="365" y="140" width="290" height="60" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="162" text-anchor="middle" font-size="12">conf-prod + 서명: 고른 이름과</text>
  <text x="510" y="184" text-anchor="middle" font-size="12">그 이미지들의 hash 값을 함께 서명</text>
  <rect x="365" y="220" width="290" height="110" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="245" text-anchor="middle" font-size="12">fdt 이름 하나만 바꿔도</text>
  <text x="510" y="268" text-anchor="middle" font-size="12">conf 서명 영역이 달라짐</text>
  <text x="510" y="291" text-anchor="middle" font-size="12">→ "Failed to verify required</text>
  <text x="510" y="314" text-anchor="middle" font-size="12">signature 'key-dev'" 부팅 거부</text>
</svg>
```
그림 3 — mix-and-match. 왼쪽은 "부품 하나하나에 정품 스티커"이고 오른쪽은 "완성품 조립도에 서명"이다. 조립도에 서명해야 부품 바꿔치기가 막힌다.

### 5.2 코드로 확인 — 같은 공격, 두 방식

**무엇을 확인하나**: 같은 FIT 구조(kernel, ramdisk, fdt-prod, fdt-dbg; configuration은 conf-prod 하나)를 (a) 이미지별 서명, (b) configuration 서명으로 만든다. 공격자는 `fdtput`으로 `conf-prod`의 `fdt` 값만 바꾼다. (a)용 U-Boot은 `required = image`인 control DTB로 따로 빌드했다.

```sh
./build/tools/mkimage -q -f l5_mix_img.its  -k keys -K l5_ctrl_img.dtb -r l5_mix_img.itb    # 이미지별 서명
./build/tools/mkimage -q -f l5_mix_conf.its -k keys l5_mix_conf.itb                        # conf 서명
fdtget l5_ctrl_img.dtb /signature/key-dev required; fdtget l5_ctrl.dtb /signature/key-dev required
for m in img conf; do cp l5_mix_$m.itb l5_mix_${m}_attack.itb
  fdtput -t s l5_mix_${m}_attack.itb /configurations/conf-prod fdt fdt-dbg
  echo "== $m: conf-prod fdt is now: $(fdtget l5_mix_${m}_attack.itb /configurations/conf-prod fdt)"; done
```

```text
image
conf
== img: conf-prod fdt is now: fdt-dbg
== conf: conf-prod fdt is now: fdt-dbg
```

(a) 이미지별 서명 U-Boot(`l5_ub_img.bin`)에서 공격 FIT 부팅:

```text
=> bootm 0x48000000
   Using 'conf-prod' configuration
   Verifying Hash Integrity ... OK
   Trying 'kernel-1' kernel subimage
   Verifying Hash Integrity ... sha256,rsa2048:dev+ sha256+ OK
   Using 'conf-prod' configuration
   Verifying Hash Integrity ... OK
   Trying 'ramdisk-1' ramdisk subimage
   Verifying Hash Integrity ... sha256,rsa2048:dev+ sha256+ OK
   Using 'conf-prod' configuration
   Verifying Hash Integrity ... OK
   Trying 'fdt-dbg' fdt subimage
     Description:  lab-only DT: debug bootargs
   Verifying Hash Integrity ... sha256,rsa2048:dev+ sha256+ OK
Starting kernel ...
[    0.000000] Kernel command line: console=ttyAMA0 LAB_CMD=echo~MIXED:~lab~debug~fdt~booted
MIXED: lab debug fdt booted
```

(b) configuration 서명 U-Boot(`l5_ub_dev.bin`)에서 같은 공격:

```text
=> bootm 0x48000000
   Using 'conf-prod' configuration
   Verifying Hash Integrity ... sha256,rsa2048:dev-  error!
Verification failed for 'conf-prod' config node
Failed to verify required signature 'key-dev'
Bad Data Hash
ERROR -2: can't get kernel image!
```

**출력에서 볼 것**: (a)에서 configuration 줄은 `Verifying Hash Integrity ... OK`뿐이다 — 서명할 게 없으니 "검사할 것 없음 = OK"다. 이미지마다 `sha256,rsa2048:dev+`로 서명이 맞으니 **디버그 DT가 그대로 부팅됐다**. (b)는 configuration 단계에서 바로 거부한다. U-Boot 문서(`doc/usage/fit/signature.rst`)가 configuration 서명을 권장하는 이유가 이것이고, 2021년 U-Boot에서 FIT 서명 우회 CVE들(CVE-2021-27097, CVE-2021-27138 — 노드 이름의 unit address와 hashed-nodes 처리를 악용)이 고쳐진 뒤로 `CONFIG_FIT_FULL_CHECK`(이 빌드에서 `=y`)가 FIT 구조를 더 엄격히 검사한다(CVE 세부는 기억 기반 — 원문 확인 권장).

### 5.3 코드로 확인 — 환경변수로 cmdline 바꾸기

**무엇을 확인하나**: 서명된 DT의 `/chosen/bootargs`는 U-Boot 환경변수 `bootargs`가 있으면 덮어써진다(`boot/fdt_support.c`의 `fdt_chosen()` → `board_fdt_chosen_bootargs()` → `env_get("bootargs")`). 개발용 U-Boot은 플래시에 쓸 수 있는 환경을 갖고 있다.

```text
=> setenv bootargs console=ttyAMA0 LAB_CMD=echo~HIJACKED~cmdline~from~env
=> bootm 0x48000000
   Using 'conf-ns' configuration
   Verifying Hash Integrity ... sha256,rsa2048:dev+ OK
   ...
   Verifying Hash Integrity ... sha256+ OK
Starting kernel ...
[    0.000000] Kernel command line: console=ttyAMA0 LAB_CMD=echo~HIJACKED~cmdline~from~env
HIJACKED cmdline from env
```

**출력에서 볼 것**: **모든 서명 검증이 통과한 상태에서** cmdline이 바뀌었다. 실제 공격이라면 `init=/bin/sh`, dm-verity root hash 바꾸기, `module.sig_enforce=0`(기본값이면 효과 없음), `lockdown=none` 등을 넣는다. 환경은 플래시의 평문 블록(C3)이라 장비를 떼어서도 바꿀 수 있다. **cmdline은 이미지의 일부**로 다뤄야 한다.

### 5.4 코드로 확인 — 서명 없는 부트 명령 `booti`

**무엇을 확인하나**: `bootm`만 서명을 확인한다. `booti`(raw arm64 `Image`), `bootz`, legacy uImage, `bootefi`, `go`는 검증 없이 점프한다. FIT 안의 커널 데이터를 꺼내 `booti`로 바로 부팅해 본다.

```text
=> setenv bootargs console=ttyAMA0 LAB_CMD=echo~UNVERIFIED~kernel~via~booti
=> cp.b 0x480000d8 0x40400000 0x33a4000
=> booti 0x40400000 0x4b3a8a24:0x1277e2 ${fdtcontroladdr}
## Flattened Device Tree blob at 8054c760
   Booting using the fdt blob at 0x8054c760
   Loading Ramdisk to 7f404000, end 7f52b7e2 ... OK
   Loading Device Tree to 000000007f3fe000, end 000000007f40360e ... OK
Starting kernel ...
[    0.000000] Kernel command line: console=ttyAMA0 LAB_CMD=echo~UNVERIFIED~kernel~via~booti
UNVERIFIED kernel via booti
```

(커널 데이터 오프셋 216=0xd8, 길이 54,151,680=0x33a4000, ramdisk 오프셋 54,168,100은 `fit_info -p data`로 얻었다. `0x480000d8`에서 바로 `booti`하면 정렬을 맞추려 옮긴 커널이 ramdisk 자리를 덮을 수 있어 먼저 `cp.b`로 0x40400000에 복사했다.)

**출력에서 볼 것**: 검증 줄이 하나도 없다. 프롬프트를 얻은 사람은 TFTP·USB·`loady`로 아무 커널이나 올려서 이렇게 부팅할 수 있다. 즉 **U-Boot 프롬프트 = 체인의 끝**이다.

### 5.5 코드로 확인 — 잠근(production) U-Boot

**무엇을 확인하나**: 위 세 구멍(프롬프트, 쓰기 가능한 환경, 검증 없는 부트 명령)을 닫은 설정. 부팅 실패 시에도 프롬프트로 떨어지지 않게 `bootcmd` 끝에 `poweroff`를 둔다(실제 제품은 `reset`이나 복구 슬롯).

```text
# prod.frag
# CONFIG_OF_BOARD is not set
CONFIG_OF_SEPARATE=y
# CONFIG_OF_OMIT_DTB is not set
CONFIG_BOOTDELAY=-2
CONFIG_ENV_IS_NOWHERE=y
# CONFIG_ENV_IS_IN_FLASH is not set
# CONFIG_LEGACY_IMAGE_FORMAT is not set
# CONFIG_CMD_BOOTI is not set
# CONFIG_CMD_BOOTZ is not set
# CONFIG_CMD_BOOTEFI is not set
CONFIG_USE_BOOTCOMMAND=y
CONFIG_BOOTCOMMAND="bootm 0x48000000; poweroff"
```

`expect` 스크립트는 U-Boot 배너가 나오자마자 스페이스·Ctrl-C·Enter를 계속 보내 프롬프트를 얻으려 한다. 정상 FIT와 1바이트 변조 FIT:

```text
#### l5_prod_good
U-Boot 2026.10 (Oct 17 2026 - 21:20:38 -0700)
Loading Environment from nowhere...Loading Environment from nowhere... OK
## Loading kernel (any) from FIT Image at 48000000 ...
   Using 'conf-ns' configuration
   Verifying Hash Integrity ... sha256,rsa2048:dev+ OK
   Verifying Hash Integrity ... sha256+ OK
   ...
   Loading Kernel Image to 40400000
[    0.000000] Kernel command line: console=ttyAMA0 LAB_CMD=cat~/proc/cmdline
#### l5_prod_t1_flip
U-Boot 2026.10 (Oct 17 2026 - 21:20:38 -0700)
Loading Environment from nowhere...Loading Environment from nowhere... OK
## Loading kernel (any) from FIT Image at 48000000 ...
   Using 'conf-ns' configuration
   Verifying Hash Integrity ... sha256,rsa2048:dev+ OK
   Verifying Hash Integrity ... sha256 error!
Bad hash value for 'hash-1' hash node in 'kernel-1' image node
Bad Data Hash
ERROR -2: can't get kernel image!
poweroff ...
```

**출력에서 볼 것**:

- `Hit any key to stop autoboot` 줄이 없다(`BOOTDELAY=-2` = 기다리지도, 키를 확인하지도 않음). 키를 두드려도 `=>`가 한 번도 나오지 않았다(스크립트가 프롬프트를 봤다면 `*** GOT A PROMPT ***`를 찍게 했다).
- `Loading Environment from nowhere` = 컴파일된 기본 환경만 쓴다. 기본 환경에 `bootargs`가 없으므로 cmdline은 서명된 DT에서만 온다.
- 실패하면 프롬프트 대신 `poweroff`. 크기도 줄었다: dev 1,513,903 → prod 1,231,999바이트(EFI 로더 등 제거). 공격 표면이 곧 코드 크기다.

남는 숙제(실행 안 함, 가이드): 실제 제품은 여기에 더해 `CONFIG_SILENT_CONSOLE`이나 콘솔 입력 비활성, `CONFIG_AUTOBOOT_KEYED` + 암호(서비스용 백도어가 필요하면 최소한 서명된 토큰), 네트워크·USB 명령 제거(`CMD_NET`, `CMD_USB`, `CMD_DFU`), `CONFIG_CMD_FDT` 제거, 그리고 DT·스크립트를 고르는 로직을 전부 컴파일 시점으로 옮긴다. 환경이 꼭 필요하면(A/B 슬롯 번호, bootcount — L4) `CONFIG_ENV_WRITEABLE_LIST` + `CONFIG_ENV_FLAGS_LIST_STATIC`로 **쓸 수 있는 변수만 화이트리스트**한다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="15" width="380" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="210" y="38" text-anchor="middle" font-size="13">bootm addr#conf (bootcmd 고정)</text>
  <rect x="20" y="80" width="380" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="210" y="101" text-anchor="middle" font-size="12">1. required 키로 configuration 서명 검증</text>
  <text x="210" y="120" text-anchor="middle" font-size="12">(hashed-nodes 영역, data 속성 제외)</text>
  <rect x="20" y="160" width="380" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="210" y="181" text-anchor="middle" font-size="12">2. 이미지 data의 sha256 = hash-1 값?</text>
  <text x="210" y="200" text-anchor="middle" font-size="12">(kernel · ramdisk · fdt 각각, 1이 매번 반복)</text>
  <rect x="20" y="240" width="380" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="261" text-anchor="middle" font-size="12">3. 로드 → fdt_chosen(): /chosen/bootargs</text>
  <text x="210" y="280" text-anchor="middle" font-size="12">env bootargs가 있으면 여기서 덮어씀 (§5.3)</text>
  <rect x="20" y="320" width="380" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="210" y="343" text-anchor="middle" font-size="13">4. Starting kernel ...</text>
  <rect x="440" y="80" width="225" height="50" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a" stroke-dasharray="5 3"/>
  <text x="552" y="101" text-anchor="middle" font-size="12">Failed to verify required</text>
  <text x="552" y="120" text-anchor="middle" font-size="12">signature 'key-dev'</text>
  <rect x="440" y="160" width="225" height="50" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a" stroke-dasharray="5 3"/>
  <text x="552" y="181" text-anchor="middle" font-size="12">Bad hash value for 'hash-1'</text>
  <text x="552" y="200" text-anchor="middle" font-size="12">→ Bad Data Hash</text>
  <rect x="440" y="240" width="225" height="70" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="552" y="262" text-anchor="middle" font-size="12">실패 시 다음 명령:</text>
  <text x="552" y="282" text-anchor="middle" font-size="12">dev: =&gt; 프롬프트 (구멍)</text>
  <text x="552" y="302" text-anchor="middle" font-size="12">prod: poweroff / reset</text>
  <rect x="20" y="375" width="645" height="36" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a"/>
  <text x="342" y="398" text-anchor="middle" font-size="12">옆길: booti · bootz · bootefi · go · legacy uImage = 검증 없음 → prod에서 명령 자체를 뺀다</text>
  <line x1="210" y1="51" x2="210" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="130" x2="210" y2="158" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="210" x2="210" y2="238" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="290" x2="210" y2="318" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="400" y1="105" x2="438" y2="105" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="400" y1="185" x2="438" y2="185" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="552" y1="210" x2="552" y2="238" stroke="currentColor" marker-end="url(#ah3)"/>
</svg>
```
그림 4 — `bootm`의 검증 흐름과 실패 경로. 검증 자체보다 "실패한 다음 무엇이 실행되나"와 "검증을 안 거치는 옆길"이 실제 구멍이 된다.

---

## 6. 전체 체인 — TF-A TBB가 "공개키가 든 U-Boot"을 검증한다

### 6.1 무엇을 연결하나

C5 §7에서 TF-A Trusted Board Boot(TBB)를 만들고 깨 봤다. 그때 BL33은 랩의 일반 U-Boot이었다. 이번에는 BL33 자리에 **§5.5의 잠근 U-Boot**(control DTB에 FIT 공개키, `required = conf`, TF-A용 DT로 다시 뜬 것)을 넣는다. 그러면:

1. BL1이 ROTPK 해시(qemu에서는 BL1에 박힌 값, 실제 칩에서는 fuse)로 BL2 인증서를 검증한다.
2. BL2가 NT FW content 인증서 안의 **BL33 해시**로 U-Boot 바이너리 전체 = 코드 + 공개키를 검증한다.
3. U-Boot이 그 공개키로 FIT configuration을 검증한다.
4. 커널은 FIT 안의 서명된 DT에서 cmdline을 받는다.

### 6.2 코드로 확인 — 빌드

Mbed TLS는 TF-A v2.15 `include/drivers/auth/mbedtls/default_mbedtls_config.h`가 요구하는 `MBEDTLS_CONFIG_VERSION 0x03060600`(= 3.6.6)에 맞췄다(TF-A changelog에도 "update mbedTLS to version 3.6.6"). 다운로드 후 GitHub 릴리스 노트의 SHA-256과 대조했다.

```sh
curl -sfL -o dl/mbedtls-3.6.6.tar.bz2 https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.6/mbedtls-3.6.6.tar.bz2
sha256sum dl/mbedtls-3.6.6.tar.bz2
curl -sfL https://api.github.com/repos/Mbed-TLS/mbedtls/releases/tags/mbedtls-3.6.6 | grep -o "[0-9a-f]\{64\}[^\"]\{0,40\}" | grep tar.bz2
make -j2 CROSS_COMPILE=aarch64-linux-gnu- PLAT=qemu DEBUG=1 LOG_LEVEL=40 \
  BL33=/var/tmp/L5/l5_ub_prod_s.bin TRUSTED_BOARD_BOOT=1 GENERATE_COT=1 \
  MBEDTLS_DIR=/var/tmp/L5/mbedtls BUILD_BASE=/var/tmp/L5/tbb all fip
```

```text
8fb65fae8dcae5840f793c0a334860a411f884cc537ea290ce1c52bb64ca007a  dl/mbedtls-3.6.6.tar.bz2
8fb65fae8dcae5840f793c0a334860a411f884cc537ea290ce1c52bb64ca007a mbedtls-3.6.6.tar.bz2
...
NOTICE:  Creating new key for 'Trusted World key'
NOTICE:  Creating new key for 'Non Trusted World key'
...
Non-Trusted Firmware BL33: offset=0x22DE7, size=0x12CA78, cmdline="--nt-fw"
...
Non-Trusted Firmware content certificate: offset=0x15113F, size=0x448, cmdline="--nt-fw-cert"
Built /var/tmp/L5/tbb/qemu/debug/fip.bin successfully
```

(빌드 로그는 NOTICE·`cmdline=` 줄만 남겼다. 전체 키·인증서 목록은 C5 §7.3과 같다.)

**출력에서 볼 것**: BL33 크기 `0x12CA78` = 1,231,480바이트 = TF-A용 잠근 U-Boot(`l5_ub_prod_s.bin`) 크기와 정확히 같다. 인증서 안의 해시와 실제 파일 해시를 대조해 보자.

```sh
sha256sum l5_ub_prod_s.bin
openssl asn1parse -inform DER -in tbb/qemu/debug/nt_fw_content.crt | grep -A2 "2100.1201" | tail -1 | grep -o "HEX DUMP.*"
```

```text
98912705886cee59a22d9033364d7dc4dafb795003fcdb63eb9963966a0b7d7e  l5_ub_prod_s.bin
HEX DUMP]:3031300D06096086480165030402010500042098912705886CEE59A22D9033364D7DC4DAFB795003FCDB63EB9963966A0B7D7E
```

**출력에서 볼 것**: OID `...2100.1201`(BL33 해시 칸, C5 §7.5)의 DigestInfo 뒤 32바이트가 U-Boot 파일의 SHA-256과 같다. 이 32바이트에 FIT 공개키(`rsa,modulus`)가 포함돼 있으므로, **공개키를 바꾸면 이 해시가 바뀐다**.

### 6.3 코드로 확인 — 전체 체인 부팅

```sh
qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -m 1057 -nographic \
  -bios tbb/qemu/debug/qemu_fw.bios -device loader,file=l5_good.itb,addr=0x48000000,force-raw=on
```

```text
NOTICE:  Booting Trusted Firmware
NOTICE:  BL1: v2.15.0(debug):
INFO:    Using crypto library 'mbed TLS'
INFO:    BL1: Loading BL2
INFO:    Loading image id=6 at address 0xe05b000
INFO:    Loading image id=1 at address 0xe05b000
NOTICE:  BL1: Booting BL2
NOTICE:  BL2: v2.15.0(debug):
INFO:    BL2: Loading image id 3
INFO:    Loading image id=7 at address 0xe090000
INFO:    Loading image id=9 at address 0xe090000
INFO:    Loading image id=13 at address 0xe090000
INFO:    Loading image id=3 at address 0xe090000
INFO:    BL2: Loading image id 5
INFO:    Loading image id=11 at address 0x60000000
INFO:    Loading image id=15 at address 0x60000000
INFO:    Loading image id=5 at address 0x60000000
INFO:    Image id=5 loaded: 0x60000000 - 0x6012ca78
NOTICE:  BL1: Booting BL31
NOTICE:  BL31: v2.15.0(debug):
INFO:    BL31: Preparing for EL3 exit to normal world
INFO:    Entry point address = 0x60000000
U-Boot 2026.10 (Oct 17 2026 - 21:21:41 -0700)
Core:  51 devices, 14 uclasses, devicetree: separate
Loading Environment from nowhere...Loading Environment from nowhere... OK
## Loading kernel (any) from FIT Image at 48000000 ...
   Using 'conf-s' configuration
   Verifying Hash Integrity ... sha256,rsa2048:dev+ OK
   Verifying Hash Integrity ... sha256+ OK
   ...
[    0.000000] Linux version 6.18.55-sec (donh@lima-elinux) ...
[    0.000000] psci: PSCIv1.1 detected in firmware.
[    0.000000] Kernel command line: console=ttyAMA0 LAB_CMD=cat~/proc/cmdline
console=ttyAMA0 LAB_CMD=cat~/proc/cmdline
[    7.887430] reboot: Power down
```

(Image-sec FIT로 돌린 실행이다. BL31 errata 줄, `Image id=N loaded` 줄 대부분, U-Boot 이미지 세부를 줄였다.)

**출력에서 볼 것**: `id=6 → 1`(BL2 인증서 → BL2), `7 → 9 → 13 → 3`(BL31), `11 → 15 → 5`(BL33 = 우리 U-Boot)가 C5 §7.4와 같은 순서로 지나가고, 그 U-Boot이 `conf-s`를 `dev+`로 검증한다. 커널은 `psci: PSCIv1.1 detected in firmware` — TF-A의 PSCI를 `smc`로 쓴다(서명된 `fdt-s`에 넣은 `/psci` 노드 덕분, §2.3). **ROM 대용 BL1부터 커널 cmdline까지 끊김 없는 체인**이다.

### 6.4 코드로 확인 — 공격자가 "자기 공개키를 넣은 U-Boot"으로 바꿔치기

**무엇을 확인하나**: 공격자가 FIT 검증을 통과하는 가장 쉬운 방법은 U-Boot의 공개키를 자기 것으로 바꾸는 것이다. 같은 U-Boot 코드(`u-boot-nodtb.bin`)에 공격자 키가 든 control DTB를 붙이고 FIP의 BL33만 바꾼다(인증서는 그대로).

```sh
cp l5_s.dtb l5_ctrl_evil.dtb
./build/tools/fdt_add_pubkey -a sha256,rsa2048 -k evilkeys -n dev -r conf l5_ctrl_evil.dtb
cat build-prod/u-boot-nodtb.bin l5_ctrl_evil.dtb > l5_ub_evil.bin
cp tbb/qemu/debug/fip.bin chain_evil/fip.bin
tbb/qemu/debug/tools/fiptool/fiptool update --nt-fw l5_ub_evil.bin chain_evil/fip.bin
cp tbb/qemu/debug/bl1.bin chain_evil/bios.bin && truncate -s 256K chain_evil/bios.bin && cat chain_evil/fip.bin >> chain_evil/bios.bin
qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -m 1057 -nographic -bios chain_evil/bios.bin \
  -device loader,file=l5_t2_evil.itb,addr=0x48000000,force-raw=on
```

```text
INFO:    BL2: Loading image id 5
INFO:    Loading image id=11 at address 0x60000000
INFO:    Image id=11 loaded: 0x60000000 - 0x600004f2
INFO:    Loading image id=15 at address 0x60000000
INFO:    Image id=15 loaded: 0x60000000 - 0x60000448
INFO:    Loading image id=5 at address 0x60000000
INFO:    Image id=5 loaded: 0x60000000 - 0x6012ca78
ERROR:   BL2: Failed to load image id 5 (-80)
```

**출력에서 볼 것**: `fdt_add_pubkey`(U-Boot 도구 — 서명 없이 공개키만 DTB에 넣는다)로 만든 가짜 U-Boot은 BL2에서 `-80`(`EAUTH`)으로 거부된다. 공격자 키로 서명한 FIT(`l5_t2_evil.itb`)는 실행될 기회조차 없다. **U-Boot의 FIT 서명 검증이 의미가 있으려면 U-Boot 자체가 앞 단계에서 검증돼야 한다** — 이 한 줄이 "chain"의 뜻이다.

> 남는 틈(실무): `fdtcontroladdr`가 가리키는 control DTB는 U-Boot이 relocation 뒤 RAM에 복사한다. 부팅 중 RAM을 바꿀 수 있는 공격자(JTAG, DMA 가능한 외부 장치)는 여기서도 이긴다. 그래서 JTAG 잠금(§9.4)과 IOMMU/외부 DMA 차단이 같이 간다.

---

## 7. 커널 다음 고리 — 모듈 서명과 lockdown

### 7.1 직관 — "커널도 부트로더처럼 다음 코드(.ko)를 검증한다"

커널이 서명돼 있어도 `insmod`로 아무 `.ko`나 넣을 수 있으면 커널 권한 코드가 체인 밖에서 들어온다. 그래서 커널은 모듈 끝에 붙은 PKCS#7 서명을 **커널 안에 컴파일된 인증서(.builtin_trusted_keys keyring)**로 검증한다. 공개키 위치만 다를 뿐 U-Boot의 control DTB와 같은 구조다.

```
 .ko file layout with CONFIG_MODULE_SIG (scripts/sign-file appends):
 +-------------------------+
 | ELF module (.text ...)  |
 +-------------------------+
 | PKCS#7 signature (DER)  |   signer, key id, sha256 of the ELF part
 +-------------------------+
 | struct module_signature |   12 bytes: algo, hash, id_type=PKEY_ID_PKCS7, sig_len (BE)
 +-------------------------+
 | "~Module signature appended~\n"   (28-byte magic)
 +-------------------------+
```

### 7.2 Image-sec의 설정

```sh
grep -E "MODULE_SIG|LOCK_DOWN|SECURITY_LOCKDOWN" /var/lab/out/linux-build-sec/.config
```

```text
CONFIG_MODULE_SIG_FORMAT=y
CONFIG_MODULE_SIG=y
# CONFIG_MODULE_SIG_FORCE is not set
CONFIG_MODULE_SIG_ALL=y
# CONFIG_MODULE_SIG_SHA1 is not set
CONFIG_MODULE_SIG_SHA256=y
...
CONFIG_MODULE_SIG_HASH="sha256"
CONFIG_SECURITY_LOCKDOWN_LSM=y
# CONFIG_SECURITY_LOCKDOWN_LSM_EARLY is not set
CONFIG_LOCK_DOWN_KERNEL_FORCE_NONE=y
```

- `MODULE_SIG=y`: 서명 검증 코드가 들어가고, 실패하면 **taint만** 한다(허용).
- `MODULE_SIG_ALL=y`: `make modules_install` 때 트리 안 모듈을 자동 서명한다. 키는 `CONFIG_MODULE_SIG_KEY`(기본 `certs/signing_key.pem`) — 설정하지 않으면 빌드가 **임시 키를 자동 생성**한다("Build time autogenerated kernel key").
- `MODULE_SIG_FORCE=y`면 무서명 모듈을 거부한다. 지금은 꺼져 있으니 부팅 인자 `module.sig_enforce=1`로 같은 효과를 낸다.

### 7.3 코드로 확인 — 모듈 세 개 만들고 서명하기

**무엇을 확인하나**: 같은 소스를 이름만 바꿔 세 번 빌드한다 — 빌드 키로 서명(`l5_signed`), 공격자 키로 서명(`l5_badkey`), 서명 없음(`l5_unsigned`).

```c
// l5 module-signing demo: same code is built three times under three names
#include <linux/module.h>
#include <linux/init.h>

static int __init l5_init(void)
{
	pr_info("%s: loaded\n", KBUILD_MODNAME);
	return 0;
}

static void __exit l5_exit(void)
{
	pr_info("%s: unloaded\n", KBUILD_MODNAME);
}

module_init(l5_init);
module_exit(l5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("L5 module signature demo");
```

```sh
# Makefile: obj-m := l5_signed.o l5_unsigned.o l5_badkey.o
B=/var/lab/out/linux-build-sec
make -C $B M=$PWD modules
openssl req -batch -new -nodes -utf8 -sha256 -days 365 -x509 -newkey rsa:2048 \
  -subj "/CN=L5 attacker module key" -keyout evil_mod.pem -out evil_mod.x509 -outform DER
$B/scripts/sign-file sha256 $B/certs/signing_key.pem $B/certs/signing_key.x509 l5_signed.ko
$B/scripts/sign-file sha256 evil_mod.pem evil_mod.x509 l5_badkey.ko
tail -c 28 l5_signed.ko | xxd
for m in l5_signed l5_badkey l5_unsigned; do echo "== modinfo $m"
  modinfo $m.ko | grep -E "^(vermagic|sig_id|signer|sig_key|sig_hashalgo)"; done
openssl x509 -in $B/certs/signing_key.x509 -inform DER -noout -subject -serial
```

```text
  LD [M]  l5_signed.ko
  LD [M]  l5_unsigned.ko
  LD [M]  l5_badkey.ko
00000000: 7e4d 6f64 756c 6520 7369 676e 6174 7572  ~Module signatur
00000010: 6520 6170 7065 6e64 6564 7e0a            e appended~.
== modinfo l5_signed
vermagic:       6.18.55-sec SMP preempt mod_unload aarch64
sig_id:         PKCS#7
signer:         Build time autogenerated kernel key
sig_key:        16:42:28:DD:9B:66:95:89:BD:25:DB:A6:D1:F6:62:DE:3E:DF:D1:8B
sig_hashalgo:   sha256
== modinfo l5_badkey
vermagic:       6.18.55-sec SMP preempt mod_unload aarch64
sig_id:         PKCS#7
signer:         L5 attacker module key
sig_key:        24:8E:25:AA:E8:1D:3D:6B:06:10:EC:19:1A:5A:40:23:42:75:69:46
sig_hashalgo:   sha256
== modinfo l5_unsigned
vermagic:       6.18.55-sec SMP preempt mod_unload aarch64
subject=CN=Build time autogenerated kernel key
serial=164228DD9B669589BD25DBA6D1F662DE3EDFD18B
```

**출력에서 볼 것**: 파일 끝 28바이트가 매직 문자열이다. `modinfo`의 `sig_key`는 인증서의 serial(`164228DD...`)과 같다. `modinfo`는 서명을 **검증하지 않는다** — 그냥 붙은 정보를 읽을 뿐이라 공격자 키 모듈도 똑같이 보인다. 판단은 커널이 한다.

### 7.4 코드로 확인 — 세 가지 정책에서 insmod

**무엇을 확인하나**: 같은 initramfs(BusyBox)로 cmdline만 바꿔 세 번 부팅해 세 모듈을 차례로 넣는다. 게스트 안 스크립트는 `insmod` 결과, `/sys/module/<m>/taint`, `/proc/sys/kernel/tainted`, dmesg를 찍는다.

```sh
for extra in "" "module.sig_enforce=1" "lockdown=integrity"; do
  timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 1G -nographic -kernel /var/lab/out/Image-sec \
    -initrd l5_mod_initrd.gz -append "console=ttyAMA0 quiet $extra LAB_CMD=sh~/l5_mod.sh" < /dev/null
done
```

```text
################ (기본: sig_enforce 없음)
== sig_enforce: N
[    3.607260] Loaded X.509 cert 'Build time autogenerated kernel key: 85c6473a51c5bcc330718d3beec0caf69ab654e2'
-- insmod l5_signed.ko
   ok, /sys/module/l5_signed/taint=[O]
-- insmod l5_badkey.ko
   ok, /sys/module/l5_badkey/taint=[OE]
-- insmod l5_unsigned.ko
   ok, /sys/module/l5_unsigned/taint=[OE]
== tainted=12288
[    4.501781] l5_signed: loading out-of-tree module taints kernel.
[    4.610123] l5_badkey: module verification failed: signature and/or required key missing - tainting kernel
################ module.sig_enforce=1
== sig_enforce: Y
-- insmod l5_signed.ko
   ok, /sys/module/l5_signed/taint=[O]
-- insmod l5_badkey.ko
insmod: can't insert '/l5_badkey.ko': Key was rejected by service
-- insmod l5_unsigned.ko
insmod: can't insert '/l5_unsigned.ko': Key was rejected by service
== tainted=4096
################ lockdown=integrity
== lockdown: none [integrity] confidentiality
== sig_enforce: N
-- insmod l5_signed.ko
   ok, /sys/module/l5_signed/taint=[O]
-- insmod l5_badkey.ko
insmod: can't insert '/l5_badkey.ko': Operation not permitted
-- insmod l5_unsigned.ko
insmod: can't insert '/l5_unsigned.ko': Operation not permitted
== tainted=4096
[    0.010571] LSM: initializing lsm=capability,landlock,lockdown,yama,apparmor,bpf,ima,evm
[    3.319382] Lockdown: insmod: unsigned module loading is restricted; see man kernel_lockdown.7
```

(세 번의 부팅 출력에서 cmdline 줄과 반복되는 `loaded` 줄을 줄였다. 첫 `lockdown` 실행은 securityfs를 마운트하지 않아 `n/a`였고, 마운트를 추가해 다시 돌린 결과를 실었다.)

**출력에서 볼 것**:

- **기본**: 셋 다 들어간다. 공격자 키 모듈과 무서명 모듈은 `E`(TAINT_UNSIGNED_MODULE, bit 13 = 8192) taint를 얻고, out-of-tree라 `O`(bit 12 = 4096)도 붙는다. 12288 = 8192 + 4096. "module verification failed" 메시지는 **첫 번째로 실패한 모듈에서 한 번만** 찍힌다(`l5_unsigned` 때는 안 찍혔다) — 로그만 보고 판단하면 놓친다. taint 비트 의미는 K7 참고.
- **`module.sig_enforce=1`**: 서명이 맞는 것만 들어간다. 무서명도 남의 키도 `Key was rejected by service`(`EKEYREJECTED`)다. tainted=4096(O만).
- **`lockdown=integrity`**: `sig_enforce`가 N인데도 거부된다. 커널의 모듈 로더가 서명 검증에 실패하면 `security_locked_down(LOCKDOWN_MODULE_SIGNATURE)`을 묻고, lockdown LSM이 거부한다(`EPERM`). lockdown은 모듈만이 아니라 `/dev/mem`, kexec 무서명 커널, hibernation(로그의 "hibernation is restricted"), 일부 debugfs·BPF 기능까지 닫는 "커널 무결성 모드"다.
- 실무 결론: 양산 커널은 `CONFIG_MODULE_SIG_FORCE=y`(cmdline에 의존하지 않음) + 필요하면 `CONFIG_LOCK_DOWN_KERNEL_FORCE_INTEGRITY=y`. cmdline에 의존하는 보안 설정은 §5.3처럼 cmdline이 서명 안에 있을 때만 의미가 있다.

### 7.5 모듈 키 관리 — 자동 생성 키의 함정

- 빌드마다 키가 새로 생기면, **같은 커널 버전의 다음 빌드 모듈을 이 커널에 넣을 수 없다**(키가 다르다). out-of-tree 드라이버(벤더 Wi-Fi, GPU)를 따로 빌드하는 팀은 반드시 고정 키(`CONFIG_MODULE_SIG_KEY="pkcs11:..."` 또는 파일)를 쓰고, 빌드 서버 밖으로 개인키가 나가지 않게 한다.
- 반대로 모든 모듈이 커널과 한 번에 빌드되는 제품이라면 **자동 생성 키를 빌드 직후 버리는 것**이 오히려 가장 안전하다(아무도 새 모듈을 서명할 수 없다). 개인키 `certs/signing_key.pem`이 빌드 산출물 디렉터리에 남는다는 점(이 랩에서도 `-rw-------`로 남아 있다)을 잊지 말 것 — 산출물을 통째로 보관·공유하면 키도 같이 나간다.
- 추가 키는 `CONFIG_SYSTEM_TRUSTED_KEYS`(PEM 파일)로 빌드 때 넣는다. 런타임에 keyring에 키를 더하는 경로(`CONFIG_SECONDARY_TRUSTED_KEYRING`)는 그 키도 이미 신뢰된 키로 서명돼 있어야 한다.

---

## 8. Secure boot vs measured boot — TPM PCR 맛보기

### 8.1 직관

- **Secure boot(verified boot)**: 각 단계가 다음 단계를 **검증하고, 틀리면 멈춘다**. 판단은 장비 안에서, 부팅 중에 한다.
- **Measured boot**: 각 단계가 다음 단계의 해시를 TPM의 PCR(Platform Configuration Register)에 **기록(extend)만 하고 계속 간다**. PCR은 덮어쓸 수 없고 `PCR_new = H(PCR_old || H(data))`로만 바뀐다 — 해시 체인이라 순서까지 기록된다. 판단은 나중에 한다: 원격 서버가 서명된 PCR 값(quote)을 보고 판단하거나(remote attestation), TPM이 "PCR이 이 값일 때만" 디스크 키를 풀어 준다(sealing).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="28" font-size="13">Secure boot: 검증하고 틀리면 멈춘다</text>
  <rect x="20" y="40" width="130" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="65" text-anchor="middle" font-size="12">ROM / BL2</text>
  <rect x="190" y="40" width="130" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="65" text-anchor="middle" font-size="12">U-Boot</text>
  <rect x="360" y="40" width="130" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="425" y="65" text-anchor="middle" font-size="12">kernel</text>
  <rect x="530" y="40" width="130" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="595" y="65" text-anchor="middle" font-size="12">틀리면 정지</text>
  <line x1="150" y1="60" x2="188" y2="60" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="320" y1="60" x2="358" y2="60" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="490" y1="60" x2="528" y2="60" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
  <text x="20" y="128" font-size="13">Measured boot: 기록만 하고 계속 간다 (판단은 나중에)</text>
  <rect x="20" y="140" width="130" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="85" y="165" text-anchor="middle" font-size="12">ROM / BL2</text>
  <rect x="190" y="140" width="130" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="255" y="165" text-anchor="middle" font-size="12">U-Boot</text>
  <rect x="360" y="140" width="130" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="425" y="165" text-anchor="middle" font-size="12">kernel · IMA</text>
  <line x1="150" y1="160" x2="188" y2="160" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="320" y1="160" x2="358" y2="160" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="20" y="215" width="470" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="255" y="240" text-anchor="middle" font-size="12">TPM PCR: new = H(old || H(다음 단계))  + 이벤트 로그</text>
  <line x1="85" y1="180" x2="85" y2="213" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="255" y1="180" x2="255" y2="213" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="425" y1="180" x2="425" y2="213" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="530" y="140" width="130" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="595" y="161" text-anchor="middle" font-size="12">원격 검증자</text>
  <text x="595" y="179" text-anchor="middle" font-size="12">(quote 확인)</text>
  <rect x="530" y="215" width="130" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="595" y="236" text-anchor="middle" font-size="12">sealing: PCR이</text>
  <text x="595" y="254" text-anchor="middle" font-size="12">맞을 때만 키 해제</text>
  <line x1="490" y1="235" x2="528" y2="235" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="490" y1="225" x2="528" y2="170" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="20" y="300" font-size="12">둘은 경쟁이 아니라 보완: secure boot = 실행 차단, measured boot = 증거와 비밀 보호</text>
</svg>
```
그림 5 — 두 방식의 차이. 국방·차량 장비는 보통 둘 다 쓴다: 부팅은 secure boot로 막고, 디스크 키는 measured boot의 PCR에 묶어(sealing) "변조된 상태로는 데이터도 못 연다"를 만든다. TPM 깊이(seal/unseal, attestation, IMA 정책)는 L6에서 다룬다.

### 8.2 코드로 확인 — swtpm + 게스트의 tpm2-tools

**무엇을 확인하나**: QEMU에 소프트웨어 TPM 2.0(swtpm)을 붙여 Image-sec + systemd rootfs(`rootfs-sysd.ext4` 사본)를 부팅하고, 게스트 안에서 PCR을 읽고, 직접 extend한 값이 공식과 맞는지 계산으로 검증한다.

```sh
swtpm socket --tpm2 --tpmstate dir=/var/tmp/L5/tpmstate --ctrl type=unixio,path=/var/tmp/L5/l5_swtpm.sock \
   --daemon --pid file=/var/tmp/L5/l5_swtpm.pid --log file=/var/tmp/L5/l5_swtpm.log
qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G -nographic -kernel /var/lab/out/Image-sec \
  -drive file=l5_root.ext4,if=virtio,format=raw \
  -chardev socket,id=chrtpm,path=/var/tmp/L5/l5_swtpm.sock -tpmdev emulator,id=tpm0,chardev=chrtpm \
  -device tpm-tis-device,tpmdev=tpm0 \
  -append "console=ttyAMA0 root=/dev/vda rw rootwait systemd.unit=multi-user.target"
```

게스트(root 자동 로그인) 안에서:

```text
# dmesg | grep -i tpm | head -5
[    5.077185] tpm_tis c000000.tpm_tis: 2.0 TPM (device-id 0x1, rev-id 1)
[    5.081785] tpm tpm0: A TPM error (256) occurred attempting the self test
[    5.081988] tpm tpm0: starting up the TPM manually
# tpm2_pcrread sha256:0,7,10,16
  sha256:
    0 : 0x0000000000000000000000000000000000000000000000000000000000000000
    7 : 0x0000000000000000000000000000000000000000000000000000000000000000
    10: 0x627A1AF4FDEE36BB9A9D759A3153C0D566DA327DA0EA9FE60C2E1A94219CEA4B
    16: 0x0000000000000000000000000000000000000000000000000000000000000000
# head -1 /sys/kernel/security/ima/ascii_runtime_measurements
10 ddee6004dc3bd4ee300406cd93181c5a2187b59b ima-ng sha1:9797edf8d0eed36b1cf92547816051c8af4e45ee boot_aggregate
# echo -n l5-app-v1.2.3 | sha256sum
e503e1b0adbee8c393d050c753b9c231ea889335644774315d523f1e0b2f3a81  -
# tpm2_pcrextend 16:sha256=$(echo -n l5-app-v1.2.3 | sha256sum | cut -d' ' -f1)
# tpm2_pcrread sha256:16
  sha256:
    16: 0x078D7DAB63AD1473510D5993AC387908B875587B4685DA157474394BEF94C475
# python3 -c "import hashlib;print(hashlib.sha256(bytes(32)+hashlib.sha256(b'l5-app-v1.2.3').digest()).hexdigest())"
078d7dab63ad1473510d5993ac387908b875587b4685da157474394bef94c475
# tpm2_pcrreset 16 && tpm2_pcrread sha256:16
  sha256:
    16: 0x0000000000000000000000000000000000000000000000000000000000000000
# tpm2_pcrreset 7; echo rc=$?
...
ERROR: Esys_PCR_Reset(0x907) - tpm:warn(2.0): bad locality
ERROR: Could not reset PCR index: 7
rc=1
```

(TCG에서 systemd 이미지 부팅부터 종료까지 약 6분 50초가 걸렸다 — VM에 다른 작업이 동시에 돌던 상황의 에뮬레이션 수치다. `tpm2_pcrreset 7`의 tss2 WARNING/ERROR 줄 두 개를 줄였다.)

**출력에서 볼 것**:

- 커널이 `tpm_tis`로 TPM을 찾고, 펌웨어가 `TPM2_Startup`을 안 했으므로 직접 시작했다("starting up the TPM manually"). 실제 measured boot라면 펌웨어가 Startup을 하고 측정을 시작했어야 한다.
- **PCR 0~7이 전부 0이다.** `-kernel`로 바로 부팅해서 펌웨어 단계를 아무도 측정하지 않았기 때문이다. measured boot는 "TPM이 있다"가 아니라 **"각 단계가 다음 단계를 측정했다"**여야 의미가 있다. U-Boot은 `CONFIG_MEASURED_BOOT`로 `bootm` 때 커널·initrd·bootargs를 PCR에 측정하고 이벤트 로그를 DT로 넘길 수 있다(`doc/usage/measured_boot.rst` — 이 노트에서는 실행하지 않았다, 가이드).
- **PCR 10**은 IMA가 쓴 값이다. Image-sec는 IMA가 켜져 있어(기본 정책 없음) 첫 항목 `boot_aggregate`(부트 PCR들의 요약 — 지금은 다 0)만 extend했다(`CONFIG_IMA_MEASURE_PCR_IDX=10`). IMA 정책과 appraise는 L6.
- PCR 16을 extend한 결과가 `sha256(32바이트 0 || sha256("l5-app-v1.2.3"))`와 **정확히 같다** — 그림 5의 공식이 실제 TPM 동작이다.
- PCR 16(디버그 PCR)은 리셋되지만 PCR 7은 `bad locality`로 거부된다. 부트 단계용 PCR은 OS가 지울 수 없어야 증거가 된다(PCR 0~15는 재부팅해야만 0이 된다).

---

## 9. 키 관리 — 진짜 어려운 부분

### 9.1 키 계층 — 한 키로 다 서명하지 않는다

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="170" y="15" width="340" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="37" text-anchor="middle" font-size="13">Root key (오프라인 HSM, 금고)</text>
  <text x="340" y="55" text-anchor="middle" font-size="12">공개키 해시 → 칩 fuse, 수명 동안 고정</text>
  <rect x="20" y="105" width="200" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="127" text-anchor="middle" font-size="12">Trusted World key</text>
  <text x="120" y="145" text-anchor="middle" font-size="12">BL31 · OP-TEE 서명</text>
  <rect x="240" y="105" width="200" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="127" text-anchor="middle" font-size="12">Non-Trusted World key</text>
  <text x="340" y="145" text-anchor="middle" font-size="12">BL33 (U-Boot) 서명</text>
  <rect x="460" y="105" width="200" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="560" y="127" text-anchor="middle" font-size="12">예비 슬롯 (revocation용)</text>
  <text x="560" y="145" text-anchor="middle" font-size="12">NXP SRK 4개 중 3개 등</text>
  <rect x="240" y="195" width="200" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="217" text-anchor="middle" font-size="12">FIT signing key</text>
  <text x="340" y="235" text-anchor="middle" font-size="12">공개키는 U-Boot DTB 안</text>
  <rect x="20" y="285" width="200" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="307" text-anchor="middle" font-size="12">kernel module key</text>
  <text x="120" y="325" text-anchor="middle" font-size="12">인증서는 커널 안 (§7)</text>
  <rect x="240" y="285" width="200" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="307" text-anchor="middle" font-size="12">verity root hash</text>
  <text x="340" y="325" text-anchor="middle" font-size="12">서명된 DT/cmdline 안 (L6)</text>
  <rect x="460" y="285" width="200" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="307" text-anchor="middle" font-size="12">OTA bundle key</text>
  <text x="560" y="325" text-anchor="middle" font-size="12">RAUC/SWUpdate 검증 (L4)</text>
  <rect x="20" y="355" width="640" height="36" rx="5" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="5 3"/>
  <text x="340" y="378" text-anchor="middle" font-size="12">CI → 서명 서버(PKCS#11) → HSM: 사람과 CI는 서명을 "요청"만 하고 개인키를 꺼낼 수 없다</text>
  <line x1="280" y1="65" x2="130" y2="103" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="340" y1="65" x2="340" y2="103" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="400" y1="65" x2="550" y2="103" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
  <line x1="340" y1="155" x2="340" y2="193" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="300" y1="245" x2="130" y2="283" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="340" y1="245" x2="340" y2="283" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="380" y1="245" x2="550" y2="283" stroke="currentColor" marker-end="url(#ah5)"/>
</svg>
```
그림 6 — 키 계층의 예. 화살표는 "이 키(또는 그 키로 서명된 이미지)가 다음 키를 담거나 보증한다". 위로 갈수록 바꾸기 어렵고 드물게 쓰며, 아래로 갈수록 자주 쓰고 바꾸기 쉽다. FIT 키는 U-Boot 안에 있으므로 "U-Boot을 새로 서명해 배포"하면 교체할 수 있다. 모듈 키·OTA 키도 같은 원리로 그 키를 담은 상위 이미지를 갱신하면 바뀐다.

원칙:

- **단계마다 다른 키**: TBBR이 ROT·TW·NTW·content 키를 나누는 이유(C5 §7.2)와 같다. U-Boot 팀의 키가 새도 BL31은 안전하고, FIT 키가 새도 U-Boot 업데이트로 교체할 수 있다.
- **개인키는 HSM 안에서만 쓴다**: mkimage는 `-N pkcs11`로 PKCS#11 엔진을 통해 HSM의 키로 서명할 수 있다(`doc/usage/fit/signature.rst`의 PKCS#11 절). TF-A `cert_create`는 `--key-alg`, 키 파일 인자로 외부 키를 받는다(`ROT_KEY=`, `TRUSTED_WORLD_KEY=`, `NON_TRUSTED_WORLD_KEY=` make 변수). 커널은 `CONFIG_MODULE_SIG_KEY`에 `pkcs11:` URI를 받는다.
- **서명 서버 + 감사 로그**: 누가 어떤 커밋의 어떤 이미지를 언제 서명했는지 기록. 양산 키 서명은 2인 승인(two-person rule).
- **dev 키와 prod 키를 분리**: 개발 보드는 fuse를 안 태우거나 dev 키 해시로 태운다. prod 키로 서명된 이미지는 릴리스 파이프라인에서만 나온다. dev 키로 서명된 이미지가 prod 장비에서 부팅되면 안 된다(그래서 해시가 달라야 한다).
- **키 길이·알고리즘은 ROM이 정한다**: 부트 ROM이 지원하는 알고리즘(RSA-2048/3072/4096, ECDSA P-256/P-384)이 바닥이다. 후단(U-Boot, 커널)은 바꿀 수 있지만 ROM은 못 바꾼다.

### 9.2 키 유출 — 무엇을 하나

"Signing key leaked — what now?"의 답은 **어느 키가 샜느냐**에 따라 다르다. 그래서 계층이 중요하다.

| 샌 키 | 공격자가 할 수 있는 것 | 대응 |
|---|---|---|
| OTA 번들 키 (L4) | 원격 업데이트로 악성 이미지 배포(단, 그 안의 부트 이미지는 아래 키로 또 검증됨) | 서버 측 차단, 새 키를 담은 업데이트 배포, 옛 키 폐기 |
| FIT 서명 키 | 자기 커널·initramfs를 부팅 | 새 FIT 키를 담은 U-Boot을 NTW 키로 서명해 배포 + U-Boot 롤백 방지 카운터 올리기 |
| NTW/TW 키 (TBBR) | 자기 U-Boot/BL31 | ROT 키로 새 Trusted Key 인증서 발급 + NV counter 올리기 |
| ROT 키 (fuse 해시) | 모든 것 | revocation 슬롯이 있으면 다음 키로 전환(NXP SRK revoke, Xilinx SPK ID 등), 없으면 **현장 장비는 영구히 신뢰 불가** → 리콜·교체 |

공통 절차: (1) 범위 확인 — 어느 키, 언제부터, 무엇이 그 키로 서명됐나(감사 로그). (2) 새 키로 서명한 이미지를 **옛 키로도 검증되는 경로**로 먼저 배포(아직 옛 키를 믿는 장비가 새 키를 받아야 하니까). (3) 장비가 새 이미지로 부팅에 성공한 뒤 옛 키를 폐기(fuse revoke, 카운터 증가). 순서를 틀리면 장비가 벽돌이 된다. (4) 유출 원인 제거 — 그래서 처음부터 HSM이다.

### 9.3 롤백 방지 — 서명만으로는 옛 버전을 못 막는다

- TF-A: 인증서의 NV counter ≥ OTP 카운터여야 통과(C5 §7.7에서 실측).
- U-Boot FIT: 이 트리(U-Boot 2026.10)의 `boot/`에서 "rollback"을 grep하면 VBE의 "eventually provide rollback protection" 주석뿐이다 — **mainline FIT에는 커널 이미지용 표준 anti-rollback이 없다**. 그래서 실무 설계는: (a) U-Boot은 TF-A NV counter로 롤백을 막고, (b) 커널 FIT의 "보안 버전"은 FIT 안의 서명된 속성으로 넣고 보드 코드가 RPMB·TPM NV counter·OTP와 비교하는 식으로 만든다(벤더 BSP에 따라 다르다).
- OTA와의 충돌: 새 이미지로 첫 부팅하자마자 카운터를 올리면 B 슬롯(옛 버전)으로 되돌아갈 수 없다. **"새 슬롯이 정상 확인(commit)된 뒤에만 카운터를 올린다"**가 정석이다(L4의 bootcount·commit과 같이 설계).

### 9.4 공장 — fuse 공정과 디버그 포트

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="15" width="400" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="220" y="42" text-anchor="middle" font-size="12">1. 빈 칩: open 상태, 아무 이미지나 부팅</text>
  <rect x="20" y="80" width="400" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="220" y="107" text-anchor="middle" font-size="12">2. prod 키로 서명한 이미지 flash</text>
  <rect x="20" y="145" width="400" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="220" y="172" text-anchor="middle" font-size="12">3. 키 해시 fuse 쓰기 (SRK / PKC / ROTPK 해시)</text>
  <rect x="20" y="210" width="400" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="220" y="237" text-anchor="middle" font-size="12">4. open 상태로 재부팅: 검증 이벤트 0개 확인</text>
  <rect x="20" y="275" width="400" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="220" y="302" text-anchor="middle" font-size="12">5. lock fuse: secure boot 강제 (closed)</text>
  <rect x="20" y="340" width="400" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="220" y="367" text-anchor="middle" font-size="12">6. JTAG 차단 · fuse 쓰기 잠금 → 출하</text>
  <line x1="220" y1="59" x2="220" y2="78" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="220" y1="124" x2="220" y2="143" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="220" y1="189" x2="220" y2="208" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="220" y1="254" x2="220" y2="273" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="220" y1="319" x2="220" y2="338" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="440" y="145" width="225" height="44" rx="5" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="5 3"/>
  <text x="552" y="164" text-anchor="middle" font-size="12">되돌릴 수 없음</text>
  <text x="552" y="181" text-anchor="middle" font-size="12">(OTP: 0 → 1만 가능)</text>
  <rect x="440" y="210" width="225" height="44" rx="5" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="5 3"/>
  <text x="552" y="229" text-anchor="middle" font-size="12">이벤트가 있으면 여기서</text>
  <text x="552" y="246" text-anchor="middle" font-size="12">멈추고 원인 분석</text>
  <rect x="440" y="275" width="225" height="44" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a" stroke-dasharray="5 3"/>
  <text x="552" y="294" text-anchor="middle" font-size="12">이미지가 틀리면</text>
  <text x="552" y="311" text-anchor="middle" font-size="12">이 순간부터 벽돌</text>
  <text x="20" y="408" font-size="12">공정 스테이션은 각 단계 결과(fuse 값, 검증 로그)를 장비 시리얼과 함께 기록한다</text>
</svg>
```
그림 7 — 양산 fuse 공정의 일반형. 칩 벤더마다 이름은 다르지만 "키 해시 쓰기 → open 상태에서 검증 확인 → lock"의 3박자는 같다. 4단계를 건너뛰는 것이 가장 비싼 실수다.

- 4단계가 핵심이다. NXP HAB은 open 상태에서도 검증을 수행하고 결과를 이벤트로 남긴다(U-Boot `hab_status`). 이벤트 0개를 확인한 뒤에만 `SEC_CONFIG`(closed) fuse를 태운다. 다른 벤더도 "검증은 하되 강제하지 않는 모드"가 있다(기억 기반 — 칩별 보안 레퍼런스 매뉴얼 확인).
- **JTAG**: secure boot가 닫혀도 JTAG이 열려 있으면 부팅 후 CPU를 멈추고 메모리·레지스터를 바꿀 수 있다. 선택지는 (a) fuse로 완전 비활성, (b) 인증 디버그(challenge-response, 칩별 secure JTAG), (c) 장비별로 다른 키로 잠그고 RMA 때만 서명된 토큰으로 연다. Don이 Apple에서 JTAG으로 bring-up하던 감각 그대로, **양산 칩에서 JTAG이 닫혔는지 실제로 붙여서 확인**하는 단계가 공정에 있어야 한다.
- UART 콘솔도 디버그 포트다: §5.5처럼 U-Boot 입력을 막고, 커널 콘솔·getty도 제품 빌드에서 끄거나 인증을 건다.

### 9.5 벤더 구현 비교 (기억 기반 — 각 SoC 보안 매뉴얼로 확인할 것)

| SoC 계열 | ROM 검증 이름 | 키 · fuse | 서명 도구 | 특징 |
|---|---|---|---|---|
| NXP i.MX6/7/8M | HAB (High Assurance Boot) | SRK 테이블(최대 4개)의 해시를 fuse, SRK_REVOKE로 폐기 | CST (Code Signing Tool), CSF | open 상태에서 `hab_status`로 이벤트 확인 후 closed |
| NXP i.MX8/8X/8ULP/9 | AHAB (Advanced HAB, SECO/ELE) | SRK 해시 fuse, container 헤더 | CST (AHAB 모드) | 이미지를 container 형식으로, `ahab_status` |
| TI K3 (AM62x, AM64x, J7) | HS 장치 ROM (HS-FS → HS-SE) | SMPK/BMPK 해시를 OTP(keywriter) | 이미지 앞 X.509 인증서 (TI 스크립트/binman) | GP/HS-FS/HS-SE 장치 등급, TIFS 보안 펌웨어 |
| NVIDIA Jetson (Orin 등) | BootROM + MB1 | PKC(공개키 해시) + SBK(AES 암호화 키) fuse | `odmfuse.sh`, `flash.sh`에 키 전달 | 서명과 암호화를 같이, fuse 후 서명 없는 flash 불가 |
| Rockchip (RK35xx) | BootROM secure boot | OTP/eFuse에 RSA 공개키 해시 | `rk_sign_tool`, U-Boot SPL FIT 서명 | SPL이 U-Boot·TF-A FIT를 검증 |
| Xilinx Zynq US+ / Versal | CSU/PMC BootROM | PPK 해시 eFuse, SPK ID로 폐기 | `bootgen` (.bif) | PPK→SPK 2단 키, AES(eFuse/BBRAM) 암호화 옵션 |

공통 패턴: **ROM이 공개키 해시(fuse)로 첫 이미지를 검증 → 그 뒤는 TF-A/U-Boot/커널의 표준 메커니즘**. 그래서 이 노트에서 QEMU로 해 본 TBB·FIT·모듈 서명은 어떤 SoC에서도 그대로 쓰이고, 벤더마다 달라지는 것은 맨 앞 한 고리와 fuse 공정이다.

---

## 10. 커널 안에서는 무슨 일이 일어나나

secure boot의 대부분은 커널 **이전**에 일어나지만, 커널은 체인을 이어받아 런타임까지 늘린다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="15" width="300" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="40" text-anchor="middle" font-size="12">insmod → finit_module(fd) syscall</text>
  <rect x="20" y="80" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="105" text-anchor="middle" font-size="12">load_module() → module_sig_check()</text>
  <rect x="20" y="145" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="170" text-anchor="middle" font-size="12">mod_verify_sig() → PKCS#7 검증</text>
  <rect x="20" y="210" width="300" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="235" text-anchor="middle" font-size="12">.builtin_trusted_keys (빌드 시 인증서)</text>
  <rect x="360" y="80" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="105" text-anchor="middle" font-size="12">실패 + sig_enforce → EKEYREJECTED</text>
  <rect x="360" y="145" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="170" text-anchor="middle" font-size="12">실패 + lockdown → EPERM</text>
  <rect x="360" y="210" width="300" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="235" text-anchor="middle" font-size="12">실패 + 허용 → TAINT_UNSIGNED_MODULE (E)</text>
  <rect x="20" y="285" width="640" height="56" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a" stroke-dasharray="5 3"/>
  <text x="340" y="307" text-anchor="middle" font-size="12">같은 keyring을 쓰는 곳: kexec_file_load(서명 커널), IMA appraise, dm-verity 서명 root hash,</text>
  <text x="340" y="327" text-anchor="middle" font-size="12">PKCS#7 펌웨어 서명 — "커널이 믿는 공개키 목록"은 하나다 (L6)</text>
  <line x1="170" y1="55" x2="170" y2="78" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="170" y1="120" x2="170" y2="143" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="170" y1="185" x2="170" y2="208" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="320" y1="100" x2="358" y2="100" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="320" y1="165" x2="358" y2="165" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="320" y1="230" x2="358" y2="230" stroke="currentColor" marker-end="url(#ah7)"/>
</svg>
```
그림 8 — 모듈 서명 검증 경로(6.x 커널 기준 함수 이름, `kernel/module/signing.c`, `kernel/module/main.c`, `certs/system_keyring.c`).

- `module_sig_check()`(`kernel/module/signing.c`)는 모듈 끝의 매직 문자열을 찾고, 있으면 `mod_verify_sig()`가 `struct module_signature`를 읽어 PKCS#7 검증(`verify_pkcs7_signature()`)을 부른다. 결과가 서명 없음(`-ENODATA`, "unsigned module"), keyring에 없는 키(`-ENOKEY`, "module with unavailable key" — 공격자 키 모듈이 여기다), 지원 안 되는 암호(`-ENOPKG`)이면 "봐줄 수 있는 실패"로 분류하고, 그 밖의 에러(파싱 불가 서명, 키는 있는데 서명 불일치 등)는 정책과 무관하게 바로 거부한다. 봐줄 수 있는 실패일 때 `is_module_sig_enforced()`면 "Loading of %s is rejected"를 찍고 **항상 `-EKEYREJECTED`**를 돌려주고(그래서 §7.4에서 두 모듈 모두 `Key was rejected by service`), 아니면 `security_locked_down(LOCKDOWN_MODULE_SIGNATURE)`의 결과(lockdown이면 `-EPERM`)를 돌려준다. 둘 다 통과하면 `kernel/module/main.c`가 `pr_notice_once()`로 "module verification failed"를 **딱 한 번** 찍고 `TAINT_UNSIGNED_MODULE`을 세운다 — §7.4에서 메시지가 한 번만 나온 이유다.
- 빌드 때 `certs/signing_key.x509`가 `certs/system_certificates.S`로 커널 이미지 안에 박혀, 부팅 중 `Loading compiled-in X.509 certificates` 메시지와 함께 `.builtin_trusted_keys`에 들어간다(§7.4 dmesg). 즉 **모듈 공개키의 무결성은 커널 Image의 무결성(= FIT 서명)에 기대고 있다** — U-Boot DTB 속 FIT 공개키와 같은 구조다.
- 드라이버를 쓸 때 다시 만나는 지점: out-of-tree 드라이버(E4, G 모듈)를 양산 장비에 넣으려면 같은 키로 서명돼야 한다. `request_firmware()`로 올리는 장치 펌웨어(H·I 모듈)는 기본적으로 서명 검증이 없다 — 펌웨어 blob도 체인 안에 넣으려면 rootfs 무결성(dm-verity, L6)이나 펌웨어 서명 검증을 따로 설계해야 한다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트 장비의 secure boot 설계 한 장

예를 들어 i.MX8M Plus 기반 로봇 센서 키트라면:

1. ROM(HAB)이 SRK 해시 fuse로 SPL+TF-A+U-Boot 컨테이너를 검증(CST로 서명).
2. U-Boot은 control DTB의 FIT 키(`required = conf`)로 커널·DT·initramfs FIT 검증. `bootcmd` 고정, `ENV_IS_NOWHERE` 또는 A/B 변수만 화이트리스트, 콘솔 입력 차단.
3. DT의 `/chosen/bootargs`에 `root=/dev/dm-0`, `dm-mod.create="..."`(verity root hash 포함), `module.sig_enforce=1`, `lockdown=integrity`.
4. rootfs는 dm-verity(L6), `/data`는 dm-crypt + TPM 또는 CAAM 블롭으로 키 보호(L6).
5. OTA(L4)는 번들 서명 + A/B, 새 슬롯 commit 후에만 rollback 카운터 증가.
6. 공장: SRK fuse → open 상태 HAB 이벤트 0 확인 → closed → JTAG 비활성 → 결과 기록.

### 11.2 "보안 켰더니 부팅이 3초 늦어졌어요"

검증 시간의 대부분은 RSA가 아니라 **큰 이미지의 해시**다(RSA-2048 검증은 한 번, 해시는 수십 MB). 대책: 커널을 압축하되 해시는 압축본에 걸기(읽을 양도 준다), SoC 해시 가속기 쓰기(U-Boot `CONFIG_SHA_HW_ACCEL`, 이 빌드의 `CONFIG_ARMV8_CRYPTO`), initramfs 줄이기. 이 노트의 QEMU 수치(TCG)는 실제와 무관하니 반드시 실보드에서 `bootstage`로 잰다.

### 11.3 "필드 장비를 디버그해야 하는데 콘솔이 막혀 있다"

인증 디버그 경로를 미리 설계한다: 장비 고유 ID에 묶인 **서명된 디버그 토큰**(우리 서명 서버가 발급, 기한 있음)을 U-Boot·커널이 검증해 그 장비에서만 콘솔을 연다. "공통 비밀번호"는 하나가 새면 전 기종이 열린다.

### 11.4 GPLv3와 secure boot

GPLv3 코드를 "User Product"에 넣으면 수정본 설치 정보(키 또는 잠금 해제 방법)를 줘야 한다(anti-tivoization, K7 참고). 그래서 잠긴 장비는 rootfs에서 GPLv3 패키지를 빼는 정책과 같이 간다. U-Boot·커널은 GPLv2라 해당하지 않는다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `mkimage -K`에 `-r`을 안 줌 | 서명 없는 FIT도 부팅됨 | 키가 `required`가 아니라 검증이 선택 사항 | `-r`로 넣고 `fdtget ... required`로 확인 (§2.5) |
| 이미지만 서명 (`required = image`) | 조합을 바꾼 FIT가 부팅됨 | mix-and-match (§5.2) | configuration 서명 + `sign-images`에 kernel·fdt·ramdisk 전부 |
| 공개키를 `-dtb`나 앞 단계 DT로 넘김 | 남의 키 FIT도 부팅 가능 | 키가 검증된 바이너리 밖에 있음 | control DTB를 U-Boot에 붙이고 그 U-Boot을 앞 단계가 검증 (§3, §6) |
| env `bootargs`를 남겨 둠 | 서명 통과 후 cmdline이 바뀜 | `fdt_chosen()`이 env로 덮어씀 (§5.3) | `ENV_IS_NOWHERE` 또는 화이트리스트, bootargs는 서명된 DT에 |
| `bootm` 실패 후 프롬프트 | 공격자가 `booti`로 무검증 부팅 | 실패 경로가 열림 (§5.4) | `bootcmd` 끝에 reset/복구, 부트 명령 제거, `BOOTDELAY=-2` |
| fuse 전 검증 확인 생략 | lock 후 전 수량 부팅 불가 | 이미지 서명·키 해시 불일치 | open 상태 이벤트 0 확인 후 lock (§9.4) |
| 모듈 서명 키를 매 빌드 자동 생성 | 다음 빌드의 out-of-tree 모듈이 `Key was rejected` | 커널마다 키가 다름 | 고정 키(HSM) 또는 모든 모듈을 같은 빌드에서 |
| `MODULE_SIG=y`만 켜고 끝 | 무서명 모듈이 E taint만 남기고 들어감 | 기본 정책은 허용 | `MODULE_SIG_FORCE=y` 또는 lockdown integrity |
| 롤백 카운터를 첫 부팅에 올림 | 새 버전 문제 시 B 슬롯으로 못 돌아감 | 카운터는 되돌릴 수 없음 | commit 후에만 증가 (§9.3, L4) |
| JTAG을 양산에서 안 닫음 | 부팅 후 메모리 변조로 우회 | secure boot는 실행 전 검증만 | fuse 비활성/인증 디버그, 공정에서 확인 |

---

## 13. 면접에서 이렇게 말한다

**Q.** Walk me through secure boot on your last product.

**A.** 뿌리부터 순서대로, 각 고리에서 "누가 무엇으로 검증하나"를 말한다. 마지막에 롤백·디버그 포트·키 관리를 덧붙여 위협 모델까지 생각했다는 것을 보인다. Don은 SSD 펌웨어 서명 경험을 출발점으로 쓰고, Linux SoC는 이 노트의 QEMU 체인으로 설명한다.

> On the SSD side, the boot ROM verified the bootloader against a key hash in OTP, and the bootloader verified the main firmware before jumping, with an anti-rollback version check. On Linux SoCs the chain is longer but the same idea: the ROM checks the first-stage image against a fused public-key hash; TF-A's Trusted Board Boot verifies BL31 and U-Boot through an X.509 certificate chain with NV counters; U-Boot has the FIT public key in its own control device tree, marked required, and verifies a signed FIT configuration — kernel, device tree with the command line, and initramfs; the kernel then verifies the root filesystem with dm-verity and only loads signed modules. I've built that whole chain in QEMU and broken each link — a flipped byte, a foreign key, an unsigned FIT, a swapped U-Boot key — to confirm each one fails closed. Around it you need the locked-down U-Boot, rollback counters, a closed JTAG, and HSM-backed keys.

**꼬리 질문**: "Where exactly does the U-Boot public key live, and who protects it?" → control DTB가 `u-boot.bin` 끝에 붙어 있고, 그 바이너리의 해시가 TF-A NT FW content 인증서에 있다(§6.2).

**Q.** Why sign the configuration, not the individual images?

**A.** 이미지별 서명은 "각 부품이 정품"만 보장하고 "이 조합이 정품"은 보장하지 않는다. 공격자가 서명된 이미지들을 섞어(옛 취약 커널 + 새 DT, 디버그 DT) 새 조합을 만들 수 있다. configuration 서명은 고른 이미지 이름과 그 해시를 함께 서명하므로 조합이 고정된다. 직접 해 보니 이미지별 서명은 디버그 DT로 바꾼 조합을 부팅했고, configuration 서명은 거부했다.

> Image signatures only prove each blob is genuine, not that this particular combination was released. An attacker can recombine signed parts — an old vulnerable kernel with a new device tree, or a lab-only debug DT. A configuration signature covers the configuration node, which names the images, plus each image's hash node, so changing any reference breaks it. Data itself is excluded and bound through the hashes, so verification is signature first, then per-image hash. I demonstrated it: with image-only signing, swapping fdt to the debug DT booted; with configuration signing it failed with "Failed to verify required signature".

**Q.** Your signing key leaked — what now?

**A.** 먼저 "어느 키냐"로 답을 나눈다. 계층이 있으면 피해는 그 키가 보증하던 단계로 한정되고, 상위 키로 새 키를 배포해 교체한다. 순서는 "새 키 이미지를 옛 신뢰 경로로 배포 → 부팅 성공 확인 → 옛 키 폐기(revoke fuse, 카운터)". ROT 키면 revocation 슬롯 유무가 생사를 가른다. 그리고 원인(HSM 밖에 키가 있었나)을 고친다.

> First, which key. With a proper hierarchy the blast radius is limited to what that key signed. If it's the FIT key, I ship a new U-Boot — signed by the non-trusted-world key — that carries the new FIT key, and bump the firmware rollback counter so the old U-Boot can't come back. If it's a TBBR intermediate key, the root key signs a new key certificate. The order matters: deliver the new key through the still-trusted path, confirm devices boot, then revoke the old key. If it's the root key, it depends on whether the SoC has revocation slots like NXP's SRK revoke fuses; without them, fielded devices can't be trusted anymore. Then fix the root cause — private keys should live only in an HSM behind a signing service with audit logs.

**꼬리 질문**: "How do you stop the attacker from installing an old image signed with the old key?" → 롤백 카운터(NV counter)와 키 폐기를 같이 한다.

**Q.** Secure boot vs measured boot?

**A.** secure boot는 검증하고 틀리면 멈춘다(장비 안 판단). measured boot는 각 단계 해시를 TPM PCR에 extend만 하고 진행한다(판단은 나중 — 원격 attestation, 또는 PCR에 묶은 키 unseal). 둘은 보완 관계다.

> Secure boot enforces: each stage verifies the next and refuses to run anything unsigned. Measured boot records: each stage extends a hash of the next into a TPM PCR, which can only be extended, never written, and boot continues regardless. The decision happens later — a remote verifier checks a signed quote of the PCRs, or the TPM releases a disk key only if the PCRs match. We typically use both: secure boot to block untrusted code, measured boot to prove state and to seal secrets like the data-partition key.

**Q.** What can secure boot NOT protect against?

**A.** 런타임 침해, 롤백(별도 카운터 없으면), JTAG·DMA 공격, rootfs 변조(체인을 rootfs까지 안 이으면), 기밀성, 키 유출. "부팅 시점에 우리 코드만 돈다"까지만 약속한다.

> It guarantees only that the code we signed is what boots. It doesn't stop a runtime exploit, a rollback to an older signed image unless you have anti-rollback counters, an open JTAG or DMA attack after boot, or tampering with an unverified root filesystem. It also isn't confidentiality — that's encryption. So I pair it with dm-verity, rollback counters, a closed debug port, least privilege at runtime, and key management.

**Q.** How do you lock down U-Boot for production?

**A.** 프롬프트·환경·옆길 부트 명령 세 가지를 닫는다. 이 노트에서 실측: 개발 U-Boot에서 env `bootargs`로 서명 통과 후 cmdline을 바꿨고, `booti`로 무검증 커널을 부팅했다. 잠근 빌드(`BOOTDELAY=-2`, `ENV_IS_NOWHERE`, `booti`/`bootz`/`bootefi`/legacy 제거, `bootcmd` 끝에 poweroff)는 키를 두드려도 프롬프트가 안 나오고, 변조 FIT에서 바로 꺼졌다.

> Close the prompt, the environment, and the side doors. Bootdelay minus two so there's no interrupt window; environment from nowhere, or a writable whitelist for A/B variables only; the command line inside the signed device tree; remove booti, bootz, bootefi and the legacy image format so bootm with FIT is the only path; and make the failure path reset or fall back to the other slot rather than drop to a shell. In my lab, the dev build let me hijack bootargs after a fully verified FIT and boot an unverified kernel with booti; the locked build never showed a prompt and powered off on a tampered FIT.

**Q.** How is kernel module signing enforced, and how did you test it?

**A.** `CONFIG_MODULE_SIG`는 검증 코드, `MODULE_SIG_FORCE`(또는 `module.sig_enforce=1`)가 강제, lockdown integrity도 무서명 모듈을 막는다. 공개키는 커널 Image 안의 builtin keyring에 있으니 커널 서명에 기대고 있다. 실측: 기본은 무서명·남의 키 모듈이 E taint와 함께 로드, enforce면 `Key was rejected by service`, lockdown이면 `Operation not permitted`.

> The build embeds an X.509 certificate into the kernel's builtin trusted keyring and appends a PKCS#7 signature to each module. With only MODULE_SIG, a bad or missing signature just taints the kernel with E. With MODULE_SIG_FORCE or module.sig_enforce=1 the load fails with EKEYREJECTED, and with lockdown in integrity mode it fails with EPERM. I tested signed, foreign-key, and unsigned builds of the same module under all three policies. The trust anchor is inside the kernel image, so it's only as strong as the FIT signature that protected the kernel.

---

## 14. 직접 해보기

1. **개념**: 1바이트 변조 FIT에서 configuration 서명이 `dev+ OK`로 통과한 이유를 그림 2로 설명하라. 공격자가 `hash-1/value`까지 새로 계산해 넣으면 어디서 걸리나?
   정답/힌트: 서명은 data를 제외한 노드 구조(해시 값 포함)를 덮는다. hash 값을 고치면 hashed-nodes 영역이 바뀌어 configuration 서명이 깨진다.
2. **코드**: §2.3의 .its에서 `algo = "sha256,rsa4096"`로 바꾸고 4096비트 키로 다시 서명해, control DTB의 `rsa,num-bits`와 FIT 크기 변화를 확인하라.
   정답/힌트: `openssl genpkey ... rsa_keygen_bits:4096`, `fdtget l5_ctrl.dtb /signature/key-dev rsa,num-bits` → 4096. 서명 `value`가 512바이트가 된다.
3. **코드**: 키 두 개(`dev`, `dev2`)를 control DTB에 넣고 `/signature`에 `required-mode = "all"`을 준 뒤, 한 키로만 서명한 FIT가 거부되는지 `fit_check_sign`으로 확인하라.
   정답/힌트: `fdtput -t s l5_ctrl.dtb /signature required-mode all`, configuration에 `signature-1`(dev), `signature-2`(dev2).
4. **관찰**: 잠근 U-Boot에 `CONFIG_ENV_WRITEABLE_LIST`를 켜고 `bootcount`·`slot` 두 변수만 쓰기 허용하도록 만들어, `bootargs`를 플래시 환경에 넣어도 무시되는지 확인하라(L4 A/B와 연결).
   정답/힌트: `CONFIG_ENV_FLAGS_LIST_STATIC="slot:sw,bootcount:dw"` 형식. 환경을 플래시에 두려면 `ENV_IS_IN_FLASH`를 다시 켠다.
5. **코드**: §7.3의 모듈을 `CONFIG_MODULE_SIG_FORCE=y`인 커널(직접 빌드)에서 넣어 cmdline 없이도 거부되는지, `/proc/sys/kernel/tainted` 값이 어떻게 다른지 확인하라.
   정답/힌트: `Key was rejected by service`, E 비트 없음.
6. **설계**: 사내 OTA(L4)로 커널 FIT를 보낼 때 "보안 버전"을 어디에 넣고, 누가 어디서 비교하고, 카운터를 언제 올릴지 한 장으로 그려라.
   정답/힌트: FIT configuration 안 서명된 속성 → U-Boot 보드 코드가 RPMB/TPM NV/OTP와 비교 → 새 슬롯 commit 후 증가.
7. **관찰**: §8.2의 swtpm 실습에서 `tpm2_pcrextend`를 같은 값으로 두 번 하면 PCR 16이 어떻게 되는지, 순서를 바꾸면 결과가 같은지 계산으로 예측하고 확인하라.
   정답/힌트: extend는 교환법칙이 없다 — `H(H(0||a)||b) ≠ H(H(0||b)||a)`.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Root of Trust | 신뢰의 뿌리 | 바꿀 수 없는 ROM 코드 + fuse 키 해시 |
| ROTPK | Root Of Trust Public Key | TF-A 용어. 해시가 fuse에 들어간다 |
| OTP / eFuse | 한 번만 쓰는 메모리 | 0→1만 가능, 키 해시·잠금 비트·카운터 |
| TBB / TBBR | Trusted Board Boot (Requirements) | TF-A의 X.509 인증서 기반 체인 (C5) |
| NV counter | 비휘발 카운터 | 인증서 버전 ≥ 카운터여야 통과, 롤백 방지 |
| FIT | Flattened Image Tree | FDT 형식에 여러 이미지 + 해시/서명을 담은 U-Boot 이미지 |
| control DTB | U-Boot 자신의 DT | `/signature/key-*`에 FIT 공개키가 들어간다 |
| `required` | 키 정책 | `conf`/`image` — 이 키 서명이 없으면 부팅 거부 |
| hashed-nodes | 서명 범위 목록 | configuration 서명이 덮는 노드들 (data 제외) |
| mix-and-match | 조합 공격 | 서명된 이미지들을 섞어 의도하지 않은 조합 부팅 |
| `OF_SEPARATE` / `OF_BOARD` | U-Boot DT 출처 | 바이너리에 붙인 DT / 앞 단계가 준 DT |
| `EXT_DTB` | U-Boot make 변수 | 기본 DTB 대신 지정한 DTB를 붙인다 |
| `MODULE_SIG_FORCE` | 커널 설정 | 서명 안 맞는 모듈 거부 (`module.sig_enforce=1`과 같음) |
| lockdown | 커널 LSM | integrity/confidentiality 모드로 커널 수정 경로 차단 |
| PCR | Platform Configuration Register | TPM 안 레지스터, extend로만 바뀐다 |
| measured boot | 측정 부팅 | 해시를 PCR에 기록, 판단은 나중 |
| HSM | Hardware Security Module | 개인키를 안에서만 쓰는 장치, PKCS#11로 접근 |
| revocation | 키 폐기 | 예비 키 슬롯으로 넘어가고 옛 키를 fuse로 무효화 |
| HAB / AHAB | NXP 부트 ROM 검증 | SRK 해시 fuse, CST 서명 |

---

## 16. 요약 & 체크리스트

secure boot는 "각 단계가 다음 단계를 실행 전에 검증하고, 맨 앞 키는 바꿀 수 없는 곳에 있다"는 하나의 원리다. Linux SoC에서는 ROM(fuse 키 해시) → TF-A TBB(X.509 인증서, NV counter) → U-Boot(control DTB 속 FIT 공개키, configuration 서명) → 커널(서명된 DT 속 cmdline, 서명 모듈, dm-verity)로 이어진다. 이 노트에서 QEMU로 그 체인을 실제로 만들고, 1바이트 변조·남의 키·서명 없음·mix-and-match·공개키 바꿔치기가 모두 막히는 것과, 환경변수·`booti`·프롬프트 같은 "검증 옆길"이 체인을 끊는 것을 확인했다. 진짜 어려운 것은 운영이다 — 키 계층과 HSM, 유출 시 교체 순서, 롤백 카운터와 OTA의 순서, fuse 공정의 검증 단계, JTAG 잠금.

- [ ] secure boot가 막는 것과 못 막는 것(롤백, 런타임, JTAG, 기밀성)을 위협 모델 표로 말할 수 있다
- [ ] `mkimage -k -K -r`이 각각 무엇을 하는지, control DTB `/signature/key-dev`의 속성을 설명할 수 있다
- [ ] configuration 서명이 무엇을 덮고(hashed-nodes, hash 값) 무엇을 빼는지(data) 설명할 수 있다
- [ ] mix-and-match 공격을 재현하고 configuration 서명이 막는 이유를 말할 수 있다
- [ ] qemu_arm64에서 공개키를 U-Boot 바이너리에 넣기 위해 `OF_BOARD`·`OF_OMIT_DTB`를 끄고 `EXT_DTB`를 쓴 이유를 설명할 수 있다
- [ ] TF-A BL2가 "공개키가 든 U-Boot"의 해시를 검증해 체인이 이어지는 것을 인증서 OID로 보일 수 있다
- [ ] U-Boot 양산 잠금 설정(BOOTDELAY, ENV, 부트 명령, 실패 경로, env 화이트리스트)을 나열할 수 있다
- [ ] 모듈 서명의 세 정책(허용+taint, sig_enforce, lockdown)의 결과와 에러 코드를 말할 수 있다
- [ ] secure boot와 measured boot의 차이, PCR extend 공식을 설명할 수 있다
- [ ] 키 계층을 그리고, 어느 키가 새느냐에 따른 대응 순서를 말할 수 있다
- [ ] 롤백 카운터를 언제 올려야 A/B OTA와 충돌하지 않는지 설명할 수 있다
- [ ] fuse 공정에서 lock 전에 무엇을 확인해야 하는지 말할 수 있다

## 참고 자료

- U-Boot 문서 (소스 트리 `doc/usage/fit/`): `signature.rst`(키 노드 속성, `required`, `required-mode`, PKCS#11), `verified-boot.rst`, `sign-configs.rst`, `sign-images.rst`, `beaglebone_vboot.rst`(`EXT_DTB`로 공개키 DTB 넣기) — 온라인: https://docs.u-boot.org/en/latest/usage/fit/signature.html
- U-Boot 문서 `doc/develop/devicetree/dt_qemu.rst` (QEMU DT 덤프·병합), `doc/usage/measured_boot.rst`
- U-Boot 소스: `boot/image-fit-sig.c`(`fit_config_check_sig`, `exc_prop[]`, `fit_config_verify_required_keys`), `boot/fdt_support.c`(`fdt_chosen`, `board_fdt_chosen_bootargs`), `scripts/check-of.sh`
- TF-A 문서: Trusted Board Boot 설계 `docs/design/trusted-board-boot.rst`, 빌드 `docs/design/trusted-board-boot-build.rst` — https://trustedfirmware-a.readthedocs.io/
- 커널 문서: Kernel module signing facility — https://docs.kernel.org/admin-guide/module-signing.html , `man 7 kernel_lockdown` (man7.org)
- 커널 소스: `kernel/module/signing.c`, `certs/system_keyring.c`, `scripts/sign-file.c` (elixir.bootlin.com)
- Mbed TLS 3.6.6 릴리스: https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.6
- TPM: tpm2-tools 문서 https://tpm2-tools.readthedocs.io/ , swtpm https://github.com/stefanberger/swtpm
- 이 노트와 이어지는 노트: C3(FIT 기초), C5 §7(TBBR·NV counter), J3(dm-verity 입문), K7(taint·GPLv3), L4(OTA 서명·A/B), L6(dm-verity·TPM/TEE·IMA)
