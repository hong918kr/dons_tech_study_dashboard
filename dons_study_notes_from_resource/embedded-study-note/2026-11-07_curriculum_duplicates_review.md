# 커리큘럼 중복 검토 (2026-11-07)

> **2차 검사 (68트랙 / 2,121개)**: 같은 날 16개 트랙(AISYS·DCN·HWLAB·LPERF·MEDIA·NET·NETHW·NUM·QFIN·QPROB·RISCV·RTLDV·RUST·SOCX·VIRT·WCONN, 531개)이 추가돼 다시 돌렸다.
> - **방법**: 1차와 같은 TF-IDF(전체 2,121개, 점수 ≥ 0.26 → 367쌍 중 신규 트랙이 낀 88쌍)와 주제 키워드 그룹(perf·RDMA·virtio·CDC·AXI·NoC·PAM4·코덱·ALSA·칼만·옵션 등 130여 개)을 다시 만들고, 신규 트랙 전체 제목을 관련 기존 트랙과 나란히 놓고 읽었다.
> - **결과**: 신규 트랙은 대부분 `see:` 태그로 기존 트랙을 참조하고 차이점만 다뤄서 **확실한 중복(A)은 새로 나오지 않았다.** 남은 겹침은 "기초 함수를 한 번 더 구현"하는 유형이라 **부분 중복(B) 8건**(🆕 표시)을 추가했다. 같은 스킬을 다른 도메인에서 쓰는 쌍 18줄은 **유지(C)**에 넣었다.
> - **1차 권장안 수정 3건**(✏️ 표시): 1차에서 권장한 교체 주제가 신규·기존 아이템과 다시 겹치게 된 경우다(A1, A4, A18).
> - 1차에 인용한 ID 160여 개는 모두 그대로 존재하고 제목도 바뀌지 않았다.

전체 1,590개 아이템을 두 가지 방법으로 훑었다(1차).

1. **문맥 유사도**: 제목·목표·과제·태그를 TF-IDF로 비교한 상위 144쌍 (점수 ≥ 0.30). 기존 `review_items.py --dups`가 잡지 못한 쌍까지 나온다.
2. **주제 키워드 그룹**: 링버퍼·CRC·워치독·OTA·EKF·쿼터니언·DMA 등 90여 개 주제로 묶은 뒤, 2개 이상 트랙에 걸친 그룹을 하나씩 읽었다.

판단 기준은 Don이 정한 대로다. **스킬은 비슷해도 도메인 조합이 다르면 중복이 아니다.** 같은 스킬을 같은 도메인에서 거의 같은 과제로 두 번 하는 경우만 중복으로 본다.

| 분류 | 개수 | 뜻 | 권장 처리 |
|---|---|---|---|
| A. 확실한 중복 | 23건 (2차 추가 0) | 같은 스킬 + 같은 도메인 + 거의 같은 과제 | 한쪽만 남기고, 다른 쪽은 삭제하거나 다른 주제로 바꾼다 |
| B. 부분 중복 | 28건 (2차 추가 🆕 8) | 핵심 코드 일부를 두 번 만든다 | 둘 다 두되, 뒤 아이템은 앞 아이템 결과물을 재사용하고 차이점만 다룬다 |
| C. 비슷하지만 유지 | 38줄 (2차 추가 🆕 18) | 스킬은 같고 도메인 조합이 다르다 | 그대로 둔다 |

## 한눈에 보이는 패턴

