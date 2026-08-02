// 11. 비트필드 플래그 매니저

/*
상황 설명:
당신은 차량용 ECU(전자제어장치) 펌웨어를 개발 중입니다. 이 ECU는 128개의 센서/액추에이터 상태, 진단 플래그, 이벤트 알림 등을 비트 단위로 관리해야 합니다.
예를 들어, 각 비트는 "센서1 이상", "센서2 이상", "CAN 통신 오류", "과전압 감지", "펌웨어 업데이트 필요" 등 다양한 상태를 나타냅니다.
이 플래그들은 실시간 태스크, 진단 루틴, 통신 스택 등 여러 모듈에서 동시에 접근될 수 있습니다.

문제는 다음과 같습니다:
- 플래그는 128개로 고정되어 있으며, 각 플래그는 인덱스(0~127)로 식별됩니다.
- 각 플래그에 대해 set(설정), clear(해제), toggle(반전), query(조회) 연산이 필요합니다.
- 연산은 빠르고, 경계(인덱스) 체크가 반드시 필요합니다.
- (선택) 멀티스레드 환경에서는 원자적(atomic) 연산이 필요할 수 있습니다.
- 마스크 연산을 활용해 효율적으로 구현해야 하며, 불필요한 분기나 반복문을 피해야 합니다.

부연 설명:
이 문제는 임베디드 펌웨어에서 흔히 요구되는 "비트 플래그 집합 관리"의 기본기를 평가합니다.
128비트는 2개의 uint64_t로 표현할 수 있으므로, 비트 연산(shift, mask)을 활용해 O(1)로 접근이 가능합니다.
실제 현업에서는 진단 플래그, 이벤트, 권한, 상태 등 다양한 용도로 비트필드가 쓰이며, 경계 체크와 멀티스레드 안전성(atomicity)도 매우 중요합니다.
테스트 코드는 각 연산(set/clear/toggle/query)에 대해 정상/경계/에러 케이스를 모두 검증해야 합니다.
*/

#ifndef FLAG128_H
#define FLAG128_H
#include <stdint.h>
#include <stdio.h>

typedef struct { uint64_t bits[2]; } flag128_t;

// 플래그 전체 초기화 (모두 0)
void flag128_init(flag128_t* f) {
    if (!f) return;
    f->bits[0] = 0;
    f->bits[1] = 0;
}

// idx(0~127)번째 플래그 set
void flag128_set(flag128_t* f, uint8_t idx) {
    if (!f || idx >= 128) return;
    f->bits[idx / 64] |= ((uint64_t)1 << (idx % 64));
}

// idx(0~127)번째 플래그 clear
void flag128_clear(flag128_t* f, uint8_t idx) {
    if (!f || idx >= 128) return;
    f->bits[idx / 64] &= ~((uint64_t)1 << (idx % 64));
}

// idx(0~127)번째 플래그 toggle
void flag128_toggle(flag128_t* f, uint8_t idx) {
    if (!f || idx >= 128) return;
    f->bits[idx / 64] ^= ((uint64_t)1 << (idx % 64));
}

// idx(0~127)번째 플래그 query (1/0)
int flag128_query(const flag128_t* f, uint8_t idx) {
    if (!f || idx >= 128) return 0;
    return (f->bits[idx / 64] >> (idx % 64)) & 1;
}

// (선택) 원자적 버전은 플랫폼별 atomic 연산 필요 (예: GCC __atomic, C11 atomic 등)
// void flag128_set_atomic(flag128_t* f, uint8_t idx);

#endif

#ifdef FLAG128_TEST_MAIN
#include <assert.h>
int main(void) {
    flag128_t f;
    flag128_init(&f);

    // 기본 상태: 모두 0
    for (int i = 0; i < 128; ++i)
        assert(flag128_query(&f, i) == 0);

    // set/clear/toggle/query 정상 동작
    flag128_set(&f, 0);
    assert(flag128_query(&f, 0) == 1);
    flag128_clear(&f, 0);
    assert(flag128_query(&f, 0) == 0);

    flag128_set(&f, 127);
    assert(flag128_query(&f, 127) == 1);
    flag128_toggle(&f, 127);
    assert(flag128_query(&f, 127) == 0);

    // 여러 비트 set
    flag128_set(&f, 1);
    flag128_set(&f, 64);
    flag128_set(&f, 65);
    assert(flag128_query(&f, 1) == 1);
    assert(flag128_query(&f, 64) == 1);
    assert(flag128_query(&f, 65) == 1);

    // 경계값/에러 케이스
    flag128_set(&f, 128); // 무시
    flag128_clear(&f, 255); // 무시
    assert(flag128_query(&f, 128) == 0);

    // 전체 clear
    flag128_init(&f);
    for (int i = 0; i < 128; ++i)
        assert(flag128_query(&f, i) == 0);

    printf("flag128_manager: 모든 테스트 통과!\n");
    return 0;
}
#endif