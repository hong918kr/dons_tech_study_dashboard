# C4. Pruning과 Sparsity — 0을 만들면 정말 빨라지나

> **이 노트를 다 읽으면**: magnitude pruning(global·layer-wise·iterative)을 직접 돌리고 accuracy–sparsity 곡선을 읽을 수 있다 · CSR·bitmask·2:4 포맷의 저장 크기를 손으로 계산하고 "몇 % sparsity부터 이득인가"를 유도할 수 있다 · 90% unstructured sparsity가 왜 10배 빨라지지 않는지 C 측정값으로 설명할 수 있다 · MCU용 CNN을 structured pruning으로 줄이는 절차와 함정(BN·residual 의존성)을 말할 수 있다
> **JD 연결**: "optimize models for on-device inference", "co-design model architectures with hardware", "evaluate silicon (NPU/DSP/GPU)" · study_prep_list C4 — unstructured vs structured(채널) pruning, **N:M sparsity (2:4)**, "sparsity가 실제로 빨라지는 조건" (HW 지원) · 연결: M4(TOPS에 숨은 sparsity 조건), E5(NPU), D1·D2(MAC·메모리)
> **Don 기준 난이도**: 희소 자료구조·캐시·SIMD·분기 예측·측정 기반 판단은 이미 강함 / "어떤 weight를 지워도 되는가"의 판단 기준, 재학습(fine-tune)으로 복구하는 흐름, N:M·LLM pruning은 새로 배움
> **선행 노트**: A1(행렬곱·GEMV·norm), A3(gradient·학습 루프), B2(conv 파라미터·MAC 공식, BN), C1(int8 양자화 — 10절에서 같이 쓴다). 참고: B4(attention head), C2(PTQ/QAT), C5(distillation)

---

## 0. 큰 그림 — 이게 왜 필요한가

학습이 끝난 신경망의 weight를 히스토그램으로 그려 보면 0 근처에 몰려 있다. "이 작은 값들은 출력에 거의 기여하지 않으니 0으로 만들어 버리자" — 이것이 **pruning(가지치기)** 이다. 그 결과 0이 많아진 상태를 **sparsity(희소성)** 라고 부른다. sparsity 90%는 "weight의 90%가 0"이라는 뜻이고, 반대로 0이 아닌 비율(10%)을 **density**라고 한다.

여기까지는 쉽다. edge 엔지니어에게 진짜 질문은 다음이다.

> **0을 많이 만들었다. 그래서 이 칩에서 실제로 빨라지나? 메모리가 줄어드나?**

답은 "**0이 어떤 모양으로 모여 있느냐**"와 "**HW·커널이 그 모양을 이용할 줄 아느냐**"에 달려 있다. 0의 모양(granularity)을 먼저 그림으로 보자.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<rect x="20" y="40" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="36" y="40" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="52" y="40" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="68" y="40" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="84" y="40" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="100" y="40" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="116" y="40" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="132" y="40" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="20" y="56" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="36" y="56" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="52" y="56" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="68" y="56" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="84" y="56" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="100" y="56" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="116" y="56" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="132" y="56" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="20" y="72" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="36" y="72" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/>
<rect x="52" y="72" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="68" y="72" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="84" y="72" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="100" y="72" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="116" y="72" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="132" y="72" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="20" y="88" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="36" y="88" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="52" y="88" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="68" y="88" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="84" y="88" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="100" y="88" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="116" y="88" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="132" y="88" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="20" y="104" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="36" y="104" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="52" y="104" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="68" y="104" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/>
<rect x="84" y="104" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="100" y="104" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="116" y="104" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="132" y="104" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="20" y="120" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="36" y="120" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="52" y="120" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="68" y="120" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="84" y="120" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="100" y="120" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="116" y="120" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="132" y="120" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="20" y="136" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="36" y="136" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="52" y="136" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="68" y="136" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="84" y="136" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="100" y="136" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/>
<rect x="116" y="136" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="132" y="136" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="20" y="152" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="36" y="152" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="52" y="152" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="68" y="152" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="84" y="152" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="100" y="152" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="116" y="152" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <rect x="132" y="152" width="14" height="14" fill="#4a7bd0" stroke="#4a7bd0" stroke-width="1" fill-opacity="0.85"/> <text x="83" y="28" font-size="13" text-anchor="middle">dense (0%)</text> <rect x="185" y="40" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="201" y="40" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="217" y="40" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="233" y="40" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="249" y="40" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="265" y="40" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="281" y="40" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/>
<rect x="297" y="40" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="185" y="56" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="201" y="56" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="217" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="233" y="56" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="249" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="265" y="56" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="281" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="297" y="56" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="185" y="72" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="201" y="72" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="217" y="72" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="233" y="72" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="249" y="72" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="265" y="72" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="281" y="72" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="297" y="72" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="185" y="88" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/>
<rect x="201" y="88" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="217" y="88" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="233" y="88" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="249" y="88" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="265" y="88" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="281" y="88" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="297" y="88" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="185" y="104" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="201" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="217" y="104" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="233" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="249" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="265" y="104" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="281" y="104" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="297" y="104" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="185" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="201" y="120" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="217" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/>
<rect x="233" y="120" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="249" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="265" y="120" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="281" y="120" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="297" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="185" y="136" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="201" y="136" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="217" y="136" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="233" y="136" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="249" y="136" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="265" y="136" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="281" y="136" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="297" y="136" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="185" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="201" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="217" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="233" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="249" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/>
<rect x="265" y="152" width="14" height="14" fill="#e08a3c" stroke="#e08a3c" stroke-width="1" fill-opacity="0.85"/> <rect x="281" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="297" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <text x="248" y="28" font-size="13" text-anchor="middle">unstructured 50%</text> <rect x="350" y="40" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="366" y="40" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="382" y="40" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="398" y="40" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="414" y="40" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="430" y="40" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="446" y="40" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="462" y="40" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="350" y="56" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="366" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="382" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="398" y="56" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="414" y="56" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="430" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="446" y="56" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/>
<rect x="462" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="350" y="72" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="366" y="72" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="382" y="72" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="398" y="72" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="414" y="72" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="430" y="72" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="446" y="72" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="462" y="72" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="350" y="88" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="366" y="88" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="382" y="88" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="398" y="88" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="414" y="88" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="430" y="88" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="446" y="88" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="462" y="88" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="350" y="104" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/>
<rect x="366" y="104" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="382" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="398" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="414" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="430" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="446" y="104" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="462" y="104" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="350" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="366" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="382" y="120" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="398" y="120" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="414" y="120" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="430" y="120" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="446" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="462" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="350" y="136" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="366" y="136" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="382" y="136" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/>
<rect x="398" y="136" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="414" y="136" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="430" y="136" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="446" y="136" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="462" y="136" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="350" y="152" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="366" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="382" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="398" y="152" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="414" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="430" y="152" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <rect x="446" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="462" y="152" width="14" height="14" fill="#3f9a6b" stroke="#3f9a6b" stroke-width="1" fill-opacity="0.85"/> <text x="413" y="28" font-size="13" text-anchor="middle">2:4 (N:M) 50%</text> <rect x="515" y="40" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="531" y="40" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="547" y="40" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="563" y="40" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/>
<rect x="579" y="40" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="595" y="40" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="611" y="40" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="627" y="40" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="515" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="531" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="547" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="563" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="579" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="595" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="611" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="627" y="56" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="515" y="72" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="531" y="72" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="547" y="72" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="563" y="72" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="579" y="72" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="595" y="72" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/>
<rect x="611" y="72" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="627" y="72" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="515" y="88" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="531" y="88" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="547" y="88" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="563" y="88" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="579" y="88" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="595" y="88" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="611" y="88" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="627" y="88" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="515" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="531" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="547" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="563" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="579" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="595" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="611" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="627" y="104" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/>
<rect x="515" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="531" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="547" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="563" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="579" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="595" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="611" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="627" y="120" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="515" y="136" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="531" y="136" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="547" y="136" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="563" y="136" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="579" y="136" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="595" y="136" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="611" y="136" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="627" y="136" width="14" height="14" fill="#d0564a" stroke="#d0564a" stroke-width="1" fill-opacity="0.85"/> <rect x="515" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="531" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/>
<rect x="547" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="563" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="579" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="595" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="611" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <rect x="627" y="152" width="14" height="14" fill="none" stroke="#888" stroke-width="1" fill-opacity="0.85"/> <text x="578" y="28" font-size="13" text-anchor="middle">channel(행) 50%</text> <line x1="413" y1="36" x2="413" y2="170" stroke="currentColor" stroke-width="1.5" stroke-dasharray="3,2"/> <text x="83" y="192" font-size="12" text-anchor="middle">저장·연산 모두 규칙적</text> <text x="83" y="208" font-size="12" text-anchor="middle">SIMD·NPU 그대로</text> <text x="248" y="192" font-size="12" text-anchor="middle">0 위치가 제멋대로</text> <text x="248" y="208" font-size="12" text-anchor="middle">index 필요 · gather</text> <text x="413" y="192" font-size="12" text-anchor="middle">4칸마다 정확히 2개</text> <text x="413" y="208" font-size="12" text-anchor="middle">2-bit index · HW 지원 시 2×</text> <text x="578" y="192" font-size="12" text-anchor="middle">행 자체가 사라짐</text> <text x="578" y="208" font-size="12" text-anchor="middle">작은 dense 행렬</text> <text x="20" y="236" font-size="12">채운 칸 = 남은 weight · 빈 칸 = 0 · 행 = 출력 채널, 열 = 입력 채널 (Linear weight [out, in])</text>
</svg>
```

그림 1 — pruning granularity 네 가지. 왼쪽에서 오른쪽으로 갈수록 "자유도는 줄고 HW 친화도는 올라간다". unstructured는 정확도를 가장 잘 지키지만 0의 위치가 제멋대로라 dense 커널은 그 0을 그냥 곱한다. channel pruning은 행(출력 채널)이 통째로 사라져 **더 작은 dense 행렬**이 되므로 어떤 HW에서도 빨라진다. 2:4는 그 중간 — 규칙이 있어서 전용 HW가 있으면 2배.

펌웨어 비유로 정리하면 이렇다.

```
pruning 종류            펌웨어 비유                                  실제 이득 조건
─────────────────────  ──────────────────────────────────────────  ──────────────────────────
unstructured           테이블 여기저기 "사용 안 함" 표시              희소 포맷 + 매우 높은 sparsity
                       (루프는 그대로 돌면서 if (entry==0) skip)      또는 압축 저장(flash만 이득)
N:M (2:4)              "4개 슬롯 중 2개만 유효" 고정 규칙 → HW가      전용 HW (sparse tensor core 등)
                       mux로 유효 슬롯만 뽑아 MAC
structured (채널/head) 사용 안 하는 테이블 행을 아예 빌드에서 제거     항상 (더 작은 dense 연산)
                       → 배열 크기·루프 횟수 자체가 줄어듦
```

이 노트의 순서: 왜 잘라도 되나(1절) → unstructured magnitude pruning과 재학습(2절) → sparse 저장 포맷(3절) → C로 CSR vs dense 측정(4절) → structured pruning(5절) → N:M 2:4(6절) → activation sparsity(7절) → LLM pruning, Wanda(8절) → "sparse TOPS" 읽는 법(9절) → pruning + quantization(10절) → 임베디드 관점(11절).

> 실행 환경: Python 예제는 이 폴더의 `.venv/bin/python` (Python 3.9, torch 2.8.0, numpy 2.0.2, scikit-learn 1.6.1, scipy 1.13.1)로 실제 실행한 출력이다. C 예제는 Apple M2(clang 21, macOS)에서 `cc -std=c11 -Wall -Wextra -O2 ... -lm`으로 경고 0개로 컴파일했다. `c4common.py`(데이터·MLP·학습 함수)와 `c4cnn.py`(CNN·채널 pruning)는 같은 디렉터리에 저장해 두고 뒤 예제에서 import 한다. 데이터는 scikit-learn에 내장된 **digits**(8×8 손글씨 숫자 1,797장, 오프라인)다. test set이 540장이라 accuracy 0.2%p(= 1장) 단위 차이는 잡음으로 봐야 한다.

---

## 1. 왜 잘라도 되나 — weight는 생각보다 많이 놀고 있다

### 1.1 직관: over-parameterization

신경망은 보통 **필요한 것보다 훨씬 많은 파라미터**로 학습한다. 파라미터가 넉넉해야 최적화(A3)가 쉽기 때문이다 — 길이 많을수록 좋은 골짜기로 내려가기 쉽다. 학습이 끝나면 그 중 상당수는 "거의 0" 또는 "다른 weight와 역할이 겹치는" 상태가 된다.

펌웨어 비유: 디버그 빌드에 넣어 둔 로그·assert·중복 체크. 개발(학습) 중엔 도움이 됐지만 양산(추론)에는 필요 없다. 다만 **어느 게 정말 필요 없는지**는 측정해 봐야 안다 — 이게 pruning의 핵심 난이도다.

### 1.2 공통 준비물 — digits MLP

이 노트 대부분의 실험은 아래 작은 MLP(64→256→256→10, weight 84,480개)로 한다. 이 파일을 먼저 저장해 둔다.

```python
# c4common.py — 이 노트의 공통 준비물: digits 데이터 + 작은 MLP + 학습/평가 함수
import torch, torch.nn as nn
from sklearn.datasets import load_digits
from sklearn.model_selection import train_test_split

def get_data():
    X, y = load_digits(return_X_y=True)              # 1797장, 8×8 흑백 숫자 → 64차원
    X = torch.tensor(X / 16.0, dtype=torch.float32)
    y = torch.tensor(y)
    return train_test_split(X, y, test_size=0.3, random_state=0, stratify=y)

def make_mlp():
    return nn.Sequential(nn.Linear(64, 256), nn.ReLU(),
                         nn.Linear(256, 256), nn.ReLU(), nn.Linear(256, 10))

def train(model, Xtr, ytr, epochs=40, lr=1e-3, masks=None, seed=0):
    g = torch.Generator().manual_seed(seed)
    opt = torch.optim.Adam(model.parameters(), lr=lr)
    for _ in range(epochs):
        perm = torch.randperm(len(Xtr), generator=g)
        for i in range(0, len(Xtr), 64):
            idx = perm[i:i + 64]
            loss = nn.functional.cross_entropy(model(Xtr[idx]), ytr[idx])
            opt.zero_grad(); loss.backward(); opt.step()
            if masks:                                  # 잘린 가중치는 계속 0으로 고정
                with torch.no_grad():
                    for p, m in masks.items(): p.mul_(m)
    return model

@torch.no_grad()
def accuracy(model, X, y):
    return (model(X).argmax(1) == y).float().mean().item()

def linears(model):
    return [m for m in model if isinstance(m, nn.Linear)]

def trained_mlp():
    torch.manual_seed(0)
    Xtr, Xte, ytr, yte = get_data()
    return train(make_mlp(), Xtr, ytr), (Xtr, Xte, ytr, yte)

def global_masks(model, s):             # 모든 층을 한 줄로 세워 임계값 하나 (magnitude)
    allw = torch.cat([l.weight.detach().abs().flatten() for l in linears(model)])
    thr = allw.kthvalue(int(s * allw.numel())).values if s > 0 else -1
    return {l.weight: (l.weight.detach().abs() > thr).float() for l in linears(model)}

def apply_masks(masks):
    with torch.no_grad():
        for p, m in masks.items(): p.mul_(m)
```

`train()`의 `masks` 인자가 중요하다. optimizer step 직후 매번 mask를 곱해서 **잘린 weight가 학습 중 되살아나지 않게** 한다. 이것이 "prune 후 fine-tune"의 가장 단순한 구현이다.

### 1.3 코드로 확인 — 0 근처 weight가 얼마나 많은가

학습된 MLP에서 각 층 weight 중 "최대 절댓값의 5%/10%/20%보다 작은 것"의 비율을 센다.

```python
# 학습된 MLP의 가중치 분포: "0 근처 가중치가 얼마나 많은가"
import torch
from c4common import trained_mlp, accuracy, linears
model, (Xtr, Xte, ytr, yte) = trained_mlp()
print(f"test acc = {accuracy(model, Xte, yte):.4f}")
for i, lin in enumerate(linears(model)):
    w = lin.weight.detach().abs().flatten()
    mx = w.max().item()
    fr = [(w < t * mx).float().mean().item() for t in (0.05, 0.1, 0.2)]
    print(f"L{i} {tuple(lin.weight.shape)}  max|w|={mx:.3f}  "
          f"|w|<5%max: {fr[0]:.1%}  <10%: {fr[1]:.1%}  <20%: {fr[2]:.1%}")