- **SYSD(시스템 설계) 트랙이 구현을 다시 한다.** OTA·A/B 부트·HIL 팜·차량 Uptane이 PLAT/INFRA/AUTO에서 이미 구현되는데, SYSD에서 또 구현한다. SYSD는 "설계 면접 형식(요구사항·추정·구조·트레이드오프 문서 + 핵심 경로 의사코드)"으로만 두고 구현은 소유 트랙을 참조하게 하면 중복 6건이 한 번에 정리된다.
- **CAP(종합 프로젝트)가 트랙 종합 아이템과 겹친다.** CAP-A02 = NPU-A12, CAP-A03 = DRONE-A05. CAP은 "이미 만든 것을 포트폴리오로 묶기(README·데모·테스트 자동화·발표)"로 바꾸면 된다.
- **SIM 트랙이 ARCH 모델을 다시 만든다.** 버스 중재(SIM-M05), 5단 파이프라인(SIM-M01), 모터 파라미터 식별(SIM-A03). SIM은 "DES 엔진·디지털 트윈 검증" 쪽으로 옮기면 겹치지 않는다.
- 🆕 **신규 트랙의 겹침은 "기초 함수 재구현" 한 가지 유형이다.** RFC 1624 증분 체크섬(NET-B02·NETHW-B07), LPM 이진 트라이(DS-M13·NET-B04), GBN/SR 상태기계(COMM-A02·DCN-A07), virtio EVENT_IDX(VIRT-M07·NETHW-M11), GSO/TSO 분할(NET-A03·NETHW-M01), PDM 데시메이션 체인(DSP-M08·MEDIA-M11)이 두 번씩 구현된다. 함수 하나를 소유 아이템에 두고 다른 쪽은 호출만 하게 `task` 문장을 고치면 된다.
- 🆕 **RTLDV·RISCV·RUST는 기존 C 모델 문제를 다른 매체로 다시 푼다.** ARCH-B05~B07·A07의 논리 회로 C 모델을 RTLDV가 RTL로, ARM 문제를 RISCV가 다른 ISA로, C/C++ 드라이버 패턴을 RUST가 소유권 모델로 다시 푼다. 매체(RTL·ISA·언어)가 다르면 면접에서 묻는 내용도 다르므로 유지(C)로 분류했다. 다만 RTLDV 테스트벤치가 ARCH C 모델을 golden 참조로 쓰게 하면 두 아이템이 자연스럽게 이어진다.
- **이미 쓴 레슨과 겹치는 아이템이 2개 있다.** CPP-B13 레슨이 `Result<T,E>` + `[[nodiscard]]` + `and_then`을 이미 구현했다 → CPP-M11과 겹친다. C-B11 레슨이 `size`로 섹션 크기를 이미 쟀다 → TOOL-B06과 겹친다.

## A. 확실한 중복 (23건)

