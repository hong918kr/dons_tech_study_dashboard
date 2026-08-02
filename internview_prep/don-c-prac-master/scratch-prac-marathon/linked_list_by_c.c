#include <stdio.h>
#include <stdlib.h>

typedef struct ListNode {
    int data;
    struct Node* next; 
} Node;



typedef struct ListNode {
    int data;
    struct Node* next;
} Node;

Node* createNode(int data)
{
    Node *newNode = (Node*) malloc(sizeof(Node));
    if (newNode == NULL) {
        printf("error\n");
        exit(1);
    }
    
    newNode->data = data;
    newNode->next = NULL;
    return newNode;
}

void insertAtBeginning(Node **head)