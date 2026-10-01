# I5. 벤치마크와 지표 — MLPerf, TOPS/W의 함정, 우리 제품용 벤치마크 만들기

> **이 노트를 다 읽으면**: MLPerf Tiny·Mobile·Inference·Client가 각각 무엇을 어떤 규칙으로 재는지 설명할 수 있다 · 벤더의 "10 TOPS"를 우리 모델의 ms로 바꾸고 무엇을 물어봐야 하는지 안다 · 웨어러블용 벤치마크 스위트(워크로드·지표·시나리오·측정법·보고)를 설계하고 Python 하니스로 torch·ORT·Core ML·llama.cpp를 한 표에 모을 수 있다 · 가중 점수와 Pareto로 결정을 내리되 가중치 민감도와 측정 잡음을 숫자로 보여 줄 수 있다
> **JD 연결**: "**Evaluate and select silicon platforms** (GPUs, NPUs etc.)", "Co-design model architectures that meet latency, memory, power, bandwidth", "Work with platform vendors to bring up toolchains, SDKs and new accelerator" — study_prep_list **I5** 행: MLPerf Tiny, MLPerf Mobile · TOPS/W의 함정, sustained vs peak · HW 선정·보고 (연결: M1 평가 기준표, M2 벤치마크 방법론, M4 데이터시트 해석, K4 열·지속 성능, O3 ML CI)
> **Don 기준 난이도**: 측정·검증 방법론, 스펙 margin 검증, 양산 전 sign-off는 이미 강하다 / 새로 배울 것은 ML 업계 벤치마크의 규칙(MLPerf 시나리오·division·정확도 목표), 정확도까지 포함한 다차원 지표, 점수화와 그 함정
> **선행 노트**: D3(roofline), D6(벤치마크 하니스·tail latency), D7(TOPS/W 해석·에너지), C7(latency LUT), E6(GPU 측정), F3(`llama-bench`), F6(Core ML compute unit), F8(성능 문제·환경 고정), I1(예산 설정)

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어 시절을 떠올리자. 새 NAND나 새 컨트롤러가 들어오면 Don은 "스펙상 3.2 GB/s"를 믿지 않고 FIO로 4K random read QoS, sustained write(쓰기 캐시가 다 찬 뒤), 온도별 throttling 곡선을 직접 쟀다. 그리고 그 결과를 고객사·경영진에게 표 한 장으로 보고했다. edge ML에서 벤치마크도 똑같은 일이다. 다만 재는 대상이 "모델 × 런타임 × 실리콘" 조합이고, 숫자에 **정확도**라는 축이 하나 더 붙는다.

벤치마크가 쓰이는 곳은 크게 세 군데다.

- **선택지 비교**: 칩 A vs 칩 B, TFLite vs QNN vs ONNX Runtime, int8 vs fp16. 예를 들어 Hark 같은 웨어러블이라면 "음성 encoder를 Hexagon NPU에 둘지 CPU에 둘지"가 이 질문이다(M1·M2).
- **회귀 추적**: 모델·컴파일러·펌웨어가 바뀔 때마다 같은 스위트를 돌려 "지난 릴리스보다 p99가 15 % 나빠졌다"를 자동으로 잡는다(O3).
- **소통**: 벤더에게 "우리 워크로드로 이 숫자를 보여 달라"고 요구하고, 경영진에게 "이 칩이면 배터리 하루, 저 칩이면 14시간"을 한 장으로 설명한다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="30" width="140" height="44" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="80" y="57" font-size="13" text-anchor="middle">실리콘 후보</text><rect x="10" y="100" width="140" height="44" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="80" y="127" font-size="13" text-anchor="middle">런타임·컴파일러</text>
<rect x="10" y="170" width="140" height="44" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="80" y="197" font-size="13" text-anchor="middle">모델·정밀도</text><line x1="150" y1="52" x2="200" y2="110" stroke="currentColor"/><line x1="150" y1="122" x2="200" y2="122" stroke="currentColor"/>
<line x1="150" y1="192" x2="200" y2="134" stroke="currentColor"/><rect x="200" y="80" width="150" height="84" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="275" y="108" font-size="13" text-anchor="middle">벤치마크 스위트</text><text x="275" y="128" font-size="12" text-anchor="middle">워크로드 × 시나리오</text>
<text x="275" y="146" font-size="12" text-anchor="middle">같은 조건·같은 하니스</text><line x1="350" y1="122" x2="390" y2="122" stroke="currentColor"/><polygon points="390,117 400,122 390,127" fill="currentColor"/><rect x="400" y="92" width="110" height="60" rx="6" fill="none" stroke="#888" stroke-width="2"/>
<text x="455" y="117" font-size="12" text-anchor="middle">원시 데이터</text><text x="455" y="135" font-size="12" text-anchor="middle">+ 환경 지문</text><line x1="510" y1="110" x2="540" y2="52" stroke="currentColor"/><line x1="510" y1="122" x2="540" y2="122" stroke="currentColor"/>
<line x1="510" y1="134" x2="540" y2="192" stroke="currentColor"/><rect x="540" y="30" width="130" height="44" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="605" y="57" font-size="13" text-anchor="middle">HW·런타임 선정</text><rect x="540" y="100" width="130" height="44" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<text x="605" y="127" font-size="13" text-anchor="middle">회귀 차단 (CI)</text><rect x="540" y="170" width="130" height="44" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="605" y="197" font-size="13" text-anchor="middle">벤더·경영진 보고</text><text x="340" y="240" font-size="12" text-anchor="middle">벤치마크 = 같은 질문을 같은 방식으로 반복 가능하게 묻는 장치</text>
</svg>
```

그림 1 — 벤치마크의 자리. 후보(왼쪽)를 같은 스위트·같은 하니스로 재서 원시 데이터와 환경 지문을 남기고, 그 데이터가 세 가지 결정(오른쪽)에 쓰인다.

이 노트에서 다른 노트와 겹치는 부분은 짧게 되짚고 ID로 가리킨다.

| 이미 다룬 것 | 노트 | 이 노트에서의 역할 |
|---|---|---|
| roofline, arithmetic intensity | D3 | TOPS를 실제 처리량으로 바꿀 때 memory-bound 여부 판정 |
| warm-up·분위수·스레드 고정 하니스 | D6 | 그 하니스를 **여러 backend·여러 지표**로 확장 |
| TOPS/W → pJ/op 환산 | D7 | 벤더 숫자 벗기기의 에너지 쪽 |
| latency LUT | C7 | op 단위 숫자 vs 이 노트의 end-to-end 숫자 |
| `llama-bench` pp/tg | F3 | 스위트의 LLM 한 행 |
| Core ML compute unit | F6 | backend 후보 두 개 |
| 환경 고정·버전 문제 | F8 | 재현성 절의 환경 지문 |

이 노트의 새 내용은 네 가지다: **업계 벤치마크(MLPerf) 규칙**, **벤더 숫자 해부**, **우리 제품용 스위트 설계와 구현**, **점수화·보고**.

> 측정 환경 주의: 이 노트의 모든 실측은 Apple M2 MacBook(8코어, 배터리 전원)에서 다른 작업이 병렬로 크게 돌던 상태에서 했다. load average가 7에서 100 이상까지 흔들렸다. 그래서 절대 숫자보다 **비율, 경향, 그리고 "잡음이 결론을 어떻게 흔드는가"** 자체를 교재로 쓴다. 숨기지 않고 그대로 보여 주는 것도 벤치마크 보고의 일부다.

---

## 1. 벤치마크의 기본 어휘

### 1.1 직관 — "무엇을, 어떤 조건에서, 어떻게 요약했나"

벤치마크 숫자 하나에는 항상 세 개의 숨은 질문이 붙어 있다.

1. **무엇을** 쟀나 — 커널 하나? 모델 하나? 전처리부터 후처리까지(end-to-end)? 기기 전체 전력까지?
2. **어떤 조건에서** — batch 크기, 정밀도, 클럭·온도 상태, 동시에 돌던 다른 일, 입력 데이터.
3. **어떻게 요약했나** — 평균? 중앙값? p99? 최고 기록(best-of-N)?

SSD로 말하면 "3.2 GB/s"가 sequential read인지, queue depth 32인지, 쓰기 캐시 안인지가 숫자보다 중요한 것과 같다.

### 1.2 측정 단위(granularity) 네 층

| 층 | 예 | 장점 | 한계 |
|---|---|---|---|
| micro / 커널 | conv 3×3 하나, GEMM 하나 | 원인 분석, 컴파일러 튜닝 | 실제 모델의 메모리 이동·fusion을 못 봄 |
| 모델 | DS-CNN 추론 1회 | 비교가 쉬움, MLPerf가 주로 이 층 | 전처리·DMA·드라이버가 빠짐 |
| end-to-end 파이프라인 | 마이크 DMA → MFCC → KWS → 후처리 | 사용자가 느끼는 지연 | 원인 분리가 어려움 |
| 시스템 | 하루 시나리오의 배터리 소모 | 제품 결정에 직결 | 비싸고 느림, 반복성 낮음 |

좋은 스위트는 **위 두 층으로 결정하고, 아래 두 층으로 원인을 찾는다**. C7의 latency LUT는 커널 층, 이 노트의 하니스는 모델 층, I1의 예산은 시스템 층이다.

### 1.3 지표 — latency 하나로는 부족하다

| 지표 | 단위 | 왜 필요한가 |
|---|---|---|
| latency p50 / p90 / p99 | ms | 실시간 deadline은 꼬리가 결정한다 (D6) |
| throughput | inferences/s, tokens/s | 처리량이 중요한 워크로드(LLM decode, batch) |
| init time, first inference | s, ms | 앱 시작·wake 후 첫 응답. 컴파일·캐시가 숨어 있다 |
| energy / inference | mJ | 배터리 (D7, K3) |
| peak memory | KiB, MiB | SRAM·DRAM 예산 (D2, K1) |
| model size | KiB, MB | flash, OTA 비용 |
| accuracy / quality delta | pp, WER, perplexity | 빠르지만 틀린 모델은 의미가 없다 |
| sustained ratio | 지속/초기 처리량 | 열·경합으로 인한 하락 (K4) |

"pp"는 percentage point다. 정확도 91.8 %가 91.4 %가 되면 −0.4 pp다(−0.4 %가 아니다).

### 1.4 왜 평균이 아니라 p90·p99인가 — 숫자로

D6에서 길게 다뤘으니 여기서는 벤치마크 보고와 관련된 두 가지만 숫자로 확인한다. (1) 평균과 p90조차 드문 스파이크를 숨길 수 있다. (2) p99를 믿으려면 샘플이 충분해야 한다.

무엇을 확인하나: 97 %는 2 ms, 3 %는 12 ms인 latency 분포에서 각 요약값과 deadline miss율, 그리고 샘플 수에 따른 p99 추정치의 흔들림.

```python
# 평균은 꼬리를 숨긴다 + p99를 믿으려면 샘플이 몇 개 필요한가
import numpy as np
rng = np.random.default_rng(0)
def sample(n):   # 97 %: 정상 2 ms 근처, 3 %: 캐시 miss·선점으로 12 ms 근처
    spike = rng.random(n) < 0.03
    return np.where(spike, rng.normal(12, 1.5, n), rng.normal(2.0, 0.15, n))
x = sample(100_000)
p50, p90, p99 = np.percentile(x, [50, 90, 99])
print(f"mean {x.mean():.2f}  p50 {p50:.2f}  p90 {p90:.2f}  p99 {p99:.2f}  max {x.max():.2f} ms")
print(f"deadline 10 ms miss rate: {(x > 10).mean()*100:.2f} %  (mean만 보면 '여유 4배')")
for n in [100, 1000, 10000]:
    est = [np.percentile(sample(n), 99) for _ in range(300)]   # 같은 실험을 300번 반복
    lo, hi = np.percentile(est, [2.5, 97.5])
    print(f"n={n:>5}: p99 추정치 95 % 범위 [{lo:5.2f}, {hi:5.2f}] ms  (p99 위 샘플 ≈ {n//100}개)")
```

```text
mean 2.30  p50 2.01  p90 2.22  p99 12.62  max 16.72 ms
deadline 10 ms miss rate: 2.66 %  (mean만 보면 '여유 4배')
n=  100: p99 추정치 95 % 범위 [ 2.32, 13.94] ms  (p99 위 샘플 ≈ 1개)
n= 1000: p99 추정치 95 % 범위 [11.59, 13.23] ms  (p99 위 샘플 ≈ 10개)
n=10000: p99 추정치 95 % 범위 [12.37, 12.92] ms  (p99 위 샘플 ≈ 100개)
```

출력에서 볼 것: 스파이크가 3 %이면 **p90(2.22 ms)도 스파이크를 못 본다**. 어떤 분위수를 볼지는 "사건이 얼마나 자주 일어나면 문제인가"로 정한다 — 1초에 100번 도는 오디오 루프라면 1 %는 초당 한 번이다. 그리고 샘플 100개로 잰 p99는 2.3 ms에서 13.9 ms까지 흔들린다. 경험칙: **p99를 보고하려면 최소 1000개, 가능하면 p99 위에 샘플이 수십 개 이상 남게** 잰다.

말로 하면: 분위수 q를 보고하려면 그 바깥 꼬리 (1 − q) × n이 충분히 커야 한다. n = 100이면 p99 바깥에 샘플이 1개뿐이라, "p99"는 사실상 "최댓값 근처 아무 값"이다.

---

## 2. 업계 벤치마크 지형 — MLPerf

### 2.1 MLCommons와 MLPerf

MLPerf는 비영리 컨소시엄 **MLCommons**가 운영하는 ML 벤치마크 모음이다. 학습(Training)과 추론(Inference)으로 나뉘고, 추론 쪽은 대상 기기 크기에 따라 여러 갈래가 있다. 핵심 아이디어는 SPEC CPU 같은 전통 벤치마크와 같다: **같은 모델, 같은 데이터, 같은 정확도 목표, 같은 측정 규칙**을 정해 두고 각 회사가 결과를 제출하면 다른 회사가 검토(peer review)한 뒤 공개한다.

| 갈래 | 대상 | 대표 지표 | 이 노트에서의 쓸모 |
|---|---|---|---|
| MLPerf Tiny | MCU·초저전력 가속기 (mW 급) | latency, energy/inference, 정확도 목표 | always-on 계층 칩 비교 |
| MLPerf Mobile | 스마트폰·태블릿 SoC (앱으로 측정) | latency, throughput (task별) | Snapdragon 급 SoC 비교 |
| MLPerf Inference (Edge / Datacenter) | edge 서버·임베디드 보드 ~ 데이터센터 | 시나리오별 latency·throughput, 선택적 power | 시나리오 개념의 원조 |
| MLPerf Client | 노트북·데스크톱 (LLM) | time to first token, tokens/s | 온디바이스 LLM 측정 방식의 참고 |

세부 모델 목록, 정확도 목표, 최소 실행 시간 같은 규칙은 **라운드(버전)마다 바뀐다**. 아래 내용은 오래 유지된 골격 위주로 쓰고, 버전에 따라 다를 수 있는 부분은 그렇게 표시한다. 실제로 인용할 때는 해당 라운드의 공식 규칙 문서를 확인해야 한다.

### 2.2 MLPerf Tiny — MCU 급

MLPerf Tiny는 2021년경 첫 라운드가 나왔고 Banbury et al.의 논문 "MLPerf Tiny Benchmark"(NeurIPS 2021 Datasets and Benchmarks)에 처음 네 개 벤치마크가 정리되어 있다.

| 벤치마크 | 데이터셋 | 참조 모델 | 품질 목표 (첫 라운드 기준, 대략) |
|---|---|---|---|
| Keyword Spotting (KWS) | Google Speech Commands (12 class) | DS-CNN | top-1 약 90 % |
| Visual Wake Words (VWW) | Visual Wake Words (COCO에서 "사람 있음/없음") | MobileNetV1 0.25× (96×96 입력) | top-1 약 80 % |
| Image Classification (IC) | CIFAR-10 | ResNet-8 계열 소형 ResNet | top-1 약 85 % |
| Anomaly Detection (AD) | ToyADMOS (장난감 차 소리) | fully-connected autoencoder | AUC 약 0.85 |

측정은 이렇게 한다(골격).

- 기기(DUT, device under test)에 모델을 올리고, 호스트 PC의 러너가 UART 등으로 입력을 보내고 결과를 받는다. 이 하니스는 **EEMBC**가 만든 러너(EnergyRunner 계열)를 쓰는 것으로 알려져 있다.
- **latency**: 기기 안에서 추론 시간(입력이 이미 기기에 있는 상태에서)을 여러 번 재서 보고한다. 전처리(예: MFCC)를 포함하는지는 벤치마크 규칙이 정한다.
- **energy**: 선택 항목. 전원 측정 장비를 붙여 추론 1회당 에너지(µJ)를 보고한다. 측정 경로 때문에 통신 잡음이 섞이지 않도록 전원·신호를 분리하는 구성이 규칙에 들어 있다.
- **accuracy**: 정해진 테스트 세트에서 품질 목표 이상이어야 결과로 인정된다.

최근 라운드에는 스트리밍 wake word 같은 벤치마크가 추가되었다고 알려져 있지만, 구성은 라운드마다 다르니 확인이 필요하다.

웨어러블 엔지니어에게 Tiny가 주는 교훈: KWS와 VWW는 Hark 같은 기기의 always-on 계층(I3 cascade의 첫 단)과 거의 같은 크기다. 벤더가 "MLPerf Tiny KWS 결과"를 내밀면, 그건 DS-CNN 한 모델의 숫자이고 **우리 KWS의 특징 추출·후처리·streaming 구조는 포함하지 않는다**는 점을 기억한다.

### 2.3 MLPerf Inference — 시나리오와 LoadGen

MLPerf Inference의 가장 중요한 발명은 **시나리오(scenario)** 개념이다. 같은 하드웨어라도 "요청이 어떻게 들어오는가"에 따라 좋은 숫자가 달라진다는 걸 규칙으로 만들었다.

| 시나리오 | 요청 패턴 | 지표 | 실제 예 |
|---|---|---|---|
| SingleStream | query 1개(sample 1개), 끝나면 다음 | **90번째 백분위 latency** | 폰 카메라 한 장씩, 웨어러블 KWS |
| MultiStream | query 1개 = sample 여러 개(최근 규칙에서 8개)를 한 번에 | 높은 백분위 latency (최근 규칙에서 p99) | 여러 카메라 프레임을 동시에 처리하는 차량 |
| Server | Poisson 분포로 무작위 도착 | latency 상한(백분위)을 지키는 최대 QPS | 클라우드 API |
| Offline | 모든 sample을 한꺼번에 | throughput (samples/s) | 사진 라이브러리 일괄 분류 |

Edge 카테고리는 주로 SingleStream·MultiStream·Offline을, Datacenter는 Server·Offline을 쓴다. MultiStream의 정의는 초기 라운드와 이후 라운드가 달라졌으니 인용할 때 버전을 확인한다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="44" font-size="13">SingleStream</text><line x1="130" y1="50" x2="660" y2="50" stroke="currentColor"/><rect x="130" y="34" width="60" height="16" fill="#4a7bd0"/><rect x="190" y="34" width="54" height="16" fill="#4a7bd0" opacity="0.7"/>
<rect x="244" y="34" width="66" height="16" fill="#4a7bd0"/><rect x="310" y="34" width="58" height="16" fill="#4a7bd0" opacity="0.7"/><rect x="368" y="34" width="62" height="16" fill="#4a7bd0"/><text x="440" y="46" font-size="12">끝나면 다음 → p90 latency</text>
<text x="10" y="104" font-size="13">MultiStream</text><line x1="130" y1="110" x2="660" y2="110" stroke="currentColor"/><rect x="130" y="78" width="120" height="32" fill="none" stroke="#e08a3c" stroke-width="2"/><rect x="250" y="78" width="126" height="32" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="190" y="99" font-size="12" text-anchor="middle">8 samples</text><text x="313" y="99" font-size="12" text-anchor="middle">8 samples</text><text x="390" y="99" font-size="12">query = 여러 sample → 높은 백분위 latency</text><text x="10" y="164" font-size="13">Server</text>
<line x1="130" y1="170" x2="660" y2="170" stroke="currentColor"/><line x1="140" y1="140" x2="140" y2="154" stroke="#d0564a" stroke-width="2"/><line x1="165" y1="140" x2="165" y2="154" stroke="#d0564a" stroke-width="2"/><line x1="235" y1="140" x2="235" y2="154" stroke="#d0564a" stroke-width="2"/>
<line x1="245" y1="140" x2="245" y2="154" stroke="#d0564a" stroke-width="2"/><line x1="330" y1="140" x2="330" y2="154" stroke="#d0564a" stroke-width="2"/><rect x="140" y="154" width="40" height="16" fill="#4a7bd0"/><rect x="180" y="154" width="40" height="16" fill="#4a7bd0" opacity="0.7"/>
<rect x="235" y="154" width="40" height="16" fill="#4a7bd0"/><rect x="275" y="154" width="40" height="16" fill="#4a7bd0" opacity="0.7"/><rect x="330" y="154" width="40" height="16" fill="#4a7bd0"/><text x="390" y="160" font-size="12">무작위 도착(빨강) + 큐 대기</text>
<text x="390" y="176" font-size="12">→ latency 상한을 지키는 최대 QPS</text><text x="10" y="224" font-size="13">Offline</text><line x1="130" y1="230" x2="660" y2="230" stroke="currentColor"/><rect x="130" y="200" width="250" height="30" fill="#3f9a6b" opacity="0.8"/>
<text x="255" y="220" font-size="12" text-anchor="middle">전부 한꺼번에, 큰 batch</text><text x="390" y="220" font-size="12">→ samples/s (throughput)</text><text x="395" y="260" font-size="12" text-anchor="middle">시간 →</text>
</svg>
```

