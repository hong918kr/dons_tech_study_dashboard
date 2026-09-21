
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// 1. 두 정수 배열의 요소를 합산하는 함수
int sum_arrays(const int* arr1, int n1, const int* arr2, int n2);

// 2. strlen, strcpy 직접 구현
size_t my_strlen(const char* s);
char* my_strcpy(char* dest, const char* src);

// 3. 포인터를 사용하여 null로 끝나는 문자열을 제자리에서 뒤집는 함수
void reverse_inplace(char* str);

// =================== 테스트 코드 ===================
int main() {
    int pass_cnt = 0;

    // 1. sum_arrays 테스트
    int a1[] = {1,2,3}, a2[] = {4,5,6,7};
    int sum1 = sum_arrays(a1, 3, a2, 4);
    if (sum1 == 28) { printf("TC1 PASS\n"); pass_cnt++; } else { printf("TC1 FAIL\n"); }

    int b1[] = {-1,-2}, b2[] = {3,4};
    int sum2 = sum_arrays(b1, 2, b2, 2);
    if (sum2 == 4) { printf("TC2 PASS\n"); pass_cnt++; } else { printf("TC2 FAIL\n"); }

    // 2. my_strlen, my_strcpy 테스트
    char s1[] = "hello";
    if (my_strlen(s1) == 5) { printf("TC3 PASS\n"); pass_cnt++; } else { printf("TC3 FAIL\n"); }

    char buf[20];
    my_strcpy(buf, "world");
    if (strcmp(buf, "world") == 0) { printf("TC4 PASS\n"); pass_cnt++; } else { printf("TC4 FAIL\n"); }

    // 3. reverse_inplace 테스트
    char s2[] = "abcd";
    reverse_inplace(s2);
    if (strcmp(s2, "dcba") == 0) { printf("TC5 PASS\n"); pass_cnt++; } else { printf("TC5 FAIL\n"); }

    char s3[] = "a";
    reverse_inplace(s3);
    if (strcmp(s3, "a") == 0) { printf("TC6 PASS\n"); pass_cnt++; } else { printf("TC6 FAIL\n"); }

    char s4[] = "";
    reverse_inplace(s4);
    if (strcmp(s4, "") == 0) { printf("TC7 PASS\n"); pass_cnt++; } else { printf("TC7 FAIL\n"); }

    printf("Total Passed: %d/7\n", pass_cnt);
    return 0;
}

// 함수 시그니처만, 구현은 직접 작성하세요.
// int sum_arrays(const int* arr1, int n1, const int* arr2, int n2) { /* TODO */ return 0; }
// size_t my_strlen(const char* s) { /* TODO */ return 0; }
// char* my_strcpy(char* dest, const char* src) { /* TODO */ return dest; }
// void reverse_inplace(char* s) { /* TODO */ }

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// 1. 두 정수 배열의 요소를 합산하는 함수
int sum_arrays(const int* arr1, int n1, const int* arr2, int n2) {
    // TODO: 구현
    int sum = 0;
    int i=0, j=0;
    int total = n1 > n2 ? n1:n2;
    for (int k = 0; k < total; ++k)
    {
        if (i < n1) sum += arr1[i++];
        if (j < n2) sum += arr2[j++];
    }
    return sum;
}

// 2. strlen, strcpy 직접 구현
size_t my_strlen(const char* s) {
    // TODO: 구현
    size_t len = 0;
    const char* c = s;
    while (*c++) len++;
    return len;
}
char* my_strcpy(char* dest, const char* src) {
    // TODO: 구현
    while (*dest++ = *src++);
    return dest;
}

// 3. 포인터를 사용하여 null로 끝나는 문자열을 제자리에서 뒤집는 함수
void reverse_inplace(char* str) {
    // TODO: 구현
    if (!*str) return;
    char* s = str;
    char* e = s;
    while (*e) e++; e--;
    while (s < e)
    {
        char temp = *s;
        *s = *e;
        *e = temp;
        s++;e--;
    }
}