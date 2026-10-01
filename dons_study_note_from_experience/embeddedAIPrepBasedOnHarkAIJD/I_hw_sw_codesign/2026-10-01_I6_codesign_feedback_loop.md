# I6. Co-design 피드백 루프 — 프로파일 → 병목 → 구조 변경 → 재측정

> **이 노트를 다 읽으면**: 프로파일 결과에서 병목을 compute·memory·fallback·양자화·pre/post·IPC로 분류하고 비용순으로 고칠 후보를 낼 수 있다 · 예산을 100배 넘는 모델을 v0 → v4 다섯 번의 "한 바퀴 한 수정"으로 예산 안에 넣는 과정을 실측 숫자로 설명할 수 있다 · 모델 수정 vs 컴파일러/런타임 수정 vs HW 설정 변경을 시간·위험으로 비교해 고를 수 있다 · 모델 파일 하나에서 예산표를 뽑는 CI 스크립트를 만들고 팀의 주간 co-design 리뷰를 굴릴 수 있다
> **JD 연결**: "Co-design model architectures that meet latency, memory, power, bandwidth constraints" · "Profile and optimize ... real-time performance" · study_prep_list **I6** 행: 피드백 루프 — 프로파일 → 병목 → 모델 구조 변경 → 재측정 ("co-design 실무") · 연결: I1(예산), I2(HW 친화 규칙), I4(전처리·후처리 배치), I5(벤치마크 harness), C7(latency LUT), C8(레이어별 diff), D2(메모리), D3(roofline), F5/F8(fallback), E8(IPC), K1/K2(프로파일링)
> **Don 기준 난이도**: 측정 → root cause → 수정 → 재측정 → sign-off는 Don이 SSD 성능 튜닝과 Apple RF NPI에서 매일 하던 일이다 / 새로 배울 것은 "모델 쪽 손잡이"(downsample, op 교체, DS-conv, 폭·해상도)와 "정확도도 매 바퀴 다시 잰다"는 ML 쪽 규율
> **선행 노트**: I1(예산), I2(HW 친화 설계), D2·D3, C8, F5·F8. 병렬로 쓰인 I1–I5와 같은 사례(손목 IMU 제스처 모델 + always-on micro-NPU)를 공유한다

---

## 0. 큰 그림 — co-design은 "한 번에 맞히는 설계"가 아니라 "루프"다