그림 2 — MLPerf Inference의 네 시나리오. 같은 가속기라도 요청이 들어오는 방식(파랑 = 처리 중, 빨강 = 도착 시각)에 따라 다른 지표를 본다.

**LoadGen**은 MLPerf가 제공하는 공용 부하 생성기(C++ 라이브러리, Python 바인딩)다. 시스템(SUT, system under test)은 "query를 받으면 결과를 돌려주는" 콜백만 구현하고, query를 언제 몇 개 보낼지·시간을 어떻게 잴지·로그를 어떻게 남길지는 LoadGen이 정한다. 그래서 제출자가 타이머를 유리하게 조작할 수 없다. 펌웨어로 비유하면 **테스트 벡터 생성기와 판정기를 고객이 제공하고, 우리는 DUT 쪽만 구현하는 factory test**와 같다. LoadGen에는 성능 모드와 정확도 모드가 따로 있고, 최소 query 수·최소 실행 시간(예전 라운드 기준 수 분 단위)이 규칙으로 정해져 있다 — 정확한 값은 버전마다 다르다.

같은 시스템을 시나리오별로 재면 숫자가 얼마나 달라지는지 간단한 모델로 확인해 보자.

무엇을 확인하나: "고정 2 ms + sample당 1 ms"인 가상 가속기를 네 시나리오 방식으로 쟀을 때의 지표.

```python
# 같은 '가상 가속기'를 MLPerf Inference 4개 시나리오 방식으로 재면 숫자가 어떻게 달라지나 (단순화 모델)
import numpy as np
rng = np.random.default_rng(1)
def svc(b):            # batch b 처리 시간(ms): 고정 2 ms + sample당 1 ms, 10 % 지터
    return (2.0 + 1.0 * b) * rng.lognormal(0, 0.1)
# SingleStream: 끝나면 다음 query(1 sample). 지표 = p90 latency
ss = np.array([svc(1) for _ in range(5000)])
print(f"SingleStream  p90 latency      {np.percentile(ss, 90):6.2f} ms")
# MultiStream: query 하나 = sample 8개를 한 batch로. 지표 = p99 latency (최근 규칙 기준, 버전마다 다름)
ms = np.array([svc(8) for _ in range(5000)])
print(f"MultiStream   p99 latency(8)   {np.percentile(ms, 99):6.2f} ms")
# Offline: 전부 한꺼번에 -> 큰 batch로 처리량 최대화. 지표 = samples/s
b = 64
print(f"Offline       throughput       {b / np.mean([svc(b) for _ in range(200)]) * 1e3:6.0f} samples/s")
# Server: Poisson 도착, batch 1, FIFO. 지표 = p99 latency <= 15 ms를 지키는 최대 QPS
def server_p99(qps, n=20000):
    arr = np.cumsum(rng.exponential(1e3 / qps, n)); free = 0.0; lat = []
    for a in arr:
        start = max(a, free); free = start + svc(1); lat.append(free - a)
    return np.percentile(lat, 99)
for qps in [100, 150, 175, 200, 250]:
    p = server_p99(qps)
    print(f"Server        {qps:4d} QPS -> p99 {p:7.2f} ms {'OK' if p <= 15 else 'FAIL'}")
```

```text
SingleStream  p90 latency        3.40 ms
MultiStream   p99 latency(8)    12.50 ms
Offline       throughput          964 samples/s
Server         100 QPS -> p99    8.78 ms OK
Server         150 QPS -> p99   12.38 ms OK
Server         175 QPS -> p99   15.33 ms FAIL
Server         200 QPS -> p99   16.41 ms FAIL
Server         250 QPS -> p99   26.46 ms FAIL
```

출력에서 볼 것: 같은 하드웨어인데 SingleStream으로는 초당 약 1/3.4 ms ≈ 294회, Offline으로는 964 samples/s, Server로는 p99 15 ms를 지키며 150~175 QPS다. 처리 능력(1/3 ms ≈ 333 QPS)에 가까워질수록 큐가 쌓여 p99가 폭발한다(D6의 대기행렬 직관). **"초당 964개"라는 Offline 숫자를 웨어러블 KWS(SingleStream)에 들이밀면 3배 이상 과장**이다. 벤더 숫자를 받으면 "어느 시나리오인가"를 먼저 묻는다.

### 2.4 MLPerf Mobile

MLPerf Mobile은 MLCommons가 배포하는 **벤치마크 앱**(Android, iOS)이다. 앱 안에 task별 모델과 데이터가 들어 있고, SoC 벤더가 자기 가속기용 backend(예: Qualcomm, MediaTek, Samsung의 SDK를 쓰는 backend, 또는 범용 TFLite delegate)를 제공한다. task로는 이미지 분류, 물체 검출, semantic segmentation, 질의응답(MobileBERT 계열), super-resolution 등이 들어갔고, 최근 라운드에는 생성 모델(예: text-to-image)이 추가되었다고 알려져 있다 — 구성은 버전마다 다르다. 지표는 task별 SingleStream latency와 Offline throughput이다.

Mobile 결과를 읽을 때 기억할 점:

- 각 벤더가 **자기 SDK로 모델을 컴파일**한다. 그래서 MLPerf Mobile 숫자는 "칩 + 그 벤더 툴체인"의 숫자다. 우리가 같은 툴체인을 못 쓰면(예: QNN 대신 ONNX Runtime을 쓸 계획) 그 숫자는 우리에게 해당되지 않는다.
- 폰은 열 설계가 웨어러블보다 훨씬 넉넉하다. 폰에서 나온 SingleStream latency는 **웨어러블 폼팩터의 sustained 숫자보다 낙관적**이다.

### 2.5 MLPerf Client

MLPerf Client는 노트북·데스크톱에서 **LLM 추론**을 재는 비교적 새 벤치마크다(2025년경 첫 공개 버전). Llama 계열 같은 공개 LLM으로 요약·생성 같은 몇 가지 작업을 돌리고, **time to first token(TTFT)**과 **초당 토큰 수(tokens/s)**를 보고한다. 지원 모델·작업·backend는 버전에 따라 바뀌고 있다.

이건 F3의 `llama-bench`가 재는 prefill(pp)·decode(tg) 구분과 같은 생각이다: TTFT는 주로 prefill에, tokens/s는 decode에 해당한다(D5). 웨어러블의 SLM은 노트북보다 훨씬 작지만 **두 숫자를 분리해서 보고한다**는 형식은 그대로 가져온다.

### 2.6 규칙 — closed vs open division, 정확도 목표

| 개념 | 뜻 | 왜 중요한가 |
|---|---|---|
| Closed division | 참조 모델(같은 구조·같은 가중치 계열)과 정해진 전처리를 써야 함. 양자화 등은 규칙 범위 안에서 허용 | **하드웨어·소프트웨어 스택끼리 공정 비교** |
| Open division | 모델 구조 변경, 재학습, pruning 등 자유. 단 정확도를 함께 보고 | 혁신 기법을 보여 주는 곳. 숫자끼리 직접 비교 불가 |
| 정확도 목표 | Inference closed는 FP32 참조 정확도의 99 % (일부 벤치마크는 99.9 % 변형도) 이상 | int8·int4로 빠르게 하되 품질을 지켰는지 |
| Available / Preview 등 | 제출 시스템이 지금 살 수 있는지, 곧 나오는지 | 아직 없는 칩의 숫자를 걸러냄 |
| peer review | 다른 제출사가 결과·코드를 검토 | 숫자 조작 억제 |

**"FP32 대비 99 %"**를 손으로 계산해 보자. 참조 모델 top-1이 76.46 %라면 99 %는 75.70 %다. 즉 int8 양자화로 0.76 pp까지 떨어져도 closed 결과로 인정된다. 웨어러블 KWS에서 0.76 pp는 false reject율로 따지면 꽤 클 수 있다 — **업계 목표가 우리 제품 목표는 아니다**.

### 2.7 MLPerf를 어떻게 쓰나 — 그리고 한계

쓸모:

- **같은 규칙에서 나온 숫자라 벤더 마케팅 숫자보다 믿을 만하다**. 특히 closed division의 SingleStream latency.
- 측정 **방법론**(시나리오, LoadGen, 정확도 게이트, 최소 실행 시간, 결과 검토)을 우리 사내 스위트에 그대로 빌려 올 수 있다.

한계:

- **모델이 오래됐거나 우리와 다르다**. MobileNetV1·ResNet-8은 우리 제품 모델이 아니다.
- **제출 자체가 선택**이다. 결과가 나쁜 칩은 제출하지 않는다. 결과가 없는 것도 정보다.
- 제출사는 그 벤치마크 모델 하나에 **컴파일러를 극한까지 튜닝**한다. 우리 모델에는 그 튜닝이 없다.
- 열·전원 조건은 제출 시스템마다 다르다. 웨어러블 폼팩터를 대표하지 않는다.

결론: MLPerf는 **후보를 거르는 1차 필터**이고, 결정은 **우리 워크로드로 직접 잰 숫자**로 한다(4~5장).

---

## 3. 벤더 숫자가 숨기는 것

### 3.1 체크리스트 — 숫자 하나에 붙은 조건

D7 5.2절과 M4에서 TOPS/W의 숨은 조건을 다뤘다. 벤치마크 관점에서 정리하면 이렇다.

| 숨은 조건 | 무엇이 부풀려지나 | 물어볼 질문 |
|---|---|---|
| peak vs sustained | 최고 클럭·차가운 칩에서 잰 숫자 | "10분 연속 실행 후 숫자는? 어떤 폼팩터·방열에서?" |
| batch 크기 | batch 32 throughput을 latency처럼 표기 | "batch=1 SingleStream p90은?" |
| 정밀도 | INT4 TOPS, sparsity 2배 크레딧 | "우리 모델 정밀도(INT8 dense)에서의 실효 처리량은?" |
| 포함 범위 | NPU 코어 시간만, 전처리·DMA·드라이버 제외 | "입력 버퍼 준비부터 결과 수신까지 end-to-end는?" |
| 첫 실행 비용 | 컴파일·캐시 이후의 steady-state만 | "init time과 첫 추론 latency는?" |
| 벤치마크별 튜닝 | 그 모델 전용 컴파일러 플래그·수작업 커널 | "기본 툴체인 설정으로 돌린 숫자는? 우리 모델로는?" |
| 모델 선택 | 그 가속기에 유리한 모델만 공개 | "attention·depthwise·LSTM이 섞인 모델로는?" |
| 전력 경계 | 코어 전력만, DRAM·PMIC·SoC 제외 | "보드 입력 전력 기준 mJ/inference는?" |
| 정확도 | 빠르게 하려고 정확도 손실 | "FP32 대비 정확도 delta는?" |

SSD 데이터시트의 "최대 순차 읽기 7 GB/s"가 queue depth·전송 크기·SLC 캐시 조건을 숨기는 것과 똑같다. Don에게는 이미 몸에 밴 질문들이다.

### 3.2 TOPS를 우리 모델의 ms로 — 손계산

벤더가 "10 TOPS NPU"라고 한다. 우리 ASR encoder(5장에서 실제로 만든 모델, 104.5 M MAC/추론)는 얼마나 걸릴까?

```
연산량 = 104.5 M MAC × 2 op/MAC = 209 M op
naive  = 209 M op ÷ 10 T op/s = 0.021 ms     ← 벤더 숫자를 그대로 믿으면
```

이제 조건을 하나씩 벗긴다. 계수는 예시 가정이고, 실제로는 벤더에게 물어서 채운다.

```
10 TOPS (INT4, 2:4 sparsity, peak)
 × 0.5  INT8로 (INT4 대비 MAC 처리량 절반인 구조가 흔함)   → 5    TOPS
 × 0.5  sparsity 크레딧 제거 (우리 모델은 dense)          → 2.5  TOPS
 × 0.7  sustained clock (열 제한)                       → 1.75 TOPS
 × 0.3  우리 모델 활용률 (작은 batch, attention, 정렬 손실) → 0.525 TOPS
```

말로 하면: 표기된 TOPS는 "모든 MAC 유닛이 매 cycle 쉬지 않고 일할 때"의 상한이고, 각 조건은 그중 실제로 일하는 비율을 곱해 나가는 것이다. 펌웨어 감각으로는 "버스 이론 대역폭 × 효율 × 버스트 비율"을 곱해 실효 대역폭을 내는 것과 같다.

여기에 memory-bound 검사(D3 roofline)와 고정 오버헤드를 더한다.

무엇을 확인하나: 위 계산을 코드로 하고, weight를 DRAM에서 매번 읽는 경우와 고정 오버헤드를 더해 effective TOPS를 낸다.

```python
# 벤더 "10 TOPS"를 우리 ASR encoder(104.5 M MAC/추론) 지연으로 바꿔 본다 — 가정은 전부 명시
MACS = 104.5e6
OPS = 2 * MACS                                   # 1 MAC = 2 op (곱 + 덧셈)
steps = [  # (설명, 곱할 계수)  ← 각 계수는 벤더에 '물어봐서' 채울 값
    ("headline: INT4, 2:4 sparsity, peak clock", 1.0),
    ("INT4 -> INT8 (MAC 처리량 절반 가정)",       0.5),
    ("sparsity 크레딧 제거 (dense 모델)",          0.5),
    ("sustained clock (열 제한 70 %)",             0.7),
    ("우리 모델 활용률 (작은 batch·attention 30 %)", 0.3),
]
tops = 10e12
for desc, k in steps:
    tops *= k
    print(f"{desc:<42} {tops/1e12:6.3f} TOPS -> {OPS/tops*1e3:7.3f} ms")
overhead_ms = 0.30                                 # 전처리·DMA·드라이버·fallback op (실측해야 함)
weights_mb, dram_gbs = 1.0, 8.0                    # int8 weight 1 MB를 매번 DRAM에서 읽는다면
mem_ms = weights_mb * 1e6 / (dram_gbs * 1e9) * 1e3
compute_ms = OPS / tops * 1e3
print(f"compute {compute_ms:.3f} ms vs weight stream {mem_ms:.3f} ms -> "
      f"{'compute' if compute_ms > mem_ms else 'memory'}-bound")
total = max(compute_ms, mem_ms) + overhead_ms
print(f"end-to-end 추정 {total:.3f} ms, headline 대비 {total / (OPS/10e12*1e3):.0f}x 느림")
print(f"effective TOPS = {OPS/(total*1e-3)/1e12:.3f} (headline의 {OPS/(total*1e-3)/10e12*100:.1f} %)")
```

```text
headline: INT4, 2:4 sparsity, peak clock   10.000 TOPS ->   0.021 ms
INT4 -> INT8 (MAC 처리량 절반 가정)                5.000 TOPS ->   0.042 ms
sparsity 크레딧 제거 (dense 모델)                  2.500 TOPS ->   0.084 ms
sustained clock (열 제한 70 %)                 1.750 TOPS ->   0.119 ms
우리 모델 활용률 (작은 batch·attention 30 %)         0.525 TOPS ->   0.398 ms
compute 0.398 ms vs weight stream 0.125 ms -> compute-bound
end-to-end 추정 0.698 ms, headline 대비 33x 느림
effective TOPS = 0.299 (headline의 3.0 %)
```