total = sum(l.weight.numel() for l in linears(model))
print("weights total:", total)
```

```text
test acc = 0.9759
L0 (256, 64)  max|w|=0.447  |w|<5%max: 15.2%  <10%: 30.3%  <20%: 59.0%
L1 (256, 256)  max|w|=0.344  |w|<5%max: 20.7%  <10%: 41.0%  <20%: 74.3%
L2 (10, 256)  max|w|=0.272  |w|<5%max: 11.5%  <10%: 22.8%  <20%: 45.9%
weights total: 84480
```

출력에서 볼 것: 가장 큰 층 L1(256×256)은 weight의 **74%가 최대값의 20%보다 작다**. 층마다 분포가 다르다는 점(L2는 덜 몰려 있음)도 기억해 두자 — 2절의 global vs layer-wise 차이가 여기서 나온다.

### 1.4 lottery ticket 가설 (이름만)

Frankle & Carbin(2019)은 "큰 네트워크 안에 처음부터 잘 학습되는 작은 sub-network가 들어 있다"는 **Lottery Ticket Hypothesis**를 제안했다. 배포 기법이라기보다 "왜 pruning이 되는가"에 대한 연구 관점이다.

---

## 2. Unstructured magnitude pruning — 가장 단순한 기준

### 2.1 정의

**magnitude pruning**: 절댓값 `|w|`가 작은 weight부터 0으로 만든다. 목표 sparsity가 s라면, 절댓값을 정렬해서 하위 s 비율의 **임계값(threshold)** 을 구하고 그보다 작거나 같은 것을 지운다.

```
mask_ij = 1  if |w_ij| > θ
          0  otherwise              θ = |w| 분포의 s-분위수
w_pruned = w ⊙ mask                 (⊙ = 원소별 곱)
```

말로 하면: "작은 weight는 출력에 기여가 작을 것이다"라는 가정 하나로 자른다. 입력 크기는 고려하지 않는다(8절에서 이 약점을 고친다).

### 2.2 손으로 계산 — 2×4 weight, 50%

```
W = [ 0.8  -0.05  0.3  -0.6 ]        |w| 정렬: 0.02 0.05 0.1 0.3 │ 0.4 0.6 0.8 0.9
    [ 0.1  -0.9   0.02  0.4 ]        하위 4개(50%) 제거 → θ = 0.3 (0.3 이하 제거)

W' = [ 0.8   0     0   -0.6 ]
     [ 0    -0.9   0    0.4 ]

x = [1, 1, 1, 1] 일 때
  원본 y  = [0.8-0.05+0.3-0.6,  0.1-0.9+0.02+0.4] = [ 0.45, -0.38]
  pruned y = [0.8-0.6,           -0.9+0.4        ] = [ 0.20, -0.50]
```

말로 하면: 작은 weight만 지웠는데도 출력이 꽤 변했다(0.45 → 0.20). 작은 값도 **여러 개가 같은 방향으로 쌓이면** 무시할 수 없다. 그래서 pruning 뒤에는 거의 항상 **fine-tune(재학습)** 으로 남은 weight가 잃어버린 몫을 보상하게 한다.

### 2.3 layer-wise vs global

- **layer-wise(uniform)**: 모든 층에서 똑같이 s%를 자른다. 층마다 임계값이 다르다.
- **global**: 모든 층의 weight를 한 줄로 세워서 임계값 **하나**로 자른다. 결과적으로 weight가 많고 작은 층(보통 가운데 큰 층)이 더 많이 잘리고, 작고 민감한 층(첫 층·마지막 층)은 덜 잘린다.

```python
# one-shot magnitude pruning (fine-tune 없음): layer-wise vs global 비교
import copy, torch
from c4common import trained_mlp, accuracy, linears, global_masks, apply_masks
base, (Xtr, Xte, ytr, yte) = trained_mlp()

def layerwise_masks(model, s):          # 각 층에서 똑같이 s 비율을 자른다
    masks = {}
    for lin in linears(model):
        w = lin.weight.detach().abs()
        k = int(s * w.numel())
        thr = w.flatten().kthvalue(k).values if k > 0 else -1
        masks[lin.weight] = (w > thr).float()
    return masks

print("sparsity  layer-wise  global   global per-layer sparsity (L0/L1/L2)")
for s in (0.0, 0.5, 0.7, 0.8, 0.9, 0.95, 0.98):
    row = []
    for fn in (layerwise_masks, global_masks):
        m = copy.deepcopy(base); masks = fn(m, s); apply_masks(masks)
        row.append(accuracy(m, Xte, yte))
    per = [1 - mk.mean().item() for mk in masks.values()]
    print(f"  {s:4.0%}    {row[0]:.4f}     {row[1]:.4f}   " + "/".join(f"{p:.0%}" for p in per))
```

```text
sparsity  layer-wise  global   global per-layer sparsity (L0/L1/L2)
    0%    0.9759     0.9759   0%/0%/0%
   50%    0.9778     0.9741   32%/55%/40%
   70%    0.9333     0.9704   48%/76%/58%
   80%    0.8500     0.9278   60%/85%/68%
   90%    0.6111     0.7796   75%/94%/83%
   95%    0.3056     0.3889   85%/98%/91%
   98%    0.1019     0.1074   93%/99%/97%
```

출력에서 볼 것: fine-tune 없이도 **50%까지는 거의 공짜**다. 70~90%에서 global이 layer-wise보다 확실히 낫다(0.97 vs 0.93, 0.78 vs 0.61). 오른쪽 열을 보면 global 90%일 때 실제로는 L1을 94%, 첫 층 L0는 75%만 잘랐다 — "큰 층은 여유가 많고 작은 층은 민감하다"를 임계값 하나가 자동으로 반영한 것이다.

> 함정: global magnitude는 층마다 weight 스케일이 크게 다르면(예: BN 없는 깊은 망, 양자화 scale이 다른 층) 한 층을 통째로 날려 버릴 수 있다. 실무에서는 층별 최대 sparsity 상한을 두거나, 층별 민감도(sensitivity) 스캔을 먼저 한다.

### 2.4 `torch.nn.utils.prune` — PyTorch 내장 도구의 정체

PyTorch에는 `torch.nn.utils.prune` 모듈이 있다. 편하지만 **내부 동작을 알고 써야** 한다.

```python
# torch.nn.utils.prune: 내부가 "weight_orig × weight_mask"라는 것을 확인
import torch, torch.nn.utils.prune as prune
from c4common import trained_mlp, accuracy, linears
model, (Xtr, Xte, ytr, yte) = trained_mlp()
L0, L1, L2 = linears(model)

prune.l1_unstructured(L0, name="weight", amount=0.5)       # 한 층만: 절댓값 하위 50%
print([n for n, _ in L0.named_parameters()], [n for n, _ in L0.named_buffers()])
print("L0 sparsity:", (L0.weight == 0).float().mean().item())
print("same storage?", L0.weight.data_ptr() == L0.weight_orig.data_ptr())

prune.global_unstructured([(L1, "weight"), (L2, "weight")],  # 여러 층을 한 줄로 세워서
                          pruning_method=prune.L1Unstructured, amount=0.8)
for n, l in (("L1", L1), ("L2", L2)):
    print(n, "sparsity:", round((l.weight == 0).float().mean().item(), 4))
print("acc (masked):", round(accuracy(model, Xte, yte), 4))

for l in (L0, L1, L2):
    prune.remove(l, "weight")                              # mask를 굳혀 평범한 weight로
print([n for n, _ in L0.named_parameters()], "buffers:", list(L0.named_buffers()))
sd = model.state_dict()
print("state_dict keys:", list(sd)[:2], " dense bytes:",
      sum(v.numel() * v.element_size() for v in sd.values()))
```

```text
['bias', 'weight_orig'] ['weight_mask']
L0 sparsity: 0.5
same storage? False
L1 sparsity: 0.8067
L2 sparsity: 0.6289
acc (masked): 0.9704
['bias', 'weight'] buffers: []
state_dict keys: ['0.bias', '0.weight']  dense bytes: 340008
```

출력에서 볼 것은 세 가지다.

1. prune을 걸면 원래 `weight` parameter가 `weight_orig`(학습되는 원본)와 `weight_mask`(buffer)로 바뀌고, `weight`는 forward 직전에 hook이 `weight_orig × weight_mask`로 **매번 새로 계산**하는 텐서가 된다. 그래서 저장 공간이 다르다(`same storage? False`) — 메모리는 오히려 **더** 쓴다 (원본 + 같은 크기의 float mask + 계산된 weight).
2. `prune.remove`는 mask를 곱한 결과를 평범한 `weight`로 굳힌다. 이때 0은 여전히 **dense 텐서 안의 0**이다.
3. 그래서 `state_dict` 크기는 340,008 B = (84,480 weight + 522 bias) × 4 B로 **pruning 전과 한 바이트도 다르지 않다**. PyTorch의 prune은 "0을 만드는 도구"이지 "모델을 작게 만드는 도구"가 아니다. 크기·속도 이득은 3~6절처럼 **포맷과 커널**이 따로 만들어 줘야 한다.

직접 mask를 관리하는 방식(`c4common.global_masks` + `train(masks=...)`)과 결과는 같다. 이 노트는 동작이 투명한 수동 mask 방식을 주로 쓴다.

### 2.5 iterative prune + fine-tune — 한 번에 자르지 말고 조금씩

one-shot으로 90%를 한 번에 자르면 네트워크가 받는 충격이 크다. 대신 50% → 70% → 80% → 90% … 처럼 **조금씩 올리면서 매번 fine-tune** 하면 남은 weight가 단계마다 적응할 시간을 얻는다. 펌웨어 비유로는 전압 margin을 한 번에 깎지 않고 step마다 스트레스 테스트를 통과시키며 깎는 것과 같다.

```python
# prune → fine-tune 반복: one-shot(+FT) vs iterative(점진적으로 올리며 매번 FT)
import copy
from c4common import trained_mlp, accuracy, linears, train, global_masks, apply_masks
base, (Xtr, Xte, ytr, yte) = trained_mlp()

print("target  one-shot+FT  iterative+FT  nonzero weights")
it = copy.deepcopy(base)                  # iterative: 이전 단계 모델을 이어서 더 자른다
for s in (0.5, 0.7, 0.8, 0.9, 0.95, 0.98):
    one = copy.deepcopy(base); mk = global_masks(one, s); apply_masks(mk)
    train(one, Xtr, ytr, epochs=10, masks=mk, seed=1)       # 한 번에 s까지 자르고 FT
    mk2 = global_masks(it, s); apply_masks(mk2)
    train(it, Xtr, ytr, epochs=10, masks=mk2, seed=1)       # 50→70→…→s 단계마다 FT
    nz = sum(int(l.weight.count_nonzero()) for l in linears(it))
    print(f" {s:4.0%}     {accuracy(one, Xte, yte):.4f}       {accuracy(it, Xte, yte):.4f}       {nz}")
```

```text
target  one-shot+FT  iterative+FT  nonzero weights
  50%     0.9704       0.9704       42240
  70%     0.9741       0.9722       25345
  80%     0.9778       0.9759       16896
  90%     0.9759       0.9833       8448
  95%     0.9463       0.9667       4224
  98%     0.4019       0.9222       1690
```

출력에서 볼 것: fine-tune만 붙여도 90%까지 원래 정확도(0.976)를 유지한다(2.3절 FT 없는 global 90%는 0.78이었다). 차이는 극단에서 난다 — 98%(남은 weight 1,690개)에서 one-shot+FT는 0.40으로 무너지지만 iterative는 **0.92**를 지킨다. 50~90% 구간의 0.97~0.98 사이 차이는 test 540장 중 몇 장 수준이라 잡음이다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250" x2="640" y2="250" stroke="currentColor"/> <line x1="70" y1="30" x2="70" y2="250" stroke="currentColor"/> <line x1="66" y1="250.0" x2="640" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="254.0" font-size="12" text-anchor="end">0.0</text> <line x1="66" y1="206.0" x2="640" y2="206.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="210.0" font-size="12" text-anchor="end">0.2</text> <line x1="66" y1="162.0" x2="640" y2="162.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="166.0" font-size="12" text-anchor="end">0.4</text> <line x1="66" y1="118.0" x2="640" y2="118.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="122.0" font-size="12" text-anchor="end">0.6</text> <line x1="66" y1="74.0" x2="640" y2="74.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="78.0" font-size="12" text-anchor="end">0.8</text> <line x1="66" y1="30.0" x2="640" y2="30.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="34.0" font-size="12" text-anchor="end">1.0</text> <text x="70.0" y="268" font-size="12" text-anchor="middle">0%</text> <text x="165.0" y="268" font-size="12" text-anchor="middle">50%</text> <text x="260.0" y="268" font-size="12" text-anchor="middle">70%</text> <text x="355.0" y="268" font-size="12" text-anchor="middle">80%</text> <text x="450.0" y="268" font-size="12" text-anchor="middle">90%</text> <text x="545.0" y="268" font-size="12" text-anchor="middle">95%</text> <text x="640.0" y="268" font-size="12" text-anchor="middle">98%</text> <polyline points="70.0,35.3 165.0,34.9 260.0,44.7 355.0,63.0 450.0,115.6 545.0,182.8 640.0,227.6" fill="none" stroke="#d0564a" stroke-width="2"/> <circle cx="70.0" cy="35.3" r="3" fill="#d0564a"/> <circle cx="165.0" cy="34.9" r="3" fill="#d0564a"/> <circle cx="260.0" cy="44.7" r="3" fill="#d0564a"/> <circle cx="355.0" cy="63.0" r="3" fill="#d0564a"/> <circle cx="450.0" cy="115.6" r="3" fill="#d0564a"/>
<circle cx="545.0" cy="182.8" r="3" fill="#d0564a"/> <circle cx="640.0" cy="227.6" r="3" fill="#d0564a"/> <polyline points="70.0,35.3 165.0,35.7 260.0,36.5 355.0,45.9 450.0,78.5 545.0,164.4 640.0,226.4" fill="none" stroke="#e08a3c" stroke-width="2"/> <circle cx="70.0" cy="35.3" r="3" fill="#e08a3c"/> <circle cx="165.0" cy="35.7" r="3" fill="#e08a3c"/> <circle cx="260.0" cy="36.5" r="3" fill="#e08a3c"/> <circle cx="355.0" cy="45.9" r="3" fill="#e08a3c"/> <circle cx="450.0" cy="78.5" r="3" fill="#e08a3c"/> <circle cx="545.0" cy="164.4" r="3" fill="#e08a3c"/> <circle cx="640.0" cy="226.4" r="3" fill="#e08a3c"/> <polyline points="70.0,35.3 165.0,36.5 260.0,35.7 355.0,34.9 450.0,35.3 545.0,41.8 640.0,161.6" fill="none" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="5,3"/> <circle cx="70.0" cy="35.3" r="3" fill="#4a7bd0"/> <circle cx="165.0" cy="36.5" r="3" fill="#4a7bd0"/> <circle cx="260.0" cy="35.7" r="3" fill="#4a7bd0"/> <circle cx="355.0" cy="34.9" r="3" fill="#4a7bd0"/> <circle cx="450.0" cy="35.3" r="3" fill="#4a7bd0"/> <circle cx="545.0" cy="41.8" r="3" fill="#4a7bd0"/> <circle cx="640.0" cy="161.6" r="3" fill="#4a7bd0"/> <polyline points="70.0,35.3 165.0,36.5 260.0,36.1 355.0,35.3 450.0,33.7 545.0,37.3 640.0,47.1" fill="none" stroke="#3f9a6b" stroke-width="2"/> <circle cx="70.0" cy="35.3" r="3" fill="#3f9a6b"/> <circle cx="165.0" cy="36.5" r="3" fill="#3f9a6b"/> <circle cx="260.0" cy="36.1" r="3" fill="#3f9a6b"/> <circle cx="355.0" cy="35.3" r="3" fill="#3f9a6b"/> <circle cx="450.0" cy="33.7" r="3" fill="#3f9a6b"/> <circle cx="545.0" cy="37.3" r="3" fill="#3f9a6b"/> <circle cx="640.0" cy="47.1" r="3" fill="#3f9a6b"/> <text x="355" y="290" font-size="13" text-anchor="middle">sparsity (0인 weight 비율, 눈금은 등간격 아님)</text> <text x="20" y="140" font-size="13" transform="rotate(-90 20 140)" text-anchor="middle">test accuracy</text> <line x1="90" y1="160" x2="115" y2="160" stroke="#d0564a" stroke-width="2"/> <text x="121" y="164" font-size="12">one-shot layer-wise (FT 없음)</text>
<line x1="90" y1="178" x2="115" y2="178" stroke="#e08a3c" stroke-width="2"/> <text x="121" y="182" font-size="12">one-shot global (FT 없음)</text> <line x1="90" y1="196" x2="115" y2="196" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="5,3"/> <text x="121" y="200" font-size="12">one-shot global + FT 10ep</text> <line x1="90" y1="214" x2="115" y2="214" stroke="#3f9a6b" stroke-width="2"/> <text x="121" y="218" font-size="12">iterative global + FT</text>
</svg>
```

그림 2 — 위 두 실험의 accuracy vs sparsity (실측값). 빨강·주황(FT 없음)은 70~80%부터 떨어지고, 파랑 점선(one-shot + FT)은 95%까지, 초록(iterative + FT)은 98%까지 버틴다. 이 "무릎(knee)"의 위치가 모델·데이터마다 다르므로 항상 곡선을 직접 그려서 정한다.

