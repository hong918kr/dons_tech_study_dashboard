#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/*
66. Define a node struct for a Singly Linked List.
*/
typedef struct node {
    int data;
    struct node *next;
} node_t;

/*
67. Add a node to the front of a linked list.
*/
void list_push_front(node_t **head, int data) {
    node_t *n = (node_t *)malloc(sizeof(node_t));
    if (!n) return;
    n->data = data;
    n->next = *head;
    *head = n;
}

/*
68. Add a node to the end of a linked list.
*/
void list_push_back(node_t **head, int data) {
    node_t *n = (node_t *)malloc(sizeof(node_t));
    if (!n) return;
    n->data = data;
    n->next = NULL;
    if (*head == NULL) {
        *head = n;
        return;
    }
    node_t *cur = *head;
    while (cur->next) {
        cur = cur->next;
    }
    cur->next = n;
}

/*
69. Delete a node with a specific value.
*/
void list_delete_value(node_t **head, int value) {
    node_t *cur = *head;
    node_t *prev = NULL;
    while (cur) {
        if (cur->data == value) {
            if (prev == NULL) {
                *head = cur->next;
            } else {
                prev->next = cur->next;
            }
            free(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

/*
70. Reverse a linked list (in-place).
*/
void list_reverse(node_t **head) {
    node_t *prev = NULL;
    node_t *cur = *head;
    while (cur) {
        node_t *next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
    }
    *head = prev;
}

/*
71. Find the middle node of a linked list.
*/
node_t* list_find_middle(node_t *head) {
    node_t *slow = head;
    node_t *fast = head;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }
    return slow;
}

/*
72. Detect if a linked list has a cycle.
*/
bool list_has_cycle(node_t *head) {
    node_t *slow = head;
    node_t *fast = head;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) {
            return true;
        }
    }
    return false;
}

/*
73. Merge two sorted linked lists.
*/
node_t* list_merge_sorted(node_t *l1, node_t *l2) {
    node_t dummy;
    dummy.next = NULL;
    node_t *tail = &dummy;
    while (l1 && l2) {
        if (l1->data <= l2->data) {
            tail->next = l1;
            l1 = l1->next;
        } else {
            tail->next = l2;
            l2 = l2->next;
        }
        tail = tail->next;
    }
    tail->next = l1 ? l1 : l2;
    return dummy.next;
}

/*
74. Find the Nth node from the end of a linked list.
*/
node_t* list_nth_from_end(node_t *head, size_t n) {
    node_t *lead = head;
    for (size_t i = 0; i < n; i++) {
        if (lead == NULL) {
            return NULL;
        }
        lead = lead->next;
    }
    node_t *trail = head;
    while (lead) {
        lead = lead->next;
        trail = trail->next;
    }
    return trail;
}

/*
75. Implement a linked list without malloc for embedded systems.
*/
// 75
/*
Use a static array as a node pool and manage unused nodes with a free list to implement a linked list without dynamic allocation.
*/
