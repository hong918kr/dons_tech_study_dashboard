# M9. 유저스페이스 디버깅 — gdb·gdbserver 원격 디버깅, core dump, strace·ltrace, valgrind·sanitizer

> **이 노트를 다 읽으면**: 보드에서 gdbserver를 띄우고 PC의 gdb로 stripped 바이너리를 sysroot·debug 파일과 함께 디버깅할 수 있다 · build-id로 core dump와 debug 정보를 맞춰 PIE 주소를 소스 줄로 바꿀 수 있다 · strace·ltrace로 "설정 파일이 없다", "fsync가 느리다"를 1분 안에 찾고 오버헤드를 숫자로 말할 수 있다 · valgrind와 ASan/TSan 중 무엇을 언제 쓸지 실측 비용으로 고를 수 있다
> **왜 필요한가**: "타깃에서만 죽는다", "며칠 지나면 메모리가 는다", "가끔 멈춘다"는 Embedded Linux 앱 팀이 매주 받는 버그 리포트이고, 면접에서도 "App crashes only on the target — how do you debug?"가 단골이다
> **Don 기준 난이도**: MCU에서 JTAG + gdb(breakpoint·watchpoint·bt)는 손에 익었다 / 새로 배울 것은 "OS 위의 한 프로세스만 멈추는" ptrace 모델, 원격 sysroot, build-id, 그리고 ISS처럼 도는 valgrind와 컴파일러 계측(sanitizer)
> **선행 노트**: A2(프로세스·스레드), A6 §8(시그널과 core dump 기초), A8(유저스페이스 동기화), B1(sysroot), B3 §3(PIE·ASLR)·§6(PLT/GOT)·§7.3(LD_PRELOAD)·§9.3(debuglink·Build ID), L1(systemd), L7 §7·§9(coredump 수집 파이프라인·symbol server). 같은 모듈의 M3(커널 Oops 해독)·M4(커널 디버거)·M6(perf)·M7(eBPF/uprobe)과 짝이다

---

**실행 환경** (이 노트의 모든 출력은 실제 실행 결과다):

- **VM 호스트** = "개발 PC" 역할: Lima Ubuntu 26.04 aarch64, kernel `7.0.0-38-generic`, gdb 17.1, valgrind 3.26.0, strace 6.19, gcc 15, glibc 2.43.
- **QEMU 게스트** = "보드" 역할: 공용 랩의 `rootfs-trace`(Ubuntu Base 24.04.5 arm64 + systemd 255) 복사본, `-cpu cortex-a57,pmu=on -smp 2 -m 1G`. 커널은 §1~§6과 §7의 memcheck·ASan까지 `6.18.55-sec`, §7의 helgrind·TSan·massif와 §8·§9는 `6.18.55-trace`(뒤늦게 빌드 완료). 게스트 도구: gcc 13.3, gdb 15.1, gdbserver, valgrind 3.22.0, strace 6.8, ltrace 0.7.3, glibc 2.39.
- 게스트는 KVM 없이 **TCG(명령어 번역 에뮬레이션)** 로 돌고, VM은 17명의 작성자가 동시에 쓰는 중이라 load average가 8~24였다. **게스트에서 잰 시간은 실제 보드 숫자가 아니다.** 비율(몇 배 느려지나)도 TCG 때문에 왜곡된다. 그래서 비용 비교는 VM 호스트(실제 Apple M2 코어 위의 Linux)에서 다시 쟀다.
- 게스트↔호스트 연결: QEMU user 네트워크 `hostfwd=tcp:127.0.0.1:2391-:2391`(gdbserver), `:2392`(gdbserver --multi), 그리고 virtio-9p 공유 디렉터리(게스트 `/mnt/m9` = 호스트 `/var/tmp/M9/share`). 콘솔은 시리얼을 unix 소켓으로 빼서 스크립트로 명령을 보냈다.

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어에서 버그를 잡을 때 Don의 도구는 셋이었다: **JTAG로 코어를 멈추고 들여다보기**, **assert 때 SRAM에 남긴 덤프를 나중에 읽기**, **UART 로그·트레이스 버퍼로 흐름 보기**. Embedded Linux의 유저스페이스에도 정확히 같은 세 가지가 있다. 다만 "코어 전체"가 아니라 **프로세스 하나**가 단위다.

| 펌웨어 세계 | Linux 유저스페이스 | 이 노트 |
|---|---|---|
| JTAG halt + gdb | `ptrace` 기반 gdb / gdbserver — 그 프로세스(또는 스레드)만 멈추고 커널·다른 프로세스는 계속 돈다 | §2, §3 |
| assert 덤프를 SRAM에 → RMA 후 분석 | core dump (프로세스 메모리 + 레지스터 ELF) → 같은 빌드의 debug 정보로 분석 | §4 |
| UART 로그, 버스 트레이스 | strace(syscall 경계), ltrace(라이브러리 경계) | §5, §6 |
| ISS(명령어 시뮬레이터)에서 돌려 보기 | valgrind — 바이너리를 JIT로 번역하면서 모든 load/store를 검사 | §7 |
| MPU 영역을 촘촘히 걸어 넘침 잡기 | AddressSanitizer — 컴파일러가 모든 load/store 앞에 shadow 메모리 검사를 심는다 | §7 |

증상을 보고 도구를 고르는 순서는 그림 1과 같다. 첫 질문은 "죽었나, 멈췄나, 느린가, 새나"이고, 두 번째 질문은 "재현이 되나, 현장에서 한 번뿐인가"다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="250" y="10" width="180" height="36" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="33" font-size="13" text-anchor="middle">증상은 무엇인가?</text>
<line x1="340" y1="46" x2="90" y2="80" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="340" y1="46" x2="255" y2="80" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="340" y1="46" x2="425" y2="80" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="340" y1="46" x2="590" y2="80" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="20" y="82" width="140" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="90" y="105" font-size="13" text-anchor="middle">죽는다 (SIGSEGV)</text>
<rect x="185" y="82" width="140" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="255" y="105" font-size="13" text-anchor="middle">멈춘다 (hang)</text>
<rect x="355" y="82" width="140" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="425" y="105" font-size="13" text-anchor="middle">틀리다 · 느리다</text>
<rect x="520" y="82" width="140" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="590" y="105" font-size="13" text-anchor="middle">메모리가 는다</text>
<rect x="20" y="140" width="140" height="80" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="90" y="160" font-size="12" text-anchor="middle">현장 1회: core dump</text>
<text x="90" y="178" font-size="12" text-anchor="middle">+ build-id 맞춘</text>
<text x="90" y="196" font-size="12" text-anchor="middle">debug 파일 (§4)</text>
<text x="90" y="214" font-size="12" text-anchor="middle">coredumpctl gdb</text>
<rect x="20" y="236" width="140" height="80" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="90" y="256" font-size="12" text-anchor="middle">재현됨: gdbserver</text>
<text x="90" y="274" font-size="12" text-anchor="middle">+ watchpoint (§3)</text>
<text x="90" y="292" font-size="12" text-anchor="middle">메모리 오염이면</text>
<text x="90" y="310" font-size="12" text-anchor="middle">ASan · memcheck</text>
<rect x="185" y="140" width="140" height="80" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="255" y="160" font-size="12" text-anchor="middle">gdb -p -batch</text>
<text x="255" y="178" font-size="12" text-anchor="middle">thread apply all bt</text>
<text x="255" y="196" font-size="12" text-anchor="middle">task/TID/stack</text>
<text x="255" y="214" font-size="12" text-anchor="middle">wchan (예제 5)</text>
<rect x="185" y="236" width="140" height="80" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="255" y="256" font-size="12" text-anchor="middle">syscall에서 막힘:</text>
<text x="255" y="274" font-size="12" text-anchor="middle">strace -p (§5)</text>
<text x="255" y="292" font-size="12" text-anchor="middle">레이스 의심:</text>
<text x="255" y="310" font-size="12" text-anchor="middle">TSan · helgrind</text>
<rect x="355" y="140" width="140" height="80" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="425" y="160" font-size="12" text-anchor="middle">파일·권한·설정:</text>
<text x="425" y="178" font-size="12" text-anchor="middle">strace -Z %file</text>
<text x="425" y="196" font-size="12" text-anchor="middle">느린 syscall:</text>
<text x="425" y="214" font-size="12" text-anchor="middle">strace -T, -c</text>
<rect x="355" y="236" width="140" height="80" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="425" y="256" font-size="12" text-anchor="middle">라이브러리 호출:</text>
<text x="425" y="274" font-size="12" text-anchor="middle">ltrace (§6)</text>
<text x="425" y="292" font-size="12" text-anchor="middle">CPU 핫스팟:</text>
<text x="425" y="310" font-size="12" text-anchor="middle">perf (M6)</text>
<rect x="520" y="140" width="140" height="80" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="590" y="160" font-size="12" text-anchor="middle">현장: smaps_rollup</text>
<text x="590" y="178" font-size="12" text-anchor="middle">주기 기록, gcore</text>
<text x="590" y="196" font-size="12" text-anchor="middle">(§8)</text>
<rect x="520" y="236" width="140" height="80" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="590" y="256" font-size="12" text-anchor="middle">벤치: massif</text>
<text x="590" y="274" font-size="12" text-anchor="middle">memcheck 누수</text>
<text x="590" y="292" font-size="12" text-anchor="middle">LSan (-fsanitize</text>
<text x="590" y="310" font-size="12" text-anchor="middle">=address)</text>
<line x1="90" y1="220" x2="90" y2="236" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="255" y1="220" x2="255" y2="236" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="425" y1="220" x2="425" y2="236" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="590" y1="220" x2="590" y2="236" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="20" y="340" width="640" height="76" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="362" font-size="13" text-anchor="middle">공통 전제: 같은 빌드의 debug 정보 (build-id) · sysroot · 소스 경로</text>
<text x="340" y="384" font-size="12" text-anchor="middle">이게 없으면 모든 도구가 "?? ()" 와 16진수 주소만 보여 준다 (§1)</text>
<text x="340" y="404" font-size="12" text-anchor="middle">위쪽 = 비용 작음 · 현장에서도 가능, 아래쪽 = 재현 환경 · 큰 오버헤드</text>
</svg>
```
그림 1 — 증상별 도구 고르기. 각 열의 위 상자가 먼저 쓰는 가벼운 도구, 아래 상자가 재현 환경에서 쓰는 무거운 도구다. 맨 아래 띠는 모든 도구의 전제 조건이다.

이 노트에서 쓰는 예제 프로그램(전부 직접 컴파일해서 돌렸다):

| 파일 | 역할 | 쓰는 절 |
|---|---|---|
| `sensord.c` | reader·filter 두 스레드가 링버퍼를 mutex+condvar로 공유하는 가짜 IMU 데몬 | §1, §2, §9 |
| `deadlock.c` | "I2C 버스 락"과 "설정 락"을 ABBA 순서로 잡는 두 스레드 | §3 |
| `needcfg.c` | 설정 파일이 없으면 조용히 10 Hz로 떨어지고, 레코드마다 fsync하는 데몬 | §3, §5 |
| `corrupt.c` | 이웃 필드의 overflow로 `calib_gain`이 바뀌는 프로그램 | §3 |
| `crash.c` | 드물게 오는 NULL payload 프레임에서 `strlen`이 죽는 파서 | §4 |
| `sysloop.c` | 값싼 syscall을 N번 부르고 호출당 ns를 재는 루프 | §5 |
| `ltr.c` | malloc/strcpy/strlen/printf/free 몇 번 | §6 |
| `bugs.c` | OOB write·UAF·초기화 안 된 읽기·누수 + 깨끗한 계산 루프(비용 측정용) | §7 |
| `race.c` | 락 없이 전역 카운터를 올리는 두 스레드 | §7 |
| `grow.c` | 지우지 않는 캐시 — 천천히 커지는 힙 | §7, §8 |
| `m9time.c` | `/usr/bin/time -v` 대용: 벽시계 시간 + 최대 RSS (게스트 이미지에 `time` 바이너리가 없다) | §7 |

---

## 1. 준비 — debug 정보, stripped 바이너리, build-id

### 1.1 직관 — "ELF와 .map 파일을 따로 보관한다"

펌웨어 시절에는 `.elf`(심볼 있음)를 빌드 서버에 남기고, 칩에는 `.bin`만 구웠다. 크래시 PC가 오면 그 빌드의 `.elf`/`.map`으로 함수를 찾았다. Linux도 똑같다. 보드에는 **stripped 바이너리**(심볼·DWARF 없음, 작다)를 올리고, PC나 서버에는 **debug 정보**를 따로 둔다. 둘을 짝짓는 지문이 **build-id**(링커가 내용 해시로 만들어 `.note.gnu.build-id`에 넣는 20바이트)다. 기본 원리와 `addr2line` 사용법은 B3 §9.3에서 했다. 여기서는 디버거가 이 짝을 **어떤 순서로 찾는지**에 집중한다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="200" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="44" font-size="13" text-anchor="middle">sensord.full</text>
<text x="120" y="64" font-size="12" text-anchor="middle">코드 + 심볼 + DWARF</text>
<line x1="220" y1="40" x2="280" y2="40" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="220" y1="64" x2="280" y2="110" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="282" y="16" width="190" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="377" y="37" font-size="13" text-anchor="middle">sensord (보드용)</text>
<text x="377" y="56" font-size="12" text-anchor="middle">코드 + build-id + debuglink</text>
<rect x="282" y="90" width="190" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="377" y="111" font-size="13" text-anchor="middle">sensord.debug (서버용)</text>
<text x="377" y="130" font-size="12" text-anchor="middle">DWARF + 같은 build-id</text>
<text x="20" y="162" font-size="12">objcopy --only-keep-debug · --strip-debug · --add-gnu-debuglink</text>
<rect x="20" y="180" width="640" height="164" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="40" y="204" font-size="13">gdb가 stripped 파일의 debug 정보를 찾는 순서 (debug-file-directory = D)</text>
<text x="40" y="230" font-size="12">1. D/.build-id/d1/a73d6a13...2377.debug   ← build-id 경로 규칙 (가장 확실)</text>
<text x="40" y="254" font-size="12">2. 실행 파일과 같은 디렉터리의 debuglink 이름 (sensord.debug, CRC 확인)</text>
<text x="40" y="278" font-size="12">3. 같은 디렉터리/.debug/ 아래, 그리고 D + 실행 파일의 절대 경로 아래</text>
<text x="40" y="302" font-size="12">4. debuginfod 서버 (DEBUGINFOD_URLS 가 있고 허락했을 때) — HTTP로 build-id 조회</text>
<text x="40" y="330" font-size="12">기본 D = /usr/lib/debug  →  배포판 -dbg / -dbgsym 패키지, Yocto -dbg 가 같은 규칙으로 깐다</text>
<line x1="472" y1="115" x2="560" y2="180" stroke="currentColor" marker-end="url(#ah2)"/>
</svg>
```
그림 2 — debug 정보 분리. 빌드 결과(파랑)를 보드용(주황)과 서버용(초록)으로 쪼개고, build-id가 둘을 잇는다. 아래 상자는 gdb가 debug 파일을 찾는 경로 순서다.

### 예제 1 — 게스트에서 빌드하고 쪼개기

무엇을 확인하나: 같은 빌드에서 나온 세 파일의 크기와 build-id, 그리고 stripped 파일에 무엇이 남는지.

```sh
gcc -std=gnu11 -Wall -Wextra -g -O1 -pthread /mnt/m9/src/sensord.c -o sensord.full
objcopy --only-keep-debug sensord.full sensord.debug
objcopy --strip-debug --strip-unneeded sensord.full sensord
objcopy --add-gnu-debuglink=sensord.debug sensord
ls -l sensord.full sensord sensord.debug
readelf -n sensord | grep "Build ID"; readelf -n sensord.debug | grep "Build ID"
readelf -S sensord.full | grep -c " .debug_"; readelf -S sensord | grep -E "debug|symtab"
cp sensord.full sensord.debug /mnt/m9/out/      # "PC"로 보낸다
```

```text
-rwxr-xr-x 1 root root 67600 Oct 18 06:20 sensord
-rwxr-xr-x 1 root root 12040 Oct 18 06:20 sensord.debug
-rwxr-xr-x 1 root root 76928 Oct 18 06:20 sensord.full
    Build ID: d1a73d6a13de03e19f11c7d2d8175708685f2377
readelf: Error: Unable to find program interpreter name
    Build ID: d1a73d6a13de03e19f11c7d2d8175708685f2377
8
  [25] .gnu_debuglink    PROGBITS         0000000000000000  00010040
```

출력에서 볼 것:

- build-id가 두 파일에서 같다. "이 debug 파일이 저 바이너리의 것"이라는 증거다.
- `sensord.debug`를 `readelf -n`할 때 나온 "Unable to find program interpreter name"은 정상이다. debug 파일은 섹션 헤더만 남고 코드 바이트(`.interp` 포함)는 비어 있는 NOBITS다.
- 보드용 파일에 `.debug_*`가 하나도 없고(원본은 8개), `.symtab`도 없다. 남은 것은 `.gnu_debuglink`(이름 + CRC)뿐이다.
- 67600 vs 76928: 이 예제는 작아서 차이가 작다(arm64 기본 64 KiB 세그먼트 정렬 패딩이 대부분). 실제 앱은 DWARF가 코드보다 3~10배 큰 경우가 흔해서, 보드 플래시에서 큰 차이가 난다.

### 1.2 무엇을 켜고 빌드하나

| 옵션 | 효과 | 보드에 올릴 때 |
|---|---|---|
| `-g` | DWARF 생성 (코드는 그대로 — `-g`는 생성 코드를 바꾸지 않는 것이 GCC의 원칙) | 분리해서 서버에 보관 |
| `-O0` vs `-O2` | `-O2`는 `<optimized out>`, inline으로 사라진 프레임이 생긴다 | 출시 빌드 그대로 디버깅하는 법을 익혀야 한다 |
| `-fno-omit-frame-pointer` | x29 프레임 체인 유지 → perf·sanitizer·간이 unwinder가 쉬워진다 | 비용이 작아 켜 두는 팀이 많다 |
| `-gsplit-dwarf` | `.dwo`로 debug 정보를 나눠 링크 시간 단축 | 큰 프로젝트용 |
| `--build-id` | GCC/ld 기본값(배포판 툴체인). Buildroot·Yocto도 켠다 | 반드시 켜져 있는지 확인 (`readelf -n`) |

Yocto는 `-dbg` 패키지(`/usr/lib/debug/.build-id/...`)와 `IMAGE_GEN_DEBUGFS`로 "debug용 rootfs"를 따로 만들고, Buildroot는 `BR2_ENABLE_DEBUG` + `BR2_STRIP_strip`로 `staging/`에는 심볼을, `target/`에는 stripped를 둔다(K3·K4 참고). 둘 다 그림 2의 구조를 자동화한 것이다.

> 함정: **같은 소스를 다시 빌드해도 build-id가 같다는 보장은 없다.** 컴파일러 버전·경로·`__DATE__`·링크 순서가 달라지면 해시가 바뀐다. "나중에 다시 빌드하면 되지"는 틀렸다. 출시 빌드의 debug 파일은 **빌드할 때 보관**해야 한다 (예제 12에서 직접 깨 본다).

---

## 2. 원격 디버깅 — gdbserver와 sysroot

### 2.1 구조 — "보드에는 작은 stub, PC에는 똑똑한 gdb"

JTAG 디버깅에서 OpenOCD가 하던 역할(타깃과 gdb 사이의 RSP 서버)을 **gdbserver**가 한다. 차이는 gdbserver가 **보드의 Linux 위에서 도는 평범한 프로세스**이고, `ptrace()`로 디버기 프로세스 하나를 조종한다는 점이다. 보드에는 심볼도 소스도 필요 없다. gdbserver는 레지스터·메모리 읽기/쓰기, breakpoint 삽입, 스레드 목록 정도만 한다. 심볼 해석·DWARF·소스 표시는 전부 PC의 gdb가 한다.

PC의 gdb가 알아야 하는 것은 세 가지다.

1. **실행 파일의 debug 정보**: `file sensord.full` (또는 stripped + `debug-file-directory`).
2. **보드의 공유 라이브러리**: 디버기는 보드의 `libc.so.6`·`ld-linux-aarch64.so.1`을 쓰므로 gdb도 **똑같은 파일**을 읽어야 한다. 이것이 `set sysroot <보드 rootfs 사본>`이다. 기본값 `target:`은 gdbserver에게 파일을 하나하나 받아 온다(느리지만 항상 맞다).
3. **소스 경로**: DWARF에는 빌드할 때의 경로가 박혀 있다. PC의 경로가 다르면 `set substitute-path <빌드 경로> <내 경로>`.

크로스 개발(x86 PC → arm 보드)에서는 PC의 gdb가 타깃 아키텍처를 알아야 하므로 `gdb-multiarch`(Debian/Ubuntu 패키지)나 툴체인의 `aarch64-linux-gnu-gdb`를 쓴다. 이 랩은 VM 호스트도 aarch64라 그냥 `gdb`를 썼지만, **sysroot 문제는 그대로 재현된다**: 호스트의 glibc는 2.43이고 게스트는 2.39이기 때문이다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="15" y="20" width="300" height="340" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165" y="42" font-size="13" text-anchor="middle">개발 PC (VM 호스트)</text>
<rect x="35" y="60" width="260" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165" y="82" font-size="13" text-anchor="middle">gdb (또는 gdb-multiarch)</text>
<text x="165" y="102" font-size="12" text-anchor="middle">심볼 · DWARF · 소스 · unwind</text>
<rect x="35" y="132" width="260" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="165" y="159" font-size="12" text-anchor="middle">file sensord.full (debug 빌드)</text>
<rect x="35" y="188" width="260" height="62" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="165" y="210" font-size="12" text-anchor="middle">set sysroot .../share/sysroot</text>
<text x="165" y="230" font-size="12" text-anchor="middle">보드 rootfs 의 libc · ld.so 사본</text>
<text x="165" y="245" font-size="12" text-anchor="middle">(+ /usr/lib/debug 사본)</text>
<rect x="35" y="262" width="260" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="165" y="289" font-size="12" text-anchor="middle">set substitute-path 빌드경로 내경로</text>
<text x="165" y="340" font-size="12" text-anchor="middle">glibc 2.43 — 보드와 다르다!</text>
<rect x="365" y="20" width="300" height="340" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="515" y="42" font-size="13" text-anchor="middle">보드 (QEMU 게스트)</text>
<rect x="385" y="60" width="260" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="515" y="82" font-size="13" text-anchor="middle">gdbserver :2391</text>
<text x="515" y="102" font-size="12" text-anchor="middle">레지스터 · 메모리 · BRK 삽입</text>
<rect x="385" y="160" width="260" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="515" y="182" font-size="13" text-anchor="middle">./sensord (stripped)</text>
<text x="515" y="202" font-size="12" text-anchor="middle">스레드 3개 · glibc 2.39</text>
<line x1="515" y1="116" x2="515" y2="158" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="525" y="142" font-size="12">ptrace()</text>
<rect x="385" y="240" width="260" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="515" y="267" font-size="12" text-anchor="middle">커널: 이 프로세스만 정지</text>
<line x1="295" y1="80" x2="383" y2="80" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="383" y1="96" x2="297" y2="96" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="340" y="72" font-size="12" text-anchor="middle">RSP</text>
<text x="340" y="124" font-size="12" text-anchor="middle">TCP 2391</text>
<text x="515" y="316" font-size="12" text-anchor="middle">심볼 · 소스 필요 없음</text>
<text x="515" y="340" font-size="12" text-anchor="middle">gdbserver 바이너리 ~0.5 MB</text>
</svg>
```
그림 3 — 원격 디버깅 토폴로지. 보드(주황)에는 stripped 실행 파일과 gdbserver만, PC(파랑)에는 debug 빌드·보드 rootfs 사본(sysroot)·소스가 있다. 둘은 TCP 위의 gdb Remote Serial Protocol(RSP)로 대화한다.

### 예제 2 — stripped 바이너리를 PC의 debug 빌드로 디버깅

무엇을 확인하나: 보드에서는 stripped `sensord`를 gdbserver로 띄우고, PC에서는 debug 빌드 + sysroot로 breakpoint·스레드·bt를 본다.

먼저 PC 쪽 sysroot를 만든다. 보드 rootfs에서 libc와 로더를 같은 경로 구조로 복사한다(Ubuntu는 `/lib → usr/lib` usrmerge라 링크도 재현).

```sh
# 게스트(보드): sysroot 재료를 공유 디렉터리로
mkdir -p /mnt/m9/sysroot/usr/lib/aarch64-linux-gnu
cp -L /usr/lib/aarch64-linux-gnu/libc.so.6 /usr/lib/aarch64-linux-gnu/libm.so.6 \
      /usr/lib/ld-linux-aarch64.so.1 /mnt/m9/sysroot/usr/lib/aarch64-linux-gnu/
