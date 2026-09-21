/*
Applied Intuition 코딩 챌린지 대비 50제

카테고리 5: 기본 알고리즘 및 논리 (Basic Algorithms & Logic)

36. 정수 배열에서 유일한 숫자 찾기 (나머지는 2번씩)
    - 설명: 배열에서 단 하나만 존재하는 숫자를 찾으세요. (나머지는 2번씩 등장)
    - Follow-up: 추가 메모리 없이 O(n) 풀이(XOR)

37. 정렬된 배열에서 특정 값의 삽입 위치 찾기
    - 설명: 정렬된 배열에서 x를 삽입할 인덱스를 반환하세요.
    - Follow-up: 이진 탐색(Binary Search)로 효율화

38. 이동 평균 필터(Moving Average Filter) 구현
    - 설명: 최근 N개의 값의 평균을 반환하는 필터
    - Follow-up: 효율적 이동 평균 계산법(누적합 활용)

39. C 문자열을 제자리에서 뒤집는 함수 void reverse_string(char* str)
    - 설명: 문자열을 in-place로 뒤집으세요.
    - Follow-up: 재귀적으로도 구현 가능

40. 피보나치 수열 n번째 항 계산 함수
    - 설명: n번째 피보나치 수를 반환하세요.
    - Follow-up: 재귀의 비효율성, DP/반복문 개선

41. 버튼 채터링 방지 디바운스 로직 구현
    - 설명: 버튼 입력이 연속적으로 들어올 때 디바운싱 처리
    - Follow-up: 폴링 메인 루프에서 일정 시간 변화 없을 때만 입력 인정

42. 8비트 곱셈(곱셈자 * 사용 금지, 16비트 결과)
    - 설명: 곱셈 연산자 없이 두 8비트 정수 곱하기
    - Follow-up: 하드웨어 곱셈기보다 느린 이유

43. 런-렝스 인코딩(RLE) 압축 함수
    - 설명: 연속된 문자를 [문자][개수]로 압축
    - Follow-up: 압축 결과가 원본보다 길면 원본 반환

44. main() 함수 호출 전 실행되는 코드와 역할
    - 설명: .data/.bss 세그먼트 초기화, C 런타임 환경 준비
    - Follow-up: .data는 초기값 복사, .bss는 0으로 초기화

45. ISR에서 printf 호출이 위험한 이유
    - 설명: ISR은 짧고 빠르게, printf는 블로킹/지연 발생
    - Follow-up: ISR에서 긴 함수 호출은 실시간성 저하

46. 두 정렬된 배열을 하나로 병합하는 함수
    - 설명: 두 정렬된 배열을 하나로 합쳐 정렬된 배열 생성
    - Follow-up: 병합 정렬(Merge Sort) 시간 복잡도는 O(n)

47. 팩토리얼 재귀 함수
    - 설명: n!을 재귀적으로 계산
    - Follow-up: 입력이 크면 스택 오버플로우 위험

48. 문자열이 회문(palindrome)인지 확인하는 함수
    - 설명: 대소문자 무시, 공백 건너뛰기
    - Follow-up: isalnum, tolower로 전처리

49. 배열 요소를 오른쪽으로 한 칸 회전 함수
    - 설명: 배열을 오른쪽으로 한 칸 회전
    - Follow-up: k칸 회전은 reverse 3번으로 O(n) 구현 가능

50. CRC-8 체크섬 계산 함수
    - 설명: CRC-8 알고리즘으로 체크섬 계산
    - Follow-up: CRC는 sum/XOR보다 에러 검출력이 높음

*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

// 36. 정수 배열에서 유일한 숫자 찾기 (나머지는 2번씩)
int find_unique(const int* arr, int n) {}

// 37. 정렬된 배열에서 특정 값의 삽입 위치 찾기
int find_insert_pos(const int* arr, int n, int x) {}

// 38. 이동 평균 필터(Moving Average Filter) 구조체 및 함수 원형
typedef struct {
    int buf[4];
    int size, idx, count, sum;
} MovingAvg;
void ma_init(MovingAvg* ma, int size) {}
int ma_update(MovingAvg* ma, int val) {}

// 39. C 문자열을 제자리에서 뒤집는 함수
void reverse_string(char* str) {}
void rec_reverse_string(char* str, int l, int r) {}

// 40. 피보나치 수열 n번째 항 계산 함수
int fib(int n) {}

// 41. 버튼 채터링 방지 디바운스 로직 구조체 및 함수 원형
typedef struct {
    int stable, last, cnt, threshold;
} Debounce;
void debounce_init(Debounce* d, int threshold) {}
int debounce_update(Debounce* d, int val) {}

// 42. 8비트 곱셈(곱셈자 * 사용 금지, 16비트 결과)
uint16_t mul8(uint8_t a, uint8_t b) {}

// 43. 런-렝스 인코딩(RLE) 압축 함수
int rle_encode(const char* src, char* dst, int maxlen) {}

// 44. main() 함수 호출 전 실행되는 코드와 역할
void explain_startup_code(void) {}

// 45. ISR에서 printf 호출이 위험한 이유
void explain_isr_printf(void) {}

// 46. 두 정렬된 배열을 하나로 병합하는 함수
void merge_sorted(const int* a, int na, const int* b, int nb, int* out) {}

// 47. 팩토리얼 재귀 함수
int factorial(int n) {}

// 48. 문자열이 회문(palindrome)인지 확인하는 함수
int is_palindrome(const char* s) {}

// 49. 배열 요소를 오른쪽으로 한 칸 회전 함수
void rotate_right(int* arr, int n) {}
void reverse(int* arr, int l, int r) {}
void rotate_k(int* arr, int n, int k) {}

// 50. CRC-8 체크섬 계산 함수
uint8_t crc8(const uint8_t* data, int len) {}

#define TEST_LABEL_WIDTH 45
void test_result(const char* label, int ok) {
    printf("%-*s %s\n", TEST_LABEL_WIDTH, label, ok ? "[PASS]" : "[FAIL]");
}

int main(void) {
    // 테스트 코드는 남겨둡니다.
    return 0;
}