# 코딩 레퍼런스 뱅크 작성 스펙 (문제 06~23 + 레벨 노트 L0~L3)

기존 `SPEC.md`의 규칙을 그대로 따른다(파일 구조, 마크다운 부분집합, 경고 0 컴파일, assert 테스트 + `ALL TESTS PASSED`, starter는 TODO만 비움). 이 문서는 **확장분**의 배정과 추가 규칙만 정한다.

## 목적
기존 5문제(01~05)는 "면접 직전 드릴"이다. 이 확장분은 **기초부터 다시 쌓고, 나중에 레퍼런스로 다시 펼쳐볼 수 있는 교재**다.
Hark BSP 면접에서 실제로 나올 수 있는 범위: 비트 연산 → 메모리·포인터 → 임베디드 관용구 → 임베디드 C++.

## 레벨과 배정
| 레벨 | 노트 | 문제 |
|---|---|---|
| **L0 비트·정수 기초** | `notes/L0_bit_basics.md` | 06 bit_ops · 07 bit_count_reverse · 08 endian_align · 09 saturating_math |
| **L1 메모리·포인터** | `notes/L1_memory_pointers.md` | 10 memcpy_move · 11 struct_layout · 12 volatile_const · 13 func_table |
| **L2 임베디드 관용구** | `notes/L2_embedded_idioms.md` | 14 debounce_fsm · 15 fixed_point_filter · 16 crc8_table · 17 timer_wheel |
| **L3 임베디드 C++** | `notes/L3_embedded_cpp.md` | 18 raii_guard · 19 static_vector · 20 register_wrapper · 21 object_pool |

## 문제 상세 (반드시 이 범위로)
- **06 bit_ops**: set/clear/toggle/test, 마스크 생성, 여러 비트 동시 조작, `1u << n` 오버플로 함정, 비트 범위 추출·삽입
- **07 bit_count_reverse**: popcount(naive/Kernighan/테이블), 비트 반전, find first/last set, 2의 거듭제곱 판정, 다음 2의 거듭제곱
- **08 endian_align**: byte swap 16/32/64, 런타임 엔디언 판별, unaligned read/write 안전 구현, 정렬 올림(`align_up`), 포인터 정렬 확인
- **09 saturating_math**: 포화 덧셈·뺄셈(부호/무부호), 오버플로 검출, 고정소수점 Q15 곱셈과 반올림, 나눗셈 없는 평균
- **10 memcpy_move**: `memcpy`와 `memmove` 차이를 직접 구현으로 보이기, 겹침 판정, word 단위 최적화 버전, `restrict` 의미
- **11 struct_layout**: 패딩 계산, `offsetof`, 패킹된 구조체 접근의 위험, 바이트 스트림 ↔ 구조체 직렬화(패킹 의존 없이)
- **12 volatile_const**: `volatile` 유무로 달라지는 동작 재현, `const volatile`, 레지스터 접근 래퍼, 컴파일러 최적화 관찰
- **13 func_table**: 함수 포인터 디스패치 테이블, 명령 핸들러, 콜백 + 컨텍스트, 테이블 검증(널 체크·범위)
- **14 debounce_fsm**: 버튼 디바운스 상태기계(누름/뗌/롱프레스), 틱 기반, 노이즈 시퀀스 테스트
- **15 fixed_point_filter**: Q15/Q16 표현, 이동평균, 1차 IIR, 오버플로 방지, float 대비 오차 비교
- **16 crc8_table**: CRC-8 비트 단위 → 테이블 생성 → 테이블 버전, 체크값 검증, 스트리밍 갱신
- **17 timer_wheel**: 소프트웨어 타이머 목록, wrap-safe 시간 비교(`(int32_t)(a-b) < 0`), 주기·원샷, 만료 처리
- **18 raii_guard**: 인터럽트 잠금 RAII 가드, 스코프 종료 시 복원, 이동 금지·복사 금지, `-fno-exceptions` 환경 가정
- **19 static_vector**: 힙 없는 고정 용량 컨테이너, placement new 사용, 소멸자 호출, `emplace_back`, 용량 초과 처리
- **20 register_wrapper**: 타입 안전 레지스터 필드 접근(템플릿/constexpr), 컴파일 타임 마스크 계산, 잘못된 필드 폭 컴파일 에러
- **21 object_pool**: 고정 풀 + placement new, `acquire/release`, 소멸자 보장, 04 memory pool과의 차이 설명

## 추가 규칙
1. **L3(18~21)은 C++**다. 파일 확장자 `.cpp`, 빌드는 `c++ -std=c++17 -Wall -Wextra -O2 -fno-exceptions -fno-rtti`. 예외·RTTI·힙 사용 금지(placement new는 허용). 표준 라이브러리는 `<cstdint> <cstddef> <new> <type_traits> <cassert> <cstdio>`까지만.
2. **레벨 노트**(`notes/L0~L3`)는 300~450줄. 문제 풀이가 아니라 **개념 레퍼런스**다. 구조:
   `# L?. 제목` → 헤더 인용 블록(목표·수록 문제·언제 다시 보나) → `## 0. 왜 이게 면접에 나오나` → 개념 절(그림·표·짧은 코드) → `## 흔한 함정` 표 → `## 면접 질문 10개`(질문/30초 답/English 한 문장) → `## 이 레벨 문제들` 표(링크) → `## 체크리스트`
3. 문제 노트는 만들지 않는다. **해설은 레벨 노트에 모으고**, 각 `problems/NN_*.md` 하단에 "해설: [L? 노트](../drills/L?_*.html) §N" 링크를 단다. (빌더가 `coding/notes/` 를 `site/drills/` 로 렌더하므로 href 는 반드시 `../drills/`) (01~05는 기존 방식대로 개별 해설 노트가 있다.)
4. 각 문제는 **15~30분 안에 풀 수 있는 크기**로 자른다. 한 파일에 함수 3~6개.
5. 문제 파일에 **난이도(기초/중급/심화)와 목표 시간**을 적는다.
6. 테스트는 경계값을 반드시 포함한다: 0, 1, 최대값, 부호 경계, wrap, 정렬 어긋남, 용량 초과.
