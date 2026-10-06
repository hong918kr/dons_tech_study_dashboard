# O2. Linux · Android 온디바이스 — adb로 기기를 다루고, 빌드 산출물을 올리고, 사용자 공간에서 NPU 드라이버를 부르는 법

> **이 노트를 다 읽으면**: Android 기기의 ML 스택(앱 → JNI → 네이티브 `.so` → 런타임 → 벤더 라이브러리 → 커널 드라이버 → DSP/NPU 펌웨어)을 한 장으로 그리고 각 층에서 무엇이 깨지는지 말할 수 있다 · adb로 바이너리·모델을 올리고 core를 고정해 벤치마크하고, logcat·thermal·cpufreq를 읽어 숫자를 해석할 수 있다 · NDK+CMake 빌드와 AOSP Soong 빌드의 차이, 16 KB 페이지 요구, SELinux·vendor 파티션·linker namespace가 ML 배포를 어떻게 막는지 설명할 수 있다 · NNAPI가 Android 15에서 deprecated 된 뒤의 가속 경로(LiteRT GPU/NPU, QNN, ONNX Runtime QNN EP)를 고를 수 있다 · 사용자 공간이 `open`/`ioctl`/`mmap`/dma-buf/fence로 NPU 드라이버와 대화하는 구조를 C로 흉내 내고 캐시 동기화 버그를 재현할 수 있다
> **JD 연결**: "Bring up toolchains, SDKs and new accelerator platforms", "Deploy and optimize models on ... edge processors" · study_prep_list **O2** 행: adb, Android 빌드 개요, NNAPI 현황, Linux 사용자 공간에서 NPU 드라이버 호출 ("Hark 기기는 Android 기반 추정", Don 출발점 🟡 Linux 있음, P1)
> **Don 기준 난이도**: 링버퍼·doorbell·DMA·캐시 clean/invalidate·레지스터 맵·버전 handshake는 SSD 펌웨어에서 매일 하던 일이라 **이미 강함** / 새로 배울 것은 그 아래층을 Android/Linux 사용자 공간 쪽에서 보는 시각 — adb 작업 흐름, Android 빌드·파티션·보안 규칙, NNAPI 이후의 가속 API 지형, 커널 uAPI(ioctl 번호, dma-buf, accel 서브시스템, FastRPC)
> **선행 노트**: F1(LiteRT·delegate·`benchmark_model`), F4(QNN·HTP·FastRPC 개요), F5(ONNX Runtime), F7(크로스 빌드·NDK·ELF 섹션), F8(벤더 SDK bring-up), E4(Hexagon DSP 인스턴스), E7(캐시·DMA·IOMMU·coherence), E8(mailbox·공유 링·FastRPC 비용), J1(NPU 오프로드 상태기계), K2(simpleperf·perf·Perfetto), K4(열·지속 성능). 형제 노트 **O1**(Python 파이프라인 자동화), **O3**(ML CI·device farm)는 병렬로 작성 중이다. 이 노트는 그 내용을 다시 설명하지 않고 ID로 가리킨다

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 Hark 기기와 Android — 추정에서 출발한다

Hark 1세대 기기는 웨어러블급 AI 기기이고, 큰 쪽 SoC는 Qualcomm 계열(Hexagon DSP/NPU)일 것으로 **추정**된다(공개 확정 정보 아님). Qualcomm의 모바일·웨어러블 SoC는 대부분 Android(또는 Android 기반 OS)나 임베디드 Linux로 출하된다. 그래서 "모델을 기기에 올려 돌린다"는 말은 실제로는 이런 일들의 합이다.

- 호스트에서 크로스 빌드한 네이티브 바이너리와 모델 파일을 **adb**로 기기에 올린다.
- 기기에서 core를 고정하고 열을 확인하며 벤치마크를 돌리고, **logcat**과 **sysfs**에서 숫자를 읽는다.
- 앱이나 서비스 안에서 런타임(LiteRT, ONNX Runtime, QNN)이 벤더 라이브러리를 거쳐 **커널 드라이버**를 부르고, 드라이버가 DSP/NPU **펌웨어**에 일을 넘긴다.

Don이 지금까지 일한 층은 맨 아래 두 층(펌웨어, 하드웨어)이다. Edge ML 엔지니어는 위에서부터 아래까지 **한 번에 디버깅**해야 한다. 예를 들어 "앱에서 wake word 지연이 가끔 30 ms로 튄다"는 증상은 스케줄러가 little core에 올린 것일 수도, NPU가 미지원 op 때문에 CPU로 fallback 한 것일 수도, 열 때문에 클럭이 깎인 것일 수도, 드라이버가 캐시 유지보수를 과하게 한 것일 수도 있다. 이 노트는 그 층들을 위에서 아래로 훑는 지도다.

### 0.2 지도 한 장 — Android ML 스택

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<rect x="20" y="20" width="420" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="38" font-size="13">앱 (Kotlin / Java)</text><text x="30" y="54" font-size="12">UI, 권한, 서비스 생명주기</text>
<rect x="20" y="70" width="420" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="88" font-size="13">JNI → 네이티브 .so (C/C++, NDK)</text><text x="30" y="104" font-size="12">libkws.so: 전처리, 링버퍼, 후처리</text>
<rect x="20" y="120" width="420" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="138" font-size="13">ML 런타임: LiteRT · ONNX Runtime · QNN API</text><text x="30" y="154" font-size="12">그래프 분할, delegate / EP, CPU 커널(XNNPACK)</text>
<rect x="20" y="170" width="420" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="188" font-size="13">벤더 사용자 공간 라이브러리 (/vendor/lib64)</text><text x="30" y="204" font-size="12">QNN HTP backend, FastRPC 클라이언트 lib, GPU 드라이버 UMD</text>
<line x1="20" y1="218" x2="440" y2="218" stroke="currentColor" stroke-dasharray="5 4"/>
<text x="448" y="222" font-size="12">user ↔ kernel: ioctl · mmap</text>
<rect x="20" y="226" width="420" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="244" font-size="13">Linux 커널 드라이버</text><text x="30" y="260" font-size="12">fastrpc, DMA-BUF heaps, accel/DRM, remoteproc, IOMMU</text>
<line x1="20" y1="274" x2="440" y2="274" stroke="currentColor" stroke-dasharray="5 4"/>
<text x="448" y="278" font-size="12">AP ↔ DSP: 공유 메모리 + 인터럽트</text>
<rect x="20" y="282" width="420" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="2"/>
<text x="30" y="300" font-size="13">DSP / NPU 펌웨어 (Hexagon 위의 RTOS, skel lib)</text><text x="30" y="316" font-size="12">명령 큐 소비, DMA, VTCM 관리, 전원 상태</text>
<rect x="20" y="332" width="420" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#d0564a" stroke-width="2"/>
<text x="30" y="350" font-size="13">하드웨어: CPU 클러스터 · GPU · Hexagon (HVX/HTP) · LPDDR</text><text x="30" y="366" font-size="12">big.LITTLE, 캐시, SMMU, 열 센서</text>
<text x="448" y="44" font-size="12">앱 개발자</text>
<text x="448" y="94" font-size="12">ML 엔지니어 ← 여기부터</text>
<text x="448" y="144" font-size="12">ML 엔지니어 (F1 · F4 · F5)</text>
<text x="448" y="194" font-size="12">벤더 SDK (F4 · F8)</text>
<text x="448" y="250" font-size="12">BSP / 커널 팀</text>
<text x="448" y="306" font-size="12">Don의 홈 (펌웨어)</text>
<text x="448" y="356" font-size="12">Don의 홈 (실리콘 bring-up)</text>
<text x="448" y="392" font-size="12">빨간 테두리 = Don이 이미 아는 층</text>
</svg>
```

그림 1 — Android 기기의 ML 스택. 점선 두 개가 중요한 경계다. 위 점선은 사용자 공간과 커널 사이(시스템 콜), 아래 점선은 AP(application processor)와 DSP 사이(공유 메모리와 인터럽트). Don은 아래 두 층을 알고, 이 노트는 위쪽 층과 점선을 건너는 방법을 채운다.

말로 하면: 앱이 "추론해 줘"라고 하면, 그 요청은 함수 호출 몇 번으로 런타임까지 내려가고, 벤더 라이브러리에서 `ioctl` 하나로 커널에 들어가고, 커널 드라이버가 공유 메모리의 큐에 명령을 쓰고 초인종(doorbell)을 눌러 DSP 펌웨어를 깨운다. 결과는 반대 방향으로 인터럽트 → 드라이버 → fence → 런타임 → 앱으로 올라온다. **아래쪽 절반은 Don이 펌웨어 쪽에서 만들던 바로 그 큐와 초인종이다.**

### 0.3 이 노트의 범위와 실습 환경

| 다루는 것 | 다른 노트로 미루는 것 |
|---|---|
| adb 작업 흐름, logcat·sysfs 읽기, core 고정 | 프로파일러 사용법 깊이 (K2), 열 모델 (K4) |
| NDK vs AOSP 빌드 개요, 16 KB 페이지, 파티션·SELinux·namespace | 툴체인·CMake toolchain file 상세, ELF 섹션 (F7) |
| NNAPI 현황과 대체 경로 선택 | LiteRT 변환·delegate 내부 (F1), QNN 흐름 (F4), ORT (F5) |
| 사용자 공간 ↔ 드라이버 uAPI (ioctl, mmap, dma-buf, fence, accel, FastRPC) | 캐시·IOMMU 하드웨어 원리 (E7), IPC 링 설계 (E8), 펌웨어 쪽 오프로드 상태기계 (J1) |
| 벤치·로그 자동화의 기초 파서 | 파이프라인 자동화 (O1), CI·device farm (O3) |

이 Mac(Apple Silicon)에는 **adb도 Android 기기도 없다**. 그래서 adb·기기 명령은 ```` ```sh ```` 블록으로 **명령만** 보여 주고 기기 출력은 지어내지 않는다. Python 예제는 기기 출력 **형식**을 흉내 낸 합성 텍스트(코드 주석에 표시)를 파싱하고, 출력은 실제 실행 결과다. C 예제는 macOS에서 도는 "가짜 NPU 드라이버"로, 진짜 커널 드라이버는 아니지만 uAPI의 모양(ioctl 번호, 확장 가능한 struct, mmap offset, 캐시 동기화, fence)을 따른다. 웹에서 확인한 사실은 **확인일 2026-10-04**로 출처를 달았고, 확인하지 못한 것은 "확인 필요"라고 쓴다.

---

## 1. adb — ML 엔지니어가 매일 쓰는 명령

### 1.1 직관 — adb는 "USB로 붙은 JTAG 콘솔 + 파일 전송"이다

Don이 SSD 펌웨어에서 UART 콘솔로 로그를 보고, 벤더 툴로 이미지를 내려받고, Trace32로 메모리를 읽었다면, Android에서는 그 역할 대부분을 **adb(Android Debug Bridge)** 하나가 한다. 구조는 세 조각이다.

```
  호스트 (Mac / Linux)                              기기 (Android)

  adb client  --TCP 5037-->  adb server  --USB 또는 TCP-->  adbd (daemon)
  (명령 한 번)               (백그라운드,                    +-- shell (uid shell)
                              기기 여러 대 관리)             +-- 파일 sync (push/pull)
                                                             +-- logcat 전달
```

- **adb client**: 터미널에서 치는 `adb ...` 명령.
- **adb server**: 호스트에서 기기 여러 대를 관리하는 백그라운드 프로세스. 기기 farm(O3)에서 시리얼 번호(`-s`)로 기기를 고르는 이유다.
- **adbd**: 기기 안의 데몬. `adb shell`로 들어가면 보통 `shell` 사용자 권한이다. `adb root`는 userdebug/eng 빌드에서만 된다(양산 user 빌드에서는 안 된다).

### 1.2 자주 쓰는 명령 — 벤치마크 한 번의 흐름

아래는 "호스트에서 빌드한 벤치마크 바이너리와 모델을 기기에 올려, 큰 core에 고정해서 돌리고, 결과와 로그를 가져오는" 한 바퀴다. 실기기가 필요하므로 이 노트에서는 실행하지 않았다.

```sh
# 0) 어떤 기기가 붙었나 — 여러 대면 이후 명령마다 -s <serial>
adb devices -l
adb shell getprop ro.build.version.sdk      # API 레벨 (예: 35 = Android 15)

# 1) 올리기 — /data/local/tmp 는 shell 사용자가 쓰고 실행할 수 있는 거의 유일한 자리
adb push build-android/bin/benchmark_model /data/local/tmp/
adb push kws_int8.tflite /data/local/tmp/
adb shell chmod +x /data/local/tmp/benchmark_model

# 2) 돌리기 — 스레드 1개, 워밍업 후 측정 (flag 이름은 LiteRT benchmark 도구 기준, F1 9.2절)
adb shell "cd /data/local/tmp && ./benchmark_model --graph=kws_int8.tflite \
    --num_threads=1 --warmup_runs=20 --num_runs=200"

# 3) core 고정 — toybox taskset, 16진 mask (2절 예제에서 mask 계산)
adb shell "cd /data/local/tmp && taskset 80 ./benchmark_model --graph=kws_int8.tflite --num_threads=1"

# 4) 로그 — 버퍼 비우고, 태그 하나만, threadtime 형식
adb logcat -c
adb logcat -v threadtime -s KwsEngine:I

# 5) 상태 덤프 — 열, 메모리
adb shell dumpsys thermalservice
adb shell dumpsys meminfo com.example.kws

# 6) 결과 가져오기
adb pull /data/local/tmp/bench_result.json ./results/
```

| 명령 | 펌웨어 세계의 대응물 | 주의 |
|---|---|---|
| `adb devices -l` | JTAG probe 인식 확인 | `unauthorized`면 기기 화면에서 RSA 키 승인 |
| `adb push` / `pull` | 이미지 다운로드 / 덤프 업로드 | 쓰기 가능한 곳은 `/data/local/tmp`, 앱 자신의 디렉터리 정도 |
| `adb shell` | UART 콘솔 | 권한은 `shell`. `/sys` 일부는 읽기만 되고 쓰기는 root 필요 |
| `adb logcat` | UART 로그 + 링버퍼 로그 | 링버퍼라 오래된 로그는 사라진다. 측정 전에 `-c`로 비운다 |
| `getprop` | 부트 로그의 버전 문자열 | 기기 fingerprint를 결과와 함께 기록 (F8 2절 버전 매트릭스) |
| `dumpsys` | 디버그 명령으로 내부 상태 덤프 | 서비스마다 출력 형식이 다르고 버전마다 바뀐다 |
| `taskset` | 태스크를 특정 core에 묶기 | 스케줄러·cpuset 정책이 덮어쓸 수 있다 |
| `adb root` / `remount` | 디버그 펌웨어로 교체 | userdebug/eng 빌드에서만 |

### 1.3 /data/local/tmp 와 앱 안 — 두 개의 실행 세계

같은 `.so`라도 **어디서 실행하느냐**에 따라 보이는 세계가 다르다. 이것이 Android 온디바이스 디버깅에서 가장 흔한 혼란이다.

