#include <stdio.h>
#include <stdlib.h>

struct hlist_head {
    struct hlist_node *first;
};

struct hlist_node {
    struct hlist_node *next. **pprev;
};

void hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
    struct hlist_node *first = h->first;
}