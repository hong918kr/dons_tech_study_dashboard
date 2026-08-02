/*
4. 고정 크기 메모리 풀(프리 리스트) - 실제 인터뷰 스타일 상세 설명

상황 설명:
당신은 자동차 ECU(전자제어장치) 펌웨어를 개발 중입니다. 이 ECU는 실시간으로 센서 이벤트, 진단 메시지, CAN 패킷 등 다양한 객체를 동적으로 생성/삭제해야 합니다.
하지만, 임베디드 환경에서는 malloc/free와 같은 일반 동적 메모리 할당은 느리고, 단편화(fragmentation)로 인해 시스템이 불안정해질 수 있습니다.
특히, 실시간 시스템에서는 할당/해제 시간이 예측 가능해야 하며, 메모리 누수나 이중 해제(double free)와 같은 버그도 치명적입니다.

이런 이유로, 실시간 임베디드 시스템에서는 "고정 크기 메모리 풀(fixed-size memory pool)"을 자주 사용합니다.
메모리 풀은 미리 N개의 고정 크기 블록을 할당해두고, 필요할 때마다 빠르게 할당/해제할 수 있습니다.
이 방식은 단편화가 없고, O(1) 시간에 동작하며, 메모리 사용량도 예측 가능합니다.

면접관이 평가하고자 하는 포인트:
- 포인터/메모리 연산의 정확성
- 프리 리스트(Free List) 구조의 이해
- 이중 해제, 잘못된 포인터 해제 등 예외 처리
- API 설계 및 테스트 케이스 작성 능력

문제:
아래 시그니처에 따라 고정 크기 메모리 풀을 구현하세요.
- mempool_create: block_size 크기의 블록 block_count개를 관리하는 풀 생성
- mempool_alloc: 사용 가능한 블록 하나를 할당(없으면 NULL)
- mempool_free: 블록을 풀에 반환(이중 해제, 잘못된 포인터 방지)
- mempool_destroy: 풀 해제

제약:
- block_size는 최소 sizeof(void*) 이상이어야 함
- 내부적으로 프리 리스트(싱글 링크드 리스트)로 관리
- O(1) 할당/해제 보장
- 테스트 코드에서 경계값(풀 가득/비어있음/이중 해제 등) 검증

아래는 구현 예시와 테스트 코드입니다.
*/

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

typedef struct mempool {
    size_t block_size;
    size_t block_count;
    void*  pool_mem;
    void*  free_list;
} mempool_t;

mempool_t* mempool_create(size_t block_size, size_t block_count) {
    if (block_size < sizeof(void*) || block_count == 0) return NULL;
    mempool_t* pool = (mempool_t*)malloc(sizeof(mempool_t));
    if (!pool) return NULL;
    pool->block_size = block_size;
    pool->block_count = block_count;
    pool->pool_mem = malloc(block_size * block_count);
    if (!pool->pool_mem) { free(pool); return NULL; }

    // 프리 리스트 초기화
    uint8_t* p = (uint8_t*)pool->pool_mem;
    for (size_t i = 0; i < block_count - 1; ++i) {
        *(void**)p = p + block_size;
        p += block_size;
    }
    *(void**)p = NULL; // 마지막 블록
    pool->free_list = pool->pool_mem;
    return pool;
}

void* mempool_alloc(mempool_t* pool) {
    if (!pool || !pool->free_list) return NULL;
    void* block = pool->free_list;
    pool->free_list = *(void**)block;
    return block;
}

void mempool_free(mempool_t* pool, void* ptr) {
    if (!pool || !ptr) return;
    // ptr이 pool 내에 있는지 확인 (간단한 경계 체크)
    uint8_t* base = (uint8_t*)pool->pool_mem;
    uint8_t* end  = base + pool->block_size * pool->block_count;
    if ((uint8_t*)ptr < base || (uint8_t*)ptr >= end) return;
    // 정렬 체크
    if (((uintptr_t)((uint8_t*)ptr - base)) % pool->block_size != 0) return;
    // 이중 해제 방지(간단: 이미 free_list에 있으면 안됨, 실제로는 더 복잡한 검증 필요)
    void* cur = pool->free_list;
    while (cur) {
        if (cur == ptr) return; // 이미 free된 블록
        cur = *(void**)cur;
    }
    // 프리 리스트에 추가
    *(void**)ptr = pool->free_list;
    pool->free_list = ptr;
}

void mempool_destroy(mempool_t* pool) {
    if (!pool) return;
    free(pool->pool_mem);
    free(pool);
}

// 테스트 코드
int main() {
    const size_t block_size = 32;
    const size_t block_count = 8;
    mempool_t* pool = mempool_create(block_size, block_count);
    assert(pool);

    void* blocks[block_count];
    // 모든 블록 할당
    for (size_t i = 0; i < block_count; ++i) {
        blocks[i] = mempool_alloc(pool);
        assert(blocks[i]);
        // 블록 메모리 초기화 테스트
        memset(blocks[i], (int)i, block_size);
    }
    // 풀 소진 시 NULL 반환
    assert(mempool_alloc(pool) == NULL);

    // 해제 후 재할당
    for (size_t i = 0; i < block_count; ++i) {
        mempool_free(pool, blocks[i]);
    }
    for (size_t i = 0; i < block_count; ++i) {
        void* p = mempool_alloc(pool);
        assert(p);
        // 재할당된 블록이 원래 풀 범위 내에 있는지 확인
        assert((uint8_t*)p >= (uint8_t*)pool->pool_mem &&
               (uint8_t*)p < (uint8_t*)pool->pool_mem + block_size * block_count);
    }
    // 이중 해제 방지 테스트 (동작은 무시)
    mempool_free(pool, blocks[0]);
    mempool_free(pool, blocks[0]); // 두 번째는 무시

    // 잘못된 포인터 해제 방지 테스트 (동작은 무시)
    int dummy;
    mempool_free(pool, &dummy);

    mempool_destroy(pool);
    printf("All mempool tests passed!\n");
    return 0;
}