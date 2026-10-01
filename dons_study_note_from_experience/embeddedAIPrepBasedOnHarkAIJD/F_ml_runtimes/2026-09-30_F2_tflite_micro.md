# F2. TensorFlow Lite Micro — MCU에서 모델 돌리기: arena, op resolver, CMSIS-NN

> **이 노트를 다 읽으면**: `.tflite` 파일을 정렬된 `const` C 배열로 만들어 TFLM 애플리케이션(`GetModel` → `MicroMutableOpResolver` → `MicroInterpreter` → `AllocateTensors` → `Invoke`)을 한 줄씩 설명하며 짤 수 있다 · tensor arena 안에 무엇이 들어가는지(head = 활성값 계획, tail = persistent 객체) 알고 크기를 손계산 → `arena_used_bytes()`로 확정할 수 있다 · op resolver와 `OPTIMIZED_KERNEL_DIR=cmsis_nn` / `CO_PROCESSOR=ethos_u`가 빌드에서 커널을 어떻게 바꿔 끼우는지 소스 수준으로 설명할 수 있다 · "Python에선 되는데 보드에선 안 된다"를 체크리스트로 좁힐 수 있다
> **JD 연결**: "Integrate ML inference into embedded firmware in C, C++, or Rust", "Embedded ML runtimes (TFLite, llama.cpp, QNN)", "Optimizing models for MCUs & edge processors (Cortex-M/A, RISC-V, DSP)" — study_prep_list **F2**: **tensor arena** (malloc 없음), op resolver / **CMSIS-NN** 커널, Ethos-U 연동, Vela 컴파일러 / 모델을 C 배열로 flash에 넣기. MCU 배포 표준(P0). 프로젝트 PJ1·PJ2의 런타임.
> **Don 기준 난이도**: 정적 메모리 풀, 링커 섹션, `const` 데이터의 flash 배치, 사이클 카운터 프로파일링, vendor SDK 포팅은 이미 손에 익은 것 / TFLM의 API 이름과 객체 수명, arena 내부 구조, 커널 선택 빌드 메커니즘, 변환기(F1)와 런타임 사이의 버전·op 호환성은 새로 배움
> **선행 노트**: C1 (int8 scale·zero-point, requantization), D2 (활성값 liveness, peak, arena 추정기, flash/SRAM 배치), E2 (Cortex-M DSP 확장·Helium, CMSIS-NN이 하는 일), E5 (NPU, Vela 컴파일러), F1 (TFLite 변환기·interpreter — 이 노트는 `.tflite` 파일이 이미 있다고 보고 시작한다)

---

## 0. 큰 그림 — 이게 왜 필요한가

F1에서 만든 `.tflite` 파일은 PC나 Android에서는 `tf.lite.Interpreter`(또는 LiteRT)로 돌린다. 그런데 Hark 같은 웨어러블(추정)의 always-on MCU — Cortex-M4F/M33/M55급, SRAM 수백 KB, flash 1~2 MB, OS는 없거나 작은 RTOS — 에는 그 런타임이 들어가지 않는다. 파일 시스템도 없고, `malloc`을 추론 경로에서 쓰면 단편화와 비결정성 때문에 양산 펌웨어 리뷰를 통과하지 못한다.

**TensorFlow Lite Micro (TFLM)** 는 이 문제를 위해 만든 "MCU용 TFLite 인터프리터"다. 같은 `.tflite` flatbuffer를 읽지만, 모든 메모리를 앱이 넘겨준 배열 하나(**tensor arena**) 안에서 해결하고, OS·파일·동적 할당 없이 돌아간다. 펌웨어 엔지니어 눈으로 보면 이것은 **"정적 메모리 풀 위에서 도는, 테이블 구동(table-driven) 드라이버 프레임워크"** 다. 모델 파일은 flash에 들어간 설정 테이블이고, 커널은 드라이버 함수들이며, op resolver는 함수 포인터 테이블이다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="f2a1" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="200" height="220" rx="6" fill="none" stroke="currentColor"/>
  <text x="120" y="22" text-anchor="middle" font-size="13">Flash (읽기 전용, XIP)</text>
  <rect x="35" y="50" width="170" height="60" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
  <text x="120" y="75" text-anchor="middle" font-size="12">g_kws_model[] (.tflite)</text>
  <text x="120" y="93" text-anchor="middle" font-size="12">6,736 B flatbuffer</text>
  <rect x="35" y="125" width="170" height="50" rx="4" fill="#888" fill-opacity="0.25" stroke="#888"/>
  <text x="120" y="147" text-anchor="middle" font-size="12">커널 코드 (.text)</text>
  <text x="120" y="164" text-anchor="middle" font-size="12">등록한 op만 링크됨</text>
  <rect x="35" y="190" width="170" height="45" rx="4" fill="#888" fill-opacity="0.15" stroke="#888"/>
  <text x="120" y="217" text-anchor="middle" font-size="12">TFLM 코어 + 앱 코드</text>
  <rect x="250" y="105" width="150" height="80" rx="6" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
  <text x="325" y="135" text-anchor="middle" font-size="13">MicroInterpreter</text>
  <text x="325" y="155" text-anchor="middle" font-size="12">flatbuffer를 읽고</text>
  <text x="325" y="172" text-anchor="middle" font-size="12">op를 순서대로 실행</text>
  <rect x="430" y="30" width="210" height="220" rx="6" fill="none" stroke="currentColor"/>
  <text x="535" y="22" text-anchor="middle" font-size="13">SRAM (읽기/쓰기)</text>
  <rect x="445" y="50" width="180" height="95" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
  <text x="535" y="75" text-anchor="middle" font-size="12">tensor_arena[] (.bss)</text>
  <text x="535" y="95" text-anchor="middle" font-size="12">head: 활성값 (계획됨)</text>
  <text x="535" y="113" text-anchor="middle" font-size="12">tail: persistent 객체</text>
  <text x="535" y="131" text-anchor="middle" font-size="12">8,464 B 사용 (이 노트 모델)</text>
  <rect x="445" y="160" width="180" height="35" rx="4" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
  <text x="535" y="182" text-anchor="middle" font-size="12">resolver·interpreter 객체</text>
  <rect x="445" y="205" width="180" height="30" rx="4" fill="#888" fill-opacity="0.15" stroke="#888"/>
  <text x="535" y="225" text-anchor="middle" font-size="12">stack</text>
  <line x1="205" y1="80" x2="250" y2="125" stroke="currentColor" marker-end="url(#f2a1)"/>
  <text x="228" y="98" text-anchor="middle" font-size="12">GetModel</text>
  <line x1="205" y1="150" x2="250" y2="150" stroke="currentColor" marker-end="url(#f2a1)"/>
  <line x1="400" y1="130" x2="445" y2="100" stroke="currentColor" marker-end="url(#f2a1)"/>
  <line x1="445" y1="177" x2="400" y2="165" stroke="currentColor" marker-end="url(#f2a1)"/>
  <text x="330" y="280" text-anchor="middle" font-size="12">앱 루프: 센서 → input(0) 채우기 → Invoke() → output(0) 읽기 → 판단</text>
</svg>
```

그림 1 — TFLM 배포의 메모리 지도. 모델(flatbuffer)과 커널 코드는 flash에 그대로 두고 읽기만 한다. 실행 중 쓰기가 필요한 모든 것은 앱이 준 정적 배열 하나(tensor arena)에 들어간다. 숫자는 이 노트 5절의 실제 측정값이다.

| | TFLite / LiteRT (F1) | TFLite Micro (이 노트) |
|---|---|---|
| 대상 | Android·Linux·macOS (MB~GB RAM) | MCU, DSP, bare-metal (KB RAM) |
| 모델 로딩 | 파일 경로 또는 mmap | 링크된 `const` 배열을 포인터로 바로 읽음 |
| 메모리 | 내부에서 `malloc`/`new`, 텐서 resize 가능 | 앱이 준 arena 하나, 런타임 중 할당 없음 |
| op 등록 | 기본 `BuiltinOpResolver`가 거의 모든 op 포함 | 앱이 필요한 op만 `MicroMutableOpResolver`로 등록 |
| 가속 | delegate (XNNPACK, GPU, NNAPI, QNN) | 빌드 시 커널 교체 (CMSIS-NN, Xtensa, Ethos-U 등) |
| 동적 shape | 지원(resize_tensor_input) | 사실상 고정 shape 전제 |
| 의존성 | C++ 표준 라이브러리, 스레드, 파일 I/O | C++17 컴파일러 + 최소한의 libc 함수 |

이 노트의 흐름은 실제 배포 순서와 같다.

```
 F1 결과물              3절                 4절                 5절            6~7절              8~9절
 kws_int8.tflite ──► C 배열 (flash) ──► 앱 골격 작성 ──► arena 크기 확정 ──► op·커널 선택 ──► 프로파일 → 보드 포팅
                     xxd -i, alignas    GetModel..Invoke    arena_used_bytes   CMSIS-NN/Ethos-U   DWT, DebugLog, 링커
```

이 노트의 모든 실행 결과는 다음 환경에서 **실제로** 얻었다: TFLM `main` 브랜치 commit `06f0b46` (2026-09-30 clone), TensorFlow 2.20.0, macOS 26.4 arm64, Apple clang 21. ARM 크로스 컴파일러와 보드·qemu는 없어서 **호스트(Mac) 빌드로만** 돌렸다. 그래서 정확도·메모리 구조·커널 선택은 그대로 검증되지만, 속도 숫자는 MCU에서의 속도가 아니다(7.4절에서 다시 강조한다).

---

## 1. TFLM의 설계 목표와 저장소 지도

### 1.1 네 가지 설계 목표

1. **동적 할당 없음.** 모든 텐서·커널 상태·런타임 객체를 앱이 넘긴 `uint8_t` 배열 하나에서 잘라 쓴다. 공식 문서 `tensorflow/lite/micro/docs/porting_reference_ops.md`는 커널 작성자에게 "Can I use malloc/free or new/delete in my operator code? — No."라고 못 박고, `new`는 placement new로만 쓴다고 적어 둔다.
2. **OS·표준 라이브러리 의존 최소.** 스레드, 파일, 예외(`-fno-exceptions`), RTTI(`-fno-rtti`) 없이 빌드된다. 플랫폼이 제공해야 하는 것은 로그 출력(`DebugLog`), 타이머(`GetCurrentTimeTicks`), 초기화(`InitializeTarget`) 정도다(9.1절).
3. **flash 안의 flatbuffer를 그대로 해석.** 모델을 RAM으로 복사하거나 파싱해서 새 자료구조를 만들지 않는다. `tflite::GetModel()`은 포인터 캐스팅 한 번이다(3.3절).
4. **타깃별 커널 교체.** 같은 앱 코드에 reference 커널 대신 CMSIS-NN(Cortex-M), Xtensa(HiFi), Ethos-U(microNPU) 커널을 **빌드 시점에** 끼운다. 런타임 delegate가 아니라 "링크하는 `.cc` 파일을 바꾸는" 방식이다(7절).

Don의 경험과 붙이면: SSD 펌웨어에서 "부팅 때 정적 풀을 나눠 주고, 런타임에는 할당하지 않는다", "HW 변형별로 HAL 파일만 바꿔 빌드한다"와 똑같은 철학이다.

### 1.2 F1(TFLite)과 갈라지는 지점

F1의 `tf.lite.Interpreter`와 TFLM은 같은 flatbuffer 스키마(`TFL3`)와 같은 int8 양자화 수식(C1 7절)을 쓴다. 갈라지는 지점은 세 가지다.

- **op 지원 범위**: TFLite가 지원하는 op 중 일부만 TFLM에 있다. 변환은 성공했는데 TFLM에서 "Didn't find op"가 나는 이유다(6.4절).
- **커널 구현**: 같은 int8 모델이라도 커널이 다르면 출력이 1 LSB씩 다를 수 있다. 3.2절에서 실제로 본다 — Python 기본 경로(XNNPACK)와 TFLite reference 커널이 이미 다르다.
- **메모리 모델**: TFLite는 텐서 resize와 동적 할당을 허용하고, TFLM은 `AllocateTensors()` 한 번에 모든 배치를 끝낸다.

### 1.3 저장소 지도 (실제 clone 기준)

```
tflite-micro/
├─ tensorflow/lite/micro/
│  ├─ micro_interpreter.h/.cc         MicroInterpreter (AllocateTensors, Invoke, input, output, arena_used_bytes)
│  ├─ micro_mutable_op_resolver.h     MicroMutableOpResolver<N> 와 AddConv2D() 등 Add* 메서드
│  ├─ micro_allocator.h/.cc           arena 분할, StartModelAllocation / FinishModelAllocation
│  ├─ memory_planner/                 greedy_memory_planner, linear_memory_planner
│  ├─ arena_allocator/                single_arena_buffer_allocator (head/tail 관리)
│  ├─ recording_micro_interpreter.h   할당 내역을 기록·출력하는 디버그용 interpreter
│  ├─ micro_profiler.h/.cc            MicroProfiler (op별 tick)
│  ├─ micro_log.h, debug_log.h/.cc    MicroPrintf → DebugLog (플랫폼 훅)
│  ├─ micro_time.h/.cc                ticks_per_second, GetCurrentTimeTicks (플랫폼 훅)
│  ├─ system_setup.h/.cc              InitializeTarget (플랫폼 훅)
│  ├─ kernels/                        reference 커널 (conv.cc, softmax.cc, ...)
│  │  ├─ cmsis_nn/                    Cortex-M 최적화 커널 (같은 파일 이름으로 덮어씀)
│  │  ├─ ethos_u/                     Ethos-U custom op 커널
│  │  └─ xtensa/, arc_mli/, ceva/ ... 그 밖의 타깃
│  ├─ cortex_m_generic/               Cortex-M용 debug_log.cc, micro_time.cc (DWT 사이클 카운터)
│  ├─ examples/                       hello_world, micro_speech, person_detection, ...
│  ├─ docs/                           memory_management.md, new_platform_support.md, profiling.md ...
│  └─ tools/make/Makefile             make 기반 빌드 (targets/, ext_libs/ 포함)
└─ tensorflow/lite/schema/schema_generated.h   flatbuffer 스키마 (GetModel 정의)
```

### 1.4 함정

- "TFLite Micro = TFLite의 축소판이라 모든 op가 된다"는 착각. 지원 op 목록은 `micro_mutable_op_resolver.h`의 `Add*` 메서드 목록이 사실상의 답이다(6.1절).
- 예전 블로그·책(TinyML, 2019)의 코드에는 `AllOpsResolver`, `MicroErrorReporter`, `tensorflow/lite/micro/all_ops_resolver.h`가 나온다. 이 commit에서 `AllOpsResolver`는 소스에 **없다**(6.2절). 오래된 예제를 그대로 붙이면 컴파일이 안 된다.

---

## 2. 실습 준비 — 호스트에서 TFLM 빌드하기

### 2.1 macOS에서 부딪힌 세 가지 문제 (정확한 에러)

TFLM은 Bazel과 make 두 빌드를 지원한다. 여기서는 make를 썼다. 처음 명령은 이것이다.

```sh
cd .tools/tflite-micro
make -f tensorflow/lite/micro/tools/make/Makefile test_hello_world_test
```

```text
tensorflow/lite/micro/tools/make/Makefile:21: *** "Requires make version 3.82 or later (current is 3.81)".  Stop.
```

macOS 기본 `/usr/bin/make`는 GNU make 3.81이다. 시스템을 건드리지 않으려고 GNU make 4.4.1을 스크래치 폴더에 소스로 빌드했다(1분 이내).

```sh
cd /private/tmp/claude-501/f2
curl -sSLO https://ftp.gnu.org/gnu/make/make-4.4.1.tar.gz && tar xzf make-4.4.1.tar.gz
cd make-4.4.1 && ./configure --disable-nls && sh build.sh     # ./make 생성
```

두 번째 문제: Makefile이 예제 데이터(이미지·오디오)를 C 배열로 바꾸는 `generate_cc_arrays.py`를 `python3`으로 부르는데, 시스템 python3에 PIL이 없었다.

```text
ModuleNotFoundError: No module named 'PIL'
```

PIL·numpy가 있는 `.venv-tf/bin`을 `PATH` 앞에 두어 해결했다. 세 번째 문제는 CMSIS-NN 빌드에서 났다. 다운로드 스크립트가 `wget`을 쓴다.

```text
tensorflow/lite/micro/tools/make/ext_libs/cmsis_download.sh: line 63: wget: command not found
```

`wget URL -O FILE`을 `curl -sSL URL -o FILE`로 바꿔 주는 3줄짜리 shim 스크립트를 스크래치 폴더에 두고 `PATH`에 넣었다. 이 세 가지만 고치면 macOS arm64에서 빌드가 그대로 된다. Makefile이 처음 실행 때 flatbuffers, gemmlowp, ruy, kissfft, eyalroz_printf(그리고 CMSIS-NN 빌드 땐 CMSIS, CMSIS-NN)를 `tensorflow/lite/micro/tools/make/downloads/`에 내려받는다.

### 2.2 예제 1 — 공식 hello_world 테스트 빌드·실행

무엇을 확인하나: 이 Mac에서 TFLM 라이브러리(`libtensorflow-microlite.a`)와 공식 예제가 실제로 빌드되고 테스트를 통과하는지.

```sh
export PATH=/private/tmp/claude-501/f2/shim:$PWD/../../.venv-tf/bin:$PATH
GM=/private/tmp/claude-501/f2/make-4.4.1/make
$GM -f tensorflow/lite/micro/tools/make/Makefile -j8 test_hello_world_test   # 84초 (처음, 다운로드 포함)
gen/osx_arm64_default_gcc/bin/hello_world_test
```

```text
"Unique Tag","Total ticks across all events with that tag."
FULLY_CONNECTED, 11
"total number of ticks", 11

