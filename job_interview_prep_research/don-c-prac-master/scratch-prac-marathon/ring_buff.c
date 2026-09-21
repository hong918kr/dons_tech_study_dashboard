#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct Node {
    int data;
    struct Node* next;
} Node;

typedef struct CircularBuffer {
    Node* head;
    Node* tail;
    int size;
    int count;
    pthread_mutex_t mutex;
} CircularBuffer;

CircularBuffer *createCircularBuffer(int size)
{
    CircularBuffer* buffer = (CircularBuffer*)malloc(sizeof(CircularBuffer));
    if (buffer  == NULL)
    {
        exit(1);
    }
    buffer->head = NULL;
    buffer->tail = NULL;
    buffer->size = size;
    buffer->count = 0;
    if (pthread_mutex_init(&buffer->mutex, NULL) != 0) // init mutex
    {
        free(buffer);
        exit(1);
    }
    return buffer;
}

int enqueue(CircularBuffer* buffer, int data) 
{
    pthread_mutex_lock(&buffer->mutex);
    if (buffer->count == buffer->size) {
        pthread_mutex_unlock(&buffer->mutex);
        return -1
    }
    
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (newNode == NULL) 
    {
        pthread_mutex_unlock(&buffer->mutex);
        exit(1);
    }
    newNode->data = data;

    if (buffer->head == NULL)
    {
        buffer->head = newNode;
        buffer->tail = newNode;
        buffer->next = newNode;
    } else {
        newNode->next = buffer->head;
        buffer->tail->next = newNode;
        buffer->tail = newNode;
    }

    buffer->count++;
    pthread_mutex_unlock(&buffer->mutex);
}

int dequeue(CircularBuffer* buffer, int* data)
{
    pthread_mutex_lock(&buffer->mutex);
    
    if (buffer->count == 0 || buffer == NULL)
    {
        pthread_mutex_unlock(&buffer->mutex);
        return -1;
    }

    Node* temp = buffer->head;
    *data = temp->data;

    if (buffer->count == 1)
    {
        buffer->head == NULL;
        buffer->tail == NULL;
    } else {
        buffer->head = temp->next;
        buffer->tail->next = buffer->head;
    }
    free(temp);

    buffer->count--;
    pthread_mutex_unlock(&buffer->mutex);
    return 1;
}

if (current != NULL)
{
    do {
        next = current->next;
        free(current);
        current = next;
    } while (current != buffer->head)
}