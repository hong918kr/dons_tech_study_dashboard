#include <stdio.h>
#include <limits.h>
#include <ctype.h>

int my_atoi(const char *str) {
    if (str == NULL) {
        return 0; // Or handle the error as needed
    }

    int result = 0;
    int sign = 1;
    int i = 0;

    // Skip leading whitespace
    while (isspace(str[i])) {
        i++;
    }

    // Check for sign
    if (str[i] == '+' || str[i] == '-') {
        sign = (str[i] == '-') ? -1 : 1;
        i++;
    }

    // Convert digits
    while (isdigit(str[i])) {
        int digit = str[i] - '0';

        // Check for overflow before multiplication
        if (result > INT_MAX / 10 || (result == INT_MAX / 10 && digit > INT_MAX % 10)) {
            return (sign == 1) ? INT_MAX : INT_MIN;
        }

        result = result * 10 + digit;
        i++;
    }

    return result * sign;
}

int main() {
    char str1[] = "42";
    char str2[] = "   -42";
    char str3[] = "4193 with words";
    char str4[] = "words and 987";
    char str5[] = "-91283472332"; // Test INT_MIN overflow
    char str6[] = "2147483648"; // Test INT_MAX overflow
    char str7[] = "+123";
    char str8[] = "   +0123";
    char str9[] = "  ";
    char str10[] = "";
    char str11[] = NULL;

    printf("'%s' -> %d\n", str1, my_atoi(str1));
    printf("'%s' -> %d\n", str2, my_atoi(str2));
    printf("'%s' -> %d\n", str3, my_atoi(str3));
    printf("'%s' -> %d\n", str4, my_atoi(str4));
    printf("'%s' -> %d\n", str5, my_atoi(str5));
    printf("'%s' -> %d\n", str6, my_atoi(str6));
    printf("'%s' -> %d\n", str7, my_atoi(str7));
    printf("'%s' -> %d\n", str8, my_atoi(str8));
    printf("'%s' -> %d\n", str9, my_atoi(str9));
    printf("'%s' -> %d\n", str10, my_atoi(str10));
    printf("'%s' -> %d\n", str11, my_atoi(str11));

    return 0;
}