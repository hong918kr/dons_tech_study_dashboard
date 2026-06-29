#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

/*
26. size_t my_strlen(const char *s)
*/
size_t my_strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

/*
27. char* my_strcpy(char *dest, const char *src)
*/
char* my_strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++) != '\0') {
        ;
    }
    return dest;
}

/*
28. char* my_strncpy(char *dest, const char *src, size_t n)
*/
char* my_strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

/*
29. char* my_strcat(char *dest, const char *src)
*/
char* my_strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d != '\0') {
        d++;
    }
    while ((*d++ = *src++) != '\0') {
        ;
    }
    return dest;
}

/*
30. int my_strcmp(const char *s1, const char *s2)
*/
int my_strcmp(const char *s1, const char *s2) {
    while (*s1 != '\0' && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

/*
31. char* my_strstr(const char *haystack, const char *needle)
*/
char* my_strstr(const char *haystack, const char *needle) {
    if (*needle == '\0') {
        return (char *)haystack;
    }
    for (; *haystack != '\0'; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h != '\0' && *n != '\0' && *h == *n) {
            h++;
            n++;
        }
        if (*n == '\0') {
            return (char *)haystack;
        }
    }
    return NULL;
}

/*
32. char* my_strchr(const char *s, int c)
*/
char* my_strchr(const char *s, int c) {
    char ch = (char)c;
    while (*s != '\0') {
        if (*s == ch) {
            return (char *)s;
        }
        s++;
    }
    if (ch == '\0') {
        return (char *)s;
    }
    return NULL;
}

/*
33. int my_atoi(const char *str)
*/
int my_atoi(const char *str) {
    int result = 0;
    int sign = 1;

    while (*str == ' ' || *str == '\t' || *str == '\n' ||
           *str == '\r' || *str == '\f' || *str == '\v') {
        str++;
    }

    if (*str == '+' || *str == '-') {
        if (*str == '-') {
            sign = -1;
        }
        str++;
    }

    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }

    return sign * result;
}

/*
34. char* my_itoa(int n, char *s, int base)
*/
char* my_itoa(int n, char *s, int base) {
    char tmp[34];
    int i = 0;
    int j = 0;
    bool negative = false;
    unsigned int value;

    if (base < 2 || base > 16) {
        s[0] = '\0';
        return s;
    }

    if (n < 0 && base == 10) {
        negative = true;
        value = (unsigned int)(-(long)n);
    } else {
        value = (unsigned int)n;
    }

    if (value == 0) {
        tmp[i++] = '0';
    }

    while (value != 0) {
        unsigned int digit = value % (unsigned int)base;
        tmp[i++] = (digit < 10) ? (char)('0' + digit) : (char)('a' + digit - 10);
        value /= (unsigned int)base;
    }

    if (negative) {
        s[j++] = '-';
    }

    while (i > 0) {
        s[j++] = tmp[--i];
    }
    s[j] = '\0';

    return s;
}

/*
35. str_to_lower / str_to_upper
*/
void str_to_lower(char *s) {
    while (*s != '\0') {
        if (*s >= 'A' && *s <= 'Z') {
            *s = (char)(*s + ('a' - 'A'));
        }
        s++;
    }
}

void str_to_upper(char *s) {
    while (*s != '\0') {
        if (*s >= 'a' && *s <= 'z') {
            *s = (char)(*s - ('a' - 'A'));
        }
        s++;
    }
}

/*
36. reverse_words
*/
static void reverse_range(char *s, int start, int end) {
    while (start < end) {
        char t = s[start];
        s[start] = s[end];
        s[end] = t;
        start++;
        end--;
    }
}

void reverse_words(char *s) {
    int len = (int)my_strlen(s);
    int i = 0;

    reverse_range(s, 0, len - 1);

    while (i < len) {
        while (i < len && s[i] == ' ') {
            i++;
        }
        int wstart = i;
        while (i < len && s[i] != ' ') {
            i++;
        }
        reverse_range(s, wstart, i - 1);
    }
}

/*
37. remove_duplicates
*/
void remove_duplicates(char *s) {
    bool seen[256] = {false};
    int w = 0;
    for (int r = 0; s[r] != '\0'; r++) {
        unsigned char c = (unsigned char)s[r];
        if (!seen[c]) {
            seen[c] = true;
            s[w++] = s[r];
        }
    }
    s[w] = '\0';
}

/*
38. is_palindrome
*/
bool is_palindrome(const char *s) {
    int left = 0;
    int right = (int)my_strlen(s) - 1;
    while (left < right) {
        if (s[left] != s[right]) {
            return false;
        }
        left++;
        right--;
    }
    return true;
}

/*
39. my_strtok
*/
static bool is_delim(char c, const char *delim) {
    for (const char *d = delim; *d != '\0'; d++) {
        if (c == *d) {
            return true;
        }
    }
    return false;
}

char* my_strtok(char *str, const char *delim) {
    static char *saved = NULL;
    char *start;

    if (str != NULL) {
        saved = str;
    }
    if (saved == NULL) {
        return NULL;
    }

    while (*saved != '\0' && is_delim(*saved, delim)) {
        saved++;
    }

    if (*saved == '\0') {
        saved = NULL;
        return NULL;
    }

    start = saved;

    while (*saved != '\0' && !is_delim(*saved, delim)) {
        saved++;
    }

    if (*saved != '\0') {
        *saved = '\0';
        saved++;
    } else {
        saved = NULL;
    }

    return start;
}

/*
40. my_sprintf (handles %d, %s, %c, %%)
*/
int my_sprintf(char *str, const char *format, ...) {
    va_list args;
    char *out = str;
    char numbuf[34];

    va_start(args, format);

    for (const char *f = format; *f != '\0'; f++) {
        if (*f != '%') {
            *out++ = *f;
            continue;
        }

        f++;
        switch (*f) {
            case 'd': {
                int n = va_arg(args, int);
                my_itoa(n, numbuf, 10);
                for (char *p = numbuf; *p != '\0'; p++) {
                    *out++ = *p;
                }
                break;
            }
            case 's': {
                const char *sarg = va_arg(args, const char *);
                while (*sarg != '\0') {
                    *out++ = *sarg++;
                }
                break;
            }
            case 'c': {
                int c = va_arg(args, int);
                *out++ = (char)c;
                break;
            }
            case '%': {
                *out++ = '%';
                break;
            }
            case '\0': {
                f--;
                break;
            }
            default: {
                *out++ = '%';
                *out++ = *f;
                break;
            }
        }
    }

    va_end(args);
    *out = '\0';
    return (int)(out - str);
}