cp -L /usr/lib/ld-linux-aarch64.so.1 /mnt/m9/sysroot/usr/lib/
ln -sfn usr/lib /mnt/m9/sysroot/lib
# 게스트(보드): stripped 바이너리를 gdbserver로 시작
cd /root/m9 && setsid nohup gdbserver :2391 ./sensord > gs1.log 2>&1 &
```

```text
Process ./sensord created; pid = 662
Listening on port 2391
```

```sh
# VM 호스트(PC)
gdb -q -batch -nx -ex "set debuginfod enabled off" \
  -ex "set sysroot /var/tmp/M9/share/sysroot" -ex "file share/out/sensord.full" \
  -ex "target remote 127.0.0.1:2391" -ex "info sharedlibrary" \
  -ex "break apply_filter" -ex "continue" -ex "bt" -ex "info threads" \
  -ex "thread apply all bt" -ex "p ring[0]" -ex "info proc mappings" -ex "kill"
```

```text
0x0000fffff7fd8a40 in ?? () from /var/tmp/M9/share/sysroot/lib/ld-linux-aarch64.so.1
From                To                  Syms Read   Shared Object Library
0x0000fffff7fb0000  0x0000fffff8000000  Yes (*)     /var/tmp/M9/share/sysroot/lib/ld-linux-aarch64.so.1
(*): Shared library is missing debugging information.
Breakpoint 1 at 0xaaaaaaaa0b84: file /mnt/m9/src/sensord.c, line 18.
[New Thread 662.665]
[New Thread 662.664]
[Switching to Thread 662.665]

Thread 2 "sensord" hit Breakpoint 1, 0x0000aaaaaaaa0b84 in apply_filter (raw=<optimized out>, prev=<optimized out>) at /mnt/m9/src/sensord.c:18
warning: 18	/mnt/m9/src/sensord.c: No such file or directory
#0  0x0000aaaaaaaa0b84 in apply_filter (raw=<optimized out>, prev=<optimized out>) at /mnt/m9/src/sensord.c:18
#1  filter (arg=<optimized out>) at /mnt/m9/src/sensord.c:44
#2  0x0000fffff7e7582c in ?? () from /var/tmp/M9/share/sysroot/lib/aarch64-linux-gnu/libc.so.6
#3  0x0000fffff7edbb4c in ?? () from /var/tmp/M9/share/sysroot/lib/aarch64-linux-gnu/libc.so.6
  Id   Target Id                Frame 
  1    Thread 662.662 "sensord" 0x0000fffff7edbb28 in ?? () from /var/tmp/M9/share/sysroot/lib/aarch64-linux-gnu/libc.so.6
* 2    Thread 662.665 "sensord" 0x0000aaaaaaaa0b84 in apply_filter (raw=<optimized out>, prev=<optimized out>) at /mnt/m9/src/sensord.c:18
  3    Thread 662.664 "sensord" 0x0000fffff7eabd90 in clock_nanosleep () from /var/tmp/M9/share/sysroot/lib/aarch64-linux-gnu/libc.so.6

Thread 3 (Thread 662.664 "sensord"):
#0  0x0000fffff7eabd90 in clock_nanosleep () from /var/tmp/M9/share/sysroot/lib/aarch64-linux-gnu/libc.so.6
#1  0x0000fffff7eb60bc in nanosleep () from /var/tmp/M9/share/sysroot/lib/aarch64-linux-gnu/libc.so.6
#2  0x0000fffff7edb5b8 in usleep () from /var/tmp/M9/share/sysroot/lib/aarch64-linux-gnu/libc.so.6
#3  0x0000aaaaaaaa0c60 in reader (arg=<optimized out>) at /mnt/m9/src/sensord.c:31
...
Thread 1 (Thread 662.662 "sensord"):
...
#4  0x0000fffff7e75e68 in pthread_create () from /var/tmp/M9/share/sysroot/lib/aarch64-linux-gnu/libc.so.6
#5  0x0000aaaaaaaa0d2c in main () at /mnt/m9/src/sensord.c:55
$1 = {seq = 0, raw = 1000, filtered = 0}
...
0x0000aaaaaaaa0000 0x0000aaaaaaaa1000 0x1000             0x0                r-xp  /root/m9/sensord 
...
0x0000fffff7df0000 0x0000fffff7f8a000 0x19a000           0x0                r-xp  /usr/lib/aarch64-linux-gnu/libc.so.6 
...
[Inferior 1 (process 662) killed]

real	0m1.461s
```
(Thread 2·1의 libc 프레임과 `info proc mappings`의 나머지 줄은 `...`로 줄였다.)

출력에서 볼 것:

- **breakpoint는 보드의 stripped 바이너리에 걸렸는데, 함수 이름·줄 번호·스레드 이름이 다 나온다.** 심볼은 전부 PC의 `sensord.full`에서 왔다. 보드에는 심볼이 한 바이트도 없다.
- **처음 멈춘 곳은 `ld-linux`** 다. gdbserver는 프로그램을 `exec` 직후, 동적 로더의 첫 명령에서 멈춘 채로 기다린다. `main`은 아직 안 돌았다.
- libc 프레임이 `?? ()`다. sysroot에는 libc **코드**만 복사했고, libc의 **debug 정보**는 복사하지 않았다(`Syms Read`의 `(*)`). 보드에 `/usr/lib/debug/.build-id/...`가 있으면(이 rootfs는 valgrind 의존성으로 libc6-dbg가 깔려 있다) 그것도 sysroot에 복사하거나 `set debug-file-directory`로 가리켜야 libc 안까지 이름이 나온다. 예제 4에서 `target:`을 쓰면 이 파일까지 자동으로 받아 오는 것을 본다.
- `sensord.c: No such file or directory` — DWARF에 박힌 빌드 경로 `/mnt/m9/src`가 PC에는 없다. 예제 4에서 `substitute-path`로 고친다.
- `info proc mappings`의 실행 파일이 `0xaaaaaaaa0000`, libc가 `0xfffff7df0000` — **ASLR이 꺼져 있다.** gdbserver(와 gdb)는 기본으로 자기가 띄운 프로세스의 ASLR을 끈다(`--no-disable-randomization` / `set disable-randomization off`로 켤 수 있다). 디버깅은 편해지지만, "ASLR 때문에만 나는 버그"는 이렇게 띄우면 재현이 안 된다.
- `filter`가 `apply_filter`를 inline했다(`#0`과 `#1`이 같은 주소). `-O1`이라서 `raw`, `prev`는 `<optimized out>`이다. 출시 빌드 디버깅은 늘 이렇다.

### 예제 3 — sysroot를 틀리면

무엇을 확인하나: PC 자신의 `/`(glibc 2.43)를 sysroot로 주면 어떻게 깨지나.

```sh
gdb -q -batch -nx -ex "set debuginfod enabled off" -ex "set sysroot /" \
  -ex "file share/out/sensord.full" -ex "target remote 127.0.0.1:2391" \
  -ex "break reader" -ex "continue" -ex "info sharedlibrary" -ex "bt" -ex "kill"
```

```text
0x0000fffff7fd8a40 in process_envvars_default (state=0x120) at ./elf/rtld.c:2726
...
Thread 2 "sensord" hit Breakpoint 1, reader (arg=0x0) at /mnt/m9/src/sensord.c:22
warning: 22	/mnt/m9/src/sensord.c: No such file or directory
warning: .dynamic section for "/lib/aarch64-linux-gnu/libc.so.6" is not at the expected address (wrong library or version mismatch?)
From                To                  Syms Read   Shared Object Library
0x0000fffff7fb0000  0x0000fffff8000000  Yes         /lib/ld-linux-aarch64.so.1
0x0000fffff7de0000  0x0000fffff7fb0000  No          /lib/aarch64-linux-gnu/libc.so.6
#0  reader (arg=0x0) at /mnt/m9/src/sensord.c:22
#1  0x0000fffff7e7582c in ?? () from /lib/aarch64-linux-gnu/libc.so.6
#2  0x0000fffff7def1a0 in ?? () from /lib/aarch64-linux-gnu/libc.so.6
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
```

출력에서 볼 것:

- 첫 줄의 `process_envvars_default ... rtld.c:2726`은 **거짓말**이다. PC의 ld.so(2.43) 심볼로 보드의 ld.so(2.39) 주소를 해석해서 엉뚱한 함수 이름이 붙었다. 경고조차 없다.
- libc에서는 gdb가 `.dynamic section ... wrong library or version mismatch?`라고 경고하고 심볼을 포기했다(`Syms Read No`). 그 결과 **unwind가 망가져** "corrupt stack?"으로 끝났다. 진짜 스택은 멀쩡하다.
- 실무에서 "원격 bt가 이상하게 끊긴다", "libc 함수 이름이 이상하다"면 **코드보다 sysroot를 먼저 의심**한다. 보드 rootfs와 같은 빌드의 sysroot(Yocto SDK의 `sysroots/<machine>`, Buildroot의 `staging/`)를 써야 한다.

### 예제 4 — 이미 돌고 있는 데몬에 붙기: `--multi`, `attach`, `target:` sysroot, 조건부 breakpoint

무엇을 확인하나: 보드에서 평소처럼 실행 중인 데몬(ASLR 켜짐)에 PC에서 붙는다. sysroot는 기본값 `target:`으로 두고, 소스 경로를 고치고, 조건부 breakpoint를 건다.

```sh
# 게스트(보드): 데몬은 평소처럼, gdbserver는 대기 모드로
cd /root/m9; setsid nohup ./sensord > s.log 2>&1 &
setsid nohup gdbserver --multi :2392 > gs2.log 2>&1 &
pidof sensord                                  # → 675
# VM 호스트(PC)
gdb -q -batch -nx -ex "set debuginfod enabled off" -ex "target extended-remote 127.0.0.1:2392" \
  -ex "show sysroot" -ex "file share/out/sensord.full" \
  -ex "set substitute-path /mnt/m9/src /var/tmp/M9/share/src" -ex "attach 675" \
  -ex "info sharedlibrary" -ex "break sensord.c:45 if y > 1050" -ex "continue" \
  -ex "list 42,46" -ex "info locals" -ex "p ring" -ex "detach" -ex "monitor exit"
```

```text
The current system root is "target:".
Reading /lib/aarch64-linux-gnu/libc.so.6 from remote target...
warning: File transfers from remote targets can be slow. Use "set sysroot" to access files locally instead.
Reading /lib/ld-linux-aarch64.so.1 from remote target...
Reading /usr/lib/debug/.build-id/27/027b96e5b8c475fc327aa445bea1c71d37b4e2.debug from remote target...
...
[New Thread 675.679]
[New Thread 675.680]
0x0000ffffa6681d6c in __futex_abstimed_wait_common64 (private=128, cancel=true, abstime=0x0, op=265, expected=679, futex_word=0xffffa65ff270) at ./nptl/futex-internal.c:57
From                To                  Syms Read   Shared Object Library
0x0000ffffa6600000  0x0000ffffa67c0000  Yes         target:/lib/aarch64-linux-gnu/libc.so.6
0x0000ffffa67c0000  0x0000ffffa6810000  Yes         target:/lib/ld-linux-aarch64.so.1
Breakpoint 1 at 0xaaaab8690bc8: file /mnt/m9/src/sensord.c, line 45.
[Switching to Thread 675.680]

Thread 3 "sensord" hit Breakpoint 1, filter (arg=<optimized out>) at /mnt/m9/src/sensord.c:45
45	        ring[i].filtered = y;
42	        int i = (head - count + RING) % RING;
43	        count--;
44	        y = apply_filter(y, ring[i].raw);
45	        ring[i].filtered = y;
46	        pthread_mutex_unlock(&mu);
i = <optimized out>
y = 1059.505248410796
$1 = {{seq = 88, raw = 1056, filtered = 1048.3403312143946}, {seq = 89, raw = 1093, filtered = 1061.0013656226085}, ...}
[Inferior 1 (process 675) detached]

real	0m3.866s
```
(같은 build-id debug 파일 읽기 줄 반복과 `ring`의 나머지 원소는 생략했다.)

출력에서 볼 것:

- `target:` sysroot는 libc·ld.so를 **gdbserver에게서 받아 왔고**, 보드의 `/usr/lib/debug/.build-id/27/...`까지 찾아 받았다. 그래서 이번에는 libc 프레임이 `__futex_abstimed_wait_common64 ... futex-internal.c:57`처럼 이름이 나온다. 대신 시간이 예제 2(1.5 s)의 2.5배(3.9 s)였다. 이 정도 프로그램에서도 그렇고, 라이브러리 수십 개짜리 앱(Qt, GStreamer, ROS 2)은 몇 분이 걸린다. 그래서 gdb가 직접 "Use set sysroot"라고 권한다.
- 주소가 `0xaaaab869...`, `0xffffa66...` — 평소처럼 띄운 프로세스라 **ASLR이 켜진** 주소다. 예제 2와 비교해 보자.
- `break sensord.c:45 if y > 1050`: **조건 검사는 gdb(PC)가 한다.** 조건이 거짓이어도 breakpoint 때마다 프로세스가 멈추고 → 네트워크로 PC에 알리고 → PC가 조건을 보고 → 다시 진행시킨다. 초당 수천 번 지나는 곳이면 데몬이 눈에 띄게 느려진다(타이밍 버그가 사라지는 원인). `ignore N`, `tbreak`, 또는 gdbserver 쪽에서 평가하는 `set breakpoint condition-evaluation target`(지원되면)을 생각한다.
- 마지막의 `detach`로 데몬은 **계속 돈다**. 운영 중인 장비에서 "붙었다 떨어지기"가 가능하다는 것이 JTAG halt와 다른 점이다(물론 멈춰 있는 동안 watchdog·타이머는 그대로 흐른다 — §9).

### 2.2 gdbserver 사용 형태 정리

| 형태 | 보드 명령 | PC 명령 | 언제 |
|---|---|---|---|
| 띄우면서 | `gdbserver :2391 ./app args` | `target remote ip:2391` | 시작부터 보고 싶을 때. 끝나면 gdbserver도 끝난다 |
| 붙기 | `gdbserver --attach :2391 PID` | `target remote ip:2391` | 이미 도는 데몬 |
| 대기 모드 | `gdbserver --multi :2392` | `target extended-remote ip:2392` 후 `attach PID` 또는 `set remote exec-file /path` + `run` | 여러 번 붙었다 떨어지기, 재시작 반복 |
| 시리얼 | `gdbserver /dev/ttyS1 ./app` | `target remote /dev/ttyUSB0` | 네트워크가 없는 bring-up 보드 |

보안 주의: gdbserver는 **인증이 없다.** 포트를 연 순간 그 네트워크의 누구든 그 프로세스 권한으로 임의 코드를 실행할 수 있다. 랩처럼 `127.0.0.1`에만 열거나, 보드에서는 `gdbserver localhost:2391`로 열고 PC에서 `ssh -L 2391:localhost:2391 board`로 터널을 판다. 출시 이미지에는 gdbserver를 넣지 않는다. 그리고 Yama `ptrace_scope`(L6)가 1 이상이면 root가 아닌 사용자의 attach는 막힌다.

---

## 3. Linux에서 gdb — MCU와 다른 것들

MCU의 gdb와 명령은 같지만, OS가 있어서 생기는 것들이 있다: 스레드, 시그널, fork/exec, syscall, 그리고 페이지 단위 메모리.

### 3.1 스레드

- `info threads`: LWP(커널 스레드 ID)와 스레드 이름(`pthread_setname_np`/`prctl(PR_SET_NAME)`으로 붙인 이름 — 데몬은 스레드마다 이름을 붙이자).
- `thread N`, `thread apply all bt`, `thread apply all bt full`(지역 변수까지).
- 기본(all-stop) 모드에서는 **한 스레드가 멈추면 gdb가 모든 스레드를 멈춘다.** `set scheduler-locking step`이면 `step`하는 동안 다른 스레드는 안 돈다(레이스 재현이 사라질 수 있다).
- `set non-stop on`(attach 전에 설정): 멈춘 스레드만 멈추고 나머지는 계속 돈다. 오디오·제어 루프 스레드가 멈추면 안 되는 장비에서 쓴다. 명령 뒤에 `&`를 붙여 비동기로 쓴다(`continue &`, `interrupt`).

### 예제 5 — 멈춘 데몬: pstack 방식으로 모든 스레드 스택 뜨기

무엇을 확인하나: ABBA 데드락에 빠진 프로세스를 커널 쪽(`/proc`)과 유저 쪽(gdb)에서 동시에 본다.

```c
/* m9 deadlock: ABBA lock order between "i2c bus" and "config" locks */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pthread_mutex_t bus_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;

static void *poll_thread(void *arg)          /* takes bus -> cfg */
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&bus_lock);
        usleep(1000);                        /* "I2C transfer" */
        pthread_mutex_lock(&cfg_lock);       /* read calibration */
        pthread_mutex_unlock(&cfg_lock);
        pthread_mutex_unlock(&bus_lock);
    }
    return NULL;
}

static void *reload_thread(void *arg)        /* takes cfg -> bus (wrong order) */
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&cfg_lock);
        usleep(1000);                        /* parse new config */
        pthread_mutex_lock(&bus_lock);       /* re-program the sensor */
        pthread_mutex_unlock(&bus_lock);
        pthread_mutex_unlock(&cfg_lock);
    }
    return NULL;
}

int main(void)
{
    pthread_t a, b;
    printf("deadlock pid %d\n", getpid());
    fflush(stdout);
    pthread_create(&a, NULL, poll_thread, NULL);
    pthread_create(&b, NULL, reload_thread, NULL);
    pthread_join(a, NULL);
    return 0;
}
```

```sh
./deadlock > dl.log 2>&1 &  ; sleep 3; P=$(pidof deadlock)
ps -L -o pid,lwp,stat,wchan:20,pcpu,comm -p $P
for t in /proc/$P/task/*; do echo "== $t"; cat $t/stack; done
time gdb -q -batch -nx -p $P -ex "set debuginfod enabled off" -ex "thread apply all bt" -ex "p bus_lock" -ex "p cfg_lock"
```

```text
    PID     LWP STAT WCHAN                %CPU COMMAND
    687     687 Ssl  futex_do_wait         3.3 deadlock
    687     689 Ssl  futex_do_wait         0.0 deadlock
    687     690 Ssl  futex_do_wait         0.0 deadlock
== /proc/687/task/687
[<0>] futex_do_wait+0x44/0x8c
[<0>] __futex_wait+0x98/0x108
[<0>] futex_wait+0x84/0x118
[<0>] do_futex+0x114/0x1a0
[<0>] __arm64_sys_futex+0xfc/0x1c4
[<0>] invoke_syscall.constprop.0+0x40/0xe0
[<0>] el0_svc_common.constprop.0+0x38/0xe0
[<0>] do_el0_svc+0x1c/0x28
[<0>] el0_svc+0x38/0x118
[<0>] el0t_64_sync_handler+0xa0/0xe4
[<0>] el0t_64_sync+0x198/0x19c
== /proc/687/task/689
...(같은 futex 스택)
== /proc/687/task/690
...(같은 futex 스택)

Thread 3 (Thread 0xffffbb4ef1a0 (LWP 689) "deadlock"):
#0  futex_wait (private=0, expected=2, futex_word=0xaaaacda90018 <cfg_lock>) at ../sysdeps/nptl/futex-internal.h:146
#1  __GI___lll_lock_wait (futex=futex@entry=0xaaaacda90018 <cfg_lock>, private=private@entry=0) at ./nptl/lowlevellock.c:49
#2  0x0000ffffbb578b44 in lll_mutex_lock_optimized (mutex=0xaaaacda90018 <cfg_lock>) at ./nptl/pthread_mutex_lock.c:48
#3  ___pthread_mutex_lock (mutex=mutex@entry=0xaaaacda90018 <cfg_lock>) at ./nptl/pthread_mutex_lock.c:93
#4  0x0000aaaacda70b1c in poll_thread (arg=<optimized out>) at /mnt/m9/src/deadlock.c:15
#5  0x0000ffffbb57582c in start_thread (arg=0xffffbb6ee620) at ./nptl/pthread_create.c:447
#6  0x0000ffffbb5dbb4c in thread_start () at ../sysdeps/unix/sysv/linux/aarch64/clone3.S:76

Thread 2 (Thread 0xffffbacdf1a0 (LWP 690) "deadlock"):
#0  futex_wait (private=0, expected=2, futex_word=0xaaaacda90048 <bus_lock>) at ../sysdeps/nptl/futex-internal.h:146
...
#4  0x0000aaaacda70ad0 in reload_thread (arg=<optimized out>) at /mnt/m9/src/deadlock.c:28
...
Thread 1 (Thread 0xffffbb6edee0 (LWP 687) "deadlock"):
...
#4  0x0000ffffbb57707c in ___pthread_join (threadid=<optimized out>, thread_return=thread_return@entry=0x0) at ./nptl/pthread_join.c:24
#5  0x0000aaaacda70bb4 in main () at /mnt/m9/src/deadlock.c:42
$1 = {__data = {__lock = 2, __count = 0, __owner = 689, __nusers = 1, __kind = 0, __spins = 0, __list = {__prev = 0x0, __next = 0x0}}, __size = "...", __align = 2}
$2 = {__data = {__lock = 2, __count = 0, __owner = 690, __nusers = 1, __kind = 0, __spins = 0, __list = {__prev = 0x0, __next = 0x0}}, __size = "...", __align = 2}
[Inferior 1 (process 687) detached]

real	0m10.169s
```
(gdb 시작 안내문, 반복되는 스택 프레임, `__size` 바이트열은 생략했다. 게스트 TCG라 gdb 한 번에 10초가 걸렸다.)

