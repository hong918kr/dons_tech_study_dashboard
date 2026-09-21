while(*s++ = *t++); 이게 인터뷰 문제로 나왔어, 아주 고전적인 c 문제인것같은데, 여기서보면, 간단한 strcpy 를 이야기하는거고 여기서 = -> == 로 되면 strcmp이 되기도해, 이 문제의 요구사항은, 

1) s와 t가 문자열인가를 물어봤는가? C문법인걸 물어봤는가? 
2) 연산자 우선순위, 및 포인터 역참조에 대해서 잘 알고있는가 평가?
3) edge case어떻게 해결 예를들면, s = "123" , t = "789a" 이런경우?
4) performance 늘릴수있는방법, s,t가 처음에 문자열 pointer이지만, int pointer로 하게되면 속도 4배 증가, 아니면 do while loop을사용해서 do앞에 더 많은 비교를 붙혀도되고, 나는 XOR연산으로 strcmp 를 풀려고 시도를했어


이런 비슷한 문제들을 연구해서 한10개 정도 나열해주고, 요구사항등도 함께 붙혀줘



----------------
1) my_strcpy: pointer-copy with sentinel  
- 스토리: 간단 strcpy 구현. while(*d++ = *s++); 패턴의 의미와 종료 조건을 정확히 이해하는지 평가.  
- 요구사항:  
  - dst는 널 종료됨. src는 유효한 C 문자열이어야 함.  
  - dst와 src가 동일 포인터일 때 no-op.  
  - NULL 인자 검증은 정책에 따라 반환 혹은 assert.  
- 체크리스트:  
  - const-correctness: dst는 char*, src는 const char*.  
  - 종료 조건: 복사한 문자가 '\0'이면 루프 종료.  
  - UB 방지: dst 용량이 충분한가? 인터뷰어에 질문.  
- 시그니처/템플릿:  
```c
char* my_strcpy(char* dst, const char* src) {
    // while ((*dst++ = *src++)) { }
    return dst;
}
```

2) my_strcmp: sentinel 비교와 부호 이슈  
- 스토리: while((c=*a++)==(d=*b++) && c); 패턴. 차이값 리턴 규약 이해.  
- 요구사항:  
  - 같으면 0, a>b 양수, a<b 음수.  
  - unsigned char 비교 여부 질문(플랫폼/locale 영향).  
- 체크리스트:  
  - char 서명(signed/unsigned) 차이 질문.  
  - 조기 종료: 첫 차이 또는 '\0'.  
- 시그니처/템플릿:  
```c
int my_strcmp(const char* a, const char* b) {
    // while (*a && *a == *b) { a++; b++; }
    // return (unsigned char)*a - (unsigned char)*b;
    return 0;
}
```



- (참고)
==와 =는 연산자 우선순위가 다르고, 동작 방식도 다릅니다.
1. my_strcpy에서 괜찮은 이유
```c
while (*dst++ = *src++);
```
여기서 =가 먼저 평가됩니다.
*dst++ = *src++의 결과는 대입된 값(즉, 복사된 문자)입니다.
복사된 값이 \0(0)이면 반복이 종료됩니다.
즉, 복사와 포인터 증가가 동시에 잘 동작합니다.


2. my_strcmp에서 문제가 생기는 이유
```c
while(*a && *a++ == *b++);
```
==는 비교 연산자입니다.
*a++ == *b++에서 비교 후 포인터가 증가합니다.
반복문이 끝나면 a와 b가 비교가 끝난 다음 문자(즉, 서로 다른 위치)에 있습니다.
그래서 return (unsigned char)*a - (unsigned char)*b;에서 원하는 위치의 문자가 아닙니다.


3. 연산자 우선순위
=(대입)는 우선순위가 낮고,
==(비교)는 우선순위가 높습니다.
while (*dst++ = *src++);는 괄호 없이도 대입이 먼저 일어나고, 그 결과로 반복이 잘 됩니다.
while (*a++ == *b++);는 비교 후 포인터가 증가하므로, 비교 위치가 달라질 수 있습니다.
결론
strcpy는 대입 결과(값)를 이용하므로 괜찮고,
strcmp는 비교 후 포인터가 증가해서, 반복문 종료 후 위치가 달라져 결과가 달라집니다.
==와 =는 우선순위와 동작 방식이 다르니 주의해야 합니다!







3) my_strncmp: 길이 제한 비교  
- 스토리: 안전 비교. 버퍼 경계 내에서만 비교.  
- 요구사항:  
  - n==0 즉시 0.  
  - '\0' 도달 시 종료.  
- 체크리스트:  
  - 비교 순서: n 감소 타이밍.  
  - 반환 규약 동일.  
- 시그니처/템플릿:  
```c
int my_strncmp(const char* a, const char* b, size_t n) {
    // for (; n && *a && (*a == *b); a++, b++, n--) {}
    // if (n == 0) return 0;
    // return (unsigned char)*a - (unsigned char)*b;
    return 0;
}
```

4) my_strlen: sentinel 스캔 + 성능 변형  
- 스토리: 널 문자를 찾을 때까지 길이 측정.  
- 요구사항:  
  - O(L). NULL 처리 정책 질문.  
- 체크리스트:  
  - 워드 단위 가속(정렬, 0바이트 탐지) 가능 질문.  
