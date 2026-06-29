# 코딩 문제 리스트 (Research)

> `don-c-prac` 레포(임베디드/펌웨어 인터뷰 준비)의 **Anduril `c_Coding_Questions_100ea`** 폴더를 Claude Code가 분석·리서치한 문제 카탈로그입니다.
> 각 함수(문제)별로 **설명 · 핵심 개념/접근 · 난이도**를 정리했습니다. 코딩 문제 대시보드(`/problems`)의 파일 단위 문제를 함수 단위로 풀어 본 연구 자료입니다.

## 개요

| 파일(문제 묶음) | 범위 | 주제 | 채점 |
|---|---|---|---|
| basicOps | 1–10 | 비트 조작 기본 | 자동 `[PASS]/[FAIL]` |
| dataManip | 11–25 | 비트 단위 데이터 조작 | 자동 |
| strFunc | 26–40 | 문자열 libc 재구현 | 출력 비교(수동) |
| memFunc | 41–55 | 메모리 libc·포인터 개념 | 출력 비교(수동) |
| ll+cb | 56–75 | 원형 버퍼 + 단일 연결리스트 | 출력 비교(수동) |
| linkedList_singly / doubly / wo_malloc | 66–75 | 연결리스트 연산 | 출력 비교(수동) |
| fsm-* | 76–85 | 유한상태머신(FSM) 패턴 | 출력 비교(수동) |
| dataProcess | 86–95 | 체크섬·CRC·압축·Base64·필터 | 출력 비교(수동) |

> 난이도 표기는 분석 기준 추정치입니다. `*_sol.c`가 있는 일부(basicOps·dataManip)는 정답 기준, 나머지는 시그니처·주석·접근으로 추정.

---

## 1. 비트 조작 · 데이터 조작

### `1-10-basicOps.c` — 비트 조작 기본 연산
단일 비트 set/clear/toggle/test와 부호·절대값을 다루는 가장 기초적인 비트 연산 10문제.

| # | 함수 | 설명 | 핵심 개념·접근 | 난이도 |
|---|------|------|----------------|--------|
| 1 | `uint32_t set_bit(uint32_t value, uint8_t bit)` | 특정 비트를 1로 설정 | 비트마스크 OR `value \| (1U<<bit)` | Easy |
| 2 | `uint32_t clear_bit(uint32_t value, uint8_t bit)` | 특정 비트를 0으로 클리어 | 반전 마스크 AND `value & ~(1U<<bit)` | Easy |
| 3 | `uint32_t toggle_bit(uint32_t value, uint8_t bit)` | 특정 비트를 반전(토글) | XOR `value ^ (1U<<bit)` | Easy |
| 4 | `bool is_bit_set(uint32_t value, uint8_t bit)` | 특정 비트가 1인지 확인 | AND 마스크 후 진리값 `value & (1U<<bit)` | Easy |
| 5 | `uint8_t swap_nibbles(uint8_t value)` | 바이트의 상위/하위 4비트 니블 교환 | `(v<<4) \| (v>>4)`, uint8 폭에서 자동 절단 | Easy |
| 6 | `bool parity_even(uint32_t value)` | 1의 개수 짝수 패리티 계산 | 비트 순회하며 XOR 누적 후 `!parity` | Easy |
| 7 | `uint32_t clear_lsb(uint32_t value)` | 가장 낮은 set 비트 제거 | Brian Kernighan `value & (value-1)` | Easy |
| 8 | `uint32_t rightmost_set_bit(uint32_t value)` | 최하위 set 비트만 남기기 | 2의 보수 `value & (~value+1)` (= `value & -value`) | Easy |
| 9 | `bool is_opposite_sign(int32_t a, int32_t b)` | 두 정수 부호가 다른지 확인 | XOR 후 부호비트 검사 `(a^b) < 0` | Easy |
| 10 | `int32_t abs_no_branch(int32_t value)` | 분기 없이 절대값 계산 | 산술시프트 마스크 `mask=v>>31; (v^mask)-mask` | Medium |

### `11-25-dataManip.c` — 비트 단위 데이터 조작
비트 반전·카운트·범위 조작·회전·엔디안·그레이 코드 등 임베디드에서 자주 나오는 데이터 조작 15문제.