Don이 SSD 펌웨어에서 성능 이슈를 잡던 방식을 떠올려 보자. 4K random read latency가 스펙을 넘는다 → 로직 분석기와 내부 타임스탬프로 구간별 시간을 잰다 → 가장 큰 구간(예: NAND 읽기 대기가 아니라 FTL 매핑 테이블 miss)을 찾는다 → 고칠 곳을 고른다(펌웨어 캐시 정책 / 컨트롤러 설정 / 다음 칩 요청) → 같은 조건으로 다시 잰다 → 스펙 + margin 안이면 sign-off. **edge ML의 co-design 피드백 루프는 이것과 구조가 똑같다.** 다른 점은 손잡이의 종류다. 펌웨어 대신 **모델 구조**를 바꿀 수 있고, 그 대가로 **정확도**라는 측정 항목이 하나 더 생긴다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 420">
<defs><marker id="i6a1" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="20" y="22" font-size="14">co-design 피드백 루프 — 한 바퀴 = 한 가지 수정</text> <rect x="20" y="40" width="190" height="66" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="115" y="64" font-size="13" text-anchor="middle">① 예산 · 계약 (I1)</text> <text x="115" y="83" font-size="12" text-anchor="middle">latency · peak · weight · acc</text> <text x="115" y="99" font-size="12" text-anchor="middle">+ margin 규칙</text> <rect x="245" y="40" width="190" height="66" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="340" y="64" font-size="13" text-anchor="middle">② 측정 (profile)</text> <text x="340" y="83" font-size="12" text-anchor="middle">타깃 실측 + proxy + 추정</text> <text x="340" y="99" font-size="12" text-anchor="middle">budget_report 자동 실행</text> <rect x="470" y="40" width="190" height="66" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="565" y="64" font-size="13" text-anchor="middle">③ 병목 식별 · 분류</text> <text x="565" y="83" font-size="12" text-anchor="middle">compute · memory · fallback</text> <text x="565" y="99" font-size="12" text-anchor="middle">quant · pre/post · IPC</text> <line x1="210" y1="73" x2="243" y2="73" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a1)"/>
<line x1="435" y1="73" x2="468" y2="73" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a1)"/> <polygon points="565,140 655,180 565,220 475,180" fill="none" stroke="currentColor" stroke-width="1.5"/> <text x="565" y="176" font-size="12" text-anchor="middle">④ 어디를</text> <text x="565" y="192" font-size="12" text-anchor="middle">고치나?</text>
<line x1="565" y1="106" x2="565" y2="138" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a1)"/> <rect x="250" y="130" width="180" height="40" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="340" y="155" font-size="12" text-anchor="middle">모델 변경 (모델팀) · 일~주</text> <rect x="250" y="180" width="180" height="40" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<text x="340" y="205" font-size="12" text-anchor="middle">컴파일러·런타임 (벤더) · 주~월</text> <rect x="250" y="230" width="180" height="40" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="340" y="255" font-size="12" text-anchor="middle">HW 설정 (HW팀) · 일~분기</text> <line x1="490" y1="173" x2="432" y2="150" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a1)"/>
<line x1="475" y1="180" x2="432" y2="200" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a1)"/> <line x1="500" y1="200" x2="432" y2="248" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a1)"/> <rect x="20" y="180" width="190" height="66" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="115" y="204" font-size="13" text-anchor="middle">⑤ 적용 · 재측정</text>
<text x="115" y="223" font-size="12" text-anchor="middle">같은 harness · 같은 조건</text> <text x="115" y="239" font-size="12" text-anchor="middle">A/B: 이전 버전과 비교</text> <line x1="250" y1="150" x2="212" y2="200" stroke="currentColor" stroke-width="1.2" marker-end="url(#i6a1)"/> <line x1="250" y1="200" x2="212" y2="210" stroke="currentColor" stroke-width="1.2" marker-end="url(#i6a1)"/>
<line x1="250" y1="250" x2="212" y2="225" stroke="currentColor" stroke-width="1.2" marker-end="url(#i6a1)"/> <polygon points="115,280 205,320 115,360 25,320" fill="none" stroke="currentColor" stroke-width="1.5"/> <text x="115" y="316" font-size="12" text-anchor="middle">⑥ 모든 예산</text> <text x="115" y="332" font-size="12" text-anchor="middle">margin 안?</text>
<line x1="115" y1="246" x2="115" y2="278" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a1)"/> <rect x="250" y="295" width="190" height="50" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="345" y="316" font-size="12" text-anchor="middle">예 → freeze · 모델 카드</text> <text x="345" y="333" font-size="12" text-anchor="middle">HW 섹션 갱신 · CI 기준선</text>
<line x1="205" y1="320" x2="248" y2="320" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a1)"/> <path d="M 115 360 L 115 400 L 670 400 L 670 30 L 340 30 L 340 37" fill="none" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="5 4" marker-end="url(#i6a1)"/> <text x="430" y="393" font-size="12" text-anchor="middle">아니오 → 다음 병목으로 (② 다시)</text> <text x="470" y="260" font-size="12">색 = 주 책임자</text>
<text x="470" y="278" font-size="12">주황: embedded AI 엔지니어</text> <text x="470" y="296" font-size="12">파랑: 모델팀 · 초록: 벤더</text> <text x="470" y="314" font-size="12">빨강: HW팀</text>
</svg>
```

그림 1 — co-design 피드백 루프. 주황 상자(예산·측정·분류·재측정)가 embedded AI 엔지니어의 몫이고, 실제 수정은 세 갈래(모델팀 · 컴파일러/벤더 · HW팀)로 나간다. ⑥에서 "아니오"면 **다음 병목**으로 돌아간다. 병목은 하나를 없애면 다음 것이 드러난다(Amdahl).

이 노트에서 "done(끝)"의 정의는 하나다.

> **모든 예산을 margin을 두고 만족한다** — latency · peak activation · weight · CPU fallback 0 · 정확도 · 양자화 SQNR이 전부 예산표(I1)의 PASS 칸에 있고, 그 숫자가 **같은 harness로 재현된다.**

"latency가 맞았다"는 done이 아니다. latency를 맞추려다 정확도가 떨어질 수 있고, 정확도를 올리려다 메모리가 넘칠 수 있다. 이 노트의 v0도 latency·메모리뿐 아니라 정확도까지 처음부터 FAIL이었다(3·4절). 그래서 **매 바퀴 모든 항목을 다시 잰다.** Don의 NPI 경험으로 말하면, 한 항목만 보고 margin sign-off를 하지 않는 것과 같다.

### 0.1 이 노트의 사례 (가정)

예를 들어 Hark 같은 웨어러블이라면(추정) 손목 IMU로 제스처 8종(탭, 더블탭, 손목 돌리기 등)을 인식하는 모델을 always-on 영역에서 돌릴 수 있다. 이 노트는 I1–I5와 같은 가상의 사례를 쓴다.

| 항목 | 값 (모두 설명용 가정) |
|---|---|
| 입력 | 6축 IMU(가속도 3 + 자이로 3), 400 Hz, 2 s 창 = 6 × 800 |
| 타깃 | always-on micro-NPU: int8 MAC 32개/cycle, 50 MHz, 16채널 lane, SRAM 512 KB, 미지원 op는 같은 50 MHz의 Cortex-M급 CPU에서 float로 실행 |
| latency 예산 | 타깃 추정 ≤ 5 ms / 추론 (margin 10% → 4.5 ms 이하면 PASS) |
| peak activation 예산 | ≤ 200 KB (SRAM 512 KB 중 오디오 버퍼·RTOS·스택 몫을 뺀 모델 몫) |
| weight 예산 | int8 ≤ 300 KB (flash의 모델 슬롯) |
| CPU fallback | 0개 (always-on 영역에서 CPU를 깨우면 전력 예산이 깨진다 — I3·E8) |
| 정확도 | int8 모델 ≥ 95% (8-class, 합성 테스트셋 2000개) |
| 양자화 guardrail | float 대비 logits SQNR ≥ 25 dB (C8) |

NPU 설정은 I1의 예시(Ethos-U55급, 약 0.2 TOPS)보다 **일부러 훨씬 작게**(32 MAC/cycle × 50 MHz = 1.6 GMAC/s, always-on 저클럭 모드라고 가정) 잡았다. 루프가 여러 바퀴 돌아야 하는 상황을 만들기 위해서다. 숫자는 달라도 방법은 그대로다.

### 0.2 측정은 세 종류다 — 그리고 이 노트에서 무엇을 썼나

이 Mac에는 타깃 NPU가 없다. 그래서 루프의 "측정"을 세 가지로 나눠서 쓴다. 실무에서도 보드가 나오기 전(pre-silicon)이나 보드가 모자랄 때 똑같이 한다. Don이 해 본 FPGA pre-silicon bring-up의 "FPGA 숫자 vs 실리콘 숫자"와 같은 구도다.

| 종류 | 이 노트에서 | 믿을 수 있는 것 | 믿으면 안 되는 것 |
|---|---|---|---|
| 타깃 실측 | 없음 (보드가 있으면 1순위) | 전부 | — |
| proxy 실측 | Mac M2 CPU 1 thread: `torch.profiler`, ORT CPU EP 프로파일 JSON, ORT CoreML EP 노드 배치, int8 TFLite 변환 결과(크기·op·SQNR·정확도) | 상대 순위의 일부, op 지원 여부(실제 백엔드 판정), 양자화 오차(실제 int8 커널) | 절대 ms, fallback 비용, 메모리 계층 효과 |
| 분석 추정 | `budget_report.py`: MAC → cycle(가정한 MAC/cycle), peak activation(D2식 liveness), 미지원 op → CPU cycle | 예산 대비 몇 배인지, 어디가 큰지 | 커널 품질, 컴파일러 fusion의 실제 결과 |

> 이 노트의 **ms 숫자는 두 종류**다. "Mac ORT CPU 0.12 ms"처럼 Mac이라고 붙은 것은 proxy 실측이고, "latency_ms 2.55"처럼 budget_report가 낸 것은 **가정한 타깃의 분석 추정**이다. 둘을 섞어 읽지 말 것. 타이밍은 다른 프로세스가 돌던 Mac에서 쟀으므로 노이즈가 크다 — 그래서 모든 시간은 여러 번 반복한 **중앙값(median)**이고, 같은 측정을 두 번 하면 10~30% 달라질 수 있다.

### 0.3 이 노트의 순서

루프의 구성 요소(1절) → 실습 준비: 데이터·모델·예산 리포트 도구(2절) → **v0 측정과 병목 찾기**(3절) → v1 일찍 downsample(4절) → v2 op 교체: LN·GELU → BN·ReLU6(5절) → v3 DS-conv + 채널 정렬(6절) → v4 폭·입력률 축소 + 재학습과 후보 선택(7절) → 다섯 바퀴 전체 보기(8절) → 병목 분류 가이드(9절) → 모델 vs 컴파일러 vs HW 설정 고르기(10절) → 루프 자동화(11절) → 팀 프로세스(12절) → 타깃에서의 층별 측정(13절).

> 실행 환경: Python 예제는 `.venv/bin/python` (Python 3.9, torch 2.8.0, onnx 1.19.1, onnxruntime 1.19.2), TFLite 예제는 `.venv-tf/bin/python` (TensorFlow 2.20)로 실제 실행한 출력이다. C 예제는 `cc -std=c11 -Wall -Wextra -O2`(Apple clang, arm64)로 경고 0개. 예제들은 한 스크래치 디렉터리에서 순서대로 실행하며, 앞에서 저장한 모듈과 파일(`i6_common.py`, `v3.pt`, `v3.onnx` 등)을 뒤에서 쓴다.

---

## 1. 루프의 구성 요소 — 단계마다 입력·출력·책임자

### 1.1 단계 표

| 단계 | 입력 | 출력 | 주 책임자 | Don 경험과의 대응 |
|---|---|---|---|---|
| ① 예산·계약 | 제품 요구(반응 시간, 배터리), HW 사양 | 예산표 + margin 규칙 (I1) | embedded AI + 제품/HW | 전력·성능 margin sign-off 기준 |
| ② 측정 | 모델 파일 + 고정된 측정 조건 | 예산표의 "현재 값" 열, op별 프로파일 | embedded AI | Trace32·로직 분석기 측정 |
| ③ 병목 분류 | 프로파일 | top-N 구간과 분류(9절) | embedded AI | root cause 분석 |
| ④ 수정 경로 선택 | 분류 + 각 경로의 비용·기간 | 결정 기록(누가, 언제까지) | 주간 리뷰(12절) | NPI 이슈 리뷰 |
| ⑤ 적용·재측정 | 새 모델/컴파일러/설정 | before/after 숫자 | 수정 담당 + embedded AI 검증 | fix 검증 |
| ⑥ 판정 | 예산표 전체 | PASS면 freeze, 아니면 다음 병목 | embedded AI가 판정, 리뷰가 승인 | sign-off |

### 1.2 측정 규율 — 루프가 돌아가려면 숫자가 재현돼야 한다

루프의 속도는 "측정 한 번이 얼마나 싸고 믿을 만한가"가 정한다. 펌웨어 성능 측정에서 지키던 것이 그대로 적용된다.

- **조건 고정**: 클럭(DVFS 끄기), 스레드 수, 입력 크기, warm-up 횟수, SDK/컴파일러 버전, 모델 해시를 결과와 같이 기록한다 (F8 2절 버전 매트릭스).
- **반복과 중앙값**: 한 번 잰 숫자는 숫자가 아니다. N회 반복해 median을 쓰고, 분산이 크면 그 자체가 신호다(열, 인터럽트, 다른 프로세스).
- **한 바퀴에 한 종류의 수정**: 두 가지를 동시에 바꾸면 어느 쪽이 효과였는지 모른다. git bisect가 커밋 하나씩 자르는 이유와 같다. 4절에서 이 원칙이 왜 필요한지 실제로 보게 된다.
- **정확도도 매 바퀴**: 구조를 바꾸면 재학습이 필요하고, 재학습은 seed에 따라 결과가 흔들린다. 그래서 정확도는 seed 여러 개의 범위로 본다(7절).
- **같은 harness**: before와 after를 다른 스크립트로 재면 차이가 수정 때문인지 측정 때문인지 모른다. 그래서 11절의 `budget_report.py` 하나로 모든 후보를 잰다.

### 1.3 병목은 이동한다 — Amdahl을 숫자로

추론 시간이 A(60%) + B(30%) + C(10%)라고 하자. A를 10배 빠르게 하면 전체는 `0.6/10 + 0.3 + 0.1 = 0.46`, 즉 2.2배 빨라질 뿐이고 이제 B가 65%를 차지한다. 말로 하면: **가장 큰 병목을 없애면 다음 병목이 주인공이 된다.** 그래서 루프는 여러 바퀴를 돈다. 이 노트의 v0에서도 첫 바퀴의 주인공은 CPU fallback과 메모리였고, 마지막 바퀴의 주인공은 pointwise conv의 MAC이었다.

---

## 2. 실습 준비 — 데이터, 모델 다섯 개, 예산 리포트 도구

### 2.1 공용 모듈 `i6_common.py` (예제 1)

무엇을 확인하는 코드인지: 합성 IMU 제스처 데이터와, `spec` 딕셔너리 하나로 v0~v4 모델을 만드는 빌더다. **모델 버전 사이의 차이가 spec 몇 줄의 diff로 보이게** 만든 것이 핵심이다 — 실무에서도 모델 구성을 코드가 아니라 설정 파일로 두면 루프의 기록이 쉬워진다.

```python
# i6_common.py — I6 공용: 합성 IMU 제스처 데이터 + spec으로 만드는 1D CNN (v0~v4)
import numpy as np, torch, torch.nn as nn

L_IN, N_CLS = 800, 8                      # 2 s 창 @ 400 Hz (가정), 제스처 8종

def make_data(n, seed, L=L_IN):
    """반환 X [n,6,L] float32, y [n]. 클래스마다 축·주파수·감쇠가 다른 짧은 burst + 잡음."""
    rng = np.random.default_rng(seed); t = np.arange(L) / 400.0
    X = 0.5 * rng.standard_normal((n, 6, L)).astype(np.float32); y = rng.integers(0, N_CLS, n)
    for i in range(n):
        c = y[i]; t0 = rng.uniform(0.3, 1.5); a = rng.uniform(0.6, 1.4)
        f = [6, 9, 13, 6, 9, 13, 18, 18][c] * rng.uniform(0.9, 1.1)      # 주파수
        axes = [[0, 1], [1, 2], [0, 2], [3, 4], [4, 5], [3, 5], [0, 3], [2, 5]][c]
        env = np.exp(-((t - t0) / 0.08) ** 2) if c < 6 else np.exp(-((t - t0) / 0.2) ** 2)
        for k, ax in enumerate(axes):
            X[i, ax] += a * env * np.sin(2 * np.pi * f * (t - t0) + k * 1.3)
        X[i, :3] += rng.normal(0, 0.3, (3, 1))                           # 중력 방향(자세) 차이
    return X, y

class ChannelLN(nn.Module):               # 시간 축마다 채널 방향 LayerNorm ([N,C,L] 입력)
    def __init__(self, c): super().__init__(); self.ln = nn.LayerNorm(c, eps=1e-5)
    def forward(self, x): return self.ln(x.transpose(1, 2)).transpose(1, 2)

def build(spec):
    """spec = dict(blocks=[(kind, cout, k, stride)], norm='ln'|'bn', act='gelu'|'relu6')"""
    layers, cin = [], 6
    for kind, cout, k, s in spec["blocks"]:
        if kind == "ds":                  # depthwise k + pointwise 1x1
            layers += [nn.Conv1d(cin, cin, k, s, k // 2, groups=cin, bias=False),
                       nn.BatchNorm1d(cin) if spec["norm"] == "bn" else ChannelLN(cin),
                       nn.ReLU6() if spec["act"] == "relu6" else nn.GELU()]
            k, s = 1, 1
        layers += [nn.Conv1d(cin, cout, k, s, k // 2, bias=spec["norm"] != "bn"),
                   nn.BatchNorm1d(cout) if spec["norm"] == "bn" else ChannelLN(cout),
                   nn.ReLU6() if spec["act"] == "relu6" else nn.GELU()]
        cin = cout
    layers += [nn.AdaptiveAvgPool1d(1), nn.Flatten(), nn.Linear(cin, N_CLS)]
    return nn.Sequential(*layers)

SPECS = {   # 반복마다 "한 종류의 수정"만 한다. L = 입력 샘플 수
  "v0": dict(L=800, norm="ln", act="gelu", blocks=[("conv", 62, 9, 1), ("conv", 130, 9, 1),
             ("conv", 162, 9, 1), ("conv", 250, 9, 4)]),
  "v1": dict(L=800, norm="ln", act="gelu", blocks=[("conv", 62, 9, 4), ("conv", 130, 9, 2),   # 일찍 downsample
             ("conv", 162, 9, 1), ("conv", 250, 9, 2)]),
  "v2": dict(L=800, norm="bn", act="relu6", blocks=[("conv", 62, 9, 4), ("conv", 130, 9, 2),  # BN + ReLU6
             ("conv", 162, 9, 1), ("conv", 250, 9, 2)]),
  "v3": dict(L=800, norm="bn", act="relu6", blocks=[("conv", 64, 9, 4), ("ds", 128, 9, 2),    # DS-conv + 16 정렬
             ("ds", 160, 9, 1), ("ds", 256, 9, 2)]),
  "v4": dict(L=400, norm="bn", act="relu6", blocks=[("conv", 32, 9, 2), ("ds", 64, 9, 2),     # 200 Hz 입력 + 폭 축소
             ("ds", 128, 9, 2), ("ds", 160, 9, 1)]),
  "v0bn": dict(L=800, norm="bn", act="relu6", blocks=[("conv", 62, 9, 1), ("conv", 130, 9, 1),  # A/B용
               ("conv", 162, 9, 1), ("conv", 250, 9, 4)]),
  "v4n": dict(L=400, norm="bn", act="relu6", blocks=[("conv", 16, 9, 2), ("ds", 16, 9, 2),    # 후보: 더 좁게
              ("ds", 32, 9, 2), ("ds", 32, 9, 1)]),
  "v4r": dict(L=200, norm="bn", act="relu6", blocks=[("conv", 32, 9, 1), ("ds", 64, 9, 2),    # 후보: 100 Hz
              ("ds", 128, 9, 2), ("ds", 160, 9, 1)]),
}

def train(m, X, y, epochs=6, seed=0, lr=3e-3):
    torch.manual_seed(seed); opt = torch.optim.AdamW(m.parameters(), lr)
    Xt, yt = torch.from_numpy(X), torch.from_numpy(y); n = len(Xt)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, lr, total_steps=epochs * ((n + 63) // 64))
    m.train()
    for _ in range(epochs):
        perm = torch.randperm(n)
        for i in range(0, n, 64):
            idx = perm[i:i + 64]
            loss = nn.functional.cross_entropy(m(Xt[idx]), yt[idx])
            opt.zero_grad(); loss.backward(); opt.step(); sched.step()
    return m.eval()

@torch.no_grad()
def accuracy(m, X, y):
    return float((m(torch.from_numpy(X)).argmax(1).numpy() == y).mean())

def for_spec(X, spec):                    # v4는 200 Hz 입력: 400 Hz 데이터를 2:1 decimation
    return np.ascontiguousarray(X[:, :, ::L_IN // spec["L"]])
```

v0은 일부러 "연구 코드에서 막 나온" 모양으로 만들었다: 처음 세 층이 stride 1이라 800 샘플 해상도를 끝까지 들고 가고(큰 feature map), 채널 수가 62·130·162·250처럼 16의 배수가 아니고, 정규화는 LayerNorm, 활성 함수는 GELU다. 서버 GPU에서는 아무 문제 없는 선택들이다. **micro-NPU에서는 각각이 다른 종류의 병목이 된다** — 3절에서 그것을 하나씩 잰다.

`ChannelLN`이 transpose 두 번으로 감싸져 있다는 것도 기억해 두자. PyTorch의 `LayerNorm`은 마지막 축을 정규화하므로 [N, C, L] 텐서에서 채널 방향으로 정규화하려면 축을 돌려야 한다. 이런 layout 변환은 배포 그래프에 `Transpose` 노드로 그대로 남는다.

### 2.2 학습 스크립트와 정확도 (예제 2)

무엇을 확인하는 코드인지: 모든 버전을 **같은 레시피**(AdamW, OneCycle, 8 epoch, 학습 3000개, 테스트 2000개)로 학습한다. 버전 간 정확도 차이가 레시피 차이가 아니라 구조 차이가 되게 하기 위해서다. 프로세스마다 1 thread로 여러 버전을 병렬로 돌렸다.

```python
# i6_train.py — 버전 하나를 같은 레시피로 학습하고 저장한다 (사용: python i6_train.py v3 [seed])
import sys, time, torch
from i6_common import *
torch.set_num_threads(1)                                   # 여러 버전을 프로세스별로 병렬 학습
v = sys.argv[1]; seed = int(sys.argv[2]) if len(sys.argv) > 2 else 0; sp = SPECS[v]
Xtr, ytr = make_data(3000, seed=1); Xte, yte = make_data(2000, seed=2)
torch.manual_seed(seed); m = build(sp); t = time.time()   # 초기 가중치도 seed로 고정
train(m, for_spec(Xtr, sp), ytr, epochs=8, seed=seed)
torch.save(m.state_dict(), f"{v}.pt" if seed == 0 else f"{v}_s{seed}.pt")
print(f"{v} seed={seed}: params={sum(p.numel() for p in m.parameters()):,}  "
      f"test acc={accuracy(m, for_spec(Xte, sp), yte):.4f}  train time={time.time() - t:.0f} s")
```

```text
v0 seed=0: params=633,748  test acc=0.5265  train time=2730 s
v0bn seed=0: params=633,144  test acc=0.9985  train time=2095 s
v1 seed=0: params=633,748  test acc=0.6565  train time=614 s
v1 seed=1: params=633,748  test acc=0.8480  train time=398 s
v1 seed=2: params=633,748  test acc=0.6725  train time=399 s
v2 seed=0: params=633,144  test acc=0.9995  train time=593 s
v3 seed=0: params=80,232  test acc=1.0000  train time=509 s
v3 seed=1: params=80,232  test acc=1.0000  train time=508 s
v3 seed=2: params=80,232  test acc=0.9995  train time=508 s
v4 seed=0: params=36,968  test acc=0.9905  train time=315 s
v4 seed=1: params=36,968  test acc=0.9815  train time=314 s
v4 seed=2: params=36,968  test acc=0.9730  train time=321 s
v4n seed=0: params=3,816  test acc=0.7810  train time=51 s
v4r seed=0: params=36,968  test acc=0.9335  train time=331 s
v4r seed=1: params=36,968  test acc=0.9465  train time=332 s
v4r seed=2: params=36,968  test acc=0.9450  train time=332 s
```

출력에서 볼 것: float 정확도가 둘로 갈린다. LayerNorm·GELU를 쓰는 v0·v1은 0.53~0.85(seed에 따라 크게 흔들림)이고, BatchNorm·ReLU6를 쓰는 버전은 거의 전부 0.97 이상이다. `v0bn`은 이 차이의 원인을 가르기 위한 A/B용 학습이다(4.3절). seed를 바꾼 줄(v1, v3, v4, v4r)은 같은 구조라도 정확도가 흔들리는 폭을 보여 준다. 학습 시간(train time)은 다른 프로세스와 CPU를 나눠 쓴 값이라 참고만 한다.

### 2.3 예산 리포트 도구 `budget_report.py` (예제 3)

무엇을 확인하는 코드인지: **모델 파일(ONNX) 하나**를 받아 예산표 한 장을 내는 도구다. 루프의 모든 바퀴에서 이것 하나로 잰다(1.2절 "같은 harness"). 하는 일은 네 가지다.

1. ONNX shape inference로 모든 텐서의 shape을 얻는다.
2. 노드마다 MAC, weight 바이트, 가정한 NPU에서의 cycle을 계산한다. Conv는 `max(compute, memory) + 층당 고정비` — roofline(D3)을 층 하나에 적용한 것이다. 채널은 16 lane 단위로 올림해서 계산한다(홀수 채널의 낭비). depthwise는 MAC 배열을 1/8만 쓴다고 가정한다.
3. allowlist에 없는 op는 CPU fallback으로 보고, 원소당 cycle + NPU→CPU 전환 비용을 더한다. fallback 구간은 float32라서 버퍼가 4배다.
4. D2식 liveness: 텐서마다 "만들어진 노드 ~ 마지막으로 읽힌 노드" 동안 살아 있다고 보고, 매 시점의 합의 최댓값을 peak으로 낸다. 원소별 op(ReLU, Clip, Erf 등)는 입력이 그 자리에서 죽으면 같은 버퍼를 쓴다(in-place).

모든 상수는 **가정**이다. 실무에서는 이 상수를 타깃 보드의 측정으로 보정한다(C7의 LUT 보정과 같은 일).

```python
# budget_report.py — 모델 파일(ONNX) 하나 → 예산표 + 병목 top 3. CI에서 후보마다 실행 (exit 1 = 예산 위반)
import json, math, os, sys, time, numpy as np, onnx, onnxruntime as ort
from onnx import numpy_helper, shape_inference
# ---- 가상의 always-on micro-NPU (모든 상수는 설명용 가정) -------------------------------
F_HZ, MPC, LANE, DW_EFF = 50e6, 32, 16, 1 / 8     # 50 MHz, int8 MAC 32개/cycle, 16채널 lane, DW 효율 1/8
SRAM_BPC, FLASH_BPC, OVH, SYNC = 16, 4, 2000, 4000 # B/cycle, layer 고정비, NPU<->CPU 전환 1회 cycle
NPU_OPS = {"Conv", "Gemm", "Relu", "Clip", "Add", "Mul", "Div", "Transpose", "GlobalAveragePool",
           "Flatten", "Reshape", "Pad"}            # NPU 컴파일러 지원 op (가정한 allowlist)
CPU_CYC = {"Erf": 30, "LayerNormalization": 12}    # CPU fallback 원소당 cycle (그 외 4)
ELEMWISE = {"Relu", "Clip", "Erf", "Add", "Mul", "Div"}
BUDGET = {"latency_ms": 5.0, "peak_act_KB": 200, "weights_KB": 300, "cpu_ops": 0}   # I1 예산표
MARGIN = 0.9                                       # 예산의 90% 이하여야 PASS (여유 10%)

def analyze(path):
    g = shape_inference.infer_shapes(onnx.load(path)).graph
    init = {t.name: numpy_helper.to_array(t) for t in g.initializer}
    for nd in g.node:                                  # Constant·Identity(상수)는 가중치 취급, 노드 아님
        if nd.op_type == "Constant": init[nd.output[0]] = numpy_helper.to_array(nd.attribute[0].t)
        if nd.op_type == "Identity" and nd.input[0] in init: init[nd.output[0]] = init[nd.input[0]]
    nodes = [nd for nd in g.node if nd.output[0] not in init]
    shp = {v.name: [d.dim_value for d in v.type.tensor_type.shape.dim]
           for v in list(g.input) + list(g.value_info) + list(g.output)}
    n = lambda t: math.prod(shp[t]) if t in shp else 0           # 원소 수 (int8이면 = 바이트)
    last = {t: i for i, nd in enumerate(nodes) for t in nd.input}
    last.update({o.name: len(nodes) for o in g.output})
    root, span, rows, prev = {g.input[0].name: g.input[0].name}, {g.input[0].name: [0, 0]}, [], "NPU"
    for i, nd in enumerate(nodes):
        acts = [t for t in nd.input if t in shp and t not in init]; y = nd.output[0]
        a_in, a_out, ws = sum(n(t) for t in acts), n(y), [init[t] for t in nd.input if t in init]
        dying = [t for t in acts if last.get(t) == i and n(t) == a_out]
        root[y] = root[dying[0]] if nd.op_type in ELEMWISE and dying else y   # in-place면 버퍼 공유
        span.setdefault(root[y], [i, i])
        r = dict(i=i, op=nd.op_type, macs=0, w=sum(w.size for w in ws), unit="NPU", f32=0)
        if nd.op_type not in NPU_OPS:                                 # CPU fallback (float32로 계산)
            r.update(unit="CPU", bound="fallback", f32=4 * (a_in + a_out),
                     cyc=a_out * CPU_CYC.get(nd.op_type, 4) + (SYNC if prev == "NPU" else 0))
        elif nd.op_type == "Conv":
            cout, cpg, k = ws[0].shape; lo = shp[y][-1]; dw = cpg == 1 and cout > 1
            r["macs"] = cout * cpg * k * lo; up = lambda c: -(-c // LANE) * LANE   # 16 lane 패딩
            comp = (up(cout) * k * lo / DW_EFF if dw else up(cout) * up(cpg) * k * lo) / MPC
            mem = (a_in + a_out) / SRAM_BPC + r["w"] / FLASH_BPC
            r.update(op="DWConv" if dw else "Conv", cyc=max(comp, mem) + OVH,
                     bound="compute" if comp >= mem else "memory")
        elif nd.op_type == "Gemm":
            r.update(macs=r["w"], cyc=r["w"] / FLASH_BPC + OVH, bound="memory")
        elif nd.op_type in ("Relu", "Clip") and nodes[i - 1].op_type == "Conv":
            r.update(cyc=0, bound="fused")                            # conv 출력 단계에서 공짜
        else:
            r.update(cyc=0 if nd.op_type in ("Flatten", "Reshape") else (a_in + a_out) / SRAM_BPC + OVH,
                     bound="memory")
        prev = r["unit"]; rows.append(r)
    for t, j in last.items():                                         # 버퍼 수명 = 공유 텐서들의 마지막 사용까지
        if t in root: span[root[t]][1] = max(span[root[t]][1], j)
    peak = max(sum(n(b) for b, (s, e) in span.items() if s <= i <= e) for i in range(len(nodes)))
    return rows, max(peak, max(r["f32"] for r in rows))              # fallback의 float 버퍼도 SRAM을 쓴다
```

```python
def report(path):
    rows, peak = analyze(path)
    so = ort.SessionOptions(); so.intra_op_num_threads = 1
    s = ort.InferenceSession(path, so, providers=["CPUExecutionProvider"]); i0 = s.get_inputs()[0]
    x = {i0.name: np.random.default_rng(0).standard_normal(i0.shape).astype(np.float32)}
    ts = []
    for k in range(60):
        t = time.perf_counter(); s.run(None, x); ts.append(time.perf_counter() - t)
    got = {"latency_ms": sum(r["cyc"] for r in rows) / F_HZ * 1e3, "peak_act_KB": peak / 1e3,
           "weights_KB": sum(r["w"] for r in rows) / 1e3, "cpu_ops": sum(r["unit"] == "CPU" for r in rows)}
    met = path.replace(".onnx", ".metrics.json")                # 평가 단계가 남긴 정확도·SQNR
    extra = json.load(open(met)) if os.path.exists(met) else {}
    print(f"== {os.path.basename(path)}  MACs={sum(r['macs'] for r in rows) / 1e6:.2f} M  "
          f"Mac ORT CPU median={1e3 * np.median(ts[10:]):.2f} ms (proxy, 50 runs)")
    fail = False
    for k, b in list(BUDGET.items()) + [("acc_int8", 0.95), ("sqnr_dB", 25)]:
        v = got.get(k, extra.get(k))
        if v is None: continue
        hi = k in ("acc_int8", "sqnr_dB")                           # 이 둘은 클수록 좋다
        ok = v >= b if hi else (v <= b * MARGIN if b else v == 0)
        st = "PASS" if ok else ("MARGIN" if not hi and v <= b else "FAIL"); fail |= st == "FAIL"
        print(f"  {k:12s} {v:9.{4 if k == 'acc_int8' else 2}f}  budget {'>=' if hi else '<='} {b:<6}  {st}")
    cls = {}
    for r in rows: cls[r["bound"]] = cls.get(r["bound"], 0) + r["cyc"] / F_HZ * 1e3
    print("  by class:", "  ".join(f"{k} {v:.2f} ms" for k, v in sorted(cls.items(), key=lambda kv: -kv[1]) if v))
    top = sorted(rows, key=lambda r: -r["cyc"])[:3]
    print("  top-3:", " | ".join(f"#{r['i']} {r['op']} {r['bound']} {r['cyc'] / F_HZ * 1e3:.2f} ms" for r in top))
    return fail

if __name__ == "__main__":
    sys.exit(int(any([report(p) for p in sys.argv[1:]])))
```

정확도와 SQNR은 모델 파일만으로는 알 수 없으므로, 평가 단계(5절의 TFLite 스크립트)가 남긴 `v?.metrics.json`이 있으면 읽어서 같은 표에 넣는다. exit code가 1이면 예산 위반 — 11절에서 이것을 CI 게이트로 쓴다. 출력은 3절부터 바퀴마다 나온다.

---

## 3. v0 — 순진한 모델을 세 가지 방법으로 잰다

루프의 첫 바퀴는 **수정하지 않고 재기만** 한다. 펌웨어에서도 첫 측정 전에 고치기 시작하면 무엇이 효과였는지 영영 모른다.

### 3.1 proxy 측정 1 — `torch.profiler` (예제 4)

무엇을 확인하는 코드인지: Mac CPU 1 thread에서 v0 추론을 10회씩 3번 프로파일하고, op별 self 시간 비중의 중앙값을 낸다. "self" 시간은 하위 호출을 뺀 그 op 자신의 시간이다.

```python
# Mac CPU(1 thread)에서 torch.profiler로 op별 self 시간을 잰다 — 반복 3회, 회차별 비중의 중앙값
import sys, statistics as st, torch
from torch.profiler import profile, ProfilerActivity
from i6_common import *
torch.set_num_threads(1); v = sys.argv[1]; sp = SPECS[v]
m = build(sp).eval(); m.load_state_dict(torch.load(f"{v}.pt"))
x = torch.from_numpy(for_spec(make_data(1, seed=3)[0], sp))
shares, totals = {}, []
with torch.no_grad():
    for _ in range(5): m(x)                                    # warm-up
    for rep in range(3):
        with profile(activities=[ProfilerActivity.CPU]) as prof:
            for _ in range(10): m(x)
        ev = [e for e in prof.key_averages() if e.self_cpu_time_total > 0]
        tot = sum(e.self_cpu_time_total for e in ev); totals.append(tot / 10 / 1e3)
        for e in ev: shares.setdefault(e.key, []).append(e.self_cpu_time_total / tot)
print(f"{v}: per-inference self CPU total (ms) per rep = {[round(t, 2) for t in totals]}")
for k, s in sorted(shares.items(), key=lambda kv: -st.median(kv[1]))[:6]:
    print(f"  {k:28s} {100 * st.median(s):5.1f} %")
```

```text
v0: per-inference self CPU total (ms) per rep = [2.61, 2.67, 2.65]
  aten::_slow_conv2d_forward    41.6 %
  aten::gelu                    33.0 %
  aten::native_layer_norm       12.0 %
  aten::copy_                   10.2 %
  aten::_convolution             0.4 %
  aten::empty                    0.3 %
```

출력에서 볼 것: PyTorch eager에서는 conv(`_slow_conv2d_forward`)가 42%, **GELU가 33%**, LayerNorm이 12%, `aten::copy_`가 10%다. `copy_`는 `ChannelLN`의 transpose가 만든 메모리 복사 — layout 변환이 공짜가 아니라는 증거다. 세 번 반복한 총 시간(2.61~2.67 ms)이 가까워서 비율도 믿을 만하다. 그런데 이 비율은 **PyTorch가 이 CPU에서 GELU를 어떻게 구현했는가**의 결과이지 타깃의 성질이 아니다. `_slow_conv2d_forward`라는 이름도 기억해 두자 — 1D conv를 2D로 바꿔 범용 경로로 돈다는 뜻이다. **proxy의 커널 품질은 타깃과 무관하다.**

### 3.2 proxy 측정 2 — ORT 프로파일 JSON과 실제 가속기 EP의 판정 (예제 5)

무엇을 확인하는 코드인지: 같은 v0를 ONNX로 내보내(`i6_export.py`, opset 17) ONNX Runtime CPU EP의 프로파일 JSON을 op 종류별로 합친다(25회 중 앞 5회 warm-up 제외, 노드별 median). 그리고 이 Mac의 **실제 가속기 백엔드**인 CoreML EP에게 노드마다 "가져갈 수 있나"를 묻는다(F8 3.5절과 같은 로그 파싱).

```python
# i6_export.py — 학습된 버전을 ONNX(opset 17)로, Keras 이식용 가중치·데이터를 npz로 저장
import json, sys, numpy as np, torch
from i6_common import *
Xte, yte = make_data(2000, seed=2); Xcal, _ = make_data(200, seed=1)    # 보정용은 학습 분포에서
np.savez("data.npz", Xte=Xte, yte=yte, Xcal=Xcal); json.dump(SPECS, open("specs.json", "w"))
for v in sys.argv[1:]:
    sp = SPECS[v]; m = build(sp).eval(); m.load_state_dict(torch.load(f"{v}.pt"))
    torch.onnx.export(m, torch.zeros(1, 6, sp["L"]), f"{v}.onnx", opset_version=17,
                      input_names=["imu"], output_names=["logits"], dynamo=False)
    np.savez(f"{v}_w.npz", **{k: t.detach().numpy() for k, t in m.state_dict().items()})
    with torch.no_grad(): np.save(f"{v}_torch_logits.npy", m(torch.from_numpy(for_spec(Xte, sp))).numpy())
    print(v, "exported")
```

```python
# ORT CPU EP 프로파일 JSON을 op_type별로 합치고, CoreML EP가 몇 노드를 CPU에 남기는지 본다
import json, os, re, sys, tempfile, statistics as st, numpy as np, onnxruntime as ort
v = sys.argv[1]; so = ort.SessionOptions(); so.enable_profiling = True; so.intra_op_num_threads = 1
s = ort.InferenceSession(f"{v}.onnx", so, providers=["CPUExecutionProvider"])
x = {"imu": np.random.default_rng(0).standard_normal(s.get_inputs()[0].shape).astype(np.float32)}
for _ in range(25): s.run(None, x)                        # 앞 5회는 warm-up으로 버린다
ev = [e for e in json.load(open(s.end_profiling())) if e.get("cat") == "Node" and e["name"].endswith("_kernel_time")]
per_op = {}
for node_i in range(len(ev) // 25):
    durs = [ev[r * (len(ev) // 25) + node_i]["dur"] for r in range(5, 25)]
    op = ev[node_i]["args"]["op_name"]; per_op[op] = per_op.get(op, 0) + st.median(durs)
tot = sum(per_op.values())
print(f"{v}: ORT CPU EP, sum of per-node medians = {tot / 1e3:.2f} ms")
for op, d in sorted(per_op.items(), key=lambda kv: -kv[1]):
    print(f"  {op:20s} {d / 1e3:7.3f} ms  {100 * d / tot:5.1f} %")
so2 = ort.SessionOptions(); so2.log_severity_level = 0
fd, path = tempfile.mkstemp(); saved = os.dup(2); os.dup2(fd, 2)
try: ort.InferenceSession(f"{v}.onnx", so2, providers=["CoreMLExecutionProvider", "CPUExecutionProvider"])
finally: os.dup2(saved, 2); os.close(fd)
log = open(path).read(); os.remove(path)
sup = re.findall(r"Operator type: \[(\w+)\] index: \[\d+\] name: \[[^\]]*\] supported: \[(\d)\]", log)
no = sorted({op for op, ok in sup if ok == "0"})
print(f"  CoreML EP: {sum(ok == '1' for _, ok in sup)}/{len(sup)} nodes supported, unsupported types = {no}")
```

```text
v0: ORT CPU EP, sum of per-node medians = 8.56 ms
  Conv                   7.030 ms   82.1 %
  Gelu                   0.744 ms    8.7 %
  LayerNormalization     0.673 ms    7.9 %
  Transpose              0.106 ms    1.2 %
  GlobalAveragePool      0.008 ms    0.1 %
  Gemm                   0.003 ms    0.0 %
  Flatten                0.001 ms    0.0 %
  CoreML EP: 30/39 nodes supported, unsupported types = ['Erf', 'GlobalAveragePool', 'LayerNormalization']
```

출력에서 볼 것:

- ORT CPU EP는 opset 17에서 `Div → Erf → Add → Mul → Mul`로 풀려 나온 GELU를 다시 `Gelu` 하나로 합쳤다(ORT의 graph optimizer가 하는 fusion). 그래서 프로파일에는 `Gelu`가 보인다. 그런데 비중은 **conv 82%, GELU 9%, LayerNorm 8%** — 같은 Mac, 같은 모델인데 3.1절과 전혀 다른 그림이다. 런타임이 바뀌면 커널이 바뀌고, 비율도 바뀐다.
- **CoreML EP는 `Erf`와 `LayerNormalization`을 가져가지 않는다.** 실제 가속기 백엔드가 "이 op는 내가 못 한다"고 답한 것이다. 우리가 가정한 micro-NPU도 같은 op를 지원하지 않는다고 놓았다. `GlobalAveragePool`도 거부됐는데, 이것은 7.4절에서 다룬다.
- 이 표만으로는 fallback이 **얼마나 비싼지** 알 수 없다. Mac에서는 CPU와 "가속기"가 같은 칩이고 float가 원래 빠르기 때문이다.

### 3.3 분석 추정 — budget_report로 타깃 기준 예산표 (예제 3 실행)

무엇을 확인하는 코드인지: 2.3절 도구를 v0에 돌린다. 가정한 타깃(50 MHz, 32 MAC/cycle, 미지원 op는 CPU float)에서의 예산표다.

```sh
python budget_report.py v0.onnx
```

```text
== v0.onnx  MACs=285.24 M  Mac ORT CPU median=7.92 ms (proxy, 50 runs)
  latency_ms      498.03  budget <= 5.0     FAIL
  peak_act_KB    1036.80  budget <= 200     FAIL
  weights_KB      633.76  budget <= 300     FAIL
  cpu_ops           8.00  budget <= 0       FAIL
  acc_int8        0.5275  budget >= 0.95    FAIL
  sqnr_dB          31.10  budget >= 25      PASS
  by class: fallback 280.53 ms  compute 210.98 ms  memory 6.53 ms
  top-3: #18 Conv compute 114.09 ms | #23 Erf fallback 77.84 ms | #14 Erf fallback 62.48 ms
```

출력에서 볼 것:

- latency 추정이 예산의 **약 100배**다. 그리고 그 절반 이상(56%)이 `fallback` 열이다 — 3.2절의 ORT 프로파일에서는 합쳐 17%였던 GELU(Erf)·LayerNorm이 타깃에서는 주인공이다. 이유는 두 가지다. 50 MHz Cortex-M급 CPU에서 원소당 수십 cycle(가정)이 드는데, v0는 feature map이 커서 원소가 수십만 개다. 그리고 NPU↔CPU 전환이 블록마다 일어난다.
- **peak activation 1036.8 KB > SRAM 512 KB.** 모델이 SRAM에 들어가지조차 않는다. 이 숫자는 int8 텐서가 아니라 fallback 구간의 **float32 버퍼**(LN 입력+출력, 원소당 4 B)에서 나왔다. fallback은 시간만이 아니라 메모리도 4배로 키운다.
- weight 634 KB > 300 KB, CPU op 8개(Erf 4 + LayerNormalization 4) > 0.
- **정확도도 FAIL이다: int8 52.75%.** 8-class라 찍기(12.5%)보다는 낫지만 예산(95%)과 거리가 멀다. float도 52.65%(2.2절)라서 양자화 문제가 아니라 학습이 안 된 것이다(SQNR 31.1 dB는 PASS). 정확도·SQNR 줄은 5.2절의 int8 변환 단계가 남긴 metrics.json에서 읽었다.

### 3.4 세 프로파일이 서로 다른 이야기를 한다

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<text x="20" y="22" font-size="14">같은 v0, 세 가지 측정 — 시간 비중 (100% 막대)</text> <text x="190" y="68" font-size="12" text-anchor="end">Mac torch.profiler</text><text x="190" y="84" font-size="12" text-anchor="end">합 2.65 ms</text> <rect x="200.0" y="50" width="176.4" height="40" fill="#4a7bd0"/>
<text x="288.2" y="66" font-size="12" text-anchor="middle">conv</text><text x="288.2" y="82" font-size="12" text-anchor="middle">42%</text> <rect x="376.4" y="50" width="138.6" height="40" fill="#e08a3c"/> <text x="445.7" y="66" font-size="12" text-anchor="middle">GELU</text><text x="445.7" y="82" font-size="12" text-anchor="middle">33%</text> <rect x="515.0" y="50" width="50.4" height="40" fill="#d0564a"/>
<text x="540.2" y="66" font-size="12" text-anchor="middle">LN</text><text x="540.2" y="82" font-size="12" text-anchor="middle">12%</text> <rect x="565.4" y="50" width="42.8" height="40" fill="#888" opacity="0.5"/> <text x="586.8" y="66" font-size="12" text-anchor="middle">copy</text><text x="586.8" y="82" font-size="12" text-anchor="middle">10.2%</text>
<rect x="608.2" y="50" width="11.8" height="40" fill="#888" opacity="0.5"/> <text x="190" y="138" font-size="12" text-anchor="end">Mac ORT CPU EP</text><text x="190" y="154" font-size="12" text-anchor="end">합 8.56 ms</text> <rect x="200.0" y="120" width="344.8" height="40" fill="#4a7bd0"/>
<text x="372.4" y="136" font-size="12" text-anchor="middle">Conv</text><text x="372.4" y="152" font-size="12" text-anchor="middle">82.1%</text> <rect x="544.8" y="120" width="36.5" height="40" fill="#e08a3c"/> <text x="563.1" y="136" font-size="12" text-anchor="middle">Gelu</text><text x="563.1" y="152" font-size="12" text-anchor="middle">8.7%</text> <rect x="581.4" y="120" width="33.2" height="40" fill="#d0564a"/>
<text x="597.9" y="136" font-size="12" text-anchor="middle">LN</text><text x="597.9" y="152" font-size="12" text-anchor="middle">7.9%</text> <rect x="614.5" y="120" width="5.5" height="40" fill="#888" opacity="0.5"/> <text x="190" y="208" font-size="12" text-anchor="end">타깃 추정 (budget_report)</text><text x="190" y="224" font-size="12" text-anchor="end">합 498 ms</text>
<rect x="200.0" y="190" width="178.1" height="40" fill="#4a7bd0"/> <text x="289.0" y="206" font-size="12" text-anchor="middle">compute (conv)</text><text x="289.0" y="222" font-size="12" text-anchor="middle">42.4%</text> <rect x="378.1" y="190" width="236.5" height="40" fill="#d0564a"/>
<text x="496.3" y="206" font-size="12" text-anchor="middle">fallback (Erf + LN on CPU)</text><text x="496.3" y="222" font-size="12" text-anchor="middle">56.3%</text> <rect x="614.5" y="190" width="5.5" height="40" fill="#888" opacity="0.5"/> <line x1="200" y1="244" x2="620" y2="244" stroke="currentColor"/>
<line x1="200.0" y1="244" x2="200.0" y2="249" stroke="currentColor"/><text x="200.0" y="262" font-size="12" text-anchor="middle">0%</text> <line x1="305.0" y1="244" x2="305.0" y2="249" stroke="currentColor"/><text x="305.0" y="262" font-size="12" text-anchor="middle">25%</text> <line x1="410.0" y1="244" x2="410.0" y2="249" stroke="currentColor"/><text x="410.0" y="262" font-size="12" text-anchor="middle">50%</text>
<line x1="515.0" y1="244" x2="515.0" y2="249" stroke="currentColor"/><text x="515.0" y="262" font-size="12" text-anchor="middle">75%</text> <line x1="620.0" y1="244" x2="620.0" y2="249" stroke="currentColor"/><text x="620.0" y="262" font-size="12" text-anchor="middle">100%</text> <text x="20" y="290" font-size="12">파랑 = conv · 주황 = GELU · 빨강 = LayerNorm(타깃 추정에서는 Erf + LN의 CPU fallback) · 회색 = 복사·transpose·기타</text>
</svg>
```

그림 3 — 같은 v0, 세 가지 측정. ORT는 "conv가 대부분", PyTorch는 "conv 42% + GELU 33%", 타깃 추정은 "CPU fallback이 56%"라고 말한다. PyTorch의 비율이 타깃 추정과 비슷해 보이는 것은 **우연**이다 — 이유가 다르다(PyTorch의 GELU 커널 비용 vs 타깃의 50 MHz CPU fallback과 전환 비용). 어느 쪽을 믿고 수정 순서를 정해야 하나? **타깃 쪽이다.** proxy는 "어떤 op가 들어 있나, 실제 백엔드가 무엇을 거부하나"를 알려 주는 데 쓰고, "무엇이 비싼가"는 타깃 실측이나 타깃 비용 모델로 판단한다. FPGA 숫자로 실리콘의 타이밍 병목을 판단하지 않는 것과 같다.

### 3.5 병목 목록과 첫 수정 고르기

v0의 문제를 전부 적고, 각각의 고칠 후보를 나란히 놓는다.

| # | 병목 (증거) | 분류 (9절) | 후보 수정 | 경로 | 비용 |
|---|---|---|---|---|---|
| 1 | peak 1037 KB > SRAM (budget_report) | 메모리 fit | 일찍 downsample · fallback 제거 | 모델 | 재학습 |
| 2 | fallback이 latency의 절반 이상, CPU op 8개 (budget_report, CoreML 판정) | CPU fallback | LN → BN, GELU → ReLU6 | 모델 | 재학습 |
| 3 | 큰 conv의 MAC (top-3의 Conv) | compute-bound | 일찍 downsample · DS-conv · 폭 축소 | 모델 | 재학습 |
| 4 | weight 634 KB > 300 KB | 크기 | DS-conv · 폭 축소 | 모델 | 재학습 |
| 5 | 홀수 채널 (62·130·162·250) | lane 낭비 | 16의 배수로 | 모델 | 재학습 |
| 6 | 정확도 52.7% (float도 같음, SQNR은 PASS) | 학습 문제 (원인 미상) | 가설: 정규화·활성 함수 선택 / 해상도 / 레시피 | 모델 | 실험 필요 |

순서를 정하는 규칙은 네 가지다.

1. **blocker 먼저**: SRAM에 안 들어가면 다른 숫자는 의미가 없다(타깃에서 돌릴 수조차 없다). → 1번.
2. **여러 병목을 동시에 줄이는 수정 먼저**: 일찍 downsample하면 feature map의 원소 수가 줄어서 (a) peak, (b) conv MAC, (c) **fallback 시간까지** 같이 준다. fallback 비용은 원소 수에 비례하기 때문이다.
3. **원인을 모르는 병목은 실험으로 좁힌다**: 6번(정확도)은 원인이 가설뿐이다. 바로 고치려 들지 말고, 다음 바퀴들의 변경을 A/B 실험으로 겸하게 설계한다.
4. **한 바퀴에 한 종류**: downsample과 op 교체를 같이 하고 싶은 유혹이 있지만, 그러면 정확도가 바뀌었을 때 원인을 모른다. 4절에서 이 규칙이 실제로 쓸모가 있다.

그래서 v1 = "일찍 downsample"이다.

---

## 4. v1 — 일찍 downsample: 메모리는 맞췄고, 정확도 문제의 원인을 분리한다

### 4.1 수정 내용

spec diff는 stride 네 개뿐이다: `(1, 1, 1, 4)` → `(4, 2, 1, 2)`. 첫 층에서 바로 800 → 200으로 줄이고, 둘째 층에서 100으로 줄인다. 전체 downsample 비율은 v0의 4배에서 16배가 됐다. IMU 제스처의 주파수(6~18 Hz, 가정)는 400 Hz 샘플링에 비해 낮아서, 시간 해상도를 줄여도 정보는 남을 것이라는 가설이다.

손계산 (int8, 블록 3의 입력+출력):

```
v0: 130 × 800 + 162 × 800 = 233,600 B   (int8)   LN fallback float: 162 × 800 × 2 × 4 = 1,036,800 B
v1: 130 × 100 + 162 × 100 =  29,200 B   (int8)   LN fallback float: 162 × 100 × 2 × 4 =   129,600 B
```

말로 하면: 시간 축을 8배 줄였으니 activation도 정확히 8배 줄었다. MAC도 같은 비율로 준다(conv MAC = 출력 길이에 비례).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 410">
<text x="20" y="22" font-size="14">블록별 activation 메모리 (KB, 로그 축) — v0 vs v1</text> <text x="142" y="61" font-size="12" text-anchor="end">블록1 v0 int8</text> <rect x="150" y="51" width="143.9" height="12" fill="#d0564a"/><text x="297.9" y="61" font-size="12">54.4</text> <text x="142" y="77" font-size="12" text-anchor="end">fallback f32</text>
<rect x="150" y="67" width="312.6" height="12" fill="#d0564a" opacity="0.45"/><text x="466.6" y="77" font-size="12">396.8</text> <text x="142" y="99" font-size="12" text-anchor="end">블록1 v1 int8</text> <rect x="150" y="89" width="46.1" height="12" fill="#4a7bd0"/><text x="200.1" y="99" font-size="12">17.2</text> <text x="142" y="115" font-size="12" text-anchor="end">fallback f32</text>
<rect x="150" y="105" width="194.9" height="12" fill="#4a7bd0" opacity="0.45"/><text x="348.9" y="115" font-size="12">99.2</text> <text x="142" y="141" font-size="12" text-anchor="end">블록2 v0 int8</text> <rect x="150" y="131" width="232.0" height="12" fill="#d0564a"/><text x="386.0" y="141" font-size="12">153.6</text> <text x="142" y="157" font-size="12" text-anchor="end">fallback f32</text>
<rect x="150" y="147" width="375.5" height="12" fill="#d0564a" opacity="0.45"/><text x="529.5" y="157" font-size="12">832</text> <text x="142" y="179" font-size="12" text-anchor="end">블록2 v1 int8</text> <rect x="150" y="169" width="79.2" height="12" fill="#4a7bd0"/><text x="233.2" y="179" font-size="12">25.4</text> <text x="142" y="195" font-size="12" text-anchor="end">fallback f32</text>
<rect x="150" y="185" width="198.9" height="12" fill="#4a7bd0" opacity="0.45"/><text x="352.9" y="195" font-size="12">104</text> <text x="142" y="221" font-size="12" text-anchor="end">블록3 v0 int8</text> <rect x="150" y="211" width="267.6" height="12" fill="#d0564a"/><text x="421.6" y="221" font-size="12">233.6</text> <text x="142" y="237" font-size="12" text-anchor="end">fallback f32</text>
<rect x="150" y="227" width="394.2" height="12" fill="#d0564a" opacity="0.45"/><text x="548.2" y="237" font-size="12">1036.8</text> <text x="142" y="259" font-size="12" text-anchor="end">블록3 v1 int8</text> <rect x="150" y="249" width="91.0" height="12" fill="#4a7bd0"/><text x="245.0" y="259" font-size="12">29.2</text> <text x="142" y="275" font-size="12" text-anchor="end">fallback f32</text>
<rect x="150" y="265" width="217.6" height="12" fill="#4a7bd0" opacity="0.45"/><text x="371.6" y="275" font-size="12">129.6</text> <text x="142" y="301" font-size="12" text-anchor="end">블록4 v0 int8</text> <rect x="150" y="291" width="245.3" height="12" fill="#d0564a"/><text x="399.3" y="301" font-size="12">179.6</text> <text x="142" y="317" font-size="12" text-anchor="end">fallback f32</text>
<rect x="150" y="307" width="313.3" height="12" fill="#d0564a" opacity="0.45"/><text x="467.3" y="317" font-size="12">400</text> <text x="142" y="339" font-size="12" text-anchor="end">블록4 v1 int8</text> <rect x="150" y="329" width="89.5" height="12" fill="#4a7bd0"/><text x="243.5" y="339" font-size="12">28.7</text> <text x="142" y="355" font-size="12" text-anchor="end">fallback f32</text>
<rect x="150" y="345" width="195.6" height="12" fill="#4a7bd0" opacity="0.45"/><text x="349.6" y="355" font-size="12">100</text> <line x1="150" y1="374" x2="600" y2="374" stroke="currentColor"/> <line x1="150.0" y1="374" x2="150.0" y2="379" stroke="currentColor"/><text x="150.0" y="392" font-size="12" text-anchor="middle">10 KB</text>
<line x1="345.6" y1="374" x2="345.6" y2="379" stroke="currentColor"/><text x="345.6" y="392" font-size="12" text-anchor="middle">100 KB</text> <line x1="541.1" y1="374" x2="541.1" y2="379" stroke="currentColor"/><text x="541.1" y="392" font-size="12" text-anchor="middle">1000 KB</text>
<line x1="404.4" y1="40" x2="404.4" y2="374" stroke="#e08a3c" stroke-dasharray="5 4"/><text x="404.4" y="36" font-size="12" text-anchor="middle">예산 200 KB</text> <line x1="484.3" y1="40" x2="484.3" y2="374" stroke="currentColor" stroke-dasharray="5 4"/><text x="484.3" y="36" font-size="12" text-anchor="middle">SRAM 512 KB</text>
</svg>
```

그림 4 — 블록별 activation 메모리. 진한 막대는 int8 텐서(입력+출력), 옅은 막대는 그 블록의 LN이 CPU에서 float로 돌 때의 버퍼. v0는 int8만으로도 블록 3이 200 KB 예산을 넘고, float fallback 버퍼는 SRAM 512 KB를 넘는다. v1은 모든 막대가 예산 아래다.

### 4.2 재측정

```text
== v1.onnx  MACs=45.10 M  Mac ORT CPU median=1.40 ms (proxy, 50 runs)
  latency_ms       81.45  budget <= 5.0     FAIL
  peak_act_KB     129.60  budget <= 200     PASS
  weights_KB      633.76  budget <= 300     FAIL
  cpu_ops           8.00  budget <= 0       FAIL
  acc_int8        0.6310  budget >= 0.95    FAIL
  sqnr_dB          15.68  budget >= 25      FAIL
  by class: fallback 46.08 ms  compute 33.42 ms  memory 1.95 ms
  top-3: #18 Conv compute 14.30 ms | #27 Conv compute 12.71 ms | #23 Erf fallback 9.80 ms
```

출력에서 볼 것: latency 498 → 81 ms(6배), peak 1037 → 130 KB로 **메모리 예산 PASS** — 이번 바퀴의 목표는 달성했다. 정확도 줄은 int8 63.1%(float 65.7%)로 v0의 52.75%보다는 올랐지만 **여전히 FAIL**이고, SQNR은 31.1 → 15.7 dB로 오히려 나빠졌다. 정확도 문제의 원인을 여기서 정리하지 않으면, 앞으로 수정할 때마다 "이번 정확도 변화는 왜인가"를 계속 추측하게 된다.

### 4.3 원인 분리 — 2×2 A/B

정확도가 왜 떨어졌나? 가설은 두 개다. (a) downsample이 정보를 버렸다. (b) LayerNorm/GELU 조합이 이 짧은 학습 레시피에서 잘 안 배운다. v1은 downsample만 바꿨으니 (a)가 의심스럽지만, 그렇다고 (b)를 배제할 수는 없다(v0에서도 같은 LN을 썼지만 해상도가 달랐다). 펌웨어에서 "새 빌드에서 에러율이 올랐다"를 bisect하듯, **변수 두 개를 각각 켜고 끈 네 조합**을 같은 레시피로 학습했다. 네 번째 조합(`v0bn`: v0 구조 + BN·ReLU6)은 이 분리를 위해서만 학습했다.

| | LN + GELU | BN + ReLU6 |
|---|---|---|
| downsample 4배 (v0 구조) | v0: 0.5265 | v0bn: 0.9985 |
| downsample 16배 (v1 구조) | v1: 0.6565 (seed 1·2: 0.8480 · 0.6725) | v2: 0.9995 |

읽는 법: **행을 따라가면(downsample 4배 → 16배) 정확도가 거의 그대로이거나 오른다**(0.5265 → 0.6565, 0.9985 → 0.9995). **열을 따라가면(LN·GELU → BN·ReLU6) 정확도가 50%대·60%대에서 99.9%대로 뛴다.** 원인은 downsample이 아니라 정규화·활성 함수 쪽이다. v1을 학습률만 바꿔 같은 레시피로 다시 학습한 결과(1e-3: test 0.8715, train 0.924 / 3e-4: test 0.7345, train 0.882)에서도 학습 정확도부터 낮았다 — 과적합이 아니라 **덜 배운(underfit)** 것이다. 그럴듯한 이유는 이 `ChannelLN`이 시간 축의 매 시점마다 채널 방향으로 정규화한다는 점이다. 제스처 burst가 있는 시점과 잡음뿐인 시점의 크기 차이를 매 시점 지워 버리니, "언제 큰 움직임이 있었나"라는 정보가 약해진다. (이 해석은 가설이다. 확인하려면 LN을 시간·채널 전체로 정규화하는 변형을 한 번 더 A/B 하면 된다.)

이 결과는 v1의 수정(downsample)을 **무죄**로 만들고, 다음 바퀴의 수정(op 교체)이 latency·fallback뿐 아니라 정확도까지 고칠 것이라고 예측하게 해 준다. 예측을 먼저 적어 두고 다음 바퀴에서 확인하는 것 — 측정 기반 디버깅의 기본이다.

### 4.4 이 바퀴에서 배운 것

- **정확도는 매 바퀴 잰다.** 예산표에 정확도 줄이 있었기 때문에 v0부터 정확도 문제가 보였고, v1에서 "downsample 때문에 나빠졌나?"를 바로 물을 수 있었다.
- **한 바퀴 한 수정** 덕분에 원인 후보가 두 개로 좁혀졌고, 2×2 한 번(추가 학습 1회, `v0bn`)으로 분리됐다. 펌웨어 bisect와 같은 비용 감각이다: 실험 하나에 40분(v0bn 학습)이 들었지만, 원인을 모른 채 세 바퀴를 더 도는 것보다 싸다.
- 그 결과가 다음 수정을 정한다: v0의 두 번째 병목(fallback)을 없애는 수정 — LN·GELU 교체 — 이 정확도 문제도 고칠 것으로 예측된다.

---

## 5. v2 — op 교체: LayerNorm → BatchNorm(fold), GELU → ReLU6

### 5.1 왜 이 두 op인가

| op | 배포 그래프에서 | micro-NPU에서 (가정, 일반적 경향) | 양자화 |
|---|---|---|---|
| LayerNorm | 추론 시에도 입력마다 평균·분산을 계산 (`MEAN`, `SQUARED_DIFFERENCE`, `RSQRT` …) | 대개 미지원 또는 느림 → CPU fallback | 분산·제곱근이 int8에 약함 |
| BatchNorm | 추론 시 상수 affine → 앞 conv에 **fold**되어 사라짐 | 노드 자체가 없음 | conv weight에 흡수 |
| GELU | `x·Φ(x)`, opset 17에서 Erf로 풀림 | Erf 미지원이 흔함 | LUT로 가능하지만 범위 설정이 까다로움 |
| ReLU6 | `Clip(0, 6)` | conv 출력 단계에서 거의 공짜(fused activation) | 범위가 [0, 6]으로 고정 → scale이 안정 |

### 5.2 int8 변환과 SQNR — 실제 int8 커널로 (예제 6)

무엇을 확인하는 코드인지(`ex_tflite.py v0 v1 v2`로 실행, TensorFlow 로그 줄은 생략): 같은 spec을 Keras로 다시 만들고 PyTorch 가중치를 옮긴 뒤(이식이 맞는지 float 출력 차이로 먼저 확인), full-int8 TFLite로 변환한다. 파일 크기, op 목록, float 대비 logits SQNR, int8 정확도를 낸다. TFLite의 int8 커널은 실제 MCU용 런타임(TFLite Micro)과 같은 양자화 규칙을 쓰므로, 이 SQNR은 proxy 중에서도 믿을 만한 쪽이다.

```python
# i6_tflite.py (.venv-tf) — 같은 spec을 Keras로 만들고 PyTorch 가중치를 옮겨 int8 TFLite로 변환
import numpy as np, tensorflow as tf
K = tf.keras.layers
def keras_model(sp, W):
    x = inp = tf.keras.Input((sp["L"], 6)); i, cin, todo = 0, 6, []   # TF는 [N, L, C] 순서
    def put(layer, x, w): todo.append((layer, w)); return layer(x)
    def norm_act(x):
        nonlocal i
        if sp["norm"] == "bn":
            x = put(K.BatchNormalization(epsilon=1e-5), x,
                    [W[f"{i}.{n}"] for n in ("weight", "bias", "running_mean", "running_var")])
        else:
            x = put(K.LayerNormalization(epsilon=1e-5), x, [W[f"{i}.ln.weight"], W[f"{i}.ln.bias"]])
        x = K.ReLU(6.0)(x) if sp["act"] == "relu6" else K.Activation(
            lambda t: tf.nn.gelu(t, approximate=False))(x)
        i += 2; return x
    for kind, cout, k, s in sp["blocks"]:
        if kind == "ds":
            x = K.ZeroPadding1D(k // 2)(x)
            x = put(K.DepthwiseConv1D(k, s, use_bias=False), x, [W[f"{i}.weight"].transpose(2, 0, 1)])
            i += 1; x = norm_act(x); k, s = 1, 1
        if k > 1: x = K.ZeroPadding1D(k // 2)(x)                      # torch padding=k//2와 정확히 같게
        wb = [W[f"{i}.weight"].transpose(2, 1, 0)] + ([W[f"{i}.bias"]] if f"{i}.bias" in W else [])
        x = put(K.Conv1D(cout, k, s, use_bias=len(wb) == 2), x, wb); i += 1
        x = norm_act(x); cin = cout
    x = K.GlobalAveragePooling1D()(x); i += 2
    out = put(K.Dense(len(W[f"{i}.bias"])), x, [W[f"{i}.weight"].T, W[f"{i}.bias"]])
    for layer, w in todo: layer.set_weights(w)
    return tf.keras.Model(inp, out)

def to_int8(model, calib):
    c = tf.lite.TFLiteConverter.from_keras_model(model)
    c.optimizations = [tf.lite.Optimize.DEFAULT]
    c.representative_dataset = lambda: ([s[None]] for s in calib)
    c.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    c.inference_input_type = c.inference_output_type = tf.int8
    return c.convert()
```

```python
# int8 TFLite: 파일 크기, op 목록, float 대비 logits SQNR, int8 정확도 (실행: .venv-tf)
import json, sys, collections, numpy as np, tensorflow as tf
from i6_tflite import keras_model, to_int8
SPECS = json.load(open("specs.json")); D = np.load("data.npz")
for v in sys.argv[1:]:
    sp = SPECS[v]; W = dict(np.load(f"{v}_w.npz")); step = 800 // sp["L"]
    Xte = D["Xte"][:, :, ::step].transpose(0, 2, 1); Xc = D["Xcal"][:, :, ::step].transpose(0, 2, 1)
    km = keras_model(sp, W); ref = km.predict(Xte, verbose=0, batch_size=256)
    fl = np.load(f"{v}_torch_logits.npy")                          # PyTorch float 출력과 이식 확인
    blob = to_int8(km, Xc); it = tf.lite.Interpreter(model_content=blob, experimental_op_resolver_type=
        tf.lite.experimental.OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES); it.allocate_tensors()
    i_d, o_d = it.get_input_details()[0], it.get_output_details()[0]
    (si, zi), (so, zo) = i_d["quantization"], o_d["quantization"]
    q = np.empty_like(ref)
    for n, x in enumerate(Xte):
        it.set_tensor(i_d["index"], np.clip(np.round(x[None] / si + zi), -128, 127).astype(np.int8))
        it.invoke(); q[n] = (it.get_tensor(o_d["index"])[0].astype(np.float32) - zo) * so
    sqnr = 10 * np.log10((ref ** 2).sum() / ((ref - q) ** 2).sum())
    ops = collections.Counter(o["op_name"] for o in it._get_ops_details())
    print(f"{v}: keras-vs-torch max|diff|={np.abs(ref - fl).max():.1e}  int8 size={len(blob) / 1024:.1f} KiB  "
          f"SQNR={sqnr:.1f} dB  acc float={np.mean(ref.argmax(1) == D['yte']):.4f} int8={np.mean(q.argmax(1) == D['yte']):.4f}")
    print("   ops:", dict(ops))
    json.dump({"acc_int8": float(np.mean(q.argmax(1) == D["yte"])), "sqnr_dB": float(sqnr)},
              open(f"{v}.metrics.json", "w"))                 # budget_report.py가 읽는다
```

```text
v0: keras-vs-torch max|diff|=5.2e-06  int8 size=654.8 KiB  SQNR=31.1 dB  acc float=0.5265 int8=0.5275
   ops: {'PAD': 4, 'EXPAND_DIMS': 4, 'CONV_2D': 4, 'RESHAPE': 4, 'MEAN': 9, 'DEQUANTIZE': 4, 'NEG': 4, 'QUANTIZE': 4, 'SQUARED_DIFFERENCE': 4, 'ADD': 12, 'RSQRT': 4, 'MUL': 12, 'GELU': 4, 'FULLY_CONNECTED': 1}
v1: keras-vs-torch max|diff|=8.6e-06  int8 size=655.1 KiB  SQNR=15.7 dB  acc float=0.6565 int8=0.6310
   ops: {'PAD': 4, 'EXPAND_DIMS': 4, 'CONV_2D': 4, 'RESHAPE': 4, 'MEAN': 9, 'DEQUANTIZE': 4, 'NEG': 4, 'QUANTIZE': 4, 'SQUARED_DIFFERENCE': 4, 'ADD': 12, 'RSQRT': 4, 'MUL': 12, 'GELU': 4, 'FULLY_CONNECTED': 1}
v2: keras-vs-torch max|diff|=9.5e-06  int8 size=642.3 KiB  SQNR=41.3 dB  acc float=0.9995 int8=1.0000
   ops: {'PAD': 4, 'EXPAND_DIMS': 4, 'CONV_2D': 4, 'RESHAPE': 4, 'MEAN': 1, 'FULLY_CONNECTED': 1}
```

출력에서 볼 것:

- `keras-vs-torch max|diff|`가 1e-5 수준 — 이식이 맞다. 이 줄이 없으면 아래의 모든 숫자가 "이식 버그"일 수 있다(C8의 "기준부터 확인").
- **v0·v1의 op 목록**: LayerNorm이 `MEAN`, `SQUARED_DIFFERENCE`, `RSQRT`, `MUL`, `ADD`, `NEG`로 풀렸고, `QUANTIZE`/`DEQUANTIZE`가 4쌍 들어 있다 — int8 그래프 중간에 float 섬이 생긴 것이다. NPU 입장에서는 이 전부가 partition 경계다.
- **SQNR: v1 15.7 dB → v2 41.3 dB.** v1은 int8로 바꾸면서 정확도가 65.7% → 63.1%로 더 떨어졌고, v2는 int8 정확도 100%. 그런데 같은 LN·GELU 구조인 v0의 SQNR은 31.1 dB로 PASS였다. LN이 언제나 SQNR을 망치는 것은 아니고, 학습된 가중치와 activation 범위에 따라 다르다. **규칙으로 단정하지 말고 버전마다 잰다** — 양자화 hot spot 분류(9절)는 이렇게 측정으로 한다.
- **v2의 op 목록**에는 `PAD`·`EXPAND_DIMS`·`RESHAPE`(1D conv를 2D conv로 표현하기 위한 모양 바꾸기)와 `CONV_2D`, `MEAN`, `FULLY_CONNECTED`뿐이다. BatchNorm은 conv에 접혀서 사라졌다.

### 5.3 BN fold를 손으로 확인 (예제 7)

BN은 추론 시 채널마다 `y = γ·(x − μ)/√(σ² + ε) + β`라는 상수 affine 변환이다. 앞 conv의 출력 `x = W·u + b`에 대입하면

```
s  = γ / √(σ² + ε)                (채널별 배율)
y  = s·(W·u + b − μ) + β
   = (s·W)·u + (s·(b − μ) + β)
W' = s·W        b' = β + s·(b − μ)     (이 conv는 bias가 없으므로 b = 0 → b' = β − μ·s)
```

말로 하면: **BN은 conv의 weight를 채널별로 늘리고 bias를 옮기는 것과 같다.** 추론 그래프에서 BN 노드가 사라지는 이유다.

무엇을 확인하는 코드인지: 학습된 v2의 첫 블록(conv + BN)을 conv 하나로 접고 출력이 같은지 본다. 그리고 ONNX export가 이미 같은 일을 했는지 확인한다.

```python
# BN fold: conv + BatchNorm을 conv 하나로 접고, 출력이 같은지 학습된 v2의 첫 블록으로 확인
import onnx, torch, torch.nn as nn
from i6_common import *
m = build(SPECS["v2"]).eval(); m.load_state_dict(torch.load("v2.pt"))
conv, bn = m[0], m[1]                                     # Conv1d(6->62, k9, s4) + BatchNorm1d(62)
s = bn.weight / torch.sqrt(bn.running_var + bn.eps)       # 채널별 배율 γ/√(σ²+ε)
fold = nn.Conv1d(6, 62, 9, 4, 4, bias=True)
with torch.no_grad():
    fold.weight.copy_(conv.weight * s[:, None, None])     # W' = W·s
    fold.bias.copy_(bn.bias - bn.running_mean * s)         # b' = β − μ·s  (conv bias 없음)
    x = torch.from_numpy(make_data(16, seed=5)[0])
    ref, got = bn(conv(x)), fold(x)
print(f"max |conv+BN - folded conv| = {(ref - got).abs().max():.2e}  (출력 크기 {ref.abs().max():.1f})")
print(f"BN scale s 범위: {s.min():.3f} ~ {s.max():.3f}")
ops = {n.op_type for n in onnx.load("v2.onnx").graph.node}
print("v2.onnx에 BatchNormalization 노드가 있나?", "BatchNormalization" in ops, "| op 종류:", sorted(ops))
```

```text
max |conv+BN - folded conv| = 1.91e-06  (출력 크기 6.9)
BN scale s 범위: 2.313 ~ 3.848
v2.onnx에 BatchNormalization 노드가 있나? False | op 종류: ['Clip', 'Constant', 'Conv', 'Flatten', 'Gemm', 'GlobalAveragePool']
```

출력에서 볼 것: 차이가 1e-6 수준(float32 반올림)이다. `torch.onnx.export`는 eval 모드에서 Conv + BN을 자동으로 접는다 — v2.onnx에는 BatchNormalization 노드가 없다. 접힌 weight의 범위가 원래보다 2~4배 커졌다는 점(s 범위)도 기억하자. per-tensor 양자화라면 채널 간 scale 차이가 문제를 만들 수 있고, 그래서 conv weight는 per-channel 양자화가 표준이다(C1).

### 5.4 재측정

```text
== v2.onnx  MACs=45.10 M  Mac ORT CPU median=1.17 ms (proxy, 50 runs)
  latency_ms       33.53  budget <= 5.0     FAIL
  peak_act_KB      29.20  budget <= 200     PASS
  weights_KB      632.55  budget <= 300     FAIL
  cpu_ops           0.00  budget <= 0       PASS
  acc_int8        1.0000  budget >= 0.95    PASS
  sqnr_dB          41.30  budget >= 25      PASS
  by class: compute 33.42 ms  memory 0.11 ms
  top-3: #4 Conv compute 14.30 ms | #6 Conv compute 12.71 ms | #2 Conv compute 5.22 ms
```

출력에서 볼 것: CPU op 0, latency 81 → 33.5 ms, int8 정확도 100%, SQNR 41.3 dB. **남은 FAIL은 latency와 weight 두 개이고, 시간은 이제 전부 `compute`다.** top-3가 전부 standard conv다. 병목이 이동했다(1.3절).

---

## 6. v3 — depthwise-separable conv + 16채널 정렬

### 6.1 수정 내용과 손계산

v2의 top-1인 블록 3(130 → 162, k=9, L=100)을 예로 든다.

```
standard conv:       130 × 162 × 9 × 100            = 18,954,000 MAC
DS-conv (128→160):   depthwise 128 × 9 × 100         =    115,200 MAC
                     pointwise 128 × 160 × 100       =  2,048,000 MAC
                     합                                =  2,163,200 MAC   (약 8.8배 감소)
weight:              130 × 162 × 9 = 189,540  →  128 × 9 + 128 × 160 = 21,632
```

채널도 64·128·160·256으로 16의 배수에 맞췄다. 가정한 NPU에서 130채널은 144로, 162채널은 176으로 패딩되므로 v2 전체로는 실제 MAC보다 18% 많은 일을 한다(손계산: 패딩 포함 53.2 M / 실제 45.1 M). 정렬하면 이 낭비가 사라진다.

### 6.2 proxy가 거꾸로 말할 때 — 층별 처리율 (예제 8)

v3의 Mac 측정을 해 보면 이상한 일이 생긴다. ORT CPU EP에서는 v2 1.44 ms → v3 0.29 ms로 빨라졌는데, `torch.profiler`로는 v2 약 0.6 ms → v3 약 1.8 ms로 **느려졌다**(아래 7.5절 출력 모음). 같은 모델, 같은 CPU다. 예제 4를 v3에 돌려 보면 단서가 보인다(이번 실행은 1.42~1.53 ms — 실행마다 이 정도는 흔들린다).

```sh
python ex_profile.py v3
```

```text
v3: per-inference self CPU total (ms) per rep = [1.53, 1.42, 1.48]
  aten::_slow_conv2d_forward    34.8 %
  aten::_convolution            13.5 %
  aten::narrow                  10.4 %
  aten::slice                    9.5 %
  aten::empty                    7.6 %
  aten::as_strided               5.4 %
```

`aten::narrow`, `aten::slice`, `aten::as_strided`, `aten::empty` 같은 "자르고 새로 할당하는" op가 합쳐서 3분의 1이다. 계산이 아니라 텐서를 쪼개는 데 시간을 쓰고 있다.

무엇을 확인하는 코드인지: v3의 conv 층마다(시간만 보므로 학습 전 가중치로) PyTorch eager 시간을 재서 달성 GMAC/s와 arithmetic intensity(int8 기준 최소 트래픽)를 나란히 놓는다.

```python
# v3의 conv 층별로 Mac CPU(1 thread) 시간을 재서 "MAC/s 효율"과 "MAC/byte"를 나란히 놓는다
import time, statistics as st, torch, torch.nn as nn
from i6_common import *
torch.set_num_threads(1)
m = build(SPECS["v3"]).eval(); x = torch.zeros(1, 6, 800); rows = []
with torch.no_grad():
    for layer in m:
        if isinstance(layer, nn.Conv1d):
            ts = []
            for _ in range(7):                                   # 7회 반복, 각 회 200번 실행의 평균
                t = time.perf_counter()
                for _ in range(200): y = layer(x)
                ts.append((time.perf_counter() - t) / 200)
            y = layer(x); macs = layer.weight.numel() * y.shape[-1]
            byt = x.numel() + y.numel() + layer.weight.numel()      # int8로 가정한 최소 트래픽(바이트)
            kind = "DW" if layer.groups > 1 else ("PW" if layer.kernel_size[0] == 1 else "Conv")
            rows.append((kind, tuple(y.shape[1:]), macs, st.median(ts) * 1e6, macs / byt))
        x = layer(x)
for kind, shp, macs, us, ai in rows:
    print(f"{kind:4s} out={str(shp):11s} MACs={macs / 1e3:7.1f}K  {us:6.1f} us  {macs / us / 1e3:5.2f} GMAC/s  AI={ai:5.1f} MAC/B")
```

```text
Conv out=(64, 200)   MACs=  691.2K    30.2 us  22.90 GMAC/s  AI= 32.8 MAC/B
DW   out=(64, 100)   MACs=   57.6K   230.7 us   0.25 GMAC/s  AI=  2.9 MAC/B
PW   out=(128, 100)  MACs=  819.2K    12.1 us  67.97 GMAC/s  AI= 29.9 MAC/B
DW   out=(128, 100)  MACs=  115.2K   308.3 us   0.37 GMAC/s  AI=  4.3 MAC/B
PW   out=(160, 100)  MACs= 2048.0K    16.5 us  124.02 GMAC/s  AI= 41.6 MAC/B
DW   out=(160, 50)   MACs=   72.0K   436.8 us   0.16 GMAC/s  AI=  2.8 MAC/B
PW   out=(256, 50)   MACs= 2048.0K    18.4 us  111.43 GMAC/s  AI= 33.2 MAC/B
```

출력에서 볼 것: pointwise(PW)는 수십 GMAC/s인데 depthwise(DW)는 **0.16~0.37 GMAC/s**로 수백 배 느리다. DW의 arithmetic intensity(약 3 MAC/B)가 낮으니 memory-bound라고 말하고 싶지만, 이 CPU의 메모리 대역폭을 보수적으로 50 GB/s로 잡아도 지붕은 `2.9 MAC/B × 50 GB/s ≈ 145 GMAC/s`이니 한참 아래다(게다가 이 크기의 텐서는 캐시에 들어간다). **compute도 memory도 아닌 "커널 품질" 문제**다 — 위 프로파일의 `narrow`·`slice`로 보아 PyTorch eager가 이 CPU에서 grouped conv1d를 그룹별로 잘라서 도는 느린 경로를 타는 것으로 보인다. 같은 그래프를 ORT로 돌리면 문제가 사라진다. 9.2절의 판정 규칙("두 지붕 어느 쪽에도 못 미치면 커널을 의심")의 실례이고, 이 경우 올바른 수정은 모델이 아니라 **런타임**이다. 타깃 NPU라면 DW 효율은 컴파일러가 정한다 — 그래서 budget_report는 DW를 별도 효율(1/8, 가정)로 계산한다.

### 6.3 재측정

```text
== v3.onnx  MACs=5.85 M  Mac ORT CPU median=0.24 ms (proxy, 50 runs)
  latency_ms        5.83  budget <= 5.0     FAIL
  peak_act_KB      28.80  budget <= 200     PASS
  weights_KB       79.29  budget <= 300     PASS
  cpu_ops           0.00  budget <= 0       PASS
  acc_int8        1.0000  budget >= 0.95    PASS
  sqnr_dB          39.11  budget >= 25      PASS
  by class: compute 5.73 ms  memory 0.11 ms
  top-3: #8 Conv compute 1.32 ms | #12 Conv compute 1.32 ms | #0 Conv compute 1.19 ms
```

출력에서 볼 것: MAC 45.1 M → 5.85 M, weight 633 KB → 79 KB(PASS), latency 33.5 → 5.83 ms. **아직 예산(5 ms)을 17% 넘는다.** int8 정확도 100%, SQNR 39.1 dB. top-3는 이제 pointwise conv 두 개와 **첫 층**(입력 6채널 conv)이다. 첫 층은 입력 채널 6이 16으로 패딩되어 2.67배를 낭비한다 — 10절에서 이것을 컴파일러 경로 후보로 다시 본다.

---

## 7. v4 — 폭·입력률 축소 + 재학습, 그리고 후보 중에서 고르기

### 7.1 후보 세 개

v3는 latency만 17% 초과다. 남은 손잡이는 폭(채널 수), 해상도(입력 샘플 수), 깊이다. 이번 바퀴에서는 **후보를 여러 개 만들어 같은 harness로 비교**한다.

| 후보 | 변경 | 제안자 (가정) | 기대 |
|---|---|---|---|
| v4 | 채널 64·128·160·256 → 32·64·128·160, 입력 400 → 200 Hz(첫 층 stride 4 → 2로 같은 시간 해상도 유지) | 모델팀 | latency 절반, 센서 ODR도 절반 |
| v4n | 채널을 16·16·32·32로 더 좁게 | 모델팀 (공격적 안) | latency 최소 |
| v4r | v4 + 입력 100 Hz (센서 전력 절감) | HW팀 | 센서 전력 추가 절감 |

### 7.2 재학습 — seed 세 개로 (예제 2의 seed 실행)

2.2절 출력에서 해당 줄만 다시 모으면:

| 후보 | 타깃 latency 추정 | float 정확도 seed 0 / 1 / 2 | 최솟값 | 판정 |
|---|---|---|---|---|
| v3 (기준) | 5.83 ms | 1.0000 / 1.0000 / 0.9995 | 0.9995 | latency FAIL |
| v4 | 2.55 ms | 0.9905 / 0.9815 / 0.9730 | 0.9730 | PASS |
| v4n | 0.90 ms | 0.7810 / — / — | 0.7810 | 정확도 FAIL (seed 1개로도 충분히 명확) |
| v4r | 2.55 ms | 0.9335 / 0.9465 / 0.9450 | 0.9335 | 정확도 FAIL |

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 380">
<text x="20" y="22" font-size="14">v4 후보 고르기 — 타깃 latency 추정 vs float 정확도 (seed 범위)</text> <rect x="80" y="50.0" width="334.3" height="54.0" fill="#3f9a6b" opacity="0.12"/> <line x1="80" y1="50" x2="80" y2="320" stroke="currentColor"/><line x1="80" y1="320" x2="600" y2="320" stroke="currentColor"/>
<line x1="76" y1="320.0" x2="80" y2="320.0" stroke="currentColor"/><text x="72" y="324.0" font-size="12" text-anchor="end">75%</text> <line x1="76" y1="266.0" x2="80" y2="266.0" stroke="currentColor"/><text x="72" y="270.0" font-size="12" text-anchor="end">80%</text> <line x1="76" y1="212.0" x2="80" y2="212.0" stroke="currentColor"/><text x="72" y="216.0" font-size="12" text-anchor="end">85%</text>
<line x1="76" y1="158.0" x2="80" y2="158.0" stroke="currentColor"/><text x="72" y="162.0" font-size="12" text-anchor="end">90%</text> <line x1="76" y1="104.0" x2="80" y2="104.0" stroke="currentColor"/><text x="72" y="108.0" font-size="12" text-anchor="end">95%</text> <line x1="76" y1="50.0" x2="80" y2="50.0" stroke="currentColor"/><text x="72" y="54.0" font-size="12" text-anchor="end">100%</text>
<line x1="80.0" y1="320" x2="80.0" y2="324" stroke="currentColor"/><text x="80.0" y="338" font-size="12" text-anchor="middle">0</text> <line x1="154.3" y1="320" x2="154.3" y2="324" stroke="currentColor"/><text x="154.3" y="338" font-size="12" text-anchor="middle">1</text> <line x1="228.6" y1="320" x2="228.6" y2="324" stroke="currentColor"/><text x="228.6" y="338" font-size="12" text-anchor="middle">2</text>
<line x1="302.9" y1="320" x2="302.9" y2="324" stroke="currentColor"/><text x="302.9" y="338" font-size="12" text-anchor="middle">3</text> <line x1="377.1" y1="320" x2="377.1" y2="324" stroke="currentColor"/><text x="377.1" y="338" font-size="12" text-anchor="middle">4</text> <line x1="451.4" y1="320" x2="451.4" y2="324" stroke="currentColor"/><text x="451.4" y="338" font-size="12" text-anchor="middle">5</text>
<line x1="525.7" y1="320" x2="525.7" y2="324" stroke="currentColor"/><text x="525.7" y="338" font-size="12" text-anchor="middle">6</text> <line x1="600.0" y1="320" x2="600.0" y2="324" stroke="currentColor"/><text x="600.0" y="338" font-size="12" text-anchor="middle">7</text> <text x="340.0" y="356" font-size="12" text-anchor="middle">타깃 latency 추정 (ms)</text>
<line x1="451.4" y1="50" x2="451.4" y2="320" stroke="#d0564a" stroke-dasharray="6 4"/><text x="455.4" y="60" font-size="12">5 ms 예산</text> <line x1="80" y1="104.0" x2="600" y2="104.0" stroke="#d0564a" stroke-dasharray="6 4"/><text x="596" y="120.0" font-size="12" text-anchor="end">95% 예산</text> <line x1="513.1" y1="50.5" x2="513.1" y2="50.0" stroke="#888" stroke-width="2"/>
<circle cx="513.1" cy="50.2" r="5" fill="#888"/> <text x="523.1" y="54.2" font-size="12">v3: 예산 초과 (5.83 ms)</text> <line x1="269.4" y1="79.2" x2="269.4" y2="60.3" stroke="#3f9a6b" stroke-width="2"/> <circle cx="269.4" cy="69.8" r="5" fill="#3f9a6b"/> <text x="279.4" y="73.8" font-size="12">v4: 채택</text> <line x1="283.4" y1="121.8" x2="283.4" y2="107.8" stroke="#e08a3c" stroke-width="2"/>
<circle cx="283.4" cy="113.0" r="5" fill="#e08a3c"/> <text x="293.4" y="117.0" font-size="12">v4r (100 Hz): 정확도 미달</text> <line x1="146.9" y1="286.5" x2="146.9" y2="286.5" stroke="#d0564a" stroke-width="2"/> <circle cx="146.9" cy="286.5" r="5" fill="#d0564a"/> <text x="156.9" y="290.5" font-size="12">v4n (좁게): 정확도 미달 (seed 1개)</text>
<text x="20" y="372" font-size="12">초록 영역 = 예산 + margin 안 (latency ≤ 4.5 ms, 정확도 ≥ 95%). 세로 막대 = seed 0·1·2의 최소~최대, 점 = 평균</text>
</svg>
```

그림 7 — v4 후보의 latency 추정과 정확도 범위. v4만 초록 영역(예산 + margin 안)에 있다.

출력에서 볼 것:

- **같은 v4 구조라도 seed에 따라 97.3% ~ 99.05%**로 1.75%p 흔들린다. 후보 간 차이가 이 폭보다 작으면 seed 하나로는 순위를 정할 수 없다. 판정은 **최솟값**으로 한다(보수적).
- **v4r은 latency 이득이 없다.** 첫 층이 stride 1로 200 샘플을 출력하므로 v4와 출력 shape이 같고 MAC도 같다(2.10 M). 얻는 것은 센서 ODR 절반(센서 전력, I1)뿐인데 정확도가 3개 seed 모두 95% 아래다. HW팀 제안을 **숫자로** 거절할 수 있다. 조건부 대안: 100 Hz에서 첫 층을 다시 설계한 별도 후보를 다음 바퀴에 평가.
- v4n은 latency가 가장 작지만 정확도가 78%다. "가장 빠른 후보"가 아니라 "예산 안에서 margin을 남기는 후보"를 고른다.

### 7.3 재측정 — 모든 예산 PASS

v4를 int8로 변환하고(예제 6을 `v3 v4`로 실행) 예산표를 낸다.

```text
v3: keras-vs-torch max|diff|=8.6e-06  int8 size=115.6 KiB  SQNR=39.1 dB  acc float=1.0000 int8=1.0000
   ops: {'PAD': 4, 'EXPAND_DIMS': 7, 'CONV_2D': 4, 'RESHAPE': 7, 'DEPTHWISE_CONV_2D': 3, 'MEAN': 1, 'FULLY_CONNECTED': 1}
v4: keras-vs-torch max|diff|=3.8e-06  int8 size=64.7 KiB  SQNR=37.4 dB  acc float=0.9905 int8=0.9905
   ops: {'PAD': 4, 'EXPAND_DIMS': 7, 'CONV_2D': 4, 'RESHAPE': 7, 'DEPTHWISE_CONV_2D': 3, 'MEAN': 1, 'FULLY_CONNECTED': 1}
```

```text
== v4.onnx  MACs=2.10 M  Mac ORT CPU median=0.11 ms (proxy, 50 runs)
  latency_ms        2.55  budget <= 5.0     PASS
  peak_act_KB      14.40  budget <= 200     PASS
  weights_KB       36.37  budget <= 300     PASS
  cpu_ops           0.00  budget <= 0       PASS
  acc_int8        0.9905  budget >= 0.95    PASS
  sqnr_dB          37.45  budget >= 25      PASS
  by class: compute 2.46 ms  memory 0.10 ms
  top-3: #12 Conv compute 0.68 ms | #0 Conv compute 0.62 ms | #10 DWConv compute 0.33 ms
```

출력에서 볼 것: **모든 줄이 PASS.** latency 2.55 ms(예산의 51%), peak 14.4 KB(7%), weight 36.4 KB(12%), CPU op 0, int8 정확도 99.05%, SQNR 37.4 dB. 참고로 int8 TFLite 파일은 64.7 KiB인데(변환할 때마다 수백 바이트씩 달라서 64.4~64.8 KiB가 나왔다) budget_report의 weight는 36.4 KB다. 파일 안의 상수를 세어 보면 int8 weight 35,744 B + int32 bias 2,568 B이고, 나머지 약 27 KB는 flatbuffer 구조(양자화 파라미터, 텐서 이름, op 정의)다. **flash 예산은 파일 크기로 비교하는 것이 보수적**이다.

### 7.4 실제 백엔드가 하나 더 거부한다 — GlobalAveragePool (예제 9)

budget_report의 allowlist에는 `GlobalAveragePool`이 들어 있어서 CPU op 0이 나왔다. 하지만 3.2절에서 CoreML EP는 이 노드를 거부했다. **지원표(가정)와 실제 백엔드의 판정이 다르다** — F8에서 말한 "지원표를 믿지 말고 직접 물어본다"의 실례다. 거부 이유(3D 입력이라서인지 등)를 다 알아낼 필요는 없다. 같은 수학을 다른 op로 표현해서 다시 물어보는 것이 가장 싸다.

무엇을 확인하는 코드인지: 마지막 pooling을 `x.mean(dim=-1)`(ONNX `ReduceMean`)로 바꿔 다시 export하고, CoreML EP 판정과 출력 차이를 본다.

```python
# 마지막 남은 CPU 노드(GlobalAveragePool, 3D 입력)를 같은 수학의 다른 표현으로 바꿔 CoreML EP에 다시 묻는다
import os, re, tempfile, torch, torch.nn as nn, onnxruntime as ort
from i6_common import *
class MeanPool(nn.Module):                       # AdaptiveAvgPool1d(1)과 같은 값: 시간 축 평균
    def forward(self, x): return x.mean(dim=-1, keepdim=True)

def coreml_unsupported(path):
    so = ort.SessionOptions(); so.log_severity_level = 0
    fd, log = tempfile.mkstemp(); saved = os.dup(2); os.dup2(fd, 2)
    try: ort.InferenceSession(path, so, providers=["CoreMLExecutionProvider", "CPUExecutionProvider"])
    finally: os.dup2(saved, 2); os.close(fd)
    sup = re.findall(r"Operator type: \[(\w+)\] index: \[\d+\] name: \[[^\]]*\] supported: \[(\d)\]", open(log).read())
    return [op for op, ok in sup if ok == "0"], len(sup)

m = build(SPECS["v4"]).eval(); m.load_state_dict(torch.load("v4.pt")); x = torch.randn(4, 6, 400)
with torch.no_grad(): before = m(x)
for tag in ["GAP", "ReduceMean"]:
    if tag == "ReduceMean": m[-3] = MeanPool()
    torch.onnx.export(m, torch.zeros(1, 6, 400), f"v4_{tag}.onnx", opset_version=17, dynamo=False)
    with torch.no_grad(): d = (m(x) - before).abs().max().item()
    print(f"{tag:10s} CoreML 미지원 노드 = {coreml_unsupported(f'v4_{tag}.onnx')}   출력 차이 = {d:.1e}")
```

```text
GAP        CoreML 미지원 노드 = (['GlobalAveragePool'], 17)   출력 차이 = 0.0e+00
ReduceMean CoreML 미지원 노드 = ([], 17)   출력 차이 = 0.0e+00
```

출력에서 볼 것: `ReduceMean`으로 바꾸자 미지원 노드가 0개가 됐고, 출력 차이는 0이다(같은 계산). 재학습이 필요 없는 **그래프 수준 수정**이라 몇 분이면 끝난다. 10절의 경로 중 가장 싼 "같은 수학의 다른 표현" 칸에 해당한다.

### 7.5 proxy 숫자 모음 — 타깃 추정과 비율이 다르다

v0~v4의 Mac 측정을 같은 표에 모으면(예제 4·5를 각 버전에 실행해 첫 줄만 모음, 타깃 추정은 budget_report):

```text
v0: per-inference self CPU total (ms) per rep = [2.61, 2.67, 2.65]
v1: per-inference self CPU total (ms) per rep = [0.76, 0.71, 0.83]
v2: per-inference self CPU total (ms) per rep = [0.76, 0.5, 0.59]
v3: per-inference self CPU total (ms) per rep = [1.8, 1.69, 2.01]
v4: per-inference self CPU total (ms) per rep = [1.25, 1.22, 1.1]
v0: ORT CPU EP, sum of per-node medians = 8.56 ms
v1: ORT CPU EP, sum of per-node medians = 1.62 ms
v2: ORT CPU EP, sum of per-node medians = 1.44 ms
v3: ORT CPU EP, sum of per-node medians = 0.29 ms
v4: ORT CPU EP, sum of per-node medians = 0.13 ms
```

| 버전 | 타깃 latency 추정 | Mac ORT CPU (프로파일 합) | Mac torch eager (3회 중앙값) | 추정 ÷ ORT |
|---|---|---|---|---|
| v0 | 498.03 ms | 8.56 ms | 2.65 ms | 58 |
| v1 | 81.45 ms | 1.62 ms | 0.76 ms | 50 |
| v2 | 33.53 ms | 1.44 ms | 0.59 ms | 23 |
| v3 | 5.83 ms | 0.29 ms | 1.80 ms | 20 |
| v4 | 2.55 ms | 0.13 ms | 1.22 ms | 20 |

말로 하면: **proxy와 타깃 추정의 비율이 버전마다 다르다.** fallback이 있는 v0·v1에서 비율이 크고(타깃에서만 fallback이 비싸다), torch eager는 v3에서 오히려 느려진다(depthwise 커널 품질). proxy로 "v3가 v2보다 나쁘다"고 결론 내렸다면 정반대의 결정을 했을 것이다.

---

## 8. 다섯 바퀴 전체 보기

| 항목 (예산) | v0 | v1 | v2 | v3 | v4 |
|---|---|---|---|---|---|
| 이번 수정 | — | 일찍 downsample | LN·GELU → BN·ReLU6 | DS-conv + 16 정렬 | 폭 축소 + 200 Hz |
| MACs | 285.2 M | 45.1 M | 45.1 M | 5.85 M | 2.10 M |
| latency 추정 (≤ 5 ms) | 498.0 FAIL | 81.5 FAIL | 33.5 FAIL | 5.83 FAIL | **2.55 PASS** |
| peak activation (≤ 200 KB) | 1036.8 FAIL | 129.6 PASS | 29.2 PASS | 28.8 PASS | **14.4 PASS** |
| weights (≤ 300 KB) | 633.8 FAIL | 633.8 FAIL | 632.6 FAIL | 79.3 PASS | **36.4 PASS** |
| CPU op (= 0) | 8 FAIL | 8 FAIL | 0 PASS | 0 PASS | **0 PASS** |
| int8 정확도 (≥ 95%) | 52.75% FAIL | 63.1% FAIL | 100% PASS | 100% PASS | **99.05% PASS** |
| SQNR (≥ 25 dB) | 31.1 PASS | 15.7 FAIL | 41.3 PASS | 39.1 PASS | **37.4 PASS** |
| int8 TFLite 파일 | 654.8 KiB | 655.1 KiB | 642.3 KiB | 115.6 KiB | **64.7 KiB** |
| 주인공 병목 (다음 바퀴의 이유) | SRAM fit + fallback | fallback + 정확도 (원인: LN·GELU) | compute (standard conv) | compute (PW + 첫 층) | — (margin 49%) |

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 420">
<text x="20" y="22" font-size="14">반복별 예산 사용률 (값 ÷ 예산, 로그 축) — 1 아래로 내려가야 한다</text> <line x1="90" y1="50" x2="90" y2="330" stroke="currentColor"/><line x1="90" y1="330" x2="560" y2="330" stroke="currentColor"/> <line x1="86" y1="330.0" x2="90" y2="330.0" stroke="currentColor"/><text x="82" y="334.0" font-size="12" text-anchor="end">0.05</text>
<line x1="86" y1="306.6" x2="90" y2="306.6" stroke="currentColor"/><text x="82" y="310.6" font-size="12" text-anchor="end">0.1</text> <line x1="86" y1="252.3" x2="90" y2="252.3" stroke="currentColor"/><text x="82" y="256.3" font-size="12" text-anchor="end">0.5</text> <line x1="86" y1="228.9" x2="90" y2="228.9" stroke="currentColor"/><text x="82" y="232.9" font-size="12" text-anchor="end">1</text>
<line x1="86" y1="151.1" x2="90" y2="151.1" stroke="currentColor"/><text x="82" y="155.1" font-size="12" text-anchor="end">10</text> <line x1="86" y1="73.4" x2="90" y2="73.4" stroke="currentColor"/><text x="82" y="77.4" font-size="12" text-anchor="end">100</text> <line x1="90" y1="228.9" x2="560" y2="228.9" stroke="#d0564a" stroke-dasharray="6 4"/><text x="566" y="232.9" font-size="12">예산 (1.0)</text>
<line x1="90" y1="232.4" x2="560" y2="232.4" stroke="#888" stroke-dasharray="2 3"/><text x="566" y="246.4" font-size="12">margin (0.9)</text> <text x="90.0" y="348" font-size="12" text-anchor="middle">v0</text> <text x="207.5" y="348" font-size="12" text-anchor="middle">v1</text> <text x="325.0" y="348" font-size="12" text-anchor="middle">v2</text> <text x="442.5" y="348" font-size="12" text-anchor="middle">v3</text>
<text x="560.0" y="348" font-size="12" text-anchor="middle">v4</text> <polyline points="90.0,73.5 207.5,134.7 325.0,164.6 442.5,223.7 560.0,251.6" fill="none" stroke="#d0564a" stroke-width="2"/> <circle cx="90.0" cy="73.5" r="4" fill="#d0564a"/> <circle cx="207.5" cy="134.7" r="4" fill="#d0564a"/> <circle cx="325.0" cy="164.6" r="4" fill="#d0564a"/> <circle cx="442.5" cy="223.7" r="4" fill="#d0564a"/>
<circle cx="560.0" cy="251.6" r="4" fill="#d0564a"/> <line x1="566" y1="70" x2="586" y2="70" stroke="#d0564a" stroke-width="2"/><text x="590" y="74" font-size="12">latency (추정)</text> <polyline points="90.0,173.3 207.5,243.5 325.0,293.8 442.5,294.3 560.0,317.7" fill="none" stroke="#4a7bd0" stroke-width="2"/> <circle cx="90.0" cy="173.3" r="4" fill="#4a7bd0"/> <circle cx="207.5" cy="243.5" r="4" fill="#4a7bd0"/>
<circle cx="325.0" cy="293.8" r="4" fill="#4a7bd0"/> <circle cx="442.5" cy="294.3" r="4" fill="#4a7bd0"/> <circle cx="560.0" cy="317.7" r="4" fill="#4a7bd0"/> <line x1="566" y1="90" x2="586" y2="90" stroke="#4a7bd0" stroke-width="2"/><text x="590" y="94" font-size="12">peak activation</text> <polyline points="90.0,203.6 207.5,203.6 325.0,203.7 442.5,273.8 560.0,300.1" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<circle cx="90.0" cy="203.6" r="4" fill="#3f9a6b"/> <circle cx="207.5" cy="203.6" r="4" fill="#3f9a6b"/> <circle cx="325.0" cy="203.7" r="4" fill="#3f9a6b"/> <circle cx="442.5" cy="273.8" r="4" fill="#3f9a6b"/> <circle cx="560.0" cy="300.1" r="4" fill="#3f9a6b"/> <line x1="566" y1="110" x2="586" y2="110" stroke="#3f9a6b" stroke-width="2"/><text x="590" y="114" font-size="12">weights</text>
<text x="20" y="374" font-size="12">CPU op</text><text x="20" y="394" font-size="12">int8 acc</text> <text x="90.0" y="374" font-size="12" text-anchor="middle">8</text><text x="90.0" y="394" font-size="12" text-anchor="middle">52.8%</text> <text x="207.5" y="374" font-size="12" text-anchor="middle">8</text><text x="207.5" y="394" font-size="12" text-anchor="middle">63.1%</text>
<text x="325.0" y="374" font-size="12" text-anchor="middle">0</text><text x="325.0" y="394" font-size="12" text-anchor="middle">100%</text> <text x="442.5" y="374" font-size="12" text-anchor="middle">0</text><text x="442.5" y="394" font-size="12" text-anchor="middle">100%</text> <text x="560.0" y="374" font-size="12" text-anchor="middle">0</text><text x="560.0" y="394" font-size="12" text-anchor="middle">99.05%</text>
<text x="566" y="394" font-size="12">(예산 ≥ 95%)</text>
</svg>
```

그림 2 — 반복별 예산 사용률(값 ÷ 예산, 로그 축). 세 선이 모두 margin선(0.9) 아래로 내려온 것이 v4다. 아래 두 줄은 CPU fallback op 수와 int8 정확도.

이 표에서 읽을 것:

- **수정마다 움직이는 선이 다르다.** downsample은 peak와 latency를, op 교체는 fallback·정확도·SQNR을, DS-conv는 weight와 MAC을, 폭 축소는 latency의 마지막 17%를 움직였다. 한 바퀴 한 종류였기 때문에 이렇게 읽을 수 있다.
- **v4에 margin이 49%나 남은 것이 낭비인가?** 아니다. 이 latency는 가정한 비용 모델의 추정이다. 타깃 실측에서 커널 효율이 가정보다 나쁘거나, 다른 작업과 메모리 대역폭을 나눠 쓰면 숫자가 오른다. 남은 margin은 그 불확실성을 위한 것이고, 타깃 실측 후에 "폭을 다시 늘려 정확도를 올릴지"를 다음 바퀴에서 결정한다.
- 정확도 줄의 v0·v1은 **latency와 무관한 문제가 처음부터 있었다**는 것을 보여 준다. 예산표에 정확도 줄이 없었으면, v2의 op 교체가 왜 정확도까지 고쳤는지 설명하지 못했을 것이다.

---

## 9. 병목 분류 가이드 — 프로파일의 증상 → 고칠 후보 (비용순)

v0 → v4에서 만난 병목은 세 종류(memory/fit, fallback, compute)였다. 실무에서는 여섯 종류를 구분한다. 분류 순서가 중요하다. **모델 밖 시간 → 가속기 밖 op → 연산 효율 → 메모리** 순으로 묻는다. 앞의 것일수록 싸게 크게 줄일 수 있고, 뒤의 것을 먼저 고치면 앞의 것이 그대로 남기 때문이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 470">
<defs><marker id="i6a5" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="20" y="22" font-size="14">병목 분류 순서 — 시간 비중이 큰 구간 하나를 들고 위에서부터 묻는다</text> <rect x="60" y="36" width="220" height="34" rx="6" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="170" y="58" font-size="12" text-anchor="middle">프로파일 top-N 구간 (시간 비중순)</text> <polygon points="170,86 275,116 170,146 65,116" fill="none" stroke="currentColor" stroke-width="1.5"/> <text x="170" y="113" font-size="12" text-anchor="middle">모델 밖 시간이 큰가?</text> <text x="170" y="129" font-size="12" text-anchor="middle">(전처리·후처리·대기)</text>
<polygon points="170,166 275,196 170,226 65,196" fill="none" stroke="currentColor" stroke-width="1.5"/> <text x="170" y="193" font-size="12" text-anchor="middle">NPU 밖(CPU)에서</text> <text x="170" y="209" font-size="12" text-anchor="middle">도는 op인가?</text> <polygon points="170,246 275,276 170,306 65,276" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="170" y="273" font-size="12" text-anchor="middle">달성 MAC/s가</text> <text x="170" y="289" font-size="12" text-anchor="middle">피크의 절반 이상?</text> <polygon points="170,326 275,356 170,386 65,356" fill="none" stroke="currentColor" stroke-width="1.5"/> <text x="170" y="353" font-size="12" text-anchor="middle">MAC/byte가</text> <text x="170" y="369" font-size="12" text-anchor="middle">ridge point보다 작나?</text>
<line x1="170" y1="70" x2="170" y2="84" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a5)"/> <line x1="170" y1="146" x2="170" y2="164" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a5)"/> <line x1="170" y1="226" x2="170" y2="244" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a5)"/>
<line x1="170" y1="306" x2="170" y2="324" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a5)"/> <text x="178" y="159" font-size="12">아니오</text><text x="178" y="239" font-size="12">아니오</text><text x="178" y="319" font-size="12">아니오</text> <rect x="330" y="96" width="330" height="40" rx="6" fill="none" stroke="#888" stroke-width="2"/>
<text x="495" y="113" font-size="12" text-anchor="middle">pre/post · scheduling/IPC 지배 (I4 · E8)</text> <text x="495" y="129" font-size="12" text-anchor="middle">→ 특징 추출 위치 · batching · 깨우기 횟수</text> <rect x="330" y="176" width="330" height="40" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="495" y="193" font-size="12" text-anchor="middle">CPU fallback · partition (F5 · F8)</text>
<text x="495" y="209" font-size="12" text-anchor="middle">→ op 교체 · 그래프 수술 · 벤더 커널</text> <rect x="330" y="256" width="330" height="40" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="495" y="273" font-size="12" text-anchor="middle">compute-bound (D3)</text> <text x="495" y="289" font-size="12" text-anchor="middle">→ MAC 줄이기: DS-conv · 폭 · 해상도 · 채널 정렬</text>
<rect x="330" y="336" width="330" height="40" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="495" y="353" font-size="12" text-anchor="middle">memory-bound (D3 · D2)</text> <text x="495" y="369" font-size="12" text-anchor="middle">→ fusion · tiling · weight 재사용 · SRAM 배치</text> <rect x="330" y="406" width="330" height="40" rx="6" fill="none" stroke="#888" stroke-width="2" stroke-dasharray="5 3"/>
<text x="495" y="423" font-size="12" text-anchor="middle">overhead-bound: 작은 층이 너무 많다</text> <text x="495" y="439" font-size="12" text-anchor="middle">→ 층 합치기 · 고정비(층당 setup) 확인</text> <line x1="275" y1="116" x2="328" y2="116" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a5)"/> <line x1="275" y1="196" x2="328" y2="196" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a5)"/>
<line x1="275" y1="276" x2="328" y2="276" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a5)"/> <line x1="275" y1="356" x2="328" y2="356" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a5)"/> <path d="M 170 386 L 170 426 L 328 426" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a5)"/>
<text x="290" y="110" font-size="12">예</text><text x="290" y="190" font-size="12">예</text><text x="290" y="270" font-size="12">예</text><text x="290" y="350" font-size="12">예</text><text x="178" y="404" font-size="12">아니오</text> <text x="20" y="464" font-size="12">정확도 쪽은 별도 트랙: int8 정확도·SQNR이 떨어지면 레이어별 SQNR(C8)로 "처음 무너지는 층" = quant hot spot</text>
</svg>
```

그림 5 — 병목 분류 순서. 시간 비중이 가장 큰 구간 하나를 들고 위에서부터 묻는다. 정확도 문제는 별도 트랙(C8 레이어별 SQNR)이다.

### 9.1 증상과 후보 표

| 병목 종류 | 프로파일에서 보이는 증상 | 확인 방법 | 고칠 후보 (싼 것부터) |
|---|---|---|---|
| compute-bound (D3) | 시간이 MAC 수에 비례, 달성 MAC/s가 피크의 50% 이상, 큰 conv·matmul이 top | 층별 `MAC ÷ 시간`을 피크와 비교, arithmetic intensity가 ridge point보다 큼 | 채널 lane 정렬 → DS-conv → 폭·해상도 축소 → 첫 층 특수 처리 → 더 큰 NPU 설정(클럭) |
| memory-bound (D3·D2) | MAC은 적은데 시간이 길다, 시간이 바이트 수에 비례, DMA·SRAM spill 이벤트 | `MAC ÷ 바이트`가 ridge point보다 작음, 벤더 프로파일러의 DMA/stall 열 | 컴파일러 fusion·tiling 옵션 → layout(NHWC 등) 맞추기 → weight를 SRAM에 상주 → feature map 줄이기(downsample) |
| CPU fallback · partition (F5·F8) | 그래프가 여러 조각으로 잘림, CPU 쪽 op가 top, NPU↔CPU 전환 이벤트, float 버퍼 | EP/컴파일러 배치 로그(5절 CoreML 판정), 배치 결과 diff | 같은 수학의 다른 표현(7.4절 GAP → ReduceMean) → op 교체 후 재학습(5절) → 벤더에 커널 요청 |
| 양자화 hot spot (C8) | latency는 OK인데 int8 정확도·SQNR이 float보다 많이 낮음 | 레이어별 SQNR로 "처음 무너지는 층" 찾기 | per-channel quant → 문제 층만 int16 → op 교체(LN → BN, 5절) → QAT |
| pre/post 지배 (I4) | 모델 시간은 작은데 end-to-end가 길다: 특징 추출, resampling, NMS, softmax 후처리 | 모델 앞뒤에 타임스탬프 (13절의 층별 측정을 파이프라인 단계로 확장) | 특징 추출을 DSP로 → 입력률 낮추기(7절 v4) → 후처리를 정수로 → 후처리 생략 가능 여부 재검토 |
| scheduling · IPC (E8) | 각 단계는 빠른데 전체가 늦다, 단계 사이 빈 시간, 깨우기·mailbox 대기 | 코어 간 시계를 맞춘 타임라인(E8 7절) | batching(창 여러 개를 한 번에) → 깨우기 횟수 줄이기 → 공유 메모리 링으로 복사 제거 → 우선순위 조정 |
| overhead-bound | 작은 층이 많고 층마다 비슷한 고정 시간, 총 MAC 대비 시간이 이상하게 큼 | 층 수 × 층당 고정비 계산, 빈 층 실험 | 층 합치기(fusion) → 블록 수 줄이기 → 런타임 호출 묶기 |

### 9.2 compute-bound인지 memory-bound인지 프로파일 한 줄에서 판단하기

층 하나에 대해 세 숫자만 있으면 된다: **시간, MAC 수, 움직인 바이트**. 손계산 예를 하나 해 보자. 가정한 NPU는 피크 `32 MAC/cycle × 50 MHz = 1.6 GMAC/s`, SRAM 대역폭 `16 B/cycle × 50 MHz = 800 MB/s`이므로 ridge point(D3)는 `1.6 G ÷ 0.8 G = 2 MAC/B`다.

```
v4의 pointwise 128→160 (L=50):  MAC = 128 × 160 × 50 = 1,024,000
  바이트(int8) = 입력 128×50 + 출력 160×50 + weight 128×160 = 6,400 + 8,000 + 20,480 = 34,880 B
  arithmetic intensity = 1,024,000 / 34,880 ≈ 29 MAC/B  >  2  → compute-bound
  예상 시간 ≈ 1,024,000 / 1.6 G = 0.64 ms  (lane 정렬이 맞아 패딩 낭비 없음)

FC 160→8:  MAC = 1,280,  바이트 = 160 + 8 + 1,280 = 1,448  → 0.9 MAC/B < 2 → memory-bound
```

말로 하면: **MAC/바이트가 ridge point보다 크면 MAC을 줄여야 빨라지고, 작으면 바이트를 줄여야 빨라진다.** budget_report의 `bound` 열이 정확히 이 비교다. 그리고 프로파일에서 실제 시간이 두 지붕 어느 쪽에도 못 미치면(6.2절의 PyTorch depthwise처럼) 그것은 "커널 품질" 문제이고, 모델이 아니라 런타임·컴파일러 쪽을 봐야 한다.

### 9.3 흔한 오분류

- **Mac(또는 개발용 GPU) 프로파일로 타깃 병목을 정한다** → 3절에서 봤듯 fallback은 개발 머신에서 안 보인다. proxy는 "무엇이 들어 있나"를 보는 데 쓰고, "무엇이 비싼가"는 타깃 숫자나 타깃 모델로 판단한다.
- **FLOPs top = 시간 top이라고 가정한다** → C7 2절. depthwise는 MAC이 적어도 느리다.
- **평균만 본다** → always-on 모델은 p99가 중요하다(D6). 열·인터럽트로 가끔 2배 걸리면 실시간 예산이 깨진다.
- **첫 실행을 포함한다** → 컴파일·캐시 warm-up(F8 6절)을 steady state와 분리한다.

---

## 10. 모델 vs 컴파일러/런타임 vs HW 설정 — 어떻게 고르나

병목을 분류했으면 "어디를 고칠지" 골라야 한다. 같은 병목이라도 고치는 곳이 세 군데다. 예를 들어 v3의 "latency 17% 초과"는 이렇게 고칠 수 있었다.

| 경로 | 구체적 수정 | 예상 효과 (가정한 비용 모델) | 걸리는 시간 | 위험 |
|---|---|---|---|---|
| 모델 | 폭 축소 + 재학습 (v4) | 5.83 → 2.55 ms | 일~주 (재학습·평가) | 정확도 하락 (seed 범위로 확인) |
| 컴파일러 | 첫 층(입력 6채널)을 lane에 맞게 특수 처리: 시간 축 일부를 채널로 접기(space-to-depth) | 첫 층 1.19 ms 중 lane 낭비분(6 → 16 패딩, 2.67배) 회수 | 주~월 (벤더 지원 여부에 달림) | bit-exact 확인 필요 |
| HW 설정 | NPU 클럭 50 → 100 MHz | 모든 NPU 층 절반 → 약 2.9 ms | 일 (설정·검증), 하지만 전력 예산 재협상 | 정확도 위험 0, always-on 전력 증가 |

### 10.1 판단 기준

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 400">
<text x="20" y="22" font-size="14">수정 경로별 걸리는 시간 (전형적 범위, 경험칙 — 회사·벤더마다 다르다)</text> <text x="222" y="63" font-size="12" text-anchor="end">컴파일러 옵션 (fusion·tiling 플래그)</text> <rect x="230.0" y="52" width="114.1" height="14" rx="3" fill="#3f9a6b" opacity="0.85"/> <text x="350.1" y="63" font-size="12">없음 (bit-exact 확인)</text> <text x="222" y="99" font-size="12" text-anchor="end">그래프 수술 (op 분해·치환)</text>
<rect x="274.2" y="88" width="102.5" height="14" rx="3" fill="#e08a3c" opacity="0.85"/> <text x="382.7" y="99" font-size="12">수치 동등성 확인</text> <text x="222" y="135" font-size="12" text-anchor="end">모델: op 교체 + 재학습</text> <rect x="318.3" y="124" width="79.8" height="14" rx="3" fill="#4a7bd0" opacity="0.85"/> <text x="404.1" y="135" font-size="12">작음</text>
<text x="222" y="171" font-size="12" text-anchor="end">모델: 폭·해상도 축소 + 재학습</text> <rect x="344.1" y="160" width="98.1" height="14" rx="3" fill="#4a7bd0" opacity="0.85"/> <text x="448.3" y="171" font-size="12">중간 (seed 여러 개)</text> <text x="222" y="207" font-size="12" text-anchor="end">HW 설정: NPU 클럭 50→100 MHz</text> <rect x="274.2" y="196" width="102.5" height="14" rx="3" fill="#d0564a" opacity="0.85"/>
<text x="382.7" y="207" font-size="12">없음 · 전력 대가</text> <text x="222" y="243" font-size="12" text-anchor="end">HW 설정: SRAM 뱅크·메모리 맵</text> <rect x="344.1" y="232" width="120.9" height="14" rx="3" fill="#d0564a" opacity="0.85"/> <text x="471.0" y="243" font-size="12">없음</text> <text x="222" y="279" font-size="12" text-anchor="end">벤더: 새 커널·버그 수정</text>
<rect x="490.8" y="268" width="88.3" height="14" rx="3" fill="#3f9a6b" opacity="0.85"/> <text x="484.8" y="279" font-size="12" text-anchor="end">작음 · 일정 위험 큼</text> <text x="222" y="315" font-size="12" text-anchor="end">HW: 다음 실리콘에 반영</text> <rect x="605.0" y="304" width="45.0" height="14" rx="3" fill="#d0564a" opacity="0.85"/> <text x="599.0" y="315" font-size="12" text-anchor="end">— (지금 제품엔 못 씀)</text>
<line x1="230" y1="346" x2="650" y2="346" stroke="currentColor"/> <line x1="230.0" y1="346" x2="230.0" y2="351" stroke="currentColor"/><text x="230.0" y="365" font-size="12" text-anchor="middle">반나절</text> <line x1="230.0" y1="44" x2="230.0" y2="346" stroke="#888" stroke-dasharray="2 4"/>
<line x1="274.2" y1="346" x2="274.2" y2="351" stroke="currentColor"/><text x="274.2" y="365" font-size="12" text-anchor="middle">1일</text> <line x1="274.2" y1="44" x2="274.2" y2="346" stroke="#888" stroke-dasharray="2 4"/> <line x1="398.1" y1="346" x2="398.1" y2="351" stroke="currentColor"/><text x="398.1" y="365" font-size="12" text-anchor="middle">1주</text>
<line x1="398.1" y1="44" x2="398.1" y2="346" stroke="#888" stroke-dasharray="2 4"/> <line x1="490.8" y1="346" x2="490.8" y2="351" stroke="currentColor"/><text x="490.8" y="365" font-size="12" text-anchor="middle">1달</text> <line x1="490.8" y1="44" x2="490.8" y2="346" stroke="#888" stroke-dasharray="2 4"/>
<line x1="560.8" y1="346" x2="560.8" y2="351" stroke="currentColor"/><text x="560.8" y="365" font-size="12" text-anchor="middle">분기</text> <line x1="560.8" y1="44" x2="560.8" y2="346" stroke="#888" stroke-dasharray="2 4"/> <line x1="650.0" y1="346" x2="650.0" y2="351" stroke="currentColor"/><text x="650.0" y="365" font-size="12" text-anchor="middle">1년</text>
<line x1="650.0" y1="44" x2="650.0" y2="346" stroke="#888" stroke-dasharray="2 4"/> <text x="20" y="388" font-size="12">막대 색 = 주 책임자 (파랑 모델팀 · 초록 컴파일러/벤더 · 주황 embedded AI · 빨강 HW팀), 오른쪽 글 = 정확도(A/B) 위험</text>
</svg>
```

그림 6 — 수정 경로별 전형적인 소요 시간(경험칙, 회사·벤더마다 다르다). 막대가 오른쪽일수록 제품 일정에 넣기 어렵다.

1. **일정**: 제품 일정 안에 들어오는가? 벤더 커널 수정은 보통 다음 SDK 릴리스를 기다려야 하므로 수개월 단위다. 모델 수정은 며칠~몇 주. HW 설정(클럭, 메모리 맵)은 빠르지만 다른 팀의 예산을 쓴다.
2. **누구 예산을 쓰나**: 클럭을 올리면 latency는 모델팀 문제에서 전력팀 문제로 바뀔 뿐이다. always-on 영역에서 클럭 2배는 동적 전력이 대략 2배(전압까지 올리면 더)다 — I1의 전력 예산과 같이 봐야 한다.
3. **정확도 A/B 위험**: 모델 수정은 재학습을 동반하고, 재학습은 seed에 따라 결과가 흔들린다(7절: 같은 v4가 97.3~99.05%). 그래서 "정확도 차이 1%p"가 진짜 차이인지 판단하려면 seed 여러 개 또는 paired bootstrap(C8 6절)이 필요하다. 컴파일러 수정은 bit-exact이거나 tolerance 안이어야 하므로 정확도 위험이 거의 없지만, **검증 비용**(golden vector, C8)이 든다.
4. **재사용성**: 컴파일러 수정은 앞으로의 모든 모델에 효과가 있다. 모델 수정은 이 모델에만 효과가 있다. 같은 병목이 모델 세 개에서 반복되면 컴파일러 쪽 투자가 맞다.
5. **되돌리기 쉬움**: HW 설정과 컴파일러 플래그는 되돌리기 쉽다. 실리콘 변경은 되돌릴 수 없다.

### 10.2 실무 패턴 — 두 경로를 동시에

자주 쓰는 패턴은 **"단기 우회는 모델로, 장기 수정은 컴파일러로"**다. 예를 들어 벤더 컴파일러가 GELU를 지원하지 않으면, 이번 제품은 ReLU6로 바꾼 모델(v2)로 가고, 동시에 벤더에 GELU 커널을 요청한다(F8 7절 최소 재현 리포트). 다음 모델 세대에 커널이 들어오면 그때 GELU를 다시 검토한다. 결정 기록에는 "왜 우회했고, 언제 다시 볼지"를 남긴다.

반대 방향도 있다. 모델팀이 "이 op가 정확도에 꼭 필요하다"고 A/B 숫자로 보여 주면(예: 같은 예산에서 2%p 차이, seed 5개 모두에서), 그때는 컴파일러/HW 쪽 투자가 정당화된다. **숫자로 논쟁한다** — NPI에서 HW·펌웨어 이슈를 숫자로 논의하는 방식과 같다.

---

## 11. 루프 자동화 — 모든 후보에 같은 예산표를

### 11.1 무엇을 자동화하나

| 자동화 대상 | 언제 | 산출물 | 이 노트의 대응 |
|---|---|---|---|
| 예산표 (latency 추정, peak, weight, fallback) | 모델 후보가 생길 때마다 (학습 job 끝) | 표 + exit code | `budget_report.py` |
| op 지원 · 배치 | 같은 때 + SDK 버전이 바뀔 때 | 미지원 op 목록, partition 수 | CoreML EP 판정(5절), I2 linter |
| int8 변환 · SQNR · 정확도 | 같은 때 | metrics.json | TFLite 스크립트(5절) |
| 타깃 실측 | 야간 (보드 farm) | 층별 시간, p50/p99, 전력 | I5 harness, 13절 |
| 추세 | 매 실행 | 버전별 그래프 (그림 2) | 8절 |
| 회귀 경보 | 기준선 대비 악화 시 | 이슈 자동 생성 | 12절 |

자동화하지 **않는** 것도 정해 둔다: 수정 경로 선택(10절)은 사람의 판단이다. 자동화는 "판단에 필요한 숫자가 늘 최신이고 같은 방법으로 쟀다"를 보장하는 데까지다.

### 11.2 CI 게이트로 돌리기 (예제 11)

무엇을 확인하는 코드인지: budget_report를 후보 전부에 돌리고 exit code를 확인한다. CI에서는 이 exit code로 merge를 막는다.

```sh
python budget_report.py v4.onnx; echo "exit=$?"
python budget_report.py v3.onnx v4.onnx > /dev/null; echo "exit(v3,v4)=$?"
```

```text
== v4.onnx  MACs=2.10 M  Mac ORT CPU median=0.11 ms (proxy, 50 runs)
  latency_ms        2.55  budget <= 5.0     PASS
  peak_act_KB      14.40  budget <= 200     PASS
  weights_KB       36.37  budget <= 300     PASS
  cpu_ops           0.00  budget <= 0       PASS
  acc_int8        0.9905  budget >= 0.95    PASS
  sqnr_dB          37.45  budget >= 25      PASS
  by class: compute 2.46 ms  memory 0.10 ms
  top-3: #12 Conv compute 0.68 ms | #0 Conv compute 0.62 ms | #10 DWConv compute 0.33 ms
exit=0
exit(v3,v4)=1
```

출력에서 볼 것: v4 하나면 exit 0(merge 가능), v3이 섞이면 exit 1. CI 설정은 대략 이런 모양이다(GitHub Actions 형식 예시, 실행하지 않은 설정 스케치).

```text
# .github/workflows/model-budget.yml (스케치)
on: [pull_request]
jobs:
  budget:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: pip install -r requirements.lock        # 버전 고정 (F8 2절)
      - run: python export.py --model ${{ github.head_ref }}   # 학습 산출물 → ONNX
      - run: python eval_int8.py                     # metrics.json (정확도·SQNR)
      - run: python budget_report.py candidate.onnx | tee budget.txt
      - uses: actions/upload-artifact@v4
        with: { name: budget, path: budget.txt }
```

### 11.3 자동화할 때 지킬 것

- **예산 상수는 한 파일에만** 둔다(`BUDGET`, `MARGIN`). 예산표 문서와 코드가 따로 놀면 둘 중 하나는 틀린다. 12절의 single source of truth.
- **비용 모델 상수는 타깃 측정으로 보정하고 버전을 붙인다.** `MPC`, `DW_EFF`, `CPU_CYC` 같은 숫자는 보드에서 잰 층별 시간으로 맞추고(C7 3절 LUT 보정), SDK 버전과 함께 기록한다.
- **proxy 숫자는 정보로만**: budget_report가 Mac ORT 시간을 찍지만 PASS/FAIL에는 쓰지 않는다. 3절에서 본 것처럼 proxy의 비율이 타깃과 다르기 때문이다.
- **MARGIN 상태를 따로 둔다**: 예산의 90~100%는 FAIL이 아니라 경고(MARGIN)다. 양산 직전에 margin이 없는 항목은 온도·버전 변화에 깨진다.

---

## 12. 팀 프로세스 — 주간 리뷰, 예산의 단일 출처, 모델-HW 계약

### 12.1 이슈 하나의 수명

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<defs><marker id="i6a8" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="20" y="22" font-size="14">co-design 이슈 한 건의 수명 — NPI 이슈 트래커와 같은 상태 기계</text> <rect x="10" y="50" width="110" height="56" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="65" y="73" font-size="12" text-anchor="middle">OPEN</text><text x="65" y="91" font-size="12" text-anchor="middle">CI 예산 FAIL</text> <rect x="142" y="50" width="110" height="56" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="197" y="73" font-size="12" text-anchor="middle">TRIAGE</text><text x="197" y="91" font-size="12" text-anchor="middle">분류 · 증거 첨부</text>
<rect x="274" y="50" width="110" height="56" rx="6" fill="none" stroke="currentColor" stroke-width="2"/> <text x="329" y="73" font-size="12" text-anchor="middle">주간 리뷰</text><text x="329" y="91" font-size="12" text-anchor="middle">경로·owner 결정</text> <rect x="406" y="50" width="110" height="56" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<text x="461" y="73" font-size="12" text-anchor="middle">FIX</text><text x="461" y="91" font-size="12" text-anchor="middle">모델·컴파일러·HW</text> <rect x="538" y="50" width="130" height="56" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="603" y="73" font-size="12" text-anchor="middle">VERIFY → CLOSE</text><text x="603" y="91" font-size="12" text-anchor="middle">같은 harness 재측정</text>
<line x1="120" y1="78" x2="140" y2="78" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a8)"/> <line x1="252" y1="78" x2="272" y2="78" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a8)"/> <line x1="384" y1="78" x2="404" y2="78" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a8)"/>
<line x1="516" y1="78" x2="536" y2="78" stroke="currentColor" stroke-width="1.5" marker-end="url(#i6a8)"/> <path d="M 603 106 L 603 140 L 461 140 L 461 108" fill="none" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="5 3" marker-end="url(#i6a8)"/> <text x="532" y="156" font-size="12" text-anchor="middle">재측정 FAIL → FIX로 되돌림</text>
<rect x="274" y="180" width="242" height="50" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="395" y="201" font-size="12" text-anchor="middle">ESCALATE: 2주 진척 없음 · 일정 영향</text> <text x="395" y="219" font-size="12" text-anchor="middle">→ 프로그램 리드 · 벤더 FAE · 예산 재협상</text> <path d="M 329 106 L 329 178" fill="none" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="5 3" marker-end="url(#i6a8)"/>
<text x="20" y="256" font-size="12">각 상태의 필수 필드: 모델 해시 · SDK/컴파일러 버전 · 측정 조건(클럭·스레드·입력)</text><text x="20" y="271" font-size="12">· before/after 숫자 · owner · 기한</text> <text x="20" y="290" font-size="12">예산표(single source of truth)는 한 곳에만 있고, 이슈는 그 행 ID를 참조한다</text>
</svg>
```

그림 8 — co-design 이슈의 상태 기계. NPI 이슈 트래커의 흐름과 같다: 증거 없이 TRIAGE를 통과하지 않고, 같은 harness의 재측정 없이 CLOSE되지 않는다.

### 12.2 주간 co-design 리뷰 (30~45분)

| 순서 | 내용 | 누가 | 산출물 |
|---|---|---|---|
| 1 | 예산표 추세: 지난주 대비 바뀐 숫자 (그림 2 같은 그래프) | embedded AI | 추세 1장 |
| 2 | 새 FAIL·MARGIN 항목: 분류와 증거(프로파일 캡처, before/after) | embedded AI | 이슈 목록 |
| 3 | 수정 경로 결정: 모델/컴파일러/HW, owner, 기한 | 전원 | 결정 기록 |
| 4 | 진행 중 이슈: 막힌 것만 | owner | 에스컬레이션 후보 |
| 5 | 예산 변경 요청 (있다면): 누가, 왜, 어느 예산에서 가져오나 | 요청자 | 예산표 개정안 |

참석자는 모델팀, embedded AI(진행), 컴파일러/벤더 담당(필요할 때 FAE), HW팀 대표. 회의의 원칙은 **숫자 없는 주장은 다음 주로** 넘긴다는 것이다.

### 12.3 예산의 단일 출처 (single source of truth)

예산은 한 곳(저장소의 `budgets.yaml` 같은 파일)에만 있고, 문서·대시보드·CI가 모두 그것을 읽는다. 바꾸려면 리뷰를 거친 변경(PR)으로만 바꾼다. 이유: "모델팀은 250 KB로 알고 있었고 HW팀은 200 KB로 알고 있었다"가 co-design 사고의 가장 흔한 원인이다.

### 12.4 모델-HW 계약 — 모델 카드의 HW 섹션

모델을 freeze할 때 모델 카드(model card)에 HW 섹션을 붙인다. 예시(v4 기준, 이 노트의 숫자):

```text
model: gesture_v4   hash: <sha256>   onnx opset: 17   int8 tflite: 64.7 KiB
input: imu 6 x 400 int8, 200 Hz, 2 s window, scale/zero-point = <from tflite>
target: aon-npu (assumed 32 MAC/cycle @ 50 MHz), compiler/SDK: <version>
latency (est): 2.55 ms   measured on target: <p50 / p99, board id, clock>
peak activation: 14.4 KB   weights: 36.4 KB (+ int32 bias, file 64.7 KiB)
cpu fallback ops: 0 (assumed allowlist)   CoreML EP: GlobalAveragePool -> ReduceMean 적용 시 0
accuracy int8: 0.9905 (seed 0), float seeds 0.973-0.9905   SQNR: 37.4 dB
re-validate when: SDK version change, clock change, input rate change, sensor change
```

이 섹션이 "계약"인 이유: HW팀은 이 숫자를 보고 SRAM 배치와 클럭을 정하고, 모델팀은 다음 버전이 이 숫자를 넘으면 안 된다는 것을 안다. 마지막 줄(언제 다시 검증하나)이 빠지면 SDK 업데이트 한 번에 계약이 조용히 깨진다(F8 8절).

### 12.5 에스컬레이션

- **2주 진척 없음** 또는 **일정 영향**이면 프로그램 리드에게 올린다.
- 벤더 이슈는 최소 재현(F8 7절)과 함께 FAE에게, 우선순위는 "제품 출시를 막는가"로 표시한다.
- 예산 자체가 불가능하다는 증거(모든 경로를 다 썼는데도 초과)가 모이면 **예산 재협상**을 연다. 이것은 실패가 아니라 루프의 정상 출력이다 — I1의 예산은 처음부터 가정이었다.

### 12.6 Don의 NPI식 이슈 추적을 그대로 쓰기

| NPI에서 하던 것 | co-design 루프에서 |
|---|---|
| 이슈마다 재현 조건·빌드·보드 번호 | 모델 해시·SDK 버전·측정 조건 |
| failure rate을 숫자로 (n/N) | 정확도를 seed 범위·신뢰구간으로 |
| root cause 확정 전엔 "suspect"로 표시 | 분류(9절)가 증거로 확정되기 전엔 "가설" |
| fix 검증은 원래 실패 조건에서 | 재측정은 같은 harness·같은 입력으로 |
| margin sign-off 표 | 예산표 + MARGIN 상태 |

---

## 13. 임베디드 관점에서 다시 보기 — 타깃에서 층별로 재는 법

보드가 있으면 루프의 ② 측정은 타깃 실측이 1순위다. 펌웨어에서 층별 시간을 재는 가장 단순한 방법은 Don이 SSD 펌웨어에서 하던 그대로다: **층 실행을 cycle 카운터로 감싸고, N회 반복해 median을 낸다.** Cortex-M이라면 DWT `CYCCNT` 레지스터를 읽는다(K2). 아래 C 예제는 그 뼈대를 Mac에서 돌린 것이다 — 시계만 다르다.

### 13.1 층별 cycle 측정 뼈대 (예제 12, C)

무엇을 확인하는 코드인지: int8 pointwise conv와 depthwise conv를 층처럼 감싸 21회씩 재고 median·min·GMAC/s를 낸다. 같은 pointwise 계산을 **루프 순서만 바꾼 커널**(`pw_t`)도 재서, 모델을 안 바꾸고 커널만 바꿨을 때의 효과(= 컴파일러/런타임 경로)를 본다.

```c
/* layer_prof.c — 펌웨어식 층별 프로파일: 층마다 cycle 카운터로 감싸고 N회 중 median을 낸다.
   타깃에서는 NOW()를 DWT->CYCCNT 읽기로 바꾼다. 여기서는 Mac의 ns 시계로 대신한다. */
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static uint64_t NOW(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }   /* macOS ns 시계 */
#define L 100
static int8_t x[160 * L], y[160 * L], w[160 * 160];
static void pw(int ci, int co) {             /* pointwise 1x1: y[o][t] = sum_i w[o][i] x[i][t] */
    for (int o = 0; o < co; o++) for (int t = 0; t < L; t++) {
        int32_t acc = 0; for (int i = 0; i < ci; i++) acc += w[o * ci + i] * x[i * L + t];
        y[o * L + t] = (int8_t)(acc >> 10); } }
static void pw_t(int ci, int co) {           /* 같은 계산, 루프 순서만 o-i-t: 안쪽 루프가 연속 주소 */
    static int32_t acc[L];
    for (int o = 0; o < co; o++) {
        for (int t = 0; t < L; t++) acc[t] = 0;
        for (int i = 0; i < ci; i++) { int32_t wv = w[o * ci + i];
            for (int t = 0; t < L; t++) acc[t] += wv * x[i * L + t]; }
        for (int t = 0; t < L; t++) y[o * L + t] = (int8_t)(acc[t] >> 10); } }
static void dw(int c, int k) {               /* depthwise k: 채널마다 독립 1D 필터 */
    for (int ch = 0; ch < c; ch++) for (int t = 0; t < L - k; t++) {
        int32_t acc = 0; for (int j = 0; j < k; j++) acc += w[ch * k + j] * x[ch * L + t + j];
        y[ch * L + t] = (int8_t)(acc >> 7); } }
static int cmp(const void *a, const void *b) { uint64_t p = *(const uint64_t *)a, q = *(const uint64_t *)b; return (p > q) - (p < q); }
int main(void) {
    for (int i = 0; i < 160 * L; i++) x[i] = (int8_t)(i * 7);
    for (int i = 0; i < 160 * 160; i++) w[i] = (int8_t)(i * 13);
    const char *name[] = {"pw128_160", "pw128_160_t", "pw130_162_t", "dw128_k9"};
    uint64_t macs[] = {128ull * 160 * L, 128ull * 160 * L, 130ull * 162 * L, 128ull * 9 * (L - 9)};
    for (int l = 0; l < 4; l++) {
        uint64_t s[21];
        for (int r = 0; r < 21; r++) {
            uint64_t t0 = NOW();
            if (l == 0) pw(128, 160); else if (l == 1) pw_t(128, 160); else if (l == 2) pw_t(130, 162); else dw(128, 9);
            s[r] = NOW() - t0;
        }
        qsort(s, 21, sizeof s[0], cmp);
        long chk = 0; for (int i = 0; i < 160 * L; i++) chk += y[i];
        printf("%-12s MACs=%7llu  median=%8.2f us  min=%8.2f us  %.2f GMAC/s  (chk %ld)\n", name[l],
               (unsigned long long)macs[l], s[10] / 1e3, s[0] / 1e3, (double)macs[l] / s[10], chk);
    }
    return 0;
}
```

```text
pw128_160    MACs=2048000  median=  729.42 us  min=  728.79 us  2.81 GMAC/s  (chk -29280)
pw128_160_t  MACs=2048000  median=   70.17 us  min=   70.08 us  29.19 GMAC/s  (chk -29280)
pw130_162_t  MACs=2106000  median=   72.04 us  min=   71.96 us  29.23 GMAC/s  (chk -8487)
dw128_k9     MACs= 104832  median=    2.42 us  min=    2.42 us  43.37 GMAC/s  (chk -17873)
--- 두 번째 실행
pw128_160    MACs=2048000  median=  728.83 us  min=  728.42 us  2.81 GMAC/s  (chk -29280)
pw128_160_t  MACs=2048000  median=   70.08 us  min=   70.00 us  29.22 GMAC/s  (chk -29280)
pw130_162_t  MACs=2106000  median=   72.12 us  min=   72.00 us  29.20 GMAC/s  (chk -8487)
dw128_k9     MACs= 104832  median=    2.54 us  min=    2.42 us  41.24 GMAC/s  (chk -17873)
```

출력에서 볼 것:

- `pw128_160`과 `pw128_160_t`는 **checksum이 같다**(같은 계산). 그런데 안쪽 루프를 연속 주소로 바꾼 것만으로 약 10배(729 → 70 µs) 빨라졌다. 모델은 그대로다 — 이것이 10절의 "컴파일러/런타임 경로"의 축소판이다. 커널 품질이 나쁜 상태에서 모델을 줄이는 것은 순서가 틀렸다.
- 두 번 실행한 결과가 1% 안에서 같다 — 다른 프로세스가 조용할 때 잰 것이다. 같은 바이너리를 다른 학습 job들이 CPU를 쓰던 때 쟀을 때는 naive 커널의 median이 940 µs와 1170 µs로 25% 차이 났다(그때의 기록). **한 번 잰 숫자를 믿지 말고, 측정 조건을 같이 기록할 것.**
- depthwise(`dw128_k9`)는 이 C 구현에서 40 GMAC/s 이상이다. 6.2절의 PyTorch eager depthwise(0.2~0.4 GMAC/s)와 같은 계산인데 100배 넘게 차이 난다 — 커널 품질은 모델이 아니라 구현의 성질이다.
- `pw130_162_t`는 `pw128_160_t`보다 MAC이 3% 많고 시간도 비슷하게 3% 늘었다. 이 CPU에서는 채널 정렬 효과가 거의 없다 — 16 lane NPU에서는 130 → 144로 패딩되어 13% 낭비가 생긴다(6절). **정렬 효과는 타깃 의존**이라 타깃에서 재야 한다.

### 13.2 타깃 측정에서 추가로 챙길 것

- **측정 오버헤드**: CYCCNT 읽기 자체가 몇 cycle이다. 빈 구간을 재서 빼 준다.
- **캐시·TCM 상태**: 첫 실행(cold)과 반복 실행(warm)을 분리해서 기록한다. always-on 모델은 매 추론이 사실상 cold일 수 있다(그 사이 다른 작업이 캐시를 덮어씀).
- **NPU 비동기 실행**: NPU에 명령을 넣고 인터럽트로 끝을 받는 구조면, CPU 쪽 시간은 "명령 넣기 + 대기"다. 벤더 프로파일러의 NPU 내부 타임라인과 CPU 쪽 타임스탬프를 같은 시계로 맞춘다(E8 7절).
- **전력**: latency와 함께 추론당 에너지를 잰다(K3, D7). 클럭을 올리는 HW 설정 경로(10절)는 이 숫자로만 판단할 수 있다.
- **층 이름 매핑**: 컴파일러가 층을 fusion하면 프로파일의 "층"과 모델의 층이 1:1이 아니다. 컴파일러가 내는 매핑 표(fused op → 원래 노드 목록)를 같이 저장한다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 한 바퀴에 여러 수정 | 개선은 됐는데 어느 수정 덕인지 모름, 정확도 하락의 원인 불명 | 변수 여러 개를 동시에 바꿈 | 한 바퀴 한 종류, 필요하면 2×2 A/B (4절) |
| 정확도를 마지막에만 잼 | latency를 맞춘 뒤에 정확도가 예산 아래로 떨어진 것을 발견, 몇 바퀴를 되돌림 | 정확도를 "모델팀 일"로 미룸 | 매 바퀴 예산표에 정확도 포함 (4절) |
| proxy 프로파일로 타깃 병목 결정 | 개발 머신에서 conv 최적화했는데 타깃은 그대로 | fallback·메모리 계층이 proxy에 없음 | 타깃 실측 또는 타깃 비용 모델로 판단 (3절) |
| seed 하나로 후보 비교 | 다음 학습에서 순위가 뒤집힘 | 재학습 분산 | seed 3개 이상, 최솟값으로 판정 (7절) |
| 지원표(allowlist)만 믿음 | 실제 백엔드에서 fallback 발견 | 지원표는 op 종류만, 실제 판정은 shape·속성에 따라 다름 | 실제 컴파일러/EP에 물어보기 (7.4절 GAP) |
| margin 없이 sign-off | SDK 업데이트·온도 변화로 예산 초과 | 100% 근처에서 freeze | MARGIN 상태를 두고 90% 이하만 PASS |
| 예산표가 여러 곳 | 팀마다 다른 숫자를 앎 | 단일 출처 없음 | `budgets.yaml` 하나, CI가 읽음 (12.3) |
| 커널 품질 문제를 모델로 해결 | 모델을 줄여 정확도를 잃었는데 타깃 런타임이 바뀌자 필요 없었음 | "느린 커널"을 compute-bound로 오분류 | 달성 MAC/s가 두 지붕 모두에 못 미치면 커널부터 (6.2절, 13.1절) |
| 첫 실행 포함 | 평균 latency가 이상하게 큼, 분산 큼 | 컴파일·캐시 warm-up | warm-up 분리, median·p99 보고 |

---

## 15. 면접에서 이렇게 말한다

**Q.** Walk me through how you'd get a model that's 3× over its latency budget into budget.

**A.** 먼저 숫자를 고정한다: 예산과 margin, 측정 조건. 그다음 타깃에서 층별 프로파일을 떠서 시간 비중이 큰 구간을 분류한다 — 모델 밖 시간, CPU fallback, compute-bound, memory-bound 순서로. fallback이 있으면 그것부터(전환 비용과 float 버퍼까지 없어진다), 다음은 큰 feature map을 일찍 줄이는 것, 그다음 DS-conv·채널 정렬, 마지막이 폭·해상도 축소와 재학습이다. 한 바퀴에 하나씩 바꾸고, 매 바퀴 정확도까지 포함한 예산표 전체를 다시 잰다. 제 연습 사례에서는 예산의 100배인 모델이 다섯 바퀴 만에 예산의 51%가 됐고, 처음부터 예산 밖이던 정확도의 원인을 2×2 A/B로 분리했다(downsample이 아니라 LayerNorm·GELU였다).

> "First I pin down the numbers: the budget, the margin I want, and the measurement conditions. Then I profile on the target per layer and classify where the time goes, in order: time outside the model, CPU fallback, compute-bound layers, memory-bound layers. I fix the biggest class first, one kind of change per iteration — remove fallback ops, downsample early so the big feature maps disappear, switch to depthwise-separable blocks with channels aligned to the NPU lanes, and only then trim width or resolution with retraining. After every iteration I re-run the full budget report, including int8 accuracy, because a latency fix can quietly break accuracy. In a practice case I took a model that was about a hundred times over budget down to about half the budget in five iterations."

**Q.** How do you tell if a layer is compute-bound or memory-bound from a profile?

**A.** 층마다 세 숫자를 본다: 시간, MAC 수, 움직인 바이트. MAC/바이트(arithmetic intensity)가 HW의 ridge point(피크 MAC/s ÷ 대역폭)보다 크면 compute 쪽, 작으면 memory 쪽이다. 확인은 달성 MAC/s를 피크와, 달성 바이트/s를 대역폭과 비교한다. 둘 다 한참 못 미치면 compute도 memory도 아니라 커널 품질이나 고정비 문제다 — 실제로 PyTorch CPU의 depthwise conv1d가 0.2 GMAC/s 안팎으로 pointwise의 수백분의 일이었는데, 같은 모델을 ONNX Runtime으로 돌리면 문제가 사라졌다.

> "I look at three numbers per layer: time, MACs, and bytes moved. MACs per byte compared with the hardware's ridge point — peak MACs per second divided by memory bandwidth — tells me which roof applies. Then I check achieved throughput against that roof. If a layer is far below both roofs, it's neither compute- nor memory-bound; it's a kernel quality or fixed-overhead problem. I've seen depthwise convs in PyTorch eager on a CPU run at a fraction of a percent of the pointwise layers' throughput, and the same model in ONNX Runtime didn't have the problem — that's a runtime fix, not a model change."

**Q.** Model fix versus compiler fix — how do you decide?

**A.** 다섯 가지로 본다: 제품 일정 안에 들어오는가(벤더 커널은 보통 수개월, 모델 수정은 며칠~몇 주), 누구의 예산을 쓰는가(클럭을 올리면 latency가 전력 문제로 바뀐다), 정확도 A/B 위험(재학습은 seed 분산이 있으니 여러 seed로 판단), 재사용성(같은 병목이 여러 모델에서 반복되면 컴파일러 투자), 되돌리기 쉬움. 실무에서는 단기 우회는 모델로 하고, 동시에 컴파일러 수정을 요청해 다음 세대에 반영하는 경우가 많다.

> "I weigh schedule, whose budget it spends, accuracy risk, reuse, and reversibility. A vendor kernel fix usually means waiting for the next SDK release — months — while a model change with retraining is days to weeks. Raising the clock moves the problem into the power budget. Model changes carry accuracy risk, so I compare them across several seeds, not one run. Compiler fixes help every future model, so if the same bottleneck shows up in several models, that's where I'd invest. In practice I often do both: ship a model-side workaround now, and file a minimal repro with the vendor so the next generation doesn't need it."

**Q.** What do you automate in the co-design loop?

**A.** 모델 후보가 생길 때마다 같은 스크립트가 예산표를 낸다: MAC, weight 바이트, liveness 기반 peak activation, 미지원 op와 partition, 타깃 latency 추정, int8 변환 후 SQNR과 정확도. 예산 상수는 한 파일에만 두고 CI가 exit code로 merge를 막는다. 보드 farm이 있으면 야간에 타깃 실측과 p99를 돌리고 추세 그래프를 갱신한다. 자동화하지 않는 것은 수정 경로 선택 — 그건 리뷰에서 사람이 숫자를 보고 정한다.

> "Every model candidate goes through the same budget report automatically: MACs, weight bytes, peak activation from a liveness analysis, unsupported ops and graph partitions, an estimated target latency from a calibrated cost model, and int8 accuracy and SQNR after conversion. The budget numbers live in one file that CI reads, and a failing budget blocks the merge. Nightly, a board farm runs the on-target measurements — p50, p99, energy — and updates a trend chart. What I don't automate is the decision about where to fix a bottleneck; that's a human call in the weekly review, but it's made from numbers that were all measured the same way."

**Q.** Tell me about a time you used measurements to drive a design change.

**A.** (STAR로 Don 자신의 사례를 넣는다. 아래 괄호는 Don이 채울 자리다 — 지어내지 말 것.)

- **S**ituation: [어떤 제품·칩에서, 어떤 스펙(latency, throughput, 전력, 에러율)이 margin을 못 맞췄나 — 예: SSD 펌웨어의 어떤 워크로드, 또는 RF 통합의 어떤 버스 이슈]
- **T**ask: [Don의 역할과 목표 숫자, 기한]
- **A**ction: [무엇으로 쟀나(Trace32, 로직 분석기, 내부 타임스탬프, power analyzer) → 무엇이 병목이었나 → 어떤 대안들을 비교했나(펌웨어 vs HW 설정 vs 다음 칩 요청) → 왜 그 경로를 골랐나]
- **R**esult: [before/after 숫자, margin, 그 측정 방법이 팀 프로세스나 자동화로 남았는지]
- **ML로 잇는 한 문장**: "이 방식이 edge ML의 co-design 루프와 같다 — 측정으로 병목을 분류하고, 모델·컴파일러·HW 중 고칠 곳을 비용으로 고르고, 같은 harness로 다시 잰다."

> "In [project], [metric] was [X] against a spec of [Y]. I instrumented [where] with [tool] and found that [root cause], not [the assumed cause]. We had three options — [firmware change], [configuration change], or [hardware change] — and I chose [option] because [schedule / risk / reuse]. After the change we measured [result] with [margin], and the measurement setup became [a regression test / part of sign-off]. That's the same loop I'd run for an ML model on an NPU: measure, classify the bottleneck, pick the cheapest fix that works, and re-measure everything."

**Q.** (보너스) Your model passes every latency number on the dev board but misses on the product. What do you check?

**A.** 측정 조건 차이부터: 클럭·DVFS, 온도(열 throttling), 같은 SDK 버전인지, 동시에 도는 다른 작업(오디오, BLE)과 메모리 대역폭 경합, cold 캐시. 그다음 입력 차이(실제 센서 데이터의 범위가 달라 다른 경로를 타는지). 개발 보드의 숫자는 그 자체가 proxy다.

> "I'd first diff the conditions: clocks and DVFS, thermal state, SDK version, what else is running and competing for memory bandwidth, and whether caches are cold in the product's duty cycle. Then the inputs — real sensor data can exercise different paths. A dev board number is itself a proxy until it's measured in the product configuration."

---

## 16. 직접 해보기

1. (손계산) 가정한 NPU(32 MAC/cycle, 50 MHz, 16 lane)에서 standard conv 130 → 162, k=9, 출력 길이 100의 compute cycle을 lane 패딩 포함/미포함으로 각각 구하라. 정답: 미포함 `130×162×9×100/32 = 592,312.5` cycle(≈ 11.8 ms), 포함 `144×176×9×100/32 = 712,800` cycle(≈ 14.3 ms) — budget_report의 v2 top-1과 같은 숫자다.
2. (손계산) v1의 budget_report는 "by class: fallback 46.08 ms, compute 33.42 ms, memory 1.95 ms"였다. fallback만 0으로 만들면 몇 배 빨라지나? 정답: `81.45 / (33.42 + 1.95) ≈ 2.3배`. 실제 v2는 33.53 ms(2.4배) — LN을 감싸던 Transpose(memory 열)도 함께 사라졌기 때문이다. 그다음 병목은 compute(거의 100%).
3. (코드) `budget_report.py`의 `CPU_CYC["Erf"]`를 30 → 10으로 바꾸면 v0·v1의 latency 추정과 top-3가 어떻게 바뀌나? 힌트: 비용 모델 상수 하나가 "어떤 수정을 먼저 할지"를 바꾸는지 확인한다. 상수를 타깃에서 보정해야 하는 이유.
4. (코드) v4에서 첫 층만 `("conv", 32, 9, 2)` → `("conv", 16, 9, 2)`로 바꾼 후보를 만들고 seed 3개로 학습해 예산표와 정확도 범위를 내라. 힌트: 첫 층 입력이 6채널이라 lane 낭비가 이미 크다. 출력 채널을 줄여도 compute cycle이 기대만큼 안 줄어드는 이유를 설명할 것.
5. (설계) HW팀이 "IMU를 100 Hz로 낮추면 센서 전력이 줄어든다"고 제안했다(v4r). 7절 숫자로 거절 또는 조건부 수락 메일을 3문장으로 써라. 힌트: 정확도 seed 범위, latency 이득이 없다는 점(같은 MAC), 대안(모델 재설계 후 재평가 조건).
6. (프로세스) 12.4절 모델 카드 HW 섹션을 v3 숫자로 채우고, v4와의 diff가 리뷰에서 어떻게 보일지 써라. 정답 예: latency 5.83 → 2.55, weights 79.3 → 36.4 KB, peak 28.8 → 14.4 KB, SQNR 39.1 → 37.4 dB, 입력 400 → 200 Hz.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| co-design loop | 공동 설계 피드백 루프 | 예산 → 측정 → 병목 분류 → 수정 → 재측정을 예산이 맞을 때까지 반복 |
| budget table | 예산표 | latency·메모리·weight·정확도 등의 목표와 margin을 한 표로 (I1) |
| margin | 여유 | 예산 대비 남겨 둘 비율. 여기서는 90% 이하를 PASS |
| proxy measurement | 대리 측정 | 타깃이 아닌 머신에서 잰 숫자. 순위·존재 여부는 참고, 절대값은 아님 |
| cost model | 비용 모델 | MAC·바이트·op 종류로 타깃 시간을 추정하는 식. 타깃 측정으로 보정 |
| CPU fallback | CPU 대체 실행 | 가속기가 지원하지 않는 op를 CPU에서 실행. 전환 비용과 float 버퍼가 따라온다 |
| partition | 그래프 분할 | 가속기와 CPU가 나눠 맡는 그래프 조각. 조각 수 = 전환 횟수 |
| liveness | 생존 구간 | 텐서가 만들어진 뒤 마지막으로 읽힐 때까지. peak activation 계산의 기초 (D2) |
| peak activation | 최대 활성 메모리 | 추론 중 동시에 살아 있는 activation 바이트의 최댓값 |
| lane alignment | 채널 정렬 | 채널 수를 NPU의 병렬 폭(예: 16)의 배수로 맞추는 것. 안 맞으면 패딩 낭비 |
| BN fold | BN 접기 | 추론 시 BatchNorm의 affine 변환을 앞 conv의 weight·bias에 합치는 것 |
| SQNR | 신호 대 양자화 잡음비 | `10·log10(∑ref² / ∑(ref − q)²)` dB. int8 오차의 크기 (C8) |
| ridge point | 꺾이는 점 | 피크 연산량 ÷ 대역폭. arithmetic intensity가 이보다 작으면 memory-bound (D3) |
| A/B test | 비교 실험 | 한 변수만 바꾼 두 버전을 같은 조건으로 비교. 여러 seed로 분산을 본다 |
| 2×2 factorial | 2요인 실험 | 두 변수 각각 두 값 → 4조합. 어느 변수가 원인인지 분리 |
| model card HW section | 모델 카드 HW 절 | 모델의 HW 계약: 입력 형식, 예산 숫자, 검증 조건, 재검증 조건 |
| single source of truth | 단일 출처 | 예산 숫자가 한 파일에만 있고 모두가 그것을 읽는 원칙 |
| escalation | 상부 보고 | 정해진 기간 안에 진척이 없거나 일정 영향이 있을 때 결정권자에게 올리는 것 |

---

## 18. 요약 & 체크리스트

co-design은 한 번에 맞히는 설계가 아니라 **측정 → 분류 → 한 가지 수정 → 전체 재측정**의 루프다. 이 노트에서는 예산의 약 100배인 순진한 IMU 모델을 다섯 바퀴 만에 모든 예산 안(latency 추정 예산의 51%, peak 7%, weight 12%, fallback 0, int8 정확도 99%)으로 넣었다. 바퀴마다 주인공 병목이 바뀌었다(SRAM fit → CPU fallback·양자화 → compute → 폭). 정확도는 처음부터 예산 밖이었고, 2×2 A/B로 원인(downsample이 아니라 LayerNorm·GELU)을 분리했다. Mac proxy 숫자는 존재 여부와 순위 일부만 믿을 수 있었고(fallback은 proxy에서 안 보였고, PyTorch의 depthwise는 커널 품질 문제로 엉뚱하게 느렸다), 판단은 타깃 비용 모델로 했다. 수정 경로는 모델·컴파일러·HW 설정 중 일정·예산 소유·정확도 위험·재사용성·되돌리기로 고르고, 팀은 단일 출처 예산표와 주간 리뷰, 모델 카드 HW 섹션으로 굴린다.

- [ ] "done"을 "모든 예산을 margin 안에서, 같은 harness로 재현"이라고 정의할 수 있다
- [ ] 측정 세 종류(타깃 실측·proxy·분석 추정)를 구분하고 각각 무엇을 믿을지 말할 수 있다
- [ ] 층 하나의 MAC·바이트·시간으로 compute/memory/커널 품질 문제를 가를 수 있다
- [ ] fallback이 시간뿐 아니라 float 버퍼로 메모리도 키운다는 것을 숫자로 설명할 수 있다
- [ ] 16 lane 패딩 낭비를 손으로 계산할 수 있다
- [ ] BN fold 식(`W' = W·s`, `b' = β − μ·s`)을 쓰고 검증할 수 있다
- [ ] 재학습 결과를 seed 여러 개의 범위로 비교하고 2×2 A/B로 원인을 분리할 수 있다
- [ ] 모델 vs 컴파일러 vs HW 설정을 일정·예산 소유·위험·재사용·되돌리기로 비교할 수 있다
- [ ] 모델 파일에서 예산표를 내는 CI 스크립트의 구성 요소를 말할 수 있다
- [ ] 주간 리뷰·단일 출처 예산·모델 카드 HW 섹션·에스컬레이션 규칙을 설계할 수 있다

---

## 참고 자료

- Williams, Waterman, Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", Communications of the ACM, 2009 — compute/memory-bound 판단의 원전 (D3)
- Lin et al., "MCUNet: Tiny Deep Learning on IoT Devices", NeurIPS 2020 — MCU의 peak SRAM을 루프 안에서 같이 최적화한 사례 (C7)
- Howard et al., "MobileNets: Efficient Convolutional Neural Networks for Mobile Vision Applications", 2017 — depthwise separable conv
- Sandler et al., "MobileNetV2: Inverted Residuals and Linear Bottlenecks", CVPR 2018 — ReLU6, 양자화 친화 구조
- Jacob et al., "Quantization and Training of Neural Networks for Efficient Integer-Arithmetic-Only Inference", CVPR 2018 — BN fold, int8 추론
- Mitchell et al., "Model Cards for Model Reporting", ACM FAT 2019 (현 FAccT) — 모델 카드 (HW 섹션은 이 노트의 확장)
- PyTorch 문서: `torch.profiler` — https://pytorch.org/docs/stable/profiler.html
- ONNX Runtime 문서: Profiling tools, Execution Providers — https://onnxruntime.ai/docs/
- TensorFlow Lite(LiteRT) 문서: Post-training integer quantization — https://ai.google.dev/edge/litert
- MIT 6.5940 TinyML and Efficient Deep Learning (Song Han) — https://efficientml.ai