출력에서 볼 것: 가정 하나하나는 그럴듯한 수준인데, 곱하면 **headline의 3 %**만 남는다. 0.021 ms와 0.698 ms는 33배 차이다. 여기서 가장 큰 불확실성은 "활용률 30 %"와 "오버헤드 0.3 ms"인데, 둘 다 **우리 모델을 실제로 돌려 봐야만** 알 수 있다. 그래서 벤더에게 TOPS가 아니라 "이 ONNX 파일을 돌린 end-to-end p90"을 요구한다. 계산 자체도 정렬된 표(헤드라인 → 실효)로 보고하면 경영진이 바로 이해한다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250" x2="630" y2="250" stroke="currentColor"/><line x1="70" y1="30" x2="70" y2="250" stroke="currentColor"/><line x1="66" y1="250.0" x2="70" y2="250.0" stroke="currentColor"/><text x="62" y="254.0" font-size="12" text-anchor="end">0</text>
<line x1="66" y1="206.0" x2="70" y2="206.0" stroke="currentColor"/><text x="62" y="210.0" font-size="12" text-anchor="end">2</text><line x1="66" y1="162.0" x2="70" y2="162.0" stroke="currentColor"/><text x="62" y="166.0" font-size="12" text-anchor="end">4</text>
<line x1="66" y1="118.0" x2="70" y2="118.0" stroke="currentColor"/><text x="62" y="122.0" font-size="12" text-anchor="end">6</text><line x1="66" y1="74.0" x2="70" y2="74.0" stroke="currentColor"/><text x="62" y="78.0" font-size="12" text-anchor="end">8</text>
<line x1="66" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/><text x="62" y="34.0" font-size="12" text-anchor="end">10</text><rect x="82.0" y="30.0" width="69.3" height="220.0" fill="#4a7bd0"/><text x="116.7" y="24.0" font-size="12" text-anchor="middle">10</text>
<text x="116.7" y="268.0" font-size="12" text-anchor="middle">headline</text><rect x="175.3" y="140.0" width="69.3" height="110.0" fill="#e08a3c"/><text x="210.0" y="134.0" font-size="12" text-anchor="middle">5</text><text x="210.0" y="268.0" font-size="12" text-anchor="middle">INT8</text>
<rect x="268.7" y="195.0" width="69.3" height="55.0" fill="#e08a3c"/><text x="303.3" y="189.0" font-size="12" text-anchor="middle">2.5</text><text x="303.3" y="268.0" font-size="12" text-anchor="middle">dense</text><rect x="362.0" y="211.5" width="69.3" height="38.5" fill="#e08a3c"/>
<text x="396.7" y="205.5" font-size="12" text-anchor="middle">1.75</text><text x="396.7" y="268.0" font-size="12" text-anchor="middle">sustained</text><rect x="455.3" y="238.4" width="69.3" height="11.6" fill="#e08a3c"/><text x="490.0" y="232.4" font-size="12" text-anchor="middle">0.525</text>
<text x="490.0" y="268.0" font-size="12" text-anchor="middle">util 30%</text><rect x="548.7" y="243.4" width="69.3" height="6.6" fill="#d0564a"/><text x="583.3" y="237.4" font-size="12" text-anchor="middle">0.299</text><text x="583.3" y="268.0" font-size="12" text-anchor="middle">+overhead</text>
<text x="18" y="140.0" font-size="12" transform="rotate(-90 18 140.0)" text-anchor="middle">TOPS</text><text x="350.0" y="292" font-size="13" text-anchor="middle">10 TOPS 표기 → 우리 ASR encoder에서 실제 0.3 TOPS (3 %)</text>
</svg>
```

그림 3 — 위 계산의 막대 그림. 파랑이 벤더 표기, 주황이 조건을 하나씩 벗긴 값, 빨강이 오버헤드까지 넣은 실효값(0.299 TOPS)이다.

LLM decode라면 판정이 반대가 된다. 135 M 파라미터 SLM(SmolLM2-135M)을 Q4_0 GGUF로 저장하면 85.8 MiB ≈ 90 MB다(5.7절 출력; embedding 등 일부 텐서는 더 높은 정밀도라 순수 4-bit 계산값보다 크다). 토큰 하나마다 이걸 대부분 읽으므로 DRAM 8 GB/s라면 토큰당 약 90 MB ÷ 8 GB/s ≈ 11 ms, 즉 **TOPS와 무관하게 약 89 tokens/s가 상한**이다(D5). 이런 워크로드에는 TOPS보다 메모리 대역폭과 on-chip SRAM 크기를 먼저 묻는다.

### 3.3 peak vs sustained — 어떻게 시험하나

| 방법 | 무엇을 보나 | 주의 |
|---|---|---|
| 연속 실행 N분, 창(window)별 처리량 기록 | 처리량이 시간에 따라 떨어지는지 | 창을 너무 짧게 잡으면 잡음, 너무 길면 변화가 뭉개짐 |
| 온도·클럭 동시 기록 | 하락의 원인이 throttling인지 | 센서 위치에 따라 늦게 반응 |
| 폼팩터에서 측정 | 실제 방열 조건 | 개발보드(방열판 있음)는 낙관적 |
| 주변 온도 고정 (챔버) | 여름·주머니 속 조건 | 장비 필요 |
| duty-cycle 패턴으로 실행 | 제품처럼 "일하다 쉬다" | 연속 부하보다 현실적 |

5.8절에서 60초 지속 실행을 실제로 해 보고, 이 Mac에서는 **열이 아니라 다른 프로세스와의 경합**이 하락을 지배했다는 것을 데이터로 보인다. 원인을 구분하려면 처리량과 함께 **원인 후보의 시계열**(온도, 클럭, load)을 같이 남겨야 한다.

---

## 4. 우리 제품용 벤치마크 스위트 설계 — 웨어러블 사례

> 이 절의 제품 구성은 **가상의 사례**다. "예를 들어 Hark 같은 음성 중심 웨어러블이라면"이라는 가정 아래, 공개 정보가 아닌 추정으로 만든 예시다.

### 4.1 원칙 — 스위트는 제품의 축소판

좋은 사내 스위트의 원칙은 MLPerf와 같다: **고정된 워크로드, 고정된 데이터, 고정된 규칙, 정확도 게이트**. 다른 점은 워크로드가 **우리 제품에서 실제로 도는 것**이라는 점이다.

| 원칙 | 펌웨어 대응 |
|---|---|
| 워크로드는 제품 기능에서 뽑는다 | 테스트 벡터는 실제 호스트 트래픽 패턴에서 뽑는다 |
| 입력도 대표성이 있어야 한다 (조용한 방만 X) | 4K random만 재지 않고 mixed workload도 잰다 |
| 정확도는 점수가 아니라 **게이트** | 데이터 무결성은 성능과 맞바꾸지 않는다 |
| 같은 하니스로 모든 후보를 잰다 | 같은 FIO job 파일로 모든 드라이브를 잰다 |
| 원시 데이터와 환경을 남긴다 | 로그·펌웨어 버전·온도를 함께 보관 |

### 4.2 워크로드 — 무엇을 넣을까

| 워크로드 | 역할 | 대표 모델(예) | 입력 | 핵심 지표 |
|---|---|---|---|---|
| KWS (wake word) | always-on 1단 | DS-CNN 류 | 1 s 오디오 → 49×10 MFCC, 매 20~100 ms | p99 latency, energy/inference, FRR/FA per hour |
| IMU gesture / 착용 감지 | always-on 센서 | 1D CNN | 6축 × 128 샘플 | p99, energy, 정확도 |
| presence detection | 저해상 카메라·근접 | MobileNet 0.35× 96×96 | 이미지 | p90, energy |
| ASR encoder | wake 이후 burst | 소형 Transformer/Conformer | 100 frame × 80 mel | p90, RTF, WER delta |
| SLM prefill / decode | 대화 응답 | 135 M~1 B 파라미터 | 프롬프트 128 tok, 생성 32 tok | TTFT, tokens/s, peak memory, perplexity delta |

RTF(real-time factor)는 "처리 시간 ÷ 오디오 길이"다. 1초 오디오를 0.2초에 처리하면 RTF 0.2이고, 1보다 작아야 실시간으로 따라간다.

### 4.3 입력 분포 — "어떤 데이터로"도 스펙이다

- **정확도용 eval 세트**: 실제 사용자 조건(소음, 억양, 착용 위치, 걷는 중)을 반영한 라벨 데이터. H4·H5에서 다룬 데이터셋 관리 대상이다.
- **latency용 입력**: 대부분의 모델은 입력 값과 무관하게 같은 연산을 한다. 하지만 **입력 의존 경로가 있으면 분포를 맞춰야 한다**: early-exit 모델, cascade(VAD가 통과시킨 비율), LLM의 프롬프트 길이·생성 길이, 동적 shape(가변 길이 오디오).
- **calibration 세트**: int8 양자화에 쓰는 대표 입력. eval 세트와 겹치면 안 된다(C3).

### 4.4 시나리오 — MLPerf 시나리오를 제품 언어로

```svg
<svg viewBox="0 0 680 240" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="40" font-size="12">KWS</text><text x="10" y="80" font-size="12">IMU</text><text x="10" y="120" font-size="12">ASR enc</text><text x="10" y="160" font-size="12">SLM</text>
<line x1="70" y1="180" x2="670" y2="180" stroke="currentColor"/><rect x="80" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="110" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="140" y="28" width="6" height="16" fill="#4a7bd0"/>
<rect x="170" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="200" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="230" y="28" width="6" height="16" fill="#d0564a"/><rect x="260" y="28" width="6" height="16" fill="#4a7bd0"/>
<rect x="290" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="320" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="350" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="380" y="28" width="6" height="16" fill="#4a7bd0"/>
<rect x="410" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="440" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="470" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="500" y="28" width="6" height="16" fill="#4a7bd0"/>
<rect x="530" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="560" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="590" y="28" width="6" height="16" fill="#4a7bd0"/><rect x="620" y="28" width="6" height="16" fill="#4a7bd0"/>
<rect x="95" y="68" width="10" height="16" fill="#3f9a6b"/><rect x="195" y="68" width="10" height="16" fill="#3f9a6b"/><rect x="295" y="68" width="10" height="16" fill="#3f9a6b"/><rect x="395" y="68" width="10" height="16" fill="#3f9a6b"/>
<rect x="495" y="68" width="10" height="16" fill="#3f9a6b"/><rect x="595" y="68" width="10" height="16" fill="#3f9a6b"/><rect x="240" y="108" width="110" height="16" fill="#e08a3c"/><rect x="355" y="148" width="40" height="16" fill="#d0564a"/>
<rect x="395" y="148" width="170" height="16" fill="#d0564a" opacity="0.6"/><line x1="233" y1="20" x2="233" y2="185" stroke="#d0564a" stroke-dasharray="4 3"/><text x="236" y="200" font-size="12">wake 검출</text><text x="80" y="200" font-size="12">① single-stream always-on</text>
<text x="300" y="220" font-size="12">② burst: ASR → SLM prefill(진함) → decode(옅음)</text><text x="420" y="200" font-size="12">③ 동시 실행: KWS·IMU 계속</text>
</svg>
```

그림 4 — 가상의 웨어러블 타임라인. 파랑(KWS)·초록(IMU)은 계속 돌고, wake(빨간 점선) 뒤에 ASR(주황)과 SLM(빨강)이 burst로 돈다. 그동안에도 always-on 모델은 멈추지 않는다.

| 시나리오 | 정의 | MLPerf 대응 | 보고할 숫자 |
|---|---|---|---|
| ① always-on single-stream | KWS를 hop마다 1회, IMU를 1 Hz | SingleStream | p99 latency, energy/inference, 평균 전력 |
| ② burst after wake | 깨어난 직후 ASR 1 s + SLM 128 tok prefill + 32 tok decode | SingleStream + TTFT | wake→첫 토큰 시간, init 포함 여부 명시 |
| ③ concurrent | ②가 도는 동안 ①도 계속 | (MLPerf에는 없음) | ①의 p99 악화, deadline miss 수 |
| ④ sustained | ②를 10분 반복 (대화가 길어진 경우) | (MLPerf에는 없음) | 지속/초기 처리량 비율, 표면 온도 |

③과 ④가 MLPerf에 없는 것이 핵심이다. 웨어러블의 진짜 위험은 "ASR이 NPU·DRAM을 쓰는 동안 KWS가 deadline을 놓치는가"(E8의 공유 자원 경합)와 "주머니 속에서 10분 대화하면 느려지는가"다. 이건 **우리가 직접 시나리오로 만들어야만** 보인다.

### 4.5 측정 방법론 — 고정할 것 체크리스트

D6의 하니스 원칙을 스위트 수준으로 올린 목록이다.

| 항목 | 규칙 | 이유 |
|---|---|---|
| warm-up | 첫 N회는 버리고 **첫 추론은 따로 기록** | cold 비용(컴파일, 캐시, page fault)은 숨기지 말고 분리 |
| 반복 수 | 보고할 분위수에 맞게 (p99면 ≥ 1000) | 1.4절 |
| 프로세스 격리 | 조합마다 새 프로세스 | 이전 backend의 메모리·스레드 풀 잔재 제거 |
| 클럭·전원 | 가능하면 고정 (performance governor, 고정 OPP), 전원 상태 기록 | DVFS가 숫자를 흔든다 |
| 스레드 | 수와 affinity 고정 | big.LITTLE에서 어느 코어인지가 결과를 바꾼다 |
| 열 상태 | 시작 온도 기록, 실행 사이 cool-down | 앞 실행의 열이 뒤 실행을 느리게 |
| 배경 부하 | 측정 전후 load 기록, 가능하면 비행기 모드·서비스 중지 | 5장의 이 Mac이 반면교사 |
| 입력 | seed 고정, 파일 해시 기록 | 재현성 |
| 반복 실행 | 같은 스위트를 여러 번(다른 시각) | run 내부 분산 ≠ run 간 분산 (7.2절) |

### 4.6 보고 — 표 하나에 무엇을

| 열 | 예 |
|---|---|
| 워크로드 · backend · 정밀도 | asr_encoder · Core ML · fp16 |
| p50 / p90 / p99 (ms) + 반복 수 | 0.373 / 0.517 / 0.655 (n=300) |
| init (s), 첫 추론 (ms) | 3.44, 7.67 |
| peak memory, model size | 159 MiB(측정 방법 주석), 2.0 MB |
| 정확도와 delta | 96.2 %, +0.004 pp |
| energy/inference | (측정 장비 필요 — 미측정이면 "미측정"이라고 쓴다) |
| 환경 지문 ID | git hash, 툴체인 버전, 온도, 전원 |

빈칸을 숨기지 않는다. "미측정"과 "0"은 다르다.

---

## 5. 미니 벤치마크 하니스 — 직접 만들어 보기

이제 4장의 설계를 축소판으로 구현한다. 이 Mac에서 쓸 수 있는 backend 여섯 개(torch CPU eager, ORT CPU fp32, ORT CPU int8, ORT CoreML EP, Core ML fp16 CPU_ONLY, Core ML fp16 CPU_AND_NE)를 "실리콘·런타임 후보"의 대역으로 쓰고, LLM 한 행은 `llama-bench`로 잰다. 파일은 모두 스크래치 폴더에서 만들었다.

```
zoo.py        모델 4개 정의 + 합성 라벨 데이터 + 짧은 학습(캐시)
export.py     ONNX fp32 / ONNX int8(QDQ static) / Core ML fp16 패키지 생성
runner.py     (model, backend) 하나를 새 프로세스에서 측정 → JSON 한 줄
run_suite.py  4 모델 × 6 backend 루프 → raw.jsonl(원시) + summary.csv(요약)
```

### 5.1 스위트 정의 — 모델과 합성 eval 데이터

진짜 데이터셋 대신 **라벨이 있는 합성 데이터**를 쓴다. 클래스마다 패턴(KWS는 시간-계수 방향의 줄무늬, IMU는 축·주파수가 다른 진동, presence는 랜덤 위치의 밝은 blob)을 잡음에 섞고, 각 모델을 짧게 학습시킨다. 이렇게 해야 "양자화 후 정확도 delta"가 의미 있는 숫자가 된다.

```python
# zoo.py (발췌: 모델 정의 부분)
import os, torch, torch.nn as nn, torchvision

def dw_block(c):  # depthwise 3x3 + pointwise 1x1 (DS-CNN 블록)
    return nn.Sequential(nn.Conv2d(c, c, 3, padding=1, groups=c), nn.BatchNorm2d(c), nn.ReLU(),
                         nn.Conv2d(c, c, 1), nn.BatchNorm2d(c), nn.ReLU())

def kws_dscnn():   # MLPerf Tiny KWS와 비슷한 DS-CNN: 49x10 MFCC -> 12 class
    return nn.Sequential(nn.Conv2d(1, 64, (10, 4), stride=2, padding=(5, 1)), nn.BatchNorm2d(64), nn.ReLU(),
                         *[dw_block(64) for _ in range(4)],
                         nn.AdaptiveAvgPool2d(1), nn.Flatten(), nn.Linear(64, 12))

def imu_cnn1d():   # 6축 IMU 128 샘플(약 2.5 s @50 Hz) -> 8 제스처
    return nn.Sequential(nn.Conv1d(6, 32, 5, padding=2), nn.BatchNorm1d(32), nn.ReLU(),
                         nn.Conv1d(32, 64, 5, stride=2, padding=2), nn.BatchNorm1d(64), nn.ReLU(),
                         nn.Conv1d(64, 64, 3, stride=2, padding=1), nn.BatchNorm1d(64), nn.ReLU(),
                         nn.AdaptiveAvgPool1d(1), nn.Flatten(), nn.Linear(64, 8))

class AsrEncoder(nn.Module):  # 1 s(100 frame x 80 mel) -> frame별 40 class logits
    def __init__(s, d=144, layers=4):
        super().__init__()
        s.inp = nn.Linear(80, d)
        layer = nn.TransformerEncoderLayer(d, 4, 4 * d, dropout=0.0, batch_first=True)
        s.enc = nn.TransformerEncoder(layer, layers, enable_nested_tensor=False)
        s.out = nn.Linear(d, 40)
        for p in s.enc.parameters():   # TransformerEncoder는 layer를 deepcopy -> 4층이 같은 가중치. 다시 랜덤화
            nn.init.normal_(p, std=0.05) if p.dim() > 1 else None
    def forward(s, x):
        return s.out(s.enc(s.inp(x)))

def presence_mnv2():  # VWW 비슷한 사람 유무: 96x96 RGB -> 2 class
    return torchvision.models.mobilenet_v2(width_mult=0.35, num_classes=2)

SUITE = {  # name: (ctor, input shape)
    "kws_dscnn":     (kws_dscnn,     (1, 1, 49, 10)),
    "imu_gesture":   (imu_cnn1d,     (1, 6, 128)),
    "asr_encoder":   (AsrEncoder,    (1, 100, 80)),
    "presence_vww":  (presence_mnv2, (1, 3, 96, 96)),
}
```

```python
# zoo.py (발췌: 합성 라벨 데이터 · 학습 · eval 세트)
NCLS = {"kws_dscnn": 12, "imu_gesture": 8, "asr_encoder": 40, "presence_vww": 2}
AMP = {"kws_dscnn": 0.42, "imu_gesture": 0.4, "asr_encoder": 0.5, "presence_vww": 1.5}

def make_data(name, n, seed):
    """합성 라벨 데이터 = 클래스별 패턴(위상·위치는 매번 랜덤) × AMP + N(0,1) 잡음"""
    shape, k, A = SUITE[name][1][1:], NCLS[name], AMP[name]
    g = torch.Generator().manual_seed(seed)
    if name == "asr_encoder":                    # frame마다 라벨, 클래스별 고정 prototype
        P = torch.randn(k, shape[-1], generator=torch.Generator().manual_seed(42))
        y = torch.randint(0, k, (n, shape[0]), generator=g)
        return A * P[y] + torch.randn((n,) + shape, generator=g), y
    y = torch.randint(0, k, (n,), generator=g)
    ph = 2 * torch.pi * torch.rand(n, generator=g)
    x = torch.randn((n,) + shape, generator=g)
    if name == "kws_dscnn":                      # (1,49,10): 시간 주파수 1~4 x 계수 방향 0~2
        t, f = torch.arange(49.0)[:, None], torch.arange(10.0)[None, :]
        for i in range(n):
            a, b = 1 + y[i] % 4, y[i] // 4
            x[i, 0] += A * torch.cos(2 * torch.pi * (a * t / 12 + b * f / 10) + ph[i])
    elif name == "imu_gesture":                  # (6,128): 축 c%6에 c별 주파수 진동
        t = torch.arange(128.0)
        for i in range(n):
            x[i, y[i] % 6] += A * 2 * torch.sin(2 * torch.pi * (2 + 3 * y[i]) * t / 128 + ph[i])
    else:                                        # presence: class 1이면 랜덤 위치에 blob(사람)
        yy, xx = torch.meshgrid(torch.arange(96.0), torch.arange(96.0), indexing="ij")
        for i in range(n):
            if y[i] == 1:
                cy, cx = 16 + 64 * torch.rand(2, generator=g)
                x[i] += A * torch.exp(-((yy - cy) ** 2 + (xx - cx) ** 2) / (2 * 10.0 ** 2))
    return x, y

def build(name, seed=0):
    """랜덤 초기화 -> 합성 데이터로 짧게 학습 (art/<name>.pt 캐시) -> eval 모델"""
    ctor, shape = SUITE[name]
    torch.manual_seed(seed); torch.set_num_threads(4)
    m = ctor()
    ck = f"art/{name}.pt"
    if os.path.exists(ck):
        m.load_state_dict(torch.load(ck))
    else:
        opt = torch.optim.Adam(m.parameters(), 1e-3)
        m.train()
        for i in range(80 if name == "presence_vww" else 200):   # MNV2 학습은 CPU에서 느려서 짧게
            x, y = make_data(name, 32, seed=1000 + i)
            loss = nn.functional.cross_entropy(m(x).reshape(-1, NCLS[name]), y.reshape(-1))
            opt.zero_grad(); loss.backward(); opt.step()
        os.makedirs("art", exist_ok=True); torch.save(m.state_dict(), ck)
    return m.eval(), shape

def eval_set(name, n=256, seed=123):
    """라벨 있는 합성 eval 세트 (학습 seed와 겹치지 않음)"""
    return make_data(name, n, seed)