| 항목 | `adb shell`에서 `/data/local/tmp/bench` | 앱 프로세스 안의 `libkws.so` |
|---|---|---|
| 사용자·SELinux 도메인 | `shell` | 앱 uid, `untrusted_app` 계열 |
| 라이브러리 검색 | `LD_LIBRARY_PATH`로 자유롭게 지정 | linker namespace 규칙 — 앱 자신의 lib, NDK 공개 lib, 벤더가 공개한 lib만 |
| `/dev` 노드 접근 | sepolicy가 shell에 허용한 것만 | sepolicy가 앱 도메인에 허용한 것만 |
| 스케줄링 | 그냥 프로세스 | 앱이 foreground/background인지에 따라 cpuset·uclamp가 달라짐 |
| 열·전원 정책 | 같은 기기 정책 | 같은 기기 정책 + 앱 standby 정책 |

결론: **`/data/local/tmp`에서 빠르다고 앱에서도 빠르지 않다.** 벤치마크 숫자를 보고할 때는 어느 세계에서 쟀는지 적는다. Don이 SSD에서 "디버그 펌웨어에서는 되는데 양산 펌웨어에서 안 된다"를 겪었던 것과 같은 종류의 함정이다.

### 1.4 기기 지형 읽기 — 어느 core가 큰 core인가

벤치마크 전에 할 첫 일은 기기의 CPU 지형을 아는 것이다. Android 기기 대부분은 **이기종 CPU 클러스터**(big.LITTLE, 요즘은 prime + big + little 3단)다. 같은 일을 해도 core마다 지연이 2~3배 다르다(E1, E8 참고).

`/proc/cpuinfo`에는 각 core의 `CPU part`(Arm이 정한 core 번호)가 있고, `/sys/devices/system/cpu/cpuN/cpufreq/cpuinfo_max_freq`에는 최대 주파수(kHz)가 있다. 이 둘을 합치면 클러스터가 나온다.

```sh
adb shell cat /proc/cpuinfo
adb shell 'for c in /sys/devices/system/cpu/cpu[0-9]*; do echo $c $(cat $c/cpufreq/cpuinfo_max_freq); done'
```

**손으로 먼저**: core 8개 중 `CPU part`가 `0xd80`인 것이 cpu0~1, `0xd81`이 cpu2~6, `0xd82`가 cpu7이라고 하자. Arm의 part 번호로 `0xd80` = Cortex-A520(little), `0xd81` = Cortex-A720(big), `0xd82` = Cortex-X4(prime)이다. taskset mask는 core 번호를 비트 위치로 쓴다. cpu7 하나 = `1 << 7` = `0x80`. cpu2~6 = `0b0111_1100` = `0x7c`. cpu0~1 = `0x03`.

무엇을 확인하는 코드인지: cpuinfo 텍스트와 최대 주파수 목록에서 클러스터를 묶고, 각 클러스터의 taskset mask를 계산한다. 입력은 1 + 5 + 2 구성을 흉내 낸 **합성 데이터**이고 주파수 값은 가상이다.

```python
# 기기에서 `adb shell cat /proc/cpuinfo` 와 cpufreq 값을 받아 왔다고 가정한 합성 텍스트
CPUINFO = """processor\t: 0\nCPU implementer\t: 0x41\nCPU part\t: 0xd80
processor\t: 1\nCPU implementer\t: 0x41\nCPU part\t: 0xd80
processor\t: 2\nCPU implementer\t: 0x41\nCPU part\t: 0xd81
processor\t: 3\nCPU implementer\t: 0x41\nCPU part\t: 0xd81
processor\t: 4\nCPU implementer\t: 0x41\nCPU part\t: 0xd81
processor\t: 5\nCPU implementer\t: 0x41\nCPU part\t: 0xd81
processor\t: 6\nCPU implementer\t: 0x41\nCPU part\t: 0xd81
processor\t: 7\nCPU implementer\t: 0x41\nCPU part\t: 0xd82"""
MAXFREQ_KHZ = [2016000]*2 + [2803200]*5 + [3302400]   # cpuN/cpufreq/cpuinfo_max_freq
PARTS = {0xd80: "Cortex-A520", 0xd81: "Cortex-A720", 0xd82: "Cortex-X4"}

cpus, cur = [], {}
for line in CPUINFO.splitlines():
    k, _, v = (s.strip() for s in line.partition(":"))
    if k == "processor":
        cur = {"cpu": int(v)}; cpus.append(cur)
    elif k == "CPU part":
        cur["part"] = int(v, 16)
clusters = {}
for c in cpus:
    key = (PARTS[c["part"]], MAXFREQ_KHZ[c["cpu"]])
    clusters.setdefault(key, []).append(c["cpu"])
for (name, f), ids in sorted(clusters.items(), key=lambda kv: kv[0][1]):
    mask = sum(1 << i for i in ids)
    print(f"{name:12s} max {f/1e6:.2f} GHz  cpus {ids}  taskset mask 0x{mask:02x}")
```

```text
Cortex-A520  max 2.02 GHz  cpus [0, 1]  taskset mask 0x03
Cortex-A720  max 2.80 GHz  cpus [2, 3, 4, 5, 6]  taskset mask 0x7c
Cortex-X4    max 3.30 GHz  cpus [7]  taskset mask 0x80
```

출력에서 볼 것: 손계산과 mask가 같다(`0x03`, `0x7c`, `0x80`). 실제 기기에서는 같은 core 종류라도 주파수가 다른 두 그룹으로 나뉘기도 하므로(같은 A720인데 최대 주파수가 다름) **part와 주파수를 함께** 키로 쓴다. 이 결과는 벤치마크 기록에 그대로 넣는다 — "어느 core에서 쟀나"가 빠진 지연 숫자는 의미가 없다.

### 1.5 core 고정과 governor — 숫자를 재현 가능하게

같은 바이너리를 세 번 돌렸는데 지연이 12 ms, 17 ms, 45 ms로 나왔다면 대부분은 모델 탓이 아니라 **스케줄러 탓**이다. Linux 스케줄러(EAS, energy-aware scheduling)는 전력을 아끼려고 일을 little core에서 시작하고, 부하가 쌓이면 big으로 옮긴다(migration). 옮길 때마다 L1/L2 캐시를 다시 채운다.

간단한 모델로 그 효과를 숫자로 보자.

```
지연 = 일의 양(cycle) / (주파수 × 상대 IPC)
  big    : 40 M / (3.3 GHz × 1.00) = 12.12 ms
  mid    : 40 M / (2.8 GHz × 0.85) = 16.81 ms
  little : 40 M / (2.0 GHz × 0.45) = 44.44 ms
```

말로 하면: 같은 40 M cycle(큰 core 기준) 일이라도 little core는 클럭도 낮고 cycle당 하는 일도 적어서 거의 4배 느리다. 고정하지 않으면 어떤 run은 little에 걸리고, 그 run들이 p99를 만든다.

무엇을 확인하는 코드인지: 위 모델로 200회 run을 시뮬레이션해서 "big 고정", "mid 고정", "고정 안 함(30%는 little에 걸리고 10%는 migration 비용)"의 p50·p99를 비교한다. 비율은 설명용 가정이다.

```python
import numpy as np
# 같은 추론(가정: 40 M cycles @ big 기준)을 세 가지 배치로 200회 돌린다 (모델은 가상)
rng = np.random.default_rng(42)
WORK = 40e6
CORE = {"little": (2.0e9, 0.45), "mid": (2.8e9, 0.85), "big": (3.3e9, 1.0)}  # (Hz, 상대 IPC)
def lat_ms(core, n): f, ipc = CORE[core]; return WORK / (f * ipc) * 1e3 * np.ones(n)
N = 200
pinned_big = lat_ms("big", N) * rng.normal(1, 0.02, N)
pinned_mid = lat_ms("mid", N) * rng.normal(1, 0.02, N)
# 고정 안 함: 스케줄러가 30%는 little에서 시작, 10%는 도중에 migration (+0.8 ms 캐시 재적재)
pick = rng.choice(["big", "mid", "little"], N, p=[0.3, 0.4, 0.3])
free = np.array([lat_ms(c, 1)[0] for c in pick]) * rng.normal(1, 0.02, N)
free += (rng.random(N) < 0.10) * 0.8
for name, x in [("pinned big (taskset 80)", pinned_big), ("pinned mid (taskset 7c)", pinned_mid), ("unpinned", free)]:
    p50, p99 = np.percentile(x, [50, 99])
    print(f"{name:24s} p50 {p50:6.2f}  p99 {p99:6.2f}  p99/p50 {p99/p50:4.2f}")
```

```text
pinned big (taskset 80)  p50  12.11  p99  12.64  p99/p50 1.04
pinned mid (taskset 7c)  p50  16.83  p99  17.59  p99/p50 1.05
unpinned                 p50  16.91  p99  45.75  p99/p50 2.71
```

출력에서 볼 것: 고정한 두 경우는 p99/p50이 1.04~1.05로 **분포가 좁다**. 고정하지 않으면 p50은 mid와 비슷한데 p99가 45.75 ms로 2.7배 튄다 — little core에 걸린 run들이다. 손계산(12.12, 16.81, 44.44 ms)이 그대로 보인다.

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<text x="190" y="52" font-size="13" text-anchor="end">pinned big (80)</text>
<rect x="200" y="36" width="106.6" height="14" fill="#4a7bd0"/><rect x="200" y="52" width="111.2" height="14" fill="#e08a3c"/>
<text x="316" y="62" font-size="12">12.11 / 12.64</text>
<text x="190" y="102" font-size="13" text-anchor="end">pinned mid (7c)</text>
<rect x="200" y="86" width="148.1" height="14" fill="#4a7bd0"/><rect x="200" y="102" width="154.8" height="14" fill="#e08a3c"/>
<text x="360" y="112" font-size="12">16.83 / 17.59</text>
<text x="190" y="152" font-size="13" text-anchor="end">unpinned</text>
<rect x="200" y="136" width="148.8" height="14" fill="#4a7bd0"/><rect x="200" y="152" width="402.6" height="14" fill="#e08a3c"/>
<text x="560" y="146" font-size="12">p99 45.75</text>
<line x1="200" y1="180" x2="640" y2="180" stroke="currentColor"/>
<line x1="200" y1="180" x2="200" y2="185" stroke="currentColor"/><text x="200" y="198" font-size="12" text-anchor="middle">0</text>
<line x1="288" y1="180" x2="288" y2="185" stroke="currentColor"/><text x="288" y="198" font-size="12" text-anchor="middle">10</text>
<line x1="376" y1="180" x2="376" y2="185" stroke="currentColor"/><text x="376" y="198" font-size="12" text-anchor="middle">20</text>
<line x1="464" y1="180" x2="464" y2="185" stroke="currentColor"/><text x="464" y="198" font-size="12" text-anchor="middle">30</text>
<line x1="552" y1="180" x2="552" y2="185" stroke="currentColor"/><text x="552" y="198" font-size="12" text-anchor="middle">40</text>
<line x1="640" y1="180" x2="640" y2="185" stroke="currentColor"/><text x="640" y="198" font-size="12" text-anchor="middle">50 ms</text>
<rect x="200" y="12" width="12" height="10" fill="#4a7bd0"/><text x="218" y="21" font-size="12">p50</text>
<rect x="260" y="12" width="12" height="10" fill="#e08a3c"/><text x="278" y="21" font-size="12">p99</text>
<text x="200" y="222" font-size="12">시뮬레이션 200 run, 일의 양 40 M cycle (가상)</text>
</svg>
```

그림 2 — core 고정 여부에 따른 지연 분포(위 예제의 실제 출력값). 고정하지 않은 경우 p50(파랑)은 mid 고정과 거의 같지만 p99(주황)가 little core 지연 근처까지 늘어난다.

기기에서 재현성을 높이는 다이얼은 네 개다. 앞의 둘은 shell 권한으로 되고, 뒤의 둘은 보통 root(userdebug 빌드)가 필요하다.

```sh
# (a) core 고정 — mask 0x80 = cpu7
adb shell "taskset 80 /data/local/tmp/bench"
# (b) 런타임 스레드 수를 고정 클러스터의 core 수 이하로 — 넘치면 little로 흘러넘친다
adb shell "/data/local/tmp/benchmark_model --graph=m.tflite --num_threads=1"
# (c) governor 확인 / 고정 (쓰기는 root 필요)
adb shell cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_governor
adb root && adb shell "echo performance > /sys/devices/system/cpu/cpu7/cpufreq/scaling_governor"
# (d) 측정 중 주파수 기록 — 벤치 앞뒤로 scaling_cur_freq를 찍어 둔다
```

함정 두 가지:

- **governor를 `performance`로 고정한 숫자는 제품 숫자가 아니다.** 비교 실험(모델 A vs B)에는 좋지만, 사용자가 겪을 지연은 기본 governor + 열 상태에서 따로 재야 한다. 두 숫자를 다 보고한다.
- `taskset`으로 묶어도 런타임이 내부에서 스레드를 더 만들면(스레드 풀) 그 스레드도 같은 mask를 상속한다. mask에 core가 1개인데 `--num_threads=4`면 스레드 4개가 core 하나를 나눠 쓴다.

### 1.6 열 읽기 — /sys/class/thermal

Android 기기의 열 센서는 Linux thermal 프레임워크로 노출된다. `thermal_zoneN/type`에 이름, `thermal_zoneN/temp`에 온도가 **milli-degree Celsius** 정수로 들어 있다(45000 = 45.0 °C). zone 이름은 SoC·커널마다 완전히 다르다.

```sh
adb shell 'for z in /sys/class/thermal/thermal_zone*; do echo $z $(cat $z/type) $(cat $z/temp); done'
adb shell dumpsys thermalservice     # 프레임워크가 보는 열 상태 (throttling status 등)
```

무엇을 확인하는 코드인지: 위 명령 출력 형식을 흉내 낸 합성 덤프를 파싱해서 단위를 바꾸고, 잘못된 값을 걸러 가장 뜨거운 zone을 찾는다. zone 이름은 가상이다.

```python
# `adb shell 'for z in /sys/class/thermal/thermal_zone*; do echo $z $(cat $z/type) $(cat $z/temp); done'`
# 의 출력 형식을 흉내 낸 합성 텍스트. zone 이름은 SoC마다 다르다.
DUMP = """/sys/class/thermal/thermal_zone0 soc-0 34600
/sys/class/thermal/thermal_zone1 cpu-0-0-0 41200
/sys/class/thermal/thermal_zone2 cpu-1-0-0 47900
/sys/class/thermal/thermal_zone3 npu-0 52300
/sys/class/thermal/thermal_zone4 battery 31000
/sys/class/thermal/thermal_zone5 skin-therm -40000"""
rows = []
for line in DUMP.splitlines():
    path, ztype, raw = line.split()
    c = int(raw) / 1000                         # 단위: milli-degree Celsius
    ok = -30 < c < 125                          # -40 C 같은 값은 센서 미연결/비활성 표시인 경우가 많다
    rows.append((ztype, c, ok))
