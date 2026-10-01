# D2. 메모리 계산 — 가중치·활성값·peak·arena·flash/SRAM 배치

> **이 노트를 다 읽으면**: 추론 배포의 모든 바이트(가중치·코드·LUT·활성값·scratch·상태·입출력 버퍼·스택)를 flash/SRAM/TCM/DRAM 중 어디에 둘지 표로 그릴 수 있다 · 가중치 바이트(scale·zero-point·bias·정렬 오버헤드 포함)와 활성값 peak를 손으로 계산할 수 있다 · `torch.fx`로 모델을 읽어 naive 합 / peak-live / greedy arena를 내는 메모리 추정기를 직접 짜서 MobileNetV2·KWS·transformer 블록에 적용할 수 있다 · `size`·링커 맵·stack painting으로 펌웨어 쪽 실측을 하고, 기기 하나의 메모리 예산 워크시트를 채울 수 있다
> **JD 연결**: "Optimizing models for MCUs & edge processors", "strong understanding of performance characteristics … memory bandwidth" · study_prep_list D2 행 — parameter 크기 = 파라미터 수 × 바이트, activation 크기, **peak memory**, tensor lifetime, memory planner (arena 재사용), "MCU SRAM 수백 KB 안에 넣기"
> **Don 기준 난이도**: SRAM/DRAM 예산 관리, 링커 섹션, 스택 overflow 디버깅, DMA 버퍼 배치는 이미 강함(SSD 펌웨어에서 매일 한 일) / "모델이 만드는 텐서"를 버퍼로 보고 생존 기간을 세는 법, 모델 구조(residual·attention)가 peak를 바꾸는 방식, ML 도구에서 숫자를 뽑는 법은 새로 배움
> **선행 노트**: A1 (텐서 shape·NCHW), B2 (conv·depthwise), B6 3·10절 (hook으로 peak 재기), C1 5절 (per-channel scale), C6 10~11절 (greedy planner·실행 순서), C7 7절 (MCUNet patch 추론)

---

## 0. 큰 그림 — 이게 왜 필요한가

펌웨어 엔지니어에게 "이 기능 넣을 수 있어?"라는 질문의 첫 번째 답은 항상 **메모리**다. SSD 컨트롤러에서 FTL 매핑 테이블을 DRAM에 둘지 SRAM에 둘지, 커맨드 큐 버퍼를 몇 개 잡을지, 스택을 몇 KB 줄지 — 전부 "바이트가 어디에 몇 개 있고, 언제 살아 있느냐"의 문제였다. ML 모델 배포도 똑같다. 다른 점은 **버퍼를 만드는 주체가 사람이 아니라 모델 그래프**라는 것뿐이다.

Hark 같은 웨어러블(추정)을 예로 들면, always-on MCU에서 wake word 모델과 IMU 제스처 모델이 돌고, 큰 SoC에서 음성 모델·작은 LLM이 돈다. 각 층에서 질문은 세 가지다.

1. **들어가나?** 가중치는 flash(또는 DRAM)에, 활성값은 SRAM에 들어가나? — 이 노트의 계산.
2. **어디에 둘까?** 무엇이 읽기 전용이고 무엇이 읽기/쓰기인가? XIP로 flash에서 바로 읽을까, RAM에 복사할까?
3. **어떻게 확인하나?** 추정한 숫자가 실제 바이너리·런타임과 맞나? — `size`, 링커 맵, arena 사용량, stack high-water.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="d2m1" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="110" y="22" font-size="14" text-anchor="middle">Flash / ROM</text> <text x="110" y="40" font-size="12" text-anchor="middle">비휘발 · 실행 중 읽기 전용</text> <text x="340" y="22" font-size="14" text-anchor="middle">SRAM / TCM</text> <text x="340" y="40" font-size="12" text-anchor="middle">휘발 · 읽기/쓰기 · 빠름</text> <text x="570" y="22" font-size="14" text-anchor="middle">LPDDR DRAM (큰 SoC)</text> <text x="570" y="40" font-size="12" text-anchor="middle">GB급 · 읽기/쓰기 · 느리고 비쌈</text>
<rect x="20" y="50" width="180" height="26" fill="none" stroke="#888"/> <text x="110" y="68" font-size="12" text-anchor="middle">bootloader</text> <rect x="20" y="76" width="180" height="50" fill="#888" fill-opacity="0.25" stroke="#888"/> <text x="110" y="98" font-size="12" text-anchor="middle">.text — 코드</text> <text x="110" y="115" font-size="12" text-anchor="middle">RTOS · 드라이버 · 추론 커널</text> <rect x="20" y="126" width="180" height="70" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0"/> <text x="110" y="156" font-size="12" text-anchor="middle">.rodata — 모델 가중치</text> <text x="110" y="173" font-size="12" text-anchor="middle">int8 W · int32 bias · scale</text>
<rect x="20" y="196" width="180" height="26" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/> <text x="110" y="214" font-size="12" text-anchor="middle">.rodata — LUT (mel · sigmoid)</text> <rect x="20" y="222" width="180" height="26" fill="none" stroke="#888"/> <text x="110" y="240" font-size="12" text-anchor="middle">.data 초기값 (복사 원본)</text> <rect x="20" y="248" width="180" height="40" fill="none" stroke="#888" stroke-dasharray="4 3"/> <text x="110" y="272" font-size="12" text-anchor="middle">OTA 슬롯 B · 설정 · calib</text> <rect x="250" y="50" width="180" height="26" fill="none" stroke="#888"/>
<text x="340" y="68" font-size="12" text-anchor="middle">.data (부팅 때 복사됨)</text> <rect x="250" y="76" width="180" height="80" fill="#e08a3c" fill-opacity="0.45" stroke="#e08a3c"/> <text x="340" y="108" font-size="12" text-anchor="middle">.bss — tensor arena</text> <text x="340" y="126" font-size="12" text-anchor="middle">활성값 + scratch + 메타데이터</text> <rect x="250" y="156" width="180" height="30" fill="#888" fill-opacity="0.25" stroke="#888"/> <text x="340" y="176" font-size="12" text-anchor="middle">heap · RTOS 객체 · BLE 스택</text> <rect x="250" y="186" width="180" height="30" fill="#888" fill-opacity="0.25" stroke="#888"/>
<text x="340" y="206" font-size="12" text-anchor="middle">task 스택들</text> <rect x="250" y="216" width="180" height="36" fill="#3f9a6b" fill-opacity="0.4" stroke="#3f9a6b"/> <text x="340" y="239" font-size="12" text-anchor="middle">DMA 링버퍼 (마이크 · IMU)</text> <rect x="250" y="252" width="180" height="36" fill="#3f9a6b" fill-opacity="0.4" stroke="#3f9a6b"/> <text x="340" y="275" font-size="12" text-anchor="middle">스트리밍 상태 (RNN h · conv 링)</text> <rect x="480" y="50" width="180" height="70" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0"/> <text x="570" y="80" font-size="12" text-anchor="middle">큰 모델 가중치</text>
<text x="570" y="97" font-size="12" text-anchor="middle">(파일 → mmap · page cache)</text> <rect x="480" y="120" width="180" height="50" fill="#e08a3c" fill-opacity="0.45" stroke="#e08a3c"/> <text x="570" y="149" font-size="12" text-anchor="middle">KV-cache · 큰 활성값</text> <rect x="480" y="170" width="180" height="50" fill="#888" fill-opacity="0.25" stroke="#888"/> <text x="570" y="199" font-size="12" text-anchor="middle">OS · 앱 · 버퍼 풀</text> <rect x="480" y="300" width="180" height="40" fill="none" stroke="#e08a3c" stroke-dasharray="4 3"/> <text x="570" y="318" font-size="12" text-anchor="middle">NPU 로컬 SRAM / VTCM</text>
<text x="570" y="333" font-size="12" text-anchor="middle">tile 단위로 올려 계산</text> <line x1="570" y1="220" x2="570" y2="298" stroke="currentColor" stroke-width="1.5" marker-end="url(#d2m1)"/> <text x="578" y="262" font-size="12">DMA tile</text> <rect x="250" y="318" width="180" height="36" fill="none" stroke="currentColor"/> <text x="340" y="341" font-size="12" text-anchor="middle">MCU CPU / DSP</text> <line x1="340" y1="288" x2="340" y2="316" stroke="currentColor" stroke-width="1.5" marker-end="url(#d2m1)"/> <polyline points="110,288 110,336 248,336" fill="none" stroke="#4a7bd0" stroke-width="1.5" marker-end="url(#d2m1)"/>
<text x="118" y="310" font-size="12">XIP: flash에서 바로 읽기</text> <polyline points="200,235 225,235 225,63 248,63" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="3 3" marker-end="url(#d2m1)"/> <text x="220" y="150" font-size="12" text-anchor="middle" transform="rotate(-90 220 150)">crt0 복사</text> <text x="20" y="385" font-size="12">파랑: 가중치(읽기 전용) · 주황: 활성값·KV(읽기/쓰기) · 초록: 입출력·상태 버퍼 · 회색: 시스템 · 점선: 선택</text>
</svg>
```

그림 1 — 추론 배포 하나의 메모리 지도. 왼쪽 두 기둥이 always-on MCU, 오른쪽이 큰 SoC다. 가중치는 **읽기만** 하니 flash(`.rodata`)나 DRAM에 두고, 활성값은 **매 추론마다 쓰니** SRAM의 arena(`.bss`)에 둔다. 이 한 문장이 배치의 기본 원칙이다.

이 노트의 흐름:

```
 1. 메모리 계층과 "무엇이 어디에"            → 표 한 장으로 정리
 2. 가중치 바이트 (flash)                    → params × bytes + 오버헤드
 3. 활성값 크기·생존 기간·peak (SRAM)         → 이 노트의 핵심
 4. scratch 버퍼 (im2col, softmax 임시)
 5. 추정기 만들기 (torch.fx) → 세 모델에 적용
 6. 실측: 호스트 Python / 펌웨어 size·맵·stack painting
 7. 스트리밍 상태 메모리
 8. 예산 워크시트 (KWS + IMU, 512 KB MCU)
 9. peak 줄이는 기술
```

이미 다른 노트에서 다룬 것은 **다시 가르치지 않고** 번호만 가리킨다: hook으로 layer별 peak 재기(B6 10절), LLM 가중치·KV 메모리(B8 4절, 깊이는 D5), scale 저장 오버헤드의 이론(C1 5절), greedy arena planner와 실행 순서가 peak를 바꾸는 이유(C6 10~11절), MCUNet patch 기반 추론(C7 7절). D2의 일은 이것들을 **하나의 체계적인 메모리 모델**로 묶고, 추정 → 배치 → 실측의 전 과정을 손에 익히는 것이다.

---

## 1. 메모리 계층과 배포의 메모리 지도

### 1.1 계층별 대략적인 크기

아래 숫자는 **전형적인 범위**다. 칩마다 크게 다르니 실제 설계에서는 데이터시트를 본다.

| 계층 | 전형적인 크기 (typical) | 접근 특성 | 모델에서 주로 담는 것 |
|---|---|---|---|
| 레지스터 · 누산기 | 수십~수백 바이트 | 1 cycle | MAC 누산값 (int32) |
| TCM (ITCM/DTCM, Cortex-M7/M55 등) | 수십 KB ~ 수백 KB | 0 wait state, 결정적 | 핫한 커널 코드, 작은 arena, 스택 |
| on-chip SRAM (MCU) | 256 KB ~ 2 MB | 버스 경유, 수 cycle | tensor arena, DMA 버퍼, heap |
| 내장 flash (MCU) | 1 ~ 4 MB | 읽기 wait state, 캐시/가속기 있음, 쓰기 매우 느림 | 코드, 가중치, LUT |
| 외장 QSPI/OSPI flash (XIP 가능) | 수 MB ~ 수십 MB | 내장보다 느림, 캐시 의존 | 큰 가중치, 리소스 |
| NPU 로컬 SRAM / DSP VTCM | 수백 KB ~ 수 MB (세대·벤더별) | NPU 전용, 매우 빠름 | 한 번에 계산할 tile |
| LPDDR DRAM (웨어러블·폰 SoC) | 수 GB | 수십~100 ns 지연, 에너지 큼 | 큰 모델 가중치, KV-cache, OS |
| UFS/eMMC 저장소 | 수십 GB | ms급 | 모델 파일 (부팅 후 DRAM에 mmap) |

말로 하면: 위로 갈수록 빠르고 작고 비싸다. SSD 컨트롤러에서 "SRAM에는 핫한 매핑만, 나머지는 DRAM, 전체는 NAND"로 나눈 것과 **완전히 같은 구조**다. 모델 배포는 이 계층에 텐서를 나눠 싣는 일이다.

### 1.2 무엇이 어디에 사는가 — 읽기 전용 vs 읽기/쓰기

| 항목 | 성격 | 크기를 정하는 식 | MCU 배치 | SoC 배치 |
|---|---|---|---|---|
| 가중치 (W, bias, scale, zp) | 읽기 전용, 추론 내내 필요 | Σ params × bytes + 오버헤드 (2절) | flash `.rodata` (XIP) 또는 SRAM 복사 | DRAM (mmap) → NPU SRAM tile |
| 코드 (커널·런타임) | 읽기 전용 | 링커 맵의 `.text` | flash (핫 루프는 ITCM) | DRAM |
| LUT (mel filterbank, sigmoid 표) | 읽기 전용 | 표 크기 | flash `.rodata` | DRAM |
| 활성값 (중간 텐서) | 읽기/쓰기, 레이어마다 생기고 죽음 | peak-live (3절) | SRAM arena (`.bss`) | DRAM 또는 NPU SRAM |
| scratch (im2col 등) | 읽기/쓰기, op 하나 동안만 | 커널별 (4절) | arena 안 | 런타임 workspace |
| 스트리밍 상태 (RNN h, conv 링) | 읽기/쓰기, **추론 사이에도 유지** | 7절 | SRAM (arena 밖!) | DRAM |
| 입력·출력 버퍼, DMA 링 | 읽기/쓰기 | 샘플레이트 × 창 길이 | SRAM (DMA 가능 영역) | DRAM |
| 스택 · heap | 읽기/쓰기 | high-water (6.4절) | SRAM / DTCM | DRAM |

핵심 구분 두 개:

- **읽기 전용 vs 읽기/쓰기**: 읽기 전용은 flash에 둘 수 있다(비휘발, 싸고 크다). 읽기/쓰기는 반드시 RAM이다. flash는 추론 중에 쓸 수 없다(쓰기는 섹터 erase가 필요하고 ms급이다 — Don이 NAND에서 겪은 그 문제).
- **추론 한 번 안에서만 사는가 vs 추론 사이에도 사는가**: 활성값·scratch는 추론이 끝나면 버려도 되므로 arena에서 서로 자리를 재사용한다. 스트리밍 상태는 **다음 추론까지 보존**해야 하므로 arena의 재사용 대상이 되면 안 된다. 이걸 섞으면 "가끔 인식률이 이상한" 버그가 난다(11절).

### 1.3 XIP vs RAM 복사 — 가중치를 어디서 읽을까

XIP(execute-in-place)는 flash가 CPU 주소 공간에 매핑되어 있어서, 복사 없이 포인터로 바로 읽는 방식이다. `const int8_t weights[]`를 선언하면 기본으로 이렇게 된다.

| 기준 | XIP (flash에서 직접) | 부팅 때 SRAM/TCM으로 복사 |
|---|---|---|
| SRAM 비용 | 0 | 가중치 전체 바이트 |
| 읽기 지연 | flash wait state + 캐시 미스 시 수~수십 cycle (클럭이 높을수록 wait state 증가) | SRAM/TCM 속도, 결정적 |
| 대역폭 | flash 인터페이스 폭·캐시에 좌우, 외장 QSPI면 더 낮음 | 높음 |
| 전력 | flash 읽기 에너지가 SRAM보다 큰 편 (칩마다 다름) | 부팅 복사 1회 비용, 이후 SRAM 읽기 |
| 결정성 | 캐시 상태에 따라 latency jitter | 좋음 |
| 언제 쓰나 | 가중치가 SRAM보다 크다, 대부분의 MCU 기본 | 작은 핫 레이어, 가중치 재사용이 많은 conv, 실시간 deadline이 빡빡할 때 |

중간 해법도 있다: **핫한 레이어만** 링커 스크립트로 SRAM 섹션에 놓거나, 레이어 실행 직전에 다음 레이어 가중치를 DMA로 SRAM에 prefetch 하는 double buffering(펌웨어의 ping-pong 버퍼 그대로). depthwise처럼 가중치 재사용이 적은 op는 flash 읽기 비용이 상대적으로 크게 보인다(D3 roofline의 memory-bound 쪽).

### 1.4 큰 SoC에서는 계층이 하나 더 있다

Qualcomm 같은 SoC(추정)에서 NPU/DSP는 DRAM에 있는 가중치와 활성값을 **자기 로컬 SRAM(Hexagon이라면 VTCM)에 tile 단위로 DMA** 해 와서 계산한다. 그래서 SoC의 메모리 질문은 둘로 갈린다.

- **용량**: 모델 전체(가중치 + KV + 활성값)가 DRAM 예산 안에 들어가나 — 앱이 쓸 수 있는 DRAM은 OS·다른 앱과 나눠 쓴다.
- **대역폭**: 매 추론에 DRAM에서 몇 바이트를 옮기나 — latency와 에너지를 정한다(D3, D5, D7).

이 노트는 주로 용량(바이트 수와 배치)을 다룬다. 대역폭 쪽은 D3·D5가 이어받는다.

---

## 2. 가중치 메모리 — params × bytes, 그리고 숨은 오버헤드

### 2.1 정의

```
 weight_bytes = Σ_layers ( params_layer × bytes_per_param )  +  양자화 메타데이터  +  정렬 padding