[RecordingMicroAllocator] Arena allocation total 2392 bytes
[RecordingMicroAllocator] Arena allocation head 128 bytes
[RecordingMicroAllocator] Arena allocation tail 2264 bytes
[RecordingMicroAllocator] 'TfLiteEvalTensor data' used 240 bytes with alignment overhead (requested 240 bytes for 10 allocations)
[RecordingMicroAllocator] 'Persistent TfLiteTensor data' used 128 bytes with alignment overhead (requested 128 bytes for 2 tensors)
[RecordingMicroAllocator] 'Persistent buffer data' used 1200 bytes with alignment overhead (requested 1184 bytes for 7 allocations)
[RecordingMicroAllocator] 'NodeAndRegistration struct' used 192 bytes with alignment overhead (requested 192 bytes for 3 NodeAndRegistration structs)
~~~ALL TESTS PASSED~~~
```

출력에서 볼 것: hello_world는 sin(x)를 근사하는 3층 MLP다. 활성값이 작아서 head는 128 B뿐이고, arena 대부분(2264 B)이 **tail(런타임 객체)** 이다 — 아주 작은 모델에선 "활성값 크기 = arena"라는 공식이 틀린다. 빌드 결과물 경로 `gen/osx_arm64_default_gcc/`는 `<TARGET>_<TARGET_ARCH>_<BUILD_TYPE>` 규칙으로 정해진다(Makefile 312행).

### 2.3 hello_world_test.cc가 보여 주는 것

`tensorflow/lite/micro/examples/hello_world/hello_world_test.cc`는 TFLM 사용법의 공식 교과서다. 핵심만 추리면 다음과 같다(원문 발췌).

```cpp
using HelloWorldOpResolver = tflite::MicroMutableOpResolver<1>;
TF_LITE_ENSURE_STATUS(op_resolver.AddFullyConnected());

constexpr int kTensorArenaSize = 3000;
uint8_t tensor_arena[kTensorArenaSize];
tflite::MicroInterpreter interpreter(model, op_resolver, tensor_arena, kTensorArenaSize);
TF_LITE_ENSURE_STATUS(interpreter.AllocateTensors());

input->data.int8[0] = golden_inputs_int8[i];
TF_LITE_ENSURE_STATUS(interpreter.Invoke());
float y_pred = (output->data.int8[0] - output_zero_point) * output_scale;
```

주석에 "Arena size just a round number. The exact arena usage can be determined using the RecordingMicroInterpreter."라고 적혀 있다. 즉 공식 예제도 arena 크기를 "넉넉히 잡고 → 측정"한다. 이 패턴을 5절에서 우리 모델로 그대로 해 본다.

---

## 3. 모델 준비 — `.tflite`에서 C 배열까지

### 3.1 예제 2 — 실습용 int8 모델 만들기

hello_world는 FULLY_CONNECTED 하나뿐이라 conv 커널·CMSIS-NN·arena 계획을 보기에 너무 작다. 그래서 키워드 스포팅(KWS) 모양의 **미니 DS-CNN**을 만들었다: 입력 49 프레임 × 10 MFCC (B5, PJ2와 같은 모양), 클래스 4개. 데이터는 "클래스 k면 MFCC 계수 2k, 2k+1에 에너지가 있다"는 합성 데이터다. 목적은 정확도가 아니라 런타임 검증이다.

무엇을 확인하나: Keras 모델 → full-int8 `.tflite` 변환 (F1에서 배운 흐름 그대로, 입력·출력도 int8).

```python
# make_model.py (발췌: 데이터 생성 함수 make_data 는 위 설명대로)
L = tf.keras.layers
inp = tf.keras.Input(shape=(49, 10, 1))
x = L.Conv2D(16, (5, 3), strides=2, padding="same", activation="relu")(inp)   # → 25×5×16
x = L.DepthwiseConv2D(3, padding="same", activation="relu")(x)               # → 25×5×16
x = L.Conv2D(32, 1, activation="relu")(x)                                    # → 25×5×32
x = L.AveragePooling2D(pool_size=(25, 5))(x)                                 # → 1×1×32
out = L.Softmax()(L.Dense(4)(L.Flatten()(x)))
model = tf.keras.Model(inp, out)
model.compile("adam", "sparse_categorical_crossentropy", metrics=["accuracy"])
h = model.fit(xtr, ytr, epochs=15, batch_size=64, verbose=0)
print("params:", model.count_params(), " train acc: %.3f" % h.history["accuracy"][-1])
conv = tf.lite.TFLiteConverter.from_keras_model(model)
conv.optimizations = [tf.lite.Optimize.DEFAULT]
conv.representative_dataset = rep            # 학습 데이터 200개로 calibration
conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
conv.inference_input_type = tf.int8
conv.inference_output_type = tf.int8
tfl = conv.convert()
open("kws_int8.tflite", "wb").write(tfl); print("tflite bytes:", len(tfl))
```

```text
params: 1092  train acc: 0.998
tflite bytes: 6736
```

출력에서 볼 것: 파라미터 1,092개(int8로 약 1.3 KB)인데 파일은 6,736 B다. 나머지는 flatbuffer 메타데이터 — 텐서 이름 문자열, per-channel scale/zero-point 배열, op 코드 표, signature — 다. **작은 모델일수록 파일 크기는 가중치가 아니라 메타데이터가 지배한다.** flash 예산(D2 8절)을 잡을 때 "params × 1 B"만 보면 안 되는 이유다.

손으로 검증: 가중치 + bias 바이트는

```
conv1   5×3×1×16 = 240 B  + bias 16×4 = 64 B
dw      3×3×16   = 144 B  + bias 16×4 = 64 B
conv1x1 1×1×16×32= 512 B  + bias 32×4 = 128 B
dense   32×4     = 128 B  + bias 4×4  = 16 B
합계 = 1024 + 272 = 1296 B   (+ RESHAPE의 shape 상수 2×int32 = 8 B → 1304 B, 5.3절 출력과 일치)
```

말로 하면: weight는 int8(1 B), bias는 int32(4 B)다 — C1 7.2절에서 본 대로 bias에 zero-point 보정항을 접어 넣기 때문에 int32다.

### 3.2 예제 3 — golden 출력: "Python 기본 경로"와 "reference 커널"은 다르다

무엇을 확인하나: 보드에서 비교할 golden 출력을 만든다. 이때 `tf.lite.Interpreter`의 기본 경로(AUTO)와 reference 커널(BUILTIN_REF)을 둘 다 돌려 본다.

```python
# ref.py
R = tf.lite.experimental.OpResolverType
def run(resolver):
    it = tf.lite.Interpreter("kws_int8.tflite", experimental_op_resolver_type=resolver)
    it.allocate_tensors()
    i, o = it.get_input_details()[0], it.get_output_details()[0]
    s, z = i["quantization"]
    xq = np.clip(np.round(np.load("test_x.npy") / s) + z, -128, 127).astype(np.int8)
    ys = []
    for x in xq:
        it.set_tensor(i["index"], x[None]); it.invoke()
        ys.append(it.get_tensor(o["index"])[0].tolist())
    return xq, ys, [op["op_name"] for op in it._get_ops_details()]
