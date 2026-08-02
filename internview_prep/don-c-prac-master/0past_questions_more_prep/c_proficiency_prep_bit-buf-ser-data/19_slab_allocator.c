
// 19. 슬랩 할당기(크기 클래스 3종)
// 설명: 여러 고정 크기 클래스(예: 16, 32, 64바이트)를 관리하는 slab allocator.
// 요구사항:
//   - 각 클래스별 프리 리스트
//   - 내부 단편화(슬랩 내 미사용 공간) 분석
#ifndef SLAB_ALLOC_H
#define SLAB_ALLOC_H
#include <stddef.h>
typedef struct slab_allocator slab_allocator_t;
slab_allocator_t* slab_allocator_create(const size_t* class_sizes, const size_t* class_counts, size_t num_classes);
void* slab_alloc(slab_allocator_t* slab, size_t size);
void  slab_free(slab_allocator_t* slab, void* ptr);
void  slab_allocator_destroy(slab_allocator_t* slab);
#endif