for z, c, ok in sorted(rows, key=lambda r: -r[1]):
    print(f"{z:12s} {c:6.1f} C  {'' if ok else '<- invalid, ignore'}".rstrip())
valid = [r for r in rows if r[2]]
hot = max(valid, key=lambda r: r[1])
print(f"hottest valid zone: {hot[0]} {hot[1]:.1f} C")
```

```text
npu-0          52.3 C
cpu-1-0-0      47.9 C
cpu-0-0-0      41.2 C
soc-0          34.6 C
battery        31.0 C
skin-therm    -40.0 C  <- invalid, ignore
hottest valid zone: npu-0 52.3 C
```

출력에서 볼 것: milli-degree를 1000으로 나눠 °C로 바꿨다. `-40.0 C` 같은 값은 센서가 연결되지 않았거나 꺼진 zone에서 흔히 보이는 표시라 걸러 낸다(값의 의미는 기기마다 다르니 확인). NPU 근처 zone이 가장 뜨겁다 — 추론 중에 이 값을 벤치 결과와 같이 기록해야 "왜 3분 뒤에 느려졌나"에 답할 수 있다. 지속 성능과 열 모델은 K4가 다룬다.

### 1.7 logcat — 기기의 printf

logcat은 커널 로그(`dmesg`)와 별개인 **사용자 공간 로그 링버퍼**다. 네이티브 코드에서는 `__android_log_print(ANDROID_LOG_INFO, "KwsEngine", "infer ms=%.1f", ms)`로 쓴다. `-v threadtime` 형식은 한 줄이 `날짜 시간 PID TID 레벨 태그: 메시지`다.

무엇을 확인하는 코드인지: threadtime 형식의 합성 로그를 정규식으로 파싱해서, 경고·에러를 뽑고, "CPU fallback" 이벤트 전후의 추론 지연을 나눠 본다.

```python
import re, statistics
# `adb logcat -v threadtime` 형식: 날짜 시간 PID TID 레벨 TAG: 메시지 (아래 줄들은 합성)
LOG = """10-04 14:02:11.120  4312  4330 I KwsEngine: init backend=htp model=kws_v3.bin
10-04 14:02:11.481  4312  4330 I KwsEngine: infer ms=4.9
10-04 14:02:11.582  4312  4330 I KwsEngine: infer ms=5.1
10-04 14:02:11.590  1021  1100 W ThermalHAL: skin temp 41.5C above threshold
10-04 14:02:11.683  4312  4330 I KwsEngine: infer ms=5.0
10-04 14:02:11.784  4312  4330 W KwsEngine: fallback to cpu: op=GELU unsupported
10-04 14:02:11.791  4312  4330 I KwsEngine: infer ms=18.7
10-04 14:02:11.892  4312  4330 I KwsEngine: infer ms=19.2
10-04 14:02:11.993  4312  4330 E KwsEngine: deadline miss ms=31.4 budget=20"""
PAT = re.compile(r"^(\d\d-\d\d) (\S+)\s+(\d+)\s+(\d+) ([VDIWEF]) ([^:]+): (.*)$")
ev = [m.groups() for m in map(PAT.match, LOG.splitlines()) if m]
print(f"parsed {len(ev)} lines, levels:", {lv: sum(e[4] == lv for e in ev) for lv in "IWE"})
seg, cur = {}, "htp"
for _, ts, pid, tid, lv, tag, msg in ev:
    if tag.strip() != "KwsEngine": continue
    if msg.startswith("fallback"): cur = "cpu"
    m = re.search(r"ms=([\d.]+)", msg)
    if m and msg.startswith("infer"): seg.setdefault(cur, []).append(float(m.group(1)))
    if lv in "WE": print(f"  {ts} {lv} {msg}")
for k, v in seg.items():
    print(f"{k}: n={len(v)} median={statistics.median(v):.2f} ms")
```

```text
parsed 9 lines, levels: {'I': 6, 'W': 2, 'E': 1}
  14:02:11.784 W fallback to cpu: op=GELU unsupported
  14:02:11.993 E deadline miss ms=31.4 budget=20
htp: n=3 median=5.00 ms
cpu: n=2 median=18.95 ms
```

출력에서 볼 것: fallback 경고 한 줄을 경계로 median 지연이 5.00 ms에서 18.95 ms로 3.8배 뛰었다. 숫자만 보면 "가끔 느리다"지만, 로그를 같은 시간축에 놓으면 원인이 op 하나(GELU)의 미지원이라는 것이 보인다(fallback 비용 모델은 F8 4절). 실제 작업에서는 이 파서가 O1의 자동화 파이프라인 안으로 들어간다.

로그 규칙: 지연은 **한 줄에 key=value**로(정규식 하나로 뽑힌다), 매 프레임이 아니라 집계해서 1초에 한 줄(logcat 자체도 CPU를 쓰고 링버퍼를 밀어낸다), 측정 전 `adb logcat -c` 후 바로 덤프.

---

## 2. Android 빌드 개요 — 내 `.so`는 어떻게 기기에 들어가나

### 2.1 두 개의 빌드 세계

Android에서 네이티브 코드를 만드는 길은 크게 둘이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="o2b" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="175" y="22" font-size="14" text-anchor="middle">앱 쪽: NDK + CMake</text>
<text x="505" y="22" font-size="14" text-anchor="middle">플랫폼 쪽: AOSP + Soong</text>
<rect x="30" y="36" width="290" height="46" rx="5" fill="none" stroke="#4a7bd0"/><text x="175" y="56" font-size="13" text-anchor="middle">C/C++ 소스 + CMakeLists.txt</text><text x="175" y="73" font-size="12" text-anchor="middle">벤더 SDK 헤더·.so (QNN 등)</text>
<rect x="30" y="106" width="290" height="46" rx="5" fill="none" stroke="#4a7bd0"/><text x="175" y="126" font-size="13" text-anchor="middle">android.toolchain.cmake</text><text x="175" y="143" font-size="12" text-anchor="middle">ABI arm64-v8a, 최소 API 레벨, STL</text>
<rect x="30" y="176" width="290" height="46" rx="5" fill="none" stroke="#4a7bd0"/><text x="175" y="196" font-size="13" text-anchor="middle">libkws.so (ELF64, AArch64)</text><text x="175" y="213" font-size="12" text-anchor="middle">PT_LOAD 16 KB 정렬 (NDK r28+ 기본)</text>
<rect x="30" y="246" width="290" height="46" rx="5" fill="none" stroke="#4a7bd0"/><text x="175" y="266" font-size="13" text-anchor="middle">APK lib/arm64-v8a/ 또는</text><text x="175" y="283" font-size="12" text-anchor="middle">adb push /data/local/tmp (실험)</text>
<rect x="360" y="36" width="290" height="46" rx="5" fill="none" stroke="#3f9a6b"/><text x="505" y="56" font-size="13" text-anchor="middle">소스 + Android.bp</text><text x="505" y="73" font-size="12" text-anchor="middle">cc_library_shared, vendor: true</text>
<rect x="360" y="106" width="290" height="46" rx="5" fill="none" stroke="#3f9a6b"/><text x="505" y="126" font-size="13" text-anchor="middle">Soong (Blueprint) + Kati → Ninja</text><text x="505" y="143" font-size="12" text-anchor="middle">lunch 타깃, m 명령</text>
<rect x="360" y="176" width="290" height="46" rx="5" fill="none" stroke="#3f9a6b"/><text x="505" y="196" font-size="13" text-anchor="middle">system.img / vendor.img / ...</text><text x="505" y="213" font-size="12" text-anchor="middle">파티션별 이미지 + SELinux 정책</text>
<rect x="360" y="246" width="290" height="46" rx="5" fill="none" stroke="#3f9a6b"/><text x="505" y="266" font-size="13" text-anchor="middle">fastboot flash / OTA</text><text x="505" y="283" font-size="12" text-anchor="middle">기기 전체 이미지 갱신</text>
<line x1="175" y1="82" x2="175" y2="104" stroke="currentColor" marker-end="url(#o2b)"/><line x1="175" y1="152" x2="175" y2="174" stroke="currentColor" marker-end="url(#o2b)"/><line x1="175" y1="222" x2="175" y2="244" stroke="currentColor" marker-end="url(#o2b)"/>
<line x1="505" y1="82" x2="505" y2="104" stroke="currentColor" marker-end="url(#o2b)"/><line x1="505" y1="152" x2="505" y2="174" stroke="currentColor" marker-end="url(#o2b)"/><line x1="505" y1="222" x2="505" y2="244" stroke="currentColor" marker-end="url(#o2b)"/>
<text x="340" y="318" font-size="12" text-anchor="middle">ML 엔지니어는 대부분 왼쪽, Hark 같은 자체 기기 회사의 펌웨어·BSP 팀은 오른쪽도 만진다</text>
</svg>
```

그림 3 — 네이티브 코드가 기기에 들어가는 두 길. 왼쪽은 앱(또는 실험용 바이너리)에 `.so`를 싣는 길, 오른쪽은 기기 OS 이미지 자체에 라이브러리·서비스·드라이버를 넣는 길이다.

| 항목 | NDK + CMake (앱 쪽) | AOSP + Soong (플랫폼 쪽) |
|---|---|---|
| 누가 쓰나 | 앱 개발자, ML 엔지니어 | OEM·SoC 벤더의 플랫폼·BSP 팀 |
| 빌드 정의 | `CMakeLists.txt` (또는 `ndk-build`의 `Android.mk`) | `Android.bp` (Blueprint 문법, Bazel BUILD와 비슷) |
| 사용할 수 있는 API | NDK 공개 API만 (안정 ABI) | 플랫폼 내부 lib까지 (대신 파티션 규칙을 따라야 함) |
| 산출물 | `.so`, 실행 파일 → APK | 파티션 이미지 (`system`, `vendor`, `product` …) |
| 배포 | Play / 사이드로드 / adb | fastboot, OTA |
| 반복 속도 | 빠름 (수 초~수 분) | 느림 (전체 빌드 수 시간, 증분도 수 분) |
| ML 예 | LiteRT·ORT를 앱에 넣기, 벤치 바이너리 | 시스템 서비스로 상시 wake word 엔진, 벤더 HAL, 드라이버 |

AOSP 문서에 따르면 Soong은 `Android.bp` 파일을 읽고, 레거시 Makefile은 Kati(GNU Make 클론)가 처리하며, 실제 실행은 Ninja가 한다(확인일 2026-10-04, source.android.com Build 문서). NDK 쪽 CMake toolchain file과 ABI·API 레벨·STL 선택은 F7 6.5절에서 이미 다뤘다.

Hark 같은 회사가 자체 기기를 만든다면 "앱 하나"가 아니라 **OS 이미지 전체**를 출하할 가능성이 높다(추정). 그러면 상시 동작하는 음성 엔진은 앱이 아니라 vendor 파티션의 네이티브 서비스로 들어갈 수도 있고, 그때는 오른쪽 길의 규칙(파티션, SELinux, VINTF)을 알아야 한다.

### 2.2 Android.bp 한 조각 — 읽을 수만 있으면 된다

```
// Android.bp (예시 — 빌드하지 않음)
cc_library_shared {
    name: "libkws_engine",
    vendor: true,                 // vendor 파티션에 설치 → /vendor/lib64/
    srcs: ["src/engine.cc", "src/ring.cc"],
    shared_libs: ["liblog", "libcutils"],
    cflags: ["-Wall", "-Werror", "-O2"],
    export_include_dirs: ["include"],
}

```

펌웨어 엔지니어 눈으로 읽으면: `cc_library_shared`는 링커 스크립트 없이 "이 소스로 `.so`를 만들어라", `vendor: true`는 "이 바이너리는 vendor 이미지에 들어가라"는 **배치 지시**다. F7에서 섹션을 flash/SRAM에 배치했던 것처럼, Android는 바이너리를 **파티션**에 배치한다.

### 2.3 파티션 · Treble · VNDK — 왜 vendor lib을 아무 데서나 못 부르나

Android 8.0의 **Project Treble**은 OS를 두 덩어리로 나눴다.

- **system** (Google/OEM의 프레임워크): 자주 업데이트된다.
- **vendor** (SoC 벤더의 드라이버 사용자 공간 부분, HAL 구현): SoC 수명 동안 잘 안 바뀐다.

둘 사이는 버전이 붙은 인터페이스(HAL, 요즘은 주로 AIDL HAL)로만 대화하게 해서, vendor를 다시 빌드하지 않고 system만 올릴 수 있게 했다. 이것을 위해 만든 것이 **VNDK**(vendor가 쓸 수 있는 system lib 묶음)인데, AOSP 문서는 **"Android 15부터 VNDK를 deprecate 한다"**고 적고 있다(확인일 2026-10-04, source.android.com VNDK 문서). 경계의 규칙 자체는 남아 있고, 그 구현 방식이 바뀌는 중이라고 이해하면 된다.

ML 엔지니어에게 이것이 의미하는 것:

- QNN HTP backend 같은 벤더 라이브러리는 보통 `/vendor/lib64/`에 있다. 앱 프로세스는 **linker namespace** 때문에 이 디렉터리의 lib을 마음대로 `dlopen` 할 수 없다. 벤더가 `/vendor/etc/public.libraries.txt`에 올린 lib만 앱에 공개된다(파일 이름은 AOSP "native libraries" 문서 기준, 기기마다 내용 확인 필요).
- 그래서 실무에서는 런타임 SDK의 `.so`를 **앱 APK 안에 같이 넣고**, 커널 드라이버로 가는 얇은 통로(예: FastRPC 클라이언트 lib)만 vendor 공개 lib으로 쓰는 구성이 흔하다(구체 구성은 SDK 버전·기기마다 다르니 F4·F8의 버전 매트릭스로 관리).
- `/data/local/tmp`에서는 `LD_LIBRARY_PATH`로 아무 lib이나 붙일 수 있으니 이 문제가 **안 보인다**. 1.3절의 "두 세계" 함정이 여기서 다시 나온다.

### 2.4 SELinux — 권한이 아니라 "정책"이 막는다

Android는 모든 프로세스와 파일에 SELinux 라벨을 붙이고, **허용 목록(allow rule)에 없는 접근은 root라도 막는다**(enforcing 모드). ML 배포에서 자주 부딪히는 장면:

- 새 서비스가 `/dev/fastrpc-cdsp` 같은 노드를 열려고 하는데 `open()`이 `EACCES`로 실패한다. 파일 권한(`ls -l`)은 문제없어 보인다.
- logcat이나 `dmesg`에 `avc: denied { open } for ... scontext=u:r:my_service:s0 tcontext=u:object_r:...` 같은 줄이 남는다.

```sh
adb shell getenforce                       # Enforcing / Permissive
adb shell ls -lZ /dev/ | grep -i -E "fastrpc|accel|dma_heap"   # 라벨 확인
adb logcat -b all | grep "avc: denied"
```

