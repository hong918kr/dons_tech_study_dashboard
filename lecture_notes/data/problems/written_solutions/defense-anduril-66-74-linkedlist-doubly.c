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
    if (head == NULL) return;
    dnode_t *node = (dnode_t *)malloc(sizeof(dnode_t));
    if (node == NULL) return;
    node->data = data;
    node->prev = NULL;
    node->next = *head;
    if (*head != NULL) {
        (*head)->prev = node;
    }
    *head = node;
}

/*
68. Add a node to the end of a doubly linked list.
    Example: Push back 4 to [1, 2, 3] -> [1, 2, 3, 4]
*/
void dlist_push_back(dnode_t **head, int data) {
    if (head == NULL) return;
    dnode_t *node = (dnode_t *)malloc(sizeof(dnode_t));
    if (node == NULL) return;
    node->data = data;
    node->next = NULL;
    if (*head == NULL) {
        node->prev = NULL;
        *head = node;
        return;
    }
    dnode_t *cur = *head;
    while (cur->next != NULL) {
        cur = cur->next;
    }
    cur->next = node;
    node->prev = cur;
}

/*
69. Delete a node with a specific value.
    Example: Delete value 2 from [1, 2, 3] -> [1, 3]
*/
void dlist_delete_value(dnode_t **head, int value) {
    if (head == NULL || *head == NULL) return;
    dnode_t *cur = *head;
    while (cur != NULL && cur->data != value) {
        cur = cur->next;
    }
    if (cur == NULL) return; /* value not found */

    if (cur->prev != NULL) {
        cur->prev->next = cur->next;
    } else {
        /* deleting the head */
        *head = cur->next;
    }
    if (cur->next != NULL) {
        cur->next->prev = cur->prev;
    }
    free(cur);
}

/*
70. Reverse a doubly linked list (in-place).
    Example: Reverse [1, 2, 3] -> [3, 2, 1]
*/
void dlist_reverse(dnode_t **head) {
    if (head == NULL || *head == NULL) return;
    dnode_t *cur = *head;
    dnode_t *newHead = *head;
    while (cur != NULL) {
        dnode_t *tmp = cur->prev;
        cur->prev = cur->next;
        cur->next = tmp;
        newHead = cur;
        cur = cur->prev; /* prev is the old next */
    }
    *head = newHead;
}

/*
71. Find the middle node of a doubly linked list.
    Example: Middle of [1, 2, 3, 4, 5] -> 3
*/
dnode_t* dlist_find_middle(dnode_t *head) {
    dnode_t *slow = head;
    dnode_t *fast = head;
    while (fast != NULL && fast->next != NULL) {
        slow = slow->next;
        fast = fast->next->next;
    }
    return slow;
}

/*
72. Detect if a doubly linked list has a cycle.
    Example: [1, 2, 3, 4] with 4->2 cycle -> true
*/
bool dlist_has_cycle(dnode_t *head) {
    dnode_t *slow = head;
    dnode_t *fast = head;
    while (fast != NULL && fast->next != NULL) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) {
            return true;
        }
    }
    return false;
}

/*
73. Merge two sorted doubly linked lists.
    Example: Merge [1, 3, 5] and [2, 4] -> [1, 2, 3, 4, 5]
*/
dnode_t* dlist_merge_sorted(dnode_t *l1, dnode_t *l2) {
    dnode_t dummy;
    dummy.prev = NULL;
    dummy.next = NULL;
    dnode_t *tail = &dummy;

    while (l1 != NULL && l2 != NULL) {
        if (l1->data <= l2->data) {
            tail->next = l1;
            l1->prev = tail;
            l1 = l1->next;
        } else {
            tail->next = l2;
            l2->prev = tail;
            l2 = l2->next;
        }
        tail = tail->next;
    }
    dnode_t *rest = (l1 != NULL) ? l1 : l2;
    if (rest != NULL) {
        tail->next = rest;
        rest->prev = tail;
    } else {
        tail->next = NULL;
    }

    dnode_t *head = dummy.next;
    if (head != NULL) {
        head->prev = NULL;
    }
    return head;
}

/*
74. Find the Nth node from the end of a doubly linked list.
    Example: 2nd from end in [1, 2, 3, 4, 5] -> 4
*/
dnode_t* dlist_nth_from_end(dnode_t *head, size_t n) {
    if (head == NULL || n == 0) return NULL;
    dnode_t *lead = head;
    size_t i;
    for (i = 0; i < n; i++) {
        if (lead == NULL) return NULL; /* n larger than list length */
        lead = lead->next;
    }
    dnode_t *trail = head;
    while (lead != NULL) {
        lead = lead->next;
        trail = trail->next;
    }
    return trail;
}