### 2.6 schedule — gradual pruning 공식

Zhu & Gupta(2017, "To prune, or not to prune")의 **gradual pruning**은 sparsity를 학습 step t에 따라 다음처럼 올린다.

```
s_t = s_f + (s_i − s_f) · (1 − (t − t₀) / (n·Δt))³      t ∈ {t₀, t₀+Δt, …, t₀+n·Δt}

s_i: 시작 sparsity (보통 0)   s_f: 목표 sparsity   Δt: 몇 step마다 mask를 갱신할지   n: 갱신 횟수
```

말로 하면: 처음엔 빠르게 자르고(아직 잘라도 되는 weight가 많을 때), 목표에 가까워질수록 천천히 자른다. 세제곱 곡선이라 초반 기울기가 크고 끝에서 평평해진다. 예: s_i = 0, s_f = 0.9, n = 4이면 단계별 sparsity는 1 − (1 − k/4)³을 0.9배 한 값 = 0, 0.52, 0.79, 0.89, 0.90이다. 위 예제의 50→70→80→90 스케줄이 대략 이 모양을 손으로 흉내 낸 것이다.

### 2.7 함정

- **FT 없이 결론 내리기**: one-shot 곡선만 보고 "70%까지밖에 안 된다"고 판단하면 틀린다.
- **FT 중 mask 누락**: Adam의 momentum이 0이던 weight를 되살린다 — 매 step 뒤 mask를 다시 곱한다(`c4common.train`).
- **bias·첫/마지막 층까지 똑같이 자르기**: 파라미터는 적은데 민감하다. 보통 제외한다.
- **accuracy만 보기**: 클래스별 recall(예: wake word false reject)이 먼저 무너질 수 있다 — C8의 회귀 테스트를 적용한다.

---

## 3. Sparse weight를 어떻게 저장하나 — 포맷과 index 오버헤드

0을 만들었으면 0을 **저장하지 않아야** 메모리가 준다. 그런데 0을 빼면 "남은 값이 원래 어디 있었는지"를 따로 적어야 한다. 이 **index 오버헤드**가 모든 sparse 포맷의 손익을 결정한다. Don에게 익숙한 말로 하면 SSD FTL의 매핑 테이블이다 — 유효 데이터만 모아 쓰면 공간은 줄지만 매핑 정보가 붙는다.

### 3.1 dense + bitmask

원소마다 1 bit로 "0인가 아닌가"를 적고, 0이 아닌 값만 순서대로 모은다.

```
bytes = (R·C)/8  +  nnz · b_v            nnz = 0이 아닌 원소 수, b_v = 값 1개 바이트 수
```

index 비용이 원소당 1 bit로 **sparsity와 무관하게 고정**이다. int8 weight(8 bit)라면 비트맵은 값 크기의 1/8이므로, density d(= 1 − sparsity)에 대해 `1/8 + d < 1` 즉 **sparsity > 12.5%면 dense보다 작다**. 구현이 단순하고(비트 스캔) 행 단위 복원이 쉬워서 NPU·가속기의 weight 압축에도 흔히 쓰이는 방식이다.

### 3.2 CSR (Compressed Sparse Row) — 손으로 만들기

CSR은 세 배열로 행렬을 표현한다.

- `values`: 0이 아닌 값을 행 순서대로 (길이 nnz)
- `col_idx`: 각 값의 열 번호 (길이 nnz)
- `row_ptr`: 행 r의 값이 `values[row_ptr[r]]`부터 `values[row_ptr[r+1]−1]`까지 있다는 시작 위치 (길이 R+1)

4×6 int8 행렬로 손으로 만들어 보자.

```
      c0 c1 c2 c3 c4 c5
r0 [   5  0  0  0  0  2 ]     r0: (0,5) (5,2)       → values 5,2   col 0,5
r1 [   0  0  0  0  0  0 ]     r1: 없음
r2 [   0  3  0  0  7  0 ]     r2: (1,3) (4,7)       → values 3,7   col 1,4
r3 [   0  0  0  0  0  4 ]     r3: (5,4)             → values 4     col 5

values  = [5, 2, 3, 7, 4]
col_idx = [0, 5, 1, 4, 5]
row_ptr = [0, 2, 2, 4, 5]     ← 누적 개수: 0, +2, +0, +2, +1
```

```svg
<svg viewBox="0 0 640 260" xmlns="http://www.w3.org/2000/svg">
<text x="142" y="30" font-size="13" text-anchor="middle">W (4×6, int8)</text> <text x="57.0" y="44" font-size="12" text-anchor="middle">c0</text> <text x="91.0" y="44" font-size="12" text-anchor="middle">c1</text> <text x="125.0" y="44" font-size="12" text-anchor="middle">c2</text> <text x="159.0" y="44" font-size="12" text-anchor="middle">c3</text> <text x="193.0" y="44" font-size="12" text-anchor="middle">c4</text> <text x="227.0" y="44" font-size="12" text-anchor="middle">c5</text> <text x="32" y="71.0" font-size="12" text-anchor="end">r0</text> <rect x="40" y="50" width="32" height="32" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0" stroke-width="1"/> <text x="56.0" y="71.0" font-size="13" text-anchor="middle">5</text> <rect x="74" y="50" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="90.0" y="71.0" font-size="13" text-anchor="middle">0</text> <rect x="108" y="50" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="124.0" y="71.0" font-size="13" text-anchor="middle">0</text> <rect x="142" y="50" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="158.0" y="71.0" font-size="13" text-anchor="middle">0</text> <rect x="176" y="50" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="192.0" y="71.0" font-size="13" text-anchor="middle">0</text> <rect x="210" y="50" width="32" height="32" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0" stroke-width="1"/> <text x="226.0" y="71.0" font-size="13" text-anchor="middle">2</text> <text x="32" y="105.0" font-size="12" text-anchor="end">r1</text> <rect x="40" y="84" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="56.0" y="105.0" font-size="13" text-anchor="middle">0</text> <rect x="74" y="84" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="90.0" y="105.0" font-size="13" text-anchor="middle">0</text>
<rect x="108" y="84" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="124.0" y="105.0" font-size="13" text-anchor="middle">0</text> <rect x="142" y="84" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="158.0" y="105.0" font-size="13" text-anchor="middle">0</text> <rect x="176" y="84" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="192.0" y="105.0" font-size="13" text-anchor="middle">0</text> <rect x="210" y="84" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="226.0" y="105.0" font-size="13" text-anchor="middle">0</text> <text x="32" y="139.0" font-size="12" text-anchor="end">r2</text> <rect x="40" y="118" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="56.0" y="139.0" font-size="13" text-anchor="middle">0</text> <rect x="74" y="118" width="32" height="32" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c" stroke-width="1"/> <text x="90.0" y="139.0" font-size="13" text-anchor="middle">3</text> <rect x="108" y="118" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="124.0" y="139.0" font-size="13" text-anchor="middle">0</text> <rect x="142" y="118" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="158.0" y="139.0" font-size="13" text-anchor="middle">0</text> <rect x="176" y="118" width="32" height="32" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c" stroke-width="1"/> <text x="192.0" y="139.0" font-size="13" text-anchor="middle">7</text> <rect x="210" y="118" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="226.0" y="139.0" font-size="13" text-anchor="middle">0</text> <text x="32" y="173.0" font-size="12" text-anchor="end">r3</text> <rect x="40" y="152" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/>
<text x="56.0" y="173.0" font-size="13" text-anchor="middle">0</text> <rect x="74" y="152" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="90.0" y="173.0" font-size="13" text-anchor="middle">0</text> <rect x="108" y="152" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="124.0" y="173.0" font-size="13" text-anchor="middle">0</text> <rect x="142" y="152" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="158.0" y="173.0" font-size="13" text-anchor="middle">0</text> <rect x="176" y="152" width="32" height="32" fill="none" fill-opacity="0.35" stroke="#888" stroke-width="1"/> <text x="192.0" y="173.0" font-size="13" text-anchor="middle">0</text> <rect x="210" y="152" width="32" height="32" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1"/> <text x="226.0" y="173.0" font-size="13" text-anchor="middle">4</text> <text x="322" y="80" font-size="13" text-anchor="end">values</text> <rect x="330" y="60" width="32" height="32" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="346.0" y="81" font-size="13" text-anchor="middle">5</text> <rect x="364" y="60" width="32" height="32" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="380.0" y="81" font-size="13" text-anchor="middle">2</text> <rect x="398" y="60" width="32" height="32" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="414.0" y="81" font-size="13" text-anchor="middle">3</text> <rect x="432" y="60" width="32" height="32" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="448.0" y="81" font-size="13" text-anchor="middle">7</text> <rect x="466" y="60" width="32" height="32" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <text x="482.0" y="81" font-size="13" text-anchor="middle">4</text> <text x="322" y="130" font-size="13" text-anchor="end">col_idx</text> <rect x="330" y="110" width="32" height="32" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="346.0" y="131" font-size="13" text-anchor="middle">0</text> <rect x="364" y="110" width="32" height="32" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="380.0" y="131" font-size="13" text-anchor="middle">5</text> <rect x="398" y="110" width="32" height="32" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="414.0" y="131" font-size="13" text-anchor="middle">1</text> <rect x="432" y="110" width="32" height="32" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="448.0" y="131" font-size="13" text-anchor="middle">4</text> <rect x="466" y="110" width="32" height="32" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <text x="482.0" y="131" font-size="13" text-anchor="middle">5</text> <text x="322" y="190" font-size="13" text-anchor="end">row_ptr</text> <rect x="330" y="170" width="32" height="32" fill="none" stroke="currentColor"/> <text x="346.0" y="191" font-size="13" text-anchor="middle">0</text> <rect x="364" y="170" width="32" height="32" fill="none" stroke="currentColor"/> <text x="380.0" y="191" font-size="13" text-anchor="middle">2</text> <rect x="398" y="170" width="32" height="32" fill="none" stroke="currentColor"/> <text x="414.0" y="191" font-size="13" text-anchor="middle">2</text> <rect x="432" y="170" width="32" height="32" fill="none" stroke="currentColor"/> <text x="448.0" y="191" font-size="13" text-anchor="middle">4</text> <rect x="466" y="170" width="32" height="32" fill="none" stroke="currentColor"/> <text x="482.0" y="191" font-size="13" text-anchor="middle">5</text> <text x="330" y="222" font-size="12">행 r의 nonzero = values[row_ptr[r] … row_ptr[r+1]−1]</text> <text x="330" y="240" font-size="12">r1: row_ptr[1]=2, row_ptr[2]=2 → 빈 행 (길이 0)</text> <text x="40" y="215" font-size="12">색 = 같은 행에서 온 원소</text>
</svg>
```

그림 3 — CSR의 세 배열. `row_ptr`에서 이웃한 두 값의 차가 그 행의 nonzero 개수다. r1처럼 빈 행은 `row_ptr[1] = row_ptr[2] = 2`로 길이 0이 된다.

곱셈 y = W·x는 "행마다 자기 구간의 값만 돌면서 `x[col_idx[k]]`를 **가져와(gather)** 곱한다". x = [1,2,3,4,5,6]이면 r0 = 5·1 + 2·6 = 17, r2 = 3·2 + 7·5 = 41, r3 = 4·6 = 24.

scipy로 확인하고, 512×512 int8 행렬에서 세 포맷의 크기를 sparsity별로 계산한다.

```python
# 손으로 만든 CSR과 scipy CSR 비교 + int8 가중치의 저장 크기 손익분기
import numpy as np, scipy.sparse as sp
W = np.array([[5, 0, 0, 0, 0, 2],
              [0, 0, 0, 0, 0, 0],
              [0, 3, 0, 0, 7, 0],
              [0, 0, 0, 0, 0, 4]], dtype=np.int8)
A = sp.csr_matrix(W)
print("values :", A.data.tolist())
print("col_idx:", A.indices.tolist())
print("row_ptr:", A.indptr.tolist())
x = np.arange(1, 7, dtype=np.int32)
print("W@x    :", (W.astype(np.int32) @ x).tolist())

R = C = 512
def sizes(s):                              # int8 값, col=uint16, row_ptr=uint32
    nnz = round(R * C * (1 - s))
    dense  = R * C
    bitmap = R * C // 8 + nnz              # 1 bit/원소 + nonzero 값
    csr    = nnz * (1 + 2) + (R + 1) * 4
    return dense, bitmap, csr
print("sparsity   dense   bitmask     CSR  (bytes, 512x512 int8)")
for s in (0.0, 0.5, 0.6667, 0.8, 0.875, 0.9, 0.95):
    print(f"  {s:6.2%}  {sizes(s)[0]:6d}  {sizes(s)[1]:8d}  {sizes(s)[2]:7d}")
```

```text
values : [5, 2, 3, 7, 4]
col_idx: [0, 5, 1, 4, 5]
row_ptr: [0, 2, 2, 4, 5]
W@x    : [17, 0, 41, 24]
sparsity   dense   bitmask     CSR  (bytes, 512x512 int8)
   0.00%  262144    294912   788484
  50.00%  262144    163840   395268
  66.67%  262144    120141   264171
  80.00%  262144     85197   159339
  87.50%  262144     65536   100356
  90.00%  262144     58982    80694
  95.00%  262144     45875    41373
```

출력에서 볼 것: scipy의 `data`/`indices`/`indptr`가 손으로 만든 배열과 똑같다. 그리고 int8에서 **CSR은 50% sparsity에서도 dense보다 1.5배 크다**. 66.67% 근처에서야 비슷해지고(264,171 vs 262,144 — 아직 약간 크다), 95%에서야 bitmask를 이긴다.

### 3.3 손익분기 유도 — "int8 weight에서 CSR은 몇 %부터 이득인가"

```
dense  = R·C·b_v
CSR    = nnz·(b_v + b_i) + (R+1)·b_p          b_i = 열 index 바이트, b_p = row_ptr 바이트
       ≈ R·C·d·(b_v + b_i)                    (row_ptr 항은 작아서 무시)

CSR < dense  ⇔  d < b_v / (b_v + b_i)

int8 값 + uint16 index : d < 1/3   → sparsity > 66.7%
fp32 값 + int32 index  : d < 1/2   → sparsity > 50%
int8 값 + uint8 index  : d < 1/2   → sparsity > 50%  (열이 256개 이하이거나 블록 내 상대 index일 때)
bitmask (int8)         : 1/8 + d < 1 → sparsity > 12.5%
```

말로 하면: **값이 작을수록(양자화할수록) index가 상대적으로 비싸진다**. fp32 시절 논문에서 "50%만 넘으면 CSR 이득"이던 계산이 int8에서는 67%, int4에서는 더 높아진다(int4 + uint16 index면 d < 0.5/2.5 = 1/5, 즉 80%). 양자화(C1)와 pruning을 같이 할 때 이 점을 놓치기 쉽다.

### 3.4 다른 포맷들 (이름과 한 줄 특징)

| 포맷 | 저장하는 것 | 특징 |
|---|---|---|
| COO | (row, col, value) 세 쌍 | 만들기 쉬움, index가 가장 큼. 변환용 |
| CSR / CSC | row_ptr + col_idx + values (CSC는 열 기준) | SpMV 표준. 행 길이가 들쭉날쭉 |
| bitmask | 원소당 1 bit + nonzero 값 | 오버헤드 고정, HW 복원 쉬움 |
| BSR (block CSR) | 0이 아닌 **블록**(예: 4×4)만 CSR로 | 블록 안은 dense라 SIMD 가능. block sparsity와 짝 |
| N:M (2:4) | 그룹당 N개 값 + 위치 index | 크기 고정·규칙적. 6절 |
| RLE / 엔트로피 코딩 | 0의 run 길이, Huffman 등 | 저장 전용. 실행 전 복원 필요 |

---

## 4. C로 측정 — 90% sparse면 10배 빠른가?

이제 핵심 질문을 실측한다. 512×512 int8 행렬 × int8 벡터(int32 누적) GEMV를 세 방식으로 돌린다.

- `dense`: 0이 있어도 전부 곱하는 이중 루프 (컴파일러가 NEON으로 자동 벡터화)
- `csr`: CSR SpMV — 0이 아닌 것만 곱하지만 `x[col[k]]` gather가 필요
- `sp24`: 2:4 압축 포맷(값 2개 + 2-bit index 2개/그룹) 소프트웨어 구현 — 6절에서 다시 본다

0의 위치는 무작위(= unstructured)이고, 각 커널을 200번 돌린 평균을 7번 재서 최소값을 쓴다. checksum으로 세 결과가 같음을 확인한다.