```

`AMP`(패턴 세기)는 fp32 정확도가 90 % 언저리가 되도록 몇 번 조정했다. 처음에는 너무 약해서 22 %, 너무 세서 99.6 %가 나왔다. 정확도가 100 %에 붙어 있으면 양자화 손실이 보이지 않고, 너무 낮으면 비교가 무의미하다 — **eval 세트의 난이도도 벤치마크 설계의 일부**다.

> 만들면서 실제로 밟은 함정 두 개. (1) 처음에는 학습 없이 랜덤 가중치 + 랜덤 입력으로 "fp32 출력과의 일치율"을 쟀는데, 랜덤 모델은 모든 입력을 같은 클래스로 분류해서(예: KWS 64개 전부 class 5) 일치율이 무조건 100 %였다. 의미 없는 정확도 지표는 숫자가 그럴듯해서 더 위험하다. (2) `nn.TransformerEncoder`는 layer 하나를 deepcopy해서 쌓기 때문에 랜덤 초기화 상태에서 4층의 가중치가 똑같다. ONNX export가 같은 initializer를 하나로 합쳐서 4 MB여야 할 모델 파일이 1.1 MB로 나왔다. **합성 모델로 "모델 크기"를 잴 때는 실제 모델과 같은 구조적 성질을 갖는지 확인**해야 한다.

무엇을 확인하나: 네 모델의 파라미터 수, 저장 크기(fp32·int8 이론값), MAC 수를 hook으로 센다(D1).

```python
import torch
from zoo import SUITE, build
for name in SUITE:
    m, shape = build(name)
    params = sum(p.numel() for p in m.parameters())
    macs = 0
    def hook(mod, i, o):
        global macs
        if isinstance(mod, (torch.nn.Conv1d, torch.nn.Conv2d)):
            macs += o.numel() * mod.in_channels // mod.groups * mod.weight[0, 0].numel()
        elif isinstance(mod, torch.nn.Linear):
            macs += o.numel() * mod.in_features
        elif isinstance(mod, torch.nn.MultiheadAttention):   # in_proj(QKV) + QK^T + AV
            L, d = o[0].shape[-2], o[0].shape[-1]
            macs += L * d * 3 * d + 2 * L * L * d
    hs = [mm.register_forward_hook(hook) for mm in m.modules()]
    with torch.no_grad():
        m(torch.randn(shape))
    for h in hs: h.remove()
    print(f"{name:<13} input {str(tuple(shape)):<16} params {params/1e3:7.1f} K  "
          f"fp32 {params*4/1024:7.1f} KiB  int8 {params/1024:6.1f} KiB  MACs {macs/1e6:6.2f} M")
```

```text
kws_dscnn     input (1, 1, 49, 10)   params    23.8 K  fp32    92.8 KiB  int8   23.2 KiB  MACs   2.66 M
imu_gesture   input (1, 6, 128)      params    24.5 K  fp32    95.7 KiB  int8   23.9 KiB  MACs   1.17 M
asr_encoder   input (1, 100, 80)     params  1020.3 K  fp32  3985.5 KiB  int8  996.4 KiB  MACs 104.49 M
presence_vww  input (1, 3, 96, 96)   params   398.7 K  fp32  1557.4 KiB  int8  389.3 KiB  MACs  10.66 M
```

출력에서 볼 것: KWS·IMU는 int8로 24 KiB 안팎이라 MCU SRAM에 들어가는 급이다. ASR encoder의 MAC 104.49 M은 손으로도 맞춰 볼 수 있다. Linear 모듈 hook만 쓰면 68.08 M이 나오는데, MultiheadAttention의 QKV 투영(100 × 144 × 432 = 6.22 M, 4층이면 24.88 M)과 QKᵀ·AV(2 × 100 × 100 × 144 = 2.88 M, 4층이면 11.52 M)가 빠지기 때문이다. 68.08 + 24.88 + 11.52 = 104.48 M ≈ 104.49 M. 이 값이 3.2절 TOPS 계산의 입력이었다.

### 5.2 backend별 산출물 만들기

```python
# export.py — ONNX fp32 / ONNX int8(QDQ, per-channel, static) / Core ML fp16
import os, warnings, numpy as np, torch, coremltools as ct
from onnxruntime.quantization import quantize_static, CalibrationDataReader, QuantFormat, QuantType
from zoo import SUITE, build, make_data
warnings.filterwarnings("ignore")
os.makedirs("art", exist_ok=True)

class Calib(CalibrationDataReader):   # 대표 입력 32개 (eval 세트와 다른 seed)
    def __init__(s, name):
        X, _ = make_data(name, 32, seed=5000)
        s.it = iter([{"x": x[None].numpy()} for x in X])
    def get_next(s):
        return next(s.it, None)

for name in SUITE:
    m, shape = build(name)
    x = torch.randn(shape)
    torch.onnx.export(m, (x,), f"art/{name}.onnx", input_names=["x"], output_names=["y"],
                      opset_version=17, dynamo=False)
    quantize_static(f"art/{name}.onnx", f"art/{name}_int8.onnx", Calib(name),
                    quant_format=QuantFormat.QDQ, per_channel=True,
                    activation_type=QuantType.QInt8, weight_type=QuantType.QInt8)
    ts = torch.jit.trace(m, x, check_trace=False)
    ml = ct.convert(ts, inputs=[ct.TensorType(name="x", shape=shape)],
                    outputs=[ct.TensorType(name="y")],
                    compute_precision=ct.precision.FLOAT16,
                    minimum_deployment_target=ct.target.macOS14)
    ml.save(f"art/{name}_fp16.mlpackage")
    print(name, "ok")
```

```text
kws_dscnn ok
imu_gesture ok
asr_encoder ok
presence_vww ok
```

출력에서 볼 것: 네 모델 모두 세 형식으로 변환됐다. `check_trace=False`는 ASR encoder의 trace가 "호출마다 그래프가 다르다"는 sanity check에 걸려서 넣었다(Transformer 경로 안의 분기 때문). 변환 과정의 이런 우회는 **보고서에 적어 둘 사항**이다 — 나중에 숫자가 이상하면 제일 먼저 의심할 곳이다(F5, F8).

### 5.3 runner — 조합 하나를 새 프로세스에서

설계 포인트: (1) 조합마다 새 프로세스로 띄워 이전 backend의 잔재를 없앤다. (2) **init time과 첫 추론을 steady-state와 분리**해 기록한다. (3) warm-up 20회 뒤 300회 측정. (4) peak RSS 증가분을 기록한다. (5) 같은 프로세스에서 라벨 대비 정확도와 fp32 torch 출력 대비 일치율을 잰다. (6) 결과는 원시 latency 배열까지 JSON 한 줄로 출력한다.

```python
"""runner.py — 한 (model, backend) 조합을 별도 프로세스에서 잰다 -> JSON 한 줄 출력"""
import sys, json, time, resource, warnings, os
import numpy as np, torch
warnings.filterwarnings("ignore")
from zoo import build, eval_set
name, backend, n_iter = sys.argv[1], sys.argv[2], int(sys.argv[3])
THREADS = 4
torch.set_num_threads(THREADS)
rss = lambda: resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 2**20  # macOS: bytes -> MiB
m, shape = build(name)
X, Y = eval_set(name)                         # 라벨 있는 합성 eval 세트 256개
x0 = X[:1]                                    # latency용 고정 입력
rss0, t0 = rss(), time.perf_counter()
if backend == "torch_cpu":
    def run(x):
        with torch.no_grad():
            return m(x).numpy()
    path = None
elif backend.startswith("ort_"):
    import onnxruntime as ort
    so = ort.SessionOptions(); so.intra_op_num_threads = THREADS
    path = f"art/{name}_int8.onnx" if backend == "ort_cpu_int8" else f"art/{name}.onnx"
    prov = ["CoreMLExecutionProvider", "CPUExecutionProvider"] if backend == "ort_coreml_ep" else ["CPUExecutionProvider"]
    sess = ort.InferenceSession(path, so, providers=prov)
    run = lambda x: sess.run(None, {"x": x.numpy()})[0]
elif backend.startswith("coreml_fp16_"):
    import coremltools as ct
    cu = {"cpu": ct.ComputeUnit.CPU_ONLY, "ane": ct.ComputeUnit.CPU_AND_NE}[backend.split("_")[-1]]
    path = f"art/{name}_fp16.mlpackage"
    ml = ct.models.MLModel(path, compute_units=cu)
    run = lambda x: ml.predict({"x": x.numpy()})["y"]
init_s = time.perf_counter() - t0 if path else float("nan")  # torch eager: 모델이 이미 메모리에
t1 = time.perf_counter(); run(x0); first_ms = (time.perf_counter() - t1) * 1e3
for _ in range(20):                          # warm-up
    run(x0)
lat = []
for _ in range(n_iter):
    s = time.perf_counter(); run(x0); lat.append((time.perf_counter() - s) * 1e3)
peak_mib = rss() - rss0 if path else float("nan")
with torch.no_grad():                         # 정확도: 라벨 대비 + fp32 torch 출력 대비
    ref = np.concatenate([m(x[None]).numpy() for x in X])
out = np.concatenate([run(x[None]) for x in X])
acc_ref = float((ref.argmax(-1) == Y.numpy()).mean())
acc = float((out.argmax(-1) == Y.numpy()).mean())
agree = float((out.argmax(-1) == ref.argmax(-1)).mean())
relerr = float(np.abs(out - ref).max() / np.abs(ref).max())
size_kib = None
if path:
    size_kib = (os.path.getsize(path) if os.path.isfile(path) else
                sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(path) for f in fs)) / 1024
print(json.dumps(dict(model=name, backend=backend, init_s=init_s, first_ms=first_ms, lat_ms=lat,
                      peak_mib=peak_mib, size_kib=size_kib, acc_ref=acc_ref, acc=acc, agree=agree, relerr=relerr)))
```

이 runner는 30줄 규칙을 넘지만, 한 화면에 "측정 규칙 전체"가 보이는 것이 오히려 중요해서 그대로 둔다. 실제 업무에서는 backend별 어댑터 클래스(`load()`, `run()`, `info()`)로 나누고 기기 쪽(adb, 시리얼)을 같은 인터페이스로 붙인다.

`peak_mib`는 `ru_maxrss`(프로세스 생애 최대 RSS)의 증가분이라 거칠다. 모델 메모리뿐 아니라 **런타임 프레임워크가 처음 로드되며 쓰는 메모리**도 들어간다. 이 한계는 결과 해석(5.5절)과 점수화(6장)에서 실제로 문제를 일으킨다.

### 5.4 스위트 실행 — 통합 결과표

```python
# run_suite.py
import subprocess, sys, json, numpy as np, pandas as pd
from zoo import SUITE
BACKENDS = ["torch_cpu", "ort_cpu_fp32", "ort_cpu_int8", "ort_coreml_ep", "coreml_fp16_cpu", "coreml_fp16_ane"]
rows = []
with open("results/raw.jsonl", "w") as raw:            # 원시 데이터는 전부 보관
    for name in SUITE:
        for b in BACKENDS:
            out = subprocess.run([sys.executable, "runner.py", name, b, "300"],
                                 capture_output=True, text=True).stdout.strip().splitlines()[-1]
            raw.write(out + "\n")
            d = json.loads(out); lat = np.array(d["lat_ms"])
            p50, p90, p99 = np.percentile(lat, [50, 90, 99])
            rows.append(dict(model=name, backend=b, p50=p50, p90=p90, p99=p99,
                             init_s=d["init_s"], first_ms=d["first_ms"], peakMiB=d["peak_mib"],
                             sizeKiB=d["size_kib"], acc=d["acc"], dacc_pp=100 * (d["acc"] - d["acc_ref"]),
                             agree=d["agree"]))
df = pd.DataFrame(rows); df.to_csv("results/summary.csv", index=False)
pd.set_option("display.width", 200)
print(df.to_string(index=False, float_format=lambda v: f"{v:.3g}"))
```

첫 번째 실행(run1, 시작 시 load average 7.3, 끝날 때 19.6)의 결과다. 단위는 latency ms, init s, 첫 추론 ms, 메모리 MiB, 크기 KiB, dacc는 pp.

```text
       model         backend    p50    p90   p99  init_s  first_ms  peakMiB  sizeKiB   acc  dacc_pp  agree
   kws_dscnn       torch_cpu  0.978   1.22  1.88     NaN      3.17      NaN      NaN 0.918        0      1
   kws_dscnn    ort_cpu_fp32  0.438   0.67  2.62   0.082      2.46       14     91.4 0.918        0      1
   kws_dscnn    ort_cpu_int8  0.119  0.183  3.56  0.0299     0.805     14.3     46.1 0.914   -0.391  0.996
   kws_dscnn   ort_coreml_ep  0.258  0.299 0.422   0.132     0.921     25.4     91.4 0.918        0      1
   kws_dscnn coreml_fp16_cpu  0.227  0.299 0.486    3.97      45.6      159       60 0.918        0      1
   kws_dscnn coreml_fp16_ane  0.276  0.393  2.48    3.48      6.48      161       60 0.918        0      1
 imu_gesture       torch_cpu  0.396  0.715  7.71     NaN      1.14      NaN      NaN 0.969        0      1
 imu_gesture    ort_cpu_fp32 0.0847  0.146 0.647  0.0942      1.57     14.1     95.6 0.969        0      1
 imu_gesture    ort_cpu_int8 0.0563 0.0837 0.309   0.102      2.55     13.5     32.6 0.969        0      1
 imu_gesture   ort_coreml_ep  0.243  0.325 0.535   0.394      1.66     25.8     95.6 0.969        0      1
 imu_gesture coreml_fp16_cpu  0.107  0.193 0.676    7.91        53      118     54.4 0.969        0      1
 imu_gesture coreml_fp16_ane  0.142  0.482  6.62    4.34       5.9      161     54.4 0.969        0      1
 asr_encoder       torch_cpu   3.48   4.45  7.96     NaN      4.35      NaN      NaN 0.962        0      1
 asr_encoder    ort_cpu_fp32   4.93   13.4  23.6  0.0838      3.11     24.2 4.04e+03 0.962        0      1
 asr_encoder    ort_cpu_int8   4.17   10.3  17.3  0.0505      3.74     19.3 1.22e+03 0.963   0.0195  0.997
 asr_encoder   ort_coreml_ep   8.61   13.6  24.4    1.18      12.4     47.6 4.04e+03 0.962        0      1
 asr_encoder coreml_fp16_cpu   1.21   1.41  1.94    2.95      43.4      160 2.05e+03 0.962 -0.00391      1
 asr_encoder coreml_fp16_ane  0.373  0.517 0.655    3.44      7.67      159 2.05e+03 0.962  0.00391      1
presence_vww       torch_cpu   15.6     19  26.4     NaN      16.7      NaN      NaN 0.992        0      1
presence_vww    ort_cpu_fp32   1.94    6.6  12.3  0.0334      2.25       18 1.57e+03 0.992        0      1
presence_vww    ort_cpu_int8  0.549  0.821  3.17  0.0378      5.54     17.5      618 0.988   -0.391  0.996
presence_vww   ort_coreml_ep   0.23  0.357 0.529   0.644      1.37     35.3 1.57e+03 0.992        0      1
presence_vww coreml_fp16_cpu  0.723   4.06  14.5     5.7      42.3      145      856 0.992        0      1
presence_vww coreml_fp16_ane  0.203  0.226 0.502    6.97      7.17      165      856 0.992        0      1
```

이 표가 이 노트의 핵심 산출물이다. 한 줄 한 줄 읽어 보자.

### 5.5 결과 읽기 — 표에서 무엇을 보나

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="240" x2="650" y2="240" stroke="currentColor"/><line x1="60" y1="20" x2="60" y2="240" stroke="currentColor"/><line x1="60" y1="240.0" x2="650" y2="240.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54" y="244.0" font-size="12" text-anchor="end">0.01</text>
<line x1="60" y1="185.0" x2="650" y2="185.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54" y="189.0" font-size="12" text-anchor="end">0.1</text><line x1="60" y1="130.0" x2="650" y2="130.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54" y="134.0" font-size="12" text-anchor="end">1</text>
<line x1="60" y1="75.0" x2="650" y2="75.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54" y="79.0" font-size="12" text-anchor="end">10</text><line x1="60" y1="20.0" x2="650" y2="20.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54" y="24.0" font-size="12" text-anchor="end">100</text>
<rect x="70.0" y="125.2" width="19.2" height="114.8" fill="#888"/><rect x="91.2" y="139.6" width="19.2" height="100.4" fill="#4a7bd0"/><rect x="112.5" y="170.6" width="19.2" height="69.4" fill="#7fa6e6"/><rect x="133.8" y="158.9" width="19.2" height="81.1" fill="#e08a3c"/>
<rect x="155.0" y="158.8" width="19.2" height="81.2" fill="#3f9a6b"/><rect x="176.2" y="152.3" width="19.2" height="87.7" fill="#d0564a"/><text x="133.8" y="256" font-size="12" text-anchor="middle">kws_dscnn</text><rect x="217.5" y="138.0" width="19.2" height="102.0" fill="#888"/>
<rect x="238.8" y="176.0" width="19.2" height="64.0" fill="#4a7bd0"/><rect x="260.0" y="189.2" width="19.2" height="50.8" fill="#7fa6e6"/><rect x="281.2" y="156.8" width="19.2" height="83.2" fill="#e08a3c"/><rect x="302.5" y="169.3" width="19.2" height="70.7" fill="#3f9a6b"/>
<rect x="323.8" y="147.4" width="19.2" height="92.6" fill="#d0564a"/><text x="281.2" y="256" font-size="12" text-anchor="middle">imu_gesture</text><rect x="365.0" y="94.4" width="19.2" height="145.6" fill="#888"/><rect x="386.2" y="68.0" width="19.2" height="172.0" fill="#4a7bd0"/>
<rect x="407.5" y="74.2" width="19.2" height="165.8" fill="#7fa6e6"/><rect x="428.8" y="67.7" width="19.2" height="172.3" fill="#e08a3c"/><rect x="450.0" y="121.9" width="19.2" height="118.1" fill="#3f9a6b"/><rect x="471.2" y="145.8" width="19.2" height="94.2" fill="#d0564a"/>
<text x="428.8" y="256" font-size="12" text-anchor="middle">asr_encoder</text><rect x="512.5" y="59.7" width="19.2" height="180.3" fill="#888"/><rect x="533.8" y="84.9" width="19.2" height="155.1" fill="#4a7bd0"/><rect x="555.0" y="134.7" width="19.2" height="105.3" fill="#7fa6e6"/>
<rect x="576.2" y="154.6" width="19.2" height="85.4" fill="#e08a3c"/><rect x="597.5" y="96.6" width="19.2" height="143.4" fill="#3f9a6b"/><rect x="618.8" y="165.5" width="19.2" height="74.5" fill="#d0564a"/><text x="576.2" y="256" font-size="12" text-anchor="middle">presence_vww</text>
<rect x="60" y="268" width="12" height="12" fill="#888"/><text x="76" y="278" font-size="12">torch_cpu</text><rect x="260" y="268" width="12" height="12" fill="#4a7bd0"/><text x="276" y="278" font-size="12">ort_cpu_fp32</text>
<rect x="460" y="268" width="12" height="12" fill="#7fa6e6"/><text x="476" y="278" font-size="12">ort_cpu_int8</text><rect x="60" y="286" width="12" height="12" fill="#e08a3c"/><text x="76" y="296" font-size="12">ort_coreml_ep</text>
<rect x="260" y="286" width="12" height="12" fill="#3f9a6b"/><text x="276" y="296" font-size="12">coreml_fp16_cpu</text><rect x="460" y="286" width="12" height="12" fill="#d0564a"/><text x="476" y="296" font-size="12">coreml_fp16_ane</text>
<text x="16" y="130.0" font-size="12" transform="rotate(-90 16 130.0)" text-anchor="middle">p90 (ms, log)</text>
</svg>
```

그림 5 — run1의 p90 latency(log 축). 모델마다 1등 backend가 다르다.

**(1) 작은 모델은 프레임워크 오버헤드가 지배한다.** KWS(2.66 M MAC)의 torch eager p50은 0.978 ms인데 ORT int8은 0.119 ms다. 2.66 M MAC은 M2 CPU에서 수십 µs면 끝나는 양이라, torch eager의 차이는 연산이 아니라 **Python·dispatcher 오버헤드**다. presence_vww의 torch 15.6 ms도 상당 부분 같은 이유로 본다(10.66 M MAC은 ORT CPU에서 약 2 ms였다). 교훈: **작은 모델의 벤치마크는 런타임 벤치마크**다. MCU에서도 마찬가지로, TFLM의 interpreter 오버헤드가 수십 µs면 KWS 추론 시간과 같은 자릿수다(F2).

**(2) 1등은 모델마다 다르다.** ASR encoder와 presence는 Core ML ANE가 압도적(0.373 ms, 0.203 ms)이지만, IMU에서는 ORT CPU int8(0.0563 ms)이 이기고 ANE는 p99 6.62 ms로 꼬리가 길다. 아주 작은 모델은 가속기로 보내는 고정 비용(데이터 복사, 드라이버 호출)이 연산보다 크다. 웨어러블이라면 "IMU는 MCU/DSP에, ASR은 NPU에"라는 I3 cascade 배치가 숫자로 정당화되는 장면이다.

