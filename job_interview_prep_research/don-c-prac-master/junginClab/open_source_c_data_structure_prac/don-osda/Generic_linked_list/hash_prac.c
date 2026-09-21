#include <stdio.h>
#include <stdlib.h>

struct hlist_head
{
    struct hlist_node *next, **pprev;
}

void hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
    struct hlist_node *first = h->first;
    n->next = first;
    if (first)
        first->pprev = &n->next;
    h->first = n;
    n->pprev = &h->first;
}

#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE *)0)->MEMBER)
#define container_of(ptr, type, member) ({ \
    const typeof( ((type*)0)->member ) *__mptr = (ptr);  \
    (type*)( (char *)__mptr - offsetof(type,member));})

#define HASH_MAX 8
#define GOLDEN_RATIO_PRIME_32 0x9e370001UL

unsigned int hash_32(unsigned int val, unsigned int bits)
{
    unsigned int hash = val * GOLDEN_RATIO_PRIME_32;
    return hash >> (32 - bits);
}

#define pid_hashfn(pid) \
        hash_32( pid, 3 )

typedef struct
{
    int sno;
    struct hlist_node hash;
} SAWON;

int hash_sno (int sno)
{
    return hash_32( sno, 3);
}