출력에서 볼 것:

- **커널 쪽**: 세 스레드 모두 `wchan = futex_do_wait`, `%CPU 0` — 바쁘게 도는 것(livelock, 무한 루프)이 아니라 **잠들어서 안 깨어나는 것**이다. 커널 스택은 `el0t_64_sync`(유저의 SVC 예외) → `__arm64_sys_futex` → `futex_wait`. 이 정도면 "유저 락 대기"라는 것까지는 루트 권한 셸만으로 안다.
- **유저 쪽**: LWP 689(`poll_thread`, deadlock.c:15)는 `cfg_lock`을 기다리고, LWP 690(`reload_thread`, :28)은 `bus_lock`을 기다린다.
- 결정적 증거는 mutex 내부의 `__owner`: `bus_lock`의 주인은 689, `cfg_lock`의 주인은 690. **689 → cfg_lock(주인 690) → bus_lock(주인 689)** 로 순환이 닫힌다. glibc의 일반 mutex는 owner TID를 기록하므로 이 방법이 통한다(`__owner`는 glibc 내부 필드라 버전에 따라 이름이 다를 수 있다).
- `gdb -p ... -batch -ex "thread apply all bt"`가 옛날 `pstack`/`gstack` 스크립트가 하던 일이다. 현장에서 "멈췄다" 리포트가 오면 **죽이기 전에 이것부터 뜬다.** gdb가 없는 보드라면 `gcore`나 `kill -ABRT`로 core를 남기고 PC에서 분석한다(§4).
- 고치는 법은 락 순서 규칙(항상 bus → cfg)이다. 커널에서는 lockdep(M8)이 이 순환을 실행 중에 자동으로 잡는다. 유저스페이스의 대응물은 helgrind(`--tool=helgrind`의 lock order 검사)와 TSan의 `lock-order-inversion` 보고다.

### 3.2 시그널 — gdb가 먼저 받는다

ptrace 중인 프로세스에 시그널이 오면 **커널은 디버기를 멈추고 gdb에게 먼저 알린다.** gdb는 시그널마다 정책이 있다(`info signals`).

| 상황 | 명령 |
|---|---|
| 소켓 데몬이 끊긴 연결에 write → SIGPIPE마다 멈춰서 귀찮다 | `handle SIGPIPE nostop noprint pass` |
| 앱이 SIGUSR1로 설정 재로드 — 멈추지 말고 전달만 | `handle SIGUSR1 nostop pass` |
| SIGSEGV를 일부러 쓰는 런타임(JVM, 일부 GC) | `handle SIGSEGV nostop noprint` (주의해서) |
| SIGINT를 앱에 보내고 싶다 (Ctrl-C는 gdb가 먹는다) | `signal SIGINT` 또는 `queue-signal` |

valgrind·ASan처럼 시그널을 가로채는 도구와 gdb를 같이 쓸 때도 이 정책이 중요하다.

### 3.3 fork/exec와 syscall

- `set follow-fork-mode child`: `fork()` 뒤 자식을 따라간다(기본은 parent). `set detach-on-fork off`면 둘 다 붙잡고 `inferior N`으로 오간다. 셸 스크립트로 띄우는 데몬, `fork()` 후 `exec()`하는 슈퍼바이저를 디버깅할 때 쓴다.
- `catch exec`, `catch fork`: 그 순간에 멈춘다.
- `catch syscall <이름>`: syscall **진입과 복귀**에서 멈춘다. strace의 한 줄을 "멈춰서 bt를 볼 수 있는 strace"로 바꾼 것이다.

### 예제 6 — `catch syscall`로 "누가 그 파일을 열었나"

무엇을 확인하나: `needcfg`가 설정 파일을 여는 openat에서 멈추고, 그 호출의 C 코드 위치와 반환값(-2 = ENOENT)을 본다.

```c
/* m9 needcfg: daemon that silently falls back when its config is missing */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int load_cfg(void)
{
    const char *paths[] = { "/etc/m9/sensord.conf", "/usr/share/m9/sensord.conf" };
    for (unsigned i = 0; i < 2; i++) {
        int fd = open(paths[i], O_RDONLY);
        if (fd >= 0) { close(fd); return 0; }
    }
    return -1;                               /* no message: the classic bug */
}

int main(void)
{
    int rate = load_cfg() == 0 ? 400 : 10;   /* default rate is far too slow */
    int fd = open("/tmp/m9_log.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); exit(1); }
    for (int i = 0; i < 20; i++) {
        char b[64] = { 0 };
        if (write(fd, b, sizeof b) != (ssize_t)sizeof b) { perror("write"); exit(1); }
        fsync(fd);                           /* slow on purpose: one fsync per record */
    }
    close(fd);
    printf("rate=%d Hz\n", rate);
    return 0;
}
```

```sh
gdb -q -batch -nx -ex "break main" -ex "run" -ex "catch syscall openat" \
  -ex c -ex "bt" -ex c -ex "p (long)\$x0" -ex "kill" ./needcfg
```

```text
Breakpoint 1 at 0xa18: file /mnt/m9/src/needcfg.c, line 18.
Breakpoint 1, main () at /mnt/m9/src/needcfg.c:18
18	{
Catchpoint 2 (syscall 'openat' [56])
Catchpoint 2 (call to syscall openat), 0x0000fffff7ed06f8 in __libc_open64 (file=file@entry=0xaaaaaaaa0b70 "/etc/m9/sensord.conf", oflag=oflag@entry=0) at ../sysdeps/unix/sysv/linux/open64.c:41
#0  0x0000fffff7ed06f8 in __libc_open64 (file=file@entry=0xaaaaaaaa0b70 "/etc/m9/sensord.conf", oflag=oflag@entry=0) at ../sysdeps/unix/sysv/linux/open64.c:41
#1  0x0000aaaaaaaa0a50 in open (__oflag=0, __path=0xaaaaaaaa0b70 "/etc/m9/sensord.conf") at /usr/include/aarch64-linux-gnu/bits/fcntl2.h:53
#2  load_cfg () at /mnt/m9/src/needcfg.c:11
#3  main () at /mnt/m9/src/needcfg.c:19
Catchpoint 2 (returned from syscall openat), 0x0000fffff7ed06f8 in __libc_open64 (file=file@entry=0xaaaaaaaa0b70 "/etc/m9/sensord.conf", oflag=oflag@entry=0) at ../sysdeps/unix/sysv/linux/open64.c:41
$1 = -2
[Inferior 1 (process 826) killed]
```

출력에서 볼 것:

- `break main` 후에 catch를 건 이유: 처음부터 걸면 동적 로더가 `/etc/ld.so.cache`, `libc.so.6`을 여는 openat에서 먼저 멈춘다(직접 해 보면 그렇게 나온다). **관심 구간에서만 catch를 켜는 것**이 요령이다.
- syscall 번호 56(arm64의 openat). arm64에는 `open` syscall이 없다(A1).
- "returned from syscall" 시점의 `x0` = **-2**. 커널은 errno를 음수로 돌려주고, glibc가 -1 + `errno=ENOENT`로 바꾼다. `bt`가 `load_cfg()` needcfg.c:11까지 보여 주므로 "어떤 코드가 어떤 경로를 열다 실패했는지"가 한 번에 나온다. strace -k(§5.2)가 이 빌드에서 안 될 때의 대안이다.

### 3.4 watchpoint — "누가 이 변수를 바꿨나"

MCU에서 DWT 비교기로 하던 것과 같다. arm64 코어에는 하드웨어 **watchpoint 레지스터(DBGWVR/DBGWCR)** 가 몇 개 있고, Linux 커널은 ptrace 요청을 받아 그 레지스터를 **스레드별로**(context switch 때 교체) 프로그래밍한다. 개수를 넘기면 gdb는 소프트웨어 watchpoint(한 명령씩 실행하며 비교 — 수백 배 느림)로 떨어지거나 삽입에 실패한다.

### 예제 7 — watchpoint로 오염시킨 범인 잡기

무엇을 확인하나: `calib_gain`이 언제·누구에 의해 100에서 다른 값으로 바뀌는지.

```c
/* m9 corrupt: who overwrites 'calib_gain'? (overflow from a neighbour) */
#include <stdio.h>
#include <string.h>

struct state {
    char name[8];
    int calib_gain;                          /* must stay 100 */
};
static struct state st = { "imu0", 100 };

static void set_name(const char *n) { memcpy(st.name, n, strlen(n) + 1); } /* no bound check */
static void tick(int i) { if (i == 7) set_name("imu0-left"); }

int main(void)
{
    for (int i = 0; i < 10; i++) {
        tick(i);
        if (st.calib_gain != 100) { printf("gain corrupted at i=%d: %d\n", i, st.calib_gain); return 1; }
    }
    puts("ok");
    return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -g -O0 corrupt.c -o corrupt     # 경고 없음
gdb -q -batch -nx -ex "break main" -ex run -ex "watch st.calib_gain" -ex continue -ex bt -ex "p st" -ex continue ./corrupt
```

```text
Breakpoint 1 at 0x8c8: file /mnt/m9/src/corrupt.c, line 16.
Breakpoint 1, main () at /mnt/m9/src/corrupt.c:16
16	    for (int i = 0; i < 10; i++) {
Hardware watchpoint 2: st.calib_gain
Hardware watchpoint 2: st.calib_gain
Old value = 100
New value = 116
__memcpy_generic () at ../sysdeps/aarch64/multiarch/../memcpy.S:100
#0  __memcpy_generic () at ../sysdeps/aarch64/multiarch/../memcpy.S:100
#1  0x0000aaaaaaaa0884 in set_name (n=0xaaaaaaaa0960 "imu0-left") at /mnt/m9/src/corrupt.c:11
#2  0x0000aaaaaaaa08b4 in tick (i=7) at /mnt/m9/src/corrupt.c:12
#3  0x0000aaaaaaaa08d8 in main () at /mnt/m9/src/corrupt.c:17
$1 = {name = "imu0-lef", calib_gain = 116}
gain corrupted at i=7: 116
[Inferior 1 (process 802) exited with code 01]
```

출력에서 볼 것:

- **값이 바뀐 바로 그 명령**(libc의 memcpy 안)에서 멈췄고, bt가 범인 `set_name("imu0-left")` ← `tick(i=7)`을 가리킨다. 116 = 0x74 = `'t'`: 10바이트 문자열 "imu0-left\0"의 9번째 바이트가 `name[8]`을 넘어 `calib_gain`의 하위 바이트에 들어갔다(리틀 엔디언).
- `Hardware watchpoint`라고 나온다. 데이터 위치가 정해진 전역이라 `watch st.calib_gain`으로 충분했다. **지역 변수나 힙 주소**는 스코프를 벗어나면 gdb가 watchpoint를 지우므로 `watch -l *(int *)0x...`(location 고정)을 쓴다.
- `-O0` 전역 변수라 컴파일러 경고가 없다. 실제로 이런 overflow는 `-O2 -D_FORTIFY_SOURCE`에서 `memcpy` 크기 검사에 잡히기도 한다 — 출시 빌드 옵션도 도구다.

### 예제 8 — 하드웨어 watchpoint는 몇 개까지?

무엇을 확인하나: 이 CPU(QEMU cortex-a57)의 watchpoint 레지스터 수와, 넘겼을 때의 gdb 반응.

```sh
dmesg | grep -i "hw-breakpoint"
gdb -q -batch -nx -ex "break main" -ex run \
  -ex "watch *(int*)((char*)&st+0)"   -ex "watch *(int*)((char*)&st+64)" \
  -ex "watch *(int*)((char*)&st+128)" -ex "watch *(int*)((char*)&st+192)" \
  -ex "watch *(int*)((char*)&st+256)" -ex continue ./corrupt
```

```text
[    1.383813] hw-breakpoint: found 6 breakpoint and 4 watchpoint registers.
...
Hardware watchpoint 2: *(int*)((char*)&st+0)
Hardware watchpoint 3: *(int*)((char*)&st+64)
Hardware watchpoint 4: *(int*)((char*)&st+128)
Hardware watchpoint 5: *(int*)((char*)&st+192)
Hardware watchpoint 6: *(int*)((char*)&st+256)
Warning:
Could not insert hardware watchpoint 6.
Could not insert hardware breakpoints:
You may have requested too many hardware breakpoints/watchpoints.
Command aborted.
```

출력에서 볼 것: 커널이 부팅 때 **breakpoint 6개, watchpoint 4개**를 찾았고(Cortex-A57의 실제 구성), 5번째 watchpoint는 `continue` 시점(실제로 레지스터에 쓸 때)에 실패했다. gdb는 `watch` 명령 때는 받아 두고 재개할 때 삽입하므로, 에러가 늦게 나온다. 실제 SoC는 코어마다 다르다(Cortex-A53/A72 등도 보통 4개 안팎 — 보드에서 이 dmesg 줄로 확인한다). 한 watchpoint 레지스터는 정렬된 최대 8바이트(바이트 마스크)를 감시하므로 큰 구조체 전체는 감시할 수 없다.

### 3.5 자주 쓰는 명령 한 장

| 명령 | 뜻 | 메모 |
|---|---|---|
| `x/16xb ADDR`, `x/4gx $sp`, `x/s ptr`, `x/8i $pc` | 메모리 덤프 (바이트·8바이트·문자열·명령) | MCU에서 쓰던 그대로 |
| `info proc mappings` | 디버기의 `/proc/PID/maps` | ASLR 기준 주소 확인 |
| `info sharedlibrary` | 로드된 .so와 심볼 로드 여부 | sysroot 문제 진단 1순위 |
| `p $_siginfo` | 마지막 시그널의 siginfo (SIGSEGV의 `si_addr`) | NULL 근처 주소면 NULL 역참조 |
| `generate-core-file` (`gcore`) | 살아 있는 프로세스의 core를 만들고 계속 진행 | §8에서 실행 |
| `tui enable` (Ctrl-x a) | 소스·어셈블리 분할 화면 | 시리얼 콘솔에서는 화면이 깨질 수 있다 |
| `set print pretty on` | 구조체 들여쓰기 | `.gdbinit`에 |
| `info registers`, `p/x $x0` | arm64 레지스터 (x0~x30, sp, pc, cpsr) | 함수 인자는 x0~x7, 반환값 x0, 복귀 주소 x30 |
| `dprintf file:line,"fmt",args` | 멈추지 않고 printf만 하는 breakpoint | 코드 재빌드 없이 로그 추가 |

---

## 4. core dump 분석 — 현장에서 한 번 죽은 것

### 4.1 무엇이 남고, 무엇이 필요하나

core 파일이 생기는 조건(`ulimit -c`, `core_pattern`, 파이프 모드에서는 RLIMIT_CORE가 무시되는 것)과 systemd-coredump로 모으는 방법은 A6 §8과 L7 §7에서 했다. 이 rootfs도 `core_pattern`이 `|/usr/lib/systemd/systemd-coredump %P %u %g %s %t ...`이다. 이 절은 **받은 core를 읽는 쪽**이다.

core 파일(ELF `ET_CORE`)에 들어 있는 것: 스레드별 레지스터(`NT_PRSTATUS`), siginfo(`NT_SIGINFO`), 어떤 파일이 어느 주소에 매핑됐었는지(`NT_FILE`), 그리고 쓰기 가능한 메모리(스택·힙·.data). 각 ELF의 **첫 페이지**(build-id 노트가 든 헤더)는 `/proc/PID/coredump_filter`의 비트 4(0x10)가 켜져 있을 때만 들어간다 — 이 보드의 기본값은 `00000023`이라 **꺼져 있었다**(예제 12에서 그 결과를 본다). 들어 있지 **않은** 것: 실행 파일과 .so의 코드 페이지, 그리고 심볼·DWARF. 그래서 **같은 빌드의 실행 파일·라이브러리·debug 정보를 따로 구해야** 한다. "core만 주세요"는 반쪽짜리 요청이다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="190" height="96" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="115" y="42" font-size="13" text-anchor="middle">core (현장)</text>
<text x="115" y="62" font-size="12" text-anchor="middle">레지스터 · 스택 · 힙</text>
<text x="115" y="80" font-size="12" text-anchor="middle">NT_FILE 매핑 표</text>
<text x="115" y="98" font-size="12" text-anchor="middle">ELF 첫 페이지 (filter 0x10)</text>
<line x1="210" y1="68" x2="268" y2="68" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="270" y="20" width="170" height="96" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="355" y="42" font-size="13" text-anchor="middle">eu-unstrip -n --core</text>
<text x="355" y="62" font-size="12" text-anchor="middle">모듈마다</text>
<text x="355" y="80" font-size="12" text-anchor="middle">주소 범위 + build-id</text>
<text x="355" y="98" font-size="12" text-anchor="middle">를 뽑는다</text>
<line x1="440" y1="68" x2="488" y2="68" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="490" y="20" width="170" height="96" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="575" y="42" font-size="13" text-anchor="middle">symbol store</text>
<text x="575" y="62" font-size="12" text-anchor="middle">.build-id/xx/yy.debug</text>
<text x="575" y="80" font-size="12" text-anchor="middle">빌드마다 보관</text>
<text x="575" y="98" font-size="12" text-anchor="middle">또는 debuginfod</text>
<line x1="575" y1="116" x2="575" y2="160" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="115" y1="116" x2="115" y2="160" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="20" y="162" width="640" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="185" font-size="13" text-anchor="middle">gdb  실행파일  core   (+ set sysroot · set debug-file-directory)</text>
<text x="340" y="205" font-size="12" text-anchor="middle">build-id 가 다 맞을 때만 bt 를 믿는다</text>
<line x1="180" y1="218" x2="130" y2="258" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="340" y1="218" x2="340" y2="258" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="500" y1="218" x2="550" y2="258" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="20" y="260" width="200" height="124" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="120" y="282" font-size="13" text-anchor="middle">모두 일치</text>
<text x="120" y="304" font-size="12" text-anchor="middle">함수 · 줄 · 지역 변수</text>
<text x="120" y="324" font-size="12" text-anchor="middle">예제 10</text>
<rect x="240" y="260" width="200" height="124" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="282" font-size="13" text-anchor="middle">debug 없음</text>
<text x="340" y="304" font-size="12" text-anchor="middle">"?? ()" + 주소</text>
<text x="340" y="324" font-size="12" text-anchor="middle">오프셋 = PC - 기준주소</text>
<text x="340" y="344" font-size="12" text-anchor="middle">나중에 addr2line</text>
<text x="340" y="364" font-size="12" text-anchor="middle">예제 9, 11</text>
<rect x="460" y="260" width="200" height="124" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="560" y="282" font-size="13" text-anchor="middle">다른 빌드</text>
<text x="560" y="304" font-size="12" text-anchor="middle">"may not match" 경고</text>
<text x="560" y="324" font-size="12" text-anchor="middle">운 좋으면 맞고</text>
<text x="560" y="344" font-size="12" text-anchor="middle">아니면 쓰레기 bt</text>
<text x="560" y="364" font-size="12" text-anchor="middle">예제 12</text>
</svg>
```
그림 4 — core 분석 흐름. core(빨강)에서 모듈별 build-id를 뽑고, 그 build-id로 symbol store(초록)에서 debug 파일을 찾아 gdb에 준다. 아래 세 결과가 이 절의 예제들이다.

### 예제 9 — stripped 바이너리가 현장에서 죽었다

무엇을 확인하나: 보드에 올라간 stripped `crash`가 SIGSEGV로 죽은 core를 debug 정보 없이 보면 무엇이 보이나.

```c
/* m9 crash: parse a sensor frame; a NULL payload crashes in strlen */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct frame { int id; int len; const char *payload; };

static int checksum(const struct frame *f)
{
    int sum = 0;
    size_t n = strlen(f->payload);           /* SIGSEGV when payload == NULL */
    for (size_t i = 0; i < n; i++) sum += (unsigned char)f->payload[i];
    return sum + f->id;
}

static void handle_rx(int seq)
{
    struct frame f = { .id = seq, .len = 4, .payload = "IMU0" };
    if (seq == 3) f.payload = NULL;          /* the "rare" bad frame */
    printf("frame %d csum %d\n", seq, checksum(&f));
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 5;
    for (int s = 0; s < n; s++) handle_rx(s);
    return 0;
}
```

빌드 서버 역할: debug 정보는 build-id 경로로 "symbol store"(`/root/symstore`)에 넣고, 보드에는 strip-all 파일만 둔다.

```sh
gcc -std=gnu11 -Wall -Wextra -g -O1 crash.c -o crash.full
objcopy --only-keep-debug crash.full crash.debug
objcopy --strip-all crash.full crash && objcopy --add-gnu-debuglink=crash.debug crash
B=$(readelf -n crash | sed -n "s/.*Build ID: //p"); echo $B
mkdir -p /root/symstore/.build-id/${B:0:2} && mv crash.debug /root/symstore/.build-id/${B:0:2}/${B:2}.debug
./crash; echo rc=$?
coredumpctl info --no-pager -1 | grep -E "Signal|Executable|Storage|#[0-9]"
coredumpctl dump --no-pager -1 -o core.crash >/dev/null 2>&1
gdb -q -batch -nx -ex "set debuginfod enabled off" -ex bt ./crash core.crash
```

```text
2b1d4309227734758ce32edec9c176ddf07c0ae8
frame 0 csum 283
frame 1 csum 284
frame 2 csum 285
Segmentation fault (core dumped)
rc=139
        Signal: 11 (SEGV)
    Executable: /root/m9/crash
       Storage: /var/lib/systemd/coredump/core.crash.0.a7c8110ddbb348a1bc7d6791e41120dc.717.1792304563000000.zst (present)
                #0  0x0000ffff9a6e2ed0 n/a (n/a + 0x0)
                #1  0x0000aaaac38e08d0 n/a (n/a + 0x0)
                #2  0x0000ffff9a6684c4 n/a (n/a + 0x0)
                #3  0x0000ffff9a668598 n/a (n/a + 0x0)
                #4  0x0000aaaac38e0730 n/a (n/a + 0x0)
