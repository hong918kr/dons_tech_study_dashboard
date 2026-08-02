#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/*
66. Define a node struct for a Doubly Linked List.
    Example: dnode_t { data=1, prev=..., next=... }
*/
typedef struct dnode {
    int data;
    struct dnode *prev;
    struct dnode *next;
} dnode_t;

/*
67. Add a node to the front of a doubly linked list.
    Example: Push front 1 to [2, 3] -> [1, 2, 3]
*/
void dlist_push_front(dnode_t **head, int data) {
    // TODO: implement
}

/*
68. Add a node to the end of a doubly linked list.
    Example: Push back 4 to [1, 2, 3] -> [1, 2, 3, 4]
*/
void dlist_push_back(dnode_t **head, int data) {
    // TODO: implement
}

/*
69. Delete a node with a specific value.
    Example: Delete value 2 from [1, 2, 3] -> [1, 3]
*/
void dlist_delete_value(dnode_t **head, int value) {
    // TODO: implement
}

/*
70. Reverse a doubly linked list (in-place).
    Example: Reverse [1, 2, 3] -> [3, 2, 1]
*/
void dlist_reverse(dnode_t **head) {
    // TODO: implement
}

/*
71. Find the middle node of a doubly linked list.
    Example: Middle of [1, 2, 3, 4, 5] -> 3
*/
dnode_t* dlist_find_middle(dnode_t *head) {
    // TODO: implement
    return NULL;
}

/*
72. Detect if a doubly linked list has a cycle.
    Example: [1, 2, 3, 4] with 4->2 cycle -> true
*/
bool dlist_has_cycle(dnode_t *head) {
    // TODO: implement
    return false;
}

/*
73. Merge two sorted doubly linked lists.
    Example: Merge [1, 3, 5] and [2, 4] -> [1, 2, 3, 4, 5]
*/
dnode_t* dlist_merge_sorted(dnode_t *l1, dnode_t *l2) {
    // TODO: implement
    return NULL;
}

/*
74. Find the Nth node from the end of a doubly linked list.
    Example: 2nd from end in [1, 2, 3, 4, 5] -> 4
*/
dnode_t* dlist_nth_from_end(dnode_t *head, size_t n) {
    // TODO: implement
    return NULL;
}

// --- Test code ---
void print_dlist(dnode_t *head) {
    printf("DList: ");
    while (head) {
        printf("%d ", head->data);
        head = head->next;
    }
    printf("\n");
}

void free_dlist(dnode_t *head) {
    while (head) {
        dnode_t *tmp = head;
        head = head->next;
        free(tmp);
    }
}

int main(void) {
    dnode_t *head = NULL;

    printf("Push front 3, 2, 1:\n");
    dlist_push_front(&head, 1);
    dlist_push_front(&head, 2);
    dlist_push_front(&head, 3);
    print_dlist(head);

    printf("Push back 4, 5:\n");
    dlist_push_back(&head, 4);
    dlist_push_back(&head, 5);
    print_dlist(head);

    printf("Delete value 3:\n");
    dlist_delete_value(&head, 3);
    print_dlist(head);

    printf("Reverse list:\n");
    dlist_reverse(&head);
    print_dlist(head);

    printf("Middle node: ");
    dnode_t *mid = dlist_find_middle(head);
    if (mid) printf("%d\n", mid->data);

    printf("N-th from end (n=2): ");
    dnode_t *nth = dlist_nth_from_end(head, 2);
    if (nth) printf("%d\n", nth->data);

    // Merge sorted lists
    dnode_t *l1 = NULL, *l2 = NULL;
    dlist_push_back(&l1, 1);
    dlist_push_back(&l1, 3);
    dlist_push_back(&l1, 5);
    dlist_push_back(&l2, 2);
    dlist_push_back(&l2, 4);
    printf("\nMerge sorted lists:\n");
    dnode_t *merged = dlist_merge_sorted(l1, l2);
    print_dlist(merged);

    // Cycle detection
    printf("Cycle detection: ");
    printf("%s\n", dlist_has_cycle(merged) ? "Cycle" : "No Cycle");

    free_dlist(merged);
    free_dlist(head);

    return 0;
}

/*
------------------ Expected Result ------------------

Push front 3, 2, 1:
DList: 3 2 1 
Push back 4, 5:
DList: 3 2 1 4 5 
Delete value 3:
DList: 2 1 4 5 
Reverse list:
DList: 5 4 1 2 
Middle node: 1
N-th from end (n=2): 1

Merge sorted lists:
DList: 1 2 3 4 5 
Cycle detection: No Cycle
*/