#include <stdio.h>
#include <stdlib.h>

// Define the structure for a node in the doubly linked list
typedef struct Node {
    int data;
    struct Node* prev;
    struct Node* next;
} Node;

// Function to create a new node
Node* createNode(int data) {
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (newNode == NULL) {
        printf("Memory allocation failed!\n");
        exit(EXIT_FAILURE);
    }
    newNode->data = data;
    newNode->prev = NULL;
    newNode->next = NULL;
    return newNode;
}

// Function to insert a node at the beginning of the list
Node* insertAtBeginning(Node* head, int data) {
    Node* newNode = createNode(data);
    if (head == NULL) {
        return newNode; // New node is the first and only node
    }
    newNode->next = head;
    head->prev = newNode;
    return newNode; // New node becomes the new head
}

// Function to insert a node at the end of the list
Node* insertAtEnd(Node* head, int data) {
    Node* newNode = createNode(data);
    if (head == NULL) {
        return newNode; // New node is the first and only node
    }
    Node* current = head;
    while (current->next != NULL) {
        current = current->next;
    }
    current->next = newNode;
    newNode->prev = current;
    return head; // Head remains the same
}

// Function to insert a node after a specific node
Node* insertAfterNode(Node* head, int data, int afterData) {
    Node* current = head;
    while (current != NULL && current->data != afterData) {
        current = current->next;
    }

    if (current == NULL) {
        printf("Node with data %d not found in the list.\n", afterData);
        return head;
    }

    Node* newNode = createNode(data);
    newNode->prev = current;
    newNode->next = current->next;
    if (current->next != NULL) {
        current->next->prev = newNode;
    }
    current->next = newNode;
    return head;
}

// Function to delete a node by its data
Node* deleteNode(Node* head, int data) {
    if (head == NULL) {
        printf("List is empty. Cannot delete.\n");
        return NULL;
    }

    Node* current = head;

    // If the node to be deleted is the head
    if (current->data == data) {
        head = current->next;
        if (head != NULL) {
            head->prev = NULL;
        }
        free(current);
        return head;
    }

    // Traverse to find the node
    while (current != NULL && current->data != data) {
        current = current->next;
    }

    // If node not found
    if (current == NULL) {
        printf("Node with data %d not found in the list. Cannot delete.\n", data);
        return head;
    }

    // Adjust pointers of previous and next nodes
    if (current->prev != NULL) {
        current->prev->next = current->next;
    }
    if (current->next != NULL) {
        current->next->prev = current->prev;
    }
    free(current);
    return head;
}

// Function to traverse and print the list from head to tail
void printListForward(Node* head) {
    printf("List (Forward): ");
    Node* current = head;
    while (current != NULL) {
        printf("%d <-> ", current->data);
        current = current->next;
    }
    printf("NULL\n");
}

// Function to traverse and print the list from tail to head
void printListBackward(Node* head) {
    printf("List (Backward): ");
    Node* current = head;
    if (head == NULL) {
        printf("NULL\n");
        return;
    }
    // Go to the last node
    while (current->next != NULL) {
        current = current->next;
    }
    // Traverse backwards
    while (current != NULL) {
        printf("%d <-> ", current->data);
        current = current->prev;
    }
    printf("NULL\n");
}

// Function to free all nodes in the list
void freeList(Node* head) {
    Node* current = head;
    Node* next;
    while (current != NULL) {
        next = current->next;
        free(current);
        current = next;
    }
}

int main() {
    Node* head = NULL; // Initialize an empty list

    // Insertion operations
    head = insertAtBeginning(head, 10); // List: 10
    head = insertAtEnd(head, 20);       // List: 10 <-> 20
    head = insertAtBeginning(head, 5);  // List: 5 <-> 10 <-> 20
    head = insertAtEnd(head, 30);       // List: 5 <-> 10 <-> 20 <-> 30
    head = insertAfterNode(head, 15, 10); // List: 5 <-> 10 <-> 15 <-> 20 <-> 30

    printf("After insertions:\n");
    printListForward(head);
    printListBackward(head);
    printf("\n");

    // Deletion operations
    head = deleteNode(head, 5);  // Delete head: 10 <-> 15 <-> 20 <-> 30
    printf("After deleting 5:\n");
    printListForward(head);
    printListBackward(head);
    printf("\n");

    head = deleteNode(head, 30); // Delete tail: 10 <-> 15 <-> 20
    printf("After deleting 30:\n");
    printListForward(head);
    printListBackward(head);
    printf("\n");

    head = deleteNode(head, 15); // Delete middle: 10 <-> 20
    printf("After deleting 15:\n");
    printListForward(head);
    printListBackward(head);
    printf("\n");

    head = deleteNode(head, 100); // Try to delete non-existent
    printf("After trying to delete 100:\n");
    printListForward(head);
    printListBackward(head);
    printf("\n");

    head = deleteNode(head, 10); // Delete remaining: 20
    printf("After deleting 10:\n");
    printListForward(head);
    printListBackward(head);
    printf("\n");

    head = deleteNode(head, 20); // Delete last node: Empty list
    printf("After deleting 20:\n");
    printListForward(head);
    printListBackward(head);
    printf("\n");

    // Free allocated memory
    freeList(head);
    head = NULL; // Set head to NULL after freeing

    return 0;
}