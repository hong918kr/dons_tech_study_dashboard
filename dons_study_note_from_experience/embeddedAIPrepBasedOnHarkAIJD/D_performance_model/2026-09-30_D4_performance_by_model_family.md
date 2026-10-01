# D4. 모델 계열별 성능 특성 — CNN, depthwise, RNN, Transformer가 하드웨어에서 다르게 움직이는 이유

> **이 노트를 다 읽으면**: 모델 계열마다 MAC·arithmetic intensity·weight/activation/state 바이트·병렬성·순차 의존성을 한 표로 비교할 수 있다 · depthwise가 "MAC은 적은데 비례해서 빨라지지 않는" 이유를 재사용·systolic array 활용률로 설명할 수 있다 · RNN과 LLM decode가 왜 GEMV라서 memory-bound인지 실측 숫자로 보일 수 있다 · 스트리밍 과제에서 1D CNN·RNN·작은 transformer의 프레임당 비용과 상태 메모리를 비교하고, 실리콘 벤더에게 무엇을 물어야 하는지 말할 수 있다
> **JD 연결**: (우대) "performance characteristics of edge AI models, including CNN, RNN, transformers, KV-cache behavior, and their memory bandwidth requirements" — 이 문장 그대로다 · study_prep_list **D4** 행: CNN compute-bound·재사용 높음 / depthwise memory-bound·NPU 효율 낮음 / RNN 순차성·작은 GEMV / Transformer seq 길이²·attention memory
> **Don 기준 난이도**: 캐시·대역폭·DMA·"같은 명령어 수인데 왜 느리지?"를 측정으로 파고드는 감각은 이미 있다 / 새로 배울 것은 "모델 구조 → 연산 모양(GEMM, GEMV, depthwise, softmax) → 하드웨어 병목"으로 이어지는 번역표
> **선행 노트**: B2(CNN·depthwise), B3(RNN·GRU, GEMV vs GEMM), B4(attention 비용·KV-cache), B9(효율 아키텍처), C7(latency LUT). 병렬 작성 중인 D1(FLOP·MAC 공식), D3(roofline), D5(LLM prefill/decode)의 개념을 ID로 참조한다 — 여기서 다시 유도하지 않는다

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어에서 Don이 자주 봤을 상황이 있다. 두 코드 경로의 명령어 수는 비슷한데, 하나는 캐시에 딱 맞는 순차 접근이고 다른 하나는 매번 DRAM을 두드리는 pointer chasing이라 실행 시간이 열 배 차이 난다. **명령어 수는 실행 시간이 아니다.** 신경망도 똑같다. **MAC 수는 latency가 아니다.**

모델 계열(CNN, depthwise CNN, RNN, Transformer)은 각자 전형적인 "연산 모양"을 갖고 있고, 그 모양이 하드웨어의 어느 자원을 먼저 바닥내는지를 결정한다.

```
 모델 계열                 대표 연산 모양              먼저 막히는 자원
 ───────────────────────  ─────────────────────────  ──────────────────────────────
 표준 conv (ResNet)       큰 GEMM (im2col)           MAC 유닛  → compute-bound
 depthwise (MobileNet)    채널별 작은 3×3 필터       메모리·배열 활용률 → memory-bound
 1×1 conv, FC(배치)       GEMM                       MAC 유닛 (채널이 충분하면)
 FC 배치1, RNN step       GEMV (행렬 × 벡터)         weight 읽기 대역폭
 Transformer prefill      큰 GEMM + T² attention     MAC 유닛, T가 크면 attention
 Transformer decode       GEMV + KV 읽기             weight + KV-cache 대역폭
```

이 노트는 표 한 장(1절)으로 비교의 틀을 만든 다음, 각 계열을 이 Mac(Apple M2)에서 **실제로 재서** 확인한다. 측정 결과는 모두 실측이지만 이 Mac에는 다른 작업이 함께 돌고 있어서 잡음이 크다. 그래서 반복 측정해 **중앙값(median)** 과 **p10**(빠른 쪽 10% 지점, 잡음이 덜 낀 값)을 같이 적었고, 같은 코드를 두 번 돌려 차이를 보여 준 곳도 있다. 절대값보다 **계열 간 비율과 경향**을 보자.

예를 들어 Hark 같은 웨어러블이라면(추정), always-on MCU에서 음성/IMU 스트리밍 모델을 돌리고, Qualcomm SoC의 NPU/DSP에서 비전이나 작은 언어 모델을 돌릴 수 있다. 이때 "이 모델을 저 코어에 올리면 몇 ms 걸릴까?"를 MAC만 보고 답하면 틀린다. 계열별 특성을 알아야 맞출 수 있다.

---

## 1. 비교 프레임워크 — 11개 축으로 계열을 본다

### 1.1 축 정의

새 모델을 받으면 아래 11개를 먼저 적는다. 펌웨어의 "성능 요구사항 체크리스트"와 같은 역할이다.

| 축 | 뜻 | 왜 중요한가 |
|---|---|---|
| compute (MAC) | 곱셈-누산 횟수 (D1 공식) | compute-bound일 때만 latency를 정한다 |
| arithmetic intensity | 옮긴 바이트 1개당 연산(FLOP/B) (D3) | ridge point보다 작으면 memory-bound |
| weight 바이트 | 파라미터 수 × 원소 바이트 | flash/DRAM 용량, 매 추론마다 읽어야 하는 양 |
| activation 바이트 | 층 입출력 텐서 크기 | SRAM peak (D2), 층 사이 트래픽 |
| state/cache 바이트 | 호출 사이에 남겨 두는 것 (RNN 상태, KV-cache, 링버퍼) | retention RAM, 시간에 따라 자라는가 |
| 병렬성 | 동시에 계산 가능한 독립 작업의 수 (공간·채널·토큰) | MAC 배열을 채울 수 있는가 |
| 순차 의존성 | 앞 결과가 나와야 다음을 시작할 수 있는가 | 병렬 HW가 놀게 된다 |
| 동적 shape | 입력 길이·context가 호출마다 바뀌는가 | NPU 컴파일러는 고정 shape를 좋아한다 |
| op 종류 | conv/GEMM 외에 softmax, LayerNorm, gather, gate 등 | NPU 미지원 op는 CPU/DSP fallback |
| 양자화 민감도 | int8로 바꿨을 때 정확도가 무너지는 정도 (C1–C3) | NPU는 int8이 기본이다 |
| 스트리밍 성질 | 프레임 하나가 들어올 때 얼마나 일하고 무엇을 기억하나 | always-on, 실시간 deadline (D6) |

### 1.2 한 장으로 보기

```svg
<svg viewBox="0 0 684 374" xmlns="http://www.w3.org/2000/svg">
<text x="145.0" y="32.0" font-size="12" text-anchor="middle">표준 conv</text><text x="227.0" y="32.0" font-size="12" text-anchor="middle">depthwise</text><text x="309.0" y="32.0" font-size="12" text-anchor="middle">1×1·FC 배치</text><text x="391.0" y="32.0" font-size="12" text-anchor="middle">FC 배치1</text><text x="473.0" y="32.0" font-size="12" text-anchor="middle">RNN/GRU</text><text x="555.0" y="32.0" font-size="12" text-anchor="middle">TF prefill</text><text x="637.0" y="32.0" font-size="12" text-anchor="middle">TF decode</text><text x="98.0" y="61.0" font-size="12" text-anchor="end">MAC 양</text>
<rect x="106" y="46" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="145.0" y="61.0" font-size="12" text-anchor="middle">많음</text><rect x="188" y="46" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="227.0" y="61.0" font-size="12" text-anchor="middle">아주 적음</text><rect x="270" y="46" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="309.0" y="61.0" font-size="12" text-anchor="middle">중간</text><rect x="352" y="46" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/>
<text x="391.0" y="61.0" font-size="12" text-anchor="middle">적음</text><rect x="434" y="46" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="473.0" y="61.0" font-size="12" text-anchor="middle">아주 적음</text><rect x="516" y="46" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="555.0" y="61.0" font-size="12" text-anchor="middle">많음</text><rect x="598" y="46" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="637.0" y="61.0" font-size="12" text-anchor="middle">≈파라미터</text>
<text x="98.0" y="87.0" font-size="12" text-anchor="end">intensity</text><rect x="106" y="72" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="145.0" y="87.0" font-size="12" text-anchor="middle">수백</text><rect x="188" y="72" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="227.0" y="87.0" font-size="12" text-anchor="middle">~9</text><rect x="270" y="72" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="309.0" y="87.0" font-size="12" text-anchor="middle">수십~백</text>
<rect x="352" y="72" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="391.0" y="87.0" font-size="12" text-anchor="middle">~2</text><rect x="434" y="72" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="473.0" y="87.0" font-size="12" text-anchor="middle">~2</text><rect x="516" y="72" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="555.0" y="87.0" font-size="12" text-anchor="middle">수백</text><rect x="598" y="72" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/>
<text x="637.0" y="87.0" font-size="12" text-anchor="middle">~2</text><text x="98.0" y="113.0" font-size="12" text-anchor="end">weight 바이트</text><rect x="106" y="98" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="145.0" y="113.0" font-size="12" text-anchor="middle">작음</text><rect x="188" y="98" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="227.0" y="113.0" font-size="12" text-anchor="middle">아주 작음</text><rect x="270" y="98" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/>
<text x="309.0" y="113.0" font-size="12" text-anchor="middle">중간</text><rect x="352" y="98" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="391.0" y="113.0" font-size="12" text-anchor="middle">큼</text><rect x="434" y="98" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="473.0" y="113.0" font-size="12" text-anchor="middle">작음</text><rect x="516" y="98" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="555.0" y="113.0" font-size="12" text-anchor="middle">큼</text>
<rect x="598" y="98" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="637.0" y="113.0" font-size="12" text-anchor="middle">큼</text><text x="98.0" y="139.0" font-size="12" text-anchor="end">activation</text><rect x="106" y="124" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="145.0" y="139.0" font-size="12" text-anchor="middle">큼</text><rect x="188" y="124" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="227.0" y="139.0" font-size="12" text-anchor="middle">큼</text>
<rect x="270" y="124" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="309.0" y="139.0" font-size="12" text-anchor="middle">큼</text><rect x="352" y="124" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="391.0" y="139.0" font-size="12" text-anchor="middle">작음</text><rect x="434" y="124" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="473.0" y="139.0" font-size="12" text-anchor="middle">아주 작음</text>
<rect x="516" y="124" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="555.0" y="139.0" font-size="12" text-anchor="middle">큼 (T×d)</text><rect x="598" y="124" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="637.0" y="139.0" font-size="12" text-anchor="middle">작음</text><text x="98.0" y="165.0" font-size="12" text-anchor="end">state/cache</text><rect x="106" y="150" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="145.0" y="165.0" font-size="12" text-anchor="middle">없음</text>
<rect x="188" y="150" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="227.0" y="165.0" font-size="12" text-anchor="middle">없음</text><rect x="270" y="150" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="309.0" y="165.0" font-size="12" text-anchor="middle">없음</text><rect x="352" y="150" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="391.0" y="165.0" font-size="12" text-anchor="middle">없음</text>
<rect x="434" y="150" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="473.0" y="165.0" font-size="12" text-anchor="middle">O(H) 작음</text><rect x="516" y="150" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="555.0" y="165.0" font-size="12" text-anchor="middle">KV 생성</text><rect x="598" y="150" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="637.0" y="165.0" font-size="12" text-anchor="middle">O(T) 증가</text><text x="98.0" y="191.0" font-size="12" text-anchor="end">병렬성</text>
<rect x="106" y="176" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="145.0" y="191.0" font-size="12" text-anchor="middle">공간·채널</text><rect x="188" y="176" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="227.0" y="191.0" font-size="12" text-anchor="middle">공간만</text><rect x="270" y="176" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="309.0" y="191.0" font-size="12" text-anchor="middle">행×열</text>
<rect x="352" y="176" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="391.0" y="191.0" font-size="12" text-anchor="middle">출력 행만</text><rect x="434" y="176" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="473.0" y="191.0" font-size="12" text-anchor="middle">gate 행만</text><rect x="516" y="176" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="555.0" y="191.0" font-size="12" text-anchor="middle">토큰·head</text>
<rect x="598" y="176" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="637.0" y="191.0" font-size="12" text-anchor="middle">head만</text><text x="98.0" y="217.0" font-size="12" text-anchor="end">순차 의존</text><rect x="106" y="202" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="145.0" y="217.0" font-size="12" text-anchor="middle">없음</text><rect x="188" y="202" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="227.0" y="217.0" font-size="12" text-anchor="middle">없음</text>
<rect x="270" y="202" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="309.0" y="217.0" font-size="12" text-anchor="middle">없음</text><rect x="352" y="202" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="391.0" y="217.0" font-size="12" text-anchor="middle">없음</text><rect x="434" y="202" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="473.0" y="217.0" font-size="12" text-anchor="middle">매 step</text>
<rect x="516" y="202" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="555.0" y="217.0" font-size="12" text-anchor="middle">층 사이만</text><rect x="598" y="202" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="637.0" y="217.0" font-size="12" text-anchor="middle">매 토큰</text><text x="98.0" y="243.0" font-size="12" text-anchor="end">동적 shape</text><rect x="106" y="228" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="145.0" y="243.0" font-size="12" text-anchor="middle">고정</text>
<rect x="188" y="228" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="227.0" y="243.0" font-size="12" text-anchor="middle">고정</text><rect x="270" y="228" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="309.0" y="243.0" font-size="12" text-anchor="middle">고정</text><rect x="352" y="228" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="391.0" y="243.0" font-size="12" text-anchor="middle">고정</text>
<rect x="434" y="228" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="473.0" y="243.0" font-size="12" text-anchor="middle">고정</text><rect x="516" y="228" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="555.0" y="243.0" font-size="12" text-anchor="middle">T 가변</text><rect x="598" y="228" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="637.0" y="243.0" font-size="12" text-anchor="middle">ctx 증가</text><text x="98.0" y="269.0" font-size="12" text-anchor="end">NPU 친화</text>
<rect x="106" y="254" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="145.0" y="269.0" font-size="12" text-anchor="middle">최상</text><rect x="188" y="254" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="227.0" y="269.0" font-size="12" text-anchor="middle">나쁨~보통</text><rect x="270" y="254" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="309.0" y="269.0" font-size="12" text-anchor="middle">최상</text>
<rect x="352" y="254" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="391.0" y="269.0" font-size="12" text-anchor="middle">배열 낭비</text><rect x="434" y="254" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="473.0" y="269.0" font-size="12" text-anchor="middle">순환·gate</text><rect x="516" y="254" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="555.0" y="269.0" font-size="12" text-anchor="middle">softmax·LN</text>
<rect x="598" y="254" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="637.0" y="269.0" font-size="12" text-anchor="middle">softmax·LN</text><text x="98.0" y="295.0" font-size="12" text-anchor="end">양자화 민감</text><rect x="106" y="280" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="145.0" y="295.0" font-size="12" text-anchor="middle">낮음</text><rect x="188" y="280" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="227.0" y="295.0" font-size="12" text-anchor="middle">중간</text>
<rect x="270" y="280" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="309.0" y="295.0" font-size="12" text-anchor="middle">낮음</text><rect x="352" y="280" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="391.0" y="295.0" font-size="12" text-anchor="middle">낮음</text><rect x="434" y="280" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="473.0" y="295.0" font-size="12" text-anchor="middle">상태 누적</text>
<rect x="516" y="280" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="555.0" y="295.0" font-size="12" text-anchor="middle">이상값</text><rect x="598" y="280" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="637.0" y="295.0" font-size="12" text-anchor="middle">이상값+KV</text><text x="98.0" y="321.0" font-size="12" text-anchor="end">스트리밍</text><rect x="106" y="306" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="145.0" y="321.0" font-size="12" text-anchor="middle">링버퍼</text>
<rect x="188" y="306" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="227.0" y="321.0" font-size="12" text-anchor="middle">링버퍼</text><rect x="270" y="306" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="309.0" y="321.0" font-size="12" text-anchor="middle">프레임</text><rect x="352" y="306" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="391.0" y="321.0" font-size="12" text-anchor="middle">프레임</text>
<rect x="434" y="306" width="78" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b" stroke-width="1"/><text x="473.0" y="321.0" font-size="12" text-anchor="middle">최적</text><rect x="516" y="306" width="78" height="22" rx="3" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a" stroke-width="1"/><text x="555.0" y="321.0" font-size="12" text-anchor="middle">부적합</text><rect x="598" y="306" width="78" height="22" rx="3" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c" stroke-width="1"/><text x="637.0" y="321.0" font-size="12" text-anchor="middle">토큰 단위</text><rect x="104" y="345" width="14" height="14" rx="2" fill="#3f9a6b" fill-opacity="0.28" stroke="#3f9a6b"/>
<text x="124.0" y="356.0" font-size="12" text-anchor="start">유리</text><rect x="254" y="345" width="14" height="14" rx="2" fill="#e08a3c" fill-opacity="0.28" stroke="#e08a3c"/><text x="274.0" y="356.0" font-size="12" text-anchor="start">조건부·중간</text><rect x="404" y="345" width="14" height="14" rx="2" fill="#d0564a" fill-opacity="0.28" stroke="#d0564a"/><text x="424.0" y="356.0" font-size="12" text-anchor="start">불리 (병목 후보)</text>
</svg>
```

그림 1 — 계열별 11개 축 비교(정성적). 초록은 하드웨어에 유리한 성질, 주황은 조건부(크기·구현에 따라), 빨강은 병목 후보다. 표준 conv와 1×1(GEMM)은 대부분 초록인 반면, depthwise·GEMV·RNN·decode는 "intensity"와 "병렬성" 행에서 빨강이 몰려 있다. 이 두 행이 memory-bound의 원인이다.

### 1.3 손으로 계산 — 대표 레이어의 intensity

int8(원소 1 B), batch 1, 최소 트래픽(weight·입력·출력을 각각 한 번씩만 옮긴다)으로 가정한다. 표준 3×3 conv, C_in = C_out = 64, 56×56, padding 1:

```
 MAC        = 56·56 · 64·64 · 9            = 115.6 M
 weight     = 64·64·9 B                    = 36.9 KB
 activation = 입력 56·56·64 + 출력 56·56·64 = 401.4 KB
 intensity  = 2·MAC / 바이트 = 231.2 M / 438.3 K ≈ 528 FLOP/B
```

말로 하면: 3×3 conv는 한 바이트를 옮길 때마다 500번 넘게 연산한다. 이유는 **재사용**이다. weight 하나는 출력 픽셀 3136개에서 다시 쓰이고, 입력 값 하나는 출력 채널 64개 × 커널 위치 9개 = 576번 쓰인다.