| # | 함수 | 설명 | 핵심 개념·접근 | 난이도 |
|---|------|------|----------------|--------|
| 11 | `uint32_t reverseBits(uint32_t n)` | 32비트 비트 순서 뒤집기 | 32회 루프 `res<<1 \| (n&1)` (또는 SWAR/룩업테이블) | Medium |
| 12 | `uint32_t hamming_weight(uint32_t n)` | set 비트 개수 카운트(popcount) | `count += n&1; n>>=1` (빠른 버전 `n&=(n-1)`) | Easy |
| 13 | `bool is_power_of_two(uint32_t n)` | 2의 거듭제곱 여부 판별 | `n && !(n & (n-1))` (0 예외 처리 필수) | Easy |
| 14 | `uint32_t swap_odd_even_bits(uint32_t n)` | 홀수/짝수 위치 비트 교환 | 마스크 `0xAAAA…>>1 \| 0x5555…<<1` | Medium |
| 15 | `uint32_t extract_bit_range(uint32_t n, uint8_t start, uint8_t end)` | 지정 범위 비트 추출 | 폭 마스크 `((1U<<(end-start+1))-1)<<start` 후 시프트 | Medium |
| 16 | `uint32_t set_bit_range(uint32_t n, uint8_t start, uint8_t end, uint32_t value)` | 지정 범위에 값 쓰기 | 마스크 클리어 후 OR `(n&~mask)\|((value<<start)&mask)` | Medium |
| 17 | `uint32_t invert_bit_range(uint32_t n, uint8_t start, uint8_t end)` | 지정 범위 비트 반전 | 범위 마스크와 XOR `n ^ mask` | Medium |
| 18 | `uint32_t next_power_of_two(uint32_t n)` / `prev_power_of_two(uint32_t n)` | n 이상/이하 가장 가까운 2의 거듭제곱 | next: `n--` 후 `n\|=n>>1,2,4,8,16; +1` / prev: 루프 `p<<=1` | Medium |
| 19 | `void xor_swap(uint32_t *a, uint32_t *b)` | 임시 변수 없이 두 값 교환 | XOR swap 3단계, 같은 주소(`a==b`) 예외 처리 | Easy |
| 20 | `uint32_t rotate_left(uint32_t n, uint8_t k)` / `rotate_right(uint32_t n, uint8_t k)` | 비트 회전(좌/우) | `(n<<k)\|(n>>(32-k))` 시프트 결합 (k=0/32 UB 주의) | Medium |
| 21 | `uint32_t swap_endian32(uint32_t n)` | 32비트 엔디안 변환 | 바이트별 시프트+마스크 4개 OR | Medium |
| 22 | `uint32_t get_field(uint32_t reg, uint8_t pos, uint8_t width)` / `set_field(...)` | 비트필드 읽기/쓰기 시뮬레이션 | 폭 마스크 `((1U<<width)-1)<<pos`, get은 시프트·set은 clear후 OR | Medium |
| 23 | `uint32_t binary_to_gray(uint32_t n)` | 이진수→그레이 코드 변환 | `n ^ (n>>1)` | Easy |
| 24 | `uint32_t gray_to_binary(uint32_t n)` | 그레이 코드→이진수 변환 | 누적 XOR: `while(n>>=1) res^=n` | Medium |
| 25 | `uint32_t mul_pow2(uint32_t n, uint8_t k)` / `div_pow2(uint32_t n, uint8_t k)` | 2의 거듭제곱 곱셈/나눗셈 | 시프트 `n<<k` / `n>>k` | Easy |

---

## 2. 문자열 · 메모리 (libc 재구현)

### `26-40-strFunc.c` — 문자열 libc 함수 재구현
표준 `<string.h>`/문자열 변환 함수(strlen, strcpy, strtok, sprintf 등)를 직접 구현하는 문제 모음.