고치는 법은 sepolicy에 규칙을 추가하는 것(플랫폼 빌드 쪽 일)이고, `setenforce 0`(permissive)으로 끄는 것은 **원인 확인용**으로만 쓴다. 펌웨어 비유로는 MPU/TrustZone 설정이 막는 접근이다 — 코드는 맞는데 보호 설정이 막는 것.

### 2.5 16 KB 페이지 — Android 15부터 생긴 새 정렬 요구

Android 개발자 문서는 다음을 요구한다(확인일 2026-10-04, developer.android.com "Support 16 KB page sizes").

- Android 15(API 35) 이상을 타깃으로 하는 앱은 64-bit 기기에서 **16 KB 메모리 페이지**를 지원해야 한다.
- **NDK r28 이상은 기본으로 16 KB 정렬**로 컴파일한다. r27 이하는 `-Wl,-z,max-page-size=16384`(그리고 `-Wl,-z,common-page-size=16384`)를 직접 줘야 한다.
- 확인 방법: `llvm-objdump -p libX.so` 출력에서 LOAD 줄의 `align`이 `2**14` 이상이어야 한다. `2**12`(4 KB)면 다시 빌드해야 한다. `zipalign -c -P 16`으로 APK 안 정렬도 확인한다.
- Google Play 문구: 2026-10-04 확인 시점의 문서는 "Starting February 1, 2027, if your app updates don't support 16 KB memory page sizes, you won't be able to release these updates"라고 적고 있다. 정책 날짜는 바뀔 수 있으니 출시 전 다시 확인한다.
- **Java/Kotlin만 있는 앱은 영향이 없고, 네이티브 `.so`가 하나라도 있으면(SDK가 넣은 것 포함) 다시 빌드해야 한다.** ML 앱은 거의 항상 네이티브 lib(LiteRT, ORT, QNN)을 갖고 있으니 직접 해당된다.

**왜 정렬이 문제인가 — 직관.** 동적 로더는 `.so`의 각 PT_LOAD 세그먼트를 `mmap(파일 offset → 가상 주소)`으로 메모리에 올린다. `mmap`은 **페이지 단위**로만 매핑할 수 있다. 그래서 세그먼트의 파일 offset과 가상 주소는 "페이지 크기로 나눈 나머지가 같아야" 하고, 코드(R+X)와 데이터(R+W)는 **서로 다른 페이지**에 있어야 각각 다른 권한을 줄 수 있다. 4 KB 기준으로 짜인 `.so`는 16 KB 페이지 커널에서 이 조건이 깨진다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<text x="80" y="22" font-size="12">초록 실선 = 16 KB 페이지 경계, 축의 작은 눈금 = 4 KB (파일 offset)</text>
<text x="72" y="64" font-size="13" text-anchor="end">4 KB</text><text x="72" y="80" font-size="12" text-anchor="end">정렬 .so</text>
<rect x="80" y="50" width="105" height="34" fill="#4a7bd0" fill-opacity="0.7"/><text x="132" y="72" font-size="12" text-anchor="middle">code RX</text>
<rect x="185" y="50" width="13" height="34" fill="#e08a3c"/>
<text x="80" y="102" font-size="12">data RW가 offset 0x1800 → code와 같은 16 KB 페이지 안: 권한 분리 불가</text>
<text x="72" y="144" font-size="13" text-anchor="end">16 KB</text><text x="72" y="160" font-size="12" text-anchor="end">정렬 .so</text>
<rect x="80" y="130" width="105" height="34" fill="#4a7bd0" fill-opacity="0.7"/><text x="132" y="152" font-size="12" text-anchor="middle">code RX</text>
<rect x="185" y="130" width="175" height="34" fill="none" stroke="#888" stroke-dasharray="4 3"/><text x="272" y="152" font-size="12" text-anchor="middle">padding</text>
<rect x="360" y="130" width="13" height="34" fill="#e08a3c"/>
<text x="381" y="146" font-size="12">data RW (offset 0x4000)</text>
<text x="381" y="161" font-size="12">→ 새 16 KB 페이지에서 시작: OK</text>
<line x1="80" y1="42" x2="80" y2="92" stroke="#3f9a6b" stroke-width="2"/><line x1="360" y1="42" x2="360" y2="92" stroke="#3f9a6b" stroke-width="2"/><line x1="640" y1="42" x2="640" y2="92" stroke="#3f9a6b" stroke-width="2"/>
<line x1="80" y1="122" x2="80" y2="172" stroke="#3f9a6b" stroke-width="2"/><line x1="360" y1="122" x2="360" y2="172" stroke="#3f9a6b" stroke-width="2"/><line x1="640" y1="122" x2="640" y2="172" stroke="#3f9a6b" stroke-width="2"/>
<line x1="80" y1="190" x2="640" y2="190" stroke="currentColor"/>
<line x1="150" y1="190" x2="150" y2="195" stroke="currentColor"/><line x1="220" y1="190" x2="220" y2="195" stroke="currentColor"/><line x1="290" y1="190" x2="290" y2="195" stroke="currentColor"/><line x1="430" y1="190" x2="430" y2="195" stroke="currentColor"/><line x1="500" y1="190" x2="500" y2="195" stroke="currentColor"/><line x1="570" y1="190" x2="570" y2="195" stroke="currentColor"/>
<line x1="80" y1="190" x2="80" y2="199" stroke="currentColor"/><line x1="360" y1="190" x2="360" y2="199" stroke="currentColor"/><line x1="640" y1="190" x2="640" y2="199" stroke="currentColor"/>
<text x="80" y="214" font-size="12" text-anchor="middle">0x0000</text><text x="360" y="214" font-size="12" text-anchor="middle">0x4000</text><text x="640" y="214" font-size="12" text-anchor="middle">0x8000</text>
<text x="80" y="240" font-size="12">대가: 세그먼트 사이 padding만큼 파일과 가상 주소 공간이 커진다</text>
</svg>
```

그림 4 — 같은 코드·데이터를 4 KB 정렬과 16 KB 정렬로 링크했을 때의 파일 배치(축척 맞춤). 4 KB 정렬에서는 data 세그먼트가 code와 같은 16 KB 페이지에 걸쳐 있어 16 KB 페이지 커널이 서로 다른 권한으로 매핑할 수 없다.

**이 Mac이 바로 16 KB 페이지 기계다.** Apple Silicon macOS의 페이지 크기는 16 KB다. 그래서 "offset이 페이지 배수가 아니면 `mmap`이 거절한다"를 여기서 직접 볼 수 있다.

무엇을 확인하는 코드인지: 시스템 페이지 크기를 읽고, 같은 파일을 offset 0, 4096, 8192, 16384에서 `mmap` 해 본다.

```c
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
    long page = sysconf(_SC_PAGESIZE);
    printf("page size = %ld bytes\n", page);
    int fd = open("seg.bin", O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0 || ftruncate(fd, 64 * 1024) != 0) return 1;
    const long offs[] = {0, 4096, 8192, 16384};
    for (size_t i = 0; i < sizeof offs / sizeof offs[0]; i++) {
        void *p = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, (off_t)offs[i]);
        if (p == MAP_FAILED) {
            printf("mmap(offset=%5ld) -> FAIL (%s)\n", offs[i], strerror(errno));
        } else {
            printf("mmap(offset=%5ld) -> OK\n", offs[i]);
            munmap(p, 4096);
        }
    }
    close(fd);
    unlink("seg.bin");
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 pagesz.c -o pagesz && ./pagesz
```

```text
page size = 16384 bytes
mmap(offset=    0) -> OK
mmap(offset= 4096) -> FAIL (Invalid argument)
mmap(offset= 8192) -> FAIL (Invalid argument)
mmap(offset=16384) -> OK
```

출력에서 볼 것: 이 기계의 페이지는 16384 B이고, 4 KB 배수지만 16 KB 배수가 아닌 offset(4096, 8192)은 `EINVAL`로 거절된다. 4 KB 정렬 `.so`를 16 KB 커널의 로더가 올릴 때 부딪히는 벽이 이것이다. 4 KB 페이지 Linux PC에서는 4096도 성공하니, **개발 PC에서 되는 것이 기기에서 안 되는** 전형적인 예다.

**ELF를 직접 검사하기.** F7 2.3절에서는 ELF32 **섹션** 헤더를 읽었다. 로더가 실제로 보는 것은 **프로그램 헤더**(세그먼트)다. ELF64 프로그램 헤더는 56 B짜리 구조체 배열이다: `p_type(4) p_flags(4) p_offset(8) p_vaddr(8) p_paddr(8) p_filesz(8) p_memsz(8) p_align(8)`.

**손으로 먼저**: PT_LOAD가 offset `0x1800`, vaddr `0x2800`, align `0x1000`이라고 하자. 4 KB 기준: `0x1800 mod 0x1000 = 0x800`, `0x2800 mod 0x1000 = 0x800` → 합동이니 4 KB 커널에서는 OK. 16 KB 기준: `0x1800 mod 0x4000 = 0x1800`, `0x2800 mod 0x4000 = 0x2800` → 다르다 → 16 KB 커널에서는 매핑 불가. align 값 자체도 `2**12`라 기준(`2**14`) 미달이다.

무엇을 확인하는 코드인지: (1) 4 KB 정렬과 16 KB 정렬의 가짜 `.so` 헤더 바이트를 만들어 검사하고, (2) 이 Mac의 clang으로 실제로 만든 AArch64 Android 오브젝트(`--target=aarch64-linux-android30`)의 ELF 헤더를 같은 함수로 읽는다. 이 Mac에는 ELF 링커(`ld.lld`)가 없어서 진짜 `.so`는 만들 수 없다 — 그래서 `.so` 쪽은 합성 바이트다.

```c
// kws.c — 검사 대상 오브젝트
#include <stdint.h>
int32_t dot_s8(const int8_t *a, const int8_t *b, int n) {
    int32_t acc = 0;
    for (int i = 0; i < n; i++) acc += a[i] * b[i];
    return acc;
}
```

```python
import struct, sys
PT_LOAD, EM_AARCH64 = 1, 183
def make_so(align):
    """PT_LOAD 2개(코드 RX, 데이터 RW)를 가진 최소 ELF64 헤더+프로그램 헤더 바이트를 만든다."""
    ehdr = b"\x7fELF" + bytes([2, 1, 1]) + bytes(9)          # 64-bit, little-endian, v1
    ehdr += struct.pack("<HHIQQQIHHHHHH", 3, EM_AARCH64, 1, 0, 64, 0, 0, 64, 56, 2, 64, 0, 0)
    ph = struct.pack("<IIQQQQQQ", PT_LOAD, 5, 0, 0, 0, 0x1800, 0x1800, align)        # R+X
    data_off = 0x1800 if align == 0x1000 else 0x4000                                   # 링커가 정렬에 맞춰 배치
    ph += struct.pack("<IIQQQQQQ", PT_LOAD, 6, data_off, data_off + align, data_off + align, 0x300, 0x900, align)
    return ehdr + ph
def check(blob, page=16384):
    assert blob[:4] == b"\x7fELF"
    cls, (etype, mach) = blob[4], struct.unpack_from("<HH", blob, 16)
    if cls != 2:  return f"ELF32? class={cls}"
    phoff, = struct.unpack_from("<Q", blob, 32); phentsize, phnum = struct.unpack_from("<HH", blob, 54)
    out = [f"e_type={etype} e_machine={mach}{' (AArch64)' if mach == EM_AARCH64 else ''} phnum={phnum}"]
    for i in range(phnum):
        p_type, flags, off, vaddr, _, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", blob, phoff + i * phentsize)
        if p_type != PT_LOAD: continue
        ok = align >= page and off % page == vaddr % page
        out.append(f"  LOAD off=0x{off:05x} vaddr=0x{vaddr:05x} align=2**{align.bit_length()-1} -> {'OK' if ok else 'UNALIGNED for 16KB'}")
    return "\n".join(out)
for a in (0x1000, 0x4000):
    print(f"[synthetic .so, align 0x{a:x}]"); print(check(make_so(a)))
print("[real kws_android.o]"); print(check(open(sys.argv[1], "rb").read()))
```

```sh
cc --target=aarch64-linux-android30 -O2 -fPIC -c kws.c -o kws_android.o
file kws_android.o
.venv/bin/python elfcheck.py kws_android.o
```

```text
kws_android.o: ELF 64-bit LSB relocatable, ARM aarch64, version 1 (SYSV), not stripped
[synthetic .so, align 0x1000]
e_type=3 e_machine=183 (AArch64) phnum=2
  LOAD off=0x00000 vaddr=0x00000 align=2**12 -> UNALIGNED for 16KB
  LOAD off=0x01800 vaddr=0x02800 align=2**12 -> UNALIGNED for 16KB
[synthetic .so, align 0x4000]
e_type=3 e_machine=183 (AArch64) phnum=2
  LOAD off=0x00000 vaddr=0x00000 align=2**14 -> OK
  LOAD off=0x04000 vaddr=0x08000 align=2**14 -> OK
