#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <semaphore.h>

#define MAILBOX_SIZE 10

// Structure to represent a mailbox
typedef struct {
    int buffer[MAILBOX_SIZE];
    int head; // Index for adding items
    int tail; // Index for removing items
    int count; // Number of items in the mailbox
    pthread_mutex_t mutex; // Mutex for protecting shared resources
    sem_t empty; // Semaphore to track empty slots
    sem_t full; // Semaphore to track filled slots
} mailbox_t;

// Function to initialize a mailbox
int mailbox_init(mailbox_t *mailbox) {
    if (pthread_mutex_init(&mailbox->mutex, NULL) != 0) {
        perror("Mutex initialization failed");
        return -1;
    }
    if (sem_init(&mailbox->empty, 0, MAILBOX_SIZE) != 0) {
        perror("Empty semaphore initialization failed");
        pthread_mutex_destroy(&mailbox->mutex);
        return -1;
    }
    if (sem_init(&mailbox->full, 0, 0) != 0) {
        perror("Full semaphore initialization failed");
        pthread_mutex_destroy(&mailbox->mutex);
        sem_destroy(&mailbox->empty);
        return -1;
    }
    mailbox->head = 0;
    mailbox->tail = 0;
    mailbox->count = 0;
    return 0;
}

// Function to send data to the mailbox
int mailbox_send(mailbox_t *mailbox, int data) {
    if (sem_wait(&mailbox->empty) != 0) { // Wait for an empty slot
        perror("Semaphore wait failed");
        return -1;
    }
    if (pthread_mutex_lock(&mailbox->mutex) != 0) { // Lock the mailbox
        perror("Mutex lock failed");
        sem_post(&mailbox->empty); // Release the semaphore if lock fails
        return -1;
    }

    mailbox->buffer[mailbox->head] = data;
    mailbox->head = (mailbox->head + 1) % MAILBOX_SIZE;
    mailbox->count++;

    pthread_mutex_unlock(&mailbox->mutex); // Unlock the mailbox
    sem_post(&mailbox->full); // Signal that a slot has been filled

    return 0;
}

// Function to receive data from the mailbox
int mailbox_receive(mailbox_t *mailbox, int *data) {
    if (sem_wait(&mailbox->full) != 0) { // Wait for a filled slot
        perror("Semaphore wait failed");
        return -1;
    }
    if (pthread_mutex_lock(&mailbox->mutex) != 0) { // Lock the mailbox
        perror("Mutex lock failed");
        sem_post(&mailbox->full); // Release the semaphore if lock fails
        return -1;
    }

    *data = mailbox->buffer[mailbox->tail];
    mailbox->tail = (mailbox->tail + 1) % MAILBOX_SIZE;
    mailbox->count--;

    pthread_mutex_unlock(&mailbox->mutex); // Unlock the mailbox
    sem_post(&mailbox->empty); // Signal that a slot has been emptied

    return 0;
}

// Function to destroy a mailbox
void mailbox_destroy(mailbox_t *mailbox) {
    pthread_mutex_destroy(&mailbox->mutex);
    sem_destroy(&mailbox->empty);
    sem_destroy(&mailbox->full);
}



// Example usage in threads:
void *sender_thread(void *arg) {
    mailbox_t *mailbox = (mailbox_t *)arg;
    for (int i = 0; i < 20; i++) {
        mailbox_send(mailbox, i);
        printf("Sent: %d\n", i);
        sleep(1); // Simulate some work
    }
    pthread_exit(NULL);
}

void *receiver_thread(void *arg) {
    mailbox_t *mailbox = (mailbox_t *)arg;
    int received_data;
    for (int i = 0; i < 20; i++) {
        mailbox_receive(mailbox, &received_data);
        printf("Received: %d\n", received_data);
        sleep(2); // Simulate some work
    }
    pthread_exit(NULL);
}

int main() {
    mailbox_t mailbox;
    if (mailbox_init(&mailbox) != 0) {
        return 1;
    }

    pthread_t sender, receiver;
    pthread_create(&sender, NULL, sender_thread, &mailbox);
    pthread_create(&receiver, NULL, receiver_thread, &mailbox);

    pthread_join(sender, NULL);
    pthread_join(receiver, NULL);

    mailbox_destroy(&mailbox);
    printf("Mailbox example finished.\n");
    return 0;
}