
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

// 1. 주어진 범위 내의 모든 소수를 찾는 함수
// 결과는 primes 배열에 저장, 반환값은 소수의 개수
int find_primes(int start, int end, int* primes, int max_count);

// 2. 최대 두 개의 고유한 정수를 포함하는 가장 긴 하위 배열의 길이
int longest_subarray_two_unique(const int* arr, int n);

// =================== 테스트 코드 ===================
int main() {
    int pass_cnt = 0;

    // 1. 소수 찾기 테스트
    int primes[100];
    int cnt = find_primes(2, 10, primes, 100);
    int expected1[] = {2,3,5,7};
    bool ok = (cnt == 4);
    for (int i = 0; i < 4 && ok; ++i) if (primes[i] != expected1[i]) ok = false;
    if (ok) { printf("TC1 PASS\n"); pass_cnt++; } else { printf("TC1 FAIL\n"); }

    cnt = find_primes(10, 20, primes, 100);
    int expected2[] = {11,13,17,19};
    ok = (cnt == 4);
    for (int i = 0; i < 4 && ok; ++i) if (primes[i] != expected2[i]) ok = false;
    if (ok) { printf("TC2 PASS\n"); pass_cnt++; } else { printf("TC2 FAIL\n"); }

    cnt = find_primes(22, 29, primes, 100);
    int expected3[] = {23, 29};
    ok = (cnt == 2);
    for (int i = 0; i < 2 && ok; ++i) if (primes[i] != expected3[i]) ok = false;
    if (ok) { printf("TC3 PASS\n"); pass_cnt++; } else { printf("TC3 FAIL\n"); }

    // 2. 최대 두 개의 고유한 정수를 포함하는 가장 긴 하위 배열의 길이
    int arr1[] = {1,2,1,2,3};
    int len1 = longest_subarray_two_unique(arr1, 5);
    if (len1 == 4) { printf("TC4 PASS\n"); pass_cnt++; } else { printf("TC4 FAIL\n"); }

    int arr2[] = {1,2,3,2,2};
    int len2 = longest_subarray_two_unique(arr2, 5);
    if (len2 == 4) { printf("TC5 PASS\n"); pass_cnt++; } else { printf("TC5 FAIL\n"); }

    int arr3[] = {1,1,1,1};
    int len3 = longest_subarray_two_unique(arr3, 4);
    if (len3 == 4) { printf("TC6 PASS\n"); pass_cnt++; } else { printf("TC6 FAIL\n"); }

    int arr4[] = {1,2,3,4,5};
    int len4 = longest_subarray_two_unique(arr4, 5);
    if (len4 == 2) { printf("TC7 PASS\n"); pass_cnt++; } else { printf("TC7 FAIL\n"); }

    int arr5[] = {1,2,2,1,3,3,2,2,1};
    int len5 = longest_subarray_two_unique(arr5, 9);
    if (len5 == 5) { printf("TC8 PASS\n"); pass_cnt++; } else { printf("TC8 FAIL\n"); }

    printf("Total Passed: %d/8\n", pass_cnt);
    return 0;
}

// 함수 시그니처만, 구현은 직접 작성하세요.
int find_primes(int start, int end, int* primes, int max_count) { /* TODO */ return 0; }
int longest_subarray_two_unique(const int* arr, int n) { /* TODO */ return 0; }