| # | 함수 | 설명 | 핵심 개념·접근 | 난이도 |
|---|------|------|----------------|--------|
| 26 | `size_t my_strlen(const char *s)` | 널 종결 전까지 문자 개수 세기 | 포인터 순회, 시작 포인터와의 차로 길이 계산 | Easy |
| 27 | `char* my_strcpy(char *dest, const char *src)` | src를 널 포함 dest로 복사 | `'\0'`까지 복사 후 종결자도 복사, dest 원본 반환 | Easy |
| 28 | `char* my_strncpy(char *dest, const char *src, size_t n)` | 최대 n바이트 복사 | src가 짧으면 나머지 `'\0'` 패딩(표준 의미) | Medium |
| 29 | `char* my_strcat(char *dest, const char *src)` | dest 끝에 src 이어붙이기 | dest 끝(`'\0'`) 탐색 후 복사 | Easy |
| 30 | `int my_strcmp(const char *s1, const char *s2)` | 사전식 문자열 비교 | 첫 불일치에서 `unsigned char` 차 반환 | Easy |
| 31 | `char* my_strstr(const char *haystack, const char *needle)` | 부분 문자열 첫 위치 탐색 | 이중 루프 슬라이딩 매칭, 빈 needle 처리 | Medium |
| 32 | `char* my_strchr(const char *s, int c)` | 문자 첫 출현 위치 탐색 | c를 char로 캐스팅, `'\0'`도 매칭 대상 | Easy |
| 33 | `int my_atoi(const char *str)` | 문자열을 정수로 파싱 | 공백 스킵·부호 처리·`result*10+digit` 누적 | Medium |
| 34 | `char* my_itoa(int n, char *s, int base)` | 정수를 진법 문자열로 변환 | 나머지로 역순 생성 후 뒤집기, 음수/부호 | Medium |
| 35 | `void str_to_lower(char *s)` / `str_to_upper(char *s)` | 대소문자 인플레이스 변환 | 범위 검사 후 `±32`, 인플레이스 수정 | Easy |
| 36 | `void reverse_words(char *s)` | 단어 순서 역순 재배치 | 전체 뒤집기 후 각 단어 다시 뒤집기(2단계 reverse) | Hard |
| 37 | `void remove_duplicates(char *s)` | 중복 문자 제거 | 등장 여부 lookup `bool[256]`, 읽기/쓰기 두 인덱스 압축 | Medium |
| 38 | `bool is_palindrome(const char *s)` | 회문 여부 판별 | 양끝 투 포인터 수렴 비교 | Easy |
| 39 | `char* my_strtok(char *str, const char *delim)` | 구분자 기반 토큰 분리 | `static` 상태 포인터, 구분자를 `'\0'`로 치환, NULL 호출로 이어가기 | Hard |
| 40 | `int my_sprintf(char *str, const char *format, ...)` | 간이 포맷 출력 (`%d`,`%s`) | 가변인자 `va_list`, 포맷 파싱, 길이 반환 | Hard |

### `41-55-memFunc.c` — 메모리 libc 함수 및 포인터 개념
`<string.h>` 메모리 함수(memcpy/memmove 등) 재구현과 포인터·메모리 개념 문제. (48·49·52·53·54·55는 함수가 아닌 **개념 설명** 문제 — `const`/`volatile`/댕글링 포인터/`void*`/구조체 패딩/포인터 배열 vs 배열 포인터)

| # | 함수 | 설명 | 핵심 개념·접근 | 난이도 |
|---|------|------|----------------|--------|
| 41 | `void* my_memset(void *s, int c, size_t n)` | n바이트를 값 c로 채우기 | `void*`→`unsigned char*` 캐스팅, 바이트 단위 쓰기 | Easy |
| 42 | `void* my_memcpy(void *dest, const void *src, size_t n)` | n바이트 복사(겹침 미고려) | 바이트 단위 정방향 복사 | Easy |
| 43 | `void* my_memmove(void *dest, const void *src, size_t n)` | 겹침 안전 복사 | dest>src면 역방향, 아니면 정방향 (방향 판단이 핵심) | Medium |
| 44 | `int my_memcmp(const void *s1, const void *s2, size_t n)` | n바이트 비교 | `unsigned char` 비교, 첫 불일치 차 반환 | Easy |
| 45 | `void* my_memchr(const void *s, int c, size_t n)` | n바이트 내 c 첫 위치 | 길이 기반 순회, 미발견 시 NULL | Easy |
| 46 | `void swap_int(int *a, int *b)` | 두 정수 포인터 값 교환 | 역참조 통한 임시 변수 스왑 | Easy |
| 47 | `void reverse_string(char *s)` | 문자열 인플레이스 뒤집기 | 양끝 투 포인터 교환 | Easy |
| 50 | `void dispatch_table_example(void)` | 함수 포인터 배열 디스패치 | `func_ptr_t` 배열에 등록 후 인덱스로 호출 | Medium |
| 51 | `void* static_malloc(size_t size)` / `static_free(void *ptr)` | 정적 풀 기반 간이 할당기 | 정적 버퍼 + 블록 헤더/free 리스트로 할당·반환 | Hard |

