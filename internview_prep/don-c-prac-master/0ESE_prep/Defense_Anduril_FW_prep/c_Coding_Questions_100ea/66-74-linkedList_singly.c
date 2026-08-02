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
    // TODO: implement
}

/*
68. Add a node to the end of a linked list.
*/
void list_push_back(node_t **head, int data) {
    // TODO: implement
}

/*
69. Delete a node with a specific value.
*/
void list_delete_value(node_t **head, int value) {
    // TODO: implement
}

/*
70. Reverse a linked list (in-place).
*/
void list_reverse(node_t **head) {
    // TODO: implement
}

/*
71. Find the middle node of a linked list.
*/
node_t* list_find_middle(node_t *head) {
    // TODO: implement
    return NULL;
}

/*
72. Detect if a linked list has a cycle.
*/
bool list_has_cycle(node_t *head) {
    // TODO: implement
    return false;
}

/*
73. Merge two sorted linked lists.
*/
node_t* list_merge_sorted(node_t *l1, node_t *l2) {
    // TODO: implement
    return NULL;
}

/*
74. Find the Nth node from the end of a linked list.
*/
node_t* list_nth_from_end(node_t *head, size_t n) {
    // TODO: implement
    return NULL;
}

/*
75. Implement a linked list without malloc for embedded systems.
*/
// 75
/*
Use a static array as a node pool and manage unused nodes with a free list to implement a linked list without dynamic allocation.
*/

// --- Test code ---
void print_list(node_t *head) {
    printf("List: ");
    while (head) {
        printf("%d ", head->data);
        head = head->next;
    }
    printf("\n");
}

int main(void) {
    node_t *head = NULL;

    printf("Push front 3, 2, 1:\n");
    list_push_front(&head, 1);
    list_push_front(&head, 2);
    list_push_front(&head, 3);
    print_list(head);

    printf("Push back 4, 5:\n");
    list_push_back(&head, 4);
    list_push_back(&head, 5);
    print_list(head);

    printf("Delete value 3:\n");
    list_delete_value(&head, 3);
    print_list(head);

    printf("Reverse list:\n");
    list_reverse(&head);
    print_list(head);

    printf("Middle node: ");
    node_t *mid = list_find_middle(head);
    if (mid) printf("%d\n", mid->data);

    printf("N-th from end (n=2): ");
    node_t *nth = list_nth_from_end(head, 2);
    if (nth) printf("%d\n", nth->data);

    // Merge sorted lists
    node_t *l1 = NULL, *l2 = NULL;
    list_push_back(&l1, 1);
    list_push_back(&l1, 3);
    list_push_back(&l1, 5);
    list_push_back(&l2, 2);
    list_push_back(&l2, 4);
    printf("\nMerge sorted lists:\n");
    node_t *merged = list_merge_sorted(l1, l2);
    print_list(merged);

    // Cycle detection
    printf("Cycle detection: ");
    printf("%s\n", list_has_cycle(merged) ? "Cycle" : "No Cycle");

    // Free all nodes
    while (merged) {
        node_t *tmp = merged;
        merged = merged->next;
        free(tmp);
    }
    while (head) {
        node_t *tmp = head;
        head = head->next;
        free(tmp);
    }

    return 0;
}

/*
------------------ Expected Result ------------------

Push front 3, 2, 1:
List: 3 2 1 
Push back 4, 5:
List: 3 2 1 4 5 
Delete value 3:
List: 2 1 4 5 
Reverse list:
List: 5 4 1 2 
Middle node: 1
N-th from end (n=2): 1

Merge sorted lists:
List: 1 2 3 4 5 
Cycle detection:


*/