**(3) init time과 첫 추론은 따로 봐야 한다.** Core ML은 init 3~8 s(처음 열 때 mlpackage를 컴파일하고 compute unit별 준비를 함), 첫 추론도 CPU_ONLY는 42~53 ms다. ORT CPU는 init 0.03~0.1 s다. steady-state p50만 보면 Core ML ANE가 최고지만, **wake 직후 처음 한 번이 중요한 시나리오**(4.4절 ②)에서는 init과 첫 추론을 미리 해 두는(pre-warm, 컴파일 결과 캐시) 설계가 따라와야 한다. 이걸 표에 안 넣으면 제품에서 "첫 응답만 느리다"는 버그로 돌아온다.

**(4) 정확도 delta는 게이트로 본다.** int8 static 양자화는 KWS·presence에서 −0.39 pp(256개 중 1개), ASR에서 +0.02 pp였다. fp16은 ±0.004 pp(25,600 frame 중 1개). 이 합성 데이터에서는 모두 작다. 실제 데이터에서는 int8이 수 pp까지 떨어지는 일이 흔하므로(C3), **delta 열이 있다는 사실 자체**가 중요하다. 256개로 잰 −0.39 pp는 샘플 1개 차이라 통계적으로 의미가 없다는 것도 같이 적는다.

**(5) model size는 "파일 크기"다.** KWS int8 ONNX는 46.1 KiB로 fp32(91.4 KiB)의 절반밖에 안 줄었다. 이론값은 4분의 1(23.2 KiB)인데, QDQ 노드·scale·zero-point·그래프 메타데이터의 고정 비용이 작은 모델에서는 상대적으로 크다. ASR(1020 K 파라미터)은 4.04 MB → 1.22 MB로 3.3배 줄었다. **작은 모델일수록 "4배"라는 상식이 안 맞는다.**

**(6) peak memory 열은 측정 방법의 한계를 그대로 드러낸다.** Core ML 행은 모두 약 160 MiB인데, 이건 모델이 아니라 Core ML 프레임워크가 프로세스에 처음 로드되며 쓰는 메모리다(KWS 60 KiB 모델이 160 MiB를 쓸 리 없다). 이 열을 그대로 점수에 넣으면 6장에서 보듯 결론이 뒤집힌다. 실제 기기에서는 런타임 arena 크기(TFLM `arena_used_bytes`, K1)나 OS 메모리 계측으로 **모델 증분**을 따로 재야 한다.

**(7) 잡음이 큰 행은 표에서 보인다.** asr_encoder ORT CPU fp32의 p50 4.93 ms, p99 23.6 ms는 같은 표에서 ORT int8·torch와 비교해도 이상하게 크다. 이 실행 중 load average가 7에서 19로 올랐다. 분포를 보면 확실해진다.

```svg
<svg viewBox="0 0 660 280" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="230" x2="635" y2="230" stroke="currentColor"/><line x1="60" y1="30" x2="60" y2="230" stroke="currentColor"/><text x="54" y="234.0" font-size="12" text-anchor="end">0</text><line x1="57" y1="230.0" x2="60" y2="230.0" stroke="currentColor"/>
<text x="54" y="184.0" font-size="12" text-anchor="end">20</text><line x1="57" y1="180.0" x2="60" y2="180.0" stroke="currentColor"/><text x="54" y="134.0" font-size="12" text-anchor="end">40</text><line x1="57" y1="130.0" x2="60" y2="130.0" stroke="currentColor"/>
<text x="54" y="84.0" font-size="12" text-anchor="end">60</text><line x1="57" y1="80.0" x2="60" y2="80.0" stroke="currentColor"/><text x="54" y="34.0" font-size="12" text-anchor="end">80</text><line x1="57" y1="30.0" x2="60" y2="30.0" stroke="currentColor"/>
<text x="60.0" y="246" font-size="12" text-anchor="middle">0</text><text x="175.0" y="246" font-size="12" text-anchor="middle">5</text><text x="290.0" y="246" font-size="12" text-anchor="middle">10</text><text x="405.0" y="246" font-size="12" text-anchor="middle">15</text>
<text x="520.0" y="246" font-size="12" text-anchor="middle">20</text><text x="635.0" y="246" font-size="12" text-anchor="middle">25</text><rect x="61.0" y="230.0" width="21.0" height="0.0" fill="#4a7bd0"/><rect x="84.0" y="225.0" width="21.0" height="5.0" fill="#4a7bd0"/>
<rect x="107.0" y="52.5" width="21.0" height="177.5" fill="#4a7bd0"/><rect x="130.0" y="97.5" width="21.0" height="132.5" fill="#4a7bd0"/><rect x="153.0" y="160.0" width="21.0" height="70.0" fill="#4a7bd0"/><rect x="176.0" y="175.0" width="21.0" height="55.0" fill="#4a7bd0"/>
<rect x="199.0" y="192.5" width="21.0" height="37.5" fill="#4a7bd0"/><rect x="222.0" y="207.5" width="21.0" height="22.5" fill="#4a7bd0"/><rect x="245.0" y="195.0" width="21.0" height="35.0" fill="#4a7bd0"/><rect x="268.0" y="175.0" width="21.0" height="55.0" fill="#4a7bd0"/>
<rect x="291.0" y="202.5" width="21.0" height="27.5" fill="#4a7bd0"/><rect x="314.0" y="217.5" width="21.0" height="12.5" fill="#4a7bd0"/><rect x="337.0" y="200.0" width="21.0" height="30.0" fill="#4a7bd0"/><rect x="360.0" y="200.0" width="21.0" height="30.0" fill="#4a7bd0"/>
<rect x="383.0" y="220.0" width="21.0" height="10.0" fill="#4a7bd0"/><rect x="406.0" y="217.5" width="21.0" height="12.5" fill="#4a7bd0"/><rect x="429.0" y="220.0" width="21.0" height="10.0" fill="#4a7bd0"/><rect x="452.0" y="230.0" width="21.0" height="0.0" fill="#4a7bd0"/>
<rect x="475.0" y="222.5" width="21.0" height="7.5" fill="#4a7bd0"/><rect x="498.0" y="227.5" width="21.0" height="2.5" fill="#4a7bd0"/><rect x="521.0" y="227.5" width="21.0" height="2.5" fill="#4a7bd0"/><rect x="544.0" y="225.0" width="21.0" height="5.0" fill="#4a7bd0"/>
<rect x="567.0" y="230.0" width="21.0" height="0.0" fill="#4a7bd0"/><rect x="590.0" y="227.5" width="21.0" height="2.5" fill="#4a7bd0"/><rect x="613.0" y="227.5" width="21.0" height="2.5" fill="#4a7bd0"/><line x1="216.2" y1="30" x2="216.2" y2="230" stroke="#888" stroke-width="2" stroke-dasharray="5 3"/>
<text x="220.2" y="56" font-size="12">mean 6.8</text><line x1="173.3" y1="30" x2="173.3" y2="230" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="5 3"/><text x="177.3" y="42" font-size="12">p50 4.9</text><line x1="368.5" y1="30" x2="368.5" y2="230" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/>
<text x="372.5" y="42" font-size="12">p90 13.4</text><line x1="603.3" y1="30" x2="603.3" y2="230" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 3"/><text x="607.3" y="42" font-size="12">p99 23.6</text><text x="347.5" y="264" font-size="12" text-anchor="middle">latency (ms) — asr_encoder · ORT CPU fp32 · 300회 (run1, 부하 높음)</text>
<text x="16" y="130.0" font-size="12" transform="rotate(-90 16 130.0)" text-anchor="middle">횟수</text>
</svg>
```

그림 6 — asr_encoder · ORT CPU fp32의 300회 latency 분포(run1). 2~4 ms에 몸통이 있고 10~25 ms까지 꼬리가 길다. 평균(6.8)이 p50(4.9)보다 크게 오른쪽에 있는 전형적인 꼬리 분포다.

같은 조합을 2분 뒤 다시 돌린 run2에서는 p50 2.78 ms, p90 4.11 ms, p99 8.14 ms였다(전체 표는 7.2절에서 비교). 하니스를 만들던 중 같은 구조(학습 전 가중치 — latency는 가중치 값과 무관하다)로 잰 시험 실행에서는 p50 1.26 ms가 나왔다. **같은 코드, 같은 모델, 같은 기계에서 p50이 1.26 → 4.93 → 2.78 ms**로 흔들린 것이다. CPU backend는 다른 프로세스와 코어를 나눠 쓰기 때문에 이런 환경에서 가장 크게 흔들리고, 전용 가속기(ANE) 행은 상대적으로 안정적이었다(0.373 → 0.36 ms).

### 5.6 느린 backend의 "왜" — 그래프가 몇 조각으로 갈렸나

ORT CoreML EP는 ASR encoder에서 8.61 ms로 오히려 ORT CPU보다 느렸다. F5·F6에서 본 partition 문제를 의심하고 확인한다.

무엇을 확인하나: CoreML EP가 각 모델 그래프에서 몇 개 노드를 몇 조각(partition)으로 가져갔는지.

```python
# ORT CoreML EP가 그래프를 몇 조각으로 나눠 가져갔나 — 느린 backend의 '왜'를 찾는 첫 질문
import re, subprocess, sys
code = """import onnxruntime as ort
so = ort.SessionOptions(); so.log_severity_level = 1          # INFO 로그에 GetCapability 요약이 찍힌다
ort.InferenceSession('art/{}.onnx', so, providers=['CoreMLExecutionProvider', 'CPUExecutionProvider'])"""
for n in ["kws_dscnn", "imu_gesture", "asr_encoder", "presence_vww"]:
    log = subprocess.run([sys.executable, "-c", code.format(n)], capture_output=True, text=True).stderr
    p, tot, sup = re.search(r"partitions supported by CoreML: (\d+) number of nodes in the graph: (\d+) "
                            r"number of nodes supported by CoreML: (\d+)", log).groups()
    print(f"{n:<13} nodes {tot:>3}  on CoreML {sup:>3} ({int(sup)/int(tot)*100:3.0f} %)  partitions {p:>2}")
```

```text
kws_dscnn     nodes  21  on CoreML  21 (100 %)  partitions  1
imu_gesture   nodes   9  on CoreML   8 ( 89 %)  partitions  2
asr_encoder   nodes 160  on CoreML 114 ( 71 %)  partitions 33
presence_vww  nodes 100  on CoreML 100 (100 %)  partitions  1
```

출력에서 볼 것: CNN 모델은 통째로(1 partition) 넘어갔고 presence는 0.23 ms로 빨랐다. ASR encoder는 노드의 71 %만 지원되고 **33조각**으로 쪼개졌다. 조각 사이마다 CPU ↔ 가속기 전환과 데이터 복사가 생긴다. 같은 모델을 Core ML로 직접 변환한 경로(coremltools)는 0.373 ms였으니, **같은 하드웨어라도 런타임 경로에 따라 23배 차이**다. 벤치마크 보고서에 "backend = CoreML"만 적으면 안 되고 **경로와 partition 수**까지 적어야 하는 이유다(M1의 "툴체인 성숙도" 항목이 바로 이것). Qualcomm 기기라면 QNN의 graph 분할·CPU fallback 로그가 같은 역할을 한다(F4).

### 5.7 LLM 한 행 — `llama-bench`

SLM은 latency 하나로 요약할 수 없다. F3처럼 prefill(pp)과 decode(tg)를 따로 재고, 반복별 원시값까지 남긴다.

무엇을 확인하나: SmolLM2-135M의 Q8_0과 Q4_0을 CPU 4스레드로 pp128·tg32 각 5회 반복.

```python
import subprocess, json
BIN = "/Users/donh/workspace/dons_tech_study_dashboard/dons_study_note_from_experience/embeddedAIPrepBasedOnHarkAIJD/.tools/llama.cpp/build/bin/llama-bench"
MD = "/Users/donh/workspace/dons_tech_study_dashboard/dons_study_note_from_experience/embeddedAIPrepBasedOnHarkAIJD/.tools/models/"
for q in ["Q8_0", "Q4_0"]:
    cmd = [BIN, "-m", MD + f"smollm2-135m-{q}.gguf", "-dev", "none", "-t", "4",
           "-p", "128", "-n", "32", "-r", "5", "-o", "jsonl"]
    for line in subprocess.run(cmd, capture_output=True, text=True).stdout.splitlines():
        d = json.loads(line)
        kind = f"pp{d['n_prompt']}" if d["n_prompt"] else f"tg{d['n_gen']}"
        ts = d["samples_ts"]                     # 반복별 tokens/s 원시값
        print(f"slm_135m {q:<5} {kind:<6} {d['avg_ts']:7.1f} ± {d['stddev_ts']:5.1f} tok/s"
              f"  min {min(ts):7.1f}  size {d['model_size']/2**20:5.1f} MiB  build {d['build_commit']}")
```

같은 스크립트를 두 번 돌렸다. 첫 번째는 load average가 약 100이던 때, 두 번째는 약 8이던 때다.

```text
slm_135m Q8_0  pp128     41.4 ±   3.2 tok/s  min    36.8  size 136.4 MiB  build f7b384c
slm_135m Q8_0  tg32       4.6 ±   1.4 tok/s  min     2.6  size 136.4 MiB  build f7b384c
slm_135m Q4_0  pp128     32.9 ±   4.7 tok/s  min    29.1  size  85.8 MiB  build f7b384c
slm_135m Q4_0  tg32       3.9 ±   0.4 tok/s  min     3.2  size  85.8 MiB  build f7b384c
```

```text
slm_135m Q8_0  pp128   1023.2 ± 269.8 tok/s  min   729.6  size 136.4 MiB  build f7b384c
slm_135m Q8_0  tg32     106.5 ±  55.8 tok/s  min    66.2  size 136.4 MiB  build f7b384c
slm_135m Q4_0  pp128   1186.5 ± 529.9 tok/s  min   637.7  size  85.8 MiB  build f7b384c
slm_135m Q4_0  tg32     166.0 ±  32.7 tok/s  min   133.7  size  85.8 MiB  build f7b384c
```

출력에서 볼 것: **같은 명령, 같은 빌드(f7b384c)인데 25~40배 차이**다. 첫 번째 결과를 보고서에 넣었다면 "135 M 모델도 초당 4 토큰, 온디바이스 LLM 불가"라는 틀린 결론이 나온다. 두 번째 결과에서도 표준편차가 평균의 20~45 %라 Q8_0과 Q4_0의 pp 차이(1023 vs 1187)는 잡음 안에 있다. tg는 Q4_0이 166으로 Q8_0의 106보다 빠른데, decode가 메모리 대역폭에 묶여 있어 읽을 바이트가 적은 쪽이 유리하다는 D5의 예측과 방향은 맞는다 — 하지만 이 잡음 수준에서는 "방향이 맞다" 이상을 주장하면 안 된다. 그래서 **환경 지문(load average)을 결과와 같이 남기는 것**이 필수다(7.1절).

스위트 표에서 LLM 행은 이렇게 들어간다: `slm_135m · llama.cpp CPU · Q4_0 · pp128 1186 tok/s(TTFT 약 108 ms) · tg32 166 tok/s · 85.8 MiB · perplexity delta (미측정)`. TTFT ≈ 128 ÷ 1186.5 ≈ 0.108 s다.

### 5.8 지속 실행 — 60초 동안 처리량이 유지되나

무엇을 확인하나: ASR encoder를 60초 동안 연속 실행하며 5초 창마다 처리량·p50·p99와 load를 기록하고, 앞뒤로 macOS 열 경고 상태를 읽는다.

```python
# 60 s 연속 실행: 5 s 창마다 처리량(inf/s)과 p99 -> 처음 5 s(=흔한 '벤치 숫자')와 비교
import sys, time, subprocess, warnings, numpy as np, onnxruntime as ort, coremltools as ct
warnings.filterwarnings("ignore")
from zoo import eval_set
backend, DUR, WIN = sys.argv[1], 60.0, 5.0
x = eval_set("asr_encoder")[0][:1].numpy()
if backend == "ort_cpu_fp32":
    so = ort.SessionOptions(); so.intra_op_num_threads = 4
    s = ort.InferenceSession("art/asr_encoder.onnx", so, providers=["CPUExecutionProvider"])
    run = lambda: s.run(None, {"x": x})
else:
    ml = ct.models.MLModel("art/asr_encoder_fp16.mlpackage", compute_units=ct.ComputeUnit.CPU_AND_NE)
    run = lambda: ml.predict({"x": x})
therm = lambda: subprocess.run(["pmset", "-g", "therm"], capture_output=True, text=True).stdout.splitlines()[0].strip()
load = lambda: subprocess.run(["sysctl", "-n", "vm.loadavg"], capture_output=True, text=True).stdout.split()[1]
print("before:", therm(), "| load1", load())
for _ in range(20): run()
t_end, rows = time.perf_counter() + DUR, []
while time.perf_counter() < t_end:
    w_end, lat = time.perf_counter() + WIN, []
    while time.perf_counter() < w_end:
        a = time.perf_counter(); run(); lat.append((time.perf_counter() - a) * 1e3)
    rows.append((len(lat) / WIN, np.percentile(lat, 50), np.percentile(lat, 99), load()))
for i, (thr, p50, p99, ld) in enumerate(rows):
    print(f"{i*WIN:4.0f}-{(i+1)*WIN:3.0f}s  {thr:7.1f} inf/s  p50 {p50:5.2f}  p99 {p99:6.2f} ms  load1 {ld}")
thr = np.array([r[0] for r in rows])
print(f"first window {thr[0]:.1f}, last {thr[-1]:.1f}, min {thr.min():.1f} inf/s -> "
      f"sustained/first = {thr[-3:].mean()/thr[0]:.2f}, CoV = {thr.std()/thr.mean()*100:.1f} %")
print("after :", therm())
```

`ort_cpu_fp32`로 실행:

```text
before: Note: No thermal warning level has been recorded | load1 10.98
   0-  5s    311.2 inf/s  p50  2.43  p99  19.30 ms  load1 11.22
   5- 10s    402.2 inf/s  p50  2.36  p99   5.26 ms  load1 11.29
  10- 15s    325.2 inf/s  p50  2.51  p99  17.77 ms  load1 11.02
  15- 20s    385.2 inf/s  p50  2.38  p99   5.57 ms  load1 10.94
  20- 25s    390.6 inf/s  p50  2.46  p99   5.16 ms  load1 10.70
  25- 30s    393.2 inf/s  p50  2.39  p99   5.82 ms  load1 10.81
  30- 35s    332.6 inf/s  p50  2.77  p99   7.41 ms  load1 10.66
  35- 40s    342.8 inf/s  p50  2.77  p99   6.60 ms  load1 10.37
  40- 45s    215.0 inf/s  p50  3.47  p99  15.87 ms  load1 15.06
  45- 50s    188.0 inf/s  p50  3.89  p99  19.96 ms  load1 15.62
  50- 55s    182.6 inf/s  p50  3.92  p99  18.30 ms  load1 20.37
  55- 60s    194.2 inf/s  p50  3.86  p99  16.28 ms  load1 22.99
first window 311.2, last 194.2, min 182.6 inf/s -> sustained/first = 0.60, CoV = 27.2 %
after : Note: No thermal warning level has been recorded
```

`coreml_ane`로 실행:

```text
before: Note: No thermal warning level has been recorded | load1 21.79
   0-  5s   2357.4 inf/s  p50  0.36  p99   1.20 ms  load1 20.84
   5- 10s   1946.2 inf/s  p50  0.37  p99   2.95 ms  load1 19.81
  10- 15s   2352.6 inf/s  p50  0.36  p99   1.27 ms  load1 18.95
  15- 20s   2241.6 inf/s  p50  0.36  p99   1.60 ms  load1 18.79
  20- 25s   2282.8 inf/s  p50  0.37  p99   1.42 ms  load1 17.93
  25- 30s   2247.4 inf/s  p50  0.37  p99   1.41 ms  load1 17.05
  30- 35s   2115.4 inf/s  p50  0.37  p99   2.44 ms  load1 16.25
  35- 40s   2226.6 inf/s  p50  0.37  p99   1.68 ms  load1 15.59
  40- 45s   2269.4 inf/s  p50  0.37  p99   1.30 ms  load1 14.98
  45- 50s   2044.4 inf/s  p50  0.38  p99   2.63 ms  load1 14.50
  50- 55s   2309.8 inf/s  p50  0.37  p99   1.22 ms  load1 13.90
  55- 60s   2320.6 inf/s  p50  0.37  p99   1.24 ms  load1 13.27
first window 2357.4, last 2320.6, min 1946.2 inf/s -> sustained/first = 0.94, CoV = 5.5 %
after : Note: No thermal warning level has been recorded
```

