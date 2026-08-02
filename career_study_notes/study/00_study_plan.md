# Study Plan — 주제 트리 (틀)

> 직무([`../roles/`](../roles/))가 요구하는 지식을 **갭 기준**으로 공부하는 마스터 인덱스.
> 각 주제는 나중에 `study/<주제>.md`로 분리해 정리(개념 + 연습문제 + 참고링크).
> 상태: ⬜ 미시작 · 🟡 진행중 · ✅ 정리완료
> _생성 2026-07-12 — 채워나갈 예정_

---

## 0. 기준
- 강점(이미 있음): PCIe·I2C·SPI·SPMI·RFFE 브링업, SSD/임베디드 FW, 실리콘-시스템 통합, 수학
- 목표 갭: 아래 트랙별로 채우기. **연습문제·개념정리·직접 구현**을 각 노트에 축적.

---

## 트랙 1 — 인터페이스 / Post-Si Validation 심화 (내 코어 강화) ⭐⭐⭐
- [ ] ⬜ PCIe Gen5/Gen6 심화 (LTSSM, equalization, lane margining)
- [ ] ⬜ SerDes / 신호무결성(SI) 기초 (eye, jitter, BER, channel)
- [ ] ⬜ NVLink / C2C / CXL 개요 (NVIDIA·가속기 인터커넥트)
- [ ] ⬜ Post-silicon 방법론 (bring-up 순서, ATE, 랩장비 DSO/BERT/analyzer)
- [ ] ⬜ DDR/HBM 메모리 인터페이스 기초 (가속기 대역폭)

## 트랙 2 — 가속기 아키텍처 / SoC 통합 ⭐⭐⭐
- [ ] ⬜ AI 가속기 아키텍처 (systolic array, TPU, GPU SM, dataflow)
- [ ] ⬜ SoC 통합: 버스/NoC, AXI/AMBA, 클럭·파워 도메인, IP integration
- [ ] ⬜ Roofline / 메모리-대역폭 병목 / arithmetic intensity
- [ ] ⬜ (선택) RTL 리터러시: SystemVerilog 입문, 간단 블록 설계·시뮬

## 트랙 3 — ML / Inference 내부 (co-design·on-device 진입) ⭐⭐
- [ ] ⬜ 선형대수/최적화 복습 (수학 자산 재활성화 → 빠르게)
- [ ] ⬜ Transformer 구조 & inference 흐름 (attention, KV cache)
- [ ] ⬜ 양자화(INT8/INT4)·프루닝·distillation
- [ ] ⬜ On-device 런타임: LiteRT/NPU, 컴파일러 파이프라인
- [ ] ⬜ PyTorch 기본 (모델 로드·추론·프로파일)

## 트랙 4 — 시스템 SW / 인프라 (Anthropic·infra role 진입) ⭐⭐
- [ ] ⬜ C/C++ 심화 (모던 C++, 빌드시스템 Bazel/CMake)
- [ ] ⬜ 리눅스 시스템 프로그래밍 (프로세스, 메모리, PCIe 디바이스 드라이버 개념)
- [ ] ⬜ 가속기 런타임/드라이버 스택 (CUDA 개요, 서빙 인프라)

## 트랙 5 — 로보틱스 / Physical AI (OpenAI·NVIDIA robotics) ⭐⭐
- [ ] ⬜ 실시간 제어 기초 (control loop, RTOS, 모터드라이브)
- [ ] ⬜ 센서 I/O·융합, 안전(safety)
- [ ] ⬜ ROS2 개요, Isaac/Jetson 스택

## 트랙 6 — 코딩 (인터뷰 공통) ⭐⭐⭐
- [ ] ⬜ C/자료구조·알고리즘 (기존 `lecture_notes` 코딩탭 연계)
- [ ] ⬜ 임베디드/시스템 스타일 문제 (비트조작, 링버퍼, 메모리)
- → 연습은 루트 `lecture_notes` 웹앱의 코딩 탭 활용 가능

---

## 우선순위 제안 (Don 확정 전 초안)
1. **트랙 1 + 트랙 6** 먼저 — 즉시 인터뷰 경쟁력(내 코어). 
2. **트랙 2** 병행 — SoC/가속기 통합으로 스토리 확장.
3. 목표 직무 shortlist에 따라 **트랙 3/4/5 중 1개** 선택 집중.

> 다음: 위 항목 중 하나를 골라 "이거 연습문제/정리 만들어줘" 하면 `study/<주제>.md`로 채움.