같은 입력에 depthwise 3×3을 걸면 MAC은 56·56·64·9 = 1.81 M으로 64배 줄지만 activation 바이트는 그대로 401 KB다. 입력 값 하나가 9번만 쓰인다. intensity는 2·1.81 M / 402 K ≈ 9 FLOP/B로 떨어진다.

### 1.4 코드로 확인 — 계열별 대표 레이어 표

각 계열의 대표 레이어 하나씩, MAC·바이트·intensity를 순수 산술로 계산한다.

**[ex1]**

```python
# 계열별 대표 레이어 1개: MAC, 바이트(int8=1B, 최소 트래픽: weight·입력·출력·상태 각 1회), intensity
def row(name, macs, w, act_in, act_out, state=0):
    byts = w + act_in + act_out + state
    print(f"{name:26s} {macs/1e6:8.2f} {w/1e3:8.1f} {(act_in+act_out)/1e3:8.1f} {state/1e3:7.1f} {2*macs/byts:7.1f}")
print(f"{'layer (batch 1, int8)':26s} {'MMAC':>8s} {'W KB':>8s} {'act KB':>8s} {'st KB':>7s} {'FLOP/B':>7s}")
H = W = 56; C = 64
row("conv3x3 64->64 @56x56", H*W*C*C*9, C*C*9, H*W*C, H*W*C)
row("depthwise3x3 C=64 @56x56", H*W*C*9, C*9, H*W*C, H*W*C)
row("pointwise1x1 64->64 @56x56", H*W*C*C, C*C, H*W*C, H*W*C)
row("FC 1024->1024 (GEMV)", 1024*1024, 1024*1024, 1024, 1024)
row("FC 1024->1024 batch 64", 64*1024*1024, 1024*1024, 64*1024, 64*1024)
Hh, I = 128, 40                                   # GRU 1 step: 3 gates
row("GRU H=128 I=40, 1 step", 3*Hh*(I+Hh), 3*Hh*(I+Hh), I, Hh, state=Hh)
d, dkv, ff, T = 576, 192, 1536, 1024              # SmolLM2-135M 한 층 모양
wl = d*d*2 + d*dkv*2 + 3*d*ff                     # q,o + k,v + gate/up/down
kv = 2*T*dkv                                      # 이 층의 KV cache (int8 가정)
row("LLM layer decode, ctx 1024", wl + 2*T*d, wl, d, d, state=kv)
row("LLM layer prefill 512 tok", 512*wl + 2*512*512*d, wl, 512*d, 512*d, state=2*512*dkv)
```

```text
layer (batch 1, int8)          MMAC     W KB   act KB   st KB  FLOP/B
conv3x3 64->64 @56x56        115.61     36.9    401.4     0.0   527.6
depthwise3x3 C=64 @56x56       1.81      0.6    401.4     0.0     9.0
pointwise1x1 64->64 @56x56    12.85      4.1    401.4     0.0    63.4
FC 1024->1024 (GEMV)           1.05   1048.6      2.0     0.0     2.0
FC 1024->1024 batch 64        67.11   1048.6    131.1     0.0   113.8
GRU H=128 I=40, 1 step         0.06     64.5      0.2     0.1     2.0
LLM layer decode, ctx 1024     4.72   3538.9      1.2   393.2     2.4
LLM layer prefill 512 tok   2113.93   3538.9    589.8   196.6   977.5
```

출력에서 볼 것: intensity가 세 무리로 갈린다. 표준 conv와 LLM prefill은 수백, pointwise와 배치 64 FC는 수십~백, depthwise(9)·GEMV·GRU step·LLM decode는 2 안팎이다. 특히 LLM decode 한 층은 weight 3.5 MB에 KV 0.4 MB를 읽어서 연산은 4.7 M MAC밖에 못 한다. 이 표 하나가 이 노트의 요약이다.

### 1.5 roofline에 올려 보기 (D3 식을 그대로 사용)

D3의 roofline 식 `attainable = min(peak, intensity × bandwidth)`에 가상의 edge NPU를 넣어 본다. **숫자는 설명용 가정이다**: int8 peak 2 TOPS(= 1 TMAC/s), DRAM 17 GB/s(LPDDR4X 2채널급), 모든 텐서가 DRAM을 한 번씩 오간다고 본다.

**[ex12]**

```python
# 가상의 edge NPU(가정: int8 1 TMAC/s = 2 TOPS, DRAM 17 GB/s)에 ex1의 intensity를 올려 본다 (D3 roofline 식)
peak_flops, bw = 2e12, 17e9
ridge = peak_flops / bw
print(f"ridge point = {ridge:.0f} FLOP/B")
layers = {"conv3x3 64->64": 527.6, "depthwise3x3 C=64": 9.0, "pointwise1x1 64->64": 63.4,
          "FC GEMV batch 1": 2.0, "FC batch 64": 113.8, "GRU step": 2.0,
          "LLM decode ctx1024": 2.4, "LLM prefill 512": 977.5}
for name, ai in layers.items():
    att = min(peak_flops, ai * bw)                      # attainable FLOP/s
    kind = "compute-bound" if ai >= ridge else "memory-bound"
    print(f"{name:21s} AI {ai:6.1f}  attainable {att/1e9:7.1f} GFLOP/s = {att/peak_flops*100:5.1f}% of peak  {kind}")
```

