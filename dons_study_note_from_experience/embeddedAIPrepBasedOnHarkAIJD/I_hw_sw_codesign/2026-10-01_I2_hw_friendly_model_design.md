# I2. HW 친화 모델 설계 — 모델팀에 주는 설계 규칙과 그 근거

> **이 노트를 다 읽으면**: micro-NPU·앱 SoC NPU를 겨냥한 모델 설계 규칙 16개를 "HW 이유 → 측정 근거 → 자동 검사" 세 줄로 설명할 수 있다 · ONNX + 타깃 프로파일을 받아 위반·추정 비용을 보고하는 HW 친화도 linter를 직접 짜서 돌릴 수 있다 · naive → partial → fixed 세 변형에서 CPU 지연, CoreML partition 수, TFLite int8 크기·SQNR, peak activation이 어떻게 바뀌는지 숫자로 보여 줄 수 있다 · 규칙을 모델 카드·CI gate·설계 리뷰로 모델팀에 전달하는 방법을 말할 수 있다
> **JD 연결**: "Co-design model architectures that meet latency, memory, power, and bandwidth constraints", "Work closely with ML researchers to …" · study_prep_list **I2** 행: NPU 지원 op만 쓰기, 채널 정렬, activation 크기 줄이기, 양자화 친화 구조 ("모델팀에 줄 피드백")
> **Don 기준 난이도**: 코딩 규칙·정적 분석·CI gate·HW 제약을 펌웨어 규칙으로 바꾸는 일은 이미 해 봤다 (SSD FW의 메모리 맵·DMA 정렬 규칙과 같은 종류) / 새로 배울 것은 규칙 하나하나가 **모델 구조의 어떤 선택**과 연결되는지, 그리고 그것을 ONNX 그래프에서 자동으로 찾는 방법
> **선행 노트**: B1(활성화·BN folding), B2(depthwise), C1·C2(양자화), C6(그래프 최적화·채널 패딩·lowering), C7(FLOPs≠latency, LUT), D2(peak 메모리), D4(계열별 성능), E5(systolic 활용률·fallback 비용), F5(ONNX·EP·deploy lint). 이 노트는 메커니즘을 다시 유도하지 않고, 그 결과를 **규칙집과 도구**로 묶는다

---

## 0. 큰 그림 — 설계 규칙은 모델팀과의 인터페이스 스펙이다

SSD 펌웨어 시절을 떠올려 보자. HW 팀은 "DMA 디스크립터는 64 B 정렬, 한 번에 최대 128 KB, 이 레지스터는 W1C"라는 규칙을 문서로 주고, 펌웨어는 그 규칙을 어기면 동작은 하더라도 느리거나(정렬 안 된 DMA가 두 번 쪼개짐) 아예 멈췄다. 그리고 좋은 팀은 그 규칙을 사람 기억에 맡기지 않고 **정적 분석·assert·CI**로 걸었다.

embedded AI 엔지니어와 모델팀의 관계가 정확히 이렇다. 모델팀은 PyTorch에서 정확도를 올리는 데 집중하고, 무엇이 NPU에서 싸고 비싼지는 모른다. 그들이 GELU 하나, 채널 65개, stride 1짜리 첫 층 하나를 넣는 순간 기기에서는 CPU fallback, 25% 느린 계단, SRAM 초과가 터진다. 그런데 이걸 **배포 직전에** 알게 되면 재학습 몇 주가 날아간다. 그래서 이 노트의 목표는 세 가지다.

- **규칙집**: "이렇게 설계하라"를 15개 남짓의 문장으로. 각 규칙에는 HW 이유와 측정 근거가 붙어 있어야 모델팀이 납득한다. 근거 없는 규칙은 무시당한다.
- **자동 검사기**: 규칙을 ONNX 그래프에서 기계적으로 찾아 "몇 번 위반, 대략 얼마 손해"를 보고하는 linter. 모델팀이 학습 전에 스스로 돌려 볼 수 있어야 한다.
- **전달 경로**: 모델 카드(HW 필드), CI gate, 설계 리뷰. 규칙이 실제로 지켜지게 만드는 절차.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 330">
<text x="20" y="24" font-size="14">HW 친화 설계 규칙이 끼어드는 자리 — 학습 전에 잡을수록 싸다</text>
<rect x="20" y="50" width="130" height="62" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="85" y="76" font-size="13" text-anchor="middle">모델팀</text> <text x="85" y="96" font-size="12" text-anchor="middle">PyTorch 설계·학습</text>
<rect x="185" y="50" width="120" height="62" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="245" y="76" font-size="13" text-anchor="middle">ONNX export</text> <text x="245" y="96" font-size="12" text-anchor="middle">static shape</text>
<rect x="340" y="40" width="150" height="82" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2.5"/> <text x="415" y="64" font-size="13" text-anchor="middle">HW 친화도 linter</text> <text x="415" y="84" font-size="12" text-anchor="middle">+ 타깃 프로파일</text> <text x="415" y="104" font-size="12" text-anchor="middle">(op·정렬·SRAM·…)</text>
<rect x="525" y="50" width="135" height="62" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="592" y="76" font-size="13" text-anchor="middle">벤더 컴파일러</text> <text x="592" y="96" font-size="12" text-anchor="middle">Vela · QNN · …</text>
<rect x="525" y="180" width="135" height="62" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="592" y="206" font-size="13" text-anchor="middle">기기 실측</text> <text x="592" y="226" font-size="12" text-anchor="middle">지연·전력·정확도</text>
<rect x="300" y="180" width="190" height="62" rx="6" fill="none" stroke="currentColor" stroke-width="1.5"/> <text x="395" y="206" font-size="13" text-anchor="middle">모델 카드 · CI 결과</text> <text x="395" y="226" font-size="12" text-anchor="middle">ops · MAC · peak · bytes</text>
<line x1="150" y1="81" x2="182" y2="81" stroke="currentColor"/> <polygon points="185,81 177,76 177,86" fill="currentColor"/>
<line x1="305" y1="81" x2="337" y2="81" stroke="currentColor"/> <polygon points="340,81 332,76 332,86" fill="currentColor"/>
<line x1="490" y1="81" x2="522" y2="81" stroke="currentColor"/> <polygon points="525,81 517,76 517,86" fill="currentColor"/>
<line x1="592" y1="112" x2="592" y2="177" stroke="currentColor"/> <polygon points="592,180 587,172 597,172" fill="currentColor"/>
<line x1="525" y1="211" x2="493" y2="211" stroke="currentColor"/> <polygon points="490,211 498,206 498,216" fill="currentColor"/>
<line x1="415" y1="122" x2="415" y2="177" stroke="#3f9a6b"/> <polygon points="415,180 410,172 420,172" fill="#3f9a6b"/>
<line x1="300" y1="211" x2="85" y2="211" stroke="#d0564a" stroke-dasharray="5 3"/> <line x1="85" y1="211" x2="85" y2="115" stroke="#d0564a" stroke-dasharray="5 3"/> <polygon points="85,112 80,120 90,120" fill="#d0564a"/>
<text x="95" y="232" font-size="12">피드백: 위반·비용·측정치</text>
<line x1="592" y1="242" x2="592" y2="290" stroke="#e08a3c" stroke-dasharray="4 3"/> <line x1="592" y1="290" x2="415" y2="290" stroke="#e08a3c" stroke-dasharray="4 3"/> <line x1="415" y1="290" x2="415" y2="125" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="20" y="315" font-size="12">주황 점선: 실측으로 프로파일(비용 상수·op 목록)을 보정한다. 초록: linter는 몇 초, 컴파일+실측은 몇 시간, 재학습은 며칠</text>
</svg>
```

그림 1 — 규칙과 linter의 위치. linter는 벤더 컴파일러를 대신하지 않는다. 컴파일러 앞에서 **싸고 빠르게 80%를 걸러 내는 필터**이고, 기기 실측이 그 필터의 상수를 계속 보정한다 (C7의 LUT 보정과 같은 구조).

이 노트에서 쓰는 사례는 이전 노트들과 같다 (**가정**, Hark 같은 웨어러블이라면): 항상 켜진 MCU + micro-NPU(Ethos-U55급, SRAM 512 KB), 오디오 DSP, 그리고 앱 SoC의 NPU(int8 약 10 TOPS, LPDDR 약 17 GB/s). 음성 키워드(KWS)와 IMU 제스처 모델이 MCU 쪽, 더 큰 모델이 SoC 쪽에서 돈다.

모든 측정은 Apple M2 노트북에서 했고, 다른 작업이 동시에 돌아 부하가 높았다(load average 3~11). 그래서 **절대값이 아니라 비율과 방향**을 본다. CPU 측정은 ORT 단일 스레드, 3세트 × N회 반복의 중앙값이다. CoreML EP는 Apple NPU의 대용(프록시)일 뿐이며, CoreML이 실제로 Neural Engine·GPU·CPU 중 무엇을 썼는지는 이 측정으로 알 수 없다.

---

## 1. 규칙표 한 장 — 모델팀에게 주는 16개 문장

먼저 결론부터 한 장에 놓는다. 각 행의 "근거"는 이 노트(예제 번호) 또는 이전 노트의 측정·시뮬레이션이다. "자동 검사"는 3절의 linter가 실제로 구현한 검사다. 심각도는 micro-NPU 프로파일 기준의 기본값이며, 팀이 합의해서 바꾼다.

| ID | 규칙 (모델팀에게 하는 말) | HW 이유 | 근거 (측정·시뮬) | 자동 검사 | 심각도 |
|---|---|---|---|---|---|
| R0 | 입력 해상도·프레임 수·채널(mel 수)을 **가장 먼저** 정한다 | 모든 층의 MAC과 activation이 입력 크기에 비례 | 예제 8: 40×98 → 32×49에서 MAC 2.4배, peak 2.4배 감소 | 요약값(MMAC, peak) | 설계 단계 |
| R1 | NPU가 지원하는 op만 쓴다 | 미지원 op는 CPU fallback. 경계마다 동기화·캐시 관리·복사 | E5 예제 9 (경계 제거로 0.449 → 0.257 ms) · 예제 12: naive는 CoreML 17조각 + CPU 21개 | op whitelist 대조 + 경계 수 | ERROR |
| R2 | 활성화는 ReLU/ReLU6 우선. GELU·SiLU는 근거가 있을 때만 | GELU는 Erf 등 5개 op로 분해되고, 범위가 열려 있어 int8 scale이 outlier에 끌려간다 | 예제 3: int8 SQNR ReLU6 49.9 dB vs GELU 21.5 dB | Erf·Sigmoid 패턴 탐지 | ERROR/WARN |
| R3 | BN은 conv **바로 뒤**에 둔다 (접을 수 있게) | 접히면 비용 0. 활성화 뒤에 있으면 원소별 op + 메모리 왕복으로 남는다 | B1 7절 · 예제 2: naive에 BN 9개 잔존 | BatchNormalization 노드 | WARN |
| R4 | shape은 전부 static | NPU 컴파일러는 tiling·arena를 컴파일 시점에 고정 | F5 예제 6 · 예제 12: 동적 입력 naive의 CoreML 분할 | dim_param 검사 | ERROR |
| R5 | 텐서 rank ≤ 4 | NPU 텐서 디스크립터는 대개 NHWC 4D | (스펙 규칙) | rank 검사 | ERROR |
| R6 | 채널 수는 16(또는 32)의 배수 | MAC 배열·brick 단위로 0을 채워 계산 | 예제 5: int8 conv 64 → 65에서 +25% · C6 7.5 패딩 낭비율 | Cin·Cout % align, MAC 낭비율 | WARN |
| R7 | 작은 op를 많이 만들지 말고, 층은 적게 굵게 | op마다 디스패치·DMA 설정·동기화 고정비 | 예제 6: 같은 MAC에서 64층 0.895 ms vs 1층 0.470 ms | 출력 512원소 미만 op 수 | WARN |
| R8 | depthwise 비중은 타깃의 depthwise 성능을 보고 정한다 | 배열 활용률이 낮다 (채널마다 K=9, N=1) | D4 ex4 0.85% · E5 예제 4 2.8% · 예제 2: MAC 10%가 toy cycle 73% | dw의 toy cycle 비중 | WARN |
| R9 | peak activation은 SRAM 예산 안 | arena가 SRAM에 안 들어가면 배포 불가 (또는 DRAM spill) | 예제 7: 689 KB(FAIL) → 184 KB(OK), stem stride 2 하나로 | liveness peak | ERROR |
| R10 | Transpose·Reshape·Concat 같은 layout op를 줄인다 | 순수 메모리 복사이고, NPU에서 미지원이거나 비싸다 | C6 7절 · 예제 9: 옛 exporter에서 F.pad 4개가 그래프를 8노드 → 72노드로 | layout op 수 | WARN |
| R11 | 제어 흐름은 그래프 밖(펌웨어)으로 | If·Loop·NonZero는 데이터 의존 → 정적 컴파일 불가 | F5 예제 7 (If 노드) | op 검사 | ERROR |
| R12 | 양자화 친화: 유계 활성화, outlier 채널 없음, 넓은 범위 op를 int8 앞에 두지 않는다 | per-tensor int8 scale 하나가 텐서 전체의 해상도를 정한다 | 예제 10: 그래프 안 log 하나로 int8 정확도 0.843 → 0.197 · C1 outlier 예 | Log·Exp·Div 등 탐지 | WARN (앞단이면 ERROR 권장) |
| R13 | MCU급 NPU에서는 softmax·LayerNorm을 최소화 | reduce + exp/rsqrt. LUT·특수 경로·CPU fallback 후보 | 예제 10: LayerNorm이 TFLite에서 float op로 남음 · E5 예제 9 | 개수 | WARN |
| R14 | 스트리밍 모델은 causal (미래 프레임을 보지 않는다) | 미래 프레임 수 × 프레임 주기 = 그대로 지연 | 예제 9: 대칭 padding TCN은 150 ms lookahead | 시간축 오른쪽 padding 누적 | WARN (스트리밍 프로파일) |
| R15 | 파라미터·embedding은 flash/DRAM 예산 안. SLM은 vocab부터 | flash 용량, LPDDR 대역폭 | 예제 11: SmolLM2-135M embedding이 전체의 21% | 가중치 바이트 | ERROR |

이 표를 읽는 법: 왼쪽 두 칸은 모델팀이 읽는 부분이고, 오른쪽 세 칸은 그 규칙을 **왜 믿어야 하는지와 누가 지키게 만드는지**다. 면접에서 "모델팀에게 어떤 가이드라인을 주겠냐"는 질문을 받으면 이 표의 앞 6~7행을 HW 이유와 함께 말하면 된다 (14절).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<text x="20" y="24" font-size="14">규칙을 HW 자원별로 묶으면 — 무엇을 아끼려는 규칙인가</text>
<rect x="20" y="45" width="122" height="200" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="81" y="68" font-size="13" text-anchor="middle">MAC 배열</text> <text x="81" y="98" font-size="12" text-anchor="middle">R6 채널 정렬</text> <text x="81" y="120" font-size="12" text-anchor="middle">R7 작은 op</text> <text x="81" y="142" font-size="12" text-anchor="middle">R8 depthwise</text> <text x="81" y="226" font-size="12" text-anchor="middle">활용률</text>
<rect x="152" y="45" width="122" height="200" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="213" y="68" font-size="13" text-anchor="middle">온칩 SRAM</text> <text x="213" y="98" font-size="12" text-anchor="middle">R0 입력 크기</text> <text x="213" y="120" font-size="12" text-anchor="middle">R9 peak</text> <text x="213" y="142" font-size="12" text-anchor="middle">R10 layout</text> <text x="213" y="226" font-size="12" text-anchor="middle">바이트</text>
<rect x="284" y="45" width="122" height="200" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="345" y="68" font-size="13" text-anchor="middle">컴파일러·런타임</text> <text x="345" y="98" font-size="12" text-anchor="middle">R1 op 지원</text> <text x="345" y="120" font-size="12" text-anchor="middle">R4 static shape</text> <text x="345" y="142" font-size="12" text-anchor="middle">R5 rank</text> <text x="345" y="164" font-size="12" text-anchor="middle">R11 제어 흐름</text> <text x="345" y="186" font-size="12" text-anchor="middle">R13 norm·softmax</text> <text x="345" y="226" font-size="12" text-anchor="middle">fallback 경계</text>
<rect x="416" y="45" width="122" height="200" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="477" y="68" font-size="13" text-anchor="middle">정수 산술</text> <text x="477" y="98" font-size="12" text-anchor="middle">R2 활성화</text> <text x="477" y="120" font-size="12" text-anchor="middle">R3 BN fold</text> <text x="477" y="142" font-size="12" text-anchor="middle">R12 양자화 친화</text> <text x="477" y="226" font-size="12" text-anchor="middle">dB · 정확도</text>
<rect x="548" y="45" width="122" height="200" rx="6" fill="none" stroke="#888" stroke-width="2"/> <text x="609" y="68" font-size="13" text-anchor="middle">시스템</text> <text x="609" y="98" font-size="12" text-anchor="middle">R14 causal</text> <text x="609" y="120" font-size="12" text-anchor="middle">R15 flash·vocab</text> <text x="609" y="226" font-size="12" text-anchor="middle">지연 · 용량</text>
<text x="20" y="275" font-size="12">아래 칸은 각 묶음을 재는 단위. 규칙 위반의 "비용"은 이 단위로 보고해야 모델팀이 trade-off를 판단할 수 있다</text>
</svg>
```

그림 2 — 16개 규칙을 아끼려는 HW 자원으로 묶은 것. linter 리포트도 이 단위(활용률·바이트·경계 수·dB·ms)로 비용을 말한다.

---

## 2. 실험 무대 — 같은 과제, 세 가지 설계

규칙의 효과를 보이려면 **같은 과제를 푸는** 모델을 규칙을 어긴 정도만 다르게 만들어야 한다. 과제는 합성 KWS다: 1초 오디오의 log-mel 특징 40 mel × 98 프레임(hop 10 ms)을 받아 4개 키워드 + 배경, 5 클래스로 분류한다. 키워드는 시간-주파수 평면의 서로 다른 모양의 능선(올라가는 chirp, 내려가는 chirp, 낮은/높은 평탄음)으로 흉내 냈다. 진짜 음성 데이터가 아니므로 정확도 숫자 자체는 의미가 없고, **같은 모델의 float과 int8 비교**와 **변형 간 비교**만 본다.

| | naive (흔한 첫 설계) | partial (op 수준만 고침) | fixed (구조까지 고침) |
|---|---|---|---|
| 입력 | (B, T, F) 선형 power. 그래프 안에서 log·정규화·transpose | (1, 1, 40, 98) 정규화 log-mel (DSP가 계산) | 같음 |
| stem | conv3×3, 60ch, **stride 1** | 같음 | conv3×3, 64ch, **stride 2** |
| 블록 | DS 블록 4개 (60, 90, 90, 90ch) + SE | DS 블록 4개 (60, 90, 90, 90ch) | DS 블록 4개 (64, 96, 96, 96ch) |
| 활성화 · norm | GELU, BN을 활성화 **뒤**에, head에 LayerNorm | ReLU6, conv → BN → ReLU6 | 같음 |
| 출력 | softmax 확률 | logits (argmax는 펌웨어에서) | 같음 |
| export | batch·time 동적 | static | static |

partial은 "linter의 ERROR 중 op 관련된 것만 고친" 상태다. 재학습은 필요하지만 구조(해상도·채널)는 그대로다. fixed는 남은 구조 문제(SRAM, 채널 정렬)까지 고친다. 실무에서도 이 두 단계로 나뉘는 경우가 많다: op 교체는 정확도 영향이 작아서 빨리 합의되고, 해상도·채널 변경은 정확도 재검증이 필요해서 협상이 길다.

공용 모듈 `i2_models.py` — 데이터 생성, 세 변형의 정의(SPECS), 모델, 학습 함수.