---

## 3. 자료구조 — 원형 버퍼 · 연결리스트

### `56-75_ll+cb.c` — 원형 버퍼 + 단일 연결리스트 종합
원형 버퍼(circular buffer)와 단일 연결리스트의 핵심 연산을 한 파일에 모은 완성 구현(레퍼런스 해답).

| # | 함수 | 설명 | 핵심 개념·접근 | 난이도 |
|---|------|------|----------------|--------|
| 57 | `void cb_init(circular_buffer_t *cb, uint8_t *buffer, size_t size)` | 원형 버퍼 초기화 | 외부 버퍼 주입, head/tail/count=0 | Easy |
| 58 | `bool cb_push(circular_buffer_t *cb, uint8_t data)` | 데이터 추가(enqueue) | full 판정(count==size), head 모듈로 증가 | Medium |
| 59 | `bool cb_pop(circular_buffer_t *cb, uint8_t *data)` | 데이터 추출(dequeue) | empty 판정(count==0), tail 모듈로 증가 | Medium |
| 60 | `bool cb_is_empty(const circular_buffer_t *cb)` | 비었는지 확인 | count==0 | Easy |
| 61 | `bool cb_is_full(const circular_buffer_t *cb)` | 가득 찼는지 확인 | count==size | Easy |
| 63 | `size_t cb_size(const circular_buffer_t *cb)` | 현재 개수 반환 | count 필드 | Easy |
| 64 | `void cb_flush(circular_buffer_t *cb)` | 버퍼 비우기 | head/tail/count 리셋 | Easy |
| 67 | `void list_push_front(node_t **head, int data)` | 맨 앞 노드 추가 | 이중 포인터로 head 갱신 | Medium |
| 68 | `void list_push_back(node_t **head, int data)` | 맨 뒤 노드 추가 | 빈 리스트 예외, 끝까지 순회 | Medium |
| 69 | `void list_delete_value(node_t **head, int value)` | 특정 값 삭제 | 이중 포인터(`node_t **cur`)로 head 삭제까지 통합 | Medium |
| 70 | `void list_reverse(node_t **head)` | in-place 역순 | prev/cur/next 3포인터 뒤집기 | Medium |
| 71 | `node_t* list_find_middle(node_t *head)` | 중간 노드 찾기 | 빠른/느린 포인터 | Medium |
| 72 | `bool list_has_cycle(node_t *head)` | 순환 탐지 | Floyd 사이클 검출 | Hard |
| 73 | `node_t* list_merge_sorted(node_t *l1, node_t *l2)` | 정렬 리스트 병합 | dummy 노드 + tail 포인터 | Hard |
| 74 | `node_t* list_nth_from_end(node_t *head, size_t n)` | 끝에서 N번째 | 빠른 포인터 n칸 선행 후 동시 이동 | Medium |

> 개념 항목(62 카운터 기반 full/empty, 65 SPSC 락프리 스레드 안전, 66 노드 구조체, 75 무malloc 풀)은 구현 함수가 아닌 주석/타입 정의로 존재.

### `66-74-linkedList_singly.c` — 단일 연결리스트 (연습용 스텁)
위 종합 파일의 연결리스트 부분만 분리한 미구현(TODO) 연습 세트.

| # | 함수 | 핵심 개념·접근 | 난이도 |
|---|------|----------------|--------|
| 67 | `list_push_front` | 이중 포인터로 head 갱신 | Medium |
| 68 | `list_push_back` | 끝 노드까지 순회, 빈 리스트 예외 | Medium |
| 69 | `list_delete_value` | 이중 포인터로 head 삭제 통합 | Medium |
| 70 | `list_reverse` | prev/cur/next 3포인터 | Medium |
| 71 | `list_find_middle` | 빠른/느린 포인터 | Medium |
| 72 | `list_has_cycle` | Floyd 사이클 검출 | Hard |
| 73 | `list_merge_sorted` | dummy + tail 포인터 | Hard |
| 74 | `list_nth_from_end` | 투 포인터 간격 유지 | Medium |