...
Core was generated by `./crash'.
Program terminated with signal SIGSEGV, Segmentation fault.
#0  __strlen_asimd () at ../sysdeps/aarch64/multiarch/strlen_asimd.S:96
#1  0x0000aaaac38e08d0 in ?? ()
#2  0x0000ffff9a6684c4 in __libc_start_call_main (main=main@entry=0xaaaac38e0818, argc=argc@entry=1, argv=argv@entry=0xffffd9bce398) at ../sysdeps/nptl/libc_start_call_main.h:58
#3  0x0000ffff9a668598 in __libc_start_main_impl (main=0xaaaac38e0818, argc=1, argv=0xffffd9bce398, init=<optimized out>, fini=<optimized out>, rtld_fini=<optimized out>, stack_end=<optimized out>) at ../csu/libc-start.c:360
#4  0x0000aaaac38e0730 in ?? ()
```

출력에서 볼 것:

- systemd-coredump가 저널에 남긴 스택은 전부 `n/a` — 보드에는 심볼이 없으니 당연하다. 주소만 남는다(그래도 이 주소가 중요하다, 예제 11).
- gdb로 보면 **libc 쪽은 이름이 나온다**(`__strlen_asimd`, `__libc_start_call_main`): 이 보드에는 libc 자체의 debug 파일이 있기 때문이다. 반면 **내 코드는 `?? ()`**. strip-all이라 `.symtab`도 없고, debuglink가 가리키는 `crash.debug`는 같은 디렉터리에 없다.
- `?? ()` 프레임 `0xaaaac38e08d0`이 strlen을 부른 곳이다. 이 주소는 이번 실행의 ASLR 위치 기준이라 그대로는 소스와 못 잇는다.

### 예제 10 — symbol store를 가리키면

```sh
gdb -q -batch -nx -ex "set debuginfod enabled off" -ex "set debug-file-directory /root/symstore:/usr/lib/debug" \
  -ex "file ./crash" -ex "core core.crash" -ex bt -ex "frame 1" -ex "info locals"
```

```text
#0  __strlen_asimd () at ../sysdeps/aarch64/multiarch/strlen_asimd.S:96
#1  0x0000aaaac38e08d0 in checksum (f=<synthetic pointer>) at /mnt/m9/src/crash.c:11
#2  handle_rx (seq=<optimized out>) at /mnt/m9/src/crash.c:20
#3  main (argc=<optimized out>, argv=<optimized out>) at /mnt/m9/src/crash.c:26
#1  0x0000aaaac38e08d0 in checksum (f=<synthetic pointer>) at /mnt/m9/src/crash.c:11
11	    size_t n = strlen(f->payload);           /* SIGSEGV when payload == NULL */
sum = 0
n = <optimized out>
...
```

출력에서 볼 것:

- gdb는 core 속 build-id `2b1d43...`로 `/root/symstore/.build-id/2b/1d43...debug`를 찾았다. **실행 파일은 보드의 stripped 그대로**인데 함수·줄이 다 나온다.
- 같은 주소 `0xaaaac38e08d0` 하나가 `checksum` ← `handle_rx` ← `main` **세 프레임**으로 펼쳐졌다. `-O1`이 두 함수를 `main`에 inline했고, DWARF의 inline 정보가 그걸 다시 풀어 준 것이다. stripped 바이너리만 보고 "main에서 죽었다"고 하면 틀린다.
- `f=<synthetic pointer>`: 구조체 `f`는 메모리에 안 만들어지고 레지스터로만 존재했다. 출시 빌드 core에서 흔히 보는 표시다.

### 4.2 PIE와 ASLR — 주소를 오프셋으로 바꾸기

PIE 실행 파일은 **링크할 때 0 기준**으로 만들어지고, 실행할 때 커널이 무작위 기준 주소에 올린다(B3 §3). 그래서 현장 로그의 절대 주소는 실행마다 다르다. 소스로 바꾸려면 **오프셋 = 주소 − 그 모듈이 올라간 기준 주소**를 계산해서 debug 파일에 묻는다. 공유 라이브러리도 똑같다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="24" font-size="13">실행 중 가상 주소 (이번 실행)</text>
<rect x="20" y="36" width="300" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170" y="61" font-size="12" text-anchor="middle">crash r-xp @ 0xaaaac38e0000</text>
<rect x="20" y="86" width="300" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="111" font-size="12" text-anchor="middle">libc.so.6 @ 0xffff9a640000</text>
<text x="20" y="152" font-size="12">PC(LR) = 0xaaaac38e08d0  (프레임 #1)</text>
<text x="370" y="24" font-size="13">ELF 파일 안 (링크 시 주소, 0 기준)</text>
<rect x="370" y="36" width="290" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="515" y="61" font-size="12" text-anchor="middle">crash.full .text @ 0x0 + ...</text>
<line x1="320" y1="56" x2="368" y2="56" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="20" y="170" width="640" height="86" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="40" y="194" font-size="12">오프셋 = 0xaaaac38e08d0 - 0xaaaac38e0000 = 0x8d0   (기준은 maps 의 offset 0 매핑 시작)</text>
<text x="40" y="216" font-size="12">LR 은 "돌아갈 주소" = BL 다음 명령 → 호출 지점은 0x8d0 - 4 = 0x8cc</text>
<text x="40" y="238" font-size="12">addr2line -f -i -e crash.full 0x8cc   →  checksum crash.c:11 / handle_rx :20 / main :26</text>
</svg>
```
그림 5 — PIE 주소 계산. 실행 중 주소(왼쪽)에서 그 모듈의 기준 주소를 빼면 ELF 파일 안의 주소(오른쪽)가 된다. 복귀 주소는 호출 다음 명령을 가리키므로 arm64에서는 4를 뺀다.

### 예제 11 — gdb 없이 손으로: 기준 주소 찾고 addr2line

무엇을 확인하나: core의 매핑 표에서 기준 주소를 읽고, 서버의 debug 빌드로 오프셋을 소스 줄로 바꾼다. 크래시 리포터가 "모듈 이름 + 오프셋 + build-id"만 올려 보내는 구조(L7 §9)가 이 계산에 기대고 있다.

```sh
gdb -q -batch -nx -ex "info proc mappings" ./crash core.crash | grep -E "crash$"
gdb -q -batch -nx -ex "p/x \$x30" -ex "p/x \$pc" ./crash core.crash | grep "^\\$"
printf "offset=0x%x\n" $((0xaaaac38e08d0 - 0xaaaac38e0000))
addr2line -f -i -C -e crash.full 0x8cc
```

```text
      0xaaaac38e0000     0xaaaac38e1000     0x1000        0x0 /root/m9/crash
      0xaaaac38ff000     0xaaaac3900000     0x1000     0xf000 /root/m9/crash
      0xaaaac3900000     0xaaaac3901000     0x1000    0x10000 /root/m9/crash
$1 = 0xaaaac38e08d0
$2 = 0xffff9a6e2ed0
offset=0x8d0
checksum
/mnt/m9/src/crash.c:11
handle_rx
/mnt/m9/src/crash.c:20
main
/mnt/m9/src/crash.c:26
```

출력에서 볼 것:

- 죽은 PC(`$pc` = `0xffff9a6e2ed0`)는 libc 안(strlen)이고, 링크 레지스터 `x30` = `0xaaaac38e08d0`이 내 코드로의 복귀 주소다. arm64는 `BL`이 복귀 주소를 x30에 넣는다 — 리프 함수인 strlen은 스택 프레임을 만들지 않아서 x30이 아직 살아 있다.
- 기준 주소는 **file offset 0인 첫 매핑**의 시작(`0xaaaac38e0000`). 0x8d0 − 4 = 0x8cc를 addr2line에 줬더니 `-i`(inline 펼치기) 덕분에 세 줄이 나왔다. 예제 10의 gdb 결과와 같다.

### 예제 12 — 다른 빌드로 읽으면 (build-id 불일치)

무엇을 확인하나: "소스가 거의 같으니 다시 빌드해서 보면 되겠지"가 어떻게 실패하나. 먼저 core 안의 build-id 목록을 본다.

```sh
unset DEBUGINFOD_URLS          # 이유는 아래
cat /proc/self/coredump_filter
eu-unstrip -n --core core.crash                 # core만으로
echo ---; eu-unstrip -n --core core.crash -e ./crash   # 디스크의 실행 파일도 주고
sed "s/IMU0/IMU1/" crash.c > /tmp/crash2.c
gcc -std=gnu11 -O1 -g /tmp/crash2.c -o crash.rebuilt; readelf -n crash.rebuilt | grep "Build ID"
gdb -q -batch -nx -ex bt ./crash.rebuilt core.crash
gcc -std=gnu11 -O0 -g /tmp/crash2.c -o crash.O0
gdb -q -batch -nx -ex bt ./crash.O0 core.crash
```

```text
00000023
0xffff9a83e000+0x1000 b2cae66e844d846e69555dc724016b06e9581906@0xffff9a83e2b0 . - linux-vdso.so.1
---
0xffff9a83e000+0x1000 b2cae66e844d846e69555dc724016b06e9581906@0xffff9a83e2b0 . - linux-vdso.so.1
0xffff9a640000+0x1bdf90 27027b96e5b8c475fc327aa445bea1c71d37b4e2@0xffff9a640280 /lib/aarch64-linux-gnu/libc.so.6 /usr/lib/debug/.build-id/27/027b96e5b8c475fc327aa445bea1c71d37b4e2.debug libc.so.6
0xaaaac38e0000+0x20018 2b1d4309227734758ce32edec9c176ddf07c0ae8@0xaaaac38e0264 ./crash - crash
    Build ID: a81385417de191a167d20a3d0b8f69d4df6bb164
warning: core file may not match specified executable file.
Core was generated by `./crash'.
Program terminated with signal SIGSEGV, Segmentation fault.
#0  __strlen_asimd () at ../sysdeps/aarch64/multiarch/strlen_asimd.S:96
#1  0x0000aaaac38e08d0 in checksum (f=<synthetic pointer>) at /tmp/crash2.c:11
#2  handle_rx (seq=<optimized out>) at /tmp/crash2.c:20
#3  main (argc=<optimized out>, argv=<optimized out>) at /tmp/crash2.c:26
warning: core file may not match specified executable file.
Core was generated by `./crash'.
Program terminated with signal SIGSEGV, Segmentation fault.
#0  0x0000ffff9a6e2ed0 in ?? ()
#1  0x0000000000000001 in ?? ()
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
```

출력에서 볼 것:

- `eu-unstrip -n --core`는 모듈마다 **주소 범위 + build-id + 찾은 파일**을 보여 준다. 그런데 **core만 줬을 때는 vdso 한 줄뿐**이었다. `coredump_filter`가 0x23(비트 4 꺼짐)이라 실행 파일·libc의 첫 페이지가 core에 안 들어갔고, 그래서 core 안에 그들의 build-id가 없다(vdso는 익명 매핑처럼 통째로 덤프돼서 남았다). `-e ./crash`로 디스크의 파일을 함께 주자 실행 파일 `2b1d43...`, libc `27027b...`(보드의 `/usr/lib/debug`에서 debug 파일까지)가 나왔다 — 이 build-id는 core가 아니라 **지금 디스크에 있는 파일**에서 읽은 것이다. 장비에 OTA로 새 버전이 깔린 뒤라면 그 값은 거짓이 된다.
- 교훈: 현장 core가 스스로 build-id를 들고 오게 하려면 `coredump_filter`에 0x10을 켠다(서비스 시작 전에 `echo 0x33 > /proc/self/coredump_filter`, 커널 파라미터 `coredump_filter=0x33`, 또는 `CONFIG_CORE_DUMP_DEFAULT_ELF_HEADERS=y`). systemd-coredump도 elfutils로 core를 풀어 모듈 이름·build-id를 저널에 남기려 하는데, 예제 9의 저널 스택이 전부 `n/a (n/a + 0x0)`였던 것도 같은 원인으로 보인다(추정 — 0x33으로 바꿔 다시 확인하는 것이 §14의 연습 문제다).
- 문자열만 바꾼 재빌드(`crash.rebuilt`)는 build-id가 다르다(`a81385...`). gdb가 "may not match"라고 경고하지만 **결과는 우연히 맞았다** — 코드 배치가 똑같았기 때문이다. 이 "우연히 맞음"이 제일 위험하다. 다음번엔 맞지 않는다.
- `-O0`로 다시 빌드한 것은 코드 배치가 완전히 달라서 bt가 `?? ()`, `0x1`, "corrupt stack?"으로 무너졌다. 실제로는 스택이 멀쩡하다. **build-id가 다르면 bt를 믿지 않는다**가 원칙이다.
- `unset DEBUGINFOD_URLS`: 이 rootfs는 `DEBUGINFOD_URLS=https://debuginfod.ubuntu.com`이 기본으로 잡혀 있어서, 처음에 `eu-unstrip`을 그냥 돌렸을 때 **아무 출력 없이 멈췄다**(debuginfod 서버 조회를 기다리는 것으로 보였고 Ctrl-C로 끊었다). 오프라인 장비·사내망에서 elfutils·gdb가 "멈춘 것 같다"면 이 변수를 먼저 본다. gdb는 대화형이면 "Enable debuginfod?"를 묻고, `-batch`면 N으로 답한다.

> 정리: 출시 파이프라인에서 할 일은 하나다 — **빌드마다 실행 파일·라이브러리의 debug 파일을 build-id 경로로 보관**(또는 debuginfod 서버에 올리기). 그러면 현장 core는 언제든 예제 10처럼 읽힌다. 이게 없으면 예제 11처럼 손으로 계산할 재료조차 없다.

---

## 5. strace — syscall 경계에서 보기

### 5.1 어떻게 동작하나, 그리고 왜 느린가

strace는 `ptrace(PTRACE_SEIZE)`로 붙은 뒤 `PTRACE_SYSCALL`로 "다음 syscall 진입/복귀에서 멈춰라"를 반복한다. syscall **한 번마다** 디버기 정지 → strace로 context switch → strace가 레지스터·인자 메모리를 읽고 출력 → 디버기 재개, 이 왕복이 **두 번**(진입, 복귀) 일어난다. Don에게 익숙하게 말하면 "SVC 핸들러 입구와 출구에 JTAG breakpoint를 걸고, 걸릴 때마다 PC 쪽 스크립트가 레지스터를 읽고 다시 run"이다. 그래서 syscall이 잦은 프로그램일수록 느려진다.

`-e trace=openat`처럼 필터를 줘도 **기본 방식에서는 모든 syscall에서 멈춘 뒤** strace가 "이건 안 찍는다"고 판단할 뿐이라 비용이 거의 그대로다. strace 5.3부터 있는 **`--seccomp-bpf`** 는 디버기에 seccomp BPF 필터를 설치해서, 관심 있는 syscall만 `SECCOMP_RET_TRACE`로 strace를 깨우고 나머지는 커널 안에서 그냥 통과시킨다. 필터는 디버기가 **시작할 때** 설치해야 하므로 이미 도는 프로세스(`-p`)에는 쓸 수 없고, 필터가 자식에게 상속되므로 `-f`와 함께 쓴다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="13">기본 ptrace 방식: 모든 syscall 이 두 번씩 멈춘다</text>
<rect x="20" y="34" width="110" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="75" y="56" font-size="12" text-anchor="middle">앱: svc #0</text>
<rect x="160" y="34" width="120" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="220" y="56" font-size="12" text-anchor="middle">진입 정지</text>
<rect x="310" y="34" width="110" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="365" y="56" font-size="12" text-anchor="middle">커널 처리</text>
<rect x="450" y="34" width="120" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="510" y="56" font-size="12" text-anchor="middle">복귀 정지</text>
<line x1="130" y1="51" x2="158" y2="51" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="280" y1="51" x2="308" y2="51" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="420" y1="51" x2="448" y2="51" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="160" y="96" width="410" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="365" y="118" font-size="12" text-anchor="middle">strace: 깨어남 → 레지스터·메모리 읽기 → 출력 → 재개</text>
<line x1="220" y1="68" x2="220" y2="94" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="510" y1="68" x2="510" y2="94" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="20" y="168" font-size="13">--seccomp-bpf: 커널 안의 BPF 필터가 관심 syscall 만 strace 로 보낸다</text>
<rect x="20" y="182" width="110" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="75" y="204" font-size="12" text-anchor="middle">앱: svc #0</text>
<rect x="160" y="182" width="150" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="235" y="204" font-size="12" text-anchor="middle">seccomp 필터</text>
<line x1="130" y1="199" x2="158" y2="199" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="350" y="182" width="220" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="460" y="204" font-size="12" text-anchor="middle">getppid 등: 그냥 실행 (ALLOW)</text>
<rect x="350" y="240" width="220" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="460" y="262" font-size="12" text-anchor="middle">openat: RET_TRACE → strace</text>
<line x1="310" y1="199" x2="348" y2="199" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="310" y1="210" x2="348" y2="252" stroke="currentColor" marker-end="url(#ah6)"/>
</svg>
```
그림 6 — strace의 두 방식. 위: 모든 syscall이 진입·복귀에서 멈추고 strace 프로세스가 매번 깨어난다. 아래: seccomp BPF 필터가 관심 없는 syscall은 커널 안에서 통과시킨다.

### 예제 13 — 조용히 실패하는 설정 파일 찾기 (ENOENT)

무엇을 확인하나: 에러 메시지 없이 10 Hz로 떨어지는 `needcfg`(예제 6의 코드)에서 "실패한 파일 관련 syscall만" 본다.

```sh
./needcfg
strace -f -Z -e trace=%file ./needcfg
```

```text
rate=10 Hz
faccessat(AT_FDCWD, "/etc/ld.so.preload", R_OK) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/etc/m9/sensord.conf", O_RDONLY) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/usr/share/m9/sensord.conf", O_RDONLY) = -1 ENOENT (No such file or directory)
+++ exited with 0 +++
```

출력에서 볼 것: `-Z`(= `--failed-only`)는 **실패한 syscall만** 찍는다. `%file`은 경로 인자를 받는 syscall 묶음(openat, faccessat, stat 계열, execve…). 프로그램은 아무 말도 안 했지만 strace는 정확히 어떤 두 경로를 찾다 실패했는지 보여 준다. 첫 줄의 `/etc/ld.so.preload`는 동적 로더가 늘 확인하는 파일이라 무시한다(B3 §5). 현장에서 제일 자주 쓰는 strace 한 줄이 이것이다: "앱이 뭘 찾다가 못 찾았나".

### 예제 14 — 어떤 syscall이 느린가: `-T`, `-tt`, `-y`

```sh
strace -y -T -tt -e trace=openat,write,fsync ./needcfg 2>&1 | grep -E "m9|fsync" | head -6
strace -T -e trace=fsync ./needcfg 2>&1 | head -4
```

```text
06:29:07.672206 openat(AT_FDCWD</root/m9>, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3</etc/ld.so.cache> <0.015684>
06:29:07.775787 openat(AT_FDCWD</root/m9>, "/lib/aarch64-linux-gnu/libc.so.6", O_RDONLY|O_CLOEXEC) = 3</usr/lib/aarch64-linux-gnu/libc.so.6> <0.000921>
06:29:08.004475 openat(AT_FDCWD</root/m9>, "/etc/m9/sensord.conf", O_RDONLY) = -1 ENOENT (No such file or directory) <0.001167>
06:29:08.018639 openat(AT_FDCWD</root/m9>, "/usr/share/m9/sensord.conf", O_RDONLY) = -1 ENOENT (No such file or directory) <0.004391>
06:29:08.029394 openat(AT_FDCWD</root/m9>, "/tmp/m9_log.bin", O_WRONLY|O_CREAT|O_TRUNC, 0644) = 3</tmp/m9_log.bin> <0.004852>
06:29:08.037087 write(3</tmp/m9_log.bin>, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"..., 64) = 64 <0.001446>
fsync(3)                                = 0 <0.110862>
fsync(3)                                = 0 <0.044821>
fsync(3)                                = 0 <0.026873>
fsync(3)                                = 0 <0.037419>
```

출력에서 볼 것:

- `-tt`는 마이크로초 단위 벽시계 시각, `-T`는 각 syscall 안에서 보낸 시간(꺾쇠 안), `-y`는 fd 번호 옆에 **그 fd가 가리키는 경로**를 붙인다(`3</tmp/m9_log.bin>`). `-yy`는 소켓의 주소·포트까지 보여 준다.
- `fsync` 한 번에 27~111 ms. 64바이트 레코드마다 fsync하는 설계가 문제라는 게 바로 보인다(J5의 전원 차단 안전성과 처리량의 트레이드오프). 이 게스트는 TCG + 공유 VM이라 절대값은 크게 부풀어 있지만, "write는 1 ms, fsync는 수십 ms"라는 **비율**이 진단의 핵심이다.
- `-T`의 시간에는 ptrace 정지·재개 비용이 섞여 있다(예제 16). syscall이 수 µs 수준이면 strace 숫자를 그대로 믿지 않는다(A1의 함정 표).

### 예제 15 — 요약 통계: `-c`

```sh
strace -c ./needcfg 2>&1 | head -14
```

```text
rate=10 Hz
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 26.60    0.083900        4195        20           fsync
 15.09    0.047599        2266        21           write
 14.38    0.045367        9073         5         2 openat
 11.62    0.036656        6109         6           mmap
  4.98    0.015711        3927         4           mprotect
  4.65    0.014676       14676         1           set_robust_list
  4.46    0.014057        4685         3           fstat
  4.27    0.013480       13480         1           execve
  2.86    0.009006        9006         1           rseq
  2.37    0.007489        2496         3           brk
  2.05    0.006467        2155         3           munmap
```

출력에서 볼 것: `-c`는 syscall별 횟수·에러 수·총 시간을 모아 준다. 카메라 앱이 프레임마다 syscall 몇 번을 하는지, 어떤 ioctl이 몇 번 실패하는지(A10 §8.4의 i2cdetect 예처럼) 보는 첫 단계다. 이 표의 "seconds"는 기본으로 **syscall 안에서 보낸 시스템 시간**이다(`-w`를 주면 진입~복귀 벽시계 시간). `write 21회`는 레코드 20회 + 마지막 printf 1회다.

### 5.2 `-k`(스택 트레이스) — 이 빌드에서는 안 된다

`strace -k`는 syscall마다 유저 스택을 찍는 옵션인데, strace가 libdw 또는 libunwind와 함께 빌드됐을 때만 동작한다. 직접 해 보면:

```sh
strace -k -e trace=openat -e status=failed ./needcfg     # 게스트 strace 6.8, 그리고 VM 호스트 strace 6.19
```

```text
strace: Stack traces (-k/--stack-trace option) are not supported by this build of strace
strace: Stack traces (-k/--stack-trace option) are not supported by this build of strace
```

두 Ubuntu 빌드 모두 지원하지 않았다. 대안: 예제 6의 `gdb catch syscall` + `bt`, `perf trace --call-graph dwarf`(M6), bpftrace의 `tracepoint:syscalls:sys_enter_openat { printf("%s\n", ustack); }`(M7). Buildroot·Yocto로 직접 빌드하는 strace는 elfutils를 켜면 `-k`가 들어간다.

### 예제 16 — strace 오버헤드 측정

무엇을 확인하나: 값싼 syscall(`getppid`) N번의 호출당 시간을 strace 없이 / strace 전체 / strace `-e` 필터 / `--seccomp-bpf` 필터로 비교한다.

```c
/* m9 sysloop: N cheap syscalls, timed -> strace overhead */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 100000;
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);      /* vDSO: not a real syscall */
    for (int i = 0; i < n; i++) syscall(SYS_getppid);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double ns = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / n;
    printf("%d getppid: %.0f ns/call\n", n, ns);
    return 0;
}
```

```sh
# VM 호스트 (실제 CPU, kernel 7.0) — 3회 반복
for r in 1 2 3; do ./sysloop 200000; strace -o /dev/null ./sysloop 200000
  strace -o /dev/null -e trace=openat ./sysloop 200000
  strace -o /dev/null --seccomp-bpf -f -e trace=openat ./sysloop 200000; done