[real kws_android.o]
e_type=1 e_machine=183 (AArch64) phnum=0
```

출력에서 볼 것: 4 KB 정렬본은 두 세그먼트 모두 `UNALIGNED for 16KB`, 16 KB 정렬본은 모두 `OK`다 — 손계산과 같다. 실제 오브젝트는 `e_machine=183`(AArch64)이지만 `e_type=1`(relocatable `.o`)이라 **프로그램 헤더가 0개**다. 세그먼트 정렬은 **링크할 때** 정해진다는 뜻이다. 그래서 정렬 문제는 소스가 아니라 링커 플래그·NDK 버전의 문제이고, 벤더가 미리 빌드해서 준 `.so`(예: 오래된 SDK)는 내가 고칠 수 없다 — 벤더에 새 빌드를 요청해야 한다. CI(O3)에 이 검사기를 넣어 두면 미정렬 `.so`가 들어오는 순간 잡힌다.

---

## 3. NNAPI 현황 — deprecated 이후 무엇으로 가속하나

### 3.1 NNAPI는 무엇이었나

**NNAPI(Neural Networks API)**는 Android 8.1(API 27)에서 들어온 C API다. 앱(보통 TFLite의 NNAPI delegate)이 그래프를 NNAPI에 넘기면, Android가 기기 벤더의 NN HAL 드라이버를 골라 DSP·NPU·GPU에 실행시키는 구조였다. 펌웨어 비유로는 "OS가 표준 명령 세트를 정의하고, 칩 벤더가 드라이버로 구현하는" NVMe 같은 모델이다.

### 3.2 무엇이 바뀌었나 — 확인된 사실

| 사실 | 출처 (확인일 2026-10-04) |
|---|---|
| NNAPI는 Android 8.1에서 도입되었고 **Android 15에서 deprecated** 되었다 | developer.android.com NNAPI 가이드, NNAPI Migration Guide |
| 이유: 트랜스포머·diffusion 등 ODML이 빠르게 바뀌어 "자주 업데이트되는 도구와 인프라"가 필요했다 | NNAPI Migration Guide |
| 대안으로 **TensorFlow Lite in Google Play Services**(업데이트 가능한 런타임)와 GenAI용 **AICore**(Gemini Nano)를 제시 | NNAPI Migration Guide |
| NNAPI 가이드 경고문: 계속 쓸 수는 있지만 앞으로 대부분의 기기는 **CPU backend**를 쓸 것으로 예상하며, 성능이 중요한 일은 TF Lite GPU 런타임 같은 대안으로 옮기라고 권장 | developer.android.com NNAPI 가이드 |
| LiteRT는 CompiledModel API로 CPU/GPU/NPU 가속을 고르며, Qualcomm NPU는 **Qualcomm AI Engine Direct(QNN)의 HTP backend**로 내린다. AOT 컴파일과 기기 위 컴파일 둘 다 지원 | Google AI Edge "Qualcomm NPU (AI Engine Direct) with LiteRT" |
| 위 문서가 나열한 지원 SoC는 Snapdragon 8 Gen 1(SM8450)부터 8 Elite Gen 5(SM8850)까지의 플래그십 계열 | 같은 문서 |
| ONNX Runtime QNN EP는 Snapdragon SoC의 Android·Windows에서 동작하고, `backend_path`(`libQnnHtp.so`) 또는 `backend_type`(`htp`, `gpu`, `cpu`)과 context 캐시(`ep.context_enable`)를 지원 | onnxruntime.ai QNN EP 문서 |

말로 하면: **"OS가 표준 가속 API를 제공하고 벤더가 구현한다"는 모델은 끝나 가고, "앱이 업데이트 가능한 런타임을 직접 싣고, 그 런타임이 벤더 SDK(QNN 등)를 직접 부른다"는 모델로 옮겨 가는 중이다.** 펌웨어로 치면 OS 표준 드라이버 대신 칩 벤더 SDK를 제품에 직접 링크하는 쪽으로 바뀐 것이다.

### 3.3 경로 선택표 — Hark 같은 Qualcomm 기기라면

| 경로 | 무엇을 거치나 | 장점 | 단점·위험 | 자세히 |
|---|---|---|---|---|
| LiteRT CPU (XNNPACK) | 런타임 안의 CPU 커널 | 어디서나 동작, 디버깅 쉬움, 기준선 | 전력·지연이 가장 나쁨 | F1 |
| LiteRT GPU | OpenCL/OpenGL ES 계열 GPU delegate | 벤더 SDK 없이 가속, float16 친화 | int8 이득 적음, 첫 실행 셰이더 컴파일 지연, GPU 전력 | F1 8절 |
| LiteRT NPU (QNN 경유) | CompiledModel → QNN HTP | Google이 지원하는 NPU 길, AOT 산출물을 Play로 배포 가능 | 지원 SoC 목록이 플래그십 위주(웨어러블 SoC는 확인 필요), API가 비교적 새로움 | F1, F4 |
| QNN SDK 직접 | QNN API → HTP backend → FastRPC → cDSP | 가장 낮은 오버헤드, context binary, 세밀한 성능 모드 | Qualcomm 전용, SDK·드라이버 버전 매트릭스 관리 부담 | F4 |
| ONNX Runtime QNN EP | ORT → QNN EP → HTP | PyTorch → ONNX 경로 유지, CPU fallback 자동 | 파티션 경계마다 복사 비용, QNN EP 빌드 필요 | F4 3.3절, F5 |
| NNAPI | NN HAL → 벤더 드라이버 | 기존 코드 유지 | Android 15부터 deprecated, 신규 프로젝트에 부적합 | 이 절 |

선택 규칙 세 개:

- **기준선은 항상 CPU.** 어떤 가속 경로든 결과를 CPU 결과와 비교하는 정확도 게이트(C8, F8 5절)를 먼저 만든다.
- **자체 기기·자체 OS 이미지**라면(Hark 추정) Play 배포 제약이 약하고 SoC가 하나로 고정되므로, QNN 직접 또는 LiteRT NPU의 AOT 경로처럼 **특정 SoC에 맞춘 산출물**이 유리하다.
- **여러 기기(폰 앱 등)**에 나간다면 LiteRT GPU/NPU처럼 런타임이 기기를 판별해 고르는 경로가 유지보수에 유리하다.

웨어러블급 Qualcomm SoC(예: Snapdragon W 계열, AR1 등)에서 위 LiteRT NPU 지원 목록이 적용되는지는 공개 문서로 확인하지 못했다 — **확인 필요**. 이런 칩은 QNN SDK 지원 목록과 레퍼런스 OS(Android인지 임베디드 Linux인지)를 공식 문서로 먼저 확인하고, 출처와 확인일을 적어 둔다.

---

## 4. Linux 사용자 공간에서 NPU 드라이버를 부르는 법

### 4.1 직관 — 펌웨어에서 하던 일을 반대편에서 본다

Don은 SSD에서 호스트가 NVMe submission queue에 명령을 쓰고 doorbell 레지스터를 치면, 펌웨어가 그 명령을 꺼내 처리하고 completion queue에 결과를 쓰고 인터럽트를 올리는 일을 **펌웨어 쪽에서** 구현했다. NPU 드라이버도 똑같다. 이번에는 Don이 **호스트(사용자 공간 + 커널) 쪽**에 서 있다.

| NVMe (Don이 알던 것) | NPU 드라이버 (이 절) |
|---|---|
| 호스트 드라이버가 SQ 엔트리 작성 | 커널 드라이버가 명령 ring에 descriptor 작성 |
| SQ tail doorbell 레지스터 쓰기 | 장치 doorbell 레지스터 / mailbox 쓰기 |
| PRP/SGL로 호스트 메모리 주소 전달 | buffer object(BO) handle, dma-buf fd, IOMMU 주소 |
| CQ 엔트리 + MSI-X 인터럽트 | 완료 인터럽트 → dma-fence signal |
| Identify / feature 명령으로 버전 확인 | `GET_INFO`류 ioctl, uAPI 버전, 펌웨어 버전 handshake |

### 4.2 다섯 개의 시스템 콜 — 이것으로 거의 모든 것을 한다

사용자 공간 런타임(예: QNN backend lib)이 NPU를 쓰려면 결국 아래 다섯 가지를 조합한다.

```
open("/dev/<node>")             장치와의 세션(context) 하나를 연다 — 파일 디스크립터 = 세션
ioctl(fd, ALLOC/IMPORT, &req)   버퍼(BO)를 만들거나, 다른 장치가 만든 dma-buf fd를 가져온다
mmap(fd, offset)                그 버퍼를 CPU 주소 공간에 매핑한다 (드라이버가 준 가짜 offset 사용)
ioctl(fd, SUBMIT, &job)         명령(그래프 실행, DMA 목록)을 큐에 넣고 doorbell을 친다
poll()/ioctl(WAIT) on fence     완료를 기다린다 — 인터럽트 → fence signal
```

- **`ioctl`**: 드라이버별 "원격 함수 호출". 요청 번호 하나와 struct 포인터 하나를 넘긴다. 번호 안에 방향·크기·종류·번호가 인코딩되어 있다(4.4절).
- **`mmap`의 offset**: 파일 위치가 아니라 **드라이버가 BO마다 발급한 가짜 offset**이다(DRM GEM에서 쓰는 방식). 그 offset을 `mmap`에 넘기면 드라이버가 해당 BO의 물리 페이지를 사용자 주소에 연결한다. 페이지 단위라 offset은 페이지 배수다(2.5절의 그 규칙).
- **dma-buf**: 버퍼를 **파일 디스크립터**로 감싸서 장치 사이에 넘기는 커널 프레임워크. 카메라 드라이버가 만든 프레임 버퍼를 fd 하나로 NPU 드라이버에 넘기면 복사 없이(zero-copy) 공유된다. 만든 쪽을 exporter, 받아 쓰는 쪽을 importer라고 부른다(docs.kernel.org dma-buf 문서).
- **fence**: "이 일이 끝나면 신호가 오는 객체". 펌웨어의 completion 플래그 + 인터럽트를 커널 객체로 만든 것이다. dma-fence를 쓰면 "카메라 캡처 완료 → NPU 실행 → 디스플레이"를 CPU가 기다리지 않고 체인으로 엮을 수 있다.

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="o2d" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="110" y="24" font-size="13" text-anchor="middle">사용자 공간 (runtime .so)</text>
<text x="350" y="24" font-size="13" text-anchor="middle">커널 드라이버</text>
<text x="575" y="24" font-size="13" text-anchor="middle">NPU 펌웨어 / HW</text>
<line x1="110" y1="34" x2="110" y2="356" stroke="currentColor" stroke-dasharray="3 4"/>
<line x1="350" y1="34" x2="350" y2="356" stroke="currentColor" stroke-dasharray="3 4"/>
<line x1="575" y1="34" x2="575" y2="356" stroke="currentColor" stroke-dasharray="3 4"/>
<line x1="110" y1="60" x2="348" y2="60" stroke="#4a7bd0" stroke-width="2" marker-end="url(#o2d)"/><text x="120" y="54" font-size="12">① open() + ioctl(ALLOC_BO)</text>
<line x1="350" y1="90" x2="112" y2="90" stroke="#4a7bd0" stroke-dasharray="5 3" marker-end="url(#o2d)"/><text x="190" y="84" font-size="12">handle, mmap offset</text>
<text x="360" y="84" font-size="12">페이지 할당 + IOMMU 매핑</text>
<line x1="110" y1="120" x2="348" y2="120" stroke="#4a7bd0" stroke-width="2" marker-end="url(#o2d)"/><text x="120" y="114" font-size="12">② mmap(offset) → CPU 주소</text>
<rect x="20" y="134" width="180" height="36" rx="4" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/><text x="30" y="150" font-size="12">③ 입력 쓰기 (CPU 캐시)</text><text x="30" y="165" font-size="12">SYNC: cache clean</text>
<line x1="110" y1="196" x2="348" y2="196" stroke="#4a7bd0" stroke-width="2" marker-end="url(#o2d)"/><text x="120" y="190" font-size="12">④ ioctl(SUBMIT, job)</text>
<line x1="350" y1="224" x2="573" y2="224" stroke="#e08a3c" stroke-width="2" marker-end="url(#o2d)"/><text x="360" y="218" font-size="12">⑤ ring에 descriptor + doorbell</text>
<rect x="490" y="236" width="170" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="500" y="252" font-size="12">DMA로 입력 읽기, 실행</text><text x="500" y="267" font-size="12">출력 DRAM에 쓰기</text>
<line x1="575" y1="290" x2="352" y2="290" stroke="#d0564a" stroke-width="2" marker-end="url(#o2d)"/><text x="400" y="284" font-size="12">⑥ 완료 IRQ</text>
<line x1="350" y1="314" x2="112" y2="314" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="5 3" marker-end="url(#o2d)"/><text x="160" y="308" font-size="12">⑦ fence signal → WAIT 리턴</text>
<rect x="20" y="322" width="190" height="34" rx="4" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/><text x="30" y="337" font-size="12">⑧ SYNC: cache invalidate</text><text x="30" y="351" font-size="12">그다음 출력 읽기</text>
</svg>
```

그림 5 — 사용자 공간 ↔ 커널 드라이버 ↔ NPU 펌웨어의 한 번 추론 시퀀스. ③과 ⑧의 캐시 동기화가 빠지면 숫자가 조용히 틀린다(아래 C 예제의 [A]·[C]).

### 4.3 캐시 동기화 — 이 절에서 제일 중요한 한 가지

CPU는 캐시를 통해 메모리를 본다. NPU의 DMA는 (하드웨어가 coherent하지 않다면) **DRAM을 직접** 본다. 그래서:

- CPU가 입력을 쓰고 나서 **clean**(캐시 → DRAM에 내보내기)하지 않으면, NPU는 DRAM에 남은 **옛 데이터**를 읽는다.
- NPU가 출력을 DRAM에 쓴 뒤 CPU가 **invalidate**(캐시의 옛 줄 버리기)하지 않으면, CPU는 캐시에 남은 **옛 결과**를 읽는다.

이 원리 자체는 E7(캐시·coherence)과 E8 3.6절에서 다뤘다. 여기서 새로운 것은 **사용자 공간에서 그것을 어떻게 요청하느냐**다. dma-buf 문서는 CPU가 매핑된 버퍼에 접근하기 전에 `DMA_BUF_IOCTL_SYNC`를 `DMA_BUF_SYNC_START`와 읽기/쓰기 플래그로 부르고, 접근이 끝나면 `DMA_BUF_SYNC_END`로 다시 부르라고 한다(docs.kernel.org, 확인일 2026-10-04). 드라이버가 그 안에서 clean/invalidate를 해 준다. Android는 Android 12 + GKI 2.0부터 ION 대신 **DMA-BUF heaps**(`/dev/dma_heap/<heap 이름>`)를 쓰며, 캐시를 쓰지 않는 `system_uncached` 같은 heap 변형도 있다(source.android.com, 확인일 2026-10-04).

### 4.4 ioctl 번호 해부 — 32비트 안에 struct 크기가 들어 있다

Linux의 ioctl 번호(asm-generic 규칙)는 32비트를 이렇게 나눈다.

```
 31 30 | 29 ............ 16 | 15 ...... 8 | 7 ....... 0
  dir  |   size (14 bit)    | type (문자) |  nr (번호)
 dir: 0 = NONE, 1 = WRITE(user→kernel), 2 = READ(kernel→user), 3 = READ|WRITE
```

**손으로 먼저**: 커널 uAPI 헤더 `include/uapi/misc/fastrpc.h`에 `FASTRPC_IOCTL_INVOKE = _IOWR('R', 3, struct fastrpc_invoke)`가 있고, 이 struct는 `u32 handle, u32 sc, u64 args` = 16 B다. 그러면

```
dir  = 3        → 3 << 30  = 0xC0000000
size = 16       → 16 << 16 = 0x00100000
type = 'R'=0x52 → 0x52 << 8 = 0x00005200
nr   = 3        →             0x00000003
합               = 0xC0105203
```

무엇을 확인하는 코드인지: 실제 커널 uAPI 헤더의 정의(fastrpc, dma-buf)를 ctypes struct로 옮겨 ioctl 번호를 계산하고, struct 크기가 바뀌면 번호가 어떻게 바뀌는지 본다.

```python
import ctypes as C
# Linux asm-generic/ioctl.h 규칙: dir(2) | size(14) | type(8) | nr(8)
NONE, WRITE, READ = 0, 1, 2
def IOC(d, t, nr, size): return (d << 30) | (size << 16) | (ord(t) << 8) | nr
def decode(code):
    d = {0: "NONE", 1: "W", 2: "R", 3: "RW"}[code >> 30]
    return f"0x{code:08x}  dir={d} size={(code >> 16) & 0x3fff} type='{chr((code >> 8) & 0xff)}' nr={code & 0xff}"
class fastrpc_invoke(C.Structure):          # include/uapi/misc/fastrpc.h
    _fields_ = [("handle", C.c_uint32), ("sc", C.c_uint32), ("args", C.c_uint64)]
class dma_buf_sync(C.Structure):            # include/uapi/linux/dma-buf.h
    _fields_ = [("flags", C.c_uint64)]
codes = {
    "FASTRPC_IOCTL_INVOKE": IOC(READ | WRITE, "R", 3, C.sizeof(fastrpc_invoke)),
    "FASTRPC_IOCTL_INIT_ATTACH": IOC(NONE, "R", 4, 0),
    "DMA_BUF_IOCTL_SYNC": IOC(WRITE, "b", 0, C.sizeof(dma_buf_sync)),
}
for name, c in codes.items(): print(f"{name:26s} {decode(c)}")
# 같은 nr인데 struct 크기만 커지면 코드가 바뀐다 -> 옛 바이너리와 새 커널의 ABI 문제
print("size 16 -> 24:", hex(IOC(3, "R", 3, 16)), "->", hex(IOC(3, "R", 3, 24)))
```

