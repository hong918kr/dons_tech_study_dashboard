#include <stdio.h>
#include <stdlib.h>
typedef struct _node
{
    int data;
    struct _node *next;
} NODE;

void insert_data( NODE *temp, NODE *head)
{
    temp->next = head->next;
    head->next = temp;
}

void reverse( NODE *head)
{
    NODE *prev = head;
    NODE *curr = prev->next;
    NODE *next;
    while (curr != head)
    {
        next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }
}