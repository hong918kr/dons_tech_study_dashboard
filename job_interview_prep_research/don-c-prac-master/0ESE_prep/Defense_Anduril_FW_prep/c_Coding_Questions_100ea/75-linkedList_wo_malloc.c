/*
75. Implement a linked list without malloc for embedded systems.
*/
// 75
/*
Use a static array as a node pool and manage unused nodes with a free list to implement a linked list without dynamic allocation.
*/

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

#define NODE_POOL_SIZE 8

typedef struct node {
    int data;
    struct node *next;
} node_t;

// Node pool and free list management
static node_t node_pool[NODE_POOL_SIZE];
static node_t *free_list = NULL;

// Initialize the node pool and free list
void node_pool_init(void) {
    for (int i = 0; i < NODE_POOL_SIZE - 1; ++i) {
        node_pool[i].next = &node_pool[i + 1];
    }
    node_pool[NODE_POOL_SIZE - 1].next = NULL;
    free_list = &node_pool[0];
}

// Allocate a node from the pool
node_t* node_alloc(void) {
    if (!free_list) return NULL;
    node_t *node = free_list;
    free_list = free_list->next;
    node->next = NULL;
    return node;
}

// Free a node back to the pool
void node_free(node_t *node) {
    node->next = free_list;
    free_list = node;
}

// Add node to front
bool list_push_front(node_t **head, int data) {
    node_t *new_node = node_alloc();
    if (!new_node) return false;
    new_node->data = data;
    new_node->next = *head;
    *head = new_node;
    return true;
}

// Add node to back
bool list_push_back(node_t **head, int data) {
    node_t *new_node = node_alloc();
    if (!new_node) return false;
    new_node->data = data;
    new_node->next = NULL;
    if (!*head) {
        *head = new_node;
        return true;
    }
    node_t *cur = *head;
    while (cur->next) cur = cur->next;
    cur->next = new_node;
    return true;
}

// Delete node with specific value
bool list_delete_value(node_t **head, int value) {
    node_t **cur = head;
    while (*cur) {
        if ((*cur)->data == value) {
            node_t *tmp = *cur;
            *cur = (*cur)->next;
            node_free(tmp);
            return true;
        }
        cur = &((*cur)->next);
    }
    return false;
}

// Print list
void print_list(node_t *head) {
    printf("List: ");
    while (head) {
        printf("%d ", head->data);
        head = head->next;
    }
    printf("\n");
}

// Free all nodes in the list
void list_free_all(node_t **head) {
    while (*head) {
        node_t *tmp = *head;
        *head = (*head)->next;
        node_free(tmp);
    }
}

int main(void) {
    node_pool_init();
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

    printf("Free all nodes:\n");
    list_free_all(&head);
    print_list(head);

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
Free all nodes: */