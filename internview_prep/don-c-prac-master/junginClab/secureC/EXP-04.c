#if 0
// 위험한 코드
#include <stdio.h>

int str_len(const char* s) {
    char* p = s;
    *p = 'a';
    while (*p)
        ++p;
    return p - s;
}

int main() {
    char str[] = "hello";
    printf("%d\n", str_len(str));
    printf("%s\n", str);

    return 0;
}
#endif

#if 1
// 안전한 코드
#include <stdio.h>

int str_len(const char* s) {
    const char* p = s;
    //*p = 'a';
    while (*p)
        ++p;
    return p - s;
}

int main() {
    char str[] = "hello";
    printf("%d\n", str_len(str));
    printf("%s\n", str);

    return 0;
}
#endif