| # | 아이템 | 겹치는 내용 | 권장 |
|---|---|---|---|
| A1 | ARCH-A10 ↔ SIM-M05 | CPU·DMA·NPU 3마스터 버스 중재(고정/RR/가중 RR) 지연 비교. 과제 문장까지 거의 같다 | ARCH-A10 유지. ✏️ SIM-M05는 삭제하거나 인터커넥트가 아닌 주제로 교체(2차: 1차 권장안이던 credit 흐름제어·QoS는 이제 SOCX-M04·SOCX-M03이 다룬다) |
| A2 | AERO-B01 ↔ TEST-M12 | 주석의 `@req` 태그를 스캔해 요구사항↔코드↔테스트 추적 매트릭스와 미추적 리포트를 만드는 도구 | TEST-M12가 도구 소유. AERO-B01은 HLR→LLR 분해와 DO-178C 리뷰 체크리스트로 교체하거나 AERO-A01에 흡수 |
| A3 | AUTON-M01 ↔ RCTL-M05 | 2D 로봇 EKF: 운동 모델 + 랜드마크 range-bearing, 야코비안, 각도 wrap | AUTON-M01 유지(NEES 포함). RCTL-M05는 모터 부하 토크 관측기 같은 제어 쪽 EKF로 교체 |
| A4 | ARCH-M03 ↔ LLAT-B07 | 정렬/랜덤 데이터 분기 예측 측정 + CSEL branchless 비교 | ARCH-M03 유지. ✏️ LLAT-B07은 `[[likely]]`·PGO·hot/cold 함수 분할(`-freorder-blocks-and-partition`) 같은 코드 배치로 교체(2차: "cold path"라는 말은 LLAT-B04 프리폴트·캐시 워밍과 헷갈리므로 코드 배치로 한정) |
| A5 | ARM-A05 ↔ EML-M11 | M2에서 int8 GEMM 마이크로커널 SDOT → SMMLA, GOPS 측정 | EML-M11 유지(가중치 패킹 포함). ARM-A05는 SDOT/SMMLA 레인 의미를 어셈블리로 직접 확인하는 작은 과제로 축소 |
| A6 | ARCH-A11 ↔ AIPERF-B06 | M2 CPU roofline을 그리고 벡터 덧셈·행렬곱·conv 측정점을 찍어 병목 판정 | AIPERF-B06 유지. ARCH-A11은 Amdahl/Gustafson 병렬 확장성 측정만 |
| A7 | ARCH-M01·M02 ↔ SIM-M01 | 5단 파이프라인 + 포워딩 + 정적/2비트 분기 예측 + CPI 분해 | ARCH 유지. SIM-M01은 삭제하거나 trace 기반 CPI 스택 모델(명령 실행 없음)로 교체 |
| A8 | PLAT-M07 ↔ SYSD-M01 | A/B 슬롯 메타데이터, trial boot, 부트 카운터, 단계별 전원 차단 주입 테스트 | PLAT-M07이 구현 소유. SYSD-M01은 설계 문서형으로 |
| A9 | AUTO-A05 ↔ SYSD-A01 | 차량 OTA Uptane 메타데이터 검증 + ECU A/B | AUTO-A05가 구현 소유. SYSD-A01은 다중 ECU 오케스트레이션 설계만 |
| A10 | INFRA-M06 ↔ SYSD-M02 | OTA 서버·클라이언트: 매니페스트, 재개 다운로드, 해시/서명 검증 | INFRA-M06이 구현 소유. SYSD-M02는 단계적 롤아웃·델타 정책 설계만 |
| A11 | INFRA-M03 ↔ SYSD-A02 | HIL 테스트 팜: 보드 예약·임대·작업 스케줄러 구현 | INFRA-M03이 구현 소유. SYSD-A02는 설계만 |
| A12 | RCTL-M11 ↔ SIM-A03 | 모터 R·L(Ke)·Kt·J를 최소제곱으로 식별, 참값 대비 오차 | RCTL-M11 유지. SIM-A03은 트윈 잔차 감시·드리프트 감지(디지털 트윈 검증)로 교체 |
| A13 | DRONE-B05 ↔ SENS-M04 | 상보 필터 roll/pitch, α 튜닝, 진값 대비 RMS 오차 | SENS-M04 유지. DRONE-B05는 진동·가속 외란 시 가속도계 신뢰도 게이팅으로 교체하거나 삭제 |
| A14 | CAP-A03 ↔ DRONE-A05 | 6-DOF 쿼드 SITL + 추정기 + 캐스케이드 PID 통합 | CAP-A03을 DRONE-A05 결과물의 포트폴리오화(문서·데모·회귀 테스트)로 |
| A15 | CAP-A02 ↔ NPU-A12 | EML int8 CNN을 toy NPU 스택으로 end-to-end 실행, golden 비교 | 위와 같은 처리 |
| A16 | CAP-M04 ↔ SYSD-A07 | 45분 모의 설계 면접 녹화 + 루브릭 자기평가 | 하나로 합치기(SYSD-A07 유지) |
| A17 | C-M12 ↔ TOOL-B12 ↔ TEST-B08 | signed overflow·shift·정렬·NULL 같은 UB 목록을 sanitizer로 잡기. 3번 반복된다 | C-M12 = UB 의미와 최적화 결과. TOOL-B12 = UBSan 옵션·trap 모드·비용. TEST-B08 = 기존 모듈 테스트에 sanitizer 빌드 타깃을 CI로 붙이기(새 버그 예제 없이) |
| A18 | C-A09 ↔ DSP-B02 | Q15 곱셈의 반올림·포화·-1×-1 코너 | DSP-B02 유지. ✏️ C-A09는 삭제(2차: 1차 권장안이던 `__builtin_*_overflow`·포화 덧셈은 DSP-B03이 이미 다루고, 정수 오버플로 버그는 SEC-B03이 다룬다) |
| A19 | C-M02 ↔ BM-B04 | W1C 상태 레지스터를 `\|=`로 지우다 다른 플래그까지 지우는 버그 재현 | C-M02 = W1C/RO/RW 모델 소유. BM-B04는 BSRR 원자 set/reset과 ISR–메인 ODR 레이스만 |
| A20 | ARM-A11 ↔ TOOL-A10 | `-Rpass=loop-vectorize` 리포트 읽기, aliasing 때문에 벡터화 실패 → `restrict`로 해결 | 하나로 합치기(TOOL-A10 유지, 인라이닝 리포트까지 포함) |
| A21 | AERO-A04 ↔ DIST-A02 | 4노드 OM(1) oral messages로 비잔틴 노드 1개 견디기 | DIST-A02가 OM(1) 소유. AERO-A04는 4채널 프레임 동기·교차 채널 교환(OM(1) 재사용)만 |
| A22 | MEM-B09 ↔ SIV-B07 | March C- 구현 + stuck-at/transition/coupling 결함 주입 | MEM-B09 유지. SIV-B07은 MBIST 컨트롤러 레지스터 구동과 실패 주소 비트맵 리포트로 교체 |
| A23 | CPP-M11 ↔ CPP-B13 (작성된 레슨) | `[[nodiscard]] Result<T,E>`, 에러 전파 체인. CPP-B13 레슨 Part A에서 이미 구현했다 | CPP-M11을 에러 카테고리 설계 + `std::expected` 코드 크기(thumbv7em) 비교로 교체 |