```python
# i2_models.py — I2 공용: 합성 KWS 데이터 + 같은 과제의 모델 3종 (naive / partial / fixed)
import numpy as np, torch, torch.nn as nn
F, T, NCLS = 40, 98, 5                       # 40 mel x 98 프레임(1 s, hop 10 ms), 4 키워드 + 배경
def make_data(n, seed):
    """log-mel 비슷한 합성 데이터. 클래스마다 시간-주파수 능선 모양이 다르다."""
    rng = np.random.default_rng(seed); L = rng.normal(-6, 1.0, (n, F, T)).astype(np.float32)
    y = rng.integers(0, NCLS, n)
    for i in range(n):
        if y[i] == 4: continue                                  # 배경(키워드 없음)
        t0 = rng.integers(0, T - 45); f0 = rng.integers(8, 32)
        for k in range(40):
            f = int(f0 + [0.4, -0.4, 0.0, 0.0][y[i]] * (k - 20)) + (12 if y[i] == 3 else 0)
            if 0 <= f < F - 1: L[i, f:f + 2, t0 + k] += 4.0
    return L, y.astype(np.int64)                               # L: 자연로그 단위의 log-power
MU, SD = -5.9, 1.1                                              # 정규화 상수 (학습 데이터에서 잰 값을 고정)

SPECS = {   # stem=(채널, stride), blocks=[(출력 채널, stride)], DS 블록 = dw3x3 + pw1x1
  "naive":   dict(log_in=True,  act="gelu",  bn_after_act=True,  se=True,  head_ln=True,  softmax=True,
                  stem=(60, 1), blocks=[(60, 1), (90, 2), (90, 1), (90, 1)]),
  "partial": dict(log_in=False, act="relu6", bn_after_act=False, se=False, head_ln=False, softmax=False,
                  stem=(60, 1), blocks=[(60, 1), (90, 2), (90, 1), (90, 1)]),
  "fixed":   dict(log_in=False, act="relu6", bn_after_act=False, se=False, head_ln=False, softmax=False,
                  stem=(64, 2), blocks=[(64, 1), (96, 2), (96, 1), (96, 1)]),
}
def act(name): return {"gelu": nn.GELU(), "relu6": nn.ReLU6(), "relu": nn.ReLU(), "silu": nn.SiLU()}[name]

class CBA(nn.Module):           # Conv → BN → act (또는 naive: Conv → act → BN)
    def __init__(s, ci, co, k, st, g, sp):
        super().__init__(); s.conv = nn.Conv2d(ci, co, k, st, k // 2, groups=g, bias=False)
        s.bn = nn.BatchNorm2d(co); s.act = act(sp["act"]); s.after = sp["bn_after_act"]
    def forward(s, x):
        x = s.conv(x); return s.bn(s.act(x)) if s.after else s.act(s.bn(x))

class SE(nn.Module):            # squeeze-excite: GAP → FC → ReLU → FC → Sigmoid → Mul
    def __init__(s, c):
        super().__init__(); s.f1 = nn.Linear(c, c // 4); s.f2 = nn.Linear(c // 4, c)
    def forward(s, x):
        w = torch.sigmoid(s.f2(torch.relu(s.f1(x.mean((2, 3))))))
        return x * w[:, :, None, None]

class KWS(nn.Module):
    def __init__(s, sp):
        super().__init__(); s.sp = sp; c0, st0 = sp["stem"]
        s.stem = CBA(1, c0, 3, st0, 1, sp); layers, ci = [], c0
        for co, st in sp["blocks"]:
            layers.append(nn.ModuleDict(dict(dw=CBA(ci, ci, 3, st, ci, sp), pw=CBA(ci, co, 1, 1, 1, sp),
                                             se=SE(co) if sp["se"] else nn.Identity())))
            layers[-1].res = (st == 1 and ci == co); ci = co
        s.blocks = nn.ModuleList(layers)
        s.ln = nn.LayerNorm(ci) if sp["head_ln"] else nn.Identity(); s.fc = nn.Linear(ci, NCLS)
    def forward(s, x):
        if s.sp["log_in"]:      # naive: 입력 = (B, T, F) 선형 power → 그래프 안에서 log·정규화·transpose
            x = ((torch.log(x + 1e-6) - MU) / SD).transpose(1, 2).unsqueeze(1)
        x = s.stem(x)
        for b in s.blocks:
            y = b["se"](b["pw"](b["dw"](x))); x = x + y if b.res else y
        x = s.fc(s.ln(x.mean((2, 3))))
        return torch.softmax(x, -1) if s.sp["softmax"] else x

def to_input(L, sp):            # 같은 log-mel L을 각 변형의 입력 형식으로
    if sp["log_in"]: return np.exp(L).transpose(0, 2, 1).copy()             # (B, T, F) 선형 power
    return ((L - MU) / SD)[:, None].astype(np.float32)                      # (B, 1, F, T) 정규화 log-mel

def train(name, epochs=3, seed=0, n=1200):
    sp = SPECS[name]; torch.manual_seed(seed); m = KWS(sp)
    L, y = make_data(n, 1); X = torch.from_numpy(to_input(L, sp)); Y = torch.from_numpy(y)
    opt = torch.optim.Adam(m.parameters(), 3e-3)
    for _ in range(epochs):
        m.train(); perm = torch.randperm(len(X))
        for i in range(0, len(X), 64):
            idx = perm[i:i + 64]; out = m(X[idx])
            loss = (nn.functional.nll_loss(torch.log(out + 1e-9), Y[idx]) if sp["softmax"]
                    else nn.functional.cross_entropy(out, Y[idx]))
            opt.zero_grad(); loss.backward(); opt.step()
    return m.eval()
```

naive의 문제들이 전부 "실제로 흔한" 선택이라는 점을 짚어 두자. 특징 추출을 모델 안에 넣으면 연구 코드가 깔끔해지고(log를 그래프에 넣기), Transformer에서 온 사람은 GELU·LayerNorm을 기본으로 쓰고, 해상도를 줄이면 정확도가 떨어질까 봐 stem stride를 1로 두고, 배포 툴 생각 없이 `dynamic_axes`를 켠다.

예제 1 — 세 변형을 같은 데이터로 학습하고 ONNX(opset 17, 옛 TorchScript exporter)로 내보낸다. naive만 batch·time을 동적으로 export한다.

```python
# ex1_train_export.py — 같은 과제(합성 KWS, 5 클래스)를 세 가지 설계로 학습하고 ONNX로 내보낸다
import torch, numpy as np, warnings
from i2_models import SPECS, train, make_data, to_input
warnings.filterwarnings("ignore")
Lte, yte = make_data(600, 2)                                       # 테스트 세트 (학습과 다른 seed)
for name, sp in SPECS.items():
    m = train(name); torch.save(m.state_dict(), f"{name}.pt")
    x = torch.from_numpy(to_input(Lte, sp))
    with torch.no_grad(): acc = (m(x).argmax(1).numpy() == yte).mean()
    nparam = sum(p.numel() for p in m.parameters())
    dyn = {"x": {0: "batch", 1: "time"}} if name == "naive" else None   # naive: batch·time을 동적으로
    torch.onnx.export(m, (x[:1],), f"{name}.onnx", input_names=["x"], output_names=["y"],
                      opset_version=17, dynamic_axes=dyn, dynamo=False)
    print(f"{name:8s} input {tuple(x.shape[1:])!s:12s} params {nparam:6d}  test acc {acc:.3f}")
```

```text
naive    input (98, 40)     params  44546  test acc 0.847
partial  input (1, 40, 98)  params  30275  test acc 0.862
fixed    input (1, 40, 98)  params  34085  test acc 0.867
```

출력에서 볼 것: 세 변형의 float 정확도는 0.85~0.87로 비슷하다 (짧게 학습한 합성 과제라 이 차이는 의미 없다). 즉 **HW 친화로 고친다고 정확도를 포기한 게 아니다** — 적어도 이 과제에서는. 실무에서도 op 교체(GELU → ReLU6)나 채널 정렬은 정확도 영향이 작은 경우가 많고, 해상도 축소(stem stride 2)는 반드시 재검증해야 하는 쪽이다. 학습은 부하 높은 CPU에서 십여 분 걸렸다 (naive가 가장 느리다 — 고해상도 depthwise의 backward 때문).

---

## 3. HW 친화도 linter 만들기

규칙을 사람 기억에 맡기면 지켜지지 않는다. 그래서 먼저 도구를 만들고, 4~9절에서 규칙마다 "linter가 무엇을 보고, 그 경고를 왜 믿어야 하는지"를 측정으로 확인한다. F5 10.3절의 `deploy_lint.py`가 "넘겨도 되는 파일인가"(형식 검사)였다면, 이 linter는 "**이 칩에서 좋은 설계인가**"(비용 검사)까지 본다.

### 3.1 설계 — 입력 두 개, 출력 두 개

```
  model.onnx ──┐                     ┌─► findings: (심각도, 규칙 ID, 메시지, 추정 비용)
               ├─► i2_lint.lint() ───┤
  profile ─────┘                     └─► summary: nodes · MMAC · weight KB · peak KB
  (op whitelist, align, SRAM,             · NPU/CPU 경계 수 · lookahead · toy cycles
   rank, 비용 상수 …)
                     exit code: ERROR가 하나라도 있으면 1  → CI gate (11절)
```

- **타깃 프로파일**이 핵심이다. 규칙의 숫자(정렬 16, SRAM 512 KB, 경계 1회 60 µs …)를 코드에 박지 않고 프로파일에 둔다. 칩이 바뀌면 프로파일만 바꾼다. 처음에는 벤더 문서와 추정으로 채우고, 기기 실측으로 계속 고친다 (그림 1의 주황 점선).
- **그래프 분석 바닥**(`i2_graph.py`): shape inference, Conv/Gemm을 GEMM 치수로 보기, D2식 liveness로 peak 계산.
- **규칙 엔진**(`i2_lint.py`): 규칙마다 10줄 이내. 각 규칙은 "찾기 → 심각도 → 비용 추정"의 세 단계다.

### 3.2 타깃 프로파일

```python
# i2_profile.py — 타깃 프로파일: "이 칩에서 무엇이 되고 무엇이 비싼가"를 숫자로 적은 계약서 (값은 가정)
MCU_NPU = dict(
    name="always-on micro-NPU (Ethos-U55급 가정)",
    ops={"Conv", "Relu", "Clip", "Add", "Mul", "MaxPool", "AveragePool", "GlobalAveragePool", "ReduceMean",
         "Gemm", "MatMul", "Reshape", "Flatten", "Squeeze", "Unsqueeze", "Concat", "Pad",
         "QuantizeLinear", "DequantizeLinear", "Sigmoid", "Tanh", "Softmax"},
    align=16, max_rank=4, act_bytes=1, weight_bytes=1,
    sram_kb=512, reserved_kb=128,                     # 512 KB 중 오디오 링버퍼·스택·NPU 명령열 등 128 KB 예약
    flash_kb=1024, array=16,                          # toy systolic 16x16 = 256 MAC/cycle (E5 예제 4 모델)
    switch_us=60.0, per_op_us=2.0, tiny_elems=512,    # 경계 1회 비용(E5 예제 9 가정), op당 고정비(가정)
    dw_cycle_share_max=0.40, layout_max=4, streaming=False, frame_ms=10.0, max_lookahead_frames=0)
SOC_NPU = dict(MCU_NPU, name="app SoC NPU (~10 TOPS int8 가정)", align=32, sram_kb=4096, reserved_kb=1024,
               flash_kb=1 << 20, array=64, switch_us=150.0, per_op_us=10.0,
               ops=MCU_NPU["ops"] | {"LayerNormalization", "HardSwish", "HardSigmoid", "Transpose", "Gelu", "Erf",
                                     "Div", "Sub", "Pow", "Sqrt", "Gather"})
```

주의: 이 whitelist는 **가상의** micro-NPU다. 실제 Ethos-U의 지원 범위는 Vela 버전별 지원 op 문서에서 채워야 하고, 대개 op 이름만이 아니라 속성 조건(커널 크기, stride, 채널 수 상한 등)까지 붙어 있다 (F8). 실제 프로파일은 ONNX op 이름이 아니라 **TFLite op + 조건** 형식일 수도 있다. 구조는 같다.

### 3.3 그래프 분석 바닥

```python
# i2_graph.py — ONNX 그래프에서 shape·MAC·peak activation을 뽑는 도구 (linter의 바닥)
import math, onnx
from onnx import shape_inference

def load_static(path, fill):
    """동적 dim(dim_param)을 fill[name] 값으로 고정한 뒤 shape inference. 동적 dim 목록도 돌려준다."""
    m = onnx.load(path); dyn = []
    for vi in m.graph.input:
        for d in vi.type.tensor_type.shape.dim:
            if not d.HasField("dim_value"):
                dyn.append(f"{vi.name}:{d.dim_param}"); d.dim_value = fill.get(d.dim_param, 1)
    m = shape_inference.infer_shapes(m)
    shapes = {}
    for vi in list(m.graph.input) + list(m.graph.value_info) + list(m.graph.output):
        shapes[vi.name] = [d.dim_value for d in vi.type.tensor_type.shape.dim]
    inits = {t.name: list(t.dims) for t in m.graph.initializer}
    return m, shapes, inits, dyn

def attr(n, name, default=None):
    for a in n.attribute:
        if a.name == name: return onnx.helper.get_attribute_value(a)
    return default

def gemm_dims(n, shapes, inits):
    """Conv/Gemm/MatMul을 GEMM [M x K] @ [K x N] (groups번)으로 본 치수. 아니면 None."""
    if n.op_type == "Conv":
        w = inits[n.input[1]]; out = shapes[n.output[0]]; g = attr(n, "group", 1)
        return dict(M=out[2] * (out[3] if len(out) > 3 else 1), K=w[1] * math.prod(w[2:]), N=w[0] // g,
                    g=g, cin=w[1] * g, cout=w[0], dw=(g > 1 and g == w[0]))
    if n.op_type in ("Gemm", "MatMul") and n.input[1] in inits:
        w = inits[n.input[1]]; K, N = (w[1], w[0]) if attr(n, "transB", 0) else (w[0], w[1])
        M = math.prod(shapes[n.input[0]][:-1])
        return dict(M=M, K=K, N=N, g=1, cin=K, cout=N, dw=False)
    return None

def macs(d): return d["M"] * d["K"] * d["N"] * d["g"]

UNARY_INPLACE = {"Relu", "Clip", "Sigmoid", "Tanh", "HardSigmoid", "HardSwish", "Erf", "Log", "Exp"}
def live_curve(m, shapes, act_bytes=1):
    """D2식 liveness: 각 노드 실행 시점에 살아 있는 activation 바이트. 단일 소비자 unary op는 in-place."""
    g = m.graph; inits = {t.name for t in g.initializer}
    nbytes = lambda t: math.prod(shapes.get(t, [0])) * act_bytes
    last, consumers = {}, {}
    for i, n in enumerate(g.node):
        for t in n.input:
            if t and t not in inits: last[t] = i; consumers[t] = consumers.get(t, 0) + 1
    for o in g.output: last[o.name] = len(g.node)
    alias = {}                                            # in-place: 출력이 입력 버퍼를 그대로 쓴다
    for n in g.node:
        if n.op_type in UNARY_INPLACE and consumers.get(n.input[0], 0) == 1:
            alias[n.output[0]] = alias.get(n.input[0], n.input[0])
    born = {vi.name: 0 for vi in g.input}
    for i, n in enumerate(g.node):
        for o in n.output: born.setdefault(o, i)
    buf_last = {}
    for t, j in last.items():                            # 버퍼 수명 = alias 사슬 전체의 마지막 사용
        r = alias.get(t, t); buf_last[r] = max(buf_last.get(r, 0), j)
    curve = []
    for i, n in enumerate(g.node):
        live = sum(nbytes(r) for r, j in buf_last.items() if born.get(r, 0) <= i <= j)
        curve.append((i, n.op_type, n.name, live))
    return curve
```

`live_curve`는 D2 5절 추정기의 ONNX판이다. 차이점 두 가지: (1) PyTorch fx 대신 ONNX의 노드 순서(위상 정렬되어 있다)를 실행 순서로 본다, (2) 입력을 하나만 쓰는 unary op(Relu, Clip …)는 **in-place**로 처리해 출력이 입력 버퍼를 재사용한다고 본다 — conv 커널 epilogue에 활성화가 fuse되는 실제 NPU에 더 가깝다. 실행 순서를 바꾸는 스케줄러(C6 11절)나 arena 정렬 손실은 넣지 않았으므로 **하한에 가까운 추정**이다.

### 3.4 규칙 엔진

```python
# i2_lint.py — HW 친화도 linter: ONNX + 타깃 프로파일 → 위반 목록(심각도·추정 비용) + 요약 지표
import math, sys, json
from i2_graph import load_static, gemm_dims, macs, live_curve, attr
from i2_profile import MCU_NPU, SOC_NPU
LAYOUT = {"Transpose", "Reshape", "Concat", "Split", "Slice", "Squeeze", "Unsqueeze", "Expand", "Tile", "Gather"}
CONTROL = {"If", "Loop", "Scan", "NonZero", "Where", "TopK", "Range"}
WIDE_RANGE = {"Log", "Exp", "Pow", "Sqrt", "Reciprocal", "Div"}   # 출력 범위가 넓거나 입력 0 근처에서 폭발
pad = lambda c, a: math.ceil(c / a) * a

def toy_cycles(d, R):          # D4/E5의 weight-stationary 장난감 모델: 타일마다 적재 R + 스트림 M + fill/drain 2R
    return d["g"] * math.ceil(d["K"] / R) * math.ceil(d["N"] / R) * (R + d["M"] + 2 * R)

def lint(path, P, fill={"batch": 1, "time": 98}):
    m, shapes, inits, dyn = load_static(path, fill); g = m.graph; out = []
    nodes = [n for n in g.node if n.op_type != "Constant"]        # Constant는 실행 op가 아니다
    add = lambda sev, rule, msg, cost="": out.append((sev, rule, msg, cost))
    ops = [n.op_type for n in nodes]
    # R1 op whitelist + partition 근사 (선형 순서에서 지원/미지원이 바뀌는 횟수)
    bad = sorted({o for o in ops if o not in P["ops"]})
    seg = [o in P["ops"] for o in ops]; bnd = sum(a != b for a, b in zip(seg, seg[1:]))
    if bad: add("ERROR", "R1 op 지원", f"미지원 {bad}, NPU/CPU 경계 {bnd}회", f"~{bnd * P['switch_us']:.0f} us")
    # R2 활성화: GELU(Erf), SiLU/Swish(Sigmoid→Mul), LUT 경로
    n_erf = ops.count("Erf") + ops.count("Gelu"); n_sig = ops.count("Sigmoid")
    if n_erf: add("ERROR" if "Erf" not in P["ops"] else "WARN", "R2 활성화", f"GELU {n_erf}개 → ReLU6/ReLU 권장")
    if n_sig: add("WARN", "R2 활성화", f"Sigmoid {n_sig}개 (SE·SiLU) — LUT 경로, 범위·정확도 확인")
    # R3 BN이 conv에 접히지 않고 남음
    if "BatchNormalization" in ops:
        add("WARN", "R3 BN fold", f"BatchNormalization {ops.count('BatchNormalization')}개 남음 (conv 바로 뒤가 아님)")
    # R4 동적 shape, R5 rank
    if dyn: add("ERROR", "R4 static shape", f"동적 dim {dyn} (분석은 {fill}로 고정해서 계속)")
    hi = {k: v for k, v in shapes.items() if len(v) > P["max_rank"]}
    if hi: add("ERROR", "R5 rank", f"rank>{P['max_rank']} 텐서 {len(hi)}개")
    # R6 채널 정렬 + R8 depthwise 비중 (MAC과 toy cycle)
    tot = padded = cyc = dw_cyc = dw_mac = 0; mis = []
    for n in nodes:
        d = gemm_dims(n, shapes, inits)
        if not d: continue
        mac = macs(d); tot += mac; c = toy_cycles(d, P["array"]); cyc += c
        if d["dw"]: dw_cyc += c; dw_mac += mac; pm = mac * pad(d["cout"], P["align"]) / d["cout"]
        else: pm = mac * pad(d["cin"], P["align"]) / d["cin"] * pad(d["cout"], P["align"]) / d["cout"]
        if d["cin"] <= 8 or d["cout"] < P["align"]: pm = mac       # 첫 층(입력 1~6ch)·분류 head는 정렬 대상에서 제외
        elif d["cin"] % P["align"] or d["cout"] % P["align"]: mis.append(f"{d['cin']}->{d['cout']}")
        padded += pm
    waste = 1 - tot / padded
    if mis: add("WARN", "R6 채널 정렬", f"{len(mis)}개 층 미정렬 {sorted(set(mis))}", f"MAC 낭비 {waste:.0%}")
    if dw_cyc / cyc > P["dw_cycle_share_max"]:
        add("WARN", "R8 depthwise", f"MAC {dw_mac / tot:.0%}인데 toy cycle {dw_cyc / cyc:.0%}")
    # R7 작은 op 다수: 출력 원소 수가 tiny_elems 미만인 op (마지막 출력 제외)
    outs = {o.name for o in g.output}
    tiny = [n for n in nodes if n.output[0] not in outs and 0 < math.prod(shapes.get(n.output[0], [0])) < P["tiny_elems"]]
    if len(tiny) > 4: add("WARN", "R7 작은 op", f"{len(tiny)}개 op 출력이 {P['tiny_elems']}원소 미만", f"~{len(tiny) * P['per_op_us']:.0f} us 고정비")
    # R9 peak activation vs SRAM 예산
    curve = live_curve(m, shapes, P["act_bytes"]); pk = max(curve, key=lambda r: r[3])
    budget = (P["sram_kb"] - P["reserved_kb"]) * 1024
    if pk[3] > budget: add("ERROR", "R9 peak SRAM", f"peak {pk[3] / 1024:.0f} KB @ {pk[1]} > 예산 {budget // 1024} KB", "앞단 stride·채널 축소")
    # R10 layout churn
    lay = [o for o in ops if o in LAYOUT]
    if "Transpose" in ops or len(lay) > P["layout_max"]: add("WARN", "R10 layout", f"layout op {len(lay)}개 (Transpose {ops.count('Transpose')})")
    # R11 제어 흐름 / 데이터 의존 shape
    cf = sorted({o for o in ops if o in CONTROL})
    if cf: add("ERROR", "R11 control flow", f"{cf}")
    # R12 양자화 위험: 넓은 범위 op, softmax·LayerNorm(분해형 포함)
    wide = [o for o in ops if o in WIDE_RANGE]
    if wide: add("WARN", "R12 양자화", f"넓은 범위 op {sorted(set(wide))} {len(wide)}개 — int8 앞단이면 치명적")
    ln = ops.count("LayerNormalization") + sum(1 for i, o in enumerate(ops) if o == "ReduceMean" and "Pow" in ops[i:i + 3])
    sm = ops.count("Softmax")
    if ln + sm: add("WARN", "R13 norm/softmax", f"LayerNorm {ln}, Softmax {sm} — MCU NPU에서는 CPU 후보")
    # R14 스트리밍: 시간축(마지막 축) 오른쪽 padding = 미래 프레임 lookahead
    look, stride = 0, 1
    for n in nodes:
        if n.op_type == "Conv":
            p, s = attr(n, "pads", [0]), attr(n, "strides", [1])
            look += p[-1] * stride; stride *= s[-1]          # 끝쪽 padding × 지금까지의 누적 stride
    if P["streaming"] and look > P["max_lookahead_frames"]:
        add("WARN", "R14 causal", f"lookahead {look} 프레임 = {look * P['frame_ms']:.0f} ms 지연")
    # R15 가중치(flash)
    wbytes = sum(math.prod(v) for v in inits.values()) * P["weight_bytes"]
    if wbytes > P["flash_kb"] * 1024: add("ERROR", "R15 flash", f"가중치 {wbytes / 1024:.0f} KB > {P['flash_kb']} KB")
    summ = dict(nodes=len(ops), MMAC=round(tot / 1e6, 2), weight_KB=round(wbytes / 1024, 1),
                peak_KB=round(pk[3] / 1024, 1), npu_cpu_boundaries=bnd, lookahead_frames=look,
                toy_Mcycles=round(cyc / 1e6, 3), dw_cycle_share=round(dw_cyc / cyc, 2))
    return out, summ

if __name__ == "__main__":
    P = SOC_NPU if "--soc" in sys.argv else MCU_NPU; nerr = 0
    for f in [a for a in sys.argv[1:] if a.endswith(".onnx")]:
        findings, summ = lint(f, P); nerr += sum(s == "ERROR" for s, *_ in findings)
        print(f"== {f}  [{P['name']}]  {json.dumps(summ, ensure_ascii=False)}")
        for sev, rule, msg, cost in findings: print(f"  {sev:5s} {rule:16s} {msg}" + (f"  [{cost}]" if cost else ""))
    sys.exit(1 if nerr else 0)
```