```text
FASTRPC_IOCTL_INVOKE       0xc0105203  dir=RW size=16 type='R' nr=3
FASTRPC_IOCTL_INIT_ATTACH  0x00005204  dir=NONE size=0 type='R' nr=4
DMA_BUF_IOCTL_SYNC         0x40086200  dir=W size=8 type='b' nr=0
size 16 -> 24: 0xc0105203 -> 0xc0185203
```

출력에서 볼 것: 손계산 `0xC0105203`과 같다. 마지막 줄이 핵심이다 — **struct에 필드 하나만 추가해도 ioctl 번호가 바뀐다.** 옛 사용자 공간 lib이 새 커널에 `0xC0105203`을 보내면, 커널이 새 번호만 안다면 `ENOTTY`로 거절된다. 그래서 커널 uAPI는 "struct는 절대 바꾸지 않고 새 ioctl을 추가"하거나, "struct 크기로 버전을 판별하고 모르는 꼬리는 0이어야 한다"는 규칙을 쓴다. Don이 SSD에서 호스트 툴과 펌웨어 사이 vendor-specific 명령 버전을 관리하던 것과 같은 문제다(F8 2.5절 버전 handshake).

### 4.5 직접 만들기 — 가짜 NPU 드라이버와 사용자 공간 클라이언트 (C)

이제 위 내용을 전부 하나의 프로그램에 넣는다. macOS에는 커널 드라이버를 만들 수 없으니 역할을 이렇게 흉내 낸다.

| 진짜 시스템 | 이 프로그램 |
|---|---|
| DRAM (장치가 DMA로 보는 메모리) | 파일을 `MAP_SHARED`로 `mmap` 한 영역 |
| CPU 캐시 (non-coherent) | BO마다 `malloc` 한 그림자 버퍼. SYNC를 불러야 DRAM과 맞춰진다 |
| 커널 드라이버의 `ioctl` 핸들러 | `npu_ioctl(dev, code, arg)` 함수 — 번호는 4.4절 규칙으로 인코딩 |
| 명령 ring + doorbell 레지스터 | 공유 영역 page 0의 `struct ctrl` (C11 atomic) |
| NPU 펌웨어 | pthread 하나. doorbell 값이 바뀌면 ring을 소비하고 `irq_count`를 올린다 |
| dma-fence | `cq_done` 카운터. `WAIT`는 fence 값까지 기다린다 |

연산은 단순하게 "int8 입력 n개의 합을 int32로" 한다. 확인하는 시나리오:

- [A] v1 클라이언트가 입력을 쓰고 **clean을 빠뜨림** → 장치는 0을 읽는다.
- [B] v2 클라이언트가 clean → submit → wait → invalidate를 다 함 → 정답.
- [C] 새 입력은 제대로 보냈지만 **invalidate를 빠뜨림** → 옛 결과를 읽는다.
- [D] 모르는 플래그 비트 → `-EINVAL`.
- [E] 미래 버전(v3) struct: 모르는 꼬리가 0이 아니면 `-E2BIG`, 0이면 받아 준다.

**손으로 먼저**: 입력 64개가 `base + i % 4`다. base = 1이면 1, 2, 3, 4가 16번 반복 → 합 = 16 × 10 = 160. base = -3이면 -3, -2, -1, 0 → 합 = 16 × (-6) = -96. `struct submit_v1`은 `u32` 세 개 = 12 B → SUBMIT 번호 = `0xC0000000 | (12 << 16) | ('N'=0x4E << 8) | 3` = `0xC00C4E03`.

코드는 두 조각이다. 첫 조각은 "커널 드라이버 + 펌웨어" 쪽이다.

```c
/* fakenpu.c — 사용자 공간 ↔ "NPU 드라이버" 인터페이스 흉내 (macOS/Linux 호스트용)
 * 파일 = DRAM, 공유 mmap = 장치가 보는 메모리, malloc 그림자 = CPU 캐시, pthread = NPU 펌웨어 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define PAGE 16384u
#define NBO 3u
#define RING 8u
#define IOC(nr, sz) ((3u << 30) | (((uint32_t)(sz) & 0x3fffu) << 16) | ('N' << 8) | (nr))
#define IOC_NR(c) ((c) & 0xffu)
#define IOC_SZ(c) (((c) >> 16) & 0x3fffu)
enum { NR_ALLOC = 1, NR_SYNC = 2, NR_SUBMIT = 3, NR_WAIT = 4 };
enum { SYNC_TO_DEV = 1, SYNC_FROM_DEV = 2 };
#define SUBMIT_F_PROFILE 0x1u /* v2에서 생긴 플래그. 나머지 비트는 예약 */

struct alloc_req { uint32_t size, handle; uint64_t mmap_offset; };
struct sync_req { uint32_t handle, dir; };
struct submit_v1 { uint32_t bo_in, bo_out, n; };
struct submit_v2 { uint32_t bo_in, bo_out, n, flags; uint64_t user_tag; uint32_t fence; uint32_t pad; };
struct cmd { uint32_t bo_in, bo_out, n, flags; uint64_t tag; };
struct ctrl {                       /* 공유 메모리 page 0: 레지스터 + 링 */
    _Atomic uint32_t sq_tail, cq_done, doorbell, irq_count, stop;
    uint32_t sq_head;               /* 장치만 쓴다 */
    struct cmd sq[RING];
};
struct dev { int fd; uint8_t *dram; struct ctrl *c; uint8_t *cache[NBO]; uint32_t nbo; pthread_t fw; };

static void *firmware(void *arg) {  /* NPU 펌웨어: doorbell을 보고 링을 소비한다 */
    struct dev *d = arg; uint32_t seen = 0;
    const struct timespec nap = {0, 100000};
    while (!atomic_load(&d->c->stop)) {
        uint32_t db = atomic_load_explicit(&d->c->doorbell, memory_order_acquire);
        if (db == seen) { nanosleep(&nap, NULL); continue; }
        seen = db;
        uint32_t tail = atomic_load_explicit(&d->c->sq_tail, memory_order_acquire);
        while (d->c->sq_head != tail) {
            struct cmd k = d->c->sq[d->c->sq_head % RING];
            const int8_t *in = (const int8_t *)(d->dram + PAGE * (1 + k.bo_in));
            int32_t acc = 0;
            for (uint32_t i = 0; i < k.n; i++) acc += in[i];
            memcpy(d->dram + PAGE * (1 + k.bo_out), &acc, sizeof acc);
            d->c->sq_head++;
            atomic_fetch_add_explicit(&d->c->cq_done, 1, memory_order_release);
            atomic_fetch_add(&d->c->irq_count, 1);          /* "인터럽트" */
        }
    }
    return NULL;
}

static int npu_ioctl(struct dev *d, uint32_t code, void *arg) {   /* 커널 드라이버 역할 */
    switch (IOC_NR(code)) {
    case NR_ALLOC: {
        struct alloc_req *r = arg;
        if (IOC_SZ(code) != sizeof *r) return -ENOTTY;
        if (d->nbo == NBO || r->size > PAGE) return -ENOMEM;
        r->handle = d->nbo; r->mmap_offset = (uint64_t)PAGE * (1 + d->nbo);
        d->cache[d->nbo++] = calloc(1, PAGE);
        return 0; }
    case NR_SYNC: {                 /* DMA_BUF_IOCTL_SYNC 흉내: clean / invalidate */
        struct sync_req *r = arg; uint8_t *mem = d->dram + PAGE * (1 + r->handle);
        if (r->handle >= d->nbo) return -EINVAL;
        if (r->dir == SYNC_TO_DEV) memcpy(mem, d->cache[r->handle], PAGE);
        else memcpy(d->cache[r->handle], mem, PAGE);
        return 0; }
    case NR_SUBMIT: {               /* 확장 가능한 struct: 크기로 버전을 판별 */
        struct submit_v2 s = {0}; uint32_t sz = IOC_SZ(code);
        if (sz < sizeof(struct submit_v1)) return -EINVAL;
        if (sz > sizeof s) {        /* 미래 버전: 모르는 꼬리는 0이어야 한다 */
            for (uint32_t i = sizeof s; i < sz; i++) if (((uint8_t *)arg)[i]) return -E2BIG;
            sz = sizeof s;
        }
        memcpy(&s, arg, sz);
        if (s.flags & ~SUBMIT_F_PROFILE) return -EINVAL;
        uint32_t t = atomic_load(&d->c->sq_tail);
        d->c->sq[t % RING] = (struct cmd){s.bo_in, s.bo_out, s.n, s.flags, s.user_tag};
        atomic_store_explicit(&d->c->sq_tail, t + 1, memory_order_release);
        atomic_fetch_add_explicit(&d->c->doorbell, 1, memory_order_release);  /* 초인종 */
        if (IOC_SZ(code) >= sizeof s) ((struct submit_v2 *)arg)->fence = t + 1;
        return 0; }
    case NR_WAIT: {                 /* fence 값까지 완료되기를 기다린다 */
        uint32_t want = *(uint32_t *)arg; const struct timespec nap = {0, 50000};
        for (int i = 0; i < 20000; i++) {
            if (atomic_load_explicit(&d->c->cq_done, memory_order_acquire) >= want) return 0;
            nanosleep(&nap, NULL);
        }
        return -ETIMEDOUT; }
    }
    return -ENOTTY;
}
```

둘째 조각은 사용자 공간 클라이언트(런타임 lib 역할)와 시나리오다. 두 조각을 이어 붙여 한 파일로 컴파일한다.

```c

static int32_t read_out(struct dev *d, uint32_t h) { int32_t v; memcpy(&v, d->cache[h], sizeof v); return v; }
static void fill(struct dev *d, uint32_t h, int8_t base) { for (int i = 0; i < 64; i++) ((int8_t *)d->cache[h])[i] = (int8_t)(base + i % 4); }

int main(void) {
    struct dev d = {0};
    d.fd = open("npu0.bin", O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (d.fd < 0 || ftruncate(d.fd, PAGE * (1 + NBO)) != 0) return 1;
    d.dram = mmap(NULL, PAGE * (1 + NBO), PROT_READ | PROT_WRITE, MAP_SHARED, d.fd, 0);
    if (d.dram == MAP_FAILED) return 1;
    d.c = (struct ctrl *)d.dram;
    pthread_create(&d.fw, NULL, firmware, &d);

    struct alloc_req a_in = {64, 0, 0}, a_out = {4, 0, 0};
    npu_ioctl(&d, IOC(NR_ALLOC, sizeof a_in), &a_in);
    npu_ioctl(&d, IOC(NR_ALLOC, sizeof a_out), &a_out);
    printf("BO in=%u off=0x%llx, out=%u off=0x%llx (page-aligned: %s)\n", a_in.handle,
           (unsigned long long)a_in.mmap_offset, a_out.handle, (unsigned long long)a_out.mmap_offset,
           (a_in.mmap_offset % PAGE == 0 && a_out.mmap_offset % PAGE == 0) ? "yes" : "no");
    printf("SUBMIT code v1=0x%08x v2=0x%08x\n", IOC(NR_SUBMIT, sizeof(struct submit_v1)),
           IOC(NR_SUBMIT, sizeof(struct submit_v2)));
    struct sync_req to_dev = {a_in.handle, SYNC_TO_DEV}, from_dev = {a_out.handle, SYNC_FROM_DEV};

    fill(&d, a_in.handle, 1);                                  /* 기대값 64개 × 평균 2.5 = 160 */
    struct submit_v1 s1 = {a_in.handle, a_out.handle, 64};     /* 실수: SYNC_TO_DEV 안 함 */
    npu_ioctl(&d, IOC(NR_SUBMIT, sizeof s1), &s1);
    uint32_t f = 1; npu_ioctl(&d, IOC(NR_WAIT, 4), &f);
    npu_ioctl(&d, IOC(NR_SYNC, sizeof from_dev), &from_dev);
    printf("[A] v1, no clean        : out=%d (expect 160)\n", read_out(&d, a_out.handle));

    npu_ioctl(&d, IOC(NR_SYNC, sizeof to_dev), &to_dev);
    struct submit_v2 s2 = {a_in.handle, a_out.handle, 64, SUBMIT_F_PROFILE, 0xC0FFEE, 0, 0};
    npu_ioctl(&d, IOC(NR_SUBMIT, sizeof s2), &s2);
    npu_ioctl(&d, IOC(NR_WAIT, 4), &s2.fence);
    npu_ioctl(&d, IOC(NR_SYNC, sizeof from_dev), &from_dev);
    printf("[B] v2, clean+invalidate: out=%d fence=%u\n", read_out(&d, a_out.handle), s2.fence);

    fill(&d, a_in.handle, -3);                                 /* 새 입력: 기대값 64 × (-1.5) = -96 */
    npu_ioctl(&d, IOC(NR_SYNC, sizeof to_dev), &to_dev);
    npu_ioctl(&d, IOC(NR_SUBMIT, sizeof s2), &s2);
    npu_ioctl(&d, IOC(NR_WAIT, 4), &s2.fence);                 /* 실수: invalidate 안 함 */
    printf("[C] no invalidate       : out=%d (expect -96, stale cache)\n", read_out(&d, a_out.handle));
    npu_ioctl(&d, IOC(NR_SYNC, sizeof from_dev), &from_dev);
    printf("[C'] after invalidate   : out=%d\n", read_out(&d, a_out.handle));

    struct submit_v2 bad = s2; bad.flags = 0x4;
    printf("[D] unknown flag        : ret=%d (-EINVAL=%d)\n", npu_ioctl(&d, IOC(NR_SUBMIT, sizeof bad), &bad), -EINVAL);
    struct { struct submit_v2 base; uint32_t future; uint32_t pad; } v3 = {s2, 7, 0};
    printf("[E] v3 tail!=0          : ret=%d (-E2BIG=%d)\n", npu_ioctl(&d, IOC(NR_SUBMIT, sizeof v3), &v3), -E2BIG);
    v3.future = 0;
    printf("[E'] v3 tail==0         : ret=%d\n", npu_ioctl(&d, IOC(NR_SUBMIT, sizeof v3), &v3));
    uint32_t last = atomic_load(&d.c->sq_tail); npu_ioctl(&d, IOC(NR_WAIT, 4), &last);
    printf("irq_count=%u sq_tail=%u\n", atomic_load(&d.c->irq_count), last);

    atomic_store(&d.c->stop, 1);
    pthread_join(d.fw, NULL);
    for (uint32_t i = 0; i < d.nbo; i++) free(d.cache[i]);
    munmap(d.dram, PAGE * (1 + NBO)); close(d.fd); unlink("npu0.bin");
    return 0;
}
```