### `66-74-linkedList_doubly.c` — 이중 연결리스트 (연습용 스텁)
prev/next 양방향 포인터를 가진 이중 연결리스트(doubly linked list) 연산.

| # | 함수 | 핵심 개념·접근 | 난이도 |
|---|------|----------------|--------|
| 67 | `dlist_push_front` | 기존 head의 prev를 신규 노드로 연결(양방향) | Medium |
| 68 | `dlist_push_back` | 끝 노드 순회 후 new->prev 설정 | Medium |
| 69 | `dlist_delete_value` | prev·next 양쪽 링크 재연결 | Medium |
| 70 | `dlist_reverse` | 각 노드 prev/next 스왑 후 head 재지정 | Medium |
| 71 | `dlist_find_middle` | 빠른/느린 포인터 | Medium |
| 72 | `dlist_has_cycle` | Floyd 사이클 검출 | Hard |
| 73 | `dlist_merge_sorted` | dummy + tail, prev 링크도 유지 | Hard |
| 74 | `dlist_nth_from_end` | 투 포인터 간격 유지 | Medium |

### `75-linkedList_wo_malloc.c` — malloc 없는 정적 풀 연결리스트
정적 배열 노드 풀 + free list로 동적 할당 없이 연결리스트를 구현(임베디드 단편화·비결정적 지연 회피).

| # | 함수 | 핵심 개념·접근 | 난이도 |
|---|------|----------------|--------|
| 1 | `node_pool_init` | 정적 배열을 next로 사슬 연결, free_list=첫 노드 | Medium |
| 2 | `node_alloc` | free list 머리 pop, 고갈 시 NULL | Medium |
| 3 | `node_free` | free list 머리에 push(LIFO 재사용) | Medium |
| 4 | `list_push_front` | node_alloc 사용, 고갈 시 false | Medium |
| 5 | `list_push_back` | 끝까지 순회 + 할당 실패 처리 | Medium |
| 6 | `list_delete_value` | 삭제 후 node_free로 풀 반납 | Medium |
| 7 | `list_free_all` | 순회하며 node_free, head=NULL | Medium |

---

## 4. 유한상태머신 (FSM)

### `76-fsm-buttonDebouncingFSM.c` — 버튼 디바운싱 FSM
하드웨어 버튼 채터링을 제거하기 위해, 매 틱마다 GPIO 입력을 샘플링해 일정 횟수 연속 유지될 때만 눌림/뗌을 확정.

| 항목 | 내용 |
|------|------|
| 상태 | `BTN_RELEASED`, `BTN_BOUNCE_PRESS`, `BTN_PRESSED`, `BTN_BOUNCE_RELEASE` |
| 입력 | 매 틱의 GPIO 값 (0/1) |
| 구현 함수 | `button_fsm_init(...)`, `button_fsm_update(button_fsm_t *fsm, int gpio_input)` |
| 핵심 개념·접근 | switch-case + 디바운싱 카운터. BOUNCE에서 `debounce_counter`가 한계 도달 시 확정, 도중 입력 변하면 이전 안정 상태 복귀 |
| 난이도 | Medium |

### `77-fsm-using-arr-func-ptr.c` — 함수 포인터 배열 FSM
상태별 동작을 함수 포인터 테이블로 관리해 switch-case 없이 전이·핸들러 호출.

| 항목 | 내용 |
|------|------|
| 상태 | `STATE_IDLE`, `STATE_RUN`, `STATE_STOP` (`STATE_MAX` 경계) |
| 입력 | 정수 input (0/1/2; 범위 밖 invalid) |
| 구현 함수 | `state_idle/run/stop(void)`, `fsm_init`, `fsm_update`; `typedef void (*state_func_t)(void)` |
| 핵심 개념·접근 | `state_func_t state_table[STATE_MAX]` 테이블 분기. 경계 검사 후 `state_table[state]()` 호출 — 상태 추가 시 함수만 추가하는 확장형 |
| 난이도 | Easy |