xq, y_def, ops_def = run(R.AUTO)
_,  y_ref, ops_ref = run(R.BUILTIN_REF)
print("labels       :", np.random.default_rng(7).integers(0, 4, 3).tolist())
print("AUTO ops     :", ops_def)
print("AUTO (XNNPACK):", y_def)
print("BUILTIN_REF  :", y_ref)
# (이어서 xq 와 y_ref 를 golden.h 로 저장)
```

```text
labels       : [3, 2, 2]
AUTO ops     : ['CONV_2D', 'DEPTHWISE_CONV_2D', 'CONV_2D', 'AVERAGE_POOL_2D', 'RESHAPE', 'FULLY_CONNECTED', 'SOFTMAX', 'DELEGATE', 'DELEGATE']
AUTO (XNNPACK): [[-128, -128, -60, 60], [-128, -128, 36, -36], [-128, -128, 45, -45]]
BUILTIN_REF  : [[-128, -128, -60, 60], [-128, -128, 36, -36], [-128, -128, 40, -41]]
```

출력에서 볼 것: op 목록 끝의 `DELEGATE` 두 개는 Python 기본 interpreter가 **XNNPACK delegate를 자동으로 붙였다**는 표시다. 그리고 세 번째 샘플에서 AUTO는 `45`, reference는 `40`이다. 같은 `.tflite`, 같은 int8 입력인데 커널 구현이 다르면 출력이 다르다. (`_get_ops_details()`는 밑줄로 시작하는 비공개 API라 확인용으로만 쓴다.)

어디서 갈렸는지 `experimental_preserve_all_tensors=True`로 중간 텐서를 꺼내 op별로 비교해 봤다(같은 세 번째 샘플, AUTO 쪽은 이 모드에서도 DELEGATE 노드가 붙은 XNNPACK 경로).

```text
CONV_2D            max|diff|=1  n_diff=1/2000
DEPTHWISE_CONV_2D  max|diff|=1  n_diff=2/2000
CONV_2D            max|diff|=1  n_diff=4/4000
AVERAGE_POOL_2D    max|diff|=1  n_diff=2/32
RESHAPE            max|diff|=1  n_diff=2/32
FULLY_CONNECTED    max|diff|=1  n_diff=2/4
SOFTMAX            max|diff|=5  n_diff=2/4
```

말로 하면: 첫 conv부터 2000개 중 1개 원소가 1 LSB 다르고(반올림 구현 차이 — C1 7.4절의 "반올림 두 번"이 라이브러리마다 다를 수 있다), 그 차이가 층을 따라 번져서 softmax 출력에서 5 LSB(확률로 약 0.02)가 됐다. 교훈은 **"golden은 기기 런타임과 같은 커널 계열로 만든다"** 다. TFLM의 reference 커널은 TFLite reference 커널에서 온 것이므로, bit-exact 비교의 기준은 `BUILTIN_REF`가 맞다(4.4절에서 실제로 0 mismatch 확인). XNNPACK 결과와 비교하면 "보드가 틀렸다"고 오진한다. 이 주제는 C8(검증), J6(golden vector)과 직결된다.

### 3.3 예제 4 — flatbuffer 헤더를 바이트로 읽기

무엇을 확인하나: `GetModel()`이 "파싱 없이 포인터만 캐스팅"한다는 말이 무슨 뜻인지, 파일의 첫 바이트로 확인한다.

```python
# hdr.py — .tflite 앞 8바이트 = root offset(uint32 LE) + file identifier "TFL3"
import struct
b = open("kws_int8.tflite", "rb").read()
root, ident = struct.unpack_from("<I4s", b, 0)
print("size =", len(b), "B | root table offset =", root, "| identifier =", ident)
vt_off = root - struct.unpack_from("<i", b, root)[0]     # root table → vtable
vt_len, tbl_len = struct.unpack_from("<HH", b, vt_off)
print("vtable at", vt_off, "| vtable bytes =", vt_len, "| Model table bytes =", tbl_len)
version = struct.unpack_from("<I", b, root + struct.unpack_from("<H", b, vt_off + 4)[0])[0]
print("Model.version (field 0) =", version)
```

```text
size = 6736 B | root table offset = 28 | identifier = b'TFL3'
vtable at 8 | vtable bytes = 20 | Model table bytes = 32
Model.version (field 0) = 3
```

출력에서 볼 것: 파일 맨 앞 4바이트는 "Model 테이블이 offset 28에 있다", 다음 4바이트는 식별자 `TFL3`이다. Model 테이블의 첫 필드가 스키마 버전 `3`이다. TFLM 소스에서 이 값은 `TFLITE_SCHEMA_VERSION`(= `(3)`, `micro_interpreter.h` 42행)과 비교된다.

`GetModel`의 실제 정의(`tensorflow/lite/schema/schema_generated.h` 25754행)는 이렇다.

```cpp
inline const tflite::Model *GetModel(const void *buf) {
  return ::flatbuffers::GetRoot<tflite::Model>(buf);
}
```

`GetRoot`는 "buf + (buf의 첫 uint32)" 위치를 `Model*`로 보는 것뿐이다. 그 뒤 `model->subgraphs()`, `model->buffers()` 같은 접근도 전부 offset을 따라가는 포인터 연산이다. 펌웨어 비유로는 **"flash에 구워 둔 디스크립터 테이블을 구조체 포인터로 overlay 해서 읽는 것"** 이다. 그래서 복사가 없고, 그래서 정렬이 중요하다(다음 절).

### 3.4 `xxd -i`로 C 배열 만들기 — `const`, 정렬, 링키지

```sh
xxd -i kws_int8.tflite > kws_model_raw.h
head -2 kws_model_raw.h; tail -1 kws_model_raw.h
```

```text
unsigned char kws_int8_tflite[] = {
  0x1c, 0x00, 0x00, 0x00, 0x54, 0x46, 0x4c, 0x33, 0x14, 0x00, 0x20, 0x00,
unsigned int kws_int8_tflite_len = 6736;
```

`0x1c 0x00 0x00 0x00` = 28 (root offset), `0x54 0x46 0x4c 0x33` = "TFL3". 그대로 쓰면 세 가지가 틀린다.

1. **`const`가 없다** → 배열이 `.data`에 들어가서 부팅 때 flash에서 RAM으로 **복사된다**. 6.7 KB짜리 모델이면 SRAM 6.7 KB를 그냥 버린다(100 KB 모델이면 치명적). `const`를 붙여야 `.rodata`(flash)에 남는다.
2. **정렬 지정이 없다** → `unsigned char` 배열의 정렬은 1바이트다. flatbuffer 안의 int32·float 필드를 포인터로 바로 읽으므로, 배열 시작이 4/8/16바이트 경계가 아니면 Cortex-M0/M0+ 같은 코어에선 unaligned access fault(HardFault)가 날 수 있고, 다른 코어에서도 느려진다. TFLM 예제들은 16바이트 정렬을 쓴다. `micro_arena_constants.h`의 `MicroArenaBufferAlignment()`도 16을 반환한다.
3. **C++에서 namespace 범위 `const` 변수는 internal linkage**다. `.cc` 파일에 `const unsigned char g_model[] = {...}`라고만 쓰면 다른 파일에서 `extern`으로 못 찾는다(링크 에러 "undefined symbol"). 헤더에 `extern` 선언을 두고 그 헤더를 정의 파일에서도 include 해야 한다.

실제로 만든 파일은 이렇다.

```cpp
// kws_model_data.h
#pragma once
extern const unsigned char g_kws_model[];
extern const unsigned int g_kws_model_len;

// kws_model_data.cc  (xxd 출력의 본문을 그대로 붙임)
#include "kws_model_data.h"
alignas(16) const unsigned char g_kws_model[] = {
  0x1c, 0x00, 0x00, 0x00, 0x54, 0x46, 0x4c, 0x33, 0x14, 0x00, 0x20, 0x00,
  /* ... 6736 바이트 ... */
};
const unsigned int g_kws_model_len = 6736;
```

링크 후 심볼 표에서 위치를 확인했다(호스트 Mach-O라 섹션 이름이 ELF와 다르다).

```text
$ nm kws_ref | grep -i "g_kws_model\|g_arena"
000000010003c020 b __ZL7g_arena
00000001000159c0 S _g_kws_model
```

`b`는 zero-fill(.bss에 해당), `S`는 `__const` 섹션 — ELF로 치면 `.rodata`다. 모델은 읽기 전용 영역, arena는 bss에 들어갔다. 실제 MCU에서는 링커 스크립트로 "모델은 외부 QSPI flash의 특정 섹션, arena는 DTCM"처럼 더 세밀하게 정한다(9.2절, F7). XIP로 읽을지 RAM으로 복사할지의 trade-off는 D2 1.3절에서 다뤘다.

### 3.5 함정

- 모델을 바꿨는데 C 배열을 다시 생성하지 않았다 → 보드에서는 옛 모델이 돈다. 빌드 시스템에서 `.tflite` → `.cc` 생성을 의존성 규칙으로 걸어 두고, 앱이 부팅 때 `g_kws_model_len`과 모델 해시(또는 metadata 버전 문자열)를 로그로 찍게 하면 현장에서 "어떤 모델이 들어갔나"를 즉시 안다(J5).
- `xxd`가 만든 변수 이름은 파일 경로에서 온다(`kws_int8_tflite`). 경로가 바뀌면 이름도 바뀐다. 위처럼 이름을 고정한 래퍼를 쓰는 게 안전하다.
- 큰 모델을 `{0x1c, 0x00, ...}` 텍스트로 넣으면 컴파일이 느려진다. GCC/Clang에서는 어셈블러 `.incbin`, C23 `#embed`, 또는 `objcopy -I binary`로 바이너리를 직접 섹션에 넣는 방법도 있다(F7).

---

## 4. 애플리케이션 골격 — 한 줄씩

### 4.1 수명 주기

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="f2a2" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="200" y="20" text-anchor="middle" font-size="13">부팅 때 한 번</text>
  <text x="560" y="20" text-anchor="middle" font-size="13">매 프레임 (루프)</text>
  <line x1="420" y1="10" x2="420" y2="220" stroke="#888" stroke-dasharray="4 4"/>
  <rect x="10" y="40" width="90" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/>
  <text x="55" y="62" text-anchor="middle" font-size="12">GetModel</text>
  <text x="55" y="79" text-anchor="middle" font-size="12">(포인터)</text>
  <rect x="110" y="40" width="90" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/>
  <text x="155" y="62" text-anchor="middle" font-size="12">resolver</text>
  <text x="155" y="79" text-anchor="middle" font-size="12">AddConv2D()...</text>
  <rect x="210" y="40" width="90" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/>
  <text x="255" y="62" text-anchor="middle" font-size="12">MicroInter-</text>
  <text x="255" y="79" text-anchor="middle" font-size="12">preter(ctor)</text>
  <rect x="310" y="40" width="100" height="50" rx="5" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
  <text x="360" y="62" text-anchor="middle" font-size="12">Allocate</text>
  <text x="360" y="79" text-anchor="middle" font-size="12">Tensors()</text>
  <line x1="100" y1="65" x2="110" y2="65" stroke="currentColor" marker-end="url(#f2a2)"/>
  <line x1="200" y1="65" x2="210" y2="65" stroke="currentColor" marker-end="url(#f2a2)"/>
  <line x1="300" y1="65" x2="310" y2="65" stroke="currentColor" marker-end="url(#f2a2)"/>
  <rect x="310" y="110" width="100" height="100" rx="5" fill="none" stroke="#e08a3c" stroke-dasharray="3 3"/>
  <text x="360" y="130" text-anchor="middle" font-size="12">1. op 등록 조회</text>
  <text x="360" y="150" text-anchor="middle" font-size="12">2. Init (OpData)</text>
  <text x="360" y="170" text-anchor="middle" font-size="12">3. Prepare</text>
  <text x="360" y="190" text-anchor="middle" font-size="12">4. 메모리 계획</text>
  <line x1="360" y1="90" x2="360" y2="110" stroke="#e08a3c"/>
  <rect x="440" y="40" width="100" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/>
  <text x="490" y="62" text-anchor="middle" font-size="12">input(0)</text>
  <text x="490" y="79" text-anchor="middle" font-size="12">채우기</text>
  <rect x="570" y="40" width="100" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/>
  <text x="620" y="69" text-anchor="middle" font-size="12">Invoke()</text>
  <rect x="505" y="140" width="110" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/>
  <text x="560" y="162" text-anchor="middle" font-size="12">output(0)</text>
  <text x="560" y="179" text-anchor="middle" font-size="12">읽고 판단</text>
  <line x1="410" y1="65" x2="440" y2="65" stroke="currentColor" marker-end="url(#f2a2)"/>
  <line x1="540" y1="65" x2="570" y2="65" stroke="currentColor" marker-end="url(#f2a2)"/>
  <line x1="620" y1="90" x2="590" y2="140" stroke="currentColor" marker-end="url(#f2a2)"/>
  <line x1="530" y1="140" x2="500" y2="90" stroke="currentColor" marker-end="url(#f2a2)"/>
</svg>
```

그림 2 — TFLM 애플리케이션 수명 주기. 왼쪽(파랑·주황)은 부팅 때 한 번, 오른쪽(초록)은 센서 프레임마다 반복한다. 실패할 수 있는 것은 거의 전부 `AllocateTensors()` 안에서 터지므로, 부팅 단계에서 걸러 낸다는 것이 펌웨어 관점의 큰 장점이다.

### 4.2 실제로 컴파일·실행한 앱 전체

무엇을 확인하나: 위 수명 주기를 실제 TFLM API로 쓴 79줄짜리 앱. 인자로 arena 크기를 바꾸고, `nosoftmax`(op 하나 빼기), `record`(할당 내역 출력) 모드를 고를 수 있게 해서 5~6절 실험에도 그대로 쓴다.

```cpp
// main.cc — TFLM 으로 kws_int8.tflite 를 돌리고 tf.lite (BUILTIN_REF) golden 과 비교
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_profiler.h"
#include "tensorflow/lite/micro/recording_micro_interpreter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "kws_model_data.h"
#include "golden.h"

constexpr size_t kArenaMax = 16 * 1024;
alignas(16) static uint8_t g_arena[kArenaMax];        // .bss — malloc 없음

int main(int argc, char** argv) {
  size_t arena_size = argc > 1 ? strtoul(argv[1], nullptr, 0) : kArenaMax;
  bool drop_softmax = argc > 2 && strcmp(argv[2], "nosoftmax") == 0;
  bool record = argc > 2 && strcmp(argv[2], "record") == 0;
  tflite::InitializeTarget();

  const tflite::Model* model = tflite::GetModel(g_kws_model);   // 복사·파싱 없음
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("schema %d != %d", model->version(), TFLITE_SCHEMA_VERSION);
    return 1;
  }
  static tflite::MicroMutableOpResolver<6> resolver;           // 6칸 고정 테이블
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddAveragePool2D();
  resolver.AddReshape();
  resolver.AddFullyConnected();
  if (!drop_softmax) resolver.AddSoftmax();

  static tflite::MicroProfiler profiler;
  tflite::MicroInterpreter* interp;
  if (record) {
    static tflite::RecordingMicroInterpreter r(model, resolver, g_arena, arena_size);
    interp = &r;
  } else {
    static tflite::MicroInterpreter m(model, resolver, g_arena, arena_size,
                                      nullptr, &profiler);
    interp = &m;
  }
  if (interp->AllocateTensors() != kTfLiteOk) {
    MicroPrintf("AllocateTensors() failed (arena=%u)", (unsigned)arena_size);
    return 1;
  }
  MicroPrintf("arena_used_bytes = %u / %u", (unsigned)interp->arena_used_bytes(),
              (unsigned)arena_size);
  if (record) {
    static_cast<tflite::RecordingMicroInterpreter*>(interp)
        ->GetMicroAllocator().PrintAllocations();
    return 0;
  }
  TfLiteTensor* in = interp->input(0);
  TfLiteTensor* out = interp->output(0);
  int mismatches = 0;
  for (int n = 0; n < 3; ++n) {
    memcpy(in->data.int8, g_inputs[n], in->bytes);
    profiler.ClearEvents();
    if (interp->Invoke() != kTfLiteOk) return 1;
    printf("sample %d: tflm=[", n);
    for (int k = 0; k < 4; ++k) {
      printf("%5d", out->data.int8[k]);
      mismatches += out->data.int8[k] != g_golden[n][k];
    }
    printf(" ]  golden=[%5d%5d%5d%5d ]\n", g_golden[n][0], g_golden[n][1],
           g_golden[n][2], g_golden[n][3]);
  }
  printf("mismatches vs BUILTIN_REF: %d\n", mismatches);
  fflush(stdout);
  profiler.ClearEvents();                             // 100회 누적 → 1회 평균은 /100
  for (int i = 0; i < 100; ++i) interp->Invoke();
  profiler.LogTicksPerTagCsv();                       // host: 1 tick = 1 us (clock())
  return mismatches != 0;
}
```

빌드 명령(TFLM Makefile이 hello_world에 쓰는 플래그를 `make -n`으로 뽑아 그대로 따랐다). 경고 0개로 컴파일됐다.

```sh
T=.tools/tflite-micro; D=$T/tensorflow/lite/micro/tools/make/downloads
g++ -std=c++17 -Os -fno-rtti -fno-exceptions -fno-threadsafe-statics \
  -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter \
  -DTF_LITE_STATIC_MEMORY -DTF_LITE_USE_CTIME \
  -I$T -I$D/flatbuffers/include -I$D/gemmlowp -I$D/ruy -I. \
  main.cc kws_model_data.cc $T/gen/osx_arm64_default_gcc/lib/libtensorflow-microlite.a \
  -Wl,-dead_strip -lm -o kws_ref
```

`-DTF_LITE_STATIC_MEMORY`는 TFLM 빌드 전체에 들어가는 정의로, TFLite 공용 헤더(`TfLiteTensor` 등)에서 동적 할당용 필드·함수를 빼서 구조체를 작게 만든다. 라이브러리와 앱이 이 정의를 **다르게** 쓰면 구조체 레이아웃이 어긋나 이상한 크래시가 난다 — 라이브러리 빌드 플래그를 앱에서 그대로 쓰는 이유다. `-DTF_LITE_USE_CTIME`은 호스트에서 `micro_time.cc`가 `clock()`을 쓰게 한다(8.1절).

### 4.3 한 줄씩 — 각 API가 하는 일과 소스 위치

| 코드 | 하는 일 | 소스 (이 commit) |
|---|---|---|
| `alignas(16) static uint8_t g_arena[...]` | arena. 전역 정적 배열 → `.bss`. 16바이트 정렬 | 정렬 상수: `micro_arena_constants.h` (`MicroArenaBufferAlignment()` = 16) |
| `tflite::InitializeTarget()` | 플랫폼 초기화 훅 (UART, 클럭 등). 기본 구현은 빈 함수 | `system_setup.cc` 23행 |
| `tflite::GetModel(g_kws_model)` | flatbuffer root를 `const Model*`로 캐스팅. 복사 없음 | `schema_generated.h` 25754행 |
| `model->version() != TFLITE_SCHEMA_VERSION` | 스키마 버전 확인 (3) | `micro_interpreter.h` 42행 |
| `MicroMutableOpResolver<6>` | op 6개짜리 고정 크기 등록 테이블 (템플릿 인자 = 칸 수) | `micro_mutable_op_resolver.h` |
| `resolver.AddConv2D()` | `AddBuiltin(BuiltinOperator_CONV_2D, Register_CONV_2D(), ParseConv2D)` | 같은 파일 209행 |
| `MicroInterpreter(model, resolver, arena, size, nullptr, &profiler)` | 생성자. 4·5번째 인자는 resource variable, profiler (기본 nullptr) | `micro_interpreter.h` 56행 |
| `AllocateTensors()` | op 조회 → Init → Prepare → 메모리 계획·배치 (4.5절) | `micro_interpreter.cc` 200행 |
| `arena_used_bytes()` | 실제 사용한 arena 바이트. AllocateTensors 후에만 의미 있음 | `micro_interpreter.h` 149행 |
| `input(0)`, `output(0)` | `TfLiteTensor*`. `data.int8`, `params.scale`, `params.zero_point`, `bytes` | `micro_interpreter.h` 89·107행 |
| `Invoke()` | 그래프의 op를 순서대로 실행 | `micro_interpreter.cc` |
| `MicroPrintf(...)` | printf 형식 로그 → 플랫폼의 `DebugLog()`로 감 | `micro_log.h`, `debug_log.cc` |
| `MicroProfiler` | op별 시작·끝 tick 기록, `LogTicksPerTagCsv()` 출력 | `micro_profiler.h` |
| `RecordingMicroInterpreter` | 할당 종류별 바이트를 기록하는 디버그용 interpreter | `recording_micro_interpreter.h` |

헤더 주석에서 꼭 기억할 문장 하나: `micro_interpreter.h`는 "The interpreter doesn't do any deallocation of any of the pointed-to objects, ownership remains with the caller"이며 model·resolver·arena는 interpreter보다 오래 살아야 한다고 적는다. 그래서 위 코드에서 resolver·interpreter를 `static`으로 뒀다. 함수 지역 변수로 resolver를 만들고 interpreter만 전역에 두면, 함수가 끝난 뒤 interpreter가 사라진 스택을 가리키는 dangling 참조가 된다 — 펌웨어에서 가장 찾기 어려운 종류의 버그다.

### 4.4 예제 5 — 실행 결과: TFLM vs tf.lite reference, bit-exact

```sh
./kws_ref
```

```text
arena_used_bytes = 8464 / 16384
sample 0: tflm=[ -128 -128  -60   60 ]  golden=[ -128 -128  -60   60 ]
sample 1: tflm=[ -128 -128   36  -36 ]  golden=[ -128 -128   36  -36 ]
sample 2: tflm=[ -128 -128   40  -41 ]  golden=[ -128 -128   40  -41 ]
mismatches vs BUILTIN_REF: 0
"Unique Tag","Total ticks across all events with that tag."
CONV_2D, 57109
DEPTHWISE_CONV_2D, 13404
AVERAGE_POOL_2D, 1212
RESHAPE, 69
FULLY_CONNECTED, 83
SOFTMAX, 121
"total number of ticks", 71998
```

출력에서 볼 것: TFLM(호스트, reference 커널)과 `tf.lite.Interpreter`(BUILTIN_REF)의 int8 출력 12개가 **전부 bit-exact**다. 반면 3.2절의 XNNPACK 결과(`45, -45`)와는 다르다. arena는 16 KB 중 8,464 B를 썼다. 아래 profiler 숫자는 8절에서 해석한다.

int8 출력을 확률로 바꾸는 것도 손으로 해 보자. 출력 텐서는 scale = 1/256 = 0.00390625, zero_point = −128이다(ref.py 첫 버전에서 출력한 값).

```
sample 0, class 3:  q = 60   →  (60 − (−128)) × 1/256 = 188/256 = 0.734
sample 0, class 2:  q = −60  →  (−60 + 128) / 256      =  68/256 = 0.266
합 = 1.000  (softmax 출력이므로)
```

말로 하면: 첫 샘플은 정답 레이블 3을 73% 확률로 맞혔다. 입력 쪽도 같다 — 앱이 float MFCC를 넣으려면 `q = round(x / 0.040238) + (−16)` 후 [−128, 127]로 clamp 해서 `input->data.int8`에 써야 한다. 이 scale·zero-point는 **하드코딩하지 말고** `input->params.scale`, `input->params.zero_point`에서 읽는다. 모델을 다시 calibration 하면 값이 바뀌기 때문이다(C1 2절).

### 4.5 `AllocateTensors()` 안에서 일어나는 일

`micro_interpreter.cc`의 `MicroInterpreter::AllocateTensors()`(200행~)를 순서대로 읽으면 이렇다.

1. `allocator_.StartModelAllocation(model_)` — flatbuffer의 텐서 표를 보고 `TfLiteEvalTensor` 배열 등 기본 구조를 arena **tail**에 만든다.
2. `PrepareNodeAndRegistrationDataFromFlatbuffer()` — 그래프의 op마다 resolver에서 커널을 찾는다. 여기서 못 찾으면 "Failed to get registration from op code ..."(131행)로 끝난다.
3. `graph_.InitSubgraphs()` — 각 커널의 `init()`. 커널 전용 상태(OpData)를 persistent 영역에 잡는다.
4. `graph_.PrepareSubgraphs()` — 각 커널의 `prepare()`. 출력 shape 확인, per-channel requantization multiplier·shift 계산(C1 7.3절의 M0·shift가 여기서 미리 계산된다), scratch 버퍼 요청(`RequestScratchBufferInArena`).
5. `allocator_.FinishModelAllocation(...)` — 모든 활성값 텐서와 scratch의 수명(lifetime)을 모아 **memory planner**(기본 `GreedyMemoryPlanner`)가 head 안의 offset을 정한다. D2 3.3절의 liveness 분석을 런타임이 부팅 때 직접 하는 셈이다.
6. 입력·출력 `TfLiteTensor` 포인터 배열을 persistent로 할당.

그래서 **`Invoke()`는 메모리를 전혀 할당하지 않는다.** 실패할 수 있는 일(op 누락, 타입 불일치, arena 부족)은 모두 부팅 때 `AllocateTensors()`에서 드러난다. Don 식으로 말하면 "bring-up 때 모든 자원 할당과 self-test를 끝내고, 메인 루프는 결정적으로 돈다"는 펌웨어 원칙을 런타임이 강제한다.

### 4.6 함정

- `AllocateTensors()`의 반환값을 안 보는 코드가 생각보다 많다. 실패한 interpreter로 `Invoke()`를 부르면 쓰레기 결과나 HardFault가 난다. 부팅 단계에서 실패하면 에러 코드를 남기고 안전 모드로 가야 한다.
- `input(0)`의 포인터를 `AllocateTensors()` **전에** 받아 두면 안 된다. 배치가 끝나야 `data` 포인터가 정해진다.
- 출력 포인터 `out->data.int8`은 다음 `Invoke()`에서 덮어써진다. 결과를 다른 태스크로 넘길 땐 복사한다. 더 나아가, planner는 텐서끼리 메모리를 겹쳐 쓰므로 **중간 텐서 포인터는 그 op 실행 직후에만 유효**하다.
- `memcpy(in->data.int8, src, in->bytes)` — 크기를 하드코딩하지 말고 `bytes`를 쓴다. shape가 바뀐 모델로 교체됐을 때 overflow를 막는다.

---

## 5. Tensor arena — 무엇이 들어가고, 크기는 어떻게 정하나

### 5.1 arena의 세 구역

`tensorflow/lite/micro/docs/memory_management.md`가 설명하는 구조는 다음과 같다.

- **Head**: 메모리 planner가 배치하는 **비영속(non-persistent)** 버퍼 — 활성값 텐서와 커널 scratch. 수명이 겹치지 않는 텐서는 같은 주소를 재사용한다.
- **Temporary**: head 끝에서 tail 쪽으로 자라는 임시 할당. `AllocateTensors()` 중에만 쓰이고 리셋된다.
- **Tail**: arena 끝에서 앞쪽으로 자라는 **영속(persistent)** 할당 — `MicroAllocator` 객체 자신(placement new로 arena 안에 만든다, `micro_allocator.cc` 575~577행), 텐서 메타데이터(`TfLiteEvalTensor`), op 노드·registration 표, 커널 OpData(per-channel multiplier 등), 입력·출력 `TfLiteTensor`.

```svg
<svg viewBox="0 0 660 250" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13">tensor arena — 이 노트 모델, reference 커널, 실제 사용 8,464 B</text>
  <rect x="20" y="40" width="425" height="50" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/>
  <rect x="445" y="40" width="175" height="50" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
  <text x="232" y="62" text-anchor="middle" font-size="13">head 6,000 B</text>
  <text x="232" y="80" text-anchor="middle" font-size="12">활성값 (planner가 offset 결정)</text>
  <text x="532" y="62" text-anchor="middle" font-size="13">tail 2,464 B</text>
  <text x="532" y="80" text-anchor="middle" font-size="12">persistent</text>
  <line x1="20" y1="100" x2="20" y2="110" stroke="currentColor"/>
  <text x="20" y="124" text-anchor="middle" font-size="12">0</text>
  <line x1="445" y1="100" x2="445" y2="110" stroke="currentColor"/>
  <text x="445" y="124" text-anchor="middle" font-size="12">6000</text>
  <line x1="620" y1="100" x2="620" y2="110" stroke="currentColor"/>
  <text x="620" y="124" text-anchor="middle" font-size="12">8464</text>
  <text x="20" y="152" font-size="12">op2(1×1 conv) 실행 순간의 head (개념도):</text>
  <rect x="20" y="162" width="142" height="30" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
  <text x="91" y="182" text-anchor="middle" font-size="12">dw 출력 2,000</text>
  <rect x="162" y="162" width="283" height="30" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/>
  <text x="303" y="182" text-anchor="middle" font-size="12">1×1 conv 출력 4,000</text>
  <text x="20" y="222" font-size="12">op0(첫 conv) 순간에는 같은 head에 입력 490 + 출력 2,000만 산다 → 주소를 재사용</text>
  <text x="445" y="152" font-size="12">tail 내용 (record 모드):</text>
  <text x="445" y="172" font-size="12">EvalTensor 408 · Node 448</text>
  <text x="445" y="190" font-size="12">OpData·버퍼 988 · 기타</text>
