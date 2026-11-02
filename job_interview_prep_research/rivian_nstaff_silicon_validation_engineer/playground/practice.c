char* my_strcpy(char* dst, char* src)
{
    char* d = dst;
    while ((*d++ = *src++) != '\0');
    return dst;
}

void *my_memcpy(void* dst, const void* src,  size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
    {
        *d++ = *s++;
    }
    return dst;
}