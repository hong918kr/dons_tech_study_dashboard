#include <stdio.h>

void reverse(char* str)
{
    char temp;
    char* end = str;
    while(*end != '\0')
    {
        end++;
    }
    --end;
    while(str < end)
    {
        temp = *str;
        *str++ = *end;
        *end-- = temp;
    }
}