```svg
<svg viewBox="0 0 660 290" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="220" x2="640" y2="220" stroke="currentColor"/><line x1="60" y1="20" x2="60" y2="220" stroke="currentColor"/><text x="54" y="224.0" font-size="12" text-anchor="end">0.4</text><line x1="60" y1="220.0" x2="640" y2="220.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/>
<text x="54" y="184.0" font-size="12" text-anchor="end">0.6</text><line x1="60" y1="180.0" x2="640" y2="180.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54" y="144.0" font-size="12" text-anchor="end">0.8</text><line x1="60" y1="140.0" x2="640" y2="140.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/>
<text x="54" y="104.0" font-size="12" text-anchor="end">1.0</text><line x1="60" y1="100.0" x2="640" y2="100.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54" y="64.0" font-size="12" text-anchor="end">1.2</text><line x1="60" y1="60.0" x2="640" y2="60.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/>
<text x="54" y="24.0" font-size="12" text-anchor="end">1.4</text><line x1="60" y1="20.0" x2="640" y2="20.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="60.0" y="236" font-size="12" text-anchor="middle">0</text><text x="156.7" y="236" font-size="12" text-anchor="middle">10</text>
<text x="253.3" y="236" font-size="12" text-anchor="middle">20</text><text x="350.0" y="236" font-size="12" text-anchor="middle">30</text><text x="446.7" y="236" font-size="12" text-anchor="middle">40</text><text x="543.3" y="236" font-size="12" text-anchor="middle">50</text>
<text x="640.0" y="236" font-size="12" text-anchor="middle">60</text><polyline points="84.2,100.0 132.5,41.5 180.8,91.0 229.2,52.4 277.5,49.0 325.8,47.3 374.2,86.2 422.5,79.7 470.8,161.8 519.2,179.2 567.5,182.6 615.8,175.2" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="84.2" cy="100.0" r="3" fill="#4a7bd0"/><circle cx="132.5" cy="41.5" r="3" fill="#4a7bd0"/>
<circle cx="180.8" cy="91.0" r="3" fill="#4a7bd0"/><circle cx="229.2" cy="52.4" r="3" fill="#4a7bd0"/><circle cx="277.5" cy="49.0" r="3" fill="#4a7bd0"/><circle cx="325.8" cy="47.3" r="3" fill="#4a7bd0"/>
<circle cx="374.2" cy="86.2" r="3" fill="#4a7bd0"/><circle cx="422.5" cy="79.7" r="3" fill="#4a7bd0"/><circle cx="470.8" cy="161.8" r="3" fill="#4a7bd0"/><circle cx="519.2" cy="179.2" r="3" fill="#4a7bd0"/>
<circle cx="567.5" cy="182.6" r="3" fill="#4a7bd0"/><circle cx="615.8" cy="175.2" r="3" fill="#4a7bd0"/><polyline points="84.2,100.0 132.5,134.9 180.8,100.4 229.2,109.8 277.5,106.3 325.8,109.3 374.2,120.5 422.5,111.1 470.8,107.5 519.2,126.6 567.5,104.0 615.8,103.1" fill="none" stroke="#d0564a" stroke-width="2"/><circle cx="84.2" cy="100.0" r="3" fill="#d0564a"/>
<circle cx="132.5" cy="134.9" r="3" fill="#d0564a"/><circle cx="180.8" cy="100.4" r="3" fill="#d0564a"/><circle cx="229.2" cy="109.8" r="3" fill="#d0564a"/><circle cx="277.5" cy="106.3" r="3" fill="#d0564a"/>
<circle cx="325.8" cy="109.3" r="3" fill="#d0564a"/><circle cx="374.2" cy="120.5" r="3" fill="#d0564a"/><circle cx="422.5" cy="111.1" r="3" fill="#d0564a"/><circle cx="470.8" cy="107.5" r="3" fill="#d0564a"/>
<circle cx="519.2" cy="126.6" r="3" fill="#d0564a"/><circle cx="567.5" cy="104.0" r="3" fill="#d0564a"/><circle cx="615.8" cy="103.1" r="3" fill="#d0564a"/><rect x="80" y="248" width="12" height="12" fill="#4a7bd0"/>
<text x="96" y="259" font-size="12">ORT CPU fp32: 마지막 15 s 평균/첫 창 0.60, CoV 27 %</text><rect x="390" y="248" width="12" height="12" fill="#d0564a"/><text x="406" y="259" font-size="12">Core ML ANE: 0.94, CoV 5.5 %</text><text x="350.0" y="282" font-size="12" text-anchor="middle">시간 (s) — 5 s 창별 처리량 ÷ 첫 창 처리량 (asr_encoder)</text>
</svg>
```

그림 7 — 5초 창별 처리량을 첫 창으로 나눈 값. 파랑(ORT CPU)은 40초 이후 0.6 근처로 떨어졌고, 빨강(Core ML ANE)은 0.83~1.0 사이에서 유지됐다.

출력에서 볼 것, 그리고 **정직한 해석**:

- ORT CPU의 처리량은 40초 지점부터 약 340 → 190 inf/s로 떨어졌다. 같은 시점에 load average가 10.4 → 15 → 23으로 올랐다. 그리고 열 경고는 실행 전후 모두 "기록 없음"이었다. 따라서 이 하락은 **열 throttling이 아니라 다른 프로세스와의 CPU 경합**으로 보는 것이 데이터와 맞다. "sustained/first = 0.60"을 열 성능이라고 보고하면 틀린 보고다.
- ANE 실행은 load가 21 → 13으로 높았는데도 CoV 5.5 %로 안정적이었다. p50은 0.36~0.38 ms로 거의 그대로이고, 흔들린 것은 p99(1.2~2.95 ms)다. 연산은 전용 하드웨어에서 하지만 호출·복사는 CPU에서 하기 때문에 경합이 **꼬리에만** 묻어난다.
- 팬리스 노트북에서 60초는 열이 쌓이기에 짧을 수 있다. 진짜 thermal 시험은 4.4절 ④처럼 10분 이상, 폼팩터에서, 표면 온도·클럭을 같이 기록해야 한다. **이 실험이 증명한 것은 "열 문제 없음"이 아니라 "이 측정으로는 열을 판정할 수 없음"**이다.

---

## 6. 점수화와 결정 — 한 숫자로 줄이기 전에

### 6.1 왜 한 숫자가 위험한가

경영진은 "그래서 어느 쪽?"이라고 묻는다. 여러 지표를 한 숫자로 합치는 흔한 방법은 **기준 대비 비율의 가중 기하평균**이다.

```
score(b) = ∏ₖ ( metricₖ(b) / metricₖ(base) ) ^ wₖ ,   ∑ wₖ = 1,   작을수록 좋음
log score(b) = ∑ₖ wₖ · log( metricₖ(b) / metricₖ(base) )
```

말로 하면: 각 지표를 기준 후보 대비 몇 배인지로 바꾸고(단위가 사라진다), 그 배수들을 가중치만큼 곱한다. 산술평균 대신 기하평균을 쓰는 이유는 비율의 평균이기 때문이다 — 하나가 2배, 하나가 0.5배면 기하평균은 1(같음)이지만 산술평균은 1.25(나쁨)가 되어 기준 선택에 따라 결론이 바뀐다. SPEC CPU의 종합 점수도 기하평균이다.

손으로 해 보자. 후보 A가 기준 대비 latency 0.5배, memory 4배이고 가중치가 (0.8, 0.2)라면:

```
log score = 0.8 · log 0.5 + 0.2 · log 4 = 0.8 · (−0.693) + 0.2 · 1.386 = −0.555 + 0.277 = −0.277
score = e^−0.277 = 0.76  → 기준보다 좋음
가중치 (0.5, 0.5)라면: 0.5 · (−0.693) + 0.5 · 1.386 = 0.347 → score 1.41 → 기준보다 나쁨
```

**같은 측정, 다른 가중치, 반대 결론.** 가중치는 제품 요구사항(I1 예산)에서 와야 하고, 결론이 가중치에 얼마나 민감한지를 같이 보여 줘야 한다.

정확도는 가중치에 넣지 않는다. "10 % 빠르면 1 pp 손해 봐도 된다" 같은 교환은 보통 성립하지 않기 때문에, **게이트**(예: fp32 대비 −1 pp 이내만 후보 자격)로 다룬다. MLPerf의 "FP32 대비 99 %" 규칙과 같은 생각이다.

### 6.2 실제 데이터로 — 점수, Pareto, 민감도

무엇을 확인하나: run1 결과로 (1) 정확도 게이트, (2) 4개 모델 기하평균 비율, (3) 시나리오별 가중치 점수, (4) p90-메모리 Pareto front, (5) 무작위 가중치 1만 개에서의 1등 빈도, (6) p90 가중치를 올려 가며 1등이 바뀌는 지점.

```python
# 후보(runtime/backend) 6개를 '기준 대비 비율의 가중 기하평균'으로 점수화 + Pareto + 가중치 민감도
import numpy as np, pandas as pd
df = pd.read_csv("results/summary_run1.csv")
df = df[df.backend != "torch_cpu"]                    # torch eager = 정확도 기준(reference), 배포 후보 아님
BASE, METRICS = "ort_cpu_fp32", ["p90", "init_s", "peakMiB", "sizeKiB"]
gate = df.groupby("backend").dacc_pp.min() >= -1.0    # 정확도는 가중치가 아니라 '통과 조건'
ratio = {}
for b, g in df.groupby("backend"):
    base = df[df.backend == BASE].set_index("model")
    r = g.set_index("model")[METRICS] / base[METRICS]          # 모델별 기준 대비 비율
    ratio[b] = np.exp(np.log(r).mean())                         # 4개 모델 기하평균
R = pd.DataFrame(ratio).T[gate]
print(R.round(2).to_string()); print()
W = {"always-on latency": [0.6, 0.1, 0.2, 0.1], "cold-start app": [0.2, 0.6, 0.1, 0.1],
     "memory-tight": [0.2, 0.1, 0.4, 0.3], "latency-only": [1, 0, 0, 0]}
for name, w in W.items():
    score = np.exp((np.log(R) * w).sum(axis=1)).sort_values()   # 낮을수록 좋음, 1.0 = 기준
    print(f"{name:<18} " + "  ".join(f"{b}={v:.2f}" for b, v in score.items()))
pts = R[["p90", "peakMiB"]].values                               # 2D Pareto: p90 vs 메모리
front = [b for i, b in enumerate(R.index)
         if not any((pts[j] <= pts[i]).all() and (pts[j] < pts[i]).any() for j in range(len(pts)))]
print("\nPareto front (p90, peakMiB):", front)
rng = np.random.default_rng(0)                                   # 가중치를 무작위로 1만 번 뽑아 1등 빈도
wins = pd.Series(0, index=R.index)
for w in rng.dirichlet(np.ones(4), 10000):
    wins[np.exp((np.log(R) * w).sum(axis=1)).idxmin()] += 1
print("win share over random weights:", (wins / 100).round(1).to_dict())
for wp in np.arange(0.5, 1.01, 0.1):                             # p90 가중치를 올려 가며 1등이 바뀌는 지점
    w = [wp] + [(1 - wp) / 3] * 3
    sc = np.exp((np.log(R) * w).sum(axis=1))
    print(f"w_p90={wp:.2f}: best={sc.idxmin():<16} ane={sc['coreml_fp16_ane']:.2f} int8={sc['ort_cpu_int8']:.2f}")
```

```text
                  p90  init_s  peakMiB  sizeKiB
coreml_fp16_ane  0.22   64.03     9.41     0.57
coreml_fp16_cpu  0.44   70.31     8.44     0.57
ort_coreml_ep    0.48    6.54     1.89     1.00
ort_cpu_fp32     1.00    1.00     1.00     1.00
ort_cpu_int8     0.35    0.72     0.93     0.38

always-on latency  ort_cpu_int8=0.46  ort_coreml_ep=0.89  coreml_fp16_ane=0.92  ort_cpu_fp32=1.00  coreml_fp16_cpu=1.36
cold-start app     ort_cpu_int8=0.60  ort_cpu_fp32=1.00  ort_coreml_ep=2.84  coreml_fp16_ane=10.64  coreml_fp16_cpu=12.74
memory-tight       ort_cpu_int8=0.57  ort_cpu_fp32=1.00  ort_coreml_ep=1.35  coreml_fp16_ane=2.33  coreml_fp16_cpu=2.57
latency-only       coreml_fp16_ane=0.22  ort_cpu_int8=0.35  coreml_fp16_cpu=0.44  ort_coreml_ep=0.48  ort_cpu_fp32=1.00

Pareto front (p90, peakMiB): ['coreml_fp16_ane', 'ort_cpu_int8']
win share over random weights: {'coreml_fp16_ane': 0.6, 'coreml_fp16_cpu': 0.0, 'ort_coreml_ep': 0.0, 'ort_cpu_fp32': 0.0, 'ort_cpu_int8': 99.4}
w_p90=0.50: best=ort_cpu_int8     ane=1.25 int8=0.47
w_p90=0.60: best=ort_cpu_int8     ane=0.89 int8=0.44
w_p90=0.70: best=ort_cpu_int8     ane=0.63 int8=0.42
w_p90=0.80: best=ort_cpu_int8     ane=0.45 int8=0.39
w_p90=0.90: best=coreml_fp16_ane  ane=0.32 int8=0.37
w_p90=1.00: best=coreml_fp16_ane  ane=0.22 int8=0.35
```

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="240" x2="610" y2="240" stroke="currentColor"/><line x1="70" y1="20" x2="70" y2="240" stroke="currentColor"/><text x="137.5" y="256" font-size="12" text-anchor="middle">0.2</text><line x1="137.5" y1="240" x2="137.5" y2="244" stroke="currentColor"/>
<text x="352.4" y="256" font-size="12" text-anchor="middle">0.5</text><line x1="352.4" y1="240" x2="352.4" y2="244" stroke="currentColor"/><text x="514.9" y="256" font-size="12" text-anchor="middle">1</text><line x1="514.9" y1="240" x2="514.9" y2="244" stroke="currentColor"/>
<text x="64" y="244.0" font-size="12" text-anchor="end">0.5</text><line x1="66" y1="240.0" x2="70" y2="240.0" stroke="currentColor"/><text x="64" y="202.7" font-size="12" text-anchor="end">1</text><line x1="66" y1="198.7" x2="70" y2="198.7" stroke="currentColor"/>
<text x="64" y="161.3" font-size="12" text-anchor="end">2</text><line x1="66" y1="157.3" x2="70" y2="157.3" stroke="currentColor"/><text x="64" y="106.7" font-size="12" text-anchor="end">5</text><line x1="66" y1="102.7" x2="70" y2="102.7" stroke="currentColor"/>
<text x="64" y="65.3" font-size="12" text-anchor="end">10</text><line x1="66" y1="61.3" x2="70" y2="61.3" stroke="currentColor"/><text x="64" y="24.0" font-size="12" text-anchor="end">20</text><line x1="66" y1="20.0" x2="70" y2="20.0" stroke="currentColor"/>
<polyline points="159.8,20 159.8,65.0 268.7,65.0 268.7,203.0 610,203.0" fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="6 4"/><circle cx="159.8" cy="65.0" r="6" fill="#3f9a6b"/><text x="167.8" y="59.0" font-size="12" text-anchor="start">coreml_fp16_ane</text><circle cx="322.4" cy="71.5" r="6" fill="#888"/>
<text x="330.4" y="85.5" font-size="12" text-anchor="start">coreml_fp16_cpu</text><circle cx="342.8" cy="160.7" r="6" fill="#888"/><text x="350.8" y="154.7" font-size="12" text-anchor="start">ort_coreml_ep</text><circle cx="514.9" cy="198.7" r="6" fill="#888"/>
<text x="506.9" y="188.7" font-size="12" text-anchor="end">ort_cpu_fp32</text><circle cx="268.7" cy="203.0" r="6" fill="#3f9a6b"/><text x="276.7" y="219.0" font-size="12" text-anchor="start">ort_cpu_int8</text><text x="340.0" y="276" font-size="12" text-anchor="middle">p90 latency (ORT CPU fp32 = 1, 4개 모델 기하평균, log)</text>
<text x="18" y="130.0" font-size="12" transform="rotate(-90 18 130.0)" text-anchor="middle">peak RSS 증가 (기준 = 1, log)</text><text x="606" y="34" font-size="12" text-anchor="end">초록 = Pareto front (왼쪽 아래가 좋음)</text>
</svg>
```

그림 8 — p90과 peak memory 증가분(둘 다 기준 대비 비율, log 축). 초록 점 두 개가 Pareto front다. 왼쪽 아래로 갈수록 좋다.

```svg
<svg viewBox="0 0 660 290" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="220" x2="620" y2="220" stroke="currentColor"/><line x1="60" y1="20" x2="60" y2="220" stroke="currentColor"/><text x="54" y="224.0" font-size="12" text-anchor="end">0.0</text><text x="54" y="195.4" font-size="12" text-anchor="end">0.2</text>
<text x="54" y="166.9" font-size="12" text-anchor="end">0.4</text><text x="54" y="138.3" font-size="12" text-anchor="end">0.6</text><text x="54" y="109.7" font-size="12" text-anchor="end">0.8</text><text x="54" y="81.1" font-size="12" text-anchor="end">1.0</text>
<text x="54" y="52.6" font-size="12" text-anchor="end">1.2</text><text x="54" y="24.0" font-size="12" text-anchor="end">1.4</text><text x="60.0" y="236" font-size="12" text-anchor="middle">0.5</text><text x="172.0" y="236" font-size="12" text-anchor="middle">0.6</text>
<text x="284.0" y="236" font-size="12" text-anchor="middle">0.7</text><text x="396.0" y="236" font-size="12" text-anchor="middle">0.8</text><text x="508.0" y="236" font-size="12" text-anchor="middle">0.9</text><text x="620.0" y="236" font-size="12" text-anchor="middle">1.0</text>
<line x1="60" y1="77.1" x2="620" y2="77.1" stroke="#888" stroke-dasharray="4 3"/><text x="616" y="72.1" font-size="12" text-anchor="end">기준 ORT CPU fp32 = 1</text><polyline points="60.0,41.4 116.0,68.6 172.0,92.9 228.0,112.9 284.0,130.0 340.0,144.3 396.0,155.7 452.0,165.7 508.0,174.3 564.0,181.4 620.0,188.6" fill="none" stroke="#d0564a" stroke-width="2"/><circle cx="60.0" cy="41.4" r="3" fill="#d0564a"/>
<circle cx="116.0" cy="68.6" r="3" fill="#d0564a"/><circle cx="172.0" cy="92.9" r="3" fill="#d0564a"/><circle cx="228.0" cy="112.9" r="3" fill="#d0564a"/><circle cx="284.0" cy="130.0" r="3" fill="#d0564a"/>
<circle cx="340.0" cy="144.3" r="3" fill="#d0564a"/><circle cx="396.0" cy="155.7" r="3" fill="#d0564a"/><circle cx="452.0" cy="165.7" r="3" fill="#d0564a"/><circle cx="508.0" cy="174.3" r="3" fill="#d0564a"/>
<circle cx="564.0" cy="181.4" r="3" fill="#d0564a"/><circle cx="620.0" cy="188.6" r="3" fill="#d0564a"/><polyline points="60.0,152.9 116.0,154.3 172.0,157.1 228.0,158.6 284.0,160.0 340.0,161.4 396.0,164.3 452.0,165.7 508.0,167.1 564.0,168.6 620.0,170.0" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="60.0" cy="152.9" r="3" fill="#4a7bd0"/>
<circle cx="116.0" cy="154.3" r="3" fill="#4a7bd0"/><circle cx="172.0" cy="157.1" r="3" fill="#4a7bd0"/><circle cx="228.0" cy="158.6" r="3" fill="#4a7bd0"/><circle cx="284.0" cy="160.0" r="3" fill="#4a7bd0"/>
<circle cx="340.0" cy="161.4" r="3" fill="#4a7bd0"/><circle cx="396.0" cy="164.3" r="3" fill="#4a7bd0"/><circle cx="452.0" cy="165.7" r="3" fill="#4a7bd0"/><circle cx="508.0" cy="167.1" r="3" fill="#4a7bd0"/>
<circle cx="564.0" cy="168.6" r="3" fill="#4a7bd0"/><circle cx="620.0" cy="170.0" r="3" fill="#4a7bd0"/><line x1="452.0" y1="20" x2="452.0" y2="220" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/><text x="446.0" y="34" font-size="12" text-anchor="end">1등이 바뀌는 지점 w≈0.85</text>
<rect x="100" y="248" width="12" height="12" fill="#d0564a"/><text x="116" y="259" font-size="12">coreml_fp16_ane 점수</text><rect x="320" y="248" width="12" height="12" fill="#4a7bd0"/><text x="336" y="259" font-size="12">ort_cpu_int8 점수 (낮을수록 좋음)</text>
<text x="340.0" y="282" font-size="12" text-anchor="middle">p90 가중치 w (나머지 1−w를 init·메모리·크기에 균등 분배)</text>
</svg>
```

그림 9 — p90 가중치 w에 따른 두 후보의 점수. w ≈ 0.85에서 1등이 바뀐다.

출력에서 볼 것:

- **정확도 게이트**는 다섯 후보 모두 통과했다(최악 −0.39 pp).
- 무작위 가중치 1만 개 중 99.4 %에서 ORT CPU int8이 1등이다. 겉보기에는 "압도적"이다.
- 하지만 latency만 보면 Core ML ANE가 0.22로 int8(0.35)보다 1.6배 좋다. 가중치 스윕에서 p90 비중이 0.8과 0.9 사이(그림 9의 0.05 간격 계산에서는 0.85에서 두 점수가 0.38로 같아진다)를 넘어야 ANE가 이긴다.
- ANE를 끌어내린 주범은 **init 64배**와 **peak memory 9.4배**다. 그런데 5.5절 (6)에서 봤듯이 peak memory 9.4배의 대부분은 Core ML 프레임워크의 1회성 로드 비용이지 모델의 메모리가 아니다. init 64배도 첫 실행의 컴파일 비용이라 캐시하면 크게 줄어드는 항목이다. 즉 **"99.4 % 압도"라는 결론의 상당 부분이 측정 정의의 산물**이다.
- 또 ORT CPU fp32(기준)가 run1에서 경합을 크게 받아 p90이 부풀었기 때문에, 그 기준으로 나눈 모든 비율이 기울어 있다. 기준 행이 흔들리면 모든 점수가 흔들린다.

여기서 얻는 결정 원칙:

1. **Pareto front를 먼저 보여 준다.** "이 두 후보 중 하나이고, 나머지는 어떤 가중치로도 지는 후보"라는 사실은 가중치와 무관하다.
2. **점수는 가중치 범위와 함께 보여 준다.** "p90 비중이 85 % 이상이면 A, 아니면 B"는 경영진이 결정할 수 있는 문장이다. 한 숫자만 주면 그 결정권을 빼앗는다.
3. **지표의 정의를 점수보다 먼저 검토한다.** 측정이 잘못된 지표에 가중치를 주면 정교한 오답이 나온다.
4. **기준(baseline)은 안정적인 것으로 고른다.** 잡음이 큰 행을 기준으로 삼지 않는다.

### 6.3 정규화 방법 비교

| 방법 | 식 | 장점 | 함정 |
|---|---|---|---|
| 기준 대비 비율 + 기하평균 | ∏ (m/m_base)^w | 기준 선택에 순위가 덜 민감, 단위 무관 | 기준 행의 잡음이 전부에 퍼짐 |
| min-max 정규화 | (m − min)/(max − min) | 0~1로 보기 좋음 | 후보 하나 추가하면 모든 점수가 바뀜 |
| 예산 대비 비율 | m / budget | **제품 요구와 직결** (I1) | 예산이 정해져 있어야 함 |
| 합격/불합격 | m ≤ budget | 단순, 게이트로 좋음 | 여유(margin) 정보를 잃음 |

웨어러블 HW 선정이라면 "예산 대비 비율"을 추천한다. 예를 들어 KWS p99 예산이 10 ms라면 p99 3.56 ms는 0.36(64 % 여유)이다. Don이 RF 칩 sign-off에서 하던 "spec 대비 margin"과 같은 표현이고, 경영진도 바로 읽는다.

---

## 7. 재현성과 보고

### 7.1 환경 지문 — 숫자에 붙이는 출생증명서

F8에서 버전 불일치 문제를 다뤘다. 벤치마크에서는 결과 파일마다 다음을 함께 저장한다.

무엇을 확인하나: 결과와 함께 저장할 환경 정보(하드웨어, OS, 패키지 버전, 전원, 열, load, 모델 파일 해시)를 한 번에 모은다.

```python
# 결과 파일마다 붙일 '측정 환경 지문' — 이게 없으면 숫자는 나중에 해석 불가
import platform, subprocess, hashlib, os, sys, importlib.metadata as md
sh = lambda *c: subprocess.run(c, capture_output=True, text=True).stdout.strip()
def sha(path):                         # 모델 파일(또는 mlpackage 폴더)의 내용 해시 앞 12자리
    h = hashlib.sha256()
    files = [path] if os.path.isfile(path) else sorted(
        os.path.join(d, f) for d, _, fs in os.walk(path) for f in fs)
    for f in files:
        h.update(open(f, "rb").read())
    return h.hexdigest()[:12]