## B. 부분 중복 (28건) — 둘 다 두고 경계만 조정

| # | 아이템 | 두 번 만드는 부분 | 경계 조정 |
|---|---|---|---|
| B1 | AUTO-A01 ↔ TEST-M07 | HARA 표 + S/E/C → ASIL 결정 함수 | AUTO-A01은 TEST-M07 함수를 재사용하고 ASIL 분해·독립성 논증만 |
| B2 | SIV-M11 ↔ TOOL-M08 | `git bisect run` 스크립트 | SIV-M11은 전압·주파수·버퍼 크기 같은 파라미터 경계 bisect만 |
| B3 | ARCH-M11 ↔ SIV-M02 | 디스크립터 체인 DMA 엔진 모델 | SIV-M02는 ARCH-M11 모델을 검증 대상으로 쓰고 랜덤·경계 자극 생성과 무결성 검사만 |
| B4 | EML-B09 ↔ NPU-M07 | requant (int32 multiplier, shift) 분해 | NPU-M07은 EML-B09 함수를 재사용하고 하드웨어 파이프라인 비트폭·LUT 활성화만 |
| B5 | DRONE-B02 ↔ GFX-B04 | 쿼터니언 곱·회전·행렬 변환 라이브러리 | GFX-B04는 slerp·보간·애니메이션만(기본 연산은 DRONE-B02 재사용) |
| B6 | COMM-A10 ↔ SIV-A07 | 로직 애널라이저 CSV → I2C 디코더 | SIV-A07은 COMM-A10 디코더를 재사용하고 setup/hold·stretching 위반 검출만 |
| B7 | DS-A09 ↔ MEM-M10 | 페이지 매핑 L2P + valid bitmap | DS-A09는 GC victim 선정 자료구조(indexed heap)만 |
| B8 | DS-M06 ↔ RTOS-M07 | 해시 타이머 휠 구현 | RTOS-M07은 DS-M06 휠을 재사용하고 정렬 리스트와 tick 비용 비교만 |
| B9 | RTOS-A03 ↔ TEST-A04 | 제어 루프 실행시간을 수만 회 측정한 분포 | TEST-A04는 최악 경로 입력 구성과 측정 기반 WCET의 한계만 |
| B10 | ARCH-A01 ↔ OS-M01 | 다단계 페이지 테이블 + 권한 비트 | OS-M01은 ARCH-A01 워커를 재사용하고 map/unmap/protect와 TLB 무효화만 |
| B11 | C-B11 (작성된 레슨) ↔ TOOL-B06 | `size`로 text/data/bss 크기 측정 | TOOL-B06은 링커 맵 파일·`arm-none-eabi-size -A`·심볼별 크기 분석으로 |
| B12 | C-A03 ↔ OS-M08 | release/acquire 데이터 게시 + relaxed 오용 버그 | OS-M08은 DCLP·`call_once`만 |
| B13 | BM-A07 ↔ TOOL-A08 | 스택 페인팅 측정 vs `-fstack-usage`를 양쪽에서 비교 | BM-A07 = 런타임 페인팅. TOOL-A08 = 정적 콜 그래프 분석(측정 비교는 BM-A07 참조) |
| B14 | BM-A09 ↔ TOOL-A05 | 크래시 덤프 구조(레지스터·스택·CRC) 정의 | TOOL-A05가 포맷과 호스트 디코더 소유. BM-A09는 .noinit 저장·리셋 후 보고 흐름만 |
| B15 | BM-A04 ↔ SEC-M02 | 이미지 헤더 경계·잘림 검사 | BM-A04 = 무결성(CRC/SHA). SEC-M02 = 서명·TOCTOU. 공통 헤더 포맷은 BM-A04를 재사용 |
| B16 | AERO-B02 ↔ TEST-B06 | MC/DC 최소 테스트 셋 | TEST-B06 = line/branch(llvm-cov). MC/DC는 AERO-B02에 몰기 |
| B17 | ARCH-M06 ↔ MEM-M03 | write-back/through 캐시 시뮬레이터 | MEM-M03은 ARCH 시뮬레이터를 확장(재사용)해 inclusive/exclusive·3C만 |
| B18 | AERO-B07 ↔ TEST-A01 ↔ DIST-A02 | TMR 다수결·중간값 선택 voter | voter는 AERO-B07 소유. TEST-A01 = 1oo2/2oo3 신뢰도·가용도 계산. DIST-A02 = OM(1)만 |
| B19 | BM-M09 ↔ SENS-A04 | ADC → DMA 하프/풀 콜백 + overrun 검출 | SENS-A04는 BM-M09 파이프라인을 재사용하고 시퀀스 카운터 무손실 검증·버퍼 크기 산정만 |
| B20 | ARCH-B14 ↔ PLAT-A06 | 리셋 원인 레지스터 디코더 | PLAT-A06은 ARCH-B14 디코더를 재사용하고 부트 루프 감지·safe mode 정책만 |
| 🆕 B21 | NET-B02 ↔ NETHW-B07 | RFC 1624 증분 체크섬 갱신(NAT 주소/포트 변경)을 구현하고, 무작위 패킷에서 전체 재계산 결과와 대조 | NET-B02가 증분 갱신 함수 소유. NETHW-B07은 그 함수를 호출하고 CHECKSUM_PARTIAL/COMPLETE 계약만 |
| 🆕 B22 | NET-A03 ↔ NETHW-M01 | 큰 버퍼를 세그먼트로 자르는 함수(헤더 복제, seq·IP ID 진행, 체크섬 재계산) | NETHW-M01이 TSO 분할 엔진 소유. NET-A03은 ethtool on/off 측정과 슈퍼패킷 관찰만 하고 분할은 NETHW-M01 엔진 호출 |
| 🆕 B23 | DS-M13 ↔ NET-B04 | IPv4 LPM 이진 트라이 + 선형 탐색 참조 대조 | NET-B04는 DS-M13 트라이를 재사용하고 IPv4 입력 검증·포워딩·TTL만(NETHW-M04는 이미 DS-M13을 출발점으로 명시) |
| 🆕 B24 | VIRT-M07 ↔ NETHW-M11 | virtio EVENT_IDX(`vring_need_event`) 알림 억제 구현과 kick/exit 횟수 측정 | VIRT-M07이 EVENT_IDX 소유. NETHW-M11은 packed ring·`virtio_net_hdr`·mergeable 버퍼만 |
| 🆕 B25 | COMM-A02 ↔ DCN-A07 | GBN·Selective Repeat 송수신 상태기계 + 손실률별 처리량 곡선 | DCN-A07은 COMM-A02 상태기계를 재사용하고 SACK·패킷 트리밍·10⁻⁶ 손실·BDP 규모만 |
| 🆕 B26 | GPUSYS-A02 ↔ DCN-M12 | ring vs tree all-reduce의 α-β 비교 | GPUSYS-A02 = ring 구현과 2(N−1)/N·S 실측만. 알고리즘 간 α-β 비교는 DCN-M12로 몰기 |
| 🆕 B27 | DSP-M08 ↔ MEDIA-M11 | PDM 비트스트림 → CIC → 보상 FIR → PCM, SNR 측정 | DSP-M08 = CIC 구현과 비트 성장만(PDM 체인 삭제). 사양 기반 다단 체인은 MEDIA-M11 소유 |
| 🆕 B28 | NET-B01 ↔ NETHW-B10 | N포트 러닝 스위치(MAC 학습·플러딩) | NETHW-B10이 FDB 소유. NET-B01의 스위치는 정적 포워딩/허브로 단순화하거나 NETHW-B10 모듈을 가져다 쓰기 |