- 시그니처/템플릿:  
```c
size_t my_strlen(const char* s) {
    // const char* p = s;
    // while (*p) p++;
    // return (size_t)(p - s);
    return 0;
}
```

5) my_strcpy_s: 용량 지정 안전 복사  
- 스토리: dst 용량이 작은 경우를 안전하게 처리.  
- 요구사항:  
  - dstsz==0 → 오류.  
  - dst는 항상 널 종료되게.  
  - overrun 시 에러 코드 반환.  
- 체크리스트:  
  - 반환 규약: 0=성공, -1=오버런 등.  
- 시그니처/템플릿:  
```c
int my_strcpy_s(char* dst, size_t dstsz, const char* src) {
    // size_t i = 0;
    // if (!dst || !src || dstsz == 0) return -1;
    // for (; i + 1 < dstsz && src[i]; i++) dst[i] = src[i];
    // dst[i] = '\0';
    // return src[i] ? -1 : 0;
    return 0;
}
```

6) my_memcpy: 바이트/워드 복사와 정렬  
- 스토리: 임의의 바이트 시퀀스 복사. 겹침 미지원.  
- 요구사항:  
  - n 바이트 exact copy. 겹침 시 UB임을 문서화.  
- 체크리스트:  
  - alignment 최적화 질문(워드 복사 → 바이트 테일).  
- 시그니처/템플릿:  
```c
void* my_memcpy(void* dst, const void* src, size_t n) {
    // uint8_t* d = dst; const uint8_t* s = src;
    // while (n--) *d++ = *s++;
    // return dst;
    return NULL;
}
```

7) my_memmove: 겹침 안전 복사  
- 스토리: 겹치는 영역에서 방향 선택으로 안전 복사.  
- 요구사항:  
  - dst<src → forward, dst>src → backward.  
- 체크리스트:  
  - 동일 포인터 경우 최적화.  
  - 포인터 차이의 부호 주의.  
- 시그니처/템플릿:  
```c
void* my_memmove(void* dst, const void* src, size_t n) {
    // if (dst == src || n == 0) return dst;
    // if (dst < src) { // forward
    //   uint8_t* d = dst; const uint8_t* s = src; while (n--) *d++ = *s++;
    // } else { // backward
    //   uint8_t* d = (uint8_t*)dst + n; const uint8_t* s = (const uint8_t*)src + n;
    //   while (n--) *--d = *--s;
    // }
    // return dst;
    return NULL;
}
```

8) my_memcmp: 바이트 비교와 조기 종료, XOR 아이디어  
- 스토리: 바이너리 비교. 첫 차이에서 종료.  
- 요구사항:  
  - 0=동일, <0 / >0 규약.  
- 체크리스트:  
  - 성능: 워드 비교 → 바이트 테일, XOR로 차이 탐지.  
- 시그니처/템플릿:  
```c
int my_memcmp(const void* a, const void* b, size_t n) {
    // const uint8_t* x = a; const uint8_t* y = b;
    // for (; n; x++, y++, n--) { if (*x != *y) return *x - *y; }
    // return 0;
    return 0;
}
```

9) my_strchr / my_strrchr: 문자 탐색  
- 스토리: 문자열에서 문자 첫/마지막 위치 찾기.  
- 요구사항:  
  - 검색 문자가 '\0'일 때 동작 정의.  
- 체크리스트:  
  - 반환형은 char* (const 변형 주의).  
- 시그니처/템플릿:  
```c
char* my_strchr(const char* s, int c) {
    // for (; *s; s++) if ((unsigned char)*s == (unsigned char)c) return (char*)s;
    // return (c == '\0') ? (char*)s : NULL;
    return NULL;
}

char* my_strrchr(const char* s, int c) {
    // const char* last = NULL;
    // for (; *s; s++) if ((unsigned char)*s == (unsigned char)c) last = s;
    // return (c == '\0') ? (char*)s : (char*)last;
    return NULL;
}
```

10) my_strtok_r (re-entrant 토크나이저)  
- 스토리: strtok의 전역 상태 문제를 피하는 재진입 안전 버전.  
- 요구사항:  
  - 구분자 집합 지원, saveptr로 상태 유지.  
- 체크리스트:  
  - 빈 토큰, 연속 구분자 처리 규칙 확인.  
- 시그니처/템플릿:  
```c
char* my_strtok_r(char* str, const char* delim, char** saveptr) {
    // char* s = str ? str : *saveptr;
    // if (!s) return NULL;
    // s += strspn(s, delim);
    // if (*s == '\0') { *saveptr = NULL; return NULL; }
    // char* end = s + strcspn(s, delim);
    // if (*end) { *end = '\0'; *saveptr = end + 1; } else { *saveptr = NULL; }
    // return s;
    return NULL;
}
```

---

면접 중 질문 포인트(공통)  
- s/t가 진짜 C 문자열인가? 종료 보장? 버퍼 크기? NULL 허용?  
- 연산자 우선순위, 평가 순서, 부작용(s++, *s++) 이해?  
- char의 signedness, locale 영향, 비교 규약?  
- 겹침(overlap) 시语의 정의(memcpy vs memmove)?  
- 성능: do-while vs while, 언롤링, 정렬, 워드/벡터 복사, 분기 예측, prefetch.  
- 안전성: 경계 검사, 반환값/에러 코드, const-correctness, 불변식 문서화.  