env = {
    "cpu": sh("sysctl", "-n", "machdep.cpu.brand_string"), "ncpu": os.cpu_count(),
    "os": platform.platform(), "python": sys.version.split()[0],
    "pkgs": {p: md.version(p) for p in ["numpy", "torch", "onnxruntime", "coremltools"]},
    "power": sh("pmset", "-g", "batt").splitlines()[0],
    "thermal": sh("pmset", "-g", "therm").splitlines()[0],
    "loadavg": sh("sysctl", "-n", "vm.loadavg"),
    "threads": 4,
    "models": {f: sha("art/" + f) for f in ["asr_encoder.onnx", "asr_encoder_int8.onnx",
                                            "asr_encoder_fp16.mlpackage"]},
}
for k, v in env.items():                 # 실제로는 json.dump로 결과 파일 옆에 저장
    print(f"{k:<8} {v}")
```

```text
cpu      Apple M2
ncpu     8
os       macOS-26.4.1-arm64-arm-64bit
python   3.9.6
pkgs     {'numpy': '2.0.2', 'torch': '2.8.0', 'onnxruntime': '1.19.2', 'coremltools': '9.0'}
power    Now drawing from 'Battery Power'
thermal  Note: No thermal warning level has been recorded
loadavg  { 4.25 6.45 12.98 }
threads  4
models   {'asr_encoder.onnx': '197ea60d5b60', 'asr_encoder_int8.onnx': 'd8d587a4419f', 'asr_encoder_fp16.mlpackage': 'fcca44a67080'}
```

출력에서 볼 것: 이 측정은 **배터리 전원**에서 했고, 1분·5분·15분 load average가 4.3·6.5·13.0이었다(15분 평균이 높다 = 조금 전까지 부하가 컸다). 이 두 줄만으로도 이 노트의 숫자가 "깨끗한 측정"이 아니라는 것이 기록된다. 전원 어댑터 연결 여부와 저전력 모드는 노트북·폰 모두에서 성능을 바꿀 수 있는 조건이라 반드시 남긴다. 모델 해시는 "같은 이름의 다른 파일"을 잡아낸다. 실제 기기라면 여기에 SoC 이름·펌웨어 빌드·런타임 라이브러리 버전·클럭 governor·시작 온도·컴파일러 플래그를 더한다.

### 7.2 같은 빌드를 두 번 — A/A 테스트로 잡음 바닥 재기

회귀 CI(O3)를 만들기 전에 해야 할 일: **아무것도 안 바꾸고 두 번 돌려서 얼마나 다르게 나오는지** 본다. 이걸 A/A 테스트라고 한다.

무엇을 확인하나: run1과 run2(코드·모델 동일)의 p50 비율과, run 내부 300개 샘플로 만든 bootstrap 95 % 신뢰구간. bootstrap은 "가진 샘플에서 복원추출로 같은 크기의 샘플을 수천 번 다시 뽑아 통계량의 흔들림을 보는 방법"이다.

```python
# run1(기준) 대비 run2의 p50 비율과 bootstrap 95 % CI -> "진짜 회귀"만 표시
import json, numpy as np
rng = np.random.default_rng(0)
load = lambda f: {(d["model"], d["backend"]): np.array(d["lat_ms"])
                  for d in map(json.loads, open(f))}
A, B = load("results/raw_run1.jsonl"), load("results/raw_run2.jsonl")
def boot_ratio(a, b, n=2000):          # median(b)/median(a)의 분포를 재표본으로
    ia = rng.integers(0, len(a), (n, len(a))); ib = rng.integers(0, len(b), (n, len(b)))
    r = np.median(b[ib], axis=1) / np.median(a[ia], axis=1)
    return np.percentile(r, [2.5, 97.5])
flags = {True: 0, False: 0}
for k in A:
    ratio = np.median(B[k]) / np.median(A[k]); lo, hi = boot_ratio(A[k], B[k])
    verdict = ("REGRESSION" if lo > 1.10 else "improved" if hi < 0.90 else
               "within noise" if lo <= 1 <= hi else "small shift")
    flags[verdict == "REGRESSION"] += 1
    if k[0] in ("asr_encoder", "kws_dscnn"):
        print(f"{k[0]:<12} {k[1]:<16} x{ratio:4.2f}  CI [{lo:4.2f}, {hi:4.2f}]  {verdict}")
print(f"\n{len(A)}개 조합 중 'REGRESSION'(CI 하한 > 1.10): {flags[True]}개 — 코드 변경 없이 같은 빌드를 두 번 돌렸다")
```

```text
kws_dscnn    torch_cpu        x1.03  CI [0.99, 1.08]  within noise
kws_dscnn    ort_cpu_fp32     x0.86  CI [0.85, 0.87]  improved
kws_dscnn    ort_cpu_int8     x0.79  CI [0.73, 0.86]  improved
kws_dscnn    ort_coreml_ep    x0.96  CI [0.95, 0.97]  small shift
kws_dscnn    coreml_fp16_cpu  x1.20  CI [1.17, 1.22]  REGRESSION
kws_dscnn    coreml_fp16_ane  x0.96  CI [0.74, 1.02]  within noise
asr_encoder  torch_cpu        x1.18  CI [1.13, 1.20]  REGRESSION
asr_encoder  ort_cpu_fp32     x0.56  CI [0.48, 0.64]  improved
asr_encoder  ort_cpu_int8     x0.79  CI [0.70, 0.85]  improved
asr_encoder  ort_coreml_ep    x0.86  CI [0.82, 0.90]  improved
asr_encoder  coreml_fp16_cpu  x0.83  CI [0.77, 0.91]  small shift
asr_encoder  coreml_fp16_ane  x0.97  CI [0.91, 0.99]  small shift