```

```text
200000 getppid: 164 ns/call
200000 getppid: 73894 ns/call
200000 getppid: 46441 ns/call
200000 getppid: 173 ns/call
200000 getppid: 157 ns/call
200000 getppid: 41019 ns/call
200000 getppid: 9901 ns/call
200000 getppid: 220 ns/call
200000 getppid: 198 ns/call
200000 getppid: 18459 ns/call
200000 getppid: 45740 ns/call
200000 getppid: 160 ns/call
```

같은 실험을 게스트(TCG)에서 하면(N을 줄여서):

```text
5000 getppid: 37908 ns/call
1000 getppid: 8557606 ns/call
1000 getppid: 8534289 ns/call
5000 getppid: 38012 ns/call
...
5000 getppid: 33664 ns/call
1000 getppid: 7717547 ns/call
1000 getppid: 707459 ns/call
5000 getppid: 21930 ns/call
```
(가운데 한 회는 생략. 순서는 위와 같이 없음 / 전체 / -e / seccomp.)

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">getppid 호출당 시간 (VM 호스트, 3회 중 최소값, 로그 축)</text>
<line x1="200" y1="36" x2="200" y2="196" stroke="currentColor"/>
<line x1="200" y1="196" x2="640" y2="196" stroke="currentColor"/>
<line x1="340" y1="36" x2="340" y2="196" stroke="#888" stroke-dasharray="3,3"/>
<line x1="480" y1="36" x2="480" y2="196" stroke="#888" stroke-dasharray="3,3"/>
<line x1="620" y1="36" x2="620" y2="196" stroke="#888" stroke-dasharray="3,3"/>
<text x="200" y="214" font-size="12" text-anchor="middle">100 ns</text>
<text x="340" y="214" font-size="12" text-anchor="middle">1 µs</text>
<text x="480" y="214" font-size="12" text-anchor="middle">10 µs</text>
<text x="620" y="214" font-size="12" text-anchor="middle">100 µs</text>
<text x="190" y="63" font-size="12" text-anchor="end">strace 없음</text>
<rect x="200" y="46" width="27" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="234" y="63" font-size="12">157 ns</text>
<text x="190" y="101" font-size="12" text-anchor="end">--seccomp-bpf -e openat</text>
<rect x="200" y="84" width="29" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="236" y="101" font-size="12">160 ns</text>
<text x="190" y="139" font-size="12" text-anchor="end">strace -e openat</text>
<rect x="200" y="122" width="279" height="26" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="486" y="139" font-size="12">9.9 µs (63배)</text>
<text x="190" y="177" font-size="12" text-anchor="end">strace (전체)</text>
<rect x="200" y="160" width="317" height="26" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="524" y="177" font-size="12">18.5 µs (118배)</text>
<text x="20" y="240" font-size="12">반복마다 편차가 컸다 (전체 18~74 µs, -e 9.9~46 µs, 부하 걸린 공유 VM). 순서는 그대로였다.</text>
</svg>
```
그림 7 — strace 오버헤드 실측. seccomp-bpf 필터는 비용이 거의 0이고, 일반 strace는 필터를 줘도 syscall마다 수십~백 배 비싸다.

출력에서 볼 것:

- 호스트에서 `getppid` 한 번은 약 160 ns. strace를 붙이면 **18~74 µs, 즉 100~450배**. `-e trace=openat`로 걸러도 10~46 µs — 필터가 비용을 거의 못 줄인다(모든 syscall에서 여전히 멈춘다). `--seccomp-bpf`면 160~220 ns로 **strace 없을 때와 같다**.
- 게스트(TCG)에서는 strace 없이도 호출당 22~45 µs, strace를 붙이면 0.7~9.7 ms였다. 에뮬레이션에서는 context switch 자체가 매우 비싸서 비율이 더 나빠진다. 실제 Cortex-A 보드는 호스트 쪽 비율에 가깝다고 보면 되지만, **보드에서 직접 재는 것이 정답**이다.
- 실무 결론: 초당 수만 번 syscall하는 데이터 경로(UDP LiDAR 수신, V4L2 DQBUF 루프)에 strace를 붙이면 타이밍이 완전히 바뀌어 버그가 사라지거나 새로 생긴다. 필요한 syscall만 보려면 처음부터 `--seccomp-bpf -f`로 띄우고, 이미 도는 프로세스는 짧게만(`timeout 2 strace -p`) 붙인다. 운영 장비에서 상시로 보려면 ptrace가 아닌 tracepoint 기반(`perf trace`, bpftrace — M6, M7)을 쓴다.

### 5.3 자주 쓰는 옵션 한 장

| 옵션 | 뜻 |
|---|---|
| `-f` / `-ff -o out` | 스레드·자식까지 따라감 / PID별 파일로 나눔 |
| `-p PID` (여러 번 가능) | 이미 도는 프로세스에 붙기. Ctrl-C로 떨어지면 프로세스는 계속 돈다 |
| `-e trace=openat,ioctl` / `%file` `%net` `%desc` `%process` `%memory` | syscall 이름·묶음 필터 |
| `-Z` / `-z` | 실패한 것만 / 성공한 것만 |
| `-s 256` | 문자열 인자를 256바이트까지 (기본 32 — write 내용이 잘려 보이는 이유) |
| `-X verbose` | 상수를 이름과 숫자로 함께 |
| `-P /dev/i2c-1` | 그 경로를 건드리는 syscall만 |
| `-e inject=openat:error=ENOENT:when=2` | 2번째 openat을 강제로 실패시킴 — 에러 경로 테스트 |
| `--seccomp-bpf` | 필터를 커널에서 (시작할 때만, `-f`와 함께) |

---

## 6. ltrace — 라이브러리 호출 경계

### 6.1 어떻게 동작하나

ltrace는 실행 파일의 **PLT 항목**(B3 §6: 공유 라이브러리 함수를 부르는 간접 점프 스텁)에 breakpoint를 심는다. 프로그램이 `malloc@plt`로 점프할 때마다 멈춰서 인자를 찍고, 복귀 주소에도 breakpoint를 걸어 반환값을 찍는다. 그래서 **PLT를 거치지 않는 호출은 보이지 않는다**: 정적 링크 바이너리(PLT가 없다), `-fno-plt`로 빌드한 바이너리(GOT를 직접 읽어 `blr`), 같은 라이브러리 안의 내부 호출(기본으로는 실행 파일의 PLT만 본다).

`-z now`(BIND_NOW)는 흔히 "ltrace를 망가뜨린다"고 알려져 있는데, 이 환경에서 직접 해 보면 결과가 다르다.

### 예제 17 — lazy / now / no-plt / static

```c
/* m9 ltr: a few libc calls for ltrace */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    char *p = malloc(32);
    if (!p) { perror("malloc"); exit(1); }
    strcpy(p, "imu0");
    printf("len=%zu\n", strlen(p));
    free(p);
    return 0;
}
```

```sh
gcc -O1 -g ltr.c -o ltr                 # Ubuntu 기본 (어떤 플래그로 링크되나 보자)
gcc -O1 -g -Wl,-z,lazy ltr.c -o ltr.lazy
gcc -O1 -g -Wl,-z,now  ltr.c -o ltr.now
gcc -O1 -g -fno-plt    ltr.c -o ltr.noplt
gcc -O1 -g -static     ltr.c -o ltr.static
for b in ltr ltr.lazy ltr.now ltr.static; do
  echo "== $b: $(readelf -d $b 2>/dev/null | grep -E "FLAGS|BIND_NOW" | tr -s " " | tr "\n" " ")"; ltrace ./$b; done
ltrace ./ltr.noplt
```

```text
== ltr:  0x000000000000001e (FLAGS) BIND_NOW  0x000000006ffffffb (FLAGS_1) Flags: NOW PIE 
__libc_start_main(0xaaaacd3b08d8, 1, 0xffffdf155648, 0 <unfinished ...>
malloc(32)                                                         = 0xaaaaffc382a0
strlen("imu0")                                                     = 4
__printf_chk(2, 0xaaaacd3b0980, 4, 0len=4
)                                                                  = 6
free(0xaaaaffc382a0)                                               = <void>
__cxa_finalize(0xaaaacd3d0008, 0xaaaacd3b0880, 1, 568)             = 1
+++ exited (status 0) +++
== ltr.lazy:  0x000000006ffffffb (FLAGS_1) Flags: PIE 
...(위와 같은 6개 호출)
== ltr.now:  0x000000000000001e (FLAGS) BIND_NOW  0x000000006ffffffb (FLAGS_1) Flags: NOW PIE 
...(위와 같은 6개 호출)
== ltr.static: 
Couldn't find .dynsym or .dynstr in "/proc/964/exe"
__libc_start_main(0xaaaacb9c0898, 1, 0xffffd106f5d8, 0len=4
 <unfinished ...>
__cxa_finalize(0xaaaacb9e0008, 0xaaaacb9c0840, 1, 568)             = 1
+++ exited (status 0) +++
```
(정렬용 공백은 줄였다. 마지막 블록이 `ltr.noplt`의 출력이다.)

출력에서 볼 것:

- **Ubuntu gcc의 기본 링크가 이미 `BIND_NOW`** 다(`Flags: NOW PIE` — 배포판의 하드닝 기본값). 그런데도 ltrace는 malloc·strlen·printf·free를 다 잡았다. `-z now`는 "GOT를 시작할 때 다 채운다"는 뜻일 뿐 **PLT 스텁은 여전히 있고 호출은 여전히 스텁을 지나간다**. 이 ltrace(0.7.3 + Debian 패치)는 그 스텁에 breakpoint를 거는 방식이라 잘 동작했다. 옛 ltrace·다른 아키텍처에서는 lazy 바인딩 시점을 이용하는 구현 때문에 `-z now`에서 아무것도 안 나오는 경우가 실제로 있었다 — "환경마다 다르니 직접 확인"이 정답이다.
- `strcpy(p, "imu0")`는 안 보인다. 컴파일러가 5바이트 상수 복사를 **인라인 store로 바꿔** 라이브러리 호출 자체가 없어졌다. 소스에 있는 호출이 ltrace에 없으면 먼저 최적화를 의심한다. `printf`도 `_FORTIFY_SOURCE` 때문에 `__printf_chk`로 바뀌어 있다.
- `-fno-plt`: `.plt` 섹션이 사라지고(readelf로 확인), 내 코드의 malloc/strlen/printf/free가 **전부 사라졌다**. 남은 두 줄은 crt 코드(`__libc_start_main`, `__cxa_finalize`)가 부른 것이다.
- `-static`: 동적 심볼 테이블이 없어서 ltrace가 아예 시작을 못 한다.

라이브러리끼리의 호출을 보려면 `-l`로 라이브러리를 지정한다. 아래는 libc 안에서 일어난 호출을 세어 본 것이다.

```sh
ltrace -c -l "libc.so*" ./ltr.now
```

```text
len=4
% time     seconds  usecs/call     calls      function
------ ----------- ----------- --------- --------------------
 41.89    0.080972       40486         2 malloc
 33.03    0.063849       63849         1 __printf_chk
 12.85    0.024845       24845         1 __cxa_finalize
 12.23    0.023644       23644         1 free
------ ----------- ----------- --------- --------------------
100.00    0.193310                     5 total
```

출력에서 볼 것: malloc이 2번이다 — 내가 부른 1번 + `printf`가 stdout 버퍼를 만들 때 부른 1번. 시간 열은 breakpoint 왕복 비용이 대부분이라 의미가 없다.

### 6.2 ltrace의 위치

ltrace는 오래 정체된 프로젝트(0.7.3이 2013년 릴리스)이고, arm64·멀티스레드·C++에서 자주 불안정하다. 쓸 만한 곳은 "이 바이너리가 어떤 라이브러리 함수를 어떤 인자로 부르나"를 **빠르게 한 번** 볼 때다. 대안:

| 하고 싶은 것 | 도구 |
|---|---|
| 특정 함수 몇 개의 인자·반환값 | `bpftrace -e 'uprobe:/lib/aarch64-linux-gnu/libc.so.6:malloc { ... }'` (M7), `perf probe -x` (M6) |
| 함수를 가로채서 로그·수정 | `LD_PRELOAD` 래퍼 (B3 §7.3) |
| 멈춰서 보기 | gdb `break malloc` + `commands` / `dprintf` |
| 정적 바이너리 | gdb 또는 uprobe (심볼이 있으면 PLT와 무관하다) |

---

## 7. 메모리 버그와 레이스 — valgrind vs sanitizer

### 7.1 두 가지 접근

**valgrind**는 **동적 바이너리 계측(DBI)** 도구다. 프로그램을 CPU에서 직접 돌리지 않고, valgrind 코어가 기계어를 블록 단위로 중간 표현(VEX IR)으로 바꾸고, 도구(memcheck, helgrind …)가 검사 코드를 끼워 넣은 뒤 다시 기계어로 JIT해서 돌린다. Don의 말로 하면 **ISS 위에서 펌웨어를 돌리면서 모든 load/store를 감시하는 것**이다. 재컴파일이 필요 없고(stripped 출시 바이너리에도 쓸 수 있다, 단 줄 번호는 debug 정보가 있어야), 대신 느리고 무겁다. 모든 스레드를 **한 번에 하나씩** 돌린다(직렬화) — 그래서 멀티코어 타이밍 버그는 재현이 바뀐다.

**sanitizer**(ASan, TSan, UBSan, LSan)는 **컴파일 시점 계측**이다. GCC/Clang이 모든 load/store 앞에 "shadow 메모리 확인" 코드를 넣고, 런타임 라이브러리(libasan …)가 malloc/free를 바꿔 끼운다. ASan은 메모리 8바이트마다 shadow 1바이트를 두고, 할당 앞뒤에 "redzone"(접근 금지 띠)을, free된 블록은 "quarantine"(한동안 재사용 금지)에 둔다. MCU의 MPU 영역을 아주 촘촘히 걸어 둔 것과 비슷하다. 다시 빌드해야 하지만 훨씬 빠르고, 실제 멀티스레드로 돈다.

| | valgrind memcheck | ASan (`-fsanitize=address`) |
|---|---|---|
| 준비 | 그대로 실행 (`valgrind ./app`) | 다시 컴파일·링크, 런타임 lib 필요 |
| 잡는 것 | heap OOB, UAF, 초기화 안 된 값 사용, 누수, 잘못된 free | heap·stack·global OOB, UAF, use-after-return(옵션), double free, 누수(LSan) |
| 못 잡는 것 | stack·global 배열 OOB(대부분) | 초기화 안 된 값 (MSan은 Clang 전용) |
| 속도 (예제 21 실측) | 13.6배 느림 | 2.4배 느림 |
| 메모리 | 85 MB (작은 프로그램) | 기본 366 MB (quarantine 256 MB), quarantine 줄이면 12 MB |
| 임베디드 | 보드에 valgrind 패키지(수십 MB), 아키텍처 지원 필요 | 보드에 libasan.so만, 가상 주소 공간 크게 예약 |

### 예제 18 — memcheck: OOB, UAF, 초기화 안 된 값

```c
/* m9 bugs: OOB write, use-after-free, uninitialised read, leak + a hot loop */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long work(int n)                      /* clean CPU+heap work, for slowdown timing */
{
    long s = 0;
    for (int i = 0; i < n; i++) {
        int *v = malloc(64 * sizeof *v);
        if (!v) { perror("malloc"); exit(1); }
        for (int j = 0; j < 64; j++) v[j] = i ^ j;
        for (int j = 0; j < 64; j++) s += v[j];
        free(v);
    }
    return s;
}

static char *interior;
static __attribute__((noinline)) void release(char *p) { free(p); }

int main(int argc, char **argv)
{
    int mode = argc > 1 ? atoi(argv[1]) : 0;
    if (mode == 0) { printf("work %ld\n", work(argc > 2 ? atoi(argv[2]) : 200000)); return 0; }
    char *buf = malloc(16);
    if (!buf) { perror("malloc"); exit(1); }
    if (mode == 1) buf[16] = 'X';                        /* heap OOB write by 1 */
    if (mode == 2) { release(buf); buf[0] = 'U'; return 0; } /* use-after-free */
    if (mode == 3 && buf[0] == 'A') puts("A");           /* malloc'd bytes are uninit */
    if (mode == 4) { buf = malloc(100); buf[0] = 0; }    /* first 16 B leak */
    if (mode == 5) {                                     /* leak kinds */
        void **head = malloc(16); head[0] = malloc(32);  /* 16 definitely, 32 indirectly */
        interior = (char *)malloc(64) + 8;               /* only a middle pointer: possibly */
    }
    printf("mode %d done\n", mode);
    free(buf);
    return 0;
}
```

`-O0 -g`로 빌드하면 경고가 없다. 같은 파일을 `-O2`로 빌드하면 GCC가 일부 버그를 **컴파일 때** 잡는다(게스트 gcc 13의 실제 출력, 경로와 note 줄은 줄였다):

```text
bugs.c:28:23: warning: array subscript 16 is outside array bounds of ‘char[16]’ [-Warray-bounds=]
bugs.c:30:25: warning: ‘*buf’ may be used uninitialized [-Wmaybe-uninitialized]
```

경고도 디버깅 도구다. (UAF를 `release()`로 감싼 이유도 이것이다 — 그냥 `free(buf); buf[0]=...`라고 쓰면 GCC 15의 `-Wuse-after-free`가 바로 잡아 버린다.)

```sh
for m in 1 2 3; do echo "=== mode $m"; valgrind -q --track-origins=yes ./bugs $m; done
```

```text
=== mode 1
==1584== Invalid write of size 1
==1584==    at 0x108AFC: main (bugs.c:28)
==1584==  Address 0x4a7e050 is 0 bytes after a block of size 16 alloc'd
==1584==    at 0x4885250: malloc (in /usr/libexec/valgrind/vgpreload_memcheck-arm64-linux.so)
==1584==    by 0x108ABF: main (bugs.c:26)
mode 1 done
=== mode 2
==1641== Invalid write of size 1
==1641==    at 0x108B1C: main (bugs.c:29)
==1641==  Address 0x4a7e040 is 0 bytes inside a block of size 16 free'd
==1641==    at 0x48882A8: free (in /usr/libexec/valgrind/vgpreload_memcheck-arm64-linux.so)
==1641==    by 0x108A23: release (bugs.c:20)
==1641==    by 0x108B13: main (bugs.c:29)
==1641==  Block was alloc'd at
==1641==    at 0x4885250: malloc (in /usr/libexec/valgrind/vgpreload_memcheck-arm64-linux.so)
==1641==    by 0x108ABF: main (bugs.c:26)
=== mode 3
==1698== Conditional jump or move depends on uninitialised value(s)
==1698==    at 0x108B40: main (bugs.c:30)
==1698==  Uninitialised value was created by a heap allocation
==1698==    at 0x4885250: malloc (in /usr/libexec/valgrind/vgpreload_memcheck-arm64-linux.so)
==1698==    by 0x108ABF: main (bugs.c:26)
mode 3 done
```

출력에서 볼 것:

- 각 보고는 **세 가지**를 준다: 잘못된 접근의 위치(28행), 그 주소가 어떤 블록과 어떤 관계인지("0 bytes after a block of size 16"), 그 블록을 할당한(그리고 해제한) 스택. UAF는 "free'd at release(20행)"까지 알려 준다.
- mode 1·3은 "done"이 찍혔다. memcheck는 **보고만 하고 계속 실행한다**(ASan은 기본으로 첫 에러에서 멈춘다). mode 1의 1바이트 넘침은 실제 하드웨어에서는 malloc의 정렬 여유 공간에 떨어져 아무 일도 안 일어난다 — 그래서 이런 버그가 몇 년씩 숨는다.
- mode 3: 값을 **복사하는 것**은 에러가 아니고, 그 값으로 **분기·주소 계산·syscall 인자**를 할 때 보고한다. `--track-origins=yes`가 "malloc한 블록에서 왔다"를 알려 준다(대신 더 느리다).
- 주소 `0x108AFC`: valgrind는 PIE를 자기 기준 주소(0x108000)에 올리므로 일반 실행과 주소가 다르다.

### 예제 19 — 누수 네 종류

```sh
for m in 4 5; do echo "=== mode $m"; valgrind --leak-check=full ./bugs $m; done
```

```text
=== mode 4
mode 4 done
==995== HEAP SUMMARY:
==995==     in use at exit: 16 bytes in 1 blocks
==995==   total heap usage: 3 allocs, 2 frees, 4,212 bytes allocated
==995== 16 bytes in 1 blocks are definitely lost in loss record 1 of 1
==995==    at 0x4885250: malloc (in /usr/libexec/valgrind/vgpreload_memcheck-arm64-linux.so)
==995==    by 0x108ABF: main (bugs.c:26)
==995== LEAK SUMMARY:
==995==    definitely lost: 16 bytes in 1 blocks
==995==    indirectly lost: 0 bytes in 0 blocks
==995==      possibly lost: 0 bytes in 0 blocks
==995==    still reachable: 0 bytes in 0 blocks
==995==         suppressed: 0 bytes in 0 blocks
=== mode 5
mode 5 done
==998== HEAP SUMMARY:
==998==     in use at exit: 112 bytes in 3 blocks
==998==   total heap usage: 5 allocs, 2 frees, 4,224 bytes allocated
==998== 48 (16 direct, 32 indirect) bytes in 1 blocks are definitely lost in loss record 2 of 3
==998==    at 0x4885250: malloc (in /usr/libexec/valgrind/vgpreload_memcheck-arm64-linux.so)
==998==    by 0x108B83: main (bugs.c:33)
==998== 64 bytes in 1 blocks are possibly lost in loss record 3 of 3
==998==    at 0x4885250: malloc (in /usr/libexec/valgrind/vgpreload_memcheck-arm64-linux.so)
==998==    by 0x108BA3: main (bugs.c:34)
==998== LEAK SUMMARY:
==998==    definitely lost: 16 bytes in 1 blocks
==998==    indirectly lost: 32 bytes in 1 blocks
==998==      possibly lost: 64 bytes in 1 blocks
==998==    still reachable: 0 bytes in 0 blocks
==998==         suppressed: 0 bytes in 0 blocks
```
(배너 줄은 생략. mode 4의 블록 주소가 26행인 것은 이 줄이 덮어쓰기 전의 첫 `malloc(16)`이기 때문이다.)