규칙 하나하나는 짧다. 어려운 부분은 코드가 아니라 **비용 상수**(경계 1회 60 µs, op당 2 µs, 16×16 배열)를 믿을 만하게 만드는 일이고, 그건 기기 실측으로만 된다. 그 전까지 이 숫자들은 "위반의 상대적 크기"를 보여 주는 용도다. 각 규칙의 근사도 정직하게 적어 둔다.

| 규칙 | 이 구현의 근사 | 실제 툴이 더 하는 것 |
|---|---|---|
| R1 경계 수 | ONNX 노드 순서에서 지원/미지원이 바뀌는 횟수 | 실제 partitioner는 순서를 바꿔 같은 장치 노드를 묶기도 한다 (F5 6.2) |
| R6 낭비율 | Cin·Cout을 align 배수로 올린 MAC 비율. 첫 층(Cin ≤ 8)과 head(Cout < align)는 제외 | 칩마다 어느 축을 몇 단위로 묶는지 다르다 (C6 7.5) |
| R8 toy cycle | 타일마다 R + M + 2R 사이클 (D4 3.5, E5 3.7 모델) | depthwise 전용 엔진이 있으면 훨씬 낫다 → waiver 대상 |
| R9 peak | 노드 순서 그대로 + 단일 소비자 unary in-place | 스케줄 최적화, arena 정렬, scratch 버퍼 (D2 4절) |
| R14 lookahead | Conv의 시간축 끝 padding × 누적 stride | Pad 노드, 비대칭 커널, attention의 미래 마스크 |

### 3.5 세 변형에 돌리기 (예제 2)

```sh
.venv/bin/python i2_lint.py naive.onnx partial.onnx fixed.onnx; echo "exit=$?"
```

```text
== naive.onnx  [always-on micro-NPU (Ethos-U55급 가정)]  {"nodes": 108, "MMAC": 41.64, "weight_KB": 44.8, "peak_KB": 918.8, "npu_cpu_boundaries": 40, "lookahead_frames": 7, "toy_Mcycles": 0.667, "dw_cycle_share": 0.73}
  ERROR R1 op 지원         미지원 ['BatchNormalization', 'Div', 'Erf', 'LayerNormalization', 'Log', 'Sub', 'Transpose'], NPU/CPU 경계 40회  [~2400 us]
  ERROR R2 활성화           GELU 9개 → ReLU6/ReLU 권장
  WARN  R2 활성화           Sigmoid 4개 (SE·SiLU) — LUT 경로, 범위·정확도 확인
  WARN  R3 BN fold       BatchNormalization 9개 남음 (conv 바로 뒤가 아님)
  ERROR R4 static shape  동적 dim ['x:batch', 'x:time'] (분석은 {'batch': 1, 'time': 98}로 고정해서 계속)
  WARN  R6 채널 정렬         15개 층 미정렬 ['15->60', '22->90', '60->60', '60->90', '90->22', '90->90']  [MAC 낭비 11%]
  WARN  R8 depthwise     MAC 10%인데 toy cycle 73%
  WARN  R7 작은 op         31개 op 출력이 512원소 미만  [~62 us 고정비]
  ERROR R9 peak SRAM     peak 919 KB @ Add > 예산 384 KB  [앞단 stride·채널 축소]
  WARN  R10 layout       layout op 10개 (Transpose 1)
  WARN  R12 양자화          넓은 범위 op ['Div', 'Log'] 11개 — int8 앞단이면 치명적
  WARN  R13 norm/softmax LayerNorm 1, Softmax 1 — MCU NPU에서는 CPU 후보
== partial.onnx  [always-on micro-NPU (Ethos-U55급 가정)]  {"nodes": 23, "MMAC": 41.63, "weight_KB": 28.9, "peak_KB": 689.1, "npu_cpu_boundaries": 0, "lookahead_frames": 7, "toy_Mcycles": 0.663, "dw_cycle_share": 0.73}
  WARN  R6 채널 정렬         8개 층 미정렬 ['60->60', '60->90', '90->90']  [MAC 낭비 11%]
  WARN  R8 depthwise     MAC 10%인데 toy cycle 73%
  ERROR R9 peak SRAM     peak 689 KB @ Conv > 예산 384 KB  [앞단 stride·채널 축소]
== fixed.onnx  [always-on micro-NPU (Ethos-U55급 가정)]  {"nodes": 23, "MMAC": 11.86, "weight_KB": 32.6, "peak_KB": 183.8, "npu_cpu_boundaries": 0, "lookahead_frames": 13, "toy_Mcycles": 0.192, "dw_cycle_share": 0.74}
  WARN  R8 depthwise     MAC 10%인데 toy cycle 74%
exit=1
```

출력에서 볼 것:

- **naive**: 노드 108개, ERROR 4개, WARN 8개. 미지원 op 7종이 그래프 곳곳에 흩어져 NPU/CPU 경계가 40번 생긴다. 경계 1회 60 µs라는 가정이면 경계 비용만 2.4 ms — 모델 계산보다 클 수 있다. `Div·Sub·Log·Transpose`는 그래프 안 특징 처리에서, `Erf·Div`는 GELU에서, `LayerNormalization`은 head에서, `BatchNormalization`은 "활성화 뒤 BN"에서 왔다. 옛 exporter는 conv 바로 뒤가 아닌 BN을 접지 못했다.
- **partial**: 노드 23개, 경계 0. op 문제는 사라졌지만 **peak 689 KB > 예산 384 KB**라는 ERROR 하나가 남았다. op만 고쳐서는 기기에 못 올린다.
- **fixed**: ERROR 없음, WARN 하나(R8). MAC은 41.6 M → 11.9 M, peak은 919 KB → 184 KB. 남은 R8은 "이 toy 모델에서는 depthwise가 사이클의 74%"라는 뜻인데, 실제 칩의 depthwise 경로를 확인한 뒤 **waiver**(예외 승인)로 닫을 항목이다 (11.2절).
- 요약 줄의 `lookahead_frames`(7, 13)는 KWS처럼 1초 창 전체를 보고 판정하는 모델에는 문제가 아니다. 그래서 이 프로파일(`streaming=False`)에서는 경고하지 않는다. 스트리밍 모델이면 경고한다 (7.3절).

같은 모델을 앱 SoC NPU 프로파일로 돌리면 결과가 달라진다 — **규칙은 타깃의 함수**라는 점을 보여 준다.

```sh
.venv/bin/python i2_lint.py --soc naive.onnx fixed.onnx | grep -E "^==|R1 |R2 |R8 |R9 "
```

```text
== naive.onnx  [app SoC NPU (~10 TOPS int8 가정)]  {"nodes": 108, "MMAC": 41.64, "weight_KB": 44.8, "peak_KB": 918.8, "npu_cpu_boundaries": 20, "lookahead_frames": 7, "toy_Mcycles": 0.551, "dw_cycle_share": 0.96}
  ERROR R1 op 지원         미지원 ['BatchNormalization', 'Log'], NPU/CPU 경계 20회  [~3000 us]
  WARN  R2 활성화           GELU 9개 → ReLU6/ReLU 권장
  WARN  R2 활성화           Sigmoid 4개 (SE·SiLU) — LUT 경로, 범위·정확도 확인
  WARN  R8 depthwise     MAC 10%인데 toy cycle 96%
== fixed.onnx  [app SoC NPU (~10 TOPS int8 가정)]  {"nodes": 23, "MMAC": 11.86, "weight_KB": 32.6, "peak_KB": 183.8, "npu_cpu_boundaries": 0, "lookahead_frames": 13, "toy_Mcycles": 0.195, "dw_cycle_share": 0.96}
  WARN  R8 depthwise     MAC 10%인데 toy cycle 96%
```

(grep으로 R1·R2·R8·R9 줄만 남겼다. R9 줄이 없다는 것은 SoC 프로파일에서 peak가 예산 안이라는 뜻이다.)

출력에서 볼 것: SoC NPU 프로파일은 GELU·LayerNorm·Transpose를 지원한다고 가정했으므로 R1의 미지원 목록이 2종으로 줄고 R2는 WARN으로 내려간다. SRAM(4 MB 가정)이 넉넉해서 R9는 사라진다. 반대로 **배열이 64×64로 커지자 depthwise의 toy cycle 비중은 73% → 96%로 나빠졌다**. 큰 배열일수록 K=9, N=1짜리 depthwise를 채우지 못한다 (E5 3.8 "배열 크기의 딜레마"). 같은 모델이 MCU에서는 SRAM이, SoC에서는 depthwise가 문제다. 그래서 규칙집은 하나가 아니라 **타깃별 프로파일 + 공통 규칙**으로 관리한다.

---

## 4. 연산 규칙 — R1 지원 op, R2 활성화, R3 BN fold

### 4.1 R1 — 지원 op만 쓴다

**HW 이유**: NPU는 고정 기능 하드웨어다. 컴파일러는 지원 op를 NPU 명령열로 바꾸고, 나머지는 CPU에 남긴다(fallback). 경계를 넘을 때마다 NPU 완료 대기·인터럽트, 캐시 clean/invalidate, 레이아웃 변환이나 복사, CPU 커널 실행, 다시 NPU kick이 필요하다. op 자체보다 **경계 비용**이 크다. E5 예제 9의 시뮬레이션에서는 HardSwish 하나를 NPU로 옮겨 경계 두 번을 없애자 0.449 ms → 0.257 ms(43% 단축)가 됐고, Edge TPU식 "첫 미지원 op 이후 전부 CPU" 정책이면 1.199 ms까지 나빠졌다.

**측정 근거 (이 노트)**: 예제 2에서 naive는 경계 40회. 10절 예제 12에서 CoreML EP는 naive를 **CoreML 17조각 + CPU 21노드**로 쪼갰고, CPU만 쓸 때보다 1.9배 느렸다.

**자동 검사**: whitelist 대조 + 경계 수 (R1). 실제 배포 전에는 벤더 컴파일러의 리포트(Vela의 CPU fallback op 목록, QNN의 지원 검사)와 `disable_cpu_ep_fallback` 같은 강제 장치로 확정한다 (F5 6.5, F8).

**모델팀에게 하는 말**: "whitelist에 없는 op는 쓰기 전에 물어봐 달라. 꼭 필요하면 (1) 지원 op 조합으로 다시 쓰거나(C6 8절), (2) 그래프 끝으로 몰아서 경계를 한 번만 넘게 하자."

### 4.2 R2 — ReLU/ReLU6 우선

**HW 이유 두 가지** (메커니즘은 B1 3~4절):

1. **op 지원**: int8 NPU에서 ReLU·ReLU6는 requantization 뒤의 clamp 한 줄이라 사실상 공짜다. GELU는 export되면 Erf·Div·Add·Mul로 분해되고, Erf는 많은 MCU급 NPU가 모른다. SiLU는 Sigmoid + Mul로, Sigmoid는 LUT 경로다.
2. **양자화 범위**: ReLU6의 출력은 [0, 6]으로 고정이라 scale을 6/255로 미리 정할 수 있다. ReLU·GELU·SiLU는 위쪽이 열려 있어, 드문 큰 값 하나가 per-tensor scale을 키우고 나머지 값의 해상도를 망친다.

예제 3 — 1×1 conv(64ch, 20×49) 뒤에 활성화 하나만 바꿔 붙여, (a) ONNX에 어떤 op로 나오는지, (b) 가상 MCU whitelist를 통과하는지, (c) ORT CPU 지연, (d) 꼬리가 두꺼운 pre-activation(Student-t, 자유도 3)에 활성화를 적용한 출력을 per-tensor min-max int8로 양자화했을 때의 SQNR을 본다.

```python
# ex_act.py — 활성화 하나만 바꾼 Conv 블록: ONNX에 무엇으로 나오나, CPU 지연, int8(min-max) 출력 SQNR
import torch, torch.nn as nn, numpy as np, onnx, io, warnings
from i2_bench import ort_ms
from i2_profile import MCU_NPU
warnings.filterwarnings("ignore"); torch.manual_seed(0)
acts = {"ReLU": nn.ReLU(), "ReLU6": nn.ReLU6(), "HardSwish": nn.Hardswish(), "SiLU": nn.SiLU(),
        "GELU": nn.GELU(), "GELU-tanh": nn.GELU("tanh")}
pre = torch.distributions.StudentT(3.0).sample((200000,)) * 1.5      # 꼬리가 두꺼운 pre-activation (outlier 포함)
def sqnr_int8(y):                                                    # per-tensor 비대칭 min-max int8
    s = (y.max() - y.min()) / 255; z = torch.round(-y.min() / s) - 128
    q = (torch.clamp(torch.round(y / s) + z, -128, 127) - z) * s
    return 10 * torch.log10((y ** 2).sum() / ((y - q) ** 2).sum()).item()
x = torch.randn(1, 64, 20, 49)
for name, a in acts.items():
    m = nn.Sequential(nn.Conv2d(64, 64, 1), a).eval(); f = io.BytesIO()
    torch.onnx.export(m, (x,), f, opset_version=17, dynamo=False, input_names=["x"])
    g = onnx.load_from_string(f.getvalue()).graph; ops = [n.op_type for n in g.node if n.op_type != "Constant"]
    ok = all(o in MCU_NPU["ops"] for o in ops)
    ms = ort_ms(onnx.load_from_string(f.getvalue()), {"x": x.numpy()}, n=300)
    y = a(pre)
    print(f"{name:9s} ops={'+'.join(ops):22s} NPU={'OK ' if ok else 'NO '} {ms:6.3f} ms "
          f"range=[{y.min().item():6.2f},{y.max().item():7.2f}] int8 SQNR={sqnr_int8(y):5.1f} dB")
```

`i2_bench.py`(지연 측정 공용 함수)는 5.1절에 있다.

```text
ReLU      ops=Conv+Relu              NPU=OK   0.197 ms range=[  0.00, 167.49] int8 SQNR= 22.7 dB
ReLU6     ops=Conv+Clip              NPU=OK   0.187 ms range=[  0.00,   6.00] int8 SQNR= 49.9 dB
HardSwish ops=Conv+HardSwish         NPU=NO   0.213 ms range=[ -0.38, 167.49] int8 SQNR= 18.6 dB
SiLU      ops=Conv+Sigmoid+Mul       NPU=OK   0.273 ms range=[ -0.28, 167.49] int8 SQNR= 19.2 dB
GELU      ops=Conv+Div+Erf+Add+Mul+Mul NPU=NO   0.432 ms range=[ -0.17, 167.49] int8 SQNR= 21.5 dB
GELU-tanh ops=Conv+Mul+Mul+Mul+Add+Mul+Tanh+Add+Mul+Mul NPU=OK   0.354 ms range=[ -0.17, 167.49] int8 SQNR= 21.5 dB
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 290">
<text x="20" y="24" font-size="14">활성화별 int8(min-max) 출력 SQNR (dB) · 오른쪽: ONNX에서 활성화가 몇 개 op로 나오나</text> <text x="20" y="61" font-size="13">ReLU</text> <rect x="110" y="44" width="136.2" height="24" fill="#4a7bd0" fill-opacity="0.8"/> <text x="252.2" y="61" font-size="12">22.7</text> <rect x="520" y="48" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/> <text x="20" y="97" font-size="13">ReLU6</text> <rect x="110" y="80" width="299.4" height="24" fill="#3f9a6b" fill-opacity="0.8"/> <text x="415.4" y="97" font-size="12">49.9</text> <rect x="520" y="84" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/> <text x="20" y="133" font-size="13">HardSwish</text> <rect x="110" y="116" width="111.6" height="24" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="227.6" y="133" font-size="12">18.6</text> <rect x="520" y="120" width="12" height="16" fill="#d0564a" fill-opacity="0.85"/> <text x="20" y="169" font-size="13">SiLU</text> <rect x="110" y="152" width="115.2" height="24" fill="#4a7bd0" fill-opacity="0.8"/> <text x="231.2" y="169" font-size="12">19.2</text> <rect x="520" y="156" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/> <rect x="535" y="156" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/> <text x="20" y="205" font-size="13">GELU</text> <rect x="110" y="188" width="129.0" height="24" fill="#4a7bd0" fill-opacity="0.8"/> <text x="245.0" y="205" font-size="12">21.5</text> <rect x="520" y="192" width="12" height="16" fill="#d0564a" fill-opacity="0.85"/>
<rect x="535" y="192" width="12" height="16" fill="#d0564a" fill-opacity="0.85"/> <rect x="550" y="192" width="12" height="16" fill="#d0564a" fill-opacity="0.85"/> <rect x="565" y="192" width="12" height="16" fill="#d0564a" fill-opacity="0.85"/> <rect x="580" y="192" width="12" height="16" fill="#d0564a" fill-opacity="0.85"/> <text x="20" y="241" font-size="13">GELU-tanh</text> <rect x="110" y="224" width="129.0" height="24" fill="#4a7bd0" fill-opacity="0.8"/> <text x="245.0" y="241" font-size="12">21.5</text> <rect x="520" y="228" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/> <rect x="535" y="228" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/> <rect x="550" y="228" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="565" y="228" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/> <rect x="580" y="228" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/> <rect x="595" y="228" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/> <rect x="610" y="228" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/> <rect x="625" y="228" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/> <rect x="640" y="228" width="12" height="16" fill="#e08a3c" fill-opacity="0.85"/> <line x1="110" y1="40" x2="110" y2="262" stroke="currentColor"/> <line x1="510" y1="40" x2="510" y2="262" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="20" y="282" font-size="12">주황 칸 = 가상 MCU 프로파일 whitelist 안의 op, 빨강 칸 = whitelist 밖 op가 섞인 경우 (칸 수 = op 수)</text></svg>
```

그림 3 — 예제 3의 결과. 왼쪽 막대: int8 출력 SQNR. 오른쪽 칸: 활성화가 ONNX에서 몇 개 op로 나오는지(빨강 = whitelist 밖 op 포함).

출력에서 볼 것:

- **SQNR**: ReLU6만 49.9 dB이고 나머지는 19~23 dB다. 차이는 범위다. pre-activation에 outlier(최대 167)가 있으면, 열린 활성화는 scale = 167/255 ≈ 0.66이 되어 0~2 근처 대부분의 값이 몇 개의 눈금에 뭉친다. ReLU6는 6에서 잘라서 scale이 0.024다. 엄밀히는 ReLU6가 **다른 함수**를 계산하는 것이므로(6 위를 버린다), 학습 때부터 ReLU6로 학습해야 이 이득이 정확도 손실 없이 온다. 그래서 "배포 직전에 바꾸기"가 아니라 "**설계 규칙**"이다.
- **op 수와 지원**: GELU(erf)는 5개 op로 분해되고 Erf가 whitelist 밖이다. tanh 근사 GELU는 9개 op로 늘어나지만 전부 whitelist 안이다 — 지원은 되지만 원소별 op 9개가 각각 텐서 전체를 읽고 쓴다(fusion이 안 되면). HardSwish는 이 가상 프로파일에서는 미지원으로 뒀다. 실제 칩은 HardSwish를 지원하는 경우도 많으니 프로파일로 확인한다.
- **CPU 지연**: GELU 블록이 ReLU6 블록의 2.3배다 (부하 때문에 절대값은 실행마다 30% 이상 흔들렸지만 순서는 같았다). MAC이 같은데도 활성화 하나가 conv보다 비쌀 수 있다.

**int8 NPU에서의 비용 관점**: int8 입력 → int8 출력인 원소별 함수는 무엇이든 **256-entry LUT 하나**로 만들 수 있다. 그래서 "GELU 자체가 int8에서 비싸다"기보다 (1) 컴파일러가 그 패턴을 LUT로 인식해 주느냐(분해된 Erf 그래프를 다시 묶어야 한다), (2) 범위가 열려 있어 scale이 나빠지느냐가 문제다. 예제 4가 이 차이를 C로 보여 준다.

예제 4 — int8 conv 출력 epilogue에서 ReLU6와 GELU가 각각 무엇을 요구하는지 C로 확인한다.

```c
// act_epilogue.c — int8 conv 출력 epilogue: ReLU6는 requant 뒤 clamp 범위만 바꾸면 끝, GELU는 256-entry LUT가 하나 더 필요하다
#include <stdio.h>
#include <stdint.h>
#include <math.h>
static int8_t sat8(int32_t v) { return (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v); }
int main(void) {
    const float s_out = 6.0f / 255.0f; const int zp = -128;          // ReLU6 출력: [0,6] → [-128,127]
    const int32_t qlo = zp, qhi = zp + (int32_t)lrintf(6.0f / s_out); // clamp 경계 = 정수 두 개
    const float s_acc = 0.01f;                                         // int32 누산값 → 실수 배율 (가정)
    int32_t acc[5] = {-300, 0, 150, 450, 900};
    printf("ReLU6 epilogue (clamp [%d,%d]):", (int)qlo, (int)qhi);
    for (int i = 0; i < 5; i++) {
        int32_t q = (int32_t)lrintf(acc[i] * s_acc / s_out) + zp;     // requant (실제로는 M0·shift 정수 곱)
        q = q < qlo ? qlo : q > qhi ? qhi : q;                          // 이것이 ReLU6의 전부
        printf(" %d", (int)q);
    }
    const float s_in = 0.05f, s_g = 0.05f; int8_t lut[256];           // GELU: 입력 int8 → 출력 int8 LUT
    for (int q = -128; q < 128; q++) {
        float x = q * s_in, y = 0.5f * x * (1.0f + erff(x / sqrtf(2.0f)));
        lut[q + 128] = sat8((int32_t)lrintf(y / s_g));
    }
    printf("\nGELU LUT 256 B, 예: q=-40(x=-2.0)->%d  q=0->%d  q=40(x=2.0)->%d  q=127->%d\n",
           lut[-40 + 128], lut[128], lut[40 + 128], lut[255]);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 act_epilogue.c -o act_epilogue -lm && ./act_epilogue
```

```text
ReLU6 epilogue (clamp [-128,127]): -128 -128 -64 63 127
GELU LUT 256 B, 예: q=-40(x=-2.0)->-1  q=0->0  q=40(x=2.0)->39  q=127->127
```