```c
/* spmv.c — int8 dense GEMV vs CSR SpMV vs 2:4 compressed GEMV (512x512) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define R 512
#define C 512
static int8_t W[R][C], x[C];
static int8_t val[R * C]; static uint16_t col[R * C]; static uint32_t rp[R + 1];
static int8_t v24[R][C / 2]; static uint8_t i24[R][C / 4]; /* 2 x 2-bit idx per group */
static int32_t y[R];
static uint32_t seed = 12345;
static uint32_t rnd(void) { seed = seed * 1664525u + 1013904223u; return seed >> 8; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static void dense(void) {
    for (int r = 0; r < R; r++) { int32_t acc = 0;
        for (int c = 0; c < C; c++) acc += W[r][c] * x[c];
        y[r] = acc; }
}
static void csr(void) {
    for (int r = 0; r < R; r++) { int32_t acc = 0;
        for (uint32_t k = rp[r]; k < rp[r + 1]; k++) acc += val[k] * x[col[k]]; /* gather */
        y[r] = acc; }
}
static void sp24(void) {
    for (int r = 0; r < R; r++) { int32_t acc = 0;
        for (int g = 0; g < C / 4; g++) { const int8_t *xg = &x[4 * g]; uint8_t m = i24[r][g];
            acc += v24[r][2 * g] * xg[m & 3] + v24[r][2 * g + 1] * xg[(m >> 2) & 3]; }
        y[r] = acc; }
}
static void build_csr(void) {
    uint32_t n = 0; rp[0] = 0;
    for (int r = 0; r < R; r++) { for (int c = 0; c < C; c++)
        if (W[r][c]) { val[n] = W[r][c]; col[n] = (uint16_t)c; n++; }
        rp[r + 1] = n; }
}
static double bench(void (*f)(void), int64_t *chk) {
    double best = 1e9;
    for (int t = 0; t < 7; t++) { double t0 = now();
        for (int i = 0; i < 200; i++) f();
        double dt = (now() - t0) / 200; if (dt < best) best = dt; }
    int64_t s = 0; for (int r = 0; r < R; r++) s += (int64_t)y[r] * (r + 1); *chk = s;
    return best * 1e6; /* us */
}
int main(void) {
    const double sps[] = {0.0, 0.5, 0.7, 0.8, 0.9, 0.95, 0.98, 0.99};
    for (int c = 0; c < C; c++) x[c] = (int8_t)(rnd() % 255 - 127);
    printf("sparsity   dense_us   csr_us   csr/dense  (checksum ok)\n");
    for (unsigned i = 0; i < sizeof sps / sizeof sps[0]; i++) {
        for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) {
            int8_t w = (int8_t)(rnd() % 255 - 127); if (w == 0) w = 1;
            W[r][c] = ((rnd() % 10000) < sps[i] * 10000) ? 0 : w; }
        build_csr(); int64_t a, b;
        double td = bench(dense, &a), tc = bench(csr, &b);
        printf("  %4.0f%%    %7.2f  %7.2f    %5.2fx     %s\n", sps[i] * 100, td, tc, tc / td, a == b ? "yes" : "NO");
    }
    /* 2:4: 각 4개 묶음에서 2개 위치를 무작위로 골라 남긴다 */
    for (int r = 0; r < R; r++) for (int g = 0; g < C / 4; g++) {
        int p = rnd() % 4, q = (p + 1 + rnd() % 3) % 4; if (p > q) { int t = p; p = q; q = t; }
        for (int k = 0; k < 4; k++) W[r][4 * g + k] = 0;
        int8_t a = (int8_t)(rnd() % 127 + 1), b = (int8_t)-(int8_t)(rnd() % 127 + 1);
        W[r][4 * g + p] = a; W[r][4 * g + q] = b;
        v24[r][2 * g] = a; v24[r][2 * g + 1] = b; i24[r][g] = (uint8_t)(p | (q << 2)); }
    build_csr(); int64_t a, b, c2;
    double td = bench(dense, &a), tc = bench(csr, &b), t24 = bench(sp24, &c2);
    printf("2:4 (50%%)  dense %.2f us  csr %.2f us  2:4-packed %.2f us  (checksum ok: %s)\n",
           td, tc, t24, (a == b && a == c2) ? "yes" : "NO");
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 spmv.c -o spmv -lm && ./spmv
```

```text
sparsity   dense_us   csr_us   csr/dense  (checksum ok)
     0%       5.37    97.23    18.12x     yes
    50%       5.63    47.87     8.51x     yes
    70%       5.69    30.48     5.36x     yes
    80%       5.51    21.57     3.91x     yes
    90%       5.49    11.28     2.05x     yes
    95%       5.48     5.10     0.93x     yes
    98%       5.50     1.99     0.36x     yes
    99%       5.45     1.23     0.23x     yes
2:4 (50%)  dense 5.37 us  csr 48.06 us  2:4-packed 49.15 us  (checksum ok: yes)
```

(시간의 절대값은 실행할 때마다 시스템 부하에 따라 달라진다. 부하가 있을 때 다시 돌렸더니 dense 12.6 µs, CSR 236 µs처럼 전부 느려졌지만 비율은 18.8×, 2.0×(90%), 0.90×(95%)로 거의 같았다. 결론은 비율로 읽는다.)

출력에서 볼 것: **90% sparse CSR이 dense보다 2배 느리다.** CSR이 dense를 이기는 건 95% 근처부터다. "90% 0이면 10배 빠르다"는 직관은 이 CPU에서 정반대로 틀렸다.

같은 코드를 **자동 벡터화를 끄고**(`-fno-vectorize -fno-slp-vectorize`) 컴파일하면 그림이 뒤집힌다.

```sh
cc -std=c11 -Wall -Wextra -O2 -fno-vectorize -fno-slp-vectorize spmv.c -o spmv_novec -lm && ./spmv_novec
```

```text
sparsity   dense_us   csr_us   csr/dense  (checksum ok)
     0%     103.13   105.21     1.02x     yes
    50%     104.87    50.83     0.48x     yes
    70%     103.44    33.06     0.32x     yes
    80%     106.44    24.08     0.23x     yes
    90%     104.72    11.61     0.11x     yes
    95%     104.36     6.55     0.06x     yes
    98%     104.87     2.49     0.02x     yes
    99%     104.41     1.33     0.01x     yes
2:4 (50%)  dense 103.29 us  csr 52.66 us  2:4-packed 52.68 us  (checksum ok: yes)
```

출력에서 볼 것: 스칼라 dense는 약 104 µs로 SIMD dense(5.5 µs)보다 **19배 느리다**. 스칼라끼리 비교하면 CSR은 nnz에 거의 정확히 비례해서 빨라진다(90% → 0.11배). 즉 **"sparse가 느리다"가 아니라 "dense가 SIMD 덕에 엄청 빠르다"** 가 정확한 설명이다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250" x2="640" y2="250" stroke="currentColor"/> <line x1="70" y1="30" x2="70" y2="250" stroke="currentColor"/> <line x1="66" y1="250.0" x2="640" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="254.0" font-size="12" text-anchor="end">1</text> <line x1="66" y1="221.2" x2="640" y2="221.2" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="225.2" font-size="12" text-anchor="end">2</text> <line x1="66" y1="183.2" x2="640" y2="183.2" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="187.2" font-size="12" text-anchor="end">5</text> <line x1="66" y1="154.4" x2="640" y2="154.4" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="158.4" font-size="12" text-anchor="end">10</text> <line x1="66" y1="125.6" x2="640" y2="125.6" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="129.6" font-size="12" text-anchor="end">20</text> <line x1="66" y1="87.6" x2="640" y2="87.6" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="91.6" font-size="12" text-anchor="end">50</text> <line x1="66" y1="58.8" x2="640" y2="58.8" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="62.8" font-size="12" text-anchor="end">100</text> <line x1="66" y1="30.0" x2="640" y2="30.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2,3"/> <text x="62" y="34.0" font-size="12" text-anchor="end">200</text> <text x="70.0" y="268" font-size="12" text-anchor="middle">0%</text> <text x="184.0" y="268" font-size="12" text-anchor="middle">20%</text> <text x="298.0" y="268" font-size="12" text-anchor="middle">40%</text> <text x="412.0" y="268" font-size="12" text-anchor="middle">60%</text> <text x="526.0" y="268" font-size="12" text-anchor="middle">80%</text> <text x="640.0" y="268" font-size="12" text-anchor="middle">100%</text> <polyline points="70.0,57.5 355.0,56.8 469.0,57.4 526.0,56.2 583.0,56.9 611.5,57.0 628.6,56.8 634.3,57.0" fill="none" stroke="#888" stroke-width="2" stroke-dasharray="5,3"/>
<circle cx="70.0" cy="57.5" r="3" fill="#888"/> <circle cx="355.0" cy="56.8" r="3" fill="#888"/> <circle cx="469.0" cy="57.4" r="3" fill="#888"/> <circle cx="526.0" cy="56.2" r="3" fill="#888"/> <circle cx="583.0" cy="56.9" r="3" fill="#888"/> <circle cx="611.5" cy="57.0" r="3" fill="#888"/> <circle cx="628.6" cy="56.8" r="3" fill="#888"/> <circle cx="634.3" cy="57.0" r="3" fill="#888"/> <polyline points="70.0,180.2 355.0,178.2 469.0,177.8 526.0,179.1 583.0,179.3 611.5,179.4 628.6,179.2 634.3,179.6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <circle cx="70.0" cy="180.2" r="3" fill="#4a7bd0"/> <circle cx="355.0" cy="178.2" r="3" fill="#4a7bd0"/> <circle cx="469.0" cy="177.8" r="3" fill="#4a7bd0"/> <circle cx="526.0" cy="179.1" r="3" fill="#4a7bd0"/> <circle cx="583.0" cy="179.3" r="3" fill="#4a7bd0"/> <circle cx="611.5" cy="179.4" r="3" fill="#4a7bd0"/> <circle cx="628.6" cy="179.2" r="3" fill="#4a7bd0"/> <circle cx="634.3" cy="179.6" r="3" fill="#4a7bd0"/> <polyline points="70.0,59.9 355.0,89.4 469.0,108.1 526.0,122.5 583.0,149.4 611.5,182.3 628.6,221.4 634.3,241.4" fill="none" stroke="#e08a3c" stroke-width="2"/> <circle cx="70.0" cy="59.9" r="3" fill="#e08a3c"/> <circle cx="355.0" cy="89.4" r="3" fill="#e08a3c"/> <circle cx="469.0" cy="108.1" r="3" fill="#e08a3c"/> <circle cx="526.0" cy="122.5" r="3" fill="#e08a3c"/> <circle cx="583.0" cy="149.4" r="3" fill="#e08a3c"/> <circle cx="611.5" cy="182.3" r="3" fill="#e08a3c"/> <circle cx="628.6" cy="221.4" r="3" fill="#e08a3c"/> <circle cx="634.3" cy="241.4" r="3" fill="#e08a3c"/> <line x1="608.6" y1="210.3" x2="608.6" y2="180.0" stroke="#d0564a" stroke-width="1.5"/> <text x="600.6" y="214.3" font-size="12" text-anchor="end">교차점 ≈ 94~95%</text> <text x="80" y="49.2" font-size="12">dense 스칼라 (-fno-vectorize) ≈ 104 µs</text> <text x="80" y="197.2" font-size="12">dense NEON (-O2 자동 벡터화) ≈ 5.5 µs</text> <text x="298.0" y="114.7" font-size="12">CSR SpMV (nnz에 비례)</text> <text x="355" y="290" font-size="13" text-anchor="middle">unstructured sparsity (무작위 위치)</text>
<text x="20" y="140" font-size="13" transform="rotate(-90 20 140)" text-anchor="middle">µs / GEMV (log)</text>
</svg>
```

그림 4 — 512×512 int8 GEMV 실측 (Apple M2, 1 core, log 스케일). CSR(주황)은 nnz에 비례해 내려오지만, NEON dense(파랑)는 sparsity와 무관하게 5.5 µs로 평평하다. 두 선은 94~95%에서 만난다. 회색 점선(스칼라 dense)과 비교하면 CSR은 거의 처음부터 이긴다.

### 4.1 왜 dense가 이기나 — 네 가지 이유

1. **SIMD 폭**: dense 내부 루프는 `W[r][c]`와 `x[c]`가 둘 다 연속 주소라 NEON이 한 번에 16개 int8을 곱한다. CSR 루프는 `x[col[k]]`가 **gather**(흩어진 주소 읽기)라 벡터화가 사실상 안 된다. 원소당 비용이 스칼라 수준으로 떨어진다.
2. **메모리 트래픽**: CSR은 값 1 B마다 index 2 B를 더 읽는다. 90% sparse면 dense 262 KB 대비 CSR 약 80 KB로 적지만, 그 80 KB를 스칼라 속도로 처리한다.
3. **불규칙한 루프 길이**: 행마다 nnz가 달라 내부 루프 trip count가 매번 다르다. 루프 끝 분기 예측이 자주 틀리고, unroll·software pipelining이 어렵다.
4. **의존성**: 값과 index를 읽고 → 그 index로 x를 읽는 2단 load 체인이다. dense는 주소가 미리 계산되므로 prefetch가 잘 된다.

### 4.2 교차점을 한 줄로 예측하기

```
t_csr(s) ≈ t_csr(0) · (1 − s)             (nnz에 비례)
t_csr(s) < t_dense  ⇔  s > 1 − t_dense / t_csr(0)

M2 NEON:   1 − 5.4 / 97  ≈ 0.94   → 94% 이상에서 CSR 승
M2 스칼라: 1 − 104 / 97  < 0      → 0%부터 이미 비슷, sparsity만큼 이득
```

말로 하면: **교차점 = 1 − (dense 커널의 원소당 비용 / sparse 커널의 원소당 비용)**. dense 커널이 빠를수록(SIMD가 넓을수록, NPU MAC array가 클수록) unstructured sparsity가 이득이 되는 문턱은 100%에 가까워진다. NPU는 MAC 수천 개가 한 박자에 도는 극단적인 dense 엔진이라, 전용 sparse 지원이 없으면 unstructured 0은 **그냥 곱해진다**.

> Don 경험과 연결: 스칼라 코어(Cortex-M0+ 등 SIMD 없는 MCU)라면 dense 원소당 비용과 CSR 원소당 비용이 비슷해서 교차점이 낮다 — unstructured sparsity가 실제 속도 이득이 될 수 있는 드문 경우다. 반대로 Cortex-M4/M7의 SIMD(`SMLAD` 등 16-bit 2-way)나 M55/M85의 Helium, DSP의 넓은 벡터 유닛이 있으면 교차점이 올라간다. 정확한 문턱은 해당 코어에서 이 벤치마크를 돌려서 재는 수밖에 없다(추정 금지 — 캐시 유무, flash wait state, 컴파일러에 따라 다르다).

### 4.3 함정

- **벤치마크를 FLOPs로만 비교**: "MAC 수 10분의 1 = 10배 빠름"은 연산 유닛이 병목이고 커널 효율이 같을 때만 성립한다(D3 roofline).
- **무작위 sparsity로만 측정**: 실제 pruned weight는 행마다 nnz 편차가 크다(global pruning이면 층마다도 다르다). 병렬 처리에서 가장 긴 행이 전체를 붙잡는 **load imbalance**가 생긴다.
- **작은 행렬로 측정**: 행렬이 L1에 다 들어가면 메모리 트래픽 이득이 안 보인다. 실제 레이어 크기로 잰다.

---

## 5. Structured pruning — 채널·head·layer를 통째로

### 5.1 직관

unstructured는 "행렬 안에 구멍을 뚫는 것", structured는 "**행렬을 작게 다시 만드는 것**"이다. conv의 출력 채널(filter) 하나를 지우면 weight 텐서 `[C_out, C_in, k, k]`에서 한 **행**이 사라지고, 그 결과 다음 층의 입력 채널도 하나 사라진다. 남은 것은 여전히 **평범한 dense conv**라서 CMSIS-NN이든 NPU든 아무 커널이나 그대로 빨라진다.

| 무엇을 지우나 | 대상 | 줄어드는 것 |
|---|---|---|
| filter / 출력 채널 | conv `weight[k]`, BN 채널 k, 다음 conv `weight[:, k]` | 파라미터, MAC, activation 메모리 |
| attention head | Q·K·V projection의 해당 head 열, output projection의 해당 행 (B4) | MAC, KV-cache |
| FFN 중간 뉴런 | 첫 Linear의 행, 둘째 Linear의 열 | MAC, 파라미터 |
| layer / block 전체 | residual block 하나 (skip만 남김) | 깊이, latency 직접 감소 |

### 5.2 기준: L1-norm filter pruning

Li et al.(2017, "Pruning Filters for Efficient ConvNets")의 기준은 단순하다 — filter k의 weight 절댓값 합 `‖W[k]‖₁`이 작은 filter부터 지운다. 손으로 해 보면:

```
conv1 filter 4개 (각 1×3×3 = 9개 weight), L1-norm:
  f0 = 2.1   f1 = 0.4   f2 = 1.7   f3 = 0.9
50% 유지 → 큰 것 2개: f0, f2 → keep = [0, 2]
  conv1.weight[4,1,3,3] → [2,1,3,3]
  bn1 (γ, β, μ, σ²)[4]  → [2]         ← keep 위치만
  conv2.weight[8,4,3,3] → [8,2,3,3]   ← 입력 채널 축에서 keep 위치만