## C. 비슷하지만 유지 (도메인 조합이 다름)

| 아이템 | 공통 스킬 | 왜 유지하나 |
|---|---|---|
| DRONE-A07 ↔ RCTL-A04 | 지연·지터 → 위상 여유 손실 | 쿼드 SITL 전체 루프 vs 서보 단일 루프 |
| DRONE-A06 ↔ RCTL-A03 | chirp 주파수 응답 식별 + PID 튜닝 | 드론 rate 축 자동 튜닝 vs 일반 플랜트 + 릴레이 auto-tune |
| AUTON-B08 ↔ GAME-B08 | 계층적 상태 머신 | 로봇 미션 관리 vs 게임 AI |
| INFRA-B06 ↔ SIV-M10 | 로그 시그니처 분류 | CI 펌웨어 로그 vs 실리콘 회귀(칩·온도·보드 상관) |
| BMC-M02 ↔ PLAT-A07 ↔ ARCH-A09 | 전원 시퀀싱 | 서버 보드 레일 vs PMIC 드라이버 vs SoC 전원·클럭·리셋 도메인 |
| BMC-A01 ↔ SEC-M02 ↔ GPUSYS-M06 | 보안 부팅 체인 | BMC(OTP 롤백) vs MCU 부트로더 vs GPU 펌웨어 로딩 |
| TEST-B09 ↔ AUTO-M07 | 워치독 감시 | 태스크 체크인 vs AUTOSAR WdgM 논리 감시 |
| SYSD-B06 ↔ TEST-M08 | FMEA 표 | 관측가능성 설계 vs RPN/AP 계산 |
| BMC-B05 ↔ BMS-B06 | 임계값 + 히스테리시스 | IPMI 센서 이벤트 vs 배터리 보호 3단계 |
| DISP-B08 ↔ REND-B05 | 알파 블렌딩 | 디스플레이 합성기 정수 연산 vs 렌더 파이프라인 정렬 |
| CV-B03 ↔ GPGPU-M06, DS-M12 ↔ GPGPU-A05, EML-B02 ↔ GPGPU-M05 | conv / SpMV / GEMM | CPU 기준 구현 vs GPU 타일링·부하 분산 |
| ARCH-B08 ↔ NPU-M04 | toy ISA + 어셈블러 | 범용 CPU vs NPU 명령 |
| AUTON-B01 ↔ LLAT-A07 ↔ CV-A08 | 파이프라인 지연 예산 | 자율주행 / HFT / 비전 |
| DS-A10 ↔ STOR-B05 | append-only 로그 KV | 플래시 섹터 + 전원 차단 vs 디스크 Bitcask |
| GPUSYS-M09 ↔ PLAT-M02 ↔ AIPERF-A08 ↔ ARCH-A08 | DVFS 전력 모델 | GPU 거버너 / SoC 전환 순서 / 추론 에너지 / race-to-idle |
| ELX-A06 ↔ PCIE-A01 | VFIO 바인딩 | 일반 UIO/VFIO vs NVMe 큐까지 |
| ALGO-A10 ↔ OS-M11 | 락 그래프 사이클 | SCC 알고리즘 vs 런타임 lockdep 계측 |
| ALGO-B14 ↔ COMM-B08 | 체크섬 | 구현 vs CRC와 검출률 실험 |
| SENS-A06 ↔ SIV-A03 | 온도 sweep 특성화 | 센서 보정 vs 전력 특성화 |
| PCIE-A02 ↔ SIM-M02 | 큐 깊이 vs IOPS 모델 | NVMe 호스트 인터페이스 vs NAND 다이 병렬성 |
| INFRA-M08 ↔ PLAT-M09 ↔ SYSD-M09 | 텔레메트리 | 서버 수집·알람 / 디바이스 레지스트리 / 설계 (SYSD는 설계만 남기면 깔끔) |
| 🆕 ARCH-B05·B06·B07·A07 ↔ RTLDV-B03·B04·B05·M06 | 우선순위 인코더·LFSR·1011 검출기·Gray FIFO | C 행위 모델 vs 합성 가능한 RTL + cocotb/yosys. RTLDV 쪽에 이미 `see:ARCH`가 있다. RTLDV TB가 ARCH C 모델을 golden으로 쓰게 하면 더 좋다 |
| 🆕 ARCH-A10 ↔ RTLDV-M03 | 고정/RR/가중 RR 중재기 | 지연 분포 모델 vs 아비터 RTL |
| 🆕 C-M01 ↔ RUST-M01, CPP-A01 ↔ RUST-M04, DS-A01 ↔ RUST-M07, DS-B04 ↔ RUST-B04, COMM-B10 ↔ RUST-B06 | MMIO·typestate·SPSC·링버퍼·프레임 파서 | 같은 문제를 Rust 소유권·타입 시스템으로 푸는 것이 RUST 트랙의 목적 |
| 🆕 ARM-B02 ↔ RISCV-B11, ARM-M01 ↔ RISCV-B07, ARM-M04 ↔ RISCV-M04, BM-B08 ↔ RISCV-B12, ARCH-A01 ↔ RISCV-M12, MEM-M07 ↔ RISCV-A04, ARM-M06 ↔ RISCV-A06 | asm 읽기·호출 규약·원자 연산·startup·페이지 워크·메모리 모델·SIMD | AArch64/Cortex-M vs RISC-V. ISA 차이 자체가 학습 대상 |
| 🆕 DSP-B03 ↔ RISCV-B05 | 오버플로 검출 | C 포화 산술 vs 플래그 없는 ISA의 SLTU 관용구 |
| 🆕 AIPERF-A02 ↔ AISYS-M09 ↔ GPGPU-A03 | FlashAttention | IO 복잡도 모델 / numpy forward+backward 재계산 / Metal 커널 |
| 🆕 AIPERF-A03 ↔ AISYS-M12 | continuous batching | 서빙 시뮬레이터 vs 실제 모델을 돌리는 엔진(시뮬 예측과 대조하도록 이미 연결됨) |
| 🆕 QFIN-A01·A02 ↔ QPROB-A03 | 마켓메이킹·역선택 | LOB 시뮬레이터 전략 vs 퀀트 면접 호가 게임 |
| 🆕 ALGO-A10 ↔ SOCX-M05 ↔ DCN-M05 | 의존 그래프 사이클 탐지 | 락 순서 / NoC 채널 의존 / PFC 버퍼 의존 |
| 🆕 SOCX-A02 ↔ PCIE-M11 | 링크 계층 CRC + replay | UCIe D2D 실효 대역폭·꼬리 지연 vs PCIe DLL 크레딧·REPLAY_NUM |
| 🆕 ARCH-A08 ↔ SOCX-A08 ↔ NPU-A07 | DVFS 전력 모델 | SoC race-to-idle / NoC 비트당 에너지 / NPU 파워 도메인 (위 DVFS 줄과 같은 계열) |
| 🆕 HWLAB-A04 ↔ SIM-M08 | 열 RC 네트워크 | 측정 곡선에서 Foster 모델 피팅 vs 순방향 열 모델 시뮬레이션 |
| 🆕 HWLAB-B09·A08 ↔ PLAT-B09 | 보드 bring-up | 계측 장비·전기 측정 계획 vs 펌웨어 smoke-test 순서 |
| 🆕 MEDIA-A05 ↔ DSP-A05, MEDIA-A03 ↔ DSP-A04 | NLMS 에코 제거 / 샘플률 변환 | 시간 영역 기초 vs 주파수 영역 AEC·double-talk / 고정비 SRC vs 클럭 드리프트 추적 ASRC |
| 🆕 LPERF-A01 ↔ ELX-A04, LPERF-A03 ↔ ARCH-M08, LPERF-M03 ↔ TOOL-M14, AIPERF-A06 ↔ LPERF-A05 | 지연·false sharing·심볼화·회귀 감지 | 개념·측정 실험 vs 리눅스 관측 도구(perf c2c, osnoise, buildid-cache, 차등 flame graph) |
| 🆕 WCONN-M08 ↔ RF-M06, WCONN-B01 ↔ RF-M09 | GATT 서버 / 802.11 파싱 | 펌웨어에서 직접 구현 vs 리눅스 BlueZ D-Bus·radiotap 모니터 캡처 |
| 🆕 NETHW-M09 ↔ DCN-M06, SYSD-M08 ↔ WCONN-A05 | ECN 마킹 / 프로비저닝 | 스위치 WRED vs DCTCP 송신자 / 인증서 수명 관리 vs BLE 온보딩 |