```sh
cat fakenpu.c fakenpu_main.c > fakenpu_all.c
cc -std=c11 -Wall -Wextra -O2 fakenpu_all.c -o fakenpu && ./fakenpu
```

```text
BO in=0 off=0x4000, out=1 off=0x8000 (page-aligned: yes)
SUBMIT code v1=0xc00c4e03 v2=0xc0204e03
[A] v1, no clean        : out=0 (expect 160)
[B] v2, clean+invalidate: out=160 fence=2
[C] no invalidate       : out=160 (expect -96, stale cache)
[C'] after invalidate   : out=-96
[D] unknown flag        : ret=-22 (-EINVAL=-22)
[E] v3 tail!=0          : ret=-7 (-E2BIG=-7)
[E'] v3 tail==0         : ret=0
irq_count=4 sq_tail=4
```

출력에서 볼 것:

- **BO offset이 `0x4000`, `0x8000`** — 페이지(16 KB) 배수다. 사용자 공간은 이 값을 그대로 `mmap` offset으로 쓴다. SUBMIT 번호 `0xc00c4e03`(v1, 12 B)과 `0xc0204e03`(v2, 32 B)이 손계산·4.4절 규칙과 같다. 같은 명령이 struct 크기만으로 구별된다.
- **[A] out=0**: 장치는 정상적으로 일했고 인터럽트도 왔다. 그런데 입력이 CPU "캐시"에만 있고 DRAM에 없어서 0을 더했다. **에러 코드도 크래시도 없이 숫자만 틀린다** — 실제 기기에서 가장 찾기 어려운 종류의 버그다.
- **[B] 160**: 손계산과 같다. fence 값 2 = 두 번째로 제출된 job.
- **[C] 160 → [C'] -96**: 장치는 새 입력으로 -96을 DRAM에 썼지만, invalidate 전에는 CPU가 옛 결과 160을 본다. "출력이 한 프레임 늦게 바뀐다"는 증상으로 나타난다.
- **[D] -22, [E] -7, [E'] 0**: 모르는 플래그는 거절(미래에 의미가 생길 비트를 지금 무시하면 안 된다), 모르는 꼬리가 0이면 호환. 이것이 Linux가 확장 가능한 struct를 다루는 방식과 같은 원리다.
- **irq_count=4**: 성공한 submit 4번(A, B, C, E')마다 완료 인터럽트가 하나씩.

이 프로그램에서 일부러 생략한 것(진짜 드라이버에는 있다): 사용자 포인터를 커널로 복사하는 `copy_from_user`, BO 참조 카운트와 해제, IOMMU 매핑, 전원 관리(runtime PM), 타임아웃 뒤 장치 리셋, 여러 프로세스 사이의 격리. 펌웨어 쪽에서 본 같은 구조의 상태기계는 J1 3.5절, 공유 링의 메모리 순서 규칙은 E8 3.3~3.5절이다.

### 4.6 실제 Linux의 NPU 드라이버 지형 — accel 서브시스템

예전에는 NPU 드라이버마다 자기 방식(misc 디바이스, 벤더 전용 char 디바이스)으로 노출했다. Linux는 이를 정리하려고 **compute accelerator(accel) 서브시스템**을 만들었다. 커널 문서의 요점(확인일 2026-10-04, docs.kernel.org/accel):

- accel 장치는 전용 major 번호 **261**과 `/dev/accel/accel*` 문자 디바이스로 사용자 공간에 노출된다.
- accel 코어는 **DRM 서브시스템의 일부**이고, accel 장치는 새로운 종류의 DRM 장치다. 그래서 GEM buffer object, dma-buf, fence, 스케줄러 같은 GPU용 기반을 그대로 재사용한다.
- 드라이버는 `drm_driver`의 기능 플래그에 `DRIVER_COMPUTE_ACCEL`을 넣는다(`DRIVER_RENDER`, `DRIVER_MODESET`과 동시 사용 불가 — "디스플레이 없는 GPU"라는 뜻).

| 드라이버 | 하드웨어 | 메모 (출처) |
|---|---|---|
| `qaic` | Qualcomm Cloud AI 100 (AIC100) — PCIe 카드 | `DRM_IOCTL_QAIC_EXECUTE_BO`, `WAIT_BO` 등 BO 중심 uAPI (docs.kernel.org/accel/qaic) |
| `amdxdna` | AMD Ryzen AI NPU | docs.kernel.org/accel 목록 |
| `rocket` | Rockchip RK3588 NPU | 사용자 공간은 Mesa3D의 같은 이름 Gallium 드라이버. 커널은 전원, 버퍼 할당·매핑, job 제출만 하고 무거운 일은 사용자 공간 (docs.kernel.org/accel/rocket) |
| `ivpu` | Intel Core Ultra NPU | `drivers/accel/ivpu/`, `/dev/accel/accel0` (Intel linux-npu-driver 저장소 문서) |

`rocket`의 설계 철학은 기억할 만하다: **커널은 얇게(전원, 메모리, 제출), 컴파일러·스케줄링 같은 무거운 일은 사용자 공간 라이브러리에서.** QNN, Intel NPU 드라이버 같은 대부분의 상용 스택도 같은 분담이다. 그래서 "NPU 드라이버를 부른다"는 말은 실제로는 **벤더 사용자 공간 lib을 부른다**는 뜻이고, 그 lib이 위의 다섯 시스템 콜을 대신 해 준다.

### 4.7 Qualcomm Hexagon — FastRPC

Qualcomm 모바일 SoC의 NPU(HTP, Hexagon Tensor Processor)는 cDSP(compute DSP) 안에 있다(E4). 메인라인 Linux에서 AP가 Hexagon DSP와 대화하는 통로는 accel이 아니라 **FastRPC** misc 드라이버다.

- DSP 도메인마다 노드가 생긴다: `adsp`, `mdsp`, `sdsp`, `cdsp` (예: `/dev/fastrpc-cdsp`). 전송 계층은 RPMsg다(커널 DT binding 문서 `qcom,fastrpc`).
- uAPI(`include/uapi/misc/fastrpc.h`)에는 `FASTRPC_IOCTL_INIT_CREATE`(DSP 쪽 프로세스 생성), `FASTRPC_IOCTL_INVOKE`(원격 함수 호출), `FASTRPC_IOCTL_ALLOC_DMA_BUFF`, `FASTRPC_IOCTL_MMAP`/`MUNMAP`, `FASTRPC_IOCTL_MEM_MAP` 등이 있다. `struct fastrpc_invoke_args`는 `ptr, length, fd, attr` — **버퍼를 포인터 또는 dma-buf fd로** 넘긴다.
- 사용자 공간에서는 Hexagon SDK가 IDL로 stub(AP 쪽)·skel(DSP 쪽) 코드를 생성하고, stub이 위 ioctl을 부른다. QNN HTP backend도 이 경로로 DSP에 skel 라이브러리를 올려 그래프를 실행한다(F4 2.7절).
- Android 기기의 Qualcomm 다운스트림 커널은 메인라인과 다른 노드 이름이나 드라이버를 쓰는 경우가 있다 — 기기에서 `ls /dev` 로 직접 확인한다(**확인 필요**, 기기·커널 버전마다 다름).
- 2026년 LKML에는 Qualcomm DSP를 accel 서브시스템으로 옮기는 `accel/qda` 패치 시리즈가 올라와 있다. 머지 여부와 시점은 **확인 필요**다. 방향은 4.6절의 정리 흐름과 같다.

FastRPC 호출 하나는 함수 호출처럼 보이지만 실제로는 "ioctl → 커널 → 공유 메모리 + 인터럽트 → DSP 깨우기 → 캐시 유지보수 → 결과 복귀"다(E8 3.7절 RPC 비용 감각). 그래서 **작은 호출을 여러 번 하지 말고 그래프 전체를 한 번에** 넘기는 것이 QNN·LiteRT NPU 경로의 기본 설계다.

---

## 5. 기기 위 성능 도구 — 어디까지가 이 노트인가

도구 사용법의 깊이는 K2에 있다. 여기서는 "Android 기기에서는 무엇을 쓰나"만 정리한다.

| 도구 | 무엇을 보나 | 기기에서 | 자세히 |
|---|---|---|---|
| `simpleperf` | CPU 샘플링, PMU 카운터 (`stat`, `record`, `report`) | NDK에 포함. `simpleperf report --sort dso`로 어느 `.so`가 시간을 먹는지 | K2 5.5절, developer.android.com simpleperf |
| `perf` | 같은 일 (Linux 표준) | 임베디드 Linux 기기에서. Android에서는 보통 simpleperf | K2 |
| ftrace | 커널 tracepoint, 함수 trace | Perfetto가 내부에서 ftrace를 쓴다 | K2 |
| `ATrace_beginSection` | 내 코드 구간 표시 | NDK API, Perfetto에 사용자 구간으로 보임 | K2 6.2절 |
| 벤더 프로파일러 | NPU op별 시간, HTP 사이클 | QNN profiling level, ORT QNN EP 프로파일링 | F4, F8 6절 |

Android에서 지연 문제를 볼 때의 순서(Don의 "측정 기반 디버깅" 그대로): logcat 지연 로그로 **언제** 나빠지는지(1.7절) → 같은 시간대의 thermal·cpufreq(1.6절, K4) → Perfetto로 **어느 core에서 누구에게 선점되며** 돌았는지(K2) → simpleperf로 CPU 시간이 **어느 `.so`·함수**인지, NPU 경로인데 CPU 커널이 보이면 fallback(F8 4절) → 벤더 프로파일러로 NPU 안(F4).

---

## 6. 임베디드 관점에서 다시 보기

### 6.1 Don의 경험 ↔ 이 노트의 개념

| Don이 해 본 것 | Android/Linux 쪽 이름 | 차이 |
|---|---|---|
| JTAG로 이미지 다운로드 | `adb push`, `fastboot flash` | 파일 단위 vs 파티션 단위 |
| 링커 스크립트로 섹션 배치 | `Android.bp`의 `vendor: true`, PT_LOAD 16 KB 정렬 | 배치 단위가 섹션 → 파티션·페이지 |
| MPU/TrustZone 접근 제어 | SELinux 정책, linker namespace | 위반이 hard fault가 아니라 `EACCES`·`dlopen` 실패로 나타난다 |
| NVMe SQ/CQ, doorbell, MSI-X | 드라이버 ring, doorbell, IRQ → dma-fence | 이번에는 호스트 쪽 |
| DMA 전 cache clean, 후 invalidate | `DMA_BUF_IOCTL_SYNC` START/END, uncached heap | 사용자 공간이 "요청"하고 커널이 실행 |
| 호스트 툴 ↔ 펌웨어 명령 버전 관리 | ioctl 번호의 size 필드, 확장 가능한 struct | 커널은 사용자 공간 ABI를 깨지 않는 것이 원칙 |
| 성능 측정 때 클럭 고정 | governor, `taskset`, 열 상태 기록 | 스케줄러와 열 정책이 계속 개입 |

### 6.2 Hark 같은 기기에서의 배치 감각 (추정)

- **Always-on 경로**(wake word, 착용 감지)는 AP의 Android 위가 아니라 저전력 DSP/센서 허브나 별도 MCU에서 돈다(E8). Android 쪽에서 그 경로를 다루는 일은 "펌웨어 이미지를 vendor/firmware 파티션에 넣고, remoteproc으로 띄우고, 결과 이벤트를 받는" 일이다.
- **AP가 깨어난 뒤의 큰 모델**(ASR, 작은 LLM)은 Android 사용자 공간의 네이티브 서비스 + QNN/LiteRT NPU로 돈다. 이 노트의 1~4절이 그 영역이다.
- 두 영역의 경계(공유 메모리 링, FastRPC, 이벤트 전달)가 Don의 경험이 가장 직접 쓰이는 곳이다. 면접에서 "펌웨어와 Android 사이의 다리를 놓을 수 있다"고 말할 근거가 된다.

---

## 7. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| core를 고정하지 않고 벤치 | run마다 지연이 2~4배 흔들림, p99 폭증 | EAS가 little core에서 시작하거나 migration | `taskset` mask + 스레드 수를 클러스터 core 수 이하로, 클러스터 정보 기록 |
| `/data/local/tmp` 결과를 앱 성능으로 보고 | 앱에 넣으면 느리거나 NPU를 못 씀 | 앱 도메인의 SELinux·namespace·cpuset이 다름 | 앱(또는 같은 도메인의 서비스) 안에서 다시 측정 |
| 4 KB 정렬 `.so` 배포 | 16 KB 페이지 기기에서 `dlopen` 실패, 앱 크래시 | PT_LOAD align `2**12` | NDK r28+, 또는 `-Wl,-z,max-page-size=16384`, CI에 정렬 검사 |
| 벤더 `.so`를 앱에서 직접 `dlopen` | `library not found` / namespace 오류 | `/vendor/lib64`는 앱에 비공개 | SDK lib은 APK에 포함, vendor 공개 lib 목록 확인 |
| 새 서비스가 `/dev` 노드를 못 엶 | `EACCES`, logcat에 `avc: denied` | sepolicy 규칙 없음 | 도메인·라벨에 allow 규칙 추가 (permissive는 진단용으로만) |
| 신규 프로젝트에 NNAPI 사용 | 기기마다 결과·속도가 들쭉날쭉, 일부 기기에서 CPU로 실행 | Android 15부터 deprecated, 벤더 드라이버 품질 차이 | LiteRT GPU/NPU, QNN, ORT QNN EP로 이전 |
| 입력 쓰고 cache clean 생략 | 에러 없이 출력이 0 또는 이전 입력 결과 | non-coherent DMA가 DRAM의 옛 값을 읽음 | `DMA_BUF_IOCTL_SYNC` START/END 짝 맞추기 |
| 출력 읽기 전 invalidate 생략 | 결과가 한 프레임 늦게 바뀜 | CPU 캐시에 옛 출력 줄이 남음 | 완료(fence) 뒤 SYNC 후 읽기 |
| uAPI struct에 필드 추가하고 같은 번호 재사용 | 옛 lib과 새 커널 조합에서 `ENOTTY` 또는 쓰레기 값 | ioctl 번호에 size가 들어 있음, 꼬리 검사 없음 | 새 ioctl 추가 또는 size 기반 버전 + 꼬리 0 검사 |

---

## 8. 면접에서 이렇게 말한다

**Q.** "How would you benchmark a model on an Android device so that the numbers are trustworthy?"

**A.** 기기 fingerprint(빌드, SoC, 런타임 버전)를 먼저 기록하고, adb로 바이너리를 올려 core를 `taskset`으로 고정하고, 스레드 수를 그 클러스터에 맞추고, 워밍업 후 수백 회 돌려 p50·p99를 본다. 앞뒤로 온도와 주파수를 찍고, governor 고정 숫자와 기본 정책 숫자를 따로 보고한다. 마지막으로 앱 안에서 다시 재서 `/data/local/tmp` 숫자와 비교한다.

> "I record the device fingerprint first, push the binary with adb, pin it to a known cluster with taskset, and match the thread count to that cluster. After warm-up I run a few hundred iterations and report p50 and p99, with temperature and CPU frequency logged before and after. I report both a pinned, fixed-governor number for A/B comparisons and a default-policy number for user-facing latency, and I re-measure inside the app process because SELinux, linker namespaces and cpusets differ from the adb shell."

**Q.** "NNAPI is deprecated. What would you use on a Snapdragon-based device instead?"

**A.** Android 15에서 deprecated 됐고, Google은 업데이트 가능한 런타임(LiteRT)과 GPU delegate를 권한다. Qualcomm NPU는 LiteRT의 QNN 경로, QNN SDK 직접 사용, 또는 ONNX Runtime QNN EP로 쓴다. 자체 기기처럼 SoC가 고정이면 QNN context binary나 AOT 컴파일로 SoC 전용 산출물을 만들고, CPU 기준선과 정확도 게이트를 항상 함께 둔다.

> "NNAPI was deprecated in Android 15, and Google now points to updatable runtimes like LiteRT plus the GPU delegate. For the Qualcomm NPU I'd go through QNN — either LiteRT's Qualcomm AI Engine Direct path, the QNN SDK directly, or ONNX Runtime's QNN execution provider, depending on where the model comes from. On a fixed-SoC product I'd prefer an ahead-of-time compiled artifact, such as a QNN context binary, and I'd always keep a CPU baseline and an accuracy gate next to it."

**Q.** "Walk me through what happens between a runtime calling 'execute' and the NPU running the graph on Linux."

**A.** 런타임이 장치 노드를 열고 ioctl로 버퍼를 할당하거나 dma-buf를 가져오고, mmap으로 CPU에 매핑한다. 입력을 쓰고 cache clean을 요청한 뒤 submit ioctl을 부르면, 드라이버가 명령 ring에 descriptor를 쓰고 doorbell을 친다. 펌웨어가 DMA로 읽고 실행해 완료 인터럽트를 올리면 드라이버가 fence를 signal 하고, 사용자 공간은 invalidate 후 결과를 읽는다. NVMe의 SQ/CQ와 같은 구조이고, 나는 그 펌웨어 쪽을 구현해 봤다.

> "The runtime opens the device node, allocates or imports buffers through ioctls — often as dma-buf file descriptors — and maps them with mmap. It writes the input, asks for a cache clean, and submits a job; the kernel driver writes a descriptor into a command ring and rings a doorbell. The firmware DMAs the input, runs the graph, writes the output and raises a completion interrupt, the driver signals a fence, and user space invalidates its cache view before reading. It's the same shape as NVMe submission and completion queues, and I've built the firmware side of that pattern."

**Q.** "What's the 16 KB page size change and why does it matter for ML apps?"

**A.** Android 15부터 64-bit 기기에서 16 KB 페이지를 지원해야 하고, 네이티브 `.so`의 LOAD 세그먼트가 16 KB 정렬이어야 한다. ML 앱은 거의 다 네이티브 런타임을 갖고 있으니 직접 해당된다. NDK r28 이상은 기본으로 맞춰 주고, 그 이하는 링커 플래그가 필요하다. 벤더가 준 미리 빌드된 lib은 내가 못 고치니 CI에서 프로그램 헤더 정렬을 검사해 일찍 잡는다.

> "Starting with Android 15, apps need to work on devices with 16 KB memory pages, which means every native library's LOAD segments must be 16 KB aligned. ML apps almost always ship native runtimes, so they're directly affected. NDK r28 and later align by default; older toolchains need max-page-size linker flags. The real risk is prebuilt vendor libraries, so I'd add an ELF program-header check to CI to catch an unaligned library as soon as it lands."

**Q.** "You see correct results on the CPU path but occasionally stale outputs on the accelerator path. How do you debug it?"

**A.** "가끔 한 프레임 늦은 결과"는 캐시 일관성 서명이다. 버퍼가 cached heap인지, 제출 전 clean과 완료 후 invalidate가 짝 맞게 불리는지, fence를 기다린 뒤에 읽는지 확인한다. 입력에 프레임 번호를 박아 출력과 대조하면 몇 프레임 밀렸는지 바로 보인다. uncached heap으로 바꿔 증상이 사라지면 확정이다.

> "Stale outputs that lag by a frame are the classic signature of a cache-maintenance bug. I'd check whether the buffer comes from a cached heap, whether the begin and end CPU-access syncs are paired around every CPU touch, and whether we actually wait on the completion fence before reading. Stamping a frame counter into the input and checking it in the output tells me exactly how far behind we are, and if switching to an uncached heap makes it disappear, that confirms it."

---

## 9. 직접 해보기

1. **손계산** — core 10개 기기에서 cpu0~3이 little, cpu4~8이 big, cpu9가 prime이다. big 클러스터 전체의 taskset mask와 prime 하나의 mask를 16진수로 쓰라.
   정답: big = 비트 4~8 = `0x1f0`, prime = `1 << 9` = `0x200`.

2. **손계산** — 구조체 `struct npu_info { u32 version; u32 n_cores; u64 sram_bytes; u64 flags; }`를 커널에서 사용자로 읽어 오는 ioctl `_IOR('N', 7, struct npu_info)`의 번호를 계산하라.
   정답: dir = 2(READ), size = 24 → `0x80000000 | 0x00180000 | 0x4E00 | 0x07` = `0x80184E07`.

3. **손계산** — PT_LOAD가 offset `0x5000`, vaddr `0x9000`, align `0x4000`이다. 16 KB 페이지 커널에서 매핑 가능한가?
   정답: `0x5000 mod 0x4000 = 0x1000`, `0x9000 mod 0x4000 = 0x1000` → 합동이고 align도 `2**14`라 OK (offset이 페이지 경계일 필요는 없고 나머지가 같으면 된다).

4. **코드 과제** — 1.7절 logcat 파서를 고쳐서 1초 창마다 추론 횟수와 p95를 계산하고, `deadline miss` 줄이 나온 창을 표시하라. 그다음 O1 노트의 CLI 형태(인자로 파일 경로·태그·예산을 받음)로 바꿔 보라.
   힌트: 시간 문자열 `14:02:11.481`을 초 단위 float로 바꾸고 `int(t)`로 창을 나눈다.

5. **코드 과제** — 4.5절 가짜 NPU에 `NR_GET_INFO` ioctl을 추가해 uAPI 버전과 ring 크기를 돌려주고, 클라이언트가 시작할 때 버전이 다르면 거절하게 하라. 또 [A] 시나리오를 "입력 버퍼를 uncached로 할당하는 플래그"로 고쳐서 SYNC 없이도 맞는 결과가 나오게 하라.
   힌트: uncached BO는 그림자 버퍼 없이 DRAM 포인터를 바로 돌려주면 된다 — 대신 CPU 접근이 느려지는 비용을 주석으로 적어 둘 것.

---

## 10. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| adb | Android Debug Bridge | 호스트 ↔ 기기의 shell·파일 전송·로그 통로 (client, server, adbd) |
| logcat | Android 로그 | 사용자 공간 로그 링버퍼, `-v threadtime` 형식이 표준 |
| taskset | CPU affinity 도구 | 16진 mask로 프로세스를 특정 core에 묶음 |
| governor | cpufreq 정책 | 부하에 따라 주파수를 고르는 커널 정책 (`schedutil`, `performance` …) |
| NDK | Native Development Kit | 앱용 C/C++ 툴체인·헤더·안정 ABI |
| Soong / Android.bp | AOSP 빌드 시스템 | Blueprint 파일을 읽어 Ninja로 빌드 |
| VNDK | Vendor NDK | vendor가 쓰는 system lib 묶음, Android 15부터 deprecated |
| linker namespace | 동적 링커 격리 | 앱이 볼 수 있는 `.so`를 제한하는 규칙 |
| SELinux | 강제 접근 제어 | 라벨·정책으로 root도 막음, 위반은 `avc: denied` |
| 16 KB page | 메모리 페이지 크기 | Android 15+ 요구, `.so`의 PT_LOAD align `2**14` |
| PT_LOAD | ELF 로드 세그먼트 | 로더가 `mmap` 하는 단위, offset ≡ vaddr (mod 페이지) |
| NNAPI | Neural Networks API | Android 8.1~, Android 15에서 deprecated 된 가속 API |
| LiteRT | TFLite의 새 이름 | Google의 온디바이스 런타임, CompiledModel API로 CPU/GPU/NPU |
| QNN / AI Engine Direct | Qualcomm NN SDK | HTP·GPU·CPU backend, context binary |
| ioctl | 장치 제어 시스템 콜 | 번호(dir·size·type·nr) + struct 포인터 |
| BO | buffer object | 드라이버가 관리하는 장치용 버퍼, handle + mmap offset |
| dma-buf | 버퍼 공유 프레임워크 | 버퍼를 fd로 감싸 장치 사이 zero-copy 공유 |
| DMA-BUF heaps | 버퍼 할당자 | `/dev/dma_heap/<이름>`, Android 12부터 ION 대체 |
| dma-fence | 완료 신호 객체 | 비동기 하드웨어 작업 완료를 알리는 커널 객체 |
| accel | compute accelerator 서브시스템 | DRM 기반, `/dev/accel/accel*`, major 261 |
| FastRPC | Qualcomm 원격 호출 | AP → Hexagon DSP 함수 호출, `/dev/fastrpc-*` |
| Perfetto | 시스템 trace 도구 | 스케줄러·주파수·사용자 구간 타임라인 |

---

## 11. 요약 & 체크리스트

Android 기기에서 모델을 돌린다는 것은 앱 → JNI → 네이티브 `.so` → 런타임 → 벤더 lib → 커널 드라이버 → DSP/NPU 펌웨어로 이어지는 층을 한 번에 다루는 일이다. adb로 올리고, core를 고정하고, 온도·주파수·로그를 같이 기록해야 숫자가 재현된다. 빌드는 앱 쪽(NDK + CMake)과 플랫폼 쪽(AOSP + Soong)으로 나뉘고, 배포를 막는 것은 대개 코드가 아니라 16 KB 페이지 정렬, linker namespace, SELinux 같은 규칙이다. NNAPI는 Android 15에서 deprecated 되었고, Qualcomm 기기에서는 LiteRT의 QNN 경로, QNN SDK 직접, ONNX Runtime QNN EP가 현실적인 선택지다. 그 아래에서 런타임은 `open`·`ioctl`·`mmap`·dma-buf·fence로 드라이버와 대화하고, 드라이버는 ring과 doorbell로 펌웨어와 대화한다 — Don이 펌웨어 쪽에서 만들던 구조를 호스트 쪽에서 보는 것이다. 가장 조용하고 위험한 버그는 캐시 동기화 누락이다.

- [ ] Android ML 스택 7층을 그리고 두 경계(user ↔ kernel, AP ↔ DSP)를 설명할 수 있다
- [ ] adb로 바이너리·모델을 올리고 `taskset` mask를 손으로 계산해 core를 고정할 수 있다
- [ ] `/proc/cpuinfo`의 CPU part와 cpufreq로 클러스터를 판별할 수 있다
- [ ] thermal zone 값(milli-degree)과 logcat threadtime 줄을 파싱해 지연 변화의 원인을 시간축으로 맞출 수 있다
- [ ] `/data/local/tmp` 실행과 앱 프로세스 실행의 차이(SELinux, namespace, cpuset)를 말할 수 있다
- [ ] NDK + CMake 빌드와 AOSP Soong 빌드의 차이, `Android.bp`의 `vendor: true` 의미를 설명할 수 있다
- [ ] PT_LOAD 정렬을 손으로 검사해 16 KB 페이지 호환 여부를 판단할 수 있다
- [ ] NNAPI deprecated 사실과 출처, 대체 경로 3가지와 선택 기준을 말할 수 있다
- [ ] ioctl 번호를 dir·size·type·nr로 손으로 인코딩·디코딩할 수 있다
- [ ] clean/invalidate 누락이 만드는 두 증상(0 또는 옛 입력 결과, 한 프레임 늦은 출력)을 재현하고 고칠 수 있다

---

## 참고 자료

- Android Debug Bridge (adb) — https://developer.android.com/tools/adb
- Logcat 명령줄 도구 — https://developer.android.com/tools/logcat
- Neural Networks API (NNAPI) 가이드, deprecated 경고문 (확인일 2026-10-04) — https://developer.android.com/ndk/guides/neuralnetworks
- NNAPI Migration Guide (Android 15 deprecated, 대안) (확인일 2026-10-04) — https://developer.android.com/ndk/guides/neuralnetworks/migration-guide
- Support 16 KB page sizes (NDK r28 기본 정렬, 확인 방법, Play 일정) (확인일 2026-10-04) — https://developer.android.com/guide/practices/page-sizes
- Simpleperf — https://developer.android.com/ndk/guides/simpleperf
- AOSP 빌드 시스템 (Soong, Android.bp) (확인일 2026-10-04) — https://source.android.com/docs/setup/build
- VNDK (Android 15부터 deprecated) (확인일 2026-10-04) — https://source.android.com/docs/core/architecture/vndk
- ION에서 DMA-BUF heaps로의 전환 (확인일 2026-10-04) — https://source.android.com/docs/core/architecture/kernel/dma-buf-heaps
- Google AI Edge — Qualcomm NPU (AI Engine Direct) with LiteRT (확인일 2026-10-04) — https://developers.google.com/edge/litert/next/qualcomm
- Google AI Edge — NPU acceleration with LiteRT — https://developers.google.com/edge/litert/next/npu
- ONNX Runtime QNN Execution Provider (확인일 2026-10-04) — https://onnxruntime.ai/docs/execution-providers/QNN-ExecutionProvider.html
- Linux kernel — Compute Accelerators (accel) 소개 (확인일 2026-10-04) — https://docs.kernel.org/accel/introduction.html
- Linux kernel — accel/qaic (Qualcomm Cloud AI) — https://docs.kernel.org/accel/qaic/qaic.html
- Linux kernel — accel/rocket (Rockchip NPU) — https://docs.kernel.org/accel/rocket/index.html
- Linux kernel — Buffer Sharing and Synchronization (dma-buf) (확인일 2026-10-04) — https://docs.kernel.org/driver-api/dma-buf.html
- Linux kernel uAPI 헤더 `include/uapi/misc/fastrpc.h`, `include/uapi/linux/dma-buf.h` — https://github.com/torvalds/linux/tree/master/include/uapi
- Qualcomm FastRPC DT binding 문서 — https://www.kernel.org/doc/Documentation/devicetree/bindings/misc/qcom,fastrpc.txt
- Intel Linux NPU driver (ivpu 사용자 공간) — https://github.com/intel/linux-npu-driver
- Perfetto 문서 — https://perfetto.dev/docs/
- 관련 노트: F1, F4, F5, F7, F8, E4, E7, E8, J1, K2, K4, 형제 노트 O1·O3