```

다른 기준도 있다: BN의 `|γ|`(Network Slimming, Liu et al. 2017 — 학습 때 γ에 L1 정규화를 걸어 0으로 모은 뒤 자름), activation의 평균 크기, Taylor 전개 기반 중요도(loss 변화 추정). 어떤 기준이든 **fine-tune 후 결과는 비슷한 경우가 많다**는 것이 여러 연구의 관찰이다.

### 5.3 의존성 문제 — 한 채널을 지우면 따라 지워야 하는 것들

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="c4m2" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <rect x="20" y="30" width="140" height="64" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="90" y="54" font-size="13" text-anchor="middle">conv1.weight</text> <text x="90" y="74" font-size="12" text-anchor="middle">[32, 1, 3, 3]</text> <text x="90" y="114" font-size="12" text-anchor="middle">행 k 삭제 (출력)</text> <rect x="190" y="30" width="140" height="64" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="260" y="54" font-size="13" text-anchor="middle">bn1 γ β μ σ²</text> <text x="260" y="74" font-size="12" text-anchor="middle">[32]</text> <text x="260" y="114" font-size="12" text-anchor="middle">원소 k 삭제</text> <rect x="360" y="30" width="140" height="64" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="430" y="54" font-size="13" text-anchor="middle">conv2.weight</text> <text x="430" y="74" font-size="12" text-anchor="middle">[64, 32, 3, 3]</text> <text x="430" y="114" font-size="12" text-anchor="middle">열 [:, k] 삭제 (입력)</text> <rect x="530" y="30" width="140" height="64" rx="6" fill="none" stroke="#888" stroke-width="2"/> <text x="600" y="54" font-size="13" text-anchor="middle">conv2 출력</text> <text x="600" y="74" font-size="12" text-anchor="middle">[64]</text> <text x="600" y="114" font-size="12" text-anchor="middle">영향 없음</text> <line x1="160" y1="62" x2="188" y2="62" stroke="currentColor" stroke-width="1.5" marker-end="url(#c4m2)"/> <line x1="330" y1="62" x2="358" y2="62" stroke="currentColor" stroke-width="1.5" marker-end="url(#c4m2)"/> <line x1="500" y1="62" x2="528" y2="62" stroke="currentColor" stroke-width="1.5" marker-end="url(#c4m2)"/> <text x="20" y="150" font-size="13">residual이 있으면: y = x + f(x) — 더하는 두 텐서의 채널이 같아야 한다</text> <rect x="20" y="165" width="150" height="44" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="95" y="192" font-size="12" text-anchor="middle">x : C 채널</text>
<rect x="230" y="165" width="170" height="44" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="315" y="192" font-size="12" text-anchor="middle">f(x) 마지막 conv : C 출력</text> <circle cx="470" cy="187" r="14" fill="none" stroke="currentColor" stroke-width="1.5"/><text x="470" y="192" font-size="14" text-anchor="middle">+</text> <line x1="170" y1="187" x2="228" y2="187" stroke="currentColor" stroke-width="1.5" marker-end="url(#c4m2)"/> <line x1="400" y1="187" x2="454" y2="187" stroke="currentColor" stroke-width="1.5" marker-end="url(#c4m2)"/> <path d="M95,209 L95,232 L470,232 L470,203" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#c4m2)"/> <text x="500" y="182" font-size="12">채널 k를 자르려면</text><text x="500" y="200" font-size="12">skip 경로·다른 블록까지</text><text x="500" y="218" font-size="12">같은 k를 함께 (group)</text>
</svg>
```

그림 5 — 위: conv1의 출력 채널 k를 지우면 BN1의 k번째 통계와 conv2의 입력 슬라이스 `[:, k]`를 함께 지워야 shape이 맞는다. 아래: residual `y = x + f(x)`에서는 더하는 두 텐서의 채널 수가 같아야 하므로, 채널 k를 자르려면 skip 경로와 그 채널을 공유하는 모든 블록에서 **같은 k를 한 묶음(group)으로** 잘라야 한다.

의존성 체크리스트:

- **다음 층 입력**: conv → conv, conv → Linear(flatten 순서 주의: `[C, H, W]`를 flatten하면 채널 k는 연속 `H·W`개 원소)
- **BN**: γ, β, running_mean, running_var 네 개 모두 같은 index로
- **residual / concat**: add는 채널 group 공유, concat은 채널 offset 재계산
- **depthwise conv**(B2): 입력 채널 = 출력 채널이 1:1 묶여 있어 앞 층과 같이 잘라야 함
- **attention head**: head 단위로 Q·K·V의 `head_dim` 열 묶음과 output projection 행 묶음을 함께

이 그래프를 자동으로 추적해 주는 도구로 Torch-Pruning(DepGraph, Fang et al. 2023)이 있다. 원리는 위 체크리스트를 계산 그래프 위에서 자동으로 하는 것이다.

### 5.4 코드 — 채널을 실제로 잘라 작은 모델 만들기

digits를 1×8×8 이미지로 보고, conv 3층(32→64→64 채널) + global average pooling + Linear 분류기를 만든다. `shrink()`가 L1-norm으로 각 conv의 출력 채널을 골라 **새로운 작은 모델**에 weight를 복사한다.

```python
# c4cnn.py — digits(1×8×8)용 작은 CNN과 채널(필터) 단위 structured pruning
import torch, torch.nn as nn

def block(cin, cout):
    return nn.Sequential(nn.Conv2d(cin, cout, 3, padding=1, bias=False),
                         nn.BatchNorm2d(cout), nn.ReLU())

def make_cnn(c=(32, 64, 64)):
    return nn.Sequential(block(1, c[0]), block(c[0], c[1]), block(c[1], c[2]),
                         nn.AdaptiveAvgPool2d(1), nn.Flatten(), nn.Linear(c[2], 10))

def keep_idx(conv, ratio):             # L1-norm이 큰 필터(출력 채널)부터 남긴다
    score = conv.weight.detach().abs().sum(dim=(1, 2, 3))
    k = int(conv.out_channels * ratio)
    return score.topk(k).indices.sort().values

@torch.no_grad()
def shrink(model, ratio):
    blocks, fc = [model[0], model[1], model[2]], model[5]
    keeps = [keep_idx(b[0], ratio) for b in blocks]
    new = make_cnn(tuple(len(k) for k in keeps))
    prev = torch.arange(1)                                   # 입력 채널 1개
    for b_old, b_new, keep in zip(blocks, new[:3], keeps):
        b_new[0].weight.copy_(b_old[0].weight[keep][:, prev])  # 출력 자르고 + 입력도 자름
        for a in ("weight", "bias", "running_mean", "running_var"):
            getattr(b_new[1], a).copy_(getattr(b_old[1], a)[keep])  # BN도 같이
        prev = keep
    new[5].weight.copy_(fc.weight[:, prev]); new[5].bias.copy_(fc.bias)  # FC 입력
    return new
```

`weight[keep][:, prev]`가 그림 5의 "행 삭제 + 열 삭제"를 한 줄로 한 것이다. `shrink(model, 1.0)`의 출력이 원본과 완전히 같은지(최대 차이 0.0) 먼저 확인해 두면 index 실수를 잡을 수 있다(직접 해보기 3번).

이제 학습 → 50% 채널 pruning → fine-tune, 그리고 비교군 두 개(같은 크기를 처음부터 학습 / 같은 모델에 unstructured 75%)를 잰다.

```python
# 채널 50% structured pruning → 진짜로 작은 dense 모델 (파라미터·MAC·지연)
import copy, time, torch
from c4common import get_data, train, accuracy
from c4cnn import make_cnn, shrink
torch.manual_seed(0); torch.set_num_threads(1)
Xtr, Xte, ytr, yte = [t.view(-1, 1, 8, 8) if t.dim() == 2 else t for t in get_data()]
big = train(make_cnn(), Xtr, ytr, epochs=30).eval()
small = shrink(big, 0.5).eval()
print(f"big   acc={accuracy(big, Xte, yte):.4f}")
print(f"small acc={accuracy(small, Xte, yte):.4f} (fine-tune 전)")
train(small.train(), Xtr, ytr, epochs=10, seed=1).eval()
print(f"small acc={accuracy(small, Xte, yte):.4f} (fine-tune 10 epoch 후)")
torch.manual_seed(1); scratch = train(make_cnn((16, 32, 32)), Xtr, ytr, epochs=30).eval()
print(f"같은 크기 scratch 학습 acc={accuracy(scratch, Xte, yte):.4f}")

unst = copy.deepcopy(big)                  # 비교: 같은 모델에 unstructured 75% (0만 채움)
with torch.no_grad():
    for b in unst[:3]:
        w = b[0].weight; w.mul_(w.abs() > w.abs().flatten().kthvalue(int(0.75 * w.numel())).values)
def macs(m, hw=96):        # conv MAC = Cout·Cin·3·3·H·W (padding=1이라 H,W 유지)
    return sum(b[0].out_channels * b[0].in_channels * 9 * hw * hw for b in m[:3])
def lat(m, hw=96):         # 96×96 입력 한 장, 스레드 1개, 30회 중 최소값(ms)
    x = torch.randn(1, 1, hw, hw); best = 1e9
    with torch.no_grad():
        for _ in range(30):
            t0 = time.perf_counter(); m(x); best = min(best, time.perf_counter() - t0)
    return best * 1e3
for name, m in (("big", big), ("unst75%", unst), ("small", small)):
    p = sum(int(q.count_nonzero()) for q in m.parameters())
    print(f"{name:7s} nonzero params={p:6d}  MACs@96x96={macs(m)/1e6:6.1f}M  latency={lat(m):5.2f} ms")
```

```text
big   acc=0.9944
small acc=0.1889 (fine-tune 전)
small acc=0.9833 (fine-tune 10 epoch 후)
같은 크기 scratch 학습 acc=0.9963
big     nonzero params= 56554  MACs@96x96= 512.3M  latency= 5.06 ms
unst75% nonzero params= 14866  MACs@96x96= 512.3M  latency= 5.05 ms
small   nonzero params= 14458  MACs@96x96= 128.7M  latency= 1.73 ms
```

(지연 값은 실행마다 달라진다 — 다시 돌렸을 때 5.52 / 5.55 / 1.93 ms로 절대값은 바뀌었지만 비율(약 2.9배)은 같았다. 모델은 8×8 digits로 학습했지만 conv + global pooling 구조라 입력 크기와 무관하게 돌아간다. 지연 측정은 차이가 잘 보이도록 96×96 입력으로 했다.)

출력에서 볼 것은 네 가지다.

1. **nonzero 파라미터는 둘 다 약 1/4인데**(unst75% 14,866 vs small 14,458) unstructured는 지연이 5.05 ms로 **전혀 줄지 않았고**, structured는 5.06 → 1.73 ms로 **2.9배** 빨라졌다. 4절 결론의 PyTorch CPU 버전이다.
2. MAC은 512.3M → 128.7M으로 **3.98배** 줄었는데 지연은 2.9배만 줄었다. 채널이 작아지면 커널 효율(SIMD 활용률, 고정 오버헤드)이 떨어지기 때문이다. MAC 감소 ≠ 지연 감소 — 항상 재야 한다.
3. **fine-tune 전 0.19**: 채널 절반을 한꺼번에 빼면 다음 층이 기대하던 입력(특히 BN β가 만든 상수 성분)이 사라져 사실상 망가진다. structured pruning에서 fine-tune은 선택이 아니라 필수다.
4. 같은 크기를 **처음부터 학습(scratch)** 한 모델이 0.9963으로 pruned + FT(0.9833)보다 오히려 좋았다. Liu et al.(2019, "Rethinking the Value of Network Pruning")이 보고한 현상과 같은 방향이다 — structured pruning으로 얻는 것이 "좋은 weight"라기보다 "좋은 **구조**(층별 채널 수)"일 수 있다. 작은 데이터셋·작은 모델에서는 scratch 학습을 반드시 비교군에 넣는다. (단, 큰 모델에서 처음부터 학습하는 비용이 크면 pruning + FT가 더 싸다.)

MAC 손계산으로 검증: 96×96 = 9,216 위치.

```
big  : 32·1·9·9216 + 64·32·9·9216 + 64·64·9·9216 = 2.65M + 169.9M + 339.7M = 512.3M
small: 16·1·9·9216 + 32·16·9·9216 + 32·32·9·9216 = 1.33M +  42.5M +  84.9M = 128.7M
```

말로 하면: 가운데 층들은 입력과 출력 채널이 **둘 다** 절반이 되므로 MAC이 1/4이 된다. 채널 50% pruning ≈ MAC 75% 감소. 첫 층만 입력 채널이 그대로라 1/2.

### 5.5 head · layer pruning (transformer)

- **head**: Michel et al.(2019, "Are Sixteen Heads Really Better than One?")은 학습된 transformer에서 상당수 head를 지워도 정확도가 거의 안 변하는 경우를 보였다. head를 지우면 Q·K·V 폭과 KV-cache(D5)가 함께 준다.
- **layer**: residual 구조에서는 블록을 통째로 빼도 skip이 신호를 전달한다. batch=1 순차 실행에서 가장 직접적인 latency 감소다.
- **FFN 폭**: transformer 파라미터의 큰 몫이 FFN이라 중간 뉴런을 줄이는 효과가 크다.

`torch.nn.utils.prune.ln_structured(module, "weight", amount, n=1, dim=0)`도 있지만 2.4절과 같은 이유로 **채널을 0으로 채울 뿐 shape을 줄이지 않는다**. 실제 이득은 `shrink()`처럼 작은 모델을 새로 만들어야 생긴다.

### 5.6 함정

- **flatten 순서**: conv → Linear 사이 flatten이 있으면 채널 k는 `[k·H·W, (k+1)·H·W)` 구간이다. 채널 index를 그대로 Linear 열 index로 쓰면 틀린다.
- **정렬 요구**: NPU·SIMD 커널은 채널 수가 8/16/32의 배수일 때 가장 효율이 좋은 경우가 많다(C6). 채널을 61개로 만들면 64개보다 느릴 수도 있다 — 남길 채널 수를 배수로 반올림한다.
- **BN folding 전후**: 배포 전 BN을 conv에 접는다면(B1), 접은 뒤 weight 기준으로 L1-norm을 다시 보면 순위가 바뀔 수 있다.

---

## 6. N:M semi-structured sparsity (2:4)

### 6.1 정의

**N:M sparsity**: weight를 입력(reduction) 방향으로 연속 M개씩 묶고, 각 묶음에서 **정확히 N개만** 남긴다. 가장 유명한 것이 **2:4** — 4개 중 2개를 남기므로 sparsity는 정확히 50%다.

손으로 해 보자. 한 행 8개 weight:

```
w     = [ 0.9  -0.1   0.3  -0.7 │ 0.05  0.2  -0.6   0.4 ]
그룹 0: |w| = 0.9 0.1 0.3 0.7 → 큰 2개: 위치 0, 3
그룹 1: |w| = 0.05 0.2 0.6 0.4 → 큰 2개: 위치 2, 3
mask  = [ 1 0 0 1 │ 0 0 1 1 ]
```

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="c4m1" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="110" y="66" font-size="13" text-anchor="end">원본 w (8개)</text> <rect x="120" y="40" width="48" height="40" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="144.0" y="65" font-size="13" text-anchor="middle">0.9</text> <rect x="172" y="40" width="48" height="40" fill="none" fill-opacity="0.35" stroke="#888"/> <text x="196.0" y="65" font-size="13" text-anchor="middle">-0.1</text> <rect x="224" y="40" width="48" height="40" fill="none" fill-opacity="0.35" stroke="#888"/> <text x="248.0" y="65" font-size="13" text-anchor="middle">0.3</text> <rect x="276" y="40" width="48" height="40" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="300.0" y="65" font-size="13" text-anchor="middle">-0.7</text> <rect x="328" y="40" width="48" height="40" fill="none" fill-opacity="0.35" stroke="#888"/> <text x="352.0" y="65" font-size="13" text-anchor="middle">0.05</text> <rect x="380" y="40" width="48" height="40" fill="none" fill-opacity="0.35" stroke="#888"/> <text x="404.0" y="65" font-size="13" text-anchor="middle">0.2</text> <rect x="432" y="40" width="48" height="40" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="456.0" y="65" font-size="13" text-anchor="middle">-0.6</text> <rect x="484" y="40" width="48" height="40" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="508.0" y="65" font-size="13" text-anchor="middle">0.4</text> <text x="222" y="32" font-size="12" text-anchor="middle">그룹 0 (4칸 중 큰 2개)</text> <text x="430" y="32" font-size="12" text-anchor="middle">그룹 1</text> <line x1="326" y1="38" x2="326" y2="84" stroke="currentColor" stroke-dasharray="3,2"/> <text x="110" y="176" font-size="13" text-anchor="end">values (4개)</text> <rect x="120" y="150" width="48" height="40" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="144.0" y="175" font-size="13" text-anchor="middle">0.9</text>
<rect x="172" y="150" width="48" height="40" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="196.0" y="175" font-size="13" text-anchor="middle">-0.7</text> <rect x="224" y="150" width="48" height="40" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="248.0" y="175" font-size="13" text-anchor="middle">-0.6</text> <rect x="276" y="150" width="48" height="40" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="300.0" y="175" font-size="13" text-anchor="middle">0.4</text> <text x="110" y="226" font-size="13" text-anchor="end">index (2-bit)</text> <rect x="120" y="200" width="48" height="40" fill="none" stroke="#4a7bd0"/> <text x="144.0" y="225" font-size="13" text-anchor="middle">00</text> <rect x="172" y="200" width="48" height="40" fill="none" stroke="#4a7bd0"/> <text x="196.0" y="225" font-size="13" text-anchor="middle">11</text> <rect x="224" y="200" width="48" height="40" fill="none" stroke="#e08a3c"/> <text x="248.0" y="225" font-size="13" text-anchor="middle">10</text> <rect x="276" y="200" width="48" height="40" fill="none" stroke="#e08a3c"/> <text x="300.0" y="225" font-size="13" text-anchor="middle">11</text> <line x1="210" y1="88" x2="210" y2="144" stroke="currentColor" stroke-width="1.5" marker-end="url(#c4m1)"/> <text x="220" y="120" font-size="12">압축: 남은 값 + 그룹 안 위치(0~3)</text> <text x="348" y="164" font-size="12">HW: index로 x[4g+i]를 mux로 골라</text> <text x="348" y="182" font-size="12">MAC 8번 → 4번 (규칙적이라 파이프라인 유지)</text> <text x="348" y="208" font-size="12">int8: 4 B → 2 B 값 + 0.5 B index = 2.5 B</text> <text x="348" y="226" font-size="12">fp16: 8 B → 4 B + 0.5 B = 4.5 B</text>
</svg>
```

그림 6 — 2:4 압축 포맷. 값은 절반만 저장하고, 각 값이 그룹 안 어느 위치(0~3)였는지 2-bit index로 적는다. HW는 이 index로 x의 해당 원소를 mux로 골라 MAC을 절반만 한다. 그룹 크기가 고정이라 루프 길이·메모리 배치가 규칙적이다.

저장 크기 (M=4 그룹당):

```
int8 : dense 4 B  → 값 2 B + index 2개 × 2 bit = 0.5 B  → 2.5 B   (62.5%)
fp16 : dense 8 B  → 값 4 B + 0.5 B                     → 4.5 B   (56.25%)
```

말로 하면: 2:4는 index가 원소당 1 bit(값 하나당 2 bit)로 **CSR보다 훨씬 싸고**, 무엇보다 **모든 행·모든 그룹의 모양이 같아서** HW가 파이프라인을 멈추지 않고 처리할 수 있다.

### 6.2 HW 지원 — 여기가 핵심

- **NVIDIA Ampere(A100 등) 이후의 sparse tensor core**는 2:4 형식의 weight를 받아 dense 대비 **최대 2배의 행렬곱 수학 처리량**을 낸다(NVIDIA 문서·Mishra et al. 2021). 소프트웨어 쪽은 NVIDIA의 ASP(Automatic SParsity), TensorRT, cuSPARSELt가 이 경로를 쓴다. PyTorch에도 2:4 semi-structured 텐서 변환 API(`torch.sparse.to_sparse_semi_structured`)가 있지만 **CUDA GPU 전용**이다.
- 실제 end-to-end 속도 향상은 2배보다 작은 경우가 많다 — 행렬곱 외 연산(activation, 메모리 이동)은 그대로이기 때문이다.
- **edge NPU**: 일부 NPU·DSP 벤더는 structured/block sparsity나 weight 압축 지원을 내세운다. 다만 어떤 패턴(N:M인지 block인지)을, 어떤 정밀도·어떤 op에서 지원하는지는 제품마다 다르고 공개 정도도 다르다. **"sparsity 지원"이라는 한 줄을 보면 반드시 패턴·op·정밀도·실측 이득을 벤더에 물어본다.**

HW 지원이 없으면 어떻게 되는지는 4절 실측에 이미 있다 — 소프트웨어 2:4 커널(`sp24`)은 49.15 µs로 **CSR(48.06 µs)과 같고 NEON dense(5.37 µs)보다 9배 느리다**. 규칙적인 포맷이라도 결국 gather(`xg[m & 3]`)가 들어가서 자동 벡터화가 안 되기 때문이다. 2:4는 "HW가 mux를 박아 둔" 칩에서만 공짜 2배다.

### 6.3 코드 — 2:4 mask와 fine-tune, unstructured 50%와 비교

```python
# 2:4 semi-structured mask vs unstructured 50% (같은 sparsity, 다른 자유도)
import copy, torch
from c4common import trained_mlp, accuracy, linears, train, global_masks, apply_masks
base, (Xtr, Xte, ytr, yte) = trained_mlp()

def nm_mask(w, n=2, m=4):              # 입력 방향 연속 m개마다 |w| 상위 n개만 남김
    g = w.detach().abs().reshape(-1, m)                 # [out·in/m, m]
    keep = g.topk(n, dim=1).indices
    return torch.zeros_like(g).scatter_(1, keep, 1.0).reshape(w.shape)

w = torch.tensor([[0.9, -0.1, 0.3, -0.7, 0.05, 0.2, -0.6, 0.4]])
print("mask:", nm_mask(w).int().tolist()[0])

def nm_masks(model):
    return {l.weight: nm_mask(l.weight) for l in linears(model)}
print("method            no-FT    FT(10ep)")
for name, fn in (("unstructured 50%", lambda m: global_masks(m, 0.5)), ("2:4", nm_masks)):
    m = copy.deepcopy(base); mk = fn(m); apply_masks(mk)
    a0 = accuracy(m, Xte, yte)
    train(m, Xtr, ytr, epochs=10, masks=mk, seed=1)
    print(f"{name:16s}  {a0:.4f}   {accuracy(m, Xte, yte):.4f}")