</svg>
```

그림 3 — 이 노트 모델의 arena 구조(축척 맞춤: 600 px = 8,464 B). head 6,000 B는 "가장 많은 활성값이 동시에 살아 있는 순간"의 합과 정확히 같다(5.3절). 아래 칸의 배치는 개념도이며 실제 offset은 greedy planner가 정한다.

### 5.2 예제 6 — `RecordingMicroInterpreter`로 tail 내역 보기

무엇을 확인하나: arena가 어디에 쓰였는지 종류별로 본다. 4.2절 앱의 `record` 모드다.

```sh
./kws_ref 16384 record
```

```text
arena_used_bytes = 8672 / 16384
[RecordingMicroAllocator] Arena allocation total 8672 bytes
[RecordingMicroAllocator] Arena allocation head 6000 bytes
[RecordingMicroAllocator] Arena allocation tail 2672 bytes
[RecordingMicroAllocator] 'TfLiteEvalTensor data' used 408 bytes with alignment overhead (requested 408 bytes for 17 allocations)
[RecordingMicroAllocator] 'Persistent TfLiteTensor data' used 128 bytes with alignment overhead (requested 128 bytes for 2 tensors)
[RecordingMicroAllocator] 'Persistent TfLiteTensor quantization data' used 64 bytes with alignment overhead (requested 64 bytes for 4 allocations)
[RecordingMicroAllocator] 'Persistent buffer data' used 988 bytes with alignment overhead (requested 952 bytes for 16 allocations)
[RecordingMicroAllocator] 'NodeAndRegistration struct' used 448 bytes with alignment overhead (requested 448 bytes for 7 NodeAndRegistration structs)
```

출력에서 볼 것:

- head 6,000 B는 일반 모드와 같고, tail이 2,672 B로 일반 모드(8,464 − 6,000 = 2,464 B)보다 208 B 크다. Recording용 allocator 객체가 더 크고 그것도 arena tail에 들어가기 때문이다. **최종 arena 크기는 recording 모드가 아니라 실제 쓸 interpreter로 잰다.**
- `TfLiteEvalTensor` 17개 × 24 B = 408 B: 그래프의 텐서 17개(활성값 8 + 상수 9)마다 메타데이터가 하나씩. 텐서 하나당 24 B는 호스트(64-bit 포인터) 값이다. Cortex-M(32-bit)에서는 포인터가 4 B라 더 작아진다 — **tail 크기는 타깃 아키텍처에서 다시 재야 한다.**
- `NodeAndRegistration` 7개 × 64 B = 448 B: op 7개.
- "Persistent buffer data" 988 B: 커널 OpData — per-channel requantization multiplier·shift 배열 등. 출력 채널이 많을수록 커진다.

### 5.3 예제 7 — `.tflite`만 보고 활성값 peak 추정하기

D2 5절에서는 PyTorch 그래프(torch.fx)로 arena를 추정했다. 배포 직전에는 **변환된 `.tflite` 자체**로 추정하는 것이 더 정확하다 — BN folding, activation fusion, RESHAPE 같은 변환기 결과가 반영돼 있기 때문이다.

먼저 손으로. 텐서 크기(int8이라 원소 수 = 바이트 수):

```
input           49×10×1  =  490
conv1 out       25×5×16  = 2000     (stride 2, same: ceil(49/2)=25, ceil(10/2)=5)
dw out          25×5×16  = 2000
conv1x1 out     25×5×32  = 4000
pool out        1×1×32   =   32
reshape out     1×32     =   32
fc out          1×4      =    4
softmax out     1×4      =    4

