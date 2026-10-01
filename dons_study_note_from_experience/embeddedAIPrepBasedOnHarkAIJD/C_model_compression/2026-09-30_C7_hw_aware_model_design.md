# C7. HW-aware 모델 설계 — NAS, MCUNet, Once-for-All, latency LUT

> **이 노트를 다 읽으면**: FLOPs·파라미터 수가 latency를 왜 못 맞추는지 실측 숫자로 설명할 수 있다 · 레이어별 latency LUT를 만들고 네트워크 latency를 예측·검증할 수 있다 · search space·탐색 전략(random, evolution, RL, DARTS, supernet/OFA)을 비교하고 작은 NAS를 직접 돌려 Pareto front를 그릴 수 있다 · MCU의 peak SRAM을 블록별로 계산하고 MCUNet/MCUNetV2(patch 기반 추론)가 무엇을 줄였는지 말할 수 있다
> **JD 연결**: "Co-design model architectures that meet latency, memory, power, bandwidth" · study_prep_list **C7** 행: NAS, MCUNet, Once-for-All, latency lookup table로 모델 검색 ("co-design model architectures") · 연결: D1–D3(MAC·메모리·roofline), I1(예산), I2(HW 친화 설계), I6(피드백 루프), M2(벤치마크 방법론)
> **Don 기준 난이도**: 측정 기반 성능 튜닝, SRAM 예산 관리, 레지스터 수준 병목 분석은 이미 강하다 / 새로 배울 것은 "모델 구조를 탐색 가능한 공간으로 만들고, 비용 모델로 걸러서 자동으로 고르는" ML 쪽 절차와 용어
> **선행 노트**: B2(CNN·depthwise·MobileNetV2 블록·EfficientNet), B9(효율 아키텍처), A4(학습·검증). 병렬 작성 중인 C1(양자화)·C6(그래프 최적화)과 D1–D3를 함께 보면 좋다

---

## 0. 큰 그림 — 이게 왜 필요한가

펌웨어 쪽에서 Don이 늘 하던 일을 떠올려 보자. 새 SSD 컨트롤러에서 "4K random read 기준 latency X µs, SRAM Y KB, 전력 Z W"라는 스펙이 먼저 정해지고, 펌웨어 구조(큐 깊이, 버퍼 배치, 인터럽트 vs 폴링)를 바꿔 가며 측정하고, 스펙을 만족하는 설계 중 가장 좋은 것을 고른다. **HW-aware 모델 설계**는 이 과정을 신경망에 그대로 적용한 것이다.

- **제약(constraint)**: "wake word 모델은 20 ms 프레임마다 5 ms 안에, SRAM 64 KB, flash 200 KB, 추론당 에너지 100 µJ" 같은 숫자.
- **search space(탐색 공간)**: 모델 구조를 바꿀 수 있는 손잡이 모음. kernel 크기, 채널 수, 층 수, 입력 해상도 등.
- **평가**: 후보 구조마다 정확도(학습해야 알 수 있다)와 비용(측정하거나 예측한다)을 구한다.
- **선택**: 정확도-비용 평면에서 "더 싸면서 더 정확한 다른 후보가 없는" 점들, 즉 **Pareto front**에서 예산 안에 드는 것을 고른다.

이 루프를 사람이 손으로 돌리면 "모델 튜닝"이고, 알고리즘이 돌리면 **NAS(Neural Architecture Search)**다. 어느 쪽이든 embedded AI 엔지니어의 핵심 기여는 **비용 쪽**이다: "이 구조가 우리 칩에서 몇 ms, 몇 KB인가"를 빠르고 정확하게 말해 주는 **latency/memory 모델**을 만드는 것.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 330">
<text x="20" y="24" font-size="14">HW-aware 모델 설계 루프 (co-design loop)</text>
<rect x="20" y="50" width="190" height="70" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="115" y="75" font-size="13" text-anchor="middle">① 제약 (예산)</text> <text x="115" y="95" font-size="12" text-anchor="middle">latency · peak SRAM</text> <text x="115" y="111" font-size="12" text-anchor="middle">flash · energy</text>
<rect x="235" y="50" width="190" height="70" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="330" y="75" font-size="13" text-anchor="middle">② search space</text> <text x="330" y="95" font-size="12" text-anchor="middle">kernel · expand · depth</text> <text x="330" y="111" font-size="12" text-anchor="middle">width · resolution</text>
<rect x="450" y="50" width="190" height="70" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="545" y="75" font-size="13" text-anchor="middle">③ 후보 생성 (search)</text> <text x="545" y="95" font-size="12" text-anchor="middle">random · evolution</text> <text x="545" y="111" font-size="12" text-anchor="middle">RL · DARTS · supernet</text>
<rect x="450" y="190" width="190" height="80" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="545" y="214" font-size="13" text-anchor="middle">④ 평가</text> <text x="545" y="234" font-size="12" text-anchor="middle">정확도: 학습 / proxy</text> <text x="545" y="252" font-size="12" text-anchor="middle">비용: LUT · 메모리 계산</text>
<rect x="235" y="190" width="190" height="80" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="330" y="214" font-size="13" text-anchor="middle">⑤ Pareto front에서</text> <text x="330" y="234" font-size="12" text-anchor="middle">예산 안의 최고 정확도</text> <text x="330" y="252" font-size="12" text-anchor="middle">후보를 고른다</text>
<rect x="20" y="190" width="190" height="80" rx="6" fill="none" stroke="currentColor" stroke-width="2"/> <text x="115" y="214" font-size="13" text-anchor="middle">⑥ 타깃에서 실측</text> <text x="115" y="234" font-size="12" text-anchor="middle">양자화·컴파일 후</text> <text x="115" y="252" font-size="12" text-anchor="middle">예측 오차 확인</text>
<line x1="210" y1="85" x2="232" y2="85" stroke="currentColor"/> <polygon points="235,85 227,80 227,90" fill="currentColor"/> <line x1="425" y1="85" x2="447" y2="85" stroke="currentColor"/> <polygon points="450,85 442,80 442,90" fill="currentColor"/>
<line x1="545" y1="120" x2="545" y2="187" stroke="currentColor"/> <polygon points="545,190 540,182 550,182" fill="currentColor"/> <line x1="450" y1="230" x2="428" y2="230" stroke="currentColor"/> <polygon points="425,230 433,225 433,235" fill="currentColor"/>
<line x1="235" y1="230" x2="213" y2="230" stroke="currentColor"/> <polygon points="210,230 218,225 218,235" fill="currentColor"/> <line x1="115" y1="190" x2="115" y2="123" stroke="currentColor" stroke-dasharray="5 3"/> <polygon points="115,120 110,128 120,128" fill="currentColor"/>
<line x1="545" y1="270" x2="545" y2="300" stroke="#e08a3c" stroke-dasharray="4 3"/> <line x1="545" y1="300" x2="115" y2="300" stroke="#e08a3c" stroke-dasharray="4 3"/> <line x1="115" y1="300" x2="115" y2="273" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="330" y="318" font-size="12" text-anchor="middle">주황 점선: 비용 모델(LUT)은 ⑥의 실측으로 보정한다 · 검은 점선: 실측이 예산을 넘으면 제약·공간을 다시 잡는다</text>
</svg>
```

그림 1 — co-design 루프. embedded AI 엔지니어가 주로 책임지는 곳은 ①(예산을 숫자로), ④의 비용 절반(LUT·메모리 모델), ⑥(타깃 실측과 오차 추적)이다. ②·③·⑤는 모델 팀과 같이 한다.

이 노트의 순서: 제약을 숫자로(1절) → FLOPs가 latency를 못 맞추는 이유 실측(2절) → latency LUT 만들고 검증(3절) → search space 설계(4절) → 탐색 전략 비교(5절) → 작은 NAS 직접 돌리기(6절) → MCU 메모리: MCUNet·patch 추론(7절) → width/resolution/depth 스케일링(8절) → HW별 경험칙(9절) → KWS 64 KB 설계 예(10절) → 팀에서의 실무 흐름(11절).

모든 실측은 **Apple M2 CPU 코어 1개**(`torch.set_num_threads(1)`), PyTorch 2.8 eager, fp32, batch 1에서 했다. 타깃 칩(Cortex-M, Hexagon, Ethos-U)이 아니므로 **절대값이 아니라 현상과 방법**을 보라. 방법은 그대로 옮겨 쓸 수 있다.

---

## 1. 제약을 숫자로 — 네 가지 예산

co-design의 출발점은 "빠르게, 작게"가 아니라 **숫자**다 (I1 참고). 웨어러블급 기기를 예로 들면(예를 들어 Hark 같은 기기라면 — 추정):

| 예산 | 무엇을 세나 | 어디서 오나 | 모델 쪽에서 무엇으로 바뀌나 |
|---|---|---|---|
| latency | 입력 하나 처리 시간 (p50, p99) | 오디오 프레임 주기 10–20 ms, UI 반응 100 ms | 연산량, 연산 종류, 레이어 수 |
| peak SRAM | 추론 중 동시에 살아 있는 activation 최대 바이트 | MCU SRAM 크기 − 오디오 버퍼·스택·RTOS | 초기 층의 해상도 × 채널, skip 연결 |
| flash | 가중치 바이트 + 코드 | 내장 flash 1–2 MB, OTA 슬롯 | 파라미터 수 × 비트폭 |
| energy | 추론 1회 에너지 (µJ) | 배터리 용량 ÷ 목표 사용 시간 ÷ 추론 횟수 | MAC 수 + **메모리 이동 바이트** (D7) |

손계산 예: wake word 모델을 16 kHz 오디오에서 20 ms마다 돌리고, always-on 전력 예산이 1 mW라면 추론 한 번에 쓸 수 있는 에너지는 `1 mW × 20 ms = 20 µJ`다 (MCU 상주 전력, 마이크 전력은 따로 빼야 하므로 실제로는 더 작다). 말로 하면: **예산은 "초당 전력"이 아니라 "추론당 에너지"로 바꿔야 모델 비용과 비교할 수 있다.**

주의할 점 두 가지.

- **peak SRAM은 합이 아니라 최대값이다.** 레이어 activation을 다 더하는 게 아니라, 한 순간에 동시에 살아 있어야 하는 텐서들의 합의 최대값이다 (D2의 tensor lifetime). 7절에서 블록별로 계산한다.
- **latency는 평균이 아니라 deadline 기준이다** (D6). 오디오 파이프라인이라면 p99가 프레임 주기 안에 들어와야 한다.

---

## 2. 왜 FLOPs·파라미터 수는 latency를 못 맞추나

### 2.1 직관 — "명령어 개수로 실행 시간 예측하기"

펌웨어에서 "이 함수는 명령어 1,000개니까 1,000 cycle"이라고 예측하면 틀린다는 걸 Don은 안다. cache miss, 분기, 메모리 대기, 함수 호출 오버헤드가 있기 때문이다. MAC 수(FLOPs)로 latency를 예측하는 것도 똑같다. MAC 수는 **일의 양**이지 **걸리는 시간**이 아니다. 시간은 다음 네 가지가 정한다.

1. **연산 효율**: 이 연산이 HW의 MAC 배열(SIMD, 행렬 유닛)을 얼마나 꽉 채우나. 큰 GEMM(행렬곱)은 잘 채우고, depthwise conv는 채널마다 따로 작은 연산이라 못 채운다.
2. **메모리 대역폭**: MAC 하나당 몇 바이트를 옮기나 = arithmetic intensity의 역수 (D3). intensity가 낮으면 memory-bound라 MAC 유닛이 놀면서 데이터를 기다린다.
3. **op당 고정 오버헤드**: 커널 호출, 텐서 할당, 스케줄링, DMA 설정. 작은 레이어 여러 개는 이 비용을 여러 번 낸다.
4. **정렬과 타일링**: 채널 수가 SIMD 폭(예: 8, 16, 32)의 배수가 아니면 남는 lane이 논다. 64채널과 65채널은 MAC이 1.5% 차이지만 타일 하나가 더 붙는다.

### 2.2 정의 — 비교에 쓸 숫자들

- **MAC 수** (D1): conv 한 층은 `MAC = H_out · W_out · C_out · (C_in / groups) · k²`.
- **arithmetic intensity** (D3): `I = MAC 수 / 옮긴 바이트 수`. 단위는 MAC/B.
- **실효 처리율**: `GMAC/s = MAC 수 / latency`. 같은 코어에서 이 숫자가 레이어마다 크게 다르다면, MAC 수는 latency의 좋은 proxy가 아니다.

### 2.3 손계산 — 같은 MAC, 다른 구조

case A: standard 3×3 conv, 32 → 32 채널, 56×56 입력, stride 1.

```
MAC = 56 · 56 · 32 · 32 · 9 = 3,136 · 9,216 = 28,901,376 ≈ 28.9 M
바이트(fp32) = 입력 32·3136·4 + 출력 32·3136·4 + 가중치 9,248·4
            = 401,408 + 401,408 + 36,992 = 839,808 B