l = linears(base)[0]; W = l.weight.detach()      # 첫 층만, 둘 다 정확히 50% 자를 때 출력 오차
thr = W.abs().flatten().kthvalue(W.numel() // 2).values
for name, mk in (("unstructured", (W.abs() > thr).float()), ("2:4", nm_mask(W))):
    y, y2 = Xte @ W.T, Xte @ (W * mk).T
    print(f"L0 50% {name:12s}: kept |w| sum={(W.abs()*mk).sum():.1f}  rel.err={(y-y2).norm()/y.norm():.4f}")
```

```text
mask: [1, 0, 0, 1, 0, 0, 1, 1]
method            no-FT    FT(10ep)
unstructured 50%  0.9741   0.9704
2:4               0.9667   0.9759
L0 50% unstructured: kept |w| sum=1059.9  rel.err=0.2401
L0 50% 2:4         : kept |w| sum=989.4  rel.err=0.3056
```

출력에서 볼 것: 손계산 mask `[1,0,0,1,0,0,1,1]`이 그대로 나온다. 2:4는 "각 그룹에서 2개"라는 제약 때문에 같은 50%라도 **덜 좋은 weight를 남긴다** — 남긴 절댓값 합이 1059.9 → 989.4로 작고, 첫 층 출력 오차가 0.24 → 0.31로 크다. 그래서 FT 전 정확도가 약간 낮다(0.9741 vs 0.9667). 하지만 **FT 후엔 차이가 사라진다**(0.9704 vs 0.9759 — 540장 중 3장 차이, 잡음 수준). NVIDIA가 권장하는 절차도 "dense 학습 → 2:4 mask → 같은 시간 정도 재학습"이다. 작은 모델·LLM처럼 여유가 적은 모델에서는 2:4의 정확도 손실이 더 커질 수 있다.


---

## 7. Activation sparsity — weight 말고 입력 쪽의 0

### 7.1 직관

지금까지는 weight의 0(**static sparsity** — 배포 전에 정해짐)을 봤다. 그런데 ReLU(B1)는 음수를 전부 0으로 만든다. 그래서 ReLU 뒤의 activation에는 **입력마다 다른 위치에** 0이 생긴다. 이것이 **activation sparsity**이고, 실행 중에 정해지므로 **dynamic sparsity**라고 부른다.

- weight sparsity: 컴파일 때 알 수 있다 → 포맷을 미리 만들 수 있다
- activation sparsity: 실행 때만 안다 → HW가 실시간으로 0을 감지해서 건너뛰어야 한다(**zero-skipping**)

### 7.2 코드 — 학습된 MLP의 ReLU 출력에 0이 얼마나 있나

```python
# ReLU 출력의 0 비율 = activation sparsity. 입력마다 달라지는 "동적" sparsity
import torch
from c4common import trained_mlp, linears
model, (Xtr, Xte, ytr, yte) = trained_mlp()
L0, L1, L2 = linears(model)
with torch.no_grad():
    h1 = torch.relu(L0(Xte)); h2 = torch.relu(L1(h1))
for name, h in (("h1 (L1의 입력)", h1), ("h2 (L2의 입력)", h2)):
    z = (h == 0).float()
    per_sample = z.mean(1)
    dead = (z.mean(0) == 1).sum().item()      # 모든 샘플에서 0인 채널 = 죽은 뉴런
    print(f"{name}: zero {z.mean():.1%}  sample별 min {per_sample.min():.1%} "
          f"max {per_sample.max():.1%}  dead channels {dead}/256")
# 0 입력을 건너뛰면 줄어드는 MAC (L1: 256×256, L2: 10×256)
full = 256 * 256 + 10 * 256
skip = ((h1 != 0).sum(1) * 256 + (h2 != 0).sum(1) * 10).float().mean().item()
print(f"L1+L2 MAC: dense {full}  zero-skip 평균 {skip:.0f}  ({skip/full:.1%})")
```

```text
h1 (L1의 입력): zero 17.7%  sample별 min 9.0% max 34.0%  dead channels 13/256
h2 (L2의 입력): zero 33.4%  sample별 min 22.7% max 46.9%  dead channels 55/256
L1+L2 MAC: dense 68096  zero-skip 평균 55668  (81.7%)
```

출력에서 볼 것:

1. 0 비율이 **샘플마다 다르다**(h2는 22.7% ~ 46.9%). 즉 zero-skipping HW의 지연도 입력에 따라 흔들린다 — real-time 기기에서는 **최악 경우(가장 덜 sparse한 입력)** 로 deadline을 잡아야 한다(D6).
2. 이 작은 MLP에서 0 입력을 건너뛰면 MAC은 81.7%로 약 18%만 준다. CNN의 깊은 층에서는 ReLU 뒤 0 비율이 이보다 훨씬 높은 경우가 흔히 보고된다. 반대로 GELU·SiLU(transformer, 최신 CNN)는 정확한 0을 거의 만들지 않아 이 이득이 없다.
3. **dead channel**: h2의 55개 채널은 test 540장 **모두**에서 0이다. 이 채널은 L1의 해당 출력 행과 L2의 해당 입력 열을 지워도 test 결과가 바뀌지 않는다 — activation 통계가 structured pruning의 공짜 후보를 알려 준 셈이다(단, 학습 데이터 전체·실제 분포에서도 죽어 있는지 확인해야 한다).

### 7.3 zero-skipping HW (개념)

연구용·상용 가속기 중에는 activation의 0을 감지해서 (a) MAC을 **clock/data gating으로 멈춰 전력을 아끼거나**, (b) 0을 건너뛰어 **사이클 자체를 줄이는** 설계가 있다. 예로 MIT의 Eyeriss는 0 activation에 대해 연산을 gating하고 activation을 run-length 압축해 DRAM 트래픽을 줄인 것으로 알려져 있다. (a)는 속도는 같고 에너지만 줄며 구현이 쉽고, (b)는 속도도 빨라지지만 PE 간 부하 불균형을 처리하는 제어 로직이 필요하다.

펌웨어 비유: (a)는 "데이터가 0이면 DMA는 돌지만 쓰기 enable을 내림", (b)는 "scatter-gather 리스트로 0인 블록 자체를 전송 목록에서 빼기".

### 7.4 dynamic sparsity의 다른 얼굴들

입력에 따라 연산 일부를 건너뛰는 기법은 더 있다 — 쉬운 입력은 중간 층에서 끝내는 **early exit**, 토큰마다 전문가 FFN 몇 개만 쓰는 **Mixture-of-Experts**(연산은 줄지만 weight는 전부 저장해야 해서 edge 메모리엔 불리), LLM FFN의 활성 뉴런만 읽는 **contextual sparsity** 연구(개념만).

---

## 8. LLM pruning — magnitude가 약해지는 이유와 Wanda

### 8.1 왜 LLM에서는 magnitude pruning이 잘 안 되나

LLM의 activation에는 **특정 채널만 값이 수십 배 큰 outlier feature**가 있다는 것이 잘 알려져 있다(C3의 SmoothQuant 동기와 같은 현상). 출력 `y = W·x`에서 weight `w_ij`의 실제 기여는 `w_ij · x_j`다. `|w_ij|`가 작아도 `x_j`가 항상 크면 그 weight는 중요하다. magnitude pruning은 `x`를 보지 않으니 이런 weight를 지워 버린다.

### 8.2 SparseGPT와 Wanda (개념)

- **SparseGPT**(Frantar & Alistarh, 2023): 층별로 "pruning 후 출력이 원본 출력과 최대한 같도록" 남은 weight를 **보정(update)** 한다. calibration activation으로 만든 2차 정보(Hessian 근사, `XᵀX`)를 쓰는 OBS(Optimal Brain Surgeon) 계열이고, GPTQ(C3)와 같은 뿌리다. 재학습 없이 one-shot으로 큰 LLM을 50% 정도 자를 수 있다고 보고했다.
- **Wanda**(Sun et al., 2023, "Pruning by Weights and activations"): 훨씬 단순하다. 중요도를

```
S_ij = |W_ij| · ‖X_j‖₂          X_j = calibration 데이터에서 입력 채널 j의 값들
```

로 정의하고, **출력 행(i)마다** S가 작은 것을 지운다. weight 보정도 재학습도 없다. 말로 하면: "weight 크기 × 그 weight가 곱해질 입력의 평소 크기"로 기여를 추정한다. 행마다 비교하는 이유는 각 출력 뉴런이 입력을 고루 잃게 해서 한 뉴런이 망가지는 것을 막기 위해서다.

손계산: 입력 2채널, `‖X_0‖ = 1`, `‖X_1‖ = 20`(outlier), 한 행 `w = [0.5, 0.1]`에서 하나만 남긴다면

```
magnitude: |0.5| > |0.1|        → w_1 제거 → 잃는 기여 ≈ 0.1 × 20 = 2.0
Wanda    : 0.5·1 = 0.5 < 0.1·20 = 2.0 → w_0 제거 → 잃는 기여 ≈ 0.5 × 1 = 0.5
```

### 8.3 코드 — outlier 채널이 있는 Linear 층에서 비교

```python
# Wanda식 점수 |W|·‖X_j‖ vs magnitude |W| — 한 Linear 층, 50% pruning 후 출력 오차
import torch
torch.manual_seed(0)
din, dout, ncal = 256, 256, 512
scale = torch.ones(din); scale[torch.randperm(din)[:8]] = 20.0   # 입력 8채널이 outlier (LLM처럼)
def acts(n): return torch.randn(n, din) * scale
W = torch.randn(dout, din) / din ** 0.5
Xcal, Xtest = acts(ncal), acts(2000)              # calibration / 평가 데이터 분리

def prune_rowwise(score, s=0.5):                  # 각 출력 행마다 점수 하위 s 제거
    k = int(din * s)
    drop = score.topk(k, dim=1, largest=False).indices
    return torch.ones_like(W).scatter_(1, drop, 0.0)

def err(mask):
    y, y2 = Xtest @ W.T, Xtest @ (W * mask).T
    return ((y - y2).norm() / y.norm()).item()

xnorm = Xcal.norm(dim=0)                          # 입력 채널별 ‖X_j‖₂ (calibration에서)
m_mag   = prune_rowwise(W.abs())
m_wanda = prune_rowwise(W.abs() * xnorm)          # 브로드캐스트: [dout,din] × [din]
print(f"magnitude rel.err = {err(m_mag):.4f}")
print(f"wanda     rel.err = {err(m_wanda):.4f}")
out = scale > 1
print("outlier 채널 weight 유지율: magnitude "
      f"{m_mag[:, out].mean():.2f}, wanda {m_wanda[:, out].mean():.2f}")
```

```text
magnitude rel.err = 0.2712
wanda     rel.err = 0.0769
outlier 채널 weight 유지율: magnitude 0.49, wanda 0.97
```

출력에서 볼 것: 같은 50%를 잘랐는데 출력 오차가 **0.27 → 0.077로 3.5배 작다**. magnitude는 outlier 채널의 weight도 절반(0.49)을 지웠고, Wanda는 97%를 지켰다. 오차 대부분이 outlier 채널에서 나오기 때문이다.

outlier를 인위적으로 넣은 실험이니, 실제 학습된 층에서도 확인한다. 1절 MLP의 L1(256→256)에 L0 뒤 ReLU 출력을 calibration으로 쓴다.

```python
# 실제 학습된 MLP의 L1(256→256)에서 같은 비교 — 입력은 L0 뒤 ReLU 출력
import torch
from c4common import trained_mlp, linears
model, (Xtr, Xte, ytr, yte) = trained_mlp()
L0, L1, _ = linears(model)
with torch.no_grad():
    A_cal, A_te = torch.relu(L0(Xtr[:256])), torch.relu(L0(Xte))   # calibration 256장
    W = L1.weight.detach()
    xn = A_cal.norm(dim=0)
    print(f"입력 채널 norm: min={xn.min():.2f} median={xn.median():.2f} max={xn.max():.2f}")
    for s in (0.5, 0.7):
        k = int(W.shape[1] * s)
        res = []
        for score in (W.abs(), W.abs() * xn):
            mask = torch.ones_like(W).scatter_(1, score.topk(k, 1, largest=False).indices, 0.0)
            y, y2 = A_te @ W.T, A_te @ (W * mask).T
            res.append(((y - y2).norm() / y.norm()).item())
        print(f"sparsity {s:.0%}: magnitude rel.err={res[0]:.4f}  wanda rel.err={res[1]:.4f}")
```

```text
입력 채널 norm: min=0.00 median=8.01 max=16.66
sparsity 50%: magnitude rel.err=0.1239  wanda rel.err=0.1028
sparsity 70%: magnitude rel.err=0.3033  wanda rel.err=0.2526
```

출력에서 볼 것: outlier가 극단적이지 않은 작은 MLP에서도 Wanda가 17% 정도 오차가 작다. `min=0.00`은 7.2절의 dead channel이다 — Wanda 점수는 그 채널의 weight를 0점으로 매겨 먼저 지운다(어차피 곱해질 입력이 0이므로 정확한 판단). 입력 분포가 고를수록 두 방법의 차이는 줄고, LLM처럼 outlier가 심할수록 벌어진다.

### 8.4 LLM pruning 실무 감각 (개념)

- LLM에서 unstructured 50%는 SparseGPT/Wanda로 비교적 잘 되지만 **2:4는 정확도 손실이 더 크다**고 보고된다. 그런데 HW 속도 이득은 2:4 쪽만 난다 — 정확도와 속도의 줄다리기.
- decode(D5)는 memory-bound라 **읽는 바이트를 줄이는 것**이 핵심이다. 그래서 LLM에서는 sparsity보다 4-bit weight 양자화(C3)가 먼저 쓰이는 경우가 많다.
- structured pruning + distillation으로 작은 LLM을 만드는 경로(예: NVIDIA Minitron 보고)는 C5와 이어진다.

---

## 9. "TOPS with sparsity" — 데이터시트 읽는 법 (→ M4)

가속기 데이터시트의 TOPS 숫자에는 조건이 숨어 있다. sparsity가 그중 하나다.

```
TOPS = MAC 개수 × 2 (곱+덧셈) × clock
sparse TOPS = dense TOPS × 2            ← 2:4 sparse 연산을 "0을 곱한 것까지 셌을 때"
```

예: NVIDIA A100은 데이터시트에 INT8 Tensor Core 성능을 624 TOPS, sparsity 적용 시 1,248 TOPS로 **두 숫자를 함께** 표기한다. FP16도 312 → 624 TFLOPS로 같은 2배다. 일부 edge 모듈 스펙에서는 sparse 기준 숫자가 헤드라인에 오고 dense는 각주로 가는 경우가 있다.

읽는 법 체크리스트:

- [ ] 헤드라인 TOPS가 **dense인가 sparse인가**? sparse면 우리 모델이 2:4로 재학습돼 있어야 그 숫자에 다가간다. 아니면 절반으로 읽는다.
- [ ] **어떤 정밀도**(INT4? INT8? FP16?) 기준인가? INT4 sparse TOPS는 INT8 dense의 4배로 보일 수 있다.
- [ ] sparsity 가속이 **어떤 op**(행렬곱만? conv도? attention도?)에 적용되나?
- [ ] 우리 모델에서 행렬곱이 차지하는 비율은? 나머지(activation, 메모리 이동)는 sparsity와 무관하다 — Amdahl의 법칙.
- [ ] memory-bound 층(depthwise, decode GEMV)에서는 TOPS 자체가 병목이 아니다(D3). sparsity는 여기서 **읽는 바이트**를 줄일 때만 도움이 된다.
- [ ] 실측: 같은 모델의 dense 버전과 2:4 버전을 벤더 툴체인으로 각각 컴파일해 end-to-end 지연·전력을 잰다(M2).

말로 하면: sparse TOPS는 "전용 패턴으로 재학습한 모델이, 그 패턴을 지원하는 op에서, compute-bound일 때"의 상한이다. Don의 margin sign-off 경험 그대로 — 스펙의 최대값이 아니라 **우리 워크로드 조건의 실측값**으로 판단한다.

---

## 10. Pruning + Quantization (+ Distillation) — 합쳐 쓰기

### 10.1 Deep Compression — 역사적 기준점

Han, Mao, Dally(2016, "Deep Compression")는 **pruning → trained quantization(weight sharing) → Huffman coding**을 쌓아 AlexNet을 35배, VGG-16을 49배 줄이면서 정확도를 유지했다고 보고했다. 교훈: (1) 기법은 **곱해진다** — 개수·비트·중복을 각각 줄인다. (2) 그 이득은 주로 **저장·전송**(flash, DRAM 대역폭)이었고, 실행 가속에는 전용 HW(같은 그룹의 EIE)가 필요했다. (3) 단계마다 재학습한다.

### 10.2 순서

일반적인 권장 순서:

```
(0) 필요하면 distillation으로 작은 구조의 student를 학습 (C5)
(1) structured pruning → fine-tune             ← 구조가 먼저 정해져야 나머지가 의미 있다
(2) (HW가 지원하면) 2:4 / unstructured → fine-tune
(3) quantization: PTQ, 부족하면 QAT (C2)       ← 마지막. scale은 최종 weight 분포로 정해야 한다
(4) 저장 포맷: bitmask/압축 + 벤더 컴파일러
```

말로 하면: **pruning이 먼저, 양자화는 나중**. 양자화 scale(C1)은 weight 분포로 정해지는데 pruning이 분포를 바꾸기 때문이다. QAT 중에 mask를 함께 유지하면 두 단계를 한 번의 재학습으로 합칠 수도 있다. 주의할 점 하나 — 양자화로 반올림하면 **작은 weight가 0으로 떨어져 sparsity가 저절로 늘어나기도** 하고, pruning된 0은 대칭 양자화에서 정확히 0으로 표현된다(zero-point가 0이 아닌 비대칭 양자화에서도 실수 0은 정확히 표현되도록 zero-point를 정수로 잡는다 — C1).

### 10.3 코드 — 90% pruning + int8, 저장 크기 비교

```python
# prune(90%, iterative+FT) → int8 양자화 → 저장 크기: dense / bitmask / zlib 압축
import zlib, numpy as np, torch
from c4common import trained_mlp, accuracy, linears, train, global_masks, apply_masks
model, (Xtr, Xte, ytr, yte) = trained_mlp()
for s in (0.5, 0.7, 0.8, 0.9):                       # 2.5절의 스케줄 그대로
    mk = global_masks(model, s); apply_masks(mk); train(model, Xtr, ytr, epochs=10, masks=mk, seed=1)
print(f"pruned fp32 acc = {accuracy(model, Xte, yte):.4f}")

blobs, bitmask_bytes = [], 0
with torch.no_grad():
    for l in linears(model):                         # per-tensor symmetric int8 (C1)
        w = l.weight; sc = w.abs().max() / 127
        q = torch.clamp(torch.round(w / sc), -127, 127)
        l.weight.copy_(q * sc)                       # fake-quant으로 정확도 확인
        qn = q.to(torch.int8).numpy()
        blobs.append(qn.tobytes())
        bitmask_bytes += qn.size // 8 + int(np.count_nonzero(qn))
print(f"pruned+int8 acc = {accuracy(model, Xte, yte):.4f}")
n = sum(l.weight.numel() for l in linears(model))
dense8 = b"".join(blobs)
print(f"fp32 dense      : {n * 4:6d} B")
print(f"int8 dense      : {len(dense8):6d} B")
print(f"int8 bitmask    : {bitmask_bytes:6d} B")
print(f"int8 dense+zlib : {len(zlib.compress(dense8, 9)):6d} B")
```

```text
pruned fp32 acc = 0.9833
pruned+int8 acc = 0.9833
fp32 dense      : 337920 B
int8 dense      :  84480 B
int8 bitmask    :  19008 B
int8 dense+zlib :  15318 B
```

출력에서 볼 것: 90% pruning + int8에서 정확도는 그대로(0.9833)이고, weight 저장 크기는 fp32 dense 337,920 B → int8 bitmask **19,008 B(약 1/18)**, 범용 압축 zlib로는 15,318 B(약 1/22)까지 준다. 비트맵 크기 손계산: 84,480/8 = 10,560 B + nonzero 8,448개 × 1 B = 19,008 B. 여기서 핵심 — **int8 dense(84,480 B)로 저장하면 pruning 이득이 0**이다. 0을 저장하지 않는 포맷이나 압축이 있어야 flash가 준다. (bias는 계산에서 뺐다.)

---

## 11. 임베디드 관점에서 다시 보기

### 11.1 HW 종류별 결정표

| HW | unstructured | N:M (2:4) | block | structured(채널·head·layer) |
|---|---|---|---|---|
| 스칼라 MCU (Cortex-M0+ 등) | 매우 높은 sparsity에서 속도 이득 가능. 커스텀 커널 필요 | 이득 작음 | 커스텀 커널 필요 | **항상 이득** |
| SIMD MCU/DSP (M4/M7 DSP ext, Helium, HiFi, HVX) | 속도 이득 거의 없음. 압축 저장으로 flash만 이득 | 전용 명령 없으면 이득 없음 | 블록이 SIMD 폭과 맞으면 가능 | **항상 이득** |
| 모바일 CPU (NEON) | 4절처럼 ~95% 이상에서만. XNNPACK 등 일부 sparse 커널 존재 | 이득 없음 | 일부 라이브러리 | **항상 이득** |
| NVIDIA GPU (Ampere 이후) | 거의 이득 없음 | **HW 지원, 행렬곱 최대 2×** | 라이브러리 의존 | 이득 |
| edge NPU | 보통 그냥 0을 곱함. weight 압축 지원 시 대역폭·저장 이득 | 벤더·제품별 확인 필요 | 벤더별 확인 필요 | **항상 이득** (채널 정렬 주의) |

(스칼라 MCU·모바일 CPU 행은 4절 측정의 논리에서 나온 판단이고, NPU 행은 제품마다 달라서 일반론으로만 쓴 것이다 — 실제 칩에서는 벤더 문서와 측정으로 확인한다.)

### 11.2 "MCU용 CNN을 줄여라" — 실무 절차

예를 들어 Hark 같은 웨어러블의 always-on 제스처/착용 감지 CNN이 SRAM·flash 예산을 넘는다고 하자(추정 시나리오).

1. **예산부터**: flash(weight), SRAM(peak activation, D2), 지연(프레임 주기, D6), 에너지(D7)의 목표를 숫자로 적는다.
2. **구조 먼저 의심**: depthwise separable(B2), 채널 폭 multiplier, 입력 해상도·윈도우 길이 줄이기가 pruning보다 효과가 클 수 있다(B9, C7).
3. **structured pruning**: 층별 민감도 스캔(한 층씩 채널을 줄여 보며 정확도 확인) → 둔감한 층을 많이 자름 → 채널 수를 SIMD/NPU 정렬 배수로 → fine-tune. 파라미터·MAC·**peak activation**이 모두 준다(activation 메모리는 채널 수에 비례하므로 SRAM에 직접 효과).
4. **int8 양자화**(C1·C2) — CMSIS-NN/TFLite Micro 같은 MCU 런타임의 기본 경로.
5. **그래도 flash가 부족하면**: unstructured pruning + 압축 저장(bitmask 등) + 실행 직전 복원. 속도는 이득이 없고 복원 비용이 든다.
6. **distillation**(C5)으로 작은 모델의 정확도를 끌어올린다.
7. **검증**(C8): 원본 대비 클래스별 지표, 최악 입력 지연, 실제 보드 측정.

### 11.3 C — flash에는 압축, 실행은 dense

5단계의 "압축 저장 + 복원"을 행 단위로 하는 최소 예제다. 행 하나를 SRAM 버퍼에 dense로 풀고, 그 다음은 평범한 (SIMD) dense 커널을 그대로 쓴다. 이렇게 하면 flash 크기는 줄고 커널 효율은 유지된다 — 대가는 복원 루프 비용과 버퍼 SRAM이다.

```c
/* bitmask_row.c — flash에는 (bitmask + nonzero 값)만, 실행 직전 SRAM에서 한 행씩 dense로 복원 */
#include <stdint.h>
#include <stdio.h>
#define COLS 16
/* 한 행 16개: 0이 아닌 위치에 비트 1 (LSB = col 0) */
static const uint16_t mask_row = 0x8421;              /* col 0, 5, 10, 15 */
static const int8_t   nz_vals[] = {5, -3, 7, 2};       /* nonzero 값만 순서대로 */

static int decode_row(uint16_t mask, const int8_t *vals, int8_t *out) {
    int k = 0;
    for (int c = 0; c < COLS; c++)
        out[c] = (mask >> c) & 1u ? vals[k++] : 0;     /* 0은 저장 안 했으니 채워 넣음 */
    return k;                                          /* 소비한 값 개수 → 다음 행 시작 */
}
int main(void) {
    int8_t row[COLS], x[COLS];
    for (int c = 0; c < COLS; c++) x[c] = (int8_t)(c + 1);
    int used = decode_row(mask_row, nz_vals, row);
    int32_t acc = 0;
    for (int c = 0; c < COLS; c++) acc += row[c] * x[c]; /* 이후는 평범한 dense 커널 */
    printf("used=%d  row:", used);
    for (int c = 0; c < COLS; c++) printf(" %d", row[c]);
    printf("\ndot=%ld  (5*1 - 3*6 + 7*11 + 2*16 = %d)\n", (long)acc, 5 - 18 + 77 + 32);
    printf("stored bytes: dense %d  vs  mask %zu + vals %zu = %zu\n", COLS,
           sizeof mask_row, sizeof nz_vals, sizeof mask_row + sizeof nz_vals);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 bitmask_row.c -o bitmask_row -lm && ./bitmask_row
```

```text
used=4  row: 5 0 0 0 0 -3 0 0 0 0 7 0 0 0 0 2
dot=96  (5*1 - 3*6 + 7*11 + 2*16 = 96)
stored bytes: dense 16  vs  mask 2 + vals 4 = 6
```

출력에서 볼 것: `0x8421` = 비트 0, 5, 10, 15가 1이라 그 자리에 값이 순서대로 들어갔다. 75% sparse 한 행이 16 B → 6 B. 실제로는 행마다 `used`만큼 값 포인터를 전진시키며 다음 행을 푼다. Cortex-M에서 `(mask >> c) & 1` 루프 대신 `__CLZ`/`RBIT` 같은 비트 스캔 명령으로 nonzero 위치만 뛰어다닐 수도 있다 — SSD 펌웨어의 valid bitmap 스캔과 같은 기법이다.


---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `torch.nn.utils.prune` 후 모델 크기가 줄 거라 기대 | 저장 파일·지연 그대로 | dense 텐서 안의 0일 뿐 | 작은 모델 재구성(structured) 또는 sparse 포맷·압축 |
| fine-tune 중 mask 미적용 | sparsity가 학습하며 점점 줄어듦 | optimizer가 0 weight를 되살림 | 매 step 뒤 mask 곱하기, prune hook 유지 |
| one-shot 곡선만 보고 한계 판정 | "70% 이상 불가" 같은 잘못된 결론 | 재학습 없이 측정 | iterative + FT 곡선으로 판단 |
| 90% sparse = 10배 빠름 가정 | 실측이 dense보다 느림 | SIMD 손실·gather·index 오버헤드 | 교차점 측정, structured/2:4 검토 |
| int8에서 CSR 사용 | 저장 크기가 오히려 증가 | index가 값보다 큼 (67% 문턱) | bitmask·block 포맷, 상대 index |
| 채널 삭제 후 shape 오류 | 다음 층·BN·FC에서 크기 불일치 | 의존성 누락 (flatten, residual) | 의존성 그래프 추적, `shrink(ratio=1.0)` 동등성 테스트 |
| 채널 수를 61, 45 같은 값으로 | MAC은 줄었는데 NPU에서 느려짐 | 벡터·MAC array 정렬 깨짐 | 8/16/32 배수로 반올림 |
| sparse TOPS로 성능 예측 | 실측이 예측의 절반 이하 | dense 모델, 비지원 op, memory-bound | dense TOPS 기준 + roofline + 실측 |
| pruning 후에 양자화 calibration 재사용 | 양자화 후 정확도 급락 | weight·activation 분포가 바뀜 | pruning 끝난 모델로 calibration 다시 |
| dynamic sparsity로 평균 지연 산정 | 특정 입력에서 deadline miss | 0 비율이 입력마다 다름 | 최악 입력 기준 WCET 측정 |

---

## 13. 면접에서 이렇게 말한다

**Q.** "Does 90% unstructured sparsity make inference 10× faster?"

**A.** 보통 아니다. dense 커널은 SIMD·MAC array로 원소당 비용이 매우 싸고, sparse 커널은 index를 읽고 gather를 해야 해서 원소당 비용이 훨씬 비싸다. M2에서 512×512 int8 GEMV를 재 보니 90% sparse CSR이 NEON dense보다 2배 느렸고, 교차점이 약 95%였다. 교차점은 1 − (dense 원소당 비용 / sparse 원소당 비용)으로 예측할 수 있다. 전용 HW 없이 unstructured sparsity가 주는 확실한 이득은 압축 저장(flash·대역폭) 쪽이다.

> No, usually not. A dense kernel runs at SIMD or MAC-array efficiency, while a sparse kernel pays for index loads and gathers on every nonzero. When I measured a 512×512 int8 GEMV on an M2, CSR at 90% sparsity was about 2× slower than the NEON dense kernel, and it only broke even around 95%. The crossover is roughly one minus the ratio of per-element costs. Without hardware support, unstructured sparsity mainly buys you storage and bandwidth through compression, not speed.

**Q.** "What's the difference between structured and unstructured pruning?"

**A.** unstructured는 개별 weight를 지워서 정확도는 가장 잘 지키지만 0이 불규칙하게 흩어져 일반 HW에서 속도 이득이 없다. structured는 채널·head·layer 단위로 지워서 결과가 더 작은 dense 모델이 되므로 어떤 HW·런타임에서도 파라미터·MAC·activation 메모리·지연이 함께 준다. 대신 한 번에 많이 잘리므로 정확도 손실이 크고, BN·다음 층 입력·residual 같은 의존성을 함께 처리해야 하며 fine-tune이 필수다.

> Unstructured pruning removes individual weights, so it preserves accuracy best, but the zeros are irregular and most hardware just multiplies them anyway. Structured pruning removes whole channels, heads, or layers, so what you get is a smaller dense model that runs faster on any backend and also shrinks activation memory. The trade-off is a bigger accuracy hit, the need to handle dependencies like BatchNorm, the next layer's input channels, and residual connections, and a mandatory fine-tuning step.

**Q.** "What is 2:4 sparsity and why does it exist?"

**A.** weight를 연속 4개씩 묶어 각 묶음에서 정확히 2개만 남기는 50% semi-structured 패턴이다. 값 2개와 2-bit 위치 index만 저장하면 되고, 모양이 규칙적이라 HW가 mux로 해당 입력을 골라 MAC을 절반만 하면 된다. NVIDIA Ampere 이후 sparse tensor core가 이를 지원해 행렬곱 처리량이 최대 2배가 된다. 보통 dense 학습 → 2:4 mask → 재학습 순서로 정확도를 회복한다. HW 지원이 없으면 이득이 없고, 소프트웨어로 구현하면 dense SIMD보다 느리다.

> 2:4 sparsity means that in every group of four consecutive weights, exactly two are zero. You store the two values plus a 2-bit index for each, and because the pattern is regular, hardware can select the matching activations with a mux and do half the MACs without stalling. NVIDIA's sparse tensor cores since Ampere support it for up to 2× matmul throughput. The usual recipe is to train dense, apply the 2:4 mask, and fine-tune. Without hardware support it gives no speedup; a software 2:4 kernel on a CPU was slower than the dense SIMD kernel in my test.

**Q.** "How would you shrink a CNN to fit on an MCU?"

**A.** 먼저 flash·SRAM·지연·에너지 예산을 숫자로 정한다. 구조적 선택(depthwise separable, 폭 multiplier, 입력 해상도)을 먼저 보고, 층별 민감도 스캔 후 structured 채널 pruning으로 둔감한 층을 줄이고 채널 수는 SIMD 폭 배수로 맞춘 뒤 fine-tune한다. 그 다음 int8 양자화, 필요하면 distillation으로 정확도를 되찾는다. flash가 여전히 부족할 때만 unstructured + 압축 저장을 쓴다. 마지막으로 원본 대비 클래스별 지표와 보드 실측 지연·peak SRAM으로 검증한다.

> I'd start from explicit budgets for flash, SRAM, latency, and energy. First I'd look at architecture choices like depthwise-separable convs, width multipliers, and input resolution. Then I'd run a per-layer sensitivity scan, apply structured channel pruning where layers are least sensitive, round channel counts to the SIMD width, and fine-tune. After that comes int8 quantization, and distillation if I need to recover accuracy. Unstructured pruning with compressed storage is a last resort for flash only. Finally I'd validate per-class metrics against the original and measure latency and peak SRAM on the board.

**Q.** "A vendor quotes 2× TOPS with sparsity. How do you evaluate that?"

**A.** 그 숫자는 보통 2:4 같은 특정 패턴의 sparse 행렬곱을 0까지 센 상한이다. 우리 모델이 그 패턴으로 재학습돼 있는지, 어떤 정밀도와 op에 적용되는지, 모델 중 행렬곱 비중과 memory-bound 층 비중이 얼마인지를 확인하고, 결국 dense 버전과 sparse 버전을 벤더 툴체인으로 컴파일해서 end-to-end 지연과 전력을 직접 잰다.

> That number usually counts a specific sparse pattern, typically 2:4, at peak. I'd check whether our model can be retrained to that pattern, which precisions and ops the speedup applies to, and how much of our runtime is compute-bound matmul versus memory-bound layers. Then I'd compile both a dense and a sparse version with the vendor toolchain and measure end-to-end latency and power myself.

**Q.** "Why does magnitude pruning work poorly for LLMs, and what does Wanda do differently?"

**A.** LLM activation에는 특정 채널만 수십 배 큰 outlier가 있어서, 작은 weight라도 큰 입력과 곱해지면 중요하다. magnitude는 입력을 보지 않는다. Wanda는 중요도를 |W| × 입력 채널의 L2 norm으로 정의하고 출력 행마다 하위를 지운다. 재학습이나 weight 보정 없이 calibration 데이터만 있으면 된다. 제가 outlier 채널 8개를 넣은 256×256 층에서 실험했을 때 같은 50% pruning에서 출력 오차가 0.27에서 0.077로 줄었다.

> LLM activations have outlier channels that are tens of times larger than the rest, so a small weight can still matter a lot if its input is large, and magnitude pruning ignores the input. Wanda scores each weight by its magnitude times the L2 norm of its input channel over calibration data, and prunes the lowest scores per output row. It needs no retraining or weight updates. In a small experiment with eight outlier channels, it cut the output error at 50% sparsity from 0.27 to 0.077 compared to magnitude pruning.

---

## 14. 직접 해보기

1. (손계산) 1,024×1,024 fp16 행렬, 열 index는 uint16, row_ptr는 uint32다. CSR이 dense보다 작아지는 최소 sparsity는? 정답: 값 2 B + index 2 B = 4 B/nnz vs dense 2 B/원소 → d < 1/2, 약 50% 초과 (row_ptr 4 KB는 무시할 만함).
2. (손계산) 채널 수가 [32, 64, 128]인 3층 3×3 conv(입력 1채널, 해상도 유지)에서 모든 층 채널을 25% 줄이면 MAC은 몇 배가 되나? 정답: 첫 층 0.75배, 나머지 0.75² ≈ 0.56배 → 전체는 0.56배에 가깝다 (중간 층이 MAC 대부분).
3. (코드) `c4cnn.shrink(big, 1.0)`의 출력이 `big`과 완전히 같은지 `(small(X) - big(X)).abs().max()`로 확인하라. 그다음 ratio 0.9, 0.75에서 FT 전 정확도를 재 보라. 정답: 최대 차이 0.0이어야 한다. 이 노트의 모델에서는 FT 전 정확도가 ratio 0.9에서 약 0.41, 0.75에서 약 0.28로 빠르게 떨어진다 (스레드 수에 따라 끝자리는 다를 수 있다).
4. (코드) `spmv.c`의 CSR 커널에 **4×1 block**(연속 4열 단위 nonzero 블록) 버전을 추가하고, 같은 sparsity에서 dense 대비 교차점이 얼마나 내려오는지 재라. 힌트: 블록 안은 연속 load라 컴파일러가 벡터화할 여지가 생긴다. 정답은 측정값 — 교차점이 CSR보다 낮아지는지 보는 것이 목적.
5. (코드) `ex7`의 `nm_mask`를 1:4, 4:8로 바꿔 FT 후 정확도를 비교하라. 힌트: 4:8은 2:4와 sparsity가 같지만 자유도가 크다(8개 중 아무 4개). 정답: 이 노트 환경에서 실행하면 FT 후 2:4 0.9759, 4:8 0.9778, 1:4(75%) 0.9722로 digits가 쉬워서 차이가 잡음 수준이다. 제약이 느슨할수록(4:8) 유리하고 sparsity가 높을수록(1:4) 불리한 경향은 더 어려운 데이터에서 뚜렷해진다.
6. (설계) 어떤 NPU 데이터시트가 "8 TOPS (INT8, with 2:4 sparsity)"라고 적었다. 모델이 dense int8이고 실행 시간의 40%가 memory-bound인 depthwise 층이라면 기대할 수 있는 compute 상한은? 정답: dense 기준 4 TOPS로 읽고, 나머지 40%는 TOPS가 아니라 대역폭으로 제한된다 — 실측 전에는 4 TOPS보다 한참 낮게 예상.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| pruning | 가지치기 | 중요하지 않은 weight·채널·head·layer를 0으로 만들거나 제거 |
| sparsity / density | 희소도 / 밀도 | 0인 원소 비율 / 0이 아닌 원소 비율 (합이 1) |
| magnitude pruning | 크기 기준 가지치기 | 절댓값이 작은 weight부터 제거 |
| iterative pruning | 점진적 가지치기 | sparsity를 조금씩 올리며 매번 fine-tune |
| fine-tune | 재학습 | pruning 후 남은 weight를 다시 학습해 정확도 회복 |
| unstructured | 비정형 | 개별 weight 단위. 0 위치가 불규칙 |
| structured | 정형 | 채널·filter·head·layer 단위. 결과가 작은 dense 모델 |
| N:M / 2:4 | semi-structured | 연속 M개 중 정확히 N개만 남김 |
| block sparsity | 블록 희소 | 블록 단위로 0/비0. 블록 안은 dense |
| CSR | Compressed Sparse Row | values + col_idx + row_ptr 세 배열 |
| bitmask | 비트맵 | 원소당 1 bit로 0 여부 표시 + nonzero 값 |
| gather | 모아 읽기 | 흩어진 주소(`x[col[k]]`)에서 값을 읽기. SIMD에 불리 |
| activation sparsity | 활성값 희소 | ReLU 등이 만든 입력 쪽 0. 실행 중 결정 (dynamic) |
| zero-skipping | 0 건너뛰기 | 0 입력의 MAC을 gating하거나 생략하는 HW 기법 |
| Wanda | Pruning by Weights and activations | 중요도 = 절댓값(W) × 입력 채널 L2 norm, 행별 제거 |
| Deep Compression | Han et al. 2016 | pruning + 양자화 + Huffman 3단 압축 |
| sparse TOPS | 희소 기준 성능 | 2:4 등의 0까지 센 TOPS. 보통 dense의 2배로 표기 |

---

## 16. 요약 & 체크리스트

pruning은 weight를 0으로 만드는 것이고, 학습된 모델은 재학습(fine-tune)만 붙이면 놀랄 만큼 많이(이 노트의 MLP는 iterative로 90%, 98%에서도 0.92) 잘라도 버틴다. 하지만 edge 엔지니어의 질문은 "그래서 빨라지나"이고, 답은 0의 **모양**과 **HW 지원**에 달려 있다. unstructured 0은 dense SIMD·NPU가 그냥 곱해 버려서 속도 이득이 거의 없고(M2 실측 교차점 약 95%), int8에서는 CSR index가 값보다 커서 저장 이득도 67% sparsity 이상에서만 난다 — 확실한 이득은 bitmask·압축 저장을 통한 flash·대역폭이다. structured(채널·head·layer) pruning은 작은 dense 모델을 만들어 어떤 HW에서도 MAC·메모리·지연을 줄이지만(이 노트의 CNN: MAC 3.98배↓, 지연 2.9배↓) 의존성 처리와 fine-tune이 필수이고, scratch 학습과 꼭 비교해야 한다. 2:4는 전용 HW(예: NVIDIA sparse tensor core)가 있을 때만 행렬곱 최대 2배이고, 데이터시트의 sparse TOPS는 이 조건의 상한이다. LLM에서는 입력 크기를 함께 보는 Wanda·SparseGPT가 magnitude보다 낫다. 순서는 structured → (HW 지원 sparsity) → 양자화 → 압축 저장, 필요하면 distillation.

- [ ] magnitude pruning의 임계값과 mask를 2×4 행렬에서 손으로 구할 수 있다
- [ ] global과 layer-wise pruning의 차이와 global이 보통 나은 이유를 설명할 수 있다
- [ ] `torch.nn.utils.prune`이 `weight_orig`·`weight_mask`로 동작하며 모델 크기를 줄이지 않는다는 것을 안다
- [ ] 4×6 행렬의 CSR 세 배열을 손으로 만들 수 있다
- [ ] int8·fp32에서 CSR과 bitmask의 손익분기 sparsity를 유도할 수 있다
- [ ] "90% sparse ≠ 10배 빠름"을 SIMD·gather·index 오버헤드로 설명하고 교차점 공식을 쓸 수 있다
- [ ] 채널 pruning 시 BN·다음 층 입력·flatten·residual 의존성을 처리할 수 있다
- [ ] 채널 50% pruning이 중간 층 MAC을 1/4로 줄이는 이유를 계산할 수 있다
- [ ] 2:4 포맷의 저장 크기와 HW 지원 조건을 설명할 수 있다
- [ ] Wanda 점수 `|W|·‖X_j‖`를 설명하고 magnitude보다 나은 경우를 예로 들 수 있다
- [ ] sparse TOPS 표기를 dense 기준으로 환산하고 확인 질문을 던질 수 있다

---

## 참고 자료

- Song Han, Jeff Pool, John Tran, William J. Dally, "Learning both Weights and Connections for Efficient Neural Networks", NeurIPS 2015 — [arXiv:1506.02626](https://arxiv.org/abs/1506.02626)
- Song Han, Huizi Mao, William J. Dally, "Deep Compression", ICLR 2016 — [arXiv:1510.00149](https://arxiv.org/abs/1510.00149)
- Michael Zhu, Suyog Gupta, "To prune, or not to prune", 2017 — [arXiv:1710.01878](https://arxiv.org/abs/1710.01878)
- Hao Li et al., "Pruning Filters for Efficient ConvNets", ICLR 2017 — [arXiv:1608.08710](https://arxiv.org/abs/1608.08710)
- Zhuang Liu et al., "Learning Efficient Convolutional Networks through Network Slimming", ICCV 2017 — [arXiv:1708.06519](https://arxiv.org/abs/1708.06519)
- Zhuang Liu et al., "Rethinking the Value of Network Pruning", ICLR 2019 — [arXiv:1810.05270](https://arxiv.org/abs/1810.05270)
- Jonathan Frankle, Michael Carbin, "The Lottery Ticket Hypothesis", ICLR 2019 — [arXiv:1803.03635](https://arxiv.org/abs/1803.03635)
- Paul Michel, Omer Levy, Graham Neubig, "Are Sixteen Heads Really Better than One?", NeurIPS 2019 — [arXiv:1905.10650](https://arxiv.org/abs/1905.10650)
- Asit Mishra et al., "Accelerating Sparse Deep Neural Networks" (NVIDIA 2:4), 2021 — [arXiv:2104.08378](https://arxiv.org/abs/2104.08378)
- Elias Frantar, Dan Alistarh, "SparseGPT", ICML 2023 — [arXiv:2301.00774](https://arxiv.org/abs/2301.00774)
- Mingjie Sun et al., "A Simple and Effective Pruning Approach for Large Language Models" (Wanda), ICLR 2024 — [arXiv:2306.11695](https://arxiv.org/abs/2306.11695)
- Gongfan Fang et al., "DepGraph: Towards Any Structural Pruning" (Torch-Pruning), CVPR 2023 — [arXiv:2301.12900](https://arxiv.org/abs/2301.12900)
- PyTorch Pruning Tutorial — [pytorch.org/tutorials/intermediate/pruning_tutorial.html](https://pytorch.org/tutorials/intermediate/pruning_tutorial.html)
- MIT 6.5940 TinyML and Efficient Deep Learning Computing (Song Han) — Pruning & Sparsity 강의 — [efficientml.ai](https://efficientml.ai)
- 이 노트 세트: A1(GEMV·norm), A3(학습 루프), B2(conv MAC·BN), B4(attention head), C1(양자화 scale), C2(PTQ/QAT), C3(LLM 양자화·outlier), C5(distillation), C6(채널 정렬), D3(roofline), D5(LLM decode), M4(데이터시트 해석)