### `78-fsm-uartPacketParser.c` — UART 패킷 파서 FSM
수신 바이트를 SYNC → LENGTH → DATA → CHECKSUM 순으로 파싱, XOR 체크섬 일치 시 패킷 완성.

| 항목 | 내용 |
|------|------|
| 상태 | `UART_IDLE`, `UART_SYNC`, `UART_LENGTH`, `UART_DATA`, `UART_CHECKSUM` |
| 입력 | 수신 바이트 (SYNC=0xAA, length, data[], checksum) |
| 구현 함수 | `uart_fsm_init(...)`, `uart_fsm_update(uart_fsm_t *fsm, uint8_t byte)` |
| 핵심 개념·접근 | 바이트 단위 switch-case 전이. 길이 검증, 데이터 누적·XOR 체크섬 계산, 마지막 바이트 비교로 `packet_ready` — 프레이밍/프로토콜 파싱 전형 |
| 난이도 | Medium |

### `79-fsm-trafficControlLight.c` — 신호등 제어 FSM
타이머 기반으로 RED → GREEN → YELLOW 순환. 매 틱 타이머 감소, 0이면 다음 상태로 전이·시간 재로드.

| 항목 | 내용 |
|------|------|
| 상태 | `TL_RED`, `TL_GREEN`, `TL_YELLOW` (`TL_MAX` 경계) |
| 입력 | 외부 입력 없음 — 주기적 틱(타이머 만료가 트리거) |
| 구현 함수 | `tl_fsm_init(...)`, `tl_fsm_update(...)`, `tl_state_str(...)` |
| 핵심 개념·접근 | 타이머 카운트다운 + switch-case 순환. `--timer<=0`에서 전환, 상태별 지속 시간 재설정 — 시간 기반(time-driven) FSM |
| 난이도 | Easy |

### `82-fsm-EntryExitAction.c` — Entry/Exit 액션 FSM
상태 진입/이탈 시 콜백을 자동 호출(상태별 초기화·정리 자동화).

| 항목 | 내용 |
|------|------|
| 상태/이벤트 | `STATE_IDLE/RUN/STOP` / 입력 0·1·2 |
| 구현 함수 | `fsm_init`, `fsm_transition(...)`, `fsm_update(...)`, `entry_*/exit_*` 액션 |
| 핵심 개념·접근 | `entry_actions[]`/`exit_actions[]` 함수 포인터 배열. 전이 시 `exit[cur]()` → 상태 갱신 → `entry[next]()`. 동일 상태면 생략 가드 |
| 난이도 | Medium |

### `83-fsm-guardCondition.c` — Guard 조건 FSM
같은 이벤트라도 변수 조건(guard)에 따라 다른 상태로 분기 (`value>0`→RUN, 아니면 ERROR).

| 항목 | 내용 |
|------|------|
| 상태/이벤트 | `STATE_IDLE/RUN/ERROR` / 이벤트 0(run 시도)·1(reset) |
| 구현 함수 | `fsm_update(fsm_t *fsm, int event)`, `entry_idle/run/error()` |
| 핵심 개념·접근 | Guard condition: 이벤트 0에 대해 `value>0` 가드로 RUN vs ERROR 분기. 다중 진입 경로(reset→IDLE) 처리 |
| 난이도 | Easy |

### `84-fsm-eventQueue.c` — 이벤트 큐(원형 버퍼) FSM
원형 버퍼 기반 이벤트 큐로 ISR(생산자)–FSM(소비자) 디커플링.

| 항목 | 내용 |
|------|------|
| 상태/이벤트 | `STATE_IDLE/RUN/ERROR` / `EVENT_START/STOP/ERROR/NONE` |
| 구현 함수 | `event_queue_init`, `event_queue_push`, `event_queue_pop`, `fsm_handle_event(...)` |
| 핵심 개념·접근 | `head/tail/count` 원형 버퍼(`% SIZE`), count로 full/empty 모호성 제거. pop 루프에서 꺼내 `switch(state)` 디스패치 |
| 난이도 | Medium |

### `85-fsm-handlesTimeoutEvents.c` — 타임아웃 처리 FSM
틱마다 타이머 증가, 무활동 한계 도달 시 TIMEOUT 전이, 활동 재개 시 복구.

