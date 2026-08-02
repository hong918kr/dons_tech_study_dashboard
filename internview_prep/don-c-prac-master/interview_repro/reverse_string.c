#include <stdio.h>

void reverse(char* str) {
    char* end = str;
    char temp;
    
    while (*end != '\0') {
        printf("%c\n", *end);
        end++;
    }    
    --end;
    printf("endloop: *end: %c\n", *end);
    printf("str: %p | end: %p\n", str, end);
    printf("res: %d \n", str<end);
    
    while (str < end) {
        printf("*str : %c | *end : %c ", *str, *end);
        temp = *str;
        *str++ = *end;        
        *end-- = temp;
        
    }
}

int main ()
{
    char str1[6] = "12345";
    printf("origin  : %s\n", str1);
    reverse(str1);
    printf("reverse : %s\n", str1);
    return 0;
}