24개 조합 중 'REGRESSION'(CI 하한 > 1.10): 3개 — 코드 변경 없이 같은 빌드를 두 번 돌렸다
```

출력에서 볼 것: 아무것도 바꾸지 않았는데 24개 중 3개가 "통계적으로 유의한 10 % 이상 회귀"로 판정됐고, 여러 개가 "개선"으로 판정됐다. bootstrap CI는 **run 안의 샘플 흔들림**만 반영한다. 실행 시각마다 바뀌는 배경 부하·열·클럭 같은 **run 사이의 흔들림**은 300개 샘플을 아무리 재표본해도 안 보인다. SSD 시험에서 "같은 드라이브를 다른 날 재면 QoS가 다르다"와 같다.

그래서 회귀 CI의 실무 규칙:

- **A/A로 잡음 바닥을 먼저 잰다.** 같은 빌드를 N번(예: 5번, 다른 시각) 돌려 run 간 변동(예: p50의 run 간 CoV)을 구하고, 회귀 임계값을 그보다 충분히 크게 잡는다.
- **기준과 후보를 번갈아(ABAB) 돌린다.** 시간에 따라 바뀌는 환경 요인이 양쪽에 고르게 들어간다.
- **판정은 여러 run의 요약으로**: run별 p50을 한 점으로 보고, 기준 run들과 후보 run들을 비교한다.
- **전용 장비·고정 클럭**: CI용 기기는 다른 일을 하지 않게 한다(O3의 device farm).
- **경보는 2단계로**: "의심"(1회 초과)과 "확정"(재실행에서도 재현). 펌웨어 CI에서 flaky test를 다루는 방식과 같다.

### 7.3 원시 데이터 보관과 릴리스별 추적

- **요약만 저장하지 않는다.** 이 노트의 `raw.jsonl`처럼 반복별 원시 latency를 남겨야 나중에 다른 분위수, 다른 통계, 분포 그림을 다시 만들 수 있다. 7.2절 비교도 원시 데이터가 있어서 가능했다.
- **결과 = (원시 데이터, 요약, 환경 지문, 스위트 버전)** 네 묶음을 한 ID로 묶는다. 스위트 자체(모델 목록, 반복 수, 입력)를 바꾸면 스위트 버전을 올리고, 버전이 다른 결과끼리는 직접 비교하지 않는다(MLPerf 라운드끼리 직접 비교하지 않는 것과 같다).
- **릴리스별 대시보드**: x축 릴리스, y축 지표(p99, mJ/inference, 정확도), 예산선을 같이 그린다. 예산선에 가까워지는 추세가 회귀 경보보다 먼저 위험을 알려 준다(O3).

### 7.4 보고서 한 장의 구조

| 순서 | 내용 |
|---|---|
| 1 | 결론 한 문장과 결정이 필요한 사항 ("p90 비중이 85 % 이상이면 A, 아니면 B") |
| 2 | Pareto 그림과 예산 대비 margin 표 |
| 3 | 워크로드 × 후보 결과표 (p50/p90/p99, n, init, 첫 추론, 메모리, 크기, 정확도 delta) |
| 4 | 측정 조건과 환경 지문, 알려진 측정 한계 ("peak memory는 프레임워크 포함") |
| 5 | 잡음 수준 (A/A 결과), 반복 run 수 |
| 6 | 미측정 항목과 다음 단계 (예: "energy는 폼팩터 시제품 + 전력 분석기로 측정 예정") |

---

## 8. 임베디드 관점에서 다시 보기

### 8.1 MCU·DSP에서 같은 하니스를 어떻게

| 하니스 요소 | 이 노트(Python) | 기기 위 |
|---|---|---|
| 타이머 | `time.perf_counter()` | Cortex-M DWT `CYCCNT`, Xtensa `CCOUNT`, ARM PMU (K2) |
| 외부 관측 | 없음 | GPIO 토글 + 로직 분석기/오실로스코프 (타이머 오버헤드 0, 외부에서 검증) |
| 에너지 | 미측정 | PPK2·Joulescope·Monsoon으로 GPIO 구간의 ∫(P − P_idle)dt (D7, K3) |
| 메모리 | `ru_maxrss` (거침) | map 파일, stack painting, TFLM `arena_used_bytes` (K1) |
| 결과 전송 | JSON 한 줄 | UART/RTT로 요약 + 히스토그램 덤프 |
| 원시 데이터 | 300개 배열 | SRAM이 부족하면 **히스토그램**으로 압축 |

MCU에서는 latency 10만 개를 저장할 SRAM이 없다. 그래서 펌웨어 쪽 하니스는 고정 크기 히스토그램에 누적하고 분위수를 근사한다. HdrHistogram과 같은 아이디어다.

무엇을 확인하나: log2 구간마다 4칸으로 나눈 512바이트 히스토그램에 10만 개 cycle 값을 넣고 p50/p90/p99의 상한을 구한다. 정상 96k cycle(96 MHz에서 1 ms) + 3 % 확률의 +960k cycle(10 ms) 스파이크.

```c
/* 펌웨어용 고정 메모리 latency 히스토그램: cycle 값을 log2 + 4 sub-bucket으로 누적, p50/p90/p99 근사 */
#include <stdint.h>
#include <stdio.h>
#define SUB 4                                  /* 2배 구간마다 4칸 -> 상대 오차 약 25 % 이내 */
#define NB  (32 * SUB)
static uint32_t hist[NB], total;
static unsigned bucket(uint32_t c) {
    if (c < SUB) return c;
    unsigned msb = 31u - (unsigned)__builtin_clz(c);           /* floor(log2 c) */
    unsigned sub = (c >> (msb - 2)) & (SUB - 1);              /* msb 아래 2비트 */
    return (msb - 1) * SUB + sub;
}
static uint32_t bucket_hi(unsigned b) {                         /* bucket의 상한(보수적 보고) */
    if (b < SUB) return b;
    unsigned msb = b / SUB + 1, sub = b % SUB;
    return ((uint32_t)(SUB + sub + 1) << (msb - 2)) - 1;
}
static void record(uint32_t cycles) { hist[bucket(cycles)]++; total++; }
static uint32_t pct(double p) {
    uint32_t need = (uint32_t)(p * total + 0.999999), acc = 0;
    for (unsigned b = 0; b < NB; b++) if ((acc += hist[b]) >= need) return bucket_hi(b);
    return 0;
}
int main(void) {
    uint32_t s = 12345u;                                         /* LCG 의사난수 (seed 고정) */
    for (int i = 0; i < 100000; i++) {
        s = s * 1664525u + 1013904223u;
        uint32_t c = 96000u + (s >> 20);                         /* 정상: ~96k cycle (1 ms @96 MHz) */
        if ((s >> 8) % 100 < 3) c += 960000u;                    /* 3 %: +10 ms 스파이크 */
        record(c);
    }
    printf("n=%u  memory=%zu bytes\n", total, sizeof hist);
    printf("p50 <= %u  p90 <= %u  p99 <= %u cycles\n", pct(0.50), pct(0.90), pct(0.99));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 lathist.c -o lathist && ./lathist
```

```text
n=100000  memory=512 bytes
p50 <= 98303  p90 <= 114687  p99 <= 1310719 cycles
```

출력에서 볼 것: 512바이트로 10만 개 분포를 요약했다. 손으로 확인해 보자. 정상 값은 96000~100095 cycle이다. 이 구간의 최상위 비트는 2¹⁶(65536)이고, 그 아래 2비트로 4칸을 나누면 한 칸 폭은 16384다. 96000은 81920~98303 칸에, 98304 이상은 98304~114687 칸에 든다. 그래서 p50 ≤ 98303, p90 ≤ 114687로 **칸의 상한**이 보고된다. 스파이크(약 1,056,000)는 2²⁰ 구간의 첫 칸(1,048,576~1,310,719)에 들어서 p99 ≤ 1,310,719다. 상한을 보고하는 이유는 deadline 판정에서 **낙관적 오차보다 보수적 오차가 안전**하기 때문이다. 정밀도가 더 필요하면 `SUB`를 8, 16으로 올린다(메모리 2배, 4배).

`__builtin_clz`는 GCC·Clang 내장 함수로, Cortex-M3 이상에서는 `CLZ` 명령 한 개로 컴파일된다. ISR 안에서 써도 될 만큼 싸다.

### 8.2 벤치마크가 보드 선택을 바꾸는 장면

웨어러블 HW 평가에서 이 노트의 개념이 실제로 쓰이는 순서를 정리하면 이렇다.

1. 벤더 자료와 MLPerf Tiny/Mobile 결과로 후보를 3~4개로 거른다(2장).
2. TOPS·대역폭·SRAM으로 우리 모델의 하한 latency를 손계산한다(3.2절, D3).
3. 평가 보드에 **우리 스위트**를 이식한다: 각 벤더 툴체인으로 같은 ONNX/TFLite를 변환하고 partition·fallback 로그를 남긴다(5.6절).
4. 시나리오 ①~④를 돌린다. 특히 ③ 동시 실행과 ④ 10분 sustained는 MLPerf에 없으니 직접 만든다.
5. 정확도 게이트 → Pareto → 가중치 범위로 보고한다(6장).
6. 선택된 칩의 스위트를 그대로 회귀 CI로 옮긴다(7장, O3).

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 평균만 보고 | 데모는 되는데 현장에서 가끔 끊김 | 3 % 스파이크가 평균·p90에 안 보임 | 사건 빈도에 맞는 분위수(p99, max)와 deadline miss 수 보고 |
| 샘플 100개로 p99 보고 | 재측정할 때마다 p99가 몇 배씩 바뀜 | p99 위 샘플이 1개 | p99면 1000개 이상, CI와 n을 같이 표기 |
| init·첫 추론을 warm-up에 버림 | 앱 첫 응답만 느리다는 버그 | 컴파일·캐시 비용을 숨김 | init, first, steady를 별도 열로 |
| Offline throughput을 latency 대신 인용 | 웨어러블 KWS가 기대보다 3배 느림 | 시나리오 불일치 | 숫자마다 시나리오·batch 명시 |
| 랜덤 가중치 모델로 정확도 비교 | 모든 backend가 일치율 100 % | 모델이 항상 같은 클래스를 냄 | 라벨 있는 데이터, 학습된 모델, 클래스 분포 확인 |
| 프로세스 전체 RSS를 모델 메모리로 사용 | 작은 모델이 160 MiB를 쓴다는 결론 | 프레임워크 로드 비용 포함 | 런타임 arena·증분 측정, 측정 정의를 표에 명시 |
| 잡음 큰 행을 기준으로 정규화 | 모든 점수가 한쪽으로 기움 | baseline 자체가 경합을 받음 | 안정적인 기준 선택, 기준도 여러 번 측정 |
| 경합을 thermal로 오해 | "sustained 0.6"을 열 성능으로 보고 | 원인 시계열을 안 남김 | load·온도·클럭을 처리량과 같이 기록 |
| run 내부 CI로 회귀 판정 | 코드 변경 없이 회귀 경보 | run 간 분산 무시 | A/A 테스트, ABAB 실행, 여러 run 요약 |
| 런타임 경로를 안 적음 | 같은 "CoreML"인데 23배 차이 | EP partition vs 직접 변환 | 변환 경로, partition 수, fallback op 기록 |

---

## 10. 면접에서 이렇게 말한다

**Q.** "What does MLPerf Tiny measure?"

**A.** MCU 급 기기의 추론 latency, 선택적으로 추론당 에너지, 그리고 정해진 정확도 목표를 넘는지를 잰다. 처음 네 벤치마크는 keyword spotting(DS-CNN, Speech Commands), visual wake words(MobileNetV1 0.25×, 96×96), image classification(ResNet-8, CIFAR-10), anomaly detection(autoencoder, ToyADMOS)이다. 호스트 러너가 기기에 입력을 보내고 결과를 받는 EEMBC 계열 하니스를 쓰고, 에너지는 전력 측정 장비로 잰다. 정확도 목표가 있어서 "빠르지만 틀린" 결과는 인정되지 않는다. 다만 모델이 오래됐고 전처리·streaming·후처리를 포함하지 않으니 우리 제품 판단은 우리 워크로드로 다시 재야 한다.

> "MLPerf Tiny measures inference latency, and optionally energy per inference, on microcontroller-class devices, with a required accuracy target so you can't trade correctness for speed. The original four benchmarks are keyword spotting with a DS-CNN on Speech Commands, visual wake words with a MobileNet at 96 by 96, image classification with a small ResNet on CIFAR-10, and anomaly detection with an autoencoder on ToyADMOS. A host runner drives the device, and energy is measured with a power monitor. It's a good first filter, but the models are reference models, and it doesn't include our feature extraction, streaming or post-processing — so the final decision comes from running our own workloads."

**Q.** "A vendor claims 10 TOPS — how do you know if it's good for us?"

**A.** 우선 조건을 묻는다: INT4인지 INT8인지, sparsity 크레딧이 들어갔는지, peak 클럭인지 sustained인지, NPU 코어만인지. 그걸로 우리 정밀도의 dense 실효 TOPS를 계산하고, 우리 모델의 MAC 수와 현실적인 활용률(작은 batch면 20~40 %)로 latency 하한을 낸다. 동시에 roofline으로 weight·activation 이동이 대역폭에 묶이는지 본다 — LLM decode면 TOPS보다 DRAM 대역폭이 결정한다. 예를 들어 우리 104 M MAC encoder로 계산하면 headline 0.02 ms가 현실적으로 0.7 ms, 즉 headline의 3 % 수준이었다. 마지막으로 벤더에게 우리 ONNX 파일을 주고 batch 1 end-to-end p90, init time, 정확도 delta, 10분 sustained 결과를 요구한다.

> "I'd first ask what the number assumes — INT4 or INT8, whether sparsity is counted, peak or sustained clock, and whether it's just the NPU core. That gives me dense TOPS at our precision. Then I divide our model's operations by that, with a realistic utilization — for batch-one inference often twenty to forty percent — and check the roofline, because if weights stream from DRAM, bandwidth rather than TOPS sets the limit, which is always the case for LLM decode. When I did this for a hundred-million-MAC encoder, the headline implied twenty microseconds and a realistic estimate was about seven hundred — three percent of the headline. Then I'd hand the vendor our ONNX model and ask for batch-one end-to-end p90, init time, accuracy delta and a ten-minute sustained run."

**Q.** "Design a benchmark suite for our wearable."

**A.** 제품 기능에서 워크로드를 뽑는다: always-on KWS·IMU 제스처·착용/존재 감지, wake 이후 ASR encoder, SLM prefill·decode. 각 워크로드에 지표를 붙인다 — always-on은 p99와 추론당 에너지와 평균 전력, burst는 wake부터 첫 토큰까지의 시간과 init 포함 여부, LLM은 TTFT·tokens/s·peak memory, 전부에 정확도 delta 게이트. 시나리오는 single-stream always-on, wake 후 burst, 그 둘의 동시 실행(always-on의 deadline miss를 본다), 10분 sustained. 측정은 새 프로세스, init·첫 추론 분리, warm-up, p99를 위한 충분한 반복, 고정 클럭·스레드, 온도·전원·load 기록. 결과는 원시 데이터와 환경 지문까지 저장하고, 예산 대비 margin 표와 Pareto로 보고하고, 같은 스위트를 회귀 CI로 쓴다.

> "I'd derive the workloads from product features: always-on keyword spotting, IMU gesture and presence detection; an ASR encoder after wake; and SLM prefill and decode. Each gets metrics that match its role — p99 latency, energy per inference and average power for always-on; wake-to-first-token time for the burst path, with init cost explicit; time to first token, tokens per second and peak memory for the LLM; and an accuracy delta gate for everything. Scenarios are single-stream always-on, burst after wake, both running concurrently — to catch deadline misses on the always-on path — and a ten-minute sustained run in the real form factor. Methodology: fresh process per run, init and first inference recorded separately, warm-up, enough iterations for p99, fixed clocks and threads, and logging temperature, power source and background load. I'd keep raw data with an environment fingerprint, report margin against budget plus a Pareto view, and reuse the same suite as regression CI."

**Q.** "Why p90 or p99 instead of the mean?"

**A.** 실시간 시스템은 평균이 아니라 deadline을 넘는 빈도로 실패하기 때문이다. 97 %는 2 ms, 3 %는 12 ms인 분포면 평균 2.3 ms, p90 2.2 ms로 10 ms deadline에 여유가 있어 보이지만 실제로는 2.7 %가 deadline을 놓친다. 분위수는 사건 빈도에 맞춰 고른다 — 초당 100번 도는 오디오 루프에서 1 %는 매초 한 번이다. 그리고 p99는 샘플이 적으면 아주 불안정하다. 100개로 잰 p99는 2 ms에서 14 ms까지 흔들렸고, 1000개 이상이 필요하다. 그래서 분위수와 함께 반복 수와 신뢰구간을 보고한다. MLPerf가 SingleStream에 90번째 백분위를 쓰는 것도 같은 이유다.

> "Because real-time systems fail on how often they miss a deadline, not on the average. In a distribution where ninety-seven percent of runs take two milliseconds and three percent take twelve, the mean is 2.3 and even p90 is 2.2, which looks like plenty of headroom against a ten-millisecond deadline — but almost three percent of frames miss it. I pick the percentile from the event rate: in an audio loop running a hundred times a second, one percent is once every second. And tail percentiles need samples: p99 from a hundred runs swung from two to fourteen milliseconds in my simulation, so I use at least a thousand and report n and a confidence interval. That's also why MLPerf reports ninetieth-percentile latency for single-stream."

**Q.** "Peak vs sustained — how do you test it?"

**A.** 같은 워크로드를 실제 폼팩터에서 최소 10분, 가능하면 제품 duty cycle로 돌리면서 짧은 창(예: 5초)마다 처리량과 p99를 기록하고, 같은 타임라인에 온도·클럭·배경 부하를 남긴다. 결과는 "마지막 구간 / 첫 구간" 비율과 창 간 변동계수로 보고한다. 중요한 건 원인 구분이다. 내가 노트북에서 60초 시험을 했을 때 CPU backend 처리량이 0.6배로 떨어졌지만 열 경고는 없었고 같은 시점에 load average가 두 배가 됐다 — 열이 아니라 경합이었다. 원인 시계열이 없으면 이걸 열 성능이라고 잘못 보고하게 된다. 주변 온도도 챔버로 고정하고, 개발보드의 방열판은 떼거나 폼팩터로 시험한다.

> "Run the workload continuously for at least ten minutes, ideally with the product's duty cycle and in the real enclosure, logging throughput and p99 per short window — say five seconds — alongside temperature, clock frequency and background load on the same timeline. Report the ratio of the last window to the first, plus window-to-window variation. The key is attributing the cause. When I ran a sixty-second test on a laptop, the CPU backend dropped to sixty percent, but there was no thermal warning and the load average doubled at the same moment — it was contention, not throttling. Without those side channels I'd have reported it as a thermal result. I'd also control ambient temperature and avoid dev boards with oversized heat sinks."

**Q.** "Two runs of the same build disagree by 20 %. How do you set up regression detection?"

**A.** 먼저 A/A 테스트로 잡음 바닥을 잰다 — 코드를 안 바꾸고 여러 번, 다른 시각에 돌려서 run 간 변동을 본다. 실제로 같은 빌드를 두 번 돌렸을 때 24개 조합 중 3개가 run 내부 bootstrap CI 기준으로 10 % 이상 "회귀"로 판정됐다. run 내부 신뢰구간은 run 사이의 환경 변화를 반영하지 못하기 때문이다. 그래서 전용 기기에 클럭을 고정하고, 기준과 후보를 번갈아 여러 번 돌리고, run별 요약값끼리 비교하고, 임계값은 A/A 변동보다 크게 잡는다. 경보는 재실행에서도 재현될 때만 확정한다.

> "Start with an A/A test: run the identical build several times at different times of day and measure the run-to-run spread. When I ran the same build twice, three of twenty-four configurations were flagged as more-than-ten-percent regressions by a within-run bootstrap interval, because that interval only captures sample noise, not environment drift between runs. So I'd use a dedicated device with fixed clocks, interleave baseline and candidate runs, compare per-run summaries, set the threshold above the A/A spread, and only confirm an alert if a rerun reproduces it."

---

## 11. 직접 해보기

1. **손계산**: 벤더가 "4 TOPS (INT8, dense, peak)"라고 한다. sustained 75 %, 활용률 40 %, 고정 오버헤드 0.2 ms일 때 presence_vww(10.66 M MAC)의 latency를 추정하라. 정답: 21.32 M op ÷ (4 × 0.75 × 0.4 = 1.2 TOPS) = 0.0178 ms, 오버헤드 포함 약 0.218 ms — 오버헤드가 약 92 %를 차지하므로 이 모델에서는 TOPS보다 호출 오버헤드를 먼저 줄여야 한다.
2. **손계산**: 참조 모델 top-1이 91.8 %이고 MLPerf 식 "99 % 목표"를 쓰면 허용 하한은? 우리 KWS 기준이 "−0.5 pp 이내"라면 어느 쪽이 엄격한가? 정답: 0.99 × 91.8 = 90.88 %(−0.92 pp 허용). −0.5 pp 기준이 더 엄격하다.
3. **코드**: 1.4절 예제에서 스파이크 확률을 0.5 %로 바꾸고, p99가 스파이크를 잡는지와 p99.9가 필요한지 확인하라. 힌트: 스파이크가 0.5 %면 p99는 정상 구간에 머문다. 사건 빈도보다 바깥 분위수를 봐야 한다.
4. **코드**: `run_suite.py`에 "p99 / p50" 열(꼬리 비율)을 추가하고 backend별로 비교하라. 힌트: run1에서 kws_dscnn ort_cpu_int8은 3.56 / 0.119 ≈ 30으로 꼬리가 몸통의 30배였다.
5. **코드**: 6.2절 점수에서 `peakMiB`를 빼고(가중치 0) 무작위 가중치 1등 빈도를 다시 계산하라. 측정 정의가 의심스러운 지표 하나가 결론을 얼마나 움직이는지 보는 연습이다. 힌트: `METRICS`에서 `peakMiB`를 지우고 Dirichlet 차원을 3으로 바꾼다.
6. **설계**: 4.4절 시나리오 ③(동시 실행)을 이 Mac에서 흉내 내라. 한 프로세스는 KWS를 10 ms마다, 다른 프로세스는 ASR encoder를 연속 실행하게 하고, KWS의 p99와 10 ms deadline miss 수를 단독 실행과 비교하라. 힌트: KWS 쪽 루프는 `time.sleep`이 아니라 절대 시각 기준으로 다음 시작 시각을 계산해야 drift가 안 쌓인다(D6).

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| MLCommons | MLPerf 운영 컨소시엄 | 규칙 제정, 결과 검토·공개 |
| MLPerf Tiny | MCU 급 추론 벤치마크 | KWS·VWW·IC·AD, latency·energy·정확도 목표 |
| MLPerf Mobile | 모바일 SoC 벤치마크 앱 | 벤더 backend로 task별 latency·throughput |
| MLPerf Inference | edge·datacenter 추론 벤치마크 | 시나리오·LoadGen·division의 원조 |
| MLPerf Client | PC LLM 벤치마크 | TTFT, tokens/s |
| scenario | 요청 패턴 정의 | SingleStream·MultiStream·Server·Offline |
| LoadGen | MLPerf 공용 부하 생성기 | query 발생·타이밍·로그를 SUT 밖에서 통제 |
| closed / open division | 비교 규칙의 엄격함 | closed = 같은 모델로 스택 비교, open = 기법 자유 |
| accuracy target | 결과 인정 조건 | 예: FP32 참조의 99 % 이상 |
| EEMBC | 임베디드 벤치마크 단체 | MLPerf Tiny 러너·에너지 측정 하니스 제공으로 알려짐 |
| peak vs sustained | 순간 최고 vs 지속 가능 성능 | 열·전력 제한 아래의 지속 숫자가 제품 숫자 |
| effective TOPS | 실제 달성 연산률 | 실측 op ÷ 실측 시간 |
| TTFT | time to first token | LLM prefill 위주의 지연 |
| pp | percentage point | 91.8 % → 91.4 %는 −0.4 pp |
| Pareto front | 지배되지 않는 후보 집합 | 어떤 지표에서도 나머지보다 다 나쁘지 않은 후보들 |
| sensitivity analysis | 민감도 분석 | 가중치·가정을 바꿔 결론이 유지되는지 확인 |
| bootstrap | 재표본 신뢰구간 | 복원추출 반복으로 통계량의 흔들림 추정 |
| A/A test | 같은 것끼리 비교 | 잡음 바닥과 거짓 경보율 측정 |
| environment fingerprint | 환경 지문 | 버전·전원·온도·load·모델 해시 |
| partition | EP가 맡은 그래프 조각 | 조각이 많을수록 전환·복사 오버헤드 |

---

## 13. 요약 & 체크리스트

벤치마크 숫자는 "무엇을, 어떤 조건에서, 어떻게 요약했나"가 붙어야 의미가 있다. MLPerf는 이 세 가지를 규칙으로 고정한 업계 표준이다: Tiny는 MCU 급 KWS·VWW·IC·AD의 latency·energy·정확도, Inference는 SingleStream(p90)·MultiStream·Server·Offline 시나리오와 LoadGen·closed/open division, Mobile은 벤더 backend로 도는 앱, Client는 PC의 LLM TTFT·tokens/s를 잰다. 그러나 결정은 우리 워크로드로 한다. 벤더의 TOPS는 정밀도·sparsity·peak 클럭·활용률·오버헤드를 하나씩 벗기면 우리 모델에서 몇 %만 남을 수 있다(예제에서 3 %). 웨어러블 스위트는 always-on·burst·동시 실행·sustained 시나리오에 p50/p90/p99, init·첫 추론, 메모리, 크기, 정확도 게이트, 에너지를 붙이고, 원시 데이터와 환경 지문을 남긴다. 실측에서 본 것처럼 측정 정의(프로세스 RSS)와 잡음(배경 부하)이 점수를 뒤집을 수 있으니, 한 숫자보다 Pareto와 가중치 범위로 보고하고, 회귀 판정은 A/A로 잰 잡음 바닥 위에서 한다.

- [ ] MLPerf Tiny의 네 벤치마크(데이터셋·참조 모델·지표)를 말할 수 있다
- [ ] MLPerf Inference 네 시나리오와 각 지표, SingleStream이 p90을 쓰는 이유를 설명할 수 있다
- [ ] closed vs open division, 정확도 목표, LoadGen의 역할을 설명할 수 있다
- [ ] 벤더 TOPS를 정밀도·sparsity·sustained·활용률·오버헤드로 벗겨 우리 모델의 ms로 손계산할 수 있다
- [ ] 웨어러블 스위트의 워크로드·지표·시나리오(동시 실행, sustained 포함)를 설계할 수 있다
- [ ] init·첫 추론·steady-state를 분리하는 하니스를 짜고, p99에 필요한 반복 수를 정할 수 있다
- [ ] 가중 기하평균 점수, Pareto front, 가중치 민감도를 계산하고 그 한계를 말할 수 있다
- [ ] 환경 지문과 원시 데이터를 남기고, A/A 테스트로 회귀 임계값을 정할 수 있다
- [ ] 처리량 하락이 열인지 경합인지 원인 시계열로 구분할 수 있다
- [ ] MCU에서 고정 메모리 히스토그램으로 분위수를 근사하는 코드를 설명할 수 있다

---

## 참고 자료

- MLCommons, MLPerf 벤치마크 소개와 결과: [mlcommons.org](https://mlcommons.org/)
- C. Banbury et al., "MLPerf Tiny Benchmark", NeurIPS 2021 Datasets and Benchmarks Track (arXiv:2106.07597)
- V. J. Reddi et al., "MLPerf Inference Benchmark", ISCA 2020 (arXiv:1911.02549) — 시나리오·LoadGen 설계 설명
- V. J. Reddi et al., "MLPerf Mobile Inference Benchmark", MLSys 2022 (arXiv:2012.02328)
- MLCommons GitHub: [github.com/mlcommons](https://github.com/mlcommons) — inference(LoadGen 포함), tiny, mobile_app 저장소
- EEMBC: [eembc.org](https://www.eembc.org/)
- ONNX Runtime 양자화와 Execution Provider 문서: [onnxruntime.ai](https://onnxruntime.ai/)
- coremltools 문서: [apple.github.io/coremltools](https://apple.github.io/coremltools/)
- llama.cpp `llama-bench`: [github.com/ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp)
- B. Efron, R. Tibshirani, "An Introduction to the Bootstrap" (Chapman & Hall, 1993)
- G. Tene, HdrHistogram: [hdrhistogram.org](http://hdrhistogram.org/)
- 이 스터디 노트 세트: D3, D6, D7, C7, E6, F3, F5, F6, F8, I1, I3, K1~K4, M1~M4, O3