| 항목 | 내용 |
|------|------|
| 상태/이벤트 | `STATE_IDLE/ACTIVE/TIMEOUT` / 이벤트 1(activate)·0(no event) |
| 구현 함수 | `fsm_init(fsm_t *fsm, int timeout_limit)`, `fsm_update(...)`, `fsm_state_str(...)` |
| 핵심 개념·접근 | `timer`/`timeout_limit` 카운터. ACTIVE에서 event==0이면 `timer++`, 한계 시 TIMEOUT; activate면 `timer=0` 리셋·복구 |
| 난이도 | Medium |

---

## 5. 데이터 처리 (체크섬 · CRC · 압축 · 인코딩)

### `86-95-dataProcess.c` — 임베디드 데이터 처리 함수 모음
체크섬·CRC·패킷 파싱·고정소수점·필터·RLE·Base64·패턴 검색 등 펌웨어 데이터 처리 함수. (TODO 스텁)

| # | 함수 | 설명 | 핵심 개념·접근 | 난이도 |
|---|------|------|----------------|--------|
| 86 | `void fsm_handle_timeout(void)` | 타임아웃 이벤트 처리(축약형) | 84/85 FSM 타임아웃 로직 축약 | Easy |
| 87 | `uint8_t xor_checksum(const uint8_t *data, size_t len)` | 버퍼 XOR 체크섬 | 누산기 `cs ^= data[i]` — 통신 무결성 기초 | Easy |
| 88 | `uint8_t sum8_checksum(const uint8_t *data, size_t len)` | 8비트 가산 체크섬 | `sum += data[i]`, uint8 오버플로 자동 mod 256 | Easy |
| 89 | `uint16_t crc16_lut(const uint8_t *data, size_t len)` | LUT 기반 CRC-16 | 256엔트리 LUT, `crc=(crc>>8)^table[(crc^byte)&0xFF]` | Hard |
| 90 | `bool parse_packet(const uint8_t *data, size_t len)` | 헤더(0xAA)·푸터(0xBB) 패킷 검증 | SOF/EOF 마커·길이 검증(프레이밍) | Easy |
| 91 | `int32_t fixed_add(...)` / `fixed_mul(...)` | Q포맷 고정소수점 연산 | 곱셈은 `(int64_t)a*b >> q` 스케일 보정 | Medium |
| 92 | `float moving_average(const float *data, size_t len, size_t window)` | 이동평균 필터 | 윈도우 합/`window`, 슬라이딩 누적합 | Easy |
| 93 | `size_t rle_compress(...)` / `rle_decompress(...)` | RLE 압축/복원 | (값,런길이) 페어, 카운트 255 상한·경계 검사 | Medium |
| 94 | `size_t base64_encode(...)` / `base64_decode(...)` | Base64 인코딩/디코딩 | 3바이트→4문자(6비트), 64자 알파벳·패딩(=) | Hard |
| 95 | `ssize_t search_pattern(const uint8_t *data, size_t len, const uint8_t *pattern, size_t pat_len)` | 바이트 패턴 첫 위치 검색 | 슬라이딩 `memcmp`, 미발견 시 -1 | Medium |

> 96번(NMEA GPS sentence 파서)은 빈 항목으로만 표시됨(시그니처/구현 없음).

---

## 부록 — 개념 분포

- **비트 조작**: set/clear/toggle/test, popcount, 회전, 엔디안, 비트필드, 그레이 코드 (1–25)
- **포인터·문자열·메모리**: libc 재구현(strlen~sprintf, memcpy~memmove), 정적 할당기 (26–55)
- **자료구조**: 원형 버퍼, 단일/이중/무malloc 연결리스트, Floyd 사이클·병합 (56–75)
- **FSM 패턴**: switch-case, 함수포인터 테이블, 디바운싱, 패킷 파서, entry/exit·guard·event queue·timeout (76–85)
- **데이터 처리**: 체크섬·CRC, 고정소수점, 필터, RLE·Base64, 패턴 검색 (86–95)

> 자동 채점이 되는 건 basicOps·dataManip(`[PASS]/[FAIL]`)이고, 나머지는 출력 비교(수동)입니다. 함수 단위 자동 채점으로 쪼개길 원하면 별도 작업 필요.