op 실행 순간 살아 있는 것 (입력 + 출력, 이후에 다시 읽히는 것)
op0 conv1   : 490 + 2000          = 2490
op1 dw      : 2000 + 2000         = 4000
op2 conv1x1 : 2000 + 4000         = 6000   ← peak
op3 pool    : 4000 + 32           = 4032
```

말로 하면: 순차 그래프에서는 "op의 입력 + 출력"이 동시에 살아 있고, 1×1 conv가 채널을 32로 펼치는 순간이 peak다. residual 같은 skip 연결이 있으면 그 텐서도 얹힌다(D2 3.4절).

같은 계산을 스크립트로.

```python
# est.py — .tflite 의 텐서·op 정보만으로 arena 의 "활성값 부분"을 추정 (liveness peak)
# (import 와 경고 끄기 줄은 ref.py 와 같아서 생략)
it = tf.lite.Interpreter("kws_int8.tflite",
        experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
it.allocate_tensors()
T = {t["index"]: t for t in it.get_tensor_details()}
ops = it._get_ops_details()
nbytes = lambda i: int(np.prod(T[i]["shape"])) * np.dtype(T[i]["dtype"]).itemsize
acts = {it.get_input_details()[0]["index"]} | {o for op in ops for o in op["outputs"]}
last = {}                                        # 각 활성값이 마지막으로 읽히는 op
for k, op in enumerate(ops):
    for i in op["inputs"]:
        if i in acts: last[i] = k
out_idx = it.get_output_details()[0]["index"]; last[out_idx] = len(ops)
born = {i: -1 for i in acts}
for k, op in enumerate(ops):
    for o in op["outputs"]: born[o] = k
peak = 0
for k, op in enumerate(ops):
    live = [i for i in acts if born[i] <= k <= last.get(i, born[i])]
    s = sum(nbytes(i) for i in live); peak = max(peak, s)
    print(f"{k} {op['op_name']:<18} out={str(T[op['outputs'][0]]['shape'].tolist()):<15} live={s:5d} B")
w = sum(nbytes(i) for i in T if i not in acts and nbytes(i) > 0)
print("sum of all activations =", sum(nbytes(i) for i in acts), "B  | peak live =", peak, "B")
print("weights+bias (flash)   =", w, "B")
```

```text
0 CONV_2D            out=[1, 25, 5, 16]  live= 2490 B
1 DEPTHWISE_CONV_2D  out=[1, 25, 5, 16]  live= 4000 B
2 CONV_2D            out=[1, 25, 5, 32]  live= 6000 B
3 AVERAGE_POOL_2D    out=[1, 1, 1, 32]   live= 4032 B
4 RESHAPE            out=[1, 32]         live=   64 B
5 FULLY_CONNECTED    out=[1, 4]          live=   36 B
6 SOFTMAX            out=[1, 4]          live=    8 B
sum of all activations = 8562 B  | peak live = 6000 B
weights+bias (flash)   = 1304 B
```

출력에서 볼 것: 추정 peak **6,000 B = TFLM이 보고한 head 6,000 B**와 정확히 같다. 모든 활성값을 따로 잡으면 8,562 B인데, 수명이 안 겹치는 텐서끼리 주소를 공유해서 30% 줄었다. 반면 이 추정은 **tail 2,464 B를 전혀 모른다** — 이 작은 모델에서는 arena의 29%가 tail이다.

```svg
<svg viewBox="0 0 640 290" xmlns="http://www.w3.org/2000/svg">
  <line x1="70" y1="230" x2="610" y2="230" stroke="currentColor"/>
  <line x1="70" y1="30" x2="70" y2="230" stroke="currentColor"/>
  <text x="62" y="234" text-anchor="end" font-size="12">0</text>
  <text x="62" y="134" text-anchor="end" font-size="12">3000</text>
  <text x="62" y="34" text-anchor="end" font-size="12">6000</text>
  <line x1="66" y1="130" x2="70" y2="130" stroke="currentColor"/>
  <line x1="70" y1="30" x2="610" y2="30" stroke="#d0564a" stroke-dasharray="5 4"/>
  <text x="606" y="24" text-anchor="end" font-size="12">TFLM head = 6000 B</text>
  <rect x="83" y="147" width="50" height="83" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="160" y="96.7" width="50" height="133.3" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="237" y="30" width="50" height="200" fill="#e08a3c" fill-opacity="0.85"/>
  <rect x="314" y="95.6" width="50" height="134.4" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="391" y="227.9" width="50" height="2.1" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="468" y="228.8" width="50" height="1.2" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="545" y="229" width="50" height="1" fill="#4a7bd0" fill-opacity="0.7"/>
  <text x="108" y="141" text-anchor="middle" font-size="12">2490</text>
  <text x="185" y="91" text-anchor="middle" font-size="12">4000</text>
  <text x="262" y="46" text-anchor="middle" font-size="12">6000</text>
  <text x="339" y="90" text-anchor="middle" font-size="12">4032</text>
  <text x="416" y="222" text-anchor="middle" font-size="12">64</text>
  <text x="493" y="222" text-anchor="middle" font-size="12">36</text>
  <text x="570" y="222" text-anchor="middle" font-size="12">8</text>
  <text x="108" y="248" text-anchor="middle" font-size="12">CONV</text>
  <text x="185" y="248" text-anchor="middle" font-size="12">DWCONV</text>
  <text x="262" y="248" text-anchor="middle" font-size="12">CONV 1×1</text>
  <text x="339" y="248" text-anchor="middle" font-size="12">AVGPOOL</text>
  <text x="416" y="248" text-anchor="middle" font-size="12">RESHAPE</text>
  <text x="493" y="248" text-anchor="middle" font-size="12">FC</text>
  <text x="570" y="248" text-anchor="middle" font-size="12">SOFTMAX</text>
  <text x="340" y="275" text-anchor="middle" font-size="12">op 실행 순서 → (막대 = 그 순간 살아 있는 활성값 바이트)</text>
</svg>
```

그림 4 — op별 live 활성값 바이트(est.py 실제 출력, 세로축 200 px = 6,000 B). 1×1 conv 순간(주황)이 peak이고, 이 값이 TFLM `RecordingMicroAllocator`의 head와 같다. 모델을 줄이고 싶으면 이 막대를 낮춰야 한다 — 예: 1×1 conv 채널 32 → 24면 peak는 2000 + 3000 = 5000 B.

### 5.4 arena 크기 정하는 절차

| 단계 | 하는 일 | 결과 |
|---|---|---|
| 1. 설계 단계 추정 | D2 추정기 또는 est.py로 peak-live 계산 + tail 여유(작은 모델 2~3 KB, op·채널 수에 비례) | 후보 모델 거르기 |
| 2. 넉넉히 잡고 측정 | arena = 추정 × 2 정도로 빌드 → `AllocateTensors()` 후 `arena_used_bytes()` 출력 | 실제 사용량 |
| 3. 확정 | 실제 사용량 + 정렬 여유(헤더 주석: 정렬이 16이 아니면 +16) + 팀 규칙 margin | `kTensorArenaSize` |
| 4. 회귀 방지 | CI에서 `arena_used_bytes()`를 기록·비교, 커널 라이브러리·모델 변경 시 재측정 | 숫자가 슬쩍 커지는 것 감지 |

`arena_used_bytes()`의 헤더 주석(`micro_interpreter.h` 143~148행)은 이렇게 말한다: "Returns the actual used arena in bytes. This method gives the optimal arena size. It's only available after `AllocateTensors` has been called. Note that normally `tensor_arena` requires 16 bytes alignment to fully utilize the space. If it's not the case, the optimal arena size would be arena_used_bytes() + 16."

실제로 정확히 8,464 B로 줄여서 돌려 봤다.

```text
$ ./kws_ref 8464
arena_used_bytes = 8464 / 8464
sample 0: tflm=[ -128 -128  -60   60 ]  golden=[ -128 -128  -60   60 ]
(이하 동일, mismatches 0)
```

경계에서도 돈다. 하지만 양산 코드에서 0 margin은 금물이다 — 커널 라이브러리 버전이 바뀌거나 CMSIS-NN으로 바꾸기만 해도 숫자가 바뀐다(7.4절에서 실제로 +80 B).

### 5.5 예제 8 — arena가 모자랄 때, op가 빠졌을 때 (실제 에러 메시지)

무엇을 확인하나: 보드 bring-up에서 가장 많이 보는 두 에러를 일부러 낸다.

```sh
./kws_ref 8000              # arena를 8,000 B로
./kws_ref 16384 nosoftmax   # resolver에서 AddSoftmax() 빼기
```

```text
Failed to resize buffer. Requested: 6000, available 5760, missing: 240
AllocateTensors() failed (arena=8000)

Didn't find op for builtin opcode 'SOFTMAX'
Failed to get registration from op code SOFTMAX
 
AllocateTensors() failed (arena=16384)
```

출력에서 볼 것:

- arena 부족: "Requested: 6000" = head가 필요한 크기(예제 7의 peak), "available 5760" = 8,000 B에서 그 시점까지 tail이 쓴 2,240 B를 뺀 나머지. 메시지만 보고 "head가 6,000 필요한데 240 모자라다"를 읽어 내면 바로 다음 크기를 정할 수 있다. **어떤 할당에서 실패했는지에 따라 메시지가 다르다** — tail이 먼저 모자라면 "Failed to allocate memory for ..." 계열 메시지(`micro_allocator.cc`)가 나온다.
- op 누락: 첫 줄은 `micro_op_resolver.cc` 37행, 둘째 줄은 `micro_interpreter.cc` 131행에서 나온다. 둘 다 **`Invoke()`가 아니라 `AllocateTensors()`에서** 터진다 — 부팅 단계에서 잡힌다.

### 5.6 arena를 더 아끼는 방법 (개념)

- **offline memory plan**: 변환 후 툴로 텐서 offset을 미리 계산해 flatbuffer metadata에 넣으면 런타임 planner가 그대로 쓴다(`docs/offline_memory_plan.md`). planner 코드와 계획 시간을 줄이고, 결과를 빌드 시점에 확정할 수 있다.
- **여러 모델이 arena 공유**: `MicroInterpreter`에는 `MicroAllocator*`를 받는 두 번째 생성자가 있고, 헤더 주석이 "allocation handled in more than one interpreter"용이라고 설명한다. VAD와 KWS처럼 동시에 돌지 않는 모델이면 같은 SRAM을 나눠 쓸 수 있다(J1). 단, 수명·순서를 설계 문서로 못 박아야 한다.
- **모델 쪽에서 줄이기**: peak 막대(그림 4)를 낮추는 구조 변경 — 채널 축소, 다운샘플을 앞당기기, patch 기반 실행 — 은 D2 9절, C7에서 다뤘다.

---

## 6. Op resolver — 필요한 커널만 링크한다

### 6.1 어떻게 생겼나

`micro_mutable_op_resolver.h`를 열어 보면 구조가 단순하다.

```cpp
template <unsigned int tOpCount>
class MicroMutableOpResolver : public MicroOpResolver {
  ...
  TfLiteStatus AddConv2D(const TFLMRegistration& registration = Register_CONV_2D()) {
    return AddBuiltin(BuiltinOperator_CONV_2D, registration, ParseConv2D);
  }
  ...
  TFLMRegistration registrations_[tOpCount];       // 커널 함수 포인터 묶음 (init/prepare/invoke...)
  BuiltinOperator builtin_codes_[tOpCount];
  TfLiteBridgeBuiltinParseFunction builtin_parsers_[tOpCount];
};
```

- 템플릿 인자 `tOpCount`가 배열 크기다. 칸이 모자라면 `AddBuiltin`이 "Couldn't register builtin op #%d, resolver size is too small (%d)."를 찍고 실패한다. 같은 op를 두 번 넣으면 "Calling AddBuiltin with the same op more than once is not supported"가 나온다.
- 조회(`FindOp`)는 이 배열을 **선형 탐색**한다. op 수가 수십 개라 부팅 때 한 번이면 비용은 무시할 만하다.
- `AddConv2D()`의 인자는 기본값이 `Register_CONV_2D()`다. 여기에 `Register_CONV_2D_INT8()`처럼 **타입 특화 registration**을 넘길 수도 있다(`kernels/conv.h` 120행). CMSIS-NN·Xtensa 빌드에서는 이것이 int8 전용 커널이라 링커가 float·int16 경로 코드를 버릴 수 있어 flash가 더 준다. reference 빌드에서는 `Register_CONV_2D()`를 그대로 반환하는 inline 함수라(128행) 차이가 없다. 특화했는데 모델에 다른 타입 conv가 있으면 실패한다.

펌웨어 비유: **"인터럽트 벡터 테이블처럼, 필요한 핸들러만 채워 넣는 함수 포인터 테이블"** 이다. 그리고 등록하지 않은 커널의 코드는 아무도 참조하지 않으므로 `--gc-sections`(macOS는 `-dead_strip`)가 버린다. 이게 resolver의 진짜 가치다.

### 6.2 `AllOpsResolver`는?

이 commit의 소스 전체를 `grep -rn AllOpsResolver`로 찾으면 남은 것은 `examples/person_detection/person_detection_test.cc` 48행의 **주석 한 줄**("An easier approach is to just use the AllOpsResolver, but this will ...")뿐이다. 클래스와 헤더(`all_ops_resolver.h`)는 이미 소스에 없다. 모든 op를 링크하는 "편한" 길을 저장소가 아예 치워 버린 것이다. 오래된 튜토리얼에서 `#include "tensorflow/lite/micro/all_ops_resolver.h"`를 보면 그 시점 이후 API가 바뀌었다고 이해하면 된다(정확히 어느 릴리스에서 지워졌는지는 shallow clone이라 확인하지 않았다).

### 6.3 예제 9 — op 6개 vs 88개, flash(코드) 크기 비교

무엇을 확인하나: "필요한 op만 등록하면 flash가 준다"를 숫자로. 같은 `main.cc`에서 resolver만 `MicroMutableOpResolver<128>`로 바꾸고 `AddAbs()`부터 `AddZerosLike()`까지 82개를 더 등록한 변형(`main_many.cc`, op 88개 — 옛 `AllOpsResolver` 흉내)을 빌드해서 섹션 크기를 비교했다. `build2.sh`는 4.2절 g++ 명령에서 소스 파일 이름과 `-D` 정의만 바꿀 수 있게 한 스크립트다.

```sh
SRC=main_many.cc EXTRA=-DMANY_OPS ./build2.sh osx_arm64_default_gcc kws_many
for b in kws_ref kws_many kws_cmsis; do echo "== $b"; size -m $b | grep -E "Section (__text|__const|__bss):"; done
```

```text
== kws_ref
	Section __text: 75476
	Section __const: 8656
	Section __const: 3240
	Section __bss: 148808 (zerofill)
== kws_many
	Section __text: 515308
	Section __const: 10828
	Section __const: 3240
	Section __bss: 157104 (zerofill)
== kws_cmsis
	Section __text: 121388
	Section __const: 8704
	Section __const: 3240
	Section __bss: 148808 (zerofill)
```

출력에서 볼 것: 코드(`__text`)가 op 6개일 때 75 KB, 88개일 때 515 KB — **6.8배**. 모델은 바뀌지 않았고 결과도 같다(kws_many도 mismatches 0). 모델 데이터(`__const` 8,656 B 안에 6,736 B 모델 포함)보다 코드가 훨씬 크다는 점도 눈여겨볼 것. 이 숫자는 arm64 호스트 `-Os` 기준이라 Cortex-M Thumb-2 코드 크기와 같지 않다. 그래도 "등록 = 링크"라는 관계와 대략의 비율은 그대로 성립한다. `__bss` 148 KB의 대부분은 arena가 아니라 MicroProfiler다 — 8.3절의 함정.

```svg
<svg viewBox="0 0 640 200" xmlns="http://www.w3.org/2000/svg">
  <line x1="170" y1="30" x2="170" y2="160" stroke="currentColor"/>
  <rect x="170" y="40" width="65.9" height="28" fill="#3f9a6b" fill-opacity="0.8"/>
  <rect x="170" y="80" width="106" height="28" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="170" y="120" width="450" height="28" fill="#d0564a" fill-opacity="0.8"/>
  <text x="162" y="59" text-anchor="end" font-size="12">op 6개 (reference)</text>
  <text x="162" y="99" text-anchor="end" font-size="12">op 6개 (CMSIS-NN)</text>
  <text x="162" y="139" text-anchor="end" font-size="12">op 88개 (reference)</text>
  <text x="243" y="59" font-size="12">75.5 KB</text>
  <text x="284" y="99" font-size="12">121.4 KB</text>
  <text x="612" y="139" text-anchor="end" font-size="12">515.3 KB</text>
  <text x="395" y="185" text-anchor="middle" font-size="12">__text 섹션 크기 (호스트 arm64, -Os, -dead_strip) — 같은 모델, 같은 출력</text>
</svg>
```

그림 5 — resolver에 등록한 op 수와 커널 라이브러리에 따른 코드 크기(실측, 450 px = 515.3 KB). 등록한 커널만 링크되므로 op 목록이 곧 flash 예산이다. CMSIS-NN 빌드가 더 큰 이유는 7.3절에서 본다.

### 6.4 모델에 어떤 op가 있는지 알아내기

resolver를 채우려면 모델의 op 목록이 필요하다. 방법은 셋이다.

1. 예제 3처럼 Python에서 `_get_ops_details()`로 출력(비공개 API지만 편하다).
2. `tf.lite.experimental.Analyzer.analyze(model_path=...)`로 그래프 덤프.
3. Netron으로 시각화(F5).

그리고 **팁**: resolver를 너무 작게 잡거나 op를 빠뜨려도 결과는 "부팅 때 에러 메시지"다. 그러니 CI에서 호스트 TFLM 빌드로 실제 앱의 resolver를 그대로 써서 `AllocateTensors()`를 한 번 돌리는 테스트를 두면, 모델팀이 op를 하나 추가했을 때 보드에 올리기 전에 잡힌다. 이 노트의 `kws_ref` 바이너리가 정확히 그런 테스트다.

### 6.5 함정

- 모델팀이 GELU나 HardSwish 같은 activation을 바꾸면 op가 하나 추가된다(예: `HARD_SWISH`). resolver 템플릿 인자와 `Add*` 줄을 같이 고쳐야 한다.
- 변환 옵션 차이로 같은 Keras 모델에서도 op가 달라진다 — Dense 앞에 `RESHAPE`가 생기거나, int8 변환이 덜 돼서 `QUANTIZE`/`DEQUANTIZE`가 끼는 경우. 후자는 성능·정확도에도 영향을 주므로 op 목록에 `QUANTIZE`/`DEQUANTIZE`가 보이면 변환을 다시 본다(F1, C2).
- `Register_CONV_2D_INT8()`로 특화했는데 나중에 int16 activation(16x8) 모델로 바꾸면 Prepare에서 타입 에러가 난다.

---

## 7. 최적화 커널 — reference vs CMSIS-NN vs Ethos-U

### 7.1 커널 선택 메커니즘: "같은 파일 이름이면 덮어쓴다"

TFLM은 런타임에 커널을 고르지 않는다. **빌드 시점에 어느 `.cc` 파일을 컴파일할지**를 바꾼다. `tools/make/Makefile`에서:

- `OPTIMIZED_KERNEL_DIR=cmsis_nn` → `kernels/cmsis_nn/`을 "특화 디렉터리"로 쓴다. `-DCMSIS_NN` 정의가 추가되고(136~137행: 이름을 대문자로 바꿔 `-D`), `ext_libs/cmsis_nn.inc`가 CMSIS·CMSIS-NN을 내려받아 소스 목록에 넣는다.
- `CO_PROCESSOR=ethos_u` → `kernels/ethos_u/`을 특화 디렉터리로 쓴다. 주석(96~99행): "If the same kernel is implemented in both kernels/OPTIMIZED_KERNEL_DIR and kernels/CO_PROCESSOR, then the implementation from kernels/CO_PROCESSOR will be used."
- 실제 교체는 `tools/make/specialize_files.py`가 한다. 기본 커널 파일 목록을 돌면서 **같은 basename의 파일이 특화 디렉터리에 있으면 그 경로로 바꾼다.** 없으면 reference를 그대로 쓴다.

```svg
<svg viewBox="0 0 660 260" xmlns="http://www.w3.org/2000/svg">
  <text x="10" y="58" font-size="12">kernels/ethos_u/</text>
  <text x="10" y="74" font-size="12">(CO_PROCESSOR)</text>
  <text x="10" y="118" font-size="12">kernels/cmsis_nn/</text>
  <text x="10" y="134" font-size="12">(OPTIMIZED_KERNEL_DIR)</text>
  <text x="10" y="178" font-size="12">kernels/</text>
  <text x="10" y="194" font-size="12">(reference)</text>
  <rect x="560" y="45" width="85" height="34" rx="4" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
  <text x="602" y="67" text-anchor="middle" font-size="12">ethosu.cc</text>
  <rect x="200" y="105" width="85" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="242" y="127" text-anchor="middle" font-size="12">conv.cc</text>
  <rect x="290" y="105" width="85" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="332" y="122" text-anchor="middle" font-size="12">depthwise_</text>
  <text x="332" y="135" text-anchor="middle" font-size="12">conv.cc</text>
  <rect x="470" y="105" width="85" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="512" y="127" text-anchor="middle" font-size="12">softmax.cc</text>
  <rect x="200" y="165" width="85" height="34" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/>
  <text x="242" y="187" text-anchor="middle" font-size="12">conv.cc</text>
  <rect x="290" y="165" width="85" height="34" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/>
  <text x="332" y="182" text-anchor="middle" font-size="12">depthwise_</text>
  <text x="332" y="195" text-anchor="middle" font-size="12">conv.cc</text>
  <rect x="380" y="165" width="85" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
  <text x="422" y="187" text-anchor="middle" font-size="12">reshape.cc</text>
  <rect x="470" y="165" width="85" height="34" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/>
  <text x="512" y="187" text-anchor="middle" font-size="12">softmax.cc</text>
  <rect x="560" y="165" width="85" height="34" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/>
  <text x="602" y="182" text-anchor="middle" font-size="12">ethosu.cc</text>
  <text x="602" y="195" text-anchor="middle" font-size="12">(stub)</text>
  <line x1="190" y1="220" x2="650" y2="220" stroke="currentColor"/>
  <text x="420" y="242" text-anchor="middle" font-size="12">빌드에 들어가는 것 = 각 열에서 위에 있는 칠해진 칸 (같은 파일 이름이면 위가 이긴다)</text>
</svg>
```

그림 6 — `specialize_files.py`의 파일 덮어쓰기 규칙. 점선 칸은 위층 파일 때문에 빌드에서 빠진다. `reshape.cc`처럼 CMSIS-NN에 없는 커널은 reference가 그대로 쓰인다. `ethosu.cc`의 reference 버전은 `Register_ETHOSU()`가 `nullptr`을 반환하는 빈 껍데기다.

### 7.2 예제 10 — `specialize_files.py`를 직접 돌려 보기

무엇을 확인하나: 그림 6의 규칙을 실제 스크립트로.

```sh
K=tensorflow/lite/micro/kernels
python tensorflow/lite/micro/tools/make/specialize_files.py \
  --base_files "$K/conv.cc $K/depthwise_conv.cc $K/reshape.cc $K/softmax.cc $K/ethosu.cc" \
  --specialize_directory $K/cmsis_nn | tr ' ' '\n'
python tensorflow/lite/micro/tools/make/specialize_files.py \
  --base_files "$K/conv.cc $K/ethosu.cc" --specialize_directory $K/ethos_u | tr ' ' '\n'
```

```text
tensorflow/lite/micro/kernels/cmsis_nn/conv.cc
tensorflow/lite/micro/kernels/cmsis_nn/depthwise_conv.cc
tensorflow/lite/micro/kernels/reshape.cc
tensorflow/lite/micro/kernels/cmsis_nn/softmax.cc
tensorflow/lite/micro/kernels/ethosu.cc
---
tensorflow/lite/micro/kernels/conv.cc
tensorflow/lite/micro/kernels/ethos_u/ethosu.cc
```

출력에서 볼 것: 앱 코드는 한 줄도 안 바뀐다. `resolver.AddConv2D()`가 부르는 `Register_CONV_2D()` 심볼의 **정의가 다른 파일에서 온다**. 펌웨어로 치면 "보드별 HAL `.c` 파일을 빌드 시스템이 골라 넣는 것"과 정확히 같다. 링크 타임 다형성이다.

### 7.3 CMSIS-NN이 맡는 커널

이 commit의 `kernels/cmsis_nn/` 디렉터리 파일 목록이 곧 "CMSIS-NN 경로가 있는 커널"이다.

```text
add.cc  batch_matmul.cc  conv.cc  depthwise_conv.cc  fully_connected.cc  maximum_minimum.cc  mul.cc
pad.cc  pooling.cc  softmax.cc  svdf.cc  transpose_conv.cc  transpose.cc  unidirectional_sequence_lstm.cc
```

각 파일이 내보내는 registration(일부): `Register_CONV_2D_INT8`, `Register_CONV_2D_INT16`, `Register_CONV_2D_INT4`, `Register_DEPTHWISE_CONV_2D_INT8`, `Register_FULLY_CONNECTED_INT8`, `Register_AVERAGE_POOL_2D_INT8`, `Register_MAX_POOL_2D_INT8`, `Register_SOFTMAX_INT8`, `Register_SVDF_INT8`, `Register_UNIDIRECTIONAL_SEQUENCE_LSTM_INT8` 등. 즉 **int8(일부 int16·int4) 양자화 경로**가 대상이다.

`cmsis_nn/conv.cc`를 열어 보면 구조가 보인다.

- `Prepare`에서 `arm_convolve_wrapper_s8_get_buffer_size(...)`로 필요한 scratch 크기를 물어보고 `context->RequestScratchBufferInArena(...)`로 arena head에 요청한다(155~165행). scratch는 im2col 같은 임시 버퍼다(D2 4.1절).
- `Eval`의 int8 경로는 `arm_convolve_wrapper_s8(...)`을 부른다(202행). wrapper가 1×1, 1×N, 일반 conv 중 맞는 CMSIS-NN 함수를 고른다.
- `case kTfLiteFloat32:`는 `tflite::micro::reference_ops::Conv(...)`로 떨어진다(432~433행). **float 모델에 CMSIS-NN을 켜도 빨라지지 않는다.** CMSIS-NN 빌드가 reference보다 코드가 큰 이유(그림 5)도 이것이다 — CMSIS-NN 커널 파일이 float용 reference 경로와 CMSIS-NN 라이브러리 함수(이 빌드에서 `arm_` 오브젝트 113개)를 함께 끌고 온다.

CMSIS-NN 함수 내부는 코어 기능에 따라 세 갈래로 컴파일된다: 순수 C, Armv7E-M DSP 확장(SMLAD 등, E2 2절), Helium/MVE(E2 3절). 이 Mac(AArch64)은 M-profile 매크로(`__ARM_FEATURE_DSP`, `__ARM_FEATURE_MVE`)가 정의되지 않아(`clang -dM -E`로 확인, `__ARM_NEON`만 있음) **순수 C 경로**로 빌드됐다.

### 7.4 예제 11 — CMSIS-NN 빌드로 같은 앱 실행

무엇을 확인하나: 커널만 CMSIS-NN으로 바꿨을 때 출력이 그대로인지(bit-exact), arena와 시간이 어떻게 바뀌는지.

```sh
$GM -f tensorflow/lite/micro/tools/make/Makefile -j8 OPTIMIZED_KERNEL_DIR=cmsis_nn microlite   # 114초
./build.sh osx_arm64_default_cmsis_nn_gcc kws_cmsis    # 4.2절과 같은 g++ 명령, 라이브러리 경로만 다름
./kws_cmsis
```

```text
arena_used_bytes = 8544 / 16384
sample 0: tflm=[ -128 -128  -60   60 ]  golden=[ -128 -128  -60   60 ]
sample 1: tflm=[ -128 -128   36  -36 ]  golden=[ -128 -128   36  -36 ]
sample 2: tflm=[ -128 -128   40  -41 ]  golden=[ -128 -128   40  -41 ]
mismatches vs BUILTIN_REF: 0
"Unique Tag","Total ticks across all events with that tag."
CONV_2D, 4745
DEPTHWISE_CONV_2D, 2988
AVERAGE_POOL_2D, 801
RESHAPE, 63
FULLY_CONNECTED, 80
SOFTMAX, 106
"total number of ticks", 8783
```

출력에서 볼 것:

- **bit-exact**: CMSIS-NN 커널도 TFLite reference와 같은 int8 결과를 낸다. CMSIS-NN은 TFLite의 requantization(C1 7.4절의 SRDHM + RDBPOT)과 같은 반올림을 구현하도록 만들어져 있고, 이 모델에서 실제로 12개 출력이 모두 일치했다. (모든 모델·모든 op에서 항상 bit-exact라고 이 실험이 보장하지는 않는다. 그래서 J6의 golden 테스트를 커널 교체 때마다 돌린다.)
- **arena +80 B** (8,464 → 8,544). record 모드로 보면 head는 6,000 그대로이고 tail이 2,752 B로 늘었다 — CMSIS-NN 커널의 OpData가 조금 더 크다. 이 호스트 빌드(순수 C 경로)에서는 scratch가 head를 키우지 않았지만, DSP/MVE 경로에서는 im2col scratch가 생겨 head가 커질 수 있다. **타깃 빌드에서 다시 잰다.**
- **시간**: 100회 누적 tick(호스트 `clock()`, 1 tick = 1 µs)을 1회로 나누면 reference 약 720 µs, CMSIS-NN 약 88 µs. 세 번씩 더 돌린 총 tick은 reference 73,019 / 76,207 / 74,441, CMSIS-NN 8,314 / 7,402 / 9,974였다 — 약 8배 차이, 실행마다 ±10~15% 흔들린다.

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
  <line x1="60" y1="240" x2="620" y2="240" stroke="currentColor"/>
  <line x1="60" y1="40" x2="60" y2="240" stroke="currentColor"/>
  <text x="52" y="244" text-anchor="end" font-size="12">0</text>
  <text x="52" y="144" text-anchor="end" font-size="12">300</text>
  <text x="52" y="44" text-anchor="end" font-size="12">600</text>
  <line x1="56" y1="140" x2="60" y2="140" stroke="currentColor"/>
  <line x1="56" y1="40" x2="60" y2="40" stroke="currentColor"/>
  <text x="24" y="140" text-anchor="middle" font-size="12" transform="rotate(-90 24 140)">µs / Invoke</text>
  <rect x="90" y="49.7" width="40" height="190.3" fill="#d0564a" fill-opacity="0.8"/>
  <rect x="135" y="224.3" width="40" height="15.7" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="230" y="195.3" width="40" height="44.7" fill="#d0564a" fill-opacity="0.8"/>
  <rect x="275" y="230" width="40" height="10" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="370" y="236" width="40" height="4" fill="#d0564a" fill-opacity="0.8"/>
  <rect x="415" y="237.3" width="40" height="2.7" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="510" y="239.1" width="40" height="0.9" fill="#d0564a" fill-opacity="0.8"/>
  <rect x="555" y="239.2" width="40" height="0.8" fill="#4a7bd0" fill-opacity="0.8"/>
  <text x="110" y="44" text-anchor="middle" font-size="12">571</text>
  <text x="155" y="218" text-anchor="middle" font-size="12">47</text>
  <text x="250" y="189" text-anchor="middle" font-size="12">134</text>
  <text x="295" y="224" text-anchor="middle" font-size="12">30</text>
  <text x="390" y="230" text-anchor="middle" font-size="12">12</text>
  <text x="435" y="230" text-anchor="middle" font-size="12">8</text>
  <text x="530" y="232" text-anchor="middle" font-size="12">2.7</text>
  <text x="575" y="232" text-anchor="middle" font-size="12">2.5</text>
  <text x="132" y="258" text-anchor="middle" font-size="12">CONV_2D (×2)</text>
  <text x="272" y="258" text-anchor="middle" font-size="12">DEPTHWISE</text>
  <text x="412" y="258" text-anchor="middle" font-size="12">AVG_POOL</text>
  <text x="552" y="258" text-anchor="middle" font-size="12">RESHAPE+FC+SOFTMAX</text>
  <rect x="380" y="60" width="14" height="14" fill="#d0564a" fill-opacity="0.8"/>
  <text x="400" y="72" font-size="12">reference 커널 (합계 720 µs)</text>
  <rect x="380" y="84" width="14" height="14" fill="#4a7bd0" fill-opacity="0.8"/>
  <text x="400" y="96" font-size="12">CMSIS-NN 순수 C 경로 (합계 88 µs)</text>
  <text x="340" y="285" text-anchor="middle" font-size="12">호스트 Mac 실측 (MicroProfiler, 100회 평균) — MCU 속도가 아님</text>
</svg>
```

그림 7 — op별 시간(호스트, 200 px = 600 µs). CONV_2D 태그에는 conv 두 개(5×3 첫 conv와 1×1 conv)가 합쳐져 있다. reference 커널은 "읽기 쉬운 정답 코드"라 범용 인덱스 계산이 많고, CMSIS-NN은 순수 C 경로에서도 루프 구조가 최적화돼 있다.

**이 숫자를 어떻게 읽어야 하나 (정직하게)**: 이것은 Apple M 시리즈 big core에서 잰 시간이다. MCU에서는 클럭이 수십~수백 MHz, 캐시가 작거나 없고, flash wait state가 있다. CMSIS-NN이 Cortex-M에서 reference보다 빠른 이유는 여기서 본 루프 구조에 더해 **DSP·MVE SIMD 명령**(E2)이 들어가기 때문인데, 이 실험은 그 부분을 전혀 측정하지 못한다. 그러니 "8배"를 면접에서 MCU 수치처럼 말하면 안 된다. 말할 수 있는 것은 "커널을 바꿔도 출력이 bit-exact였고, arena가 +80 B 변했으며, 같은 앱 코드로 커널만 바꿀 수 있음을 호스트에서 검증했다"이다. MCU 사이클 수는 보드의 DWT 카운터(8.1절)나 Corstone-300 FVP로 재야 한다.

참고로 이 모델의 연산량은 conv1 25×5×16×(5×3×1) = 30,000 MAC, dw 25×5×16×9 = 18,000 MAC, 1×1 conv 25×5×32×16 = 64,000 MAC, FC 128 MAC, 합계 약 112k MAC다(D1). Cortex-M4 @ 64 MHz에서 CMSIS-NN이 "MAC당 몇 사이클"이 되는지는 보드에서 재 볼 숙제다(13절).

### 7.5 Ethos-U — 커널이 아니라 "custom op 하나"로 NPU에 넘긴다

Ethos-U(Arm microNPU, E5)는 방식이 다르다.

1. 변환된 int8 `.tflite`를 Arm의 **Vela** 컴파일러로 다시 컴파일한다(E5 6.4절). Vela는 NPU가 실행할 수 있는 연속 구간을 **`ethos-u`라는 이름의 custom op 하나**로 묶고, 그 안에 NPU command stream과 재배열된 가중치를 넣는다. NPU가 못 하는 op는 일반 TFLite op로 남는다(CPU fallback).
2. TFLM 쪽에서는 `CO_PROCESSOR=ethos_u`로 빌드하고 resolver에 `AddEthosU()`를 넣는다. `micro_mutable_op_resolver.h` 287행:

```cpp
TfLiteStatus AddEthosU() {
  TFLMRegistration* registration = tflite::Register_ETHOSU();
  if (registration) {
    return AddCustom(tflite::GetString_ETHOSU(), registration);
  }
  return kTfLiteOk;
}
```

3. reference 빌드의 `kernels/ethosu.cc`에서는 `Register_ETHOSU()`가 `nullptr`, `GetString_ETHOSU()`가 `""`이라 아무것도 등록되지 않는다. `CO_PROCESSOR=ethos_u` 빌드에서는 `kernels/ethos_u/ethosu.cc`로 바뀌어 이름이 `"ethos-u"`가 되고, `Eval`에서 Ethos-U 드라이버의 `ethosu_reserve_driver()` → `ethosu_invoke_v3(...)`를 부른다(152~153행).

결과적으로 Ethos-U 시스템의 그래프는 "CPU op 몇 개 + `ethos-u` custom op 한두 개"가 되고, CPU op는 CMSIS-NN 커널로 돈다. 그래서 Corstone-300(Cortex-M55 + Ethos-U55) 같은 시스템에서는 `OPTIMIZED_KERNEL_DIR=cmsis_nn CO_PROCESSOR=ethos_u`를 같이 쓰는 것이 일반적이다. Ethos-U 경로는 이 Mac에서 빌드하지 않았다(`ethos_u.inc`가 Arm 툴체인·드라이버를 전제로 하고, 하드웨어나 FVP 없이는 실행할 수 없다).

### 7.6 그 밖의 타깃

`kernels/` 아래에는 `xtensa/`(Cadence HiFi·Vision DSP — Don이 SSD에서 쓴 Xtensa 계열의 DSP 변형, E4), `arc_mli/`(Synopsys ARC), `ceva/`, `hexagon/` 디렉터리가 있고, `tools/make/targets/`에는 `cortex_m_generic`, `cortex_m_corstone_300`, `xtensa`, `riscv32_generic`, `hexagon`, `bluepill` 등 타깃 makefile이 있다. 구조는 전부 같다 — 같은 이름의 커널 파일을 덮어쓴다.

---

## 8. 프로파일링 — MicroProfiler

### 8.1 어떻게 붙어 있나

- `MicroInterpreter` 생성자의 profiler 인자(`MicroProfilerInterface*`)에 `MicroProfiler` 객체를 넘기면 된다.
- 그래프 실행 루프(`micro_interpreter_graph.cc` 270~271행)가 op마다 `ScopedMicroProfiler`를 만들어 `BeginEvent(op 이름)` / `EndEvent()`를 부른다. op 이름이 태그다.
- 시간은 `tflite::GetCurrentTimeTicks()`에서 온다. **기본 `micro_time.cc`는 0을 반환**한다("timing is an optional feature"). 호스트 빌드는 `-DTF_LITE_USE_CTIME`이라 `clock()`(`CLOCKS_PER_SEC` = 1,000,000)을 쓴다.
- Cortex-M용 `cortex_m_generic/micro_time.cc`는 처음 호출 때 `DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0; DWT->CTRL |= 1UL;`로 **DWT 사이클 카운터**를 켜고 `DWT->CYCCNT`를 반환한다(Cortex-M7은 `DWT->LAR = 0xC5ACCE55` unlock도 한다, M0/M0+는 DWT CYCCNT가 없어 제외). Don이 SSD 펌웨어에서 성능을 잴 때 쓰던 바로 그 카운터다.

출력 함수는 셋이다: `Log()`(이벤트별), `LogCsv()`(이벤트별 CSV), `LogTicksPerTagCsv()`(태그별 합). 그리고 `GetTotalTicks()`, `ClearEvents()`.

### 8.2 예제 결과 읽기

예제 5·11의 profiler 출력은 `ClearEvents()` 후 `Invoke()` 100번의 태그별 합이다. 손으로 나누면:

```
reference : CONV_2D 57109/100 = 571 µs,  DEPTHWISE 134 µs,  AVG_POOL 12 µs,  나머지 2.7 µs  → 720 µs
CMSIS-NN  : CONV_2D  4745/100 =  47 µs,  DEPTHWISE  30 µs,  AVG_POOL  8 µs,  나머지 2.5 µs  →  88 µs
```

말로 하면: 시간의 79%(reference)가 CONV_2D 두 개에 몰려 있다. 최적화할 곳은 conv이고, RESHAPE·FC·SOFTMAX는 건드릴 가치가 없다. 보드에서도 첫 단계는 똑같다 — op별 표를 뽑고, 큰 막대부터 본다(K1~K3).

### 8.3 함정 — 프로파일러 자체의 비용

`micro_profiler.h`를 보면 `static constexpr int kMaxEvents = 4096;`이고 멤버 배열이 네 개다: `tags_[4096]`(포인터), `start_ticks_[4096]`, `end_ticks_[4096]`(uint32), `total_ticks_per_tag_[4096]`(포인터 + uint32 구조체). 계산하면

```
64-bit 호스트 : 4096 × (8 + 4 + 4 + 16) = 131,072 B ≈ 128 KB
32-bit MCU    : 4096 × (4 + 4 + 4 + 8)  =  81,920 B ≈  80 KB
```

예제 9의 `__bss` 148,808 B 중 약 128 KB가 이 프로파일러다(arena는 16 KB). **SRAM 256 KB MCU에 프로파일러를 그대로 넣으면 SRAM의 1/3이 사라진다.** 실제 보드에서는 `MicroProfilerInterface`를 상속해 op 수만큼만 기록하는 작은 프로파일러를 직접 만들거나, `kMaxEvents`를 줄인 사본을 쓰거나, 디버그 빌드에서만 켠다. 또 `TF_LITE_STRIP_ERROR_STRINGS`를 정의하면 그래프 루프의 `ScopedMicroProfiler`와 로그 문자열이 통째로 빠진다(릴리스 빌드용) — 그러면 위의 에러 메시지도 안 나오므로, 현장 디버깅용 빌드와 양산 빌드를 구분해야 한다.

또 하나: 100회 루프에서 `kMaxEvents`를 넘으면 `MicroProfiler`는 "MicroProfiler errored out because total number of events exceeded the maximum of 4096."을 찍고 assert로 멈춘다. op 7개 × 100회 = 700 이벤트라 이 예제는 안전했지만, op 50개 모델로 100회를 돌리면 넘는다.

---

## 9. 실제 보드로 포팅하기

### 9.1 공식 절차 (`docs/new_platform_support.md`)

1. **소스 트리 뽑기**: `python3 tensorflow/lite/micro/tools/project_generation/create_tflm_tree.py -e hello_world /tmp/tflm-tree`. 필요한 소스·헤더·third_party만 복사한 디렉터리가 생기고, 이것을 벤더 IDE나 CMake 프로젝트에 넣어 `libtflm.a`로 빌드한다. 최적화 커널을 포함하려면 `--makefile_options="TARGET=cortex_m_generic OPTIMIZED_KERNEL_DIR=cmsis_nn TARGET_ARCH=project_generation"`.
2. **플랫폼 훅 세 개 구현**: `debug_log.cc`(`DebugLog`, `DebugVsnprintf`), `micro_time.cc`(`ticks_per_second`, `GetCurrentTimeTicks`), `system_setup.cc`(`InitializeTarget`). 링크할 때 이 함수들의 정의만 있으면 어디에 둬도 된다.
3. **hello_world를 UART로** 돌려 본다 — 이 예제는 수정 없이 돌아야 한다.
4. 예제를 fork 해서 센서를 붙인다.
5. 최적화 커널을 넣는다.

Cortex-M 범용 구현(`cortex_m_generic/`)은 로그를 콜백으로 넘긴다: `RegisterDebugLogCallback(DebugLogCallback cb)`(`debug_log_callback.h`), 콜백 타입은 `void (*)(const char* s)`. 앱이 UART 송신 함수를 등록하면 `MicroPrintf`가 거기로 간다.

```cpp
// 보드 쪽 글루 코드 예 (컴파일 안 함 — 보드 HAL 함수 이름은 가상)
#include "tensorflow/lite/micro/cortex_m_generic/debug_log_callback.h"
static void uart_log(const char* s) { board_uart_write(s); }   // 블로킹 UART TX

int main(void) {
  board_init_clocks_and_uart();
  RegisterDebugLogCallback(uart_log);     // 첫 MicroPrintf 전에
  app_tflm_setup();                       // GetModel ... AllocateTensors
  for (;;) { wait_for_frame(); app_tflm_run_one_frame(); }
}
```

### 9.2 링커 스크립트로 모델과 arena 배치 (F7, D2 연결)

```text
/* GNU ld 링커 스크립트 발췌 (컴파일 안 함 — 메모리 이름·크기는 예시) */
MEMORY {
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
  DTCM  (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
  SRAM  (rw)  : ORIGIN = 0x24000000, LENGTH = 512K
}
SECTIONS {
  .model_data (READONLY) : ALIGN(16) { KEEP(*(.model_data)) } > FLASH
  .tensor_arena (NOLOAD) : ALIGN(16) { *(.tensor_arena) }     > DTCM
}
```

```cpp
// 앱 쪽 (컴파일 안 함 — GCC 속성 문법)
alignas(16) const unsigned char g_kws_model[]
    __attribute__((section(".model_data"))) = { /* xxd 출력 */ };
alignas(16) static uint8_t g_arena[kArenaSize] __attribute__((section(".tensor_arena")));
```

- 모델은 flash의 전용 섹션에 두면 OTA로 모델만 교체하기 쉽다(J5 — 모델 영역을 A/B 슬롯으로).
- arena는 가장 빠른 RAM(DTCM 등)에 두는 것이 보통이다. 활성값은 매 op 읽고 쓰기 때문이다. `NOLOAD`라 부팅 때 0으로 채우는 비용도 없다(TFLM은 arena 초기값에 의존하지 않는다).
- 가중치가 flash wait state에 막히면 핫 레이어 가중치만 RAM으로 옮기는 선택지가 있다 — D2 1.3절의 XIP vs RAM 복사 trade-off.
- DMA가 arena나 입력 버퍼를 건드리면 캐시 일관성(Cortex-M7의 D-cache)을 챙겨야 한다(E7).

### 9.3 생태계에서의 통합 (출발점)

- **Zephyr**: tflite-micro가 Zephyr 모듈로 들어가 있고 `samples/modules/tflite-micro/` 아래에 hello_world 등 샘플이 있다. 로그는 Zephyr `printk`로 붙인다.
- **Arduino**: TensorFlow 팀의 `tflite-micro-arduino-examples` 저장소가 Arduino 라이브러리 형태로 TFLM을 제공한다(Nano 33 BLE Sense 예제가 유명하다 — TinyML 책의 실습 보드).
- **벤더 SDK**: Espressif `esp-tflite-micro`(ESP-IDF 컴포넌트, ESP-NN 최적화 커널), NXP eIQ(MCUXpresso SDK에 TFLM 포함) 등 벤더가 자기 최적화 커널을 끼운 TFLM 패키지를 제공하는 경우가 많다. 이 경우 **벤더가 고정한 TFLM 버전**이 변환기 버전과 맞는지가 첫 확인 사항이다(F8).

### 9.4 예제 12 — 바이너리에 정말 malloc이 없나

무엇을 확인하나: "TFLM은 동적 할당을 안 한다"를 링크된 바이너리로 확인.

```sh
nm -u kws_ref | tr '\n' ' '
nm -u kws_ref | grep -c -E "malloc|_Znwm|free|calloc"
```

```text
___assert_rtn ___cxa_atexit ___stack_chk_fail ___stack_chk_guard ___stderrp ___stdoutp _abort _bzero _clock _expf _fflush _frexp _memcpy _memset _printf _strcmp _strncmp _strtoul _vfprintf 
0
```

출력에서 볼 것: 외부에서 가져오는 함수 중 `malloc`/`free`/`calloc`/`operator new`(`_Znwm`)가 **0개**다. TFLM 라이브러리 전체와 앱을 링크해도 동적 할당 심볼이 없다. 남은 것은 `memcpy`/`memset`, 수학 함수(`expf` — softmax, `frexp` — multiplier 계산), 출력(`vfprintf` — 호스트 `DebugLog`, `printf` — 앱), `clock`(호스트 타이머)이다. 보드에서는 이 목록이 곧 "libc에서 제공해야 하는 것"이다. 이 검사를 CI에 넣으면 누군가 실수로 `std::vector`를 넣는 순간 잡힌다(D2 6.4절의 `--wrap=malloc` 아이디어와 같은 목적).

`___cxa_atexit`은 앱의 함수 지역 `static` 객체(`static MicroInterpreter m(...)`) 때문이다. 소멸자가 있는 static 객체는 컴파일러가 종료 시 소멸자 호출을 `atexit`에 등록한다. bare-metal에서는 `main`이 끝나지 않으니 무해하지만, 링크 시 `__cxa_atexit` 스텁을 요구할 수 있다. 원치 않으면 전역 저장소에 placement new로 만들거나 `-fno-use-cxa-atexit`류 옵션을 검토한다. `-fno-threadsafe-statics`는 static 초기화에 guard 락 코드를 넣지 않게 한다(TFLM Makefile 기본 플래그).

### 9.5 체크리스트 — "Python에선 되는데 보드에선 안 된다"

| 순서 | 확인 | 방법 |
|---|---|---|
| 1 | 같은 모델 파일인가 | 보드 로그로 `g_model_len`·해시 출력, 빌드 산출물과 비교 |
| 2 | golden이 올바른가 | Python에서 `BUILTIN_REF`로 만든 golden인가 (XNNPACK 아님, 3.2절) |
| 3 | 호스트 TFLM에서 맞나 | 같은 앱 코드를 호스트 빌드로 돌려 golden과 비교 (이 노트의 kws_ref) — 여기서 틀리면 런타임·모델 문제, 맞으면 보드 문제 |
| 4 | 입력 전처리가 같나 | 같은 raw 센서 입력 → 보드 MFCC/정규화 결과를 덤프해 Python과 비교 (layout HWC, 채널 순서, 윈도, 스케일) |
| 5 | 입력 양자화가 맞나 | `input->params.scale`·`zero_point`를 런타임에 읽어 쓰나, round·clamp를 하나 |
| 6 | AllocateTensors 반환값 | 실패를 무시하고 Invoke 하고 있지 않나, 로그가 보이게 DebugLog를 연결했나 |
| 7 | 메모리 오염 | arena·모델 정렬(16), arena가 스택·DMA 버퍼와 겹치지 않나, 스택 오버플로(D2 6.3 stack painting) |
| 8 | 커널 차이 | 보드는 CMSIS-NN, 호스트는 reference일 수 있다 — 호스트에서도 `OPTIMIZED_KERNEL_DIR=cmsis_nn`으로 빌드해 비교(예제 11) |
| 9 | 버전 | 변환기(TF) 버전이 만든 op 버전·옵션을 보드의 TFLM 버전이 아나 |
| 10 | 레이어별 비교 | 그래도 다르면 중간 텐서를 덤프해 레이어별 SQNR (C8) |

이 순서의 핵심은 **3번 — 호스트 TFLM 빌드** 다. 보드와 Python 사이에 "같은 C++ 런타임을 호스트에서 돌린 결과"를 끼워 넣으면 문제를 반으로 쪼갤 수 있다. Don이 SSD에서 "시뮬레이터 → FPGA → 실리콘"으로 버그 위치를 이분 탐색하던 것과 같다.

---

## 10. 임베디드 관점에서 다시 보기

### 10.1 추론을 펌웨어 구조에 넣는 모양 (J1, J2)

```
 마이크 I2S DMA ──(half/full IRQ)──► 링버퍼 ──► 특징 추출 태스크 (MFCC 1프레임/10 ms)
                                                       │ 49프레임 창이 차면 (stride마다)
                                                       ▼
                                              추론 태스크 (낮은 우선순위, 1회 ~수 ms)
                                              memcpy → input(0)  /  Invoke()  /  output(0) → 후처리
                                                       │
                                                       ▼
                                              이벤트 큐 → 앱 (wake word 감지 → SoC 깨우기)
```

- `Invoke()`는 중간에 멈출 수 없는 긴 함수 호출이다. ISR에서 부르면 안 되고, 우선순위가 낮은 태스크에서 부른다. WCET는 프로파일러로 잰 최악값 + 여유로 잡고, 센서 프레임 주기 안에 들어오는지 확인한다(J2, D6).
- interpreter 하나는 **재진입 불가**다. 두 태스크가 같은 interpreter를 부르면 안 된다. 모델 두 개면 interpreter 두 개, arena는 동시에 돌지 않을 때만 공유.
- watchdog: `Invoke()`가 수십 ms 걸리는 모델이면 그 사이 watchdog을 어떻게 다룰지 정한다.
- 입력은 **복사**한다(`memcpy` → `input(0)`). DMA가 링버퍼에 쓰는 동안 추론이 같은 메모리를 읽지 않게 한다 — double buffering(J1).

### 10.2 숫자로 보는 이 노트 모델의 예산

| 항목 | 값 (실측/계산) | 어디에 |
|---|---|---|
| 모델 파일 | 6,736 B | flash `.rodata` |
| 그중 가중치+bias | 1,304 B | (모델 파일 안) |
| arena (reference) | 8,464 B = head 6,000 + tail 2,464 | SRAM/DTCM |
| arena (CMSIS-NN, 호스트) | 8,544 B | SRAM/DTCM |
| 코드 (op 6개, 호스트 arm64) | 75.5 KB (reference) / 121.4 KB (CMSIS-NN) | flash `.text` — MCU에서 다시 잼 |
| MicroProfiler (32-bit) | 약 80 KB | SRAM — 디버그 빌드에서만 |
| 연산량 | 약 112k MAC / 추론 | — |

이 표가 D2 8절 "메모리 예산 워크시트"의 TFLM 버전이다. 모델이 작을수록 tail·코드·프로파일러 같은 **고정 비용**이 지배한다는 것이 이 노트의 핵심 관찰이다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 모델 배열에 `const` 빠짐 | SRAM이 모델 크기만큼 사라짐, 부팅이 느려짐 | `.data`로 들어가 flash→RAM 복사 | `const` + 필요하면 전용 섹션 |
| 모델·arena 정렬 안 함 | M0+에서 HardFault, 다른 코어에서 이상하게 느림 | flatbuffer 필드를 unaligned로 읽음 | `alignas(16)` 또는 링커 `ALIGN(16)` |
| arena 작게 잡음 | "Failed to resize buffer. Requested: X, available Y, missing: Z" | head(활성값 peak) 부족 | 메시지의 missing만큼 + margin, 또는 `arena_used_bytes()` 측정 |
| op 등록 누락 | "Didn't find op for builtin opcode 'SOFTMAX'" | resolver에 `AddSoftmax()` 없음 | 모델 op 목록과 resolver 대조, CI에 호스트 AllocateTensors 테스트 |
| resolver 템플릿 인자 작음 | "Couldn't register builtin op #N, resolver size is too small" | `MicroMutableOpResolver<N>`의 N < Add 호출 수 | N을 늘림 |
| golden을 XNNPACK으로 만듦 | 보드 출력이 1~5 LSB 다름, "보드 버그" 오진 | 커널 계열이 다름 | `OpResolverType.BUILTIN_REF`로 golden |
| 입력 scale 하드코딩 | 모델 재학습 후 정확도 급락 | calibration이 바뀌어 scale·zp 변경 | `input->params`에서 런타임에 읽기 |
| resolver를 지역 변수로 | 간헐적 크래시, 첫 Invoke에서 쓰레기 | interpreter가 사라진 객체를 참조 | model·resolver·arena를 interpreter보다 오래 살게 (static) |
| 프로파일러를 양산 빌드에 넣음 | SRAM 80 KB 증발, 링크 실패 | `kMaxEvents` = 4096 배열 | 디버그 빌드만, 또는 작은 커스텀 프로파일러 |
| 오래된 튜토리얼 API 사용 | `all_ops_resolver.h` not found | `AllOpsResolver` 제거됨 | `MicroMutableOpResolver<N>` + 필요한 `Add*` |
| 라이브러리·앱 빌드 플래그 불일치 | 원인 모를 크래시, 텐서 필드가 이상한 값 | `TF_LITE_STATIC_MEMORY` 등 정의가 달라 구조체 레이아웃 불일치 | 라이브러리 플래그를 앱에도 동일하게 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "How do you size the tensor arena?"

**A.** 설계 단계에서는 변환된 `.tflite`의 활성값 크기와 수명으로 peak-live를 계산하고, 런타임 객체(tail) 몫으로 몇 KB를 더한다. 그다음 arena를 넉넉히 잡고 빌드해서 `AllocateTensors()` 뒤 `arena_used_bytes()`로 실제 사용량을 읽고, 정렬과 margin을 붙여 확정한다. 직접 해 본 작은 KWS 모델에서 계산한 peak 6,000 B가 TFLM이 보고한 head와 정확히 같았고, tail이 2.4 KB로 전체의 30% 가까이 됐다. 그래서 작은 모델일수록 측정이 필수다. 커널 라이브러리나 타깃이 바뀌면 다시 재고, CI에서 숫자를 추적한다.

> At design time I compute peak live activation bytes from the converted .tflite — tensor sizes and lifetimes in execution order — and add a few kilobytes for the runtime's persistent structures. Then I build with a generous arena, call AllocateTensors, read arena_used_bytes, and set the final size with alignment and a margin. On a small keyword-spotting model I tried, my estimate of 6,000 bytes matched the head section TFLM reported exactly, but the persistent tail was another 2.4 KB — almost 30 percent — so for small models measurement is essential. I re-measure whenever the kernel library or target changes and track the number in CI.

**Q.** "Why doesn't TFLM use malloc?"

**A.** MCU 펌웨어에서 동적 할당은 단편화, 비결정적 지연, 런타임에 늦게 드러나는 out-of-memory를 낳는다. TFLM은 앱이 준 arena 하나에서 모든 것을 해결하고, 모든 배치를 `AllocateTensors()`에서 끝내서 `Invoke()`는 할당을 전혀 하지 않는다. 그래서 메모리 사용이 부팅 때 확정되고, 실패도 부팅 때 드러나고, 링커 맵만 보면 RAM 예산이 맞는지 안다. 실제로 링크된 바이너리에 malloc/free 심볼이 하나도 없는 것을 확인했다.

> Dynamic allocation on a microcontroller brings fragmentation, non-deterministic timing and out-of-memory failures that show up late in the field. TFLM takes one arena from the application and does all planning inside AllocateTensors, so Invoke never allocates. Memory use is fixed at boot, failures surface at boot, and the linker map tells you whether the RAM budget closes. I checked a linked binary and it imports no malloc or free at all.

**Q.** "What does MicroMutableOpResolver buy you?"

**A.** 필요한 커널만 등록하는 고정 크기 테이블이다. 등록하지 않은 커널은 아무도 참조하지 않아 링커가 버리므로 flash가 준다. 같은 모델로 op 6개만 등록했을 때와 88개를 등록했을 때 코드 크기가 75 KB 대 515 KB였다. 또 타입 특화 registration(예: `Register_CONV_2D_INT8`)을 넘기면 더 줄일 수 있다. 대가는 모델의 op가 바뀌면 resolver도 바꿔야 한다는 것이고, 빠뜨리면 부팅 때 "Didn't find op" 에러가 난다 — 그래서 CI에서 잡는다.

> It is a fixed-size table where I register only the kernels the model actually uses. Kernels that are never registered are never referenced, so the linker drops them and flash shrinks. With the same model I measured about 75 KB of code with six ops registered versus 515 KB with eighty-eight. You can go further with type-specialized registrations like the int8-only conv. The cost is that the resolver must track the model's op list — a missing op fails at AllocateTensors with "Didn't find op", which I catch in a host-side CI test.

**Q.** "How does CMSIS-NN plug into TFLM?"

**A.** 빌드 시점의 파일 교체다. `OPTIMIZED_KERNEL_DIR=cmsis_nn`으로 빌드하면 Makefile이 `kernels/cmsis_nn/`에 같은 이름의 파일(conv.cc, depthwise_conv.cc, fully_connected.cc, pooling.cc, softmax.cc 등)이 있는 커널을 그 파일로 바꿔 컴파일한다. 앱 코드와 resolver 호출은 그대로다. 그 커널들은 Prepare에서 CMSIS-NN이 필요한 scratch를 arena에 요청하고, int8 Eval에서 `arm_convolve_wrapper_s8` 같은 함수를 부른다. float 경로는 reference로 떨어진다. 바꾼 뒤엔 golden으로 bit-exact를 확인하고 arena를 다시 잰다 — 직접 해 보니 출력은 bit-exact, arena는 80 B 늘었다.

> It's a build-time substitution. With OPTIMIZED_KERNEL_DIR=cmsis_nn, the makefile swaps in any kernel source that has a same-named file under kernels/cmsis_nn — conv, depthwise conv, fully connected, pooling, softmax and so on. Application code and resolver calls stay the same. Those kernels request CMSIS-NN scratch buffers from the arena in Prepare and call functions like arm_convolve_wrapper_s8 for int8; float falls back to the reference path. After switching I re-run the golden tests and re-measure the arena — in my run the outputs stayed bit-exact and the arena grew by 80 bytes.

**Q.** "The model works in Python but not on the device. How do you debug it?"

**A.** 먼저 golden이 맞는지 본다 — Python 기본 interpreter는 XNNPACK을 붙여서 reference 커널과 1~5 LSB 다를 수 있으니 reference resolver로 만든다. 다음으로 같은 앱 코드를 호스트에서 TFLM으로 돌려 golden과 비교한다. 호스트에서 맞으면 문제는 보드 쪽 — 입력 전처리, 입력 양자화, 정렬, 메모리 오염, 다른 커널 라이브러리 — 이고, 틀리면 모델·런타임 버전 쪽이다. 그다음 입력 텐서 덤프 비교, 마지막으로 레이어별 중간 텐서 비교로 좁힌다. 펌웨어에서 시뮬레이터와 실리콘 사이를 이분 탐색하는 것과 같다.

> First I make sure the golden is right: the default Python interpreter applies XNNPACK, which can differ by a few LSBs from the reference kernels, so I generate goldens with the reference resolver. Then I run the exact application code against TFLM on the host. If the host matches, the problem is on the board side — preprocessing, input quantization, alignment, memory corruption, or a different kernel library — and if it doesn't, it's the model or a version mismatch. From there I compare the input tensor dumps and finally per-layer intermediates. It's the same bisection I'd do between a simulator and silicon.

**Q.** "How would you get an Ethos-U NPU into this flow?"

**A.** int8 `.tflite`를 Vela로 다시 컴파일하면 NPU가 할 수 있는 구간이 `ethos-u` custom op 하나로 묶인다. TFLM은 `CO_PROCESSOR=ethos_u`로 빌드하고 resolver에 `AddEthosU()`를 넣으면, 그 custom op가 Ethos-U 드라이버를 불러 command stream을 실행한다. 남은 CPU op는 CMSIS-NN으로 돈다. 확인할 것은 Vela 리포트의 CPU fallback op, NPU가 쓰는 arena·scratch 크기, 그리고 NPU 결과의 golden 비교다.

> I compile the int8 .tflite with Vela, which packs the NPU-supported subgraphs into an ethos-u custom operator. TFLM is built with CO_PROCESSOR=ethos_u and the resolver registers AddEthosU, so that op hands its command stream to the Ethos-U driver, while the remaining CPU ops run on CMSIS-NN kernels. I then check Vela's report for CPU fallbacks, re-measure the arena, and compare outputs against goldens.

---

## 13. 직접 해보기

1. **손계산**: 5.3절 모델에서 1×1 conv의 출력 채널을 32 → 48로 늘리면 head(활성값 peak)는 몇 바이트가 되나? 어느 op에서 peak가 나나?
   정답: 1×1 conv 순간 2,000 + 25×5×48 = 2,000 + 6,000 = 8,000 B. 그다음 pool 순간은 6,000 + 48 = 6,048 B라 peak는 여전히 1×1 conv.
2. **손계산**: 32-bit MCU에서 `MicroProfiler`(kMaxEvents = 4096)의 멤버 배열 크기는? kMaxEvents를 256으로 줄이면?
   정답: 4096 × (4 + 4 + 4 + 8) = 81,920 B. 256이면 256 × 20 = 5,120 B.
3. **코드**: est.py를 고쳐서, 텐서 수명이 겹치지 않는 경우 같은 주소를 쓰는 간단한 first-fit planner로 각 텐서의 offset을 출력해 보라. head 크기가 6,000 B가 되는지 확인하라.
   힌트: 텐서를 크기 내림차순으로 정렬하고, 이미 배치된 것 중 수명이 겹치는 텐서들과 주소 구간이 안 겹치는 가장 낮은 offset을 고른다 — `GreedyMemoryPlanner`가 하는 일과 같다.
4. **코드**: `main.cc`의 resolver에서 `AddConv2D()`를 `AddConv2D(tflite::Register_CONV_2D_INT8())`로 바꾸고(`kernels/conv.h` include), reference 빌드와 CMSIS-NN 빌드에서 각각 `__text` 크기와 출력이 어떻게 변하는지 재 보라.
   힌트: reference 빌드에서는 `Register_CONV_2D_INT8()`이 `Register_CONV_2D()`를 그대로 반환하는 inline 함수다(`conv.h` 128행) — 차이가 날지 예측부터 해 보라.
5. **보드 과제 (PJ1/PJ2)**: Cortex-M4F 보드에서 이 모델을 reference와 CMSIS-NN으로 각각 빌드하고, `cortex_m_generic/micro_time.cc`의 DWT 카운터로 op별 사이클을 재라. 112k MAC으로 나눠 "MAC당 사이클"을 계산하고, 호스트 비율(약 8배)과 비교하라.
   힌트: `cortex_m_generic/micro_time.cc`의 `ticks_per_second()`는 0을 반환한다. tick은 DWT 사이클 수 그대로이니 µs로 바꿀 때는 코어 클럭으로 직접 나눈다.
6. **실패 재현**: `alignas(16)`을 빼고 모델 배열 앞에 `const char pad[1] = {0};`를 둔 뒤, 호스트에서는 왜 여전히 돌아가는지, Cortex-M0+에서는 무엇이 달라질지 설명하라.
   정답: AArch64·Cortex-M4 이상은 일반 load의 unaligned 접근을 하드웨어가 처리한다(느릴 수 있음). M0/M0+(Armv6-M)는 unaligned 접근 시 HardFault가 난다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| TFLM (TensorFlow Lite Micro, LiteRT for Microcontrollers) | MCU용 TFLite 인터프리터 | 같은 `.tflite`를 동적 할당 없이 실행 |
| flatbuffer | 직렬화 포맷 | 파싱 없이 offset을 따라가며 바로 읽는 바이너리. `.tflite`의 포맷 |
| `TFL3` | file identifier | `.tflite` 파일 4~7번째 바이트 |
| tensor arena | 앱이 주는 정적 메모리 블록 | 활성값·scratch·런타임 객체가 전부 여기에 |
| head / tail | arena의 두 끝 | head = planner가 배치한 비영속 버퍼, tail = 영속 할당 |
| memory planner | 텐서 offset을 정하는 알고리즘 | 기본 `GreedyMemoryPlanner`, 수명이 안 겹치면 주소 공유 |
| `arena_used_bytes()` | 실제 사용 arena 바이트 | `AllocateTensors()` 후 호출, arena 크기 확정에 사용 |
| op resolver | op 코드 → 커널 매핑 테이블 | `MicroMutableOpResolver<N>`, 등록한 것만 링크 |
| `TFLMRegistration` | 커널 함수 묶음 | init / prepare / invoke 등 함수 포인터 |
| reference kernel | 기준 구현 | 읽기 쉬운 C++, 모든 타깃에서 동작, 느림 |
| CMSIS-NN | Arm의 Cortex-M용 NN 커널 라이브러리 | int8/int16 conv·FC·pool·softmax 등, DSP/MVE 최적화 |
| `OPTIMIZED_KERNEL_DIR` | make 변수 | 같은 이름 커널 파일을 덮어쓸 디렉터리 (cmsis_nn, xtensa ...) |
| `CO_PROCESSOR` | make 변수 | 코프로세서 커널 디렉터리 (ethos_u), 최우선 |
| Ethos-U / Vela | Arm microNPU / 그 컴파일러 | Vela가 NPU 구간을 `ethos-u` custom op로 묶음 |
| scratch buffer | 커널 임시 버퍼 | `RequestScratchBufferInArena`로 Prepare에서 요청, head에 배치 |
| `MicroProfiler` | op별 tick 기록기 | `LogTicksPerTagCsv()`, kMaxEvents 4096 |
| DWT CYCCNT | Cortex-M 사이클 카운터 | `cortex_m_generic/micro_time.cc`가 tick 소스로 사용 |
| `MicroPrintf` / `DebugLog` | 로그 API / 플랫폼 훅 | 보드에서 UART 등으로 연결 |
| `TF_LITE_STATIC_MEMORY` | 빌드 정의 | 동적 할당용 필드를 뺀 작은 텐서 구조체 |
| `TF_LITE_STRIP_ERROR_STRINGS` | 빌드 정의 | 로그 문자열·프로파일링 제거 (릴리스용) |
| BUILTIN_REF | tf.lite resolver 종류 | Python에서 reference 커널로 실행 — golden용 |
| bit-exact | 비트 단위 일치 | 같은 int8 입력에 출력 바이트가 완전히 같음 |

---

## 15. 요약 & 체크리스트

TFLM은 `.tflite` flatbuffer를 flash에 그대로 두고 포인터로 읽으며(`GetModel`), 앱이 등록한 커널만(`MicroMutableOpResolver<N>`) 링크하고, 앱이 준 정적 arena 하나 안에서 `AllocateTensors()` 때 모든 메모리를 계획한 뒤 `Invoke()`에서는 할당 없이 op를 순서대로 실행하는 MCU용 인터프리터다. arena는 head(활성값 peak — `.tflite`의 liveness로 손계산 가능, 이 노트에서 6,000 B로 정확히 일치)와 tail(런타임 객체 — 추정이 어려워 `arena_used_bytes()`로 측정)로 나뉜다. 최적화 커널은 `OPTIMIZED_KERNEL_DIR=cmsis_nn`, `CO_PROCESSOR=ethos_u`로 같은 이름의 커널 파일을 빌드 시 바꿔 끼우는 방식이고, 이 노트의 호스트 실험에서 reference·CMSIS-NN 모두 `tf.lite` reference 커널과 bit-exact였다(Python 기본 XNNPACK 경로와는 5 LSB까지 달랐다). 보드 포팅은 `DebugLog`·`GetCurrentTimeTicks`·`InitializeTarget` 세 훅, 링커 스크립트 배치, 그리고 "호스트 TFLM 빌드를 중간 기준으로 쓰는 이분 탐색"이 핵심이다.

- [ ] `.tflite`를 `xxd -i`로 바꾸고 `const`, `alignas(16)`, `extern` 선언을 붙여야 하는 이유를 각각 말할 수 있다
- [ ] `GetModel` → resolver `Add*` → `MicroInterpreter` → `AllocateTensors` → `input(0)` → `Invoke` → `output(0)`을 보지 않고 쓸 수 있다
- [ ] `AllocateTensors()` 안의 단계(op 조회, Init, Prepare, 메모리 계획)와 각 단계에서 나는 에러를 연결할 수 있다
- [ ] 순차 CNN의 활성값 peak를 손으로 계산하고, arena head와 tail의 차이를 설명할 수 있다
- [ ] "Failed to resize buffer. Requested/available/missing" 메시지를 읽고 arena 크기를 고칠 수 있다
- [ ] `MicroMutableOpResolver`가 flash를 줄이는 원리(등록 = 링크)를 숫자 예와 함께 설명할 수 있다
- [ ] `OPTIMIZED_KERNEL_DIR`·`CO_PROCESSOR`·`specialize_files.py`의 파일 교체 규칙을 설명할 수 있다
- [ ] CMSIS-NN이 맡는 커널과 float 경로가 reference로 떨어지는 이유를 말할 수 있다
- [ ] golden을 `BUILTIN_REF`로 만들어야 하는 이유를 실제 LSB 차이 예로 설명할 수 있다
- [ ] 보드 포팅의 세 훅과 MicroProfiler의 SRAM 비용, "Python에선 되는데 보드에선 안 된다" 체크리스트를 말할 수 있다

## 참고 자료

- TFLM 저장소: [github.com/tensorflow/tflite-micro](https://github.com/tensorflow/tflite-micro) — 이 노트는 commit `06f0b46` 기준. 특히 `tensorflow/lite/micro/docs/`의 `memory_management.md`, `new_platform_support.md`, `porting_reference_ops.md`, `profiling.md`, `offline_memory_plan.md`, `optimized_kernel_implementations.md`
- `tensorflow/lite/micro/kernels/cmsis_nn/README.md`, `tensorflow/lite/micro/kernels/ethos_u/README.md` (같은 저장소)
- CMSIS-NN: [github.com/ARM-software/CMSIS-NN](https://github.com/ARM-software/CMSIS-NN)
- Ethos-U Vela: [gitlab.arm.com/artificial-intelligence/ethos-u/ethos-u-vela](https://gitlab.arm.com/artificial-intelligence/ethos-u/ethos-u-vela)
- Pete Warden, Daniel Situnayake, "TinyML" (O'Reilly, 2019) — 개념은 유효하지만 API(`AllOpsResolver`, `MicroErrorReporter`)는 옛 버전
- R. David et al., "TensorFlow Lite Micro: Embedded Machine Learning on TinyML Systems", MLSys 2021 — TFLM 설계 논문
- 이 노트 세트: C1(requantization), C8(검증), D2(메모리 추정·배치), E2(CMSIS-NN·Helium), E5(NPU·Vela), F1(변환기), F7(링커 배치), J1·J6(통합·golden 테스트)
