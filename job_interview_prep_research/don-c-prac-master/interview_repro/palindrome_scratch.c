#include <stdio.h>
#include <string.h>
#define false (0)
#define true (1)

int isStringPalindrome(const char *str) {
    
    if (str == NULL)
    {
        return false;
    }
    int len = strlen(str);
    if (len == 0)
    {
        return true;
    }
    int i = 0;
    int j = len - 1;
    while (i < j)
    {
        
        if (str[i] != str[j])
        {
            return false;
        }
        ++i;
        --j;
    }
    return true;
}

int isIntegerPalindrome(int num)
{
    
}



int main() {
    // String Palindrome Tests
    char str1[] = "racecar";
    char str2[] = "hello";
    char str3[] = "";
    char* str4 = NULL;
    printf("String Palindrome Tests:\n");
    printf("\"%s\" is a palindrome: %s\n", str1, isStringPalindrome(str1) ? "true" : "false");
    printf("\"%s\" is a palindrome: %s\n", str2, isStringPalindrome(str2) ? "true" : "false");
    printf("\"%s\" is a palindrome: %s\n", str3, isStringPalindrome(str3) ? "true" : "false");
    printf("NULL string is a palindrome: %s\n", isStringPalindrome(str4) ? "true" : "false");


    // Integer Palindrome Tests
    int num1 = 121;
    int num2 = 123;
    int num3 = 12321;
    int num4 = -121;
    int num5 = 5;

    printf("\nInteger Palindrome Tests:\n");
    printf("%d is a palindrome: %s\n", num1, isIntegerPalindrome(num1) ? "true" : "false");
    printf("%d is a palindrome: %s\n", num2, isIntegerPalindrome(num2) ? "true" : "false");
    printf("%d is a palindrome: %s\n", num3, isIntegerPalindrome(num3) ? "true" : "false");
    printf("%d is a palindrome: %s\n", num4, isIntegerPalindrome(num4) ? "true" : "false");
    printf("%d is a palindrome: %s\n", num5, isIntegerPalindrome(num5) ? "true" : "false");

    return 0;
}