```

말로 하면: 파라미터 개수에 원소 크기를 곱한 것이 기본이고, 양자화 모델은 채널마다 붙는 부가 정보와 정렬 때문에 그보다 조금 더 크다.

bytes_per_param: fp32 = 4, fp16/bf16 = 2, int8 = 1, int4 = 0.5 (두 개를 한 바이트에 pack).

### 2.2 손계산 — conv 하나

3×3 conv, 입력 16채널 → 출력 32채널. BN은 conv에 fold 되었다고 본다(B1) → 출력 채널마다 bias가 하나 생긴다.

```
 weight 원소 = 32 × 16 × 3 × 3 = 4,608

 fp32:  W 4,608 × 4 = 18,432 B   + bias 32 × 4 = 128 B                   = 18,560 B
 int8 per-channel (C1 5절):
        W 4,608 × 1 =  4,608 B
        bias int32  32 × 4 = 128 B     (누산기가 int32라 bias도 int32)
        scale fp32  32 × 4 = 128 B     (채널마다 scale 하나)
        zero-point  32 × 4 = 128 B     (가정: int32 저장. 대칭 양자화면 0이라 생략하는 형식도 있다)
                                                                           =  4,992 B
 비율 18,560 / 4,992 = 3.72배 (4배가 아니다)
```

이번엔 **depthwise** 3×3, 32채널:

```
 weight 원소 = 32 × 1 × 3 × 3 = 288
 int8:  W 288 B + bias 128 + scale 128 + zp 128 = 672 B    ← 메타데이터(384 B)가 가중치(288 B)보다 크다!
 fp32:  288 × 4 + 128 = 1,280 B                              비율 1.9배
```

말로 하면: 채널당 가중치가 9개뿐인 depthwise는 채널당 12바이트의 메타데이터가 가중치보다 커서, int8로 바꿔도 4배가 줄지 않는다. MobileNet류가 int8에서 "생각보다 덜 줄어드는" 이유다.

### 2.3 코드로 확인 — MobileNetV2-0.35의 flash 크기 (예제 1)

무엇을 확인하는 코드인지: 모든 conv/linear를 돌며 fp32와 int8(per-channel) flash 바이트를 세고, 16바이트 정렬 padding까지 포함한다. 분류기 크기(1000 클래스 vs 2 클래스)가 얼마나 차이 나는지도 본다. 이 함수는 8절에서도 import 하므로 `ex1_weights.py`로 저장한다.

```python
import torch, torchvision
def weight_bytes(model, align=16):
    """BN을 conv에 fold했다고 보고, conv/linear마다 flash에 놓일 바이트를 센다."""
    pad = lambda n: (n + align - 1) // align * align
    fp32 = i8 = raw_w = n_layers = 0
    for m in model.modules():
        if isinstance(m, (torch.nn.Conv1d, torch.nn.Conv2d, torch.nn.Linear)):
            w, oc = m.weight.numel(), m.weight.shape[0]
            fp32 += pad(w * 4) + pad(oc * 4)                 # fp32 weight + fp32 bias(BN fold)
            i8 += pad(w) + pad(oc * 4) + pad(oc * 4) * 2     # int8 w + int32 bias + fp32 scale + int32 zp
            raw_w += w; n_layers += 1
    return n_layers, raw_w, fp32, i8
if __name__ == "__main__":
    for nc in (1000, 2):
        m = torchvision.models.mobilenet_v2(width_mult=0.35, num_classes=nc)
        total = sum(p.numel() for p in m.parameters())
        n, w, f, q = weight_bytes(m)
        print(f"classes={nc:4d}: params={total:,} (conv/fc weight {w:,}, layers {n})")
        print(f"   fp32 flash = {f/1024:7.1f} KB   int8 flash = {q/1024:6.1f} KB   ratio = {f/q:.2f}x")
        print(f"   int8 순수 weight = {w/1024:6.1f} KB -> 오버헤드(bias·scale·zp·pad) = {(q-w)/1024:.1f} KB ({(q-w)/q:.1%})")
```

```text
classes=1000: params=1,677,128 (conv/fc weight 1,662,048, layers 53)
   fp32 flash =  6523.8 KB   int8 flash = 1717.3 KB   ratio = 3.80x
   int8 순수 weight = 1623.1 KB -> 오버헤드(bias·scale·zp·pad) = 94.2 KB (5.5%)
classes=   2: params=398,690 (conv/fc weight 384,608, layers 53)
   fp32 flash =  1529.9 KB   int8 flash =  458.1 KB   ratio = 3.34x
   int8 순수 weight =  375.6 KB -> 오버헤드(bias·scale·zp·pad) = 82.5 KB (18.0%)
```

출력에서 볼 것:

- 1000 클래스일 때 파라미터 168만 개 중 **128만 개(1280 × 1000)가 마지막 FC 하나**다. torchvision의 MobileNetV2는 width를 줄여도 마지막 채널을 1280으로 유지한다. "0.35배 모델"이라고 가볍다고 믿으면 안 된다. 분류기를 2 클래스(예: 사람 있음/없음)로 바꾸면 int8 flash가 1.7 MB → 458 KB로 떨어진다.
- 작은 모델에서는 메타데이터 오버헤드가 **18 %**까지 올라간다. 2.2절의 depthwise 손계산이 53개 레이어에 누적된 결과다.
- BN 파라미터는 fold 되어 사라지므로 `params` 합(398,690)과 conv/fc weight 합(384,608)의 차이는 BN의 γ·β와 FC bias다. 배포 바이트를 셀 때는 **배포 그래프 기준**으로 센다.

### 2.4 flash footprint vs RAM footprint

같은 가중치라도 배치에 따라 RAM 비용이 다르다.

| 배치 | flash | SRAM/DRAM |
|---|---|---|
| MCU, XIP | weight_bytes | 0 (캐시만 사용) |
| MCU, 부팅 시 SRAM 복사 | weight_bytes | weight_bytes |
| MCU, 레이어별 DMA prefetch (double buffer) | weight_bytes | 2 × (가장 큰 레이어의 weight) |
| SoC, 파일 mmap | 저장소에 파일 | 실제로 읽힌 page만 RSS에 잡힘 (page cache) |
| NPU 런타임이 자기 포맷으로 재배치 | 저장소에 원본 + 변환본 | 변환본 (layout 변경·padding으로 원본보다 클 수 있음) |

마지막 행이 실무 함정이다: NPU 컴파일러는 가중치를 자기 layout(채널 정렬, 블록 형식)으로 **다시 포장**하면서 padding을 넣는다(C6 7.5절). "모델 파일이 5 MB인데 DRAM은 7 MB 먹는다"는 보고가 흔한 이유다.

### 2.5 함정

- **파라미터 수 × 1 = int8 크기**라고 답하면 틀린다: scale·zp·int32 bias·정렬·런타임 메타데이터(텐서 이름, shape, flatbuffer 구조)가 붙는다. 작은 모델일수록 비율이 크다.
- **weight sharing / tied embedding**: LLM에서 입력 embedding과 출력 head가 같은 행렬을 공유하면 한 번만 센다(B8 3절). 파라미터를 `model.parameters()`로 세면 PyTorch는 공유 텐서를 한 번만 돌려준다 — 하지만 export 된 파일이 두 벌을 저장하는 경우도 있으니 파일 크기로 교차 확인한다.
- **int4**: 0.5 B/param이지만 group-wise scale(예: 32~128개마다 fp16 scale 하나)이 붙어 실효 4.5 비트 안팎이 된다(C3). LLM 메모리는 D5에서 자세히.

---

## 3. 활성값 — 크기, 생존 기간, peak

### 3.1 텐서 하나의 크기

```
 activation_bytes = N × C × H × W × bytes_per_element      (conv 계열)
                  = N × T × D × bytes_per_element          (sequence 계열)
```

말로 하면: 텐서의 원소 수에 원소 크기를 곱한다. 배치 N은 기기 추론에서 거의 항상 1이다.

손계산 — DS-CNN KWS(Hello Edge 논문 스타일): 입력은 1초 오디오의 MFCC 49 프레임 × 10 계수 = `1×1×49×10` = 490 원소. 첫 conv는 커널 10×4, stride 2, padding (5, 1), 출력 64채널.

```
 H_out = floor((49 + 2·5 − 10) / 2) + 1 = floor(49/2) + 1 = 25
 W_out = floor((10 + 2·1 −  4) / 2) + 1 = floor(8/2)  + 1 =  5
 출력 = 1 × 64 × 25 × 5 = 8,000 원소 → int8 8,000 B, fp32 32,000 B