출력에서 볼 것 — memcheck는 프로그램이 끝날 때 **GC처럼** 루트(전역·스택·레지스터)에서 포인터를 따라가 블록을 분류한다:

| 종류 | 뜻 | mode 5에서 |
|---|---|---|
| definitely lost | 어디서도 가리키지 않는다 → 확실한 누수 | `head`(16 B) — 지역 변수가 사라졌다 |
| indirectly lost | 잃어버린 블록만 가리킨다 → 부모를 고치면 같이 해결 | `head[0]`(32 B) |
| possibly lost | 블록 **중간**을 가리키는 포인터만 있다 → 누수일 수도, 의도된 내부 포인터일 수도 | `interior`(64 B 블록의 +8) |
| still reachable | 끝날 때까지 시작 주소를 가리키는 포인터가 있다 → 보통 "안 해제한 전역 캐시" | (§8의 `grow`가 이것) |

장기 실행 데몬에서는 **still reachable도 문제**가 될 수 있다. 끝날 때 해제 안 하는 것은 괜찮지만, 실행 중에 계속 늘어나면 그게 바로 "며칠 지나면 메모리가 는다"다(§8).

### 예제 20 — 같은 버그를 ASan으로

```sh
gcc -std=gnu11 -Wall -Wextra -O0 -g -fsanitize=address -fno-omit-frame-pointer bugs.c -o bugs.asan
for m in 1 2 3 5; do echo "=== asan mode $m"; ./bugs.asan $m 2>&1 | grep -E "ERROR|WRITE|READ|#[0-3] |located|freed by|allocated by|SUMMARY|Direct|Indirect"; done
```

```text
=== asan mode 1
==1007==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x502000000020 at pc 0xaaaae96311fc bp 0xfffffeb4a890 sp 0xfffffeb4a880
WRITE of size 1 at 0x502000000020 thread T0
    #0 0xaaaae96311f8 in main /mnt/m9/src/bugs.c:28
    #1 0xffff89bb84c0 in __libc_start_call_main ../sysdeps/nptl/libc_start_call_main.h:58
    #2 0xffff89bb8594 in __libc_start_main_impl ../csu/libc-start.c:360
    #3 0xaaaae9630dec in _start (/root/m9/bugs.asan+0xdec) (BuildId: 4e7c4099983026b9c082e9e794d083d496945f49)
0x502000000020 is located 0 bytes after 16-byte region [0x502000000010,0x502000000020)
allocated by thread T0 here:
    #0 0xffff89e376d0 in malloc ../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:69
    #1 0xaaaae9631170 in main /mnt/m9/src/bugs.c:26
...
SUMMARY: AddressSanitizer: heap-buffer-overflow /mnt/m9/src/bugs.c:28 in main
=== asan mode 2
==1010==ERROR: AddressSanitizer: heap-use-after-free on address 0x502000000010 at pc 0xaaaae3db1264 bp 0xffffe011e380 sp 0xffffe011e370
WRITE of size 1 at 0x502000000010 thread T0
    #0 0xaaaae3db1260 in main /mnt/m9/src/bugs.c:29
...
freed by thread T0 here:
    #0 0xffffa59d61b4 in free ../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:52
    #1 0xaaaae3db108c in release /mnt/m9/src/bugs.c:20
    #2 0xaaaae3db1214 in main /mnt/m9/src/bugs.c:29
...
=== asan mode 3
=== asan mode 5
==1017==ERROR: LeakSanitizer: detected memory leaks
Direct leak of 16 byte(s) in 1 object(s) allocated from:
    #1 0xaaaabbf51368 in main /mnt/m9/src/bugs.c:33
...
Indirect leak of 32 byte(s) in 1 object(s) allocated from:
    #1 0xaaaabbf51374 in main /mnt/m9/src/bugs.c:33
...
SUMMARY: AddressSanitizer: 48 byte(s) leaked in 2 allocation(s).
```

출력에서 볼 것:

- mode 1·2는 memcheck와 같은 결론(28행 1바이트 넘침, 29행 UAF + 20행에서 free)이다. ASan 보고에는 **BuildId와 모듈+오프셋**(`bugs.asan+0xdec`)이 같이 찍힌다 — §4의 오프라인 해석이 그대로 된다.
- **mode 3은 아무것도 안 나왔다.** ASan은 초기화 안 된 메모리를 모른다. Clang의 MemorySanitizer(`-fsanitize=memory`)가 그 일을 하지만 GCC에는 없고, 모든 라이브러리를 MSan으로 다시 빌드해야 해서 임베디드에서는 거의 안 쓴다. 이 종류는 valgrind가 강하다.
- mode 5: LSan은 direct 16 B, indirect 32 B만 보고했고 **possibly lost(내부 포인터)는 보고하지 않았다** — LSan은 블록 중간을 가리키는 포인터도 "도달 가능"으로 친다.
- 이 rootfs의 gcc 13에는 `libasan.so.8`, `libtsan.so.2`, `libubsan.so.1`, `liblsan`, `libhwasan`이 다 깔려 있었다. arm64에는 **HWASan**(`-fsanitize=hwaddress`)도 있다 — arm64의 TBI(포인터 상위 바이트 무시) 기능으로 포인터에 태그를 달아 ASan보다 메모리를 훨씬 덜 쓴다(GCC 11+, 커널이 tagged address ABI를 지원해야 한다). 이번에는 실행해 보지 않았다(가이드).

### 예제 21 — 비용 실측: native vs ASan vs valgrind

무엇을 확인하나: 버그 없는 계산 루프(`bugs 0 N` — malloc/free 100만 번 + 정수 연산)를 네 방식으로 돌려 벽시계 시간과 최대 RSS를 잰다. 측정용 래퍼:

```c
/* m9time: run a command, report wall time and peak RSS (like /usr/bin/time -v) */
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: m9time cmd...\n"); return 2; }
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    pid_t p = fork();
    if (p < 0) { perror("fork"); exit(1); }
    if (p == 0) { execvp(argv[1], argv + 1); perror("execvp"); _exit(127); }
    int st; struct rusage ru;
    if (wait4(p, &st, 0, &ru) < 0) { perror("wait4"); exit(1); }
    clock_gettime(CLOCK_MONOTONIC, &b);
    double s = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
    fprintf(stderr, "M9TIME wall=%.2fs maxrss=%ldKB cmd=%s\n", s, ru.ru_maxrss, argv[1]);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
}
```

```sh
# VM 호스트, N = 1,000,000, 3회
for r in 1 2 3; do ./m9time ./bugs 0 $N; ./m9time ./bugs.asan 0 $N
  ./m9time valgrind -q --tool=none ./bugs 0 $N; ./m9time valgrind -q ./bugs 0 $N; done 2>&1 | grep M9TIME
ASAN_OPTIONS=quarantine_size_mb=4 ./m9time ./bugs.asan 0 $N
```

```text
M9TIME wall=0.48s maxrss=1248KB cmd=./bugs
M9TIME wall=1.59s maxrss=366520KB cmd=./bugs.asan
M9TIME wall=1.37s maxrss=35968KB cmd=valgrind
M9TIME wall=6.68s maxrss=85416KB cmd=valgrind
M9TIME wall=0.54s maxrss=1264KB cmd=./bugs
M9TIME wall=1.25s maxrss=366456KB cmd=./bugs.asan
M9TIME wall=1.08s maxrss=35968KB cmd=valgrind
M9TIME wall=6.37s maxrss=85680KB cmd=valgrind
M9TIME wall=0.47s maxrss=1264KB cmd=./bugs
M9TIME wall=1.15s maxrss=366456KB cmd=./bugs.asan
M9TIME wall=1.06s maxrss=35968KB cmd=valgrind
M9TIME wall=6.52s maxrss=85676KB cmd=valgrind
M9TIME wall=0.98s maxrss=12408KB cmd=./bugs.asan
```
(valgrind 두 줄은 위가 `--tool=none`, 아래가 memcheck다.)

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">A. 실행 시간 (native 대비 배수, 3회 중 최소, VM 호스트)</text>
<line x1="180" y1="34" x2="180" y2="190" stroke="currentColor"/>
<line x1="180" y1="190" x2="640" y2="190" stroke="currentColor"/>
<text x="180" y="206" font-size="12" text-anchor="middle">0</text>
<text x="337" y="206" font-size="12" text-anchor="middle">5배</text>
<text x="494" y="206" font-size="12" text-anchor="middle">10배</text>
<line x1="337" y1="34" x2="337" y2="190" stroke="#888" stroke-dasharray="3,3"/>
<line x1="494" y1="34" x2="494" y2="190" stroke="#888" stroke-dasharray="3,3"/>
<text x="170" y="61" font-size="12" text-anchor="end">native</text>
<rect x="180" y="44" width="31" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="218" y="61" font-size="12">1.0 (0.47 s)</text>
<text x="170" y="97" font-size="12" text-anchor="end">valgrind none</text>
<rect x="180" y="80" width="71" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="258" y="97" font-size="12">2.3 (1.06 s)</text>
<text x="170" y="133" font-size="12" text-anchor="end">ASan</text>
<rect x="180" y="116" width="77" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="264" y="133" font-size="12">2.4 (1.15 s)</text>
<text x="170" y="169" font-size="12" text-anchor="end">valgrind memcheck</text>
<rect x="180" y="152" width="426" height="26" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="476" y="169" font-size="12">13.6 (6.37 s)</text>
<text x="20" y="238" font-size="13">B. 최대 RSS (MB)</text>
<line x1="180" y1="250" x2="180" y2="400" stroke="currentColor"/>
<text x="170" y="270" font-size="12" text-anchor="end">native</text>
<rect x="180" y="257" width="2" height="20" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="190" y="272" font-size="12">1.3</text>
<text x="170" y="298" font-size="12" text-anchor="end">ASan quarantine 4 MB</text>
<rect x="180" y="285" width="15" height="20" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="202" y="300" font-size="12">12.4</text>
<text x="170" y="326" font-size="12" text-anchor="end">valgrind none</text>
<rect x="180" y="313" width="43" height="20" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="230" y="328" font-size="12">36.0</text>
<text x="170" y="354" font-size="12" text-anchor="end">valgrind memcheck</text>
<rect x="180" y="341" width="102" height="20" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="289" y="356" font-size="12">85.4</text>
<text x="170" y="382" font-size="12" text-anchor="end">ASan 기본</text>
<rect x="180" y="369" width="436" height="20" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="560" y="384" font-size="12" text-anchor="end">366.5</text>
</svg>
```
그림 8 — valgrind vs ASan 비용 실측(같은 계산 루프, VM 호스트). 시간은 memcheck가 압도적으로 크고, 메모리는 ASan의 기본 quarantine(256 MB)이 가장 크다. quarantine을 4 MB로 줄이면 ASan의 RSS는 12 MB로 떨어진다.

출력에서 볼 것:

- **시간**: ASan 2.4배, memcheck 13.6배. `--tool=none`(검사 없이 JIT만) 자체가 2.3배라서, valgrind의 기본 비용 중 상당 부분이 "번역해서 돌리는 것" 자체다. 매뉴얼은 memcheck를 보통 10~50배로 안내한다. 이 루프는 malloc/free 비중이 커서 그 범위의 아래쪽이다.
- **메모리**: 1.3 MB짜리 프로그램이 memcheck에서 85 MB, ASan 기본 설정에서 **366 MB**. 1 GB RAM 보드에서 ASan 빌드 데몬 3개를 띄우면 OOM이 난다. `ASAN_OPTIONS=quarantine_size_mb=4`로 12 MB까지 줄었다(대신 UAF를 잡을 확률이 줄어든다 — 재사용이 빨라지므로). ASan은 shadow 영역으로 **가상 주소 공간**을 수 TB 예약하는데, 이건 RSS가 아니라서 위 숫자에 안 잡힌다. `ulimit -v`를 걸어 둔 시스템이나 32비트 ARM에서는 이 예약이 문제가 된다.
- 게스트(TCG)에서 같은 실험(N = 100,000, 3회 중 최소, load 13~22): native 0.51 s, ASan 10.8 s(21배), valgrind none 15.4 s(30배), memcheck 59.6 s(117배). 회차 간 편차가 2~5배라서 숫자 자체는 믿을 수 없지만, **에뮬레이터·느린 보드에서는 배수가 훨씬 커진다**는 것과 순서(native < ASan < memcheck)는 같았다. TSan은 게스트에서 `race`(예제 22) 하나에 31 s가 걸렸다(호스트 0.15 s).

### 7.2 레이스 — helgrind / DRD vs ThreadSanitizer

```c
/* m9 race: two threads bump a shared counter without a lock */
#include <pthread.h>
#include <stdio.h>

static long frames;                          /* shared, unprotected */

static void *rx(void *arg)
{
    (void)arg;
    for (int i = 0; i < 1000000; i++) frames++;
    return NULL;
}