출력에서 볼 것: ReLU6는 출력 scale을 6/255, zero-point를 −128로 잡는 순간 **int8 포화 범위 [−128, 127]이 곧 [0, 6]**이 된다. 즉 ReLU6는 추가 명령어가 0개다 (요구 사항은 "출력 scale을 이렇게 잡아라"뿐). 손으로 확인: 누산값 150 × 0.01 = 1.5, 1.5 / (6/255) = 63.75 → 64, 64 − 128 = −64. GELU는 LUT 256 B와 원소당 메모리 읽기 1회가 더 든다. 그리고 LUT는 입력·출력 scale 쌍마다 하나라 **층마다 따로** 만들어야 한다. 큰 문제는 아니지만, NPU 하드웨어에 LUT 유닛이 있는지·컴파일러가 이 패턴을 잡는지는 벤더에게 물어야 하는 항목이다.

### 4.3 R3 — BN은 conv 바로 뒤에

**HW 이유**: conv → BN이면 BN은 오프라인에서 conv의 weight·bias로 접혀 **비용이 0**이 된다 (B1 7절의 `W' = γ/σ · W`, `b' = γ/σ · (b − μ) + β`). conv → 활성화 → BN이면 사이에 비선형이 끼어 접을 수 없다. 남은 BN은 채널별 곱·더하기 두 번이고, fusion되지 않으면 텐서 전체를 한 번 더 읽고 쓴다.

**측정 근거**: 예제 2에서 naive(활성화 뒤 BN)는 BatchNormalization 9개가 그래프에 남았고, 이게 micro-NPU 프로파일의 미지원 op 목록에 들어갔다. partial·fixed는 옛 exporter가 conv 바로 뒤 BN을 자동으로 접어서 BN이 0개다 (ops_used에 Conv·Clip만 보인다, 11.1절).

**자동 검사**: BatchNormalization 노드가 남아 있으면 WARN. "접을 수 있었는데 export 설정 때문에 안 접힌" 경우(학습 모드 export 등, A5)도 같은 증상이니 함께 잡힌다.

---

## 5. 모양 규칙 — R4 static shape, R5 rank, R6 채널 정렬, R7 작은 op, R8 depthwise

### 5.1 측정 도구

이 절부터 쓰는 지연 측정 함수. ORT CPU EP, 단일 스레드, warm-up 20회 뒤 N회씩 3세트를 재서 세트별 중앙값의 중앙값을 ms로 돌려준다.

```python
# i2_bench.py — ORT CPU 지연 측정 (단일 스레드, 반복 3세트 × N회, 세트별 중앙값의 중앙값)
import time, numpy as np, onnxruntime as ort
def ort_ms(model_or_path, feeds, n=200, threads=1, providers=("CPUExecutionProvider",)):
    so = ort.SessionOptions(); so.intra_op_num_threads = threads; so.log_severity_level = 3
    src = model_or_path if isinstance(model_or_path, str) else model_or_path.SerializeToString()
    s = ort.InferenceSession(src, so, providers=list(providers))
    for _ in range(20): s.run(None, feeds)
    meds = []
    for _ in range(3):
        t = []
        for _ in range(n):
            t0 = time.perf_counter(); s.run(None, feeds); t.append(time.perf_counter() - t0)
        meds.append(np.median(t))
    return float(np.median(meds)) * 1e3
```

### 5.2 R4·R5 — static shape, rank ≤ 4

**HW 이유**: NPU 컴파일러는 컴파일 시점에 텐서를 타일로 자르고, SRAM 주소를 정하고, DMA 디스크립터를 미리 만든다. 크기를 모르면 셋 다 못 한다 (E5 6.3, C6 9절). 하드웨어 텐서 디스크립터도 대개 4D(NHWC)까지라, 5D 텐서(예: 비디오 N·T·C·H·W, attention의 head 축 분리)는 reshape으로 4D로 접어야 한다.

**측정 근거**: F5 예제 6에서 동적 batch export와 "batch 1 특수화" 함정을 봤다. 이 노트에서는 10절 예제 12에서 동적 입력을 가진 naive가 CoreML EP에서 17조각으로 쪼개졌다 (op 미지원과 섞여 있어 동적 shape만의 효과로 분리하지는 못했다).

**자동 검사**: 그래프 입력의 `dim_param`(이름 붙은 미지수 차원) 검사, 모든 텐서의 rank 검사. linter는 동적 dim을 기본값(batch 1, time 98)으로 고정한 뒤 나머지 분석을 계속한다 — ERROR가 있어도 다른 규칙의 리포트는 받아야 하기 때문이다.

**모델팀에게 하는 말**: "가변 길이 입력은 고정 창(예: 1초)으로 자르고 패딩하라. 길이가 다른 입력이 정말 필요하면 길이별로 2~3개 버전을 따로 export하자 (bucket)."

### 5.3 R6 — 채널은 16(또는 32)의 배수

**HW 이유**: MAC 배열과 SIMD는 채널을 몇 개씩 묶어 한 번에 처리하고, 남는 칸은 0으로 채운다. Arm Ethos-U의 Vela는 내부적으로 16채널 단위 brick 형식(NHCWB16)을 쓴다고 문서화돼 있다 (C6 7.5). 1×1 conv에서는 입력·출력 채널이 둘 다 패딩되므로 낭비가 곱으로 쌓인다.

```
 padded(C) = ⌈C / A⌉ · A          (A = 정렬 단위, 예: 16)
 1×1 conv MAC 낭비 = 1 − (Cin · Cout) / (padded(Cin) · padded(Cout))

 손계산: 60 → 60, A = 16  →  padded = 64,  낭비 = 1 − 3600/4096 = 12.1 %
         90 → 90, A = 16  →  padded = 96,  낭비 = 1 − 8100/9216 = 12.1 %
         65 → 65, A = 16  →  padded = 80,  낭비 = 1 − 4225/6400 = 34.0 %
```

말로 하면: 채널을 정렬 단위보다 하나만 넘겨도 다음 단위까지 통째로 계산한다. 64는 낭비 0%, 65는 34%다.

**측정 근거**: C7 예제 2(PyTorch fp32 CPU)에서 64 → 65 채널에 latency가 22% 뛰었다. 여기서는 배포 dtype에 가까운 **int8** 1×1 conv(ORT `QLinearConv`)로 다시 잰다.

예제 5 — int8 1×1 conv(C → C, 20×49)의 채널 수를 바꿔 가며 ORT CPU 지연을 잰다.

```python
# ex_cliff8.py — 같은 1x1 conv를 int8(QLinearConv)로: ORT CPU 지연 vs 채널 수
import numpy as np, math
from onnx import helper as h, TensorProto as TP, numpy_helper as nh
from i2_bench import ort_ms
def qconv(C, H=20, W=49):
    rng = np.random.default_rng(0); I = lambda n, a: nh.from_array(np.array(a), n)
    inits = [I("xs", np.float32(0.05)), I("xz", np.int8(0)), nh.from_array(rng.integers(-127, 127, (C, C, 1, 1)).astype(np.int8), "w"),
             I("ws", np.float32(0.01)), I("wz", np.int8(0)), I("ys", np.float32(0.1)), I("yz", np.int8(0))]
    n = h.make_node("QLinearConv", ["x", "xs", "xz", "w", "ws", "wz", "ys", "yz"], ["y"])
    g = h.make_graph([n], "g", [h.make_tensor_value_info("x", TP.INT8, [1, C, H, W])],
                     [h.make_tensor_value_info("y", TP.INT8, [1, C, H, W])], inits)
    return h.make_model(g, opset_imports=[h.make_opsetid("", 17)], ir_version=8)
print(f"{'C':>3s} {'int8 ms':>8s} {'GMAC/s':>7s}")
for C in [48, 56, 60, 63, 64, 65, 72, 80, 90, 96, 97]:
    x = np.random.default_rng(1).integers(-100, 100, (1, C, 20, 49)).astype(np.int8)
    ms = ort_ms(qconv(C), {"x": x}, n=300)
    print(f"{C:3d} {ms:8.4f} {C * C * 980 / ms / 1e6:7.1f}")
```

```text
  C  int8 ms  GMAC/s
 48   0.0731    30.9
 56   0.1033    29.8
 60   0.1134    31.1
 63   0.1278    30.4
 64   0.1165    34.5
 65   0.1458    28.4
 72   0.1538    33.0
 80   0.1683    37.3
 90   0.2448    32.4
 96   0.2482    36.4
 97   0.2720    33.9
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 340">
<text x="20" y="24" font-size="14">int8 1×1 conv (C→C, 20×49) ORT CPU 지연 — 점: 실측, 점선: MAC에 비례한 기대값</text> <line x1="70" y1="280" x2="640" y2="280" stroke="currentColor"/> <line x1="70" y1="280" x2="70" y2="50" stroke="currentColor"/> <line x1="110.7" y1="50" x2="110.7" y2="280" stroke="#3f9a6b" stroke-dasharray="3 3"/> <line x1="273.6" y1="50" x2="273.6" y2="280" stroke="#3f9a6b" stroke-dasharray="3 3"/> <line x1="436.4" y1="50" x2="436.4" y2="280" stroke="#3f9a6b" stroke-dasharray="3 3"/> <line x1="599.3" y1="50" x2="599.3" y2="280" stroke="#3f9a6b" stroke-dasharray="3 3"/> <text x="62" y="207.3" font-size="12" text-anchor="end">0.1</text> <line x1="66" y1="203.3" x2="70" y2="203.3" stroke="currentColor"/>
<text x="62" y="130.7" font-size="12" text-anchor="end">0.2</text> <line x1="66" y1="126.7" x2="70" y2="126.7" stroke="currentColor"/> <text x="62" y="54.0" font-size="12" text-anchor="end">0.3</text> <line x1="66" y1="50.0" x2="70" y2="50.0" stroke="currentColor"/> <text x="110.7" y="298" font-size="12" text-anchor="middle">48</text> <text x="192.1" y="298" font-size="12" text-anchor="middle">56</text> <text x="273.6" y="298" font-size="12" text-anchor="middle">64</text> <text x="355.0" y="298" font-size="12" text-anchor="middle">72</text> <text x="436.4" y="298" font-size="12" text-anchor="middle">80</text> <text x="517.9" y="298" font-size="12" text-anchor="middle">88</text> <text x="599.3" y="298" font-size="12" text-anchor="middle">96</text>
<polyline points="90.4,233.9 110.7,229.8 131.1,225.5 151.4,221.0 171.8,216.4 192.1,211.6 212.5,206.6 232.9,201.5 253.2,196.2 273.6,190.7 293.9,185.0 314.3,179.2 334.6,173.2 355.0,167.0 375.4,160.6 395.7,154.0 416.1,147.3 436.4,140.4 456.8,133.4 477.1,126.1 497.5,118.7 517.9,111.1 538.2,103.4 558.6,95.4 578.9,87.3 599.3,79.0 619.6,70.6" fill="none" stroke="#888" stroke-dasharray="5 4"/> <polyline points="110.7,224.0 192.1,200.8 232.9,193.1 263.4,182.0 273.6,190.7 283.8,168.2 355.0,162.1 436.4,151.0 538.2,92.3 599.3,89.7 609.5,71.5" fill="none" stroke="#4a7bd0" stroke-width="1.5"/> <circle cx="110.7" cy="224.0" r="4" fill="#4a7bd0"/> <circle cx="192.1" cy="200.8" r="4" fill="#4a7bd0"/> <circle cx="232.9" cy="193.1" r="4" fill="#4a7bd0"/>
<circle cx="263.4" cy="182.0" r="4" fill="#d0564a"/> <circle cx="273.6" cy="190.7" r="4" fill="#4a7bd0"/> <circle cx="283.8" cy="168.2" r="4" fill="#d0564a"/> <circle cx="355.0" cy="162.1" r="4" fill="#4a7bd0"/> <circle cx="436.4" cy="151.0" r="4" fill="#4a7bd0"/> <circle cx="538.2" cy="92.3" r="4" fill="#4a7bd0"/> <circle cx="599.3" cy="89.7" r="4" fill="#4a7bd0"/> <circle cx="609.5" cy="71.5" r="4" fill="#d0564a"/> <text x="291.8" y="172.2" font-size="12">65: +25%</text> <text x="603.5" y="63.5" font-size="12" text-anchor="end">97: +10%</text> <text x="257.4" y="174.0" font-size="12" text-anchor="end">63 &gt; 64</text>
<text x="355.0" y="320" font-size="12" text-anchor="middle">채널 수 C (초록 점선 = 16의 배수) · 세로: ms</text></svg>
```

그림 4 — 예제 5의 실측(파랑 점)과 "MAC에 비례"했을 때의 기대 곡선(회색 점선, C = 64에 맞춤). 빨강 점이 계단 지점이다.

출력에서 볼 것:

- **63채널이 64채널보다 느리다** (0.1278 vs 0.1165 ms). MAC은 3% 적은데 시간은 10% 더 든다.
- **64 → 65에서 +25%** (MAC +3%). 96 → 97에서 +10%. 90채널은 96채널과 시간이 거의 같다 — 90을 쓰면 96의 값을 내고 90만큼 일한다.
- 같은 실험을 fp32 `Conv`로 하면 이 CPU에서는 계단이 거의 보이지 않았다 (모든 C에서 약 32 GMAC/s로 평평). fp32 커널은 4~8 단위로 정렬하므로 계단이 촘촘하고 작다. int8 커널은 더 큰 단위로 묶어서 계단이 커진다. NPU는 16·32 단위라 더 크다 — 그쪽은 CPU로 잴 수 없으니 위의 패딩 낭비 산수와 벤더 성능 모델로 본다.
- 세 번 반복해 봤고, 63 > 64, 65의 +25%, 97의 +10~18%는 매번 재현됐다 (부하가 높아서 개별 값은 몇 % 흔들렸다).

**자동 검사**: 예제 2의 R6 줄. partial의 60·90채널 층 8개를 찾아 "MAC 낭비 11%"를 보고했다 (첫 층 1→60과 분류 head 90→5는 제외). fixed는 64·96으로 맞춰 R6가 사라졌다.

**모델팀에게 하는 말**: "채널 수는 `make_divisible(c, 16)`으로 반올림하라 (MobileNet 계열 코드에 이미 있는 함수와 같은 일). 첫 층과 분류 head는 예외다."

### 5.4 R7 — 작은 op를 많이 만들지 말 것

**HW 이유**: op 하나를 실행할 때마다 고정비가 든다. CPU 런타임은 커널 디스패치와 스레드 동기화, NPU는 명령열 디코드·DMA 디스크립터 설정·파이프라인 fill/drain, delegate는 호출 오버헤드. 작은 op 여러 개는 이 고정비를 여러 번 낸다. 그리고 층이 많으면 중간 텐서가 메모리를 왕복하는 횟수도 늘어난다.

예제 6 — MAC이 똑같이 16.06 M인 1×1 conv + ReLU 스택을 층 수 L과 폭 C만 바꿔 만든다 (L × C² = 16384 고정, 20×49).

```python
# ex_tiny.py — MAC이 같은(16.06 MMAC) 1x1 conv+ReLU 스택: 층 수 L, 폭 C를 바꾸면 ORT CPU 지연은?
import numpy as np, onnx
from onnx import helper as h, TensorProto as TP, numpy_helper as nh
from i2_bench import ort_ms
def stack(L, C, H=20, W=49):
    nodes, inits, prev, rng = [], [], "x", np.random.default_rng(0)
    for i in range(L):
        w = (rng.standard_normal((C, C, 1, 1)) / np.sqrt(C)).astype(np.float32)
        inits.append(nh.from_array(w, f"w{i}"))
        nodes += [h.make_node("Conv", [prev, f"w{i}"], [f"c{i}"]), h.make_node("Relu", [f"c{i}"], [f"r{i}"])]
        prev = f"r{i}"
    g = h.make_graph(nodes, "g", [h.make_tensor_value_info("x", TP.FLOAT, [1, C, H, W])],
                     [h.make_tensor_value_info(prev, TP.FLOAT, [1, C, H, W])], inits)
    return h.make_model(g, opset_imports=[h.make_opsetid("", 17)], ir_version=8)
rows = []
for L, C in [(64, 16), (16, 32), (4, 64), (1, 128)]:
    x = np.random.default_rng(1).standard_normal((1, C, 20, 49)).astype(np.float32)
    ms = ort_ms(stack(L, C), {"x": x}, n=200); rows.append((L, ms))
    print(f"L={L:2d} x C={C:3d}  MAC={L * C * C * 980 / 1e6:5.2f} M  {ms:7.3f} ms  {ms / L * 1e3:6.1f} us/layer")
L, t = np.array(rows).T; a, b = np.polyfit(L, t, 1)
print(f"직선 맞춤: latency ≈ {b:.3f} ms + {a * 1e3:.1f} us × 층 수")
```

```text
L=64 x C= 16  MAC=16.06 M    0.895 ms    14.0 us/layer
L=16 x C= 32  MAC=16.06 M    0.633 ms    39.6 us/layer
L= 4 x C= 64  MAC=16.06 M    0.515 ms   128.7 us/layer
L= 1 x C=128  MAC=16.06 M    0.470 ms   470.3 us/layer
직선 맞춤: latency ≈ 0.491 ms + 6.4 us × 층 수
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 230">
<text x="20" y="24" font-size="14">MAC이 똑같이 16.06 M인 1×1 conv+ReLU 스택 — ORT CPU 지연 (ms)</text> <text x="20" y="65" font-size="13">64층 × 16ch</text> <rect x="140" y="45" width="393.8" height="28" fill="#e08a3c" fill-opacity="0.75"/> <text x="541.8" y="64" font-size="13">0.895</text> <text x="20" y="107" font-size="13">16층 × 32ch</text> <rect x="140" y="87" width="278.5" height="28" fill="#4a7bd0" fill-opacity="0.75"/> <text x="426.5" y="106" font-size="13">0.633</text> <text x="20" y="149" font-size="13">4층 × 64ch</text> <rect x="140" y="129" width="226.6" height="28" fill="#4a7bd0" fill-opacity="0.75"/> <text x="374.6" y="148" font-size="13">0.515</text> <text x="20" y="191" font-size="13">1층 × 128ch</text>
<rect x="140" y="171" width="206.8" height="28" fill="#4a7bd0" fill-opacity="0.75"/> <text x="354.8" y="190" font-size="13">0.470</text>
<line x1="140" y1="40" x2="140" y2="215" stroke="currentColor"/><text x="140" y="222" font-size="12">같은 일, 층을 잘게 쪼갤수록 느려진다: 64층은 1층의 1.9배</text></svg>
```

그림 5 — 예제 6. 같은 MAC을 64층으로 쪼개면 1층보다 1.9배 느리다.

출력에서 볼 것: 직선 맞춤에서 층 하나가 약 6.4 µs를 더한다 — 이 CPU 런타임의 "op당 고정비 + 층마다 늘어나는 activation 왕복"이 합쳐진 값이다 (C = 16 스택은 activation을 64번 읽고 쓴다). 그리고 16채널 층은 MAC 배열을 반밖에 못 채운다(E5 예제 4: 8→16 conv 40.5%). NPU에서는 명령열 하나에 여러 층을 묶을 수 있어 고정비가 더 작을 수도, delegate 호출이면 훨씬 클 수도 있다 — 그래서 프로파일의 `per_op_us`는 기기에서 재서 채운다.

**자동 검사**: 출력이 512원소 미만인 op 수 (R7). naive에서 31개를 찾았다: SE 블록 4개의 GAP·FC·ReLU·FC·Sigmoid(각 블록 5개), head의 LayerNorm 분해, softmax. SE는 MAC은 거의 없지만 op 수와 "전역 평균이 끝날 때까지 기다리는" 동기화 지점을 만든다 (C7 9절).

### 5.5 R8 — depthwise 비중은 타깃을 보고

**HW 이유**: depthwise conv는 채널마다 따로 계산하는 K = 9, N = 1짜리 작은 GEMM이라 MAC 배열을 거의 채우지 못한다. 이미 두 노트에서 숫자로 봤다.

| 출처 | 설정 | depthwise 활용률 | 비교 |
|---|---|---|---|
| D4 3.5 ex4 | 32×32 WS 배열, 56×56, C = 64 | 0.85% | 표준 3×3 conv 97% |
| E5 3.7 예제 4 | 16×16 WS 배열 사이클 시뮬, C = 16 | 2.8% | conv3×3 81%. MAC은 1.6%인데 사이클은 44% |
| 이 노트 예제 2 | 16×16 toy, naive/partial/fixed 전체 | — | MAC 10%가 toy cycle 73~74% |
| 이 노트 예제 2 (SoC) | 64×64 toy | — | MAC 10%가 toy cycle 96% |

**그러나 이것은 "depthwise를 쓰지 마라"가 아니다.** 실제 NPU는 depthwise 전용 경로(채널 병렬 매핑, 3×3 전용 벡터 유닛)를 두거나 depthwise를 뒤의 pointwise와 fuse한다 (D4 3.5, E5 3.8). Cortex-M + CMSIS-NN이나 DSP에서는 depthwise가 MAC 절약 그대로 꽤 이득일 수 있다. 그래서 이 규칙은 **"타깃의 depthwise 실측 LUT를 보고 비중을 정하라"**이고, linter는 WARN만 낸다. 대안은 (1) 앞쪽 저채널 구간을 일반 conv로(MobileNetV2 대신 FuseMBConv/EfficientNetV2식 "fused" 블록), (2) depthwise 수를 줄이고 채널을 키우기다.

**자동 검사**: depthwise의 toy cycle 비중이 프로파일 상한(40%)을 넘으면 WARN. 실제 판정은 벤더 성능 추정기(Vela의 per-layer cycle 추정 등)나 기기 LUT로 바꾼다.

---

## 6. 메모리 규칙 — R9 peak activation, R0 입력 크기

### 6.1 R9 — peak activation은 SRAM 예산 안

**HW 이유**: micro-NPU는 activation을 온칩 SRAM arena에서 읽고 쓴다. arena가 SRAM에 안 들어가면 배포가 안 된다 (외부 메모리가 있으면 spill할 수 있지만 대역폭·전력이 크게 나빠진다). peak는 텐서 합이 아니라 **동시에 살아 있는 텐서 합의 최댓값**이다 (D2 3.3). 여기서 예산은 SRAM 512 KB에서 오디오 링버퍼·스택·NPU 명령열 등 128 KB를 뺀 **384 KB**로 가정한다 (D2 8절의 워크시트 방식).