I = 28.9 M / 0.84 M ≈ 34 MAC/B
```

case B: 같은 MAC이 되도록 채널을 맞춘 depthwise separable (dw 3×3 + pw 1×1), 92 채널, 56×56.

```
픽셀당 MAC = 9·92 (dw) + 92·92 (pw) = 828 + 8,464 = 9,292
MAC = 3,136 · 9,292 ≈ 29.1 M            ← case A와 거의 같다
바이트 ≈ 입력 + 중간(dw 출력, 쓰고 다시 읽음) + 출력 ≈ 3 · 92·3136·4 ≈ 3.46 MB
I ≈ 29.1 M / 3.5 M ≈ 8 MAC/B            ← A의 1/4
```

case G: depthwise 3×3만, 512 채널, 28×28.

```
MAC = 28 · 28 · 512 · 9 = 3,612,672 ≈ 3.6 M
바이트 ≈ 입력 + 출력 = 2 · 512·784·4 = 3,211,264 B
I ≈ 1.1 MAC/B                            ← 옮긴 바이트 하나당 MAC 한 번
```

말로 하면: depthwise는 출력 원소 하나 만들 때 입력 9개만 곱하고 끝나서, **데이터를 가져오는 비용에 비해 계산이 너무 적다**. 그래서 memory-bound다 (B2 6절, D4).

### 2.4 코드로 확인

먼저 이 노트의 모든 예제가 공유하는 도구 모음이다. 예제 0 — `c7common.py`로 저장한다 (측정 함수, MAC 카운터, MobileNetV2식 블록, search space 정의, LUT 빌더). 30줄을 넘지만 한 번만 읽으면 된다.

```python
import time, random, torch, torch.nn as nn
torch.set_num_threads(1)                       # 코어 1개 = MCU/DSP 한 코어 흉내, 측정 잡음도 줄어든다
def bench(m, x, reps=40, warm=5):
    """p10 latency (ms). eval + inference_mode, batch=1."""
    m.eval()
    with torch.inference_mode():
        for _ in range(warm): m(x)
        ts = []
        for _ in range(reps):
            t0 = time.perf_counter(); m(x); ts.append(time.perf_counter() - t0)
    return sorted(ts)[len(ts) // 10] * 1e3       # 10번째 백분위: 다른 프로세스 간섭에 덜 민감
def make_div(v, d=8):                          # 채널을 8의 배수로 (SIMD/NPU 친화)
    return max(d, int(v + d / 2) // d * d)
def conv(cin, cout, k, s=1, g=1, act=True):    # BN은 배포 때 conv에 fold된다고 보고 뺀다
    layers = [nn.Conv2d(cin, cout, k, s, k // 2, groups=g, bias=True)]
    return nn.Sequential(*layers, nn.ReLU6()) if act else layers[0]
class MBConv(nn.Module):                       # MobileNetV2 inverted residual
    def __init__(s, cin, cout, k, e, stride):
        super().__init__(); mid = cin * e
        s.body = nn.Sequential(conv(cin, mid, 1), conv(mid, mid, k, stride, g=mid),
                               conv(mid, cout, 1, act=False))
        s.res = stride == 1 and cin == cout
    def forward(s, x):
        y = s.body(x); return x + y if s.res else y
STAGES = [(24, 2), (40, 2), (80, 2), (96, 1)]  # (base channels, 첫 블록 stride)
def sample_arch(rng):
    return dict(r=rng.choice([96, 128, 160]), w=rng.choice([0.5, 0.75, 1.0]),
                d=[rng.choice([1, 2, 3]) for _ in STAGES],
                ks=[rng.choice([3, 5, 7]) for _ in range(12)],
                es=[rng.choice([3, 6]) for _ in range(12)])
def block_specs(a):
    """arch dict -> 레이어(블록) 명세 리스트. LUT의 key가 된다."""
    r, w = a['r'], a['w']; c0 = make_div(16 * w); h = r // 2
    specs = [('stem', r, 3, c0, 3, 1, 2)]; cin = c0
    for si, ((c, s), d) in enumerate(zip(STAGES, a['d'])):
        cout = make_div(c * w)
        for j in range(d):
            st = s if j == 0 else 1; i = si * 3 + j
            specs.append(('mb', h, cin, cout, a['ks'][i], a['es'][i], st))
            h = (h + st - 1) // st; cin = cout
    specs.append(('head', h, cin, 4 * cin, 1, 1, 1))
    return specs
class Head(nn.Module):
    def __init__(s, cin, cmid, ncls=10):
        super().__init__(); s.c = conv(cin, cmid, 1); s.fc = nn.Linear(cmid, ncls)
    def forward(s, x):
        return s.fc(s.c(x).mean((2, 3)))
def build_layer(spec):
    t, h, cin, cout, k, e, st = spec
    if t == 'stem': return conv(cin, cout, k, st)
    if t == 'head': return Head(cin, cout)
    return MBConv(cin, cout, k, e, st)
def build_net(a):
    return nn.Sequential(*[build_layer(sp) for sp in block_specs(a)])
def in_shape(spec):
    return (1, spec[2], spec[1], spec[1])
def count_macs(m, x):
    tot = [0]
    def hk(mod, inp, out):
        if isinstance(mod, nn.Conv2d):
            tot[0] += out.numel() * mod.in_channels // mod.groups * mod.kernel_size[0] * mod.kernel_size[1]
        elif isinstance(mod, nn.Linear):
            tot[0] += mod.in_features * mod.out_features
    hs = [mm.register_forward_hook(hk) for mm in m.modules()]
    with torch.inference_mode(): m(x)
    for hh in hs: hh.remove()
    return tot[0]
def n_params(m):
    return sum(p.numel() for p in m.parameters())
def build_lut(rs, ws, reps=25):
    """search space에 나올 수 있는 모든 레이어 명세를 모아 하나씩 측정 -> {str(spec): ms}"""
    import itertools; keys = set()
    for r, w in itertools.product(rs, ws):
        for d in itertools.product([1, 2, 3], repeat=len(STAGES)):
            for k, e in itertools.product([3, 5, 7], [3, 6]):
                keys.update(block_specs(dict(r=r, w=w, d=list(d), ks=[k] * 12, es=[e] * 12)))
    lut = {}
    for sp in sorted(keys):
        torch.manual_seed(0); lut[str(sp)] = bench(build_layer(sp), torch.randn(in_shape(sp)), reps=reps)
    return lut
def lut_latency(lut, a):
    return sum(lut[str(sp)] for sp in block_specs(a))
```

측정 방법 메모: 중앙값(p50) 대신 **p10**을 쓴 이유는, 이 노트를 쓰는 동안 같은 머신에서 다른 작업이 돌고 있어 가끔 긴 꼬리가 섞였기 때문이다. 타깃 보드에서는 인터럽트를 막고 cycle counter(DWT `CYCCNT` 같은)로 재는 게 정석이고, 그때는 p50과 p99를 둘 다 본다.

예제 1 — MAC이 같거나 비슷한 레이어 쌍 7개의 latency와 실효 GMAC/s를 잰다.

```python
import torch, torch.nn as nn
from c7common import bench, count_macs, n_params
torch.manual_seed(0)
def C(ci, co, k, g=1): return nn.Conv2d(ci, co, k, 1, k // 2, groups=g)
cases = {                                     # 이름: (모델, 입력 shape)
 "A std3x3 32->32 @56":      (C(32, 32, 3), (1, 32, 56, 56)),
 "B dw3x3+pw 92 @56":        (nn.Sequential(C(92, 92, 3, g=92), C(92, 92, 1)), (1, 92, 56, 56)),
 "C 1x1 256->256 @28 x1":    (C(256, 256, 1), (1, 256, 28, 28)),
 "D 1x1 64->64 @28 x16":     (nn.Sequential(*[C(64, 64, 1) for _ in range(16)]), (1, 64, 28, 28)),
 "E 1x1 16->16 @112":        (C(16, 16, 1), (1, 16, 112, 112)),
 "F 1x1 256->256 @7":        (C(256, 256, 1), (1, 256, 7, 7)),
 "G dw3x3 only 512 @28":     (C(512, 512, 3, g=512), (1, 512, 28, 28)),
}
print(f"{'case':24s} {'MMAC':>6s} {'params':>7s} {'act KB':>7s} {'ms':>6s} {'GMAC/s':>7s}")
for name, (m, shp) in cases.items():
    x = torch.randn(shp); mac = count_macs(m, x)
    with torch.inference_mode(): y = m(x)
    act_kb = (x.numel() + y.numel()) * 4 / 1024          # fp32 입력+출력 (경계 텐서만)
    ms = bench(m, x, reps=60)
    print(f"{name:24s} {mac/1e6:6.1f} {n_params(m):7d} {act_kb:7.0f} {ms:6.3f} {mac/ms/1e6:7.1f}")
```

```text
case                       MMAC  params  act KB     ms  GMAC/s
A std3x3 32->32 @56        28.9    9248     784  0.211   137.1
B dw3x3+pw 92 @56          29.1    9476    2254  0.544    53.6
C 1x1 256->256 @28 x1      51.4   65792    1568  0.100   515.5
D 1x1 64->64 @28 x16       51.4   66560     392  0.299   171.9
E 1x1 16->16 @112           3.2     272    1568  0.055    58.2
F 1x1 256->256 @7           3.2   65792      98  0.022   143.0
G dw3x3 only 512 @28        3.6    5120    3136  0.725     5.0
```

출력에서 볼 것: 같은 코어에서 실효 처리율이 **5 GMAC/s부터 515 GMAC/s까지 100배** 차이 난다.

- **A vs B**: MAC·파라미터가 거의 같은데(28.9 M vs 29.1 M, 9,248 vs 9,476) depthwise separable 쪽이 2.6배 느리다. MAC을 줄이려고 DS conv를 쓰는데, 같은 MAC이면 DS conv가 더 비싸다는 뜻이다.
- **C vs D**: MAC·파라미터 동일(51.4 M). 큰 1×1 한 번이 작은 1×1 16번보다 3배 빠르다. 큰 GEMM은 MAC 배열을 꽉 채우고, 작은 op 16개는 op마다 호출 오버헤드와 덜 채워진 타일을 16번 낸다.
- **E vs F**: MAC 동일(3.2 M). 얇은 채널·큰 해상도(E)는 activation이 16배 많아 2.5배 느리다. F는 파라미터가 240배 많은데도 빠르다 — **파라미터 수는 latency와 거의 무관**할 수 있다.
- **G**: depthwise 단독은 5 GMAC/s. MAC은 3.6 M으로 E·F와 비슷한데 13~33배 느리다.

이 M2 코어가 큰 1×1에서 500 GMAC/s가 넘는 건 PyTorch가 내부적으로 쓰는 최적화된 행렬 연산 경로 덕분으로 보인다 (정확히 어떤 하드웨어 경로인지는 여기서 확인하지 않았다). 절대값은 칩마다 다르지만 "**GEMM형은 빠르고 depthwise·작은 op는 느리다**"는 패턴은 모바일 CPU, DSP, NPU 대부분에서 나타난다. NPU는 MAC 배열이 더 넓어서 이 격차가 **더 커지는** 경우가 많다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 300">
<text x="20" y="22" font-size="14">같은 CPU 코어, 레이어마다 실제로 뽑아낸 처리율 (GMAC/s, 높을수록 효율적)</text> <text x="192" y="56" font-size="12" text-anchor="end">A std 3×3, 32ch @56</text> <rect x="200" y="43" width="84.4" height="20" fill="#4a7bd0"/> <text x="290.4" y="57" font-size="12">137.1  (28.9 MMAC)</text> <text x="192" y="90" font-size="12" text-anchor="end">B dw3×3+pw, 92ch @56</text> <rect x="200" y="77" width="33.0" height="20" fill="#d0564a"/> <text x="239.0" y="91" font-size="12">53.6  (29.1 MMAC)</text>
<text x="192" y="124" font-size="12" text-anchor="end">C 1×1 256→256 @28 ×1</text> <rect x="200" y="111" width="317.2" height="20" fill="#4a7bd0"/> <text x="523.2" y="125" font-size="12">515.5  (51.4 MMAC)</text> <text x="192" y="158" font-size="12" text-anchor="end">D 1×1 64→64 @28 ×16</text> <rect x="200" y="145" width="105.8" height="20" fill="#e08a3c"/> <text x="311.8" y="159" font-size="12">171.9  (51.4 MMAC)</text> <text x="192" y="192" font-size="12" text-anchor="end">E 1×1 16→16 @112</text>
<rect x="200" y="179" width="35.8" height="20" fill="#e08a3c"/> <text x="241.8" y="193" font-size="12">58.2  (3.2 MMAC)</text> <text x="192" y="226" font-size="12" text-anchor="end">F 1×1 256→256 @7</text> <rect x="200" y="213" width="88.0" height="20" fill="#4a7bd0"/> <text x="294.0" y="227" font-size="12">143.0  (3.2 MMAC)</text> <text x="192" y="260" font-size="12" text-anchor="end">G dw3×3 512ch @28</text> <rect x="200" y="247" width="3.1" height="20" fill="#d0564a"/>
<text x="209.1" y="261" font-size="12">5.0  (3.6 MMAC)</text> <line x1="200" y1="36" x2="200" y2="282" stroke="currentColor"/> <text x="20" y="292" font-size="12">파랑 = 큰 GEMM형(연산 위주) · 주황 = 작은 레이어 여러 개/얇은 채널 · 빨강 = depthwise 포함</text>
</svg>
```

그림 2 — 예제 1의 실효 처리율. MAC 수가 같아도 막대 길이(= MAC당 시간의 역수)가 제각각이다. MAC으로 latency를 예측하면 이 막대들이 모두 같은 길이라고 가정하는 셈이다.

### 2.5 채널 수 정렬 — 64와 65의 차이

예제 2 — 1×1 conv(C → C, 28×28)의 채널 수를 1씩 바꿔 가며 latency를 잰다.

```python
import torch, torch.nn as nn
from c7common import bench
print(f"{'C':>4s} {'1x1 CxC @28 ms':>15s} {'GMAC/s':>7s}")
for C in [60, 62, 63, 64, 65, 66, 72, 96, 97, 128]:
    torch.manual_seed(0); m = nn.Conv2d(C, C, 1); x = torch.randn(1, C, 28, 28)
    ms = bench(m, x, reps=200); mac = C * C * 28 * 28
    print(f"{C:4d} {ms:15.4f} {mac / ms / 1e6:7.1f}")
```

```text
   C  1x1 CxC @28 ms  GMAC/s
  60          0.0219   128.8
  62          0.0220   137.2
  63          0.0224   139.1
  64          0.0216   148.8
  65          0.0263   125.8
  66          0.0265   128.9
  72          0.0279   145.8
  96          0.0320   225.8
  97          0.0343   214.8
 128          0.0436   294.4
```

출력에서 볼 것: 60~64 채널은 latency가 거의 같고(0.022 ms), **64 → 65에서 계단처럼 22% 뛴다** (MAC 증가는 3%). 97은 96보다 7% 느리다. latency가 채널 수에 대해 연속 함수가 아니라 **타일 단위 계단 함수**라는 뜻이다. 그래서 `make_div(v, 8)`처럼 채널을 8·16·32의 배수로 맞추는 관례가 있다. 어느 배수가 좋은지는 타깃마다 다르다 (Cortex-M의 SIMD는 int8 4개씩, NPU는 16·32·64 단위가 흔하다 — 반드시 벤더 문서와 실측으로 확인).

### 2.6 임베디드 연결과 함정

MCU(Cortex-M + CMSIS-NN류 커널)에서도 원리는 같다: depthwise는 채널별 루프라 SIMD 활용이 낮고, 1×1 conv는 행렬곱이라 잘 최적화되어 있다. NPU는 격차가 더 크고, **지원하지 않는 op는 CPU로 fallback되어 수십 배 느려질 수 있다** (C6, F 모듈). 흔한 함정 세 가지: "MAC이 10배 적으니 10배 빠르다"(B2 6절), "파라미터를 줄이면 빨라진다"(예제 1의 F가 반례), "원소별 op(ReLU, add, SE의 곱)는 MAC이 0이니 공짜다"(메모리를 통째로 한 번 읽고 쓴다 — fusion(C6)되지 않으면 시간이 든다).

---

## 3. Latency lookup table (LUT) — 측정을 조립해서 예측하기

### 3.1 직관

FLOPs로 예측할 수 없다면, **타깃에서 직접 재면 된다**. 그런데 후보가 수천 개면 전부 컴파일해서 보드에 올려 재는 건 너무 느리다. 해결책: 네트워크를 이루는 **레이어(블록) 종류는 유한**하다. 가능한 레이어 명세를 전부 한 번씩 재서 표로 만들고, 네트워크 latency는 **표에서 찾아 더한다**.

```
LAT(net) ≈ ∑ LUT[layer_spec_i]          (i = 1 … 레이어 수)
layer_spec = (종류, 입력 해상도, C_in, C_out, kernel, expand, stride)
```

말로 하면: 네트워크 latency ≈ 각 레이어를 따로 쟀을 때 latency의 합. 펌웨어에서 "함수별 cycle을 프로파일링해 두고 호출 시퀀스로 전체 시간을 추정"하는 것과 같다.

이 방법은 실제 NAS 논문들이 쓴 방식이다. FBNet(Wu et al., 2019)은 레이어별 latency LUT를 썼고, ProxylessNAS(Cai et al., 2019)는 latency 예측 모델로 latency를 미분 가능한 loss 항으로 넣었다. MnasNet(Tan et al., 2019)은 실제 휴대폰에서 직접 쟀다.

### 3.2 LUT key 설계 — 무엇이 latency를 바꾸나

key에 들어가야 하는 것 = latency를 바꾸는 모든 것. 우리 search space에서는 블록 하나의 latency가 `(입력 해상도, C_in, C_out, kernel, expand, stride)`로 완전히 정해진다. 가중치 **값**은 latency를 바꾸지 않는다 (dense 연산이라면. sparsity를 활용하는 HW에서는 달라진다 — C4).

손계산: 몇 개나 재야 하나? 해상도 3종 × width 3종 = 9가지 (r, w) 조합마다 stem 1개 + head(마지막 채널에 따라) + 4 stage × {첫 블록, 반복 블록} × kernel 3 × expand 2. 대략 `9 × (4 × 2 × 6) = 432`개. 실제로 dedupe하면 조금 달라진다. 반면 이 공간에 있는 **네트워크** 수는 4절에서 계산하듯 약 4×10¹⁰개다. 432번 재서 400억 개를 예측한다 — 이게 LUT의 레버리지다.

### 3.3 코드 — LUT 만들기

예제 3 — search space에 나올 수 있는 모든 블록 명세를 모아 하나씩 측정한다.

```python
import json, time
from c7common import build_lut
t0 = time.time()
lut = build_lut([96, 128, 160], [0.5, 0.75, 1.0])          # 레이어(블록) 하나씩 따로 측정
json.dump(lut, open("lut.json", "w"))
print("entries:", len(lut), f"| build time {time.time() - t0:.0f} s")
for sp in [('mb', 32, 24, 24, 3, 3, 1), ('mb', 32, 24, 24, 7, 3, 1), ('mb', 32, 24, 24, 3, 6, 1),
           ('mb', 32, 24, 24, 7, 6, 1), ('stem', 160, 3, 16, 3, 1, 2), ('head', 10, 96, 384, 1, 1, 1)]:
    print(sp, f"{lut[str(sp)]:.3f} ms")
```

```text
entries: 429 | build time 14 s
('mb', 32, 24, 24, 3, 3, 1) 0.193 ms
('mb', 32, 24, 24, 7, 3, 1) 1.383 ms
('mb', 32, 24, 24, 3, 6, 1) 0.351 ms
('mb', 32, 24, 24, 7, 6, 1) 2.752 ms
('stem', 160, 3, 16, 3, 1, 2) 0.229 ms
('head', 10, 96, 384, 1, 1, 1) 0.053 ms
```

출력에서 볼 것: 429개 항목을 14초에 쟀다. 같은 블록(32×32, 24 → 24)에서 kernel 3 → 7로 바꾸면 **7배 이상** 느려진다 (0.193 → 1.383 ms). MAC으로 보면 이 블록의 expand·project 1×1은 그대로이고 depthwise만 9 → 49로 늘어서, 블록 전체 MAC 증가는 약 1.7배다 (손으로 확인해 보라: 1×1 두 개 = 2·24·72·1024, dw = 72·k²·1024). 이 CPU에서는 큰 kernel depthwise가 특히 비싸다. **이런 비선형성이 LUT가 필요한 이유다.**

### 3.4 코드 — 예측 vs 실측

예제 4 — 무작위 네트워크 30개를 만들어 LUT 합과 실측 latency를 비교하고, "MAC × 최적 상수" 예측과도 비교한다.

```python
import json, random, numpy as np, torch
from c7common import *
lut = json.load(open("lut.json")); rng = random.Random(1); rows = []
for _ in range(30):                                        # 무작위 네트워크 30개
    a = sample_arch(rng); torch.manual_seed(0)
    m = build_net(a); x = torch.randn(1, 3, a['r'], a['r'])
    pred = sum(lut[str(sp)] for sp in block_specs(a))       # LUT 합 = 예측
    rows.append((count_macs(m, x) / 1e6, pred, bench(m, x, reps=25)))
mac, pred, meas = map(np.array, zip(*rows))
rank = lambda v: np.argsort(np.argsort(v))
def report(name, p):
    r = np.corrcoef(p, meas)[0, 1]; rho = np.corrcoef(rank(p), rank(meas))[0, 1]
    mape = np.mean(np.abs(p - meas) / meas) * 100
    print(f"{name:18s} pearson r={r:.3f}  spearman={rho:.3f}  MAPE={mape:5.1f}%")
report("LUT sum", pred)
k = np.sum(mac * meas) / np.sum(mac * mac)                 # 최선의 비례상수로 MAC→ms 적합
report("MACs x const", k * mac)
print("measured ms range:", meas.min().round(2), "~", meas.max().round(2))
np.save("ex4.npy", np.stack([mac, pred, meas]))
```

```text
LUT sum            pearson r=1.000  spearman=1.000  MAPE=  0.2%
MACs x const       pearson r=0.936  spearman=0.942  MAPE= 20.9%
measured ms range: 4.93 ~ 21.88
```

출력에서 볼 것:

- **LUT 합**: 상관계수 1.000, 평균 절대 백분율 오차(MAPE) 0.2%. 30개 네트워크의 latency를 거의 정확히 맞췄다.
- **MAC × 상수**: 상관 0.936으로 "대체로 비례"하긴 하지만 평균 21% 틀린다. 순위 상관(spearman) 0.942는, **두 후보 중 어느 쪽이 빠른지를 MAC으로 판단하면 꽤 자주 틀린다**는 뜻이다. NAS에서 중요한 건 순위다.
- 정직한 메모: 같은 코드를 머신 부하가 높을 때(다른 학습 작업이 동시에 돌 때) 돌렸을 때는 LUT 쪽이 r = 0.98~0.997, MAPE 4~16% 수준이었고, 첫 실행에서는 LUT가 체계적으로 **과소** 예측했다 (블록 단독 측정 때는 캐시가 따뜻하고, 전체 네트워크에서는 앞 블록이 캐시를 밀어냄). MAC 쪽은 매번 20% 안팎이었다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 310">
<text x="40" y="28" font-size="13">LUT 합 예측 vs 실측 (r=1.000)</text> <line x1="40.0" y1="260.0" x2="270.0" y2="260.0" stroke="currentColor"/> <line x1="40.0" y1="260.0" x2="40.0" y2="40.0" stroke="currentColor"/> <line x1="40.0" y1="260.0" x2="270.0" y2="40.0" stroke="#888" stroke-dasharray="4 3"/> <text x="40.0" y="276.0" font-size="12" text-anchor="middle">0</text> <text x="34.0" y="264.0" font-size="12" text-anchor="end">0</text> <text x="87.9" y="276.0" font-size="12" text-anchor="middle">5</text>
<text x="34.0" y="218.2" font-size="12" text-anchor="end">5</text> <text x="135.8" y="276.0" font-size="12" text-anchor="middle">10</text> <text x="34.0" y="172.3" font-size="12" text-anchor="end">10</text> <text x="183.8" y="276.0" font-size="12" text-anchor="middle">15</text> <text x="34.0" y="126.5" font-size="12" text-anchor="end">15</text> <text x="231.7" y="276.0" font-size="12" text-anchor="middle">20</text> <text x="34.0" y="80.7" font-size="12" text-anchor="end">20</text>
<circle cx="98.3" cy="204.3" r="3.5" fill="#4a7bd0"/> <circle cx="129.4" cy="174.3" r="3.5" fill="#4a7bd0"/> <circle cx="116.5" cy="186.4" r="3.5" fill="#4a7bd0"/> <circle cx="101.2" cy="201.4" r="3.5" fill="#4a7bd0"/> <circle cx="120.8" cy="182.7" r="3.5" fill="#4a7bd0"/> <circle cx="90.9" cy="211.3" r="3.5" fill="#4a7bd0"/> <circle cx="87.4" cy="214.8" r="3.5" fill="#4a7bd0"/>
<circle cx="94.5" cy="207.8" r="3.5" fill="#4a7bd0"/> <circle cx="113.6" cy="189.5" r="3.5" fill="#4a7bd0"/> <circle cx="155.8" cy="148.8" r="3.5" fill="#4a7bd0"/> <circle cx="110.2" cy="192.6" r="3.5" fill="#4a7bd0"/> <circle cx="144.9" cy="159.5" r="3.5" fill="#4a7bd0"/> <circle cx="111.7" cy="191.5" r="3.5" fill="#4a7bd0"/> <circle cx="95.3" cy="207.1" r="3.5" fill="#4a7bd0"/>
<circle cx="100.5" cy="202.3" r="3.5" fill="#4a7bd0"/> <circle cx="133.5" cy="170.9" r="3.5" fill="#4a7bd0"/> <circle cx="173.2" cy="133.0" r="3.5" fill="#4a7bd0"/> <circle cx="134.8" cy="169.6" r="3.5" fill="#4a7bd0"/> <circle cx="112.1" cy="191.3" r="3.5" fill="#4a7bd0"/> <circle cx="105.8" cy="197.2" r="3.5" fill="#4a7bd0"/> <circle cx="213.3" cy="94.6" r="3.5" fill="#4a7bd0"/>
<circle cx="185.0" cy="121.5" r="3.5" fill="#4a7bd0"/> <circle cx="249.4" cy="59.4" r="3.5" fill="#4a7bd0"/> <circle cx="93.4" cy="209.1" r="3.5" fill="#4a7bd0"/> <circle cx="222.7" cy="85.3" r="3.5" fill="#4a7bd0"/> <circle cx="136.9" cy="167.1" r="3.5" fill="#4a7bd0"/> <circle cx="103.5" cy="199.2" r="3.5" fill="#4a7bd0"/> <circle cx="173.1" cy="132.6" r="3.5" fill="#4a7bd0"/>
<circle cx="126.9" cy="176.9" r="3.5" fill="#4a7bd0"/> <circle cx="193.5" cy="113.2" r="3.5" fill="#4a7bd0"/> <text x="155.0" y="292.0" font-size="12" text-anchor="middle">예측 (ms)</text> <text x="370" y="28" font-size="13">MACs×상수 예측 vs 실측 (r=0.936)</text> <line x1="370.0" y1="260.0" x2="600.0" y2="260.0" stroke="currentColor"/> <line x1="370.0" y1="260.0" x2="370.0" y2="40.0" stroke="currentColor"/> <line x1="370.0" y1="260.0" x2="600.0" y2="40.0" stroke="#888" stroke-dasharray="4 3"/>
<text x="370.0" y="276.0" font-size="12" text-anchor="middle">0</text> <text x="364.0" y="264.0" font-size="12" text-anchor="end">0</text> <text x="417.9" y="276.0" font-size="12" text-anchor="middle">5</text> <text x="364.0" y="218.2" font-size="12" text-anchor="end">5</text> <text x="465.8" y="276.0" font-size="12" text-anchor="middle">10</text> <text x="364.0" y="172.3" font-size="12" text-anchor="end">10</text> <text x="513.8" y="276.0" font-size="12" text-anchor="middle">15</text>
<text x="364.0" y="126.5" font-size="12" text-anchor="end">15</text> <text x="561.7" y="276.0" font-size="12" text-anchor="middle">20</text> <text x="364.0" y="80.7" font-size="12" text-anchor="end">20</text> <circle cx="418.1" cy="204.3" r="3.5" fill="#e08a3c"/> <circle cx="441.5" cy="174.3" r="3.5" fill="#e08a3c"/> <circle cx="433.7" cy="186.4" r="3.5" fill="#e08a3c"/> <circle cx="404.8" cy="201.4" r="3.5" fill="#e08a3c"/>
<circle cx="442.1" cy="182.7" r="3.5" fill="#e08a3c"/> <circle cx="400.1" cy="211.3" r="3.5" fill="#e08a3c"/> <circle cx="390.6" cy="214.8" r="3.5" fill="#e08a3c"/> <circle cx="411.5" cy="207.8" r="3.5" fill="#e08a3c"/> <circle cx="451.4" cy="189.5" r="3.5" fill="#e08a3c"/> <circle cx="465.5" cy="148.8" r="3.5" fill="#e08a3c"/> <circle cx="425.6" cy="192.6" r="3.5" fill="#e08a3c"/>
<circle cx="454.7" cy="159.5" r="3.5" fill="#e08a3c"/> <circle cx="461.1" cy="191.5" r="3.5" fill="#e08a3c"/> <circle cx="423.2" cy="207.1" r="3.5" fill="#e08a3c"/> <circle cx="395.4" cy="202.3" r="3.5" fill="#e08a3c"/> <circle cx="478.2" cy="170.9" r="3.5" fill="#e08a3c"/> <circle cx="461.7" cy="133.0" r="3.5" fill="#e08a3c"/> <circle cx="461.4" cy="169.6" r="3.5" fill="#e08a3c"/>
<circle cx="446.1" cy="191.3" r="3.5" fill="#e08a3c"/> <circle cx="433.8" cy="197.2" r="3.5" fill="#e08a3c"/> <circle cx="543.2" cy="94.6" r="3.5" fill="#e08a3c"/> <circle cx="476.8" cy="121.5" r="3.5" fill="#e08a3c"/> <circle cx="602.5" cy="59.4" r="3.5" fill="#e08a3c"/> <circle cx="403.2" cy="209.1" r="3.5" fill="#e08a3c"/> <circle cx="593.7" cy="85.3" r="3.5" fill="#e08a3c"/>
<circle cx="492.4" cy="167.1" r="3.5" fill="#e08a3c"/> <circle cx="413.3" cy="199.2" r="3.5" fill="#e08a3c"/> <circle cx="514.3" cy="132.6" r="3.5" fill="#e08a3c"/> <circle cx="460.1" cy="176.9" r="3.5" fill="#e08a3c"/> <circle cx="503.4" cy="113.2" r="3.5" fill="#e08a3c"/> <text x="485.0" y="292.0" font-size="12" text-anchor="middle">예측 (ms)</text> <text x="12" y="150" font-size="12" transform="rotate(-90 12 150)" text-anchor="middle">실측 (ms)</text>
</svg>
```

그림 3 — 예제 4의 산점도. 점선은 예측 = 실측. 왼쪽(LUT)은 점이 대각선에 붙어 있고, 오른쪽(MAC × 상수)은 같은 예측값에서 실측이 크게 흩어진다.

### 3.5 왜 여기서는 이렇게 잘 맞았나 — 그리고 NPU에서는 왜 덜 맞나

PyTorch eager CPU 실행은 **op를 하나씩 순서대로** 실행한다. 레이어 사이에 겹침(overlap)도, 레이어를 넘나드는 fusion도 없다. 그래서 "따로 잰 것의 합"이 거의 정확하다. 실제 타깃에서 LUT 합이 틀어지는 원인:

| 원인 | 무슨 일이 생기나 | 대응 |
|---|---|---|
| op fusion (C6) | conv+ReLU+add가 컴파일러에서 한 커널로 합쳐져 따로 잰 합보다 빠르다 | LUT key를 **fusion 단위(블록)**로 잡는다. nn-Meter(Zhang et al., 2021)가 이 문제를 다룬 연구다 |
| 파이프라이닝·DMA 겹침 | NPU가 다음 레이어 가중치를 DMA로 미리 가져와 레이어 경계가 겹친다 | 블록 단위 측정 + 전체 네트워크 몇 개로 보정 |
| 캐시·SRAM 상태 | 단독 측정은 캐시가 따뜻하다. 전체 실행에서는 앞 레이어가 밀어낸다 | 측정 전에 캐시 flush, 또는 앞뒤에 더미 레이어를 붙여 잰다 |
| 메모리 배치 | activation이 SRAM에 들어가면 빠르고, 넘쳐서 DRAM으로 가면 느리다 (peak memory에 따라 레이어 latency가 달라짐) | key에 "메모리 tier"를 포함하거나, 메모리 계획까지 한 뒤 잰다 |
| DVFS·온도 | 클럭이 바뀌면 전부 바뀐다 | 클럭 고정, sustained 조건에서 측정 (M2) |

실무 요령: LUT 합을 그대로 믿지 말고, **실제 전체 네트워크 10~20개를 타깃에서 재서** `실측 ≈ α · LUT합 + β`로 보정한다. β는 "그래프 시작·종료 오버헤드", α는 "fusion·캐시로 인한 비율 차이"를 흡수한다. 보정 후 남는 오차가 수 %면 NAS 순위 판단에 충분하다.

### 3.6 LUT의 대안들

**분석 모델(roofline)** `latency ≈ max(MAC / peak_MAC_rate, bytes / bandwidth) + op_overhead`는 데이터시트만으로 만들 수 있어 HW 선정 단계(M 모듈)에 좋지만, depthwise 효율 같은 상수를 잘못 잡으면 크게 틀린다. **학습된 predictor**(레이어 shape를 입력으로 latency를 회귀하는 선형회귀·gradient boosting·MLP)는 LUT에 없는 조합도 보간한다. 후보가 수십 개뿐이면 **직접 측정**이 가장 정확하고, 최종 후보 검증은 항상 직접 측정으로 한다.

---

## 4. Search space 설계 — 무엇을 고르게 할 것인가

### 4.1 MobileNetV2식 블록 공간

실제 HW-aware NAS 대부분(MnasNet, ProxylessNAS, FBNet, OFA, MCUNet)이 **MobileNetV2 inverted residual 블록(MBConv)을 뼈대로, 블록마다 몇 가지 손잡이를 고르게** 하는 공간을 쓴다. 이 노트의 공간은 다음과 같다 (`c7common.py`의 `sample_arch`).

| 손잡이 | 선택지 | 무엇을 바꾸나 | 비용에 미치는 영향 |
|---|---|---|---|
| kernel k (블록마다) | 3, 5, 7 | depthwise의 공간 필터 크기 → receptive field | dw MAC ∝ k². CPU·NPU에서 k=7이 특히 비쌀 수 있다 (예제 3) |
| expand ratio e (블록마다) | 3, 6 | 블록 안쪽 폭 = C_in × e | 1×1 두 개와 dw의 MAC ∝ e, **블록 안쪽 activation ∝ e** |
| depth d (stage마다) | 1, 2, 3 | stage당 블록 수 | MAC·latency ∝ d, peak memory는 거의 불변 |
| width multiplier w (전역) | 0.5, 0.75, 1.0 | 모든 채널 × w (8의 배수로 반올림) | MAC ∝ w², 파라미터 ∝ w², activation ∝ w |
| input resolution r (전역) | 96, 128, 160 | 입력 크기 | MAC ∝ r², activation ∝ r², 파라미터 불변 |

손계산 — 공간 크기: 블록 하나의 선택지는 k 3개 × e 2개 = 6. stage 하나에서 depth가 1이면 6가지, 2면 6², 3이면 6³이므로 stage당 `6 + 36 + 216 = 258`가지. stage 4개면 `258⁴ ≈ 4.43 × 10⁹`, 여기에 해상도 3 × width 3을 곱하면 **약 4.0 × 10¹⁰개**. 전부 학습해 보는 건 불가능하다. 이게 탐색 전략(5절)이 필요한 이유다.

참고로 OFA의 공간은 unit 5개, unit당 depth 2/3/4, 블록당 kernel 3종 × expand 3종 = 9가지라서 `(9² + 9³ + 9⁴)⁵ = 7,371⁵ ≈ 2 × 10¹⁹`개다 — OFA 논문이 "10¹⁹개 이상의 subnet"이라고 말하는 숫자와 맞는다.

### 4.2 좋은 search space의 조건

- **HW가 잘 돌리는 op만 넣는다.** 공간에 NPU가 지원하지 않는 op(예: 특정 activation, 큰 dilation)가 있으면 탐색이 그걸 고를 수 있고, 배포 때 CPU fallback이 난다. 탐색 전에 공간을 HW로 걸러야 한다.
- **비용의 범위가 예산을 가로지르도록.** 공간의 가장 작은 모델도 예산을 넘으면 탐색은 무의미하다. 가장 큰 모델이 예산의 몇 배 정도인 게 좋다. MCUNet의 TinyNAS가 "search space 자체를 먼저 최적화"하는 이유가 이것이다 (7절).
- **사람이 이미 아는 좋은 구조를 포함.** MobileNetV2 자체가 공간 안의 한 점이 되게 만들면, 탐색 결과가 최소한 그보다 나쁘지 않은지 확인할 기준점이 생긴다.
- **손잡이는 비용 축별로.** 해상도는 activation·peak memory를, width는 파라미터·flash를, depth는 latency를, kernel은 receptive field를 주로 움직인다. 축이 분리되어 있어야 탐색이 예산별로 다른 답을 낸다.

---

## 5. 탐색 전략 — 어떻게 후보를 고르나

### 5.1 한눈에 비교

| 전략 | 아이디어 | 탐색 비용 | 장점 | 약점 |
|---|---|---|---|---|
| random search | 공간에서 균등하게 뽑아 평가 | 후보 수 × 학습 비용 | 구현 1분, 의외로 강한 baseline | 좋은 영역에 집중하지 못한다 |
| evolutionary | 좋은 후보를 부모로 변이·교차 | 후보 수 × 학습 비용 | 이산 공간·하드 제약에 자연스럽다 | 세대 수만큼 순차적 |
| RL (NASNet, MnasNet) | controller(RNN)가 구조를 샘플, reward로 policy gradient | 매우 큼 (수천 개 학습) | reward에 latency를 섞기 쉽다 | 계산량이 막대하다 |
| differentiable (DARTS) | op 선택을 softmax 가중합으로 풀어 gradient로 학습 | 학습 한두 번 수준 | 빠르다 | 메모리 많이 씀, 불안정, 이산화 시 괴리 |
| one-shot / supernet (OFA) | 모든 후보를 품은 큰 망을 한 번 학습, subnet은 가중치를 잘라 씀 | 큰 학습 1번 + 평가는 거의 공짜 | 기기마다 재학습 없이 특화 | supernet 학습이 까다롭다 |

### 5.2 Random search와 evolutionary search

random search는 "공간에서 N개 뽑아 학습·평가하고 제일 좋은 것"이다. 싱겁지만, 공간이 잘 설계되어 있으면 놀랄 만큼 강한 baseline이라서 **NAS 결과를 볼 때는 항상 random baseline과 비교했는지 물어야 한다**.

evolutionary search(예: AmoebaNet의 regularized evolution, Real et al., 2019)의 뼈대:

```
population ← 무작위 후보 P개 (예산 만족하는 것만)
반복:
    parents ← population에서 정확도 상위 몇 개
    child ← mutate(parent)       # 손잡이 몇 개를 무작위로 바꾼다
    if LAT(child) > 예산: 다시 뽑는다    ← LUT 덕분에 학습 전에 공짜로 거른다
    child를 학습·평가해서 population에 추가 (오래된 것 / 나쁜 것 제거)
```

펌웨어 비유: 레지스터 설정 튜닝에서 "지금까지 제일 좋았던 설정에서 한두 비트만 바꿔 다시 재 보기"를 반복하는 것과 같다. **하드 제약(latency ≤ T)을 LUT로 학습 전에 거를 수 있다**는 점이 HW-aware NAS에서 evolution이 인기 있는 이유다. 비싼 것은 학습이지 LUT 조회가 아니다.

### 5.3 RL 기반 NAS와 MnasNet reward

NAS의 시작(Zoph & Le, 2017)은 RNN controller가 구조를 한 토큰씩 생성하고, 그 구조를 학습한 검증 정확도를 reward로 REINFORCE(policy gradient) 업데이트를 하는 방식이었다. 막대한 GPU 시간이 들었다.

MnasNet(Tan et al., 2019)은 같은 틀에 **실측 latency**를 reward에 넣었다.

```
reward(m) = ACC(m) × [ LAT(m) / T ]^w
w = α   (LAT(m) ≤ T 일 때)
w = β   (LAT(m) > T 일 때)
```

말로 하면: 목표 latency T보다 느리면 정확도에 1보다 작은 배수를 곱해 깎고, 빠르면 1보다 큰 배수를 곱해 약간 올려 준다. 논문은 `α = β = −0.07`(soft 제약)과 `α = 0, β = −1`(T를 넘으면 강하게 벌점) 두 설정을 다뤘다. −0.07은 대략 "latency가 2배면 정확도가 약 5% 상대적으로 올라야 본전"이 되도록 잡은 값이다 (`2^−0.07 ≈ 0.953`).

예제 5 — 후보 4개에 세 가지 reward를 계산해 순위가 어떻게 바뀌는지 본다.

```python
# MnasNet식 reward = ACC × (LAT/T)^w,  w = α (LAT ≤ T) 또는 β (LAT > T)
T = 20.0                                                     # 목표 latency (ms)
def reward(acc, lat, alpha, beta):
    return acc * (lat / T) ** (alpha if lat <= T else beta)
cands = {"A small": (0.900, 12.0), "B on-target": (0.920, 20.0), "C slow": (0.935, 30.0), "D very slow": (0.945, 60.0)}
print(f"{'cand':12s} {'acc':>5s} {'lat':>5s} {'soft a=b=-0.07':>15s} {'a=0,b=-1':>9s} {'reject':>7s}")
for name, (acc, lat) in cands.items():
    print(f"{name:12s} {acc:.3f} {lat:5.1f} {reward(acc, lat, -0.07, -0.07):15.4f} "
          f"{reward(acc, lat, 0.0, -1.0):9.4f} {acc if lat <= T else 0.0:7.3f}")
```

```text
cand           acc   lat  soft a=b=-0.07  a=0,b=-1  reject
A small      0.900  12.0          0.9328    0.9000   0.900
B on-target  0.920  20.0          0.9200    0.9200   0.920
C slow       0.935  30.0          0.9088    0.6233   0.000
D very slow  0.945  60.0          0.8751    0.3150   0.000
```

출력에서 볼 것: soft reward(−0.07)에서는 **A(더 빠르고 정확도 2%p 낮음)가 1등**이다. soft 설정은 T를 "딱 맞춰야 할 선"이 아니라 "정확도와 교환할 수 있는 통화"로 다룬다 — 여러 latency의 좋은 모델을 두루 찾게 한다. 제품에서 "20 ms 넘으면 프레임 드랍"처럼 **deadline이 하드**라면 오른쪽 두 열(강한 벌점 또는 탈락)이 맞다. reward 설계 자체가 제품 요구사항을 반영하는 설계 결정이다.

### 5.4 Differentiable NAS — DARTS

DARTS(Liu, Simonyan, Yang, 2019)는 "이 자리에 어떤 op를 쓸까"라는 이산 선택을 **연속 가중합으로 풀었다**.

```
ō(x) = ∑_o  softmax(α)_o · o(x)          o ∈ {conv3×3, conv5×5, skip, pool, …}
```

말로 하면: 한 자리에 후보 op를 **전부 병렬로** 두고 출력을 softmax 가중치로 섞는다. 가중치 w는 학습 데이터로, 구조 파라미터 α는 검증 데이터로 번갈아 gradient descent 한다 (bilevel 최적화). 학습이 끝나면 자리마다 α가 가장 큰 op만 남긴다.

- 장점: 수천 번 학습 대신 큰 학습 한두 번이면 된다.
- 약점: 모든 후보 op를 동시에 메모리에 올리므로 **학습 메모리가 op 수만큼** 커진다. 마지막에 "가장 큰 α만 남기기"를 하면 섞인 망과 이산 망의 성능이 다를 수 있다.
- HW-aware 확장: ProxylessNAS·FBNet은 latency를 **기대값**으로 만들어 loss에 넣었다: `E[LAT] = ∑_o p_o · LUT[o]`. p가 연속이므로 E[LAT]도 미분 가능하다. 여기서 LUT가 다시 등장한다 — 3절의 표가 gradient 계산의 재료가 된다. (ProxylessNAS는 경로를 binarize해 메모리 문제도 줄였다.)

### 5.5 One-shot / supernet과 Once-for-All

**one-shot NAS**의 발상: 후보마다 따로 학습하지 말고, 모든 후보를 부분집합으로 품는 **supernet** 하나를 학습한다. 후보(subnet)는 supernet 가중치를 잘라 쓴다 (weight sharing).

- kernel 7×7 가중치의 **가운데 3×3·5×5**를 잘라 작은 kernel로 쓴다.
- expand 6의 채널 중 **앞쪽 채널만** 써서 expand 3·4로 쓴다.
- stage의 **앞쪽 블록만** 써서 depth를 줄인다.

**Once-for-All(OFA, Cai et al., ICLR 2020)**은 이걸 "기기별 특화" 문제로 정리했다: supernet을 **한 번** 학습하면, 새 기기가 생길 때마다 재학습 없이 그 기기의 LUT와 예산으로 subnet을 **검색만** 해서 꺼내 쓴다. 논문에서 강조하는 요소(요약, 세부는 원문 확인):

- **progressive shrinking**: 먼저 가장 큰 망을 학습하고, 그다음 kernel을 줄일 수 있게, 그다음 depth, 그다음 expand(폭)를 줄일 수 있게 단계적으로 fine-tune한다. 큰 subnet이 작은 subnet 때문에 망가지는 간섭을 줄인다. 큰 망을 teacher로 한 distillation(C5)도 쓴다.
- **elastic kernel**: 가운데를 자르기만 하지 않고, 작은 kernel용 **변환 행렬**을 두어 큰 kernel 가중치를 변형해 쓴다.
- **specialization**: 정확도 predictor(작은 MLP) + 기기별 latency LUT + evolutionary search로, 기기·예산마다 subnet을 몇 분 만에 고른다. subnet마다 BN 통계를 다시 계산(재보정)한다.

펌웨어 비유: 칩 여러 개(SKU)를 위해 펌웨어를 따로 짜지 않고, **기능 플래그가 전부 들어간 하나의 이미지를 만들어 두고 SKU별 설정으로 켜고 끄는** 것과 비슷하다. 다만 신경망에서는 "전부 켜진 상태"와 "일부만 켜진 상태"가 가중치를 공유해도 모두 잘 동작하도록 학습시키는 게 어렵다.

정직한 메모: 이 노트용으로 BN 없이 elastic kernel·expand·depth만 구현한 단순 supernet을 무작위 subnet 샘플링으로 학습해 보았는데, 수백 step 동안 loss가 거의 내려가지 않았고 "큰 망 먼저" 단계를 넣어도 subnet 정확도가 불안정했다. 원인을 따로 분석하지는 않았지만, OFA가 progressive shrinking·distillation·BN 재보정을 쓰는 이유를 체감하기엔 충분했다. 그래서 6절의 실습은 weight sharing 대신 **후보마다 짧게 따로 학습**하는 방법을 쓴다.

---

## 6. 직접 돌려 보는 작은 NAS — LUT latency + 정확도 proxy

### 6.1 설정

- **공간**: 4절의 공간을 MCU급으로 줄였다. width 0.5 고정, 해상도 {32, 48, 64}, depth·kernel·expand는 그대로.
- **latency**: 이 공간 전용 LUT(150개 항목)를 CPU 1코어에서 측정.
- **정확도 proxy**: 후보마다 150 step만 학습한 뒤 검증 정확도. 과제는 합성 데이터 — 10개 방향(18° 간격)의 사선 격자 무늬에 강한 가우시안 잡음(σ = 2)을 섞은 64×64 이미지를 후보 해상도로 area 축소한 것. 격자 주파수가 높아서 **해상도를 낮추면 신호가 뭉개진다**. 학습은 Apple GPU(MPS)에서 하고, latency는 CPU에서 잰다 — "학습은 서버, 비용은 타깃"이라는 실제 분업과 같은 구조다.
- **proxy의 한계**: 짧은 학습은 "빨리 배우는 모델"을 선호한다 (큰 모델은 150 step으로는 덜 수렴). 시드 하나라 ±1~2%p 잡음이 있다. 실제 NAS의 proxy(짧은 학습, supernet 가중치, 정확도 predictor)도 모두 이런 편향이 있어서, **최종 후보는 반드시 끝까지 학습해 다시 확인**한다.

정확도 proxy 코드 — `proxy.py`로 저장한다. 학습 때만 BN을 넣는다 (배포 때는 conv에 fold되므로 LUT에는 없다).

```python
import math, torch, torch.nn as nn, torch.nn.functional as F
from c7common import build_net
def make_data(n, seed, size=64, noise=2.0):                  # 10방향 격자 + 잡음 (합성 과제)
    g = torch.Generator().manual_seed(seed); y = torch.randint(0, 10, (n,), generator=g)
    th = y.float() * math.pi / 10; ph = 2 * math.pi * torch.rand(n, generator=g)
    yy, xx = torch.meshgrid(torch.arange(size).float(), torch.arange(size).float(), indexing="ij")
    u = xx * torch.cos(th)[:, None, None] + yy * torch.sin(th)[:, None, None]
    img = torch.sin(1.9 * u + ph[:, None, None]) + noise * torch.randn(n, size, size, generator=g)
    return img[:, None].expand(-1, 3, -1, -1), y
XTR, YTR = make_data(6400, 0); XTE, YTE = make_data(1000, 1)
def add_bn(m):                                               # 학습용 BN (배포 때는 conv에 fold → LUT엔 없음)
    for name, ch in m.named_children():
        if isinstance(ch, nn.Conv2d): setattr(m, name, nn.Sequential(ch, nn.BatchNorm2d(ch.out_channels)))
        else: add_bn(ch)
    return m
def proxy_acc(a, steps=150, bs=64, dev="mps"):              # "짧게 학습한 뒤 검증 정확도" = 정확도 proxy
    torch.manual_seed(0); m = add_bn(build_net(a)).to(dev); opt = torch.optim.Adam(m.parameters(), 1e-3)
    xtr = F.interpolate(XTR, size=a['r'], mode="area").to(dev); ytr = YTR.to(dev)
    for s in range(steps):
        i = torch.arange(s * bs, (s + 1) * bs) % len(xtr)
        loss = F.cross_entropy(m(xtr[i]), ytr[i]); opt.zero_grad(); loss.backward(); opt.step()
    m.eval()
    with torch.no_grad():
        p = m(F.interpolate(XTE, size=a['r'], mode="area").to(dev)).argmax(1).cpu()
    return (p == YTE).float().mean().item()
```

MPS가 없는 환경이면 `dev="cpu"`로 바꾸면 되지만, 이 머신의 CPU에서 depthwise 역전파는 매우 느려서(예제 1의 G 참고) 후보당 수십 초가 걸렸다.

### 6.2 Random search

예제 6 — 작은 공간의 LUT를 만들고, 제약 없이 무작위 후보 14개를 평가해 Pareto 점을 표시한다.

```python
import json, random, time
from c7common import build_lut, lut_latency, sample_arch
from proxy import proxy_acc
t0 = time.time(); lut = build_lut([32, 48, 64], [0.5]); json.dump(lut, open("lut_tiny.json", "w"))
print(f"tiny LUT: {len(lut)} entries, {time.time() - t0:.0f} s")
rng = random.Random(7); pts = []
for _ in range(14):                                          # random search: 14개 후보
    a = sample_arch(rng); a['r'] = rng.choice([32, 48, 64]); a['w'] = 0.5
    pts.append(dict(a=a, lat=lut_latency(lut, a), acc=proxy_acc(a)))
pts.sort(key=lambda p: p['lat']); best = -1
for p in pts:                                                # 지연 오름차순으로 훑으며 정확도 신기록 = Pareto
    p['pareto'] = p['acc'] > best; best = max(best, p['acc'])
    print(f"r={p['a']['r']} d={p['a']['d']}  LUT {p['lat']:5.2f} ms  acc {p['acc']:.3f}  {'*' if p['pareto'] else ''}")
json.dump(pts, open("random_pts.json", "w")); print(f"total {time.time() - t0:.0f} s")
```

```text
tiny LUT: 150 entries, 2 s
r=48 d=[2, 3, 1, 1]  LUT  1.72 ms  acc 0.938  *
r=64 d=[2, 2, 2, 1]  LUT  2.02 ms  acc 0.991  *
r=32 d=[3, 2, 1, 3]  LUT  2.15 ms  acc 0.630  
r=48 d=[1, 2, 2, 2]  LUT  2.41 ms  acc 0.798  
r=48 d=[2, 2, 2, 1]  LUT  3.05 ms  acc 0.924  
r=32 d=[1, 1, 3, 3]  LUT  3.38 ms  acc 0.717  
r=32 d=[1, 3, 3, 3]  LUT  3.43 ms  acc 0.524  
r=64 d=[1, 2, 3, 1]  LUT  3.44 ms  acc 0.918  
r=64 d=[3, 1, 3, 2]  LUT  3.49 ms  acc 0.892  
r=64 d=[2, 3, 2, 1]  LUT  3.59 ms  acc 0.943  
r=32 d=[3, 3, 3, 3]  LUT  3.74 ms  acc 0.433  
r=48 d=[3, 1, 1, 3]  LUT  3.77 ms  acc 0.924  
r=48 d=[3, 1, 2, 3]  LUT  3.98 ms  acc 0.699  
r=48 d=[3, 2, 3, 2]  LUT  5.14 ms  acc 0.793  
total 67 s
```

출력에서 볼 것:

- **Pareto 점(`*`)은 2개뿐**이다. 나머지 12개는 "더 빠르면서 더 정확한" 후보가 이미 있어서 지배(dominated)당했다.
- **해상도 32는 전부 나쁘다** (0.43~0.72). 이 과제는 높은 공간 주파수를 봐야 해서, 해상도를 줄이면 모델을 아무리 깊게 해도 회복되지 않는다. 가장 깊은 `r=32 d=[3,3,3,3]`이 0.433으로 꼴찌다 — **비용을 들인 곳이 틀렸다**.
- 이 과제에서 latency를 가장 효율적으로 쓰는 곳은 **해상도**이고, depth는 거의 도움이 안 된다. 과제마다 이 답은 다르다 — 그래서 탐색한다.
- **제약 T = 1.6 ms를 만족하는 후보는 하나도 없다** (최솟값 1.72 ms). 제약 없이 무작위로 뽑으면 예산이 빡빡할 때 쓸 게 없다.

### 6.3 Evolutionary search — 예산 안에서만

예제 7 — T = 1.6 ms 예산을 LUT로 먼저 거르고, 통과한 후보만 학습한다. 초기 6개 + 2세대 × 자식 4개 = 총 14개 학습 (random search와 같은 학습 예산).

```python
import copy, json, random, time
from c7common import lut_latency, sample_arch
from proxy import proxy_acc
lut = json.load(open("lut_tiny.json")); rng = random.Random(11); T = 1.6   # latency 예산 (ms)
def rand_arch():
    a = sample_arch(rng); a['r'] = rng.choice([32, 48, 64]); a['w'] = 0.5; return a
def mutate(a, p=0.3):
    b = copy.deepcopy(a); c = rand_arch()
    for key in ('r', 'd', 'ks', 'es'):
        if isinstance(b[key], list): b[key] = [cv if rng.random() < p else bv for bv, cv in zip(b[key], c[key])]
        elif rng.random() < p: b[key] = c[key]
    return b
def feasible(gen):                                           # LUT로 공짜 필터링: 예산 넘는 후보는 학습 안 함
    while True:
        a = gen()
        if lut_latency(lut, a) <= T: return a
t0 = time.time(); pop = []
for _ in range(6): a = feasible(rand_arch); pop.append((proxy_acc(a), lut_latency(lut, a), a))
print(f"gen0 (random, feasible) best acc {max(p[0] for p in pop):.3f}")
for g in range(1, 3):                                        # 2세대 x 자식 4개
    parents = sorted(pop, key=lambda p: -p[0])[:3]           # 상위 3개만 부모
    for _ in range(4):
        a = feasible(lambda: mutate(rng.choice(parents)[2])); pop.append((proxy_acc(a), lut_latency(lut, a), a))
    print(f"gen{g} best acc {max(p[0] for p in pop):.3f}  evaluated {len(pop)}")
acc, lat, a = max(pop, key=lambda p: p[0])
act = [(a['ks'][s * 3 + j], a['es'][s * 3 + j]) for s, d in enumerate(a['d']) for j in range(d)]
print(f"best: r={a['r']} d={a['d']} (k,e)={act} LUT {lat:.2f} ms  ({time.time() - t0:.0f} s)")
json.dump([dict(acc=p[0], lat=p[1], a=p[2]) for p in pop], open("evo_pts.json", "w"))
```

```text
gen0 (random, feasible) best acc 0.932
gen1 best acc 0.959  evaluated 10
gen2 best acc 0.995  evaluated 14
best: r=48 d=[1, 1, 1, 1] (k,e)=[(5, 6), (3, 6), (7, 3), (7, 3)] LUT 1.47 ms  (42 s)
```

출력에서 볼 것:

- 예산 안의 무작위 6개 중 최고는 0.932였고, 두 세대 변이 후 **0.995**가 됐다. 같은 14번의 학습으로 unconstrained random search는 예산 안의 후보를 하나도 못 찾았다.
- 찾은 구조는 **stage마다 블록 1개(가장 얕음)**, 해상도 48, 뒤쪽에 kernel 7. 6.2에서 본 "depth보다 해상도·receptive field" 경향과 맞는다. depth를 줄여 아낀 latency를 해상도와 큰 kernel에 쓴 셈이다.
- LUT 조회는 `feasible()` 안에서 수십 번 불렸지만 시간은 무시할 수준이다. 42초는 거의 전부 학습 시간이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 330">
<text x="60" y="24" font-size="14">정확도 proxy vs LUT latency — 무작위 14개 + 진화 탐색 14개</text> <line x1="60.0" y1="260.0" x2="620.0" y2="260.0" stroke="currentColor"/> <line x1="60.0" y1="260.0" x2="60.0" y2="40.0" stroke="currentColor"/> <text x="116.0" y="276.0" font-size="12" text-anchor="middle">1</text> <text x="228.0" y="276.0" font-size="12" text-anchor="middle">2</text> <text x="340.0" y="276.0" font-size="12" text-anchor="middle">3</text> <text x="452.0" y="276.0" font-size="12" text-anchor="middle">4</text>
<text x="564.0" y="276.0" font-size="12" text-anchor="middle">5</text> <text x="54.0" y="264.0" font-size="12" text-anchor="end">0.4</text> <text x="54.0" y="190.7" font-size="12" text-anchor="end">0.6</text> <text x="54.0" y="117.3" font-size="12" text-anchor="end">0.8</text> <text x="54.0" y="44.0" font-size="12" text-anchor="end">1.0</text> <line x1="183.2" y1="260.0" x2="183.2" y2="40.0" stroke="#d0564a" stroke-dasharray="5 3"/> <text x="187.2" y="241.7" font-size="12">예산 T = 1.6 ms</text>
<polyline points="97.1,70.1 125.0,65.7 129.0,55.0 168.3,41.8" fill="none" stroke="#3f9a6b" stroke-width="2"/> <circle cx="197.2" cy="62.7" r="4" fill="#4a7bd0"/> <circle cx="230.5" cy="43.3" r="4" fill="#4a7bd0"/> <circle cx="244.4" cy="175.7" r="4" fill="#4a7bd0"/> <circle cx="273.9" cy="114.1" r="4" fill="#4a7bd0"/> <circle cx="346.1" cy="67.9" r="4" fill="#4a7bd0"/> <circle cx="382.9" cy="143.8" r="4" fill="#4a7bd0"/>
<circle cx="388.0" cy="214.5" r="4" fill="#4a7bd0"/> <circle cx="389.7" cy="70.1" r="4" fill="#4a7bd0"/> <circle cx="394.8" cy="79.6" r="4" fill="#4a7bd0"/> <circle cx="405.8" cy="60.9" r="4" fill="#4a7bd0"/> <circle cx="423.4" cy="247.9" r="4" fill="#4a7bd0"/> <circle cx="425.9" cy="67.9" r="4" fill="#4a7bd0"/> <circle cx="449.2" cy="150.4" r="4" fill="#4a7bd0"/>
<circle cx="579.2" cy="115.9" r="4" fill="#4a7bd0"/> <rect x="177.6" y="60.9" width="8" height="8" fill="#e08a3c"/> <rect x="168.5" y="189.3" width="8" height="8" fill="#e08a3c"/> <rect x="119.5" y="166.2" width="8" height="8" fill="#e08a3c"/> <rect x="124.2" y="143.1" width="8" height="8" fill="#e08a3c"/> <rect x="97.4" y="69.7" width="8" height="8" fill="#e08a3c"/> <rect x="171.1" y="82.9" width="8" height="8" fill="#e08a3c"/>
<rect x="145.2" y="58.7" width="8" height="8" fill="#e08a3c"/> <rect x="125.0" y="51.0" width="8" height="8" fill="#e08a3c"/> <rect x="168.4" y="59.1" width="8" height="8" fill="#e08a3c"/> <rect x="155.2" y="159.2" width="8" height="8" fill="#e08a3c"/> <rect x="164.3" y="37.8" width="8" height="8" fill="#e08a3c"/> <rect x="121.0" y="61.7" width="8" height="8" fill="#e08a3c"/> <rect x="93.1" y="66.1" width="8" height="8" fill="#e08a3c"/>
<rect x="156.6" y="59.8" width="8" height="8" fill="#e08a3c"/> <text x="340.0" y="294.0" font-size="12" text-anchor="middle">LUT latency (ms, M2 CPU 1코어)</text> <text x="14" y="140" font-size="12" transform="rotate(-90 14 140)" text-anchor="middle">정확도 proxy</text> <text x="60" y="318" font-size="12">● 파랑 = 무작위 (제약 없음) · ■ 주황 = 진화 (T 이하만 학습) · 초록선 = Pareto front</text>
</svg>
```

그림 4 — 예제 6(파랑 원)과 예제 7(주황 사각형)의 모든 후보. 초록 선이 전체 Pareto front, 빨간 점선이 예산 T. 진화 탐색은 예산 왼쪽에 점을 몰아서 찍고, 그 안에서 front를 위로 밀어 올렸다. 오른쪽의 파랑 점들은 예산을 넘는 곳에 학습 비용을 쓴 것이다.

### 6.4 이 실습에서 가져갈 것

**LUT는 탐색 루프 안의 공짜 제약 검사기다** — 예산 밖 후보에 학습을 낭비하지 않는다. **proxy 정확도는 순위만 맞으면 된다** — 최종 후보는 제대로 학습해서 다시 잰다. **어느 축이 효율적인지는 과제가 정한다** — 이 합성 과제에서는 해상도였고, 실제 비전 과제에서도 해상도가 크게 작용하는 경우가 많다 (B6). KWS 같은 오디오 과제에서는 입력 길이·특징 수가 비슷한 역할을 한다.

---

## 7. MCU를 위한 메모리 중심 설계 — MCUNet과 patch 기반 추론

### 7.1 MCU에서는 latency보다 peak SRAM이 먼저 막힌다

휴대폰 NAS는 latency를 제약으로 삼았다. MCU(예: Cortex-M7, SRAM 수백 KB, flash 1~2 MB)에서는 **activation이 SRAM에 들어가느냐**가 먼저다. 못 들어가면 "느리다"가 아니라 **아예 돌지 않는다**. 가중치는 flash에서 직접 읽을 수 있지만(XIP), activation은 쓰기가 필요하므로 SRAM에 있어야 한다.

layer-by-layer 실행에서 어떤 conv가 실행되는 순간 살아 있어야 하는 바이트는:

```
need(layer) = 입력 텐서 + 출력 텐서 (+ residual이면 블록 입력 skip 텐서)
peak = max over layers need(layer)
```

말로 하면: 한 번에 두 텐서(와 skip)만 들고 있으면 되므로, 합이 아니라 **가장 큰 이웃 쌍**이 peak를 정한다 (D2, 펌웨어의 ping-pong 버퍼와 같다).

손계산 — MobileNetV2 width 0.35, 입력 144×144, int8. 블록 2는 `72×72×8` 입력을 expand 6배 해서 `72×72×48`을 만들고, stride 2 depthwise로 `36×36×48`을 만든다. depthwise 순간:

```
need = 72·72·48 + 36·36·48 = 248,832 + 62,208 = 311,040 B ≈ 303.8 KB
```

256 KB SRAM MCU에는 이 블록 하나 때문에 모델 전체가 못 들어간다.

### 7.2 코드 — 블록별 peak

예제 8 — torchvision MobileNetV2(width 0.35)의 모든 conv에 hook을 걸어 블록별 peak를 구한다.

```python
import torch, torch.nn as nn, torchvision.models as tvm
m = tvm.mobilenet_v2(width_mult=0.35).eval(); R = 144      # MCU급 MobileNetV2 (int8 가정: 원소 1개 = 1 byte)
recs = []                                                    # (블록 번호, conv 입력 원소, 출력 원소, 살아 있는 skip 원소)
for bi, blk in enumerate(m.features):
    res = getattr(blk, "use_res_connect", False); convs = [c for c in blk.modules() if isinstance(c, nn.Conv2d)]
    for ci, c in enumerate(convs):
        c.register_forward_hook(lambda mod, i, o, bi=bi, ci=ci, res=res:
            recs.append((bi, i[0].numel(), o.numel(), i[0].numel() if (res and ci > 0) else 0)))
with torch.no_grad(): m(torch.randn(1, 3, R, R))
skip = {}                                                    # 블록 첫 conv의 입력 = skip 텐서 크기
for bi, a, o, s in recs: skip.setdefault(bi, a)
peak_blk = {}
for bi, a, o, s in recs:
    need = a + o + (skip[bi] if s else 0)                   # 이 conv 실행 순간 살아 있어야 하는 바이트
    peak_blk[bi] = max(peak_blk.get(bi, 0), need)
print("peak KB per block:", " ".join(f"{p / 1024:.1f}" for p in peak_blk.values()))
top = max(peak_blk, key=peak_blk.get)
print(f"network peak = {peak_blk[top] / 1024:.1f} KB at block {top}; "
      f"blocks 8+ max = {max(v for k, v in peak_blk.items() if k >= 8) / 1024:.1f} KB")
```

```text
peak KB per block: 141.8 162.0 303.8 131.6 75.9 65.8 65.8 38.0 24.7 24.7 24.7 22.8 32.9 32.9 19.9 17.8 17.8 16.4 34.0
network peak = 303.8 KB at block 2; blocks 8+ max = 34.0 KB
```

출력에서 볼 것: peak가 **앞쪽 몇 블록에 몰려 있고**(303.8 KB), 블록 8 이후는 최대 34 KB다. 9배 가까운 불균형이다. 메모리 예산은 가장 비싼 한 블록이 정하므로, **뒤쪽 블록들은 SRAM을 거의 놀리고 있다**. 손계산(303.8 KB)과 코드 결과가 일치한다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 300">
<text x="20" y="22" font-size="14">MobileNetV2-0.35 @144, int8 — 블록별 activation peak (KB)</text> <line x1="66" y1="250.0" x2="630" y2="250.0" stroke="currentColor"/> <line x1="66" y1="250.0" x2="66" y2="40.0" stroke="currentColor"/> <text x="60" y="254.0" font-size="12" text-anchor="end">0</text> <text x="60" y="188.4" font-size="12" text-anchor="end">100</text> <text x="60" y="122.8" font-size="12" text-anchor="end">200</text> <text x="60" y="57.1" font-size="12" text-anchor="end">300</text>
<rect x="70" y="156.9" width="22" height="93.1" fill="#d0564a"/> <text x="81" y="265.0" font-size="12" text-anchor="middle">0</text> <rect x="99" y="143.7" width="22" height="106.3" fill="#d0564a"/> <text x="110" y="265.0" font-size="12" text-anchor="middle">1</text> <rect x="128" y="50.6" width="22" height="199.4" fill="#d0564a"/> <text x="139" y="265.0" font-size="12" text-anchor="middle">2</text> <rect x="157" y="163.6" width="22" height="86.4" fill="#d0564a"/>
<text x="168" y="265.0" font-size="12" text-anchor="middle">3</text> <rect x="186" y="200.2" width="22" height="49.8" fill="#4a7bd0"/> <text x="197" y="265.0" font-size="12" text-anchor="middle">4</text> <rect x="215" y="206.8" width="22" height="43.2" fill="#4a7bd0"/> <text x="226" y="265.0" font-size="12" text-anchor="middle">5</text> <rect x="244" y="206.8" width="22" height="43.2" fill="#4a7bd0"/> <text x="255" y="265.0" font-size="12" text-anchor="middle">6</text>
<rect x="273" y="225.1" width="22" height="24.9" fill="#4a7bd0"/> <text x="284" y="265.0" font-size="12" text-anchor="middle">7</text> <rect x="302" y="233.8" width="22" height="16.2" fill="#4a7bd0"/> <text x="313" y="265.0" font-size="12" text-anchor="middle">8</text> <rect x="331" y="233.8" width="22" height="16.2" fill="#4a7bd0"/> <text x="342" y="265.0" font-size="12" text-anchor="middle">9</text> <rect x="360" y="233.8" width="22" height="16.2" fill="#4a7bd0"/>
<text x="371" y="265.0" font-size="12" text-anchor="middle">10</text> <rect x="389" y="235.0" width="22" height="15.0" fill="#4a7bd0"/> <text x="400" y="265.0" font-size="12" text-anchor="middle">11</text> <rect x="418" y="228.4" width="22" height="21.6" fill="#4a7bd0"/> <text x="429" y="265.0" font-size="12" text-anchor="middle">12</text> <rect x="447" y="228.4" width="22" height="21.6" fill="#4a7bd0"/> <text x="458" y="265.0" font-size="12" text-anchor="middle">13</text>
<rect x="476" y="236.9" width="22" height="13.1" fill="#4a7bd0"/> <text x="487" y="265.0" font-size="12" text-anchor="middle">14</text> <rect x="505" y="238.3" width="22" height="11.7" fill="#4a7bd0"/> <text x="516" y="265.0" font-size="12" text-anchor="middle">15</text> <rect x="534" y="238.3" width="22" height="11.7" fill="#4a7bd0"/> <text x="545" y="265.0" font-size="12" text-anchor="middle">16</text> <rect x="563" y="239.2" width="22" height="10.8" fill="#4a7bd0"/>
<text x="574" y="265.0" font-size="12" text-anchor="middle">17</text> <rect x="592" y="227.7" width="22" height="22.3" fill="#4a7bd0"/> <text x="603" y="265.0" font-size="12" text-anchor="middle">18</text> <line x1="66" y1="82.0" x2="630" y2="82.0" stroke="#888" stroke-dasharray="5 3"/> <text x="630" y="77.0" font-size="12" text-anchor="end">256 KB SRAM (예시 MCU)</text> <line x1="66" y1="168.9" x2="630" y2="168.9" stroke="#3f9a6b" stroke-dasharray="5 3"/> <text x="630" y="163.9" font-size="12" text-anchor="end">블록 0–3을 4×4 patch로: 123.6 KB</text>
<text x="340" y="288" font-size="12" text-anchor="middle">블록 번호 (빨강 = 고해상도 앞단, patch 대상)</text>
</svg>
```

그림 5 — 블록별 activation peak. 빨강(블록 0–3)이 256 KB 예시 SRAM선을 넘기거나 거의 닿는다. 초록 점선은 7.4에서 블록 0–3을 patch로 실행했을 때의 peak.

### 7.3 MCUNet — TinyNAS + TinyEngine

MCUNet(Lin et al., NeurIPS 2020)은 "MCU에서 ImageNet급 모델"을 목표로 **모델 탐색과 추론 엔진을 함께 설계**했다 (system-algorithm co-design). 핵심 요소(요약, 수치는 원문 확인):

- **제약을 peak SRAM과 flash로**: latency보다 메모리 제약이 먼저라는 7.1의 관점.
- **TinyNAS 1단계 — search space 자체를 최적화**: MCU마다 메모리가 다르므로 해상도·width 조합 후보별로 search space를 만들고, 그 공간에서 **메모리 제약을 만족하는 모델들의 FLOPs 분포**를 본다. 같은 메모리 안에서 FLOPs가 큰 모델이 많은 공간일수록 정확도가 높은 모델이 있을 가능성이 크다는 경험칙으로 공간을 고른다. 학습 없이 샘플링만으로 할 수 있다.
- **TinyNAS 2단계 — 그 공간 안에서 one-shot NAS**: supernet 학습 + evolutionary search (5.5의 OFA 계열 방법).
- **TinyEngine**: 인터프리터 대신 **모델별 코드 생성**(런타임 오버헤드와 안 쓰는 코드 제거), 모델에 맞춘 메모리 스케줄링, **in-place depthwise**(depthwise는 채널끼리 섞이지 않으므로 채널 하나 분량 임시 버퍼만으로 입력 위에 출력을 덮어쓴다 — 7.1의 `need`에서 depthwise의 입력+출력을 거의 절반으로 줄인다), op fusion, loop 최적화. 논문은 TF Lite Micro·CMSIS-NN 대비 메모리와 속도 모두 개선을 보고했다.

co-design의 교훈: **엔진이 메모리를 어떻게 쓰는지(in-place 여부, 스케줄링)가 바뀌면 "좋은 모델"의 모양도 바뀐다.** 그래서 탐색 쪽 메모리 모델은 반드시 실제 엔진의 규칙을 따라야 한다. Don이 펌웨어에서 "메모리 맵이 바뀌면 버퍼 배치 최적해가 바뀐다"고 느끼는 것과 같다.

### 7.4 MCUNetV2 — patch 기반 추론

MCUNetV2(Lin et al., NeurIPS 2021)는 7.2의 불균형을 직접 공략했다. 메모리를 많이 먹는 **앞쪽 stage만** 이미지 전체가 아니라 **작은 공간 patch 단위로** 실행하고, 결과를 출력 버퍼에 이어 붙인 뒤, 나머지는 평소처럼 layer-by-layer로 실행한다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 300">
<text x="20" y="22" font-size="14">patch 기반 추론 — 앞 stage를 3×3 patch로 나눠 하나씩</text>
<rect x="40" y="50" width="200" height="200" fill="none" stroke="currentColor" stroke-width="2"/> <line x1="106.7" y1="50" x2="106.7" y2="250" stroke="currentColor"/> <line x1="173.3" y1="50" x2="173.3" y2="250" stroke="currentColor"/> <line x1="40" y1="116.7" x2="240" y2="116.7" stroke="currentColor"/> <line x1="40" y1="183.3" x2="240" y2="183.3" stroke="currentColor"/>
<rect x="106.7" y="116.7" width="66.6" height="66.6" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0" stroke-width="2"/> <rect x="90" y="100" width="100" height="100" fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/>
<text x="140" y="270" font-size="12" text-anchor="middle">입력 144×144 (patch 하나 + halo)</text>
<line x1="250" y1="150" x2="330" y2="150" stroke="currentColor"/> <polygon points="336,150 328,145 328,155" fill="currentColor"/> <rect x="340" y="115" width="110" height="70" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="395" y="140" font-size="12" text-anchor="middle">블록 0–3 실행</text> <text x="395" y="158" font-size="12" text-anchor="middle">(patch 크기의</text> <text x="395" y="174" font-size="12" text-anchor="middle">activation만)</text>
<line x1="450" y1="150" x2="490" y2="150" stroke="currentColor"/> <polygon points="496,150 488,145 488,155" fill="currentColor"/>
<rect x="500" y="90" width="120" height="120" fill="none" stroke="currentColor" stroke-width="2"/> <line x1="540" y1="90" x2="540" y2="210" stroke="currentColor"/> <line x1="580" y1="90" x2="580" y2="210" stroke="currentColor"/> <line x1="500" y1="130" x2="620" y2="130" stroke="currentColor"/> <line x1="500" y1="170" x2="620" y2="170" stroke="currentColor"/> <rect x="540" y="130" width="40" height="40" fill="#4a7bd0" fill-opacity="0.35"/>
<text x="560" y="230" font-size="12" text-anchor="middle">출력 버퍼 36×36</text> <text x="560" y="246" font-size="12" text-anchor="middle">(칸마다 채움)</text>
<text x="20" y="292" font-size="12">파랑 = patch가 책임지는 영역 · 주황 점선 = receptive field 때문에 더 읽어야 하는 halo (이웃 patch와 겹침 → 중복 계산)</text>
</svg>
```

그림 6 — patch 기반 추론. patch 하나를 처리하는 동안에는 patch 크기의 중간 텐서만 있으면 된다. 대가는 halo(가장자리 겹침)만큼의 **중복 계산**이다.

halo 크기 손계산: 출력 한 칸이 입력에서 보는 범위(receptive field, B2 4절)를 층별로 쌓는다. `r ← r + (k − 1)·j`, `j ← j·s` (j는 누적 stride).

```
블록 0 (stem 3×3, s2):   r = 1 + 2·1 = 3,    j = 2
블록 1 (dw 3×3, s1):     r = 3 + 2·2 = 7,    j = 2
블록 2 (dw 3×3, s2):     r = 7 + 2·2 = 11,   j = 4
블록 3 (dw 3×3, s1):     r = 11 + 2·4 = 19,  j = 4
→ 출력 한 칸은 중심에서 좌우 (19−1)/2 = 9 픽셀씩 더 본다
→ stride 4에 맞춰 올리면 halo = 12 픽셀
```

(1×1 conv는 receptive field를 늘리지 않는다.) 블록 0–6까지 가면 같은 계산으로 r = 59, halo는 29 → 8의 배수로 올려 32가 된다.

예제 9 — 앞쪽 블록을 P×P patch로 실행해서 (1) 결과가 전체 실행과 **같은지** 확인하고, (2) peak memory와 MAC 증가를 잰다.

```python
import torch, torch.nn as nn, torchvision.models as tvm
from c7common import count_macs
torch.manual_seed(0); m = tvm.mobilenet_v2(width_mult=0.35).eval(); R = 144
def peak(model, x):                                           # 각 conv 순간의 (입력+출력+skip) 최대, int8 byte
    need = [0]; hs = []
    for blk in model:
        res = getattr(blk, "use_res_connect", False); skip = []
        for ci, c in enumerate([c for c in blk.modules() if isinstance(c, nn.Conv2d)]):
            def hk(mod, i, o, ci=ci, res=res, skip=skip):
                if ci == 0: skip[:] = [i[0].numel()]
                need[0] = max(need[0], i[0].numel() + o.numel() + (skip[0] if res and ci > 0 else 0))
            hs.append(c.register_forward_hook(hk))
    with torch.no_grad(): y = model(x)
    for h in hs: h.remove()
    return need[0], y
def run(nb, S, HALO, P):                                      # 앞 nb블록(누적 stride S)을 P×P patch로 실행
    stage = m.features[:nb]; x = torch.randn(1, 3, R, R); full_peak, full = peak(stage, x)
    O, C = full.shape[-1], full.shape[1]; out = torch.zeros_like(full); pp = macs = 0; st = O // P
    for o0 in range(0, O, st):
        for q0 in range(0, O, st):
            a0, a1 = max(0, o0 * S - HALO), min(R, (o0 + st) * S + HALO)   # halo 포함 입력 영역 (S 배수 정렬)
            b0, b1 = max(0, q0 * S - HALO), min(R, (q0 + st) * S + HALO)
            xp = x[:, :, a0:a1, b0:b1]; pk, yp = peak(stage, xp); pp = max(pp, pk); macs += count_macs(stage, xp)
            oa, ob = (o0 * S - a0) // S, (q0 * S - b0) // S
            out[:, :, o0:o0 + st, q0:q0 + st] = yp[:, :, oa:oa + st, ob:ob + st]
    tot = 3 * R * R + C * O * O + pp                           # 원본 이미지 + 출력 버퍼 + patch 하나의 peak
    print(f"blocks 0-{nb - 1} {P}x{P}: err {(out - full).abs().max().item():.1e}  peak {full_peak / 1024:5.1f} -> "
          f"{tot / 1024:5.1f} KB   MACs x{macs / count_macs(stage, x):.2f}")
for cfg in [(4, 4, 12, 2), (4, 4, 12, 3), (4, 4, 12, 4), (7, 8, 32, 3)]: run(*cfg)
```

```text
blocks 0-3 2x2: err 7.5e-09  peak 303.8 -> 174.2 KB   MACs x1.36
blocks 0-3 3x3: err 7.5e-09  peak 303.8 -> 146.8 KB   MACs x1.78
blocks 0-3 4x4: err 7.5e-09  peak 303.8 -> 123.6 KB   MACs x2.25
blocks 0-6 3x3: err 0.0e+00  peak 303.8 -> 249.6 KB   MACs x3.57
```

출력에서 볼 것:

- **err ≈ 1e-8**: patch로 나눠 계산해도 결과는 전체 실행과 같다 (fp32 반올림 수준). halo를 receptive field만큼 잡고, 입력 경계에서만 zero padding이 일어나도록 잘랐기 때문이다. 펌웨어에서 "타일 DMA로 나눠 처리한 결과가 전체 처리와 bit-exact인지" 확인하는 것과 같은 검증이다 (C8).
- **블록 0–3을 4×4 patch로**: peak 303.8 → 123.6 KB (2.5배 감소). 이제 네트워크 전체 peak는 이 123.6 KB이고(나머지 블록 최대 75.9 KB), 256 KB MCU에 들어간다. 123.6 KB 중 60.8 KB는 **원본 이미지 자체**다 (patch가 다 끝날 때까지 들고 있어야 함). 카메라에서 행 단위로 받아 바로 patch에 넣는 식이면 더 줄일 여지가 있다.
- **대가는 MAC**: patch를 잘게 나눌수록 halo 겹침이 늘어 MAC이 1.36 → 2.25배. 메모리 ↔ 연산의 교환이다.
- **patch 구간을 너무 깊게(블록 0–6) 잡으면 역효과**: receptive field가 59로 커져 halo(32픽셀)가 patch(48픽셀)만큼 커지고, MAC은 3.57배인데 메모리는 249.6 KB로 거의 안 준다.

MCUNetV2 논문은 이 halo 비용을 줄이려고 **receptive field 재분배**(patch 구간의 receptive field는 작게, 뒤쪽 구간에서 키움)를 제안하고, patch 설정과 모델 구조를 **함께 탐색**했다. 논문은 기존 네트워크의 peak memory를 수 배(요약에서 4~8배로 언급) 줄였다고 보고한다 — 정확한 조건은 원문 확인.

---

## 8. Width · resolution · depth 스케일링 — 같은 MAC, 다른 비용

B2 7.6절의 EfficientNet은 "깊이 d, 폭 w, 해상도 r을 정해진 비율로 함께 키운다"(compound scaling)고 했다. 그 논문의 목표는 **정확도 대비 FLOPs**였다. MCU·NPU에서는 같은 FLOPs라도 **어느 축으로 키웠는지에 따라 메모리·latency가 다르다**.

손계산 — conv 한 층의 비용이 각 축에 어떻게 비례하나:

| 축 | MAC | 파라미터 (flash) | activation (peak SRAM) |
|---|---|---|---|
| width × a | a² | a² | a |
| resolution × a | a² | 1 (불변) | a² |
| depth × a | a | a | 1 (거의 불변) |

말로 하면: **flash가 빡빡하면 해상도로, SRAM이 빡빡하면 깊이나 폭으로, latency만 보면 연산 효율이 좋은 축(보통 폭)으로** 키우는 게 유리하다.

예제 10 — 기준 모델에서 한 축씩 키운 변형들의 MAC, 파라미터, peak activation(int8), latency를 잰다.

```python
import torch, torch.nn as nn
from c7common import build_net, count_macs, n_params, bench
def peak_kb(m, x):                                          # conv 순간 입력+출력 최대 (int8, skip 무시한 근사)
    pk = [0]; hs = [c.register_forward_hook(lambda mod, i, o: pk.__setitem__(0, max(pk[0], i[0].numel() + o.numel())))
                    for c in m.modules() if isinstance(c, nn.Conv2d)]
    with torch.no_grad(): m(x)
    for h in hs: h.remove()
    return pk[0] / 1024
base = dict(r=96, w=0.5, d=[2, 2, 2, 2], ks=[3] * 12, es=[6] * 12)
variants = [("base", {}), ("width x1.5", dict(w=0.75)), ("width x2", dict(w=1.0)),
            ("res x1.33", dict(r=128)), ("res x1.67", dict(r=160)),
            ("depth 3/stage", dict(d=[3] * 4)), ("compound", dict(r=112, w=0.625, d=[2, 3, 3, 2]))]
print(f"{'variant':14s} {'MMAC':>6s} {'params K':>8s} {'peak KB':>8s} {'ms':>6s}")
for name, ch in variants:
    a = {**base, **ch}; torch.manual_seed(0); m = build_net(a); x = torch.randn(1, 3, a['r'], a['r'])
    print(f"{name:14s} {count_macs(m, x) / 1e6:6.1f} {n_params(m) / 1e3:8.1f} {peak_kb(m, x):8.1f} {bench(m, x):6.2f}")
```

```text
variant          MMAC params K  peak KB     ms
base             10.6    118.2    135.0   2.60
width x1.5       20.1    250.4    270.0   3.48
width x2         30.9    408.3    270.0   4.20
res x1.33        18.9    118.2    240.0   3.77
res x1.67        29.6    118.2    375.0   4.86
depth 3/stage    16.0    183.6    135.0   3.11
compound         20.1    209.9    183.8   3.34
```

출력에서 볼 것:

- **width x2 vs res x1.67**: MAC이 비슷한데(30.9 vs 29.6 M) 해상도 쪽이 **16% 느리고 peak SRAM이 1.4배**(375 vs 270 KB)다. 대신 파라미터는 3.5배 적다(118 vs 408 K). flash가 제약이면 해상도, SRAM이 제약이면 폭이 답이다.
- **width x1.5와 x2의 peak가 같다(270 KB)**: peak 블록이 stem 직후인데, stem 채널이 `make_div(16 × 0.75) = make_div(16 × 1.0) = 16`으로 반올림돼 같기 때문이다. 채널 반올림 규칙이 메모리 계산에 직접 들어간다 — LUT·메모리 모델은 실제 채널 수로 계산해야 한다.
- **depth**: MAC +51%, peak 불변(135 KB), latency +20%. SRAM이 빡빡한 MCU에서 가장 안전한 확장 축이다.
- **compound**(r 112, w 0.625, depth 일부 증가)는 width x1.5와 **MAC이 같은데**(20.1 M) peak가 183.8 vs 270 KB, latency도 약간 낮다. 축을 섞으면 한 축만 키울 때의 극단적 비용을 피할 수 있다.
- peak 계산은 skip을 무시한 근사다. 정확한 값은 예제 8의 방식으로 구한다.

---

## 9. 하드웨어별 경험칙 (반드시 타깃에서 확인할 것)

아래는 여러 가속기에서 자주 관찰되는 경향이다. **칩·컴파일러 버전마다 다르므로 "가설"로 들고 가서 LUT로 확인한다.**

| 경험칙 | 이유 | 확인 방법 |
|---|---|---|
| 채널 수를 SIMD/NPU 폭의 배수로 (8, 16, 32 …) | 남는 lane·타일이 논다 (예제 2의 64 → 65 계단) | 채널을 1씩 바꾸며 1×1 conv latency 스윕 |
| NPU가 지원하는 op만 쓴다 | 미지원 op는 CPU fallback → 데이터가 NPU와 CPU를 오가며 수십 배 느려질 수 있다 | 벤더 컴파일러 로그에서 partition·delegate 결과 확인 (C6, F 모듈) |
| 활성화는 ReLU/ReLU6 우선, 필요하면 hard-swish | swish·GELU는 LUT·다항 근사가 필요하거나 미지원일 수 있다. ReLU6는 int8 범위 잡기 쉬움 (B2) | 활성화만 바꾼 블록의 LUT 비교 |
| SE·attention 같은 전역 동기화 op는 신중히 | feature map 전체를 읽은 뒤에야 다음으로 간다 → 파이프라인이 끊기고 patch 추론과도 안 맞는다 | 블록 단위 LUT에 SE 유무 비교 |
| depthwise는 MAC 대비 느리다 — 큰 kernel depthwise는 특히 | 낮은 arithmetic intensity (2절). kernel 7은 이 CPU에서 7배 이상 (예제 3) | kernel별 LUT |
| 레이어 수를 줄이고 레이어를 크게 | op당 오버헤드 (예제 1의 C vs D) | 같은 MAC, 다른 레이어 수 비교 |
| 레이어마다 compute와 memory의 균형 | 한 레이어가 memory-bound면 그 시간 동안 MAC 배열이 논다. roofline에서 ridge point 근처가 효율적 (D3) | 레이어별 intensity를 계산해 ridge point와 비교 |
| 앞쪽 고해상도 구간은 expand ratio를 작게, skip을 적게 | peak SRAM이 앞 블록에 몰린다 (예제 8의 블록 2 = 72×72×48). residual은 블록 입력을 끝까지 살려 둔다 | 블록별 peak 계산 (예제 8), expand 6 → 3이면 그 블록 peak가 약 절반 |
| 실행 순서·in-place를 엔진 규칙대로 | 분기 그래프는 실행 순서에 따라 동시에 살아 있는 텐서가 다르고, in-place depthwise를 지원하면 depthwise가 싸진다 (D2, 7.3) | 컴파일러가 보고하는 arena 크기와 계산기 비교 |
| 양자화 친화 구조 | per-tensor int8에서 depthwise 채널 간 range 차이가 크면 정확도 손실 (C1, C2) | 양자화 후 layer별 SQNR (C8) |

---

## 10. 설계 예 — SRAM 64 KB에 KWS 모델 넣기

면접에서 자주 나오는 형태의 문제다: "Cortex-M급 MCU, SRAM 64 KB, 12개 키워드 KWS를 설계하라." 손으로 푸는 순서:

1. **입력 정하기**: 1초 오디오, 40 ms 창·20 ms hop이면 49 프레임, 프레임당 MFCC 10개 → 입력 `49×10` (Hello Edge 논문(Zhang et al., 2017)의 KWS 설정이 이 형태다).
2. **SRAM 나누기**: 64 KB 전부가 모델 것이 아니다. 오디오 링버퍼(16 kHz × 16 bit × 1 s = 32 KB면 너무 크다 → 특징만 링버퍼로 들고 있는 스트리밍 설계로 줄인다), MFCC scratch, 스택, RTOS. 여기서는 **24 KB를 예약하고 activation에 40 KB**를 준다고 가정한다.
3. **모델 뼈대**: DS-CNN — 첫 conv 10×4 stride 2×2로 `25×5×C`, 이후 depthwise separable 블록 N개, GAP, FC.
4. **각 후보의 params(flash), MAC(latency), activation peak(SRAM)를 계산해 예산과 비교한다.**

예제 11 — DS-CNN 설계안 5개의 비용을 순수 산술로 계산한다.

```python
# KWS DS-CNN (입력 49 프레임 x 10 MFCC) 설계안별 비용 — 순수 산술, int8 가정
def dscnn(C, N, k=3):
    h, w = 25, 5                                             # 첫 conv 10x4, stride 2x2 -> 25x5 (Hello Edge DS-CNN 형태)
    params = 10 * 4 * 1 * C + C; macs = h * w * C * 10 * 4
    for _ in range(N):                                       # DS 블록 N개: dw kxk + pw CxC
        params += (k * k * C + C) + (C * C + C); macs += h * w * C * (k * k + C)
    params += C * 12 + 12; macs += C * 12                    # GAP + FC(12 클래스)
    act = [49 * 10, h * w * C]                               # 입력, 이후 모든 feature map 크기 동일
    peak = max(a + b for a, b in zip(act, act[1:] + [act[-1]]))   # ping-pong: 입력+출력 동시 상주
    return params, macs, peak
SRAM, RESERVED = 64 * 1024, 24 * 1024                        # 오디오 링버퍼·MFCC scratch·스택 등 예약(가정)
print(f"{'C':>4s} {'N':>2s} {'params':>7s} {'MMAC':>6s} {'act peak B':>10s}  fits (act <= {(SRAM - RESERVED) // 1024} KB)?")
for C, N in [(64, 4), (64, 6), (128, 4), (172, 5), (276, 5)]:
    p, m, pk = dscnn(C, N)
    print(f"{C:4d} {N:2d} {p:7d} {m / 1e6:6.2f} {pk:10d}  {'yes' if pk <= SRAM - RESERVED else 'NO'}")
```

```text
   C  N  params   MMAC act peak B  fits (act <= 40 KB)?
  64  4   22604   2.66      16000  yes
  64  6   32204   3.82      16000  yes
 128  4   77964   9.41      32000  yes
 172  5  166508  20.32      43000  NO
 276  5  410700  50.55      69000  NO
```

출력에서 볼 것:

- activation peak는 **채널 수 C에만 비례**하고(`25×5×C × 2`), 블록 수 N과는 무관하다 (8절의 "depth는 SRAM에 안전"). C = 64 → 16 KB, 128 → 32 KB, 172 → 43 KB로 예산 초과.
- 그래서 SRAM 64 KB에서의 탐색 공간은 사실상 **"C ≤ 160 정도, N은 latency 예산이 허락하는 만큼"**으로 좁혀진다. 이게 7.3의 TinyNAS가 말하는 "search space를 먼저 메모리로 자른다"의 손계산 버전이다.
- flash: C = 128, N = 4는 int8로 약 78 KB. 이것도 flash 예산과 비교한다.
- latency: MAC(9.41 M)만으로는 모른다. 타깃에서 DS 블록 하나의 LUT를 재고 (depthwise가 느린 칩이면 C를 키우고 N을 줄이는 쪽이 유리할 수 있다), 20 ms 프레임마다 돌릴지, 여러 프레임마다 돌릴지 정한다.

이어서 탐색은 6절처럼 한다: C·N·kernel·첫 conv stride를 공간으로 두고, 메모리 계산 + LUT로 걸러서, 남은 후보를 학습(또는 proxy)한다. 마지막으로 **양자화(C2)까지 한 모델을 보드에서 실측**해 LUT를 보정한다.

---

## 11. 실제 팀에서는 이렇게 굴러간다

co-design은 한 사람이 다 하는 일이 아니다. Hark 같은 기기 팀이라면(추정) 대략 이렇게 나뉜다.

| 단계 | embedded AI 엔지니어 (Don의 자리) | 모델 팀 | 산출물 |
|---|---|---|---|
| 1. 예산 | 전력·메모리·latency 예산을 숫자로 (I1). 오디오 버퍼·RTOS 몫을 빼고 모델 몫을 정한다 | 정확도 목표 (FAR/FRR 등) | 예산 표 |
| 2. 비용 모델 | 타깃에서 op·블록 LUT 측정, 메모리 계산기, 지원 op 목록 | — | LUT 파일 + `cost(model)` 함수 |
| 3. 공간 설계 | 미지원 op 제거, 채널 정렬 규칙 | 과제에 맞는 블록·입력 형태 | search space 정의 |
| 4. 탐색·학습 | 비용 함수 제공, 질문에 답하기 | 탐색·학습 실행 | 후보 몇 개 (Pareto) |
| 5. 배포·실측 | 양자화·컴파일·보드 실측, LUT 오차 보고 (C8, K 모듈) | 양자화 정확도 확인 | 실측 표 |
| 6. 피드백 | "블록 3의 depthwise 7×7이 예측보다 2배 느림, fusion 안 됨" 같은 root cause (I6) | 구조 수정 | 다음 루프 |

Don의 펌웨어 경험이 가장 직접 쓰이는 곳은 2·5·6이다. "측정 방법이 재현 가능한가(클럭 고정, 캐시 상태, 인터럽트), 예측과 실측이 어긋나면 왜인가"는 SSD 성능 튜닝에서 매일 하던 질문이다. 실무에서는 NAS를 거창하게 돌리기보다, **LUT와 메모리 계산기를 모델 팀의 학습 스크립트에 붙여서 "이 모델은 예산 안인가"를 매번 자동으로 보여 주는 것**만으로도 효과가 크다.

---

## 12. 임베디드 관점에서 다시 보기

탐색 루프가 호출하는 비용 함수는 결국 "레이어 목록을 훑으며 LUT를 더하고 peak를 구하는" 코드다. 기기 쪽 빌드 도구나 CI에 C로 넣어 두면, 새 모델이 들어올 때마다 예산 검사를 자동으로 할 수 있다.

예제 12 — 레이어 표를 훑어 peak activation과 LUT latency를 구하고 예산과 비교하는 C 코드. LUT 값(`lut_us`)은 예시 숫자다 (실제로는 타깃에서 측정한 값을 넣는다).

```c
#include <stdio.h>

typedef struct {
    const char *name;
    unsigned in_b, out_b, skip_b;   /* int8 텐서 바이트: 입력, 출력, 살아 있는 skip */
    unsigned lut_us;                /* 타깃에서 측정한 LUT 값 (여기선 예시 숫자) */
} layer_t;

static const layer_t net[] = {      /* KWS DS-CNN C=64, N=2 (49x10 MFCC -> 25x5x64) */
    {"conv10x4 s2", 490, 8000, 0, 310}, {"dw1 3x3", 8000, 8000, 0, 95},
    {"pw1 1x1", 8000, 8000, 0, 240},    {"dw2 3x3", 8000, 8000, 0, 95},
    {"pw2 1x1", 8000, 8000, 0, 240},    {"gap", 8000, 64, 0, 20}, {"fc12", 64, 12, 0, 5},
};

int main(void)
{
    const unsigned budget_b = 40u * 1024u, deadline_us = 2000u;
    unsigned peak = 0, lat = 0;
    const char *peak_at = "";
    for (size_t i = 0; i < sizeof net / sizeof net[0]; i++) {
        unsigned need = net[i].in_b + net[i].out_b + net[i].skip_b;
        if (need > peak) { peak = need; peak_at = net[i].name; }
        lat += net[i].lut_us;
    }
    printf("peak activation = %u B at '%s' (budget %u B) -> %s\n",
           peak, peak_at, budget_b, peak <= budget_b ? "OK" : "OVER");
    printf("LUT latency     = %u us (deadline %u us) -> %s\n",
           lat, deadline_us, lat <= deadline_us ? "OK" : "OVER");
    return 0;
}
```

```text
peak activation = 16000 B at 'dw1 3x3' (budget 40960 B) -> OK
LUT latency     = 1005 us (deadline 2000 us) -> OK
```

출력에서 볼 것: `cc -std=c11 -Wall -Wextra -O2 plan.c`로 경고 없이 컴파일되고, peak가 첫 conv(490 + 8,000 = 8,490 B)가 아니라 feature map끼리 만나는 `dw1`(16,000 B)에서 나온다. 예제 11의 C = 64 값과 같다.

기기 쪽에서 챙길 것:

- **LUT는 컴파일러·런타임 버전에 묶인다.** 벤더 SDK를 올리면 fusion 규칙이 바뀌어 LUT가 틀어질 수 있다. LUT 파일에 SDK 버전·클럭·메모리 배치를 같이 기록하고, CI에서 대표 모델 몇 개로 회귀 검사한다 (C8).
- **peak 계산은 엔진의 memory planner를 흉내 내야 한다.** in-place 지원, 텐서 정렬 패딩(예: 16 B 정렬), scratch 버퍼(im2col 버퍼 등)까지 넣어야 실제와 맞는다. 컴파일러가 arena 크기를 알려 주면 그 숫자를 정답으로 삼아 계산기를 검증한다.
- **에너지 LUT도 같은 방식으로 만들 수 있다.** 레이어별로 전력계(Don의 Power Analyzer 경험)로 재서 µJ 표를 만든다. 에너지는 MAC보다 **메모리 이동 바이트**에 더 민감하다 (D7).
- **patch 추론은 런타임이 지원해야 한다.** 모델만 바꿔서 되는 게 아니라, 엔진이 halo를 포함한 타일 실행과 출력 버퍼 조립을 해야 한다. 펌웨어의 타일 DMA 스케줄링과 같은 구조다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| FLOPs를 latency 목표로 탐색 | "FLOPs 30% 줄였는데 보드에서 더 느림" | depthwise·큰 kernel·작은 op가 늘어 효율 하락 (예제 1) | 타깃 LUT 또는 실측을 목표로 삼는다 |
| LUT를 개발 PC나 다른 SDK 버전에서 측정 | 타깃 실측과 순위가 뒤집힘 | 연산 효율·fusion 규칙이 다름 | 반드시 타깃·같은 컴파일러로 측정, 버전 기록 |
| LUT 합을 보정 없이 사용 | 일정 비율 또는 일정 오프셋만큼 계속 틀림 | fusion, 캐시, 그래프 오버헤드 | 전체 네트워크 10~20개로 `α·LUT + β` 보정 |
| peak memory를 "activation 합"으로 계산 | 필요 SRAM을 과대평가해 좋은 후보를 버림 | lifetime 무시 | 동시에 살아 있는 텐서만 합산 (예제 8) |
| skip·scratch 버퍼를 빼먹고 peak 계산 | 계산상 들어가는데 보드에서 arena 할당 실패 | residual 입력, im2col 버퍼, 정렬 패딩 | 엔진의 memory planner 규칙을 그대로 구현, 컴파일러 arena 크기로 검증 |
| proxy 정확도를 최종 정확도로 믿음 | 선택한 모델이 끝까지 학습하면 다른 후보보다 나쁨 | 짧은 학습·weight sharing의 순위 편향 | Pareto 상위 몇 개는 풀 학습으로 재평가 |
| random baseline 없이 NAS 결과 보고 | NAS가 좋아 보이지만 공간이 좋았을 뿐 | 공간 설계 효과와 탐색 효과 혼동 | 같은 예산의 random search와 비교 (6절) |
| search space에 미지원 op 포함 | 탐색 결과가 컴파일 시 CPU fallback | 공간을 HW로 먼저 거르지 않음 | 지원 op 목록으로 공간 필터링 |
| 해상도를 공짜 손잡이로 취급 | 파라미터는 그대로인데 SRAM 초과 | activation ∝ r² (예제 10) | 해상도 후보마다 peak 계산 |

---

## 14. 면접에서 이렇게 말한다

**Q.** Why don't FLOPs predict latency on edge hardware?

**A.** FLOPs는 일의 양이지 시간이 아니다. 시간은 연산 효율(MAC 배열을 얼마나 채우나), 메모리 이동(arithmetic intensity), op당 오버헤드, 정렬이 정한다. 같은 CPU 코어에서 MAC이 같은 레이어들의 처리율이 5~515 GMAC/s로 100배 차이 났고, depthwise separable 블록은 같은 MAC의 standard conv보다 2.6배 느렸다. 30개 네트워크에서 MAC 기반 예측은 평균 21% 틀렸고 LUT 합은 거의 정확했다.

> "FLOPs measure work, not time. Latency depends on how well an op fills the compute units, how many bytes it moves per MAC, per-op overhead, and alignment to the SIMD or NPU tile width. In my own measurements on one CPU core, layers with the same MAC count ranged from 5 to over 500 GMAC per second — depthwise convs were the slowest because their arithmetic intensity is about one MAC per byte. A depthwise-separable block was 2.6 times slower than a standard conv with the same MACs, and a MAC-based predictor was off by about 20 percent on average, while a per-layer lookup table was within a few percent."

**Q.** How would you build a latency model for an NPU?

**A.** 먼저 컴파일러가 어떤 단위로 fusion하는지 확인하고, 그 단위(보통 conv+BN+act, 또는 MBConv 블록 전체)를 key로 잡는다. search space에 나올 모든 key를 타깃 보드에서 클럭 고정·캐시 상태 통제하고 측정해 LUT를 만든다. 네트워크 latency는 합으로 예측하고, 실제 전체 네트워크 10~20개를 재서 선형 보정한다. 오차가 큰 모델은 op별 프로파일로 원인(fusion 실패, fallback, SRAM 넘침)을 찾는다. LUT는 SDK 버전·클럭과 함께 버전 관리하고 CI에서 회귀 검사한다.

> "I'd start from how the vendor compiler fuses ops, and use the fused kernel — say conv plus activation, or a whole inverted-residual block — as the lookup key. I'd enumerate every key the search space can produce, compile each one and time it on the target with fixed clocks and controlled cache state, and store the table together with the SDK version. Network latency is predicted as the sum, then calibrated with a linear fit against ten to twenty full networks measured end to end. When a model is way off, I profile per op to find the cause — a fusion that didn't happen, a CPU fallback, or activations spilling out of SRAM."

**Q.** What did MCUNet optimize for?

**A.** latency보다 **peak SRAM과 flash**를 제약으로 삼았다. MCU에서는 activation이 SRAM에 안 들어가면 아예 못 돌기 때문이다. TinyNAS는 먼저 메모리 제약 아래에서 해상도·width 조합별 search space를 평가해 좋은 공간을 고르고, 그 안에서 one-shot NAS를 한다. TinyEngine은 코드 생성, 메모리 스케줄링, in-place depthwise로 엔진 쪽 메모리를 줄였다. 모델과 엔진을 같이 설계했다는 게 핵심이다. 후속 MCUNetV2는 앞쪽 고해상도 stage를 patch 단위로 실행해 peak를 더 줄였다.

> "MCUNet treated peak SRAM and flash as the primary constraints, because on a microcontroller a model whose activations don't fit simply doesn't run. TinyNAS first picks the search space — input resolution and width — by looking at how much compute the memory-feasible models in each space can have, then runs one-shot NAS inside it. TinyEngine is the matching inference engine: code generation instead of an interpreter, model-specific memory scheduling, and in-place depthwise convolution. The point is that the model and the runtime were co-designed. MCUNetV2 went further with patch-based inference for the memory-heavy early stages."

**Q.** What is Once-for-All?

**A.** 모든 후보를 품은 supernet을 **한 번** 학습하고, 기기마다 재학습 없이 subnet을 검색해 꺼내 쓰는 방법이다. kernel은 큰 kernel의 가운데(와 변환)로, expand는 앞쪽 채널로, depth는 앞쪽 블록으로 줄인다. 큰 망부터 학습하고 점점 작은 설정을 허용하는 progressive shrinking으로 subnet 간 간섭을 줄인다. 기기별로는 latency LUT와 정확도 predictor로 evolutionary search만 하면 되므로 기기가 늘어도 학습 비용이 늘지 않는다.

> "Once-for-All trains a single supernet that contains all the sub-networks — elastic kernel size, width via expand ratio, depth, and input resolution — using progressive shrinking, so the large network is trained first and smaller configurations are added gradually to limit interference. After that, specializing for a new device is just a search: use that device's latency table and an accuracy predictor, run evolutionary search, and extract the sub-network with shared weights. Training cost is paid once, and each new hardware target costs minutes instead of another training run."

**Q.** How would you co-design a keyword-spotting model with a 64 KB SRAM budget?

**A.** 먼저 64 KB에서 오디오 링버퍼·MFCC scratch·스택을 빼서 activation 몫(예: 40 KB)을 정한다. 입력 49×10 MFCC, DS-CNN이면 activation peak ≈ 2 × 25×5×C 바이트라 C는 160 이하로 제한된다. 블록 수는 SRAM에 영향이 없으니 latency 예산으로 정한다. 타깃에서 DS 블록 LUT를 재서 C·N·kernel 후보를 걸러내고, 남은 후보를 학습해 FRR/FAR로 고른다. 마지막으로 int8 양자화 후 보드에서 arena 크기와 latency를 실측해 계산과 맞는지 확인한다.

> "First I'd split the 64 KB: audio or feature ring buffer, MFCC scratch, stack and RTOS, and whatever is left — say 40 KB — is the activation arena. With a 49 by 10 MFCC input and a DS-CNN, the feature maps are 25 by 5 by C, so ping-pong buffering needs about 250 times C bytes, which caps C at around 160 channels. Depth doesn't change peak memory, so the number of blocks is set by the latency budget, using a lookup table of the depthwise-separable block measured on the target. I'd filter candidates with that memory model and the table, train the survivors, pick on false-reject and false-accept rates, then quantize to int8 and verify the real arena size and latency on the board."

---

## 15. 직접 해보기

1. (손계산) standard conv 3×3, 64 → 64, 28×28과 같은 MAC이 되는 depthwise separable 블록의 채널 수 C를 구하라 (`9C + C² = 9·64·64`).
정답: `C² + 9C − 36,864 = 0` → C ≈ 187.6. C = 188이면 `9·188 + 188² = 37,036`으로 36,864보다 약간 크다.

2. (손계산) 4절 공간에서 stage 수를 5로, depth 선택지를 {2, 3, 4}로 바꾸면 해상도·width를 빼고 네트워크 수는 몇 개인가? 블록 선택지는 여전히 6가지.
정답: stage당 `6² + 6³ + 6⁴ = 36 + 216 + 1,296 = 1,548`, 전체 `1,548⁵ ≈ 8.9 × 10¹⁵`.

3. (손계산) MobileNetV2-0.35, 입력 144에서 블록 2의 expand ratio를 6 → 3으로 줄이면 그 블록의 depthwise 순간 need는 몇 KB인가?
정답: 중간 채널 24 → `72·72·24 + 36·36·24 = 124,416 + 31,104 = 155,520 B ≈ 151.9 KB` (절반).

4. (코드) 예제 4를 바꿔 LUT key를 블록 단위가 아니라 **conv 단위**(expand, dw, project를 따로)로 측정하고 합으로 예측해 보라. 블록 단위보다 오차가 커지는가? 이유는?
힌트: conv 단위로 나누면 ReLU6·residual add 같은 사이 op와 호출 경계가 빠지거나 중복된다. fusion 단위가 key가 되어야 하는 이유(3.5절)를 직접 확인하는 과제다.

5. (코드) 예제 7의 fitness를 "reject" 대신 MnasNet soft reward(`acc × (lat/T)^−0.07`)로 바꾸고, 예산을 넘는 후보도 학습하게 해 보라. 찾은 모델의 latency 분포가 어떻게 달라지나?
힌트: soft reward는 T 근처를 "선"이 아니라 "기울기"로 본다. 예제 5에서 본 것처럼 T보다 조금 빠른 모델도 점수가 좋아진다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| HW-aware 모델 설계 | hardware-aware model design | 정확도와 함께 타깃 HW의 latency·메모리·에너지를 목표에 넣어 구조를 고르는 것 |
| NAS | Neural Architecture Search | 모델 구조를 알고리즘이 탐색하는 것 |
| search space | 탐색 공간 | 고를 수 있는 구조 손잡이(kernel, expand, depth, width, resolution)의 조합 |
| Pareto front | 파레토 전선 | 다른 어떤 후보에게도 "더 싸고 더 정확함"으로 지지 않는 후보들의 집합 |
| latency LUT | latency lookup table | 레이어(블록) 명세별 타깃 실측 latency 표. 합으로 네트워크 latency 예측 |
| arithmetic intensity | 연산 밀도 | MAC(또는 FLOP) ÷ 옮긴 바이트. 낮으면 memory-bound |
| evolutionary search | 진화 탐색 | 좋은 후보를 부모로 변이시켜 다음 후보를 만드는 탐색 |
| MnasNet reward | — | `ACC × (LAT/T)^w`. latency를 정확도에 곱해 넣는 RL reward |
| DARTS | Differentiable Architecture Search | op 선택을 softmax 가중합으로 풀어 gradient로 탐색 |
| supernet / one-shot | — | 모든 후보를 품은 큰 망 하나를 학습하고 subnet은 가중치를 공유 |
| OFA | Once-for-All | supernet을 한 번 학습해 기기마다 검색만으로 subnet을 꺼내 쓰는 방법 |
| progressive shrinking | 점진적 축소 | 큰 망 먼저, 그다음 작은 kernel·depth·width를 차례로 허용하며 학습 |
| MCUNet | — | peak SRAM·flash 제약의 TinyNAS + TinyEngine co-design |
| patch 기반 추론 | patch-based inference | 앞쪽 고해상도 stage를 공간 patch 단위로 실행해 peak memory를 줄임 (MCUNetV2) |
| halo | — | patch 경계 밖으로 더 읽어야 하는 receptive field 영역. 중복 계산의 원인 |
| peak SRAM | — | 추론 중 동시에 살아 있는 텐서 바이트의 최대값 |
| compound scaling | — | depth·width·resolution을 정해진 비율로 함께 키우는 규칙 (EfficientNet) |

---

## 17. 요약 & 체크리스트

HW-aware 모델 설계는 "예산을 숫자로 정하고 → 구조 손잡이로 search space를 만들고 → 후보마다 정확도와 **타깃 기준 비용**을 평가해 → Pareto front에서 예산 안의 최고를 고르는" 루프다. FLOPs와 파라미터 수는 비용의 나쁜 proxy다 — 같은 MAC에서 처리율이 100배 차이 날 수 있다 (depthwise, 작은 op, 채널 정렬). 대신 **타깃에서 잰 레이어(블록)별 LUT의 합**으로 latency를 예측하고, 전체 네트워크 실측으로 보정한다. 탐색은 random·evolution·RL(MnasNet reward)·DARTS·supernet(OFA)이 있고, 어느 것이든 LUT가 제약 검사와 비용 항을 공급한다. MCU에서는 latency보다 **peak SRAM**이 먼저 막히며, peak는 앞쪽 고해상도 블록에 몰린다. MCUNet은 메모리 제약으로 공간과 엔진을 함께 설계했고, MCUNetV2는 앞 stage를 patch 단위로 실행해 연산을 조금 더 쓰고 메모리를 크게 줄였다. embedded AI 엔지니어의 핵심 기여는 이 비용 모델(LUT·메모리 계산기)을 정확하게 만들고, 실측으로 검증하고, 어긋나는 원인을 찾아 모델 팀에 돌려주는 것이다.

- [ ] MAC이 같은 두 레이어가 latency가 다른 이유 네 가지(연산 효율, 대역폭, op 오버헤드, 정렬)를 예를 들어 말할 수 있다
- [ ] conv·depthwise의 arithmetic intensity를 손으로 계산할 수 있다
- [ ] search space의 레이어 명세를 열거해 latency LUT를 만들고, 합으로 예측하고, 상관·MAPE로 검증할 수 있다
- [ ] LUT 합이 타깃에서 틀어지는 원인(fusion, DMA 겹침, 캐시, SRAM 넘침)과 보정 방법을 말할 수 있다
- [ ] search space 크기를 손으로 계산할 수 있다
- [ ] random, evolutionary, RL(MnasNet reward), DARTS, OFA의 차이와 비용을 비교할 수 있다
- [ ] 정확도 proxy의 편향과, 최종 후보를 풀 학습으로 재확인해야 하는 이유를 말할 수 있다
- [ ] 블록별 peak SRAM을 계산하고 어느 블록이 병목인지 찾을 수 있다
- [ ] patch 기반 추론의 halo를 receptive field로 계산하고, 메모리와 MAC의 교환을 설명할 수 있다
- [ ] 64 KB SRAM KWS처럼 예산에서 채널 수 상한을 역산해 search space를 좁힐 수 있다

---

## 참고 자료

- Tan et al., "MnasNet: Platform-Aware Neural Architecture Search for Mobile", CVPR 2019 (arXiv:1807.11626) — 실측 latency를 넣은 RL NAS, `ACC × (LAT/T)^w` reward.
- Cai, Zhu, Han, "ProxylessNAS: Direct Neural Architecture Search on Target Task and Hardware", ICLR 2019 (arXiv:1812.00332) — latency 기대값을 미분 가능한 loss로.
- Wu et al., "FBNet: Hardware-Aware Efficient ConvNet Design via Differentiable Neural Architecture Search", CVPR 2019 (arXiv:1812.03443) — latency lookup table.
- Liu, Simonyan, Yang, "DARTS: Differentiable Architecture Search", ICLR 2019 (arXiv:1806.09055).
- Zoph & Le, "Neural Architecture Search with Reinforcement Learning", ICLR 2017.
- Real et al., "Regularized Evolution for Image Classifier Architecture Search", AAAI 2019 (AmoebaNet).
- Cai, Gan, Wang, Zhang, Han, "Once-for-All: Train One Network and Specialize it for Efficient Deployment", ICLR 2020 (arXiv:1908.09791).
- Lin et al., "MCUNet: Tiny Deep Learning on IoT Devices", NeurIPS 2020 (arXiv:2007.10319).
- Lin et al., "MCUNetV2: Memory-Efficient Patch-based Inference for Tiny Deep Learning", NeurIPS 2021 (arXiv:2110.15352).
- Tan & Le, "EfficientNet: Rethinking Model Scaling for Convolutional Neural Networks", ICML 2019 (arXiv:1905.11946) — compound scaling (B2 7.6절).
- Zhang, Suda, Lai, Chandra, "Hello Edge: Keyword Spotting on Microcontrollers", 2017 (arXiv:1711.07128) — DS-CNN KWS와 MCU 메모리 예산.
- Zhang et al., "nn-Meter: Towards Accurate Latency Prediction of Deep-Learning Model Inference on Diverse Edge Devices", MobiSys 2021 — fusion을 고려한 커널 단위 latency 예측.
- MIT 6.5940 "TinyML and Efficient Deep Learning Computing" (Song Han) — NAS, MCUNet, OFA 강의가 포함된 과목.
- PyTorch 문서: `torch.nn.Conv2d`, forward hook(`register_forward_hook`), MPS backend — pytorch.org/docs.