int main(void)
{
    pthread_t a, b;
    pthread_create(&a, NULL, rx, NULL);
    pthread_create(&b, NULL, rx, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    printf("frames=%ld (expected 2000000)\n", frames);
    return 0;
}
```

### 예제 22 — 같은 레이스를 helgrind와 TSan으로

```sh
gcc -std=gnu11 -Wall -Wextra -O1 -g -pthread race.c -o race; ./race
valgrind --tool=helgrind ./race
gcc -std=gnu11 -Wall -Wextra -O1 -g -fsanitize=thread -pthread race.c -o race.tsan; ./race.tsan
```

```text
frames=1000000 (expected 2000000)
...
==460== Possible data race during read of size 8 at 0x128018 by thread #3
==460==    at 0x1088DC: rx (race.c:10)
==460==    by 0x4892B93: ??? (in /usr/libexec/valgrind/vgpreload_helgrind-arm64-linux.so)
==460==    by 0x495582B: start_thread (pthread_create.c:447)
==460==    by 0x49BB8DB: thread_start (clone.S:79)
==460== This conflicts with a previous write of size 8 by thread #2
...
==================
WARNING: ThreadSanitizer: data race (pid=557)
  Read of size 8 at 0xaaaac0ca0018 by thread T2:
    #0 rx /mnt/m9/src/race.c:10 (race.tsan+0xaf8) (BuildId: 5a511aeef10b210fc0ba7ac7c45f9771bd6c9b8a)

  Previous write of size 8 at 0xaaaac0ca0018 by thread T1:
    #0 rx /mnt/m9/src/race.c:10 (race.tsan+0xb24) (BuildId: 5a511aeef10b210fc0ba7ac7c45f9771bd6c9b8a)

  Location is global 'frames' of size 8 at 0xaaaac0ca0018 (race.tsan+0x20018)

  Thread T2 (tid=560, running) created by main thread at:
    #0 pthread_create ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:1022 (libtsan.so.2+0x5dd98) (BuildId: dc0d657f42587fea53114ce058dbdfe5834e0205)
    #1 main /mnt/m9/src/race.c:18 (race.tsan+0xba8) (BuildId: 5a511aeef10b210fc0ba7ac7c45f9771bd6c9b8a)
...
SUMMARY: ThreadSanitizer: data race /mnt/m9/src/race.c:10 in rx
==================
ThreadSanitizer: reported 1 warnings
frames=2000000 (expected 2000000)
```
(helgrind의 스레드 생성 안내 블록과 TSan의 T1 생성 블록은 생략했다. 게스트, 커널 6.18.55-trace.)

출력에서 볼 것:

- 그냥 돌린 `race`는 **1,000,000** — 정확히 절반이다. `-O1`의 GCC는 루프를 "frames를 한 번 읽고 1,000,000을 더해 한 번 쓰기"로 바꿨고, 두 스레드가 같은 옛 값을 읽어 한쪽 결과가 통째로 사라졌다. VM 호스트에서 같은 바이너리를 두 번 돌리니 한 번은 1,000,000, 한 번은 2,000,000이었다. **레이스는 결과가 맞을 때도 있다** — 테스트가 통과했다고 레이스가 없는 게 아니다.
- helgrind 아래에서는 결과가 2,000,000이었다(valgrind는 스레드를 하나씩 돌린다). 그래도 "read/write 충돌"은 보고한다 — happens-before 관계(락·join 등의 동기화 순서)를 추적해서, 실제로 겹치지 않았어도 **겹칠 수 있었음**을 잡기 때문이다. TSan도 같은 원리이고 결과값이 맞은 실행에서 레이스를 보고했다.
- TSan 보고: 두 접근의 스택, 변수 이름(`global 'frames'`), 스레드가 어디서 생성됐는지. 비용은 VM 호스트에서 race 기준 0.15~0.21 s / 47 MB RSS(native는 0.00 s / 1.4 MB), helgrind 0.49~0.55 s / 55~59 MB, DRD 0.53~0.56 s / 43 MB였다. 일반적으로 TSan은 5~15배 느리고 메모리를 5~10배 쓴다고 알려져 있다.
- TSan은 arm64에서 **가상 주소 비트 수**에 민감하다(39/42/48비트 VA 레이아웃을 지원). 이 게스트 커널은 48비트 VA였다. 커널을 `CONFIG_ARM64_VA_BITS`가 다른 값으로 빌드한 보드에서는 TSan이 시작조차 못 할 수 있다 — 보드에서 먼저 한 번 돌려 본다.
- ASan과 TSan은 **같이 못 쓴다**(둘 다 shadow 메모리를 다른 방식으로 쓴다). CI에서 빌드를 따로 만든다: `asan` 빌드(+UBSan), `tsan` 빌드.

### 예제 23 — massif: 힙이 어디서 크나

```c
/* m9 grow: a "cache" that never evicts -> slow heap growth */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct entry { struct entry *next; char blob[4000]; };
static struct entry *cache;

static void remember(int id)
{
    struct entry *e = malloc(sizeof *e);
    if (!e) { perror("malloc"); exit(1); }
    memset(e->blob, id, sizeof e->blob);
    e->next = cache;                         /* never freed */
    cache = e;
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 200;
    int us = argc > 2 ? atoi(argv[2]) : 0;
    for (int i = 0; i < n; i++) {
        remember(i);
        char *tmp = malloc(1000);            /* transient, freed */
        if (!tmp) { perror("malloc"); exit(1); }
        free(tmp);
        if (us) usleep(us);
    }
    printf("cached %d entries\n", n);
    if (us) pause();
    return 0;
}
```

```sh
valgrind --tool=massif --massif-out-file=massif.out ./grow 300
ms_print massif.out | head -40         # 그래프
ms_print massif.out > ms.txt            # 피크 스냅샷의 할당 트리
```

```text
    MB
1.151^                                                                       :
     |                                                                    @@#:
     |                                                                  @@@@#:
     |                                                                @@@@@@#:
...
     |                                     @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@#:
     |                                   :@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@#:
   0 +----------------------------------------------------------------------->ki
     0                                                                   239.2

 82        238,338        1,204,192        1,199,392         4,800            0
99.60% (1,199,392B) (heap allocation functions) malloc/new/new[], --alloc-fns, etc.
->99.52% (1,198,392B) 0x108ABF: remember (grow.c:12)
| ->99.52% (1,198,392B) 0x108ABF: main (grow.c:24)
|   
->00.08% (1,000B) in 1+ places, all below ms_print's threshold (01.00%)
```

출력에서 볼 것: x축은 시간이 아니라 **실행한 명령 수(ki = 천 개)** 다. 그래프는 단조 증가하고, 피크 스냅샷(82번)의 99.5%가 `remember (grow.c:12)`에서 온다. 1000바이트짜리 임시 할당은 1% 미만 — 할당 **횟수**가 많아도 **살아남는 양**이 아니면 massif에서는 안 보인다(횟수는 `--tool=dhat`이 본다). `extra-heap` 4,800 B는 malloc 헤더·정렬 오버헤드다(엔트리당 16 B). memcheck로는 이 프로그램이 still reachable로만 나온다 — 끝에 cache가 아직 전역에서 닿기 때문이다. "누수 검사기는 깨끗한데 메모리가 는다"의 전형이다.

### 7.3 그 밖의 valgrind 도구 (가이드, 이번에 실행 안 함)

- `--tool=cachegrind`: 명령 수(와 옵션으로 캐시 시뮬레이션)를 함수·줄 단위로. valgrind 3.21부터 캐시 시뮬레이션은 기본으로 꺼져 있다(`--cache-sim=yes`). 실제 하드웨어 카운터가 있으면 perf(M6)가 낫다.
- `--tool=callgrind` + KCachegrind: 호출 그래프 프로파일.
- `--tool=dhat`: 할당 횟수·수명·접근 패턴.
- `--vgdb=yes --vgdb-error=0`: valgrind가 gdbserver 역할을 해서, 에러가 난 순간 gdb로 붙어 들여다볼 수 있다.
- UBSan(`-fsanitize=undefined`): 부호 있는 정수 오버플로, 정렬 안 된 포인터, 잘못된 shift, 배열 경계(`-fsanitize=bounds`). 비용이 작아 ASan 빌드에 같이 켜는 것이 보통이다. 센서 데이터 처리에서 `int16_t` 변환·shift 버그를 잘 잡는다.

---

## 8. "메모리가 며칠에 걸쳐 는다" — 현장 접근

valgrind·ASan은 재현 환경 도구다. 현장 장비에서는 먼저 **숫자를 기록**한다.

| 파일 | 무엇을 보나 |
|---|---|
| `/proc/PID/status` | `VmRSS`(상주), `RssAnon`(익명 = 힙·스택), `RssFile`, `VmSwap`, `VmHWM`(최고치) |
| `/proc/PID/smaps_rollup` | 프로세스 전체의 Rss·Pss(공유를 나눠 낸 값)·Private_Dirty·Anonymous 합계 — 한 번 읽기가 싸다 |
| `/proc/PID/smaps` | 매핑마다 같은 항목 → 어느 영역(`[heap]`, 익명 mmap, 특정 .so)이 크나 |
| `/proc/PID/maps` | 영역 경계 — `[heap]` 끝이 계속 커지나, 익명 mmap이 늘어나나 |
| `/proc/meminfo`, `/sys/fs/cgroup/.../memory.current` | 시스템·서비스 단위 (L1의 `MemoryMax=`) |

### 예제 24 — 자라는 프로세스를 관찰하고 gcore로 떠 두기

무엇을 확인하나: `grow`(엔트리당 4 KB, 10 ms마다 하나)가 도는 동안 4초 간격으로 RSS와 `[heap]` 경계를 기록하고, 죽이기 전에 core를 떠 둔다.

```sh
setsid ./grow 1500 10000 > /dev/null 2>&1 &  ; sleep 1; P=$(pidof grow)
for i in 1 2 3 4; do echo "t=$((i*4))s $(grep -E "VmRSS|RssAnon" /proc/$P/status | tr -s " \t" " " | tr "\n" " ") heap=$(grep "\[heap\]" /proc/$P/maps | cut -d" " -f1)"; sleep 4; done
grep -A3 "\[heap\]" /proc/$P/smaps | head -4
grep -E "^(Rss|Pss|Private_Dirty|Anonymous)" /proc/$P/smaps_rollup
gcore -o /root/m9/grow.gcore $P | tail -1; ls -l /root/m9/grow.gcore.*; kill $P
```

```text
t=4s VmRSS: 2048 kB RssAnon: 1052 kB  heap=aaab153a8000-aaab154d1000
t=8s VmRSS: 3808 kB RssAnon: 2812 kB  heap=aaab153a8000-aaab156c0000
t=12s VmRSS: 5692 kB RssAnon: 4696 kB  heap=aaab153a8000-aaab1584c000
t=16s VmRSS: 7416 kB RssAnon: 5972 kB  heap=aaab153a8000-aaab15975000
aaab153a8000-aaab15975000 rw-p 00000000 00:00 0                          [heap]
Size:               5940 kB
KernelPageSize:        4 kB
MMUPageSize:           4 kB
Rss:                7416 kB
Pss:                6058 kB
Pss_Dirty:          5972 kB
Pss_Anon:           5972 kB
Pss_File:             86 kB
Pss_Shmem:             0 kB
Private_Dirty:      5972 kB
Anonymous:          5972 kB
[Inferior 1 (process 630) detached]
-rw-r--r-- 1 root root 6341256 Oct 18 07:25 /root/m9/grow.gcore.630
```

출력에서 볼 것:

- `RssAnon`이 4초에 약 1.8 MB씩 일정하게 늘고, `[heap]`의 끝 주소(`brk`)가 같이 밀려난다. 늘어나는 것이 **익명·private dirty 힙**이라는 것까지 `/proc`만으로 알았다. 파일 캐시·공유 라이브러리가 아니다.
- 128 KB 이상의 큰 할당은 glibc가 `[heap]` 대신 별도 익명 `mmap`으로 받는다(`M_MMAP_THRESHOLD`). 그 경우는 `[heap]`이 아니라 이름 없는 `rw-p` 매핑 개수가 늘어난다. 스레드별 arena(`64 MB` 단위 익명 매핑)도 같은 모양이다.
- `gcore`는 프로세스를 잠깐 멈추고 core를 쓴 뒤 **detach해서 계속 돌게** 한다. 6.3 MB core를 PC로 가져가 gdb로 `p cache->next->blob[0]`처럼 자료구조를 따라가면 "무엇이 쌓였나"(어떤 id, 어떤 크기)를 본다. 큰 힙이면 core를 `strings`/`hexdump`로 훑어 반복 패턴을 찾는 것도 실전에서 통한다.
- 실전 순서: (1) 시간당 증가율을 `smaps_rollup`으로 며칠 기록(로그로 남기거나 메트릭으로 송신) → (2) 증가가 `[heap]`/익명인지, 특정 `.so`의 매핑인지, fd·스레드 수(`ls /proc/PID/fd`, `Threads:`)인지 구분 → (3) 재현 환경에서 massif / heaptrack / LSan → (4) 의심 경로에 카운터를 단다. 펌웨어 시절 "free block 수를 주기 로그로 남기던 것"과 같은 생각이다.
- **누수가 아닐 수도 있다**: glibc malloc은 free된 메모리를 OS에 바로 돌려주지 않는다(단편화, arena). RSS가 계단식으로 오르다 멈추면 정상일 수 있다. `malloc_trim(0)`을 불러 보거나 `MALLOC_ARENA_MAX=2`로 arena 수를 줄여 보면 구분된다.

---

## 9. systemd 아래의 데몬 디버깅

실제 장비의 앱은 셸이 아니라 systemd 서비스로 돈다(L1). 다른 점: stdout이 저널로 가고, 환경 변수·cgroup·보안 옵션(`ProtectSystem=`, `NoNewPrivileges=`, seccomp `SystemCallFilter=`)이 다르고, 죽으면 재시작된다.

### 예제 25 — 서비스에 붙고, 죽이고, `coredumpctl gdb`

무엇을 확인하나: `systemd-run`으로 임시 서비스를 띄우고, MainPID에 gdb로 붙고, SIGSEGV로 죽인 뒤 core를 바로 gdb로 연다.

```sh
systemd-run --unit=m9-sensord -p LimitCORE=infinity /root/m9/sensord.full
P=$(systemctl show -p MainPID --value m9-sensord); echo MainPID=$P
gdb -q -batch -nx -p $P -ex "info threads" | grep -E "^\*? +[0-9] +Thread"
systemctl kill -s SEGV m9-sensord
systemctl status --no-pager m9-sensord | grep -E "Active|Main PID|code="     # 덤프가 끝난 뒤
coredumpctl list --no-pager | tail -2
coredumpctl gdb --no-pager sensord.full --debugger-arguments="-batch -ex \"thread apply all bt 3\""
```

```text
Running as unit: m9-sensord.service; invocation ID: a5d8e408bb0e46ffb748377e630f17d9
MainPID=1396
* 1    Thread 0xffff8bc19ee0 (LWP 1396) "sensord.full" 0x0000ffff8baa1d6c in __futex_abstimed_wait_common64 (...) at ./nptl/futex-internal.c:57
  2    Thread 0xffff8b20f1a0 (LWP 1402) "sensord.full" 0x0000ffff8baa1d6c in __futex_abstimed_wait_common64 (... futex_word=0xaaaab4c1007c <cv+44>) at ./nptl/futex-internal.c:57
  3    Thread 0xffff8ba1f1a0 (LWP 1401) "sensord.full" 0x0000ffff8badbd90 in __GI___clock_nanosleep (...) at ../sysdeps/unix/sysv/linux/clock_nanosleep.c:78
     Active: failed (Result: core-dump) since Sun 2026-10-18 07:34:38 UTC; 19s ago
    Process: 1396 ExecStart=/root/m9/sensord.full (code=dumped, signal=SEGV)
   Main PID: 1396 (code=dumped, signal=SEGV)
Sun 2026-10-18 06:22:47 UTC  717   0   0 SIGSEGV present  /root/m9/crash         11.5K
Sun 2026-10-18 07:34:36 UTC 1396   0   0 SIGSEGV present  /root/m9/sensord.full 108.0K
Core was generated by `/root/m9/sensord.full'.
Program terminated with signal SIGSEGV, Segmentation fault.
#0  0x0000ffff8baa1d6c in __futex_abstimed_wait_common64 (private=128, cancel=true, abstime=0x0, op=265, expected=1401, futex_word=0xffff8ba1f270) at ./nptl/futex-internal.c:57
Thread 3 (Thread 0xffff8b20f1a0 (LWP 1402)):
#0  0x0000ffff8baa1d6c in __futex_abstimed_wait_common64 (...) at ./nptl/futex-internal.c:57
...
Thread 2 (Thread 0xffff8ba1f1a0 (LWP 1401)):
#0  0x0000ffff8badbd90 in __GI___clock_nanosleep (...) at ../sysdeps/unix/sysv/linux/clock_nanosleep.c:78
#1  0x0000ffff8bae60bc in __GI___nanosleep (req=req@entry=0xffff8ba1e878, rem=rem@entry=0x0) at ../sysdeps/unix/sysv/linux/nanosleep.c:25
#2  0x0000ffff8bb0b5b8 in usleep (useconds=useconds@entry=100000) at ../sysdeps/posix/usleep.c:31
Thread 1 (Thread 0xffff8bc19ee0 (LWP 1396)):
...
```
(긴 인자 목록은 `...`로 줄였다.)

출력에서 볼 것:

- **서비스로 돌 때는 MainPID로 붙는다.** `systemctl show -p MainPID --value`가 스크립트에 쓰기 좋다.
- `systemctl kill -s SEGV` 직후의 status는 아직 `active (running)`이었다. 커널이 core를 systemd-coredump에 파이프로 넘기고, systemd-coredump가 압축해서 저장을 마칠 때까지 **죽은 프로세스가 회수되지 않는다**(L7 §7의 `core_pipe_limit`). 이 TCG 게스트에서는 그게 약 60초 걸렸다. 그 동안 서비스 재시작도 지연된다 — 큰 프로세스의 core는 장비의 재시작 시간을 늘린다.
- `coredumpctl gdb <match>`가 저장된 core를 꺼내 gdb를 바로 띄운다. `--debugger-arguments`로 배치 명령을 넘기면 스크립트로 쓸 수 있다. `--debugger=`로 `gdb-multiarch` 등을 지정할 수도 있다.
- `-p LimitCORE=infinity`: 이 rootfs는 파이프 방식이라 RLIMIT_CORE가 무시되지만, 서비스 쪽 한도는 `coredump.conf`의 크기 제한과 함께 확인하는 습관을 들인다(L7 예제 10).

### 9.1 systemd 서비스 디버깅 함정 (가이드)

- **watchdog**: `WatchdogSec=`를 쓰는 서비스는 gdb에서 breakpoint에 멈춰 있는 동안 `sd_notify("WATCHDOG=1")`를 못 보내서 systemd가 **SIGABRT로 죽인다**. 디버깅할 때는 `systemctl edit`로 `WatchdogSec=0`을 넣거나 `systemctl set-property --runtime`(지원되는 속성일 때)로 끈다. HW watchdog(I9)를 데몬이 직접 쓰면 보드가 리셋된다 — 펌웨어 시절과 똑같은 함정이다.
- **처음부터 gdbserver 아래에서 띄우기**: drop-in으로 `ExecStart=` 를 비우고 `ExecStart=/usr/bin/gdbserver localhost:2391 /usr/bin/sensord`로 바꾸고 `TimeoutStartSec=infinity`. `Type=notify` 서비스라면 gdbserver 아래에서도 notify가 오도록 `NotifyAccess=all`이 필요할 수 있다.
- **환경 차이 재현**: `systemd-run --pty -p ProtectSystem=strict -p User=sensor ...`로 같은 샌드박스 옵션을 주고 셸에서 실행해 본다. "셸에서는 되는데 서비스로는 안 된다"의 대부분은 권한·경로·환경 변수다. strace -f로 EACCES·EPERM을 찾는다(`SystemCallFilter=` 위반은 SIGSYS로 죽는다).
- **ptrace 권한**: 서비스가 `NoNewPrivileges`, 다른 사용자로 돌면 Yama `ptrace_scope`(L6)에 따라 root만 붙을 수 있다. 출시 장비에서 `ptrace_scope=2`(관리자만)나 3(아예 금지)로 잠그는 것도 정책이다.

---

## 10. 커널 안에서는 무슨 일이 일어나나

이 노트의 거의 모든 도구는 커널의 세 가지 기능 위에 서 있다: **ptrace**(gdb, gdbserver, strace, ltrace), **hw_breakpoint**(watchpoint), **coredump**(core, gcore 제외 — gcore는 gdb가 ptrace로 메모리를 읽어 직접 쓴다). valgrind와 sanitizer는 커널 도움 없이 **유저 공간에서** 일한다(valgrind는 시그널·syscall을 가로채 흉내 낼 뿐이다).

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="20" y1="120" x2="660" y2="120" stroke="#888" stroke-dasharray="5,4"/>
<text x="650" y="112" font-size="12" text-anchor="end">유저</text>
<text x="650" y="138" font-size="12" text-anchor="end">커널</text>
<rect x="20" y="20" width="150" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="95" y="44" font-size="13" text-anchor="middle">gdb · strace</text>
<text x="95" y="64" font-size="12" text-anchor="middle">ptrace(SEIZE,</text>
<text x="95" y="80" font-size="12" text-anchor="middle">SYSCALL, GETREGSET)</text>
<rect x="250" y="20" width="150" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="325" y="44" font-size="13" text-anchor="middle">디버기 스레드</text>
<text x="325" y="64" font-size="12" text-anchor="middle">BRK #0 · svc #0</text>
<text x="325" y="80" font-size="12" text-anchor="middle">load/store</text>
<rect x="470" y="20" width="160" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="550" y="44" font-size="13" text-anchor="middle">systemd-coredump</text>
<text x="550" y="64" font-size="12" text-anchor="middle">stdin 으로 core 받기</text>
<rect x="20" y="150" width="150" height="80" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="95" y="172" font-size="12" text-anchor="middle">kernel/ptrace.c</text>
<text x="95" y="190" font-size="12" text-anchor="middle">ptrace_attach()</text>
<text x="95" y="208" font-size="12" text-anchor="middle">arch_ptrace() regset</text>
<text x="95" y="224" font-size="12" text-anchor="middle">task_struct.ptrace</text>
<rect x="190" y="150" width="140" height="80" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="260" y="172" font-size="12" text-anchor="middle">el0_brk64()</text>
<text x="260" y="190" font-size="12" text-anchor="middle">→ SIGTRAP</text>
<text x="260" y="208" font-size="12" text-anchor="middle">syscall_trace_</text>
<text x="260" y="224" font-size="12" text-anchor="middle">enter() 정지</text>
<rect x="345" y="150" width="140" height="80" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="415" y="172" font-size="12" text-anchor="middle">hw_breakpoint.c</text>
<text x="415" y="190" font-size="12" text-anchor="middle">WVR/WCR 레지스터</text>
<text x="415" y="208" font-size="12" text-anchor="middle">thread_switch 때</text>
<text x="415" y="224" font-size="12" text-anchor="middle">교체 (스레드별)</text>
<rect x="500" y="150" width="160" height="80" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="580" y="172" font-size="12" text-anchor="middle">get_signal()</text>
<text x="580" y="190" font-size="12" text-anchor="middle">→ vfs_coredump()</text>
<text x="580" y="208" font-size="12" text-anchor="middle">→ elf_core_dump()</text>
<text x="580" y="224" font-size="12" text-anchor="middle">coredump_filter</text>
<line x1="95" y1="90" x2="95" y2="148" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="300" y1="90" x2="270" y2="148" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="360" y1="90" x2="405" y2="148" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="560" y1="150" x2="560" y2="92" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="20" y="260" width="640" height="140" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="40" y="284" font-size="12">SW breakpoint = 텍스트에 BRK 명령 쓰기 (POKEDATA 또는 /proc/PID/mem)</text>
<text x="40" y="306" font-size="12">→ 그 프로세스의 private 사본만 바뀐다 (COW). 같은 .so 를 쓰는 다른 프로세스는 영향 없음</text>
<text x="40" y="328" font-size="12">hw watchpoint = NT_ARM_HW_WATCH regset → perf 이벤트 → 스레드별 레지스터</text>
<text x="40" y="350" font-size="12">strace --seccomp-bpf = seccomp 필터의 SECCOMP_RET_TRACE → PTRACE_EVENT_SECCOMP 로만 깨어남</text>
<text x="40" y="372" font-size="12">/proc/PID/task/TID/stack · wchan = 커널 스택 그대로 (예제 5) → 드라이버 wait 지점</text>
<text x="40" y="392" font-size="12">valgrind · ASan · TSan = 커널 기능 없이 유저 공간에서 계측 (그래서 어떤 커널에서도 돈다)</text>
</svg>
```
그림 9 — 유저스페이스 디버깅 도구가 기대는 커널 경로. 왼쪽부터 ptrace(파랑), 디버기의 예외·syscall 정지와 hw watchpoint(주황), core dump(초록).

- **ptrace 연결**: `ptrace(PTRACE_SEIZE/ATTACH)` → `ptrace_attach()`(kernel/ptrace.c)가 디버기의 `task_struct->ptrace`에 플래그를 세우고 tracer를 기록한다. 이후 디버기에 오는 시그널은 전달 전에 `ptrace_signal()`에서 멈추고 tracer에 `waitpid()`로 통보된다. 레지스터는 `PTRACE_GETREGSET`(`NT_PRSTATUS`, FP/SIMD는 `NT_PRFPREG`)로, 메모리는 `PTRACE_PEEKDATA`나 `/proc/PID/mem`·`process_vm_readv()`로 읽는다.
- **소프트웨어 breakpoint**: gdb가 해당 주소에 arm64 `BRK` 명령을 써 넣는다. 실행되면 EL0 동기 예외 → `el0_brk64()`(arch/arm64/kernel/entry-common.c) → SIGTRAP → ptrace 정지. 텍스트 페이지는 private 매핑이라 쓰는 순간 COW로 그 프로세스 사본만 바뀐다.
- **syscall 정지**: `PTRACE_SYSCALL`이면 디버기 스레드에 syscall trace 플래그가 서고, arm64의 `syscall_trace_enter()`(arch/arm64/kernel/ptrace.c)가 `ptrace_report_syscall_entry()`로 멈춘다(복귀 때는 exit 쪽 대응 함수). strace의 두 번 정지가 이것이다. seccomp 필터가 있으면 `__secure_computing()`이 먼저 돌고, `SECCOMP_RET_TRACE`일 때만 tracer를 깨운다.
- **hw watchpoint**: gdb가 `NT_ARM_HW_WATCH` regset을 쓰면 커널은 `register_user_hw_breakpoint()`로 perf 이벤트를 만들고, arch/arm64/kernel/hw_breakpoint.c가 DBGWVR/DBGWCR 레지스터를 프로그래밍한다. 스레드 전환마다 `hw_breakpoint_thread_switch()`가 값을 교체하므로 watchpoint는 **스레드 단위**다(gdb는 모든 스레드에 같은 값을 넣어 프로세스 단위처럼 보여 준다). 레지스터 수(이 CPU는 4개)를 넘으면 등록이 실패한다(예제 8).
- **core dump**: 치명 시그널의 기본 동작이 "core"이면 `get_signal()`이 `vfs_coredump()`(fs/coredump.c, 6.18 이름 — 예전에는 `do_coredump()`)를 부르고, ELF 실행 파일이면 `binfmt->core_dump` = `elf_core_dump()`(fs/binfmt_elf.c)가 note(레지스터·siginfo·NT_FILE)와 VMA를 쓴다. 어떤 VMA를 쓸지는 `mm->flags`의 coredump filter 비트가 정한다(예제 12).
- **드라이버와 다시 만나는 곳**: 데몬이 "멈췄다"면 예제 5처럼 `/proc/PID/task/*/stack`을 본다. 거기에 `wait_event_interruptible` → 내 드라이버의 `.read`(G3)가 보이면 유저가 아니라 **드라이버가 안 깨우는 것**이다(인터럽트 핸들러가 `wake_up()`을 안 부름, 조건 변수 갱신 누락 — F 모듈). `strace -e trace=ioctl,read -T`로 어느 ioctl이 몇 ms 막히는지 보는 것도 드라이버 디버깅의 첫 단계다(A3, A10). 커널 쪽 자체를 멈춰서 보는 것은 M4(kgdb·JTAG), 커널 함수 흐름은 M5(ftrace), 커널 메모리 버그 검사기는 M8(KASAN = ASan의 커널판, KCSAN = TSan의 커널판, kmemleak = LSan의 커널판)이다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트 — "보드에서만 죽는다"

i.MX8 보드의 센서 퓨전 데몬이 하루에 한두 번 SIGSEGV. PC의 x86 빌드에서는 재현이 안 된다. 순서:

1. **증거부터**: `coredumpctl list`로 core가 있는지, `coredumpctl info`로 시그널·스레드·저널 스택(주소)을 본다. core가 없으면 `coredump.conf`·`coredump_filter`(0x33)·디스크 여유부터 고친다(L7).
2. **같은 빌드의 debug 파일**: CI의 symbol store에서 build-id로 꺼낸다. 없다면 이 장애에서 가장 먼저 고칠 것은 빌드 파이프라인이다.
3. **PC에서 분석**: `gdb-multiarch -ex "set sysroot <Yocto SDK sysroot>" -ex "set debug-file-directory <store>" sensord core` → `bt`, `info threads`, `p $_siginfo`.
4. **타깃 차이 목록**: 아키텍처(정렬 — arm64 유저 공간은 비정렬 접근을 대부분 허용하지만 원자 연산·Device 메모리는 아니다, `char`의 부호 — ARM은 기본 unsigned), 엔디언 가정, 메모리 순서(arm64는 x86보다 약하다 → 락 없는 링버퍼에서 barrier 누락이 ARM에서만 터진다), 타이밍(느린 CPU에서만 나는 레이스), 라이브러리 버전(sysroot), 커널 기능(`ENOSYS`), 메모리 부족.
5. **재현 환경**: 타깃용 ASan 빌드를 보드에 올려 돌린다(라이브러리 `libasan.so` 동봉, quarantine 줄이기). 레이스가 의심되면 TSan 빌드(VA 비트 확인). 재현이 되면 gdbserver + watchpoint로 마무리.

### 11.2 카메라 장비 — 일주일에 RSS 40 MB 증가

Verkada 같은 카메라의 스트리밍 데몬. 매시간 `smaps_rollup`을 메트릭으로 보내 증가율을 본다. `[heap]`이 아니라 익명 mmap 개수가 늘면 큰 버퍼(프레임)·스레드 arena 쪽이고, fd 수가 같이 늘면 소켓·dmabuf fd 누수다(`ls -l /proc/PID/fd`에서 `anon_inode:dmabuf`가 쌓인다 — I 모듈의 V4L2/DRM 버퍼). 랩에서 같은 스트림을 재생해 massif·heaptrack으로 확인한다. ASan/LSan 빌드로 오래 돌리면 프로세스 종료 시 누수 스택이 나온다.

### 11.3 로봇 제어 루프 — 디버거가 문제를 바꾼다

1 kHz 모터 제어 스레드(N2의 SCHED_FIFO)에 breakpoint를 걸면 루프가 멈추고, CAN 버스 상대(모터 컨트롤러)는 타임아웃으로 안전 정지한다. 그래서 실시간 경로는 **멈추지 않는 도구**로 본다: `dprintf`, non-stop 모드, uprobe·tracepoint(M7, M5), GPIO 토글 + 스코프(M11). strace도 그 스레드에는 붙이지 않는다(§5의 100배 오버헤드). 사후 분석은 core와 링버퍼 로그(펌웨어 시절 trace buffer와 같은 생각)로 한다.

### 11.4 "설정을 바꿨는데 안 먹는다"

현장 지원에서 제일 흔한 문의다. `strace -f -Z -e trace=%file -p $(pidof app)`로 재로드(SIGHUP) 순간 앱이 **실제로 여는 경로**를 본다 — 대부분 다른 경로(`/etc` vs `/usr/share` vs overlay 상위층 J 모듈), 권한(EACCES), 또는 아예 다시 열지 않는다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 원격 디버깅에서 sysroot를 PC의 `/`로 둠 | libc 프레임 이름이 엉뚱함, "corrupt stack?" | PC와 보드의 라이브러리 버전이 다름 (예제 3) | 보드 rootfs와 같은 빌드의 sysroot, 또는 `target:` |
| 출시 빌드의 debug 파일을 안 남김 | core가 와도 `?? ()`만 | 다시 빌드하면 build-id가 다름 (예제 12) | CI에서 build-id 경로로 보관, debuginfod |
| build-id 경고를 무시 | 그럴듯하지만 틀린 bt | "core file may not match" | 같은 build-id 파일을 찾을 때까지 결론 보류 |
| 절대 주소를 그대로 addr2line | 엉뚱한 줄 또는 `??` | PIE/ASLR 기준 주소를 안 뺌, LR에서 4를 안 뺌 | 매핑 표에서 기준 주소 확인 (예제 11) |
| strace를 고속 데이터 경로에 상시 부착 | 프레임 드롭, 타이밍 버그가 사라지거나 생김 | syscall마다 2회 정지 (100배 이상) | `--seccomp-bpf -f`, 짧게, 또는 perf trace/bpftrace |
| `-e trace=`로 걸렀으니 가볍다고 믿음 | 여전히 느림 | 기본 방식은 모든 syscall에서 멈춤 (예제 16) | `--seccomp-bpf` |
| ltrace가 아무것도 안 보여 줌 → 호출이 없다고 결론 | 잘못된 결론 | 정적 링크, `-fno-plt`, 인라인된 호출 (예제 17) | uprobe, gdb, LD_PRELOAD |
| ASan 빌드를 작은 보드에서 그대로 실행 | OOM, 시작 실패 | quarantine 256 MB + shadow 예약 | `ASAN_OPTIONS=quarantine_size_mb=...`, `ulimit -v` 해제 |
| valgrind로 레이스 재현을 기대 | 결과가 늘 맞게 나옴 | valgrind는 스레드를 직렬화 | helgrind/TSan의 보고를 본다 (예제 22) |
| WatchdogSec 서비스를 breakpoint에 세움 | 디버깅 중 SIGABRT로 죽음 | sd_notify 끊김 | 디버깅 동안 `WatchdogSec=0` |
| gdbserver 포트를 0.0.0.0에 엶 | 누구나 원격 코드 실행 | RSP에 인증 없음 | localhost + SSH 터널, 출시 이미지에서 제거 |
| 오프라인 장비에서 `DEBUGINFOD_URLS`가 설정됨 | eu-unstrip·gdb가 멈춘 듯 대기 | 서버 조회 대기 | `unset DEBUGINFOD_URLS`, `set debuginfod enabled off` |

---

## 13. 면접에서 이렇게 말한다

**Q.** "An application crashes only on the target board, never on your PC. How do you debug it?"

**A.** 먼저 증거를 확보한다: core dump가 남게 설정하고(coredump 정책·filter), 크래시 때의 build-id를 남긴다. 그 build-id로 CI에 보관한 debug 파일을 꺼내 PC에서 sysroot를 맞춰 gdb로 core를 연다. 동시에 타깃과 PC의 차이 — 아키텍처(정렬·char 부호·약한 메모리 순서), 라이브러리 버전, 커널 기능, 타이밍, 메모리 — 를 목록으로 놓고 좁힌다. 재현이 되면 타깃용 ASan/TSan 빌드와 gdbserver + watchpoint로 마무리한다.

> First I make sure the target leaves evidence: core dumps enabled with a size policy, and the build-id recorded. With the build-id I pull the exact debug files from our symbol store and open the core on my PC with gdb-multiarch, with the sysroot pointing at the same rootfs build the board runs — otherwise the libc frames are wrong. That usually gives me the faulting thread and line. In parallel I list what differs between the board and the PC: the architecture — alignment, signed versus unsigned char, ARM's weaker memory ordering for lock-free code — library versions, kernel features, timing on a slower CPU, and memory pressure. If I can reproduce it, I run an ASan or TSan build on the board, and for memory corruption I use gdbserver with a hardware watchpoint on the variable that gets corrupted.

**꼬리 질문**: "core가 없다면?" → systemd-coredump 설정, `coredump_filter`, 디스크 공간, 그리고 다음 번을 위해 크래시 핸들러(시그널 핸들러에서 `backtrace()`는 async-signal-safe하지 않으니 별도 프로세스·core 권장)와 크래시 직전 링버퍼 로그.

**Q.** "Memory usage of a daemon grows slowly over days. How do you approach it?"

**A.** 누수라고 단정하지 않고 먼저 측정한다. 현장에서는 `/proc/PID/smaps_rollup`과 `status`를 주기적으로 기록해서 무엇이 느는지(익명 힙인지, 파일 매핑인지, 특정 매핑인지)와 증가율을 본다. fd·스레드 수도 같이 본다. 그다음 재현 환경에서 massif나 heaptrack으로 어느 호출 경로의 메모리가 살아남는지 보고, LSan/memcheck로 진짜 누수인지 본다. "still reachable"인 캐시 무한 증가, glibc arena 단편화도 후보다.

> I don't assume it's a leak; I measure first. On the device I log smaps_rollup and the status fields every hour, so I know whether it's anonymous heap, file-backed mappings or one specific mapping, and the growth rate. I also track the fd count and the thread count. Then in a lab setup I replay the same workload under massif or heaptrack to see which allocation path keeps memory alive, and under LeakSanitizer or memcheck to see whether it's a true leak. Very often it isn't — it's a cache with no eviction, which memcheck reports only as still reachable, or malloc arena fragmentation, which I can test with MALLOC_ARENA_MAX or malloc_trim. And if I need a snapshot from the field, gcore takes a core without killing the process.

**Q.** "How does remote debugging with gdbserver work, and what is a sysroot for?"

**A.** 보드에서는 gdbserver가 ptrace로 대상 프로세스를 조종하고 레지스터·메모리·breakpoint만 다룬다. PC의 gdb가 RSP로 붙어서 심볼·DWARF·unwind를 다 한다. 디버기는 보드의 공유 라이브러리를 쓰므로 gdb도 똑같은 파일이 필요한데, 그 보드 rootfs 사본이 sysroot다. 틀리면 라이브러리 심볼이 어긋나 bt가 깨진다.

> On the board, gdbserver controls the process through ptrace and only handles registers, memory, breakpoints and threads, so the board doesn't need symbols or sources. The gdb on my PC connects over the remote serial protocol, usually TCP, and does all the symbol, DWARF and unwinding work. The process uses the board's shared libraries, so gdb has to read exactly the same files; the sysroot is a copy of the board's root filesystem — typically the Yocto SDK sysroot or Buildroot's staging directory. If it's wrong, gdb resolves libc addresses with the wrong library and the backtrace breaks; the default target: sysroot fetches files over the connection, which is correct but slow.

**Q.** "valgrind or AddressSanitizer — which do you use and why?"

**A.** 재빌드할 수 있으면 ASan이 기본이다: 2배 남짓 느리고 실제 멀티스레드로 돌고 stack·global 넘침도 잡는다. 대신 메모리를 많이 쓴다(quarantine). valgrind는 재빌드가 안 될 때, 초기화 안 된 값 사용을 찾을 때 쓴다. 10배 이상 느리고 스레드를 직렬화한다. 내가 잰 숫자로는 ASan 2.4배, memcheck 13.6배였다.

> If I can rebuild, AddressSanitizer is my default: in my measurement it was about 2.4 times slower, against 13.6 times for memcheck, it runs threads truly in parallel, and it also catches stack and global overflows. Its cost is memory — the default quarantine alone is 256 MB, so on a small board I shrink it. I use valgrind when I can't rebuild, for third-party binaries, or when I'm hunting uninitialised reads, which ASan doesn't detect. For races I use ThreadSanitizer in a separate build, since it can't be combined with ASan.

**Q.** "What's the overhead of strace and how do you limit it?"

**A.** ptrace 기반이라 syscall마다 진입·복귀 두 번 멈춘다. 내 측정에서 getppid가 160 ns에서 18~74 µs로, 100배 이상 느려졌다. `-e`로 걸러도 기본 방식은 모든 syscall에서 멈추니 거의 그대로다. `--seccomp-bpf`를 쓰면 관심 syscall만 멈춰서 오버헤드가 사라진다. 운영 장비에서는 짧게 붙이거나 perf trace, bpftrace 같은 tracepoint 기반 도구를 쓴다.

> strace uses ptrace, so the traced thread stops twice per system call, on entry and exit, and the strace process wakes up each time. I measured a getppid loop going from about 160 nanoseconds per call to 18 to 74 microseconds — more than a hundred times slower. Filtering with -e doesn't help much because it still stops on every syscall; with --seccomp-bpf the kernel's seccomp filter only stops the ones I asked for, and the overhead was gone. On a production device I attach only briefly, or I use tracepoint-based tools like perf trace or bpftrace.

**Q.** "A process is hung. What do you collect before you kill it?"

**A.** `ps -L`로 스레드 상태와 wchan, CPU 사용률(도는지 자는지)을 보고, `/proc/PID/task/*/stack`으로 커널 스택, `gdb -p PID -batch -ex "thread apply all bt"`로 유저 스택을 뜬다. 가능하면 `gcore`로 core를 남긴다. 데드락이면 mutex의 owner 필드로 순환을 증명한다.

> First whether it's spinning or sleeping: ps with threads shows each thread's state, CPU and wchan. Then the kernel stacks from `/proc/PID/task/*/stack`, and the user stacks with gdb attached in batch mode running thread apply all bt — that's the classic pstack. If I can afford a pause I take a gcore so I can keep analysing after a restart. For a deadlock, glibc mutexes record the owner TID, so printing the two mutexes shows the cycle directly — thread A holds the bus lock and waits for the config lock, thread B the other way round.

**Q.** "How do hardware watchpoints work on ARM, and what are the limits?"

**A.** 코어의 watchpoint 레지스터(주소 + 제어)에 감시 주소를 넣으면 그 주소 접근 시 debug 예외가 난다. Linux에서는 ptrace regset으로 요청하고 커널이 스레드 전환 때 값을 바꿔 끼운다. 개수가 적고(Cortex-A57은 4개), 하나가 정렬된 8바이트까지만 본다. 넘으면 소프트웨어 watchpoint로 떨어져 매우 느려진다.

> The core has a few watchpoint register pairs — an address and a control register with a byte mask and read/write bits. When a matching access happens, the CPU raises a debug exception and the kernel turns it into a SIGTRAP for the debugger. On Linux, gdb sets them through a ptrace regset, and the kernel saves and restores them per thread on context switches. The limits are the count — this Cortex-A57 reported four — and the size: one register covers an aligned doubleword at most. Beyond that gdb has to single-step and compare in software, which is orders of magnitude slower.

---

## 14. 직접 해보기

1. **개념**: gdbserver를 띄운 프로세스와 그냥 띄운 프로세스의 `info proc mappings` 주소가 다른 이유는? 이 차이 때문에 재현이 안 되는 버그는 어떤 종류인가?
   정답/힌트: gdbserver/gdb가 ASLR을 끈다(`--no-disable-randomization`). 주소 의존 버그(초기화 안 된 포인터 값, 해시 순서, 정렬 우연)가 사라질 수 있다.
2. **코드**: 예제 2에서 sysroot에 게스트의 `/usr/lib/debug/.build-id`를 추가 복사하고 다시 실행해, libc 프레임의 `?? ()`가 이름으로 바뀌는지 확인하라. `info sharedlibrary`의 `(*)`도 사라지는가?
   정답/힌트: `set debug-file-directory <sysroot>/usr/lib/debug`도 같이. 예제 4에서 `target:`이 그 경로를 읽어 오던 것을 로컬로 옮기는 것이다.
3. **코드**: 서비스 시작 전에 `echo 0x33 > /proc/self/coredump_filter`(또는 systemd의 `CoredumpFilter=`)를 적용해 `crash`를 다시 죽이고, `eu-unstrip -n --core core`만으로 실행 파일 build-id가 나오는지, `coredumpctl info`의 스택이 `n/a` 대신 모듈 이름을 보여 주는지 확인하라.
   정답/힌트: 비트 4(0x10) = ELF 헤더 페이지. §4.1, 예제 12의 추정을 검증하는 실험이다.
4. **/proc·strace 관찰**: `strace -f -e inject=openat:error=ENOENT:when=3 ./needcfg`로 세 번째 openat(로그 파일)을 일부러 실패시키고, 프로그램의 에러 처리 경로(`perror("open")`)가 동작하는지 보라.
   정답/힌트: 몇 번째 openat인지는 로더가 여는 것까지 센다 — 먼저 `strace -e trace=openat`으로 순서를 세라.
5. **코드**: `race.c`를 `__atomic_fetch_add(&frames, 1, __ATOMIC_RELAXED)`로 고치고 TSan이 조용해지는지, 그리고 `-O1` 일반 실행 결과가 항상 2,000,000인지 확인하라. relaxed로 충분한 이유는?
   정답/힌트: 카운터 하나만 맞으면 되고 다른 메모리와의 순서를 기대하지 않으니까. 다른 데이터의 "준비 완료" 플래그라면 release/acquire가 필요하다(A8).
6. **측정**: 예제 21을 `ASAN_OPTIONS=quarantine_size_mb=0`에서 `256`까지 바꿔 RSS와 시간을 표로 만들고, 예제 18의 UAF(mode 2)를 각 설정에서 여전히 잡는지 보라.
   정답/힌트: mode 2는 free 직후 접근이라 quarantine이 작아도 잡힌다. 오래 뒤의 UAF일수록 큰 quarantine이 필요하다.
7. **설계**: 1 kHz SCHED_FIFO 제어 스레드가 가끔 한 주기를 놓친다. gdb·strace를 쓰면 안 되는 이유와 대신 쓸 도구 3가지를 들어라.
   정답/힌트: 멈추거나 100배 느려지면 증상이 바뀐다. dprintf/non-stop은 그래도 위험. uprobe·tracepoint(M7, M5), `cyclictest`/`rtla`(N3), GPIO 토글 + 스코프(M11).

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| ptrace | process trace syscall | 한 프로세스가 다른 프로세스를 멈추고 레지스터·메모리를 읽고 쓰는 커널 인터페이스 |
| gdbserver | 원격 디버그 stub | 보드에서 ptrace로 디버기를 조종하고 RSP로 PC의 gdb와 대화 |
| RSP | Remote Serial Protocol | gdb와 stub(gdbserver, OpenOCD, QEMU gdbstub) 사이의 텍스트 프로토콜 |
| sysroot | 타깃 루트 사본 | gdb·링커가 타깃의 라이브러리·헤더를 찾는 디렉터리 (B1) |
| `target:` | gdb sysroot 접두어 | 파일을 원격 stub에게서 받아 읽기 |
| build-id | `.note.gnu.build-id` | 빌드 내용 해시. 바이너리·debug 파일·core를 짝짓는 지문 |
| debuglink | `.gnu_debuglink` | stripped 파일에 적힌 debug 파일 이름 + CRC |
| debuginfod | debug 정보 HTTP 서버 | build-id로 debug 파일·소스를 내려 주는 elfutils 서버 |
| core dump | ELF `ET_CORE` | 죽는 순간의 레지스터·메모리·매핑 표 |
| coredump_filter | `/proc/PID/coredump_filter` | 어떤 종류의 메모리를 core에 쓸지 비트 마스크 |
| gcore | `generate-core-file` | 살아 있는 프로세스의 core를 만들고 계속 실행 |
| PIE / ASLR | 위치 독립 실행 파일 / 주소 무작위화 | 실행마다 기준 주소가 바뀜 → 오프셋으로 계산 |
| watchpoint | 데이터 breakpoint | 주소 접근 시 정지. arm64는 WVR/WCR 레지스터 |
| catchpoint | gdb `catch` | syscall·fork·exec·예외 같은 이벤트에서 정지 |
| non-stop mode | gdb 모드 | 멈춘 스레드만 멈추고 나머지는 계속 |
| seccomp-bpf | syscall 필터 | strace `--seccomp-bpf`가 관심 syscall만 멈추게 하는 데 씀 |
| PLT | Procedure Linkage Table | 공유 라이브러리 호출 스텁. ltrace가 여기에 breakpoint를 건다 |
| DBI | dynamic binary instrumentation | valgrind 방식: 실행 중 번역하며 계측 |
| shadow memory | 그림자 메모리 | 각 바이트의 접근 가능 여부를 기록하는 별도 영역 (ASan, memcheck) |
| redzone / quarantine | ASan 용어 | 할당 앞뒤 금지 띠 / 해제 블록 재사용 지연 |
| definitely / indirectly / possibly lost | memcheck 누수 분류 | 포인터 없음 / 잃은 블록만 가리킴 / 중간 포인터만 있음 |
| happens-before | 동기화 순서 관계 | helgrind·TSan이 레이스를 판정하는 기준 |
| massif | valgrind 힙 프로파일러 | 시간(명령 수)에 따른 힙 크기와 할당 위치 |
| smaps_rollup | `/proc/PID/smaps_rollup` | 프로세스 메모리 합계(Rss, Pss, Anonymous …) |
| wchan | wait channel | 스레드가 잠든 커널 함수 이름 |

---

## 16. 요약 & 체크리스트

유저스페이스 디버깅은 "한 프로세스만 멈추는 JTAG"(ptrace 기반 gdb·gdbserver), "죽은 순간의 덤프"(core), "경계 로그"(strace·ltrace), "계측 실행"(valgrind·sanitizer) 네 가지다. 모든 도구의 전제는 **같은 빌드의 debug 정보**(build-id로 짝지음)와 **같은 라이브러리**(sysroot)다 — 틀리면 도구가 거짓말을 한다(예제 3, 12). 원격 디버깅은 보드에 gdbserver와 stripped 바이너리만, PC에 debug 빌드·sysroot·소스를 둔다. core는 PIE 기준 주소를 빼서 오프셋으로 다루고, `coredump_filter`로 build-id가 core에 남게 한다. strace는 syscall마다 두 번 멈춰 100배 이상 느려지므로 `--seccomp-bpf`나 짧은 부착으로 쓴다. ltrace는 PLT를 지나는 호출만 본다. ASan은 2.4배·큰 메모리, memcheck는 13.6배·초기화 안 된 값까지, TSan/helgrind는 결과가 맞은 실행에서도 레이스를 보고한다. 현장의 메모리 증가는 `/proc/PID/smaps_rollup` 기록부터, 멈춘 데몬은 죽이기 전에 모든 스레드의 유저·커널 스택부터.

- [ ] gdbserver를 띄우기·붙기·`--multi` 세 방식으로 쓰고, PC에서 `set sysroot`·`substitute-path`를 설정할 수 있다
- [ ] sysroot가 틀렸을 때의 증상(엉뚱한 libc 이름, corrupt stack)을 보고 원인을 말할 수 있다
- [ ] `objcopy --only-keep-debug`·`--add-gnu-debuglink`와 `.build-id/xx/yyyy.debug` 경로 규칙으로 symbol store를 만들 수 있다
- [ ] core dump에서 PIE 기준 주소를 찾아 오프셋을 계산하고 `addr2line -i`로 inline 프레임까지 풀 수 있다
- [ ] build-id가 다른 바이너리로 core를 읽으면 왜 믿으면 안 되는지 실제 예로 설명할 수 있다
- [ ] 멈춘 프로세스에서 `ps -L`·`/proc/PID/task/*/stack`·`thread apply all bt`로 데드락 순환을 증명할 수 있다
- [ ] `catch syscall`, `handle SIGPIPE nostop`, `follow-fork-mode`, 하드웨어 watchpoint를 상황에 맞게 쓸 수 있다
- [ ] strace `-f -Z -e trace=%file`, `-T -tt -y`, `-c`로 ENOENT·느린 syscall을 찾을 수 있다
- [ ] strace 오버헤드를 숫자로 말하고 `--seccomp-bpf`가 왜 빠른지 커널 경로로 설명할 수 있다
- [ ] ltrace가 정적·`-fno-plt`·인라인된 호출을 못 보는 이유를 PLT로 설명할 수 있다
- [ ] memcheck·ASan·LSan·helgrind·TSan이 각각 잡는 것과 못 잡는 것, 실측 비용을 말할 수 있다
- [ ] systemd 서비스에 MainPID로 붙고, `coredumpctl gdb`로 core를 열고, watchdog 함정을 피할 수 있다

---

## 참고 자료

- man7.org: [ptrace(2)](https://man7.org/linux/man-pages/man2/ptrace.2.html), [core(5)](https://man7.org/linux/man-pages/man5/core.5.html), [strace(1)](https://man7.org/linux/man-pages/man1/strace.1.html), [proc_pid_smaps(5)](https://man7.org/linux/man-pages/man5/proc_pid_smaps.5.html), [seccomp(2)](https://man7.org/linux/man-pages/man2/seccomp.2.html), [coredumpctl(1)](https://man7.org/linux/man-pages/man1/coredumpctl.1.html)
- GDB 매뉴얼(gdbserver 포함): [Remote Debugging](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Remote-Debugging.html), [Separate Debug Files](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Separate-Debug-Files.html), [Files (sysroot)](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Files.html), [Non-Stop Mode](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Non_002dStop-Mode.html)
- Valgrind 매뉴얼: [valgrind.org/docs/manual](https://valgrind.org/docs/manual/manual.html) — memcheck(4장), helgrind(7장), massif(9장)
- Sanitizer: [GCC Instrumentation Options](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html), [google/sanitizers wiki](https://github.com/google/sanitizers/wiki)
- elfutils debuginfod: [sourceware.org/elfutils/Debuginfod.html](https://sourceware.org/elfutils/Debuginfod.html)
- Michael Kerrisk, "The Linux Programming Interface" — 22장(시그널: core dump), 부록 A(strace)
- 커널 소스(elixir.bootlin.com, v6.18): `kernel/ptrace.c`, `arch/arm64/kernel/ptrace.c`(`syscall_trace_enter`, `NT_ARM_HW_WATCH`), `arch/arm64/kernel/hw_breakpoint.c`, `arch/arm64/kernel/entry-common.c`(`el0_brk64`), `fs/coredump.c`(`vfs_coredump`), `fs/binfmt_elf.c`(`elf_core_dump`), `kernel/seccomp.c`
- 관련 노트: A6 §8, B3 §6·§9, L1, L6, L7 §7·§9, M3, M4, M5, M6, M7, M8, N2, N3