**손계산 (partial)**: stem이 stride 1이라 첫 블록이 40×98 해상도, 60채널에서 돈다. 텐서 하나 = 40 × 98 × 60 = 235,200 B (int8). 첫 DS 블록의 pointwise를 실행하는 순간 (1) residual을 위해 남겨 둔 블록 입력, (2) depthwise 출력, (3) pointwise 출력이 동시에 살아 있다 → 3 × 235,200 = **705,600 B = 689 KB**. fixed는 stem stride 2로 20×49, 64채널 → 텐서 62,720 B, 3개면 188,160 B = **184 KB**.

예제 7 — linter의 `live_curve`로 노드별 live activation 곡선을 뽑아 손계산과 맞춰 본다.

```python
# ex_peak.py — D2식 liveness로 노드별 live activation(int8) 곡선: partial(실패) vs fixed(통과)
from i2_graph import load_static, live_curve
from i2_profile import MCU_NPU as P
budget = (P["sram_kb"] - P["reserved_kb"]) * 1024
for name in ["partial", "fixed"]:
    m, shapes, inits, _ = load_static(f"{name}.onnx", {})
    c = [r for r in live_curve(m, shapes, P["act_bytes"]) if r[1] != "Constant"]
    k, pk = max(enumerate(c), key=lambda t: t[1][3])
    print(f"{name:7s} peak {pk[3]:6d} B = {pk[3] / 1024:5.1f} KB @ #{k} {pk[1]:4s} (예산 {budget // 1024} KB → "
          f"{'FAIL' if pk[3] > budget else 'OK'})")
    print("        KB: " + " ".join(f"{r[3] // 1024}" for r in c))
```

```text
partial peak 705600 B = 689.1 KB @ #4 Conv (예산 384 KB → FAIL)
        KB: 233 229 459 459 689 459 689 287 57 143 86 172 172 258 172 258 172 172 258 172 258 86 0
fixed   peak 188160 B = 183.8 KB @ #4 Conv (예산 384 KB → OK)
        KB: 65 61 122 122 183 122 183 76 15 39 23 46 46 70 46 70 46 46 70 46 70 23 0
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 370">
<text x="20" y="24" font-size="14">노드별 live activation (int8, KB) — 같은 그래프 모양, stem stride만 다르다</text> <line x1="70" y1="300" x2="650" y2="300" stroke="currentColor"/> <line x1="70" y1="300" x2="70" y2="50" stroke="currentColor"/> <text x="62" y="304.0" font-size="12" text-anchor="end">0</text> <text x="62" y="237.3" font-size="12" text-anchor="end">200</text> <text x="62" y="170.7" font-size="12" text-anchor="end">400</text> <text x="62" y="104.0" font-size="12" text-anchor="end">600</text> <line x1="70" y1="172.0" x2="650" y2="172.0" stroke="#d0564a" stroke-dasharray="6 4"/> <text x="650" y="166.0" font-size="12" text-anchor="end">activation 예산 384 KB (512 − 예약 128)</text> <polyline points="70.0,222.3 96.4,223.7 122.7,147.0 149.1,147.0 175.5,70.3 201.8,147.0 228.2,70.3 254.5,204.3 280.9,281.0 307.3,252.3 333.6,271.3 360.0,242.7 386.4,242.7 412.7,214.0 439.1,242.7 465.5,214.0 491.8,242.7 518.2,242.7 544.5,214.0 570.9,242.7 597.3,214.0 623.6,271.3 650.0,300.0" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="70.0" cy="222.3" r="2.5" fill="#e08a3c"/> <circle cx="96.4" cy="223.7" r="2.5" fill="#e08a3c"/> <circle cx="122.7" cy="147.0" r="2.5" fill="#e08a3c"/> <circle cx="149.1" cy="147.0" r="2.5" fill="#e08a3c"/> <circle cx="175.5" cy="70.3" r="2.5" fill="#e08a3c"/> <circle cx="201.8" cy="147.0" r="2.5" fill="#e08a3c"/> <circle cx="228.2" cy="70.3" r="2.5" fill="#e08a3c"/> <circle cx="254.5" cy="204.3" r="2.5" fill="#e08a3c"/> <circle cx="280.9" cy="281.0" r="2.5" fill="#e08a3c"/> <circle cx="307.3" cy="252.3" r="2.5" fill="#e08a3c"/> <circle cx="333.6" cy="271.3" r="2.5" fill="#e08a3c"/> <circle cx="360.0" cy="242.7" r="2.5" fill="#e08a3c"/> <circle cx="386.4" cy="242.7" r="2.5" fill="#e08a3c"/> <circle cx="412.7" cy="214.0" r="2.5" fill="#e08a3c"/>
<circle cx="439.1" cy="242.7" r="2.5" fill="#e08a3c"/> <circle cx="465.5" cy="214.0" r="2.5" fill="#e08a3c"/> <circle cx="491.8" cy="242.7" r="2.5" fill="#e08a3c"/> <circle cx="518.2" cy="242.7" r="2.5" fill="#e08a3c"/> <circle cx="544.5" cy="214.0" r="2.5" fill="#e08a3c"/> <circle cx="570.9" cy="242.7" r="2.5" fill="#e08a3c"/> <circle cx="597.3" cy="214.0" r="2.5" fill="#e08a3c"/> <circle cx="623.6" cy="271.3" r="2.5" fill="#e08a3c"/> <circle cx="650.0" cy="300.0" r="2.5" fill="#e08a3c"/> <polyline points="70.0,278.3 96.4,279.7 122.7,259.3 149.1,259.3 175.5,239.0 201.8,259.3 228.2,239.0 254.5,274.7 280.9,295.0 307.3,287.0 333.6,292.3 360.0,284.7 386.4,284.7 412.7,276.7 439.1,284.7 465.5,276.7 491.8,284.7 518.2,284.7 544.5,276.7 570.9,284.7 597.3,276.7 623.6,292.3 650.0,300.0" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<circle cx="70.0" cy="278.3" r="2.5" fill="#3f9a6b"/> <circle cx="96.4" cy="279.7" r="2.5" fill="#3f9a6b"/> <circle cx="122.7" cy="259.3" r="2.5" fill="#3f9a6b"/> <circle cx="149.1" cy="259.3" r="2.5" fill="#3f9a6b"/> <circle cx="175.5" cy="239.0" r="2.5" fill="#3f9a6b"/> <circle cx="201.8" cy="259.3" r="2.5" fill="#3f9a6b"/> <circle cx="228.2" cy="239.0" r="2.5" fill="#3f9a6b"/> <circle cx="254.5" cy="274.7" r="2.5" fill="#3f9a6b"/> <circle cx="280.9" cy="295.0" r="2.5" fill="#3f9a6b"/> <circle cx="307.3" cy="287.0" r="2.5" fill="#3f9a6b"/> <circle cx="333.6" cy="292.3" r="2.5" fill="#3f9a6b"/> <circle cx="360.0" cy="284.7" r="2.5" fill="#3f9a6b"/> <circle cx="386.4" cy="284.7" r="2.5" fill="#3f9a6b"/> <circle cx="412.7" cy="276.7" r="2.5" fill="#3f9a6b"/>
<circle cx="439.1" cy="284.7" r="2.5" fill="#3f9a6b"/> <circle cx="465.5" cy="276.7" r="2.5" fill="#3f9a6b"/> <circle cx="491.8" cy="284.7" r="2.5" fill="#3f9a6b"/> <circle cx="518.2" cy="284.7" r="2.5" fill="#3f9a6b"/> <circle cx="544.5" cy="276.7" r="2.5" fill="#3f9a6b"/> <circle cx="570.9" cy="284.7" r="2.5" fill="#3f9a6b"/> <circle cx="597.3" cy="276.7" r="2.5" fill="#3f9a6b"/> <circle cx="623.6" cy="292.3" r="2.5" fill="#3f9a6b"/> <circle cx="650.0" cy="300.0" r="2.5" fill="#3f9a6b"/> <line x1="90" y1="330" x2="115" y2="330" stroke="#e08a3c" stroke-width="2"/> <text x="122" y="334" font-size="12">partial: stem stride 1, 60/90ch → peak 689 KB (FAIL)</text> <line x1="90" y1="352" x2="115" y2="352" stroke="#3f9a6b" stroke-width="2"/>
<text x="122" y="356" font-size="12">fixed: stem stride 2, 64/96ch → peak 184 KB (OK)</text> <text x="175.5" y="62.3" font-size="12" text-anchor="middle">블록1 pw: 입력(residual)+dw출력+pw출력</text>
<text x="650" y="316" font-size="12" text-anchor="end">실행 순서 (노드 23개) →</text></svg>
```

그림 6 — 예제 7의 실제 출력으로 그린 live activation 곡선. 빨강 점선이 예산. partial은 첫 블록 구간(노드 2~6)에서 예산을 넘고, fixed는 전 구간이 예산의 절반 아래다.

출력에서 볼 것:

- 추정기의 peak 705,600 B가 손계산과 바이트 단위로 같다. peak 지점(#4 Conv)은 첫 블록의 pointwise다.
- **peak는 앞 블록에 몰린다.** 두 번째 블록(stride 2) 이후는 partial도 287 KB 이하다. 그래서 고칠 곳은 "모델 전체를 줄이기"가 아니라 **"해상도가 높은 앞쪽 구간"**이다. stem stride 2 하나로 앞 구간 텐서가 4분의 1(40×98 → 20×49)이 되면서 peak이 689 → 184 KB가 됐다 (채널을 60 → 64로 늘렸는데도).
- 정확도: fixed는 해상도를 줄였지만 float 정확도가 떨어지지 않았다(예제 1: 0.867). 실제 음성에서는 시간 해상도가 중요한 과제도 있으므로, **stride를 넣을 때는 반드시 재학습·재검증**한다.

**"MAC은 맞는데 SRAM이 안 맞는다"의 처방 순서** (면접 단골, 14절):

1. 앞쪽에서 일찍 downsample한다 (stem stride 2, 또는 첫 블록 stride). 효과가 가장 크다 — 텐서 면적이 4분의 1.
2. 앞쪽 고해상도 구간의 채널·expand ratio를 줄인다 (C7 9절: expand 6 → 3이면 그 블록 peak 약 절반).
3. 앞쪽 구간의 residual(skip)을 없애거나 짧게 한다 — 블록 입력을 끝까지 살려 두는 비용.
4. 실행 순서·in-place를 컴파일러가 잘 쓰게 한다 (C6 11절, D2 3.5).
5. 그래도 안 되면 patch 기반 추론(MCUNetV2, C7 7.4) — 컴파일러·런타임 지원이 필요하다.
6. 마지막으로 입력 자체를 줄인다 (R0).

### 6.2 R0 — 입력 해상도·프레임 수가 첫 번째 손잡이

**HW 이유**: 모델의 거의 모든 층은 입력의 시간·주파수(또는 H·W) 크기에 비례하는 MAC과 activation을 가진다. 입력을 줄이면 모든 층이 동시에 줄어든다. 그리고 입력 크기는 **센서·DSP 설정**(샘플레이트, mel 수, hop)이라 모델 구조보다 먼저 정해진다. embedded 엔지니어가 가장 먼저 모델팀과 합의할 숫자다 (I1의 예산 설정과 같은 단계).

예제 8 — fixed 구조 그대로, 입력의 mel 수와 hop만 바꿔 linter 요약값과 CPU 지연을 본다 (가중치는 무작위 — 비용만 본다).

```python
# ex_res.py — 입력 해상도(mel 수 × 프레임 수)가 첫 번째 손잡이: 같은 fixed 구조로 MAC·peak·CPU 지연
import torch, io, onnx, numpy as np, warnings
from i2_models import SPECS, KWS
from i2_lint import lint
from i2_profile import MCU_NPU
from i2_bench import ort_ms
warnings.filterwarnings("ignore"); torch.manual_seed(0); m = KWS(SPECS["fixed"]).eval()
for F, T, note in [(40, 98, "40 mel, hop 10 ms"), (32, 98, "32 mel, hop 10 ms"), (40, 49, "40 mel, hop 20 ms"), (32, 49, "32 mel, hop 20 ms")]:
    x = torch.randn(1, 1, F, T); torch.onnx.export(m, (x,), "res.onnx", input_names=["x"], opset_version=17, dynamo=False)
    _, s = lint("res.onnx", MCU_NPU); ms = ort_ms("res.onnx", {"x": x.numpy()}, n=200)
    print(f"{note:18s} input {F}x{T:<3d} {s['MMAC']:6.2f} MMAC  peak {s['peak_KB']:6.1f} KB  CPU {ms:6.3f} ms")
```

```text
40 mel, hop 10 ms  input 40x98   11.86 MMAC  peak  184.2 KB  CPU  0.692 ms
32 mel, hop 10 ms  input 32x98    9.49 MMAC  peak  147.5 KB  CPU  0.574 ms
40 mel, hop 20 ms  input 40x49    6.12 MMAC  peak   94.2 KB  CPU  0.434 ms
32 mel, hop 20 ms  input 32x49    4.90 MMAC  peak   75.5 KB  CPU  0.354 ms
```

출력에서 볼 것:

- MAC과 peak이 입력 면적에 거의 정비례한다: 40×98 → 32×49(면적 40%)에서 MAC 41%, peak 41%. CPU 지연은 51%로 덜 줄어든다 — 고정비(R7) 때문이다.
- peak 184.2 KB는 예제 7의 183.8 KB와 512 B 다르다. 원인을 찾아보니, 학습하지 않은 모델은 BN 통계가 기본값이라 접힌 bias 값들이 서로 같고, exporter가 중복 initializer를 `Identity` 노드 7개로 공유시켰다. 추정기가 그 bias 벡터(64·96원소)를 activation으로 센 것이다. 추정기는 이런 export 잔재에 몇 백 바이트씩 흔들린다 — linter도 "initializer만 받는 Identity는 상수"로 처리하도록 고칠 거리다.
- 비용은 확실히 줄지만 정확도는 공짜가 아니다. mel 32개는 고음역 해상도를, hop 20 ms는 짧은 음소의 시간 해상도를 잃는다. **이 표를 모델팀에게 주고 "정확도 곡선을 그려 달라"**고 요청하는 것이 co-design이다 (C7 8절의 width·resolution·depth 스케일링).
- 덤: hop을 20 ms로 늘리면 DSP의 특징 추출 횟수도 절반이다 — 모델 밖의 전력도 준다 (I4).

---

## 7. 실행 흐름 규칙 — R11 제어 흐름, R13 softmax·LayerNorm, R14 causal, R10 layout

### 7.1 R11 — 제어 흐름은 그래프 밖으로

**HW 이유**: `If`, `Loop`, `NonZero`, 데이터 의존 `TopK`·`Range`는 실행해 봐야 다음에 무엇을 할지(또는 텐서 크기가) 정해진다. 정적 명령열을 미리 만드는 NPU 컴파일러에는 맞지 않는다. 대부분 CPU fallback이거나 컴파일 실패다.

**근거**: F5 예제 7에서 데이터 의존 분기를 export하면 `If` 노드가 생기거나(스크립트 방식) 한쪽 분기만 남은 **조용히 틀린** 모델이 나오는 것을 봤다. F5의 deploy lint가 `ops:Abs,Greater,If`로 잡았다.

**모델팀에게 하는 말**: "early-exit, 조건부 실행, 가변 길이 루프는 그래프에 넣지 말고 **펌웨어가 모델 두 개를 순서대로 부르는 구조**로 바꾸자." 예를 들어 VAD → KWS → ASR의 cascade(I3)가 바로 "제어 흐름을 그래프 밖으로" 뺀 설계다. 모델 안 if 대신 펌웨어의 if.

### 7.2 R13 — MCU급 NPU에서는 softmax·LayerNorm을 최소화

**HW 이유**: softmax는 max·exp·sum·나눗셈, LayerNorm은 평균·분산·rsqrt를 축 전체에 대해 해야 한다. 축 전체를 다 본 뒤에야 다음으로 갈 수 있고(reduce), int8로는 범위 관리가 까다롭다 (exp는 LUT, rsqrt는 고정소수점 Newton 반복 등). micro-NPU는 이런 op를 지원하더라도 특수 경로이거나 CPU fallback인 경우가 많다.

**근거**: 8절 예제 10에서 naive의 head LayerNorm이 TFLite int8 변환 후에도 **float `NEG` op와 `DEQUANTIZE`/`QUANTIZE` 쌍**을 남겼다 — 완전 int8이 안 되는 구간이 생긴 것이다. E5 예제 9의 시뮬레이션에서는 그래프 끝의 Softmax 하나는 경계가 한 번뿐이라 상대적으로 쌌다(0.257 → 0.197 ms).

**모델팀에게 하는 말**: "분류기 끝의 softmax는 빼고 logits를 내보내라. argmax나 threshold는 펌웨어가 한다 (softmax는 단조 함수라 argmax가 같다). 정규화는 BN(접힘)으로. Transformer 계열이 꼭 필요하면 SoC NPU 쪽에 두자." 이 노트의 partial·fixed가 정확히 그렇게 했다.

### 7.3 R14 — 스트리밍 모델은 causal

**HW 이유**: 오디오·IMU처럼 프레임이 계속 들어오는 입력에서, 출력 하나를 내기 위해 미래 프레임 k개가 필요하면 그 출력은 최소 k × 프레임 주기만큼 늦는다. 대칭 padding의 conv는 양쪽을 보므로 미래를 본다. dilated conv를 쌓으면 그 미래가 누적된다.

```
 lookahead (프레임) = Σ_층 (오른쪽 padding_층 × 그 층 입력까지의 누적 stride)

 손계산: k = 3, dilation 1·2·4·8, 대칭 padding (padding = dilation)
         lookahead = 1 + 2 + 4 + 8 = 15 프레임 → 100 Hz IMU면 150 ms
         causal (왼쪽만 2·dilation padding) → 0 프레임
```

말로 하면: 수용 영역(31프레임)은 같아도, 그 영역을 **과거 쪽에만** 두면 지연이 0이 된다. 대신 과거 프레임을 링버퍼(streaming state)로 들고 있어야 한다 (D2 7절, D4 7절).

예제 9 — 6축 IMU용 dilated TCN을 대칭/인과 padding으로 export해 linter의 R14로 lookahead를 재고, 같은 인과 모델을 옛 exporter로 export했을 때의 그래프도 비교한다.

```python
# ex_causal.py — 스트리밍 TCN: 대칭 padding(비인과) vs 왼쪽 padding(인과)을 linter의 lookahead로 잰다 + exporter 차이
import torch, torch.nn as nn, onnx, collections, warnings
from i2_lint import lint
from i2_profile import MCU_NPU
warnings.filterwarnings("ignore")
class TCN(nn.Module):
    def __init__(s, causal, C=32, k=3, dil=(1, 2, 4, 8)):
        super().__init__(); s.causal, s.k = causal, k
        s.convs = nn.ModuleList(nn.Conv2d(6 if i == 0 else C, C, (1, k), dilation=(1, d),
                                padding=(0, 0 if causal else d * (k - 1) // 2)) for i, d in enumerate(dil))
    def forward(s, x):                     # x: (B, 6축, 1, T) — rank 4로 맞춘 1D conv
        for c in s.convs:
            if s.causal: x = nn.functional.pad(x, (c.dilation[1] * (s.k - 1), 0))   # 과거 쪽만 padding
            x = torch.relu(c(x))
        return x
P = dict(MCU_NPU, streaming=True, frame_ms=10.0)          # 100 Hz IMU라면 1 프레임 = 10 ms
x = torch.randn(1, 6, 1, 100)
for causal in (False, True):
    torch.onnx.export(TCN(causal).eval(), (x,), "tcn.onnx", opset_version=18, dynamo=True, verbose=False)
    f, s = lint("tcn.onnx", P)
    print(f"causal={causal!s:5s} lookahead={s['lookahead_frames']:2d}  {[m for _, _, m, _ in f] or 'no findings'}")
torch.onnx.export(TCN(True).eval(), (x,), "tcn_legacy.onnx", opset_version=17, dynamo=False)   # 같은 모델, 옛 exporter
for f in ["tcn.onnx", "tcn_legacy.onnx"]:
    print(f"{f:16s}", dict(collections.Counter(n.op_type for n in onnx.load(f).graph.node)))
```

```text
causal=False lookahead=15  ['lookahead 15 프레임 = 150 ms 지연']
causal=True  lookahead= 0  no findings
tcn.onnx         {'Conv': 4, 'Relu': 4}
tcn_legacy.onnx  {'Constant': 32, 'ConstantOfShape': 4, 'Concat': 4, 'Reshape': 8, 'Slice': 4, 'Transpose': 4, 'Cast': 4, 'Pad': 4, 'Conv': 4, 'Relu': 4}
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<text x="20" y="24" font-size="14">dilated TCN (k=3, d=1·2·4·8)의 출력 1개가 보는 입력 프레임 — 0 = 지금</text> <text x="20" y="52" font-size="13">대칭 padding (비인과): 입력 -15 … +15 프레임</text> <rect x="51.0" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="60.5" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="70.0" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="79.5" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="89.0" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="98.5" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/>
<rect x="108.0" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="117.5" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="127.0" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="136.5" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="146.0" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="155.5" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="165.0" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="174.5" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/>
<rect x="184.0" y="70" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="193.5" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="203.0" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="212.5" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="222.0" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="231.5" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="241.0" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="250.5" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="260.0" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/>
<rect x="269.5" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="279.0" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="288.5" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="298.0" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="307.5" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="317.0" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="326.5" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="336.0" y="70" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="345.5" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/>
<rect x="355.0" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <rect x="364.5" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <rect x="374.0" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <rect x="383.5" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <rect x="393.0" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <rect x="402.5" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <rect x="412.0" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <rect x="421.5" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <rect x="431.0" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/>
<rect x="440.5" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <rect x="450.0" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <rect x="459.5" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <rect x="469.0" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <rect x="478.5" y="70" width="8" height="26" fill="#d0564a" fill-opacity="0.8"/> <line x1="340.0" y1="64" x2="340.0" y2="104" stroke="currentColor" stroke-width="2"/> <text x="492.5" y="88" font-size="12">미래 15프레임 = 150 ms 대기</text> <text x="20" y="172" font-size="13">왼쪽만 padding (인과): 입력 -30 … +0 프레임</text> <rect x="51.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/>
<rect x="60.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="70.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="79.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="89.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="98.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="108.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="117.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="127.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="136.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/>
<rect x="146.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="155.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="165.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="174.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="184.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="193.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="203.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="212.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="222.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/>
<rect x="231.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="241.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="250.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="260.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="269.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="279.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="288.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="298.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="307.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/>
<rect x="317.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="326.5" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="336.0" y="190" width="8" height="26" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="345.5" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="355.0" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="364.5" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="374.0" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="383.5" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/>
<rect x="393.0" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="402.5" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="412.0" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="421.5" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="431.0" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="440.5" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="450.0" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="459.5" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/>
<rect x="469.0" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <rect x="478.5" y="190" width="8" height="26" fill="none" stroke="#888" stroke-width="0.6"/> <line x1="340.0" y1="184" x2="340.0" y2="224" stroke="currentColor" stroke-width="2"/> <text x="492.5" y="208" font-size="12">미래 0프레임</text> <text x="51.0" y="250" font-size="12">−30</text> <text x="340.0" y="250" font-size="12" text-anchor="middle">0</text> <text x="482.5" y="250" font-size="12" text-anchor="middle">+15</text>
<text x="20" y="276" font-size="12">파랑 = 과거·현재 프레임, 빨강 = 미래 프레임. 둘 다 31프레임을 보지만,</text><text x="20" y="292" font-size="12">비인과 모델은 출력을 내기 전에 15프레임을 기다려야 한다</text></svg>
```

그림 7 — 예제 9의 TCN에서 출력 1개가 보는 입력 프레임. 두 모델 모두 31프레임을 보지만, 비인과 모델은 그중 15프레임이 미래다.

출력에서 볼 것:

- linter가 손계산과 같은 15프레임 = 150 ms를 찾았다. 제스처 인식에서 150 ms 지연은 사용자가 느낄 수 있는 크기다. causal로 바꾸면 0.
- 새 exporter(dynamo, opset 18)는 왼쪽 padding을 Conv의 비대칭 `pads` 속성으로 흡수해 **Conv 4 + Relu 4**만 남겼다. 같은 모델을 옛 exporter로 내보내면 `F.pad` 하나마다 pads 계산용 `ConstantOfShape·Concat·Reshape(2)·Slice·Transpose·Cast` 7개 + `Constant` 8개 + `Pad`가 생겨, 그래프가 8노드에서 72노드가 된다. 이것이 R10의 실제 모습이다: **모델 코드는 같은데 export 경로가 layout churn을 만든다.** 상수 접기(constant folding)나 onnx 최적화로 대부분 지울 수 있지만(C6 3절), 지워졌는지 linter로 확인해야 한다.
- streaming state는 두 모델이 같다: 층마다 (k−1) × dilation 프레임 × 입력 채널 → 2 × (1·6 + 2·32 + 4·32 + 8·32) = 908 B (int8). causal로 바꾼다고 메모리가 늘지 않는다 — 지연만 준다.

### 7.4 R10 — layout op를 줄인다

**HW 이유**: Transpose·Reshape·Concat·Split은 MAC이 0이지만 텐서를 통째로 다른 순서로 복사한다. NPU는 내부 레이아웃(NHWC, brick)이 정해져 있어서 그래프의 Transpose가 실제 데이터 이동이 되고, 미지원이면 fallback이다. C6 7절에서 transpose 상쇄 규칙과 "안 지워지는 경우"를 봤다.

**측정 근거**: naive는 입력 레이아웃(B, T, F)을 그래프 안에서 transpose해 layout op가 10개, partial·fixed는 DSP가 처음부터 (1, 1, F, T)로 써 주므로 0개다 (예제 2). 예제 9의 옛 exporter는 pad 4개에 layout op 20개(Concat 4, Reshape 8, Slice 4, Transpose 4)를 만들었다.

**모델팀에게 하는 말**: "입력 텐서의 레이아웃은 DSP/펌웨어와 합의된 것 하나로 고정하고, 모델 안에서 permute하지 말라. concat은 채널 축으로만, 그리고 정렬된 채널 수로."

---

## 8. 양자화 규칙 — R12 int8 친화 구조

**HW 이유**: int8 텐서 하나는 scale 하나(per-tensor)로 실수 범위를 256칸으로 나눈다. 그래서 (1) 범위가 넓은 텐서, (2) 채널끼리 범위가 크게 다른 텐서, (3) 0 근처의 작은 값이 중요한 텐서가 int8에서 망가진다. 메커니즘은 C1(SQNR, outlier 채널: 전체 36.2 dB인데 작은 채널들은 23 dB)과 C2(PTQ/QAT)에서 봤다. 설계 규칙으로 바꾸면:

- 활성화는 유계로 (ReLU6) — 예제 3.
- BN을 써서 채널별 범위를 고르게, 그리고 접히게 — R3.
- **넓은 범위 op(log, exp, 나눗셈, pow)를 int8 구간 앞에 두지 말라.** 특히 특징 추출(log-mel)은 DSP에서 float·고정소수점으로 하고, 정규화된 결과를 int8 모델에 넣는다.
- 덧셈으로 만나는 두 텐서(residual)의 범위가 비슷하게.

이 중 세 번째가 가장 극적이다. 예제 10에서 숫자로 본다.

### 8.1 실험 준비 — torch 가중치를 Keras로 옮겨 TFLite int8로

TFLite 변환기는 TensorFlow 환경(`.venv-tf`)에 있고 torch는 없다. 그래서 (1) torch 환경에서 가중치와 테스트 입력을 npz로 덤프하고, (2) TF 환경에서 **같은 구조의 Keras 모델**에 가중치를 옮긴 뒤 float 출력이 torch와 같은지 확인하고, (3) full-int8 PTQ로 변환한다. 입출력도 int8로 둔다 — MCU 배포에서는 DSP가 int8 특징을 바로 넣는 경우가 많기 때문이다.

```python
# dump_np.py (torch 환경) — torch 가중치·테스트 입력·torch 출력을 npz로 (TF 환경에는 torch가 없다)
import torch, numpy as np
from i2_models import SPECS, KWS, make_data, to_input
Lte, yte = make_data(600, 2); Lrep, _ = make_data(200, 3)
for name, sp in SPECS.items():
    m = KWS(sp); m.load_state_dict(torch.load(f"{name}.pt")); m.eval()
    np.savez(f"{name}_w.npz", **{k: v.numpy() for k, v in m.state_dict().items()})
    x = to_input(Lte, sp)
    with torch.no_grad(): y = m(torch.from_numpy(x)).numpy()
    np.savez(f"{name}_io.npz", x=x, y=y, label=yte, rep=to_input(Lrep, sp))
print("ok")
```

```sh
.venv/bin/python dump_np.py
.venv/bin/python -c "from i2_models import SPECS; open('i2_specs.py','w').write('SPECS = ' + repr(SPECS) + '\n')"
```

```python
# i2_keras.py (TF 환경) — torch 가중치(.npz)를 같은 구조의 Keras 모델(NHWC)로 옮긴다 → TFLite int8 변환용
import numpy as np, tensorflow as tf
from tensorflow import keras
from keras import layers as Lr
MU, SD = -5.9, 1.1

def build(sp, W):
    """sp: SPECS 항목, W: torch state_dict를 numpy로 (키 이름 그대로). 반환: keras.Model"""
    todo = []                                                  # (layer, weights) — 모델을 만든 뒤 한꺼번에 set
    def conv(x, pre, ci, co, k, st, dw):
        if k > 1: x = Lr.ZeroPadding2D(k // 2)(x)              # torch의 대칭 padding을 그대로 재현
        w = W[pre + ".conv.weight"]
        if dw: l = Lr.DepthwiseConv2D(k, st, use_bias=False); todo.append((l, [w.transpose(2, 3, 0, 1)]))
        else:  l = Lr.Conv2D(co, k, st, use_bias=False);       todo.append((l, [w.transpose(2, 3, 1, 0)]))
        x = l(x)
        bn = Lr.BatchNormalization(epsilon=1e-5)
        todo.append((bn, [W[pre + ".bn.weight"], W[pre + ".bn.bias"], W[pre + ".bn.running_mean"], W[pre + ".bn.running_var"]]))
        a = (Lr.Activation(lambda t: tf.nn.gelu(t, approximate=False)) if sp["act"] == "gelu" else Lr.ReLU(6.0))
        return bn(a(x)) if sp["bn_after_act"] else a(bn(x))
    if sp["log_in"]:
        inp = keras.Input((98, 40), batch_size=1)
        x = Lr.Lambda(lambda t: (tf.math.log(t + 1e-6) - MU) / SD)(inp)
        x = Lr.Reshape((40, 98, 1))(Lr.Permute((2, 1))(x))
    else:
        inp = keras.Input((40, 98, 1), batch_size=1); x = inp
    c0, st0 = sp["stem"]; x = conv(x, "stem", 1, c0, 3, st0, False); ci = c0
    for i, (co, st) in enumerate(sp["blocks"]):
        y = conv(x, f"blocks.{i}.dw", ci, ci, 3, st, True); y = conv(y, f"blocks.{i}.pw", ci, co, 1, 1, False)
        if sp["se"]:
            s = Lr.GlobalAveragePooling2D()(y)
            f1 = Lr.Dense(co // 4, activation="relu"); f2 = Lr.Dense(co, activation="sigmoid")
            todo += [(f1, [W[f"blocks.{i}.se.f1.weight"].T, W[f"blocks.{i}.se.f1.bias"]]),
                     (f2, [W[f"blocks.{i}.se.f2.weight"].T, W[f"blocks.{i}.se.f2.bias"]])]
            y = Lr.Multiply()([y, Lr.Reshape((1, 1, co))(f2(f1(s)))])
        x = Lr.Add()([x, y]) if (st == 1 and ci == co) else y; ci = co
    x = Lr.GlobalAveragePooling2D()(x)
    if sp["head_ln"]:
        ln = Lr.LayerNormalization(epsilon=1e-5); todo.append((ln, [W["ln.weight"], W["ln.bias"]])); x = ln(x)
    fc = Lr.Dense(5); todo.append((fc, [W["fc.weight"].T, W["fc.bias"]])); x = fc(x)
    if sp["softmax"]: x = Lr.Softmax()(x)
    model = keras.Model(inp, x)
    for l, w in todo: l.set_weights(w)
    return model

def to_tflite_int8(model, rep):
    cv = tf.lite.TFLiteConverter.from_keras_model(model)
    cv.optimizations = [tf.lite.Optimize.DEFAULT]
    cv.representative_dataset = lambda: ([r[None]] for r in rep)
    cv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8, tf.lite.OpsSet.TFLITE_BUILTINS]
    cv.inference_input_type = tf.int8; cv.inference_output_type = tf.int8   # MCU 배포처럼 int8 입출력
    return cv.convert()
```

가중치 옮기기의 핵심은 레이아웃 변환이다: PyTorch conv weight `(O, I, kh, kw)` → Keras `(kh, kw, I, O)`, depthwise `(C, 1, kh, kw)` → `(kh, kw, C, 1)`, Linear `(O, I)` → `(I, O)`. padding은 torch의 대칭 padding을 `ZeroPadding2D` + `valid`로 재현했다 — Keras의 `same` + stride 2는 오른쪽에 더 패딩해서 결과가 달라진다. `supported_ops`에 `TFLITE_BUILTINS`를 같이 둔 것은 int8 커널이 없는 op가 있으면 float로 남기라는 뜻이다 (변환 실패 대신 fallback을 **관찰**하려고).

### 8.2 예제 10 — TFLite int8: 크기, SQNR, 정확도, 남은 float op

네 가지를 변환한다: naive, **naive-logout**(naive 가중치 그대로, log·정규화만 그래프 밖 DSP로 뺀 것 — 나머지 naive 요소는 전부 그대로), partial, fixed. SQNR은 같은 입력 300개에 대해 Keras float 출력 대비 int8 출력(dequantize)의 `10·log10(Σy² / Σ(y − ŷ)²)`이다.

```python
# ex_tflite.py (TF 환경) — Keras로 옮긴 모델을 TFLite full-int8로: 크기, float 대비 SQNR, 정확도, op 목록
import numpy as np, tensorflow as tf, collections
from i2_keras import build, to_tflite_int8
from i2_specs import SPECS                                   # i2_models.SPECS와 같은 dict (torch 없이 import)
NODELEG = tf.lite.experimental.OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES
runs = [("naive", "naive", "naive"), ("naive-logout", "naive", "partial"), ("partial", "partial", "partial"), ("fixed", "fixed", "fixed")]
for label, wname, ioname in runs:      # naive-logout: naive 가중치 그대로, log·정규화만 그래프 밖(DSP)으로
    sp = dict(SPECS[wname], log_in=(label == "naive")); W = dict(np.load(f"{wname}_w.npz")); io = np.load(f"{ioname}_io.npz")
    nhwc = lambda a: a if sp["log_in"] else a.transpose(0, 2, 3, 1)
    x, rep = nhwc(io["x"][:300]), nhwc(io["rep"])
    km = build(sp, W); yk = np.concatenate([km(x[i:i + 1], training=False).numpy() for i in range(300)])
    tfl = to_tflite_int8(km, rep)
    it = tf.lite.Interpreter(model_content=tfl, experimental_op_resolver_type=NODELEG); it.allocate_tensors()
    di, do = it.get_input_details()[0], it.get_output_details()[0]; (si, zi), (so, zo) = di["quantization"], do["quantization"]
    yq = []
    for i in range(300):
        it.set_tensor(di["index"], np.clip(np.round(x[i:i + 1] / si + zi), -128, 127).astype(np.int8)); it.invoke()
        yq.append((it.get_tensor(do["index"]).astype(np.float32) - zo) * so)
    yq = np.concatenate(yq); lab = io["label"][:300]; td = it.get_tensor_details()
    sqnr = 10 * np.log10((yk ** 2).sum() / ((yk - yq) ** 2).sum())
    ops = it._get_ops_details(); cnt = collections.Counter(o["op_name"] for o in ops)
    fl = sorted({o["op_name"] for o in ops if any(td[t]["dtype"] == np.float32 for t in o["inputs"] if t >= 0)})
    print(f"{label:12s} {len(tfl) / 1024:5.1f} KB  in_scale {si:.4f}  SQNR {sqnr:5.1f} dB  "
          f"acc float {(yk.argmax(1) == lab).mean():.3f} → int8 {(yq.argmax(1) == lab).mean():.3f}  float ops {fl}")
    print(f"{'':12s} {sum(cnt.values())} ops: {dict(cnt)}")
```

```sh
.venv-tf/bin/python ex_tflite.py 2>/dev/null | grep -E "KB  in_scale| ops: "
```

```text
naive         96.3 KB  in_scale 0.0274  SQNR  -0.5 dB  acc float 0.843 → int8 0.197  float ops ['NEG', 'QUANTIZE']
             82 ops: {'ADD': 16, 'LOG': 1, 'SUB': 1, 'MUL': 17, 'TRANSPOSE': 1, 'RESHAPE': 1, 'CONV_2D': 5, 'GELU': 9, 'DEPTHWISE_CONV_2D': 4, 'MEAN': 7, 'FULLY_CONNECTED': 9, 'LOGISTIC': 4, 'PAD': 1, 'DEQUANTIZE': 1, 'NEG': 1, 'QUANTIZE': 1, 'SQUARED_DIFFERENCE': 1, 'RSQRT': 1, 'SOFTMAX': 1}
naive-logout  95.1 KB  in_scale 0.0483  SQNR  15.5 dB  acc float 0.843 → int8 0.843  float ops ['NEG', 'QUANTIZE']
             76 ops: {'CONV_2D': 5, 'GELU': 9, 'MUL': 16, 'ADD': 15, 'DEPTHWISE_CONV_2D': 4, 'MEAN': 7, 'FULLY_CONNECTED': 9, 'LOGISTIC': 4, 'PAD': 1, 'DEQUANTIZE': 1, 'NEG': 1, 'QUANTIZE': 1, 'SQUARED_DIFFERENCE': 1, 'RSQRT': 1, 'SOFTMAX': 1}
partial       55.3 KB  in_scale 0.0483  SQNR  42.3 dB  acc float 0.853 → int8 0.853  float ops []
             15 ops: {'CONV_2D': 5, 'DEPTHWISE_CONV_2D': 4, 'ADD': 3, 'PAD': 1, 'MEAN': 1, 'FULLY_CONNECTED': 1}
fixed         60.3 KB  in_scale 0.0483  SQNR  39.3 dB  acc float 0.860 → int8 0.860  float ops []
             16 ops: {'PAD': 2, 'CONV_2D': 5, 'DEPTHWISE_CONV_2D': 4, 'ADD': 3, 'MEAN': 1, 'FULLY_CONNECTED': 1}
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<text x="20" y="24" font-size="14">TFLite full-int8: 출력 SQNR(dB, 왼쪽)과 정확도 float → int8 (오른쪽)</text> <line x1="190.0" y1="40" x2="190.0" y2="240" stroke="currentColor"/> <text x="190.0" y="256" font-size="12" text-anchor="middle">0 dB</text> <text x="350.0" y="256" font-size="12" text-anchor="middle">40 dB</text> <text x="20" y="68" font-size="13">naive</text> <rect x="188.0" y="52" width="2.0" height="22" fill="#d0564a" fill-opacity="0.8"/> <text x="196.0" y="68" font-size="12">-0.5</text> <rect x="440" y="52" width="168.6" height="9" fill="#888" fill-opacity="0.7"/> <rect x="440" y="64" width="39.4" height="9" fill="#d0564a" fill-opacity="0.8"/> <text x="614.6" y="68" font-size="12">0.84→0.20</text> <text x="20" y="116" font-size="13">naive-logout</text>
<rect x="190.0" y="100" width="62.0" height="22" fill="#e08a3c" fill-opacity="0.8"/> <text x="258.0" y="116" font-size="12">15.5</text> <rect x="440" y="100" width="168.6" height="9" fill="#888" fill-opacity="0.7"/> <rect x="440" y="112" width="168.6" height="9" fill="#e08a3c" fill-opacity="0.8"/> <text x="614.6" y="116" font-size="12">0.84→0.84</text> <text x="20" y="164" font-size="13">partial</text> <rect x="190.0" y="148" width="169.2" height="22" fill="#3f9a6b" fill-opacity="0.8"/> <text x="365.2" y="164" font-size="12">42.3</text> <rect x="440" y="148" width="170.6" height="9" fill="#888" fill-opacity="0.7"/> <rect x="440" y="160" width="170.6" height="9" fill="#3f9a6b" fill-opacity="0.8"/> <text x="616.6" y="164" font-size="12">0.85→0.85</text>
<text x="20" y="212" font-size="13">fixed</text> <rect x="190.0" y="196" width="157.2" height="22" fill="#3f9a6b" fill-opacity="0.8"/> <text x="353.2" y="212" font-size="12">39.3</text> <rect x="440" y="196" width="172.0" height="9" fill="#888" fill-opacity="0.7"/> <rect x="440" y="208" width="172.0" height="9" fill="#3f9a6b" fill-opacity="0.8"/> <text x="618.0" y="212" font-size="12">0.86→0.86</text>
<text x="20" y="284" font-size="12">naive-logout = naive 가중치 그대로, log·정규화만 그래프 밖(DSP)으로 뺀 것. 회색 막대 = float 정확도</text></svg>
```

그림 8 — 예제 10. 왼쪽: int8 출력 SQNR. 오른쪽: 정확도(회색 = float, 색 = int8). naive는 int8에서 정확도가 우연 수준(0.2 = 5 클래스 중 1)으로 떨어진다.

출력에서 볼 것 — 네 줄이 네 가지 교훈이다.

- **naive: SQNR −0.5 dB, 정확도 0.843 → 0.197.** 출력이 신호보다 noise가 큰 상태, 즉 모델이 죽었다. 원인은 int8 입력이다. 입력이 **선형 power**라 범위가 [1e-5, 17] 정도이고, int8 scale은 0.0274가 된다. 배경 noise의 power는 대략 e^−6 ≈ 0.0025라 **scale의 10분의 1** — 거의 모든 배경 칸이 0으로 양자화되고, 그래프 안의 `log(0 + 1e-6)` = −13.8이 된다. float에서는 −6 근처였던 값이다. 양자화 오차가 log를 지나며 폭발했다. 이것이 "넓은 범위 op를 int8 앞에 두지 말라"의 실체다.
- **naive-logout: 같은 가중치, log만 밖으로 뺐더니 정확도 0.843 그대로.** 문제의 대부분이 한 op의 위치였다. 그래도 SQNR은 15.5 dB로 낮다 — 남은 GELU·SE(Sigmoid)·LayerNorm·softmax 출력이 int8에서 거칠다(출력이 softmax 확률이라 다른 변형의 logits SQNR과 1:1 비교는 아니다). 그리고 `NEG`·`QUANTIZE`가 float 입력으로 남았다: LayerNorm 구간 일부가 int8 커널을 못 받아 float로 돈다는 뜻이다. MCU에서는 이 구간이 CPU float 연산이 된다 (R13).
- **partial: 42.3 dB, 정확도 변화 없음, op 15개, float op 없음.** 완전한 int8 그래프. 크기도 96 → 55 KB (BN 접힘, SE·LN 제거).
- **fixed: 39.3 dB, 정확도 변화 없음.** partial과 같은 수준이다 — 구조 수정(stride, 채널 정렬)은 양자화에 거의 중립이다. 파라미터가 조금 많아 60 KB.

정리하면, **양자화 친화성은 대부분 op 선택(R2·R3·R12·R13)에서 결정되고, 구조(R6·R9)는 비용을 결정한다.** 그리고 이 실험에서 가장 큰 효과는 "어떤 op를 쓰냐"가 아니라 "**그 op가 어디에 있냐**"(log가 int8 경계의 안이냐 밖이냐)였다.

**자동 검사**: R12는 Log·Exp·Pow·Sqrt·Reciprocal·Div를 찾아 WARN을 낸다. 더 엄밀하게 하려면 "그래프 입력에서 첫 Conv까지의 경로에 이 op가 있으면 ERROR"로 위치를 볼 수 있다 (15절 연습문제). 최종 판정은 이 예제처럼 **변환 후 SQNR·정확도 측정**이고, 그건 CI의 두 번째 단계로 돌린다 (11.2절). layer별 SQNR로 어느 층이 나쁜지 찾는 방법은 C8에 있다.

---

## 9. R15 — 파라미터·embedding 예산 (SLM은 vocab부터)

**HW 이유**: MCU 모델은 가중치가 flash(XIP 또는 SRAM 복사)에, SoC 모델은 LPDDR에 있다. 작은 CNN에서는 대개 activation(R9)이 먼저 막히지만, 언어 모델에서는 **embedding 테이블**이 따로 큰 덩어리다. vocab × d_model 바이트이고, lm_head가 tied가 아니면 두 배다. decode 때 lm_head는 토큰마다 vocab × d MAC의 GEMV이고 그 가중치를 매번 읽는다 (D5).

예제 11 — 공개 config 값(vocab, hidden size)으로 embedding 크기를 계산한다 (산술만).

```python
# ex_embed.py — SLM의 vocab·embedding 크기 (B8 연결): 공개 config 값으로 산술만
cfgs = [("SmolLM2-135M", 49152, 576, 134.5e6), ("SmolLM2-360M", 49152, 960, 361.8e6), ("Qwen2.5-0.5B", 151936, 896, 494.0e6)]
for name, V, d, total in cfgs:
    emb = V * d
    print(f"{name:13s} vocab {V:6d} x d {d:4d} = {emb / 1e6:6.1f} M params ({emb / total:4.0%} of {total / 1e6:.0f} M)"
          f"  int8 {emb / 2**20:6.1f} MiB  untied(+lm_head) {2 * emb / 2**20:6.1f} MiB")
for V in (8000, 16000, 32000):                # 기기 전용 작은 vocab으로 줄였을 때 (d = 576 고정)
    print(f"vocab {V:5d}: embedding int8 {V * 576 / 2**20:5.1f} MiB, lm_head GEMV per token {V * 576 / 1e6:5.1f} MMAC")
```

```text
SmolLM2-135M  vocab  49152 x d  576 =   28.3 M params ( 21% of 134 M)  int8   27.0 MiB  untied(+lm_head)   54.0 MiB
SmolLM2-360M  vocab  49152 x d  960 =   47.2 M params ( 13% of 362 M)  int8   45.0 MiB  untied(+lm_head)   90.0 MiB
Qwen2.5-0.5B  vocab 151936 x d  896 =  136.1 M params ( 28% of 494 M)  int8  129.8 MiB  untied(+lm_head)  259.7 MiB
vocab  8000: embedding int8   4.4 MiB, lm_head GEMV per token   4.6 MMAC
vocab 16000: embedding int8   8.8 MiB, lm_head GEMV per token   9.2 MMAC
vocab 32000: embedding int8  17.6 MiB, lm_head GEMV per token  18.4 MMAC
```

출력에서 볼 것: 작은 언어 모델일수록 embedding 비중이 크다 — SmolLM2-135M은 21%, Qwen2.5-0.5B는 vocab이 15만이라 28%. 세 모델 모두 embedding과 lm_head를 공유(tied)하는 config라서 이 숫자가 한 번만 든다. 기기 전용으로 vocab을 줄이면(도메인 특화 tokenizer) embedding 메모리와 토큰당 lm_head 비용이 비례해서 준다. 대신 같은 문장이 더 많은 토큰이 되어 decode 횟수가 늘 수 있다 — 이 trade-off는 B8·L 모듈에서 다룬다.

**모델팀에게 하는 말 (SLM)**: "embedding·lm_head는 tie하라. vocab은 기기 과제에 필요한 만큼. embedding을 int8(또는 그 이하)로 양자화해도 되는지 확인하자. 총 가중치 바이트 ÷ LPDDR 대역폭(약 17 GB/s 가정)이 토큰당 지연의 하한이다 (D5)."

**자동 검사**: R15는 initializer 총 바이트를 flash 예산과 비교한다. 언어 모델이면 `Gather`가 읽는 큰 initializer(embedding 테이블)를 따로 보고하도록 늘릴 수 있다.

---

## 10. 고친 효과를 한 장에 — CPU, CoreML(NPU 프록시), TFLite, peak

### 10.1 예제 12 — ORT CPU vs CoreML EP: 지연과 partition

무엇을 확인하는 코드인지: 세 변형을 (a) ORT CPU EP 단일 스레드, (b) CoreML EP + CPU로 돌려 지연을 재고, 프로파일링에서 EP별 kernel 개수(= 그래프가 몇 조각으로 나뉘었나)를 센다. CoreML EP는 F5 6.4절처럼 **NPU 컴파일러 partitioner의 프록시**로만 쓴다.

```python
# ex_ep.py — 세 변형: ORT CPU 지연 vs CoreML EP(+CPU) 지연, EP별 kernel 수 (CoreML = NPU 대용 '프록시')
import onnxruntime as ort, numpy as np, json, collections, warnings
from i2_bench import ort_ms
warnings.filterwarnings("ignore")
def ep_kernels(path, feeds):
    so = ort.SessionOptions(); so.enable_profiling = True; so.log_severity_level = 3
    so.intra_op_num_threads = 1; so.profile_file_prefix = f"prof_{path[:-5]}"
    s = ort.InferenceSession(path, so, providers=["CoreMLExecutionProvider", "CPUExecutionProvider"])
    for _ in range(5): s.run(None, feeds)
    ev = [e for e in json.load(open(s.end_profiling())) if e["name"].endswith("_kernel_time")]
    return collections.Counter(e["args"]["provider"].replace("ExecutionProvider", "") for e in
                               {e["name"]: e for e in ev}.values())
for name in ["naive", "partial", "fixed"]:
    x = np.load(f"{name}_io.npz")["x"][:1]; feeds = {"x": x}
    cpu = ort_ms(f"{name}.onnx", feeds, n=100)
    cml = ort_ms(f"{name}.onnx", feeds, n=100, providers=("CoreMLExecutionProvider", "CPUExecutionProvider"))
    print(f"{name:8s} CPU EP {cpu:6.3f} ms | CoreML+CPU {cml:6.3f} ms | kernels {dict(ep_kernels(f'{name}.onnx', feeds))}")
```

```text
naive    CPU EP  5.961 ms | CoreML+CPU 11.187 ms | kernels {'CoreML': 17, 'CPU': 21}
partial  CPU EP  2.676 ms | CoreML+CPU  0.230 ms | kernels {'CoreML': 1}
fixed    CPU EP  0.823 ms | CoreML+CPU  0.154 ms | kernels {'CoreML': 1}
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<text x="20" y="24" font-size="14">세 변형의 지연 (ms, 로그 축) — 파랑: ORT CPU EP · 주황: CoreML EP + CPU</text> <line x1="150.0" y1="40" x2="150.0" y2="250" stroke="#888" stroke-dasharray="3 3"/> <text x="150.0" y="266" font-size="12" text-anchor="middle">0.1</text> <line x1="350.0" y1="40" x2="350.0" y2="250" stroke="#888" stroke-dasharray="3 3"/> <text x="350.0" y="266" font-size="12" text-anchor="middle">1</text> <line x1="550.0" y1="40" x2="550.0" y2="250" stroke="#888" stroke-dasharray="3 3"/> <text x="550.0" y="266" font-size="12" text-anchor="middle">10</text> <text x="20" y="78" font-size="13">naive</text> <rect x="150" y="50" width="355.1" height="22" fill="#4a7bd0" fill-opacity="0.75"/> <text x="511.1" y="66" font-size="12">5.961</text>
<rect x="150" y="76" width="409.7" height="22" fill="#e08a3c" fill-opacity="0.75"/> <text x="554" y="91" font-size="12" text-anchor="end">11.187 · CoreML 17 + CPU 21 조각</text> <text x="20" y="146" font-size="13">partial</text> <rect x="150" y="118" width="285.5" height="22" fill="#4a7bd0" fill-opacity="0.75"/> <text x="441.5" y="134" font-size="12">2.676</text> <rect x="150" y="144" width="72.3" height="22" fill="#e08a3c" fill-opacity="0.75"/> <text x="228.3" y="160" font-size="12">0.230 · CoreML 1</text> <text x="20" y="214" font-size="13">fixed</text> <rect x="150" y="186" width="183.1" height="22" fill="#4a7bd0" fill-opacity="0.75"/> <text x="339.1" y="202" font-size="12">0.823</text> <rect x="150" y="212" width="37.5" height="22" fill="#e08a3c" fill-opacity="0.75"/>
<text x="193.5" y="228" font-size="12">0.154 · CoreML 1</text>
<line x1="150" y1="40" x2="150" y2="250" stroke="currentColor"/><text x="20" y="290" font-size="12">CoreML은 Apple NPU/GPU/CPU 중 무엇을 썼는지 이 로그로는 모른다 — "partition 수" 프록시로만 본다</text></svg>
```

그림 9 — 예제 12 (로그 축). 막대 끝 글자는 EP별 kernel 수.

출력에서 볼 것:

- **naive는 CoreML 17조각 + CPU 21노드로 쪼개져, 가속기를 붙였는데 CPU 단독보다 1.9배 느리다** (11.2 vs 6.0 ms). 경계마다 데이터를 넘기는 비용이 가속 이득을 다 먹었다. E5 예제 9의 시뮬레이션이 예측한 현상이 실제 런타임에서 그대로 나왔다.
- **partial은 한 덩어리**(CoreML 1)로 위임되어 CPU 대비 약 12배 빠르다. op 규칙(R1~R4, R13)만 지켜도 "가속기에 통째로 올라가는" 모델이 된다.
- **fixed는 CPU에서 partial의 3.3배 빠르다** (MAC 41.6 → 11.9 M). CoreML에서도 0.230 → 0.154 ms. 구조 규칙(R9 stem stride)의 효과다.
- 다른 시각에 한 번 더 돌렸을 때(부하가 더 높았다) 값은 naive 10.6/22.9, partial 5.0/0.32, fixed 1.6/0.28 ms였다. 절대값은 두 배 가까이 흔들렸지만 **순서와 조각 수는 같았다.** 보고할 때는 이런 반복 결과의 범위를 같이 적는다.
- CoreML이 Neural Engine을 썼는지 GPU를 썼는지는 이 로그로 알 수 없다. 이 숫자를 "Apple NPU 성능"이라고 말하면 안 되고, "가속기 위임이 한 덩어리로 되느냐"의 증거로만 쓴다.

### 10.2 한 장 요약

| 지표 | naive | partial | fixed | 어느 규칙이 바꿨나 |
|---|---|---|---|---|
| float 정확도 (합성 과제) | 0.847 | 0.862 | 0.867 | — (거의 같음) |
| ONNX 노드 수 (Constant 제외) | 108 | 23 | 23 | R1~R3, R10, R13 |
| linter ERROR / WARN | 4 / 8 | 1 / 2 | 0 / 1 | — |
| MAC | 41.6 M | 41.6 M | 11.9 M | R9·R0 (stem stride 2) |
| peak activation (int8) | 919 KB | 689 KB | 184 KB | R9 |
| 예산 384 KB 안? | 아니오 | 아니오 | 예 | R9 |
| 16-정렬 MAC 낭비 | 11% | 11% | 0% | R6 |
| ORT CPU 지연 | 5.96 ms | 2.68 ms | 0.82 ms | 전부 |
| CoreML EP 조각 / 지연 | 17+21 / 11.19 ms | 1 / 0.23 ms | 1 / 0.15 ms | R1, R4 |
| TFLite int8 크기 | 96.3 KB | 55.3 KB | 60.3 KB | R3, R13, SE 제거 |
| int8 SQNR / 정확도 | −0.5 dB / 0.197 | 42.3 dB / 0.853 | 39.3 dB / 0.860 | R12 |

이 표가 모델팀과의 회의에서 보여 줄 한 장이다. 정확도는 그대로인데 지연 7배, peak 5배, int8 정확도는 "죽음 → 정상". 그리고 각 개선이 **어느 규칙에서 왔는지** 오른쪽 칸이 말해 준다. 규칙에 근거가 붙는 순간 모델팀은 규칙을 "제약"이 아니라 "공짜 성능"으로 본다.

---

## 11. 규칙을 전달하는 법 — 모델 카드, CI gate, 설계 리뷰

규칙집은 문서로 주면 읽히지 않는다. 지켜지게 만드는 장치가 세 개 필요하다: **모델과 함께 다니는 HW 정보(모델 카드)**, **자동으로 막는 관문(CI gate)**, **사람이 판단하는 자리(설계 리뷰)**. SSD 펌웨어로 치면 릴리스 노트의 리소스 사용량 표, 빌드 서버의 static analysis gate, 아키텍처 리뷰다.

### 11.1 예제 13 — 모델 카드의 HW 필드를 자동으로

모델 카드(model card)는 원래 모델의 용도·데이터·평가·한계를 적는 문서(Mitchell et al., 2019)다. embedded 쪽에서는 여기에 **HW 필드**를 붙인다. 사람이 손으로 쓰면 틀리므로 linter가 채운다.

```python
# ex_card.py — linter 결과로 "HW 모델 카드"를 자동 생성 (측정값 칸은 벤치 결과를 넣는다)
import json, onnx, collections, sys
from i2_lint import lint
from i2_profile import MCU_NPU as P
path = sys.argv[1]; m = onnx.load(path); findings, s = lint(path, P)
card = {
    "model": path, "opset": m.opset_import[0].version, "target_profile": P["name"],
    "inputs": {i.name: "x".join(str(d.dim_value or d.dim_param) for d in i.type.tensor_type.shape.dim) for i in m.graph.input},
    "ops_used": dict(collections.Counter(n.op_type for n in m.graph.node if n.op_type != "Constant")),
    "MMAC": s["MMAC"], "weight_KB_int8": s["weight_KB"], "peak_activation_KB_int8": s["peak_KB"],
    "sram_budget_KB": P["sram_kb"] - P["reserved_kb"], "npu_cpu_boundaries": s["npu_cpu_boundaries"],
    "lint": {"errors": [f"{r}: {msg}" for sev, r, msg, _ in findings if sev == "ERROR"],
             "warnings": [f"{r}: {msg}" for sev, r, msg, _ in findings if sev == "WARN"]},
    "waivers": {"R8 depthwise": "toy 모델 기준 경고. 벤더 성능 모델/실기기 LUT로 확인 후 닫는다 (D4, E5)"},
    "measured": {"ort_cpu_ms": None, "tflite_int8_KB": None, "int8_sqnr_dB": None, "target_latency_ms": None},
}
print("{\n" + ",\n".join(f" {json.dumps(k)}: {json.dumps(v, ensure_ascii=False)}" for k, v in card.items()) + "\n}")
```

```sh
.venv/bin/python ex_card.py fixed.onnx
```

```text
{
 "model": "fixed.onnx",
 "opset": 17,
 "target_profile": "always-on micro-NPU (Ethos-U55급 가정)",
 "inputs": {"x": "1x1x40x98"},
 "ops_used": {"Conv": 9, "Clip": 9, "Add": 3, "ReduceMean": 1, "Gemm": 1},
 "MMAC": 11.86,
 "weight_KB_int8": 32.6,
 "peak_activation_KB_int8": 183.8,
 "sram_budget_KB": 384,
 "npu_cpu_boundaries": 0,
 "lint": {"errors": [], "warnings": ["R8 depthwise: MAC 10%인데 toy cycle 74%"]},
 "waivers": {"R8 depthwise": "toy 모델 기준 경고. 벤더 성능 모델/실기기 LUT로 확인 후 닫는다 (D4, E5)"},
 "measured": {"ort_cpu_ms": null, "tflite_int8_KB": null, "int8_sqnr_dB": null, "target_latency_ms": null}
}
```

출력에서 볼 것: `ops_used`가 Conv·Clip·Add·ReduceMean·Gemm 다섯 종뿐이다 — 이 모델을 돌리는 데 필요한 커널 목록이 곧 이 다섯 개라는 뜻이고, TFLite Micro의 op resolver에 등록할 목록(F2)이나 ORT minimal build의 op 목록(F5 9.3)과 같다. `measured`는 비워 두고 CI의 다음 단계(양자화·기기 측정)가 채운다. 모델 카드에 넣을 HW 필드를 정리하면:

| 필드 | 누가 채우나 | 왜 필요한가 |
|---|---|---|
| 입력 shape·dtype·레이아웃, 특징 설정 (mel 수, hop, 정규화 상수) | export 스크립트 | DSP/펌웨어와의 계약 (R0, R10) |
| ops_used (종류·개수) | linter | 커널 등록, fallback 확인 (R1) |
| MAC, 가중치 바이트, peak activation | linter | 예산 대조 (R9, R15) |
| NPU/CPU 경계 수, lint 결과, waiver | linter + 리뷰 | 위험 추적 |
| int8 크기, SQNR(층별 최저값 포함), float vs int8 정확도 | 양자화 CI 단계 | R12 (C8) |
| 기기 지연 p50/p99, 추론당 에너지 | 기기 farm | 실제 예산 대조 (I1, D6, D7) |
| 컴파일러·런타임 버전, 프로파일 버전 | CI | 재현성 (J5) |

### 11.2 CI gate — linter의 exit code가 관문이다

linter는 ERROR가 하나라도 있으면 exit code 1로 끝난다 (3.4절 마지막 줄). 그래서 CI에 한 줄로 붙는다.

```sh
.venv/bin/python i2_lint.py fixed.onnx > /dev/null; echo "fixed exit=$?"
.venv/bin/python i2_lint.py partial.onnx > /dev/null; echo "partial exit=$?"
```

```text
fixed exit=0
partial exit=1
```

출력에서 볼 것: fixed는 WARN(R8)만 있어 통과, partial은 R9(peak SRAM) ERROR로 막힌다. CI 파이프라인 예시는 아래와 같다 (GitHub Actions 문법의 스케치 — 실제 job 이름·러너는 팀 환경에 맞춘다, O3).

```text
# .github/workflows/model-gate.yml (스케치)
on: [pull_request]
jobs:
  hw-gate:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: pip install -r requirements-gate.txt          # onnx, onnxruntime, tensorflow (고정 버전)
      - run: python export.py --out model.onnx              # 모델팀 export 스크립트 (static shape)
      - run: python i2_lint.py model.onnx                   # 1단계: ERROR면 여기서 실패 (수 초)
      - run: python quantize_and_check.py model.onnx       # 2단계: int8 변환, SQNR·정확도 하한 검사 (수 분)
      - run: python ex_card.py model.onnx > model_card.json
      - uses: actions/upload-artifact@v4
        with: { name: model-card, path: model_card.json }
# 3단계(야간): 기기 farm에서 지연·전력 측정 → model_card.measured 채움, 예산 초과면 알림
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 270">
<text x="20" y="24" font-size="14">모델 PR의 HW gate — 싼 검사를 앞에, 비싼 검사를 뒤에</text>
<rect x="20" y="50" width="110" height="56" rx="6" fill="none" stroke="currentColor" stroke-width="1.5"/> <text x="75" y="74" font-size="13" text-anchor="middle">모델 PR</text> <text x="75" y="93" font-size="12" text-anchor="middle">export</text>
<rect x="160" y="50" width="140" height="56" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="230" y="74" font-size="13" text-anchor="middle">① linter</text> <text x="230" y="93" font-size="12" text-anchor="middle">수 초 · 매 PR</text>
<rect x="330" y="50" width="150" height="56" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="405" y="74" font-size="13" text-anchor="middle">② 양자화 검사</text> <text x="405" y="93" font-size="12" text-anchor="middle">SQNR·정확도 · 수 분</text>
<rect x="510" y="50" width="150" height="56" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="585" y="74" font-size="13" text-anchor="middle">③ 기기 farm</text> <text x="585" y="93" font-size="12" text-anchor="middle">지연·전력 · 야간</text>
<line x1="130" y1="78" x2="157" y2="78" stroke="currentColor"/> <polygon points="160,78 152,73 152,83" fill="currentColor"/>
<line x1="300" y1="78" x2="327" y2="78" stroke="currentColor"/> <polygon points="330,78 322,73 322,83" fill="currentColor"/>
<line x1="480" y1="78" x2="507" y2="78" stroke="currentColor"/> <polygon points="510,78 502,73 502,83" fill="currentColor"/>
<line x1="230" y1="106" x2="230" y2="150" stroke="#d0564a"/> <text x="230" y="168" font-size="12" text-anchor="middle">ERROR → PR 차단</text> <text x="230" y="186" font-size="12" text-anchor="middle">WARN → waiver 필요</text>
<line x1="405" y1="106" x2="405" y2="150" stroke="#d0564a"/> <text x="405" y="168" font-size="12" text-anchor="middle">SQNR·정확도 하한 미달</text> <text x="405" y="186" font-size="12" text-anchor="middle">→ PR 차단</text>
<line x1="585" y1="106" x2="585" y2="150" stroke="#e08a3c"/> <text x="585" y="168" font-size="12" text-anchor="middle">예산 초과 → 알림</text> <text x="585" y="186" font-size="12" text-anchor="middle">+ 프로파일 상수 보정</text>
<rect x="160" y="210" width="500" height="40" rx="6" fill="none" stroke="#888" stroke-width="1.5" stroke-dasharray="5 3"/> <text x="410" y="235" font-size="13" text-anchor="middle">model_card.json — 각 단계가 자기 필드를 채운다</text>
</svg>
```

그림 10 — HW gate의 세 단계. 앞 단계일수록 싸고 자주 돌고, 뒤 단계일수록 비싸고 정확하다.

**WARN과 waiver**: 모든 규칙이 ERROR면 아무것도 못 지나간다. WARN은 "이유를 적고 승인받으면 통과"다. waiver는 모델 카드에 남아서, 칩이나 컴파일러가 바뀌었을 때 다시 볼 목록이 된다. 이 노트의 fixed는 R8을 waiver로 닫았다: "toy 모델의 경고이며, 기기 LUT로 depthwise 블록 지연을 확인한 뒤 닫는다." 이 방식은 펌웨어의 MISRA deviation 기록과 똑같다.

**gate가 실패했을 때 메시지가 처방이어야 한다.** "R9 FAIL"이 아니라 "peak 689 KB @ 첫 블록 pointwise > 384 KB. 앞단 stride·채널 축소"처럼 위치와 처방을 준다 (예제 2의 비용 칸). 모델팀이 embedded 엔지니어를 부르지 않고 스스로 고칠 수 있어야 gate가 병목이 안 된다.

### 11.3 설계 리뷰 — 학습 전에 한 번, 배포 전에 한 번

자동 검사가 못 하는 것은 사람이 한다. 두 번의 리뷰를 권한다.

| 시점 | 무엇을 보나 | embedded 쪽 준비물 |
|---|---|---|
| **학습 전** (아키텍처 제안 단계) | 입력 해상도·특징 설정(R0), 블록 종류(R2, R8), 해상도별 채널 계획(R9), 스트리밍 여부(R14), 대상 코어(MCU/DSP/NPU, I3) | 타깃 프로파일, 블록 LUT(C7), 예산 표(I1), 이 노트의 규칙표 |
| **배포 전** (학습 끝, 양자화 후) | linter 결과와 waiver, int8 SQNR·정확도, 컴파일러 리포트(fallback, SRAM 사용), 기기 지연 | 모델 카드, 벤더 컴파일러 리포트, 기기 측정 |

학습 전 리뷰가 훨씬 중요하다. 규칙 위반을 학습 후에 고치면 재학습이지만, 학습 전에 고치면 설정 파일 한 줄이다. 실무 팁 몇 가지:

- 모델팀에게 **linter를 로컬에서 돌리는 명령 한 줄**을 준다 (`python i2_lint.py model.onnx`). 무작위 가중치 모델로도 MAC·peak·op 검사는 다 된다 — 학습 전에 돌릴 수 있다는 뜻이다 (예제 8이 그렇게 했다).
- 규칙에는 항상 **근거 측정**을 링크한다 (이 노트의 예제처럼). "NPU가 싫어해서"가 아니라 "64 → 65에서 25% 느려지는 측정"이 설득한다.
- 규칙은 **버전 관리**한다. 칩 리비전·컴파일러 업데이트로 whitelist와 비용 상수가 바뀐다. 프로파일 파일에 버전을 붙이고, 모델 카드에 어느 프로파일로 검사했는지 남긴다.
- 반대 방향 피드백도 받는다. 모델팀이 "이 op가 정확도에 꼭 필요하다"고 하면, 그 op의 NPU 지원을 벤더에게 요청하거나 CPU 커널을 최적화하는 것도 co-design이다 (I6).

---

## 12. 임베디드 관점에서 다시 보기

**규칙은 결국 C 코드의 모양을 정한다.** micro-NPU 배포의 펌웨어는 대략 이렇다 (TFLM + Ethos-U 구조를 단순화한 의사코드 — 실제 API 이름은 벤더 SDK를 따른다).

```
// 규칙을 지킨 모델이 펌웨어에서 어떤 모양이 되나 (의사코드, 실제 API는 SDK마다 다르다)
static uint8_t arena[384 * 1024] __attribute__((aligned(16)));   // R9: peak이 이 안에 들어와야 한다
static int8_t  feat[40 * 98];                                    // R0·R10: DSP가 (1,1,40,98) int8로 써 준다

void kws_task(void) {
    for (;;) {
        wait_for_feature_frame();                  // DSP가 log-mel 정규화·int8 양자화까지 끝낸다 (R12)
        model_set_input(feat, sizeof feat);        // 레이아웃 변환 없음 (R10)
        model_invoke(arena);                       // 명령열 하나로 NPU 실행 — CPU 경계 0회 (R1, R4, R11)
        const int8_t *logits = model_output();     // softmax 없음, 펌웨어가 argmax·threshold (R13)
        int best = argmax_i8(logits, 5);
        if (best != BACKGROUND && logits[best] > THRESH_Q) post_event(best);
    }
}
```

규칙을 어기면 이 코드가 어떻게 망가지는지 짝을 지어 보면:

| 규칙 위반 | 펌웨어에서 생기는 일 |
|---|---|
| R1 미지원 op | `model_invoke`가 NPU → CPU → NPU를 오가며 CPU 커널(TFLM reference)을 부른다. 지연이 몇 배, CPU가 깨어 있는 시간만큼 전력 증가 |
| R4 동적 shape | 컴파일러가 거부하거나, 최대 크기로 arena를 잡아 SRAM 낭비 |
| R9 peak 초과 | 링크 단계에서 `.bss` 초과, 또는 arena를 외부 메모리에 둬서 대역폭·전력 폭증 |
| R10 layout | 입력 앞에 transpose 루프가 생기거나 NPU 안에서 별도 copy pass |
| R12 넓은 범위 op | 정확도 붕괴. 펌웨어 쪽에서는 "가끔 오인식"으로 보여서 원인 찾기가 가장 어렵다 |
| R13 softmax/LN | CPU float 코드가 섞여 FPU를 켜야 하고, 지연 jitter가 커진다 (D6) |
| R14 비인과 | 이벤트가 프레임 주기 × lookahead만큼 늦게 나온다 — 사용자 체감 지연 |

Don의 경험과 연결하면, 이건 SSD 컨트롤러 펌웨어에서 "HW 가속 엔진(암호화, ECC, DMA)이 처리할 수 있는 형태로 데이터 경로를 설계하라, 아니면 펌웨어가 소프트웨어로 처리해야 하고 성능이 무너진다"와 같은 이야기다. 엔진의 지원 범위(op whitelist), 정렬 요구(채널 정렬), 버퍼 크기(SRAM peak), 엔진 전환 비용(fallback 경계)을 문서와 측정으로 정리해서 상위 설계자에게 주는 역할 — 그 상위 설계자가 이번에는 모델팀이다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 특징 추출(log, 정규화)을 int8 모델 안에 넣음 | float 정확도는 정상, int8에서 우연 수준 | 넓은 범위 입력이 양자화되면서 작은 값이 0이 되고, log가 오차를 폭발시킴 (예제 10) | 특징 추출은 DSP에서, 모델 입력은 정규화된 값으로 (R12) |
| GELU·LayerNorm을 습관적으로 씀 | 컴파일러 리포트에 CPU fallback, 가속기가 오히려 느림 | Erf 미지원, 그래프가 여러 조각 (예제 12) | ReLU6·BN으로 교체 후 재학습 (R1, R2, R13) |
| BN을 활성화 뒤에 둠 | BatchNormalization 노드가 배포 그래프에 남음 | 비선형을 사이에 두면 fold 불가 | conv → BN → act 순서 (R3) |
| 채널을 60·90처럼 아무 수로 | MAC 대비 지연이 비례하지 않음, 63이 64보다 느림 | 정렬 단위까지 0 패딩 (예제 5) | `make_divisible(c, 16)` (R6) |
| "MAC 예산 안이니 OK" | 컴파일러가 SRAM 부족으로 실패, 또는 외부 메모리 spill | peak activation은 MAC과 별개, 앞쪽 고해상도 구간에 몰림 (예제 7) | stem stride, 앞쪽 채널 축소 (R9) |
| `dynamic_axes`를 기본으로 켬 | NPU 컴파일 실패 또는 최대 크기 할당 | 컴파일 시점에 tiling 불가 | 고정 창으로 export (R4) |
| linter 통과 = 배포 가능으로 착각 | 컴파일러가 특정 속성 조합(stride, 커널 크기) 거부 | whitelist가 op 이름만 봄 | 벤더 컴파일러 리포트를 CI 다음 단계에 (11.2) |
| 비용 상수를 실측 없이 계속 씀 | linter의 추정 비용과 기기 측정이 몇 배 차이 | 프로파일 상수는 가정값 | 기기 측정으로 프로파일 보정 (그림 1) |
| 스트리밍 모델에 대칭 padding | 이벤트가 늘 100 ms 넘게 늦음 | 미래 프레임을 기다림 (예제 9) | causal padding (R14) |
| 규칙을 근거 없이 문서로만 줌 | 모델팀이 무시, 배포 직전에 재작업 | 비용이 안 보임 | 규칙마다 측정 근거 + 자동 검사 + CI gate (11절) |

---

## 14. 면접에서 이렇게 말한다

**Q.** "What design guidelines would you give a model team targeting a micro-NPU?"

**A.** 다섯 개로 시작한다: 지원 op만(특히 ReLU6, BN은 conv 바로 뒤), static shape, 채널 16 배수, 앞쪽에서 일찍 downsample해서 peak activation을 SRAM 안에, 특징 추출은 모델 밖에서. 각 규칙에는 측정 근거를 붙이고, 사람 기억이 아니라 linter와 CI gate로 지키게 한다.

> I'd give them a short rule set, each rule backed by a measurement: use only ops on the NPU's support list — ReLU6 instead of GELU, BatchNorm directly after conv so it folds; keep all shapes static; round channel counts to multiples of 16; downsample early so peak activation fits in SRAM; and keep feature extraction like log-mel outside the int8 graph. Then I'd turn those rules into a linter that runs on every model PR, so violations show up before training, not at deployment.

**Q.** "Why should channel counts be multiples of 16?"

**A.** MAC 배열과 SIMD는 채널을 16(또는 32)개씩 묶어 처리하고, 남는 칸은 0으로 채워 계산한다. 그래서 65채널은 80채널만큼 일한다 — 1×1 conv에서 34% 낭비. 이 노트북 CPU에서 int8 1×1 conv를 재 봐도 64 → 65에서 25% 느려졌고, 63이 64보다 느렸다. 정렬 단위는 칩마다 다르니 벤더 문서와 실측으로 정한다.

> Because the MAC array or SIMD unit processes channels in fixed groups — 16 or 32 is common — and pads the remainder with zeros. A 65-channel layer costs as much as an 80-channel one; for a 1x1 conv that's 34 percent wasted MACs. Even on a laptop CPU, an int8 1x1 conv got 25 percent slower going from 64 to 65 channels, and 63 channels was slower than 64. The exact granule is target-specific, so I confirm it with the vendor docs and a channel sweep.

**Q.** "GELU or ReLU6 on an int8 NPU?"

**A.** 기본은 ReLU6. 이유 둘: (1) ReLU6는 requant 뒤 clamp라 공짜이고 거의 모든 NPU가 지원한다. GELU는 Erf 등으로 분해되어 미지원이면 CPU fallback이다. (2) ReLU6의 범위는 [0, 6]으로 고정이라 int8 scale이 outlier에 끌려가지 않는다 — 측정에서 int8 SQNR이 50 dB 대 22 dB였다. GELU가 정확도에 꼭 필요하면, NPU가 LUT로 지원하는지 확인하고 SoC 쪽 NPU에 두는 걸 검토한다.

> ReLU6 by default. It's free on an int8 NPU — it's just the clamp after requantization — and it's universally supported, while GELU exports as Erf plus several elementwise ops and often falls back to the CPU. ReLU6 also has a fixed [0, 6] range, so the int8 scale isn't dragged around by outliers; in my test the quantized output SQNR was about 50 dB for ReLU6 versus 22 dB for GELU. If GELU really buys accuracy, I'd check whether the NPU implements it as a lookup table, or place that model on the bigger SoC NPU.

**Q.** "How do you catch NPU-unfriendly models early?"

**A.** 세 단계. (1) 학습 전 설계 리뷰에서 규칙표와 무작위 가중치 export로 linter를 돌린다 — MAC, peak, op 검사는 학습 없이 된다. (2) 모든 모델 PR에 linter를 CI gate로 건다 — ERROR면 차단, WARN은 waiver. (3) 양자화 SQNR 검사와 야간 기기 farm 측정으로 linter의 비용 상수를 계속 보정한다.

> Three layers. First, at architecture review — before training — I run the linter on a randomly initialized export; op support, MACs, and peak memory don't need trained weights. Second, the same linter runs as a CI gate on every model PR: errors block, warnings need a written waiver that goes into the model card. Third, a quantization check and nightly on-device runs measure SQNR and latency, and I use those measurements to recalibrate the linter's cost constants.

**Q.** "The model fits the MAC budget but not SRAM — what do you change?"

**A.** peak activation이 어디서 나오는지 먼저 본다. 거의 항상 해상도가 높은 앞쪽 블록이고, residual 때문에 입력·중간·출력 세 텐서가 동시에 산다. 그래서 첫 처방은 stem stride 2 같은 이른 downsampling — 텐서 면적이 4분의 1이 된다. 실험에서 peak가 689 KB에서 184 KB가 됐고 정확도는 유지됐다. 다음은 앞쪽 채널·expand ratio 축소, 앞쪽 skip 제거, 실행 순서·in-place 최적화, 마지막으로 patch 기반 추론.

> I'd find where the peak occurs — it's almost always an early, high-resolution block where the residual input, the depthwise output, and the pointwise output are all live at once. The first fix is to downsample earlier, for example a stride-2 stem, which quarters every tensor in that stage; in my experiment peak activation went from 689 KB to 184 KB with no accuracy loss. After that: fewer channels or a smaller expansion ratio in the early stages, dropping early skip connections, letting the compiler reorder and run ops in place, and finally patch-based inference.

**Q.** "Your linter says a model is fine but the device is slow. What now?"

**A.** linter는 필요조건만 본다. 벤더 컴파일러 리포트에서 fallback op와 그 이유(속성 조합), 층별 사이클 추정을 보고, 기기에서 층별 프로파일을 잰다. 차이가 나는 층을 찾으면 그걸 프로파일에 새 규칙이나 비용 상수로 반영한다 — linter는 측정으로 자라는 도구다.

> The linter only checks necessary conditions with estimated costs. I'd read the vendor compiler report for fallbacks and per-layer cycle estimates, profile per layer on the device, and compare. Whatever explains the gap — say depthwise with stride 2 isn't supported natively — becomes a new rule or a corrected cost constant in the target profile. The linter grows from measurements.

---

## 15. 직접 해보기

1. **손계산**: 1×1 conv 40 → 72를 정렬 단위 16, 32인 NPU에 올릴 때 MAC 낭비율을 각각 구하라. 정답: align 16 → padded 48 × 80 = 3840 대비 2880, 낭비 25%. align 32 → 64 × 96 = 6144 대비 2880, 낭비 53.1%.
2. **손계산**: 입력 49 × 10(MFCC), stem conv stride 1, 64채널, 첫 DS 블록에 residual이 있다. int8 peak(입력 + dw 출력 + pw 출력)은? stem stride 2(25 × 5)로 바꾸면? 정답: 49 × 10 × 64 = 31,360 B × 3 = 94,080 B. stride 2이면 25 × 5 × 64 = 8,000 B × 3 = 24,000 B.
3. **손계산**: k = 5, dilation 1·2·4, 대칭 padding인 TCN의 lookahead 프레임은? IMU 50 Hz면 몇 ms? 정답: 대칭 padding = 2·dilation이므로 2 + 4 + 8 = 14 프레임, 50 Hz(20 ms)면 280 ms.
4. **코드**: R12를 위치 기반으로 고쳐라 — 그래프 입력에서 첫 Conv까지의 경로에 Log·Exp·Div가 있으면 ERROR, 그 뒤면 WARN. naive에서 ERROR가 나오는지 확인하라. 힌트: 노드 출력 → 소비 노드 맵을 만들어 입력에서 BFS, Conv를 만나면 멈춘다.
5. **코드**: `i2_graph.load_static`이 initializer만 입력으로 받는 `Identity` 노드를 상수로 접도록 고쳐서, 예제 8의 peak 184.2 KB가 183.8 KB가 되는지 확인하라. 힌트: Identity의 입력이 initializer면 출력 이름을 initializer 집합에 추가하고 live 계산에서 뺀다.
6. **코드**: 예제 6을 int8(`QLinearConv`)로 다시 해서 층당 고정비가 fp32보다 큰지 작은지 재 보라. 그리고 그 값을 `MCU_NPU["per_op_us"]` 대신 쓸 수 없는 이유를 한 문장으로 적어라. 힌트: CPU 런타임의 디스패치 비용과 NPU 명령열의 층당 비용은 다른 하드웨어의 다른 메커니즘이다 — 프로파일 상수는 타깃에서 잰다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| HW-friendly design | HW 친화 설계 | 정확도를 거의 잃지 않으면서 타깃 HW에서 싸게 도는 구조를 고르는 것 |
| op whitelist | 지원 op 목록 | NPU 컴파일러가 가속기로 내리는 op(와 속성 조건)의 목록 |
| CPU fallback | CPU로 떨어짐 | 미지원 op를 CPU가 실행하는 것. 경계마다 동기화·복사 비용 |
| partition | 그래프 조각 | 같은 장치에서 연속 실행되는 노드 묶음. 조각 수 = 경계 수 + 1 |
| target profile | 타깃 프로파일 | 칩의 지원 op, 정렬, SRAM, 비용 상수를 적은 설정 파일 |
| linter | 정적 검사기 | 실행하지 않고 그래프만 보고 규칙 위반을 찾는 도구 |
| channel alignment | 채널 정렬 | 채널 수를 HW 처리 단위(16, 32)의 배수로 맞추는 것 |
| padding waste | 패딩 낭비 | 정렬을 위해 0을 채운 칸에 쓰인 MAC·메모리의 비율 |
| peak activation | 최대 활성값 메모리 | 실행 중 동시에 살아 있는 activation 바이트의 최댓값 |
| liveness | 생존 구간 | 텐서가 만들어진 시점부터 마지막으로 쓰이는 시점까지 |
| in-place | 제자리 실행 | 출력이 입력 버퍼를 덮어써 메모리를 아끼는 실행 |
| layout churn | 레이아웃 뒤섞기 | Transpose·Reshape·Concat이 반복되어 데이터 복사가 늘어나는 것 |
| lookahead | 미래 프레임 의존 | 출력을 내기 위해 기다려야 하는 미래 입력 프레임 수 |
| causal | 인과적 | 현재와 과거 입력만 쓰는 (미래를 보지 않는) 연산 |
| SQNR | 신호 대 양자화 잡음비 | float 출력 대비 양자화 출력의 오차를 dB로 잰 것 |
| waiver | 예외 승인 | 경고를 이유와 함께 승인해 통과시키고 기록하는 절차 |
| model card | 모델 카드 | 모델의 용도·평가·한계(+ HW 필드)를 적는 표준 문서 |
| CI gate | CI 관문 | 조건을 만족하지 않으면 병합을 막는 자동 검사 단계 |
| tied embedding | 공유 임베딩 | 입력 embedding과 출력 lm_head가 같은 가중치를 쓰는 것 |

---

## 17. 요약 & 체크리스트

embedded AI 엔지니어가 모델팀에 주는 것은 "빠르게 해 달라"는 부탁이 아니라 **근거가 붙은 규칙 + 자동 검사기 + 전달 절차**다. 규칙은 HW 자원별로 묶인다: MAC 배열(채널 정렬, 작은 op, depthwise), 온칩 SRAM(입력 크기, peak, layout), 컴파일러·런타임(지원 op, static shape, rank, 제어 흐름, softmax·LN), 정수 산술(활성화, BN fold, 양자화 친화), 시스템(causal, 가중치·vocab). 이 노트의 실험에서 같은 과제의 naive 설계는 linter ERROR 4개, CoreML 17조각 + CPU 21노드, peak 919 KB, int8 정확도 붕괴였고, op만 고친 partial은 가속기에 한 덩어리로 올라갔지만 SRAM을 넘었고, stem stride와 채널 정렬까지 고친 fixed는 CPU 지연 7배 단축·peak 5배 감소·int8 정확도 유지로 모든 ERROR를 통과했다. 규칙은 타깃의 함수이므로 프로파일로 관리하고, 기기 측정으로 비용 상수를 계속 보정한다.

- [ ] 모델팀에게 줄 규칙 16개를 HW 이유와 함께 말할 수 있다
- [ ] 1×1 conv의 채널 패딩 낭비율을 손으로 계산할 수 있다 (예: 65채널 @ align 16 → 34%)
- [ ] residual이 있는 블록의 peak activation을 손으로 계산하고, stride로 얼마나 줄어드는지 말할 수 있다
- [ ] ReLU6가 int8 NPU에서 "공짜"인 이유를 requant clamp로 설명할 수 있다
- [ ] 그래프 안 log가 int8에서 정확도를 무너뜨리는 메커니즘을 숫자로 설명할 수 있다
- [ ] dilated conv 스택의 lookahead를 계산하고 causal padding으로 바꿀 수 있다
- [ ] ONNX + 타깃 프로파일을 받는 linter를 짜서 위반·비용·exit code를 낼 수 있다
- [ ] CoreML EP partition 수를 NPU partitioner의 프록시로 쓰되, 그 한계를 말할 수 있다
- [ ] 모델 카드의 HW 필드와 CI gate 3단계(linter → 양자화 → 기기)를 설계할 수 있다
- [ ] WARN을 waiver로 관리하고, 기기 측정으로 프로파일을 보정하는 절차를 말할 수 있다

## 참고 자료

- Arm, "Ethos-U Vela compiler" (패키지 설명과 버전별 지원 op 문서) — https://pypi.org/project/ethos-u-vela/
- TensorFlow Lite, "Post-training integer quantization" — https://www.tensorflow.org/lite/performance/post_training_integer_quant
- ONNX Runtime, "CoreML Execution Provider" — https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html
- ONNX 연산자 문서 — https://onnx.ai/onnx/operators/
- Sandler et al., "MobileNetV2: Inverted Residuals and Linear Bottlenecks", CVPR 2018 (ReLU6, `make_divisible` 관례)
- Howard et al., "Searching for MobileNetV3", ICCV 2019 (hard-swish, SE, HW-aware 탐색)
- Lin et al., "MCUNet: Tiny Deep Learning on IoT Devices", NeurIPS 2020 / "MCUNetV2", NeurIPS 2021 (peak SRAM, patch 추론)
- Zhang et al., "Hello Edge: Keyword Spotting on Microcontrollers", 2017 (KWS DS-CNN)
- Mitchell et al., "Model Cards for Model Reporting", ACM FAT 2019 (현 FAccT)
- MIT 6.5940 TinyML and Efficient Deep Learning Computing — https://efficientml.ai
- 이 노트 묶음: B1, B2, C1, C2, C6, C7, C8, D2, D4, D5, E5, F5, F8, I1, I3, O3
