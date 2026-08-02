/*
Applied Intuition 코딩 챌린지 대비 50제

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

/*
카테고리 5: 기본 알고리즘 및 논리 (Basic Algorithms & Logic)
각 문제는 함수 시그니처, 설명, 예제 변수, follow-up 답변, 테스트 코드(PASS/FAIL)를 포함합니다.
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

// 36. 정수 배열에서 유일한 숫자 찾기 (나머지는 2번씩)
// 설명: 배열에서 단 하나만 존재하는 숫자를 찾으세요. (나머지는 2번씩 등장)
// Follow-up: 추가 메모리 없이 O(n) 풀이(XOR)
int find_unique(const int* arr, int n) {
    int res = 0;
    for (int i = 0; i < n; ++i) res ^= arr[i];
    return res;
}
// Follow-up 답변: XOR은 짝수 번 등장하는 수를 모두 상쇄시켜 유일한 수만 남김

// 37. 정렬된 배열에서 특정 값의 삽입 위치 찾기
// 설명: 정렬된 배열에서 x를 삽입할 인덱스를 반환하세요.
// Follow-up: 이진 탐색(Binary Search)로 효율화
int find_insert_pos(const int* arr, int n, int x) {
    int l = 0, r = n;
    while (l < r) {
        int m = (l + r) / 2;
        if (arr[m] < x) l = m + 1;
        else r = m;
    }
    return l;
}
// Follow-up 답변: 이진 탐색은 O(log n)으로 빠르게 위치를 찾음

// 38. 이동 평균 필터(Moving Average Filter) 구현
// 설명: 최근 N개의 값의 평균을 반환하는 필터
// Follow-up: 누적합을 이용하면 효율적으로 구현 가능
typedef struct {
    int buf[4];
    int size, idx, count, sum;
} MovingAvg;
void ma_init(MovingAvg* ma, int size) {
    memset(ma, 0, sizeof(*ma));
    ma->size = size;
}
int ma_update(MovingAvg* ma, int val) {
    if (ma->count < ma->size) {
        ma->sum += val;
        ma->buf[ma->idx++] = val;
        ma->count++;
        if (ma->idx == ma->size) ma->idx = 0;
        return ma->sum / ma->count;
    } else {
        ma->sum -= ma->buf[ma->idx];
        ma->sum += val;
        ma->buf[ma->idx++] = val;
        if (ma->idx == ma->size) ma->idx = 0;
        return ma->sum / ma->size;
    }
}
// Follow-up 답변: 누적합(sum)에서 빠지는 값만 빼고 새 값만 더하면 O(1)로 평균 계산

// 39. C 문자열을 제자리에서 뒤집는 함수
// 설명: 문자열을 in-place로 뒤집으세요.
// Follow-up: 재귀적으로도 구현 가능
void reverse_string(char* str) {
    int n = strlen(str);
    for (int i = 0; i < n/2; ++i) {
        char t = str[i];
        str[i] = str[n-1-i];
        str[n-1-i] = t;
    }
}
// Follow-up 답변: void rec_rev(char* s, int l, int r) { if(l<r){swap; rec_rev(s,l+1,r-1);} }

void rec_reverse_string(char* str, int l, int r) {
    if (l < r) {
        char t = str[l]; str[l] = str[r]; str[r] = t;
        rec_reverse_string(str, l+1, r-1);
    }
}

// 40. 피보나치 수열 n번째 항 계산 함수
// 설명: n번째 피보나치 수를 반환하세요.
// Follow-up: 재귀는 비효율적, 반복문/DP가 빠름
int fib(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1, c;
    for (int i = 2; i <= n; ++i) {
        c = a + b; a = b; b = c;
    }
    return b;
}
// Follow-up 답변: 재귀는 중복 계산 많아 느림, 반복문/메모이제이션이 효율적

// 41. 버튼 채터링 방지 디바운스 로직 구현
// 설명: 버튼 입력이 연속적으로 들어올 때 디바운싱 처리
// Follow-up: 폴링 메인 루프에서 일정 시간 변화 없을 때만 입력 인정
typedef struct {
    int stable, last, cnt, threshold;
} Debounce;
void debounce_init(Debounce* d, int threshold) {
    d->stable = d->last = d->cnt = 0; d->threshold = threshold;
}
int debounce_update(Debounce* d, int val) {
    if (val == d->last) d->cnt++;
    else { d->cnt = 1; d->last = val; }
    if (d->cnt >= d->threshold && d->stable != val) {
        d->stable = val;
        return 1; // 상태 변화 감지
    }
    return 0;
}
// Follow-up 답변: 폴링 루프에서 debounce_update를 주기적으로 호출

// 42. 8비트 곱셈(곱셈자 * 사용 금지, 16비트 결과)
// 설명: 곱셈 연산자 없이 두 8비트 정수 곱하기
// Follow-up: 하드웨어 곱셈기가 없으면 느림
uint16_t mul8(uint8_t a, uint8_t b) {
    uint16_t res = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & (1 << i)) res += (a << i);
    }
    return res;
}
// Follow-up 답변: 소프트웨어 곱셈은 반복문 필요, HW 곱셈기보다 느림

// 43. 런-렝스 인코딩(RLE) 압축 함수
// 설명: 연속된 문자를 [문자][개수]로 압축
// Follow-up: 압축 결과가 원본보다 길면 원본 반환
int rle_encode(const char* src, char* dst, int maxlen) {
    int n = strlen(src), j = 0;
    for (int i = 0; i < n;) {
        char c = src[i];
        int cnt = 1;
        while (i + cnt < n && src[i + cnt] == c) cnt++;
        if (j + 2 > maxlen) return -1;
        dst[j++] = c;
        dst[j++] = '0' + cnt;
        i += cnt;
    }
    dst[j] = 0;
    if (j >= n) { strcpy(dst, src); return 0; }
    return 1;
}
// Follow-up 답변: 압축 결과가 원본보다 길면 원본 반환

// 44. main() 함수 호출 전 실행되는 코드와 역할
// 설명: .data/.bss 세그먼트 초기화, C 런타임 환경 준비
// Follow-up: .data는 초기값 복사, .bss는 0으로 초기화
void explain_startup_code(void) {
    printf("44. Startup: .data 복사, .bss 0, 라이브러리 초기화, main() 호출\n");
}
// Follow-up 답변: .data는 ROM→RAM 복사, .bss는 0, main() 전 준비

// 45. ISR에서 printf 호출이 위험한 이유
// 설명: ISR은 짧고 빠르게, printf는 블로킹/지연 발생
// Follow-up: ISR에서 긴 함수 호출은 실시간성 저하
void explain_isr_printf(void) {
    printf("45. ISR에서 printf는 지연/블로킹 위험, ISR은 짧게!\n");
}
// Follow-up 답변: printf는 블로킹, ISR은 빠르게 끝내야 함

// 46. 두 정렬된 배열을 하나로 병합하는 함수
// 설명: 두 정렬된 배열을 하나로 합쳐 정렬된 배열 생성
// Follow-up: 병합 정렬(Merge Sort) 시간 복잡도는 O(n)
void merge_sorted(const int* a, int na, const int* b, int nb, int* out) {
    int i = 0, j = 0, k = 0;
    while (i < na && j < nb) {
        if (a[i] < b[j]) out[k++] = a[i++];
        else out[k++] = b[j++];
    }
    while (i < na) out[k++] = a[i++];
    while (j < nb) out[k++] = b[j++];
}
// Follow-up 답변: 병합 정렬의 merge 단계는 O(n)

// 47. 팩토리얼 재귀 함수
// 설명: n!을 재귀적으로 계산
// Follow-up: 입력이 크면 스택 오버플로우 위험
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n-1);
}
// Follow-up 답변: n이 크면 스택 오버플로우, 반복문 사용 권장

// 48. 문자열이 회문(palindrome)인지 확인하는 함수
// 설명: 대소문자 무시, 공백 건너뛰기
// Follow-up: isalnum, tolower로 전처리
int is_palindrome(const char* s) {
    int l = 0, r = strlen(s) - 1;
    while (l < r) {
        while (l < r && !isalnum((unsigned char)s[l])) l++;
        while (l < r && !isalnum((unsigned char)s[r])) r--;
        if (tolower((unsigned char)s[l]) != tolower((unsigned char)s[r])) return 0;
        l++; r--;
    }
    return 1;
}
// Follow-up 답변: isalnum, tolower로 전처리하면 공백/대소문자 무시 가능

// 49. 배열 요소를 오른쪽으로 한 칸 회전 함수
// 설명: 배열을 오른쪽으로 한 칸 회전
// Follow-up: k칸 회전은 reverse 3번으로 O(n) 구현 가능
void rotate_right(int* arr, int n) {
    if (n <= 1) return;
    int last = arr[n-1];
    for (int i = n-1; i > 0; --i) arr[i] = arr[i-1];
    arr[0] = last;
}
void reverse(int* arr, int l, int r) {
    while (l < r) {
        int t = arr[l]; arr[l] = arr[r]; arr[r] = t;
        l++; r--;
    }
}
void rotate_k(int* arr, int n, int k) {
    k = k % n;
    reverse(arr, 0, n-1);
    reverse(arr, 0, k-1);
    reverse(arr, k, n-1);
}
// Follow-up 답변: k칸 회전은 reverse 3번으로 O(n) 가능

// 50. CRC-8 체크섬 계산 함수
// 설명: CRC-8 알고리즘으로 체크섬 계산
// Follow-up: CRC는 sum/XOR보다 에러 검출력이 높음
uint8_t crc8(const uint8_t* data, int len) {
    uint8_t crc = 0;
    for (int i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}
// Follow-up 답변: CRC는 다항식 기반, 연속 에러 검출에 강함

// ------------------- 테스트 코드 -------------------
#define TEST_LABEL_WIDTH 45
void test_result(const char* label, int ok) {
    printf("%-*s %s\n", TEST_LABEL_WIDTH, label, ok ? "[PASS]" : "[FAIL]");
}

int main(void) {
    // 36. 유일한 숫자 찾기
    int arr1[] = {2, 3, 2, 4, 3};
    test_result("36. find_unique", find_unique(arr1, 5) == 4);

    // 37. 삽입 위치 찾기
    int arr2[] = {1, 3, 5, 7};
    test_result("37. find_insert_pos 4", find_insert_pos(arr2, 4, 4) == 2);

    // 38. 이동 평균 필터
    MovingAvg ma; ma_init(&ma, 4);
    int avg1 = ma_update(&ma, 10);
    int avg2 = ma_update(&ma, 20);
    int avg3 = ma_update(&ma, 30);
    int avg4 = ma_update(&ma, 40);
    int avg5 = ma_update(&ma, 50);
    test_result("38. moving avg", avg1 == 10 && avg4 == 25 && avg5 == 35);

    // 39. 문자열 뒤집기
    char s1[] = "hello";
    reverse_string(s1);
    test_result("39. reverse_string", strcmp(s1, "olleh") == 0);
    char s2[] = "world";
    rec_reverse_string(s2, 0, strlen(s2)-1);
    test_result("39. rec_reverse_string", strcmp(s2, "dlrow") == 0);

    // 40. 피보나치
    test_result("40. fib(10)", fib(10) == 55);

    // 41. 디바운스
    Debounce d; debounce_init(&d, 3);
    int changed = 0;
    for (int i = 0; i < 5; ++i) changed += debounce_update(&d, 1);
    test_result("41. debounce", changed == 1);

    // 42. 8비트 곱셈
    test_result("42. mul8", mul8(13, 7) == 91);

    // 43. RLE
    char rle_dst[16];
    int rle_res = rle_encode("aaabb", rle_dst, 16);
    test_result("43. rle_encode", rle_res == 1 && strcmp(rle_dst, "a3b2") == 0);

    // 44. startup code
    explain_startup_code();
    test_result("44. explain_startup_code (manual check)", 1);

    // 45. ISR printf
    explain_isr_printf();
    test_result("45. explain_isr_printf (manual check)", 1);

    // 46. 배열 병합
    int a[] = {1,3,5}, b[] = {2,4,6}, out[6];
    merge_sorted(a, 3, b, 3, out);
    test_result("46. merge_sorted", out[0]==1 && out[1]==2 && out[5]==6);

    // 47. 팩토리얼
    test_result("47. factorial(5)", factorial(5) == 120);

    // 48. 회문
    test_result("48. is_palindrome", is_palindrome("A man, a plan, a canal: Panama") == 1);

    // 49. 배열 회전
    int arr3[] = {1,2,3,4,5};
    rotate_right(arr3, 5);
    test_result("49. rotate_right", arr3[0]==5 && arr3[1]==1);
    int arr4[] = {1,2,3,4,5};
    rotate_k(arr4, 5, 2);
    test_result("49. rotate_k", arr4[0]==4 && arr4[1]==5 && arr4[2]==1);

    // 50. CRC-8
    uint8_t crc_data[] = {0x12, 0x34, 0x56};
    test_result("50. crc8", crc8(crc_data, 3) == 0xF4);

    return 0;
}