
#if 0
// 위험한 코드
#include <stdio.h>
#include <stdarg.h>

enum { VA_END = -1 };
double average(int first, ...) {
    int sum = 0;
    int cnt = 0;

    va_list args;
    va_start(args, first);

    int i = first;
    while (i != VA_END) {
        sum += i;
        ++cnt;
        i = va_arg(args, int);
    }
    va_end(args);

    return cnt ? sum / (double)cnt : 0;
}

int main() {
    double avg = average(1, 2, 3, 4);
    printf("%lf\n", avg);

    return 0;
}
#endif

#if 0
// _INTSIZEOF 테스트 코드
#include <stdio.h>

#define _INTSIZEOF(n) ((sizeof(n) + sizeof(int) - 1) & ~(sizeof(int) - 1))

#pragma  pack(1)
typedef struct
{
    int i;
    char ch;
} ST;


int main() {
    int first;
    char ch;
    short s;
    printf("%d\n", _INTSIZEOF(first));
    printf("%d\n", _INTSIZEOF(ch));
    printf("%d\n", _INTSIZEOF(s));
    printf("%d\n", _INTSIZEOF(ST));

    return 0;
}
#endif



#if 0
// 위험한 코드
#include <stdio.h>
#include <stdarg.h>

enum { VA_END = -1 };
double average(int first, ...) {
    int sum = 0;
    int cnt = 0;

    va_list args;
    va_start(args, first);

    int i = first;
    while (i != VA_END) {
        sum += i;
        ++cnt;
        i = va_arg(args, int);
    }
    va_end(args);

    return cnt ? sum / (double)cnt : 0;
}

int main() {
    double avg = average(1, 2, 3, 4, VA_END);
    printf("%lf\n", avg);

    return 0;
}
#endif

#if 1
// 해결 방법 2.
#include <stdio.h>
#include <stdarg.h>

double average(int cnt, ...) {
    if (cnt == 0)
        return 0;

    va_list args;
    va_start(args, cnt);

    int sum = 0;
    for (int i = 0; i < cnt; i++)
        sum += va_arg(args, int);
    va_end(args);

    return sum / (double)cnt;
}

int main() {
    double avg = average(4, 1, 2, 3, 4);
    printf("%lf\n", avg);

    return 0;
}

#endif
