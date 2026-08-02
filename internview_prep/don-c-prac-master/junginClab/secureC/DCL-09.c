
#if 0
// restrict 한정자
#include <stdio.h>

void foo(int* a, int* b, int* c)
{
    *a += *c;
    *b += *c;
}

int main() {
    int a = 1;
    foo(&a, &a, &a);
    printf("%d\n", a);
    return 0;
}
#endif

#if 0
// restrict 한정자
#include <stdio.h>

void foo(int* a, int* b, int* c)
{
    *a += *c;
    *b += *c;
}

int main() {
    int a = 1, b = 1, c = 1;
    foo(&a, &b, &c);
    printf("%d\n", a);
    return 0;
}
#endif

#if 0
// restrict 한정자
#include <stdio.h>

void foo(int* restrict a, int* restrict b, int* restrict c)
{
    *a += *c;
    *b += *c;
}

int main() {
    int a = 1, b = 1, c = 1;
    foo(&a, &b, &c);
    printf("%d\n", a);
    return 0;
}
#endif

#if 0
// restrict 한정자
#include <stdio.h>

void foo(int* restrict a, int* restrict b, int* restrict c)
{
    *a += *c;
    *b += *c;
}

int main() {
    int a = 1;
    foo(&a, &a, &a);
    printf("%d\n", a);
    return 0;
}
#endif

#if 0
// 위험한 코드
#include <stdio.h>
#include <string.h>

void print_arr(int* arr, int len) {
    for (int i = 0; i < len; i++)
        printf("%d ", arr[i]);
    getchar();
}

int main() {
    int cnt = 10;
    int arr[10] = { 0,1,2,3,4,5,6,7,8,9 };
    print_arr(arr, cnt);

    --cnt;
    memcpy(arr, arr + 1, sizeof(int) * cnt);
    print_arr(arr, cnt);

    return 0;
}
#endif



#if 0
// 해결 방법 1.
#include <stdio.h>
#include <string.h>

void print_arr(int* arr, int len) {
    for (int i = 0; i < len; i++)
        printf("%d ", arr[i]);
    getchar();
}

int main() {
    int cnt = 10;
    int arr[10] = { 0,1,2,3,4,5,6,7,8,9 };
    print_arr(arr, cnt);

    arr[0] = arr[--cnt];
    print_arr(arr, cnt);

    return 0;
}

#endif





#if 0
// 해결 방법 2.
#include <stdio.h>
#include <string.h>

void print_arr(int* arr, int len) {
    for (int i = 0; i < len; i++)
        printf("%d ", arr[i]);
    getchar();
}

int main() {
    int cnt = 10;
    int arr[10] = { 0,1,2,3,4,5,6,7,8,9 };
    print_arr(arr, cnt);

    --cnt;
    memmove(arr, arr + 1, sizeof(int) * cnt);
    print_arr(arr, cnt);

    return 0;
}


#endif