신규 트랙끼리, 또는 신규 트랙 안에서 `see:`로 명시적으로 이어 붙인 확장(DS-A07→NETHW-M05 cuckoo, PCIE-A06→NETHW-A03 SR-IOV, LLAT-M08→QFIN-B03 오더북, MEM-A05→NETHW-A07 RS, PLAT-M06→VIRT-M06 vring, COMM-A10→HWLAB-M05 LA 디코더, AISYS-M06→M07, QPROB-M05→A02, DCN-M12→M13 등)도 학습 경로라서 목록에서 뺐다.

같은 트랙 안의 단계적 진행(MEM-B06→B07 Hamming→SECDED, BMS-B09→M10 수동→능동 밸런싱, DIST-B10→M03 Bully→Raft, RCTL-B07→B08 사다리꼴→S-curve, EMU-B06→M01, CAM-B07→M01 등)은 중복이 아니라 학습 경로라서 목록에서 뺐다.

## 다음 단계

- 이 문서는 **검토 결과만** 담았다. `curriculum/tracks/*.json`은 아직 바꾸지 않았다.
- 적용하려면 A의 "권장" 칸대로 아이템을 삭제·교체하고, B는 `task` 문장에 "XX-Mnn 결과물 재사용" 한 줄과 `see:` 태그를 추가하면 된다. 그 뒤 `python3 review_items.py`와 `python3 build_site.py`를 다시 돌린다.
- 아이템을 지우면 같은 레벨 안의 ID 번호가 비게 된다. 기존 규칙("ids sequential")을 지키려면 지우기보다 **같은 ID에 다른 주제로 교체**하는 쪽을 권장한다.