```text
ridge point = 118 FLOP/B
conv3x3 64->64        AI  527.6  attainable  2000.0 GFLOP/s = 100.0% of peak  compute-bound
depthwise3x3 C=64     AI    9.0  attainable   153.0 GFLOP/s =   7.6% of peak  memory-bound
pointwise1x1 64->64   AI   63.4  attainable  1077.8 GFLOP/s =  53.9% of peak  memory-bound
FC GEMV batch 1       AI    2.0  attainable    34.0 GFLOP/s =   1.7% of peak  memory-bound
FC batch 64           AI  113.8  attainable  1934.6 GFLOP/s =  96.7% of peak  memory-bound
GRU step              AI    2.0  attainable    34.0 GFLOP/s =   1.7% of peak  memory-bound
LLM decode ctx1024    AI    2.4  attainable    40.8 GFLOP/s =   2.0% of peak  memory-bound
LLM prefill 512       AI  977.5  attainable  2000.0 GFLOP/s = 100.0% of peak  compute-bound
```

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="300" x2="640" y2="300" stroke="currentColor"/><line x1="70" y1="30" x2="70" y2="300" stroke="currentColor"/><line x1="70.0" y1="300" x2="70.0" y2="305" stroke="currentColor"/><text x="70.0" y="318.0" font-size="12" text-anchor="middle">1</text><line x1="242.7" y1="300" x2="242.7" y2="305" stroke="currentColor"/><text x="242.7" y="318.0" font-size="12" text-anchor="middle">10</text><line x1="415.3" y1="300" x2="415.3" y2="305" stroke="currentColor"/><text x="415.3" y="318.0" font-size="12" text-anchor="middle">100</text><line x1="588.0" y1="300" x2="588.0" y2="305" stroke="currentColor"/><text x="588.0" y="318.0" font-size="12" text-anchor="middle">1000</text>
<line x1="65" y1="300.0" x2="70" y2="300.0" stroke="currentColor"/><text x="62.0" y="304.0" font-size="12" text-anchor="end">10</text><line x1="65" y1="196.2" x2="70" y2="196.2" stroke="currentColor"/><text x="62.0" y="200.2" font-size="12" text-anchor="end">100</text><line x1="65" y1="92.5" x2="70" y2="92.5" stroke="currentColor"/><text x="62.0" y="96.5" font-size="12" text-anchor="end">1000</text><polyline points="70.0,276.1 427.5,61.2 640.0,61.2" fill="none" stroke="currentColor" stroke-width="2"/><line x1="427.5" y1="61.2" x2="427.5" y2="300" stroke="#888" stroke-dasharray="4 3"/><text x="431.5" y="292.0" font-size="12" text-anchor="start">ridge ≈ 118 FLOP/B</text>
<text x="497.7" y="31.2" font-size="12" text-anchor="middle">peak 2 TOPS (가정)</text><text x="294.7" y="119.1" font-size="12" text-anchor="end">DRAM 17 GB/s (가정)</text><circle cx="540.1" cy="61.2" r="5" fill="#4a7bd0"/><text x="512.1" y="83.2" font-size="12" text-anchor="start">conv3×3</text><circle cx="234.8" cy="177.1" r="5" fill="#d0564a"/><text x="242.8" y="193.1" font-size="12" text-anchor="start">depthwise</text><circle cx="381.2" cy="89.1" r="5" fill="#4a7bd0"/><text x="389.2" y="105.1" font-size="12" text-anchor="start">pointwise</text><circle cx="122.0" cy="244.9" r="5" fill="#e08a3c"/><text x="130.0" y="260.9" font-size="12" text-anchor="start">GEMV·GRU</text>
<circle cx="425.0" cy="62.7" r="5" fill="#4a7bd0"/><text x="415.0" y="50.7" font-size="12" text-anchor="end">FC 배치64</text><circle cx="135.7" cy="236.6" r="5" fill="#3f9a6b"/><text x="143.7" y="228.6" font-size="12" text-anchor="start">decode</text><circle cx="586.3" cy="61.2" r="5" fill="#3f9a6b"/><text x="576.3" y="83.2" font-size="12" text-anchor="start">prefill</text><text x="355.0" y="342.0" font-size="12" text-anchor="middle">arithmetic intensity (FLOP/byte, log)</text><text x="16.0" y="165.0" font-size="12" text-anchor="middle" transform="rotate(-90 16 165.0)">GFLOP/s</text>
</svg>
```

그림 2 — 가상의 NPU(2 TOPS, 17 GB/s) roofline 위의 계열별 대표 레이어. 대각선 구간(memory-bound)에 GEMV·GRU·decode·depthwise가, 평평한 구간(compute-bound)에 conv3×3·prefill이 앉는다. 배치 64 FC와 pointwise는 ridge 근처다.

출력에서 볼 것: 같은 칩에서 표준 conv는 peak의 100%, depthwise는 7.6%, GEMV·GRU·decode는 2% 안팎이다. "2 TOPS NPU"라는 스펙은 GEMV형 모델에는 거의 의미가 없고, 그 모델의 속도는 17 GB/s가 정한다. 실제 NPU는 activation을 on-chip SRAM에 두기 때문에 depthwise·pointwise는 이보다 나아질 수 있다. 반대로 decode의 weight는 SRAM에 안 들어가므로 이 계산이 거의 그대로 맞는다.

### 1.6 함정

- intensity는 **데이터가 실제로 어디를 오가느냐**에 달려 있다. 같은 depthwise라도 activation이 SRAM에 머물면 DRAM 기준 intensity는 훨씬 높다. 비교할 때는 "어느 메모리 계층 기준인지"를 꼭 밝힌다.
- 이 표는 **batch 1** 기준이다. 서버 GPU처럼 batch를 키우면 GEMV가 GEMM이 되어 전혀 다른 그림이 된다. edge의 스트리밍 추론은 거의 항상 batch 1이다.

---

## 2. 표준 conv — 재사용이 높은 compute-bound의 모범생

### 2.1 직관

표준 conv는 im2col로 펴면 큰 GEMM이 된다(B2 10.2). `[출력 픽셀 수 × (C_in·k·k)] @ [(C_in·k·k) × C_out]`. 세 차원 M(픽셀), K(감소축), N(출력 채널)이 모두 크면 MAC 배열을 가득 채우고, 타일링으로 weight와 입력을 캐시/SRAM에 올려 여러 번 재사용할 수 있다. 펌웨어로 치면 "한 번 DMA로 가져온 버퍼를 루프에서 수백 번 쓰는" 이상적인 경우다.

### 2.2 손계산 — 채널이 적으면 재사용이 줄어든다

입력 값 하나의 재사용 횟수는 `C_out × k × k`, weight 하나의 재사용 횟수는 출력 픽셀 수다. C_in = C_out = 8, 3×3이면 입력 재사용이 72번, C = 256이면 2304번이다. GEMM으로 보면 K = 9·C, N = C라서, C가 작을수록 "얇은" GEMM이 되고 SIMD 레지스터·MAC 배열 폭을 다 못 쓴다. 그래서 같은 conv라도 **채널이 적은 초기 층은 GMAC/s가 낮다.**

### 2.3 코드로 확인 — 채널 수별 GMAC/s

56×56 입력에서 3×3 conv의 채널 수만 바꿔 가며 측정한다(fp32, batch 1, torch CPU 기본 4 threads). 같은 코드를 두 번 실행한 출력이다.

**[ex2]**

```python
# 표준 3x3 conv: 채널 수를 늘리면 GMAC/s가 어떻게 변하나 (fp32, batch 1, 56x56, torch CPU)
import torch, time, statistics as st
torch.manual_seed(0); torch.set_grad_enabled(False)
def bench(f, reps=40):
    for _ in range(5): f()
    ts = []
    for _ in range(reps):
        t0 = time.perf_counter(); f(); ts.append(time.perf_counter() - t0)
    ts.sort(); return st.median(ts), ts[len(ts)//10]      # median, p10
print(f"threads={torch.get_num_threads()}")
print(f"{'C_in=C_out':>10s} {'MMAC':>8s} {'median ms':>9s} {'GMAC/s med':>10s} {'GMAC/s p10':>10s}")
for C in [8, 16, 32, 64, 128, 256]:
    conv = torch.nn.Conv2d(C, C, 3, padding=1, bias=False).eval()
    x = torch.randn(1, C, 56, 56)
    macs = 56 * 56 * C * C * 9
    med, p10 = bench(lambda: conv(x))
    print(f"{C:10d} {macs/1e6:8.1f} {med*1e3:9.3f} {macs/med/1e9:10.1f} {macs/p10/1e9:10.1f}")
```

```text
threads=4
C_in=C_out     MMAC median ms GMAC/s med GMAC/s p10
         8      1.8     0.062       29.3       32.7
        16      7.2     0.090       80.0       86.7
        32     28.9     0.161      179.5      195.5
        64    115.6     0.352      328.4      346.1
       128    462.4     1.659      278.7      293.3
       256   1849.7     4.122      448.7      458.7
threads=4
C_in=C_out     MMAC median ms GMAC/s med GMAC/s p10
         8      1.8     0.043       42.0       42.8
        16      7.2     0.060      120.0      122.6
        32     28.9     0.162      178.1      250.2
        64    115.6     0.303      381.1      432.5
       128    462.4     1.866      247.8      356.1
       256   1849.7     4.043      457.5      477.6
```

출력에서 볼 것: C = 8에서는 30~42 GMAC/s, C = 64 이상에서는 250~460 GMAC/s다. 채널이 많아질수록 GEMM이 두꺼워져 10배 이상 효율이 오른다. C = 128이 C = 64보다 낮게 나온 것은 두 번 모두 재현됐으니 잡음만은 아니고, 이 크기에서 커널 선택·캐시 타일링이 불리했을 가능성이 크다(원인은 확인하지 않았다). 두 실행의 차이(예: C = 8에서 29 vs 42)가 이 Mac의 측정 잡음 수준이다.

### 2.4 임베디드 연결

- 표준 conv는 NPU 벤더가 "TOPS"를 광고할 때 쓰는 바로 그 워크로드다. NPU 데이터시트의 효율 숫자는 보통 이런 레이어 기준이라고 생각하면 된다.
- MCU(Cortex-M + CMSIS-NN)에서도 가장 잘 최적화된 커널은 표준 conv와 1×1 conv다. SIMD 명령(`SMLAD`, Helium `VMLADAV`)이 채널 방향 내적을 한 번에 처리하기 좋다.
- 함정: 입력 채널이 3(RGB)이나 1(마이크 스펙트로그램)인 **첫 층**은 표준 conv여도 K가 작아서 효율이 낮다. 벤더들이 첫 층 전용 경로를 두는 이유다.

---

## 3. Depthwise conv — MAC은 적은데 왜 비례해서 빨라지지 않나

### 3.1 직관

depthwise 3×3은 채널마다 자기 3×3 필터 하나만 쓴다(B2 6절). 채널끼리 섞지 않으니 MAC이 C_out배 줄어든다. 그런데 **줄어든 것은 연산뿐이고, 읽고 써야 할 activation은 그대로**다. 1.3에서 계산했듯이 입력 값 하나를 9번만 쓰고 버린다. 펌웨어 비유로는 "DMA로 4 KB를 가져와서 체크섬 한 번 계산하고 버리는" 루프다. CPU가 아무리 빨라도 메모리가 속도를 정한다.

### 3.2 손계산 — depthwise separable의 기대치와 현실

depthwise separable = depthwise 3×3 + pointwise 1×1. MAC 비율은 표준 conv 대비 `1/C_out + 1/9`이다(B2 6.3).

```
 C = 64 :  1/64 + 1/9 = 0.127  →  MAC 기준 7.9배 절약 기대
 C = 256:  1/256 + 1/9 = 0.115 →  MAC 기준 8.7배 절약 기대
```

말로 하면: 채널이 충분히 많으면 약 8~9배 적은 MAC이다. 이제 실측 시간이 이 비율을 따라가는지 본다.

### 3.3 코드로 확인 — standard vs depthwise vs pointwise

**[ex3]**

```python
# 같은 입력(C x 56 x 56)에서 standard 3x3 / depthwise 3x3 / pointwise 1x1 의 GMAC/s
import torch, time, statistics as st
torch.manual_seed(0); torch.set_grad_enabled(False)
def bench(f, reps=60):
    for _ in range(5): f()
    ts = []
    for _ in range(reps):
        t0 = time.perf_counter(); f(); ts.append(time.perf_counter() - t0)
    ts.sort(); return st.median(ts), ts[len(ts)//10]
print(f"{'layer':22s} {'MMAC':>7s} {'med ms':>7s} {'GMAC/s med':>10s} {'p10':>6s}")
for C in [64, 256]:
    x = torch.randn(1, C, 56, 56)
    for name, m, macs in [
        (f"standard3x3 C={C}", torch.nn.Conv2d(C, C, 3, padding=1, bias=False), 56*56*C*C*9),
        (f"depthwise3x3 C={C}", torch.nn.Conv2d(C, C, 3, padding=1, groups=C, bias=False), 56*56*C*9),
        (f"pointwise1x1 C={C}", torch.nn.Conv2d(C, C, 1, bias=False), 56*56*C*C)]:
        med, p10 = bench(lambda: m(x))
        print(f"{name:22s} {macs/1e6:7.1f} {med*1e3:7.3f} {macs/med/1e9:10.1f} {macs/p10/1e9:6.1f}")
```

```text
layer                     MMAC  med ms GMAC/s med    p10
standard3x3 C=64         115.6   0.426      271.5  285.8
depthwise3x3 C=64          1.8   0.177       10.2   11.0
pointwise1x1 C=64         12.8   0.042      308.3  341.4
standard3x3 C=256       1849.7   5.366      344.7  350.0
depthwise3x3 C=256         7.2   0.609       11.9   12.4
pointwise1x1 C=256       205.5   0.439      467.7  511.8
```

출력에서 볼 것: 같은 입력에서 depthwise는 10~12 GMAC/s, standard와 pointwise는 270~470 GMAC/s다. **30배 가까이 효율이 낮다.** 여기서 손계산을 이어 가자. C = 64일 때 depthwise separable(dw + pw)은

```
 MAC : 1.8 + 12.8 = 14.6 MMAC   (standard 115.6의 12.7%, 7.9배 적음)
 시간 : 0.177 + 0.042 = 0.219 ms (standard 0.426 ms의 51%, 1.9배 빠름)
 depthwise가 MAC의 12%인데 시간의 81%를 먹는다
```

C = 256이면 MAC은 8.7배 적은데 시간은 1.048 ms vs 5.366 ms로 5.1배 빠르다. 채널이 많을수록 pointwise 비중이 커져 사정이 나아지지만, 어느 쪽이든 **MAC 절약 비율만큼 빨라지지 않는다.**

주의: 이 측정에는 "PyTorch CPU 구현의 사정"도 섞여 있다. 프로파일러로 보면 이 torch 빌드는 depthwise를 `aten::_slow_conv2d_forward`로 **채널 그룹마다 한 번씩** 호출한다(MobileNetV2 한 번 forward에 약 1000번). 즉 10 GMAC/s는 "depthwise의 본질적 한계 + 전용 커널이 없는 런타임의 한계"가 합쳐진 값이다. 이것도 실무에서 그대로 만나는 현상이다(3.6 참고).

### 3.4 C로 확인 — 전용 루프로 짜도 depthwise는 느리다

런타임 탓만인지 확인하려고 NHWC float 루프를 C로 직접 짰다. 두 함수 모두 채널 방향이 가장 안쪽이라 컴파일러가 벡터화할 수 있는 모양이다. 1코어, `-O2`.

**[dwconv.c]**

```c
/* 같은 입력 56x56x64 (NHWC, float, padding 1): standard 3x3 vs depthwise 3x3 — 1코어 GMAC/s */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define H 56
#define W 56
#define C 64
static float in[(H + 2) * (W + 2) * C], out[H * W * C];   /* 입력은 미리 zero-pad */
static float wstd[3 * 3 * C * C], wdw[3 * 3 * C];
static float frand(void) { return (float)((double)rand() / RAND_MAX); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void conv_std(void) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float *o = &out[(y * W + x) * C];
        for (int co = 0; co < C; co++) o[co] = 0.f;
        for (int ky = 0; ky < 3; ky++) for (int kx = 0; kx < 3; kx++) {
            const float *i = &in[((y + ky) * (W + 2) + x + kx) * C];
            for (int ci = 0; ci < C; ci++) {              /* 입력 1개를 C_out번 재사용 */
                const float *w = &wstd[((ky * 3 + kx) * C + ci) * C];
                for (int co = 0; co < C; co++) o[co] += i[ci] * w[co];
            }
        }
    }
}
static void conv_dw(void) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float *o = &out[(y * W + x) * C];
        for (int c = 0; c < C; c++) o[c] = 0.f;
        for (int ky = 0; ky < 3; ky++) for (int kx = 0; kx < 3; kx++) {
            const float *i = &in[((y + ky) * (W + 2) + x + kx) * C];
            const float *w = &wdw[(ky * 3 + kx) * C];
            for (int c = 0; c < C; c++) o[c] += i[c] * w[c];   /* 입력 1개를 1번만 사용 */
        }
    }
}
static double run(void (*f)(void), double macs) {
    double best = 1e9;
    for (int r = 0; r < 15; r++) { double t0 = now(); f(); double dt = now() - t0; if (dt < best) best = dt; }
    return macs / best / 1e9;
}
int main(void) {
    srand(1);
    for (size_t k = 0; k < sizeof in / sizeof in[0]; k++) in[k] = frand();
    for (size_t k = 0; k < sizeof wstd / sizeof wstd[0]; k++) wstd[k] = frand() - 0.5f;
    for (size_t k = 0; k < sizeof wdw / sizeof wdw[0]; k++) wdw[k] = frand() - 0.5f;
    double m_std = (double)H * W * C * C * 9, m_dw = (double)H * W * C * 9;
    printf("standard 3x3 : %7.1f MMAC  %6.2f GMAC/s (best of 15)\n", m_std / 1e6, run(conv_std, m_std));
    printf("depthwise 3x3: %7.1f MMAC  %6.2f GMAC/s (best of 15)\n", m_dw / 1e6, run(conv_dw, m_dw));
    printf("checksum %.3f\n", out[123]);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 -lm dwconv.c -o dwconv && ./dwconv && ./dwconv
```

```text
standard 3x3 :   115.6 MMAC   19.82 GMAC/s (best of 15)
depthwise 3x3:     1.8 MMAC   10.50 GMAC/s (best of 15)
checksum 0.229
standard 3x3 :   115.6 MMAC   20.28 GMAC/s (best of 15)
depthwise 3x3:     1.8 MMAC   10.50 GMAC/s (best of 15)
checksum 0.229
```

출력에서 볼 것: 같은 구조의 C 루프에서도 depthwise는 표준 conv의 약 절반(10.5 vs 20 GMAC/s)이다. 표준 conv의 안쪽 루프는 입력 값 `i[ci]` 하나를 레지스터에 두고 `C_out`개 weight와 곱하지만(재사용), depthwise의 안쪽 루프는 입력·weight·출력을 모두 새로 읽고 쓴다. 곱셈 1번에 load 2번 + load/store 1번이 붙는다. 스칼라 CPU에서도 **MAC당 메모리 연산 수**가 depthwise를 느리게 만든다.

### 3.5 NPU에서는 더 나쁘다 — systolic array 활용률

NPU의 MAC 엔진은 흔히 **systolic array**(R×C 격자의 MAC 셀, PE)다. weight-stationary 방식이라면 weight 타일 `K×N`을 격자에 깔아 두고, 입력 행을 한 사이클에 하나씩 흘려보내면 각 PE가 곱하고 옆으로 넘기면서 누산한다. 격자의 세로(R)는 감소축 K, 가로(C)는 출력 채널 N에 대응한다. **K와 N이 모두 R, C 이상이어야 격자가 꽉 찬다.**

depthwise는 채널마다 따로 계산하므로 채널 하나가 "K = 9, N = 1"짜리 아주 작은 GEMM이다. 32×32 격자라면 1024개 PE 중 9개만 일한다.

```svg
<svg viewBox="0 0 660 320" xmlns="http://www.w3.org/2000/svg">
<text x="206.0" y="30.0" font-size="13" text-anchor="middle">표준 conv / 1×1 / 큰 GEMM</text><rect x="111" y="61" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="135" y="61" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="159" y="61" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="183" y="61" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="207" y="61" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="231" y="61" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="255" y="61" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="279" y="61" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="111" y="85" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="135" y="85" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="159" y="85" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="183" y="85" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="207" y="85" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="231" y="85" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="255" y="85" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="279" y="85" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="111" y="109" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="135" y="109" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="159" y="109" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="183" y="109" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="207" y="109" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="231" y="109" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="255" y="109" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="279" y="109" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="111" y="133" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="135" y="133" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="159" y="133" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="183" y="133" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="207" y="133" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="231" y="133" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="255" y="133" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="279" y="133" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="111" y="157" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="135" y="157" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="159" y="157" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="183" y="157" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="207" y="157" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="231" y="157" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="255" y="157" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="279" y="157" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="111" y="181" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="135" y="181" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="159" y="181" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="183" y="181" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="207" y="181" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="231" y="181" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="255" y="181" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="279" y="181" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="111" y="205" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="135" y="205" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="159" y="205" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="183" y="205" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="207" y="205" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="231" y="205" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="255" y="205" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="279" y="205" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="111" y="229" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="135" y="229" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="159" y="229" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="183" y="229" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="207" y="229" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="231" y="229" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="255" y="229" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="279" y="229" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><text x="206.0" y="274.0" font-size="12" text-anchor="middle">K=C_in·9 ≥ 8, N=C_out ≥ 8 → 64/64 PE 사용</text><text x="104.0" y="156.0" font-size="12" text-anchor="end">K (감소축)</text>
<text x="206.0" y="50.0" font-size="12" text-anchor="middle">N (출력 채널) →</text><text x="526.0" y="30.0" font-size="13" text-anchor="middle">depthwise (채널 하나 = GEMM 하나)</text><rect x="431" y="61" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="455" y="61" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="479" y="61" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="503" y="61" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="527" y="61" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="551" y="61" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="575" y="61" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="599" y="61" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="431" y="85" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="455" y="85" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="479" y="85" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="503" y="85" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="527" y="85" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="551" y="85" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="575" y="85" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="599" y="85" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="431" y="109" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="455" y="109" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="479" y="109" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="503" y="109" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="527" y="109" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="551" y="109" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="575" y="109" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="599" y="109" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="431" y="133" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="455" y="133" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="479" y="133" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="503" y="133" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="527" y="133" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="551" y="133" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="575" y="133" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="599" y="133" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="431" y="157" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="455" y="157" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="479" y="157" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="503" y="157" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="527" y="157" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="551" y="157" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="575" y="157" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="599" y="157" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="431" y="181" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="455" y="181" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="479" y="181" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="503" y="181" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="527" y="181" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="551" y="181" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="575" y="181" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="599" y="181" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="431" y="205" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="455" y="205" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="479" y="205" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="503" y="205" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="527" y="205" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="551" y="205" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="575" y="205" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="599" y="205" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="431" y="229" width="22" height="22" fill="#4a7bd0" fill-opacity="0.8" stroke="currentColor" stroke-opacity="0.3"/><rect x="455" y="229" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="479" y="229" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/>
<rect x="503" y="229" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="527" y="229" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="551" y="229" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="575" y="229" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><rect x="599" y="229" width="22" height="22" fill="#888" fill-opacity="0.15" stroke="currentColor" stroke-opacity="0.3"/><text x="526.0" y="274.0" font-size="12" text-anchor="middle">K=9, N=1 → 첫 열만: 8/64 PE</text>
<text x="424.0" y="156.0" font-size="12" text-anchor="end">K (감소축)</text><text x="526.0" y="50.0" font-size="12" text-anchor="middle">N (출력 채널) →</text><text x="330.0" y="310.0" font-size="12" text-anchor="middle">8×8 배열 그림 기준(실제 32×32면 depthwise 활용률은 9/1024 ≈ 0.9%)</text>
</svg>
```

그림 3 — 8×8 systolic array에 올린 모습. 표준 conv·1×1 conv는 K와 N이 충분히 커서 모든 PE가 일한다(왼쪽). depthwise는 채널 하나가 N = 1짜리 GEMM이라 한 열만 쓴다(오른쪽). 32×32 격자면 활용률은 9/1024 ≈ 0.9%다.

사이클을 세는 장난감 모델로 숫자를 뽑아 본다. 가정: 32×32 weight-stationary, 타일마다 weight 적재 R사이클 + 입력 M행 스트리밍 + fill/drain R + C 사이클. 실제 NPU는 이보다 훨씬 정교하지만, 비율 감각을 잡기에는 충분하다.

**[ex4]**

```python
# 장난감 모델: weight-stationary 32x32 systolic array의 MAC 활용률 (가정: weight 적재 R cycle + fill/drain R+C)
import math
R = C = 32
def util(M, K, N, groups=1):
    """GEMM [M x K] @ [K x N] 을 groups번 (depthwise면 채널마다 1번)"""
    tiles = math.ceil(K / R) * math.ceil(N / C) * groups
    cycles = tiles * (R + M + R + C)            # weight load + 스트리밍 M행 + fill/drain
    return M * K * N * groups / (cycles * R * C), cycles
P = 56 * 56                                     # 출력 픽셀 수 = GEMM의 M
cases = [
    ("standard3x3 64->64",      P, 9 * 64, 64, 1),
    ("pointwise1x1 64->64",     P, 64, 64, 1),
    ("pointwise1x1 16->96",     P, 16, 96, 1),
    ("depthwise3x3 C=64 naive", P, 9, 1, 64),   # 채널마다 K=9, N=1짜리 GEMM
    ("FC 1024->1024 batch 1",   1, 1024, 1024, 1),
    ("FC 1024->1024 batch 64",  64, 1024, 1024, 1),
]
for name, M, K, N, g in cases:
    u, cyc = util(M, K, N, g)
    print(f"{name:25s} M={M:5d} K={K:5d} N={N:5d} x{g:<3d} util={u*100:6.2f}%  cycles={cyc:8d}")
```

```text
standard3x3 64->64        M= 3136 K=  576 N=   64 x1   util= 97.03%  cycles=  116352
pointwise1x1 64->64       M= 3136 K=   64 N=   64 x1   util= 97.03%  cycles=   12928
pointwise1x1 16->96       M= 3136 K=   16 N=   96 x1   util= 48.51%  cycles=    9696
depthwise3x3 C=64 naive   M= 3136 K=    9 N=    1 x64  util=  0.85%  cycles=  206848
FC 1024->1024 batch 1     M=    1 K= 1024 N= 1024 x1   util=  1.03%  cycles=   99328
FC 1024->1024 batch 64    M=   64 K= 1024 N= 1024 x1   util= 40.00%  cycles=  163840
```

출력에서 볼 것: naive depthwise는 활용률 0.85%이고, **MAC이 64배 적은데도 사이클은 표준 conv보다 많다**(206,848 vs 116,352). 채널마다 타일을 새로 깔고 fill/drain을 반복하기 때문이다. 입력 채널이 16인 pointwise도 K가 32를 못 채워 절반만 쓴다. batch 1 FC(GEMV)는 M = 1이라 격자를 한 번 통과하는 동안 한 행만 흐른다(1%). 이 경우는 사실 활용률 이전에 weight 1 MB를 읽는 대역폭이 먼저 막힌다.

실제 NPU들은 이 문제를 알고 대응한다. depthwise 전용 엔진(채널을 PE 열에 하나씩 대응시키는 channel-parallel 매핑, 3×3 전용 벡터 유닛)을 두거나, depthwise를 뒤의 pointwise와 **fuse**해서 activation을 SRAM 밖으로 내보내지 않는다. 그래서 "depthwise 효율"은 칩마다 크게 다르고, **벤더에게 반드시 물어야 할 항목**이다(9절).

### 3.6 모델 전체로 — MobileNetV2 vs ResNet18

MobileNetV2(약 301 MMAC)는 ResNet18(약 1814 MMAC)보다 MAC이 6배 적다. 두 런타임에서 재 본다. 같은 스크립트를 두 번 돌린 출력이다(`torch.onnx.export`의 경고 줄은 생략).

**[ex11]**

```python
# 같은 모델, 다른 런타임: PyTorch eager vs ONNX Runtime (CPU, fp32, batch 1, 224x224)
import torch, time, statistics as st, numpy as np, onnxruntime as ort, torchvision.models as tvm
torch.manual_seed(0); torch.set_grad_enabled(False)
def bench(f, reps=30):
    for _ in range(5): f()
    ts = []
    for _ in range(reps):
        t0 = time.perf_counter(); f(); ts.append(time.perf_counter() - t0)
    ts.sort(); return st.median(ts), ts[len(ts) // 10]
x = torch.randn(1, 3, 224, 224)
for name, macs, m in [("mobilenet_v2", 300.8e6, tvm.mobilenet_v2(weights=None)),
                      ("resnet18", 1814e6, tvm.resnet18(weights=None))]:
    m.eval(); path = f"/private/tmp/claude-501/d4/{name}.onnx"
    torch.onnx.export(m, (x,), path, input_names=["x"], dynamo=False)
    so = ort.SessionOptions(); so.intra_op_num_threads = 4
    sess = ort.InferenceSession(path, so, providers=["CPUExecutionProvider"])
    xn = x.numpy()
    for rt, f in [("torch eager", lambda: m(x)), ("onnxruntime", lambda: sess.run(None, {"x": xn}))]:
        med, p10 = bench(f)
        print(f"{name:13s} {rt:12s} median {med*1e3:6.2f} ms  p10 {p10*1e3:6.2f} ms  {macs/med/1e9:6.1f} GMAC/s")
```

```text
mobilenet_v2  torch eager  median  22.50 ms  p10  21.77 ms    13.4 GMAC/s
mobilenet_v2  onnxruntime  median   8.50 ms  p10   8.35 ms    35.4 GMAC/s
resnet18      torch eager  median  11.16 ms  p10  10.44 ms   162.6 GMAC/s
resnet18      onnxruntime  median  18.03 ms  p10  13.35 ms   100.6 GMAC/s
mobilenet_v2  torch eager  median  30.21 ms  p10  25.30 ms    10.0 GMAC/s
mobilenet_v2  onnxruntime  median  11.41 ms  p10   8.62 ms    26.4 GMAC/s
resnet18      torch eager  median  12.85 ms  p10  10.91 ms   141.2 GMAC/s
resnet18      onnxruntime  median  15.76 ms  p10  13.67 ms   115.1 GMAC/s
```

출력에서 볼 것:

- PyTorch eager에서는 MobileNetV2가 ResNet18보다 **오히려 느리다**(22.5 ms vs 11.2 ms). depthwise가 느린 generic 경로로 떨어졌기 때문이다. 연산자 하나가 느린 경로로 떨어지면 모델 전체가 무너진다는 것, NPU에서 op 하나가 CPU로 fallback될 때와 같은 현상이다.
- 전용 depthwise 커널이 있는 ONNX Runtime에서는 MobileNetV2가 8.5~11.4 ms, ResNet18이 15.8~18.0 ms다. MAC은 6배 적은데 **1.4~2.1배만 빠르다**. GMAC/s로 보면 MobileNetV2 26~35, ResNet18 100~115로 3~4배 차이다.
- ResNet18은 torch 쪽이 오히려 빠르다. 런타임마다 잘하는 op가 다르다. "이 모델은 X ms"라는 말에는 항상 "어느 런타임, 어느 코어에서"가 붙어야 한다.

### 3.7 함정

- "MobileNet이 MAC이 적으니 NPU에서 훨씬 빠르겠지" → NPU에서 depthwise 효율이 나쁘면 차이가 거의 없거나 역전된다. C7 2절의 "FLOPs가 latency를 못 맞춘다"의 대표 사례다.
- depthwise는 양자화에서도 까다롭다. 채널마다 weight 분포가 크게 달라서 **per-channel 양자화가 사실상 필수**다(C1). per-tensor만 지원하는 오래된 런타임에서는 정확도가 크게 떨어진다.

---

## 4. Pointwise(1×1) = GEMM, 배치 1 dense = GEMV

### 4.1 1×1 conv는 그냥 GEMM이다

1×1 conv는 공간 이웃을 보지 않으므로, NHWC 텐서를 `[H·W, C_in]` 행렬로 보면 `[H·W, C_in] @ [C_in, C_out]` 행렬곱과 완전히 같다. 픽셀 수 H·W가 GEMM의 M 역할을 하므로, 해상도가 충분하면 weight가 픽셀 수만큼 재사용된다. 3.3의 측정에서 pointwise가 300~470 GMAC/s로 가장 빨랐던 이유다. MobileNet v1 논문 기준으로 MAC의 약 95%가 이 1×1 conv에 있고, 이것은 NPU에 잘 맞는다. 문제는 MAC 비중은 작고 시간 비중은 큰 depthwise다.

### 4.2 dense 층 batch 1은 GEMV — weight를 한 번 쓰고 버린다

FC 층 `y = W·x`에 입력 벡터가 하나뿐이면 M = 1인 GEMV다. weight N×N개를 전부 읽어서 각각 딱 한 번 곱한다. fp32면 4 B를 읽고 2 FLOP, intensity 0.5 FLOP/B다(B3 11.2와 같은 식). batch B개의 입력을 묶으면 weight 하나를 B번 재사용한다.

### 4.3 코드로 확인 — batch를 키우면

**[ex5]**

```python
# Linear(N->N) fp32: batch를 키우면 GEMV → GEMM. weight를 몇 번 재사용하느냐가 GMAC/s를 정한다
import torch, time, statistics as st
torch.manual_seed(0); torch.set_grad_enabled(False)
def bench(f, reps=30):
    for _ in range(3): f()
    ts = []
    for _ in range(reps):
        t0 = time.perf_counter(); f(); ts.append(time.perf_counter() - t0)
    return st.median(ts)
for N in [1024, 4096]:                       # weight 4 MB (캐시에 들어감) vs 64 MB (DRAM)
    lin = torch.nn.Linear(N, N, bias=False)
    wbytes = N * N * 4
    print(f"N={N}  weight={wbytes/2**20:.0f} MiB")
    for B in [1, 4, 16, 64, 256]:
        x = torch.randn(B, N)
        t = bench(lambda: lin(x))
        macs = B * N * N
        print(f"  batch {B:3d}: {t*1e3:7.3f} ms  {macs/t/1e9:6.1f} GMAC/s  "
              f"weight stream {wbytes/t/1e9:6.1f} GB/s  AI(fp32)={2*B*N*N/(wbytes+8*B*N):6.1f} FLOP/B")
```

```text
N=1024  weight=4 MiB
  batch   1:   0.031 ms    34.1 GMAC/s  weight stream  136.6 GB/s  AI(fp32)=   0.5 FLOP/B
  batch   4:   0.146 ms    28.7 GMAC/s  weight stream   28.7 GB/s  AI(fp32)=   2.0 FLOP/B
  batch  16:   0.134 ms   125.7 GMAC/s  weight stream   31.4 GB/s  AI(fp32)=   7.8 FLOP/B
  batch  64:   0.216 ms   310.5 GMAC/s  weight stream   19.4 GB/s  AI(fp32)=  28.4 FLOP/B
  batch 256:   0.649 ms   413.5 GMAC/s  weight stream    6.5 GB/s  AI(fp32)=  85.3 FLOP/B
N=4096  weight=64 MiB
  batch   1:   1.943 ms     8.6 GMAC/s  weight stream   34.5 GB/s  AI(fp32)=   0.5 FLOP/B
  batch   4:   8.194 ms     8.2 GMAC/s  weight stream    8.2 GB/s  AI(fp32)=   2.0 FLOP/B
  batch  16:   9.456 ms    28.4 GMAC/s  weight stream    7.1 GB/s  AI(fp32)=   7.9 FLOP/B
  batch  64:  10.430 ms   102.9 GMAC/s  weight stream    6.4 GB/s  AI(fp32)=  31.0 FLOP/B
  batch 256:  20.098 ms   213.7 GMAC/s  weight stream    3.3 GB/s  AI(fp32)= 113.8 FLOP/B
```

출력에서 볼 것:

- **N = 4096(weight 64 MiB, 캐시보다 큼)** batch 1은 8.6 GMAC/s, weight를 34.5 GB/s로 읽고 있다. M2의 공칭 메모리 대역폭은 100 GB/s지만 CPU 코어 몇 개로 낼 수 있는 값은 그보다 낮다. 이 층은 **DRAM 대역폭이 속도를 정한다.** batch 256에서는 214 GMAC/s로 25배다. weight를 읽는 양은 같은데 연산이 256배가 되었기 때문이다.
- **N = 1024(weight 4 MiB, 캐시에 들어감)** batch 1은 34 GMAC/s이고 "weight stream"이 136 GB/s로 DRAM 대역폭보다 크다. 반복 측정이라 weight가 캐시에 남아 있어서다. **작은 weight가 on-chip 메모리에 상주하면 GEMV도 버틸 만하다.** MCU에서 작은 RNN이 괜찮은 이유와 같다(5절).
- batch 4가 batch 1보다 느린 **절벽**이 있다(0.146 ms vs 0.031 ms). batch 1은 전용 GEMV 경로, batch 4는 작은 M에 최적화되지 않은 GEMM 경로를 탄 것으로 보인다. 커널 선택이 모양에 따라 갈리는 전형적인 모습이다. 실제 기기에서도 "batch를 2로 바꿨더니 느려졌다"가 일어날 수 있다.

### 4.4 지금까지의 측정을 한 장에

```svg
<svg viewBox="0 0 680 334" xmlns="http://www.w3.org/2000/svg">
<line x1="190.0" y1="24" x2="190.0" y2="294" stroke="currentColor" stroke-opacity="0.25"/><text x="190.0" y="310.0" font-size="12" text-anchor="middle">1</text><line x1="340.0" y1="24" x2="340.0" y2="294" stroke="currentColor" stroke-opacity="0.25"/><text x="340.0" y="310.0" font-size="12" text-anchor="middle">10</text><line x1="490.0" y1="24" x2="490.0" y2="294" stroke="currentColor" stroke-opacity="0.25"/><text x="490.0" y="310.0" font-size="12" text-anchor="middle">100</text><line x1="640.0" y1="24" x2="640.0" y2="294" stroke="currentColor" stroke-opacity="0.25"/><text x="640.0" y="310.0" font-size="12" text-anchor="middle">1000</text>
<text x="415.0" y="328.0" font-size="12" text-anchor="middle">측정 GMAC/s (log 축, fp32, M2 CPU 4 threads 중앙값)</text><text x="184.0" y="45.0" font-size="12" text-anchor="end">pointwise 1×1 C=256</text><rect x="190" y="34" width="400.5" height="14" fill="#4a7bd0"/><text x="594.5" y="45.0" font-size="12" text-anchor="start">467.7</text><text x="184.0" y="67.0" font-size="12" text-anchor="end">FC 1024 배치 256</text><rect x="190" y="56" width="392.5" height="14" fill="#4a7bd0"/><text x="586.5" y="67.0" font-size="12" text-anchor="start">413.5</text><text x="184.0" y="89.0" font-size="12" text-anchor="end">표준 3×3 C=256</text><rect x="190" y="78" width="380.6" height="14" fill="#4a7bd0"/>
<text x="574.6" y="89.0" font-size="12" text-anchor="start">344.7</text><text x="184.0" y="111.0" font-size="12" text-anchor="end">pointwise 1×1 C=64</text><rect x="190" y="100" width="373.3" height="14" fill="#4a7bd0"/><text x="567.3" y="111.0" font-size="12" text-anchor="start">308.3</text><text x="184.0" y="133.0" font-size="12" text-anchor="end">표준 3×3 C=64</text><rect x="190" y="122" width="365.1" height="14" fill="#4a7bd0"/><text x="559.1" y="133.0" font-size="12" text-anchor="start">271.5</text><text x="184.0" y="155.0" font-size="12" text-anchor="end">LLM prefill 256 tok</text><rect x="190" y="144" width="364.5" height="14" fill="#3f9a6b"/>
<text x="558.5" y="155.0" font-size="12" text-anchor="start">269</text><text x="184.0" y="177.0" font-size="12" text-anchor="end">FC 1024 배치 1 (GEMV)</text><rect x="190" y="166" width="229.9" height="14" fill="#e08a3c"/><text x="423.9" y="177.0" font-size="12" text-anchor="start">34.1</text><text x="184.0" y="199.0" font-size="12" text-anchor="end">depthwise 3×3 C=256</text><rect x="190" y="188" width="161.3" height="14" fill="#d0564a"/><text x="355.3" y="199.0" font-size="12" text-anchor="start">11.9</text><text x="184.0" y="221.0" font-size="12" text-anchor="end">depthwise 3×3 C=64</text><rect x="190" y="210" width="151.3" height="14" fill="#d0564a"/>
<text x="345.3" y="221.0" font-size="12" text-anchor="start">10.2</text><text x="184.0" y="243.0" font-size="12" text-anchor="end">FC 4096 배치 1 (GEMV)</text><rect x="190" y="232" width="140.2" height="14" fill="#e08a3c"/><text x="334.2" y="243.0" font-size="12" text-anchor="start">8.6</text><text x="184.0" y="265.0" font-size="12" text-anchor="end">LLM decode ctx 16</text><rect x="190" y="254" width="133.0" height="14" fill="#3f9a6b"/><text x="327.0" y="265.0" font-size="12" text-anchor="start">7.7</text><text x="184.0" y="287.0" font-size="12" text-anchor="end">GRUCell H=128 (1 thread)</text><rect x="190" y="276" width="36.8" height="14" fill="#e08a3c"/>
<text x="230.8" y="287.0" font-size="12" text-anchor="start">1.76</text><text x="190.0" y="18.0" font-size="12" text-anchor="start">파랑 = GEMM형 conv/FC · 초록 = transformer · 주황 = GEMV형 · 빨강 = depthwise</text>
</svg>
```

그림 4 — 이 Mac에서 잰 계열별 GMAC/s(log 축). 파랑(GEMM형 conv·FC)은 270~470, 초록 prefill도 수백인데, 주황(GEMV형)·빨강(depthwise)·초록 decode는 2~34에 모여 있다. LLM 두 값은 6절 측정(ex7)에서 유도했다: prefill 256 토큰은 약 36.7 GMAC을 136.4 ms에, decode는 토큰당 약 134.5 M MAC을 17.56 ms에 처리했다. GRUCell은 1 thread 측정이라 나머지와 조건이 다르다.

말로 하면: 같은 CPU에서 계열에 따라 효율이 **두 자릿수 배** 차이 난다. 모델 latency를 예측하려면 MAC을 "그 계열의 실효 GMAC/s"로 나눠야 하고, 그 값이 계열마다 다르다. C7의 latency LUT가 레이어 종류별로 따로 측정하는 이유다.

---

## 5. RNN · GRU · LSTM — 순차적인 작은 GEMV

### 5.1 직관

RNN은 매 step `h_t = f(W_ih·x_t + W_hh·h_{t−1})`를 계산한다(B3). `W_hh·h_{t−1}`은 GEMV이고, **다음 step은 이번 step 결과가 나와야 시작**할 수 있다. 시간 축으로는 병렬화가 안 된다. 펌웨어로 치면 IIR 필터를 샘플 하나씩 돌리는 것과 같다.

- 연산량: step당 GRU `3·H·(I + H)` MAC, LSTM `4·H·(I + H)` MAC. 시퀀스 T개면 T배. **총 시간은 T에 선형**이다.
- 상태: H개(LSTM은 2H) 원소. H = 128이어도 int8 128 B.
- step당 weight를 전부 한 번씩 읽는다. intensity ≈ 2 FLOP/B(int8). 1절 표의 GRU 행이다.

### 5.2 손계산 — MCU에서 GRU 한 step

GRU I = 40, H = 64, 1층: `3·64·(40 + 64) = 19,968 MAC`. weight int8 약 20 KB. Cortex-M4/M33급에서 int8 SIMD로 오버헤드 포함 사이클당 약 1 MAC을 낸다고 **가정**하면 약 2만 사이클, 100 MHz에서 0.2 ms다. 10 ms 오디오 프레임의 2%다. weight 20 KB는 SRAM에 상주할 수 있어서 memory-bound라도 문제가 되지 않는다. RNN이 MCU에서 여전히 인기 있는 이유다.

### 5.3 코드로 확인 — hidden 크기, 시퀀스 길이, 스트림 묶기

작은 코어를 흉내 내려고 torch를 1 thread로 고정한다.

**[ex6]**

```python
# GRU: (a) hidden 크기별 step당 latency  (b) 시퀀스 길이별 총 latency  (c) 스트림 B개를 묶으면
import torch, time, statistics as st
torch.manual_seed(0); torch.set_grad_enabled(False); torch.set_num_threads(1)   # 작은 코어 1개 흉내
def bench(f, reps=30):
    for _ in range(3): f()
    ts = []
    for _ in range(reps):
        t0 = time.perf_counter(); f(); ts.append(time.perf_counter() - t0)
    return st.median(ts)
I = 40
print("(a) GRUCell 1 step, batch 1")
for H in [32, 64, 128, 256, 512, 1024]:
    cell = torch.nn.GRUCell(I, H); x, h = torch.randn(1, I), torch.zeros(1, H)
    macs = 3 * H * (I + H); wkb = 4 * macs / 1e3
    t = bench(lambda: cell(x, h), reps=200)
    print(f"  H={H:4d}  MAC={macs:8d}  weight={wkb:7.1f} KB  {t*1e6:7.1f} us/step  {macs/t/1e9:5.2f} GMAC/s")
gru = torch.nn.GRU(I, 128, batch_first=True)
print("(b) nn.GRU H=128, 전체 시퀀스 한 번 호출")
for T in [100, 200, 400, 800, 1600]:
    x = torch.randn(1, T, I); t = bench(lambda: gru(x))
    print(f"  T={T:5d}  {t*1e3:7.2f} ms   {t/T*1e6:5.1f} us/step")
print("(c) T=400, 독립 스트림 B개를 batch로 묶기")
for B in [1, 4, 16, 64]:
    x = torch.randn(B, 400, I); t = bench(lambda: gru(x))
    print(f"  B={B:3d}  {t*1e3:7.2f} ms   {t/(400*B)*1e6:6.2f} us per stream-step")
```

```text
(a) GRUCell 1 step, batch 1
  H=  32  MAC=    6912  weight=   27.6 KB     12.9 us/step   0.54 GMAC/s
  H=  64  MAC=   19968  weight=   79.9 KB     13.9 us/step   1.44 GMAC/s
  H= 128  MAC=   64512  weight=  258.0 KB     36.7 us/step   1.76 GMAC/s
  H= 256  MAC=  227328  weight=  909.3 KB     31.4 us/step   7.25 GMAC/s
  H= 512  MAC=  847872  weight= 3391.5 KB     59.5 us/step  14.24 GMAC/s
  H=1024  MAC= 3268608  weight=13074.4 KB    187.2 us/step  17.46 GMAC/s
(b) nn.GRU H=128, 전체 시퀀스 한 번 호출
  T=  100     1.29 ms    12.9 us/step
  T=  200     2.47 ms    12.4 us/step
  T=  400     5.07 ms    12.7 us/step
  T=  800    10.43 ms    13.0 us/step
  T= 1600    20.13 ms    12.6 us/step
(c) T=400, 독립 스트림 B개를 batch로 묶기
  B=  1     5.08 ms    12.71 us per stream-step
  B=  4     9.72 ms     6.07 us per stream-step
  B= 16    15.54 ms     2.43 us per stream-step
  B= 64    50.62 ms     1.98 us per stream-step
```

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="230" x2="310" y2="230" stroke="currentColor"/><line x1="60" y1="30" x2="60" y2="230" stroke="currentColor"/><text x="60.0" y="246.0" font-size="12" text-anchor="middle">0</text><text x="122.5" y="246.0" font-size="12" text-anchor="middle">400</text><text x="185.0" y="246.0" font-size="12" text-anchor="middle">800</text><text x="247.5" y="246.0" font-size="12" text-anchor="middle">1200</text><text x="310.0" y="246.0" font-size="12" text-anchor="middle">1600</text><text x="54.0" y="234.0" font-size="12" text-anchor="end">0</text><text x="54.0" y="188.5" font-size="12" text-anchor="end">5</text><text x="54.0" y="143.1" font-size="12" text-anchor="end">10</text>
<text x="54.0" y="97.6" font-size="12" text-anchor="end">15</text><text x="54.0" y="52.2" font-size="12" text-anchor="end">20</text><line x1="60.0" y1="230.0" x2="310.0" y2="45.3" stroke="#888" stroke-dasharray="4 3"/><polyline points="75.6,218.3 91.2,207.5 122.5,183.9 185.0,135.2 310.0,47.0" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="75.6" cy="218.3" r="4" fill="#4a7bd0"/><circle cx="91.2" cy="207.5" r="4" fill="#4a7bd0"/><circle cx="122.5" cy="183.9" r="4" fill="#4a7bd0"/><circle cx="185.0" cy="135.2" r="4" fill="#4a7bd0"/><circle cx="310.0" cy="47.0" r="4" fill="#4a7bd0"/>
<text x="185.0" y="18.0" font-size="12" text-anchor="middle">nn.GRU H=128: 총 latency (ms) vs 길이 T</text><text x="185.0" y="264.0" font-size="12" text-anchor="middle">시퀀스 길이 T (step)</text><text x="169.4" y="102.7" font-size="12" text-anchor="middle">점선 = 12.7 µs × T</text><line x1="400" y1="230" x2="650" y2="230" stroke="currentColor"/><line x1="400" y1="30" x2="400" y2="230" stroke="currentColor"/><text x="400.0" y="246.0" font-size="12" text-anchor="middle">32</text><text x="450.0" y="246.0" font-size="12" text-anchor="middle">64</text><text x="500.0" y="246.0" font-size="12" text-anchor="middle">128</text><text x="550.0" y="246.0" font-size="12" text-anchor="middle">256</text>
<text x="600.0" y="246.0" font-size="12" text-anchor="middle">512</text><text x="650.0" y="246.0" font-size="12" text-anchor="middle">1024</text><text x="394.0" y="234.0" font-size="12" text-anchor="end">0</text><text x="394.0" y="184.0" font-size="12" text-anchor="end">50</text><text x="394.0" y="134.0" font-size="12" text-anchor="end">100</text><text x="394.0" y="84.0" font-size="12" text-anchor="end">150</text><text x="394.0" y="34.0" font-size="12" text-anchor="end">200</text><polyline points="400.0,217.1 450.0,216.1 500.0,193.3 550.0,198.6 600.0,170.5 650.0,42.8" fill="none" stroke="#e08a3c" stroke-width="2"/><circle cx="400.0" cy="217.1" r="4" fill="#e08a3c"/>
<circle cx="450.0" cy="216.1" r="4" fill="#e08a3c"/><circle cx="500.0" cy="193.3" r="4" fill="#e08a3c"/><circle cx="550.0" cy="198.6" r="4" fill="#e08a3c"/><circle cx="600.0" cy="170.5" r="4" fill="#e08a3c"/><circle cx="650.0" cy="42.8" r="4" fill="#e08a3c"/><text x="525.0" y="18.0" font-size="12" text-anchor="middle">GRUCell 1 step: µs vs hidden H (log2)</text><text x="525.0" y="264.0" font-size="12" text-anchor="middle">hidden size H</text><text x="406.0" y="130.0" font-size="12" text-anchor="start">H ≤ 64: 호출 오버헤드 바닥</text><text x="406.0" y="146.0" font-size="12" text-anchor="start">(~13 µs, MAC과 무관)</text>
<text x="340.0" y="282.0" font-size="12" text-anchor="middle">M2 CPU, torch 1 thread, 중앙값 30회 — 오른쪽 H=128 점은 잡음으로 튄 값</text>
</svg>
```

그림 5 — 왼쪽: 전체 시퀀스를 한 번 호출해도 총 latency는 T에 정확히 비례한다(step당 약 12.7 µs, 점선). 오른쪽: step당 latency는 H ≤ 64에서 약 13 µs로 바닥을 친다. 이 구간에서는 MAC이 아니라 호출·dispatch 오버헤드가 시간을 정한다. H = 128 점(36.7 µs)은 다시 재면 20 µs 정도로 나오는 잡음이다.

출력에서 볼 것:

- **(a)** H = 32→64에서 MAC은 3배지만 시간은 거의 같다(13 µs). 작은 GEMV는 프레임워크 오버헤드에 묻힌다. H = 1024에서는 weight가 13 MB라 GMAC/s가 17 정도에서 멈춘다. GEMM형 conv(수백 GMAC/s)의 1/20이다.
- **(b)** T를 16배 늘리면 시간도 16배(1.29 → 20.13 ms). step당 시간이 일정하다. 이것이 "순차 의존성"의 비용이다. 길이를 늘려도 병렬성이 늘지 않는다.
- **(c)** 독립 스트림 B개를 batch로 묶으면 stream-step당 비용이 12.7 → 2.0 µs로 줄어든다. GEMV가 `[3H×H] @ [H×B]` GEMM이 되어 weight를 B번 재사용하기 때문이다. 서버에서 여러 사용자 음성을 한꺼번에 처리할 때 쓰는 방법이지만, 웨어러블은 스트림이 하나라 이 이득을 못 얻는다.

### 5.4 NPU와 MCU에서 반대로 보이는 이유

| 관점 | NPU / GPU | MCU / DSP |
|---|---|---|
| 순차 의존성 | 큰 MAC 배열이 매 step 1% 남짓만 일한다 | 어차피 MAC이 사이클당 몇 개라 손해가 작다 |
| step마다의 고정 비용 | 커널 launch, DMA 설정, 동기화가 step마다 붙는다 | 함수 호출 한 번, 거의 0 |
| weight 위치 | DRAM에서 매 step 다시 읽는 경우가 많다 | 작은 weight가 SRAM/TCM에 상주 |
| 상태 메모리 | 문제 안 됨 | O(H) 수백 바이트 — 큰 장점 |
| 지원 op | sigmoid/tanh gate, 순환 그래프를 지원 안 하는 컴파일러가 있다 | CMSIS-NN 등에 LSTM/GRU 커널이 있다 |

말로 하면: RNN은 "가속기가 싫어하고 MCU가 좋아하는" 계열이다. 상세한 C 구현과 양자화 누적 오차는 B3 9절·13절에 있다.

### 5.5 함정

- 입력 투영 `W_ih·x_t`는 순환이 아니므로 시퀀스 전체를 한 번에 GEMM으로 계산할 수 있다(B3 11.2). 오프라인 처리라면 이 부분을 분리하는 것만으로 꽤 빨라진다. 스트리밍에서는 프레임이 하나씩 오므로 해당 없다.
- LSTM/GRU를 NPU에 올릴 때 컴파일러가 루프를 T번 unroll해서 그래프가 거대해지거나, 순환 부분 전체를 CPU로 보내는 경우가 있다. 변환 로그에서 "fallback" op를 꼭 확인한다.

---

## 6. Transformer — prefill은 GEMM, decode는 GEMV

### 6.1 직관

decoder-only transformer의 추론은 두 단계다(B4 9절, 자세한 LLM 성능 모델은 **D5**).

- **prefill**: prompt T개 토큰을 한 번에 처리한다. 모든 선형층이 `[T × d] @ [d × d']` GEMM이라 weight가 T번 재사용된다. compute-bound. 여기에 attention의 `QKᵀ`, `AV`가 T²에 비례해 붙는다.
- **decode**: 토큰을 하나씩 만든다. 선형층이 `[1 × d] @ [d × d']` GEMV다. 모든 weight를 토큰 하나를 위해 다시 읽는다. 여기에 과거 모든 토큰의 K, V(KV-cache)를 읽는다. memory-bound.

같은 모델이 한 번은 conv처럼, 한 번은 RNN처럼 움직인다. 이것이 transformer를 한 계열로 뭉뚱그려 말하면 안 되는 이유다.

### 6.2 코드로 확인 — SmolLM2-135M으로 재기

로컬 캐시의 SmolLM2-135M-Instruct(Llama 구조, 30층, d = 576, query head 9, KV head 3, head_dim 64)를 fp32로 올려 prompt 길이 L별 prefill 시간과, 그 context 뒤에 토큰 하나를 만드는 decode 시간을 잰다(M2 CPU, 4 threads, urllib3 경고 줄 생략). decode는 25번 반복의 중앙값이고, `p10` 열은 정렬한 25개 중 3번째 값(약 p10)이다.

**[ex7]**

```python
# SmolLM2-135M-Instruct (fp32, CPU): prefill 시간 vs prompt 길이, decode 1 token 시간 vs context 길이
import torch, time, statistics as st
from transformers import AutoModelForCausalLM
torch.manual_seed(0); torch.set_grad_enabled(False)
m = AutoModelForCausalLM.from_pretrained("HuggingFaceTB/SmolLM2-135M-Instruct", local_files_only=True,
                                         dtype=torch.float32).eval()
cfg = m.config; nparam = sum(p.numel() for p in m.parameters())
kv_per_tok = 2 * cfg.num_hidden_layers * cfg.num_key_value_heads * (cfg.hidden_size // cfg.num_attention_heads) * 4
print(f"params {nparam/1e6:.1f} M, weight {nparam*4/2**20:.0f} MiB fp32, KV {kv_per_tok} B/token fp32")
print(f"{'ctx L':>6s} {'prefill ms':>10s} {'ms/tok(pref)':>12s} {'decode ms/tok':>13s} {'p10':>6s} {'KV MiB':>7s}")
for L in [16, 64, 256, 1024, 2048]:
    ids = torch.randint(100, 40000, (1, L))
    pt = []
    for _ in range(3):
        t0 = time.perf_counter(); out = m(ids, use_cache=True); pt.append(time.perf_counter() - t0)
    cache, nxt = out.past_key_values, ids[:, -1:]
    dt = []
    for _ in range(25):                                 # context가 L → L+25 로 조금씩 늘어남
        t0 = time.perf_counter(); out = m(nxt, past_key_values=cache, use_cache=True)
        dt.append(time.perf_counter() - t0); cache = out.past_key_values
    dt.sort(); p = st.median(pt)
    print(f"{L:6d} {p*1e3:10.1f} {p/L*1e3:12.2f} {st.median(dt)*1e3:13.2f} {dt[2]*1e3:6.2f} {kv_per_tok*L/2**20:7.1f}")
```

```text
params 134.5 M, weight 513 MiB fp32, KV 46080 B/token fp32
 ctx L prefill ms ms/tok(pref) decode ms/tok    p10  KV MiB
    16       38.6         2.41         17.56  17.15     0.7
    64       57.5         0.90         18.82  18.58     2.8
   256      136.4         0.53         22.26  21.41    11.2
  1024      967.3         0.94         28.53  27.42    45.0
  2048     3023.1         1.48         46.88  43.37    90.0
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="240" x2="560" y2="240" stroke="currentColor"/><line x1="70" y1="36" x2="70" y2="240" stroke="currentColor"/><text x="70.0" y="256.0" font-size="12" text-anchor="middle">16</text><text x="210.0" y="256.0" font-size="12" text-anchor="middle">64</text><text x="350.0" y="256.0" font-size="12" text-anchor="middle">256</text><text x="490.0" y="256.0" font-size="12" text-anchor="middle">1024</text><text x="560.0" y="256.0" font-size="12" text-anchor="middle">2048</text><line x1="70" y1="220.3" x2="560" y2="220.3" stroke="currentColor" stroke-opacity="0.12"/><text x="64.0" y="224.3" font-size="12" text-anchor="end">0.5</text>
<line x1="70" y1="193.6" x2="560" y2="193.6" stroke="currentColor" stroke-opacity="0.12"/><text x="64.0" y="197.6" font-size="12" text-anchor="end">1</text><line x1="70" y1="167.0" x2="560" y2="167.0" stroke="currentColor" stroke-opacity="0.12"/><text x="64.0" y="171.0" font-size="12" text-anchor="end">2</text><line x1="70" y1="131.7" x2="560" y2="131.7" stroke="currentColor" stroke-opacity="0.12"/><text x="64.0" y="135.7" font-size="12" text-anchor="end">5</text><line x1="70" y1="105.0" x2="560" y2="105.0" stroke="currentColor" stroke-opacity="0.12"/><text x="64.0" y="109.0" font-size="12" text-anchor="end">10</text>
<line x1="70" y1="78.3" x2="560" y2="78.3" stroke="currentColor" stroke-opacity="0.12"/><text x="64.0" y="82.3" font-size="12" text-anchor="end">20</text><line x1="70" y1="43.0" x2="560" y2="43.0" stroke="currentColor" stroke-opacity="0.12"/><text x="64.0" y="47.0" font-size="12" text-anchor="end">50</text><polyline points="70.0,83.3 210.0,80.6 350.0,74.2 490.0,64.6 560.0,45.5" fill="none" stroke="#d0564a" stroke-width="2"/><circle cx="70.0" cy="83.3" r="4" fill="#d0564a"/><text x="76.0" y="76.3" font-size="12" text-anchor="start">17.56</text><circle cx="210.0" cy="80.6" r="4" fill="#d0564a"/><text x="216.0" y="73.6" font-size="12" text-anchor="start">18.82</text>
<circle cx="350.0" cy="74.2" r="4" fill="#d0564a"/><text x="356.0" y="67.2" font-size="12" text-anchor="start">22.26</text><circle cx="490.0" cy="64.6" r="4" fill="#d0564a"/><text x="496.0" y="57.6" font-size="12" text-anchor="start">28.53</text><circle cx="560.0" cy="45.5" r="4" fill="#d0564a"/><text x="566.0" y="38.5" font-size="12" text-anchor="start">46.88</text><text x="568.0" y="49.5" font-size="12" text-anchor="start">decode</text><polyline points="70.0,159.8 210.0,197.7 350.0,218.1 490.0,196.0 560.0,178.5" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="70.0" cy="159.8" r="4" fill="#4a7bd0"/><text x="76.0" y="152.8" font-size="12" text-anchor="start">2.41</text>
<circle cx="210.0" cy="197.7" r="4" fill="#4a7bd0"/><text x="216.0" y="190.7" font-size="12" text-anchor="start">0.9</text><circle cx="350.0" cy="218.1" r="4" fill="#4a7bd0"/><text x="356.0" y="211.1" font-size="12" text-anchor="start">0.53</text><circle cx="490.0" cy="196.0" r="4" fill="#4a7bd0"/><text x="496.0" y="189.0" font-size="12" text-anchor="start">0.94</text><circle cx="560.0" cy="178.5" r="4" fill="#4a7bd0"/><text x="566.0" y="171.5" font-size="12" text-anchor="start">1.48</text><text x="568.0" y="182.5" font-size="12" text-anchor="start">prefill</text><text x="315.0" y="20.0" font-size="12" text-anchor="middle">SmolLM2-135M fp32, M2 CPU: 토큰당 ms (log) vs context 길이</text>
<text x="315.0" y="276.0" font-size="12" text-anchor="middle">context / prompt 길이 L (log2)</text><text x="315.0" y="290.0" font-size="12" text-anchor="middle">decode는 weight 513 MiB를 토큰마다 다시 읽는다 → prefill보다 토큰당 20~40배 느림</text>
</svg>
```

그림 6 — 토큰당 시간(log 축). decode(빨강)는 짧은 context에서도 17.6 ms이고 context가 길어지면 늘어난다. prefill(파랑)은 토큰당 0.5~2.4 ms로 decode보다 한 자릿수 이상 싸다. L = 16에서는 GEMM이 얇아 비효율적이고, L ≥ 1024에서는 T² attention 때문에 다시 올라간다.

출력에서 볼 것:

- **decode는 memory-bound다.** L = 16에서 17.56 ms/token이다. 토큰마다 weight 513 MiB를 읽으니 실효 대역폭은 513 MiB / 17.56 ms ≈ 30.6 GB/s로, 4.3절 GEMV의 34.5 GB/s와 거의 같다. 연산은 토큰당 약 134.5 M MAC이라 7.7 GMAC/s밖에 안 된다. **decode 속도는 "모델 바이트 ÷ 메모리 대역폭"이 정한다**(D5의 tokens/s 상한 식).
- **prefill은 compute-bound다.** L = 256에서 토큰당 0.53 ms, decode보다 42배 싸다. weight를 한 번 읽어 256개 토큰에 재사용하기 때문이다.
- **context가 길어지면 decode도 느려진다.** L = 2048에서 46.88 ms로 2.7배다. KV-cache가 fp32로 90 MiB라 읽을 바이트가 약 18% 늘어난 것치고는 너무 많이 느려졌다. HF의 `DynamicCache`는 매 step `torch.cat`으로 KV 텐서를 새로 이어 붙이므로 캐시 전체를 복사하는 트래픽이 추가되고, attention 커널의 비효율도 섞여 있다. **KV-cache를 미리 할당한 링버퍼/정적 버퍼로 관리하는 것**이 edge 런타임(llama.cpp 등)이 하는 일이다. 펌웨어의 "매번 realloc 대신 고정 풀" 원칙과 같다.

### 6.3 attention의 T² 비중과 KV 대역폭 — 산술로

SmolLM2-135M 모양으로 두 가지를 계산한다. (1) prefill에서 attention(`QKᵀ`, `AV`) MAC이 전체의 몇 %인가. (2) decode 1 토큰이 읽는 바이트와, 가정한 두 메모리 대역폭에서의 tokens/s 상한.

**[ex8]**

```python
# SmolLM2-135M 모양으로 (1) prefill에서 attention(T²) 항의 MAC 비중 (2) decode 1 token이 읽는 바이트
Lyr, d, dkv, ff, V = 30, 576, 192, 1536, 49152
w_layer = 2 * d * d + 2 * d * dkv + 3 * d * ff          # q,o + k,v + gate/up/down (param 수)
w_all = Lyr * w_layer + V * d                            # + embedding(=lm_head, tied)
print(f"params ≈ {w_all/1e6:.1f} M")
print("(1) prefill T tokens, 층당 MAC: linear = T·w_layer, attention = 2·T²·d (QKᵀ + AV, causal 절약 無)")
for T in [128, 512, 1024, 2048, 4096, 8192]:
    lin, att = T * w_layer, 2 * T * T * d
    print(f"  T={T:5d}  linear {lin/1e9:6.2f} GMAC  attn {att/1e9:6.2f} GMAC  attn share {att/(lin+att)*100:5.1f}%")
print("(2) decode 1 token: weight int8(1B) + KV fp16(2B) 를 전부 한 번씩 읽는다고 가정")
kv_tok = 2 * Lyr * dkv * 2                               # K,V × 층 × kv_dim × 2B
for L in [0, 512, 2048, 8192]:
    wb, kvb = w_all * 1, kv_tok * L
    for bw in [8.5, 51.2]:                                # GB/s: LPDDR4X x16 급 / LPDDR5 x64 급 (가정)
        print(f"  ctx={L:5d}  weight {wb/2**20:5.0f} MiB + KV {kvb/2**20:5.1f} MiB"
              f"  @ {bw:4.1f} GB/s -> ≤ {bw*1e9/(wb+kvb):6.1f} tok/s")
```

```text
params ≈ 134.5 M
(1) prefill T tokens, 층당 MAC: linear = T·w_layer, attention = 2·T²·d (QKᵀ + AV, causal 절약 無)
  T=  128  linear   0.45 GMAC  attn   0.02 GMAC  attn share   4.0%
  T=  512  linear   1.81 GMAC  attn   0.30 GMAC  attn share  14.3%
  T= 1024  linear   3.62 GMAC  attn   1.21 GMAC  attn share  25.0%
  T= 2048  linear   7.25 GMAC  attn   4.83 GMAC  attn share  40.0%
  T= 4096  linear  14.50 GMAC  attn  19.33 GMAC  attn share  57.1%
  T= 8192  linear  28.99 GMAC  attn  77.31 GMAC  attn share  72.7%
(2) decode 1 token: weight int8(1B) + KV fp16(2B) 를 전부 한 번씩 읽는다고 가정
  ctx=    0  weight   128 MiB + KV   0.0 MiB  @  8.5 GB/s -> ≤   63.2 tok/s
  ctx=    0  weight   128 MiB + KV   0.0 MiB  @ 51.2 GB/s -> ≤  380.7 tok/s
  ctx=  512  weight   128 MiB + KV  11.2 MiB  @  8.5 GB/s -> ≤   58.1 tok/s
  ctx=  512  weight   128 MiB + KV  11.2 MiB  @ 51.2 GB/s -> ≤  350.0 tok/s
  ctx= 2048  weight   128 MiB + KV  45.0 MiB  @  8.5 GB/s -> ≤   46.8 tok/s
  ctx= 2048  weight   128 MiB + KV  45.0 MiB  @ 51.2 GB/s -> ≤  281.8 tok/s
  ctx= 8192  weight   128 MiB + KV 180.0 MiB  @  8.5 GB/s -> ≤   26.3 tok/s
  ctx= 8192  weight   128 MiB + KV 180.0 MiB  @ 51.2 GB/s -> ≤  158.4 tok/s
```

출력에서 볼 것:

- d = 576인 작은 모델에서는 T = 1024에서 이미 attention이 MAC의 25%, T = 4096에서는 57%다. 층당 선형 부분은 `T × 약 3.5 M`, attention은 `2·T²·d`이므로 둘이 같아지는 T는 `w_layer / (2d)` ≈ 3070이다. **모델이 작을수록(d가 작을수록) T² 항이 더 일찍 지배한다.** edge용 작은 transformer가 긴 context에 특히 약한 이유다.
- decode 상한: int8 weight 128 MiB만 읽어도 8.5 GB/s 메모리에서는 63 tok/s가 최대다. context 8192에서 fp16 KV가 180 MiB로 **weight보다 커진다.** GQA(KV head 3개)를 이미 쓰는데도 그렇다. 긴 context에서는 KV-cache가 대역폭의 주인공이 된다. KV 양자화, sliding window, GQA/MQA의 가치가 여기서 나온다(D5, B4 5절).

### 6.4 KV-cache를 다른 계열의 "상태"와 비교하면

| 계열 | 호출 사이에 남기는 것 | 크기 | 시간에 따라 |
|---|---|---|---|
| 표준/depthwise CNN (배치 추론) | 없음 | 0 | 일정 |
| 1D CNN 스트리밍 | 층별 과거 `(k−1)·dilation` 프레임 링버퍼 | receptive field에 비례 | 일정 |
| RNN/GRU | 상태 h (LSTM은 h, c) | H 또는 2H | 일정 |
| Transformer (창 없음) | 모든 과거 토큰의 K, V | `2·층·kv_heads·head_dim·T·바이트` | **T에 선형 증가** |
| Transformer (sliding window W) | 최근 W개 토큰의 K, V | W에 비례 | 일정 (W 도달 후) |
| SSM/linear attention (B9) | 고정 크기 상태 행렬 | d·N | 일정 |

말로 하면: KV-cache는 "자라는 상태"다. 다른 계열의 상태는 크기가 고정이라 SRAM 예산을 한 번 잡으면 끝이지만, KV-cache는 context 길이 정책을 정해야 메모리 예산이 정해진다.

### 6.5 NPU에서 transformer가 까다로운 이유

- **op 다양성**: softmax(지수·나눗셈·행 최댓값), LayerNorm/RMSNorm(평균·분산·역제곱근), GELU/SiLU, RoPE(sin/cos 회전), gather(embedding). conv 중심 NPU에는 이 op들이 없거나 느린 벡터 유닛에서 돈다.
- **동적 shape**: context 길이가 매 토큰 바뀐다. 고정 shape로 컴파일하는 NPU는 최대 길이로 패딩하거나 길이별로 여러 그래프를 만든다.
- **양자화 민감도**: activation 이상값(outlier), softmax·LayerNorm의 넓은 동적 범위 때문에 int8만으로는 정확도가 떨어지는 경우가 많다(C3).
- **KV-cache의 위치**: SRAM에 들어가면 decode가 빨라지고, DRAM에 있으면 매 토큰 대역폭을 먹는다.

---

## 7. 오디오 · IMU 스트리밍 모델 — 프레임 예산 안에서의 비교

### 7.1 프레임 예산

스트리밍 모델은 "평균 latency"가 아니라 **프레임 deadline**으로 평가한다(D6).

```
 오디오 16 kHz, hop 10 ms  → 프레임 100개/s, 한 프레임 처리 예산 < 10 ms (보통 그 일부만 허용)
 IMU 100 Hz               → 샘플 간격 10 ms
 always-on 코어 100 MHz   → 10 ms = 1,000,000 사이클. 모델에 20%를 준다면 200k 사이클
```

말로 하면: 프레임당 수만 MAC짜리 모델이면 MCU 한 개로 충분하고, 수백만 MAC이면 DSP/NPU가 필요하다. 그리고 **상태 메모리**가 always-on SRAM 예산을 정한다.

### 7.2 같은 과제, 세 가지 모델

40차원 특징(예: log-mel)을 10 ms마다 받는 스트리밍 과제를 세 가지로 구현한다. MAC 규모를 비슷하게 맞췄다.

- **TCN**: causal 1D conv 4층(k = 3, dilation 1, 2, 4, 8, 64채널). 층마다 과거 `(k−1)·dilation` 프레임만 링버퍼에 보관한다(B2 8.4). receptive field = 1 + 2·(1+2+4+8) = 31 프레임 = 310 ms.
- **GRU**: 2층, H = 64. 상태 h 두 개만 보관.
- **Transformer**: 2층, d = 64, 4 head, FFN 128, KV-cache를 최근 W = 100 프레임(1초)으로 제한.

프레임당 MAC은 TCN `40·64·3 + 3·64·64·3 = 44,544`, GRU `3·64·104 + 3·64·128 = 44,544`로 우연히 같고, transformer는 선형층 약 68k + attention `2·2·100·64` = 25.6k로 93,696이다.

### 7.3 코드로 확인 — torch로 프레임 단위 실행

torch 1 thread, 400프레임을 돌리고 처음 100프레임(워밍업, KV 창이 차는 구간)은 버린다. TCN의 링버퍼 스트리밍 결과가 causal conv를 전체 시퀀스에 한 번 건 결과와 같은지는 따로 확인했다(최대 오차 6e−8).

**[ex9]**

```python
# 같은 스트리밍 과제(40-dim 특징, 10 ms 프레임): TCN+ring buffer vs GRU vs 작은 Transformer(KV 창)
import torch, torch.nn as nn, torch.nn.functional as F, time, statistics as st
torch.manual_seed(0); torch.set_grad_enabled(False); torch.set_num_threads(1)
I, C, K, DIL = 40, 64, 3, [1, 2, 4, 8]
class TCN:                                   # 층마다 과거 (K-1)·dil 프레임만 ring buffer로 보관
    def __init__(s):
        s.convs = [nn.Conv1d(I if i == 0 else C, C, K, dilation=d) for i, d in enumerate(DIL)]
        s.bufs = [torch.zeros(1, cv.in_channels, (K - 1) * d + 1) for cv, d in zip(s.convs, DIL)]
    def step(s, x):                          # x: [1, I]
        h = x[:, :, None]
        for i, cv in enumerate(s.convs):
            s.bufs[i] = torch.cat([s.bufs[i][:, :, 1:], h], 2)   # 링버퍼 push (가장 오래된 것 drop)
            taps = s.bufs[i][:, :, ::DIL[i]].reshape(1, -1)          # dilation 간격의 K개 탭
            h = F.relu(F.linear(taps, cv.weight.reshape(C, -1), cv.bias))[:, :, None]  # = conv 1 프레임
        return h[:, :, 0]
    def state(s): return sum(b.shape[1] * (b.shape[2] - 1) for b in s.bufs)
    macs = I * C * K + 3 * C * C * K
class GRU2:
    def __init__(s): s.c = [nn.GRUCell(I, C), nn.GRUCell(C, C)]; s.h = [torch.zeros(1, C)] * 2
    def step(s, x):
        h0 = s.c[0](x, s.h[0]); h1 = s.c[1](h0, s.h[1]); s.h = [h0, h1]
        return h1
    def state(s): return 2 * C
    macs = 3 * C * (I + C) + 3 * C * (C + C)
class TinyTF:                                # 2층, d=64, 4 head, FFN 128, KV 창 W 프레임
    def __init__(s, W=100):
        s.inp, s.W = nn.Linear(I, C), W
        s.qkv = [nn.Linear(C, 3 * C) for _ in range(2)]; s.o = [nn.Linear(C, C) for _ in range(2)]
        s.ff = [nn.Sequential(nn.Linear(C, 2 * C), nn.GELU(), nn.Linear(2 * C, C)) for _ in range(2)]
        s.ln = [nn.LayerNorm(C) for _ in range(4)]; s.kv = [None, None]
    def step(s, x):
        h = s.inp(x)
        for l in range(2):
            q, k, v = s.qkv[l](s.ln[2 * l](h)).view(1, 3, 4, 16).unbind(1)      # [1, heads, 16]
            kv = torch.stack([k, v])[:, :, :, None]                             # [2,1,4,1,16]
            s.kv[l] = kv if s.kv[l] is None else torch.cat([s.kv[l], kv], 3)[:, :, :, -s.W:]
            a = F.scaled_dot_product_attention(q[:, :, None], s.kv[l][0], s.kv[l][1])
            h = h + s.o[l](a.reshape(1, C)); h = h + s.ff[l](s.ln[2 * l + 1](h))
        return h
    def state(s): return 2 * 2 * s.W * C
    macs = I * C + 2 * (C * 3 * C + C * C + 2 * C * 2 * C + 2 * 100 * C)   # 마지막 항: QKᵀ·AV over W=100
def run(model, frames=400):
    xs, ts = torch.randn(frames, 1, I), []
    for x in xs:
        t0 = time.perf_counter(); model.step(x); ts.append(time.perf_counter() - t0)
    ts = sorted(ts[100:]); return st.median(ts), ts[len(ts) // 10], ts[int(len(ts) * 0.99)]
for name, m in [("TCN k3 d1-8 (ring buf)", TCN()), ("GRU 2x64", GRU2()), ("Transformer 2L W=100", TinyTF())]:
    med, p10, p99 = run(m)
    print(f"{name:22s} MAC/frame {m.macs:6d}  median {med*1e6:5.1f} us  p10 {p10*1e6:5.1f}  "
          f"p99 {p99*1e6:5.1f}  state {m.state():5d} values")
```

```text
TCN k3 d1-8 (ring buf) MAC/frame  44544  median  77.3 us  p10  76.7  p99  96.4  state  1872 values
GRU 2x64               MAC/frame  44544  median  27.1 us  p10  26.9  p99  33.3  state   128 values
Transformer 2L W=100   MAC/frame  93696  median 134.5 us  p10 133.0  p99 175.2  state 25600 values
```

출력에서 볼 것: MAC이 같은 TCN과 GRU가 77 µs vs 27 µs로 3배 가까이 차이 난다. TCN은 층마다 `torch.cat`, 슬라이싱, `linear`, `relu`로 작은 op가 4층 × 4~5개인 반면, `GRUCell`은 층마다 fused 커널 한 번이다. 이 크기에서 torch 시간은 **MAC이 아니라 op 개수**가 정한다. NPU에서도 똑같다. 작은 op가 많으면 op당 고정 비용(스케줄링, DMA 설정)이 지배한다.

### 7.4 C로 확인 — 프레임워크를 걷어 내면

MCU 펌웨어에 가까운 모습으로 TCN과 GRU를 C로 짰다. 링버퍼는 `[시간][채널]` 배치라 탭 하나가 연속 메모리다.

**[stream.c]**

```c
/* 스트리밍 1 프레임 비용을 C로: TCN(ring buffer, 4층) vs GRU(2층) — 둘 다 44,544 MAC/frame */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define I 40
#define C 64
#define K 3
static const int DIL[4] = {1, 2, 4, 8};
static float wt[4][C][K * C], bt[4][C];          /* TCN weight: [out][k·cin + c] (층0은 cin=I) */
static float ring[4][(K - 1) * 8 + 1][C];         /* 층별 링버퍼 [시간][채널] (NHWC처럼 채널 연속) */
static int head[4];
static float wi[2][3 * C][C], wh[2][3 * C][C], hs[2][C];   /* GRU: 층0 입력은 앞 I열만 사용 */
static float frand(void) { return (float)((double)rand() / RAND_MAX - 0.5) * 0.2f; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void tcn_step(const float *x, float *y) {
    float in[C], taps[C * K];
    memcpy(in, x, I * sizeof(float));
    for (int l = 0; l < 4; l++) {
        int cin = l ? C : I, len = (K - 1) * DIL[l] + 1;
        head[l] = (head[l] + 1) % len;                           /* 가장 오래된 칸에 덮어쓰기 */
        memcpy(ring[l][head[l]], in, cin * sizeof(float));
        for (int k = 0; k < K; k++)                              /* k=K-1 이 현재 프레임 */
            memcpy(taps + k * cin, ring[l][(head[l] - (K - 1 - k) * DIL[l] + len) % len], cin * sizeof(float));
        for (int o = 0; o < C; o++) {
            float acc = bt[l][o];
            for (int j = 0; j < cin * K; j++) acc += wt[l][o][j] * taps[j];
            y[o] = acc > 0 ? acc : 0;
        }
        memcpy(in, y, C * sizeof(float));
    }
}
static float sig(float v) { return 1.f / (1.f + expf(-v)); }
static void gru_step(const float *x) {
    const float *in = x;
    for (int l = 0; l < 2; l++) {
        int nin = l ? C : I; float gi[3 * C], gh[3 * C];
        for (int r = 0; r < 3 * C; r++) {
            float a = 0, b = 0;
            for (int j = 0; j < nin; j++) a += wi[l][r][j] * in[j];
            for (int j = 0; j < C; j++) b += wh[l][r][j] * hs[l][j];
            gi[r] = a; gh[r] = b;
        }
        for (int j = 0; j < C; j++) {                            /* torch 순서: r, z, n */
            float r = sig(gi[j] + gh[j]), z = sig(gi[C + j] + gh[C + j]);
            float n = tanhf(gi[2 * C + j] + r * gh[2 * C + j]);
            hs[l][j] = (1 - z) * n + z * hs[l][j];
        }
        in = hs[l];
    }
}
int main(void) {
    float *p = &wt[0][0][0]; for (size_t k = 0; k < sizeof wt / sizeof(float); k++) p[k] = frand();
    p = &wi[0][0][0]; for (size_t k = 0; k < sizeof wi / sizeof(float); k++) p[k] = frand();
    p = &wh[0][0][0]; for (size_t k = 0; k < sizeof wh / sizeof(float); k++) p[k] = frand();
    enum { N = 20000 }; float x[I], y[C]; double t0, s = 0;
    t0 = now();
    for (int n = 0; n < N; n++) { for (int i = 0; i < I; i++) x[i] = frand(); tcn_step(x, y); s += y[0]; }
    printf("TCN  4 layers: %6.2f us/frame\n", (now() - t0) / N * 1e6);
    t0 = now();
    for (int n = 0; n < N; n++) { for (int i = 0; i < I; i++) x[i] = frand(); gru_step(x); s += hs[1][0]; }
    printf("GRU  2 layers: %6.2f us/frame\n", (now() - t0) / N * 1e6);
    int need = 0; for (int l = 0; l < 4; l++) need += (l ? C : I) * (K - 1) * DIL[l];
    printf("state (fp32): TCN %d B (past taps), GRU %zu B   checksum %.3f\n", need * 4, sizeof hs, s);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 -lm stream.c -o stream && ./stream && ./stream && ./stream
```

```text
TCN  4 layers:  22.75 us/frame
GRU  2 layers:  20.78 us/frame
state (fp32): TCN 7488 B (past taps), GRU 512 B   checksum 1.026
TCN  4 layers:  23.65 us/frame
GRU  2 layers:  13.59 us/frame
state (fp32): TCN 7488 B (past taps), GRU 512 B   checksum 1.026
TCN  4 layers:  21.59 us/frame
GRU  2 layers:  14.17 us/frame
state (fp32): TCN 7488 B (past taps), GRU 512 B   checksum 1.026
```

출력에서 볼 것: 프레임워크를 걷어 내면 둘 다 약 14~23 µs, 44.5k MAC을 2~3 GMAC/s로 처리한다(M2 1코어, `-O2`, 스칼라에 가까운 코드). 세 번 실행의 중앙값은 TCN 22.75 µs, GRU 14.17 µs다(첫 실행의 GRU 20.78은 잡음). TCN이 여전히 느린 이유는 **내적 한 줄이 192개 곱을 하나의 누산기에 이어 붙이는 의존 체인**이기 때문이다. `-ffast-math` 없이는 float 덧셈 순서를 바꿀 수 없어서 컴파일러가 벡터화하지 못한다. GRU 루프는 `a`, `b` 두 누산기를 번갈아 써서 명령어 수준 병렬성이 두 배다. 같은 MAC인데 **코드 구조가 latency를 1.6배 바꾼다.** MCU용 int8 커널(CMSIS-NN)이 누산기 여러 개와 SIMD를 쓰는 이유다.

상태 메모리는 TCN 7,488 B(fp32) = 1,872값, GRU 512 B = 128값이다. int8로 바꾸면 각각 1.8 KB, 128 B다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<text x="230.0" y="22.0" font-size="12" text-anchor="middle">프레임당 latency (µs, 10 ms 예산 대비)</text><text x="122.0" y="72.0" font-size="12" text-anchor="end">TCN 4층</text><rect x="130" y="54" width="103.1" height="16" fill="#4a7bd0"/><text x="237.1" y="67.0" font-size="12" text-anchor="start">77.3 torch</text><rect x="130" y="74" width="30.3" height="16" fill="#3f9a6b"/><text x="164.3" y="87.0" font-size="12" text-anchor="start">22.75 C (3회 중앙값)</text><text x="122.0" y="122.0" font-size="12" text-anchor="end">GRU 2×64</text><rect x="130" y="104" width="36.1" height="16" fill="#4a7bd0"/><text x="170.1" y="117.0" font-size="12" text-anchor="start">27.1 torch</text>
<rect x="130" y="124" width="18.9" height="16" fill="#3f9a6b"/><text x="152.9" y="137.0" font-size="12" text-anchor="start">14.17 C (3회 중앙값)</text><text x="122.0" y="172.0" font-size="12" text-anchor="end">Transformer 2층</text><rect x="130" y="154" width="179.3" height="16" fill="#4a7bd0"/><text x="313.3" y="167.0" font-size="12" text-anchor="start">134.5 torch</text><line x1="130" y1="50" x2="130" y2="200" stroke="currentColor"/><text x="130.0" y="216.0" font-size="12" text-anchor="middle">0</text><text x="196.7" y="216.0" font-size="12" text-anchor="middle">50</text><text x="263.3" y="216.0" font-size="12" text-anchor="middle">100</text>
<text x="330.0" y="216.0" font-size="12" text-anchor="middle">150</text><text x="530.0" y="22.0" font-size="12" text-anchor="middle">스트리밍 상태 (int8 바이트, log)</text><text x="472.0" y="72.0" font-size="12" text-anchor="end">TCN 4층</text><rect x="480" y="60" width="82.2" height="18" fill="#e08a3c"/><text x="566.2" y="74.0" font-size="12" text-anchor="start">1,872 B</text><text x="472.0" y="122.0" font-size="12" text-anchor="end">GRU 2×64</text><rect x="480" y="110" width="6.9" height="18" fill="#e08a3c"/><text x="490.9" y="124.0" font-size="12" text-anchor="start">128 B</text><text x="472.0" y="172.0" font-size="12" text-anchor="end">Transformer 2층</text>
<rect x="480" y="160" width="155.6" height="18" fill="#e08a3c"/><text x="600.0" y="194.0" font-size="12" text-anchor="start">25,600 B</text><line x1="480" y1="50" x2="480" y2="200" stroke="currentColor"/><text x="480.0" y="216.0" font-size="12" text-anchor="middle">100</text><text x="544.6" y="216.0" font-size="12" text-anchor="middle">1,000</text><text x="609.2" y="216.0" font-size="12" text-anchor="middle">10,000</text><text x="340.0" y="250.0" font-size="12" text-anchor="middle">MAC/frame: TCN 44.5k · GRU 44.5k · Transformer 93.7k (W=100 KV 창)</text>
</svg>
```

그림 7 — 왼쪽: 프레임당 latency. torch(파랑)에서는 op 개수 때문에 TCN이 GRU보다 3배 느리고, C(초록)로 짜면 둘이 가까워진다. transformer는 C로 짜지 않았다. 오른쪽: 스트리밍 상태(int8, log 축). GRU 128 B, TCN 1.8 KB, 1초 KV 창을 가진 transformer는 25 KB다.

### 7.5 MCU에서의 결론

| 항목 | TCN (링버퍼) | GRU | 작은 Transformer (W = 100) |
|---|---|---|---|
| 프레임당 MAC | 44.5k | 44.5k | 93.7k (W에 비례해 증가) |
| 상태 (int8) | 1.8 KB | 128 B | 25 KB (W·층·d에 비례) |
| weight (int8, 대략) | 45 KB | 45 KB | 68 KB |
| 볼 수 있는 과거 | 310 ms (receptive field, 고정) | 이론상 무한, 실제로는 수백 ms~수 초 | 정확히 W 프레임 |
| 필요한 op | conv/GEMV, ReLU | GEMV, sigmoid, tanh | GEMV, softmax, LayerNorm, GELU |
| 가속기 이득 | 학습·배치 추론 때 시간축 병렬 | 거의 없음 | 학습·prefill 때 큼 |
| 양자화 | 가장 쉬움 | 상태 누적 오차 주의 (B3 13절) | softmax·LN 민감 |

말로 하면: 가장 작은 always-on 코어라면 **GRU가 상태·op 면에서 가장 싸고**, TCN은 op가 단순해 양자화·검증이 쉬우며 NPU로 옮기기도 좋다. 작은 transformer는 같은 MAC 규모에서 상태가 10~200배 크고 op가 다양해서, MCU보다는 DSP/NPU가 있을 때 고려한다. 실제 선택은 정확도와 함께 타깃에서 재서 한다.

---

## 8. 비전 백본 — 해상도와 계열

### 8.1 해상도가 비용에 들어가는 방식

- CNN: MAC ∝ 픽셀 수 = 해상도². 96 → 320이면 (320/96)² ≈ 11배.
- ViT: 토큰 수 N = (해상도/patch)². 선형층은 N에 비례, attention은 N²에 비례. 즉 해상도의 4제곱으로 자라는 항이 있다.
- activation 메모리도 해상도²에 비례한다. MCU에서는 latency보다 **peak SRAM**이 먼저 막힌다(C7 7절).

### 8.2 코드로 확인 — 해상도별 latency

모두 `weights=None`(무작위 초기화, 속도만 볼 것이므로 상관없다), fp32, batch 1, torch eager 4 threads. ViT는 DeiT-Tiny 폭(d = 192, 3 head, MLP 768, patch 16)에 층 수만 3과 12로 바꿨다. MAC은 `torch.utils.flop_counter.FlopCounterMode`로 센 FLOP의 절반이다.

**[ex10]**

```python
# 비전 백본: 해상도별 MAC과 latency — MobileNetV2 / ResNet18 / 작은 ViT (전부 weights=None, fp32, batch 1)
import torch, time, statistics as st, torchvision.models as tvm
from torch.utils.flop_counter import FlopCounterMode
torch.manual_seed(0); torch.set_grad_enabled(False)
def vit(res, depth):   # DeiT-Tiny 폭(d=192, 3 head, MLP 768), patch 16
    return tvm.VisionTransformer(image_size=res, patch_size=16, num_layers=depth, num_heads=3,
                                 hidden_dim=192, mlp_dim=768)
def bench(m, x, reps=20):
    for _ in range(3): m(x)
    ts = []
    for _ in range(reps):
        t0 = time.perf_counter(); m(x); ts.append(time.perf_counter() - t0)
    return st.median(ts)
print(f"threads={torch.get_num_threads()}")
for res in [96, 160, 224, 320]:
    x = torch.randn(1, 3, res, res)
    for name, m in [("mobilenet_v2", tvm.mobilenet_v2(weights=None)), ("resnet18", tvm.resnet18(weights=None)),
                    ("vit d=192 L=3", vit(res, 3)), ("vit d=192 L=12", vit(res, 12))]:
        m.eval()
        with FlopCounterMode(display=False) as fc: m(x)
        macs = fc.get_total_flops() / 2
        t = bench(m, x)
        print(f"res {res:3d} {name:15s} {macs/1e6:7.0f} MMAC {t*1e3:7.2f} ms {macs/t/1e9:6.1f} GMAC/s")
```

```text
threads=4
res  96 mobilenet_v2         56 MMAC   26.04 ms    2.2 GMAC/s
res  96 resnet18            334 MMAC   10.80 ms   30.9 GMAC/s
res  96 vit d=192 L=3        55 MMAC    1.59 ms   34.4 GMAC/s
res  96 vit d=192 L=12      202 MMAC    5.65 ms   35.8 GMAC/s
res 160 mobilenet_v2        154 MMAC   34.56 ms    4.5 GMAC/s
res 160 resnet18            926 MMAC    9.22 ms  100.5 GMAC/s
res 160 vit d=192 L=3       149 MMAC    2.02 ms   73.8 GMAC/s
res 160 vit d=192 L=12      551 MMAC    7.17 ms   76.8 GMAC/s
res 224 mobilenet_v2        301 MMAC   32.38 ms    9.3 GMAC/s
res 224 resnet18           1814 MMAC   18.89 ms   96.1 GMAC/s
res 224 vit d=192 L=3       291 MMAC    3.78 ms   76.8 GMAC/s
res 224 vit d=192 L=12     1075 MMAC   15.41 ms   69.7 GMAC/s
res 320 mobilenet_v2        612 MMAC   45.83 ms   13.4 GMAC/s
res 320 resnet18           3702 MMAC   38.30 ms   96.6 GMAC/s
res 320 vit d=192 L=3       591 MMAC    8.01 ms   73.8 GMAC/s
res 320 vit d=192 L=12     2188 MMAC   32.53 ms   67.3 GMAC/s
```

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="240" x2="540" y2="240" stroke="currentColor"/><line x1="60" y1="30" x2="60" y2="240" stroke="currentColor"/><text x="60.0" y="256.0" font-size="12" text-anchor="middle">96</text><text x="197.1" y="256.0" font-size="12" text-anchor="middle">160</text><text x="334.3" y="256.0" font-size="12" text-anchor="middle">224</text><text x="540.0" y="256.0" font-size="12" text-anchor="middle">320</text><text x="54.0" y="244.0" font-size="12" text-anchor="end">0</text><text x="54.0" y="202.0" font-size="12" text-anchor="end">10</text><text x="54.0" y="160.0" font-size="12" text-anchor="end">20</text><text x="54.0" y="118.0" font-size="12" text-anchor="end">30</text>
<text x="54.0" y="76.0" font-size="12" text-anchor="end">40</text><text x="54.0" y="34.0" font-size="12" text-anchor="end">50</text><polyline points="60.0,130.6 197.1,94.8 334.3,104.0 540.0,47.5" fill="none" stroke="#d0564a" stroke-width="2"/><circle cx="60.0" cy="130.6" r="4" fill="#d0564a"/><circle cx="197.1" cy="94.8" r="4" fill="#d0564a"/><circle cx="334.3" cy="104.0" r="4" fill="#d0564a"/><circle cx="540.0" cy="47.5" r="4" fill="#d0564a"/><text x="548.0" y="51.5" font-size="12" text-anchor="start">mobilenet_v2</text><polyline points="60.0,194.6 197.1,201.3 334.3,160.7 540.0,79.1" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="60.0" cy="194.6" r="4" fill="#4a7bd0"/>
<circle cx="197.1" cy="201.3" r="4" fill="#4a7bd0"/><circle cx="334.3" cy="160.7" r="4" fill="#4a7bd0"/><circle cx="540.0" cy="79.1" r="4" fill="#4a7bd0"/><text x="548.0" y="83.1" font-size="12" text-anchor="start">resnet18</text><polyline points="60.0,233.3 197.1,231.5 334.3,224.1 540.0,206.4" fill="none" stroke="#3f9a6b" stroke-width="2"/><circle cx="60.0" cy="233.3" r="4" fill="#3f9a6b"/><circle cx="197.1" cy="231.5" r="4" fill="#3f9a6b"/><circle cx="334.3" cy="224.1" r="4" fill="#3f9a6b"/><circle cx="540.0" cy="206.4" r="4" fill="#3f9a6b"/><text x="548.0" y="210.4" font-size="12" text-anchor="start">vit L=3</text>
<polyline points="60.0,216.3 197.1,209.9 334.3,175.3 540.0,103.4" fill="none" stroke="#e08a3c" stroke-width="2"/><circle cx="60.0" cy="216.3" r="4" fill="#e08a3c"/><circle cx="197.1" cy="209.9" r="4" fill="#e08a3c"/><circle cx="334.3" cy="175.3" r="4" fill="#e08a3c"/><circle cx="540.0" cy="103.4" r="4" fill="#e08a3c"/><text x="548.0" y="107.4" font-size="12" text-anchor="start">vit L=12</text><text x="300.0" y="18.0" font-size="12" text-anchor="middle">torch eager fp32, M2 CPU 4 threads: latency (ms) vs 입력 해상도</text><text x="300.0" y="274.0" font-size="12" text-anchor="middle">해상도 (정사각형 한 변, px)</text>
<text x="340.0" y="302.0" font-size="12" text-anchor="middle">MobileNetV2 곡선은 depthwise가 느린 경로로 떨어진 결과 (7절·ex11 참고)</text>
</svg>
```

그림 8 — 해상도별 latency. ResNet18과 ViT는 해상도에 따라 대략 MAC만큼 늘어난다. MobileNetV2는 모든 해상도에서 가장 느린데, MAC 문제가 아니라 이 런타임에서 depthwise가 느린 경로로 떨어졌기 때문이다(3.6).

출력에서 볼 것:

- **같은 MAC, 다른 latency**: 224에서 MobileNetV2(301 MMAC) 32.4 ms vs ViT L = 3(291 MMAC) 3.8 ms. CPU에서 ViT의 연산은 거의 전부 `[197 × 192] @ [192 × 576]` 같은 GEMM이라 70 GMAC/s 이상 나온다. ONNX Runtime 기준으로도 MobileNetV2는 8.5 ms였다(3.6). 즉 CPU에서는 "GEMM으로만 이루어진" ViT가 유리하다.
- **해상도 스케일링**: ViT L = 12의 MAC은 224 → 320에서 1075 → 2188 MMAC(2.04배)이고, 토큰 수 비율 400/196 = 2.04와 같다. 이 폭(d = 192)과 이 토큰 수에서는 attention(N²) 항이 아직 작다는 뜻이다. 해상도를 더 올리거나 patch를 줄이면 N² 항이 커진다.
- ResNet18이 96에서 160보다 느리게 나온 것(10.8 vs 9.2 ms)은 측정 잡음과 작은 입력에서의 비효율이 섞인 것이다. 다른 실행에서는 9.9 vs 14.5 ms였다.

### 8.3 그렇다면 edge에서는 ViT가 낫나? — 그렇지 않은 경우가 많다

CPU 측정은 "GEMM만 잘하는 하드웨어"의 관점이다. edge NPU에서는 반대가 될 수 있다.

- NPU는 conv/depthwise용 전용 경로를 갖고 있는 경우가 많고, MobileNet류는 벤더가 가장 많이 튜닝한 모델이다.
- ViT의 softmax·LayerNorm·GELU는 NPU의 느린 벡터 유닛이나 CPU로 간다. 층마다 attention 행렬 `3 head × 197 × 197`을 만들고 정규화해야 한다.
- int8 양자화에서 ViT는 LayerNorm 앞뒤의 이상값 때문에 정확도가 더 떨어지기 쉽다.
- ViT는 CNN의 locality 가정이 없어서 같은 정확도를 내려면 데이터·학습 기법이 더 필요하다. 이것은 성능이 아니라 정확도 문제지만, 같은 MAC에서의 비교를 흔든다.

그래서 edge 비전에서는 CNN 앞단 + attention 약간(MobileViT, EfficientFormer류 hybrid)이 흔하다. 결론은 늘 같다. **타깃 하드웨어에서 재 본다.**

---

## 9. 계열별 경험칙과 "벤더에게 물을 것"

### 9.1 경험칙

| 계열 | 병목 | 경험칙 | 빨리 만드는 레버 |
|---|---|---|---|
| 표준 conv | compute | MAC ÷ (peak × 효율 60~90%)로 거의 맞는다. 채널이 적은 초기 층은 효율이 낮다 | 채널을 HW 폭의 배수로, int8, 층 fusion |
| depthwise | 메모리·배열 활용률 | MAC 비중 10%여도 시간 비중은 50% 이상일 수 있다 | dw+pw fusion, 전용 엔진 확인, 필요하면 표준 conv로 교체 |
| 1×1 conv / 배치 FC | compute (채널이 충분하면) | 가장 효율 좋은 op. K, N이 HW 폭보다 작으면 활용률 급락 | 채널 정렬(C7 2.5), batch |
| FC 배치 1 (GEMV) | weight 대역폭 | 시간 ≈ weight 바이트 ÷ 대역폭 | weight 양자화(int8/int4), SRAM 상주, batch |
| RNN/GRU/LSTM | 순차성 + weight 대역폭 | 총 시간 = T × step 시간. step은 작은 GEMV | 입력 투영 분리, GRU 선택, 작은 H, MCU/DSP 배치 |
| Transformer prefill | compute + T² | 짧은 T는 GEMM, 긴 T는 attention이 지배 (T ≈ w_layer/2d에서 역전) | FlashAttention류, 짧은 prompt, chunked prefill |
| Transformer decode | weight + KV 대역폭 | tokens/s ≈ 대역폭 ÷ (weight 바이트 + KV 바이트) (D5) | weight int4, KV int8, GQA, 창 제한, speculative decoding |
| 스트리밍 소형 모델 | op당 고정 비용 | 프레임당 수만 MAC이면 MAC보다 op 개수·호출 비용이 지배 | op fusion, 층 수 줄이기, C 커널 직접 |

### 9.2 실리콘 벤더에게 물을 것

NPU/DSP 데이터시트의 "X TOPS"는 대개 표준 conv 기준이다. 우리 모델 계열에서 실제로 얼마나 나오는지는 아래를 물어야 안다.

| 계열 | 질문 | 왜 |
|---|---|---|
| depthwise | depthwise 3×3/5×5의 실효 활용률은? dw+pw fusion을 지원하나? stride 2 depthwise는? | 전용 경로가 없으면 MobileNet이 CPU보다 느릴 수 있다 |
| 1×1 / 표준 conv | MAC 배열 크기와 채널 정렬 단위는? (예: 16, 32의 배수) 첫 층(C_in = 1, 3) 전용 경로는? | 정렬 안 된 채널은 활용률이 뚝 떨어진다 |
| FC·GEMV | weight를 SRAM에 상주시킬 수 있는 용량은? DRAM 실효 대역폭은? weight 압축/int4 디코드를 하드웨어가 하나? | GEMV 속도는 대역폭이 정한다 |
| RNN | LSTM/GRU를 네이티브로 지원하나, unroll하나, CPU로 보내나? 상태를 on-chip에 유지하나? step당 launch 오버헤드는? | step마다 fallback·launch가 붙으면 수 배 느려진다 |
| Transformer | softmax, LayerNorm/RMSNorm, GELU/SiLU, RoPE를 NPU에서 하나? 정밀도는(int8, int16, fp16)? 동적 shape나 여러 길이의 그래프를 지원하나? | 미지원 op는 CPU 왕복으로 대역폭과 latency를 먹는다 |
| KV-cache | KV를 SRAM에 둘 수 있나, DRAM인가? KV int8/fp8 지원? in-place 갱신(링버퍼)이 되나, 매번 복사하나? | decode tokens/s와 context 한계가 여기서 정해진다 |
| 공통 | op당 고정 오버헤드(µs)는? 작은 모델에서 CPU 대비 이득이 나는 최소 크기는? 프로파일러가 레이어별 사이클을 보여 주나? | always-on 소형 모델은 오버헤드가 전부일 수 있다 |

말로 하면: Don이 RF 칩 통합에서 "스펙 시트의 throughput 말고 우리 트래픽 패턴에서의 실측"을 요구했던 것과 똑같다.

---

## 10. 임베디드 관점에서 다시 보기

### 10.1 코어별 배치 가이드 (웨어러블 예시, 추정)

```
 ┌─────────────── always-on MCU (수백 KB SRAM, ~100 MHz) ───────────────┐
 │  wake word 1단 · VAD · 착용 감지 · IMU 제스처                          │
 │  → GRU / 작은 DS-CNN / TCN, 프레임당 수만 MAC, 상태 수백 B~수 KB        │
 └───────────────────────────────┬───────────────────────────────────────┘
                                 │ 이벤트 발생 시 깨움
 ┌───────────────── DSP / NPU (MB급 SRAM, DRAM 공유) ────────────────────┐
 │  ASR 인코더 · 비전 백본 · 화자 인식                                   │
 │  → 표준 conv / pointwise / transformer prefill (GEMM형이 유리)        │
 └───────────────────────────────┬───────────────────────────────────────┘
                                 │
 ┌───────────────── SoC CPU + DRAM (GB급, 수~수십 GB/s) ─────────────────┐
 │  소형 LLM decode → weight·KV 대역폭이 전부. int4 weight, KV 창 제한     │
 └───────────────────────────────────────────────────────────────────────┘
```

말로 하면: GEMV형(RNN, 작은 FC)은 weight가 SRAM에 들어가는 한 MCU가 적합하고, GEMM형(conv, prefill)은 NPU로, 큰 GEMV형(LLM decode)은 대역폭이 가장 큰 곳으로 보낸다.

### 10.2 새 모델을 받았을 때의 절차

1. 레이어별로 MAC, weight 바이트, activation 바이트, state 바이트를 표로 뽑는다(D1, D2, ex1).
2. 각 레이어의 intensity를 계산해 타깃의 ridge point와 비교한다(D3, ex12).
3. 계열별 실효 GMAC/s(ex2, ex3, ex5 같은 microbenchmark 또는 C7의 LUT)로 레이어 시간을 예측한다.
4. 타깃 컴파일러의 변환 로그에서 fallback op, 동적 shape, 삽입된 transpose/cast를 확인한다.
5. 타깃에서 레이어별 프로파일을 떠서 예측과 비교하고, 가장 큰 차이부터 원인을 찾는다. 대개 depthwise, softmax/LayerNorm, 작은 op의 오버헤드, GEMV 대역폭 중 하나다.

### 10.3 Don의 경험과 연결

- SSD 펌웨어의 "4K random read는 IOPS가, 128K sequential은 대역폭이 한계"라는 구분이 그대로 "conv는 compute가, GEMV는 대역폭이 한계"다.
- "NAND 채널 수만큼 병렬로 못 쓰면 throughput이 안 나온다"는 경험이 "채널 수가 MAC 배열 폭을 못 채우면 활용률이 안 나온다"와 같다.
- "커맨드당 고정 오버헤드가 작은 I/O에서 지배한다"는 경험이 "작은 op가 많은 스트리밍 모델에서 op당 오버헤드가 지배한다"와 같다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| MAC 수로 latency를 예측 | MobileNet이 예측보다 3~5배 느림 | depthwise가 memory-bound, 활용률 낮음 | 계열별 실효 GMAC/s 또는 레이어별 LUT로 예측 |
| NPU TOPS로 LLM tokens/s 추정 | 실제가 수십 배 느림 | decode는 GEMV, 대역폭이 상한 | tokens/s ≈ 대역폭 ÷ (weight + KV 바이트) |
| batch 1 벤치 없이 batch 32 숫자 인용 | 기기에서 처리량이 한참 모자람 | batch가 GEMV를 GEMM으로 바꿔 줬던 것 | edge 조건(batch 1, 스트리밍)으로 측정 |
| RNN을 NPU에 그대로 변환 | 변환은 됐는데 CPU보다 느림 | 순환부 CPU fallback, step마다 launch | 변환 로그 확인, MCU/DSP에 배치하거나 TCN으로 교체 |
| KV-cache를 매 step 이어 붙이기 | context가 길어질수록 decode가 과하게 느려짐 | 캐시 전체 복사, 메모리 재할당 | 최대 길이로 미리 할당, 인덱스로 in-place 기록 |
| context 길이 정책 없음 | 긴 대화에서 OOM 또는 tokens/s 급락 | KV-cache가 T에 선형 증가 | sliding window, KV 양자화, 최대 길이 제한 |
| 채널 수를 HW 폭과 무관하게 설계 | 65채널 층이 128채널 층만큼 걸림 | 타일 경계에서 활용률 급락 | 채널을 8/16/32의 배수로 (C7 2.5) |
| 작은 스트리밍 모델에 op를 잘게 쪼갬 | MAC은 적은데 프레임당 시간이 큼 | op당 고정 비용 지배 | fusion, 층 수 줄이기, 커널 직접 작성 |
| 잡음 많은 환경에서 1회 측정 | 실행마다 결론이 바뀜 | 다른 프로세스, 캐시 상태, 주파수 변화 | 반복 후 median·p10, 두 번 이상 재현 확인 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "Why is MobileNet not 8× faster than ResNet despite 8× fewer MACs?"

**A.** MAC은 줄었지만 줄어든 것은 연산뿐이고 activation 트래픽은 거의 그대로다. depthwise는 입력 값 하나를 9번만 쓰므로 intensity가 약 9 FLOP/B로, 표준 conv의 수백과 비교하면 memory-bound다. NPU에서는 depthwise가 채널마다 K = 9, N = 1짜리 GEMM이라 MAC 배열의 1%도 못 쓸 수 있다. 제 M2 측정에서 C = 64 depthwise separable은 MAC이 7.9배 적었지만 1.9배만 빨랐고, depthwise가 MAC의 12%인데 시간의 81%를 차지했다. 모델 전체로도 MobileNetV2는 ResNet18보다 MAC이 6배 적지만 ONNX Runtime에서 1.4~2배만 빨랐다.

> MACs went down, but memory traffic didn't. A depthwise layer uses each input value only nine times, so its arithmetic intensity is around 9 FLOPs per byte versus hundreds for a standard conv — it's memory-bound. On an NPU it's worse: each channel is a tiny GEMM with K equals 9 and N equals 1, so a 32-by-32 systolic array can sit below one percent utilization. When I measured a depthwise-separable block on my laptop, it had 8× fewer MACs but was only about 2× faster — the depthwise part was 12% of the MACs and 80% of the time. So I predict latency with per-layer-type efficiency, not MAC counts, and I ask the vendor specifically about depthwise support and dw-pw fusion.

**Q.** "Which is better for an always-on streaming model on an MCU: RNN, CNN, or transformer?"

**A.** 가장 작은 코어라면 보통 GRU나 작은 causal 1D CNN이다. GRU는 상태가 H개(수백 바이트)이고 프레임마다 일정한 작업을 한다. TCN은 receptive field만큼 층별 링버퍼가 필요하지만(수 KB) op가 단순해 양자화·검증이 쉽고 NPU로 옮기기 좋다. transformer는 KV 창만큼 상태가 커지고(1초 창이면 수십 KB) softmax·LayerNorm이 필요해서 MCU에는 잘 안 맞는다. 제 측정에서 같은 44.5k MAC의 TCN과 GRU는 C로 프레임당 14~23 µs였고, 상태는 1.8 KB 대 128 B였다. 최종 선택은 정확도와 타깃 프로파일로 한다.

> For the smallest always-on core I'd usually pick a GRU or a small causal 1-D CNN. A GRU carries just its hidden state — a few hundred bytes — and does constant work per frame. A causal TCN needs a ring buffer per layer sized by its receptive field, a few kilobytes, but its ops are simple, it quantizes cleanly and it ports well to an NPU later. A transformer needs a KV window — tens of kilobytes for one second of context — plus softmax and LayerNorm, which is a poor fit for an MCU. Then I'd confirm on the target: accuracy on long streams and cycles per frame against the frame deadline.

**Q.** "Why is LLM decode memory-bound?"

**A.** decode는 토큰 하나를 만들 때 모든 선형층이 벡터 하나에 대한 GEMV라서, weight 전체를 읽어 각 원소를 한 번만 쓴다. intensity가 int8 기준 약 2 FLOP/B이고, 여기에 과거 전체의 KV-cache 읽기가 더해진다. 그래서 tokens/s 상한은 메모리 대역폭 ÷ (weight 바이트 + KV 바이트)다. SmolLM2-135M fp32를 M2 CPU에서 돌리면 decode가 토큰당 17.6 ms로 약 30 GB/s의 weight 스트림에 해당했고, prefill은 weight를 여러 토큰에 재사용해 토큰당 0.5 ms였다.

> In decode every linear layer is a matrix-vector product: we read every weight once to produce one token, so the intensity is about two operations per byte at int8. On top of that we read the whole KV cache for the context. So the ceiling is memory bandwidth divided by weight bytes plus KV bytes, no matter how many TOPS the chip has. On my laptop a 135M-parameter model in fp32 took about 17 milliseconds per decoded token — about 30 GB/s of weight streaming — while prefill was around half a millisecond per token because each weight read is reused across the whole prompt.

**Q.** "What model characteristics make an NPU underperform?"

**A.** 다섯 가지를 본다. (1) 낮은 intensity와 적은 재사용 — depthwise, batch 1 FC, RNN step, decode. (2) MAC 배열 폭보다 작은 채널·차원, 정렬 안 된 채널. (3) 순차 의존성 — RNN, 토큰 단위 decode. (4) 지원 안 되는 op — softmax, LayerNorm, 특이한 activation, gather, 동적 shape 때문에 CPU로 fallback. (5) 작은 op가 많아 op당 고정 오버헤드가 지배하는 경우. 그리고 양자화에 민감해서 fp16으로 남겨야 하는 층도 느린 경로로 간다.

> Five things. Low arithmetic intensity — depthwise, batch-one fully connected layers, RNN steps, LLM decode. Dimensions smaller than or misaligned with the MAC array. Sequential dependencies, like recurrence or token-by-token decode. Unsupported ops or dynamic shapes that force a CPU fallback — softmax, LayerNorm, gathers, variable sequence length. And many tiny ops where the per-op launch overhead dominates. Layers that must stay in higher precision because of quantization sensitivity also end up on slower paths.

**Q.** "How does the KV cache grow, and what does it cost per token?"

**A.** KV-cache는 `2 × 층 × KV head × head_dim × context 길이 × 바이트`로 context에 선형으로 자란다. decode 토큰마다 이것을 전부 읽으니, 긴 context에서는 weight보다 KV 읽기가 커질 수 있다. SmolLM2-135M 모양이면 fp16 KV가 토큰당 23 KB라 8K context에서 180 MiB로 int8 weight 128 MiB를 넘는다. 그래서 GQA/MQA, KV 양자화, sliding window, 미리 할당한 정적 캐시가 중요하다. 전체 LLM 성능 모델은 prefill/decode 분석에서 더 자세히 다룬다.

> The KV cache is 2 × layers × KV heads × head dimension × context length × bytes per element, so it grows linearly with context, and every decode step reads all of it. For a 135M model shaped like SmolLM2, fp16 KV is about 23 KB per token, so at 8K context it's 180 MB — larger than the int8 weights. That's why GQA, KV quantization, sliding windows and a preallocated in-place cache matter so much on device.

**Q.** "Compare a CNN and a ViT at the same FLOPs on edge hardware."

**A.** 같은 MAC이어도 결과는 하드웨어에 따라 뒤집힌다. 제 CPU 측정에서는 약 300 MMAC의 작은 ViT가 3.8 ms, MobileNetV2가 8.5~32 ms였다. ViT는 거의 전부 GEMM이고 MobileNet은 depthwise가 병목이기 때문이다. 하지만 conv 중심 NPU에서는 MobileNet이 가장 잘 튜닝된 경로이고, ViT의 softmax·LayerNorm·GELU는 벡터 유닛이나 CPU로 가며 int8 양자화도 더 어렵다. 그래서 타깃 NPU의 op 지원표와 레이어별 프로파일을 보고 결정하고, 많은 경우 CNN 앞단에 attention을 조금 섞은 hybrid가 답이다.

> At equal FLOPs the answer flips with the hardware. On a CPU, a small ViT at about 300 MMACs ran in under 4 ms while MobileNetV2 took 8 to 30 ms, because the ViT is almost pure GEMM and MobileNet is bottlenecked by depthwise. On a conv-oriented NPU it's often the reverse: MobileNet is the vendor's best-tuned path, while the ViT's softmax, LayerNorm and GELU fall back to vector units or the CPU and are harder to quantize. So I'd check the op-support table and a per-layer profile — and often end up with a hybrid, a conv stem with a little attention.

---

## 13. 직접 해보기

1. **손계산**: 3×3 conv, C_in = 32, C_out = 128, 28×28, padding 1, int8에서 MAC, weight 바이트, activation 바이트, 최소 트래픽 intensity를 손으로 계산하라. 같은 입력에 depthwise 3×3(C = 32)이면 intensity는?
정답: 표준 28·28·32·128·9 = 28.9 M MAC, weight 36.9 KB, activation 25.1 + 100.4 = 125.4 KB, intensity ≈ 2·28.9 M / 162.3 K ≈ 356 FLOP/B. depthwise는 MAC 0.226 M, weight 288 B, activation 25.1 + 25.1 KB, intensity ≈ 9 FLOP/B.

2. **손계산**: 3.3의 C = 256 측정값으로 depthwise separable이 MAC의 몇 %, 시간의 몇 %인지 계산하라.
정답: dw MAC 7.2/(7.2 + 205.5) ≈ 3.4%, 시간 0.609/(0.609 + 0.439) ≈ 58%.

3. **코드**: ex4의 장난감 모델에서 배열을 16×16으로 줄이면 depthwise 활용률과 사이클은 어떻게 바뀌나? 코드를 고쳐 확인하라.
힌트: 활용률은 9/256 근처로 오르지만 채널마다 타일이 하나씩인 구조는 그대로다. 작은 배열일수록 depthwise에 덜 불리하다.

4. **손계산**: SmolLM2-135M을 int4 weight(0.5 B/param), fp16 KV로 돌린다면 LPDDR4X 8.5 GB/s에서 context 0과 4096의 decode tokens/s 상한은?
정답: weight 134.5 M × 0.5 B ≈ 64 MiB → 약 126 tok/s. KV 23,040 B × 4096 ≈ 90 MiB 추가 → 8.5e9 / (67.3 M + 94.4 M B) ≈ 53 tok/s.

5. **코드**: ex9의 TinyTF에서 창 W를 25, 100, 400으로 바꿔 프레임당 latency와 상태 크기를 재라. 어느 쪽이 먼저 문제가 되나?
힌트: 상태는 W에 정확히 비례한다(6.4k, 25.6k, 102.4k 값). latency는 직접 재서 확인할 것 — 작은 W에서는 op 오버헤드에 묻혀 차이가 작을 것으로 예상된다. MCU라면 대개 상태(SRAM)가 먼저 막힌다. 코드의 `macs` 식에 박힌 100도 W로 바꿔야 한다.

6. **코드**: stream.c의 TCN 내적 루프를 누산기 4개로 나누거나, 루프 순서를 바꿔 출력 채널 방향(`y[o] += w[j][o]·taps[j]`, weight 전치)으로 벡터화되게 고쳐 µs/frame을 다시 재라.
힌트: 의존 체인이 끊기면 GRU와 비슷하거나 더 빨라진다. `-O2` 그대로, 결과 값이 같은지(허용 오차 안) 반드시 비교할 것.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| arithmetic intensity | 연산 강도 | 옮긴 바이트당 연산 수(FLOP/B). ridge point보다 작으면 memory-bound (D3) |
| compute-bound | 연산 한계 | MAC 유닛이 먼저 바닥나는 상태. 표준 conv, prefill |
| memory-bound | 메모리 한계 | 데이터 공급이 먼저 막히는 상태. depthwise, GEMV, decode |
| GEMM | 행렬 × 행렬 | 재사용이 많아 가속기에 이상적인 연산 |
| GEMV | 행렬 × 벡터 | weight를 한 번 쓰고 버림. batch 1 FC, RNN step, decode |
| reuse (재사용) | 한 번 가져온 값을 몇 번 쓰나 | 높을수록 intensity가 높다 |
| systolic array | 수축 배열 | PE 격자로 데이터를 흘려 가며 MAC하는 NPU 구조 |
| PE | processing element | systolic array의 MAC 셀 하나 |
| utilization (활용률) | 실제 MAC ÷ (사이클 × PE 수) | 모양이 배열에 안 맞으면 급락 |
| depthwise conv | 채널별 conv | 채널 안 섞음, MAC은 적고 intensity 낮음 |
| pointwise conv | 1×1 conv | GEMM과 같음, 채널만 섞음 |
| prefill | 프롬프트 처리 | 토큰 T개를 한 번에, GEMM형 |
| decode | 토큰 생성 | 한 번에 1 토큰, GEMV형 |
| KV-cache | 과거 토큰의 K, V 저장소 | context에 선형 증가하는 상태 |
| TCN | temporal convolutional network | dilated causal 1D conv 스택 |
| receptive field | 수용 영역 | 출력 하나가 보는 입력 범위 |
| ring buffer | 링버퍼 | 스트리밍 conv가 과거 프레임을 보관하는 고정 크기 버퍼 |
| fallback | 대체 실행 | NPU가 못 하는 op를 CPU/DSP가 대신 실행 |
| op fusion | 연산 결합 | 여러 op를 한 커널로 합쳐 메모리 왕복·호출 비용 제거 |
| p10 / median | 10백분위 / 중앙값 | 잡음 많은 측정에서 평균 대신 쓰는 통계 |

---

## 15. 요약 & 체크리스트

모델 계열마다 전형적인 연산 모양이 있고, 그 모양이 하드웨어 병목을 정한다. 표준 conv와 1×1 conv, transformer prefill은 재사용이 높은 GEMM이라 compute-bound이고 NPU의 TOPS를 제대로 쓴다. depthwise는 MAC이 적지만 activation 트래픽은 그대로라 memory-bound이고, NPU MAC 배열을 거의 못 채운다. 그래서 MobileNet은 MAC 절약만큼 빨라지지 않는다(M2 실측: 7.9배 적은 MAC, 1.9배 빠름). batch 1 FC, RNN step, LLM decode는 GEMV라서 weight 바이트 ÷ 대역폭이 속도를 정한다. RNN은 순차 의존성 때문에 가속기에서는 불리하지만, 작은 weight가 SRAM에 상주하고 상태가 수백 바이트인 MCU에서는 오히려 이상적이다. transformer decode는 여기에 context에 따라 자라는 KV-cache 읽기가 더해진다. 작은 스트리밍 모델에서는 MAC보다 op 개수와 호출 오버헤드가 지배한다. 결론은 항상 "계열별 실효 효율로 예측하고, 타깃에서 재서 확인한다"이다.

- [ ] 11개 비교 축을 말하고, 새 모델을 받았을 때 표로 채울 수 있다
- [ ] 표준 conv와 depthwise의 재사용 횟수와 intensity를 손으로 계산할 수 있다
- [ ] depthwise가 systolic array 활용률을 떨어뜨리는 이유를 K, N 차원으로 설명할 수 있다
- [ ] "MobileNet이 MAC만큼 빨라지지 않는 이유"를 실측 숫자와 함께 30초 안에 말할 수 있다
- [ ] batch 1 FC가 GEMV라서 대역폭이 속도를 정한다는 것을 식으로 쓸 수 있다
- [ ] RNN의 총 latency가 시퀀스 길이에 선형인 이유와 MCU에서 유리한 이유를 말할 수 있다
- [ ] prefill과 decode의 병목 차이를 설명하고, tokens/s 상한을 대역폭으로 계산할 수 있다
- [ ] attention의 T² 항이 선형 항을 넘는 T를 `w_layer / (2d)`로 추정할 수 있다
- [ ] 스트리밍 과제에서 TCN·GRU·transformer의 프레임당 MAC과 상태 바이트를 계산할 수 있다
- [ ] 계열별로 실리콘 벤더에게 물어야 할 질문을 3개 이상 말할 수 있다

---

## 참고 자료

- Williams, Waterman, Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", Communications of the ACM, 2009 — intensity와 ridge point (D3)
- Sze, Chen, Yang, Emer, "Efficient Processing of Deep Neural Networks" (Morgan & Claypool, 2020) — dataflow, systolic array, 레이어 모양별 활용률
- Howard et al., "MobileNets: Efficient Convolutional Neural Networks for Mobile Vision Applications", 2017 / Sandler et al., "MobileNetV2: Inverted Residuals and Linear Bottlenecks", CVPR 2018
- Jouppi et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit", ISCA 2017 — systolic array와 LSTM/MLP의 낮은 활용률 사례
- Gholami et al., "AI and Memory Wall", 2024 (arXiv 2403.14123) — decode가 대역폭에 묶이는 이유 (D5)
- Dosovitskiy et al., "An Image is Worth 16x16 Words" (ViT), ICLR 2021 / Touvron et al., "Training data-efficient image transformers" (DeiT), ICML 2021
- Bai, Kolter, Koltun, "An Empirical Evaluation of Generic Convolutional and Recurrent Networks for Sequence Modeling" (TCN), 2018
- MIT 6.5940 TinyML and Efficient Deep Learning (Song Han) — https://efficientml.ai
- PyTorch 문서: `torch.utils.flop_counter`, `torch.profiler` — https://pytorch.org/docs/stable/
- ONNX Runtime 문서 — https://onnxruntime.ai/docs/
- CMSIS-NN — https://github.com/ARM-software/CMSIS-NN