```

이후 depthwise 3×3 + pointwise 1×1 블록 4개는 모양을 `64×25×5`로 유지한다.

### 3.2 코드로 확인 — layer별 활성값 표 (예제 2)

무엇을 확인하는 코드인지: forward hook으로 DS-CNN의 각 conv/pool/fc 출력 shape와 바이트를 표로 뽑는다(hook 기법 자체는 B6 3.2절). 모델 정의는 `models.py`에 두고 5절에서 재사용한다.

```python
# models.py
import torch, torch.nn as nn
def cbr(i, o, k, s=1, g=1, p=None):
    return nn.Sequential(nn.Conv2d(i, o, k, s, k // 2 if p is None else p, groups=g, bias=False),
                         nn.BatchNorm2d(o), nn.ReLU())
class DSCNN(nn.Module):                       # Hello Edge 스타일 KWS: 49 프레임 × 10 MFCC
    def __init__(s, c=64, n=12):
        super().__init__()
        s.stem = cbr(1, c, (10, 4), 2, p=(5, 1))
        s.blocks = nn.Sequential(*[nn.Sequential(cbr(c, c, 3, g=c), cbr(c, c, 1)) for _ in range(4)])
        s.pool, s.fc = nn.AdaptiveAvgPool2d(1), nn.Linear(c, n)
    def forward(s, x): return s.fc(torch.flatten(s.pool(s.blocks(s.stem(x))), 1))
class TBlock(nn.Module):                      # pre-LN transformer block 하나 (d=64, 4 heads)
    def __init__(s, d=64, h=4, T=64):
        super().__init__(); s.h, s.T, s.d = h, T, d
        s.ln1, s.ln2 = nn.LayerNorm(d), nn.LayerNorm(d)
        s.q, s.k, s.v, s.o = (nn.Linear(d, d) for _ in range(4))
        s.f1, s.f2, s.act = nn.Linear(d, 4 * d), nn.Linear(4 * d, d), nn.GELU()
    def forward(s, x):
        h, T, dh = s.h, s.T, s.d // s.h
        y = s.ln1(x)
        q = s.q(y).view(1, T, h, dh).transpose(1, 2)
        k = s.k(y).view(1, T, h, dh).transpose(1, 2)
        v = s.v(y).view(1, T, h, dh).transpose(1, 2)
        a = (q @ k.transpose(-2, -1) * 0.25).softmax(-1)      # (1, h, T, T) ← T²
        z = (a @ v).transpose(1, 2).reshape(1, T, s.d)
        x = x + s.o(z)
        return x + s.f2(s.act(s.f1(s.ln2(x))))
```

```python
# ex2_acts.py
import torch
from models import DSCNN
torch.manual_seed(0)
m, rows = DSCNN().eval(), []
def hook(mod, inp, out):
    rows.append((mod._n, tuple(out.shape), out.numel()))
for n, mod in m.named_modules():
    if isinstance(mod, (torch.nn.Conv2d, torch.nn.Linear, torch.nn.AdaptiveAvgPool2d)):
        mod._n = n; mod.register_forward_hook(hook)
x = torch.randn(1, 1, 49, 10)                      # 49 프레임 × 10 MFCC (1 s 오디오)
m(x)
print(f"{'layer':<18}{'shape (N,C,H,W)':<18}{'elems':>7}{'int8 B':>8}{'fp32 B':>8}")
print(f"{'input':<18}{str(tuple(x.shape)):<18}{x.numel():>7}{x.numel():>8}{x.numel()*4:>8}")
for n, s, e in rows:
    print(f"{n:<18}{str(s):<18}{e:>7}{e:>8}{e*4:>8}")
print("모든 활성값 합(int8) =", x.numel() + sum(e for _, _, e in rows), "B")
```

```text
layer             shape (N,C,H,W)     elems  int8 B  fp32 B
input             (1, 1, 49, 10)        490     490    1960
stem.0            (1, 64, 25, 5)       8000    8000   32000
blocks.0.0.0      (1, 64, 25, 5)       8000    8000   32000
blocks.0.1.0      (1, 64, 25, 5)       8000    8000   32000
blocks.1.0.0      (1, 64, 25, 5)       8000    8000   32000
blocks.1.1.0      (1, 64, 25, 5)       8000    8000   32000
blocks.2.0.0      (1, 64, 25, 5)       8000    8000   32000
blocks.2.1.0      (1, 64, 25, 5)       8000    8000   32000
blocks.3.0.0      (1, 64, 25, 5)       8000    8000   32000
blocks.3.1.0      (1, 64, 25, 5)       8000    8000   32000
pool              (1, 64, 1, 1)          64      64     256
fc                (1, 12)                12      12      48
모든 활성값 합(int8) = 72566 B
```

출력에서 볼 것: 손계산한 `64×25×5 = 8,000`이 그대로 나온다. 활성값을 **전부 따로** 잡으면 71 KB지만, 실제로 동시에 필요한 것은 훨씬 적다 — 그게 다음 절이다. BN·ReLU 출력은 표에 없다: 배포 그래프에서는 conv에 fuse 되어 별도 버퍼가 없기 때문이다(C6 4절).

### 3.3 생존 기간(liveness)과 peak

텐서의 **lifetime**은 `[만들어지는 step, 마지막으로 읽히는 step]`이다. 그 구간 밖에서는 메모리를 돌려줘도 된다.

```
 live(t)  = Σ { size(x) : first(x) ≤ t ≤ last(x) }       ← step t에 살아 있는 텐서 바이트 합
 peak     = max_t live(t)
 naive    = Σ_x size(x)                                  ← 텐서마다 전용 버퍼
 arena    ≥ peak                                         ← 어떤 배치든 peak보다 작을 수 없다
```

말로 하면: 매 순간 살아 있는 텐서들의 합을 시간축으로 그리면 곡선이 나오고, 그 곡선의 꼭대기가 peak다. arena는 이 꼭대기 이상이어야 하고, 좋은 planner는 꼭대기에 딱 맞춘다(C6 10절).

직선형(chain) 모델에서 레이어 하나가 실행되는 순간 살아 있는 것은 **입력과 출력 둘**뿐이다. 그래서 chain의 peak는 "가장 큰 이웃 텐서 쌍의 합"이다. DS-CNN이면 `8,000 + 8,000 = 16,000 B` — 모든 텐서 합 72,566 B의 22 %다. Don에게 익숙한 말로: **ping-pong 버퍼 두 개로 파이프라인 전체를 돌리는 것**과 같다.

### 3.4 residual이 peak를 올리는 이유 (예제 3)

residual(skip) 연결 `y = F(x) + x`는 x를 블록 **끝까지** 들고 있어야 한다. 블록 안에서 채널을 크게 펼치는(expand) 순간에도 x가 살아 있으니, 그만큼 peak에 얹힌다.

무엇을 확인하는 코드인지: 같은 세 op(expand → depthwise → project)를 skip 없이/있게 실행할 때 step별 살아 있는 텐서와 합을 손계산 크기로 출력한다.

```python
K = 1024
def live_table(ops, sizes):
    """ops: [(op, [입력], 출력)]. 각 step에서 살아 있는 텐서와 바이트 합을 출력한다."""
    last = {}
    for s, (_, ins, _) in enumerate(ops):
        for t in ins: last[t] = s                       # 마지막으로 읽히는 step
    born, peak = {"x0": -1}, 0
    for s, (op, ins, out) in enumerate(ops):
        born[out] = s
        alive = [t for t in born if born[t] <= s and last.get(t, len(ops)) >= s]
        tot = sum(sizes[t] for t in alive); peak = max(peak, tot)
        print(f"  step {s} {op:<8} live={'+'.join(alive):<16} {tot//K:3d} KB")
    return peak
sizes = {"x0": 8*K, "e": 32*K, "d": 32*K, "p": 8*K, "y": 8*K}
plain = [("expand", ["x0"], "e"), ("dw", ["e"], "d"), ("project", ["d"], "p")]
resid = plain + [("add", ["p", "x0"], "y")]
print("plain (skip 없음):");      print("  peak =", live_table(plain, sizes)//K, "KB")
print("residual (y = p + x0):");  print("  peak =", live_table(resid, sizes)//K, "KB")
```

```text
plain (skip 없음):
  step 0 expand   live=x0+e              40 KB
  step 1 dw       live=e+d               64 KB
  step 2 project  live=d+p               40 KB
  peak = 64 KB
residual (y = p + x0):
  step 0 expand   live=x0+e              40 KB
  step 1 dw       live=x0+e+d            72 KB
  step 2 project  live=x0+d+p            48 KB
  step 3 add      live=x0+p+y            24 KB
  peak = 72 KB
```

```svg
<svg viewBox="0 0 680 220" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">plain — x0는 expand 뒤 바로 해제</text> <text x="360" y="22" font-size="13">residual — x0가 add(step 3)까지 산다</text> <text x="64" y="59" font-size="12" text-anchor="end">x0 8K</text> <text x="64" y="84" font-size="12" text-anchor="end">e 32K</text> <text x="64" y="109" font-size="12" text-anchor="end">d 32K</text> <text x="64" y="134" font-size="12" text-anchor="end">p 8K</text> <rect x="70" y="45" width="66" height="20" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/> <rect x="70" y="70" width="136" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<rect x="140" y="95" width="136" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/> <rect x="210" y="120" width="66" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/> <text x="103" y="183" font-size="12" text-anchor="middle">step 0</text> <text x="173" y="183" font-size="12" text-anchor="middle">step 1</text> <text x="243" y="183" font-size="12" text-anchor="middle">step 2</text> <text x="103" y="203" font-size="12" text-anchor="middle">40 KB</text> <text x="173" y="203" font-size="13" text-anchor="middle" fill="#d0564a">64 KB</text> <text x="243" y="203" font-size="12" text-anchor="middle">40 KB</text>
<text x="394" y="59" font-size="12" text-anchor="end">x0 8K</text> <text x="394" y="84" font-size="12" text-anchor="end">e 32K</text> <text x="394" y="109" font-size="12" text-anchor="end">d 32K</text> <text x="394" y="134" font-size="12" text-anchor="end">p 8K</text> <text x="394" y="159" font-size="12" text-anchor="end">y 8K</text> <rect x="400" y="45" width="244" height="20" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/> <rect x="400" y="70" width="120" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/> <rect x="462" y="95" width="120" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="524" y="120" width="120" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/> <rect x="586" y="145" width="58" height="20" fill="#888" fill-opacity="0.55" stroke="#888"/> <text x="429" y="183" font-size="12" text-anchor="middle">step 0</text> <text x="491" y="183" font-size="12" text-anchor="middle">step 1</text> <text x="553" y="183" font-size="12" text-anchor="middle">step 2</text> <text x="615" y="183" font-size="12" text-anchor="middle">step 3</text> <text x="429" y="203" font-size="12" text-anchor="middle">40 KB</text> <text x="491" y="203" font-size="13" text-anchor="middle" fill="#d0564a">72 KB</text>
<text x="553" y="203" font-size="12" text-anchor="middle">48 KB</text> <text x="615" y="203" font-size="12" text-anchor="middle">24 KB</text> <line x1="330" y1="35" x2="330" y2="210" stroke="currentColor" stroke-dasharray="3 3"/>
</svg>
```

그림 2 — 예제 3의 lifetime을 막대로 그린 것(가로 = 살아 있는 step 구간). 오른쪽에서 빨간 x0 막대가 블록 끝까지 뻗어 있고, 가장 큰 두 텐서(e, d)가 겹치는 step 1에 x0가 얹혀 peak가 64 → 72 KB로 오른다. 아래 숫자는 각 step의 live 합이다.

출력에서 볼 것: skip 텐서 x0 자체는 8 KB로 작지만, **peak 순간에 살아 있기 때문에** 그대로 peak에 더해진다. 일반화하면 residual 블록의 peak는 `skip 입력 + (블록 안에서 가장 큰 이웃 쌍)`이다. 입력 해상도가 높은 초반 블록(skip 텐서도 큼)에서 특히 비싸다. C6 10.2절은 같은 효과를 MobileNetV2 블록 하나로 보였다.

### 3.5 in-place op — 입력 자리에 출력을 쓴다

원소별 op(ReLU, add, mul, 양자화 requantize)는 출력 원소 i가 입력 원소 i에만 의존한다. 그래서 **입력이 이 op에서 마지막으로 읽힌다면** 입력 버퍼에 바로 결과를 쓸 수 있다. 새 버퍼가 필요 없다.

```
 in-place 조건:   (1) op가 원소별이다 (같은 인덱스만 읽고 쓴다)
                  (2) 출력 크기 = 입력 크기 (dtype까지 같거나 작다)
                  (3) 입력의 last == 이 op (뒤에서 또 읽는 사람이 없다)
```

depthwise/일반 conv는 이웃 픽셀을 읽으므로 단순 in-place가 안 된다(출력으로 덮어쓴 칸을 이웃 계산이 다시 읽는다). residual add는 조건 (3)을 만족하는 입력 쪽에 in-place로 쓸 수 있다. softmax는 행 단위로 in-place 구현이 가능하지만 런타임마다 다르다. 5절 추정기에 `inplace` 옵션으로 넣어 효과를 잰다.

### 3.6 여기까지 정리 — 세 개의 숫자

| 숫자 | 뜻 | 쓰임 |
|---|---|---|
| naive | 텐서마다 전용 버퍼 = Σ size | 상한. "재사용 안 하면 이만큼" |
| peak-live | max_t live(t) | **하한**. 어떤 planner도 이보다 작게 못 만든다 (실행 순서가 고정일 때) |
| arena | planner가 실제로 배치한 끝 주소 | 실제로 잡아야 하는 배열 크기. peak에 단편화·정렬·메타데이터가 붙는다 |

실행 순서를 바꾸면 peak-live 자체가 바뀐다(C6 11절, 가지가 있는 그래프에서).

---

## 4. Scratch 버퍼 — 커널이 몰래 쓰는 메모리

활성값 텐서만 세면 arena가 모자라는 경우가 있다. 커널이 **계산 도중에만** 쓰는 임시 버퍼가 있기 때문이다.

### 4.1 im2col

im2col은 conv를 행렬곱(GEMM)으로 바꾸기 위해 입력 patch들을 펼쳐 행렬로 만드는 기법이다(B2). 출력 픽셀 하나당 길이 `kh·kw·C_in`인 열이 하나 생긴다.

```
 full im2col  = (kh · kw · C_in) × (H_out · W_out) × bytes          ← 출력 전체를 한 번에 펼침
 partial      = (kh · kw · C_in) × (한 번에 처리할 출력 픽셀 수) × bytes
```

말로 하면: 전부 펼치면 입력보다 커질 수 있다(3×3이면 입력의 약 9배). MCU 커널은 그래서 **출력 픽셀 몇 개분만** 펼쳐서 계산하고 버린다.

MCU용 커널 라이브러리(CMSIS-NN이 대표적)는 이런 "부분 im2col" 방식을 쓰고, 커널마다 **필요한 scratch 크기를 알려 주는 함수**를 따로 둔다(CMSIS-NN이라면 `..._get_buffer_size()` 계열). 크기는 커널 종류·입력 채널·커널 크기·그리고 CPU 확장(DSP/MVE 유무)에 따라 달라지므로 **정확한 값은 사용하는 라이브러리 버전의 함수가 알려 주는 값**을 쓴다. 아래 예제의 "2열 int16"은 일반적인 모양을 보여 주는 가정이다(int8 입력을 int16으로 확장해 SIMD MAC에 먹이는 방식).

손계산 — MobileNetV2-0.35의 첫 conv(3×3, 3 → 16채널, stride 2, 96×96 → 48×48):

```
 열 길이 = 3·3·3 = 27
 full im2col = 27 × 48·48 = 27 × 2,304 = 62,208 B (int8)   ← 출력 활성값 36,864 B보다 크다
 2열 int16  = 2 × 27 × 2 = 108 B
```

예제 4 — 무엇을 확인하는 코드인지: 두 모델의 im2col이 필요한 conv(1×1과 depthwise 제외)마다 full/partial scratch를 계산하고, softmax 임시 버퍼도 계산한다.

```python
import torch, torchvision
from models import DSCNN
def conv_scratch(model, x):
    rows = []
    def hook(m, inp, out):
        kh, kw = m.kernel_size; cin = m.in_channels; ho, wo = out.shape[2:]
        if m.groups == m.in_channels and cin > 1 or (kh, kw) == (1, 1): return  # dw·1×1은 im2col 불필요
        col = kh * kw * cin                                   # im2col 한 열의 길이
        rows.append((m._n, (kh, kw, cin), ho * wo, col * ho * wo, 2 * col * 2, out.numel()))
    for n, m in model.named_modules():
        if isinstance(m, torch.nn.Conv2d): m._n = n; m.register_forward_hook(hook)
    model(x)
    for n, k, npix, full, part, act in rows:
        print(f"  {n:<12} k·k·Cin={k}  full im2col(int8)={full:>7,d} B  "
              f"2-col int16={part:>5,d} B  (출력 활성값 {act:,d} B)")
print("DS-CNN:"); conv_scratch(DSCNN().eval(), torch.randn(1, 1, 49, 10))
print("MobileNetV2-0.35@96:")
conv_scratch(torchvision.models.mobilenet_v2(width_mult=0.35, num_classes=2).eval(), torch.randn(1, 3, 96, 96))
T, h = 64, 4
print(f"softmax: 행 하나 fp32 임시 = {T*4} B, score 전체 fp32 = {h*T*T*4:,d} B, int8 = {h*T*T:,d} B")
```

```text
DS-CNN:
  stem.0       k·k·Cin=(10, 4, 1)  full im2col(int8)=  5,000 B  2-col int16=  160 B  (출력 활성값 8,000 B)
MobileNetV2-0.35@96:
  features.0.0 k·k·Cin=(3, 3, 3)  full im2col(int8)= 62,208 B  2-col int16=  108 B  (출력 활성값 36,864 B)
softmax: 행 하나 fp32 임시 = 256 B, score 전체 fp32 = 65,536 B, int8 = 16,384 B
```

출력에서 볼 것:

- MobileNetV2의 첫 conv를 full im2col로 돌리면 scratch 62 KB가 그 순간 입력(27 KB)·출력(36 KB)과 **동시에** 살아 있다 → 그 step의 live가 63 KB(27,648 + 36,864 B)에서 124 KB(+ 62,208 B)로 뛴다. 이 모델의 전체 peak(135 KB, 5절)에 근접한다. 호스트용 커널(예: PyTorch CPU의 slow conv)은 이런 큰 workspace를 쓰기도 하니, **호스트 메모리 측정이 기기와 다른 이유** 중 하나다(6.1절).
- MobileNet의 나머지 conv는 전부 1×1이거나 depthwise라 im2col이 필요 없다. MobileNet이 MCU에 잘 맞는 이유 중 하나다.

### 4.2 softmax · LayerNorm의 임시 버퍼

- **softmax**: 수치 안정을 위해 행의 max를 빼고 `exp`를 계산해 합으로 나눈다(B4). int8 모델도 이 부분은 보통 더 넓은 정밀도(int32 고정소수점 또는 fp32)로 계산하므로 **행 하나 길이 × 4 B** 정도의 임시 버퍼를 쓴다. 행 단위로 처리하면 256 B(T = 64)지만, 구현이 score 행렬 전체를 fp32로 만들면 64 KB가 된다 — 같은 모델이 런타임에 따라 arena가 4배 차이 나는 지점이다.
- **LayerNorm**: 행마다 평균·분산(스칼라 2개)을 구한다. 정수 구현은 행 하나를 int32로 누산하는 임시(`d × 4` B)를 쓰기도 한다. 크기는 작지만 **arena 추정에서 빠뜨리기 쉬운** 항목이다.
- **attention score 자체**(`h × T × T`)는 scratch가 아니라 활성값이다. T²이므로 시퀀스가 두 배면 4배가 된다(D4, D5).

### 4.3 scratch도 arena 안에 산다

TFLite Micro라면 커널이 `Prepare` 단계에서 `RequestScratchBufferInArena()`로 필요한 크기를 요청하고, planner가 이것을 **그 op 동안만 사는 텐서**처럼 취급해 arena에 같이 배치한다. 개념적으로는 이렇게 모델링하면 된다:

```
 op k의 scratch s_k  →  lifetime = [k, k]인 가상 텐서
 peak = max_t ( live_activations(t) + s_t )
```

말로 하면: scratch는 "그 op 한 순간에만 사는 텐서"로 세면 3절의 식을 그대로 쓸 수 있다.

---

## 5. 메모리 추정기 만들기 — torch.fx로 모델을 읽어 peak와 arena 계산

### 5.1 설계

hook(B6)은 모듈 출력만 보인다. `x + y`, `view`, `softmax` 같은 함수 호출은 안 보이고, **누가 언제 마지막으로 읽는지**(lifetime의 끝)도 알 수 없다. 그래서 `torch.fx`로 그래프를 뽑는다. fx 그래프의 노드 순서가 곧 실행 순서이고, 각 노드의 `users`가 소비자다.

```svg
<svg viewBox="0 0 680 220" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="d2m3" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <rect x="10" y="30" width="150" height="56" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="85" y="54" font-size="12" text-anchor="middle">nn.Module</text> <text x="85" y="72" font-size="12" text-anchor="middle">+ 예시 입력 x</text> <rect x="180" y="30" width="150" height="56" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="255" y="54" font-size="12" text-anchor="middle">fx.symbolic_trace</text> <text x="255" y="72" font-size="12" text-anchor="middle">+ ShapeProp (shape)</text>
<rect x="350" y="30" width="150" height="56" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="425" y="50" font-size="12" text-anchor="middle">버퍼 규칙</text> <text x="425" y="66" font-size="12" text-anchor="middle">BN·ReLU·view = alias</text> <text x="425" y="81" font-size="12" text-anchor="middle">(옵션) in-place 병합</text> <rect x="520" y="30" width="150" height="56" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="595" y="54" font-size="12" text-anchor="middle">버퍼별 lifetime</text> <text x="595" y="72" font-size="12" text-anchor="middle">size · first · last</text> <line x1="160" y1="58" x2="178" y2="58" stroke="currentColor" stroke-width="1.5" marker-end="url(#d2m3)"/>
<line x1="330" y1="58" x2="348" y2="58" stroke="currentColor" stroke-width="1.5" marker-end="url(#d2m3)"/> <line x1="500" y1="58" x2="518" y2="58" stroke="currentColor" stroke-width="1.5" marker-end="url(#d2m3)"/> <line x1="595" y1="86" x2="595" y2="110" stroke="currentColor" stroke-width="1.5"/> <line x1="255" y1="110" x2="595" y2="110" stroke="currentColor" stroke-width="1.5"/> <line x1="255" y1="110" x2="255" y2="133" stroke="currentColor" stroke-width="1.5" marker-end="url(#d2m3)"/> <line x1="425" y1="110" x2="425" y2="133" stroke="currentColor" stroke-width="1.5" marker-end="url(#d2m3)"/> <line x1="595" y1="110" x2="595" y2="133" stroke="currentColor" stroke-width="1.5" marker-end="url(#d2m3)"/>
<rect x="180" y="135" width="150" height="50" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="255" y="156" font-size="12" text-anchor="middle">naive</text> <text x="255" y="174" font-size="12" text-anchor="middle">Σ size</text> <rect x="350" y="135" width="150" height="50" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="425" y="156" font-size="12" text-anchor="middle">peak-live (하한)</text> <text x="425" y="174" font-size="12" text-anchor="middle">max_t live(t)</text> <rect x="520" y="135" width="150" height="50" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<text x="595" y="156" font-size="12" text-anchor="middle">greedy arena</text> <text x="595" y="174" font-size="12" text-anchor="middle">배치 끝 주소</text> <text x="10" y="210" font-size="12">bpe(원소당 바이트) = 1 int8 · 2 int16 · 4 fp32 — 같은 그래프로 정밀도별 숫자를 한 번에 낸다</text>
</svg>
```

그림 3 — 추정기의 구조. 주황 단계(버퍼 규칙)가 "배포 그래프처럼 세기"의 핵심이다: PyTorch eager에서는 BN·ReLU가 별도 텐서를 만들지만, 배포 런타임에서는 conv에 fuse 되거나 view라서 새 버퍼가 없다.

버퍼 규칙(단순화한 가정 — 실제 런타임과의 차이는 5.6절):

- `BatchNorm2d`, `ReLU`, `ReLU6`, `Identity`, `Dropout` 모듈 → 앞 conv에 fuse 된다고 보고 **입력 버퍼를 그대로 쓴다**(alias).
- `flatten`, `view`, `reshape` 등 → 메모리 재해석이므로 alias.
- 그 외 op(conv, linear, matmul, add, softmax, transpose, LayerNorm, GELU…) → **새 버퍼**.
- `inplace=True`면 원소별 op(add, mul 등)가 "마지막으로 읽히는 같은 크기 입력"과 버퍼를 합친다.

### 5.2 코드 — `d2_mem.py` (예제 5)

무엇을 확인하는 코드인지: 5.1절 설계를 그대로 구현한 모듈이다. 첫 블록이 그래프 → 버퍼 lifetime, 둘째 블록이 live 곡선·greedy 배치(C6 10.3절과 같은 발상을 간결하게 다시 씀)·보고서다. 두 블록을 이어 붙여 `d2_mem.py`로 저장한다.

```python
import torch, torch.fx as fx
from torch.fx.passes.shape_prop import ShapeProp
FUSED = (torch.nn.BatchNorm2d, torch.nn.ReLU, torch.nn.ReLU6, torch.nn.Identity, torch.nn.Dropout)
VIEWS = {"flatten", "view", "reshape", "squeeze", "unsqueeze", "contiguous"}   # 새 버퍼 없음
ELEMWISE = {"add", "iadd", "mul", "relu", "gelu"}      # 원소별 op: 입력 자리에 출력을 써도 된다

def trace_tensors(model, x, bpe, inplace=False):
    """fx로 실행 순서를 얻고, 버퍼마다 {size, first, last}를 만든다. bpe = 원소당 바이트."""
    gm = fx.symbolic_trace(model); ShapeProp(gm).propagate(x)
    mods, buf_of, bufs, cands = dict(gm.named_modules()), {}, {}, []
    for i, n in enumerate(gm.graph.nodes):
        if n.op == "get_attr": continue
        ins = [buf_of[a] for a in n.all_input_nodes if a in buf_of]
        for b in ins: bufs[b]["last"] = i                       # 읽히는 순간까지 산다
        if n.op == "output": continue
        tgt = mods[n.target] if n.op == "call_module" else n.target
        name = tgt if isinstance(tgt, str) else getattr(tgt, "__name__", type(tgt).__name__)
        if ins and (isinstance(tgt, FUSED) or name in VIEWS):   # conv에 fuse되거나 view
            buf_of[n] = ins[0]; continue
        bufs[n.name] = dict(size=n.meta["tensor_meta"].shape.numel() * bpe, first=i, last=i, op=name)
        buf_of[n] = n.name
        if inplace and name in ELEMWISE and ins: cands.append((n.name, ins))
    for out, ins in cands:                                      # 입력이 바로 이 op에서 죽으면 합친다
        b = next((b for b in ins if b in bufs and bufs[b]["last"] == bufs[out]["first"]
                  and bufs[b]["size"] == bufs[out]["size"]), None)
        if b: bufs[b]["last"] = bufs[out]["last"]; del bufs[out]
    return bufs
```

```python
def live_curve(bufs):
    T = max(b["last"] for b in bufs.values()) + 1
    return [sum(b["size"] for b in bufs.values() if b["first"] <= t <= b["last"]) for t in range(T)]

def greedy_arena(bufs):
    """큰 버퍼부터, 수명이 겹치는 버퍼와 주소가 안 겹치는 가장 낮은 offset (C6 10.3과 같은 발상)"""
    placed = {}
    for k in sorted(bufs, key=lambda k: -bufs[k]["size"]):
        b, off = bufs[k], 0
        clash = sorted((placed[u], placed[u] + bufs[u]["size"]) for u in placed
                       if not (b["last"] < bufs[u]["first"] or bufs[u]["last"] < b["first"]))
        for lo, hi in clash:
            if off + b["size"] <= lo: break
            off = max(off, hi)
        placed[k] = off
    return max(placed[k] + bufs[k]["size"] for k in placed), placed

def report(tag, model, x, bpe, inplace=False):
    bufs = trace_tensors(model, x, bpe, inplace)
    naive, peak = sum(b["size"] for b in bufs.values()), max(live_curve(bufs))
    arena, _ = greedy_arena(bufs)
    print(f"{tag:<26} bufs={len(bufs):3d} naive={naive/1024:8.1f}KB peak_live={peak/1024:7.1f}KB arena={arena/1024:7.1f}KB")
    return bufs
```

(출력 없음 — 모듈이다.)

코드 읽는 법 (Don에게 익숙한 말로):

- `buf_of`는 "fx 노드 → 실제 버퍼 이름" 매핑이다. alias 노드는 앞 버퍼 이름을 물려받는다 — C에서 `int8_t *relu_out = conv_out;` 한 것과 같다.
- lifetime의 `last`는 **읽는 쪽**이 갱신한다. 그래서 alias를 통해 늦게 읽혀도 원래 버퍼의 수명이 늘어난다(residual skip이 이 경로로 잡힌다).
- 그래프 `output` 노드도 "읽는 사람"으로 취급해서 최종 출력은 끝까지 산다. 입력 placeholder도 버퍼다 — TFLM처럼 입력 텐서가 arena 안에 있는 런타임을 흉내 낸 것이다.

### 5.3 세 모델에 적용 — int8 vs fp32 vs in-place (예제 6)

무엇을 확인하는 코드인지: MobileNetV2-0.35@96(비전 wake-up 후보), DS-CNN KWS(오디오), transformer 블록 하나(d = 64, T = 64)의 naive / peak-live / arena를 fp32·int8·int8+in-place로 비교한다.

```python
import torch, torchvision
from d2_mem import report
from models import DSCNN, TBlock
torch.manual_seed(0)
cases = [("MBv2-0.35@96", torchvision.models.mobilenet_v2(width_mult=0.35, num_classes=2), (1, 3, 96, 96)),
         ("DS-CNN KWS", DSCNN(), (1, 1, 49, 10)),
         ("TBlock d64 T64", TBlock(), (1, 64, 64))]
for tag, m, shp in cases:
    x = torch.randn(*shp)
    for bpe, dt in ((4, "fp32"), (1, "int8")):
        report(f"{tag} {dt}", m.eval(), x, bpe)
    report(f"{tag} int8+inplace", m.eval(), x, 1, inplace=True)
```

```text
MBv2-0.35@96 fp32          bufs= 65 naive=  2226.0KB peak_live=  540.0KB arena=  540.0KB
MBv2-0.35@96 int8          bufs= 65 naive=   556.5KB peak_live=  135.0KB arena=  135.0KB
MBv2-0.35@96 int8+inplace  bufs= 55 naive=   541.7KB peak_live=  135.0KB arena=  135.0KB
DS-CNN KWS fp32            bufs= 12 naive=   283.5KB peak_live=   62.5KB arena=   62.5KB
DS-CNN KWS int8            bufs= 12 naive=    70.9KB peak_live=   15.6KB arena=   15.6KB
DS-CNN KWS int8+inplace    bufs= 12 naive=    70.9KB peak_live=   15.6KB arena=   15.6KB
TBlock d64 T64 fp32        bufs= 21 naive=   576.0KB peak_live=  160.0KB arena=  160.0KB
TBlock d64 T64 int8        bufs= 21 naive=   144.0KB peak_live=   40.0KB arena=   40.0KB
TBlock d64 T64 int8+inplace bufs= 18 naive=   120.0KB peak_live=   40.0KB arena=   40.0KB
```

정리하면:

| 모델 | int8 가중치 (flash) | int8 naive | int8 peak = arena | fp32 arena | naive 대비 절약 |
|---|---|---|---|---|---|
| MobileNetV2-0.35 @96, 2 클래스 | 458 KB (예제 1) | 556.5 KB | 135.0 KB | 540.0 KB | 76 % |
| DS-CNN KWS (64채널) | 28.4 KB (8절) | 70.9 KB | 15.6 KB | 62.5 KB | 78 % |
| Transformer 블록 d64·T64 | (Linear 6개 약 49 K params) | 144.0 KB | 40.0 KB | 160.0 KB | 72 % |

출력에서 볼 것:

- **arena가 peak-live와 정확히 같다.** 세 모델 모두 거의 chain이라 greedy가 하한을 찾았다. 항상 그렇지는 않다 — 같은 MobileNetV2도 width 0.5에서는 단편화로 arena가 peak보다 6.7 % 커진다(5.5절).
- **fp32 → int8은 arena를 정확히 4배** 줄인다. 모든 버퍼가 같은 bpe를 쓰는 모델이라 당연하다. 실제 int8 모델은 입력(예: int16 오디오)·softmax 내부 등 일부가 넓은 dtype이라 4배보다 조금 덜 준다.
- **in-place는 naive를 줄였지만 peak는 그대로다.** MBv2의 peak 지점은 residual이 없는 stride-2 블록이고, TBlock의 peak는 softmax 입력·출력이 동시에 사는 순간이다(5.4절). in-place가 이득인 곳과 peak가 사는 곳이 다르면 효과가 없다 — **peak를 줄이려면 peak 지점의 텐서를 공격해야 한다**.
- MobileNetV2의 **가중치(458 KB)가 arena(135 KB)보다 3배 크다.** 가중치는 flash, arena는 SRAM이니 256 KB SRAM + 1 MB flash MCU에는 들어간다. 반대로 해상도를 올리면 arena가 먼저 SRAM을 넘는다(5.5절).

### 5.4 peak 지점 분해와 live 곡선 (예제 7)

무엇을 확인하는 코드인지: peak가 생기는 fx 노드와 그 순간 살아 있는 버퍼 목록을 출력한다. "peak 135 KB"라는 숫자만으로는 무엇을 고쳐야 할지 모르기 때문이다.

```python
import torch, torchvision
from d2_mem import trace_tensors, live_curve
from models import TBlock
torch.manual_seed(0)
def at_peak(tag, m, x):
    bufs = trace_tensors(m.eval(), x, 1)                  # int8
    curve = live_curve(bufs)
    t = max(range(len(curve)), key=curve.__getitem__)
    print(f"{tag}: fx 노드 {len(curve)}개, peak {curve[t]:,d} B at node {t}")
    for k, b in bufs.items():
        if b["first"] <= t <= b["last"]:
            print(f"   live {k:<22}{b['size']:>8,d} B  life=[{b['first']:>3},{b['last']:>3}]  op={b['op']}")
    return curve
c1 = at_peak("MBv2-0.35@96", torchvision.models.mobilenet_v2(width_mult=0.35, num_classes=2),
             torch.randn(1, 3, 96, 96))
c2 = at_peak("TBlock", TBlock(), torch.randn(1, 64, 64))
print("MBv2 곡선(8노드마다 KB):", [round(v / 1024) for v in c1[::8]])
```

```text
MBv2-0.35@96: fx 노드 155개, peak 138,240 B at node 12
   live features_2_conv_0_0    110,592 B  life=[  9, 12]  op=Conv2d
   live features_2_conv_1_0     27,648 B  life=[ 12, 15]  op=Conv2d
TBlock: fx 노드 26개, peak 40,960 B at node 13
   live x                        4,096 B  life=[  0, 19]  op=x
   live transpose_2              4,096 B  life=[ 10, 15]  op=transpose
   live matmul                  16,384 B  life=[ 12, 13]  op=matmul
   live mul                     16,384 B  life=[ 13, 14]  op=mul
MBv2 곡선(8노드마다 KB): [27, 18, 4, 9, 9, 18, 16, 3, 6, 11, 6, 5, 8, 8, 3, 0, 1, 4, 3, 1]
```

손으로 확인:

```
 MBv2 features.2 = 두 번째 inverted residual (stride 2, residual 없음)
   expand 1×1: 8 → 48채널, 48×48    → 48·48·48 = 110,592 B
   depthwise 3×3 stride 2: 48×24×24 →  48·24·24 =  27,648 B
   depthwise 순간 live = 110,592 + 27,648 = 138,240 B = 135 KB      ✓ (B6 10절 hook 결과와 같은 지점)

 TBlock node 13 (score × 0.25):
   matmul(q, kᵀ) = 4 heads × 64 × 64 = 16,384 B
   mul 결과                          = 16,384 B
   v (transpose_2)                   =  4,096 B   ← 뒤에서 a @ v로 읽힐 때까지 산다
   x (블록 입력)                     =  4,096 B   ← residual: node 19(첫 번째 add)까지 산다
   합                                = 40,960 B                        ✓
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="260" x2="650" y2="260" stroke="currentColor"/> <line x1="70" y1="260" x2="70" y2="30" stroke="currentColor"/> <line x1="66" y1="260.0" x2="70" y2="260.0" stroke="currentColor"/><text x="62" y="264.0" font-size="12" text-anchor="end">0</text> <line x1="66" y1="207.4" x2="70" y2="207.4" stroke="currentColor"/><text x="62" y="211.4" font-size="12" text-anchor="end">32</text> <line x1="66" y1="154.9" x2="70" y2="154.9" stroke="currentColor"/><text x="62" y="158.9" font-size="12" text-anchor="end">64</text> <line x1="66" y1="102.3" x2="70" y2="102.3" stroke="currentColor"/><text x="62" y="106.3" font-size="12" text-anchor="end">96</text>
<line x1="66" y1="49.7" x2="70" y2="49.7" stroke="currentColor"/><text x="62" y="53.7" font-size="12" text-anchor="end">128</text> <text x="18" y="145.0" font-size="12" text-anchor="middle" transform="rotate(-90 18 145.0)">live bytes (KB, int8)</text> <line x1="85.1" y1="260" x2="85.1" y2="265" stroke="currentColor"/><text x="85.1" y="278" font-size="12" text-anchor="middle">f1</text> <line x1="103.9" y1="260" x2="103.9" y2="265" stroke="currentColor"/><text x="103.9" y="278" font-size="12" text-anchor="middle">f2</text> <line x1="167.9" y1="260" x2="167.9" y2="265" stroke="currentColor"/><text x="167.9" y="278" font-size="12" text-anchor="middle">f4</text>
<line x1="265.8" y1="260" x2="265.8" y2="265" stroke="currentColor"/><text x="265.8" y="278" font-size="12" text-anchor="middle">f7</text> <line x1="397.7" y1="260" x2="397.7" y2="265" stroke="currentColor"/><text x="397.7" y="278" font-size="12" text-anchor="middle">f11</text> <line x1="495.6" y1="260" x2="495.6" y2="265" stroke="currentColor"/><text x="495.6" y="278" font-size="12" text-anchor="middle">f14</text> <line x1="593.5" y1="260" x2="593.5" y2="265" stroke="currentColor"/><text x="593.5" y="278" font-size="12" text-anchor="middle">f17</text> <text x="360.0" y="300" font-size="12" text-anchor="middle">fx 노드 실행 순서 (f = features 블록 시작, 전체 155노드)</text>
<polyline points="70.0,215.6 73.8,156.5 77.5,200.9 81.3,200.9 85.1,141.7 88.8,200.9 92.6,200.9 96.4,171.3 100.1,230.4 103.9,53.0 107.7,82.6 111.4,82.6 115.2,38.2 119.0,215.6 122.7,215.6 126.5,208.2 130.3,252.6 134.0,208.2 137.8,208.2 141.6,208.2 145.3,163.9 149.1,208.2 152.9,208.2 156.6,200.9 160.4,245.2 164.2,237.8 167.9,208.2 171.7,215.6 175.5,215.6 179.2,204.6 183.0,248.9 186.8,248.9 190.5,245.2 194.3,256.3 198.1,234.1 201.8,234.1 205.6,234.1 209.4,211.9 213.1,234.1 216.9,234.1 220.6,230.4 224.4,252.6 228.2,248.9 231.9,234.1 235.7,234.1 239.5,234.1 243.2,211.9 247.0,234.1 250.8,234.1 254.5,230.4 258.3,252.6 262.1,248.9 265.8,234.1 269.6,237.8 273.4,237.8 277.1,232.3 280.9,254.5 284.7,254.5 288.4,253.1 292.2,258.6 296.0,250.3 299.7,250.3 303.5,250.3 307.3,242.0 311.0,250.3 314.8,250.3 318.6,248.9 322.3,257.2 326.1,255.8 329.9,250.3 333.6,250.3 337.4,250.3 341.2,242.0 344.9,250.3 348.7,250.3 352.5,248.9 356.2,257.2 360.0,255.8 363.8,250.3 367.5,250.3 371.3,250.3 375.1,242.0 378.8,250.3 382.6,250.3 386.4,248.9 390.1,257.2 393.9,255.8 397.7,250.3 401.4,251.7 405.2,251.7 409.0,243.4 412.7,251.7 416.5,251.7 420.3,249.8 424.0,258.2 427.8,247.1 431.6,247.1 435.3,247.1 439.1,236.0 442.9,247.1 446.6,247.1 450.4,245.2 454.2,256.3 457.9,254.5 461.7,247.1 465.5,247.1 469.2,247.1 473.0,236.0 476.8,247.1 480.5,247.1 484.3,245.2 488.1,256.3 491.8,254.5 495.6,247.1 499.4,248.9 503.1,248.9 506.9,246.1 510.6,257.2 514.4,257.2 518.2,256.4 521.9,259.2 525.7,254.3 529.5,254.3 533.2,254.3 537.0,249.5 540.8,254.3 544.5,254.3 548.3,253.5 552.1,258.4 555.8,257.6 559.6,254.3 563.4,254.3 567.1,254.3 570.9,249.5 574.7,254.3 578.4,254.3 582.2,253.5 586.0,258.4 589.7,257.6 593.5,254.3 597.3,255.1 601.0,255.1 604.8,250.3 608.6,255.1 612.3,255.1 616.1,253.5 619.9,258.4 623.6,239.9 627.4,241.5 631.2,241.5 634.9,239.5 638.7,257.9 642.5,257.9 646.2,257.9 650.0,260.0" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="115.2" cy="38.2" r="4" fill="#d0564a"/> <text x="125.2" y="42.2" font-size="12">peak 135 KB = f2 depthwise 입력 108 KB + 출력 27 KB</text> <line x1="70" y1="154.9" x2="650" y2="154.9" stroke="#e08a3c" stroke-dasharray="4 3"/> <text x="650" y="148.9" font-size="12" text-anchor="end">예: 64 KB SRAM 예산</text>
</svg>
```

그림 4 — 예제 5의 추정기로 계산한 MobileNetV2-0.35@96 int8의 live bytes 곡선(155개 fx 노드 전부, 실제 계산값). peak는 초반 features.2의 depthwise 순간 한 점에 몰려 있고, 뒤로 갈수록 해상도가 줄어 곡선이 낮아진다. 점선처럼 64 KB SRAM 예산이라면 **앞쪽 몇 블록만** 문제다 — MCUNetV2가 초반 블록만 patch 단위로 실행하는 이유(C7 7.4절)가 이 그림 한 장에 있다.

출력에서 볼 것:

- MBv2의 peak는 **residual이 없는** stride-2 블록이다. 이 모델에서는 skip이 peak를 올리지 않는다. 반면 TBlock의 peak에는 residual 입력 x(4 KB, 10 %)가 얹혀 있다 — 3.4절의 효과다.
- TBlock의 peak는 `* 0.25`(scale) 때문에 16 KB 텐서 두 개가 동시에 산다. scale을 q에 미리 곱하거나(q가 4 KB라 싸다) mul을 in-place로 하면 16 KB가 줄 것 같지만, 그러면 peak가 바로 다음의 softmax(입력 16 KB + 출력 16 KB)로 옮겨가서 여전히 40 KB다. **peak를 줄이려면 T × T 행렬 자체를 없애야 한다** — 행 단위 softmax를 fuse한 attention 커널(FlashAttention류의 발상, E·D4에서)이 그 방법이다.
- `op=x`는 입력 placeholder다.

### 5.5 해상도 · 폭 · 활성값 정밀도 sweep (예제 8)

무엇을 확인하는 코드인지: MobileNetV2의 width(0.35, 0.5)와 입력 해상도를 바꾸며 int8 peak-live와 int8/int16/fp32 활성값 arena를 잰다. SRAM 예산 안에 들어가는 조합을 고르는 표다.

```python
import torch, torchvision
from d2_mem import trace_tensors, live_curve, greedy_arena
print(f"{'width':>5} {'res':>4} | {'int8 peak':>9} {'int8 arena':>10} {'int16 arena':>11} {'fp32 arena':>10}")
for w in (0.35, 0.5):
    m = torchvision.models.mobilenet_v2(width_mult=w, num_classes=2).eval()
    for r in (64, 96, 128, 160):
        x = torch.randn(1, 3, r, r)
        pk = max(live_curve(trace_tensors(m, x, 1))) / 1024
        a = [greedy_arena(trace_tensors(m, x, b))[0] / 1024 for b in (1, 2, 4)]
        print(f"{w:>5} {r:>4} | {pk:>7.1f}KB {a[0]:>8.1f}KB {a[1]:>9.1f}KB {a[2]:>8.1f}KB")
```

```text
width  res | int8 peak int8 arena int16 arena fp32 arena
 0.35   64 |    60.0KB     60.0KB     120.0KB    240.0KB
 0.35   96 |   135.0KB    135.0KB     270.0KB    540.0KB
 0.35  128 |   240.0KB    240.0KB     480.0KB    960.0KB
 0.35  160 |   375.0KB    375.0KB     750.0KB   1500.0KB
  0.5   64 |    60.0KB     64.0KB     128.0KB    256.0KB
  0.5   96 |   135.0KB    144.0KB     288.0KB    576.0KB
  0.5  128 |   240.0KB    256.0KB     512.0KB   1024.0KB
  0.5  160 |   375.0KB    400.0KB     800.0KB   1600.0KB
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="270" x2="590" y2="270" stroke="currentColor"/> <line x1="70" y1="270" x2="70" y2="30" stroke="currentColor"/> <text x="62" y="274.0" font-size="12" text-anchor="end">0</text> <text x="62" y="214.0" font-size="12" text-anchor="end">400</text> <text x="62" y="154.0" font-size="12" text-anchor="end">800</text> <text x="62" y="94.0" font-size="12" text-anchor="end">1200</text> <text x="62" y="34.0" font-size="12" text-anchor="end">1600</text> <text x="70.0" y="288" font-size="12" text-anchor="middle">64</text> <text x="156.7" y="288" font-size="12" text-anchor="middle">80</text> <text x="243.3" y="288" font-size="12" text-anchor="middle">96</text>
<text x="330.0" y="288" font-size="12" text-anchor="middle">112</text> <text x="416.7" y="288" font-size="12" text-anchor="middle">128</text> <text x="503.3" y="288" font-size="12" text-anchor="middle">144</text> <text x="590.0" y="288" font-size="12" text-anchor="middle">160</text> <text x="330.0" y="310" font-size="12" text-anchor="middle">입력 해상도 (정사각형 한 변, MobileNetV2-0.35)</text> <text x="18" y="150" font-size="12" text-anchor="middle" transform="rotate(-90 18 150)">arena (KB)</text> <line x1="70" y1="231.6" x2="590" y2="231.6" stroke="#888" stroke-dasharray="4 3"/><text x="76" y="226.6" font-size="12">SRAM 256 KB</text>
<line x1="70" y1="193.2" x2="590" y2="193.2" stroke="#888" stroke-dasharray="4 3"/><text x="76" y="188.2" font-size="12">SRAM 512 KB</text> <polyline points="70.0,261.0 156.7,255.9 243.3,249.8 330.0,242.4 416.7,234.0 503.3,224.4 590.0,213.8" fill="none" stroke="#4a7bd0" stroke-width="2"/> <circle cx="70.0" cy="261.0" r="3" fill="#4a7bd0"/> <circle cx="156.7" cy="255.9" r="3" fill="#4a7bd0"/> <circle cx="243.3" cy="249.8" r="3" fill="#4a7bd0"/> <circle cx="330.0" cy="242.4" r="3" fill="#4a7bd0"/> <circle cx="416.7" cy="234.0" r="3" fill="#4a7bd0"/> <circle cx="503.3" cy="224.4" r="3" fill="#4a7bd0"/>
<circle cx="590.0" cy="213.8" r="3" fill="#4a7bd0"/> <text x="596.0" y="217.8" font-size="12">int8 375</text> <polyline points="70.0,252.0 156.7,241.9 243.3,229.5 330.0,214.9 416.7,198.0 503.3,178.9 590.0,157.5" fill="none" stroke="#e08a3c" stroke-width="2"/> <circle cx="70.0" cy="252.0" r="3" fill="#e08a3c"/> <circle cx="156.7" cy="241.9" r="3" fill="#e08a3c"/> <circle cx="243.3" cy="229.5" r="3" fill="#e08a3c"/> <circle cx="330.0" cy="214.9" r="3" fill="#e08a3c"/> <circle cx="416.7" cy="198.0" r="3" fill="#e08a3c"/> <circle cx="503.3" cy="178.9" r="3" fill="#e08a3c"/> <circle cx="590.0" cy="157.5" r="3" fill="#e08a3c"/>
<text x="596.0" y="161.5" font-size="12">int16 750</text> <polyline points="70.0,234.0 156.7,213.8 243.3,189.0 330.0,159.8 416.7,126.0 503.3,87.8 590.0,45.0" fill="none" stroke="#d0564a" stroke-width="2"/> <circle cx="70.0" cy="234.0" r="3" fill="#d0564a"/> <circle cx="156.7" cy="213.8" r="3" fill="#d0564a"/> <circle cx="243.3" cy="189.0" r="3" fill="#d0564a"/> <circle cx="330.0" cy="159.8" r="3" fill="#d0564a"/> <circle cx="416.7" cy="126.0" r="3" fill="#d0564a"/> <circle cx="503.3" cy="87.8" r="3" fill="#d0564a"/> <circle cx="590.0" cy="45.0" r="3" fill="#d0564a"/> <text x="596.0" y="49.0" font-size="12">fp32 1500</text>
</svg>
```

그림 5 — MobileNetV2-0.35의 arena vs 입력 해상도(64~160, 16 간격으로 실제 계산). 세 곡선 모두 해상도²에 비례해 위로 휜다. 회색 점선은 흔한 SRAM 크기다. int8이라도 128에서 240 KB로 256 KB MCU가 빠듯하고(런타임 오버헤드·다른 버퍼를 더하면 넘는다), int16 활성값은 96에서 이미 256 KB를 넘는다.

출력에서 볼 것:

- 해상도 96 → 128(1.33배)에서 arena는 135 → 240 KB(1.78배 = 1.33²)다. **활성값은 해상도²에 비례한다**(B6 3절의 MACs와 같은 법칙). 그림 5를 만들 때 같이 계산한 144에서는 303.75 KB — C7 7.1절 손계산(303.8 KB)과 같다.
- width 0.35와 0.5의 **peak-live는 똑같다**(96에서 둘 다 135 KB). peak 블록(features.2)의 expand 채널이 둘 다 48로 반올림되기 때문이다(`_make_divisible`로 8의 배수). **메모리는 채널보다 해상도에 훨씬 민감하다** — SRAM이 모자라면 폭보다 해상도를 먼저 깎는다.
- 그런데 width 0.5의 **arena는 peak보다 6.7 % 크다**(135 → 144 KB). 이게 **단편화(fragmentation)**다. 배치를 열어 보면(`greedy_arena`가 돌려주는 offset) 범인은 features.2의 출력 9 KB짜리 텐서다. 이 텐서는 features.3의 residual add까지 살아야 해서(lifetime [15, 25]) 긴데, greedy는 큰 텐서부터 주소 0 근처에 깔아 버리므로 이 긴 텐서가 들어갈 틈이 없어 135 KB 위(138,240 ~ 147,456 B)로 밀려난다. 손으로 배치하면 135 KB에 넣을 수 있다. **residual은 peak뿐 아니라 배치 품질도 나쁘게 만든다** — 그래서 실제 planner들은 크기순 외에 lifetime 길이·시작 순서 같은 여러 휴리스틱을 시도해 가장 작은 것을 고른다.
- int16 활성값(정확도 때문에 A16W8을 쓰는 경우, C2)은 arena가 정확히 2배다. 정밀도 선택이 곧 SRAM 선택이다.

### 5.6 이 추정기의 한계 — 정직하게

| 이 추정기가 가정한 것 | 실제 런타임 | 영향 |
|---|---|---|
| BN·ReLU는 항상 conv에 fuse | 대부분의 배포 런타임이 그렇다. 지원 안 되는 패턴은 별도 op | 드물게 과소 추정 |
| op마다 출력 버퍼 1개, scratch 0 | 커널별 scratch가 있다 (4절) | 과소 추정 — scratch를 가상 텐서로 더해야 함 |
| 정렬 없음 | 보통 16 B 등으로 정렬 | 텐서 수 × 최대 15 B 정도 |
| 텐서 메타데이터 없음 | TFLM은 텐서 구조체·노드 정보도 arena에 둔다 | 모델 크기에 비례해 수 KB |
| fx 노드 순서 = 실행 순서 | 런타임이 순서를 바꾸기도 한다 (C6 11절) | peak가 달라질 수 있음 |
| transpose는 새 버퍼 | NPU는 layout 변환을 없애거나 합치기도 한다 | 과대 추정 |
| in-place 병합은 한 단계만 | 실제 planner는 op별 in-place 가능 여부 표를 가진다 | 대략적 |

그래서 이 숫자는 **설계 단계의 1차 추정**이다. 확정은 타깃 런타임이 알려 주는 숫자(TFLM `arena_used_bytes()`, NPU 컴파일러 리포트)로 한다. 실무 순서: 추정기로 후보 모델을 거르고 → 런타임으로 확정하고 → 그 차이를 기록해 추정기에 보정 계수로 넣는다. Don이 SSD에서 "시뮬레이션 성능 모델 → 실측 → 모델 보정"을 돌린 것과 같은 루프다.

---

## 6. 실제 메모리 측정

추정은 틀릴 수 있다. 측정은 두 쪽에서 한다: **호스트(Python)**, **펌웨어(바이너리·런타임)**. 둘은 재는 대상이 다르다는 걸 먼저 알아야 한다.

### 6.1 호스트 Python — 무엇이 보이고 무엇이 안 보이나 (예제 9)

무엇을 확인하는 코드인지: `tracemalloc`(파이썬 메모리 추적기)과 `resource.getrusage().ru_maxrss`(프로세스 최대 RSS)가 numpy·torch 할당을 각각 어떻게 보는지 확인한다.

```python
import tracemalloc, resource, sys, numpy as np, torch
def maxrss_mb():                       # macOS: 바이트, Linux: KB 단위! (man getrusage)
    r = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return r / 2**20 if sys.platform == "darwin" else r / 2**10
tracemalloc.start()
base = maxrss_mb()
a = np.ones(64 * 2**20 // 4, dtype=np.float32)          # 64 MB numpy
cur_np, _ = tracemalloc.get_traced_memory()
t = torch.ones(64 * 2**20 // 4, dtype=torch.float32)    # 64 MB torch
cur_both, peak = tracemalloc.get_traced_memory()
print(f"tracemalloc: numpy 후 {cur_np/2**20:6.1f} MB, torch 추가 후 {cur_both/2**20:6.1f} MB")
print(f"ru_maxrss 증가: {maxrss_mb() - base:6.1f} MB (numpy 64 + torch 64 = 128 MB 기대)")
del a, t
print(f"del 후 ru_maxrss 증가: {maxrss_mb() - base:6.1f} MB  (최대값이라 줄지 않는다)")
```

```text
tracemalloc: numpy 후   64.0 MB, torch 추가 후   64.0 MB
ru_maxrss 증가:  128.7 MB (numpy 64 + torch 64 = 128 MB 기대)
del 후 ru_maxrss 증가:  128.7 MB  (최대값이라 줄지 않는다)
```

출력에서 볼 것:

- `tracemalloc`은 numpy 배열은 보지만(numpy가 자기 할당을 tracemalloc에 알려 준다) **torch 텐서 64 MB는 못 본다**(torch의 C++ allocator는 알리지 않는다). "tracemalloc으로 모델 메모리를 쟀다"는 말은 거의 항상 틀린다.
- `ru_maxrss`는 둘 다 본다. 하지만 **프로세스 전체의 최댓값(high-water)**이라 줄지 않고, 인터프리터·라이브러리 로딩까지 섞여 있으며, 단위가 OS마다 다르다(macOS 바이트, Linux KB). 여기서는 64 MB씩 큰 할당이라 깔끔하게 보였지만, KB 단위의 모델 활성값은 잡음에 묻힌다.

PyTorch에는 CPU에서도 op별 할당을 기록하는 프로파일러가 있다. 예제 10 — 무엇을 확인하는 코드인지: DS-CNN fp32를 eager로 한 번 돌릴 때 op별 CPU 메모리 할당을 보고, 추정기(fp32 naive 283.5 KB)와 비교한다.

```python
import torch
from torch.profiler import profile, ProfilerActivity
from models import DSCNN
torch.manual_seed(0)
m, x = DSCNN().eval(), torch.randn(1, 1, 49, 10)
with torch.no_grad():
    m(x)                                                      # warm-up
    with profile(activities=[ProfilerActivity.CPU], profile_memory=True) as prof:
        m(x)
ev = prof.key_averages()
alloc = sum(e.self_cpu_memory_usage for e in ev if e.self_cpu_memory_usage > 0)
print(f"profiler: 양수 할당 합 = {alloc/1024:.1f} KB")
for e in sorted(ev, key=lambda e: -e.self_cpu_memory_usage)[:4]:
    print(f"  {e.key:<28} calls={e.count:2d} self_cpu_mem={e.self_cpu_memory_usage/1024:8.1f} KB")
```

```text
profiler: 양수 할당 합 = 849.1 KB
  aten::empty                  calls=68 self_cpu_mem=   430.3 KB
  aten::clamp_min              calls= 9 self_cpu_mem=   281.2 KB
  aten::resize_                calls= 5 self_cpu_mem=   125.0 KB
  aten::_slow_conv2d_forward   calls= 5 self_cpu_mem=    11.7 KB
```

출력에서 볼 것:

- (재현성 주의: 이 예제는 실행할 때마다 op별 귀속이 수십 KB씩 달라진다 — 다섯 번 실행해 총합 848~868 KB, `empty`·`resize_`·conv 항목이 오르내렸다. thread 스케줄과 allocator 재사용 때문이다. `clamp_min` 281.2 KB만은 매번 같았다.)
- eager PyTorch는 약 850 KB를 할당했다. 추정기의 fp32 naive(283.5 KB)의 3배다. 이유가 출력에 다 있다: `clamp_min`(= ReLU) 9번이 281.2 KB — 정확히 `9 × 32,000 B`다. **eager에서는 ReLU가 fuse 되지 않고 새 텐서를 만든다.** BN 출력도 마찬가지(`empty`에 포함). `resize_`와 conv 쪽 항목(`_slow_conv2d_forward` 등)은 conv 커널이 내부에서 잡는 workspace(4.1절의 im2col류)로 보인다.
- 결론: **호스트 eager 메모리는 배포 메모리가 아니다.** 호스트 측정은 "추정기의 가정이 어디서 틀리는지"를 찾는 데 쓰고, 배포 숫자는 타깃 런타임에서 잰다.

ONNX Runtime(호스트나 SoC CPU에서 돌리는 경우)은 기본으로 **메모리 arena**(`SessionOptions.enable_cpu_mem_arena`)와 **memory pattern**(`enable_mem_pattern`, 첫 실행에서 할당 패턴을 기억해 재사용)을 켠다. 그래서 RSS가 첫 추론 뒤에 커지고 줄지 않는 게 정상이다. 레이어별 사용량을 정확히 내주는 간단한 공개 API는 이 노트에서 확인하지 못했으므로, ORT에서는 "RSS 전후 차이 + 프로파일링 JSON"으로 대략 본다고만 해 둔다.

### 6.2 펌웨어 — 바이너리 섹션을 읽는다 (예제 11, C)

펌웨어 쪽 정적 메모리는 **링커가 이미 계산해 놓았다**. 읽기만 하면 된다. Cortex-M 툴체인(`arm-none-eabi-size`)은 이렇게 보여 준다:

```
 text  = .text + .rodata (+ .data의 초기값은 별도)     → flash
 data  = .data                                          → flash(초기값) + RAM(실행 중)
 bss   = .bss                                           → RAM만 (부팅 때 0으로 채움)

 flash 사용 = text + data
 RAM 정적 사용 = data + bss     (+ 스택·heap은 링커 스크립트의 예약 크기)
```

말로 하면: `.data`는 초기값이 있어서 flash에 원본, RAM에 실행용 사본 — **두 번** 센다. tensor arena는 초기값이 없으니 `.bss`에 넣어 flash를 낭비하지 않는다.

이 Mac에는 ARM 툴체인이 없으므로 같은 개념을 Mach-O 바이너리로 확인한다. 이름만 다르다: `__TEXT,__const` ↔ `.rodata`, `__DATA,__data` ↔ `.data`, `__DATA,__bss` ↔ `.bss`.

무엇을 확인하는 코드인지: 가중치(const), requant 계수(초기값 있는 전역), arena(초기값 없는 전역)가 각각 어느 섹션에 몇 바이트로 들어가는지 `size -m`으로 본다.

```c
#include <stdint.h>
#include <stdio.h>

#define W_BYTES   40000                  /* 모델 가중치 (int8) — 읽기 전용 */
#define ARENA     (64 * 1024)            /* tensor arena — 읽기/쓰기, 초기값 없음 */
const int8_t g_weights[W_BYTES] = {1, 2, 3};          /* → __TEXT,__const  (ELF: .rodata) */
int32_t g_quant_mult[64] = {1073741824, 1};            /* → __DATA,__data   (ELF: .data)   */
static int8_t g_arena[ARENA];                          /* → __DATA,__bss    (ELF: .bss)    */

int main(int argc, char **argv) {
    (void)argv;
    volatile int idx = argc;                            /* 최적화로 배열이 사라지지 않게 */
    g_arena[idx] = g_weights[idx];
    g_quant_mult[idx] += g_arena[idx];
    printf("w=%d arena=%d mult=%d\n", g_weights[idx], g_arena[idx], (int)g_quant_mult[idx]);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 sections.c -o sections && ./sections && size sections && size -m sections
stat -f "file size = %z B" sections
```

```text
w=2 arena=2 mult=3
__TEXT	__DATA	__OBJC	others	dec	hex
49152	81920	0	4295000064	4295131136	100028000
Segment __PAGEZERO: 4294967296 (zero fill) 
Segment __TEXT: 49152
	Section __text: 140
	Section __stubs: 12
	Section __const: 40000
	Section __cstring: 23
	Section __unwind_info: 88
	total 40263
Segment __DATA_CONST: 16384
	Section __got: 8
	total 8
Segment __DATA: 81920
	Section __data: 256
	Section __bss: 65536 (zerofill)
	total 65792
Segment __LINKEDIT: 16384
total 4295131136
file size = 83080 B
```

출력에서 볼 것:

- `__const: 40000` = 가중치 배열 정확히 40,000 B. `__data: 256` = `64 × 4` B. `__bss: 65536 (zerofill)` = arena.
- 세그먼트 크기(49152, 81920)는 16 KB 페이지로 반올림된 값이다. MCU 링커 맵에서도 섹션 정렬·padding 때문에 합이 딱 맞지 않는 일이 흔하다 — **섹션 단위로** 읽는다.
- 파일 크기는 83 KB인데 `__DATA` 세그먼트는 80 KB다: **bss는 파일(= flash 이미지)에 없다.** 64 KB arena를 `= {0}`이 아닌 초기값으로 채우면 `.data`로 가서 flash 64 KB가 낭비된다(11절 흔한 실수).
- 첫 줄의 `others 4295000064`는 4 GB `__PAGEZERO`(널 포인터 보호용 가상 영역) 때문이다. 실제 메모리가 아니다 — 도구 출력을 읽을 때 이런 항목을 걸러 내는 습관.

**링커 맵 파일**(`-Wl,-Map=out.map`, GNU ld)은 이 숫자를 **심볼 단위**로 보여 준다. 모델 배포에서 맵 파일로 확인할 것:

- 가중치 배열(예: `g_model_data`)이 `.rodata`에 있는가 — 실수로 `const`를 빼면 `.data`로 가서 flash와 RAM을 **둘 다** 먹는다.
- tensor arena가 의도한 RAM 영역(예: DTCM vs 일반 SRAM, DMA 가능 영역)에 있는가 — 링커 스크립트의 section attribute(`__attribute__((section(".sram2")))` 등)가 먹었는지.
- 추론 런타임·커널 라이브러리의 `.text` 기여 — 사용하지 않는 op 커널이 링크되어 있지 않은가(TFLM이라면 `MicroMutableOpResolver`로 필요한 op만 등록해 줄인다).
- 정렬 padding(`*fill*` 항목)이 큰 곳.

### 6.3 스택 high-water — stack painting (예제 12, C)

정적 섹션은 링커가 알려 주지만 **스택 사용량은 실행해 봐야 안다**(재귀, 함수 포인터, 라이브러리 호출 때문에 정적 분석이 완전하지 않다). 표준 기법이 **stack painting**이다:

1. 태스크 시작 전에 스택 전체를 알려진 패턴(예: `0xA5`)으로 칠한다.
2. 최악 조건으로 일을 시킨다(가장 긴 추론 경로, 인터럽트 포함).
3. 스택 바닥(스택은 높은 주소에서 낮은 주소로 자란다)부터 패턴이 **남아 있는** 바이트를 센다. 칠이 벗겨진 가장 깊은 지점이 high-water mark다.

FreeRTOS의 `uxTaskGetStackHighWaterMark()`가 정확히 이 방식이다(칠하는 값 `tskSTACK_FILL_BYTE`가 `0xa5`).

무엇을 확인하는 코드인지: POSIX 스레드에 **우리가 칠한 배열을 스택으로 주고**(`pthread_attr_setstack`) 추론 커널 흉내(재귀 깊이 × 지역 scratch 배열)를 돌린 뒤 high-water를 잰다. printf 한 번이 스택을 얼마나 먹는지도 본다.

```c
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STACK_SIZE (64 * 1024)
#define FILL 0xA5                     /* FreeRTOS도 같은 값(tskSTACK_FILL_BYTE)으로 칠한다 */
static _Alignas(16384) uint8_t task_stack[STACK_SIZE];

static int32_t layer(int depth, int scratch) {      /* 추론 커널 흉내: 지역 scratch 배열 사용 */
    volatile int8_t col[scratch];                    /* VLA: 크기만큼 스택을 쓴다 */
    int32_t acc = 0;
    for (int i = 0; i < scratch; i++) col[i] = (int8_t)(i + depth);
    for (int i = 0; i < scratch; i++) acc += col[i];
    return depth ? acc + layer(depth - 1, scratch) : acc;
}
static void *task(void *arg) {
    const int *p = arg;
    volatile int32_t acc = layer(p[0], p[1]);
    if (p[2]) printf("  (task 안에서 printf 호출: acc=%d)\n", (int)acc);
    return NULL;
}
static size_t high_water(void) {      /* 스택은 아래로 자란다: 바닥부터 칠이 남은 바이트를 센다 */
    size_t untouched = 0;
    while (untouched < STACK_SIZE && task_stack[untouched] == FILL) untouched++;
    return STACK_SIZE - untouched;
}
int main(void) {
    int cfg[][3] = {{0, 256, 0}, {0, 256, 1}, {3, 256, 0}, {3, 2048, 0}, {10, 2048, 0}};
    for (int k = 0; k < 5; k++) {
        memset(task_stack, FILL, sizeof task_stack);             /* 1) 칠하기 */
        pthread_attr_t at; pthread_t th;
        pthread_attr_init(&at);
        pthread_attr_setstack(&at, task_stack, sizeof task_stack);
        pthread_create(&th, &at, task, cfg[k]);                   /* 2) 그 스택에서 일 시키기 */
        pthread_join(th, NULL);
        printf("depth=%2d scratch=%4d printf=%d -> high-water = %5zu / %d B\n",
               cfg[k][0], cfg[k][1], cfg[k][2], high_water(), STACK_SIZE);  /* 3) 긁어 보기 */
    }
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 stack_paint.c -o stack_paint && ./stack_paint
```

```text
depth= 0 scratch= 256 printf=0 -> high-water =   400 / 65536 B
  (task 안에서 printf 호출: acc=-128)
depth= 0 scratch= 256 printf=1 -> high-water =  1296 / 65536 B
depth= 3 scratch= 256 printf=0 -> high-water =  1312 / 65536 B
depth= 3 scratch=2048 printf=0 -> high-water =  8480 / 65536 B
depth=10 scratch=2048 printf=0 -> high-water = 23152 / 65536 B
```

(컴파일 경고 0개, Apple clang 21, arm64.)

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="d2m6" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <rect x="250" y="30" width="120" height="260" fill="none" stroke="currentColor"/> <rect x="250" y="30" width="120" height="91.8" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/> <rect x="250" y="121.8" width="120" height="168.2" fill="#888" fill-opacity="0.3" stroke="#888"/> <text x="310" y="210" font-size="13" text-anchor="middle">0xA5 0xA5 …</text> <text x="310" y="228" font-size="12" text-anchor="middle">한 번도 안 쓴 영역</text>
<text x="310" y="80" font-size="12" text-anchor="middle">사용된 영역</text> <line x1="235" y1="121.8" x2="390" y2="121.8" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 3"/> <text x="396" y="118" font-size="12">high-water 23,152 B (depth 10, scratch 2 KB)</text> <line x1="235" y1="63.6" x2="390" y2="63.6" stroke="#d0564a" stroke-dasharray="2 3"/> <text x="396" y="67" font-size="12">8,480 B (depth 3, scratch 2 KB)</text> <line x1="235" y1="35.1" x2="390" y2="35.1" stroke="#d0564a" stroke-dasharray="2 3"/> <text x="396" y="39" font-size="12">400 ~ 1,312 B (작은 경우들 · printf)</text> <text x="396" y="210" font-size="12">남은 여유 = 65,536 − 23,152</text>
<text x="396" y="228" font-size="12">= 42,384 B</text> <text x="240" y="26" font-size="12" text-anchor="end">높은 주소 · 초기 SP</text> <text x="240" y="313" font-size="12" text-anchor="end">task_stack[0] · 낮은 주소</text> <line x1="200" y1="50" x2="200" y2="140" stroke="currentColor" stroke-width="1.5" marker-end="url(#d2m6)"/> <text x="192" y="100" font-size="12" text-anchor="end">스택이 자라는 방향</text> <line x1="170" y1="280" x2="244" y2="280" stroke="currentColor" stroke-width="1.5" marker-end="url(#d2m6)"/> <text x="165" y="275" font-size="12" text-anchor="end">여기부터 위로</text> <text x="165" y="291" font-size="12" text-anchor="end">0xA5를 센다</text>
</svg>
```

그림 6 — stack painting. 64 KB 스택(세로 260 px, 비율 그대로)을 0xA5로 칠하고 일을 시킨 뒤, 바닥부터 칠이 남은 구간(회색)을 센다. 칠이 벗겨진 가장 깊은 선(빨간 점선)이 high-water다. 위쪽 점선들은 예제 12의 다른 설정들.

출력에서 볼 것:

- depth 3 → 10(재귀 7단계 추가)에서 high-water가 8,480 → 23,152 B로 늘었다. 한 단계당 `(23,152 − 8,480) / 7 ≈ 2,096 B` = scratch 2,048 B + 스택 프레임 약 48 B. **지역 배열을 쓰는 커널은 그 크기가 그대로 스택 비용**이다. 커널 scratch를 스택이 아닌 arena에서 받는 이유(4.3절)다.
- `printf` 한 번이 400 → 1,296 B, 약 **900 B**를 더 썼다. 펌웨어에서 로그 한 줄 추가했다가 스택 overflow가 나는 고전적인 버그의 크기감이다. 추론 태스크 안에서 디버그 printf를 켜고 high-water를 다시 재야 한다.
- high-water는 **그 실행에서 실제로 지나간 경로**만 반영한다. 최악 경로(가장 긴 입력, 가장 깊은 인터럽트 중첩)를 돌리지 않으면 과소 측정된다. 정적 분석(`-fstack-usage`로 함수별 프레임 크기 + 호출 그래프)과 **둘 다** 쓴다. 여유는 보통 high-water에 20~50 %를 더한다(팀 규칙에 따름).

### 6.4 arena·heap 실측

- **TFLM**: `AllocateTensors()` 후 `interpreter.arena_used_bytes()`가 실제 사용량을 준다. 절차: arena를 넉넉히(예: 추정 × 2) 잡고 → 한 번 돌려 사용량을 읽고 → 여유를 더해 확정(C6 10.5절).
- **arena도 칠할 수 있다**: 스택과 똑같이 arena 배열을 0xA5로 칠하고 여러 번 추론한 뒤 끝에서부터 칠이 남은 구간을 세면, 런타임 보고값을 교차 검증할 수 있다(단, 활성값이 우연히 0xA5와 같은 바이트일 수 있으니 대략값).
- **heap**: MCU에서 추론 경로에 `malloc`이 있으면 안 된다(단편화·비결정성). 링크 후 `malloc` 심볼 참조를 grep하거나, `malloc`을 감싸 호출 횟수·최대 사용량을 세는 wrapper(`-Wl,--wrap=malloc`, GNU ld)를 붙여 확인한다.
- **SoC(Linux/Android)**: 프로세스 RSS/PSS(`/proc/<pid>/smaps`, Android `dumpsys meminfo`)와 벤더 NPU 런타임의 메모리 리포트를 함께 본다. mmap 된 가중치는 공유 page cache로 잡혀 PSS로 나눠 보인다.

---

## 7. 스트리밍 모델의 persistent state

### 7.1 무엇이 추론 사이에 살아남나

always-on 오디오·IMU 모델은 보통 **스트리밍**으로 돈다: 1초 창 전체를 매번 다시 계산하지 않고, 새 프레임(예: 10~20 ms)이 올 때마다 조금씩 계산하며 과거 정보를 **상태(state)**로 들고 간다. 이 상태는 1.2절 표의 "추론 사이에도 사는" 메모리다.

| 모델 부품 | 상태 | 크기 식 |
|---|---|---|
| GRU | hidden h | H × bytes |
| LSTM | hidden h + cell c | 2 × H × bytes (c는 보통 더 넓은 정밀도) |
| 시간축 conv (커널 k_t) | 과거 k_t − 1 프레임의 입력 (링버퍼) | (k_t − 1) × C_in × (나머지 축) × bytes |
| dilated conv (dilation d) | 과거 (k_t − 1) × d 프레임 | (k_t − 1) × d × C_in × bytes |
| 특징 추출 (MFCC) | FFT 창 overlap 샘플 | (창 − hop) × 2 B (int16) |
| Transformer decoder | KV-cache | 2 × layers × kv_heads × head_dim × T × bytes (D5) |

### 7.2 conv 링버퍼 — 스트리밍이 같은 답을 내는지 확인 (예제 13)

무엇을 확인하는 코드인지: 채널 8개 depthwise 시간축 conv(k = 3)를 "창 전체를 한 번에" 계산한 결과와, 프레임이 하나씩 들어올 때 과거 2 프레임만 링버퍼로 들고 계산한 결과가 같은지 확인하고 상태 크기를 센다.

```python
import numpy as np
rng = np.random.default_rng(0)
C, K, T = 8, 3, 20                         # 채널 8, 시간축 커널 3, 프레임 20개
w = rng.standard_normal((C, K))            # depthwise temporal conv (채널마다 커널 하나)
x = rng.standard_normal((C, T))
full = np.stack([np.convolve(x[c], w[c][::-1], "valid") for c in range(C)])   # 전체 창을 한 번에
ring = np.zeros((C, K - 1))                # 스트리밍 상태: 과거 K-1 프레임만 보관
outs = []
for t in range(T):                         # 프레임이 하나씩 도착한다
    win = np.concatenate([ring, x[:, t:t+1]], axis=1)       # (C, K)
    outs.append((win * w).sum(axis=1))
    ring = win[:, 1:]                      # 가장 오래된 프레임 버리기 (링버퍼 한 칸 전진)
stream = np.stack(outs, axis=1)[:, K - 1:] # 처음 K-1개는 워밍업(0 패딩) 출력
print("max |full - stream| =", np.abs(full - stream).max())
print(f"상태 = (K-1)·C = {ring.size} 원소 = {ring.size} B(int8) — 창 전체 {C*T} 원소 대신")
for name, h, n in (("GRU", 64, 1), ("LSTM", 64, 2)):     # LSTM은 h와 c 두 개
    print(f"{name} hidden {h}: 상태 {n*h} 원소 = int8 {n*h} B / fp32 {4*n*h} B")
```

```text
max |full - stream| = 8.881784197001252e-16
상태 = (K-1)·C = 16 원소 = 16 B(int8) — 창 전체 160 원소 대신
GRU hidden 64: 상태 64 원소 = int8 64 B / fp32 256 B
LSTM hidden 64: 상태 128 원소 = int8 128 B / fp32 512 B
```

출력에서 볼 것:

- 차이가 1e-15(부동소수점 반올림 수준)로 **같은 계산**이다. 스트리밍은 "창 전체 버퍼"를 "k − 1 프레임 링버퍼"로 바꾼다 — 여기서는 160 → 16 원소.
- 3절의 DS-CNN을 시간축 스트리밍으로 바꾼다면(stride를 제외하고 단순 계산): stem은 과거 9 프레임 × 10 계수 = 90 B, depthwise 3×3 네 개는 각각 `2 × 64 × 5 = 640 B` → 합 약 2.6 KB가 **arena 밖에 영구히** 있어야 한다. 크지 않지만 arena와 분리해 관리하는 게 핵심이다.
- RNN 상태는 작다(수십~수백 바이트). LLM의 KV-cache는 같은 성격의 상태지만 T에 비례해 수백 MB까지 커진다 — 예: 레이어 16, KV head 8, head_dim 64, T = 2,048, fp16이면 `2 × 16 × 8 × 64 × 2048 × 2 B = 64 MiB`. 자세한 식과 GQA·sliding window는 D5에서.

### 7.3 임베디드 관점

- 상태 버퍼는 링버퍼 인덱스를 돌리는 방식(memmove 없이 head 포인터만 전진)으로 구현한다 — Don이 UART/DMA 링에서 쓴 그대로다. 예제 13은 가독성 때문에 `concatenate`로 복사했다.
- **리셋 정책**이 메모리만큼 중요하다: wake word 감지 후, 착용 해제 후, 오디오 경로 전환 후 상태를 0으로 할지 유지할지. 잘못 유지하면 이전 문맥이 새 입력을 오염시킨다.
- 상태를 arena 안의 "모델 입력·출력 텐서"로 표현하는 런타임도 있다(TFLite의 variable tensor 등). 이 경우 planner가 그 텐서를 **재사용 대상에서 빼는지** 반드시 확인한다.

---

## 8. 메모리 예산 워크시트

### 8.1 빈 템플릿

기기 하나를 설계할 때 채우는 표다. SSD 펌웨어에서 SRAM 맵을 관리하던 스프레드시트와 같은 모양이면 된다.

| 영역 | 항목 | 크기 식 / 출처 | 추정 (KB) | 실측 (KB) | 측정 방법 |
|---|---|---|---|---|---|
| Flash | bootloader | 기존 이미지 | | | 링커 맵 |
| Flash | 앱 코드 + RTOS + 드라이버 | `.text` | | | `size` |
| Flash | 추론 런타임 + 커널 | 링크된 op만 | | | 링커 맵 (심볼별) |
| Flash | 모델 가중치 (모델마다) | params × bytes + 메타데이터 (2절) | | | 모델 파일 크기, `.rodata` |
| Flash | LUT · 상수 | 표 크기 | | | 링커 맵 |
| Flash | OTA 두 번째 슬롯 | A/B면 앱 이미지 × 1 | | | 파티션 표 |
| Flash | 설정 · 캘리브레이션 | 섹터 단위 | | | 파티션 표 |
| SRAM | tensor arena (모델마다, 또는 공유) | peak + scratch + 메타데이터 (3~5절) | | | `arena_used_bytes()` |
| SRAM | 입력 DMA 링 · 전처리 버퍼 | 샘플레이트 × 창 × bytes | | | 설계값 |
| SRAM | 스트리밍 상태 | 7절 | | | 설계값 |
| SRAM | task 스택들 | high-water × (1 + 여유) | | | stack painting |
| SRAM | heap · RTOS · 통신 스택 | 벤더 문서 + 실측 | | | heap 통계 |
| SRAM | 로그 · 텔레메트리 | 설계값 | | | 설계값 |
| SRAM | **여유 (margin)** | 목표 예: 15~25 % | | | |

두 가지 설계 선택이 표 전체를 바꾼다:

- **모델 여러 개의 arena를 공유할 수 있나?** KWS와 IMU 모델이 **절대 동시에 실행되지 않는다면**(같은 태스크에서 순차 실행) arena 하나를 max(두 arena)로 공유할 수 있다. 다른 태스크에서 선점될 수 있으면 각자 필요하다. 상태 버퍼는 어느 경우든 공유 불가.
- **wake 후 오디오를 SoC로 넘기나?** 그렇다면 wake word **이전** 오디오(pre-roll)를 링버퍼로 들고 있어야 한다 — 이게 모델 arena보다 큰 경우가 많다.

### 8.2 채워 보기 — always-on KWS + IMU, SRAM 512 KB · flash 2 MB MCU (예제 14)

아래 시스템 항목 숫자(bootloader, RTOS, BLE 힙 등)는 **설명용 가정**이다. 모델 항목만 이 노트의 코드로 계산한다.

무엇을 확인하는 코드인지: DS-CNN KWS와 작은 1D-CNN IMU 모델의 가중치(예제 1 함수)·arena(예제 5 추정기)를 계산하고, 가정한 시스템 항목과 합쳐 flash/SRAM 사용률을 낸다.

```python
import torch, torch.nn as nn
from d2_mem import trace_tensors, greedy_arena
from models import DSCNN
from ex1_weights import weight_bytes
KB = 1024
imu = nn.Sequential(nn.Conv1d(6, 16, 5, padding=2), nn.ReLU(), nn.Conv1d(16, 32, 5, padding=2),
                    nn.ReLU(), nn.AdaptiveAvgPool1d(1), nn.Flatten(), nn.Linear(32, 5)).eval()
def model_cost(m, x):                                  # (int8 가중치 flash, int8 arena) 바이트
    return weight_bytes(m)[3], greedy_arena(trace_tensors(m, x, 1))[0]
kws_w, kws_a = model_cost(DSCNN().eval(), torch.randn(1, 1, 49, 10))
imu_w, imu_a = model_cost(imu, torch.randn(1, 6, 200))    # 100 Hz × 2 s 창
ovh = lambda a: int(a * 1.10) + 2 * KB                    # 가정: 런타임 메타데이터·scratch +10% +2 KB
app = {"rtos+drivers+app code": 160*KB, "inference runtime+kernels": 90*KB,
       "KWS weights (int8)": kws_w, "IMU weights (int8)": imu_w, "MFCC LUTs": 6*KB}
flash = {"bootloader": 32*KB, "app slot A": sum(app.values()), "app slot B (OTA)": sum(app.values()),
         "config/calibration": 8*KB}
sram = {"rtos/drivers/BLE heap": 96*KB, "task stacks": 24*KB, "audio DMA ping-pong": 1280,
        "audio pre-roll 1.5 s": 48000, "MFCC ring 49x10": 490, "KWS arena": ovh(kws_a),
        "IMU window 6x200 int16": 2400, "IMU arena": ovh(imu_a), "streaming state": KB, "log buffers": 16*KB}
print(f"KWS: weights {kws_w:,} B, arena {kws_a:,} B | IMU: weights {imu_w:,} B, arena {imu_a:,} B")
print(f"app image = {sum(app.values())/KB:.1f} KB")
for title, d, cap in (("FLASH 2 MB", flash, 2048*KB), ("SRAM 512 KB", sram, 512*KB)):
    tot = sum(d.values()); print(title)
    for k, v in d.items(): print(f"  {k:<26}{v/KB:8.1f} KB")
    print(f"  {'TOTAL':<26}{tot/KB:8.1f} KB  used {tot/cap:.1%}, free {(cap-tot)/KB:.1f} KB")
```

```text
KWS: weights 29,072 B, arena 16,000 B | IMU: weights 3,872 B, arena 9,600 B
app image = 288.2 KB
FLASH 2 MB
  bootloader                    32.0 KB
  app slot A                   288.2 KB
  app slot B (OTA)             288.2 KB
  config/calibration             8.0 KB
  TOTAL                        616.3 KB  used 30.1%, free 1431.7 KB
SRAM 512 KB
  rtos/drivers/BLE heap         96.0 KB
  task stacks                   24.0 KB
  audio DMA ping-pong            1.2 KB
  audio pre-roll 1.5 s          46.9 KB
  MFCC ring 49x10                0.5 KB
  KWS arena                     19.2 KB
  IMU window 6x200 int16         2.3 KB
  IMU arena                     12.3 KB
  streaming state                1.0 KB
  log buffers                   16.0 KB
  TOTAL                        219.4 KB  used 42.9%, free 292.6 KB
```

손으로 확인:

```
 KWS 가중치 (int8, 16 B 정렬, 레이어마다 W + bias·scale·zp 각 oc×4 B)
   stem  64×1×10×4 = 2,560  + 3 × 256 =  3,328
   dw    64×9      =   576  + 3 × 256 =  1,344  × 4 =  5,376
   pw    64×64     = 4,096  + 3 × 256 =  4,864  × 4 = 19,456
   fc    12×64     =   768  + 3 × 48  =    912
   합                                             = 29,072 B   ✓
 KWS arena = 8,000 + 8,000 = 16,000 B                          ✓ (3.3절)
 IMU arena = conv1 출력 16×200 + conv2 출력 32×200 = 3,200 + 6,400 = 9,600 B   ✓
 오디오 DMA = 16 kHz × 20 ms × 2 B = 640 B × 2 (ping-pong) = 1,280 B
 pre-roll   = 16 kHz × 1.5 s × 2 B = 48,000 B
```

```svg
<svg viewBox="0 0 680 210" xmlns="http://www.w3.org/2000/svg">
<text x="30" y="30" font-size="13">SRAM 512 KB — KWS + IMU always-on MCU 예산 (가정 숫자, 예제 14)</text> <rect x="30.0" y="45" width="116.2" height="50" fill="#888" fill-opacity="0.55" stroke="#888"/> <text x="88.1" y="67" font-size="12" text-anchor="middle">RTOS·BLE 힙</text><text x="88.1" y="85" font-size="12" text-anchor="middle">96.0 KB</text> <rect x="146.2" y="45" width="29.1" height="50" fill="#888" fill-opacity="0.55" stroke="#888"/> <text x="160.8" y="75" font-size="12" text-anchor="middle">1</text> <rect x="175.3" y="45" width="56.8" height="50" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="203.7" y="75" font-size="12" text-anchor="middle">2</text> <rect x="232.1" y="45" width="23.2" height="50" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/> <text x="243.7" y="75" font-size="12" text-anchor="middle">3</text> <rect x="255.3" y="45" width="14.9" height="50" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/> <text x="262.8" y="75" font-size="12" text-anchor="middle">4</text> <rect x="270.2" y="45" width="19.4" height="50" fill="#888" fill-opacity="0.55" stroke="#888"/> <text x="279.9" y="75" font-size="12" text-anchor="middle">5</text> <rect x="289.6" y="45" width="6.1" height="50" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
<line x1="292.7" y1="95" x2="292.7" y2="106" stroke="currentColor"/><text x="292.7" y="118" font-size="12" text-anchor="middle">6</text> <rect x="295.7" y="45" width="354.3" height="50" fill="none" stroke="currentColor" stroke-dasharray="4 3"/> <text x="472.9" y="67" font-size="12" text-anchor="middle">여유</text><text x="472.9" y="85" font-size="12" text-anchor="middle">292.6 KB</text> <line x1="295.7" y1="38" x2="295.7" y2="102" stroke="#d0564a" stroke-width="2"/> <text x="301.7" y="118" font-size="12">사용 219.4 KB (43%) | 여유 292.6 KB</text> <text x="30" y="145" font-size="12">1: 스택 24.0 KB</text>
<text x="240" y="145" font-size="12">2: 오디오 pre-roll 46.9 KB</text> <text x="450" y="145" font-size="12">3: KWS arena 19.2 KB</text> <text x="30" y="167" font-size="12">4: IMU arena 12.3 KB</text> <text x="240" y="167" font-size="12">5: 로그 버퍼 16.0 KB</text> <text x="450" y="167" font-size="12">6: DMA·MFCC·IMU창·상태 5.1 KB</text> <text x="30" y="198" font-size="12">회색 시스템 · 파랑 오디오 · 주황 추론 arena · 초록 작은 버퍼들 · 점선 여유</text>
</svg>
```

그림 7 — 예제 14의 SRAM 예산을 512 KB 막대 하나에 쌓은 것(폭은 바이트에 비례). 추론 arena 두 개(주황)를 합쳐도 31.5 KB로, 오디오 pre-roll(파랑 46.9 KB)과 시스템 힙(96 KB)보다 작다.

출력에서 볼 것:

- **always-on 모델 자체는 SRAM의 6 %**다. 이런 기기에서 SRAM을 먹는 건 모델이 아니라 **시스템(RTOS·BLE)과 오디오 버퍼**다. 면접에서 "모델이 몇 KB냐"보다 "기기 전체 SRAM 맵이 어떻게 생겼냐"를 말할 수 있으면 펌웨어 경력이 빛나는 지점이다.
- 여유가 293 KB 남으므로 다음 질문이 가능하다: KWS를 더 큰 모델로 키울까(정확도), 사람 감지 비전 모델(5절 MBv2-0.35@96, arena 135 KB + 가중치 458 KB flash)을 추가할 수 있을까? 135 × 1.1 + 2 ≈ 150.5 KB → SRAM 사용 약 72 %, flash는 앱 이미지가 두 슬롯 모두 커지므로 616 + 2 × 458 ≈ 1.5 MB(75 %). **들어간다, 하지만 OTA 두 슬롯 때문에 flash 여유가 빠르게 준다** — 이런 식으로 표 한 장이 설계 토론의 바탕이 된다.
- arena 공유: KWS와 IMU를 같은 태스크에서 순차 실행하면 arena를 max(19.2, 12.3) = 19.2 KB 하나로 공유해 12.3 KB를 아낄 수 있다.

---

## 9. peak를 줄이는 기술

peak 지점의 텐서를 공격해야 효과가 있다(5.3절 교훈). 먼저 5.4절처럼 **peak 순간에 누가 살아 있는지** 보고, 그 텐서를 줄이는 기술을 고른다.

| 기술 | 원리 | 줄어드는 것 | 대가 | 더 볼 곳 |
|---|---|---|---|---|
| 실행 순서 바꾸기 | 가지가 있는 그래프에서 큰 텐서를 빨리 소비하는 순서 선택 | peak-live 자체 | 없음 (컴파일러 옵션) | C6 11절 |
| in-place op | 원소별 op가 입력 버퍼에 결과를 씀 | 해당 op의 출력 버퍼 | 입력을 다시 못 읽음 | 3.5절 |
| op fusion | conv+BN+ReLU, 또는 expand+dw+project를 한 커널로 → 중간 텐서가 메모리에 안 나옴 | 큰 중간 텐서 | 커널 복잡도 | C6 4절 |
| 공간 분할 (patch/tiling) | 초반 고해상도 블록을 공간 타일로 나눠 실행 | 초반 peak (수 배) | 겹침(halo) 재계산 | C7 7.4절 |
| 활성값 정밀도 낮추기 | fp32 → int8, int16 → int8 | 모든 활성값 비례 | 정확도 (C2 확인) | 5.5절, C1 |
| 해상도 · 창 길이 줄이기 | 활성값 ∝ H × W (또는 T, attention은 T²) | 전체 곡선 | 정확도 | 5.5절 |
| expand 비율 · 채널 줄이기 | peak 블록의 채널 수 축소 | peak 블록 | 정확도 | C7 4절 |
| 재계산 (recomputation) | 오래 살아야 하는 텐서를 저장하지 않고 필요할 때 다시 계산 | 긴 lifetime 텐서 (skip 등) | 연산 증가 | 아래 |
| 스트리밍 | 창 전체 대신 프레임 단위 + 상태 | 입력 창·중간 텐서 | 상태 관리, 설계 제약 (causal) | 7절 |
| attention fusion | score 행렬 T × T를 행 단위로 계산·소비 | T² 텐서 | 커널 지원 필요 | 5.4절, D5 |
| 무거운 텐서 spill | 일부 텐서를 느린 메모리(외장 RAM/DRAM)에 둠 | on-chip SRAM | 대역폭·latency·에너지 | D3, D7 |

**재계산** 한 줄 설명: 학습에서는 gradient checkpointing으로 유명하지만, 추론에서도 "skip 텐서를 블록 끝까지 들고 있는 대신 블록 끝에서 다시 만든다"는 식으로 쓸 수 있다. 예제 3에서 x0(8 KB)를 버렸다가 다시 만들 수 있다면 peak가 72 → 64 KB가 된다. 다만 x0를 만든 연산의 입력이 여전히 살아 있어야 하므로 항상 가능한 건 아니고, MCU 추론에서는 patch 기반 실행 안에서 halo 영역을 재계산하는 형태로 가장 많이 나타난다.

---

## 10. 임베디드 관점에서 다시 보기

**한 장의 식으로**:

```
 FLASH ≥ boot + code + runtime + Σ_models weight_bytes + LUT + (A/B면 앱 × 2) + config
 SRAM  ≥ .data + .bss(arena들 + 상태 + DMA 버퍼 + 로그) + Σ stacks + heap + margin
         arena_model = peak_live + scratch + 메타데이터 + 정렬
         peak_live   = max_t Σ { size(x) : x가 t에 살아 있음 }
```

**Don의 경험과 1:1 대응**:

| SSD 펌웨어에서 | 모델 배포에서 |
|---|---|
| FTL 매핑 테이블을 SRAM/DRAM에 나눠 배치 | 가중치 flash(XIP)/SRAM, 활성값 SRAM, 큰 모델 DRAM |
| 커맨드 버퍼 풀을 재사용 (free list) | tensor arena에서 lifetime이 안 겹치는 텐서끼리 주소 공유 |
| 링커 스크립트로 핫 코드 ITCM 배치 | 핫 커널·작은 가중치를 TCM에 배치 |
| DMA ping-pong 버퍼 | chain 모델의 peak = 이웃 텐서 두 개 |
| stack overflow 디버깅 (0xA5 칠하기) | 추론 태스크 스택 high-water |
| NAND 쓰기는 느려서 런타임에 안 씀 | flash의 가중치는 읽기 전용, 활성값은 RAM |
| 전원 끊겨도 보존할 메타데이터 | 스트리밍 상태는 추론 사이에 보존 (arena 재사용 금지) |

**arena 배치를 C로 읽으면**: planner가 계산한 offset은 결국 `arena + offset` 포인터다. DS-CNN처럼 chain인 모델이면 greedy 결과는 8,000 B 슬롯 두 개를 번갈아 쓰는 ping-pong 배치가 되고(입력 490 B는 stem 출력과 동시에 살아 있으므로 두 번째 슬롯 자리에 들어간다), arena는 `static uint8_t arena[16000]`에 정렬 속성을 붙인 `.bss` 배열 하나다. 런타임이 자동으로 하는 일을 손으로 쓰면 이게 전부다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 가중치 배열에 `const` 누락 | flash와 RAM이 동시에 모델 크기만큼 늘어남, 부팅이 느려짐 | `.data`로 가서 crt0가 RAM에 복사 | `const` + 링커 맵에서 `.rodata` 확인 |
| arena를 초기값으로 선언 (`= {1}` 등) | 이미지 크기가 arena만큼 커짐 | `.bss`가 아닌 `.data`로 감 | 초기값 없이 선언, 필요하면 런타임에 memset |
| params × 1로 int8 flash 추정 | 실제 파일이 10~20 % 큼 | scale·zp·int32 bias·정렬·메타데이터 | 2절 식 + 모델 파일 크기로 교차 확인 |
| 활성값을 "모든 텐서 합"으로 추정 | SRAM이 모자라다고 잘못 판단해 모델을 과하게 줄임 | lifetime 무시 | peak-live로 계산 (3절, 5절) |
| peak만 보고 scratch 무시 | `AllocateTensors()` 실패, 몇 KB 부족 | im2col·softmax 임시 버퍼 | scratch를 가상 텐서로 더함 (4.3절) |
| 스트리밍 상태를 arena 안 임시 텐서로 둠 | 가끔 인식률 급락, 재현 어려움 | 다른 텐서가 상태 자리를 덮어씀 | 상태는 arena 밖 전용 버퍼 또는 variable tensor로 |
| 호스트 eager 메모리를 배포 메모리로 보고 | 숫자가 3배 이상 큼 (예제 10) | BN·ReLU 미fuse, conv workspace, allocator 캐시 | 배포 그래프 기준 추정 + 타깃 런타임 측정 |
| `tracemalloc`으로 torch 메모리 측정 | 텐서가 0 MB로 보임 | torch allocator는 tracemalloc에 안 알림 (예제 9) | RSS, torch profiler, 타깃 측정 |
| 스택 high-water를 평범한 입력으로만 측정 | 현장에서 드물게 hard fault | 최악 경로·printf·인터럽트 중첩 미포함 | 최악 조건 테스트 + `-fstack-usage` + 여유 |
| 해상도를 조금 올림 | arena가 예상보다 훨씬 커짐 | 활성값 ∝ 해상도² | 5.5절 sweep으로 미리 표 만들기 |
| 두 모델이 arena를 공유하는데 다른 태스크에서 실행 | 간헐적 오출력 | 선점 시 서로의 활성값을 덮어씀 | 공유는 같은 태스크 순차 실행일 때만 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "How do you estimate the tensor arena size for a model on an MCU?"

**A.** 배포 그래프(BN fold, ReLU fuse 후)에서 텐서마다 크기와 lifetime을 구하고, 매 step 살아 있는 텐서 합의 최댓값 — peak-live — 를 하한으로 잡는다. 여기에 커널 scratch(im2col 등), 텐서 메타데이터, 정렬을 더하면 arena 추정치다. 설계 단계에서는 이걸 스크립트로 계산해 후보 모델을 거르고, 확정은 런타임이 보고하는 실제 사용량(TFLM이라면 `arena_used_bytes()`)으로 하고 여유를 붙인다.

> I compute tensor sizes and lifetimes on the deployment graph — after BN folding and activation fusion — and take the peak of live bytes over the execution order as the lower bound. Then I add kernel scratch like im2col buffers, runtime metadata and alignment. I use that estimate to screen candidate models, but I size the final arena from what the runtime actually reports, for example `arena_used_bytes()` in TFLite Micro, plus a margin.

**Q.** "Should the weights live in flash or in RAM?"

**A.** 기본은 flash에 `const`로 두고 XIP로 읽는다 — SRAM을 0 쓰기 때문이다. 대가는 flash wait state와 캐시 미스로 인한 느리고 흔들리는 latency다. 가중치 재사용이 적어서 flash 대역폭에 막히는 레이어나 deadline이 빡빡한 핫 레이어만 SRAM/TCM으로 복사하거나 DMA double buffering으로 prefetch 한다. SoC에서는 DRAM에 mmap 해 두고 NPU가 tile 단위로 로컬 SRAM에 올린다.

> By default the weights stay in flash as const data and are read in place, which costs zero SRAM. The price is flash wait states and cache misses, so latency is slower and less deterministic. I only copy hot layers into SRAM or TCM — or prefetch the next layer with DMA double buffering — when profiling shows a layer is limited by flash bandwidth or a deadline is tight. On a bigger SoC the weights are memory-mapped in DRAM and the NPU streams tiles into its local SRAM.

**Q.** "Why do residual connections increase peak memory?"

**A.** skip 텐서는 블록 입력인데 블록 끝의 add에서 다시 읽히므로 블록 전체 동안 살아 있어야 한다. 블록 안에서 채널을 펼친 가장 큰 두 텐서가 동시에 살아 있는 순간에도 skip이 얹혀서 peak가 그만큼 오른다. 그래서 residual 블록의 peak는 skip + 블록 안의 가장 큰 이웃 쌍이다. 고해상도 초반 블록에서 특히 비싸고, 해결책은 patch 기반 실행, 재계산, 또는 그 블록의 expand 비율 조정이다.

> The skip tensor is the block input, and it's read again by the add at the end of the block, so its lifetime spans the whole block. At the moment the two largest expanded tensors inside the block are both alive, the skip tensor is alive too, so it adds directly to the peak — the block's peak is the skip tensor plus the largest adjacent pair inside. It hurts most in early high-resolution blocks, and the fixes are patch-based execution, recomputation, or reducing the expansion ratio of that block.

**Q.** "How do you find the stack high-water mark of your inference task?"

**A.** stack painting이다. 태스크 시작 전에 스택을 0xA5 같은 패턴으로 칠하고, 최악 조건 — 가장 긴 추론 경로, 로그 켠 상태, 인터럽트 부하 — 으로 돌린 뒤 스택 바닥부터 패턴이 남은 바이트를 센다. FreeRTOS의 `uxTaskGetStackHighWaterMark()`가 이 방식이다. 측정은 실행한 경로만 반영하므로 `-fstack-usage`와 호출 그래프로 정적 상한도 같이 보고, 여유를 붙여 스택 크기를 정한다. 커널이 큰 지역 배열을 쓰면 그대로 스택 비용이 되니 scratch는 arena에서 받게 한다.

> Stack painting. Before the task starts, I fill its stack with a known pattern like 0xA5, run the worst case — longest inference path, logging enabled, interrupt load — and then count from the bottom of the stack how many bytes still hold the pattern. That's exactly what FreeRTOS `uxTaskGetStackHighWaterMark()` does. Since it only reflects paths that actually ran, I cross-check with `-fstack-usage` and the call graph, and add margin. I also make kernels take scratch from the arena instead of large local arrays.

**Q.** "Your model's weights are 450 KB and activations peak at 135 KB. Will it fit on a part with 256 KB SRAM and 1 MB flash?"

**A.** 가중치는 flash에 XIP로 두면 458 KB 정도로 1 MB에 들어간다 — 단, 앱 코드와 OTA 두 번째 슬롯까지 합치면 1 MB가 빠듯할 수 있으니 파티션을 먼저 본다. SRAM은 arena 135 KB에 scratch·메타데이터 10~15 %, 그리고 시스템 힙·스택·오디오나 카메라 버퍼가 붙는다. 256 KB라면 모델 외에 100 KB 남짓이 남는데, 카메라 프레임 버퍼 하나(96×96×3 = 27 KB)는 들어가지만 BLE 스택까지 있으면 빠듯하다. 그래서 워크시트로 전체를 합쳐 보고, 모자라면 해상도를 먼저 줄인다 — 활성값은 해상도²에 비례하니까.

> The weights can stay in flash, about 460 KB, which fits in 1 MB — but with application code and a second OTA slot, 1 MB gets tight, so I'd check the partition map first. In SRAM, the 135 KB arena grows by maybe 10 to 15 percent for scratch and metadata, and it shares the part with the system heap, stacks and sensor buffers. On 256 KB that leaves around 100 KB for everything else, which is tight with a BLE stack. I'd put it all in a budget sheet, and if it doesn't fit I'd reduce input resolution first, because activation memory scales with resolution squared.

**Q.** "What persistent memory does a streaming keyword-spotting model need?"

**A.** 추론 사이에 보존해야 하는 상태다: 시간축 conv마다 과거 k−1 프레임의 링버퍼, RNN이라면 hidden(LSTM은 cell까지), 특징 추출의 FFT overlap. DS-CNN급이면 수 KB 수준이지만 핵심은 arena와 분리하는 것이다 — arena는 추론마다 재사용되니까 상태가 들어가면 덮어쓰인다. 그리고 wake 후나 경로 전환 시 리셋 정책을 정한다. 트랜스포머라면 같은 역할이 KV-cache이고 컨텍스트 길이에 비례해 커진다.

> State that survives between invocations: a ring buffer of the last k minus one frames for every temporal convolution, the hidden state for an RNN — plus the cell state for an LSTM — and the FFT overlap in the feature front end. For a DS-CNN-sized model that's a few kilobytes, but the key is to keep it outside the arena, because the arena is reused every invocation and would overwrite it. I also define a reset policy, for example after a wake event. In a transformer the same role is played by the KV-cache, which grows with context length.

---

## 13. 직접 해보기

1. 손계산: 3×3 conv, 32 → 64채널을 int8 per-channel(W int8, bias int32, scale fp32, zp int32, 정렬 무시)로 저장하면 몇 바이트인가? fp32(W + bias)와의 비율은?
정답: W 18,432 + 64 × 12 = 768 → 19,200 B. fp32 = 73,728 + 256 = 73,984 B. 비율 약 3.85배.

2. 손계산: 입력 `1×32×40×40` int8, 1×1 conv로 96채널 expand → 3×3 depthwise stride 1 → 1×1 project로 32채널, 그리고 입력과 residual add. 각 step의 live 합과 peak는? skip이 없다면?
정답: expand 순간 51,200 + 153,600 = 204,800 / dw 순간 x(51,200) + 153,600 + 153,600 = 358,400 B (peak) / skip 없으면 dw 순간 307,200 B.

3. 코드: 예제 5의 추정기에 **scratch를 가상 텐서로 더하는** 기능을 넣어라. Conv2d 중 k > 1이고 depthwise가 아닌 op마다 `[i, i]` 수명의 "2열 int16" 버퍼를 추가하고, MobileNetV2-0.35@96의 arena가 바뀌는지 확인하라.
힌트: `bufs[n.name + "_scratch"] = dict(size=..., first=i, last=i, op="scratch")`. 108 B짜리라 arena는 거의 안 바뀐다. full im2col(62,208 B)로 바꾸면 첫 conv step의 live가 약 124 KB가 되지만 peak(135 KB)는 넘지 않는다.

4. 코드: 예제 8을 DS-CNN으로 바꿔, 채널 수 c ∈ {32, 64, 128, 172}와 입력 프레임 수(49, 98)에 대해 int8 arena와 int8 가중치를 표로 만들어라. 무엇이 채널에 1차, 무엇이 2차로 늘어나는가?
힌트: arena = 2 × c × H × W로 c에 1차, pointwise 가중치는 c²에 비례(2차). 프레임 수는 arena에만 영향.

5. C: 예제 12의 `layer()`에서 VLA 대신 `static` 배열(또는 arena에서 받은 포인터)을 쓰도록 바꾸고 high-water가 어떻게 변하는지 확인하라. 재진입(reentrancy) 측면에서 무엇을 잃는가?
힌트: 스택 사용이 프레임 크기만 남는다. 대신 두 태스크가 동시에 `layer()`를 호출하면 버퍼를 공유해 깨진다 — arena 배치와 같은 "동시에 살아 있지 않아야 공유 가능" 규칙이다.

6. 워크시트: 예제 14에 "사람 감지 MBv2-0.35@96" 모델을 추가하고, (a) arena를 KWS와 공유하지 않을 때, (b) KWS·IMU·비전이 같은 태스크에서 순차 실행되어 arena 하나를 공유할 때 SRAM 사용률을 각각 계산하라.
정답: (a) 비전 arena 135 KB × 1.1 + 2 KB ≈ 150.5 KB 추가 → 약 369.9 KB, 72 %. (b) 공유 arena = max(150.5, 19.2, 12.3) = 150.5 KB → 219.4 − 19.2 − 12.3 + 150.5 ≈ 338.4 KB, 66 %.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| tensor arena | 텐서 전용 정적 메모리 영역 | 런타임이 모든 중간 텐서를 이 배열 안에 배치한다 (보통 `.bss`) |
| lifetime / liveness | 텐서가 살아 있는 구간 | [만들어진 step, 마지막으로 읽힌 step] |
| peak-live | 동시에 살아 있는 바이트의 최댓값 | arena 크기의 하한 |
| naive 합 | 텐서마다 전용 버퍼일 때의 합 | 재사용이 없을 때의 상한 |
| memory planner | 텐서에 arena offset을 정하는 알고리즘 | lifetime이 겹치면 주소도 겹치지 않게 (C6) |
| in-place | 출력을 입력 버퍼에 덮어쓰기 | 원소별 op + 입력이 여기서 마지막으로 읽힐 때 |
| scratch buffer | 커널 계산 중에만 쓰는 임시 메모리 | im2col, softmax 임시 등. op 한 번 동안만 산다 |
| im2col | 입력 patch를 열로 펼치는 변환 | conv를 GEMM으로 바꾼다. 전부 펼치면 크다 |
| XIP | execute-in-place | flash를 복사 없이 주소로 직접 읽는 방식 |
| TCM | tightly coupled memory | CPU에 붙은 0 wait state SRAM (ITCM/DTCM) |
| VTCM | vector TCM | Hexagon DSP/NPU의 로컬 고속 메모리 |
| `.text` / `.rodata` | 코드 / 읽기 전용 데이터 섹션 | flash에 놓인다. 가중치는 `.rodata` |
| `.data` | 초기값 있는 전역 변수 | flash에 원본 + RAM에 사본 (두 번 셈) |
| `.bss` | 초기값 없는 전역 변수 | RAM만, 부팅 때 0. arena는 여기 |
| linker map | 링커가 쓰는 심볼별 배치 보고서 | 어떤 심볼이 어느 섹션에 몇 바이트인지 |
| stack painting | 스택을 패턴으로 칠해 사용량 측정 | 칠이 남은 부분을 세서 high-water를 얻는다 |
| high-water mark | 관측된 최대 사용량 | 스택·arena·heap 모두에 쓰는 말 |
| RSS | resident set size | 프로세스가 실제 물리 메모리에 올린 양 |
| persistent state | 추론 사이에 보존되는 상태 | RNN hidden, conv 링버퍼, KV-cache |
| pre-roll | 트리거 이전 구간의 버퍼 | wake word 앞 오디오를 SoC로 넘기기 위해 보관 |
| A/B 슬롯 | OTA용 이미지 두 벌 | 업데이트 실패 시 롤백. flash를 앱 크기만큼 더 씀 |

---

## 15. 요약 & 체크리스트

추론 배포의 메모리는 **읽기 전용(가중치·코드·LUT → flash/DRAM)**과 **읽기/쓰기(활성값·scratch → arena, 상태·DMA·스택 → 전용 SRAM)**로 나뉜다. 가중치는 params × bytes에 per-channel 메타데이터·int32 bias·정렬이 붙고, 작은 모델·depthwise일수록 그 비율이 크다. 활성값은 텐서마다 lifetime을 세서 **peak-live**를 구하는 것이 핵심이며, chain 모델에서는 가장 큰 이웃 쌍, residual 블록에서는 거기에 skip 텐서가 더해진다. 좋은 planner는 arena를 peak-live에 맞추고, 실제 arena에는 scratch·메타데이터·정렬이 더해진다. 이 노트의 fx 추정기로 MobileNetV2-0.35@96은 int8 arena 135 KB(가중치 458 KB), DS-CNN KWS는 15.6 KB(가중치 28 KB), transformer 블록(d64·T64)은 40 KB가 나왔고, fp32는 정확히 4배, 활성값은 해상도²에 비례했다. 호스트 Python 측정(tracemalloc, eager profiler)은 배포 메모리가 아니고, 펌웨어에서는 `size`·링커 맵·stack painting·런타임 arena 보고로 확인한다. 마지막으로 기기 전체 예산표에 넣어 보면, always-on MCU에서 SRAM을 먹는 건 모델보다 시스템과 오디오 버퍼인 경우가 많다.

- [ ] 메모리 계층(TCM/SRAM/flash/DRAM/NPU SRAM)의 대략적인 크기와 각각에 무엇을 두는지 말할 수 있다
- [ ] conv 하나의 int8 per-channel 저장 바이트(W + bias + scale + zp)를 손으로 계산할 수 있다
- [ ] depthwise에서 메타데이터가 가중치보다 커지는 이유를 설명할 수 있다
- [ ] 텐서 lifetime을 세서 step별 live 합과 peak를 손으로 계산할 수 있다
- [ ] residual 연결이 peak를 올리는 이유를 lifetime 그림으로 설명할 수 있다
- [ ] im2col full/partial scratch 크기를 계산하고 arena 추정에 넣을 수 있다
- [ ] `torch.fx`로 naive / peak-live / arena를 계산하는 추정기를 짜고 한계를 말할 수 있다
- [ ] `size`·링커 맵에서 `.text/.rodata/.data/.bss`를 읽고 가중치·arena가 제자리에 있는지 확인할 수 있다
- [ ] stack painting으로 high-water를 재고 printf·지역 배열의 비용을 설명할 수 있다
- [ ] 기기 하나의 flash/SRAM 예산 워크시트를 채우고 peak를 줄이는 기술을 고를 수 있다

---

## 참고 자료

- PyTorch 문서 — `torch.fx` (symbolic tracing, `ShapeProp`): https://pytorch.org/docs/stable/fx.html
- PyTorch 문서 — `torch.profiler` (`profile_memory`): https://pytorch.org/docs/stable/profiler.html
- Python 문서 — `tracemalloc`: https://docs.python.org/3/library/tracemalloc.html , `resource`: https://docs.python.org/3/library/resource.html
- TensorFlow Lite for Microcontrollers (arena, memory planner, `arena_used_bytes`): https://github.com/tensorflow/tflite-micro
- CMSIS-NN (MCU용 int8 커널, 커널별 buffer size 함수): https://github.com/ARM-software/CMSIS-NN
- FreeRTOS — `uxTaskGetStackHighWaterMark()`: https://www.freertos.org/uxTaskGetStackHighWaterMark.html
- Zhang et al., "Hello Edge: Keyword Spotting on Microcontrollers" (2017, DS-CNN KWS): https://arxiv.org/abs/1711.07128
- Lin et al., "MCUNet: Tiny Deep Learning on IoT Devices" (NeurIPS 2020), "MCUNetV2: Memory-Efficient Patch-based Inference" (2021): https://arxiv.org/abs/2007.10319 , https://arxiv.org/abs/2110.15352
- Sandler et al., "MobileNetV2: Inverted Residuals and Linear Bottlenecks" (CVPR 2018): https://arxiv.org/abs/1801.04381
- MIT 6.5940 TinyML and Efficient Deep Learning (Song Han) — 메모리·MCU 강의: https://hanlab.mit.edu/courses/2024-fall-65940
- 이 노트와 이어지는 노트: B6 10절, B8 4절, C1 5절, C6 10~11절, C7 7절, D3 (roofline), D5 (KV-cache), D7 (에너지)
