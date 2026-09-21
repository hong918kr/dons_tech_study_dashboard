/*
Applied Intuition 코딩 챌린지 대비 50제

카테고리 1: 비트 연산 (Bit Manipulation)
1. reg 변수의 pos 위치 비트를 1로 설정하는 SET_BIT 매크로를 작성하세요.
   Follow-up: CLEAR_BIT, TOGGLE_BIT, CHECK_BIT 매크로도 작성하세요.
2. 32비트 부호 없는 정수(uint32_t)에서 1로 설정된 비트의 개수를 세는 함수를 작성하세요 (해밍 가중치).
   Follow-up: 더 효율적인 계산 방법은? (Brian Kernighan 알고리즘)
3. 주어진 정수가 2의 거듭제곱인지 확인하는 함수 bool is_power_of_two(int n)를 작성하세요.
   Follow-up: 비트 연산만으로 구현 (n && !(n & (n - 1)))
4. 32비트 정수의 엔디언(Endianness)을 변환하는 함수 uint32_t swap_endian(uint32_t val)를 작성하세요.
   Follow-up: 시스템 엔디언 확인 코드 작성
5. 부호 없는 8비트 정수(uint8_t)의 비트 순서를 뒤집는 함수 uint8_t reverse_bits(uint8_t num)를 작성하세요.
   Follow-up: 32비트 정수로 확장
6. 두 정수를 임시 변수 없이 XOR로 교환(swap)하는 코드를 작성하세요.
   Follow-up: 이 방법의 장단점은?
7. 정수의 특정 비트가 1인지 0인지 확인하는 함수를 작성하세요.
   Follow-up: 특정 범위의 비트 추출 함수 작성
8. 정수의 홀수 번째 비트들만 1로 설정하는 함수를 작성하세요.
   Follow-up: 짝수 번째 비트들을 모두 0으로 만드는 함수
9. 두 정수 a와 b를 더할 때, 덧셈 연산자(+) 없이 비트 연산만으로 구현하세요.
   Follow-up: 이 방법의 성능상 이점/한계는?
10. 주어진 숫자의 패리티(parity)가 짝수인지 홀수인지 확인하는 함수를 작성하세요.
    Follow-up: 큰 데이터 스트림의 패리티 효율적 계산법
11. (1 << n)으로 n번째 비트 마스크 만드는 코드
    Follow-up: n번째 비트만 0이고 나머지는 1인 마스크 (~(1 << n))
12. x & (-x) 비트 트릭 설명 및 예시
    Follow-up: LSB 분리, 활용 예시
13. 레지스터의 특정 필드(3-5번 비트)를 주어진 값으로 업데이트하는 함수
    Follow-up: 원자적(atomic) 수행 이유
14. uint32_t 변수에서 상위 16비트와 하위 16비트 교환 함수
    Follow-up: 데이터 압축/암호화에서의 활용
15. 숫자를 2로 나누거나 곱하는 연산을 시프트 연산자로 구현
    Follow-up: 음수의 오른쪽 시프트 동작(산술/논리)

카테고리 2: 자료구조 (Data Structures)
16. uint8_t 데이터를 저장하는 원형 버퍼(Circular Buffer) 구현 (init, push, pop, is_empty, is_full)
    Follow-up: head==tail일 때 가득참/비어있음 구분법
17. (16번 연계) ISR과 메인 루프 간 데이터 전송 시 스레드 안전성 보장법
    Follow-up: volatile 변수 사용 이유
18. 고정 크기 메모리 풀에서 노드 할당하는 단일 연결 리스트 구현 (malloc 금지)
    Follow-up: 실시간 시스템에서 malloc을 피하는 이유
19. 배열로 스택(LIFO) 구현 (push, pop, peek)
    Follow-up: 오버플로우/언더플로우 감지법
20. 배열로 큐(FIFO) 구현 (enqueue, dequeue)
    Follow-up: 원형 버퍼와의 차이, 적합 상황
21. 키-값 쌍 저장 간단 해시 테이블 구현 (충돌은 연결 리스트)
    Follow-up: 임베디드에서 해시 테이블의 메모리/성능 고려
22. 이진 탐색 트리(BST)에 정수 삽입 함수
    Follow-up: 펌웨어에서 BST가 자주 쓰이지 않는 이유

카테고리 3: 데이터 파싱 및 상태 머신 (Data Parsing & FSM)
23. [0x7E, 길이, 데이터..., 체크섬] 패킷 파서 함수 (체크섬=합)
    Follow-up: 유효하지 않은 패킷 처리법
24. enum/switch로 LED 3상태(OFF, ON, BLINKING) FSM 함수
    Follow-up: 상태 변수 static 선언 이유
25. 쉼표로 구분된 숫자 문자열 파싱 함수
    Follow-up: strtok의 위험성과 대안
26. 16진수 문자열을 정수로 변환하는 함수 int hex_to_int(const char* hex_str)
    Follow-up: 유효하지 않은 문자 처리법
27. 12비트 ADC값(0-4095)을 0.0~3.3V로 변환하는 함수
    Follow-up: 정수 연산만으로 변환하는 방법

카테고리 4: 포인터와 메모리 (Pointers & Memory)
28. const 키워드로 (1) 정수 상수, (2) 상수에 대한 포인터, (3) 정수에 대한 상수 포인터 선언
    Follow-up: const int* p와 int* const p 차이
29. void 포인터란? 유용한 상황 설명
    Follow-up: 역참조 전 타입 캐스팅 이유
30. 함수 포인터로 콜백 메커니즘 예제
    Follow-up: 함수 포인터 배열로 FSM/명령어 처리기 유연성
31. my_strcpy 함수 구현
    Follow-up: 버퍼 오버플로우 방지법(strncpy 등)
32. struct의 메모리 정렬(alignment)과 패딩 설명
    Follow-up: sizeof로 구조체 크기가 예상과 다른 이유
33. 스택과 힙 메모리 차이 설명
    Follow-up: 펌웨어에서 스택 오버플로우의 위험성
34. volatile 키워드가 필요한 코드 예시와 이유
    Follow-up: volatile 없는 코드에서의 최적화 문제
35. static 키워드의 함수 내/파일 범위 사용 차이
    Follow-up: static 함수의 캡슐화 효과

카테고리 5: 기본 알고리즘 및 논리 (Basic Algorithms & Logic)
36. 정수 배열에서 유일한 숫자 찾기 (나머지는 2번씩)
    Follow-up: 추가 메모리 없이 O(n) 풀이(XOR)
37. 정렬된 배열에서 특정 값의 삽입 위치 찾기
    Follow-up: 이진 탐색(Binary Search)로 효율화
38. 이동 평균 필터(Moving Average Filter) 구현
    Follow-up: 효율적 이동 평균 계산법
39. C 문자열을 제자리에서 뒤집는 함수 void reverse_string(char* str)
    Follow-up: 재귀적 구현 가능성
40. 피보나치 수열 n번째 항 계산 함수
    Follow-up: 재귀의 비효율성, DP/반복문 개선
41. 버튼 채터링 방지 디바운스 로직 구현
    Follow-up: 폴링 메인 루프 통합법
42. 8비트 곱셈(곱셈자 * 사용 금지, 16비트 결과)
    Follow-up: 하드웨어 곱셈기보다 느린 이유
43. 런-렝스 인코딩(RLE) 압축 함수
    Follow-up: 압축 문자열이 원본보다 길 때 처리
44. main() 함수 호출 전 실행되는 코드와 역할
    Follow-up: .data/.bss 세그먼트 초기화 과정
45. ISR에서 printf 호출이 위험한 이유
    Follow-up: ISR의 특성(짧고, 빠르며, 비결정적 지연 없음)
46. 두 정렬된 배열을 하나로 병합하는 함수
    Follow-up: 병합 정렬(Merge Sort) 시간 복잡도
47. 팩토리얼 재귀 함수
    Follow-up: 스택 오버플로우 발생 입력 범위
48. 문자열이 회문(palindrome)인지 확인하는 함수
    Follow-up: 대소문자 무시, 공백 건너뛰기
49. 배열 요소를 오른쪽으로 한 칸 회전 함수
    Follow-up: k칸씩 효율적 회전 구현
50. CRC-8 체크섬 계산 함수
    Follow-up: CRC가 sum/XOR보다 강력한 이유

*/