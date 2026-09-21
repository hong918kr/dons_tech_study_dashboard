# 03. String Functions (문자열 함수) · Q26-40 🔤

> libc 문자열 함수를 **직접** 재구현하는 드릴. Anduril 펌웨어 인터뷰의 단골 —
> "표준 라이브러리 없이 이거 짜보세요"는 사실상 **포인터 + 버퍼 안전 + 널 종료**를
> 제대로 이해하는지 보는 시험이다.

---

## 왜 이 토픽이 인터뷰에 나오나

- 임베디드/펌웨어에서 `<string.h>`는 흔하지만, **바운드 검사가 없다**. 실무 버그의 상당수가
  strcpy/strcat 오버플로, strncpy 널 미종료, strtok 재진입 문제에서 나온다.
- 인터뷰어는 `my_strcpy` 한 줄짜리를 시켜놓고 **엣지케이스**(NULL, 빈 문자열, 버퍼 경계,
  오버플로, 종료 널)를 파고든다. "이거 언제 깨지나요?"가 진짜 질문이다.
- 드론/자율체 펌웨어에서 문자열은 **명령 파싱, 텔레메트리 포맷팅, 로그, NMEA/AT 커맨드**
  등에 쓰인다. 힙이 없거나 제한적이라 **고정 버퍼 + 직접 포맷팅**이 기본이다.

---

## 핵심 개념 치트시트

| 개념 | 요점 |
|---|---|
| **null-termination** | C 문자열은 `'\0'`로 끝난다. 길이는 저장 안 됨 → 매번 스캔. 종료 널 빼먹으면 OOB read. |
| **buffer safety** | dest 버퍼 크기는 **호출자 책임**. `strcpy`/`strcat`은 검사 안 함 → 오버플로 주범. |
| **strncpy 함정** | src ≥ n 이면 **널 종료 안 됨**. 짧으면 나머지를 `'\0'`로 패딩(성능 비용). |
| **unsigned char 비교** | `strcmp`는 `unsigned char`로 비교해야 부호 있는 char 에서 결과 부호가 뒤집히지 않음. |
| **strtok 상태** | `static` 포인터로 위치 기억 → **스레드 안전 X, 재진입 X**. 원본 문자열을 파괴(널 삽입). |
| **overflow(atoi)** | 표준 `atoi`는 오버플로 시 UB. 안전하게 **INT_MAX/INT_MIN saturate** 권장. |
| **INT_MIN abs** | `-INT_MIN`은 오버플로. `unsigned`로 승격 후 `-(long)n` 처리. |
| **in-place 기법** | reverse_words = 전체 뒤집기 → 각 단어 되뒤집기. 추가 버퍼 0. |

---

## 문제별 요점 & 함정

### 26-29. strlen / strcpy / strncpy / strcat
- `strlen`: 포인터 차(`p - s`)로 O(n).
- `strcpy`: `while ((*d++ = *src++));` — 널까지 복사되고 0이 루프를 끝냄.
- `strncpy`: **libc 시맨틱을 정확히** — 짧으면 널 패딩, 길면 종료 안 함. 인터뷰에서 이걸
  물어보면 "그래서 저는 항상 `dst[n-1]='\0'`를 직접 넣습니다"라고 답하면 가점.
- `strcat`: dest 끝(`while(*d) d++;`)까지 간 뒤 거기서부터 복사. **매번 dest 전체 스캔**
  → 반복 strcat 은 O(n²)(이른바 "Shlemiel the painter").

### 30-32. strcmp / strstr / strchr
- `strcmp`: 마지막에 `(unsigned char)*s1 - (unsigned char)*s2`.
- `strstr`: 순진한 O(n·m). 빈 needle → haystack 반환(libc 규약).
- `strchr`: `c`는 `int`지만 `char`로 캐스팅해 비교. `c=='\0'`이면 **종료 널 주소** 반환.

### 33-34. atoi / itoa
- `atoi`: 공백 skip → 부호 → 숫자. 오버플로 **saturate**. 반복마다 clamp 하면 long long 도 안전.
- `itoa`: base 2..16. 뒤에서부터 자리수 뽑아 `tmp`에 담고 **뒤집어** 복사. `INT_MIN`은
  `unsigned`로 처리. base 10 에서만 부호.

### 35-38. case / reverse_words / remove_duplicates / palindrome
- 대소문자: ASCII 범위 체크 후 `± ('a'-'A')`. (로케일/UTF-8 은 범위 밖.)
- reverse_words: **전체 뒤집고 → 단어별 되뒤집기**. 공백 위치 보존, 추가 메모리 없음.
- remove_duplicates: `bool seen[256]` 룩업 + write-index compaction, O(n).
- palindrome: 투 포인터 `i<j`. 빈 문자열은 회문으로 간주.

### 39. strtok
```c
static char *save = NULL;      // ← 상태 유지의 핵심
if (str) save = str;
// 앞쪽 구분자 skip → 토큰 시작 → 다음 구분자를 '\0'로 잘라내고 save 전진
```
- **원본을 파괴**하고 **static** 을 쓴다 → 멀티스레드/중첩 토큰화에서 깨짐.
  실무 대안: `strtok_r`(reentrant), 또는 직접 인덱스 관리.

### 40. tiny sprintf
- `va_list` / `va_start` / `va_arg` / `va_end`. `%d %u %x %X %c %s %%` 지원.
- `%d` 음수는 `-` 출력 후 `unsigned`로 승격(`-(long)v`)해서 자리수 처리.
- 반환값은 **쓴 문자 수(널 제외)**. 실무판은 반드시 **버퍼 크기 인자**(snprintf류)를 받아야 안전.

---

## 인터뷰 팔로업 (자주 나오는 꼬리질문)

1. **"strcpy 어디서 깨지나요?"** → dest 버퍼가 src+1 보다 작으면 오버플로. NULL 포인터. 겹치는 메모리(overlap)는 memmove 영역.
2. **"strncpy 는 왜 위험?"** → 널 종료를 보장 안 함. 이후 strlen/printf 가 OOB read.
3. **"strtok 를 ISR/스레드에서 쓰면?"** → static 상태 공유로 레이스. strtok_r 써야 함.
4. **"atoi vs strtol?"** → strtol 은 에러/오버플로/진법/끝 포인터를 알려줌. 실무는 strtol.
5. **"반복 strcat 이 느린 이유?"** → 매번 끝을 다시 찾음 → O(n²). 끝 포인터를 유지하면 O(n).
6. **"UTF-8 이면?"** → 바이트 길이 ≠ 문자 수. tolower/upper 는 ASCII 전용.

---

## 빌드 & 실행

```sh
# 연습(직접 채우기)
cc -std=c11 -Wall -Wextra problems/03_strings.c -o /tmp/andb_strings && /tmp/andb_strings
# 정답 확인
cc -std=c11 -Wall -Wextra solutions/03_strings.c -o /tmp/andb_strings && /tmp/andb_strings
# 또는
make prob N=03_strings      #  연습
make sol  N=03_strings      #  정답 (전부 PASS)
```

> **핵심 한 줄**: C 문자열 함수의 90%는 "포인터로 널까지 스캔 + 버퍼 크기는 내 책임 +
> 종료 널 잊지 마라"로 요약된다. 나머지는 엣지케이스 방어다.
